/*
 * Copyright (c) 2008-2012 Travis Geiselbrecht
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <debug.h>
#include <rand.h>
#include <err.h>
#include <app/tests.h>
#include <kernel/thread.h>
#include <kernel/mutex.h>
#include <kernel/semaphore.h>
#include <kernel/event.h>
#include <platform.h>

static int sleep_thread(void *arg)
{
	for (;;) {
		printf("sleeper %p\n", current_thread);
		thread_sleep(rand() % 500);
	}
	return 0;
}

int sleep_test(void)
{
	int i;
	for (i=0; i < 16; i++)
		thread_resume(thread_create("sleeper", &sleep_thread, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	return 0;
}

static semaphore_t sem;
static const int sem_total_its = 10000;
static const int sem_thread_max_its = 1000;
static const int sem_start_value = 10;
static int sem_remaining_its = 0;
static int sem_threads = 0;
static mutex_t sem_test_mutex;

static int semaphore_producer()
{
	printf("semaphore producer %p starting up, running for %d iterations\n", current_thread, sem_total_its);

	for (int x = 0; x < sem_total_its; x++) {
		sem_post(&sem);
	}

	return 0;
}

static int semaphore_consumer()
{
	unsigned int iterations = 0;

	mutex_acquire(&sem_test_mutex);
	if (sem_remaining_its >= sem_thread_max_its) {
		iterations = rand();
		iterations %= sem_thread_max_its;
	} else {
		iterations = sem_remaining_its;
	}
	sem_remaining_its -= iterations;
	mutex_release(&sem_test_mutex);

	printf("semaphore consumer %p starting up, running for %u iterations\n", current_thread, iterations);
	for (unsigned int x = 0; x < iterations; x++)
		sem_wait(&sem);
	printf("semaphore consumer %p done\n", current_thread);
	atomic_add(&sem_threads, -1);
	return 0;
}

static int semaphore_test()
{
	sem_init(&sem, sem_start_value);
	mutex_init(&sem_test_mutex);

	sem_remaining_its = sem_total_its;
	while (1) {
		mutex_acquire(&sem_test_mutex);
		if (sem_remaining_its) {
			thread_resume(thread_create("semaphore consumer", &semaphore_consumer, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
			atomic_add(&sem_threads, 1);
		} else {
			mutex_release(&sem_test_mutex);
			break;
		}
		mutex_release(&sem_test_mutex);
	}

	thread_resume(thread_create("semaphore producer", &semaphore_producer, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));

	while (sem_threads)
		thread_yield();

	if (sem.count == sem_start_value)
		printf("semaphore tests successfully complete\n");
	else
		printf("semaphore tests failed: %d != %d\n", sem.count, sem_start_value);

	sem_destroy(&sem);
	mutex_destroy(&sem_test_mutex);

	return 0;
}


static volatile int shared = 0;
static mutex_t m;
static volatile int mutex_thread_count = 0;

static int mutex_thread(void *arg)
{
	int i;
	const int iterations = 10000;

	atomic_add(&mutex_thread_count, 1);

	printf("mutex tester thread %p starting up, will go for %d iterations\n", current_thread, iterations);

	for (i = 0; i < iterations; i++) {
		mutex_acquire(&m);

		if (shared != 0)
			panic("someone else has messed with the shared data\n");

		shared = (int)current_thread;
		thread_yield();
		shared = 0;

		mutex_release(&m);
		thread_yield();
	}
	atomic_add(&mutex_thread_count, -1);

	return 0;
}

static int mutex_timeout_thread(void *arg)
{
	mutex_t *timeout_mutex = (mutex_t *)arg;
	status_t err;

	printf("mutex_timeout_thread acquiring mutex %p with 1 second timeout\n", timeout_mutex);
	err = mutex_acquire_timeout(timeout_mutex, 1000);
	if (err == ERR_TIMED_OUT)
		printf("mutex_acquire_timeout returns with TIMEOUT\n");
	else
		printf("mutex_acquire_timeout returns %d\n", err);

	return err;
}

static int mutex_zerotimeout_thread(void *arg)
{
	mutex_t *timeout_mutex = (mutex_t *)arg;
	status_t err;

	printf("mutex_zerotimeout_thread acquiring mutex %p with zero second timeout\n", timeout_mutex);
	err = mutex_acquire_timeout(timeout_mutex, 0);
	if (err == ERR_TIMED_OUT)
		printf("mutex_acquire_timeout returns with TIMEOUT\n");
	else
		printf("mutex_acquire_timeout returns %d\n", err);

	return err;
}

int mutex_test(void)
{
	mutex_init(&m);

	int i;
	for (i=0; i < 5; i++)
		thread_resume(thread_create("mutex tester", &mutex_thread, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));

	thread_sleep(1000);

	while (mutex_thread_count > 0)
		thread_yield();

	printf("done with simple mutex tests\n");

	printf("testing mutex timeout\n");

	mutex_t timeout_mutex;

	mutex_init(&timeout_mutex);
	mutex_acquire(&timeout_mutex);

	for (i=0; i < 2; i++)
		thread_resume(thread_create("mutex timeout tester", &mutex_timeout_thread, (void *)&timeout_mutex, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	for (i=0; i < 2; i++)
		thread_resume(thread_create("mutex timeout tester", &mutex_zerotimeout_thread, (void *)&timeout_mutex, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));

	thread_sleep(5000);
	mutex_release(&timeout_mutex);

	printf("done with mutex tests\n");

	mutex_destroy(&timeout_mutex);

	return 0;
}

static event_t e;

static int event_signaller(void *arg)
{
	printf("event signaller pausing\n");
	thread_sleep(1000);

//	for (;;) {
		printf("signalling event\n");
		event_signal(&e, true);
		printf("done signalling event\n");
		thread_yield();
//	}

	return 0;
}

static int event_waiter(void *arg)
{
	int count = (int)arg;

	printf("event waiter starting\n");

	while (count > 0) {
		printf("%p: waiting on event...\n", current_thread);
		if (event_wait(&e) < 0) {
			printf("%p: event_wait() returned error\n", current_thread);
			return -1;
		}
		printf("%p: done waiting on event...\n", current_thread);
		thread_yield();
		count--;
	}

	return 0;
}

void event_test(void)
{
	printf("event tests starting\n");

	/* make sure signalling the event wakes up all the threads */
	event_init(&e, false, 0);
	thread_resume(thread_create("event signaller", &event_signaller, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("event waiter 0", &event_waiter, (void *)2, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("event waiter 1", &event_waiter, (void *)2, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("event waiter 2", &event_waiter, (void *)2, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("event waiter 3", &event_waiter, (void *)2, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_sleep(2000);
	printf("destroying event\n");
	event_destroy(&e);
	thread_sleep(1000);

	/* make sure signalling the event wakes up precisely one thread */
	event_init(&e, false, EVENT_FLAG_AUTOUNSIGNAL);
	thread_resume(thread_create("event signaller", &event_signaller, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("event waiter 0", &event_waiter, (void *)99, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("event waiter 1", &event_waiter, (void *)99, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("event waiter 2", &event_waiter, (void *)99, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("event waiter 3", &event_waiter, (void *)99, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_sleep(2000);
	event_destroy(&e);

	printf("event tests done\n");
}

static int quantum_tester(void *arg)
{
	for (;;) {
		printf("%p: in this thread. rq %d\n", current_thread, current_thread->remaining_quantum);
	}
	return 0;
}

void quantum_test(void)
{
	thread_resume(thread_create("quantum tester 0", &quantum_tester, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("quantum tester 1", &quantum_tester, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("quantum tester 2", &quantum_tester, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("quantum tester 3", &quantum_tester, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
}

static event_t context_switch_event;
static event_t context_switch_done_event;

static int context_switch_tester(void *arg)
{
	int i;
	uint total_count = 0;
	const int iter = 100000;
	int thread_count = (int)arg;

	event_wait(&context_switch_event);

	uint count = arch_cycle_count();
	for (i = 0; i < iter; i++) {
		thread_yield();
	}
	total_count += arch_cycle_count() - count;
	thread_sleep(1000);
	printf("took %u cycles to yield %d times, %u per yield, %u per yield per thread\n",
	       total_count, iter, total_count / iter, total_count / iter / thread_count);

	event_signal(&context_switch_done_event, true);

	return 0;
}

void context_switch_test(void)
{
	event_init(&context_switch_event, false, 0);
	event_init(&context_switch_done_event, false, 0);

	thread_resume(thread_create("context switch idle", &context_switch_tester, (void *)1, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_sleep(100);
	event_signal(&context_switch_event, true);
	event_wait(&context_switch_done_event);
	thread_sleep(100);

	event_unsignal(&context_switch_event);
	event_unsignal(&context_switch_done_event);
	thread_resume(thread_create("context switch 2a", &context_switch_tester, (void *)2, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("context switch 2b", &context_switch_tester, (void *)2, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_sleep(100);
	event_signal(&context_switch_event, true);
	event_wait(&context_switch_done_event);
	thread_sleep(100);

	event_unsignal(&context_switch_event);
	event_unsignal(&context_switch_done_event);
	thread_resume(thread_create("context switch 4a", &context_switch_tester, (void *)4, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("context switch 4b", &context_switch_tester, (void *)4, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("context switch 4c", &context_switch_tester, (void *)4, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("context switch 4d", &context_switch_tester, (void *)4, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
	thread_sleep(100);
	event_signal(&context_switch_event, true);
	event_wait(&context_switch_done_event);
	thread_sleep(100);
}

static volatile int atomic;
static volatile int atomic_count;

static int atomic_tester(void *arg)
{
	int add = (int)arg;
	int i;

	TRACEF("add %d\n", add);

	for (i=0; i < 1000000; i++) {
		atomic_add(&atomic, add);
	}

	int old = atomic_add(&atomic_count, -1);
	TRACEF("exiting, old count %d\n", old);

	return 0;
}

static void atomic_test(void)
{
	atomic = 0;
	atomic_count = 8;

	printf("testing atomic routines\n");

	thread_resume(thread_create("atomic tester 1", &atomic_tester, (void *)1, LOW_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("atomic tester 1", &atomic_tester, (void *)1, LOW_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("atomic tester 1", &atomic_tester, (void *)1, LOW_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("atomic tester 1", &atomic_tester, (void *)1, LOW_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("atomic tester 2", &atomic_tester, (void *)-1, LOW_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("atomic tester 2", &atomic_tester, (void *)-1, LOW_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("atomic tester 2", &atomic_tester, (void *)-1, LOW_PRIORITY, DEFAULT_STACK_SIZE));
	thread_resume(thread_create("atomic tester 2", &atomic_tester, (void *)-1, LOW_PRIORITY, DEFAULT_STACK_SIZE));

	while (atomic_count > 0) {
		thread_sleep(1);
	}

	printf("atomic count == %d (should be zero)\n", atomic);
}

static volatile int preempt_count;

static int preempt_tester(void *arg)
{
#define COUNT (8*1024*1024)

	int i;
	for (i = 0; i < COUNT; i++)
		__asm__ volatile("nop");

	printf("exiting ts %lld\n", current_time_hires());

	atomic_add(&preempt_count, -1);

	return 0;
}

static void preempt_test(void)
{
	printf("testing preemption\n");

	preempt_count = 5;

	int i;
	for (i = 0; i < preempt_count; i++)
		thread_resume(thread_create("preempt tester", &preempt_tester, NULL, LOW_PRIORITY, DEFAULT_STACK_SIZE));

	while (preempt_count > 0) {
		thread_sleep(1000);
	}

	printf("done with preempt test, above time stamps should be very close\n");
}

int thread_tests(void)
{
	mutex_test();
	semaphore_test();
	event_test();

	atomic_test();

	thread_sleep(200);
	context_switch_test();

	preempt_test();

	return 0;
}

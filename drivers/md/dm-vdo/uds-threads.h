/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_THREADS_H
#define UDS_THREADS_H

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/wait.h>

#include "errors.h"
#include "time-utils.h"

/* Thread and synchronization utilities for UDS */

struct cond_var {
	wait_queue_head_t wait_queue;
};

struct thread;

struct barrier {
	/* Mutex for this barrier object */
	struct semaphore mutex;
	/* Semaphore for threads waiting at the barrier */
	struct semaphore wait;
	/* Number of threads which have arrived */
	int arrived;
	/* Total number of threads using this barrier */
	int thread_count;
};

int __must_check uds_create_thread(void (*thread_function)(void *), void *thread_data,
				   const char *name, struct thread **new_thread);

void uds_perform_once(atomic_t *once_state, void (*function) (void));

int uds_join_threads(struct thread *thread);

int __must_check uds_initialize_barrier(struct barrier *barrier,
					unsigned int thread_count);
int uds_destroy_barrier(struct barrier *barrier);
int uds_enter_barrier(struct barrier *barrier);

int __must_check uds_init_cond(struct cond_var *cond);
int uds_signal_cond(struct cond_var *cond);
int uds_broadcast_cond(struct cond_var *cond);
int uds_wait_cond(struct cond_var *cond, struct mutex *mutex);
int uds_destroy_cond(struct cond_var *cond);

static inline int __must_check uds_init_mutex(struct mutex *mutex)
{
	mutex_init(mutex);
	return UDS_SUCCESS;
}

static inline int uds_destroy_mutex(struct mutex *mutex)
{
	return UDS_SUCCESS;
}

static inline void uds_lock_mutex(struct mutex *mutex)
{
	mutex_lock(mutex);
}

static inline void uds_unlock_mutex(struct mutex *mutex)
{
	mutex_unlock(mutex);
}

static inline int __must_check uds_initialize_semaphore(struct semaphore *semaphore,
							unsigned int value)
{
	sema_init(semaphore, value);
	return UDS_SUCCESS;
}

static inline int uds_destroy_semaphore(struct semaphore *semaphore)
{
	return UDS_SUCCESS;
}

static inline void uds_acquire_semaphore(struct semaphore *semaphore)
{
	/*
	 * Do not use down(semaphore). Instead use down_interruptible so that
	 * we do not get 120 second stall messages in kern.log.
	 */
	while (down_interruptible(semaphore) != 0) {
		/*
		 * If we're called from a user-mode process (e.g., "dmsetup
		 * remove") while waiting for an operation that may take a
		 * while (e.g., UDS index save), and a signal is sent (SIGINT,
		 * SIGUSR2), then down_interruptible will not block. If that
		 * happens, sleep briefly to avoid keeping the CPU locked up in
		 * this loop. We could just call cond_resched, but then we'd
		 * still keep consuming CPU time slices and swamp other threads
		 * trying to do computational work. [VDO-4980]
		 */
		fsleep(1000);
	}
}

static inline void uds_release_semaphore(struct semaphore *semaphore)
{
	up(semaphore);
}

#endif /* UDS_THREADS_H */

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
	/* Lock for this barrier object */
	struct semaphore lock;
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


#endif /* UDS_THREADS_H */

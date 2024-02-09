/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef THREAD_UTILS_H
#define THREAD_UTILS_H

#include <linux/atomic.h>

/* Thread and synchronization utilities */

struct thread;


int __must_check vdo_create_thread(void (*thread_function)(void *), void *thread_data,
				   const char *name, struct thread **new_thread);
void vdo_join_threads(struct thread *thread);

void vdo_perform_once(atomic_t *once_state, void (*function) (void));

#endif /* UDS_THREADS_H */

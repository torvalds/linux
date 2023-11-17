/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_THREAD_REGISTRY_H
#define UDS_THREAD_REGISTRY_H

#include <linux/list.h>
#include <linux/spinlock.h>

struct thread_registry {
	struct list_head links;
	spinlock_t lock;
};

struct registered_thread {
	struct list_head links;
	const void *pointer;
	struct task_struct *task;
};

void uds_initialize_thread_registry(struct thread_registry *registry);

void uds_register_thread(struct thread_registry *registry,
			 struct registered_thread *new_thread, const void *pointer);

void uds_unregister_thread(struct thread_registry *registry);

const void *uds_lookup_thread(struct thread_registry *registry);

#endif /* UDS_THREAD_REGISTRY_H */

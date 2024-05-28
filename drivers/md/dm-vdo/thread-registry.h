/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_THREAD_REGISTRY_H
#define VDO_THREAD_REGISTRY_H

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

void vdo_initialize_thread_registry(struct thread_registry *registry);

void vdo_register_thread(struct thread_registry *registry,
			 struct registered_thread *new_thread, const void *pointer);

void vdo_unregister_thread(struct thread_registry *registry);

const void *vdo_lookup_thread(struct thread_registry *registry);

#endif /* VDO_THREAD_REGISTRY_H */

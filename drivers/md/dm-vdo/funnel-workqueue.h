/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_WORK_QUEUE_H
#define VDO_WORK_QUEUE_H

#include <linux/sched.h> /* for TASK_COMM_LEN */

#include "types.h"

enum {
	MAX_VDO_WORK_QUEUE_NAME_LEN = TASK_COMM_LEN,
};

struct vdo_work_queue_type {
	void (*start)(void *context);
	void (*finish)(void *context);
	enum vdo_completion_priority max_priority;
	enum vdo_completion_priority default_priority;
};

struct vdo_completion;
struct vdo_thread;
struct vdo_work_queue;

int vdo_make_work_queue(const char *thread_name_prefix, const char *name,
			struct vdo_thread *owner, const struct vdo_work_queue_type *type,
			unsigned int thread_count, void *thread_privates[],
			struct vdo_work_queue **queue_ptr);

void vdo_enqueue_work_queue(struct vdo_work_queue *queue, struct vdo_completion *completion);

void vdo_finish_work_queue(struct vdo_work_queue *queue);

void vdo_free_work_queue(struct vdo_work_queue *queue);

void vdo_dump_work_queue(struct vdo_work_queue *queue);

void vdo_dump_completion_to_buffer(struct vdo_completion *completion, char *buffer,
				   size_t length);

void *vdo_get_work_queue_private_data(void);
struct vdo_work_queue *vdo_get_current_work_queue(void);
struct vdo_thread *vdo_get_work_queue_owner(struct vdo_work_queue *queue);

bool __must_check vdo_work_queue_type_is(struct vdo_work_queue *queue,
					 const struct vdo_work_queue_type *type);

#endif /* VDO_WORK_QUEUE_H */

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#include "vchiq_util.h"

static inline int is_pow2(int i)
{
	return i && !(i & (i - 1));
}

int vchiu_queue_init(struct vchiu_queue *queue, int size)
{
	WARN_ON(!is_pow2(size));

	queue->size = size;
	queue->read = 0;
	queue->write = 0;
	queue->initialized = 1;

	init_completion(&queue->pop);
	init_completion(&queue->push);

	queue->storage = kcalloc(size, sizeof(struct vchiq_header *),
				 GFP_KERNEL);
	if (!queue->storage) {
		vchiu_queue_delete(queue);
		return -ENOMEM;
	}
	return 0;
}

void vchiu_queue_delete(struct vchiu_queue *queue)
{
	kfree(queue->storage);
}

int vchiu_queue_is_empty(struct vchiu_queue *queue)
{
	return queue->read == queue->write;
}

void vchiu_queue_push(struct vchiu_queue *queue, struct vchiq_header *header)
{
	if (!queue->initialized)
		return;

	while (queue->write == queue->read + queue->size) {
		if (wait_for_completion_interruptible(&queue->pop))
			flush_signals(current);
	}

	queue->storage[queue->write & (queue->size - 1)] = header;
	queue->write++;

	complete(&queue->push);
}

struct vchiq_header *vchiu_queue_peek(struct vchiu_queue *queue)
{
	while (queue->write == queue->read) {
		if (wait_for_completion_interruptible(&queue->push))
			flush_signals(current);
	}

	complete(&queue->push); // We haven't removed anything from the queue.

	return queue->storage[queue->read & (queue->size - 1)];
}

struct vchiq_header *vchiu_queue_pop(struct vchiu_queue *queue)
{
	struct vchiq_header *header;

	while (queue->write == queue->read) {
		if (wait_for_completion_interruptible(&queue->push))
			flush_signals(current);
	}

	header = queue->storage[queue->read & (queue->size - 1)];
	queue->read++;

	complete(&queue->pop);

	return header;
}

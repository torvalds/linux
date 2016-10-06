/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "vchiq_util.h"
#include "vchiq_killable.h"

static inline int is_pow2(int i)
{
	return i && !(i & (i - 1));
}

int vchiu_queue_init(VCHIU_QUEUE_T *queue, int size)
{
	WARN_ON(!is_pow2(size));

	queue->size = size;
	queue->read = 0;
	queue->write = 0;
	queue->initialized = 1;

	sema_init(&queue->pop, 0);
	sema_init(&queue->push, 0);

	queue->storage = kzalloc(size * sizeof(VCHIQ_HEADER_T *), GFP_KERNEL);
	if (queue->storage == NULL) {
		vchiu_queue_delete(queue);
		return 0;
	}
	return 1;
}

void vchiu_queue_delete(VCHIU_QUEUE_T *queue)
{
	if (queue->storage != NULL)
		kfree(queue->storage);
}

int vchiu_queue_is_empty(VCHIU_QUEUE_T *queue)
{
	return queue->read == queue->write;
}

int vchiu_queue_is_full(VCHIU_QUEUE_T *queue)
{
	return queue->write == queue->read + queue->size;
}

void vchiu_queue_push(VCHIU_QUEUE_T *queue, VCHIQ_HEADER_T *header)
{
	if (!queue->initialized)
		return;

	while (queue->write == queue->read + queue->size) {
		if (down_interruptible(&queue->pop) != 0) {
			flush_signals(current);
		}
	}

	/*
	 * Write to queue->storage must be visible after read from
	 * queue->read
	 */
	smp_mb();

	queue->storage[queue->write & (queue->size - 1)] = header;

	/*
	 * Write to queue->storage must be visible before write to
	 * queue->write
	 */
	smp_wmb();

	queue->write++;

	up(&queue->push);
}

VCHIQ_HEADER_T *vchiu_queue_peek(VCHIU_QUEUE_T *queue)
{
	while (queue->write == queue->read) {
		if (down_interruptible(&queue->push) != 0) {
			flush_signals(current);
		}
	}

	up(&queue->push); // We haven't removed anything from the queue.

	/*
	 * Read from queue->storage must be visible after read from
	 * queue->write
	 */
	smp_rmb();

	return queue->storage[queue->read & (queue->size - 1)];
}

VCHIQ_HEADER_T *vchiu_queue_pop(VCHIU_QUEUE_T *queue)
{
	VCHIQ_HEADER_T *header;

	while (queue->write == queue->read) {
		if (down_interruptible(&queue->push) != 0) {
			flush_signals(current);
		}
	}

	/*
	 * Read from queue->storage must be visible after read from
	 * queue->write
	 */
	smp_rmb();

	header = queue->storage[queue->read & (queue->size - 1)];

	/*
	 * Read from queue->storage must be visible before write to
	 * queue->read
	 */
	smp_mb();

	queue->read++;

	up(&queue->pop);

	return header;
}

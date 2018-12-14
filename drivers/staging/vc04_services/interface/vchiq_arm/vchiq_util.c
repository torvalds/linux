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
		return 0;
	}
	return 1;
}

void vchiu_queue_delete(struct vchiu_queue *queue)
{
	kfree(queue->storage);
}

int vchiu_queue_is_empty(struct vchiu_queue *queue)
{
	return queue->read == queue->write;
}

int vchiu_queue_is_full(struct vchiu_queue *queue)
{
	return queue->write == queue->read + queue->size;
}

void vchiu_queue_push(struct vchiu_queue *queue, struct vchiq_header *header)
{
	if (!queue->initialized)
		return;

	while (queue->write == queue->read + queue->size) {
		if (wait_for_completion_killable(&queue->pop))
			flush_signals(current);
	}

	queue->storage[queue->write & (queue->size - 1)] = header;
	queue->write++;

	complete(&queue->push);
}

struct vchiq_header *vchiu_queue_peek(struct vchiu_queue *queue)
{
	while (queue->write == queue->read) {
		if (wait_for_completion_killable(&queue->push))
			flush_signals(current);
	}

	complete(&queue->push); // We haven't removed anything from the queue.

	return queue->storage[queue->read & (queue->size - 1)];
}

struct vchiq_header *vchiu_queue_pop(struct vchiu_queue *queue)
{
	struct vchiq_header *header;

	while (queue->write == queue->read) {
		if (wait_for_completion_killable(&queue->push))
			flush_signals(current);
	}

	header = queue->storage[queue->read & (queue->size - 1)];
	queue->read++;

	complete(&queue->pop);

	return header;
}

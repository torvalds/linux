/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/slab.h>
#include "kfd_priv.h"

void print_queue_properties(struct queue_properties *q)
{
	if (!q)
		return;

	pr_debug("Printing queue properties:\n");
	pr_debug("Queue Type: %u\n", q->type);
	pr_debug("Queue Size: %llu\n", q->queue_size);
	pr_debug("Queue percent: %u\n", q->queue_percent);
	pr_debug("Queue Address: 0x%llX\n", q->queue_address);
	pr_debug("Queue Id: %u\n", q->queue_id);
	pr_debug("Queue Process Vmid: %u\n", q->vmid);
	pr_debug("Queue Read Pointer: 0x%p\n", q->read_ptr);
	pr_debug("Queue Write Pointer: 0x%p\n", q->write_ptr);
	pr_debug("Queue Doorbell Pointer: 0x%p\n", q->doorbell_ptr);
	pr_debug("Queue Doorbell Offset: %u\n", q->doorbell_off);
}

void print_queue(struct queue *q)
{
	if (!q)
		return;
	pr_debug("Printing queue:\n");
	pr_debug("Queue Type: %u\n", q->properties.type);
	pr_debug("Queue Size: %llu\n", q->properties.queue_size);
	pr_debug("Queue percent: %u\n", q->properties.queue_percent);
	pr_debug("Queue Address: 0x%llX\n", q->properties.queue_address);
	pr_debug("Queue Id: %u\n", q->properties.queue_id);
	pr_debug("Queue Process Vmid: %u\n", q->properties.vmid);
	pr_debug("Queue Read Pointer: 0x%p\n", q->properties.read_ptr);
	pr_debug("Queue Write Pointer: 0x%p\n", q->properties.write_ptr);
	pr_debug("Queue Doorbell Pointer: 0x%p\n", q->properties.doorbell_ptr);
	pr_debug("Queue Doorbell Offset: %u\n", q->properties.doorbell_off);
	pr_debug("Queue MQD Address: 0x%p\n", q->mqd);
	pr_debug("Queue MQD Gart: 0x%llX\n", q->gart_mqd_addr);
	pr_debug("Queue Process Address: 0x%p\n", q->process);
	pr_debug("Queue Device Address: 0x%p\n", q->device);
}

int init_queue(struct queue **q, const struct queue_properties *properties)
{
	struct queue *tmp;

	BUG_ON(!q);

	tmp = kzalloc(sizeof(struct queue), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	memcpy(&tmp->properties, properties, sizeof(struct queue_properties));

	*q = tmp;
	return 0;
}

void uninit_queue(struct queue *q)
{
	kfree(q);
}

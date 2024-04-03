/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
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

#ifndef KFD_KERNEL_QUEUE_H_
#define KFD_KERNEL_QUEUE_H_

#include <linux/list.h>
#include <linux/types.h>
#include "kfd_priv.h"

/**
 * kq_acquire_packet_buffer: Returns a pointer to the location in the kernel
 * queue ring buffer where the calling function can write its packet. It is
 * Guaranteed that there is enough space for that packet. It also updates the
 * pending write pointer to that location so subsequent calls to
 * acquire_packet_buffer will get a correct write pointer
 *
 * kq_submit_packet: Update the write pointer and doorbell of a kernel queue.
 *
 * kq_rollback_packet: This routine is called if we failed to build an acquired
 * packet for some reason. It just overwrites the pending wptr with the current
 * one
 *
 */

int kq_acquire_packet_buffer(struct kernel_queue *kq,
				size_t packet_size_in_dwords,
				unsigned int **buffer_ptr);
int kq_submit_packet(struct kernel_queue *kq);
void kq_rollback_packet(struct kernel_queue *kq);


struct kernel_queue {
	/* data */
	struct kfd_node		*dev;
	struct mqd_manager	*mqd_mgr;
	struct queue		*queue;
	uint64_t		pending_wptr64;
	uint32_t		pending_wptr;
	unsigned int		nop_packet;

	struct kfd_mem_obj	*rptr_mem;
	uint32_t		*rptr_kernel;
	uint64_t		rptr_gpu_addr;
	struct kfd_mem_obj	*wptr_mem;
	union {
		uint64_t	*wptr64_kernel;
		uint32_t	*wptr_kernel;
	};
	uint64_t		wptr_gpu_addr;
	struct kfd_mem_obj	*pq;
	uint64_t		pq_gpu_addr;
	uint32_t		*pq_kernel_addr;
	struct kfd_mem_obj	*eop_mem;
	uint64_t		eop_gpu_addr;
	uint32_t		*eop_kernel_addr;

	struct kfd_mem_obj	*fence_mem_obj;
	uint64_t		fence_gpu_addr;
	void			*fence_kernel_address;

	struct list_head	list;
};

#endif /* KFD_KERNEL_QUEUE_H_ */

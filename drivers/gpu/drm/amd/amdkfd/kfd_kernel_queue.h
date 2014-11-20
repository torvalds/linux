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

#ifndef KFD_KERNEL_QUEUE_H_
#define KFD_KERNEL_QUEUE_H_

#include <linux/list.h>
#include <linux/types.h>
#include "kfd_priv.h"

struct kernel_queue {
	/* interface */
	bool	(*initialize)(struct kernel_queue *kq, struct kfd_dev *dev,
			enum kfd_queue_type type, unsigned int queue_size);
	void	(*uninitialize)(struct kernel_queue *kq);
	int	(*acquire_packet_buffer)(struct kernel_queue *kq,
					size_t packet_size_in_dwords,
					unsigned int **buffer_ptr);

	void	(*submit_packet)(struct kernel_queue *kq);
	int	(*sync_with_hw)(struct kernel_queue *kq,
				unsigned long timeout_ms);
	void	(*rollback_packet)(struct kernel_queue *kq);

	/* data */
	struct kfd_dev		*dev;
	struct mqd_manager	*mqd;
	struct queue		*queue;
	uint32_t		pending_wptr;
	unsigned int		nop_packet;

	struct kfd_mem_obj	*rptr_mem;
	uint32_t		*rptr_kernel;
	uint64_t		rptr_gpu_addr;
	struct kfd_mem_obj	*wptr_mem;
	uint32_t		*wptr_kernel;
	uint64_t		wptr_gpu_addr;
	struct kfd_mem_obj	*pq;
	uint64_t		pq_gpu_addr;
	uint32_t		*pq_kernel_addr;

	struct kfd_mem_obj	*fence_mem_obj;
	uint64_t		fence_gpu_addr;
	void			*fence_kernel_address;

	struct list_head	list;
};

#endif /* KFD_KERNEL_QUEUE_H_ */

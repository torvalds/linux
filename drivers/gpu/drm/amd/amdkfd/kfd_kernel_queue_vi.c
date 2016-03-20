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

#include "kfd_kernel_queue.h"

static bool initialize_vi(struct kernel_queue *kq, struct kfd_dev *dev,
			enum kfd_queue_type type, unsigned int queue_size);
static void uninitialize_vi(struct kernel_queue *kq);

void kernel_queue_init_vi(struct kernel_queue_ops *ops)
{
	ops->initialize = initialize_vi;
	ops->uninitialize = uninitialize_vi;
}

static bool initialize_vi(struct kernel_queue *kq, struct kfd_dev *dev,
			enum kfd_queue_type type, unsigned int queue_size)
{
	int retval;

	retval = kfd_gtt_sa_allocate(dev, PAGE_SIZE, &kq->eop_mem);
	if (retval != 0)
		return false;

	kq->eop_gpu_addr = kq->eop_mem->gpu_addr;
	kq->eop_kernel_addr = kq->eop_mem->cpu_ptr;

	memset(kq->eop_kernel_addr, 0, PAGE_SIZE);

	return true;
}

static void uninitialize_vi(struct kernel_queue *kq)
{
	kfd_gtt_sa_free(kq->dev, kq->eop_mem);
}

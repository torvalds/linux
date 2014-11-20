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

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include "kfd_kernel_queue.h"
#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_pm4_headers.h"
#include "kfd_pm4_opcodes.h"

#define PM4_COUNT_ZERO (((1 << 15) - 1) << 16)

static bool initialize(struct kernel_queue *kq, struct kfd_dev *dev,
		enum kfd_queue_type type, unsigned int queue_size)
{
	struct queue_properties prop;
	int retval;
	union PM4_MES_TYPE_3_HEADER nop;

	BUG_ON(!kq || !dev);
	BUG_ON(type != KFD_QUEUE_TYPE_DIQ && type != KFD_QUEUE_TYPE_HIQ);

	pr_debug("kfd: In func %s initializing queue type %d size %d\n",
			__func__, KFD_QUEUE_TYPE_HIQ, queue_size);

	nop.opcode = IT_NOP;
	nop.type = PM4_TYPE_3;
	nop.u32all |= PM4_COUNT_ZERO;

	kq->dev = dev;
	kq->nop_packet = nop.u32all;
	switch (type) {
	case KFD_QUEUE_TYPE_DIQ:
	case KFD_QUEUE_TYPE_HIQ:
		kq->mqd = dev->dqm->get_mqd_manager(dev->dqm,
						KFD_MQD_TYPE_CIK_HIQ);
		break;
	default:
		BUG();
		break;
	}

	if (kq->mqd == NULL)
		return false;

	prop.doorbell_ptr =
		(uint32_t *)kfd_get_kernel_doorbell(dev, &prop.doorbell_off);

	if (prop.doorbell_ptr == NULL)
		goto err_get_kernel_doorbell;

	retval = kfd2kgd->allocate_mem(dev->kgd,
					queue_size,
					PAGE_SIZE,
					KFD_MEMPOOL_SYSTEM_WRITECOMBINE,
					(struct kgd_mem **) &kq->pq);

	if (retval != 0)
		goto err_pq_allocate_vidmem;

	kq->pq_kernel_addr = kq->pq->cpu_ptr;
	kq->pq_gpu_addr = kq->pq->gpu_addr;

	retval = kfd2kgd->allocate_mem(dev->kgd,
					sizeof(*kq->rptr_kernel),
					32,
					KFD_MEMPOOL_SYSTEM_WRITECOMBINE,
					(struct kgd_mem **) &kq->rptr_mem);

	if (retval != 0)
		goto err_rptr_allocate_vidmem;

	kq->rptr_kernel = kq->rptr_mem->cpu_ptr;
	kq->rptr_gpu_addr = kq->rptr_mem->gpu_addr;

	retval = kfd2kgd->allocate_mem(dev->kgd,
					sizeof(*kq->wptr_kernel),
					32,
					KFD_MEMPOOL_SYSTEM_WRITECOMBINE,
					(struct kgd_mem **) &kq->wptr_mem);

	if (retval != 0)
		goto err_wptr_allocate_vidmem;

	kq->wptr_kernel = kq->wptr_mem->cpu_ptr;
	kq->wptr_gpu_addr = kq->wptr_mem->gpu_addr;

	memset(kq->pq_kernel_addr, 0, queue_size);
	memset(kq->rptr_kernel, 0, sizeof(*kq->rptr_kernel));
	memset(kq->wptr_kernel, 0, sizeof(*kq->wptr_kernel));

	prop.queue_size = queue_size;
	prop.is_interop = false;
	prop.priority = 1;
	prop.queue_percent = 100;
	prop.type = type;
	prop.vmid = 0;
	prop.queue_address = kq->pq_gpu_addr;
	prop.read_ptr = (uint32_t *) kq->rptr_gpu_addr;
	prop.write_ptr = (uint32_t *) kq->wptr_gpu_addr;

	if (init_queue(&kq->queue, prop) != 0)
		goto err_init_queue;

	kq->queue->device = dev;
	kq->queue->process = kfd_get_process(current);

	retval = kq->mqd->init_mqd(kq->mqd, &kq->queue->mqd,
					&kq->queue->mqd_mem_obj,
					&kq->queue->gart_mqd_addr,
					&kq->queue->properties);
	if (retval != 0)
		goto err_init_mqd;

	/* assign HIQ to HQD */
	if (type == KFD_QUEUE_TYPE_HIQ) {
		pr_debug("assigning hiq to hqd\n");
		kq->queue->pipe = KFD_CIK_HIQ_PIPE;
		kq->queue->queue = KFD_CIK_HIQ_QUEUE;
		kq->mqd->load_mqd(kq->mqd, kq->queue->mqd, kq->queue->pipe,
					kq->queue->queue, NULL);
	} else {
		/* allocate fence for DIQ */

		retval = kfd2kgd->allocate_mem(dev->kgd,
					sizeof(uint32_t),
					32,
					KFD_MEMPOOL_SYSTEM_WRITECOMBINE,
					(struct kgd_mem **) &kq->fence_mem_obj);

		if (retval != 0)
			goto err_alloc_fence;

		kq->fence_kernel_address = kq->fence_mem_obj->cpu_ptr;
		kq->fence_gpu_addr = kq->fence_mem_obj->gpu_addr;
	}

	print_queue(kq->queue);

	return true;
err_alloc_fence:
err_init_mqd:
	uninit_queue(kq->queue);
err_init_queue:
	kfd2kgd->free_mem(dev->kgd, (struct kgd_mem *) kq->wptr_mem);
err_wptr_allocate_vidmem:
	kfd2kgd->free_mem(dev->kgd, (struct kgd_mem *) kq->rptr_mem);
err_rptr_allocate_vidmem:
	kfd2kgd->free_mem(dev->kgd, (struct kgd_mem *) kq->pq);
err_pq_allocate_vidmem:
	pr_err("kfd: error init pq\n");
	kfd_release_kernel_doorbell(dev, (u32 *)prop.doorbell_ptr);
err_get_kernel_doorbell:
	pr_err("kfd: error init doorbell");
	return false;

}

static void uninitialize(struct kernel_queue *kq)
{
	BUG_ON(!kq);

	if (kq->queue->properties.type == KFD_QUEUE_TYPE_HIQ)
		kq->mqd->destroy_mqd(kq->mqd,
					NULL,
					false,
					QUEUE_PREEMPT_DEFAULT_TIMEOUT_MS,
					kq->queue->pipe,
					kq->queue->queue);

	kfd2kgd->free_mem(kq->dev->kgd, (struct kgd_mem *) kq->rptr_mem);
	kfd2kgd->free_mem(kq->dev->kgd, (struct kgd_mem *) kq->wptr_mem);
	kfd2kgd->free_mem(kq->dev->kgd, (struct kgd_mem *) kq->pq);
	kfd_release_kernel_doorbell(kq->dev,
				(u32 *)kq->queue->properties.doorbell_ptr);
	uninit_queue(kq->queue);
}

static int acquire_packet_buffer(struct kernel_queue *kq,
		size_t packet_size_in_dwords, unsigned int **buffer_ptr)
{
	size_t available_size;
	size_t queue_size_dwords;
	uint32_t wptr, rptr;
	unsigned int *queue_address;

	BUG_ON(!kq || !buffer_ptr);

	rptr = *kq->rptr_kernel;
	wptr = *kq->wptr_kernel;
	queue_address = (unsigned int *)kq->pq_kernel_addr;
	queue_size_dwords = kq->queue->properties.queue_size / sizeof(uint32_t);

	pr_debug("kfd: In func %s\nrptr: %d\nwptr: %d\nqueue_address 0x%p\n",
			__func__, rptr, wptr, queue_address);

	available_size = (rptr - 1 - wptr + queue_size_dwords) %
							queue_size_dwords;

	if (packet_size_in_dwords >= queue_size_dwords ||
			packet_size_in_dwords >= available_size)
		return -ENOMEM;

	if (wptr + packet_size_in_dwords >= queue_size_dwords) {
		while (wptr > 0) {
			queue_address[wptr] = kq->nop_packet;
			wptr = (wptr + 1) % queue_size_dwords;
		}
	}

	*buffer_ptr = &queue_address[wptr];
	kq->pending_wptr = wptr + packet_size_in_dwords;

	return 0;
}

static void submit_packet(struct kernel_queue *kq)
{
#ifdef DEBUG
	int i;
#endif

	BUG_ON(!kq);

#ifdef DEBUG
	for (i = *kq->wptr_kernel; i < kq->pending_wptr; i++) {
		pr_debug("0x%2X ", kq->pq_kernel_addr[i]);
		if (i % 15 == 0)
			pr_debug("\n");
	}
	pr_debug("\n");
#endif

	*kq->wptr_kernel = kq->pending_wptr;
	write_kernel_doorbell((u32 *)kq->queue->properties.doorbell_ptr,
				kq->pending_wptr);
}

static int sync_with_hw(struct kernel_queue *kq, unsigned long timeout_ms)
{
	unsigned long org_timeout_ms;

	BUG_ON(!kq);

	org_timeout_ms = timeout_ms;
	timeout_ms += jiffies * 1000 / HZ;
	while (*kq->wptr_kernel != *kq->rptr_kernel) {
		if (time_after(jiffies * 1000 / HZ, timeout_ms)) {
			pr_err("kfd: kernel_queue %s timeout expired %lu\n",
				__func__, org_timeout_ms);
			pr_err("kfd: wptr: %d rptr: %d\n",
				*kq->wptr_kernel, *kq->rptr_kernel);
			return -ETIME;
		}
		cpu_relax();
	}

	return 0;
}

static void rollback_packet(struct kernel_queue *kq)
{
	BUG_ON(!kq);
	kq->pending_wptr = *kq->queue->properties.write_ptr;
}

struct kernel_queue *kernel_queue_init(struct kfd_dev *dev,
					enum kfd_queue_type type)
{
	struct kernel_queue *kq;

	BUG_ON(!dev);

	kq = kzalloc(sizeof(struct kernel_queue), GFP_KERNEL);
	if (!kq)
		return NULL;

	kq->initialize = initialize;
	kq->uninitialize = uninitialize;
	kq->acquire_packet_buffer = acquire_packet_buffer;
	kq->submit_packet = submit_packet;
	kq->sync_with_hw = sync_with_hw;
	kq->rollback_packet = rollback_packet;

	if (kq->initialize(kq, dev, type, KFD_KERNEL_QUEUE_SIZE) == false) {
		pr_err("kfd: failed to init kernel queue\n");
		kfree(kq);
		return NULL;
	}
	return kq;
}

void kernel_queue_uninit(struct kernel_queue *kq)
{
	BUG_ON(!kq);

	kq->uninitialize(kq);
	kfree(kq);
}

void test_kq(struct kfd_dev *dev)
{
	struct kernel_queue *kq;
	uint32_t *buffer, i;
	int retval;

	BUG_ON(!dev);

	pr_debug("kfd: starting kernel queue test\n");

	kq = kernel_queue_init(dev, KFD_QUEUE_TYPE_HIQ);
	BUG_ON(!kq);

	retval = kq->acquire_packet_buffer(kq, 5, &buffer);
	BUG_ON(retval != 0);
	for (i = 0; i < 5; i++)
		buffer[i] = kq->nop_packet;
	kq->submit_packet(kq);
	kq->sync_with_hw(kq, 1000);

	pr_debug("kfd: ending kernel queue test\n");
}



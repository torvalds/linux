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
#include <linux/sched.h>
#include "kfd_kernel_queue.h"
#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_pm4_headers.h"
#include "kfd_pm4_opcodes.h"

#define PM4_COUNT_ZERO (((1 << 15) - 1) << 16)

/* Initialize a kernel queue, including allocations of GART memory
 * needed for the queue.
 */
static bool kq_initialize(struct kernel_queue *kq, struct kfd_dev *dev,
		enum kfd_queue_type type, unsigned int queue_size)
{
	struct queue_properties prop;
	int retval;
	union PM4_MES_TYPE_3_HEADER nop;

	if (WARN_ON(type != KFD_QUEUE_TYPE_DIQ && type != KFD_QUEUE_TYPE_HIQ))
		return false;

	pr_debug("Initializing queue type %d size %d\n", KFD_QUEUE_TYPE_HIQ,
			queue_size);

	memset(&prop, 0, sizeof(prop));
	memset(&nop, 0, sizeof(nop));

	nop.opcode = IT_NOP;
	nop.type = PM4_TYPE_3;
	nop.u32all |= PM4_COUNT_ZERO;

	kq->dev = dev;
	kq->nop_packet = nop.u32all;
	switch (type) {
	case KFD_QUEUE_TYPE_DIQ:
		kq->mqd_mgr = dev->dqm->mqd_mgrs[KFD_MQD_TYPE_DIQ];
		break;
	case KFD_QUEUE_TYPE_HIQ:
		kq->mqd_mgr = dev->dqm->mqd_mgrs[KFD_MQD_TYPE_HIQ];
		break;
	default:
		pr_err("Invalid queue type %d\n", type);
		return false;
	}

	if (!kq->mqd_mgr)
		return false;

	prop.doorbell_ptr = kfd_get_kernel_doorbell(dev, &prop.doorbell_off);

	if (!prop.doorbell_ptr) {
		pr_err("Failed to initialize doorbell");
		goto err_get_kernel_doorbell;
	}

	retval = kfd_gtt_sa_allocate(dev, queue_size, &kq->pq);
	if (retval != 0) {
		pr_err("Failed to init pq queues size %d\n", queue_size);
		goto err_pq_allocate_vidmem;
	}

	kq->pq_kernel_addr = kq->pq->cpu_ptr;
	kq->pq_gpu_addr = kq->pq->gpu_addr;

	/* For CIK family asics, kq->eop_mem is not needed */
	if (dev->device_info->asic_family > CHIP_MULLINS) {
		retval = kfd_gtt_sa_allocate(dev, PAGE_SIZE, &kq->eop_mem);
		if (retval != 0)
			goto err_eop_allocate_vidmem;

		kq->eop_gpu_addr = kq->eop_mem->gpu_addr;
		kq->eop_kernel_addr = kq->eop_mem->cpu_ptr;

		memset(kq->eop_kernel_addr, 0, PAGE_SIZE);
	}

	retval = kfd_gtt_sa_allocate(dev, sizeof(*kq->rptr_kernel),
					&kq->rptr_mem);

	if (retval != 0)
		goto err_rptr_allocate_vidmem;

	kq->rptr_kernel = kq->rptr_mem->cpu_ptr;
	kq->rptr_gpu_addr = kq->rptr_mem->gpu_addr;

	retval = kfd_gtt_sa_allocate(dev, dev->device_info->doorbell_size,
					&kq->wptr_mem);

	if (retval != 0)
		goto err_wptr_allocate_vidmem;

	kq->wptr_kernel = kq->wptr_mem->cpu_ptr;
	kq->wptr_gpu_addr = kq->wptr_mem->gpu_addr;

	memset(kq->pq_kernel_addr, 0, queue_size);
	memset(kq->rptr_kernel, 0, sizeof(*kq->rptr_kernel));
	memset(kq->wptr_kernel, 0, sizeof(*kq->wptr_kernel));

	prop.queue_size = queue_size;
	prop.is_interop = false;
	prop.is_gws = false;
	prop.priority = 1;
	prop.queue_percent = 100;
	prop.type = type;
	prop.vmid = 0;
	prop.queue_address = kq->pq_gpu_addr;
	prop.read_ptr = (uint32_t *) kq->rptr_gpu_addr;
	prop.write_ptr = (uint32_t *) kq->wptr_gpu_addr;
	prop.eop_ring_buffer_address = kq->eop_gpu_addr;
	prop.eop_ring_buffer_size = PAGE_SIZE;
	prop.cu_mask = NULL;

	if (init_queue(&kq->queue, &prop) != 0)
		goto err_init_queue;

	kq->queue->device = dev;
	kq->queue->process = kfd_get_process(current);

	kq->queue->mqd_mem_obj = kq->mqd_mgr->allocate_mqd(kq->mqd_mgr->dev,
					&kq->queue->properties);
	if (!kq->queue->mqd_mem_obj)
		goto err_allocate_mqd;
	kq->mqd_mgr->init_mqd(kq->mqd_mgr, &kq->queue->mqd,
					kq->queue->mqd_mem_obj,
					&kq->queue->gart_mqd_addr,
					&kq->queue->properties);
	/* assign HIQ to HQD */
	if (type == KFD_QUEUE_TYPE_HIQ) {
		pr_debug("Assigning hiq to hqd\n");
		kq->queue->pipe = KFD_CIK_HIQ_PIPE;
		kq->queue->queue = KFD_CIK_HIQ_QUEUE;
		kq->mqd_mgr->load_mqd(kq->mqd_mgr, kq->queue->mqd,
				kq->queue->pipe, kq->queue->queue,
				&kq->queue->properties, NULL);
	} else {
		/* allocate fence for DIQ */

		retval = kfd_gtt_sa_allocate(dev, sizeof(uint32_t),
						&kq->fence_mem_obj);

		if (retval != 0)
			goto err_alloc_fence;

		kq->fence_kernel_address = kq->fence_mem_obj->cpu_ptr;
		kq->fence_gpu_addr = kq->fence_mem_obj->gpu_addr;
	}

	print_queue(kq->queue);

	return true;
err_alloc_fence:
	kq->mqd_mgr->free_mqd(kq->mqd_mgr, kq->queue->mqd, kq->queue->mqd_mem_obj);
err_allocate_mqd:
	uninit_queue(kq->queue);
err_init_queue:
	kfd_gtt_sa_free(dev, kq->wptr_mem);
err_wptr_allocate_vidmem:
	kfd_gtt_sa_free(dev, kq->rptr_mem);
err_rptr_allocate_vidmem:
	kfd_gtt_sa_free(dev, kq->eop_mem);
err_eop_allocate_vidmem:
	kfd_gtt_sa_free(dev, kq->pq);
err_pq_allocate_vidmem:
	kfd_release_kernel_doorbell(dev, prop.doorbell_ptr);
err_get_kernel_doorbell:
	return false;

}

/* Uninitialize a kernel queue and free all its memory usages. */
static void kq_uninitialize(struct kernel_queue *kq, bool hanging)
{
	if (kq->queue->properties.type == KFD_QUEUE_TYPE_HIQ && !hanging)
		kq->mqd_mgr->destroy_mqd(kq->mqd_mgr,
					kq->queue->mqd,
					KFD_PREEMPT_TYPE_WAVEFRONT_RESET,
					KFD_UNMAP_LATENCY_MS,
					kq->queue->pipe,
					kq->queue->queue);
	else if (kq->queue->properties.type == KFD_QUEUE_TYPE_DIQ)
		kfd_gtt_sa_free(kq->dev, kq->fence_mem_obj);

	kq->mqd_mgr->free_mqd(kq->mqd_mgr, kq->queue->mqd,
				kq->queue->mqd_mem_obj);

	kfd_gtt_sa_free(kq->dev, kq->rptr_mem);
	kfd_gtt_sa_free(kq->dev, kq->wptr_mem);

	/* For CIK family asics, kq->eop_mem is Null, kfd_gtt_sa_free()
	 * is able to handle NULL properly.
	 */
	kfd_gtt_sa_free(kq->dev, kq->eop_mem);

	kfd_gtt_sa_free(kq->dev, kq->pq);
	kfd_release_kernel_doorbell(kq->dev,
					kq->queue->properties.doorbell_ptr);
	uninit_queue(kq->queue);
}

int kq_acquire_packet_buffer(struct kernel_queue *kq,
		size_t packet_size_in_dwords, unsigned int **buffer_ptr)
{
	size_t available_size;
	size_t queue_size_dwords;
	uint32_t wptr, rptr;
	uint64_t wptr64;
	unsigned int *queue_address;

	/* When rptr == wptr, the buffer is empty.
	 * When rptr == wptr + 1, the buffer is full.
	 * It is always rptr that advances to the position of wptr, rather than
	 * the opposite. So we can only use up to queue_size_dwords - 1 dwords.
	 */
	rptr = *kq->rptr_kernel;
	wptr = kq->pending_wptr;
	wptr64 = kq->pending_wptr64;
	queue_address = (unsigned int *)kq->pq_kernel_addr;
	queue_size_dwords = kq->queue->properties.queue_size / 4;

	pr_debug("rptr: %d\n", rptr);
	pr_debug("wptr: %d\n", wptr);
	pr_debug("queue_address 0x%p\n", queue_address);

	available_size = (rptr + queue_size_dwords - 1 - wptr) %
							queue_size_dwords;

	if (packet_size_in_dwords > available_size) {
		/*
		 * make sure calling functions know
		 * acquire_packet_buffer() failed
		 */
		goto err_no_space;
	}

	if (wptr + packet_size_in_dwords >= queue_size_dwords) {
		/* make sure after rolling back to position 0, there is
		 * still enough space.
		 */
		if (packet_size_in_dwords >= rptr)
			goto err_no_space;

		/* fill nops, roll back and start at position 0 */
		while (wptr > 0) {
			queue_address[wptr] = kq->nop_packet;
			wptr = (wptr + 1) % queue_size_dwords;
			wptr64++;
		}
	}

	*buffer_ptr = &queue_address[wptr];
	kq->pending_wptr = wptr + packet_size_in_dwords;
	kq->pending_wptr64 = wptr64 + packet_size_in_dwords;

	return 0;

err_no_space:
	*buffer_ptr = NULL;
	return -ENOMEM;
}

void kq_submit_packet(struct kernel_queue *kq)
{
#ifdef DEBUG
	int i;

	for (i = *kq->wptr_kernel; i < kq->pending_wptr; i++) {
		pr_debug("0x%2X ", kq->pq_kernel_addr[i]);
		if (i % 15 == 0)
			pr_debug("\n");
	}
	pr_debug("\n");
#endif
	if (kq->dev->device_info->doorbell_size == 8) {
		*kq->wptr64_kernel = kq->pending_wptr64;
		write_kernel_doorbell64(kq->queue->properties.doorbell_ptr,
					kq->pending_wptr64);
	} else {
		*kq->wptr_kernel = kq->pending_wptr;
		write_kernel_doorbell(kq->queue->properties.doorbell_ptr,
					kq->pending_wptr);
	}
}

void kq_rollback_packet(struct kernel_queue *kq)
{
	if (kq->dev->device_info->doorbell_size == 8) {
		kq->pending_wptr64 = *kq->wptr64_kernel;
		kq->pending_wptr = *kq->wptr_kernel %
			(kq->queue->properties.queue_size / 4);
	} else {
		kq->pending_wptr = *kq->wptr_kernel;
	}
}

struct kernel_queue *kernel_queue_init(struct kfd_dev *dev,
					enum kfd_queue_type type)
{
	struct kernel_queue *kq;

	kq = kzalloc(sizeof(*kq), GFP_KERNEL);
	if (!kq)
		return NULL;

	if (kq_initialize(kq, dev, type, KFD_KERNEL_QUEUE_SIZE))
		return kq;

	pr_err("Failed to init kernel queue\n");

	kfree(kq);
	return NULL;
}

void kernel_queue_uninit(struct kernel_queue *kq, bool hanging)
{
	kq_uninitialize(kq, hanging);
	kfree(kq);
}

/* FIXME: Can this test be removed? */
static __attribute__((unused)) void test_kq(struct kfd_dev *dev)
{
	struct kernel_queue *kq;
	uint32_t *buffer, i;
	int retval;

	pr_err("Starting kernel queue test\n");

	kq = kernel_queue_init(dev, KFD_QUEUE_TYPE_HIQ);
	if (unlikely(!kq)) {
		pr_err("  Failed to initialize HIQ\n");
		pr_err("Kernel queue test failed\n");
		return;
	}

	retval = kq_acquire_packet_buffer(kq, 5, &buffer);
	if (unlikely(retval != 0)) {
		pr_err("  Failed to acquire packet buffer\n");
		pr_err("Kernel queue test failed\n");
		return;
	}
	for (i = 0; i < 5; i++)
		buffer[i] = kq->nop_packet;
	kq_submit_packet(kq);

	pr_err("Ending kernel queue test\n");
}



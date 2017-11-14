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

static bool initialize(struct kernel_queue *kq, struct kfd_dev *dev,
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
	case KFD_QUEUE_TYPE_HIQ:
		kq->mqd = dev->dqm->ops.get_mqd_manager(dev->dqm,
						KFD_MQD_TYPE_HIQ);
		break;
	default:
		pr_err("Invalid queue type %d\n", type);
		return false;
	}

	if (!kq->mqd)
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

	retval = kq->ops_asic_specific.initialize(kq, dev, type, queue_size);
	if (!retval)
		goto err_eop_allocate_vidmem;

	retval = kfd_gtt_sa_allocate(dev, sizeof(*kq->rptr_kernel),
					&kq->rptr_mem);

	if (retval != 0)
		goto err_rptr_allocate_vidmem;

	kq->rptr_kernel = kq->rptr_mem->cpu_ptr;
	kq->rptr_gpu_addr = kq->rptr_mem->gpu_addr;

	retval = kfd_gtt_sa_allocate(dev, sizeof(*kq->wptr_kernel),
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
	prop.priority = 1;
	prop.queue_percent = 100;
	prop.type = type;
	prop.vmid = 0;
	prop.queue_address = kq->pq_gpu_addr;
	prop.read_ptr = (uint32_t *) kq->rptr_gpu_addr;
	prop.write_ptr = (uint32_t *) kq->wptr_gpu_addr;
	prop.eop_ring_buffer_address = kq->eop_gpu_addr;
	prop.eop_ring_buffer_size = PAGE_SIZE;

	if (init_queue(&kq->queue, &prop) != 0)
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
		pr_debug("Assigning hiq to hqd\n");
		kq->queue->pipe = KFD_CIK_HIQ_PIPE;
		kq->queue->queue = KFD_CIK_HIQ_QUEUE;
		kq->mqd->load_mqd(kq->mqd, kq->queue->mqd, kq->queue->pipe,
				  kq->queue->queue, &kq->queue->properties,
				  NULL);
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
err_init_mqd:
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

static void uninitialize(struct kernel_queue *kq)
{
	if (kq->queue->properties.type == KFD_QUEUE_TYPE_HIQ)
		kq->mqd->destroy_mqd(kq->mqd,
					kq->queue->mqd,
					KFD_PREEMPT_TYPE_WAVEFRONT_RESET,
					KFD_UNMAP_LATENCY_MS,
					kq->queue->pipe,
					kq->queue->queue);
	else if (kq->queue->properties.type == KFD_QUEUE_TYPE_DIQ)
		kfd_gtt_sa_free(kq->dev, kq->fence_mem_obj);

	kq->mqd->uninit_mqd(kq->mqd, kq->queue->mqd, kq->queue->mqd_mem_obj);

	kfd_gtt_sa_free(kq->dev, kq->rptr_mem);
	kfd_gtt_sa_free(kq->dev, kq->wptr_mem);
	kq->ops_asic_specific.uninitialize(kq);
	kfd_gtt_sa_free(kq->dev, kq->pq);
	kfd_release_kernel_doorbell(kq->dev,
					kq->queue->properties.doorbell_ptr);
	uninit_queue(kq->queue);
}

static int acquire_packet_buffer(struct kernel_queue *kq,
		size_t packet_size_in_dwords, unsigned int **buffer_ptr)
{
	size_t available_size;
	size_t queue_size_dwords;
	uint32_t wptr, rptr;
	unsigned int *queue_address;

	/* When rptr == wptr, the buffer is empty.
	 * When rptr == wptr + 1, the buffer is full.
	 * It is always rptr that advances to the position of wptr, rather than
	 * the opposite. So we can only use up to queue_size_dwords - 1 dwords.
	 */
	rptr = *kq->rptr_kernel;
	wptr = *kq->wptr_kernel;
	queue_address = (unsigned int *)kq->pq_kernel_addr;
	queue_size_dwords = kq->queue->properties.queue_size / sizeof(uint32_t);

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
		*buffer_ptr = NULL;
		return -ENOMEM;
	}

	if (wptr + packet_size_in_dwords >= queue_size_dwords) {
		/* make sure after rolling back to position 0, there is
		 * still enough space.
		 */
		if (packet_size_in_dwords >= rptr) {
			*buffer_ptr = NULL;
			return -ENOMEM;
		}
		/* fill nops, roll back and start at position 0 */
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

	for (i = *kq->wptr_kernel; i < kq->pending_wptr; i++) {
		pr_debug("0x%2X ", kq->pq_kernel_addr[i]);
		if (i % 15 == 0)
			pr_debug("\n");
	}
	pr_debug("\n");
#endif

	*kq->wptr_kernel = kq->pending_wptr;
	write_kernel_doorbell(kq->queue->properties.doorbell_ptr,
				kq->pending_wptr);
}

static void rollback_packet(struct kernel_queue *kq)
{
	kq->pending_wptr = *kq->queue->properties.write_ptr;
}

struct kernel_queue *kernel_queue_init(struct kfd_dev *dev,
					enum kfd_queue_type type)
{
	struct kernel_queue *kq;

	kq = kzalloc(sizeof(*kq), GFP_KERNEL);
	if (!kq)
		return NULL;

	kq->ops.initialize = initialize;
	kq->ops.uninitialize = uninitialize;
	kq->ops.acquire_packet_buffer = acquire_packet_buffer;
	kq->ops.submit_packet = submit_packet;
	kq->ops.rollback_packet = rollback_packet;

	switch (dev->device_info->asic_family) {
	case CHIP_CARRIZO:
		kernel_queue_init_vi(&kq->ops_asic_specific);
		break;

	case CHIP_KAVERI:
		kernel_queue_init_cik(&kq->ops_asic_specific);
		break;
	default:
		WARN(1, "Unexpected ASIC family %u",
		     dev->device_info->asic_family);
		goto out_free;
	}

	if (kq->ops.initialize(kq, dev, type, KFD_KERNEL_QUEUE_SIZE))
		return kq;

	pr_err("Failed to init kernel queue\n");

out_free:
	kfree(kq);
	return NULL;
}

void kernel_queue_uninit(struct kernel_queue *kq)
{
	kq->ops.uninitialize(kq);
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

	retval = kq->ops.acquire_packet_buffer(kq, 5, &buffer);
	if (unlikely(retval != 0)) {
		pr_err("  Failed to acquire packet buffer\n");
		pr_err("Kernel queue test failed\n");
		return;
	}
	for (i = 0; i < 5; i++)
		buffer[i] = kq->nop_packet;
	kq->ops.submit_packet(kq);

	pr_err("Ending kernel queue test\n");
}



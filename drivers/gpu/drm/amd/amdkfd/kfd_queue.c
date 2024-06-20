// SPDX-License-Identifier: GPL-2.0 OR MIT
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

#include <linux/slab.h>
#include "kfd_priv.h"
#include "kfd_svm.h"

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
	pr_debug("Queue Read Pointer: 0x%px\n", q->read_ptr);
	pr_debug("Queue Write Pointer: 0x%px\n", q->write_ptr);
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
	pr_debug("Queue Read Pointer: 0x%px\n", q->properties.read_ptr);
	pr_debug("Queue Write Pointer: 0x%px\n", q->properties.write_ptr);
	pr_debug("Queue Doorbell Pointer: 0x%p\n", q->properties.doorbell_ptr);
	pr_debug("Queue Doorbell Offset: %u\n", q->properties.doorbell_off);
	pr_debug("Queue MQD Address: 0x%p\n", q->mqd);
	pr_debug("Queue MQD Gart: 0x%llX\n", q->gart_mqd_addr);
	pr_debug("Queue Process Address: 0x%p\n", q->process);
	pr_debug("Queue Device Address: 0x%p\n", q->device);
}

int init_queue(struct queue **q, const struct queue_properties *properties)
{
	struct queue *tmp_q;

	tmp_q = kzalloc(sizeof(*tmp_q), GFP_KERNEL);
	if (!tmp_q)
		return -ENOMEM;

	memcpy(&tmp_q->properties, properties, sizeof(*properties));

	*q = tmp_q;
	return 0;
}

void uninit_queue(struct queue *q)
{
	kfree(q);
}

static int kfd_queue_buffer_svm_get(struct kfd_process_device *pdd, u64 addr, u64 size)
{
	struct kfd_process *p = pdd->process;
	struct list_head update_list;
	struct svm_range *prange;
	int ret = -EINVAL;

	INIT_LIST_HEAD(&update_list);
	addr >>= PAGE_SHIFT;
	size >>= PAGE_SHIFT;

	mutex_lock(&p->svms.lock);

	/*
	 * range may split to multiple svm pranges aligned to granularity boundaery.
	 */
	while (size) {
		uint32_t gpuid, gpuidx;
		int r;

		prange = svm_range_from_addr(&p->svms, addr, NULL);
		if (!prange)
			break;

		if (!prange->mapped_to_gpu)
			break;

		r = kfd_process_gpuid_from_node(p, pdd->dev, &gpuid, &gpuidx);
		if (r < 0)
			break;
		if (!test_bit(gpuidx, prange->bitmap_access) &&
		    !test_bit(gpuidx, prange->bitmap_aip))
			break;

		if (!(prange->flags & KFD_IOCTL_SVM_FLAG_GPU_ALWAYS_MAPPED))
			break;

		list_add(&prange->update_list, &update_list);

		if (prange->last - prange->start + 1 >= size) {
			size = 0;
			break;
		}

		size -= prange->last - prange->start + 1;
		addr += prange->last - prange->start + 1;
	}
	if (size) {
		pr_debug("[0x%llx 0x%llx] not registered\n", addr, addr + size - 1);
		goto out_unlock;
	}

	list_for_each_entry(prange, &update_list, update_list)
		atomic_inc(&prange->queue_refcount);
	ret = 0;

out_unlock:
	mutex_unlock(&p->svms.lock);
	return ret;
}

static void kfd_queue_buffer_svm_put(struct kfd_process_device *pdd, u64 addr, u64 size)
{
	struct kfd_process *p = pdd->process;
	struct svm_range *prange, *pchild;
	struct interval_tree_node *node;
	unsigned long last;

	addr >>= PAGE_SHIFT;
	last = addr + (size >> PAGE_SHIFT) - 1;

	mutex_lock(&p->svms.lock);

	node = interval_tree_iter_first(&p->svms.objects, addr, last);
	while (node) {
		struct interval_tree_node *next_node;
		unsigned long next_start;

		prange = container_of(node, struct svm_range, it_node);
		next_node = interval_tree_iter_next(node, addr, last);
		next_start = min(node->last, last) + 1;

		if (atomic_add_unless(&prange->queue_refcount, -1, 0)) {
			list_for_each_entry(pchild, &prange->child_list, child_list)
				atomic_add_unless(&pchild->queue_refcount, -1, 0);
		}

		node = next_node;
		addr = next_start;
	}

	mutex_unlock(&p->svms.lock);
}

int kfd_queue_buffer_get(struct amdgpu_vm *vm, void __user *addr, struct amdgpu_bo **pbo,
			 u64 expected_size)
{
	struct amdgpu_bo_va_mapping *mapping;
	u64 user_addr;
	u64 size;

	user_addr = (u64)addr >> AMDGPU_GPU_PAGE_SHIFT;
	size = expected_size >> AMDGPU_GPU_PAGE_SHIFT;

	mapping = amdgpu_vm_bo_lookup_mapping(vm, user_addr);
	if (!mapping)
		goto out_err;

	if (user_addr != mapping->start ||
	    (size != 0 && user_addr + size - 1 != mapping->last)) {
		pr_debug("expected size 0x%llx not equal to mapping addr 0x%llx size 0x%llx\n",
			expected_size, mapping->start << AMDGPU_GPU_PAGE_SHIFT,
			(mapping->last - mapping->start + 1) << AMDGPU_GPU_PAGE_SHIFT);
		goto out_err;
	}

	*pbo = amdgpu_bo_ref(mapping->bo_va->base.bo);
	mapping->bo_va->queue_refcount++;
	return 0;

out_err:
	*pbo = NULL;
	return -EINVAL;
}

void kfd_queue_buffer_put(struct amdgpu_vm *vm, struct amdgpu_bo **bo)
{
	if (*bo) {
		struct amdgpu_bo_va *bo_va;

		bo_va = amdgpu_vm_bo_find(vm, *bo);
		if (bo_va)
			bo_va->queue_refcount--;
	}

	amdgpu_bo_unref(bo);
}

int kfd_queue_acquire_buffers(struct kfd_process_device *pdd, struct queue_properties *properties)
{
	struct amdgpu_vm *vm;
	int err;

	vm = drm_priv_to_vm(pdd->drm_priv);
	err = amdgpu_bo_reserve(vm->root.bo, false);
	if (err)
		return err;

	err = kfd_queue_buffer_get(vm, properties->write_ptr, &properties->wptr_bo, PAGE_SIZE);
	if (err)
		goto out_err_unreserve;

	err = kfd_queue_buffer_get(vm, properties->read_ptr, &properties->rptr_bo, PAGE_SIZE);
	if (err)
		goto out_err_unreserve;

	err = kfd_queue_buffer_get(vm, (void *)properties->queue_address,
				   &properties->ring_bo, properties->queue_size);
	if (err)
		goto out_err_unreserve;

	/* only compute queue requires EOP buffer and CWSR area */
	if (properties->type != KFD_QUEUE_TYPE_COMPUTE)
		goto out_unreserve;

	/* EOP buffer is not required for all ASICs */
	if (properties->eop_ring_buffer_address) {
		err = kfd_queue_buffer_get(vm, (void *)properties->eop_ring_buffer_address,
					   &properties->eop_buf_bo,
					   properties->eop_ring_buffer_size);
		if (err)
			goto out_err_unreserve;
	}

	err = kfd_queue_buffer_get(vm, (void *)properties->ctx_save_restore_area_address,
				   &properties->cwsr_bo, 0);
	if (!err)
		goto out_unreserve;

	amdgpu_bo_unreserve(vm->root.bo);

	err = kfd_queue_buffer_svm_get(pdd, properties->ctx_save_restore_area_address,
				       properties->ctx_save_restore_area_size);
	if (err)
		goto out_err_release;

	return 0;

out_unreserve:
	amdgpu_bo_unreserve(vm->root.bo);
	return 0;

out_err_unreserve:
	amdgpu_bo_unreserve(vm->root.bo);
out_err_release:
	kfd_queue_release_buffers(pdd, properties);
	return err;
}

int kfd_queue_release_buffers(struct kfd_process_device *pdd, struct queue_properties *properties)
{
	struct amdgpu_vm *vm;
	int err;

	vm = drm_priv_to_vm(pdd->drm_priv);
	err = amdgpu_bo_reserve(vm->root.bo, false);
	if (err)
		return err;

	kfd_queue_buffer_put(vm, &properties->wptr_bo);
	kfd_queue_buffer_put(vm, &properties->rptr_bo);
	kfd_queue_buffer_put(vm, &properties->ring_bo);
	kfd_queue_buffer_put(vm, &properties->eop_buf_bo);
	kfd_queue_buffer_put(vm, &properties->cwsr_bo);

	amdgpu_bo_unreserve(vm->root.bo);

	kfd_queue_buffer_svm_put(pdd, properties->ctx_save_restore_area_address,
				 properties->ctx_save_restore_area_size);
	return 0;
}

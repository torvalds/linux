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
#include "kfd_topology.h"
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

#if IS_ENABLED(CONFIG_HSA_AMD_SVM)

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
#else

static int kfd_queue_buffer_svm_get(struct kfd_process_device *pdd, u64 addr, u64 size)
{
	return -EINVAL;
}

static void kfd_queue_buffer_svm_put(struct kfd_process_device *pdd, u64 addr, u64 size)
{
}

#endif

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

/* FIXME: remove this function, just call amdgpu_bo_unref directly */
void kfd_queue_buffer_put(struct amdgpu_bo **bo)
{
	amdgpu_bo_unref(bo);
}

int kfd_queue_acquire_buffers(struct kfd_process_device *pdd, struct queue_properties *properties)
{
	struct kfd_topology_device *topo_dev;
	u64 expected_queue_size;
	struct amdgpu_vm *vm;
	u32 total_cwsr_size;
	int err;

	topo_dev = kfd_topology_device_by_id(pdd->dev->id);
	if (!topo_dev)
		return -EINVAL;

	/* AQL queues on GFX7 and GFX8 appear twice their actual size */
	if (properties->type == KFD_QUEUE_TYPE_COMPUTE &&
	    properties->format == KFD_QUEUE_FORMAT_AQL &&
	    topo_dev->node_props.gfx_target_version >= 70000 &&
	    topo_dev->node_props.gfx_target_version < 90000)
		expected_queue_size = properties->queue_size / 2;
	else
		expected_queue_size = properties->queue_size;

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
				   &properties->ring_bo, expected_queue_size);
	if (err)
		goto out_err_unreserve;

	/* only compute queue requires EOP buffer and CWSR area */
	if (properties->type != KFD_QUEUE_TYPE_COMPUTE)
		goto out_unreserve;

	/* EOP buffer is not required for all ASICs */
	if (properties->eop_ring_buffer_address) {
		if (properties->eop_ring_buffer_size != topo_dev->node_props.eop_buffer_size) {
			pr_debug("queue eop bo size 0x%x not equal to node eop buf size 0x%x\n",
				properties->eop_ring_buffer_size,
				topo_dev->node_props.eop_buffer_size);
			err = -EINVAL;
			goto out_err_unreserve;
		}
		err = kfd_queue_buffer_get(vm, (void *)properties->eop_ring_buffer_address,
					   &properties->eop_buf_bo,
					   properties->eop_ring_buffer_size);
		if (err)
			goto out_err_unreserve;
	}

	if (properties->ctl_stack_size != topo_dev->node_props.ctl_stack_size) {
		pr_debug("queue ctl stack size 0x%x not equal to node ctl stack size 0x%x\n",
			properties->ctl_stack_size,
			topo_dev->node_props.ctl_stack_size);
		err = -EINVAL;
		goto out_err_unreserve;
	}

	if (properties->ctx_save_restore_area_size != topo_dev->node_props.cwsr_size) {
		pr_debug("queue cwsr size 0x%x not equal to node cwsr size 0x%x\n",
			properties->ctx_save_restore_area_size,
			topo_dev->node_props.cwsr_size);
		err = -EINVAL;
		goto out_err_unreserve;
	}

	total_cwsr_size = (topo_dev->node_props.cwsr_size + topo_dev->node_props.debug_memory_size)
			  * NUM_XCC(pdd->dev->xcc_mask);
	total_cwsr_size = ALIGN(total_cwsr_size, PAGE_SIZE);

	err = kfd_queue_buffer_get(vm, (void *)properties->ctx_save_restore_area_address,
				   &properties->cwsr_bo, total_cwsr_size);
	if (!err)
		goto out_unreserve;

	amdgpu_bo_unreserve(vm->root.bo);

	err = kfd_queue_buffer_svm_get(pdd, properties->ctx_save_restore_area_address,
				       total_cwsr_size);
	if (err)
		goto out_err_release;

	return 0;

out_unreserve:
	amdgpu_bo_unreserve(vm->root.bo);
	return 0;

out_err_unreserve:
	amdgpu_bo_unreserve(vm->root.bo);
out_err_release:
	/* FIXME: make a _locked version of this that can be called before
	 * dropping the VM reservation.
	 */
	kfd_queue_unref_bo_vas(pdd, properties);
	kfd_queue_release_buffers(pdd, properties);
	return err;
}

int kfd_queue_release_buffers(struct kfd_process_device *pdd, struct queue_properties *properties)
{
	struct kfd_topology_device *topo_dev;
	u32 total_cwsr_size;

	kfd_queue_buffer_put(&properties->wptr_bo);
	kfd_queue_buffer_put(&properties->rptr_bo);
	kfd_queue_buffer_put(&properties->ring_bo);
	kfd_queue_buffer_put(&properties->eop_buf_bo);
	kfd_queue_buffer_put(&properties->cwsr_bo);

	topo_dev = kfd_topology_device_by_id(pdd->dev->id);
	if (!topo_dev)
		return -EINVAL;
	total_cwsr_size = (topo_dev->node_props.cwsr_size + topo_dev->node_props.debug_memory_size)
			  * NUM_XCC(pdd->dev->xcc_mask);
	total_cwsr_size = ALIGN(total_cwsr_size, PAGE_SIZE);

	kfd_queue_buffer_svm_put(pdd, properties->ctx_save_restore_area_address, total_cwsr_size);
	return 0;
}

void kfd_queue_unref_bo_va(struct amdgpu_vm *vm, struct amdgpu_bo **bo)
{
	if (*bo) {
		struct amdgpu_bo_va *bo_va;

		bo_va = amdgpu_vm_bo_find(vm, *bo);
		if (bo_va && bo_va->queue_refcount)
			bo_va->queue_refcount--;
	}
}

int kfd_queue_unref_bo_vas(struct kfd_process_device *pdd,
			   struct queue_properties *properties)
{
	struct amdgpu_vm *vm;
	int err;

	vm = drm_priv_to_vm(pdd->drm_priv);
	err = amdgpu_bo_reserve(vm->root.bo, false);
	if (err)
		return err;

	kfd_queue_unref_bo_va(vm, &properties->wptr_bo);
	kfd_queue_unref_bo_va(vm, &properties->rptr_bo);
	kfd_queue_unref_bo_va(vm, &properties->ring_bo);
	kfd_queue_unref_bo_va(vm, &properties->eop_buf_bo);
	kfd_queue_unref_bo_va(vm, &properties->cwsr_bo);

	amdgpu_bo_unreserve(vm->root.bo);
	return 0;
}

#define SGPR_SIZE_PER_CU	0x4000
#define LDS_SIZE_PER_CU		0x10000
#define HWREG_SIZE_PER_CU	0x1000
#define DEBUGGER_BYTES_ALIGN	64
#define DEBUGGER_BYTES_PER_WAVE	32

static u32 kfd_get_vgpr_size_per_cu(u32 gfxv)
{
	u32 vgpr_size = 0x40000;

	if ((gfxv / 100 * 100) == 90400 ||	/* GFX_VERSION_AQUA_VANJARAM */
	    gfxv == 90010 ||			/* GFX_VERSION_ALDEBARAN */
	    gfxv == 90008 ||			/* GFX_VERSION_ARCTURUS */
	    gfxv == 90500)
		vgpr_size = 0x80000;
	else if (gfxv == 110000 ||		/* GFX_VERSION_PLUM_BONITO */
		 gfxv == 110001 ||		/* GFX_VERSION_WHEAT_NAS */
		 gfxv == 120000 ||		/* GFX_VERSION_GFX1200 */
		 gfxv == 120001)		/* GFX_VERSION_GFX1201 */
		vgpr_size = 0x60000;

	return vgpr_size;
}

#define WG_CONTEXT_DATA_SIZE_PER_CU(gfxv, props)	\
	(kfd_get_vgpr_size_per_cu(gfxv) + SGPR_SIZE_PER_CU +\
	 (((gfxv) == 90500) ? (props->lds_size_in_kb << 10) : LDS_SIZE_PER_CU) +\
	 HWREG_SIZE_PER_CU)

#define CNTL_STACK_BYTES_PER_WAVE(gfxv)	\
	((gfxv) >= 100100 ? 12 : 8)	/* GFX_VERSION_NAVI10*/

#define SIZEOF_HSA_USER_CONTEXT_SAVE_AREA_HEADER 40

void kfd_queue_ctx_save_restore_size(struct kfd_topology_device *dev)
{
	struct kfd_node_properties *props = &dev->node_props;
	u32 gfxv = props->gfx_target_version;
	u32 ctl_stack_size;
	u32 wg_data_size;
	u32 wave_num;
	u32 cu_num;

	if (gfxv < 80001)	/* GFX_VERSION_CARRIZO */
		return;

	cu_num = props->simd_count / props->simd_per_cu / NUM_XCC(dev->gpu->xcc_mask);
	wave_num = (gfxv < 100100) ?	/* GFX_VERSION_NAVI10 */
		    min(cu_num * 40, props->array_count / props->simd_arrays_per_engine * 512)
		    : cu_num * 32;

	wg_data_size = ALIGN(cu_num * WG_CONTEXT_DATA_SIZE_PER_CU(gfxv, props), PAGE_SIZE);
	ctl_stack_size = wave_num * CNTL_STACK_BYTES_PER_WAVE(gfxv) + 8;
	ctl_stack_size = ALIGN(SIZEOF_HSA_USER_CONTEXT_SAVE_AREA_HEADER + ctl_stack_size,
			       PAGE_SIZE);

	if ((gfxv / 10000 * 10000) == 100000) {
		/* HW design limits control stack size to 0x7000.
		 * This is insufficient for theoretical PM4 cases
		 * but sufficient for AQL, limited by SPI events.
		 */
		ctl_stack_size = min(ctl_stack_size, 0x7000);
	}

	props->ctl_stack_size = ctl_stack_size;
	props->debug_memory_size = ALIGN(wave_num * DEBUGGER_BYTES_PER_WAVE, DEBUGGER_BYTES_ALIGN);
	props->cwsr_size = ctl_stack_size + wg_data_size;

	if (gfxv == 80002)	/* GFX_VERSION_TONGA */
		props->eop_buffer_size = 0x8000;
	else if ((gfxv / 100 * 100) == 90400)	/* GFX_VERSION_AQUA_VANJARAM */
		props->eop_buffer_size = 4096;
	else if (gfxv >= 80000)
		props->eop_buffer_size = 4096;
}

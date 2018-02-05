/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#include "amdgpu_ids.h"

#include <linux/idr.h>
#include <linux/dma-fence-array.h>
#include <drm/drmP.h>

#include "amdgpu.h"
#include "amdgpu_trace.h"

/*
 * PASID manager
 *
 * PASIDs are global address space identifiers that can be shared
 * between the GPU, an IOMMU and the driver. VMs on different devices
 * may use the same PASID if they share the same address
 * space. Therefore PASIDs are allocated using a global IDA. VMs are
 * looked up from the PASID per amdgpu_device.
 */
static DEFINE_IDA(amdgpu_pasid_ida);

/**
 * amdgpu_pasid_alloc - Allocate a PASID
 * @bits: Maximum width of the PASID in bits, must be at least 1
 *
 * Allocates a PASID of the given width while keeping smaller PASIDs
 * available if possible.
 *
 * Returns a positive integer on success. Returns %-EINVAL if bits==0.
 * Returns %-ENOSPC if no PASID was available. Returns %-ENOMEM on
 * memory allocation failure.
 */
int amdgpu_pasid_alloc(unsigned int bits)
{
	int pasid = -EINVAL;

	for (bits = min(bits, 31U); bits > 0; bits--) {
		pasid = ida_simple_get(&amdgpu_pasid_ida,
				       1U << (bits - 1), 1U << bits,
				       GFP_KERNEL);
		if (pasid != -ENOSPC)
			break;
	}

	return pasid;
}

/**
 * amdgpu_pasid_free - Free a PASID
 * @pasid: PASID to free
 */
void amdgpu_pasid_free(unsigned int pasid)
{
	ida_simple_remove(&amdgpu_pasid_ida, pasid);
}

/*
 * VMID manager
 *
 * VMIDs are a per VMHUB identifier for page tables handling.
 */

/**
 * amdgpu_vmid_had_gpu_reset - check if reset occured since last use
 *
 * @adev: amdgpu_device pointer
 * @id: VMID structure
 *
 * Check if GPU reset occured since last use of the VMID.
 */
bool amdgpu_vmid_had_gpu_reset(struct amdgpu_device *adev,
			       struct amdgpu_vmid *id)
{
	return id->current_gpu_reset_count !=
		atomic_read(&adev->gpu_reset_counter);
}

/* idr_mgr->lock must be held */
static int amdgpu_vmid_grab_reserved_locked(struct amdgpu_vm *vm,
					    struct amdgpu_ring *ring,
					    struct amdgpu_sync *sync,
					    struct dma_fence *fence,
					    struct amdgpu_job *job)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned vmhub = ring->funcs->vmhub;
	uint64_t fence_context = adev->fence_context + ring->idx;
	struct amdgpu_vmid *id = vm->reserved_vmid[vmhub];
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];
	struct dma_fence *updates = sync->last_vm_update;
	int r = 0;
	struct dma_fence *flushed, *tmp;
	bool needs_flush = vm->use_cpu_for_update;

	flushed  = id->flushed_updates;
	if ((amdgpu_vmid_had_gpu_reset(adev, id)) ||
	    (atomic64_read(&id->owner) != vm->entity.fence_context) ||
	    (job->vm_pd_addr != id->pd_gpu_addr) ||
	    (updates && (!flushed || updates->context != flushed->context ||
			dma_fence_is_later(updates, flushed))) ||
	    (!id->last_flush || (id->last_flush->context != fence_context &&
				 !dma_fence_is_signaled(id->last_flush)))) {
		needs_flush = true;
		/* to prevent one context starved by another context */
		id->pd_gpu_addr = 0;
		tmp = amdgpu_sync_peek_fence(&id->active, ring);
		if (tmp) {
			r = amdgpu_sync_fence(adev, sync, tmp, false);
			return r;
		}
	}

	/* Good we can use this VMID. Remember this submission as
	* user of the VMID.
	*/
	r = amdgpu_sync_fence(ring->adev, &id->active, fence, false);
	if (r)
		goto out;

	if (updates && (!flushed || updates->context != flushed->context ||
			dma_fence_is_later(updates, flushed))) {
		dma_fence_put(id->flushed_updates);
		id->flushed_updates = dma_fence_get(updates);
	}
	id->pd_gpu_addr = job->vm_pd_addr;
	atomic64_set(&id->owner, vm->entity.fence_context);
	job->vm_needs_flush = needs_flush;
	if (needs_flush) {
		dma_fence_put(id->last_flush);
		id->last_flush = NULL;
	}
	job->vmid = id - id_mgr->ids;
	trace_amdgpu_vm_grab_id(vm, ring, job);
out:
	return r;
}

/**
 * amdgpu_vm_grab_id - allocate the next free VMID
 *
 * @vm: vm to allocate id for
 * @ring: ring we want to submit job to
 * @sync: sync object where we add dependencies
 * @fence: fence protecting ID from reuse
 *
 * Allocate an id for the vm, adding fences to the sync obj as necessary.
 */
int amdgpu_vmid_grab(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		     struct amdgpu_sync *sync, struct dma_fence *fence,
		     struct amdgpu_job *job)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned vmhub = ring->funcs->vmhub;
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];
	uint64_t fence_context = adev->fence_context + ring->idx;
	struct dma_fence *updates = sync->last_vm_update;
	struct amdgpu_vmid *id, *idle;
	struct dma_fence **fences;
	unsigned i;
	int r = 0;

	mutex_lock(&id_mgr->lock);
	if (vm->reserved_vmid[vmhub]) {
		r = amdgpu_vmid_grab_reserved_locked(vm, ring, sync, fence, job);
		mutex_unlock(&id_mgr->lock);
		return r;
	}
	fences = kmalloc_array(sizeof(void *), id_mgr->num_ids, GFP_KERNEL);
	if (!fences) {
		mutex_unlock(&id_mgr->lock);
		return -ENOMEM;
	}
	/* Check if we have an idle VMID */
	i = 0;
	list_for_each_entry(idle, &id_mgr->ids_lru, list) {
		fences[i] = amdgpu_sync_peek_fence(&idle->active, ring);
		if (!fences[i])
			break;
		++i;
	}

	/* If we can't find a idle VMID to use, wait till one becomes available */
	if (&idle->list == &id_mgr->ids_lru) {
		u64 fence_context = adev->vm_manager.fence_context + ring->idx;
		unsigned seqno = ++adev->vm_manager.seqno[ring->idx];
		struct dma_fence_array *array;
		unsigned j;

		for (j = 0; j < i; ++j)
			dma_fence_get(fences[j]);

		array = dma_fence_array_create(i, fences, fence_context,
					   seqno, true);
		if (!array) {
			for (j = 0; j < i; ++j)
				dma_fence_put(fences[j]);
			kfree(fences);
			r = -ENOMEM;
			goto error;
		}


		r = amdgpu_sync_fence(ring->adev, sync, &array->base, false);
		dma_fence_put(&array->base);
		if (r)
			goto error;

		mutex_unlock(&id_mgr->lock);
		return 0;

	}
	kfree(fences);

	job->vm_needs_flush = vm->use_cpu_for_update;
	/* Check if we can use a VMID already assigned to this VM */
	list_for_each_entry_reverse(id, &id_mgr->ids_lru, list) {
		struct dma_fence *flushed;
		bool needs_flush = vm->use_cpu_for_update;

		/* Check all the prerequisites to using this VMID */
		if (amdgpu_vmid_had_gpu_reset(adev, id))
			continue;

		if (atomic64_read(&id->owner) != vm->entity.fence_context)
			continue;

		if (job->vm_pd_addr != id->pd_gpu_addr)
			continue;

		if (!id->last_flush ||
		    (id->last_flush->context != fence_context &&
		     !dma_fence_is_signaled(id->last_flush)))
			needs_flush = true;

		flushed  = id->flushed_updates;
		if (updates && (!flushed || dma_fence_is_later(updates, flushed)))
			needs_flush = true;

		/* Concurrent flushes are only possible starting with Vega10 */
		if (adev->asic_type < CHIP_VEGA10 && needs_flush)
			continue;

		/* Good we can use this VMID. Remember this submission as
		 * user of the VMID.
		 */
		r = amdgpu_sync_fence(ring->adev, &id->active, fence, false);
		if (r)
			goto error;

		if (updates && (!flushed || dma_fence_is_later(updates, flushed))) {
			dma_fence_put(id->flushed_updates);
			id->flushed_updates = dma_fence_get(updates);
		}

		if (needs_flush)
			goto needs_flush;
		else
			goto no_flush_needed;

	};

	/* Still no ID to use? Then use the idle one found earlier */
	id = idle;

	/* Remember this submission as user of the VMID */
	r = amdgpu_sync_fence(ring->adev, &id->active, fence, false);
	if (r)
		goto error;

	id->pd_gpu_addr = job->vm_pd_addr;
	dma_fence_put(id->flushed_updates);
	id->flushed_updates = dma_fence_get(updates);
	atomic64_set(&id->owner, vm->entity.fence_context);

needs_flush:
	job->vm_needs_flush = true;
	dma_fence_put(id->last_flush);
	id->last_flush = NULL;

no_flush_needed:
	list_move_tail(&id->list, &id_mgr->ids_lru);

	job->vmid = id - id_mgr->ids;
	trace_amdgpu_vm_grab_id(vm, ring, job);

error:
	mutex_unlock(&id_mgr->lock);
	return r;
}

int amdgpu_vmid_alloc_reserved(struct amdgpu_device *adev,
			       struct amdgpu_vm *vm,
			       unsigned vmhub)
{
	struct amdgpu_vmid_mgr *id_mgr;
	struct amdgpu_vmid *idle;
	int r = 0;

	id_mgr = &adev->vm_manager.id_mgr[vmhub];
	mutex_lock(&id_mgr->lock);
	if (vm->reserved_vmid[vmhub])
		goto unlock;
	if (atomic_inc_return(&id_mgr->reserved_vmid_num) >
	    AMDGPU_VM_MAX_RESERVED_VMID) {
		DRM_ERROR("Over limitation of reserved vmid\n");
		atomic_dec(&id_mgr->reserved_vmid_num);
		r = -EINVAL;
		goto unlock;
	}
	/* Select the first entry VMID */
	idle = list_first_entry(&id_mgr->ids_lru, struct amdgpu_vmid, list);
	list_del_init(&idle->list);
	vm->reserved_vmid[vmhub] = idle;
	mutex_unlock(&id_mgr->lock);

	return 0;
unlock:
	mutex_unlock(&id_mgr->lock);
	return r;
}

void amdgpu_vmid_free_reserved(struct amdgpu_device *adev,
			       struct amdgpu_vm *vm,
			       unsigned vmhub)
{
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];

	mutex_lock(&id_mgr->lock);
	if (vm->reserved_vmid[vmhub]) {
		list_add(&vm->reserved_vmid[vmhub]->list,
			&id_mgr->ids_lru);
		vm->reserved_vmid[vmhub] = NULL;
		atomic_dec(&id_mgr->reserved_vmid_num);
	}
	mutex_unlock(&id_mgr->lock);
}

/**
 * amdgpu_vmid_reset - reset VMID to zero
 *
 * @adev: amdgpu device structure
 * @vmid: vmid number to use
 *
 * Reset saved GDW, GWS and OA to force switch on next flush.
 */
void amdgpu_vmid_reset(struct amdgpu_device *adev, unsigned vmhub,
		       unsigned vmid)
{
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];
	struct amdgpu_vmid *id = &id_mgr->ids[vmid];

	atomic64_set(&id->owner, 0);
	id->gds_base = 0;
	id->gds_size = 0;
	id->gws_base = 0;
	id->gws_size = 0;
	id->oa_base = 0;
	id->oa_size = 0;
}

/**
 * amdgpu_vmid_reset_all - reset VMID to zero
 *
 * @adev: amdgpu device structure
 *
 * Reset VMID to force flush on next use
 */
void amdgpu_vmid_reset_all(struct amdgpu_device *adev)
{
	unsigned i, j;

	for (i = 0; i < AMDGPU_MAX_VMHUBS; ++i) {
		struct amdgpu_vmid_mgr *id_mgr =
			&adev->vm_manager.id_mgr[i];

		for (j = 1; j < id_mgr->num_ids; ++j)
			amdgpu_vmid_reset(adev, i, j);
	}
}

/**
 * amdgpu_vmid_mgr_init - init the VMID manager
 *
 * @adev: amdgpu_device pointer
 *
 * Initialize the VM manager structures
 */
void amdgpu_vmid_mgr_init(struct amdgpu_device *adev)
{
	unsigned i, j;

	for (i = 0; i < AMDGPU_MAX_VMHUBS; ++i) {
		struct amdgpu_vmid_mgr *id_mgr =
			&adev->vm_manager.id_mgr[i];

		mutex_init(&id_mgr->lock);
		INIT_LIST_HEAD(&id_mgr->ids_lru);
		atomic_set(&id_mgr->reserved_vmid_num, 0);

		/* skip over VMID 0, since it is the system VM */
		for (j = 1; j < id_mgr->num_ids; ++j) {
			amdgpu_vmid_reset(adev, i, j);
			amdgpu_sync_create(&id_mgr->ids[i].active);
			list_add_tail(&id_mgr->ids[j].list, &id_mgr->ids_lru);
		}
	}

	adev->vm_manager.fence_context =
		dma_fence_context_alloc(AMDGPU_MAX_RINGS);
	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		adev->vm_manager.seqno[i] = 0;
}

/**
 * amdgpu_vmid_mgr_fini - cleanup VM manager
 *
 * @adev: amdgpu_device pointer
 *
 * Cleanup the VM manager and free resources.
 */
void amdgpu_vmid_mgr_fini(struct amdgpu_device *adev)
{
	unsigned i, j;

	for (i = 0; i < AMDGPU_MAX_VMHUBS; ++i) {
		struct amdgpu_vmid_mgr *id_mgr =
			&adev->vm_manager.id_mgr[i];

		mutex_destroy(&id_mgr->lock);
		for (j = 0; j < AMDGPU_NUM_VMID; ++j) {
			struct amdgpu_vmid *id = &id_mgr->ids[j];

			amdgpu_sync_free(&id->active);
			dma_fence_put(id->flushed_updates);
			dma_fence_put(id->last_flush);
		}
	}
}

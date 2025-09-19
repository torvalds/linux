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

/* Helper to free pasid from a fence callback */
struct amdgpu_pasid_cb {
	struct dma_fence_cb cb;
	u32 pasid;
};

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
		pasid = ida_alloc_range(&amdgpu_pasid_ida, 1U << (bits - 1),
					(1U << bits) - 1, GFP_KERNEL);
		if (pasid != -ENOSPC)
			break;
	}

	if (pasid >= 0)
		trace_amdgpu_pasid_allocated(pasid);

	return pasid;
}

/**
 * amdgpu_pasid_free - Free a PASID
 * @pasid: PASID to free
 */
void amdgpu_pasid_free(u32 pasid)
{
	trace_amdgpu_pasid_freed(pasid);
	ida_free(&amdgpu_pasid_ida, pasid);
}

static void amdgpu_pasid_free_cb(struct dma_fence *fence,
				 struct dma_fence_cb *_cb)
{
	struct amdgpu_pasid_cb *cb =
		container_of(_cb, struct amdgpu_pasid_cb, cb);

	amdgpu_pasid_free(cb->pasid);
	dma_fence_put(fence);
	kfree(cb);
}

/**
 * amdgpu_pasid_free_delayed - free pasid when fences signal
 *
 * @resv: reservation object with the fences to wait for
 * @pasid: pasid to free
 *
 * Free the pasid only after all the fences in resv are signaled.
 */
void amdgpu_pasid_free_delayed(struct dma_resv *resv,
			       u32 pasid)
{
	struct amdgpu_pasid_cb *cb;
	struct dma_fence *fence;
	int r;

	r = dma_resv_get_singleton(resv, DMA_RESV_USAGE_BOOKKEEP, &fence);
	if (r)
		goto fallback;

	if (!fence) {
		amdgpu_pasid_free(pasid);
		return;
	}

	cb = kmalloc(sizeof(*cb), GFP_KERNEL);
	if (!cb) {
		/* Last resort when we are OOM */
		dma_fence_wait(fence, false);
		dma_fence_put(fence);
		amdgpu_pasid_free(pasid);
	} else {
		cb->pasid = pasid;
		if (dma_fence_add_callback(fence, &cb->cb,
					   amdgpu_pasid_free_cb))
			amdgpu_pasid_free_cb(fence, &cb->cb);
	}

	return;

fallback:
	/* Not enough memory for the delayed delete, as last resort
	 * block for all the fences to complete.
	 */
	dma_resv_wait_timeout(resv, DMA_RESV_USAGE_BOOKKEEP,
			      false, MAX_SCHEDULE_TIMEOUT);
	amdgpu_pasid_free(pasid);
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

/* Check if we need to switch to another set of resources */
static bool amdgpu_vmid_gds_switch_needed(struct amdgpu_vmid *id,
					  struct amdgpu_job *job)
{
	return id->gds_base != job->gds_base ||
		id->gds_size != job->gds_size ||
		id->gws_base != job->gws_base ||
		id->gws_size != job->gws_size ||
		id->oa_base != job->oa_base ||
		id->oa_size != job->oa_size;
}

/* Check if the id is compatible with the job */
static bool amdgpu_vmid_compatible(struct amdgpu_vmid *id,
				   struct amdgpu_job *job)
{
	return  id->pd_gpu_addr == job->vm_pd_addr &&
		!amdgpu_vmid_gds_switch_needed(id, job);
}

/**
 * amdgpu_vmid_grab_idle - grab idle VMID
 *
 * @ring: ring we want to submit job to
 * @idle: resulting idle VMID
 * @fence: fence to wait for if no id could be grabbed
 *
 * Try to find an idle VMID, if none is idle add a fence to wait to the sync
 * object. Returns -ENOMEM when we are out of memory.
 */
static int amdgpu_vmid_grab_idle(struct amdgpu_ring *ring,
				 struct amdgpu_vmid **idle,
				 struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned vmhub = ring->vm_hub;
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];
	struct dma_fence **fences;
	unsigned i;

	if (!dma_fence_is_signaled(ring->vmid_wait)) {
		*fence = dma_fence_get(ring->vmid_wait);
		return 0;
	}

	fences = kmalloc_array(id_mgr->num_ids, sizeof(void *), GFP_NOWAIT);
	if (!fences)
		return -ENOMEM;

	/* Check if we have an idle VMID */
	i = 0;
	list_for_each_entry((*idle), &id_mgr->ids_lru, list) {
		/* Don't use per engine and per process VMID at the same time */
		struct amdgpu_ring *r = adev->vm_manager.concurrent_flush ?
			NULL : ring;

		fences[i] = amdgpu_sync_peek_fence(&(*idle)->active, r);
		if (!fences[i])
			break;
		++i;
	}

	/* If we can't find a idle VMID to use, wait till one becomes available */
	if (&(*idle)->list == &id_mgr->ids_lru) {
		u64 fence_context = adev->vm_manager.fence_context + ring->idx;
		unsigned seqno = ++adev->vm_manager.seqno[ring->idx];
		struct dma_fence_array *array;
		unsigned j;

		*idle = NULL;
		for (j = 0; j < i; ++j)
			dma_fence_get(fences[j]);

		array = dma_fence_array_create(i, fences, fence_context,
					       seqno, true);
		if (!array) {
			for (j = 0; j < i; ++j)
				dma_fence_put(fences[j]);
			kfree(fences);
			return -ENOMEM;
		}

		*fence = dma_fence_get(&array->base);
		dma_fence_put(ring->vmid_wait);
		ring->vmid_wait = &array->base;
		return 0;
	}
	kfree(fences);

	return 0;
}

/**
 * amdgpu_vmid_grab_reserved - try to assign reserved VMID
 *
 * @vm: vm to allocate id for
 * @ring: ring we want to submit job to
 * @job: job who wants to use the VMID
 * @id: resulting VMID
 * @fence: fence to wait for if no id could be grabbed
 *
 * Try to assign a reserved VMID.
 */
static int amdgpu_vmid_grab_reserved(struct amdgpu_vm *vm,
				     struct amdgpu_ring *ring,
				     struct amdgpu_job *job,
				     struct amdgpu_vmid **id,
				     struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned vmhub = ring->vm_hub;
	uint64_t fence_context = adev->fence_context + ring->idx;
	bool needs_flush = vm->use_cpu_for_update;
	uint64_t updates = amdgpu_vm_tlb_seq(vm);
	int r;

	*id = vm->reserved_vmid[vmhub];
	if ((*id)->owner != vm->immediate.fence_context ||
	    !amdgpu_vmid_compatible(*id, job) ||
	    (*id)->flushed_updates < updates ||
	    !(*id)->last_flush ||
	    ((*id)->last_flush->context != fence_context &&
	     !dma_fence_is_signaled((*id)->last_flush)))
		needs_flush = true;

	if ((*id)->owner != vm->immediate.fence_context ||
	    (!adev->vm_manager.concurrent_flush && needs_flush)) {
		struct dma_fence *tmp;

		/* Don't use per engine and per process VMID at the
		 * same time
		 */
		if (adev->vm_manager.concurrent_flush)
			ring = NULL;

		/* to prevent one context starved by another context */
		(*id)->pd_gpu_addr = 0;
		tmp = amdgpu_sync_peek_fence(&(*id)->active, ring);
		if (tmp) {
			*id = NULL;
			*fence = dma_fence_get(tmp);
			return 0;
		}
	}

	/* Good we can use this VMID. Remember this submission as
	* user of the VMID.
	*/
	r = amdgpu_sync_fence(&(*id)->active, &job->base.s_fence->finished,
			      GFP_NOWAIT);
	if (r)
		return r;

	job->vm_needs_flush = needs_flush;
	job->spm_update_needed = true;
	return 0;
}

/**
 * amdgpu_vmid_grab_used - try to reuse a VMID
 *
 * @vm: vm to allocate id for
 * @ring: ring we want to submit job to
 * @job: job who wants to use the VMID
 * @id: resulting VMID
 *
 * Try to reuse a VMID for this submission.
 */
static int amdgpu_vmid_grab_used(struct amdgpu_vm *vm,
				 struct amdgpu_ring *ring,
				 struct amdgpu_job *job,
				 struct amdgpu_vmid **id)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned vmhub = ring->vm_hub;
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];
	uint64_t fence_context = adev->fence_context + ring->idx;
	uint64_t updates = amdgpu_vm_tlb_seq(vm);
	int r;

	job->vm_needs_flush = vm->use_cpu_for_update;

	/* Check if we can use a VMID already assigned to this VM */
	list_for_each_entry_reverse((*id), &id_mgr->ids_lru, list) {
		bool needs_flush = vm->use_cpu_for_update;

		/* Check all the prerequisites to using this VMID */
		if ((*id)->owner != vm->immediate.fence_context)
			continue;

		if (!amdgpu_vmid_compatible(*id, job))
			continue;

		if (!(*id)->last_flush ||
		    ((*id)->last_flush->context != fence_context &&
		     !dma_fence_is_signaled((*id)->last_flush)))
			needs_flush = true;

		if ((*id)->flushed_updates < updates)
			needs_flush = true;

		if (needs_flush && !adev->vm_manager.concurrent_flush)
			continue;

		/* Good, we can use this VMID. Remember this submission as
		 * user of the VMID.
		 */
		r = amdgpu_sync_fence(&(*id)->active,
				      &job->base.s_fence->finished,
				      GFP_NOWAIT);
		if (r)
			return r;

		job->vm_needs_flush |= needs_flush;
		return 0;
	}

	*id = NULL;
	return 0;
}

/**
 * amdgpu_vmid_grab - allocate the next free VMID
 *
 * @vm: vm to allocate id for
 * @ring: ring we want to submit job to
 * @job: job who wants to use the VMID
 * @fence: fence to wait for if no id could be grabbed
 *
 * Allocate an id for the vm, adding fences to the sync obj as necessary.
 */
int amdgpu_vmid_grab(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		     struct amdgpu_job *job, struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned vmhub = ring->vm_hub;
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];
	struct amdgpu_vmid *idle = NULL;
	struct amdgpu_vmid *id = NULL;
	int r = 0;

	mutex_lock(&id_mgr->lock);
	r = amdgpu_vmid_grab_idle(ring, &idle, fence);
	if (r || !idle)
		goto error;

	if (amdgpu_vmid_uses_reserved(vm, vmhub)) {
		r = amdgpu_vmid_grab_reserved(vm, ring, job, &id, fence);
		if (r || !id)
			goto error;
	} else {
		r = amdgpu_vmid_grab_used(vm, ring, job, &id);
		if (r)
			goto error;

		if (!id) {
			/* Still no ID to use? Then use the idle one found earlier */
			id = idle;

			/* Remember this submission as user of the VMID */
			r = amdgpu_sync_fence(&id->active,
					      &job->base.s_fence->finished,
					      GFP_NOWAIT);
			if (r)
				goto error;

			job->vm_needs_flush = true;
		}

		list_move_tail(&id->list, &id_mgr->ids_lru);
	}

	job->gds_switch_needed = amdgpu_vmid_gds_switch_needed(id, job);
	if (job->vm_needs_flush) {
		id->flushed_updates = amdgpu_vm_tlb_seq(vm);
		dma_fence_put(id->last_flush);
		id->last_flush = NULL;
	}
	job->vmid = id - id_mgr->ids;
	job->pasid = vm->pasid;

	id->gds_base = job->gds_base;
	id->gds_size = job->gds_size;
	id->gws_base = job->gws_base;
	id->gws_size = job->gws_size;
	id->oa_base = job->oa_base;
	id->oa_size = job->oa_size;
	id->pd_gpu_addr = job->vm_pd_addr;
	id->owner = vm->immediate.fence_context;

	trace_amdgpu_vm_grab_id(vm, ring, job);

error:
	mutex_unlock(&id_mgr->lock);
	return r;
}

/*
 * amdgpu_vmid_uses_reserved - check if a VM will use a reserved VMID
 * @vm: the VM to check
 * @vmhub: the VMHUB which will be used
 *
 * Returns: True if the VM will use a reserved VMID.
 */
bool amdgpu_vmid_uses_reserved(struct amdgpu_vm *vm, unsigned int vmhub)
{
	return vm->reserved_vmid[vmhub];
}

/*
 * amdgpu_vmid_alloc_reserved - reserve a specific VMID for this vm
 * @adev: amdgpu device structure
 * @vm: the VM to reserve an ID for
 * @vmhub: the VMHUB which should be used
 *
 * Mostly used to have a reserved VMID for debugging and SPM.
 *
 * Returns: 0 for success, -ENOENT if an ID is already reserved.
 */
int amdgpu_vmid_alloc_reserved(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			       unsigned vmhub)
{
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];
	struct amdgpu_vmid *id;
	int r = 0;

	mutex_lock(&id_mgr->lock);
	if (vm->reserved_vmid[vmhub])
		goto unlock;
	if (id_mgr->reserved_vmid) {
		r = -ENOENT;
		goto unlock;
	}
	/* Remove from normal round robin handling */
	id = list_first_entry(&id_mgr->ids_lru, struct amdgpu_vmid, list);
	list_del_init(&id->list);
	vm->reserved_vmid[vmhub] = id;
	id_mgr->reserved_vmid = true;
	mutex_unlock(&id_mgr->lock);

	return 0;
unlock:
	mutex_unlock(&id_mgr->lock);
	return r;
}

/*
 * amdgpu_vmid_free_reserved - free up a reserved VMID again
 * @adev: amdgpu device structure
 * @vm: the VM with the reserved ID
 * @vmhub: the VMHUB which should be used
 */
void amdgpu_vmid_free_reserved(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			       unsigned vmhub)
{
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];

	mutex_lock(&id_mgr->lock);
	if (vm->reserved_vmid[vmhub]) {
		list_add(&vm->reserved_vmid[vmhub]->list,
			&id_mgr->ids_lru);
		vm->reserved_vmid[vmhub] = NULL;
		id_mgr->reserved_vmid = false;
	}
	mutex_unlock(&id_mgr->lock);
}

/**
 * amdgpu_vmid_reset - reset VMID to zero
 *
 * @adev: amdgpu device structure
 * @vmhub: vmhub type
 * @vmid: vmid number to use
 *
 * Reset saved GDW, GWS and OA to force switch on next flush.
 */
void amdgpu_vmid_reset(struct amdgpu_device *adev, unsigned vmhub,
		       unsigned vmid)
{
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];
	struct amdgpu_vmid *id = &id_mgr->ids[vmid];

	mutex_lock(&id_mgr->lock);
	id->owner = 0;
	id->gds_base = 0;
	id->gds_size = 0;
	id->gws_base = 0;
	id->gws_size = 0;
	id->oa_base = 0;
	id->oa_size = 0;
	mutex_unlock(&id_mgr->lock);
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

		/* for GC <10, SDMA uses MMHUB so use first_kfd_vmid for both GC and MM */
		if (amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(10, 0, 0))
			/* manage only VMIDs not used by KFD */
			id_mgr->num_ids = adev->vm_manager.first_kfd_vmid;
		else if (AMDGPU_IS_MMHUB0(i) ||
			 AMDGPU_IS_MMHUB1(i))
			id_mgr->num_ids = 16;
		else
			/* manage only VMIDs not used by KFD */
			id_mgr->num_ids = adev->vm_manager.first_kfd_vmid;

		/* skip over VMID 0, since it is the system VM */
		for (j = 1; j < id_mgr->num_ids; ++j) {
			amdgpu_vmid_reset(adev, i, j);
			amdgpu_sync_create(&id_mgr->ids[j].active);
			list_add_tail(&id_mgr->ids[j].list, &id_mgr->ids_lru);
		}
	}
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
			dma_fence_put(id->last_flush);
			dma_fence_put(id->pasid_mapping);
		}
	}
}

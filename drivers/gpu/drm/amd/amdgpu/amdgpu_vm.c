/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <linux/fence-array.h>
#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"

/*
 * GPUVM
 * GPUVM is similar to the legacy gart on older asics, however
 * rather than there being a single global gart table
 * for the entire GPU, there are multiple VM page tables active
 * at any given time.  The VM page tables can contain a mix
 * vram pages and system memory pages and system memory pages
 * can be mapped as snooped (cached system pages) or unsnooped
 * (uncached system pages).
 * Each VM has an ID associated with it and there is a page table
 * associated with each VMID.  When execting a command buffer,
 * the kernel tells the the ring what VMID to use for that command
 * buffer.  VMIDs are allocated dynamically as commands are submitted.
 * The userspace drivers maintain their own address space and the kernel
 * sets up their pages tables accordingly when they submit their
 * command buffers and a VMID is assigned.
 * Cayman/Trinity support up to 8 active VMs at any given time;
 * SI supports 16.
 */

/* Local structure. Encapsulate some VM table update parameters to reduce
 * the number of function parameters
 */
struct amdgpu_pte_update_params {
	/* amdgpu device we do this update for */
	struct amdgpu_device *adev;
	/* address where to copy page table entries from */
	uint64_t src;
	/* indirect buffer to fill with commands */
	struct amdgpu_ib *ib;
	/* Function which actually does the update */
	void (*func)(struct amdgpu_pte_update_params *params, uint64_t pe,
		     uint64_t addr, unsigned count, uint32_t incr,
		     uint32_t flags);
	/* indicate update pt or its shadow */
	bool shadow;
};

/**
 * amdgpu_vm_num_pde - return the number of page directory entries
 *
 * @adev: amdgpu_device pointer
 *
 * Calculate the number of page directory entries.
 */
static unsigned amdgpu_vm_num_pdes(struct amdgpu_device *adev)
{
	return adev->vm_manager.max_pfn >> amdgpu_vm_block_size;
}

/**
 * amdgpu_vm_directory_size - returns the size of the page directory in bytes
 *
 * @adev: amdgpu_device pointer
 *
 * Calculate the size of the page directory in bytes.
 */
static unsigned amdgpu_vm_directory_size(struct amdgpu_device *adev)
{
	return AMDGPU_GPU_PAGE_ALIGN(amdgpu_vm_num_pdes(adev) * 8);
}

/**
 * amdgpu_vm_get_pd_bo - add the VM PD to a validation list
 *
 * @vm: vm providing the BOs
 * @validated: head of validation list
 * @entry: entry to add
 *
 * Add the page directory to the list of BOs to
 * validate for command submission.
 */
void amdgpu_vm_get_pd_bo(struct amdgpu_vm *vm,
			 struct list_head *validated,
			 struct amdgpu_bo_list_entry *entry)
{
	entry->robj = vm->page_directory;
	entry->priority = 0;
	entry->tv.bo = &vm->page_directory->tbo;
	entry->tv.shared = true;
	entry->user_pages = NULL;
	list_add(&entry->tv.head, validated);
}

/**
 * amdgpu_vm_get_bos - add the vm BOs to a duplicates list
 *
 * @adev: amdgpu device pointer
 * @vm: vm providing the BOs
 * @duplicates: head of duplicates list
 *
 * Add the page directory to the BO duplicates list
 * for command submission.
 */
void amdgpu_vm_get_pt_bos(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			  struct list_head *duplicates)
{
	uint64_t num_evictions;
	unsigned i;

	/* We only need to validate the page tables
	 * if they aren't already valid.
	 */
	num_evictions = atomic64_read(&adev->num_evictions);
	if (num_evictions == vm->last_eviction_counter)
		return;

	/* add the vm page table to the list */
	for (i = 0; i <= vm->max_pde_used; ++i) {
		struct amdgpu_bo_list_entry *entry = &vm->page_tables[i].entry;

		if (!entry->robj)
			continue;

		list_add(&entry->tv.head, duplicates);
	}

}

/**
 * amdgpu_vm_move_pt_bos_in_lru - move the PT BOs to the LRU tail
 *
 * @adev: amdgpu device instance
 * @vm: vm providing the BOs
 *
 * Move the PT BOs to the tail of the LRU.
 */
void amdgpu_vm_move_pt_bos_in_lru(struct amdgpu_device *adev,
				  struct amdgpu_vm *vm)
{
	struct ttm_bo_global *glob = adev->mman.bdev.glob;
	unsigned i;

	spin_lock(&glob->lru_lock);
	for (i = 0; i <= vm->max_pde_used; ++i) {
		struct amdgpu_bo_list_entry *entry = &vm->page_tables[i].entry;

		if (!entry->robj)
			continue;

		ttm_bo_move_to_lru_tail(&entry->robj->tbo);
	}
	spin_unlock(&glob->lru_lock);
}

static bool amdgpu_vm_is_gpu_reset(struct amdgpu_device *adev,
			      struct amdgpu_vm_id *id)
{
	return id->current_gpu_reset_count !=
		atomic_read(&adev->gpu_reset_counter) ? true : false;
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
int amdgpu_vm_grab_id(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		      struct amdgpu_sync *sync, struct fence *fence,
		      struct amdgpu_job *job)
{
	struct amdgpu_device *adev = ring->adev;
	uint64_t fence_context = adev->fence_context + ring->idx;
	struct fence *updates = sync->last_vm_update;
	struct amdgpu_vm_id *id, *idle;
	struct fence **fences;
	unsigned i;
	int r = 0;

	fences = kmalloc_array(sizeof(void *), adev->vm_manager.num_ids,
			       GFP_KERNEL);
	if (!fences)
		return -ENOMEM;

	mutex_lock(&adev->vm_manager.lock);

	/* Check if we have an idle VMID */
	i = 0;
	list_for_each_entry(idle, &adev->vm_manager.ids_lru, list) {
		fences[i] = amdgpu_sync_peek_fence(&idle->active, ring);
		if (!fences[i])
			break;
		++i;
	}

	/* If we can't find a idle VMID to use, wait till one becomes available */
	if (&idle->list == &adev->vm_manager.ids_lru) {
		u64 fence_context = adev->vm_manager.fence_context + ring->idx;
		unsigned seqno = ++adev->vm_manager.seqno[ring->idx];
		struct fence_array *array;
		unsigned j;

		for (j = 0; j < i; ++j)
			fence_get(fences[j]);

		array = fence_array_create(i, fences, fence_context,
					   seqno, true);
		if (!array) {
			for (j = 0; j < i; ++j)
				fence_put(fences[j]);
			kfree(fences);
			r = -ENOMEM;
			goto error;
		}


		r = amdgpu_sync_fence(ring->adev, sync, &array->base);
		fence_put(&array->base);
		if (r)
			goto error;

		mutex_unlock(&adev->vm_manager.lock);
		return 0;

	}
	kfree(fences);

	job->vm_needs_flush = true;
	/* Check if we can use a VMID already assigned to this VM */
	i = ring->idx;
	do {
		struct fence *flushed;

		id = vm->ids[i++];
		if (i == AMDGPU_MAX_RINGS)
			i = 0;

		/* Check all the prerequisites to using this VMID */
		if (!id)
			continue;
		if (amdgpu_vm_is_gpu_reset(adev, id))
			continue;

		if (atomic64_read(&id->owner) != vm->client_id)
			continue;

		if (job->vm_pd_addr != id->pd_gpu_addr)
			continue;

		if (!id->last_flush)
			continue;

		if (id->last_flush->context != fence_context &&
		    !fence_is_signaled(id->last_flush))
			continue;

		flushed  = id->flushed_updates;
		if (updates &&
		    (!flushed || fence_is_later(updates, flushed)))
			continue;

		/* Good we can use this VMID. Remember this submission as
		 * user of the VMID.
		 */
		r = amdgpu_sync_fence(ring->adev, &id->active, fence);
		if (r)
			goto error;

		id->current_gpu_reset_count = atomic_read(&adev->gpu_reset_counter);
		list_move_tail(&id->list, &adev->vm_manager.ids_lru);
		vm->ids[ring->idx] = id;

		job->vm_id = id - adev->vm_manager.ids;
		job->vm_needs_flush = false;
		trace_amdgpu_vm_grab_id(vm, ring->idx, job);

		mutex_unlock(&adev->vm_manager.lock);
		return 0;

	} while (i != ring->idx);

	/* Still no ID to use? Then use the idle one found earlier */
	id = idle;

	/* Remember this submission as user of the VMID */
	r = amdgpu_sync_fence(ring->adev, &id->active, fence);
	if (r)
		goto error;

	fence_put(id->first);
	id->first = fence_get(fence);

	fence_put(id->last_flush);
	id->last_flush = NULL;

	fence_put(id->flushed_updates);
	id->flushed_updates = fence_get(updates);

	id->pd_gpu_addr = job->vm_pd_addr;
	id->current_gpu_reset_count = atomic_read(&adev->gpu_reset_counter);
	list_move_tail(&id->list, &adev->vm_manager.ids_lru);
	atomic64_set(&id->owner, vm->client_id);
	vm->ids[ring->idx] = id;

	job->vm_id = id - adev->vm_manager.ids;
	trace_amdgpu_vm_grab_id(vm, ring->idx, job);

error:
	mutex_unlock(&adev->vm_manager.lock);
	return r;
}

static bool amdgpu_vm_ring_has_compute_vm_bug(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	const struct amdgpu_ip_block_version *ip_block;

	if (ring->type != AMDGPU_RING_TYPE_COMPUTE)
		/* only compute rings */
		return false;

	ip_block = amdgpu_get_ip_block(adev, AMD_IP_BLOCK_TYPE_GFX);
	if (!ip_block)
		return false;

	if (ip_block->major <= 7) {
		/* gfx7 has no workaround */
		return true;
	} else if (ip_block->major == 8) {
		if (adev->gfx.mec_fw_version >= 673)
			/* gfx8 is fixed in MEC firmware 673 */
			return false;
		else
			return true;
	}
	return false;
}

/**
 * amdgpu_vm_flush - hardware flush the vm
 *
 * @ring: ring to use for flush
 * @vm_id: vmid number to use
 * @pd_addr: address of the page directory
 *
 * Emit a VM flush when it is necessary.
 */
int amdgpu_vm_flush(struct amdgpu_ring *ring, struct amdgpu_job *job)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_vm_id *id = &adev->vm_manager.ids[job->vm_id];
	bool gds_switch_needed = ring->funcs->emit_gds_switch && (
		id->gds_base != job->gds_base ||
		id->gds_size != job->gds_size ||
		id->gws_base != job->gws_base ||
		id->gws_size != job->gws_size ||
		id->oa_base != job->oa_base ||
		id->oa_size != job->oa_size);
	int r;

	if (ring->funcs->emit_pipeline_sync && (
	    job->vm_needs_flush || gds_switch_needed ||
	    amdgpu_vm_ring_has_compute_vm_bug(ring)))
		amdgpu_ring_emit_pipeline_sync(ring);

	if (ring->funcs->emit_vm_flush && (job->vm_needs_flush ||
	    amdgpu_vm_is_gpu_reset(adev, id))) {
		struct fence *fence;

		trace_amdgpu_vm_flush(job->vm_pd_addr, ring->idx, job->vm_id);
		amdgpu_ring_emit_vm_flush(ring, job->vm_id, job->vm_pd_addr);

		r = amdgpu_fence_emit(ring, &fence);
		if (r)
			return r;

		mutex_lock(&adev->vm_manager.lock);
		fence_put(id->last_flush);
		id->last_flush = fence;
		mutex_unlock(&adev->vm_manager.lock);
	}

	if (gds_switch_needed) {
		id->gds_base = job->gds_base;
		id->gds_size = job->gds_size;
		id->gws_base = job->gws_base;
		id->gws_size = job->gws_size;
		id->oa_base = job->oa_base;
		id->oa_size = job->oa_size;
		amdgpu_ring_emit_gds_switch(ring, job->vm_id,
					    job->gds_base, job->gds_size,
					    job->gws_base, job->gws_size,
					    job->oa_base, job->oa_size);
	}

	return 0;
}

/**
 * amdgpu_vm_reset_id - reset VMID to zero
 *
 * @adev: amdgpu device structure
 * @vm_id: vmid number to use
 *
 * Reset saved GDW, GWS and OA to force switch on next flush.
 */
void amdgpu_vm_reset_id(struct amdgpu_device *adev, unsigned vm_id)
{
	struct amdgpu_vm_id *id = &adev->vm_manager.ids[vm_id];

	id->gds_base = 0;
	id->gds_size = 0;
	id->gws_base = 0;
	id->gws_size = 0;
	id->oa_base = 0;
	id->oa_size = 0;
}

/**
 * amdgpu_vm_bo_find - find the bo_va for a specific vm & bo
 *
 * @vm: requested vm
 * @bo: requested buffer object
 *
 * Find @bo inside the requested vm.
 * Search inside the @bos vm list for the requested vm
 * Returns the found bo_va or NULL if none is found
 *
 * Object has to be reserved!
 */
struct amdgpu_bo_va *amdgpu_vm_bo_find(struct amdgpu_vm *vm,
				       struct amdgpu_bo *bo)
{
	struct amdgpu_bo_va *bo_va;

	list_for_each_entry(bo_va, &bo->va, bo_list) {
		if (bo_va->vm == vm) {
			return bo_va;
		}
	}
	return NULL;
}

/**
 * amdgpu_vm_do_set_ptes - helper to call the right asic function
 *
 * @params: see amdgpu_pte_update_params definition
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: hw access flags
 *
 * Traces the parameters and calls the right asic functions
 * to setup the page table using the DMA.
 */
static void amdgpu_vm_do_set_ptes(struct amdgpu_pte_update_params *params,
				  uint64_t pe, uint64_t addr,
				  unsigned count, uint32_t incr,
				  uint32_t flags)
{
	trace_amdgpu_vm_set_page(pe, addr, count, incr, flags);

	if (count < 3) {
		amdgpu_vm_write_pte(params->adev, params->ib, pe,
				    addr | flags, count, incr);

	} else {
		amdgpu_vm_set_pte_pde(params->adev, params->ib, pe, addr,
				      count, incr, flags);
	}
}

/**
 * amdgpu_vm_do_copy_ptes - copy the PTEs from the GART
 *
 * @params: see amdgpu_pte_update_params definition
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: hw access flags
 *
 * Traces the parameters and calls the DMA function to copy the PTEs.
 */
static void amdgpu_vm_do_copy_ptes(struct amdgpu_pte_update_params *params,
				   uint64_t pe, uint64_t addr,
				   unsigned count, uint32_t incr,
				   uint32_t flags)
{
	trace_amdgpu_vm_set_page(pe, addr, count, incr, flags);

	amdgpu_vm_copy_pte(params->adev, params->ib, pe,
			   (params->src + (addr >> 12) * 8), count);
}

/**
 * amdgpu_vm_clear_bo - initially clear the page dir/table
 *
 * @adev: amdgpu_device pointer
 * @bo: bo to clear
 *
 * need to reserve bo first before calling it.
 */
static int amdgpu_vm_clear_bo(struct amdgpu_device *adev,
			      struct amdgpu_vm *vm,
			      struct amdgpu_bo *bo)
{
	struct amdgpu_ring *ring;
	struct fence *fence = NULL;
	struct amdgpu_job *job;
	struct amdgpu_pte_update_params params;
	unsigned entries;
	uint64_t addr;
	int r;

	ring = container_of(vm->entity.sched, struct amdgpu_ring, sched);

	r = reservation_object_reserve_shared(bo->tbo.resv);
	if (r)
		return r;

	r = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
	if (r)
		goto error;

	r = amdgpu_ttm_bind(&bo->tbo, &bo->tbo.mem);
	if (r)
		goto error;

	addr = amdgpu_bo_gpu_offset(bo);
	entries = amdgpu_bo_size(bo) / 8;

	r = amdgpu_job_alloc_with_ib(adev, 64, &job);
	if (r)
		goto error;

	memset(&params, 0, sizeof(params));
	params.adev = adev;
	params.ib = &job->ibs[0];
	amdgpu_vm_do_set_ptes(&params, addr, 0, entries, 0, 0);
	amdgpu_ring_pad_ib(ring, &job->ibs[0]);

	WARN_ON(job->ibs[0].length_dw > 64);
	r = amdgpu_job_submit(job, ring, &vm->entity,
			      AMDGPU_FENCE_OWNER_VM, &fence);
	if (r)
		goto error_free;

	amdgpu_bo_fence(bo, fence, true);
	fence_put(fence);
	return 0;

error_free:
	amdgpu_job_free(job);

error:
	return r;
}

/**
 * amdgpu_vm_map_gart - Resolve gart mapping of addr
 *
 * @pages_addr: optional DMA address to use for lookup
 * @addr: the unmapped addr
 *
 * Look up the physical address of the page that the pte resolves
 * to and return the pointer for the page table entry.
 */
static uint64_t amdgpu_vm_map_gart(const dma_addr_t *pages_addr, uint64_t addr)
{
	uint64_t result;

	/* page table offset */
	result = pages_addr[addr >> PAGE_SHIFT];

	/* in case cpu page size != gpu page size*/
	result |= addr & (~PAGE_MASK);

	result &= 0xFFFFFFFFFFFFF000ULL;

	return result;
}

static int amdgpu_vm_update_pd_or_shadow(struct amdgpu_device *adev,
					 struct amdgpu_vm *vm,
					 bool shadow)
{
	struct amdgpu_ring *ring;
	struct amdgpu_bo *pd = shadow ? vm->page_directory->shadow :
		vm->page_directory;
	uint64_t pd_addr;
	uint32_t incr = AMDGPU_VM_PTE_COUNT * 8;
	uint64_t last_pde = ~0, last_pt = ~0;
	unsigned count = 0, pt_idx, ndw;
	struct amdgpu_job *job;
	struct amdgpu_pte_update_params params;
	struct fence *fence = NULL;

	int r;

	if (!pd)
		return 0;

	r = amdgpu_ttm_bind(&pd->tbo, &pd->tbo.mem);
	if (r)
		return r;

	pd_addr = amdgpu_bo_gpu_offset(pd);
	ring = container_of(vm->entity.sched, struct amdgpu_ring, sched);

	/* padding, etc. */
	ndw = 64;

	/* assume the worst case */
	ndw += vm->max_pde_used * 6;

	r = amdgpu_job_alloc_with_ib(adev, ndw * 4, &job);
	if (r)
		return r;

	memset(&params, 0, sizeof(params));
	params.adev = adev;
	params.ib = &job->ibs[0];

	/* walk over the address space and update the page directory */
	for (pt_idx = 0; pt_idx <= vm->max_pde_used; ++pt_idx) {
		struct amdgpu_bo *bo = vm->page_tables[pt_idx].entry.robj;
		uint64_t pde, pt;

		if (bo == NULL)
			continue;

		if (bo->shadow) {
			struct amdgpu_bo *shadow = bo->shadow;

			r = amdgpu_ttm_bind(&shadow->tbo, &shadow->tbo.mem);
			if (r)
				return r;
		}

		pt = amdgpu_bo_gpu_offset(bo);
		if (!shadow) {
			if (vm->page_tables[pt_idx].addr == pt)
				continue;
			vm->page_tables[pt_idx].addr = pt;
		} else {
			if (vm->page_tables[pt_idx].shadow_addr == pt)
				continue;
			vm->page_tables[pt_idx].shadow_addr = pt;
		}

		pde = pd_addr + pt_idx * 8;
		if (((last_pde + 8 * count) != pde) ||
		    ((last_pt + incr * count) != pt) ||
		    (count == AMDGPU_VM_MAX_UPDATE_SIZE)) {

			if (count) {
				amdgpu_vm_do_set_ptes(&params, last_pde,
						      last_pt, count, incr,
						      AMDGPU_PTE_VALID);
			}

			count = 1;
			last_pde = pde;
			last_pt = pt;
		} else {
			++count;
		}
	}

	if (count)
		amdgpu_vm_do_set_ptes(&params, last_pde, last_pt,
				      count, incr, AMDGPU_PTE_VALID);

	if (params.ib->length_dw != 0) {
		amdgpu_ring_pad_ib(ring, params.ib);
		amdgpu_sync_resv(adev, &job->sync, pd->tbo.resv,
				 AMDGPU_FENCE_OWNER_VM);
		WARN_ON(params.ib->length_dw > ndw);
		r = amdgpu_job_submit(job, ring, &vm->entity,
				      AMDGPU_FENCE_OWNER_VM, &fence);
		if (r)
			goto error_free;

		amdgpu_bo_fence(pd, fence, true);
		fence_put(vm->page_directory_fence);
		vm->page_directory_fence = fence_get(fence);
		fence_put(fence);

	} else {
		amdgpu_job_free(job);
	}

	return 0;

error_free:
	amdgpu_job_free(job);
	return r;
}

/*
 * amdgpu_vm_update_pdes - make sure that page directory is valid
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @start: start of GPU address range
 * @end: end of GPU address range
 *
 * Allocates new page tables if necessary
 * and updates the page directory.
 * Returns 0 for success, error for failure.
 */
int amdgpu_vm_update_page_directory(struct amdgpu_device *adev,
                                   struct amdgpu_vm *vm)
{
	int r;

	r = amdgpu_vm_update_pd_or_shadow(adev, vm, true);
	if (r)
		return r;
	return amdgpu_vm_update_pd_or_shadow(adev, vm, false);
}

/**
 * amdgpu_vm_update_ptes - make sure that page tables are valid
 *
 * @params: see amdgpu_pte_update_params definition
 * @vm: requested vm
 * @start: start of GPU address range
 * @end: end of GPU address range
 * @dst: destination address to map to, the next dst inside the function
 * @flags: mapping flags
 *
 * Update the page tables in the range @start - @end.
 */
static void amdgpu_vm_update_ptes(struct amdgpu_pte_update_params *params,
				  struct amdgpu_vm *vm,
				  uint64_t start, uint64_t end,
				  uint64_t dst, uint32_t flags)
{
	const uint64_t mask = AMDGPU_VM_PTE_COUNT - 1;

	uint64_t cur_pe_start, cur_nptes, cur_dst;
	uint64_t addr; /* next GPU address to be updated */
	uint64_t pt_idx;
	struct amdgpu_bo *pt;
	unsigned nptes; /* next number of ptes to be updated */
	uint64_t next_pe_start;

	/* initialize the variables */
	addr = start;
	pt_idx = addr >> amdgpu_vm_block_size;
	pt = vm->page_tables[pt_idx].entry.robj;
	if (params->shadow) {
		if (!pt->shadow)
			return;
		pt = vm->page_tables[pt_idx].entry.robj->shadow;
	}
	if ((addr & ~mask) == (end & ~mask))
		nptes = end - addr;
	else
		nptes = AMDGPU_VM_PTE_COUNT - (addr & mask);

	cur_pe_start = amdgpu_bo_gpu_offset(pt);
	cur_pe_start += (addr & mask) * 8;
	cur_nptes = nptes;
	cur_dst = dst;

	/* for next ptb*/
	addr += nptes;
	dst += nptes * AMDGPU_GPU_PAGE_SIZE;

	/* walk over the address space and update the page tables */
	while (addr < end) {
		pt_idx = addr >> amdgpu_vm_block_size;
		pt = vm->page_tables[pt_idx].entry.robj;
		if (params->shadow) {
			if (!pt->shadow)
				return;
			pt = vm->page_tables[pt_idx].entry.robj->shadow;
		}

		if ((addr & ~mask) == (end & ~mask))
			nptes = end - addr;
		else
			nptes = AMDGPU_VM_PTE_COUNT - (addr & mask);

		next_pe_start = amdgpu_bo_gpu_offset(pt);
		next_pe_start += (addr & mask) * 8;

		if ((cur_pe_start + 8 * cur_nptes) == next_pe_start &&
		    ((cur_nptes + nptes) <= AMDGPU_VM_MAX_UPDATE_SIZE)) {
			/* The next ptb is consecutive to current ptb.
			 * Don't call the update function now.
			 * Will update two ptbs together in future.
			*/
			cur_nptes += nptes;
		} else {
			params->func(params, cur_pe_start, cur_dst, cur_nptes,
				     AMDGPU_GPU_PAGE_SIZE, flags);

			cur_pe_start = next_pe_start;
			cur_nptes = nptes;
			cur_dst = dst;
		}

		/* for next ptb*/
		addr += nptes;
		dst += nptes * AMDGPU_GPU_PAGE_SIZE;
	}

	params->func(params, cur_pe_start, cur_dst, cur_nptes,
		     AMDGPU_GPU_PAGE_SIZE, flags);
}

/*
 * amdgpu_vm_frag_ptes - add fragment information to PTEs
 *
 * @params: see amdgpu_pte_update_params definition
 * @vm: requested vm
 * @start: first PTE to handle
 * @end: last PTE to handle
 * @dst: addr those PTEs should point to
 * @flags: hw mapping flags
 */
static void amdgpu_vm_frag_ptes(struct amdgpu_pte_update_params	*params,
				struct amdgpu_vm *vm,
				uint64_t start, uint64_t end,
				uint64_t dst, uint32_t flags)
{
	/**
	 * The MC L1 TLB supports variable sized pages, based on a fragment
	 * field in the PTE. When this field is set to a non-zero value, page
	 * granularity is increased from 4KB to (1 << (12 + frag)). The PTE
	 * flags are considered valid for all PTEs within the fragment range
	 * and corresponding mappings are assumed to be physically contiguous.
	 *
	 * The L1 TLB can store a single PTE for the whole fragment,
	 * significantly increasing the space available for translation
	 * caching. This leads to large improvements in throughput when the
	 * TLB is under pressure.
	 *
	 * The L2 TLB distributes small and large fragments into two
	 * asymmetric partitions. The large fragment cache is significantly
	 * larger. Thus, we try to use large fragments wherever possible.
	 * Userspace can support this by aligning virtual base address and
	 * allocation size to the fragment size.
	 */

	const uint64_t frag_align = 1 << AMDGPU_LOG2_PAGES_PER_FRAG;

	uint64_t frag_start = ALIGN(start, frag_align);
	uint64_t frag_end = end & ~(frag_align - 1);

	uint32_t frag;

	/* system pages are non continuously */
	if (params->src || !(flags & AMDGPU_PTE_VALID) ||
	    (frag_start >= frag_end)) {

		amdgpu_vm_update_ptes(params, vm, start, end, dst, flags);
		return;
	}

	/* use more than 64KB fragment size if possible */
	frag = lower_32_bits(frag_start | frag_end);
	frag = likely(frag) ? __ffs(frag) : 31;

	/* handle the 4K area at the beginning */
	if (start != frag_start) {
		amdgpu_vm_update_ptes(params, vm, start, frag_start,
				      dst, flags);
		dst += (frag_start - start) * AMDGPU_GPU_PAGE_SIZE;
	}

	/* handle the area in the middle */
	amdgpu_vm_update_ptes(params, vm, frag_start, frag_end, dst,
			      flags | AMDGPU_PTE_FRAG(frag));

	/* handle the 4K area at the end */
	if (frag_end != end) {
		dst += (frag_end - frag_start) * AMDGPU_GPU_PAGE_SIZE;
		amdgpu_vm_update_ptes(params, vm, frag_end, end, dst, flags);
	}
}

/**
 * amdgpu_vm_bo_update_mapping - update a mapping in the vm page table
 *
 * @adev: amdgpu_device pointer
 * @exclusive: fence we need to sync to
 * @src: address where to copy page table entries from
 * @pages_addr: DMA addresses to use for mapping
 * @vm: requested vm
 * @start: start of mapped range
 * @last: last mapped entry
 * @flags: flags for the entries
 * @addr: addr to set the area to
 * @fence: optional resulting fence
 *
 * Fill in the page table entries between @start and @last.
 * Returns 0 for success, -EINVAL for failure.
 */
static int amdgpu_vm_bo_update_mapping(struct amdgpu_device *adev,
				       struct fence *exclusive,
				       uint64_t src,
				       dma_addr_t *pages_addr,
				       struct amdgpu_vm *vm,
				       uint64_t start, uint64_t last,
				       uint32_t flags, uint64_t addr,
				       struct fence **fence)
{
	struct amdgpu_ring *ring;
	void *owner = AMDGPU_FENCE_OWNER_VM;
	unsigned nptes, ncmds, ndw;
	struct amdgpu_job *job;
	struct amdgpu_pte_update_params params;
	struct fence *f = NULL;
	int r;

	memset(&params, 0, sizeof(params));
	params.adev = adev;
	params.src = src;

	ring = container_of(vm->entity.sched, struct amdgpu_ring, sched);

	memset(&params, 0, sizeof(params));
	params.adev = adev;
	params.src = src;

	/* sync to everything on unmapping */
	if (!(flags & AMDGPU_PTE_VALID))
		owner = AMDGPU_FENCE_OWNER_UNDEFINED;

	nptes = last - start + 1;

	/*
	 * reserve space for one command every (1 << BLOCK_SIZE)
	 *  entries or 2k dwords (whatever is smaller)
	 */
	ncmds = (nptes >> min(amdgpu_vm_block_size, 11)) + 1;

	/* padding, etc. */
	ndw = 64;

	if (src) {
		/* only copy commands needed */
		ndw += ncmds * 7;

		params.func = amdgpu_vm_do_copy_ptes;

	} else if (pages_addr) {
		/* copy commands needed */
		ndw += ncmds * 7;

		/* and also PTEs */
		ndw += nptes * 2;

		params.func = amdgpu_vm_do_copy_ptes;

	} else {
		/* set page commands needed */
		ndw += ncmds * 10;

		/* two extra commands for begin/end of fragment */
		ndw += 2 * 10;

		params.func = amdgpu_vm_do_set_ptes;
	}

	r = amdgpu_job_alloc_with_ib(adev, ndw * 4, &job);
	if (r)
		return r;

	params.ib = &job->ibs[0];

	if (!src && pages_addr) {
		uint64_t *pte;
		unsigned i;

		/* Put the PTEs at the end of the IB. */
		i = ndw - nptes * 2;
		pte= (uint64_t *)&(job->ibs->ptr[i]);
		params.src = job->ibs->gpu_addr + i * 4;

		for (i = 0; i < nptes; ++i) {
			pte[i] = amdgpu_vm_map_gart(pages_addr, addr + i *
						    AMDGPU_GPU_PAGE_SIZE);
			pte[i] |= flags;
		}
		addr = 0;
	}

	r = amdgpu_sync_fence(adev, &job->sync, exclusive);
	if (r)
		goto error_free;

	r = amdgpu_sync_resv(adev, &job->sync, vm->page_directory->tbo.resv,
			     owner);
	if (r)
		goto error_free;

	r = reservation_object_reserve_shared(vm->page_directory->tbo.resv);
	if (r)
		goto error_free;

	params.shadow = true;
	amdgpu_vm_frag_ptes(&params, vm, start, last + 1, addr, flags);
	params.shadow = false;
	amdgpu_vm_frag_ptes(&params, vm, start, last + 1, addr, flags);

	amdgpu_ring_pad_ib(ring, params.ib);
	WARN_ON(params.ib->length_dw > ndw);
	r = amdgpu_job_submit(job, ring, &vm->entity,
			      AMDGPU_FENCE_OWNER_VM, &f);
	if (r)
		goto error_free;

	amdgpu_bo_fence(vm->page_directory, f, true);
	if (fence) {
		fence_put(*fence);
		*fence = fence_get(f);
	}
	fence_put(f);
	return 0;

error_free:
	amdgpu_job_free(job);
	return r;
}

/**
 * amdgpu_vm_bo_split_mapping - split a mapping into smaller chunks
 *
 * @adev: amdgpu_device pointer
 * @exclusive: fence we need to sync to
 * @gtt_flags: flags as they are used for GTT
 * @pages_addr: DMA addresses to use for mapping
 * @vm: requested vm
 * @mapping: mapped range and flags to use for the update
 * @addr: addr to set the area to
 * @flags: HW flags for the mapping
 * @fence: optional resulting fence
 *
 * Split the mapping into smaller chunks so that each update fits
 * into a SDMA IB.
 * Returns 0 for success, -EINVAL for failure.
 */
static int amdgpu_vm_bo_split_mapping(struct amdgpu_device *adev,
				      struct fence *exclusive,
				      uint32_t gtt_flags,
				      dma_addr_t *pages_addr,
				      struct amdgpu_vm *vm,
				      struct amdgpu_bo_va_mapping *mapping,
				      uint32_t flags, uint64_t addr,
				      struct fence **fence)
{
	const uint64_t max_size = 64ULL * 1024ULL * 1024ULL / AMDGPU_GPU_PAGE_SIZE;

	uint64_t src = 0, start = mapping->it.start;
	int r;

	/* normally,bo_va->flags only contians READABLE and WIRTEABLE bit go here
	 * but in case of something, we filter the flags in first place
	 */
	if (!(mapping->flags & AMDGPU_PTE_READABLE))
		flags &= ~AMDGPU_PTE_READABLE;
	if (!(mapping->flags & AMDGPU_PTE_WRITEABLE))
		flags &= ~AMDGPU_PTE_WRITEABLE;

	trace_amdgpu_vm_bo_update(mapping);

	if (pages_addr) {
		if (flags == gtt_flags)
			src = adev->gart.table_addr + (addr >> 12) * 8;
		addr = 0;
	}
	addr += mapping->offset;

	if (!pages_addr || src)
		return amdgpu_vm_bo_update_mapping(adev, exclusive,
						   src, pages_addr, vm,
						   start, mapping->it.last,
						   flags, addr, fence);

	while (start != mapping->it.last + 1) {
		uint64_t last;

		last = min((uint64_t)mapping->it.last, start + max_size - 1);
		r = amdgpu_vm_bo_update_mapping(adev, exclusive,
						src, pages_addr, vm,
						start, last, flags, addr,
						fence);
		if (r)
			return r;

		start = last + 1;
		addr += max_size * AMDGPU_GPU_PAGE_SIZE;
	}

	return 0;
}

/**
 * amdgpu_vm_bo_update - update all BO mappings in the vm page table
 *
 * @adev: amdgpu_device pointer
 * @bo_va: requested BO and VM object
 * @clear: if true clear the entries
 *
 * Fill in the page table entries for @bo_va.
 * Returns 0 for success, -EINVAL for failure.
 */
int amdgpu_vm_bo_update(struct amdgpu_device *adev,
			struct amdgpu_bo_va *bo_va,
			bool clear)
{
	struct amdgpu_vm *vm = bo_va->vm;
	struct amdgpu_bo_va_mapping *mapping;
	dma_addr_t *pages_addr = NULL;
	uint32_t gtt_flags, flags;
	struct ttm_mem_reg *mem;
	struct fence *exclusive;
	uint64_t addr;
	int r;

	if (clear) {
		mem = NULL;
		addr = 0;
		exclusive = NULL;
	} else {
		struct ttm_dma_tt *ttm;

		mem = &bo_va->bo->tbo.mem;
		addr = (u64)mem->start << PAGE_SHIFT;
		switch (mem->mem_type) {
		case TTM_PL_TT:
			ttm = container_of(bo_va->bo->tbo.ttm, struct
					   ttm_dma_tt, ttm);
			pages_addr = ttm->dma_address;
			break;

		case TTM_PL_VRAM:
			addr += adev->vm_manager.vram_base_offset;
			break;

		default:
			break;
		}

		exclusive = reservation_object_get_excl(bo_va->bo->tbo.resv);
	}

	flags = amdgpu_ttm_tt_pte_flags(adev, bo_va->bo->tbo.ttm, mem);
	gtt_flags = (amdgpu_ttm_is_bound(bo_va->bo->tbo.ttm) &&
		adev == bo_va->bo->adev) ? flags : 0;

	spin_lock(&vm->status_lock);
	if (!list_empty(&bo_va->vm_status))
		list_splice_init(&bo_va->valids, &bo_va->invalids);
	spin_unlock(&vm->status_lock);

	list_for_each_entry(mapping, &bo_va->invalids, list) {
		r = amdgpu_vm_bo_split_mapping(adev, exclusive,
					       gtt_flags, pages_addr, vm,
					       mapping, flags, addr,
					       &bo_va->last_pt_update);
		if (r)
			return r;
	}

	if (trace_amdgpu_vm_bo_mapping_enabled()) {
		list_for_each_entry(mapping, &bo_va->valids, list)
			trace_amdgpu_vm_bo_mapping(mapping);

		list_for_each_entry(mapping, &bo_va->invalids, list)
			trace_amdgpu_vm_bo_mapping(mapping);
	}

	spin_lock(&vm->status_lock);
	list_splice_init(&bo_va->invalids, &bo_va->valids);
	list_del_init(&bo_va->vm_status);
	if (clear)
		list_add(&bo_va->vm_status, &vm->cleared);
	spin_unlock(&vm->status_lock);

	return 0;
}

/**
 * amdgpu_vm_clear_freed - clear freed BOs in the PT
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 *
 * Make sure all freed BOs are cleared in the PT.
 * Returns 0 for success.
 *
 * PTs have to be reserved and mutex must be locked!
 */
int amdgpu_vm_clear_freed(struct amdgpu_device *adev,
			  struct amdgpu_vm *vm)
{
	struct amdgpu_bo_va_mapping *mapping;
	int r;

	while (!list_empty(&vm->freed)) {
		mapping = list_first_entry(&vm->freed,
			struct amdgpu_bo_va_mapping, list);
		list_del(&mapping->list);

		r = amdgpu_vm_bo_split_mapping(adev, NULL, 0, NULL, vm, mapping,
					       0, 0, NULL);
		kfree(mapping);
		if (r)
			return r;

	}
	return 0;

}

/**
 * amdgpu_vm_clear_invalids - clear invalidated BOs in the PT
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 *
 * Make sure all invalidated BOs are cleared in the PT.
 * Returns 0 for success.
 *
 * PTs have to be reserved and mutex must be locked!
 */
int amdgpu_vm_clear_invalids(struct amdgpu_device *adev,
			     struct amdgpu_vm *vm, struct amdgpu_sync *sync)
{
	struct amdgpu_bo_va *bo_va = NULL;
	int r = 0;

	spin_lock(&vm->status_lock);
	while (!list_empty(&vm->invalidated)) {
		bo_va = list_first_entry(&vm->invalidated,
			struct amdgpu_bo_va, vm_status);
		spin_unlock(&vm->status_lock);

		r = amdgpu_vm_bo_update(adev, bo_va, true);
		if (r)
			return r;

		spin_lock(&vm->status_lock);
	}
	spin_unlock(&vm->status_lock);

	if (bo_va)
		r = amdgpu_sync_fence(adev, sync, bo_va->last_pt_update);

	return r;
}

/**
 * amdgpu_vm_bo_add - add a bo to a specific vm
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @bo: amdgpu buffer object
 *
 * Add @bo into the requested vm.
 * Add @bo to the list of bos associated with the vm
 * Returns newly added bo_va or NULL for failure
 *
 * Object has to be reserved!
 */
struct amdgpu_bo_va *amdgpu_vm_bo_add(struct amdgpu_device *adev,
				      struct amdgpu_vm *vm,
				      struct amdgpu_bo *bo)
{
	struct amdgpu_bo_va *bo_va;

	bo_va = kzalloc(sizeof(struct amdgpu_bo_va), GFP_KERNEL);
	if (bo_va == NULL) {
		return NULL;
	}
	bo_va->vm = vm;
	bo_va->bo = bo;
	bo_va->ref_count = 1;
	INIT_LIST_HEAD(&bo_va->bo_list);
	INIT_LIST_HEAD(&bo_va->valids);
	INIT_LIST_HEAD(&bo_va->invalids);
	INIT_LIST_HEAD(&bo_va->vm_status);

	list_add_tail(&bo_va->bo_list, &bo->va);

	return bo_va;
}

/**
 * amdgpu_vm_bo_map - map bo inside a vm
 *
 * @adev: amdgpu_device pointer
 * @bo_va: bo_va to store the address
 * @saddr: where to map the BO
 * @offset: requested offset in the BO
 * @flags: attributes of pages (read/write/valid/etc.)
 *
 * Add a mapping of the BO at the specefied addr into the VM.
 * Returns 0 for success, error for failure.
 *
 * Object has to be reserved and unreserved outside!
 */
int amdgpu_vm_bo_map(struct amdgpu_device *adev,
		     struct amdgpu_bo_va *bo_va,
		     uint64_t saddr, uint64_t offset,
		     uint64_t size, uint32_t flags)
{
	struct amdgpu_bo_va_mapping *mapping;
	struct amdgpu_vm *vm = bo_va->vm;
	struct interval_tree_node *it;
	unsigned last_pfn, pt_idx;
	uint64_t eaddr;
	int r;

	/* validate the parameters */
	if (saddr & AMDGPU_GPU_PAGE_MASK || offset & AMDGPU_GPU_PAGE_MASK ||
	    size == 0 || size & AMDGPU_GPU_PAGE_MASK)
		return -EINVAL;

	/* make sure object fit at this offset */
	eaddr = saddr + size - 1;
	if ((saddr >= eaddr) || (offset + size > amdgpu_bo_size(bo_va->bo)))
		return -EINVAL;

	last_pfn = eaddr / AMDGPU_GPU_PAGE_SIZE;
	if (last_pfn >= adev->vm_manager.max_pfn) {
		dev_err(adev->dev, "va above limit (0x%08X >= 0x%08X)\n",
			last_pfn, adev->vm_manager.max_pfn);
		return -EINVAL;
	}

	saddr /= AMDGPU_GPU_PAGE_SIZE;
	eaddr /= AMDGPU_GPU_PAGE_SIZE;

	it = interval_tree_iter_first(&vm->va, saddr, eaddr);
	if (it) {
		struct amdgpu_bo_va_mapping *tmp;
		tmp = container_of(it, struct amdgpu_bo_va_mapping, it);
		/* bo and tmp overlap, invalid addr */
		dev_err(adev->dev, "bo %p va 0x%010Lx-0x%010Lx conflict with "
			"0x%010lx-0x%010lx\n", bo_va->bo, saddr, eaddr,
			tmp->it.start, tmp->it.last + 1);
		r = -EINVAL;
		goto error;
	}

	mapping = kmalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		r = -ENOMEM;
		goto error;
	}

	INIT_LIST_HEAD(&mapping->list);
	mapping->it.start = saddr;
	mapping->it.last = eaddr;
	mapping->offset = offset;
	mapping->flags = flags;

	list_add(&mapping->list, &bo_va->invalids);
	interval_tree_insert(&mapping->it, &vm->va);

	/* Make sure the page tables are allocated */
	saddr >>= amdgpu_vm_block_size;
	eaddr >>= amdgpu_vm_block_size;

	BUG_ON(eaddr >= amdgpu_vm_num_pdes(adev));

	if (eaddr > vm->max_pde_used)
		vm->max_pde_used = eaddr;

	/* walk over the address space and allocate the page tables */
	for (pt_idx = saddr; pt_idx <= eaddr; ++pt_idx) {
		struct reservation_object *resv = vm->page_directory->tbo.resv;
		struct amdgpu_bo_list_entry *entry;
		struct amdgpu_bo *pt;

		entry = &vm->page_tables[pt_idx].entry;
		if (entry->robj)
			continue;

		r = amdgpu_bo_create(adev, AMDGPU_VM_PTE_COUNT * 8,
				     AMDGPU_GPU_PAGE_SIZE, true,
				     AMDGPU_GEM_DOMAIN_VRAM,
				     AMDGPU_GEM_CREATE_NO_CPU_ACCESS |
				     AMDGPU_GEM_CREATE_SHADOW,
				     NULL, resv, &pt);
		if (r)
			goto error_free;

		/* Keep a reference to the page table to avoid freeing
		 * them up in the wrong order.
		 */
		pt->parent = amdgpu_bo_ref(vm->page_directory);

		r = amdgpu_vm_clear_bo(adev, vm, pt);
		if (r) {
			amdgpu_bo_unref(&pt->shadow);
			amdgpu_bo_unref(&pt);
			goto error_free;
		}

		entry->robj = pt;
		entry->priority = 0;
		entry->tv.bo = &entry->robj->tbo;
		entry->tv.shared = true;
		entry->user_pages = NULL;
		vm->page_tables[pt_idx].addr = 0;
	}

	return 0;

error_free:
	list_del(&mapping->list);
	interval_tree_remove(&mapping->it, &vm->va);
	trace_amdgpu_vm_bo_unmap(bo_va, mapping);
	kfree(mapping);

error:
	return r;
}

/**
 * amdgpu_vm_bo_unmap - remove bo mapping from vm
 *
 * @adev: amdgpu_device pointer
 * @bo_va: bo_va to remove the address from
 * @saddr: where to the BO is mapped
 *
 * Remove a mapping of the BO at the specefied addr from the VM.
 * Returns 0 for success, error for failure.
 *
 * Object has to be reserved and unreserved outside!
 */
int amdgpu_vm_bo_unmap(struct amdgpu_device *adev,
		       struct amdgpu_bo_va *bo_va,
		       uint64_t saddr)
{
	struct amdgpu_bo_va_mapping *mapping;
	struct amdgpu_vm *vm = bo_va->vm;
	bool valid = true;

	saddr /= AMDGPU_GPU_PAGE_SIZE;

	list_for_each_entry(mapping, &bo_va->valids, list) {
		if (mapping->it.start == saddr)
			break;
	}

	if (&mapping->list == &bo_va->valids) {
		valid = false;

		list_for_each_entry(mapping, &bo_va->invalids, list) {
			if (mapping->it.start == saddr)
				break;
		}

		if (&mapping->list == &bo_va->invalids)
			return -ENOENT;
	}

	list_del(&mapping->list);
	interval_tree_remove(&mapping->it, &vm->va);
	trace_amdgpu_vm_bo_unmap(bo_va, mapping);

	if (valid)
		list_add(&mapping->list, &vm->freed);
	else
		kfree(mapping);

	return 0;
}

/**
 * amdgpu_vm_bo_rmv - remove a bo to a specific vm
 *
 * @adev: amdgpu_device pointer
 * @bo_va: requested bo_va
 *
 * Remove @bo_va->bo from the requested vm.
 *
 * Object have to be reserved!
 */
void amdgpu_vm_bo_rmv(struct amdgpu_device *adev,
		      struct amdgpu_bo_va *bo_va)
{
	struct amdgpu_bo_va_mapping *mapping, *next;
	struct amdgpu_vm *vm = bo_va->vm;

	list_del(&bo_va->bo_list);

	spin_lock(&vm->status_lock);
	list_del(&bo_va->vm_status);
	spin_unlock(&vm->status_lock);

	list_for_each_entry_safe(mapping, next, &bo_va->valids, list) {
		list_del(&mapping->list);
		interval_tree_remove(&mapping->it, &vm->va);
		trace_amdgpu_vm_bo_unmap(bo_va, mapping);
		list_add(&mapping->list, &vm->freed);
	}
	list_for_each_entry_safe(mapping, next, &bo_va->invalids, list) {
		list_del(&mapping->list);
		interval_tree_remove(&mapping->it, &vm->va);
		kfree(mapping);
	}

	fence_put(bo_va->last_pt_update);
	kfree(bo_va);
}

/**
 * amdgpu_vm_bo_invalidate - mark the bo as invalid
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @bo: amdgpu buffer object
 *
 * Mark @bo as invalid.
 */
void amdgpu_vm_bo_invalidate(struct amdgpu_device *adev,
			     struct amdgpu_bo *bo)
{
	struct amdgpu_bo_va *bo_va;

	list_for_each_entry(bo_va, &bo->va, bo_list) {
		spin_lock(&bo_va->vm->status_lock);
		if (list_empty(&bo_va->vm_status))
			list_add(&bo_va->vm_status, &bo_va->vm->invalidated);
		spin_unlock(&bo_va->vm->status_lock);
	}
}

/**
 * amdgpu_vm_init - initialize a vm instance
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 *
 * Init @vm fields.
 */
int amdgpu_vm_init(struct amdgpu_device *adev, struct amdgpu_vm *vm)
{
	const unsigned align = min(AMDGPU_VM_PTB_ALIGN_SIZE,
		AMDGPU_VM_PTE_COUNT * 8);
	unsigned pd_size, pd_entries;
	unsigned ring_instance;
	struct amdgpu_ring *ring;
	struct amd_sched_rq *rq;
	int i, r;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		vm->ids[i] = NULL;
	vm->va = RB_ROOT;
	vm->client_id = atomic64_inc_return(&adev->vm_manager.client_counter);
	spin_lock_init(&vm->status_lock);
	INIT_LIST_HEAD(&vm->invalidated);
	INIT_LIST_HEAD(&vm->cleared);
	INIT_LIST_HEAD(&vm->freed);

	pd_size = amdgpu_vm_directory_size(adev);
	pd_entries = amdgpu_vm_num_pdes(adev);

	/* allocate page table array */
	vm->page_tables = drm_calloc_large(pd_entries, sizeof(struct amdgpu_vm_pt));
	if (vm->page_tables == NULL) {
		DRM_ERROR("Cannot allocate memory for page table array\n");
		return -ENOMEM;
	}

	/* create scheduler entity for page table updates */

	ring_instance = atomic_inc_return(&adev->vm_manager.vm_pte_next_ring);
	ring_instance %= adev->vm_manager.vm_pte_num_rings;
	ring = adev->vm_manager.vm_pte_rings[ring_instance];
	rq = &ring->sched.sched_rq[AMD_SCHED_PRIORITY_KERNEL];
	r = amd_sched_entity_init(&ring->sched, &vm->entity,
				  rq, amdgpu_sched_jobs);
	if (r)
		return r;

	vm->page_directory_fence = NULL;

	r = amdgpu_bo_create(adev, pd_size, align, true,
			     AMDGPU_GEM_DOMAIN_VRAM,
			     AMDGPU_GEM_CREATE_NO_CPU_ACCESS |
			     AMDGPU_GEM_CREATE_SHADOW,
			     NULL, NULL, &vm->page_directory);
	if (r)
		goto error_free_sched_entity;

	r = amdgpu_bo_reserve(vm->page_directory, false);
	if (r)
		goto error_free_page_directory;

	r = amdgpu_vm_clear_bo(adev, vm, vm->page_directory);
	amdgpu_bo_unreserve(vm->page_directory);
	if (r)
		goto error_free_page_directory;
	vm->last_eviction_counter = atomic64_read(&adev->num_evictions);

	return 0;

error_free_page_directory:
	amdgpu_bo_unref(&vm->page_directory->shadow);
	amdgpu_bo_unref(&vm->page_directory);
	vm->page_directory = NULL;

error_free_sched_entity:
	amd_sched_entity_fini(&ring->sched, &vm->entity);

	return r;
}

/**
 * amdgpu_vm_fini - tear down a vm instance
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 *
 * Tear down @vm.
 * Unbind the VM and remove all bos from the vm bo list
 */
void amdgpu_vm_fini(struct amdgpu_device *adev, struct amdgpu_vm *vm)
{
	struct amdgpu_bo_va_mapping *mapping, *tmp;
	int i;

	amd_sched_entity_fini(vm->entity.sched, &vm->entity);

	if (!RB_EMPTY_ROOT(&vm->va)) {
		dev_err(adev->dev, "still active bo inside vm\n");
	}
	rbtree_postorder_for_each_entry_safe(mapping, tmp, &vm->va, it.rb) {
		list_del(&mapping->list);
		interval_tree_remove(&mapping->it, &vm->va);
		kfree(mapping);
	}
	list_for_each_entry_safe(mapping, tmp, &vm->freed, list) {
		list_del(&mapping->list);
		kfree(mapping);
	}

	for (i = 0; i < amdgpu_vm_num_pdes(adev); i++) {
		struct amdgpu_bo *pt = vm->page_tables[i].entry.robj;

		if (!pt)
			continue;

		amdgpu_bo_unref(&pt->shadow);
		amdgpu_bo_unref(&pt);
	}
	drm_free_large(vm->page_tables);

	amdgpu_bo_unref(&vm->page_directory->shadow);
	amdgpu_bo_unref(&vm->page_directory);
	fence_put(vm->page_directory_fence);
}

/**
 * amdgpu_vm_manager_init - init the VM manager
 *
 * @adev: amdgpu_device pointer
 *
 * Initialize the VM manager structures
 */
void amdgpu_vm_manager_init(struct amdgpu_device *adev)
{
	unsigned i;

	INIT_LIST_HEAD(&adev->vm_manager.ids_lru);

	/* skip over VMID 0, since it is the system VM */
	for (i = 1; i < adev->vm_manager.num_ids; ++i) {
		amdgpu_vm_reset_id(adev, i);
		amdgpu_sync_create(&adev->vm_manager.ids[i].active);
		list_add_tail(&adev->vm_manager.ids[i].list,
			      &adev->vm_manager.ids_lru);
	}

	adev->vm_manager.fence_context = fence_context_alloc(AMDGPU_MAX_RINGS);
	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		adev->vm_manager.seqno[i] = 0;

	atomic_set(&adev->vm_manager.vm_pte_next_ring, 0);
	atomic64_set(&adev->vm_manager.client_counter, 0);
}

/**
 * amdgpu_vm_manager_fini - cleanup VM manager
 *
 * @adev: amdgpu_device pointer
 *
 * Cleanup the VM manager and free resources.
 */
void amdgpu_vm_manager_fini(struct amdgpu_device *adev)
{
	unsigned i;

	for (i = 0; i < AMDGPU_NUM_VM; ++i) {
		struct amdgpu_vm_id *id = &adev->vm_manager.ids[i];

		fence_put(adev->vm_manager.ids[i].first);
		amdgpu_sync_free(&adev->vm_manager.ids[i].active);
		fence_put(id->flushed_updates);
	}
}

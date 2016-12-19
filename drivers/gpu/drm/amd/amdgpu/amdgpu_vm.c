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

/**
 * amdgpu_vm_num_pde - return the number of page directory entries
 *
 * @adev: amdgpu_device pointer
 *
 * Calculate the number of page directory entries (cayman+).
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
 * Calculate the size of the page directory in bytes (cayman+).
 */
static unsigned amdgpu_vm_directory_size(struct amdgpu_device *adev)
{
	return AMDGPU_GPU_PAGE_ALIGN(amdgpu_vm_num_pdes(adev) * 8);
}

/**
 * amdgpu_vm_get_bos - add the vm BOs to a validation list
 *
 * @vm: vm providing the BOs
 * @head: head of validation list
 *
 * Add the page directory to the list of BOs to
 * validate for command submission (cayman+).
 */
struct amdgpu_bo_list_entry *amdgpu_vm_get_bos(struct amdgpu_device *adev,
					  struct amdgpu_vm *vm,
					  struct list_head *head)
{
	struct amdgpu_bo_list_entry *list;
	unsigned i, idx;

	list = drm_malloc_ab(vm->max_pde_used + 2,
			     sizeof(struct amdgpu_bo_list_entry));
	if (!list) {
		return NULL;
	}

	/* add the vm page table to the list */
	list[0].robj = vm->page_directory;
	list[0].prefered_domains = AMDGPU_GEM_DOMAIN_VRAM;
	list[0].allowed_domains = AMDGPU_GEM_DOMAIN_VRAM;
	list[0].priority = 0;
	list[0].tv.bo = &vm->page_directory->tbo;
	list[0].tv.shared = true;
	list_add(&list[0].tv.head, head);

	for (i = 0, idx = 1; i <= vm->max_pde_used; i++) {
		if (!vm->page_tables[i].bo)
			continue;

		list[idx].robj = vm->page_tables[i].bo;
		list[idx].prefered_domains = AMDGPU_GEM_DOMAIN_VRAM;
		list[idx].allowed_domains = AMDGPU_GEM_DOMAIN_VRAM;
		list[idx].priority = 0;
		list[idx].tv.bo = &list[idx].robj->tbo;
		list[idx].tv.shared = true;
		list_add(&list[idx++].tv.head, head);
	}

	return list;
}

/**
 * amdgpu_vm_grab_id - allocate the next free VMID
 *
 * @vm: vm to allocate id for
 * @ring: ring we want to submit job to
 * @sync: sync object where we add dependencies
 *
 * Allocate an id for the vm, adding fences to the sync obj as necessary.
 *
 * Global mutex must be locked!
 */
int amdgpu_vm_grab_id(struct amdgpu_vm *vm, struct amdgpu_ring *ring,
		      struct amdgpu_sync *sync)
{
	struct fence *best[AMDGPU_MAX_RINGS] = {};
	struct amdgpu_vm_id *vm_id = &vm->ids[ring->idx];
	struct amdgpu_device *adev = ring->adev;

	unsigned choices[2] = {};
	unsigned i;

	/* check if the id is still valid */
	if (vm_id->id) {
		unsigned id = vm_id->id;
		long owner;

		owner = atomic_long_read(&adev->vm_manager.ids[id].owner);
		if (owner == (long)vm) {
			trace_amdgpu_vm_grab_id(vm_id->id, ring->idx);
			return 0;
		}
	}

	/* we definately need to flush */
	vm_id->pd_gpu_addr = ~0ll;

	/* skip over VMID 0, since it is the system VM */
	for (i = 1; i < adev->vm_manager.nvm; ++i) {
		struct fence *fence = adev->vm_manager.ids[i].active;
		struct amdgpu_ring *fring;

		if (fence == NULL) {
			/* found a free one */
			vm_id->id = i;
			trace_amdgpu_vm_grab_id(i, ring->idx);
			return 0;
		}

		fring = amdgpu_ring_from_fence(fence);
		if (best[fring->idx] == NULL ||
		    fence_is_later(best[fring->idx], fence)) {
			best[fring->idx] = fence;
			choices[fring == ring ? 0 : 1] = i;
		}
	}

	for (i = 0; i < 2; ++i) {
		if (choices[i]) {
			struct fence *fence;

			fence  = adev->vm_manager.ids[choices[i]].active;
			vm_id->id = choices[i];

			trace_amdgpu_vm_grab_id(choices[i], ring->idx);
			return amdgpu_sync_fence(ring->adev, sync, fence);
		}
	}

	/* should never happen */
	BUG();
	return -EINVAL;
}

/**
 * amdgpu_vm_flush - hardware flush the vm
 *
 * @ring: ring to use for flush
 * @vm: vm we want to flush
 * @updates: last vm update that we waited for
 *
 * Flush the vm (cayman+).
 *
 * Global and local mutex must be locked!
 */
void amdgpu_vm_flush(struct amdgpu_ring *ring,
		     struct amdgpu_vm *vm,
		     struct fence *updates)
{
	uint64_t pd_addr = amdgpu_bo_gpu_offset(vm->page_directory);
	struct amdgpu_vm_id *vm_id = &vm->ids[ring->idx];
	struct fence *flushed_updates = vm_id->flushed_updates;
	bool is_later;

	if (!flushed_updates)
		is_later = true;
	else if (!updates)
		is_later = false;
	else
		is_later = fence_is_later(updates, flushed_updates);

	if (pd_addr != vm_id->pd_gpu_addr || is_later) {
		trace_amdgpu_vm_flush(pd_addr, ring->idx, vm_id->id);
		if (is_later) {
			vm_id->flushed_updates = fence_get(updates);
			fence_put(flushed_updates);
		}
		vm_id->pd_gpu_addr = pd_addr;
		amdgpu_ring_emit_vm_flush(ring, vm_id->id, vm_id->pd_gpu_addr);
	}
}

/**
 * amdgpu_vm_fence - remember fence for vm
 *
 * @adev: amdgpu_device pointer
 * @vm: vm we want to fence
 * @fence: fence to remember
 *
 * Fence the vm (cayman+).
 * Set the fence used to protect page table and id.
 *
 * Global and local mutex must be locked!
 */
void amdgpu_vm_fence(struct amdgpu_device *adev,
		     struct amdgpu_vm *vm,
		     struct fence *fence)
{
	struct amdgpu_ring *ring = amdgpu_ring_from_fence(fence);
	unsigned vm_id = vm->ids[ring->idx].id;

	fence_put(adev->vm_manager.ids[vm_id].active);
	adev->vm_manager.ids[vm_id].active = fence_get(fence);
	atomic_long_set(&adev->vm_manager.ids[vm_id].owner, (long)vm);
}

/**
 * amdgpu_vm_bo_find - find the bo_va for a specific vm & bo
 *
 * @vm: requested vm
 * @bo: requested buffer object
 *
 * Find @bo inside the requested vm (cayman+).
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
 * amdgpu_vm_update_pages - helper to call the right asic function
 *
 * @adev: amdgpu_device pointer
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: hw access flags
 * @gtt_flags: GTT hw access flags
 *
 * Traces the parameters and calls the right asic functions
 * to setup the page table using the DMA.
 */
static void amdgpu_vm_update_pages(struct amdgpu_device *adev,
				   struct amdgpu_ib *ib,
				   uint64_t pe, uint64_t addr,
				   unsigned count, uint32_t incr,
				   uint32_t flags, uint32_t gtt_flags)
{
	trace_amdgpu_vm_set_page(pe, addr, count, incr, flags);

	if ((flags & AMDGPU_PTE_SYSTEM) && (flags == gtt_flags)) {
		uint64_t src = adev->gart.table_addr + (addr >> 12) * 8;
		amdgpu_vm_copy_pte(adev, ib, pe, src, count);

	} else if ((flags & AMDGPU_PTE_SYSTEM) || (count < 3)) {
		amdgpu_vm_write_pte(adev, ib, pe, addr,
				      count, incr, flags);

	} else {
		amdgpu_vm_set_pte_pde(adev, ib, pe, addr,
				      count, incr, flags);
	}
}

int amdgpu_vm_free_job(struct amdgpu_job *job)
{
	int i;
	for (i = 0; i < job->num_ibs; i++)
		amdgpu_ib_free(job->adev, &job->ibs[i]);
	kfree(job->ibs);
	return 0;
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
			      struct amdgpu_bo *bo)
{
	struct amdgpu_ring *ring = adev->vm_manager.vm_pte_funcs_ring;
	struct fence *fence = NULL;
	struct amdgpu_ib *ib;
	unsigned entries;
	uint64_t addr;
	int r;

	r = reservation_object_reserve_shared(bo->tbo.resv);
	if (r)
		return r;

	r = ttm_bo_validate(&bo->tbo, &bo->placement, true, false);
	if (r)
		goto error;

	addr = amdgpu_bo_gpu_offset(bo);
	entries = amdgpu_bo_size(bo) / 8;

	ib = kzalloc(sizeof(struct amdgpu_ib), GFP_KERNEL);
	if (!ib)
		goto error;

	r = amdgpu_ib_get(ring, NULL, entries * 2 + 64, ib);
	if (r)
		goto error_free;

	ib->length_dw = 0;

	amdgpu_vm_update_pages(adev, ib, addr, 0, entries, 0, 0, 0);
	amdgpu_vm_pad_ib(adev, ib);
	WARN_ON(ib->length_dw > 64);
	r = amdgpu_sched_ib_submit_kernel_helper(adev, ring, ib, 1,
						 &amdgpu_vm_free_job,
						 AMDGPU_FENCE_OWNER_VM,
						 &fence);
	if (!r)
		amdgpu_bo_fence(bo, fence, true);
	fence_put(fence);
	if (amdgpu_enable_scheduler)
		return 0;

error_free:
	amdgpu_ib_free(adev, ib);
	kfree(ib);

error:
	return r;
}

/**
 * amdgpu_vm_map_gart - get the physical address of a gart page
 *
 * @adev: amdgpu_device pointer
 * @addr: the unmapped addr
 *
 * Look up the physical address of the page that the pte resolves
 * to (cayman+).
 * Returns the physical address of the page.
 */
uint64_t amdgpu_vm_map_gart(struct amdgpu_device *adev, uint64_t addr)
{
	uint64_t result;

	/* page table offset */
	result = adev->gart.pages_addr[addr >> PAGE_SHIFT];

	/* in case cpu page size != gpu page size*/
	result |= addr & (~PAGE_MASK);

	return result;
}

/**
 * amdgpu_vm_update_pdes - make sure that page directory is valid
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @start: start of GPU address range
 * @end: end of GPU address range
 *
 * Allocates new page tables if necessary
 * and updates the page directory (cayman+).
 * Returns 0 for success, error for failure.
 *
 * Global and local mutex must be locked!
 */
int amdgpu_vm_update_page_directory(struct amdgpu_device *adev,
				    struct amdgpu_vm *vm)
{
	struct amdgpu_ring *ring = adev->vm_manager.vm_pte_funcs_ring;
	struct amdgpu_bo *pd = vm->page_directory;
	uint64_t pd_addr = amdgpu_bo_gpu_offset(pd);
	uint32_t incr = AMDGPU_VM_PTE_COUNT * 8;
	uint64_t last_pde = ~0, last_pt = ~0;
	unsigned count = 0, pt_idx, ndw;
	struct amdgpu_ib *ib;
	struct fence *fence = NULL;

	int r;

	/* padding, etc. */
	ndw = 64;

	/* assume the worst case */
	ndw += vm->max_pde_used * 6;

	/* update too big for an IB */
	if (ndw > 0xfffff)
		return -ENOMEM;

	ib = kzalloc(sizeof(struct amdgpu_ib), GFP_KERNEL);
	if (!ib)
		return -ENOMEM;

	r = amdgpu_ib_get(ring, NULL, ndw * 4, ib);
	if (r) {
		kfree(ib);
		return r;
	}
	ib->length_dw = 0;

	/* walk over the address space and update the page directory */
	for (pt_idx = 0; pt_idx <= vm->max_pde_used; ++pt_idx) {
		struct amdgpu_bo *bo = vm->page_tables[pt_idx].bo;
		uint64_t pde, pt;

		if (bo == NULL)
			continue;

		pt = amdgpu_bo_gpu_offset(bo);
		if (vm->page_tables[pt_idx].addr == pt)
			continue;
		vm->page_tables[pt_idx].addr = pt;

		pde = pd_addr + pt_idx * 8;
		if (((last_pde + 8 * count) != pde) ||
		    ((last_pt + incr * count) != pt)) {

			if (count) {
				amdgpu_vm_update_pages(adev, ib, last_pde,
						       last_pt, count, incr,
						       AMDGPU_PTE_VALID, 0);
			}

			count = 1;
			last_pde = pde;
			last_pt = pt;
		} else {
			++count;
		}
	}

	if (count)
		amdgpu_vm_update_pages(adev, ib, last_pde, last_pt, count,
				       incr, AMDGPU_PTE_VALID, 0);

	if (ib->length_dw != 0) {
		amdgpu_vm_pad_ib(adev, ib);
		amdgpu_sync_resv(adev, &ib->sync, pd->tbo.resv, AMDGPU_FENCE_OWNER_VM);
		WARN_ON(ib->length_dw > ndw);
		r = amdgpu_sched_ib_submit_kernel_helper(adev, ring, ib, 1,
							 &amdgpu_vm_free_job,
							 AMDGPU_FENCE_OWNER_VM,
							 &fence);
		if (r)
			goto error_free;

		amdgpu_bo_fence(pd, fence, true);
		fence_put(vm->page_directory_fence);
		vm->page_directory_fence = fence_get(fence);
		fence_put(fence);
	}

	if (!amdgpu_enable_scheduler || ib->length_dw == 0) {
		amdgpu_ib_free(adev, ib);
		kfree(ib);
	}

	return 0;

error_free:
	amdgpu_ib_free(adev, ib);
	kfree(ib);
	return r;
}

/**
 * amdgpu_vm_frag_ptes - add fragment information to PTEs
 *
 * @adev: amdgpu_device pointer
 * @ib: IB for the update
 * @pe_start: first PTE to handle
 * @pe_end: last PTE to handle
 * @addr: addr those PTEs should point to
 * @flags: hw mapping flags
 * @gtt_flags: GTT hw mapping flags
 *
 * Global and local mutex must be locked!
 */
static void amdgpu_vm_frag_ptes(struct amdgpu_device *adev,
				struct amdgpu_ib *ib,
				uint64_t pe_start, uint64_t pe_end,
				uint64_t addr, uint32_t flags,
				uint32_t gtt_flags)
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

	/* SI and newer are optimized for 64KB */
	uint64_t frag_flags = AMDGPU_PTE_FRAG_64KB;
	uint64_t frag_align = 0x80;

	uint64_t frag_start = ALIGN(pe_start, frag_align);
	uint64_t frag_end = pe_end & ~(frag_align - 1);

	unsigned count;

	/* system pages are non continuously */
	if ((flags & AMDGPU_PTE_SYSTEM) || !(flags & AMDGPU_PTE_VALID) ||
	    (frag_start >= frag_end)) {

		count = (pe_end - pe_start) / 8;
		amdgpu_vm_update_pages(adev, ib, pe_start, addr, count,
				       AMDGPU_GPU_PAGE_SIZE, flags, gtt_flags);
		return;
	}

	/* handle the 4K area at the beginning */
	if (pe_start != frag_start) {
		count = (frag_start - pe_start) / 8;
		amdgpu_vm_update_pages(adev, ib, pe_start, addr, count,
				       AMDGPU_GPU_PAGE_SIZE, flags, gtt_flags);
		addr += AMDGPU_GPU_PAGE_SIZE * count;
	}

	/* handle the area in the middle */
	count = (frag_end - frag_start) / 8;
	amdgpu_vm_update_pages(adev, ib, frag_start, addr, count,
			       AMDGPU_GPU_PAGE_SIZE, flags | frag_flags,
			       gtt_flags);

	/* handle the 4K area at the end */
	if (frag_end != pe_end) {
		addr += AMDGPU_GPU_PAGE_SIZE * count;
		count = (pe_end - frag_end) / 8;
		amdgpu_vm_update_pages(adev, ib, frag_end, addr, count,
				       AMDGPU_GPU_PAGE_SIZE, flags, gtt_flags);
	}
}

/**
 * amdgpu_vm_update_ptes - make sure that page tables are valid
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @start: start of GPU address range
 * @end: end of GPU address range
 * @dst: destination address to map to
 * @flags: mapping flags
 *
 * Update the page tables in the range @start - @end (cayman+).
 *
 * Global and local mutex must be locked!
 */
static int amdgpu_vm_update_ptes(struct amdgpu_device *adev,
				 struct amdgpu_vm *vm,
				 struct amdgpu_ib *ib,
				 uint64_t start, uint64_t end,
				 uint64_t dst, uint32_t flags,
				 uint32_t gtt_flags)
{
	uint64_t mask = AMDGPU_VM_PTE_COUNT - 1;
	uint64_t last_pte = ~0, last_dst = ~0;
	void *owner = AMDGPU_FENCE_OWNER_VM;
	unsigned count = 0;
	uint64_t addr;

	/* sync to everything on unmapping */
	if (!(flags & AMDGPU_PTE_VALID))
		owner = AMDGPU_FENCE_OWNER_UNDEFINED;

	/* walk over the address space and update the page tables */
	for (addr = start; addr < end; ) {
		uint64_t pt_idx = addr >> amdgpu_vm_block_size;
		struct amdgpu_bo *pt = vm->page_tables[pt_idx].bo;
		unsigned nptes;
		uint64_t pte;
		int r;

		amdgpu_sync_resv(adev, &ib->sync, pt->tbo.resv, owner);
		r = reservation_object_reserve_shared(pt->tbo.resv);
		if (r)
			return r;

		if ((addr & ~mask) == (end & ~mask))
			nptes = end - addr;
		else
			nptes = AMDGPU_VM_PTE_COUNT - (addr & mask);

		pte = amdgpu_bo_gpu_offset(pt);
		pte += (addr & mask) * 8;

		if ((last_pte + 8 * count) != pte) {

			if (count) {
				amdgpu_vm_frag_ptes(adev, ib, last_pte,
						    last_pte + 8 * count,
						    last_dst, flags,
						    gtt_flags);
			}

			count = nptes;
			last_pte = pte;
			last_dst = dst;
		} else {
			count += nptes;
		}

		addr += nptes;
		dst += nptes * AMDGPU_GPU_PAGE_SIZE;
	}

	if (count) {
		amdgpu_vm_frag_ptes(adev, ib, last_pte,
				    last_pte + 8 * count,
				    last_dst, flags, gtt_flags);
	}

	return 0;
}

/**
 * amdgpu_vm_bo_update_mapping - update a mapping in the vm page table
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @mapping: mapped range and flags to use for the update
 * @addr: addr to set the area to
 * @gtt_flags: flags as they are used for GTT
 * @fence: optional resulting fence
 *
 * Fill in the page table entries for @mapping.
 * Returns 0 for success, -EINVAL for failure.
 *
 * Object have to be reserved and mutex must be locked!
 */
static int amdgpu_vm_bo_update_mapping(struct amdgpu_device *adev,
				       struct amdgpu_vm *vm,
				       struct amdgpu_bo_va_mapping *mapping,
				       uint64_t addr, uint32_t gtt_flags,
				       struct fence **fence)
{
	struct amdgpu_ring *ring = adev->vm_manager.vm_pte_funcs_ring;
	unsigned nptes, ncmds, ndw;
	uint32_t flags = gtt_flags;
	struct amdgpu_ib *ib;
	struct fence *f = NULL;
	int r;

	/* normally,bo_va->flags only contians READABLE and WIRTEABLE bit go here
	 * but in case of something, we filter the flags in first place
	 */
	if (!(mapping->flags & AMDGPU_PTE_READABLE))
		flags &= ~AMDGPU_PTE_READABLE;
	if (!(mapping->flags & AMDGPU_PTE_WRITEABLE))
		flags &= ~AMDGPU_PTE_WRITEABLE;

	trace_amdgpu_vm_bo_update(mapping);

	nptes = mapping->it.last - mapping->it.start + 1;

	/*
	 * reserve space for one command every (1 << BLOCK_SIZE)
	 *  entries or 2k dwords (whatever is smaller)
	 */
	ncmds = (nptes >> min(amdgpu_vm_block_size, 11)) + 1;

	/* padding, etc. */
	ndw = 64;

	if ((flags & AMDGPU_PTE_SYSTEM) && (flags == gtt_flags)) {
		/* only copy commands needed */
		ndw += ncmds * 7;

	} else if (flags & AMDGPU_PTE_SYSTEM) {
		/* header for write data commands */
		ndw += ncmds * 4;

		/* body of write data command */
		ndw += nptes * 2;

	} else {
		/* set page commands needed */
		ndw += ncmds * 10;

		/* two extra commands for begin/end of fragment */
		ndw += 2 * 10;
	}

	/* update too big for an IB */
	if (ndw > 0xfffff)
		return -ENOMEM;

	ib = kzalloc(sizeof(struct amdgpu_ib), GFP_KERNEL);
	if (!ib)
		return -ENOMEM;

	r = amdgpu_ib_get(ring, NULL, ndw * 4, ib);
	if (r) {
		kfree(ib);
		return r;
	}

	ib->length_dw = 0;

	r = amdgpu_vm_update_ptes(adev, vm, ib, mapping->it.start,
				  mapping->it.last + 1, addr + mapping->offset,
				  flags, gtt_flags);

	if (r) {
		amdgpu_ib_free(adev, ib);
		kfree(ib);
		return r;
	}

	amdgpu_vm_pad_ib(adev, ib);
	WARN_ON(ib->length_dw > ndw);
	r = amdgpu_sched_ib_submit_kernel_helper(adev, ring, ib, 1,
						 &amdgpu_vm_free_job,
						 AMDGPU_FENCE_OWNER_VM,
						 &f);
	if (r)
		goto error_free;

	amdgpu_bo_fence(vm->page_directory, f, true);
	if (fence) {
		fence_put(*fence);
		*fence = fence_get(f);
	}
	fence_put(f);
	if (!amdgpu_enable_scheduler) {
		amdgpu_ib_free(adev, ib);
		kfree(ib);
	}
	return 0;

error_free:
	amdgpu_ib_free(adev, ib);
	kfree(ib);
	return r;
}

/**
 * amdgpu_vm_bo_update - update all BO mappings in the vm page table
 *
 * @adev: amdgpu_device pointer
 * @bo_va: requested BO and VM object
 * @mem: ttm mem
 *
 * Fill in the page table entries for @bo_va.
 * Returns 0 for success, -EINVAL for failure.
 *
 * Object have to be reserved and mutex must be locked!
 */
int amdgpu_vm_bo_update(struct amdgpu_device *adev,
			struct amdgpu_bo_va *bo_va,
			struct ttm_mem_reg *mem)
{
	struct amdgpu_vm *vm = bo_va->vm;
	struct amdgpu_bo_va_mapping *mapping;
	uint32_t flags;
	uint64_t addr;
	int r;

	if (mem) {
		addr = (u64)mem->start << PAGE_SHIFT;
		if (mem->mem_type != TTM_PL_TT)
			addr += adev->vm_manager.vram_base_offset;
	} else {
		addr = 0;
	}

	flags = amdgpu_ttm_tt_pte_flags(adev, bo_va->bo->tbo.ttm, mem);

	spin_lock(&vm->status_lock);
	if (!list_empty(&bo_va->vm_status))
		list_splice_init(&bo_va->valids, &bo_va->invalids);
	spin_unlock(&vm->status_lock);

	list_for_each_entry(mapping, &bo_va->invalids, list) {
		r = amdgpu_vm_bo_update_mapping(adev, vm, mapping, addr,
						flags, &bo_va->last_pt_update);
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
	if (!mem)
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

	spin_lock(&vm->freed_lock);
	while (!list_empty(&vm->freed)) {
		mapping = list_first_entry(&vm->freed,
			struct amdgpu_bo_va_mapping, list);
		list_del(&mapping->list);
		spin_unlock(&vm->freed_lock);
		r = amdgpu_vm_bo_update_mapping(adev, vm, mapping, 0, 0, NULL);
		kfree(mapping);
		if (r)
			return r;

		spin_lock(&vm->freed_lock);
	}
	spin_unlock(&vm->freed_lock);

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
		mutex_lock(&bo_va->mutex);
		r = amdgpu_vm_bo_update(adev, bo_va, NULL);
		mutex_unlock(&bo_va->mutex);
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
 * Add @bo into the requested vm (cayman+).
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
	mutex_init(&bo_va->mutex);
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

	spin_lock(&vm->it_lock);
	it = interval_tree_iter_first(&vm->va, saddr, eaddr);
	spin_unlock(&vm->it_lock);
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

	mutex_lock(&bo_va->mutex);
	list_add(&mapping->list, &bo_va->invalids);
	mutex_unlock(&bo_va->mutex);
	spin_lock(&vm->it_lock);
	interval_tree_insert(&mapping->it, &vm->va);
	spin_unlock(&vm->it_lock);
	trace_amdgpu_vm_bo_map(bo_va, mapping);

	/* Make sure the page tables are allocated */
	saddr >>= amdgpu_vm_block_size;
	eaddr >>= amdgpu_vm_block_size;

	BUG_ON(eaddr >= amdgpu_vm_num_pdes(adev));

	if (eaddr > vm->max_pde_used)
		vm->max_pde_used = eaddr;

	/* walk over the address space and allocate the page tables */
	for (pt_idx = saddr; pt_idx <= eaddr; ++pt_idx) {
		struct reservation_object *resv = vm->page_directory->tbo.resv;
		struct amdgpu_bo *pt;

		if (vm->page_tables[pt_idx].bo)
			continue;

		r = amdgpu_bo_create(adev, AMDGPU_VM_PTE_COUNT * 8,
				     AMDGPU_GPU_PAGE_SIZE, true,
				     AMDGPU_GEM_DOMAIN_VRAM,
				     AMDGPU_GEM_CREATE_NO_CPU_ACCESS,
				     NULL, resv, &pt);
		if (r)
			goto error_free;

		/* Keep a reference to the page table to avoid freeing
		 * them up in the wrong order.
		 */
		pt->parent = amdgpu_bo_ref(vm->page_directory);

		r = amdgpu_vm_clear_bo(adev, pt);
		if (r) {
			amdgpu_bo_unref(&pt);
			goto error_free;
		}

		vm->page_tables[pt_idx].addr = 0;
		vm->page_tables[pt_idx].bo = pt;
	}

	return 0;

error_free:
	list_del(&mapping->list);
	spin_lock(&vm->it_lock);
	interval_tree_remove(&mapping->it, &vm->va);
	spin_unlock(&vm->it_lock);
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
	mutex_lock(&bo_va->mutex);
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

		if (&mapping->list == &bo_va->invalids) {
			mutex_unlock(&bo_va->mutex);
			return -ENOENT;
		}
	}
	mutex_unlock(&bo_va->mutex);
	list_del(&mapping->list);
	spin_lock(&vm->it_lock);
	interval_tree_remove(&mapping->it, &vm->va);
	spin_unlock(&vm->it_lock);
	trace_amdgpu_vm_bo_unmap(bo_va, mapping);

	if (valid) {
		spin_lock(&vm->freed_lock);
		list_add(&mapping->list, &vm->freed);
		spin_unlock(&vm->freed_lock);
	} else {
		kfree(mapping);
	}

	return 0;
}

/**
 * amdgpu_vm_bo_rmv - remove a bo to a specific vm
 *
 * @adev: amdgpu_device pointer
 * @bo_va: requested bo_va
 *
 * Remove @bo_va->bo from the requested vm (cayman+).
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
		spin_lock(&vm->it_lock);
		interval_tree_remove(&mapping->it, &vm->va);
		spin_unlock(&vm->it_lock);
		trace_amdgpu_vm_bo_unmap(bo_va, mapping);
		spin_lock(&vm->freed_lock);
		list_add(&mapping->list, &vm->freed);
		spin_unlock(&vm->freed_lock);
	}
	list_for_each_entry_safe(mapping, next, &bo_va->invalids, list) {
		list_del(&mapping->list);
		spin_lock(&vm->it_lock);
		interval_tree_remove(&mapping->it, &vm->va);
		spin_unlock(&vm->it_lock);
		kfree(mapping);
	}
	fence_put(bo_va->last_pt_update);
	mutex_destroy(&bo_va->mutex);
	kfree(bo_va);
}

/**
 * amdgpu_vm_bo_invalidate - mark the bo as invalid
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @bo: amdgpu buffer object
 *
 * Mark @bo as invalid (cayman+).
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
 * Init @vm fields (cayman+).
 */
int amdgpu_vm_init(struct amdgpu_device *adev, struct amdgpu_vm *vm)
{
	const unsigned align = min(AMDGPU_VM_PTB_ALIGN_SIZE,
		AMDGPU_VM_PTE_COUNT * 8);
	unsigned pd_size, pd_entries;
	int i, r;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		vm->ids[i].id = 0;
		vm->ids[i].flushed_updates = NULL;
	}
	vm->va = RB_ROOT;
	spin_lock_init(&vm->status_lock);
	INIT_LIST_HEAD(&vm->invalidated);
	INIT_LIST_HEAD(&vm->cleared);
	INIT_LIST_HEAD(&vm->freed);
	spin_lock_init(&vm->it_lock);
	spin_lock_init(&vm->freed_lock);
	pd_size = amdgpu_vm_directory_size(adev);
	pd_entries = amdgpu_vm_num_pdes(adev);

	/* allocate page table array */
	vm->page_tables = drm_calloc_large(pd_entries, sizeof(struct amdgpu_vm_pt));
	if (vm->page_tables == NULL) {
		DRM_ERROR("Cannot allocate memory for page table array\n");
		return -ENOMEM;
	}

	vm->page_directory_fence = NULL;

	r = amdgpu_bo_create(adev, pd_size, align, true,
			     AMDGPU_GEM_DOMAIN_VRAM,
			     AMDGPU_GEM_CREATE_NO_CPU_ACCESS,
			     NULL, NULL, &vm->page_directory);
	if (r)
		return r;
	r = amdgpu_bo_reserve(vm->page_directory, false);
	if (r) {
		amdgpu_bo_unref(&vm->page_directory);
		vm->page_directory = NULL;
		return r;
	}
	r = amdgpu_vm_clear_bo(adev, vm->page_directory);
	amdgpu_bo_unreserve(vm->page_directory);
	if (r) {
		amdgpu_bo_unref(&vm->page_directory);
		vm->page_directory = NULL;
		return r;
	}

	return 0;
}

/**
 * amdgpu_vm_fini - tear down a vm instance
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 *
 * Tear down @vm (cayman+).
 * Unbind the VM and remove all bos from the vm bo list
 */
void amdgpu_vm_fini(struct amdgpu_device *adev, struct amdgpu_vm *vm)
{
	struct amdgpu_bo_va_mapping *mapping, *tmp;
	int i;

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

	for (i = 0; i < amdgpu_vm_num_pdes(adev); i++)
		amdgpu_bo_unref(&vm->page_tables[i].bo);
	drm_free_large(vm->page_tables);

	amdgpu_bo_unref(&vm->page_directory);
	fence_put(vm->page_directory_fence);
	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		unsigned id = vm->ids[i].id;

		atomic_long_cmpxchg(&adev->vm_manager.ids[id].owner,
				    (long)vm, 0);
		fence_put(vm->ids[i].flushed_updates);
	}

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

	for (i = 0; i < AMDGPU_NUM_VM; ++i)
		fence_put(adev->vm_manager.ids[i].active);
}

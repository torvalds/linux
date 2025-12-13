// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
 */

#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_trace.h"
#include "amdgpu_vm.h"
#include "amdgpu_job.h"

/*
 * amdgpu_vm_pt_cursor - state for for_each_amdgpu_vm_pt
 */
struct amdgpu_vm_pt_cursor {
	uint64_t pfn;
	struct amdgpu_vm_bo_base *parent;
	struct amdgpu_vm_bo_base *entry;
	unsigned int level;
};

/**
 * amdgpu_vm_pt_level_shift - return the addr shift for each level
 *
 * @adev: amdgpu_device pointer
 * @level: VMPT level
 *
 * Returns:
 * The number of bits the pfn needs to be right shifted for a level.
 */
static unsigned int amdgpu_vm_pt_level_shift(struct amdgpu_device *adev,
					     unsigned int level)
{
	switch (level) {
	case AMDGPU_VM_PDB2:
	case AMDGPU_VM_PDB1:
	case AMDGPU_VM_PDB0:
		return 9 * (AMDGPU_VM_PDB0 - level) +
			adev->vm_manager.block_size;
	case AMDGPU_VM_PTB:
		return 0;
	default:
		return ~0;
	}
}

/**
 * amdgpu_vm_pt_num_entries - return the number of entries in a PD/PT
 *
 * @adev: amdgpu_device pointer
 * @level: VMPT level
 *
 * Returns:
 * The number of entries in a page directory or page table.
 */
static unsigned int amdgpu_vm_pt_num_entries(struct amdgpu_device *adev,
					     unsigned int level)
{
	unsigned int shift;

	shift = amdgpu_vm_pt_level_shift(adev, adev->vm_manager.root_level);
	if (level == adev->vm_manager.root_level)
		/* For the root directory */
		return round_up(adev->vm_manager.max_pfn, 1ULL << shift)
			>> shift;
	else if (level != AMDGPU_VM_PTB)
		/* Everything in between */
		return 512;

	/* For the page tables on the leaves */
	return AMDGPU_VM_PTE_COUNT(adev);
}

/**
 * amdgpu_vm_pt_entries_mask - the mask to get the entry number of a PD/PT
 *
 * @adev: amdgpu_device pointer
 * @level: VMPT level
 *
 * Returns:
 * The mask to extract the entry number of a PD/PT from an address.
 */
static uint32_t amdgpu_vm_pt_entries_mask(struct amdgpu_device *adev,
					  unsigned int level)
{
	if (level <= adev->vm_manager.root_level)
		return 0xffffffff;
	else if (level != AMDGPU_VM_PTB)
		return 0x1ff;
	else
		return AMDGPU_VM_PTE_COUNT(adev) - 1;
}

/**
 * amdgpu_vm_pt_size - returns the size of the page table in bytes
 *
 * @adev: amdgpu_device pointer
 * @level: VMPT level
 *
 * Returns:
 * The size of the BO for a page directory or page table in bytes.
 */
static unsigned int amdgpu_vm_pt_size(struct amdgpu_device *adev,
				      unsigned int level)
{
	return AMDGPU_GPU_PAGE_ALIGN(amdgpu_vm_pt_num_entries(adev, level) * 8);
}

/**
 * amdgpu_vm_pt_parent - get the parent page directory
 *
 * @pt: child page table
 *
 * Helper to get the parent entry for the child page table. NULL if we are at
 * the root page directory.
 */
static struct amdgpu_vm_bo_base *
amdgpu_vm_pt_parent(struct amdgpu_vm_bo_base *pt)
{
	struct amdgpu_bo *parent = pt->bo->parent;

	if (!parent)
		return NULL;

	return parent->vm_bo;
}

/**
 * amdgpu_vm_pt_start - start PD/PT walk
 *
 * @adev: amdgpu_device pointer
 * @vm: amdgpu_vm structure
 * @start: start address of the walk
 * @cursor: state to initialize
 *
 * Initialize a amdgpu_vm_pt_cursor to start a walk.
 */
static void amdgpu_vm_pt_start(struct amdgpu_device *adev,
			       struct amdgpu_vm *vm, uint64_t start,
			       struct amdgpu_vm_pt_cursor *cursor)
{
	cursor->pfn = start;
	cursor->parent = NULL;
	cursor->entry = &vm->root;
	cursor->level = adev->vm_manager.root_level;
}

/**
 * amdgpu_vm_pt_descendant - go to child node
 *
 * @adev: amdgpu_device pointer
 * @cursor: current state
 *
 * Walk to the child node of the current node.
 * Returns:
 * True if the walk was possible, false otherwise.
 */
static bool amdgpu_vm_pt_descendant(struct amdgpu_device *adev,
				    struct amdgpu_vm_pt_cursor *cursor)
{
	unsigned int mask, shift, idx;

	if ((cursor->level == AMDGPU_VM_PTB) || !cursor->entry ||
	    !cursor->entry->bo)
		return false;

	mask = amdgpu_vm_pt_entries_mask(adev, cursor->level);
	shift = amdgpu_vm_pt_level_shift(adev, cursor->level);

	++cursor->level;
	idx = (cursor->pfn >> shift) & mask;
	cursor->parent = cursor->entry;
	cursor->entry = &to_amdgpu_bo_vm(cursor->entry->bo)->entries[idx];
	return true;
}

/**
 * amdgpu_vm_pt_sibling - go to sibling node
 *
 * @adev: amdgpu_device pointer
 * @cursor: current state
 *
 * Walk to the sibling node of the current node.
 * Returns:
 * True if the walk was possible, false otherwise.
 */
static bool amdgpu_vm_pt_sibling(struct amdgpu_device *adev,
				 struct amdgpu_vm_pt_cursor *cursor)
{

	unsigned int shift, num_entries;
	struct amdgpu_bo_vm *parent;

	/* Root doesn't have a sibling */
	if (!cursor->parent)
		return false;

	/* Go to our parents and see if we got a sibling */
	shift = amdgpu_vm_pt_level_shift(adev, cursor->level - 1);
	num_entries = amdgpu_vm_pt_num_entries(adev, cursor->level - 1);
	parent = to_amdgpu_bo_vm(cursor->parent->bo);

	if (cursor->entry == &parent->entries[num_entries - 1])
		return false;

	cursor->pfn += 1ULL << shift;
	cursor->pfn &= ~((1ULL << shift) - 1);
	++cursor->entry;
	return true;
}

/**
 * amdgpu_vm_pt_ancestor - go to parent node
 *
 * @cursor: current state
 *
 * Walk to the parent node of the current node.
 * Returns:
 * True if the walk was possible, false otherwise.
 */
static bool amdgpu_vm_pt_ancestor(struct amdgpu_vm_pt_cursor *cursor)
{
	if (!cursor->parent)
		return false;

	--cursor->level;
	cursor->entry = cursor->parent;
	cursor->parent = amdgpu_vm_pt_parent(cursor->parent);
	return true;
}

/**
 * amdgpu_vm_pt_next - get next PD/PT in hieratchy
 *
 * @adev: amdgpu_device pointer
 * @cursor: current state
 *
 * Walk the PD/PT tree to the next node.
 */
static void amdgpu_vm_pt_next(struct amdgpu_device *adev,
			      struct amdgpu_vm_pt_cursor *cursor)
{
	/* First try a newborn child */
	if (amdgpu_vm_pt_descendant(adev, cursor))
		return;

	/* If that didn't worked try to find a sibling */
	while (!amdgpu_vm_pt_sibling(adev, cursor)) {
		/* No sibling, go to our parents and grandparents */
		if (!amdgpu_vm_pt_ancestor(cursor)) {
			cursor->pfn = ~0ll;
			return;
		}
	}
}

/**
 * amdgpu_vm_pt_first_dfs - start a deep first search
 *
 * @adev: amdgpu_device structure
 * @vm: amdgpu_vm structure
 * @start: optional cursor to start with
 * @cursor: state to initialize
 *
 * Starts a deep first traversal of the PD/PT tree.
 */
static void amdgpu_vm_pt_first_dfs(struct amdgpu_device *adev,
				   struct amdgpu_vm *vm,
				   struct amdgpu_vm_pt_cursor *start,
				   struct amdgpu_vm_pt_cursor *cursor)
{
	if (start)
		*cursor = *start;
	else
		amdgpu_vm_pt_start(adev, vm, 0, cursor);

	while (amdgpu_vm_pt_descendant(adev, cursor))
		;
}

/**
 * amdgpu_vm_pt_continue_dfs - check if the deep first search should continue
 *
 * @start: starting point for the search
 * @entry: current entry
 *
 * Returns:
 * True when the search should continue, false otherwise.
 */
static bool amdgpu_vm_pt_continue_dfs(struct amdgpu_vm_pt_cursor *start,
				      struct amdgpu_vm_bo_base *entry)
{
	return entry && (!start || entry != start->entry);
}

/**
 * amdgpu_vm_pt_next_dfs - get the next node for a deep first search
 *
 * @adev: amdgpu_device structure
 * @cursor: current state
 *
 * Move the cursor to the next node in a deep first search.
 */
static void amdgpu_vm_pt_next_dfs(struct amdgpu_device *adev,
				  struct amdgpu_vm_pt_cursor *cursor)
{
	if (!cursor->entry)
		return;

	if (!cursor->parent)
		cursor->entry = NULL;
	else if (amdgpu_vm_pt_sibling(adev, cursor))
		while (amdgpu_vm_pt_descendant(adev, cursor))
			;
	else
		amdgpu_vm_pt_ancestor(cursor);
}

/*
 * for_each_amdgpu_vm_pt_dfs_safe - safe deep first search of all PDs/PTs
 */
#define for_each_amdgpu_vm_pt_dfs_safe(adev, vm, start, cursor, entry)		\
	for (amdgpu_vm_pt_first_dfs((adev), (vm), (start), &(cursor)),		\
	     (entry) = (cursor).entry, amdgpu_vm_pt_next_dfs((adev), &(cursor));\
	     amdgpu_vm_pt_continue_dfs((start), (entry));			\
	     (entry) = (cursor).entry, amdgpu_vm_pt_next_dfs((adev), &(cursor)))

/**
 * amdgpu_vm_pt_clear - initially clear the PDs/PTs
 *
 * @adev: amdgpu_device pointer
 * @vm: VM to clear BO from
 * @vmbo: BO to clear
 * @immediate: use an immediate update
 *
 * Root PD needs to be reserved when calling this.
 *
 * Returns:
 * 0 on success, errno otherwise.
 */
int amdgpu_vm_pt_clear(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		       struct amdgpu_bo_vm *vmbo, bool immediate)
{
	unsigned int level = adev->vm_manager.root_level;
	struct ttm_operation_ctx ctx = { true, false };
	struct amdgpu_vm_update_params params;
	struct amdgpu_bo *ancestor = &vmbo->bo;
	unsigned int entries;
	struct amdgpu_bo *bo = &vmbo->bo;
	uint64_t addr;
	int r, idx;

	/* Figure out our place in the hierarchy */
	if (ancestor->parent) {
		++level;
		while (ancestor->parent->parent) {
			++level;
			ancestor = ancestor->parent;
		}
	}

	entries = amdgpu_bo_size(bo) / 8;

	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (r)
		return r;

	if (!drm_dev_enter(adev_to_drm(adev), &idx))
		return -ENODEV;

	r = vm->update_funcs->map_table(vmbo);
	if (r)
		goto exit;

	memset(&params, 0, sizeof(params));
	params.adev = adev;
	params.vm = vm;
	params.immediate = immediate;

	r = vm->update_funcs->prepare(&params, NULL,
				      AMDGPU_KERNEL_JOB_ID_VM_PT_CLEAR);
	if (r)
		goto exit;

	addr = 0;

	uint64_t value = 0, flags = 0;
	if (adev->asic_type >= CHIP_VEGA10) {
		if (level != AMDGPU_VM_PTB) {
			/* Handle leaf PDEs as PTEs */
			flags |= AMDGPU_PDE_PTE_FLAG(adev);
			amdgpu_gmc_get_vm_pde(adev, level,
					      &value, &flags);
		} else {
			/* Workaround for fault priority problem on GMC9 */
			flags = AMDGPU_PTE_EXECUTABLE;
		}
	}

	r = vm->update_funcs->update(&params, vmbo, addr, 0, entries,
				     value, flags);
	if (r)
		goto exit;

	r = vm->update_funcs->commit(&params, NULL);
exit:
	drm_dev_exit(idx);
	return r;
}

/**
 * amdgpu_vm_pt_create - create bo for PD/PT
 *
 * @adev: amdgpu_device pointer
 * @vm: requesting vm
 * @level: the page table level
 * @immediate: use a immediate update
 * @vmbo: pointer to the buffer object pointer
 * @xcp_id: GPU partition id
 */
int amdgpu_vm_pt_create(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			int level, bool immediate, struct amdgpu_bo_vm **vmbo,
			int32_t xcp_id)
{
	struct amdgpu_bo_param bp;
	unsigned int num_entries;

	memset(&bp, 0, sizeof(bp));

	bp.size = amdgpu_vm_pt_size(adev, level);
	bp.byte_align = AMDGPU_GPU_PAGE_SIZE;

	if (!adev->gmc.is_app_apu)
		bp.domain = AMDGPU_GEM_DOMAIN_VRAM;
	else
		bp.domain = AMDGPU_GEM_DOMAIN_GTT;

	bp.domain = amdgpu_bo_get_preferred_domain(adev, bp.domain);
	bp.flags = AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS |
		AMDGPU_GEM_CREATE_CPU_GTT_USWC;

	if (level < AMDGPU_VM_PTB)
		num_entries = amdgpu_vm_pt_num_entries(adev, level);
	else
		num_entries = 0;

	bp.bo_ptr_size = struct_size((*vmbo), entries, num_entries);

	if (vm->use_cpu_for_update)
		bp.flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;

	bp.type = ttm_bo_type_kernel;
	bp.no_wait_gpu = immediate;
	bp.xcp_id_plus1 = xcp_id + 1;

	if (vm->root.bo)
		bp.resv = vm->root.bo->tbo.base.resv;

	return amdgpu_bo_create_vm(adev, &bp, vmbo);
}

/**
 * amdgpu_vm_pt_alloc - Allocate a specific page table
 *
 * @adev: amdgpu_device pointer
 * @vm: VM to allocate page tables for
 * @cursor: Which page table to allocate
 * @immediate: use an immediate update
 *
 * Make sure a specific page table or directory is allocated.
 *
 * Returns:
 * 1 if page table needed to be allocated, 0 if page table was already
 * allocated, negative errno if an error occurred.
 */
static int amdgpu_vm_pt_alloc(struct amdgpu_device *adev,
			      struct amdgpu_vm *vm,
			      struct amdgpu_vm_pt_cursor *cursor,
			      bool immediate)
{
	struct amdgpu_vm_bo_base *entry = cursor->entry;
	struct amdgpu_bo *pt_bo;
	struct amdgpu_bo_vm *pt;
	int r;

	if (entry->bo)
		return 0;

	amdgpu_vm_eviction_unlock(vm);
	r = amdgpu_vm_pt_create(adev, vm, cursor->level, immediate, &pt,
				vm->root.bo->xcp_id);
	amdgpu_vm_eviction_lock(vm);
	if (r)
		return r;

	/* Keep a reference to the root directory to avoid
	 * freeing them up in the wrong order.
	 */
	pt_bo = &pt->bo;
	pt_bo->parent = amdgpu_bo_ref(cursor->parent->bo);
	amdgpu_vm_bo_base_init(entry, vm, pt_bo);
	r = amdgpu_vm_pt_clear(adev, vm, pt, immediate);
	if (r)
		goto error_free_pt;

	return 0;

error_free_pt:
	amdgpu_bo_unref(&pt_bo);
	return r;
}

/**
 * amdgpu_vm_pt_free - free one PD/PT
 *
 * @entry: PDE to free
 */
static void amdgpu_vm_pt_free(struct amdgpu_vm_bo_base *entry)
{
	if (!entry->bo)
		return;

	amdgpu_vm_update_stats(entry, entry->bo->tbo.resource, -1);
	entry->bo->vm_bo = NULL;
	ttm_bo_set_bulk_move(&entry->bo->tbo, NULL);

	spin_lock(&entry->vm->status_lock);
	list_del(&entry->vm_status);
	spin_unlock(&entry->vm->status_lock);
	amdgpu_bo_unref(&entry->bo);
}

/**
 * amdgpu_vm_pt_free_list - free PD/PT levels
 *
 * @adev: amdgpu device structure
 * @params: see amdgpu_vm_update_params definition
 *
 * Free the page directory objects saved in the flush list
 */
void amdgpu_vm_pt_free_list(struct amdgpu_device *adev,
			    struct amdgpu_vm_update_params *params)
{
	struct amdgpu_vm_bo_base *entry, *next;
	bool unlocked = params->unlocked;

	if (list_empty(&params->tlb_flush_waitlist))
		return;

	/*
	 * unlocked unmap clear page table leaves, warning to free the page entry.
	 */
	WARN_ON(unlocked);

	list_for_each_entry_safe(entry, next, &params->tlb_flush_waitlist, vm_status)
		amdgpu_vm_pt_free(entry);
}

/**
 * amdgpu_vm_pt_add_list - add PD/PT level to the flush list
 *
 * @params: parameters for the update
 * @cursor: first PT entry to start DF search from, non NULL
 *
 * This list will be freed after TLB flush.
 */
static void amdgpu_vm_pt_add_list(struct amdgpu_vm_update_params *params,
				  struct amdgpu_vm_pt_cursor *cursor)
{
	struct amdgpu_vm_pt_cursor seek;
	struct amdgpu_vm_bo_base *entry;

	spin_lock(&params->vm->status_lock);
	for_each_amdgpu_vm_pt_dfs_safe(params->adev, params->vm, cursor, seek, entry) {
		if (entry && entry->bo)
			list_move(&entry->vm_status, &params->tlb_flush_waitlist);
	}

	/* enter start node now */
	list_move(&cursor->entry->vm_status, &params->tlb_flush_waitlist);
	spin_unlock(&params->vm->status_lock);
}

/**
 * amdgpu_vm_pt_free_root - free root PD
 * @adev: amdgpu device structure
 * @vm: amdgpu vm structure
 *
 * Free the root page directory and everything below it.
 */
void amdgpu_vm_pt_free_root(struct amdgpu_device *adev, struct amdgpu_vm *vm)
{
	struct amdgpu_vm_pt_cursor cursor;
	struct amdgpu_vm_bo_base *entry;

	for_each_amdgpu_vm_pt_dfs_safe(adev, vm, NULL, cursor, entry) {
		if (entry)
			amdgpu_vm_pt_free(entry);
	}
}

/**
 * amdgpu_vm_pde_update - update a single level in the hierarchy
 *
 * @params: parameters for the update
 * @entry: entry to update
 *
 * Makes sure the requested entry in parent is up to date.
 */
int amdgpu_vm_pde_update(struct amdgpu_vm_update_params *params,
			 struct amdgpu_vm_bo_base *entry)
{
	struct amdgpu_vm_bo_base *parent = amdgpu_vm_pt_parent(entry);
	struct amdgpu_bo *bo, *pbo;
	struct amdgpu_vm *vm = params->vm;
	uint64_t pde, pt, flags;
	unsigned int level;

	if (WARN_ON(!parent))
		return -EINVAL;

	bo = parent->bo;
	for (level = 0, pbo = bo->parent; pbo; ++level)
		pbo = pbo->parent;

	level += params->adev->vm_manager.root_level;
	amdgpu_gmc_get_pde_for_bo(entry->bo, level, &pt, &flags);
	pde = (entry - to_amdgpu_bo_vm(parent->bo)->entries) * 8;
	return vm->update_funcs->update(params, to_amdgpu_bo_vm(bo), pde, pt,
					1, 0, flags);
}

/**
 * amdgpu_vm_pte_update_noretry_flags - Update PTE no-retry flags
 *
 * @adev: amdgpu_device pointer
 * @flags: pointer to PTE flags
 *
 * Update PTE no-retry flags when TF is enabled.
 */
static void amdgpu_vm_pte_update_noretry_flags(struct amdgpu_device *adev,
						uint64_t *flags)
{
	/*
	 * Update no-retry flags with the corresponding TF
	 * no-retry combination.
	 */
	if ((*flags & AMDGPU_VM_NORETRY_FLAGS) == AMDGPU_VM_NORETRY_FLAGS) {
		*flags &= ~AMDGPU_VM_NORETRY_FLAGS;
		*flags |= adev->gmc.noretry_flags;
	}
}

/*
 * amdgpu_vm_pte_update_flags - figure out flags for PTE updates
 *
 * Make sure to set the right flags for the PTEs at the desired level.
 */
static void amdgpu_vm_pte_update_flags(struct amdgpu_vm_update_params *params,
				       struct amdgpu_bo_vm *pt,
				       unsigned int level,
				       uint64_t pe, uint64_t addr,
				       unsigned int count, uint32_t incr,
				       uint64_t flags)
{
	struct amdgpu_device *adev = params->adev;

	if (level != AMDGPU_VM_PTB) {
		flags |= AMDGPU_PDE_PTE_FLAG(params->adev);
		amdgpu_gmc_get_vm_pde(adev, level, &addr, &flags);

	} else if (adev->asic_type >= CHIP_VEGA10 &&
		   !(flags & AMDGPU_PTE_VALID) &&
		   !(flags & AMDGPU_PTE_PRT_FLAG(params->adev))) {

		/* Workaround for fault priority problem on GMC9 */
		flags |= AMDGPU_PTE_EXECUTABLE;
	}

	/*
	 * Update no-retry flags to use the no-retry flag combination
	 * with TF enabled. The AMDGPU_VM_NORETRY_FLAGS flag combination
	 * does not work when TF is enabled. So, replace them with
	 * AMDGPU_VM_NORETRY_FLAGS_TF flag combination which works for
	 * all cases.
	 */
	if (level == AMDGPU_VM_PTB)
		amdgpu_vm_pte_update_noretry_flags(adev, &flags);

	/* APUs mapping system memory may need different MTYPEs on different
	 * NUMA nodes. Only do this for contiguous ranges that can be assumed
	 * to be on the same NUMA node.
	 */
	if ((flags & AMDGPU_PTE_SYSTEM) && (adev->flags & AMD_IS_APU) &&
	    adev->gmc.gmc_funcs->override_vm_pte_flags &&
	    num_possible_nodes() > 1 && !params->pages_addr && params->allow_override)
		amdgpu_gmc_override_vm_pte_flags(adev, params->vm, addr, &flags);

	params->vm->update_funcs->update(params, pt, pe, addr, count, incr,
					 flags);
}

/**
 * amdgpu_vm_pte_fragment - get fragment for PTEs
 *
 * @params: see amdgpu_vm_update_params definition
 * @start: first PTE to handle
 * @end: last PTE to handle
 * @flags: hw mapping flags
 * @frag: resulting fragment size
 * @frag_end: end of this fragment
 *
 * Returns the first possible fragment for the start and end address.
 */
static void amdgpu_vm_pte_fragment(struct amdgpu_vm_update_params *params,
				   uint64_t start, uint64_t end, uint64_t flags,
				   unsigned int *frag, uint64_t *frag_end)
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
	 *
	 * Starting with Vega10 the fragment size only controls the L1. The L2
	 * is now directly feed with small/huge/giant pages from the walker.
	 */
	unsigned int max_frag;

	if (params->adev->asic_type < CHIP_VEGA10)
		max_frag = params->adev->vm_manager.fragment_size;
	else
		max_frag = 31;

	/* system pages are non continuously */
	if (params->pages_addr) {
		*frag = 0;
		*frag_end = end;
		return;
	}

	/* This intentionally wraps around if no bit is set */
	*frag = min_t(unsigned int, ffs(start) - 1, fls64(end - start) - 1);
	if (*frag >= max_frag) {
		*frag = max_frag;
		*frag_end = end & ~((1ULL << max_frag) - 1);
	} else {
		*frag_end = start + (1 << *frag);
	}
}

/**
 * amdgpu_vm_ptes_update - make sure that page tables are valid
 *
 * @params: see amdgpu_vm_update_params definition
 * @start: start of GPU address range
 * @end: end of GPU address range
 * @dst: destination address to map to, the next dst inside the function
 * @flags: mapping flags
 *
 * Update the page tables in the range @start - @end.
 *
 * Returns:
 * 0 for success, -EINVAL for failure.
 */
int amdgpu_vm_ptes_update(struct amdgpu_vm_update_params *params,
			  uint64_t start, uint64_t end,
			  uint64_t dst, uint64_t flags)
{
	struct amdgpu_device *adev = params->adev;
	struct amdgpu_vm_pt_cursor cursor;
	uint64_t frag_start = start, frag_end;
	unsigned int frag;
	int r;

	/* figure out the initial fragment */
	amdgpu_vm_pte_fragment(params, frag_start, end, flags, &frag,
			       &frag_end);

	/* walk over the address space and update the PTs */
	amdgpu_vm_pt_start(adev, params->vm, start, &cursor);
	while (cursor.pfn < end) {
		unsigned int shift, parent_shift, mask;
		uint64_t incr, entry_end, pe_start;
		struct amdgpu_bo *pt;

		if (!params->unlocked) {
			/* make sure that the page tables covering the
			 * address range are actually allocated
			 */
			r = amdgpu_vm_pt_alloc(params->adev, params->vm,
					       &cursor, params->immediate);
			if (r)
				return r;
		}

		shift = amdgpu_vm_pt_level_shift(adev, cursor.level);
		parent_shift = amdgpu_vm_pt_level_shift(adev, cursor.level - 1);
		if (params->unlocked) {
			/* Unlocked updates are only allowed on the leaves */
			if (amdgpu_vm_pt_descendant(adev, &cursor))
				continue;
		} else if (adev->asic_type < CHIP_VEGA10 &&
			   (flags & AMDGPU_PTE_VALID)) {
			/* No huge page support before GMC v9 */
			if (cursor.level != AMDGPU_VM_PTB) {
				if (!amdgpu_vm_pt_descendant(adev, &cursor))
					return -ENOENT;
				continue;
			}
		} else if (frag < shift) {
			/* We can't use this level when the fragment size is
			 * smaller than the address shift. Go to the next
			 * child entry and try again.
			 */
			if (amdgpu_vm_pt_descendant(adev, &cursor))
				continue;
		} else if (frag >= parent_shift) {
			/* If the fragment size is even larger than the parent
			 * shift we should go up one level and check it again.
			 */
			if (!amdgpu_vm_pt_ancestor(&cursor))
				return -EINVAL;
			continue;
		}

		pt = cursor.entry->bo;
		if (!pt) {
			/* We need all PDs and PTs for mapping something, */
			if (flags & AMDGPU_PTE_VALID)
				return -ENOENT;

			/* but unmapping something can happen at a higher
			 * level.
			 */
			if (!amdgpu_vm_pt_ancestor(&cursor))
				return -EINVAL;

			pt = cursor.entry->bo;
			shift = parent_shift;
			frag_end = max(frag_end, ALIGN(frag_start + 1,
				   1ULL << shift));
		}

		/* Looks good so far, calculate parameters for the update */
		incr = (uint64_t)AMDGPU_GPU_PAGE_SIZE << shift;
		mask = amdgpu_vm_pt_entries_mask(adev, cursor.level);
		pe_start = ((cursor.pfn >> shift) & mask) * 8;

		if (cursor.level < AMDGPU_VM_PTB && params->unlocked)
			/*
			 * MMU notifier callback unlocked unmap huge page, leave is PDE entry,
			 * only clear one entry. Next entry search again for PDE or PTE leave.
			 */
			entry_end = 1ULL << shift;
		else
			entry_end = ((uint64_t)mask + 1) << shift;
		entry_end += cursor.pfn & ~(entry_end - 1);
		entry_end = min(entry_end, end);

		do {
			struct amdgpu_vm *vm = params->vm;
			uint64_t upd_end = min(entry_end, frag_end);
			unsigned int nptes = (upd_end - frag_start) >> shift;
			uint64_t upd_flags = flags | AMDGPU_PTE_FRAG(frag);

			/* This can happen when we set higher level PDs to
			 * silent to stop fault floods.
			 */
			nptes = max(nptes, 1u);

			trace_amdgpu_vm_update_ptes(params, frag_start, upd_end,
						    min(nptes, 32u), dst, incr,
						    upd_flags,
						    vm->task_info ? vm->task_info->tgid : 0,
						    vm->immediate.fence_context);
			amdgpu_vm_pte_update_flags(params, to_amdgpu_bo_vm(pt),
						   cursor.level, pe_start, dst,
						   nptes, incr, upd_flags);

			pe_start += nptes * 8;
			dst += nptes * incr;

			frag_start = upd_end;
			if (frag_start >= frag_end) {
				/* figure out the next fragment */
				amdgpu_vm_pte_fragment(params, frag_start, end,
						       flags, &frag, &frag_end);
				if (frag < shift)
					break;
			}
		} while (frag_start < entry_end);

		if (amdgpu_vm_pt_descendant(adev, &cursor)) {
			/* Free all child entries.
			 * Update the tables with the flags and addresses and free up subsequent
			 * tables in the case of huge pages or freed up areas.
			 * This is the maximum you can free, because all other page tables are not
			 * completely covered by the range and so potentially still in use.
			 */
			while (cursor.pfn < frag_start) {
				/* Make sure previous mapping is freed */
				if (cursor.entry->bo) {
					params->needs_flush = true;
					amdgpu_vm_pt_add_list(params, &cursor);
				}
				amdgpu_vm_pt_next(adev, &cursor);
			}

		} else if (frag >= shift) {
			/* or just move on to the next on the same level. */
			amdgpu_vm_pt_next(adev, &cursor);
		}
	}

	return 0;
}

/**
 * amdgpu_vm_pt_map_tables - have bo of root PD cpu accessible
 * @adev: amdgpu device structure
 * @vm: amdgpu vm structure
 *
 * make root page directory and everything below it cpu accessible.
 */
int amdgpu_vm_pt_map_tables(struct amdgpu_device *adev, struct amdgpu_vm *vm)
{
	struct amdgpu_vm_pt_cursor cursor;
	struct amdgpu_vm_bo_base *entry;

	for_each_amdgpu_vm_pt_dfs_safe(adev, vm, NULL, cursor, entry) {

		struct amdgpu_bo_vm *bo;
		int r;

		if (entry->bo) {
			bo = to_amdgpu_bo_vm(entry->bo);
			r = vm->update_funcs->map_table(bo);
			if (r)
				return r;
		}
	}

	return 0;
}

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
#include <linux/dma-fence-array.h>
#include <linux/interval_tree_generic.h>
#include <linux/idr.h>
#include <linux/dma-buf.h>

#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_gmc.h"
#include "amdgpu_xgmi.h"
#include "amdgpu_dma_buf.h"

/**
 * DOC: GPUVM
 *
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

#define START(node) ((node)->start)
#define LAST(node) ((node)->last)

INTERVAL_TREE_DEFINE(struct amdgpu_bo_va_mapping, rb, uint64_t, __subtree_last,
		     START, LAST, static, amdgpu_vm_it)

#undef START
#undef LAST

/**
 * struct amdgpu_prt_cb - Helper to disable partial resident texture feature from a fence callback
 */
struct amdgpu_prt_cb {

	/**
	 * @adev: amdgpu device
	 */
	struct amdgpu_device *adev;

	/**
	 * @cb: callback
	 */
	struct dma_fence_cb cb;
};

/*
 * vm eviction_lock can be taken in MMU notifiers. Make sure no reclaim-FS
 * happens while holding this lock anywhere to prevent deadlocks when
 * an MMU notifier runs in reclaim-FS context.
 */
static inline void amdgpu_vm_eviction_lock(struct amdgpu_vm *vm)
{
	mutex_lock(&vm->eviction_lock);
	vm->saved_flags = memalloc_nofs_save();
}

static inline int amdgpu_vm_eviction_trylock(struct amdgpu_vm *vm)
{
	if (mutex_trylock(&vm->eviction_lock)) {
		vm->saved_flags = memalloc_nofs_save();
		return 1;
	}
	return 0;
}

static inline void amdgpu_vm_eviction_unlock(struct amdgpu_vm *vm)
{
	memalloc_nofs_restore(vm->saved_flags);
	mutex_unlock(&vm->eviction_lock);
}

/**
 * amdgpu_vm_level_shift - return the addr shift for each level
 *
 * @adev: amdgpu_device pointer
 * @level: VMPT level
 *
 * Returns:
 * The number of bits the pfn needs to be right shifted for a level.
 */
static unsigned amdgpu_vm_level_shift(struct amdgpu_device *adev,
				      unsigned level)
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
 * amdgpu_vm_num_entries - return the number of entries in a PD/PT
 *
 * @adev: amdgpu_device pointer
 * @level: VMPT level
 *
 * Returns:
 * The number of entries in a page directory or page table.
 */
static unsigned amdgpu_vm_num_entries(struct amdgpu_device *adev,
				      unsigned level)
{
	unsigned shift = amdgpu_vm_level_shift(adev,
					       adev->vm_manager.root_level);

	if (level == adev->vm_manager.root_level)
		/* For the root directory */
		return round_up(adev->vm_manager.max_pfn, 1ULL << shift)
			>> shift;
	else if (level != AMDGPU_VM_PTB)
		/* Everything in between */
		return 512;
	else
		/* For the page tables on the leaves */
		return AMDGPU_VM_PTE_COUNT(adev);
}

/**
 * amdgpu_vm_num_ats_entries - return the number of ATS entries in the root PD
 *
 * @adev: amdgpu_device pointer
 *
 * Returns:
 * The number of entries in the root page directory which needs the ATS setting.
 */
static unsigned amdgpu_vm_num_ats_entries(struct amdgpu_device *adev)
{
	unsigned shift;

	shift = amdgpu_vm_level_shift(adev, adev->vm_manager.root_level);
	return AMDGPU_GMC_HOLE_START >> (shift + AMDGPU_GPU_PAGE_SHIFT);
}

/**
 * amdgpu_vm_entries_mask - the mask to get the entry number of a PD/PT
 *
 * @adev: amdgpu_device pointer
 * @level: VMPT level
 *
 * Returns:
 * The mask to extract the entry number of a PD/PT from an address.
 */
static uint32_t amdgpu_vm_entries_mask(struct amdgpu_device *adev,
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
 * amdgpu_vm_bo_size - returns the size of the BOs in bytes
 *
 * @adev: amdgpu_device pointer
 * @level: VMPT level
 *
 * Returns:
 * The size of the BO for a page directory or page table in bytes.
 */
static unsigned amdgpu_vm_bo_size(struct amdgpu_device *adev, unsigned level)
{
	return AMDGPU_GPU_PAGE_ALIGN(amdgpu_vm_num_entries(adev, level) * 8);
}

/**
 * amdgpu_vm_bo_evicted - vm_bo is evicted
 *
 * @vm_bo: vm_bo which is evicted
 *
 * State for PDs/PTs and per VM BOs which are not at the location they should
 * be.
 */
static void amdgpu_vm_bo_evicted(struct amdgpu_vm_bo_base *vm_bo)
{
	struct amdgpu_vm *vm = vm_bo->vm;
	struct amdgpu_bo *bo = vm_bo->bo;

	vm_bo->moved = true;
	if (bo->tbo.type == ttm_bo_type_kernel)
		list_move(&vm_bo->vm_status, &vm->evicted);
	else
		list_move_tail(&vm_bo->vm_status, &vm->evicted);
}
/**
 * amdgpu_vm_bo_moved - vm_bo is moved
 *
 * @vm_bo: vm_bo which is moved
 *
 * State for per VM BOs which are moved, but that change is not yet reflected
 * in the page tables.
 */
static void amdgpu_vm_bo_moved(struct amdgpu_vm_bo_base *vm_bo)
{
	list_move(&vm_bo->vm_status, &vm_bo->vm->moved);
}

/**
 * amdgpu_vm_bo_idle - vm_bo is idle
 *
 * @vm_bo: vm_bo which is now idle
 *
 * State for PDs/PTs and per VM BOs which have gone through the state machine
 * and are now idle.
 */
static void amdgpu_vm_bo_idle(struct amdgpu_vm_bo_base *vm_bo)
{
	list_move(&vm_bo->vm_status, &vm_bo->vm->idle);
	vm_bo->moved = false;
}

/**
 * amdgpu_vm_bo_invalidated - vm_bo is invalidated
 *
 * @vm_bo: vm_bo which is now invalidated
 *
 * State for normal BOs which are invalidated and that change not yet reflected
 * in the PTs.
 */
static void amdgpu_vm_bo_invalidated(struct amdgpu_vm_bo_base *vm_bo)
{
	spin_lock(&vm_bo->vm->invalidated_lock);
	list_move(&vm_bo->vm_status, &vm_bo->vm->invalidated);
	spin_unlock(&vm_bo->vm->invalidated_lock);
}

/**
 * amdgpu_vm_bo_relocated - vm_bo is reloacted
 *
 * @vm_bo: vm_bo which is relocated
 *
 * State for PDs/PTs which needs to update their parent PD.
 * For the root PD, just move to idle state.
 */
static void amdgpu_vm_bo_relocated(struct amdgpu_vm_bo_base *vm_bo)
{
	if (vm_bo->bo->parent)
		list_move(&vm_bo->vm_status, &vm_bo->vm->relocated);
	else
		amdgpu_vm_bo_idle(vm_bo);
}

/**
 * amdgpu_vm_bo_done - vm_bo is done
 *
 * @vm_bo: vm_bo which is now done
 *
 * State for normal BOs which are invalidated and that change has been updated
 * in the PTs.
 */
static void amdgpu_vm_bo_done(struct amdgpu_vm_bo_base *vm_bo)
{
	spin_lock(&vm_bo->vm->invalidated_lock);
	list_del_init(&vm_bo->vm_status);
	spin_unlock(&vm_bo->vm->invalidated_lock);
}

/**
 * amdgpu_vm_bo_base_init - Adds bo to the list of bos associated with the vm
 *
 * @base: base structure for tracking BO usage in a VM
 * @vm: vm to which bo is to be added
 * @bo: amdgpu buffer object
 *
 * Initialize a bo_va_base structure and add it to the appropriate lists
 *
 */
static void amdgpu_vm_bo_base_init(struct amdgpu_vm_bo_base *base,
				   struct amdgpu_vm *vm,
				   struct amdgpu_bo *bo)
{
	base->vm = vm;
	base->bo = bo;
	base->next = NULL;
	INIT_LIST_HEAD(&base->vm_status);

	if (!bo)
		return;
	base->next = bo->vm_bo;
	bo->vm_bo = base;

	if (bo->tbo.base.resv != vm->root.base.bo->tbo.base.resv)
		return;

	vm->bulk_moveable = false;
	if (bo->tbo.type == ttm_bo_type_kernel && bo->parent)
		amdgpu_vm_bo_relocated(base);
	else
		amdgpu_vm_bo_idle(base);

	if (bo->preferred_domains &
	    amdgpu_mem_type_to_domain(bo->tbo.mem.mem_type))
		return;

	/*
	 * we checked all the prerequisites, but it looks like this per vm bo
	 * is currently evicted. add the bo to the evicted list to make sure it
	 * is validated on next vm use to avoid fault.
	 * */
	amdgpu_vm_bo_evicted(base);
}

/**
 * amdgpu_vm_pt_parent - get the parent page directory
 *
 * @pt: child page table
 *
 * Helper to get the parent entry for the child page table. NULL if we are at
 * the root page directory.
 */
static struct amdgpu_vm_pt *amdgpu_vm_pt_parent(struct amdgpu_vm_pt *pt)
{
	struct amdgpu_bo *parent = pt->base.bo->parent;

	if (!parent)
		return NULL;

	return container_of(parent->vm_bo, struct amdgpu_vm_pt, base);
}

/*
 * amdgpu_vm_pt_cursor - state for for_each_amdgpu_vm_pt
 */
struct amdgpu_vm_pt_cursor {
	uint64_t pfn;
	struct amdgpu_vm_pt *parent;
	struct amdgpu_vm_pt *entry;
	unsigned level;
};

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
	unsigned mask, shift, idx;

	if (!cursor->entry->entries)
		return false;

	BUG_ON(!cursor->entry->base.bo);
	mask = amdgpu_vm_entries_mask(adev, cursor->level);
	shift = amdgpu_vm_level_shift(adev, cursor->level);

	++cursor->level;
	idx = (cursor->pfn >> shift) & mask;
	cursor->parent = cursor->entry;
	cursor->entry = &cursor->entry->entries[idx];
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
	unsigned shift, num_entries;

	/* Root doesn't have a sibling */
	if (!cursor->parent)
		return false;

	/* Go to our parents and see if we got a sibling */
	shift = amdgpu_vm_level_shift(adev, cursor->level - 1);
	num_entries = amdgpu_vm_num_entries(adev, cursor->level - 1);

	if (cursor->entry == &cursor->parent->entries[num_entries - 1])
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
	while (amdgpu_vm_pt_descendant(adev, cursor));
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
				      struct amdgpu_vm_pt *entry)
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
		while (amdgpu_vm_pt_descendant(adev, cursor));
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
	entry->priority = 0;
	entry->tv.bo = &vm->root.base.bo->tbo;
	/* Two for VM updates, one for TTM and one for the CS job */
	entry->tv.num_shared = 4;
	entry->user_pages = NULL;
	list_add(&entry->tv.head, validated);
}

/**
 * amdgpu_vm_del_from_lru_notify - update bulk_moveable flag
 *
 * @bo: BO which was removed from the LRU
 *
 * Make sure the bulk_moveable flag is updated when a BO is removed from the
 * LRU.
 */
void amdgpu_vm_del_from_lru_notify(struct ttm_buffer_object *bo)
{
	struct amdgpu_bo *abo;
	struct amdgpu_vm_bo_base *bo_base;

	if (!amdgpu_bo_is_amdgpu_bo(bo))
		return;

	if (bo->mem.placement & TTM_PL_FLAG_NO_EVICT)
		return;

	abo = ttm_to_amdgpu_bo(bo);
	if (!abo->parent)
		return;
	for (bo_base = abo->vm_bo; bo_base; bo_base = bo_base->next) {
		struct amdgpu_vm *vm = bo_base->vm;

		if (abo->tbo.base.resv == vm->root.base.bo->tbo.base.resv)
			vm->bulk_moveable = false;
	}

}
/**
 * amdgpu_vm_move_to_lru_tail - move all BOs to the end of LRU
 *
 * @adev: amdgpu device pointer
 * @vm: vm providing the BOs
 *
 * Move all BOs to the end of LRU and remember their positions to put them
 * together.
 */
void amdgpu_vm_move_to_lru_tail(struct amdgpu_device *adev,
				struct amdgpu_vm *vm)
{
	struct amdgpu_vm_bo_base *bo_base;

	if (vm->bulk_moveable) {
		spin_lock(&ttm_bo_glob.lru_lock);
		ttm_bo_bulk_move_lru_tail(&vm->lru_bulk_move);
		spin_unlock(&ttm_bo_glob.lru_lock);
		return;
	}

	memset(&vm->lru_bulk_move, 0, sizeof(vm->lru_bulk_move));

	spin_lock(&ttm_bo_glob.lru_lock);
	list_for_each_entry(bo_base, &vm->idle, vm_status) {
		struct amdgpu_bo *bo = bo_base->bo;

		if (!bo->parent)
			continue;

		ttm_bo_move_to_lru_tail(&bo->tbo, &vm->lru_bulk_move);
		if (bo->shadow)
			ttm_bo_move_to_lru_tail(&bo->shadow->tbo,
						&vm->lru_bulk_move);
	}
	spin_unlock(&ttm_bo_glob.lru_lock);

	vm->bulk_moveable = true;
}

/**
 * amdgpu_vm_validate_pt_bos - validate the page table BOs
 *
 * @adev: amdgpu device pointer
 * @vm: vm providing the BOs
 * @validate: callback to do the validation
 * @param: parameter for the validation callback
 *
 * Validate the page table BOs on command submission if neccessary.
 *
 * Returns:
 * Validation result.
 */
int amdgpu_vm_validate_pt_bos(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			      int (*validate)(void *p, struct amdgpu_bo *bo),
			      void *param)
{
	struct amdgpu_vm_bo_base *bo_base, *tmp;
	int r;

	vm->bulk_moveable &= list_empty(&vm->evicted);

	list_for_each_entry_safe(bo_base, tmp, &vm->evicted, vm_status) {
		struct amdgpu_bo *bo = bo_base->bo;

		r = validate(param, bo);
		if (r)
			return r;

		if (bo->tbo.type != ttm_bo_type_kernel) {
			amdgpu_vm_bo_moved(bo_base);
		} else {
			vm->update_funcs->map_table(bo);
			amdgpu_vm_bo_relocated(bo_base);
		}
	}

	amdgpu_vm_eviction_lock(vm);
	vm->evicting = false;
	amdgpu_vm_eviction_unlock(vm);

	return 0;
}

/**
 * amdgpu_vm_ready - check VM is ready for updates
 *
 * @vm: VM to check
 *
 * Check if all VM PDs/PTs are ready for updates
 *
 * Returns:
 * True if VM is not evicting.
 */
bool amdgpu_vm_ready(struct amdgpu_vm *vm)
{
	bool ret;

	amdgpu_vm_eviction_lock(vm);
	ret = !vm->evicting;
	amdgpu_vm_eviction_unlock(vm);

	return ret && list_empty(&vm->evicted);
}

/**
 * amdgpu_vm_clear_bo - initially clear the PDs/PTs
 *
 * @adev: amdgpu_device pointer
 * @vm: VM to clear BO from
 * @bo: BO to clear
 * @immediate: use an immediate update
 *
 * Root PD needs to be reserved when calling this.
 *
 * Returns:
 * 0 on success, errno otherwise.
 */
static int amdgpu_vm_clear_bo(struct amdgpu_device *adev,
			      struct amdgpu_vm *vm,
			      struct amdgpu_bo *bo,
			      bool immediate)
{
	struct ttm_operation_ctx ctx = { true, false };
	unsigned level = adev->vm_manager.root_level;
	struct amdgpu_vm_update_params params;
	struct amdgpu_bo *ancestor = bo;
	unsigned entries, ats_entries;
	uint64_t addr;
	int r;

	/* Figure out our place in the hierarchy */
	if (ancestor->parent) {
		++level;
		while (ancestor->parent->parent) {
			++level;
			ancestor = ancestor->parent;
		}
	}

	entries = amdgpu_bo_size(bo) / 8;
	if (!vm->pte_support_ats) {
		ats_entries = 0;

	} else if (!bo->parent) {
		ats_entries = amdgpu_vm_num_ats_entries(adev);
		ats_entries = min(ats_entries, entries);
		entries -= ats_entries;

	} else {
		struct amdgpu_vm_pt *pt;

		pt = container_of(ancestor->vm_bo, struct amdgpu_vm_pt, base);
		ats_entries = amdgpu_vm_num_ats_entries(adev);
		if ((pt - vm->root.entries) >= ats_entries) {
			ats_entries = 0;
		} else {
			ats_entries = entries;
			entries = 0;
		}
	}

	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (r)
		return r;

	if (bo->shadow) {
		r = ttm_bo_validate(&bo->shadow->tbo, &bo->shadow->placement,
				    &ctx);
		if (r)
			return r;
	}

	r = vm->update_funcs->map_table(bo);
	if (r)
		return r;

	memset(&params, 0, sizeof(params));
	params.adev = adev;
	params.vm = vm;
	params.immediate = immediate;

	r = vm->update_funcs->prepare(&params, NULL, AMDGPU_SYNC_EXPLICIT);
	if (r)
		return r;

	addr = 0;
	if (ats_entries) {
		uint64_t value = 0, flags;

		flags = AMDGPU_PTE_DEFAULT_ATC;
		if (level != AMDGPU_VM_PTB) {
			/* Handle leaf PDEs as PTEs */
			flags |= AMDGPU_PDE_PTE;
			amdgpu_gmc_get_vm_pde(adev, level, &value, &flags);
		}

		r = vm->update_funcs->update(&params, bo, addr, 0, ats_entries,
					     value, flags);
		if (r)
			return r;

		addr += ats_entries * 8;
	}

	if (entries) {
		uint64_t value = 0, flags = 0;

		if (adev->asic_type >= CHIP_VEGA10) {
			if (level != AMDGPU_VM_PTB) {
				/* Handle leaf PDEs as PTEs */
				flags |= AMDGPU_PDE_PTE;
				amdgpu_gmc_get_vm_pde(adev, level,
						      &value, &flags);
			} else {
				/* Workaround for fault priority problem on GMC9 */
				flags = AMDGPU_PTE_EXECUTABLE;
			}
		}

		r = vm->update_funcs->update(&params, bo, addr, 0, entries,
					     value, flags);
		if (r)
			return r;
	}

	return vm->update_funcs->commit(&params, NULL);
}

/**
 * amdgpu_vm_bo_param - fill in parameters for PD/PT allocation
 *
 * @adev: amdgpu_device pointer
 * @vm: requesting vm
 * @level: the page table level
 * @immediate: use a immediate update
 * @bp: resulting BO allocation parameters
 */
static void amdgpu_vm_bo_param(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			       int level, bool immediate,
			       struct amdgpu_bo_param *bp)
{
	memset(bp, 0, sizeof(*bp));

	bp->size = amdgpu_vm_bo_size(adev, level);
	bp->byte_align = AMDGPU_GPU_PAGE_SIZE;
	bp->domain = AMDGPU_GEM_DOMAIN_VRAM;
	bp->domain = amdgpu_bo_get_preferred_pin_domain(adev, bp->domain);
	bp->flags = AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS |
		AMDGPU_GEM_CREATE_CPU_GTT_USWC;
	if (vm->use_cpu_for_update)
		bp->flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
	else if (!vm->root.base.bo || vm->root.base.bo->shadow)
		bp->flags |= AMDGPU_GEM_CREATE_SHADOW;
	bp->type = ttm_bo_type_kernel;
	bp->no_wait_gpu = immediate;
	if (vm->root.base.bo)
		bp->resv = vm->root.base.bo->tbo.base.resv;
}

/**
 * amdgpu_vm_alloc_pts - Allocate a specific page table
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
static int amdgpu_vm_alloc_pts(struct amdgpu_device *adev,
			       struct amdgpu_vm *vm,
			       struct amdgpu_vm_pt_cursor *cursor,
			       bool immediate)
{
	struct amdgpu_vm_pt *entry = cursor->entry;
	struct amdgpu_bo_param bp;
	struct amdgpu_bo *pt;
	int r;

	if (cursor->level < AMDGPU_VM_PTB && !entry->entries) {
		unsigned num_entries;

		num_entries = amdgpu_vm_num_entries(adev, cursor->level);
		entry->entries = kvmalloc_array(num_entries,
						sizeof(*entry->entries),
						GFP_KERNEL | __GFP_ZERO);
		if (!entry->entries)
			return -ENOMEM;
	}

	if (entry->base.bo)
		return 0;

	amdgpu_vm_bo_param(adev, vm, cursor->level, immediate, &bp);

	r = amdgpu_bo_create(adev, &bp, &pt);
	if (r)
		return r;

	/* Keep a reference to the root directory to avoid
	 * freeing them up in the wrong order.
	 */
	pt->parent = amdgpu_bo_ref(cursor->parent->base.bo);
	amdgpu_vm_bo_base_init(&entry->base, vm, pt);

	r = amdgpu_vm_clear_bo(adev, vm, pt, immediate);
	if (r)
		goto error_free_pt;

	return 0;

error_free_pt:
	amdgpu_bo_unref(&pt->shadow);
	amdgpu_bo_unref(&pt);
	return r;
}

/**
 * amdgpu_vm_free_table - fre one PD/PT
 *
 * @entry: PDE to free
 */
static void amdgpu_vm_free_table(struct amdgpu_vm_pt *entry)
{
	if (entry->base.bo) {
		entry->base.bo->vm_bo = NULL;
		list_del(&entry->base.vm_status);
		amdgpu_bo_unref(&entry->base.bo->shadow);
		amdgpu_bo_unref(&entry->base.bo);
	}
	kvfree(entry->entries);
	entry->entries = NULL;
}

/**
 * amdgpu_vm_free_pts - free PD/PT levels
 *
 * @adev: amdgpu device structure
 * @vm: amdgpu vm structure
 * @start: optional cursor where to start freeing PDs/PTs
 *
 * Free the page directory or page table level and all sub levels.
 */
static void amdgpu_vm_free_pts(struct amdgpu_device *adev,
			       struct amdgpu_vm *vm,
			       struct amdgpu_vm_pt_cursor *start)
{
	struct amdgpu_vm_pt_cursor cursor;
	struct amdgpu_vm_pt *entry;

	vm->bulk_moveable = false;

	for_each_amdgpu_vm_pt_dfs_safe(adev, vm, start, cursor, entry)
		amdgpu_vm_free_table(entry);

	if (start)
		amdgpu_vm_free_table(start->entry);
}

/**
 * amdgpu_vm_check_compute_bug - check whether asic has compute vm bug
 *
 * @adev: amdgpu_device pointer
 */
void amdgpu_vm_check_compute_bug(struct amdgpu_device *adev)
{
	const struct amdgpu_ip_block *ip_block;
	bool has_compute_vm_bug;
	struct amdgpu_ring *ring;
	int i;

	has_compute_vm_bug = false;

	ip_block = amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_GFX);
	if (ip_block) {
		/* Compute has a VM bug for GFX version < 7.
		   Compute has a VM bug for GFX 8 MEC firmware version < 673.*/
		if (ip_block->version->major <= 7)
			has_compute_vm_bug = true;
		else if (ip_block->version->major == 8)
			if (adev->gfx.mec_fw_version < 673)
				has_compute_vm_bug = true;
	}

	for (i = 0; i < adev->num_rings; i++) {
		ring = adev->rings[i];
		if (ring->funcs->type == AMDGPU_RING_TYPE_COMPUTE)
			/* only compute rings */
			ring->has_compute_vm_bug = has_compute_vm_bug;
		else
			ring->has_compute_vm_bug = false;
	}
}

/**
 * amdgpu_vm_need_pipeline_sync - Check if pipe sync is needed for job.
 *
 * @ring: ring on which the job will be submitted
 * @job: job to submit
 *
 * Returns:
 * True if sync is needed.
 */
bool amdgpu_vm_need_pipeline_sync(struct amdgpu_ring *ring,
				  struct amdgpu_job *job)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned vmhub = ring->funcs->vmhub;
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];
	struct amdgpu_vmid *id;
	bool gds_switch_needed;
	bool vm_flush_needed = job->vm_needs_flush || ring->has_compute_vm_bug;

	if (job->vmid == 0)
		return false;
	id = &id_mgr->ids[job->vmid];
	gds_switch_needed = ring->funcs->emit_gds_switch && (
		id->gds_base != job->gds_base ||
		id->gds_size != job->gds_size ||
		id->gws_base != job->gws_base ||
		id->gws_size != job->gws_size ||
		id->oa_base != job->oa_base ||
		id->oa_size != job->oa_size);

	if (amdgpu_vmid_had_gpu_reset(adev, id))
		return true;

	return vm_flush_needed || gds_switch_needed;
}

/**
 * amdgpu_vm_flush - hardware flush the vm
 *
 * @ring: ring to use for flush
 * @job:  related job
 * @need_pipe_sync: is pipe sync needed
 *
 * Emit a VM flush when it is necessary.
 *
 * Returns:
 * 0 on success, errno otherwise.
 */
int amdgpu_vm_flush(struct amdgpu_ring *ring, struct amdgpu_job *job,
		    bool need_pipe_sync)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned vmhub = ring->funcs->vmhub;
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_manager.id_mgr[vmhub];
	struct amdgpu_vmid *id = &id_mgr->ids[job->vmid];
	bool gds_switch_needed = ring->funcs->emit_gds_switch && (
		id->gds_base != job->gds_base ||
		id->gds_size != job->gds_size ||
		id->gws_base != job->gws_base ||
		id->gws_size != job->gws_size ||
		id->oa_base != job->oa_base ||
		id->oa_size != job->oa_size);
	bool vm_flush_needed = job->vm_needs_flush;
	struct dma_fence *fence = NULL;
	bool pasid_mapping_needed = false;
	unsigned patch_offset = 0;
	bool update_spm_vmid_needed = (job->vm && (job->vm->reserved_vmid[vmhub] != NULL));
	int r;

	if (update_spm_vmid_needed && adev->gfx.rlc.funcs->update_spm_vmid)
		adev->gfx.rlc.funcs->update_spm_vmid(adev, job->vmid);

	if (amdgpu_vmid_had_gpu_reset(adev, id)) {
		gds_switch_needed = true;
		vm_flush_needed = true;
		pasid_mapping_needed = true;
	}

	mutex_lock(&id_mgr->lock);
	if (id->pasid != job->pasid || !id->pasid_mapping ||
	    !dma_fence_is_signaled(id->pasid_mapping))
		pasid_mapping_needed = true;
	mutex_unlock(&id_mgr->lock);

	gds_switch_needed &= !!ring->funcs->emit_gds_switch;
	vm_flush_needed &= !!ring->funcs->emit_vm_flush  &&
			job->vm_pd_addr != AMDGPU_BO_INVALID_OFFSET;
	pasid_mapping_needed &= adev->gmc.gmc_funcs->emit_pasid_mapping &&
		ring->funcs->emit_wreg;

	if (!vm_flush_needed && !gds_switch_needed && !need_pipe_sync)
		return 0;

	if (ring->funcs->init_cond_exec)
		patch_offset = amdgpu_ring_init_cond_exec(ring);

	if (need_pipe_sync)
		amdgpu_ring_emit_pipeline_sync(ring);

	if (vm_flush_needed) {
		trace_amdgpu_vm_flush(ring, job->vmid, job->vm_pd_addr);
		amdgpu_ring_emit_vm_flush(ring, job->vmid, job->vm_pd_addr);
	}

	if (pasid_mapping_needed)
		amdgpu_gmc_emit_pasid_mapping(ring, job->vmid, job->pasid);

	if (vm_flush_needed || pasid_mapping_needed) {
		r = amdgpu_fence_emit(ring, &fence, 0);
		if (r)
			return r;
	}

	if (vm_flush_needed) {
		mutex_lock(&id_mgr->lock);
		dma_fence_put(id->last_flush);
		id->last_flush = dma_fence_get(fence);
		id->current_gpu_reset_count =
			atomic_read(&adev->gpu_reset_counter);
		mutex_unlock(&id_mgr->lock);
	}

	if (pasid_mapping_needed) {
		mutex_lock(&id_mgr->lock);
		id->pasid = job->pasid;
		dma_fence_put(id->pasid_mapping);
		id->pasid_mapping = dma_fence_get(fence);
		mutex_unlock(&id_mgr->lock);
	}
	dma_fence_put(fence);

	if (ring->funcs->emit_gds_switch && gds_switch_needed) {
		id->gds_base = job->gds_base;
		id->gds_size = job->gds_size;
		id->gws_base = job->gws_base;
		id->gws_size = job->gws_size;
		id->oa_base = job->oa_base;
		id->oa_size = job->oa_size;
		amdgpu_ring_emit_gds_switch(ring, job->vmid, job->gds_base,
					    job->gds_size, job->gws_base,
					    job->gws_size, job->oa_base,
					    job->oa_size);
	}

	if (ring->funcs->patch_cond_exec)
		amdgpu_ring_patch_cond_exec(ring, patch_offset);

	/* the double SWITCH_BUFFER here *cannot* be skipped by COND_EXEC */
	if (ring->funcs->emit_switch_buffer) {
		amdgpu_ring_emit_switch_buffer(ring);
		amdgpu_ring_emit_switch_buffer(ring);
	}
	return 0;
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
 *
 * Returns:
 * Found bo_va or NULL.
 */
struct amdgpu_bo_va *amdgpu_vm_bo_find(struct amdgpu_vm *vm,
				       struct amdgpu_bo *bo)
{
	struct amdgpu_vm_bo_base *base;

	for (base = bo->vm_bo; base; base = base->next) {
		if (base->vm != vm)
			continue;

		return container_of(base, struct amdgpu_bo_va, base);
	}
	return NULL;
}

/**
 * amdgpu_vm_map_gart - Resolve gart mapping of addr
 *
 * @pages_addr: optional DMA address to use for lookup
 * @addr: the unmapped addr
 *
 * Look up the physical address of the page that the pte resolves
 * to.
 *
 * Returns:
 * The pointer for the page table entry.
 */
uint64_t amdgpu_vm_map_gart(const dma_addr_t *pages_addr, uint64_t addr)
{
	uint64_t result;

	/* page table offset */
	result = pages_addr[addr >> PAGE_SHIFT];

	/* in case cpu page size != gpu page size*/
	result |= addr & (~PAGE_MASK);

	result &= 0xFFFFFFFFFFFFF000ULL;

	return result;
}

/**
 * amdgpu_vm_update_pde - update a single level in the hierarchy
 *
 * @params: parameters for the update
 * @vm: requested vm
 * @entry: entry to update
 *
 * Makes sure the requested entry in parent is up to date.
 */
static int amdgpu_vm_update_pde(struct amdgpu_vm_update_params *params,
				struct amdgpu_vm *vm,
				struct amdgpu_vm_pt *entry)
{
	struct amdgpu_vm_pt *parent = amdgpu_vm_pt_parent(entry);
	struct amdgpu_bo *bo = parent->base.bo, *pbo;
	uint64_t pde, pt, flags;
	unsigned level;

	for (level = 0, pbo = bo->parent; pbo; ++level)
		pbo = pbo->parent;

	level += params->adev->vm_manager.root_level;
	amdgpu_gmc_get_pde_for_bo(entry->base.bo, level, &pt, &flags);
	pde = (entry - parent->entries) * 8;
	return vm->update_funcs->update(params, bo, pde, pt, 1, 0, flags);
}

/**
 * amdgpu_vm_invalidate_pds - mark all PDs as invalid
 *
 * @adev: amdgpu_device pointer
 * @vm: related vm
 *
 * Mark all PD level as invalid after an error.
 */
static void amdgpu_vm_invalidate_pds(struct amdgpu_device *adev,
				     struct amdgpu_vm *vm)
{
	struct amdgpu_vm_pt_cursor cursor;
	struct amdgpu_vm_pt *entry;

	for_each_amdgpu_vm_pt_dfs_safe(adev, vm, NULL, cursor, entry)
		if (entry->base.bo && !entry->base.moved)
			amdgpu_vm_bo_relocated(&entry->base);
}

/**
 * amdgpu_vm_update_pdes - make sure that all directories are valid
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @immediate: submit immediately to the paging queue
 *
 * Makes sure all directories are up to date.
 *
 * Returns:
 * 0 for success, error for failure.
 */
int amdgpu_vm_update_pdes(struct amdgpu_device *adev,
			  struct amdgpu_vm *vm, bool immediate)
{
	struct amdgpu_vm_update_params params;
	int r;

	if (list_empty(&vm->relocated))
		return 0;

	memset(&params, 0, sizeof(params));
	params.adev = adev;
	params.vm = vm;
	params.immediate = immediate;

	r = vm->update_funcs->prepare(&params, NULL, AMDGPU_SYNC_EXPLICIT);
	if (r)
		return r;

	while (!list_empty(&vm->relocated)) {
		struct amdgpu_vm_pt *entry;

		entry = list_first_entry(&vm->relocated, struct amdgpu_vm_pt,
					 base.vm_status);
		amdgpu_vm_bo_idle(&entry->base);

		r = amdgpu_vm_update_pde(&params, vm, entry);
		if (r)
			goto error;
	}

	r = vm->update_funcs->commit(&params, &vm->last_update);
	if (r)
		goto error;
	return 0;

error:
	amdgpu_vm_invalidate_pds(adev, vm);
	return r;
}

/*
 * amdgpu_vm_update_flags - figure out flags for PTE updates
 *
 * Make sure to set the right flags for the PTEs at the desired level.
 */
static void amdgpu_vm_update_flags(struct amdgpu_vm_update_params *params,
				   struct amdgpu_bo *bo, unsigned level,
				   uint64_t pe, uint64_t addr,
				   unsigned count, uint32_t incr,
				   uint64_t flags)

{
	if (level != AMDGPU_VM_PTB) {
		flags |= AMDGPU_PDE_PTE;
		amdgpu_gmc_get_vm_pde(params->adev, level, &addr, &flags);

	} else if (params->adev->asic_type >= CHIP_VEGA10 &&
		   !(flags & AMDGPU_PTE_VALID) &&
		   !(flags & AMDGPU_PTE_PRT)) {

		/* Workaround for fault priority problem on GMC9 */
		flags |= AMDGPU_PTE_EXECUTABLE;
	}

	params->vm->update_funcs->update(params, bo, pe, addr, count, incr,
					 flags);
}

/**
 * amdgpu_vm_fragment - get fragment for PTEs
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
static void amdgpu_vm_fragment(struct amdgpu_vm_update_params *params,
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
	unsigned max_frag;

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
	*frag = min((unsigned)ffs(start) - 1, (unsigned)fls64(end - start) - 1);
	if (*frag >= max_frag) {
		*frag = max_frag;
		*frag_end = end & ~((1ULL << max_frag) - 1);
	} else {
		*frag_end = start + (1 << *frag);
	}
}

/**
 * amdgpu_vm_update_ptes - make sure that page tables are valid
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
static int amdgpu_vm_update_ptes(struct amdgpu_vm_update_params *params,
				 uint64_t start, uint64_t end,
				 uint64_t dst, uint64_t flags)
{
	struct amdgpu_device *adev = params->adev;
	struct amdgpu_vm_pt_cursor cursor;
	uint64_t frag_start = start, frag_end;
	unsigned int frag;
	int r;

	/* figure out the initial fragment */
	amdgpu_vm_fragment(params, frag_start, end, flags, &frag, &frag_end);

	/* walk over the address space and update the PTs */
	amdgpu_vm_pt_start(adev, params->vm, start, &cursor);
	while (cursor.pfn < end) {
		unsigned shift, parent_shift, mask;
		uint64_t incr, entry_end, pe_start;
		struct amdgpu_bo *pt;

		if (!params->unlocked) {
			/* make sure that the page tables covering the
			 * address range are actually allocated
			 */
			r = amdgpu_vm_alloc_pts(params->adev, params->vm,
						&cursor, params->immediate);
			if (r)
				return r;
		}

		shift = amdgpu_vm_level_shift(adev, cursor.level);
		parent_shift = amdgpu_vm_level_shift(adev, cursor.level - 1);
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

		pt = cursor.entry->base.bo;
		if (!pt) {
			/* We need all PDs and PTs for mapping something, */
			if (flags & AMDGPU_PTE_VALID)
				return -ENOENT;

			/* but unmapping something can happen at a higher
			 * level.
			 */
			if (!amdgpu_vm_pt_ancestor(&cursor))
				return -EINVAL;

			pt = cursor.entry->base.bo;
			shift = parent_shift;
			frag_end = max(frag_end, ALIGN(frag_start + 1,
				   1ULL << shift));
		}

		/* Looks good so far, calculate parameters for the update */
		incr = (uint64_t)AMDGPU_GPU_PAGE_SIZE << shift;
		mask = amdgpu_vm_entries_mask(adev, cursor.level);
		pe_start = ((cursor.pfn >> shift) & mask) * 8;
		entry_end = ((uint64_t)mask + 1) << shift;
		entry_end += cursor.pfn & ~(entry_end - 1);
		entry_end = min(entry_end, end);

		do {
			struct amdgpu_vm *vm = params->vm;
			uint64_t upd_end = min(entry_end, frag_end);
			unsigned nptes = (upd_end - frag_start) >> shift;
			uint64_t upd_flags = flags | AMDGPU_PTE_FRAG(frag);

			/* This can happen when we set higher level PDs to
			 * silent to stop fault floods.
			 */
			nptes = max(nptes, 1u);

			trace_amdgpu_vm_update_ptes(params, frag_start, upd_end,
						    nptes, dst, incr, upd_flags,
						    vm->task_info.pid,
						    vm->immediate.fence_context);
			amdgpu_vm_update_flags(params, pt, cursor.level,
					       pe_start, dst, nptes, incr,
					       upd_flags);

			pe_start += nptes * 8;
			dst += nptes * incr;

			frag_start = upd_end;
			if (frag_start >= frag_end) {
				/* figure out the next fragment */
				amdgpu_vm_fragment(params, frag_start, end,
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
				amdgpu_vm_free_pts(adev, params->vm, &cursor);
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
 * amdgpu_vm_bo_update_mapping - update a mapping in the vm page table
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @immediate: immediate submission in a page fault
 * @unlocked: unlocked invalidation during MM callback
 * @resv: fences we need to sync to
 * @start: start of mapped range
 * @last: last mapped entry
 * @flags: flags for the entries
 * @addr: addr to set the area to
 * @pages_addr: DMA addresses to use for mapping
 * @fence: optional resulting fence
 *
 * Fill in the page table entries between @start and @last.
 *
 * Returns:
 * 0 for success, -EINVAL for failure.
 */
static int amdgpu_vm_bo_update_mapping(struct amdgpu_device *adev,
				       struct amdgpu_vm *vm, bool immediate,
				       bool unlocked, struct dma_resv *resv,
				       uint64_t start, uint64_t last,
				       uint64_t flags, uint64_t addr,
				       dma_addr_t *pages_addr,
				       struct dma_fence **fence)
{
	struct amdgpu_vm_update_params params;
	enum amdgpu_sync_mode sync_mode;
	int r;

	memset(&params, 0, sizeof(params));
	params.adev = adev;
	params.vm = vm;
	params.immediate = immediate;
	params.pages_addr = pages_addr;
	params.unlocked = unlocked;

	/* Implicitly sync to command submissions in the same VM before
	 * unmapping. Sync to moving fences before mapping.
	 */
	if (!(flags & AMDGPU_PTE_VALID))
		sync_mode = AMDGPU_SYNC_EQ_OWNER;
	else
		sync_mode = AMDGPU_SYNC_EXPLICIT;

	amdgpu_vm_eviction_lock(vm);
	if (vm->evicting) {
		r = -EBUSY;
		goto error_unlock;
	}

	if (!unlocked && !dma_fence_is_signaled(vm->last_unlocked)) {
		struct dma_fence *tmp = dma_fence_get_stub();

		amdgpu_bo_fence(vm->root.base.bo, vm->last_unlocked, true);
		swap(vm->last_unlocked, tmp);
		dma_fence_put(tmp);
	}

	r = vm->update_funcs->prepare(&params, resv, sync_mode);
	if (r)
		goto error_unlock;

	r = amdgpu_vm_update_ptes(&params, start, last + 1, addr, flags);
	if (r)
		goto error_unlock;

	r = vm->update_funcs->commit(&params, fence);

error_unlock:
	amdgpu_vm_eviction_unlock(vm);
	return r;
}

/**
 * amdgpu_vm_bo_split_mapping - split a mapping into smaller chunks
 *
 * @adev: amdgpu_device pointer
 * @resv: fences we need to sync to
 * @pages_addr: DMA addresses to use for mapping
 * @vm: requested vm
 * @mapping: mapped range and flags to use for the update
 * @flags: HW flags for the mapping
 * @bo_adev: amdgpu_device pointer that bo actually been allocated
 * @nodes: array of drm_mm_nodes with the MC addresses
 * @fence: optional resulting fence
 *
 * Split the mapping into smaller chunks so that each update fits
 * into a SDMA IB.
 *
 * Returns:
 * 0 for success, -EINVAL for failure.
 */
static int amdgpu_vm_bo_split_mapping(struct amdgpu_device *adev,
				      struct dma_resv *resv,
				      dma_addr_t *pages_addr,
				      struct amdgpu_vm *vm,
				      struct amdgpu_bo_va_mapping *mapping,
				      uint64_t flags,
				      struct amdgpu_device *bo_adev,
				      struct drm_mm_node *nodes,
				      struct dma_fence **fence)
{
	unsigned min_linear_pages = 1 << adev->vm_manager.fragment_size;
	uint64_t pfn, start = mapping->start;
	int r;

	/* normally,bo_va->flags only contians READABLE and WIRTEABLE bit go here
	 * but in case of something, we filter the flags in first place
	 */
	if (!(mapping->flags & AMDGPU_PTE_READABLE))
		flags &= ~AMDGPU_PTE_READABLE;
	if (!(mapping->flags & AMDGPU_PTE_WRITEABLE))
		flags &= ~AMDGPU_PTE_WRITEABLE;

	/* Apply ASIC specific mapping flags */
	amdgpu_gmc_get_vm_pte(adev, mapping, &flags);

	trace_amdgpu_vm_bo_update(mapping);

	pfn = mapping->offset >> PAGE_SHIFT;
	if (nodes) {
		while (pfn >= nodes->size) {
			pfn -= nodes->size;
			++nodes;
		}
	}

	do {
		dma_addr_t *dma_addr = NULL;
		uint64_t max_entries;
		uint64_t addr, last;

		max_entries = mapping->last - start + 1;
		if (nodes) {
			addr = nodes->start << PAGE_SHIFT;
			max_entries = min((nodes->size - pfn) *
				AMDGPU_GPU_PAGES_IN_CPU_PAGE, max_entries);
		} else {
			addr = 0;
		}

		if (pages_addr) {
			uint64_t count;

			for (count = 1;
			     count < max_entries / AMDGPU_GPU_PAGES_IN_CPU_PAGE;
			     ++count) {
				uint64_t idx = pfn + count;

				if (pages_addr[idx] !=
				    (pages_addr[idx - 1] + PAGE_SIZE))
					break;
			}

			if (count < min_linear_pages) {
				addr = pfn << PAGE_SHIFT;
				dma_addr = pages_addr;
			} else {
				addr = pages_addr[pfn];
				max_entries = count *
					AMDGPU_GPU_PAGES_IN_CPU_PAGE;
			}

		} else if (flags & (AMDGPU_PTE_VALID | AMDGPU_PTE_PRT)) {
			addr += bo_adev->vm_manager.vram_base_offset;
			addr += pfn << PAGE_SHIFT;
		}

		last = start + max_entries - 1;
		r = amdgpu_vm_bo_update_mapping(adev, vm, false, false, resv,
						start, last, flags, addr,
						dma_addr, fence);
		if (r)
			return r;

		pfn += (last - start + 1) / AMDGPU_GPU_PAGES_IN_CPU_PAGE;
		if (nodes && nodes->size == pfn) {
			pfn = 0;
			++nodes;
		}
		start = last + 1;

	} while (unlikely(start != mapping->last + 1));

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
 *
 * Returns:
 * 0 for success, -EINVAL for failure.
 */
int amdgpu_vm_bo_update(struct amdgpu_device *adev, struct amdgpu_bo_va *bo_va,
			bool clear)
{
	struct amdgpu_bo *bo = bo_va->base.bo;
	struct amdgpu_vm *vm = bo_va->base.vm;
	struct amdgpu_bo_va_mapping *mapping;
	dma_addr_t *pages_addr = NULL;
	struct ttm_resource *mem;
	struct drm_mm_node *nodes;
	struct dma_fence **last_update;
	struct dma_resv *resv;
	uint64_t flags;
	struct amdgpu_device *bo_adev = adev;
	int r;

	if (clear || !bo) {
		mem = NULL;
		nodes = NULL;
		resv = vm->root.base.bo->tbo.base.resv;
	} else {
		struct drm_gem_object *obj = &bo->tbo.base;
		struct ttm_dma_tt *ttm;

		resv = bo->tbo.base.resv;
		if (obj->import_attach && bo_va->is_xgmi) {
			struct dma_buf *dma_buf = obj->import_attach->dmabuf;
			struct drm_gem_object *gobj = dma_buf->priv;
			struct amdgpu_bo *abo = gem_to_amdgpu_bo(gobj);

			if (abo->tbo.mem.mem_type == TTM_PL_VRAM)
				bo = gem_to_amdgpu_bo(gobj);
		}
		mem = &bo->tbo.mem;
		nodes = mem->mm_node;
		if (mem->mem_type == TTM_PL_TT) {
			ttm = container_of(bo->tbo.ttm, struct ttm_dma_tt, ttm);
			pages_addr = ttm->dma_address;
		}
	}

	if (bo) {
		flags = amdgpu_ttm_tt_pte_flags(adev, bo->tbo.ttm, mem);

		if (amdgpu_bo_encrypted(bo))
			flags |= AMDGPU_PTE_TMZ;

		bo_adev = amdgpu_ttm_adev(bo->tbo.bdev);
	} else {
		flags = 0x0;
	}

	if (clear || (bo && bo->tbo.base.resv ==
		      vm->root.base.bo->tbo.base.resv))
		last_update = &vm->last_update;
	else
		last_update = &bo_va->last_pt_update;

	if (!clear && bo_va->base.moved) {
		bo_va->base.moved = false;
		list_splice_init(&bo_va->valids, &bo_va->invalids);

	} else if (bo_va->cleared != clear) {
		list_splice_init(&bo_va->valids, &bo_va->invalids);
	}

	list_for_each_entry(mapping, &bo_va->invalids, list) {
		r = amdgpu_vm_bo_split_mapping(adev, resv, pages_addr, vm,
					       mapping, flags, bo_adev, nodes,
					       last_update);
		if (r)
			return r;
	}

	/* If the BO is not in its preferred location add it back to
	 * the evicted list so that it gets validated again on the
	 * next command submission.
	 */
	if (bo && bo->tbo.base.resv == vm->root.base.bo->tbo.base.resv) {
		uint32_t mem_type = bo->tbo.mem.mem_type;

		if (!(bo->preferred_domains &
		      amdgpu_mem_type_to_domain(mem_type)))
			amdgpu_vm_bo_evicted(&bo_va->base);
		else
			amdgpu_vm_bo_idle(&bo_va->base);
	} else {
		amdgpu_vm_bo_done(&bo_va->base);
	}

	list_splice_init(&bo_va->invalids, &bo_va->valids);
	bo_va->cleared = clear;

	if (trace_amdgpu_vm_bo_mapping_enabled()) {
		list_for_each_entry(mapping, &bo_va->valids, list)
			trace_amdgpu_vm_bo_mapping(mapping);
	}

	return 0;
}

/**
 * amdgpu_vm_update_prt_state - update the global PRT state
 *
 * @adev: amdgpu_device pointer
 */
static void amdgpu_vm_update_prt_state(struct amdgpu_device *adev)
{
	unsigned long flags;
	bool enable;

	spin_lock_irqsave(&adev->vm_manager.prt_lock, flags);
	enable = !!atomic_read(&adev->vm_manager.num_prt_users);
	adev->gmc.gmc_funcs->set_prt(adev, enable);
	spin_unlock_irqrestore(&adev->vm_manager.prt_lock, flags);
}

/**
 * amdgpu_vm_prt_get - add a PRT user
 *
 * @adev: amdgpu_device pointer
 */
static void amdgpu_vm_prt_get(struct amdgpu_device *adev)
{
	if (!adev->gmc.gmc_funcs->set_prt)
		return;

	if (atomic_inc_return(&adev->vm_manager.num_prt_users) == 1)
		amdgpu_vm_update_prt_state(adev);
}

/**
 * amdgpu_vm_prt_put - drop a PRT user
 *
 * @adev: amdgpu_device pointer
 */
static void amdgpu_vm_prt_put(struct amdgpu_device *adev)
{
	if (atomic_dec_return(&adev->vm_manager.num_prt_users) == 0)
		amdgpu_vm_update_prt_state(adev);
}

/**
 * amdgpu_vm_prt_cb - callback for updating the PRT status
 *
 * @fence: fence for the callback
 * @_cb: the callback function
 */
static void amdgpu_vm_prt_cb(struct dma_fence *fence, struct dma_fence_cb *_cb)
{
	struct amdgpu_prt_cb *cb = container_of(_cb, struct amdgpu_prt_cb, cb);

	amdgpu_vm_prt_put(cb->adev);
	kfree(cb);
}

/**
 * amdgpu_vm_add_prt_cb - add callback for updating the PRT status
 *
 * @adev: amdgpu_device pointer
 * @fence: fence for the callback
 */
static void amdgpu_vm_add_prt_cb(struct amdgpu_device *adev,
				 struct dma_fence *fence)
{
	struct amdgpu_prt_cb *cb;

	if (!adev->gmc.gmc_funcs->set_prt)
		return;

	cb = kmalloc(sizeof(struct amdgpu_prt_cb), GFP_KERNEL);
	if (!cb) {
		/* Last resort when we are OOM */
		if (fence)
			dma_fence_wait(fence, false);

		amdgpu_vm_prt_put(adev);
	} else {
		cb->adev = adev;
		if (!fence || dma_fence_add_callback(fence, &cb->cb,
						     amdgpu_vm_prt_cb))
			amdgpu_vm_prt_cb(fence, &cb->cb);
	}
}

/**
 * amdgpu_vm_free_mapping - free a mapping
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @mapping: mapping to be freed
 * @fence: fence of the unmap operation
 *
 * Free a mapping and make sure we decrease the PRT usage count if applicable.
 */
static void amdgpu_vm_free_mapping(struct amdgpu_device *adev,
				   struct amdgpu_vm *vm,
				   struct amdgpu_bo_va_mapping *mapping,
				   struct dma_fence *fence)
{
	if (mapping->flags & AMDGPU_PTE_PRT)
		amdgpu_vm_add_prt_cb(adev, fence);
	kfree(mapping);
}

/**
 * amdgpu_vm_prt_fini - finish all prt mappings
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 *
 * Register a cleanup callback to disable PRT support after VM dies.
 */
static void amdgpu_vm_prt_fini(struct amdgpu_device *adev, struct amdgpu_vm *vm)
{
	struct dma_resv *resv = vm->root.base.bo->tbo.base.resv;
	struct dma_fence *excl, **shared;
	unsigned i, shared_count;
	int r;

	r = dma_resv_get_fences_rcu(resv, &excl,
					      &shared_count, &shared);
	if (r) {
		/* Not enough memory to grab the fence list, as last resort
		 * block for all the fences to complete.
		 */
		dma_resv_wait_timeout_rcu(resv, true, false,
						    MAX_SCHEDULE_TIMEOUT);
		return;
	}

	/* Add a callback for each fence in the reservation object */
	amdgpu_vm_prt_get(adev);
	amdgpu_vm_add_prt_cb(adev, excl);

	for (i = 0; i < shared_count; ++i) {
		amdgpu_vm_prt_get(adev);
		amdgpu_vm_add_prt_cb(adev, shared[i]);
	}

	kfree(shared);
}

/**
 * amdgpu_vm_clear_freed - clear freed BOs in the PT
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @fence: optional resulting fence (unchanged if no work needed to be done
 * or if an error occurred)
 *
 * Make sure all freed BOs are cleared in the PT.
 * PTs have to be reserved and mutex must be locked!
 *
 * Returns:
 * 0 for success.
 *
 */
int amdgpu_vm_clear_freed(struct amdgpu_device *adev,
			  struct amdgpu_vm *vm,
			  struct dma_fence **fence)
{
	struct dma_resv *resv = vm->root.base.bo->tbo.base.resv;
	struct amdgpu_bo_va_mapping *mapping;
	uint64_t init_pte_value = 0;
	struct dma_fence *f = NULL;
	int r;

	while (!list_empty(&vm->freed)) {
		mapping = list_first_entry(&vm->freed,
			struct amdgpu_bo_va_mapping, list);
		list_del(&mapping->list);

		if (vm->pte_support_ats &&
		    mapping->start < AMDGPU_GMC_HOLE_START)
			init_pte_value = AMDGPU_PTE_DEFAULT_ATC;

		r = amdgpu_vm_bo_update_mapping(adev, vm, false, false, resv,
						mapping->start, mapping->last,
						init_pte_value, 0, NULL, &f);
		amdgpu_vm_free_mapping(adev, vm, mapping, f);
		if (r) {
			dma_fence_put(f);
			return r;
		}
	}

	if (fence && f) {
		dma_fence_put(*fence);
		*fence = f;
	} else {
		dma_fence_put(f);
	}

	return 0;

}

/**
 * amdgpu_vm_handle_moved - handle moved BOs in the PT
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 *
 * Make sure all BOs which are moved are updated in the PTs.
 *
 * Returns:
 * 0 for success.
 *
 * PTs have to be reserved!
 */
int amdgpu_vm_handle_moved(struct amdgpu_device *adev,
			   struct amdgpu_vm *vm)
{
	struct amdgpu_bo_va *bo_va, *tmp;
	struct dma_resv *resv;
	bool clear;
	int r;

	list_for_each_entry_safe(bo_va, tmp, &vm->moved, base.vm_status) {
		/* Per VM BOs never need to bo cleared in the page tables */
		r = amdgpu_vm_bo_update(adev, bo_va, false);
		if (r)
			return r;
	}

	spin_lock(&vm->invalidated_lock);
	while (!list_empty(&vm->invalidated)) {
		bo_va = list_first_entry(&vm->invalidated, struct amdgpu_bo_va,
					 base.vm_status);
		resv = bo_va->base.bo->tbo.base.resv;
		spin_unlock(&vm->invalidated_lock);

		/* Try to reserve the BO to avoid clearing its ptes */
		if (!amdgpu_vm_debug && dma_resv_trylock(resv))
			clear = false;
		/* Somebody else is using the BO right now */
		else
			clear = true;

		r = amdgpu_vm_bo_update(adev, bo_va, clear);
		if (r)
			return r;

		if (!clear)
			dma_resv_unlock(resv);
		spin_lock(&vm->invalidated_lock);
	}
	spin_unlock(&vm->invalidated_lock);

	return 0;
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
 *
 * Returns:
 * Newly added bo_va or NULL for failure
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
	amdgpu_vm_bo_base_init(&bo_va->base, vm, bo);

	bo_va->ref_count = 1;
	INIT_LIST_HEAD(&bo_va->valids);
	INIT_LIST_HEAD(&bo_va->invalids);

	if (!bo)
		return bo_va;

	if (amdgpu_dmabuf_is_xgmi_accessible(adev, bo)) {
		bo_va->is_xgmi = true;
		/* Power up XGMI if it can be potentially used */
		amdgpu_xgmi_set_pstate(adev, AMDGPU_XGMI_PSTATE_MAX_VEGA20);
	}

	return bo_va;
}


/**
 * amdgpu_vm_bo_insert_mapping - insert a new mapping
 *
 * @adev: amdgpu_device pointer
 * @bo_va: bo_va to store the address
 * @mapping: the mapping to insert
 *
 * Insert a new mapping into all structures.
 */
static void amdgpu_vm_bo_insert_map(struct amdgpu_device *adev,
				    struct amdgpu_bo_va *bo_va,
				    struct amdgpu_bo_va_mapping *mapping)
{
	struct amdgpu_vm *vm = bo_va->base.vm;
	struct amdgpu_bo *bo = bo_va->base.bo;

	mapping->bo_va = bo_va;
	list_add(&mapping->list, &bo_va->invalids);
	amdgpu_vm_it_insert(mapping, &vm->va);

	if (mapping->flags & AMDGPU_PTE_PRT)
		amdgpu_vm_prt_get(adev);

	if (bo && bo->tbo.base.resv == vm->root.base.bo->tbo.base.resv &&
	    !bo_va->base.moved) {
		list_move(&bo_va->base.vm_status, &vm->moved);
	}
	trace_amdgpu_vm_bo_map(bo_va, mapping);
}

/**
 * amdgpu_vm_bo_map - map bo inside a vm
 *
 * @adev: amdgpu_device pointer
 * @bo_va: bo_va to store the address
 * @saddr: where to map the BO
 * @offset: requested offset in the BO
 * @size: BO size in bytes
 * @flags: attributes of pages (read/write/valid/etc.)
 *
 * Add a mapping of the BO at the specefied addr into the VM.
 *
 * Returns:
 * 0 for success, error for failure.
 *
 * Object has to be reserved and unreserved outside!
 */
int amdgpu_vm_bo_map(struct amdgpu_device *adev,
		     struct amdgpu_bo_va *bo_va,
		     uint64_t saddr, uint64_t offset,
		     uint64_t size, uint64_t flags)
{
	struct amdgpu_bo_va_mapping *mapping, *tmp;
	struct amdgpu_bo *bo = bo_va->base.bo;
	struct amdgpu_vm *vm = bo_va->base.vm;
	uint64_t eaddr;

	/* validate the parameters */
	if (saddr & ~PAGE_MASK || offset & ~PAGE_MASK ||
	    size == 0 || size & ~PAGE_MASK)
		return -EINVAL;

	/* make sure object fit at this offset */
	eaddr = saddr + size - 1;
	if (saddr >= eaddr ||
	    (bo && offset + size > amdgpu_bo_size(bo)) ||
	    (eaddr >= adev->vm_manager.max_pfn << AMDGPU_GPU_PAGE_SHIFT))
		return -EINVAL;

	saddr /= AMDGPU_GPU_PAGE_SIZE;
	eaddr /= AMDGPU_GPU_PAGE_SIZE;

	tmp = amdgpu_vm_it_iter_first(&vm->va, saddr, eaddr);
	if (tmp) {
		/* bo and tmp overlap, invalid addr */
		dev_err(adev->dev, "bo %p va 0x%010Lx-0x%010Lx conflict with "
			"0x%010Lx-0x%010Lx\n", bo, saddr, eaddr,
			tmp->start, tmp->last + 1);
		return -EINVAL;
	}

	mapping = kmalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return -ENOMEM;

	mapping->start = saddr;
	mapping->last = eaddr;
	mapping->offset = offset;
	mapping->flags = flags;

	amdgpu_vm_bo_insert_map(adev, bo_va, mapping);

	return 0;
}

/**
 * amdgpu_vm_bo_replace_map - map bo inside a vm, replacing existing mappings
 *
 * @adev: amdgpu_device pointer
 * @bo_va: bo_va to store the address
 * @saddr: where to map the BO
 * @offset: requested offset in the BO
 * @size: BO size in bytes
 * @flags: attributes of pages (read/write/valid/etc.)
 *
 * Add a mapping of the BO at the specefied addr into the VM. Replace existing
 * mappings as we do so.
 *
 * Returns:
 * 0 for success, error for failure.
 *
 * Object has to be reserved and unreserved outside!
 */
int amdgpu_vm_bo_replace_map(struct amdgpu_device *adev,
			     struct amdgpu_bo_va *bo_va,
			     uint64_t saddr, uint64_t offset,
			     uint64_t size, uint64_t flags)
{
	struct amdgpu_bo_va_mapping *mapping;
	struct amdgpu_bo *bo = bo_va->base.bo;
	uint64_t eaddr;
	int r;

	/* validate the parameters */
	if (saddr & ~PAGE_MASK || offset & ~PAGE_MASK ||
	    size == 0 || size & ~PAGE_MASK)
		return -EINVAL;

	/* make sure object fit at this offset */
	eaddr = saddr + size - 1;
	if (saddr >= eaddr ||
	    (bo && offset + size > amdgpu_bo_size(bo)) ||
	    (eaddr >= adev->vm_manager.max_pfn << AMDGPU_GPU_PAGE_SHIFT))
		return -EINVAL;

	/* Allocate all the needed memory */
	mapping = kmalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return -ENOMEM;

	r = amdgpu_vm_bo_clear_mappings(adev, bo_va->base.vm, saddr, size);
	if (r) {
		kfree(mapping);
		return r;
	}

	saddr /= AMDGPU_GPU_PAGE_SIZE;
	eaddr /= AMDGPU_GPU_PAGE_SIZE;

	mapping->start = saddr;
	mapping->last = eaddr;
	mapping->offset = offset;
	mapping->flags = flags;

	amdgpu_vm_bo_insert_map(adev, bo_va, mapping);

	return 0;
}

/**
 * amdgpu_vm_bo_unmap - remove bo mapping from vm
 *
 * @adev: amdgpu_device pointer
 * @bo_va: bo_va to remove the address from
 * @saddr: where to the BO is mapped
 *
 * Remove a mapping of the BO at the specefied addr from the VM.
 *
 * Returns:
 * 0 for success, error for failure.
 *
 * Object has to be reserved and unreserved outside!
 */
int amdgpu_vm_bo_unmap(struct amdgpu_device *adev,
		       struct amdgpu_bo_va *bo_va,
		       uint64_t saddr)
{
	struct amdgpu_bo_va_mapping *mapping;
	struct amdgpu_vm *vm = bo_va->base.vm;
	bool valid = true;

	saddr /= AMDGPU_GPU_PAGE_SIZE;

	list_for_each_entry(mapping, &bo_va->valids, list) {
		if (mapping->start == saddr)
			break;
	}

	if (&mapping->list == &bo_va->valids) {
		valid = false;

		list_for_each_entry(mapping, &bo_va->invalids, list) {
			if (mapping->start == saddr)
				break;
		}

		if (&mapping->list == &bo_va->invalids)
			return -ENOENT;
	}

	list_del(&mapping->list);
	amdgpu_vm_it_remove(mapping, &vm->va);
	mapping->bo_va = NULL;
	trace_amdgpu_vm_bo_unmap(bo_va, mapping);

	if (valid)
		list_add(&mapping->list, &vm->freed);
	else
		amdgpu_vm_free_mapping(adev, vm, mapping,
				       bo_va->last_pt_update);

	return 0;
}

/**
 * amdgpu_vm_bo_clear_mappings - remove all mappings in a specific range
 *
 * @adev: amdgpu_device pointer
 * @vm: VM structure to use
 * @saddr: start of the range
 * @size: size of the range
 *
 * Remove all mappings in a range, split them as appropriate.
 *
 * Returns:
 * 0 for success, error for failure.
 */
int amdgpu_vm_bo_clear_mappings(struct amdgpu_device *adev,
				struct amdgpu_vm *vm,
				uint64_t saddr, uint64_t size)
{
	struct amdgpu_bo_va_mapping *before, *after, *tmp, *next;
	LIST_HEAD(removed);
	uint64_t eaddr;

	eaddr = saddr + size - 1;
	saddr /= AMDGPU_GPU_PAGE_SIZE;
	eaddr /= AMDGPU_GPU_PAGE_SIZE;

	/* Allocate all the needed memory */
	before = kzalloc(sizeof(*before), GFP_KERNEL);
	if (!before)
		return -ENOMEM;
	INIT_LIST_HEAD(&before->list);

	after = kzalloc(sizeof(*after), GFP_KERNEL);
	if (!after) {
		kfree(before);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&after->list);

	/* Now gather all removed mappings */
	tmp = amdgpu_vm_it_iter_first(&vm->va, saddr, eaddr);
	while (tmp) {
		/* Remember mapping split at the start */
		if (tmp->start < saddr) {
			before->start = tmp->start;
			before->last = saddr - 1;
			before->offset = tmp->offset;
			before->flags = tmp->flags;
			before->bo_va = tmp->bo_va;
			list_add(&before->list, &tmp->bo_va->invalids);
		}

		/* Remember mapping split at the end */
		if (tmp->last > eaddr) {
			after->start = eaddr + 1;
			after->last = tmp->last;
			after->offset = tmp->offset;
			after->offset += (after->start - tmp->start) << PAGE_SHIFT;
			after->flags = tmp->flags;
			after->bo_va = tmp->bo_va;
			list_add(&after->list, &tmp->bo_va->invalids);
		}

		list_del(&tmp->list);
		list_add(&tmp->list, &removed);

		tmp = amdgpu_vm_it_iter_next(tmp, saddr, eaddr);
	}

	/* And free them up */
	list_for_each_entry_safe(tmp, next, &removed, list) {
		amdgpu_vm_it_remove(tmp, &vm->va);
		list_del(&tmp->list);

		if (tmp->start < saddr)
		    tmp->start = saddr;
		if (tmp->last > eaddr)
		    tmp->last = eaddr;

		tmp->bo_va = NULL;
		list_add(&tmp->list, &vm->freed);
		trace_amdgpu_vm_bo_unmap(NULL, tmp);
	}

	/* Insert partial mapping before the range */
	if (!list_empty(&before->list)) {
		amdgpu_vm_it_insert(before, &vm->va);
		if (before->flags & AMDGPU_PTE_PRT)
			amdgpu_vm_prt_get(adev);
	} else {
		kfree(before);
	}

	/* Insert partial mapping after the range */
	if (!list_empty(&after->list)) {
		amdgpu_vm_it_insert(after, &vm->va);
		if (after->flags & AMDGPU_PTE_PRT)
			amdgpu_vm_prt_get(adev);
	} else {
		kfree(after);
	}

	return 0;
}

/**
 * amdgpu_vm_bo_lookup_mapping - find mapping by address
 *
 * @vm: the requested VM
 * @addr: the address
 *
 * Find a mapping by it's address.
 *
 * Returns:
 * The amdgpu_bo_va_mapping matching for addr or NULL
 *
 */
struct amdgpu_bo_va_mapping *amdgpu_vm_bo_lookup_mapping(struct amdgpu_vm *vm,
							 uint64_t addr)
{
	return amdgpu_vm_it_iter_first(&vm->va, addr, addr);
}

/**
 * amdgpu_vm_bo_trace_cs - trace all reserved mappings
 *
 * @vm: the requested vm
 * @ticket: CS ticket
 *
 * Trace all mappings of BOs reserved during a command submission.
 */
void amdgpu_vm_bo_trace_cs(struct amdgpu_vm *vm, struct ww_acquire_ctx *ticket)
{
	struct amdgpu_bo_va_mapping *mapping;

	if (!trace_amdgpu_vm_bo_cs_enabled())
		return;

	for (mapping = amdgpu_vm_it_iter_first(&vm->va, 0, U64_MAX); mapping;
	     mapping = amdgpu_vm_it_iter_next(mapping, 0, U64_MAX)) {
		if (mapping->bo_va && mapping->bo_va->base.bo) {
			struct amdgpu_bo *bo;

			bo = mapping->bo_va->base.bo;
			if (dma_resv_locking_ctx(bo->tbo.base.resv) !=
			    ticket)
				continue;
		}

		trace_amdgpu_vm_bo_cs(mapping);
	}
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
	struct amdgpu_bo *bo = bo_va->base.bo;
	struct amdgpu_vm *vm = bo_va->base.vm;
	struct amdgpu_vm_bo_base **base;

	if (bo) {
		if (bo->tbo.base.resv == vm->root.base.bo->tbo.base.resv)
			vm->bulk_moveable = false;

		for (base = &bo_va->base.bo->vm_bo; *base;
		     base = &(*base)->next) {
			if (*base != &bo_va->base)
				continue;

			*base = bo_va->base.next;
			break;
		}
	}

	spin_lock(&vm->invalidated_lock);
	list_del(&bo_va->base.vm_status);
	spin_unlock(&vm->invalidated_lock);

	list_for_each_entry_safe(mapping, next, &bo_va->valids, list) {
		list_del(&mapping->list);
		amdgpu_vm_it_remove(mapping, &vm->va);
		mapping->bo_va = NULL;
		trace_amdgpu_vm_bo_unmap(bo_va, mapping);
		list_add(&mapping->list, &vm->freed);
	}
	list_for_each_entry_safe(mapping, next, &bo_va->invalids, list) {
		list_del(&mapping->list);
		amdgpu_vm_it_remove(mapping, &vm->va);
		amdgpu_vm_free_mapping(adev, vm, mapping,
				       bo_va->last_pt_update);
	}

	dma_fence_put(bo_va->last_pt_update);

	if (bo && bo_va->is_xgmi)
		amdgpu_xgmi_set_pstate(adev, AMDGPU_XGMI_PSTATE_MIN);

	kfree(bo_va);
}

/**
 * amdgpu_vm_evictable - check if we can evict a VM
 *
 * @bo: A page table of the VM.
 *
 * Check if it is possible to evict a VM.
 */
bool amdgpu_vm_evictable(struct amdgpu_bo *bo)
{
	struct amdgpu_vm_bo_base *bo_base = bo->vm_bo;

	/* Page tables of a destroyed VM can go away immediately */
	if (!bo_base || !bo_base->vm)
		return true;

	/* Don't evict VM page tables while they are busy */
	if (!dma_resv_test_signaled_rcu(bo->tbo.base.resv, true))
		return false;

	/* Try to block ongoing updates */
	if (!amdgpu_vm_eviction_trylock(bo_base->vm))
		return false;

	/* Don't evict VM page tables while they are updated */
	if (!dma_fence_is_signaled(bo_base->vm->last_unlocked)) {
		amdgpu_vm_eviction_unlock(bo_base->vm);
		return false;
	}

	bo_base->vm->evicting = true;
	amdgpu_vm_eviction_unlock(bo_base->vm);
	return true;
}

/**
 * amdgpu_vm_bo_invalidate - mark the bo as invalid
 *
 * @adev: amdgpu_device pointer
 * @bo: amdgpu buffer object
 * @evicted: is the BO evicted
 *
 * Mark @bo as invalid.
 */
void amdgpu_vm_bo_invalidate(struct amdgpu_device *adev,
			     struct amdgpu_bo *bo, bool evicted)
{
	struct amdgpu_vm_bo_base *bo_base;

	/* shadow bo doesn't have bo base, its validation needs its parent */
	if (bo->parent && bo->parent->shadow == bo)
		bo = bo->parent;

	for (bo_base = bo->vm_bo; bo_base; bo_base = bo_base->next) {
		struct amdgpu_vm *vm = bo_base->vm;

		if (evicted && bo->tbo.base.resv == vm->root.base.bo->tbo.base.resv) {
			amdgpu_vm_bo_evicted(bo_base);
			continue;
		}

		if (bo_base->moved)
			continue;
		bo_base->moved = true;

		if (bo->tbo.type == ttm_bo_type_kernel)
			amdgpu_vm_bo_relocated(bo_base);
		else if (bo->tbo.base.resv == vm->root.base.bo->tbo.base.resv)
			amdgpu_vm_bo_moved(bo_base);
		else
			amdgpu_vm_bo_invalidated(bo_base);
	}
}

/**
 * amdgpu_vm_get_block_size - calculate VM page table size as power of two
 *
 * @vm_size: VM size
 *
 * Returns:
 * VM page table as power of two
 */
static uint32_t amdgpu_vm_get_block_size(uint64_t vm_size)
{
	/* Total bits covered by PD + PTs */
	unsigned bits = ilog2(vm_size) + 18;

	/* Make sure the PD is 4K in size up to 8GB address space.
	   Above that split equal between PD and PTs */
	if (vm_size <= 8)
		return (bits - 9);
	else
		return ((bits + 3) / 2);
}

/**
 * amdgpu_vm_adjust_size - adjust vm size, block size and fragment size
 *
 * @adev: amdgpu_device pointer
 * @min_vm_size: the minimum vm size in GB if it's set auto
 * @fragment_size_default: Default PTE fragment size
 * @max_level: max VMPT level
 * @max_bits: max address space size in bits
 *
 */
void amdgpu_vm_adjust_size(struct amdgpu_device *adev, uint32_t min_vm_size,
			   uint32_t fragment_size_default, unsigned max_level,
			   unsigned max_bits)
{
	unsigned int max_size = 1 << (max_bits - 30);
	unsigned int vm_size;
	uint64_t tmp;

	/* adjust vm size first */
	if (amdgpu_vm_size != -1) {
		vm_size = amdgpu_vm_size;
		if (vm_size > max_size) {
			dev_warn(adev->dev, "VM size (%d) too large, max is %u GB\n",
				 amdgpu_vm_size, max_size);
			vm_size = max_size;
		}
	} else {
		struct sysinfo si;
		unsigned int phys_ram_gb;

		/* Optimal VM size depends on the amount of physical
		 * RAM available. Underlying requirements and
		 * assumptions:
		 *
		 *  - Need to map system memory and VRAM from all GPUs
		 *     - VRAM from other GPUs not known here
		 *     - Assume VRAM <= system memory
		 *  - On GFX8 and older, VM space can be segmented for
		 *    different MTYPEs
		 *  - Need to allow room for fragmentation, guard pages etc.
		 *
		 * This adds up to a rough guess of system memory x3.
		 * Round up to power of two to maximize the available
		 * VM size with the given page table size.
		 */
		si_meminfo(&si);
		phys_ram_gb = ((uint64_t)si.totalram * si.mem_unit +
			       (1 << 30) - 1) >> 30;
		vm_size = roundup_pow_of_two(
			min(max(phys_ram_gb * 3, min_vm_size), max_size));
	}

	adev->vm_manager.max_pfn = (uint64_t)vm_size << 18;

	tmp = roundup_pow_of_two(adev->vm_manager.max_pfn);
	if (amdgpu_vm_block_size != -1)
		tmp >>= amdgpu_vm_block_size - 9;
	tmp = DIV_ROUND_UP(fls64(tmp) - 1, 9) - 1;
	adev->vm_manager.num_level = min(max_level, (unsigned)tmp);
	switch (adev->vm_manager.num_level) {
	case 3:
		adev->vm_manager.root_level = AMDGPU_VM_PDB2;
		break;
	case 2:
		adev->vm_manager.root_level = AMDGPU_VM_PDB1;
		break;
	case 1:
		adev->vm_manager.root_level = AMDGPU_VM_PDB0;
		break;
	default:
		dev_err(adev->dev, "VMPT only supports 2~4+1 levels\n");
	}
	/* block size depends on vm size and hw setup*/
	if (amdgpu_vm_block_size != -1)
		adev->vm_manager.block_size =
			min((unsigned)amdgpu_vm_block_size, max_bits
			    - AMDGPU_GPU_PAGE_SHIFT
			    - 9 * adev->vm_manager.num_level);
	else if (adev->vm_manager.num_level > 1)
		adev->vm_manager.block_size = 9;
	else
		adev->vm_manager.block_size = amdgpu_vm_get_block_size(tmp);

	if (amdgpu_vm_fragment_size == -1)
		adev->vm_manager.fragment_size = fragment_size_default;
	else
		adev->vm_manager.fragment_size = amdgpu_vm_fragment_size;

	DRM_INFO("vm size is %u GB, %u levels, block size is %u-bit, fragment size is %u-bit\n",
		 vm_size, adev->vm_manager.num_level + 1,
		 adev->vm_manager.block_size,
		 adev->vm_manager.fragment_size);
}

/**
 * amdgpu_vm_wait_idle - wait for the VM to become idle
 *
 * @vm: VM object to wait for
 * @timeout: timeout to wait for VM to become idle
 */
long amdgpu_vm_wait_idle(struct amdgpu_vm *vm, long timeout)
{
	timeout = dma_resv_wait_timeout_rcu(vm->root.base.bo->tbo.base.resv,
					    true, true, timeout);
	if (timeout <= 0)
		return timeout;

	return dma_fence_wait_timeout(vm->last_unlocked, true, timeout);
}

/**
 * amdgpu_vm_init - initialize a vm instance
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @vm_context: Indicates if it GFX or Compute context
 * @pasid: Process address space identifier
 *
 * Init @vm fields.
 *
 * Returns:
 * 0 for success, error for failure.
 */
int amdgpu_vm_init(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		   int vm_context, u32 pasid)
{
	struct amdgpu_bo_param bp;
	struct amdgpu_bo *root;
	int r, i;

	vm->va = RB_ROOT_CACHED;
	for (i = 0; i < AMDGPU_MAX_VMHUBS; i++)
		vm->reserved_vmid[i] = NULL;
	INIT_LIST_HEAD(&vm->evicted);
	INIT_LIST_HEAD(&vm->relocated);
	INIT_LIST_HEAD(&vm->moved);
	INIT_LIST_HEAD(&vm->idle);
	INIT_LIST_HEAD(&vm->invalidated);
	spin_lock_init(&vm->invalidated_lock);
	INIT_LIST_HEAD(&vm->freed);


	/* create scheduler entities for page table updates */
	r = drm_sched_entity_init(&vm->immediate, DRM_SCHED_PRIORITY_NORMAL,
				  adev->vm_manager.vm_pte_scheds,
				  adev->vm_manager.vm_pte_num_scheds, NULL);
	if (r)
		return r;

	r = drm_sched_entity_init(&vm->delayed, DRM_SCHED_PRIORITY_NORMAL,
				  adev->vm_manager.vm_pte_scheds,
				  adev->vm_manager.vm_pte_num_scheds, NULL);
	if (r)
		goto error_free_immediate;

	vm->pte_support_ats = false;
	vm->is_compute_context = false;

	if (vm_context == AMDGPU_VM_CONTEXT_COMPUTE) {
		vm->use_cpu_for_update = !!(adev->vm_manager.vm_update_mode &
						AMDGPU_VM_USE_CPU_FOR_COMPUTE);

		if (adev->asic_type == CHIP_RAVEN)
			vm->pte_support_ats = true;
	} else {
		vm->use_cpu_for_update = !!(adev->vm_manager.vm_update_mode &
						AMDGPU_VM_USE_CPU_FOR_GFX);
	}
	DRM_DEBUG_DRIVER("VM update mode is %s\n",
			 vm->use_cpu_for_update ? "CPU" : "SDMA");
	WARN_ONCE((vm->use_cpu_for_update &&
		   !amdgpu_gmc_vram_full_visible(&adev->gmc)),
		  "CPU update of VM recommended only for large BAR system\n");

	if (vm->use_cpu_for_update)
		vm->update_funcs = &amdgpu_vm_cpu_funcs;
	else
		vm->update_funcs = &amdgpu_vm_sdma_funcs;
	vm->last_update = NULL;
	vm->last_unlocked = dma_fence_get_stub();

	mutex_init(&vm->eviction_lock);
	vm->evicting = false;

	amdgpu_vm_bo_param(adev, vm, adev->vm_manager.root_level, false, &bp);
	if (vm_context == AMDGPU_VM_CONTEXT_COMPUTE)
		bp.flags &= ~AMDGPU_GEM_CREATE_SHADOW;
	r = amdgpu_bo_create(adev, &bp, &root);
	if (r)
		goto error_free_delayed;

	r = amdgpu_bo_reserve(root, true);
	if (r)
		goto error_free_root;

	r = dma_resv_reserve_shared(root->tbo.base.resv, 1);
	if (r)
		goto error_unreserve;

	amdgpu_vm_bo_base_init(&vm->root.base, vm, root);

	r = amdgpu_vm_clear_bo(adev, vm, root, false);
	if (r)
		goto error_unreserve;

	amdgpu_bo_unreserve(vm->root.base.bo);

	if (pasid) {
		unsigned long flags;

		spin_lock_irqsave(&adev->vm_manager.pasid_lock, flags);
		r = idr_alloc(&adev->vm_manager.pasid_idr, vm, pasid, pasid + 1,
			      GFP_ATOMIC);
		spin_unlock_irqrestore(&adev->vm_manager.pasid_lock, flags);
		if (r < 0)
			goto error_free_root;

		vm->pasid = pasid;
	}

	INIT_KFIFO(vm->faults);

	return 0;

error_unreserve:
	amdgpu_bo_unreserve(vm->root.base.bo);

error_free_root:
	amdgpu_bo_unref(&vm->root.base.bo->shadow);
	amdgpu_bo_unref(&vm->root.base.bo);
	vm->root.base.bo = NULL;

error_free_delayed:
	dma_fence_put(vm->last_unlocked);
	drm_sched_entity_destroy(&vm->delayed);

error_free_immediate:
	drm_sched_entity_destroy(&vm->immediate);

	return r;
}

/**
 * amdgpu_vm_check_clean_reserved - check if a VM is clean
 *
 * @adev: amdgpu_device pointer
 * @vm: the VM to check
 *
 * check all entries of the root PD, if any subsequent PDs are allocated,
 * it means there are page table creating and filling, and is no a clean
 * VM
 *
 * Returns:
 *	0 if this VM is clean
 */
static int amdgpu_vm_check_clean_reserved(struct amdgpu_device *adev,
	struct amdgpu_vm *vm)
{
	enum amdgpu_vm_level root = adev->vm_manager.root_level;
	unsigned int entries = amdgpu_vm_num_entries(adev, root);
	unsigned int i = 0;

	if (!(vm->root.entries))
		return 0;

	for (i = 0; i < entries; i++) {
		if (vm->root.entries[i].base.bo)
			return -EINVAL;
	}

	return 0;
}

/**
 * amdgpu_vm_make_compute - Turn a GFX VM into a compute VM
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @pasid: pasid to use
 *
 * This only works on GFX VMs that don't have any BOs added and no
 * page tables allocated yet.
 *
 * Changes the following VM parameters:
 * - use_cpu_for_update
 * - pte_supports_ats
 * - pasid (old PASID is released, because compute manages its own PASIDs)
 *
 * Reinitializes the page directory to reflect the changed ATS
 * setting.
 *
 * Returns:
 * 0 for success, -errno for errors.
 */
int amdgpu_vm_make_compute(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			   u32 pasid)
{
	bool pte_support_ats = (adev->asic_type == CHIP_RAVEN);
	int r;

	r = amdgpu_bo_reserve(vm->root.base.bo, true);
	if (r)
		return r;

	/* Sanity checks */
	r = amdgpu_vm_check_clean_reserved(adev, vm);
	if (r)
		goto unreserve_bo;

	if (pasid) {
		unsigned long flags;

		spin_lock_irqsave(&adev->vm_manager.pasid_lock, flags);
		r = idr_alloc(&adev->vm_manager.pasid_idr, vm, pasid, pasid + 1,
			      GFP_ATOMIC);
		spin_unlock_irqrestore(&adev->vm_manager.pasid_lock, flags);

		if (r == -ENOSPC)
			goto unreserve_bo;
		r = 0;
	}

	/* Check if PD needs to be reinitialized and do it before
	 * changing any other state, in case it fails.
	 */
	if (pte_support_ats != vm->pte_support_ats) {
		vm->pte_support_ats = pte_support_ats;
		r = amdgpu_vm_clear_bo(adev, vm, vm->root.base.bo, false);
		if (r)
			goto free_idr;
	}

	/* Update VM state */
	vm->use_cpu_for_update = !!(adev->vm_manager.vm_update_mode &
				    AMDGPU_VM_USE_CPU_FOR_COMPUTE);
	DRM_DEBUG_DRIVER("VM update mode is %s\n",
			 vm->use_cpu_for_update ? "CPU" : "SDMA");
	WARN_ONCE((vm->use_cpu_for_update &&
		   !amdgpu_gmc_vram_full_visible(&adev->gmc)),
		  "CPU update of VM recommended only for large BAR system\n");

	if (vm->use_cpu_for_update) {
		/* Sync with last SDMA update/clear before switching to CPU */
		r = amdgpu_bo_sync_wait(vm->root.base.bo,
					AMDGPU_FENCE_OWNER_UNDEFINED, true);
		if (r)
			goto free_idr;

		vm->update_funcs = &amdgpu_vm_cpu_funcs;
	} else {
		vm->update_funcs = &amdgpu_vm_sdma_funcs;
	}
	dma_fence_put(vm->last_update);
	vm->last_update = NULL;
	vm->is_compute_context = true;

	if (vm->pasid) {
		unsigned long flags;

		spin_lock_irqsave(&adev->vm_manager.pasid_lock, flags);
		idr_remove(&adev->vm_manager.pasid_idr, vm->pasid);
		spin_unlock_irqrestore(&adev->vm_manager.pasid_lock, flags);

		/* Free the original amdgpu allocated pasid
		 * Will be replaced with kfd allocated pasid
		 */
		amdgpu_pasid_free(vm->pasid);
		vm->pasid = 0;
	}

	/* Free the shadow bo for compute VM */
	amdgpu_bo_unref(&vm->root.base.bo->shadow);

	if (pasid)
		vm->pasid = pasid;

	goto unreserve_bo;

free_idr:
	if (pasid) {
		unsigned long flags;

		spin_lock_irqsave(&adev->vm_manager.pasid_lock, flags);
		idr_remove(&adev->vm_manager.pasid_idr, pasid);
		spin_unlock_irqrestore(&adev->vm_manager.pasid_lock, flags);
	}
unreserve_bo:
	amdgpu_bo_unreserve(vm->root.base.bo);
	return r;
}

/**
 * amdgpu_vm_release_compute - release a compute vm
 * @adev: amdgpu_device pointer
 * @vm: a vm turned into compute vm by calling amdgpu_vm_make_compute
 *
 * This is a correspondant of amdgpu_vm_make_compute. It decouples compute
 * pasid from vm. Compute should stop use of vm after this call.
 */
void amdgpu_vm_release_compute(struct amdgpu_device *adev, struct amdgpu_vm *vm)
{
	if (vm->pasid) {
		unsigned long flags;

		spin_lock_irqsave(&adev->vm_manager.pasid_lock, flags);
		idr_remove(&adev->vm_manager.pasid_idr, vm->pasid);
		spin_unlock_irqrestore(&adev->vm_manager.pasid_lock, flags);
	}
	vm->pasid = 0;
	vm->is_compute_context = false;
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
	bool prt_fini_needed = !!adev->gmc.gmc_funcs->set_prt;
	struct amdgpu_bo *root;
	int i;

	amdgpu_amdkfd_gpuvm_destroy_cb(adev, vm);

	root = amdgpu_bo_ref(vm->root.base.bo);
	amdgpu_bo_reserve(root, true);
	if (vm->pasid) {
		unsigned long flags;

		spin_lock_irqsave(&adev->vm_manager.pasid_lock, flags);
		idr_remove(&adev->vm_manager.pasid_idr, vm->pasid);
		spin_unlock_irqrestore(&adev->vm_manager.pasid_lock, flags);
		vm->pasid = 0;
	}

	dma_fence_wait(vm->last_unlocked, false);
	dma_fence_put(vm->last_unlocked);

	list_for_each_entry_safe(mapping, tmp, &vm->freed, list) {
		if (mapping->flags & AMDGPU_PTE_PRT && prt_fini_needed) {
			amdgpu_vm_prt_fini(adev, vm);
			prt_fini_needed = false;
		}

		list_del(&mapping->list);
		amdgpu_vm_free_mapping(adev, vm, mapping, NULL);
	}

	amdgpu_vm_free_pts(adev, vm, NULL);
	amdgpu_bo_unreserve(root);
	amdgpu_bo_unref(&root);
	WARN_ON(vm->root.base.bo);

	drm_sched_entity_destroy(&vm->immediate);
	drm_sched_entity_destroy(&vm->delayed);

	if (!RB_EMPTY_ROOT(&vm->va.rb_root)) {
		dev_err(adev->dev, "still active bo inside vm\n");
	}
	rbtree_postorder_for_each_entry_safe(mapping, tmp,
					     &vm->va.rb_root, rb) {
		/* Don't remove the mapping here, we don't want to trigger a
		 * rebalance and the tree is about to be destroyed anyway.
		 */
		list_del(&mapping->list);
		kfree(mapping);
	}

	dma_fence_put(vm->last_update);
	for (i = 0; i < AMDGPU_MAX_VMHUBS; i++)
		amdgpu_vmid_free_reserved(adev, vm, i);
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

	/* Concurrent flushes are only possible starting with Vega10 and
	 * are broken on Navi10 and Navi14.
	 */
	adev->vm_manager.concurrent_flush = !(adev->asic_type < CHIP_VEGA10 ||
					      adev->asic_type == CHIP_NAVI10 ||
					      adev->asic_type == CHIP_NAVI14);
	amdgpu_vmid_mgr_init(adev);

	adev->vm_manager.fence_context =
		dma_fence_context_alloc(AMDGPU_MAX_RINGS);
	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		adev->vm_manager.seqno[i] = 0;

	spin_lock_init(&adev->vm_manager.prt_lock);
	atomic_set(&adev->vm_manager.num_prt_users, 0);

	/* If not overridden by the user, by default, only in large BAR systems
	 * Compute VM tables will be updated by CPU
	 */
#ifdef CONFIG_X86_64
	if (amdgpu_vm_update_mode == -1) {
		if (amdgpu_gmc_vram_full_visible(&adev->gmc))
			adev->vm_manager.vm_update_mode =
				AMDGPU_VM_USE_CPU_FOR_COMPUTE;
		else
			adev->vm_manager.vm_update_mode = 0;
	} else
		adev->vm_manager.vm_update_mode = amdgpu_vm_update_mode;
#else
	adev->vm_manager.vm_update_mode = 0;
#endif

	idr_init(&adev->vm_manager.pasid_idr);
	spin_lock_init(&adev->vm_manager.pasid_lock);
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
	WARN_ON(!idr_is_empty(&adev->vm_manager.pasid_idr));
	idr_destroy(&adev->vm_manager.pasid_idr);

	amdgpu_vmid_mgr_fini(adev);
}

/**
 * amdgpu_vm_ioctl - Manages VMID reservation for vm hubs.
 *
 * @dev: drm device pointer
 * @data: drm_amdgpu_vm
 * @filp: drm file pointer
 *
 * Returns:
 * 0 for success, -errno for errors.
 */
int amdgpu_vm_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	union drm_amdgpu_vm *args = data;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	long timeout = msecs_to_jiffies(2000);
	int r;

	switch (args->in.op) {
	case AMDGPU_VM_OP_RESERVE_VMID:
		/* We only have requirement to reserve vmid from gfxhub */
		r = amdgpu_vmid_alloc_reserved(adev, &fpriv->vm,
					       AMDGPU_GFXHUB_0);
		if (r)
			return r;
		break;
	case AMDGPU_VM_OP_UNRESERVE_VMID:
		if (amdgpu_sriov_runtime(adev))
			timeout = 8 * timeout;

		/* Wait vm idle to make sure the vmid set in SPM_VMID is
		 * not referenced anymore.
		 */
		r = amdgpu_bo_reserve(fpriv->vm.root.base.bo, true);
		if (r)
			return r;

		r = amdgpu_vm_wait_idle(&fpriv->vm, timeout);
		if (r < 0)
			return r;

		amdgpu_bo_unreserve(fpriv->vm.root.base.bo);
		amdgpu_vmid_free_reserved(adev, &fpriv->vm, AMDGPU_GFXHUB_0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * amdgpu_vm_get_task_info - Extracts task info for a PASID.
 *
 * @adev: drm device pointer
 * @pasid: PASID identifier for VM
 * @task_info: task_info to fill.
 */
void amdgpu_vm_get_task_info(struct amdgpu_device *adev, u32 pasid,
			 struct amdgpu_task_info *task_info)
{
	struct amdgpu_vm *vm;
	unsigned long flags;

	spin_lock_irqsave(&adev->vm_manager.pasid_lock, flags);

	vm = idr_find(&adev->vm_manager.pasid_idr, pasid);
	if (vm)
		*task_info = vm->task_info;

	spin_unlock_irqrestore(&adev->vm_manager.pasid_lock, flags);
}

/**
 * amdgpu_vm_set_task_info - Sets VMs task info.
 *
 * @vm: vm for which to set the info
 */
void amdgpu_vm_set_task_info(struct amdgpu_vm *vm)
{
	if (vm->task_info.pid)
		return;

	vm->task_info.pid = current->pid;
	get_task_comm(vm->task_info.task_name, current);

	if (current->group_leader->mm != current->mm)
		return;

	vm->task_info.tgid = current->group_leader->pid;
	get_task_comm(vm->task_info.process_name, current->group_leader);
}

/**
 * amdgpu_vm_handle_fault - graceful handling of VM faults.
 * @adev: amdgpu device pointer
 * @pasid: PASID of the VM
 * @addr: Address of the fault
 *
 * Try to gracefully handle a VM fault. Return true if the fault was handled and
 * shouldn't be reported any more.
 */
bool amdgpu_vm_handle_fault(struct amdgpu_device *adev, u32 pasid,
			    uint64_t addr)
{
	struct amdgpu_bo *root;
	uint64_t value, flags;
	struct amdgpu_vm *vm;
	long r;

	spin_lock(&adev->vm_manager.pasid_lock);
	vm = idr_find(&adev->vm_manager.pasid_idr, pasid);
	if (vm)
		root = amdgpu_bo_ref(vm->root.base.bo);
	else
		root = NULL;
	spin_unlock(&adev->vm_manager.pasid_lock);

	if (!root)
		return false;

	r = amdgpu_bo_reserve(root, true);
	if (r)
		goto error_unref;

	/* Double check that the VM still exists */
	spin_lock(&adev->vm_manager.pasid_lock);
	vm = idr_find(&adev->vm_manager.pasid_idr, pasid);
	if (vm && vm->root.base.bo != root)
		vm = NULL;
	spin_unlock(&adev->vm_manager.pasid_lock);
	if (!vm)
		goto error_unlock;

	addr /= AMDGPU_GPU_PAGE_SIZE;
	flags = AMDGPU_PTE_VALID | AMDGPU_PTE_SNOOPED |
		AMDGPU_PTE_SYSTEM;

	if (vm->is_compute_context) {
		/* Intentionally setting invalid PTE flag
		 * combination to force a no-retry-fault
		 */
		flags = AMDGPU_PTE_EXECUTABLE | AMDGPU_PDE_PTE |
			AMDGPU_PTE_TF;
		value = 0;

	} else if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_NEVER) {
		/* Redirect the access to the dummy page */
		value = adev->dummy_page_addr;
		flags |= AMDGPU_PTE_EXECUTABLE | AMDGPU_PTE_READABLE |
			AMDGPU_PTE_WRITEABLE;

	} else {
		/* Let the hw retry silently on the PTE */
		value = 0;
	}

	r = amdgpu_vm_bo_update_mapping(adev, vm, true, false, NULL, addr,
					addr + 1, flags, value, NULL, NULL);
	if (r)
		goto error_unlock;

	r = amdgpu_vm_update_pdes(adev, vm, true);

error_unlock:
	amdgpu_bo_unreserve(root);
	if (r < 0)
		DRM_ERROR("Can't handle page fault (%ld)\n", r);

error_unref:
	amdgpu_bo_unref(&root);

	return false;
}

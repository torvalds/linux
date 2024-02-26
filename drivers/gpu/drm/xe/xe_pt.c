// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_pt.h"

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_drm_client.h"
#include "xe_gt.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_migrate.h"
#include "xe_pt_types.h"
#include "xe_pt_walk.h"
#include "xe_res_cursor.h"
#include "xe_trace.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_vm.h"

struct xe_pt_dir {
	struct xe_pt pt;
	/** @children: Array of page-table child nodes */
	struct xe_ptw *children[XE_PDES];
};

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_VM)
#define xe_pt_set_addr(__xe_pt, __addr) ((__xe_pt)->addr = (__addr))
#define xe_pt_addr(__xe_pt) ((__xe_pt)->addr)
#else
#define xe_pt_set_addr(__xe_pt, __addr)
#define xe_pt_addr(__xe_pt) 0ull
#endif

static const u64 xe_normal_pt_shifts[] = {12, 21, 30, 39, 48};
static const u64 xe_compact_pt_shifts[] = {16, 21, 30, 39, 48};

#define XE_PT_HIGHEST_LEVEL (ARRAY_SIZE(xe_normal_pt_shifts) - 1)

static struct xe_pt_dir *as_xe_pt_dir(struct xe_pt *pt)
{
	return container_of(pt, struct xe_pt_dir, pt);
}

static struct xe_pt *xe_pt_entry(struct xe_pt_dir *pt_dir, unsigned int index)
{
	return container_of(pt_dir->children[index], struct xe_pt, base);
}

static u64 __xe_pt_empty_pte(struct xe_tile *tile, struct xe_vm *vm,
			     unsigned int level)
{
	struct xe_device *xe = tile_to_xe(tile);
	u16 pat_index = xe->pat.idx[XE_CACHE_WB];
	u8 id = tile->id;

	if (!xe_vm_has_scratch(vm))
		return 0;

	if (level > MAX_HUGEPTE_LEVEL)
		return vm->pt_ops->pde_encode_bo(vm->scratch_pt[id][level - 1]->bo,
						 0, pat_index);

	return vm->pt_ops->pte_encode_addr(xe, 0, pat_index, level, IS_DGFX(xe), 0) |
		XE_PTE_NULL;
}

static void xe_pt_free(struct xe_pt *pt)
{
	if (pt->level)
		kfree(as_xe_pt_dir(pt));
	else
		kfree(pt);
}

/**
 * xe_pt_create() - Create a page-table.
 * @vm: The vm to create for.
 * @tile: The tile to create for.
 * @level: The page-table level.
 *
 * Allocate and initialize a single struct xe_pt metadata structure. Also
 * create the corresponding page-table bo, but don't initialize it. If the
 * level is grater than zero, then it's assumed to be a directory page-
 * table and the directory structure is also allocated and initialized to
 * NULL pointers.
 *
 * Return: A valid struct xe_pt pointer on success, Pointer error code on
 * error.
 */
struct xe_pt *xe_pt_create(struct xe_vm *vm, struct xe_tile *tile,
			   unsigned int level)
{
	struct xe_pt *pt;
	struct xe_bo *bo;
	int err;

	if (level) {
		struct xe_pt_dir *dir = kzalloc(sizeof(*dir), GFP_KERNEL);

		pt = (dir) ? &dir->pt : NULL;
	} else {
		pt = kzalloc(sizeof(*pt), GFP_KERNEL);
	}
	if (!pt)
		return ERR_PTR(-ENOMEM);

	pt->level = level;
	bo = xe_bo_create_pin_map(vm->xe, tile, vm, SZ_4K,
				  ttm_bo_type_kernel,
				  XE_BO_CREATE_VRAM_IF_DGFX(tile) |
				  XE_BO_CREATE_IGNORE_MIN_PAGE_SIZE_BIT |
				  XE_BO_CREATE_PINNED_BIT |
				  XE_BO_CREATE_NO_RESV_EVICT |
				  XE_BO_PAGETABLE);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		goto err_kfree;
	}
	pt->bo = bo;
	pt->base.children = level ? as_xe_pt_dir(pt)->children : NULL;

	if (vm->xef)
		xe_drm_client_add_bo(vm->xef->client, pt->bo);
	xe_tile_assert(tile, level <= XE_VM_MAX_LEVEL);

	return pt;

err_kfree:
	xe_pt_free(pt);
	return ERR_PTR(err);
}

/**
 * xe_pt_populate_empty() - Populate a page-table bo with scratch- or zero
 * entries.
 * @tile: The tile the scratch pagetable of which to use.
 * @vm: The vm we populate for.
 * @pt: The pagetable the bo of which to initialize.
 *
 * Populate the page-table bo of @pt with entries pointing into the tile's
 * scratch page-table tree if any. Otherwise populate with zeros.
 */
void xe_pt_populate_empty(struct xe_tile *tile, struct xe_vm *vm,
			  struct xe_pt *pt)
{
	struct iosys_map *map = &pt->bo->vmap;
	u64 empty;
	int i;

	if (!xe_vm_has_scratch(vm)) {
		/*
		 * FIXME: Some memory is allocated already allocated to zero?
		 * Find out which memory that is and avoid this memset...
		 */
		xe_map_memset(vm->xe, map, 0, 0, SZ_4K);
	} else {
		empty = __xe_pt_empty_pte(tile, vm, pt->level);
		for (i = 0; i < XE_PDES; i++)
			xe_pt_write(vm->xe, map, i, empty);
	}
}

/**
 * xe_pt_shift() - Return the ilog2 value of the size of the address range of
 * a page-table at a certain level.
 * @level: The level.
 *
 * Return: The ilog2 value of the size of the address range of a page-table
 * at level @level.
 */
unsigned int xe_pt_shift(unsigned int level)
{
	return XE_PTE_SHIFT + XE_PDE_SHIFT * level;
}

/**
 * xe_pt_destroy() - Destroy a page-table tree.
 * @pt: The root of the page-table tree to destroy.
 * @flags: vm flags. Currently unused.
 * @deferred: List head of lockless list for deferred putting. NULL for
 *            immediate putting.
 *
 * Puts the page-table bo, recursively calls xe_pt_destroy on all children
 * and finally frees @pt. TODO: Can we remove the @flags argument?
 */
void xe_pt_destroy(struct xe_pt *pt, u32 flags, struct llist_head *deferred)
{
	int i;

	if (!pt)
		return;

	XE_WARN_ON(!list_empty(&pt->bo->ttm.base.gpuva.list));
	xe_bo_unpin(pt->bo);
	xe_bo_put_deferred(pt->bo, deferred);

	if (pt->level > 0 && pt->num_live) {
		struct xe_pt_dir *pt_dir = as_xe_pt_dir(pt);

		for (i = 0; i < XE_PDES; i++) {
			if (xe_pt_entry(pt_dir, i))
				xe_pt_destroy(xe_pt_entry(pt_dir, i), flags,
					      deferred);
		}
	}
	xe_pt_free(pt);
}

/**
 * DOC: Pagetable building
 *
 * Below we use the term "page-table" for both page-directories, containing
 * pointers to lower level page-directories or page-tables, and level 0
 * page-tables that contain only page-table-entries pointing to memory pages.
 *
 * When inserting an address range in an already existing page-table tree
 * there will typically be a set of page-tables that are shared with other
 * address ranges, and a set that are private to this address range.
 * The set of shared page-tables can be at most two per level,
 * and those can't be updated immediately because the entries of those
 * page-tables may still be in use by the gpu for other mappings. Therefore
 * when inserting entries into those, we instead stage those insertions by
 * adding insertion data into struct xe_vm_pgtable_update structures. This
 * data, (subtrees for the cpu and page-table-entries for the gpu) is then
 * added in a separate commit step. CPU-data is committed while still under the
 * vm lock, the object lock and for userptr, the notifier lock in read mode.
 * The GPU async data is committed either by the GPU or CPU after fulfilling
 * relevant dependencies.
 * For non-shared page-tables (and, in fact, for shared ones that aren't
 * existing at the time of staging), we add the data in-place without the
 * special update structures. This private part of the page-table tree will
 * remain disconnected from the vm page-table tree until data is committed to
 * the shared page tables of the vm tree in the commit phase.
 */

struct xe_pt_update {
	/** @update: The update structure we're building for this parent. */
	struct xe_vm_pgtable_update *update;
	/** @parent: The parent. Used to detect a parent change. */
	struct xe_pt *parent;
	/** @preexisting: Whether the parent was pre-existing or allocated */
	bool preexisting;
};

struct xe_pt_stage_bind_walk {
	/** base: The base class. */
	struct xe_pt_walk base;

	/* Input parameters for the walk */
	/** @vm: The vm we're building for. */
	struct xe_vm *vm;
	/** @tile: The tile we're building for. */
	struct xe_tile *tile;
	/** @default_pte: PTE flag only template. No address is associated */
	u64 default_pte;
	/** @dma_offset: DMA offset to add to the PTE. */
	u64 dma_offset;
	/**
	 * @needs_64k: This address range enforces 64K alignment and
	 * granularity.
	 */
	bool needs_64K;
	/**
	 * @vma: VMA being mapped
	 */
	struct xe_vma *vma;

	/* Also input, but is updated during the walk*/
	/** @curs: The DMA address cursor. */
	struct xe_res_cursor *curs;
	/** @va_curs_start: The Virtual address coresponding to @curs->start */
	u64 va_curs_start;

	/* Output */
	struct xe_walk_update {
		/** @wupd.entries: Caller provided storage. */
		struct xe_vm_pgtable_update *entries;
		/** @wupd.num_used_entries: Number of update @entries used. */
		unsigned int num_used_entries;
		/** @wupd.updates: Tracks the update entry at a given level */
		struct xe_pt_update updates[XE_VM_MAX_LEVEL + 1];
	} wupd;

	/* Walk state */
	/**
	 * @l0_end_addr: The end address of the current l0 leaf. Used for
	 * 64K granularity detection.
	 */
	u64 l0_end_addr;
	/** @addr_64K: The start address of the current 64K chunk. */
	u64 addr_64K;
	/** @found_64: Whether @add_64K actually points to a 64K chunk. */
	bool found_64K;
};

static int
xe_pt_new_shared(struct xe_walk_update *wupd, struct xe_pt *parent,
		 pgoff_t offset, bool alloc_entries)
{
	struct xe_pt_update *upd = &wupd->updates[parent->level];
	struct xe_vm_pgtable_update *entry;

	/*
	 * For *each level*, we could only have one active
	 * struct xt_pt_update at any one time. Once we move on to a
	 * new parent and page-directory, the old one is complete, and
	 * updates are either already stored in the build tree or in
	 * @wupd->entries
	 */
	if (likely(upd->parent == parent))
		return 0;

	upd->parent = parent;
	upd->preexisting = true;

	if (wupd->num_used_entries == XE_VM_MAX_LEVEL * 2 + 1)
		return -EINVAL;

	entry = wupd->entries + wupd->num_used_entries++;
	upd->update = entry;
	entry->ofs = offset;
	entry->pt_bo = parent->bo;
	entry->pt = parent;
	entry->flags = 0;
	entry->qwords = 0;

	if (alloc_entries) {
		entry->pt_entries = kmalloc_array(XE_PDES,
						  sizeof(*entry->pt_entries),
						  GFP_KERNEL);
		if (!entry->pt_entries)
			return -ENOMEM;
	}

	return 0;
}

/*
 * NOTE: This is a very frequently called function so we allow ourselves
 * to annotate (using branch prediction hints) the fastpath of updating a
 * non-pre-existing pagetable with leaf ptes.
 */
static int
xe_pt_insert_entry(struct xe_pt_stage_bind_walk *xe_walk, struct xe_pt *parent,
		   pgoff_t offset, struct xe_pt *xe_child, u64 pte)
{
	struct xe_pt_update *upd = &xe_walk->wupd.updates[parent->level];
	struct xe_pt_update *child_upd = xe_child ?
		&xe_walk->wupd.updates[xe_child->level] : NULL;
	int ret;

	ret = xe_pt_new_shared(&xe_walk->wupd, parent, offset, true);
	if (unlikely(ret))
		return ret;

	/*
	 * Register this new pagetable so that it won't be recognized as
	 * a shared pagetable by a subsequent insertion.
	 */
	if (unlikely(child_upd)) {
		child_upd->update = NULL;
		child_upd->parent = xe_child;
		child_upd->preexisting = false;
	}

	if (likely(!upd->preexisting)) {
		/* Continue building a non-connected subtree. */
		struct iosys_map *map = &parent->bo->vmap;

		if (unlikely(xe_child))
			parent->base.children[offset] = &xe_child->base;

		xe_pt_write(xe_walk->vm->xe, map, offset, pte);
		parent->num_live++;
	} else {
		/* Shared pt. Stage update. */
		unsigned int idx;
		struct xe_vm_pgtable_update *entry = upd->update;

		idx = offset - entry->ofs;
		entry->pt_entries[idx].pt = xe_child;
		entry->pt_entries[idx].pte = pte;
		entry->qwords++;
	}

	return 0;
}

static bool xe_pt_hugepte_possible(u64 addr, u64 next, unsigned int level,
				   struct xe_pt_stage_bind_walk *xe_walk)
{
	u64 size, dma;

	if (level > MAX_HUGEPTE_LEVEL)
		return false;

	/* Does the virtual range requested cover a huge pte? */
	if (!xe_pt_covers(addr, next, level, &xe_walk->base))
		return false;

	/* Does the DMA segment cover the whole pte? */
	if (next - xe_walk->va_curs_start > xe_walk->curs->size)
		return false;

	/* null VMA's do not have dma addresses */
	if (xe_vma_is_null(xe_walk->vma))
		return true;

	/* Is the DMA address huge PTE size aligned? */
	size = next - addr;
	dma = addr - xe_walk->va_curs_start + xe_res_dma(xe_walk->curs);

	return IS_ALIGNED(dma, size);
}

/*
 * Scan the requested mapping to check whether it can be done entirely
 * with 64K PTEs.
 */
static bool
xe_pt_scan_64K(u64 addr, u64 next, struct xe_pt_stage_bind_walk *xe_walk)
{
	struct xe_res_cursor curs = *xe_walk->curs;

	if (!IS_ALIGNED(addr, SZ_64K))
		return false;

	if (next > xe_walk->l0_end_addr)
		return false;

	/* null VMA's do not have dma addresses */
	if (xe_vma_is_null(xe_walk->vma))
		return true;

	xe_res_next(&curs, addr - xe_walk->va_curs_start);
	for (; addr < next; addr += SZ_64K) {
		if (!IS_ALIGNED(xe_res_dma(&curs), SZ_64K) || curs.size < SZ_64K)
			return false;

		xe_res_next(&curs, SZ_64K);
	}

	return addr == next;
}

/*
 * For non-compact "normal" 4K level-0 pagetables, we want to try to group
 * addresses together in 64K-contigous regions to add a 64K TLB hint for the
 * device to the PTE.
 * This function determines whether the address is part of such a
 * segment. For VRAM in normal pagetables, this is strictly necessary on
 * some devices.
 */
static bool
xe_pt_is_pte_ps64K(u64 addr, u64 next, struct xe_pt_stage_bind_walk *xe_walk)
{
	/* Address is within an already found 64k region */
	if (xe_walk->found_64K && addr - xe_walk->addr_64K < SZ_64K)
		return true;

	xe_walk->found_64K = xe_pt_scan_64K(addr, addr + SZ_64K, xe_walk);
	xe_walk->addr_64K = addr;

	return xe_walk->found_64K;
}

static int
xe_pt_stage_bind_entry(struct xe_ptw *parent, pgoff_t offset,
		       unsigned int level, u64 addr, u64 next,
		       struct xe_ptw **child,
		       enum page_walk_action *action,
		       struct xe_pt_walk *walk)
{
	struct xe_pt_stage_bind_walk *xe_walk =
		container_of(walk, typeof(*xe_walk), base);
	u16 pat_index = xe_walk->vma->pat_index;
	struct xe_pt *xe_parent = container_of(parent, typeof(*xe_parent), base);
	struct xe_vm *vm = xe_walk->vm;
	struct xe_pt *xe_child;
	bool covers;
	int ret = 0;
	u64 pte;

	/* Is this a leaf entry ?*/
	if (level == 0 || xe_pt_hugepte_possible(addr, next, level, xe_walk)) {
		struct xe_res_cursor *curs = xe_walk->curs;
		bool is_null = xe_vma_is_null(xe_walk->vma);

		XE_WARN_ON(xe_walk->va_curs_start != addr);

		pte = vm->pt_ops->pte_encode_vma(is_null ? 0 :
						 xe_res_dma(curs) + xe_walk->dma_offset,
						 xe_walk->vma, pat_index, level);
		pte |= xe_walk->default_pte;

		/*
		 * Set the XE_PTE_PS64 hint if possible, otherwise if
		 * this device *requires* 64K PTE size for VRAM, fail.
		 */
		if (level == 0 && !xe_parent->is_compact) {
			if (xe_pt_is_pte_ps64K(addr, next, xe_walk)) {
				xe_walk->vma->gpuva.flags |= XE_VMA_PTE_64K;
				pte |= XE_PTE_PS64;
			} else if (XE_WARN_ON(xe_walk->needs_64K)) {
				return -EINVAL;
			}
		}

		ret = xe_pt_insert_entry(xe_walk, xe_parent, offset, NULL, pte);
		if (unlikely(ret))
			return ret;

		if (!is_null)
			xe_res_next(curs, next - addr);
		xe_walk->va_curs_start = next;
		xe_walk->vma->gpuva.flags |= (XE_VMA_PTE_4K << level);
		*action = ACTION_CONTINUE;

		return ret;
	}

	/*
	 * Descending to lower level. Determine if we need to allocate a
	 * new page table or -directory, which we do if there is no
	 * previous one or there is one we can completely replace.
	 */
	if (level == 1) {
		walk->shifts = xe_normal_pt_shifts;
		xe_walk->l0_end_addr = next;
	}

	covers = xe_pt_covers(addr, next, level, &xe_walk->base);
	if (covers || !*child) {
		u64 flags = 0;

		xe_child = xe_pt_create(xe_walk->vm, xe_walk->tile, level - 1);
		if (IS_ERR(xe_child))
			return PTR_ERR(xe_child);

		xe_pt_set_addr(xe_child,
			       round_down(addr, 1ull << walk->shifts[level]));

		if (!covers)
			xe_pt_populate_empty(xe_walk->tile, xe_walk->vm, xe_child);

		*child = &xe_child->base;

		/*
		 * Prefer the compact pagetable layout for L0 if possible. Only
		 * possible if VMA covers entire 2MB region as compact 64k and
		 * 4k pages cannot be mixed within a 2MB region.
		 * TODO: Suballocate the pt bo to avoid wasting a lot of
		 * memory.
		 */
		if (GRAPHICS_VERx100(tile_to_xe(xe_walk->tile)) >= 1250 && level == 1 &&
		    covers && xe_pt_scan_64K(addr, next, xe_walk)) {
			walk->shifts = xe_compact_pt_shifts;
			xe_walk->vma->gpuva.flags |= XE_VMA_PTE_COMPACT;
			flags |= XE_PDE_64K;
			xe_child->is_compact = true;
		}

		pte = vm->pt_ops->pde_encode_bo(xe_child->bo, 0, pat_index) | flags;
		ret = xe_pt_insert_entry(xe_walk, xe_parent, offset, xe_child,
					 pte);
	}

	*action = ACTION_SUBTREE;
	return ret;
}

static const struct xe_pt_walk_ops xe_pt_stage_bind_ops = {
	.pt_entry = xe_pt_stage_bind_entry,
};

/**
 * xe_pt_stage_bind() - Build a disconnected page-table tree for a given address
 * range.
 * @tile: The tile we're building for.
 * @vma: The vma indicating the address range.
 * @entries: Storage for the update entries used for connecting the tree to
 * the main tree at commit time.
 * @num_entries: On output contains the number of @entries used.
 *
 * This function builds a disconnected page-table tree for a given address
 * range. The tree is connected to the main vm tree for the gpu using
 * xe_migrate_update_pgtables() and for the cpu using xe_pt_commit_bind().
 * The function builds xe_vm_pgtable_update structures for already existing
 * shared page-tables, and non-existing shared and non-shared page-tables
 * are built and populated directly.
 *
 * Return 0 on success, negative error code on error.
 */
static int
xe_pt_stage_bind(struct xe_tile *tile, struct xe_vma *vma,
		 struct xe_vm_pgtable_update *entries, u32 *num_entries)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_bo *bo = xe_vma_bo(vma);
	bool is_devmem = !xe_vma_is_userptr(vma) && bo &&
		(xe_bo_is_vram(bo) || xe_bo_is_stolen_devmem(bo));
	struct xe_res_cursor curs;
	struct xe_pt_stage_bind_walk xe_walk = {
		.base = {
			.ops = &xe_pt_stage_bind_ops,
			.shifts = xe_normal_pt_shifts,
			.max_level = XE_PT_HIGHEST_LEVEL,
		},
		.vm = xe_vma_vm(vma),
		.tile = tile,
		.curs = &curs,
		.va_curs_start = xe_vma_start(vma),
		.vma = vma,
		.wupd.entries = entries,
		.needs_64K = (xe_vma_vm(vma)->flags & XE_VM_FLAG_64K) && is_devmem,
	};
	struct xe_pt *pt = xe_vma_vm(vma)->pt_root[tile->id];
	int ret;

	if (vma && (vma->gpuva.flags & XE_VMA_ATOMIC_PTE_BIT) &&
	    (is_devmem || !IS_DGFX(xe)))
		xe_walk.default_pte |= XE_USM_PPGTT_PTE_AE;

	if (is_devmem) {
		xe_walk.default_pte |= XE_PPGTT_PTE_DM;
		xe_walk.dma_offset = vram_region_gpu_offset(bo->ttm.resource);
	}

	if (!xe_vma_has_no_bo(vma) && xe_bo_is_stolen(bo))
		xe_walk.dma_offset = xe_ttm_stolen_gpu_offset(xe_bo_device(bo));

	xe_bo_assert_held(bo);

	if (!xe_vma_is_null(vma)) {
		if (xe_vma_is_userptr(vma))
			xe_res_first_sg(to_userptr_vma(vma)->userptr.sg, 0,
					xe_vma_size(vma), &curs);
		else if (xe_bo_is_vram(bo) || xe_bo_is_stolen(bo))
			xe_res_first(bo->ttm.resource, xe_vma_bo_offset(vma),
				     xe_vma_size(vma), &curs);
		else
			xe_res_first_sg(xe_bo_sg(bo), xe_vma_bo_offset(vma),
					xe_vma_size(vma), &curs);
	} else {
		curs.size = xe_vma_size(vma);
	}

	ret = xe_pt_walk_range(&pt->base, pt->level, xe_vma_start(vma),
			       xe_vma_end(vma), &xe_walk.base);

	*num_entries = xe_walk.wupd.num_used_entries;
	return ret;
}

/**
 * xe_pt_nonshared_offsets() - Determine the non-shared entry offsets of a
 * shared pagetable.
 * @addr: The start address within the non-shared pagetable.
 * @end: The end address within the non-shared pagetable.
 * @level: The level of the non-shared pagetable.
 * @walk: Walk info. The function adjusts the walk action.
 * @action: next action to perform (see enum page_walk_action)
 * @offset: Ignored on input, First non-shared entry on output.
 * @end_offset: Ignored on input, Last non-shared entry + 1 on output.
 *
 * A non-shared page-table has some entries that belong to the address range
 * and others that don't. This function determines the entries that belong
 * fully to the address range. Depending on level, some entries may
 * partially belong to the address range (that can't happen at level 0).
 * The function detects that and adjust those offsets to not include those
 * partial entries. Iff it does detect partial entries, we know that there must
 * be shared page tables also at lower levels, so it adjusts the walk action
 * accordingly.
 *
 * Return: true if there were non-shared entries, false otherwise.
 */
static bool xe_pt_nonshared_offsets(u64 addr, u64 end, unsigned int level,
				    struct xe_pt_walk *walk,
				    enum page_walk_action *action,
				    pgoff_t *offset, pgoff_t *end_offset)
{
	u64 size = 1ull << walk->shifts[level];

	*offset = xe_pt_offset(addr, level, walk);
	*end_offset = xe_pt_num_entries(addr, end, level, walk) + *offset;

	if (!level)
		return true;

	/*
	 * If addr or next are not size aligned, there are shared pts at lower
	 * level, so in that case traverse down the subtree
	 */
	*action = ACTION_CONTINUE;
	if (!IS_ALIGNED(addr, size)) {
		*action = ACTION_SUBTREE;
		(*offset)++;
	}

	if (!IS_ALIGNED(end, size)) {
		*action = ACTION_SUBTREE;
		(*end_offset)--;
	}

	return *end_offset > *offset;
}

struct xe_pt_zap_ptes_walk {
	/** @base: The walk base-class */
	struct xe_pt_walk base;

	/* Input parameters for the walk */
	/** @tile: The tile we're building for */
	struct xe_tile *tile;

	/* Output */
	/** @needs_invalidate: Whether we need to invalidate TLB*/
	bool needs_invalidate;
};

static int xe_pt_zap_ptes_entry(struct xe_ptw *parent, pgoff_t offset,
				unsigned int level, u64 addr, u64 next,
				struct xe_ptw **child,
				enum page_walk_action *action,
				struct xe_pt_walk *walk)
{
	struct xe_pt_zap_ptes_walk *xe_walk =
		container_of(walk, typeof(*xe_walk), base);
	struct xe_pt *xe_child = container_of(*child, typeof(*xe_child), base);
	pgoff_t end_offset;

	XE_WARN_ON(!*child);
	XE_WARN_ON(!level && xe_child->is_compact);

	/*
	 * Note that we're called from an entry callback, and we're dealing
	 * with the child of that entry rather than the parent, so need to
	 * adjust level down.
	 */
	if (xe_pt_nonshared_offsets(addr, next, --level, walk, action, &offset,
				    &end_offset)) {
		xe_map_memset(tile_to_xe(xe_walk->tile), &xe_child->bo->vmap,
			      offset * sizeof(u64), 0,
			      (end_offset - offset) * sizeof(u64));
		xe_walk->needs_invalidate = true;
	}

	return 0;
}

static const struct xe_pt_walk_ops xe_pt_zap_ptes_ops = {
	.pt_entry = xe_pt_zap_ptes_entry,
};

/**
 * xe_pt_zap_ptes() - Zap (zero) gpu ptes of an address range
 * @tile: The tile we're zapping for.
 * @vma: GPU VMA detailing address range.
 *
 * Eviction and Userptr invalidation needs to be able to zap the
 * gpu ptes of a given address range in pagefaulting mode.
 * In order to be able to do that, that function needs access to the shared
 * page-table entrieaso it can either clear the leaf PTEs or
 * clear the pointers to lower-level page-tables. The caller is required
 * to hold the necessary locks to ensure neither the page-table connectivity
 * nor the page-table entries of the range is updated from under us.
 *
 * Return: Whether ptes were actually updated and a TLB invalidation is
 * required.
 */
bool xe_pt_zap_ptes(struct xe_tile *tile, struct xe_vma *vma)
{
	struct xe_pt_zap_ptes_walk xe_walk = {
		.base = {
			.ops = &xe_pt_zap_ptes_ops,
			.shifts = xe_normal_pt_shifts,
			.max_level = XE_PT_HIGHEST_LEVEL,
		},
		.tile = tile,
	};
	struct xe_pt *pt = xe_vma_vm(vma)->pt_root[tile->id];

	if (!(vma->tile_present & BIT(tile->id)))
		return false;

	(void)xe_pt_walk_shared(&pt->base, pt->level, xe_vma_start(vma),
				xe_vma_end(vma), &xe_walk.base);

	return xe_walk.needs_invalidate;
}

static void
xe_vm_populate_pgtable(struct xe_migrate_pt_update *pt_update, struct xe_tile *tile,
		       struct iosys_map *map, void *data,
		       u32 qword_ofs, u32 num_qwords,
		       const struct xe_vm_pgtable_update *update)
{
	struct xe_pt_entry *ptes = update->pt_entries;
	u64 *ptr = data;
	u32 i;

	for (i = 0; i < num_qwords; i++) {
		if (map)
			xe_map_wr(tile_to_xe(tile), map, (qword_ofs + i) *
				  sizeof(u64), u64, ptes[i].pte);
		else
			ptr[i] = ptes[i].pte;
	}
}

static void xe_pt_abort_bind(struct xe_vma *vma,
			     struct xe_vm_pgtable_update *entries,
			     u32 num_entries)
{
	u32 i, j;

	for (i = 0; i < num_entries; i++) {
		if (!entries[i].pt_entries)
			continue;

		for (j = 0; j < entries[i].qwords; j++)
			xe_pt_destroy(entries[i].pt_entries[j].pt, xe_vma_vm(vma)->flags, NULL);
		kfree(entries[i].pt_entries);
	}
}

static void xe_pt_commit_locks_assert(struct xe_vma *vma)
{
	struct xe_vm *vm = xe_vma_vm(vma);

	lockdep_assert_held(&vm->lock);

	if (xe_vma_is_userptr(vma))
		lockdep_assert_held_read(&vm->userptr.notifier_lock);
	else if (!xe_vma_is_null(vma))
		dma_resv_assert_held(xe_vma_bo(vma)->ttm.base.resv);

	xe_vm_assert_held(vm);
}

static void xe_pt_commit_bind(struct xe_vma *vma,
			      struct xe_vm_pgtable_update *entries,
			      u32 num_entries, bool rebind,
			      struct llist_head *deferred)
{
	u32 i, j;

	xe_pt_commit_locks_assert(vma);

	for (i = 0; i < num_entries; i++) {
		struct xe_pt *pt = entries[i].pt;
		struct xe_pt_dir *pt_dir;

		if (!rebind)
			pt->num_live += entries[i].qwords;

		if (!pt->level) {
			kfree(entries[i].pt_entries);
			continue;
		}

		pt_dir = as_xe_pt_dir(pt);
		for (j = 0; j < entries[i].qwords; j++) {
			u32 j_ = j + entries[i].ofs;
			struct xe_pt *newpte = entries[i].pt_entries[j].pt;

			if (xe_pt_entry(pt_dir, j_))
				xe_pt_destroy(xe_pt_entry(pt_dir, j_),
					      xe_vma_vm(vma)->flags, deferred);

			pt_dir->children[j_] = &newpte->base;
		}
		kfree(entries[i].pt_entries);
	}
}

static int
xe_pt_prepare_bind(struct xe_tile *tile, struct xe_vma *vma,
		   struct xe_vm_pgtable_update *entries, u32 *num_entries)
{
	int err;

	*num_entries = 0;
	err = xe_pt_stage_bind(tile, vma, entries, num_entries);
	if (!err)
		xe_tile_assert(tile, *num_entries);
	else /* abort! */
		xe_pt_abort_bind(vma, entries, *num_entries);

	return err;
}

static void xe_vm_dbg_print_entries(struct xe_device *xe,
				    const struct xe_vm_pgtable_update *entries,
				    unsigned int num_entries)
#if (IS_ENABLED(CONFIG_DRM_XE_DEBUG_VM))
{
	unsigned int i;

	vm_dbg(&xe->drm, "%u entries to update\n", num_entries);
	for (i = 0; i < num_entries; i++) {
		const struct xe_vm_pgtable_update *entry = &entries[i];
		struct xe_pt *xe_pt = entry->pt;
		u64 page_size = 1ull << xe_pt_shift(xe_pt->level);
		u64 end;
		u64 start;

		xe_assert(xe, !entry->pt->is_compact);
		start = entry->ofs * page_size;
		end = start + page_size * entry->qwords;
		vm_dbg(&xe->drm,
		       "\t%u: Update level %u at (%u + %u) [%llx...%llx) f:%x\n",
		       i, xe_pt->level, entry->ofs, entry->qwords,
		       xe_pt_addr(xe_pt) + start, xe_pt_addr(xe_pt) + end, 0);
	}
}
#else
{}
#endif

#ifdef CONFIG_DRM_XE_USERPTR_INVAL_INJECT

static int xe_pt_userptr_inject_eagain(struct xe_userptr_vma *uvma)
{
	u32 divisor = uvma->userptr.divisor ? uvma->userptr.divisor : 2;
	static u32 count;

	if (count++ % divisor == divisor - 1) {
		struct xe_vm *vm = xe_vma_vm(&uvma->vma);

		uvma->userptr.divisor = divisor << 1;
		spin_lock(&vm->userptr.invalidated_lock);
		list_move_tail(&uvma->userptr.invalidate_link,
			       &vm->userptr.invalidated);
		spin_unlock(&vm->userptr.invalidated_lock);
		return true;
	}

	return false;
}

#else

static bool xe_pt_userptr_inject_eagain(struct xe_userptr_vma *uvma)
{
	return false;
}

#endif

/**
 * struct xe_pt_migrate_pt_update - Callback argument for pre-commit callbacks
 * @base: Base we derive from.
 * @bind: Whether this is a bind or an unbind operation. A bind operation
 *        makes the pre-commit callback error with -EAGAIN if it detects a
 *        pending invalidation.
 * @locked: Whether the pre-commit callback locked the userptr notifier lock
 *          and it needs unlocking.
 */
struct xe_pt_migrate_pt_update {
	struct xe_migrate_pt_update base;
	bool bind;
	bool locked;
};

/*
 * This function adds the needed dependencies to a page-table update job
 * to make sure racing jobs for separate bind engines don't race writing
 * to the same page-table range, wreaking havoc. Initially use a single
 * fence for the entire VM. An optimization would use smaller granularity.
 */
static int xe_pt_vm_dependencies(struct xe_sched_job *job,
				 struct xe_range_fence_tree *rftree,
				 u64 start, u64 last)
{
	struct xe_range_fence *rtfence;
	struct dma_fence *fence;
	int err;

	rtfence = xe_range_fence_tree_first(rftree, start, last);
	while (rtfence) {
		fence = rtfence->fence;

		if (!dma_fence_is_signaled(fence)) {
			/*
			 * Is this a CPU update? GPU is busy updating, so return
			 * an error
			 */
			if (!job)
				return -ETIME;

			dma_fence_get(fence);
			err = drm_sched_job_add_dependency(&job->drm, fence);
			if (err)
				return err;
		}

		rtfence = xe_range_fence_tree_next(rtfence, start, last);
	}

	return 0;
}

static int xe_pt_pre_commit(struct xe_migrate_pt_update *pt_update)
{
	struct xe_range_fence_tree *rftree =
		&xe_vma_vm(pt_update->vma)->rftree[pt_update->tile_id];

	return xe_pt_vm_dependencies(pt_update->job, rftree,
				     pt_update->start, pt_update->last);
}

static int xe_pt_userptr_pre_commit(struct xe_migrate_pt_update *pt_update)
{
	struct xe_pt_migrate_pt_update *userptr_update =
		container_of(pt_update, typeof(*userptr_update), base);
	struct xe_userptr_vma *uvma = to_userptr_vma(pt_update->vma);
	unsigned long notifier_seq = uvma->userptr.notifier_seq;
	struct xe_vm *vm = xe_vma_vm(&uvma->vma);
	int err = xe_pt_vm_dependencies(pt_update->job,
					&vm->rftree[pt_update->tile_id],
					pt_update->start,
					pt_update->last);

	if (err)
		return err;

	userptr_update->locked = false;

	/*
	 * Wait until nobody is running the invalidation notifier, and
	 * since we're exiting the loop holding the notifier lock,
	 * nobody can proceed invalidating either.
	 *
	 * Note that we don't update the vma->userptr.notifier_seq since
	 * we don't update the userptr pages.
	 */
	do {
		down_read(&vm->userptr.notifier_lock);
		if (!mmu_interval_read_retry(&uvma->userptr.notifier,
					     notifier_seq))
			break;

		up_read(&vm->userptr.notifier_lock);

		if (userptr_update->bind)
			return -EAGAIN;

		notifier_seq = mmu_interval_read_begin(&uvma->userptr.notifier);
	} while (true);

	/* Inject errors to test_whether they are handled correctly */
	if (userptr_update->bind && xe_pt_userptr_inject_eagain(uvma)) {
		up_read(&vm->userptr.notifier_lock);
		return -EAGAIN;
	}

	userptr_update->locked = true;

	return 0;
}

static const struct xe_migrate_pt_update_ops bind_ops = {
	.populate = xe_vm_populate_pgtable,
	.pre_commit = xe_pt_pre_commit,
};

static const struct xe_migrate_pt_update_ops userptr_bind_ops = {
	.populate = xe_vm_populate_pgtable,
	.pre_commit = xe_pt_userptr_pre_commit,
};

struct invalidation_fence {
	struct xe_gt_tlb_invalidation_fence base;
	struct xe_gt *gt;
	struct xe_vma *vma;
	struct dma_fence *fence;
	struct dma_fence_cb cb;
	struct work_struct work;
};

static const char *
invalidation_fence_get_driver_name(struct dma_fence *dma_fence)
{
	return "xe";
}

static const char *
invalidation_fence_get_timeline_name(struct dma_fence *dma_fence)
{
	return "invalidation_fence";
}

static const struct dma_fence_ops invalidation_fence_ops = {
	.get_driver_name = invalidation_fence_get_driver_name,
	.get_timeline_name = invalidation_fence_get_timeline_name,
};

static void invalidation_fence_cb(struct dma_fence *fence,
				  struct dma_fence_cb *cb)
{
	struct invalidation_fence *ifence =
		container_of(cb, struct invalidation_fence, cb);

	trace_xe_gt_tlb_invalidation_fence_cb(&ifence->base);
	if (!ifence->fence->error) {
		queue_work(system_wq, &ifence->work);
	} else {
		ifence->base.base.error = ifence->fence->error;
		dma_fence_signal(&ifence->base.base);
		dma_fence_put(&ifence->base.base);
	}
	dma_fence_put(ifence->fence);
}

static void invalidation_fence_work_func(struct work_struct *w)
{
	struct invalidation_fence *ifence =
		container_of(w, struct invalidation_fence, work);

	trace_xe_gt_tlb_invalidation_fence_work_func(&ifence->base);
	xe_gt_tlb_invalidation_vma(ifence->gt, &ifence->base, ifence->vma);
}

static int invalidation_fence_init(struct xe_gt *gt,
				   struct invalidation_fence *ifence,
				   struct dma_fence *fence,
				   struct xe_vma *vma)
{
	int ret;

	trace_xe_gt_tlb_invalidation_fence_create(&ifence->base);

	spin_lock_irq(&gt->tlb_invalidation.lock);
	dma_fence_init(&ifence->base.base, &invalidation_fence_ops,
		       &gt->tlb_invalidation.lock,
		       gt->tlb_invalidation.fence_context,
		       ++gt->tlb_invalidation.fence_seqno);
	spin_unlock_irq(&gt->tlb_invalidation.lock);

	INIT_LIST_HEAD(&ifence->base.link);

	dma_fence_get(&ifence->base.base);	/* Ref for caller */
	ifence->fence = fence;
	ifence->gt = gt;
	ifence->vma = vma;

	INIT_WORK(&ifence->work, invalidation_fence_work_func);
	ret = dma_fence_add_callback(fence, &ifence->cb, invalidation_fence_cb);
	if (ret == -ENOENT) {
		dma_fence_put(ifence->fence);	/* Usually dropped in CB */
		invalidation_fence_work_func(&ifence->work);
	} else if (ret) {
		dma_fence_put(&ifence->base.base);	/* Caller ref */
		dma_fence_put(&ifence->base.base);	/* Creation ref */
	}

	xe_gt_assert(gt, !ret || ret == -ENOENT);

	return ret && ret != -ENOENT ? ret : 0;
}

static void xe_pt_calc_rfence_interval(struct xe_vma *vma,
				       struct xe_pt_migrate_pt_update *update,
				       struct xe_vm_pgtable_update *entries,
				       u32 num_entries)
{
	int i, level = 0;

	for (i = 0; i < num_entries; i++) {
		const struct xe_vm_pgtable_update *entry = &entries[i];

		if (entry->pt->level > level)
			level = entry->pt->level;
	}

	/* Greedy (non-optimal) calculation but simple */
	update->base.start = ALIGN_DOWN(xe_vma_start(vma),
					0x1ull << xe_pt_shift(level));
	update->base.last = ALIGN(xe_vma_end(vma),
				  0x1ull << xe_pt_shift(level)) - 1;
}

/**
 * __xe_pt_bind_vma() - Build and connect a page-table tree for the vma
 * address range.
 * @tile: The tile to bind for.
 * @vma: The vma to bind.
 * @q: The exec_queue with which to do pipelined page-table updates.
 * @syncs: Entries to sync on before binding the built tree to the live vm tree.
 * @num_syncs: Number of @sync entries.
 * @rebind: Whether we're rebinding this vma to the same address range without
 * an unbind in-between.
 *
 * This function builds a page-table tree (see xe_pt_stage_bind() for more
 * information on page-table building), and the xe_vm_pgtable_update entries
 * abstracting the operations needed to attach it to the main vm tree. It
 * then takes the relevant locks and updates the metadata side of the main
 * vm tree and submits the operations for pipelined attachment of the
 * gpu page-table to the vm main tree, (which can be done either by the
 * cpu and the GPU).
 *
 * Return: A valid dma-fence representing the pipelined attachment operation
 * on success, an error pointer on error.
 */
struct dma_fence *
__xe_pt_bind_vma(struct xe_tile *tile, struct xe_vma *vma, struct xe_exec_queue *q,
		 struct xe_sync_entry *syncs, u32 num_syncs,
		 bool rebind)
{
	struct xe_vm_pgtable_update entries[XE_VM_MAX_LEVEL * 2 + 1];
	struct xe_pt_migrate_pt_update bind_pt_update = {
		.base = {
			.ops = xe_vma_is_userptr(vma) ? &userptr_bind_ops : &bind_ops,
			.vma = vma,
			.tile_id = tile->id,
		},
		.bind = true,
	};
	struct xe_vm *vm = xe_vma_vm(vma);
	u32 num_entries;
	struct dma_fence *fence;
	struct invalidation_fence *ifence = NULL;
	struct xe_range_fence *rfence;
	int err;

	bind_pt_update.locked = false;
	xe_bo_assert_held(xe_vma_bo(vma));
	xe_vm_assert_held(vm);

	vm_dbg(&xe_vma_vm(vma)->xe->drm,
	       "Preparing bind, with range [%llx...%llx) engine %p.\n",
	       xe_vma_start(vma), xe_vma_end(vma), q);

	err = xe_pt_prepare_bind(tile, vma, entries, &num_entries);
	if (err)
		goto err;
	xe_tile_assert(tile, num_entries <= ARRAY_SIZE(entries));

	xe_vm_dbg_print_entries(tile_to_xe(tile), entries, num_entries);
	xe_pt_calc_rfence_interval(vma, &bind_pt_update, entries,
				   num_entries);

	/*
	 * If rebind, we have to invalidate TLB on !LR vms to invalidate
	 * cached PTEs point to freed memory. on LR vms this is done
	 * automatically when the context is re-enabled by the rebind worker,
	 * or in fault mode it was invalidated on PTE zapping.
	 *
	 * If !rebind, and scratch enabled VMs, there is a chance the scratch
	 * PTE is already cached in the TLB so it needs to be invalidated.
	 * on !LR VMs this is done in the ring ops preceding a batch, but on
	 * non-faulting LR, in particular on user-space batch buffer chaining,
	 * it needs to be done here.
	 */
	if ((rebind && !xe_vm_in_lr_mode(vm) && !vm->batch_invalidate_tlb) ||
	    (!rebind && xe_vm_has_scratch(vm) && xe_vm_in_preempt_fence_mode(vm))) {
		ifence = kzalloc(sizeof(*ifence), GFP_KERNEL);
		if (!ifence)
			return ERR_PTR(-ENOMEM);
	}

	rfence = kzalloc(sizeof(*rfence), GFP_KERNEL);
	if (!rfence) {
		kfree(ifence);
		return ERR_PTR(-ENOMEM);
	}

	fence = xe_migrate_update_pgtables(tile->migrate,
					   vm, xe_vma_bo(vma), q,
					   entries, num_entries,
					   syncs, num_syncs,
					   &bind_pt_update.base);
	if (!IS_ERR(fence)) {
		bool last_munmap_rebind = vma->gpuva.flags & XE_VMA_LAST_REBIND;
		LLIST_HEAD(deferred);
		int err;

		err = xe_range_fence_insert(&vm->rftree[tile->id], rfence,
					    &xe_range_fence_kfree_ops,
					    bind_pt_update.base.start,
					    bind_pt_update.base.last, fence);
		if (err)
			dma_fence_wait(fence, false);

		/* TLB invalidation must be done before signaling rebind */
		if (ifence) {
			int err = invalidation_fence_init(tile->primary_gt, ifence, fence,
							  vma);
			if (err) {
				dma_fence_put(fence);
				kfree(ifence);
				return ERR_PTR(err);
			}
			fence = &ifence->base.base;
		}

		/* add shared fence now for pagetable delayed destroy */
		dma_resv_add_fence(xe_vm_resv(vm), fence, !rebind &&
				   last_munmap_rebind ?
				   DMA_RESV_USAGE_KERNEL :
				   DMA_RESV_USAGE_BOOKKEEP);

		if (!xe_vma_has_no_bo(vma) && !xe_vma_bo(vma)->vm)
			dma_resv_add_fence(xe_vma_bo(vma)->ttm.base.resv, fence,
					   DMA_RESV_USAGE_BOOKKEEP);
		xe_pt_commit_bind(vma, entries, num_entries, rebind,
				  bind_pt_update.locked ? &deferred : NULL);

		/* This vma is live (again?) now */
		vma->tile_present |= BIT(tile->id);

		if (bind_pt_update.locked) {
			to_userptr_vma(vma)->userptr.initial_bind = true;
			up_read(&vm->userptr.notifier_lock);
			xe_bo_put_commit(&deferred);
		}
		if (!rebind && last_munmap_rebind &&
		    xe_vm_in_preempt_fence_mode(vm))
			xe_vm_queue_rebind_worker(vm);
	} else {
		kfree(rfence);
		kfree(ifence);
		if (bind_pt_update.locked)
			up_read(&vm->userptr.notifier_lock);
		xe_pt_abort_bind(vma, entries, num_entries);
	}

	return fence;

err:
	return ERR_PTR(err);
}

struct xe_pt_stage_unbind_walk {
	/** @base: The pagewalk base-class. */
	struct xe_pt_walk base;

	/* Input parameters for the walk */
	/** @tile: The tile we're unbinding from. */
	struct xe_tile *tile;

	/**
	 * @modified_start: Walk range start, modified to include any
	 * shared pagetables that we're the only user of and can thus
	 * treat as private.
	 */
	u64 modified_start;
	/** @modified_end: Walk range start, modified like @modified_start. */
	u64 modified_end;

	/* Output */
	/* @wupd: Structure to track the page-table updates we're building */
	struct xe_walk_update wupd;
};

/*
 * Check whether this range is the only one populating this pagetable,
 * and in that case, update the walk range checks so that higher levels don't
 * view us as a shared pagetable.
 */
static bool xe_pt_check_kill(u64 addr, u64 next, unsigned int level,
			     const struct xe_pt *child,
			     enum page_walk_action *action,
			     struct xe_pt_walk *walk)
{
	struct xe_pt_stage_unbind_walk *xe_walk =
		container_of(walk, typeof(*xe_walk), base);
	unsigned int shift = walk->shifts[level];
	u64 size = 1ull << shift;

	if (IS_ALIGNED(addr, size) && IS_ALIGNED(next, size) &&
	    ((next - addr) >> shift) == child->num_live) {
		u64 size = 1ull << walk->shifts[level + 1];

		*action = ACTION_CONTINUE;

		if (xe_walk->modified_start >= addr)
			xe_walk->modified_start = round_down(addr, size);
		if (xe_walk->modified_end <= next)
			xe_walk->modified_end = round_up(next, size);

		return true;
	}

	return false;
}

static int xe_pt_stage_unbind_entry(struct xe_ptw *parent, pgoff_t offset,
				    unsigned int level, u64 addr, u64 next,
				    struct xe_ptw **child,
				    enum page_walk_action *action,
				    struct xe_pt_walk *walk)
{
	struct xe_pt *xe_child = container_of(*child, typeof(*xe_child), base);

	XE_WARN_ON(!*child);
	XE_WARN_ON(!level && xe_child->is_compact);

	xe_pt_check_kill(addr, next, level - 1, xe_child, action, walk);

	return 0;
}

static int
xe_pt_stage_unbind_post_descend(struct xe_ptw *parent, pgoff_t offset,
				unsigned int level, u64 addr, u64 next,
				struct xe_ptw **child,
				enum page_walk_action *action,
				struct xe_pt_walk *walk)
{
	struct xe_pt_stage_unbind_walk *xe_walk =
		container_of(walk, typeof(*xe_walk), base);
	struct xe_pt *xe_child = container_of(*child, typeof(*xe_child), base);
	pgoff_t end_offset;
	u64 size = 1ull << walk->shifts[--level];

	if (!IS_ALIGNED(addr, size))
		addr = xe_walk->modified_start;
	if (!IS_ALIGNED(next, size))
		next = xe_walk->modified_end;

	/* Parent == *child is the root pt. Don't kill it. */
	if (parent != *child &&
	    xe_pt_check_kill(addr, next, level, xe_child, action, walk))
		return 0;

	if (!xe_pt_nonshared_offsets(addr, next, level, walk, action, &offset,
				     &end_offset))
		return 0;

	(void)xe_pt_new_shared(&xe_walk->wupd, xe_child, offset, false);
	xe_walk->wupd.updates[level].update->qwords = end_offset - offset;

	return 0;
}

static const struct xe_pt_walk_ops xe_pt_stage_unbind_ops = {
	.pt_entry = xe_pt_stage_unbind_entry,
	.pt_post_descend = xe_pt_stage_unbind_post_descend,
};

/**
 * xe_pt_stage_unbind() - Build page-table update structures for an unbind
 * operation
 * @tile: The tile we're unbinding for.
 * @vma: The vma we're unbinding.
 * @entries: Caller-provided storage for the update structures.
 *
 * Builds page-table update structures for an unbind operation. The function
 * will attempt to remove all page-tables that we're the only user
 * of, and for that to work, the unbind operation must be committed in the
 * same critical section that blocks racing binds to the same page-table tree.
 *
 * Return: The number of entries used.
 */
static unsigned int xe_pt_stage_unbind(struct xe_tile *tile, struct xe_vma *vma,
				       struct xe_vm_pgtable_update *entries)
{
	struct xe_pt_stage_unbind_walk xe_walk = {
		.base = {
			.ops = &xe_pt_stage_unbind_ops,
			.shifts = xe_normal_pt_shifts,
			.max_level = XE_PT_HIGHEST_LEVEL,
		},
		.tile = tile,
		.modified_start = xe_vma_start(vma),
		.modified_end = xe_vma_end(vma),
		.wupd.entries = entries,
	};
	struct xe_pt *pt = xe_vma_vm(vma)->pt_root[tile->id];

	(void)xe_pt_walk_shared(&pt->base, pt->level, xe_vma_start(vma),
				xe_vma_end(vma), &xe_walk.base);

	return xe_walk.wupd.num_used_entries;
}

static void
xe_migrate_clear_pgtable_callback(struct xe_migrate_pt_update *pt_update,
				  struct xe_tile *tile, struct iosys_map *map,
				  void *ptr, u32 qword_ofs, u32 num_qwords,
				  const struct xe_vm_pgtable_update *update)
{
	struct xe_vma *vma = pt_update->vma;
	u64 empty = __xe_pt_empty_pte(tile, xe_vma_vm(vma), update->pt->level);
	int i;

	if (map && map->is_iomem)
		for (i = 0; i < num_qwords; ++i)
			xe_map_wr(tile_to_xe(tile), map, (qword_ofs + i) *
				  sizeof(u64), u64, empty);
	else if (map)
		memset64(map->vaddr + qword_ofs * sizeof(u64), empty,
			 num_qwords);
	else
		memset64(ptr, empty, num_qwords);
}

static void
xe_pt_commit_unbind(struct xe_vma *vma,
		    struct xe_vm_pgtable_update *entries, u32 num_entries,
		    struct llist_head *deferred)
{
	u32 j;

	xe_pt_commit_locks_assert(vma);

	for (j = 0; j < num_entries; ++j) {
		struct xe_vm_pgtable_update *entry = &entries[j];
		struct xe_pt *pt = entry->pt;

		pt->num_live -= entry->qwords;
		if (pt->level) {
			struct xe_pt_dir *pt_dir = as_xe_pt_dir(pt);
			u32 i;

			for (i = entry->ofs; i < entry->ofs + entry->qwords;
			     i++) {
				if (xe_pt_entry(pt_dir, i))
					xe_pt_destroy(xe_pt_entry(pt_dir, i),
						      xe_vma_vm(vma)->flags, deferred);

				pt_dir->children[i] = NULL;
			}
		}
	}
}

static const struct xe_migrate_pt_update_ops unbind_ops = {
	.populate = xe_migrate_clear_pgtable_callback,
	.pre_commit = xe_pt_pre_commit,
};

static const struct xe_migrate_pt_update_ops userptr_unbind_ops = {
	.populate = xe_migrate_clear_pgtable_callback,
	.pre_commit = xe_pt_userptr_pre_commit,
};

/**
 * __xe_pt_unbind_vma() - Disconnect and free a page-table tree for the vma
 * address range.
 * @tile: The tile to unbind for.
 * @vma: The vma to unbind.
 * @q: The exec_queue with which to do pipelined page-table updates.
 * @syncs: Entries to sync on before disconnecting the tree to be destroyed.
 * @num_syncs: Number of @sync entries.
 *
 * This function builds a the xe_vm_pgtable_update entries abstracting the
 * operations needed to detach the page-table tree to be destroyed from the
 * man vm tree.
 * It then takes the relevant locks and submits the operations for
 * pipelined detachment of the gpu page-table from  the vm main tree,
 * (which can be done either by the cpu and the GPU), Finally it frees the
 * detached page-table tree.
 *
 * Return: A valid dma-fence representing the pipelined detachment operation
 * on success, an error pointer on error.
 */
struct dma_fence *
__xe_pt_unbind_vma(struct xe_tile *tile, struct xe_vma *vma, struct xe_exec_queue *q,
		   struct xe_sync_entry *syncs, u32 num_syncs)
{
	struct xe_vm_pgtable_update entries[XE_VM_MAX_LEVEL * 2 + 1];
	struct xe_pt_migrate_pt_update unbind_pt_update = {
		.base = {
			.ops = xe_vma_is_userptr(vma) ? &userptr_unbind_ops :
			&unbind_ops,
			.vma = vma,
			.tile_id = tile->id,
		},
	};
	struct xe_vm *vm = xe_vma_vm(vma);
	u32 num_entries;
	struct dma_fence *fence = NULL;
	struct invalidation_fence *ifence;
	struct xe_range_fence *rfence;

	LLIST_HEAD(deferred);

	xe_bo_assert_held(xe_vma_bo(vma));
	xe_vm_assert_held(vm);

	vm_dbg(&xe_vma_vm(vma)->xe->drm,
	       "Preparing unbind, with range [%llx...%llx) engine %p.\n",
	       xe_vma_start(vma), xe_vma_end(vma), q);

	num_entries = xe_pt_stage_unbind(tile, vma, entries);
	xe_tile_assert(tile, num_entries <= ARRAY_SIZE(entries));

	xe_vm_dbg_print_entries(tile_to_xe(tile), entries, num_entries);
	xe_pt_calc_rfence_interval(vma, &unbind_pt_update, entries,
				   num_entries);

	ifence = kzalloc(sizeof(*ifence), GFP_KERNEL);
	if (!ifence)
		return ERR_PTR(-ENOMEM);

	rfence = kzalloc(sizeof(*rfence), GFP_KERNEL);
	if (!rfence) {
		kfree(ifence);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * Even if we were already evicted and unbind to destroy, we need to
	 * clear again here. The eviction may have updated pagetables at a
	 * lower level, because it needs to be more conservative.
	 */
	fence = xe_migrate_update_pgtables(tile->migrate,
					   vm, NULL, q ? q :
					   vm->q[tile->id],
					   entries, num_entries,
					   syncs, num_syncs,
					   &unbind_pt_update.base);
	if (!IS_ERR(fence)) {
		int err;

		err = xe_range_fence_insert(&vm->rftree[tile->id], rfence,
					    &xe_range_fence_kfree_ops,
					    unbind_pt_update.base.start,
					    unbind_pt_update.base.last, fence);
		if (err)
			dma_fence_wait(fence, false);

		/* TLB invalidation must be done before signaling unbind */
		err = invalidation_fence_init(tile->primary_gt, ifence, fence, vma);
		if (err) {
			dma_fence_put(fence);
			kfree(ifence);
			return ERR_PTR(err);
		}
		fence = &ifence->base.base;

		/* add shared fence now for pagetable delayed destroy */
		dma_resv_add_fence(xe_vm_resv(vm), fence,
				   DMA_RESV_USAGE_BOOKKEEP);

		/* This fence will be installed by caller when doing eviction */
		if (!xe_vma_has_no_bo(vma) && !xe_vma_bo(vma)->vm)
			dma_resv_add_fence(xe_vma_bo(vma)->ttm.base.resv, fence,
					   DMA_RESV_USAGE_BOOKKEEP);
		xe_pt_commit_unbind(vma, entries, num_entries,
				    unbind_pt_update.locked ? &deferred : NULL);
		vma->tile_present &= ~BIT(tile->id);
	} else {
		kfree(rfence);
		kfree(ifence);
	}

	if (!vma->tile_present)
		list_del_init(&vma->combined_links.rebind);

	if (unbind_pt_update.locked) {
		xe_tile_assert(tile, xe_vma_is_userptr(vma));

		if (!vma->tile_present) {
			spin_lock(&vm->userptr.invalidated_lock);
			list_del_init(&to_userptr_vma(vma)->userptr.invalidate_link);
			spin_unlock(&vm->userptr.invalidated_lock);
		}
		up_read(&vm->userptr.notifier_lock);
		xe_bo_put_commit(&deferred);
	}

	return fence;
}

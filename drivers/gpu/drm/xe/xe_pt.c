// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_migrate.h"
#include "xe_pt.h"
#include "xe_pt_types.h"
#include "xe_pt_walk.h"
#include "xe_vm.h"
#include "xe_res_cursor.h"
#include "xe_ttm_stolen_mgr.h"

struct xe_pt_dir {
	struct xe_pt pt;
	/** @dir: Directory structure for the xe_pt_walk functionality */
	struct xe_ptw_dir dir;
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
	return container_of(pt_dir->dir.entries[index], struct xe_pt, base);
}

/**
 * gen8_pde_encode() - Encode a page-table directory entry pointing to
 * another page-table.
 * @bo: The page-table bo of the page-table to point to.
 * @bo_offset: Offset in the page-table bo to point to.
 * @level: The cache level indicating the caching of @bo.
 *
 * TODO: Rename.
 *
 * Return: An encoded page directory entry. No errors.
 */
u64 gen8_pde_encode(struct xe_bo *bo, u64 bo_offset,
		    const enum xe_cache_level level)
{
	u64 pde;
	bool is_lmem;

	pde = xe_bo_addr(bo, bo_offset, GEN8_PAGE_SIZE, &is_lmem);
	pde |= GEN8_PAGE_PRESENT | GEN8_PAGE_RW;

	XE_WARN_ON(IS_DGFX(xe_bo_device(bo)) && !is_lmem);

	/* FIXME: I don't think the PPAT handling is correct for MTL */

	if (level != XE_CACHE_NONE)
		pde |= PPAT_CACHED_PDE;
	else
		pde |= PPAT_UNCACHED;

	return pde;
}

static dma_addr_t vma_addr(struct xe_vma *vma, u64 offset,
			   size_t page_size, bool *is_lmem)
{
	if (xe_vma_is_userptr(vma)) {
		struct xe_res_cursor cur;
		u64 page;

		*is_lmem = false;
		page = offset >> PAGE_SHIFT;
		offset &= (PAGE_SIZE - 1);

		xe_res_first_sg(vma->userptr.sg, page << PAGE_SHIFT, page_size,
				&cur);
		return xe_res_dma(&cur) + offset;
	} else {
		return xe_bo_addr(vma->bo, offset, page_size, is_lmem);
	}
}

static u64 __gen8_pte_encode(u64 pte, enum xe_cache_level cache, u32 flags,
			     u32 pt_level)
{
	pte |= GEN8_PAGE_PRESENT | GEN8_PAGE_RW;

	if (unlikely(flags & PTE_READ_ONLY))
		pte &= ~GEN8_PAGE_RW;

	/* FIXME: I don't think the PPAT handling is correct for MTL */

	switch (cache) {
	case XE_CACHE_NONE:
		pte |= PPAT_UNCACHED;
		break;
	case XE_CACHE_WT:
		pte |= PPAT_DISPLAY_ELLC;
		break;
	default:
		pte |= PPAT_CACHED;
		break;
	}

	if (pt_level == 1)
		pte |= GEN8_PDE_PS_2M;
	else if (pt_level == 2)
		pte |= GEN8_PDPE_PS_1G;

	/* XXX: Does hw support 1 GiB pages? */
	XE_BUG_ON(pt_level > 2);

	return pte;
}

/**
 * gen8_pte_encode() - Encode a page-table entry pointing to memory.
 * @vma: The vma representing the memory to point to.
 * @bo: If @vma is NULL, representing the memory to point to.
 * @offset: The offset into @vma or @bo.
 * @cache: The cache level indicating
 * @flags: Currently only supports PTE_READ_ONLY for read-only access.
 * @pt_level: The page-table level of the page-table into which the entry
 * is to be inserted.
 *
 * TODO: Rename.
 *
 * Return: An encoded page-table entry. No errors.
 */
u64 gen8_pte_encode(struct xe_vma *vma, struct xe_bo *bo,
		    u64 offset, enum xe_cache_level cache,
		    u32 flags, u32 pt_level)
{
	u64 pte;
	bool is_vram;

	if (vma)
		pte = vma_addr(vma, offset, GEN8_PAGE_SIZE, &is_vram);
	else
		pte = xe_bo_addr(bo, offset, GEN8_PAGE_SIZE, &is_vram);

	if (is_vram) {
		pte |= GEN12_PPGTT_PTE_LM;
		if (vma && vma->use_atomic_access_pte_bit)
			pte |= GEN12_USM_PPGTT_PTE_AE;
	}

	return __gen8_pte_encode(pte, cache, flags, pt_level);
}

static u64 __xe_pt_empty_pte(struct xe_gt *gt, struct xe_vm *vm,
			     unsigned int level)
{
	u8 id = gt->info.id;

	XE_BUG_ON(xe_gt_is_media_type(gt));

	if (!vm->scratch_bo[id])
		return 0;

	if (level == 0) {
		u64 empty = gen8_pte_encode(NULL, vm->scratch_bo[id], 0,
					    XE_CACHE_WB, 0, 0);
		if (vm->flags & XE_VM_FLAGS_64K)
			empty |= GEN12_PTE_PS64;

		return empty;
	} else {
		return gen8_pde_encode(vm->scratch_pt[id][level - 1]->bo, 0,
				       XE_CACHE_WB);
	}
}

/**
 * xe_pt_create() - Create a page-table.
 * @vm: The vm to create for.
 * @gt: The gt to create for.
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
struct xe_pt *xe_pt_create(struct xe_vm *vm, struct xe_gt *gt,
			   unsigned int level)
{
	struct xe_pt *pt;
	struct xe_bo *bo;
	size_t size;
	int err;

	size = !level ?  sizeof(struct xe_pt) : sizeof(struct xe_pt_dir) +
		GEN8_PDES * sizeof(struct xe_ptw *);
	pt = kzalloc(size, GFP_KERNEL);
	if (!pt)
		return ERR_PTR(-ENOMEM);

	bo = xe_bo_create_pin_map(vm->xe, gt, vm, SZ_4K,
				  ttm_bo_type_kernel,
				  XE_BO_CREATE_VRAM_IF_DGFX(gt) |
				  XE_BO_CREATE_IGNORE_MIN_PAGE_SIZE_BIT |
				  XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		goto err_kfree;
	}
	pt->bo = bo;
	pt->level = level;
	pt->base.dir = level ? &as_xe_pt_dir(pt)->dir : NULL;

	XE_BUG_ON(level > XE_VM_MAX_LEVEL);

	return pt;

err_kfree:
	kfree(pt);
	return ERR_PTR(err);
}

/**
 * xe_pt_populate_empty() - Populate a page-table bo with scratch- or zero
 * entries.
 * @gt: The gt the scratch pagetable of which to use.
 * @vm: The vm we populate for.
 * @pt: The pagetable the bo of which to initialize.
 *
 * Populate the page-table bo of @pt with entries pointing into the gt's
 * scratch page-table tree if any. Otherwise populate with zeros.
 */
void xe_pt_populate_empty(struct xe_gt *gt, struct xe_vm *vm,
			  struct xe_pt *pt)
{
	struct iosys_map *map = &pt->bo->vmap;
	u64 empty;
	int i;

	XE_BUG_ON(xe_gt_is_media_type(gt));

	if (!vm->scratch_bo[gt->info.id]) {
		/*
		 * FIXME: Some memory is allocated already allocated to zero?
		 * Find out which memory that is and avoid this memset...
		 */
		xe_map_memset(vm->xe, map, 0, 0, SZ_4K);
	} else {
		empty = __xe_pt_empty_pte(gt, vm, pt->level);
		for (i = 0; i < GEN8_PDES; i++)
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
	return GEN8_PTE_SHIFT + GEN8_PDE_SHIFT * level;
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

	XE_BUG_ON(!list_empty(&pt->bo->vmas));
	xe_bo_unpin(pt->bo);
	xe_bo_put_deferred(pt->bo, deferred);

	if (pt->level > 0 && pt->num_live) {
		struct xe_pt_dir *pt_dir = as_xe_pt_dir(pt);

		for (i = 0; i < GEN8_PDES; i++) {
			if (xe_pt_entry(pt_dir, i))
				xe_pt_destroy(xe_pt_entry(pt_dir, i), flags,
					      deferred);
		}
	}
	kfree(pt);
}

/**
 * xe_pt_create_scratch() - Setup a scratch memory pagetable tree for the
 * given gt and vm.
 * @xe: xe device.
 * @gt: gt to set up for.
 * @vm: vm to set up for.
 *
 * Sets up a pagetable tree with one page-table per level and a single
 * leaf bo. All pagetable entries point to the single page-table or,
 * for L0, the single bo one level below.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_pt_create_scratch(struct xe_device *xe, struct xe_gt *gt,
			 struct xe_vm *vm)
{
	u8 id = gt->info.id;
	int i;

	vm->scratch_bo[id] = xe_bo_create(xe, gt, vm, SZ_4K,
					  ttm_bo_type_kernel,
					  XE_BO_CREATE_VRAM_IF_DGFX(gt) |
					  XE_BO_CREATE_IGNORE_MIN_PAGE_SIZE_BIT |
					  XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(vm->scratch_bo[id]))
		return PTR_ERR(vm->scratch_bo[id]);
	xe_bo_pin(vm->scratch_bo[id]);

	for (i = 0; i < vm->pt_root[id]->level; i++) {
		vm->scratch_pt[id][i] = xe_pt_create(vm, gt, i);
		if (IS_ERR(vm->scratch_pt[id][i]))
			return PTR_ERR(vm->scratch_pt[id][i]);

		xe_pt_populate_empty(gt, vm, vm->scratch_pt[id][i]);
	}

	return 0;
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
	/** @gt: The gt we're building for. */
	struct xe_gt *gt;
	/** @cache: Desired cache level for the ptes */
	enum xe_cache_level cache;
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
	 * @pte_flags: Flags determining PTE setup. These are not flags
	 * encoded directly in the PTE. See @default_pte for those.
	 */
	u32 pte_flags;

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
		entry->pt_entries = kmalloc_array(GEN8_PDES,
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
			parent->base.dir->entries[offset] = &xe_child->base;

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

	/* Does the virtual range requested cover a huge pte? */
	if (!xe_pt_covers(addr, next, level, &xe_walk->base))
		return false;

	/* Does the DMA segment cover the whole pte? */
	if (next - xe_walk->va_curs_start > xe_walk->curs->size)
		return false;

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
	struct xe_pt *xe_parent = container_of(parent, typeof(*xe_parent), base);
	struct xe_pt *xe_child;
	bool covers;
	int ret = 0;
	u64 pte;

	/* Is this a leaf entry ?*/
	if (level == 0 || xe_pt_hugepte_possible(addr, next, level, xe_walk)) {
		struct xe_res_cursor *curs = xe_walk->curs;

		XE_WARN_ON(xe_walk->va_curs_start != addr);

		pte = __gen8_pte_encode(xe_res_dma(curs) + xe_walk->dma_offset,
					xe_walk->cache, xe_walk->pte_flags,
					level);
		pte |= xe_walk->default_pte;

		/*
		 * Set the GEN12_PTE_PS64 hint if possible, otherwise if
		 * this device *requires* 64K PTE size for VRAM, fail.
		 */
		if (level == 0 && !xe_parent->is_compact) {
			if (xe_pt_is_pte_ps64K(addr, next, xe_walk))
				pte |= GEN12_PTE_PS64;
			else if (XE_WARN_ON(xe_walk->needs_64K))
				return -EINVAL;
		}

		ret = xe_pt_insert_entry(xe_walk, xe_parent, offset, NULL, pte);
		if (unlikely(ret))
			return ret;

		xe_res_next(curs, next - addr);
		xe_walk->va_curs_start = next;
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

		xe_child = xe_pt_create(xe_walk->vm, xe_walk->gt, level - 1);
		if (IS_ERR(xe_child))
			return PTR_ERR(xe_child);

		xe_pt_set_addr(xe_child,
			       round_down(addr, 1ull << walk->shifts[level]));

		if (!covers)
			xe_pt_populate_empty(xe_walk->gt, xe_walk->vm, xe_child);

		*child = &xe_child->base;

		/*
		 * Prefer the compact pagetable layout for L0 if possible.
		 * TODO: Suballocate the pt bo to avoid wasting a lot of
		 * memory.
		 */
		if (GRAPHICS_VERx100(xe_walk->gt->xe) >= 1250 && level == 1 &&
		    covers && xe_pt_scan_64K(addr, next, xe_walk)) {
			walk->shifts = xe_compact_pt_shifts;
			flags |= GEN12_PDE_64K;
			xe_child->is_compact = true;
		}

		pte = gen8_pde_encode(xe_child->bo, 0, xe_walk->cache) | flags;
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
 * @gt: The gt we're building for.
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
xe_pt_stage_bind(struct xe_gt *gt, struct xe_vma *vma,
		 struct xe_vm_pgtable_update *entries, u32 *num_entries)
{
	struct xe_bo *bo = vma->bo;
	bool is_vram = !xe_vma_is_userptr(vma) && bo && xe_bo_is_vram(bo);
	struct xe_res_cursor curs;
	struct xe_pt_stage_bind_walk xe_walk = {
		.base = {
			.ops = &xe_pt_stage_bind_ops,
			.shifts = xe_normal_pt_shifts,
			.max_level = XE_PT_HIGHEST_LEVEL,
		},
		.vm = vma->vm,
		.gt = gt,
		.curs = &curs,
		.va_curs_start = vma->start,
		.pte_flags = vma->pte_flags,
		.wupd.entries = entries,
		.needs_64K = (vma->vm->flags & XE_VM_FLAGS_64K) && is_vram,
	};
	struct xe_pt *pt = vma->vm->pt_root[gt->info.id];
	int ret;

	if (is_vram) {
		xe_walk.default_pte = GEN12_PPGTT_PTE_LM;
		if (vma && vma->use_atomic_access_pte_bit)
			xe_walk.default_pte |= GEN12_USM_PPGTT_PTE_AE;
		xe_walk.dma_offset = gt->mem.vram.io_start -
			gt_to_xe(gt)->mem.vram.io_start;
		xe_walk.cache = XE_CACHE_WB;
	} else {
		if (!xe_vma_is_userptr(vma) && bo->flags & XE_BO_SCANOUT_BIT)
			xe_walk.cache = XE_CACHE_WT;
		else
			xe_walk.cache = XE_CACHE_WB;
	}
	if (!xe_vma_is_userptr(vma) && xe_bo_is_stolen(bo))
		xe_walk.dma_offset = xe_ttm_stolen_gpu_offset(xe_bo_device(bo));

	xe_bo_assert_held(bo);
	if (xe_vma_is_userptr(vma))
		xe_res_first_sg(vma->userptr.sg, 0, vma->end - vma->start + 1,
				&curs);
	else if (xe_bo_is_vram(bo) || xe_bo_is_stolen(bo))
		xe_res_first(bo->ttm.resource, vma->bo_offset,
			     vma->end - vma->start + 1, &curs);
	else
		xe_res_first_sg(xe_bo_get_sg(bo), vma->bo_offset,
				vma->end - vma->start + 1, &curs);

	ret = xe_pt_walk_range(&pt->base, pt->level, vma->start, vma->end + 1,
				&xe_walk.base);

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
	/** @gt: The gt we're building for */
	struct xe_gt *gt;

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

	XE_BUG_ON(!*child);
	XE_BUG_ON(!level && xe_child->is_compact);

	/*
	 * Note that we're called from an entry callback, and we're dealing
	 * with the child of that entry rather than the parent, so need to
	 * adjust level down.
	 */
	if (xe_pt_nonshared_offsets(addr, next, --level, walk, action, &offset,
				    &end_offset)) {
		xe_map_memset(gt_to_xe(xe_walk->gt), &xe_child->bo->vmap,
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
 * @gt: The gt we're zapping for.
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
bool xe_pt_zap_ptes(struct xe_gt *gt, struct xe_vma *vma)
{
	struct xe_pt_zap_ptes_walk xe_walk = {
		.base = {
			.ops = &xe_pt_zap_ptes_ops,
			.shifts = xe_normal_pt_shifts,
			.max_level = XE_PT_HIGHEST_LEVEL,
		},
		.gt = gt,
	};
	struct xe_pt *pt = vma->vm->pt_root[gt->info.id];

	if (!(vma->gt_present & BIT(gt->info.id)))
		return false;

	(void)xe_pt_walk_shared(&pt->base, pt->level, vma->start, vma->end + 1,
				 &xe_walk.base);

	return xe_walk.needs_invalidate;
}

static void
xe_vm_populate_pgtable(struct xe_migrate_pt_update *pt_update, struct xe_gt *gt,
		       struct iosys_map *map, void *data,
		       u32 qword_ofs, u32 num_qwords,
		       const struct xe_vm_pgtable_update *update)
{
	struct xe_pt_entry *ptes = update->pt_entries;
	u64 *ptr = data;
	u32 i;

	XE_BUG_ON(xe_gt_is_media_type(gt));

	for (i = 0; i < num_qwords; i++) {
		if (map)
			xe_map_wr(gt_to_xe(gt), map, (qword_ofs + i) *
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
			xe_pt_destroy(entries[i].pt_entries[j].pt, vma->vm->flags, NULL);
		kfree(entries[i].pt_entries);
	}
}

static void xe_pt_commit_locks_assert(struct xe_vma *vma)
{
	struct xe_vm *vm = vma->vm;

	lockdep_assert_held(&vm->lock);

	if (xe_vma_is_userptr(vma))
		lockdep_assert_held_read(&vm->userptr.notifier_lock);
	else
		dma_resv_assert_held(vma->bo->ttm.base.resv);

	dma_resv_assert_held(&vm->resv);
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
					      vma->vm->flags, deferred);

			pt_dir->dir.entries[j_] = &newpte->base;
		}
		kfree(entries[i].pt_entries);
	}
}

static int
xe_pt_prepare_bind(struct xe_gt *gt, struct xe_vma *vma,
		   struct xe_vm_pgtable_update *entries, u32 *num_entries,
		   bool rebind)
{
	int err;

	*num_entries = 0;
	err = xe_pt_stage_bind(gt, vma, entries, num_entries);
	if (!err)
		BUG_ON(!*num_entries);
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

		XE_BUG_ON(entry->pt->is_compact);
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

static int xe_pt_userptr_inject_eagain(struct xe_vma *vma)
{
	u32 divisor = vma->userptr.divisor ? vma->userptr.divisor : 2;
	static u32 count;

	if (count++ % divisor == divisor - 1) {
		struct xe_vm *vm = vma->vm;

		vma->userptr.divisor = divisor << 1;
		spin_lock(&vm->userptr.invalidated_lock);
		list_move_tail(&vma->userptr.invalidate_link,
			       &vm->userptr.invalidated);
		spin_unlock(&vm->userptr.invalidated_lock);
		return true;
	}

	return false;
}

#else

static bool xe_pt_userptr_inject_eagain(struct xe_vma *vma)
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

static int xe_pt_userptr_pre_commit(struct xe_migrate_pt_update *pt_update)
{
	struct xe_pt_migrate_pt_update *userptr_update =
		container_of(pt_update, typeof(*userptr_update), base);
	struct xe_vma *vma = pt_update->vma;
	unsigned long notifier_seq = vma->userptr.notifier_seq;
	struct xe_vm *vm = vma->vm;

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
		if (!mmu_interval_read_retry(&vma->userptr.notifier,
					     notifier_seq))
			break;

		up_read(&vm->userptr.notifier_lock);

		if (userptr_update->bind)
			return -EAGAIN;

		notifier_seq = mmu_interval_read_begin(&vma->userptr.notifier);
	} while (true);

	/* Inject errors to test_whether they are handled correctly */
	if (userptr_update->bind && xe_pt_userptr_inject_eagain(vma)) {
		up_read(&vm->userptr.notifier_lock);
		return -EAGAIN;
	}

	userptr_update->locked = true;

	return 0;
}

static const struct xe_migrate_pt_update_ops bind_ops = {
	.populate = xe_vm_populate_pgtable,
};

static const struct xe_migrate_pt_update_ops userptr_bind_ops = {
	.populate = xe_vm_populate_pgtable,
	.pre_commit = xe_pt_userptr_pre_commit,
};

/**
 * __xe_pt_bind_vma() - Build and connect a page-table tree for the vma
 * address range.
 * @gt: The gt to bind for.
 * @vma: The vma to bind.
 * @e: The engine with which to do pipelined page-table updates.
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
__xe_pt_bind_vma(struct xe_gt *gt, struct xe_vma *vma, struct xe_engine *e,
		 struct xe_sync_entry *syncs, u32 num_syncs,
		 bool rebind)
{
	struct xe_vm_pgtable_update entries[XE_VM_MAX_LEVEL * 2 + 1];
	struct xe_pt_migrate_pt_update bind_pt_update = {
		.base = {
			.ops = xe_vma_is_userptr(vma) ? &userptr_bind_ops : &bind_ops,
			.vma = vma,
		},
		.bind = true,
	};
	struct xe_vm *vm = vma->vm;
	u32 num_entries;
	struct dma_fence *fence;
	int err;

	bind_pt_update.locked = false;
	xe_bo_assert_held(vma->bo);
	xe_vm_assert_held(vm);
	XE_BUG_ON(xe_gt_is_media_type(gt));

	vm_dbg(&vma->vm->xe->drm,
	       "Preparing bind, with range [%llx...%llx) engine %p.\n",
	       vma->start, vma->end, e);

	err = xe_pt_prepare_bind(gt, vma, entries, &num_entries, rebind);
	if (err)
		goto err;
	XE_BUG_ON(num_entries > ARRAY_SIZE(entries));

	xe_vm_dbg_print_entries(gt_to_xe(gt), entries, num_entries);

	fence = xe_migrate_update_pgtables(gt->migrate,
					   vm, vma->bo,
					   e ? e : vm->eng[gt->info.id],
					   entries, num_entries,
					   syncs, num_syncs,
					   &bind_pt_update.base);
	if (!IS_ERR(fence)) {
		LLIST_HEAD(deferred);

		/* add shared fence now for pagetable delayed destroy */
		dma_resv_add_fence(&vm->resv, fence, !rebind &&
				   vma->last_munmap_rebind ?
				   DMA_RESV_USAGE_KERNEL :
				   DMA_RESV_USAGE_BOOKKEEP);

		if (!xe_vma_is_userptr(vma) && !vma->bo->vm)
			dma_resv_add_fence(vma->bo->ttm.base.resv, fence,
					   DMA_RESV_USAGE_BOOKKEEP);
		xe_pt_commit_bind(vma, entries, num_entries, rebind,
				  bind_pt_update.locked ? &deferred : NULL);

		/* This vma is live (again?) now */
		vma->gt_present |= BIT(gt->info.id);

		if (bind_pt_update.locked) {
			vma->userptr.initial_bind = true;
			up_read(&vm->userptr.notifier_lock);
			xe_bo_put_commit(&deferred);
		}
		if (!rebind && vma->last_munmap_rebind &&
		    xe_vm_in_compute_mode(vm))
			queue_work(vm->xe->ordered_wq,
				   &vm->preempt.rebind_work);
	} else {
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
	/** @gt: The gt we're unbinding from. */
	struct xe_gt *gt;

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

	XE_BUG_ON(!*child);
	XE_BUG_ON(!level && xe_child->is_compact);

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
 * @gt: The gt we're unbinding for.
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
static unsigned int xe_pt_stage_unbind(struct xe_gt *gt, struct xe_vma *vma,
				       struct xe_vm_pgtable_update *entries)
{
	struct xe_pt_stage_unbind_walk xe_walk = {
		.base = {
			.ops = &xe_pt_stage_unbind_ops,
			.shifts = xe_normal_pt_shifts,
			.max_level = XE_PT_HIGHEST_LEVEL,
		},
		.gt = gt,
		.modified_start = vma->start,
		.modified_end = vma->end + 1,
		.wupd.entries = entries,
	};
	struct xe_pt *pt = vma->vm->pt_root[gt->info.id];

	(void)xe_pt_walk_shared(&pt->base, pt->level, vma->start, vma->end + 1,
				 &xe_walk.base);

	return xe_walk.wupd.num_used_entries;
}

static void
xe_migrate_clear_pgtable_callback(struct xe_migrate_pt_update *pt_update,
				  struct xe_gt *gt, struct iosys_map *map,
				  void *ptr, u32 qword_ofs, u32 num_qwords,
				  const struct xe_vm_pgtable_update *update)
{
	struct xe_vma *vma = pt_update->vma;
	u64 empty = __xe_pt_empty_pte(gt, vma->vm, update->pt->level);
	int i;

	XE_BUG_ON(xe_gt_is_media_type(gt));

	if (map && map->is_iomem)
		for (i = 0; i < num_qwords; ++i)
			xe_map_wr(gt_to_xe(gt), map, (qword_ofs + i) *
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
						      vma->vm->flags, deferred);

				pt_dir->dir.entries[i] = NULL;
			}
		}
	}
}

static const struct xe_migrate_pt_update_ops unbind_ops = {
	.populate = xe_migrate_clear_pgtable_callback,
};

static const struct xe_migrate_pt_update_ops userptr_unbind_ops = {
	.populate = xe_migrate_clear_pgtable_callback,
	.pre_commit = xe_pt_userptr_pre_commit,
};

/**
 * __xe_pt_unbind_vma() - Disconnect and free a page-table tree for the vma
 * address range.
 * @gt: The gt to unbind for.
 * @vma: The vma to unbind.
 * @e: The engine with which to do pipelined page-table updates.
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
__xe_pt_unbind_vma(struct xe_gt *gt, struct xe_vma *vma, struct xe_engine *e,
		   struct xe_sync_entry *syncs, u32 num_syncs)
{
	struct xe_vm_pgtable_update entries[XE_VM_MAX_LEVEL * 2 + 1];
	struct xe_pt_migrate_pt_update unbind_pt_update = {
		.base = {
			.ops = xe_vma_is_userptr(vma) ? &userptr_unbind_ops :
			&unbind_ops,
			.vma = vma,
		},
	};
	struct xe_vm *vm = vma->vm;
	u32 num_entries;
	struct dma_fence *fence = NULL;
	LLIST_HEAD(deferred);

	xe_bo_assert_held(vma->bo);
	xe_vm_assert_held(vm);
	XE_BUG_ON(xe_gt_is_media_type(gt));

	vm_dbg(&vma->vm->xe->drm,
	       "Preparing unbind, with range [%llx...%llx) engine %p.\n",
	       vma->start, vma->end, e);

	num_entries = xe_pt_stage_unbind(gt, vma, entries);
	XE_BUG_ON(num_entries > ARRAY_SIZE(entries));

	xe_vm_dbg_print_entries(gt_to_xe(gt), entries, num_entries);

	/*
	 * Even if we were already evicted and unbind to destroy, we need to
	 * clear again here. The eviction may have updated pagetables at a
	 * lower level, because it needs to be more conservative.
	 */
	fence = xe_migrate_update_pgtables(gt->migrate,
					   vm, NULL, e ? e :
					   vm->eng[gt->info.id],
					   entries, num_entries,
					   syncs, num_syncs,
					   &unbind_pt_update.base);
	if (!IS_ERR(fence)) {
		/* add shared fence now for pagetable delayed destroy */
		dma_resv_add_fence(&vm->resv, fence,
				   DMA_RESV_USAGE_BOOKKEEP);

		/* This fence will be installed by caller when doing eviction */
		if (!xe_vma_is_userptr(vma) && !vma->bo->vm)
			dma_resv_add_fence(vma->bo->ttm.base.resv, fence,
					   DMA_RESV_USAGE_BOOKKEEP);
		xe_pt_commit_unbind(vma, entries, num_entries,
				    unbind_pt_update.locked ? &deferred : NULL);
		vma->gt_present &= ~BIT(gt->info.id);
	}

	if (!vma->gt_present)
		list_del_init(&vma->rebind_link);

	if (unbind_pt_update.locked) {
		XE_WARN_ON(!xe_vma_is_userptr(vma));

		if (!vma->gt_present) {
			spin_lock(&vm->userptr.invalidated_lock);
			list_del_init(&vma->userptr.invalidate_link);
			spin_unlock(&vm->userptr.invalidated_lock);
		}
		up_read(&vm->userptr.notifier_lock);
		xe_bo_put_commit(&deferred);
	}

	return fence;
}

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_ggtt.h"

#include <linux/fault-inject.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/sizes.h>

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/intel/i915_drm.h>
#include <generated/xe_wa_oob.h>

#include "regs/xe_gt_regs.h"
#include "regs/xe_gtt_defs.h"
#include "regs/xe_regs.h"
#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_gt_sriov_vf.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_pm.h"
#include "xe_sriov.h"
#include "xe_wa.h"
#include "xe_wopcm.h"

/**
 * DOC: Global Graphics Translation Table (GGTT)
 *
 * Xe GGTT implements the support for a Global Virtual Address space that is used
 * for resources that are accessible to privileged (i.e. kernel-mode) processes,
 * and not tied to a specific user-level process. For example, the Graphics
 * micro-Controller (GuC) and Display Engine (if present) utilize this Global
 * address space.
 *
 * The Global GTT (GGTT) translates from the Global virtual address to a physical
 * address that can be accessed by HW. The GGTT is a flat, single-level table.
 *
 * Xe implements a simplified version of the GGTT specifically managing only a
 * certain range of it that goes from the Write Once Protected Content Memory (WOPCM)
 * Layout to a predefined GUC_GGTT_TOP. This approach avoids complications related to
 * the GuC (Graphics Microcontroller) hardware limitations. The GuC address space
 * is limited on both ends of the GGTT, because the GuC shim HW redirects
 * accesses to those addresses to other HW areas instead of going through the
 * GGTT. On the bottom end, the GuC can't access offsets below the WOPCM size,
 * while on the top side the limit is fixed at GUC_GGTT_TOP. To keep things
 * simple, instead of checking each object to see if they are accessed by GuC or
 * not, we just exclude those areas from the allocator. Additionally, to simplify
 * the driver load, we use the maximum WOPCM size in this logic instead of the
 * programmed one, so we don't need to wait until the actual size to be
 * programmed is determined (which requires FW fetch) before initializing the
 * GGTT. These simplifications might waste space in the GGTT (about 20-25 MBs
 * depending on the platform) but we can live with this. Another benefit of this
 * is the GuC bootrom can't access anything below the WOPCM max size so anything
 * the bootrom needs to access (e.g. a RSA key) needs to be placed in the GGTT
 * above the WOPCM max size. Starting the GGTT allocations above the WOPCM max
 * give us the correct placement for free.
 */

static u64 xelp_ggtt_pte_encode_bo(struct xe_bo *bo, u64 bo_offset,
				   u16 pat_index)
{
	u64 pte;

	pte = xe_bo_addr(bo, bo_offset, XE_PAGE_SIZE);
	pte |= XE_PAGE_PRESENT;

	if (xe_bo_is_vram(bo) || xe_bo_is_stolen_devmem(bo))
		pte |= XE_GGTT_PTE_DM;

	return pte;
}

static u64 xelpg_ggtt_pte_encode_bo(struct xe_bo *bo, u64 bo_offset,
				    u16 pat_index)
{
	struct xe_device *xe = xe_bo_device(bo);
	u64 pte;

	pte = xelp_ggtt_pte_encode_bo(bo, bo_offset, pat_index);

	xe_assert(xe, pat_index <= 3);

	if (pat_index & BIT(0))
		pte |= XELPG_GGTT_PTE_PAT0;

	if (pat_index & BIT(1))
		pte |= XELPG_GGTT_PTE_PAT1;

	return pte;
}

static unsigned int probe_gsm_size(struct pci_dev *pdev)
{
	u16 gmch_ctl, ggms;

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &gmch_ctl);
	ggms = (gmch_ctl >> BDW_GMCH_GGMS_SHIFT) & BDW_GMCH_GGMS_MASK;
	return ggms ? SZ_1M << ggms : 0;
}

static void ggtt_update_access_counter(struct xe_ggtt *ggtt)
{
	struct xe_tile *tile = ggtt->tile;
	struct xe_gt *affected_gt = XE_WA(tile->primary_gt, 22019338487) ?
		tile->primary_gt : tile->media_gt;
	struct xe_mmio *mmio = &affected_gt->mmio;
	u32 max_gtt_writes = XE_WA(ggtt->tile->primary_gt, 22019338487) ? 1100 : 63;
	/*
	 * Wa_22019338487: GMD_ID is a RO register, a dummy write forces gunit
	 * to wait for completion of prior GTT writes before letting this through.
	 * This needs to be done for all GGTT writes originating from the CPU.
	 */
	lockdep_assert_held(&ggtt->lock);

	if ((++ggtt->access_count % max_gtt_writes) == 0) {
		xe_mmio_write32(mmio, GMD_ID, 0x0);
		ggtt->access_count = 0;
	}
}

static void xe_ggtt_set_pte(struct xe_ggtt *ggtt, u64 addr, u64 pte)
{
	xe_tile_assert(ggtt->tile, !(addr & XE_PTE_MASK));
	xe_tile_assert(ggtt->tile, addr < ggtt->size);

	writeq(pte, &ggtt->gsm[addr >> XE_PTE_SHIFT]);
}

static void xe_ggtt_set_pte_and_flush(struct xe_ggtt *ggtt, u64 addr, u64 pte)
{
	xe_ggtt_set_pte(ggtt, addr, pte);
	ggtt_update_access_counter(ggtt);
}

static void xe_ggtt_clear(struct xe_ggtt *ggtt, u64 start, u64 size)
{
	u16 pat_index = tile_to_xe(ggtt->tile)->pat.idx[XE_CACHE_WB];
	u64 end = start + size - 1;
	u64 scratch_pte;

	xe_tile_assert(ggtt->tile, start < end);

	if (ggtt->scratch)
		scratch_pte = ggtt->pt_ops->pte_encode_bo(ggtt->scratch, 0,
							  pat_index);
	else
		scratch_pte = 0;

	while (start < end) {
		ggtt->pt_ops->ggtt_set_pte(ggtt, start, scratch_pte);
		start += XE_PAGE_SIZE;
	}
}

static void ggtt_fini_early(struct drm_device *drm, void *arg)
{
	struct xe_ggtt *ggtt = arg;

	destroy_workqueue(ggtt->wq);
	mutex_destroy(&ggtt->lock);
	drm_mm_takedown(&ggtt->mm);
}

static void ggtt_fini(void *arg)
{
	struct xe_ggtt *ggtt = arg;

	ggtt->scratch = NULL;
}

static void primelockdep(struct xe_ggtt *ggtt)
{
	if (!IS_ENABLED(CONFIG_LOCKDEP))
		return;

	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&ggtt->lock);
	fs_reclaim_release(GFP_KERNEL);
}

static const struct xe_ggtt_pt_ops xelp_pt_ops = {
	.pte_encode_bo = xelp_ggtt_pte_encode_bo,
	.ggtt_set_pte = xe_ggtt_set_pte,
};

static const struct xe_ggtt_pt_ops xelpg_pt_ops = {
	.pte_encode_bo = xelpg_ggtt_pte_encode_bo,
	.ggtt_set_pte = xe_ggtt_set_pte,
};

static const struct xe_ggtt_pt_ops xelpg_pt_wa_ops = {
	.pte_encode_bo = xelpg_ggtt_pte_encode_bo,
	.ggtt_set_pte = xe_ggtt_set_pte_and_flush,
};

/**
 * xe_ggtt_init_early - Early GGTT initialization
 * @ggtt: the &xe_ggtt to be initialized
 *
 * It allows to create new mappings usable by the GuC.
 * Mappings are not usable by the HW engines, as it doesn't have scratch nor
 * initial clear done to it yet. That will happen in the regular, non-early
 * GGTT initialization.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_init_early(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	unsigned int gsm_size;
	int err;

	if (IS_SRIOV_VF(xe))
		gsm_size = SZ_8M; /* GGTT is expected to be 4GiB */
	else
		gsm_size = probe_gsm_size(pdev);

	if (gsm_size == 0) {
		drm_err(&xe->drm, "Hardware reported no preallocated GSM\n");
		return -ENOMEM;
	}

	ggtt->gsm = ggtt->tile->mmio.regs + SZ_8M;
	ggtt->size = (gsm_size / 8) * (u64) XE_PAGE_SIZE;

	if (IS_DGFX(xe) && xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
		ggtt->flags |= XE_GGTT_FLAGS_64K;

	if (ggtt->size > GUC_GGTT_TOP)
		ggtt->size = GUC_GGTT_TOP;

	if (GRAPHICS_VERx100(xe) >= 1270)
		ggtt->pt_ops = (ggtt->tile->media_gt &&
			       XE_WA(ggtt->tile->media_gt, 22019338487)) ||
			       XE_WA(ggtt->tile->primary_gt, 22019338487) ?
			       &xelpg_pt_wa_ops : &xelpg_pt_ops;
	else
		ggtt->pt_ops = &xelp_pt_ops;

	ggtt->wq = alloc_workqueue("xe-ggtt-wq", 0, WQ_MEM_RECLAIM);

	drm_mm_init(&ggtt->mm, xe_wopcm_size(xe),
		    ggtt->size - xe_wopcm_size(xe));
	mutex_init(&ggtt->lock);
	primelockdep(ggtt);

	err = drmm_add_action_or_reset(&xe->drm, ggtt_fini_early, ggtt);
	if (err)
		return err;

	if (IS_SRIOV_VF(xe)) {
		err = xe_gt_sriov_vf_prepare_ggtt(xe_tile_get_gt(ggtt->tile, 0));
		if (err)
			return err;
	}

	return 0;
}
ALLOW_ERROR_INJECTION(xe_ggtt_init_early, ERRNO); /* See xe_pci_probe() */

static void xe_ggtt_invalidate(struct xe_ggtt *ggtt);

static void xe_ggtt_initial_clear(struct xe_ggtt *ggtt)
{
	struct drm_mm_node *hole;
	u64 start, end;

	/* Display may have allocated inside ggtt, so be careful with clearing here */
	mutex_lock(&ggtt->lock);
	drm_mm_for_each_hole(hole, &ggtt->mm, start, end)
		xe_ggtt_clear(ggtt, start, end - start);

	xe_ggtt_invalidate(ggtt);
	mutex_unlock(&ggtt->lock);
}

static void ggtt_node_remove(struct xe_ggtt_node *node)
{
	struct xe_ggtt *ggtt = node->ggtt;
	struct xe_device *xe = tile_to_xe(ggtt->tile);
	bool bound;
	int idx;

	bound = drm_dev_enter(&xe->drm, &idx);

	mutex_lock(&ggtt->lock);
	if (bound)
		xe_ggtt_clear(ggtt, node->base.start, node->base.size);
	drm_mm_remove_node(&node->base);
	node->base.size = 0;
	mutex_unlock(&ggtt->lock);

	if (!bound)
		goto free_node;

	if (node->invalidate_on_remove)
		xe_ggtt_invalidate(ggtt);

	drm_dev_exit(idx);

free_node:
	xe_ggtt_node_fini(node);
}

static void ggtt_node_remove_work_func(struct work_struct *work)
{
	struct xe_ggtt_node *node = container_of(work, typeof(*node),
						 delayed_removal_work);
	struct xe_device *xe = tile_to_xe(node->ggtt->tile);

	xe_pm_runtime_get(xe);
	ggtt_node_remove(node);
	xe_pm_runtime_put(xe);
}

/**
 * xe_ggtt_node_remove - Remove a &xe_ggtt_node from the GGTT
 * @node: the &xe_ggtt_node to be removed
 * @invalidate: if node needs invalidation upon removal
 */
void xe_ggtt_node_remove(struct xe_ggtt_node *node, bool invalidate)
{
	struct xe_ggtt *ggtt;
	struct xe_device *xe;

	if (!node || !node->ggtt)
		return;

	ggtt = node->ggtt;
	xe = tile_to_xe(ggtt->tile);

	node->invalidate_on_remove = invalidate;

	if (xe_pm_runtime_get_if_active(xe)) {
		ggtt_node_remove(node);
		xe_pm_runtime_put(xe);
	} else {
		queue_work(ggtt->wq, &node->delayed_removal_work);
	}
}

/**
 * xe_ggtt_init - Regular non-early GGTT initialization
 * @ggtt: the &xe_ggtt to be initialized
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_init(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);
	unsigned int flags;
	int err;

	/*
	 * So we don't need to worry about 64K GGTT layout when dealing with
	 * scratch entries, rather keep the scratch page in system memory on
	 * platforms where 64K pages are needed for VRAM.
	 */
	flags = XE_BO_FLAG_PINNED;
	if (ggtt->flags & XE_GGTT_FLAGS_64K)
		flags |= XE_BO_FLAG_SYSTEM;
	else
		flags |= XE_BO_FLAG_VRAM_IF_DGFX(ggtt->tile);

	ggtt->scratch = xe_managed_bo_create_pin_map(xe, ggtt->tile, XE_PAGE_SIZE, flags);
	if (IS_ERR(ggtt->scratch)) {
		err = PTR_ERR(ggtt->scratch);
		goto err;
	}

	xe_map_memset(xe, &ggtt->scratch->vmap, 0, 0, ggtt->scratch->size);

	xe_ggtt_initial_clear(ggtt);

	return devm_add_action_or_reset(xe->drm.dev, ggtt_fini, ggtt);
err:
	ggtt->scratch = NULL;
	return err;
}

static void ggtt_invalidate_gt_tlb(struct xe_gt *gt)
{
	int err;

	if (!gt)
		return;

	err = xe_gt_tlb_invalidation_ggtt(gt);
	if (err)
		drm_warn(&gt_to_xe(gt)->drm, "xe_gt_tlb_invalidation_ggtt error=%d", err);
}

static void xe_ggtt_invalidate(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);

	/*
	 * XXX: Barrier for GGTT pages. Unsure exactly why this required but
	 * without this LNL is having issues with the GuC reading scratch page
	 * vs. correct GGTT page. Not particularly a hot code path so blindly
	 * do a mmio read here which results in GuC reading correct GGTT page.
	 */
	xe_mmio_read32(xe_root_tile_mmio(xe), VF_CAP_REG);

	/* Each GT in a tile has its own TLB to cache GGTT lookups */
	ggtt_invalidate_gt_tlb(ggtt->tile->primary_gt);
	ggtt_invalidate_gt_tlb(ggtt->tile->media_gt);
}

static void xe_ggtt_dump_node(struct xe_ggtt *ggtt,
			      const struct drm_mm_node *node, const char *description)
{
	char buf[10];

	if (IS_ENABLED(CONFIG_DRM_XE_DEBUG)) {
		string_get_size(node->size, 1, STRING_UNITS_2, buf, sizeof(buf));
		xe_gt_dbg(ggtt->tile->primary_gt, "GGTT %#llx-%#llx (%s) %s\n",
			  node->start, node->start + node->size, buf, description);
	}
}

/**
 * xe_ggtt_node_insert_balloon - prevent allocation of specified GGTT addresses
 * @node: the &xe_ggtt_node to hold reserved GGTT node
 * @start: the starting GGTT address of the reserved region
 * @end: then end GGTT address of the reserved region
 *
 * Use xe_ggtt_node_remove_balloon() to release a reserved GGTT node.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_node_insert_balloon(struct xe_ggtt_node *node, u64 start, u64 end)
{
	struct xe_ggtt *ggtt = node->ggtt;
	int err;

	xe_tile_assert(ggtt->tile, start < end);
	xe_tile_assert(ggtt->tile, IS_ALIGNED(start, XE_PAGE_SIZE));
	xe_tile_assert(ggtt->tile, IS_ALIGNED(end, XE_PAGE_SIZE));
	xe_tile_assert(ggtt->tile, !drm_mm_node_allocated(&node->base));

	node->base.color = 0;
	node->base.start = start;
	node->base.size = end - start;

	mutex_lock(&ggtt->lock);
	err = drm_mm_reserve_node(&ggtt->mm, &node->base);
	mutex_unlock(&ggtt->lock);

	if (xe_gt_WARN(ggtt->tile->primary_gt, err,
		       "Failed to balloon GGTT %#llx-%#llx (%pe)\n",
		       node->base.start, node->base.start + node->base.size, ERR_PTR(err)))
		return err;

	xe_ggtt_dump_node(ggtt, &node->base, "balloon");
	return 0;
}

/**
 * xe_ggtt_node_remove_balloon - release a reserved GGTT region
 * @node: the &xe_ggtt_node with reserved GGTT region
 *
 * See xe_ggtt_node_insert_balloon() for details.
 */
void xe_ggtt_node_remove_balloon(struct xe_ggtt_node *node)
{
	if (!node || !node->ggtt)
		return;

	if (!drm_mm_node_allocated(&node->base))
		goto free_node;

	xe_ggtt_dump_node(node->ggtt, &node->base, "remove-balloon");

	mutex_lock(&node->ggtt->lock);
	drm_mm_remove_node(&node->base);
	mutex_unlock(&node->ggtt->lock);

free_node:
	xe_ggtt_node_fini(node);
}

/**
 * xe_ggtt_node_insert_locked - Locked version to insert a &xe_ggtt_node into the GGTT
 * @node: the &xe_ggtt_node to be inserted
 * @size: size of the node
 * @align: alignment constrain of the node
 * @mm_flags: flags to control the node behavior
 *
 * It cannot be called without first having called xe_ggtt_init() once.
 * To be used in cases where ggtt->lock is already taken.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_node_insert_locked(struct xe_ggtt_node *node,
			       u32 size, u32 align, u32 mm_flags)
{
	return drm_mm_insert_node_generic(&node->ggtt->mm, &node->base, size, align, 0,
					  mm_flags);
}

/**
 * xe_ggtt_node_insert - Insert a &xe_ggtt_node into the GGTT
 * @node: the &xe_ggtt_node to be inserted
 * @size: size of the node
 * @align: alignment constrain of the node
 *
 * It cannot be called without first having called xe_ggtt_init() once.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_node_insert(struct xe_ggtt_node *node, u32 size, u32 align)
{
	int ret;

	if (!node || !node->ggtt)
		return -ENOENT;

	mutex_lock(&node->ggtt->lock);
	ret = xe_ggtt_node_insert_locked(node, size, align,
					 DRM_MM_INSERT_HIGH);
	mutex_unlock(&node->ggtt->lock);

	return ret;
}

/**
 * xe_ggtt_node_init - Initialize %xe_ggtt_node struct
 * @ggtt: the &xe_ggtt where the new node will later be inserted/reserved.
 *
 * This function will allocated the struct %xe_ggtt_node and return it's pointer.
 * This struct will then be freed after the node removal upon xe_ggtt_node_remove()
 * or xe_ggtt_node_remove_balloon().
 * Having %xe_ggtt_node struct allocated doesn't mean that the node is already allocated
 * in GGTT. Only the xe_ggtt_node_insert(), xe_ggtt_node_insert_locked(),
 * xe_ggtt_node_insert_balloon() will ensure the node is inserted or reserved in GGTT.
 *
 * Return: A pointer to %xe_ggtt_node struct on success. An ERR_PTR otherwise.
 **/
struct xe_ggtt_node *xe_ggtt_node_init(struct xe_ggtt *ggtt)
{
	struct xe_ggtt_node *node = kzalloc(sizeof(*node), GFP_NOFS);

	if (!node)
		return ERR_PTR(-ENOMEM);

	INIT_WORK(&node->delayed_removal_work, ggtt_node_remove_work_func);
	node->ggtt = ggtt;

	return node;
}

/**
 * xe_ggtt_node_fini - Forcebly finalize %xe_ggtt_node struct
 * @node: the &xe_ggtt_node to be freed
 *
 * If anything went wrong with either xe_ggtt_node_insert(), xe_ggtt_node_insert_locked(),
 * or xe_ggtt_node_insert_balloon(); and this @node is not going to be reused, then,
 * this function needs to be called to free the %xe_ggtt_node struct
 **/
void xe_ggtt_node_fini(struct xe_ggtt_node *node)
{
	kfree(node);
}

/**
 * xe_ggtt_node_allocated - Check if node is allocated in GGTT
 * @node: the &xe_ggtt_node to be inspected
 *
 * Return: True if allocated, False otherwise.
 */
bool xe_ggtt_node_allocated(const struct xe_ggtt_node *node)
{
	if (!node || !node->ggtt)
		return false;

	return drm_mm_node_allocated(&node->base);
}

/**
 * xe_ggtt_map_bo - Map the BO into GGTT
 * @ggtt: the &xe_ggtt where node will be mapped
 * @bo: the &xe_bo to be mapped
 */
void xe_ggtt_map_bo(struct xe_ggtt *ggtt, struct xe_bo *bo)
{
	u16 cache_mode = bo->flags & XE_BO_FLAG_NEEDS_UC ? XE_CACHE_NONE : XE_CACHE_WB;
	u16 pat_index = tile_to_xe(ggtt->tile)->pat.idx[cache_mode];
	u64 start;
	u64 offset, pte;

	if (XE_WARN_ON(!bo->ggtt_node[ggtt->tile->id]))
		return;

	start = bo->ggtt_node[ggtt->tile->id]->base.start;

	for (offset = 0; offset < bo->size; offset += XE_PAGE_SIZE) {
		pte = ggtt->pt_ops->pte_encode_bo(bo, offset, pat_index);
		ggtt->pt_ops->ggtt_set_pte(ggtt, start + offset, pte);
	}
}

static int __xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo,
				  u64 start, u64 end)
{
	u64 alignment = bo->min_align > 0 ? bo->min_align : XE_PAGE_SIZE;
	u8 tile_id = ggtt->tile->id;
	int err;

	if (xe_bo_is_vram(bo) && ggtt->flags & XE_GGTT_FLAGS_64K)
		alignment = SZ_64K;

	if (XE_WARN_ON(bo->ggtt_node[tile_id])) {
		/* Someone's already inserted this BO in the GGTT */
		xe_tile_assert(ggtt->tile, bo->ggtt_node[tile_id]->base.size == bo->size);
		return 0;
	}

	err = xe_bo_validate(bo, NULL, false);
	if (err)
		return err;

	xe_pm_runtime_get_noresume(tile_to_xe(ggtt->tile));

	bo->ggtt_node[tile_id] = xe_ggtt_node_init(ggtt);
	if (IS_ERR(bo->ggtt_node[tile_id])) {
		err = PTR_ERR(bo->ggtt_node[tile_id]);
		bo->ggtt_node[tile_id] = NULL;
		goto out;
	}

	mutex_lock(&ggtt->lock);
	err = drm_mm_insert_node_in_range(&ggtt->mm, &bo->ggtt_node[tile_id]->base,
					  bo->size, alignment, 0, start, end, 0);
	if (err) {
		xe_ggtt_node_fini(bo->ggtt_node[tile_id]);
		bo->ggtt_node[tile_id] = NULL;
	} else {
		xe_ggtt_map_bo(ggtt, bo);
	}
	mutex_unlock(&ggtt->lock);

	if (!err && bo->flags & XE_BO_FLAG_GGTT_INVALIDATE)
		xe_ggtt_invalidate(ggtt);

out:
	xe_pm_runtime_put(tile_to_xe(ggtt->tile));

	return err;
}

/**
 * xe_ggtt_insert_bo_at - Insert BO at a specific GGTT space
 * @ggtt: the &xe_ggtt where bo will be inserted
 * @bo: the &xe_bo to be inserted
 * @start: address where it will be inserted
 * @end: end of the range where it will be inserted
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo,
			 u64 start, u64 end)
{
	return __xe_ggtt_insert_bo_at(ggtt, bo, start, end);
}

/**
 * xe_ggtt_insert_bo - Insert BO into GGTT
 * @ggtt: the &xe_ggtt where bo will be inserted
 * @bo: the &xe_bo to be inserted
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_insert_bo(struct xe_ggtt *ggtt, struct xe_bo *bo)
{
	return __xe_ggtt_insert_bo_at(ggtt, bo, 0, U64_MAX);
}

/**
 * xe_ggtt_remove_bo - Remove a BO from the GGTT
 * @ggtt: the &xe_ggtt where node will be removed
 * @bo: the &xe_bo to be removed
 */
void xe_ggtt_remove_bo(struct xe_ggtt *ggtt, struct xe_bo *bo)
{
	u8 tile_id = ggtt->tile->id;

	if (XE_WARN_ON(!bo->ggtt_node[tile_id]))
		return;

	/* This BO is not currently in the GGTT */
	xe_tile_assert(ggtt->tile, bo->ggtt_node[tile_id]->base.size == bo->size);

	xe_ggtt_node_remove(bo->ggtt_node[tile_id],
			    bo->flags & XE_BO_FLAG_GGTT_INVALIDATE);
}

/**
 * xe_ggtt_largest_hole - Largest GGTT hole
 * @ggtt: the &xe_ggtt that will be inspected
 * @alignment: minimum alignment
 * @spare: If not NULL: in: desired memory size to be spared / out: Adjusted possible spare
 *
 * Return: size of the largest continuous GGTT region
 */
u64 xe_ggtt_largest_hole(struct xe_ggtt *ggtt, u64 alignment, u64 *spare)
{
	const struct drm_mm *mm = &ggtt->mm;
	const struct drm_mm_node *entry;
	u64 hole_min_start = xe_wopcm_size(tile_to_xe(ggtt->tile));
	u64 hole_start, hole_end, hole_size;
	u64 max_hole = 0;

	mutex_lock(&ggtt->lock);

	drm_mm_for_each_hole(entry, mm, hole_start, hole_end) {
		hole_start = max(hole_start, hole_min_start);
		hole_start = ALIGN(hole_start, alignment);
		hole_end = ALIGN_DOWN(hole_end, alignment);
		if (hole_start >= hole_end)
			continue;
		hole_size = hole_end - hole_start;
		if (spare)
			*spare -= min3(*spare, hole_size, max_hole);
		max_hole = max(max_hole, hole_size);
	}

	mutex_unlock(&ggtt->lock);

	return max_hole;
}

#ifdef CONFIG_PCI_IOV
static u64 xe_encode_vfid_pte(u16 vfid)
{
	return FIELD_PREP(GGTT_PTE_VFID, vfid) | XE_PAGE_PRESENT;
}

static void xe_ggtt_assign_locked(struct xe_ggtt *ggtt, const struct drm_mm_node *node, u16 vfid)
{
	u64 start = node->start;
	u64 size = node->size;
	u64 end = start + size - 1;
	u64 pte = xe_encode_vfid_pte(vfid);

	lockdep_assert_held(&ggtt->lock);

	if (!drm_mm_node_allocated(node))
		return;

	while (start < end) {
		ggtt->pt_ops->ggtt_set_pte(ggtt, start, pte);
		start += XE_PAGE_SIZE;
	}

	xe_ggtt_invalidate(ggtt);
}

/**
 * xe_ggtt_assign - assign a GGTT region to the VF
 * @node: the &xe_ggtt_node to update
 * @vfid: the VF identifier
 *
 * This function is used by the PF driver to assign a GGTT region to the VF.
 * In addition to PTE's VFID bits 11:2 also PRESENT bit 0 is set as on some
 * platforms VFs can't modify that either.
 */
void xe_ggtt_assign(const struct xe_ggtt_node *node, u16 vfid)
{
	mutex_lock(&node->ggtt->lock);
	xe_ggtt_assign_locked(node->ggtt, &node->base, vfid);
	mutex_unlock(&node->ggtt->lock);
}
#endif

/**
 * xe_ggtt_dump - Dump GGTT for debug
 * @ggtt: the &xe_ggtt to be dumped
 * @p: the &drm_mm_printer helper handle to be used to dump the information
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_ggtt_dump(struct xe_ggtt *ggtt, struct drm_printer *p)
{
	int err;

	err = mutex_lock_interruptible(&ggtt->lock);
	if (err)
		return err;

	drm_mm_print(&ggtt->mm, p);
	mutex_unlock(&ggtt->lock);
	return err;
}

/**
 * xe_ggtt_print_holes - Print holes
 * @ggtt: the &xe_ggtt to be inspected
 * @alignment: min alignment
 * @p: the &drm_printer
 *
 * Print GGTT ranges that are available and return total size available.
 *
 * Return: Total available size.
 */
u64 xe_ggtt_print_holes(struct xe_ggtt *ggtt, u64 alignment, struct drm_printer *p)
{
	const struct drm_mm *mm = &ggtt->mm;
	const struct drm_mm_node *entry;
	u64 hole_min_start = xe_wopcm_size(tile_to_xe(ggtt->tile));
	u64 hole_start, hole_end, hole_size;
	u64 total = 0;
	char buf[10];

	mutex_lock(&ggtt->lock);

	drm_mm_for_each_hole(entry, mm, hole_start, hole_end) {
		hole_start = max(hole_start, hole_min_start);
		hole_start = ALIGN(hole_start, alignment);
		hole_end = ALIGN_DOWN(hole_end, alignment);
		if (hole_start >= hole_end)
			continue;
		hole_size = hole_end - hole_start;
		total += hole_size;

		string_get_size(hole_size, 1, STRING_UNITS_2, buf, sizeof(buf));
		drm_printf(p, "range:\t%#llx-%#llx\t(%s)\n",
			   hole_start, hole_end - 1, buf);
	}

	mutex_unlock(&ggtt->lock);

	return total;
}

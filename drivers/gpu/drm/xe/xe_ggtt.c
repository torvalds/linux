// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_ggtt.h"

#include <linux/sizes.h>

#include <drm/drm_managed.h>
#include <drm/i915_drm.h>

#include "regs/xe_gt_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_wopcm.h"

#define XELPG_GGTT_PTE_PAT0	BIT_ULL(52)
#define XELPG_GGTT_PTE_PAT1	BIT_ULL(53)

/* GuC addresses above GUC_GGTT_TOP also don't map through the GTT */
#define GUC_GGTT_TOP	0xFEE00000

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

void xe_ggtt_set_pte(struct xe_ggtt *ggtt, u64 addr, u64 pte)
{
	xe_tile_assert(ggtt->tile, !(addr & XE_PTE_MASK));
	xe_tile_assert(ggtt->tile, addr < ggtt->size);

	writeq(pte, &ggtt->gsm[addr >> XE_PTE_SHIFT]);
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
		xe_ggtt_set_pte(ggtt, start, scratch_pte);
		start += XE_PAGE_SIZE;
	}
}

static void ggtt_fini_early(struct drm_device *drm, void *arg)
{
	struct xe_ggtt *ggtt = arg;

	mutex_destroy(&ggtt->lock);
	drm_mm_takedown(&ggtt->mm);
}

static void ggtt_fini(struct drm_device *drm, void *arg)
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
};

static const struct xe_ggtt_pt_ops xelpg_pt_ops = {
	.pte_encode_bo = xelpg_ggtt_pte_encode_bo,
};

/*
 * Early GGTT initialization, which allows to create new mappings usable by the
 * GuC.
 * Mappings are not usable by the HW engines, as it doesn't have scratch /
 * initial clear done to it yet. That will happen in the regular, non-early
 * GGTT init.
 */
int xe_ggtt_init_early(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	unsigned int gsm_size;

	gsm_size = probe_gsm_size(pdev);
	if (gsm_size == 0) {
		drm_err(&xe->drm, "Hardware reported no preallocated GSM\n");
		return -ENOMEM;
	}

	ggtt->gsm = ggtt->tile->mmio.regs + SZ_8M;
	ggtt->size = (gsm_size / 8) * (u64) XE_PAGE_SIZE;

	if (IS_DGFX(xe) && xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
		ggtt->flags |= XE_GGTT_FLAGS_64K;

	/*
	 * 8B per entry, each points to a 4KB page.
	 *
	 * The GuC address space is limited on both ends of the GGTT, because
	 * the GuC shim HW redirects accesses to those addresses to other HW
	 * areas instead of going through the GGTT. On the bottom end, the GuC
	 * can't access offsets below the WOPCM size, while on the top side the
	 * limit is fixed at GUC_GGTT_TOP. To keep things simple, instead of
	 * checking each object to see if they are accessed by GuC or not, we
	 * just exclude those areas from the allocator. Additionally, to
	 * simplify the driver load, we use the maximum WOPCM size in this logic
	 * instead of the programmed one, so we don't need to wait until the
	 * actual size to be programmed is determined (which requires FW fetch)
	 * before initializing the GGTT. These simplifications might waste space
	 * in the GGTT (about 20-25 MBs depending on the platform) but we can
	 * live with this.
	 *
	 * Another benifit of this is the GuC bootrom can't access anything
	 * below the WOPCM max size so anything the bootom needs to access (e.g.
	 * a RSA key) needs to be placed in the GGTT above the WOPCM max size.
	 * Starting the GGTT allocations above the WOPCM max give us the correct
	 * placement for free.
	 */
	if (ggtt->size > GUC_GGTT_TOP)
		ggtt->size = GUC_GGTT_TOP;

	if (GRAPHICS_VERx100(xe) >= 1270)
		ggtt->pt_ops = &xelpg_pt_ops;
	else
		ggtt->pt_ops = &xelp_pt_ops;

	drm_mm_init(&ggtt->mm, xe_wopcm_size(xe),
		    ggtt->size - xe_wopcm_size(xe));
	mutex_init(&ggtt->lock);
	primelockdep(ggtt);

	return drmm_add_action_or_reset(&xe->drm, ggtt_fini_early, ggtt);
}

static void xe_ggtt_initial_clear(struct xe_ggtt *ggtt)
{
	struct drm_mm_node *hole;
	u64 start, end;

	/* Display may have allocated inside ggtt, so be careful with clearing here */
	xe_device_mem_access_get(tile_to_xe(ggtt->tile));
	mutex_lock(&ggtt->lock);
	drm_mm_for_each_hole(hole, &ggtt->mm, start, end)
		xe_ggtt_clear(ggtt, start, end - start);

	xe_ggtt_invalidate(ggtt);
	mutex_unlock(&ggtt->lock);
	xe_device_mem_access_put(tile_to_xe(ggtt->tile));
}

int xe_ggtt_init(struct xe_ggtt *ggtt)
{
	struct xe_device *xe = tile_to_xe(ggtt->tile);
	unsigned int flags;
	int err;

	/*
	 * So we don't need to worry about 64K GGTT layout when dealing with
	 * scratch entires, rather keep the scratch page in system memory on
	 * platforms where 64K pages are needed for VRAM.
	 */
	flags = XE_BO_CREATE_PINNED_BIT;
	if (ggtt->flags & XE_GGTT_FLAGS_64K)
		flags |= XE_BO_CREATE_SYSTEM_BIT;
	else
		flags |= XE_BO_CREATE_VRAM_IF_DGFX(ggtt->tile);

	ggtt->scratch = xe_managed_bo_create_pin_map(xe, ggtt->tile, XE_PAGE_SIZE, flags);
	if (IS_ERR(ggtt->scratch)) {
		err = PTR_ERR(ggtt->scratch);
		goto err;
	}

	xe_map_memset(xe, &ggtt->scratch->vmap, 0, 0, ggtt->scratch->size);

	xe_ggtt_initial_clear(ggtt);

	return drmm_add_action_or_reset(&xe->drm, ggtt_fini, ggtt);
err:
	ggtt->scratch = NULL;
	return err;
}

#define GUC_TLB_INV_CR				XE_REG(0xcee8)
#define   GUC_TLB_INV_CR_INVALIDATE		REG_BIT(0)
#define PVC_GUC_TLB_INV_DESC0			XE_REG(0xcf7c)
#define   PVC_GUC_TLB_INV_DESC0_VALID		REG_BIT(0)
#define PVC_GUC_TLB_INV_DESC1			XE_REG(0xcf80)
#define   PVC_GUC_TLB_INV_DESC1_INVALIDATE	REG_BIT(6)

static void ggtt_invalidate_gt_tlb(struct xe_gt *gt)
{
	if (!gt)
		return;

	/*
	 * Invalidation can happen when there's no in-flight work keeping the
	 * GT awake.  We need to explicitly grab forcewake to ensure the GT
	 * and GuC are accessible.
	 */
	xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);

	/* TODO: vfunc for GuC vs. non-GuC */

	if (gt->uc.guc.submission_state.enabled) {
		int seqno;

		seqno = xe_gt_tlb_invalidation_guc(gt);
		xe_gt_assert(gt, seqno > 0);
		if (seqno > 0)
			xe_gt_tlb_invalidation_wait(gt, seqno);
	} else if (xe_device_uc_enabled(gt_to_xe(gt))) {
		struct xe_device *xe = gt_to_xe(gt);

		if (xe->info.platform == XE_PVC || GRAPHICS_VER(xe) >= 20) {
			xe_mmio_write32(gt, PVC_GUC_TLB_INV_DESC1,
					PVC_GUC_TLB_INV_DESC1_INVALIDATE);
			xe_mmio_write32(gt, PVC_GUC_TLB_INV_DESC0,
					PVC_GUC_TLB_INV_DESC0_VALID);
		} else
			xe_mmio_write32(gt, GUC_TLB_INV_CR,
					GUC_TLB_INV_CR_INVALIDATE);
	}

	xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
}

void xe_ggtt_invalidate(struct xe_ggtt *ggtt)
{
	/* Each GT in a tile has its own TLB to cache GGTT lookups */
	ggtt_invalidate_gt_tlb(ggtt->tile->primary_gt);
	ggtt_invalidate_gt_tlb(ggtt->tile->media_gt);
}

void xe_ggtt_printk(struct xe_ggtt *ggtt, const char *prefix)
{
	u16 pat_index = tile_to_xe(ggtt->tile)->pat.idx[XE_CACHE_WB];
	u64 addr, scratch_pte;

	scratch_pte = ggtt->pt_ops->pte_encode_bo(ggtt->scratch, 0, pat_index);

	printk("%sGlobal GTT:", prefix);
	for (addr = 0; addr < ggtt->size; addr += XE_PAGE_SIZE) {
		unsigned int i = addr / XE_PAGE_SIZE;

		xe_tile_assert(ggtt->tile, addr <= U32_MAX);
		if (ggtt->gsm[i] == scratch_pte)
			continue;

		printk("%s    ggtt[0x%08x] = 0x%016llx",
		       prefix, (u32)addr, ggtt->gsm[i]);
	}
}

int xe_ggtt_insert_special_node_locked(struct xe_ggtt *ggtt, struct drm_mm_node *node,
				       u32 size, u32 align, u32 mm_flags)
{
	return drm_mm_insert_node_generic(&ggtt->mm, node, size, align, 0,
					  mm_flags);
}

int xe_ggtt_insert_special_node(struct xe_ggtt *ggtt, struct drm_mm_node *node,
				u32 size, u32 align)
{
	int ret;

	mutex_lock(&ggtt->lock);
	ret = xe_ggtt_insert_special_node_locked(ggtt, node, size,
						 align, DRM_MM_INSERT_HIGH);
	mutex_unlock(&ggtt->lock);

	return ret;
}

void xe_ggtt_map_bo(struct xe_ggtt *ggtt, struct xe_bo *bo)
{
	u16 pat_index = tile_to_xe(ggtt->tile)->pat.idx[XE_CACHE_WB];
	u64 start = bo->ggtt_node.start;
	u64 offset, pte;

	for (offset = 0; offset < bo->size; offset += XE_PAGE_SIZE) {
		pte = ggtt->pt_ops->pte_encode_bo(bo, offset, pat_index);
		xe_ggtt_set_pte(ggtt, start + offset, pte);
	}

	xe_ggtt_invalidate(ggtt);
}

static int __xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo,
				  u64 start, u64 end)
{
	int err;
	u64 alignment = XE_PAGE_SIZE;

	if (xe_bo_is_vram(bo) && ggtt->flags & XE_GGTT_FLAGS_64K)
		alignment = SZ_64K;

	if (XE_WARN_ON(bo->ggtt_node.size)) {
		/* Someone's already inserted this BO in the GGTT */
		xe_tile_assert(ggtt->tile, bo->ggtt_node.size == bo->size);
		return 0;
	}

	err = xe_bo_validate(bo, NULL, false);
	if (err)
		return err;

	xe_device_mem_access_get(tile_to_xe(ggtt->tile));
	mutex_lock(&ggtt->lock);
	err = drm_mm_insert_node_in_range(&ggtt->mm, &bo->ggtt_node, bo->size,
					  alignment, 0, start, end, 0);
	if (!err)
		xe_ggtt_map_bo(ggtt, bo);
	mutex_unlock(&ggtt->lock);
	xe_device_mem_access_put(tile_to_xe(ggtt->tile));

	return err;
}

int xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo,
			 u64 start, u64 end)
{
	return __xe_ggtt_insert_bo_at(ggtt, bo, start, end);
}

int xe_ggtt_insert_bo(struct xe_ggtt *ggtt, struct xe_bo *bo)
{
	return __xe_ggtt_insert_bo_at(ggtt, bo, 0, U64_MAX);
}

void xe_ggtt_remove_node(struct xe_ggtt *ggtt, struct drm_mm_node *node)
{
	xe_device_mem_access_get(tile_to_xe(ggtt->tile));
	mutex_lock(&ggtt->lock);

	xe_ggtt_clear(ggtt, node->start, node->size);
	drm_mm_remove_node(node);
	node->size = 0;

	xe_ggtt_invalidate(ggtt);

	mutex_unlock(&ggtt->lock);
	xe_device_mem_access_put(tile_to_xe(ggtt->tile));
}

void xe_ggtt_remove_bo(struct xe_ggtt *ggtt, struct xe_bo *bo)
{
	if (XE_WARN_ON(!bo->ggtt_node.size))
		return;

	/* This BO is not currently in the GGTT */
	xe_tile_assert(ggtt->tile, bo->ggtt_node.size == bo->size);

	xe_ggtt_remove_node(ggtt, &bo->ggtt_node);
}

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

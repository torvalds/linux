// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_ggtt.h"

#include <linux/sizes.h>
#include <drm/i915_drm.h>

#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_bo.h"
#include "xe_gt.h"
#include "xe_mmio.h"
#include "xe_wopcm.h"

#include "i915_reg.h"
#include "gt/intel_gt_regs.h"

/* FIXME: Common file, preferably auto-gen */
#define MTL_GGTT_PTE_PAT0	BIT_ULL(52)
#define MTL_GGTT_PTE_PAT1	BIT_ULL(53)

u64 xe_ggtt_pte_encode(struct xe_bo *bo, u64 bo_offset)
{
	struct xe_device *xe = xe_bo_device(bo);
	u64 pte;
	bool is_lmem;

	pte = xe_bo_addr(bo, bo_offset, GEN8_PAGE_SIZE, &is_lmem);
	pte |= GEN8_PAGE_PRESENT;

	if (is_lmem)
		pte |= GEN12_GGTT_PTE_LM;

	/* FIXME: vfunc + pass in caching rules */
	if (xe->info.platform == XE_METEORLAKE) {
		pte |= MTL_GGTT_PTE_PAT0;
		pte |= MTL_GGTT_PTE_PAT1;
	}

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
	XE_BUG_ON(addr & GEN8_PTE_MASK);
	XE_BUG_ON(addr >= ggtt->size);

	writeq(pte, &ggtt->gsm[addr >> GEN8_PTE_SHIFT]);
}

static void xe_ggtt_clear(struct xe_ggtt *ggtt, u64 start, u64 size)
{
	u64 end = start + size - 1;
	u64 scratch_pte;

	XE_BUG_ON(start >= end);

	if (ggtt->scratch)
		scratch_pte = xe_ggtt_pte_encode(ggtt->scratch, 0);
	else
		scratch_pte = 0;

	while (start < end) {
		xe_ggtt_set_pte(ggtt, start, scratch_pte);
		start += GEN8_PAGE_SIZE;
	}
}

static void ggtt_fini_noalloc(struct drm_device *drm, void *arg)
{
	struct xe_ggtt *ggtt = arg;

	mutex_destroy(&ggtt->lock);
	drm_mm_takedown(&ggtt->mm);

	xe_bo_unpin_map_no_vm(ggtt->scratch);
}

int xe_ggtt_init_noalloc(struct xe_gt *gt, struct xe_ggtt *ggtt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	unsigned int gsm_size;

	XE_BUG_ON(xe_gt_is_media_type(gt));

	ggtt->gt = gt;

	gsm_size = probe_gsm_size(pdev);
	if (gsm_size == 0) {
		drm_err(&xe->drm, "Hardware reported no preallocated GSM\n");
		return -ENOMEM;
	}

	ggtt->gsm = gt->mmio.regs + SZ_8M;
	ggtt->size = (gsm_size / 8) * (u64)GEN8_PAGE_SIZE;

	if (IS_DGFX(xe) && xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
		ggtt->flags |= XE_GGTT_FLAGS_64K;

	/*
	 * 8B per entry, each points to a 4KB page.
	 *
	 * The GuC owns the WOPCM space, thus we can't allocate GGTT address in
	 * this area. Even though we likely configure the WOPCM to less than the
	 * maximum value, to simplify the driver load (no need to fetch HuC +
	 * GuC firmwares and determine there sizes before initializing the GGTT)
	 * just start the GGTT allocation above the max WOPCM size. This might
	 * waste space in the GGTT (WOPCM is 2MB on modern platforms) but we can
	 * live with this.
	 *
	 * Another benifit of this is the GuC bootrom can't access anything
	 * below the WOPCM max size so anything the bootom needs to access (e.g.
	 * a RSA key) needs to be placed in the GGTT above the WOPCM max size.
	 * Starting the GGTT allocations above the WOPCM max give us the correct
	 * placement for free.
	 */
	drm_mm_init(&ggtt->mm, xe_wopcm_size(xe),
		    ggtt->size - xe_wopcm_size(xe));
	mutex_init(&ggtt->lock);

	return drmm_add_action_or_reset(&xe->drm, ggtt_fini_noalloc, ggtt);
}

static void xe_ggtt_initial_clear(struct xe_ggtt *ggtt)
{
	struct drm_mm_node *hole;
	u64 start, end;

	/* Display may have allocated inside ggtt, so be careful with clearing here */
	mutex_lock(&ggtt->lock);
	drm_mm_for_each_hole(hole, &ggtt->mm, start, end)
		xe_ggtt_clear(ggtt, start, end - start);

	xe_ggtt_invalidate(ggtt->gt);
	mutex_unlock(&ggtt->lock);
}

int xe_ggtt_init(struct xe_gt *gt, struct xe_ggtt *ggtt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	ggtt->scratch = xe_bo_create_locked(xe, gt, NULL, GEN8_PAGE_SIZE,
					    ttm_bo_type_kernel,
					    XE_BO_CREATE_VRAM_IF_DGFX(gt) |
					    XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(ggtt->scratch)) {
		err = PTR_ERR(ggtt->scratch);
		goto err;
	}

	err = xe_bo_pin(ggtt->scratch);
	xe_bo_unlock_no_vm(ggtt->scratch);
	if (err) {
		xe_bo_put(ggtt->scratch);
		goto err;
	}

	xe_ggtt_initial_clear(ggtt);
	return 0;
err:
	ggtt->scratch = NULL;
	return err;
}

#define GEN12_GUC_TLB_INV_CR                     _MMIO(0xcee8)
#define   GEN12_GUC_TLB_INV_CR_INVALIDATE        (1 << 0)
#define PVC_GUC_TLB_INV_DESC0			_MMIO(0xcf7c)
#define   PVC_GUC_TLB_INV_DESC0_VALID		 (1 << 0)
#define PVC_GUC_TLB_INV_DESC1			_MMIO(0xcf80)
#define   PVC_GUC_TLB_INV_DESC1_INVALIDATE	 (1 << 6)

void xe_ggtt_invalidate(struct xe_gt *gt)
{
	/* TODO: vfunc for GuC vs. non-GuC */

	/* TODO: i915 makes comments about this being uncached and
	 * therefore flushing WC buffers.  Is that really true here?
	 */
	xe_mmio_write32(gt, GFX_FLSH_CNTL_GEN6.reg, GFX_FLSH_CNTL_EN);
	if (xe_device_guc_submission_enabled(gt_to_xe(gt))) {
		struct xe_device *xe = gt_to_xe(gt);

		/* TODO: also use vfunc here */
		if (xe->info.platform == XE_PVC) {
			xe_mmio_write32(gt, PVC_GUC_TLB_INV_DESC1.reg,
					PVC_GUC_TLB_INV_DESC1_INVALIDATE);
			xe_mmio_write32(gt, PVC_GUC_TLB_INV_DESC0.reg,
					PVC_GUC_TLB_INV_DESC0_VALID);
		} else
			xe_mmio_write32(gt, GEN12_GUC_TLB_INV_CR.reg,
					GEN12_GUC_TLB_INV_CR_INVALIDATE);
	}
}

void xe_ggtt_printk(struct xe_ggtt *ggtt, const char *prefix)
{
	u64 addr, scratch_pte;

	scratch_pte = xe_ggtt_pte_encode(ggtt->scratch, 0);

	printk("%sGlobal GTT:", prefix);
	for (addr = 0; addr < ggtt->size; addr += GEN8_PAGE_SIZE) {
		unsigned int i = addr / GEN8_PAGE_SIZE;

		XE_BUG_ON(addr > U32_MAX);
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
	u64 start = bo->ggtt_node.start;
	u64 offset, pte;

	for (offset = 0; offset < bo->size; offset += GEN8_PAGE_SIZE) {
		pte = xe_ggtt_pte_encode(bo, offset);
		xe_ggtt_set_pte(ggtt, start + offset, pte);
	}

	xe_ggtt_invalidate(ggtt->gt);
}

static int __xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo,
				  u64 start, u64 end, u64 alignment)
{
	int err;

	if (XE_WARN_ON(bo->ggtt_node.size)) {
		/* Someone's already inserted this BO in the GGTT */
		XE_BUG_ON(bo->ggtt_node.size != bo->size);
		return 0;
	}

	err = xe_bo_validate(bo, NULL, false);
	if (err)
		return err;

	mutex_lock(&ggtt->lock);
	err = drm_mm_insert_node_in_range(&ggtt->mm, &bo->ggtt_node, bo->size,
					  alignment, 0, start, end, 0);
	if (!err)
		xe_ggtt_map_bo(ggtt, bo);
	mutex_unlock(&ggtt->lock);

	return err;
}

int xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo, u64 ofs)
{
	if (xe_bo_is_vram(bo) && ggtt->flags & XE_GGTT_FLAGS_64K) {
		if (XE_WARN_ON(!IS_ALIGNED(ofs, SZ_64K)) ||
		    XE_WARN_ON(!IS_ALIGNED(bo->size, SZ_64K)))
			return -EINVAL;
	}

	return __xe_ggtt_insert_bo_at(ggtt, bo, ofs, ofs + bo->size, 0);
}

int xe_ggtt_insert_bo(struct xe_ggtt *ggtt, struct xe_bo *bo)
{
	u64 alignment;

	alignment = GEN8_PAGE_SIZE;
	if (xe_bo_is_vram(bo) && ggtt->flags & XE_GGTT_FLAGS_64K)
		alignment = SZ_64K;

	return __xe_ggtt_insert_bo_at(ggtt, bo, 0, U64_MAX, alignment);
}

void xe_ggtt_remove_node(struct xe_ggtt *ggtt, struct drm_mm_node *node)
{
	mutex_lock(&ggtt->lock);

	xe_ggtt_clear(ggtt, node->start, node->size);
	drm_mm_remove_node(node);
	node->size = 0;

	xe_ggtt_invalidate(ggtt->gt);

	mutex_unlock(&ggtt->lock);
}

void xe_ggtt_remove_bo(struct xe_ggtt *ggtt, struct xe_bo *bo)
{
	if (XE_WARN_ON(!bo->ggtt_node.size))
		return;

	/* This BO is not currently in the GGTT */
	XE_BUG_ON(bo->ggtt_node.size != bo->size);

	xe_ggtt_remove_node(ggtt, &bo->ggtt_node);
}

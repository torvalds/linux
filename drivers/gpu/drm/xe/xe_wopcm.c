// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_wopcm.h"

#include "regs/xe_guc_regs.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_mmio.h"
#include "xe_uc_fw.h"

/**
 * DOC: Write Once Protected Content Memory (WOPCM) Layout
 *
 * The layout of the WOPCM will be fixed after writing to GuC WOPCM size and
 * offset registers whose values are calculated and determined by HuC/GuC
 * firmware size and set of hardware requirements/restrictions as shown below:
 *
 * ::
 *
 *    +=========> +====================+ <== WOPCM Top
 *    ^           |  HW contexts RSVD  |
 *    |     +===> +====================+ <== GuC WOPCM Top
 *    |     ^     |                    |
 *    |     |     |                    |
 *    |     |     |                    |
 *    |    GuC    |                    |
 *    |   WOPCM   |                    |
 *    |    Size   +--------------------+
 *  WOPCM   |     |    GuC FW RSVD     |
 *    |     |     +--------------------+
 *    |     |     |   GuC Stack RSVD   |
 *    |     |     +------------------- +
 *    |     v     |   GuC WOPCM RSVD   |
 *    |     +===> +====================+ <== GuC WOPCM base
 *    |           |     WOPCM RSVD     |
 *    |           +------------------- + <== HuC Firmware Top
 *    v           |      HuC FW        |
 *    +=========> +====================+ <== WOPCM Base
 *
 * GuC accessible WOPCM starts at GuC WOPCM base and ends at GuC WOPCM top.
 * The top part of the WOPCM is reserved for hardware contexts (e.g. RC6
 * context).
 */

/* Default WOPCM size is 2MB from Gen11, 1MB on previous platforms */
/* FIXME: Larger size require for 2 tile PVC, do a proper probe sooner or later */
#define DGFX_WOPCM_SIZE			SZ_4M
/* FIXME: Larger size require for MTL, do a proper probe sooner or later */
#define MTL_WOPCM_SIZE			SZ_4M
#define WOPCM_SIZE			SZ_2M

#define MAX_WOPCM_SIZE			SZ_8M

/* 16KB WOPCM (RSVD WOPCM) is reserved from HuC firmware top. */
#define WOPCM_RESERVED_SIZE		SZ_16K

/* 16KB reserved at the beginning of GuC WOPCM. */
#define GUC_WOPCM_RESERVED		SZ_16K
/* 8KB from GUC_WOPCM_RESERVED is reserved for GuC stack. */
#define GUC_WOPCM_STACK_RESERVED	SZ_8K

/* GuC WOPCM Offset value needs to be aligned to 16KB. */
#define GUC_WOPCM_OFFSET_ALIGNMENT	(1UL << GUC_WOPCM_OFFSET_SHIFT)

/* 36KB WOPCM reserved at the end of WOPCM */
#define WOPCM_HW_CTX_RESERVED		(SZ_32K + SZ_4K)

static inline struct xe_gt *wopcm_to_gt(struct xe_wopcm *wopcm)
{
	return container_of(wopcm, struct xe_gt, uc.wopcm);
}

static inline struct xe_device *wopcm_to_xe(struct xe_wopcm *wopcm)
{
	return gt_to_xe(wopcm_to_gt(wopcm));
}

static u32 context_reserved_size(void)
{
	return WOPCM_HW_CTX_RESERVED;
}

static bool __check_layout(struct xe_device *xe, u32 wopcm_size,
			   u32 guc_wopcm_base, u32 guc_wopcm_size,
			   u32 guc_fw_size, u32 huc_fw_size)
{
	const u32 ctx_rsvd = context_reserved_size();
	u32 size;

	size = wopcm_size - ctx_rsvd;
	if (unlikely(guc_wopcm_base >= size ||
		     guc_wopcm_size > size - guc_wopcm_base)) {
		drm_err(&xe->drm,
			"WOPCM: invalid GuC region layout: %uK + %uK > %uK\n",
			guc_wopcm_base / SZ_1K, guc_wopcm_size / SZ_1K,
			size / SZ_1K);
		return false;
	}

	size = guc_fw_size + GUC_WOPCM_RESERVED + GUC_WOPCM_STACK_RESERVED;
	if (unlikely(guc_wopcm_size < size)) {
		drm_err(&xe->drm, "WOPCM: no space for %s: %uK < %uK\n",
			xe_uc_fw_type_repr(XE_UC_FW_TYPE_GUC),
			guc_wopcm_size / SZ_1K, size / SZ_1K);
		return false;
	}

	size = huc_fw_size + WOPCM_RESERVED_SIZE;
	if (unlikely(guc_wopcm_base < size)) {
		drm_err(&xe->drm, "WOPCM: no space for %s: %uK < %uK\n",
			xe_uc_fw_type_repr(XE_UC_FW_TYPE_HUC),
			guc_wopcm_base / SZ_1K, size / SZ_1K);
		return false;
	}

	return true;
}

static bool __wopcm_regs_locked(struct xe_gt *gt,
				u32 *guc_wopcm_base, u32 *guc_wopcm_size)
{
	u32 reg_base = xe_mmio_read32(gt, DMA_GUC_WOPCM_OFFSET);
	u32 reg_size = xe_mmio_read32(gt, GUC_WOPCM_SIZE);

	if (!(reg_size & GUC_WOPCM_SIZE_LOCKED) ||
	    !(reg_base & GUC_WOPCM_OFFSET_VALID))
		return false;

	*guc_wopcm_base = reg_base & GUC_WOPCM_OFFSET_MASK;
	*guc_wopcm_size = reg_size & GUC_WOPCM_SIZE_MASK;
	return true;
}

static int __wopcm_init_regs(struct xe_device *xe, struct xe_gt *gt,
			     struct xe_wopcm *wopcm)
{
	u32 base = wopcm->guc.base;
	u32 size = wopcm->guc.size;
	u32 huc_agent = xe_uc_fw_is_available(&gt->uc.huc.fw) ? HUC_LOADING_AGENT_GUC : 0;
	u32 mask;
	int err;

	XE_WARN_ON(!(base & GUC_WOPCM_OFFSET_MASK));
	XE_WARN_ON(base & ~GUC_WOPCM_OFFSET_MASK);
	XE_WARN_ON(!(size & GUC_WOPCM_SIZE_MASK));
	XE_WARN_ON(size & ~GUC_WOPCM_SIZE_MASK);

	mask = GUC_WOPCM_SIZE_MASK | GUC_WOPCM_SIZE_LOCKED;
	err = xe_mmio_write32_and_verify(gt, GUC_WOPCM_SIZE, size, mask,
					 size | GUC_WOPCM_SIZE_LOCKED);
	if (err)
		goto err_out;

	mask = GUC_WOPCM_OFFSET_MASK | GUC_WOPCM_OFFSET_VALID | huc_agent;
	err = xe_mmio_write32_and_verify(gt, DMA_GUC_WOPCM_OFFSET,
					 base | huc_agent, mask,
					 base | huc_agent |
					 GUC_WOPCM_OFFSET_VALID);
	if (err)
		goto err_out;

	return 0;

err_out:
	drm_notice(&xe->drm, "Failed to init uC WOPCM registers!\n");
	drm_notice(&xe->drm, "%s(%#x)=%#x\n", "DMA_GUC_WOPCM_OFFSET",
		   DMA_GUC_WOPCM_OFFSET.addr,
		   xe_mmio_read32(gt, DMA_GUC_WOPCM_OFFSET));
	drm_notice(&xe->drm, "%s(%#x)=%#x\n", "GUC_WOPCM_SIZE",
		   GUC_WOPCM_SIZE.addr,
		   xe_mmio_read32(gt, GUC_WOPCM_SIZE));

	return err;
}

u32 xe_wopcm_size(struct xe_device *xe)
{
	return IS_DGFX(xe) ? DGFX_WOPCM_SIZE :
		xe->info.platform == XE_METEORLAKE ? MTL_WOPCM_SIZE :
		WOPCM_SIZE;
}

/**
 * xe_wopcm_init() - Initialize the WOPCM structure.
 * @wopcm: pointer to xe_wopcm.
 *
 * This function will partition WOPCM space based on GuC and HuC firmware sizes
 * and will allocate max remaining for use by GuC. This function will also
 * enforce platform dependent hardware restrictions on GuC WOPCM offset and
 * size. It will fail the WOPCM init if any of these checks fail, so that the
 * following WOPCM registers setup and GuC firmware uploading would be aborted.
 */
int xe_wopcm_init(struct xe_wopcm *wopcm)
{
	struct xe_device *xe = wopcm_to_xe(wopcm);
	struct xe_gt *gt = wopcm_to_gt(wopcm);
	u32 guc_fw_size = xe_uc_fw_get_upload_size(&gt->uc.guc.fw);
	u32 huc_fw_size = xe_uc_fw_get_upload_size(&gt->uc.huc.fw);
	u32 ctx_rsvd = context_reserved_size();
	u32 guc_wopcm_base;
	u32 guc_wopcm_size;
	bool locked;
	int ret = 0;

	if (!guc_fw_size)
		return -EINVAL;

	wopcm->size = xe_wopcm_size(xe);
	drm_dbg(&xe->drm, "WOPCM: %uK\n", wopcm->size / SZ_1K);

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);
	XE_WARN_ON(guc_fw_size >= wopcm->size);
	XE_WARN_ON(huc_fw_size >= wopcm->size);
	XE_WARN_ON(ctx_rsvd + WOPCM_RESERVED_SIZE >= wopcm->size);

	locked = __wopcm_regs_locked(gt, &guc_wopcm_base, &guc_wopcm_size);
	if (locked) {
		drm_dbg(&xe->drm, "GuC WOPCM is already locked [%uK, %uK)\n",
			guc_wopcm_base / SZ_1K, guc_wopcm_size / SZ_1K);
		/*
		 * When the GuC wopcm base and size are preprogrammed by
		 * BIOS/IFWI, check against the max allowed wopcm size to
		 * validate if the programmed values align to the wopcm layout.
		 */
		wopcm->size = MAX_WOPCM_SIZE;

		goto check;
	}

	/*
	 * Aligned value of guc_wopcm_base will determine available WOPCM space
	 * for HuC firmware and mandatory reserved area.
	 */
	guc_wopcm_base = huc_fw_size + WOPCM_RESERVED_SIZE;
	guc_wopcm_base = ALIGN(guc_wopcm_base, GUC_WOPCM_OFFSET_ALIGNMENT);

	/*
	 * Need to clamp guc_wopcm_base now to make sure the following math is
	 * correct. Formal check of whole WOPCM layout will be done below.
	 */
	guc_wopcm_base = min(guc_wopcm_base, wopcm->size - ctx_rsvd);

	/* Aligned remainings of usable WOPCM space can be assigned to GuC. */
	guc_wopcm_size = wopcm->size - ctx_rsvd - guc_wopcm_base;
	guc_wopcm_size &= GUC_WOPCM_SIZE_MASK;

	drm_dbg(&xe->drm, "Calculated GuC WOPCM [%uK, %uK)\n",
		guc_wopcm_base / SZ_1K, guc_wopcm_size / SZ_1K);

check:
	if (__check_layout(xe, wopcm->size, guc_wopcm_base, guc_wopcm_size,
			   guc_fw_size, huc_fw_size)) {
		wopcm->guc.base = guc_wopcm_base;
		wopcm->guc.size = guc_wopcm_size;
		XE_WARN_ON(!wopcm->guc.base);
		XE_WARN_ON(!wopcm->guc.size);
	} else {
		drm_notice(&xe->drm, "Unsuccessful WOPCM partitioning\n");
		return -E2BIG;
	}

	if (!locked)
		ret = __wopcm_init_regs(xe, gt, wopcm);

	return ret;
}

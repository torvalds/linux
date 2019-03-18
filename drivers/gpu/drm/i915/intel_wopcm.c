/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017-2018 Intel Corporation
 */

#include "intel_wopcm.h"
#include "i915_drv.h"

/**
 * DOC: WOPCM Layout
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

/* Default WOPCM size 1MB. */
#define GEN9_WOPCM_SIZE			(1024 * 1024)
/* 16KB WOPCM (RSVD WOPCM) is reserved from HuC firmware top. */
#define WOPCM_RESERVED_SIZE		(16 * 1024)

/* 16KB reserved at the beginning of GuC WOPCM. */
#define GUC_WOPCM_RESERVED		(16 * 1024)
/* 8KB from GUC_WOPCM_RESERVED is reserved for GuC stack. */
#define GUC_WOPCM_STACK_RESERVED	(8 * 1024)

/* GuC WOPCM Offset value needs to be aligned to 16KB. */
#define GUC_WOPCM_OFFSET_ALIGNMENT	(1UL << GUC_WOPCM_OFFSET_SHIFT)

/* 24KB at the end of WOPCM is reserved for RC6 CTX on BXT. */
#define BXT_WOPCM_RC6_CTX_RESERVED	(24 * 1024)
/* 36KB WOPCM reserved at the end of WOPCM on CNL. */
#define CNL_WOPCM_HW_CTX_RESERVED	(36 * 1024)

/* 128KB from GUC_WOPCM_RESERVED is reserved for FW on Gen9. */
#define GEN9_GUC_FW_RESERVED	(128 * 1024)
#define GEN9_GUC_WOPCM_OFFSET	(GUC_WOPCM_RESERVED + GEN9_GUC_FW_RESERVED)

/**
 * intel_wopcm_init_early() - Early initialization of the WOPCM.
 * @wopcm: pointer to intel_wopcm.
 *
 * Setup the size of WOPCM which will be used by later on WOPCM partitioning.
 */
void intel_wopcm_init_early(struct intel_wopcm *wopcm)
{
	wopcm->size = GEN9_WOPCM_SIZE;

	DRM_DEBUG_DRIVER("WOPCM size: %uKiB\n", wopcm->size / 1024);
}

static inline u32 context_reserved_size(struct drm_i915_private *i915)
{
	if (IS_GEN9_LP(i915))
		return BXT_WOPCM_RC6_CTX_RESERVED;
	else if (INTEL_GEN(i915) >= 10)
		return CNL_WOPCM_HW_CTX_RESERVED;
	else
		return 0;
}

static inline int gen9_check_dword_gap(u32 guc_wopcm_base, u32 guc_wopcm_size)
{
	u32 offset;

	/*
	 * GuC WOPCM size shall be at least a dword larger than the offset from
	 * WOPCM base (GuC WOPCM offset from WOPCM base + GEN9_GUC_WOPCM_OFFSET)
	 * due to hardware limitation on Gen9.
	 */
	offset = guc_wopcm_base + GEN9_GUC_WOPCM_OFFSET;
	if (offset > guc_wopcm_size ||
	    (guc_wopcm_size - offset) < sizeof(u32)) {
		DRM_ERROR("GuC WOPCM size %uKiB is too small. %uKiB needed.\n",
			  guc_wopcm_size / 1024,
			  (u32)(offset + sizeof(u32)) / 1024);
		return -E2BIG;
	}

	return 0;
}

static inline int gen9_check_huc_fw_fits(u32 guc_wopcm_size, u32 huc_fw_size)
{
	/*
	 * On Gen9 & CNL A0, hardware requires the total available GuC WOPCM
	 * size to be larger than or equal to HuC firmware size. Otherwise,
	 * firmware uploading would fail.
	 */
	if (huc_fw_size > guc_wopcm_size - GUC_WOPCM_RESERVED) {
		DRM_ERROR("HuC FW (%uKiB) won't fit in GuC WOPCM (%uKiB).\n",
			  huc_fw_size / 1024,
			  (guc_wopcm_size - GUC_WOPCM_RESERVED) / 1024);
		return -E2BIG;
	}

	return 0;
}

static inline int check_hw_restriction(struct drm_i915_private *i915,
				       u32 guc_wopcm_base, u32 guc_wopcm_size,
				       u32 huc_fw_size)
{
	int err = 0;

	if (IS_GEN(i915, 9))
		err = gen9_check_dword_gap(guc_wopcm_base, guc_wopcm_size);

	if (!err &&
	    (IS_GEN(i915, 9) || IS_CNL_REVID(i915, CNL_REVID_A0, CNL_REVID_A0)))
		err = gen9_check_huc_fw_fits(guc_wopcm_size, huc_fw_size);

	return err;
}

/**
 * intel_wopcm_init() - Initialize the WOPCM structure.
 * @wopcm: pointer to intel_wopcm.
 *
 * This function will partition WOPCM space based on GuC and HuC firmware sizes
 * and will allocate max remaining for use by GuC. This function will also
 * enforce platform dependent hardware restrictions on GuC WOPCM offset and
 * size. It will fail the WOPCM init if any of these checks were failed, so that
 * the following GuC firmware uploading would be aborted.
 *
 * Return: 0 on success, non-zero error code on failure.
 */
int intel_wopcm_init(struct intel_wopcm *wopcm)
{
	struct drm_i915_private *i915 = wopcm_to_i915(wopcm);
	u32 guc_fw_size = intel_uc_fw_get_upload_size(&i915->guc.fw);
	u32 huc_fw_size = intel_uc_fw_get_upload_size(&i915->huc.fw);
	u32 ctx_rsvd = context_reserved_size(i915);
	u32 guc_wopcm_base;
	u32 guc_wopcm_size;
	u32 guc_wopcm_rsvd;
	int err;

	if (!USES_GUC(i915))
		return 0;

	GEM_BUG_ON(!wopcm->size);

	if (i915_inject_load_failure())
		return -E2BIG;

	if (guc_fw_size >= wopcm->size) {
		DRM_ERROR("GuC FW (%uKiB) is too big to fit in WOPCM.",
			  guc_fw_size / 1024);
		return -E2BIG;
	}

	if (huc_fw_size >= wopcm->size) {
		DRM_ERROR("HuC FW (%uKiB) is too big to fit in WOPCM.",
			  huc_fw_size / 1024);
		return -E2BIG;
	}

	guc_wopcm_base = ALIGN(huc_fw_size + WOPCM_RESERVED_SIZE,
			       GUC_WOPCM_OFFSET_ALIGNMENT);
	if ((guc_wopcm_base + ctx_rsvd) >= wopcm->size) {
		DRM_ERROR("GuC WOPCM base (%uKiB) is too big.\n",
			  guc_wopcm_base / 1024);
		return -E2BIG;
	}

	guc_wopcm_size = wopcm->size - guc_wopcm_base - ctx_rsvd;
	guc_wopcm_size &= GUC_WOPCM_SIZE_MASK;

	DRM_DEBUG_DRIVER("Calculated GuC WOPCM Region: [%uKiB, %uKiB)\n",
			 guc_wopcm_base / 1024, guc_wopcm_size / 1024);

	guc_wopcm_rsvd = GUC_WOPCM_RESERVED + GUC_WOPCM_STACK_RESERVED;
	if ((guc_fw_size + guc_wopcm_rsvd) > guc_wopcm_size) {
		DRM_ERROR("Need %uKiB WOPCM for GuC, %uKiB available.\n",
			  (guc_fw_size + guc_wopcm_rsvd) / 1024,
			  guc_wopcm_size / 1024);
		return -E2BIG;
	}

	err = check_hw_restriction(i915, guc_wopcm_base, guc_wopcm_size,
				   huc_fw_size);
	if (err)
		return err;

	wopcm->guc.base = guc_wopcm_base;
	wopcm->guc.size = guc_wopcm_size;

	return 0;
}

static inline int write_and_verify(struct drm_i915_private *dev_priv,
				   i915_reg_t reg, u32 val, u32 mask,
				   u32 locked_bit)
{
	u32 reg_val;

	GEM_BUG_ON(val & ~mask);

	I915_WRITE(reg, val);

	reg_val = I915_READ(reg);

	return (reg_val & mask) != (val | locked_bit) ? -EIO : 0;
}

/**
 * intel_wopcm_init_hw() - Setup GuC WOPCM registers.
 * @wopcm: pointer to intel_wopcm.
 *
 * Setup the GuC WOPCM size and offset registers with the calculated values. It
 * will verify the register values to make sure the registers are locked with
 * correct values.
 *
 * Return: 0 on success. -EIO if registers were locked with incorrect values.
 */
int intel_wopcm_init_hw(struct intel_wopcm *wopcm)
{
	struct drm_i915_private *dev_priv = wopcm_to_i915(wopcm);
	u32 huc_agent;
	u32 mask;
	int err;

	if (!USES_GUC(dev_priv))
		return 0;

	GEM_BUG_ON(!HAS_GUC(dev_priv));
	GEM_BUG_ON(!wopcm->guc.size);
	GEM_BUG_ON(!wopcm->guc.base);

	err = write_and_verify(dev_priv, GUC_WOPCM_SIZE, wopcm->guc.size,
			       GUC_WOPCM_SIZE_MASK | GUC_WOPCM_SIZE_LOCKED,
			       GUC_WOPCM_SIZE_LOCKED);
	if (err)
		goto err_out;

	huc_agent = USES_HUC(dev_priv) ? HUC_LOADING_AGENT_GUC : 0;
	mask = GUC_WOPCM_OFFSET_MASK | GUC_WOPCM_OFFSET_VALID | huc_agent;
	err = write_and_verify(dev_priv, DMA_GUC_WOPCM_OFFSET,
			       wopcm->guc.base | huc_agent, mask,
			       GUC_WOPCM_OFFSET_VALID);
	if (err)
		goto err_out;

	return 0;

err_out:
	DRM_ERROR("Failed to init WOPCM registers:\n");
	DRM_ERROR("DMA_GUC_WOPCM_OFFSET=%#x\n",
		  I915_READ(DMA_GUC_WOPCM_OFFSET));
	DRM_ERROR("GUC_WOPCM_SIZE=%#x\n", I915_READ(GUC_WOPCM_SIZE));

	return err;
}

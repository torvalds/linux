/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Vinit Azad <vinit.azad@intel.com>
 *    Ben Widawsky <ben@bwidawsk.net>
 *    Dave Gordon <david.s.gordon@intel.com>
 *    Alex Dai <yu.dai@intel.com>
 */

#include "intel_guc_fw.h"
#include "i915_drv.h"

#define SKL_FW_MAJOR 6
#define SKL_FW_MINOR 1

#define BXT_FW_MAJOR 8
#define BXT_FW_MINOR 7

#define KBL_FW_MAJOR 9
#define KBL_FW_MINOR 14

#define GLK_FW_MAJOR 10
#define GLK_FW_MINOR 56

#define GUC_FW_PATH(platform, major, minor) \
       "i915/" __stringify(platform) "_guc_ver" __stringify(major) "_" __stringify(minor) ".bin"

#define I915_SKL_GUC_UCODE GUC_FW_PATH(skl, SKL_FW_MAJOR, SKL_FW_MINOR)
MODULE_FIRMWARE(I915_SKL_GUC_UCODE);

#define I915_BXT_GUC_UCODE GUC_FW_PATH(bxt, BXT_FW_MAJOR, BXT_FW_MINOR)
MODULE_FIRMWARE(I915_BXT_GUC_UCODE);

#define I915_KBL_GUC_UCODE GUC_FW_PATH(kbl, KBL_FW_MAJOR, KBL_FW_MINOR)
MODULE_FIRMWARE(I915_KBL_GUC_UCODE);

#define I915_GLK_GUC_UCODE GUC_FW_PATH(glk, GLK_FW_MAJOR, GLK_FW_MINOR)

/**
 * intel_guc_fw_select() - selects GuC firmware for uploading
 *
 * @guc:	intel_guc struct
 *
 * Return: zero when we know firmware, non-zero in other case
 */
int intel_guc_fw_select(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	intel_uc_fw_init(&guc->fw, INTEL_UC_FW_TYPE_GUC);

	if (i915_modparams.guc_firmware_path) {
		guc->fw.path = i915_modparams.guc_firmware_path;
		guc->fw.major_ver_wanted = 0;
		guc->fw.minor_ver_wanted = 0;
	} else if (IS_SKYLAKE(dev_priv)) {
		guc->fw.path = I915_SKL_GUC_UCODE;
		guc->fw.major_ver_wanted = SKL_FW_MAJOR;
		guc->fw.minor_ver_wanted = SKL_FW_MINOR;
	} else if (IS_BROXTON(dev_priv)) {
		guc->fw.path = I915_BXT_GUC_UCODE;
		guc->fw.major_ver_wanted = BXT_FW_MAJOR;
		guc->fw.minor_ver_wanted = BXT_FW_MINOR;
	} else if (IS_KABYLAKE(dev_priv) || IS_COFFEELAKE(dev_priv)) {
		guc->fw.path = I915_KBL_GUC_UCODE;
		guc->fw.major_ver_wanted = KBL_FW_MAJOR;
		guc->fw.minor_ver_wanted = KBL_FW_MINOR;
	} else if (IS_GEMINILAKE(dev_priv)) {
		guc->fw.path = I915_GLK_GUC_UCODE;
		guc->fw.major_ver_wanted = GLK_FW_MAJOR;
		guc->fw.minor_ver_wanted = GLK_FW_MINOR;
	} else {
		DRM_ERROR("No GuC firmware known for platform with GuC!\n");
		return -ENOENT;
	}

	return 0;
}

/*
 * Read the GuC status register (GUC_STATUS) and store it in the
 * specified location; then return a boolean indicating whether
 * the value matches either of two values representing completion
 * of the GuC boot process.
 *
 * This is used for polling the GuC status in a wait_for()
 * loop below.
 */
static inline bool guc_ucode_response(struct drm_i915_private *dev_priv,
				      u32 *status)
{
	u32 val = I915_READ(GUC_STATUS);
	u32 uk_val = val & GS_UKERNEL_MASK;
	*status = val;
	return (uk_val == GS_UKERNEL_READY ||
		((val & GS_MIA_CORE_STATE) && uk_val == GS_UKERNEL_LAPIC_DONE));
}

/*
 * Transfer the firmware image to RAM for execution by the microcontroller.
 *
 * Architecturally, the DMA engine is bidirectional, and can potentially even
 * transfer between GTT locations. This functionality is left out of the API
 * for now as there is no need for it.
 *
 * Note that GuC needs the CSS header plus uKernel code to be copied by the
 * DMA engine in one operation, whereas the RSA signature is loaded via MMIO.
 */
static int guc_ucode_xfer_dma(struct drm_i915_private *dev_priv,
			      struct i915_vma *vma)
{
	struct intel_uc_fw *guc_fw = &dev_priv->guc.fw;
	unsigned long offset;
	struct sg_table *sg = vma->pages;
	u32 status, rsa[UOS_RSA_SCRATCH_MAX_COUNT];
	int i, ret = 0;

	/* where RSA signature starts */
	offset = guc_fw->rsa_offset;

	/* Copy RSA signature from the fw image to HW for verification */
	sg_pcopy_to_buffer(sg->sgl, sg->nents, rsa, sizeof(rsa), offset);
	for (i = 0; i < UOS_RSA_SCRATCH_MAX_COUNT; i++)
		I915_WRITE(UOS_RSA_SCRATCH(i), rsa[i]);

	/* The header plus uCode will be copied to WOPCM via DMA, excluding any
	 * other components */
	I915_WRITE(DMA_COPY_SIZE, guc_fw->header_size + guc_fw->ucode_size);

	/* Set the source address for the new blob */
	offset = guc_ggtt_offset(vma) + guc_fw->header_offset;
	I915_WRITE(DMA_ADDR_0_LOW, lower_32_bits(offset));
	I915_WRITE(DMA_ADDR_0_HIGH, upper_32_bits(offset) & 0xFFFF);

	/*
	 * Set the DMA destination. Current uCode expects the code to be
	 * loaded at 8k; locations below this are used for the stack.
	 */
	I915_WRITE(DMA_ADDR_1_LOW, 0x2000);
	I915_WRITE(DMA_ADDR_1_HIGH, DMA_ADDRESS_SPACE_WOPCM);

	/* Finally start the DMA */
	I915_WRITE(DMA_CTRL, _MASKED_BIT_ENABLE(UOS_MOVE | START_DMA));

	/*
	 * Wait for the DMA to complete & the GuC to start up.
	 * NB: Docs recommend not using the interrupt for completion.
	 * Measurements indicate this should take no more than 20ms, so a
	 * timeout here indicates that the GuC has failed and is unusable.
	 * (Higher levels of the driver will attempt to fall back to
	 * execlist mode if this happens.)
	 */
	ret = wait_for(guc_ucode_response(dev_priv, &status), 100);

	DRM_DEBUG_DRIVER("DMA status 0x%x, GuC status 0x%x\n",
			I915_READ(DMA_CTRL), status);

	if ((status & GS_BOOTROM_MASK) == GS_BOOTROM_RSA_FAILED) {
		DRM_ERROR("GuC firmware signature verification failed\n");
		ret = -ENOEXEC;
	}

	DRM_DEBUG_DRIVER("returning %d\n", ret);

	return ret;
}

/*
 * Load the GuC firmware blob into the MinuteIA.
 */
static int guc_ucode_xfer(struct intel_uc_fw *guc_fw, struct i915_vma *vma)
{
	struct intel_guc *guc = container_of(guc_fw, struct intel_guc, fw);
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	int ret;

	GEM_BUG_ON(guc_fw->type != INTEL_UC_FW_TYPE_GUC);

	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	/* Enable MIA caching. GuC clock gating is disabled. */
	I915_WRITE(GUC_SHIM_CONTROL, GUC_SHIM_CONTROL_VALUE);

	/* WaDisableMinuteIaClockGating:bxt */
	if (IS_BXT_REVID(dev_priv, 0, BXT_REVID_A1)) {
		I915_WRITE(GUC_SHIM_CONTROL, (I915_READ(GUC_SHIM_CONTROL) &
					      ~GUC_ENABLE_MIA_CLOCK_GATING));
	}

	/* WaC6DisallowByGfxPause:bxt */
	if (IS_BXT_REVID(dev_priv, 0, BXT_REVID_B0))
		I915_WRITE(GEN6_GFXPAUSE, 0x30FFF);

	if (IS_GEN9_LP(dev_priv))
		I915_WRITE(GEN9LP_GT_PM_CONFIG, GT_DOORBELL_ENABLE);
	else
		I915_WRITE(GEN9_GT_PM_CONFIG, GT_DOORBELL_ENABLE);

	if (IS_GEN9(dev_priv)) {
		/* DOP Clock Gating Enable for GuC clocks */
		I915_WRITE(GEN7_MISCCPCTL, (GEN8_DOP_CLOCK_GATE_GUC_ENABLE |
					    I915_READ(GEN7_MISCCPCTL)));

		/* allows for 5us (in 10ns units) before GT can go to RC6 */
		I915_WRITE(GUC_ARAT_C6DIS, 0x1FF);
	}

	ret = guc_ucode_xfer_dma(dev_priv, vma);

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

	return ret;
}

/**
 * intel_guc_fw_upload() - finish preparing the GuC for activity
 * @guc: intel_guc structure
 *
 * Called during driver loading and also after a GPU reset.
 *
 * The main action required here it to load the GuC uCode into the device.
 * The firmware image should have already been fetched into memory by the
 * earlier call to intel_guc_init(), so here we need only check that
 * worked, and then transfer the image to the h/w.
 *
 * Return:	non-zero code on error
 */
int intel_guc_fw_upload(struct intel_guc *guc)
{
	return intel_uc_fw_upload(&guc->fw, guc_ucode_xfer);
}

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

#define __MAKE_GUC_FW_PATH(KEY) \
	"i915/" \
	__stringify(KEY##_GUC_FW_PREFIX) "_guc_" \
	__stringify(KEY##_GUC_FW_MAJOR) "." \
	__stringify(KEY##_GUC_FW_MINOR) "." \
	__stringify(KEY##_GUC_FW_PATCH) ".bin"

#define SKL_GUC_FW_PREFIX skl
#define SKL_GUC_FW_MAJOR 33
#define SKL_GUC_FW_MINOR 0
#define SKL_GUC_FW_PATCH 0
#define SKL_GUC_FIRMWARE_PATH __MAKE_GUC_FW_PATH(SKL)
MODULE_FIRMWARE(SKL_GUC_FIRMWARE_PATH);

#define BXT_GUC_FW_PREFIX bxt
#define BXT_GUC_FW_MAJOR 33
#define BXT_GUC_FW_MINOR 0
#define BXT_GUC_FW_PATCH 0
#define BXT_GUC_FIRMWARE_PATH __MAKE_GUC_FW_PATH(BXT)
MODULE_FIRMWARE(BXT_GUC_FIRMWARE_PATH);

#define KBL_GUC_FW_PREFIX kbl
#define KBL_GUC_FW_MAJOR 33
#define KBL_GUC_FW_MINOR 0
#define KBL_GUC_FW_PATCH 0
#define KBL_GUC_FIRMWARE_PATH __MAKE_GUC_FW_PATH(KBL)
MODULE_FIRMWARE(KBL_GUC_FIRMWARE_PATH);

#define GLK_GUC_FW_PREFIX glk
#define GLK_GUC_FW_MAJOR 33
#define GLK_GUC_FW_MINOR 0
#define GLK_GUC_FW_PATCH 0
#define GLK_GUC_FIRMWARE_PATH __MAKE_GUC_FW_PATH(GLK)
MODULE_FIRMWARE(GLK_GUC_FIRMWARE_PATH);

#define ICL_GUC_FW_PREFIX icl
#define ICL_GUC_FW_MAJOR 33
#define ICL_GUC_FW_MINOR 0
#define ICL_GUC_FW_PATCH 0
#define ICL_GUC_FIRMWARE_PATH __MAKE_GUC_FW_PATH(ICL)
MODULE_FIRMWARE(ICL_GUC_FIRMWARE_PATH);

static void guc_fw_select(struct intel_uc_fw *guc_fw)
{
	struct intel_guc *guc = container_of(guc_fw, struct intel_guc, fw);
	struct drm_i915_private *i915 = guc_to_i915(guc);

	GEM_BUG_ON(guc_fw->type != INTEL_UC_FW_TYPE_GUC);

	if (!HAS_GUC(i915))
		return;

	if (i915_modparams.guc_firmware_path) {
		guc_fw->path = i915_modparams.guc_firmware_path;
		guc_fw->major_ver_wanted = 0;
		guc_fw->minor_ver_wanted = 0;
	} else if (IS_ICELAKE(i915)) {
		guc_fw->path = ICL_GUC_FIRMWARE_PATH;
		guc_fw->major_ver_wanted = ICL_GUC_FW_MAJOR;
		guc_fw->minor_ver_wanted = ICL_GUC_FW_MINOR;
	} else if (IS_GEMINILAKE(i915)) {
		guc_fw->path = GLK_GUC_FIRMWARE_PATH;
		guc_fw->major_ver_wanted = GLK_GUC_FW_MAJOR;
		guc_fw->minor_ver_wanted = GLK_GUC_FW_MINOR;
	} else if (IS_KABYLAKE(i915) || IS_COFFEELAKE(i915)) {
		guc_fw->path = KBL_GUC_FIRMWARE_PATH;
		guc_fw->major_ver_wanted = KBL_GUC_FW_MAJOR;
		guc_fw->minor_ver_wanted = KBL_GUC_FW_MINOR;
	} else if (IS_BROXTON(i915)) {
		guc_fw->path = BXT_GUC_FIRMWARE_PATH;
		guc_fw->major_ver_wanted = BXT_GUC_FW_MAJOR;
		guc_fw->minor_ver_wanted = BXT_GUC_FW_MINOR;
	} else if (IS_SKYLAKE(i915)) {
		guc_fw->path = SKL_GUC_FIRMWARE_PATH;
		guc_fw->major_ver_wanted = SKL_GUC_FW_MAJOR;
		guc_fw->minor_ver_wanted = SKL_GUC_FW_MINOR;
	}
}

/**
 * intel_guc_fw_init_early() - initializes GuC firmware struct
 * @guc: intel_guc struct
 *
 * On platforms with GuC selects firmware for uploading
 */
void intel_guc_fw_init_early(struct intel_guc *guc)
{
	struct intel_uc_fw *guc_fw = &guc->fw;

	intel_uc_fw_init_early(guc_fw, INTEL_UC_FW_TYPE_GUC);
	guc_fw_select(guc_fw);
}

static void guc_prepare_xfer(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	/* Must program this register before loading the ucode with DMA */
	I915_WRITE(GUC_SHIM_CONTROL, GUC_DISABLE_SRAM_INIT_TO_ZEROES |
				     GUC_ENABLE_READ_CACHE_LOGIC |
				     GUC_ENABLE_MIA_CACHING |
				     GUC_ENABLE_READ_CACHE_FOR_SRAM_DATA |
				     GUC_ENABLE_READ_CACHE_FOR_WOPCM_DATA |
				     GUC_ENABLE_MIA_CLOCK_GATING);

	if (IS_GEN9_LP(dev_priv))
		I915_WRITE(GEN9LP_GT_PM_CONFIG, GT_DOORBELL_ENABLE);
	else
		I915_WRITE(GEN9_GT_PM_CONFIG, GT_DOORBELL_ENABLE);

	if (IS_GEN(dev_priv, 9)) {
		/* DOP Clock Gating Enable for GuC clocks */
		I915_WRITE(GEN7_MISCCPCTL, (GEN8_DOP_CLOCK_GATE_GUC_ENABLE |
					    I915_READ(GEN7_MISCCPCTL)));

		/* allows for 5us (in 10ns units) before GT can go to RC6 */
		I915_WRITE(GUC_ARAT_C6DIS, 0x1FF);
	}
}

/* Copy RSA signature from the fw image to HW for verification */
static void guc_xfer_rsa(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct intel_uc_fw *fw = &guc->fw;
	struct sg_table *pages = fw->obj->mm.pages;
	u32 rsa[UOS_RSA_SCRATCH_COUNT];
	int i;

	sg_pcopy_to_buffer(pages->sgl, pages->nents,
			   rsa, sizeof(rsa), fw->rsa_offset);

	for (i = 0; i < UOS_RSA_SCRATCH_COUNT; i++)
		I915_WRITE(UOS_RSA_SCRATCH(i), rsa[i]);
}

static bool guc_xfer_completed(struct intel_guc *guc, u32 *status)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	/* Did we complete the xfer? */
	*status = I915_READ(DMA_CTRL);
	return !(*status & START_DMA);
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
static inline bool guc_ready(struct intel_guc *guc, u32 *status)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 val = I915_READ(GUC_STATUS);
	u32 uk_val = val & GS_UKERNEL_MASK;

	*status = val;
	return (uk_val == GS_UKERNEL_READY) ||
		((val & GS_MIA_CORE_STATE) && (uk_val == GS_UKERNEL_LAPIC_DONE));
}

static int guc_wait_ucode(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_i915(guc);
	u32 status;
	int ret;

	/*
	 * Wait for the GuC to start up.
	 * NB: Docs recommend not using the interrupt for completion.
	 * Measurements indicate this should take no more than 20ms, so a
	 * timeout here indicates that the GuC has failed and is unusable.
	 * (Higher levels of the driver may decide to reset the GuC and
	 * attempt the ucode load again if this happens.)
	 */
	ret = wait_for(guc_ready(guc, &status), 100);
	DRM_DEBUG_DRIVER("GuC status %#x\n", status);

	if ((status & GS_BOOTROM_MASK) == GS_BOOTROM_RSA_FAILED) {
		DRM_ERROR("GuC firmware signature verification failed\n");
		ret = -ENOEXEC;
	}

	if ((status & GS_UKERNEL_MASK) == GS_UKERNEL_EXCEPTION) {
		DRM_ERROR("GuC firmware exception. EIP: %#x\n",
			  intel_uncore_read(&i915->uncore, SOFT_SCRATCH(13)));
		ret = -ENXIO;
	}

	if (ret == 0 && !guc_xfer_completed(guc, &status)) {
		DRM_ERROR("GuC is ready, but the xfer %08x is incomplete\n",
			  status);
		ret = -ENXIO;
	}

	return ret;
}

/*
 * Transfer the firmware image to RAM for execution by the microcontroller.
 *
 * Architecturally, the DMA engine is bidirectional, and can potentially even
 * transfer between GTT locations. This functionality is left out of the API
 * for now as there is no need for it.
 */
static int guc_xfer_ucode(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct intel_uc_fw *guc_fw = &guc->fw;
	unsigned long offset;

	/*
	 * The header plus uCode will be copied to WOPCM via DMA, excluding any
	 * other components
	 */
	I915_WRITE(DMA_COPY_SIZE, guc_fw->header_size + guc_fw->ucode_size);

	/* Set the source address for the new blob */
	offset = intel_uc_fw_ggtt_offset(guc_fw) + guc_fw->header_offset;
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

	return guc_wait_ucode(guc);
}
/*
 * Load the GuC firmware blob into the MinuteIA.
 */
static int guc_fw_xfer(struct intel_uc_fw *guc_fw)
{
	struct intel_guc *guc = container_of(guc_fw, struct intel_guc, fw);
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	int ret;

	GEM_BUG_ON(guc_fw->type != INTEL_UC_FW_TYPE_GUC);

	intel_uncore_forcewake_get(&dev_priv->uncore, FORCEWAKE_ALL);

	guc_prepare_xfer(guc);

	/*
	 * Note that GuC needs the CSS header plus uKernel code to be copied
	 * by the DMA engine in one operation, whereas the RSA signature is
	 * loaded via MMIO.
	 */
	guc_xfer_rsa(guc);

	ret = guc_xfer_ucode(guc);

	intel_uncore_forcewake_put(&dev_priv->uncore, FORCEWAKE_ALL);

	return ret;
}

/**
 * intel_guc_fw_upload() - load GuC uCode to device
 * @guc: intel_guc structure
 *
 * Called from intel_uc_init_hw() during driver load, resume from sleep and
 * after a GPU reset.
 *
 * The firmware image should have already been fetched into memory, so only
 * check that fetch succeeded, and then transfer the image to the h/w.
 *
 * Return:	non-zero code on error
 */
int intel_guc_fw_upload(struct intel_guc *guc)
{
	return intel_uc_fw_upload(&guc->fw, guc_fw_xfer);
}

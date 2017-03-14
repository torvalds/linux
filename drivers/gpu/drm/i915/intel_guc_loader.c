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
#include <linux/firmware.h>
#include "i915_drv.h"
#include "intel_uc.h"

/**
 * DOC: GuC-specific firmware loader
 *
 * intel_guc:
 * Top level structure of guc. It handles firmware loading and manages client
 * pool and doorbells. intel_guc owns a i915_guc_client to replace the legacy
 * ExecList submission.
 *
 * Firmware versioning:
 * The firmware build process will generate a version header file with major and
 * minor version defined. The versions are built into CSS header of firmware.
 * i915 kernel driver set the minimal firmware version required per platform.
 * The firmware installation package will install (symbolic link) proper version
 * of firmware.
 *
 * GuC address space:
 * GuC does not allow any gfx GGTT address that falls into range [0, WOPCM_TOP),
 * which is reserved for Boot ROM, SRAM and WOPCM. Currently this top address is
 * 512K. In order to exclude 0-512K address space from GGTT, all gfx objects
 * used by GuC is pinned with PIN_OFFSET_BIAS along with size of WOPCM.
 *
 */

#define SKL_FW_MAJOR 6
#define SKL_FW_MINOR 1

#define BXT_FW_MAJOR 8
#define BXT_FW_MINOR 7

#define KBL_FW_MAJOR 9
#define KBL_FW_MINOR 14

#define GUC_FW_PATH(platform, major, minor) \
       "i915/" __stringify(platform) "_guc_ver" __stringify(major) "_" __stringify(minor) ".bin"

#define I915_SKL_GUC_UCODE GUC_FW_PATH(skl, SKL_FW_MAJOR, SKL_FW_MINOR)
MODULE_FIRMWARE(I915_SKL_GUC_UCODE);

#define I915_BXT_GUC_UCODE GUC_FW_PATH(bxt, BXT_FW_MAJOR, BXT_FW_MINOR)
MODULE_FIRMWARE(I915_BXT_GUC_UCODE);

#define I915_KBL_GUC_UCODE GUC_FW_PATH(kbl, KBL_FW_MAJOR, KBL_FW_MINOR)
MODULE_FIRMWARE(I915_KBL_GUC_UCODE);

/* User-friendly representation of an enum */
const char *intel_uc_fw_status_repr(enum intel_uc_fw_status status)
{
	switch (status) {
	case INTEL_UC_FIRMWARE_FAIL:
		return "FAIL";
	case INTEL_UC_FIRMWARE_NONE:
		return "NONE";
	case INTEL_UC_FIRMWARE_PENDING:
		return "PENDING";
	case INTEL_UC_FIRMWARE_SUCCESS:
		return "SUCCESS";
	default:
		return "UNKNOWN!";
	}
};

static u32 get_gttype(struct drm_i915_private *dev_priv)
{
	/* XXX: GT type based on PCI device ID? field seems unused by fw */
	return 0;
}

static u32 get_core_family(struct drm_i915_private *dev_priv)
{
	u32 gen = INTEL_GEN(dev_priv);

	switch (gen) {
	case 9:
		return GFXCORE_FAMILY_GEN9;

	default:
		WARN(1, "GEN%d does not support GuC operation!\n", gen);
		return GFXCORE_FAMILY_UNKNOWN;
	}
}

/*
 * Initialise the GuC parameter block before starting the firmware
 * transfer. These parameters are read by the firmware on startup
 * and cannot be changed thereafter.
 */
static void guc_params_init(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	u32 params[GUC_CTL_MAX_DWORDS];
	int i;

	memset(&params, 0, sizeof(params));

	params[GUC_CTL_DEVICE_INFO] |=
		(get_gttype(dev_priv) << GUC_CTL_GTTYPE_SHIFT) |
		(get_core_family(dev_priv) << GUC_CTL_COREFAMILY_SHIFT);

	/*
	 * GuC ARAT increment is 10 ns. GuC default scheduler quantum is one
	 * second. This ARAR is calculated by:
	 * Scheduler-Quantum-in-ns / ARAT-increment-in-ns = 1000000000 / 10
	 */
	params[GUC_CTL_ARAT_HIGH] = 0;
	params[GUC_CTL_ARAT_LOW] = 100000000;

	params[GUC_CTL_WA] |= GUC_CTL_WA_UK_BY_DRIVER;

	params[GUC_CTL_FEATURE] |= GUC_CTL_DISABLE_SCHEDULER |
			GUC_CTL_VCS2_ENABLED;

	params[GUC_CTL_LOG_PARAMS] = guc->log.flags;

	if (i915.guc_log_level >= 0) {
		params[GUC_CTL_DEBUG] =
			i915.guc_log_level << GUC_LOG_VERBOSITY_SHIFT;
	} else
		params[GUC_CTL_DEBUG] = GUC_LOG_DISABLED;

	if (guc->ads_vma) {
		u32 ads = guc_ggtt_offset(guc->ads_vma) >> PAGE_SHIFT;
		params[GUC_CTL_DEBUG] |= ads << GUC_ADS_ADDR_SHIFT;
		params[GUC_CTL_DEBUG] |= GUC_ADS_ENABLED;
	}

	/* If GuC submission is enabled, set up additional parameters here */
	if (i915.enable_guc_submission) {
		u32 pgs = guc_ggtt_offset(dev_priv->guc.ctx_pool_vma);
		u32 ctx_in_16 = GUC_MAX_GPU_CONTEXTS / 16;

		pgs >>= PAGE_SHIFT;
		params[GUC_CTL_CTXINFO] = (pgs << GUC_CTL_BASE_ADDR_SHIFT) |
			(ctx_in_16 << GUC_CTL_CTXNUM_IN16_SHIFT);

		params[GUC_CTL_FEATURE] |= GUC_CTL_KERNEL_SUBMISSIONS;

		/* Unmask this bit to enable the GuC's internal scheduler */
		params[GUC_CTL_FEATURE] &= ~GUC_CTL_DISABLE_SCHEDULER;
	}

	I915_WRITE(SOFT_SCRATCH(0), 0);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		I915_WRITE(SOFT_SCRATCH(1 + i), params[i]);
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

u32 intel_guc_wopcm_size(struct drm_i915_private *dev_priv)
{
	u32 wopcm_size = GUC_WOPCM_TOP;

	/* On BXT, the top of WOPCM is reserved for RC6 context */
	if (IS_GEN9_LP(dev_priv))
		wopcm_size -= BXT_GUC_WOPCM_RC6_RESERVED;

	return wopcm_size;
}

/*
 * Load the GuC firmware blob into the MinuteIA.
 */
static int guc_ucode_xfer(struct drm_i915_private *dev_priv)
{
	struct intel_uc_fw *guc_fw = &dev_priv->guc.fw;
	struct i915_vma *vma;
	int ret;

	ret = i915_gem_object_set_to_gtt_domain(guc_fw->obj, false);
	if (ret) {
		DRM_DEBUG_DRIVER("set-domain failed %d\n", ret);
		return ret;
	}

	vma = i915_gem_object_ggtt_pin(guc_fw->obj, NULL, 0, 0,
				       PIN_OFFSET_BIAS | GUC_WOPCM_TOP);
	if (IS_ERR(vma)) {
		DRM_DEBUG_DRIVER("pin failed %d\n", (int)PTR_ERR(vma));
		return PTR_ERR(vma);
	}

	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	/* init WOPCM */
	I915_WRITE(GUC_WOPCM_SIZE, intel_guc_wopcm_size(dev_priv));
	I915_WRITE(DMA_GUC_WOPCM_OFFSET, GUC_WOPCM_OFFSET_VALUE);

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

	guc_params_init(dev_priv);

	ret = guc_ucode_xfer_dma(dev_priv, vma);

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

	/*
	 * We keep the object pages for reuse during resume. But we can unpin it
	 * now that DMA has completed, so it doesn't continue to take up space.
	 */
	i915_vma_unpin(vma);

	return ret;
}

static int guc_hw_reset(struct drm_i915_private *dev_priv)
{
	int ret;
	u32 guc_status;

	ret = intel_guc_reset(dev_priv);
	if (ret) {
		DRM_ERROR("GuC reset failed, ret = %d\n", ret);
		return ret;
	}

	guc_status = I915_READ(GUC_STATUS);
	WARN(!(guc_status & GS_MIA_IN_RESET),
	     "GuC status: 0x%x, MIA core expected to be in reset\n", guc_status);

	return ret;
}

/**
 * intel_guc_init_hw() - finish preparing the GuC for activity
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
int intel_guc_init_hw(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	const char *fw_path = guc->fw.path;
	int retries, ret, err;

	DRM_DEBUG_DRIVER("GuC fw status: path %s, fetch %s, load %s\n",
		fw_path,
		intel_uc_fw_status_repr(guc->fw.fetch_status),
		intel_uc_fw_status_repr(guc->fw.load_status));

	/* Loading forbidden, or no firmware to load? */
	if (!i915.enable_guc_loading) {
		err = 0;
		goto fail;
	} else if (fw_path == NULL) {
		/* Device is known to have no uCode (e.g. no GuC) */
		err = -ENXIO;
		goto fail;
	} else if (*fw_path == '\0') {
		/* Device has a GuC but we don't know what f/w to load? */
		WARN(1, "No GuC firmware known for this platform!\n");
		err = -ENODEV;
		goto fail;
	}

	/* Fetch failed, or already fetched but failed to load? */
	if (guc->fw.fetch_status != INTEL_UC_FIRMWARE_SUCCESS) {
		err = -EIO;
		goto fail;
	} else if (guc->fw.load_status == INTEL_UC_FIRMWARE_FAIL) {
		err = -ENOEXEC;
		goto fail;
	}

	gen9_reset_guc_interrupts(dev_priv);

	/* We need to notify the guc whenever we change the GGTT */
	i915_ggtt_enable_guc(dev_priv);

	guc->fw.load_status = INTEL_UC_FIRMWARE_PENDING;

	DRM_DEBUG_DRIVER("GuC fw status: fetch %s, load %s\n",
		intel_uc_fw_status_repr(guc->fw.fetch_status),
		intel_uc_fw_status_repr(guc->fw.load_status));

	err = i915_guc_submission_init(dev_priv);
	if (err)
		goto fail;

	/*
	 * WaEnableuKernelHeaderValidFix:skl,bxt
	 * For BXT, this is only upto B0 but below WA is required for later
	 * steppings also so this is extended as well.
	 */
	/* WaEnableGuCBootHashCheckNotSet:skl,bxt */
	for (retries = 3; ; ) {
		/*
		 * Always reset the GuC just before (re)loading, so
		 * that the state and timing are fairly predictable
		 */
		err = guc_hw_reset(dev_priv);
		if (err)
			goto fail;

		intel_huc_init_hw(&dev_priv->huc);
		err = guc_ucode_xfer(dev_priv);
		if (!err)
			break;

		if (--retries == 0)
			goto fail;

		DRM_INFO("GuC fw load failed: %d; will reset and "
			 "retry %d more time(s)\n", err, retries);
	}

	guc->fw.load_status = INTEL_UC_FIRMWARE_SUCCESS;

	intel_guc_auth_huc(dev_priv);

	if (i915.enable_guc_submission) {
		if (i915.guc_log_level >= 0)
			gen9_enable_guc_interrupts(dev_priv);

		err = i915_guc_submission_enable(dev_priv);
		if (err)
			goto fail;
	}

	DRM_INFO("GuC %s (firmware %s [version %u.%u])\n",
		 i915.enable_guc_submission ? "submission enabled" : "loaded",
		 guc->fw.path,
		 guc->fw.major_ver_found, guc->fw.minor_ver_found);

	return 0;

fail:
	if (guc->fw.load_status == INTEL_UC_FIRMWARE_PENDING)
		guc->fw.load_status = INTEL_UC_FIRMWARE_FAIL;

	i915_guc_submission_disable(dev_priv);
	i915_guc_submission_fini(dev_priv);
	i915_ggtt_disable_guc(dev_priv);

	/*
	 * We've failed to load the firmware :(
	 *
	 * Decide whether to disable GuC submission and fall back to
	 * execlist mode, and whether to hide the error by returning
	 * zero or to return -EIO, which the caller will treat as a
	 * nonfatal error (i.e. it doesn't prevent driver load, but
	 * marks the GPU as wedged until reset).
	 */
	if (i915.enable_guc_loading > 1) {
		ret = -EIO;
	} else if (i915.enable_guc_submission > 1) {
		ret = -EIO;
	} else {
		ret = 0;
	}

	if (err == 0 && !HAS_GUC_UCODE(dev_priv))
		;	/* Don't mention the GuC! */
	else if (err == 0)
		DRM_INFO("GuC firmware load skipped\n");
	else if (ret != -EIO)
		DRM_NOTE("GuC firmware load failed: %d\n", err);
	else
		DRM_WARN("GuC firmware load failed: %d\n", err);

	if (i915.enable_guc_submission) {
		if (fw_path == NULL)
			DRM_INFO("GuC submission without firmware not supported\n");
		if (ret == 0)
			DRM_NOTE("Falling back from GuC submission to execlist mode\n");
		else
			DRM_ERROR("GuC init failed: %d\n", ret);
	}
	i915.enable_guc_submission = 0;

	return ret;
}

void intel_uc_fw_fetch(struct drm_i915_private *dev_priv,
			 struct intel_uc_fw *uc_fw)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	struct drm_i915_gem_object *obj;
	const struct firmware *fw = NULL;
	struct uc_css_header *css;
	size_t size;
	int err;

	DRM_DEBUG_DRIVER("before requesting firmware: uC fw fetch status %s\n",
		intel_uc_fw_status_repr(uc_fw->fetch_status));

	err = request_firmware(&fw, uc_fw->path, &pdev->dev);
	if (err)
		goto fail;
	if (!fw)
		goto fail;

	DRM_DEBUG_DRIVER("fetch uC fw from %s succeeded, fw %p\n",
		uc_fw->path, fw);

	/* Check the size of the blob before examining buffer contents */
	if (fw->size < sizeof(struct uc_css_header)) {
		DRM_NOTE("Firmware header is missing\n");
		goto fail;
	}

	css = (struct uc_css_header *)fw->data;

	/* Firmware bits always start from header */
	uc_fw->header_offset = 0;
	uc_fw->header_size = (css->header_size_dw - css->modulus_size_dw -
		css->key_size_dw - css->exponent_size_dw) * sizeof(u32);

	if (uc_fw->header_size != sizeof(struct uc_css_header)) {
		DRM_NOTE("CSS header definition mismatch\n");
		goto fail;
	}

	/* then, uCode */
	uc_fw->ucode_offset = uc_fw->header_offset + uc_fw->header_size;
	uc_fw->ucode_size = (css->size_dw - css->header_size_dw) * sizeof(u32);

	/* now RSA */
	if (css->key_size_dw != UOS_RSA_SCRATCH_MAX_COUNT) {
		DRM_NOTE("RSA key size is bad\n");
		goto fail;
	}
	uc_fw->rsa_offset = uc_fw->ucode_offset + uc_fw->ucode_size;
	uc_fw->rsa_size = css->key_size_dw * sizeof(u32);

	/* At least, it should have header, uCode and RSA. Size of all three. */
	size = uc_fw->header_size + uc_fw->ucode_size + uc_fw->rsa_size;
	if (fw->size < size) {
		DRM_NOTE("Missing firmware components\n");
		goto fail;
	}

	/*
	 * The GuC firmware image has the version number embedded at a well-known
	 * offset within the firmware blob; note that major / minor version are
	 * TWO bytes each (i.e. u16), although all pointers and offsets are defined
	 * in terms of bytes (u8).
	 */
	switch (uc_fw->fw) {
	case INTEL_UC_FW_TYPE_GUC:
		/* Header and uCode will be loaded to WOPCM. Size of the two. */
		size = uc_fw->header_size + uc_fw->ucode_size;

		/* Top 32k of WOPCM is reserved (8K stack + 24k RC6 context). */
		if (size > intel_guc_wopcm_size(dev_priv)) {
			DRM_ERROR("Firmware is too large to fit in WOPCM\n");
			goto fail;
		}
		uc_fw->major_ver_found = css->guc.sw_version >> 16;
		uc_fw->minor_ver_found = css->guc.sw_version & 0xFFFF;
		break;

	case INTEL_UC_FW_TYPE_HUC:
		uc_fw->major_ver_found = css->huc.sw_version >> 16;
		uc_fw->minor_ver_found = css->huc.sw_version & 0xFFFF;
		break;

	default:
		DRM_ERROR("Unknown firmware type %d\n", uc_fw->fw);
		err = -ENOEXEC;
		goto fail;
	}

	if (uc_fw->major_ver_found != uc_fw->major_ver_wanted ||
	    uc_fw->minor_ver_found < uc_fw->minor_ver_wanted) {
		DRM_NOTE("uC firmware version %d.%d, required %d.%d\n",
			uc_fw->major_ver_found, uc_fw->minor_ver_found,
			uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted);
		err = -ENOEXEC;
		goto fail;
	}

	DRM_DEBUG_DRIVER("firmware version %d.%d OK (minimum %d.%d)\n",
			uc_fw->major_ver_found, uc_fw->minor_ver_found,
			uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted);

	mutex_lock(&dev_priv->drm.struct_mutex);
	obj = i915_gem_object_create_from_data(dev_priv, fw->data, fw->size);
	mutex_unlock(&dev_priv->drm.struct_mutex);
	if (IS_ERR_OR_NULL(obj)) {
		err = obj ? PTR_ERR(obj) : -ENOMEM;
		goto fail;
	}

	uc_fw->obj = obj;
	uc_fw->size = fw->size;

	DRM_DEBUG_DRIVER("uC fw fetch status SUCCESS, obj %p\n",
			uc_fw->obj);

	release_firmware(fw);
	uc_fw->fetch_status = INTEL_UC_FIRMWARE_SUCCESS;
	return;

fail:
	DRM_WARN("Failed to fetch valid uC firmware from %s (error %d)\n",
		 uc_fw->path, err);
	DRM_DEBUG_DRIVER("uC fw fetch status FAIL; err %d, fw %p, obj %p\n",
		err, fw, uc_fw->obj);

	obj = fetch_and_zero(&uc_fw->obj);
	if (obj)
		i915_gem_object_put(obj);

	release_firmware(fw);		/* OK even if fw is NULL */
	uc_fw->fetch_status = INTEL_UC_FIRMWARE_FAIL;
}

/**
 * intel_guc_init() - define parameters and fetch firmware
 * @dev_priv:	i915 device private
 *
 * Called early during driver load, but after GEM is initialised.
 *
 * The firmware will be transferred to the GuC's memory later,
 * when intel_guc_init_hw() is called.
 */
void intel_guc_init(struct drm_i915_private *dev_priv)
{
	struct intel_uc_fw *guc_fw = &dev_priv->guc.fw;
	const char *fw_path;

	if (!HAS_GUC(dev_priv)) {
		i915.enable_guc_loading = 0;
		i915.enable_guc_submission = 0;
	} else {
		/* A negative value means "use platform default" */
		if (i915.enable_guc_loading < 0)
			i915.enable_guc_loading = HAS_GUC_UCODE(dev_priv);
		if (i915.enable_guc_submission < 0)
			i915.enable_guc_submission = HAS_GUC_SCHED(dev_priv);
	}

	if (!HAS_GUC_UCODE(dev_priv)) {
		fw_path = NULL;
	} else if (IS_SKYLAKE(dev_priv)) {
		fw_path = I915_SKL_GUC_UCODE;
		guc_fw->major_ver_wanted = SKL_FW_MAJOR;
		guc_fw->minor_ver_wanted = SKL_FW_MINOR;
	} else if (IS_BROXTON(dev_priv)) {
		fw_path = I915_BXT_GUC_UCODE;
		guc_fw->major_ver_wanted = BXT_FW_MAJOR;
		guc_fw->minor_ver_wanted = BXT_FW_MINOR;
	} else if (IS_KABYLAKE(dev_priv)) {
		fw_path = I915_KBL_GUC_UCODE;
		guc_fw->major_ver_wanted = KBL_FW_MAJOR;
		guc_fw->minor_ver_wanted = KBL_FW_MINOR;
	} else {
		fw_path = "";	/* unknown device */
	}

	guc_fw->path = fw_path;
	guc_fw->fetch_status = INTEL_UC_FIRMWARE_NONE;
	guc_fw->load_status = INTEL_UC_FIRMWARE_NONE;

	/* Early (and silent) return if GuC loading is disabled */
	if (!i915.enable_guc_loading)
		return;
	if (fw_path == NULL)
		return;
	if (*fw_path == '\0')
		return;

	guc_fw->fetch_status = INTEL_UC_FIRMWARE_PENDING;
	DRM_DEBUG_DRIVER("GuC firmware pending, path %s\n", fw_path);
	intel_uc_fw_fetch(dev_priv, guc_fw);
	/* status must now be FAIL or SUCCESS */
}

/**
 * intel_guc_fini() - clean up all allocated resources
 * @dev_priv:	i915 device private
 */
void intel_guc_fini(struct drm_i915_private *dev_priv)
{
	struct intel_uc_fw *guc_fw = &dev_priv->guc.fw;
	struct drm_i915_gem_object *obj;

	mutex_lock(&dev_priv->drm.struct_mutex);
	i915_guc_submission_disable(dev_priv);
	i915_guc_submission_fini(dev_priv);
	mutex_unlock(&dev_priv->drm.struct_mutex);

	obj = fetch_and_zero(&guc_fw->obj);
	if (obj)
		i915_gem_object_put(obj);

	guc_fw->fetch_status = INTEL_UC_FIRMWARE_NONE;
}

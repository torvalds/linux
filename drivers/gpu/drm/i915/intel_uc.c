/*
 * Copyright © 2016 Intel Corporation
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
 */

#include "intel_uc.h"
#include "i915_drv.h"
#include "i915_guc_submission.h"

/* Reset GuC providing us with fresh state for both GuC and HuC.
 */
static int __intel_uc_reset_hw(struct drm_i915_private *dev_priv)
{
	int ret;
	u32 guc_status;

	ret = intel_reset_guc(dev_priv);
	if (ret) {
		DRM_ERROR("Failed to reset GuC, ret = %d\n", ret);
		return ret;
	}

	guc_status = I915_READ(GUC_STATUS);
	WARN(!(guc_status & GS_MIA_IN_RESET),
	     "GuC status: 0x%x, MIA core expected to be in reset\n",
	     guc_status);

	return ret;
}

void intel_uc_sanitize_options(struct drm_i915_private *dev_priv)
{
	if (!HAS_GUC(dev_priv)) {
		if (i915_modparams.enable_guc_loading > 0 ||
		    i915_modparams.enable_guc_submission > 0)
			DRM_INFO("Ignoring GuC options, no hardware\n");

		i915_modparams.enable_guc_loading = 0;
		i915_modparams.enable_guc_submission = 0;
		return;
	}

	/* A negative value means "use platform default" */
	if (i915_modparams.enable_guc_loading < 0)
		i915_modparams.enable_guc_loading = HAS_GUC_UCODE(dev_priv);

	/* Verify firmware version */
	if (i915_modparams.enable_guc_loading) {
		if (HAS_HUC_UCODE(dev_priv))
			intel_huc_select_fw(&dev_priv->huc);

		if (intel_guc_fw_select(&dev_priv->guc))
			i915_modparams.enable_guc_loading = 0;
	}

	/* Can't enable guc submission without guc loaded */
	if (!i915_modparams.enable_guc_loading)
		i915_modparams.enable_guc_submission = 0;

	/* A negative value means "use platform default" */
	if (i915_modparams.enable_guc_submission < 0)
		i915_modparams.enable_guc_submission = HAS_GUC_SCHED(dev_priv);
}

void intel_uc_init_early(struct drm_i915_private *dev_priv)
{
	intel_guc_init_early(&dev_priv->guc);
}

void intel_uc_init_fw(struct drm_i915_private *dev_priv)
{
	intel_uc_fw_fetch(dev_priv, &dev_priv->huc.fw);
	intel_uc_fw_fetch(dev_priv, &dev_priv->guc.fw);
}

void intel_uc_fini_fw(struct drm_i915_private *dev_priv)
{
	intel_uc_fw_fini(&dev_priv->guc.fw);
	intel_uc_fw_fini(&dev_priv->huc.fw);
}

/**
 * intel_uc_init_mmio - setup uC MMIO access
 *
 * @dev_priv: device private
 *
 * Setup minimal state necessary for MMIO accesses later in the
 * initialization sequence.
 */
void intel_uc_init_mmio(struct drm_i915_private *dev_priv)
{
	intel_guc_init_send_regs(&dev_priv->guc);
}

static void guc_capture_load_err_log(struct intel_guc *guc)
{
	if (!guc->log.vma || i915_modparams.guc_log_level < 0)
		return;

	if (!guc->load_err_log)
		guc->load_err_log = i915_gem_object_get(guc->log.vma->obj);

	return;
}

static void guc_free_load_err_log(struct intel_guc *guc)
{
	if (guc->load_err_log)
		i915_gem_object_put(guc->load_err_log);
}

static int guc_enable_communication(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	if (HAS_GUC_CT(dev_priv))
		return intel_guc_enable_ct(guc);

	guc->send = intel_guc_send_mmio;
	return 0;
}

static void guc_disable_communication(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	if (HAS_GUC_CT(dev_priv))
		intel_guc_disable_ct(guc);

	guc->send = intel_guc_send_nop;
}

int intel_uc_init_hw(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	int ret, attempts;

	if (!i915_modparams.enable_guc_loading)
		return 0;

	guc_disable_communication(guc);
	gen9_reset_guc_interrupts(dev_priv);

	/* We need to notify the guc whenever we change the GGTT */
	i915_ggtt_enable_guc(dev_priv);

	if (i915_modparams.enable_guc_submission) {
		/*
		 * This is stuff we need to have available at fw load time
		 * if we are planning to enable submission later
		 */
		ret = intel_guc_submission_init(guc);
		if (ret)
			goto err_guc;
	}

	/* init WOPCM */
	I915_WRITE(GUC_WOPCM_SIZE, intel_guc_wopcm_size(dev_priv));
	I915_WRITE(DMA_GUC_WOPCM_OFFSET,
		   GUC_WOPCM_OFFSET_VALUE | HUC_LOADING_AGENT_GUC);

	/* WaEnableuKernelHeaderValidFix:skl */
	/* WaEnableGuCBootHashCheckNotSet:skl,bxt,kbl */
	if (IS_GEN9(dev_priv))
		attempts = 3;
	else
		attempts = 1;

	while (attempts--) {
		/*
		 * Always reset the GuC just before (re)loading, so
		 * that the state and timing are fairly predictable
		 */
		ret = __intel_uc_reset_hw(dev_priv);
		if (ret)
			goto err_submission;

		intel_huc_init_hw(&dev_priv->huc);
		intel_guc_init_params(guc);
		ret = intel_guc_fw_upload(guc);
		if (ret == 0 || ret != -EAGAIN)
			break;

		DRM_DEBUG_DRIVER("GuC fw load failed: %d; will reset and "
				 "retry %d more time(s)\n", ret, attempts);
	}

	/* Did we succeded or run out of retries? */
	if (ret)
		goto err_log_capture;

	ret = guc_enable_communication(guc);
	if (ret)
		goto err_log_capture;

	intel_huc_auth(&dev_priv->huc);
	if (i915_modparams.enable_guc_submission) {
		if (i915_modparams.guc_log_level >= 0)
			gen9_enable_guc_interrupts(dev_priv);

		ret = intel_guc_submission_enable(guc);
		if (ret)
			goto err_interrupts;
	}

	dev_info(dev_priv->drm.dev, "GuC %s (firmware %s [version %u.%u])\n",
		 i915_modparams.enable_guc_submission ? "submission enabled" :
							"loaded",
		 guc->fw.path,
		 guc->fw.major_ver_found, guc->fw.minor_ver_found);

	return 0;

	/*
	 * We've failed to load the firmware :(
	 *
	 * Decide whether to disable GuC submission and fall back to
	 * execlist mode, and whether to hide the error by returning
	 * zero or to return -EIO, which the caller will treat as a
	 * nonfatal error (i.e. it doesn't prevent driver load, but
	 * marks the GPU as wedged until reset).
	 */
err_interrupts:
	guc_disable_communication(guc);
	gen9_disable_guc_interrupts(dev_priv);
err_log_capture:
	guc_capture_load_err_log(guc);
err_submission:
	if (i915_modparams.enable_guc_submission)
		intel_guc_submission_fini(guc);
err_guc:
	i915_ggtt_disable_guc(dev_priv);

	if (i915_modparams.enable_guc_loading > 1 ||
	    i915_modparams.enable_guc_submission > 1) {
		DRM_ERROR("GuC init failed. Firmware loading disabled.\n");
		ret = -EIO;
	} else {
		DRM_NOTE("GuC init failed. Firmware loading disabled.\n");
		ret = 0;
	}

	if (i915_modparams.enable_guc_submission) {
		i915_modparams.enable_guc_submission = 0;
		DRM_NOTE("Falling back from GuC submission to execlist mode\n");
	}

	i915_modparams.enable_guc_loading = 0;

	return ret;
}

void intel_uc_fini_hw(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;

	guc_free_load_err_log(guc);

	if (!i915_modparams.enable_guc_loading)
		return;

	if (i915_modparams.enable_guc_submission)
		intel_guc_submission_disable(guc);

	guc_disable_communication(guc);

	if (i915_modparams.enable_guc_submission) {
		gen9_disable_guc_interrupts(dev_priv);
		intel_guc_submission_fini(guc);
	}

	i915_ggtt_disable_guc(dev_priv);
}

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
#include "intel_guc_submission.h"
#include "intel_guc.h"
#include "i915_drv.h"

static void guc_free_load_err_log(struct intel_guc *guc);

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

static int __get_platform_enable_guc(struct drm_i915_private *dev_priv)
{
	struct intel_uc_fw *guc_fw = &dev_priv->guc.fw;
	struct intel_uc_fw *huc_fw = &dev_priv->huc.fw;
	int enable_guc = 0;

	/* Default is to enable GuC/HuC if we know their firmwares */
	if (intel_uc_fw_is_selected(guc_fw))
		enable_guc |= ENABLE_GUC_SUBMISSION;
	if (intel_uc_fw_is_selected(huc_fw))
		enable_guc |= ENABLE_GUC_LOAD_HUC;

	/* Any platform specific fine-tuning can be done here */

	return enable_guc;
}

static int __get_default_guc_log_level(struct drm_i915_private *dev_priv)
{
	int guc_log_level = 0; /* disabled */

	/* Enable if we're running on platform with GuC and debug config */
	if (HAS_GUC(dev_priv) && intel_uc_is_using_guc() &&
	    (IS_ENABLED(CONFIG_DRM_I915_DEBUG) ||
	     IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)))
		guc_log_level = 1 + GUC_LOG_VERBOSITY_MAX;

	/* Any platform specific fine-tuning can be done here */

	return guc_log_level;
}

/**
 * intel_uc_sanitize_options - sanitize uC related modparam options
 * @dev_priv: device private
 *
 * In case of "enable_guc" option this function will attempt to modify
 * it only if it was initially set to "auto(-1)". Default value for this
 * modparam varies between platforms and it is hardcoded in driver code.
 * Any other modparam value is only monitored against availability of the
 * related hardware or firmware definitions.
 *
 * In case of "guc_log_level" option this function will attempt to modify
 * it only if it was initially set to "auto(-1)" or if initial value was
 * "enable(1..4)" on platforms without the GuC. Default value for this
 * modparam varies between platforms and is usually set to "disable(0)"
 * unless GuC is enabled on given platform and the driver is compiled with
 * debug config when this modparam will default to "enable(1..4)".
 */
void intel_uc_sanitize_options(struct drm_i915_private *dev_priv)
{
	struct intel_uc_fw *guc_fw = &dev_priv->guc.fw;
	struct intel_uc_fw *huc_fw = &dev_priv->huc.fw;

	/* A negative value means "use platform default" */
	if (i915_modparams.enable_guc < 0)
		i915_modparams.enable_guc = __get_platform_enable_guc(dev_priv);

	DRM_DEBUG_DRIVER("enable_guc=%d (submission:%s huc:%s)\n",
			 i915_modparams.enable_guc,
			 yesno(intel_uc_is_using_guc_submission()),
			 yesno(intel_uc_is_using_huc()));

	/* Verify GuC firmware availability */
	if (intel_uc_is_using_guc() && !intel_uc_fw_is_selected(guc_fw)) {
		DRM_WARN("Incompatible option detected: %s=%d, %s!\n",
			 "enable_guc", i915_modparams.enable_guc,
			 !HAS_GUC(dev_priv) ? "no GuC hardware" :
					      "no GuC firmware");
	}

	/* Verify HuC firmware availability */
	if (intel_uc_is_using_huc() && !intel_uc_fw_is_selected(huc_fw)) {
		DRM_WARN("Incompatible option detected: %s=%d, %s!\n",
			 "enable_guc", i915_modparams.enable_guc,
			 !HAS_HUC(dev_priv) ? "no HuC hardware" :
					      "no HuC firmware");
	}

	/* A negative value means "use platform/config default" */
	if (i915_modparams.guc_log_level < 0)
		i915_modparams.guc_log_level =
			__get_default_guc_log_level(dev_priv);

	if (i915_modparams.guc_log_level > 0 && !intel_uc_is_using_guc()) {
		DRM_WARN("Incompatible option detected: %s=%d, %s!\n",
			 "guc_log_level", i915_modparams.guc_log_level,
			 !HAS_GUC(dev_priv) ? "no GuC hardware" :
					      "GuC not enabled");
		i915_modparams.guc_log_level = 0;
	}

	if (i915_modparams.guc_log_level > 1 + GUC_LOG_VERBOSITY_MAX) {
		DRM_WARN("Incompatible option detected: %s=%d, %s!\n",
			 "guc_log_level", i915_modparams.guc_log_level,
			 "verbosity too high");
		i915_modparams.guc_log_level = 1 + GUC_LOG_VERBOSITY_MAX;
	}

	DRM_DEBUG_DRIVER("guc_log_level=%d (enabled:%s verbosity:%d)\n",
			 i915_modparams.guc_log_level,
			 yesno(i915_modparams.guc_log_level),
			 i915_modparams.guc_log_level - 1);

	/* Make sure that sanitization was done */
	GEM_BUG_ON(i915_modparams.enable_guc < 0);
	GEM_BUG_ON(i915_modparams.guc_log_level < 0);
}

void intel_uc_init_early(struct drm_i915_private *dev_priv)
{
	intel_guc_init_early(&dev_priv->guc);
	intel_huc_init_early(&dev_priv->huc);
}

void intel_uc_init_fw(struct drm_i915_private *dev_priv)
{
	if (!USES_GUC(dev_priv))
		return;

	if (USES_HUC(dev_priv))
		intel_uc_fw_fetch(dev_priv, &dev_priv->huc.fw);

	intel_uc_fw_fetch(dev_priv, &dev_priv->guc.fw);
}

void intel_uc_fini_fw(struct drm_i915_private *dev_priv)
{
	if (!USES_GUC(dev_priv))
		return;

	intel_uc_fw_fini(&dev_priv->guc.fw);

	if (USES_HUC(dev_priv))
		intel_uc_fw_fini(&dev_priv->huc.fw);

	guc_free_load_err_log(&dev_priv->guc);
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
	if (!guc->log.vma || !i915_modparams.guc_log_level)
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

int intel_uc_init_misc(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	int ret;

	if (!USES_GUC(dev_priv))
		return 0;

	ret = intel_guc_init_wq(guc);
	if (ret) {
		DRM_ERROR("Couldn't allocate workqueues for GuC\n");
		goto err;
	}

	ret = intel_guc_log_relay_create(guc);
	if (ret) {
		DRM_ERROR("Couldn't allocate relay for GuC log\n");
		goto err_relay;
	}

	return 0;

err_relay:
	intel_guc_fini_wq(guc);
err:
	return ret;
}

void intel_uc_fini_misc(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;

	if (!USES_GUC(dev_priv))
		return;

	intel_guc_fini_wq(guc);

	intel_guc_log_relay_destroy(guc);
}

int intel_uc_init(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	int ret;

	if (!USES_GUC(dev_priv))
		return 0;

	if (!HAS_GUC(dev_priv))
		return -ENODEV;

	ret = intel_guc_init(guc);
	if (ret)
		return ret;

	if (USES_GUC_SUBMISSION(dev_priv)) {
		/*
		 * This is stuff we need to have available at fw load time
		 * if we are planning to enable submission later
		 */
		ret = intel_guc_submission_init(guc);
		if (ret) {
			intel_guc_fini(guc);
			return ret;
		}
	}

	return 0;
}

void intel_uc_fini(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;

	if (!USES_GUC(dev_priv))
		return;

	GEM_BUG_ON(!HAS_GUC(dev_priv));

	if (USES_GUC_SUBMISSION(dev_priv))
		intel_guc_submission_fini(guc);

	intel_guc_fini(guc);
}

int intel_uc_init_hw(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	struct intel_huc *huc = &dev_priv->huc;
	int ret, attempts;

	if (!USES_GUC(dev_priv))
		return 0;

	GEM_BUG_ON(!HAS_GUC(dev_priv));

	guc_disable_communication(guc);
	gen9_reset_guc_interrupts(dev_priv);

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
			goto err_out;

		if (USES_HUC(dev_priv)) {
			ret = intel_huc_fw_upload(huc);
			if (ret)
				goto err_out;
		}

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

	if (USES_HUC(dev_priv)) {
		ret = intel_huc_auth(huc);
		if (ret)
			goto err_communication;
	}

	if (USES_GUC_SUBMISSION(dev_priv)) {
		if (i915_modparams.guc_log_level)
			gen9_enable_guc_interrupts(dev_priv);

		ret = intel_guc_submission_enable(guc);
		if (ret)
			goto err_interrupts;
	}

	dev_info(dev_priv->drm.dev, "GuC firmware version %u.%u\n",
		 guc->fw.major_ver_found, guc->fw.minor_ver_found);
	dev_info(dev_priv->drm.dev, "GuC submission %s\n",
		 enableddisabled(USES_GUC_SUBMISSION(dev_priv)));
	dev_info(dev_priv->drm.dev, "HuC %s\n",
		 enableddisabled(USES_HUC(dev_priv)));

	return 0;

	/*
	 * We've failed to load the firmware :(
	 */
err_interrupts:
	gen9_disable_guc_interrupts(dev_priv);
err_communication:
	guc_disable_communication(guc);
err_log_capture:
	guc_capture_load_err_log(guc);
err_out:
	/*
	 * Note that there is no fallback as either user explicitly asked for
	 * the GuC or driver default option was to run with the GuC enabled.
	 */
	if (GEM_WARN_ON(ret == -EIO))
		ret = -EINVAL;

	dev_err(dev_priv->drm.dev, "GuC initialization failed %d\n", ret);
	return ret;
}

void intel_uc_fini_hw(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;

	if (!USES_GUC(dev_priv))
		return;

	GEM_BUG_ON(!HAS_GUC(dev_priv));

	if (USES_GUC_SUBMISSION(dev_priv))
		intel_guc_submission_disable(guc);

	guc_disable_communication(guc);

	if (USES_GUC_SUBMISSION(dev_priv))
		gen9_disable_guc_interrupts(dev_priv);
}

int intel_uc_suspend(struct drm_i915_private *i915)
{
	struct intel_guc *guc = &i915->guc;
	int err;

	if (!USES_GUC(i915))
		return 0;

	if (guc->fw.load_status != INTEL_UC_FIRMWARE_SUCCESS)
		return 0;

	err = intel_guc_suspend(guc);
	if (err) {
		DRM_DEBUG_DRIVER("Failed to suspend GuC, err=%d", err);
		return err;
	}

	gen9_disable_guc_interrupts(i915);

	return 0;
}

int intel_uc_resume(struct drm_i915_private *i915)
{
	struct intel_guc *guc = &i915->guc;
	int err;

	if (!USES_GUC(i915))
		return 0;

	if (guc->fw.load_status != INTEL_UC_FIRMWARE_SUCCESS)
		return 0;

	if (i915_modparams.guc_log_level)
		gen9_enable_guc_interrupts(i915);

	err = intel_guc_resume(guc);
	if (err) {
		DRM_DEBUG_DRIVER("Failed to resume GuC, err=%d", err);
		return err;
	}

	return 0;
}

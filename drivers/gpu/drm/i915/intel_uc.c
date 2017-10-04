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

#include "i915_drv.h"
#include "intel_uc.h"
#include <linux/firmware.h>

/* Reset GuC providing us with fresh state for both GuC and HuC.
 */
static int __intel_uc_reset_hw(struct drm_i915_private *dev_priv)
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

		if (intel_guc_select_fw(&dev_priv->guc))
			i915_modparams.enable_guc_loading = 0;
	}

	/* Can't enable guc submission without guc loaded */
	if (!i915_modparams.enable_guc_loading)
		i915_modparams.enable_guc_submission = 0;

	/* A negative value means "use platform default" */
	if (i915_modparams.enable_guc_submission < 0)
		i915_modparams.enable_guc_submission = HAS_GUC_SCHED(dev_priv);
}

static void gen8_guc_raise_irq(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	I915_WRITE(GUC_SEND_INTERRUPT, GUC_SEND_TRIGGER);
}

static void guc_init_early(struct intel_guc *guc)
{
	intel_guc_ct_init_early(&guc->ct);

	mutex_init(&guc->send_mutex);
	guc->send = intel_guc_send_nop;
	guc->notify = gen8_guc_raise_irq;
}

void intel_uc_init_early(struct drm_i915_private *dev_priv)
{
	guc_init_early(&dev_priv->guc);
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

static inline i915_reg_t guc_send_reg(struct intel_guc *guc, u32 i)
{
	GEM_BUG_ON(!guc->send_regs.base);
	GEM_BUG_ON(!guc->send_regs.count);
	GEM_BUG_ON(i >= guc->send_regs.count);

	return _MMIO(guc->send_regs.base + 4 * i);
}

static void guc_init_send_regs(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	enum forcewake_domains fw_domains = 0;
	unsigned int i;

	guc->send_regs.base = i915_mmio_reg_offset(SOFT_SCRATCH(0));
	guc->send_regs.count = SOFT_SCRATCH_COUNT - 1;

	for (i = 0; i < guc->send_regs.count; i++) {
		fw_domains |= intel_uncore_forcewake_for_reg(dev_priv,
					guc_send_reg(guc, i),
					FW_REG_READ | FW_REG_WRITE);
	}
	guc->send_regs.fw_domains = fw_domains;
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
	guc_init_send_regs(&dev_priv->guc);
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

/**
 * intel_guc_auth_huc() - Send action to GuC to authenticate HuC ucode
 * @guc: intel_guc structure
 * @rsa_offset: rsa offset w.r.t ggtt base of huc vma
 *
 * Triggers a HuC firmware authentication request to the GuC via intel_guc_send
 * INTEL_GUC_ACTION_AUTHENTICATE_HUC interface. This function is invoked by
 * intel_huc_auth().
 *
 * Return:	non-zero code on error
 */
int intel_guc_auth_huc(struct intel_guc *guc, u32 rsa_offset)
{
	u32 action[] = {
		INTEL_GUC_ACTION_AUTHENTICATE_HUC,
		rsa_offset
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
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
		ret = i915_guc_submission_init(dev_priv);
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
		ret = intel_guc_init_hw(&dev_priv->guc);
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

		ret = i915_guc_submission_enable(dev_priv);
		if (ret)
			goto err_interrupts;
	}

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
		i915_guc_submission_fini(dev_priv);
err_guc:
	i915_ggtt_disable_guc(dev_priv);

	DRM_ERROR("GuC init failed\n");
	if (i915_modparams.enable_guc_loading > 1 ||
	    i915_modparams.enable_guc_submission > 1)
		ret = -EIO;
	else
		ret = 0;

	if (i915_modparams.enable_guc_submission) {
		i915_modparams.enable_guc_submission = 0;
		DRM_NOTE("Falling back from GuC submission to execlist mode\n");
	}

	i915_modparams.enable_guc_loading = 0;
	DRM_NOTE("GuC firmware loading disabled\n");

	return ret;
}

void intel_uc_fini_hw(struct drm_i915_private *dev_priv)
{
	guc_free_load_err_log(&dev_priv->guc);

	if (!i915_modparams.enable_guc_loading)
		return;

	if (i915_modparams.enable_guc_submission)
		i915_guc_submission_disable(dev_priv);

	guc_disable_communication(&dev_priv->guc);

	if (i915_modparams.enable_guc_submission) {
		gen9_disable_guc_interrupts(dev_priv);
		i915_guc_submission_fini(dev_priv);
	}

	i915_ggtt_disable_guc(dev_priv);
}

int intel_guc_send_nop(struct intel_guc *guc, const u32 *action, u32 len)
{
	WARN(1, "Unexpected send: action=%#x\n", *action);
	return -ENODEV;
}

/*
 * This function implements the MMIO based host to GuC interface.
 */
int intel_guc_send_mmio(struct intel_guc *guc, const u32 *action, u32 len)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 status;
	int i;
	int ret;

	GEM_BUG_ON(!len);
	GEM_BUG_ON(len > guc->send_regs.count);

	/* If CT is available, we expect to use MMIO only during init/fini */
	GEM_BUG_ON(HAS_GUC_CT(dev_priv) &&
		*action != INTEL_GUC_ACTION_REGISTER_COMMAND_TRANSPORT_BUFFER &&
		*action != INTEL_GUC_ACTION_DEREGISTER_COMMAND_TRANSPORT_BUFFER);

	mutex_lock(&guc->send_mutex);
	intel_uncore_forcewake_get(dev_priv, guc->send_regs.fw_domains);

	for (i = 0; i < len; i++)
		I915_WRITE(guc_send_reg(guc, i), action[i]);

	POSTING_READ(guc_send_reg(guc, i - 1));

	intel_guc_notify(guc);

	/*
	 * No GuC command should ever take longer than 10ms.
	 * Fast commands should still complete in 10us.
	 */
	ret = __intel_wait_for_register_fw(dev_priv,
					   guc_send_reg(guc, 0),
					   INTEL_GUC_RECV_MASK,
					   INTEL_GUC_RECV_MASK,
					   10, 10, &status);
	if (status != INTEL_GUC_STATUS_SUCCESS) {
		/*
		 * Either the GuC explicitly returned an error (which
		 * we convert to -EIO here) or no response at all was
		 * received within the timeout limit (-ETIMEDOUT)
		 */
		if (ret != -ETIMEDOUT)
			ret = -EIO;

		DRM_WARN("INTEL_GUC_SEND: Action 0x%X failed;"
			 " ret=%d status=0x%08X response=0x%08X\n",
			 action[0], ret, status, I915_READ(SOFT_SCRATCH(15)));
	}

	intel_uncore_forcewake_put(dev_priv, guc->send_regs.fw_domains);
	mutex_unlock(&guc->send_mutex);

	return ret;
}

int intel_guc_sample_forcewake(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 action[2];

	action[0] = INTEL_GUC_ACTION_SAMPLE_FORCEWAKE;
	/* WaRsDisableCoarsePowerGating:skl,bxt */
	if (!intel_enable_rc6() || NEEDS_WaRsDisableCoarsePowerGating(dev_priv))
		action[1] = 0;
	else
		/* bit 0 and 1 are for Render and Media domain separately */
		action[1] = GUC_FORCEWAKE_RENDER | GUC_FORCEWAKE_MEDIA;

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

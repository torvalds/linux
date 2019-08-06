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

#include "gt/intel_gt.h"
#include "gt/intel_reset.h"
#include "intel_guc.h"
#include "intel_guc_ads.h"
#include "intel_guc_submission.h"
#include "intel_uc.h"

#include "i915_drv.h"

static void guc_free_load_err_log(struct intel_guc *guc);

/* Reset GuC providing us with fresh state for both GuC and HuC.
 */
static int __intel_uc_reset_hw(struct intel_uc *uc)
{
	struct intel_gt *gt = uc_to_gt(uc);
	int ret;
	u32 guc_status;

	ret = intel_reset_guc(gt);
	if (ret) {
		DRM_ERROR("Failed to reset GuC, ret = %d\n", ret);
		return ret;
	}

	guc_status = intel_uncore_read(gt->uncore, GUC_STATUS);
	WARN(!(guc_status & GS_MIA_IN_RESET),
	     "GuC status: 0x%x, MIA core expected to be in reset\n",
	     guc_status);

	return ret;
}

static int __get_platform_enable_guc(struct intel_uc *uc)
{
	struct intel_uc_fw *guc_fw = &uc->guc.fw;
	struct intel_uc_fw *huc_fw = &uc->huc.fw;
	int enable_guc = 0;

	if (!HAS_GT_UC(uc_to_gt(uc)->i915))
		return 0;

	/* We don't want to enable GuC/HuC on pre-Gen11 by default */
	if (INTEL_GEN(uc_to_gt(uc)->i915) < 11)
		return 0;

	if (intel_uc_fw_supported(guc_fw) && intel_uc_fw_supported(huc_fw))
		enable_guc |= ENABLE_GUC_LOAD_HUC;

	return enable_guc;
}

/**
 * sanitize_options_early - sanitize uC related modparam options
 * @uc: the intel_uc structure
 *
 * In case of "enable_guc" option this function will attempt to modify
 * it only if it was initially set to "auto(-1)". Default value for this
 * modparam varies between platforms and it is hardcoded in driver code.
 * Any other modparam value is only monitored against availability of the
 * related hardware or firmware definitions.
 */
static void sanitize_options_early(struct intel_uc *uc)
{
	struct intel_uc_fw *guc_fw = &uc->guc.fw;
	struct intel_uc_fw *huc_fw = &uc->huc.fw;

	/* A negative value means "use platform default" */
	if (i915_modparams.enable_guc < 0)
		i915_modparams.enable_guc = __get_platform_enable_guc(uc);

	DRM_DEBUG_DRIVER("enable_guc=%d (submission:%s huc:%s)\n",
			 i915_modparams.enable_guc,
			 yesno(intel_uc_is_using_guc_submission(uc)),
			 yesno(intel_uc_is_using_huc(uc)));

	/* Verify GuC firmware availability */
	if (intel_uc_is_using_guc(uc) && !intel_uc_fw_supported(guc_fw)) {
		DRM_WARN("Incompatible option detected: enable_guc=%d, "
			 "but GuC is not supported!\n",
			 i915_modparams.enable_guc);
		DRM_INFO("Disabling GuC/HuC loading!\n");
		i915_modparams.enable_guc = 0;
	}

	/* Verify HuC firmware availability */
	if (intel_uc_is_using_huc(uc) && !intel_uc_fw_supported(huc_fw)) {
		DRM_WARN("Incompatible option detected: enable_guc=%d, "
			 "but HuC is not supported!\n",
			 i915_modparams.enable_guc);
		DRM_INFO("Disabling HuC loading!\n");
		i915_modparams.enable_guc &= ~ENABLE_GUC_LOAD_HUC;
	}

	/* XXX: GuC submission is unavailable for now */
	if (intel_uc_is_using_guc_submission(uc)) {
		DRM_INFO("Incompatible option detected: enable_guc=%d, "
			 "but GuC submission is not supported!\n",
			 i915_modparams.enable_guc);
		DRM_INFO("Switching to non-GuC submission mode!\n");
		i915_modparams.enable_guc &= ~ENABLE_GUC_SUBMISSION;
	}

	/* Make sure that sanitization was done */
	GEM_BUG_ON(i915_modparams.enable_guc < 0);
}

void intel_uc_init_early(struct intel_uc *uc)
{
	intel_guc_init_early(&uc->guc);
	intel_huc_init_early(&uc->huc);

	sanitize_options_early(uc);
}

void intel_uc_cleanup_early(struct intel_uc *uc)
{
	guc_free_load_err_log(&uc->guc);
}

/**
 * intel_uc_init_mmio - setup uC MMIO access
 * @uc: the intel_uc structure
 *
 * Setup minimal state necessary for MMIO accesses later in the
 * initialization sequence.
 */
void intel_uc_init_mmio(struct intel_uc *uc)
{
	intel_guc_init_send_regs(&uc->guc);
}

static void guc_capture_load_err_log(struct intel_guc *guc)
{
	if (!guc->log.vma || !intel_guc_log_get_level(&guc->log))
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

/*
 * Events triggered while CT buffers are disabled are logged in the SCRATCH_15
 * register using the same bits used in the CT message payload. Since our
 * communication channel with guc is turned off at this point, we can save the
 * message and handle it after we turn it back on.
 */
static void guc_clear_mmio_msg(struct intel_guc *guc)
{
	intel_uncore_write(guc_to_gt(guc)->uncore, SOFT_SCRATCH(15), 0);
}

static void guc_get_mmio_msg(struct intel_guc *guc)
{
	u32 val;

	spin_lock_irq(&guc->irq_lock);

	val = intel_uncore_read(guc_to_gt(guc)->uncore, SOFT_SCRATCH(15));
	guc->mmio_msg |= val & guc->msg_enabled_mask;

	/*
	 * clear all events, including the ones we're not currently servicing,
	 * to make sure we don't try to process a stale message if we enable
	 * handling of more events later.
	 */
	guc_clear_mmio_msg(guc);

	spin_unlock_irq(&guc->irq_lock);
}

static void guc_handle_mmio_msg(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	/* we need communication to be enabled to reply to GuC */
	GEM_BUG_ON(guc->handler == intel_guc_to_host_event_handler_nop);

	if (!guc->mmio_msg)
		return;

	spin_lock_irq(&i915->irq_lock);
	intel_guc_to_host_process_recv_msg(guc, &guc->mmio_msg, 1);
	spin_unlock_irq(&i915->irq_lock);

	guc->mmio_msg = 0;
}

static void guc_reset_interrupts(struct intel_guc *guc)
{
	guc->interrupts.reset(guc);
}

static void guc_enable_interrupts(struct intel_guc *guc)
{
	guc->interrupts.enable(guc);
}

static void guc_disable_interrupts(struct intel_guc *guc)
{
	guc->interrupts.disable(guc);
}

static int guc_enable_communication(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	int ret;

	ret = intel_guc_ct_enable(&guc->ct);
	if (ret)
		return ret;

	guc->send = intel_guc_send_ct;
	guc->handler = intel_guc_to_host_event_handler_ct;

	/* check for mmio messages received before/during the CT enable */
	guc_get_mmio_msg(guc);
	guc_handle_mmio_msg(guc);

	guc_enable_interrupts(guc);

	/* check for CT messages received before we enabled interrupts */
	spin_lock_irq(&i915->irq_lock);
	intel_guc_to_host_event_handler_ct(guc);
	spin_unlock_irq(&i915->irq_lock);

	DRM_INFO("GuC communication enabled\n");

	return 0;
}

static void guc_stop_communication(struct intel_guc *guc)
{
	intel_guc_ct_stop(&guc->ct);

	guc->send = intel_guc_send_nop;
	guc->handler = intel_guc_to_host_event_handler_nop;

	guc_clear_mmio_msg(guc);
}

static void guc_disable_communication(struct intel_guc *guc)
{
	/*
	 * Events generated during or after CT disable are logged by guc in
	 * via mmio. Make sure the register is clear before disabling CT since
	 * all events we cared about have already been processed via CT.
	 */
	guc_clear_mmio_msg(guc);

	guc_disable_interrupts(guc);

	guc->send = intel_guc_send_nop;
	guc->handler = intel_guc_to_host_event_handler_nop;

	intel_guc_ct_disable(&guc->ct);

	/*
	 * Check for messages received during/after the CT disable. We do not
	 * expect any messages to have arrived via CT between the interrupt
	 * disable and the CT disable because GuC should've been idle until we
	 * triggered the CT disable protocol.
	 */
	guc_get_mmio_msg(guc);

	DRM_INFO("GuC communication disabled\n");
}

void intel_uc_fetch_firmwares(struct intel_uc *uc)
{
	struct drm_i915_private *i915 = uc_to_gt(uc)->i915;

	if (!intel_uc_is_using_guc(uc))
		return;

	intel_uc_fw_fetch(&uc->guc.fw, i915);

	if (intel_uc_is_using_huc(uc))
		intel_uc_fw_fetch(&uc->huc.fw, i915);
}

void intel_uc_cleanup_firmwares(struct intel_uc *uc)
{
	if (!intel_uc_is_using_guc(uc))
		return;

	if (intel_uc_is_using_huc(uc))
		intel_uc_fw_cleanup_fetch(&uc->huc.fw);

	intel_uc_fw_cleanup_fetch(&uc->guc.fw);
}

int intel_uc_init(struct intel_uc *uc)
{
	struct intel_guc *guc = &uc->guc;
	struct intel_huc *huc = &uc->huc;
	int ret;

	if (!intel_uc_is_using_guc(uc))
		return 0;

	if (!intel_uc_fw_supported(&guc->fw))
		return -ENODEV;

	/* XXX: GuC submission is unavailable for now */
	GEM_BUG_ON(intel_uc_is_using_guc_submission(uc));

	ret = intel_guc_init(guc);
	if (ret)
		return ret;

	if (intel_uc_is_using_huc(uc)) {
		ret = intel_huc_init(huc);
		if (ret)
			goto err_guc;
	}

	return 0;

err_guc:
	intel_guc_fini(guc);
	return ret;
}

void intel_uc_fini(struct intel_uc *uc)
{
	struct intel_guc *guc = &uc->guc;

	if (!intel_uc_is_using_guc(uc))
		return;

	GEM_BUG_ON(!intel_uc_fw_supported(&guc->fw));

	if (intel_uc_is_using_huc(uc))
		intel_huc_fini(&uc->huc);

	intel_guc_fini(guc);
}

static void __uc_sanitize(struct intel_uc *uc)
{
	struct intel_guc *guc = &uc->guc;
	struct intel_huc *huc = &uc->huc;

	GEM_BUG_ON(!intel_uc_fw_supported(&guc->fw));

	intel_huc_sanitize(huc);
	intel_guc_sanitize(guc);

	__intel_uc_reset_hw(uc);
}

void intel_uc_sanitize(struct intel_uc *uc)
{
	if (!intel_uc_is_using_guc(uc))
		return;

	__uc_sanitize(uc);
}

int intel_uc_init_hw(struct intel_uc *uc)
{
	struct drm_i915_private *i915 = uc_to_gt(uc)->i915;
	struct intel_guc *guc = &uc->guc;
	struct intel_huc *huc = &uc->huc;
	int ret, attempts;

	if (!intel_uc_is_using_guc(uc))
		return 0;

	GEM_BUG_ON(!intel_uc_fw_supported(&guc->fw));

	guc_reset_interrupts(guc);

	/* WaEnableuKernelHeaderValidFix:skl */
	/* WaEnableGuCBootHashCheckNotSet:skl,bxt,kbl */
	if (IS_GEN(i915, 9))
		attempts = 3;
	else
		attempts = 1;

	while (attempts--) {
		/*
		 * Always reset the GuC just before (re)loading, so
		 * that the state and timing are fairly predictable
		 */
		ret = __intel_uc_reset_hw(uc);
		if (ret)
			goto err_out;

		if (intel_uc_is_using_huc(uc)) {
			ret = intel_huc_fw_upload(huc);
			if (ret && intel_uc_fw_is_overridden(&huc->fw))
				goto err_out;
		}

		intel_guc_ads_reset(guc);
		intel_guc_write_params(guc);
		ret = intel_guc_fw_upload(guc);
		if (ret == 0)
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

	if (intel_uc_fw_is_loaded(&huc->fw)) {
		ret = intel_huc_auth(huc);
		if (ret && intel_uc_fw_is_overridden(&huc->fw))
			goto err_communication;
	}

	ret = intel_guc_sample_forcewake(guc);
	if (ret)
		goto err_communication;

	if (intel_uc_is_using_guc_submission(uc)) {
		ret = intel_guc_submission_enable(guc);
		if (ret)
			goto err_communication;
	}

	dev_info(i915->drm.dev, "GuC firmware version %u.%u\n",
		 guc->fw.major_ver_found, guc->fw.minor_ver_found);
	dev_info(i915->drm.dev, "GuC submission %s\n",
		 enableddisabled(intel_uc_is_using_guc_submission(uc)));
	dev_info(i915->drm.dev, "HuC %s\n",
		 enableddisabled(intel_huc_is_authenticated(huc)));

	return 0;

	/*
	 * We've failed to load the firmware :(
	 */
err_communication:
	guc_disable_communication(guc);
err_log_capture:
	guc_capture_load_err_log(guc);
err_out:
	__uc_sanitize(uc);

	/*
	 * Note that there is no fallback as either user explicitly asked for
	 * the GuC or driver default option was to run with the GuC enabled.
	 */
	if (GEM_WARN_ON(ret == -EIO))
		ret = -EINVAL;

	dev_err(i915->drm.dev, "GuC initialization failed %d\n", ret);
	return ret;
}

void intel_uc_fini_hw(struct intel_uc *uc)
{
	struct intel_guc *guc = &uc->guc;

	if (!intel_guc_is_running(guc))
		return;

	GEM_BUG_ON(!intel_uc_fw_supported(&guc->fw));

	if (intel_uc_is_using_guc_submission(uc))
		intel_guc_submission_disable(guc);

	guc_disable_communication(guc);
	__uc_sanitize(uc);
}

/**
 * intel_uc_reset_prepare - Prepare for reset
 * @uc: the intel_uc structure
 *
 * Preparing for full gpu reset.
 */
void intel_uc_reset_prepare(struct intel_uc *uc)
{
	struct intel_guc *guc = &uc->guc;

	if (!intel_guc_is_running(guc))
		return;

	guc_stop_communication(guc);
	__uc_sanitize(uc);
}

void intel_uc_runtime_suspend(struct intel_uc *uc)
{
	struct intel_guc *guc = &uc->guc;
	int err;

	if (!intel_guc_is_running(guc))
		return;

	err = intel_guc_suspend(guc);
	if (err)
		DRM_DEBUG_DRIVER("Failed to suspend GuC, err=%d", err);

	guc_disable_communication(guc);
}

void intel_uc_suspend(struct intel_uc *uc)
{
	struct intel_guc *guc = &uc->guc;
	intel_wakeref_t wakeref;

	if (!intel_guc_is_running(guc))
		return;

	with_intel_runtime_pm(&uc_to_gt(uc)->i915->runtime_pm, wakeref)
		intel_uc_runtime_suspend(uc);
}

int intel_uc_resume(struct intel_uc *uc)
{
	struct intel_guc *guc = &uc->guc;
	int err;

	if (!intel_guc_is_running(guc))
		return 0;

	guc_enable_communication(guc);

	err = intel_guc_resume(guc);
	if (err) {
		DRM_DEBUG_DRIVER("Failed to resume GuC, err=%d", err);
		return err;
	}

	return 0;
}

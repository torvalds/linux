// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#include "gem/i915_gem_lmem.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_irq.h"
#include "gt/intel_gt_pm_irq.h"
#include "gt/intel_gt_regs.h"
#include "intel_guc.h"
#include "intel_guc_ads.h"
#include "intel_guc_capture.h"
#include "intel_guc_slpc.h"
#include "intel_guc_submission.h"
#include "i915_drv.h"
#include "i915_irq.h"

/**
 * DOC: GuC
 *
 * The GuC is a microcontroller inside the GT HW, introduced in gen9. The GuC is
 * designed to offload some of the functionality usually performed by the host
 * driver; currently the main operations it can take care of are:
 *
 * - Authentication of the HuC, which is required to fully enable HuC usage.
 * - Low latency graphics context scheduling (a.k.a. GuC submission).
 * - GT Power management.
 *
 * The enable_guc module parameter can be used to select which of those
 * operations to enable within GuC. Note that not all the operations are
 * supported on all gen9+ platforms.
 *
 * Enabling the GuC is not mandatory and therefore the firmware is only loaded
 * if at least one of the operations is selected. However, not loading the GuC
 * might result in the loss of some features that do require the GuC (currently
 * just the HuC, but more are expected to land in the future).
 */

void intel_guc_notify(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	/*
	 * On Gen11+, the value written to the register is passes as a payload
	 * to the FW. However, the FW currently treats all values the same way
	 * (H2G interrupt), so we can just write the value that the HW expects
	 * on older gens.
	 */
	intel_uncore_write(gt->uncore, guc->notify_reg, GUC_SEND_TRIGGER);
}

static inline i915_reg_t guc_send_reg(struct intel_guc *guc, u32 i)
{
	GEM_BUG_ON(!guc->send_regs.base);
	GEM_BUG_ON(!guc->send_regs.count);
	GEM_BUG_ON(i >= guc->send_regs.count);

	return _MMIO(guc->send_regs.base + 4 * i);
}

void intel_guc_init_send_regs(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	enum forcewake_domains fw_domains = 0;
	unsigned int i;

	GEM_BUG_ON(!guc->send_regs.base);
	GEM_BUG_ON(!guc->send_regs.count);

	for (i = 0; i < guc->send_regs.count; i++) {
		fw_domains |= intel_uncore_forcewake_for_reg(gt->uncore,
					guc_send_reg(guc, i),
					FW_REG_READ | FW_REG_WRITE);
	}
	guc->send_regs.fw_domains = fw_domains;
}

static void gen9_reset_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	assert_rpm_wakelock_held(&gt->i915->runtime_pm);

	spin_lock_irq(gt->irq_lock);
	gen6_gt_pm_reset_iir(gt, gt->pm_guc_events);
	spin_unlock_irq(gt->irq_lock);
}

static void gen9_enable_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	assert_rpm_wakelock_held(&gt->i915->runtime_pm);

	spin_lock_irq(gt->irq_lock);
	WARN_ON_ONCE(intel_uncore_read(gt->uncore, GEN8_GT_IIR(2)) &
		     gt->pm_guc_events);
	gen6_gt_pm_enable_irq(gt, gt->pm_guc_events);
	spin_unlock_irq(gt->irq_lock);
}

static void gen9_disable_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	assert_rpm_wakelock_held(&gt->i915->runtime_pm);

	spin_lock_irq(gt->irq_lock);

	gen6_gt_pm_disable_irq(gt, gt->pm_guc_events);

	spin_unlock_irq(gt->irq_lock);
	intel_synchronize_irq(gt->i915);

	gen9_reset_guc_interrupts(guc);
}

static void gen11_reset_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	spin_lock_irq(gt->irq_lock);
	gen11_gt_reset_one_iir(gt, 0, GEN11_GUC);
	spin_unlock_irq(gt->irq_lock);
}

static void gen11_enable_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	u32 events = REG_FIELD_PREP(ENGINE1_MASK, GUC_INTR_GUC2HOST);

	spin_lock_irq(gt->irq_lock);
	WARN_ON_ONCE(gen11_gt_reset_one_iir(gt, 0, GEN11_GUC));
	intel_uncore_write(gt->uncore,
			   GEN11_GUC_SG_INTR_ENABLE, events);
	intel_uncore_write(gt->uncore,
			   GEN11_GUC_SG_INTR_MASK, ~events);
	spin_unlock_irq(gt->irq_lock);
}

static void gen11_disable_guc_interrupts(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	spin_lock_irq(gt->irq_lock);

	intel_uncore_write(gt->uncore, GEN11_GUC_SG_INTR_MASK, ~0);
	intel_uncore_write(gt->uncore, GEN11_GUC_SG_INTR_ENABLE, 0);

	spin_unlock_irq(gt->irq_lock);
	intel_synchronize_irq(gt->i915);

	gen11_reset_guc_interrupts(guc);
}

void intel_guc_init_early(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	intel_uc_fw_init_early(&guc->fw, INTEL_UC_FW_TYPE_GUC);
	intel_guc_ct_init_early(&guc->ct);
	intel_guc_log_init_early(&guc->log);
	intel_guc_submission_init_early(guc);
	intel_guc_slpc_init_early(&guc->slpc);
	intel_guc_rc_init_early(guc);

	mutex_init(&guc->send_mutex);
	spin_lock_init(&guc->irq_lock);
	if (GRAPHICS_VER(i915) >= 11) {
		guc->notify_reg = GEN11_GUC_HOST_INTERRUPT;
		guc->interrupts.reset = gen11_reset_guc_interrupts;
		guc->interrupts.enable = gen11_enable_guc_interrupts;
		guc->interrupts.disable = gen11_disable_guc_interrupts;
		guc->send_regs.base =
			i915_mmio_reg_offset(GEN11_SOFT_SCRATCH(0));
		guc->send_regs.count = GEN11_SOFT_SCRATCH_COUNT;

	} else {
		guc->notify_reg = GUC_SEND_INTERRUPT;
		guc->interrupts.reset = gen9_reset_guc_interrupts;
		guc->interrupts.enable = gen9_enable_guc_interrupts;
		guc->interrupts.disable = gen9_disable_guc_interrupts;
		guc->send_regs.base = i915_mmio_reg_offset(SOFT_SCRATCH(0));
		guc->send_regs.count = GUC_MAX_MMIO_MSG_LEN;
		BUILD_BUG_ON(GUC_MAX_MMIO_MSG_LEN > SOFT_SCRATCH_COUNT);
	}

	intel_guc_enable_msg(guc, INTEL_GUC_RECV_MSG_EXCEPTION |
				  INTEL_GUC_RECV_MSG_CRASH_DUMP_POSTED);
}

void intel_guc_init_late(struct intel_guc *guc)
{
	intel_guc_ads_init_late(guc);
}

static u32 guc_ctl_debug_flags(struct intel_guc *guc)
{
	u32 level = intel_guc_log_get_level(&guc->log);
	u32 flags = 0;

	if (!GUC_LOG_LEVEL_IS_VERBOSE(level))
		flags |= GUC_LOG_DISABLED;
	else
		flags |= GUC_LOG_LEVEL_TO_VERBOSITY(level) <<
			 GUC_LOG_VERBOSITY_SHIFT;

	return flags;
}

static u32 guc_ctl_feature_flags(struct intel_guc *guc)
{
	u32 flags = 0;

	if (!intel_guc_submission_is_used(guc))
		flags |= GUC_CTL_DISABLE_SCHEDULER;

	if (intel_guc_slpc_is_used(guc))
		flags |= GUC_CTL_ENABLE_SLPC;

	return flags;
}

static u32 guc_ctl_log_params_flags(struct intel_guc *guc)
{
	struct intel_guc_log *log = &guc->log;
	u32 offset, flags;

	GEM_BUG_ON(!log->sizes_initialised);

	offset = intel_guc_ggtt_offset(guc, log->vma) >> PAGE_SHIFT;

	flags = GUC_LOG_VALID |
		GUC_LOG_NOTIFY_ON_HALF_FULL |
		log->sizes[GUC_LOG_SECTIONS_DEBUG].flag |
		log->sizes[GUC_LOG_SECTIONS_CAPTURE].flag |
		(log->sizes[GUC_LOG_SECTIONS_CRASH].count << GUC_LOG_CRASH_SHIFT) |
		(log->sizes[GUC_LOG_SECTIONS_DEBUG].count << GUC_LOG_DEBUG_SHIFT) |
		(log->sizes[GUC_LOG_SECTIONS_CAPTURE].count << GUC_LOG_CAPTURE_SHIFT) |
		(offset << GUC_LOG_BUF_ADDR_SHIFT);

	return flags;
}

static u32 guc_ctl_ads_flags(struct intel_guc *guc)
{
	u32 ads = intel_guc_ggtt_offset(guc, guc->ads_vma) >> PAGE_SHIFT;
	u32 flags = ads << GUC_ADS_ADDR_SHIFT;

	return flags;
}

static u32 guc_ctl_wa_flags(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	u32 flags = 0;

	/* Wa_22012773006:gen11,gen12 < XeHP */
	if (GRAPHICS_VER(gt->i915) >= 11 &&
	    GRAPHICS_VER_FULL(gt->i915) < IP_VER(12, 50))
		flags |= GUC_WA_POLLCS;

	/* Wa_16011759253:dg2_g10:a0 */
	if (IS_DG2_GRAPHICS_STEP(gt->i915, G10, STEP_A0, STEP_B0))
		flags |= GUC_WA_GAM_CREDITS;

	/* Wa_14014475959:dg2 */
	if (IS_DG2(gt->i915))
		flags |= GUC_WA_HOLD_CCS_SWITCHOUT;

	/*
	 * Wa_14012197797:dg2_g10:a0,dg2_g11:a0
	 * Wa_22011391025:dg2_g10,dg2_g11,dg2_g12
	 *
	 * The same WA bit is used for both and 22011391025 is applicable to
	 * all DG2.
	 */
	if (IS_DG2(gt->i915))
		flags |= GUC_WA_DUAL_QUEUE;

	/* Wa_22011802037: graphics version 11/12 */
	if (IS_GRAPHICS_VER(gt->i915, 11, 12))
		flags |= GUC_WA_PRE_PARSER;

	/* Wa_16011777198:dg2 */
	if (IS_DG2_GRAPHICS_STEP(gt->i915, G10, STEP_A0, STEP_C0) ||
	    IS_DG2_GRAPHICS_STEP(gt->i915, G11, STEP_A0, STEP_B0))
		flags |= GUC_WA_RCS_RESET_BEFORE_RC6;

	/*
	 * Wa_22012727170:dg2_g10[a0-c0), dg2_g11[a0..)
	 * Wa_22012727685:dg2_g11[a0..)
	 */
	if (IS_DG2_GRAPHICS_STEP(gt->i915, G10, STEP_A0, STEP_C0) ||
	    IS_DG2_GRAPHICS_STEP(gt->i915, G11, STEP_A0, STEP_FOREVER))
		flags |= GUC_WA_CONTEXT_ISOLATION;

	/* Wa_16015675438 */
	if (!RCS_MASK(gt))
		flags |= GUC_WA_RCS_REGS_IN_CCS_REGS_LIST;

	return flags;
}

static u32 guc_ctl_devid(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	return (INTEL_DEVID(i915) << 16) | INTEL_REVID(i915);
}

/*
 * Initialise the GuC parameter block before starting the firmware
 * transfer. These parameters are read by the firmware on startup
 * and cannot be changed thereafter.
 */
static void guc_init_params(struct intel_guc *guc)
{
	u32 *params = guc->params;
	int i;

	BUILD_BUG_ON(sizeof(guc->params) != GUC_CTL_MAX_DWORDS * sizeof(u32));

	params[GUC_CTL_LOG_PARAMS] = guc_ctl_log_params_flags(guc);
	params[GUC_CTL_FEATURE] = guc_ctl_feature_flags(guc);
	params[GUC_CTL_DEBUG] = guc_ctl_debug_flags(guc);
	params[GUC_CTL_ADS] = guc_ctl_ads_flags(guc);
	params[GUC_CTL_WA] = guc_ctl_wa_flags(guc);
	params[GUC_CTL_DEVID] = guc_ctl_devid(guc);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		DRM_DEBUG_DRIVER("param[%2d] = %#x\n", i, params[i]);
}

/*
 * Initialise the GuC parameter block before starting the firmware
 * transfer. These parameters are read by the firmware on startup
 * and cannot be changed thereafter.
 */
void intel_guc_write_params(struct intel_guc *guc)
{
	struct intel_uncore *uncore = guc_to_gt(guc)->uncore;
	int i;

	/*
	 * All SOFT_SCRATCH registers are in FORCEWAKE_GT domain and
	 * they are power context saved so it's ok to release forcewake
	 * when we are done here and take it again at xfer time.
	 */
	intel_uncore_forcewake_get(uncore, FORCEWAKE_GT);

	intel_uncore_write(uncore, SOFT_SCRATCH(0), 0);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		intel_uncore_write(uncore, SOFT_SCRATCH(1 + i), guc->params[i]);

	intel_uncore_forcewake_put(uncore, FORCEWAKE_GT);
}

void intel_guc_dump_time_info(struct intel_guc *guc, struct drm_printer *p)
{
	struct intel_gt *gt = guc_to_gt(guc);
	intel_wakeref_t wakeref;
	u32 stamp = 0;
	u64 ktime;

	with_intel_runtime_pm(&gt->i915->runtime_pm, wakeref)
		stamp = intel_uncore_read(gt->uncore, GUCPMTIMESTAMP);
	ktime = ktime_get_boottime_ns();

	drm_printf(p, "Kernel timestamp: 0x%08llX [%llu]\n", ktime, ktime);
	drm_printf(p, "GuC timestamp: 0x%08X [%u]\n", stamp, stamp);
	drm_printf(p, "CS timestamp frequency: %u Hz, %u ns\n",
		   gt->clock_frequency, gt->clock_period_ns);
}

int intel_guc_init(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	int ret;

	ret = intel_uc_fw_init(&guc->fw);
	if (ret)
		goto out;

	ret = intel_guc_log_create(&guc->log);
	if (ret)
		goto err_fw;

	ret = intel_guc_capture_init(guc);
	if (ret)
		goto err_log;

	ret = intel_guc_ads_create(guc);
	if (ret)
		goto err_capture;

	GEM_BUG_ON(!guc->ads_vma);

	ret = intel_guc_ct_init(&guc->ct);
	if (ret)
		goto err_ads;

	if (intel_guc_submission_is_used(guc)) {
		/*
		 * This is stuff we need to have available at fw load time
		 * if we are planning to enable submission later
		 */
		ret = intel_guc_submission_init(guc);
		if (ret)
			goto err_ct;
	}

	if (intel_guc_slpc_is_used(guc)) {
		ret = intel_guc_slpc_init(&guc->slpc);
		if (ret)
			goto err_submission;
	}

	/* now that everything is perma-pinned, initialize the parameters */
	guc_init_params(guc);

	/* We need to notify the guc whenever we change the GGTT */
	i915_ggtt_enable_guc(gt->ggtt);

	intel_uc_fw_change_status(&guc->fw, INTEL_UC_FIRMWARE_LOADABLE);

	return 0;

err_submission:
	intel_guc_submission_fini(guc);
err_ct:
	intel_guc_ct_fini(&guc->ct);
err_ads:
	intel_guc_ads_destroy(guc);
err_capture:
	intel_guc_capture_destroy(guc);
err_log:
	intel_guc_log_destroy(&guc->log);
err_fw:
	intel_uc_fw_fini(&guc->fw);
out:
	i915_probe_error(gt->i915, "failed with %d\n", ret);
	return ret;
}

void intel_guc_fini(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	if (!intel_uc_fw_is_loadable(&guc->fw))
		return;

	i915_ggtt_disable_guc(gt->ggtt);

	if (intel_guc_slpc_is_used(guc))
		intel_guc_slpc_fini(&guc->slpc);

	if (intel_guc_submission_is_used(guc))
		intel_guc_submission_fini(guc);

	intel_guc_ct_fini(&guc->ct);

	intel_guc_ads_destroy(guc);
	intel_guc_capture_destroy(guc);
	intel_guc_log_destroy(&guc->log);
	intel_uc_fw_fini(&guc->fw);
}

/*
 * This function implements the MMIO based host to GuC interface.
 */
int intel_guc_send_mmio(struct intel_guc *guc, const u32 *request, u32 len,
			u32 *response_buf, u32 response_buf_size)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct intel_uncore *uncore = guc_to_gt(guc)->uncore;
	u32 header;
	int i;
	int ret;

	GEM_BUG_ON(!len);
	GEM_BUG_ON(len > guc->send_regs.count);

	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, request[0]) != GUC_HXG_ORIGIN_HOST);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, request[0]) != GUC_HXG_TYPE_REQUEST);

	mutex_lock(&guc->send_mutex);
	intel_uncore_forcewake_get(uncore, guc->send_regs.fw_domains);

retry:
	for (i = 0; i < len; i++)
		intel_uncore_write(uncore, guc_send_reg(guc, i), request[i]);

	intel_uncore_posting_read(uncore, guc_send_reg(guc, i - 1));

	intel_guc_notify(guc);

	/*
	 * No GuC command should ever take longer than 10ms.
	 * Fast commands should still complete in 10us.
	 */
	ret = __intel_wait_for_register_fw(uncore,
					   guc_send_reg(guc, 0),
					   GUC_HXG_MSG_0_ORIGIN,
					   FIELD_PREP(GUC_HXG_MSG_0_ORIGIN,
						      GUC_HXG_ORIGIN_GUC),
					   10, 10, &header);
	if (unlikely(ret)) {
timeout:
		drm_err(&i915->drm, "mmio request %#x: no reply %x\n",
			request[0], header);
		goto out;
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) == GUC_HXG_TYPE_NO_RESPONSE_BUSY) {
#define done ({ header = intel_uncore_read(uncore, guc_send_reg(guc, 0)); \
		FIELD_GET(GUC_HXG_MSG_0_ORIGIN, header) != GUC_HXG_ORIGIN_GUC || \
		FIELD_GET(GUC_HXG_MSG_0_TYPE, header) != GUC_HXG_TYPE_NO_RESPONSE_BUSY; })

		ret = wait_for(done, 1000);
		if (unlikely(ret))
			goto timeout;
		if (unlikely(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, header) !=
				       GUC_HXG_ORIGIN_GUC))
			goto proto;
#undef done
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) == GUC_HXG_TYPE_NO_RESPONSE_RETRY) {
		u32 reason = FIELD_GET(GUC_HXG_RETRY_MSG_0_REASON, header);

		drm_dbg(&i915->drm, "mmio request %#x: retrying, reason %u\n",
			request[0], reason);
		goto retry;
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) == GUC_HXG_TYPE_RESPONSE_FAILURE) {
		u32 hint = FIELD_GET(GUC_HXG_FAILURE_MSG_0_HINT, header);
		u32 error = FIELD_GET(GUC_HXG_FAILURE_MSG_0_ERROR, header);

		drm_err(&i915->drm, "mmio request %#x: failure %x/%u\n",
			request[0], error, hint);
		ret = -ENXIO;
		goto out;
	}

	if (FIELD_GET(GUC_HXG_MSG_0_TYPE, header) != GUC_HXG_TYPE_RESPONSE_SUCCESS) {
proto:
		drm_err(&i915->drm, "mmio request %#x: unexpected reply %#x\n",
			request[0], header);
		ret = -EPROTO;
		goto out;
	}

	if (response_buf) {
		int count = min(response_buf_size, guc->send_regs.count);

		GEM_BUG_ON(!count);

		response_buf[0] = header;

		for (i = 1; i < count; i++)
			response_buf[i] = intel_uncore_read(uncore,
							    guc_send_reg(guc, i));

		/* Use number of copied dwords as our return value */
		ret = count;
	} else {
		/* Use data from the GuC response as our return value */
		ret = FIELD_GET(GUC_HXG_RESPONSE_MSG_0_DATA0, header);
	}

out:
	intel_uncore_forcewake_put(uncore, guc->send_regs.fw_domains);
	mutex_unlock(&guc->send_mutex);

	return ret;
}

int intel_guc_to_host_process_recv_msg(struct intel_guc *guc,
				       const u32 *payload, u32 len)
{
	u32 msg;

	if (unlikely(!len))
		return -EPROTO;

	/* Make sure to handle only enabled messages */
	msg = payload[0] & guc->msg_enabled_mask;

	if (msg & INTEL_GUC_RECV_MSG_CRASH_DUMP_POSTED)
		drm_err(&guc_to_gt(guc)->i915->drm, "Received early GuC crash dump notification!\n");
	if (msg & INTEL_GUC_RECV_MSG_EXCEPTION)
		drm_err(&guc_to_gt(guc)->i915->drm, "Received early GuC exception notification!\n");

	return 0;
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

/**
 * intel_guc_suspend() - notify GuC entering suspend state
 * @guc:	the guc
 */
int intel_guc_suspend(struct intel_guc *guc)
{
	int ret;
	u32 action[] = {
		INTEL_GUC_ACTION_CLIENT_SOFT_RESET,
	};

	if (!intel_guc_is_ready(guc))
		return 0;

	if (intel_guc_submission_is_used(guc)) {
		/*
		 * This H2G MMIO command tears down the GuC in two steps. First it will
		 * generate a G2H CTB for every active context indicating a reset. In
		 * practice the i915 shouldn't ever get a G2H as suspend should only be
		 * called when the GPU is idle. Next, it tears down the CTBs and this
		 * H2G MMIO command completes.
		 *
		 * Don't abort on a failure code from the GuC. Keep going and do the
		 * clean up in santize() and re-initialisation on resume and hopefully
		 * the error here won't be problematic.
		 */
		ret = intel_guc_send_mmio(guc, action, ARRAY_SIZE(action), NULL, 0);
		if (ret)
			DRM_ERROR("GuC suspend: RESET_CLIENT action failed with error %d!\n", ret);
	}

	/* Signal that the GuC isn't running. */
	intel_guc_sanitize(guc);

	return 0;
}

/**
 * intel_guc_resume() - notify GuC resuming from suspend state
 * @guc:	the guc
 */
int intel_guc_resume(struct intel_guc *guc)
{
	/*
	 * NB: This function can still be called even if GuC submission is
	 * disabled, e.g. if GuC is enabled for HuC authentication only. Thus,
	 * if any code is later added here, it must be support doing nothing
	 * if submission is disabled (as per intel_guc_suspend).
	 */
	return 0;
}

/**
 * DOC: GuC Memory Management
 *
 * GuC can't allocate any memory for its own usage, so all the allocations must
 * be handled by the host driver. GuC accesses the memory via the GGTT, with the
 * exception of the top and bottom parts of the 4GB address space, which are
 * instead re-mapped by the GuC HW to memory location of the FW itself (WOPCM)
 * or other parts of the HW. The driver must take care not to place objects that
 * the GuC is going to access in these reserved ranges. The layout of the GuC
 * address space is shown below:
 *
 * ::
 *
 *     +===========> +====================+ <== FFFF_FFFF
 *     ^             |      Reserved      |
 *     |             +====================+ <== GUC_GGTT_TOP
 *     |             |                    |
 *     |             |        DRAM        |
 *    GuC            |                    |
 *  Address    +===> +====================+ <== GuC ggtt_pin_bias
 *   Space     ^     |                    |
 *     |       |     |                    |
 *     |      GuC    |        GuC         |
 *     |     WOPCM   |       WOPCM        |
 *     |      Size   |                    |
 *     |       |     |                    |
 *     v       v     |                    |
 *     +=======+===> +====================+ <== 0000_0000
 *
 * The lower part of GuC Address Space [0, ggtt_pin_bias) is mapped to GuC WOPCM
 * while upper part of GuC Address Space [ggtt_pin_bias, GUC_GGTT_TOP) is mapped
 * to DRAM. The value of the GuC ggtt_pin_bias is the GuC WOPCM size.
 */

/**
 * intel_guc_allocate_vma() - Allocate a GGTT VMA for GuC usage
 * @guc:	the guc
 * @size:	size of area to allocate (both virtual space and memory)
 *
 * This is a wrapper to create an object for use with the GuC. In order to
 * use it inside the GuC, an object needs to be pinned lifetime, so we allocate
 * both some backing storage and a range inside the Global GTT. We must pin
 * it in the GGTT somewhere other than than [0, GUC ggtt_pin_bias) because that
 * range is reserved inside GuC.
 *
 * Return:	A i915_vma if successful, otherwise an ERR_PTR.
 */
struct i915_vma *intel_guc_allocate_vma(struct intel_guc *guc, u32 size)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u64 flags;
	int ret;

	if (HAS_LMEM(gt->i915))
		obj = i915_gem_object_create_lmem(gt->i915, size,
						  I915_BO_ALLOC_CPU_CLEAR |
						  I915_BO_ALLOC_CONTIGUOUS |
						  I915_BO_ALLOC_PM_EARLY);
	else
		obj = i915_gem_object_create_shmem(gt->i915, size);

	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma))
		goto err;

	flags = PIN_OFFSET_BIAS | i915_ggtt_pin_bias(vma);
	ret = i915_ggtt_pin(vma, NULL, 0, flags);
	if (ret) {
		vma = ERR_PTR(ret);
		goto err;
	}

	return i915_vma_make_unshrinkable(vma);

err:
	i915_gem_object_put(obj);
	return vma;
}

/**
 * intel_guc_allocate_and_map_vma() - Allocate and map VMA for GuC usage
 * @guc:	the guc
 * @size:	size of area to allocate (both virtual space and memory)
 * @out_vma:	return variable for the allocated vma pointer
 * @out_vaddr:	return variable for the obj mapping
 *
 * This wrapper calls intel_guc_allocate_vma() and then maps the allocated
 * object with I915_MAP_WB.
 *
 * Return:	0 if successful, a negative errno code otherwise.
 */
int intel_guc_allocate_and_map_vma(struct intel_guc *guc, u32 size,
				   struct i915_vma **out_vma, void **out_vaddr)
{
	struct i915_vma *vma;
	void *vaddr;

	vma = intel_guc_allocate_vma(guc, size);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	vaddr = i915_gem_object_pin_map_unlocked(vma->obj,
						 i915_coherent_map_type(guc_to_gt(guc)->i915,
									vma->obj, true));
	if (IS_ERR(vaddr)) {
		i915_vma_unpin_and_release(&vma, 0);
		return PTR_ERR(vaddr);
	}

	*out_vma = vma;
	*out_vaddr = vaddr;

	return 0;
}

static int __guc_action_self_cfg(struct intel_guc *guc, u16 key, u16 len, u64 value)
{
	u32 request[HOST2GUC_SELF_CFG_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_HOST2GUC_SELF_CFG),
		FIELD_PREP(HOST2GUC_SELF_CFG_REQUEST_MSG_1_KLV_KEY, key) |
		FIELD_PREP(HOST2GUC_SELF_CFG_REQUEST_MSG_1_KLV_LEN, len),
		FIELD_PREP(HOST2GUC_SELF_CFG_REQUEST_MSG_2_VALUE32, lower_32_bits(value)),
		FIELD_PREP(HOST2GUC_SELF_CFG_REQUEST_MSG_3_VALUE64, upper_32_bits(value)),
	};
	int ret;

	GEM_BUG_ON(len > 2);
	GEM_BUG_ON(len == 1 && upper_32_bits(value));

	/* Self config must go over MMIO */
	ret = intel_guc_send_mmio(guc, request, ARRAY_SIZE(request), NULL, 0);

	if (unlikely(ret < 0))
		return ret;
	if (unlikely(ret > 1))
		return -EPROTO;
	if (unlikely(!ret))
		return -ENOKEY;

	return 0;
}

static int __guc_self_cfg(struct intel_guc *guc, u16 key, u16 len, u64 value)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	int err = __guc_action_self_cfg(guc, key, len, value);

	if (unlikely(err))
		i915_probe_error(i915, "Unsuccessful self-config (%pe) key %#hx value %#llx\n",
				 ERR_PTR(err), key, value);
	return err;
}

int intel_guc_self_cfg32(struct intel_guc *guc, u16 key, u32 value)
{
	return __guc_self_cfg(guc, key, 1, value);
}

int intel_guc_self_cfg64(struct intel_guc *guc, u16 key, u64 value)
{
	return __guc_self_cfg(guc, key, 2, value);
}

/**
 * intel_guc_load_status - dump information about GuC load status
 * @guc: the GuC
 * @p: the &drm_printer
 *
 * Pretty printer for GuC load status.
 */
void intel_guc_load_status(struct intel_guc *guc, struct drm_printer *p)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_uncore *uncore = gt->uncore;
	intel_wakeref_t wakeref;

	if (!intel_guc_is_supported(guc)) {
		drm_printf(p, "GuC not supported\n");
		return;
	}

	if (!intel_guc_is_wanted(guc)) {
		drm_printf(p, "GuC disabled\n");
		return;
	}

	intel_uc_fw_dump(&guc->fw, p);

	with_intel_runtime_pm(uncore->rpm, wakeref) {
		u32 status = intel_uncore_read(uncore, GUC_STATUS);
		u32 i;

		drm_printf(p, "\nGuC status 0x%08x:\n", status);
		drm_printf(p, "\tBootrom status = 0x%x\n",
			   (status & GS_BOOTROM_MASK) >> GS_BOOTROM_SHIFT);
		drm_printf(p, "\tuKernel status = 0x%x\n",
			   (status & GS_UKERNEL_MASK) >> GS_UKERNEL_SHIFT);
		drm_printf(p, "\tMIA Core status = 0x%x\n",
			   (status & GS_MIA_MASK) >> GS_MIA_SHIFT);
		drm_puts(p, "\nScratch registers:\n");
		for (i = 0; i < 16; i++) {
			drm_printf(p, "\t%2d: \t0x%x\n",
				   i, intel_uncore_read(uncore, SOFT_SCRATCH(i)));
		}
	}
}

void intel_guc_write_barrier(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	if (i915_gem_object_is_lmem(guc->ct.vma->obj)) {
		/*
		 * Ensure intel_uncore_write_fw can be used rather than
		 * intel_uncore_write.
		 */
		GEM_BUG_ON(guc->send_regs.fw_domains);

		/*
		 * This register is used by the i915 and GuC for MMIO based
		 * communication. Once we are in this code CTBs are the only
		 * method the i915 uses to communicate with the GuC so it is
		 * safe to write to this register (a value of 0 is NOP for MMIO
		 * communication). If we ever start mixing CTBs and MMIOs a new
		 * register will have to be chosen. This function is also used
		 * to enforce ordering of a work queue item write and an update
		 * to the process descriptor. When a work queue is being used,
		 * CTBs are also the only mechanism of communication.
		 */
		intel_uncore_write_fw(gt->uncore, GEN11_SOFT_SCRATCH(0), 0);
	} else {
		/* wmb() sufficient for a barrier if in smem */
		wmb();
	}
}

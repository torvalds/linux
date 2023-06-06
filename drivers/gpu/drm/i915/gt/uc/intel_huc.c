// SPDX-License-Identifier: MIT
/*
 * Copyright © 2016-2019 Intel Corporation
 */

#include <linux/types.h>

#include "gt/intel_gt.h"
#include "gt/intel_gt_print.h"
#include "intel_guc_reg.h"
#include "intel_huc.h"
#include "i915_drv.h"

#include <linux/device/bus.h>
#include <linux/mei_aux.h>

#define huc_printk(_huc, _level, _fmt, ...) \
	gt_##_level(huc_to_gt(_huc), "HuC: " _fmt, ##__VA_ARGS__)
#define huc_err(_huc, _fmt, ...)	huc_printk((_huc), err, _fmt, ##__VA_ARGS__)
#define huc_warn(_huc, _fmt, ...)	huc_printk((_huc), warn, _fmt, ##__VA_ARGS__)
#define huc_notice(_huc, _fmt, ...)	huc_printk((_huc), notice, _fmt, ##__VA_ARGS__)
#define huc_info(_huc, _fmt, ...)	huc_printk((_huc), info, _fmt, ##__VA_ARGS__)
#define huc_dbg(_huc, _fmt, ...)	huc_printk((_huc), dbg, _fmt, ##__VA_ARGS__)
#define huc_probe_error(_huc, _fmt, ...) huc_printk((_huc), probe_error, _fmt, ##__VA_ARGS__)

/**
 * DOC: HuC
 *
 * The HuC is a dedicated microcontroller for usage in media HEVC (High
 * Efficiency Video Coding) operations. Userspace can directly use the firmware
 * capabilities by adding HuC specific commands to batch buffers.
 *
 * The kernel driver is only responsible for loading the HuC firmware and
 * triggering its security authentication, which is performed by the GuC on
 * older platforms and by the GSC on newer ones. For the GuC to correctly
 * perform the authentication, the HuC binary must be loaded before the GuC one.
 * Loading the HuC is optional; however, not using the HuC might negatively
 * impact power usage and/or performance of media workloads, depending on the
 * use-cases.
 * HuC must be reloaded on events that cause the WOPCM to lose its contents
 * (S3/S4, FLR); GuC-authenticated HuC must also be reloaded on GuC/GT reset,
 * while GSC-managed HuC will survive that.
 *
 * See https://github.com/intel/media-driver for the latest details on HuC
 * functionality.
 */

/**
 * DOC: HuC Memory Management
 *
 * Similarly to the GuC, the HuC can't do any memory allocations on its own,
 * with the difference being that the allocations for HuC usage are handled by
 * the userspace driver instead of the kernel one. The HuC accesses the memory
 * via the PPGTT belonging to the context loaded on the VCS executing the
 * HuC-specific commands.
 */

/*
 * MEI-GSC load is an async process. The probing of the exposed aux device
 * (see intel_gsc.c) usually happens a few seconds after i915 probe, depending
 * on when the kernel schedules it. Unless something goes terribly wrong, we're
 * guaranteed for this to happen during boot, so the big timeout is a safety net
 * that we never expect to need.
 * MEI-PXP + HuC load usually takes ~300ms, but if the GSC needs to be resumed
 * and/or reset, this can take longer. Note that the kernel might schedule
 * other work between the i915 init/resume and the MEI one, which can add to
 * the delay.
 */
#define GSC_INIT_TIMEOUT_MS 10000
#define PXP_INIT_TIMEOUT_MS 5000

static int sw_fence_dummy_notify(struct i915_sw_fence *sf,
				 enum i915_sw_fence_notify state)
{
	return NOTIFY_DONE;
}

static void __delayed_huc_load_complete(struct intel_huc *huc)
{
	if (!i915_sw_fence_done(&huc->delayed_load.fence))
		i915_sw_fence_complete(&huc->delayed_load.fence);
}

static void delayed_huc_load_complete(struct intel_huc *huc)
{
	hrtimer_cancel(&huc->delayed_load.timer);
	__delayed_huc_load_complete(huc);
}

static void __gsc_init_error(struct intel_huc *huc)
{
	huc->delayed_load.status = INTEL_HUC_DELAYED_LOAD_ERROR;
	__delayed_huc_load_complete(huc);
}

static void gsc_init_error(struct intel_huc *huc)
{
	hrtimer_cancel(&huc->delayed_load.timer);
	__gsc_init_error(huc);
}

static void gsc_init_done(struct intel_huc *huc)
{
	hrtimer_cancel(&huc->delayed_load.timer);

	/* MEI-GSC init is done, now we wait for MEI-PXP to bind */
	huc->delayed_load.status = INTEL_HUC_WAITING_ON_PXP;
	if (!i915_sw_fence_done(&huc->delayed_load.fence))
		hrtimer_start(&huc->delayed_load.timer,
			      ms_to_ktime(PXP_INIT_TIMEOUT_MS),
			      HRTIMER_MODE_REL);
}

static enum hrtimer_restart huc_delayed_load_timer_callback(struct hrtimer *hrtimer)
{
	struct intel_huc *huc = container_of(hrtimer, struct intel_huc, delayed_load.timer);

	if (!intel_huc_is_authenticated(huc)) {
		if (huc->delayed_load.status == INTEL_HUC_WAITING_ON_GSC)
			huc_notice(huc, "timed out waiting for MEI GSC\n");
		else if (huc->delayed_load.status == INTEL_HUC_WAITING_ON_PXP)
			huc_notice(huc, "timed out waiting for MEI PXP\n");
		else
			MISSING_CASE(huc->delayed_load.status);

		__gsc_init_error(huc);
	}

	return HRTIMER_NORESTART;
}

static void huc_delayed_load_start(struct intel_huc *huc)
{
	ktime_t delay;

	GEM_BUG_ON(intel_huc_is_authenticated(huc));

	/*
	 * On resume we don't have to wait for MEI-GSC to be re-probed, but we
	 * do need to wait for MEI-PXP to reset & re-bind
	 */
	switch (huc->delayed_load.status) {
	case INTEL_HUC_WAITING_ON_GSC:
		delay = ms_to_ktime(GSC_INIT_TIMEOUT_MS);
		break;
	case INTEL_HUC_WAITING_ON_PXP:
		delay = ms_to_ktime(PXP_INIT_TIMEOUT_MS);
		break;
	default:
		gsc_init_error(huc);
		return;
	}

	/*
	 * This fence is always complete unless we're waiting for the
	 * GSC device to come up to load the HuC. We arm the fence here
	 * and complete it when we confirm that the HuC is loaded from
	 * the PXP bind callback.
	 */
	GEM_BUG_ON(!i915_sw_fence_done(&huc->delayed_load.fence));
	i915_sw_fence_fini(&huc->delayed_load.fence);
	i915_sw_fence_reinit(&huc->delayed_load.fence);
	i915_sw_fence_await(&huc->delayed_load.fence);
	i915_sw_fence_commit(&huc->delayed_load.fence);

	hrtimer_start(&huc->delayed_load.timer, delay, HRTIMER_MODE_REL);
}

static int gsc_notifier(struct notifier_block *nb, unsigned long action, void *data)
{
	struct device *dev = data;
	struct intel_huc *huc = container_of(nb, struct intel_huc, delayed_load.nb);
	struct intel_gsc_intf *intf = &huc_to_gt(huc)->gsc.intf[0];

	if (!intf->adev || &intf->adev->aux_dev.dev != dev)
		return 0;

	switch (action) {
	case BUS_NOTIFY_BOUND_DRIVER: /* mei driver bound to aux device */
		gsc_init_done(huc);
		break;

	case BUS_NOTIFY_DRIVER_NOT_BOUND: /* mei driver fails to be bound */
	case BUS_NOTIFY_UNBIND_DRIVER: /* mei driver about to be unbound */
		huc_info(huc, "MEI driver not bound, disabling load\n");
		gsc_init_error(huc);
		break;
	}

	return 0;
}

void intel_huc_register_gsc_notifier(struct intel_huc *huc, const struct bus_type *bus)
{
	int ret;

	if (!intel_huc_is_loaded_by_gsc(huc))
		return;

	huc->delayed_load.nb.notifier_call = gsc_notifier;
	ret = bus_register_notifier(bus, &huc->delayed_load.nb);
	if (ret) {
		huc_err(huc, "failed to register GSC notifier %pe\n", ERR_PTR(ret));
		huc->delayed_load.nb.notifier_call = NULL;
		gsc_init_error(huc);
	}
}

void intel_huc_unregister_gsc_notifier(struct intel_huc *huc, const struct bus_type *bus)
{
	if (!huc->delayed_load.nb.notifier_call)
		return;

	delayed_huc_load_complete(huc);

	bus_unregister_notifier(bus, &huc->delayed_load.nb);
	huc->delayed_load.nb.notifier_call = NULL;
}

static void delayed_huc_load_init(struct intel_huc *huc)
{
	/*
	 * Initialize fence to be complete as this is expected to be complete
	 * unless there is a delayed HuC load in progress.
	 */
	i915_sw_fence_init(&huc->delayed_load.fence,
			   sw_fence_dummy_notify);
	i915_sw_fence_commit(&huc->delayed_load.fence);

	hrtimer_init(&huc->delayed_load.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	huc->delayed_load.timer.function = huc_delayed_load_timer_callback;
}

static void delayed_huc_load_fini(struct intel_huc *huc)
{
	/*
	 * the fence is initialized in init_early, so we need to clean it up
	 * even if HuC loading is off.
	 */
	delayed_huc_load_complete(huc);
	i915_sw_fence_fini(&huc->delayed_load.fence);
}

int intel_huc_sanitize(struct intel_huc *huc)
{
	delayed_huc_load_complete(huc);
	intel_uc_fw_sanitize(&huc->fw);
	return 0;
}

static bool vcs_supported(struct intel_gt *gt)
{
	intel_engine_mask_t mask = gt->info.engine_mask;

	/*
	 * We reach here from i915_driver_early_probe for the primary GT before
	 * its engine mask is set, so we use the device info engine mask for it;
	 * this means we're not taking VCS fusing into account, but if the
	 * primary GT supports VCS engines we expect at least one of them to
	 * remain unfused so we're fine.
	 * For other GTs we expect the GT-specific mask to be set before we
	 * call this function.
	 */
	GEM_BUG_ON(!gt_is_root(gt) && !gt->info.engine_mask);

	if (gt_is_root(gt))
		mask = RUNTIME_INFO(gt->i915)->platform_engine_mask;
	else
		mask = gt->info.engine_mask;

	return __ENGINE_INSTANCES_MASK(mask, VCS0, I915_MAX_VCS);
}

void intel_huc_init_early(struct intel_huc *huc)
{
	struct drm_i915_private *i915 = huc_to_gt(huc)->i915;
	struct intel_gt *gt = huc_to_gt(huc);

	intel_uc_fw_init_early(&huc->fw, INTEL_UC_FW_TYPE_HUC);

	/*
	 * we always init the fence as already completed, even if HuC is not
	 * supported. This way we don't have to distinguish between HuC not
	 * supported/disabled or already loaded, and can focus on if the load
	 * is currently in progress (fence not complete) or not, which is what
	 * we care about for stalling userspace submissions.
	 */
	delayed_huc_load_init(huc);

	if (!vcs_supported(gt)) {
		intel_uc_fw_change_status(&huc->fw, INTEL_UC_FIRMWARE_NOT_SUPPORTED);
		return;
	}

	if (GRAPHICS_VER(i915) >= 11) {
		huc->status.reg = GEN11_HUC_KERNEL_LOAD_INFO;
		huc->status.mask = HUC_LOAD_SUCCESSFUL;
		huc->status.value = HUC_LOAD_SUCCESSFUL;
	} else {
		huc->status.reg = HUC_STATUS2;
		huc->status.mask = HUC_FW_VERIFIED;
		huc->status.value = HUC_FW_VERIFIED;
	}
}

#define HUC_LOAD_MODE_STRING(x) (x ? "GSC" : "legacy")
static int check_huc_loading_mode(struct intel_huc *huc)
{
	struct intel_gt *gt = huc_to_gt(huc);
	bool fw_needs_gsc = intel_huc_is_loaded_by_gsc(huc);
	bool hw_uses_gsc = false;

	/*
	 * The fuse for HuC load via GSC is only valid on platforms that have
	 * GuC deprivilege.
	 */
	if (HAS_GUC_DEPRIVILEGE(gt->i915))
		hw_uses_gsc = intel_uncore_read(gt->uncore, GUC_SHIM_CONTROL2) &
			      GSC_LOADS_HUC;

	if (fw_needs_gsc != hw_uses_gsc) {
		huc_err(huc, "mismatch between FW (%s) and HW (%s) load modes\n",
			HUC_LOAD_MODE_STRING(fw_needs_gsc), HUC_LOAD_MODE_STRING(hw_uses_gsc));
		return -ENOEXEC;
	}

	/* make sure we can access the GSC via the mei driver if we need it */
	if (!(IS_ENABLED(CONFIG_INTEL_MEI_PXP) && IS_ENABLED(CONFIG_INTEL_MEI_GSC)) &&
	    fw_needs_gsc) {
		huc_info(huc, "can't load due to missing MEI modules\n");
		return -EIO;
	}

	huc_dbg(huc, "loaded by GSC = %s\n", str_yes_no(fw_needs_gsc));

	return 0;
}

int intel_huc_init(struct intel_huc *huc)
{
	int err;

	err = check_huc_loading_mode(huc);
	if (err)
		goto out;

	err = intel_uc_fw_init(&huc->fw);
	if (err)
		goto out;

	intel_uc_fw_change_status(&huc->fw, INTEL_UC_FIRMWARE_LOADABLE);

	return 0;

out:
	intel_uc_fw_change_status(&huc->fw, INTEL_UC_FIRMWARE_INIT_FAIL);
	huc_info(huc, "initialization failed %pe\n", ERR_PTR(err));
	return err;
}

void intel_huc_fini(struct intel_huc *huc)
{
	/*
	 * the fence is initialized in init_early, so we need to clean it up
	 * even if HuC loading is off.
	 */
	delayed_huc_load_fini(huc);

	if (intel_uc_fw_is_loadable(&huc->fw))
		intel_uc_fw_fini(&huc->fw);
}

void intel_huc_suspend(struct intel_huc *huc)
{
	if (!intel_uc_fw_is_loadable(&huc->fw))
		return;

	/*
	 * in the unlikely case that we're suspending before the GSC has
	 * completed its loading sequence, just stop waiting. We'll restart
	 * on resume.
	 */
	delayed_huc_load_complete(huc);
}

int intel_huc_wait_for_auth_complete(struct intel_huc *huc)
{
	struct intel_gt *gt = huc_to_gt(huc);
	int ret;

	ret = __intel_wait_for_register(gt->uncore,
					huc->status.reg,
					huc->status.mask,
					huc->status.value,
					2, 50, NULL);

	/* mark the load process as complete even if the wait failed */
	delayed_huc_load_complete(huc);

	if (ret) {
		huc_err(huc, "firmware not verified %pe\n", ERR_PTR(ret));
		intel_uc_fw_change_status(&huc->fw, INTEL_UC_FIRMWARE_LOAD_FAIL);
		return ret;
	}

	intel_uc_fw_change_status(&huc->fw, INTEL_UC_FIRMWARE_RUNNING);
	huc_info(huc, "authenticated!\n");
	return 0;
}

/**
 * intel_huc_auth() - Authenticate HuC uCode
 * @huc: intel_huc structure
 *
 * Called after HuC and GuC firmware loading during intel_uc_init_hw().
 *
 * This function invokes the GuC action to authenticate the HuC firmware,
 * passing the offset of the RSA signature to intel_guc_auth_huc(). It then
 * waits for up to 50ms for firmware verification ACK.
 */
int intel_huc_auth(struct intel_huc *huc)
{
	struct intel_gt *gt = huc_to_gt(huc);
	struct intel_guc *guc = &gt->uc.guc;
	int ret;

	if (!intel_uc_fw_is_loaded(&huc->fw))
		return -ENOEXEC;

	/* GSC will do the auth */
	if (intel_huc_is_loaded_by_gsc(huc))
		return -ENODEV;

	ret = i915_inject_probe_error(gt->i915, -ENXIO);
	if (ret)
		goto fail;

	GEM_BUG_ON(intel_uc_fw_is_running(&huc->fw));

	ret = intel_guc_auth_huc(guc, intel_guc_ggtt_offset(guc, huc->fw.rsa_data));
	if (ret) {
		huc_err(huc, "authentication by GuC failed %pe\n", ERR_PTR(ret));
		goto fail;
	}

	/* Check authentication status, it should be done by now */
	ret = intel_huc_wait_for_auth_complete(huc);
	if (ret)
		goto fail;

	return 0;

fail:
	huc_probe_error(huc, "authentication failed %pe\n", ERR_PTR(ret));
	return ret;
}

bool intel_huc_is_authenticated(struct intel_huc *huc)
{
	struct intel_gt *gt = huc_to_gt(huc);
	intel_wakeref_t wakeref;
	u32 status = 0;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		status = intel_uncore_read(gt->uncore, huc->status.reg);

	return (status & huc->status.mask) == huc->status.value;
}

/**
 * intel_huc_check_status() - check HuC status
 * @huc: intel_huc structure
 *
 * This function reads status register to verify if HuC
 * firmware was successfully loaded.
 *
 * The return values match what is expected for the I915_PARAM_HUC_STATUS
 * getparam.
 */
int intel_huc_check_status(struct intel_huc *huc)
{
	switch (__intel_uc_fw_status(&huc->fw)) {
	case INTEL_UC_FIRMWARE_NOT_SUPPORTED:
		return -ENODEV;
	case INTEL_UC_FIRMWARE_DISABLED:
		return -EOPNOTSUPP;
	case INTEL_UC_FIRMWARE_MISSING:
		return -ENOPKG;
	case INTEL_UC_FIRMWARE_ERROR:
		return -ENOEXEC;
	case INTEL_UC_FIRMWARE_INIT_FAIL:
		return -ENOMEM;
	case INTEL_UC_FIRMWARE_LOAD_FAIL:
		return -EIO;
	default:
		break;
	}

	return intel_huc_is_authenticated(huc);
}

static bool huc_has_delayed_load(struct intel_huc *huc)
{
	return intel_huc_is_loaded_by_gsc(huc) &&
	       (huc->delayed_load.status != INTEL_HUC_DELAYED_LOAD_ERROR);
}

void intel_huc_update_auth_status(struct intel_huc *huc)
{
	if (!intel_uc_fw_is_loadable(&huc->fw))
		return;

	if (intel_huc_is_authenticated(huc))
		intel_uc_fw_change_status(&huc->fw,
					  INTEL_UC_FIRMWARE_RUNNING);
	else if (huc_has_delayed_load(huc))
		huc_delayed_load_start(huc);
}

/**
 * intel_huc_load_status - dump information about HuC load status
 * @huc: the HuC
 * @p: the &drm_printer
 *
 * Pretty printer for HuC load status.
 */
void intel_huc_load_status(struct intel_huc *huc, struct drm_printer *p)
{
	struct intel_gt *gt = huc_to_gt(huc);
	intel_wakeref_t wakeref;

	if (!intel_huc_is_supported(huc)) {
		drm_printf(p, "HuC not supported\n");
		return;
	}

	if (!intel_huc_is_wanted(huc)) {
		drm_printf(p, "HuC disabled\n");
		return;
	}

	intel_uc_fw_dump(&huc->fw, p);

	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		drm_printf(p, "HuC status: 0x%08x\n",
			   intel_uncore_read(gt->uncore, huc->status.reg));
}

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2016-2019 Intel Corporation
 */

#include <linux/types.h>

#include "gt/intel_gt.h"
#include "intel_guc_reg.h"
#include "intel_huc.h"
#include "i915_drv.h"

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

void intel_huc_init_early(struct intel_huc *huc)
{
	struct drm_i915_private *i915 = huc_to_gt(huc)->i915;

	intel_uc_fw_init_early(&huc->fw, INTEL_UC_FW_TYPE_HUC);

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
		drm_err(&gt->i915->drm,
			"mismatch between HuC FW (%s) and HW (%s) load modes\n",
			HUC_LOAD_MODE_STRING(fw_needs_gsc),
			HUC_LOAD_MODE_STRING(hw_uses_gsc));
		return -ENOEXEC;
	}

	/* make sure we can access the GSC via the mei driver if we need it */
	if (!(IS_ENABLED(CONFIG_INTEL_MEI_PXP) && IS_ENABLED(CONFIG_INTEL_MEI_GSC)) &&
	    fw_needs_gsc) {
		drm_info(&gt->i915->drm,
			 "Can't load HuC due to missing MEI modules\n");
		return -EIO;
	}

	drm_dbg(&gt->i915->drm, "GSC loads huc=%s\n", str_yes_no(fw_needs_gsc));

	return 0;
}

int intel_huc_init(struct intel_huc *huc)
{
	struct drm_i915_private *i915 = huc_to_gt(huc)->i915;
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
	drm_info(&i915->drm, "HuC init failed with %d\n", err);
	return err;
}

void intel_huc_fini(struct intel_huc *huc)
{
	if (!intel_uc_fw_is_loadable(&huc->fw))
		return;

	intel_uc_fw_fini(&huc->fw);
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
		DRM_ERROR("HuC: GuC did not ack Auth request %d\n", ret);
		goto fail;
	}

	/* Check authentication status, it should be done by now */
	ret = __intel_wait_for_register(gt->uncore,
					huc->status.reg,
					huc->status.mask,
					huc->status.value,
					2, 50, NULL);
	if (ret) {
		DRM_ERROR("HuC: Firmware not verified %d\n", ret);
		goto fail;
	}

	intel_uc_fw_change_status(&huc->fw, INTEL_UC_FIRMWARE_RUNNING);
	drm_info(&gt->i915->drm, "HuC authenticated\n");
	return 0;

fail:
	i915_probe_error(gt->i915, "HuC: Authentication failed %d\n", ret);
	intel_uc_fw_change_status(&huc->fw, INTEL_UC_FIRMWARE_LOAD_FAIL);
	return ret;
}

static bool huc_is_authenticated(struct intel_huc *huc)
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
 * Returns:
 *  * -ENODEV if HuC is not present on this platform,
 *  * -EOPNOTSUPP if HuC firmware is disabled,
 *  * -ENOPKG if HuC firmware was not installed,
 *  * -ENOEXEC if HuC firmware is invalid or mismatched,
 *  * 0 if HuC firmware is not running,
 *  * 1 if HuC firmware is authenticated and running.
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
	default:
		break;
	}

	return huc_is_authenticated(huc);
}

void intel_huc_update_auth_status(struct intel_huc *huc)
{
	if (!intel_uc_fw_is_loadable(&huc->fw))
		return;

	if (huc_is_authenticated(huc))
		intel_uc_fw_change_status(&huc->fw,
					  INTEL_UC_FIRMWARE_RUNNING);
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

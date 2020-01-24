// SPDX-License-Identifier: MIT
/*
 * Copyright © 2016-2019 Intel Corporation
 */

#include <linux/types.h>

#include "gt/intel_gt.h"
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
 * triggering its security authentication, which is performed by the GuC. For
 * The GuC to correctly perform the authentication, the HuC binary must be
 * loaded before the GuC one. Loading the HuC is optional; however, not using
 * the HuC might negatively impact power usage and/or performance of media
 * workloads, depending on the use-cases.
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

	intel_huc_fw_init_early(huc);

	if (INTEL_GEN(i915) >= 11) {
		huc->status.reg = GEN11_HUC_KERNEL_LOAD_INFO;
		huc->status.mask = HUC_LOAD_SUCCESSFUL;
		huc->status.value = HUC_LOAD_SUCCESSFUL;
	} else {
		huc->status.reg = HUC_STATUS2;
		huc->status.mask = HUC_FW_VERIFIED;
		huc->status.value = HUC_FW_VERIFIED;
	}
}

static int intel_huc_rsa_data_create(struct intel_huc *huc)
{
	struct intel_gt *gt = huc_to_gt(huc);
	struct intel_guc *guc = &gt->uc.guc;
	struct i915_vma *vma;
	size_t copied;
	void *vaddr;
	int err;

	err = i915_inject_probe_error(gt->i915, -ENXIO);
	if (err)
		return err;

	/*
	 * HuC firmware will sit above GUC_GGTT_TOP and will not map
	 * through GTT. Unfortunately, this means GuC cannot perform
	 * the HuC auth. as the rsa offset now falls within the GuC
	 * inaccessible range. We resort to perma-pinning an additional
	 * vma within the accessible range that only contains the rsa
	 * signature. The GuC can use this extra pinning to perform
	 * the authentication since its GGTT offset will be GuC
	 * accessible.
	 */
	GEM_BUG_ON(huc->fw.rsa_size > PAGE_SIZE);
	vma = intel_guc_allocate_vma(guc, PAGE_SIZE);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	vaddr = i915_gem_object_pin_map(vma->obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		i915_vma_unpin_and_release(&vma, 0);
		return PTR_ERR(vaddr);
	}

	copied = intel_uc_fw_copy_rsa(&huc->fw, vaddr, vma->size);
	GEM_BUG_ON(copied < huc->fw.rsa_size);

	i915_gem_object_unpin_map(vma->obj);

	huc->rsa_data = vma;

	return 0;
}

static void intel_huc_rsa_data_destroy(struct intel_huc *huc)
{
	i915_vma_unpin_and_release(&huc->rsa_data, 0);
}

int intel_huc_init(struct intel_huc *huc)
{
	struct drm_i915_private *i915 = huc_to_gt(huc)->i915;
	int err;

	err = intel_uc_fw_init(&huc->fw);
	if (err)
		goto out;

	/*
	 * HuC firmware image is outside GuC accessible range.
	 * Copy the RSA signature out of the image into
	 * a perma-pinned region set aside for it
	 */
	err = intel_huc_rsa_data_create(huc);
	if (err)
		goto out_fini;

	return 0;

out_fini:
	intel_uc_fw_fini(&huc->fw);
out:
	intel_uc_fw_cleanup_fetch(&huc->fw);
	DRM_DEV_DEBUG_DRIVER(i915->drm.dev, "failed with %d\n", err);
	return err;
}

void intel_huc_fini(struct intel_huc *huc)
{
	if (!intel_uc_fw_is_available(&huc->fw))
		return;

	intel_huc_rsa_data_destroy(huc);
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

	GEM_BUG_ON(intel_huc_is_authenticated(huc));

	if (!intel_uc_fw_is_loaded(&huc->fw))
		return -ENOEXEC;

	ret = i915_inject_probe_error(gt->i915, -ENXIO);
	if (ret)
		goto fail;

	ret = intel_guc_auth_huc(guc,
				 intel_guc_ggtt_offset(guc, huc->rsa_data));
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
	return 0;

fail:
	i915_probe_error(gt->i915, "HuC: Authentication failed %d\n", ret);
	intel_uc_fw_change_status(&huc->fw, INTEL_UC_FIRMWARE_FAIL);
	return ret;
}

/**
 * intel_huc_check_status() - check HuC status
 * @huc: intel_huc structure
 *
 * This function reads status register to verify if HuC
 * firmware was successfully loaded.
 *
 * Returns: 1 if HuC firmware is loaded and verified,
 * 0 if HuC firmware is not loaded and -ENODEV if HuC
 * is not present on this platform.
 */
int intel_huc_check_status(struct intel_huc *huc)
{
	struct intel_gt *gt = huc_to_gt(huc);
	intel_wakeref_t wakeref;
	u32 status = 0;

	if (!intel_huc_is_supported(huc))
		return -ENODEV;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		status = intel_uncore_read(gt->uncore, huc->status.reg);

	return (status & huc->status.mask) == huc->status.value;
}

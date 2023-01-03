// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#include "gt/intel_gsc.h"
#include "gt/intel_gt.h"
#include "intel_huc.h"
#include "intel_huc_fw.h"
#include "i915_drv.h"
#include "pxp/intel_pxp_huc.h"

int intel_huc_fw_load_and_auth_via_gsc(struct intel_huc *huc)
{
	int ret;

	if (!intel_huc_is_loaded_by_gsc(huc))
		return -ENODEV;

	if (!intel_uc_fw_is_loadable(&huc->fw))
		return -ENOEXEC;

	/*
	 * If we abort a suspend, HuC might still be loaded when the mei
	 * component gets re-bound and this function called again. If so, just
	 * mark the HuC as loaded.
	 */
	if (intel_huc_is_authenticated(huc)) {
		intel_uc_fw_change_status(&huc->fw, INTEL_UC_FIRMWARE_RUNNING);
		return 0;
	}

	GEM_WARN_ON(intel_uc_fw_is_loaded(&huc->fw));

	ret = intel_pxp_huc_load_and_auth(&huc_to_gt(huc)->pxp);
	if (ret)
		return ret;

	intel_uc_fw_change_status(&huc->fw, INTEL_UC_FIRMWARE_TRANSFERRED);

	return intel_huc_wait_for_auth_complete(huc);
}

/**
 * intel_huc_fw_upload() - load HuC uCode to device via DMA transfer
 * @huc: intel_huc structure
 *
 * Called from intel_uc_init_hw() during driver load, resume from sleep and
 * after a GPU reset. Note that HuC must be loaded before GuC.
 *
 * The firmware image should have already been fetched into memory, so only
 * check that fetch succeeded, and then transfer the image to the h/w.
 *
 * Return:	non-zero code on error
 */
int intel_huc_fw_upload(struct intel_huc *huc)
{
	if (intel_huc_is_loaded_by_gsc(huc))
		return -ENODEV;

	/* HW doesn't look at destination address for HuC, so set it to 0 */
	return intel_uc_fw_upload(&huc->fw, 0, HUC_UKERNEL);
}

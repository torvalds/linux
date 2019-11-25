// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#include "gt/intel_gt.h"
#include "intel_huc_fw.h"
#include "i915_drv.h"

/**
 * DOC: HuC Firmware
 *
 * Motivation:
 * GEN9 introduces a new dedicated firmware for usage in media HEVC (High
 * Efficiency Video Coding) operations. Userspace can use the firmware
 * capabilities by adding HuC specific commands to batch buffers.
 *
 * Implementation:
 * The same firmware loader is used as the GuC. However, the actual
 * loading to HW is deferred until GEM initialization is done.
 *
 * Note that HuC firmware loading must be done before GuC loading.
 */

/**
 * intel_huc_fw_init_early() - initializes HuC firmware struct
 * @huc: intel_huc struct
 *
 * On platforms with HuC selects firmware for uploading
 */
void intel_huc_fw_init_early(struct intel_huc *huc)
{
	struct intel_gt *gt = huc_to_gt(huc);
	struct intel_uc *uc = &gt->uc;
	struct drm_i915_private *i915 = gt->i915;

	intel_uc_fw_init_early(&huc->fw, INTEL_UC_FW_TYPE_HUC,
			       intel_uc_uses_guc(uc),
			       INTEL_INFO(i915)->platform, INTEL_REVID(i915));
}

/**
 * intel_huc_fw_upload() - load HuC uCode to device
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
	/* HW doesn't look at destination address for HuC, so set it to 0 */
	return intel_uc_fw_upload(&huc->fw, huc_to_gt(huc), 0, HUC_UKERNEL);
}

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_huc.h"

#include "regs/xe_guc_regs.h"
#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_guc.h"
#include "xe_mmio.h"
#include "xe_uc_fw.h"

static struct xe_gt *
huc_to_gt(struct xe_huc *huc)
{
	return container_of(huc, struct xe_gt, uc.huc);
}

static struct xe_device *
huc_to_xe(struct xe_huc *huc)
{
	return gt_to_xe(huc_to_gt(huc));
}

static struct xe_guc *
huc_to_guc(struct xe_huc *huc)
{
	return &container_of(huc, struct xe_uc, huc)->guc;
}

int xe_huc_init(struct xe_huc *huc)
{
	struct xe_device *xe = huc_to_xe(huc);
	int ret;

	huc->fw.type = XE_UC_FW_TYPE_HUC;
	ret = xe_uc_fw_init(&huc->fw);
	if (ret)
		goto out;

	xe_uc_fw_change_status(&huc->fw, XE_UC_FIRMWARE_LOADABLE);

	return 0;

out:
	if (xe_uc_fw_is_disabled(&huc->fw)) {
		drm_info(&xe->drm, "HuC disabled\n");
		return 0;
	}
	drm_err(&xe->drm, "HuC init failed with %d", ret);
	return ret;
}

int xe_huc_upload(struct xe_huc *huc)
{
	if (xe_uc_fw_is_disabled(&huc->fw))
		return 0;
	return xe_uc_fw_upload(&huc->fw, 0, HUC_UKERNEL);
}

int xe_huc_auth(struct xe_huc *huc)
{
	struct xe_device *xe = huc_to_xe(huc);
	struct xe_gt *gt = huc_to_gt(huc);
	struct xe_guc *guc = huc_to_guc(huc);
	int ret;

	if (xe_uc_fw_is_disabled(&huc->fw))
		return 0;

	xe_assert(xe, !xe_uc_fw_is_running(&huc->fw));

	if (!xe_uc_fw_is_loaded(&huc->fw))
		return -ENOEXEC;

	ret = xe_guc_auth_huc(guc, xe_bo_ggtt_addr(huc->fw.bo) +
			      xe_uc_fw_rsa_offset(&huc->fw));
	if (ret) {
		drm_err(&xe->drm, "HuC: GuC did not ack Auth request %d\n",
			ret);
		goto fail;
	}

	ret = xe_mmio_wait32(gt, HUC_KERNEL_LOAD_INFO, HUC_LOAD_SUCCESSFUL,
			     HUC_LOAD_SUCCESSFUL, 100000, NULL, false);
	if (ret) {
		drm_err(&xe->drm, "HuC: Firmware not verified %d\n", ret);
		goto fail;
	}

	xe_uc_fw_change_status(&huc->fw, XE_UC_FIRMWARE_RUNNING);
	drm_dbg(&xe->drm, "HuC authenticated\n");

	return 0;

fail:
	drm_err(&xe->drm, "HuC authentication failed %d\n", ret);
	xe_uc_fw_change_status(&huc->fw, XE_UC_FIRMWARE_LOAD_FAIL);

	return ret;
}

void xe_huc_sanitize(struct xe_huc *huc)
{
	if (xe_uc_fw_is_disabled(&huc->fw))
		return;
	xe_uc_fw_change_status(&huc->fw, XE_UC_FIRMWARE_LOADABLE);
}

void xe_huc_print_info(struct xe_huc *huc, struct drm_printer *p)
{
	struct xe_gt *gt = huc_to_gt(huc);
	int err;

	xe_uc_fw_print(&huc->fw, p);

	if (xe_uc_fw_is_disabled(&huc->fw))
		return;

	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		return;

	drm_printf(p, "\nHuC status: 0x%08x\n",
		   xe_mmio_read32(gt, HUC_KERNEL_LOAD_INFO));

	xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
}

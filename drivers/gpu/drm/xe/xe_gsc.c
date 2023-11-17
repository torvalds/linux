// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gsc.h"

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_uc_fw.h"

static struct xe_gt *
gsc_to_gt(struct xe_gsc *gsc)
{
	return container_of(gsc, struct xe_gt, uc.gsc);
}

int xe_gsc_init(struct xe_gsc *gsc)
{
	struct xe_gt *gt = gsc_to_gt(gsc);
	struct xe_tile *tile = gt_to_tile(gt);
	int ret;

	gsc->fw.type = XE_UC_FW_TYPE_GSC;

	/* The GSC uC is only available on the media GT */
	if (tile->media_gt && (gt != tile->media_gt)) {
		xe_uc_fw_change_status(&gsc->fw, XE_UC_FIRMWARE_NOT_SUPPORTED);
		return 0;
	}

	/*
	 * Some platforms can have GuC but not GSC. That would cause
	 * xe_uc_fw_init(gsc) to return a "not supported" failure code and abort
	 * all firmware loading. So check for GSC being enabled before
	 * propagating the failure back up. That way the higher level will keep
	 * going and load GuC as appropriate.
	 */
	ret = xe_uc_fw_init(&gsc->fw);
	if (!xe_uc_fw_is_enabled(&gsc->fw))
		return 0;
	else if (ret)
		goto out;

	return 0;

out:
	xe_gt_err(gt, "GSC init failed with %d", ret);
	return ret;
}


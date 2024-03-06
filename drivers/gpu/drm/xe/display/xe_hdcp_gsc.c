// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include <drm/drm_print.h>
#include "intel_hdcp_gsc.h"
#include "xe_device_types.h"

bool intel_hdcp_gsc_cs_required(struct xe_device *xe)
{
	return true;
}

bool intel_hdcp_gsc_check_status(struct xe_device *xe)
{
	return false;
}

int intel_hdcp_gsc_init(struct xe_device *xe)
{
	drm_dbg_kms(&xe->drm, "HDCP support not yet implemented\n");
	return -ENODEV;
}

void intel_hdcp_gsc_fini(struct xe_device *xe)
{
}

ssize_t intel_hdcp_gsc_msg_send(struct xe_device *xe, u8 *msg_in,
				size_t msg_in_len, u8 *msg_out,
				size_t msg_out_len)
{
	return -ENODEV;
}

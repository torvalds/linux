// SPDX-License-Identifier: MIT
/* Copyright © 2026 Intel Corporation */

#include <drm/intel/display_parent_interface.h>

#include "xe_device.h"
#include "xe_pcode.h"

static int xe_display_pcode_read(struct drm_device *drm, u32 mbox, u32 *val, u32 *val1)
{
	struct xe_device *xe = to_xe_device(drm);
	struct xe_tile *tile = xe_device_get_root_tile(xe);

	return xe_pcode_read(tile, mbox, val, val1);
}

static int xe_display_pcode_write_timeout(struct drm_device *drm, u32 mbox, u32 val, int timeout_ms)
{
	struct xe_device *xe = to_xe_device(drm);
	struct xe_tile *tile = xe_device_get_root_tile(xe);

	return xe_pcode_write_timeout(tile, mbox, val, timeout_ms);
}

static int xe_display_pcode_request(struct drm_device *drm, u32 mbox, u32 request,
				    u32 reply_mask, u32 reply, int timeout_base_ms)
{
	struct xe_device *xe = to_xe_device(drm);
	struct xe_tile *tile = xe_device_get_root_tile(xe);

	return xe_pcode_request(tile, mbox, request, reply_mask, reply, timeout_base_ms);
}

const struct intel_display_pcode_interface xe_display_pcode_interface = {
	.read = xe_display_pcode_read,
	.write = xe_display_pcode_write_timeout,
	.request = xe_display_pcode_request,
};

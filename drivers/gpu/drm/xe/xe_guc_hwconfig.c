// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_hwconfig.h"

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "abi/guc_actions_abi.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_guc.h"
#include "xe_map.h"

static int send_get_hwconfig(struct xe_guc *guc, u64 ggtt_addr, u32 size)
{
	u32 action[] = {
		XE_GUC_ACTION_GET_HWCONFIG,
		lower_32_bits(ggtt_addr),
		upper_32_bits(ggtt_addr),
		size,
	};

	return xe_guc_mmio_send(guc, action, ARRAY_SIZE(action));
}

static int guc_hwconfig_size(struct xe_guc *guc, u32 *size)
{
	int ret = send_get_hwconfig(guc, 0, 0);

	if (ret < 0)
		return ret;

	*size = ret;
	return 0;
}

static int guc_hwconfig_copy(struct xe_guc *guc)
{
	int ret = send_get_hwconfig(guc, xe_bo_ggtt_addr(guc->hwconfig.bo),
				    guc->hwconfig.size);

	if (ret < 0)
		return ret;

	return 0;
}

int xe_guc_hwconfig_init(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_bo *bo;
	u32 size;
	int err;

	/* Initialization already done */
	if (guc->hwconfig.bo)
		return 0;

	/*
	 * All hwconfig the same across GTs so only GT0 needs to be configured
	 */
	if (gt->info.id != XE_GT0)
		return 0;

	/* ADL_P, DG2+ supports hwconfig table */
	if (GRAPHICS_VERx100(xe) < 1255 && xe->info.platform != XE_ALDERLAKE_P)
		return 0;

	err = guc_hwconfig_size(guc, &size);
	if (err)
		return err;
	if (!size)
		return -EINVAL;

	bo = xe_managed_bo_create_pin_map(xe, tile, PAGE_ALIGN(size),
					  XE_BO_FLAG_SYSTEM |
					  XE_BO_FLAG_GGTT |
					  XE_BO_FLAG_GGTT_INVALIDATE);
	if (IS_ERR(bo))
		return PTR_ERR(bo);
	guc->hwconfig.bo = bo;
	guc->hwconfig.size = size;

	return guc_hwconfig_copy(guc);
}

u32 xe_guc_hwconfig_size(struct xe_guc *guc)
{
	return !guc->hwconfig.bo ? 0 : guc->hwconfig.size;
}

void xe_guc_hwconfig_copy(struct xe_guc *guc, void *dst)
{
	struct xe_device *xe = guc_to_xe(guc);

	XE_WARN_ON(!guc->hwconfig.bo);

	xe_map_memcpy_from(xe, dst, &guc->hwconfig.bo->vmap, 0,
			   guc->hwconfig.size);
}

void xe_guc_hwconfig_dump(struct xe_guc *guc, struct drm_printer *p)
{
	size_t size = xe_guc_hwconfig_size(guc);
	u32 *hwconfig;
	u64 num_dw;
	u32 extra_bytes;
	int i = 0;

	if (size == 0) {
		drm_printf(p, "No hwconfig available\n");
		return;
	}

	num_dw = div_u64_rem(size, sizeof(u32), &extra_bytes);

	hwconfig = kzalloc(size, GFP_KERNEL);
	if (!hwconfig) {
		drm_printf(p, "Error: could not allocate hwconfig memory\n");
		return;
	}

	xe_guc_hwconfig_copy(guc, hwconfig);

	/* An entry requires at least three dwords for key, length, value */
	while (i + 3 <= num_dw) {
		u32 attribute = hwconfig[i++];
		u32 len_dw = hwconfig[i++];

		if (i + len_dw > num_dw) {
			drm_printf(p, "Error: Attribute %u is %u dwords, but only %llu remain\n",
				   attribute, len_dw, num_dw - i);
			len_dw = num_dw - i;
		}

		/*
		 * If it's a single dword (as most hwconfig attributes are),
		 * then it's probably a number that makes sense to display
		 * in decimal form.  In the rare cases where it's more than
		 * one dword, just print it in hex form and let the user
		 * figure out how to interpret it.
		 */
		if (len_dw == 1)
			drm_printf(p, "[%2u] = %u\n", attribute, hwconfig[i]);
		else
			drm_printf(p, "[%2u] = { %*ph }\n", attribute,
				   (int)(len_dw * sizeof(u32)), &hwconfig[i]);
		i += len_dw;
	}

	if (i < num_dw || extra_bytes)
		drm_printf(p, "Error: %llu extra bytes at end of hwconfig\n",
			   (num_dw - i) * sizeof(u32) + extra_bytes);

	kfree(hwconfig);
}

/*
 * Lookup a specific 32-bit attribute value in the GuC's hwconfig table.
 */
int xe_guc_hwconfig_lookup_u32(struct xe_guc *guc, u32 attribute, u32 *val)
{
	size_t size = xe_guc_hwconfig_size(guc);
	u64 num_dw = div_u64(size, sizeof(u32));
	u32 *hwconfig;
	bool found = false;
	int i = 0;

	if (num_dw == 0)
		return -EINVAL;

	hwconfig = kzalloc(size, GFP_KERNEL);
	if (!hwconfig)
		return -ENOMEM;

	xe_guc_hwconfig_copy(guc, hwconfig);

	/* An entry requires at least three dwords for key, length, value */
	while (i + 3 <= num_dw) {
		u32 key = hwconfig[i++];
		u32 len_dw = hwconfig[i++];

		if (key != attribute) {
			i += len_dw;
			continue;
		}

		*val = hwconfig[i];
		found = true;
		break;
	}

	kfree(hwconfig);

	return found ? 0 : -ENOENT;
}

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_guc.h"
#include "xe_guc_hwconfig.h"
#include "xe_map.h"

static struct xe_gt *
guc_to_gt(struct xe_guc *guc)
{
	return container_of(guc, struct xe_gt, uc.guc);
}

static struct xe_device *
guc_to_xe(struct xe_guc *guc)
{
	return gt_to_xe(guc_to_gt(guc));
}

static int send_get_hwconfig(struct xe_guc *guc, u32 ggtt_addr, u32 size)
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

static void guc_hwconfig_fini(struct drm_device *drm, void *arg)
{
	struct xe_guc *guc = arg;

	xe_bo_unpin_map_no_vm(guc->hwconfig.bo);
}

int xe_guc_hwconfig_init(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
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

	bo = xe_bo_create_pin_map(xe, gt, NULL, PAGE_ALIGN(size),
				  ttm_bo_type_kernel,
				  XE_BO_CREATE_VRAM_IF_DGFX(gt) |
				  XE_BO_CREATE_GGTT_BIT);
	if (IS_ERR(bo))
		return PTR_ERR(bo);
	guc->hwconfig.bo = bo;
	guc->hwconfig.size = size;

	err = drmm_add_action_or_reset(&xe->drm, guc_hwconfig_fini, guc);
	if (err)
		return err;

	return guc_hwconfig_copy(guc);
}

u32 xe_guc_hwconfig_size(struct xe_guc *guc)
{
	return !guc->hwconfig.bo ? 0 : guc->hwconfig.size;
}

void xe_guc_hwconfig_copy(struct xe_guc *guc, void *dst)
{
	struct xe_device *xe = guc_to_xe(guc);

	XE_BUG_ON(!guc->hwconfig.bo);

	xe_map_memcpy_from(xe, dst, &guc->hwconfig.bo->vmap, 0,
			   guc->hwconfig.size);
}

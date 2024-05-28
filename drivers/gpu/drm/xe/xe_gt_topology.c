// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt_topology.h"

#include <linux/bitmap.h>

#include "regs/xe_gt_regs.h"
#include "xe_gt.h"
#include "xe_mmio.h"

#define XE_MAX_DSS_FUSE_BITS (32 * XE_MAX_DSS_FUSE_REGS)
#define XE_MAX_EU_FUSE_BITS (32 * XE_MAX_EU_FUSE_REGS)

static void
load_dss_mask(struct xe_gt *gt, xe_dss_mask_t mask, int numregs, ...)
{
	va_list argp;
	u32 fuse_val[XE_MAX_DSS_FUSE_REGS] = {};
	int i;

	if (drm_WARN_ON(&gt_to_xe(gt)->drm, numregs > XE_MAX_DSS_FUSE_REGS))
		numregs = XE_MAX_DSS_FUSE_REGS;

	va_start(argp, numregs);
	for (i = 0; i < numregs; i++)
		fuse_val[i] = xe_mmio_read32(gt, va_arg(argp, struct xe_reg));
	va_end(argp);

	bitmap_from_arr32(mask, fuse_val, numregs * 32);
}

static void
load_eu_mask(struct xe_gt *gt, xe_eu_mask_t mask)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 reg_val = xe_mmio_read32(gt, XELP_EU_ENABLE);
	u32 val = 0;
	int i;

	BUILD_BUG_ON(XE_MAX_EU_FUSE_REGS > 1);

	/*
	 * Pre-Xe_HP platforms inverted the bit meaning (disable instead
	 * of enable).
	 */
	if (GRAPHICS_VERx100(xe) < 1250)
		reg_val = ~reg_val & XELP_EU_MASK;

	/* On PVC, one bit = one EU */
	if (GRAPHICS_VERx100(xe) == 1260) {
		val = reg_val;
	} else {
		/* All other platforms, one bit = 2 EU */
		for (i = 0; i < fls(reg_val); i++)
			if (reg_val & BIT(i))
				val |= 0x3 << 2 * i;
	}

	bitmap_from_arr32(mask, &val, XE_MAX_EU_FUSE_BITS);
}

static void
get_num_dss_regs(struct xe_device *xe, int *geometry_regs, int *compute_regs)
{
	if (GRAPHICS_VER(xe) > 20) {
		*geometry_regs = 3;
		*compute_regs = 3;
	} else if (GRAPHICS_VERx100(xe) == 1260) {
		*geometry_regs = 0;
		*compute_regs = 2;
	} else if (GRAPHICS_VERx100(xe) >= 1250) {
		*geometry_regs = 1;
		*compute_regs = 1;
	} else {
		*geometry_regs = 1;
		*compute_regs = 0;
	}
}

void
xe_gt_topology_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct drm_printer p;
	int num_geometry_regs, num_compute_regs;

	get_num_dss_regs(xe, &num_geometry_regs, &num_compute_regs);

	/*
	 * Register counts returned shouldn't exceed the number of registers
	 * passed as parameters below.
	 */
	drm_WARN_ON(&xe->drm, num_geometry_regs > 3);
	drm_WARN_ON(&xe->drm, num_compute_regs > 3);

	load_dss_mask(gt, gt->fuse_topo.g_dss_mask,
		      num_geometry_regs,
		      XELP_GT_GEOMETRY_DSS_ENABLE,
		      XE2_GT_GEOMETRY_DSS_1,
		      XE2_GT_GEOMETRY_DSS_2);
	load_dss_mask(gt, gt->fuse_topo.c_dss_mask, num_compute_regs,
		      XEHP_GT_COMPUTE_DSS_ENABLE,
		      XEHPC_GT_COMPUTE_DSS_ENABLE_EXT,
		      XE2_GT_COMPUTE_DSS_2);
	load_eu_mask(gt, gt->fuse_topo.eu_mask_per_dss);

	p = drm_dbg_printer(&gt_to_xe(gt)->drm, DRM_UT_DRIVER, "GT topology");

	xe_gt_topology_dump(gt, &p);
}

void
xe_gt_topology_dump(struct xe_gt *gt, struct drm_printer *p)
{
	drm_printf(p, "dss mask (geometry): %*pb\n", XE_MAX_DSS_FUSE_BITS,
		   gt->fuse_topo.g_dss_mask);
	drm_printf(p, "dss mask (compute):  %*pb\n", XE_MAX_DSS_FUSE_BITS,
		   gt->fuse_topo.c_dss_mask);

	drm_printf(p, "EU mask per DSS:     %*pb\n", XE_MAX_EU_FUSE_BITS,
		   gt->fuse_topo.eu_mask_per_dss);

}

/*
 * Used to obtain the index of the first DSS.  Can start searching from the
 * beginning of a specific dss group (e.g., gslice, cslice, etc.) if
 * groupsize and groupnum are non-zero.
 */
unsigned int
xe_dss_mask_group_ffs(const xe_dss_mask_t mask, int groupsize, int groupnum)
{
	return find_next_bit(mask, XE_MAX_DSS_FUSE_BITS, groupnum * groupsize);
}

bool xe_dss_mask_empty(const xe_dss_mask_t mask)
{
	return bitmap_empty(mask, XE_MAX_DSS_FUSE_BITS);
}

/**
 * xe_gt_topology_has_dss_in_quadrant - check fusing of DSS in GT quadrant
 * @gt: GT to check
 * @quad: Which quadrant of the DSS space to check
 *
 * Since Xe_HP platforms can have up to four CCS engines, those engines
 * are each logically associated with a quarter of the possible DSS.  If there
 * are no DSS present in one of the four quadrants of the DSS space, the
 * corresponding CCS engine is also not available for use.
 *
 * Returns false if all DSS in a quadrant of the GT are fused off, else true.
 */
bool xe_gt_topology_has_dss_in_quadrant(struct xe_gt *gt, int quad)
{
	struct xe_device *xe = gt_to_xe(gt);
	xe_dss_mask_t all_dss;
	int g_dss_regs, c_dss_regs, dss_per_quad, quad_first;

	bitmap_or(all_dss, gt->fuse_topo.g_dss_mask, gt->fuse_topo.c_dss_mask,
		  XE_MAX_DSS_FUSE_BITS);

	get_num_dss_regs(xe, &g_dss_regs, &c_dss_regs);
	dss_per_quad = 32 * max(g_dss_regs, c_dss_regs) / 4;

	quad_first = xe_dss_mask_group_ffs(all_dss, dss_per_quad, quad);

	return quad_first < (quad + 1) * dss_per_quad;
}

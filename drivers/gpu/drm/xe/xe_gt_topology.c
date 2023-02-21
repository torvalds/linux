// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/bitmap.h>

#include "xe_gt.h"
#include "xe_gt_topology.h"
#include "xe_mmio.h"

#define XE_MAX_DSS_FUSE_BITS (32 * XE_MAX_DSS_FUSE_REGS)
#define XE_MAX_EU_FUSE_BITS (32 * XE_MAX_EU_FUSE_REGS)

#define XELP_EU_ENABLE				0x9134	/* "_DISABLE" on Xe_LP */
#define   XELP_EU_MASK				REG_GENMASK(7, 0)
#define XELP_GT_GEOMETRY_DSS_ENABLE		0x913c
#define XEHP_GT_COMPUTE_DSS_ENABLE		0x9144
#define XEHPC_GT_COMPUTE_DSS_ENABLE_EXT		0x9148

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
		fuse_val[i] = xe_mmio_read32(gt, va_arg(argp, u32));
	va_end(argp);

	bitmap_from_arr32(mask, fuse_val, numregs * 32);
}

static void
load_eu_mask(struct xe_gt *gt, xe_eu_mask_t mask)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 reg = xe_mmio_read32(gt, XELP_EU_ENABLE);
	u32 val = 0;
	int i;

	BUILD_BUG_ON(XE_MAX_EU_FUSE_REGS > 1);

	/*
	 * Pre-Xe_HP platforms inverted the bit meaning (disable instead
	 * of enable).
	 */
	if (GRAPHICS_VERx100(xe) < 1250)
		reg = ~reg & XELP_EU_MASK;

	/* On PVC, one bit = one EU */
	if (GRAPHICS_VERx100(xe) == 1260) {
		val = reg;
	} else {
		/* All other platforms, one bit = 2 EU */
		for (i = 0; i < fls(reg); i++)
			if (reg & BIT(i))
				val |= 0x3 << 2 * i;
	}

	bitmap_from_arr32(mask, &val, XE_MAX_EU_FUSE_BITS);
}

void
xe_gt_topology_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct drm_printer p = drm_debug_printer("GT topology");
	int num_geometry_regs, num_compute_regs;

	if (GRAPHICS_VERx100(xe) == 1260) {
		num_geometry_regs = 0;
		num_compute_regs = 2;
	} else if (GRAPHICS_VERx100(xe) >= 1250) {
		num_geometry_regs = 1;
		num_compute_regs = 1;
	} else {
		num_geometry_regs = 1;
		num_compute_regs = 0;
	}

	load_dss_mask(gt, gt->fuse_topo.g_dss_mask, num_geometry_regs,
		      XELP_GT_GEOMETRY_DSS_ENABLE);
	load_dss_mask(gt, gt->fuse_topo.c_dss_mask, num_compute_regs,
		      XEHP_GT_COMPUTE_DSS_ENABLE,
		      XEHPC_GT_COMPUTE_DSS_ENABLE_EXT);
	load_eu_mask(gt, gt->fuse_topo.eu_mask_per_dss);

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
xe_dss_mask_group_ffs(xe_dss_mask_t mask, int groupsize, int groupnum)
{
	return find_next_bit(mask, XE_MAX_DSS_FUSE_BITS, groupnum * groupsize);
}

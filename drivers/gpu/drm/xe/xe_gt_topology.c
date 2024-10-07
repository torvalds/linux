// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt_topology.h"

#include <generated/xe_wa_oob.h>
#include <linux/bitmap.h>
#include <linux/compiler.h>

#include "regs/xe_gt_regs.h"
#include "xe_assert.h"
#include "xe_gt.h"
#include "xe_mmio.h"
#include "xe_wa.h"

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
load_eu_mask(struct xe_gt *gt, xe_eu_mask_t mask, enum xe_gt_eu_type *eu_type)
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

	if (GRAPHICS_VERx100(xe) == 1260 || GRAPHICS_VER(xe) >= 20) {
		/* SIMD16 EUs, one bit == one EU */
		*eu_type = XE_GT_EU_TYPE_SIMD16;
		val = reg_val;
	} else {
		/* SIMD8 EUs, one bit == 2 EU */
		*eu_type = XE_GT_EU_TYPE_SIMD8;
		for (i = 0; i < fls(reg_val); i++)
			if (reg_val & BIT(i))
				val |= 0x3 << 2 * i;
	}

	bitmap_from_arr32(mask, &val, XE_MAX_EU_FUSE_BITS);
}

/**
 * gen_l3_mask_from_pattern - Replicate a bit pattern according to a mask
 *
 * It is used to compute the L3 bank masks in a generic format on
 * various platforms where the internal representation of L3 node
 * and masks from registers are different.
 *
 * @xe: device
 * @dst: destination
 * @pattern: pattern to replicate
 * @patternbits: size of the pattern, in bits
 * @mask: mask describing where to replicate the pattern
 *
 * Example 1:
 * ----------
 * @pattern =    0b1111
 *                 └┬─┘
 * @patternbits =   4 (bits)
 * @mask = 0b0101
 *           ││││
 *           │││└────────────────── 0b1111 (=1×0b1111)
 *           ││└──────────── 0b0000    │   (=0×0b1111)
 *           │└────── 0b1111    │      │   (=1×0b1111)
 *           └ 0b0000    │      │      │   (=0×0b1111)
 *                │      │      │      │
 * @dst =      0b0000 0b1111 0b0000 0b1111
 *
 * Example 2:
 * ----------
 * @pattern =    0b11111111
 *                 └┬─────┘
 * @patternbits =   8 (bits)
 * @mask = 0b10
 *           ││
 *           ││
 *           ││
 *           │└────────── 0b00000000 (=0×0b11111111)
 *           └ 0b11111111      │     (=1×0b11111111)
 *                  │          │
 * @dst =      0b11111111 0b00000000
 */
static void
gen_l3_mask_from_pattern(struct xe_device *xe, xe_l3_bank_mask_t dst,
			 xe_l3_bank_mask_t pattern, int patternbits,
			 unsigned long mask)
{
	unsigned long bit;

	xe_assert(xe, find_last_bit(pattern, XE_MAX_L3_BANK_MASK_BITS) < patternbits ||
		  bitmap_empty(pattern, XE_MAX_L3_BANK_MASK_BITS));
	xe_assert(xe, !mask || patternbits * (__fls(mask) + 1) <= XE_MAX_L3_BANK_MASK_BITS);
	for_each_set_bit(bit, &mask, 32) {
		xe_l3_bank_mask_t shifted_pattern = {};

		bitmap_shift_left(shifted_pattern, pattern, bit * patternbits,
				  XE_MAX_L3_BANK_MASK_BITS);
		bitmap_or(dst, dst, shifted_pattern, XE_MAX_L3_BANK_MASK_BITS);
	}
}

static void
load_l3_bank_mask(struct xe_gt *gt, xe_l3_bank_mask_t l3_bank_mask)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 fuse3 = xe_mmio_read32(gt, MIRROR_FUSE3);

	/*
	 * PTL platforms with media version 30.00 do not provide proper values
	 * for the media GT's L3 bank registers.  Skip the readout since we
	 * don't have any way to obtain real values.
	 *
	 * This may get re-described as an official workaround in the future,
	 * but there's no tracking number assigned yet so we use a custom
	 * OOB workaround descriptor.
	 */
	if (XE_WA(gt, no_media_l3))
		return;

	if (GRAPHICS_VER(xe) >= 20) {
		xe_l3_bank_mask_t per_node = {};
		u32 meml3_en = REG_FIELD_GET(XE2_NODE_ENABLE_MASK, fuse3);
		u32 bank_val = REG_FIELD_GET(XE2_GT_L3_MODE_MASK, fuse3);

		bitmap_from_arr32(per_node, &bank_val, 32);
		gen_l3_mask_from_pattern(xe, l3_bank_mask, per_node, 4,
					 meml3_en);
	} else if (GRAPHICS_VERx100(xe) >= 1270) {
		xe_l3_bank_mask_t per_node = {};
		xe_l3_bank_mask_t per_mask_bit = {};
		u32 meml3_en = REG_FIELD_GET(MEML3_EN_MASK, fuse3);
		u32 fuse4 = xe_mmio_read32(gt, XEHP_FUSE4);
		u32 bank_val = REG_FIELD_GET(GT_L3_EXC_MASK, fuse4);

		bitmap_set_value8(per_mask_bit, 0x3, 0);
		gen_l3_mask_from_pattern(xe, per_node, per_mask_bit, 2, bank_val);
		gen_l3_mask_from_pattern(xe, l3_bank_mask, per_node, 4,
					 meml3_en);
	} else if (xe->info.platform == XE_PVC) {
		xe_l3_bank_mask_t per_node = {};
		xe_l3_bank_mask_t per_mask_bit = {};
		u32 meml3_en = REG_FIELD_GET(MEML3_EN_MASK, fuse3);
		u32 bank_val = REG_FIELD_GET(XEHPC_GT_L3_MODE_MASK, fuse3);

		bitmap_set_value8(per_mask_bit, 0xf, 0);
		gen_l3_mask_from_pattern(xe, per_node, per_mask_bit, 4,
					 bank_val);
		gen_l3_mask_from_pattern(xe, l3_bank_mask, per_node, 16,
					 meml3_en);
	} else if (xe->info.platform == XE_DG2) {
		xe_l3_bank_mask_t per_node = {};
		u32 mask = REG_FIELD_GET(MEML3_EN_MASK, fuse3);

		bitmap_set_value8(per_node, 0xff, 0);
		gen_l3_mask_from_pattern(xe, l3_bank_mask, per_node, 8, mask);
	} else {
		/* 1:1 register bit to mask bit (inverted register bits) */
		u32 mask = REG_FIELD_GET(XELP_GT_L3_MODE_MASK, ~fuse3);

		bitmap_from_arr32(l3_bank_mask, &mask, 32);
	}
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
	load_eu_mask(gt, gt->fuse_topo.eu_mask_per_dss, &gt->fuse_topo.eu_type);
	load_l3_bank_mask(gt, gt->fuse_topo.l3_bank_mask);

	p = drm_dbg_printer(&gt_to_xe(gt)->drm, DRM_UT_DRIVER, "GT topology");

	xe_gt_topology_dump(gt, &p);
}

static const char *eu_type_to_str(enum xe_gt_eu_type eu_type)
{
	switch (eu_type) {
	case XE_GT_EU_TYPE_SIMD16:
		return "simd16";
	case XE_GT_EU_TYPE_SIMD8:
		return "simd8";
	}

	return NULL;
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
	drm_printf(p, "EU type:             %s\n",
		   eu_type_to_str(gt->fuse_topo.eu_type));

	drm_printf(p, "L3 bank mask:        %*pb\n", XE_MAX_L3_BANK_MASK_BITS,
		   gt->fuse_topo.l3_bank_mask);
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

bool xe_gt_has_geometry_dss(struct xe_gt *gt, unsigned int dss)
{
	return test_bit(dss, gt->fuse_topo.g_dss_mask);
}

bool xe_gt_has_compute_dss(struct xe_gt *gt, unsigned int dss)
{
	return test_bit(dss, gt->fuse_topo.c_dss_mask);
}

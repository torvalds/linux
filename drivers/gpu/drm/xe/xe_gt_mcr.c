// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_gt_topology.h"
#include "xe_gt_types.h"
#include "xe_mmio.h"

#include "gt/intel_gt_regs.h"

/**
 * DOC: GT Multicast/Replicated (MCR) Register Support
 *
 * Some GT registers are designed as "multicast" or "replicated" registers:
 * multiple instances of the same register share a single MMIO offset.  MCR
 * registers are generally used when the hardware needs to potentially track
 * independent values of a register per hardware unit (e.g., per-subslice,
 * per-L3bank, etc.).  The specific types of replication that exist vary
 * per-platform.
 *
 * MMIO accesses to MCR registers are controlled according to the settings
 * programmed in the platform's MCR_SELECTOR register(s).  MMIO writes to MCR
 * registers can be done in either a (i.e., a single write updates all
 * instances of the register to the same value) or unicast (a write updates only
 * one specific instance).  Reads of MCR registers always operate in a unicast
 * manner regardless of how the multicast/unicast bit is set in MCR_SELECTOR.
 * Selection of a specific MCR instance for unicast operations is referred to
 * as "steering."
 *
 * If MCR register operations are steered toward a hardware unit that is
 * fused off or currently powered down due to power gating, the MMIO operation
 * is "terminated" by the hardware.  Terminated read operations will return a
 * value of zero and terminated unicast write operations will be silently
 * ignored.
 */

enum {
	MCR_OP_READ,
	MCR_OP_WRITE
};

static const struct xe_mmio_range xelp_l3bank_steering_table[] = {
	{ 0x00B100, 0x00B3FF },
	{},
};

/*
 * Although the bspec lists more "MSLICE" ranges than shown here, some of those
 * are of a "GAM" subclass that has special rules and doesn't need to be
 * included here.
 */
static const struct xe_mmio_range xehp_mslice_steering_table[] = {
	{ 0x00DD00, 0x00DDFF },
	{ 0x00E900, 0x00FFFF }, /* 0xEA00 - OxEFFF is unused */
	{},
};

static const struct xe_mmio_range xehp_lncf_steering_table[] = {
	{ 0x00B000, 0x00B0FF },
	{ 0x00D880, 0x00D8FF },
	{},
};

/*
 * We have several types of MCR registers where steering to (0,0) will always
 * provide us with a non-terminated value.  We'll stick them all in the same
 * table for simplicity.
 */
static const struct xe_mmio_range xehpc_instance0_steering_table[] = {
	{ 0x004000, 0x004AFF },		/* HALF-BSLICE */
	{ 0x008800, 0x00887F },		/* CC */
	{ 0x008A80, 0x008AFF },		/* TILEPSMI */
	{ 0x00B000, 0x00B0FF },		/* HALF-BSLICE */
	{ 0x00B100, 0x00B3FF },		/* L3BANK */
	{ 0x00C800, 0x00CFFF },		/* HALF-BSLICE */
	{ 0x00D800, 0x00D8FF },		/* HALF-BSLICE */
	{ 0x00DD00, 0x00DDFF },		/* BSLICE */
	{ 0x00E900, 0x00E9FF },		/* HALF-BSLICE */
	{ 0x00EC00, 0x00EEFF },		/* HALF-BSLICE */
	{ 0x00F000, 0x00FFFF },		/* HALF-BSLICE */
	{ 0x024180, 0x0241FF },		/* HALF-BSLICE */
	{},
};

static const struct xe_mmio_range xelpg_instance0_steering_table[] = {
	{ 0x000B00, 0x000BFF },         /* SQIDI */
	{ 0x001000, 0x001FFF },         /* SQIDI */
	{ 0x004000, 0x0048FF },         /* GAM */
	{ 0x008700, 0x0087FF },         /* SQIDI */
	{ 0x00B000, 0x00B0FF },         /* NODE */
	{ 0x00C800, 0x00CFFF },         /* GAM */
	{ 0x00D880, 0x00D8FF },         /* NODE */
	{ 0x00DD00, 0x00DDFF },         /* OAAL2 */
	{},
};

static const struct xe_mmio_range xelpg_l3bank_steering_table[] = {
	{ 0x00B100, 0x00B3FF },
	{},
};

static const struct xe_mmio_range xelp_dss_steering_table[] = {
	{ 0x008150, 0x00815F },
	{ 0x009520, 0x00955F },
	{ 0x00DE80, 0x00E8FF },
	{ 0x024A00, 0x024A7F },
	{},
};

/* DSS steering is used for GSLICE ranges as well */
static const struct xe_mmio_range xehp_dss_steering_table[] = {
	{ 0x005200, 0x0052FF },		/* GSLICE */
	{ 0x005400, 0x007FFF },		/* GSLICE */
	{ 0x008140, 0x00815F },		/* GSLICE (0x8140-0x814F), DSS (0x8150-0x815F) */
	{ 0x008D00, 0x008DFF },		/* DSS */
	{ 0x0094D0, 0x00955F },		/* GSLICE (0x94D0-0x951F), DSS (0x9520-0x955F) */
	{ 0x009680, 0x0096FF },		/* DSS */
	{ 0x00D800, 0x00D87F },		/* GSLICE */
	{ 0x00DC00, 0x00DCFF },		/* GSLICE */
	{ 0x00DE80, 0x00E8FF },		/* DSS (0xE000-0xE0FF reserved ) */
	{ 0x017000, 0x017FFF },		/* GSLICE */
	{ 0x024A00, 0x024A7F },		/* DSS */
	{},
};

/* DSS steering is used for COMPUTE ranges as well */
static const struct xe_mmio_range xehpc_dss_steering_table[] = {
	{ 0x008140, 0x00817F },		/* COMPUTE (0x8140-0x814F & 0x8160-0x817F), DSS (0x8150-0x815F) */
	{ 0x0094D0, 0x00955F },		/* COMPUTE (0x94D0-0x951F), DSS (0x9520-0x955F) */
	{ 0x009680, 0x0096FF },		/* DSS */
	{ 0x00DC00, 0x00DCFF },		/* COMPUTE */
	{ 0x00DE80, 0x00E7FF },		/* DSS (0xDF00-0xE1FF reserved ) */
	{},
};

/* DSS steering is used for SLICE ranges as well */
static const struct xe_mmio_range xelpg_dss_steering_table[] = {
	{ 0x005200, 0x0052FF },		/* SLICE */
	{ 0x005500, 0x007FFF },		/* SLICE */
	{ 0x008140, 0x00815F },		/* SLICE (0x8140-0x814F), DSS (0x8150-0x815F) */
	{ 0x0094D0, 0x00955F },		/* SLICE (0x94D0-0x951F), DSS (0x9520-0x955F) */
	{ 0x009680, 0x0096FF },		/* DSS */
	{ 0x00D800, 0x00D87F },		/* SLICE */
	{ 0x00DC00, 0x00DCFF },		/* SLICE */
	{ 0x00DE80, 0x00E8FF },		/* DSS (0xE000-0xE0FF reserved) */
	{},
};

static const struct xe_mmio_range xelpmp_oaddrm_steering_table[] = {
	{ 0x393200, 0x39323F },
	{ 0x393400, 0x3934FF },
	{},
};

/*
 * DG2 GAM registers are a special case; this table is checked directly in
 * xe_gt_mcr_get_nonterminated_steering and is not hooked up via
 * gt->steering[].
 */
static const struct xe_mmio_range dg2_gam_ranges[] = {
	{ 0x004000, 0x004AFF },
	{ 0x00C800, 0x00CFFF },
	{ 0x00F000, 0x00FFFF },
	{},
};

static void init_steering_l3bank(struct xe_gt *gt)
{
	if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1270) {
		u32 mslice_mask = REG_FIELD_GET(GEN12_MEML3_EN_MASK,
						xe_mmio_read32(gt, GEN10_MIRROR_FUSE3.reg));
		u32 bank_mask = REG_FIELD_GET(GT_L3_EXC_MASK,
					      xe_mmio_read32(gt, XEHP_FUSE4.reg));

		/*
		 * Group selects mslice, instance selects bank within mslice.
		 * Bank 0 is always valid _except_ when the bank mask is 010b.
		 */
		gt->steering[L3BANK].group_target = __ffs(mslice_mask);
		gt->steering[L3BANK].instance_target =
			bank_mask & BIT(0) ? 0 : 2;
	} else {
		u32 fuse = REG_FIELD_GET(GEN10_L3BANK_MASK,
					 ~xe_mmio_read32(gt, GEN10_MIRROR_FUSE3.reg));

		gt->steering[L3BANK].group_target = 0;	/* unused */
		gt->steering[L3BANK].instance_target = __ffs(fuse);
	}
}

static void init_steering_mslice(struct xe_gt *gt)
{
	u32 mask = REG_FIELD_GET(GEN12_MEML3_EN_MASK,
				 xe_mmio_read32(gt, GEN10_MIRROR_FUSE3.reg));

	/*
	 * mslice registers are valid (not terminated) if either the meml3
	 * associated with the mslice is present, or at least one DSS associated
	 * with the mslice is present.  There will always be at least one meml3
	 * so we can just use that to find a non-terminated mslice and ignore
	 * the DSS fusing.
	 */
	gt->steering[MSLICE].group_target = __ffs(mask);
	gt->steering[MSLICE].instance_target = 0;	/* unused */

	/*
	 * LNCF termination is also based on mslice presence, so we'll set
	 * it up here.  Either LNCF within a non-terminated mslice will work,
	 * so we just always pick LNCF 0 here.
	 */
	gt->steering[LNCF].group_target = __ffs(mask) << 1;
	gt->steering[LNCF].instance_target = 0;		/* unused */
}

static void init_steering_dss(struct xe_gt *gt)
{
	unsigned int dss = min(xe_dss_mask_group_ffs(gt->fuse_topo.g_dss_mask, 0, 0),
			       xe_dss_mask_group_ffs(gt->fuse_topo.c_dss_mask, 0, 0));
	unsigned int dss_per_grp = gt_to_xe(gt)->info.platform == XE_PVC ? 8 : 4;

	gt->steering[DSS].group_target = dss / dss_per_grp;
	gt->steering[DSS].instance_target = dss % dss_per_grp;
}

static void init_steering_oaddrm(struct xe_gt *gt)
{
	/*
	 * First instance is only terminated if the entire first media slice
	 * is absent (i.e., no VCS0 or VECS0).
	 */
	if (gt->info.engine_mask & (XE_HW_ENGINE_VCS0 | XE_HW_ENGINE_VECS0))
		gt->steering[OADDRM].group_target = 0;
	else
		gt->steering[OADDRM].group_target = 1;

	gt->steering[DSS].instance_target = 0;		/* unused */
}

static void init_steering_inst0(struct xe_gt *gt)
{
	gt->steering[DSS].group_target = 0;		/* unused */
	gt->steering[DSS].instance_target = 0;		/* unused */
}

static const struct {
	const char *name;
	void (*init)(struct xe_gt *);
} xe_steering_types[] = {
	{ "L3BANK",	init_steering_l3bank },
	{ "MSLICE",	init_steering_mslice },
	{ "LNCF",	NULL },		/* initialized by mslice init */
	{ "DSS",	init_steering_dss },
	{ "OADDRM",	init_steering_oaddrm },
	{ "INSTANCE 0",	init_steering_inst0 },
};

void xe_gt_mcr_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	BUILD_BUG_ON(ARRAY_SIZE(xe_steering_types) != NUM_STEERING_TYPES);

	spin_lock_init(&gt->mcr_lock);

	if (gt->info.type == XE_GT_TYPE_MEDIA) {
		drm_WARN_ON(&xe->drm, MEDIA_VER(xe) < 13);

		gt->steering[OADDRM].ranges = xelpmp_oaddrm_steering_table;
	} else if (GRAPHICS_VERx100(xe) >= 1270) {
		gt->steering[INSTANCE0].ranges = xelpg_instance0_steering_table;
		gt->steering[L3BANK].ranges = xelpg_l3bank_steering_table;
		gt->steering[DSS].ranges = xelpg_dss_steering_table;
	} else if (xe->info.platform == XE_PVC) {
		gt->steering[INSTANCE0].ranges = xehpc_instance0_steering_table;
		gt->steering[DSS].ranges = xehpc_dss_steering_table;
	} else if (xe->info.platform == XE_DG2) {
		gt->steering[MSLICE].ranges = xehp_mslice_steering_table;
		gt->steering[LNCF].ranges = xehp_lncf_steering_table;
		gt->steering[DSS].ranges = xehp_dss_steering_table;
	} else {
		gt->steering[L3BANK].ranges = xelp_l3bank_steering_table;
		gt->steering[DSS].ranges = xelp_dss_steering_table;
	}

	/* Select non-terminated steering target for each type */
	for (int i = 0; i < NUM_STEERING_TYPES; i++)
		if (gt->steering[i].ranges && xe_steering_types[i].init)
			xe_steering_types[i].init(gt);
}

/*
 * xe_gt_mcr_get_nonterminated_steering - find group/instance values that
 *    will steer a register to a non-terminated instance
 * @gt: GT structure
 * @reg: register for which the steering is required
 * @group: return variable for group steering
 * @instance: return variable for instance steering
 *
 * This function returns a group/instance pair that is guaranteed to work for
 * read steering of the given register. Note that a value will be returned even
 * if the register is not replicated and therefore does not actually require
 * steering.
 *
 * Returns true if the caller should steer to the @group/@instance values
 * returned.  Returns false if the caller need not perform any steering (i.e.,
 * the DG2 GAM range special case).
 */
static bool xe_gt_mcr_get_nonterminated_steering(struct xe_gt *gt,
						 i915_mcr_reg_t reg,
						 u8 *group, u8 *instance)
{
	for (int type = 0; type < NUM_STEERING_TYPES; type++) {
		if (!gt->steering[type].ranges)
			continue;

		for (int i = 0; gt->steering[type].ranges[i].end > 0; i++) {
			if (xe_mmio_in_range(&gt->steering[type].ranges[i], reg.reg)) {
				*group = gt->steering[type].group_target;
				*instance = gt->steering[type].instance_target;
				return true;
			}
		}
	}

	/*
	 * All MCR registers should usually be part of one of the steering
	 * ranges we're tracking.  However there's one special case:  DG2
	 * GAM registers are technically multicast registers, but are special
	 * in a number of ways:
	 *  - they have their own dedicated steering control register (they
	 *    don't share 0xFDC with other MCR classes)
	 *  - all reads should be directed to instance 1 (unicast reads against
	 *    other instances are not allowed), and instance 1 is already the
	 *    the hardware's default steering target, which we never change
	 *
	 * Ultimately this means that we can just treat them as if they were
	 * unicast registers and all operations will work properly.
	 */
	for (int i = 0; dg2_gam_ranges[i].end > 0; i++)
		if (xe_mmio_in_range(&dg2_gam_ranges[i], reg.reg))
			return false;

	/*
	 * Not found in a steering table and not a DG2 GAM register?  We'll
	 * just steer to 0/0 as a guess and raise a warning.
	 */
	drm_WARN(&gt_to_xe(gt)->drm, true,
		 "Did not find MCR register %#x in any MCR steering table\n",
		 reg.reg);
	*group = 0;
	*instance = 0;

	return true;
}

#define STEER_SEMAPHORE		0xFD0

/*
 * Obtain exclusive access to MCR steering.  On MTL and beyond we also need
 * to synchronize with external clients (e.g., firmware), so a semaphore
 * register will also need to be taken.
 */
static void mcr_lock(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int ret;

	spin_lock(&gt->mcr_lock);

	/*
	 * Starting with MTL we also need to grab a semaphore register
	 * to synchronize with external agents (e.g., firmware) that now
	 * shares the same steering control register.
	 */
	if (GRAPHICS_VERx100(xe) >= 1270)
		ret = xe_mmio_wait32(gt, STEER_SEMAPHORE, 0, 0x1, 10, NULL,
				     false);

	drm_WARN_ON_ONCE(&xe->drm, ret == -ETIMEDOUT);
}

static void mcr_unlock(struct xe_gt *gt) {
	/* Release hardware semaphore */
	if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1270)
		xe_mmio_write32(gt, STEER_SEMAPHORE, 0x1);

	spin_unlock(&gt->mcr_lock);
}

/*
 * Access a register with specific MCR steering
 *
 * Caller needs to make sure the relevant forcewake wells are up.
 */
static u32 rw_with_mcr_steering(struct xe_gt *gt, i915_mcr_reg_t reg, u8 rw_flag,
				int group, int instance, u32 value)
{
	u32 steer_reg, steer_val, val = 0;

	lockdep_assert_held(&gt->mcr_lock);

	if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1270) {
		steer_reg = MTL_MCR_SELECTOR.reg;
		steer_val = REG_FIELD_PREP(MTL_MCR_GROUPID, group) |
			REG_FIELD_PREP(MTL_MCR_INSTANCEID, instance);
	} else {
		steer_reg = GEN8_MCR_SELECTOR.reg;
		steer_val = REG_FIELD_PREP(GEN11_MCR_SLICE_MASK, group) |
			REG_FIELD_PREP(GEN11_MCR_SUBSLICE_MASK, instance);
	}

	/*
	 * Always leave the hardware in multicast mode when doing reads
	 * (see comment about Wa_22013088509 below) and only change it
	 * to unicast mode when doing writes of a specific instance.
	 *
	 * No need to save old steering reg value.
	 */
	if (rw_flag == MCR_OP_READ)
		steer_val |= GEN11_MCR_MULTICAST;

	xe_mmio_write32(gt, steer_reg, steer_val);

	if (rw_flag == MCR_OP_READ)
		val = xe_mmio_read32(gt, reg.reg);
	else
		xe_mmio_write32(gt, reg.reg, value);

	/*
	 * If we turned off the multicast bit (during a write) we're required
	 * to turn it back on before finishing.  The group and instance values
	 * don't matter since they'll be re-programmed on the next MCR
	 * operation.
	 */
	if (rw_flag == MCR_OP_WRITE)
		xe_mmio_write32(gt, steer_reg, GEN11_MCR_MULTICAST);

	return val;
}

/**
 * xe_gt_mcr_unicast_read_any - reads a non-terminated instance of an MCR register
 * @gt: GT structure
 * @reg: register to read
 *
 * Reads a GT MCR register.  The read will be steered to a non-terminated
 * instance (i.e., one that isn't fused off or powered down by power gating).
 * This function assumes the caller is already holding any necessary forcewake
 * domains.
 *
 * Returns the value from a non-terminated instance of @reg.
 */
u32 xe_gt_mcr_unicast_read_any(struct xe_gt *gt, i915_mcr_reg_t reg)
{
	u8 group, instance;
	u32 val;
	bool steer;

	steer = xe_gt_mcr_get_nonterminated_steering(gt, reg, &group, &instance);

	if (steer) {
		mcr_lock(gt);
		val = rw_with_mcr_steering(gt, reg, MCR_OP_READ,
					   group, instance, 0);
		mcr_unlock(gt);
	} else {
		/* DG2 GAM special case rules; treat as if unicast */
		val = xe_mmio_read32(gt, reg.reg);
	}

	return val;
}

/**
 * xe_gt_mcr_unicast_read - read a specific instance of an MCR register
 * @gt: GT structure
 * @reg: the MCR register to read
 * @group: the MCR group
 * @instance: the MCR instance
 *
 * Returns the value read from an MCR register after steering toward a specific
 * group/instance.
 */
u32 xe_gt_mcr_unicast_read(struct xe_gt *gt,
			   i915_mcr_reg_t reg,
			   int group, int instance)
{
	u32 val;

	mcr_lock(gt);
	val = rw_with_mcr_steering(gt, reg, MCR_OP_READ, group, instance, 0);
	mcr_unlock(gt);

	return val;
}

/**
 * xe_gt_mcr_unicast_write - write a specific instance of an MCR register
 * @gt: GT structure
 * @reg: the MCR register to write
 * @value: value to write
 * @group: the MCR group
 * @instance: the MCR instance
 *
 * Write an MCR register in unicast mode after steering toward a specific
 * group/instance.
 */
void xe_gt_mcr_unicast_write(struct xe_gt *gt, i915_mcr_reg_t reg, u32 value,
			     int group, int instance)
{
	mcr_lock(gt);
	rw_with_mcr_steering(gt, reg, MCR_OP_WRITE, group, instance, value);
	mcr_unlock(gt);
}

/**
 * xe_gt_mcr_multicast_write - write a value to all instances of an MCR register
 * @gt: GT structure
 * @reg: the MCR register to write
 * @value: value to write
 *
 * Write an MCR register in multicast mode to update all instances.
 */
void xe_gt_mcr_multicast_write(struct xe_gt *gt, i915_mcr_reg_t reg, u32 value)
{
	/*
	 * Synchronize with any unicast operations.  Once we have exclusive
	 * access, the MULTICAST bit should already be set, so there's no need
	 * to touch the steering register.
	 */
	mcr_lock(gt);
	xe_mmio_write32(gt, reg.reg, value);
	mcr_unlock(gt);
}

void xe_gt_mcr_steering_dump(struct xe_gt *gt, struct drm_printer *p)
{
	for (int i = 0; i < NUM_STEERING_TYPES; i++) {
		if (gt->steering[i].ranges) {
			drm_printf(p, "%s steering: group=%#x, instance=%#x\n",
				   xe_steering_types[i].name,
				   gt->steering[i].group_target,
				   gt->steering[i].instance_target);
			for (int j = 0; gt->steering[i].ranges[j].end; j++)
				drm_printf(p, "\t0x%06x - 0x%06x\n",
					   gt->steering[i].ranges[j].start,
					   gt->steering[i].ranges[j].end);
		}
	}
}

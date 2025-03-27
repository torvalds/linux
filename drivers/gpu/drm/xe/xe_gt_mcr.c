// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt_mcr.h"

#include "regs/xe_gt_regs.h"
#include "xe_assert.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_gt_topology.h"
#include "xe_gt_types.h"
#include "xe_guc_hwconfig.h"
#include "xe_mmio.h"
#include "xe_sriov.h"

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
 * registers can be done in either multicast (a single write updates all
 * instances of the register to the same value) or unicast (a write updates only
 * one specific instance) form.  Reads of MCR registers always operate in a
 * unicast manner regardless of how the multicast/unicast bit is set in
 * MCR_SELECTOR.  Selection of a specific MCR instance for unicast operations is
 * referred to as "steering."
 *
 * If MCR register operations are steered toward a hardware unit that is
 * fused off or currently powered down due to power gating, the MMIO operation
 * is "terminated" by the hardware.  Terminated read operations will return a
 * value of zero and terminated unicast write operations will be silently
 * ignored. During device initialization, the goal of the various
 * ``init_steering_*()`` functions is to apply the platform-specific rules for
 * each MCR register type to identify a steering target that will select a
 * non-terminated instance.
 *
 * MCR registers are not available on Virtual Function (VF).
 */

#define STEER_SEMAPHORE		XE_REG(0xFD0)

static inline struct xe_reg to_xe_reg(struct xe_reg_mcr reg_mcr)
{
	return reg_mcr.__reg;
}

enum {
	MCR_OP_READ,
	MCR_OP_WRITE
};

static const struct xe_mmio_range xelp_l3bank_steering_table[] = {
	{ 0x00B100, 0x00B3FF },
	{},
};

static const struct xe_mmio_range xehp_l3bank_steering_table[] = {
	{ 0x008C80, 0x008CFF },
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

static const struct xe_mmio_range dg2_implicit_steering_table[] = {
	{ 0x000B00, 0x000BFF },		/* SF (SQIDI replication) */
	{ 0x001000, 0x001FFF },		/* SF (SQIDI replication) */
	{ 0x004000, 0x004AFF },		/* GAM (MSLICE replication) */
	{ 0x008700, 0x0087FF },		/* MCFG (SQIDI replication) */
	{ 0x00C800, 0x00CFFF },		/* GAM (MSLICE replication) */
	{ 0x00F000, 0x00FFFF },		/* GAM (MSLICE replication) */
	{},
};

static const struct xe_mmio_range xe2lpg_dss_steering_table[] = {
	{ 0x005200, 0x0052FF },         /* SLICE */
	{ 0x005500, 0x007FFF },         /* SLICE */
	{ 0x008140, 0x00815F },         /* SLICE (0x8140-0x814F), DSS (0x8150-0x815F) */
	{ 0x0094D0, 0x00955F },         /* SLICE (0x94D0-0x951F), DSS (0x9520-0x955F) */
	{ 0x009680, 0x0096FF },         /* DSS */
	{ 0x00D800, 0x00D87F },         /* SLICE */
	{ 0x00DC00, 0x00DCFF },         /* SLICE */
	{ 0x00DE80, 0x00E8FF },         /* DSS (0xE000-0xE0FF reserved) */
	{ 0x00E980, 0x00E9FF },         /* SLICE */
	{ 0x013000, 0x0133FF },         /* DSS (0x13000-0x131FF), SLICE (0x13200-0x133FF) */
	{},
};

static const struct xe_mmio_range xe2lpg_sqidi_psmi_steering_table[] = {
	{ 0x000B00, 0x000BFF },
	{ 0x001000, 0x001FFF },
	{},
};

static const struct xe_mmio_range xe2lpg_instance0_steering_table[] = {
	{ 0x004000, 0x004AFF },         /* GAM, rsvd, GAMWKR */
	{ 0x008700, 0x00887F },         /* SQIDI, MEMPIPE */
	{ 0x00B000, 0x00B3FF },         /* NODE, L3BANK */
	{ 0x00C800, 0x00CFFF },         /* GAM */
	{ 0x00D880, 0x00D8FF },         /* NODE */
	{ 0x00DD00, 0x00DDFF },         /* MEMPIPE */
	{ 0x00E900, 0x00E97F },         /* MEMPIPE */
	{ 0x00F000, 0x00FFFF },         /* GAM, GAMWKR */
	{ 0x013400, 0x0135FF },         /* MEMPIPE */
	{},
};

static const struct xe_mmio_range xe2lpm_gpmxmt_steering_table[] = {
	{ 0x388160, 0x38817F },
	{ 0x389480, 0x3894CF },
	{},
};

static const struct xe_mmio_range xe2lpm_instance0_steering_table[] = {
	{ 0x384000, 0x3847DF },         /* GAM, rsvd, GAM */
	{ 0x384900, 0x384AFF },         /* GAM */
	{ 0x389560, 0x3895FF },         /* MEDIAINF */
	{ 0x38B600, 0x38B8FF },         /* L3BANK */
	{ 0x38C800, 0x38D07F },         /* GAM, MEDIAINF */
	{ 0x38F000, 0x38F0FF },         /* GAM */
	{ 0x393C00, 0x393C7F },         /* MEDIAINF */
	{},
};

static const struct xe_mmio_range xe3lpm_instance0_steering_table[] = {
	{ 0x384000, 0x3847DF },         /* GAM, rsvd, GAM */
	{ 0x384900, 0x384AFF },         /* GAM */
	{ 0x389560, 0x3895FF },         /* MEDIAINF */
	{ 0x38B600, 0x38B8FF },         /* L3BANK */
	{ 0x38C800, 0x38D07F },         /* GAM, MEDIAINF */
	{ 0x38D0D0, 0x38F0FF },		/* MEDIAINF, GAM */
	{ 0x393C00, 0x393C7F },         /* MEDIAINF */
	{},
};

static void init_steering_l3bank(struct xe_gt *gt)
{
	struct xe_mmio *mmio = &gt->mmio;

	if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1270) {
		u32 mslice_mask = REG_FIELD_GET(MEML3_EN_MASK,
						xe_mmio_read32(mmio, MIRROR_FUSE3));
		u32 bank_mask = REG_FIELD_GET(GT_L3_EXC_MASK,
					      xe_mmio_read32(mmio, XEHP_FUSE4));

		/*
		 * Group selects mslice, instance selects bank within mslice.
		 * Bank 0 is always valid _except_ when the bank mask is 010b.
		 */
		gt->steering[L3BANK].group_target = __ffs(mslice_mask);
		gt->steering[L3BANK].instance_target =
			bank_mask & BIT(0) ? 0 : 2;
	} else if (gt_to_xe(gt)->info.platform == XE_DG2) {
		u32 mslice_mask = REG_FIELD_GET(MEML3_EN_MASK,
						xe_mmio_read32(mmio, MIRROR_FUSE3));
		u32 bank = __ffs(mslice_mask) * 8;

		/*
		 * Like mslice registers, look for a valid mslice and steer to
		 * the first L3BANK of that quad. Access to the Nth L3 bank is
		 * split between the first bits of group and instance
		 */
		gt->steering[L3BANK].group_target = (bank >> 2) & 0x7;
		gt->steering[L3BANK].instance_target = bank & 0x3;
	} else {
		u32 fuse = REG_FIELD_GET(L3BANK_MASK,
					 ~xe_mmio_read32(mmio, MIRROR_FUSE3));

		gt->steering[L3BANK].group_target = 0;	/* unused */
		gt->steering[L3BANK].instance_target = __ffs(fuse);
	}
}

static void init_steering_mslice(struct xe_gt *gt)
{
	u32 mask = REG_FIELD_GET(MEML3_EN_MASK,
				 xe_mmio_read32(&gt->mmio, MIRROR_FUSE3));

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

static unsigned int dss_per_group(struct xe_gt *gt)
{
	struct xe_guc *guc = &gt->uc.guc;
	u32 max_slices = 0, max_subslices = 0;
	int ret;

	/*
	 * Try to query the GuC's hwconfig table for the maximum number of
	 * slices and subslices.  These don't reflect the platform's actual
	 * slice/DSS counts, just the physical layout by which we should
	 * determine the steering targets.  On older platforms with older GuC
	 * firmware releases it's possible that these attributes may not be
	 * included in the table, so we can always fall back to the old
	 * hardcoded layouts.
	 */
#define HWCONFIG_ATTR_MAX_SLICES	1
#define HWCONFIG_ATTR_MAX_SUBSLICES	70

	ret = xe_guc_hwconfig_lookup_u32(guc, HWCONFIG_ATTR_MAX_SLICES,
					 &max_slices);
	if (ret < 0 || max_slices == 0)
		goto fallback;

	ret = xe_guc_hwconfig_lookup_u32(guc, HWCONFIG_ATTR_MAX_SUBSLICES,
					 &max_subslices);
	if (ret < 0 || max_subslices == 0)
		goto fallback;

	return DIV_ROUND_UP(max_subslices, max_slices);

fallback:
	xe_gt_dbg(gt, "GuC hwconfig cannot provide dss/slice; using typical fallback values\n");
	if (gt_to_xe(gt)->info.platform == XE_PVC)
		return 8;
	else if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1250)
		return 4;
	else
		return 6;
}

/**
 * xe_gt_mcr_get_dss_steering - Get the group/instance steering for a DSS
 * @gt: GT structure
 * @dss: DSS ID to obtain steering for
 * @group: pointer to storage for steering group ID
 * @instance: pointer to storage for steering instance ID
 */
void xe_gt_mcr_get_dss_steering(struct xe_gt *gt, unsigned int dss, u16 *group, u16 *instance)
{
	xe_gt_assert(gt, dss < XE_MAX_DSS_FUSE_BITS);

	*group = dss / gt->steering_dss_per_grp;
	*instance = dss % gt->steering_dss_per_grp;
}

/**
 * xe_gt_mcr_steering_info_to_dss_id - Get DSS ID from group/instance steering
 * @gt: GT structure
 * @group: steering group ID
 * @instance: steering instance ID
 *
 * Return: the converted DSS id.
 */
u32 xe_gt_mcr_steering_info_to_dss_id(struct xe_gt *gt, u16 group, u16 instance)
{
	return group * dss_per_group(gt) + instance;
}

static void init_steering_dss(struct xe_gt *gt)
{
	gt->steering_dss_per_grp = dss_per_group(gt);

	xe_gt_mcr_get_dss_steering(gt,
				   min(xe_dss_mask_group_ffs(gt->fuse_topo.g_dss_mask, 0, 0),
				       xe_dss_mask_group_ffs(gt->fuse_topo.c_dss_mask, 0, 0)),
				   &gt->steering[DSS].group_target,
				   &gt->steering[DSS].instance_target);
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

	gt->steering[OADDRM].instance_target = 0;	/* unused */
}

static void init_steering_sqidi_psmi(struct xe_gt *gt)
{
	u32 mask = REG_FIELD_GET(XE2_NODE_ENABLE_MASK,
				 xe_mmio_read32(&gt->mmio, MIRROR_FUSE3));
	u32 select = __ffs(mask);

	gt->steering[SQIDI_PSMI].group_target = select >> 1;
	gt->steering[SQIDI_PSMI].instance_target = select & 0x1;
}

static void init_steering_inst0(struct xe_gt *gt)
{
	gt->steering[INSTANCE0].group_target = 0;	/* unused */
	gt->steering[INSTANCE0].instance_target = 0;	/* unused */
}

static const struct {
	const char *name;
	void (*init)(struct xe_gt *gt);
} xe_steering_types[] = {
	[L3BANK] =	{ "L3BANK",	init_steering_l3bank },
	[MSLICE] =	{ "MSLICE",	init_steering_mslice },
	[LNCF] =	{ "LNCF",	NULL }, /* initialized by mslice init */
	[DSS] =		{ "DSS",	init_steering_dss },
	[OADDRM] =	{ "OADDRM / GPMXMT", init_steering_oaddrm },
	[SQIDI_PSMI] =  { "SQIDI_PSMI", init_steering_sqidi_psmi },
	[INSTANCE0] =	{ "INSTANCE 0",	init_steering_inst0 },
	[IMPLICIT_STEERING] = { "IMPLICIT", NULL },
};

/**
 * xe_gt_mcr_init_early - Early initialization of the MCR support
 * @gt: GT structure
 *
 * Perform early software only initialization of the MCR lock to allow
 * the synchronization on accessing the STEER_SEMAPHORE register and
 * use the xe_gt_mcr_multicast_write() function.
 */
void xe_gt_mcr_init_early(struct xe_gt *gt)
{
	BUILD_BUG_ON(IMPLICIT_STEERING + 1 != NUM_STEERING_TYPES);
	BUILD_BUG_ON(ARRAY_SIZE(xe_steering_types) != NUM_STEERING_TYPES);

	spin_lock_init(&gt->mcr_lock);
}

/**
 * xe_gt_mcr_init - Normal initialization of the MCR support
 * @gt: GT structure
 *
 * Perform normal initialization of the MCR for all usages.
 */
void xe_gt_mcr_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (IS_SRIOV_VF(xe))
		return;

	if (gt->info.type == XE_GT_TYPE_MEDIA) {
		drm_WARN_ON(&xe->drm, MEDIA_VER(xe) < 13);

		if (MEDIA_VER(xe) >= 30) {
			gt->steering[OADDRM].ranges = xe2lpm_gpmxmt_steering_table;
			gt->steering[INSTANCE0].ranges = xe3lpm_instance0_steering_table;
		} else if (MEDIA_VERx100(xe) >= 1301) {
			gt->steering[OADDRM].ranges = xe2lpm_gpmxmt_steering_table;
			gt->steering[INSTANCE0].ranges = xe2lpm_instance0_steering_table;
		} else {
			gt->steering[OADDRM].ranges = xelpmp_oaddrm_steering_table;
		}
	} else {
		if (GRAPHICS_VER(xe) >= 20) {
			gt->steering[DSS].ranges = xe2lpg_dss_steering_table;
			gt->steering[SQIDI_PSMI].ranges = xe2lpg_sqidi_psmi_steering_table;
			gt->steering[INSTANCE0].ranges = xe2lpg_instance0_steering_table;
		} else if (GRAPHICS_VERx100(xe) >= 1270) {
			gt->steering[INSTANCE0].ranges = xelpg_instance0_steering_table;
			gt->steering[L3BANK].ranges = xelpg_l3bank_steering_table;
			gt->steering[DSS].ranges = xelpg_dss_steering_table;
		} else if (xe->info.platform == XE_PVC) {
			gt->steering[INSTANCE0].ranges = xehpc_instance0_steering_table;
			gt->steering[DSS].ranges = xehpc_dss_steering_table;
		} else if (xe->info.platform == XE_DG2) {
			gt->steering[L3BANK].ranges = xehp_l3bank_steering_table;
			gt->steering[MSLICE].ranges = xehp_mslice_steering_table;
			gt->steering[LNCF].ranges = xehp_lncf_steering_table;
			gt->steering[DSS].ranges = xehp_dss_steering_table;
			gt->steering[IMPLICIT_STEERING].ranges = dg2_implicit_steering_table;
		} else {
			gt->steering[L3BANK].ranges = xelp_l3bank_steering_table;
			gt->steering[DSS].ranges = xelp_dss_steering_table;
		}
	}

	/* Select non-terminated steering target for each type */
	for (int i = 0; i < NUM_STEERING_TYPES; i++)
		if (gt->steering[i].ranges && xe_steering_types[i].init)
			xe_steering_types[i].init(gt);
}

/**
 * xe_gt_mcr_set_implicit_defaults - Initialize steer control registers
 * @gt: GT structure
 *
 * Some register ranges don't need to have their steering control registers
 * changed on each access - it's sufficient to set them once on initialization.
 * This function sets those registers for each platform *
 */
void xe_gt_mcr_set_implicit_defaults(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (IS_SRIOV_VF(xe))
		return;

	if (xe->info.platform == XE_DG2) {
		u32 steer_val = REG_FIELD_PREP(MCR_SLICE_MASK, 0) |
			REG_FIELD_PREP(MCR_SUBSLICE_MASK, 2);

		xe_mmio_write32(&gt->mmio, MCFG_MCR_SELECTOR, steer_val);
		xe_mmio_write32(&gt->mmio, SF_MCR_SELECTOR, steer_val);
		/*
		 * For GAM registers, all reads should be directed to instance 1
		 * (unicast reads against other instances are not allowed),
		 * and instance 1 is already the hardware's default steering
		 * target, which we never change
		 */
	}
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
 * returned.  Returns false if the caller need not perform any steering
 */
bool xe_gt_mcr_get_nonterminated_steering(struct xe_gt *gt,
					  struct xe_reg_mcr reg_mcr,
					  u8 *group, u8 *instance)
{
	const struct xe_reg reg = to_xe_reg(reg_mcr);
	const struct xe_mmio_range *implicit_ranges;

	for (int type = 0; type < IMPLICIT_STEERING; type++) {
		if (!gt->steering[type].ranges)
			continue;

		for (int i = 0; gt->steering[type].ranges[i].end > 0; i++) {
			if (xe_mmio_in_range(&gt->mmio, &gt->steering[type].ranges[i], reg)) {
				*group = gt->steering[type].group_target;
				*instance = gt->steering[type].instance_target;
				return true;
			}
		}
	}

	implicit_ranges = gt->steering[IMPLICIT_STEERING].ranges;
	if (implicit_ranges)
		for (int i = 0; implicit_ranges[i].end > 0; i++)
			if (xe_mmio_in_range(&gt->mmio, &implicit_ranges[i], reg))
				return false;

	/*
	 * Not found in a steering table and not a register with implicit
	 * steering. Just steer to 0/0 as a guess and raise a warning.
	 */
	drm_WARN(&gt_to_xe(gt)->drm, true,
		 "Did not find MCR register %#x in any MCR steering table\n",
		 reg.addr);
	*group = 0;
	*instance = 0;

	return true;
}

/*
 * Obtain exclusive access to MCR steering.  On MTL and beyond we also need
 * to synchronize with external clients (e.g., firmware), so a semaphore
 * register will also need to be taken.
 */
static void mcr_lock(struct xe_gt *gt) __acquires(&gt->mcr_lock)
{
	struct xe_device *xe = gt_to_xe(gt);
	int ret = 0;

	spin_lock(&gt->mcr_lock);

	/*
	 * Starting with MTL we also need to grab a semaphore register
	 * to synchronize with external agents (e.g., firmware) that now
	 * shares the same steering control register. The semaphore is obtained
	 * when a read to the relevant register returns 1.
	 */
	if (GRAPHICS_VERx100(xe) >= 1270)
		ret = xe_mmio_wait32(&gt->mmio, STEER_SEMAPHORE, 0x1, 0x1, 10, NULL,
				     true);

	drm_WARN_ON_ONCE(&xe->drm, ret == -ETIMEDOUT);
}

static void mcr_unlock(struct xe_gt *gt) __releases(&gt->mcr_lock)
{
	/* Release hardware semaphore - this is done by writing 1 to the register */
	if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1270)
		xe_mmio_write32(&gt->mmio, STEER_SEMAPHORE, 0x1);

	spin_unlock(&gt->mcr_lock);
}

/*
 * Access a register with specific MCR steering
 *
 * Caller needs to make sure the relevant forcewake wells are up.
 */
static u32 rw_with_mcr_steering(struct xe_gt *gt, struct xe_reg_mcr reg_mcr,
				u8 rw_flag, int group, int instance, u32 value)
{
	const struct xe_reg reg = to_xe_reg(reg_mcr);
	struct xe_mmio *mmio = &gt->mmio;
	struct xe_reg steer_reg;
	u32 steer_val, val = 0;

	lockdep_assert_held(&gt->mcr_lock);

	if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1270) {
		steer_reg = MTL_MCR_SELECTOR;
		steer_val = REG_FIELD_PREP(MTL_MCR_GROUPID, group) |
			REG_FIELD_PREP(MTL_MCR_INSTANCEID, instance);
	} else {
		steer_reg = MCR_SELECTOR;
		steer_val = REG_FIELD_PREP(MCR_SLICE_MASK, group) |
			REG_FIELD_PREP(MCR_SUBSLICE_MASK, instance);
	}

	/*
	 * Always leave the hardware in multicast mode when doing reads and only
	 * change it to unicast mode when doing writes of a specific instance.
	 *
	 * The setting of the multicast/unicast bit usually wouldn't matter for
	 * read operations (which always return the value from a single register
	 * instance regardless of how that bit is set), but some platforms may
	 * have workarounds requiring us to remain in multicast mode for reads,
	 * e.g. Wa_22013088509 on PVC.  There's no real downside to this, so
	 * we'll just go ahead and do so on all platforms; we'll only clear the
	 * multicast bit from the mask when explicitly doing a write operation.
	 *
	 * No need to save old steering reg value.
	 */
	if (rw_flag == MCR_OP_READ)
		steer_val |= MCR_MULTICAST;

	xe_mmio_write32(mmio, steer_reg, steer_val);

	if (rw_flag == MCR_OP_READ)
		val = xe_mmio_read32(mmio, reg);
	else
		xe_mmio_write32(mmio, reg, value);

	/*
	 * If we turned off the multicast bit (during a write) we're required
	 * to turn it back on before finishing.  The group and instance values
	 * don't matter since they'll be re-programmed on the next MCR
	 * operation.
	 */
	if (rw_flag == MCR_OP_WRITE)
		xe_mmio_write32(mmio, steer_reg, MCR_MULTICAST);

	return val;
}

/**
 * xe_gt_mcr_unicast_read_any - reads a non-terminated instance of an MCR register
 * @gt: GT structure
 * @reg_mcr: register to read
 *
 * Reads a GT MCR register.  The read will be steered to a non-terminated
 * instance (i.e., one that isn't fused off or powered down by power gating).
 * This function assumes the caller is already holding any necessary forcewake
 * domains.
 *
 * Returns the value from a non-terminated instance of @reg.
 */
u32 xe_gt_mcr_unicast_read_any(struct xe_gt *gt, struct xe_reg_mcr reg_mcr)
{
	const struct xe_reg reg = to_xe_reg(reg_mcr);
	u8 group, instance;
	u32 val;
	bool steer;

	xe_gt_assert(gt, !IS_SRIOV_VF(gt_to_xe(gt)));

	steer = xe_gt_mcr_get_nonterminated_steering(gt, reg_mcr,
						     &group, &instance);

	if (steer) {
		mcr_lock(gt);
		val = rw_with_mcr_steering(gt, reg_mcr, MCR_OP_READ,
					   group, instance, 0);
		mcr_unlock(gt);
	} else {
		val = xe_mmio_read32(&gt->mmio, reg);
	}

	return val;
}

/**
 * xe_gt_mcr_unicast_read - read a specific instance of an MCR register
 * @gt: GT structure
 * @reg_mcr: the MCR register to read
 * @group: the MCR group
 * @instance: the MCR instance
 *
 * Returns the value read from an MCR register after steering toward a specific
 * group/instance.
 */
u32 xe_gt_mcr_unicast_read(struct xe_gt *gt,
			   struct xe_reg_mcr reg_mcr,
			   int group, int instance)
{
	u32 val;

	xe_gt_assert(gt, !IS_SRIOV_VF(gt_to_xe(gt)));

	mcr_lock(gt);
	val = rw_with_mcr_steering(gt, reg_mcr, MCR_OP_READ, group, instance, 0);
	mcr_unlock(gt);

	return val;
}

/**
 * xe_gt_mcr_unicast_write - write a specific instance of an MCR register
 * @gt: GT structure
 * @reg_mcr: the MCR register to write
 * @value: value to write
 * @group: the MCR group
 * @instance: the MCR instance
 *
 * Write an MCR register in unicast mode after steering toward a specific
 * group/instance.
 */
void xe_gt_mcr_unicast_write(struct xe_gt *gt, struct xe_reg_mcr reg_mcr,
			     u32 value, int group, int instance)
{
	xe_gt_assert(gt, !IS_SRIOV_VF(gt_to_xe(gt)));

	mcr_lock(gt);
	rw_with_mcr_steering(gt, reg_mcr, MCR_OP_WRITE, group, instance, value);
	mcr_unlock(gt);
}

/**
 * xe_gt_mcr_multicast_write - write a value to all instances of an MCR register
 * @gt: GT structure
 * @reg_mcr: the MCR register to write
 * @value: value to write
 *
 * Write an MCR register in multicast mode to update all instances.
 */
void xe_gt_mcr_multicast_write(struct xe_gt *gt, struct xe_reg_mcr reg_mcr,
			       u32 value)
{
	struct xe_reg reg = to_xe_reg(reg_mcr);

	xe_gt_assert(gt, !IS_SRIOV_VF(gt_to_xe(gt)));

	/*
	 * Synchronize with any unicast operations.  Once we have exclusive
	 * access, the MULTICAST bit should already be set, so there's no need
	 * to touch the steering register.
	 */
	mcr_lock(gt);
	xe_mmio_write32(&gt->mmio, reg, value);
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

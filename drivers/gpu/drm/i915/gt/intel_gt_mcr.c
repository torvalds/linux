// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"

#include "intel_gt_mcr.h"
#include "intel_gt_regs.h"

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

#define HAS_MSLICE_STEERING(dev_priv)	(INTEL_INFO(dev_priv)->has_mslice_steering)

static const char * const intel_steering_types[] = {
	"L3BANK",
	"MSLICE",
	"LNCF",
	"INSTANCE 0",
};

static const struct intel_mmio_range icl_l3bank_steering_table[] = {
	{ 0x00B100, 0x00B3FF },
	{},
};

static const struct intel_mmio_range xehpsdv_mslice_steering_table[] = {
	{ 0x004000, 0x004AFF },
	{ 0x00C800, 0x00CFFF },
	{ 0x00DD00, 0x00DDFF },
	{ 0x00E900, 0x00FFFF }, /* 0xEA00 - OxEFFF is unused */
	{},
};

static const struct intel_mmio_range xehpsdv_lncf_steering_table[] = {
	{ 0x00B000, 0x00B0FF },
	{ 0x00D800, 0x00D8FF },
	{},
};

static const struct intel_mmio_range dg2_lncf_steering_table[] = {
	{ 0x00B000, 0x00B0FF },
	{ 0x00D880, 0x00D8FF },
	{},
};

/*
 * We have several types of MCR registers on PVC where steering to (0,0)
 * will always provide us with a non-terminated value.  We'll stick them
 * all in the same table for simplicity.
 */
static const struct intel_mmio_range pvc_instance0_steering_table[] = {
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

void intel_gt_mcr_init(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;

	/*
	 * An mslice is unavailable only if both the meml3 for the slice is
	 * disabled *and* all of the DSS in the slice (quadrant) are disabled.
	 */
	if (HAS_MSLICE_STEERING(i915)) {
		gt->info.mslice_mask =
			intel_slicemask_from_xehp_dssmask(gt->info.sseu.subslice_mask,
							  GEN_DSS_PER_MSLICE);
		gt->info.mslice_mask |=
			(intel_uncore_read(gt->uncore, GEN10_MIRROR_FUSE3) &
			 GEN12_MEML3_EN_MASK);

		if (!gt->info.mslice_mask) /* should be impossible! */
			drm_warn(&i915->drm, "mslice mask all zero!\n");
	}

	if (IS_PONTEVECCHIO(i915)) {
		gt->steering_table[INSTANCE0] = pvc_instance0_steering_table;
	} else if (IS_DG2(i915)) {
		gt->steering_table[MSLICE] = xehpsdv_mslice_steering_table;
		gt->steering_table[LNCF] = dg2_lncf_steering_table;
	} else if (IS_XEHPSDV(i915)) {
		gt->steering_table[MSLICE] = xehpsdv_mslice_steering_table;
		gt->steering_table[LNCF] = xehpsdv_lncf_steering_table;
	} else if (GRAPHICS_VER(i915) >= 11 &&
		   GRAPHICS_VER_FULL(i915) < IP_VER(12, 50)) {
		gt->steering_table[L3BANK] = icl_l3bank_steering_table;
		gt->info.l3bank_mask =
			~intel_uncore_read(gt->uncore, GEN10_MIRROR_FUSE3) &
			GEN10_L3BANK_MASK;
		if (!gt->info.l3bank_mask) /* should be impossible! */
			drm_warn(&i915->drm, "L3 bank mask is all zero!\n");
	} else if (GRAPHICS_VER(i915) >= 11) {
		/*
		 * We expect all modern platforms to have at least some
		 * type of steering that needs to be initialized.
		 */
		MISSING_CASE(INTEL_INFO(i915)->platform);
	}
}

/**
 * uncore_rw_with_mcr_steering_fw - Access a register after programming
 *				    the MCR selector register.
 * @uncore: pointer to struct intel_uncore
 * @reg: register being accessed
 * @rw_flag: FW_REG_READ for read access or FW_REG_WRITE for write access
 * @slice: slice number (ignored for multi-cast write)
 * @subslice: sub-slice number (ignored for multi-cast write)
 * @value: register value to be written (ignored for read)
 *
 * Return: 0 for write access. register value for read access.
 *
 * Caller needs to make sure the relevant forcewake wells are up.
 */
static u32 uncore_rw_with_mcr_steering_fw(struct intel_uncore *uncore,
					  i915_reg_t reg, u8 rw_flag,
					  int slice, int subslice, u32 value)
{
	u32 mcr_mask, mcr_ss, mcr, old_mcr, val = 0;

	lockdep_assert_held(&uncore->lock);

	if (GRAPHICS_VER(uncore->i915) >= 11) {
		mcr_mask = GEN11_MCR_SLICE_MASK | GEN11_MCR_SUBSLICE_MASK;
		mcr_ss = GEN11_MCR_SLICE(slice) | GEN11_MCR_SUBSLICE(subslice);

		/*
		 * Wa_22013088509
		 *
		 * The setting of the multicast/unicast bit usually wouldn't
		 * matter for read operations (which always return the value
		 * from a single register instance regardless of how that bit
		 * is set), but some platforms have a workaround requiring us
		 * to remain in multicast mode for reads.  There's no real
		 * downside to this, so we'll just go ahead and do so on all
		 * platforms; we'll only clear the multicast bit from the mask
		 * when exlicitly doing a write operation.
		 */
		if (rw_flag == FW_REG_WRITE)
			mcr_mask |= GEN11_MCR_MULTICAST;
	} else {
		mcr_mask = GEN8_MCR_SLICE_MASK | GEN8_MCR_SUBSLICE_MASK;
		mcr_ss = GEN8_MCR_SLICE(slice) | GEN8_MCR_SUBSLICE(subslice);
	}

	old_mcr = mcr = intel_uncore_read_fw(uncore, GEN8_MCR_SELECTOR);

	mcr &= ~mcr_mask;
	mcr |= mcr_ss;
	intel_uncore_write_fw(uncore, GEN8_MCR_SELECTOR, mcr);

	if (rw_flag == FW_REG_READ)
		val = intel_uncore_read_fw(uncore, reg);
	else
		intel_uncore_write_fw(uncore, reg, value);

	mcr &= ~mcr_mask;
	mcr |= old_mcr & mcr_mask;

	intel_uncore_write_fw(uncore, GEN8_MCR_SELECTOR, mcr);

	return val;
}

static u32 uncore_rw_with_mcr_steering(struct intel_uncore *uncore,
				       i915_reg_t reg, u8 rw_flag,
				       int slice, int subslice,
				       u32 value)
{
	enum forcewake_domains fw_domains;
	u32 val;

	fw_domains = intel_uncore_forcewake_for_reg(uncore, reg,
						    rw_flag);
	fw_domains |= intel_uncore_forcewake_for_reg(uncore,
						     GEN8_MCR_SELECTOR,
						     FW_REG_READ | FW_REG_WRITE);

	spin_lock_irq(&uncore->lock);
	intel_uncore_forcewake_get__locked(uncore, fw_domains);

	val = uncore_rw_with_mcr_steering_fw(uncore, reg, rw_flag,
					     slice, subslice, value);

	intel_uncore_forcewake_put__locked(uncore, fw_domains);
	spin_unlock_irq(&uncore->lock);

	return val;
}

u32 intel_uncore_read_with_mcr_steering_fw(struct intel_uncore *uncore,
					   i915_reg_t reg, int slice, int subslice)
{
	return uncore_rw_with_mcr_steering_fw(uncore, reg, FW_REG_READ,
					      slice, subslice, 0);
}

u32 intel_uncore_read_with_mcr_steering(struct intel_uncore *uncore,
					i915_reg_t reg, int slice, int subslice)
{
	return uncore_rw_with_mcr_steering(uncore, reg, FW_REG_READ,
					   slice, subslice, 0);
}

void intel_uncore_write_with_mcr_steering(struct intel_uncore *uncore,
					  i915_reg_t reg, u32 value,
					  int slice, int subslice)
{
	uncore_rw_with_mcr_steering(uncore, reg, FW_REG_WRITE,
				    slice, subslice, value);
}

/**
 * intel_gt_reg_needs_read_steering - determine whether a register read
 *     requires explicit steering
 * @gt: GT structure
 * @reg: the register to check steering requirements for
 * @type: type of multicast steering to check
 *
 * Determines whether @reg needs explicit steering of a specific type for
 * reads.
 *
 * Returns false if @reg does not belong to a register range of the given
 * steering type, or if the default (subslice-based) steering IDs are suitable
 * for @type steering too.
 */
static bool intel_gt_reg_needs_read_steering(struct intel_gt *gt,
					     i915_reg_t reg,
					     enum intel_steering_type type)
{
	const u32 offset = i915_mmio_reg_offset(reg);
	const struct intel_mmio_range *entry;

	if (likely(!intel_gt_needs_read_steering(gt, type)))
		return false;

	for (entry = gt->steering_table[type]; entry->end; entry++) {
		if (offset >= entry->start && offset <= entry->end)
			return true;
	}

	return false;
}

/**
 * intel_gt_get_valid_steering - determines valid IDs for a class of MCR steering
 * @gt: GT structure
 * @type: multicast register type
 * @sliceid: Slice ID returned
 * @subsliceid: Subslice ID returned
 *
 * Determines sliceid and subsliceid values that will steer reads
 * of a specific multicast register class to a valid value.
 */
static void intel_gt_get_valid_steering(struct intel_gt *gt,
					enum intel_steering_type type,
					u8 *sliceid, u8 *subsliceid)
{
	switch (type) {
	case L3BANK:
		*sliceid = 0;		/* unused */
		*subsliceid = __ffs(gt->info.l3bank_mask);
		break;
	case MSLICE:
		GEM_WARN_ON(!HAS_MSLICE_STEERING(gt->i915));
		*sliceid = __ffs(gt->info.mslice_mask);
		*subsliceid = 0;	/* unused */
		break;
	case LNCF:
		/*
		 * An LNCF is always present if its mslice is present, so we
		 * can safely just steer to LNCF 0 in all cases.
		 */
		GEM_WARN_ON(!HAS_MSLICE_STEERING(gt->i915));
		*sliceid = __ffs(gt->info.mslice_mask) << 1;
		*subsliceid = 0;	/* unused */
		break;
	case INSTANCE0:
		/*
		 * There are a lot of MCR types for which instance (0, 0)
		 * will always provide a non-terminated value.
		 */
		*sliceid = 0;
		*subsliceid = 0;
		break;
	default:
		MISSING_CASE(type);
		*sliceid = 0;
		*subsliceid = 0;
	}
}

/**
 * intel_gt_get_valid_steering_for_reg - get a valid steering for a register
 * @gt: GT structure
 * @reg: register for which the steering is required
 * @sliceid: return variable for slice steering
 * @subsliceid: return variable for subslice steering
 *
 * This function returns a slice/subslice pair that is guaranteed to work for
 * read steering of the given register. Note that a value will be returned even
 * if the register is not replicated and therefore does not actually require
 * steering.
 */
void intel_gt_get_valid_steering_for_reg(struct intel_gt *gt, i915_reg_t reg,
					 u8 *sliceid, u8 *subsliceid)
{
	int type;

	for (type = 0; type < NUM_STEERING_TYPES; type++) {
		if (intel_gt_reg_needs_read_steering(gt, reg, type)) {
			intel_gt_get_valid_steering(gt, type, sliceid,
						    subsliceid);
			return;
		}
	}

	*sliceid = gt->default_steering.groupid;
	*subsliceid = gt->default_steering.instanceid;
}

/**
 * intel_gt_read_register_fw - reads a GT register with support for multicast
 * @gt: GT structure
 * @reg: register to read
 *
 * This function will read a GT register.  If the register is a multicast
 * register, the read will be steered to a valid instance (i.e., one that
 * isn't fused off or powered down by power gating).
 *
 * Returns the value from a valid instance of @reg.
 */
u32 intel_gt_read_register_fw(struct intel_gt *gt, i915_reg_t reg)
{
	int type;
	u8 sliceid, subsliceid;

	for (type = 0; type < NUM_STEERING_TYPES; type++) {
		if (intel_gt_reg_needs_read_steering(gt, reg, type)) {
			intel_gt_get_valid_steering(gt, type, &sliceid,
						    &subsliceid);
			return intel_uncore_read_with_mcr_steering_fw(gt->uncore,
								      reg,
								      sliceid,
								      subsliceid);
		}
	}

	return intel_uncore_read_fw(gt->uncore, reg);
}

u32 intel_gt_read_register(struct intel_gt *gt, i915_reg_t reg)
{
	int type;
	u8 sliceid, subsliceid;

	for (type = 0; type < NUM_STEERING_TYPES; type++) {
		if (intel_gt_reg_needs_read_steering(gt, reg, type)) {
			intel_gt_get_valid_steering(gt, type, &sliceid,
						    &subsliceid);
			return intel_uncore_read_with_mcr_steering(gt->uncore,
								   reg,
								   sliceid,
								   subsliceid);
		}
	}

	return intel_uncore_read(gt->uncore, reg);
}

static void report_steering_type(struct drm_printer *p,
				 struct intel_gt *gt,
				 enum intel_steering_type type,
				 bool dump_table)
{
	const struct intel_mmio_range *entry;
	u8 slice, subslice;

	BUILD_BUG_ON(ARRAY_SIZE(intel_steering_types) != NUM_STEERING_TYPES);

	if (!gt->steering_table[type]) {
		drm_printf(p, "%s steering: uses default steering\n",
			   intel_steering_types[type]);
		return;
	}

	intel_gt_get_valid_steering(gt, type, &slice, &subslice);
	drm_printf(p, "%s steering: sliceid=0x%x, subsliceid=0x%x\n",
		   intel_steering_types[type], slice, subslice);

	if (!dump_table)
		return;

	for (entry = gt->steering_table[type]; entry->end; entry++)
		drm_printf(p, "\t0x%06x - 0x%06x\n", entry->start, entry->end);
}

void intel_gt_report_steering(struct drm_printer *p, struct intel_gt *gt,
			      bool dump_table)
{
	drm_printf(p, "Default steering: sliceid=0x%x, subsliceid=0x%x\n",
		   gt->default_steering.groupid,
		   gt->default_steering.instanceid);

	if (IS_PONTEVECCHIO(gt->i915)) {
		report_steering_type(p, gt, INSTANCE0, dump_table);
	} else if (HAS_MSLICE_STEERING(gt->i915)) {
		report_steering_type(p, gt, MSLICE, dump_table);
		report_steering_type(p, gt, LNCF, dump_table);
	}
}


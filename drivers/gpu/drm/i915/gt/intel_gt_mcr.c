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

/*
 * rw_with_mcr_steering_fw - Access a register with specific MCR steering
 * @uncore: pointer to struct intel_uncore
 * @reg: register being accessed
 * @rw_flag: FW_REG_READ for read access or FW_REG_WRITE for write access
 * @group: group number (documented as "sliceid" on older platforms)
 * @instance: instance number (documented as "subsliceid" on older platforms)
 * @value: register value to be written (ignored for read)
 *
 * Return: 0 for write access. register value for read access.
 *
 * Caller needs to make sure the relevant forcewake wells are up.
 */
static u32 rw_with_mcr_steering_fw(struct intel_uncore *uncore,
				   i915_reg_t reg, u8 rw_flag,
				   int group, int instance, u32 value)
{
	u32 mcr_mask, mcr_ss, mcr, old_mcr, val = 0;

	lockdep_assert_held(&uncore->lock);

	if (GRAPHICS_VER(uncore->i915) >= 11) {
		mcr_mask = GEN11_MCR_SLICE_MASK | GEN11_MCR_SUBSLICE_MASK;
		mcr_ss = GEN11_MCR_SLICE(group) | GEN11_MCR_SUBSLICE(instance);

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
		mcr_ss = GEN8_MCR_SLICE(group) | GEN8_MCR_SUBSLICE(instance);
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

static u32 rw_with_mcr_steering(struct intel_uncore *uncore,
				i915_reg_t reg, u8 rw_flag,
				int group, int instance,
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

	val = rw_with_mcr_steering_fw(uncore, reg, rw_flag, group, instance, value);

	intel_uncore_forcewake_put__locked(uncore, fw_domains);
	spin_unlock_irq(&uncore->lock);

	return val;
}

/**
 * intel_gt_mcr_read - read a specific instance of an MCR register
 * @gt: GT structure
 * @reg: the MCR register to read
 * @group: the MCR group
 * @instance: the MCR instance
 *
 * Returns the value read from an MCR register after steering toward a specific
 * group/instance.
 */
u32 intel_gt_mcr_read(struct intel_gt *gt,
		      i915_reg_t reg,
		      int group, int instance)
{
	return rw_with_mcr_steering(gt->uncore, reg, FW_REG_READ, group, instance, 0);
}

/**
 * intel_gt_mcr_unicast_write - write a specific instance of an MCR register
 * @gt: GT structure
 * @reg: the MCR register to write
 * @value: value to write
 * @group: the MCR group
 * @instance: the MCR instance
 *
 * Write an MCR register in unicast mode after steering toward a specific
 * group/instance.
 */
void intel_gt_mcr_unicast_write(struct intel_gt *gt, i915_reg_t reg, u32 value,
				int group, int instance)
{
	rw_with_mcr_steering(gt->uncore, reg, FW_REG_WRITE, group, instance, value);
}

/**
 * intel_gt_mcr_multicast_write - write a value to all instances of an MCR register
 * @gt: GT structure
 * @reg: the MCR register to write
 * @value: value to write
 *
 * Write an MCR register in multicast mode to update all instances.
 */
void intel_gt_mcr_multicast_write(struct intel_gt *gt,
				i915_reg_t reg, u32 value)
{
	intel_uncore_write(gt->uncore, reg, value);
}

/**
 * intel_gt_mcr_multicast_write_fw - write a value to all instances of an MCR register
 * @gt: GT structure
 * @reg: the MCR register to write
 * @value: value to write
 *
 * Write an MCR register in multicast mode to update all instances.  This
 * function assumes the caller is already holding any necessary forcewake
 * domains; use intel_gt_mcr_multicast_write() in cases where forcewake should
 * be obtained automatically.
 */
void intel_gt_mcr_multicast_write_fw(struct intel_gt *gt, i915_reg_t reg, u32 value)
{
	intel_uncore_write_fw(gt->uncore, reg, value);
}

/*
 * reg_needs_read_steering - determine whether a register read requires
 *     explicit steering
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
static bool reg_needs_read_steering(struct intel_gt *gt,
				    i915_reg_t reg,
				    enum intel_steering_type type)
{
	const u32 offset = i915_mmio_reg_offset(reg);
	const struct intel_mmio_range *entry;

	if (likely(!gt->steering_table[type]))
		return false;

	for (entry = gt->steering_table[type]; entry->end; entry++) {
		if (offset >= entry->start && offset <= entry->end)
			return true;
	}

	return false;
}

/*
 * get_nonterminated_steering - determines valid IDs for a class of MCR steering
 * @gt: GT structure
 * @type: multicast register type
 * @group: Group ID returned
 * @instance: Instance ID returned
 *
 * Determines group and instance values that will steer reads of the specified
 * MCR class to a non-terminated instance.
 */
static void get_nonterminated_steering(struct intel_gt *gt,
				       enum intel_steering_type type,
				       u8 *group, u8 *instance)
{
	switch (type) {
	case L3BANK:
		*group = 0;		/* unused */
		*instance = __ffs(gt->info.l3bank_mask);
		break;
	case MSLICE:
		GEM_WARN_ON(!HAS_MSLICE_STEERING(gt->i915));
		*group = __ffs(gt->info.mslice_mask);
		*instance = 0;	/* unused */
		break;
	case LNCF:
		/*
		 * An LNCF is always present if its mslice is present, so we
		 * can safely just steer to LNCF 0 in all cases.
		 */
		GEM_WARN_ON(!HAS_MSLICE_STEERING(gt->i915));
		*group = __ffs(gt->info.mslice_mask) << 1;
		*instance = 0;	/* unused */
		break;
	case INSTANCE0:
		/*
		 * There are a lot of MCR types for which instance (0, 0)
		 * will always provide a non-terminated value.
		 */
		*group = 0;
		*instance = 0;
		break;
	default:
		MISSING_CASE(type);
		*group = 0;
		*instance = 0;
	}
}

/**
 * intel_gt_mcr_get_nonterminated_steering - find group/instance values that
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
 */
void intel_gt_mcr_get_nonterminated_steering(struct intel_gt *gt,
					     i915_reg_t reg,
					     u8 *group, u8 *instance)
{
	int type;

	for (type = 0; type < NUM_STEERING_TYPES; type++) {
		if (reg_needs_read_steering(gt, reg, type)) {
			get_nonterminated_steering(gt, type, group, instance);
			return;
		}
	}

	*group = gt->default_steering.groupid;
	*instance = gt->default_steering.instanceid;
}

/**
 * intel_gt_mcr_read_any_fw - reads one instance of an MCR register
 * @gt: GT structure
 * @reg: register to read
 *
 * Reads a GT MCR register.  The read will be steered to a non-terminated
 * instance (i.e., one that isn't fused off or powered down by power gating).
 * This function assumes the caller is already holding any necessary forcewake
 * domains; use intel_gt_mcr_read_any() in cases where forcewake should be
 * obtained automatically.
 *
 * Returns the value from a non-terminated instance of @reg.
 */
u32 intel_gt_mcr_read_any_fw(struct intel_gt *gt, i915_reg_t reg)
{
	int type;
	u8 group, instance;

	for (type = 0; type < NUM_STEERING_TYPES; type++) {
		if (reg_needs_read_steering(gt, reg, type)) {
			get_nonterminated_steering(gt, type, &group, &instance);
			return rw_with_mcr_steering_fw(gt->uncore, reg,
						       FW_REG_READ,
						       group, instance, 0);
		}
	}

	return intel_uncore_read_fw(gt->uncore, reg);
}

/**
 * intel_gt_mcr_read_any - reads one instance of an MCR register
 * @gt: GT structure
 * @reg: register to read
 *
 * Reads a GT MCR register.  The read will be steered to a non-terminated
 * instance (i.e., one that isn't fused off or powered down by power gating).
 *
 * Returns the value from a non-terminated instance of @reg.
 */
u32 intel_gt_mcr_read_any(struct intel_gt *gt, i915_reg_t reg)
{
	int type;
	u8 group, instance;

	for (type = 0; type < NUM_STEERING_TYPES; type++) {
		if (reg_needs_read_steering(gt, reg, type)) {
			get_nonterminated_steering(gt, type, &group, &instance);
			return rw_with_mcr_steering(gt->uncore, reg,
						    FW_REG_READ,
						    group, instance, 0);
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
	u8 group, instance;

	BUILD_BUG_ON(ARRAY_SIZE(intel_steering_types) != NUM_STEERING_TYPES);

	if (!gt->steering_table[type]) {
		drm_printf(p, "%s steering: uses default steering\n",
			   intel_steering_types[type]);
		return;
	}

	get_nonterminated_steering(gt, type, &group, &instance);
	drm_printf(p, "%s steering: group=0x%x, instance=0x%x\n",
		   intel_steering_types[type], group, instance);

	if (!dump_table)
		return;

	for (entry = gt->steering_table[type]; entry->end; entry++)
		drm_printf(p, "\t0x%06x - 0x%06x\n", entry->start, entry->end);
}

void intel_gt_mcr_report_steering(struct drm_printer *p, struct intel_gt *gt,
				  bool dump_table)
{
	drm_printf(p, "Default steering: group=0x%x, instance=0x%x\n",
		   gt->default_steering.groupid,
		   gt->default_steering.instanceid);

	if (IS_PONTEVECCHIO(gt->i915)) {
		report_steering_type(p, gt, INSTANCE0, dump_table);
	} else if (HAS_MSLICE_STEERING(gt->i915)) {
		report_steering_type(p, gt, MSLICE, dump_table);
		report_steering_type(p, gt, LNCF, dump_table);
	}
}

/**
 * intel_gt_mcr_get_ss_steering - returns the group/instance steering for a SS
 * @gt: GT structure
 * @dss: DSS ID to obtain steering for
 * @group: pointer to storage for steering group ID
 * @instance: pointer to storage for steering instance ID
 *
 * Returns the steering IDs (via the @group and @instance parameters) that
 * correspond to a specific subslice/DSS ID.
 */
void intel_gt_mcr_get_ss_steering(struct intel_gt *gt, unsigned int dss,
				   unsigned int *group, unsigned int *instance)
{
	if (IS_PONTEVECCHIO(gt->i915)) {
		*group = dss / GEN_DSS_PER_CSLICE;
		*instance = dss % GEN_DSS_PER_CSLICE;
	} else if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 50)) {
		*group = dss / GEN_DSS_PER_GSLICE;
		*instance = dss % GEN_DSS_PER_GSLICE;
	} else {
		*group = dss / GEN_MAX_SS_PER_HSW_SLICE;
		*instance = dss % GEN_MAX_SS_PER_HSW_SLICE;
		return;
	}
}

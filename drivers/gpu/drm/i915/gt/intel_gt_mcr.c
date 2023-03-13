// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"

#include "intel_gt_mcr.h"
#include "intel_gt_print.h"
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
	"GAM",
	"DSS",
	"OADDRM",
	"INSTANCE 0",
};

static const struct intel_mmio_range icl_l3bank_steering_table[] = {
	{ 0x00B100, 0x00B3FF },
	{},
};

/*
 * Although the bspec lists more "MSLICE" ranges than shown here, some of those
 * are of a "GAM" subclass that has special rules.  Thus we use a separate
 * GAM table farther down for those.
 */
static const struct intel_mmio_range xehpsdv_mslice_steering_table[] = {
	{ 0x00DD00, 0x00DDFF },
	{ 0x00E900, 0x00FFFF }, /* 0xEA00 - OxEFFF is unused */
	{},
};

static const struct intel_mmio_range xehpsdv_gam_steering_table[] = {
	{ 0x004000, 0x004AFF },
	{ 0x00C800, 0x00CFFF },
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

static const struct intel_mmio_range xelpg_instance0_steering_table[] = {
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

static const struct intel_mmio_range xelpg_l3bank_steering_table[] = {
	{ 0x00B100, 0x00B3FF },
	{},
};

/* DSS steering is used for SLICE ranges as well */
static const struct intel_mmio_range xelpg_dss_steering_table[] = {
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

static const struct intel_mmio_range xelpmp_oaddrm_steering_table[] = {
	{ 0x393200, 0x39323F },
	{ 0x393400, 0x3934FF },
	{},
};

void intel_gt_mcr_init(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	unsigned long fuse;
	int i;

	spin_lock_init(&gt->mcr_lock);

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
			gt_warn(gt, "mslice mask all zero!\n");
	}

	if (MEDIA_VER(i915) >= 13 && gt->type == GT_MEDIA) {
		gt->steering_table[OADDRM] = xelpmp_oaddrm_steering_table;
	} else if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 70)) {
		/* Wa_14016747170 */
		if (IS_MTL_GRAPHICS_STEP(i915, M, STEP_A0, STEP_B0) ||
		    IS_MTL_GRAPHICS_STEP(i915, P, STEP_A0, STEP_B0))
			fuse = REG_FIELD_GET(MTL_GT_L3_EXC_MASK,
					     intel_uncore_read(gt->uncore,
							       MTL_GT_ACTIVITY_FACTOR));
		else
			fuse = REG_FIELD_GET(GT_L3_EXC_MASK,
					     intel_uncore_read(gt->uncore, XEHP_FUSE4));

		/*
		 * Despite the register field being named "exclude mask" the
		 * bits actually represent enabled banks (two banks per bit).
		 */
		for_each_set_bit(i, &fuse, 3)
			gt->info.l3bank_mask |= 0x3 << 2 * i;

		gt->steering_table[INSTANCE0] = xelpg_instance0_steering_table;
		gt->steering_table[L3BANK] = xelpg_l3bank_steering_table;
		gt->steering_table[DSS] = xelpg_dss_steering_table;
	} else if (IS_PONTEVECCHIO(i915)) {
		gt->steering_table[INSTANCE0] = pvc_instance0_steering_table;
	} else if (IS_DG2(i915)) {
		gt->steering_table[MSLICE] = xehpsdv_mslice_steering_table;
		gt->steering_table[LNCF] = dg2_lncf_steering_table;
		/*
		 * No need to hook up the GAM table since it has a dedicated
		 * steering control register on DG2 and can use implicit
		 * steering.
		 */
	} else if (IS_XEHPSDV(i915)) {
		gt->steering_table[MSLICE] = xehpsdv_mslice_steering_table;
		gt->steering_table[LNCF] = xehpsdv_lncf_steering_table;
		gt->steering_table[GAM] = xehpsdv_gam_steering_table;
	} else if (GRAPHICS_VER(i915) >= 11 &&
		   GRAPHICS_VER_FULL(i915) < IP_VER(12, 50)) {
		gt->steering_table[L3BANK] = icl_l3bank_steering_table;
		gt->info.l3bank_mask =
			~intel_uncore_read(gt->uncore, GEN10_MIRROR_FUSE3) &
			GEN10_L3BANK_MASK;
		if (!gt->info.l3bank_mask) /* should be impossible! */
			gt_warn(gt, "L3 bank mask is all zero!\n");
	} else if (GRAPHICS_VER(i915) >= 11) {
		/*
		 * We expect all modern platforms to have at least some
		 * type of steering that needs to be initialized.
		 */
		MISSING_CASE(INTEL_INFO(i915)->platform);
	}
}

/*
 * Although the rest of the driver should use MCR-specific functions to
 * read/write MCR registers, we still use the regular intel_uncore_* functions
 * internally to implement those, so we need a way for the functions in this
 * file to "cast" an i915_mcr_reg_t into an i915_reg_t.
 */
static i915_reg_t mcr_reg_cast(const i915_mcr_reg_t mcr)
{
	i915_reg_t r = { .reg = mcr.reg };

	return r;
}

/*
 * rw_with_mcr_steering_fw - Access a register with specific MCR steering
 * @gt: GT to read register from
 * @reg: register being accessed
 * @rw_flag: FW_REG_READ for read access or FW_REG_WRITE for write access
 * @group: group number (documented as "sliceid" on older platforms)
 * @instance: instance number (documented as "subsliceid" on older platforms)
 * @value: register value to be written (ignored for read)
 *
 * Context: The caller must hold the MCR lock
 * Return: 0 for write access. register value for read access.
 *
 * Caller needs to make sure the relevant forcewake wells are up.
 */
static u32 rw_with_mcr_steering_fw(struct intel_gt *gt,
				   i915_mcr_reg_t reg, u8 rw_flag,
				   int group, int instance, u32 value)
{
	struct intel_uncore *uncore = gt->uncore;
	u32 mcr_mask, mcr_ss, mcr, old_mcr, val = 0;

	lockdep_assert_held(&gt->mcr_lock);

	if (GRAPHICS_VER_FULL(uncore->i915) >= IP_VER(12, 70)) {
		/*
		 * Always leave the hardware in multicast mode when doing reads
		 * (see comment about Wa_22013088509 below) and only change it
		 * to unicast mode when doing writes of a specific instance.
		 *
		 * No need to save old steering reg value.
		 */
		intel_uncore_write_fw(uncore, MTL_MCR_SELECTOR,
				      REG_FIELD_PREP(MTL_MCR_GROUPID, group) |
				      REG_FIELD_PREP(MTL_MCR_INSTANCEID, instance) |
				      (rw_flag == FW_REG_READ ? GEN11_MCR_MULTICAST : 0));
	} else if (GRAPHICS_VER(uncore->i915) >= 11) {
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

		mcr = intel_uncore_read_fw(uncore, GEN8_MCR_SELECTOR);
		old_mcr = mcr;

		mcr &= ~mcr_mask;
		mcr |= mcr_ss;
		intel_uncore_write_fw(uncore, GEN8_MCR_SELECTOR, mcr);
	} else {
		mcr_mask = GEN8_MCR_SLICE_MASK | GEN8_MCR_SUBSLICE_MASK;
		mcr_ss = GEN8_MCR_SLICE(group) | GEN8_MCR_SUBSLICE(instance);

		mcr = intel_uncore_read_fw(uncore, GEN8_MCR_SELECTOR);
		old_mcr = mcr;

		mcr &= ~mcr_mask;
		mcr |= mcr_ss;
		intel_uncore_write_fw(uncore, GEN8_MCR_SELECTOR, mcr);
	}

	if (rw_flag == FW_REG_READ)
		val = intel_uncore_read_fw(uncore, mcr_reg_cast(reg));
	else
		intel_uncore_write_fw(uncore, mcr_reg_cast(reg), value);

	/*
	 * For pre-MTL platforms, we need to restore the old value of the
	 * steering control register to ensure that implicit steering continues
	 * to behave as expected.  For MTL and beyond, we need only reinstate
	 * the 'multicast' bit (and only if we did a write that cleared it).
	 */
	if (GRAPHICS_VER_FULL(uncore->i915) >= IP_VER(12, 70) && rw_flag == FW_REG_WRITE)
		intel_uncore_write_fw(uncore, MTL_MCR_SELECTOR, GEN11_MCR_MULTICAST);
	else if (GRAPHICS_VER_FULL(uncore->i915) < IP_VER(12, 70))
		intel_uncore_write_fw(uncore, GEN8_MCR_SELECTOR, old_mcr);

	return val;
}

static u32 rw_with_mcr_steering(struct intel_gt *gt,
				i915_mcr_reg_t reg, u8 rw_flag,
				int group, int instance,
				u32 value)
{
	struct intel_uncore *uncore = gt->uncore;
	enum forcewake_domains fw_domains;
	unsigned long flags;
	u32 val;

	fw_domains = intel_uncore_forcewake_for_reg(uncore, mcr_reg_cast(reg),
						    rw_flag);
	fw_domains |= intel_uncore_forcewake_for_reg(uncore,
						     GEN8_MCR_SELECTOR,
						     FW_REG_READ | FW_REG_WRITE);

	intel_gt_mcr_lock(gt, &flags);
	spin_lock(&uncore->lock);
	intel_uncore_forcewake_get__locked(uncore, fw_domains);

	val = rw_with_mcr_steering_fw(gt, reg, rw_flag, group, instance, value);

	intel_uncore_forcewake_put__locked(uncore, fw_domains);
	spin_unlock(&uncore->lock);
	intel_gt_mcr_unlock(gt, flags);

	return val;
}

/**
 * intel_gt_mcr_lock - Acquire MCR steering lock
 * @gt: GT structure
 * @flags: storage to save IRQ flags to
 *
 * Performs locking to protect the steering for the duration of an MCR
 * operation.  On MTL and beyond, a hardware lock will also be taken to
 * serialize access not only for the driver, but also for external hardware and
 * firmware agents.
 *
 * Context: Takes gt->mcr_lock.  uncore->lock should *not* be held when this
 *          function is called, although it may be acquired after this
 *          function call.
 */
void intel_gt_mcr_lock(struct intel_gt *gt, unsigned long *flags)
{
	unsigned long __flags;
	int err = 0;

	lockdep_assert_not_held(&gt->uncore->lock);

	/*
	 * Starting with MTL, we need to coordinate not only with other
	 * driver threads, but also with hardware/firmware agents.  A dedicated
	 * locking register is used.
	 */
	if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 70))
		err = wait_for(intel_uncore_read_fw(gt->uncore,
						    MTL_STEER_SEMAPHORE) == 0x1, 100);

	/*
	 * Even on platforms with a hardware lock, we'll continue to grab
	 * a software spinlock too for lockdep purposes.  If the hardware lock
	 * was already acquired, there should never be contention on the
	 * software lock.
	 */
	spin_lock_irqsave(&gt->mcr_lock, __flags);

	*flags = __flags;

	/*
	 * In theory we should never fail to acquire the HW semaphore; this
	 * would indicate some hardware/firmware is misbehaving and not
	 * releasing it properly.
	 */
	if (err == -ETIMEDOUT) {
		gt_err_ratelimited(gt, "hardware MCR steering semaphore timed out");
		add_taint_for_CI(gt->i915, TAINT_WARN);  /* CI is now unreliable */
	}
}

/**
 * intel_gt_mcr_unlock - Release MCR steering lock
 * @gt: GT structure
 * @flags: IRQ flags to restore
 *
 * Releases the lock acquired by intel_gt_mcr_lock().
 *
 * Context: Releases gt->mcr_lock
 */
void intel_gt_mcr_unlock(struct intel_gt *gt, unsigned long flags)
{
	spin_unlock_irqrestore(&gt->mcr_lock, flags);

	if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 70))
		intel_uncore_write_fw(gt->uncore, MTL_STEER_SEMAPHORE, 0x1);
}

/**
 * intel_gt_mcr_read - read a specific instance of an MCR register
 * @gt: GT structure
 * @reg: the MCR register to read
 * @group: the MCR group
 * @instance: the MCR instance
 *
 * Context: Takes and releases gt->mcr_lock
 *
 * Returns the value read from an MCR register after steering toward a specific
 * group/instance.
 */
u32 intel_gt_mcr_read(struct intel_gt *gt,
		      i915_mcr_reg_t reg,
		      int group, int instance)
{
	return rw_with_mcr_steering(gt, reg, FW_REG_READ, group, instance, 0);
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
 *
 * Context: Calls a function that takes and releases gt->mcr_lock
 */
void intel_gt_mcr_unicast_write(struct intel_gt *gt, i915_mcr_reg_t reg, u32 value,
				int group, int instance)
{
	rw_with_mcr_steering(gt, reg, FW_REG_WRITE, group, instance, value);
}

/**
 * intel_gt_mcr_multicast_write - write a value to all instances of an MCR register
 * @gt: GT structure
 * @reg: the MCR register to write
 * @value: value to write
 *
 * Write an MCR register in multicast mode to update all instances.
 *
 * Context: Takes and releases gt->mcr_lock
 */
void intel_gt_mcr_multicast_write(struct intel_gt *gt,
				  i915_mcr_reg_t reg, u32 value)
{
	unsigned long flags;

	intel_gt_mcr_lock(gt, &flags);

	/*
	 * Ensure we have multicast behavior, just in case some non-i915 agent
	 * left the hardware in unicast mode.
	 */
	if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 70))
		intel_uncore_write_fw(gt->uncore, MTL_MCR_SELECTOR, GEN11_MCR_MULTICAST);

	intel_uncore_write(gt->uncore, mcr_reg_cast(reg), value);

	intel_gt_mcr_unlock(gt, flags);
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
 *
 * Context: The caller must hold gt->mcr_lock.
 */
void intel_gt_mcr_multicast_write_fw(struct intel_gt *gt, i915_mcr_reg_t reg, u32 value)
{
	lockdep_assert_held(&gt->mcr_lock);

	/*
	 * Ensure we have multicast behavior, just in case some non-i915 agent
	 * left the hardware in unicast mode.
	 */
	if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 70))
		intel_uncore_write_fw(gt->uncore, MTL_MCR_SELECTOR, GEN11_MCR_MULTICAST);

	intel_uncore_write_fw(gt->uncore, mcr_reg_cast(reg), value);
}

/**
 * intel_gt_mcr_multicast_rmw - Performs a multicast RMW operations
 * @gt: GT structure
 * @reg: the MCR register to read and write
 * @clear: bits to clear during RMW
 * @set: bits to set during RMW
 *
 * Performs a read-modify-write on an MCR register in a multicast manner.
 * This operation only makes sense on MCR registers where all instances are
 * expected to have the same value.  The read will target any non-terminated
 * instance and the write will be applied to all instances.
 *
 * This function assumes the caller is already holding any necessary forcewake
 * domains; use intel_gt_mcr_multicast_rmw() in cases where forcewake should
 * be obtained automatically.
 *
 * Context: Calls functions that take and release gt->mcr_lock
 *
 * Returns the old (unmodified) value read.
 */
u32 intel_gt_mcr_multicast_rmw(struct intel_gt *gt, i915_mcr_reg_t reg,
			       u32 clear, u32 set)
{
	u32 val = intel_gt_mcr_read_any(gt, reg);

	intel_gt_mcr_multicast_write(gt, reg, (val & ~clear) | set);

	return val;
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
				    i915_mcr_reg_t reg,
				    enum intel_steering_type type)
{
	u32 offset = i915_mmio_reg_offset(reg);
	const struct intel_mmio_range *entry;

	if (likely(!gt->steering_table[type]))
		return false;

	if (IS_GSI_REG(offset))
		offset += gt->uncore->gsi_offset;

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
	u32 dss;

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
	case GAM:
		*group = IS_DG2(gt->i915) ? 1 : 0;
		*instance = 0;
		break;
	case DSS:
		dss = intel_sseu_find_first_xehp_dss(&gt->info.sseu, 0, 0);
		*group = dss / GEN_DSS_PER_GSLICE;
		*instance = dss % GEN_DSS_PER_GSLICE;
		break;
	case INSTANCE0:
		/*
		 * There are a lot of MCR types for which instance (0, 0)
		 * will always provide a non-terminated value.
		 */
		*group = 0;
		*instance = 0;
		break;
	case OADDRM:
		if ((VDBOX_MASK(gt) | VEBOX_MASK(gt) | gt->info.sfc_mask) & BIT(0))
			*group = 0;
		else
			*group = 1;
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
					     i915_mcr_reg_t reg,
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
 * Context: The caller must hold gt->mcr_lock.
 *
 * Returns the value from a non-terminated instance of @reg.
 */
u32 intel_gt_mcr_read_any_fw(struct intel_gt *gt, i915_mcr_reg_t reg)
{
	int type;
	u8 group, instance;

	lockdep_assert_held(&gt->mcr_lock);

	for (type = 0; type < NUM_STEERING_TYPES; type++) {
		if (reg_needs_read_steering(gt, reg, type)) {
			get_nonterminated_steering(gt, type, &group, &instance);
			return rw_with_mcr_steering_fw(gt, reg,
						       FW_REG_READ,
						       group, instance, 0);
		}
	}

	return intel_uncore_read_fw(gt->uncore, mcr_reg_cast(reg));
}

/**
 * intel_gt_mcr_read_any - reads one instance of an MCR register
 * @gt: GT structure
 * @reg: register to read
 *
 * Reads a GT MCR register.  The read will be steered to a non-terminated
 * instance (i.e., one that isn't fused off or powered down by power gating).
 *
 * Context: Calls a function that takes and releases gt->mcr_lock.
 *
 * Returns the value from a non-terminated instance of @reg.
 */
u32 intel_gt_mcr_read_any(struct intel_gt *gt, i915_mcr_reg_t reg)
{
	int type;
	u8 group, instance;

	for (type = 0; type < NUM_STEERING_TYPES; type++) {
		if (reg_needs_read_steering(gt, reg, type)) {
			get_nonterminated_steering(gt, type, &group, &instance);
			return rw_with_mcr_steering(gt, reg,
						    FW_REG_READ,
						    group, instance, 0);
		}
	}

	return intel_uncore_read(gt->uncore, mcr_reg_cast(reg));
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
	/*
	 * Starting with MTL we no longer have default steering;
	 * all ranges are explicitly steered.
	 */
	if (GRAPHICS_VER_FULL(gt->i915) < IP_VER(12, 70))
		drm_printf(p, "Default steering: group=0x%x, instance=0x%x\n",
			   gt->default_steering.groupid,
			   gt->default_steering.instanceid);

	if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 70)) {
		for (int i = 0; i < NUM_STEERING_TYPES; i++)
			if (gt->steering_table[i])
				report_steering_type(p, gt, i, dump_table);
	} else if (IS_PONTEVECCHIO(gt->i915)) {
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

/**
 * intel_gt_mcr_wait_for_reg - wait until MCR register matches expected state
 * @gt: GT structure
 * @reg: the register to read
 * @mask: mask to apply to register value
 * @value: value to wait for
 * @fast_timeout_us: fast timeout in microsecond for atomic/tight wait
 * @slow_timeout_ms: slow timeout in millisecond
 *
 * This routine waits until the target register @reg contains the expected
 * @value after applying the @mask, i.e. it waits until ::
 *
 *     (intel_gt_mcr_read_any_fw(gt, reg) & mask) == value
 *
 * Otherwise, the wait will timeout after @slow_timeout_ms milliseconds.
 * For atomic context @slow_timeout_ms must be zero and @fast_timeout_us
 * must be not larger than 20,0000 microseconds.
 *
 * This function is basically an MCR-friendly version of
 * __intel_wait_for_register_fw().  Generally this function will only be used
 * on GAM registers which are a bit special --- although they're MCR registers,
 * reads (e.g., waiting for status updates) are always directed to the primary
 * instance.
 *
 * Note that this routine assumes the caller holds forcewake asserted, it is
 * not suitable for very long waits.
 *
 * Context: Calls a function that takes and releases gt->mcr_lock
 * Return: 0 if the register matches the desired condition, or -ETIMEDOUT.
 */
int intel_gt_mcr_wait_for_reg(struct intel_gt *gt,
			      i915_mcr_reg_t reg,
			      u32 mask,
			      u32 value,
			      unsigned int fast_timeout_us,
			      unsigned int slow_timeout_ms)
{
	int ret;

	lockdep_assert_not_held(&gt->mcr_lock);

#define done ((intel_gt_mcr_read_any(gt, reg) & mask) == value)

	/* Catch any overuse of this function */
	might_sleep_if(slow_timeout_ms);
	GEM_BUG_ON(fast_timeout_us > 20000);
	GEM_BUG_ON(!fast_timeout_us && !slow_timeout_ms);

	ret = -ETIMEDOUT;
	if (fast_timeout_us && fast_timeout_us <= 20000)
		ret = _wait_for_atomic(done, fast_timeout_us, 0);
	if (ret && slow_timeout_ms)
		ret = wait_for(done, slow_timeout_ms);

	return ret;
#undef done
}

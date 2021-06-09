// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"

/**
 * ice_ptp_read_src_clk_reg - Read the source clock register
 * @pf: Board private structure
 * @sts: Optional parameter for holding a pair of system timestamps from
 *       the system clock. Will be ignored if NULL is given.
 */
static u64
ice_ptp_read_src_clk_reg(struct ice_pf *pf, struct ptp_system_timestamp *sts)
{
	struct ice_hw *hw = &pf->hw;
	u32 hi, lo, lo2;
	u8 tmr_idx;

	tmr_idx = ice_get_ptp_src_clock_index(hw);
	/* Read the system timestamp pre PHC read */
	if (sts)
		ptp_read_system_prets(sts);

	lo = rd32(hw, GLTSYN_TIME_L(tmr_idx));

	/* Read the system timestamp post PHC read */
	if (sts)
		ptp_read_system_postts(sts);

	hi = rd32(hw, GLTSYN_TIME_H(tmr_idx));
	lo2 = rd32(hw, GLTSYN_TIME_L(tmr_idx));

	if (lo2 < lo) {
		/* if TIME_L rolled over read TIME_L again and update
		 * system timestamps
		 */
		if (sts)
			ptp_read_system_prets(sts);
		lo = rd32(hw, GLTSYN_TIME_L(tmr_idx));
		if (sts)
			ptp_read_system_postts(sts);
		hi = rd32(hw, GLTSYN_TIME_H(tmr_idx));
	}

	return ((u64)hi << 32) | lo;
}

/**
 * ice_ptp_read_time - Read the time from the device
 * @pf: Board private structure
 * @ts: timespec structure to hold the current time value
 * @sts: Optional parameter for holding a pair of system timestamps from
 *       the system clock. Will be ignored if NULL is given.
 *
 * This function reads the source clock registers and stores them in a timespec.
 * However, since the registers are 64 bits of nanoseconds, we must convert the
 * result to a timespec before we can return.
 */
static void
ice_ptp_read_time(struct ice_pf *pf, struct timespec64 *ts,
		  struct ptp_system_timestamp *sts)
{
	u64 time_ns = ice_ptp_read_src_clk_reg(pf, sts);

	*ts = ns_to_timespec64(time_ns);
}

/**
 * ice_ptp_write_init - Set PHC time to provided value
 * @pf: Board private structure
 * @ts: timespec structure that holds the new time value
 *
 * Set the PHC time to the specified time provided in the timespec.
 */
static int ice_ptp_write_init(struct ice_pf *pf, struct timespec64 *ts)
{
	u64 ns = timespec64_to_ns(ts);
	struct ice_hw *hw = &pf->hw;

	return ice_ptp_init_time(hw, ns);
}

/**
 * ice_ptp_write_adj - Adjust PHC clock time atomically
 * @pf: Board private structure
 * @adj: Adjustment in nanoseconds
 *
 * Perform an atomic adjustment of the PHC time by the specified number of
 * nanoseconds.
 */
static int ice_ptp_write_adj(struct ice_pf *pf, s32 adj)
{
	struct ice_hw *hw = &pf->hw;

	return ice_ptp_adj_clock(hw, adj);
}

/**
 * ice_ptp_adjfine - Adjust clock increment rate
 * @info: the driver's PTP info structure
 * @scaled_ppm: Parts per million with 16-bit fractional field
 *
 * Adjust the frequency of the clock by the indicated scaled ppm from the
 * base frequency.
 */
static int ice_ptp_adjfine(struct ptp_clock_info *info, long scaled_ppm)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	u64 freq, divisor = 1000000ULL;
	struct ice_hw *hw = &pf->hw;
	s64 incval, diff;
	int neg_adj = 0;
	int err;

	incval = ICE_PTP_NOMINAL_INCVAL_E810;

	if (scaled_ppm < 0) {
		neg_adj = 1;
		scaled_ppm = -scaled_ppm;
	}

	while ((u64)scaled_ppm > div_u64(U64_MAX, incval)) {
		/* handle overflow by scaling down the scaled_ppm and
		 * the divisor, losing some precision
		 */
		scaled_ppm >>= 2;
		divisor >>= 2;
	}

	freq = (incval * (u64)scaled_ppm) >> 16;
	diff = div_u64(freq, divisor);

	if (neg_adj)
		incval -= diff;
	else
		incval += diff;

	err = ice_ptp_write_incval_locked(hw, incval);
	if (err) {
		dev_err(ice_pf_to_dev(pf), "PTP failed to set incval, err %d\n",
			err);
		return -EIO;
	}

	return 0;
}

/**
 * ice_ptp_gettimex64 - Get the time of the clock
 * @info: the driver's PTP info structure
 * @ts: timespec64 structure to hold the current time value
 * @sts: Optional parameter for holding a pair of system timestamps from
 *       the system clock. Will be ignored if NULL is given.
 *
 * Read the device clock and return the correct value on ns, after converting it
 * into a timespec struct.
 */
static int
ice_ptp_gettimex64(struct ptp_clock_info *info, struct timespec64 *ts,
		   struct ptp_system_timestamp *sts)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	struct ice_hw *hw = &pf->hw;

	if (!ice_ptp_lock(hw)) {
		dev_err(ice_pf_to_dev(pf), "PTP failed to get time\n");
		return -EBUSY;
	}

	ice_ptp_read_time(pf, ts, sts);
	ice_ptp_unlock(hw);

	return 0;
}

/**
 * ice_ptp_settime64 - Set the time of the clock
 * @info: the driver's PTP info structure
 * @ts: timespec64 structure that holds the new time value
 *
 * Set the device clock to the user input value. The conversion from timespec
 * to ns happens in the write function.
 */
static int
ice_ptp_settime64(struct ptp_clock_info *info, const struct timespec64 *ts)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	struct timespec64 ts64 = *ts;
	struct ice_hw *hw = &pf->hw;
	int err;

	if (!ice_ptp_lock(hw)) {
		err = -EBUSY;
		goto exit;
	}

	err = ice_ptp_write_init(pf, &ts64);
	ice_ptp_unlock(hw);

exit:
	if (err) {
		dev_err(ice_pf_to_dev(pf), "PTP failed to set time %d\n", err);
		return err;
	}

	return 0;
}

/**
 * ice_ptp_adjtime_nonatomic - Do a non-atomic clock adjustment
 * @info: the driver's PTP info structure
 * @delta: Offset in nanoseconds to adjust the time by
 */
static int ice_ptp_adjtime_nonatomic(struct ptp_clock_info *info, s64 delta)
{
	struct timespec64 now, then;

	then = ns_to_timespec64(delta);
	ice_ptp_gettimex64(info, &now, NULL);
	now = timespec64_add(now, then);

	return ice_ptp_settime64(info, (const struct timespec64 *)&now);
}

/**
 * ice_ptp_adjtime - Adjust the time of the clock by the indicated delta
 * @info: the driver's PTP info structure
 * @delta: Offset in nanoseconds to adjust the time by
 */
static int ice_ptp_adjtime(struct ptp_clock_info *info, s64 delta)
{
	struct ice_pf *pf = ptp_info_to_pf(info);
	struct ice_hw *hw = &pf->hw;
	struct device *dev;
	int err;

	dev = ice_pf_to_dev(pf);

	/* Hardware only supports atomic adjustments using signed 32-bit
	 * integers. For any adjustment outside this range, perform
	 * a non-atomic get->adjust->set flow.
	 */
	if (delta > S32_MAX || delta < S32_MIN) {
		dev_dbg(dev, "delta = %lld, adjtime non-atomic\n", delta);
		return ice_ptp_adjtime_nonatomic(info, delta);
	}

	if (!ice_ptp_lock(hw)) {
		dev_err(dev, "PTP failed to acquire semaphore in adjtime\n");
		return -EBUSY;
	}

	err = ice_ptp_write_adj(pf, delta);

	ice_ptp_unlock(hw);

	if (err) {
		dev_err(dev, "PTP failed to adjust time, err %d\n", err);
		return err;
	}

	return 0;
}

/**
 * ice_ptp_set_caps - Set PTP capabilities
 * @pf: Board private structure
 */
static void ice_ptp_set_caps(struct ice_pf *pf)
{
	struct ptp_clock_info *info = &pf->ptp.info;
	struct device *dev = ice_pf_to_dev(pf);

	snprintf(info->name, sizeof(info->name) - 1, "%s-%s-clk",
		 dev_driver_string(dev), dev_name(dev));
	info->owner = THIS_MODULE;
	info->max_adj = 999999999;
	info->adjtime = ice_ptp_adjtime;
	info->adjfine = ice_ptp_adjfine;
	info->gettimex64 = ice_ptp_gettimex64;
	info->settime64 = ice_ptp_settime64;
}

/**
 * ice_ptp_create_clock - Create PTP clock device for userspace
 * @pf: Board private structure
 *
 * This function creates a new PTP clock device. It only creates one if we
 * don't already have one. Will return error if it can't create one, but success
 * if we already have a device. Should be used by ice_ptp_init to create clock
 * initially, and prevent global resets from creating new clock devices.
 */
static long ice_ptp_create_clock(struct ice_pf *pf)
{
	struct ptp_clock_info *info;
	struct ptp_clock *clock;
	struct device *dev;

	/* No need to create a clock device if we already have one */
	if (pf->ptp.clock)
		return 0;

	ice_ptp_set_caps(pf);

	info = &pf->ptp.info;
	dev = ice_pf_to_dev(pf);

	/* Attempt to register the clock before enabling the hardware. */
	clock = ptp_clock_register(info, dev);
	if (IS_ERR(clock))
		return PTR_ERR(clock);

	pf->ptp.clock = clock;

	return 0;
}

/**
 * ice_ptp_init_owner - Initialize PTP_1588_CLOCK device
 * @pf: Board private structure
 *
 * Setup and initialize a PTP clock device that represents the device hardware
 * clock. Save the clock index for other functions connected to the same
 * hardware resource.
 */
static int ice_ptp_init_owner(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	struct timespec64 ts;
	u8 src_idx;
	int err;

	wr32(hw, GLTSYN_SYNC_DLAY, 0);

	/* Clear some HW residue and enable source clock */
	src_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Enable source clocks */
	wr32(hw, GLTSYN_ENA(src_idx), GLTSYN_ENA_TSYN_ENA_M);

	/* Enable PHY time sync */
	err = ice_ptp_init_phy_e810(hw);
	if (err)
		goto err_exit;

	/* Clear event status indications for auxiliary pins */
	(void)rd32(hw, GLTSYN_STAT(src_idx));

	/* Acquire the global hardware lock */
	if (!ice_ptp_lock(hw)) {
		err = -EBUSY;
		goto err_exit;
	}

	/* Write the increment time value to PHY and LAN */
	err = ice_ptp_write_incval(hw, ICE_PTP_NOMINAL_INCVAL_E810);
	if (err) {
		ice_ptp_unlock(hw);
		goto err_exit;
	}

	ts = ktime_to_timespec64(ktime_get_real());
	/* Write the initial Time value to PHY and LAN */
	err = ice_ptp_write_init(pf, &ts);
	if (err) {
		ice_ptp_unlock(hw);
		goto err_exit;
	}

	/* Release the global hardware lock */
	ice_ptp_unlock(hw);

	/* Ensure we have a clock device */
	err = ice_ptp_create_clock(pf);
	if (err)
		goto err_clk;

	return 0;

err_clk:
	pf->ptp.clock = NULL;
err_exit:
	dev_err(dev, "PTP failed to register clock, err %d\n", err);

	return err;
}

/**
 * ice_ptp_init - Initialize the PTP support after device probe or reset
 * @pf: Board private structure
 *
 * This function sets device up for PTP support. The first time it is run, it
 * will create a clock device. It does not create a clock device if one
 * already exists. It also reconfigures the device after a reset.
 */
void ice_ptp_init(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	int err;

	/* PTP is currently only supported on E810 devices */
	if (!ice_is_e810(hw))
		return;

	/* Check if this PF owns the source timer */
	if (hw->func_caps.ts_func_info.src_tmr_owned) {
		err = ice_ptp_init_owner(pf);
		if (err)
			return;
	}

	set_bit(ICE_FLAG_PTP, pf->flags);

	dev_info(dev, "PTP init successful\n");
}

/**
 * ice_ptp_release - Disable the driver/HW support and unregister the clock
 * @pf: Board private structure
 *
 * This function handles the cleanup work required from the initialization by
 * clearing out the important information and unregistering the clock
 */
void ice_ptp_release(struct ice_pf *pf)
{
	clear_bit(ICE_FLAG_PTP, pf->flags);

	if (!pf->ptp.clock)
		return;

	ptp_clock_unregister(pf->ptp.clock);
	pf->ptp.clock = NULL;

	dev_info(ice_pf_to_dev(pf), "Removed PTP clock\n");
}

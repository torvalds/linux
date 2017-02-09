/*
 * Copyright (c) 2012 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/mlx4/device.h>
#include <linux/clocksource.h>

#include "mlx4_en.h"

/* mlx4_en_read_clock - read raw cycle counter (to be used by time counter)
 */
static u64 mlx4_en_read_clock(const struct cyclecounter *tc)
{
	struct mlx4_en_dev *mdev =
		container_of(tc, struct mlx4_en_dev, cycles);
	struct mlx4_dev *dev = mdev->dev;

	return mlx4_read_clock(dev) & tc->mask;
}

u64 mlx4_en_get_cqe_ts(struct mlx4_cqe *cqe)
{
	u64 hi, lo;
	struct mlx4_ts_cqe *ts_cqe = (struct mlx4_ts_cqe *)cqe;

	lo = (u64)be16_to_cpu(ts_cqe->timestamp_lo);
	hi = ((u64)be32_to_cpu(ts_cqe->timestamp_hi) + !lo) << 16;

	return hi | lo;
}

void mlx4_en_fill_hwtstamps(struct mlx4_en_dev *mdev,
			    struct skb_shared_hwtstamps *hwts,
			    u64 timestamp)
{
	unsigned int seq;
	u64 nsec;

	do {
		seq = read_seqbegin(&mdev->clock_lock);
		nsec = timecounter_cyc2time(&mdev->clock, timestamp);
	} while (read_seqretry(&mdev->clock_lock, seq));

	memset(hwts, 0, sizeof(struct skb_shared_hwtstamps));
	hwts->hwtstamp = ns_to_ktime(nsec);
}

/**
 * mlx4_en_remove_timestamp - disable PTP device
 * @mdev: board private structure
 *
 * Stop the PTP support.
 **/
void mlx4_en_remove_timestamp(struct mlx4_en_dev *mdev)
{
	if (mdev->ptp_clock) {
		ptp_clock_unregister(mdev->ptp_clock);
		mdev->ptp_clock = NULL;
		mlx4_info(mdev, "removed PHC\n");
	}
}

void mlx4_en_ptp_overflow_check(struct mlx4_en_dev *mdev)
{
	bool timeout = time_is_before_jiffies(mdev->last_overflow_check +
					      mdev->overflow_period);
	unsigned long flags;

	if (timeout) {
		write_seqlock_irqsave(&mdev->clock_lock, flags);
		timecounter_read(&mdev->clock);
		write_sequnlock_irqrestore(&mdev->clock_lock, flags);
		mdev->last_overflow_check = jiffies;
	}
}

/**
 * mlx4_en_phc_adjfreq - adjust the frequency of the hardware clock
 * @ptp: ptp clock structure
 * @delta: Desired frequency change in parts per billion
 *
 * Adjust the frequency of the PHC cycle counter by the indicated delta from
 * the base frequency.
 **/
static int mlx4_en_phc_adjfreq(struct ptp_clock_info *ptp, s32 delta)
{
	u64 adj;
	u32 diff, mult;
	int neg_adj = 0;
	unsigned long flags;
	struct mlx4_en_dev *mdev = container_of(ptp, struct mlx4_en_dev,
						ptp_clock_info);

	if (delta < 0) {
		neg_adj = 1;
		delta = -delta;
	}
	mult = mdev->nominal_c_mult;
	adj = mult;
	adj *= delta;
	diff = div_u64(adj, 1000000000ULL);

	write_seqlock_irqsave(&mdev->clock_lock, flags);
	timecounter_read(&mdev->clock);
	mdev->cycles.mult = neg_adj ? mult - diff : mult + diff;
	write_sequnlock_irqrestore(&mdev->clock_lock, flags);

	return 0;
}

/**
 * mlx4_en_phc_adjtime - Shift the time of the hardware clock
 * @ptp: ptp clock structure
 * @delta: Desired change in nanoseconds
 *
 * Adjust the timer by resetting the timecounter structure.
 **/
static int mlx4_en_phc_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct mlx4_en_dev *mdev = container_of(ptp, struct mlx4_en_dev,
						ptp_clock_info);
	unsigned long flags;

	write_seqlock_irqsave(&mdev->clock_lock, flags);
	timecounter_adjtime(&mdev->clock, delta);
	write_sequnlock_irqrestore(&mdev->clock_lock, flags);

	return 0;
}

/**
 * mlx4_en_phc_gettime - Reads the current time from the hardware clock
 * @ptp: ptp clock structure
 * @ts: timespec structure to hold the current time value
 *
 * Read the timecounter and return the correct value in ns after converting
 * it into a struct timespec.
 **/
static int mlx4_en_phc_gettime(struct ptp_clock_info *ptp,
			       struct timespec64 *ts)
{
	struct mlx4_en_dev *mdev = container_of(ptp, struct mlx4_en_dev,
						ptp_clock_info);
	unsigned long flags;
	u64 ns;

	write_seqlock_irqsave(&mdev->clock_lock, flags);
	ns = timecounter_read(&mdev->clock);
	write_sequnlock_irqrestore(&mdev->clock_lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

/**
 * mlx4_en_phc_settime - Set the current time on the hardware clock
 * @ptp: ptp clock structure
 * @ts: timespec containing the new time for the cycle counter
 *
 * Reset the timecounter to use a new base value instead of the kernel
 * wall timer value.
 **/
static int mlx4_en_phc_settime(struct ptp_clock_info *ptp,
			       const struct timespec64 *ts)
{
	struct mlx4_en_dev *mdev = container_of(ptp, struct mlx4_en_dev,
						ptp_clock_info);
	u64 ns = timespec64_to_ns(ts);
	unsigned long flags;

	/* reset the timecounter */
	write_seqlock_irqsave(&mdev->clock_lock, flags);
	timecounter_init(&mdev->clock, &mdev->cycles, ns);
	write_sequnlock_irqrestore(&mdev->clock_lock, flags);

	return 0;
}

/**
 * mlx4_en_phc_enable - enable or disable an ancillary feature
 * @ptp: ptp clock structure
 * @request: Desired resource to enable or disable
 * @on: Caller passes one to enable or zero to disable
 *
 * Enable (or disable) ancillary features of the PHC subsystem.
 * Currently, no ancillary features are supported.
 **/
static int mlx4_en_phc_enable(struct ptp_clock_info __always_unused *ptp,
			      struct ptp_clock_request __always_unused *request,
			      int __always_unused on)
{
	return -EOPNOTSUPP;
}

static const struct ptp_clock_info mlx4_en_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.max_adj	= 100000000,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfreq	= mlx4_en_phc_adjfreq,
	.adjtime	= mlx4_en_phc_adjtime,
	.gettime64	= mlx4_en_phc_gettime,
	.settime64	= mlx4_en_phc_settime,
	.enable		= mlx4_en_phc_enable,
};

#define MLX4_EN_WRAP_AROUND_SEC	10ULL

/* This function calculates the max shift that enables the user range
 * of MLX4_EN_WRAP_AROUND_SEC values in the cycles register.
 */
static u32 freq_to_shift(u16 freq)
{
	u32 freq_khz = freq * 1000;
	u64 max_val_cycles = freq_khz * 1000 * MLX4_EN_WRAP_AROUND_SEC;
	u64 max_val_cycles_rounded = 1ULL << fls64(max_val_cycles - 1);
	/* calculate max possible multiplier in order to fit in 64bit */
	u64 max_mul = div64_u64(ULLONG_MAX, max_val_cycles_rounded);

	/* This comes from the reverse of clocksource_khz2mult */
	return ilog2(div_u64(max_mul * freq_khz, 1000000));
}

void mlx4_en_init_timestamp(struct mlx4_en_dev *mdev)
{
	struct mlx4_dev *dev = mdev->dev;
	unsigned long flags;
	u64 ns, zero = 0;

	/* mlx4_en_init_timestamp is called for each netdev.
	 * mdev->ptp_clock is common for all ports, skip initialization if
	 * was done for other port.
	 */
	if (mdev->ptp_clock)
		return;

	seqlock_init(&mdev->clock_lock);

	memset(&mdev->cycles, 0, sizeof(mdev->cycles));
	mdev->cycles.read = mlx4_en_read_clock;
	mdev->cycles.mask = CLOCKSOURCE_MASK(48);
	mdev->cycles.shift = freq_to_shift(dev->caps.hca_core_clock);
	mdev->cycles.mult =
		clocksource_khz2mult(1000 * dev->caps.hca_core_clock, mdev->cycles.shift);
	mdev->nominal_c_mult = mdev->cycles.mult;

	write_seqlock_irqsave(&mdev->clock_lock, flags);
	timecounter_init(&mdev->clock, &mdev->cycles,
			 ktime_to_ns(ktime_get_real()));
	write_sequnlock_irqrestore(&mdev->clock_lock, flags);

	/* Calculate period in seconds to call the overflow watchdog - to make
	 * sure counter is checked at least once every wrap around.
	 */
	ns = cyclecounter_cyc2ns(&mdev->cycles, mdev->cycles.mask, zero, &zero);
	do_div(ns, NSEC_PER_SEC / 2 / HZ);
	mdev->overflow_period = ns;

	/* Configure the PHC */
	mdev->ptp_clock_info = mlx4_en_ptp_clock_info;
	snprintf(mdev->ptp_clock_info.name, 16, "mlx4 ptp");

	mdev->ptp_clock = ptp_clock_register(&mdev->ptp_clock_info,
					     &mdev->pdev->dev);
	if (IS_ERR(mdev->ptp_clock)) {
		mdev->ptp_clock = NULL;
		mlx4_err(mdev, "ptp_clock_register failed\n");
	} else if (mdev->ptp_clock) {
		mlx4_info(mdev, "registered PHC clock\n");
	}

}

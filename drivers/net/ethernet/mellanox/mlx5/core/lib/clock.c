/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
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
 */

#include <linux/clocksource.h>
#include <linux/highmem.h>
#include <linux/log2.h>
#include <linux/ptp_clock_kernel.h>
#include <rdma/mlx5-abi.h>
#include "lib/eq.h"
#include "en.h"
#include "clock.h"
#ifdef CONFIG_X86
#include <linux/timekeeping.h>
#include <linux/cpufeature.h>
#endif /* CONFIG_X86 */

#define MLX5_RT_CLOCK_IDENTITY_SIZE MLX5_FLD_SZ_BYTES(mrtcq_reg, rt_clock_identity)

enum {
	MLX5_PIN_MODE_IN		= 0x0,
	MLX5_PIN_MODE_OUT		= 0x1,
};

enum {
	MLX5_OUT_PATTERN_PULSE		= 0x0,
	MLX5_OUT_PATTERN_PERIODIC	= 0x1,
};

enum {
	MLX5_EVENT_MODE_DISABLE	= 0x0,
	MLX5_EVENT_MODE_REPETETIVE	= 0x1,
	MLX5_EVENT_MODE_ONCE_TILL_ARM	= 0x2,
};

enum {
	MLX5_MTPPS_FS_ENABLE			= BIT(0x0),
	MLX5_MTPPS_FS_PATTERN			= BIT(0x2),
	MLX5_MTPPS_FS_PIN_MODE			= BIT(0x3),
	MLX5_MTPPS_FS_TIME_STAMP		= BIT(0x4),
	MLX5_MTPPS_FS_OUT_PULSE_DURATION	= BIT(0x5),
	MLX5_MTPPS_FS_ENH_OUT_PER_ADJ		= BIT(0x7),
	MLX5_MTPPS_FS_NPPS_PERIOD               = BIT(0x9),
	MLX5_MTPPS_FS_OUT_PULSE_DURATION_NS     = BIT(0xa),
};

enum {
	MLX5_MTUTC_OPERATION_ADJUST_TIME_MIN          = S16_MIN,
	MLX5_MTUTC_OPERATION_ADJUST_TIME_MAX          = S16_MAX,
	MLX5_MTUTC_OPERATION_ADJUST_TIME_EXTENDED_MIN = -200000,
	MLX5_MTUTC_OPERATION_ADJUST_TIME_EXTENDED_MAX = 200000,
};

struct mlx5_clock_dev_state {
	struct mlx5_core_dev *mdev;
	struct mlx5_devcom_comp_dev *compdev;
	struct mlx5_nb pps_nb;
	struct work_struct out_work;
};

struct mlx5_clock_priv {
	struct mlx5_clock clock;
	struct mlx5_core_dev *mdev;
	struct mutex lock; /* protect mdev and used in PTP callbacks */
	struct mlx5_core_dev *event_mdev;
};

static struct mlx5_clock_priv *clock_priv(struct mlx5_clock *clock)
{
	return container_of(clock, struct mlx5_clock_priv, clock);
}

static void mlx5_clock_lockdep_assert(struct mlx5_clock *clock)
{
	if (!clock->shared)
		return;

	lockdep_assert(lockdep_is_held(&clock_priv(clock)->lock));
}

static struct mlx5_core_dev *mlx5_clock_mdev_get(struct mlx5_clock *clock)
{
	mlx5_clock_lockdep_assert(clock);

	return clock_priv(clock)->mdev;
}

static void mlx5_clock_lock(struct mlx5_clock *clock)
{
	if (!clock->shared)
		return;

	mutex_lock(&clock_priv(clock)->lock);
}

static void mlx5_clock_unlock(struct mlx5_clock *clock)
{
	if (!clock->shared)
		return;

	mutex_unlock(&clock_priv(clock)->lock);
}

static bool mlx5_real_time_mode(struct mlx5_core_dev *mdev)
{
	return (mlx5_is_real_time_rq(mdev) || mlx5_is_real_time_sq(mdev));
}

static bool mlx5_npps_real_time_supported(struct mlx5_core_dev *mdev)
{
	return (mlx5_real_time_mode(mdev) &&
		MLX5_CAP_MCAM_FEATURE(mdev, npps_period) &&
		MLX5_CAP_MCAM_FEATURE(mdev, out_pulse_duration_ns));
}

static bool mlx5_modify_mtutc_allowed(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_MCAM_FEATURE(mdev, ptpcyc2realtime_modify);
}

static int mlx5_clock_identity_get(struct mlx5_core_dev *mdev,
				   u8 identify[MLX5_RT_CLOCK_IDENTITY_SIZE])
{
	u32 out[MLX5_ST_SZ_DW(mrtcq_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(mrtcq_reg)] = {};
	int err;

	err = mlx5_core_access_reg(mdev, in, sizeof(in),
				   out, sizeof(out), MLX5_REG_MRTCQ, 0, 0);
	if (!err)
		memcpy(identify, MLX5_ADDR_OF(mrtcq_reg, out, rt_clock_identity),
		       MLX5_RT_CLOCK_IDENTITY_SIZE);

	return err;
}

static u32 mlx5_ptp_shift_constant(u32 dev_freq_khz)
{
	/* Optimal shift constant leads to corrections above just 1 scaled ppm.
	 *
	 * Two sets of equations are needed to derive the optimal shift
	 * constant for the cyclecounter.
	 *
	 *    dev_freq_khz * 1000 / 2^shift_constant = 1 scaled_ppm
	 *    ppb = scaled_ppm * 1000 / 2^16
	 *
	 * Using the two equations together
	 *
	 *    dev_freq_khz * 1000 / 1 scaled_ppm = 2^shift_constant
	 *    dev_freq_khz * 2^16 / 1 ppb = 2^shift_constant
	 *    dev_freq_khz = 2^(shift_constant - 16)
	 *
	 * then yields
	 *
	 *    shift_constant = ilog2(dev_freq_khz) + 16
	 */

	return min(ilog2(dev_freq_khz) + 16,
		   ilog2((U32_MAX / NSEC_PER_MSEC) * dev_freq_khz));
}

static s32 mlx5_clock_getmaxphase(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_MCAM_FEATURE(mdev, mtutc_time_adjustment_extended_range) ?
		       MLX5_MTUTC_OPERATION_ADJUST_TIME_EXTENDED_MAX :
			     MLX5_MTUTC_OPERATION_ADJUST_TIME_MAX;
}

static s32 mlx5_ptp_getmaxphase(struct ptp_clock_info *ptp)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock, ptp_info);
	struct mlx5_core_dev *mdev;
	s32 ret;

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);
	ret = mlx5_clock_getmaxphase(mdev);
	mlx5_clock_unlock(clock);

	return ret;
}

static bool mlx5_is_mtutc_time_adj_cap(struct mlx5_core_dev *mdev, s64 delta)
{
	s64 max = mlx5_clock_getmaxphase(mdev);

	if (delta < -max || delta > max)
		return false;

	return true;
}

static int mlx5_set_mtutc(struct mlx5_core_dev *dev, u32 *mtutc, u32 size)
{
	u32 out[MLX5_ST_SZ_DW(mtutc_reg)] = {};

	if (!MLX5_CAP_MCAM_REG(dev, mtutc))
		return -EOPNOTSUPP;

	return mlx5_core_access_reg(dev, mtutc, size, out, sizeof(out),
				    MLX5_REG_MTUTC, 0, 1);
}

#ifdef CONFIG_X86
static bool mlx5_is_ptm_source_time_available(struct mlx5_core_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(mtptm_reg)] = {0};
	u32 in[MLX5_ST_SZ_DW(mtptm_reg)] = {0};
	int err;

	if (!MLX5_CAP_MCAM_REG3(dev, mtptm))
		return false;

	err = mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out), MLX5_REG_MTPTM,
				   0, 0);
	if (err)
		return false;

	return !!MLX5_GET(mtptm_reg, out, psta);
}

static int mlx5_mtctr_read(struct mlx5_core_dev *mdev,
			   bool real_time_mode,
			   struct system_counterval_t *sys_counterval,
			   u64 *device)
{
	u32 out[MLX5_ST_SZ_DW(mtctr_reg)] = {0};
	u32 in[MLX5_ST_SZ_DW(mtctr_reg)] = {0};
	u64 host;
	int err;

	MLX5_SET(mtctr_reg, in, first_clock_timestamp_request,
		 MLX5_MTCTR_REQUEST_PTM_ROOT_CLOCK);
	MLX5_SET(mtctr_reg, in, second_clock_timestamp_request,
		 real_time_mode ? MLX5_MTCTR_REQUEST_REAL_TIME_CLOCK :
				  MLX5_MTCTR_REQUEST_FREE_RUNNING_COUNTER);

	err = mlx5_core_access_reg(mdev, in, sizeof(in), out, sizeof(out),
				   MLX5_REG_MTCTR, 0, 0);
	if (err)
		return err;

	if (!MLX5_GET(mtctr_reg, out, first_clock_valid) ||
	    !MLX5_GET(mtctr_reg, out, second_clock_valid))
		return -EINVAL;

	host = MLX5_GET64(mtctr_reg, out, first_clock_timestamp);
	*sys_counterval = (struct system_counterval_t) {
			.cycles = host,
			.cs_id = CSID_X86_ART,
			.use_nsecs = true,
	};
	*device = MLX5_GET64(mtctr_reg, out, second_clock_timestamp);

	return 0;
}

static int mlx5_mtctr_syncdevicetime(ktime_t *device_time,
				     struct system_counterval_t *sys_counterval,
				     void *ctx)
{
	struct mlx5_core_dev *mdev = ctx;
	bool real_time_mode;
	u64 device;
	int err;

	real_time_mode = mlx5_real_time_mode(mdev);

	err = mlx5_mtctr_read(mdev, real_time_mode, sys_counterval, &device);
	if (err)
		return err;

	if (real_time_mode)
		*device_time = ns_to_ktime(REAL_TIME_TO_NS(device >> 32, device & U32_MAX));
	else
		*device_time = mlx5_timecounter_cyc2time(mdev->clock, device);

	return 0;
}

static int
mlx5_mtctr_syncdevicecyclestime(ktime_t *device_time,
				struct system_counterval_t *sys_counterval,
				void *ctx)
{
	struct mlx5_core_dev *mdev = ctx;
	u64 device;
	int err;

	err = mlx5_mtctr_read(mdev, false, sys_counterval, &device);
	if (err)
		return err;
	*device_time = ns_to_ktime(device);

	return 0;
}

static int mlx5_ptp_getcrosststamp(struct ptp_clock_info *ptp,
				   struct system_device_crosststamp *cts)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock, ptp_info);
	struct system_time_snapshot history_begin = {0};
	struct mlx5_core_dev *mdev;
	int err;

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);

	if (!mlx5_is_ptm_source_time_available(mdev)) {
		err = -EBUSY;
		goto unlock;
	}

	ktime_get_snapshot(&history_begin);

	err = get_device_system_crosststamp(mlx5_mtctr_syncdevicetime, mdev,
					    &history_begin, cts);
unlock:
	mlx5_clock_unlock(clock);
	return err;
}

static int mlx5_ptp_getcrosscycles(struct ptp_clock_info *ptp,
				   struct system_device_crosststamp *cts)
{
	struct mlx5_clock *clock =
		container_of(ptp, struct mlx5_clock, ptp_info);
	struct system_time_snapshot history_begin = {0};
	struct mlx5_core_dev *mdev;
	int err;

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);

	if (!mlx5_is_ptm_source_time_available(mdev)) {
		err = -EBUSY;
		goto unlock;
	}

	ktime_get_snapshot(&history_begin);

	err = get_device_system_crosststamp(mlx5_mtctr_syncdevicecyclestime,
					    mdev, &history_begin, cts);
unlock:
	mlx5_clock_unlock(clock);
	return err;
}
#endif /* CONFIG_X86 */

static u64 mlx5_read_time(struct mlx5_core_dev *dev,
			  struct ptp_system_timestamp *sts,
			  bool real_time)
{
	u32 timer_h, timer_h1, timer_l;

	timer_h = ioread32be(real_time ? &dev->iseg->real_time_h :
			     &dev->iseg->internal_timer_h);
	ptp_read_system_prets(sts);
	timer_l = ioread32be(real_time ? &dev->iseg->real_time_l :
			     &dev->iseg->internal_timer_l);
	ptp_read_system_postts(sts);
	timer_h1 = ioread32be(real_time ? &dev->iseg->real_time_h :
			      &dev->iseg->internal_timer_h);
	if (timer_h != timer_h1) {
		/* wrap around */
		ptp_read_system_prets(sts);
		timer_l = ioread32be(real_time ? &dev->iseg->real_time_l :
				     &dev->iseg->internal_timer_l);
		ptp_read_system_postts(sts);
	}

	return real_time ? REAL_TIME_TO_NS(timer_h1, timer_l) :
			   (u64)timer_l | (u64)timer_h1 << 32;
}

static u64 read_internal_timer(struct cyclecounter *cc)
{
	struct mlx5_timer *timer = container_of(cc, struct mlx5_timer, cycles);
	struct mlx5_clock *clock = container_of(timer, struct mlx5_clock, timer);
	struct mlx5_core_dev *mdev = mlx5_clock_mdev_get(clock);

	return mlx5_read_time(mdev, NULL, false) & cc->mask;
}

static void mlx5_update_clock_info_page(struct mlx5_core_dev *mdev)
{
	struct mlx5_ib_clock_info *clock_info = mdev->clock_info;
	struct mlx5_clock *clock = mdev->clock;
	struct mlx5_timer *timer;
	u32 sign;

	if (!clock_info)
		return;

	sign = smp_load_acquire(&clock_info->sign);
	smp_store_mb(clock_info->sign,
		     sign | MLX5_IB_CLOCK_INFO_KERNEL_UPDATING);

	timer = &clock->timer;
	clock_info->cycles = timer->tc.cycle_last;
	clock_info->mult   = timer->cycles.mult;
	clock_info->nsec   = timer->tc.nsec;
	clock_info->frac   = timer->tc.frac;

	smp_store_release(&clock_info->sign,
			  sign + MLX5_IB_CLOCK_INFO_KERNEL_UPDATING * 2);
}

static void mlx5_pps_out(struct work_struct *work)
{
	struct mlx5_clock_dev_state *clock_state = container_of(work, struct mlx5_clock_dev_state,
								out_work);
	struct mlx5_core_dev *mdev = clock_state->mdev;
	struct mlx5_clock *clock = mdev->clock;
	u32 in[MLX5_ST_SZ_DW(mtpps_reg)] = {0};
	unsigned long flags;
	int i;

	for (i = 0; i < clock->ptp_info.n_pins; i++) {
		u64 tstart;

		write_seqlock_irqsave(&clock->lock, flags);
		tstart = clock->pps_info.start[i];
		clock->pps_info.start[i] = 0;
		write_sequnlock_irqrestore(&clock->lock, flags);
		if (!tstart)
			continue;

		MLX5_SET(mtpps_reg, in, pin, i);
		MLX5_SET64(mtpps_reg, in, time_stamp, tstart);
		MLX5_SET(mtpps_reg, in, field_select, MLX5_MTPPS_FS_TIME_STAMP);
		mlx5_set_mtpps(mdev, in, sizeof(in));
	}
}

static long mlx5_timestamp_overflow(struct ptp_clock_info *ptp_info)
{
	struct mlx5_core_dev *mdev;
	struct mlx5_timer *timer;
	struct mlx5_clock *clock;
	unsigned long flags;

	clock = container_of(ptp_info, struct mlx5_clock, ptp_info);
	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);
	timer = &clock->timer;

	if (mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR)
		goto out;

	write_seqlock_irqsave(&clock->lock, flags);
	timecounter_read(&timer->tc);
	mlx5_update_clock_info_page(mdev);
	write_sequnlock_irqrestore(&clock->lock, flags);

out:
	mlx5_clock_unlock(clock);
	return timer->overflow_period;
}

static int mlx5_ptp_settime_real_time(struct mlx5_core_dev *mdev,
				      const struct timespec64 *ts)
{
	u32 in[MLX5_ST_SZ_DW(mtutc_reg)] = {};

	if (ts->tv_sec < 0 || ts->tv_sec > U32_MAX ||
	    ts->tv_nsec < 0 || ts->tv_nsec > NSEC_PER_SEC)
		return -EINVAL;

	MLX5_SET(mtutc_reg, in, operation, MLX5_MTUTC_OPERATION_SET_TIME_IMMEDIATE);
	MLX5_SET(mtutc_reg, in, utc_sec, ts->tv_sec);
	MLX5_SET(mtutc_reg, in, utc_nsec, ts->tv_nsec);

	return mlx5_set_mtutc(mdev, in, sizeof(in));
}

static int mlx5_clock_settime(struct mlx5_core_dev *mdev, struct mlx5_clock *clock,
			      const struct timespec64 *ts)
{
	struct mlx5_timer *timer = &clock->timer;
	unsigned long flags;

	if (mlx5_modify_mtutc_allowed(mdev)) {
		int err = mlx5_ptp_settime_real_time(mdev, ts);

		if (err)
			return err;
	}

	write_seqlock_irqsave(&clock->lock, flags);
	timecounter_init(&timer->tc, &timer->cycles, timespec64_to_ns(ts));
	mlx5_update_clock_info_page(mdev);
	write_sequnlock_irqrestore(&clock->lock, flags);

	return 0;
}

static int mlx5_ptp_settime(struct ptp_clock_info *ptp, const struct timespec64 *ts)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock, ptp_info);
	struct mlx5_core_dev *mdev;
	int err;

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);
	err = mlx5_clock_settime(mdev, clock, ts);
	mlx5_clock_unlock(clock);

	return err;
}

static
struct timespec64 mlx5_ptp_gettimex_real_time(struct mlx5_core_dev *mdev,
					      struct ptp_system_timestamp *sts)
{
	struct timespec64 ts;
	u64 time;

	time = mlx5_read_time(mdev, sts, true);
	ts = ns_to_timespec64(time);
	return ts;
}

static int mlx5_ptp_gettimex(struct ptp_clock_info *ptp, struct timespec64 *ts,
			     struct ptp_system_timestamp *sts)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock, ptp_info);
	struct mlx5_core_dev *mdev;
	u64 cycles, ns;

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);
	if (mlx5_real_time_mode(mdev)) {
		*ts = mlx5_ptp_gettimex_real_time(mdev, sts);
		goto out;
	}

	cycles = mlx5_read_time(mdev, sts, false);
	ns = mlx5_timecounter_cyc2time(clock, cycles);
	*ts = ns_to_timespec64(ns);
out:
	mlx5_clock_unlock(clock);
	return 0;
}

static int mlx5_ptp_getcyclesx(struct ptp_clock_info *ptp,
			       struct timespec64 *ts,
			       struct ptp_system_timestamp *sts)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock,
						ptp_info);
	struct mlx5_core_dev *mdev;
	u64 cycles;

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);

	cycles = mlx5_read_time(mdev, sts, false);
	*ts = ns_to_timespec64(cycles);
	mlx5_clock_unlock(clock);
	return 0;
}

static int mlx5_ptp_adjtime_real_time(struct mlx5_core_dev *mdev, s64 delta)
{
	u32 in[MLX5_ST_SZ_DW(mtutc_reg)] = {};

	/* HW time adjustment range is checked. If out of range, settime instead */
	if (!mlx5_is_mtutc_time_adj_cap(mdev, delta)) {
		struct timespec64 ts;
		s64 ns;

		ts = mlx5_ptp_gettimex_real_time(mdev, NULL);
		ns = timespec64_to_ns(&ts) + delta;
		ts = ns_to_timespec64(ns);
		return mlx5_ptp_settime_real_time(mdev, &ts);
	}

	MLX5_SET(mtutc_reg, in, operation, MLX5_MTUTC_OPERATION_ADJUST_TIME);
	MLX5_SET(mtutc_reg, in, time_adjustment, delta);

	return mlx5_set_mtutc(mdev, in, sizeof(in));
}

static int mlx5_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock, ptp_info);
	struct mlx5_timer *timer = &clock->timer;
	struct mlx5_core_dev *mdev;
	unsigned long flags;
	int err = 0;

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);

	if (mlx5_modify_mtutc_allowed(mdev)) {
		err = mlx5_ptp_adjtime_real_time(mdev, delta);

		if (err)
			goto unlock;
	}

	write_seqlock_irqsave(&clock->lock, flags);
	timecounter_adjtime(&timer->tc, delta);
	mlx5_update_clock_info_page(mdev);
	write_sequnlock_irqrestore(&clock->lock, flags);

unlock:
	mlx5_clock_unlock(clock);
	return err;
}

static int mlx5_ptp_adjphase(struct ptp_clock_info *ptp, s32 delta)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock, ptp_info);
	struct mlx5_core_dev *mdev;
	int err;

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);
	err = mlx5_ptp_adjtime_real_time(mdev, delta);
	mlx5_clock_unlock(clock);

	return err;
}

static int mlx5_ptp_freq_adj_real_time(struct mlx5_core_dev *mdev, long scaled_ppm)
{
	u32 in[MLX5_ST_SZ_DW(mtutc_reg)] = {};

	MLX5_SET(mtutc_reg, in, operation, MLX5_MTUTC_OPERATION_ADJUST_FREQ_UTC);

	if (MLX5_CAP_MCAM_FEATURE(mdev, mtutc_freq_adj_units) &&
	    scaled_ppm <= S32_MAX && scaled_ppm >= S32_MIN) {
		/* HW scaled_ppm support on mlx5 devices only supports a 32-bit value */
		MLX5_SET(mtutc_reg, in, freq_adj_units,
			 MLX5_MTUTC_FREQ_ADJ_UNITS_SCALED_PPM);
		MLX5_SET(mtutc_reg, in, freq_adjustment, (s32)scaled_ppm);
	} else {
		MLX5_SET(mtutc_reg, in, freq_adj_units, MLX5_MTUTC_FREQ_ADJ_UNITS_PPB);
		MLX5_SET(mtutc_reg, in, freq_adjustment, scaled_ppm_to_ppb(scaled_ppm));
	}

	return mlx5_set_mtutc(mdev, in, sizeof(in));
}

static int mlx5_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock, ptp_info);
	struct mlx5_timer *timer = &clock->timer;
	struct mlx5_core_dev *mdev;
	unsigned long flags;
	int err = 0;
	u32 mult;

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);

	if (mlx5_modify_mtutc_allowed(mdev)) {
		err = mlx5_ptp_freq_adj_real_time(mdev, scaled_ppm);

		if (err)
			goto unlock;
	}

	mult = (u32)adjust_by_scaled_ppm(timer->nominal_c_mult, scaled_ppm);

	write_seqlock_irqsave(&clock->lock, flags);
	timecounter_read(&timer->tc);
	timer->cycles.mult = mult;
	mlx5_update_clock_info_page(mdev);
	write_sequnlock_irqrestore(&clock->lock, flags);
	ptp_schedule_worker(clock->ptp, timer->overflow_period);

unlock:
	mlx5_clock_unlock(clock);
	return err;
}

static int mlx5_extts_configure(struct ptp_clock_info *ptp,
				struct ptp_clock_request *rq,
				int on)
{
	struct mlx5_clock *clock =
			container_of(ptp, struct mlx5_clock, ptp_info);
	u32 in[MLX5_ST_SZ_DW(mtpps_reg)] = {0};
	struct mlx5_core_dev *mdev;
	u32 field_select = 0;
	u8 pin_mode = 0;
	u8 pattern = 0;
	int pin = -1;
	int err = 0;

	/* Reject requests to enable time stamping on both edges. */
	if ((rq->extts.flags & PTP_STRICT_FLAGS) &&
	    (rq->extts.flags & PTP_ENABLE_FEATURE) &&
	    (rq->extts.flags & PTP_EXTTS_EDGES) == PTP_EXTTS_EDGES)
		return -EOPNOTSUPP;

	if (rq->extts.index >= clock->ptp_info.n_pins)
		return -EINVAL;

	pin = ptp_find_pin(clock->ptp, PTP_PF_EXTTS, rq->extts.index);
	if (pin < 0)
		return -EBUSY;

	if (on) {
		pin_mode = MLX5_PIN_MODE_IN;
		pattern = !!(rq->extts.flags & PTP_FALLING_EDGE);
		field_select = MLX5_MTPPS_FS_PIN_MODE |
			       MLX5_MTPPS_FS_PATTERN |
			       MLX5_MTPPS_FS_ENABLE;
	} else {
		field_select = MLX5_MTPPS_FS_ENABLE;
	}

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);

	if (!MLX5_PPS_CAP(mdev)) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	MLX5_SET(mtpps_reg, in, pin, pin);
	MLX5_SET(mtpps_reg, in, pin_mode, pin_mode);
	MLX5_SET(mtpps_reg, in, pattern, pattern);
	MLX5_SET(mtpps_reg, in, enable, on);
	MLX5_SET(mtpps_reg, in, field_select, field_select);

	err = mlx5_set_mtpps(mdev, in, sizeof(in));
	if (err)
		goto unlock;

	err = mlx5_set_mtppse(mdev, pin, 0, MLX5_EVENT_MODE_REPETETIVE & on);
	if (err)
		goto unlock;

	clock->pps_info.pin_armed[pin] = on;
	clock_priv(clock)->event_mdev = mdev;

unlock:
	mlx5_clock_unlock(clock);
	return err;
}

static u64 find_target_cycles(struct mlx5_core_dev *mdev, s64 target_ns)
{
	struct mlx5_clock *clock = mdev->clock;
	u64 cycles_now, cycles_delta;
	u64 nsec_now, nsec_delta;
	struct mlx5_timer *timer;
	unsigned long flags;

	timer = &clock->timer;

	cycles_now = mlx5_read_time(mdev, NULL, false);
	write_seqlock_irqsave(&clock->lock, flags);
	nsec_now = timecounter_cyc2time(&timer->tc, cycles_now);
	nsec_delta = target_ns - nsec_now;
	cycles_delta = div64_u64(nsec_delta << timer->cycles.shift,
				 timer->cycles.mult);
	write_sequnlock_irqrestore(&clock->lock, flags);

	return cycles_now + cycles_delta;
}

static u64 perout_conf_internal_timer(struct mlx5_core_dev *mdev, s64 sec)
{
	struct timespec64 ts = {};
	s64 target_ns;

	ts.tv_sec = sec;
	target_ns = timespec64_to_ns(&ts);

	return find_target_cycles(mdev, target_ns);
}

static u64 perout_conf_real_time(s64 sec, u32 nsec)
{
	return (u64)nsec | (u64)sec << 32;
}

static int perout_conf_1pps(struct mlx5_core_dev *mdev, struct ptp_clock_request *rq,
			    u64 *time_stamp, bool real_time)
{
	struct timespec64 ts;
	s64 ns;

	ts.tv_nsec = rq->perout.period.nsec;
	ts.tv_sec = rq->perout.period.sec;
	ns = timespec64_to_ns(&ts);

	if ((ns >> 1) != 500000000LL)
		return -EINVAL;

	*time_stamp = real_time ? perout_conf_real_time(rq->perout.start.sec, 0) :
		      perout_conf_internal_timer(mdev, rq->perout.start.sec);

	return 0;
}

#define MLX5_MAX_PULSE_DURATION (BIT(__mlx5_bit_sz(mtpps_reg, out_pulse_duration_ns)) - 1)
static int mlx5_perout_conf_out_pulse_duration(struct mlx5_core_dev *mdev,
					       struct ptp_clock_request *rq,
					       u32 *out_pulse_duration_ns)
{
	struct mlx5_pps *pps_info = &mdev->clock->pps_info;
	u32 out_pulse_duration;
	struct timespec64 ts;

	if (rq->perout.flags & PTP_PEROUT_DUTY_CYCLE) {
		ts.tv_sec = rq->perout.on.sec;
		ts.tv_nsec = rq->perout.on.nsec;
		out_pulse_duration = (u32)timespec64_to_ns(&ts);
	} else {
		/* out_pulse_duration_ns should be up to 50% of the
		 * pulse period as default
		 */
		ts.tv_sec = rq->perout.period.sec;
		ts.tv_nsec = rq->perout.period.nsec;
		out_pulse_duration = (u32)timespec64_to_ns(&ts) >> 1;
	}

	if (out_pulse_duration < pps_info->min_out_pulse_duration_ns ||
	    out_pulse_duration > MLX5_MAX_PULSE_DURATION) {
		mlx5_core_err(mdev, "NPPS pulse duration %u is not in [%llu, %lu]\n",
			      out_pulse_duration, pps_info->min_out_pulse_duration_ns,
			      MLX5_MAX_PULSE_DURATION);
		return -EINVAL;
	}
	*out_pulse_duration_ns = out_pulse_duration;

	return 0;
}

static int perout_conf_npps_real_time(struct mlx5_core_dev *mdev, struct ptp_clock_request *rq,
				      u32 *field_select, u32 *out_pulse_duration_ns,
				      u64 *period, u64 *time_stamp)
{
	struct mlx5_pps *pps_info = &mdev->clock->pps_info;
	struct ptp_clock_time *time = &rq->perout.start;
	struct timespec64 ts;

	ts.tv_sec = rq->perout.period.sec;
	ts.tv_nsec = rq->perout.period.nsec;
	if (timespec64_to_ns(&ts) < pps_info->min_npps_period) {
		mlx5_core_err(mdev, "NPPS period is lower than minimal npps period %llu\n",
			      pps_info->min_npps_period);
		return -EINVAL;
	}
	*period = perout_conf_real_time(rq->perout.period.sec, rq->perout.period.nsec);

	if (mlx5_perout_conf_out_pulse_duration(mdev, rq, out_pulse_duration_ns))
		return -EINVAL;

	*time_stamp = perout_conf_real_time(time->sec, time->nsec);
	*field_select |= MLX5_MTPPS_FS_NPPS_PERIOD |
			 MLX5_MTPPS_FS_OUT_PULSE_DURATION_NS;

	return 0;
}

static int mlx5_perout_configure(struct ptp_clock_info *ptp,
				 struct ptp_clock_request *rq,
				 int on)
{
	struct mlx5_clock *clock =
			container_of(ptp, struct mlx5_clock, ptp_info);
	u32 in[MLX5_ST_SZ_DW(mtpps_reg)] = {0};
	u32 out_pulse_duration_ns = 0;
	struct mlx5_core_dev *mdev;
	u32 field_select = 0;
	u64 npps_period = 0;
	u64 time_stamp = 0;
	u8 pin_mode = 0;
	u8 pattern = 0;
	bool rt_mode;
	int pin = -1;
	int err = 0;

	if (rq->perout.index >= clock->ptp_info.n_pins)
		return -EINVAL;

	field_select = MLX5_MTPPS_FS_ENABLE;
	pin = ptp_find_pin(clock->ptp, PTP_PF_PEROUT, rq->perout.index);
	if (pin < 0)
		return -EBUSY;

	mlx5_clock_lock(clock);
	mdev = mlx5_clock_mdev_get(clock);
	rt_mode = mlx5_real_time_mode(mdev);

	if (!MLX5_PPS_CAP(mdev)) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	if (on) {
		pin_mode = MLX5_PIN_MODE_OUT;
		pattern = MLX5_OUT_PATTERN_PERIODIC;

		if (rt_mode &&  rq->perout.start.sec > U32_MAX) {
			err = -EINVAL;
			goto unlock;
		}

		field_select |= MLX5_MTPPS_FS_PIN_MODE |
				MLX5_MTPPS_FS_PATTERN |
				MLX5_MTPPS_FS_TIME_STAMP;

		if (mlx5_npps_real_time_supported(mdev))
			err = perout_conf_npps_real_time(mdev, rq, &field_select,
							 &out_pulse_duration_ns, &npps_period,
							 &time_stamp);
		else
			err = perout_conf_1pps(mdev, rq, &time_stamp, rt_mode);
		if (err)
			goto unlock;
	}

	MLX5_SET(mtpps_reg, in, pin, pin);
	MLX5_SET(mtpps_reg, in, pin_mode, pin_mode);
	MLX5_SET(mtpps_reg, in, pattern, pattern);
	MLX5_SET(mtpps_reg, in, enable, on);
	MLX5_SET64(mtpps_reg, in, time_stamp, time_stamp);
	MLX5_SET(mtpps_reg, in, field_select, field_select);
	MLX5_SET64(mtpps_reg, in, npps_period, npps_period);
	MLX5_SET(mtpps_reg, in, out_pulse_duration_ns, out_pulse_duration_ns);
	err = mlx5_set_mtpps(mdev, in, sizeof(in));
	if (err)
		goto unlock;

	if (rt_mode)
		goto unlock;

	err = mlx5_set_mtppse(mdev, pin, 0, MLX5_EVENT_MODE_REPETETIVE & on);

unlock:
	mlx5_clock_unlock(clock);
	return err;
}

static int mlx5_pps_configure(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *rq,
			      int on)
{
	struct mlx5_clock *clock =
			container_of(ptp, struct mlx5_clock, ptp_info);

	clock->pps_info.enabled = !!on;
	return 0;
}

static int mlx5_ptp_enable(struct ptp_clock_info *ptp,
			   struct ptp_clock_request *rq,
			   int on)
{
	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		return mlx5_extts_configure(ptp, rq, on);
	case PTP_CLK_REQ_PEROUT:
		return mlx5_perout_configure(ptp, rq, on);
	case PTP_CLK_REQ_PPS:
		return mlx5_pps_configure(ptp, rq, on);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

enum {
	MLX5_MTPPS_REG_CAP_PIN_X_MODE_SUPPORT_PPS_IN = BIT(0),
	MLX5_MTPPS_REG_CAP_PIN_X_MODE_SUPPORT_PPS_OUT = BIT(1),
};

static int mlx5_ptp_verify(struct ptp_clock_info *ptp, unsigned int pin,
			   enum ptp_pin_function func, unsigned int chan)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock,
						ptp_info);

	switch (func) {
	case PTP_PF_NONE:
		return 0;
	case PTP_PF_EXTTS:
		return !(clock->pps_info.pin_caps[pin] &
			 MLX5_MTPPS_REG_CAP_PIN_X_MODE_SUPPORT_PPS_IN);
	case PTP_PF_PEROUT:
		return !(clock->pps_info.pin_caps[pin] &
			 MLX5_MTPPS_REG_CAP_PIN_X_MODE_SUPPORT_PPS_OUT);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ptp_clock_info mlx5_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "mlx5_ptp",
	.max_adj	= 50000000,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfine	= mlx5_ptp_adjfine,
	.adjphase	= mlx5_ptp_adjphase,
	.getmaxphase    = mlx5_ptp_getmaxphase,
	.adjtime	= mlx5_ptp_adjtime,
	.gettimex64	= mlx5_ptp_gettimex,
	.settime64	= mlx5_ptp_settime,
	.enable		= NULL,
	.verify		= NULL,
	.do_aux_work	= mlx5_timestamp_overflow,
};

static int mlx5_query_mtpps_pin_mode(struct mlx5_core_dev *mdev, u8 pin,
				     u32 *mtpps, u32 mtpps_size)
{
	u32 in[MLX5_ST_SZ_DW(mtpps_reg)] = {};

	MLX5_SET(mtpps_reg, in, pin, pin);

	return mlx5_core_access_reg(mdev, in, sizeof(in), mtpps,
				    mtpps_size, MLX5_REG_MTPPS, 0, 0);
}

static int mlx5_get_pps_pin_mode(struct mlx5_core_dev *mdev, u8 pin)
{
	u32 out[MLX5_ST_SZ_DW(mtpps_reg)] = {};
	u8 mode;
	int err;

	err = mlx5_query_mtpps_pin_mode(mdev, pin, out, sizeof(out));
	if (err || !MLX5_GET(mtpps_reg, out, enable))
		return PTP_PF_NONE;

	mode = MLX5_GET(mtpps_reg, out, pin_mode);

	if (mode == MLX5_PIN_MODE_IN)
		return PTP_PF_EXTTS;
	else if (mode == MLX5_PIN_MODE_OUT)
		return PTP_PF_PEROUT;

	return PTP_PF_NONE;
}

static void mlx5_init_pin_config(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = mdev->clock;
	int i;

	if (!clock->ptp_info.n_pins)
		return;

	clock->ptp_info.pin_config =
			kcalloc(clock->ptp_info.n_pins,
				sizeof(*clock->ptp_info.pin_config),
				GFP_KERNEL);
	if (!clock->ptp_info.pin_config)
		return;
	clock->ptp_info.enable = mlx5_ptp_enable;
	clock->ptp_info.verify = mlx5_ptp_verify;
	clock->ptp_info.pps = 1;

	clock->ptp_info.supported_extts_flags = PTP_RISING_EDGE |
						PTP_FALLING_EDGE |
						PTP_STRICT_FLAGS;

	if (mlx5_npps_real_time_supported(mdev))
		clock->ptp_info.supported_perout_flags = PTP_PEROUT_DUTY_CYCLE;

	for (i = 0; i < clock->ptp_info.n_pins; i++) {
		snprintf(clock->ptp_info.pin_config[i].name,
			 sizeof(clock->ptp_info.pin_config[i].name),
			 "mlx5_pps%d", i);
		clock->ptp_info.pin_config[i].index = i;
		clock->ptp_info.pin_config[i].func = mlx5_get_pps_pin_mode(mdev, i);
		clock->ptp_info.pin_config[i].chan = 0;
	}
}

static void mlx5_get_pps_caps(struct mlx5_core_dev *mdev)
{
	u32 out[MLX5_ST_SZ_DW(mtpps_reg)] = {0};
	struct mlx5_clock *clock = mdev->clock;

	mlx5_query_mtpps(mdev, out, sizeof(out));

	clock->ptp_info.n_pins = MLX5_GET(mtpps_reg, out,
					  cap_number_of_pps_pins);
	clock->ptp_info.n_ext_ts = MLX5_GET(mtpps_reg, out,
					    cap_max_num_of_pps_in_pins);
	clock->ptp_info.n_per_out = MLX5_GET(mtpps_reg, out,
					     cap_max_num_of_pps_out_pins);

	if (MLX5_CAP_MCAM_FEATURE(mdev, npps_period))
		clock->pps_info.min_npps_period = 1 << MLX5_GET(mtpps_reg, out,
								cap_log_min_npps_period);
	if (MLX5_CAP_MCAM_FEATURE(mdev, out_pulse_duration_ns))
		clock->pps_info.min_out_pulse_duration_ns = 1 << MLX5_GET(mtpps_reg, out,
								cap_log_min_out_pulse_duration_ns);

	clock->pps_info.pin_caps[0] = MLX5_GET(mtpps_reg, out, cap_pin_0_mode);
	clock->pps_info.pin_caps[1] = MLX5_GET(mtpps_reg, out, cap_pin_1_mode);
	clock->pps_info.pin_caps[2] = MLX5_GET(mtpps_reg, out, cap_pin_2_mode);
	clock->pps_info.pin_caps[3] = MLX5_GET(mtpps_reg, out, cap_pin_3_mode);
	clock->pps_info.pin_caps[4] = MLX5_GET(mtpps_reg, out, cap_pin_4_mode);
	clock->pps_info.pin_caps[5] = MLX5_GET(mtpps_reg, out, cap_pin_5_mode);
	clock->pps_info.pin_caps[6] = MLX5_GET(mtpps_reg, out, cap_pin_6_mode);
	clock->pps_info.pin_caps[7] = MLX5_GET(mtpps_reg, out, cap_pin_7_mode);
}

static void ts_next_sec(struct timespec64 *ts)
{
	ts->tv_sec += 1;
	ts->tv_nsec = 0;
}

static u64 perout_conf_next_event_timer(struct mlx5_core_dev *mdev,
					struct mlx5_clock *clock)
{
	struct timespec64 ts;
	s64 target_ns;

	mlx5_ptp_gettimex(&clock->ptp_info, &ts, NULL);
	ts_next_sec(&ts);
	target_ns = timespec64_to_ns(&ts);

	return find_target_cycles(mdev, target_ns);
}

static int mlx5_pps_event(struct notifier_block *nb,
			  unsigned long type, void *data)
{
	struct mlx5_clock_dev_state *clock_state = mlx5_nb_cof(nb, struct mlx5_clock_dev_state,
							       pps_nb);
	struct mlx5_core_dev *mdev = clock_state->mdev;
	struct mlx5_clock *clock = mdev->clock;
	struct ptp_clock_event ptp_event;
	struct mlx5_eqe *eqe = data;
	int pin = eqe->data.pps.pin;
	unsigned long flags;
	u64 ns;

	switch (clock->ptp_info.pin_config[pin].func) {
	case PTP_PF_EXTTS:
		ptp_event.index = pin;
		ptp_event.timestamp = mlx5_real_time_mode(mdev) ?
			mlx5_real_time_cyc2time(clock,
						be64_to_cpu(eqe->data.pps.time_stamp)) :
			mlx5_timecounter_cyc2time(clock,
						  be64_to_cpu(eqe->data.pps.time_stamp));
		if (clock->pps_info.enabled) {
			ptp_event.type = PTP_CLOCK_PPSUSR;
			ptp_event.pps_times.ts_real =
					ns_to_timespec64(ptp_event.timestamp);
		} else {
			ptp_event.type = PTP_CLOCK_EXTTS;
		}
		/* TODOL clock->ptp can be NULL if ptp_clock_register fails */
		ptp_clock_event(clock->ptp, &ptp_event);
		break;
	case PTP_PF_PEROUT:
		if (clock->shared) {
			mlx5_core_warn(mdev, " Received unexpected PPS out event\n");
			break;
		}
		ns = perout_conf_next_event_timer(mdev, clock);
		write_seqlock_irqsave(&clock->lock, flags);
		clock->pps_info.start[pin] = ns;
		write_sequnlock_irqrestore(&clock->lock, flags);
		schedule_work(&clock_state->out_work);
		break;
	default:
		mlx5_core_err(mdev, " Unhandled clock PPS event, func %d\n",
			      clock->ptp_info.pin_config[pin].func);
	}

	return NOTIFY_OK;
}

static void mlx5_timecounter_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = mdev->clock;
	struct mlx5_timer *timer = &clock->timer;
	u32 dev_freq;

	dev_freq = MLX5_CAP_GEN(mdev, device_frequency_khz);
	timer->cycles.read = read_internal_timer;
	timer->cycles.shift = mlx5_ptp_shift_constant(dev_freq);
	timer->cycles.mult = clocksource_khz2mult(dev_freq,
						  timer->cycles.shift);
	timer->nominal_c_mult = timer->cycles.mult;
	timer->cycles.mask = CLOCKSOURCE_MASK(41);

	timecounter_init(&timer->tc, &timer->cycles,
			 ktime_to_ns(ktime_get_real()));
}

static void mlx5_init_overflow_period(struct mlx5_core_dev *mdev)
{
	struct mlx5_ib_clock_info *clock_info = mdev->clock_info;
	struct mlx5_clock *clock = mdev->clock;
	struct mlx5_timer *timer = &clock->timer;
	u64 overflow_cycles;
	u64 frac = 0;
	u64 ns;

	/* Calculate period in seconds to call the overflow watchdog - to make
	 * sure counter is checked at least twice every wrap around.
	 * The period is calculated as the minimum between max HW cycles count
	 * (The clock source mask) and max amount of cycles that can be
	 * multiplied by clock multiplier where the result doesn't exceed
	 * 64bits.
	 */
	overflow_cycles = div64_u64(~0ULL >> 1, timer->cycles.mult);
	overflow_cycles = min(overflow_cycles, div_u64(timer->cycles.mask, 3));

	ns = cyclecounter_cyc2ns(&timer->cycles, overflow_cycles,
				 frac, &frac);
	do_div(ns, NSEC_PER_SEC / HZ);
	timer->overflow_period = ns;

	if (!timer->overflow_period) {
		timer->overflow_period = HZ;
		mlx5_core_warn(mdev,
			       "invalid overflow period, overflow_work is scheduled once per second\n");
	}

	if (clock_info)
		clock_info->overflow_period = timer->overflow_period;
}

static void mlx5_init_clock_info(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = mdev->clock;
	struct mlx5_ib_clock_info *info;
	struct mlx5_timer *timer;

	mdev->clock_info = (struct mlx5_ib_clock_info *)get_zeroed_page(GFP_KERNEL);
	if (!mdev->clock_info) {
		mlx5_core_warn(mdev, "Failed to allocate IB clock info page\n");
		return;
	}

	info = mdev->clock_info;
	timer = &clock->timer;

	info->nsec = timer->tc.nsec;
	info->cycles = timer->tc.cycle_last;
	info->mask = timer->cycles.mask;
	info->mult = timer->nominal_c_mult;
	info->shift = timer->cycles.shift;
	info->frac = timer->tc.frac;
}

static void mlx5_init_timer_max_freq_adjustment(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = mdev->clock;
	u32 out[MLX5_ST_SZ_DW(mtutc_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(mtutc_reg)] = {};
	u8 log_max_freq_adjustment = 0;
	int err;

	err = mlx5_core_access_reg(mdev, in, sizeof(in), out, sizeof(out),
				   MLX5_REG_MTUTC, 0, 0);
	if (!err)
		log_max_freq_adjustment =
			MLX5_GET(mtutc_reg, out, log_max_freq_adjustment);

	if (log_max_freq_adjustment)
		clock->ptp_info.max_adj =
			min(S32_MAX, 1 << log_max_freq_adjustment);
}

static void mlx5_init_timer_clock(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = mdev->clock;
	bool expose_cycles;

	/* Configure the PHC */
	clock->ptp_info = mlx5_ptp_clock_info;

	if (MLX5_CAP_MCAM_REG(mdev, mtutc))
		mlx5_init_timer_max_freq_adjustment(mdev);

	expose_cycles = !MLX5_CAP_GEN(mdev, disciplined_fr_counter) ||
			!mlx5_real_time_mode(mdev);

#ifdef CONFIG_X86
	if (MLX5_CAP_MCAM_REG3(mdev, mtptm) &&
	    MLX5_CAP_MCAM_REG3(mdev, mtctr) && boot_cpu_has(X86_FEATURE_ART)) {
		clock->ptp_info.getcrosststamp = mlx5_ptp_getcrosststamp;
		if (expose_cycles)
			clock->ptp_info.getcrosscycles =
				mlx5_ptp_getcrosscycles;
	}
#endif /* CONFIG_X86 */

	if (expose_cycles)
		clock->ptp_info.getcyclesx64 = mlx5_ptp_getcyclesx;

	mlx5_timecounter_init(mdev);
	mlx5_init_clock_info(mdev);
	mlx5_init_overflow_period(mdev);

	if (mlx5_real_time_mode(mdev)) {
		struct timespec64 ts;

		ktime_get_real_ts64(&ts);
		mlx5_clock_settime(mdev, clock, &ts);
	}
}

static void mlx5_init_pps(struct mlx5_core_dev *mdev)
{
	if (!MLX5_PPS_CAP(mdev))
		return;

	mlx5_get_pps_caps(mdev);
	mlx5_init_pin_config(mdev);
}

static void mlx5_init_clock_dev(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = mdev->clock;

	seqlock_init(&clock->lock);

	/* Initialize the device clock */
	mlx5_init_timer_clock(mdev);

	/* Initialize 1PPS data structures */
	mlx5_init_pps(mdev);

	clock->ptp = ptp_clock_register(&clock->ptp_info,
					clock->shared ? NULL : &mdev->pdev->dev);
	if (IS_ERR(clock->ptp)) {
		mlx5_core_warn(mdev, "%sptp_clock_register failed %pe\n",
			       clock->shared ? "shared clock " : "",
			       clock->ptp);
		clock->ptp = NULL;
	}

	if (clock->ptp)
		ptp_schedule_worker(clock->ptp, 0);
}

static void mlx5_destroy_clock_dev(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = mdev->clock;

	if (clock->ptp) {
		ptp_clock_unregister(clock->ptp);
		clock->ptp = NULL;
	}

	if (mdev->clock_info) {
		free_page((unsigned long)mdev->clock_info);
		mdev->clock_info = NULL;
	}

	kfree(clock->ptp_info.pin_config);
}

static void mlx5_clock_free(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock_priv *cpriv = clock_priv(mdev->clock);

	mlx5_destroy_clock_dev(mdev);
	mutex_destroy(&cpriv->lock);
	kfree(cpriv);
	mdev->clock = NULL;
}

static int mlx5_clock_alloc(struct mlx5_core_dev *mdev, bool shared)
{
	struct mlx5_clock_priv *cpriv;
	struct mlx5_clock *clock;

	cpriv = kzalloc(sizeof(*cpriv), GFP_KERNEL);
	if (!cpriv)
		return -ENOMEM;

	mutex_init(&cpriv->lock);
	cpriv->mdev = mdev;
	clock = &cpriv->clock;
	clock->shared = shared;
	mdev->clock = clock;
	mlx5_clock_lock(clock);
	mlx5_init_clock_dev(mdev);
	mlx5_clock_unlock(clock);

	if (!clock->shared)
		return 0;

	if (!clock->ptp) {
		mlx5_core_warn(mdev, "failed to create ptp dev shared by multiple functions");
		mlx5_clock_free(mdev);
		return -EINVAL;
	}

	return 0;
}

static void mlx5_shared_clock_register(struct mlx5_core_dev *mdev, u64 key)
{
	struct mlx5_core_dev *peer_dev, *next = NULL;
	struct mlx5_devcom_match_attr attr = {
		.key.val = key,
	};
	struct mlx5_devcom_comp_dev *compd;
	struct mlx5_devcom_comp_dev *pos;

	compd = mlx5_devcom_register_component(mdev->priv.devc,
					       MLX5_DEVCOM_SHARED_CLOCK,
					       &attr, NULL, mdev);
	if (!compd)
		return;

	mdev->clock_state->compdev = compd;

	mlx5_devcom_comp_lock(mdev->clock_state->compdev);
	mlx5_devcom_for_each_peer_entry(mdev->clock_state->compdev, peer_dev, pos) {
		if (peer_dev->clock) {
			next = peer_dev;
			break;
		}
	}

	if (next) {
		mdev->clock = next->clock;
		/* clock info is shared among all the functions using the same clock */
		mdev->clock_info = next->clock_info;
	} else {
		mlx5_clock_alloc(mdev, true);
	}
	mlx5_devcom_comp_unlock(mdev->clock_state->compdev);

	if (!mdev->clock) {
		mlx5_devcom_unregister_component(mdev->clock_state->compdev);
		mdev->clock_state->compdev = NULL;
	}
}

static void mlx5_shared_clock_unregister(struct mlx5_core_dev *mdev)
{
	struct mlx5_core_dev *peer_dev, *next = NULL;
	struct mlx5_clock *clock = mdev->clock;
	struct mlx5_devcom_comp_dev *pos;

	mlx5_devcom_comp_lock(mdev->clock_state->compdev);
	mlx5_devcom_for_each_peer_entry(mdev->clock_state->compdev, peer_dev, pos) {
		if (peer_dev->clock && peer_dev != mdev) {
			next = peer_dev;
			break;
		}
	}

	if (next) {
		struct mlx5_clock_priv *cpriv = clock_priv(clock);

		mlx5_clock_lock(clock);
		if (mdev == cpriv->mdev)
			cpriv->mdev = next;
		mlx5_clock_unlock(clock);
	} else {
		mlx5_clock_free(mdev);
	}

	mdev->clock = NULL;
	mdev->clock_info = NULL;
	mlx5_devcom_comp_unlock(mdev->clock_state->compdev);

	mlx5_devcom_unregister_component(mdev->clock_state->compdev);
}

static void mlx5_clock_arm_pps_in_event(struct mlx5_clock *clock,
					struct mlx5_core_dev *new_mdev,
					struct mlx5_core_dev *old_mdev)
{
	struct ptp_clock_info *ptp_info = &clock->ptp_info;
	struct mlx5_clock_priv *cpriv = clock_priv(clock);
	int i;

	for (i = 0; i < ptp_info->n_pins; i++) {
		if (ptp_info->pin_config[i].func != PTP_PF_EXTTS ||
		    !clock->pps_info.pin_armed[i])
			continue;

		if (new_mdev) {
			mlx5_set_mtppse(new_mdev, i, 0, MLX5_EVENT_MODE_REPETETIVE);
			cpriv->event_mdev = new_mdev;
		} else {
			cpriv->event_mdev = NULL;
		}

		if (old_mdev)
			mlx5_set_mtppse(old_mdev, i, 0, MLX5_EVENT_MODE_DISABLE);
	}
}

void mlx5_clock_load(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = mdev->clock;
	struct mlx5_clock_priv *cpriv;

	if (!MLX5_CAP_GEN(mdev, device_frequency_khz))
		return;

	INIT_WORK(&mdev->clock_state->out_work, mlx5_pps_out);
	MLX5_NB_INIT(&mdev->clock_state->pps_nb, mlx5_pps_event, PPS_EVENT);
	mlx5_eq_notifier_register(mdev, &mdev->clock_state->pps_nb);

	if (!clock->shared) {
		mlx5_clock_arm_pps_in_event(clock, mdev, NULL);
		return;
	}

	cpriv = clock_priv(clock);
	mlx5_devcom_comp_lock(mdev->clock_state->compdev);
	mlx5_clock_lock(clock);
	if (mdev == cpriv->mdev && mdev != cpriv->event_mdev)
		mlx5_clock_arm_pps_in_event(clock, mdev, cpriv->event_mdev);
	mlx5_clock_unlock(clock);
	mlx5_devcom_comp_unlock(mdev->clock_state->compdev);
}

void mlx5_clock_unload(struct mlx5_core_dev *mdev)
{
	struct mlx5_core_dev *peer_dev, *next = NULL;
	struct mlx5_clock *clock = mdev->clock;
	struct mlx5_devcom_comp_dev *pos;

	if (!MLX5_CAP_GEN(mdev, device_frequency_khz))
		return;

	if (!clock->shared) {
		mlx5_clock_arm_pps_in_event(clock, NULL, mdev);
		goto out;
	}

	mlx5_devcom_comp_lock(mdev->clock_state->compdev);
	mlx5_devcom_for_each_peer_entry(mdev->clock_state->compdev, peer_dev, pos) {
		if (peer_dev->clock && peer_dev != mdev) {
			next = peer_dev;
			break;
		}
	}

	mlx5_clock_lock(clock);
	if (mdev == clock_priv(clock)->event_mdev)
		mlx5_clock_arm_pps_in_event(clock, next, mdev);
	mlx5_clock_unlock(clock);
	mlx5_devcom_comp_unlock(mdev->clock_state->compdev);

out:
	mlx5_eq_notifier_unregister(mdev, &mdev->clock_state->pps_nb);
	cancel_work_sync(&mdev->clock_state->out_work);
}

static struct mlx5_clock null_clock;

int mlx5_init_clock(struct mlx5_core_dev *mdev)
{
	u8 identity[MLX5_RT_CLOCK_IDENTITY_SIZE];
	struct mlx5_clock_dev_state *clock_state;
	u64 key;
	int err;

	if (!MLX5_CAP_GEN(mdev, device_frequency_khz)) {
		mdev->clock = &null_clock;
		mlx5_core_warn(mdev, "invalid device_frequency_khz, aborting HW clock init\n");
		return 0;
	}

	clock_state = kzalloc(sizeof(*clock_state), GFP_KERNEL);
	if (!clock_state)
		return -ENOMEM;
	clock_state->mdev = mdev;
	mdev->clock_state = clock_state;

	if (MLX5_CAP_MCAM_REG3(mdev, mrtcq) && mlx5_real_time_mode(mdev)) {
		if (mlx5_clock_identity_get(mdev, identity)) {
			mlx5_core_warn(mdev, "failed to get rt clock identity, create ptp dev per function\n");
		} else {
			memcpy(&key, &identity, sizeof(key));
			mlx5_shared_clock_register(mdev, key);
		}
	}

	if (!mdev->clock) {
		err = mlx5_clock_alloc(mdev, false);
		if (err) {
			kfree(clock_state);
			mdev->clock_state = NULL;
			return err;
		}
	}

	return 0;
}

void mlx5_cleanup_clock(struct mlx5_core_dev *mdev)
{
	if (!MLX5_CAP_GEN(mdev, device_frequency_khz))
		return;

	if (mdev->clock->shared)
		mlx5_shared_clock_unregister(mdev);
	else
		mlx5_clock_free(mdev);
	kfree(mdev->clock_state);
	mdev->clock_state = NULL;
}

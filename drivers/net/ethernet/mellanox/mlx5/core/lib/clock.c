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
#include "en.h"

enum {
	MLX5_CYCLES_SHIFT	= 23
};

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
};

static u64 read_internal_timer(const struct cyclecounter *cc)
{
	struct mlx5_clock *clock = container_of(cc, struct mlx5_clock, cycles);
	struct mlx5_core_dev *mdev = container_of(clock, struct mlx5_core_dev,
						  clock);

	return mlx5_read_internal_timer(mdev) & cc->mask;
}

static void mlx5_pps_out(struct work_struct *work)
{
	struct mlx5_pps *pps_info = container_of(work, struct mlx5_pps,
						 out_work);
	struct mlx5_clock *clock = container_of(pps_info, struct mlx5_clock,
						pps_info);
	struct mlx5_core_dev *mdev = container_of(clock, struct mlx5_core_dev,
						  clock);
	u32 in[MLX5_ST_SZ_DW(mtpps_reg)] = {0};
	unsigned long flags;
	int i;

	for (i = 0; i < clock->ptp_info.n_pins; i++) {
		u64 tstart;

		write_lock_irqsave(&clock->lock, flags);
		tstart = clock->pps_info.start[i];
		clock->pps_info.start[i] = 0;
		write_unlock_irqrestore(&clock->lock, flags);
		if (!tstart)
			continue;

		MLX5_SET(mtpps_reg, in, pin, i);
		MLX5_SET64(mtpps_reg, in, time_stamp, tstart);
		MLX5_SET(mtpps_reg, in, field_select, MLX5_MTPPS_FS_TIME_STAMP);
		mlx5_set_mtpps(mdev, in, sizeof(in));
	}
}

static void mlx5_timestamp_overflow(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mlx5_clock *clock = container_of(dwork, struct mlx5_clock,
						overflow_work);
	unsigned long flags;

	write_lock_irqsave(&clock->lock, flags);
	timecounter_read(&clock->tc);
	write_unlock_irqrestore(&clock->lock, flags);
	schedule_delayed_work(&clock->overflow_work, clock->overflow_period);
}

static int mlx5_ptp_settime(struct ptp_clock_info *ptp,
			    const struct timespec64 *ts)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock,
						 ptp_info);
	u64 ns = timespec64_to_ns(ts);
	unsigned long flags;

	write_lock_irqsave(&clock->lock, flags);
	timecounter_init(&clock->tc, &clock->cycles, ns);
	write_unlock_irqrestore(&clock->lock, flags);

	return 0;
}

static int mlx5_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock,
						ptp_info);
	u64 ns;
	unsigned long flags;

	write_lock_irqsave(&clock->lock, flags);
	ns = timecounter_read(&clock->tc);
	write_unlock_irqrestore(&clock->lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int mlx5_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock,
						ptp_info);
	unsigned long flags;

	write_lock_irqsave(&clock->lock, flags);
	timecounter_adjtime(&clock->tc, delta);
	write_unlock_irqrestore(&clock->lock, flags);

	return 0;
}

static int mlx5_ptp_adjfreq(struct ptp_clock_info *ptp, s32 delta)
{
	u64 adj;
	u32 diff;
	unsigned long flags;
	int neg_adj = 0;
	struct mlx5_clock *clock = container_of(ptp, struct mlx5_clock,
						ptp_info);

	if (delta < 0) {
		neg_adj = 1;
		delta = -delta;
	}

	adj = clock->nominal_c_mult;
	adj *= delta;
	diff = div_u64(adj, 1000000000ULL);

	write_lock_irqsave(&clock->lock, flags);
	timecounter_read(&clock->tc);
	clock->cycles.mult = neg_adj ? clock->nominal_c_mult - diff :
				       clock->nominal_c_mult + diff;
	write_unlock_irqrestore(&clock->lock, flags);

	return 0;
}

static int mlx5_extts_configure(struct ptp_clock_info *ptp,
				struct ptp_clock_request *rq,
				int on)
{
	struct mlx5_clock *clock =
			container_of(ptp, struct mlx5_clock, ptp_info);
	struct mlx5_core_dev *mdev =
			container_of(clock, struct mlx5_core_dev, clock);
	u32 in[MLX5_ST_SZ_DW(mtpps_reg)] = {0};
	u32 field_select = 0;
	u8 pin_mode = 0;
	u8 pattern = 0;
	int pin = -1;
	int err = 0;

	if (!MLX5_PPS_CAP(mdev))
		return -EOPNOTSUPP;

	if (rq->extts.index >= clock->ptp_info.n_pins)
		return -EINVAL;

	if (on) {
		pin = ptp_find_pin(clock->ptp, PTP_PF_EXTTS, rq->extts.index);
		if (pin < 0)
			return -EBUSY;
		pin_mode = MLX5_PIN_MODE_IN;
		pattern = !!(rq->extts.flags & PTP_FALLING_EDGE);
		field_select = MLX5_MTPPS_FS_PIN_MODE |
			       MLX5_MTPPS_FS_PATTERN |
			       MLX5_MTPPS_FS_ENABLE;
	} else {
		pin = rq->extts.index;
		field_select = MLX5_MTPPS_FS_ENABLE;
	}

	MLX5_SET(mtpps_reg, in, pin, pin);
	MLX5_SET(mtpps_reg, in, pin_mode, pin_mode);
	MLX5_SET(mtpps_reg, in, pattern, pattern);
	MLX5_SET(mtpps_reg, in, enable, on);
	MLX5_SET(mtpps_reg, in, field_select, field_select);

	err = mlx5_set_mtpps(mdev, in, sizeof(in));
	if (err)
		return err;

	return mlx5_set_mtppse(mdev, pin, 0,
			       MLX5_EVENT_MODE_REPETETIVE & on);
}

static int mlx5_perout_configure(struct ptp_clock_info *ptp,
				 struct ptp_clock_request *rq,
				 int on)
{
	struct mlx5_clock *clock =
			container_of(ptp, struct mlx5_clock, ptp_info);
	struct mlx5_core_dev *mdev =
			container_of(clock, struct mlx5_core_dev, clock);
	u32 in[MLX5_ST_SZ_DW(mtpps_reg)] = {0};
	u64 nsec_now, nsec_delta, time_stamp = 0;
	u64 cycles_now, cycles_delta;
	struct timespec64 ts;
	unsigned long flags;
	u32 field_select = 0;
	u8 pin_mode = 0;
	u8 pattern = 0;
	int pin = -1;
	int err = 0;
	s64 ns;

	if (!MLX5_PPS_CAP(mdev))
		return -EOPNOTSUPP;

	if (rq->perout.index >= clock->ptp_info.n_pins)
		return -EINVAL;

	if (on) {
		pin = ptp_find_pin(clock->ptp, PTP_PF_PEROUT,
				   rq->perout.index);
		if (pin < 0)
			return -EBUSY;

		pin_mode = MLX5_PIN_MODE_OUT;
		pattern = MLX5_OUT_PATTERN_PERIODIC;
		ts.tv_sec = rq->perout.period.sec;
		ts.tv_nsec = rq->perout.period.nsec;
		ns = timespec64_to_ns(&ts);

		if ((ns >> 1) != 500000000LL)
			return -EINVAL;

		ts.tv_sec = rq->perout.start.sec;
		ts.tv_nsec = rq->perout.start.nsec;
		ns = timespec64_to_ns(&ts);
		cycles_now = mlx5_read_internal_timer(mdev);
		write_lock_irqsave(&clock->lock, flags);
		nsec_now = timecounter_cyc2time(&clock->tc, cycles_now);
		nsec_delta = ns - nsec_now;
		cycles_delta = div64_u64(nsec_delta << clock->cycles.shift,
					 clock->cycles.mult);
		write_unlock_irqrestore(&clock->lock, flags);
		time_stamp = cycles_now + cycles_delta;
		field_select = MLX5_MTPPS_FS_PIN_MODE |
			       MLX5_MTPPS_FS_PATTERN |
			       MLX5_MTPPS_FS_ENABLE |
			       MLX5_MTPPS_FS_TIME_STAMP;
	} else {
		pin = rq->perout.index;
		field_select = MLX5_MTPPS_FS_ENABLE;
	}

	MLX5_SET(mtpps_reg, in, pin, pin);
	MLX5_SET(mtpps_reg, in, pin_mode, pin_mode);
	MLX5_SET(mtpps_reg, in, pattern, pattern);
	MLX5_SET(mtpps_reg, in, enable, on);
	MLX5_SET64(mtpps_reg, in, time_stamp, time_stamp);
	MLX5_SET(mtpps_reg, in, field_select, field_select);

	err = mlx5_set_mtpps(mdev, in, sizeof(in));
	if (err)
		return err;

	return mlx5_set_mtppse(mdev, pin, 0,
			       MLX5_EVENT_MODE_REPETETIVE & on);
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

static int mlx5_ptp_verify(struct ptp_clock_info *ptp, unsigned int pin,
			   enum ptp_pin_function func, unsigned int chan)
{
	return (func == PTP_PF_PHYSYNC) ? -EOPNOTSUPP : 0;
}

static const struct ptp_clock_info mlx5_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "mlx5_p2p",
	.max_adj	= 100000000,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfreq	= mlx5_ptp_adjfreq,
	.adjtime	= mlx5_ptp_adjtime,
	.gettime64	= mlx5_ptp_gettime,
	.settime64	= mlx5_ptp_settime,
	.enable		= NULL,
	.verify		= NULL,
};

static int mlx5_init_pin_config(struct mlx5_clock *clock)
{
	int i;

	clock->ptp_info.pin_config =
			kzalloc(sizeof(*clock->ptp_info.pin_config) *
				clock->ptp_info.n_pins, GFP_KERNEL);
	if (!clock->ptp_info.pin_config)
		return -ENOMEM;
	clock->ptp_info.enable = mlx5_ptp_enable;
	clock->ptp_info.verify = mlx5_ptp_verify;
	clock->ptp_info.pps = 1;

	for (i = 0; i < clock->ptp_info.n_pins; i++) {
		snprintf(clock->ptp_info.pin_config[i].name,
			 sizeof(clock->ptp_info.pin_config[i].name),
			 "mlx5_pps%d", i);
		clock->ptp_info.pin_config[i].index = i;
		clock->ptp_info.pin_config[i].func = PTP_PF_NONE;
		clock->ptp_info.pin_config[i].chan = i;
	}

	return 0;
}

static void mlx5_get_pps_caps(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = &mdev->clock;
	u32 out[MLX5_ST_SZ_DW(mtpps_reg)] = {0};

	mlx5_query_mtpps(mdev, out, sizeof(out));

	clock->ptp_info.n_pins = MLX5_GET(mtpps_reg, out,
					  cap_number_of_pps_pins);
	clock->ptp_info.n_ext_ts = MLX5_GET(mtpps_reg, out,
					    cap_max_num_of_pps_in_pins);
	clock->ptp_info.n_per_out = MLX5_GET(mtpps_reg, out,
					     cap_max_num_of_pps_out_pins);

	clock->pps_info.pin_caps[0] = MLX5_GET(mtpps_reg, out, cap_pin_0_mode);
	clock->pps_info.pin_caps[1] = MLX5_GET(mtpps_reg, out, cap_pin_1_mode);
	clock->pps_info.pin_caps[2] = MLX5_GET(mtpps_reg, out, cap_pin_2_mode);
	clock->pps_info.pin_caps[3] = MLX5_GET(mtpps_reg, out, cap_pin_3_mode);
	clock->pps_info.pin_caps[4] = MLX5_GET(mtpps_reg, out, cap_pin_4_mode);
	clock->pps_info.pin_caps[5] = MLX5_GET(mtpps_reg, out, cap_pin_5_mode);
	clock->pps_info.pin_caps[6] = MLX5_GET(mtpps_reg, out, cap_pin_6_mode);
	clock->pps_info.pin_caps[7] = MLX5_GET(mtpps_reg, out, cap_pin_7_mode);
}

void mlx5_pps_event(struct mlx5_core_dev *mdev,
		    struct mlx5_eqe *eqe)
{
	struct mlx5_clock *clock = &mdev->clock;
	struct ptp_clock_event ptp_event;
	struct timespec64 ts;
	u64 nsec_now, nsec_delta;
	u64 cycles_now, cycles_delta;
	int pin = eqe->data.pps.pin;
	s64 ns;
	unsigned long flags;

	switch (clock->ptp_info.pin_config[pin].func) {
	case PTP_PF_EXTTS:
		if (clock->pps_info.enabled) {
			ptp_event.type = PTP_CLOCK_PPSUSR;
			ptp_event.pps_times.ts_real = ns_to_timespec64(eqe->data.pps.time_stamp);
		} else {
			ptp_event.type = PTP_CLOCK_EXTTS;
		}
		ptp_clock_event(clock->ptp, &ptp_event);
		break;
	case PTP_PF_PEROUT:
		mlx5_ptp_gettime(&clock->ptp_info, &ts);
		cycles_now = mlx5_read_internal_timer(mdev);
		ts.tv_sec += 1;
		ts.tv_nsec = 0;
		ns = timespec64_to_ns(&ts);
		write_lock_irqsave(&clock->lock, flags);
		nsec_now = timecounter_cyc2time(&clock->tc, cycles_now);
		nsec_delta = ns - nsec_now;
		cycles_delta = div64_u64(nsec_delta << clock->cycles.shift,
					 clock->cycles.mult);
		clock->pps_info.start[pin] = cycles_now + cycles_delta;
		schedule_work(&clock->pps_info.out_work);
		write_unlock_irqrestore(&clock->lock, flags);
		break;
	default:
		mlx5_core_err(mdev, " Unhandled event\n");
	}
}

void mlx5_init_clock(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = &mdev->clock;
	u64 ns;
	u64 frac = 0;
	u32 dev_freq;

	dev_freq = MLX5_CAP_GEN(mdev, device_frequency_khz);
	if (!dev_freq) {
		mlx5_core_warn(mdev, "invalid device_frequency_khz, aborting HW clock init\n");
		return;
	}
	rwlock_init(&clock->lock);
	clock->cycles.read = read_internal_timer;
	clock->cycles.shift = MLX5_CYCLES_SHIFT;
	clock->cycles.mult = clocksource_khz2mult(dev_freq,
						  clock->cycles.shift);
	clock->nominal_c_mult = clock->cycles.mult;
	clock->cycles.mask = CLOCKSOURCE_MASK(41);

	timecounter_init(&clock->tc, &clock->cycles,
			 ktime_to_ns(ktime_get_real()));

	/* Calculate period in seconds to call the overflow watchdog - to make
	 * sure counter is checked at least once every wrap around.
	 */
	ns = cyclecounter_cyc2ns(&clock->cycles, clock->cycles.mask,
				 frac, &frac);
	do_div(ns, NSEC_PER_SEC / 2 / HZ);
	clock->overflow_period = ns;

	INIT_WORK(&clock->pps_info.out_work, mlx5_pps_out);
	INIT_DELAYED_WORK(&clock->overflow_work, mlx5_timestamp_overflow);
	if (clock->overflow_period)
		schedule_delayed_work(&clock->overflow_work, 0);
	else
		mlx5_core_warn(mdev, "invalid overflow period, overflow_work is not scheduled\n");

	/* Configure the PHC */
	clock->ptp_info = mlx5_ptp_clock_info;

	/* Initialize 1PPS data structures */
	if (MLX5_PPS_CAP(mdev))
		mlx5_get_pps_caps(mdev);
	if (clock->ptp_info.n_pins)
		mlx5_init_pin_config(clock);

	clock->ptp = ptp_clock_register(&clock->ptp_info,
					&mdev->pdev->dev);
	if (IS_ERR(clock->ptp)) {
		mlx5_core_warn(mdev, "ptp_clock_register failed %ld\n",
			       PTR_ERR(clock->ptp));
		clock->ptp = NULL;
	}
}

void mlx5_cleanup_clock(struct mlx5_core_dev *mdev)
{
	struct mlx5_clock *clock = &mdev->clock;

	if (!MLX5_CAP_GEN(mdev, device_frequency_khz))
		return;

	if (clock->ptp) {
		ptp_clock_unregister(clock->ptp);
		clock->ptp = NULL;
	}

	cancel_work_sync(&clock->pps_info.out_work);
	cancel_delayed_work_sync(&clock->overflow_work);
	kfree(clock->ptp_info.pin_config);
}

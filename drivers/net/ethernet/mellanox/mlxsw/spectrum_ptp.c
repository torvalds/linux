// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019 Mellanox Technologies. All rights reserved */

#include <linux/ptp_clock_kernel.h>
#include <linux/clocksource.h>
#include <linux/timecounter.h>
#include <linux/spinlock.h>
#include <linux/device.h>

#include "spectrum_ptp.h"
#include "core.h"

#define MLXSW_SP1_PTP_CLOCK_CYCLES_SHIFT	29
#define MLXSW_SP1_PTP_CLOCK_FREQ_KHZ		156257 /* 6.4nSec */
#define MLXSW_SP1_PTP_CLOCK_MASK		64

struct mlxsw_sp_ptp_clock {
	struct mlxsw_core *core;
	spinlock_t lock; /* protect this structure */
	struct cyclecounter cycles;
	struct timecounter tc;
	u32 nominal_c_mult;
	struct ptp_clock *ptp;
	struct ptp_clock_info ptp_info;
	unsigned long overflow_period;
	struct delayed_work overflow_work;
};

static u64 __mlxsw_sp1_ptp_read_frc(struct mlxsw_sp_ptp_clock *clock,
				    struct ptp_system_timestamp *sts)
{
	struct mlxsw_core *mlxsw_core = clock->core;
	u32 frc_h1, frc_h2, frc_l;

	frc_h1 = mlxsw_core_read_frc_h(mlxsw_core);
	ptp_read_system_prets(sts);
	frc_l = mlxsw_core_read_frc_l(mlxsw_core);
	ptp_read_system_postts(sts);
	frc_h2 = mlxsw_core_read_frc_h(mlxsw_core);

	if (frc_h1 != frc_h2) {
		/* wrap around */
		ptp_read_system_prets(sts);
		frc_l = mlxsw_core_read_frc_l(mlxsw_core);
		ptp_read_system_postts(sts);
	}

	return (u64) frc_l | (u64) frc_h2 << 32;
}

static u64 mlxsw_sp1_ptp_read_frc(const struct cyclecounter *cc)
{
	struct mlxsw_sp_ptp_clock *clock =
		container_of(cc, struct mlxsw_sp_ptp_clock, cycles);

	return __mlxsw_sp1_ptp_read_frc(clock, NULL) & cc->mask;
}

static int
mlxsw_sp1_ptp_phc_adjfreq(struct mlxsw_sp_ptp_clock *clock, int freq_adj)
{
	struct mlxsw_core *mlxsw_core = clock->core;
	char mtutc_pl[MLXSW_REG_MTUTC_LEN];

	mlxsw_reg_mtutc_pack(mtutc_pl, MLXSW_REG_MTUTC_OPERATION_ADJUST_FREQ,
			     freq_adj, 0);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mtutc), mtutc_pl);
}

static u64 mlxsw_sp1_ptp_ns2cycles(const struct timecounter *tc, u64 nsec)
{
	u64 cycles = (u64) nsec;

	cycles <<= tc->cc->shift;
	cycles = div_u64(cycles, tc->cc->mult);

	return cycles;
}

static int
mlxsw_sp1_ptp_phc_settime(struct mlxsw_sp_ptp_clock *clock, u64 nsec)
{
	struct mlxsw_core *mlxsw_core = clock->core;
	u64 next_sec, next_sec_in_nsec, cycles;
	char mtutc_pl[MLXSW_REG_MTUTC_LEN];
	char mtpps_pl[MLXSW_REG_MTPPS_LEN];
	int err;

	next_sec = div_u64(nsec, NSEC_PER_SEC) + 1;
	next_sec_in_nsec = next_sec * NSEC_PER_SEC;

	spin_lock(&clock->lock);
	cycles = mlxsw_sp1_ptp_ns2cycles(&clock->tc, next_sec_in_nsec);
	spin_unlock(&clock->lock);

	mlxsw_reg_mtpps_vpin_pack(mtpps_pl, cycles);
	err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(mtpps), mtpps_pl);
	if (err)
		return err;

	mlxsw_reg_mtutc_pack(mtutc_pl,
			     MLXSW_REG_MTUTC_OPERATION_SET_TIME_AT_NEXT_SEC,
			     0, next_sec);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mtutc), mtutc_pl);
}

static int mlxsw_sp1_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct mlxsw_sp_ptp_clock *clock =
		container_of(ptp, struct mlxsw_sp_ptp_clock, ptp_info);
	int neg_adj = 0;
	u32 diff;
	u64 adj;
	s32 ppb;

	ppb = scaled_ppm_to_ppb(scaled_ppm);

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}

	adj = clock->nominal_c_mult;
	adj *= ppb;
	diff = div_u64(adj, NSEC_PER_SEC);

	spin_lock(&clock->lock);
	timecounter_read(&clock->tc);
	clock->cycles.mult = neg_adj ? clock->nominal_c_mult - diff :
				       clock->nominal_c_mult + diff;
	spin_unlock(&clock->lock);

	return mlxsw_sp1_ptp_phc_adjfreq(clock, neg_adj ? -ppb : ppb);
}

static int mlxsw_sp1_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct mlxsw_sp_ptp_clock *clock =
		container_of(ptp, struct mlxsw_sp_ptp_clock, ptp_info);
	u64 nsec;

	spin_lock(&clock->lock);
	timecounter_adjtime(&clock->tc, delta);
	nsec = timecounter_read(&clock->tc);
	spin_unlock(&clock->lock);

	return mlxsw_sp1_ptp_phc_settime(clock, nsec);
}

static int mlxsw_sp1_ptp_gettimex(struct ptp_clock_info *ptp,
				  struct timespec64 *ts,
				  struct ptp_system_timestamp *sts)
{
	struct mlxsw_sp_ptp_clock *clock =
		container_of(ptp, struct mlxsw_sp_ptp_clock, ptp_info);
	u64 cycles, nsec;

	spin_lock(&clock->lock);
	cycles = __mlxsw_sp1_ptp_read_frc(clock, sts);
	nsec = timecounter_cyc2time(&clock->tc, cycles);
	spin_unlock(&clock->lock);

	*ts = ns_to_timespec64(nsec);

	return 0;
}

static int mlxsw_sp1_ptp_settime(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct mlxsw_sp_ptp_clock *clock =
		container_of(ptp, struct mlxsw_sp_ptp_clock, ptp_info);
	u64 nsec = timespec64_to_ns(ts);

	spin_lock(&clock->lock);
	timecounter_init(&clock->tc, &clock->cycles, nsec);
	nsec = timecounter_read(&clock->tc);
	spin_unlock(&clock->lock);

	return mlxsw_sp1_ptp_phc_settime(clock, nsec);
}

static const struct ptp_clock_info mlxsw_sp1_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "mlxsw_sp_clock",
	.max_adj	= 100000000,
	.adjfine	= mlxsw_sp1_ptp_adjfine,
	.adjtime	= mlxsw_sp1_ptp_adjtime,
	.gettimex64	= mlxsw_sp1_ptp_gettimex,
	.settime64	= mlxsw_sp1_ptp_settime,
};

static void mlxsw_sp1_ptp_clock_overflow(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mlxsw_sp_ptp_clock *clock;

	clock = container_of(dwork, struct mlxsw_sp_ptp_clock, overflow_work);

	spin_lock(&clock->lock);
	timecounter_read(&clock->tc);
	spin_unlock(&clock->lock);
	mlxsw_core_schedule_dw(&clock->overflow_work, clock->overflow_period);
}

struct mlxsw_sp_ptp_clock *
mlxsw_sp1_ptp_clock_init(struct mlxsw_sp *mlxsw_sp, struct device *dev)
{
	u64 overflow_cycles, nsec, frac = 0;
	struct mlxsw_sp_ptp_clock *clock;
	int err;

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&clock->lock);
	clock->cycles.read = mlxsw_sp1_ptp_read_frc;
	clock->cycles.shift = MLXSW_SP1_PTP_CLOCK_CYCLES_SHIFT;
	clock->cycles.mult = clocksource_khz2mult(MLXSW_SP1_PTP_CLOCK_FREQ_KHZ,
						  clock->cycles.shift);
	clock->nominal_c_mult = clock->cycles.mult;
	clock->cycles.mask = CLOCKSOURCE_MASK(MLXSW_SP1_PTP_CLOCK_MASK);
	clock->core = mlxsw_sp->core;

	timecounter_init(&clock->tc, &clock->cycles,
			 ktime_to_ns(ktime_get_real()));

	/* Calculate period in seconds to call the overflow watchdog - to make
	 * sure counter is checked at least twice every wrap around.
	 * The period is calculated as the minimum between max HW cycles count
	 * (The clock source mask) and max amount of cycles that can be
	 * multiplied by clock multiplier where the result doesn't exceed
	 * 64bits.
	 */
	overflow_cycles = div64_u64(~0ULL >> 1, clock->cycles.mult);
	overflow_cycles = min(overflow_cycles, div_u64(clock->cycles.mask, 3));

	nsec = cyclecounter_cyc2ns(&clock->cycles, overflow_cycles, 0, &frac);
	clock->overflow_period = nsecs_to_jiffies(nsec);

	INIT_DELAYED_WORK(&clock->overflow_work, mlxsw_sp1_ptp_clock_overflow);
	mlxsw_core_schedule_dw(&clock->overflow_work, 0);

	clock->ptp_info = mlxsw_sp1_ptp_clock_info;
	clock->ptp = ptp_clock_register(&clock->ptp_info, dev);
	if (IS_ERR(clock->ptp)) {
		err = PTR_ERR(clock->ptp);
		dev_err(dev, "ptp_clock_register failed %d\n", err);
		goto err_ptp_clock_register;
	}

	return clock;

err_ptp_clock_register:
	cancel_delayed_work_sync(&clock->overflow_work);
	kfree(clock);
	return ERR_PTR(err);
}

void mlxsw_sp1_ptp_clock_fini(struct mlxsw_sp_ptp_clock *clock)
{
	ptp_clock_unregister(clock->ptp);
	cancel_delayed_work_sync(&clock->overflow_work);
	kfree(clock);
}

void mlxsw_sp1_ptp_receive(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			   u8 local_port)
{
	mlxsw_sp_rx_listener_no_mark_func(skb, local_port, mlxsw_sp);
}

void mlxsw_sp1_ptp_transmitted(struct mlxsw_sp *mlxsw_sp,
			       struct sk_buff *skb, u8 local_port)
{
	dev_kfree_skb_any(skb);
}

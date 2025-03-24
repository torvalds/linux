// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019 Mellanox Technologies. All rights reserved */

#include <linux/ptp_clock_kernel.h>
#include <linux/clocksource.h>
#include <linux/timecounter.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/rhashtable.h>
#include <linux/ptp_classify.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/net_tstamp.h>
#include <linux/refcount.h>

#include "spectrum.h"
#include "spectrum_ptp.h"
#include "core.h"
#include "txheader.h"

#define MLXSW_SP1_PTP_CLOCK_CYCLES_SHIFT	29
#define MLXSW_SP1_PTP_CLOCK_FREQ_KHZ		156257 /* 6.4nSec */
#define MLXSW_SP1_PTP_CLOCK_MASK		64

#define MLXSW_SP1_PTP_HT_GC_INTERVAL		500 /* ms */

/* How long, approximately, should the unmatched entries stay in the hash table
 * before they are collected. Should be evenly divisible by the GC interval.
 */
#define MLXSW_SP1_PTP_HT_GC_TIMEOUT		1000 /* ms */

struct mlxsw_sp_ptp_state {
	struct mlxsw_sp *mlxsw_sp;
};

struct mlxsw_sp1_ptp_state {
	struct mlxsw_sp_ptp_state common;
	struct rhltable unmatched_ht;
	spinlock_t unmatched_lock; /* protects the HT */
	struct delayed_work ht_gc_dw;
	u32 gc_cycle;
};

struct mlxsw_sp2_ptp_state {
	struct mlxsw_sp_ptp_state common;
	refcount_t ptp_port_enabled_ref; /* Number of ports with time stamping
					  * enabled.
					  */
	struct hwtstamp_config config;
	struct mutex lock; /* Protects 'config' and HW configuration. */
};

struct mlxsw_sp1_ptp_key {
	u16 local_port;
	u8 message_type;
	u16 sequence_id;
	u8 domain_number;
	bool ingress;
};

struct mlxsw_sp1_ptp_unmatched {
	struct mlxsw_sp1_ptp_key key;
	struct rhlist_head ht_node;
	struct rcu_head rcu;
	struct sk_buff *skb;
	u64 timestamp;
	u32 gc_cycle;
};

static const struct rhashtable_params mlxsw_sp1_ptp_unmatched_ht_params = {
	.key_len = sizeof_field(struct mlxsw_sp1_ptp_unmatched, key),
	.key_offset = offsetof(struct mlxsw_sp1_ptp_unmatched, key),
	.head_offset = offsetof(struct mlxsw_sp1_ptp_unmatched, ht_node),
};

struct mlxsw_sp_ptp_clock {
	struct mlxsw_core *core;
	struct ptp_clock *ptp;
	struct ptp_clock_info ptp_info;
};

struct mlxsw_sp1_ptp_clock {
	struct mlxsw_sp_ptp_clock common;
	spinlock_t lock; /* protect this structure */
	struct cyclecounter cycles;
	struct timecounter tc;
	u32 nominal_c_mult;
	unsigned long overflow_period;
	struct delayed_work overflow_work;
};

static struct mlxsw_sp1_ptp_state *
mlxsw_sp1_ptp_state(struct mlxsw_sp *mlxsw_sp)
{
	return container_of(mlxsw_sp->ptp_state, struct mlxsw_sp1_ptp_state,
			    common);
}

static struct mlxsw_sp2_ptp_state *
mlxsw_sp2_ptp_state(struct mlxsw_sp *mlxsw_sp)
{
	return container_of(mlxsw_sp->ptp_state, struct mlxsw_sp2_ptp_state,
			    common);
}

static struct mlxsw_sp1_ptp_clock *
mlxsw_sp1_ptp_clock(struct ptp_clock_info *ptp)
{
	return container_of(ptp, struct mlxsw_sp1_ptp_clock, common.ptp_info);
}

static u64 __mlxsw_sp1_ptp_read_frc(struct mlxsw_sp1_ptp_clock *clock,
				    struct ptp_system_timestamp *sts)
{
	struct mlxsw_core *mlxsw_core = clock->common.core;
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
	struct mlxsw_sp1_ptp_clock *clock =
		container_of(cc, struct mlxsw_sp1_ptp_clock, cycles);

	return __mlxsw_sp1_ptp_read_frc(clock, NULL) & cc->mask;
}

static int
mlxsw_sp_ptp_phc_adjfreq(struct mlxsw_sp_ptp_clock *clock, int freq_adj)
{
	struct mlxsw_core *mlxsw_core = clock->core;
	char mtutc_pl[MLXSW_REG_MTUTC_LEN];

	mlxsw_reg_mtutc_pack(mtutc_pl, MLXSW_REG_MTUTC_OPERATION_ADJUST_FREQ,
			     freq_adj, 0, 0, 0);
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
mlxsw_sp1_ptp_phc_settime(struct mlxsw_sp1_ptp_clock *clock, u64 nsec)
{
	struct mlxsw_core *mlxsw_core = clock->common.core;
	u64 next_sec, next_sec_in_nsec, cycles;
	char mtutc_pl[MLXSW_REG_MTUTC_LEN];
	char mtpps_pl[MLXSW_REG_MTPPS_LEN];
	int err;

	next_sec = div_u64(nsec, NSEC_PER_SEC) + 1;
	next_sec_in_nsec = next_sec * NSEC_PER_SEC;

	spin_lock_bh(&clock->lock);
	cycles = mlxsw_sp1_ptp_ns2cycles(&clock->tc, next_sec_in_nsec);
	spin_unlock_bh(&clock->lock);

	mlxsw_reg_mtpps_vpin_pack(mtpps_pl, cycles);
	err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(mtpps), mtpps_pl);
	if (err)
		return err;

	mlxsw_reg_mtutc_pack(mtutc_pl,
			     MLXSW_REG_MTUTC_OPERATION_SET_TIME_AT_NEXT_SEC,
			     0, next_sec, 0, 0);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mtutc), mtutc_pl);
}

static int mlxsw_sp1_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct mlxsw_sp1_ptp_clock *clock = mlxsw_sp1_ptp_clock(ptp);
	s32 ppb;

	ppb = scaled_ppm_to_ppb(scaled_ppm);

	spin_lock_bh(&clock->lock);
	timecounter_read(&clock->tc);
	clock->cycles.mult = adjust_by_scaled_ppm(clock->nominal_c_mult,
						  scaled_ppm);
	spin_unlock_bh(&clock->lock);

	return mlxsw_sp_ptp_phc_adjfreq(&clock->common, ppb);
}

static int mlxsw_sp1_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct mlxsw_sp1_ptp_clock *clock = mlxsw_sp1_ptp_clock(ptp);
	u64 nsec;

	spin_lock_bh(&clock->lock);
	timecounter_adjtime(&clock->tc, delta);
	nsec = timecounter_read(&clock->tc);
	spin_unlock_bh(&clock->lock);

	return mlxsw_sp1_ptp_phc_settime(clock, nsec);
}

static int mlxsw_sp1_ptp_gettimex(struct ptp_clock_info *ptp,
				  struct timespec64 *ts,
				  struct ptp_system_timestamp *sts)
{
	struct mlxsw_sp1_ptp_clock *clock = mlxsw_sp1_ptp_clock(ptp);
	u64 cycles, nsec;

	spin_lock_bh(&clock->lock);
	cycles = __mlxsw_sp1_ptp_read_frc(clock, sts);
	nsec = timecounter_cyc2time(&clock->tc, cycles);
	spin_unlock_bh(&clock->lock);

	*ts = ns_to_timespec64(nsec);

	return 0;
}

static int mlxsw_sp1_ptp_settime(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct mlxsw_sp1_ptp_clock *clock = mlxsw_sp1_ptp_clock(ptp);
	u64 nsec = timespec64_to_ns(ts);

	spin_lock_bh(&clock->lock);
	timecounter_init(&clock->tc, &clock->cycles, nsec);
	nsec = timecounter_read(&clock->tc);
	spin_unlock_bh(&clock->lock);

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
	struct mlxsw_sp1_ptp_clock *clock;

	clock = container_of(dwork, struct mlxsw_sp1_ptp_clock, overflow_work);

	spin_lock_bh(&clock->lock);
	timecounter_read(&clock->tc);
	spin_unlock_bh(&clock->lock);
	mlxsw_core_schedule_dw(&clock->overflow_work, clock->overflow_period);
}

struct mlxsw_sp_ptp_clock *
mlxsw_sp1_ptp_clock_init(struct mlxsw_sp *mlxsw_sp, struct device *dev)
{
	u64 overflow_cycles, nsec, frac = 0;
	struct mlxsw_sp1_ptp_clock *clock;
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
	clock->common.core = mlxsw_sp->core;

	timecounter_init(&clock->tc, &clock->cycles, 0);

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

	clock->common.ptp_info = mlxsw_sp1_ptp_clock_info;
	clock->common.ptp = ptp_clock_register(&clock->common.ptp_info, dev);
	if (IS_ERR(clock->common.ptp)) {
		err = PTR_ERR(clock->common.ptp);
		dev_err(dev, "ptp_clock_register failed %d\n", err);
		goto err_ptp_clock_register;
	}

	return &clock->common;

err_ptp_clock_register:
	cancel_delayed_work_sync(&clock->overflow_work);
	kfree(clock);
	return ERR_PTR(err);
}

void mlxsw_sp1_ptp_clock_fini(struct mlxsw_sp_ptp_clock *clock_common)
{
	struct mlxsw_sp1_ptp_clock *clock =
		container_of(clock_common, struct mlxsw_sp1_ptp_clock, common);

	ptp_clock_unregister(clock_common->ptp);
	cancel_delayed_work_sync(&clock->overflow_work);
	kfree(clock);
}

static u64 mlxsw_sp2_ptp_read_utc(struct mlxsw_sp_ptp_clock *clock,
				  struct ptp_system_timestamp *sts)
{
	struct mlxsw_core *mlxsw_core = clock->core;
	u32 utc_sec1, utc_sec2, utc_nsec;

	utc_sec1 = mlxsw_core_read_utc_sec(mlxsw_core);
	ptp_read_system_prets(sts);
	utc_nsec = mlxsw_core_read_utc_nsec(mlxsw_core);
	ptp_read_system_postts(sts);
	utc_sec2 = mlxsw_core_read_utc_sec(mlxsw_core);

	if (utc_sec1 != utc_sec2) {
		/* Wrap around. */
		ptp_read_system_prets(sts);
		utc_nsec = mlxsw_core_read_utc_nsec(mlxsw_core);
		ptp_read_system_postts(sts);
	}

	return (u64)utc_sec2 * NSEC_PER_SEC + utc_nsec;
}

static int
mlxsw_sp2_ptp_phc_settime(struct mlxsw_sp_ptp_clock *clock, u64 nsec)
{
	struct mlxsw_core *mlxsw_core = clock->core;
	char mtutc_pl[MLXSW_REG_MTUTC_LEN];
	u32 sec, nsec_rem;

	sec = div_u64_rem(nsec, NSEC_PER_SEC, &nsec_rem);
	mlxsw_reg_mtutc_pack(mtutc_pl,
			     MLXSW_REG_MTUTC_OPERATION_SET_TIME_IMMEDIATE,
			     0, sec, nsec_rem, 0);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mtutc), mtutc_pl);
}

static int mlxsw_sp2_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct mlxsw_sp_ptp_clock *clock =
		container_of(ptp, struct mlxsw_sp_ptp_clock, ptp_info);
	s32 ppb = scaled_ppm_to_ppb(scaled_ppm);

	/* In Spectrum-2 and newer ASICs, the frequency adjustment in MTUTC is
	 * reversed, positive values mean to decrease the frequency. Adjust the
	 * sign of PPB to this behavior.
	 */
	return mlxsw_sp_ptp_phc_adjfreq(clock, -ppb);
}

static int mlxsw_sp2_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct mlxsw_sp_ptp_clock *clock =
		container_of(ptp, struct mlxsw_sp_ptp_clock, ptp_info);
	struct mlxsw_core *mlxsw_core = clock->core;
	char mtutc_pl[MLXSW_REG_MTUTC_LEN];

	/* HW time adjustment range is s16. If out of range, set time instead. */
	if (delta < S16_MIN || delta > S16_MAX) {
		u64 nsec;

		nsec = mlxsw_sp2_ptp_read_utc(clock, NULL);
		nsec += delta;

		return mlxsw_sp2_ptp_phc_settime(clock, nsec);
	}

	mlxsw_reg_mtutc_pack(mtutc_pl,
			     MLXSW_REG_MTUTC_OPERATION_ADJUST_TIME,
			     0, 0, 0, delta);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mtutc), mtutc_pl);
}

static int mlxsw_sp2_ptp_gettimex(struct ptp_clock_info *ptp,
				  struct timespec64 *ts,
				  struct ptp_system_timestamp *sts)
{
	struct mlxsw_sp_ptp_clock *clock =
		container_of(ptp, struct mlxsw_sp_ptp_clock, ptp_info);
	u64 nsec;

	nsec = mlxsw_sp2_ptp_read_utc(clock, sts);
	*ts = ns_to_timespec64(nsec);

	return 0;
}

static int mlxsw_sp2_ptp_settime(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct mlxsw_sp_ptp_clock *clock =
		container_of(ptp, struct mlxsw_sp_ptp_clock, ptp_info);
	u64 nsec = timespec64_to_ns(ts);

	return mlxsw_sp2_ptp_phc_settime(clock, nsec);
}

static const struct ptp_clock_info mlxsw_sp2_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "mlxsw_sp_clock",
	.max_adj	= MLXSW_REG_MTUTC_MAX_FREQ_ADJ,
	.adjfine	= mlxsw_sp2_ptp_adjfine,
	.adjtime	= mlxsw_sp2_ptp_adjtime,
	.gettimex64	= mlxsw_sp2_ptp_gettimex,
	.settime64	= mlxsw_sp2_ptp_settime,
};

struct mlxsw_sp_ptp_clock *
mlxsw_sp2_ptp_clock_init(struct mlxsw_sp *mlxsw_sp, struct device *dev)
{
	struct mlxsw_sp_ptp_clock *clock;
	int err;

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return ERR_PTR(-ENOMEM);

	clock->core = mlxsw_sp->core;

	clock->ptp_info = mlxsw_sp2_ptp_clock_info;

	err = mlxsw_sp2_ptp_phc_settime(clock, 0);
	if (err) {
		dev_err(dev, "setting UTC time failed %d\n", err);
		goto err_ptp_phc_settime;
	}

	clock->ptp = ptp_clock_register(&clock->ptp_info, dev);
	if (IS_ERR(clock->ptp)) {
		err = PTR_ERR(clock->ptp);
		dev_err(dev, "ptp_clock_register failed %d\n", err);
		goto err_ptp_clock_register;
	}

	return clock;

err_ptp_clock_register:
err_ptp_phc_settime:
	kfree(clock);
	return ERR_PTR(err);
}

void mlxsw_sp2_ptp_clock_fini(struct mlxsw_sp_ptp_clock *clock)
{
	ptp_clock_unregister(clock->ptp);
	kfree(clock);
}

static int mlxsw_sp_ptp_parse(struct sk_buff *skb,
			      u8 *p_domain_number,
			      u8 *p_message_type,
			      u16 *p_sequence_id)
{
	unsigned int ptp_class;
	struct ptp_header *hdr;

	ptp_class = ptp_classify_raw(skb);

	switch (ptp_class & PTP_CLASS_VMASK) {
	case PTP_CLASS_V1:
	case PTP_CLASS_V2:
		break;
	default:
		return -ERANGE;
	}

	hdr = ptp_parse_header(skb, ptp_class);
	if (!hdr)
		return -EINVAL;

	*p_message_type	 = ptp_get_msgtype(hdr, ptp_class);
	*p_domain_number = hdr->domain_number;
	*p_sequence_id	 = be16_to_cpu(hdr->sequence_id);

	return 0;
}

/* Returns NULL on successful insertion, a pointer on conflict, or an ERR_PTR on
 * error.
 */
static int
mlxsw_sp1_ptp_unmatched_save(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp1_ptp_key key,
			     struct sk_buff *skb,
			     u64 timestamp)
{
	int cycles = MLXSW_SP1_PTP_HT_GC_TIMEOUT / MLXSW_SP1_PTP_HT_GC_INTERVAL;
	struct mlxsw_sp1_ptp_state *ptp_state = mlxsw_sp1_ptp_state(mlxsw_sp);
	struct mlxsw_sp1_ptp_unmatched *unmatched;
	int err;

	unmatched = kzalloc(sizeof(*unmatched), GFP_ATOMIC);
	if (!unmatched)
		return -ENOMEM;

	unmatched->key = key;
	unmatched->skb = skb;
	unmatched->timestamp = timestamp;
	unmatched->gc_cycle = ptp_state->gc_cycle + cycles;

	err = rhltable_insert(&ptp_state->unmatched_ht, &unmatched->ht_node,
			      mlxsw_sp1_ptp_unmatched_ht_params);
	if (err)
		kfree(unmatched);

	return err;
}

static struct mlxsw_sp1_ptp_unmatched *
mlxsw_sp1_ptp_unmatched_lookup(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp1_ptp_key key, int *p_length)
{
	struct mlxsw_sp1_ptp_state *ptp_state = mlxsw_sp1_ptp_state(mlxsw_sp);
	struct mlxsw_sp1_ptp_unmatched *unmatched, *last = NULL;
	struct rhlist_head *tmp, *list;
	int length = 0;

	list = rhltable_lookup(&ptp_state->unmatched_ht, &key,
			       mlxsw_sp1_ptp_unmatched_ht_params);
	rhl_for_each_entry_rcu(unmatched, tmp, list, ht_node) {
		last = unmatched;
		length++;
	}

	*p_length = length;
	return last;
}

static int
mlxsw_sp1_ptp_unmatched_remove(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp1_ptp_unmatched *unmatched)
{
	struct mlxsw_sp1_ptp_state *ptp_state = mlxsw_sp1_ptp_state(mlxsw_sp);

	return rhltable_remove(&ptp_state->unmatched_ht,
			       &unmatched->ht_node,
			       mlxsw_sp1_ptp_unmatched_ht_params);
}

/* This function is called in the following scenarios:
 *
 * 1) When a packet is matched with its timestamp.
 * 2) In several situation when it is necessary to immediately pass on
 *    an SKB without a timestamp.
 * 3) From GC indirectly through mlxsw_sp1_ptp_unmatched_finish().
 *    This case is similar to 2) above.
 */
static void mlxsw_sp1_ptp_packet_finish(struct mlxsw_sp *mlxsw_sp,
					struct sk_buff *skb, u16 local_port,
					bool ingress,
					struct skb_shared_hwtstamps *hwtstamps)
{
	struct mlxsw_sp_port *mlxsw_sp_port;

	/* Between capturing the packet and finishing it, there is a window of
	 * opportunity for the originating port to go away (e.g. due to a
	 * split). Also make sure the SKB device reference is still valid.
	 */
	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!(mlxsw_sp_port && (!skb->dev || skb->dev == mlxsw_sp_port->dev))) {
		dev_kfree_skb_any(skb);
		return;
	}

	if (ingress) {
		if (hwtstamps)
			*skb_hwtstamps(skb) = *hwtstamps;
		mlxsw_sp_rx_listener_no_mark_func(skb, local_port, mlxsw_sp);
	} else {
		/* skb_tstamp_tx() allows hwtstamps to be NULL. */
		skb_tstamp_tx(skb, hwtstamps);
		dev_kfree_skb_any(skb);
	}
}

static void mlxsw_sp1_packet_timestamp(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp1_ptp_key key,
				       struct sk_buff *skb,
				       u64 timestamp)
{
	struct mlxsw_sp_ptp_clock *clock_common = mlxsw_sp->clock;
	struct mlxsw_sp1_ptp_clock *clock =
		container_of(clock_common, struct mlxsw_sp1_ptp_clock, common);

	struct skb_shared_hwtstamps hwtstamps;
	u64 nsec;

	spin_lock_bh(&clock->lock);
	nsec = timecounter_cyc2time(&clock->tc, timestamp);
	spin_unlock_bh(&clock->lock);

	hwtstamps.hwtstamp = ns_to_ktime(nsec);
	mlxsw_sp1_ptp_packet_finish(mlxsw_sp, skb,
				    key.local_port, key.ingress, &hwtstamps);
}

static void
mlxsw_sp1_ptp_unmatched_finish(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp1_ptp_unmatched *unmatched)
{
	if (unmatched->skb && unmatched->timestamp)
		mlxsw_sp1_packet_timestamp(mlxsw_sp, unmatched->key,
					   unmatched->skb,
					   unmatched->timestamp);
	else if (unmatched->skb)
		mlxsw_sp1_ptp_packet_finish(mlxsw_sp, unmatched->skb,
					    unmatched->key.local_port,
					    unmatched->key.ingress, NULL);
	kfree_rcu(unmatched, rcu);
}

static void mlxsw_sp1_ptp_unmatched_free_fn(void *ptr, void *arg)
{
	struct mlxsw_sp1_ptp_unmatched *unmatched = ptr;

	/* This is invoked at a point where the ports are gone already. Nothing
	 * to do with whatever is left in the HT but to free it.
	 */
	if (unmatched->skb)
		dev_kfree_skb_any(unmatched->skb);
	kfree_rcu(unmatched, rcu);
}

static void mlxsw_sp1_ptp_got_piece(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp1_ptp_key key,
				    struct sk_buff *skb, u64 timestamp)
{
	struct mlxsw_sp1_ptp_state *ptp_state = mlxsw_sp1_ptp_state(mlxsw_sp);
	struct mlxsw_sp1_ptp_unmatched *unmatched;
	int length;
	int err;

	rcu_read_lock();

	spin_lock(&ptp_state->unmatched_lock);

	unmatched = mlxsw_sp1_ptp_unmatched_lookup(mlxsw_sp, key, &length);
	if (skb && unmatched && unmatched->timestamp) {
		unmatched->skb = skb;
	} else if (timestamp && unmatched && unmatched->skb) {
		unmatched->timestamp = timestamp;
	} else {
		/* Either there is no entry to match, or one that is there is
		 * incompatible.
		 */
		if (length < 100)
			err = mlxsw_sp1_ptp_unmatched_save(mlxsw_sp, key,
							   skb, timestamp);
		else
			err = -E2BIG;
		if (err && skb)
			mlxsw_sp1_ptp_packet_finish(mlxsw_sp, skb,
						    key.local_port,
						    key.ingress, NULL);
		unmatched = NULL;
	}

	if (unmatched) {
		err = mlxsw_sp1_ptp_unmatched_remove(mlxsw_sp, unmatched);
		WARN_ON_ONCE(err);
	}

	spin_unlock(&ptp_state->unmatched_lock);

	if (unmatched)
		mlxsw_sp1_ptp_unmatched_finish(mlxsw_sp, unmatched);

	rcu_read_unlock();
}

static void mlxsw_sp1_ptp_got_packet(struct mlxsw_sp *mlxsw_sp,
				     struct sk_buff *skb, u16 local_port,
				     bool ingress)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp1_ptp_key key;
	u8 types;
	int err;

	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port)
		goto immediate;

	types = ingress ? mlxsw_sp_port->ptp.ing_types :
			  mlxsw_sp_port->ptp.egr_types;
	if (!types)
		goto immediate;

	memset(&key, 0, sizeof(key));
	key.local_port = local_port;
	key.ingress = ingress;

	err = mlxsw_sp_ptp_parse(skb, &key.domain_number, &key.message_type,
				 &key.sequence_id);
	if (err)
		goto immediate;

	/* For packets whose timestamping was not enabled on this port, don't
	 * bother trying to match the timestamp.
	 */
	if (!((1 << key.message_type) & types))
		goto immediate;

	mlxsw_sp1_ptp_got_piece(mlxsw_sp, key, skb, 0);
	return;

immediate:
	mlxsw_sp1_ptp_packet_finish(mlxsw_sp, skb, local_port, ingress, NULL);
}

void mlxsw_sp1_ptp_got_timestamp(struct mlxsw_sp *mlxsw_sp, bool ingress,
				 u16 local_port, u8 message_type,
				 u8 domain_number, u16 sequence_id,
				 u64 timestamp)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp1_ptp_key key;
	u8 types;

	if (WARN_ON_ONCE(!mlxsw_sp_local_port_is_valid(mlxsw_sp, local_port)))
		return;
	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port)
		return;

	types = ingress ? mlxsw_sp_port->ptp.ing_types :
			  mlxsw_sp_port->ptp.egr_types;

	/* For message types whose timestamping was not enabled on this port,
	 * don't bother with the timestamp.
	 */
	if (!((1 << message_type) & types))
		return;

	memset(&key, 0, sizeof(key));
	key.local_port = local_port;
	key.domain_number = domain_number;
	key.message_type = message_type;
	key.sequence_id = sequence_id;
	key.ingress = ingress;

	mlxsw_sp1_ptp_got_piece(mlxsw_sp, key, NULL, timestamp);
}

void mlxsw_sp1_ptp_receive(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			   u16 local_port)
{
	skb_reset_mac_header(skb);
	mlxsw_sp1_ptp_got_packet(mlxsw_sp, skb, local_port, true);
}

void mlxsw_sp1_ptp_transmitted(struct mlxsw_sp *mlxsw_sp,
			       struct sk_buff *skb, u16 local_port)
{
	mlxsw_sp1_ptp_got_packet(mlxsw_sp, skb, local_port, false);
}

static void
mlxsw_sp1_ptp_ht_gc_collect(struct mlxsw_sp1_ptp_state *ptp_state,
			    struct mlxsw_sp1_ptp_unmatched *unmatched)
{
	struct mlxsw_sp *mlxsw_sp = ptp_state->common.mlxsw_sp;
	struct mlxsw_sp_ptp_port_dir_stats *stats;
	struct mlxsw_sp_port *mlxsw_sp_port;
	int err;

	/* If an unmatched entry has an SKB, it has to be handed over to the
	 * networking stack. This is usually done from a trap handler, which is
	 * invoked in a softirq context. Here we are going to do it in process
	 * context. If that were to be interrupted by a softirq, it could cause
	 * a deadlock when an attempt is made to take an already-taken lock
	 * somewhere along the sending path. Disable softirqs to prevent this.
	 */
	local_bh_disable();

	spin_lock(&ptp_state->unmatched_lock);
	err = rhltable_remove(&ptp_state->unmatched_ht, &unmatched->ht_node,
			      mlxsw_sp1_ptp_unmatched_ht_params);
	spin_unlock(&ptp_state->unmatched_lock);

	if (err)
		/* The packet was matched with timestamp during the walk. */
		goto out;

	mlxsw_sp_port = mlxsw_sp->ports[unmatched->key.local_port];
	if (mlxsw_sp_port) {
		stats = unmatched->key.ingress ?
			&mlxsw_sp_port->ptp.stats.rx_gcd :
			&mlxsw_sp_port->ptp.stats.tx_gcd;
		if (unmatched->skb)
			stats->packets++;
		else
			stats->timestamps++;
	}

	/* mlxsw_sp1_ptp_unmatched_finish() invokes netif_receive_skb(). While
	 * the comment at that function states that it can only be called in
	 * soft IRQ context, this pattern of local_bh_disable() +
	 * netif_receive_skb(), in process context, is seen elsewhere in the
	 * kernel, notably in pktgen.
	 */
	mlxsw_sp1_ptp_unmatched_finish(mlxsw_sp, unmatched);

out:
	local_bh_enable();
}

static void mlxsw_sp1_ptp_ht_gc(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mlxsw_sp1_ptp_unmatched *unmatched;
	struct mlxsw_sp1_ptp_state *ptp_state;
	struct rhashtable_iter iter;
	u32 gc_cycle;
	void *obj;

	ptp_state = container_of(dwork, struct mlxsw_sp1_ptp_state, ht_gc_dw);
	gc_cycle = ptp_state->gc_cycle++;

	rhltable_walk_enter(&ptp_state->unmatched_ht, &iter);
	rhashtable_walk_start(&iter);
	while ((obj = rhashtable_walk_next(&iter))) {
		if (IS_ERR(obj))
			continue;

		unmatched = obj;
		if (unmatched->gc_cycle <= gc_cycle)
			mlxsw_sp1_ptp_ht_gc_collect(ptp_state, unmatched);
	}
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);

	mlxsw_core_schedule_dw(&ptp_state->ht_gc_dw,
			       MLXSW_SP1_PTP_HT_GC_INTERVAL);
}

static int mlxsw_sp_ptp_mtptpt_set(struct mlxsw_sp *mlxsw_sp,
				   enum mlxsw_reg_mtptpt_trap_id trap_id,
				   u16 message_type)
{
	char mtptpt_pl[MLXSW_REG_MTPTPT_LEN];

	mlxsw_reg_mtptpt_pack(mtptpt_pl, trap_id, message_type);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mtptpt), mtptpt_pl);
}

static int mlxsw_sp1_ptp_set_fifo_clr_on_trap(struct mlxsw_sp *mlxsw_sp,
					      bool clr)
{
	char mogcr_pl[MLXSW_REG_MOGCR_LEN] = {0};
	int err;

	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(mogcr), mogcr_pl);
	if (err)
		return err;

	mlxsw_reg_mogcr_ptp_iftc_set(mogcr_pl, clr);
	mlxsw_reg_mogcr_ptp_eftc_set(mogcr_pl, clr);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mogcr), mogcr_pl);
}

static int mlxsw_sp1_ptp_mtpppc_set(struct mlxsw_sp *mlxsw_sp,
				    u16 ing_types, u16 egr_types)
{
	char mtpppc_pl[MLXSW_REG_MTPPPC_LEN];

	mlxsw_reg_mtpppc_pack(mtpppc_pl, ing_types, egr_types);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mtpppc), mtpppc_pl);
}

struct mlxsw_sp1_ptp_shaper_params {
	u32 ethtool_speed;
	enum mlxsw_reg_qpsc_port_speed port_speed;
	u8 shaper_time_exp;
	u8 shaper_time_mantissa;
	u8 shaper_inc;
	u8 shaper_bs;
	u8 port_to_shaper_credits;
	int ing_timestamp_inc;
	int egr_timestamp_inc;
};

static const struct mlxsw_sp1_ptp_shaper_params
mlxsw_sp1_ptp_shaper_params[] = {
	{
		.ethtool_speed		= SPEED_100,
		.port_speed		= MLXSW_REG_QPSC_PORT_SPEED_100M,
		.shaper_time_exp	= 4,
		.shaper_time_mantissa	= 12,
		.shaper_inc		= 9,
		.shaper_bs		= 1,
		.port_to_shaper_credits	= 1,
		.ing_timestamp_inc	= -313,
		.egr_timestamp_inc	= 313,
	},
	{
		.ethtool_speed		= SPEED_1000,
		.port_speed		= MLXSW_REG_QPSC_PORT_SPEED_1G,
		.shaper_time_exp	= 0,
		.shaper_time_mantissa	= 12,
		.shaper_inc		= 6,
		.shaper_bs		= 0,
		.port_to_shaper_credits	= 1,
		.ing_timestamp_inc	= -35,
		.egr_timestamp_inc	= 35,
	},
	{
		.ethtool_speed		= SPEED_10000,
		.port_speed		= MLXSW_REG_QPSC_PORT_SPEED_10G,
		.shaper_time_exp	= 0,
		.shaper_time_mantissa	= 2,
		.shaper_inc		= 14,
		.shaper_bs		= 1,
		.port_to_shaper_credits	= 1,
		.ing_timestamp_inc	= -11,
		.egr_timestamp_inc	= 11,
	},
	{
		.ethtool_speed		= SPEED_25000,
		.port_speed		= MLXSW_REG_QPSC_PORT_SPEED_25G,
		.shaper_time_exp	= 0,
		.shaper_time_mantissa	= 0,
		.shaper_inc		= 11,
		.shaper_bs		= 1,
		.port_to_shaper_credits	= 1,
		.ing_timestamp_inc	= -14,
		.egr_timestamp_inc	= 14,
	},
};

#define MLXSW_SP1_PTP_SHAPER_PARAMS_LEN ARRAY_SIZE(mlxsw_sp1_ptp_shaper_params)

static int mlxsw_sp1_ptp_shaper_params_set(struct mlxsw_sp *mlxsw_sp)
{
	const struct mlxsw_sp1_ptp_shaper_params *params;
	char qpsc_pl[MLXSW_REG_QPSC_LEN];
	int i, err;

	for (i = 0; i < MLXSW_SP1_PTP_SHAPER_PARAMS_LEN; i++) {
		params = &mlxsw_sp1_ptp_shaper_params[i];
		mlxsw_reg_qpsc_pack(qpsc_pl, params->port_speed,
				    params->shaper_time_exp,
				    params->shaper_time_mantissa,
				    params->shaper_inc, params->shaper_bs,
				    params->port_to_shaper_credits,
				    params->ing_timestamp_inc,
				    params->egr_timestamp_inc);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qpsc), qpsc_pl);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_ptp_traps_set(struct mlxsw_sp *mlxsw_sp)
{
	u16 event_message_type;
	int err;

	/* Deliver these message types as PTP0. */
	event_message_type = BIT(PTP_MSGTYPE_SYNC) |
			     BIT(PTP_MSGTYPE_DELAY_REQ) |
			     BIT(PTP_MSGTYPE_PDELAY_REQ) |
			     BIT(PTP_MSGTYPE_PDELAY_RESP);

	err = mlxsw_sp_ptp_mtptpt_set(mlxsw_sp, MLXSW_REG_MTPTPT_TRAP_ID_PTP0,
				      event_message_type);
	if (err)
		return err;

	/* Everything else is PTP1. */
	err = mlxsw_sp_ptp_mtptpt_set(mlxsw_sp, MLXSW_REG_MTPTPT_TRAP_ID_PTP1,
				      ~event_message_type);
	if (err)
		goto err_mtptpt1_set;

	return 0;

err_mtptpt1_set:
	mlxsw_sp_ptp_mtptpt_set(mlxsw_sp, MLXSW_REG_MTPTPT_TRAP_ID_PTP0, 0);
	return err;
}

static void mlxsw_sp_ptp_traps_unset(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp_ptp_mtptpt_set(mlxsw_sp, MLXSW_REG_MTPTPT_TRAP_ID_PTP1, 0);
	mlxsw_sp_ptp_mtptpt_set(mlxsw_sp, MLXSW_REG_MTPTPT_TRAP_ID_PTP0, 0);
}

struct mlxsw_sp_ptp_state *mlxsw_sp1_ptp_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp1_ptp_state *ptp_state;
	int err;

	err = mlxsw_sp1_ptp_shaper_params_set(mlxsw_sp);
	if (err)
		return ERR_PTR(err);

	ptp_state = kzalloc(sizeof(*ptp_state), GFP_KERNEL);
	if (!ptp_state)
		return ERR_PTR(-ENOMEM);
	ptp_state->common.mlxsw_sp = mlxsw_sp;

	spin_lock_init(&ptp_state->unmatched_lock);

	err = rhltable_init(&ptp_state->unmatched_ht,
			    &mlxsw_sp1_ptp_unmatched_ht_params);
	if (err)
		goto err_hashtable_init;

	err = mlxsw_sp_ptp_traps_set(mlxsw_sp);
	if (err)
		goto err_ptp_traps_set;

	err = mlxsw_sp1_ptp_set_fifo_clr_on_trap(mlxsw_sp, true);
	if (err)
		goto err_fifo_clr;

	INIT_DELAYED_WORK(&ptp_state->ht_gc_dw, mlxsw_sp1_ptp_ht_gc);
	mlxsw_core_schedule_dw(&ptp_state->ht_gc_dw,
			       MLXSW_SP1_PTP_HT_GC_INTERVAL);
	return &ptp_state->common;

err_fifo_clr:
	mlxsw_sp_ptp_traps_unset(mlxsw_sp);
err_ptp_traps_set:
	rhltable_destroy(&ptp_state->unmatched_ht);
err_hashtable_init:
	kfree(ptp_state);
	return ERR_PTR(err);
}

void mlxsw_sp1_ptp_fini(struct mlxsw_sp_ptp_state *ptp_state_common)
{
	struct mlxsw_sp *mlxsw_sp = ptp_state_common->mlxsw_sp;
	struct mlxsw_sp1_ptp_state *ptp_state;

	ptp_state = mlxsw_sp1_ptp_state(mlxsw_sp);

	cancel_delayed_work_sync(&ptp_state->ht_gc_dw);
	mlxsw_sp1_ptp_mtpppc_set(mlxsw_sp, 0, 0);
	mlxsw_sp1_ptp_set_fifo_clr_on_trap(mlxsw_sp, false);
	mlxsw_sp_ptp_traps_unset(mlxsw_sp);
	rhltable_free_and_destroy(&ptp_state->unmatched_ht,
				  &mlxsw_sp1_ptp_unmatched_free_fn, NULL);
	kfree(ptp_state);
}

int mlxsw_sp1_ptp_hwtstamp_get(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct hwtstamp_config *config)
{
	*config = mlxsw_sp_port->ptp.hwtstamp_config;
	return 0;
}

static int
mlxsw_sp1_ptp_get_message_types(const struct hwtstamp_config *config,
				u16 *p_ing_types, u16 *p_egr_types,
				enum hwtstamp_rx_filters *p_rx_filter)
{
	enum hwtstamp_rx_filters rx_filter = config->rx_filter;
	enum hwtstamp_tx_types tx_type = config->tx_type;
	u16 ing_types = 0x00;
	u16 egr_types = 0x00;

	switch (tx_type) {
	case HWTSTAMP_TX_OFF:
		egr_types = 0x00;
		break;
	case HWTSTAMP_TX_ON:
		egr_types = 0xff;
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
	case HWTSTAMP_TX_ONESTEP_P2P:
		return -ERANGE;
	default:
		return -EINVAL;
	}

	switch (rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		ing_types = 0x00;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
		ing_types = 0x01;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		ing_types = 0x02;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		ing_types = 0x0f;
		break;
	case HWTSTAMP_FILTER_ALL:
		ing_types = 0xff;
		break;
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_NTP_ALL:
		return -ERANGE;
	default:
		return -EINVAL;
	}

	*p_ing_types = ing_types;
	*p_egr_types = egr_types;
	*p_rx_filter = rx_filter;
	return 0;
}

static int mlxsw_sp1_ptp_mtpppc_update(struct mlxsw_sp_port *mlxsw_sp_port,
				       u16 ing_types, u16 egr_types)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port *tmp;
	u16 orig_ing_types = 0;
	u16 orig_egr_types = 0;
	int err;
	int i;

	/* MTPPPC configures timestamping globally, not per port. Find the
	 * configuration that contains all configured timestamping requests.
	 */
	for (i = 1; i < mlxsw_core_max_ports(mlxsw_sp->core); i++) {
		tmp = mlxsw_sp->ports[i];
		if (tmp) {
			orig_ing_types |= tmp->ptp.ing_types;
			orig_egr_types |= tmp->ptp.egr_types;
		}
		if (tmp && tmp != mlxsw_sp_port) {
			ing_types |= tmp->ptp.ing_types;
			egr_types |= tmp->ptp.egr_types;
		}
	}

	if ((ing_types || egr_types) && !(orig_ing_types || orig_egr_types)) {
		err = mlxsw_sp_parsing_depth_inc(mlxsw_sp);
		if (err) {
			netdev_err(mlxsw_sp_port->dev, "Failed to increase parsing depth");
			return err;
		}
	}
	if (!(ing_types || egr_types) && (orig_ing_types || orig_egr_types))
		mlxsw_sp_parsing_depth_dec(mlxsw_sp);

	return mlxsw_sp1_ptp_mtpppc_set(mlxsw_sp_port->mlxsw_sp,
				       ing_types, egr_types);
}

static bool mlxsw_sp1_ptp_hwtstamp_enabled(struct mlxsw_sp_port *mlxsw_sp_port)
{
	return mlxsw_sp_port->ptp.ing_types || mlxsw_sp_port->ptp.egr_types;
}

static int
mlxsw_sp1_ptp_port_shaper_set(struct mlxsw_sp_port *mlxsw_sp_port, bool enable)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qeec_pl[MLXSW_REG_QEEC_LEN];

	mlxsw_reg_qeec_ptps_pack(qeec_pl, mlxsw_sp_port->local_port, enable);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qeec), qeec_pl);
}

static int mlxsw_sp1_ptp_port_shaper_check(struct mlxsw_sp_port *mlxsw_sp_port)
{
	bool ptps = false;
	int err, i;
	u32 speed;

	if (!mlxsw_sp1_ptp_hwtstamp_enabled(mlxsw_sp_port))
		return mlxsw_sp1_ptp_port_shaper_set(mlxsw_sp_port, false);

	err = mlxsw_sp_port_speed_get(mlxsw_sp_port, &speed);
	if (err)
		return err;

	for (i = 0; i < MLXSW_SP1_PTP_SHAPER_PARAMS_LEN; i++) {
		if (mlxsw_sp1_ptp_shaper_params[i].ethtool_speed == speed) {
			ptps = true;
			break;
		}
	}

	return mlxsw_sp1_ptp_port_shaper_set(mlxsw_sp_port, ptps);
}

void mlxsw_sp1_ptp_shaper_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mlxsw_sp_port *mlxsw_sp_port;
	int err;

	mlxsw_sp_port = container_of(dwork, struct mlxsw_sp_port,
				     ptp.shaper_dw);

	if (!mlxsw_sp1_ptp_hwtstamp_enabled(mlxsw_sp_port))
		return;

	err = mlxsw_sp1_ptp_port_shaper_check(mlxsw_sp_port);
	if (err)
		netdev_err(mlxsw_sp_port->dev, "Failed to set up PTP shaper\n");
}

int mlxsw_sp1_ptp_hwtstamp_set(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct hwtstamp_config *config)
{
	enum hwtstamp_rx_filters rx_filter;
	u16 ing_types;
	u16 egr_types;
	int err;

	err = mlxsw_sp1_ptp_get_message_types(config, &ing_types, &egr_types,
					      &rx_filter);
	if (err)
		return err;

	err = mlxsw_sp1_ptp_mtpppc_update(mlxsw_sp_port, ing_types, egr_types);
	if (err)
		return err;

	mlxsw_sp_port->ptp.hwtstamp_config = *config;
	mlxsw_sp_port->ptp.ing_types = ing_types;
	mlxsw_sp_port->ptp.egr_types = egr_types;

	err = mlxsw_sp1_ptp_port_shaper_check(mlxsw_sp_port);
	if (err)
		return err;

	/* Notify the ioctl caller what we are actually timestamping. */
	config->rx_filter = rx_filter;

	return 0;
}

int mlxsw_sp1_ptp_get_ts_info(struct mlxsw_sp *mlxsw_sp,
			      struct kernel_ethtool_ts_info *info)
{
	info->phc_index = ptp_clock_index(mlxsw_sp->clock->ptp);

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	info->tx_types = BIT(HWTSTAMP_TX_OFF) |
			 BIT(HWTSTAMP_TX_ON);

	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

struct mlxsw_sp_ptp_port_stat {
	char str[ETH_GSTRING_LEN];
	ptrdiff_t offset;
};

#define MLXSW_SP_PTP_PORT_STAT(NAME, FIELD)				\
	{								\
		.str = NAME,						\
		.offset = offsetof(struct mlxsw_sp_ptp_port_stats,	\
				    FIELD),				\
	}

static const struct mlxsw_sp_ptp_port_stat mlxsw_sp_ptp_port_stats[] = {
	MLXSW_SP_PTP_PORT_STAT("ptp_rx_gcd_packets",    rx_gcd.packets),
	MLXSW_SP_PTP_PORT_STAT("ptp_rx_gcd_timestamps", rx_gcd.timestamps),
	MLXSW_SP_PTP_PORT_STAT("ptp_tx_gcd_packets",    tx_gcd.packets),
	MLXSW_SP_PTP_PORT_STAT("ptp_tx_gcd_timestamps", tx_gcd.timestamps),
};

#undef MLXSW_SP_PTP_PORT_STAT

#define MLXSW_SP_PTP_PORT_STATS_LEN \
	ARRAY_SIZE(mlxsw_sp_ptp_port_stats)

int mlxsw_sp1_get_stats_count(void)
{
	return MLXSW_SP_PTP_PORT_STATS_LEN;
}

void mlxsw_sp1_get_stats_strings(u8 **p)
{
	int i;

	for (i = 0; i < MLXSW_SP_PTP_PORT_STATS_LEN; i++) {
		memcpy(*p, mlxsw_sp_ptp_port_stats[i].str,
		       ETH_GSTRING_LEN);
		*p += ETH_GSTRING_LEN;
	}
}

void mlxsw_sp1_get_stats(struct mlxsw_sp_port *mlxsw_sp_port,
			 u64 *data, int data_index)
{
	void *stats = &mlxsw_sp_port->ptp.stats;
	ptrdiff_t offset;
	int i;

	data += data_index;
	for (i = 0; i < MLXSW_SP_PTP_PORT_STATS_LEN; i++) {
		offset = mlxsw_sp_ptp_port_stats[i].offset;
		*data++ = *(u64 *)(stats + offset);
	}
}

struct mlxsw_sp_ptp_state *mlxsw_sp2_ptp_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp2_ptp_state *ptp_state;
	int err;

	/* Max FID will be used in data path, check validity as part of init. */
	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, FID))
		return ERR_PTR(-EIO);

	ptp_state = kzalloc(sizeof(*ptp_state), GFP_KERNEL);
	if (!ptp_state)
		return ERR_PTR(-ENOMEM);

	ptp_state->common.mlxsw_sp = mlxsw_sp;

	err = mlxsw_sp_ptp_traps_set(mlxsw_sp);
	if (err)
		goto err_ptp_traps_set;

	refcount_set(&ptp_state->ptp_port_enabled_ref, 0);
	mutex_init(&ptp_state->lock);
	return &ptp_state->common;

err_ptp_traps_set:
	kfree(ptp_state);
	return ERR_PTR(err);
}

void mlxsw_sp2_ptp_fini(struct mlxsw_sp_ptp_state *ptp_state_common)
{
	struct mlxsw_sp *mlxsw_sp = ptp_state_common->mlxsw_sp;
	struct mlxsw_sp2_ptp_state *ptp_state;

	ptp_state = mlxsw_sp2_ptp_state(mlxsw_sp);

	mutex_destroy(&ptp_state->lock);
	mlxsw_sp_ptp_traps_unset(mlxsw_sp);
	kfree(ptp_state);
}

static u32 mlxsw_ptp_utc_time_stamp_sec_get(struct mlxsw_core *mlxsw_core,
					    u8 cqe_ts_sec)
{
	u32 utc_sec = mlxsw_core_read_utc_sec(mlxsw_core);

	if (cqe_ts_sec > (utc_sec & 0xff))
		/* Time stamp above the last bits of UTC (UTC & 0xff) means the
		 * latter has wrapped after the time stamp was collected.
		 */
		utc_sec -= 256;

	utc_sec &= ~0xff;
	utc_sec |= cqe_ts_sec;

	return utc_sec;
}

static void mlxsw_sp2_ptp_hwtstamp_fill(struct mlxsw_core *mlxsw_core,
					const struct mlxsw_skb_cb *cb,
					struct skb_shared_hwtstamps *hwtstamps)
{
	u64 ts_sec, ts_nsec, nsec;

	WARN_ON_ONCE(!cb->cqe_ts.sec && !cb->cqe_ts.nsec);

	/* The time stamp in the CQE is represented by 38 bits, which is a short
	 * representation of UTC time. Software should create the full time
	 * stamp using the global UTC clock. The seconds have only 8 bits in the
	 * CQE, to create the full time stamp, use the current UTC time and fix
	 * the seconds according to the relation between UTC seconds and CQE
	 * seconds.
	 */
	ts_sec = mlxsw_ptp_utc_time_stamp_sec_get(mlxsw_core, cb->cqe_ts.sec);
	ts_nsec = cb->cqe_ts.nsec;

	nsec = ts_sec * NSEC_PER_SEC + ts_nsec;

	hwtstamps->hwtstamp = ns_to_ktime(nsec);
}

void mlxsw_sp2_ptp_receive(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
			   u16 local_port)
{
	struct skb_shared_hwtstamps hwtstamps;

	mlxsw_sp2_ptp_hwtstamp_fill(mlxsw_sp->core, mlxsw_skb_cb(skb),
				    &hwtstamps);
	*skb_hwtstamps(skb) = hwtstamps;
	mlxsw_sp_rx_listener_no_mark_func(skb, local_port, mlxsw_sp);
}

void mlxsw_sp2_ptp_transmitted(struct mlxsw_sp *mlxsw_sp,
			       struct sk_buff *skb, u16 local_port)
{
	struct skb_shared_hwtstamps hwtstamps;

	mlxsw_sp2_ptp_hwtstamp_fill(mlxsw_sp->core, mlxsw_skb_cb(skb),
				    &hwtstamps);
	skb_tstamp_tx(skb, &hwtstamps);
	dev_kfree_skb_any(skb);
}

int mlxsw_sp2_ptp_hwtstamp_get(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct hwtstamp_config *config)
{
	struct mlxsw_sp2_ptp_state *ptp_state;

	ptp_state = mlxsw_sp2_ptp_state(mlxsw_sp_port->mlxsw_sp);

	mutex_lock(&ptp_state->lock);
	*config = ptp_state->config;
	mutex_unlock(&ptp_state->lock);

	return 0;
}

static int
mlxsw_sp2_ptp_get_message_types(const struct hwtstamp_config *config,
				u16 *p_ing_types, u16 *p_egr_types,
				enum hwtstamp_rx_filters *p_rx_filter)
{
	enum hwtstamp_rx_filters rx_filter = config->rx_filter;
	enum hwtstamp_tx_types tx_type = config->tx_type;
	u16 ing_types = 0x00;
	u16 egr_types = 0x00;

	*p_rx_filter = rx_filter;

	switch (rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		ing_types = 0x00;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		/* In Spectrum-2 and above, all packets get time stamp by
		 * default and the driver fill the time stamp only for event
		 * packets. Return all event types even if only specific types
		 * were required.
		 */
		ing_types = 0x0f;
		*p_rx_filter = HWTSTAMP_FILTER_SOME;
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_NTP_ALL:
		return -ERANGE;
	default:
		return -EINVAL;
	}

	switch (tx_type) {
	case HWTSTAMP_TX_OFF:
		egr_types = 0x00;
		break;
	case HWTSTAMP_TX_ON:
		egr_types = 0x0f;
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
	case HWTSTAMP_TX_ONESTEP_P2P:
		return -ERANGE;
	default:
		return -EINVAL;
	}

	if ((ing_types && !egr_types) || (!ing_types && egr_types))
		return -EINVAL;

	*p_ing_types = ing_types;
	*p_egr_types = egr_types;
	return 0;
}

static int mlxsw_sp2_ptp_mtpcpc_set(struct mlxsw_sp *mlxsw_sp, bool ptp_trap_en,
				    u16 ing_types, u16 egr_types)
{
	char mtpcpc_pl[MLXSW_REG_MTPCPC_LEN];

	mlxsw_reg_mtpcpc_pack(mtpcpc_pl, false, 0, ptp_trap_en, ing_types,
			      egr_types);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mtpcpc), mtpcpc_pl);
}

static int mlxsw_sp2_ptp_enable(struct mlxsw_sp *mlxsw_sp, u16 ing_types,
				u16 egr_types,
				struct hwtstamp_config new_config)
{
	struct mlxsw_sp2_ptp_state *ptp_state = mlxsw_sp2_ptp_state(mlxsw_sp);
	int err;

	err = mlxsw_sp2_ptp_mtpcpc_set(mlxsw_sp, true, ing_types, egr_types);
	if (err)
		return err;

	ptp_state->config = new_config;
	return 0;
}

static int mlxsw_sp2_ptp_disable(struct mlxsw_sp *mlxsw_sp,
				 struct hwtstamp_config new_config)
{
	struct mlxsw_sp2_ptp_state *ptp_state = mlxsw_sp2_ptp_state(mlxsw_sp);
	int err;

	err = mlxsw_sp2_ptp_mtpcpc_set(mlxsw_sp, false, 0, 0);
	if (err)
		return err;

	ptp_state->config = new_config;
	return 0;
}

static int mlxsw_sp2_ptp_configure_port(struct mlxsw_sp_port *mlxsw_sp_port,
					u16 ing_types, u16 egr_types,
					struct hwtstamp_config new_config)
{
	struct mlxsw_sp2_ptp_state *ptp_state;
	int err;

	ptp_state = mlxsw_sp2_ptp_state(mlxsw_sp_port->mlxsw_sp);

	if (refcount_inc_not_zero(&ptp_state->ptp_port_enabled_ref))
		return 0;

	err = mlxsw_sp2_ptp_enable(mlxsw_sp_port->mlxsw_sp, ing_types,
				   egr_types, new_config);
	if (err)
		return err;

	refcount_set(&ptp_state->ptp_port_enabled_ref, 1);

	return 0;
}

static int mlxsw_sp2_ptp_deconfigure_port(struct mlxsw_sp_port *mlxsw_sp_port,
					  struct hwtstamp_config new_config)
{
	struct mlxsw_sp2_ptp_state *ptp_state;
	int err;

	ptp_state = mlxsw_sp2_ptp_state(mlxsw_sp_port->mlxsw_sp);

	if (!refcount_dec_and_test(&ptp_state->ptp_port_enabled_ref))
		return 0;

	err = mlxsw_sp2_ptp_disable(mlxsw_sp_port->mlxsw_sp, new_config);
	if (err)
		goto err_ptp_disable;

	return 0;

err_ptp_disable:
	refcount_set(&ptp_state->ptp_port_enabled_ref, 1);
	return err;
}

int mlxsw_sp2_ptp_hwtstamp_set(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct hwtstamp_config *config)
{
	struct mlxsw_sp2_ptp_state *ptp_state;
	enum hwtstamp_rx_filters rx_filter;
	struct hwtstamp_config new_config;
	u16 new_ing_types, new_egr_types;
	bool ptp_enabled;
	int err;

	ptp_state = mlxsw_sp2_ptp_state(mlxsw_sp_port->mlxsw_sp);
	mutex_lock(&ptp_state->lock);

	err = mlxsw_sp2_ptp_get_message_types(config, &new_ing_types,
					      &new_egr_types, &rx_filter);
	if (err)
		goto err_get_message_types;

	new_config.flags = config->flags;
	new_config.tx_type = config->tx_type;
	new_config.rx_filter = rx_filter;

	ptp_enabled = mlxsw_sp_port->ptp.ing_types ||
		      mlxsw_sp_port->ptp.egr_types;

	if ((new_ing_types || new_egr_types) && !ptp_enabled) {
		err = mlxsw_sp2_ptp_configure_port(mlxsw_sp_port, new_ing_types,
						   new_egr_types, new_config);
		if (err)
			goto err_configure_port;
	} else if (!new_ing_types && !new_egr_types && ptp_enabled) {
		err = mlxsw_sp2_ptp_deconfigure_port(mlxsw_sp_port, new_config);
		if (err)
			goto err_deconfigure_port;
	}

	mlxsw_sp_port->ptp.ing_types = new_ing_types;
	mlxsw_sp_port->ptp.egr_types = new_egr_types;

	/* Notify the ioctl caller what we are actually timestamping. */
	config->rx_filter = rx_filter;
	mutex_unlock(&ptp_state->lock);

	return 0;

err_deconfigure_port:
err_configure_port:
err_get_message_types:
	mutex_unlock(&ptp_state->lock);
	return err;
}

int mlxsw_sp2_ptp_get_ts_info(struct mlxsw_sp *mlxsw_sp,
			      struct kernel_ethtool_ts_info *info)
{
	info->phc_index = ptp_clock_index(mlxsw_sp->clock->ptp);

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	info->tx_types = BIT(HWTSTAMP_TX_OFF) |
			 BIT(HWTSTAMP_TX_ON);

	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);

	return 0;
}

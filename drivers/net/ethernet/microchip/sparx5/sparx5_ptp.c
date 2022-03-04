// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 */
#include <linux/ptp_classify.h>

#include "sparx5_main_regs.h"
#include "sparx5_main.h"

#define SPARX5_MAX_PTP_ID	512

#define TOD_ACC_PIN		0x4

enum {
	PTP_PIN_ACTION_IDLE = 0,
	PTP_PIN_ACTION_LOAD,
	PTP_PIN_ACTION_SAVE,
	PTP_PIN_ACTION_CLOCK,
	PTP_PIN_ACTION_DELTA,
	PTP_PIN_ACTION_TOD
};

static u64 sparx5_ptp_get_1ppm(struct sparx5 *sparx5)
{
	/* Represents 1ppm adjustment in 2^59 format with 1.59687500000(625)
	 * 1.99609375000(500), 3.99218750000(250) as reference
	 * The value is calculated as following:
	 * (1/1000000)/((2^-59)/X)
	 */

	u64 res;

	switch (sparx5->coreclock) {
	case SPX5_CORE_CLOCK_250MHZ:
		res = 2301339409586;
		break;
	case SPX5_CORE_CLOCK_500MHZ:
		res = 1150669704793;
		break;
	case SPX5_CORE_CLOCK_625MHZ:
		res =  920535763834;
		break;
	default:
		WARN_ON("Invalid core clock");
		break;
	}

	return res;
}

static u64 sparx5_ptp_get_nominal_value(struct sparx5 *sparx5)
{
	u64 res;

	switch (sparx5->coreclock) {
	case SPX5_CORE_CLOCK_250MHZ:
		res = 0x1FF0000000000000;
		break;
	case SPX5_CORE_CLOCK_500MHZ:
		res = 0x0FF8000000000000;
		break;
	case SPX5_CORE_CLOCK_625MHZ:
		res = 0x0CC6666666666666;
		break;
	default:
		WARN_ON("Invalid core clock");
		break;
	}

	return res;
}

static int sparx5_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct sparx5_phc *phc = container_of(ptp, struct sparx5_phc, info);
	struct sparx5 *sparx5 = phc->sparx5;
	unsigned long flags;
	bool neg_adj = 0;
	u64 tod_inc;
	u64 ref;

	if (!scaled_ppm)
		return 0;

	if (scaled_ppm < 0) {
		neg_adj = 1;
		scaled_ppm = -scaled_ppm;
	}

	tod_inc = sparx5_ptp_get_nominal_value(sparx5);

	/* The multiplication is split in 2 separate additions because of
	 * overflow issues. If scaled_ppm with 16bit fractional part was bigger
	 * than 20ppm then we got overflow.
	 */
	ref = sparx5_ptp_get_1ppm(sparx5) * (scaled_ppm >> 16);
	ref += (sparx5_ptp_get_1ppm(sparx5) * (0xffff & scaled_ppm)) >> 16;
	tod_inc = neg_adj ? tod_inc - ref : tod_inc + ref;

	spin_lock_irqsave(&sparx5->ptp_clock_lock, flags);

	spx5_rmw(PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS_SET(1 << BIT(phc->index)),
		 PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS,
		 sparx5, PTP_PTP_DOM_CFG);

	spx5_wr((u32)tod_inc & 0xFFFFFFFF, sparx5,
		PTP_CLK_PER_CFG(phc->index, 0));
	spx5_wr((u32)(tod_inc >> 32), sparx5,
		PTP_CLK_PER_CFG(phc->index, 1));

	spx5_rmw(PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS_SET(0),
		 PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS, sparx5,
		 PTP_PTP_DOM_CFG);

	spin_unlock_irqrestore(&sparx5->ptp_clock_lock, flags);

	return 0;
}

static int sparx5_ptp_settime64(struct ptp_clock_info *ptp,
				const struct timespec64 *ts)
{
	struct sparx5_phc *phc = container_of(ptp, struct sparx5_phc, info);
	struct sparx5 *sparx5 = phc->sparx5;
	unsigned long flags;

	spin_lock_irqsave(&sparx5->ptp_clock_lock, flags);

	/* Must be in IDLE mode before the time can be loaded */
	spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_IDLE) |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(phc->index) |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
		 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
		 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

	/* Set new value */
	spx5_wr(PTP_PTP_TOD_SEC_MSB_PTP_TOD_SEC_MSB_SET(upper_32_bits(ts->tv_sec)),
		sparx5, PTP_PTP_TOD_SEC_MSB(TOD_ACC_PIN));
	spx5_wr(lower_32_bits(ts->tv_sec),
		sparx5, PTP_PTP_TOD_SEC_LSB(TOD_ACC_PIN));
	spx5_wr(ts->tv_nsec, sparx5, PTP_PTP_TOD_NSEC(TOD_ACC_PIN));

	/* Apply new values */
	spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_LOAD) |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(phc->index) |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
		 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
		 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

	spin_unlock_irqrestore(&sparx5->ptp_clock_lock, flags);

	return 0;
}

static int sparx5_ptp_gettime64(struct ptp_clock_info *ptp,
				struct timespec64 *ts)
{
	struct sparx5_phc *phc = container_of(ptp, struct sparx5_phc, info);
	struct sparx5 *sparx5 = phc->sparx5;
	unsigned long flags;
	time64_t s;
	s64 ns;

	spin_lock_irqsave(&sparx5->ptp_clock_lock, flags);

	spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_SAVE) |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(phc->index) |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
		 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
		 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
		 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
		 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

	s = spx5_rd(sparx5, PTP_PTP_TOD_SEC_MSB(TOD_ACC_PIN));
	s <<= 32;
	s |= spx5_rd(sparx5, PTP_PTP_TOD_SEC_LSB(TOD_ACC_PIN));
	ns = spx5_rd(sparx5, PTP_PTP_TOD_NSEC(TOD_ACC_PIN));
	ns &= PTP_PTP_TOD_NSEC_PTP_TOD_NSEC;

	spin_unlock_irqrestore(&sparx5->ptp_clock_lock, flags);

	/* Deal with negative values */
	if ((ns & 0xFFFFFFF0) == 0x3FFFFFF0) {
		s--;
		ns &= 0xf;
		ns += 999999984;
	}

	set_normalized_timespec64(ts, s, ns);
	return 0;
}

static int sparx5_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct sparx5_phc *phc = container_of(ptp, struct sparx5_phc, info);
	struct sparx5 *sparx5 = phc->sparx5;

	if (delta > -(NSEC_PER_SEC / 2) && delta < (NSEC_PER_SEC / 2)) {
		unsigned long flags;

		spin_lock_irqsave(&sparx5->ptp_clock_lock, flags);

		/* Must be in IDLE mode before the time can be loaded */
		spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_IDLE) |
			 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(phc->index) |
			 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
			 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
			 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
			 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
			 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

		spx5_wr(PTP_PTP_TOD_NSEC_PTP_TOD_NSEC_SET(delta),
			sparx5, PTP_PTP_TOD_NSEC(TOD_ACC_PIN));

		/* Adjust time with the value of PTP_TOD_NSEC */
		spx5_rmw(PTP_PTP_PIN_CFG_PTP_PIN_ACTION_SET(PTP_PIN_ACTION_DELTA) |
			 PTP_PTP_PIN_CFG_PTP_PIN_DOM_SET(phc->index) |
			 PTP_PTP_PIN_CFG_PTP_PIN_SYNC_SET(0),
			 PTP_PTP_PIN_CFG_PTP_PIN_ACTION |
			 PTP_PTP_PIN_CFG_PTP_PIN_DOM |
			 PTP_PTP_PIN_CFG_PTP_PIN_SYNC,
			 sparx5, PTP_PTP_PIN_CFG(TOD_ACC_PIN));

		spin_unlock_irqrestore(&sparx5->ptp_clock_lock, flags);
	} else {
		/* Fall back using sparx5_ptp_settime64 which is not exact */
		struct timespec64 ts;
		u64 now;

		sparx5_ptp_gettime64(ptp, &ts);

		now = ktime_to_ns(timespec64_to_ktime(ts));
		ts = ns_to_timespec64(now + delta);

		sparx5_ptp_settime64(ptp, &ts);
	}

	return 0;
}

static struct ptp_clock_info sparx5_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "sparx5 ptp",
	.max_adj	= 200000,
	.gettime64	= sparx5_ptp_gettime64,
	.settime64	= sparx5_ptp_settime64,
	.adjtime	= sparx5_ptp_adjtime,
	.adjfine	= sparx5_ptp_adjfine,
};

static int sparx5_ptp_phc_init(struct sparx5 *sparx5,
			       int index,
			       struct ptp_clock_info *clock_info)
{
	struct sparx5_phc *phc = &sparx5->phc[index];

	phc->info = *clock_info;
	phc->clock = ptp_clock_register(&phc->info, sparx5->dev);
	if (IS_ERR(phc->clock))
		return PTR_ERR(phc->clock);

	phc->index = index;
	phc->sparx5 = sparx5;

	/* PTP Rx stamping is always enabled.  */
	phc->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;

	return 0;
}

int sparx5_ptp_init(struct sparx5 *sparx5)
{
	u64 tod_adj = sparx5_ptp_get_nominal_value(sparx5);
	int err, i;

	if (!sparx5->ptp)
		return 0;

	for (i = 0; i < SPARX5_PHC_COUNT; ++i) {
		err = sparx5_ptp_phc_init(sparx5, i, &sparx5_ptp_clock_info);
		if (err)
			return err;
	}

	spin_lock_init(&sparx5->ptp_clock_lock);

	/* Disable master counters */
	spx5_wr(PTP_PTP_DOM_CFG_PTP_ENA_SET(0), sparx5, PTP_PTP_DOM_CFG);

	/* Configure the nominal TOD increment per clock cycle */
	spx5_rmw(PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS_SET(0x7),
		 PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS,
		 sparx5, PTP_PTP_DOM_CFG);

	for (i = 0; i < SPARX5_PHC_COUNT; ++i) {
		spx5_wr((u32)tod_adj & 0xFFFFFFFF, sparx5,
			PTP_CLK_PER_CFG(i, 0));
		spx5_wr((u32)(tod_adj >> 32), sparx5,
			PTP_CLK_PER_CFG(i, 1));
	}

	spx5_rmw(PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS_SET(0),
		 PTP_PTP_DOM_CFG_PTP_CLKCFG_DIS,
		 sparx5, PTP_PTP_DOM_CFG);

	/* Enable master counters */
	spx5_wr(PTP_PTP_DOM_CFG_PTP_ENA_SET(0x7), sparx5, PTP_PTP_DOM_CFG);

	return 0;
}

void sparx5_ptp_deinit(struct sparx5 *sparx5)
{
	int i;

	for (i = 0; i < SPARX5_PHC_COUNT; ++i)
		ptp_clock_unregister(sparx5->phc[i].clock);
}

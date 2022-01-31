// SPDX-License-Identifier: GPL-2.0+

#include <linux/ptp_classify.h>

#include "lan966x_main.h"

#define LAN966X_MAX_PTP_ID	512

/* Represents 1ppm adjustment in 2^59 format with 6.037735849ns as reference
 * The value is calculated as following: (1/1000000)/((2^-59)/6.037735849)
 */
#define LAN966X_1PPM_FORMAT		3480517749723LL

/* Represents 1ppb adjustment in 2^29 format with 6.037735849ns as reference
 * The value is calculated as following: (1/1000000000)/((2^59)/6.037735849)
 */
#define LAN966X_1PPB_FORMAT		3480517749LL

#define TOD_ACC_PIN		0x5

enum {
	PTP_PIN_ACTION_IDLE = 0,
	PTP_PIN_ACTION_LOAD,
	PTP_PIN_ACTION_SAVE,
	PTP_PIN_ACTION_CLOCK,
	PTP_PIN_ACTION_DELTA,
	PTP_PIN_ACTION_TOD
};

static u64 lan966x_ptp_get_nominal_value(void)
{
	u64 res = 0x304d2df1;

	res <<= 32;
	return res;
}

static int lan966x_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;
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

	tod_inc = lan966x_ptp_get_nominal_value();

	/* The multiplication is split in 2 separate additions because of
	 * overflow issues. If scaled_ppm with 16bit fractional part was bigger
	 * than 20ppm then we got overflow.
	 */
	ref = LAN966X_1PPM_FORMAT * (scaled_ppm >> 16);
	ref += (LAN966X_1PPM_FORMAT * (0xffff & scaled_ppm)) >> 16;
	tod_inc = neg_adj ? tod_inc - ref : tod_inc + ref;

	spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);

	lan_rmw(PTP_DOM_CFG_CLKCFG_DIS_SET(1 << BIT(phc->index)),
		PTP_DOM_CFG_CLKCFG_DIS,
		lan966x, PTP_DOM_CFG);

	lan_wr((u32)tod_inc & 0xFFFFFFFF, lan966x,
	       PTP_CLK_PER_CFG(phc->index, 0));
	lan_wr((u32)(tod_inc >> 32), lan966x,
	       PTP_CLK_PER_CFG(phc->index, 1));

	lan_rmw(PTP_DOM_CFG_CLKCFG_DIS_SET(0),
		PTP_DOM_CFG_CLKCFG_DIS,
		lan966x, PTP_DOM_CFG);

	spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);

	return 0;
}

static int lan966x_ptp_settime64(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;
	unsigned long flags;

	spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);

	/* Must be in IDLE mode before the time can be loaded */
	lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_IDLE) |
		PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
		PTP_PIN_CFG_PIN_SYNC_SET(0),
		PTP_PIN_CFG_PIN_ACTION |
		PTP_PIN_CFG_PIN_DOM |
		PTP_PIN_CFG_PIN_SYNC,
		lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

	/* Set new value */
	lan_wr(PTP_TOD_SEC_MSB_TOD_SEC_MSB_SET(upper_32_bits(ts->tv_sec)),
	       lan966x, PTP_TOD_SEC_MSB(TOD_ACC_PIN));
	lan_wr(lower_32_bits(ts->tv_sec),
	       lan966x, PTP_TOD_SEC_LSB(TOD_ACC_PIN));
	lan_wr(ts->tv_nsec, lan966x, PTP_TOD_NSEC(TOD_ACC_PIN));

	/* Apply new values */
	lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_LOAD) |
		PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
		PTP_PIN_CFG_PIN_SYNC_SET(0),
		PTP_PIN_CFG_PIN_ACTION |
		PTP_PIN_CFG_PIN_DOM |
		PTP_PIN_CFG_PIN_SYNC,
		lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

	spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);

	return 0;
}

static int lan966x_ptp_gettime64(struct ptp_clock_info *ptp,
				 struct timespec64 *ts)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;
	unsigned long flags;
	time64_t s;
	s64 ns;

	spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);

	lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_SAVE) |
		PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
		PTP_PIN_CFG_PIN_SYNC_SET(0),
		PTP_PIN_CFG_PIN_ACTION |
		PTP_PIN_CFG_PIN_DOM |
		PTP_PIN_CFG_PIN_SYNC,
		lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

	s = lan_rd(lan966x, PTP_TOD_SEC_MSB(TOD_ACC_PIN));
	s <<= 32;
	s |= lan_rd(lan966x, PTP_TOD_SEC_LSB(TOD_ACC_PIN));
	ns = lan_rd(lan966x, PTP_TOD_NSEC(TOD_ACC_PIN));
	ns &= PTP_TOD_NSEC_TOD_NSEC;

	spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);

	/* Deal with negative values */
	if ((ns & 0xFFFFFFF0) == 0x3FFFFFF0) {
		s--;
		ns &= 0xf;
		ns += 999999984;
	}

	set_normalized_timespec64(ts, s, ns);
	return 0;
}

static int lan966x_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct lan966x_phc *phc = container_of(ptp, struct lan966x_phc, info);
	struct lan966x *lan966x = phc->lan966x;

	if (delta > -(NSEC_PER_SEC / 2) && delta < (NSEC_PER_SEC / 2)) {
		unsigned long flags;

		spin_lock_irqsave(&lan966x->ptp_clock_lock, flags);

		/* Must be in IDLE mode before the time can be loaded */
		lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_IDLE) |
			PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
			PTP_PIN_CFG_PIN_SYNC_SET(0),
			PTP_PIN_CFG_PIN_ACTION |
			PTP_PIN_CFG_PIN_DOM |
			PTP_PIN_CFG_PIN_SYNC,
			lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

		lan_wr(PTP_TOD_NSEC_TOD_NSEC_SET(delta),
		       lan966x, PTP_TOD_NSEC(TOD_ACC_PIN));

		/* Adjust time with the value of PTP_TOD_NSEC */
		lan_rmw(PTP_PIN_CFG_PIN_ACTION_SET(PTP_PIN_ACTION_DELTA) |
			PTP_PIN_CFG_PIN_DOM_SET(phc->index) |
			PTP_PIN_CFG_PIN_SYNC_SET(0),
			PTP_PIN_CFG_PIN_ACTION |
			PTP_PIN_CFG_PIN_DOM |
			PTP_PIN_CFG_PIN_SYNC,
			lan966x, PTP_PIN_CFG(TOD_ACC_PIN));

		spin_unlock_irqrestore(&lan966x->ptp_clock_lock, flags);
	} else {
		/* Fall back using lan966x_ptp_settime64 which is not exact */
		struct timespec64 ts;
		u64 now;

		lan966x_ptp_gettime64(ptp, &ts);

		now = ktime_to_ns(timespec64_to_ktime(ts));
		ts = ns_to_timespec64(now + delta);

		lan966x_ptp_settime64(ptp, &ts);
	}

	return 0;
}

static struct ptp_clock_info lan966x_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "lan966x ptp",
	.max_adj	= 200000,
	.gettime64	= lan966x_ptp_gettime64,
	.settime64	= lan966x_ptp_settime64,
	.adjtime	= lan966x_ptp_adjtime,
	.adjfine	= lan966x_ptp_adjfine,
};

static int lan966x_ptp_phc_init(struct lan966x *lan966x,
				int index,
				struct ptp_clock_info *clock_info)
{
	struct lan966x_phc *phc = &lan966x->phc[index];

	phc->info = *clock_info;
	phc->clock = ptp_clock_register(&phc->info, lan966x->dev);
	if (IS_ERR(phc->clock))
		return PTR_ERR(phc->clock);

	phc->index = index;
	phc->lan966x = lan966x;

	/* PTP Rx stamping is always enabled.  */
	phc->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;

	return 0;
}

int lan966x_ptp_init(struct lan966x *lan966x)
{
	u64 tod_adj = lan966x_ptp_get_nominal_value();
	int err, i;

	if (!lan966x->ptp)
		return 0;

	for (i = 0; i < LAN966X_PHC_COUNT; ++i) {
		err = lan966x_ptp_phc_init(lan966x, i, &lan966x_ptp_clock_info);
		if (err)
			return err;
	}

	spin_lock_init(&lan966x->ptp_clock_lock);

	/* Disable master counters */
	lan_wr(PTP_DOM_CFG_ENA_SET(0), lan966x, PTP_DOM_CFG);

	/* Configure the nominal TOD increment per clock cycle */
	lan_rmw(PTP_DOM_CFG_CLKCFG_DIS_SET(0x7),
		PTP_DOM_CFG_CLKCFG_DIS,
		lan966x, PTP_DOM_CFG);

	for (i = 0; i < LAN966X_PHC_COUNT; ++i) {
		lan_wr((u32)tod_adj & 0xFFFFFFFF, lan966x,
		       PTP_CLK_PER_CFG(i, 0));
		lan_wr((u32)(tod_adj >> 32), lan966x,
		       PTP_CLK_PER_CFG(i, 1));
	}

	lan_rmw(PTP_DOM_CFG_CLKCFG_DIS_SET(0),
		PTP_DOM_CFG_CLKCFG_DIS,
		lan966x, PTP_DOM_CFG);

	/* Enable master counters */
	lan_wr(PTP_DOM_CFG_ENA_SET(0x7), lan966x, PTP_DOM_CFG);

	return 0;
}

void lan966x_ptp_deinit(struct lan966x *lan966x)
{
	int i;

	for (i = 0; i < LAN966X_PHC_COUNT; ++i)
		ptp_clock_unregister(lan966x->phc[i].clock);
}

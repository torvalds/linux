// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot PTP clock driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 * Copyright 2020 NXP
 */
#include <soc/mscc/ocelot_ptp.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot.h>

int ocelot_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
	unsigned long flags;
	time64_t s;
	u32 val;
	s64 ns;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_SAVE);
	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

	s = ocelot_read_rix(ocelot, PTP_PIN_TOD_SEC_MSB, TOD_ACC_PIN) & 0xffff;
	s <<= 32;
	s += ocelot_read_rix(ocelot, PTP_PIN_TOD_SEC_LSB, TOD_ACC_PIN);
	ns = ocelot_read_rix(ocelot, PTP_PIN_TOD_NSEC, TOD_ACC_PIN);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);

	/* Deal with negative values */
	if (ns >= 0x3ffffff0 && ns <= 0x3fffffff) {
		s--;
		ns &= 0xf;
		ns += 999999984;
	}

	set_normalized_timespec64(ts, s, ns);
	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_gettime64);

int ocelot_ptp_settime64(struct ptp_clock_info *ptp,
			 const struct timespec64 *ts)
{
	struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_IDLE);

	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

	ocelot_write_rix(ocelot, lower_32_bits(ts->tv_sec), PTP_PIN_TOD_SEC_LSB,
			 TOD_ACC_PIN);
	ocelot_write_rix(ocelot, upper_32_bits(ts->tv_sec), PTP_PIN_TOD_SEC_MSB,
			 TOD_ACC_PIN);
	ocelot_write_rix(ocelot, ts->tv_nsec, PTP_PIN_TOD_NSEC, TOD_ACC_PIN);

	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_LOAD);

	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_settime64);

int ocelot_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	if (delta > -(NSEC_PER_SEC / 2) && delta < (NSEC_PER_SEC / 2)) {
		struct ocelot *ocelot = container_of(ptp, struct ocelot,
						     ptp_info);
		unsigned long flags;
		u32 val;

		spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

		val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
		val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK |
			 PTP_PIN_CFG_DOM);
		val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_IDLE);

		ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

		ocelot_write_rix(ocelot, 0, PTP_PIN_TOD_SEC_LSB, TOD_ACC_PIN);
		ocelot_write_rix(ocelot, 0, PTP_PIN_TOD_SEC_MSB, TOD_ACC_PIN);
		ocelot_write_rix(ocelot, delta, PTP_PIN_TOD_NSEC, TOD_ACC_PIN);

		val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
		val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK |
			 PTP_PIN_CFG_DOM);
		val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_DELTA);

		ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

		spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
	} else {
		/* Fall back using ocelot_ptp_settime64 which is not exact. */
		struct timespec64 ts;
		u64 now;

		ocelot_ptp_gettime64(ptp, &ts);

		now = ktime_to_ns(timespec64_to_ktime(ts));
		ts = ns_to_timespec64(now + delta);

		ocelot_ptp_settime64(ptp, &ts);
	}
	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_adjtime);

int ocelot_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
	u32 unit = 0, direction = 0;
	unsigned long flags;
	u64 adj = 0;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	if (!scaled_ppm)
		goto disable_adj;

	if (scaled_ppm < 0) {
		direction = PTP_CFG_CLK_ADJ_CFG_DIR;
		scaled_ppm = -scaled_ppm;
	}

	adj = PSEC_PER_SEC << 16;
	do_div(adj, scaled_ppm);
	do_div(adj, 1000);

	/* If the adjustment value is too large, use ns instead */
	if (adj >= (1L << 30)) {
		unit = PTP_CFG_CLK_ADJ_FREQ_NS;
		do_div(adj, 1000);
	}

	/* Still too big */
	if (adj >= (1L << 30))
		goto disable_adj;

	ocelot_write(ocelot, unit | adj, PTP_CLK_CFG_ADJ_FREQ);
	ocelot_write(ocelot, PTP_CFG_CLK_ADJ_CFG_ENA | direction,
		     PTP_CLK_CFG_ADJ_CFG);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
	return 0;

disable_adj:
	ocelot_write(ocelot, 0, PTP_CLK_CFG_ADJ_CFG);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_adjfine);

int ocelot_init_timestamp(struct ocelot *ocelot, struct ptp_clock_info *info)
{
	struct ptp_clock *ptp_clock;

	ocelot->ptp_info = *info;
	ptp_clock = ptp_clock_register(&ocelot->ptp_info, ocelot->dev);
	if (IS_ERR(ptp_clock))
		return PTR_ERR(ptp_clock);
	/* Check if PHC support is missing at the configuration level */
	if (!ptp_clock)
		return 0;

	ocelot->ptp_clock = ptp_clock;

	ocelot_write(ocelot, SYS_PTP_CFG_PTP_STAMP_WID(30), SYS_PTP_CFG);
	ocelot_write(ocelot, 0xffffffff, ANA_TABLES_PTP_ID_LOW);
	ocelot_write(ocelot, 0xffffffff, ANA_TABLES_PTP_ID_HIGH);

	ocelot_write(ocelot, PTP_CFG_MISC_PTP_EN, PTP_CFG_MISC);

	/* There is no device reconfiguration, PTP Rx stamping is always
	 * enabled.
	 */
	ocelot->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;

	return 0;
}
EXPORT_SYMBOL(ocelot_init_timestamp);

int ocelot_deinit_timestamp(struct ocelot *ocelot)
{
	if (ocelot->ptp_clock)
		ptp_clock_unregister(ocelot->ptp_clock);
	return 0;
}
EXPORT_SYMBOL(ocelot_deinit_timestamp);

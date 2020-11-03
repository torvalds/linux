// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * DSA driver for:
 * Hirschmann Hellcreek TSN switch.
 *
 * Copyright (C) 2019,2020 Hochschule Offenburg
 * Copyright (C) 2019,2020 Linutronix GmbH
 * Authors: Kamil Alkhouri <kamil.alkhouri@hs-offenburg.de>
 *	    Kurt Kanzenbach <kurt@linutronix.de>
 */

#include <linux/ptp_clock_kernel.h>
#include "hellcreek.h"
#include "hellcreek_ptp.h"

static u16 hellcreek_ptp_read(struct hellcreek *hellcreek, unsigned int offset)
{
	return readw(hellcreek->ptp_base + offset);
}

static void hellcreek_ptp_write(struct hellcreek *hellcreek, u16 data,
				unsigned int offset)
{
	writew(data, hellcreek->ptp_base + offset);
}

/* Get nanoseconds from PTP clock */
static u64 hellcreek_ptp_clock_read(struct hellcreek *hellcreek)
{
	u16 nsl, nsh;

	/* Take a snapshot */
	hellcreek_ptp_write(hellcreek, PR_COMMAND_C_SS, PR_COMMAND_C);

	/* The time of the day is saved as 96 bits. However, due to hardware
	 * limitations the seconds are not or only partly kept in the PTP
	 * core. Currently only three bits for the seconds are available. That's
	 * why only the nanoseconds are used and the seconds are tracked in
	 * software. Anyway due to internal locking all five registers should be
	 * read.
	 */
	nsh = hellcreek_ptp_read(hellcreek, PR_SS_SYNC_DATA_C);
	nsh = hellcreek_ptp_read(hellcreek, PR_SS_SYNC_DATA_C);
	nsh = hellcreek_ptp_read(hellcreek, PR_SS_SYNC_DATA_C);
	nsh = hellcreek_ptp_read(hellcreek, PR_SS_SYNC_DATA_C);
	nsl = hellcreek_ptp_read(hellcreek, PR_SS_SYNC_DATA_C);

	return (u64)nsl | ((u64)nsh << 16);
}

static u64 __hellcreek_ptp_gettime(struct hellcreek *hellcreek)
{
	u64 ns;

	ns = hellcreek_ptp_clock_read(hellcreek);
	if (ns < hellcreek->last_ts)
		hellcreek->seconds++;
	hellcreek->last_ts = ns;
	ns += hellcreek->seconds * NSEC_PER_SEC;

	return ns;
}

static int hellcreek_ptp_gettime(struct ptp_clock_info *ptp,
				 struct timespec64 *ts)
{
	struct hellcreek *hellcreek = ptp_to_hellcreek(ptp);
	u64 ns;

	mutex_lock(&hellcreek->ptp_lock);
	ns = __hellcreek_ptp_gettime(hellcreek);
	mutex_unlock(&hellcreek->ptp_lock);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int hellcreek_ptp_settime(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct hellcreek *hellcreek = ptp_to_hellcreek(ptp);
	u16 secl, nsh, nsl;

	secl = ts->tv_sec & 0xffff;
	nsh  = ((u32)ts->tv_nsec & 0xffff0000) >> 16;
	nsl  = ts->tv_nsec & 0xffff;

	mutex_lock(&hellcreek->ptp_lock);

	/* Update overflow data structure */
	hellcreek->seconds = ts->tv_sec;
	hellcreek->last_ts = ts->tv_nsec;

	/* Set time in clock */
	hellcreek_ptp_write(hellcreek, 0x00, PR_CLOCK_WRITE_C);
	hellcreek_ptp_write(hellcreek, 0x00, PR_CLOCK_WRITE_C);
	hellcreek_ptp_write(hellcreek, secl, PR_CLOCK_WRITE_C);
	hellcreek_ptp_write(hellcreek, nsh,  PR_CLOCK_WRITE_C);
	hellcreek_ptp_write(hellcreek, nsl,  PR_CLOCK_WRITE_C);

	mutex_unlock(&hellcreek->ptp_lock);

	return 0;
}

static int hellcreek_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct hellcreek *hellcreek = ptp_to_hellcreek(ptp);
	u16 negative = 0, addendh, addendl;
	u32 addend;
	u64 adj;

	if (scaled_ppm < 0) {
		negative = 1;
		scaled_ppm = -scaled_ppm;
	}

	/* IP-Core adjusts the nominal frequency by adding or subtracting 1 ns
	 * from the 8 ns (period of the oscillator) every time the accumulator
	 * register overflows. The value stored in the addend register is added
	 * to the accumulator register every 8 ns.
	 *
	 * addend value = (2^30 * accumulator_overflow_rate) /
	 *                oscillator_frequency
	 * where:
	 *
	 * oscillator_frequency = 125 MHz
	 * accumulator_overflow_rate = 125 MHz * scaled_ppm * 2^-16 * 10^-6 * 8
	 */
	adj = scaled_ppm;
	adj <<= 11;
	addend = (u32)div_u64(adj, 15625);

	addendh = (addend & 0xffff0000) >> 16;
	addendl = addend & 0xffff;

	negative = (negative << 15) & 0x8000;

	mutex_lock(&hellcreek->ptp_lock);

	/* Set drift register */
	hellcreek_ptp_write(hellcreek, negative, PR_CLOCK_DRIFT_C);
	hellcreek_ptp_write(hellcreek, 0x00, PR_CLOCK_DRIFT_C);
	hellcreek_ptp_write(hellcreek, 0x00, PR_CLOCK_DRIFT_C);
	hellcreek_ptp_write(hellcreek, addendh,  PR_CLOCK_DRIFT_C);
	hellcreek_ptp_write(hellcreek, addendl,  PR_CLOCK_DRIFT_C);

	mutex_unlock(&hellcreek->ptp_lock);

	return 0;
}

static int hellcreek_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct hellcreek *hellcreek = ptp_to_hellcreek(ptp);
	u16 negative = 0, counth, countl;
	u32 count_val;

	/* If the offset is larger than IP-Core slow offset resources. Don't
	 * consider slow adjustment. Rather, add the offset directly to the
	 * current time
	 */
	if (abs(delta) > MAX_SLOW_OFFSET_ADJ) {
		struct timespec64 now, then = ns_to_timespec64(delta);

		hellcreek_ptp_gettime(ptp, &now);
		now = timespec64_add(now, then);
		hellcreek_ptp_settime(ptp, &now);

		return 0;
	}

	if (delta < 0) {
		negative = 1;
		delta = -delta;
	}

	/* 'count_val' does not exceed the maximum register size (2^30) */
	count_val = div_s64(delta, MAX_NS_PER_STEP);

	counth = (count_val & 0xffff0000) >> 16;
	countl = count_val & 0xffff;

	negative = (negative << 15) & 0x8000;

	mutex_lock(&hellcreek->ptp_lock);

	/* Set offset write register */
	hellcreek_ptp_write(hellcreek, negative, PR_CLOCK_OFFSET_C);
	hellcreek_ptp_write(hellcreek, MAX_NS_PER_STEP, PR_CLOCK_OFFSET_C);
	hellcreek_ptp_write(hellcreek, MIN_CLK_CYCLES_BETWEEN_STEPS,
			    PR_CLOCK_OFFSET_C);
	hellcreek_ptp_write(hellcreek, countl,  PR_CLOCK_OFFSET_C);
	hellcreek_ptp_write(hellcreek, counth,  PR_CLOCK_OFFSET_C);

	mutex_unlock(&hellcreek->ptp_lock);

	return 0;
}

static int hellcreek_ptp_enable(struct ptp_clock_info *ptp,
				struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static void hellcreek_ptp_overflow_check(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct hellcreek *hellcreek;

	hellcreek = dw_overflow_to_hellcreek(dw);

	mutex_lock(&hellcreek->ptp_lock);
	__hellcreek_ptp_gettime(hellcreek);
	mutex_unlock(&hellcreek->ptp_lock);

	schedule_delayed_work(&hellcreek->overflow_work,
			      HELLCREEK_OVERFLOW_PERIOD);
}

int hellcreek_ptp_setup(struct hellcreek *hellcreek)
{
	u16 status;

	/* Set up the overflow work */
	INIT_DELAYED_WORK(&hellcreek->overflow_work,
			  hellcreek_ptp_overflow_check);

	/* Setup PTP clock */
	hellcreek->ptp_clock_info.owner = THIS_MODULE;
	snprintf(hellcreek->ptp_clock_info.name,
		 sizeof(hellcreek->ptp_clock_info.name),
		 dev_name(hellcreek->dev));

	/* IP-Core can add up to 0.5 ns per 8 ns cycle, which means
	 * accumulator_overflow_rate shall not exceed 62.5 MHz (which adjusts
	 * the nominal frequency by 6.25%)
	 */
	hellcreek->ptp_clock_info.max_adj   = 62500000;
	hellcreek->ptp_clock_info.n_alarm   = 0;
	hellcreek->ptp_clock_info.n_pins    = 0;
	hellcreek->ptp_clock_info.n_ext_ts  = 0;
	hellcreek->ptp_clock_info.n_per_out = 0;
	hellcreek->ptp_clock_info.pps	    = 0;
	hellcreek->ptp_clock_info.adjfine   = hellcreek_ptp_adjfine;
	hellcreek->ptp_clock_info.adjtime   = hellcreek_ptp_adjtime;
	hellcreek->ptp_clock_info.gettime64 = hellcreek_ptp_gettime;
	hellcreek->ptp_clock_info.settime64 = hellcreek_ptp_settime;
	hellcreek->ptp_clock_info.enable    = hellcreek_ptp_enable;

	hellcreek->ptp_clock = ptp_clock_register(&hellcreek->ptp_clock_info,
						  hellcreek->dev);
	if (IS_ERR(hellcreek->ptp_clock))
		return PTR_ERR(hellcreek->ptp_clock);

	/* Enable the offset correction process, if no offset correction is
	 * already taking place
	 */
	status = hellcreek_ptp_read(hellcreek, PR_CLOCK_STATUS_C);
	if (!(status & PR_CLOCK_STATUS_C_OFS_ACT))
		hellcreek_ptp_write(hellcreek,
				    status | PR_CLOCK_STATUS_C_ENA_OFS,
				    PR_CLOCK_STATUS_C);

	/* Enable the drift correction process */
	hellcreek_ptp_write(hellcreek, status | PR_CLOCK_STATUS_C_ENA_DRIFT,
			    PR_CLOCK_STATUS_C);

	schedule_delayed_work(&hellcreek->overflow_work,
			      HELLCREEK_OVERFLOW_PERIOD);

	return 0;
}

void hellcreek_ptp_free(struct hellcreek *hellcreek)
{
	cancel_delayed_work_sync(&hellcreek->overflow_work);
	if (hellcreek->ptp_clock)
		ptp_clock_unregister(hellcreek->ptp_clock);
	hellcreek->ptp_clock = NULL;
}

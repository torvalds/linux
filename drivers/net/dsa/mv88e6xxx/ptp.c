/*
 * Marvell 88E6xxx Switch PTP support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 National Instruments
 *      Erik Hons <erik.hons@ni.com>
 *      Brandon Streiff <brandon.streiff@ni.com>
 *      Dane Wagner <dane.wagner@ni.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "chip.h"
#include "global2.h"
#include "ptp.h"

/* Raw timestamps are in units of 8-ns clock periods. */
#define CC_SHIFT	28
#define CC_MULT		(8 << CC_SHIFT)
#define CC_MULT_NUM	(1 << 9)
#define CC_MULT_DEM	15625ULL

#define TAI_EVENT_WORK_INTERVAL msecs_to_jiffies(100)

#define cc_to_chip(cc) container_of(cc, struct mv88e6xxx_chip, tstamp_cc)
#define ptp_to_chip(ptp) container_of(ptp, struct mv88e6xxx_chip, \
				      ptp_clock_info)
#define dw_overflow_to_chip(dw) container_of(dw, struct mv88e6xxx_chip, \
					     overflow_work)

static int mv88e6xxx_tai_read(struct mv88e6xxx_chip *chip, int addr,
			      u16 *data, int len)
{
	if (!chip->info->ops->avb_ops->tai_read)
		return -EOPNOTSUPP;

	return chip->info->ops->avb_ops->tai_read(chip, addr, data, len);
}

static u64 mv88e6xxx_ptp_clock_read(const struct cyclecounter *cc)
{
	struct mv88e6xxx_chip *chip = cc_to_chip(cc);
	u16 phc_time[2];
	int err;

	err = mv88e6xxx_tai_read(chip, MV88E6XXX_TAI_TIME_LO, phc_time,
				 ARRAY_SIZE(phc_time));
	if (err)
		return 0;
	else
		return ((u32)phc_time[1] << 16) | phc_time[0];
}

static int mv88e6xxx_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct mv88e6xxx_chip *chip = ptp_to_chip(ptp);
	int neg_adj = 0;
	u32 diff, mult;
	u64 adj;

	if (scaled_ppm < 0) {
		neg_adj = 1;
		scaled_ppm = -scaled_ppm;
	}
	mult = CC_MULT;
	adj = CC_MULT_NUM;
	adj *= scaled_ppm;
	diff = div_u64(adj, CC_MULT_DEM);

	mutex_lock(&chip->reg_lock);

	timecounter_read(&chip->tstamp_tc);
	chip->tstamp_cc.mult = neg_adj ? mult - diff : mult + diff;

	mutex_unlock(&chip->reg_lock);

	return 0;
}

static int mv88e6xxx_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct mv88e6xxx_chip *chip = ptp_to_chip(ptp);

	mutex_lock(&chip->reg_lock);
	timecounter_adjtime(&chip->tstamp_tc, delta);
	mutex_unlock(&chip->reg_lock);

	return 0;
}

static int mv88e6xxx_ptp_gettime(struct ptp_clock_info *ptp,
				 struct timespec64 *ts)
{
	struct mv88e6xxx_chip *chip = ptp_to_chip(ptp);
	u64 ns;

	mutex_lock(&chip->reg_lock);
	ns = timecounter_read(&chip->tstamp_tc);
	mutex_unlock(&chip->reg_lock);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int mv88e6xxx_ptp_settime(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct mv88e6xxx_chip *chip = ptp_to_chip(ptp);
	u64 ns;

	ns = timespec64_to_ns(ts);

	mutex_lock(&chip->reg_lock);
	timecounter_init(&chip->tstamp_tc, &chip->tstamp_cc, ns);
	mutex_unlock(&chip->reg_lock);

	return 0;
}

static int mv88e6xxx_ptp_enable(struct ptp_clock_info *ptp,
				struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static int mv88e6xxx_ptp_verify(struct ptp_clock_info *ptp, unsigned int pin,
				enum ptp_pin_function func, unsigned int chan)
{
	return -EOPNOTSUPP;
}

/* With a 125MHz input clock, the 32-bit timestamp counter overflows in ~34.3
 * seconds; this task forces periodic reads so that we don't miss any.
 */
#define MV88E6XXX_TAI_OVERFLOW_PERIOD (HZ * 16)
static void mv88e6xxx_ptp_overflow_check(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct mv88e6xxx_chip *chip = dw_overflow_to_chip(dw);
	struct timespec64 ts;

	mv88e6xxx_ptp_gettime(&chip->ptp_clock_info, &ts);

	schedule_delayed_work(&chip->overflow_work,
			      MV88E6XXX_TAI_OVERFLOW_PERIOD);
}

int mv88e6xxx_ptp_setup(struct mv88e6xxx_chip *chip)
{
	/* Set up the cycle counter */
	memset(&chip->tstamp_cc, 0, sizeof(chip->tstamp_cc));
	chip->tstamp_cc.read	= mv88e6xxx_ptp_clock_read;
	chip->tstamp_cc.mask	= CYCLECOUNTER_MASK(32);
	chip->tstamp_cc.mult	= CC_MULT;
	chip->tstamp_cc.shift	= CC_SHIFT;

	timecounter_init(&chip->tstamp_tc, &chip->tstamp_cc,
			 ktime_to_ns(ktime_get_real()));

	INIT_DELAYED_WORK(&chip->overflow_work, mv88e6xxx_ptp_overflow_check);

	chip->ptp_clock_info.owner = THIS_MODULE;
	snprintf(chip->ptp_clock_info.name, sizeof(chip->ptp_clock_info.name),
		 dev_name(chip->dev));
	chip->ptp_clock_info.max_adj	= 1000000;

	chip->ptp_clock_info.adjfine	= mv88e6xxx_ptp_adjfine;
	chip->ptp_clock_info.adjtime	= mv88e6xxx_ptp_adjtime;
	chip->ptp_clock_info.gettime64	= mv88e6xxx_ptp_gettime;
	chip->ptp_clock_info.settime64	= mv88e6xxx_ptp_settime;
	chip->ptp_clock_info.enable	= mv88e6xxx_ptp_enable;
	chip->ptp_clock_info.verify	= mv88e6xxx_ptp_verify;

	chip->ptp_clock = ptp_clock_register(&chip->ptp_clock_info, chip->dev);
	if (IS_ERR(chip->ptp_clock))
		return PTR_ERR(chip->ptp_clock);

	schedule_delayed_work(&chip->overflow_work,
			      MV88E6XXX_TAI_OVERFLOW_PERIOD);

	return 0;
}

void mv88e6xxx_ptp_free(struct mv88e6xxx_chip *chip)
{
	if (chip->ptp_clock) {
		cancel_delayed_work_sync(&chip->overflow_work);

		ptp_clock_unregister(chip->ptp_clock);
		chip->ptp_clock = NULL;
	}
}

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Switch PTP support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 National Instruments
 *      Erik Hons <erik.hons@ni.com>
 *      Brandon Streiff <brandon.streiff@ni.com>
 *      Dane Wagner <dane.wagner@ni.com>
 */

#include "chip.h"
#include "global2.h"
#include "hwtstamp.h"
#include "ptp.h"

/* Raw timestamps are in units of 8-ns clock periods. */
#define CC_SHIFT	28
#define CC_MULT		(8 << CC_SHIFT)
#define CC_MULT_NUM	(1 << 9)
#define CC_MULT_DEM	15625ULL

#define TAI_EVENT_WORK_INTERVAL msecs_to_jiffies(100)

#define cc_to_chip(cc) container_of(cc, struct mv88e6xxx_chip, tstamp_cc)
#define dw_overflow_to_chip(dw) container_of(dw, struct mv88e6xxx_chip, \
					     overflow_work)
#define dw_tai_event_to_chip(dw) container_of(dw, struct mv88e6xxx_chip, \
					      tai_event_work)

static int mv88e6xxx_tai_read(struct mv88e6xxx_chip *chip, int addr,
			      u16 *data, int len)
{
	if (!chip->info->ops->avb_ops->tai_read)
		return -EOPNOTSUPP;

	return chip->info->ops->avb_ops->tai_read(chip, addr, data, len);
}

static int mv88e6xxx_tai_write(struct mv88e6xxx_chip *chip, int addr, u16 data)
{
	if (!chip->info->ops->avb_ops->tai_write)
		return -EOPNOTSUPP;

	return chip->info->ops->avb_ops->tai_write(chip, addr, data);
}

/* TODO: places where this are called should be using pinctrl */
static int mv88e6352_set_gpio_func(struct mv88e6xxx_chip *chip, int pin,
				   int func, int input)
{
	int err;

	if (!chip->info->ops->gpio_ops)
		return -EOPNOTSUPP;

	err = chip->info->ops->gpio_ops->set_dir(chip, pin, input);
	if (err)
		return err;

	return chip->info->ops->gpio_ops->set_pctl(chip, pin, func);
}

static u64 mv88e6352_ptp_clock_read(const struct cyclecounter *cc)
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

static u64 mv88e6165_ptp_clock_read(const struct cyclecounter *cc)
{
	struct mv88e6xxx_chip *chip = cc_to_chip(cc);
	u16 phc_time[2];
	int err;

	err = mv88e6xxx_tai_read(chip, MV88E6XXX_PTP_GC_TIME_LO, phc_time,
				 ARRAY_SIZE(phc_time));
	if (err)
		return 0;
	else
		return ((u32)phc_time[1] << 16) | phc_time[0];
}

/* mv88e6352_config_eventcap - configure TAI event capture
 * @event: PTP_CLOCK_PPS (internal) or PTP_CLOCK_EXTTS (external)
 * @rising: zero for falling-edge trigger, else rising-edge trigger
 *
 * This will also reset the capture sequence counter.
 */
static int mv88e6352_config_eventcap(struct mv88e6xxx_chip *chip, int event,
				     int rising)
{
	u16 global_config;
	u16 cap_config;
	int err;

	chip->evcap_config = MV88E6XXX_TAI_CFG_CAP_OVERWRITE |
			     MV88E6XXX_TAI_CFG_CAP_CTR_START;
	if (!rising)
		chip->evcap_config |= MV88E6XXX_TAI_CFG_EVREQ_FALLING;

	global_config = (chip->evcap_config | chip->trig_config);
	err = mv88e6xxx_tai_write(chip, MV88E6XXX_TAI_CFG, global_config);
	if (err)
		return err;

	if (event == PTP_CLOCK_PPS) {
		cap_config = MV88E6XXX_TAI_EVENT_STATUS_CAP_TRIG;
	} else if (event == PTP_CLOCK_EXTTS) {
		/* if STATUS_CAP_TRIG is unset we capture PTP_EVREQ events */
		cap_config = 0;
	} else {
		return -EINVAL;
	}

	/* Write the capture config; this also clears the capture counter */
	err = mv88e6xxx_tai_write(chip, MV88E6XXX_TAI_EVENT_STATUS,
				  cap_config);

	return err;
}

static void mv88e6352_tai_event_work(struct work_struct *ugly)
{
	struct delayed_work *dw = to_delayed_work(ugly);
	struct mv88e6xxx_chip *chip = dw_tai_event_to_chip(dw);
	struct ptp_clock_event ev;
	u16 status[4];
	u32 raw_ts;
	int err;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_tai_read(chip, MV88E6XXX_TAI_EVENT_STATUS,
				 status, ARRAY_SIZE(status));
	mutex_unlock(&chip->reg_lock);

	if (err) {
		dev_err(chip->dev, "failed to read TAI status register\n");
		return;
	}
	if (status[0] & MV88E6XXX_TAI_EVENT_STATUS_ERROR) {
		dev_warn(chip->dev, "missed event capture\n");
		return;
	}
	if (!(status[0] & MV88E6XXX_TAI_EVENT_STATUS_VALID))
		goto out;

	raw_ts = ((u32)status[2] << 16) | status[1];

	/* Clear the valid bit so the next timestamp can come in */
	status[0] &= ~MV88E6XXX_TAI_EVENT_STATUS_VALID;
	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_tai_write(chip, MV88E6XXX_TAI_EVENT_STATUS, status[0]);
	mutex_unlock(&chip->reg_lock);

	/* This is an external timestamp */
	ev.type = PTP_CLOCK_EXTTS;

	/* We only have one timestamping channel. */
	ev.index = 0;
	mutex_lock(&chip->reg_lock);
	ev.timestamp = timecounter_cyc2time(&chip->tstamp_tc, raw_ts);
	mutex_unlock(&chip->reg_lock);

	ptp_clock_event(chip->ptp_clock, &ev);
out:
	schedule_delayed_work(&chip->tai_event_work, TAI_EVENT_WORK_INTERVAL);
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

static int mv88e6352_ptp_enable_extts(struct mv88e6xxx_chip *chip,
				      struct ptp_clock_request *rq, int on)
{
	int rising = (rq->extts.flags & PTP_RISING_EDGE);
	int func;
	int pin;
	int err;

	pin = ptp_find_pin(chip->ptp_clock, PTP_PF_EXTTS, rq->extts.index);

	if (pin < 0)
		return -EBUSY;

	mutex_lock(&chip->reg_lock);

	if (on) {
		func = MV88E6352_G2_SCRATCH_GPIO_PCTL_EVREQ;

		err = mv88e6352_set_gpio_func(chip, pin, func, true);
		if (err)
			goto out;

		schedule_delayed_work(&chip->tai_event_work,
				      TAI_EVENT_WORK_INTERVAL);

		err = mv88e6352_config_eventcap(chip, PTP_CLOCK_EXTTS, rising);
	} else {
		func = MV88E6352_G2_SCRATCH_GPIO_PCTL_GPIO;

		err = mv88e6352_set_gpio_func(chip, pin, func, true);

		cancel_delayed_work_sync(&chip->tai_event_work);
	}

out:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6352_ptp_enable(struct ptp_clock_info *ptp,
				struct ptp_clock_request *rq, int on)
{
	struct mv88e6xxx_chip *chip = ptp_to_chip(ptp);

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		return mv88e6352_ptp_enable_extts(chip, rq, on);
	default:
		return -EOPNOTSUPP;
	}
}

static int mv88e6352_ptp_verify(struct ptp_clock_info *ptp, unsigned int pin,
				enum ptp_pin_function func, unsigned int chan)
{
	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_EXTTS:
		break;
	case PTP_PF_PEROUT:
	case PTP_PF_PHYSYNC:
		return -EOPNOTSUPP;
	}
	return 0;
}

const struct mv88e6xxx_ptp_ops mv88e6352_ptp_ops = {
	.clock_read = mv88e6352_ptp_clock_read,
	.ptp_enable = mv88e6352_ptp_enable,
	.ptp_verify = mv88e6352_ptp_verify,
	.event_work = mv88e6352_tai_event_work,
	.port_enable = mv88e6352_hwtstamp_port_enable,
	.port_disable = mv88e6352_hwtstamp_port_disable,
	.n_ext_ts = 1,
	.arr0_sts_reg = MV88E6XXX_PORT_PTP_ARR0_STS,
	.arr1_sts_reg = MV88E6XXX_PORT_PTP_ARR1_STS,
	.dep_sts_reg = MV88E6XXX_PORT_PTP_DEP_STS,
	.rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ) |
		(1 << HWTSTAMP_FILTER_PTP_V2_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_SYNC) |
		(1 << HWTSTAMP_FILTER_PTP_V2_DELAY_REQ),
};

const struct mv88e6xxx_ptp_ops mv88e6165_ptp_ops = {
	.clock_read = mv88e6165_ptp_clock_read,
	.global_enable = mv88e6165_global_enable,
	.global_disable = mv88e6165_global_disable,
	.arr0_sts_reg = MV88E6165_PORT_PTP_ARR0_STS,
	.arr1_sts_reg = MV88E6165_PORT_PTP_ARR1_STS,
	.dep_sts_reg = MV88E6165_PORT_PTP_DEP_STS,
	.rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ) |
		(1 << HWTSTAMP_FILTER_PTP_V2_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_SYNC) |
		(1 << HWTSTAMP_FILTER_PTP_V2_DELAY_REQ),
};

static u64 mv88e6xxx_ptp_clock_read(const struct cyclecounter *cc)
{
	struct mv88e6xxx_chip *chip = cc_to_chip(cc);

	if (chip->info->ops->ptp_ops->clock_read)
		return chip->info->ops->ptp_ops->clock_read(cc);

	return 0;
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
	const struct mv88e6xxx_ptp_ops *ptp_ops = chip->info->ops->ptp_ops;
	int i;

	/* Set up the cycle counter */
	memset(&chip->tstamp_cc, 0, sizeof(chip->tstamp_cc));
	chip->tstamp_cc.read	= mv88e6xxx_ptp_clock_read;
	chip->tstamp_cc.mask	= CYCLECOUNTER_MASK(32);
	chip->tstamp_cc.mult	= CC_MULT;
	chip->tstamp_cc.shift	= CC_SHIFT;

	timecounter_init(&chip->tstamp_tc, &chip->tstamp_cc,
			 ktime_to_ns(ktime_get_real()));

	INIT_DELAYED_WORK(&chip->overflow_work, mv88e6xxx_ptp_overflow_check);
	if (ptp_ops->event_work)
		INIT_DELAYED_WORK(&chip->tai_event_work, ptp_ops->event_work);

	chip->ptp_clock_info.owner = THIS_MODULE;
	snprintf(chip->ptp_clock_info.name, sizeof(chip->ptp_clock_info.name),
		 "%s", dev_name(chip->dev));
	chip->ptp_clock_info.max_adj	= 1000000;

	chip->ptp_clock_info.n_ext_ts	= ptp_ops->n_ext_ts;
	chip->ptp_clock_info.n_per_out	= 0;
	chip->ptp_clock_info.n_pins	= mv88e6xxx_num_gpio(chip);
	chip->ptp_clock_info.pps	= 0;

	for (i = 0; i < chip->ptp_clock_info.n_pins; ++i) {
		struct ptp_pin_desc *ppd = &chip->pin_config[i];

		snprintf(ppd->name, sizeof(ppd->name), "mv88e6xxx_gpio%d", i);
		ppd->index = i;
		ppd->func = PTP_PF_NONE;
	}
	chip->ptp_clock_info.pin_config = chip->pin_config;

	chip->ptp_clock_info.adjfine	= mv88e6xxx_ptp_adjfine;
	chip->ptp_clock_info.adjtime	= mv88e6xxx_ptp_adjtime;
	chip->ptp_clock_info.gettime64	= mv88e6xxx_ptp_gettime;
	chip->ptp_clock_info.settime64	= mv88e6xxx_ptp_settime;
	chip->ptp_clock_info.enable	= ptp_ops->ptp_enable;
	chip->ptp_clock_info.verify	= ptp_ops->ptp_verify;
	chip->ptp_clock_info.do_aux_work = mv88e6xxx_hwtstamp_work;

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
		if (chip->info->ops->ptp_ops->event_work)
			cancel_delayed_work_sync(&chip->tai_event_work);

		ptp_clock_unregister(chip->ptp_clock);
		chip->ptp_clock = NULL;
	}
}

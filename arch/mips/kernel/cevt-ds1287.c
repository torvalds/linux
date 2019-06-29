// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  DS1287 clockevent driver
 *
 *  Copyright (C) 2008	Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/irq.h>

#include <asm/time.h>

int ds1287_timer_state(void)
{
	return (CMOS_READ(RTC_REG_C) & RTC_PF) != 0;
}

int ds1287_set_base_clock(unsigned int hz)
{
	u8 rate;

	switch (hz) {
	case 128:
		rate = 0x9;
		break;
	case 256:
		rate = 0x8;
		break;
	case 1024:
		rate = 0x6;
		break;
	default:
		return -EINVAL;
	}

	CMOS_WRITE(RTC_REF_CLCK_32KHZ | rate, RTC_REG_A);

	return 0;
}

static int ds1287_set_next_event(unsigned long delta,
				 struct clock_event_device *evt)
{
	return -EINVAL;
}

static int ds1287_shutdown(struct clock_event_device *evt)
{
	u8 val;

	spin_lock(&rtc_lock);

	val = CMOS_READ(RTC_REG_B);
	val &= ~RTC_PIE;
	CMOS_WRITE(val, RTC_REG_B);

	spin_unlock(&rtc_lock);
	return 0;
}

static int ds1287_set_periodic(struct clock_event_device *evt)
{
	u8 val;

	spin_lock(&rtc_lock);

	val = CMOS_READ(RTC_REG_B);
	val |= RTC_PIE;
	CMOS_WRITE(val, RTC_REG_B);

	spin_unlock(&rtc_lock);
	return 0;
}

static void ds1287_event_handler(struct clock_event_device *dev)
{
}

static struct clock_event_device ds1287_clockevent = {
	.name			= "ds1287",
	.features		= CLOCK_EVT_FEAT_PERIODIC,
	.set_next_event		= ds1287_set_next_event,
	.set_state_shutdown	= ds1287_shutdown,
	.set_state_periodic	= ds1287_set_periodic,
	.tick_resume		= ds1287_shutdown,
	.event_handler		= ds1287_event_handler,
};

static irqreturn_t ds1287_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *cd = &ds1287_clockevent;

	/* Ack the RTC interrupt. */
	CMOS_READ(RTC_REG_C);

	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static struct irqaction ds1287_irqaction = {
	.handler	= ds1287_interrupt,
	.flags		= IRQF_PERCPU | IRQF_TIMER,
	.name		= "ds1287",
};

int __init ds1287_clockevent_init(int irq)
{
	struct clock_event_device *cd;

	cd = &ds1287_clockevent;
	cd->rating = 100;
	cd->irq = irq;
	clockevent_set_clock(cd, 32768);
	cd->max_delta_ns = clockevent_delta2ns(0x7fffffff, cd);
	cd->max_delta_ticks = 0x7fffffff;
	cd->min_delta_ns = clockevent_delta2ns(0x300, cd);
	cd->min_delta_ticks = 0x300;
	cd->cpumask = cpumask_of(0);

	clockevents_register_device(&ds1287_clockevent);

	return setup_irq(irq, &ds1287_irqaction);
}

/*
 *  DS1287 clockevent driver
 *
 *  Copyright (C) 2008  Yoichi Yuasa <yuasa@linux-mips.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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

static void ds1287_set_mode(enum clock_event_mode mode,
			    struct clock_event_device *evt)
{
	u8 val;

	spin_lock(&rtc_lock);

	val = CMOS_READ(RTC_REG_B);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		val |= RTC_PIE;
		break;
	default:
		val &= ~RTC_PIE;
		break;
	}

	CMOS_WRITE(val, RTC_REG_B);

	spin_unlock(&rtc_lock);
}

static void ds1287_event_handler(struct clock_event_device *dev)
{
}

static struct clock_event_device ds1287_clockevent = {
	.name		= "ds1287",
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.set_next_event	= ds1287_set_next_event,
	.set_mode	= ds1287_set_mode,
	.event_handler	= ds1287_event_handler,
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
	.flags		= IRQF_DISABLED | IRQF_PERCPU | IRQF_TIMER,
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
	cd->min_delta_ns = clockevent_delta2ns(0x300, cd);
	cd->cpumask = cpumask_of(0);

	clockevents_register_device(&ds1287_clockevent);

	return setup_irq(irq, &ds1287_irqaction);
}

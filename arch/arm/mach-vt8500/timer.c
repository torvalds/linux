/*
 *  arch/arm/mach-vt8500/timer.c
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/delay.h>

#include <asm/mach/time.h>

#include "devices.h"

#define VT8500_TIMER_OFFSET	0x0100
#define TIMER_MATCH_VAL		0x0000
#define TIMER_COUNT_VAL		0x0010
#define TIMER_STATUS_VAL	0x0014
#define TIMER_IER_VAL		0x001c		/* interrupt enable */
#define TIMER_CTRL_VAL		0x0020
#define TIMER_AS_VAL		0x0024		/* access status */
#define TIMER_COUNT_R_ACTIVE	(1 << 5)	/* not ready for read */
#define TIMER_COUNT_W_ACTIVE	(1 << 4)	/* not ready for write */
#define TIMER_MATCH_W_ACTIVE	(1 << 0)	/* not ready for write */
#define VT8500_TIMER_HZ		3000000

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)

static void __iomem *regbase;

static cycle_t vt8500_timer_read(struct clocksource *cs)
{
	int loops = msecs_to_loops(10);
	writel(3, regbase + TIMER_CTRL_VAL);
	while ((readl((regbase + TIMER_AS_VAL)) & TIMER_COUNT_R_ACTIVE)
						&& --loops)
		cpu_relax();
	return readl(regbase + TIMER_COUNT_VAL);
}

struct clocksource clocksource = {
	.name           = "vt8500_timer",
	.rating         = 200,
	.read           = vt8500_timer_read,
	.mask           = CLOCKSOURCE_MASK(32),
	.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int vt8500_timer_set_next_event(unsigned long cycles,
				    struct clock_event_device *evt)
{
	int loops = msecs_to_loops(10);
	cycle_t alarm = clocksource.read(&clocksource) + cycles;
	while ((readl(regbase + TIMER_AS_VAL) & TIMER_MATCH_W_ACTIVE)
						&& --loops)
		cpu_relax();
	writel((unsigned long)alarm, regbase + TIMER_MATCH_VAL);

	if ((signed)(alarm - clocksource.read(&clocksource)) <= 16)
		return -ETIME;

	writel(1, regbase + TIMER_IER_VAL);

	return 0;
}

static void vt8500_timer_set_mode(enum clock_event_mode mode,
			      struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
		break;
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		writel(readl(regbase + TIMER_CTRL_VAL) | 1,
			regbase + TIMER_CTRL_VAL);
		writel(0, regbase + TIMER_IER_VAL);
		break;
	}
}

struct clock_event_device clockevent = {
	.name           = "vt8500_timer",
	.features       = CLOCK_EVT_FEAT_ONESHOT,
	.rating         = 200,
	.set_next_event = vt8500_timer_set_next_event,
	.set_mode       = vt8500_timer_set_mode,
};

static irqreturn_t vt8500_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	writel(0xf, regbase + TIMER_STATUS_VAL);
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

struct irqaction irq = {
	.name    = "vt8500_timer",
	.flags   = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = vt8500_timer_interrupt,
	.dev_id  = &clockevent,
};

static void __init vt8500_timer_init(void)
{
	regbase = ioremap(wmt_pmc_base + VT8500_TIMER_OFFSET, 0x28);
	if (!regbase)
		printk(KERN_ERR "vt8500_timer_init: failed to map MMIO registers\n");

	writel(1, regbase + TIMER_CTRL_VAL);
	writel(0xf, regbase + TIMER_STATUS_VAL);
	writel(~0, regbase + TIMER_MATCH_VAL);

	if (clocksource_register_hz(&clocksource, VT8500_TIMER_HZ))
		printk(KERN_ERR "vt8500_timer_init: clocksource_register failed for %s\n",
					clocksource.name);

	clockevents_calc_mult_shift(&clockevent, VT8500_TIMER_HZ, 4);

	/* copy-pasted from mach-msm; no idea */
	clockevent.max_delta_ns =
		clockevent_delta2ns(0xf0000000, &clockevent);
	clockevent.min_delta_ns = clockevent_delta2ns(4, &clockevent);
	clockevent.cpumask = cpumask_of(0);

	if (setup_irq(wmt_timer_irq, &irq))
		printk(KERN_ERR "vt8500_timer_init: setup_irq failed for %s\n",
					clockevent.name);
	clockevents_register_device(&clockevent);
}

struct sys_timer vt8500_timer = {
	.init = vt8500_timer_init
};

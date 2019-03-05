/*
 *  arch/arm/mach-vt8500/timer.c
 *
 *  Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
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

/*
 * This file is copied and modified from the original timer.c provided by
 * Alexey Charkov. Minor changes have been made for Device Tree Support.
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/delay.h>

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define VT8500_TIMER_OFFSET	0x0100
#define VT8500_TIMER_HZ		3000000
#define TIMER_MATCH_VAL		0x0000
#define TIMER_COUNT_VAL		0x0010
#define TIMER_STATUS_VAL	0x0014
#define TIMER_IER_VAL		0x001c		/* interrupt enable */
#define TIMER_CTRL_VAL		0x0020
#define TIMER_AS_VAL		0x0024		/* access status */
#define TIMER_COUNT_R_ACTIVE	(1 << 5)	/* not ready for read */
#define TIMER_COUNT_W_ACTIVE	(1 << 4)	/* not ready for write */
#define TIMER_MATCH_W_ACTIVE	(1 << 0)	/* not ready for write */

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)

#define MIN_OSCR_DELTA		16

static void __iomem *regbase;

static u64 vt8500_timer_read(struct clocksource *cs)
{
	int loops = msecs_to_loops(10);
	writel(3, regbase + TIMER_CTRL_VAL);
	while ((readl((regbase + TIMER_AS_VAL)) & TIMER_COUNT_R_ACTIVE)
						&& --loops)
		cpu_relax();
	return readl(regbase + TIMER_COUNT_VAL);
}

static struct clocksource clocksource = {
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
	u64 alarm = clocksource.read(&clocksource) + cycles;
	while ((readl(regbase + TIMER_AS_VAL) & TIMER_MATCH_W_ACTIVE)
						&& --loops)
		cpu_relax();
	writel((unsigned long)alarm, regbase + TIMER_MATCH_VAL);

	if ((signed)(alarm - clocksource.read(&clocksource)) <= MIN_OSCR_DELTA)
		return -ETIME;

	writel(1, regbase + TIMER_IER_VAL);

	return 0;
}

static int vt8500_shutdown(struct clock_event_device *evt)
{
	writel(readl(regbase + TIMER_CTRL_VAL) | 1, regbase + TIMER_CTRL_VAL);
	writel(0, regbase + TIMER_IER_VAL);
	return 0;
}

static struct clock_event_device clockevent = {
	.name			= "vt8500_timer",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.rating			= 200,
	.set_next_event		= vt8500_timer_set_next_event,
	.set_state_shutdown	= vt8500_shutdown,
	.set_state_oneshot	= vt8500_shutdown,
};

static irqreturn_t vt8500_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	writel(0xf, regbase + TIMER_STATUS_VAL);
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction irq = {
	.name    = "vt8500_timer",
	.flags   = IRQF_TIMER | IRQF_IRQPOLL,
	.handler = vt8500_timer_interrupt,
	.dev_id  = &clockevent,
};

static int __init vt8500_timer_init(struct device_node *np)
{
	int timer_irq, ret;

	regbase = of_iomap(np, 0);
	if (!regbase) {
		pr_err("%s: Missing iobase description in Device Tree\n",
								__func__);
		return -ENXIO;
	}

	timer_irq = irq_of_parse_and_map(np, 0);
	if (!timer_irq) {
		pr_err("%s: Missing irq description in Device Tree\n",
								__func__);
		return -EINVAL;
	}

	writel(1, regbase + TIMER_CTRL_VAL);
	writel(0xf, regbase + TIMER_STATUS_VAL);
	writel(~0, regbase + TIMER_MATCH_VAL);

	ret = clocksource_register_hz(&clocksource, VT8500_TIMER_HZ);
	if (ret) {
		pr_err("%s: clocksource_register failed for %s\n",
		       __func__, clocksource.name);
		return ret;
	}

	clockevent.cpumask = cpumask_of(0);

	ret = setup_irq(timer_irq, &irq);
	if (ret) {
		pr_err("%s: setup_irq failed for %s\n", __func__,
							clockevent.name);
		return ret;
	}

	clockevents_config_and_register(&clockevent, VT8500_TIMER_HZ,
					MIN_OSCR_DELTA * 2, 0xf0000000);

	return 0;
}

TIMER_OF_DECLARE(vt8500, "via,vt8500-timer", vt8500_timer_init);

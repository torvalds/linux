/*
 * Copyright 2012 Simon Arlott
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

#include <linux/bcm2835_timer.h>
#include <linux/bitops.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/sched_clock.h>
#include <asm/irq.h>

#define REG_CONTROL	0x00
#define REG_COUNTER_LO	0x04
#define REG_COUNTER_HI	0x08
#define REG_COMPARE(n)	(0x0c + (n) * 4)
#define MAX_TIMER	3
#define DEFAULT_TIMER	3

struct bcm2835_timer {
	void __iomem *control;
	void __iomem *compare;
	int match_mask;
	struct clock_event_device evt;
	struct irqaction act;
};

static void __iomem *system_clock __read_mostly;

static u32 notrace bcm2835_sched_read(void)
{
	return readl_relaxed(system_clock);
}

static void bcm2835_time_set_mode(enum clock_event_mode mode,
	struct clock_event_device *evt_dev)
{
	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	default:
		WARN(1, "%s: unhandled event mode %d\n", __func__, mode);
		break;
	}
}

static int bcm2835_time_set_next_event(unsigned long event,
	struct clock_event_device *evt_dev)
{
	struct bcm2835_timer *timer = container_of(evt_dev,
		struct bcm2835_timer, evt);
	writel_relaxed(readl_relaxed(system_clock) + event,
		timer->compare);
	return 0;
}

static irqreturn_t bcm2835_time_interrupt(int irq, void *dev_id)
{
	struct bcm2835_timer *timer = dev_id;
	void (*event_handler)(struct clock_event_device *);
	if (readl_relaxed(timer->control) & timer->match_mask) {
		writel_relaxed(timer->match_mask, timer->control);

		event_handler = ACCESS_ONCE(timer->evt.event_handler);
		if (event_handler)
			event_handler(&timer->evt);
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static struct of_device_id bcm2835_time_match[] __initconst = {
	{ .compatible = "brcm,bcm2835-system-timer" },
	{}
};

void __init bcm2835_timer_init(void)
{
	struct device_node *node;
	void __iomem *base;
	u32 freq;
	int irq;
	struct bcm2835_timer *timer;

	node = of_find_matching_node(NULL, bcm2835_time_match);
	if (!node)
		panic("No bcm2835 timer node");

	base = of_iomap(node, 0);
	if (!base)
		panic("Can't remap registers");

	if (of_property_read_u32(node, "clock-frequency", &freq))
		panic("Can't read clock-frequency");

	system_clock = base + REG_COUNTER_LO;
	setup_sched_clock(bcm2835_sched_read, 32, freq);

	clocksource_mmio_init(base + REG_COUNTER_LO, node->name,
		freq, 300, 32, clocksource_mmio_readl_up);

	irq = irq_of_parse_and_map(node, DEFAULT_TIMER);
	if (irq <= 0)
		panic("Can't parse IRQ");

	timer = kzalloc(sizeof(*timer), GFP_KERNEL);
	if (!timer)
		panic("Can't allocate timer struct\n");

	timer->control = base + REG_CONTROL;
	timer->compare = base + REG_COMPARE(DEFAULT_TIMER);
	timer->match_mask = BIT(DEFAULT_TIMER);
	timer->evt.name = node->name;
	timer->evt.rating = 300;
	timer->evt.features = CLOCK_EVT_FEAT_ONESHOT;
	timer->evt.set_mode = bcm2835_time_set_mode;
	timer->evt.set_next_event = bcm2835_time_set_next_event;
	timer->evt.cpumask = cpumask_of(0);
	timer->act.name = node->name;
	timer->act.flags = IRQF_TIMER | IRQF_SHARED;
	timer->act.dev_id = timer;
	timer->act.handler = bcm2835_time_interrupt;

	if (setup_irq(irq, &timer->act))
		panic("Can't set up timer IRQ\n");

	clockevents_config_and_register(&timer->evt, freq, 0xf, 0xffffffff);

	pr_info("bcm2835: system timer (irq = %d)\n", irq);
}

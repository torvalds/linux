/*
 * Copyright (C) 2012 Altera Corporation
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * Modified from mach-picoxcell/time.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/dw_apb_timer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/mach/time.h>
#include <asm/sched_clock.h>

static void timer_get_base_and_rate(struct device_node *np,
				    void __iomem **base, u32 *rate)
{
	*base = of_iomap(np, 0);

	if (!*base)
		panic("Unable to map regs for %s", np->name);

	if (of_property_read_u32(np, "clock-freq", rate) &&
		of_property_read_u32(np, "clock-frequency", rate))
		panic("No clock-frequency property for %s", np->name);
}

static void add_clockevent(struct device_node *event_timer)
{
	void __iomem *iobase;
	struct dw_apb_clock_event_device *ced;
	u32 irq, rate;

	irq = irq_of_parse_and_map(event_timer, 0);
	if (irq == NO_IRQ)
		panic("No IRQ for clock event timer");

	timer_get_base_and_rate(event_timer, &iobase, &rate);

	ced = dw_apb_clockevent_init(0, event_timer->name, 300, iobase, irq,
				     rate);
	if (!ced)
		panic("Unable to initialise clockevent device");

	dw_apb_clockevent_register(ced);
}

static void __iomem *sched_io_base;
static u32 sched_rate;

static void add_clocksource(struct device_node *source_timer)
{
	void __iomem *iobase;
	struct dw_apb_clocksource *cs;
	u32 rate;

	timer_get_base_and_rate(source_timer, &iobase, &rate);

	cs = dw_apb_clocksource_init(300, source_timer->name, iobase, rate);
	if (!cs)
		panic("Unable to initialise clocksource device");

	dw_apb_clocksource_start(cs);
	dw_apb_clocksource_register(cs);

	/*
	 * Fallback to use the clocksource as sched_clock if no separate
	 * timer is found. sched_io_base then points to the current_value
	 * register of the clocksource timer.
	 */
	sched_io_base = iobase + 0x04;
	sched_rate = rate;
}

static u32 read_sched_clock(void)
{
	return __raw_readl(sched_io_base);
}

static const struct of_device_id sptimer_ids[] __initconst = {
	{ .compatible = "picochip,pc3x2-rtc" },
	{ .compatible = "snps,dw-apb-timer-sp" },
	{ /* Sentinel */ },
};

static void init_sched_clock(void)
{
	struct device_node *sched_timer;

	sched_timer = of_find_matching_node(NULL, sptimer_ids);
	if (sched_timer) {
		timer_get_base_and_rate(sched_timer, &sched_io_base,
					&sched_rate);
		of_node_put(sched_timer);
	}

	setup_sched_clock(read_sched_clock, 32, sched_rate);
}

static const struct of_device_id osctimer_ids[] __initconst = {
	{ .compatible = "picochip,pc3x2-timer" },
	{ .compatible = "snps,dw-apb-timer-osc" },
	{},
};

void __init dw_apb_timer_init(void)
{
	struct device_node *event_timer, *source_timer;

	event_timer = of_find_matching_node(NULL, osctimer_ids);
	if (!event_timer)
		panic("No timer for clockevent");
	add_clockevent(event_timer);

	source_timer = of_find_matching_node(event_timer, osctimer_ids);
	if (!source_timer)
		panic("No timer for clocksource");
	add_clocksource(source_timer);

	of_node_put(source_timer);

	init_sched_clock();
}

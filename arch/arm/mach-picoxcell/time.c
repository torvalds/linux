/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/dw_apb_timer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/mach/time.h>
#include <asm/sched_clock.h>

#include "common.h"

static void timer_get_base_and_rate(struct device_node *np,
				    void __iomem **base, u32 *rate)
{
	*base = of_iomap(np, 0);

	if (!*base)
		panic("Unable to map regs for %s", np->name);

	if (of_property_read_u32(np, "clock-freq", rate))
		panic("No clock-freq property for %s", np->name);
}

static void picoxcell_add_clockevent(struct device_node *event_timer)
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

static void picoxcell_add_clocksource(struct device_node *source_timer)
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
}

static void __iomem *sched_io_base;

unsigned u32 notrace picoxcell_read_sched_clock(void)
{
	return __raw_readl(sched_io_base);
}

static const struct of_device_id picoxcell_rtc_ids[] __initconst = {
	{ .compatible = "picochip,pc3x2-rtc" },
	{ /* Sentinel */ },
};

static void picoxcell_init_sched_clock(void)
{
	struct device_node *sched_timer;
	u32 rate;

	sched_timer = of_find_matching_node(NULL, picoxcell_rtc_ids);
	if (!sched_timer)
		panic("No RTC for sched clock to use");

	timer_get_base_and_rate(sched_timer, &sched_io_base, &rate);
	of_node_put(sched_timer);

	setup_sched_clock(picoxcell_read_sched_clock, 32, rate);
}

static const struct of_device_id picoxcell_timer_ids[] __initconst = {
	{ .compatible = "picochip,pc3x2-timer" },
	{},
};

static void __init picoxcell_timer_init(void)
{
	struct device_node *event_timer, *source_timer;

	event_timer = of_find_matching_node(NULL, picoxcell_timer_ids);
	if (!event_timer)
		panic("No timer for clockevent");
	picoxcell_add_clockevent(event_timer);

	source_timer = of_find_matching_node(event_timer, picoxcell_timer_ids);
	if (!source_timer)
		panic("No timer for clocksource");
	picoxcell_add_clocksource(source_timer);

	of_node_put(source_timer);

	picoxcell_init_sched_clock();
}

struct sys_timer picoxcell_timer = {
	.init = picoxcell_timer_init,
};

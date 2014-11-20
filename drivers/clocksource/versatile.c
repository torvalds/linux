/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2014 ARM Limited
 */

#include <linux/clocksource.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/sched_clock.h>

#define SYS_24MHZ 0x05c

static void __iomem *versatile_sys_24mhz;

static u64 notrace versatile_sys_24mhz_read(void)
{
	return readl(versatile_sys_24mhz);
}

static void __init versatile_sched_clock_init(struct device_node *node)
{
	void __iomem *base = of_iomap(node, 0);

	if (!base)
		return;

	versatile_sys_24mhz = base + SYS_24MHZ;

	sched_clock_register(versatile_sys_24mhz_read, 32, 24000000);
}
CLOCKSOURCE_OF_DECLARE(versatile, "arm,vexpress-sysreg",
		       versatile_sched_clock_init);

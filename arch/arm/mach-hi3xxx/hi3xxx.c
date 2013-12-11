5/*
 * (Hisilicon's Hi36xx/Hi37xx SoC based) flattened device tree enabled machine
 *
 * Copyright (c) 2012-2013 Hisilicon Ltd.
 * Copyright (c) 2012-2013 Linaro Ltd.
 *
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/irqchip.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

/*
 * This table is only for optimization. Since ioremap() could always share
 * the same mapping if it's defined as static IO mapping.
 *
 * Without this table, system could also work. The cost is some virtual address
 * spaces wasted since ioremap() may be called multi times for the same
 * IO space.
 */
static struct map_desc hi3620_io_desc[] __initdata = {
	{
		.pfn		= __phys_to_pfn(0xfc802000),
		.virtual	= 0xfe802000,
		.length		= 0x1000,
		.type		= MT_DEVICE,
	},
};

static void __init hi3620_map_io(void)
{
	debug_ll_io_init();
	iotable_init(hi3620_io_desc, ARRAY_SIZE(hi3620_io_desc));
}

static void __init hi3xxx_timer_init(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
}

static const char *hi3xxx_compat[] __initconst = {
	"hisilicon,hi3620-hi4511",
	NULL,
};

DT_MACHINE_START(HI3620, "Hisilicon Hi3620 (Flattened Device Tree)")
	.map_io		= hi3620_map_io,
	.init_time	= hi3xxx_timer_init,
	.dt_compat	= hi3xxx_compat,
MACHINE_END

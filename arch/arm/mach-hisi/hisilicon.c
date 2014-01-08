/*
 * (Hisilicon's SoC based) flattened device tree enabled machine
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
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/proc-fns.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "core.h"

#define HI3620_SYSCTRL_PHYS_BASE		0xfc802000
#define HI3620_SYSCTRL_VIRT_BASE		0xfe802000

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
		/* sysctrl */
		.pfn		= __phys_to_pfn(HI3620_SYSCTRL_PHYS_BASE),
		.virtual	= HI3620_SYSCTRL_VIRT_BASE,
		.length		= 0x1000,
		.type		= MT_DEVICE,
	},
};

static void __init hi3620_map_io(void)
{
	debug_ll_io_init();
	iotable_init(hi3620_io_desc, ARRAY_SIZE(hi3620_io_desc));
}

static void hi3xxx_restart(enum reboot_mode mode, const char *cmd)
{
	struct device_node *np;
	void __iomem *base;
	int offset;

	np = of_find_compatible_node(NULL, NULL, "hisilicon,sysctrl");
	if (!np) {
		pr_err("failed to find hisilicon,sysctrl node\n");
		return;
	}
	base = of_iomap(np, 0);
	if (!base) {
		pr_err("failed to map address in hisilicon,sysctrl node\n");
		return;
	}
	if (of_property_read_u32(np, "reboot-offset", &offset) < 0) {
		pr_err("failed to find reboot-offset property\n");
		return;
	}
	writel_relaxed(0xdeadbeef, base + offset);

	while (1)
		cpu_do_idle();
}

static const char *hi3xxx_compat[] __initconst = {
	"hisilicon,hi3620-hi4511",
	NULL,
};

DT_MACHINE_START(HI3620, "Hisilicon Hi3620 (Flattened Device Tree)")
	.map_io		= hi3620_map_io,
	.dt_compat	= hi3xxx_compat,
	.smp		= smp_ops(hi3xxx_smp_ops),
	.restart	= hi3xxx_restart,
MACHINE_END

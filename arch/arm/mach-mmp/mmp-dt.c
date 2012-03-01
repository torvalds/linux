/*
 *  linux/arch/arm/mach-mmp/mmp-dt.c
 *
 *  Copyright (C) 2012 Marvell Technology Group Ltd.
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <mach/irqs.h>

#include "common.h"

extern struct sys_timer pxa168_timer;
extern void __init icu_init_irq(void);

static const struct of_dev_auxdata mmp_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4017000, "pxa2xx-uart.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4018000, "pxa2xx-uart.1", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4026000, "pxa2xx-uart.2", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4011000, "pxa2xx-i2c.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4025000, "pxa2xx-i2c.1", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-gpio", 0xd4019000, "pxa-gpio", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-rtc", 0xd4010000, "sa1100-rtc", NULL),
	{}
};

static int __init mmp_intc_add_irq_domain(struct device_node *np,
					   struct device_node *parent)
{
	irq_domain_add_simple(np, 0);
	return 0;
}

static int __init mmp_gpio_add_irq_domain(struct device_node *np,
					   struct device_node *parent)
{
	irq_domain_add_simple(np, IRQ_GPIO_START);
	return 0;
}

static const struct of_device_id mmp_irq_match[] __initconst = {
	{ .compatible = "mrvl,mmp-intc", .data = mmp_intc_add_irq_domain, },
	{ .compatible = "mrvl,mmp-gpio", .data = mmp_gpio_add_irq_domain, },
	{}
};

static void __init mmp_dt_init(void)
{

	of_irq_init(mmp_irq_match);

	of_platform_populate(NULL, of_default_bus_match_table,
			     mmp_auxdata_lookup, NULL);
}

static const char *pxa168_dt_board_compat[] __initdata = {
	"mrvl,pxa168-aspenite",
	NULL,
};

DT_MACHINE_START(PXA168_DT, "Marvell PXA168 (Device Tree Support)")
	.map_io		= mmp_map_io,
	.init_irq	= icu_init_irq,
	.timer		= &pxa168_timer,
	.init_machine	= mmp_dt_init,
	.dt_compat	= pxa168_dt_board_compat,
MACHINE_END

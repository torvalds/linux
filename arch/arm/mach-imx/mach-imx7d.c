/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"

static void __init imx7d_init_machine(void)
{
	struct device *parent;

	parent = imx_soc_device_init();
	if (parent == NULL)
		pr_warn("failed to initialize soc device\n");

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	imx_anatop_init();
}

static void __init imx7d_init_irq(void)
{
	imx_init_revision_from_anatop();
	imx_src_init();
	irqchip_init();
}

static const char *const imx7d_dt_compat[] __initconst = {
	"fsl,imx7d",
	NULL,
};

DT_MACHINE_START(IMX7D, "Freescale i.MX7 Dual (Device Tree)")
	.init_irq	= imx7d_init_irq,
	.init_machine	= imx7d_init_machine,
	.dt_compat	= imx7d_dt_compat,
MACHINE_END

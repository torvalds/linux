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
#include <linux/phy.h>

#include "common.h"

static void __init imx7d_init_machine(void)
{
	struct device *parent;

	parent = imx_soc_device_init();
	if (parent == NULL)
		pr_warn("failed to initialize soc device\n");

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	imx7d_pm_init();
	imx_anatop_init();
}

static void __init imx7d_init_irq(void)
{
	imx_gpcv2_check_dt();
	imx_init_revision_from_anatop();
	imx_src_init();
	irqchip_init();
}

static const char *imx7d_dt_compat[] __initconst = {
	"fsl,imx7d",
	NULL,
};

static void __init imx7d_map_io(void)
{
	debug_ll_io_init();
	imx7_pm_map_io();
}

DT_MACHINE_START(IMX7D, "Freescale i.MX7 Dual (Device Tree)")
	.map_io		= imx7d_map_io,
	.smp            = smp_ops(imx_smp_ops),
	.init_irq	= imx7d_init_irq,
	.init_machine	= imx7d_init_machine,
	.dt_compat	= imx7d_dt_compat,
	.restart	= mxc_restart,
MACHINE_END

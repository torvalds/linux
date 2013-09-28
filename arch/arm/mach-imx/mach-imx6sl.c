/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk-provider.h>
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"

static void __init imx6sl_init_machine(void)
{
	mxc_arch_reset_init_dt();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static void __init imx6sl_init_irq(void)
{
	imx_init_l2cache();
	imx_src_init();
	imx_gpc_init();
	irqchip_init();
}

static void __init imx6sl_timer_init(void)
{
	of_clk_init(NULL);
}

static const char *imx6sl_dt_compat[] __initdata = {
	"fsl,imx6sl",
	NULL,
};

DT_MACHINE_START(IMX6SL, "Freescale i.MX6 SoloLite (Device Tree)")
	.map_io		= debug_ll_io_init,
	.init_irq	= imx6sl_init_irq,
	.init_time	= imx6sl_timer_init,
	.init_machine	= imx6sl_init_machine,
	.dt_compat	= imx6sl_dt_compat,
	.restart	= mxc_restart,
MACHINE_END

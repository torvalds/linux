/*
 * Copyright 2012 Steffen Trumtrar, Pengutronix
 *
 * based on imx27-dt.c
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-l2x0.h>
#include "common.h"
#include "mx35.h"

static void __init imx35_dt_init(void)
{
	mxc_arch_reset_init_dt();

	of_platform_populate(NULL, of_default_bus_match_table,
			     NULL, NULL);
}

static void __init imx35_irq_init(void)
{
	imx_init_l2cache();
	mx35_init_irq();
}

static const char *imx35_dt_board_compat[] __initconst = {
	"fsl,imx35",
	NULL
};

DT_MACHINE_START(IMX35_DT, "Freescale i.MX35 (Device Tree Support)")
	.map_io		= mx35_map_io,
	.init_early	= imx35_init_early,
	.init_irq	= imx35_irq_init,
	.handle_irq	= imx35_handle_irq,
	.init_machine	= imx35_dt_init,
	.dt_compat	= imx35_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END

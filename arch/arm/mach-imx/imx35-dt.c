// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Steffen Trumtrar, Pengutronix
 *
 * based on imx27-dt.c
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

static const char * const imx35_dt_board_compat[] __initconst = {
	"fsl,imx35",
	NULL
};

DT_MACHINE_START(IMX35_DT, "Freescale i.MX35 (Device Tree Support)")
	.l2c_aux_val 	= 0,
	.l2c_aux_mask	= ~0,
	.map_io		= mx35_map_io,
	.init_early	= imx35_init_early,
	.init_irq	= mx35_init_irq,
	.dt_compat	= imx35_dt_board_compat,
MACHINE_END

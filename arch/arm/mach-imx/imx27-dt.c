// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012 Sascha Hauer, Pengutronix
 */

#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"
#include "mx27.h"

static const char * const imx27_dt_board_compat[] __initconst = {
	"fsl,imx27",
	NULL
};

DT_MACHINE_START(IMX27_DT, "Freescale i.MX27 (Device Tree Support)")
	.map_io		= mx27_map_io,
	.init_early	= imx27_init_early,
	.init_irq	= mx27_init_irq,
	.init_late	= imx27_pm_init,
	.dt_compat	= imx27_dt_board_compat,
MACHINE_END

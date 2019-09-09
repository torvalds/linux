// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012 Sascha Hauer, Pengutronix
 */

#include <asm/mach/arch.h>
#include "common.h"

static const char * const imx31_dt_board_compat[] __initconst = {
	"fsl,imx31",
	NULL
};

DT_MACHINE_START(IMX31_DT, "Freescale i.MX31 (Device Tree Support)")
	.map_io		= mx31_map_io,
	.init_early	= imx31_init_early,
	.init_irq	= mx31_init_irq,
	.dt_compat	= imx31_dt_board_compat,
MACHINE_END

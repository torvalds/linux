// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Steffen Trumtrar, Pengutronix
 *
 * based on imx27-dt.c
 */

#include <asm/mach/arch.h>
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
	.dt_compat	= imx35_dt_board_compat,
MACHINE_END

/*
 *  Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/of_platform.h>
#include <asm/mach/arch.h>

#include "common.h"

static const char * const imx1_dt_board_compat[] __initconst = {
	"fsl,imx1",
	NULL
};

DT_MACHINE_START(IMX1_DT, "Freescale i.MX1 (Device Tree Support)")
	.map_io		= mx1_map_io,
	.init_early	= imx1_init_early,
	.init_irq	= mx1_init_irq,
	.dt_compat	= imx1_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END

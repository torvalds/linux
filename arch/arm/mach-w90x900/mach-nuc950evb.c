/*
 * linux/arch/arm/mach-w90x900/mach-nuc950evb.c
 *
 * Based on mach-s3c2410/mach-smdk2410.c by Jonas Dietsche
 *
 * Copyright (C) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/platform_device.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <mach/map.h>

#include "nuc950.h"

static void __init nuc950evb_map_io(void)
{
	nuc950_map_io();
	nuc950_init_clocks();
}

static void __init nuc950evb_init(void)
{
	nuc950_board_init();
}

MACHINE_START(W90P950EVB, "W90P950EVB")
	/* Maintainer: Wan ZongShun */
	.phys_io	= W90X900_PA_UART,
	.io_pg_offst	= (((u32)W90X900_VA_UART) >> 18) & 0xfffc,
	.boot_params	= 0,
	.map_io		= nuc950evb_map_io,
	.init_irq	= nuc900_init_irq,
	.init_machine	= nuc950evb_init,
	.timer		= &nuc900_timer,
MACHINE_END

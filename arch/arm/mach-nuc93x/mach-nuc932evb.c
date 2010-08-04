/*
 * linux/arch/arm/mach-w90x900/mach-nuc910evb.c
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

#include "nuc932.h"

static void __init nuc932evb_map_io(void)
{
	nuc932_map_io();
	nuc932_init_clocks();
	nuc932_init_uartclk();
}

static void __init nuc932evb_init(void)
{
	nuc932_board_init();
}

MACHINE_START(NUC932EVB, "NUC932EVB")
	/* Maintainer: Wan ZongShun */
	.phys_io	= NUC93X_PA_UART,
	.io_pg_offst	= (((u32)NUC93X_VA_UART) >> 18) & 0xfffc,
	.boot_params	= 0,
	.map_io		= nuc932evb_map_io,
	.init_irq	= nuc93x_init_irq,
	.init_machine	= nuc932evb_init,
	.timer		= &nuc93x_timer,
MACHINE_END

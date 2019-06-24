// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-w90x900/mach-nuc910evb.c
 *
 * Based on mach-s3c2410/mach-smdk2410.c by Jonas Dietsche
 *
 * Copyright (C) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 */

#include <linux/platform_device.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <mach/map.h>

#include "nuc910.h"

static void __init nuc910evb_map_io(void)
{
	nuc910_map_io();
	nuc910_init_clocks();
}

static void __init nuc910evb_init(void)
{
	nuc910_board_init();
}

MACHINE_START(W90P910EVB, "W90P910EVB")
	/* Maintainer: Wan ZongShun */
	.map_io		= nuc910evb_map_io,
	.init_irq	= nuc900_init_irq,
	.init_machine	= nuc910evb_init,
	.init_time	= nuc900_timer_init,
	.restart	= nuc9xx_restart,
MACHINE_END

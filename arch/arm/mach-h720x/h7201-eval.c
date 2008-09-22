/*
 * linux/arch/arm/mach-h720x/h7201-eval.c
 *
 * Copyright (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *               2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *               2004 Sascha Hauer    <s.hauer@pengutronix.de>
 *
 * Architecture specific stuff for Hynix GMS30C7201 development board
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/device.h>

#include <asm/setup.h>
#include <asm/types.h>
#include <asm/mach-types.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mach/arch.h>
#include <mach/hardware.h>
#include "common.h"

MACHINE_START(H7201, "Hynix GMS30C7201")
	/* Maintainer: Robert Schwebel, Pengutronix */
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf0000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0001000,
	.map_io		= h720x_map_io,
	.init_irq	= h720x_init_irq,
	.timer		= &h7201_timer,
MACHINE_END

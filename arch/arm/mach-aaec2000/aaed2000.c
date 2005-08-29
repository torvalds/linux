/*
 *  linux/arch/arm/mach-aaec2000/aaed2000.c
 *
 *  Support for the Agilent AAED-2000 Development Platform.
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "core.h"

static void __init aaed2000_init_irq(void)
{
	aaec2000_init_irq();
}

static void __init aaed2000_map_io(void)
{
	aaec2000_map_io();
}

MACHINE_START(AAED2000, "Agilent AAED-2000 Development Platform")
	/* Maintainer: Nicolas Bellido Y Ortega */
	.phys_ram	= 0xf0000000,
	.phys_io	= PIO_BASE,
	.io_pg_offst	= ((VIO_BASE) >> 18) & 0xfffc,
	.map_io		= aaed2000_map_io,
	.init_irq	= aaed2000_init_irq,
	.timer		= &aaec2000_timer,
MACHINE_END

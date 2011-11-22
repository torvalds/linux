/*
 *
 * arch/arm/mach-u300/u300.c
 *
 *
 * Copyright (C) 2006-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Platform machine definition.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/memblock.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/memory.h>

static void __init u300_init_machine(void)
{
	u300_init_devices();
}

#ifdef CONFIG_MACH_U300_BS2X
#define MACH_U300_STRING "Ericsson AB U300 S25/S26/B25/B26 Prototype Board"
#endif

#ifdef CONFIG_MACH_U300_BS330
#define MACH_U300_STRING "Ericsson AB U330 S330/B330 Prototype Board"
#endif

#ifdef CONFIG_MACH_U300_BS335
#define MACH_U300_STRING "Ericsson AB U335 S335/B335 Prototype Board"
#endif

#ifdef CONFIG_MACH_U300_BS365
#define MACH_U300_STRING "Ericsson AB U365 S365/B365 Prototype Board"
#endif

MACHINE_START(U300, MACH_U300_STRING)
	/* Maintainer: Linus Walleij <linus.walleij@stericsson.com> */
	.atag_offset	= BOOT_PARAMS_OFFSET,
	.map_io		= u300_map_io,
	.init_irq	= u300_init_irq,
	.timer		= &u300_timer,
	.init_machine	= u300_init_machine,
MACHINE_END

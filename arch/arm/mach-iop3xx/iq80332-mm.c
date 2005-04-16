/*
 * linux/arch/arm/mach-iop3xx/mm.c
 *
 * Low level memory initialization for iq80332 platform
 *
 * Author: Dave Jiang <dave.jiang@intel.com>
 * Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/mm.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>
#include <asm/mach-types.h>


/*
 * IQ80332 specific IO mappings
 *
 * We use RedBoot's setup for the onboard devices.
 */

void __init iq80332_map_io(void)
{
	iop331_map_io();
}

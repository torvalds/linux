/*
 * linux/arch/arm/mach-iop3xx/mm.c
 *
 * Low level memory initialization for iq80321 platform
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
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


/*
 * IQ80321 specific IO mappings
 *
 * We use RedBoot's setup for the onboard devices.
 */
static struct map_desc iq80321_io_desc[] __initdata = {
 /* virtual     physical      length        type */

 /* on-board devices */
 { IQ80321_UART, IQ80321_UART,   0x00100000,   MT_DEVICE }
};

void __init iq80321_map_io(void)
{
	iop321_map_io();

	iotable_init(iq80321_io_desc, ARRAY_SIZE(iq80321_io_desc));
}

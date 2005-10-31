/*
 *  linux/arch/arm/mach-mp1000/mm.c
 *
 *  Extra MM routines for the MP1000
 *
 *  Copyright (C) 2005 Comdial Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sizes.h>

#include <asm/mach/map.h>

extern void clps711x_map_io(void);

static struct map_desc mp1000_io_desc[] __initdata = {
    { MP1000_EIO_BASE,	MP1000_EIO_START,	MP1000_EIO_SIZE, MT_DEVICE },
    { MP1000_FIO_BASE,	MP1000_FIO_START,	MP1000_FIO_SIZE, MT_DEVICE },
    { MP1000_LIO_BASE,	MP1000_LIO_START,	MP1000_LIO_SIZE, MT_DEVICE },
    { MP1000_NIO_BASE,	MP1000_NIO_START,	MP1000_NIO_SIZE, MT_DEVICE },
    { MP1000_IDE_BASE,	MP1000_IDE_START,	MP1000_IDE_SIZE, MT_DEVICE },
    { MP1000_DSP_BASE,	MP1000_DSP_START,	MP1000_DSP_SIZE, MT_DEVICE }
};

void __init mp1000_map_io(void)
{
	clps711x_map_io();
	iotable_init(mp1000_io_desc, ARRAY_SIZE(mp1000_io_desc));
}

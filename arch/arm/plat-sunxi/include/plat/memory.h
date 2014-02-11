/*
 * arch/arm/plat-sunxi/include/plat/memory.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __SW_MEMORY_H
#define __SW_MEMORY_H

/* physical offset of RAM */
#define PLAT_PHYS_OFFSET		UL(0x40000000)

/* default ATAG_MEM size */
#define MEM_SIZE	(512*1024*1024)

/*
 * HACK: if 'x' argument is lower than PLAT_PHYS_OFFSET in the '__phys_to_bus'
 *       macro (which is incorrect by itself), then we know that something is
 *       wrong. Most likely 'x' is already a bus address (result of a conversion
 *       of some physical address belonging to the first 1GB of DRAM). Not much
 *       harm is done if we just keep it as-is instead of wrapping around into
 *       the 0xC0000000-0xFFFFFFFF range. The typical offenders are the CedarX
 *       userland wrappers around proprietary blobs, which mess up the address
 *       arithmetics and may introduce an arbitrary address offset (this offset
 *       is a multiple of 0x40000000). As we have little control over the blobs
 *       used in Android, this hack may workaround some issues for the users
 *       of the systems with 2GB of RAM (the adverse effects of a multiple of
 *       0x40000000 address calculation bugs only show up on such systems).
 */

#define __phys_to_bus(x) ((u32)(x) < PLAT_PHYS_OFFSET ? (x) : \
							(x) - PLAT_PHYS_OFFSET)
#define __bus_to_phys(x) ((x) + PLAT_PHYS_OFFSET)

#endif

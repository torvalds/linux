/*
 *  arch/arm/mach-clps711x/include/mach/memory.h
 *
 *  Copyright (C) 1999 ARM Limited
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
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET	UL(0xc0000000)

#if !defined(CONFIG_ARCH_CDB89712) && !defined (CONFIG_ARCH_AUTCPU12)

#define __virt_to_bus(x)	((x) - PAGE_OFFSET)
#define __bus_to_virt(x)	((x) + PAGE_OFFSET)
#define __pfn_to_bus(x)		(__pfn_to_phys(x) - PHYS_OFFSET)
#define __bus_to_pfn(x)		__phys_to_pfn((x) + PHYS_OFFSET)

#endif


/*
 * Like the SA1100, the EDB7211 has a large gap between physical RAM
 * banks.  In 2.2, the Psion (CL-PS7110) port added custom support for
 * discontiguous physical memory.  In 2.4, we can use the standard
 * Linux NUMA support.
 *
 * This is not necessary for EP7211 implementations with only one used
 * memory bank.  For those systems, simply undefine CONFIG_DISCONTIGMEM.
 */

/*
 * The PS7211 allows up to 256MB max per DRAM bank, but the EDB7211
 * uses only one of the two banks (bank #1).  However, even within
 * bank #1, memory is discontiguous.
 *
 * The EDB7211 has two 8MB DRAM areas with 8MB of empty space between
 * them, so we use 24 for the node max shift to get 16MB node sizes.
 */

/*
 * Because of the wide memory address space between physical RAM banks on the 
 * SA1100, it's much more convenient to use Linux's NUMA support to implement
 * our memory map representation.  Assuming all memory nodes have equal access 
 * characteristics, we then have generic discontiguous memory support.
 *
 * Of course, all this isn't mandatory for SA1100 implementations with only
 * one used memory bank.  For those, simply undefine CONFIG_DISCONTIGMEM.
 *
 * The nodes are matched with the physical memory bank addresses which are 
 * incidentally the same as virtual addresses.
 * 
 * 	node 0:  0xc0000000 - 0xc7ffffff
 * 	node 1:  0xc8000000 - 0xcfffffff
 * 	node 2:  0xd0000000 - 0xd7ffffff
 * 	node 3:  0xd8000000 - 0xdfffffff
 */
#define SECTION_SIZE_BITS	24
#define MAX_PHYSMEM_BITS	32

#endif


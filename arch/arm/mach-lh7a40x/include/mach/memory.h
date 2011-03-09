/* arch/arm/mach-lh7a40x/include/mach/memory.h
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 *
 *  Refer to <file:Documentation/arm/Sharp-LH/SDRAM> for more information.
 *
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET	UL(0xc0000000)

/*
 * Sparsemem version of the above
 */
#define MAX_PHYSMEM_BITS	32
#define SECTION_SIZE_BITS	24

#endif

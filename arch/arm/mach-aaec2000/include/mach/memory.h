/*
 *  arch/arm/mach-aaec2000/include/mach/memory.h
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H


#define PHYS_OFFSET	UL(0xf0000000)

#define __virt_to_bus(x)	__virt_to_phys(x)
#define __bus_to_virt(x)	__phys_to_virt(x)

/*
 * The nodes are the followings:
 *
 *   node 0: 0xf000.0000 - 0xf3ff.ffff
 *   node 1: 0xf400.0000 - 0xf7ff.ffff
 *   node 2: 0xf800.0000 - 0xfbff.ffff
 *   node 3: 0xfc00.0000 - 0xffff.ffff
 */
#define NODE_MEM_SIZE_BITS	26

#endif /* __ASM_ARCH_MEMORY_H */

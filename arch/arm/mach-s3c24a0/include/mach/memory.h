/* linux/arch/arm/mach-s3c24a0/include/mach/memory.h
 *  from linux/include/asm-arm/arch-rpc/memory.h
 *
 *  Copyright (C) 1996,1997,1998 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_24A0_MEMORY_H
#define __ASM_ARCH_24A0_MEMORY_H __FILE__

#define PHYS_OFFSET UL(0x10000000)

#define __virt_to_bus(x) __virt_to_phys(x)
#define __bus_to_virt(x) __phys_to_virt(x)

#endif

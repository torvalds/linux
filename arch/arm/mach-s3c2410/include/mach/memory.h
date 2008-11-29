/* arch/arm/mach-s3c2410/include/mach/memory.h
 *  from arch/arm/mach-rpc/include/mach/memory.h
 *
 *  Copyright (C) 1996,1997,1998 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#define PHYS_OFFSET	UL(0x30000000)

/*
 * This is the maximum DMA address(physical address) that can be DMAd to.
 *  Err, no, this is a virtual address.  And you must set ISA_DMA_THRESHOLD
 *  and setup a DMA zone if this restricts the amount of RAM which is
 *  capable of DMA.
 */
#define MAX_DMA_ADDRESS		0x40000000

#endif

/*
 * arch/arm/mach-h720x/include/mach/memory.h
 *
 * Copyright (c) 2000 Jungjun Kim
 *
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#define PLAT_PHYS_OFFSET	UL(0x40000000)
/*
 * This is the maximum DMA address that can be DMAd to.
 * There should not be more than (0xd0000000 - 0xc0000000)
 * bytes of RAM.
 */
#define ARM_DMA_ZONE_SIZE	SZ_256M

#endif

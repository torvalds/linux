/*
 * arch/arm/mach-sa1100/include/mach/memory.h
 *
 * Copyright (C) 1999-2000 Nicolas Pitre <nico@cam.org>
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#include <asm/sizes.h>

/*
 * Physical DRAM offset is 0xc0000000 on the SA1100
 */
#define PHYS_OFFSET	UL(0xc0000000)

#ifndef __ASSEMBLY__

#ifdef CONFIG_SA1111
void sa1111_adjust_zones(int node, unsigned long *size, unsigned long *holes);

#define arch_adjust_zones(node, size, holes) \
	sa1111_adjust_zones(node, size, holes)

#define ISA_DMA_THRESHOLD	(PHYS_OFFSET + SZ_1M - 1)

#endif
#endif

/*
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *		address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *		to an address that the kernel can use.
 *
 * On the SA1100, bus addresses are equivalent to physical addresses.
 */
#define __virt_to_bus(x)	 __virt_to_phys(x)
#define __bus_to_virt(x)	 __phys_to_virt(x)

/*
 * Because of the wide memory address space between physical RAM banks on the 
 * SA1100, it's much convenient to use Linux's NUMA support to implement our 
 * memory map representation.  Assuming all memory nodes have equal access 
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
#define NODE_MEM_SIZE_BITS	27

/*
 * Cache flushing area - SA1100 zero bank
 */
#define FLUSH_BASE_PHYS		0xe0000000
#define FLUSH_BASE		0xf5000000
#define FLUSH_BASE_MINICACHE	0xf5100000

#endif

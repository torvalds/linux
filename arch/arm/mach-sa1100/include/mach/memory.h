/*
 * arch/arm/mach-sa1100/include/mach/memory.h
 *
 * Copyright (C) 1999-2000 Nicolas Pitre <nico@fluxnic.net>
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#include <asm/sizes.h>

/*
 * Physical DRAM offset is 0xc0000000 on the SA1100
 */
#define PLAT_PHYS_OFFSET	UL(0xc0000000)

/*
 * Because of the wide memory address space between physical RAM banks on the
 * SA1100, it's much convenient to use Linux's SparseMEM support to implement
 * our memory map representation.  Assuming all memory nodes have equal access
 * characteristics, we then have generic discontiguous memory support.
 *
 * The sparsemem banks are matched with the physical memory bank addresses
 * which are incidentally the same as virtual addresses.
 * 
 * 	node 0:  0xc0000000 - 0xc7ffffff
 * 	node 1:  0xc8000000 - 0xcfffffff
 * 	node 2:  0xd0000000 - 0xd7ffffff
 * 	node 3:  0xd8000000 - 0xdfffffff
 */
#define MAX_PHYSMEM_BITS	32
#define SECTION_SIZE_BITS	27

/*
 * Cache flushing area - SA1100 zero bank
 */
#define FLUSH_BASE_PHYS		0xe0000000
#define FLUSH_BASE		0xf5000000
#define FLUSH_BASE_MINICACHE	0xf5100000

#endif

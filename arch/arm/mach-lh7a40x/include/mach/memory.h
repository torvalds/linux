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
 * Virtual view <-> DMA view memory address translations
 * virt_to_bus: Used to translate the virtual address to an
 *		address suitable to be passed to set_dma_addr
 * bus_to_virt: Used to convert an address for DMA operations
 *		to an address that the kernel can use.
 */
#define __virt_to_bus(x)	 __virt_to_phys(x)
#define __bus_to_virt(x)	 __phys_to_virt(x)

#ifdef CONFIG_DISCONTIGMEM

/*
 * Given a kernel address, find the home node of the underlying memory.
 */

# ifdef CONFIG_LH7A40X_ONE_BANK_PER_NODE
#  define KVADDR_TO_NID(addr) \
  (  ((((unsigned long) (addr) - PAGE_OFFSET) >> 24) &  1)\
   | ((((unsigned long) (addr) - PAGE_OFFSET) >> 25) & ~1))
# else  /* 2 banks per node */
#  define KVADDR_TO_NID(addr) \
      (((unsigned long) (addr) - PAGE_OFFSET) >> 26)
# endif

/*
 * Given a page frame number, convert it to a node id.
 */

# ifdef CONFIG_LH7A40X_ONE_BANK_PER_NODE
#  define PFN_TO_NID(pfn) \
  (((((pfn) - PHYS_PFN_OFFSET) >> (24 - PAGE_SHIFT)) &  1)\
 | ((((pfn) - PHYS_PFN_OFFSET) >> (25 - PAGE_SHIFT)) & ~1))
# else  /* 2 banks per node */
#  define PFN_TO_NID(pfn) \
    (((pfn) - PHYS_PFN_OFFSET) >> (26 - PAGE_SHIFT))
#endif

/*
 * Given a kaddr, LOCAL_MEM_MAP finds the owning node of the memory
 * and returns the index corresponding to the appropriate page in the
 * node's mem_map.
 */

# ifdef CONFIG_LH7A40X_ONE_BANK_PER_NODE
#  define LOCAL_MAP_NR(addr) \
       (((unsigned long)(addr) & 0x003fffff) >> PAGE_SHIFT)
# else  /* 2 banks per node */
#  define LOCAL_MAP_NR(addr) \
       (((unsigned long)(addr) & 0x01ffffff) >> PAGE_SHIFT)
# endif

#endif

/*
 * Sparsemem version of the above
 */
#define MAX_PHYSMEM_BITS	32
#define SECTION_SIZE_BITS	24

#endif

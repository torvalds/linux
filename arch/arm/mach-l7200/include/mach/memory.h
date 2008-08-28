/*
 * arch/arm/mach-l7200/include/mach/memory.h
 *
 * Copyright (c) 2000 Steve Hill (sjhill@cotw.com)
 * Copyright (c) 2000 Rob Scott (rscott@mtrob.fdns.net)
 *
 * Changelog:
 *  03-13-2000	SJH	Created
 *  04-13-2000  RS      Changed bus macros for new addr
 *  05-03-2000  SJH     Removed bus macros and fixed virt_to_phys macro
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset on the L7200 SDB.
 */
#define PHYS_OFFSET     UL(0xf0000000)

#define __virt_to_bus(x) __virt_to_phys(x)
#define __bus_to_virt(x) __phys_to_virt(x)

/*
 * Cache flushing area - ROM
 */
#define FLUSH_BASE_PHYS		0x40000000
#define FLUSH_BASE		0xdf000000

#endif

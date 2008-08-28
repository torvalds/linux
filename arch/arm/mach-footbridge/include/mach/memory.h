/*
 *  arch/arm/mach-footbridge/include/mach/memory.h
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   20-Oct-1996 RMK	Created
 *   31-Dec-1997 RMK	Fixed definitions to reduce warnings.
 *   17-May-1998 DAG	Added __virt_to_bus and __bus_to_virt functions.
 *   21-Nov-1998 RMK	Changed __virt_to_bus and __bus_to_virt to macros.
 *   21-Mar-1999 RMK	Added PAGE_OFFSET for co285 architecture.
 *			Renamed to memory.h
 *			Moved PAGE_OFFSET and TASK_SIZE here
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H


#if defined(CONFIG_FOOTBRIDGE_ADDIN)
/*
 * If we may be using add-in footbridge mode, then we must
 * use the out-of-line translation that makes use of the
 * PCI BAR
 */
#ifndef __ASSEMBLY__
extern unsigned long __virt_to_bus(unsigned long);
extern unsigned long __bus_to_virt(unsigned long);
#endif

#elif defined(CONFIG_FOOTBRIDGE_HOST)

#define __virt_to_bus(x)	((x) - 0xe0000000)
#define __bus_to_virt(x)	((x) + 0xe0000000)

#else

#error "Undefined footbridge mode"

#endif

/* Task size and page offset at 3GB */
#define TASK_SIZE		UL(0xbf000000)
#define PAGE_OFFSET		UL(0xc0000000)

/*
 * Cache flushing area.
 */
#define FLUSH_BASE		0xf9000000

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET		UL(0x00000000)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE ((TASK_SIZE + 0x01000000) / 3)

#define FLUSH_BASE_PHYS		0x50000000

#endif

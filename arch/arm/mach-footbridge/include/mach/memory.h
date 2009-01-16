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
#define __virt_to_bus	__virt_to_bus
#define __bus_to_virt	__bus_to_virt

#elif defined(CONFIG_FOOTBRIDGE_HOST)

/*
 * The footbridge is programmed to expose the system RAM at the corresponding
 * address.  So, if PAGE_OFFSET is 0xc0000000, RAM appears at 0xe0000000.
 * If 0x80000000, then its exposed at 0xa0000000 on the bus. etc.
 * The only requirement is that the RAM isn't placed at bus address 0 which
 * would clash with VGA cards.
 */
#define __virt_to_bus(x)	((x) - 0xe0000000)
#define __bus_to_virt(x)	((x) + 0xe0000000)

#else

#error "Undefined footbridge mode"

#endif

/*
 * Cache flushing area.
 */
#define FLUSH_BASE		0xf9000000

/*
 * Physical DRAM offset.
 */
#define PHYS_OFFSET		UL(0x00000000)

#define FLUSH_BASE_PHYS		0x50000000

#endif

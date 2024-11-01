/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/mach-footbridge/include/mach/memory.h
 *
 *  Copyright (C) 1996-1999 Russell King.
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

/*
 * Cache flushing area.
 */
#define FLUSH_BASE		0xf9000000

#define FLUSH_BASE_PHYS		0x50000000

#endif

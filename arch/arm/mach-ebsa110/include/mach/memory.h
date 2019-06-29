/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/mach-ebsa110/include/mach/memory.h
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 *  Changelog:
 *   20-Oct-1996 RMK	Created
 *   31-Dec-1997 RMK	Fixed definitions to reduce warnings
 *   21-Mar-1999 RMK	Renamed to memory.h
 *		 RMK	Moved TASK_SIZE and PAGE_OFFSET here
 */
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Cache flushing area - SRAM
 */
#define FLUSH_BASE_PHYS		0x40000000
#define FLUSH_BASE		0xdf000000

#endif

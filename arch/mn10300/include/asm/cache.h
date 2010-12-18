/* MN10300 cache management registers
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_CACHE_H
#define _ASM_CACHE_H

#include <asm/cpu-regs.h>
#include <proc/cache.h>

#ifndef __ASSEMBLY__
#define L1_CACHE_DISPARITY	(L1_CACHE_NENTRIES * L1_CACHE_BYTES)
#else
#define L1_CACHE_DISPARITY	L1_CACHE_NENTRIES * L1_CACHE_BYTES
#endif

#define ARCH_KMALLOC_MINALIGN	L1_CACHE_BYTES

/* data cache purge registers
 * - read from the register to unconditionally purge that cache line
 * - write address & 0xffffff00 to conditionally purge that cache line
 *   - clear LSB to request invalidation as well
 */
#define DCACHE_PURGE(WAY, ENTRY) \
	__SYSREG(0xc8400000 + (WAY) * L1_CACHE_WAYDISP + \
		 (ENTRY) * L1_CACHE_BYTES, u32)

#define DCACHE_PURGE_WAY0(ENTRY) \
	__SYSREG(0xc8400000 + 0 * L1_CACHE_WAYDISP + (ENTRY) * L1_CACHE_BYTES, u32)
#define DCACHE_PURGE_WAY1(ENTRY) \
	__SYSREG(0xc8400000 + 1 * L1_CACHE_WAYDISP + (ENTRY) * L1_CACHE_BYTES, u32)
#define DCACHE_PURGE_WAY2(ENTRY) \
	__SYSREG(0xc8400000 + 2 * L1_CACHE_WAYDISP + (ENTRY) * L1_CACHE_BYTES, u32)
#define DCACHE_PURGE_WAY3(ENTRY) \
	__SYSREG(0xc8400000 + 3 * L1_CACHE_WAYDISP + (ENTRY) * L1_CACHE_BYTES, u32)

/* instruction cache access registers */
#define ICACHE_DATA(WAY, ENTRY, OFF) \
	__SYSREG(0xc8000000 + (WAY) * L1_CACHE_WAYDISP + (ENTRY) * 0x10 + (OFF) * 4, u32)
#define ICACHE_TAG(WAY, ENTRY)	 \
	__SYSREG(0xc8100000 + (WAY) * L1_CACHE_WAYDISP + (ENTRY) * 0x10, u32)

/* instruction cache access registers */
#define DCACHE_DATA(WAY, ENTRY, OFF) \
	__SYSREG(0xc8200000 + (WAY) * L1_CACHE_WAYDISP + (ENTRY) * 0x10 + (OFF) * 4, u32)
#define DCACHE_TAG(WAY, ENTRY)	 \
	__SYSREG(0xc8300000 + (WAY) * L1_CACHE_WAYDISP + (ENTRY) * 0x10, u32)

#endif /* _ASM_CACHE_H */

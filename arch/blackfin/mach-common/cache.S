/*
 * File:         arch/blackfin/mach-common/cache.S
 * Based on:
 * Author:       LG Soft India
 *
 * Created:
 * Description:  cache control support
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/linkage.h>
#include <asm/cplb.h>
#include <asm/entry.h>
#include <asm/blackfin.h>
#include <asm/cache.h>

.text

/*
 * blackfin_cache_flush_range(start, end)
 * Invalidate all cache lines assocoiated with this
 * area of memory.
 *
 * start:	Start address
 * end:		End address
 */
ENTRY(_blackfin_icache_flush_range)
	R2 = -L1_CACHE_BYTES;
	R2 = R0 & R2;
	P0 = R2;
	P1 = R1;
	CSYNC(R3);
	IFLUSH [P0];
1:
	IFLUSH [P0++];
	CC = P0 < P1 (iu);
	IF CC JUMP 1b (bp);
	IFLUSH [P0];
	SSYNC(R3);
	RTS;
ENDPROC(_blackfin_icache_flush_range)

/*
 * blackfin_icache_dcache_flush_range(start, end)
 * FLUSH all cache lines assocoiated with this
 * area of memory.
 *
 * start:	Start address
 * end:		End address
 */

ENTRY(_blackfin_icache_dcache_flush_range)
	R2 = -L1_CACHE_BYTES;
	R2 = R0 & R2;
	P0 = R2;
	P1 = R1;
	CSYNC(R3);
	IFLUSH [P0];
1:
	FLUSH [P0];
	IFLUSH [P0++];
	CC = P0 < P1 (iu);
	IF CC JUMP 1b (bp);
	IFLUSH [P0];
	FLUSH [P0];
	SSYNC(R3);
	RTS;
ENDPROC(_blackfin_icache_dcache_flush_range)

/* Throw away all D-cached data in specified region without any obligation to
 * write them back. However, we must clean the D-cached entries around the
 * boundaries of the start and/or end address is not cache aligned.
 *
 * Start: start address,
 * end  : end address.
 */

ENTRY(_blackfin_dcache_invalidate_range)
	R2 = -L1_CACHE_BYTES;
	R2 = R0 & R2;
	P0 = R2;
	P1 = R1;
	CSYNC(R3);
	FLUSHINV[P0];
1:
	FLUSHINV[P0++];
	CC = P0 < P1 (iu);
	IF CC JUMP 1b (bp);

	/* If the data crosses a cache line, then we'll be pointing to
	 * the last cache line, but won't have flushed/invalidated it yet,
	 * so do one more.
	 */
	FLUSHINV[P0];
	SSYNC(R3);
	RTS;
ENDPROC(_blackfin_dcache_invalidate_range)

ENTRY(_blackfin_dcache_flush_range)
	R2 = -L1_CACHE_BYTES;
	R2 = R0 & R2;
	P0 = R2;
	P1 = R1;
	CSYNC(R3);
	FLUSH[P0];
1:
	FLUSH[P0++];
	CC = P0 < P1 (iu);
	IF CC JUMP 1b (bp);

	/* If the data crosses a cache line, then we'll be pointing to
	 * the last cache line, but won't have flushed it yet, so do
	 * one more.
	 */
	FLUSH[P0];
	SSYNC(R3);
	RTS;
ENDPROC(_blackfin_dcache_flush_range)

ENTRY(_blackfin_dflush_page)
	P1 = 1 << (PAGE_SHIFT - L1_CACHE_SHIFT);
	P0 = R0;
	CSYNC(R3);
	FLUSH[P0];
	LSETUP (.Lfl1, .Lfl1) LC0 = P1;
.Lfl1:	FLUSH [P0++];
	SSYNC(R3);
	RTS;
ENDPROC(_blackfin_dflush_page)

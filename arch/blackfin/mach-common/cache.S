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
.align 2
ENTRY(_cache_invalidate)

	/*
	 * Icache or DcacheA or DcacheB Invalidation
	 * or any combination thereof
	 * R0 has bits
	 * CPLB_ENABLE_ICACHE_P,CPLB_ENABLE_DCACHE_P,CPLB_ENABLE_DCACHE2_P
	 * set as required
	 */
	[--SP] = R7;

	R7 = R0;
	CC = BITTST(R7,CPLB_ENABLE_ICACHE_P);
	IF !CC JUMP .Lno_icache;
	[--SP] = RETS;
	CALL _icache_invalidate;
	RETS = [SP++];
.Lno_icache:
	CC = BITTST(R7,CPLB_ENABLE_DCACHE_P);
	IF !CC JUMP .Lno_dcache_a;
	R0 = 0;         /* specifies bank A */
	[--SP] = RETS;
	CALL _dcache_invalidate;
	RETS = [SP++];
.Lno_dcache_a:
	CC = BITTST(R7,CPLB_ENABLE_DCACHE2_P);
	IF !CC JUMP .Lno_dcache_b;
	R0 = 0;
	BITSET(R0, 23);		/* specifies bank B */
	[--SP] = RETS;
	CALL  _dcache_invalidate;
	RETS = [SP++];
.Lno_dcache_b:
	R7 = [SP++];
	RTS;

/* Invalidate the Entire Instruction cache by
 * disabling IMC bit
 */
ENTRY(_icache_invalidate)
ENTRY(_invalidate_entire_icache)
	[--SP] = ( R7:5);

	P0.L = (IMEM_CONTROL & 0xFFFF);
	P0.H = (IMEM_CONTROL >> 16);
	R7 = [P0];

	/* Clear the IMC bit , All valid bits in the instruction
	 * cache are set to the invalid state
	 */
	BITCLR(R7,IMC_P);
	CLI R6;
	SSYNC;		/* SSYNC required before invalidating cache. */
	.align 8;
	[P0] = R7;
	SSYNC;
	STI R6;

	/* Configures the instruction cache agian */
	R6 = (IMC | ENICPLB);
	R7 = R7 | R6;

	CLI R6;
	SSYNC;		/* SSYNC required before writing to IMEM_CONTROL. */
	.align 8;
	[P0] = R7;
	SSYNC;
	STI R6;

	( R7:5) = [SP++];
	RTS;

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
	CSYNC;
	IFLUSH [P0];
1:
	IFLUSH [P0++];
	CC = P0 < P1 (iu);
	IF CC JUMP 1b (bp);
	IFLUSH [P0];
	SSYNC;
	RTS;

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
	CSYNC;
	IFLUSH [P0];
1:
	FLUSH [P0];
	IFLUSH [P0++];
	CC = P0 < P1 (iu);
	IF CC JUMP 1b (bp);
	IFLUSH [P0];
	FLUSH [P0];
	SSYNC;
	RTS;

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
	CSYNC;
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
	SSYNC;
	RTS;

/* Invalidate the Entire Data cache by
 * clearing DMC[1:0] bits
 */
ENTRY(_invalidate_entire_dcache)
ENTRY(_dcache_invalidate)
	[--SP] = ( R7:6);

	P0.L = (DMEM_CONTROL & 0xFFFF);
	P0.H = (DMEM_CONTROL >> 16);
	R7 = [P0];

	/* Clear the DMC[1:0] bits, All valid bits in the data
	 * cache are set to the invalid state
	 */
	BITCLR(R7,DMC0_P);
	BITCLR(R7,DMC1_P);
	CLI R6;
	SSYNC;		/* SSYNC required before writing to DMEM_CONTROL. */
	.align 8;
	[P0] = R7;
	SSYNC;
	STI R6;

	/* Configures the data cache again */

	R6 = DMEM_CNTR;
	R7 = R7 | R6;

	CLI R6;
	SSYNC;		/* SSYNC required before writing to DMEM_CONTROL. */
	.align 8;
	[P0] = R7;
	SSYNC;
	STI R6;

	( R7:6) = [SP++];
	RTS;

ENTRY(_blackfin_dcache_flush_range)
	R2 = -L1_CACHE_BYTES;
	R2 = R0 & R2;
	P0 = R2;
	P1 = R1;
	CSYNC;
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
	SSYNC;
	RTS;

ENTRY(_blackfin_dflush_page)
	P1 = 1 << (PAGE_SHIFT - L1_CACHE_SHIFT);
	P0 = R0;
	CSYNC;
	FLUSH[P0];
	LSETUP (.Lfl1, .Lfl1) LC0 = P1;
.Lfl1:	FLUSH [P0++];
	SSYNC;
	RTS;

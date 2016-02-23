/*
 * asm/metag_isa.h
 *
 * Copyright (C) 2000-2007, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Various defines for Meta instruction set.
 */

#ifndef _ASM_METAG_ISA_H_
#define _ASM_METAG_ISA_H_


/* L1 cache layout */

/* Data cache line size as bytes and shift */
#define DCACHE_LINE_BYTES 64
#define DCACHE_LINE_S     6

/* Number of ways in the data cache */
#define DCACHE_WAYS       4

/* Instruction cache line size as bytes and shift */
#define ICACHE_LINE_BYTES 64
#define ICACHE_LINE_S     6

/* Number of ways in the instruction cache */
#define ICACHE_WAYS       4


/*
 * CACHEWD/CACHEWL instructions use the bottom 8 bits of the data presented to
 * control the operation actually achieved.
 */
/* Use of these two bits should be discouraged since the bits dont have
 * consistent meanings
 */
#define CACHEW_ICACHE_BIT           0x01
#define CACHEW_TLBFLUSH_BIT         0x02

#define CACHEW_FLUSH_L1D_L2         0x0
#define CACHEW_INVALIDATE_L1I       0x1
#define CACHEW_INVALIDATE_L1DTLB    0x2
#define CACHEW_INVALIDATE_L1ITLB    0x3
#define CACHEW_WRITEBACK_L1D_L2     0x4
#define CACHEW_INVALIDATE_L1D       0x8
#define CACHEW_INVALIDATE_L1D_L2    0xC

/*
 * CACHERD/CACHERL instructions use bits 3:5 of the address presented to
 * control the operation achieved and hence the specific result.
 */
#define CACHER_ADDR_BITS            0xFFFFFFC0
#define CACHER_OPER_BITS            0x00000030
#define CACHER_OPER_S               4
#define     CACHER_OPER_LINPHY          0
#define CACHER_ICACHE_BIT           0x00000008
#define CACHER_ICACHE_S             3

/*
 * CACHERD/CACHERL LINPHY Oper result is one/two 32-bit words
 *
 *  If CRLINPHY0_VAL_BIT (Bit 0) set then,
 *      Lower 32-bits corresponds to MMCU_ENTRY_* above.
 *      Upper 32-bits corresponds to CRLINPHY1_* values below (if requested).
 *  else
 *      Lower 32-bits corresponds to CRLINPHY0_* values below.
 *      Upper 32-bits undefined.
 */
#define CRLINPHY0_VAL_BIT      0x00000001
#define CRLINPHY0_FIRST_BIT    0x00000004 /* Set if VAL=0 due to first level */

#define CRLINPHY1_READ_BIT     0x00000001 /* Set if reads permitted          */
#define CRLINPHY1_SINGLE_BIT   0x00000004 /* Set if TLB does not cache entry */
#define CRLINPHY1_PAGEMSK_BITS 0x0000FFF0 /* Set to ((2^n-1)>>12) value      */
#define CRLINPHY1_PAGEMSK_S    4

#endif /* _ASM_METAG_ISA_H_ */

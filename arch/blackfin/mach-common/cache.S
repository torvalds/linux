/*
 * Blackfin cache control code
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/linkage.h>
#include <asm/blackfin.h>
#include <asm/cache.h>
#include <asm/page.h>

#ifdef CONFIG_CACHE_FLUSH_L1
.section .l1.text
#else
.text
#endif

/* 05000443 - IFLUSH cannot be last instruction in hardware loop */
#if ANOMALY_05000443
# define BROK_FLUSH_INST "IFLUSH"
#else
# define BROK_FLUSH_INST "no anomaly! yeah!"
#endif

/* Since all L1 caches work the same way, we use the same method for flushing
 * them.  Only the actual flush instruction differs.  We write this in asm as
 * GCC can be hard to coax into writing nice hardware loops.
 *
 * Also, we assume the following register setup:
 * R0 = start address
 * R1 = end address
 */
.macro do_flush flushins:req label

	R2 = -L1_CACHE_BYTES;

	/* start = (start & -L1_CACHE_BYTES) */
	R0 = R0 & R2;

	/* end = ((end - 1) & -L1_CACHE_BYTES) + L1_CACHE_BYTES; */
	R1 += -1;
	R1 = R1 & R2;
	R1 += L1_CACHE_BYTES;

	/* count = (end - start) >> L1_CACHE_SHIFT */
	R2 = R1 - R0;
	R2 >>= L1_CACHE_SHIFT;
	P1 = R2;

.ifnb \label
\label :
.endif
	P0 = R0;

	LSETUP (1f, 2f) LC1 = P1;
1:
.ifeqs "\flushins", BROK_FLUSH_INST
	\flushins [P0++];
2:	nop;
.else
2:	\flushins [P0++];
.endif

	RTS;
.endm

/* Invalidate all instruction cache lines assocoiated with this memory area */
ENTRY(_blackfin_icache_flush_range)
	do_flush IFLUSH
ENDPROC(_blackfin_icache_flush_range)

/* Throw away all D-cached data in specified region without any obligation to
 * write them back.  Since the Blackfin ISA does not have an "invalidate"
 * instruction, we use flush/invalidate.  Perhaps as a speed optimization we
 * could bang on the DTEST MMRs ...
 */
ENTRY(_blackfin_dcache_invalidate_range)
	do_flush FLUSHINV
ENDPROC(_blackfin_dcache_invalidate_range)

/* Flush all data cache lines assocoiated with this memory area */
ENTRY(_blackfin_dcache_flush_range)
	do_flush FLUSH, .Ldfr
ENDPROC(_blackfin_dcache_flush_range)

/* Our headers convert the page structure to an address, so just need to flush
 * its contents like normal.  We know the start address is page aligned (which
 * greater than our cache alignment), as is the end address.  So just jump into
 * the middle of the dcache flush function.
 */
ENTRY(_blackfin_dflush_page)
	P1 = 1 << (PAGE_SHIFT - L1_CACHE_SHIFT);
	jump .Ldfr;
ENDPROC(_blackfin_dflush_page)

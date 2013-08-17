/* MN10300 CPU cache invalidation routines, using automatic purge registers
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/smp.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/irqflags.h>
#include <asm/cacheflush.h>
#include "cache.inc"

#define mn10300_local_dcache_inv_range_intr_interval \
	+((1 << MN10300_DCACHE_INV_RANGE_INTR_LOG2_INTERVAL) - 1)

#if mn10300_local_dcache_inv_range_intr_interval > 0xff
#error MN10300_DCACHE_INV_RANGE_INTR_LOG2_INTERVAL must be 8 or less
#endif

	.am33_2

#ifndef CONFIG_SMP
	.globl	mn10300_icache_inv
	.globl	mn10300_icache_inv_page
	.globl	mn10300_icache_inv_range
	.globl	mn10300_icache_inv_range2
	.globl	mn10300_dcache_inv
	.globl	mn10300_dcache_inv_page
	.globl	mn10300_dcache_inv_range
	.globl	mn10300_dcache_inv_range2

mn10300_icache_inv		= mn10300_local_icache_inv
mn10300_icache_inv_page		= mn10300_local_icache_inv_page
mn10300_icache_inv_range	= mn10300_local_icache_inv_range
mn10300_icache_inv_range2	= mn10300_local_icache_inv_range2
mn10300_dcache_inv		= mn10300_local_dcache_inv
mn10300_dcache_inv_page		= mn10300_local_dcache_inv_page
mn10300_dcache_inv_range	= mn10300_local_dcache_inv_range
mn10300_dcache_inv_range2	= mn10300_local_dcache_inv_range2

#endif /* !CONFIG_SMP */

###############################################################################
#
# void mn10300_local_icache_inv(void)
# Invalidate the entire icache
#
###############################################################################
	ALIGN
	.globl	mn10300_local_icache_inv
        .type	mn10300_local_icache_inv,@function
mn10300_local_icache_inv:
	mov	CHCTR,a0

	movhu	(a0),d0
	btst	CHCTR_ICEN,d0
	beq	mn10300_local_icache_inv_end

	invalidate_icache 1

mn10300_local_icache_inv_end:
	ret	[],0
	.size	mn10300_local_icache_inv,.-mn10300_local_icache_inv

###############################################################################
#
# void mn10300_local_dcache_inv(void)
# Invalidate the entire dcache
#
###############################################################################
	ALIGN
	.globl	mn10300_local_dcache_inv
	.type	mn10300_local_dcache_inv,@function
mn10300_local_dcache_inv:
	mov	CHCTR,a0

	movhu	(a0),d0
	btst	CHCTR_DCEN,d0
	beq	mn10300_local_dcache_inv_end

	invalidate_dcache 1
	
mn10300_local_dcache_inv_end:
	ret	[],0
	.size	mn10300_local_dcache_inv,.-mn10300_local_dcache_inv

###############################################################################
#
# void mn10300_local_dcache_inv_range(unsigned long start, unsigned long end)
# void mn10300_local_dcache_inv_range2(unsigned long start, unsigned long size)
# void mn10300_local_dcache_inv_page(unsigned long start)
# Invalidate a range of addresses on a page in the dcache
#
###############################################################################
	ALIGN
	.globl	mn10300_local_dcache_inv_page
	.globl	mn10300_local_dcache_inv_range
	.globl	mn10300_local_dcache_inv_range2
	.type	mn10300_local_dcache_inv_page,@function
	.type	mn10300_local_dcache_inv_range,@function
	.type	mn10300_local_dcache_inv_range2,@function
mn10300_local_dcache_inv_page:
	and	~(PAGE_SIZE-1),d0
	mov	PAGE_SIZE,d1
mn10300_local_dcache_inv_range2:
	add	d0,d1
mn10300_local_dcache_inv_range:
	# If we are in writeback mode we check the start and end alignments,
	# and if they're not cacheline-aligned, we must flush any bits outside
	# the range that share cachelines with stuff inside the range
#ifdef CONFIG_MN10300_CACHE_WBACK
	btst	~L1_CACHE_TAG_MASK,d0
	bne	1f
	btst	~L1_CACHE_TAG_MASK,d1
	beq	2f
1:
	bra	mn10300_local_dcache_flush_inv_range
2:
#endif /* CONFIG_MN10300_CACHE_WBACK */

	movm	[d2,d3,a2],(sp)

	mov	CHCTR,a0
	movhu	(a0),d2
	btst	CHCTR_DCEN,d2
	beq	mn10300_local_dcache_inv_range_end

	# round the addresses out to be full cachelines, unless we're in
	# writeback mode, in which case we would be in flush and invalidate by
	# now
#ifndef CONFIG_MN10300_CACHE_WBACK
	and	L1_CACHE_TAG_MASK,d0	# round start addr down

	mov	L1_CACHE_BYTES-1,d2
	add	d2,d1
	and	L1_CACHE_TAG_MASK,d1	# round end addr up
#endif /* !CONFIG_MN10300_CACHE_WBACK */

	sub	d0,d1,d2		# calculate the total size
	mov	d0,a2			# A2 = start address
	mov	d1,a1			# A1 = end address

	LOCAL_CLI_SAVE(d3)

	mov	DCPGCR,a0		# make sure the purger isn't busy
	setlb
	mov	(a0),d0
	btst	DCPGCR_DCPGBSY,d0
	lne

	# skip initial address alignment calculation if address is zero
	mov	d2,d1
	cmp	0,a2
	beq	1f

dcivloop:
	/* calculate alignsize
	 *
	 * alignsize = L1_CACHE_BYTES;
	 * while (! start & alignsize) {
	 *	alignsize <<=1;
	 * }
	 * d1 = alignsize;
	 */
	mov	L1_CACHE_BYTES,d1
	lsr	1,d1
	setlb
	add	d1,d1
	mov	d1,d0
	and	a2,d0
	leq

1:
	/* calculate invsize
	 *
	 * if (totalsize > alignsize) {
	 *	invsize = alignsize;
	 * } else {
	 *	invsize = totalsize;
	 *	tmp = 0x80000000;
	 *	while (! invsize & tmp) {
	 *		tmp >>= 1;
	 *	}
	 *	invsize = tmp;
	 * }
	 * d1 = invsize
	 */
	cmp	d2,d1
	bns	2f
	mov	d2,d1

	mov	0x80000000,d0		# start from 31bit=1
	setlb
	lsr	1,d0
	mov	d0,e0
	and	d1,e0
	leq
	mov	d0,d1

2:
	/* set mask
	 *
	 * mask = ~(invsize-1);
	 * DCPGMR = mask;
	 */
	mov	d1,d0
	add	-1,d0
	not	d0
	mov	d0,(DCPGMR)

	# invalidate area
	mov	a2,d0
	or	DCPGCR_DCI,d0
	mov	d0,(a0)			# DCPGCR = (mask & start) | DCPGCR_DCI

	setlb				# wait for the purge to complete
	mov	(a0),d0
	btst	DCPGCR_DCPGBSY,d0
	lne

	sub	d1,d2			# decrease size remaining
	add	d1,a2			# increase next start address

	/* check invalidating of end address
	 *
	 * a2 = a2 + invsize
	 * if (a2 < end) {
	 *     goto dcivloop;
	 * } */
	cmp	a1,a2
	bns	dcivloop

	LOCAL_IRQ_RESTORE(d3)

mn10300_local_dcache_inv_range_end:
	ret	[d2,d3,a2],12
	.size	mn10300_local_dcache_inv_page,.-mn10300_local_dcache_inv_page
	.size	mn10300_local_dcache_inv_range,.-mn10300_local_dcache_inv_range
	.size	mn10300_local_dcache_inv_range2,.-mn10300_local_dcache_inv_range2

###############################################################################
#
# void mn10300_local_icache_inv_page(unsigned long start)
# void mn10300_local_icache_inv_range2(unsigned long start, unsigned long size)
# void mn10300_local_icache_inv_range(unsigned long start, unsigned long end)
# Invalidate a range of addresses on a page in the icache
#
###############################################################################
	ALIGN
	.globl	mn10300_local_icache_inv_page
	.globl	mn10300_local_icache_inv_range
	.globl	mn10300_local_icache_inv_range2
	.type	mn10300_local_icache_inv_page,@function
	.type	mn10300_local_icache_inv_range,@function
	.type	mn10300_local_icache_inv_range2,@function
mn10300_local_icache_inv_page:
	and	~(PAGE_SIZE-1),d0
	mov	PAGE_SIZE,d1
mn10300_local_icache_inv_range2:
	add	d0,d1
mn10300_local_icache_inv_range:
	movm	[d2,d3,a2],(sp)

	mov	CHCTR,a0
	movhu	(a0),d2
	btst	CHCTR_ICEN,d2
	beq	mn10300_local_icache_inv_range_reg_end

	/* calculate alignsize
	 *
	 * alignsize = L1_CACHE_BYTES;
	 * for (i = (end - start - 1) / L1_CACHE_BYTES ;  i > 0; i >>= 1) {
	 *     alignsize <<= 1;
	 * }
	 * d2 = alignsize;
	 */
	mov	L1_CACHE_BYTES,d2
	sub	d0,d1,d3
	add	-1,d3
	lsr	L1_CACHE_SHIFT,d3
	beq	2f
1:
	add	d2,d2
	lsr	1,d3
	bne	1b
2:

	/* a1 = end */
	mov	d1,a1

	LOCAL_CLI_SAVE(d3)

	mov	ICIVCR,a0
	/* wait for busy bit of area invalidation */
	setlb
	mov	(a0),d1
	btst	ICIVCR_ICIVBSY,d1
	lne

	/* set mask
	 *
	 * mask = ~(alignsize-1);
	 * ICIVMR = mask;
	 */
	mov	d2,d1
	add	-1,d1
	not	d1
	mov	d1,(ICIVMR)
	/* a2 = mask & start */
	and	d1,d0,a2

icivloop:
	/* area invalidate
	 *
	 * ICIVCR = (mask & start) | ICIVCR_ICI
	 */
	mov	a2,d0
	or	ICIVCR_ICI,d0
	mov	d0,(a0)

	/* wait for busy bit of area invalidation */
	setlb
	mov	(a0),d1
	btst	ICIVCR_ICIVBSY,d1
	lne

	/* check invalidating of end address
	 *
	 * a2 = a2 + alignsize
	 * if (a2 < end) {
	 *     goto icivloop;
	 * } */
	add	d2,a2
	cmp	a1,a2
	bns	icivloop

	LOCAL_IRQ_RESTORE(d3)

mn10300_local_icache_inv_range_reg_end:
	ret	[d2,d3,a2],12
	.size	mn10300_local_icache_inv_page,.-mn10300_local_icache_inv_page
	.size	mn10300_local_icache_inv_range,.-mn10300_local_icache_inv_range
	.size	mn10300_local_icache_inv_range2,.-mn10300_local_icache_inv_range2

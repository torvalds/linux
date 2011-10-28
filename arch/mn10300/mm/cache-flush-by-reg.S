/* MN10300 CPU core caching routines, using indirect regs on cache controller
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

	.am33_2

#ifndef CONFIG_SMP
	.globl mn10300_dcache_flush
	.globl mn10300_dcache_flush_page
	.globl mn10300_dcache_flush_range
	.globl mn10300_dcache_flush_range2
	.globl mn10300_dcache_flush_inv
	.globl mn10300_dcache_flush_inv_page
	.globl mn10300_dcache_flush_inv_range
	.globl mn10300_dcache_flush_inv_range2

mn10300_dcache_flush		= mn10300_local_dcache_flush
mn10300_dcache_flush_page	= mn10300_local_dcache_flush_page
mn10300_dcache_flush_range	= mn10300_local_dcache_flush_range
mn10300_dcache_flush_range2	= mn10300_local_dcache_flush_range2
mn10300_dcache_flush_inv	= mn10300_local_dcache_flush_inv
mn10300_dcache_flush_inv_page	= mn10300_local_dcache_flush_inv_page
mn10300_dcache_flush_inv_range	= mn10300_local_dcache_flush_inv_range
mn10300_dcache_flush_inv_range2	= mn10300_local_dcache_flush_inv_range2

#endif /* !CONFIG_SMP */

###############################################################################
#
# void mn10300_local_dcache_flush(void)
# Flush the entire data cache back to RAM
#
###############################################################################
	ALIGN
	.globl	mn10300_local_dcache_flush
        .type	mn10300_local_dcache_flush,@function
mn10300_local_dcache_flush:
	movhu	(CHCTR),d0
	btst	CHCTR_DCEN,d0
	beq	mn10300_local_dcache_flush_end

	mov	DCPGCR,a0

	LOCAL_CLI_SAVE(d1)

	# wait for busy bit of area purge
	setlb
	mov	(a0),d0
	btst	DCPGCR_DCPGBSY,d0
	lne

	# set mask
	clr	d0
	mov	d0,(DCPGMR)

	# area purge
	#
	# DCPGCR = DCPGCR_DCP
	#
	mov	DCPGCR_DCP,d0
	mov	d0,(a0)

	# wait for busy bit of area purge
	setlb
	mov	(a0),d0
	btst	DCPGCR_DCPGBSY,d0
	lne

	LOCAL_IRQ_RESTORE(d1)

mn10300_local_dcache_flush_end:
	ret	[],0
	.size	mn10300_local_dcache_flush,.-mn10300_local_dcache_flush

###############################################################################
#
# void mn10300_local_dcache_flush_page(unsigned long start)
# void mn10300_local_dcache_flush_range(unsigned long start, unsigned long end)
# void mn10300_local_dcache_flush_range2(unsigned long start, unsigned long size)
# Flush a range of addresses on a page in the dcache
#
###############################################################################
	ALIGN
	.globl	mn10300_local_dcache_flush_page
	.globl	mn10300_local_dcache_flush_range
	.globl	mn10300_local_dcache_flush_range2
	.type	mn10300_local_dcache_flush_page,@function
	.type	mn10300_local_dcache_flush_range,@function
	.type	mn10300_local_dcache_flush_range2,@function
mn10300_local_dcache_flush_page:
	and	~(PAGE_SIZE-1),d0
	mov	PAGE_SIZE,d1
mn10300_local_dcache_flush_range2:
	add	d0,d1
mn10300_local_dcache_flush_range:
	movm	[d2,d3,a2],(sp)

	movhu	(CHCTR),d2
	btst	CHCTR_DCEN,d2
	beq	mn10300_local_dcache_flush_range_end

	# calculate alignsize
	#
	# alignsize = L1_CACHE_BYTES;
	# for (i = (end - start - 1) / L1_CACHE_BYTES ;  i > 0; i >>= 1)
	#     alignsize <<= 1;
	# d2 = alignsize;
	#
	mov	L1_CACHE_BYTES,d2
	sub	d0,d1,d3
	add	-1,d3
	lsr	L1_CACHE_SHIFT,d3
	beq	2f
1:
	add     d2,d2
	lsr     1,d3
	bne     1b
2:
	mov	d1,a1		# a1 = end

	LOCAL_CLI_SAVE(d3)
	mov	DCPGCR,a0

	# wait for busy bit of area purge
	setlb
	mov	(a0),d1
	btst	DCPGCR_DCPGBSY,d1
	lne

	# determine the mask
	mov	d2,d1
	add	-1,d1
	not	d1		# d1 = mask = ~(alignsize-1)
	mov	d1,(DCPGMR)

	and	d1,d0,a2	# a2 = mask & start

dcpgloop:
	# area purge
	mov	a2,d0
	or	DCPGCR_DCP,d0
	mov	d0,(a0)		# DCPGCR = (mask & start) | DCPGCR_DCP

	# wait for busy bit of area purge
	setlb
	mov	(a0),d1
	btst	DCPGCR_DCPGBSY,d1
	lne

	# check purge of end address
	add	d2,a2		# a2 += alignsize
	cmp	a1,a2		# if (a2 < end) goto dcpgloop
	bns	dcpgloop

	LOCAL_IRQ_RESTORE(d3)

mn10300_local_dcache_flush_range_end:
	ret	[d2,d3,a2],12

	.size	mn10300_local_dcache_flush_page,.-mn10300_local_dcache_flush_page
	.size	mn10300_local_dcache_flush_range,.-mn10300_local_dcache_flush_range
	.size	mn10300_local_dcache_flush_range2,.-mn10300_local_dcache_flush_range2

###############################################################################
#
# void mn10300_local_dcache_flush_inv(void)
# Flush the entire data cache and invalidate all entries
#
###############################################################################
	ALIGN
	.globl	mn10300_local_dcache_flush_inv
	.type	mn10300_local_dcache_flush_inv,@function
mn10300_local_dcache_flush_inv:
	movhu	(CHCTR),d0
	btst	CHCTR_DCEN,d0
	beq	mn10300_local_dcache_flush_inv_end

	mov	DCPGCR,a0

	LOCAL_CLI_SAVE(d1)

	# wait for busy bit of area purge & invalidate
	setlb
	mov	(a0),d0
	btst	DCPGCR_DCPGBSY,d0
	lne

	# set the mask to cover everything
	clr	d0
	mov	d0,(DCPGMR)

	# area purge & invalidate
	mov	DCPGCR_DCP|DCPGCR_DCI,d0
	mov	d0,(a0)

	# wait for busy bit of area purge & invalidate
	setlb
	mov	(a0),d0
	btst	DCPGCR_DCPGBSY,d0
	lne

	LOCAL_IRQ_RESTORE(d1)

mn10300_local_dcache_flush_inv_end:
	ret	[],0
	.size	mn10300_local_dcache_flush_inv,.-mn10300_local_dcache_flush_inv

###############################################################################
#
# void mn10300_local_dcache_flush_inv_page(unsigned long start)
# void mn10300_local_dcache_flush_inv_range(unsigned long start, unsigned long end)
# void mn10300_local_dcache_flush_inv_range2(unsigned long start, unsigned long size)
# Flush and invalidate a range of addresses on a page in the dcache
#
###############################################################################
	ALIGN
	.globl	mn10300_local_dcache_flush_inv_page
	.globl	mn10300_local_dcache_flush_inv_range
	.globl	mn10300_local_dcache_flush_inv_range2
	.type	mn10300_local_dcache_flush_inv_page,@function
	.type	mn10300_local_dcache_flush_inv_range,@function
	.type	mn10300_local_dcache_flush_inv_range2,@function
mn10300_local_dcache_flush_inv_page:
	and	~(PAGE_SIZE-1),d0
	mov	PAGE_SIZE,d1
mn10300_local_dcache_flush_inv_range2:
	add	d0,d1
mn10300_local_dcache_flush_inv_range:
	movm	[d2,d3,a2],(sp)

	movhu	(CHCTR),d2
	btst	CHCTR_DCEN,d2
	beq	mn10300_local_dcache_flush_inv_range_end

	# calculate alignsize
	#
	# alignsize = L1_CACHE_BYTES;
	# for (i = (end - start - 1) / L1_CACHE_BYTES; i > 0; i >>= 1)
	#     alignsize <<= 1;
	# d2 = alignsize
	#
	mov	L1_CACHE_BYTES,d2
	sub	d0,d1,d3
	add	-1,d3
	lsr	L1_CACHE_SHIFT,d3
	beq	2f
1:
	add     d2,d2
	lsr     1,d3
	bne     1b
2:
	mov	d1,a1		# a1 = end

	LOCAL_CLI_SAVE(d3)
	mov	DCPGCR,a0

	# wait for busy bit of area purge & invalidate
	setlb
	mov	(a0),d1
	btst	DCPGCR_DCPGBSY,d1
	lne

	# set the mask
	mov	d2,d1
	add	-1,d1
	not	d1		# d1 = mask = ~(alignsize-1)
	mov	d1,(DCPGMR)

	and	d1,d0,a2	# a2 = mask & start

dcpgivloop:
	# area purge & invalidate
	mov	a2,d0
	or	DCPGCR_DCP|DCPGCR_DCI,d0
	mov	d0,(a0)		# DCPGCR = (mask & start)|DCPGCR_DCP|DCPGCR_DCI

	# wait for busy bit of area purge & invalidate
	setlb
	mov	(a0),d1
	btst	DCPGCR_DCPGBSY,d1
	lne

	# check purge & invalidate of end address
	add	d2,a2		# a2 += alignsize
	cmp	a1,a2		# if (a2 < end) goto dcpgivloop
	bns	dcpgivloop

	LOCAL_IRQ_RESTORE(d3)

mn10300_local_dcache_flush_inv_range_end:
	ret	[d2,d3,a2],12
	.size	mn10300_local_dcache_flush_inv_page,.-mn10300_local_dcache_flush_inv_page
	.size	mn10300_local_dcache_flush_inv_range,.-mn10300_local_dcache_flush_inv_range
	.size	mn10300_local_dcache_flush_inv_range2,.-mn10300_local_dcache_flush_inv_range2

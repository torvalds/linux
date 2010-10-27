/* MN10300 CPU core caching routines
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

#define mn10300_dcache_inv_range_intr_interval \
	+((1 << MN10300_DCACHE_INV_RANGE_INTR_LOG2_INTERVAL) - 1)

#if mn10300_dcache_inv_range_intr_interval > 0xff
#error MN10300_DCACHE_INV_RANGE_INTR_LOG2_INTERVAL must be 8 or less
#endif

	.am33_2

	.globl mn10300_icache_inv
	.globl mn10300_dcache_inv
	.globl mn10300_dcache_inv_range
	.globl mn10300_dcache_inv_range2
	.globl mn10300_dcache_inv_page

###############################################################################
#
# void mn10300_icache_inv(void)
# Invalidate the entire icache
#
###############################################################################
	ALIGN
mn10300_icache_inv:
	mov	CHCTR,a0

	movhu	(a0),d0
	btst	CHCTR_ICEN,d0
	beq	mn10300_icache_inv_end

	mov	epsw,d1
	and	~EPSW_IE,epsw
	nop
	nop

	# disable the icache
	and	~CHCTR_ICEN,d0
	movhu	d0,(a0)

	# and wait for it to calm down
	setlb
	movhu	(a0),d0
	btst	CHCTR_ICBUSY,d0
	lne

	# invalidate
	or	CHCTR_ICINV,d0
	movhu	d0,(a0)

	# wait for the cache to finish
	mov	CHCTR,a0
	setlb
	movhu	(a0),d0
	btst	CHCTR_ICBUSY,d0
	lne

	# and reenable it
	and	~CHCTR_ICINV,d0
	or	CHCTR_ICEN,d0
	movhu	d0,(a0)
	movhu	(a0),d0

	mov	d1,epsw

mn10300_icache_inv_end:
	ret	[],0

###############################################################################
#
# void mn10300_dcache_inv(void)
# Invalidate the entire dcache
#
###############################################################################
	ALIGN
mn10300_dcache_inv:
	mov	CHCTR,a0

	movhu	(a0),d0
	btst	CHCTR_DCEN,d0
	beq	mn10300_dcache_inv_end

	mov	epsw,d1
	and	~EPSW_IE,epsw
	nop
	nop

	# disable the dcache
	and	~CHCTR_DCEN,d0
	movhu	d0,(a0)

	# and wait for it to calm down
	setlb
	movhu	(a0),d0
	btst	CHCTR_DCBUSY,d0
	lne

	# invalidate
	or	CHCTR_DCINV,d0
	movhu	d0,(a0)

	# wait for the cache to finish
	mov	CHCTR,a0
	setlb
	movhu	(a0),d0
	btst	CHCTR_DCBUSY,d0
	lne

	# and reenable it
	and	~CHCTR_DCINV,d0
	or	CHCTR_DCEN,d0
	movhu	d0,(a0)
	movhu	(a0),d0

	mov	d1,epsw

mn10300_dcache_inv_end:
	ret	[],0

###############################################################################
#
# void mn10300_dcache_inv_range(unsigned start, unsigned end)
# void mn10300_dcache_inv_range2(unsigned start, unsigned size)
# void mn10300_dcache_inv_page(unsigned start)
# Invalidate a range of addresses on a page in the dcache
#
###############################################################################
	ALIGN
mn10300_dcache_inv_page:
	mov	PAGE_SIZE,d1
mn10300_dcache_inv_range2:
	add	d0,d1
mn10300_dcache_inv_range:
	movm	[d2,d3,a2],(sp)
	mov	CHCTR,a2

	movhu	(a2),d2
	btst	CHCTR_DCEN,d2
	beq	mn10300_dcache_inv_range_end

	and	L1_CACHE_TAG_ADDRESS|L1_CACHE_TAG_ENTRY,d0	# round start
								# addr down
	mov	d0,a1

	add	L1_CACHE_BYTES,d1			# round end addr up
	and	L1_CACHE_TAG_ADDRESS|L1_CACHE_TAG_ENTRY,d1

	clr	d2				# we're going to clear tag ram
						# entries

	# read the tags from the tag RAM, and if they indicate a valid dirty
	# cache line then invalidate that line
	mov	DCACHE_TAG(0,0),a0
	mov	a1,d0
	and	L1_CACHE_TAG_ENTRY,d0
	add	d0,a0				# starting dcache tag RAM
						# access address

	sub	a1,d1
	lsr	L1_CACHE_SHIFT,d1		# total number of entries to
						# examine

	and	~(L1_CACHE_DISPARITY-1),a1	# determine comparator base

mn10300_dcache_inv_range_outer_loop:
	# disable interrupts
	mov	epsw,d3
	and	~EPSW_IE,epsw
	nop					# note that reading CHCTR and
						# AND'ing D0 occupy two delay
						# slots after disabling
						# interrupts

	# disable the dcache
	movhu	(a2),d0
	and	~CHCTR_DCEN,d0
	movhu	d0,(a2)

	# and wait for it to calm down
	setlb
	movhu	(a2),d0
	btst	CHCTR_DCBUSY,d0
	lne

mn10300_dcache_inv_range_loop:

	# process the way 0 slot
	mov	(L1_CACHE_WAYDISP*0,a0),d0	# read the tag in the way 0 slot
	btst	L1_CACHE_TAG_VALID,d0
	beq	mn10300_dcache_inv_range_skip_0	# jump if this cacheline is not
						# valid

	xor	a1,d0
	lsr	12,d0
	bne	mn10300_dcache_inv_range_skip_0	# jump if not this cacheline

	mov	d2,(a0)				# kill the tag

mn10300_dcache_inv_range_skip_0:

	# process the way 1 slot
	mov	(L1_CACHE_WAYDISP*1,a0),d0	# read the tag in the way 1 slot
	btst	L1_CACHE_TAG_VALID,d0
	beq	mn10300_dcache_inv_range_skip_1	# jump if this cacheline is not
						# valid

	xor	a1,d0
	lsr	12,d0
	bne	mn10300_dcache_inv_range_skip_1	# jump if not this cacheline

	mov	d2,(a0)				# kill the tag

mn10300_dcache_inv_range_skip_1:

	# process the way 2 slot
	mov	(L1_CACHE_WAYDISP*2,a0),d0	# read the tag in the way 2 slot
	btst	L1_CACHE_TAG_VALID,d0
	beq	mn10300_dcache_inv_range_skip_2	# jump if this cacheline is not
						# valid

	xor	a1,d0
	lsr	12,d0
	bne	mn10300_dcache_inv_range_skip_2	# jump if not this cacheline

	mov	d2,(a0)				# kill the tag

mn10300_dcache_inv_range_skip_2:

	# process the way 3 slot
	mov	(L1_CACHE_WAYDISP*3,a0),d0	# read the tag in the way 3 slot
	btst	L1_CACHE_TAG_VALID,d0
	beq	mn10300_dcache_inv_range_skip_3	# jump if this cacheline is not
						# valid

	xor	a1,d0
	lsr	12,d0
	bne	mn10300_dcache_inv_range_skip_3	# jump if not this cacheline

	mov	d2,(a0)				# kill the tag

mn10300_dcache_inv_range_skip_3:

	# approx every N steps we re-enable the cache and see if there are any
	# interrupts to be processed
	# we also break out if we've reached the end of the loop
	# (the bottom nibble of the count is zero in both cases)
	add	L1_CACHE_BYTES,a0
	add	L1_CACHE_BYTES,a1
	add	-1,d1
	btst	mn10300_dcache_inv_range_intr_interval,d1
	bne	mn10300_dcache_inv_range_loop

	# wait for the cache to finish what it's doing
	setlb
	movhu	(a2),d0
	btst	CHCTR_DCBUSY,d0
	lne

	# and reenable it
	or	CHCTR_DCEN,d0
	movhu	d0,(a2)
	movhu	(a2),d0

	# re-enable interrupts
	# - we don't bother with delay NOPs as we'll have enough instructions
	#   before we disable interrupts again to give the interrupts a chance
	#   to happen
	mov	d3,epsw

	# go around again if the counter hasn't yet reached zero
	add	0,d1
	bne	mn10300_dcache_inv_range_outer_loop

mn10300_dcache_inv_range_end:
	ret	[d2,d3,a2],12

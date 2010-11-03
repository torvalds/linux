/* MN10300 CPU core caching routines, using direct tag flushing
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

	# read the addresses tagged in the cache's tag RAM and attempt to flush
	# those addresses specifically
	# - we rely on the hardware to filter out invalid tag entry addresses
	mov	DCACHE_TAG(0,0),a0		# dcache tag RAM access address
	mov	DCACHE_PURGE(0,0),a1		# dcache purge request address
	mov	L1_CACHE_NWAYS*L1_CACHE_NENTRIES,d1  # total number of entries

mn10300_local_dcache_flush_loop:
	mov	(a0),d0
	and	L1_CACHE_TAG_ADDRESS|L1_CACHE_TAG_ENTRY,d0
	or	L1_CACHE_TAG_VALID,d0		# retain valid entries in the
						# cache
	mov	d0,(a1)				# conditional purge

	add	L1_CACHE_BYTES,a0
	add	L1_CACHE_BYTES,a1
	add	-1,d1
	bne	mn10300_local_dcache_flush_loop

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
	movm	[d2],(sp)

	movhu	(CHCTR),d2
	btst	CHCTR_DCEN,d2
	beq	mn10300_local_dcache_flush_range_end

	sub	d0,d1,a0
	cmp	MN10300_DCACHE_FLUSH_BORDER,a0
	ble	1f

	movm	(sp),[d2]
	bra	mn10300_local_dcache_flush
1:

	# round start addr down
	and	L1_CACHE_TAG_ADDRESS|L1_CACHE_TAG_ENTRY,d0
	mov	d0,a1

	add	L1_CACHE_BYTES,d1			# round end addr up
	and	L1_CACHE_TAG_ADDRESS|L1_CACHE_TAG_ENTRY,d1

	# write a request to flush all instances of an address from the cache
	mov	DCACHE_PURGE(0,0),a0
	mov	a1,d0
	and	L1_CACHE_TAG_ENTRY,d0
	add	d0,a0				# starting dcache purge control
						# reg address

	sub	a1,d1
	lsr	L1_CACHE_SHIFT,d1		# total number of entries to
						# examine

	or	L1_CACHE_TAG_VALID,a1		# retain valid entries in the
						# cache

mn10300_local_dcache_flush_range_loop:
	mov	a1,(L1_CACHE_WAYDISP*0,a0)	# conditionally purge this line
						# all ways

	add	L1_CACHE_BYTES,a0
	add	L1_CACHE_BYTES,a1
	and	~L1_CACHE_WAYDISP,a0		# make sure way stay on way 0
	add	-1,d1
	bne	mn10300_local_dcache_flush_range_loop

mn10300_local_dcache_flush_range_end:
	ret	[d2],4

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

	mov	L1_CACHE_NENTRIES,d1
	clr	a1

mn10300_local_dcache_flush_inv_loop:
	mov	(DCACHE_PURGE_WAY0(0),a1),d0	# unconditional purge
	mov	(DCACHE_PURGE_WAY1(0),a1),d0	# unconditional purge
	mov	(DCACHE_PURGE_WAY2(0),a1),d0	# unconditional purge
	mov	(DCACHE_PURGE_WAY3(0),a1),d0	# unconditional purge

	add	L1_CACHE_BYTES,a1
	add	-1,d1
	bne	mn10300_local_dcache_flush_inv_loop

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
	movm	[d2],(sp)

	movhu	(CHCTR),d2
	btst	CHCTR_DCEN,d2
	beq	mn10300_local_dcache_flush_inv_range_end

	sub	d0,d1,a0
	cmp	MN10300_DCACHE_FLUSH_INV_BORDER,a0
	ble	1f

	movm	(sp),[d2]
	bra	mn10300_local_dcache_flush_inv
1:

	and	L1_CACHE_TAG_ADDRESS|L1_CACHE_TAG_ENTRY,d0	# round start
								# addr down
	mov	d0,a1

	add	L1_CACHE_BYTES,d1			# round end addr up
	and	L1_CACHE_TAG_ADDRESS|L1_CACHE_TAG_ENTRY,d1

	# write a request to flush and invalidate all instances of an address
	# from the cache
	mov	DCACHE_PURGE(0,0),a0
	mov	a1,d0
	and	L1_CACHE_TAG_ENTRY,d0
	add	d0,a0				# starting dcache purge control
						# reg address

	sub	a1,d1
	lsr	L1_CACHE_SHIFT,d1		# total number of entries to
						# examine

mn10300_local_dcache_flush_inv_range_loop:
	mov	a1,(L1_CACHE_WAYDISP*0,a0)	# conditionally purge this line
						# in all ways

	add	L1_CACHE_BYTES,a0
	add	L1_CACHE_BYTES,a1
	and	~L1_CACHE_WAYDISP,a0		# make sure way stay on way 0
	add	-1,d1
	bne	mn10300_local_dcache_flush_inv_range_loop

mn10300_local_dcache_flush_inv_range_end:
	ret	[d2],4
	.size	mn10300_local_dcache_flush_inv_page,.-mn10300_local_dcache_flush_inv_page
	.size	mn10300_local_dcache_flush_inv_range,.-mn10300_local_dcache_flush_inv_range
	.size	mn10300_local_dcache_flush_inv_range2,.-mn10300_local_dcache_flush_inv_range2

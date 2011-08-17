/* MN10300 CPU cache invalidation routines, using direct tag flushing
 *
 * Copyright (C) 2011 Red Hat, Inc. All Rights Reserved.
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

	.am33_2

	.globl	debugger_local_cache_flushinv_one_icache

###############################################################################
#
# void debugger_local_cache_flushinv_one(u8 *addr)
#
# Invalidate one particular cacheline if it's in the icache
#
###############################################################################
	ALIGN
	.globl	debugger_local_cache_flushinv_one_icache
	.type	debugger_local_cache_flushinv_one_icache,@function
debugger_local_cache_flushinv_one_icache:
	movm	[d3,a2],(sp)

	mov	CHCTR,a2
	movhu	(a2),d0
	btst	CHCTR_ICEN,d0
	beq	debugger_local_cache_flushinv_one_icache_end

	mov	d0,a1
	and	L1_CACHE_TAG_MASK,a1

	# read the tags from the tag RAM, and if they indicate a matching valid
	# cache line then we invalidate that line
	mov	ICACHE_TAG(0,0),a0
	mov	a1,d0
	and	L1_CACHE_TAG_ENTRY,d0
	add	d0,a0				# starting icache tag RAM
						# access address

	and	~(L1_CACHE_DISPARITY-1),a1	# determine comparator base
	or	L1_CACHE_TAG_VALID,a1
	mov	L1_CACHE_TAG_ADDRESS|L1_CACHE_TAG_VALID,d1

	LOCAL_CLI_SAVE(d3)

	# disable the icache
	movhu	(a2),d0
	and	~CHCTR_ICEN,d0
	movhu	d0,(a2)

	# and wait for it to calm down
	setlb
	movhu	(a2),d0
	btst	CHCTR_ICBUSY,d0
	lne

	# check all the way tags for this cache entry
	mov	(a0),d0				# read the tag in the way 0 slot
	xor	a1,d0
	and	d1,d0
	beq	debugger_local_icache_kill	# jump if matched

	add	L1_CACHE_WAYDISP,a0
	mov	(a0),d0				# read the tag in the way 1 slot
	xor	a1,d0
	and	d1,d0
	beq	debugger_local_icache_kill	# jump if matched

	add	L1_CACHE_WAYDISP,a0
	mov	(a0),d0				# read the tag in the way 2 slot
	xor	a1,d0
	and	d1,d0
	beq	debugger_local_icache_kill	# jump if matched

	add	L1_CACHE_WAYDISP,a0
	mov	(a0),d0				# read the tag in the way 3 slot
	xor	a1,d0
	and	d1,d0
	bne	debugger_local_icache_finish	# jump if not matched

debugger_local_icache_kill:
	mov	d0,(a0)				# kill the tag (D0 is 0 at this point)

debugger_local_icache_finish:
	# wait for the cache to finish what it's doing
	setlb
	movhu	(a2),d0
	btst	CHCTR_ICBUSY,d0
	lne

	# and reenable it
	or	CHCTR_ICEN,d0
	movhu	d0,(a2)
	movhu	(a2),d0

	# re-enable interrupts
	LOCAL_IRQ_RESTORE(d3)

debugger_local_cache_flushinv_one_icache_end:
	ret	[d3,a2],8
	.size	debugger_local_cache_flushinv_one_icache,.-debugger_local_cache_flushinv_one_icache

#ifdef CONFIG_MN10300_DEBUGGER_CACHE_INV_BY_TAG
	.globl	debugger_local_cache_flushinv_one
	.type	debugger_local_cache_flushinv_one,@function
debugger_local_cache_flushinv_one = debugger_local_cache_flushinv_one_icache
#endif

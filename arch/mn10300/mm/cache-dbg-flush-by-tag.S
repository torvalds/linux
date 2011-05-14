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

###############################################################################
#
# void debugger_local_cache_flushinv(void)
#
# Flush the entire data cache back to RAM and invalidate the icache
#
###############################################################################
	ALIGN
	.globl	debugger_local_cache_flushinv
        .type	debugger_local_cache_flushinv,@function
debugger_local_cache_flushinv:
	#
	# firstly flush the dcache
	#
	movhu	(CHCTR),d0
	btst	CHCTR_DCEN|CHCTR_ICEN,d0
	beq	debugger_local_cache_flushinv_end

	btst	CHCTR_DCEN,d0
	beq	debugger_local_cache_flushinv_no_dcache

	# read the addresses tagged in the cache's tag RAM and attempt to flush
	# those addresses specifically
	# - we rely on the hardware to filter out invalid tag entry addresses
	mov	DCACHE_TAG(0,0),a0		# dcache tag RAM access address
	mov	DCACHE_PURGE(0,0),a1		# dcache purge request address
	mov	L1_CACHE_NWAYS*L1_CACHE_NENTRIES,e0  # total number of entries

mn10300_local_dcache_flush_loop:
	mov	(a0),d0
	and	L1_CACHE_TAG_MASK,d0
	or	L1_CACHE_TAG_VALID,d0		# retain valid entries in the
						# cache
	mov	d0,(a1)				# conditional purge

	add	L1_CACHE_BYTES,a0
	add	L1_CACHE_BYTES,a1
	add	-1,e0
	bne	mn10300_local_dcache_flush_loop

debugger_local_cache_flushinv_no_dcache:
	#
	# secondly, invalidate the icache if it is enabled
	#
	mov	CHCTR,a0
	movhu	(a0),d0
	btst	CHCTR_ICEN,d0
	beq	debugger_local_cache_flushinv_end

	invalidate_icache 1

debugger_local_cache_flushinv_end:
	ret	[],0
	.size	debugger_local_cache_flushinv,.-debugger_local_cache_flushinv

###############################################################################
#
# void debugger_local_cache_flushinv_one(u8 *addr)
#
# Invalidate one particular cacheline if it's in the icache
#
###############################################################################
	ALIGN
	.globl	debugger_local_cache_flushinv_one
	.type	debugger_local_cache_flushinv_one,@function
debugger_local_cache_flushinv_one:
	movhu	(CHCTR),d1
	btst	CHCTR_DCEN|CHCTR_ICEN,d1
	beq	debugger_local_cache_flushinv_one_end
	btst	CHCTR_DCEN,d1
	beq	debugger_local_cache_flushinv_one_icache

	# round cacheline addr down
	and	L1_CACHE_TAG_MASK,d0
	mov	d0,a1

	# determine the dcache purge control reg address
	mov	DCACHE_PURGE(0,0),a0
	and	L1_CACHE_TAG_ENTRY,d0
	add	d0,a0

	# retain valid entries in the cache
	or	L1_CACHE_TAG_VALID,a1

	# conditionally purge this line in all ways
	mov	a1,(L1_CACHE_WAYDISP*0,a0)

	# now go and do the icache
	bra	debugger_local_cache_flushinv_one_icache

debugger_local_cache_flushinv_one_end:
	ret	[],0
	.size	debugger_local_cache_flushinv_one,.-debugger_local_cache_flushinv_one

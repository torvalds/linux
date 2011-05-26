/* MN10300 CPU cache invalidation routines, using automatic purge registers
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
#include <asm/cache.h>
#include <asm/irqflags.h>
#include <asm/cacheflush.h>
#include "cache.inc"

	.am33_2

	.globl	debugger_local_cache_flushinv_one

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
	mov	d0,a1

	mov	CHCTR,a0
	movhu	(a0),d0
	btst	CHCTR_ICEN,d0
	beq	mn10300_local_icache_inv_range_reg_end

	LOCAL_CLI_SAVE(d1)

	mov	ICIVCR,a0

	# wait for the invalidator to quiesce
	setlb
	mov	(a0),d0
	btst	ICIVCR_ICIVBSY,d0
	lne

	# set the mask
	mov	~L1_CACHE_TAG_MASK,d0
	mov	d0,(ICIVMR)

	# invalidate the cache line at the given address
	and	~L1_CACHE_TAG_MASK,a1
	or	ICIVCR_ICI,a1
	mov	a1,(a0)

	# wait for the invalidator to quiesce again
	setlb
	mov	(a0),d0
	btst	ICIVCR_ICIVBSY,d0
	lne

	LOCAL_IRQ_RESTORE(d1)

mn10300_local_icache_inv_range_reg_end:
	ret	[],0
	.size	debugger_local_cache_flushinv_one,.-debugger_local_cache_flushinv_one

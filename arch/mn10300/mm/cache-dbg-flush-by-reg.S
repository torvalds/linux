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

	mov	DCPGCR,a0

	mov	epsw,d1
	and	~EPSW_IE,epsw
	or	EPSW_NMID,epsw
	nop

	btst	CHCTR_DCEN,d0
	beq	debugger_local_cache_flushinv_no_dcache

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

debugger_local_cache_flushinv_no_dcache:
	#
	# secondly, invalidate the icache if it is enabled
	#
	mov	CHCTR,a0
	movhu	(a0),d0
	btst	CHCTR_ICEN,d0
	beq	debugger_local_cache_flushinv_done

	invalidate_icache 0

debugger_local_cache_flushinv_done:
	mov	d1,epsw

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
	beq	debugger_local_cache_flushinv_one_no_dcache

	# round cacheline addr down
	and	L1_CACHE_TAG_MASK,d0
	mov	d0,a1
	mov	d0,d1

	# determine the dcache purge control reg address
	mov	DCACHE_PURGE(0,0),a0
	and	L1_CACHE_TAG_ENTRY,d0
	add	d0,a0

	# retain valid entries in the cache
	or	L1_CACHE_TAG_VALID,d1

	# conditionally purge this line in all ways
	mov	d1,(L1_CACHE_WAYDISP*0,a0)

debugger_local_cache_flushinv_no_dcache:
	#
	# now try to flush the icache
	#
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
	mov	L1_CACHE_TAG_MASK,d0
	mov	d0,(ICIVMR)

	# invalidate the cache line at the given address
	or	ICIVCR_ICI,a1
	mov	a1,(a0)

	# wait for the invalidator to quiesce again
	setlb
	mov	(a0),d0
	btst	ICIVCR_ICIVBSY,d0
	lne

	LOCAL_IRQ_RESTORE(d1)

debugger_local_cache_flushinv_one_end:
	ret	[],0
	.size	debugger_local_cache_flushinv_one,.-debugger_local_cache_flushinv_one

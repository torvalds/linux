/* MN10300 CPU cache invalidation routines
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

	.globl	debugger_local_cache_flushinv

###############################################################################
#
# void debugger_local_cache_flushinv(void)
#
# Invalidate the entire icache
#
###############################################################################
	ALIGN
	.globl	debugger_local_cache_flushinv
        .type	debugger_local_cache_flushinv,@function
debugger_local_cache_flushinv:
	#
	# we only need to invalidate the icache in this cache mode
	#
	mov	CHCTR,a0
	movhu	(a0),d0
	btst	CHCTR_ICEN,d0
	beq	debugger_local_cache_flushinv_end

	invalidate_icache 1

debugger_local_cache_flushinv_end:
	ret	[],0
	.size	debugger_local_cache_flushinv,.-debugger_local_cache_flushinv

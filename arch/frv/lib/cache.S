/* cache.S: cache managment routines
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/spr-regs.h>
#include <asm/cache.h>

        .text
        .p2align	4

###############################################################################
#
# Write back a range of dcache
# - void frv_dcache_writeback(unsigned long start [GR8], unsigned long size [GR9])
#
###############################################################################
        .globl		frv_dcache_writeback
        .type		frv_dcache_writeback,@function
frv_dcache_writeback:
	andi		gr8,~(L1_CACHE_BYTES-1),gr8

2:	dcf		@(gr8,gr0)
	addi		gr8,#L1_CACHE_BYTES,gr8
	cmp		gr9,gr8,icc0
	bhi		icc0,#2,2b

	membar
	bralr
	.size		frv_dcache_writeback, .-frv_dcache_writeback

##############################################################################
#
# Invalidate a range of dcache and icache
# - void frv_cache_invalidate(unsigned long start [GR8], unsigned long end [GR9]);
#
###############################################################################
        .globl		frv_cache_invalidate
        .type		frv_cache_invalidate,@function
frv_cache_invalidate:
	andi		gr8,~(L1_CACHE_BYTES-1),gr8

2:	dci		@(gr8,gr0)
	ici		@(gr8,gr0)
	addi		gr8,#L1_CACHE_BYTES,gr8
	cmp		gr9,gr8,icc0
	bhi		icc0,#2,2b

	membar
	bralr
	.size		frv_cache_invalidate, .-frv_cache_invalidate

##############################################################################
#
# Invalidate a range of icache
# - void frv_icache_invalidate(unsigned long start [GR8], unsigned long end [GR9]);
#
###############################################################################
        .globl		frv_icache_invalidate
        .type		frv_icache_invalidate,@function
frv_icache_invalidate:
	andi		gr8,~(L1_CACHE_BYTES-1),gr8

2:	ici		@(gr8,gr0)
	addi		gr8,#L1_CACHE_BYTES,gr8
	cmp		gr9,gr8,icc0
	bhi		icc0,#2,2b

	membar
	bralr
	.size		frv_icache_invalidate, .-frv_icache_invalidate

###############################################################################
#
# Write back and invalidate a range of dcache and icache
# - void frv_cache_wback_inv(unsigned long start [GR8], unsigned long end [GR9])
#
###############################################################################
        .globl		frv_cache_wback_inv
        .type		frv_cache_wback_inv,@function
frv_cache_wback_inv:
	andi		gr8,~(L1_CACHE_BYTES-1),gr8

2:	dcf		@(gr8,gr0)
	ici		@(gr8,gr0)
	addi		gr8,#L1_CACHE_BYTES,gr8
	cmp		gr9,gr8,icc0
	bhi		icc0,#2,2b

	membar
	bralr
	.size		frv_cache_wback_inv, .-frv_cache_wback_inv

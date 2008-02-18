/* MN10300 64-bit arithmetic right shift
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <asm/cache.h>

        .text
        .balign	L1_CACHE_BYTES

###############################################################################
#
# unsigned long long __ashrdi3(unsigned long long value [D1:D0],
#			       unsigned by [(12,SP)])
#
###############################################################################
        .globl	__ashrdi3
        .type	__ashrdi3,@function
__ashrdi3:
	mov	(12,sp),a0
	and	+63,a0
	beq	__ashrdi3_zero

	cmp	+31,a0
	bhi	__ashrdi3_32plus

	# the count is in the range 1-31
	lsr	a0,d0

	mov	+32,a1
	sub	a0,a1,a1			# a1 = 32 - count
	asl	a1,d1,a1			# get underflow from MSW -> LSW

	or_asr	a1,d0,a0,d1			# insert underflow into LSW and
						# shift the MSW
	rets

	.balign	L1_CACHE_BYTES
	# the count is in the range 32-63
__ashrdi3_32plus:
	asr	a0,d1,d0
	ext	d0				# sign-extend result through MDR
	mov	mdr,d1
__ashrdi3_zero:
	rets

	.size	__ashrdi3, .-__ashrdi3

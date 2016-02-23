/* MN10300 64-bit logical right shift
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
# unsigned long long __lshrdi3(unsigned long long value [D1:D0],
#			       unsigned by [(12,SP)])
#
###############################################################################
        .globl	__lshrdi3
        .type	__lshrdi3,@function
__lshrdi3:
	mov	(12,sp),a0
	and	+63,a0
	beq	__lshrdi3_zero

	cmp	+31,a0
	bhi	__lshrdi3_32plus

	# the count is in the range 1-31
	lsr	a0,d0

	mov	+32,a1
	sub	a0,a1,a1			# a1 = 32 - count
	asl	a1,d1,a1			# get underflow from MSW -> LSW

	or_lsr	a1,d0,a0,d1			# insert underflow into LSW and
						# shift the MSW
	rets

	.balign	L1_CACHE_BYTES
	# the count is in the range 32-63
__lshrdi3_32plus:
	lsr	a0,d1,d0
	clr	d1
__lshrdi3_zero:
	rets

	.size	__lshrdi3, .-__lshrdi3

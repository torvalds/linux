/* MN10300 64-bit arithmetic left shift
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
# unsigned long long __ashldi3(unsigned long long value [D1:D0],
#			       unsigned by [(12,SP)])
#
###############################################################################
        .globl	__ashldi3
        .type	__ashldi3,@function
__ashldi3:
	mov	(12,sp),a0
	and	+63,a0
	beq	__ashldi3_zero

	cmp	+31,a0
	bhi	__ashldi3_32plus

	# the count is in the range 1-31
	asl	a0,d1

	mov	+32,a1
	sub	a0,a1,a1			# a1 = 32 - count
	lsr	a1,d0,a1			# get overflow from LSW -> MSW

	or_asl	a1,d1,a0,d0			# insert overflow into MSW and
						# shift the LSW
	rets

	.balign	L1_CACHE_BYTES
	# the count is in the range 32-63
__ashldi3_32plus:
	asl	a0,d0,d1
	clr	d0
__ashldi3_zero:
	rets

	.size	__ashldi3, .-__ashldi3

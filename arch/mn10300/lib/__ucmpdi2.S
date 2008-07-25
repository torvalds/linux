/* __ucmpdi2.S: 64-bit unsigned compare
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */


        .text
        .p2align	4

###############################################################################
#
# int __ucmpdi2(unsigned long long a [D0:D1],
#		unsigned long long b [(SP,12),(SP,16)])
#
# - returns 0, 1, or 2 as a <, =, > b respectively.
#
###############################################################################
        .globl		__ucmpdi2
        .type		__ucmpdi2,@function
__ucmpdi2:
	mov		(12,sp),a0		# b.lsw
	mov		(16,sp),a1		# b.msw

	sub		a0,d0
	subc		a1,d1			# may clear Z, never sets it
	bne		__ucmpdi2_differ	# a.msw != b.msw
	mov		+1,d0
	rets

__ucmpdi2_differ:
	# C flag is set if LE, clear if GE
	subc		d0,d0			# -1 if LE, 0 if GE
	add		+1,d0			#  0 if LE, 1 if GE
	add		d0,d0			#  0 if LE, 2 if GE
	rets

	.size		__ucmpdi2, .-__ucmpdi2

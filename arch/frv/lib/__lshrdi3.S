/* __lshrdi3.S:	64-bit logical shift right
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
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
# unsigned long long __lshrdi3(unsigned long long value [GR8:GR9], unsigned by [GR10])
#
###############################################################################
        .globl		__lshrdi3
        .type		__lshrdi3,@function
__lshrdi3:
	andicc.p	gr10,#63,gr10,icc0
	setlos		#32,gr5
	andicc.p	gr10,#32,gr0,icc1
	beqlr		icc0,#0
	ckeq		icc1,cc4			; cc4 is true if 0<N<32

	# deal with a shift in the range 1<=N<=31
	csrl.p		gr9,gr10,gr9	,cc4,#1		; LSW >>= N
	csub		gr5,gr10,gr5	,cc4,#1		; M = 32 - N
	csll.p		gr8,gr5,gr4	,cc4,#1
	csrl		gr8,gr10,gr8	,cc4,#1		; MSW >>= N
	cor.p		gr4,gr9,gr9	,cc4,#1		; LSW |= MSW << M

	# deal with a shift in the range 32<=N<=63
	csrl		gr8,gr10,gr9	,cc4,#0		; LSW = MSW >> (N & 31 [implicit AND])
	cor.p		gr0,gr0,gr8	,cc4,#0		; MSW = 0
	bralr
	.size		__lshrdi3, .-__lshrdi3

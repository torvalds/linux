/* __ashrdi3.S:	64-bit arithmetic shift right
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
# signed long long __ashrdi3(signed long long value [GR8:GR9], unsigned by [GR10])
#
###############################################################################
        .globl		__ashrdi3
        .type		__ashrdi3,@function
__ashrdi3:
	andicc.p	gr10,#63,gr10,icc0
	setlos		#32,gr5
	andicc.p	gr10,#32,gr0,icc1
	beqlr		icc0,#0
	setlos.p	#31,gr6
	ckeq		icc1,cc4			; cc4 is true if 0<N<32

	# deal with a shift in the range 1<=N<=31
	csrl.p		gr9,gr10,gr9	,cc4,#1		; LSW >>= N
	csub		gr5,gr10,gr5	,cc4,#1		; M = 32 - N
	csll.p		gr8,gr5,gr4	,cc4,#1
	csra		gr8,gr10,gr8	,cc4,#1		; MSW >>= N
	cor.p		gr4,gr9,gr9	,cc4,#1		; LSW |= MSW << M

	# deal with a shift in the range 32<=N<=63
	csra		gr8,gr10,gr9	,cc4,#0		; LSW = MSW >> (N & 31 [implicit AND])
	csra.p		gr8,gr6,gr8	,cc4,#0		; MSW >>= 31
	bralr
	.size		__ashrdi3, .-__ashrdi3

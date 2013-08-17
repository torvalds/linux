/* __ucmpdi2.S: 64-bit unsigned compare
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
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
# int __ucmpdi2(unsigned long long a [GR8:GR9],
#		unsigned long long b [GR10:GR11])
#
# - returns 0, 1, or 2 as a <, =, > b respectively.
#
###############################################################################
        .globl		__ucmpdi2
        .type		__ucmpdi2,@function
__ucmpdi2:
	or.p		gr8,gr0,gr4
	subcc		gr8,gr10,gr0,icc0
	setlos.p	#0,gr8
	bclr		icc0,#2			; a.msw < b.msw

	setlos.p	#2,gr8
	bhilr		icc0,#0			; a.msw > b.msw

	subcc.p		gr9,gr11,gr0,icc1
	setlos		#0,gr8
	setlos.p	#2,gr9
	setlos		#1,gr7
	cknc		icc1,cc6
	cor.p		gr9,gr0,gr8,		cc6,#1
	cckls		icc1,cc4,		cc6,#1
	andcr		cc6,cc4,cc4
	cor		gr7,gr0,gr8,		cc4,#1
	bralr
	.size		__ucmpdi2, .-__ucmpdi2

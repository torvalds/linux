/* __muldi3.S:	64-bit multiply
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
# unsigned long long __muldi3(unsigned long long x [GR8:GR9],
#                             unsigned long long y [GR10:GR11])
#
###############################################################################
        .globl		__muldi3, __mulll, __umulll
        .type		__muldi3,@function
__muldi3:
__mulll:
__umulll:
	umul		gr8,gr11,gr4		; GR4:GR5 = x.MSW * y.LSW
	umul		gr9,gr10,gr6		; GR6:GR7 = x.LSW * y.MSW
	umul.p		gr9,gr11,gr8		; GR8:GR9 = x.LSW * y.LSW
	add		gr5,gr7,gr5
	add.p		gr8,gr5,gr8		; GR8 += GR5 + GR7
	bralr
	.size		__muldi3, .-__muldi3

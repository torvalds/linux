/* __negdi2.S: 64-bit negate
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
# unsigned long long __negdi2(unsigned long long value [GR8:GR9])
#
###############################################################################
        .globl		__negdi2
        .type		__negdi2,@function
__negdi2:
	subcc		gr0,gr9,gr9,icc0
	subx		gr0,gr8,gr8,icc0
	bralr
	.size		__negdi2, .-__negdi2


/* insl_ns.S: input array of 4b words from device port without byte swapping
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
# void __insl_ns(unsigned int port, void *buf, int n)
#
###############################################################################
        .globl		__insl_ns
        .type		__insl_ns,@function
__insl_ns:
	andicc.p	gr9,#3,gr0,icc0
	setlos		#4,gr4
	bne		icc0,#0,__insl_ns_misaligned
	subi		gr9,#4,gr9
0:
	ldi.p		@(gr8,#0),gr5
	subicc		gr10,#1,gr10,icc0
	stu.p		gr5,@(gr9,gr4)
	bhi		icc0,#2,0b
	bralr

__insl_ns_misaligned:
	subi.p		gr9,#1,gr9
	setlos		#1,gr4
0:
	ldi		@(gr8,#0),gr5

	srli		gr5,#24,gr6
	stbu.p		gr6,@(gr9,gr4)
	srli		gr5,#16,gr6
	stbu.p		gr6,@(gr9,gr4)
	srli		gr5,#8,gr6
	stbu.p		gr6,@(gr9,gr4)
	subicc		gr10,#1,gr10,icc0
	stbu.p		gr5,@(gr9,gr4)
	bhi		icc0,#2,0b
	bralr

	.size		__insl_ns, .-__insl_ns

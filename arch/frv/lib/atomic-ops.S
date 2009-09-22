/* atomic-ops.S: kernel atomic operations
 *
 * For an explanation of how atomic ops work in this arch, see:
 *   Documentation/frv/atomic-ops.txt
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/spr-regs.h>

	.text
	.balign 4

###############################################################################
#
# unsigned long atomic_test_and_ANDNOT_mask(unsigned long mask, volatile unsigned long *v);
#
###############################################################################
	.globl		atomic_test_and_ANDNOT_mask
        .type		atomic_test_and_ANDNOT_mask,@function
atomic_test_and_ANDNOT_mask:
	not.p		gr8,gr10
0:
	orcc		gr0,gr0,gr0,icc3		/* set ICC3.Z */
	ckeq		icc3,cc7
	ld.p		@(gr9,gr0),gr8			/* LD.P/ORCR must be atomic */
	orcr		cc7,cc7,cc3			/* set CC3 to true */
	and		gr8,gr10,gr11
	cst.p		gr11,@(gr9,gr0)		,cc3,#1
	corcc		gr29,gr29,gr0		,cc3,#1	/* clear ICC3.Z if store happens */
	beq		icc3,#0,0b
	bralr

	.size		atomic_test_and_ANDNOT_mask, .-atomic_test_and_ANDNOT_mask

###############################################################################
#
# unsigned long atomic_test_and_OR_mask(unsigned long mask, volatile unsigned long *v);
#
###############################################################################
	.globl		atomic_test_and_OR_mask
        .type		atomic_test_and_OR_mask,@function
atomic_test_and_OR_mask:
	or.p		gr8,gr8,gr10
0:
	orcc		gr0,gr0,gr0,icc3		/* set ICC3.Z */
	ckeq		icc3,cc7
	ld.p		@(gr9,gr0),gr8			/* LD.P/ORCR must be atomic */
	orcr		cc7,cc7,cc3			/* set CC3 to true */
	or		gr8,gr10,gr11
	cst.p		gr11,@(gr9,gr0)		,cc3,#1
	corcc		gr29,gr29,gr0		,cc3,#1	/* clear ICC3.Z if store happens */
	beq		icc3,#0,0b
	bralr

	.size		atomic_test_and_OR_mask, .-atomic_test_and_OR_mask

###############################################################################
#
# unsigned long atomic_test_and_XOR_mask(unsigned long mask, volatile unsigned long *v);
#
###############################################################################
	.globl		atomic_test_and_XOR_mask
        .type		atomic_test_and_XOR_mask,@function
atomic_test_and_XOR_mask:
	or.p		gr8,gr8,gr10
0:
	orcc		gr0,gr0,gr0,icc3		/* set ICC3.Z */
	ckeq		icc3,cc7
	ld.p		@(gr9,gr0),gr8			/* LD.P/ORCR must be atomic */
	orcr		cc7,cc7,cc3			/* set CC3 to true */
	xor		gr8,gr10,gr11
	cst.p		gr11,@(gr9,gr0)		,cc3,#1
	corcc		gr29,gr29,gr0		,cc3,#1	/* clear ICC3.Z if store happens */
	beq		icc3,#0,0b
	bralr

	.size		atomic_test_and_XOR_mask, .-atomic_test_and_XOR_mask

###############################################################################
#
# int atomic_add_return(int i, atomic_t *v)
#
###############################################################################
	.globl		atomic_add_return
        .type		atomic_add_return,@function
atomic_add_return:
	or.p		gr8,gr8,gr10
0:
	orcc		gr0,gr0,gr0,icc3		/* set ICC3.Z */
	ckeq		icc3,cc7
	ld.p		@(gr9,gr0),gr8			/* LD.P/ORCR must be atomic */
	orcr		cc7,cc7,cc3			/* set CC3 to true */
	add		gr8,gr10,gr8
	cst.p		gr8,@(gr9,gr0)		,cc3,#1
	corcc		gr29,gr29,gr0		,cc3,#1	/* clear ICC3.Z if store happens */
	beq		icc3,#0,0b
	bralr

	.size		atomic_add_return, .-atomic_add_return

###############################################################################
#
# int atomic_sub_return(int i, atomic_t *v)
#
###############################################################################
	.globl		atomic_sub_return
        .type		atomic_sub_return,@function
atomic_sub_return:
	or.p		gr8,gr8,gr10
0:
	orcc		gr0,gr0,gr0,icc3		/* set ICC3.Z */
	ckeq		icc3,cc7
	ld.p		@(gr9,gr0),gr8			/* LD.P/ORCR must be atomic */
	orcr		cc7,cc7,cc3			/* set CC3 to true */
	sub		gr8,gr10,gr8
	cst.p		gr8,@(gr9,gr0)		,cc3,#1
	corcc		gr29,gr29,gr0		,cc3,#1	/* clear ICC3.Z if store happens */
	beq		icc3,#0,0b
	bralr

	.size		atomic_sub_return, .-atomic_sub_return

###############################################################################
#
# uint32_t __xchg_32(uint32_t i, uint32_t *v)
#
###############################################################################
	.globl		__xchg_32
        .type		__xchg_32,@function
__xchg_32:
	or.p		gr8,gr8,gr10
0:
	orcc		gr0,gr0,gr0,icc3		/* set ICC3.Z */
	ckeq		icc3,cc7
	ld.p		@(gr9,gr0),gr8			/* LD.P/ORCR must be atomic */
	orcr		cc7,cc7,cc3			/* set CC3 to true */
	cst.p		gr10,@(gr9,gr0)		,cc3,#1
	corcc		gr29,gr29,gr0		,cc3,#1	/* clear ICC3.Z if store happens */
	beq		icc3,#0,0b
	bralr

	.size		__xchg_32, .-__xchg_32

###############################################################################
#
# uint32_t __cmpxchg_32(uint32_t *v, uint32_t test, uint32_t new)
#
###############################################################################
	.globl		__cmpxchg_32
        .type		__cmpxchg_32,@function
__cmpxchg_32:
	or.p		gr8,gr8,gr11
0:
	orcc		gr0,gr0,gr0,icc3
	ckeq		icc3,cc7
	ld.p		@(gr11,gr0),gr8
	orcr		cc7,cc7,cc3
	subcc		gr8,gr9,gr7,icc0
	bnelr		icc0,#0
	cst.p		gr10,@(gr11,gr0)	,cc3,#1
	corcc		gr29,gr29,gr0		,cc3,#1
	beq		icc3,#0,0b
	bralr

	.size		__cmpxchg_32, .-__cmpxchg_32

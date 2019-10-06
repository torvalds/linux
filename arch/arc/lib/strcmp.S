/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

/* This is optimized primarily for the ARC700.
   It would be possible to speed up the loops by one cycle / word
   respective one cycle / byte by forcing double source 1 alignment, unrolling
   by a factor of two, and speculatively loading the second word / byte of
   source 1; however, that would increase the overhead for loop setup / finish,
   and strcmp might often terminate early.  */

#include <linux/linkage.h>

ENTRY_CFI(strcmp)
	or	r2,r0,r1
	bmsk_s	r2,r2,1
	brne	r2,0,.Lcharloop
	mov_s	r12,0x01010101
	ror	r5,r12
.Lwordloop:
	ld.ab	r2,[r0,4]
	ld.ab	r3,[r1,4]
	nop_s
	sub	r4,r2,r12
	bic	r4,r4,r2
	and	r4,r4,r5
	brne	r4,0,.Lfound0
	breq	r2,r3,.Lwordloop
#ifdef	__LITTLE_ENDIAN__
	xor	r0,r2,r3	; mask for difference
	sub_s	r1,r0,1
	bic_s	r0,r0,r1	; mask for least significant difference bit
	sub	r1,r5,r0
	xor	r0,r5,r1	; mask for least significant difference byte
	and_s	r2,r2,r0
	and_s	r3,r3,r0
#endif /* LITTLE ENDIAN */
	cmp_s	r2,r3
	mov_s	r0,1
	j_s.d	[blink]
	bset.lo	r0,r0,31

	.balign	4
#ifdef __LITTLE_ENDIAN__
.Lfound0:
	xor	r0,r2,r3	; mask for difference
	or	r0,r0,r4	; or in zero indicator
	sub_s	r1,r0,1
	bic_s	r0,r0,r1	; mask for least significant difference bit
	sub	r1,r5,r0
	xor	r0,r5,r1	; mask for least significant difference byte
	and_s	r2,r2,r0
	and_s	r3,r3,r0
	sub.f	r0,r2,r3
	mov.hi	r0,1
	j_s.d	[blink]
	bset.lo	r0,r0,31
#else /* BIG ENDIAN */
	/* The zero-detection above can mis-detect 0x01 bytes as zeroes
	   because of carry-propagateion from a lower significant zero byte.
	   We can compensate for this by checking that bit0 is zero.
	   This compensation is not necessary in the step where we
	   get a low estimate for r2, because in any affected bytes
	   we already have 0x00 or 0x01, which will remain unchanged
	   when bit 7 is cleared.  */
	.balign	4
.Lfound0:
	lsr	r0,r4,8
	lsr_s	r1,r2
	bic_s	r2,r2,r0	; get low estimate for r2 and get ...
	bic_s	r0,r0,r1	; <this is the adjusted mask for zeros>
	or_s	r3,r3,r0	; ... high estimate r3 so that r2 > r3 will ...
	cmp_s	r3,r2		; ... be independent of trailing garbage
	or_s	r2,r2,r0	; likewise for r3 > r2
	bic_s	r3,r3,r0
	rlc	r0,0		; r0 := r2 > r3 ? 1 : 0
	cmp_s	r2,r3
	j_s.d	[blink]
	bset.lo	r0,r0,31
#endif /* ENDIAN */

	.balign	4
.Lcharloop:
	ldb.ab	r2,[r0,1]
	ldb.ab	r3,[r1,1]
	nop_s
	breq	r2,0,.Lcmpend
	breq	r2,r3,.Lcharloop
.Lcmpend:
	j_s.d	[blink]
	sub	r0,r2,r3
END_CFI(strcmp)

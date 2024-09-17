/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/linkage.h>

ENTRY_CFI(strlen)
	or	r3,r0,7
	ld	r2,[r3,-7]
	ld.a	r6,[r3,-3]
	mov	r4,0x01010101
	; uses long immediate
#ifdef __LITTLE_ENDIAN__
	asl_s	r1,r0,3
	btst_s	r0,2
	asl	r7,r4,r1
	ror	r5,r4
	sub	r1,r2,r7
	bic_s	r1,r1,r2
	mov.eq	r7,r4
	sub	r12,r6,r7
	bic	r12,r12,r6
	or.eq	r12,r12,r1
	and	r12,r12,r5
	brne	r12,0,.Learly_end
#else /* BIG ENDIAN */
	ror	r5,r4
	btst_s	r0,2
	mov_s	r1,31
	sub3	r7,r1,r0
	sub	r1,r2,r4
	bic_s	r1,r1,r2
	bmsk	r1,r1,r7
	sub	r12,r6,r4
	bic	r12,r12,r6
	bmsk.ne	r12,r12,r7
	or.eq	r12,r12,r1
	and	r12,r12,r5
	brne	r12,0,.Learly_end
#endif /* ENDIAN */

.Loop:
	ld_s	r2,[r3,4]
	ld.a	r6,[r3,8]
	; stall for load result
	sub	r1,r2,r4
	bic_s	r1,r1,r2
	sub	r12,r6,r4
	bic	r12,r12,r6
	or	r12,r12,r1
	and	r12,r12,r5
	breq r12,0,.Loop
.Lend:
	and.f	r1,r1,r5
	sub.ne	r3,r3,4
	mov.eq	r1,r12
#ifdef __LITTLE_ENDIAN__
	sub_s	r2,r1,1
	bic_s	r2,r2,r1
	norm	r1,r2
	sub_s	r0,r0,3
	lsr_s	r1,r1,3
	sub	    r0,r3,r0
	j_s.d	[blink]
	sub	    r0,r0,r1
#else /* BIG ENDIAN */
	lsr_s	r1,r1,7
	mov.eq	r2,r6
	bic_s	r1,r1,r2
	norm	r1,r1
	sub	    r0,r3,r0
	lsr_s	r1,r1,3
	j_s.d	[blink]
	add	    r0,r0,r1
#endif /* ENDIAN */
.Learly_end:
	b.d	.Lend
	sub_s.ne r1,r1,r1
END_CFI(strlen)

/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* ARC700 has a relatively long pipeline and branch prediction, so we want
   to avoid branches that are hard to predict.  On the other hand, the
   presence of the norm instruction makes it easier to operate on whole
   words branch-free.  */

#include <linux/linkage.h>

ENTRY_CFI(strchr)
	extb_s	r1,r1
	asl	r5,r1,8
	bmsk	r2,r0,1
	or	r5,r5,r1
	mov_s	r3,0x01010101
	breq.d	r2,r0,.Laligned
	asl	r4,r5,16
	sub_s	r0,r0,r2
	asl	r7,r2,3
	ld_s	r2,[r0]
#ifdef __LITTLE_ENDIAN__
	asl	r7,r3,r7
#else
	lsr	r7,r3,r7
#endif
	or	r5,r5,r4
	ror	r4,r3
	sub	r12,r2,r7
	bic_s	r12,r12,r2
	and	r12,r12,r4
	brne.d	r12,0,.Lfound0_ua
	xor	r6,r2,r5
	ld.a	r2,[r0,4]
	sub	r12,r6,r7
	bic	r12,r12,r6
#ifdef __LITTLE_ENDIAN__
	and	r7,r12,r4
	breq	r7,0,.Loop ; For speed, we want this branch to be unaligned.
	b	.Lfound_char ; Likewise this one.
#else
	and	r12,r12,r4
	breq	r12,0,.Loop ; For speed, we want this branch to be unaligned.
	lsr_s	r12,r12,7
	bic 	r2,r7,r6
	b.d	.Lfound_char_b
	and_s	r2,r2,r12
#endif
; /* We require this code address to be unaligned for speed...  */
.Laligned:
	ld_s	r2,[r0]
	or	r5,r5,r4
	ror	r4,r3
; /* ... so that this code address is aligned, for itself and ...  */
.Loop:
	sub	r12,r2,r3
	bic_s	r12,r12,r2
	and	r12,r12,r4
	brne.d	r12,0,.Lfound0
	xor	r6,r2,r5
	ld.a	r2,[r0,4]
	sub	r12,r6,r3
	bic	r12,r12,r6
	and	r7,r12,r4
	breq	r7,0,.Loop /* ... so that this branch is unaligned.  */
	; Found searched-for character.  r0 has already advanced to next word.
#ifdef __LITTLE_ENDIAN__
/* We only need the information about the first matching byte
   (i.e. the least significant matching byte) to be exact,
   hence there is no problem with carry effects.  */
.Lfound_char:
	sub	r3,r7,1
	bic	r3,r3,r7
	norm	r2,r3
	sub_s	r0,r0,1
	asr_s	r2,r2,3
	j.d	[blink]
	sub_s	r0,r0,r2

	.balign	4
.Lfound0_ua:
	mov	r3,r7
.Lfound0:
	sub	r3,r6,r3
	bic	r3,r3,r6
	and	r2,r3,r4
	or_s	r12,r12,r2
	sub_s	r3,r12,1
	bic_s	r3,r3,r12
	norm	r3,r3
	add_s	r0,r0,3
	asr_s	r12,r3,3
	asl.f	0,r2,r3
	sub_s	r0,r0,r12
	j_s.d	[blink]
	mov.pl	r0,0
#else /* BIG ENDIAN */
.Lfound_char:
	lsr	r7,r7,7

	bic	r2,r7,r6
.Lfound_char_b:
	norm	r2,r2
	sub_s	r0,r0,4
	asr_s	r2,r2,3
	j.d	[blink]
	add_s	r0,r0,r2

.Lfound0_ua:
	mov_s	r3,r7
.Lfound0:
	asl_s	r2,r2,7
	or	r7,r6,r4
	bic_s	r12,r12,r2
	sub	r2,r7,r3
	or	r2,r2,r6
	bic	r12,r2,r12
	bic.f	r3,r4,r12
	norm	r3,r3

	add.pl	r3,r3,1
	asr_s	r12,r3,3
	asl.f	0,r2,r3
	add_s	r0,r0,r12
	j_s.d	[blink]
	mov.mi	r0,0
#endif /* ENDIAN */
END_CFI(strchr)

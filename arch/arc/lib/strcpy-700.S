/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* If dst and src are 4 byte aligned, copy 8 bytes at a time.
   If the src is 4, but not 8 byte aligned, we first read 4 bytes to get
   it 8 byte aligned.  Thus, we can do a little read-ahead, without
   dereferencing a cache line that we should not touch.
   Note that short and long instructions have been scheduled to avoid
   branch stalls.
   The beq_s to r3z could be made unaligned & long to avoid a stall
   there, but the it is not likely to be taken often, and it
   would also be likey to cost an unaligned mispredict at the next call.  */

#include <asm/linkage.h>

ARC_ENTRY strcpy
	or	r2,r0,r1
	bmsk_s	r2,r2,1
	brne.d	r2,0,charloop
	mov_s	r10,r0
	ld_s	r3,[r1,0]
	mov	r8,0x01010101
	bbit0.d	r1,2,loop_start
	ror	r12,r8
	sub	r2,r3,r8
	bic_s	r2,r2,r3
	tst_s	r2,r12
	bne	r3z
	mov_s	r4,r3
	.balign 4
loop:
	ld.a	r3,[r1,4]
	st.ab	r4,[r10,4]
loop_start:
	ld.a	r4,[r1,4]
	sub	r2,r3,r8
	bic_s	r2,r2,r3
	tst_s	r2,r12
	bne_s	r3z
	st.ab	r3,[r10,4]
	sub	r2,r4,r8
	bic	r2,r2,r4
	tst	r2,r12
	beq	loop
	mov_s	r3,r4
#ifdef __LITTLE_ENDIAN__
r3z:	bmsk.f	r1,r3,7
	lsr_s	r3,r3,8
#else
r3z:	lsr.f	r1,r3,24
	asl_s	r3,r3,8
#endif
	bne.d	r3z
	stb.ab	r1,[r10,1]
	j_s	[blink]

	.balign	4
charloop:
	ldb.ab	r3,[r1,1]


	brne.d	r3,0,charloop
	stb.ab	r3,[r10,1]
	j	[blink]
ARC_EXIT strcpy

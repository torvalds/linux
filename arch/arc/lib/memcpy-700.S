/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>

ENTRY(memcpy)
	or	r3,r0,r1
	asl_s	r3,r3,30
	mov_s	r5,r0
	brls.d	r2,r3,.Lcopy_bytewise
	sub.f	r3,r2,1
	ld_s	r12,[r1,0]
	asr.f	lp_count,r3,3
	bbit0.d	r3,2,.Lnox4
	bmsk_s	r2,r2,1
	st.ab	r12,[r5,4]
	ld.a	r12,[r1,4]
.Lnox4:
	lppnz	.Lendloop
	ld_s	r3,[r1,4]
	st.ab	r12,[r5,4]
	ld.a	r12,[r1,8]
	st.ab	r3,[r5,4]
.Lendloop:
	breq	r2,0,.Last_store
	ld	r3,[r5,0]
#ifdef __LITTLE_ENDIAN__
	add3	r2,-1,r2
	; uses long immediate
	xor_s	r12,r12,r3
	bmsk	r12,r12,r2
    xor_s	r12,r12,r3
#else /* BIG ENDIAN */
	sub3	r2,31,r2
	; uses long immediate
        xor_s	r3,r3,r12
        bmsk	r3,r3,r2
        xor_s	r12,r12,r3
#endif /* ENDIAN */
.Last_store:
	j_s.d	[blink]
	st	r12,[r5,0]

	.balign	4
.Lcopy_bytewise:
	jcs	[blink]
	ldb_s	r12,[r1,0]
	lsr.f	lp_count,r3
	bhs_s	.Lnox1
	stb.ab	r12,[r5,1]
	ldb.a	r12,[r1,1]
.Lnox1:
	lppnz	.Lendbloop
	ldb_s	r3,[r1,1]
	stb.ab	r12,[r5,1]
	ldb.a	r12,[r1,2]
	stb.ab	r3,[r5,1]
.Lendbloop:
	j_s.d	[blink]
	stb	r12,[r5,0]
END(memcpy)

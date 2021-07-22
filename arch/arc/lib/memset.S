/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/linkage.h>

#define SMALL	7 /* Must be at least 6 to deal with alignment/loop issues.  */

ENTRY_CFI(memset)
	mov_s	r4,r0
	or	r12,r0,r2
	bmsk.f	r12,r12,1
	extb_s	r1,r1
	asl	r3,r1,8
	beq.d	.Laligned
	or_s	r1,r1,r3
	brls	r2,SMALL,.Ltiny
	add	r3,r2,r0
	stb	r1,[r3,-1]
	bclr_s	r3,r3,0
	stw	r1,[r3,-2]
	bmsk.f	r12,r0,1
	add_s	r2,r2,r12
	sub.ne	r2,r2,4
	stb.ab	r1,[r4,1]
	and	r4,r4,-2
	stw.ab	r1,[r4,2]
	and	r4,r4,-4
.Laligned:	; This code address should be aligned for speed.
	asl	r3,r1,16
	lsr.f	lp_count,r2,2
	or_s	r1,r1,r3
	lpne	.Loop_end
	st.ab	r1,[r4,4]
.Loop_end:
	j_s	[blink]

	.balign	4
.Ltiny:
	mov.f	lp_count,r2
	lpne	.Ltiny_end
	stb.ab	r1,[r4,1]
.Ltiny_end:
	j_s	[blink]
END_CFI(memset)

; memzero: @r0 = mem, @r1 = size_t
; memset:  @r0 = mem, @r1 = char, @r2 = size_t

ENTRY_CFI(memzero)
    ; adjust bzero args to memset args
    mov r2, r1
    mov r1, 0
    b  memset    ;tail call so need to tinker with blink
END_CFI(memzero)

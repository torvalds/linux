/* SPDX-License-Identifier: GPL-2.0 */
/*
 * __clear_user_page, __clear_user, clear_page implementation of SuperH
 *
 * Copyright (C) 2001  Kaz Kojima
 * Copyright (C) 2001, 2002  Niibe Yutaka
 * Copyright (C) 2006  Paul Mundt
 */
#include <linux/linkage.h>
#include <asm/page.h>

ENTRY(__clear_user)
	!
	mov	#0, r0
	mov	#0xffffffe0, r1
	!
	! r4..(r4+31)&~32 	   -------- not aligned	[ Area 0 ]
	! (r4+31)&~32..(r4+r5)&~32 -------- aligned	[ Area 1 ]
	! (r4+r5)&~32..r4+r5       -------- not aligned	[ Area 2 ]
	!
	! Clear area 0
	mov	r4, r2
	!
	tst	r1, r5		! length < 32
	bt	.Larea2		! skip to remainder
	!
	add	#31, r2
	and	r1, r2
	cmp/eq	r4, r2
	bt	.Larea1
	mov	r2, r3
	sub	r4, r3
	mov	r3, r7
	mov	r4, r2
	!
.L0:	dt	r3
0:	mov.b	r0, @r2
	bf/s	.L0
	 add	#1, r2
	!
	sub	r7, r5
	mov	r2, r4
.Larea1:
	mov	r4, r3
	add	r5, r3
	and	r1, r3
	cmp/hi	r2, r3
	bf	.Larea2
	!
	! Clear area 1
#if defined(CONFIG_CPU_SH4)
1:	movca.l	r0, @r2
#else
1:	mov.l	r0, @r2
#endif
	add	#4, r2
2:	mov.l	r0, @r2
	add	#4, r2
3:	mov.l	r0, @r2
	add	#4, r2
4:	mov.l	r0, @r2
	add	#4, r2
5:	mov.l	r0, @r2
	add	#4, r2
6:	mov.l	r0, @r2
	add	#4, r2
7:	mov.l	r0, @r2
	add	#4, r2
8:	mov.l	r0, @r2
	add	#4, r2
	cmp/hi	r2, r3
	bt/s	1b
	 nop
	!
	! Clear area 2
.Larea2:
	mov	r4, r3
	add	r5, r3
	cmp/hs	r3, r2
	bt/s	.Ldone
	 sub	r2, r3
.L2:	dt	r3
9:	mov.b	r0, @r2
	bf/s	.L2
	 add	#1, r2
	!
.Ldone:	rts
	 mov	#0, r0	! return 0 as normal return

	! return the number of bytes remained
.Lbad_clear_user:
	mov	r4, r0
	add	r5, r0
	rts
	 sub	r2, r0

.section __ex_table,"a"
	.align 2
	.long	0b, .Lbad_clear_user
	.long	1b, .Lbad_clear_user
	.long	2b, .Lbad_clear_user
	.long	3b, .Lbad_clear_user
	.long	4b, .Lbad_clear_user
	.long	5b, .Lbad_clear_user
	.long	6b, .Lbad_clear_user
	.long	7b, .Lbad_clear_user
	.long	8b, .Lbad_clear_user
	.long	9b, .Lbad_clear_user
.previous

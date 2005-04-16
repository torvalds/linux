/* $Id: clear_page.S,v 1.13 2003/08/25 17:03:10 lethal Exp $
 *
 * __clear_user_page, __clear_user, clear_page implementation of SuperH
 *
 * Copyright (C) 2001  Kaz Kojima
 * Copyright (C) 2001, 2002  Niibe Yutaka
 *
 */
#include <linux/config.h>
#include <linux/linkage.h>

/*
 * clear_page_slow
 * @to: P1 address
 *
 * void clear_page_slow(void *to)
 */

/*
 * r0 --- scratch
 * r4 --- to
 * r5 --- to + 4096
 */
ENTRY(clear_page_slow)
	mov	r4,r5
	mov.w	.Llimit,r0
	add	r0,r5
	mov	#0,r0
	!
1:
#if defined(CONFIG_CPU_SH3)
	mov.l	r0,@r4
#elif defined(CONFIG_CPU_SH4)
	movca.l	r0,@r4
	mov	r4,r1
#endif
	add	#32,r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
#if defined(CONFIG_CPU_SH4)
	ocbwb	@r1
#endif
	cmp/eq	r5,r4
	bf/s	1b
	 add	#28,r4
	!
	rts
	 nop
.Llimit:	.word	(4096-28)

ENTRY(__clear_user)
	!
	mov	#0, r0
	mov	#0xe0, r1	! 0xffffffe0
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

#if defined(CONFIG_CPU_SH4)
/*
 * __clear_user_page
 * @to: P3 address (with same color)
 * @orig_to: P1 address
 *
 * void __clear_user_page(void *to, void *orig_to)
 */

/*
 * r0 --- scratch 
 * r4 --- to
 * r5 --- orig_to
 * r6 --- to + 4096
 */
ENTRY(__clear_user_page)
	mov.w	.L4096,r0
	mov	r4,r6
	add	r0,r6
	mov	#0,r0
	!
1:	ocbi	@r5
	add	#32,r5
	movca.l	r0,@r4
	mov	r4,r1
	add	#32,r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	add	#28,r4
	cmp/eq	r6,r4
	bf/s	1b
	 ocbwb	@r1
	!
	rts
	 nop
.L4096:	.word	4096

ENTRY(__flush_cache_4096)
	mov.l	1f,r3
	add	r6,r3
	mov	r4,r0
	mov	#64,r2
	shll	r2
	mov	#64,r6
	jmp	@r3
	 mov	#96,r7
	.align	2
1:	.long	2f
2:
	.rept	32
	mov.l	r5,@r0
	mov.l	r5,@(32,r0)
	mov.l	r5,@(r0,r6)
	mov.l	r5,@(r0,r7)
	add	r2,r5
	add	r2,r0
	.endr
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	rts
	 nop

ENTRY(__flush_dcache_all)
	mov.l	2f,r0
	mov.l	3f,r4
	and	r0,r4		! r4 = (unsigned long)&empty_zero_page[0] & ~0xffffc000
	stc	sr,r1		! save SR
	mov.l	4f,r2
	or	r1,r2
	mov	#32,r3
	shll2	r3
1:
	ldc	r2,sr		! set BL bit
	movca.l	r0,@r4
	ocbi	@r4
	add	#32,r4
	movca.l	r0,@r4
	ocbi	@r4
	add	#32,r4
	movca.l	r0,@r4
	ocbi	@r4
	add	#32,r4
	movca.l	r0,@r4
	ocbi	@r4
	ldc	r1,sr		! restore SR
	dt	r3
	bf/s	1b
	 add	#32,r4

	rts
	 nop
	.align	2
2:	.long	0xffffc000
3:	.long	empty_zero_page
4:	.long	0x10000000	! BL bit

/* __flush_cache_4096_all(unsigned long addr) */
ENTRY(__flush_cache_4096_all)
	mov.l	2f,r0
	mov.l	3f,r2
	and	r0,r2
	or	r2,r4		! r4 = addr | (unsigned long)&empty_zero_page[0] & ~0x3fff
	stc	sr,r1		! save SR
	mov.l	4f,r2
	or	r1,r2
	mov	#32,r3
1:
	ldc	r2,sr		! set BL bit
	movca.l	r0,@r4
	ocbi	@r4
	add	#32,r4
	movca.l	r0,@r4
	ocbi	@r4
	add	#32,r4
	movca.l	r0,@r4
	ocbi	@r4
	add	#32,r4
	movca.l	r0,@r4
	ocbi	@r4
	ldc	r1,sr		! restore SR
	dt	r3
	bf/s	1b
	 add	#32,r4

	rts
	 nop
	.align	2
2:	.long	0xffffc000
3:	.long	empty_zero_page
4:	.long	0x10000000	! BL bit
#endif

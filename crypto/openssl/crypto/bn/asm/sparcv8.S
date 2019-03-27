.ident	"sparcv8.s, Version 1.4"
.ident	"SPARC v8 ISA artwork by Andy Polyakov <appro@openssl.org>"

/*
 * ====================================================================
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 * ====================================================================
 */

/*
 * This is my modest contribution to OpenSSL project (see
 * http://www.openssl.org/ for more information about it) and is
 * a drop-in SuperSPARC ISA replacement for crypto/bn/bn_asm.c
 * module. For updates see http://fy.chalmers.se/~appro/hpe/.
 *
 * See bn_asm.sparc.v8plus.S for more details.
 */

/*
 * Revision history.
 *
 * 1.1	- new loop unrolling model(*);
 * 1.2	- made gas friendly;
 * 1.3	- fixed problem with /usr/ccs/lib/cpp;
 * 1.4	- some retunes;
 *
 * (*)	see bn_asm.sparc.v8plus.S for details
 */

.section	".text",#alloc,#execinstr
.file		"bn_asm.sparc.v8.S"

.align	32

.global bn_mul_add_words
/*
 * BN_ULONG bn_mul_add_words(rp,ap,num,w)
 * BN_ULONG *rp,*ap;
 * int num;
 * BN_ULONG w;
 */
bn_mul_add_words:
	cmp	%o2,0
	bg,a	.L_bn_mul_add_words_proceed
	ld	[%o1],%g2
	retl
	clr	%o0

.L_bn_mul_add_words_proceed:
	andcc	%o2,-4,%g0
	bz	.L_bn_mul_add_words_tail
	clr	%o5

.L_bn_mul_add_words_loop:
	ld	[%o0],%o4
	ld	[%o1+4],%g3
	umul	%o3,%g2,%g2
	rd	%y,%g1
	addcc	%o4,%o5,%o4
	addx	%g1,0,%g1
	addcc	%o4,%g2,%o4
	st	%o4,[%o0]
	addx	%g1,0,%o5

	ld	[%o0+4],%o4
	ld	[%o1+8],%g2
	umul	%o3,%g3,%g3
	dec	4,%o2
	rd	%y,%g1
	addcc	%o4,%o5,%o4
	addx	%g1,0,%g1
	addcc	%o4,%g3,%o4
	st	%o4,[%o0+4]
	addx	%g1,0,%o5

	ld	[%o0+8],%o4
	ld	[%o1+12],%g3
	umul	%o3,%g2,%g2
	inc	16,%o1
	rd	%y,%g1
	addcc	%o4,%o5,%o4
	addx	%g1,0,%g1
	addcc	%o4,%g2,%o4
	st	%o4,[%o0+8]
	addx	%g1,0,%o5

	ld	[%o0+12],%o4
	umul	%o3,%g3,%g3
	inc	16,%o0
	rd	%y,%g1
	addcc	%o4,%o5,%o4
	addx	%g1,0,%g1
	addcc	%o4,%g3,%o4
	st	%o4,[%o0-4]
	addx	%g1,0,%o5
	andcc	%o2,-4,%g0
	bnz,a	.L_bn_mul_add_words_loop
	ld	[%o1],%g2

	tst	%o2
	bnz,a	.L_bn_mul_add_words_tail
	ld	[%o1],%g2
.L_bn_mul_add_words_return:
	retl
	mov	%o5,%o0
	nop

.L_bn_mul_add_words_tail:
	ld	[%o0],%o4
	umul	%o3,%g2,%g2
	addcc	%o4,%o5,%o4
	rd	%y,%g1
	addx	%g1,0,%g1
	addcc	%o4,%g2,%o4
	addx	%g1,0,%o5
	deccc	%o2
	bz	.L_bn_mul_add_words_return
	st	%o4,[%o0]

	ld	[%o1+4],%g2
	ld	[%o0+4],%o4
	umul	%o3,%g2,%g2
	rd	%y,%g1
	addcc	%o4,%o5,%o4
	addx	%g1,0,%g1
	addcc	%o4,%g2,%o4
	addx	%g1,0,%o5
	deccc	%o2
	bz	.L_bn_mul_add_words_return
	st	%o4,[%o0+4]

	ld	[%o1+8],%g2
	ld	[%o0+8],%o4
	umul	%o3,%g2,%g2
	rd	%y,%g1
	addcc	%o4,%o5,%o4
	addx	%g1,0,%g1
	addcc	%o4,%g2,%o4
	st	%o4,[%o0+8]
	retl
	addx	%g1,0,%o0

.type	bn_mul_add_words,#function
.size	bn_mul_add_words,(.-bn_mul_add_words)

.align	32

.global bn_mul_words
/*
 * BN_ULONG bn_mul_words(rp,ap,num,w)
 * BN_ULONG *rp,*ap;
 * int num;
 * BN_ULONG w;
 */
bn_mul_words:
	cmp	%o2,0
	bg,a	.L_bn_mul_words_proceed
	ld	[%o1],%g2
	retl
	clr	%o0

.L_bn_mul_words_proceed:
	andcc	%o2,-4,%g0
	bz	.L_bn_mul_words_tail
	clr	%o5

.L_bn_mul_words_loop:
	ld	[%o1+4],%g3
	umul	%o3,%g2,%g2
	addcc	%g2,%o5,%g2
	rd	%y,%g1
	addx	%g1,0,%o5
	st	%g2,[%o0]

	ld	[%o1+8],%g2
	umul	%o3,%g3,%g3
	addcc	%g3,%o5,%g3
	rd	%y,%g1
	dec	4,%o2
	addx	%g1,0,%o5
	st	%g3,[%o0+4]

	ld	[%o1+12],%g3
	umul	%o3,%g2,%g2
	addcc	%g2,%o5,%g2
	rd	%y,%g1
	inc	16,%o1
	st	%g2,[%o0+8]
	addx	%g1,0,%o5

	umul	%o3,%g3,%g3
	addcc	%g3,%o5,%g3
	rd	%y,%g1
	inc	16,%o0
	addx	%g1,0,%o5
	st	%g3,[%o0-4]
	andcc	%o2,-4,%g0
	nop
	bnz,a	.L_bn_mul_words_loop
	ld	[%o1],%g2

	tst	%o2
	bnz,a	.L_bn_mul_words_tail
	ld	[%o1],%g2
.L_bn_mul_words_return:
	retl
	mov	%o5,%o0
	nop

.L_bn_mul_words_tail:
	umul	%o3,%g2,%g2
	addcc	%g2,%o5,%g2
	rd	%y,%g1
	addx	%g1,0,%o5
	deccc	%o2
	bz	.L_bn_mul_words_return
	st	%g2,[%o0]
	nop

	ld	[%o1+4],%g2
	umul	%o3,%g2,%g2
	addcc	%g2,%o5,%g2
	rd	%y,%g1
	addx	%g1,0,%o5
	deccc	%o2
	bz	.L_bn_mul_words_return
	st	%g2,[%o0+4]

	ld	[%o1+8],%g2
	umul	%o3,%g2,%g2
	addcc	%g2,%o5,%g2
	rd	%y,%g1
	st	%g2,[%o0+8]
	retl
	addx	%g1,0,%o0

.type	bn_mul_words,#function
.size	bn_mul_words,(.-bn_mul_words)

.align  32
.global	bn_sqr_words
/*
 * void bn_sqr_words(r,a,n)
 * BN_ULONG *r,*a;
 * int n;
 */
bn_sqr_words:
	cmp	%o2,0
	bg,a	.L_bn_sqr_words_proceed
	ld	[%o1],%g2
	retl
	clr	%o0

.L_bn_sqr_words_proceed:
	andcc	%o2,-4,%g0
	bz	.L_bn_sqr_words_tail
	clr	%o5

.L_bn_sqr_words_loop:
	ld	[%o1+4],%g3
	umul	%g2,%g2,%o4
	st	%o4,[%o0]
	rd	%y,%o5
	st	%o5,[%o0+4]

	ld	[%o1+8],%g2
	umul	%g3,%g3,%o4
	dec	4,%o2
	st	%o4,[%o0+8]
	rd	%y,%o5
	st	%o5,[%o0+12]
	nop

	ld	[%o1+12],%g3
	umul	%g2,%g2,%o4
	st	%o4,[%o0+16]
	rd	%y,%o5
	inc	16,%o1
	st	%o5,[%o0+20]

	umul	%g3,%g3,%o4
	inc	32,%o0
	st	%o4,[%o0-8]
	rd	%y,%o5
	st	%o5,[%o0-4]
	andcc	%o2,-4,%g2
	bnz,a	.L_bn_sqr_words_loop
	ld	[%o1],%g2

	tst	%o2
	nop
	bnz,a	.L_bn_sqr_words_tail
	ld	[%o1],%g2
.L_bn_sqr_words_return:
	retl
	clr	%o0

.L_bn_sqr_words_tail:
	umul	%g2,%g2,%o4
	st	%o4,[%o0]
	deccc	%o2
	rd	%y,%o5
	bz	.L_bn_sqr_words_return
	st	%o5,[%o0+4]

	ld	[%o1+4],%g2
	umul	%g2,%g2,%o4
	st	%o4,[%o0+8]
	deccc	%o2
	rd	%y,%o5
	nop
	bz	.L_bn_sqr_words_return
	st	%o5,[%o0+12]

	ld	[%o1+8],%g2
	umul	%g2,%g2,%o4
	st	%o4,[%o0+16]
	rd	%y,%o5
	st	%o5,[%o0+20]
	retl
	clr	%o0

.type	bn_sqr_words,#function
.size	bn_sqr_words,(.-bn_sqr_words)

.align	32

.global bn_div_words
/*
 * BN_ULONG bn_div_words(h,l,d)
 * BN_ULONG h,l,d;
 */
bn_div_words:
	wr	%o0,%y
	udiv	%o1,%o2,%o0
	retl
	nop

.type	bn_div_words,#function
.size	bn_div_words,(.-bn_div_words)

.align	32

.global bn_add_words
/*
 * BN_ULONG bn_add_words(rp,ap,bp,n)
 * BN_ULONG *rp,*ap,*bp;
 * int n;
 */
bn_add_words:
	cmp	%o3,0
	bg,a	.L_bn_add_words_proceed
	ld	[%o1],%o4
	retl
	clr	%o0

.L_bn_add_words_proceed:
	andcc	%o3,-4,%g0
	bz	.L_bn_add_words_tail
	clr	%g1
	ba	.L_bn_add_words_warn_loop
	addcc	%g0,0,%g0	! clear carry flag

.L_bn_add_words_loop:
	ld	[%o1],%o4
.L_bn_add_words_warn_loop:
	ld	[%o2],%o5
	ld	[%o1+4],%g3
	ld	[%o2+4],%g4
	dec	4,%o3
	addxcc	%o5,%o4,%o5
	st	%o5,[%o0]

	ld	[%o1+8],%o4
	ld	[%o2+8],%o5
	inc	16,%o1
	addxcc	%g3,%g4,%g3
	st	%g3,[%o0+4]
	
	ld	[%o1-4],%g3
	ld	[%o2+12],%g4
	inc	16,%o2
	addxcc	%o5,%o4,%o5
	st	%o5,[%o0+8]

	inc	16,%o0
	addxcc	%g3,%g4,%g3
	st	%g3,[%o0-4]
	addx	%g0,0,%g1
	andcc	%o3,-4,%g0
	bnz,a	.L_bn_add_words_loop
	addcc	%g1,-1,%g0

	tst	%o3
	bnz,a	.L_bn_add_words_tail
	ld	[%o1],%o4
.L_bn_add_words_return:
	retl
	mov	%g1,%o0

.L_bn_add_words_tail:
	addcc	%g1,-1,%g0
	ld	[%o2],%o5
	addxcc	%o5,%o4,%o5
	addx	%g0,0,%g1
	deccc	%o3
	bz	.L_bn_add_words_return
	st	%o5,[%o0]

	ld	[%o1+4],%o4
	addcc	%g1,-1,%g0
	ld	[%o2+4],%o5
	addxcc	%o5,%o4,%o5
	addx	%g0,0,%g1
	deccc	%o3
	bz	.L_bn_add_words_return
	st	%o5,[%o0+4]

	ld	[%o1+8],%o4
	addcc	%g1,-1,%g0
	ld	[%o2+8],%o5
	addxcc	%o5,%o4,%o5
	st	%o5,[%o0+8]
	retl
	addx	%g0,0,%o0

.type	bn_add_words,#function
.size	bn_add_words,(.-bn_add_words)

.align	32

.global bn_sub_words
/*
 * BN_ULONG bn_sub_words(rp,ap,bp,n)
 * BN_ULONG *rp,*ap,*bp;
 * int n;
 */
bn_sub_words:
	cmp	%o3,0
	bg,a	.L_bn_sub_words_proceed
	ld	[%o1],%o4
	retl
	clr	%o0

.L_bn_sub_words_proceed:
	andcc	%o3,-4,%g0
	bz	.L_bn_sub_words_tail
	clr	%g1
	ba	.L_bn_sub_words_warm_loop
	addcc	%g0,0,%g0	! clear carry flag

.L_bn_sub_words_loop:
	ld	[%o1],%o4
.L_bn_sub_words_warm_loop:
	ld	[%o2],%o5
	ld	[%o1+4],%g3
	ld	[%o2+4],%g4
	dec	4,%o3
	subxcc	%o4,%o5,%o5
	st	%o5,[%o0]

	ld	[%o1+8],%o4
	ld	[%o2+8],%o5
	inc	16,%o1
	subxcc	%g3,%g4,%g4
	st	%g4,[%o0+4]
	
	ld	[%o1-4],%g3
	ld	[%o2+12],%g4
	inc	16,%o2
	subxcc	%o4,%o5,%o5
	st	%o5,[%o0+8]

	inc	16,%o0
	subxcc	%g3,%g4,%g4
	st	%g4,[%o0-4]
	addx	%g0,0,%g1
	andcc	%o3,-4,%g0
	bnz,a	.L_bn_sub_words_loop
	addcc	%g1,-1,%g0

	tst	%o3
	nop
	bnz,a	.L_bn_sub_words_tail
	ld	[%o1],%o4
.L_bn_sub_words_return:
	retl
	mov	%g1,%o0

.L_bn_sub_words_tail:
	addcc	%g1,-1,%g0
	ld	[%o2],%o5
	subxcc	%o4,%o5,%o5
	addx	%g0,0,%g1
	deccc	%o3
	bz	.L_bn_sub_words_return
	st	%o5,[%o0]
	nop

	ld	[%o1+4],%o4
	addcc	%g1,-1,%g0
	ld	[%o2+4],%o5
	subxcc	%o4,%o5,%o5
	addx	%g0,0,%g1
	deccc	%o3
	bz	.L_bn_sub_words_return
	st	%o5,[%o0+4]

	ld	[%o1+8],%o4
	addcc	%g1,-1,%g0
	ld	[%o2+8],%o5
	subxcc	%o4,%o5,%o5
	st	%o5,[%o0+8]
	retl
	addx	%g0,0,%o0

.type	bn_sub_words,#function
.size	bn_sub_words,(.-bn_sub_words)

#define FRAME_SIZE	-96

/*
 * Here is register usage map for *all* routines below.
 */
#define t_1	%o0
#define	t_2	%o1
#define c_1	%o2
#define c_2	%o3
#define c_3	%o4

#define ap(I)	[%i1+4*I]
#define bp(I)	[%i2+4*I]
#define rp(I)	[%i0+4*I]

#define	a_0	%l0
#define	a_1	%l1
#define	a_2	%l2
#define	a_3	%l3
#define	a_4	%l4
#define	a_5	%l5
#define	a_6	%l6
#define	a_7	%l7

#define	b_0	%i3
#define	b_1	%i4
#define	b_2	%i5
#define	b_3	%o5
#define	b_4	%g1
#define	b_5	%g2
#define	b_6	%g3
#define	b_7	%g4

.align	32
.global bn_mul_comba8
/*
 * void bn_mul_comba8(r,a,b)
 * BN_ULONG *r,*a,*b;
 */
bn_mul_comba8:
	save	%sp,FRAME_SIZE,%sp
	ld	ap(0),a_0
	ld	bp(0),b_0
	umul	a_0,b_0,c_1	!=!mul_add_c(a[0],b[0],c1,c2,c3);
	ld	bp(1),b_1
	rd	%y,c_2
	st	c_1,rp(0)	!r[0]=c1;

	umul	a_0,b_1,t_1	!=!mul_add_c(a[0],b[1],c2,c3,c1);
	ld	ap(1),a_1
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	%g0,t_2,c_3	!=
	addx	%g0,%g0,c_1
	ld	ap(2),a_2
	umul	a_1,b_0,t_1	!mul_add_c(a[1],b[0],c2,c3,c1);
	addcc	c_2,t_1,c_2	!=
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	st	c_2,rp(1)	!r[1]=c2;
	addx	c_1,%g0,c_1	!=

	umul	a_2,b_0,t_1	!mul_add_c(a[2],b[0],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!=
	addx	%g0,%g0,c_2
	ld	bp(2),b_2
	umul	a_1,b_1,t_1	!mul_add_c(a[1],b[1],c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	ld	bp(3),b_3
	addx	c_2,%g0,c_2	!=
	umul	a_0,b_2,t_1	!mul_add_c(a[0],b[2],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!=
	addx	c_2,%g0,c_2
	st	c_3,rp(2)	!r[2]=c3;

	umul	a_0,b_3,t_1	!mul_add_c(a[0],b[3],c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	%g0,%g0,c_3
	umul	a_1,b_2,t_1	!=!mul_add_c(a[1],b[2],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	ld	ap(3),a_3
	umul	a_2,b_1,t_1	!mul_add_c(a[2],b[1],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2		!=
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	ld	ap(4),a_4
	umul	a_3,b_0,t_1	!mul_add_c(a[3],b[0],c1,c2,c3);!=
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	st	c_1,rp(3)	!r[3]=c1;

	umul	a_4,b_0,t_1	!mul_add_c(a[4],b[0],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	%g0,%g0,c_1
	umul	a_3,b_1,t_1	!mul_add_c(a[3],b[1],c2,c3,c1);
	addcc	c_2,t_1,c_2	!=
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	umul	a_2,b_2,t_1	!=!mul_add_c(a[2],b[2],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1	!=
	ld	bp(4),b_4
	umul	a_1,b_3,t_1	!mul_add_c(a[1],b[3],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	ld	bp(5),b_5
	umul	a_0,b_4,t_1	!=!mul_add_c(a[0],b[4],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1	!=
	st	c_2,rp(4)	!r[4]=c2;

	umul	a_0,b_5,t_1	!mul_add_c(a[0],b[5],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2		!=
	addxcc	c_1,t_2,c_1
	addx	%g0,%g0,c_2
	umul	a_1,b_4,t_1	!mul_add_c(a[1],b[4],c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	umul	a_2,b_3,t_1	!=!mul_add_c(a[2],b[3],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2	!=
	umul	a_3,b_2,t_1	!mul_add_c(a[3],b[2],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!=
	addx	c_2,%g0,c_2
	ld	ap(5),a_5
	umul	a_4,b_1,t_1	!mul_add_c(a[4],b[1],c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	ld	ap(6),a_6
	addx	c_2,%g0,c_2	!=
	umul	a_5,b_0,t_1	!mul_add_c(a[5],b[0],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!=
	addx	c_2,%g0,c_2
	st	c_3,rp(5)	!r[5]=c3;

	umul	a_6,b_0,t_1	!mul_add_c(a[6],b[0],c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	%g0,%g0,c_3
	umul	a_5,b_1,t_1	!=!mul_add_c(a[5],b[1],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	umul	a_4,b_2,t_1	!mul_add_c(a[4],b[2],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	c_3,%g0,c_3
	umul	a_3,b_3,t_1	!mul_add_c(a[3],b[3],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2		!=
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	umul	a_2,b_4,t_1	!mul_add_c(a[2],b[4],c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	ld	bp(6),b_6
	addx	c_3,%g0,c_3	!=
	umul	a_1,b_5,t_1	!mul_add_c(a[1],b[5],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	c_3,%g0,c_3
	ld	bp(7),b_7
	umul	a_0,b_6,t_1	!mul_add_c(a[0],b[6],c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	st	c_1,rp(6)	!r[6]=c1;
	addx	c_3,%g0,c_3	!=

	umul	a_0,b_7,t_1	!mul_add_c(a[0],b[7],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3	!=
	addx	%g0,%g0,c_1
	umul	a_1,b_6,t_1	!mul_add_c(a[1],b[6],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	umul	a_2,b_5,t_1	!mul_add_c(a[2],b[5],c2,c3,c1);
	addcc	c_2,t_1,c_2	!=
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	umul	a_3,b_4,t_1	!=!mul_add_c(a[3],b[4],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1	!=
	umul	a_4,b_3,t_1	!mul_add_c(a[4],b[3],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	umul	a_5,b_2,t_1	!mul_add_c(a[5],b[2],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	ld	ap(7),a_7
	umul	a_6,b_1,t_1	!=!mul_add_c(a[6],b[1],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1	!=
	umul	a_7,b_0,t_1	!mul_add_c(a[7],b[0],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	st	c_2,rp(7)	!r[7]=c2;

	umul	a_7,b_1,t_1	!mul_add_c(a[7],b[1],c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	%g0,%g0,c_2
	umul	a_6,b_2,t_1	!=!mul_add_c(a[6],b[2],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2	!=
	umul	a_5,b_3,t_1	!mul_add_c(a[5],b[3],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!=
	addx	c_2,%g0,c_2
	umul	a_4,b_4,t_1	!mul_add_c(a[4],b[4],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2		!=
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	umul	a_3,b_5,t_1	!mul_add_c(a[3],b[5],c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	umul	a_2,b_6,t_1	!=!mul_add_c(a[2],b[6],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2	!=
	umul	a_1,b_7,t_1	!mul_add_c(a[1],b[7],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!
	addx	c_2,%g0,c_2
	st	c_3,rp(8)	!r[8]=c3;

	umul	a_2,b_7,t_1	!mul_add_c(a[2],b[7],c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	%g0,%g0,c_3
	umul	a_3,b_6,t_1	!=!mul_add_c(a[3],b[6],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	umul	a_4,b_5,t_1	!mul_add_c(a[4],b[5],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	c_3,%g0,c_3
	umul	a_5,b_4,t_1	!mul_add_c(a[5],b[4],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2		!=
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	umul	a_6,b_3,t_1	!mul_add_c(a[6],b[3],c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	umul	a_7,b_2,t_1	!=!mul_add_c(a[7],b[2],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	st	c_1,rp(9)	!r[9]=c1;

	umul	a_7,b_3,t_1	!mul_add_c(a[7],b[3],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	%g0,%g0,c_1
	umul	a_6,b_4,t_1	!mul_add_c(a[6],b[4],c2,c3,c1);
	addcc	c_2,t_1,c_2	!=
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	umul	a_5,b_5,t_1	!=!mul_add_c(a[5],b[5],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1	!=
	umul	a_4,b_6,t_1	!mul_add_c(a[4],b[6],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	umul	a_3,b_7,t_1	!mul_add_c(a[3],b[7],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	st	c_2,rp(10)	!r[10]=c2;

	umul	a_4,b_7,t_1	!=!mul_add_c(a[4],b[7],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	%g0,%g0,c_2	!=
	umul	a_5,b_6,t_1	!mul_add_c(a[5],b[6],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!=
	addx	c_2,%g0,c_2
	umul	a_6,b_5,t_1	!mul_add_c(a[6],b[5],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2		!=
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	umul	a_7,b_4,t_1	!mul_add_c(a[7],b[4],c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	st	c_3,rp(11)	!r[11]=c3;
	addx	c_2,%g0,c_2	!=

	umul	a_7,b_5,t_1	!mul_add_c(a[7],b[5],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	%g0,%g0,c_3
	umul	a_6,b_6,t_1	!mul_add_c(a[6],b[6],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2		!=
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	umul	a_5,b_7,t_1	!mul_add_c(a[5],b[7],c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	st	c_1,rp(12)	!r[12]=c1;
	addx	c_3,%g0,c_3	!=

	umul	a_6,b_7,t_1	!mul_add_c(a[6],b[7],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3	!=
	addx	%g0,%g0,c_1
	umul	a_7,b_6,t_1	!mul_add_c(a[7],b[6],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	st	c_2,rp(13)	!r[13]=c2;

	umul	a_7,b_7,t_1	!=!mul_add_c(a[7],b[7],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	nop			!=
	st	c_3,rp(14)	!r[14]=c3;
	st	c_1,rp(15)	!r[15]=c1;

	ret
	restore	%g0,%g0,%o0

.type	bn_mul_comba8,#function
.size	bn_mul_comba8,(.-bn_mul_comba8)

.align	32

.global bn_mul_comba4
/*
 * void bn_mul_comba4(r,a,b)
 * BN_ULONG *r,*a,*b;
 */
bn_mul_comba4:
	save	%sp,FRAME_SIZE,%sp
	ld	ap(0),a_0
	ld	bp(0),b_0
	umul	a_0,b_0,c_1	!=!mul_add_c(a[0],b[0],c1,c2,c3);
	ld	bp(1),b_1
	rd	%y,c_2
	st	c_1,rp(0)	!r[0]=c1;

	umul	a_0,b_1,t_1	!=!mul_add_c(a[0],b[1],c2,c3,c1);
	ld	ap(1),a_1
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	%g0,t_2,c_3
	addx	%g0,%g0,c_1
	ld	ap(2),a_2
	umul	a_1,b_0,t_1	!=!mul_add_c(a[1],b[0],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1	!=
	st	c_2,rp(1)	!r[1]=c2;

	umul	a_2,b_0,t_1	!mul_add_c(a[2],b[0],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2		!=
	addxcc	c_1,t_2,c_1
	addx	%g0,%g0,c_2
	ld	bp(2),b_2
	umul	a_1,b_1,t_1	!=!mul_add_c(a[1],b[1],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2	!=
	ld	bp(3),b_3
	umul	a_0,b_2,t_1	!mul_add_c(a[0],b[2],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2		!=
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	st	c_3,rp(2)	!r[2]=c3;

	umul	a_0,b_3,t_1	!=!mul_add_c(a[0],b[3],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	%g0,%g0,c_3	!=
	umul	a_1,b_2,t_1	!mul_add_c(a[1],b[2],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	c_3,%g0,c_3
	ld	ap(3),a_3
	umul	a_2,b_1,t_1	!mul_add_c(a[2],b[1],c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	umul	a_3,b_0,t_1	!=!mul_add_c(a[3],b[0],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	st	c_1,rp(3)	!r[3]=c1;

	umul	a_3,b_1,t_1	!mul_add_c(a[3],b[1],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	%g0,%g0,c_1
	umul	a_2,b_2,t_1	!mul_add_c(a[2],b[2],c2,c3,c1);
	addcc	c_2,t_1,c_2	!=
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	umul	a_1,b_3,t_1	!=!mul_add_c(a[1],b[3],c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1	!=
	st	c_2,rp(4)	!r[4]=c2;

	umul	a_2,b_3,t_1	!mul_add_c(a[2],b[3],c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2		!=
	addxcc	c_1,t_2,c_1
	addx	%g0,%g0,c_2
	umul	a_3,b_2,t_1	!mul_add_c(a[3],b[2],c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	st	c_3,rp(5)	!r[5]=c3;
	addx	c_2,%g0,c_2	!=

	umul	a_3,b_3,t_1	!mul_add_c(a[3],b[3],c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	st	c_1,rp(6)	!r[6]=c1;
	st	c_2,rp(7)	!r[7]=c2;
	
	ret
	restore	%g0,%g0,%o0

.type	bn_mul_comba4,#function
.size	bn_mul_comba4,(.-bn_mul_comba4)

.align	32

.global bn_sqr_comba8
bn_sqr_comba8:
	save	%sp,FRAME_SIZE,%sp
	ld	ap(0),a_0
	ld	ap(1),a_1
	umul	a_0,a_0,c_1	!=!sqr_add_c(a,0,c1,c2,c3);
	rd	%y,c_2
	st	c_1,rp(0)	!r[0]=c1;

	ld	ap(2),a_2
	umul	a_0,a_1,t_1	!=!sqr_add_c2(a,1,0,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	%g0,t_2,c_3
	addx	%g0,%g0,c_1	!=
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3
	st	c_2,rp(1)	!r[1]=c2;
	addx	c_1,%g0,c_1	!=

	umul	a_2,a_0,t_1	!sqr_add_c2(a,2,0,c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!=
	addx	%g0,%g0,c_2
	addcc	c_3,t_1,c_3
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2	!=
	ld	ap(3),a_3
	umul	a_1,a_1,t_1	!sqr_add_c(a,1,c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2		!=
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	st	c_3,rp(2)	!r[2]=c3;

	umul	a_0,a_3,t_1	!=!sqr_add_c2(a,3,0,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	%g0,%g0,c_3	!=
	addcc	c_1,t_1,c_1
	addxcc	c_2,t_2,c_2
	ld	ap(4),a_4
	addx	c_3,%g0,c_3	!=
	umul	a_1,a_2,t_1	!sqr_add_c2(a,2,1,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	c_3,%g0,c_3
	addcc	c_1,t_1,c_1
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	st	c_1,rp(3)	!r[3]=c1;

	umul	a_4,a_0,t_1	!sqr_add_c2(a,4,0,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	%g0,%g0,c_1
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	umul	a_3,a_1,t_1	!sqr_add_c2(a,3,1,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	ld	ap(5),a_5
	umul	a_2,a_2,t_1	!sqr_add_c(a,2,c2,c3,c1);
	addcc	c_2,t_1,c_2	!=
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	st	c_2,rp(4)	!r[4]=c2;
	addx	c_1,%g0,c_1	!=

	umul	a_0,a_5,t_1	!sqr_add_c2(a,5,0,c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!=
	addx	%g0,%g0,c_2
	addcc	c_3,t_1,c_3
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2	!=
	umul	a_1,a_4,t_1	!sqr_add_c2(a,4,1,c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!=
	addx	c_2,%g0,c_2
	addcc	c_3,t_1,c_3
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2	!=
	ld	ap(6),a_6
	umul	a_2,a_3,t_1	!sqr_add_c2(a,3,2,c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2		!=
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	addcc	c_3,t_1,c_3
	addxcc	c_1,t_2,c_1	!=
	addx	c_2,%g0,c_2
	st	c_3,rp(5)	!r[5]=c3;

	umul	a_6,a_0,t_1	!sqr_add_c2(a,6,0,c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	%g0,%g0,c_3
	addcc	c_1,t_1,c_1	!=
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	umul	a_5,a_1,t_1	!sqr_add_c2(a,5,1,c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	addcc	c_1,t_1,c_1	!=
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	umul	a_4,a_2,t_1	!sqr_add_c2(a,4,2,c1,c2,c3);
	addcc	c_1,t_1,c_1	!=
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	addcc	c_1,t_1,c_1	!=
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3
	ld	ap(7),a_7
	umul	a_3,a_3,t_1	!=!sqr_add_c(a,3,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	st	c_1,rp(6)	!r[6]=c1;

	umul	a_0,a_7,t_1	!sqr_add_c2(a,7,0,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	%g0,%g0,c_1
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	umul	a_1,a_6,t_1	!sqr_add_c2(a,6,1,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	umul	a_2,a_5,t_1	!sqr_add_c2(a,5,2,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	umul	a_3,a_4,t_1	!sqr_add_c2(a,4,3,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	st	c_2,rp(7)	!r[7]=c2;

	umul	a_7,a_1,t_1	!sqr_add_c2(a,7,1,c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	%g0,%g0,c_2
	addcc	c_3,t_1,c_3	!=
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	umul	a_6,a_2,t_1	!sqr_add_c2(a,6,2,c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	addcc	c_3,t_1,c_3	!=
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	umul	a_5,a_3,t_1	!sqr_add_c2(a,5,3,c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	addcc	c_3,t_1,c_3	!=
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	umul	a_4,a_4,t_1	!sqr_add_c(a,4,c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	st	c_3,rp(8)	!r[8]=c3;
	addx	c_2,%g0,c_2	!=

	umul	a_2,a_7,t_1	!sqr_add_c2(a,7,2,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	%g0,%g0,c_3
	addcc	c_1,t_1,c_1
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	umul	a_3,a_6,t_1	!sqr_add_c2(a,6,3,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	c_3,%g0,c_3
	addcc	c_1,t_1,c_1
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	umul	a_4,a_5,t_1	!sqr_add_c2(a,5,4,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	c_3,%g0,c_3
	addcc	c_1,t_1,c_1
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	st	c_1,rp(9)	!r[9]=c1;

	umul	a_7,a_3,t_1	!sqr_add_c2(a,7,3,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	%g0,%g0,c_1
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	umul	a_6,a_4,t_1	!sqr_add_c2(a,6,4,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	umul	a_5,a_5,t_1	!sqr_add_c(a,5,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	st	c_2,rp(10)	!r[10]=c2;

	umul	a_4,a_7,t_1	!=!sqr_add_c2(a,7,4,c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	%g0,%g0,c_2	!=
	addcc	c_3,t_1,c_3
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2
	umul	a_5,a_6,t_1	!=!sqr_add_c2(a,6,5,c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	c_2,%g0,c_2	!=
	addcc	c_3,t_1,c_3
	addxcc	c_1,t_2,c_1
	st	c_3,rp(11)	!r[11]=c3;
	addx	c_2,%g0,c_2	!=

	umul	a_7,a_5,t_1	!sqr_add_c2(a,7,5,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	%g0,%g0,c_3
	addcc	c_1,t_1,c_1
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	umul	a_6,a_6,t_1	!sqr_add_c(a,6,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	c_3,%g0,c_3
	st	c_1,rp(12)	!r[12]=c1;

	umul	a_6,a_7,t_1	!sqr_add_c2(a,7,6,c2,c3,c1);
	addcc	c_2,t_1,c_2	!=
	rd	%y,t_2
	addxcc	c_3,t_2,c_3
	addx	%g0,%g0,c_1
	addcc	c_2,t_1,c_2	!=
	addxcc	c_3,t_2,c_3
	st	c_2,rp(13)	!r[13]=c2;
	addx	c_1,%g0,c_1	!=

	umul	a_7,a_7,t_1	!sqr_add_c(a,7,c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1	!=
	st	c_3,rp(14)	!r[14]=c3;
	st	c_1,rp(15)	!r[15]=c1;

	ret
	restore	%g0,%g0,%o0

.type	bn_sqr_comba8,#function
.size	bn_sqr_comba8,(.-bn_sqr_comba8)

.align	32

.global bn_sqr_comba4
/*
 * void bn_sqr_comba4(r,a)
 * BN_ULONG *r,*a;
 */
bn_sqr_comba4:
	save	%sp,FRAME_SIZE,%sp
	ld	ap(0),a_0
	umul	a_0,a_0,c_1	!sqr_add_c(a,0,c1,c2,c3);
	ld	ap(1),a_1	!=
	rd	%y,c_2
	st	c_1,rp(0)	!r[0]=c1;

	ld	ap(2),a_2
	umul	a_0,a_1,t_1	!=!sqr_add_c2(a,1,0,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2
	addxcc	%g0,t_2,c_3
	addx	%g0,%g0,c_1	!=
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1	!=
	st	c_2,rp(1)	!r[1]=c2;

	umul	a_2,a_0,t_1	!sqr_add_c2(a,2,0,c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2		!=
	addxcc	c_1,t_2,c_1
	addx	%g0,%g0,c_2
	addcc	c_3,t_1,c_3
	addxcc	c_1,t_2,c_1	!=
	addx	c_2,%g0,c_2
	ld	ap(3),a_3
	umul	a_1,a_1,t_1	!sqr_add_c(a,1,c3,c1,c2);
	addcc	c_3,t_1,c_3	!=
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	st	c_3,rp(2)	!r[2]=c3;
	addx	c_2,%g0,c_2	!=

	umul	a_0,a_3,t_1	!sqr_add_c2(a,3,0,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	%g0,%g0,c_3
	addcc	c_1,t_1,c_1
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	umul	a_1,a_2,t_1	!sqr_add_c2(a,2,1,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	addx	c_3,%g0,c_3
	addcc	c_1,t_1,c_1
	addxcc	c_2,t_2,c_2
	addx	c_3,%g0,c_3	!=
	st	c_1,rp(3)	!r[3]=c1;

	umul	a_3,a_1,t_1	!sqr_add_c2(a,3,1,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	%g0,%g0,c_1
	addcc	c_2,t_1,c_2
	addxcc	c_3,t_2,c_3	!=
	addx	c_1,%g0,c_1
	umul	a_2,a_2,t_1	!sqr_add_c(a,2,c2,c3,c1);
	addcc	c_2,t_1,c_2
	rd	%y,t_2		!=
	addxcc	c_3,t_2,c_3
	addx	c_1,%g0,c_1
	st	c_2,rp(4)	!r[4]=c2;

	umul	a_2,a_3,t_1	!=!sqr_add_c2(a,3,2,c3,c1,c2);
	addcc	c_3,t_1,c_3
	rd	%y,t_2
	addxcc	c_1,t_2,c_1
	addx	%g0,%g0,c_2	!=
	addcc	c_3,t_1,c_3
	addxcc	c_1,t_2,c_1
	st	c_3,rp(5)	!r[5]=c3;
	addx	c_2,%g0,c_2	!=

	umul	a_3,a_3,t_1	!sqr_add_c(a,3,c1,c2,c3);
	addcc	c_1,t_1,c_1
	rd	%y,t_2
	addxcc	c_2,t_2,c_2	!=
	st	c_1,rp(6)	!r[6]=c1;
	st	c_2,rp(7)	!r[7]=c2;
	
	ret
	restore	%g0,%g0,%o0

.type	bn_sqr_comba4,#function
.size	bn_sqr_comba4,(.-bn_sqr_comba4)

.align	32

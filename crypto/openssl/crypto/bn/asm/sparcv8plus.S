.ident	"sparcv8plus.s, Version 1.4"
.ident	"SPARC v9 ISA artwork by Andy Polyakov <appro@openssl.org>"

/*
 * ====================================================================
 * Copyright 1999-2019 The OpenSSL Project Authors. All Rights Reserved.
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
 * a drop-in UltraSPARC ISA replacement for crypto/bn/bn_asm.c
 * module. For updates see http://fy.chalmers.se/~appro/hpe/.
 *
 * Questions-n-answers.
 *
 * Q. How to compile?
 * A. With SC4.x/SC5.x:
 *
 *	cc -xarch=v8plus -c bn_asm.sparc.v8plus.S -o bn_asm.o
 *
 *    and with gcc:
 *
 *	gcc -mcpu=ultrasparc -c bn_asm.sparc.v8plus.S -o bn_asm.o
 *
 *    or if above fails (it does if you have gas installed):
 *
 *	gcc -E bn_asm.sparc.v8plus.S | as -xarch=v8plus /dev/fd/0 -o bn_asm.o
 *
 *    Quick-n-dirty way to fuse the module into the library.
 *    Provided that the library is already configured and built
 *    (in 0.9.2 case with no-asm option):
 *
 *	# cd crypto/bn
 *	# cp /some/place/bn_asm.sparc.v8plus.S .
 *	# cc -xarch=v8plus -c bn_asm.sparc.v8plus.S -o bn_asm.o
 *	# make
 *	# cd ../..
 *	# make; make test
 *
 *    Quick-n-dirty way to get rid of it:
 *
 *	# cd crypto/bn
 *	# touch bn_asm.c
 *	# make
 *	# cd ../..
 *	# make; make test
 *
 * Q. V8plus architecture? What kind of beast is that?
 * A. Well, it's rather a programming model than an architecture...
 *    It's actually v9-compliant, i.e. *any* UltraSPARC, CPU under
 *    special conditions, namely when kernel doesn't preserve upper
 *    32 bits of otherwise 64-bit registers during a context switch.
 *
 * Q. Why just UltraSPARC? What about SuperSPARC?
 * A. Original release did target UltraSPARC only. Now SuperSPARC
 *    version is provided along. Both version share bn_*comba[48]
 *    implementations (see comment later in code for explanation).
 *    But what's so special about this UltraSPARC implementation?
 *    Why didn't I let compiler do the job? Trouble is that most of
 *    available compilers (well, SC5.0 is the only exception) don't
 *    attempt to take advantage of UltraSPARC's 64-bitness under
 *    32-bit kernels even though it's perfectly possible (see next
 *    question).
 *
 * Q. 64-bit registers under 32-bit kernels? Didn't you just say it
 *    doesn't work?
 * A. You can't address *all* registers as 64-bit wide:-( The catch is
 *    that you actually may rely upon %o0-%o5 and %g1-%g4 being fully
 *    preserved if you're in a leaf function, i.e. such never calling
 *    any other functions. All functions in this module are leaf and
 *    10 registers is a handful. And as a matter of fact none-"comba"
 *    routines don't require even that much and I could even afford to
 *    not allocate own stack frame for 'em:-)
 *
 * Q. What about 64-bit kernels?
 * A. What about 'em? Just kidding:-) Pure 64-bit version is currently
 *    under evaluation and development...
 *
 * Q. What about shared libraries?
 * A. What about 'em? Kidding again:-) Code does *not* contain any
 *    code position dependencies and it's safe to include it into
 *    shared library as is.
 *
 * Q. How much faster does it go?
 * A. Do you have a good benchmark? In either case below is what I
 *    experience with crypto/bn/expspeed.c test program:
 *
 *	v8plus module on U10/300MHz against bn_asm.c compiled with:
 *
 *	cc-5.0 -xarch=v8plus -xO5 -xdepend	+7-12%
 *	cc-4.2 -xarch=v8plus -xO5 -xdepend	+25-35%
 *	egcs-1.1.2 -mcpu=ultrasparc -O3		+35-45%
 *
 *	v8 module on SS10/60MHz against bn_asm.c compiled with:
 *
 *	cc-5.0 -xarch=v8 -xO5 -xdepend		+7-10%
 *	cc-4.2 -xarch=v8 -xO5 -xdepend		+10%
 *	egcs-1.1.2 -mv8 -O3			+35-45%
 *
 *    As you can see it's damn hard to beat the new Sun C compiler
 *    and it's in first place GNU C users who will appreciate this
 *    assembler implementation:-)	
 */

/*
 * Revision history.
 *
 * 1.0	- initial release;
 * 1.1	- new loop unrolling model(*);
 *	- some more fine tuning;
 * 1.2	- made gas friendly;
 *	- updates to documentation concerning v9;
 *	- new performance comparison matrix;
 * 1.3	- fixed problem with /usr/ccs/lib/cpp;
 * 1.4	- native V9 bn_*_comba[48] implementation (15% more efficient)
 *	  resulting in slight overall performance kick;
 *	- some retunes;
 *	- support for GNU as added;
 *
 * (*)	Originally unrolled loop looked like this:
 *	    for (;;) {
 *		op(p+0); if (--n==0) break;
 *		op(p+1); if (--n==0) break;
 *		op(p+2); if (--n==0) break;
 *		op(p+3); if (--n==0) break;
 *		p+=4;
 *	    }
 *	I unroll according to following:
 *	    while (n&~3) {
 *		op(p+0); op(p+1); op(p+2); op(p+3);
 *		p+=4; n=-4;
 *	    }
 *	    if (n) {
 *		op(p+0); if (--n==0) return;
 *		op(p+2); if (--n==0) return;
 *		op(p+3); return;
 *	    }
 */

#if defined(__SUNPRO_C) && defined(__sparcv9)
  /* They've said -xarch=v9 at command line */
  .register	%g2,#scratch
  .register	%g3,#scratch
# define	FRAME_SIZE	-192
#elif defined(__GNUC__) && defined(__arch64__)
  /* They've said -m64 at command line */
  .register	%g2,#scratch
  .register	%g3,#scratch
# define	FRAME_SIZE	-192
#else
# define	FRAME_SIZE	-96
#endif
/*
 * GNU assembler can't stand stuw:-(
 */
#define stuw st

.section	".text",#alloc,#execinstr
.file		"bn_asm.sparc.v8plus.S"

.align	32

.global bn_mul_add_words
/*
 * BN_ULONG bn_mul_add_words(rp,ap,num,w)
 * BN_ULONG *rp,*ap;
 * int num;
 * BN_ULONG w;
 */
bn_mul_add_words:
	sra	%o2,%g0,%o2	! signx %o2
	brgz,a	%o2,.L_bn_mul_add_words_proceed
	lduw	[%o1],%g2
	retl
	clr	%o0
	nop
	nop
	nop

.L_bn_mul_add_words_proceed:
	srl	%o3,%g0,%o3	! clruw	%o3
	andcc	%o2,-4,%g0
	bz,pn	%icc,.L_bn_mul_add_words_tail
	clr	%o5

.L_bn_mul_add_words_loop:	! wow! 32 aligned!
	lduw	[%o0],%g1
	lduw	[%o1+4],%g3
	mulx	%o3,%g2,%g2
	add	%g1,%o5,%o4
	nop
	add	%o4,%g2,%o4
	stuw	%o4,[%o0]
	srlx	%o4,32,%o5

	lduw	[%o0+4],%g1
	lduw	[%o1+8],%g2
	mulx	%o3,%g3,%g3
	add	%g1,%o5,%o4
	dec	4,%o2
	add	%o4,%g3,%o4
	stuw	%o4,[%o0+4]
	srlx	%o4,32,%o5

	lduw	[%o0+8],%g1
	lduw	[%o1+12],%g3
	mulx	%o3,%g2,%g2
	add	%g1,%o5,%o4
	inc	16,%o1
	add	%o4,%g2,%o4
	stuw	%o4,[%o0+8]
	srlx	%o4,32,%o5

	lduw	[%o0+12],%g1
	mulx	%o3,%g3,%g3
	add	%g1,%o5,%o4
	inc	16,%o0
	add	%o4,%g3,%o4
	andcc	%o2,-4,%g0
	stuw	%o4,[%o0-4]
	srlx	%o4,32,%o5
	bnz,a,pt	%icc,.L_bn_mul_add_words_loop
	lduw	[%o1],%g2

	brnz,a,pn	%o2,.L_bn_mul_add_words_tail
	lduw	[%o1],%g2
.L_bn_mul_add_words_return:
	retl
	mov	%o5,%o0

.L_bn_mul_add_words_tail:
	lduw	[%o0],%g1
	mulx	%o3,%g2,%g2
	add	%g1,%o5,%o4
	dec	%o2
	add	%o4,%g2,%o4
	srlx	%o4,32,%o5
	brz,pt	%o2,.L_bn_mul_add_words_return
	stuw	%o4,[%o0]

	lduw	[%o1+4],%g2
	lduw	[%o0+4],%g1
	mulx	%o3,%g2,%g2
	add	%g1,%o5,%o4
	dec	%o2
	add	%o4,%g2,%o4
	srlx	%o4,32,%o5
	brz,pt	%o2,.L_bn_mul_add_words_return
	stuw	%o4,[%o0+4]

	lduw	[%o1+8],%g2
	lduw	[%o0+8],%g1
	mulx	%o3,%g2,%g2
	add	%g1,%o5,%o4
	add	%o4,%g2,%o4
	stuw	%o4,[%o0+8]
	retl
	srlx	%o4,32,%o0

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
	sra	%o2,%g0,%o2	! signx %o2
	brgz,a	%o2,.L_bn_mul_words_proceed
	lduw	[%o1],%g2
	retl
	clr	%o0
	nop
	nop
	nop

.L_bn_mul_words_proceed:
	srl	%o3,%g0,%o3	! clruw	%o3
	andcc	%o2,-4,%g0
	bz,pn	%icc,.L_bn_mul_words_tail
	clr	%o5

.L_bn_mul_words_loop:		! wow! 32 aligned!
	lduw	[%o1+4],%g3
	mulx	%o3,%g2,%g2
	add	%g2,%o5,%o4
	nop
	stuw	%o4,[%o0]
	srlx	%o4,32,%o5

	lduw	[%o1+8],%g2
	mulx	%o3,%g3,%g3
	add	%g3,%o5,%o4
	dec	4,%o2
	stuw	%o4,[%o0+4]
	srlx	%o4,32,%o5

	lduw	[%o1+12],%g3
	mulx	%o3,%g2,%g2
	add	%g2,%o5,%o4
	inc	16,%o1
	stuw	%o4,[%o0+8]
	srlx	%o4,32,%o5

	mulx	%o3,%g3,%g3
	add	%g3,%o5,%o4
	inc	16,%o0
	stuw	%o4,[%o0-4]
	srlx	%o4,32,%o5
	andcc	%o2,-4,%g0
	bnz,a,pt	%icc,.L_bn_mul_words_loop
	lduw	[%o1],%g2
	nop
	nop

	brnz,a,pn	%o2,.L_bn_mul_words_tail
	lduw	[%o1],%g2
.L_bn_mul_words_return:
	retl
	mov	%o5,%o0

.L_bn_mul_words_tail:
	mulx	%o3,%g2,%g2
	add	%g2,%o5,%o4
	dec	%o2
	srlx	%o4,32,%o5
	brz,pt	%o2,.L_bn_mul_words_return
	stuw	%o4,[%o0]

	lduw	[%o1+4],%g2
	mulx	%o3,%g2,%g2
	add	%g2,%o5,%o4
	dec	%o2
	srlx	%o4,32,%o5
	brz,pt	%o2,.L_bn_mul_words_return
	stuw	%o4,[%o0+4]

	lduw	[%o1+8],%g2
	mulx	%o3,%g2,%g2
	add	%g2,%o5,%o4
	stuw	%o4,[%o0+8]
	retl
	srlx	%o4,32,%o0

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
	sra	%o2,%g0,%o2	! signx %o2
	brgz,a	%o2,.L_bn_sqr_words_proceed
	lduw	[%o1],%g2
	retl
	clr	%o0
	nop
	nop
	nop

.L_bn_sqr_words_proceed:
	andcc	%o2,-4,%g0
	nop
	bz,pn	%icc,.L_bn_sqr_words_tail
	nop

.L_bn_sqr_words_loop:		! wow! 32 aligned!
	lduw	[%o1+4],%g3
	mulx	%g2,%g2,%o4
	stuw	%o4,[%o0]
	srlx	%o4,32,%o5
	stuw	%o5,[%o0+4]
	nop

	lduw	[%o1+8],%g2
	mulx	%g3,%g3,%o4
	dec	4,%o2
	stuw	%o4,[%o0+8]
	srlx	%o4,32,%o5
	stuw	%o5,[%o0+12]

	lduw	[%o1+12],%g3
	mulx	%g2,%g2,%o4
	srlx	%o4,32,%o5
	stuw	%o4,[%o0+16]
	inc	16,%o1
	stuw	%o5,[%o0+20]

	mulx	%g3,%g3,%o4
	inc	32,%o0
	stuw	%o4,[%o0-8]
	srlx	%o4,32,%o5
	andcc	%o2,-4,%g2
	stuw	%o5,[%o0-4]
	bnz,a,pt	%icc,.L_bn_sqr_words_loop
	lduw	[%o1],%g2
	nop

	brnz,a,pn	%o2,.L_bn_sqr_words_tail
	lduw	[%o1],%g2
.L_bn_sqr_words_return:
	retl
	clr	%o0

.L_bn_sqr_words_tail:
	mulx	%g2,%g2,%o4
	dec	%o2
	stuw	%o4,[%o0]
	srlx	%o4,32,%o5
	brz,pt	%o2,.L_bn_sqr_words_return
	stuw	%o5,[%o0+4]

	lduw	[%o1+4],%g2
	mulx	%g2,%g2,%o4
	dec	%o2
	stuw	%o4,[%o0+8]
	srlx	%o4,32,%o5
	brz,pt	%o2,.L_bn_sqr_words_return
	stuw	%o5,[%o0+12]

	lduw	[%o1+8],%g2
	mulx	%g2,%g2,%o4
	srlx	%o4,32,%o5
	stuw	%o4,[%o0+16]
	stuw	%o5,[%o0+20]
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
	sllx	%o0,32,%o0
	or	%o0,%o1,%o0
	udivx	%o0,%o2,%o0
	retl
	srl	%o0,%g0,%o0	! clruw	%o0

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
	sra	%o3,%g0,%o3	! signx %o3
	brgz,a	%o3,.L_bn_add_words_proceed
	lduw	[%o1],%o4
	retl
	clr	%o0

.L_bn_add_words_proceed:
	andcc	%o3,-4,%g0
	bz,pn	%icc,.L_bn_add_words_tail
	addcc	%g0,0,%g0	! clear carry flag

.L_bn_add_words_loop:		! wow! 32 aligned!
	dec	4,%o3
	lduw	[%o2],%o5
	lduw	[%o1+4],%g1
	lduw	[%o2+4],%g2
	lduw	[%o1+8],%g3
	lduw	[%o2+8],%g4
	addccc	%o5,%o4,%o5
	stuw	%o5,[%o0]

	lduw	[%o1+12],%o4
	lduw	[%o2+12],%o5
	inc	16,%o1
	addccc	%g1,%g2,%g1
	stuw	%g1,[%o0+4]
	
	inc	16,%o2
	addccc	%g3,%g4,%g3
	stuw	%g3,[%o0+8]

	inc	16,%o0
	addccc	%o5,%o4,%o5
	stuw	%o5,[%o0-4]
	and	%o3,-4,%g1
	brnz,a,pt	%g1,.L_bn_add_words_loop
	lduw	[%o1],%o4

	brnz,a,pn	%o3,.L_bn_add_words_tail
	lduw	[%o1],%o4
.L_bn_add_words_return:
	clr	%o0
	retl
	movcs	%icc,1,%o0
	nop

.L_bn_add_words_tail:
	lduw	[%o2],%o5
	dec	%o3
	addccc	%o5,%o4,%o5
	brz,pt	%o3,.L_bn_add_words_return
	stuw	%o5,[%o0]

	lduw	[%o1+4],%o4
	lduw	[%o2+4],%o5
	dec	%o3
	addccc	%o5,%o4,%o5
	brz,pt	%o3,.L_bn_add_words_return
	stuw	%o5,[%o0+4]

	lduw	[%o1+8],%o4
	lduw	[%o2+8],%o5
	addccc	%o5,%o4,%o5
	stuw	%o5,[%o0+8]
	clr	%o0
	retl
	movcs	%icc,1,%o0

.type	bn_add_words,#function
.size	bn_add_words,(.-bn_add_words)

.global bn_sub_words
/*
 * BN_ULONG bn_sub_words(rp,ap,bp,n)
 * BN_ULONG *rp,*ap,*bp;
 * int n;
 */
bn_sub_words:
	sra	%o3,%g0,%o3	! signx %o3
	brgz,a	%o3,.L_bn_sub_words_proceed
	lduw	[%o1],%o4
	retl
	clr	%o0

.L_bn_sub_words_proceed:
	andcc	%o3,-4,%g0
	bz,pn	%icc,.L_bn_sub_words_tail
	addcc	%g0,0,%g0	! clear carry flag

.L_bn_sub_words_loop:		! wow! 32 aligned!
	dec	4,%o3
	lduw	[%o2],%o5
	lduw	[%o1+4],%g1
	lduw	[%o2+4],%g2
	lduw	[%o1+8],%g3
	lduw	[%o2+8],%g4
	subccc	%o4,%o5,%o5
	stuw	%o5,[%o0]

	lduw	[%o1+12],%o4
	lduw	[%o2+12],%o5
	inc	16,%o1
	subccc	%g1,%g2,%g2
	stuw	%g2,[%o0+4]

	inc	16,%o2
	subccc	%g3,%g4,%g4
	stuw	%g4,[%o0+8]

	inc	16,%o0
	subccc	%o4,%o5,%o5
	stuw	%o5,[%o0-4]
	and	%o3,-4,%g1
	brnz,a,pt	%g1,.L_bn_sub_words_loop
	lduw	[%o1],%o4

	brnz,a,pn	%o3,.L_bn_sub_words_tail
	lduw	[%o1],%o4
.L_bn_sub_words_return:
	clr	%o0
	retl
	movcs	%icc,1,%o0
	nop

.L_bn_sub_words_tail:		! wow! 32 aligned!
	lduw	[%o2],%o5
	dec	%o3
	subccc	%o4,%o5,%o5
	brz,pt	%o3,.L_bn_sub_words_return
	stuw	%o5,[%o0]

	lduw	[%o1+4],%o4
	lduw	[%o2+4],%o5
	dec	%o3
	subccc	%o4,%o5,%o5
	brz,pt	%o3,.L_bn_sub_words_return
	stuw	%o5,[%o0+4]

	lduw	[%o1+8],%o4
	lduw	[%o2+8],%o5
	subccc	%o4,%o5,%o5
	stuw	%o5,[%o0+8]
	clr	%o0
	retl
	movcs	%icc,1,%o0

.type	bn_sub_words,#function
.size	bn_sub_words,(.-bn_sub_words)

/*
 * Code below depends on the fact that upper parts of the %l0-%l7
 * and %i0-%i7 are zeroed by kernel after context switch. In
 * previous versions this comment stated that "the trouble is that
 * it's not feasible to implement the mumbo-jumbo in less V9
 * instructions:-(" which apparently isn't true thanks to
 * 'bcs,a %xcc,.+8; inc %rd' pair. But the performance improvement
 * results not from the shorter code, but from elimination of
 * multicycle none-pairable 'rd %y,%rd' instructions.
 *
 *							Andy.
 */

/*
 * Here is register usage map for *all* routines below.
 */
#define t_1	%o0
#define	t_2	%o1
#define c_12	%o2
#define c_3	%o3

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
#define	b_3	%o4
#define	b_4	%o5
#define	b_5	%o7
#define	b_6	%g1
#define	b_7	%g4

.align	32
.global bn_mul_comba8
/*
 * void bn_mul_comba8(r,a,b)
 * BN_ULONG *r,*a,*b;
 */
bn_mul_comba8:
	save	%sp,FRAME_SIZE,%sp
	mov	1,t_2
	lduw	ap(0),a_0
	sllx	t_2,32,t_2
	lduw	bp(0),b_0	!=
	lduw	bp(1),b_1
	mulx	a_0,b_0,t_1	!mul_add_c(a[0],b[0],c1,c2,c3);
	srlx	t_1,32,c_12
	stuw	t_1,rp(0)	!=!r[0]=c1;

	lduw	ap(1),a_1
	mulx	a_0,b_1,t_1	!mul_add_c(a[0],b[1],c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3		!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	ap(2),a_2
	mulx	a_1,b_0,t_1	!=!mul_add_c(a[1],b[0],c2,c3,c1);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12	!=
	stuw	t_1,rp(1)	!r[1]=c2;
	or	c_12,c_3,c_12

	mulx	a_2,b_0,t_1	!mul_add_c(a[2],b[0],c3,c1,c2);
	addcc	c_12,t_1,c_12	!=
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	bp(2),b_2	!=
	mulx	a_1,b_1,t_1	!mul_add_c(a[1],b[1],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	lduw	bp(3),b_3
	mulx	a_0,b_2,t_1	!mul_add_c(a[0],b[2],c3,c1,c2);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(2)	!r[2]=c3;
	or	c_12,c_3,c_12	!=

	mulx	a_0,b_3,t_1	!mul_add_c(a[0],b[3],c1,c2,c3);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_1,b_2,t_1	!=!mul_add_c(a[1],b[2],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	lduw	ap(3),a_3
	mulx	a_2,b_1,t_1	!mul_add_c(a[2],b[1],c1,c2,c3);
	addcc	c_12,t_1,c_12	!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	ap(4),a_4
	mulx	a_3,b_0,t_1	!=!mul_add_c(a[3],b[0],c1,c2,c3);!=
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12	!=
	stuw	t_1,rp(3)	!r[3]=c1;
	or	c_12,c_3,c_12

	mulx	a_4,b_0,t_1	!mul_add_c(a[4],b[0],c2,c3,c1);
	addcc	c_12,t_1,c_12	!=
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_3,b_1,t_1	!=!mul_add_c(a[3],b[1],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_2,b_2,t_1	!=!mul_add_c(a[2],b[2],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	bp(4),b_4	!=
	mulx	a_1,b_3,t_1	!mul_add_c(a[1],b[3],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	lduw	bp(5),b_5
	mulx	a_0,b_4,t_1	!mul_add_c(a[0],b[4],c2,c3,c1);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(4)	!r[4]=c2;
	or	c_12,c_3,c_12	!=

	mulx	a_0,b_5,t_1	!mul_add_c(a[0],b[5],c3,c1,c2);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_1,b_4,t_1	!mul_add_c(a[1],b[4],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_2,b_3,t_1	!mul_add_c(a[2],b[3],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_3,b_2,t_1	!mul_add_c(a[3],b[2],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	lduw	ap(5),a_5
	mulx	a_4,b_1,t_1	!mul_add_c(a[4],b[1],c3,c1,c2);
	addcc	c_12,t_1,c_12	!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	ap(6),a_6
	mulx	a_5,b_0,t_1	!=!mul_add_c(a[5],b[0],c3,c1,c2);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12	!=
	stuw	t_1,rp(5)	!r[5]=c3;
	or	c_12,c_3,c_12

	mulx	a_6,b_0,t_1	!mul_add_c(a[6],b[0],c1,c2,c3);
	addcc	c_12,t_1,c_12	!=
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_5,b_1,t_1	!=!mul_add_c(a[5],b[1],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_4,b_2,t_1	!=!mul_add_c(a[4],b[2],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_3,b_3,t_1	!=!mul_add_c(a[3],b[3],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_2,b_4,t_1	!=!mul_add_c(a[2],b[4],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	bp(6),b_6	!=
	mulx	a_1,b_5,t_1	!mul_add_c(a[1],b[5],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	lduw	bp(7),b_7
	mulx	a_0,b_6,t_1	!mul_add_c(a[0],b[6],c1,c2,c3);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(6)	!r[6]=c1;
	or	c_12,c_3,c_12	!=

	mulx	a_0,b_7,t_1	!mul_add_c(a[0],b[7],c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_1,b_6,t_1	!mul_add_c(a[1],b[6],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_2,b_5,t_1	!mul_add_c(a[2],b[5],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_3,b_4,t_1	!mul_add_c(a[3],b[4],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_4,b_3,t_1	!mul_add_c(a[4],b[3],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_5,b_2,t_1	!mul_add_c(a[5],b[2],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	lduw	ap(7),a_7
	mulx	a_6,b_1,t_1	!=!mul_add_c(a[6],b[1],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_7,b_0,t_1	!=!mul_add_c(a[7],b[0],c2,c3,c1);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12	!=
	stuw	t_1,rp(7)	!r[7]=c2;
	or	c_12,c_3,c_12

	mulx	a_7,b_1,t_1	!=!mul_add_c(a[7],b[1],c3,c1,c2);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	mulx	a_6,b_2,t_1	!mul_add_c(a[6],b[2],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	mulx	a_5,b_3,t_1	!mul_add_c(a[5],b[3],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	mulx	a_4,b_4,t_1	!mul_add_c(a[4],b[4],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	mulx	a_3,b_5,t_1	!mul_add_c(a[3],b[5],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	mulx	a_2,b_6,t_1	!mul_add_c(a[2],b[6],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	mulx	a_1,b_7,t_1	!mul_add_c(a[1],b[7],c3,c1,c2);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	srlx	t_1,32,c_12
	stuw	t_1,rp(8)	!r[8]=c3;
	or	c_12,c_3,c_12

	mulx	a_2,b_7,t_1	!=!mul_add_c(a[2],b[7],c1,c2,c3);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	mulx	a_3,b_6,t_1	!mul_add_c(a[3],b[6],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_4,b_5,t_1	!mul_add_c(a[4],b[5],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_5,b_4,t_1	!mul_add_c(a[5],b[4],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_6,b_3,t_1	!mul_add_c(a[6],b[3],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_7,b_2,t_1	!mul_add_c(a[7],b[2],c1,c2,c3);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(9)	!r[9]=c1;
	or	c_12,c_3,c_12	!=

	mulx	a_7,b_3,t_1	!mul_add_c(a[7],b[3],c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_6,b_4,t_1	!mul_add_c(a[6],b[4],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_5,b_5,t_1	!mul_add_c(a[5],b[5],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_4,b_6,t_1	!mul_add_c(a[4],b[6],c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_3,b_7,t_1	!mul_add_c(a[3],b[7],c2,c3,c1);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(10)	!r[10]=c2;
	or	c_12,c_3,c_12	!=

	mulx	a_4,b_7,t_1	!mul_add_c(a[4],b[7],c3,c1,c2);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_5,b_6,t_1	!mul_add_c(a[5],b[6],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_6,b_5,t_1	!mul_add_c(a[6],b[5],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_7,b_4,t_1	!mul_add_c(a[7],b[4],c3,c1,c2);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(11)	!r[11]=c3;
	or	c_12,c_3,c_12	!=

	mulx	a_7,b_5,t_1	!mul_add_c(a[7],b[5],c1,c2,c3);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_6,b_6,t_1	!mul_add_c(a[6],b[6],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_5,b_7,t_1	!mul_add_c(a[5],b[7],c1,c2,c3);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(12)	!r[12]=c1;
	or	c_12,c_3,c_12	!=

	mulx	a_6,b_7,t_1	!mul_add_c(a[6],b[7],c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_7,b_6,t_1	!mul_add_c(a[7],b[6],c2,c3,c1);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	st	t_1,rp(13)	!r[13]=c2;
	or	c_12,c_3,c_12	!=

	mulx	a_7,b_7,t_1	!mul_add_c(a[7],b[7],c3,c1,c2);
	addcc	c_12,t_1,t_1
	srlx	t_1,32,c_12	!=
	stuw	t_1,rp(14)	!r[14]=c3;
	stuw	c_12,rp(15)	!r[15]=c1;

	ret
	restore	%g0,%g0,%o0	!=

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
	lduw	ap(0),a_0
	mov	1,t_2
	lduw	bp(0),b_0
	sllx	t_2,32,t_2	!=
	lduw	bp(1),b_1
	mulx	a_0,b_0,t_1	!mul_add_c(a[0],b[0],c1,c2,c3);
	srlx	t_1,32,c_12
	stuw	t_1,rp(0)	!=!r[0]=c1;

	lduw	ap(1),a_1
	mulx	a_0,b_1,t_1	!mul_add_c(a[0],b[1],c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3		!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	ap(2),a_2
	mulx	a_1,b_0,t_1	!=!mul_add_c(a[1],b[0],c2,c3,c1);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12	!=
	stuw	t_1,rp(1)	!r[1]=c2;
	or	c_12,c_3,c_12

	mulx	a_2,b_0,t_1	!mul_add_c(a[2],b[0],c3,c1,c2);
	addcc	c_12,t_1,c_12	!=
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	bp(2),b_2	!=
	mulx	a_1,b_1,t_1	!mul_add_c(a[1],b[1],c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3	!=
	lduw	bp(3),b_3
	mulx	a_0,b_2,t_1	!mul_add_c(a[0],b[2],c3,c1,c2);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(2)	!r[2]=c3;
	or	c_12,c_3,c_12	!=

	mulx	a_0,b_3,t_1	!mul_add_c(a[0],b[3],c1,c2,c3);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	mulx	a_1,b_2,t_1	!mul_add_c(a[1],b[2],c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8	!=
	add	c_3,t_2,c_3
	lduw	ap(3),a_3
	mulx	a_2,b_1,t_1	!mul_add_c(a[2],b[1],c1,c2,c3);
	addcc	c_12,t_1,c_12	!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_3,b_0,t_1	!mul_add_c(a[3],b[0],c1,c2,c3);!=
	addcc	c_12,t_1,t_1	!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(3)	!=!r[3]=c1;
	or	c_12,c_3,c_12

	mulx	a_3,b_1,t_1	!mul_add_c(a[3],b[1],c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3		!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_2,b_2,t_1	!mul_add_c(a[2],b[2],c2,c3,c1);
	addcc	c_12,t_1,c_12	!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_1,b_3,t_1	!mul_add_c(a[1],b[3],c2,c3,c1);
	addcc	c_12,t_1,t_1	!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(4)	!=!r[4]=c2;
	or	c_12,c_3,c_12

	mulx	a_2,b_3,t_1	!mul_add_c(a[2],b[3],c3,c1,c2);
	addcc	c_12,t_1,c_12
	clr	c_3		!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_3,b_2,t_1	!mul_add_c(a[3],b[2],c3,c1,c2);
	addcc	c_12,t_1,t_1	!=
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(5)	!=!r[5]=c3;
	or	c_12,c_3,c_12

	mulx	a_3,b_3,t_1	!mul_add_c(a[3],b[3],c1,c2,c3);
	addcc	c_12,t_1,t_1
	srlx	t_1,32,c_12	!=
	stuw	t_1,rp(6)	!r[6]=c1;
	stuw	c_12,rp(7)	!r[7]=c2;
	
	ret
	restore	%g0,%g0,%o0

.type	bn_mul_comba4,#function
.size	bn_mul_comba4,(.-bn_mul_comba4)

.align	32

.global bn_sqr_comba8
bn_sqr_comba8:
	save	%sp,FRAME_SIZE,%sp
	mov	1,t_2
	lduw	ap(0),a_0
	sllx	t_2,32,t_2
	lduw	ap(1),a_1
	mulx	a_0,a_0,t_1	!sqr_add_c(a,0,c1,c2,c3);
	srlx	t_1,32,c_12
	stuw	t_1,rp(0)	!r[0]=c1;

	lduw	ap(2),a_2
	mulx	a_0,a_1,t_1	!=!sqr_add_c2(a,1,0,c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(1)	!r[1]=c2;
	or	c_12,c_3,c_12

	mulx	a_2,a_0,t_1	!sqr_add_c2(a,2,0,c3,c1,c2);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	ap(3),a_3
	mulx	a_1,a_1,t_1	!sqr_add_c(a,1,c3,c1,c2);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(2)	!r[2]=c3;
	or	c_12,c_3,c_12

	mulx	a_0,a_3,t_1	!sqr_add_c2(a,3,0,c1,c2,c3);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	ap(4),a_4
	mulx	a_1,a_2,t_1	!sqr_add_c2(a,2,1,c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	st	t_1,rp(3)	!r[3]=c1;
	or	c_12,c_3,c_12

	mulx	a_4,a_0,t_1	!sqr_add_c2(a,4,0,c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_3,a_1,t_1	!sqr_add_c2(a,3,1,c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	ap(5),a_5
	mulx	a_2,a_2,t_1	!sqr_add_c(a,2,c2,c3,c1);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(4)	!r[4]=c2;
	or	c_12,c_3,c_12

	mulx	a_0,a_5,t_1	!sqr_add_c2(a,5,0,c3,c1,c2);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_1,a_4,t_1	!sqr_add_c2(a,4,1,c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	ap(6),a_6
	mulx	a_2,a_3,t_1	!sqr_add_c2(a,3,2,c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(5)	!r[5]=c3;
	or	c_12,c_3,c_12

	mulx	a_6,a_0,t_1	!sqr_add_c2(a,6,0,c1,c2,c3);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_5,a_1,t_1	!sqr_add_c2(a,5,1,c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_4,a_2,t_1	!sqr_add_c2(a,4,2,c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	ap(7),a_7
	mulx	a_3,a_3,t_1	!=!sqr_add_c(a,3,c1,c2,c3);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(6)	!r[6]=c1;
	or	c_12,c_3,c_12

	mulx	a_0,a_7,t_1	!sqr_add_c2(a,7,0,c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_1,a_6,t_1	!sqr_add_c2(a,6,1,c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_2,a_5,t_1	!sqr_add_c2(a,5,2,c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_3,a_4,t_1	!sqr_add_c2(a,4,3,c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(7)	!r[7]=c2;
	or	c_12,c_3,c_12

	mulx	a_7,a_1,t_1	!sqr_add_c2(a,7,1,c3,c1,c2);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_6,a_2,t_1	!sqr_add_c2(a,6,2,c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_5,a_3,t_1	!sqr_add_c2(a,5,3,c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_4,a_4,t_1	!sqr_add_c(a,4,c3,c1,c2);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(8)	!r[8]=c3;
	or	c_12,c_3,c_12

	mulx	a_2,a_7,t_1	!sqr_add_c2(a,7,2,c1,c2,c3);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_3,a_6,t_1	!sqr_add_c2(a,6,3,c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_4,a_5,t_1	!sqr_add_c2(a,5,4,c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(9)	!r[9]=c1;
	or	c_12,c_3,c_12

	mulx	a_7,a_3,t_1	!sqr_add_c2(a,7,3,c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_6,a_4,t_1	!sqr_add_c2(a,6,4,c2,c3,c1);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_5,a_5,t_1	!sqr_add_c(a,5,c2,c3,c1);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(10)	!r[10]=c2;
	or	c_12,c_3,c_12

	mulx	a_4,a_7,t_1	!sqr_add_c2(a,7,4,c3,c1,c2);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_5,a_6,t_1	!sqr_add_c2(a,6,5,c3,c1,c2);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(11)	!r[11]=c3;
	or	c_12,c_3,c_12

	mulx	a_7,a_5,t_1	!sqr_add_c2(a,7,5,c1,c2,c3);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_6,a_6,t_1	!sqr_add_c(a,6,c1,c2,c3);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(12)	!r[12]=c1;
	or	c_12,c_3,c_12

	mulx	a_6,a_7,t_1	!sqr_add_c2(a,7,6,c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(13)	!r[13]=c2;
	or	c_12,c_3,c_12

	mulx	a_7,a_7,t_1	!sqr_add_c(a,7,c3,c1,c2);
	addcc	c_12,t_1,t_1
	srlx	t_1,32,c_12
	stuw	t_1,rp(14)	!r[14]=c3;
	stuw	c_12,rp(15)	!r[15]=c1;

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
	mov	1,t_2
	lduw	ap(0),a_0
	sllx	t_2,32,t_2
	lduw	ap(1),a_1
	mulx	a_0,a_0,t_1	!sqr_add_c(a,0,c1,c2,c3);
	srlx	t_1,32,c_12
	stuw	t_1,rp(0)	!r[0]=c1;

	lduw	ap(2),a_2
	mulx	a_0,a_1,t_1	!sqr_add_c2(a,1,0,c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(1)	!r[1]=c2;
	or	c_12,c_3,c_12

	mulx	a_2,a_0,t_1	!sqr_add_c2(a,2,0,c3,c1,c2);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	lduw	ap(3),a_3
	mulx	a_1,a_1,t_1	!sqr_add_c(a,1,c3,c1,c2);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(2)	!r[2]=c3;
	or	c_12,c_3,c_12

	mulx	a_0,a_3,t_1	!sqr_add_c2(a,3,0,c1,c2,c3);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_1,a_2,t_1	!sqr_add_c2(a,2,1,c1,c2,c3);
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(3)	!r[3]=c1;
	or	c_12,c_3,c_12

	mulx	a_3,a_1,t_1	!sqr_add_c2(a,3,1,c2,c3,c1);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,c_12
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	mulx	a_2,a_2,t_1	!sqr_add_c(a,2,c2,c3,c1);
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(4)	!r[4]=c2;
	or	c_12,c_3,c_12

	mulx	a_2,a_3,t_1	!sqr_add_c2(a,3,2,c3,c1,c2);
	addcc	c_12,t_1,c_12
	clr	c_3
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	addcc	c_12,t_1,t_1
	bcs,a	%xcc,.+8
	add	c_3,t_2,c_3
	srlx	t_1,32,c_12
	stuw	t_1,rp(5)	!r[5]=c3;
	or	c_12,c_3,c_12

	mulx	a_3,a_3,t_1	!sqr_add_c(a,3,c1,c2,c3);
	addcc	c_12,t_1,t_1
	srlx	t_1,32,c_12
	stuw	t_1,rp(6)	!r[6]=c1;
	stuw	c_12,rp(7)	!r[7]=c2;
	
	ret
	restore	%g0,%g0,%o0

.type	bn_sqr_comba4,#function
.size	bn_sqr_comba4,(.-bn_sqr_comba4)

.align	32

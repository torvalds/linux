.ident "s390x.S, version 1.1"
// ====================================================================
// Copyright 2007-2016 The OpenSSL Project Authors. All Rights Reserved.
//
// Licensed under the OpenSSL license (the "License").  You may not use
// this file except in compliance with the License.  You can obtain a copy
// in the file LICENSE in the source distribution or at
// https://www.openssl.org/source/license.html
// ====================================================================

.text

#define zero	%r0

// BN_ULONG bn_mul_add_words(BN_ULONG *r2,BN_ULONG *r3,int r4,BN_ULONG r5);
.globl	bn_mul_add_words
.type	bn_mul_add_words,@function
.align	4
bn_mul_add_words:
	lghi	zero,0		// zero = 0
	la	%r1,0(%r2)	// put rp aside [to give way to]
	lghi	%r2,0		// return value
	ltgfr	%r4,%r4
	bler	%r14		// if (len<=0) return 0;

	stmg	%r6,%r13,48(%r15)
	lghi	%r2,3
	lghi	%r12,0		// carry = 0
	slgr	%r1,%r3		// rp-=ap
	nr	%r2,%r4		// len%4
	sra	%r4,2		// cnt=len/4
	jz	.Loop1_madd	// carry is incidentally cleared if branch taken
	algr	zero,zero	// clear carry

	lg	%r7,0(%r3)	// ap[0]
	lg	%r9,8(%r3)	// ap[1]
	mlgr	%r6,%r5		// *=w
	brct	%r4,.Loop4_madd
	j	.Loop4_madd_tail

.Loop4_madd:
	mlgr	%r8,%r5
	lg	%r11,16(%r3)	// ap[i+2]
	alcgr	%r7,%r12	// +=carry
	alcgr	%r6,zero
	alg	%r7,0(%r3,%r1)	// +=rp[i]
	stg	%r7,0(%r3,%r1)	// rp[i]=

	mlgr	%r10,%r5
	lg	%r13,24(%r3)
	alcgr	%r9,%r6
	alcgr	%r8,zero
	alg	%r9,8(%r3,%r1)
	stg	%r9,8(%r3,%r1)

	mlgr	%r12,%r5
	lg	%r7,32(%r3)
	alcgr	%r11,%r8
	alcgr	%r10,zero
	alg	%r11,16(%r3,%r1)
	stg	%r11,16(%r3,%r1)

	mlgr	%r6,%r5
	lg	%r9,40(%r3)
	alcgr	%r13,%r10
	alcgr	%r12,zero
	alg	%r13,24(%r3,%r1)
	stg	%r13,24(%r3,%r1)

	la	%r3,32(%r3)	// i+=4
	brct	%r4,.Loop4_madd

.Loop4_madd_tail:
	mlgr	%r8,%r5
	lg	%r11,16(%r3)
	alcgr	%r7,%r12	// +=carry
	alcgr	%r6,zero
	alg	%r7,0(%r3,%r1)	// +=rp[i]
	stg	%r7,0(%r3,%r1)	// rp[i]=

	mlgr	%r10,%r5
	lg	%r13,24(%r3)
	alcgr	%r9,%r6
	alcgr	%r8,zero
	alg	%r9,8(%r3,%r1)
	stg	%r9,8(%r3,%r1)

	mlgr	%r12,%r5
	alcgr	%r11,%r8
	alcgr	%r10,zero
	alg	%r11,16(%r3,%r1)
	stg	%r11,16(%r3,%r1)

	alcgr	%r13,%r10
	alcgr	%r12,zero
	alg	%r13,24(%r3,%r1)
	stg	%r13,24(%r3,%r1)

	la	%r3,32(%r3)	// i+=4

	la	%r2,1(%r2)	// see if len%4 is zero ...
	brct	%r2,.Loop1_madd	// without touching condition code:-)

.Lend_madd:
	lgr	%r2,zero	// return value
	alcgr	%r2,%r12	// collect even carry bit
	lmg	%r6,%r13,48(%r15)
	br	%r14

.Loop1_madd:
	lg	%r7,0(%r3)	// ap[i]
	mlgr	%r6,%r5		// *=w
	alcgr	%r7,%r12	// +=carry
	alcgr	%r6,zero
	alg	%r7,0(%r3,%r1)	// +=rp[i]
	stg	%r7,0(%r3,%r1)	// rp[i]=

	lgr	%r12,%r6
	la	%r3,8(%r3)	// i++
	brct	%r2,.Loop1_madd

	j	.Lend_madd
.size	bn_mul_add_words,.-bn_mul_add_words

// BN_ULONG bn_mul_words(BN_ULONG *r2,BN_ULONG *r3,int r4,BN_ULONG r5);
.globl	bn_mul_words
.type	bn_mul_words,@function
.align	4
bn_mul_words:
	lghi	zero,0		// zero = 0
	la	%r1,0(%r2)	// put rp aside
	lghi	%r2,0		// i=0;
	ltgfr	%r4,%r4
	bler	%r14		// if (len<=0) return 0;

	stmg	%r6,%r10,48(%r15)
	lghi	%r10,3
	lghi	%r8,0		// carry = 0
	nr	%r10,%r4	// len%4
	sra	%r4,2		// cnt=len/4
	jz	.Loop1_mul	// carry is incidentally cleared if branch taken
	algr	zero,zero	// clear carry

.Loop4_mul:
	lg	%r7,0(%r2,%r3)	// ap[i]
	mlgr	%r6,%r5		// *=w
	alcgr	%r7,%r8		// +=carry
	stg	%r7,0(%r2,%r1)	// rp[i]=

	lg	%r9,8(%r2,%r3)
	mlgr	%r8,%r5
	alcgr	%r9,%r6
	stg	%r9,8(%r2,%r1)

	lg	%r7,16(%r2,%r3)
	mlgr	%r6,%r5
	alcgr	%r7,%r8
	stg	%r7,16(%r2,%r1)

	lg	%r9,24(%r2,%r3)
	mlgr	%r8,%r5
	alcgr	%r9,%r6
	stg	%r9,24(%r2,%r1)

	la	%r2,32(%r2)	// i+=4
	brct	%r4,.Loop4_mul

	la	%r10,1(%r10)		// see if len%4 is zero ...
	brct	%r10,.Loop1_mul		// without touching condition code:-)

.Lend_mul:
	alcgr	%r8,zero	// collect carry bit
	lgr	%r2,%r8
	lmg	%r6,%r10,48(%r15)
	br	%r14

.Loop1_mul:
	lg	%r7,0(%r2,%r3)	// ap[i]
	mlgr	%r6,%r5		// *=w
	alcgr	%r7,%r8		// +=carry
	stg	%r7,0(%r2,%r1)	// rp[i]=

	lgr	%r8,%r6
	la	%r2,8(%r2)	// i++
	brct	%r10,.Loop1_mul

	j	.Lend_mul
.size	bn_mul_words,.-bn_mul_words

// void bn_sqr_words(BN_ULONG *r2,BN_ULONG *r2,int r4)
.globl	bn_sqr_words
.type	bn_sqr_words,@function
.align	4
bn_sqr_words:
	ltgfr	%r4,%r4
	bler	%r14

	stmg	%r6,%r7,48(%r15)
	srag	%r1,%r4,2	// cnt=len/4
	jz	.Loop1_sqr

.Loop4_sqr:
	lg	%r7,0(%r3)
	mlgr	%r6,%r7
	stg	%r7,0(%r2)
	stg	%r6,8(%r2)

	lg	%r7,8(%r3)
	mlgr	%r6,%r7
	stg	%r7,16(%r2)
	stg	%r6,24(%r2)

	lg	%r7,16(%r3)
	mlgr	%r6,%r7
	stg	%r7,32(%r2)
	stg	%r6,40(%r2)

	lg	%r7,24(%r3)
	mlgr	%r6,%r7
	stg	%r7,48(%r2)
	stg	%r6,56(%r2)

	la	%r3,32(%r3)
	la	%r2,64(%r2)
	brct	%r1,.Loop4_sqr

	lghi	%r1,3
	nr	%r4,%r1		// cnt=len%4
	jz	.Lend_sqr

.Loop1_sqr:
	lg	%r7,0(%r3)
	mlgr	%r6,%r7
	stg	%r7,0(%r2)
	stg	%r6,8(%r2)

	la	%r3,8(%r3)
	la	%r2,16(%r2)
	brct	%r4,.Loop1_sqr

.Lend_sqr:
	lmg	%r6,%r7,48(%r15)
	br	%r14
.size	bn_sqr_words,.-bn_sqr_words

// BN_ULONG bn_div_words(BN_ULONG h,BN_ULONG l,BN_ULONG d);
.globl	bn_div_words
.type	bn_div_words,@function
.align	4
bn_div_words:
	dlgr	%r2,%r4
	lgr	%r2,%r3
	br	%r14
.size	bn_div_words,.-bn_div_words

// BN_ULONG bn_add_words(BN_ULONG *r2,BN_ULONG *r3,BN_ULONG *r4,int r5);
.globl	bn_add_words
.type	bn_add_words,@function
.align	4
bn_add_words:
	la	%r1,0(%r2)	// put rp aside
	lghi	%r2,0		// i=0
	ltgfr	%r5,%r5
	bler	%r14		// if (len<=0) return 0;

	stg	%r6,48(%r15)
	lghi	%r6,3
	nr	%r6,%r5		// len%4
	sra	%r5,2		// len/4, use sra because it sets condition code
	jz	.Loop1_add	// carry is incidentally cleared if branch taken
	algr	%r2,%r2		// clear carry

.Loop4_add:
	lg	%r0,0(%r2,%r3)
	alcg	%r0,0(%r2,%r4)
	stg	%r0,0(%r2,%r1)
	lg	%r0,8(%r2,%r3)
	alcg	%r0,8(%r2,%r4)
	stg	%r0,8(%r2,%r1)
	lg	%r0,16(%r2,%r3)
	alcg	%r0,16(%r2,%r4)
	stg	%r0,16(%r2,%r1)
	lg	%r0,24(%r2,%r3)
	alcg	%r0,24(%r2,%r4)
	stg	%r0,24(%r2,%r1)

	la	%r2,32(%r2)	// i+=4
	brct	%r5,.Loop4_add

	la	%r6,1(%r6)	// see if len%4 is zero ...
	brct	%r6,.Loop1_add	// without touching condition code:-)

.Lexit_add:
	lghi	%r2,0
	alcgr	%r2,%r2
	lg	%r6,48(%r15)
	br	%r14

.Loop1_add:
	lg	%r0,0(%r2,%r3)
	alcg	%r0,0(%r2,%r4)
	stg	%r0,0(%r2,%r1)

	la	%r2,8(%r2)	// i++
	brct	%r6,.Loop1_add

	j	.Lexit_add
.size	bn_add_words,.-bn_add_words

// BN_ULONG bn_sub_words(BN_ULONG *r2,BN_ULONG *r3,BN_ULONG *r4,int r5);
.globl	bn_sub_words
.type	bn_sub_words,@function
.align	4
bn_sub_words:
	la	%r1,0(%r2)	// put rp aside
	lghi	%r2,0		// i=0
	ltgfr	%r5,%r5
	bler	%r14		// if (len<=0) return 0;

	stg	%r6,48(%r15)
	lghi	%r6,3
	nr	%r6,%r5		// len%4
	sra	%r5,2		// len/4, use sra because it sets condition code
	jnz	.Loop4_sub	// borrow is incidentally cleared if branch taken
	slgr	%r2,%r2		// clear borrow

.Loop1_sub:
	lg	%r0,0(%r2,%r3)
	slbg	%r0,0(%r2,%r4)
	stg	%r0,0(%r2,%r1)

	la	%r2,8(%r2)	// i++
	brct	%r6,.Loop1_sub
	j	.Lexit_sub

.Loop4_sub:
	lg	%r0,0(%r2,%r3)
	slbg	%r0,0(%r2,%r4)
	stg	%r0,0(%r2,%r1)
	lg	%r0,8(%r2,%r3)
	slbg	%r0,8(%r2,%r4)
	stg	%r0,8(%r2,%r1)
	lg	%r0,16(%r2,%r3)
	slbg	%r0,16(%r2,%r4)
	stg	%r0,16(%r2,%r1)
	lg	%r0,24(%r2,%r3)
	slbg	%r0,24(%r2,%r4)
	stg	%r0,24(%r2,%r1)

	la	%r2,32(%r2)	// i+=4
	brct	%r5,.Loop4_sub

	la	%r6,1(%r6)	// see if len%4 is zero ...
	brct	%r6,.Loop1_sub	// without touching condition code:-)

.Lexit_sub:
	lghi	%r2,0
	slbgr	%r2,%r2
	lcgr	%r2,%r2
	lg	%r6,48(%r15)
	br	%r14
.size	bn_sub_words,.-bn_sub_words

#define c1	%r1
#define c2	%r5
#define c3	%r8

#define mul_add_c(ai,bi,c1,c2,c3)	\
	lg	%r7,ai*8(%r3);		\
	mlg	%r6,bi*8(%r4);		\
	algr	c1,%r7;			\
	alcgr	c2,%r6;			\
	alcgr	c3,zero

// void bn_mul_comba8(BN_ULONG *r2,BN_ULONG *r3,BN_ULONG *r4);
.globl	bn_mul_comba8
.type	bn_mul_comba8,@function
.align	4
bn_mul_comba8:
	stmg	%r6,%r8,48(%r15)

	lghi	c1,0
	lghi	c2,0
	lghi	c3,0
	lghi	zero,0

	mul_add_c(0,0,c1,c2,c3);
	stg	c1,0*8(%r2)
	lghi	c1,0

	mul_add_c(0,1,c2,c3,c1);
	mul_add_c(1,0,c2,c3,c1);
	stg	c2,1*8(%r2)
	lghi	c2,0

	mul_add_c(2,0,c3,c1,c2);
	mul_add_c(1,1,c3,c1,c2);
	mul_add_c(0,2,c3,c1,c2);
	stg	c3,2*8(%r2)
	lghi	c3,0

	mul_add_c(0,3,c1,c2,c3);
	mul_add_c(1,2,c1,c2,c3);
	mul_add_c(2,1,c1,c2,c3);
	mul_add_c(3,0,c1,c2,c3);
	stg	c1,3*8(%r2)
	lghi	c1,0

	mul_add_c(4,0,c2,c3,c1);
	mul_add_c(3,1,c2,c3,c1);
	mul_add_c(2,2,c2,c3,c1);
	mul_add_c(1,3,c2,c3,c1);
	mul_add_c(0,4,c2,c3,c1);
	stg	c2,4*8(%r2)
	lghi	c2,0

	mul_add_c(0,5,c3,c1,c2);
	mul_add_c(1,4,c3,c1,c2);
	mul_add_c(2,3,c3,c1,c2);
	mul_add_c(3,2,c3,c1,c2);
	mul_add_c(4,1,c3,c1,c2);
	mul_add_c(5,0,c3,c1,c2);
	stg	c3,5*8(%r2)
	lghi	c3,0

	mul_add_c(6,0,c1,c2,c3);
	mul_add_c(5,1,c1,c2,c3);
	mul_add_c(4,2,c1,c2,c3);
	mul_add_c(3,3,c1,c2,c3);
	mul_add_c(2,4,c1,c2,c3);
	mul_add_c(1,5,c1,c2,c3);
	mul_add_c(0,6,c1,c2,c3);
	stg	c1,6*8(%r2)
	lghi	c1,0

	mul_add_c(0,7,c2,c3,c1);
	mul_add_c(1,6,c2,c3,c1);
	mul_add_c(2,5,c2,c3,c1);
	mul_add_c(3,4,c2,c3,c1);
	mul_add_c(4,3,c2,c3,c1);
	mul_add_c(5,2,c2,c3,c1);
	mul_add_c(6,1,c2,c3,c1);
	mul_add_c(7,0,c2,c3,c1);
	stg	c2,7*8(%r2)
	lghi	c2,0

	mul_add_c(7,1,c3,c1,c2);
	mul_add_c(6,2,c3,c1,c2);
	mul_add_c(5,3,c3,c1,c2);
	mul_add_c(4,4,c3,c1,c2);
	mul_add_c(3,5,c3,c1,c2);
	mul_add_c(2,6,c3,c1,c2);
	mul_add_c(1,7,c3,c1,c2);
	stg	c3,8*8(%r2)
	lghi	c3,0

	mul_add_c(2,7,c1,c2,c3);
	mul_add_c(3,6,c1,c2,c3);
	mul_add_c(4,5,c1,c2,c3);
	mul_add_c(5,4,c1,c2,c3);
	mul_add_c(6,3,c1,c2,c3);
	mul_add_c(7,2,c1,c2,c3);
	stg	c1,9*8(%r2)
	lghi	c1,0

	mul_add_c(7,3,c2,c3,c1);
	mul_add_c(6,4,c2,c3,c1);
	mul_add_c(5,5,c2,c3,c1);
	mul_add_c(4,6,c2,c3,c1);
	mul_add_c(3,7,c2,c3,c1);
	stg	c2,10*8(%r2)
	lghi	c2,0

	mul_add_c(4,7,c3,c1,c2);
	mul_add_c(5,6,c3,c1,c2);
	mul_add_c(6,5,c3,c1,c2);
	mul_add_c(7,4,c3,c1,c2);
	stg	c3,11*8(%r2)
	lghi	c3,0

	mul_add_c(7,5,c1,c2,c3);
	mul_add_c(6,6,c1,c2,c3);
	mul_add_c(5,7,c1,c2,c3);
	stg	c1,12*8(%r2)
	lghi	c1,0


	mul_add_c(6,7,c2,c3,c1);
	mul_add_c(7,6,c2,c3,c1);
	stg	c2,13*8(%r2)
	lghi	c2,0

	mul_add_c(7,7,c3,c1,c2);
	stg	c3,14*8(%r2)
	stg	c1,15*8(%r2)

	lmg	%r6,%r8,48(%r15)
	br	%r14
.size	bn_mul_comba8,.-bn_mul_comba8

// void bn_mul_comba4(BN_ULONG *r2,BN_ULONG *r3,BN_ULONG *r4);
.globl	bn_mul_comba4
.type	bn_mul_comba4,@function
.align	4
bn_mul_comba4:
	stmg	%r6,%r8,48(%r15)

	lghi	c1,0
	lghi	c2,0
	lghi	c3,0
	lghi	zero,0

	mul_add_c(0,0,c1,c2,c3);
	stg	c1,0*8(%r3)
	lghi	c1,0

	mul_add_c(0,1,c2,c3,c1);
	mul_add_c(1,0,c2,c3,c1);
	stg	c2,1*8(%r2)
	lghi	c2,0

	mul_add_c(2,0,c3,c1,c2);
	mul_add_c(1,1,c3,c1,c2);
	mul_add_c(0,2,c3,c1,c2);
	stg	c3,2*8(%r2)
	lghi	c3,0

	mul_add_c(0,3,c1,c2,c3);
	mul_add_c(1,2,c1,c2,c3);
	mul_add_c(2,1,c1,c2,c3);
	mul_add_c(3,0,c1,c2,c3);
	stg	c1,3*8(%r2)
	lghi	c1,0

	mul_add_c(3,1,c2,c3,c1);
	mul_add_c(2,2,c2,c3,c1);
	mul_add_c(1,3,c2,c3,c1);
	stg	c2,4*8(%r2)
	lghi	c2,0

	mul_add_c(2,3,c3,c1,c2);
	mul_add_c(3,2,c3,c1,c2);
	stg	c3,5*8(%r2)
	lghi	c3,0

	mul_add_c(3,3,c1,c2,c3);
	stg	c1,6*8(%r2)
	stg	c2,7*8(%r2)

	stmg	%r6,%r8,48(%r15)
	br	%r14
.size	bn_mul_comba4,.-bn_mul_comba4

#define sqr_add_c(ai,c1,c2,c3)		\
	lg	%r7,ai*8(%r3);		\
	mlgr	%r6,%r7;		\
	algr	c1,%r7;			\
	alcgr	c2,%r6;			\
	alcgr	c3,zero

#define sqr_add_c2(ai,aj,c1,c2,c3)	\
	lg	%r7,ai*8(%r3);		\
	mlg	%r6,aj*8(%r3);		\
	algr	c1,%r7;			\
	alcgr	c2,%r6;			\
	alcgr	c3,zero;		\
	algr	c1,%r7;			\
	alcgr	c2,%r6;			\
	alcgr	c3,zero

// void bn_sqr_comba8(BN_ULONG *r2,BN_ULONG *r3);
.globl	bn_sqr_comba8
.type	bn_sqr_comba8,@function
.align	4
bn_sqr_comba8:
	stmg	%r6,%r8,48(%r15)

	lghi	c1,0
	lghi	c2,0
	lghi	c3,0
	lghi	zero,0

	sqr_add_c(0,c1,c2,c3);
	stg	c1,0*8(%r2)
	lghi	c1,0

	sqr_add_c2(1,0,c2,c3,c1);
	stg	c2,1*8(%r2)
	lghi	c2,0

	sqr_add_c(1,c3,c1,c2);
	sqr_add_c2(2,0,c3,c1,c2);
	stg	c3,2*8(%r2)
	lghi	c3,0

	sqr_add_c2(3,0,c1,c2,c3);
	sqr_add_c2(2,1,c1,c2,c3);
	stg	c1,3*8(%r2)
	lghi	c1,0

	sqr_add_c(2,c2,c3,c1);
	sqr_add_c2(3,1,c2,c3,c1);
	sqr_add_c2(4,0,c2,c3,c1);
	stg	c2,4*8(%r2)
	lghi	c2,0

	sqr_add_c2(5,0,c3,c1,c2);
	sqr_add_c2(4,1,c3,c1,c2);
	sqr_add_c2(3,2,c3,c1,c2);
	stg	c3,5*8(%r2)
	lghi	c3,0

	sqr_add_c(3,c1,c2,c3);
	sqr_add_c2(4,2,c1,c2,c3);
	sqr_add_c2(5,1,c1,c2,c3);
	sqr_add_c2(6,0,c1,c2,c3);
	stg	c1,6*8(%r2)
	lghi	c1,0

	sqr_add_c2(7,0,c2,c3,c1);
	sqr_add_c2(6,1,c2,c3,c1);
	sqr_add_c2(5,2,c2,c3,c1);
	sqr_add_c2(4,3,c2,c3,c1);
	stg	c2,7*8(%r2)
	lghi	c2,0

	sqr_add_c(4,c3,c1,c2);
	sqr_add_c2(5,3,c3,c1,c2);
	sqr_add_c2(6,2,c3,c1,c2);
	sqr_add_c2(7,1,c3,c1,c2);
	stg	c3,8*8(%r2)
	lghi	c3,0

	sqr_add_c2(7,2,c1,c2,c3);
	sqr_add_c2(6,3,c1,c2,c3);
	sqr_add_c2(5,4,c1,c2,c3);
	stg	c1,9*8(%r2)
	lghi	c1,0

	sqr_add_c(5,c2,c3,c1);
	sqr_add_c2(6,4,c2,c3,c1);
	sqr_add_c2(7,3,c2,c3,c1);
	stg	c2,10*8(%r2)
	lghi	c2,0

	sqr_add_c2(7,4,c3,c1,c2);
	sqr_add_c2(6,5,c3,c1,c2);
	stg	c3,11*8(%r2)
	lghi	c3,0

	sqr_add_c(6,c1,c2,c3);
	sqr_add_c2(7,5,c1,c2,c3);
	stg	c1,12*8(%r2)
	lghi	c1,0

	sqr_add_c2(7,6,c2,c3,c1);
	stg	c2,13*8(%r2)
	lghi	c2,0

	sqr_add_c(7,c3,c1,c2);
	stg	c3,14*8(%r2)
	stg	c1,15*8(%r2)

	lmg	%r6,%r8,48(%r15)
	br	%r14
.size	bn_sqr_comba8,.-bn_sqr_comba8

// void bn_sqr_comba4(BN_ULONG *r2,BN_ULONG *r3);
.globl bn_sqr_comba4
.type	bn_sqr_comba4,@function
.align	4
bn_sqr_comba4:
	stmg	%r6,%r8,48(%r15)

	lghi	c1,0
	lghi	c2,0
	lghi	c3,0
	lghi	zero,0

	sqr_add_c(0,c1,c2,c3);
	stg	c1,0*8(%r2)
	lghi	c1,0

	sqr_add_c2(1,0,c2,c3,c1);
	stg	c2,1*8(%r2)
	lghi	c2,0

	sqr_add_c(1,c3,c1,c2);
	sqr_add_c2(2,0,c3,c1,c2);
	stg	c3,2*8(%r2)
	lghi	c3,0

	sqr_add_c2(3,0,c1,c2,c3);
	sqr_add_c2(2,1,c1,c2,c3);
	stg	c1,3*8(%r2)
	lghi	c1,0

	sqr_add_c(2,c2,c3,c1);
	sqr_add_c2(3,1,c2,c3,c1);
	stg	c2,4*8(%r2)
	lghi	c2,0

	sqr_add_c2(3,2,c3,c1,c2);
	stg	c3,5*8(%r2)
	lghi	c3,0

	sqr_add_c(3,c1,c2,c3);
	stg	c1,6*8(%r2)
	stg	c2,7*8(%r2)

	lmg	%r6,%r8,48(%r15)
	br	%r14
.size	bn_sqr_comba4,.-bn_sqr_comba4

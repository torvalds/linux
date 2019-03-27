#! /usr/bin/env perl
# Copyright 2015-2019 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# March 2015
#
# "Teaser" Montgomery multiplication module for ARMv8. Needs more
# work. While it does improve RSA sign performance by 20-30% (less for
# longer keys) on most processors, for some reason RSA2048 is not
# faster and RSA4096 goes 15-20% slower on Cortex-A57. Multiplication
# instruction issue rate is limited on processor in question, meaning
# that dedicated squaring procedure is a must. Well, actually all
# contemporary AArch64 processors seem to have limited multiplication
# issue rate, i.e. they can't issue multiplication every cycle, which
# explains moderate improvement coefficients in comparison to
# compiler-generated code. Recall that compiler is instructed to use
# umulh and therefore uses same amount of multiplication instructions
# to do the job. Assembly's edge is to minimize number of "collateral"
# instructions and of course instruction scheduling.
#
# April 2015
#
# Squaring procedure that handles lengths divisible by 8 improves
# RSA/DSA performance by 25-40-60% depending on processor and key
# length. Overall improvement coefficients are always positive in
# comparison to compiler-generated code. On Cortex-A57 improvement
# is still modest on longest key lengths, while others exhibit e.g.
# 50-70% improvement for RSA4096 sign. RSA2048 sign is ~25% faster
# on Cortex-A57 and ~60-100% faster on others.

$flavour = shift;
$output  = shift;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/arm-xlate.pl" and -f $xlate) or
die "can't locate arm-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

($lo0,$hi0,$aj,$m0,$alo,$ahi,
 $lo1,$hi1,$nj,$m1,$nlo,$nhi,
 $ovf, $i,$j,$tp,$tj) = map("x$_",6..17,19..24);

# int bn_mul_mont(
$rp="x0";	# BN_ULONG *rp,
$ap="x1";	# const BN_ULONG *ap,
$bp="x2";	# const BN_ULONG *bp,
$np="x3";	# const BN_ULONG *np,
$n0="x4";	# const BN_ULONG *n0,
$num="x5";	# int num);

$code.=<<___;
.text

.globl	bn_mul_mont
.type	bn_mul_mont,%function
.align	5
bn_mul_mont:
	tst	$num,#7
	b.eq	__bn_sqr8x_mont
	tst	$num,#3
	b.eq	__bn_mul4x_mont
.Lmul_mont:
	stp	x29,x30,[sp,#-64]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]

	ldr	$m0,[$bp],#8		// bp[0]
	sub	$tp,sp,$num,lsl#3
	ldp	$hi0,$aj,[$ap],#16	// ap[0..1]
	lsl	$num,$num,#3
	ldr	$n0,[$n0]		// *n0
	and	$tp,$tp,#-16		// ABI says so
	ldp	$hi1,$nj,[$np],#16	// np[0..1]

	mul	$lo0,$hi0,$m0		// ap[0]*bp[0]
	sub	$j,$num,#16		// j=num-2
	umulh	$hi0,$hi0,$m0
	mul	$alo,$aj,$m0		// ap[1]*bp[0]
	umulh	$ahi,$aj,$m0

	mul	$m1,$lo0,$n0		// "tp[0]"*n0
	mov	sp,$tp			// alloca

	// (*)	mul	$lo1,$hi1,$m1	// np[0]*m1
	umulh	$hi1,$hi1,$m1
	mul	$nlo,$nj,$m1		// np[1]*m1
	// (*)	adds	$lo1,$lo1,$lo0	// discarded
	// (*)	As for removal of first multiplication and addition
	//	instructions. The outcome of first addition is
	//	guaranteed to be zero, which leaves two computationally
	//	significant outcomes: it either carries or not. Then
	//	question is when does it carry? Is there alternative
	//	way to deduce it? If you follow operations, you can
	//	observe that condition for carry is quite simple:
	//	$lo0 being non-zero. So that carry can be calculated
	//	by adding -1 to $lo0. That's what next instruction does.
	subs	xzr,$lo0,#1		// (*)
	umulh	$nhi,$nj,$m1
	adc	$hi1,$hi1,xzr
	cbz	$j,.L1st_skip

.L1st:
	ldr	$aj,[$ap],#8
	adds	$lo0,$alo,$hi0
	sub	$j,$j,#8		// j--
	adc	$hi0,$ahi,xzr

	ldr	$nj,[$np],#8
	adds	$lo1,$nlo,$hi1
	mul	$alo,$aj,$m0		// ap[j]*bp[0]
	adc	$hi1,$nhi,xzr
	umulh	$ahi,$aj,$m0

	adds	$lo1,$lo1,$lo0
	mul	$nlo,$nj,$m1		// np[j]*m1
	adc	$hi1,$hi1,xzr
	umulh	$nhi,$nj,$m1
	str	$lo1,[$tp],#8		// tp[j-1]
	cbnz	$j,.L1st

.L1st_skip:
	adds	$lo0,$alo,$hi0
	sub	$ap,$ap,$num		// rewind $ap
	adc	$hi0,$ahi,xzr

	adds	$lo1,$nlo,$hi1
	sub	$np,$np,$num		// rewind $np
	adc	$hi1,$nhi,xzr

	adds	$lo1,$lo1,$lo0
	sub	$i,$num,#8		// i=num-1
	adcs	$hi1,$hi1,$hi0

	adc	$ovf,xzr,xzr		// upmost overflow bit
	stp	$lo1,$hi1,[$tp]

.Louter:
	ldr	$m0,[$bp],#8		// bp[i]
	ldp	$hi0,$aj,[$ap],#16
	ldr	$tj,[sp]		// tp[0]
	add	$tp,sp,#8

	mul	$lo0,$hi0,$m0		// ap[0]*bp[i]
	sub	$j,$num,#16		// j=num-2
	umulh	$hi0,$hi0,$m0
	ldp	$hi1,$nj,[$np],#16
	mul	$alo,$aj,$m0		// ap[1]*bp[i]
	adds	$lo0,$lo0,$tj
	umulh	$ahi,$aj,$m0
	adc	$hi0,$hi0,xzr

	mul	$m1,$lo0,$n0
	sub	$i,$i,#8		// i--

	// (*)	mul	$lo1,$hi1,$m1	// np[0]*m1
	umulh	$hi1,$hi1,$m1
	mul	$nlo,$nj,$m1		// np[1]*m1
	// (*)	adds	$lo1,$lo1,$lo0
	subs	xzr,$lo0,#1		// (*)
	umulh	$nhi,$nj,$m1
	cbz	$j,.Linner_skip

.Linner:
	ldr	$aj,[$ap],#8
	adc	$hi1,$hi1,xzr
	ldr	$tj,[$tp],#8		// tp[j]
	adds	$lo0,$alo,$hi0
	sub	$j,$j,#8		// j--
	adc	$hi0,$ahi,xzr

	adds	$lo1,$nlo,$hi1
	ldr	$nj,[$np],#8
	adc	$hi1,$nhi,xzr

	mul	$alo,$aj,$m0		// ap[j]*bp[i]
	adds	$lo0,$lo0,$tj
	umulh	$ahi,$aj,$m0
	adc	$hi0,$hi0,xzr

	mul	$nlo,$nj,$m1		// np[j]*m1
	adds	$lo1,$lo1,$lo0
	umulh	$nhi,$nj,$m1
	str	$lo1,[$tp,#-16]		// tp[j-1]
	cbnz	$j,.Linner

.Linner_skip:
	ldr	$tj,[$tp],#8		// tp[j]
	adc	$hi1,$hi1,xzr
	adds	$lo0,$alo,$hi0
	sub	$ap,$ap,$num		// rewind $ap
	adc	$hi0,$ahi,xzr

	adds	$lo1,$nlo,$hi1
	sub	$np,$np,$num		// rewind $np
	adcs	$hi1,$nhi,$ovf
	adc	$ovf,xzr,xzr

	adds	$lo0,$lo0,$tj
	adc	$hi0,$hi0,xzr

	adds	$lo1,$lo1,$lo0
	adcs	$hi1,$hi1,$hi0
	adc	$ovf,$ovf,xzr		// upmost overflow bit
	stp	$lo1,$hi1,[$tp,#-16]

	cbnz	$i,.Louter

	// Final step. We see if result is larger than modulus, and
	// if it is, subtract the modulus. But comparison implies
	// subtraction. So we subtract modulus, see if it borrowed,
	// and conditionally copy original value.
	ldr	$tj,[sp]		// tp[0]
	add	$tp,sp,#8
	ldr	$nj,[$np],#8		// np[0]
	subs	$j,$num,#8		// j=num-1 and clear borrow
	mov	$ap,$rp
.Lsub:
	sbcs	$aj,$tj,$nj		// tp[j]-np[j]
	ldr	$tj,[$tp],#8
	sub	$j,$j,#8		// j--
	ldr	$nj,[$np],#8
	str	$aj,[$ap],#8		// rp[j]=tp[j]-np[j]
	cbnz	$j,.Lsub

	sbcs	$aj,$tj,$nj
	sbcs	$ovf,$ovf,xzr		// did it borrow?
	str	$aj,[$ap],#8		// rp[num-1]

	ldr	$tj,[sp]		// tp[0]
	add	$tp,sp,#8
	ldr	$aj,[$rp],#8		// rp[0]
	sub	$num,$num,#8		// num--
	nop
.Lcond_copy:
	sub	$num,$num,#8		// num--
	csel	$nj,$tj,$aj,lo		// did it borrow?
	ldr	$tj,[$tp],#8
	ldr	$aj,[$rp],#8
	str	xzr,[$tp,#-16]		// wipe tp
	str	$nj,[$rp,#-16]
	cbnz	$num,.Lcond_copy

	csel	$nj,$tj,$aj,lo
	str	xzr,[$tp,#-8]		// wipe tp
	str	$nj,[$rp,#-8]

	ldp	x19,x20,[x29,#16]
	mov	sp,x29
	ldp	x21,x22,[x29,#32]
	mov	x0,#1
	ldp	x23,x24,[x29,#48]
	ldr	x29,[sp],#64
	ret
.size	bn_mul_mont,.-bn_mul_mont
___
{
########################################################################
# Following is ARMv8 adaptation of sqrx8x_mont from x86_64-mont5 module.

my ($a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7)=map("x$_",(6..13));
my ($t0,$t1,$t2,$t3)=map("x$_",(14..17));
my ($acc0,$acc1,$acc2,$acc3,$acc4,$acc5,$acc6,$acc7)=map("x$_",(19..26));
my ($cnt,$carry,$topmost)=("x27","x28","x30");
my ($tp,$ap_end,$na0)=($bp,$np,$carry);

$code.=<<___;
.type	__bn_sqr8x_mont,%function
.align	5
__bn_sqr8x_mont:
	cmp	$ap,$bp
	b.ne	__bn_mul4x_mont
.Lsqr8x_mont:
	.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-128]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]
	stp	$rp,$np,[sp,#96]	// offload rp and np

	ldp	$a0,$a1,[$ap,#8*0]
	ldp	$a2,$a3,[$ap,#8*2]
	ldp	$a4,$a5,[$ap,#8*4]
	ldp	$a6,$a7,[$ap,#8*6]

	sub	$tp,sp,$num,lsl#4
	lsl	$num,$num,#3
	ldr	$n0,[$n0]		// *n0
	mov	sp,$tp			// alloca
	sub	$cnt,$num,#8*8
	b	.Lsqr8x_zero_start

.Lsqr8x_zero:
	sub	$cnt,$cnt,#8*8
	stp	xzr,xzr,[$tp,#8*0]
	stp	xzr,xzr,[$tp,#8*2]
	stp	xzr,xzr,[$tp,#8*4]
	stp	xzr,xzr,[$tp,#8*6]
.Lsqr8x_zero_start:
	stp	xzr,xzr,[$tp,#8*8]
	stp	xzr,xzr,[$tp,#8*10]
	stp	xzr,xzr,[$tp,#8*12]
	stp	xzr,xzr,[$tp,#8*14]
	add	$tp,$tp,#8*16
	cbnz	$cnt,.Lsqr8x_zero

	add	$ap_end,$ap,$num
	add	$ap,$ap,#8*8
	mov	$acc0,xzr
	mov	$acc1,xzr
	mov	$acc2,xzr
	mov	$acc3,xzr
	mov	$acc4,xzr
	mov	$acc5,xzr
	mov	$acc6,xzr
	mov	$acc7,xzr
	mov	$tp,sp
	str	$n0,[x29,#112]		// offload n0

	// Multiply everything but a[i]*a[i]
.align	4
.Lsqr8x_outer_loop:
        //                                                 a[1]a[0]	(i)
        //                                             a[2]a[0]
        //                                         a[3]a[0]
        //                                     a[4]a[0]
        //                                 a[5]a[0]
        //                             a[6]a[0]
        //                         a[7]a[0]
        //                                         a[2]a[1]		(ii)
        //                                     a[3]a[1]
        //                                 a[4]a[1]
        //                             a[5]a[1]
        //                         a[6]a[1]
        //                     a[7]a[1]
        //                                 a[3]a[2]			(iii)
        //                             a[4]a[2]
        //                         a[5]a[2]
        //                     a[6]a[2]
        //                 a[7]a[2]
        //                         a[4]a[3]				(iv)
        //                     a[5]a[3]
        //                 a[6]a[3]
        //             a[7]a[3]
        //                 a[5]a[4]					(v)
        //             a[6]a[4]
        //         a[7]a[4]
        //         a[6]a[5]						(vi)
        //     a[7]a[5]
        // a[7]a[6]							(vii)

	mul	$t0,$a1,$a0		// lo(a[1..7]*a[0])		(i)
	mul	$t1,$a2,$a0
	mul	$t2,$a3,$a0
	mul	$t3,$a4,$a0
	adds	$acc1,$acc1,$t0		// t[1]+lo(a[1]*a[0])
	mul	$t0,$a5,$a0
	adcs	$acc2,$acc2,$t1
	mul	$t1,$a6,$a0
	adcs	$acc3,$acc3,$t2
	mul	$t2,$a7,$a0
	adcs	$acc4,$acc4,$t3
	umulh	$t3,$a1,$a0		// hi(a[1..7]*a[0])
	adcs	$acc5,$acc5,$t0
	umulh	$t0,$a2,$a0
	adcs	$acc6,$acc6,$t1
	umulh	$t1,$a3,$a0
	adcs	$acc7,$acc7,$t2
	umulh	$t2,$a4,$a0
	stp	$acc0,$acc1,[$tp],#8*2	// t[0..1]
	adc	$acc0,xzr,xzr		// t[8]
	adds	$acc2,$acc2,$t3		// t[2]+lo(a[1]*a[0])
	umulh	$t3,$a5,$a0
	adcs	$acc3,$acc3,$t0
	umulh	$t0,$a6,$a0
	adcs	$acc4,$acc4,$t1
	umulh	$t1,$a7,$a0
	adcs	$acc5,$acc5,$t2
	 mul	$t2,$a2,$a1		// lo(a[2..7]*a[1])		(ii)
	adcs	$acc6,$acc6,$t3
	 mul	$t3,$a3,$a1
	adcs	$acc7,$acc7,$t0
	 mul	$t0,$a4,$a1
	adc	$acc0,$acc0,$t1

	mul	$t1,$a5,$a1
	adds	$acc3,$acc3,$t2
	mul	$t2,$a6,$a1
	adcs	$acc4,$acc4,$t3
	mul	$t3,$a7,$a1
	adcs	$acc5,$acc5,$t0
	umulh	$t0,$a2,$a1		// hi(a[2..7]*a[1])
	adcs	$acc6,$acc6,$t1
	umulh	$t1,$a3,$a1
	adcs	$acc7,$acc7,$t2
	umulh	$t2,$a4,$a1
	adcs	$acc0,$acc0,$t3
	umulh	$t3,$a5,$a1
	stp	$acc2,$acc3,[$tp],#8*2	// t[2..3]
	adc	$acc1,xzr,xzr		// t[9]
	adds	$acc4,$acc4,$t0
	umulh	$t0,$a6,$a1
	adcs	$acc5,$acc5,$t1
	umulh	$t1,$a7,$a1
	adcs	$acc6,$acc6,$t2
	 mul	$t2,$a3,$a2		// lo(a[3..7]*a[2])		(iii)
	adcs	$acc7,$acc7,$t3
	 mul	$t3,$a4,$a2
	adcs	$acc0,$acc0,$t0
	 mul	$t0,$a5,$a2
	adc	$acc1,$acc1,$t1

	mul	$t1,$a6,$a2
	adds	$acc5,$acc5,$t2
	mul	$t2,$a7,$a2
	adcs	$acc6,$acc6,$t3
	umulh	$t3,$a3,$a2		// hi(a[3..7]*a[2])
	adcs	$acc7,$acc7,$t0
	umulh	$t0,$a4,$a2
	adcs	$acc0,$acc0,$t1
	umulh	$t1,$a5,$a2
	adcs	$acc1,$acc1,$t2
	umulh	$t2,$a6,$a2
	stp	$acc4,$acc5,[$tp],#8*2	// t[4..5]
	adc	$acc2,xzr,xzr		// t[10]
	adds	$acc6,$acc6,$t3
	umulh	$t3,$a7,$a2
	adcs	$acc7,$acc7,$t0
	 mul	$t0,$a4,$a3		// lo(a[4..7]*a[3])		(iv)
	adcs	$acc0,$acc0,$t1
	 mul	$t1,$a5,$a3
	adcs	$acc1,$acc1,$t2
	 mul	$t2,$a6,$a3
	adc	$acc2,$acc2,$t3

	mul	$t3,$a7,$a3
	adds	$acc7,$acc7,$t0
	umulh	$t0,$a4,$a3		// hi(a[4..7]*a[3])
	adcs	$acc0,$acc0,$t1
	umulh	$t1,$a5,$a3
	adcs	$acc1,$acc1,$t2
	umulh	$t2,$a6,$a3
	adcs	$acc2,$acc2,$t3
	umulh	$t3,$a7,$a3
	stp	$acc6,$acc7,[$tp],#8*2	// t[6..7]
	adc	$acc3,xzr,xzr		// t[11]
	adds	$acc0,$acc0,$t0
	 mul	$t0,$a5,$a4		// lo(a[5..7]*a[4])		(v)
	adcs	$acc1,$acc1,$t1
	 mul	$t1,$a6,$a4
	adcs	$acc2,$acc2,$t2
	 mul	$t2,$a7,$a4
	adc	$acc3,$acc3,$t3

	umulh	$t3,$a5,$a4		// hi(a[5..7]*a[4])
	adds	$acc1,$acc1,$t0
	umulh	$t0,$a6,$a4
	adcs	$acc2,$acc2,$t1
	umulh	$t1,$a7,$a4
	adcs	$acc3,$acc3,$t2
	 mul	$t2,$a6,$a5		// lo(a[6..7]*a[5])		(vi)
	adc	$acc4,xzr,xzr		// t[12]
	adds	$acc2,$acc2,$t3
	 mul	$t3,$a7,$a5
	adcs	$acc3,$acc3,$t0
	 umulh	$t0,$a6,$a5		// hi(a[6..7]*a[5])
	adc	$acc4,$acc4,$t1

	umulh	$t1,$a7,$a5
	adds	$acc3,$acc3,$t2
	 mul	$t2,$a7,$a6		// lo(a[7]*a[6])		(vii)
	adcs	$acc4,$acc4,$t3
	 umulh	$t3,$a7,$a6		// hi(a[7]*a[6])
	adc	$acc5,xzr,xzr		// t[13]
	adds	$acc4,$acc4,$t0
	sub	$cnt,$ap_end,$ap	// done yet?
	adc	$acc5,$acc5,$t1

	adds	$acc5,$acc5,$t2
	sub	$t0,$ap_end,$num	// rewinded ap
	adc	$acc6,xzr,xzr		// t[14]
	add	$acc6,$acc6,$t3

	cbz	$cnt,.Lsqr8x_outer_break

	mov	$n0,$a0
	ldp	$a0,$a1,[$tp,#8*0]
	ldp	$a2,$a3,[$tp,#8*2]
	ldp	$a4,$a5,[$tp,#8*4]
	ldp	$a6,$a7,[$tp,#8*6]
	adds	$acc0,$acc0,$a0
	adcs	$acc1,$acc1,$a1
	ldp	$a0,$a1,[$ap,#8*0]
	adcs	$acc2,$acc2,$a2
	adcs	$acc3,$acc3,$a3
	ldp	$a2,$a3,[$ap,#8*2]
	adcs	$acc4,$acc4,$a4
	adcs	$acc5,$acc5,$a5
	ldp	$a4,$a5,[$ap,#8*4]
	adcs	$acc6,$acc6,$a6
	mov	$rp,$ap
	adcs	$acc7,xzr,$a7
	ldp	$a6,$a7,[$ap,#8*6]
	add	$ap,$ap,#8*8
	//adc	$carry,xzr,xzr		// moved below
	mov	$cnt,#-8*8

	//                                                         a[8]a[0]
	//                                                     a[9]a[0]
	//                                                 a[a]a[0]
	//                                             a[b]a[0]
	//                                         a[c]a[0]
	//                                     a[d]a[0]
	//                                 a[e]a[0]
	//                             a[f]a[0]
	//                                                     a[8]a[1]
	//                         a[f]a[1]........................
	//                                                 a[8]a[2]
	//                     a[f]a[2]........................
	//                                             a[8]a[3]
	//                 a[f]a[3]........................
	//                                         a[8]a[4]
	//             a[f]a[4]........................
	//                                     a[8]a[5]
	//         a[f]a[5]........................
	//                                 a[8]a[6]
	//     a[f]a[6]........................
	//                             a[8]a[7]
	// a[f]a[7]........................
.Lsqr8x_mul:
	mul	$t0,$a0,$n0
	adc	$carry,xzr,xzr		// carry bit, modulo-scheduled
	mul	$t1,$a1,$n0
	add	$cnt,$cnt,#8
	mul	$t2,$a2,$n0
	mul	$t3,$a3,$n0
	adds	$acc0,$acc0,$t0
	mul	$t0,$a4,$n0
	adcs	$acc1,$acc1,$t1
	mul	$t1,$a5,$n0
	adcs	$acc2,$acc2,$t2
	mul	$t2,$a6,$n0
	adcs	$acc3,$acc3,$t3
	mul	$t3,$a7,$n0
	adcs	$acc4,$acc4,$t0
	umulh	$t0,$a0,$n0
	adcs	$acc5,$acc5,$t1
	umulh	$t1,$a1,$n0
	adcs	$acc6,$acc6,$t2
	umulh	$t2,$a2,$n0
	adcs	$acc7,$acc7,$t3
	umulh	$t3,$a3,$n0
	adc	$carry,$carry,xzr
	str	$acc0,[$tp],#8
	adds	$acc0,$acc1,$t0
	umulh	$t0,$a4,$n0
	adcs	$acc1,$acc2,$t1
	umulh	$t1,$a5,$n0
	adcs	$acc2,$acc3,$t2
	umulh	$t2,$a6,$n0
	adcs	$acc3,$acc4,$t3
	umulh	$t3,$a7,$n0
	ldr	$n0,[$rp,$cnt]
	adcs	$acc4,$acc5,$t0
	adcs	$acc5,$acc6,$t1
	adcs	$acc6,$acc7,$t2
	adcs	$acc7,$carry,$t3
	//adc	$carry,xzr,xzr		// moved above
	cbnz	$cnt,.Lsqr8x_mul
					// note that carry flag is guaranteed
					// to be zero at this point
	cmp	$ap,$ap_end		// done yet?
	b.eq	.Lsqr8x_break

	ldp	$a0,$a1,[$tp,#8*0]
	ldp	$a2,$a3,[$tp,#8*2]
	ldp	$a4,$a5,[$tp,#8*4]
	ldp	$a6,$a7,[$tp,#8*6]
	adds	$acc0,$acc0,$a0
	ldr	$n0,[$rp,#-8*8]
	adcs	$acc1,$acc1,$a1
	ldp	$a0,$a1,[$ap,#8*0]
	adcs	$acc2,$acc2,$a2
	adcs	$acc3,$acc3,$a3
	ldp	$a2,$a3,[$ap,#8*2]
	adcs	$acc4,$acc4,$a4
	adcs	$acc5,$acc5,$a5
	ldp	$a4,$a5,[$ap,#8*4]
	adcs	$acc6,$acc6,$a6
	mov	$cnt,#-8*8
	adcs	$acc7,$acc7,$a7
	ldp	$a6,$a7,[$ap,#8*6]
	add	$ap,$ap,#8*8
	//adc	$carry,xzr,xzr		// moved above
	b	.Lsqr8x_mul

.align	4
.Lsqr8x_break:
	ldp	$a0,$a1,[$rp,#8*0]
	add	$ap,$rp,#8*8
	ldp	$a2,$a3,[$rp,#8*2]
	sub	$t0,$ap_end,$ap		// is it last iteration?
	ldp	$a4,$a5,[$rp,#8*4]
	sub	$t1,$tp,$t0
	ldp	$a6,$a7,[$rp,#8*6]
	cbz	$t0,.Lsqr8x_outer_loop

	stp	$acc0,$acc1,[$tp,#8*0]
	ldp	$acc0,$acc1,[$t1,#8*0]
	stp	$acc2,$acc3,[$tp,#8*2]
	ldp	$acc2,$acc3,[$t1,#8*2]
	stp	$acc4,$acc5,[$tp,#8*4]
	ldp	$acc4,$acc5,[$t1,#8*4]
	stp	$acc6,$acc7,[$tp,#8*6]
	mov	$tp,$t1
	ldp	$acc6,$acc7,[$t1,#8*6]
	b	.Lsqr8x_outer_loop

.align	4
.Lsqr8x_outer_break:
	// Now multiply above result by 2 and add a[n-1]*a[n-1]|...|a[0]*a[0]
	ldp	$a1,$a3,[$t0,#8*0]	// recall that $t0 is &a[0]
	ldp	$t1,$t2,[sp,#8*1]
	ldp	$a5,$a7,[$t0,#8*2]
	add	$ap,$t0,#8*4
	ldp	$t3,$t0,[sp,#8*3]

	stp	$acc0,$acc1,[$tp,#8*0]
	mul	$acc0,$a1,$a1
	stp	$acc2,$acc3,[$tp,#8*2]
	umulh	$a1,$a1,$a1
	stp	$acc4,$acc5,[$tp,#8*4]
	mul	$a2,$a3,$a3
	stp	$acc6,$acc7,[$tp,#8*6]
	mov	$tp,sp
	umulh	$a3,$a3,$a3
	adds	$acc1,$a1,$t1,lsl#1
	extr	$t1,$t2,$t1,#63
	sub	$cnt,$num,#8*4

.Lsqr4x_shift_n_add:
	adcs	$acc2,$a2,$t1
	extr	$t2,$t3,$t2,#63
	sub	$cnt,$cnt,#8*4
	adcs	$acc3,$a3,$t2
	ldp	$t1,$t2,[$tp,#8*5]
	mul	$a4,$a5,$a5
	ldp	$a1,$a3,[$ap],#8*2
	umulh	$a5,$a5,$a5
	mul	$a6,$a7,$a7
	umulh	$a7,$a7,$a7
	extr	$t3,$t0,$t3,#63
	stp	$acc0,$acc1,[$tp,#8*0]
	adcs	$acc4,$a4,$t3
	extr	$t0,$t1,$t0,#63
	stp	$acc2,$acc3,[$tp,#8*2]
	adcs	$acc5,$a5,$t0
	ldp	$t3,$t0,[$tp,#8*7]
	extr	$t1,$t2,$t1,#63
	adcs	$acc6,$a6,$t1
	extr	$t2,$t3,$t2,#63
	adcs	$acc7,$a7,$t2
	ldp	$t1,$t2,[$tp,#8*9]
	mul	$a0,$a1,$a1
	ldp	$a5,$a7,[$ap],#8*2
	umulh	$a1,$a1,$a1
	mul	$a2,$a3,$a3
	umulh	$a3,$a3,$a3
	stp	$acc4,$acc5,[$tp,#8*4]
	extr	$t3,$t0,$t3,#63
	stp	$acc6,$acc7,[$tp,#8*6]
	add	$tp,$tp,#8*8
	adcs	$acc0,$a0,$t3
	extr	$t0,$t1,$t0,#63
	adcs	$acc1,$a1,$t0
	ldp	$t3,$t0,[$tp,#8*3]
	extr	$t1,$t2,$t1,#63
	cbnz	$cnt,.Lsqr4x_shift_n_add
___
my ($np,$np_end)=($ap,$ap_end);
$code.=<<___;
	 ldp	$np,$n0,[x29,#104]	// pull np and n0

	adcs	$acc2,$a2,$t1
	extr	$t2,$t3,$t2,#63
	adcs	$acc3,$a3,$t2
	ldp	$t1,$t2,[$tp,#8*5]
	mul	$a4,$a5,$a5
	umulh	$a5,$a5,$a5
	stp	$acc0,$acc1,[$tp,#8*0]
	mul	$a6,$a7,$a7
	umulh	$a7,$a7,$a7
	stp	$acc2,$acc3,[$tp,#8*2]
	extr	$t3,$t0,$t3,#63
	adcs	$acc4,$a4,$t3
	extr	$t0,$t1,$t0,#63
	 ldp	$acc0,$acc1,[sp,#8*0]
	adcs	$acc5,$a5,$t0
	extr	$t1,$t2,$t1,#63
	 ldp	$a0,$a1,[$np,#8*0]
	adcs	$acc6,$a6,$t1
	extr	$t2,xzr,$t2,#63
	 ldp	$a2,$a3,[$np,#8*2]
	adc	$acc7,$a7,$t2
	 ldp	$a4,$a5,[$np,#8*4]

	// Reduce by 512 bits per iteration
	mul	$na0,$n0,$acc0		// t[0]*n0
	ldp	$a6,$a7,[$np,#8*6]
	add	$np_end,$np,$num
	ldp	$acc2,$acc3,[sp,#8*2]
	stp	$acc4,$acc5,[$tp,#8*4]
	ldp	$acc4,$acc5,[sp,#8*4]
	stp	$acc6,$acc7,[$tp,#8*6]
	ldp	$acc6,$acc7,[sp,#8*6]
	add	$np,$np,#8*8
	mov	$topmost,xzr		// initial top-most carry
	mov	$tp,sp
	mov	$cnt,#8

.Lsqr8x_reduction:
	// (*)	mul	$t0,$a0,$na0	// lo(n[0-7])*lo(t[0]*n0)
	mul	$t1,$a1,$na0
	sub	$cnt,$cnt,#1
	mul	$t2,$a2,$na0
	str	$na0,[$tp],#8		// put aside t[0]*n0 for tail processing
	mul	$t3,$a3,$na0
	// (*)	adds	xzr,$acc0,$t0
	subs	xzr,$acc0,#1		// (*)
	mul	$t0,$a4,$na0
	adcs	$acc0,$acc1,$t1
	mul	$t1,$a5,$na0
	adcs	$acc1,$acc2,$t2
	mul	$t2,$a6,$na0
	adcs	$acc2,$acc3,$t3
	mul	$t3,$a7,$na0
	adcs	$acc3,$acc4,$t0
	umulh	$t0,$a0,$na0		// hi(n[0-7])*lo(t[0]*n0)
	adcs	$acc4,$acc5,$t1
	umulh	$t1,$a1,$na0
	adcs	$acc5,$acc6,$t2
	umulh	$t2,$a2,$na0
	adcs	$acc6,$acc7,$t3
	umulh	$t3,$a3,$na0
	adc	$acc7,xzr,xzr
	adds	$acc0,$acc0,$t0
	umulh	$t0,$a4,$na0
	adcs	$acc1,$acc1,$t1
	umulh	$t1,$a5,$na0
	adcs	$acc2,$acc2,$t2
	umulh	$t2,$a6,$na0
	adcs	$acc3,$acc3,$t3
	umulh	$t3,$a7,$na0
	mul	$na0,$n0,$acc0		// next t[0]*n0
	adcs	$acc4,$acc4,$t0
	adcs	$acc5,$acc5,$t1
	adcs	$acc6,$acc6,$t2
	adc	$acc7,$acc7,$t3
	cbnz	$cnt,.Lsqr8x_reduction

	ldp	$t0,$t1,[$tp,#8*0]
	ldp	$t2,$t3,[$tp,#8*2]
	mov	$rp,$tp
	sub	$cnt,$np_end,$np	// done yet?
	adds	$acc0,$acc0,$t0
	adcs	$acc1,$acc1,$t1
	ldp	$t0,$t1,[$tp,#8*4]
	adcs	$acc2,$acc2,$t2
	adcs	$acc3,$acc3,$t3
	ldp	$t2,$t3,[$tp,#8*6]
	adcs	$acc4,$acc4,$t0
	adcs	$acc5,$acc5,$t1
	adcs	$acc6,$acc6,$t2
	adcs	$acc7,$acc7,$t3
	//adc	$carry,xzr,xzr		// moved below
	cbz	$cnt,.Lsqr8x8_post_condition

	ldr	$n0,[$tp,#-8*8]
	ldp	$a0,$a1,[$np,#8*0]
	ldp	$a2,$a3,[$np,#8*2]
	ldp	$a4,$a5,[$np,#8*4]
	mov	$cnt,#-8*8
	ldp	$a6,$a7,[$np,#8*6]
	add	$np,$np,#8*8

.Lsqr8x_tail:
	mul	$t0,$a0,$n0
	adc	$carry,xzr,xzr		// carry bit, modulo-scheduled
	mul	$t1,$a1,$n0
	add	$cnt,$cnt,#8
	mul	$t2,$a2,$n0
	mul	$t3,$a3,$n0
	adds	$acc0,$acc0,$t0
	mul	$t0,$a4,$n0
	adcs	$acc1,$acc1,$t1
	mul	$t1,$a5,$n0
	adcs	$acc2,$acc2,$t2
	mul	$t2,$a6,$n0
	adcs	$acc3,$acc3,$t3
	mul	$t3,$a7,$n0
	adcs	$acc4,$acc4,$t0
	umulh	$t0,$a0,$n0
	adcs	$acc5,$acc5,$t1
	umulh	$t1,$a1,$n0
	adcs	$acc6,$acc6,$t2
	umulh	$t2,$a2,$n0
	adcs	$acc7,$acc7,$t3
	umulh	$t3,$a3,$n0
	adc	$carry,$carry,xzr
	str	$acc0,[$tp],#8
	adds	$acc0,$acc1,$t0
	umulh	$t0,$a4,$n0
	adcs	$acc1,$acc2,$t1
	umulh	$t1,$a5,$n0
	adcs	$acc2,$acc3,$t2
	umulh	$t2,$a6,$n0
	adcs	$acc3,$acc4,$t3
	umulh	$t3,$a7,$n0
	ldr	$n0,[$rp,$cnt]
	adcs	$acc4,$acc5,$t0
	adcs	$acc5,$acc6,$t1
	adcs	$acc6,$acc7,$t2
	adcs	$acc7,$carry,$t3
	//adc	$carry,xzr,xzr		// moved above
	cbnz	$cnt,.Lsqr8x_tail
					// note that carry flag is guaranteed
					// to be zero at this point
	ldp	$a0,$a1,[$tp,#8*0]
	sub	$cnt,$np_end,$np	// done yet?
	sub	$t2,$np_end,$num	// rewinded np
	ldp	$a2,$a3,[$tp,#8*2]
	ldp	$a4,$a5,[$tp,#8*4]
	ldp	$a6,$a7,[$tp,#8*6]
	cbz	$cnt,.Lsqr8x_tail_break

	ldr	$n0,[$rp,#-8*8]
	adds	$acc0,$acc0,$a0
	adcs	$acc1,$acc1,$a1
	ldp	$a0,$a1,[$np,#8*0]
	adcs	$acc2,$acc2,$a2
	adcs	$acc3,$acc3,$a3
	ldp	$a2,$a3,[$np,#8*2]
	adcs	$acc4,$acc4,$a4
	adcs	$acc5,$acc5,$a5
	ldp	$a4,$a5,[$np,#8*4]
	adcs	$acc6,$acc6,$a6
	mov	$cnt,#-8*8
	adcs	$acc7,$acc7,$a7
	ldp	$a6,$a7,[$np,#8*6]
	add	$np,$np,#8*8
	//adc	$carry,xzr,xzr		// moved above
	b	.Lsqr8x_tail

.align	4
.Lsqr8x_tail_break:
	ldr	$n0,[x29,#112]		// pull n0
	add	$cnt,$tp,#8*8		// end of current t[num] window

	subs	xzr,$topmost,#1		// "move" top-most carry to carry bit
	adcs	$t0,$acc0,$a0
	adcs	$t1,$acc1,$a1
	ldp	$acc0,$acc1,[$rp,#8*0]
	adcs	$acc2,$acc2,$a2
	ldp	$a0,$a1,[$t2,#8*0]	// recall that $t2 is &n[0]
	adcs	$acc3,$acc3,$a3
	ldp	$a2,$a3,[$t2,#8*2]
	adcs	$acc4,$acc4,$a4
	adcs	$acc5,$acc5,$a5
	ldp	$a4,$a5,[$t2,#8*4]
	adcs	$acc6,$acc6,$a6
	adcs	$acc7,$acc7,$a7
	ldp	$a6,$a7,[$t2,#8*6]
	add	$np,$t2,#8*8
	adc	$topmost,xzr,xzr	// top-most carry
	mul	$na0,$n0,$acc0
	stp	$t0,$t1,[$tp,#8*0]
	stp	$acc2,$acc3,[$tp,#8*2]
	ldp	$acc2,$acc3,[$rp,#8*2]
	stp	$acc4,$acc5,[$tp,#8*4]
	ldp	$acc4,$acc5,[$rp,#8*4]
	cmp	$cnt,x29		// did we hit the bottom?
	stp	$acc6,$acc7,[$tp,#8*6]
	mov	$tp,$rp			// slide the window
	ldp	$acc6,$acc7,[$rp,#8*6]
	mov	$cnt,#8
	b.ne	.Lsqr8x_reduction

	// Final step. We see if result is larger than modulus, and
	// if it is, subtract the modulus. But comparison implies
	// subtraction. So we subtract modulus, see if it borrowed,
	// and conditionally copy original value.
	ldr	$rp,[x29,#96]		// pull rp
	add	$tp,$tp,#8*8
	subs	$t0,$acc0,$a0
	sbcs	$t1,$acc1,$a1
	sub	$cnt,$num,#8*8
	mov	$ap_end,$rp		// $rp copy

.Lsqr8x_sub:
	sbcs	$t2,$acc2,$a2
	ldp	$a0,$a1,[$np,#8*0]
	sbcs	$t3,$acc3,$a3
	stp	$t0,$t1,[$rp,#8*0]
	sbcs	$t0,$acc4,$a4
	ldp	$a2,$a3,[$np,#8*2]
	sbcs	$t1,$acc5,$a5
	stp	$t2,$t3,[$rp,#8*2]
	sbcs	$t2,$acc6,$a6
	ldp	$a4,$a5,[$np,#8*4]
	sbcs	$t3,$acc7,$a7
	ldp	$a6,$a7,[$np,#8*6]
	add	$np,$np,#8*8
	ldp	$acc0,$acc1,[$tp,#8*0]
	sub	$cnt,$cnt,#8*8
	ldp	$acc2,$acc3,[$tp,#8*2]
	ldp	$acc4,$acc5,[$tp,#8*4]
	ldp	$acc6,$acc7,[$tp,#8*6]
	add	$tp,$tp,#8*8
	stp	$t0,$t1,[$rp,#8*4]
	sbcs	$t0,$acc0,$a0
	stp	$t2,$t3,[$rp,#8*6]
	add	$rp,$rp,#8*8
	sbcs	$t1,$acc1,$a1
	cbnz	$cnt,.Lsqr8x_sub

	sbcs	$t2,$acc2,$a2
	 mov	$tp,sp
	 add	$ap,sp,$num
	 ldp	$a0,$a1,[$ap_end,#8*0]
	sbcs	$t3,$acc3,$a3
	stp	$t0,$t1,[$rp,#8*0]
	sbcs	$t0,$acc4,$a4
	 ldp	$a2,$a3,[$ap_end,#8*2]
	sbcs	$t1,$acc5,$a5
	stp	$t2,$t3,[$rp,#8*2]
	sbcs	$t2,$acc6,$a6
	 ldp	$acc0,$acc1,[$ap,#8*0]
	sbcs	$t3,$acc7,$a7
	 ldp	$acc2,$acc3,[$ap,#8*2]
	sbcs	xzr,$topmost,xzr	// did it borrow?
	ldr	x30,[x29,#8]		// pull return address
	stp	$t0,$t1,[$rp,#8*4]
	stp	$t2,$t3,[$rp,#8*6]

	sub	$cnt,$num,#8*4
.Lsqr4x_cond_copy:
	sub	$cnt,$cnt,#8*4
	csel	$t0,$acc0,$a0,lo
	 stp	xzr,xzr,[$tp,#8*0]
	csel	$t1,$acc1,$a1,lo
	ldp	$a0,$a1,[$ap_end,#8*4]
	ldp	$acc0,$acc1,[$ap,#8*4]
	csel	$t2,$acc2,$a2,lo
	 stp	xzr,xzr,[$tp,#8*2]
	 add	$tp,$tp,#8*4
	csel	$t3,$acc3,$a3,lo
	ldp	$a2,$a3,[$ap_end,#8*6]
	ldp	$acc2,$acc3,[$ap,#8*6]
	add	$ap,$ap,#8*4
	stp	$t0,$t1,[$ap_end,#8*0]
	stp	$t2,$t3,[$ap_end,#8*2]
	add	$ap_end,$ap_end,#8*4
	 stp	xzr,xzr,[$ap,#8*0]
	 stp	xzr,xzr,[$ap,#8*2]
	cbnz	$cnt,.Lsqr4x_cond_copy

	csel	$t0,$acc0,$a0,lo
	 stp	xzr,xzr,[$tp,#8*0]
	csel	$t1,$acc1,$a1,lo
	 stp	xzr,xzr,[$tp,#8*2]
	csel	$t2,$acc2,$a2,lo
	csel	$t3,$acc3,$a3,lo
	stp	$t0,$t1,[$ap_end,#8*0]
	stp	$t2,$t3,[$ap_end,#8*2]

	b	.Lsqr8x_done

.align	4
.Lsqr8x8_post_condition:
	adc	$carry,xzr,xzr
	ldr	x30,[x29,#8]		// pull return address
	// $acc0-7,$carry hold result, $a0-7 hold modulus
	subs	$a0,$acc0,$a0
	ldr	$ap,[x29,#96]		// pull rp
	sbcs	$a1,$acc1,$a1
	 stp	xzr,xzr,[sp,#8*0]
	sbcs	$a2,$acc2,$a2
	 stp	xzr,xzr,[sp,#8*2]
	sbcs	$a3,$acc3,$a3
	 stp	xzr,xzr,[sp,#8*4]
	sbcs	$a4,$acc4,$a4
	 stp	xzr,xzr,[sp,#8*6]
	sbcs	$a5,$acc5,$a5
	 stp	xzr,xzr,[sp,#8*8]
	sbcs	$a6,$acc6,$a6
	 stp	xzr,xzr,[sp,#8*10]
	sbcs	$a7,$acc7,$a7
	 stp	xzr,xzr,[sp,#8*12]
	sbcs	$carry,$carry,xzr	// did it borrow?
	 stp	xzr,xzr,[sp,#8*14]

	// $a0-7 hold result-modulus
	csel	$a0,$acc0,$a0,lo
	csel	$a1,$acc1,$a1,lo
	csel	$a2,$acc2,$a2,lo
	csel	$a3,$acc3,$a3,lo
	stp	$a0,$a1,[$ap,#8*0]
	csel	$a4,$acc4,$a4,lo
	csel	$a5,$acc5,$a5,lo
	stp	$a2,$a3,[$ap,#8*2]
	csel	$a6,$acc6,$a6,lo
	csel	$a7,$acc7,$a7,lo
	stp	$a4,$a5,[$ap,#8*4]
	stp	$a6,$a7,[$ap,#8*6]

.Lsqr8x_done:
	ldp	x19,x20,[x29,#16]
	mov	sp,x29
	ldp	x21,x22,[x29,#32]
	mov	x0,#1
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldr	x29,[sp],#128
	.inst	0xd50323bf		// autiasp
	ret
.size	__bn_sqr8x_mont,.-__bn_sqr8x_mont
___
}

{
########################################################################
# Even though this might look as ARMv8 adaptation of mulx4x_mont from
# x86_64-mont5 module, it's different in sense that it performs
# reduction 256 bits at a time.

my ($a0,$a1,$a2,$a3,
    $t0,$t1,$t2,$t3,
    $m0,$m1,$m2,$m3,
    $acc0,$acc1,$acc2,$acc3,$acc4,
    $bi,$mi,$tp,$ap_end,$cnt) = map("x$_",(6..17,19..28));
my  $bp_end=$rp;
my  ($carry,$topmost) = ($rp,"x30");

$code.=<<___;
.type	__bn_mul4x_mont,%function
.align	5
__bn_mul4x_mont:
	.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-128]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]

	sub	$tp,sp,$num,lsl#3
	lsl	$num,$num,#3
	ldr	$n0,[$n0]		// *n0
	sub	sp,$tp,#8*4		// alloca

	add	$t0,$bp,$num
	add	$ap_end,$ap,$num
	stp	$rp,$t0,[x29,#96]	// offload rp and &b[num]

	ldr	$bi,[$bp,#8*0]		// b[0]
	ldp	$a0,$a1,[$ap,#8*0]	// a[0..3]
	ldp	$a2,$a3,[$ap,#8*2]
	add	$ap,$ap,#8*4
	mov	$acc0,xzr
	mov	$acc1,xzr
	mov	$acc2,xzr
	mov	$acc3,xzr
	ldp	$m0,$m1,[$np,#8*0]	// n[0..3]
	ldp	$m2,$m3,[$np,#8*2]
	adds	$np,$np,#8*4		// clear carry bit
	mov	$carry,xzr
	mov	$cnt,#0
	mov	$tp,sp

.Loop_mul4x_1st_reduction:
	mul	$t0,$a0,$bi		// lo(a[0..3]*b[0])
	adc	$carry,$carry,xzr	// modulo-scheduled
	mul	$t1,$a1,$bi
	add	$cnt,$cnt,#8
	mul	$t2,$a2,$bi
	and	$cnt,$cnt,#31
	mul	$t3,$a3,$bi
	adds	$acc0,$acc0,$t0
	umulh	$t0,$a0,$bi		// hi(a[0..3]*b[0])
	adcs	$acc1,$acc1,$t1
	mul	$mi,$acc0,$n0		// t[0]*n0
	adcs	$acc2,$acc2,$t2
	umulh	$t1,$a1,$bi
	adcs	$acc3,$acc3,$t3
	umulh	$t2,$a2,$bi
	adc	$acc4,xzr,xzr
	umulh	$t3,$a3,$bi
	ldr	$bi,[$bp,$cnt]		// next b[i] (or b[0])
	adds	$acc1,$acc1,$t0
	// (*)	mul	$t0,$m0,$mi	// lo(n[0..3]*t[0]*n0)
	str	$mi,[$tp],#8		// put aside t[0]*n0 for tail processing
	adcs	$acc2,$acc2,$t1
	mul	$t1,$m1,$mi
	adcs	$acc3,$acc3,$t2
	mul	$t2,$m2,$mi
	adc	$acc4,$acc4,$t3		// can't overflow
	mul	$t3,$m3,$mi
	// (*)	adds	xzr,$acc0,$t0
	subs	xzr,$acc0,#1		// (*)
	umulh	$t0,$m0,$mi		// hi(n[0..3]*t[0]*n0)
	adcs	$acc0,$acc1,$t1
	umulh	$t1,$m1,$mi
	adcs	$acc1,$acc2,$t2
	umulh	$t2,$m2,$mi
	adcs	$acc2,$acc3,$t3
	umulh	$t3,$m3,$mi
	adcs	$acc3,$acc4,$carry
	adc	$carry,xzr,xzr
	adds	$acc0,$acc0,$t0
	sub	$t0,$ap_end,$ap
	adcs	$acc1,$acc1,$t1
	adcs	$acc2,$acc2,$t2
	adcs	$acc3,$acc3,$t3
	//adc	$carry,$carry,xzr
	cbnz	$cnt,.Loop_mul4x_1st_reduction

	cbz	$t0,.Lmul4x4_post_condition

	ldp	$a0,$a1,[$ap,#8*0]	// a[4..7]
	ldp	$a2,$a3,[$ap,#8*2]
	add	$ap,$ap,#8*4
	ldr	$mi,[sp]		// a[0]*n0
	ldp	$m0,$m1,[$np,#8*0]	// n[4..7]
	ldp	$m2,$m3,[$np,#8*2]
	add	$np,$np,#8*4

.Loop_mul4x_1st_tail:
	mul	$t0,$a0,$bi		// lo(a[4..7]*b[i])
	adc	$carry,$carry,xzr	// modulo-scheduled
	mul	$t1,$a1,$bi
	add	$cnt,$cnt,#8
	mul	$t2,$a2,$bi
	and	$cnt,$cnt,#31
	mul	$t3,$a3,$bi
	adds	$acc0,$acc0,$t0
	umulh	$t0,$a0,$bi		// hi(a[4..7]*b[i])
	adcs	$acc1,$acc1,$t1
	umulh	$t1,$a1,$bi
	adcs	$acc2,$acc2,$t2
	umulh	$t2,$a2,$bi
	adcs	$acc3,$acc3,$t3
	umulh	$t3,$a3,$bi
	adc	$acc4,xzr,xzr
	ldr	$bi,[$bp,$cnt]		// next b[i] (or b[0])
	adds	$acc1,$acc1,$t0
	mul	$t0,$m0,$mi		// lo(n[4..7]*a[0]*n0)
	adcs	$acc2,$acc2,$t1
	mul	$t1,$m1,$mi
	adcs	$acc3,$acc3,$t2
	mul	$t2,$m2,$mi
	adc	$acc4,$acc4,$t3		// can't overflow
	mul	$t3,$m3,$mi
	adds	$acc0,$acc0,$t0
	umulh	$t0,$m0,$mi		// hi(n[4..7]*a[0]*n0)
	adcs	$acc1,$acc1,$t1
	umulh	$t1,$m1,$mi
	adcs	$acc2,$acc2,$t2
	umulh	$t2,$m2,$mi
	adcs	$acc3,$acc3,$t3
	adcs	$acc4,$acc4,$carry
	umulh	$t3,$m3,$mi
	adc	$carry,xzr,xzr
	ldr	$mi,[sp,$cnt]		// next t[0]*n0
	str	$acc0,[$tp],#8		// result!!!
	adds	$acc0,$acc1,$t0
	sub	$t0,$ap_end,$ap		// done yet?
	adcs	$acc1,$acc2,$t1
	adcs	$acc2,$acc3,$t2
	adcs	$acc3,$acc4,$t3
	//adc	$carry,$carry,xzr
	cbnz	$cnt,.Loop_mul4x_1st_tail

	sub	$t1,$ap_end,$num	// rewinded $ap
	cbz	$t0,.Lmul4x_proceed

	ldp	$a0,$a1,[$ap,#8*0]
	ldp	$a2,$a3,[$ap,#8*2]
	add	$ap,$ap,#8*4
	ldp	$m0,$m1,[$np,#8*0]
	ldp	$m2,$m3,[$np,#8*2]
	add	$np,$np,#8*4
	b	.Loop_mul4x_1st_tail

.align	5
.Lmul4x_proceed:
	ldr	$bi,[$bp,#8*4]!		// *++b
	adc	$topmost,$carry,xzr
	ldp	$a0,$a1,[$t1,#8*0]	// a[0..3]
	sub	$np,$np,$num		// rewind np
	ldp	$a2,$a3,[$t1,#8*2]
	add	$ap,$t1,#8*4

	stp	$acc0,$acc1,[$tp,#8*0]	// result!!!
	ldp	$acc0,$acc1,[sp,#8*4]	// t[0..3]
	stp	$acc2,$acc3,[$tp,#8*2]	// result!!!
	ldp	$acc2,$acc3,[sp,#8*6]

	ldp	$m0,$m1,[$np,#8*0]	// n[0..3]
	mov	$tp,sp
	ldp	$m2,$m3,[$np,#8*2]
	adds	$np,$np,#8*4		// clear carry bit
	mov	$carry,xzr

.align	4
.Loop_mul4x_reduction:
	mul	$t0,$a0,$bi		// lo(a[0..3]*b[4])
	adc	$carry,$carry,xzr	// modulo-scheduled
	mul	$t1,$a1,$bi
	add	$cnt,$cnt,#8
	mul	$t2,$a2,$bi
	and	$cnt,$cnt,#31
	mul	$t3,$a3,$bi
	adds	$acc0,$acc0,$t0
	umulh	$t0,$a0,$bi		// hi(a[0..3]*b[4])
	adcs	$acc1,$acc1,$t1
	mul	$mi,$acc0,$n0		// t[0]*n0
	adcs	$acc2,$acc2,$t2
	umulh	$t1,$a1,$bi
	adcs	$acc3,$acc3,$t3
	umulh	$t2,$a2,$bi
	adc	$acc4,xzr,xzr
	umulh	$t3,$a3,$bi
	ldr	$bi,[$bp,$cnt]		// next b[i]
	adds	$acc1,$acc1,$t0
	// (*)	mul	$t0,$m0,$mi
	str	$mi,[$tp],#8		// put aside t[0]*n0 for tail processing
	adcs	$acc2,$acc2,$t1
	mul	$t1,$m1,$mi		// lo(n[0..3]*t[0]*n0
	adcs	$acc3,$acc3,$t2
	mul	$t2,$m2,$mi
	adc	$acc4,$acc4,$t3		// can't overflow
	mul	$t3,$m3,$mi
	// (*)	adds	xzr,$acc0,$t0
	subs	xzr,$acc0,#1		// (*)
	umulh	$t0,$m0,$mi		// hi(n[0..3]*t[0]*n0
	adcs	$acc0,$acc1,$t1
	umulh	$t1,$m1,$mi
	adcs	$acc1,$acc2,$t2
	umulh	$t2,$m2,$mi
	adcs	$acc2,$acc3,$t3
	umulh	$t3,$m3,$mi
	adcs	$acc3,$acc4,$carry
	adc	$carry,xzr,xzr
	adds	$acc0,$acc0,$t0
	adcs	$acc1,$acc1,$t1
	adcs	$acc2,$acc2,$t2
	adcs	$acc3,$acc3,$t3
	//adc	$carry,$carry,xzr
	cbnz	$cnt,.Loop_mul4x_reduction

	adc	$carry,$carry,xzr
	ldp	$t0,$t1,[$tp,#8*4]	// t[4..7]
	ldp	$t2,$t3,[$tp,#8*6]
	ldp	$a0,$a1,[$ap,#8*0]	// a[4..7]
	ldp	$a2,$a3,[$ap,#8*2]
	add	$ap,$ap,#8*4
	adds	$acc0,$acc0,$t0
	adcs	$acc1,$acc1,$t1
	adcs	$acc2,$acc2,$t2
	adcs	$acc3,$acc3,$t3
	//adc	$carry,$carry,xzr

	ldr	$mi,[sp]		// t[0]*n0
	ldp	$m0,$m1,[$np,#8*0]	// n[4..7]
	ldp	$m2,$m3,[$np,#8*2]
	add	$np,$np,#8*4

.align	4
.Loop_mul4x_tail:
	mul	$t0,$a0,$bi		// lo(a[4..7]*b[4])
	adc	$carry,$carry,xzr	// modulo-scheduled
	mul	$t1,$a1,$bi
	add	$cnt,$cnt,#8
	mul	$t2,$a2,$bi
	and	$cnt,$cnt,#31
	mul	$t3,$a3,$bi
	adds	$acc0,$acc0,$t0
	umulh	$t0,$a0,$bi		// hi(a[4..7]*b[4])
	adcs	$acc1,$acc1,$t1
	umulh	$t1,$a1,$bi
	adcs	$acc2,$acc2,$t2
	umulh	$t2,$a2,$bi
	adcs	$acc3,$acc3,$t3
	umulh	$t3,$a3,$bi
	adc	$acc4,xzr,xzr
	ldr	$bi,[$bp,$cnt]		// next b[i]
	adds	$acc1,$acc1,$t0
	mul	$t0,$m0,$mi		// lo(n[4..7]*t[0]*n0)
	adcs	$acc2,$acc2,$t1
	mul	$t1,$m1,$mi
	adcs	$acc3,$acc3,$t2
	mul	$t2,$m2,$mi
	adc	$acc4,$acc4,$t3		// can't overflow
	mul	$t3,$m3,$mi
	adds	$acc0,$acc0,$t0
	umulh	$t0,$m0,$mi		// hi(n[4..7]*t[0]*n0)
	adcs	$acc1,$acc1,$t1
	umulh	$t1,$m1,$mi
	adcs	$acc2,$acc2,$t2
	umulh	$t2,$m2,$mi
	adcs	$acc3,$acc3,$t3
	umulh	$t3,$m3,$mi
	adcs	$acc4,$acc4,$carry
	ldr	$mi,[sp,$cnt]		// next a[0]*n0
	adc	$carry,xzr,xzr
	str	$acc0,[$tp],#8		// result!!!
	adds	$acc0,$acc1,$t0
	sub	$t0,$ap_end,$ap		// done yet?
	adcs	$acc1,$acc2,$t1
	adcs	$acc2,$acc3,$t2
	adcs	$acc3,$acc4,$t3
	//adc	$carry,$carry,xzr
	cbnz	$cnt,.Loop_mul4x_tail

	sub	$t1,$np,$num		// rewinded np?
	adc	$carry,$carry,xzr
	cbz	$t0,.Loop_mul4x_break

	ldp	$t0,$t1,[$tp,#8*4]
	ldp	$t2,$t3,[$tp,#8*6]
	ldp	$a0,$a1,[$ap,#8*0]
	ldp	$a2,$a3,[$ap,#8*2]
	add	$ap,$ap,#8*4
	adds	$acc0,$acc0,$t0
	adcs	$acc1,$acc1,$t1
	adcs	$acc2,$acc2,$t2
	adcs	$acc3,$acc3,$t3
	//adc	$carry,$carry,xzr
	ldp	$m0,$m1,[$np,#8*0]
	ldp	$m2,$m3,[$np,#8*2]
	add	$np,$np,#8*4
	b	.Loop_mul4x_tail

.align	4
.Loop_mul4x_break:
	ldp	$t2,$t3,[x29,#96]	// pull rp and &b[num]
	adds	$acc0,$acc0,$topmost
	add	$bp,$bp,#8*4		// bp++
	adcs	$acc1,$acc1,xzr
	sub	$ap,$ap,$num		// rewind ap
	adcs	$acc2,$acc2,xzr
	stp	$acc0,$acc1,[$tp,#8*0]	// result!!!
	adcs	$acc3,$acc3,xzr
	ldp	$acc0,$acc1,[sp,#8*4]	// t[0..3]
	adc	$topmost,$carry,xzr
	stp	$acc2,$acc3,[$tp,#8*2]	// result!!!
	cmp	$bp,$t3			// done yet?
	ldp	$acc2,$acc3,[sp,#8*6]
	ldp	$m0,$m1,[$t1,#8*0]	// n[0..3]
	ldp	$m2,$m3,[$t1,#8*2]
	add	$np,$t1,#8*4
	b.eq	.Lmul4x_post

	ldr	$bi,[$bp]
	ldp	$a0,$a1,[$ap,#8*0]	// a[0..3]
	ldp	$a2,$a3,[$ap,#8*2]
	adds	$ap,$ap,#8*4		// clear carry bit
	mov	$carry,xzr
	mov	$tp,sp
	b	.Loop_mul4x_reduction

.align	4
.Lmul4x_post:
	// Final step. We see if result is larger than modulus, and
	// if it is, subtract the modulus. But comparison implies
	// subtraction. So we subtract modulus, see if it borrowed,
	// and conditionally copy original value.
	mov	$rp,$t2
	mov	$ap_end,$t2		// $rp copy
	subs	$t0,$acc0,$m0
	add	$tp,sp,#8*8
	sbcs	$t1,$acc1,$m1
	sub	$cnt,$num,#8*4

.Lmul4x_sub:
	sbcs	$t2,$acc2,$m2
	ldp	$m0,$m1,[$np,#8*0]
	sub	$cnt,$cnt,#8*4
	ldp	$acc0,$acc1,[$tp,#8*0]
	sbcs	$t3,$acc3,$m3
	ldp	$m2,$m3,[$np,#8*2]
	add	$np,$np,#8*4
	ldp	$acc2,$acc3,[$tp,#8*2]
	add	$tp,$tp,#8*4
	stp	$t0,$t1,[$rp,#8*0]
	sbcs	$t0,$acc0,$m0
	stp	$t2,$t3,[$rp,#8*2]
	add	$rp,$rp,#8*4
	sbcs	$t1,$acc1,$m1
	cbnz	$cnt,.Lmul4x_sub

	sbcs	$t2,$acc2,$m2
	 mov	$tp,sp
	 add	$ap,sp,#8*4
	 ldp	$a0,$a1,[$ap_end,#8*0]
	sbcs	$t3,$acc3,$m3
	stp	$t0,$t1,[$rp,#8*0]
	 ldp	$a2,$a3,[$ap_end,#8*2]
	stp	$t2,$t3,[$rp,#8*2]
	 ldp	$acc0,$acc1,[$ap,#8*0]
	 ldp	$acc2,$acc3,[$ap,#8*2]
	sbcs	xzr,$topmost,xzr	// did it borrow?
	ldr	x30,[x29,#8]		// pull return address

	sub	$cnt,$num,#8*4
.Lmul4x_cond_copy:
	sub	$cnt,$cnt,#8*4
	csel	$t0,$acc0,$a0,lo
	 stp	xzr,xzr,[$tp,#8*0]
	csel	$t1,$acc1,$a1,lo
	ldp	$a0,$a1,[$ap_end,#8*4]
	ldp	$acc0,$acc1,[$ap,#8*4]
	csel	$t2,$acc2,$a2,lo
	 stp	xzr,xzr,[$tp,#8*2]
	 add	$tp,$tp,#8*4
	csel	$t3,$acc3,$a3,lo
	ldp	$a2,$a3,[$ap_end,#8*6]
	ldp	$acc2,$acc3,[$ap,#8*6]
	add	$ap,$ap,#8*4
	stp	$t0,$t1,[$ap_end,#8*0]
	stp	$t2,$t3,[$ap_end,#8*2]
	add	$ap_end,$ap_end,#8*4
	cbnz	$cnt,.Lmul4x_cond_copy

	csel	$t0,$acc0,$a0,lo
	 stp	xzr,xzr,[$tp,#8*0]
	csel	$t1,$acc1,$a1,lo
	 stp	xzr,xzr,[$tp,#8*2]
	csel	$t2,$acc2,$a2,lo
	 stp	xzr,xzr,[$tp,#8*3]
	csel	$t3,$acc3,$a3,lo
	 stp	xzr,xzr,[$tp,#8*4]
	stp	$t0,$t1,[$ap_end,#8*0]
	stp	$t2,$t3,[$ap_end,#8*2]

	b	.Lmul4x_done

.align	4
.Lmul4x4_post_condition:
	adc	$carry,$carry,xzr
	ldr	$ap,[x29,#96]		// pull rp
	// $acc0-3,$carry hold result, $m0-7 hold modulus
	subs	$a0,$acc0,$m0
	ldr	x30,[x29,#8]		// pull return address
	sbcs	$a1,$acc1,$m1
	 stp	xzr,xzr,[sp,#8*0]
	sbcs	$a2,$acc2,$m2
	 stp	xzr,xzr,[sp,#8*2]
	sbcs	$a3,$acc3,$m3
	 stp	xzr,xzr,[sp,#8*4]
	sbcs	xzr,$carry,xzr		// did it borrow?
	 stp	xzr,xzr,[sp,#8*6]

	// $a0-3 hold result-modulus
	csel	$a0,$acc0,$a0,lo
	csel	$a1,$acc1,$a1,lo
	csel	$a2,$acc2,$a2,lo
	csel	$a3,$acc3,$a3,lo
	stp	$a0,$a1,[$ap,#8*0]
	stp	$a2,$a3,[$ap,#8*2]

.Lmul4x_done:
	ldp	x19,x20,[x29,#16]
	mov	sp,x29
	ldp	x21,x22,[x29,#32]
	mov	x0,#1
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldr	x29,[sp],#128
	.inst	0xd50323bf		// autiasp
	ret
.size	__bn_mul4x_mont,.-__bn_mul4x_mont
___
}
$code.=<<___;
.asciz	"Montgomery Multiplication for ARMv8, CRYPTOGAMS by <appro\@openssl.org>"
.align	4
___

print $code;

close STDOUT;

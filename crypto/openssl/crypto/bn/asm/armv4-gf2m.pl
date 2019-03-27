#! /usr/bin/env perl
# Copyright 2011-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# May 2011
#
# The module implements bn_GF2m_mul_2x2 polynomial multiplication
# used in bn_gf2m.c. It's kind of low-hanging mechanical port from
# C for the time being... Except that it has two code paths: pure
# integer code suitable for any ARMv4 and later CPU and NEON code
# suitable for ARMv7. Pure integer 1x1 multiplication subroutine runs
# in ~45 cycles on dual-issue core such as Cortex A8, which is ~50%
# faster than compiler-generated code. For ECDH and ECDSA verify (but
# not for ECDSA sign) it means 25%-45% improvement depending on key
# length, more for longer keys. Even though NEON 1x1 multiplication
# runs in even less cycles, ~30, improvement is measurable only on
# longer keys. One has to optimize code elsewhere to get NEON glow...
#
# April 2014
#
# Double bn_GF2m_mul_2x2 performance by using algorithm from paper
# referred below, which improves ECDH and ECDSA verify benchmarks
# by 18-40%.
#
# Câmara, D.; Gouvêa, C. P. L.; López, J. & Dahab, R.: Fast Software
# Polynomial Multiplication on ARM Processors using the NEON Engine.
#
# http://conradoplg.cryptoland.net/files/2010/12/mocrysen13.pdf

$flavour = shift;
if ($flavour=~/\w[\w\-]*\.\w+$/) { $output=$flavour; undef $flavour; }
else { while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {} }

if ($flavour && $flavour ne "void") {
    $0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
    ( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
    ( $xlate="${dir}../../perlasm/arm-xlate.pl" and -f $xlate) or
    die "can't locate arm-xlate.pl";

    open STDOUT,"| \"$^X\" $xlate $flavour $output";
} else {
    open STDOUT,">$output";
}

$code=<<___;
#include "arm_arch.h"

.text
#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif
___
################
# private interface to mul_1x1_ialu
#
$a="r1";
$b="r0";

($a0,$a1,$a2,$a12,$a4,$a14)=
($hi,$lo,$t0,$t1, $i0,$i1 )=map("r$_",(4..9),12);

$mask="r12";

$code.=<<___;
.type	mul_1x1_ialu,%function
.align	5
mul_1x1_ialu:
	mov	$a0,#0
	bic	$a1,$a,#3<<30		@ a1=a&0x3fffffff
	str	$a0,[sp,#0]		@ tab[0]=0
	add	$a2,$a1,$a1		@ a2=a1<<1
	str	$a1,[sp,#4]		@ tab[1]=a1
	eor	$a12,$a1,$a2		@ a1^a2
	str	$a2,[sp,#8]		@ tab[2]=a2
	mov	$a4,$a1,lsl#2		@ a4=a1<<2
	str	$a12,[sp,#12]		@ tab[3]=a1^a2
	eor	$a14,$a1,$a4		@ a1^a4
	str	$a4,[sp,#16]		@ tab[4]=a4
	eor	$a0,$a2,$a4		@ a2^a4
	str	$a14,[sp,#20]		@ tab[5]=a1^a4
	eor	$a12,$a12,$a4		@ a1^a2^a4
	str	$a0,[sp,#24]		@ tab[6]=a2^a4
	and	$i0,$mask,$b,lsl#2
	str	$a12,[sp,#28]		@ tab[7]=a1^a2^a4

	and	$i1,$mask,$b,lsr#1
	ldr	$lo,[sp,$i0]		@ tab[b       & 0x7]
	and	$i0,$mask,$b,lsr#4
	ldr	$t1,[sp,$i1]		@ tab[b >>  3 & 0x7]
	and	$i1,$mask,$b,lsr#7
	ldr	$t0,[sp,$i0]		@ tab[b >>  6 & 0x7]
	eor	$lo,$lo,$t1,lsl#3	@ stall
	mov	$hi,$t1,lsr#29
	ldr	$t1,[sp,$i1]		@ tab[b >>  9 & 0x7]

	and	$i0,$mask,$b,lsr#10
	eor	$lo,$lo,$t0,lsl#6
	eor	$hi,$hi,$t0,lsr#26
	ldr	$t0,[sp,$i0]		@ tab[b >> 12 & 0x7]

	and	$i1,$mask,$b,lsr#13
	eor	$lo,$lo,$t1,lsl#9
	eor	$hi,$hi,$t1,lsr#23
	ldr	$t1,[sp,$i1]		@ tab[b >> 15 & 0x7]

	and	$i0,$mask,$b,lsr#16
	eor	$lo,$lo,$t0,lsl#12
	eor	$hi,$hi,$t0,lsr#20
	ldr	$t0,[sp,$i0]		@ tab[b >> 18 & 0x7]

	and	$i1,$mask,$b,lsr#19
	eor	$lo,$lo,$t1,lsl#15
	eor	$hi,$hi,$t1,lsr#17
	ldr	$t1,[sp,$i1]		@ tab[b >> 21 & 0x7]

	and	$i0,$mask,$b,lsr#22
	eor	$lo,$lo,$t0,lsl#18
	eor	$hi,$hi,$t0,lsr#14
	ldr	$t0,[sp,$i0]		@ tab[b >> 24 & 0x7]

	and	$i1,$mask,$b,lsr#25
	eor	$lo,$lo,$t1,lsl#21
	eor	$hi,$hi,$t1,lsr#11
	ldr	$t1,[sp,$i1]		@ tab[b >> 27 & 0x7]

	tst	$a,#1<<30
	and	$i0,$mask,$b,lsr#28
	eor	$lo,$lo,$t0,lsl#24
	eor	$hi,$hi,$t0,lsr#8
	ldr	$t0,[sp,$i0]		@ tab[b >> 30      ]

#ifdef	__thumb2__
	itt	ne
#endif
	eorne	$lo,$lo,$b,lsl#30
	eorne	$hi,$hi,$b,lsr#2
	tst	$a,#1<<31
	eor	$lo,$lo,$t1,lsl#27
	eor	$hi,$hi,$t1,lsr#5
#ifdef	__thumb2__
	itt	ne
#endif
	eorne	$lo,$lo,$b,lsl#31
	eorne	$hi,$hi,$b,lsr#1
	eor	$lo,$lo,$t0,lsl#30
	eor	$hi,$hi,$t0,lsr#2

	mov	pc,lr
.size	mul_1x1_ialu,.-mul_1x1_ialu
___
################
# void	bn_GF2m_mul_2x2(BN_ULONG *r,
#	BN_ULONG a1,BN_ULONG a0,
#	BN_ULONG b1,BN_ULONG b0);	# r[3..0]=a1a0·b1b0
{
$code.=<<___;
.global	bn_GF2m_mul_2x2
.type	bn_GF2m_mul_2x2,%function
.align	5
bn_GF2m_mul_2x2:
#if __ARM_MAX_ARCH__>=7
	stmdb	sp!,{r10,lr}
	ldr	r12,.LOPENSSL_armcap
	adr	r10,.LOPENSSL_armcap
	ldr	r12,[r12,r10]
#ifdef	__APPLE__
	ldr	r12,[r12]
#endif
	tst	r12,#ARMV7_NEON
	itt	ne
	ldrne	r10,[sp],#8
	bne	.LNEON
	stmdb	sp!,{r4-r9}
#else
	stmdb	sp!,{r4-r10,lr}
#endif
___
$ret="r10";	# reassigned 1st argument
$code.=<<___;
	mov	$ret,r0			@ reassign 1st argument
	mov	$b,r3			@ $b=b1
	sub	r7,sp,#36
	mov	r8,sp
	and	r7,r7,#-32
	ldr	r3,[sp,#32]		@ load b0
	mov	$mask,#7<<2
	mov	sp,r7			@ allocate tab[8]
	str	r8,[r7,#32]

	bl	mul_1x1_ialu		@ a1·b1
	str	$lo,[$ret,#8]
	str	$hi,[$ret,#12]

	eor	$b,$b,r3		@ flip b0 and b1
	 eor	$a,$a,r2		@ flip a0 and a1
	eor	r3,r3,$b
	 eor	r2,r2,$a
	eor	$b,$b,r3
	 eor	$a,$a,r2
	bl	mul_1x1_ialu		@ a0·b0
	str	$lo,[$ret]
	str	$hi,[$ret,#4]

	eor	$a,$a,r2
	eor	$b,$b,r3
	bl	mul_1x1_ialu		@ (a1+a0)·(b1+b0)
___
@r=map("r$_",(6..9));
$code.=<<___;
	ldmia	$ret,{@r[0]-@r[3]}
	eor	$lo,$lo,$hi
	ldr	sp,[sp,#32]		@ destroy tab[8]
	eor	$hi,$hi,@r[1]
	eor	$lo,$lo,@r[0]
	eor	$hi,$hi,@r[2]
	eor	$lo,$lo,@r[3]
	eor	$hi,$hi,@r[3]
	str	$hi,[$ret,#8]
	eor	$lo,$lo,$hi
	str	$lo,[$ret,#4]

#if __ARM_ARCH__>=5
	ldmia	sp!,{r4-r10,pc}
#else
	ldmia	sp!,{r4-r10,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
___
}
{
my ($r,$t0,$t1,$t2,$t3)=map("q$_",(0..3,8..12));
my ($a,$b,$k48,$k32,$k16)=map("d$_",(26..31));

$code.=<<___;
#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.align	5
.LNEON:
	ldr		r12, [sp]		@ 5th argument
	vmov		$a, r2, r1
	vmov		$b, r12, r3
	vmov.i64	$k48, #0x0000ffffffffffff
	vmov.i64	$k32, #0x00000000ffffffff
	vmov.i64	$k16, #0x000000000000ffff

	vext.8		$t0#lo, $a, $a, #1	@ A1
	vmull.p8	$t0, $t0#lo, $b		@ F = A1*B
	vext.8		$r#lo, $b, $b, #1	@ B1
	vmull.p8	$r, $a, $r#lo		@ E = A*B1
	vext.8		$t1#lo, $a, $a, #2	@ A2
	vmull.p8	$t1, $t1#lo, $b		@ H = A2*B
	vext.8		$t3#lo, $b, $b, #2	@ B2
	vmull.p8	$t3, $a, $t3#lo		@ G = A*B2
	vext.8		$t2#lo, $a, $a, #3	@ A3
	veor		$t0, $t0, $r		@ L = E + F
	vmull.p8	$t2, $t2#lo, $b		@ J = A3*B
	vext.8		$r#lo, $b, $b, #3	@ B3
	veor		$t1, $t1, $t3		@ M = G + H
	vmull.p8	$r, $a, $r#lo		@ I = A*B3
	veor		$t0#lo, $t0#lo, $t0#hi	@ t0 = (L) (P0 + P1) << 8
	vand		$t0#hi, $t0#hi, $k48
	vext.8		$t3#lo, $b, $b, #4	@ B4
	veor		$t1#lo, $t1#lo, $t1#hi	@ t1 = (M) (P2 + P3) << 16
	vand		$t1#hi, $t1#hi, $k32
	vmull.p8	$t3, $a, $t3#lo		@ K = A*B4
	veor		$t2, $t2, $r		@ N = I + J
	veor		$t0#lo, $t0#lo, $t0#hi
	veor		$t1#lo, $t1#lo, $t1#hi
	veor		$t2#lo, $t2#lo, $t2#hi	@ t2 = (N) (P4 + P5) << 24
	vand		$t2#hi, $t2#hi, $k16
	vext.8		$t0, $t0, $t0, #15
	veor		$t3#lo, $t3#lo, $t3#hi	@ t3 = (K) (P6 + P7) << 32
	vmov.i64	$t3#hi, #0
	vext.8		$t1, $t1, $t1, #14
	veor		$t2#lo, $t2#lo, $t2#hi
	vmull.p8	$r, $a, $b		@ D = A*B
	vext.8		$t3, $t3, $t3, #12
	vext.8		$t2, $t2, $t2, #13
	veor		$t0, $t0, $t1
	veor		$t2, $t2, $t3
	veor		$r, $r, $t0
	veor		$r, $r, $t2

	vst1.32		{$r}, [r0]
	ret		@ bx lr
#endif
___
}
$code.=<<___;
.size	bn_GF2m_mul_2x2,.-bn_GF2m_mul_2x2
#if __ARM_MAX_ARCH__>=7
.align	5
.LOPENSSL_armcap:
.word	OPENSSL_armcap_P-.
#endif
.asciz	"GF(2^m) Multiplication for ARMv4/NEON, CRYPTOGAMS by <appro\@openssl.org>"
.align	5

#if __ARM_MAX_ARCH__>=7
.comm	OPENSSL_armcap_P,4,4
#endif
___

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/geo;

	s/\bq([0-9]+)#(lo|hi)/sprintf "d%d",2*$1+($2 eq "hi")/geo	or
	s/\bret\b/bx	lr/go		or
	s/\bbx\s+lr\b/.word\t0xe12fff1e/go;    # make it possible to compile with -march=armv4

	print $_,"\n";
}
close STDOUT;   # enforce flush

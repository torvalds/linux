#! /usr/bin/env perl
# Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
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
# This module implements Poly1305 hash for SPARCv9, vanilla, as well
# as VIS3 and FMA extensions.
#
# May, August 2015
#
# Numbers are cycles per processed byte with poly1305_blocks alone.
#
#			IALU(*)		FMA
#
# UltraSPARC III	12.3(**)
# SPARC T3		7.92
# SPARC T4		1.70(***)	6.55
# SPARC64 X		5.60		3.64
#
# (*)	Comparison to compiler-generated code is really problematic,
#	because latter's performance varies too much depending on too
#	many variables. For example, one can measure from 5x to 15x
#	improvement on T4 for gcc-4.6. Well, in T4 case it's a bit
#	unfair comparison, because compiler doesn't use VIS3, but
#	given same initial conditions coefficient varies from 3x to 9x.
# (**)	Pre-III performance should be even worse; floating-point
#	performance for UltraSPARC I-IV on the other hand is reported
#	to be 4.25 for hand-coded assembly, but they are just too old
#	to care about.
# (***)	Multi-process benchmark saturates at ~12.5x single-process
#	result on 8-core processor, or ~21GBps per 2.85GHz socket.

my $output = pop;
open STDOUT,">$output";

my ($ctx,$inp,$len,$padbit,$shl,$shr)	= map("%i$_",(0..5));
my ($r0,$r1,$r2,$r3,$s1,$s2,$s3,$h4)	= map("%l$_",(0..7));
my ($h0,$h1,$h2,$h3, $t0,$t1,$t2)	= map("%o$_",(0..5,7));
my ($d0,$d1,$d2,$d3)			= map("%g$_",(1..4));

my $output = pop;
open STDOUT,">$stdout";

$code.=<<___;
#include "sparc_arch.h"

#ifdef	__arch64__
.register	%g2,#scratch
.register	%g3,#scratch
# define	STPTR	stx
# define	SIZE_T	8
#else
# define	STPTR	st
# define	SIZE_T	4
#endif
#define	LOCALS	(STACK_BIAS+STACK_FRAME)

.section	".text",#alloc,#execinstr

#ifdef __PIC__
SPARC_PIC_THUNK(%g1)
#endif

.globl	poly1305_init
.align	32
poly1305_init:
	save	%sp,-STACK_FRAME-16,%sp
	nop

	SPARC_LOAD_ADDRESS(OPENSSL_sparcv9cap_P,%g1)
	ld	[%g1],%g1

	and	%g1,SPARCV9_FMADD|SPARCV9_VIS3,%g1
	cmp	%g1,SPARCV9_FMADD
	be	.Lpoly1305_init_fma
	nop

	stx	%g0,[$ctx+0]
	stx	%g0,[$ctx+8]		! zero hash value
	brz,pn	$inp,.Lno_key
	stx	%g0,[$ctx+16]

	and	$inp,7,$shr		! alignment factor
	andn	$inp,7,$inp
	sll	$shr,3,$shr		! *8
	neg	$shr,$shl

	sethi	%hi(0x0ffffffc),$t0
	set	8,$h1
	or	$t0,%lo(0x0ffffffc),$t0
	set	16,$h2
	sllx	$t0,32,$t1
	or	$t0,$t1,$t1		! 0x0ffffffc0ffffffc
	or	$t1,3,$t0		! 0x0ffffffc0fffffff

	ldxa	[$inp+%g0]0x88,$h0	! load little-endian key
	brz,pt	$shr,.Lkey_aligned
	ldxa	[$inp+$h1]0x88,$h1

	ldxa	[$inp+$h2]0x88,$h2
	srlx	$h0,$shr,$h0
	sllx	$h1,$shl,$t2
	srlx	$h1,$shr,$h1
	or	$t2,$h0,$h0
	sllx	$h2,$shl,$h2
	or	$h2,$h1,$h1

.Lkey_aligned:
	and	$t0,$h0,$h0
	and	$t1,$h1,$h1
	stx	$h0,[$ctx+32+0]		! store key
	stx	$h1,[$ctx+32+8]

	andcc	%g1,SPARCV9_VIS3,%g0
	be	.Lno_key
	nop

1:	call	.+8
	add	%o7,poly1305_blocks_vis3-1b,%o7

	add	%o7,poly1305_emit-poly1305_blocks_vis3,%o5
	STPTR	%o7,[%i2]
	STPTR	%o5,[%i2+SIZE_T]

	ret
	restore	%g0,1,%o0		! return 1

.Lno_key:
	ret
	restore	%g0,%g0,%o0		! return 0
.type	poly1305_init,#function
.size	poly1305_init,.-poly1305_init

.globl	poly1305_blocks
.align	32
poly1305_blocks:
	save	%sp,-STACK_FRAME,%sp
	srln	$len,4,$len

	brz,pn	$len,.Lno_data
	nop

	ld	[$ctx+32+0],$r1		! load key
	ld	[$ctx+32+4],$r0
	ld	[$ctx+32+8],$r3
	ld	[$ctx+32+12],$r2

	ld	[$ctx+0],$h1		! load hash value
	ld	[$ctx+4],$h0
	ld	[$ctx+8],$h3
	ld	[$ctx+12],$h2
	ld	[$ctx+16],$h4

	and	$inp,7,$shr		! alignment factor
	andn	$inp,7,$inp
	set	8,$d1
	sll	$shr,3,$shr		! *8
	set	16,$d2
	neg	$shr,$shl

	srl	$r1,2,$s1
	srl	$r2,2,$s2
	add	$r1,$s1,$s1
	srl	$r3,2,$s3
	add	$r2,$s2,$s2
	add	$r3,$s3,$s3

.Loop:
	ldxa	[$inp+%g0]0x88,$d0	! load little-endian input
	brz,pt	$shr,.Linp_aligned
	ldxa	[$inp+$d1]0x88,$d1

	ldxa	[$inp+$d2]0x88,$d2
	srlx	$d0,$shr,$d0
	sllx	$d1,$shl,$t1
	srlx	$d1,$shr,$d1
	or	$t1,$d0,$d0
	sllx	$d2,$shl,$d2
	or	$d2,$d1,$d1

.Linp_aligned:
	srlx	$d0,32,$t0
	addcc	$d0,$h0,$h0		! accumulate input
	srlx	$d1,32,$t1
	addccc	$t0,$h1,$h1
	addccc	$d1,$h2,$h2
	addccc	$t1,$h3,$h3
	addc	$padbit,$h4,$h4

	umul	$r0,$h0,$d0
	umul	$r1,$h0,$d1
	umul	$r2,$h0,$d2
	umul	$r3,$h0,$d3
	 sub	$len,1,$len
	 add	$inp,16,$inp

	umul	$s3,$h1,$t0
	umul	$r0,$h1,$t1
	umul	$r1,$h1,$t2
	add	$t0,$d0,$d0
	add	$t1,$d1,$d1
	umul	$r2,$h1,$t0
	add	$t2,$d2,$d2
	add	$t0,$d3,$d3

	umul	$s2,$h2,$t1
	umul	$s3,$h2,$t2
	umul	$r0,$h2,$t0
	add	$t1,$d0,$d0
	add	$t2,$d1,$d1
	umul	$r1,$h2,$t1
	add	$t0,$d2,$d2
	add	$t1,$d3,$d3

	umul	$s1,$h3,$t2
	umul	$s2,$h3,$t0
	umul	$s3,$h3,$t1
	add	$t2,$d0,$d0
	add	$t0,$d1,$d1
	umul	$r0,$h3,$t2
	add	$t1,$d2,$d2
	add	$t2,$d3,$d3

	umul	$s1,$h4,$t0
	umul	$s2,$h4,$t1
	umul	$s3,$h4,$t2
	umul	$r0,$h4,$h4
	add	$t0,$d1,$d1
	add	$t1,$d2,$d2
	srlx	$d0,32,$h1
	add	$t2,$d3,$d3
	srlx	$d1,32,$h2

	addcc	$d1,$h1,$h1
	srlx	$d2,32,$h3
	 set	8,$d1
	addccc	$d2,$h2,$h2
	srlx	$d3,32,$t0
	 set	16,$d2
	addccc	$d3,$h3,$h3
	addc	$t0,$h4,$h4

	srl	$h4,2,$t0		! final reduction step
	andn	$h4,3,$t1
	and	$h4,3,$h4
	add	$t1,$t0,$t0

	addcc	$t0,$d0,$h0
	addccc	%g0,$h1,$h1
	addccc	%g0,$h2,$h2
	addccc	%g0,$h3,$h3
	brnz,pt	$len,.Loop
	addc	%g0,$h4,$h4

	st	$h1,[$ctx+0]		! store hash value
	st	$h0,[$ctx+4]
	st	$h3,[$ctx+8]
	st	$h2,[$ctx+12]
	st	$h4,[$ctx+16]

.Lno_data:
	ret
	restore
.type	poly1305_blocks,#function
.size	poly1305_blocks,.-poly1305_blocks
___
########################################################################
# VIS3 has umulxhi and addxc...
{
my ($H0,$H1,$H2,$R0,$R1,$S1,$T1) = map("%o$_",(0..5,7));
my ($D0,$D1,$D2,$T0) = map("%g$_",(1..4));

$code.=<<___;
.align	32
poly1305_blocks_vis3:
	save	%sp,-STACK_FRAME,%sp
	srln	$len,4,$len

	brz,pn	$len,.Lno_data
	nop

	ldx	[$ctx+32+0],$R0		! load key
	ldx	[$ctx+32+8],$R1

	ldx	[$ctx+0],$H0		! load hash value
	ldx	[$ctx+8],$H1
	ld	[$ctx+16],$H2

	and	$inp,7,$shr		! alignment factor
	andn	$inp,7,$inp
	set	8,$r1
	sll	$shr,3,$shr		! *8
	set	16,$r2
	neg	$shr,$shl

	srlx	$R1,2,$S1
	b	.Loop_vis3
	add	$R1,$S1,$S1

.Loop_vis3:
	ldxa	[$inp+%g0]0x88,$D0	! load little-endian input
	brz,pt	$shr,.Linp_aligned_vis3
	ldxa	[$inp+$r1]0x88,$D1

	ldxa	[$inp+$r2]0x88,$D2
	srlx	$D0,$shr,$D0
	sllx	$D1,$shl,$T1
	srlx	$D1,$shr,$D1
	or	$T1,$D0,$D0
	sllx	$D2,$shl,$D2
	or	$D2,$D1,$D1

.Linp_aligned_vis3:
	addcc	$D0,$H0,$H0		! accumulate input
	 sub	$len,1,$len
	addxccc	$D1,$H1,$H1
	 add	$inp,16,$inp

	mulx	$R0,$H0,$D0		! r0*h0
	addxc	$padbit,$H2,$H2
	umulxhi	$R0,$H0,$D1
	mulx	$S1,$H1,$T0		! s1*h1
	umulxhi	$S1,$H1,$T1
	addcc	$T0,$D0,$D0
	mulx	$R1,$H0,$T0		! r1*h0
	addxc	$T1,$D1,$D1
	umulxhi	$R1,$H0,$D2
	addcc	$T0,$D1,$D1
	mulx	$R0,$H1,$T0		! r0*h1
	addxc	%g0,$D2,$D2
	umulxhi	$R0,$H1,$T1
	addcc	$T0,$D1,$D1
	mulx	$S1,$H2,$T0		! s1*h2
	addxc	$T1,$D2,$D2
	mulx	$R0,$H2,$T1		! r0*h2
	addcc	$T0,$D1,$D1
	addxc	$T1,$D2,$D2

	srlx	$D2,2,$T0		! final reduction step
	andn	$D2,3,$T1
	and	$D2,3,$H2
	add	$T1,$T0,$T0

	addcc	$T0,$D0,$H0
	addxccc	%g0,$D1,$H1
	brnz,pt	$len,.Loop_vis3
	addxc	%g0,$H2,$H2

	stx	$H0,[$ctx+0]		! store hash value
	stx	$H1,[$ctx+8]
	st	$H2,[$ctx+16]

	ret
	restore
.type	poly1305_blocks_vis3,#function
.size	poly1305_blocks_vis3,.-poly1305_blocks_vis3
___
}
my ($mac,$nonce) = ($inp,$len);

$code.=<<___;
.globl	poly1305_emit
.align	32
poly1305_emit:
	save	%sp,-STACK_FRAME,%sp

	ld	[$ctx+0],$h1		! load hash value
	ld	[$ctx+4],$h0
	ld	[$ctx+8],$h3
	ld	[$ctx+12],$h2
	ld	[$ctx+16],$h4

	addcc	$h0,5,$r0		! compare to modulus
	addccc	$h1,0,$r1
	addccc	$h2,0,$r2
	addccc	$h3,0,$r3
	addc	$h4,0,$h4
	andcc	$h4,4,%g0		! did it carry/borrow?

	movnz	%icc,$r0,$h0
	ld	[$nonce+0],$r0		! load nonce
	movnz	%icc,$r1,$h1
	ld	[$nonce+4],$r1
	movnz	%icc,$r2,$h2
	ld	[$nonce+8],$r2
	movnz	%icc,$r3,$h3
	ld	[$nonce+12],$r3

	addcc	$r0,$h0,$h0		! accumulate nonce
	addccc	$r1,$h1,$h1
	addccc	$r2,$h2,$h2
	addc	$r3,$h3,$h3

	srl	$h0,8,$r0
	stb	$h0,[$mac+0]		! store little-endian result
	srl	$h0,16,$r1
	stb	$r0,[$mac+1]
	srl	$h0,24,$r2
	stb	$r1,[$mac+2]
	stb	$r2,[$mac+3]

	srl	$h1,8,$r0
	stb	$h1,[$mac+4]
	srl	$h1,16,$r1
	stb	$r0,[$mac+5]
	srl	$h1,24,$r2
	stb	$r1,[$mac+6]
	stb	$r2,[$mac+7]

	srl	$h2,8,$r0
	stb	$h2,[$mac+8]
	srl	$h2,16,$r1
	stb	$r0,[$mac+9]
	srl	$h2,24,$r2
	stb	$r1,[$mac+10]
	stb	$r2,[$mac+11]

	srl	$h3,8,$r0
	stb	$h3,[$mac+12]
	srl	$h3,16,$r1
	stb	$r0,[$mac+13]
	srl	$h3,24,$r2
	stb	$r1,[$mac+14]
	stb	$r2,[$mac+15]

	ret
	restore
.type	poly1305_emit,#function
.size	poly1305_emit,.-poly1305_emit
___

{
my ($ctx,$inp,$len,$padbit) = map("%i$_",(0..3));
my ($in0,$in1,$in2,$in3,$in4) = map("%o$_",(0..4));
my ($i1,$step,$shr,$shl) = map("%l$_",(0..7));
my $i2=$step;

my ($h0lo,$h0hi,$h1lo,$h1hi,$h2lo,$h2hi,$h3lo,$h3hi,
    $two0,$two32,$two64,$two96,$two130,$five_two130,
    $r0lo,$r0hi,$r1lo,$r1hi,$r2lo,$r2hi,
    $s2lo,$s2hi,$s3lo,$s3hi,
    $c0lo,$c0hi,$c1lo,$c1hi,$c2lo,$c2hi,$c3lo,$c3hi) = map("%f".2*$_,(0..31));
# borrowings
my ($r3lo,$r3hi,$s1lo,$s1hi) = ($c0lo,$c0hi,$c1lo,$c1hi);
my ($x0,$x1,$x2,$x3) = ($c2lo,$c2hi,$c3lo,$c3hi);
my ($y0,$y1,$y2,$y3) = ($c1lo,$c1hi,$c3hi,$c3lo);

$code.=<<___;
.align	32
poly1305_init_fma:
	save	%sp,-STACK_FRAME-16,%sp
	nop

.Lpoly1305_init_fma:
1:	call	.+8
	add	%o7,.Lconsts_fma-1b,%o7

	ldd	[%o7+8*0],$two0			! load constants
	ldd	[%o7+8*1],$two32
	ldd	[%o7+8*2],$two64
	ldd	[%o7+8*3],$two96
	ldd	[%o7+8*5],$five_two130

	std	$two0,[$ctx+8*0]		! initial hash value, biased 0
	std	$two32,[$ctx+8*1]
	std	$two64,[$ctx+8*2]
	std	$two96,[$ctx+8*3]

	brz,pn	$inp,.Lno_key_fma
	nop

	stx	%fsr,[%sp+LOCALS]		! save original %fsr
	ldx	[%o7+8*6],%fsr			! load new %fsr

	std	$two0,[$ctx+8*4] 		! key "template"
	std	$two32,[$ctx+8*5]
	std	$two64,[$ctx+8*6]
	std	$two96,[$ctx+8*7]

	and	$inp,7,$shr
	andn	$inp,7,$inp			! align pointer
	mov	8,$i1
	sll	$shr,3,$shr
	mov	16,$i2
	neg	$shr,$shl

	ldxa	[$inp+%g0]0x88,$in0		! load little-endian key
	ldxa	[$inp+$i1]0x88,$in2

	brz	$shr,.Lkey_aligned_fma
	sethi	%hi(0xf0000000),$i1		!   0xf0000000

	ldxa	[$inp+$i2]0x88,$in4

	srlx	$in0,$shr,$in0			! align data
	sllx	$in2,$shl,$in1
	srlx	$in2,$shr,$in2
	or	$in1,$in0,$in0
	sllx	$in4,$shl,$in3
	or	$in3,$in2,$in2

.Lkey_aligned_fma:
	or	$i1,3,$i2			!   0xf0000003
	srlx	$in0,32,$in1
	andn	$in0,$i1,$in0			! &=0x0fffffff
	andn	$in1,$i2,$in1			! &=0x0ffffffc
	srlx	$in2,32,$in3
	andn	$in2,$i2,$in2
	andn	$in3,$i2,$in3

	st	$in0,[$ctx+`8*4+4`]		! fill "template"
	st	$in1,[$ctx+`8*5+4`]
	st	$in2,[$ctx+`8*6+4`]
	st	$in3,[$ctx+`8*7+4`]

	ldd	[$ctx+8*4],$h0lo 		! load [biased] key
	ldd	[$ctx+8*5],$h1lo
	ldd	[$ctx+8*6],$h2lo
	ldd	[$ctx+8*7],$h3lo

	fsubd	$h0lo,$two0, $h0lo		! r0
	 ldd	[%o7+8*7],$two0 		! more constants
	fsubd	$h1lo,$two32,$h1lo		! r1
	 ldd	[%o7+8*8],$two32
	fsubd	$h2lo,$two64,$h2lo		! r2
	 ldd	[%o7+8*9],$two64
	fsubd	$h3lo,$two96,$h3lo		! r3
	 ldd	[%o7+8*10],$two96

	fmuld	$five_two130,$h1lo,$s1lo	! s1
	fmuld	$five_two130,$h2lo,$s2lo	! s2
	fmuld	$five_two130,$h3lo,$s3lo	! s3

	faddd	$h0lo,$two0, $h0hi
	faddd	$h1lo,$two32,$h1hi
	faddd	$h2lo,$two64,$h2hi
	faddd	$h3lo,$two96,$h3hi

	fsubd	$h0hi,$two0, $h0hi
	 ldd	[%o7+8*11],$two0		! more constants
	fsubd	$h1hi,$two32,$h1hi
	 ldd	[%o7+8*12],$two32
	fsubd	$h2hi,$two64,$h2hi
	 ldd	[%o7+8*13],$two64
	fsubd	$h3hi,$two96,$h3hi

	fsubd	$h0lo,$h0hi,$h0lo
	 std	$h0hi,[$ctx+8*5] 		! r0hi
	fsubd	$h1lo,$h1hi,$h1lo
	 std	$h1hi,[$ctx+8*7] 		! r1hi
	fsubd	$h2lo,$h2hi,$h2lo
	 std	$h2hi,[$ctx+8*9] 		! r2hi
	fsubd	$h3lo,$h3hi,$h3lo
	 std	$h3hi,[$ctx+8*11]		! r3hi

	faddd	$s1lo,$two0, $s1hi
	faddd	$s2lo,$two32,$s2hi
	faddd	$s3lo,$two64,$s3hi

	fsubd	$s1hi,$two0, $s1hi
	fsubd	$s2hi,$two32,$s2hi
	fsubd	$s3hi,$two64,$s3hi

	fsubd	$s1lo,$s1hi,$s1lo
	fsubd	$s2lo,$s2hi,$s2lo
	fsubd	$s3lo,$s3hi,$s3lo

	ldx	[%sp+LOCALS],%fsr		! restore %fsr

	std	$h0lo,[$ctx+8*4] 		! r0lo
	std	$h1lo,[$ctx+8*6] 		! r1lo
	std	$h2lo,[$ctx+8*8] 		! r2lo
	std	$h3lo,[$ctx+8*10]		! r3lo

	std	$s1hi,[$ctx+8*13]
	std	$s2hi,[$ctx+8*15]
	std	$s3hi,[$ctx+8*17]

	std	$s1lo,[$ctx+8*12]
	std	$s2lo,[$ctx+8*14]
	std	$s3lo,[$ctx+8*16]

	add	%o7,poly1305_blocks_fma-.Lconsts_fma,%o0
	add	%o7,poly1305_emit_fma-.Lconsts_fma,%o1
	STPTR	%o0,[%i2]
	STPTR	%o1,[%i2+SIZE_T]

	ret
	restore	%g0,1,%o0			! return 1

.Lno_key_fma:
	ret
	restore	%g0,%g0,%o0			! return 0
.type	poly1305_init_fma,#function
.size	poly1305_init_fma,.-poly1305_init_fma

.align	32
poly1305_blocks_fma:
	save	%sp,-STACK_FRAME-48,%sp
	srln	$len,4,$len

	brz,pn	$len,.Labort
	sub	$len,1,$len

1:	call	.+8
	add	%o7,.Lconsts_fma-1b,%o7

	ldd	[%o7+8*0],$two0			! load constants
	ldd	[%o7+8*1],$two32
	ldd	[%o7+8*2],$two64
	ldd	[%o7+8*3],$two96
	ldd	[%o7+8*4],$two130
	ldd	[%o7+8*5],$five_two130

	ldd	[$ctx+8*0],$h0lo 		! load [biased] hash value
	ldd	[$ctx+8*1],$h1lo
	ldd	[$ctx+8*2],$h2lo
	ldd	[$ctx+8*3],$h3lo

	std	$two0,[%sp+LOCALS+8*0]		! input "template"
	sethi	%hi((1023+52+96)<<20),$in3
	std	$two32,[%sp+LOCALS+8*1]
	or	$padbit,$in3,$in3
	std	$two64,[%sp+LOCALS+8*2]
	st	$in3,[%sp+LOCALS+8*3]

	and	$inp,7,$shr
	andn	$inp,7,$inp			! align pointer
	mov	8,$i1
	sll	$shr,3,$shr
	mov	16,$step
	neg	$shr,$shl

	ldxa	[$inp+%g0]0x88,$in0		! load little-endian input
	brz	$shr,.Linp_aligned_fma
	ldxa	[$inp+$i1]0x88,$in2

	ldxa	[$inp+$step]0x88,$in4
	add	$inp,8,$inp

	srlx	$in0,$shr,$in0			! align data
	sllx	$in2,$shl,$in1
	srlx	$in2,$shr,$in2
	or	$in1,$in0,$in0
	sllx	$in4,$shl,$in3
	srlx	$in4,$shr,$in4			! pre-shift
	or	$in3,$in2,$in2

.Linp_aligned_fma:
	srlx	$in0,32,$in1
	movrz	$len,0,$step
	srlx	$in2,32,$in3
	add	$step,$inp,$inp			! conditional advance

	st	$in0,[%sp+LOCALS+8*0+4]		! fill "template"
	st	$in1,[%sp+LOCALS+8*1+4]
	st	$in2,[%sp+LOCALS+8*2+4]
	st	$in3,[%sp+LOCALS+8*3+4]

	ldd	[$ctx+8*4],$r0lo 		! load key
	ldd	[$ctx+8*5],$r0hi
	ldd	[$ctx+8*6],$r1lo
	ldd	[$ctx+8*7],$r1hi
	ldd	[$ctx+8*8],$r2lo
	ldd	[$ctx+8*9],$r2hi
	ldd	[$ctx+8*10],$r3lo
	ldd	[$ctx+8*11],$r3hi
	ldd	[$ctx+8*12],$s1lo
	ldd	[$ctx+8*13],$s1hi
	ldd	[$ctx+8*14],$s2lo
	ldd	[$ctx+8*15],$s2hi
	ldd	[$ctx+8*16],$s3lo
	ldd	[$ctx+8*17],$s3hi

	stx	%fsr,[%sp+LOCALS+8*4]		! save original %fsr
	ldx	[%o7+8*6],%fsr			! load new %fsr

	subcc	$len,1,$len
	movrz	$len,0,$step

	ldd	[%sp+LOCALS+8*0],$x0		! load biased input
	ldd	[%sp+LOCALS+8*1],$x1
	ldd	[%sp+LOCALS+8*2],$x2
	ldd	[%sp+LOCALS+8*3],$x3

	fsubd	$h0lo,$two0, $h0lo		! de-bias hash value
	fsubd	$h1lo,$two32,$h1lo
	 ldxa	[$inp+%g0]0x88,$in0		! modulo-scheduled input load
	fsubd	$h2lo,$two64,$h2lo
	fsubd	$h3lo,$two96,$h3lo
	 ldxa	[$inp+$i1]0x88,$in2

	fsubd	$x0,$two0, $x0  		! de-bias input
	fsubd	$x1,$two32,$x1
	fsubd	$x2,$two64,$x2
	fsubd	$x3,$two96,$x3

	brz	$shr,.Linp_aligned_fma2
	add	$step,$inp,$inp			! conditional advance

	sllx	$in0,$shl,$in1			! align data
	srlx	$in0,$shr,$in3
	or	$in1,$in4,$in0
	sllx	$in2,$shl,$in1
	srlx	$in2,$shr,$in4			! pre-shift
	or	$in3,$in1,$in2
.Linp_aligned_fma2:
	srlx	$in0,32,$in1
	srlx	$in2,32,$in3

	faddd	$h0lo,$x0,$x0			! accumulate input
	 stw	$in0,[%sp+LOCALS+8*0+4]
	faddd	$h1lo,$x1,$x1
	 stw	$in1,[%sp+LOCALS+8*1+4]
	faddd	$h2lo,$x2,$x2
	 stw	$in2,[%sp+LOCALS+8*2+4]
	faddd	$h3lo,$x3,$x3
	 stw	$in3,[%sp+LOCALS+8*3+4]

	b	.Lentry_fma
	nop

.align	16
.Loop_fma:
	ldxa	[$inp+%g0]0x88,$in0		! modulo-scheduled input load
	ldxa	[$inp+$i1]0x88,$in2
	movrz	$len,0,$step

	faddd	$y0,$h0lo,$h0lo 		! accumulate input
	faddd	$y1,$h0hi,$h0hi
	faddd	$y2,$h2lo,$h2lo
	faddd	$y3,$h2hi,$h2hi

	brz,pn	$shr,.Linp_aligned_fma3
	add	$step,$inp,$inp			! conditional advance

	sllx	$in0,$shl,$in1			! align data
	srlx	$in0,$shr,$in3
	or	$in1,$in4,$in0
	sllx	$in2,$shl,$in1
	srlx	$in2,$shr,$in4			! pre-shift
	or	$in3,$in1,$in2

.Linp_aligned_fma3:
	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! base 2^48 -> base 2^32
	faddd	$two64,$h1lo,$c1lo
	 srlx	$in0,32,$in1
	faddd	$two64,$h1hi,$c1hi
	 srlx	$in2,32,$in3
	faddd	$two130,$h3lo,$c3lo
	 st	$in0,[%sp+LOCALS+8*0+4]		! fill "template"
	faddd	$two130,$h3hi,$c3hi
	 st	$in1,[%sp+LOCALS+8*1+4]
	faddd	$two32,$h0lo,$c0lo
	 st	$in2,[%sp+LOCALS+8*2+4]
	faddd	$two32,$h0hi,$c0hi
	 st	$in3,[%sp+LOCALS+8*3+4]
	faddd	$two96,$h2lo,$c2lo
	faddd	$two96,$h2hi,$c2hi

	fsubd	$c1lo,$two64,$c1lo
	fsubd	$c1hi,$two64,$c1hi
	fsubd	$c3lo,$two130,$c3lo
	fsubd	$c3hi,$two130,$c3hi
	fsubd	$c0lo,$two32,$c0lo
	fsubd	$c0hi,$two32,$c0hi
	fsubd	$c2lo,$two96,$c2lo
	fsubd	$c2hi,$two96,$c2hi

	fsubd	$h1lo,$c1lo,$h1lo
	fsubd	$h1hi,$c1hi,$h1hi
	fsubd	$h3lo,$c3lo,$h3lo
	fsubd	$h3hi,$c3hi,$h3hi
	fsubd	$h2lo,$c2lo,$h2lo
	fsubd	$h2hi,$c2hi,$h2hi
	fsubd	$h0lo,$c0lo,$h0lo
	fsubd	$h0hi,$c0hi,$h0hi

	faddd	$h1lo,$c0lo,$h1lo
	faddd	$h1hi,$c0hi,$h1hi
	faddd	$h3lo,$c2lo,$h3lo
	faddd	$h3hi,$c2hi,$h3hi
	faddd	$h2lo,$c1lo,$h2lo
	faddd	$h2hi,$c1hi,$h2hi
	fmaddd	$five_two130,$c3lo,$h0lo,$h0lo
	fmaddd	$five_two130,$c3hi,$h0hi,$h0hi

	faddd	$h1lo,$h1hi,$x1
	 ldd	[$ctx+8*12],$s1lo		! reload constants
	faddd	$h3lo,$h3hi,$x3
	 ldd	[$ctx+8*13],$s1hi
	faddd	$h2lo,$h2hi,$x2
	 ldd	[$ctx+8*10],$r3lo
	faddd	$h0lo,$h0hi,$x0
	 ldd	[$ctx+8*11],$r3hi

.Lentry_fma:
	fmuld	$x1,$s3lo,$h0lo
	fmuld	$x1,$s3hi,$h0hi
	fmuld	$x1,$r1lo,$h2lo
	fmuld	$x1,$r1hi,$h2hi
	fmuld	$x1,$r0lo,$h1lo
	fmuld	$x1,$r0hi,$h1hi
	fmuld	$x1,$r2lo,$h3lo
	fmuld	$x1,$r2hi,$h3hi

	fmaddd	$x3,$s1lo,$h0lo,$h0lo
	fmaddd	$x3,$s1hi,$h0hi,$h0hi
	fmaddd	$x3,$s3lo,$h2lo,$h2lo
	fmaddd	$x3,$s3hi,$h2hi,$h2hi
	fmaddd	$x3,$s2lo,$h1lo,$h1lo
	fmaddd	$x3,$s2hi,$h1hi,$h1hi
	fmaddd	$x3,$r0lo,$h3lo,$h3lo
	fmaddd	$x3,$r0hi,$h3hi,$h3hi

	fmaddd	$x2,$s2lo,$h0lo,$h0lo
	fmaddd	$x2,$s2hi,$h0hi,$h0hi
	fmaddd	$x2,$r0lo,$h2lo,$h2lo
	fmaddd	$x2,$r0hi,$h2hi,$h2hi
	fmaddd	$x2,$s3lo,$h1lo,$h1lo
	 ldd	[%sp+LOCALS+8*0],$y0		! load [biased] input
	fmaddd	$x2,$s3hi,$h1hi,$h1hi
	 ldd	[%sp+LOCALS+8*1],$y1
	fmaddd	$x2,$r1lo,$h3lo,$h3lo
	 ldd	[%sp+LOCALS+8*2],$y2
	fmaddd	$x2,$r1hi,$h3hi,$h3hi
	 ldd	[%sp+LOCALS+8*3],$y3

	fmaddd	$x0,$r0lo,$h0lo,$h0lo
	 fsubd	$y0,$two0, $y0  		! de-bias input
	fmaddd	$x0,$r0hi,$h0hi,$h0hi
	 fsubd	$y1,$two32,$y1
	fmaddd	$x0,$r2lo,$h2lo,$h2lo
	 fsubd	$y2,$two64,$y2
	fmaddd	$x0,$r2hi,$h2hi,$h2hi
	 fsubd	$y3,$two96,$y3
	fmaddd	$x0,$r1lo,$h1lo,$h1lo
	fmaddd	$x0,$r1hi,$h1hi,$h1hi
	fmaddd	$x0,$r3lo,$h3lo,$h3lo
	fmaddd	$x0,$r3hi,$h3hi,$h3hi

	bcc	SIZE_T_CC,.Loop_fma
	subcc	$len,1,$len

	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! base 2^48 -> base 2^32
	faddd	$h0lo,$two32,$c0lo
	faddd	$h0hi,$two32,$c0hi
	faddd	$h2lo,$two96,$c2lo
	faddd	$h2hi,$two96,$c2hi
	faddd	$h1lo,$two64,$c1lo
	faddd	$h1hi,$two64,$c1hi
	faddd	$h3lo,$two130,$c3lo
	faddd	$h3hi,$two130,$c3hi

	fsubd	$c0lo,$two32,$c0lo
	fsubd	$c0hi,$two32,$c0hi
	fsubd	$c2lo,$two96,$c2lo
	fsubd	$c2hi,$two96,$c2hi
	fsubd	$c1lo,$two64,$c1lo
	fsubd	$c1hi,$two64,$c1hi
	fsubd	$c3lo,$two130,$c3lo
	fsubd	$c3hi,$two130,$c3hi

	fsubd	$h1lo,$c1lo,$h1lo
	fsubd	$h1hi,$c1hi,$h1hi
	fsubd	$h3lo,$c3lo,$h3lo
	fsubd	$h3hi,$c3hi,$h3hi
	fsubd	$h2lo,$c2lo,$h2lo
	fsubd	$h2hi,$c2hi,$h2hi
	fsubd	$h0lo,$c0lo,$h0lo
	fsubd	$h0hi,$c0hi,$h0hi

	faddd	$h1lo,$c0lo,$h1lo
	faddd	$h1hi,$c0hi,$h1hi
	faddd	$h3lo,$c2lo,$h3lo
	faddd	$h3hi,$c2hi,$h3hi
	faddd	$h2lo,$c1lo,$h2lo
	faddd	$h2hi,$c1hi,$h2hi
	fmaddd	$five_two130,$c3lo,$h0lo,$h0lo
	fmaddd	$five_two130,$c3hi,$h0hi,$h0hi

	faddd	$h1lo,$h1hi,$x1
	faddd	$h3lo,$h3hi,$x3
	faddd	$h2lo,$h2hi,$x2
	faddd	$h0lo,$h0hi,$x0

	faddd	$x1,$two32,$x1  		! bias
	faddd	$x3,$two96,$x3
	faddd	$x2,$two64,$x2
	faddd	$x0,$two0, $x0

	ldx	[%sp+LOCALS+8*4],%fsr		! restore saved %fsr

	std	$x1,[$ctx+8*1]			! store [biased] hash value
	std	$x3,[$ctx+8*3]
	std	$x2,[$ctx+8*2]
	std	$x0,[$ctx+8*0]

.Labort:
	ret
	restore
.type	poly1305_blocks_fma,#function
.size	poly1305_blocks_fma,.-poly1305_blocks_fma
___
{
my ($mac,$nonce)=($inp,$len);

my ($h0,$h1,$h2,$h3,$h4, $d0,$d1,$d2,$d3, $mask
   ) = (map("%l$_",(0..5)),map("%o$_",(0..4)));

$code.=<<___;
.align	32
poly1305_emit_fma:
	save	%sp,-STACK_FRAME,%sp

	ld	[$ctx+8*0+0],$d0		! load hash
	ld	[$ctx+8*0+4],$h0
	ld	[$ctx+8*1+0],$d1
	ld	[$ctx+8*1+4],$h1
	ld	[$ctx+8*2+0],$d2
	ld	[$ctx+8*2+4],$h2
	ld	[$ctx+8*3+0],$d3
	ld	[$ctx+8*3+4],$h3

	sethi	%hi(0xfff00000),$mask
	andn	$d0,$mask,$d0			! mask exponent
	andn	$d1,$mask,$d1
	andn	$d2,$mask,$d2
	andn	$d3,$mask,$d3			! can be partially reduced...
	mov	3,$mask

	srl	$d3,2,$padbit			! ... so reduce
	and	$d3,$mask,$h4
	andn	$d3,$mask,$d3
	add	$padbit,$d3,$d3

	addcc	$d3,$h0,$h0
	addccc	$d0,$h1,$h1
	addccc	$d1,$h2,$h2
	addccc	$d2,$h3,$h3
	addc	%g0,$h4,$h4

	addcc	$h0,5,$d0			! compare to modulus
	addccc	$h1,0,$d1
	addccc	$h2,0,$d2
	addccc	$h3,0,$d3
	addc	$h4,0,$mask

	srl	$mask,2,$mask			! did it carry/borrow?
	neg	$mask,$mask
	sra	$mask,31,$mask			! mask

	andn	$h0,$mask,$h0
	and	$d0,$mask,$d0
	andn	$h1,$mask,$h1
	and	$d1,$mask,$d1
	or	$d0,$h0,$h0
	ld	[$nonce+0],$d0			! load nonce
	andn	$h2,$mask,$h2
	and	$d2,$mask,$d2
	or	$d1,$h1,$h1
	ld	[$nonce+4],$d1
	andn	$h3,$mask,$h3
	and	$d3,$mask,$d3
	or	$d2,$h2,$h2
	ld	[$nonce+8],$d2
	or	$d3,$h3,$h3
	ld	[$nonce+12],$d3

	addcc	$d0,$h0,$h0			! accumulate nonce
	addccc	$d1,$h1,$h1
	addccc	$d2,$h2,$h2
	addc	$d3,$h3,$h3

	stb	$h0,[$mac+0]			! write little-endian result
	srl	$h0,8,$h0
	stb	$h1,[$mac+4]
	srl	$h1,8,$h1
	stb	$h2,[$mac+8]
	srl	$h2,8,$h2
	stb	$h3,[$mac+12]
	srl	$h3,8,$h3

	stb	$h0,[$mac+1]
	srl	$h0,8,$h0
	stb	$h1,[$mac+5]
	srl	$h1,8,$h1
	stb	$h2,[$mac+9]
	srl	$h2,8,$h2
	stb	$h3,[$mac+13]
	srl	$h3,8,$h3

	stb	$h0,[$mac+2]
	srl	$h0,8,$h0
	stb	$h1,[$mac+6]
	srl	$h1,8,$h1
	stb	$h2,[$mac+10]
	srl	$h2,8,$h2
	stb	$h3,[$mac+14]
	srl	$h3,8,$h3

	stb	$h0,[$mac+3]
	stb	$h1,[$mac+7]
	stb	$h2,[$mac+11]
	stb	$h3,[$mac+15]

	ret
	restore
.type	poly1305_emit_fma,#function
.size	poly1305_emit_fma,.-poly1305_emit_fma
___
}

$code.=<<___;
.align	64
.Lconsts_fma:
.word	0x43300000,0x00000000		! 2^(52+0)
.word	0x45300000,0x00000000		! 2^(52+32)
.word	0x47300000,0x00000000		! 2^(52+64)
.word	0x49300000,0x00000000		! 2^(52+96)
.word	0x4b500000,0x00000000		! 2^(52+130)

.word	0x37f40000,0x00000000		! 5/2^130
.word	0,1<<30				! fsr: truncate, no exceptions

.word	0x44300000,0x00000000		! 2^(52+16+0)
.word	0x46300000,0x00000000		! 2^(52+16+32)
.word	0x48300000,0x00000000		! 2^(52+16+64)
.word	0x4a300000,0x00000000		! 2^(52+16+96)
.word	0x3e300000,0x00000000		! 2^(52+16+0-96)
.word	0x40300000,0x00000000		! 2^(52+16+32-96)
.word	0x42300000,0x00000000		! 2^(52+16+64-96)
.asciz	"Poly1305 for SPARCv9/VIS3/FMA, CRYPTOGAMS by <appro\@openssl.org>"
.align	4
___
}

# Purpose of these subroutines is to explicitly encode VIS instructions,
# so that one can compile the module without having to specify VIS
# extensions on compiler command line, e.g. -xarch=v9 vs. -xarch=v9a.
# Idea is to reserve for option to produce "universal" binary and let
# programmer detect if current CPU is VIS capable at run-time.
sub unvis3 {
my ($mnemonic,$rs1,$rs2,$rd)=@_;
my %bias = ( "g" => 0, "o" => 8, "l" => 16, "i" => 24 );
my ($ref,$opf);
my %visopf = (	"addxc"		=> 0x011,
		"addxccc"	=> 0x013,
		"umulxhi"	=> 0x016	);

    $ref = "$mnemonic\t$rs1,$rs2,$rd";

    if ($opf=$visopf{$mnemonic}) {
	foreach ($rs1,$rs2,$rd) {
	    return $ref if (!/%([goli])([0-9])/);
	    $_=$bias{$1}+$2;
	}

	return	sprintf ".word\t0x%08x !%s",
			0x81b00000|$rd<<25|$rs1<<14|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

sub unfma {
my ($mnemonic,$rs1,$rs2,$rs3,$rd)=@_;
my ($ref,$opf);
my %fmaopf = (	"fmadds"	=> 0x1,
		"fmaddd"	=> 0x2,
		"fmsubs"	=> 0x5,
		"fmsubd"	=> 0x6		);

    $ref = "$mnemonic\t$rs1,$rs2,$rs3,$rd";

    if ($opf=$fmaopf{$mnemonic}) {
	foreach ($rs1,$rs2,$rs3,$rd) {
	    return $ref if (!/%f([0-9]{1,2})/);
	    $_=$1;
	    if ($1>=32) {
		return $ref if ($1&1);
		# re-encode for upper double register addressing
		$_=($1|$1>>5)&31;
	    }
	}

	return	sprintf ".word\t0x%08x !%s",
			0x81b80000|$rd<<25|$rs1<<14|$rs3<<9|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	s/\b(umulxhi|addxc[c]{0,2})\s+(%[goli][0-7]),\s*(%[goli][0-7]),\s*(%[goli][0-7])/
		&unvis3($1,$2,$3,$4)
	 /ge	or
	s/\b(fmadd[sd])\s+(%f[0-9]+),\s*(%f[0-9]+),\s*(%f[0-9]+),\s*(%f[0-9]+)/
		&unfma($1,$2,$3,$4,$5)
	 /ge;

	print $_,"\n";
}

close STDOUT;

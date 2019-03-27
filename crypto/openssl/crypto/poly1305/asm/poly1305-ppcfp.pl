#! /usr/bin/env perl
# Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
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
# This module implements Poly1305 hash for PowerPC FPU.
#
# June 2015
#
# Numbers are cycles per processed byte with poly1305_blocks alone,
# and improvement coefficients relative to gcc-generated code.
#
# Freescale e300	9.78/+30%
# PPC74x0		6.92/+50%
# PPC970		6.03/+80%
# POWER7		3.50/+30%
# POWER8		3.75/+10%

$flavour = shift;

if ($flavour =~ /64/) {
	$SIZE_T	=8;
	$LRSAVE	=2*$SIZE_T;
	$UCMP	="cmpld";
	$STU	="stdu";
	$POP	="ld";
	$PUSH	="std";
} elsif ($flavour =~ /32/) {
	$SIZE_T	=4;
	$LRSAVE	=$SIZE_T;
	$UCMP	="cmplw";
	$STU	="stwu";
	$POP	="lwz";
	$PUSH	="stw";
} else { die "nonsense $flavour"; }

$LITTLE_ENDIAN = ($flavour=~/le$/) ? 4 : 0;

$LWXLE = $LITTLE_ENDIAN ? "lwzx" : "lwbrx";

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour ".shift || die "can't call $xlate: $!";

$LOCALS=6*$SIZE_T;
$FRAME=$LOCALS+6*8+18*8;

my $sp="r1";

my ($ctx,$inp,$len,$padbit) = map("r$_",(3..6));
my ($in0,$in1,$in2,$in3,$i1,$i2,$i3) = map("r$_",(7..12,6));

my ($h0lo,$h0hi,$h1lo,$h1hi,$h2lo,$h2hi,$h3lo,$h3hi,
    $two0,$two32,$two64,$two96,$two130,$five_two130,
    $r0lo,$r0hi,$r1lo,$r1hi,$r2lo,$r2hi,
    $s2lo,$s2hi,$s3lo,$s3hi,
    $c0lo,$c0hi,$c1lo,$c1hi,$c2lo,$c2hi,$c3lo,$c3hi) = map("f$_",(0..31));
# borrowings
my ($r3lo,$r3hi,$s1lo,$s1hi) = ($c0lo,$c0hi,$c1lo,$c1hi);
my ($x0,$x1,$x2,$x3) = ($c2lo,$c2hi,$c3lo,$c3hi);
my ($y0,$y1,$y2,$y3) = ($c3lo,$c3hi,$c1lo,$c1hi);

$code.=<<___;
.machine	"any"
.text

.globl	.poly1305_init_fpu
.align	6
.poly1305_init_fpu:
	$STU	$sp,-$LOCALS($sp)		# minimal frame
	mflr	$padbit
	$PUSH	$padbit,`$LOCALS+$LRSAVE`($sp)

	bl	LPICmeup

	xor	r0,r0,r0
	mtlr	$padbit				# restore lr

	lfd	$two0,8*0($len)			# load constants
	lfd	$two32,8*1($len)
	lfd	$two64,8*2($len)
	lfd	$two96,8*3($len)
	lfd	$two130,8*4($len)
	lfd	$five_two130,8*5($len)

	stfd	$two0,8*0($ctx)			# initial hash value, biased 0
	stfd	$two32,8*1($ctx)
	stfd	$two64,8*2($ctx)
	stfd	$two96,8*3($ctx)

	$UCMP	$inp,r0
	beq-	Lno_key

	lfd	$h3lo,8*13($len)		# new fpscr
	mffs	$h3hi				# old fpscr

	stfd	$two0,8*4($ctx)			# key "template"
	stfd	$two32,8*5($ctx)
	stfd	$two64,8*6($ctx)
	stfd	$two96,8*7($ctx)

	li	$in1,4
	li	$in2,8
	li	$in3,12
	$LWXLE	$in0,0,$inp			# load key
	$LWXLE	$in1,$in1,$inp
	$LWXLE	$in2,$in2,$inp
	$LWXLE	$in3,$in3,$inp

	lis	$i1,0xf000			#   0xf0000000
	ori	$i2,$i1,3			#   0xf0000003
	andc	$in0,$in0,$i1			# &=0x0fffffff
	andc	$in1,$in1,$i2			# &=0x0ffffffc
	andc	$in2,$in2,$i2
	andc	$in3,$in3,$i2

	stw	$in0,`8*4+(4^$LITTLE_ENDIAN)`($ctx)	# fill "template"
	stw	$in1,`8*5+(4^$LITTLE_ENDIAN)`($ctx)
	stw	$in2,`8*6+(4^$LITTLE_ENDIAN)`($ctx)
	stw	$in3,`8*7+(4^$LITTLE_ENDIAN)`($ctx)

	mtfsf	255,$h3lo			# fpscr
	stfd	$two0,8*18($ctx)		# copy constants to context
	stfd	$two32,8*19($ctx)
	stfd	$two64,8*20($ctx)
	stfd	$two96,8*21($ctx)
	stfd	$two130,8*22($ctx)
	stfd	$five_two130,8*23($ctx)

	lfd	$h0lo,8*4($ctx)			# load [biased] key
	lfd	$h1lo,8*5($ctx)
	lfd	$h2lo,8*6($ctx)
	lfd	$h3lo,8*7($ctx)

	fsub	$h0lo,$h0lo,$two0		# r0
	fsub	$h1lo,$h1lo,$two32		# r1
	fsub	$h2lo,$h2lo,$two64		# r2
	fsub	$h3lo,$h3lo,$two96		# r3

	lfd	$two0,8*6($len)			# more constants
	lfd	$two32,8*7($len)
	lfd	$two64,8*8($len)
	lfd	$two96,8*9($len)

	fmul	$h1hi,$h1lo,$five_two130	# s1
	fmul	$h2hi,$h2lo,$five_two130	# s2
	 stfd	$h3hi,8*15($ctx)		# borrow slot for original fpscr
	fmul	$h3hi,$h3lo,$five_two130	# s3

	fadd	$h0hi,$h0lo,$two0
	 stfd	$h1hi,8*12($ctx)		# put aside for now
	fadd	$h1hi,$h1lo,$two32
	 stfd	$h2hi,8*13($ctx)
	fadd	$h2hi,$h2lo,$two64
	 stfd	$h3hi,8*14($ctx)
	fadd	$h3hi,$h3lo,$two96

	fsub	$h0hi,$h0hi,$two0
	fsub	$h1hi,$h1hi,$two32
	fsub	$h2hi,$h2hi,$two64
	fsub	$h3hi,$h3hi,$two96

	lfd	$two0,8*10($len)		# more constants
	lfd	$two32,8*11($len)
	lfd	$two64,8*12($len)

	fsub	$h0lo,$h0lo,$h0hi
	fsub	$h1lo,$h1lo,$h1hi
	fsub	$h2lo,$h2lo,$h2hi
	fsub	$h3lo,$h3lo,$h3hi

	stfd	$h0hi,8*5($ctx)			# r0hi
	stfd	$h1hi,8*7($ctx)			# r1hi
	stfd	$h2hi,8*9($ctx)			# r2hi
	stfd	$h3hi,8*11($ctx)		# r3hi

	stfd	$h0lo,8*4($ctx)			# r0lo
	stfd	$h1lo,8*6($ctx)			# r1lo
	stfd	$h2lo,8*8($ctx)			# r2lo
	stfd	$h3lo,8*10($ctx)		# r3lo

	lfd	$h1lo,8*12($ctx)		# s1
	lfd	$h2lo,8*13($ctx)		# s2
	lfd	$h3lo,8*14($ctx)		# s3
	lfd	$h0lo,8*15($ctx)		# pull original fpscr

	fadd	$h1hi,$h1lo,$two0
	fadd	$h2hi,$h2lo,$two32
	fadd	$h3hi,$h3lo,$two64

	fsub	$h1hi,$h1hi,$two0
	fsub	$h2hi,$h2hi,$two32
	fsub	$h3hi,$h3hi,$two64

	fsub	$h1lo,$h1lo,$h1hi
	fsub	$h2lo,$h2lo,$h2hi
	fsub	$h3lo,$h3lo,$h3hi

	stfd	$h1hi,8*13($ctx)		# s1hi
	stfd	$h2hi,8*15($ctx)		# s2hi
	stfd	$h3hi,8*17($ctx)		# s3hi

	stfd	$h1lo,8*12($ctx)		# s1lo
	stfd	$h2lo,8*14($ctx)		# s2lo
	stfd	$h3lo,8*16($ctx)		# s3lo

	mtfsf	255,$h0lo			# restore fpscr
Lno_key:
	xor	r3,r3,r3
	addi	$sp,$sp,$LOCALS
	blr
	.long	0
	.byte	0,12,4,1,0x80,0,2,0
.size	.poly1305_init_fpu,.-.poly1305_init_fpu

.globl	.poly1305_blocks_fpu
.align	4
.poly1305_blocks_fpu:
	srwi.	$len,$len,4
	beq-	Labort

	$STU	$sp,-$FRAME($sp)
	mflr	r0
	stfd	f14,`$FRAME-8*18`($sp)
	stfd	f15,`$FRAME-8*17`($sp)
	stfd	f16,`$FRAME-8*16`($sp)
	stfd	f17,`$FRAME-8*15`($sp)
	stfd	f18,`$FRAME-8*14`($sp)
	stfd	f19,`$FRAME-8*13`($sp)
	stfd	f20,`$FRAME-8*12`($sp)
	stfd	f21,`$FRAME-8*11`($sp)
	stfd	f22,`$FRAME-8*10`($sp)
	stfd	f23,`$FRAME-8*9`($sp)
	stfd	f24,`$FRAME-8*8`($sp)
	stfd	f25,`$FRAME-8*7`($sp)
	stfd	f26,`$FRAME-8*6`($sp)
	stfd	f27,`$FRAME-8*5`($sp)
	stfd	f28,`$FRAME-8*4`($sp)
	stfd	f29,`$FRAME-8*3`($sp)
	stfd	f30,`$FRAME-8*2`($sp)
	stfd	f31,`$FRAME-8*1`($sp)
	$PUSH	r0,`$FRAME+$LRSAVE`($sp)

	xor	r0,r0,r0
	li	$in3,1
	mtctr	$len
	neg	$len,$len
	stw	r0,`$LOCALS+8*4+(0^$LITTLE_ENDIAN)`($sp)
	stw	$in3,`$LOCALS+8*4+(4^$LITTLE_ENDIAN)`($sp)

	lfd	$two0,8*18($ctx)		# load constants
	lfd	$two32,8*19($ctx)
	lfd	$two64,8*20($ctx)
	lfd	$two96,8*21($ctx)
	lfd	$two130,8*22($ctx)
	lfd	$five_two130,8*23($ctx)

	lfd	$h0lo,8*0($ctx)			# load [biased] hash value
	lfd	$h1lo,8*1($ctx)
	lfd	$h2lo,8*2($ctx)
	lfd	$h3lo,8*3($ctx)

	stfd	$two0,`$LOCALS+8*0`($sp)	# input "template"
	oris	$in3,$padbit,`(1023+52+96)<<4`
	stfd	$two32,`$LOCALS+8*1`($sp)
	stfd	$two64,`$LOCALS+8*2`($sp)
	stw	$in3,`$LOCALS+8*3+(0^$LITTLE_ENDIAN)`($sp)

	li	$i1,4
	li	$i2,8
	li	$i3,12
	$LWXLE	$in0,0,$inp			# load input
	$LWXLE	$in1,$i1,$inp
	$LWXLE	$in2,$i2,$inp
	$LWXLE	$in3,$i3,$inp
	addi	$inp,$inp,16

	stw	$in0,`$LOCALS+8*0+(4^$LITTLE_ENDIAN)`($sp)	# fill "template"
	stw	$in1,`$LOCALS+8*1+(4^$LITTLE_ENDIAN)`($sp)
	stw	$in2,`$LOCALS+8*2+(4^$LITTLE_ENDIAN)`($sp)
	stw	$in3,`$LOCALS+8*3+(4^$LITTLE_ENDIAN)`($sp)

	mffs	$x0				# original fpscr
	lfd	$x1,`$LOCALS+8*4`($sp)		# new fpscr
	lfd	$r0lo,8*4($ctx)			# load key
	lfd	$r0hi,8*5($ctx)
	lfd	$r1lo,8*6($ctx)
	lfd	$r1hi,8*7($ctx)
	lfd	$r2lo,8*8($ctx)
	lfd	$r2hi,8*9($ctx)
	lfd	$r3lo,8*10($ctx)
	lfd	$r3hi,8*11($ctx)
	lfd	$s1lo,8*12($ctx)
	lfd	$s1hi,8*13($ctx)
	lfd	$s2lo,8*14($ctx)
	lfd	$s2hi,8*15($ctx)
	lfd	$s3lo,8*16($ctx)
	lfd	$s3hi,8*17($ctx)

	stfd	$x0,`$LOCALS+8*4`($sp)		# save original fpscr
	mtfsf	255,$x1

	addic	$len,$len,1
	addze	r0,r0
	slwi.	r0,r0,4
	sub	$inp,$inp,r0			# conditional rewind

	lfd	$x0,`$LOCALS+8*0`($sp)
	lfd	$x1,`$LOCALS+8*1`($sp)
	lfd	$x2,`$LOCALS+8*2`($sp)
	lfd	$x3,`$LOCALS+8*3`($sp)

	fsub	$h0lo,$h0lo,$two0		# de-bias hash value
	 $LWXLE	$in0,0,$inp			# modulo-scheduled input load
	fsub	$h1lo,$h1lo,$two32
	 $LWXLE	$in1,$i1,$inp
	fsub	$h2lo,$h2lo,$two64
	 $LWXLE	$in2,$i2,$inp
	fsub	$h3lo,$h3lo,$two96
	 $LWXLE	$in3,$i3,$inp

	fsub	$x0,$x0,$two0			# de-bias input
	 addi	$inp,$inp,16
	fsub	$x1,$x1,$two32
	fsub	$x2,$x2,$two64
	fsub	$x3,$x3,$two96

	fadd	$x0,$x0,$h0lo			# accumulate input
	 stw	$in0,`$LOCALS+8*0+(4^$LITTLE_ENDIAN)`($sp)
	fadd	$x1,$x1,$h1lo
	 stw	$in1,`$LOCALS+8*1+(4^$LITTLE_ENDIAN)`($sp)
	fadd	$x2,$x2,$h2lo
	 stw	$in2,`$LOCALS+8*2+(4^$LITTLE_ENDIAN)`($sp)
	fadd	$x3,$x3,$h3lo
	 stw	$in3,`$LOCALS+8*3+(4^$LITTLE_ENDIAN)`($sp)

	b	Lentry

.align	4
Loop:
	fsub	$y0,$y0,$two0			# de-bias input
	 addic	$len,$len,1
	fsub	$y1,$y1,$two32
	 addze	r0,r0
	fsub	$y2,$y2,$two64
	 slwi.	r0,r0,4
	fsub	$y3,$y3,$two96
	 sub	$inp,$inp,r0			# conditional rewind

	fadd	$h0lo,$h0lo,$y0			# accumulate input
	fadd	$h0hi,$h0hi,$y1
	fadd	$h2lo,$h2lo,$y2
	fadd	$h2hi,$h2hi,$y3

	######################################### base 2^48 -> base 2^32
	fadd	$c1lo,$h1lo,$two64
	 $LWXLE	$in0,0,$inp			# modulo-scheduled input load
	fadd	$c1hi,$h1hi,$two64
	 $LWXLE	$in1,$i1,$inp
	fadd	$c3lo,$h3lo,$two130
	 $LWXLE	$in2,$i2,$inp
	fadd	$c3hi,$h3hi,$two130
	 $LWXLE	$in3,$i3,$inp
	fadd	$c0lo,$h0lo,$two32
	 addi	$inp,$inp,16
	fadd	$c0hi,$h0hi,$two32
	fadd	$c2lo,$h2lo,$two96
	fadd	$c2hi,$h2hi,$two96

	fsub	$c1lo,$c1lo,$two64
	 stw	$in0,`$LOCALS+8*0+(4^$LITTLE_ENDIAN)`($sp)	# fill "template"
	fsub	$c1hi,$c1hi,$two64
	 stw	$in1,`$LOCALS+8*1+(4^$LITTLE_ENDIAN)`($sp)
	fsub	$c3lo,$c3lo,$two130
	 stw	$in2,`$LOCALS+8*2+(4^$LITTLE_ENDIAN)`($sp)
	fsub	$c3hi,$c3hi,$two130
	 stw	$in3,`$LOCALS+8*3+(4^$LITTLE_ENDIAN)`($sp)
	fsub	$c0lo,$c0lo,$two32
	fsub	$c0hi,$c0hi,$two32
	fsub	$c2lo,$c2lo,$two96
	fsub	$c2hi,$c2hi,$two96

	fsub	$h1lo,$h1lo,$c1lo
	fsub	$h1hi,$h1hi,$c1hi
	fsub	$h3lo,$h3lo,$c3lo
	fsub	$h3hi,$h3hi,$c3hi
	fsub	$h2lo,$h2lo,$c2lo
	fsub	$h2hi,$h2hi,$c2hi
	fsub	$h0lo,$h0lo,$c0lo
	fsub	$h0hi,$h0hi,$c0hi

	fadd	$h1lo,$h1lo,$c0lo
	fadd	$h1hi,$h1hi,$c0hi
	fadd	$h3lo,$h3lo,$c2lo
	fadd	$h3hi,$h3hi,$c2hi
	fadd	$h2lo,$h2lo,$c1lo
	fadd	$h2hi,$h2hi,$c1hi
	fmadd	$h0lo,$c3lo,$five_two130,$h0lo
	fmadd	$h0hi,$c3hi,$five_two130,$h0hi

	fadd	$x1,$h1lo,$h1hi
	 lfd	$s1lo,8*12($ctx)		# reload constants
	fadd	$x3,$h3lo,$h3hi
	 lfd	$s1hi,8*13($ctx)
	fadd	$x2,$h2lo,$h2hi
	 lfd	$r3lo,8*10($ctx)
	fadd	$x0,$h0lo,$h0hi
	 lfd	$r3hi,8*11($ctx)
Lentry:
	fmul	$h0lo,$s3lo,$x1
	fmul	$h0hi,$s3hi,$x1
	fmul	$h2lo,$r1lo,$x1
	fmul	$h2hi,$r1hi,$x1
	fmul	$h1lo,$r0lo,$x1
	fmul	$h1hi,$r0hi,$x1
	fmul	$h3lo,$r2lo,$x1
	fmul	$h3hi,$r2hi,$x1

	fmadd	$h0lo,$s1lo,$x3,$h0lo
	fmadd	$h0hi,$s1hi,$x3,$h0hi
	fmadd	$h2lo,$s3lo,$x3,$h2lo
	fmadd	$h2hi,$s3hi,$x3,$h2hi
	fmadd	$h1lo,$s2lo,$x3,$h1lo
	fmadd	$h1hi,$s2hi,$x3,$h1hi
	fmadd	$h3lo,$r0lo,$x3,$h3lo
	fmadd	$h3hi,$r0hi,$x3,$h3hi

	fmadd	$h0lo,$s2lo,$x2,$h0lo
	fmadd	$h0hi,$s2hi,$x2,$h0hi
	fmadd	$h2lo,$r0lo,$x2,$h2lo
	fmadd	$h2hi,$r0hi,$x2,$h2hi
	fmadd	$h1lo,$s3lo,$x2,$h1lo
	fmadd	$h1hi,$s3hi,$x2,$h1hi
	fmadd	$h3lo,$r1lo,$x2,$h3lo
	fmadd	$h3hi,$r1hi,$x2,$h3hi

	fmadd	$h0lo,$r0lo,$x0,$h0lo
	 lfd	$y0,`$LOCALS+8*0`($sp)		# load [biased] input
	fmadd	$h0hi,$r0hi,$x0,$h0hi
	 lfd	$y1,`$LOCALS+8*1`($sp)
	fmadd	$h2lo,$r2lo,$x0,$h2lo
	 lfd	$y2,`$LOCALS+8*2`($sp)
	fmadd	$h2hi,$r2hi,$x0,$h2hi
	 lfd	$y3,`$LOCALS+8*3`($sp)
	fmadd	$h1lo,$r1lo,$x0,$h1lo
	fmadd	$h1hi,$r1hi,$x0,$h1hi
	fmadd	$h3lo,$r3lo,$x0,$h3lo
	fmadd	$h3hi,$r3hi,$x0,$h3hi

	bdnz	Loop

	######################################### base 2^48 -> base 2^32
	fadd	$c0lo,$h0lo,$two32
	fadd	$c0hi,$h0hi,$two32
	fadd	$c2lo,$h2lo,$two96
	fadd	$c2hi,$h2hi,$two96
	fadd	$c1lo,$h1lo,$two64
	fadd	$c1hi,$h1hi,$two64
	fadd	$c3lo,$h3lo,$two130
	fadd	$c3hi,$h3hi,$two130

	fsub	$c0lo,$c0lo,$two32
	fsub	$c0hi,$c0hi,$two32
	fsub	$c2lo,$c2lo,$two96
	fsub	$c2hi,$c2hi,$two96
	fsub	$c1lo,$c1lo,$two64
	fsub	$c1hi,$c1hi,$two64
	fsub	$c3lo,$c3lo,$two130
	fsub	$c3hi,$c3hi,$two130

	fsub	$h1lo,$h1lo,$c1lo
	fsub	$h1hi,$h1hi,$c1hi
	fsub	$h3lo,$h3lo,$c3lo
	fsub	$h3hi,$h3hi,$c3hi
	fsub	$h2lo,$h2lo,$c2lo
	fsub	$h2hi,$h2hi,$c2hi
	fsub	$h0lo,$h0lo,$c0lo
	fsub	$h0hi,$h0hi,$c0hi

	fadd	$h1lo,$h1lo,$c0lo
	fadd	$h1hi,$h1hi,$c0hi
	fadd	$h3lo,$h3lo,$c2lo
	fadd	$h3hi,$h3hi,$c2hi
	fadd	$h2lo,$h2lo,$c1lo
	fadd	$h2hi,$h2hi,$c1hi
	fmadd	$h0lo,$c3lo,$five_two130,$h0lo
	fmadd	$h0hi,$c3hi,$five_two130,$h0hi

	fadd	$x1,$h1lo,$h1hi
	fadd	$x3,$h3lo,$h3hi
	fadd	$x2,$h2lo,$h2hi
	fadd	$x0,$h0lo,$h0hi

	lfd	$h0lo,`$LOCALS+8*4`($sp)	# pull saved fpscr
	fadd	$x1,$x1,$two32			# bias
	fadd	$x3,$x3,$two96
	fadd	$x2,$x2,$two64
	fadd	$x0,$x0,$two0

	stfd	$x1,8*1($ctx)			# store [biased] hash value
	stfd	$x3,8*3($ctx)
	stfd	$x2,8*2($ctx)
	stfd	$x0,8*0($ctx)

	mtfsf	255,$h0lo			# restore original fpscr
	lfd	f14,`$FRAME-8*18`($sp)
	lfd	f15,`$FRAME-8*17`($sp)
	lfd	f16,`$FRAME-8*16`($sp)
	lfd	f17,`$FRAME-8*15`($sp)
	lfd	f18,`$FRAME-8*14`($sp)
	lfd	f19,`$FRAME-8*13`($sp)
	lfd	f20,`$FRAME-8*12`($sp)
	lfd	f21,`$FRAME-8*11`($sp)
	lfd	f22,`$FRAME-8*10`($sp)
	lfd	f23,`$FRAME-8*9`($sp)
	lfd	f24,`$FRAME-8*8`($sp)
	lfd	f25,`$FRAME-8*7`($sp)
	lfd	f26,`$FRAME-8*6`($sp)
	lfd	f27,`$FRAME-8*5`($sp)
	lfd	f28,`$FRAME-8*4`($sp)
	lfd	f29,`$FRAME-8*3`($sp)
	lfd	f30,`$FRAME-8*2`($sp)
	lfd	f31,`$FRAME-8*1`($sp)
	addi	$sp,$sp,$FRAME
Labort:
	blr
	.long	0
	.byte	0,12,4,1,0x80,0,4,0
.size	.poly1305_blocks_fpu,.-.poly1305_blocks_fpu
___
{
my ($mac,$nonce)=($inp,$len);

my ($h0,$h1,$h2,$h3,$h4, $d0,$d1,$d2,$d3
   ) = map("r$_",(7..11,28..31));
my $mask = "r0";
my $FRAME = (6+4)*$SIZE_T;

$code.=<<___;
.globl	.poly1305_emit_fpu
.align	4
.poly1305_emit_fpu:
	$STU	$sp,-$FRAME($sp)
	mflr	r0
	$PUSH	r28,`$FRAME-$SIZE_T*4`($sp)
	$PUSH	r29,`$FRAME-$SIZE_T*3`($sp)
	$PUSH	r30,`$FRAME-$SIZE_T*2`($sp)
	$PUSH	r31,`$FRAME-$SIZE_T*1`($sp)
	$PUSH	r0,`$FRAME+$LRSAVE`($sp)

	lwz	$d0,`8*0+(0^$LITTLE_ENDIAN)`($ctx)	# load hash
	lwz	$h0,`8*0+(4^$LITTLE_ENDIAN)`($ctx)
	lwz	$d1,`8*1+(0^$LITTLE_ENDIAN)`($ctx)
	lwz	$h1,`8*1+(4^$LITTLE_ENDIAN)`($ctx)
	lwz	$d2,`8*2+(0^$LITTLE_ENDIAN)`($ctx)
	lwz	$h2,`8*2+(4^$LITTLE_ENDIAN)`($ctx)
	lwz	$d3,`8*3+(0^$LITTLE_ENDIAN)`($ctx)
	lwz	$h3,`8*3+(4^$LITTLE_ENDIAN)`($ctx)

	lis	$mask,0xfff0
	andc	$d0,$d0,$mask			# mask exponent
	andc	$d1,$d1,$mask
	andc	$d2,$d2,$mask
	andc	$d3,$d3,$mask			# can be partially reduced...
	li	$mask,3

	srwi	$padbit,$d3,2			# ... so reduce
	and	$h4,$d3,$mask
	andc	$d3,$d3,$mask
	add	$d3,$d3,$padbit
___
						if ($SIZE_T==4) {
$code.=<<___;
	addc	$h0,$h0,$d3
	adde	$h1,$h1,$d0
	adde	$h2,$h2,$d1
	adde	$h3,$h3,$d2
	addze	$h4,$h4

	addic	$d0,$h0,5			# compare to modulus
	addze	$d1,$h1
	addze	$d2,$h2
	addze	$d3,$h3
	addze	$mask,$h4

	srwi	$mask,$mask,2			# did it carry/borrow?
	neg	$mask,$mask
	srawi	$mask,$mask,31			# mask

	andc	$h0,$h0,$mask
	and	$d0,$d0,$mask
	andc	$h1,$h1,$mask
	and	$d1,$d1,$mask
	or	$h0,$h0,$d0
	lwz	$d0,0($nonce)			# load nonce
	andc	$h2,$h2,$mask
	and	$d2,$d2,$mask
	or	$h1,$h1,$d1
	lwz	$d1,4($nonce)
	andc	$h3,$h3,$mask
	and	$d3,$d3,$mask
	or	$h2,$h2,$d2
	lwz	$d2,8($nonce)
	or	$h3,$h3,$d3
	lwz	$d3,12($nonce)

	addc	$h0,$h0,$d0			# accumulate nonce
	adde	$h1,$h1,$d1
	adde	$h2,$h2,$d2
	adde	$h3,$h3,$d3
___
						} else {
$code.=<<___;
	add	$h0,$h0,$d3
	add	$h1,$h1,$d0
	add	$h2,$h2,$d1
	add	$h3,$h3,$d2

	srdi	$d0,$h0,32
	add	$h1,$h1,$d0
	srdi	$d1,$h1,32
	add	$h2,$h2,$d1
	srdi	$d2,$h2,32
	add	$h3,$h3,$d2
	srdi	$d3,$h3,32
	add	$h4,$h4,$d3

	insrdi	$h0,$h1,32,0
	insrdi	$h2,$h3,32,0

	addic	$d0,$h0,5			# compare to modulus
	addze	$d1,$h2
	addze	$d2,$h4

	srdi	$mask,$d2,2			# did it carry/borrow?
	neg	$mask,$mask
	sradi	$mask,$mask,63			# mask
	ld	$d2,0($nonce)			# load nonce
	ld	$d3,8($nonce)

	andc	$h0,$h0,$mask
	and	$d0,$d0,$mask
	andc	$h2,$h2,$mask
	and	$d1,$d1,$mask
	or	$h0,$h0,$d0
	or	$h2,$h2,$d1
___
$code.=<<___	if (!$LITTLE_ENDIAN);
	rotldi	$d2,$d2,32			# flip nonce words
	rotldi	$d3,$d3,32
___
$code.=<<___;
	addc	$h0,$h0,$d2			# accumulate nonce
	adde	$h2,$h2,$d3

	srdi	$h1,$h0,32
	srdi	$h3,$h2,32
___
						}
$code.=<<___	if ($LITTLE_ENDIAN);
	stw	$h0,0($mac)			# write result
	stw	$h1,4($mac)
	stw	$h2,8($mac)
	stw	$h3,12($mac)
___
$code.=<<___	if (!$LITTLE_ENDIAN);
	li	$d1,4
	stwbrx	$h0,0,$mac			# write result
	li	$d2,8
	stwbrx	$h1,$d1,$mac
	li	$d3,12
	stwbrx	$h2,$d2,$mac
	stwbrx	$h3,$d3,$mac
___
$code.=<<___;
	$POP	r28,`$FRAME-$SIZE_T*4`($sp)
	$POP	r29,`$FRAME-$SIZE_T*3`($sp)
	$POP	r30,`$FRAME-$SIZE_T*2`($sp)
	$POP	r31,`$FRAME-$SIZE_T*1`($sp)
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,4,1,0x80,4,3,0
.size	.poly1305_emit_fpu,.-.poly1305_emit_fpu
___
}
# Ugly hack here, because PPC assembler syntax seem to vary too
# much from platforms to platform...
$code.=<<___;
.align	6
LPICmeup:
	mflr	r0
	bcl	20,31,\$+4
	mflr	$len	# vvvvvv "distance" between . and 1st data entry
	addi	$len,$len,`64-8`	# borrow $len
	mtlr	r0
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,0,0
	.space	`64-9*4`

.quad	0x4330000000000000		# 2^(52+0)
.quad	0x4530000000000000		# 2^(52+32)
.quad	0x4730000000000000		# 2^(52+64)
.quad	0x4930000000000000		# 2^(52+96)
.quad	0x4b50000000000000		# 2^(52+130)

.quad	0x37f4000000000000		# 5/2^130

.quad	0x4430000000000000		# 2^(52+16+0)
.quad	0x4630000000000000		# 2^(52+16+32)
.quad	0x4830000000000000		# 2^(52+16+64)
.quad	0x4a30000000000000		# 2^(52+16+96)
.quad	0x3e30000000000000		# 2^(52+16+0-96)
.quad	0x4030000000000000		# 2^(52+16+32-96)
.quad	0x4230000000000000		# 2^(52+16+64-96)

.quad	0x0000000000000001		# fpscr: truncate, no exceptions
.asciz	"Poly1305 for PPC FPU, CRYPTOGAMS by <appro\@openssl.org>"
.align	4
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

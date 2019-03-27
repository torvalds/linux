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
# October 2015
#
# ChaCha20 for PowerPC/AltiVec.
#
# June 2018
#
# Add VSX 2.07 code path. Original 3xAltiVec+1xIALU is well-suited for
# processors that can't issue more than one vector instruction per
# cycle. But POWER8 (and POWER9) can issue a pair, and vector-only 4x
# interleave would perform better. Incidentally PowerISA 2.07 (first
# implemented by POWER8) defined new usable instructions, hence 4xVSX
# code path...
#
# Performance in cycles per byte out of large buffer.
#
#			IALU/gcc-4.x    3xAltiVec+1xIALU	4xVSX
#
# Freescale e300	13.6/+115%	-			-
# PPC74x0/G4e		6.81/+310%	3.81			-
# PPC970/G5		9.29/+160%	?			-
# POWER7		8.62/+61%	3.35			-
# POWER8		8.70/+51%	2.91			2.09
# POWER9		8.80/+29%	4.44(*)			2.45(**)
#
# (*)	this is trade-off result, it's possible to improve it, but
#	then it would negatively affect all others;
# (**)	POWER9 seems to be "allergic" to mixing vector and integer
#	instructions, which is why switch to vector-only code pays
#	off that much;

$flavour = shift;

if ($flavour =~ /64/) {
	$SIZE_T	=8;
	$LRSAVE	=2*$SIZE_T;
	$STU	="stdu";
	$POP	="ld";
	$PUSH	="std";
	$UCMP	="cmpld";
} elsif ($flavour =~ /32/) {
	$SIZE_T	=4;
	$LRSAVE	=$SIZE_T;
	$STU	="stwu";
	$POP	="lwz";
	$PUSH	="stw";
	$UCMP	="cmplw";
} else { die "nonsense $flavour"; }

$LITTLE_ENDIAN = ($flavour=~/le$/) ? 1 : 0;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour ".shift || die "can't call $xlate: $!";

$LOCALS=6*$SIZE_T;
$FRAME=$LOCALS+64+18*$SIZE_T;	# 64 is for local variables

sub AUTOLOAD()		# thunk [simplified] x86-style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://; $opcode =~ s/_/\./;
    $code .= "\t$opcode\t".join(',',@_)."\n";
}

my $sp = "r1";

my ($out,$inp,$len,$key,$ctr) = map("r$_",(3..7));

my @x=map("r$_",(16..31));
my @d=map("r$_",(11,12,14,15));
my @t=map("r$_",(7..10));

sub ROUND {
my ($a0,$b0,$c0,$d0)=@_;
my ($a1,$b1,$c1,$d1)=map(($_&~3)+(($_+1)&3),($a0,$b0,$c0,$d0));
my ($a2,$b2,$c2,$d2)=map(($_&~3)+(($_+1)&3),($a1,$b1,$c1,$d1));
my ($a3,$b3,$c3,$d3)=map(($_&~3)+(($_+1)&3),($a2,$b2,$c2,$d2));

    (
	"&add		(@x[$a0],@x[$a0],@x[$b0])",
	 "&add		(@x[$a1],@x[$a1],@x[$b1])",
	  "&add		(@x[$a2],@x[$a2],@x[$b2])",
	   "&add	(@x[$a3],@x[$a3],@x[$b3])",
	"&xor		(@x[$d0],@x[$d0],@x[$a0])",
	 "&xor		(@x[$d1],@x[$d1],@x[$a1])",
	  "&xor		(@x[$d2],@x[$d2],@x[$a2])",
	   "&xor	(@x[$d3],@x[$d3],@x[$a3])",
	"&rotlwi	(@x[$d0],@x[$d0],16)",
	 "&rotlwi	(@x[$d1],@x[$d1],16)",
	  "&rotlwi	(@x[$d2],@x[$d2],16)",
	   "&rotlwi	(@x[$d3],@x[$d3],16)",

	"&add		(@x[$c0],@x[$c0],@x[$d0])",
	 "&add		(@x[$c1],@x[$c1],@x[$d1])",
	  "&add		(@x[$c2],@x[$c2],@x[$d2])",
	   "&add	(@x[$c3],@x[$c3],@x[$d3])",
	"&xor		(@x[$b0],@x[$b0],@x[$c0])",
	 "&xor		(@x[$b1],@x[$b1],@x[$c1])",
	  "&xor		(@x[$b2],@x[$b2],@x[$c2])",
	   "&xor	(@x[$b3],@x[$b3],@x[$c3])",
	"&rotlwi	(@x[$b0],@x[$b0],12)",
	 "&rotlwi	(@x[$b1],@x[$b1],12)",
	  "&rotlwi	(@x[$b2],@x[$b2],12)",
	   "&rotlwi	(@x[$b3],@x[$b3],12)",

	"&add		(@x[$a0],@x[$a0],@x[$b0])",
	 "&add		(@x[$a1],@x[$a1],@x[$b1])",
	  "&add		(@x[$a2],@x[$a2],@x[$b2])",
	   "&add	(@x[$a3],@x[$a3],@x[$b3])",
	"&xor		(@x[$d0],@x[$d0],@x[$a0])",
	 "&xor		(@x[$d1],@x[$d1],@x[$a1])",
	  "&xor		(@x[$d2],@x[$d2],@x[$a2])",
	   "&xor	(@x[$d3],@x[$d3],@x[$a3])",
	"&rotlwi	(@x[$d0],@x[$d0],8)",
	 "&rotlwi	(@x[$d1],@x[$d1],8)",
	  "&rotlwi	(@x[$d2],@x[$d2],8)",
	   "&rotlwi	(@x[$d3],@x[$d3],8)",

	"&add		(@x[$c0],@x[$c0],@x[$d0])",
	 "&add		(@x[$c1],@x[$c1],@x[$d1])",
	  "&add		(@x[$c2],@x[$c2],@x[$d2])",
	   "&add	(@x[$c3],@x[$c3],@x[$d3])",
	"&xor		(@x[$b0],@x[$b0],@x[$c0])",
	 "&xor		(@x[$b1],@x[$b1],@x[$c1])",
	  "&xor		(@x[$b2],@x[$b2],@x[$c2])",
	   "&xor	(@x[$b3],@x[$b3],@x[$c3])",
	"&rotlwi	(@x[$b0],@x[$b0],7)",
	 "&rotlwi	(@x[$b1],@x[$b1],7)",
	  "&rotlwi	(@x[$b2],@x[$b2],7)",
	   "&rotlwi	(@x[$b3],@x[$b3],7)"
    );
}

$code.=<<___;
.machine	"any"
.text

.globl	.ChaCha20_ctr32_int
.align	5
.ChaCha20_ctr32_int:
__ChaCha20_ctr32_int:
	${UCMP}i $len,0
	beqlr-

	$STU	$sp,-$FRAME($sp)
	mflr	r0

	$PUSH	r14,`$FRAME-$SIZE_T*18`($sp)
	$PUSH	r15,`$FRAME-$SIZE_T*17`($sp)
	$PUSH	r16,`$FRAME-$SIZE_T*16`($sp)
	$PUSH	r17,`$FRAME-$SIZE_T*15`($sp)
	$PUSH	r18,`$FRAME-$SIZE_T*14`($sp)
	$PUSH	r19,`$FRAME-$SIZE_T*13`($sp)
	$PUSH	r20,`$FRAME-$SIZE_T*12`($sp)
	$PUSH	r21,`$FRAME-$SIZE_T*11`($sp)
	$PUSH	r22,`$FRAME-$SIZE_T*10`($sp)
	$PUSH	r23,`$FRAME-$SIZE_T*9`($sp)
	$PUSH	r24,`$FRAME-$SIZE_T*8`($sp)
	$PUSH	r25,`$FRAME-$SIZE_T*7`($sp)
	$PUSH	r26,`$FRAME-$SIZE_T*6`($sp)
	$PUSH	r27,`$FRAME-$SIZE_T*5`($sp)
	$PUSH	r28,`$FRAME-$SIZE_T*4`($sp)
	$PUSH	r29,`$FRAME-$SIZE_T*3`($sp)
	$PUSH	r30,`$FRAME-$SIZE_T*2`($sp)
	$PUSH	r31,`$FRAME-$SIZE_T*1`($sp)
	$PUSH	r0,`$FRAME+$LRSAVE`($sp)

	lwz	@d[0],0($ctr)			# load counter
	lwz	@d[1],4($ctr)
	lwz	@d[2],8($ctr)
	lwz	@d[3],12($ctr)

	bl	__ChaCha20_1x

	$POP	r0,`$FRAME+$LRSAVE`($sp)
	$POP	r14,`$FRAME-$SIZE_T*18`($sp)
	$POP	r15,`$FRAME-$SIZE_T*17`($sp)
	$POP	r16,`$FRAME-$SIZE_T*16`($sp)
	$POP	r17,`$FRAME-$SIZE_T*15`($sp)
	$POP	r18,`$FRAME-$SIZE_T*14`($sp)
	$POP	r19,`$FRAME-$SIZE_T*13`($sp)
	$POP	r20,`$FRAME-$SIZE_T*12`($sp)
	$POP	r21,`$FRAME-$SIZE_T*11`($sp)
	$POP	r22,`$FRAME-$SIZE_T*10`($sp)
	$POP	r23,`$FRAME-$SIZE_T*9`($sp)
	$POP	r24,`$FRAME-$SIZE_T*8`($sp)
	$POP	r25,`$FRAME-$SIZE_T*7`($sp)
	$POP	r26,`$FRAME-$SIZE_T*6`($sp)
	$POP	r27,`$FRAME-$SIZE_T*5`($sp)
	$POP	r28,`$FRAME-$SIZE_T*4`($sp)
	$POP	r29,`$FRAME-$SIZE_T*3`($sp)
	$POP	r30,`$FRAME-$SIZE_T*2`($sp)
	$POP	r31,`$FRAME-$SIZE_T*1`($sp)
	mtlr	r0
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,4,1,0x80,18,5,0
	.long	0
.size	.ChaCha20_ctr32_int,.-.ChaCha20_ctr32_int

.align	5
__ChaCha20_1x:
Loop_outer:
	lis	@x[0],0x6170			# synthesize sigma
	lis	@x[1],0x3320
	lis	@x[2],0x7962
	lis	@x[3],0x6b20
	ori	@x[0],@x[0],0x7865
	ori	@x[1],@x[1],0x646e
	ori	@x[2],@x[2],0x2d32
	ori	@x[3],@x[3],0x6574

	li	r0,10				# inner loop counter
	lwz	@x[4],0($key)			# load key
	lwz	@x[5],4($key)
	lwz	@x[6],8($key)
	lwz	@x[7],12($key)
	lwz	@x[8],16($key)
	mr	@x[12],@d[0]			# copy counter
	lwz	@x[9],20($key)
	mr	@x[13],@d[1]
	lwz	@x[10],24($key)
	mr	@x[14],@d[2]
	lwz	@x[11],28($key)
	mr	@x[15],@d[3]

	mr	@t[0],@x[4]
	mr	@t[1],@x[5]
	mr	@t[2],@x[6]
	mr	@t[3],@x[7]

	mtctr	r0
Loop:
___
	foreach (&ROUND(0, 4, 8,12)) { eval; }
	foreach (&ROUND(0, 5,10,15)) { eval; }
$code.=<<___;
	bdnz	Loop

	subic	$len,$len,64			# $len-=64
	addi	@x[0],@x[0],0x7865		# accumulate key block
	addi	@x[1],@x[1],0x646e
	addi	@x[2],@x[2],0x2d32
	addi	@x[3],@x[3],0x6574
	addis	@x[0],@x[0],0x6170
	addis	@x[1],@x[1],0x3320
	addis	@x[2],@x[2],0x7962
	addis	@x[3],@x[3],0x6b20

	subfe.	r0,r0,r0			# borrow?-1:0
	add	@x[4],@x[4],@t[0]
	lwz	@t[0],16($key)
	add	@x[5],@x[5],@t[1]
	lwz	@t[1],20($key)
	add	@x[6],@x[6],@t[2]
	lwz	@t[2],24($key)
	add	@x[7],@x[7],@t[3]
	lwz	@t[3],28($key)
	add	@x[8],@x[8],@t[0]
	add	@x[9],@x[9],@t[1]
	add	@x[10],@x[10],@t[2]
	add	@x[11],@x[11],@t[3]

	add	@x[12],@x[12],@d[0]
	add	@x[13],@x[13],@d[1]
	add	@x[14],@x[14],@d[2]
	add	@x[15],@x[15],@d[3]
	addi	@d[0],@d[0],1			# increment counter
___
if (!$LITTLE_ENDIAN) { for($i=0;$i<16;$i++) {	# flip byte order
$code.=<<___;
	mr	@t[$i&3],@x[$i]
	rotlwi	@x[$i],@x[$i],8
	rlwimi	@x[$i],@t[$i&3],24,0,7
	rlwimi	@x[$i],@t[$i&3],24,16,23
___
} }
$code.=<<___;
	bne	Ltail				# $len-=64 borrowed

	lwz	@t[0],0($inp)			# load input, aligned or not
	lwz	@t[1],4($inp)
	${UCMP}i $len,0				# done already?
	lwz	@t[2],8($inp)
	lwz	@t[3],12($inp)
	xor	@x[0],@x[0],@t[0]		# xor with input
	lwz	@t[0],16($inp)
	xor	@x[1],@x[1],@t[1]
	lwz	@t[1],20($inp)
	xor	@x[2],@x[2],@t[2]
	lwz	@t[2],24($inp)
	xor	@x[3],@x[3],@t[3]
	lwz	@t[3],28($inp)
	xor	@x[4],@x[4],@t[0]
	lwz	@t[0],32($inp)
	xor	@x[5],@x[5],@t[1]
	lwz	@t[1],36($inp)
	xor	@x[6],@x[6],@t[2]
	lwz	@t[2],40($inp)
	xor	@x[7],@x[7],@t[3]
	lwz	@t[3],44($inp)
	xor	@x[8],@x[8],@t[0]
	lwz	@t[0],48($inp)
	xor	@x[9],@x[9],@t[1]
	lwz	@t[1],52($inp)
	xor	@x[10],@x[10],@t[2]
	lwz	@t[2],56($inp)
	xor	@x[11],@x[11],@t[3]
	lwz	@t[3],60($inp)
	xor	@x[12],@x[12],@t[0]
	stw	@x[0],0($out)			# store output, aligned or not
	xor	@x[13],@x[13],@t[1]
	stw	@x[1],4($out)
	xor	@x[14],@x[14],@t[2]
	stw	@x[2],8($out)
	xor	@x[15],@x[15],@t[3]
	stw	@x[3],12($out)
	stw	@x[4],16($out)
	stw	@x[5],20($out)
	stw	@x[6],24($out)
	stw	@x[7],28($out)
	stw	@x[8],32($out)
	stw	@x[9],36($out)
	stw	@x[10],40($out)
	stw	@x[11],44($out)
	stw	@x[12],48($out)
	stw	@x[13],52($out)
	stw	@x[14],56($out)
	addi	$inp,$inp,64
	stw	@x[15],60($out)
	addi	$out,$out,64

	bne	Loop_outer

	blr

.align	4
Ltail:
	addi	$len,$len,64			# restore tail length
	subi	$inp,$inp,1			# prepare for *++ptr
	subi	$out,$out,1
	addi	@t[0],$sp,$LOCALS-1
	mtctr	$len

	stw	@x[0],`$LOCALS+0`($sp)		# save whole block to stack
	stw	@x[1],`$LOCALS+4`($sp)
	stw	@x[2],`$LOCALS+8`($sp)
	stw	@x[3],`$LOCALS+12`($sp)
	stw	@x[4],`$LOCALS+16`($sp)
	stw	@x[5],`$LOCALS+20`($sp)
	stw	@x[6],`$LOCALS+24`($sp)
	stw	@x[7],`$LOCALS+28`($sp)
	stw	@x[8],`$LOCALS+32`($sp)
	stw	@x[9],`$LOCALS+36`($sp)
	stw	@x[10],`$LOCALS+40`($sp)
	stw	@x[11],`$LOCALS+44`($sp)
	stw	@x[12],`$LOCALS+48`($sp)
	stw	@x[13],`$LOCALS+52`($sp)
	stw	@x[14],`$LOCALS+56`($sp)
	stw	@x[15],`$LOCALS+60`($sp)

Loop_tail:					# byte-by-byte loop
	lbzu	@d[0],1($inp)
	lbzu	@x[0],1(@t[0])
	xor	@d[1],@d[0],@x[0]
	stbu	@d[1],1($out)
	bdnz	Loop_tail

	stw	$sp,`$LOCALS+0`($sp)		# wipe block on stack
	stw	$sp,`$LOCALS+4`($sp)
	stw	$sp,`$LOCALS+8`($sp)
	stw	$sp,`$LOCALS+12`($sp)
	stw	$sp,`$LOCALS+16`($sp)
	stw	$sp,`$LOCALS+20`($sp)
	stw	$sp,`$LOCALS+24`($sp)
	stw	$sp,`$LOCALS+28`($sp)
	stw	$sp,`$LOCALS+32`($sp)
	stw	$sp,`$LOCALS+36`($sp)
	stw	$sp,`$LOCALS+40`($sp)
	stw	$sp,`$LOCALS+44`($sp)
	stw	$sp,`$LOCALS+48`($sp)
	stw	$sp,`$LOCALS+52`($sp)
	stw	$sp,`$LOCALS+56`($sp)
	stw	$sp,`$LOCALS+60`($sp)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,0,0
___

{{{
my ($A0,$B0,$C0,$D0,$A1,$B1,$C1,$D1,$A2,$B2,$C2,$D2)
				= map("v$_",(0..11));
my @K				= map("v$_",(12..17));
my ($FOUR,$sixteen,$twenty4)	= map("v$_",(18..19,23));
my ($inpperm,$outperm,$outmask)	= map("v$_",(24..26));
my @D				= map("v$_",(27..31));
my ($twelve,$seven,$T0,$T1) = @D;

my $FRAME=$LOCALS+64+10*16+18*$SIZE_T;	# 10*16 is for v23-v31 offload

sub VMXROUND {
my $odd = pop;
my ($a,$b,$c,$d)=@_;

	(
	"&vadduwm	('$a','$a','$b')",
	"&vxor		('$d','$d','$a')",
	"&vperm		('$d','$d','$d','$sixteen')",

	"&vadduwm	('$c','$c','$d')",
	"&vxor		('$b','$b','$c')",
	"&vrlw		('$b','$b','$twelve')",

	"&vadduwm	('$a','$a','$b')",
	"&vxor		('$d','$d','$a')",
	"&vperm		('$d','$d','$d','$twenty4')",

	"&vadduwm	('$c','$c','$d')",
	"&vxor		('$b','$b','$c')",
	"&vrlw		('$b','$b','$seven')",

	"&vrldoi	('$c','$c',8)",
	"&vrldoi	('$b','$b',$odd?4:12)",
	"&vrldoi	('$d','$d',$odd?12:4)"
	);
}

$code.=<<___;

.globl	.ChaCha20_ctr32_vmx
.align	5
.ChaCha20_ctr32_vmx:
	${UCMP}i $len,256
	blt	__ChaCha20_ctr32_int

	$STU	$sp,-$FRAME($sp)
	mflr	r0
	li	r10,`15+$LOCALS+64`
	li	r11,`31+$LOCALS+64`
	mfspr	r12,256
	stvx	v23,r10,$sp
	addi	r10,r10,32
	stvx	v24,r11,$sp
	addi	r11,r11,32
	stvx	v25,r10,$sp
	addi	r10,r10,32
	stvx	v26,r11,$sp
	addi	r11,r11,32
	stvx	v27,r10,$sp
	addi	r10,r10,32
	stvx	v28,r11,$sp
	addi	r11,r11,32
	stvx	v29,r10,$sp
	addi	r10,r10,32
	stvx	v30,r11,$sp
	stvx	v31,r10,$sp
	stw	r12,`$FRAME-$SIZE_T*18-4`($sp)	# save vrsave
	$PUSH	r14,`$FRAME-$SIZE_T*18`($sp)
	$PUSH	r15,`$FRAME-$SIZE_T*17`($sp)
	$PUSH	r16,`$FRAME-$SIZE_T*16`($sp)
	$PUSH	r17,`$FRAME-$SIZE_T*15`($sp)
	$PUSH	r18,`$FRAME-$SIZE_T*14`($sp)
	$PUSH	r19,`$FRAME-$SIZE_T*13`($sp)
	$PUSH	r20,`$FRAME-$SIZE_T*12`($sp)
	$PUSH	r21,`$FRAME-$SIZE_T*11`($sp)
	$PUSH	r22,`$FRAME-$SIZE_T*10`($sp)
	$PUSH	r23,`$FRAME-$SIZE_T*9`($sp)
	$PUSH	r24,`$FRAME-$SIZE_T*8`($sp)
	$PUSH	r25,`$FRAME-$SIZE_T*7`($sp)
	$PUSH	r26,`$FRAME-$SIZE_T*6`($sp)
	$PUSH	r27,`$FRAME-$SIZE_T*5`($sp)
	$PUSH	r28,`$FRAME-$SIZE_T*4`($sp)
	$PUSH	r29,`$FRAME-$SIZE_T*3`($sp)
	$PUSH	r30,`$FRAME-$SIZE_T*2`($sp)
	$PUSH	r31,`$FRAME-$SIZE_T*1`($sp)
	li	r12,-4096+511
	$PUSH	r0, `$FRAME+$LRSAVE`($sp)
	mtspr	256,r12				# preserve 29 AltiVec registers

	bl	Lconsts				# returns pointer Lsigma in r12
	li	@x[0],16
	li	@x[1],32
	li	@x[2],48
	li	@x[3],64
	li	@x[4],31			# 31 is not a typo
	li	@x[5],15			# nor is 15

	lvx	@K[1],0,$key			# load key
	?lvsr	$T0,0,$key			# prepare unaligned load
	lvx	@K[2],@x[0],$key
	lvx	@D[0],@x[4],$key

	lvx	@K[3],0,$ctr			# load counter
	?lvsr	$T1,0,$ctr			# prepare unaligned load
	lvx	@D[1],@x[5],$ctr

	lvx	@K[0],0,r12			# load constants
	lvx	@K[5],@x[0],r12			# one
	lvx	$FOUR,@x[1],r12
	lvx	$sixteen,@x[2],r12
	lvx	$twenty4,@x[3],r12

	?vperm	@K[1],@K[2],@K[1],$T0		# align key
	?vperm	@K[2],@D[0],@K[2],$T0
	?vperm	@K[3],@D[1],@K[3],$T1		# align counter

	lwz	@d[0],0($ctr)			# load counter to GPR
	lwz	@d[1],4($ctr)
	vadduwm	@K[3],@K[3],@K[5]		# adjust AltiVec counter
	lwz	@d[2],8($ctr)
	vadduwm	@K[4],@K[3],@K[5]
	lwz	@d[3],12($ctr)
	vadduwm	@K[5],@K[4],@K[5]

	vxor	$T0,$T0,$T0			# 0x00..00
	vspltisw $outmask,-1			# 0xff..ff
	?lvsr	$inpperm,0,$inp			# prepare for unaligned load
	?lvsl	$outperm,0,$out			# prepare for unaligned store
	?vperm	$outmask,$outmask,$T0,$outperm

	be?lvsl	$T0,0,@x[0]			# 0x00..0f
	be?vspltisb $T1,3			# 0x03..03
	be?vxor	$T0,$T0,$T1			# swap bytes within words
	be?vxor	$outperm,$outperm,$T1
	be?vperm $inpperm,$inpperm,$inpperm,$T0

	li	r0,10				# inner loop counter
	b	Loop_outer_vmx

.align	4
Loop_outer_vmx:
	lis	@x[0],0x6170			# synthesize sigma
	lis	@x[1],0x3320
	 vmr	$A0,@K[0]
	lis	@x[2],0x7962
	lis	@x[3],0x6b20
	 vmr	$A1,@K[0]
	ori	@x[0],@x[0],0x7865
	ori	@x[1],@x[1],0x646e
	 vmr	$A2,@K[0]
	ori	@x[2],@x[2],0x2d32
	ori	@x[3],@x[3],0x6574
	 vmr	$B0,@K[1]

	lwz	@x[4],0($key)			# load key to GPR
	 vmr	$B1,@K[1]
	lwz	@x[5],4($key)
	 vmr	$B2,@K[1]
	lwz	@x[6],8($key)
	 vmr	$C0,@K[2]
	lwz	@x[7],12($key)
	 vmr	$C1,@K[2]
	lwz	@x[8],16($key)
	 vmr	$C2,@K[2]
	mr	@x[12],@d[0]			# copy GPR counter
	lwz	@x[9],20($key)
	 vmr	$D0,@K[3]
	mr	@x[13],@d[1]
	lwz	@x[10],24($key)
	 vmr	$D1,@K[4]
	mr	@x[14],@d[2]
	lwz	@x[11],28($key)
	 vmr	$D2,@K[5]
	mr	@x[15],@d[3]

	mr	@t[0],@x[4]
	mr	@t[1],@x[5]
	mr	@t[2],@x[6]
	mr	@t[3],@x[7]

	vspltisw $twelve,12			# synthesize constants
	vspltisw $seven,7

	mtctr	r0
	nop
Loop_vmx:
___
	my @thread0=&VMXROUND($A0,$B0,$C0,$D0,0);
	my @thread1=&VMXROUND($A1,$B1,$C1,$D1,0);
	my @thread2=&VMXROUND($A2,$B2,$C2,$D2,0);
	my @thread3=&ROUND(0,4,8,12);

	foreach (@thread0) {
		eval;
		eval(shift(@thread1));
		eval(shift(@thread2));

		eval(shift(@thread3));
		eval(shift(@thread3));
		eval(shift(@thread3));
	}
	foreach (@thread3) { eval; }

	@thread0=&VMXROUND($A0,$B0,$C0,$D0,1);
	@thread1=&VMXROUND($A1,$B1,$C1,$D1,1);
	@thread2=&VMXROUND($A2,$B2,$C2,$D2,1);
	@thread3=&ROUND(0,5,10,15);

	foreach (@thread0) {
		eval;
		eval(shift(@thread1));
		eval(shift(@thread2));

		eval(shift(@thread3));
		eval(shift(@thread3));
		eval(shift(@thread3));
	}
	foreach (@thread3) { eval; }
$code.=<<___;
	bdnz	Loop_vmx

	subi	$len,$len,256			# $len-=256
	addi	@x[0],@x[0],0x7865		# accumulate key block
	addi	@x[1],@x[1],0x646e
	addi	@x[2],@x[2],0x2d32
	addi	@x[3],@x[3],0x6574
	addis	@x[0],@x[0],0x6170
	addis	@x[1],@x[1],0x3320
	addis	@x[2],@x[2],0x7962
	addis	@x[3],@x[3],0x6b20
	add	@x[4],@x[4],@t[0]
	lwz	@t[0],16($key)
	add	@x[5],@x[5],@t[1]
	lwz	@t[1],20($key)
	add	@x[6],@x[6],@t[2]
	lwz	@t[2],24($key)
	add	@x[7],@x[7],@t[3]
	lwz	@t[3],28($key)
	add	@x[8],@x[8],@t[0]
	add	@x[9],@x[9],@t[1]
	add	@x[10],@x[10],@t[2]
	add	@x[11],@x[11],@t[3]
	add	@x[12],@x[12],@d[0]
	add	@x[13],@x[13],@d[1]
	add	@x[14],@x[14],@d[2]
	add	@x[15],@x[15],@d[3]

	vadduwm	$A0,$A0,@K[0]			# accumulate key block
	vadduwm	$A1,$A1,@K[0]
	vadduwm	$A2,$A2,@K[0]
	vadduwm	$B0,$B0,@K[1]
	vadduwm	$B1,$B1,@K[1]
	vadduwm	$B2,$B2,@K[1]
	vadduwm	$C0,$C0,@K[2]
	vadduwm	$C1,$C1,@K[2]
	vadduwm	$C2,$C2,@K[2]
	vadduwm	$D0,$D0,@K[3]
	vadduwm	$D1,$D1,@K[4]
	vadduwm	$D2,$D2,@K[5]

	addi	@d[0],@d[0],4			# increment counter
	vadduwm	@K[3],@K[3],$FOUR
	vadduwm	@K[4],@K[4],$FOUR
	vadduwm	@K[5],@K[5],$FOUR

___
if (!$LITTLE_ENDIAN) { for($i=0;$i<16;$i++) {	# flip byte order
$code.=<<___;
	mr	@t[$i&3],@x[$i]
	rotlwi	@x[$i],@x[$i],8
	rlwimi	@x[$i],@t[$i&3],24,0,7
	rlwimi	@x[$i],@t[$i&3],24,16,23
___
} }
$code.=<<___;
	lwz	@t[0],0($inp)			# load input, aligned or not
	lwz	@t[1],4($inp)
	lwz	@t[2],8($inp)
	lwz	@t[3],12($inp)
	xor	@x[0],@x[0],@t[0]		# xor with input
	lwz	@t[0],16($inp)
	xor	@x[1],@x[1],@t[1]
	lwz	@t[1],20($inp)
	xor	@x[2],@x[2],@t[2]
	lwz	@t[2],24($inp)
	xor	@x[3],@x[3],@t[3]
	lwz	@t[3],28($inp)
	xor	@x[4],@x[4],@t[0]
	lwz	@t[0],32($inp)
	xor	@x[5],@x[5],@t[1]
	lwz	@t[1],36($inp)
	xor	@x[6],@x[6],@t[2]
	lwz	@t[2],40($inp)
	xor	@x[7],@x[7],@t[3]
	lwz	@t[3],44($inp)
	xor	@x[8],@x[8],@t[0]
	lwz	@t[0],48($inp)
	xor	@x[9],@x[9],@t[1]
	lwz	@t[1],52($inp)
	xor	@x[10],@x[10],@t[2]
	lwz	@t[2],56($inp)
	xor	@x[11],@x[11],@t[3]
	lwz	@t[3],60($inp)
	xor	@x[12],@x[12],@t[0]
	stw	@x[0],0($out)			# store output, aligned or not
	xor	@x[13],@x[13],@t[1]
	stw	@x[1],4($out)
	xor	@x[14],@x[14],@t[2]
	stw	@x[2],8($out)
	xor	@x[15],@x[15],@t[3]
	stw	@x[3],12($out)
	addi	$inp,$inp,64
	stw	@x[4],16($out)
	li	@t[0],16
	stw	@x[5],20($out)
	li	@t[1],32
	stw	@x[6],24($out)
	li	@t[2],48
	stw	@x[7],28($out)
	li	@t[3],64
	stw	@x[8],32($out)
	stw	@x[9],36($out)
	stw	@x[10],40($out)
	stw	@x[11],44($out)
	stw	@x[12],48($out)
	stw	@x[13],52($out)
	stw	@x[14],56($out)
	stw	@x[15],60($out)
	addi	$out,$out,64

	lvx	@D[0],0,$inp			# load input
	lvx	@D[1],@t[0],$inp
	lvx	@D[2],@t[1],$inp
	lvx	@D[3],@t[2],$inp
	lvx	@D[4],@t[3],$inp
	addi	$inp,$inp,64

	?vperm	@D[0],@D[1],@D[0],$inpperm	# align input
	?vperm	@D[1],@D[2],@D[1],$inpperm
	?vperm	@D[2],@D[3],@D[2],$inpperm
	?vperm	@D[3],@D[4],@D[3],$inpperm
	vxor	$A0,$A0,@D[0]			# xor with input
	vxor	$B0,$B0,@D[1]
	lvx	@D[1],@t[0],$inp		# keep loading input
	vxor	$C0,$C0,@D[2]
	lvx	@D[2],@t[1],$inp
	vxor	$D0,$D0,@D[3]
	lvx	@D[3],@t[2],$inp
	lvx	@D[0],@t[3],$inp
	addi	$inp,$inp,64
	li	@t[3],63			# 63 is not a typo
	vperm	$A0,$A0,$A0,$outperm		# pre-misalign output
	vperm	$B0,$B0,$B0,$outperm
	vperm	$C0,$C0,$C0,$outperm
	vperm	$D0,$D0,$D0,$outperm

	?vperm	@D[4],@D[1],@D[4],$inpperm	# align input
	?vperm	@D[1],@D[2],@D[1],$inpperm
	?vperm	@D[2],@D[3],@D[2],$inpperm
	?vperm	@D[3],@D[0],@D[3],$inpperm
	vxor	$A1,$A1,@D[4]
	vxor	$B1,$B1,@D[1]
	lvx	@D[1],@t[0],$inp		# keep loading input
	vxor	$C1,$C1,@D[2]
	lvx	@D[2],@t[1],$inp
	vxor	$D1,$D1,@D[3]
	lvx	@D[3],@t[2],$inp
	lvx	@D[4],@t[3],$inp		# redundant in aligned case
	addi	$inp,$inp,64
	vperm	$A1,$A1,$A1,$outperm		# pre-misalign output
	vperm	$B1,$B1,$B1,$outperm
	vperm	$C1,$C1,$C1,$outperm
	vperm	$D1,$D1,$D1,$outperm

	?vperm	@D[0],@D[1],@D[0],$inpperm	# align input
	?vperm	@D[1],@D[2],@D[1],$inpperm
	?vperm	@D[2],@D[3],@D[2],$inpperm
	?vperm	@D[3],@D[4],@D[3],$inpperm
	vxor	$A2,$A2,@D[0]
	vxor	$B2,$B2,@D[1]
	vxor	$C2,$C2,@D[2]
	vxor	$D2,$D2,@D[3]
	vperm	$A2,$A2,$A2,$outperm		# pre-misalign output
	vperm	$B2,$B2,$B2,$outperm
	vperm	$C2,$C2,$C2,$outperm
	vperm	$D2,$D2,$D2,$outperm

	andi.	@x[1],$out,15			# is $out aligned?
	mr	@x[0],$out

	vsel	@D[0],$A0,$B0,$outmask		# collect pre-misaligned output
	vsel	@D[1],$B0,$C0,$outmask
	vsel	@D[2],$C0,$D0,$outmask
	vsel	@D[3],$D0,$A1,$outmask
	vsel	$B0,$A1,$B1,$outmask
	vsel	$C0,$B1,$C1,$outmask
	vsel	$D0,$C1,$D1,$outmask
	vsel	$A1,$D1,$A2,$outmask
	vsel	$B1,$A2,$B2,$outmask
	vsel	$C1,$B2,$C2,$outmask
	vsel	$D1,$C2,$D2,$outmask

	#stvx	$A0,0,$out			# take it easy on the edges
	stvx	@D[0],@t[0],$out		# store output
	stvx	@D[1],@t[1],$out
	stvx	@D[2],@t[2],$out
	addi	$out,$out,64
	stvx	@D[3],0,$out
	stvx	$B0,@t[0],$out
	stvx	$C0,@t[1],$out
	stvx	$D0,@t[2],$out
	addi	$out,$out,64
	stvx	$A1,0,$out
	stvx	$B1,@t[0],$out
	stvx	$C1,@t[1],$out
	stvx	$D1,@t[2],$out
	addi	$out,$out,64

	beq	Laligned_vmx

	sub	@x[2],$out,@x[1]		# in misaligned case edges
	li	@x[3],0				# are written byte-by-byte
Lunaligned_tail_vmx:
	stvebx	$D2,@x[3],@x[2]
	addi	@x[3],@x[3],1
	cmpw	@x[3],@x[1]
	bne	Lunaligned_tail_vmx

	sub	@x[2],@x[0],@x[1]
Lunaligned_head_vmx:
	stvebx	$A0,@x[1],@x[2]
	cmpwi	@x[1],15
	addi	@x[1],@x[1],1
	bne	Lunaligned_head_vmx

	${UCMP}i $len,255			# done with 256-byte blocks yet?
	bgt	Loop_outer_vmx

	b	Ldone_vmx

.align	4
Laligned_vmx:
	stvx	$A0,0,@x[0]			# head hexaword was not stored

	${UCMP}i $len,255			# done with 256-byte blocks yet?
	bgt	Loop_outer_vmx
	nop

Ldone_vmx:
	${UCMP}i $len,0				# done yet?
	bnel	__ChaCha20_1x

	lwz	r12,`$FRAME-$SIZE_T*18-4`($sp)	# pull vrsave
	li	r10,`15+$LOCALS+64`
	li	r11,`31+$LOCALS+64`
	mtspr	256,r12				# restore vrsave
	lvx	v23,r10,$sp
	addi	r10,r10,32
	lvx	v24,r11,$sp
	addi	r11,r11,32
	lvx	v25,r10,$sp
	addi	r10,r10,32
	lvx	v26,r11,$sp
	addi	r11,r11,32
	lvx	v27,r10,$sp
	addi	r10,r10,32
	lvx	v28,r11,$sp
	addi	r11,r11,32
	lvx	v29,r10,$sp
	addi	r10,r10,32
	lvx	v30,r11,$sp
	lvx	v31,r10,$sp
	$POP	r0, `$FRAME+$LRSAVE`($sp)
	$POP	r14,`$FRAME-$SIZE_T*18`($sp)
	$POP	r15,`$FRAME-$SIZE_T*17`($sp)
	$POP	r16,`$FRAME-$SIZE_T*16`($sp)
	$POP	r17,`$FRAME-$SIZE_T*15`($sp)
	$POP	r18,`$FRAME-$SIZE_T*14`($sp)
	$POP	r19,`$FRAME-$SIZE_T*13`($sp)
	$POP	r20,`$FRAME-$SIZE_T*12`($sp)
	$POP	r21,`$FRAME-$SIZE_T*11`($sp)
	$POP	r22,`$FRAME-$SIZE_T*10`($sp)
	$POP	r23,`$FRAME-$SIZE_T*9`($sp)
	$POP	r24,`$FRAME-$SIZE_T*8`($sp)
	$POP	r25,`$FRAME-$SIZE_T*7`($sp)
	$POP	r26,`$FRAME-$SIZE_T*6`($sp)
	$POP	r27,`$FRAME-$SIZE_T*5`($sp)
	$POP	r28,`$FRAME-$SIZE_T*4`($sp)
	$POP	r29,`$FRAME-$SIZE_T*3`($sp)
	$POP	r30,`$FRAME-$SIZE_T*2`($sp)
	$POP	r31,`$FRAME-$SIZE_T*1`($sp)
	mtlr	r0
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,0x04,1,0x80,18,5,0
	.long	0
.size	.ChaCha20_ctr32_vmx,.-.ChaCha20_ctr32_vmx
___
}}}
{{{
my ($xa0,$xa1,$xa2,$xa3, $xb0,$xb1,$xb2,$xb3,
    $xc0,$xc1,$xc2,$xc3, $xd0,$xd1,$xd2,$xd3) = map("v$_",(0..15));
my @K = map("v$_",(16..19));
my $CTR = "v26";
my ($xt0,$xt1,$xt2,$xt3) = map("v$_",(27..30));
my ($sixteen,$twelve,$eight,$seven) = ($xt0,$xt1,$xt2,$xt3);
my $beperm = "v31";

my ($x00,$x10,$x20,$x30) = (0, map("r$_",(8..10)));

my $FRAME=$LOCALS+64+7*16;	# 7*16 is for v26-v31 offload

sub VSX_lane_ROUND {
my ($a0,$b0,$c0,$d0)=@_;
my ($a1,$b1,$c1,$d1)=map(($_&~3)+(($_+1)&3),($a0,$b0,$c0,$d0));
my ($a2,$b2,$c2,$d2)=map(($_&~3)+(($_+1)&3),($a1,$b1,$c1,$d1));
my ($a3,$b3,$c3,$d3)=map(($_&~3)+(($_+1)&3),($a2,$b2,$c2,$d2));
my @x=map("\"v$_\"",(0..15));

	(
	"&vadduwm	(@x[$a0],@x[$a0],@x[$b0])",	# Q1
	 "&vadduwm	(@x[$a1],@x[$a1],@x[$b1])",	# Q2
	  "&vadduwm	(@x[$a2],@x[$a2],@x[$b2])",	# Q3
	   "&vadduwm	(@x[$a3],@x[$a3],@x[$b3])",	# Q4
	"&vxor		(@x[$d0],@x[$d0],@x[$a0])",
	 "&vxor		(@x[$d1],@x[$d1],@x[$a1])",
	  "&vxor	(@x[$d2],@x[$d2],@x[$a2])",
	   "&vxor	(@x[$d3],@x[$d3],@x[$a3])",
	"&vrlw		(@x[$d0],@x[$d0],'$sixteen')",
	 "&vrlw		(@x[$d1],@x[$d1],'$sixteen')",
	  "&vrlw	(@x[$d2],@x[$d2],'$sixteen')",
	   "&vrlw	(@x[$d3],@x[$d3],'$sixteen')",

	"&vadduwm	(@x[$c0],@x[$c0],@x[$d0])",
	 "&vadduwm	(@x[$c1],@x[$c1],@x[$d1])",
	  "&vadduwm	(@x[$c2],@x[$c2],@x[$d2])",
	   "&vadduwm	(@x[$c3],@x[$c3],@x[$d3])",
	"&vxor		(@x[$b0],@x[$b0],@x[$c0])",
	 "&vxor		(@x[$b1],@x[$b1],@x[$c1])",
	  "&vxor	(@x[$b2],@x[$b2],@x[$c2])",
	   "&vxor	(@x[$b3],@x[$b3],@x[$c3])",
	"&vrlw		(@x[$b0],@x[$b0],'$twelve')",
	 "&vrlw		(@x[$b1],@x[$b1],'$twelve')",
	  "&vrlw	(@x[$b2],@x[$b2],'$twelve')",
	   "&vrlw	(@x[$b3],@x[$b3],'$twelve')",

	"&vadduwm	(@x[$a0],@x[$a0],@x[$b0])",
	 "&vadduwm	(@x[$a1],@x[$a1],@x[$b1])",
	  "&vadduwm	(@x[$a2],@x[$a2],@x[$b2])",
	   "&vadduwm	(@x[$a3],@x[$a3],@x[$b3])",
	"&vxor		(@x[$d0],@x[$d0],@x[$a0])",
	 "&vxor		(@x[$d1],@x[$d1],@x[$a1])",
	  "&vxor	(@x[$d2],@x[$d2],@x[$a2])",
	   "&vxor	(@x[$d3],@x[$d3],@x[$a3])",
	"&vrlw		(@x[$d0],@x[$d0],'$eight')",
	 "&vrlw		(@x[$d1],@x[$d1],'$eight')",
	  "&vrlw	(@x[$d2],@x[$d2],'$eight')",
	   "&vrlw	(@x[$d3],@x[$d3],'$eight')",

	"&vadduwm	(@x[$c0],@x[$c0],@x[$d0])",
	 "&vadduwm	(@x[$c1],@x[$c1],@x[$d1])",
	  "&vadduwm	(@x[$c2],@x[$c2],@x[$d2])",
	   "&vadduwm	(@x[$c3],@x[$c3],@x[$d3])",
	"&vxor		(@x[$b0],@x[$b0],@x[$c0])",
	 "&vxor		(@x[$b1],@x[$b1],@x[$c1])",
	  "&vxor	(@x[$b2],@x[$b2],@x[$c2])",
	   "&vxor	(@x[$b3],@x[$b3],@x[$c3])",
	"&vrlw		(@x[$b0],@x[$b0],'$seven')",
	 "&vrlw		(@x[$b1],@x[$b1],'$seven')",
	  "&vrlw	(@x[$b2],@x[$b2],'$seven')",
	   "&vrlw	(@x[$b3],@x[$b3],'$seven')"
	);
}

$code.=<<___;

.globl	.ChaCha20_ctr32_vsx
.align	5
.ChaCha20_ctr32_vsx:
	$STU	$sp,-$FRAME($sp)
	mflr	r0
	li	r10,`15+$LOCALS+64`
	li	r11,`31+$LOCALS+64`
	mfspr	r12,256
	stvx	v26,r10,$sp
	addi	r10,r10,32
	stvx	v27,r11,$sp
	addi	r11,r11,32
	stvx	v28,r10,$sp
	addi	r10,r10,32
	stvx	v29,r11,$sp
	addi	r11,r11,32
	stvx	v30,r10,$sp
	stvx	v31,r11,$sp
	stw	r12,`$FRAME-4`($sp)		# save vrsave
	li	r12,-4096+63
	$PUSH	r0, `$FRAME+$LRSAVE`($sp)
	mtspr	256,r12				# preserve 29 AltiVec registers

	bl	Lconsts				# returns pointer Lsigma in r12
	lvx_4w	@K[0],0,r12			# load sigma
	addi	r12,r12,0x50
	li	$x10,16
	li	$x20,32
	li	$x30,48
	li	r11,64

	lvx_4w	@K[1],0,$key			# load key
	lvx_4w	@K[2],$x10,$key
	lvx_4w	@K[3],0,$ctr			# load counter

	vxor	$xt0,$xt0,$xt0
	lvx_4w	$xt1,r11,r12
	vspltw	$CTR,@K[3],0
	vsldoi	@K[3],@K[3],$xt0,4
	vsldoi	@K[3],$xt0,@K[3],12		# clear @K[3].word[0]
	vadduwm	$CTR,$CTR,$xt1

	be?lvsl	$beperm,0,$x10			# 0x00..0f
	be?vspltisb $xt0,3			# 0x03..03
	be?vxor	$beperm,$beperm,$xt0		# swap bytes within words

	li	r0,10				# inner loop counter
	mtctr	r0
	b	Loop_outer_vsx

.align	5
Loop_outer_vsx:
	lvx	$xa0,$x00,r12			# load [smashed] sigma
	lvx	$xa1,$x10,r12
	lvx	$xa2,$x20,r12
	lvx	$xa3,$x30,r12

	vspltw	$xb0,@K[1],0			# smash the key
	vspltw	$xb1,@K[1],1
	vspltw	$xb2,@K[1],2
	vspltw	$xb3,@K[1],3

	vspltw	$xc0,@K[2],0
	vspltw	$xc1,@K[2],1
	vspltw	$xc2,@K[2],2
	vspltw	$xc3,@K[2],3

	vmr	$xd0,$CTR			# smash the counter
	vspltw	$xd1,@K[3],1
	vspltw	$xd2,@K[3],2
	vspltw	$xd3,@K[3],3

	vspltisw $sixteen,-16			# synthesize constants
	vspltisw $twelve,12
	vspltisw $eight,8
	vspltisw $seven,7

Loop_vsx:
___
	foreach (&VSX_lane_ROUND(0, 4, 8,12)) { eval; }
	foreach (&VSX_lane_ROUND(0, 5,10,15)) { eval; }
$code.=<<___;
	bdnz	Loop_vsx

	vadduwm	$xd0,$xd0,$CTR

	vmrgew	$xt0,$xa0,$xa1			# transpose data
	vmrgew	$xt1,$xa2,$xa3
	vmrgow	$xa0,$xa0,$xa1
	vmrgow	$xa2,$xa2,$xa3
	 vmrgew	$xt2,$xb0,$xb1
	 vmrgew	$xt3,$xb2,$xb3
	vpermdi	$xa1,$xa0,$xa2,0b00
	vpermdi	$xa3,$xa0,$xa2,0b11
	vpermdi	$xa0,$xt0,$xt1,0b00
	vpermdi	$xa2,$xt0,$xt1,0b11

	vmrgow	$xb0,$xb0,$xb1
	vmrgow	$xb2,$xb2,$xb3
	 vmrgew	$xt0,$xc0,$xc1
	 vmrgew	$xt1,$xc2,$xc3
	vpermdi	$xb1,$xb0,$xb2,0b00
	vpermdi	$xb3,$xb0,$xb2,0b11
	vpermdi	$xb0,$xt2,$xt3,0b00
	vpermdi	$xb2,$xt2,$xt3,0b11

	vmrgow	$xc0,$xc0,$xc1
	vmrgow	$xc2,$xc2,$xc3
	 vmrgew	$xt2,$xd0,$xd1
	 vmrgew	$xt3,$xd2,$xd3
	vpermdi	$xc1,$xc0,$xc2,0b00
	vpermdi	$xc3,$xc0,$xc2,0b11
	vpermdi	$xc0,$xt0,$xt1,0b00
	vpermdi	$xc2,$xt0,$xt1,0b11

	vmrgow	$xd0,$xd0,$xd1
	vmrgow	$xd2,$xd2,$xd3
	 vspltisw $xt0,4
	 vadduwm  $CTR,$CTR,$xt0		# next counter value
	vpermdi	$xd1,$xd0,$xd2,0b00
	vpermdi	$xd3,$xd0,$xd2,0b11
	vpermdi	$xd0,$xt2,$xt3,0b00
	vpermdi	$xd2,$xt2,$xt3,0b11

	vadduwm	$xa0,$xa0,@K[0]
	vadduwm	$xb0,$xb0,@K[1]
	vadduwm	$xc0,$xc0,@K[2]
	vadduwm	$xd0,$xd0,@K[3]

	be?vperm $xa0,$xa0,$xa0,$beperm
	be?vperm $xb0,$xb0,$xb0,$beperm
	be?vperm $xc0,$xc0,$xc0,$beperm
	be?vperm $xd0,$xd0,$xd0,$beperm

	${UCMP}i $len,0x40
	blt	Ltail_vsx

	lvx_4w	$xt0,$x00,$inp
	lvx_4w	$xt1,$x10,$inp
	lvx_4w	$xt2,$x20,$inp
	lvx_4w	$xt3,$x30,$inp

	vxor	$xt0,$xt0,$xa0
	vxor	$xt1,$xt1,$xb0
	vxor	$xt2,$xt2,$xc0
	vxor	$xt3,$xt3,$xd0

	stvx_4w	$xt0,$x00,$out
	stvx_4w	$xt1,$x10,$out
	addi	$inp,$inp,0x40
	stvx_4w	$xt2,$x20,$out
	subi	$len,$len,0x40
	stvx_4w	$xt3,$x30,$out
	addi	$out,$out,0x40
	beq	Ldone_vsx

	vadduwm	$xa0,$xa1,@K[0]
	vadduwm	$xb0,$xb1,@K[1]
	vadduwm	$xc0,$xc1,@K[2]
	vadduwm	$xd0,$xd1,@K[3]

	be?vperm $xa0,$xa0,$xa0,$beperm
	be?vperm $xb0,$xb0,$xb0,$beperm
	be?vperm $xc0,$xc0,$xc0,$beperm
	be?vperm $xd0,$xd0,$xd0,$beperm

	${UCMP}i $len,0x40
	blt	Ltail_vsx

	lvx_4w	$xt0,$x00,$inp
	lvx_4w	$xt1,$x10,$inp
	lvx_4w	$xt2,$x20,$inp
	lvx_4w	$xt3,$x30,$inp

	vxor	$xt0,$xt0,$xa0
	vxor	$xt1,$xt1,$xb0
	vxor	$xt2,$xt2,$xc0
	vxor	$xt3,$xt3,$xd0

	stvx_4w	$xt0,$x00,$out
	stvx_4w	$xt1,$x10,$out
	addi	$inp,$inp,0x40
	stvx_4w	$xt2,$x20,$out
	subi	$len,$len,0x40
	stvx_4w	$xt3,$x30,$out
	addi	$out,$out,0x40
	beq	Ldone_vsx

	vadduwm	$xa0,$xa2,@K[0]
	vadduwm	$xb0,$xb2,@K[1]
	vadduwm	$xc0,$xc2,@K[2]
	vadduwm	$xd0,$xd2,@K[3]

	be?vperm $xa0,$xa0,$xa0,$beperm
	be?vperm $xb0,$xb0,$xb0,$beperm
	be?vperm $xc0,$xc0,$xc0,$beperm
	be?vperm $xd0,$xd0,$xd0,$beperm

	${UCMP}i $len,0x40
	blt	Ltail_vsx

	lvx_4w	$xt0,$x00,$inp
	lvx_4w	$xt1,$x10,$inp
	lvx_4w	$xt2,$x20,$inp
	lvx_4w	$xt3,$x30,$inp

	vxor	$xt0,$xt0,$xa0
	vxor	$xt1,$xt1,$xb0
	vxor	$xt2,$xt2,$xc0
	vxor	$xt3,$xt3,$xd0

	stvx_4w	$xt0,$x00,$out
	stvx_4w	$xt1,$x10,$out
	addi	$inp,$inp,0x40
	stvx_4w	$xt2,$x20,$out
	subi	$len,$len,0x40
	stvx_4w	$xt3,$x30,$out
	addi	$out,$out,0x40
	beq	Ldone_vsx

	vadduwm	$xa0,$xa3,@K[0]
	vadduwm	$xb0,$xb3,@K[1]
	vadduwm	$xc0,$xc3,@K[2]
	vadduwm	$xd0,$xd3,@K[3]

	be?vperm $xa0,$xa0,$xa0,$beperm
	be?vperm $xb0,$xb0,$xb0,$beperm
	be?vperm $xc0,$xc0,$xc0,$beperm
	be?vperm $xd0,$xd0,$xd0,$beperm

	${UCMP}i $len,0x40
	blt	Ltail_vsx

	lvx_4w	$xt0,$x00,$inp
	lvx_4w	$xt1,$x10,$inp
	lvx_4w	$xt2,$x20,$inp
	lvx_4w	$xt3,$x30,$inp

	vxor	$xt0,$xt0,$xa0
	vxor	$xt1,$xt1,$xb0
	vxor	$xt2,$xt2,$xc0
	vxor	$xt3,$xt3,$xd0

	stvx_4w	$xt0,$x00,$out
	stvx_4w	$xt1,$x10,$out
	addi	$inp,$inp,0x40
	stvx_4w	$xt2,$x20,$out
	subi	$len,$len,0x40
	stvx_4w	$xt3,$x30,$out
	addi	$out,$out,0x40
	mtctr	r0
	bne	Loop_outer_vsx

Ldone_vsx:
	lwz	r12,`$FRAME-4`($sp)		# pull vrsave
	li	r10,`15+$LOCALS+64`
	li	r11,`31+$LOCALS+64`
	$POP	r0, `$FRAME+$LRSAVE`($sp)
	mtspr	256,r12				# restore vrsave
	lvx	v26,r10,$sp
	addi	r10,r10,32
	lvx	v27,r11,$sp
	addi	r11,r11,32
	lvx	v28,r10,$sp
	addi	r10,r10,32
	lvx	v29,r11,$sp
	addi	r11,r11,32
	lvx	v30,r10,$sp
	lvx	v31,r11,$sp
	mtlr	r0
	addi	$sp,$sp,$FRAME
	blr

.align	4
Ltail_vsx:
	addi	r11,$sp,$LOCALS
	mtctr	$len
	stvx_4w	$xa0,$x00,r11			# offload block to stack
	stvx_4w	$xb0,$x10,r11
	stvx_4w	$xc0,$x20,r11
	stvx_4w	$xd0,$x30,r11
	subi	r12,r11,1			# prepare for *++ptr
	subi	$inp,$inp,1
	subi	$out,$out,1

Loop_tail_vsx:
	lbzu	r6,1(r12)
	lbzu	r7,1($inp)
	xor	r6,r6,r7
	stbu	r6,1($out)
	bdnz	Loop_tail_vsx

	stvx_4w	$K[0],$x00,r11			# wipe copy of the block
	stvx_4w	$K[0],$x10,r11
	stvx_4w	$K[0],$x20,r11
	stvx_4w	$K[0],$x30,r11

	b	Ldone_vsx
	.long	0
	.byte	0,12,0x04,1,0x80,0,5,0
	.long	0
.size	.ChaCha20_ctr32_vsx,.-.ChaCha20_ctr32_vsx
___
}}}
$code.=<<___;
.align	5
Lconsts:
	mflr	r0
	bcl	20,31,\$+4
	mflr	r12	#vvvvv "distance between . and Lsigma
	addi	r12,r12,`64-8`
	mtlr	r0
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,0,0
	.space	`64-9*4`
Lsigma:
	.long   0x61707865,0x3320646e,0x79622d32,0x6b206574
	.long	1,0,0,0
	.long	4,0,0,0
___
$code.=<<___ 	if ($LITTLE_ENDIAN);
	.long	0x0e0f0c0d,0x0a0b0809,0x06070405,0x02030001
	.long	0x0d0e0f0c,0x090a0b08,0x05060704,0x01020300
___
$code.=<<___ 	if (!$LITTLE_ENDIAN);	# flipped words
	.long	0x02030001,0x06070405,0x0a0b0809,0x0e0f0c0d
	.long	0x01020300,0x05060704,0x090a0b08,0x0d0e0f0c
___
$code.=<<___;
	.long	0x61707865,0x61707865,0x61707865,0x61707865
	.long	0x3320646e,0x3320646e,0x3320646e,0x3320646e
	.long	0x79622d32,0x79622d32,0x79622d32,0x79622d32
	.long	0x6b206574,0x6b206574,0x6b206574,0x6b206574
	.long	0,1,2,3
.asciz  "ChaCha20 for PowerPC/AltiVec, CRYPTOGAMS by <appro\@openssl.org>"
.align	2
___

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	# instructions prefixed with '?' are endian-specific and need
	# to be adjusted accordingly...
	if ($flavour !~ /le$/) {	# big-endian
	    s/be\?//		or
	    s/le\?/#le#/	or
	    s/\?lvsr/lvsl/	or
	    s/\?lvsl/lvsr/	or
	    s/\?(vperm\s+v[0-9]+,\s*)(v[0-9]+,\s*)(v[0-9]+,\s*)(v[0-9]+)/$1$3$2$4/ or
	    s/vrldoi(\s+v[0-9]+,\s*)(v[0-9]+,)\s*([0-9]+)/vsldoi$1$2$2 16-$3/;
	} else {			# little-endian
	    s/le\?//		or
	    s/be\?/#be#/	or
	    s/\?([a-z]+)/$1/	or
	    s/vrldoi(\s+v[0-9]+,\s*)(v[0-9]+,)\s*([0-9]+)/vsldoi$1$2$2 $3/;
	}

	print $_,"\n";
}

close STDOUT;

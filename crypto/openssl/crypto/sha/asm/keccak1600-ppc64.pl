#!/usr/bin/env perl
# Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
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
# Keccak-1600 for PPC64.
#
# June 2017.
#
# This is straightforward KECCAK_1X_ALT implementation that works on
# *any* PPC64. Then PowerISA 2.07 adds 2x64-bit vector rotate, and
# it's possible to achieve performance better than below, but that is
# naturally option only for POWER8 and successors...
#
######################################################################
# Numbers are cycles per processed byte.
#
#		r=1088(*)
#
# PPC970/G5	14.6/+120%
# POWER7	10.3/+100%
# POWER8	11.5/+85%
# POWER9	9.4/+45%
#
# (*)	Corresponds to SHA3-256. Percentage after slash is improvement
#	over gcc-4.x-generated KECCAK_1X_ALT code. Newer compilers do
#	much better (but watch out for them generating code specific
#	to processor they execute on).

$flavour = shift;

if ($flavour =~ /64/) {
	$SIZE_T	=8;
	$LRSAVE	=2*$SIZE_T;
	$UCMP	="cmpld";
	$STU	="stdu";
	$POP	="ld";
	$PUSH	="std";
} else { die "nonsense $flavour"; }

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour ".shift || die "can't call $xlate: $!";

$FRAME=24*$SIZE_T+6*$SIZE_T+32;
$LOCALS=6*$SIZE_T;
$TEMP=$LOCALS+6*$SIZE_T;

my $sp ="r1";

my @A = map([ "r$_", "r".($_+1), "r".($_+2), "r".($_+3), "r".($_+4) ],
            (7, 12, 17, 22, 27));
   $A[1][1] = "r6"; # r13 is reserved

my @C = map("r$_", (0,3,4,5));

my @rhotates = ([  0,  1, 62, 28, 27 ],
                [ 36, 44,  6, 55, 20 ],
                [  3, 10, 43, 25, 39 ],
                [ 41, 45, 15, 21,  8 ],
                [ 18,  2, 61, 56, 14 ]);

$code.=<<___;
.text

.type	KeccakF1600_int,\@function
.align	5
KeccakF1600_int:
	li	r0,24
	mtctr	r0
	b	.Loop
.align	4
.Loop:
	xor	$C[0],$A[0][0],$A[1][0]		; Theta
	std	$A[0][4],`$TEMP+0`($sp)
	xor	$C[1],$A[0][1],$A[1][1]
	std	$A[1][4],`$TEMP+8`($sp)
	xor	$C[2],$A[0][2],$A[1][2]
	std	$A[2][4],`$TEMP+16`($sp)
	xor	$C[3],$A[0][3],$A[1][3]
	std	$A[3][4],`$TEMP+24`($sp)
___
	$C[4]=$A[0][4];
	$C[5]=$A[1][4];
	$C[6]=$A[2][4];
	$C[7]=$A[3][4];
$code.=<<___;
	xor	$C[4],$A[0][4],$A[1][4]
	xor	$C[0],$C[0],$A[2][0]
	xor	$C[1],$C[1],$A[2][1]
	xor	$C[2],$C[2],$A[2][2]
	xor	$C[3],$C[3],$A[2][3]
	xor	$C[4],$C[4],$A[2][4]
	xor	$C[0],$C[0],$A[3][0]
	xor	$C[1],$C[1],$A[3][1]
	xor	$C[2],$C[2],$A[3][2]
	xor	$C[3],$C[3],$A[3][3]
	xor	$C[4],$C[4],$A[3][4]
	xor	$C[0],$C[0],$A[4][0]
	xor	$C[2],$C[2],$A[4][2]
	xor	$C[1],$C[1],$A[4][1]
	xor	$C[3],$C[3],$A[4][3]
	rotldi	$C[5],$C[2],1
	xor	$C[4],$C[4],$A[4][4]
	rotldi	$C[6],$C[3],1
	xor	$C[5],$C[5],$C[0]
	rotldi	$C[7],$C[4],1

	xor	$A[0][1],$A[0][1],$C[5]
	xor	$A[1][1],$A[1][1],$C[5]
	xor	$A[2][1],$A[2][1],$C[5]
	xor	$A[3][1],$A[3][1],$C[5]
	xor	$A[4][1],$A[4][1],$C[5]

	rotldi	$C[5],$C[0],1
	xor	$C[6],$C[6],$C[1]
	xor	$C[2],$C[2],$C[7]
	rotldi	$C[7],$C[1],1
	xor	$C[3],$C[3],$C[5]
	xor	$C[4],$C[4],$C[7]

	xor	$C[1],   $A[0][2],$C[6]			;mr	$C[1],$A[0][2]
	xor	$A[1][2],$A[1][2],$C[6]
	xor	$A[2][2],$A[2][2],$C[6]
	xor	$A[3][2],$A[3][2],$C[6]
	xor	$A[4][2],$A[4][2],$C[6]

	xor	$A[0][0],$A[0][0],$C[4]
	xor	$A[1][0],$A[1][0],$C[4]
	xor	$A[2][0],$A[2][0],$C[4]
	xor	$A[3][0],$A[3][0],$C[4]
	xor	$A[4][0],$A[4][0],$C[4]
___
	$C[4]=undef;
	$C[5]=undef;
	$C[6]=undef;
	$C[7]=undef;
$code.=<<___;
	ld	$A[0][4],`$TEMP+0`($sp)
	xor	$C[0],   $A[0][3],$C[2]			;mr	$C[0],$A[0][3]
	ld	$A[1][4],`$TEMP+8`($sp)
	xor	$A[1][3],$A[1][3],$C[2]
	ld	$A[2][4],`$TEMP+16`($sp)
	xor	$A[2][3],$A[2][3],$C[2]
	ld	$A[3][4],`$TEMP+24`($sp)
	xor	$A[3][3],$A[3][3],$C[2]
	xor	$A[4][3],$A[4][3],$C[2]

	xor	$C[2],   $A[0][4],$C[3]			;mr	$C[2],$A[0][4]
	xor	$A[1][4],$A[1][4],$C[3]
	xor	$A[2][4],$A[2][4],$C[3]
	xor	$A[3][4],$A[3][4],$C[3]
	xor	$A[4][4],$A[4][4],$C[3]

	mr	$C[3],$A[0][1]				; Rho+Pi
	rotldi	$A[0][1],$A[1][1],$rhotates[1][1]
	;mr	$C[1],$A[0][2]
	rotldi	$A[0][2],$A[2][2],$rhotates[2][2]
	;mr	$C[0],$A[0][3]
	rotldi	$A[0][3],$A[3][3],$rhotates[3][3]
	;mr	$C[2],$A[0][4]
	rotldi	$A[0][4],$A[4][4],$rhotates[4][4]

	rotldi	$A[1][1],$A[1][4],$rhotates[1][4]
	rotldi	$A[2][2],$A[2][3],$rhotates[2][3]
	rotldi	$A[3][3],$A[3][2],$rhotates[3][2]
	rotldi	$A[4][4],$A[4][1],$rhotates[4][1]

	rotldi	$A[1][4],$A[4][2],$rhotates[4][2]
	rotldi	$A[2][3],$A[3][4],$rhotates[3][4]
	rotldi	$A[3][2],$A[2][1],$rhotates[2][1]
	rotldi	$A[4][1],$A[1][3],$rhotates[1][3]

	rotldi	$A[4][2],$A[2][4],$rhotates[2][4]
	rotldi	$A[3][4],$A[4][3],$rhotates[4][3]
	rotldi	$A[2][1],$A[1][2],$rhotates[1][2]
	rotldi	$A[1][3],$A[3][1],$rhotates[3][1]

	rotldi	$A[2][4],$A[4][0],$rhotates[4][0]
	rotldi	$A[4][3],$A[3][0],$rhotates[3][0]
	rotldi	$A[1][2],$A[2][0],$rhotates[2][0]
	rotldi	$A[3][1],$A[1][0],$rhotates[1][0]

	rotldi	$A[1][0],$C[0],$rhotates[0][3]
	rotldi	$A[2][0],$C[3],$rhotates[0][1]
	rotldi	$A[3][0],$C[2],$rhotates[0][4]
	rotldi	$A[4][0],$C[1],$rhotates[0][2]

	andc	$C[0],$A[0][2],$A[0][1]			; Chi+Iota
	andc	$C[1],$A[0][3],$A[0][2]
	andc	$C[2],$A[0][0],$A[0][4]
	andc	$C[3],$A[0][1],$A[0][0]
	xor	$A[0][0],$A[0][0],$C[0]
	andc	$C[0],$A[0][4],$A[0][3]
	xor	$A[0][1],$A[0][1],$C[1]
	 ld	$C[1],`$LOCALS+4*$SIZE_T`($sp)
	xor	$A[0][3],$A[0][3],$C[2]
	xor	$A[0][4],$A[0][4],$C[3]
	xor	$A[0][2],$A[0][2],$C[0]
	 ldu	$C[3],8($C[1])				; Iota[i++]

	andc	$C[0],$A[1][2],$A[1][1]
	 std	$C[1],`$LOCALS+4*$SIZE_T`($sp)
	andc	$C[1],$A[1][3],$A[1][2]
	andc	$C[2],$A[1][0],$A[1][4]
	 xor	$A[0][0],$A[0][0],$C[3]			; A[0][0] ^= Iota
	andc	$C[3],$A[1][1],$A[1][0]
	xor	$A[1][0],$A[1][0],$C[0]
	andc	$C[0],$A[1][4],$A[1][3]
	xor	$A[1][1],$A[1][1],$C[1]
	xor	$A[1][3],$A[1][3],$C[2]
	xor	$A[1][4],$A[1][4],$C[3]
	xor	$A[1][2],$A[1][2],$C[0]

	andc	$C[0],$A[2][2],$A[2][1]
	andc	$C[1],$A[2][3],$A[2][2]
	andc	$C[2],$A[2][0],$A[2][4]
	andc	$C[3],$A[2][1],$A[2][0]
	xor	$A[2][0],$A[2][0],$C[0]
	andc	$C[0],$A[2][4],$A[2][3]
	xor	$A[2][1],$A[2][1],$C[1]
	xor	$A[2][3],$A[2][3],$C[2]
	xor	$A[2][4],$A[2][4],$C[3]
	xor	$A[2][2],$A[2][2],$C[0]

	andc	$C[0],$A[3][2],$A[3][1]
	andc	$C[1],$A[3][3],$A[3][2]
	andc	$C[2],$A[3][0],$A[3][4]
	andc	$C[3],$A[3][1],$A[3][0]
	xor	$A[3][0],$A[3][0],$C[0]
	andc	$C[0],$A[3][4],$A[3][3]
	xor	$A[3][1],$A[3][1],$C[1]
	xor	$A[3][3],$A[3][3],$C[2]
	xor	$A[3][4],$A[3][4],$C[3]
	xor	$A[3][2],$A[3][2],$C[0]

	andc	$C[0],$A[4][2],$A[4][1]
	andc	$C[1],$A[4][3],$A[4][2]
	andc	$C[2],$A[4][0],$A[4][4]
	andc	$C[3],$A[4][1],$A[4][0]
	xor	$A[4][0],$A[4][0],$C[0]
	andc	$C[0],$A[4][4],$A[4][3]
	xor	$A[4][1],$A[4][1],$C[1]
	xor	$A[4][3],$A[4][3],$C[2]
	xor	$A[4][4],$A[4][4],$C[3]
	xor	$A[4][2],$A[4][2],$C[0]

	bdnz	.Loop

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,0,0
.size	KeccakF1600_int,.-KeccakF1600_int

.type	KeccakF1600,\@function
.align	5
KeccakF1600:
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

	bl	PICmeup
	subi	r12,r12,8			; prepare for ldu

	$PUSH	r3,`$LOCALS+0*$SIZE_T`($sp)
	;$PUSH	r4,`$LOCALS+1*$SIZE_T`($sp)
	;$PUSH	r5,`$LOCALS+2*$SIZE_T`($sp)
	;$PUSH	r6,`$LOCALS+3*$SIZE_T`($sp)
	$PUSH	r12,`$LOCALS+4*$SIZE_T`($sp)

	ld	$A[0][0],`8*0`(r3)		; load A[5][5]
	ld	$A[0][1],`8*1`(r3)
	ld	$A[0][2],`8*2`(r3)
	ld	$A[0][3],`8*3`(r3)
	ld	$A[0][4],`8*4`(r3)
	ld	$A[1][0],`8*5`(r3)
	ld	$A[1][1],`8*6`(r3)
	ld	$A[1][2],`8*7`(r3)
	ld	$A[1][3],`8*8`(r3)
	ld	$A[1][4],`8*9`(r3)
	ld	$A[2][0],`8*10`(r3)
	ld	$A[2][1],`8*11`(r3)
	ld	$A[2][2],`8*12`(r3)
	ld	$A[2][3],`8*13`(r3)
	ld	$A[2][4],`8*14`(r3)
	ld	$A[3][0],`8*15`(r3)
	ld	$A[3][1],`8*16`(r3)
	ld	$A[3][2],`8*17`(r3)
	ld	$A[3][3],`8*18`(r3)
	ld	$A[3][4],`8*19`(r3)
	ld	$A[4][0],`8*20`(r3)
	ld	$A[4][1],`8*21`(r3)
	ld	$A[4][2],`8*22`(r3)
	ld	$A[4][3],`8*23`(r3)
	ld	$A[4][4],`8*24`(r3)

	bl	KeccakF1600_int

	$POP	r3,`$LOCALS+0*$SIZE_T`($sp)
	std	$A[0][0],`8*0`(r3)		; return A[5][5]
	std	$A[0][1],`8*1`(r3)
	std	$A[0][2],`8*2`(r3)
	std	$A[0][3],`8*3`(r3)
	std	$A[0][4],`8*4`(r3)
	std	$A[1][0],`8*5`(r3)
	std	$A[1][1],`8*6`(r3)
	std	$A[1][2],`8*7`(r3)
	std	$A[1][3],`8*8`(r3)
	std	$A[1][4],`8*9`(r3)
	std	$A[2][0],`8*10`(r3)
	std	$A[2][1],`8*11`(r3)
	std	$A[2][2],`8*12`(r3)
	std	$A[2][3],`8*13`(r3)
	std	$A[2][4],`8*14`(r3)
	std	$A[3][0],`8*15`(r3)
	std	$A[3][1],`8*16`(r3)
	std	$A[3][2],`8*17`(r3)
	std	$A[3][3],`8*18`(r3)
	std	$A[3][4],`8*19`(r3)
	std	$A[4][0],`8*20`(r3)
	std	$A[4][1],`8*21`(r3)
	std	$A[4][2],`8*22`(r3)
	std	$A[4][3],`8*23`(r3)
	std	$A[4][4],`8*24`(r3)

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
	.byte	0,12,4,1,0x80,18,1,0
	.long	0
.size	KeccakF1600,.-KeccakF1600

.type	dword_le_load,\@function
.align	5
dword_le_load:
	lbzu	r0,1(r3)
	lbzu	r4,1(r3)
	lbzu	r5,1(r3)
	insrdi	r0,r4,8,48
	lbzu	r4,1(r3)
	insrdi	r0,r5,8,40
	lbzu	r5,1(r3)
	insrdi	r0,r4,8,32
	lbzu	r4,1(r3)
	insrdi	r0,r5,8,24
	lbzu	r5,1(r3)
	insrdi	r0,r4,8,16
	lbzu	r4,1(r3)
	insrdi	r0,r5,8,8
	insrdi	r0,r4,8,0
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,1,0
	.long	0
.size	dword_le_load,.-dword_le_load

.globl	SHA3_absorb
.type	SHA3_absorb,\@function
.align	5
SHA3_absorb:
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

	bl	PICmeup
	subi	r4,r4,1				; prepare for lbzu
	subi	r12,r12,8			; prepare for ldu

	$PUSH	r3,`$LOCALS+0*$SIZE_T`($sp)	; save A[][]
	$PUSH	r4,`$LOCALS+1*$SIZE_T`($sp)	; save inp
	$PUSH	r5,`$LOCALS+2*$SIZE_T`($sp)	; save len
	$PUSH	r6,`$LOCALS+3*$SIZE_T`($sp)	; save bsz
	mr	r0,r6
	$PUSH	r12,`$LOCALS+4*$SIZE_T`($sp)

	ld	$A[0][0],`8*0`(r3)		; load A[5][5]
	ld	$A[0][1],`8*1`(r3)
	ld	$A[0][2],`8*2`(r3)
	ld	$A[0][3],`8*3`(r3)
	ld	$A[0][4],`8*4`(r3)
	ld	$A[1][0],`8*5`(r3)
	ld	$A[1][1],`8*6`(r3)
	ld	$A[1][2],`8*7`(r3)
	ld	$A[1][3],`8*8`(r3)
	ld	$A[1][4],`8*9`(r3)
	ld	$A[2][0],`8*10`(r3)
	ld	$A[2][1],`8*11`(r3)
	ld	$A[2][2],`8*12`(r3)
	ld	$A[2][3],`8*13`(r3)
	ld	$A[2][4],`8*14`(r3)
	ld	$A[3][0],`8*15`(r3)
	ld	$A[3][1],`8*16`(r3)
	ld	$A[3][2],`8*17`(r3)
	ld	$A[3][3],`8*18`(r3)
	ld	$A[3][4],`8*19`(r3)
	ld	$A[4][0],`8*20`(r3)
	ld	$A[4][1],`8*21`(r3)
	ld	$A[4][2],`8*22`(r3)
	ld	$A[4][3],`8*23`(r3)
	ld	$A[4][4],`8*24`(r3)

	mr	r3,r4
	mr	r4,r5
	mr	r5,r0

	b	.Loop_absorb

.align	4
.Loop_absorb:
	$UCMP	r4,r5				; len < bsz?
	blt	.Labsorbed

	sub	r4,r4,r5			; len -= bsz
	srwi	r5,r5,3
	$PUSH	r4,`$LOCALS+2*$SIZE_T`($sp)	; save len
	mtctr	r5
	bl	dword_le_load			; *inp++
	xor	$A[0][0],$A[0][0],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[0][1],$A[0][1],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[0][2],$A[0][2],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[0][3],$A[0][3],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[0][4],$A[0][4],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[1][0],$A[1][0],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[1][1],$A[1][1],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[1][2],$A[1][2],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[1][3],$A[1][3],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[1][4],$A[1][4],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[2][0],$A[2][0],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[2][1],$A[2][1],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[2][2],$A[2][2],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[2][3],$A[2][3],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[2][4],$A[2][4],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[3][0],$A[3][0],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[3][1],$A[3][1],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[3][2],$A[3][2],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[3][3],$A[3][3],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[3][4],$A[3][4],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[4][0],$A[4][0],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[4][1],$A[4][1],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[4][2],$A[4][2],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[4][3],$A[4][3],r0
	bdz	.Lprocess_block
	bl	dword_le_load			; *inp++
	xor	$A[4][4],$A[4][4],r0

.Lprocess_block:
	$PUSH	r3,`$LOCALS+1*$SIZE_T`($sp)	; save inp

	bl	KeccakF1600_int

	$POP	r0,`$LOCALS+4*$SIZE_T`($sp)	; pull iotas[24]
	$POP	r5,`$LOCALS+3*$SIZE_T`($sp)	; restore bsz
	$POP	r4,`$LOCALS+2*$SIZE_T`($sp)	; restore len
	$POP	r3,`$LOCALS+1*$SIZE_T`($sp)	; restore inp
	addic	r0,r0,`-8*24`			; rewind iotas
	$PUSH	r0,`$LOCALS+4*$SIZE_T`($sp)

	b	.Loop_absorb

.align	4
.Labsorbed:
	$POP	r3,`$LOCALS+0*$SIZE_T`($sp)
	std	$A[0][0],`8*0`(r3)		; return A[5][5]
	std	$A[0][1],`8*1`(r3)
	std	$A[0][2],`8*2`(r3)
	std	$A[0][3],`8*3`(r3)
	std	$A[0][4],`8*4`(r3)
	std	$A[1][0],`8*5`(r3)
	std	$A[1][1],`8*6`(r3)
	std	$A[1][2],`8*7`(r3)
	std	$A[1][3],`8*8`(r3)
	std	$A[1][4],`8*9`(r3)
	std	$A[2][0],`8*10`(r3)
	std	$A[2][1],`8*11`(r3)
	std	$A[2][2],`8*12`(r3)
	std	$A[2][3],`8*13`(r3)
	std	$A[2][4],`8*14`(r3)
	std	$A[3][0],`8*15`(r3)
	std	$A[3][1],`8*16`(r3)
	std	$A[3][2],`8*17`(r3)
	std	$A[3][3],`8*18`(r3)
	std	$A[3][4],`8*19`(r3)
	std	$A[4][0],`8*20`(r3)
	std	$A[4][1],`8*21`(r3)
	std	$A[4][2],`8*22`(r3)
	std	$A[4][3],`8*23`(r3)
	std	$A[4][4],`8*24`(r3)

	mr	r3,r4				; return value
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
	.byte	0,12,4,1,0x80,18,4,0
	.long	0
.size	SHA3_absorb,.-SHA3_absorb
___
{
my ($A_flat,$out,$len,$bsz) = map("r$_",(28..31));
$code.=<<___;
.globl	SHA3_squeeze
.type	SHA3_squeeze,\@function
.align	5
SHA3_squeeze:
	$STU	$sp,`-10*$SIZE_T`($sp)
	mflr	r0
	$PUSH	r28,`6*$SIZE_T`($sp)
	$PUSH	r29,`7*$SIZE_T`($sp)
	$PUSH	r30,`8*$SIZE_T`($sp)
	$PUSH	r31,`9*$SIZE_T`($sp)
	$PUSH	r0,`10*$SIZE_T+$LRSAVE`($sp)

	mr	$A_flat,r3
	subi	r3,r3,8			; prepare for ldu
	subi	$out,r4,1		; prepare for stbu
	mr	$len,r5
	mr	$bsz,r6
	b	.Loop_squeeze

.align	4
.Loop_squeeze:
	ldu	r0,8(r3)
	${UCMP}i $len,8
	blt	.Lsqueeze_tail

	stbu	r0,1($out)
	srdi	r0,r0,8
	stbu	r0,1($out)
	srdi	r0,r0,8
	stbu	r0,1($out)
	srdi	r0,r0,8
	stbu	r0,1($out)
	srdi	r0,r0,8
	stbu	r0,1($out)
	srdi	r0,r0,8
	stbu	r0,1($out)
	srdi	r0,r0,8
	stbu	r0,1($out)
	srdi	r0,r0,8
	stbu	r0,1($out)

	subic.	$len,$len,8
	beq	.Lsqueeze_done

	subic.	r6,r6,8
	bgt	.Loop_squeeze

	mr	r3,$A_flat
	bl	KeccakF1600
	subi	r3,$A_flat,8		; prepare for ldu
	mr	r6,$bsz
	b	.Loop_squeeze

.align	4
.Lsqueeze_tail:
	mtctr	$len
.Loop_tail:
	stbu	r0,1($out)
	srdi	r0,r0,8
	bdnz	.Loop_tail

.Lsqueeze_done:
	$POP	r0,`10*$SIZE_T+$LRSAVE`($sp)
	$POP	r28,`6*$SIZE_T`($sp)
	$POP	r29,`7*$SIZE_T`($sp)
	$POP	r30,`8*$SIZE_T`($sp)
	$POP	r31,`9*$SIZE_T`($sp)
	mtlr	r0
	addi	$sp,$sp,`10*$SIZE_T`
	blr
	.long	0
	.byte	0,12,4,1,0x80,4,4,0
	.long	0
.size	SHA3_squeeze,.-SHA3_squeeze
___
}

# Ugly hack here, because PPC assembler syntax seem to vary too
# much from platforms to platform...
$code.=<<___;
.align	6
PICmeup:
	mflr	r0
	bcl	20,31,\$+4
	mflr	r12   ; vvvvvv "distance" between . and 1st data entry
	addi	r12,r12,`64-8`
	mtlr	r0
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,0,0
	.space	`64-9*4`
.type	iotas,\@object
iotas:
	.quad	0x0000000000000001
	.quad	0x0000000000008082
	.quad	0x800000000000808a
	.quad	0x8000000080008000
	.quad	0x000000000000808b
	.quad	0x0000000080000001
	.quad	0x8000000080008081
	.quad	0x8000000000008009
	.quad	0x000000000000008a
	.quad	0x0000000000000088
	.quad	0x0000000080008009
	.quad	0x000000008000000a
	.quad	0x000000008000808b
	.quad	0x800000000000008b
	.quad	0x8000000000008089
	.quad	0x8000000000008003
	.quad	0x8000000000008002
	.quad	0x8000000000000080
	.quad	0x000000000000800a
	.quad	0x800000008000000a
	.quad	0x8000000080008081
	.quad	0x8000000000008080
	.quad	0x0000000080000001
	.quad	0x8000000080008008
.size	iotas,.-iotas
.asciz	"Keccak-1600 absorb and squeeze for PPC64, CRYPTOGAMS by <appro\@openssl.org>"
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

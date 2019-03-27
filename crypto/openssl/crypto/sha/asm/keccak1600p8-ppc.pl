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
# Keccak-1600 for PowerISA 2.07.
#
# June 2017.
#
# This is straightforward KECCAK_1X_ALT SIMD implementation, but with
# disjoint Rho and Pi. The module is ABI-bitness- and endian-neutral.
# POWER8 processor spends 9.8 cycles to process byte out of large
# buffer for r=1088, which matches SHA3-256. This is 17% better than
# scalar PPC64 code. It probably should be noted that if POWER8's
# successor can achieve higher scalar instruction issue rate, then
# this module will loose... And it does on POWER9 with 12.0 vs. 9.4.

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
	$STU	="stwu";
	$POP	="lwz";
	$PUSH	="stw";
	$UCMP	="cmplw";
} else { die "nonsense $flavour"; }

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour ".shift || die "can't call $xlate: $!";

$FRAME=6*$SIZE_T+13*16;	# 13*16 is for v20-v31 offload

my $sp ="r1";

my $iotas = "r12";

########################################################################
# Register layout:
#
# v0		A[0][0] A[1][0]
# v1		A[0][1] A[1][1]
# v2		A[0][2] A[1][2]
# v3		A[0][3] A[1][3]
# v4		A[0][4] A[1][4]
#
# v5		A[2][0] A[3][0]
# v6		A[2][1] A[3][1]
# v7		A[2][2] A[3][2]
# v8		A[2][3] A[3][3]
# v9		A[2][4] A[3][4]
#
# v10		A[4][0] A[4][1]
# v11		A[4][2] A[4][3]
# v12		A[4][4] A[4][4]
#
# v13..25	rhotates[][]
# v26..31	volatile
#
$code.=<<___;
.machine	"any"
.text

.type	KeccakF1600_int,\@function
.align	5
KeccakF1600_int:
	li	r0,24
	mtctr	r0
	li	r0,0
	b	.Loop

.align	4
.Loop:
	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Theta
	vxor	v26,v0, v5		; A[0..1][0]^A[2..3][0]
	vxor	v27,v1, v6		; A[0..1][1]^A[2..3][1]
	vxor	v28,v2, v7		; A[0..1][2]^A[2..3][2]
	vxor	v29,v3, v8		; A[0..1][3]^A[2..3][3]
	vxor	v30,v4, v9		; A[0..1][4]^A[2..3][4]
	vpermdi	v31,v26,v27,0b00	; A[0][0..1]^A[2][0..1]
	vpermdi	v26,v26,v27,0b11	; A[1][0..1]^A[3][0..1]
	vpermdi	v27,v28,v29,0b00	; A[0][2..3]^A[2][2..3]
	vpermdi	v28,v28,v29,0b11	; A[1][2..3]^A[3][2..3]
	vpermdi	v29,v30,v30,0b10	; A[1..0][4]^A[3..2][4]
	vxor	v26,v26,v31		; C[0..1]
	vxor	v27,v27,v28		; C[2..3]
	vxor	v28,v29,v30		; C[4..4]
	vspltisb v31,1
	vxor	v26,v26,v10		; C[0..1] ^= A[4][0..1]
	vxor	v27,v27,v11		; C[2..3] ^= A[4][2..3]
	vxor	v28,v28,v12		; C[4..4] ^= A[4][4..4], low!

	vrld	v29,v26,v31		; ROL64(C[0..1],1)
	vrld	v30,v27,v31		; ROL64(C[2..3],1)
	vrld	v31,v28,v31		; ROL64(C[4..4],1)
	vpermdi	v31,v31,v29,0b10
	vxor	v26,v26,v30		; C[0..1] ^= ROL64(C[2..3],1)
	vxor	v27,v27,v31		; C[2..3] ^= ROL64(C[4..0],1)
	vxor	v28,v28,v29		; C[4..4] ^= ROL64(C[0..1],1), low!

	vpermdi	v29,v26,v26,0b00	; C[0..0]
	vpermdi	v30,v28,v26,0b10	; C[4..0]
	vpermdi	v31,v28,v28,0b11	; C[4..4]
	vxor	v1, v1, v29		; A[0..1][1] ^= C[0..0]
	vxor	v6, v6, v29		; A[2..3][1] ^= C[0..0]
	vxor	v10,v10,v30		; A[4][0..1] ^= C[4..0]
	vxor	v0, v0, v31		; A[0..1][0] ^= C[4..4]
	vxor	v5, v5, v31		; A[2..3][0] ^= C[4..4]

	vpermdi	v29,v27,v27,0b00	; C[2..2]
	vpermdi	v30,v26,v26,0b11	; C[1..1]
	vpermdi	v31,v26,v27,0b10	; C[1..2]
	vxor	v3, v3, v29		; A[0..1][3] ^= C[2..2]
	vxor	v8, v8, v29		; A[2..3][3] ^= C[2..2]
	vxor	v2, v2, v30		; A[0..1][2] ^= C[1..1]
	vxor	v7, v7, v30		; A[2..3][2] ^= C[1..1]
	vxor	v11,v11,v31		; A[4][2..3] ^= C[1..2]

	vpermdi	v29,v27,v27,0b11	; C[3..3]
	vxor	v4, v4, v29		; A[0..1][4] ^= C[3..3]
	vxor	v9, v9, v29		; A[2..3][4] ^= C[3..3]
	vxor	v12,v12,v29		; A[4..4][4] ^= C[3..3]

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Rho
	vrld	v26,v0, v13		; v0
	vrld	v1, v1, v14
	vrld	v27,v2, v15		; v2
	vrld	v28,v3, v16		; v3
	vrld	v4, v4, v17
	vrld	v5, v5, v18
	vrld	v6, v6, v19
	vrld	v29,v7, v20		; v7
	vrld	v8, v8, v21
	vrld	v9, v9, v22
	vrld	v10,v10,v23
	vrld	v30,v11,v24		; v11
	vrld	v12,v12,v25

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Pi
	vpermdi	v0, v26,v28,0b00	; [0][0] [1][0] < [0][0] [0][3]
	vpermdi	v2, v29,v5, 0b00	; [0][2] [1][2] < [2][2] [2][0]
	vpermdi	v11,v9, v5, 0b01	; [4][2] [4][3] < [2][4] [3][0]
	vpermdi	v5, v1, v4, 0b00	; [2][0] [3][0] < [0][1] [0][4]
	vpermdi	v1, v1, v4, 0b11	; [0][1] [1][1] < [1][1] [1][4]
	vpermdi	v3, v8, v6, 0b11	; [0][3] [1][3] < [3][3] [3][1]
	vpermdi	v4, v12,v30,0b10	; [0][4] [1][4] < [4][4] [4][2]
	vpermdi	v7, v8, v6, 0b00	; [2][2] [3][2] < [2][3] [2][1]
	vpermdi	v6, v27,v26,0b11	; [2][1] [3][1] < [1][2] [1][0]
	vpermdi	v8, v9, v29,0b11	; [2][3] [3][3] < [3][4] [3][2]
	vpermdi	v12,v10,v10,0b11	; [4][4] [4][4] < [4][1] [4][1]
	vpermdi	v9, v10,v30,0b01	; [2][4] [3][4] < [4][0] [4][3]
	vpermdi	v10,v27,v28,0b01	; [4][0] [4][1] < [0][2] [1][3]

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Chi + Iota
	lvx_u	v31,$iotas,r0		; iotas[index]
	addic	r0,r0,16		; index++

	vandc	v26,v2, v1		; (~A[0..1][1] & A[0..1][2])
	vandc	v27,v3, v2		; (~A[0..1][2] & A[0..1][3])
	vandc	v28,v4, v3		; (~A[0..1][3] & A[0..1][4])
	vandc	v29,v0, v4		; (~A[0..1][4] & A[0..1][0])
	vandc	v30,v1, v0		; (~A[0..1][0] & A[0..1][1])
	vxor	v0, v0, v26		; A[0..1][0] ^= (~A[0..1][1] & A[0..1][2])
	vxor	v1, v1, v27		; A[0..1][1] ^= (~A[0..1][2] & A[0..1][3])
	vxor	v2, v2, v28		; A[0..1][2] ^= (~A[0..1][3] & A[0..1][4])
	vxor	v3, v3, v29		; A[0..1][3] ^= (~A[0..1][4] & A[0..1][0])
	vxor	v4, v4, v30		; A[0..1][4] ^= (~A[0..1][0] & A[0..1][1])

	vandc	v26,v7, v6		; (~A[2..3][1] & A[2..3][2])
	vandc	v27,v8, v7		; (~A[2..3][2] & A[2..3][3])
	vandc	v28,v9, v8		; (~A[2..3][3] & A[2..3][4])
	vandc	v29,v5, v9		; (~A[2..3][4] & A[2..3][0])
	vandc	v30,v6, v5		; (~A[2..3][0] & A[2..3][1])
	vxor	v5, v5, v26		; A[2..3][0] ^= (~A[2..3][1] & A[2..3][2])
	vxor	v6, v6, v27		; A[2..3][1] ^= (~A[2..3][2] & A[2..3][3])
	vxor	v7, v7, v28		; A[2..3][2] ^= (~A[2..3][3] & A[2..3][4])
	vxor	v8, v8, v29		; A[2..3][3] ^= (~A[2..3][4] & A[2..3][0])
	vxor	v9, v9, v30		; A[2..3][4] ^= (~A[2..3][0] & A[2..3][1])

	vxor	v0, v0, v31		; A[0][0] ^= iotas[index++]

	vpermdi	v26,v10,v11,0b10	; A[4][1..2]
	vpermdi	v27,v12,v10,0b00	; A[4][4..0]
	vpermdi	v28,v11,v12,0b10	; A[4][3..4]
	vpermdi	v29,v10,v10,0b10	; A[4][1..0]
	vandc	v26,v11,v26		; (~A[4][1..2] & A[4][2..3])
	vandc	v27,v27,v28		; (~A[4][3..4] & A[4][4..0])
	vandc	v28,v10,v29		; (~A[4][1..0] & A[4][0..1])
	vxor	v10,v10,v26		; A[4][0..1] ^= (~A[4][1..2] & A[4][2..3])
	vxor	v11,v11,v27		; A[4][2..3] ^= (~A[4][3..4] & A[4][4..0])
	vxor	v12,v12,v28		; A[4][4..4] ^= (~A[4][0..1] & A[4][1..0])

	bdnz	.Loop

	vpermdi	v12,v12,v12,0b11	; broadcast A[4][4]
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,0,0
.size	KeccakF1600_int,.-KeccakF1600_int

.type	KeccakF1600,\@function
.align	5
KeccakF1600:
	$STU	$sp,-$FRAME($sp)
	li	r10,`15+6*$SIZE_T`
	li	r11,`31+6*$SIZE_T`
	mflr	r8
	mfspr	r7, 256			; save vrsave
	stvx	v20,r10,$sp
	addi	r10,r10,32
	stvx	v21,r11,$sp
	addi	r11,r11,32
	stvx	v22,r10,$sp
	addi	r10,r10,32
	stvx	v23,r11,$sp
	addi	r11,r11,32
	stvx	v24,r10,$sp
	addi	r10,r10,32
	stvx	v25,r11,$sp
	addi	r11,r11,32
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
	stw	r7,`$FRAME-4`($sp)	; save vrsave
	li	r0, -1
	$PUSH	r8,`$FRAME+$LRSAVE`($sp)
	mtspr	256, r0			; preserve all AltiVec registers

	li	r11,16
	lvx_4w	v0,0,r3			; load A[5][5]
	li	r10,32
	lvx_4w	v1,r11,r3
	addi	r11,r11,32
	lvx_4w	v2,r10,r3
	addi	r10,r10,32
	lvx_4w	v3,r11,r3
	addi	r11,r11,32
	lvx_4w	v4,r10,r3
	addi	r10,r10,32
	lvx_4w	v5,r11,r3
	addi	r11,r11,32
	lvx_4w	v6,r10,r3
	addi	r10,r10,32
	lvx_4w	v7,r11,r3
	addi	r11,r11,32
	lvx_4w	v8,r10,r3
	addi	r10,r10,32
	lvx_4w	v9,r11,r3
	addi	r11,r11,32
	lvx_4w	v10,r10,r3
	addi	r10,r10,32
	lvx_4w	v11,r11,r3
	lvx_splt v12,r10,r3

	bl	PICmeup

	li	r11,16
	lvx_u	v13,0,r12		; load rhotates
	li	r10,32
	lvx_u	v14,r11,r12
	addi	r11,r11,32
	lvx_u	v15,r10,r12
	addi	r10,r10,32
	lvx_u	v16,r11,r12
	addi	r11,r11,32
	lvx_u	v17,r10,r12
	addi	r10,r10,32
	lvx_u	v18,r11,r12
	addi	r11,r11,32
	lvx_u	v19,r10,r12
	addi	r10,r10,32
	lvx_u	v20,r11,r12
	addi	r11,r11,32
	lvx_u	v21,r10,r12
	addi	r10,r10,32
	lvx_u	v22,r11,r12
	addi	r11,r11,32
	lvx_u	v23,r10,r12
	addi	r10,r10,32
	lvx_u	v24,r11,r12
	lvx_u	v25,r10,r12
	addi	r12,r12,`16*16`		; points at iotas

	bl	KeccakF1600_int

	li	r11,16
	stvx_4w	v0,0,r3			; return A[5][5]
	li	r10,32
	stvx_4w	v1,r11,r3
	addi	r11,r11,32
	stvx_4w	v2,r10,r3
	addi	r10,r10,32
	stvx_4w	v3,r11,r3
	addi	r11,r11,32
	stvx_4w	v4,r10,r3
	addi	r10,r10,32
	stvx_4w	v5,r11,r3
	addi	r11,r11,32
	stvx_4w	v6,r10,r3
	addi	r10,r10,32
	stvx_4w	v7,r11,r3
	addi	r11,r11,32
	stvx_4w	v8,r10,r3
	addi	r10,r10,32
	stvx_4w	v9,r11,r3
	addi	r11,r11,32
	stvx_4w	v10,r10,r3
	addi	r10,r10,32
	stvx_4w	v11,r11,r3
	stvdx_u v12,r10,r3

	li	r10,`15+6*$SIZE_T`
	li	r11,`31+6*$SIZE_T`
	mtlr	r8
	mtspr	256, r7			; restore vrsave
	lvx	v20,r10,$sp
	addi	r10,r10,32
	lvx	v21,r11,$sp
	addi	r11,r11,32
	lvx	v22,r10,$sp
	addi	r10,r10,32
	lvx	v23,r11,$sp
	addi	r11,r11,32
	lvx	v24,r10,$sp
	addi	r10,r10,32
	lvx	v25,r11,$sp
	addi	r11,r11,32
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
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,0x04,1,0x80,0,1,0
	.long	0
.size	KeccakF1600,.-KeccakF1600
___
{
my ($A_jagged,$inp,$len,$bsz) = map("r$_",(3..6));

$code.=<<___;
.globl	SHA3_absorb
.type	SHA3_absorb,\@function
.align	5
SHA3_absorb:
	$STU	$sp,-$FRAME($sp)
	li	r10,`15+6*$SIZE_T`
	li	r11,`31+6*$SIZE_T`
	mflr	r8
	mfspr	r7, 256			; save vrsave
	stvx	v20,r10,$sp
	addi	r10,r10,32
	stvx	v21,r11,$sp
	addi	r11,r11,32
	stvx	v22,r10,$sp
	addi	r10,r10,32
	stvx	v23,r11,$sp
	addi	r11,r11,32
	stvx	v24,r10,$sp
	addi	r10,r10,32
	stvx	v25,r11,$sp
	addi	r11,r11,32
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
	stw	r7,`$FRAME-4`($sp)	; save vrsave
	li	r0, -1
	$PUSH	r8,`$FRAME+$LRSAVE`($sp)
	mtspr	256, r0			; preserve all AltiVec registers

	li	r11,16
	lvx_4w	v0,0,$A_jagged		; load A[5][5]
	li	r10,32
	lvx_4w	v1,r11,$A_jagged
	addi	r11,r11,32
	lvx_4w	v2,r10,$A_jagged
	addi	r10,r10,32
	lvx_4w	v3,r11,$A_jagged
	addi	r11,r11,32
	lvx_4w	v4,r10,$A_jagged
	addi	r10,r10,32
	lvx_4w	v5,r11,$A_jagged
	addi	r11,r11,32
	lvx_4w	v6,r10,$A_jagged
	addi	r10,r10,32
	lvx_4w	v7,r11,$A_jagged
	addi	r11,r11,32
	lvx_4w	v8,r10,$A_jagged
	addi	r10,r10,32
	lvx_4w	v9,r11,$A_jagged
	addi	r11,r11,32
	lvx_4w	v10,r10,$A_jagged
	addi	r10,r10,32
	lvx_4w	v11,r11,$A_jagged
	lvx_splt v12,r10,$A_jagged

	bl	PICmeup

	li	r11,16
	lvx_u	v13,0,r12		; load rhotates
	li	r10,32
	lvx_u	v14,r11,r12
	addi	r11,r11,32
	lvx_u	v15,r10,r12
	addi	r10,r10,32
	lvx_u	v16,r11,r12
	addi	r11,r11,32
	lvx_u	v17,r10,r12
	addi	r10,r10,32
	lvx_u	v18,r11,r12
	addi	r11,r11,32
	lvx_u	v19,r10,r12
	addi	r10,r10,32
	lvx_u	v20,r11,r12
	addi	r11,r11,32
	lvx_u	v21,r10,r12
	addi	r10,r10,32
	lvx_u	v22,r11,r12
	addi	r11,r11,32
	lvx_u	v23,r10,r12
	addi	r10,r10,32
	lvx_u	v24,r11,r12
	lvx_u	v25,r10,r12
	li	r10,-32
	li	r11,-16
	addi	r12,r12,`16*16`		; points at iotas
	b	.Loop_absorb

.align	4
.Loop_absorb:
	$UCMP	$len,$bsz		; len < bsz?
	blt	.Labsorbed

	sub	$len,$len,$bsz		; len -= bsz
	srwi	r0,$bsz,3
	mtctr	r0

	lvx_u	v30,r10,r12		; permutation masks
	lvx_u	v31,r11,r12
	?vspltisb v27,7			; prepare masks for byte swap
	?vxor	v30,v30,v27		; on big-endian
	?vxor	v31,v31,v27

	vxor	v27,v27,v27		; zero
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v0, v0, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v1, v1, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v2, v2, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v3, v3, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v4, v4, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v0, v0, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v1, v1, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v2, v2, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v3, v3, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v4, v4, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v5, v5, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v6, v6, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v7, v7, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v8, v8, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v9, v9, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v5, v5, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v6, v6, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v7, v7, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v8, v8, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v9, v9, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v10, v10, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v10, v10, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v30
	vxor	v11, v11, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v11, v11, v26
	bdz	.Lprocess_block
	lvdx_u	v26,0,$inp
	addi	$inp,$inp,8
	vperm	v26,v26,v27,v31
	vxor	v12, v12, v26

.Lprocess_block:
	bl	KeccakF1600_int

	b	.Loop_absorb

.align	4
.Labsorbed:
	li	r11,16
	stvx_4w	v0,0,$A_jagged		; return A[5][5]
	li	r10,32
	stvx_4w	v1,r11,$A_jagged
	addi	r11,r11,32
	stvx_4w	v2,r10,$A_jagged
	addi	r10,r10,32
	stvx_4w	v3,r11,$A_jagged
	addi	r11,r11,32
	stvx_4w	v4,r10,$A_jagged
	addi	r10,r10,32
	stvx_4w	v5,r11,$A_jagged
	addi	r11,r11,32
	stvx_4w	v6,r10,$A_jagged
	addi	r10,r10,32
	stvx_4w	v7,r11,$A_jagged
	addi	r11,r11,32
	stvx_4w	v8,r10,$A_jagged
	addi	r10,r10,32
	stvx_4w	v9,r11,$A_jagged
	addi	r11,r11,32
	stvx_4w	v10,r10,$A_jagged
	addi	r10,r10,32
	stvx_4w	v11,r11,$A_jagged
	stvdx_u v12,r10,$A_jagged

	mr	r3,$len			; return value
	li	r10,`15+6*$SIZE_T`
	li	r11,`31+6*$SIZE_T`
	mtlr	r8
	mtspr	256, r7			; restore vrsave
	lvx	v20,r10,$sp
	addi	r10,r10,32
	lvx	v21,r11,$sp
	addi	r11,r11,32
	lvx	v22,r10,$sp
	addi	r10,r10,32
	lvx	v23,r11,$sp
	addi	r11,r11,32
	lvx	v24,r10,$sp
	addi	r10,r10,32
	lvx	v25,r11,$sp
	addi	r11,r11,32
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
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,0x04,1,0x80,0,4,0
	.long	0
.size	SHA3_absorb,.-SHA3_absorb
___
}
{
my ($A_jagged,$out,$len,$bsz) = map("r$_",(3..6));

$code.=<<___;
.globl	SHA3_squeeze
.type	SHA3_squeeze,\@function
.align	5
SHA3_squeeze:
	mflr	r9			; r9 is not touched by KeccakF1600
	subi	$out,$out,1		; prepare for stbu
	addi	r8,$A_jagged,4		; prepare volatiles
	mr	r10,$bsz
	li	r11,0
	b	.Loop_squeeze
.align	4
.Loop_squeeze:
	lwzx	r7,r11,r8		; lo
	lwzx	r0,r11,$A_jagged	; hi
	${UCMP}i $len,8
	blt	.Lsqueeze_tail

	stbu	r7,1($out)		; write lo
	srwi	r7,r7,8
	stbu	r7,1($out)
	srwi	r7,r7,8
	stbu	r7,1($out)
	srwi	r7,r7,8
	stbu	r7,1($out)
	stbu	r0,1($out)		; write hi
	srwi	r0,r0,8
	stbu	r0,1($out)
	srwi	r0,r0,8
	stbu	r0,1($out)
	srwi	r0,r0,8
	stbu	r0,1($out)

	subic.	$len,$len,8
	beqlr				; return if done

	subic.	r10,r10,8
	ble	.Loutput_expand

	addi	r11,r11,16		; calculate jagged index
	cmplwi	r11,`16*5`
	blt	.Loop_squeeze
	subi	r11,r11,72
	beq	.Loop_squeeze
	addi	r11,r11,72
	cmplwi	r11,`16*5+8`
	subi	r11,r11,8
	beq	.Loop_squeeze
	addi	r11,r11,8
	cmplwi	r11,`16*10`
	subi	r11,r11,72
	beq	.Loop_squeeze
	addi	r11,r11,72
	blt	.Loop_squeeze
	subi	r11,r11,8
	b	.Loop_squeeze

.align	4
.Loutput_expand:
	bl	KeccakF1600
	mtlr	r9

	addi	r8,$A_jagged,4		; restore volatiles
	mr	r10,$bsz
	li	r11,0
	b	.Loop_squeeze

.align	4
.Lsqueeze_tail:
	mtctr	$len
	subic.	$len,$len,4
	ble	.Loop_tail_lo
	li	r8,4
	mtctr	r8
.Loop_tail_lo:
	stbu	r7,1($out)
	srdi	r7,r7,8
	bdnz	.Loop_tail_lo
	ble	.Lsqueeze_done
	mtctr	$len
.Loop_tail_hi:
	stbu	r0,1($out)
	srdi	r0,r0,8
	bdnz	.Loop_tail_hi

.Lsqueeze_done:
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,4,0
	.long	0
.size	SHA3_squeeze,.-SHA3_squeeze
___
}
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
.type	rhotates,\@object
.align	6
rhotates:
	.quad	0,  36
	.quad	1,  44
	.quad	62,  6
	.quad	28, 55
	.quad	27, 20
	.quad	3,  41
	.quad	10, 45
	.quad	43, 15
	.quad	25, 21
	.quad	39,  8
	.quad	18,  2
	.quad	61, 56
	.quad	14, 14
.size	rhotates,.-rhotates
	.quad	0,0
	.quad	0x0001020304050607,0x1011121314151617
	.quad	0x1011121314151617,0x0001020304050607
.type	iotas,\@object
iotas:
	.quad	0x0000000000000001,0
	.quad	0x0000000000008082,0
	.quad	0x800000000000808a,0
	.quad	0x8000000080008000,0
	.quad	0x000000000000808b,0
	.quad	0x0000000080000001,0
	.quad	0x8000000080008081,0
	.quad	0x8000000000008009,0
	.quad	0x000000000000008a,0
	.quad	0x0000000000000088,0
	.quad	0x0000000080008009,0
	.quad	0x000000008000000a,0
	.quad	0x000000008000808b,0
	.quad	0x800000000000008b,0
	.quad	0x8000000000008089,0
	.quad	0x8000000000008003,0
	.quad	0x8000000000008002,0
	.quad	0x8000000000000080,0
	.quad	0x000000000000800a,0
	.quad	0x800000008000000a,0
	.quad	0x8000000080008081,0
	.quad	0x8000000000008080,0
	.quad	0x0000000080000001,0
	.quad	0x8000000080008008,0
.size	iotas,.-iotas
.asciz	"Keccak-1600 absorb and squeeze for PowerISA 2.07, CRYPTOGAMS by <appro\@openssl.org>"
___

foreach  (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	if ($flavour =~ /le$/) {	# little-endian
	    s/\?([a-z]+)/;$1/;
	} else {			# big-endian
	    s/\?([a-z]+)/$1/;
	}

	print $_,"\n";
}

close STDOUT;

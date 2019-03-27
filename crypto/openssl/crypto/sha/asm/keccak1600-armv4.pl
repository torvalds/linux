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
# Keccak-1600 for ARMv4.
#
# June 2017.
#
# Non-NEON code is KECCAK_1X variant (see sha/keccak1600.c) with bit
# interleaving. How does it compare to Keccak Code Package? It's as
# fast, but several times smaller, and is endian- and ISA-neutral. ISA
# neutrality means that minimum ISA requirement is ARMv4, yet it can
# be assembled even as Thumb-2. NEON code path is KECCAK_1X_ALT with
# register layout taken from Keccak Code Package. It's also as fast,
# in fact faster by 10-15% on some processors, and endian-neutral.
#
# August 2017.
#
# Switch to KECCAK_2X variant for non-NEON code and merge almost 1/2
# of rotate instructions with logical ones. This resulted in ~10%
# improvement on most processors. Switch to KECCAK_2X effectively
# minimizes re-loads from temporary storage, and merged rotates just
# eliminate corresponding instructions. As for latter. When examining
# code you'll notice commented ror instructions. These are eliminated
# ones, and you should trace destination register below to see what's
# going on. Just in case, why not all rotates are eliminated. Trouble
# is that you have operations that require both inputs to be rotated,
# e.g. 'eor a,b>>>x,c>>>y'. This conundrum is resolved by using
# 'eor a,b,c>>>(x-y)' and then merge-rotating 'a' in next operation
# that takes 'a' as input. And thing is that this next operation can
# be in next round. It's totally possible to "carry" rotate "factors"
# to the next round, but it makes code more complex. And the last word
# is the keyword, i.e. "almost 1/2" is kind of complexity cap [for the
# time being]...
#
# Reduce per-round instruction count in Thumb-2 case by 16%. This is
# achieved by folding ldr/str pairs to their double-word counterparts.
# Theoretically this should have improved performance on single-issue
# cores, such as Cortex-A5/A7, by 19%. Reality is a bit different, as
# usual...
#
########################################################################
# Numbers are cycles per processed byte. Non-NEON results account even
# for input bit interleaving.
#
#		r=1088(*)   Thumb-2(**) NEON
#
# ARM11xx	82/+150%
# Cortex-A5	88/+160%,   86,         36
# Cortex-A7	78/+160%,   68,         34
# Cortex-A8	51/+230%,   57,         30
# Cortex-A9	53/+210%,   51,         26
# Cortex-A15	42/+160%,   38,         18
# Snapdragon S4	43/+210%,   38,         24
#
# (*)	Corresponds to SHA3-256. Percentage after slash is improvement
#	over compiler-generated KECCAK_2X reference code.
# (**)	Thumb-2 results for Cortex-A5/A7 are likely to apply even to
#	Cortex-Mx, x>=3. Otherwise, non-NEON results for NEON-capable
#	processors are presented mostly for reference purposes.

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

my @C = map("r$_",(0..9));
my @E = map("r$_",(10..12,14));

########################################################################
# Stack layout
# ----->+-----------------------+
#       | uint64_t A[5][5]      |
#       | ...                   |
# +200->+-----------------------+
#       | uint64_t D[5]         |
#       | ...                   |
# +240->+-----------------------+
#       | uint64_t T[5][5]      |
#       | ...                   |
# +440->+-----------------------+
#       | saved lr              |
# +444->+-----------------------+
#       | loop counter          |
# +448->+-----------------------+
#       | ...

my @A = map([ 8*$_, 8*($_+1), 8*($_+2), 8*($_+3), 8*($_+4) ], (0,5,10,15,20));
my @D = map(8*$_, (25..29));
my @T = map([ 8*$_, 8*($_+1), 8*($_+2), 8*($_+3), 8*($_+4) ], (30,35,40,45,50));

$code.=<<___;
#include "arm_arch.h"

.text

#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif

.type	iotas32, %object
.align	5
iotas32:
	.long	0x00000001, 0x00000000
	.long	0x00000000, 0x00000089
	.long	0x00000000, 0x8000008b
	.long	0x00000000, 0x80008080
	.long	0x00000001, 0x0000008b
	.long	0x00000001, 0x00008000
	.long	0x00000001, 0x80008088
	.long	0x00000001, 0x80000082
	.long	0x00000000, 0x0000000b
	.long	0x00000000, 0x0000000a
	.long	0x00000001, 0x00008082
	.long	0x00000000, 0x00008003
	.long	0x00000001, 0x0000808b
	.long	0x00000001, 0x8000000b
	.long	0x00000001, 0x8000008a
	.long	0x00000001, 0x80000081
	.long	0x00000000, 0x80000081
	.long	0x00000000, 0x80000008
	.long	0x00000000, 0x00000083
	.long	0x00000000, 0x80008003
	.long	0x00000001, 0x80008088
	.long	0x00000000, 0x80000088
	.long	0x00000001, 0x00008000
	.long	0x00000000, 0x80008082
.size	iotas32,.-iotas32

.type	KeccakF1600_int, %function
.align	5
KeccakF1600_int:
	add	@C[9],sp,#$A[4][2]
	add	@E[2],sp,#$A[0][0]
	add	@E[0],sp,#$A[1][0]
	ldmia	@C[9],{@C[4]-@C[9]}		@ A[4][2..4]
KeccakF1600_enter:
	str	lr,[sp,#440]
	eor	@E[1],@E[1],@E[1]
	str	@E[1],[sp,#444]
	b	.Lround2x

.align	4
.Lround2x:
___
sub Round {
my (@A,@R); (@A[0..4],@R) = @_;

$code.=<<___;
	ldmia	@E[2],{@C[0]-@C[3]}		@ A[0][0..1]
	ldmia	@E[0],{@E[0]-@E[2],@E[3]}	@ A[1][0..1]
#ifdef	__thumb2__
	eor	@C[0],@C[0],@E[0]
	eor	@C[1],@C[1],@E[1]
	eor	@C[2],@C[2],@E[2]
	ldrd	@E[0],@E[1],[sp,#$A[1][2]]
	eor	@C[3],@C[3],@E[3]
	ldrd	@E[2],@E[3],[sp,#$A[1][3]]
	eor	@C[4],@C[4],@E[0]
	eor	@C[5],@C[5],@E[1]
	eor	@C[6],@C[6],@E[2]
	ldrd	@E[0],@E[1],[sp,#$A[1][4]]
	eor	@C[7],@C[7],@E[3]
	ldrd	@E[2],@E[3],[sp,#$A[2][0]]
	eor	@C[8],@C[8],@E[0]
	eor	@C[9],@C[9],@E[1]
	eor	@C[0],@C[0],@E[2]
	ldrd	@E[0],@E[1],[sp,#$A[2][1]]
	eor	@C[1],@C[1],@E[3]
	ldrd	@E[2],@E[3],[sp,#$A[2][2]]
	eor	@C[2],@C[2],@E[0]
	eor	@C[3],@C[3],@E[1]
	eor	@C[4],@C[4],@E[2]
	ldrd	@E[0],@E[1],[sp,#$A[2][3]]
	eor	@C[5],@C[5],@E[3]
	ldrd	@E[2],@E[3],[sp,#$A[2][4]]
	eor	@C[6],@C[6],@E[0]
	eor	@C[7],@C[7],@E[1]
	eor	@C[8],@C[8],@E[2]
	ldrd	@E[0],@E[1],[sp,#$A[3][0]]
	eor	@C[9],@C[9],@E[3]
	ldrd	@E[2],@E[3],[sp,#$A[3][1]]
	eor	@C[0],@C[0],@E[0]
	eor	@C[1],@C[1],@E[1]
	eor	@C[2],@C[2],@E[2]
	ldrd	@E[0],@E[1],[sp,#$A[3][2]]
	eor	@C[3],@C[3],@E[3]
	ldrd	@E[2],@E[3],[sp,#$A[3][3]]
	eor	@C[4],@C[4],@E[0]
	eor	@C[5],@C[5],@E[1]
	eor	@C[6],@C[6],@E[2]
	ldrd	@E[0],@E[1],[sp,#$A[3][4]]
	eor	@C[7],@C[7],@E[3]
	ldrd	@E[2],@E[3],[sp,#$A[4][0]]
	eor	@C[8],@C[8],@E[0]
	eor	@C[9],@C[9],@E[1]
	eor	@C[0],@C[0],@E[2]
	ldrd	@E[0],@E[1],[sp,#$A[4][1]]
	eor	@C[1],@C[1],@E[3]
	ldrd	@E[2],@E[3],[sp,#$A[0][2]]
	eor	@C[2],@C[2],@E[0]
	eor	@C[3],@C[3],@E[1]
	eor	@C[4],@C[4],@E[2]
	ldrd	@E[0],@E[1],[sp,#$A[0][3]]
	eor	@C[5],@C[5],@E[3]
	ldrd	@E[2],@E[3],[sp,#$A[0][4]]
#else
	eor	@C[0],@C[0],@E[0]
	 add	@E[0],sp,#$A[1][2]
	eor	@C[1],@C[1],@E[1]
	eor	@C[2],@C[2],@E[2]
	eor	@C[3],@C[3],@E[3]
	ldmia	@E[0],{@E[0]-@E[2],@E[3]}	@ A[1][2..3]
	eor	@C[4],@C[4],@E[0]
	 add	@E[0],sp,#$A[1][4]
	eor	@C[5],@C[5],@E[1]
	eor	@C[6],@C[6],@E[2]
	eor	@C[7],@C[7],@E[3]
	ldmia	@E[0],{@E[0]-@E[2],@E[3]}	@ A[1][4]..A[2][0]
	eor	@C[8],@C[8],@E[0]
	 add	@E[0],sp,#$A[2][1]
	eor	@C[9],@C[9],@E[1]
	eor	@C[0],@C[0],@E[2]
	eor	@C[1],@C[1],@E[3]
	ldmia	@E[0],{@E[0]-@E[2],@E[3]}	@ A[2][1..2]
	eor	@C[2],@C[2],@E[0]
	 add	@E[0],sp,#$A[2][3]
	eor	@C[3],@C[3],@E[1]
	eor	@C[4],@C[4],@E[2]
	eor	@C[5],@C[5],@E[3]
	ldmia	@E[0],{@E[0]-@E[2],@E[3]}	@ A[2][3..4]
	eor	@C[6],@C[6],@E[0]
	 add	@E[0],sp,#$A[3][0]
	eor	@C[7],@C[7],@E[1]
	eor	@C[8],@C[8],@E[2]
	eor	@C[9],@C[9],@E[3]
	ldmia	@E[0],{@E[0]-@E[2],@E[3]}	@ A[3][0..1]
	eor	@C[0],@C[0],@E[0]
	 add	@E[0],sp,#$A[3][2]
	eor	@C[1],@C[1],@E[1]
	eor	@C[2],@C[2],@E[2]
	eor	@C[3],@C[3],@E[3]
	ldmia	@E[0],{@E[0]-@E[2],@E[3]}	@ A[3][2..3]
	eor	@C[4],@C[4],@E[0]
	 add	@E[0],sp,#$A[3][4]
	eor	@C[5],@C[5],@E[1]
	eor	@C[6],@C[6],@E[2]
	eor	@C[7],@C[7],@E[3]
	ldmia	@E[0],{@E[0]-@E[2],@E[3]}	@ A[3][4]..A[4][0]
	eor	@C[8],@C[8],@E[0]
	ldr	@E[0],[sp,#$A[4][1]]		@ A[4][1]
	eor	@C[9],@C[9],@E[1]
	ldr	@E[1],[sp,#$A[4][1]+4]
	eor	@C[0],@C[0],@E[2]
	ldr	@E[2],[sp,#$A[0][2]]		@ A[0][2]
	eor	@C[1],@C[1],@E[3]
	ldr	@E[3],[sp,#$A[0][2]+4]
	eor	@C[2],@C[2],@E[0]
	 add	@E[0],sp,#$A[0][3]
	eor	@C[3],@C[3],@E[1]
	eor	@C[4],@C[4],@E[2]
	eor	@C[5],@C[5],@E[3]
	ldmia	@E[0],{@E[0]-@E[2],@E[3]}	@ A[0][3..4]
#endif
	eor	@C[6],@C[6],@E[0]
	eor	@C[7],@C[7],@E[1]
	eor	@C[8],@C[8],@E[2]
	eor	@C[9],@C[9],@E[3]

	eor	@E[0],@C[0],@C[5],ror#32-1	@ E[0] = ROL64(C[2], 1) ^ C[0];
	str.l	@E[0],[sp,#$D[1]]		@ D[1] = E[0]
	eor	@E[1],@C[1],@C[4]
	str.h	@E[1],[sp,#$D[1]+4]
	eor	@E[2],@C[6],@C[1],ror#32-1	@ E[1] = ROL64(C[0], 1) ^ C[3];
	eor	@E[3],@C[7],@C[0]
	str.l	@E[2],[sp,#$D[4]]		@ D[4] = E[1]
	eor	@C[0],@C[8],@C[3],ror#32-1	@ C[0] = ROL64(C[1], 1) ^ C[4];
	str.h	@E[3],[sp,#$D[4]+4]
	eor	@C[1],@C[9],@C[2]
	str.l	@C[0],[sp,#$D[0]]		@ D[0] = C[0]
	eor	@C[2],@C[2],@C[7],ror#32-1	@ C[1] = ROL64(C[3], 1) ^ C[1];
	 ldr.l	@C[7],[sp,#$A[3][3]]
	eor	@C[3],@C[3],@C[6]
	str.h	@C[1],[sp,#$D[0]+4]
	 ldr.h	@C[6],[sp,#$A[3][3]+4]
	str.l	@C[2],[sp,#$D[2]]		@ D[2] = C[1]
	eor	@C[4],@C[4],@C[9],ror#32-1	@ C[2] = ROL64(C[4], 1) ^ C[2];
	str.h	@C[3],[sp,#$D[2]+4]
	eor	@C[5],@C[5],@C[8]

	ldr.l	@C[8],[sp,#$A[4][4]]
	ldr.h	@C[9],[sp,#$A[4][4]+4]
	 str.l	@C[4],[sp,#$D[3]]		@ D[3] = C[2]
	eor	@C[7],@C[7],@C[4]
	 str.h	@C[5],[sp,#$D[3]+4]
	eor	@C[6],@C[6],@C[5]
	ldr.l	@C[4],[sp,#$A[0][0]]
	@ ror	@C[7],@C[7],#32-10		@ C[3] = ROL64(A[3][3] ^ C[2], rhotates[3][3]);   /* D[3] */
	@ ror	@C[6],@C[6],#32-11
	ldr.h	@C[5],[sp,#$A[0][0]+4]
	eor	@C[8],@C[8],@E[2]
	eor	@C[9],@C[9],@E[3]
	ldr.l	@E[2],[sp,#$A[2][2]]
	eor	@C[0],@C[0],@C[4]
	ldr.h	@E[3],[sp,#$A[2][2]+4]
	@ ror	@C[8],@C[8],#32-7		@ C[4] = ROL64(A[4][4] ^ E[1], rhotates[4][4]);   /* D[4] */
	@ ror	@C[9],@C[9],#32-7
	eor	@C[1],@C[1],@C[5]		@ C[0] =       A[0][0] ^ C[0]; /* rotate by 0 */  /* D[0] */
	eor	@E[2],@E[2],@C[2]
	ldr.l	@C[2],[sp,#$A[1][1]]
	eor	@E[3],@E[3],@C[3]
	ldr.h	@C[3],[sp,#$A[1][1]+4]
	ror	@C[5],@E[2],#32-21		@ C[2] = ROL64(A[2][2] ^ C[1], rhotates[2][2]);   /* D[2] */
	 ldr	@E[2],[sp,#444]			@ load counter
	eor	@C[2],@C[2],@E[0]
	 adr	@E[0],iotas32
	ror	@C[4],@E[3],#32-22
	 add	@E[3],@E[0],@E[2]
	eor	@C[3],@C[3],@E[1]
___
$code.=<<___	if ($A[0][0] != $T[0][0]);
	ldmia	@E[3],{@E[0],@E[1]}		@ iotas[i]
___
$code.=<<___	if ($A[0][0] == $T[0][0]);
	ldr.l	@E[0],[@E[3],#8]		@ iotas[i].lo
	add	@E[2],@E[2],#16
	ldr.h	@E[1],[@E[3],#12]		@ iotas[i].hi
	cmp	@E[2],#192
	str	@E[2],[sp,#444]			@ store counter
___
$code.=<<___;
	bic	@E[2],@C[4],@C[2],ror#32-22
	bic	@E[3],@C[5],@C[3],ror#32-22
	 ror	@C[2],@C[2],#32-22		@ C[1] = ROL64(A[1][1] ^ E[0], rhotates[1][1]);   /* D[1] */
	 ror	@C[3],@C[3],#32-22
	eor	@E[2],@E[2],@C[0]
	eor	@E[3],@E[3],@C[1]
	eor	@E[0],@E[0],@E[2]
	eor	@E[1],@E[1],@E[3]
	str.l	@E[0],[sp,#$R[0][0]]		@ R[0][0] = C[0] ^ (~C[1] & C[2]) ^ iotas[i];
	bic	@E[2],@C[6],@C[4],ror#11
	str.h	@E[1],[sp,#$R[0][0]+4]
	bic	@E[3],@C[7],@C[5],ror#10
	bic	@E[0],@C[8],@C[6],ror#32-(11-7)
	bic	@E[1],@C[9],@C[7],ror#32-(10-7)
	eor	@E[2],@C[2],@E[2],ror#32-11
	str.l	@E[2],[sp,#$R[0][1]]		@ R[0][1] = C[1] ^ (~C[2] & C[3]);
	eor	@E[3],@C[3],@E[3],ror#32-10
	str.h	@E[3],[sp,#$R[0][1]+4]
	eor	@E[0],@C[4],@E[0],ror#32-7
	eor	@E[1],@C[5],@E[1],ror#32-7
	str.l	@E[0],[sp,#$R[0][2]]		@ R[0][2] = C[2] ^ (~C[3] & C[4]);
	bic	@E[2],@C[0],@C[8],ror#32-7
	str.h	@E[1],[sp,#$R[0][2]+4]
	bic	@E[3],@C[1],@C[9],ror#32-7
	eor	@E[2],@E[2],@C[6],ror#32-11
	str.l	@E[2],[sp,#$R[0][3]]		@ R[0][3] = C[3] ^ (~C[4] & C[0]);
	eor	@E[3],@E[3],@C[7],ror#32-10
	str.h	@E[3],[sp,#$R[0][3]+4]
	bic	@E[0],@C[2],@C[0]
	 add	@E[3],sp,#$D[3]
	 ldr.l	@C[0],[sp,#$A[0][3]]		@ A[0][3]
	bic	@E[1],@C[3],@C[1]
	 ldr.h	@C[1],[sp,#$A[0][3]+4]
	eor	@E[0],@E[0],@C[8],ror#32-7
	eor	@E[1],@E[1],@C[9],ror#32-7
	str.l	@E[0],[sp,#$R[0][4]]		@ R[0][4] = C[4] ^ (~C[0] & C[1]);
	 add	@C[9],sp,#$D[0]
	str.h	@E[1],[sp,#$R[0][4]+4]

	ldmia	@E[3],{@E[0]-@E[2],@E[3]}	@ D[3..4]
	ldmia	@C[9],{@C[6]-@C[9]}		@ D[0..1]

	ldr.l	@C[2],[sp,#$A[1][4]]		@ A[1][4]
	eor	@C[0],@C[0],@E[0]
	ldr.h	@C[3],[sp,#$A[1][4]+4]
	eor	@C[1],@C[1],@E[1]
	@ ror	@C[0],@C[0],#32-14		@ C[0] = ROL64(A[0][3] ^ D[3], rhotates[0][3]);
	ldr.l	@E[0],[sp,#$A[3][1]]		@ A[3][1]
	@ ror	@C[1],@C[1],#32-14
	ldr.h	@E[1],[sp,#$A[3][1]+4]

	eor	@C[2],@C[2],@E[2]
	ldr.l	@C[4],[sp,#$A[2][0]]		@ A[2][0]
	eor	@C[3],@C[3],@E[3]
	ldr.h	@C[5],[sp,#$A[2][0]+4]
	@ ror	@C[2],@C[2],#32-10		@ C[1] = ROL64(A[1][4] ^ D[4], rhotates[1][4]);
	@ ror	@C[3],@C[3],#32-10

	eor	@C[6],@C[6],@C[4]
	ldr.l	@E[2],[sp,#$D[2]]		@ D[2]
	eor	@C[7],@C[7],@C[5]
	ldr.h	@E[3],[sp,#$D[2]+4]
	ror	@C[5],@C[6],#32-1		@ C[2] = ROL64(A[2][0] ^ D[0], rhotates[2][0]);
	ror	@C[4],@C[7],#32-2

	eor	@E[0],@E[0],@C[8]
	ldr.l	@C[8],[sp,#$A[4][2]]		@ A[4][2]
	eor	@E[1],@E[1],@C[9]
	ldr.h	@C[9],[sp,#$A[4][2]+4]
	ror	@C[7],@E[0],#32-22		@ C[3] = ROL64(A[3][1] ^ D[1], rhotates[3][1]);
	ror	@C[6],@E[1],#32-23

	bic	@E[0],@C[4],@C[2],ror#32-10
	bic	@E[1],@C[5],@C[3],ror#32-10
	 eor	@E[2],@E[2],@C[8]
	 eor	@E[3],@E[3],@C[9]
	 ror	@C[9],@E[2],#32-30		@ C[4] = ROL64(A[4][2] ^ D[2], rhotates[4][2]);
	 ror	@C[8],@E[3],#32-31
	eor	@E[0],@E[0],@C[0],ror#32-14
	eor	@E[1],@E[1],@C[1],ror#32-14
	str.l	@E[0],[sp,#$R[1][0]]		@ R[1][0] = C[0] ^ (~C[1] & C[2])
	bic	@E[2],@C[6],@C[4]
	str.h	@E[1],[sp,#$R[1][0]+4]
	bic	@E[3],@C[7],@C[5]
	eor	@E[2],@E[2],@C[2],ror#32-10
	str.l	@E[2],[sp,#$R[1][1]]		@ R[1][1] = C[1] ^ (~C[2] & C[3]);
	eor	@E[3],@E[3],@C[3],ror#32-10
	str.h	@E[3],[sp,#$R[1][1]+4]
	bic	@E[0],@C[8],@C[6]
	bic	@E[1],@C[9],@C[7]
	bic	@E[2],@C[0],@C[8],ror#14
	bic	@E[3],@C[1],@C[9],ror#14
	eor	@E[0],@E[0],@C[4]
	eor	@E[1],@E[1],@C[5]
	str.l	@E[0],[sp,#$R[1][2]]		@ R[1][2] = C[2] ^ (~C[3] & C[4]);
	bic	@C[2],@C[2],@C[0],ror#32-(14-10)
	str.h	@E[1],[sp,#$R[1][2]+4]
	eor	@E[2],@C[6],@E[2],ror#32-14
	bic	@E[1],@C[3],@C[1],ror#32-(14-10)
	str.l	@E[2],[sp,#$R[1][3]]		@ R[1][3] = C[3] ^ (~C[4] & C[0]);
	eor	@E[3],@C[7],@E[3],ror#32-14
	str.h	@E[3],[sp,#$R[1][3]+4]
	 add	@E[2],sp,#$D[1]
	 ldr.l	@C[1],[sp,#$A[0][1]]		@ A[0][1]
	eor	@E[0],@C[8],@C[2],ror#32-10
	 ldr.h	@C[0],[sp,#$A[0][1]+4]
	eor	@E[1],@C[9],@E[1],ror#32-10
	str.l	@E[0],[sp,#$R[1][4]]		@ R[1][4] = C[4] ^ (~C[0] & C[1]);
	str.h	@E[1],[sp,#$R[1][4]+4]

	add	@C[9],sp,#$D[3]
	ldmia	@E[2],{@E[0]-@E[2],@E[3]}	@ D[1..2]
	ldr.l	@C[2],[sp,#$A[1][2]]		@ A[1][2]
	ldr.h	@C[3],[sp,#$A[1][2]+4]
	ldmia	@C[9],{@C[6]-@C[9]}		@ D[3..4]

	eor	@C[1],@C[1],@E[0]
	ldr.l	@C[4],[sp,#$A[2][3]]		@ A[2][3]
	eor	@C[0],@C[0],@E[1]
	ldr.h	@C[5],[sp,#$A[2][3]+4]
	ror	@C[0],@C[0],#32-1		@ C[0] = ROL64(A[0][1] ^ D[1], rhotates[0][1]);

	eor	@C[2],@C[2],@E[2]
	ldr.l	@E[0],[sp,#$A[3][4]]		@ A[3][4]
	eor	@C[3],@C[3],@E[3]
	ldr.h	@E[1],[sp,#$A[3][4]+4]
	@ ror	@C[2],@C[2],#32-3		@ C[1] = ROL64(A[1][2] ^ D[2], rhotates[1][2]);
	ldr.l	@E[2],[sp,#$D[0]]		@ D[0]
	@ ror	@C[3],@C[3],#32-3
	ldr.h	@E[3],[sp,#$D[0]+4]

	eor	@C[4],@C[4],@C[6]
	eor	@C[5],@C[5],@C[7]
	@ ror	@C[5],@C[6],#32-12		@ C[2] = ROL64(A[2][3] ^ D[3], rhotates[2][3]);
	@ ror	@C[4],@C[7],#32-13		@ [track reverse order below]

	eor	@E[0],@E[0],@C[8]
	ldr.l	@C[8],[sp,#$A[4][0]]		@ A[4][0]
	eor	@E[1],@E[1],@C[9]
	ldr.h	@C[9],[sp,#$A[4][0]+4]
	ror	@C[6],@E[0],#32-4		@ C[3] = ROL64(A[3][4] ^ D[4], rhotates[3][4]);
	ror	@C[7],@E[1],#32-4

	eor	@E[2],@E[2],@C[8]
	eor	@E[3],@E[3],@C[9]
	ror	@C[8],@E[2],#32-9		@ C[4] = ROL64(A[4][0] ^ D[0], rhotates[4][0]);
	ror	@C[9],@E[3],#32-9

	bic	@E[0],@C[5],@C[2],ror#13-3
	bic	@E[1],@C[4],@C[3],ror#12-3
	bic	@E[2],@C[6],@C[5],ror#32-13
	bic	@E[3],@C[7],@C[4],ror#32-12
	eor	@E[0],@C[0],@E[0],ror#32-13
	eor	@E[1],@C[1],@E[1],ror#32-12
	str.l	@E[0],[sp,#$R[2][0]]		@ R[2][0] = C[0] ^ (~C[1] & C[2])
	eor	@E[2],@E[2],@C[2],ror#32-3
	str.h	@E[1],[sp,#$R[2][0]+4]
	eor	@E[3],@E[3],@C[3],ror#32-3
	str.l	@E[2],[sp,#$R[2][1]]		@ R[2][1] = C[1] ^ (~C[2] & C[3]);
	bic	@E[0],@C[8],@C[6]
	bic	@E[1],@C[9],@C[7]
	str.h	@E[3],[sp,#$R[2][1]+4]
	eor	@E[0],@E[0],@C[5],ror#32-13
	eor	@E[1],@E[1],@C[4],ror#32-12
	str.l	@E[0],[sp,#$R[2][2]]		@ R[2][2] = C[2] ^ (~C[3] & C[4]);
	bic	@E[2],@C[0],@C[8]
	str.h	@E[1],[sp,#$R[2][2]+4]
	bic	@E[3],@C[1],@C[9]
	eor	@E[2],@E[2],@C[6]
	eor	@E[3],@E[3],@C[7]
	str.l	@E[2],[sp,#$R[2][3]]		@ R[2][3] = C[3] ^ (~C[4] & C[0]);
	bic	@E[0],@C[2],@C[0],ror#3
	str.h	@E[3],[sp,#$R[2][3]+4]
	bic	@E[1],@C[3],@C[1],ror#3
	 ldr.l	@C[1],[sp,#$A[0][4]]		@ A[0][4] [in reverse order]
	eor	@E[0],@C[8],@E[0],ror#32-3
	 ldr.h	@C[0],[sp,#$A[0][4]+4]
	eor	@E[1],@C[9],@E[1],ror#32-3
	str.l	@E[0],[sp,#$R[2][4]]		@ R[2][4] = C[4] ^ (~C[0] & C[1]);
	 add	@C[9],sp,#$D[1]
	str.h	@E[1],[sp,#$R[2][4]+4]

	ldr.l	@E[0],[sp,#$D[4]]		@ D[4]
	ldr.h	@E[1],[sp,#$D[4]+4]
	ldr.l	@E[2],[sp,#$D[0]]		@ D[0]
	ldr.h	@E[3],[sp,#$D[0]+4]

	ldmia	@C[9],{@C[6]-@C[9]}		@ D[1..2]

	eor	@C[1],@C[1],@E[0]
	ldr.l	@C[2],[sp,#$A[1][0]]		@ A[1][0]
	eor	@C[0],@C[0],@E[1]
	ldr.h	@C[3],[sp,#$A[1][0]+4]
	@ ror	@C[1],@E[0],#32-13		@ C[0] = ROL64(A[0][4] ^ D[4], rhotates[0][4]);
	ldr.l	@C[4],[sp,#$A[2][1]]		@ A[2][1]
	@ ror	@C[0],@E[1],#32-14		@ [was loaded in reverse order]
	ldr.h	@C[5],[sp,#$A[2][1]+4]

	eor	@C[2],@C[2],@E[2]
	ldr.l	@E[0],[sp,#$A[3][2]]		@ A[3][2]
	eor	@C[3],@C[3],@E[3]
	ldr.h	@E[1],[sp,#$A[3][2]+4]
	@ ror	@C[2],@C[2],#32-18		@ C[1] = ROL64(A[1][0] ^ D[0], rhotates[1][0]);
	ldr.l	@E[2],[sp,#$D[3]]		@ D[3]
	@ ror	@C[3],@C[3],#32-18
	ldr.h	@E[3],[sp,#$D[3]+4]

	eor	@C[6],@C[6],@C[4]
	eor	@C[7],@C[7],@C[5]
	ror	@C[4],@C[6],#32-5		@ C[2] = ROL64(A[2][1] ^ D[1], rhotates[2][1]);
	ror	@C[5],@C[7],#32-5

	eor	@E[0],@E[0],@C[8]
	ldr.l	@C[8],[sp,#$A[4][3]]		@ A[4][3]
	eor	@E[1],@E[1],@C[9]
	ldr.h	@C[9],[sp,#$A[4][3]+4]
	ror	@C[7],@E[0],#32-7		@ C[3] = ROL64(A[3][2] ^ D[2], rhotates[3][2]);
	ror	@C[6],@E[1],#32-8

	eor	@E[2],@E[2],@C[8]
	eor	@E[3],@E[3],@C[9]
	ror	@C[8],@E[2],#32-28		@ C[4] = ROL64(A[4][3] ^ D[3], rhotates[4][3]);
	ror	@C[9],@E[3],#32-28

	bic	@E[0],@C[4],@C[2],ror#32-18
	bic	@E[1],@C[5],@C[3],ror#32-18
	eor	@E[0],@E[0],@C[0],ror#32-14
	eor	@E[1],@E[1],@C[1],ror#32-13
	str.l	@E[0],[sp,#$R[3][0]]		@ R[3][0] = C[0] ^ (~C[1] & C[2])
	bic	@E[2],@C[6],@C[4]
	str.h	@E[1],[sp,#$R[3][0]+4]
	bic	@E[3],@C[7],@C[5]
	eor	@E[2],@E[2],@C[2],ror#32-18
	str.l	@E[2],[sp,#$R[3][1]]		@ R[3][1] = C[1] ^ (~C[2] & C[3]);
	eor	@E[3],@E[3],@C[3],ror#32-18
	str.h	@E[3],[sp,#$R[3][1]+4]
	bic	@E[0],@C[8],@C[6]
	bic	@E[1],@C[9],@C[7]
	bic	@E[2],@C[0],@C[8],ror#14
	bic	@E[3],@C[1],@C[9],ror#13
	eor	@E[0],@E[0],@C[4]
	eor	@E[1],@E[1],@C[5]
	str.l	@E[0],[sp,#$R[3][2]]		@ R[3][2] = C[2] ^ (~C[3] & C[4]);
	bic	@C[2],@C[2],@C[0],ror#18-14
	str.h	@E[1],[sp,#$R[3][2]+4]
	eor	@E[2],@C[6],@E[2],ror#32-14
	bic	@E[1],@C[3],@C[1],ror#18-13
	eor	@E[3],@C[7],@E[3],ror#32-13
	str.l	@E[2],[sp,#$R[3][3]]		@ R[3][3] = C[3] ^ (~C[4] & C[0]);
	str.h	@E[3],[sp,#$R[3][3]+4]
	 add	@E[3],sp,#$D[2]
	 ldr.l	@C[0],[sp,#$A[0][2]]		@ A[0][2]
	eor	@E[0],@C[8],@C[2],ror#32-18
	 ldr.h	@C[1],[sp,#$A[0][2]+4]
	eor	@E[1],@C[9],@E[1],ror#32-18
	str.l	@E[0],[sp,#$R[3][4]]		@ R[3][4] = C[4] ^ (~C[0] & C[1]);
	str.h	@E[1],[sp,#$R[3][4]+4]

	ldmia	@E[3],{@E[0]-@E[2],@E[3]}	@ D[2..3]
	ldr.l	@C[2],[sp,#$A[1][3]]		@ A[1][3]
	ldr.h	@C[3],[sp,#$A[1][3]+4]
	ldr.l	@C[6],[sp,#$D[4]]		@ D[4]
	ldr.h	@C[7],[sp,#$D[4]+4]

	eor	@C[0],@C[0],@E[0]
	ldr.l	@C[4],[sp,#$A[2][4]]		@ A[2][4]
	eor	@C[1],@C[1],@E[1]
	ldr.h	@C[5],[sp,#$A[2][4]+4]
	@ ror	@C[0],@C[0],#32-31		@ C[0] = ROL64(A[0][2] ^ D[2], rhotates[0][2]);
	ldr.l	@C[8],[sp,#$D[0]]		@ D[0]
	@ ror	@C[1],@C[1],#32-31
	ldr.h	@C[9],[sp,#$D[0]+4]

	eor	@E[2],@E[2],@C[2]
	ldr.l	@E[0],[sp,#$A[3][0]]		@ A[3][0]
	eor	@E[3],@E[3],@C[3]
	ldr.h	@E[1],[sp,#$A[3][0]+4]
	ror	@C[3],@E[2],#32-27		@ C[1] = ROL64(A[1][3] ^ D[3], rhotates[1][3]);
	ldr.l	@E[2],[sp,#$D[1]]		@ D[1]
	ror	@C[2],@E[3],#32-28
	ldr.h	@E[3],[sp,#$D[1]+4]

	eor	@C[6],@C[6],@C[4]
	eor	@C[7],@C[7],@C[5]
	ror	@C[5],@C[6],#32-19		@ C[2] = ROL64(A[2][4] ^ D[4], rhotates[2][4]);
	ror	@C[4],@C[7],#32-20

	eor	@E[0],@E[0],@C[8]
	ldr.l	@C[8],[sp,#$A[4][1]]		@ A[4][1]
	eor	@E[1],@E[1],@C[9]
	ldr.h	@C[9],[sp,#$A[4][1]+4]
	ror	@C[7],@E[0],#32-20		@ C[3] = ROL64(A[3][0] ^ D[0], rhotates[3][0]);
	ror	@C[6],@E[1],#32-21

	eor	@C[8],@C[8],@E[2]
	eor	@C[9],@C[9],@E[3]
	@ ror	@C[8],@C[2],#32-1		@ C[4] = ROL64(A[4][1] ^ D[1], rhotates[4][1]);
	@ ror	@C[9],@C[3],#32-1

	bic	@E[0],@C[4],@C[2]
	bic	@E[1],@C[5],@C[3]
	eor	@E[0],@E[0],@C[0],ror#32-31
	str.l	@E[0],[sp,#$R[4][0]]		@ R[4][0] = C[0] ^ (~C[1] & C[2])
	eor	@E[1],@E[1],@C[1],ror#32-31
	str.h	@E[1],[sp,#$R[4][0]+4]
	bic	@E[2],@C[6],@C[4]
	bic	@E[3],@C[7],@C[5]
	eor	@E[2],@E[2],@C[2]
	eor	@E[3],@E[3],@C[3]
	str.l	@E[2],[sp,#$R[4][1]]		@ R[4][1] = C[1] ^ (~C[2] & C[3]);
	bic	@E[0],@C[8],@C[6],ror#1
	str.h	@E[3],[sp,#$R[4][1]+4]
	bic	@E[1],@C[9],@C[7],ror#1
	bic	@E[2],@C[0],@C[8],ror#31-1
	bic	@E[3],@C[1],@C[9],ror#31-1
	eor	@C[4],@C[4],@E[0],ror#32-1
	str.l	@C[4],[sp,#$R[4][2]]		@ R[4][2] = C[2] ^= (~C[3] & C[4]);
	eor	@C[5],@C[5],@E[1],ror#32-1
	str.h	@C[5],[sp,#$R[4][2]+4]
	eor	@C[6],@C[6],@E[2],ror#32-31
	eor	@C[7],@C[7],@E[3],ror#32-31
	str.l	@C[6],[sp,#$R[4][3]]		@ R[4][3] = C[3] ^= (~C[4] & C[0]);
	bic	@E[0],@C[2],@C[0],ror#32-31
	str.h	@C[7],[sp,#$R[4][3]+4]
	bic	@E[1],@C[3],@C[1],ror#32-31
	 add	@E[2],sp,#$R[0][0]
	eor	@C[8],@E[0],@C[8],ror#32-1
	 add	@E[0],sp,#$R[1][0]
	eor	@C[9],@E[1],@C[9],ror#32-1
	str.l	@C[8],[sp,#$R[4][4]]		@ R[4][4] = C[4] ^= (~C[0] & C[1]);
	str.h	@C[9],[sp,#$R[4][4]+4]
___
}
	Round(@A,@T);
	Round(@T,@A);
$code.=<<___;
	blo	.Lround2x

	ldr	pc,[sp,#440]
.size	KeccakF1600_int,.-KeccakF1600_int

.type	KeccakF1600, %function
.align	5
KeccakF1600:
	stmdb	sp!,{r0,r4-r11,lr}
	sub	sp,sp,#440+16			@ space for A[5][5],D[5],T[5][5],...

	add	@E[0],r0,#$A[1][0]
	add	@E[1],sp,#$A[1][0]
	ldmia	r0,    {@C[0]-@C[9]}		@ copy A[5][5] to stack
	stmia	sp,    {@C[0]-@C[9]}
	ldmia	@E[0]!,{@C[0]-@C[9]}
	stmia	@E[1]!,{@C[0]-@C[9]}
	ldmia	@E[0]!,{@C[0]-@C[9]}
	stmia	@E[1]!,{@C[0]-@C[9]}
	ldmia	@E[0]!,{@C[0]-@C[9]}
	stmia	@E[1]!,{@C[0]-@C[9]}
	ldmia	@E[0], {@C[0]-@C[9]}
	add	@E[2],sp,#$A[0][0]
	add	@E[0],sp,#$A[1][0]
	stmia	@E[1], {@C[0]-@C[9]}

	bl	KeccakF1600_enter

	ldr	@E[1], [sp,#440+16]		@ restore pointer to A
	ldmia	sp,    {@C[0]-@C[9]}
	stmia	@E[1]!,{@C[0]-@C[9]}		@ return A[5][5]
	ldmia	@E[0]!,{@C[0]-@C[9]}
	stmia	@E[1]!,{@C[0]-@C[9]}
	ldmia	@E[0]!,{@C[0]-@C[9]}
	stmia	@E[1]!,{@C[0]-@C[9]}
	ldmia	@E[0]!,{@C[0]-@C[9]}
	stmia	@E[1]!,{@C[0]-@C[9]}
	ldmia	@E[0], {@C[0]-@C[9]}
	stmia	@E[1], {@C[0]-@C[9]}

	add	sp,sp,#440+20
	ldmia	sp!,{r4-r11,pc}
.size	KeccakF1600,.-KeccakF1600
___
{ my ($A_flat,$inp,$len,$bsz) = map("r$_",(10..12,14));

########################################################################
# Stack layout
# ----->+-----------------------+
#       | uint64_t A[5][5]      |
#       | ...                   |
#       | ...                   |
# +456->+-----------------------+
#       | 0x55555555            |
# +460->+-----------------------+
#       | 0x33333333            |
# +464->+-----------------------+
#       | 0x0f0f0f0f            |
# +468->+-----------------------+
#       | 0x00ff00ff            |
# +472->+-----------------------+
#       | uint64_t *A           |
# +476->+-----------------------+
#       | const void *inp       |
# +480->+-----------------------+
#       | size_t len            |
# +484->+-----------------------+
#       | size_t bs             |
# +488->+-----------------------+
#       | ....

$code.=<<___;
.global	SHA3_absorb
.type	SHA3_absorb,%function
.align	5
SHA3_absorb:
	stmdb	sp!,{r0-r12,lr}
	sub	sp,sp,#456+16

	add	$A_flat,r0,#$A[1][0]
	@ mov	$inp,r1
	mov	$len,r2
	mov	$bsz,r3
	cmp	r2,r3
	blo	.Labsorb_abort

	add	$inp,sp,#0
	ldmia	r0,      {@C[0]-@C[9]}	@ copy A[5][5] to stack
	stmia	$inp!,   {@C[0]-@C[9]}
	ldmia	$A_flat!,{@C[0]-@C[9]}
	stmia	$inp!,   {@C[0]-@C[9]}
	ldmia	$A_flat!,{@C[0]-@C[9]}
	stmia	$inp!,   {@C[0]-@C[9]}
	ldmia	$A_flat!,{@C[0]-@C[9]}
	stmia	$inp!,   {@C[0]-@C[9]}
	ldmia	$A_flat!,{@C[0]-@C[9]}
	stmia	$inp,    {@C[0]-@C[9]}

	ldr	$inp,[sp,#476]		@ restore $inp
#ifdef	__thumb2__
	mov	r9,#0x00ff00ff
	mov	r8,#0x0f0f0f0f
	mov	r7,#0x33333333
	mov	r6,#0x55555555
#else
	mov	r6,#0x11		@ compose constants
	mov	r8,#0x0f
	mov	r9,#0xff
	orr	r6,r6,r6,lsl#8
	orr	r8,r8,r8,lsl#8
	orr	r6,r6,r6,lsl#16		@ 0x11111111
	orr	r9,r9,r9,lsl#16		@ 0x00ff00ff
	orr	r8,r8,r8,lsl#16		@ 0x0f0f0f0f
	orr	r7,r6,r6,lsl#1		@ 0x33333333
	orr	r6,r6,r6,lsl#2		@ 0x55555555
#endif
	str	r9,[sp,#468]
	str	r8,[sp,#464]
	str	r7,[sp,#460]
	str	r6,[sp,#456]
	b	.Loop_absorb

.align	4
.Loop_absorb:
	subs	r0,$len,$bsz
	blo	.Labsorbed
	add	$A_flat,sp,#0
	str	r0,[sp,#480]		@ save len - bsz

.align	4
.Loop_block:
	ldrb	r0,[$inp],#1
	ldrb	r1,[$inp],#1
	ldrb	r2,[$inp],#1
	ldrb	r3,[$inp],#1
	ldrb	r4,[$inp],#1
	orr	r0,r0,r1,lsl#8
	ldrb	r1,[$inp],#1
	orr	r0,r0,r2,lsl#16
	ldrb	r2,[$inp],#1
	orr	r0,r0,r3,lsl#24		@ lo
	ldrb	r3,[$inp],#1
	orr	r1,r4,r1,lsl#8
	orr	r1,r1,r2,lsl#16
	orr	r1,r1,r3,lsl#24		@ hi

	and	r2,r0,r6		@ &=0x55555555
	and	r0,r0,r6,lsl#1		@ &=0xaaaaaaaa
	and	r3,r1,r6		@ &=0x55555555
	and	r1,r1,r6,lsl#1		@ &=0xaaaaaaaa
	orr	r2,r2,r2,lsr#1
	orr	r0,r0,r0,lsl#1
	orr	r3,r3,r3,lsr#1
	orr	r1,r1,r1,lsl#1
	and	r2,r2,r7		@ &=0x33333333
	and	r0,r0,r7,lsl#2		@ &=0xcccccccc
	and	r3,r3,r7		@ &=0x33333333
	and	r1,r1,r7,lsl#2		@ &=0xcccccccc
	orr	r2,r2,r2,lsr#2
	orr	r0,r0,r0,lsl#2
	orr	r3,r3,r3,lsr#2
	orr	r1,r1,r1,lsl#2
	and	r2,r2,r8		@ &=0x0f0f0f0f
	and	r0,r0,r8,lsl#4		@ &=0xf0f0f0f0
	and	r3,r3,r8		@ &=0x0f0f0f0f
	and	r1,r1,r8,lsl#4		@ &=0xf0f0f0f0
	ldmia	$A_flat,{r4-r5}		@ A_flat[i]
	orr	r2,r2,r2,lsr#4
	orr	r0,r0,r0,lsl#4
	orr	r3,r3,r3,lsr#4
	orr	r1,r1,r1,lsl#4
	and	r2,r2,r9		@ &=0x00ff00ff
	and	r0,r0,r9,lsl#8		@ &=0xff00ff00
	and	r3,r3,r9		@ &=0x00ff00ff
	and	r1,r1,r9,lsl#8		@ &=0xff00ff00
	orr	r2,r2,r2,lsr#8
	orr	r0,r0,r0,lsl#8
	orr	r3,r3,r3,lsr#8
	orr	r1,r1,r1,lsl#8

	lsl	r2,r2,#16
	lsr	r1,r1,#16
	eor	r4,r4,r3,lsl#16
	eor	r5,r5,r0,lsr#16
	eor	r4,r4,r2,lsr#16
	eor	r5,r5,r1,lsl#16
	stmia	$A_flat!,{r4-r5}	@ A_flat[i++] ^= BitInterleave(inp[0..7])

	subs	$bsz,$bsz,#8
	bhi	.Loop_block

	str	$inp,[sp,#476]

	bl	KeccakF1600_int

	add	r14,sp,#456
	ldmia	r14,{r6-r12,r14}	@ restore constants and variables
	b	.Loop_absorb

.align	4
.Labsorbed:
	add	$inp,sp,#$A[1][0]
	ldmia	sp,      {@C[0]-@C[9]}
	stmia	$A_flat!,{@C[0]-@C[9]}	@ return A[5][5]
	ldmia	$inp!,   {@C[0]-@C[9]}
	stmia	$A_flat!,{@C[0]-@C[9]}
	ldmia	$inp!,   {@C[0]-@C[9]}
	stmia	$A_flat!,{@C[0]-@C[9]}
	ldmia	$inp!,   {@C[0]-@C[9]}
	stmia	$A_flat!,{@C[0]-@C[9]}
	ldmia	$inp,    {@C[0]-@C[9]}
	stmia	$A_flat, {@C[0]-@C[9]}

.Labsorb_abort:
	add	sp,sp,#456+32
	mov	r0,$len			@ return value
	ldmia	sp!,{r4-r12,pc}
.size	SHA3_absorb,.-SHA3_absorb
___
}
{ my ($out,$len,$A_flat,$bsz) = map("r$_", (4,5,10,12));

$code.=<<___;
.global	SHA3_squeeze
.type	SHA3_squeeze,%function
.align	5
SHA3_squeeze:
	stmdb	sp!,{r0,r3-r10,lr}

	mov	$A_flat,r0
	mov	$out,r1
	mov	$len,r2
	mov	$bsz,r3

#ifdef	__thumb2__
	mov	r9,#0x00ff00ff
	mov	r8,#0x0f0f0f0f
	mov	r7,#0x33333333
	mov	r6,#0x55555555
#else
	mov	r6,#0x11		@ compose constants
	mov	r8,#0x0f
	mov	r9,#0xff
	orr	r6,r6,r6,lsl#8
	orr	r8,r8,r8,lsl#8
	orr	r6,r6,r6,lsl#16		@ 0x11111111
	orr	r9,r9,r9,lsl#16		@ 0x00ff00ff
	orr	r8,r8,r8,lsl#16		@ 0x0f0f0f0f
	orr	r7,r6,r6,lsl#1		@ 0x33333333
	orr	r6,r6,r6,lsl#2		@ 0x55555555
#endif
	stmdb	sp!,{r6-r9}

	mov	r14,$A_flat
	b	.Loop_squeeze

.align	4
.Loop_squeeze:
	ldmia	$A_flat!,{r0,r1}	@ A_flat[i++]

	lsl	r2,r0,#16
	lsl	r3,r1,#16		@ r3 = r1 << 16
	lsr	r2,r2,#16		@ r2 = r0 & 0x0000ffff
	lsr	r1,r1,#16
	lsr	r0,r0,#16		@ r0 = r0 >> 16
	lsl	r1,r1,#16		@ r1 = r1 & 0xffff0000

	orr	r2,r2,r2,lsl#8
	orr	r3,r3,r3,lsr#8
	orr	r0,r0,r0,lsl#8
	orr	r1,r1,r1,lsr#8
	and	r2,r2,r9		@ &=0x00ff00ff
	and	r3,r3,r9,lsl#8		@ &=0xff00ff00
	and	r0,r0,r9		@ &=0x00ff00ff
	and	r1,r1,r9,lsl#8		@ &=0xff00ff00
	orr	r2,r2,r2,lsl#4
	orr	r3,r3,r3,lsr#4
	orr	r0,r0,r0,lsl#4
	orr	r1,r1,r1,lsr#4
	and	r2,r2,r8		@ &=0x0f0f0f0f
	and	r3,r3,r8,lsl#4		@ &=0xf0f0f0f0
	and	r0,r0,r8		@ &=0x0f0f0f0f
	and	r1,r1,r8,lsl#4		@ &=0xf0f0f0f0
	orr	r2,r2,r2,lsl#2
	orr	r3,r3,r3,lsr#2
	orr	r0,r0,r0,lsl#2
	orr	r1,r1,r1,lsr#2
	and	r2,r2,r7		@ &=0x33333333
	and	r3,r3,r7,lsl#2		@ &=0xcccccccc
	and	r0,r0,r7		@ &=0x33333333
	and	r1,r1,r7,lsl#2		@ &=0xcccccccc
	orr	r2,r2,r2,lsl#1
	orr	r3,r3,r3,lsr#1
	orr	r0,r0,r0,lsl#1
	orr	r1,r1,r1,lsr#1
	and	r2,r2,r6		@ &=0x55555555
	and	r3,r3,r6,lsl#1		@ &=0xaaaaaaaa
	and	r0,r0,r6		@ &=0x55555555
	and	r1,r1,r6,lsl#1		@ &=0xaaaaaaaa

	orr	r2,r2,r3
	orr	r0,r0,r1

	cmp	$len,#8
	blo	.Lsqueeze_tail
	lsr	r1,r2,#8
	strb	r2,[$out],#1
	lsr	r3,r2,#16
	strb	r1,[$out],#1
	lsr	r2,r2,#24
	strb	r3,[$out],#1
	strb	r2,[$out],#1

	lsr	r1,r0,#8
	strb	r0,[$out],#1
	lsr	r3,r0,#16
	strb	r1,[$out],#1
	lsr	r0,r0,#24
	strb	r3,[$out],#1
	strb	r0,[$out],#1
	subs	$len,$len,#8
	beq	.Lsqueeze_done

	subs	$bsz,$bsz,#8		@ bsz -= 8
	bhi	.Loop_squeeze

	mov	r0,r14			@ original $A_flat

	bl	KeccakF1600

	ldmia	sp,{r6-r10,r12}		@ restore constants and variables
	mov	r14,$A_flat
	b	.Loop_squeeze

.align	4
.Lsqueeze_tail:
	strb	r2,[$out],#1
	lsr	r2,r2,#8
	subs	$len,$len,#1
	beq	.Lsqueeze_done
	strb	r2,[$out],#1
	lsr	r2,r2,#8
	subs	$len,$len,#1
	beq	.Lsqueeze_done
	strb	r2,[$out],#1
	lsr	r2,r2,#8
	subs	$len,$len,#1
	beq	.Lsqueeze_done
	strb	r2,[$out],#1
	subs	$len,$len,#1
	beq	.Lsqueeze_done

	strb	r0,[$out],#1
	lsr	r0,r0,#8
	subs	$len,$len,#1
	beq	.Lsqueeze_done
	strb	r0,[$out],#1
	lsr	r0,r0,#8
	subs	$len,$len,#1
	beq	.Lsqueeze_done
	strb	r0,[$out]
	b	.Lsqueeze_done

.align	4
.Lsqueeze_done:
	add	sp,sp,#24
	ldmia	sp!,{r4-r10,pc}
.size	SHA3_squeeze,.-SHA3_squeeze
___
}

$code.=<<___;
#if __ARM_MAX_ARCH__>=7
.fpu	neon

.type	iotas64, %object
.align 5
iotas64:
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
.size	iotas64,.-iotas64

.type	KeccakF1600_neon, %function
.align	5
KeccakF1600_neon:
	add	r1, r0, #16
	adr	r2, iotas64
	mov	r3, #24			@ loop counter
	b	.Loop_neon

.align	4
.Loop_neon:
	@ Theta
	vst1.64		{q4},  [r0:64]		@ offload A[0..1][4]
	veor		q13, q0,  q5		@ A[0..1][0]^A[2..3][0]
	vst1.64		{d18}, [r1:64]		@ offload A[2][4]
	veor		q14, q1,  q6		@ A[0..1][1]^A[2..3][1]
	veor		q15, q2,  q7		@ A[0..1][2]^A[2..3][2]
	veor		d26, d26, d27		@ C[0]=A[0][0]^A[1][0]^A[2][0]^A[3][0]
	veor		d27, d28, d29		@ C[1]=A[0][1]^A[1][1]^A[2][1]^A[3][1]
	veor		q14, q3,  q8		@ A[0..1][3]^A[2..3][3]
	veor		q4,  q4,  q9		@ A[0..1][4]^A[2..3][4]
	veor		d30, d30, d31		@ C[2]=A[0][2]^A[1][2]^A[2][2]^A[3][2]
	veor		d31, d28, d29		@ C[3]=A[0][3]^A[1][3]^A[2][3]^A[3][3]
	veor		d25, d8,  d9		@ C[4]=A[0][4]^A[1][4]^A[2][4]^A[3][4]
	veor		q13, q13, q10		@ C[0..1]^=A[4][0..1]
	veor		q14, q15, q11		@ C[2..3]^=A[4][2..3]
	veor		d25, d25, d24		@ C[4]^=A[4][4]

	vadd.u64	q4,  q13, q13		@ C[0..1]<<1
	vadd.u64	q15, q14, q14		@ C[2..3]<<1
	vadd.u64	d18, d25, d25		@ C[4]<<1
	vsri.u64	q4,  q13, #63		@ ROL64(C[0..1],1)
	vsri.u64	q15, q14, #63		@ ROL64(C[2..3],1)
	vsri.u64	d18, d25, #63		@ ROL64(C[4],1)
	veor		d25, d25, d9		@ D[0] = C[4] ^= ROL64(C[1],1)
	veor		q13, q13, q15		@ D[1..2] = C[0..1] ^ ROL64(C[2..3],1)
	veor		d28, d28, d18		@ D[3] = C[2] ^= ROL64(C[4],1)
	veor		d29, d29, d8		@ D[4] = C[3] ^= ROL64(C[0],1)

	veor		d0,  d0,  d25		@ A[0][0] ^= C[4]
	veor		d1,  d1,  d25		@ A[1][0] ^= C[4]
	veor		d10, d10, d25		@ A[2][0] ^= C[4]
	veor		d11, d11, d25		@ A[3][0] ^= C[4]
	veor		d20, d20, d25		@ A[4][0] ^= C[4]

	veor		d2,  d2,  d26		@ A[0][1] ^= D[1]
	veor		d3,  d3,  d26		@ A[1][1] ^= D[1]
	veor		d12, d12, d26		@ A[2][1] ^= D[1]
	veor		d13, d13, d26		@ A[3][1] ^= D[1]
	veor		d21, d21, d26		@ A[4][1] ^= D[1]
	vmov		d26, d27

	veor		d6,  d6,  d28		@ A[0][3] ^= C[2]
	veor		d7,  d7,  d28		@ A[1][3] ^= C[2]
	veor		d16, d16, d28		@ A[2][3] ^= C[2]
	veor		d17, d17, d28		@ A[3][3] ^= C[2]
	veor		d23, d23, d28		@ A[4][3] ^= C[2]
	vld1.64		{q4},  [r0:64]		@ restore A[0..1][4]
	vmov		d28, d29

	vld1.64		{d18}, [r1:64]		@ restore A[2][4]
	veor		q2,  q2,  q13		@ A[0..1][2] ^= D[2]
	veor		q7,  q7,  q13		@ A[2..3][2] ^= D[2]
	veor		d22, d22, d27		@ A[4][2]    ^= D[2]

	veor		q4,  q4,  q14		@ A[0..1][4] ^= C[3]
	veor		q9,  q9,  q14		@ A[2..3][4] ^= C[3]
	veor		d24, d24, d29		@ A[4][4]    ^= C[3]

	@ Rho + Pi
	vmov		d26, d2			@ C[1] = A[0][1]
	vshl.u64	d2,  d3,  #44
	vmov		d27, d4			@ C[2] = A[0][2]
	vshl.u64	d4,  d14, #43
	vmov		d28, d6			@ C[3] = A[0][3]
	vshl.u64	d6,  d17, #21
	vmov		d29, d8			@ C[4] = A[0][4]
	vshl.u64	d8,  d24, #14
	vsri.u64	d2,  d3,  #64-44	@ A[0][1] = ROL64(A[1][1], rhotates[1][1])
	vsri.u64	d4,  d14, #64-43	@ A[0][2] = ROL64(A[2][2], rhotates[2][2])
	vsri.u64	d6,  d17, #64-21	@ A[0][3] = ROL64(A[3][3], rhotates[3][3])
	vsri.u64	d8,  d24, #64-14	@ A[0][4] = ROL64(A[4][4], rhotates[4][4])

	vshl.u64	d3,  d9,  #20
	vshl.u64	d14, d16, #25
	vshl.u64	d17, d15, #15
	vshl.u64	d24, d21, #2
	vsri.u64	d3,  d9,  #64-20	@ A[1][1] = ROL64(A[1][4], rhotates[1][4])
	vsri.u64	d14, d16, #64-25	@ A[2][2] = ROL64(A[2][3], rhotates[2][3])
	vsri.u64	d17, d15, #64-15	@ A[3][3] = ROL64(A[3][2], rhotates[3][2])
	vsri.u64	d24, d21, #64-2		@ A[4][4] = ROL64(A[4][1], rhotates[4][1])

	vshl.u64	d9,  d22, #61
	@ vshl.u64	d16, d19, #8
	vshl.u64	d15, d12, #10
	vshl.u64	d21, d7,  #55
	vsri.u64	d9,  d22, #64-61	@ A[1][4] = ROL64(A[4][2], rhotates[4][2])
	vext.8		d16, d19, d19, #8-1	@ A[2][3] = ROL64(A[3][4], rhotates[3][4])
	vsri.u64	d15, d12, #64-10	@ A[3][2] = ROL64(A[2][1], rhotates[2][1])
	vsri.u64	d21, d7,  #64-55	@ A[4][1] = ROL64(A[1][3], rhotates[1][3])

	vshl.u64	d22, d18, #39
	@ vshl.u64	d19, d23, #56
	vshl.u64	d12, d5,  #6
	vshl.u64	d7,  d13, #45
	vsri.u64	d22, d18, #64-39	@ A[4][2] = ROL64(A[2][4], rhotates[2][4])
	vext.8		d19, d23, d23, #8-7	@ A[3][4] = ROL64(A[4][3], rhotates[4][3])
	vsri.u64	d12, d5,  #64-6		@ A[2][1] = ROL64(A[1][2], rhotates[1][2])
	vsri.u64	d7,  d13, #64-45	@ A[1][3] = ROL64(A[3][1], rhotates[3][1])

	vshl.u64	d18, d20, #18
	vshl.u64	d23, d11, #41
	vshl.u64	d5,  d10, #3
	vshl.u64	d13, d1,  #36
	vsri.u64	d18, d20, #64-18	@ A[2][4] = ROL64(A[4][0], rhotates[4][0])
	vsri.u64	d23, d11, #64-41	@ A[4][3] = ROL64(A[3][0], rhotates[3][0])
	vsri.u64	d5,  d10, #64-3		@ A[1][2] = ROL64(A[2][0], rhotates[2][0])
	vsri.u64	d13, d1,  #64-36	@ A[3][1] = ROL64(A[1][0], rhotates[1][0])

	vshl.u64	d1,  d28, #28
	vshl.u64	d10, d26, #1
	vshl.u64	d11, d29, #27
	vshl.u64	d20, d27, #62
	vsri.u64	d1,  d28, #64-28	@ A[1][0] = ROL64(C[3],    rhotates[0][3])
	vsri.u64	d10, d26, #64-1		@ A[2][0] = ROL64(C[1],    rhotates[0][1])
	vsri.u64	d11, d29, #64-27	@ A[3][0] = ROL64(C[4],    rhotates[0][4])
	vsri.u64	d20, d27, #64-62	@ A[4][0] = ROL64(C[2],    rhotates[0][2])

	@ Chi + Iota
	vbic		q13, q2,  q1
	vbic		q14, q3,  q2
	vbic		q15, q4,  q3
	veor		q13, q13, q0		@ A[0..1][0] ^ (~A[0..1][1] & A[0..1][2])
	veor		q14, q14, q1		@ A[0..1][1] ^ (~A[0..1][2] & A[0..1][3])
	veor		q2,  q2,  q15		@ A[0..1][2] ^= (~A[0..1][3] & A[0..1][4])
	vst1.64		{q13}, [r0:64]		@ offload A[0..1][0]
	vbic		q13, q0,  q4
	vbic		q15, q1,  q0
	vmov		q1,  q14		@ A[0..1][1]
	veor		q3,  q3,  q13		@ A[0..1][3] ^= (~A[0..1][4] & A[0..1][0])
	veor		q4,  q4,  q15		@ A[0..1][4] ^= (~A[0..1][0] & A[0..1][1])

	vbic		q13, q7,  q6
	vmov		q0,  q5			@ A[2..3][0]
	vbic		q14, q8,  q7
	vmov		q15, q6			@ A[2..3][1]
	veor		q5,  q5,  q13		@ A[2..3][0] ^= (~A[2..3][1] & A[2..3][2])
	vbic		q13, q9,  q8
	veor		q6,  q6,  q14		@ A[2..3][1] ^= (~A[2..3][2] & A[2..3][3])
	vbic		q14, q0,  q9
	veor		q7,  q7,  q13		@ A[2..3][2] ^= (~A[2..3][3] & A[2..3][4])
	vbic		q13, q15, q0
	veor		q8,  q8,  q14		@ A[2..3][3] ^= (~A[2..3][4] & A[2..3][0])
	vmov		q14, q10		@ A[4][0..1]
	veor		q9,  q9,  q13		@ A[2..3][4] ^= (~A[2..3][0] & A[2..3][1])

	vld1.64		d25, [r2:64]!		@ Iota[i++]
	vbic		d26, d22, d21
	vbic		d27, d23, d22
	vld1.64		{q0}, [r0:64]		@ restore A[0..1][0]
	veor		d20, d20, d26		@ A[4][0] ^= (~A[4][1] & A[4][2])
	vbic		d26, d24, d23
	veor		d21, d21, d27		@ A[4][1] ^= (~A[4][2] & A[4][3])
	vbic		d27, d28, d24
	veor		d22, d22, d26		@ A[4][2] ^= (~A[4][3] & A[4][4])
	vbic		d26, d29, d28
	veor		d23, d23, d27		@ A[4][3] ^= (~A[4][4] & A[4][0])
	veor		d0,  d0,  d25		@ A[0][0] ^= Iota[i]
	veor		d24, d24, d26		@ A[4][4] ^= (~A[4][0] & A[4][1])

	subs	r3, r3, #1
	bne	.Loop_neon

	bx	lr
.size	KeccakF1600_neon,.-KeccakF1600_neon

.global	SHA3_absorb_neon
.type	SHA3_absorb_neon, %function
.align	5
SHA3_absorb_neon:
	stmdb	sp!, {r4-r6,lr}
	vstmdb	sp!, {d8-d15}

	mov	r4, r1			@ inp
	mov	r5, r2			@ len
	mov	r6, r3			@ bsz

	vld1.32	{d0}, [r0:64]!		@ A[0][0]
	vld1.32	{d2}, [r0:64]!		@ A[0][1]
	vld1.32	{d4}, [r0:64]!		@ A[0][2]
	vld1.32	{d6}, [r0:64]!		@ A[0][3]
	vld1.32	{d8}, [r0:64]!		@ A[0][4]

	vld1.32	{d1}, [r0:64]!		@ A[1][0]
	vld1.32	{d3}, [r0:64]!		@ A[1][1]
	vld1.32	{d5}, [r0:64]!		@ A[1][2]
	vld1.32	{d7}, [r0:64]!		@ A[1][3]
	vld1.32	{d9}, [r0:64]!		@ A[1][4]

	vld1.32	{d10}, [r0:64]!		@ A[2][0]
	vld1.32	{d12}, [r0:64]!		@ A[2][1]
	vld1.32	{d14}, [r0:64]!		@ A[2][2]
	vld1.32	{d16}, [r0:64]!		@ A[2][3]
	vld1.32	{d18}, [r0:64]!		@ A[2][4]

	vld1.32	{d11}, [r0:64]!		@ A[3][0]
	vld1.32	{d13}, [r0:64]!		@ A[3][1]
	vld1.32	{d15}, [r0:64]!		@ A[3][2]
	vld1.32	{d17}, [r0:64]!		@ A[3][3]
	vld1.32	{d19}, [r0:64]!		@ A[3][4]

	vld1.32	{d20-d23}, [r0:64]!	@ A[4][0..3]
	vld1.32	{d24}, [r0:64]		@ A[4][4]
	sub	r0, r0, #24*8		@ rewind
	b	.Loop_absorb_neon

.align	4
.Loop_absorb_neon:
	subs	r12, r5, r6		@ len - bsz
	blo	.Labsorbed_neon
	mov	r5, r12

	vld1.8	{d31}, [r4]!		@ endian-neutral loads...
	cmp	r6, #8*2
	veor	d0, d0, d31		@ A[0][0] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d2, d2, d31		@ A[0][1] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*4
	veor	d4, d4, d31		@ A[0][2] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d6, d6, d31		@ A[0][3] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31},[r4]!
	cmp	r6, #8*6
	veor	d8, d8, d31		@ A[0][4] ^= *inp++
	blo	.Lprocess_neon

	vld1.8	{d31}, [r4]!
	veor	d1, d1, d31		@ A[1][0] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*8
	veor	d3, d3, d31		@ A[1][1] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d5, d5, d31		@ A[1][2] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*10
	veor	d7, d7, d31		@ A[1][3] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d9, d9, d31		@ A[1][4] ^= *inp++
	beq	.Lprocess_neon

	vld1.8	{d31}, [r4]!
	cmp	r6, #8*12
	veor	d10, d10, d31		@ A[2][0] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d12, d12, d31		@ A[2][1] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*14
	veor	d14, d14, d31		@ A[2][2] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d16, d16, d31		@ A[2][3] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*16
	veor	d18, d18, d31		@ A[2][4] ^= *inp++
	blo	.Lprocess_neon

	vld1.8	{d31}, [r4]!
	veor	d11, d11, d31		@ A[3][0] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*18
	veor	d13, d13, d31		@ A[3][1] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d15, d15, d31		@ A[3][2] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*20
	veor	d17, d17, d31		@ A[3][3] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d19, d19, d31		@ A[3][4] ^= *inp++
	beq	.Lprocess_neon

	vld1.8	{d31}, [r4]!
	cmp	r6, #8*22
	veor	d20, d20, d31		@ A[4][0] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d21, d21, d31		@ A[4][1] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	cmp	r6, #8*24
	veor	d22, d22, d31		@ A[4][2] ^= *inp++
	blo	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d23, d23, d31		@ A[4][3] ^= *inp++
	beq	.Lprocess_neon
	vld1.8	{d31}, [r4]!
	veor	d24, d24, d31		@ A[4][4] ^= *inp++

.Lprocess_neon:
	bl	KeccakF1600_neon
	b 	.Loop_absorb_neon

.align	4
.Labsorbed_neon:
	vst1.32	{d0}, [r0:64]!		@ A[0][0..4]
	vst1.32	{d2}, [r0:64]!
	vst1.32	{d4}, [r0:64]!
	vst1.32	{d6}, [r0:64]!
	vst1.32	{d8}, [r0:64]!

	vst1.32	{d1}, [r0:64]!		@ A[1][0..4]
	vst1.32	{d3}, [r0:64]!
	vst1.32	{d5}, [r0:64]!
	vst1.32	{d7}, [r0:64]!
	vst1.32	{d9}, [r0:64]!

	vst1.32	{d10}, [r0:64]!		@ A[2][0..4]
	vst1.32	{d12}, [r0:64]!
	vst1.32	{d14}, [r0:64]!
	vst1.32	{d16}, [r0:64]!
	vst1.32	{d18}, [r0:64]!

	vst1.32	{d11}, [r0:64]!		@ A[3][0..4]
	vst1.32	{d13}, [r0:64]!
	vst1.32	{d15}, [r0:64]!
	vst1.32	{d17}, [r0:64]!
	vst1.32	{d19}, [r0:64]!

	vst1.32	{d20-d23}, [r0:64]!	@ A[4][0..4]
	vst1.32	{d24}, [r0:64]

	mov	r0, r5			@ return value
	vldmia	sp!, {d8-d15}
	ldmia	sp!, {r4-r6,pc}
.size	SHA3_absorb_neon,.-SHA3_absorb_neon

.global	SHA3_squeeze_neon
.type	SHA3_squeeze_neon, %function
.align	5
SHA3_squeeze_neon:
	stmdb	sp!, {r4-r6,lr}

	mov	r4, r1			@ out
	mov	r5, r2			@ len
	mov	r6, r3			@ bsz
	mov	r12, r0			@ A_flat
	mov	r14, r3			@ bsz
	b	.Loop_squeeze_neon

.align	4
.Loop_squeeze_neon:
	cmp	r5, #8
	blo	.Lsqueeze_neon_tail
	vld1.32	{d0}, [r12]!
	vst1.8	{d0}, [r4]!		@ endian-neutral store

	subs	r5, r5, #8		@ len -= 8
	beq	.Lsqueeze_neon_done

	subs	r14, r14, #8		@ bsz -= 8
	bhi	.Loop_squeeze_neon

	vstmdb	sp!,  {d8-d15}

	vld1.32	{d0}, [r0:64]!		@ A[0][0..4]
	vld1.32	{d2}, [r0:64]!
	vld1.32	{d4}, [r0:64]!
	vld1.32	{d6}, [r0:64]!
	vld1.32	{d8}, [r0:64]!

	vld1.32	{d1}, [r0:64]!		@ A[1][0..4]
	vld1.32	{d3}, [r0:64]!
	vld1.32	{d5}, [r0:64]!
	vld1.32	{d7}, [r0:64]!
	vld1.32	{d9}, [r0:64]!

	vld1.32	{d10}, [r0:64]!		@ A[2][0..4]
	vld1.32	{d12}, [r0:64]!
	vld1.32	{d14}, [r0:64]!
	vld1.32	{d16}, [r0:64]!
	vld1.32	{d18}, [r0:64]!

	vld1.32	{d11}, [r0:64]!		@ A[3][0..4]
	vld1.32	{d13}, [r0:64]!
	vld1.32	{d15}, [r0:64]!
	vld1.32	{d17}, [r0:64]!
	vld1.32	{d19}, [r0:64]!

	vld1.32	{d20-d23}, [r0:64]!	@ A[4][0..4]
	vld1.32	{d24}, [r0:64]
	sub	r0, r0, #24*8		@ rewind

	bl	KeccakF1600_neon

	mov	r12, r0			@ A_flat
	vst1.32	{d0}, [r0:64]!		@ A[0][0..4]
	vst1.32	{d2}, [r0:64]!
	vst1.32	{d4}, [r0:64]!
	vst1.32	{d6}, [r0:64]!
	vst1.32	{d8}, [r0:64]!

	vst1.32	{d1}, [r0:64]!		@ A[1][0..4]
	vst1.32	{d3}, [r0:64]!
	vst1.32	{d5}, [r0:64]!
	vst1.32	{d7}, [r0:64]!
	vst1.32	{d9}, [r0:64]!

	vst1.32	{d10}, [r0:64]!		@ A[2][0..4]
	vst1.32	{d12}, [r0:64]!
	vst1.32	{d14}, [r0:64]!
	vst1.32	{d16}, [r0:64]!
	vst1.32	{d18}, [r0:64]!

	vst1.32	{d11}, [r0:64]!		@ A[3][0..4]
	vst1.32	{d13}, [r0:64]!
	vst1.32	{d15}, [r0:64]!
	vst1.32	{d17}, [r0:64]!
	vst1.32	{d19}, [r0:64]!

	vst1.32	{d20-d23}, [r0:64]!	@ A[4][0..4]
	mov	r14, r6			@ bsz
	vst1.32	{d24}, [r0:64]
	mov	r0,  r12		@ rewind

	vldmia	sp!, {d8-d15}
	b	.Loop_squeeze_neon

.align	4
.Lsqueeze_neon_tail:
	ldmia	r12, {r2,r3}
	cmp	r5, #2
	strb	r2, [r4],#1		@ endian-neutral store
	lsr	r2, r2, #8
	blo	.Lsqueeze_neon_done
	strb	r2, [r4], #1
	lsr	r2, r2, #8
	beq	.Lsqueeze_neon_done
	strb	r2, [r4], #1
	lsr	r2, r2, #8
	cmp	r5, #4
	blo	.Lsqueeze_neon_done
	strb	r2, [r4], #1
	beq	.Lsqueeze_neon_done

	strb	r3, [r4], #1
	lsr	r3, r3, #8
	cmp	r5, #6
	blo	.Lsqueeze_neon_done
	strb	r3, [r4], #1
	lsr	r3, r3, #8
	beq	.Lsqueeze_neon_done
	strb	r3, [r4], #1

.Lsqueeze_neon_done:
	ldmia	sp!, {r4-r6,pc}
.size	SHA3_squeeze_neon,.-SHA3_squeeze_neon
#endif
.asciz	"Keccak-1600 absorb and squeeze for ARMv4/NEON, CRYPTOGAMS by <appro\@openssl.org>"
.align	2
___

{
    my %ldr, %str;

    sub ldrd {
	my ($mnemonic,$half,$reg,$ea) = @_;
	my $op = $mnemonic eq "ldr" ? \%ldr : \%str;

	if ($half eq "l") {
	    $$op{reg} = $reg;
	    $$op{ea}  = $ea;
	    sprintf "#ifndef	__thumb2__\n"	.
		    "	%s\t%s,%s\n"		.
		    "#endif", $mnemonic,$reg,$ea;
	} else {
	    sprintf "#ifndef	__thumb2__\n"	.
		    "	%s\t%s,%s\n"		.
		    "#else\n"			.
		    "	%sd\t%s,%s,%s\n"	.
		    "#endif",	$mnemonic,$reg,$ea,
				$mnemonic,$$op{reg},$reg,$$op{ea};
	}
    }
}

foreach (split($/,$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	s/^\s+(ldr|str)\.([lh])\s+(r[0-9]+),\s*(\[.*)/ldrd($1,$2,$3,$4)/ge or
	s/\b(ror|ls[rl])\s+(r[0-9]+.*)#/mov	$2$1#/g or
	s/\bret\b/bx	lr/g		or
	s/\bbx\s+lr\b/.word\t0xe12fff1e/g;	# make it possible to compile with -march=armv4

	print $_,"\n";
}

close STDOUT; # enforce flush

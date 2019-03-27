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
# Keccak-1600 for AVX-512F.
#
# July 2017.
#
# Below code is KECCAK_1X_ALT implementation (see sha/keccak1600.c).
# Pretty straightforward, the only "magic" is data layout in registers.
# It's impossible to have one that is optimal for every step, hence
# it's changing as algorithm progresses. Data is saved in linear order,
# but in-register order morphs between rounds. Even rounds take in
# linear layout, and odd rounds - transposed, or "verticaly-shaped"...
#
########################################################################
# Numbers are cycles per processed byte out of large message.
#
#			r=1088(*)
#
# Knights Landing	7.6
# Skylake-X		5.7
#
# (*)	Corresponds to SHA3-256.

########################################################################
# Below code is combination of two ideas. One is taken from Keccak Code
# Package, hereafter KCP, and another one from initial version of this
# module. What is common is observation that Pi's input and output are
# "mostly transposed", i.e. if input is aligned by x coordinate, then
# output is [mostly] aligned by y. Both versions, KCP and predecessor,
# were trying to use one of them from round to round, which resulted in
# some kind of transposition in each round. This version still does
# transpose data, but only every second round. Another essential factor
# is that KCP transposition has to be performed with instructions that
# turned to be rather expensive on Knights Landing, both latency- and
# throughput-wise. Not to mention that some of them have to depend on
# each other. On the other hand initial version of this module was
# relying heavily on blend instructions. There were lots of them,
# resulting in higher instruction count, yet it performed better on
# Knights Landing, because processor can execute pair of them each
# cycle and they have minimal latency. This module is an attempt to
# bring best parts together:-)
#
# Coordinates below correspond to those in sha/keccak1600.c. Input
# layout is straight linear:
#
# [0][4] [0][3] [0][2] [0][1] [0][0]
# [1][4] [1][3] [1][2] [1][1] [1][0]
# [2][4] [2][3] [2][2] [2][1] [2][0]
# [3][4] [3][3] [3][2] [3][1] [3][0]
# [4][4] [4][3] [4][2] [4][1] [4][0]
#
# It's perfect for Theta, while Pi is reduced to intra-register
# permutations which yield layout perfect for Chi:
#
# [4][0] [3][0] [2][0] [1][0] [0][0]
# [4][1] [3][1] [2][1] [1][1] [0][1]
# [4][2] [3][2] [2][2] [1][2] [0][2]
# [4][3] [3][3] [2][3] [1][3] [0][3]
# [4][4] [3][4] [2][4] [1][4] [0][4]
#
# Now instead of performing full transposition and feeding it to next
# identical round, we perform kind of diagonal transposition to layout
# from initial version of this module, and make it suitable for Theta:
#
# [4][4] [3][3] [2][2] [1][1] [0][0]>4.3.2.1.0>[4][4] [3][3] [2][2] [1][1] [0][0]
# [4][0] [3][4] [2][3] [1][2] [0][1]>3.2.1.0.4>[3][4] [2][3] [1][2] [0][1] [4][0]
# [4][1] [3][0] [2][4] [1][3] [0][2]>2.1.0.4.3>[2][4] [1][3] [0][2] [4][1] [3][0]
# [4][2] [3][1] [2][0] [1][4] [0][3]>1.0.4.3.2>[1][4] [0][3] [4][2] [3][1] [2][0]
# [4][3] [3][2] [2][1] [1][0] [0][4]>0.4.3.2.1>[0][4] [4][3] [3][2] [2][1] [1][0]
#
# Now intra-register permutations yield initial [almost] straight
# linear layout:
#
# [4][4] [3][3] [2][2] [1][1] [0][0]
##[0][4] [0][3] [0][2] [0][1] [0][0]
# [3][4] [2][3] [1][2] [0][1] [4][0]
##[2][3] [2][2] [2][1] [2][0] [2][4]
# [2][4] [1][3] [0][2] [4][1] [3][0]
##[4][2] [4][1] [4][0] [4][4] [4][3]
# [1][4] [0][3] [4][2] [3][1] [2][0]
##[1][1] [1][0] [1][4] [1][3] [1][2]
# [0][4] [4][3] [3][2] [2][1] [1][0]
##[3][0] [3][4] [3][3] [3][2] [3][1]
#
# This means that odd round Chi is performed in less suitable layout,
# with a number of additional permutations. But overall it turned to be
# a win. Permutations are fastest possible on Knights Landing and they
# are laid down to be independent of each other. In the essence I traded
# 20 blend instructions for 3 permutations. The result is 13% faster
# than KCP on Skylake-X, and >40% on Knights Landing.
#
# As implied, data is loaded in straight linear order. Digits in
# variables' names represent coordinates of right-most element of
# loaded data chunk:

my ($A00,	# [0][4] [0][3] [0][2] [0][1] [0][0]
    $A10,	# [1][4] [1][3] [1][2] [1][1] [1][0]
    $A20,	# [2][4] [2][3] [2][2] [2][1] [2][0]
    $A30,	# [3][4] [3][3] [3][2] [3][1] [3][0]
    $A40) =	# [4][4] [4][3] [4][2] [4][1] [4][0]
    map("%zmm$_",(0..4));

# We also need to map the magic order into offsets within structure:

my @A_jagged = ([0,0], [0,1], [0,2], [0,3], [0,4],
		[1,0], [1,1], [1,2], [1,3], [1,4],
		[2,0], [2,1], [2,2], [2,3], [2,4],
		[3,0], [3,1], [3,2], [3,3], [3,4],
		[4,0], [4,1], [4,2], [4,3], [4,4]);
   @A_jagged = map(8*($$_[0]*8+$$_[1]), @A_jagged);	# ... and now linear

my @T        = map("%zmm$_",(5..12));
my @Theta    = map("%zmm$_",(33,13..16));	# invalid @Theta[0] is not typo
my @Pi0      = map("%zmm$_",(17..21));
my @Rhotate0 = map("%zmm$_",(22..26));
my @Rhotate1 = map("%zmm$_",(27..31));

my ($C00,$D00) = @T[0..1];
my ($k00001,$k00010,$k00100,$k01000,$k10000,$k11111) = map("%k$_",(1..6));

$code.=<<___;
.text

.type	__KeccakF1600,\@function
.align	32
__KeccakF1600:
	lea		iotas(%rip),%r10
	mov		\$12,%eax
	jmp		.Loop_avx512

.align	32
.Loop_avx512:
	######################################### Theta, even round
	vmovdqa64	$A00,@T[0]		# put aside original A00
	vpternlogq	\$0x96,$A20,$A10,$A00	# and use it as "C00"
	vpternlogq	\$0x96,$A40,$A30,$A00

	vprolq		\$1,$A00,$D00
	vpermq		$A00,@Theta[1],$A00
	vpermq		$D00,@Theta[4],$D00

	vpternlogq	\$0x96,$A00,$D00,@T[0]	# T[0] is original A00
	vpternlogq	\$0x96,$A00,$D00,$A10
	vpternlogq	\$0x96,$A00,$D00,$A20
	vpternlogq	\$0x96,$A00,$D00,$A30
	vpternlogq	\$0x96,$A00,$D00,$A40

	######################################### Rho
	vprolvq		@Rhotate0[0],@T[0],$A00	# T[0] is original A00
	vprolvq		@Rhotate0[1],$A10,$A10
	vprolvq		@Rhotate0[2],$A20,$A20
	vprolvq		@Rhotate0[3],$A30,$A30
	vprolvq		@Rhotate0[4],$A40,$A40

	######################################### Pi
	vpermq		$A00,@Pi0[0],$A00
	vpermq		$A10,@Pi0[1],$A10
	vpermq		$A20,@Pi0[2],$A20
	vpermq		$A30,@Pi0[3],$A30
	vpermq		$A40,@Pi0[4],$A40

	######################################### Chi
	vmovdqa64	$A00,@T[0]
	vmovdqa64	$A10,@T[1]
	vpternlogq	\$0xD2,$A20,$A10,$A00
	vpternlogq	\$0xD2,$A30,$A20,$A10
	vpternlogq	\$0xD2,$A40,$A30,$A20
	vpternlogq	\$0xD2,@T[0],$A40,$A30
	vpternlogq	\$0xD2,@T[1],@T[0],$A40

	######################################### Iota
	vpxorq		(%r10),$A00,${A00}{$k00001}
	lea		16(%r10),%r10

	######################################### Harmonize rounds
	vpblendmq	$A20,$A10,@{T[1]}{$k00010}
	vpblendmq	$A30,$A20,@{T[2]}{$k00010}
	vpblendmq	$A40,$A30,@{T[3]}{$k00010}
	 vpblendmq	$A10,$A00,@{T[0]}{$k00010}
	vpblendmq	$A00,$A40,@{T[4]}{$k00010}

	vpblendmq	$A30,@T[1],@{T[1]}{$k00100}
	vpblendmq	$A40,@T[2],@{T[2]}{$k00100}
	 vpblendmq	$A20,@T[0],@{T[0]}{$k00100}
	vpblendmq	$A00,@T[3],@{T[3]}{$k00100}
	vpblendmq	$A10,@T[4],@{T[4]}{$k00100}

	vpblendmq	$A40,@T[1],@{T[1]}{$k01000}
	 vpblendmq	$A30,@T[0],@{T[0]}{$k01000}
	vpblendmq	$A00,@T[2],@{T[2]}{$k01000}
	vpblendmq	$A10,@T[3],@{T[3]}{$k01000}
	vpblendmq	$A20,@T[4],@{T[4]}{$k01000}

	vpblendmq	$A40,@T[0],@{T[0]}{$k10000}
	vpblendmq	$A00,@T[1],@{T[1]}{$k10000}
	vpblendmq	$A10,@T[2],@{T[2]}{$k10000}
	vpblendmq	$A20,@T[3],@{T[3]}{$k10000}
	vpblendmq	$A30,@T[4],@{T[4]}{$k10000}

	#vpermq		@T[0],@Theta[0],$A00	# doesn't actually change order
	vpermq		@T[1],@Theta[1],$A10
	vpermq		@T[2],@Theta[2],$A20
	vpermq		@T[3],@Theta[3],$A30
	vpermq		@T[4],@Theta[4],$A40

	######################################### Theta, odd round
	vmovdqa64	$T[0],$A00		# real A00
	vpternlogq	\$0x96,$A20,$A10,$C00	# C00 is @T[0]'s alias
	vpternlogq	\$0x96,$A40,$A30,$C00

	vprolq		\$1,$C00,$D00
	vpermq		$C00,@Theta[1],$C00
	vpermq		$D00,@Theta[4],$D00

	vpternlogq	\$0x96,$C00,$D00,$A00
	vpternlogq	\$0x96,$C00,$D00,$A30
	vpternlogq	\$0x96,$C00,$D00,$A10
	vpternlogq	\$0x96,$C00,$D00,$A40
	vpternlogq	\$0x96,$C00,$D00,$A20

	######################################### Rho
	vprolvq		@Rhotate1[0],$A00,$A00
	vprolvq		@Rhotate1[3],$A30,@T[1]
	vprolvq		@Rhotate1[1],$A10,@T[2]
	vprolvq		@Rhotate1[4],$A40,@T[3]
	vprolvq		@Rhotate1[2],$A20,@T[4]

	 vpermq		$A00,@Theta[4],@T[5]
	 vpermq		$A00,@Theta[3],@T[6]

	######################################### Iota
	vpxorq		-8(%r10),$A00,${A00}{$k00001}

	######################################### Pi
	vpermq		@T[1],@Theta[2],$A10
	vpermq		@T[2],@Theta[4],$A20
	vpermq		@T[3],@Theta[1],$A30
	vpermq		@T[4],@Theta[3],$A40

	######################################### Chi
	vpternlogq	\$0xD2,@T[6],@T[5],$A00

	vpermq		@T[1],@Theta[1],@T[7]
	#vpermq		@T[1],@Theta[0],@T[1]
	vpternlogq	\$0xD2,@T[1],@T[7],$A10

	vpermq		@T[2],@Theta[3],@T[0]
	vpermq		@T[2],@Theta[2],@T[2]
	vpternlogq	\$0xD2,@T[2],@T[0],$A20

	#vpermq		@T[3],@Theta[0],@T[3]
	vpermq		@T[3],@Theta[4],@T[1]
	vpternlogq	\$0xD2,@T[1],@T[3],$A30

	vpermq		@T[4],@Theta[2],@T[0]
	vpermq		@T[4],@Theta[1],@T[4]
	vpternlogq	\$0xD2,@T[4],@T[0],$A40

	dec		%eax
	jnz		.Loop_avx512

	ret
.size	__KeccakF1600,.-__KeccakF1600
___

my ($A_flat,$inp,$len,$bsz) = ("%rdi","%rsi","%rdx","%rcx");
my  $out = $inp;	# in squeeze

$code.=<<___;
.globl	SHA3_absorb
.type	SHA3_absorb,\@function
.align	32
SHA3_absorb:
	mov	%rsp,%r11

	lea	-320(%rsp),%rsp
	and	\$-64,%rsp

	lea	96($A_flat),$A_flat
	lea	96($inp),$inp
	lea	128(%rsp),%r9

	lea		theta_perm(%rip),%r8

	kxnorw		$k11111,$k11111,$k11111
	kshiftrw	\$15,$k11111,$k00001
	kshiftrw	\$11,$k11111,$k11111
	kshiftlw	\$1,$k00001,$k00010
	kshiftlw	\$2,$k00001,$k00100
	kshiftlw	\$3,$k00001,$k01000
	kshiftlw	\$4,$k00001,$k10000

	#vmovdqa64	64*0(%r8),@Theta[0]
	vmovdqa64	64*1(%r8),@Theta[1]
	vmovdqa64	64*2(%r8),@Theta[2]
	vmovdqa64	64*3(%r8),@Theta[3]
	vmovdqa64	64*4(%r8),@Theta[4]

	vmovdqa64	64*5(%r8),@Rhotate1[0]
	vmovdqa64	64*6(%r8),@Rhotate1[1]
	vmovdqa64	64*7(%r8),@Rhotate1[2]
	vmovdqa64	64*8(%r8),@Rhotate1[3]
	vmovdqa64	64*9(%r8),@Rhotate1[4]

	vmovdqa64	64*10(%r8),@Rhotate0[0]
	vmovdqa64	64*11(%r8),@Rhotate0[1]
	vmovdqa64	64*12(%r8),@Rhotate0[2]
	vmovdqa64	64*13(%r8),@Rhotate0[3]
	vmovdqa64	64*14(%r8),@Rhotate0[4]

	vmovdqa64	64*15(%r8),@Pi0[0]
	vmovdqa64	64*16(%r8),@Pi0[1]
	vmovdqa64	64*17(%r8),@Pi0[2]
	vmovdqa64	64*18(%r8),@Pi0[3]
	vmovdqa64	64*19(%r8),@Pi0[4]

	vmovdqu64	40*0-96($A_flat),${A00}{$k11111}{z}
	vpxorq		@T[0],@T[0],@T[0]
	vmovdqu64	40*1-96($A_flat),${A10}{$k11111}{z}
	vmovdqu64	40*2-96($A_flat),${A20}{$k11111}{z}
	vmovdqu64	40*3-96($A_flat),${A30}{$k11111}{z}
	vmovdqu64	40*4-96($A_flat),${A40}{$k11111}{z}

	vmovdqa64	@T[0],0*64-128(%r9)	# zero transfer area on stack
	vmovdqa64	@T[0],1*64-128(%r9)
	vmovdqa64	@T[0],2*64-128(%r9)
	vmovdqa64	@T[0],3*64-128(%r9)
	vmovdqa64	@T[0],4*64-128(%r9)
	jmp		.Loop_absorb_avx512

.align	32
.Loop_absorb_avx512:
	mov		$bsz,%rax
	sub		$bsz,$len
	jc		.Ldone_absorb_avx512

	shr		\$3,%eax
___
for(my $i=0; $i<25; $i++) {
$code.=<<___
	mov	8*$i-96($inp),%r8
	mov	%r8,$A_jagged[$i]-128(%r9)
	dec	%eax
	jz	.Labsorved_avx512
___
}
$code.=<<___;
.Labsorved_avx512:
	lea	($inp,$bsz),$inp

	vpxorq	64*0-128(%r9),$A00,$A00
	vpxorq	64*1-128(%r9),$A10,$A10
	vpxorq	64*2-128(%r9),$A20,$A20
	vpxorq	64*3-128(%r9),$A30,$A30
	vpxorq	64*4-128(%r9),$A40,$A40

	call	__KeccakF1600

	jmp	.Loop_absorb_avx512

.align	32
.Ldone_absorb_avx512:
	vmovdqu64	$A00,40*0-96($A_flat){$k11111}
	vmovdqu64	$A10,40*1-96($A_flat){$k11111}
	vmovdqu64	$A20,40*2-96($A_flat){$k11111}
	vmovdqu64	$A30,40*3-96($A_flat){$k11111}
	vmovdqu64	$A40,40*4-96($A_flat){$k11111}

	vzeroupper

	lea	(%r11),%rsp
	lea	($len,$bsz),%rax		# return value
	ret
.size	SHA3_absorb,.-SHA3_absorb

.globl	SHA3_squeeze
.type	SHA3_squeeze,\@function
.align	32
SHA3_squeeze:
	mov	%rsp,%r11

	lea	96($A_flat),$A_flat
	cmp	$bsz,$len
	jbe	.Lno_output_extension_avx512

	lea		theta_perm(%rip),%r8

	kxnorw		$k11111,$k11111,$k11111
	kshiftrw	\$15,$k11111,$k00001
	kshiftrw	\$11,$k11111,$k11111
	kshiftlw	\$1,$k00001,$k00010
	kshiftlw	\$2,$k00001,$k00100
	kshiftlw	\$3,$k00001,$k01000
	kshiftlw	\$4,$k00001,$k10000

	#vmovdqa64	64*0(%r8),@Theta[0]
	vmovdqa64	64*1(%r8),@Theta[1]
	vmovdqa64	64*2(%r8),@Theta[2]
	vmovdqa64	64*3(%r8),@Theta[3]
	vmovdqa64	64*4(%r8),@Theta[4]

	vmovdqa64	64*5(%r8),@Rhotate1[0]
	vmovdqa64	64*6(%r8),@Rhotate1[1]
	vmovdqa64	64*7(%r8),@Rhotate1[2]
	vmovdqa64	64*8(%r8),@Rhotate1[3]
	vmovdqa64	64*9(%r8),@Rhotate1[4]

	vmovdqa64	64*10(%r8),@Rhotate0[0]
	vmovdqa64	64*11(%r8),@Rhotate0[1]
	vmovdqa64	64*12(%r8),@Rhotate0[2]
	vmovdqa64	64*13(%r8),@Rhotate0[3]
	vmovdqa64	64*14(%r8),@Rhotate0[4]

	vmovdqa64	64*15(%r8),@Pi0[0]
	vmovdqa64	64*16(%r8),@Pi0[1]
	vmovdqa64	64*17(%r8),@Pi0[2]
	vmovdqa64	64*18(%r8),@Pi0[3]
	vmovdqa64	64*19(%r8),@Pi0[4]

	vmovdqu64	40*0-96($A_flat),${A00}{$k11111}{z}
	vmovdqu64	40*1-96($A_flat),${A10}{$k11111}{z}
	vmovdqu64	40*2-96($A_flat),${A20}{$k11111}{z}
	vmovdqu64	40*3-96($A_flat),${A30}{$k11111}{z}
	vmovdqu64	40*4-96($A_flat),${A40}{$k11111}{z}

.Lno_output_extension_avx512:
	shr	\$3,$bsz
	lea	-96($A_flat),%r9
	mov	$bsz,%rax
	jmp	.Loop_squeeze_avx512

.align	32
.Loop_squeeze_avx512:
	cmp	\$8,$len
	jb	.Ltail_squeeze_avx512

	mov	(%r9),%r8
	lea	8(%r9),%r9
	mov	%r8,($out)
	lea	8($out),$out
	sub	\$8,$len		# len -= 8
	jz	.Ldone_squeeze_avx512

	sub	\$1,%rax		# bsz--
	jnz	.Loop_squeeze_avx512

	#vpermq		@Theta[4],@Theta[4],@Theta[3]
	#vpermq		@Theta[3],@Theta[4],@Theta[2]
	#vpermq		@Theta[3],@Theta[3],@Theta[1]

	call		__KeccakF1600

	vmovdqu64	$A00,40*0-96($A_flat){$k11111}
	vmovdqu64	$A10,40*1-96($A_flat){$k11111}
	vmovdqu64	$A20,40*2-96($A_flat){$k11111}
	vmovdqu64	$A30,40*3-96($A_flat){$k11111}
	vmovdqu64	$A40,40*4-96($A_flat){$k11111}

	lea	-96($A_flat),%r9
	mov	$bsz,%rax
	jmp	.Loop_squeeze_avx512

.Ltail_squeeze_avx512:
	mov	$out,%rdi
	mov	%r9,%rsi
	mov	$len,%rcx
	.byte	0xf3,0xa4		# rep movsb

.Ldone_squeeze_avx512:
	vzeroupper

	lea	(%r11),%rsp
	ret
.size	SHA3_squeeze,.-SHA3_squeeze

.align	64
theta_perm:
	.quad	0, 1, 2, 3, 4, 5, 6, 7		# [not used]
	.quad	4, 0, 1, 2, 3, 5, 6, 7
	.quad	3, 4, 0, 1, 2, 5, 6, 7
	.quad	2, 3, 4, 0, 1, 5, 6, 7
	.quad	1, 2, 3, 4, 0, 5, 6, 7

rhotates1:
	.quad	0,  44, 43, 21, 14, 0, 0, 0	# [0][0] [1][1] [2][2] [3][3] [4][4]
	.quad	18, 1,  6,  25, 8,  0, 0, 0	# [4][0] [0][1] [1][2] [2][3] [3][4]
	.quad	41, 2,	62, 55, 39, 0, 0, 0	# [3][0] [4][1] [0][2] [1][3] [2][4]
	.quad	3,  45, 61, 28, 20, 0, 0, 0	# [2][0] [3][1] [4][2] [0][3] [1][4]
	.quad	36, 10, 15, 56, 27, 0, 0, 0	# [1][0] [2][1] [3][2] [4][3] [0][4]

rhotates0:
	.quad	 0,  1, 62, 28, 27, 0, 0, 0
	.quad	36, 44,  6, 55, 20, 0, 0, 0
	.quad	 3, 10, 43, 25, 39, 0, 0, 0
	.quad	41, 45, 15, 21,  8, 0, 0, 0
	.quad	18,  2, 61, 56, 14, 0, 0, 0

pi0_perm:
	.quad	0, 3, 1, 4, 2, 5, 6, 7
	.quad	1, 4, 2, 0, 3, 5, 6, 7
	.quad	2, 0, 3, 1, 4, 5, 6, 7
	.quad	3, 1, 4, 2, 0, 5, 6, 7
	.quad	4, 2, 0, 3, 1, 5, 6, 7


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

.asciz	"Keccak-1600 absorb and squeeze for AVX-512F, CRYPTOGAMS by <appro\@openssl.org>"
___

$output=pop;
open STDOUT,">$output";
print $code;
close STDOUT;

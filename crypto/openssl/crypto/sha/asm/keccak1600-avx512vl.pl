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
# Keccak-1600 for AVX512VL.
#
# December 2017.
#
# This is an adaptation of AVX2 module that reuses register data
# layout, but utilizes new 256-bit AVX512VL instructions. See AVX2
# module for further information on layout.
#
########################################################################
# Numbers are cycles per processed byte out of large message.
#
#			r=1088(*)
#
# Skylake-X		6.4/+47%
#
# (*)	Corresponds to SHA3-256. Percentage after slash is improvement
#	coefficient in comparison to scalar keccak1600-x86_64.pl.

# Digits in variables' names denote right-most coordinates:

my ($A00,	# [0][0] [0][0] [0][0] [0][0]		# %ymm0
    $A01,	# [0][4] [0][3] [0][2] [0][1]		# %ymm1
    $A20,	# [3][0] [1][0] [4][0] [2][0]		# %ymm2
    $A31,	# [2][4] [4][3] [1][2] [3][1]		# %ymm3
    $A21,	# [3][4] [1][3] [4][2] [2][1]		# %ymm4
    $A41,	# [1][4] [2][3] [3][2] [4][1]		# %ymm5
    $A11) =	# [4][4] [3][3] [2][2] [1][1]		# %ymm6
    map("%ymm$_",(0..6));

# We also need to map the magic order into offsets within structure:

my @A_jagged = ([0,0], [1,0], [1,1], [1,2], [1,3],	# [0][0..4]
		[2,2], [6,0], [3,1], [4,2], [5,3],	# [1][0..4]
		[2,0], [4,0], [6,1], [5,2], [3,3],	# [2][0..4]
		[2,3], [3,0], [5,1], [6,2], [4,3],	# [3][0..4]
		[2,1], [5,0], [4,1], [3,2], [6,3]);	# [4][0..4]
   @A_jagged = map(8*($$_[0]*4+$$_[1]), @A_jagged);	# ... and now linear

my @T = map("%ymm$_",(7..15));
my ($C14,$C00,$D00,$D14) = @T[5..8];
my ($R20,$R01,$R31,$R21,$R41,$R11) = map("%ymm$_",(16..21));

$code.=<<___;
.text

.type	__KeccakF1600,\@function
.align	32
__KeccakF1600:
	lea		iotas(%rip),%r10
	mov		\$24,%eax
	jmp		.Loop_avx512vl

.align	32
.Loop_avx512vl:
	######################################### Theta
	vpshufd		\$0b01001110,$A20,$C00
	vpxor		$A31,$A41,$C14
	vpxor		$A11,$A21,@T[2]
	vpternlogq	\$0x96,$A01,$T[2],$C14	# C[1..4]

	vpxor		$A20,$C00,$C00
	vpermq		\$0b01001110,$C00,@T[0]

	vpermq		\$0b10010011,$C14,@T[4]
	vprolq		\$1,$C14,@T[1]		# ROL64(C[1..4],1)

	vpermq		\$0b00111001,@T[1],$D14
	vpxor		@T[4],@T[1],$D00
	vpermq		\$0b00000000,$D00,$D00	# D[0..0] = ROL64(C[1],1) ^ C[4]

	vpternlogq	\$0x96,@T[0],$A00,$C00	# C[0..0]
	vprolq		\$1,$C00,@T[1]		# ROL64(C[0..0],1)

	vpxor		$D00,$A00,$A00		# ^= D[0..0]

	vpblendd	\$0b11000000,@T[1],$D14,$D14
	vpblendd	\$0b00000011,$C00,@T[4],@T[0]

	######################################### Rho + Pi + pre-Chi shuffle
	 vpxor		$D00,$A20,$A20		# ^= D[0..0] from Theta
	vprolvq		$R20,$A20,$A20

	 vpternlogq	\$0x96,@T[0],$D14,$A31	# ^= D[1..4] from Theta
	vprolvq		$R31,$A31,$A31

	 vpternlogq	\$0x96,@T[0],$D14,$A21	# ^= D[1..4] from Theta
	vprolvq		$R21,$A21,$A21

	 vpternlogq	\$0x96,@T[0],$D14,$A41	# ^= D[1..4] from Theta
	vprolvq		$R41,$A41,$A41

	 vpermq		\$0b10001101,$A20,@T[3]	# $A20 -> future $A31
	 vpermq		\$0b10001101,$A31,@T[4]	# $A31 -> future $A21
	 vpternlogq	\$0x96,@T[0],$D14,$A11	# ^= D[1..4] from Theta
	vprolvq		$R11,$A11,@T[1]		# $A11 -> future $A01

	 vpermq		\$0b00011011,$A21,@T[5]	# $A21 -> future $A41
	 vpermq		\$0b01110010,$A41,@T[6]	# $A41 -> future $A11
	 vpternlogq	\$0x96,@T[0],$D14,$A01	# ^= D[1..4] from Theta
	vprolvq		$R01,$A01,@T[2]		# $A01 -> future $A20

	######################################### Chi
	vpblendd	\$0b00001100,@T[6],@T[2],$A31	#               [4][4] [2][0]
	vpblendd	\$0b00001100,@T[2],@T[4],@T[8]	#               [4][0] [2][1]
	 vpblendd	\$0b00001100,@T[4],@T[3],$A41	#               [4][2] [2][4]
	 vpblendd	\$0b00001100,@T[3],@T[2],@T[7]	#               [4][3] [2][0]
	vpblendd	\$0b00110000,@T[4],$A31,$A31	#        [1][3] [4][4] [2][0]
	vpblendd	\$0b00110000,@T[5],@T[8],@T[8]	#        [1][4] [4][0] [2][1]
	 vpblendd	\$0b00110000,@T[2],$A41,$A41	#        [1][0] [4][2] [2][4]
	 vpblendd	\$0b00110000,@T[6],@T[7],@T[7]	#        [1][1] [4][3] [2][0]
	vpblendd	\$0b11000000,@T[5],$A31,$A31	# [3][2] [1][3] [4][4] [2][0]
	vpblendd	\$0b11000000,@T[6],@T[8],@T[8]	# [3][3] [1][4] [4][0] [2][1]
	 vpblendd	\$0b11000000,@T[6],$A41,$A41	# [3][3] [1][0] [4][2] [2][4]
	 vpblendd	\$0b11000000,@T[4],@T[7],@T[7]	# [3][4] [1][1] [4][3] [2][0]
	vpternlogq	\$0xC6,@T[8],@T[3],$A31		# [3][1] [1][2] [4][3] [2][4]
	 vpternlogq	\$0xC6,@T[7],@T[5],$A41		# [3][2] [1][4] [4][1] [2][3]

	vpsrldq		\$8,@T[1],@T[0]
	vpandn		@T[0],@T[1],@T[0]	# tgting  [0][0] [0][0] [0][0] [0][0]

	vpblendd	\$0b00001100,@T[2],@T[5],$A11	#               [4][0] [2][3]
	vpblendd	\$0b00001100,@T[5],@T[3],@T[8]	#               [4][1] [2][4]
	vpblendd	\$0b00110000,@T[3],$A11,$A11	#        [1][2] [4][0] [2][3]
	vpblendd	\$0b00110000,@T[4],@T[8],@T[8]	#        [1][3] [4][1] [2][4]
	vpblendd	\$0b11000000,@T[4],$A11,$A11	# [3][4] [1][2] [4][0] [2][3]
	vpblendd	\$0b11000000,@T[2],@T[8],@T[8]	# [3][0] [1][3] [4][1] [2][4]
	vpternlogq	\$0xC6,@T[8],@T[6],$A11		# [3][3] [1][1] [4][4] [2][2]

	  vpermq	\$0b00011110,@T[1],$A21		# [0][1] [0][2] [0][4] [0][3]
	  vpblendd	\$0b00110000,$A00,$A21,@T[8]	# [0][1] [0][0] [0][4] [0][3]
	  vpermq	\$0b00111001,@T[1],$A01		# [0][1] [0][4] [0][3] [0][2]
	  vpblendd	\$0b11000000,$A00,$A01,$A01	# [0][0] [0][4] [0][3] [0][2]

	vpblendd	\$0b00001100,@T[5],@T[4],$A20	#               [4][1] [2][1]
	vpblendd	\$0b00001100,@T[4],@T[6],@T[7]	#               [4][2] [2][2]
	vpblendd	\$0b00110000,@T[6],$A20,$A20	#        [1][1] [4][1] [2][1]
	vpblendd	\$0b00110000,@T[3],@T[7],@T[7]	#        [1][2] [4][2] [2][2]
	vpblendd	\$0b11000000,@T[3],$A20,$A20	# [3][1] [1][1] [4][1] [2][1]
	vpblendd	\$0b11000000,@T[5],@T[7],@T[7]	# [3][2] [1][2] [4][2] [2][2]
	vpternlogq	\$0xC6,@T[7],@T[2],$A20		# [3][0] [1][0] [4][0] [2][0]

	 vpermq		\$0b00000000,@T[0],@T[0]	# [0][0] [0][0] [0][0] [0][0]
	 vpermq		\$0b00011011,$A31,$A31		# post-Chi shuffle
	 vpermq		\$0b10001101,$A41,$A41
	 vpermq		\$0b01110010,$A11,$A11

	vpblendd	\$0b00001100,@T[3],@T[6],$A21	#               [4][3] [2][2]
	vpblendd	\$0b00001100,@T[6],@T[5],@T[7]	#               [4][4] [2][3]
	vpblendd	\$0b00110000,@T[5],$A21,$A21	#        [1][4] [4][3] [2][2]
	vpblendd	\$0b00110000,@T[2],@T[7],@T[7]	#        [1][0] [4][4] [2][3]
	vpblendd	\$0b11000000,@T[2],$A21,$A21	# [3][0] [1][4] [4][3] [2][2]
	vpblendd	\$0b11000000,@T[3],@T[7],@T[7]	# [3][1] [1][0] [4][4] [2][3]

	vpternlogq	\$0xC6,@T[8],@T[1],$A01		# [0][4] [0][3] [0][2] [0][1]
	vpternlogq	\$0xC6,@T[7],@T[4],$A21		# [3][4] [1][3] [4][2] [2][1]

	######################################### Iota
	vpternlogq	\$0x96,(%r10),@T[0],$A00
	lea		32(%r10),%r10

	dec		%eax
	jnz		.Loop_avx512vl

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

	lea	-240(%rsp),%rsp
	and	\$-32,%rsp

	lea	96($A_flat),$A_flat
	lea	96($inp),$inp
	lea	96(%rsp),%r10
	lea	rhotates_left(%rip),%r8

	vzeroupper

	vpbroadcastq	-96($A_flat),$A00	# load A[5][5]
	vmovdqu		8+32*0-96($A_flat),$A01
	vmovdqu		8+32*1-96($A_flat),$A20
	vmovdqu		8+32*2-96($A_flat),$A31
	vmovdqu		8+32*3-96($A_flat),$A21
	vmovdqu		8+32*4-96($A_flat),$A41
	vmovdqu		8+32*5-96($A_flat),$A11

	vmovdqa64	0*32(%r8),$R20		# load "rhotate" indices
	vmovdqa64	1*32(%r8),$R01
	vmovdqa64	2*32(%r8),$R31
	vmovdqa64	3*32(%r8),$R21
	vmovdqa64	4*32(%r8),$R41
	vmovdqa64	5*32(%r8),$R11

	vpxor		@T[0],@T[0],@T[0]
	vmovdqa		@T[0],32*2-96(%r10)	# zero transfer area on stack
	vmovdqa		@T[0],32*3-96(%r10)
	vmovdqa		@T[0],32*4-96(%r10)
	vmovdqa		@T[0],32*5-96(%r10)
	vmovdqa		@T[0],32*6-96(%r10)

.Loop_absorb_avx512vl:
	mov		$bsz,%rax
	sub		$bsz,$len
	jc		.Ldone_absorb_avx512vl

	shr		\$3,%eax
	vpbroadcastq	0-96($inp),@T[0]
	vmovdqu		8-96($inp),@T[1]
	sub		\$4,%eax
___
for(my $i=5; $i<25; $i++) {
$code.=<<___
	dec	%eax
	jz	.Labsorved_avx512vl
	mov	8*$i-96($inp),%r8
	mov	%r8,$A_jagged[$i]-96(%r10)
___
}
$code.=<<___;
.Labsorved_avx512vl:
	lea	($inp,$bsz),$inp

	vpxor	@T[0],$A00,$A00
	vpxor	@T[1],$A01,$A01
	vpxor	32*2-96(%r10),$A20,$A20
	vpxor	32*3-96(%r10),$A31,$A31
	vpxor	32*4-96(%r10),$A21,$A21
	vpxor	32*5-96(%r10),$A41,$A41
	vpxor	32*6-96(%r10),$A11,$A11

	call	__KeccakF1600

	lea	96(%rsp),%r10
	jmp	.Loop_absorb_avx512vl

.Ldone_absorb_avx512vl:
	vmovq	%xmm0,-96($A_flat)
	vmovdqu	$A01,8+32*0-96($A_flat)
	vmovdqu	$A20,8+32*1-96($A_flat)
	vmovdqu	$A31,8+32*2-96($A_flat)
	vmovdqu	$A21,8+32*3-96($A_flat)
	vmovdqu	$A41,8+32*4-96($A_flat)
	vmovdqu	$A11,8+32*5-96($A_flat)

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
	lea	rhotates_left(%rip),%r8
	shr	\$3,$bsz

	vzeroupper

	vpbroadcastq	-96($A_flat),$A00
	vpxor		@T[0],@T[0],@T[0]
	vmovdqu		8+32*0-96($A_flat),$A01
	vmovdqu		8+32*1-96($A_flat),$A20
	vmovdqu		8+32*2-96($A_flat),$A31
	vmovdqu		8+32*3-96($A_flat),$A21
	vmovdqu		8+32*4-96($A_flat),$A41
	vmovdqu		8+32*5-96($A_flat),$A11

	vmovdqa64	0*32(%r8),$R20		# load "rhotate" indices
	vmovdqa64	1*32(%r8),$R01
	vmovdqa64	2*32(%r8),$R31
	vmovdqa64	3*32(%r8),$R21
	vmovdqa64	4*32(%r8),$R41
	vmovdqa64	5*32(%r8),$R11

	mov	$bsz,%rax

.Loop_squeeze_avx512vl:
	mov	@A_jagged[$i]-96($A_flat),%r8
___
for (my $i=0; $i<25; $i++) {
$code.=<<___;
	sub	\$8,$len
	jc	.Ltail_squeeze_avx512vl
	mov	%r8,($out)
	lea	8($out),$out
	je	.Ldone_squeeze_avx512vl
	dec	%eax
	je	.Lextend_output_avx512vl
	mov	@A_jagged[$i+1]-120($A_flat),%r8
___
}
$code.=<<___;
.Lextend_output_avx512vl:
	call	__KeccakF1600

	vmovq	%xmm0,-96($A_flat)
	vmovdqu	$A01,8+32*0-96($A_flat)
	vmovdqu	$A20,8+32*1-96($A_flat)
	vmovdqu	$A31,8+32*2-96($A_flat)
	vmovdqu	$A21,8+32*3-96($A_flat)
	vmovdqu	$A41,8+32*4-96($A_flat)
	vmovdqu	$A11,8+32*5-96($A_flat)

	mov	$bsz,%rax
	jmp	.Loop_squeeze_avx512vl


.Ltail_squeeze_avx512vl:
	add	\$8,$len
.Loop_tail_avx512vl:
	mov	%r8b,($out)
	lea	1($out),$out
	shr	\$8,%r8
	dec	$len
	jnz	.Loop_tail_avx512vl

.Ldone_squeeze_avx512vl:
	vzeroupper

	lea	(%r11),%rsp
	ret
.size	SHA3_squeeze,.-SHA3_squeeze

.align	64
rhotates_left:
	.quad	3,	18,	36,	41	# [2][0] [4][0] [1][0] [3][0]
	.quad	1,	62,	28,	27	# [0][1] [0][2] [0][3] [0][4]
	.quad	45,	6,	56,	39	# [3][1] [1][2] [4][3] [2][4]
	.quad	10,	61,	55,	8	# [2][1] [4][2] [1][3] [3][4]
	.quad	2,	15,	25,	20	# [4][1] [3][2] [2][3] [1][4]
	.quad	44,	43,	21,	14	# [1][1] [2][2] [3][3] [4][4]
iotas:
	.quad	0x0000000000000001, 0x0000000000000001, 0x0000000000000001, 0x0000000000000001
	.quad	0x0000000000008082, 0x0000000000008082, 0x0000000000008082, 0x0000000000008082
	.quad	0x800000000000808a, 0x800000000000808a, 0x800000000000808a, 0x800000000000808a
	.quad	0x8000000080008000, 0x8000000080008000, 0x8000000080008000, 0x8000000080008000
	.quad	0x000000000000808b, 0x000000000000808b, 0x000000000000808b, 0x000000000000808b
	.quad	0x0000000080000001, 0x0000000080000001, 0x0000000080000001, 0x0000000080000001
	.quad	0x8000000080008081, 0x8000000080008081, 0x8000000080008081, 0x8000000080008081
	.quad	0x8000000000008009, 0x8000000000008009, 0x8000000000008009, 0x8000000000008009
	.quad	0x000000000000008a, 0x000000000000008a, 0x000000000000008a, 0x000000000000008a
	.quad	0x0000000000000088, 0x0000000000000088, 0x0000000000000088, 0x0000000000000088
	.quad	0x0000000080008009, 0x0000000080008009, 0x0000000080008009, 0x0000000080008009
	.quad	0x000000008000000a, 0x000000008000000a, 0x000000008000000a, 0x000000008000000a
	.quad	0x000000008000808b, 0x000000008000808b, 0x000000008000808b, 0x000000008000808b
	.quad	0x800000000000008b, 0x800000000000008b, 0x800000000000008b, 0x800000000000008b
	.quad	0x8000000000008089, 0x8000000000008089, 0x8000000000008089, 0x8000000000008089
	.quad	0x8000000000008003, 0x8000000000008003, 0x8000000000008003, 0x8000000000008003
	.quad	0x8000000000008002, 0x8000000000008002, 0x8000000000008002, 0x8000000000008002
	.quad	0x8000000000000080, 0x8000000000000080, 0x8000000000000080, 0x8000000000000080
	.quad	0x000000000000800a, 0x000000000000800a, 0x000000000000800a, 0x000000000000800a
	.quad	0x800000008000000a, 0x800000008000000a, 0x800000008000000a, 0x800000008000000a
	.quad	0x8000000080008081, 0x8000000080008081, 0x8000000080008081, 0x8000000080008081
	.quad	0x8000000000008080, 0x8000000000008080, 0x8000000000008080, 0x8000000000008080
	.quad	0x0000000080000001, 0x0000000080000001, 0x0000000080000001, 0x0000000080000001
	.quad	0x8000000080008008, 0x8000000080008008, 0x8000000080008008, 0x8000000080008008

.asciz	"Keccak-1600 absorb and squeeze for AVX512VL, CRYPTOGAMS by <appro\@openssl.org>"
___

$output=pop;
open STDOUT,">$output";
print $code;
close STDOUT;

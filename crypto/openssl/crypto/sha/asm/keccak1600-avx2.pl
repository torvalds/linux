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
# Keccak-1600 for AVX2.
#
# July 2017.
#
# To paraphrase Gilles Van Assche, if you contemplate Fig. 2.3 on page
# 20 of The Keccak reference [or Fig. 5 of FIPS PUB 202], and load data
# other than A[0][0] in magic order into 6 [256-bit] registers, *each
# dedicated to one axis*, Pi permutation is reduced to intra-register
# shuffles...
#
# It makes other steps more intricate, but overall, is it a win? To be
# more specific index permutations organized by quadruples are:
#
#       [4][4] [3][3] [2][2] [1][1]<-+
#       [0][4] [0][3] [0][2] [0][1]<-+
#       [3][0] [1][0] [4][0] [2][0]  |
#       [4][3] [3][1] [2][4] [1][2]  |
#       [3][4] [1][3] [4][2] [2][1]  |
#       [2][3] [4][1] [1][4] [3][2]  |
#       [2][2] [4][4] [1][1] [3][3] -+
#
# This however is highly impractical for Theta and Chi. What would help
# Theta is if x indices were aligned column-wise, or in other words:
#
#       [0][4] [0][3] [0][2] [0][1]
#       [3][0] [1][0] [4][0] [2][0]
#vpermq([4][3] [3][1] [2][4] [1][2], 0b01110010)
#       [2][4] [4][3] [1][2] [3][1]
#vpermq([4][2] [3][4] [2][1] [1][3], 0b10001101)
#       [3][4] [1][3] [4][2] [2][1]
#vpermq([2][3] [4][1] [1][4] [3][2], 0b01110010)
#       [1][4] [2][3] [3][2] [4][1]
#vpermq([1][1] [2][2] [3][3] [4][4], 0b00011011)
#       [4][4] [3][3] [2][2] [1][1]
#
# So here we have it, lines not marked with vpermq() represent the magic
# order in which data is to be loaded and maintained. [And lines marked
# with vpermq() represent Pi circular permutation in chosen layout. Note
# that first step is permutation-free.] A[0][0] is loaded to register of
# its own, to all lanes. [A[0][0] is not part of Pi permutation or Rho.]
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

# But on the other hand Chi is much better off if y indices were aligned
# column-wise, not x. For this reason we have to shuffle data prior
# Chi and revert it afterwards. Prior shuffle is naturally merged with
# Pi itself:
#
#       [0][4] [0][3] [0][2] [0][1]
#       [3][0] [1][0] [4][0] [2][0]
#vpermq([4][3] [3][1] [2][4] [1][2], 0b01110010)
#vpermq([2][4] [4][3] [1][2] [3][1], 0b00011011) = 0b10001101
#       [3][1] [1][2] [4][3] [2][4]
#vpermq([4][2] [3][4] [2][1] [1][3], 0b10001101)
#vpermq([3][4] [1][3] [4][2] [2][1], 0b11100100) = 0b10001101
#       [3][4] [1][3] [4][2] [2][1]
#vpermq([2][3] [4][1] [1][4] [3][2], 0b01110010)
#vpermq([1][4] [2][3] [3][2] [4][1], 0b01110010) = 0b00011011
#       [3][2] [1][4] [4][1] [2][3]
#vpermq([1][1] [2][2] [3][3] [4][4], 0b00011011)
#vpermq([4][4] [3][3] [2][2] [1][1], 0b10001101) = 0b01110010
#       [3][3] [1][1] [4][4] [2][2]
#
# And reverse post-Chi permutation:
#
#       [0][4] [0][3] [0][2] [0][1]
#       [3][0] [1][0] [4][0] [2][0]
#vpermq([3][1] [1][2] [4][3] [2][4], 0b00011011)
#       [2][4] [4][3] [1][2] [3][1]
#vpermq([3][4] [1][3] [4][2] [2][1], 0b11100100) = nop :-)
#       [3][4] [1][3] [4][2] [2][1]
#vpermq([3][2] [1][4] [4][1] [2][3], 0b10001101)
#       [1][4] [2][3] [3][2] [4][1]
#vpermq([3][3] [1][1] [4][4] [2][2], 0b01110010)
#       [4][4] [3][3] [2][2] [1][1]
#
########################################################################
# Numbers are cycles per processed byte out of large message.
#
#			r=1088(*)
#
# Haswell		8.7/+10%
# Skylake		7.8/+20%
# Ryzen			17(**)
#
# (*)	Corresponds to SHA3-256. Percentage after slash is improvement
#	coefficient in comparison to scalar keccak1600-x86_64.pl.
# (**)	It's expected that Ryzen performs poorly, because instruction
#	issue rate is limited to two AVX2 instructions per cycle and
#	in addition vpblendd is reportedly bound to specific port.
#	Obviously this code path should not be executed on Ryzen.

my @T = map("%ymm$_",(7..15));
my ($C14,$C00,$D00,$D14) = @T[5..8];

$code.=<<___;
.text

.type	__KeccakF1600,\@function
.align	32
__KeccakF1600:
	lea		rhotates_left+96(%rip),%r8
	lea		rhotates_right+96(%rip),%r9
	lea		iotas(%rip),%r10
	mov		\$24,%eax
	jmp		.Loop_avx2

.align	32
.Loop_avx2:
	######################################### Theta
	vpshufd		\$0b01001110,$A20,$C00
	vpxor		$A31,$A41,$C14
	vpxor		$A11,$A21,@T[2]
	vpxor		$A01,$C14,$C14
	vpxor		@T[2],$C14,$C14		# C[1..4]

	vpermq		\$0b10010011,$C14,@T[4]
	vpxor		$A20,$C00,$C00
	vpermq		\$0b01001110,$C00,@T[0]

	vpsrlq		\$63,$C14,@T[1]
	vpaddq		$C14,$C14,@T[2]
	vpor		@T[2],@T[1],@T[1]	# ROL64(C[1..4],1)

	vpermq		\$0b00111001,@T[1],$D14
	vpxor		@T[4],@T[1],$D00
	vpermq		\$0b00000000,$D00,$D00	# D[0..0] = ROL64(C[1],1) ^ C[4]

	vpxor		$A00,$C00,$C00
	vpxor		@T[0],$C00,$C00		# C[0..0]

	vpsrlq		\$63,$C00,@T[0]
	vpaddq		$C00,$C00,@T[1]
	vpor		@T[0],@T[1],@T[1]	# ROL64(C[0..0],1)

	vpxor		$D00,$A20,$A20		# ^= D[0..0]
	vpxor		$D00,$A00,$A00		# ^= D[0..0]

	vpblendd	\$0b11000000,@T[1],$D14,$D14
	vpblendd	\$0b00000011,$C00,@T[4],@T[4]
	vpxor		@T[4],$D14,$D14		# D[1..4] = ROL64(C[2..4,0),1) ^ C[0..3]

	######################################### Rho + Pi + pre-Chi shuffle
	vpsllvq		0*32-96(%r8),$A20,@T[3]
	vpsrlvq		0*32-96(%r9),$A20,$A20
	vpor		@T[3],$A20,$A20

	 vpxor		$D14,$A31,$A31		# ^= D[1..4] from Theta
	vpsllvq		2*32-96(%r8),$A31,@T[4]
	vpsrlvq		2*32-96(%r9),$A31,$A31
	vpor		@T[4],$A31,$A31

	 vpxor		$D14,$A21,$A21		# ^= D[1..4] from Theta
	vpsllvq		3*32-96(%r8),$A21,@T[5]
	vpsrlvq		3*32-96(%r9),$A21,$A21
	vpor		@T[5],$A21,$A21

	 vpxor		$D14,$A41,$A41		# ^= D[1..4] from Theta
	vpsllvq		4*32-96(%r8),$A41,@T[6]
	vpsrlvq		4*32-96(%r9),$A41,$A41
	vpor		@T[6],$A41,$A41

	 vpxor		$D14,$A11,$A11		# ^= D[1..4] from Theta
	 vpermq		\$0b10001101,$A20,@T[3]	# $A20 -> future $A31
	 vpermq		\$0b10001101,$A31,@T[4]	# $A31 -> future $A21
	vpsllvq		5*32-96(%r8),$A11,@T[7]
	vpsrlvq		5*32-96(%r9),$A11,@T[1]
	vpor		@T[7],@T[1],@T[1]	# $A11 -> future $A01

	 vpxor		$D14,$A01,$A01		# ^= D[1..4] from Theta
	 vpermq		\$0b00011011,$A21,@T[5]	# $A21 -> future $A41
	 vpermq		\$0b01110010,$A41,@T[6]	# $A41 -> future $A11
	vpsllvq		1*32-96(%r8),$A01,@T[8]
	vpsrlvq		1*32-96(%r9),$A01,@T[2]
	vpor		@T[8],@T[2],@T[2]	# $A01 -> future $A20

	######################################### Chi
	vpsrldq		\$8,@T[1],@T[7]
	vpandn		@T[7],@T[1],@T[0]	# tgting  [0][0] [0][0] [0][0] [0][0]

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
	vpandn		@T[8],$A31,$A31		# tgting  [3][1] [1][2] [4][3] [2][4]
	 vpandn		@T[7],$A41,$A41		# tgting  [3][2] [1][4] [4][1] [2][3]

	vpblendd	\$0b00001100,@T[2],@T[5],$A11	#               [4][0] [2][3]
	vpblendd	\$0b00001100,@T[5],@T[3],@T[8]	#               [4][1] [2][4]
	 vpxor		@T[3],$A31,$A31
	vpblendd	\$0b00110000,@T[3],$A11,$A11	#        [1][2] [4][0] [2][3]
	vpblendd	\$0b00110000,@T[4],@T[8],@T[8]	#        [1][3] [4][1] [2][4]
	 vpxor		@T[5],$A41,$A41
	vpblendd	\$0b11000000,@T[4],$A11,$A11	# [3][4] [1][2] [4][0] [2][3]
	vpblendd	\$0b11000000,@T[2],@T[8],@T[8]	# [3][0] [1][3] [4][1] [2][4]
	vpandn		@T[8],$A11,$A11		# tgting  [3][3] [1][1] [4][4] [2][2]
	vpxor		@T[6],$A11,$A11

	  vpermq	\$0b00011110,@T[1],$A21		# [0][1] [0][2] [0][4] [0][3]
	  vpblendd	\$0b00110000,$A00,$A21,@T[8]	# [0][1] [0][0] [0][4] [0][3]
	  vpermq	\$0b00111001,@T[1],$A01		# [0][1] [0][4] [0][3] [0][2]
	  vpblendd	\$0b11000000,$A00,$A01,$A01	# [0][0] [0][4] [0][3] [0][2]
	  vpandn	@T[8],$A01,$A01		# tgting  [0][4] [0][3] [0][2] [0][1]

	vpblendd	\$0b00001100,@T[5],@T[4],$A20	#               [4][1] [2][1]
	vpblendd	\$0b00001100,@T[4],@T[6],@T[7]	#               [4][2] [2][2]
	vpblendd	\$0b00110000,@T[6],$A20,$A20	#        [1][1] [4][1] [2][1]
	vpblendd	\$0b00110000,@T[3],@T[7],@T[7]	#        [1][2] [4][2] [2][2]
	vpblendd	\$0b11000000,@T[3],$A20,$A20	# [3][1] [1][1] [4][1] [2][1]
	vpblendd	\$0b11000000,@T[5],@T[7],@T[7]	# [3][2] [1][2] [4][2] [2][2]
	vpandn		@T[7],$A20,$A20		# tgting  [3][0] [1][0] [4][0] [2][0]
	vpxor		@T[2],$A20,$A20

	 vpermq		\$0b00000000,@T[0],@T[0]	# [0][0] [0][0] [0][0] [0][0]
	 vpermq		\$0b00011011,$A31,$A31	# post-Chi shuffle
	 vpermq		\$0b10001101,$A41,$A41
	 vpermq		\$0b01110010,$A11,$A11

	vpblendd	\$0b00001100,@T[3],@T[6],$A21	#               [4][3] [2][2]
	vpblendd	\$0b00001100,@T[6],@T[5],@T[7]	#               [4][4] [2][3]
	vpblendd	\$0b00110000,@T[5],$A21,$A21	#        [1][4] [4][3] [2][2]
	vpblendd	\$0b00110000,@T[2],@T[7],@T[7]	#        [1][0] [4][4] [2][3]
	vpblendd	\$0b11000000,@T[2],$A21,$A21	# [3][0] [1][4] [4][3] [2][2]
	vpblendd	\$0b11000000,@T[3],@T[7],@T[7]	# [3][1] [1][0] [4][4] [2][3]
	vpandn		@T[7],$A21,$A21		# tgting  [3][4] [1][3] [4][2] [2][1]

	vpxor		@T[0],$A00,$A00
	vpxor		@T[1],$A01,$A01
	vpxor		@T[4],$A21,$A21

	######################################### Iota
	vpxor		(%r10),$A00,$A00
	lea		32(%r10),%r10

	dec		%eax
	jnz		.Loop_avx2

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

	vzeroupper

	vpbroadcastq	-96($A_flat),$A00	# load A[5][5]
	vmovdqu		8+32*0-96($A_flat),$A01
	vmovdqu		8+32*1-96($A_flat),$A20
	vmovdqu		8+32*2-96($A_flat),$A31
	vmovdqu		8+32*3-96($A_flat),$A21
	vmovdqu		8+32*4-96($A_flat),$A41
	vmovdqu		8+32*5-96($A_flat),$A11

	vpxor		@T[0],@T[0],@T[0]
	vmovdqa		@T[0],32*2-96(%r10)	# zero transfer area on stack
	vmovdqa		@T[0],32*3-96(%r10)
	vmovdqa		@T[0],32*4-96(%r10)
	vmovdqa		@T[0],32*5-96(%r10)
	vmovdqa		@T[0],32*6-96(%r10)

.Loop_absorb_avx2:
	mov		$bsz,%rax
	sub		$bsz,$len
	jc		.Ldone_absorb_avx2

	shr		\$3,%eax
	vpbroadcastq	0-96($inp),@T[0]
	vmovdqu		8-96($inp),@T[1]
	sub		\$4,%eax
___
for(my $i=5; $i<25; $i++) {
$code.=<<___
	dec	%eax
	jz	.Labsorved_avx2
	mov	8*$i-96($inp),%r8
	mov	%r8,$A_jagged[$i]-96(%r10)
___
}
$code.=<<___;
.Labsorved_avx2:
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
	jmp	.Loop_absorb_avx2

.Ldone_absorb_avx2:
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

	mov	$bsz,%rax

.Loop_squeeze_avx2:
	mov	@A_jagged[$i]-96($A_flat),%r8
___
for (my $i=0; $i<25; $i++) {
$code.=<<___;
	sub	\$8,$len
	jc	.Ltail_squeeze_avx2
	mov	%r8,($out)
	lea	8($out),$out
	je	.Ldone_squeeze_avx2
	dec	%eax
	je	.Lextend_output_avx2
	mov	@A_jagged[$i+1]-120($A_flat),%r8
___
}
$code.=<<___;
.Lextend_output_avx2:
	call	__KeccakF1600

	vmovq	%xmm0,-96($A_flat)
	vmovdqu	$A01,8+32*0-96($A_flat)
	vmovdqu	$A20,8+32*1-96($A_flat)
	vmovdqu	$A31,8+32*2-96($A_flat)
	vmovdqu	$A21,8+32*3-96($A_flat)
	vmovdqu	$A41,8+32*4-96($A_flat)
	vmovdqu	$A11,8+32*5-96($A_flat)

	mov	$bsz,%rax
	jmp	.Loop_squeeze_avx2


.Ltail_squeeze_avx2:
	add	\$8,$len
.Loop_tail_avx2:
	mov	%r8b,($out)
	lea	1($out),$out
	shr	\$8,%r8
	dec	$len
	jnz	.Loop_tail_avx2

.Ldone_squeeze_avx2:
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
rhotates_right:
	.quad	64-3,	64-18,	64-36,	64-41
	.quad	64-1,	64-62,	64-28,	64-27
	.quad	64-45,	64-6,	64-56,	64-39
	.quad	64-10,	64-61,	64-55,	64-8
	.quad	64-2,	64-15,	64-25,	64-20
	.quad	64-44,	64-43,	64-21,	64-14
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

.asciz	"Keccak-1600 absorb and squeeze for AVX2, CRYPTOGAMS by <appro\@openssl.org>"
___

$output=pop;
open STDOUT,">$output";
print $code;
close STDOUT;

#! /usr/bin/env perl
# Copyright 2014-2018 The OpenSSL Project Authors. All Rights Reserved.
# Copyright (c) 2014, Intel Corporation. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html
#
# Originally written by Shay Gueron (1, 2), and Vlad Krasnov (1)
# (1) Intel Corporation, Israel Development Center, Haifa, Israel
# (2) University of Haifa, Israel
#
# Reference:
# S.Gueron and V.Krasnov, "Fast Prime Field Elliptic Curve Cryptography with
#                          256 Bit Primes"

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

if (`$ENV{CC} -Wa,-v -c -o /dev/null -x assembler /dev/null 2>&1`
		=~ /GNU assembler version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.19) + ($1>=2.22);
	$addx = ($1>=2.23);
}

if (!$addx && $win64 && ($flavour =~ /nasm/ || $ENV{ASM} =~ /nasm/) &&
	    `nasm -v 2>&1` =~ /NASM version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.09) + ($1>=2.10);
	$addx = ($1>=2.10);
}

if (!$addx && $win64 && ($flavour =~ /masm/ || $ENV{ASM} =~ /ml64/) &&
	    `ml64 2>&1` =~ /Version ([0-9]+)\./) {
	$avx = ($1>=10) + ($1>=11);
	$addx = ($1>=12);
}

if (!$addx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|based on LLVM) ([3-9])\.([0-9]+)/) {
	my $ver = $2 + $3/100.0;	# 3.1->3.01, 3.10->3.10
	$avx = ($ver>=3.0) + ($ver>=3.01);
	$addx = ($ver>=3.03);
}

if ($avx>=2) {{
$digit_size = "\$29";
$n_digits = "\$9";

$code.=<<___;
.text

.align 64
.LAVX2_AND_MASK:
.LAVX2_POLY:
.quad 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff
.quad 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff
.quad 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff
.quad 0x000001ff, 0x000001ff, 0x000001ff, 0x000001ff
.quad 0x00000000, 0x00000000, 0x00000000, 0x00000000
.quad 0x00000000, 0x00000000, 0x00000000, 0x00000000
.quad 0x00040000, 0x00040000, 0x00040000, 0x00040000
.quad 0x1fe00000, 0x1fe00000, 0x1fe00000, 0x1fe00000
.quad 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff

.LAVX2_POLY_x2:
.quad 0x7FFFFFFC, 0x7FFFFFFC, 0x7FFFFFFC, 0x7FFFFFFC
.quad 0x7FFFFFFC, 0x7FFFFFFC, 0x7FFFFFFC, 0x7FFFFFFC
.quad 0x7FFFFFFC, 0x7FFFFFFC, 0x7FFFFFFC, 0x7FFFFFFC
.quad 0x400007FC, 0x400007FC, 0x400007FC, 0x400007FC
.quad 0x3FFFFFFE, 0x3FFFFFFE, 0x3FFFFFFE, 0x3FFFFFFE
.quad 0x3FFFFFFE, 0x3FFFFFFE, 0x3FFFFFFE, 0x3FFFFFFE
.quad 0x400FFFFE, 0x400FFFFE, 0x400FFFFE, 0x400FFFFE
.quad 0x7F7FFFFE, 0x7F7FFFFE, 0x7F7FFFFE, 0x7F7FFFFE
.quad 0x03FFFFFC, 0x03FFFFFC, 0x03FFFFFC, 0x03FFFFFC

.LAVX2_POLY_x8:
.quad 0xFFFFFFF8, 0xFFFFFFF8, 0xFFFFFFF8, 0xFFFFFFF8
.quad 0xFFFFFFF8, 0xFFFFFFF8, 0xFFFFFFF8, 0xFFFFFFF8
.quad 0xFFFFFFF8, 0xFFFFFFF8, 0xFFFFFFF8, 0xFFFFFFF8
.quad 0x80000FF8, 0x80000FF8, 0x80000FF8, 0x80000FF8
.quad 0x7FFFFFFC, 0x7FFFFFFC, 0x7FFFFFFC, 0x7FFFFFFC
.quad 0x7FFFFFFC, 0x7FFFFFFC, 0x7FFFFFFC, 0x7FFFFFFC
.quad 0x801FFFFC, 0x801FFFFC, 0x801FFFFC, 0x801FFFFC
.quad 0xFEFFFFFC, 0xFEFFFFFC, 0xFEFFFFFC, 0xFEFFFFFC
.quad 0x07FFFFF8, 0x07FFFFF8, 0x07FFFFF8, 0x07FFFFF8

.LONE:
.quad 0x00000020, 0x00000020, 0x00000020, 0x00000020
.quad 0x00000000, 0x00000000, 0x00000000, 0x00000000
.quad 0x00000000, 0x00000000, 0x00000000, 0x00000000
.quad 0x1fffc000, 0x1fffc000, 0x1fffc000, 0x1fffc000
.quad 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff
.quad 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff
.quad 0x1f7fffff, 0x1f7fffff, 0x1f7fffff, 0x1f7fffff
.quad 0x03ffffff, 0x03ffffff, 0x03ffffff, 0x03ffffff
.quad 0x00000000, 0x00000000, 0x00000000, 0x00000000

# RR = 2^266 mod p in AVX2 format, to transform from the native OpenSSL
# Montgomery form (*2^256) to our format (*2^261)

.LTO_MONT_AVX2:
.quad 0x00000400, 0x00000400, 0x00000400, 0x00000400
.quad 0x00000000, 0x00000000, 0x00000000, 0x00000000
.quad 0x00000000, 0x00000000, 0x00000000, 0x00000000
.quad 0x1ff80000, 0x1ff80000, 0x1ff80000, 0x1ff80000
.quad 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff
.quad 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff
.quad 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff
.quad 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff
.quad 0x00000003, 0x00000003, 0x00000003, 0x00000003

.LFROM_MONT_AVX2:
.quad 0x00000001, 0x00000001, 0x00000001, 0x00000001
.quad 0x00000000, 0x00000000, 0x00000000, 0x00000000
.quad 0x00000000, 0x00000000, 0x00000000, 0x00000000
.quad 0x1ffffe00, 0x1ffffe00, 0x1ffffe00, 0x1ffffe00
.quad 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff
.quad 0x1fffffff, 0x1fffffff, 0x1fffffff, 0x1fffffff
.quad 0x1ffbffff, 0x1ffbffff, 0x1ffbffff, 0x1ffbffff
.quad 0x001fffff, 0x001fffff, 0x001fffff, 0x001fffff
.quad 0x00000000, 0x00000000, 0x00000000, 0x00000000

.LIntOne:
.long 1,1,1,1,1,1,1,1
___

{
# This function receives a pointer to an array of four affine points
# (X, Y, <1>) and rearranges the data for AVX2 execution, while
# converting it to 2^29 radix redundant form

my ($X0,$X1,$X2,$X3, $Y0,$Y1,$Y2,$Y3,
    $T0,$T1,$T2,$T3, $T4,$T5,$T6,$T7)=map("%ymm$_",(0..15));

$code.=<<___;
.globl	ecp_nistz256_avx2_transpose_convert
.type	ecp_nistz256_avx2_transpose_convert,\@function,2
.align 64
ecp_nistz256_avx2_transpose_convert:
	vzeroupper
___
$code.=<<___	if ($win64);
	lea	-8-16*10(%rsp), %rsp
	vmovaps	%xmm6, -8-16*10(%rax)
	vmovaps	%xmm7, -8-16*9(%rax)
	vmovaps	%xmm8, -8-16*8(%rax)
	vmovaps	%xmm9, -8-16*7(%rax)
	vmovaps	%xmm10, -8-16*6(%rax)
	vmovaps	%xmm11, -8-16*5(%rax)
	vmovaps	%xmm12, -8-16*4(%rax)
	vmovaps	%xmm13, -8-16*3(%rax)
	vmovaps	%xmm14, -8-16*2(%rax)
	vmovaps	%xmm15, -8-16*1(%rax)
___
$code.=<<___;
	# Load the data
	vmovdqa		32*0(%rsi), $X0
	lea		112(%rsi), %rax		# size optimization
	vmovdqa		32*1(%rsi), $Y0
	lea		.LAVX2_AND_MASK(%rip), %rdx
	vmovdqa		32*2(%rsi), $X1
	vmovdqa		32*3(%rsi), $Y1
	vmovdqa		32*4-112(%rax), $X2
	vmovdqa		32*5-112(%rax), $Y2
	vmovdqa		32*6-112(%rax), $X3
	vmovdqa		32*7-112(%rax), $Y3

	# Transpose X and Y independently
	vpunpcklqdq	$X1, $X0, $T0		# T0 = [B2 A2 B0 A0]
	vpunpcklqdq	$X3, $X2, $T1		# T1 = [D2 C2 D0 C0]
	vpunpckhqdq	$X1, $X0, $T2		# T2 = [B3 A3 B1 A1]
	vpunpckhqdq	$X3, $X2, $T3		# T3 = [D3 C3 D1 C1]

	vpunpcklqdq	$Y1, $Y0, $T4
	vpunpcklqdq	$Y3, $Y2, $T5
	vpunpckhqdq	$Y1, $Y0, $T6
	vpunpckhqdq	$Y3, $Y2, $T7

	vperm2i128	\$0x20, $T1, $T0, $X0	# X0 = [D0 C0 B0 A0]
	vperm2i128	\$0x20, $T3, $T2, $X1	# X1 = [D1 C1 B1 A1]
	vperm2i128	\$0x31, $T1, $T0, $X2	# X2 = [D2 C2 B2 A2]
	vperm2i128	\$0x31, $T3, $T2, $X3	# X3 = [D3 C3 B3 A3]

	vperm2i128	\$0x20, $T5, $T4, $Y0
	vperm2i128	\$0x20, $T7, $T6, $Y1
	vperm2i128	\$0x31, $T5, $T4, $Y2
	vperm2i128	\$0x31, $T7, $T6, $Y3
	vmovdqa		(%rdx), $T7

	vpand		(%rdx), $X0, $T0	# out[0] = in[0] & mask;
	vpsrlq		\$29, $X0, $X0
	vpand		$T7, $X0, $T1		# out[1] = (in[0] >> shift) & mask;
	vpsrlq		\$29, $X0, $X0
	vpsllq		\$6, $X1, $T2
	vpxor		$X0, $T2, $T2
	vpand		$T7, $T2, $T2		# out[2] = ((in[0] >> (shift*2)) ^ (in[1] << (64-shift*2))) & mask;
	vpsrlq		\$23, $X1, $X1
	vpand		$T7, $X1, $T3		# out[3] = (in[1] >> ((shift*3)%64)) & mask;
	vpsrlq		\$29, $X1, $X1
	vpsllq		\$12, $X2, $T4
	vpxor		$X1, $T4, $T4
	vpand		$T7, $T4, $T4		# out[4] = ((in[1] >> ((shift*4)%64)) ^ (in[2] << (64*2-shift*4))) & mask;
	vpsrlq		\$17, $X2, $X2
	vpand		$T7, $X2, $T5		# out[5] = (in[2] >> ((shift*5)%64)) & mask;
	vpsrlq		\$29, $X2, $X2
	vpsllq		\$18, $X3, $T6
	vpxor		$X2, $T6, $T6
	vpand		$T7, $T6, $T6		# out[6] = ((in[2] >> ((shift*6)%64)) ^ (in[3] << (64*3-shift*6))) & mask;
	vpsrlq		\$11, $X3, $X3
	 vmovdqa	$T0, 32*0(%rdi)
	 lea		112(%rdi), %rax		# size optimization
	vpand		$T7, $X3, $T0		# out[7] = (in[3] >> ((shift*7)%64)) & mask;
	vpsrlq		\$29, $X3, $X3		# out[8] = (in[3] >> ((shift*8)%64)) & mask;

	vmovdqa		$T1, 32*1(%rdi)
	vmovdqa		$T2, 32*2(%rdi)
	vmovdqa		$T3, 32*3(%rdi)
	vmovdqa		$T4, 32*4-112(%rax)
	vmovdqa		$T5, 32*5-112(%rax)
	vmovdqa		$T6, 32*6-112(%rax)
	vmovdqa		$T0, 32*7-112(%rax)
	vmovdqa		$X3, 32*8-112(%rax)
	lea		448(%rdi), %rax		# size optimization

	vpand		$T7, $Y0, $T0		# out[0] = in[0] & mask;
	vpsrlq		\$29, $Y0, $Y0
	vpand		$T7, $Y0, $T1		# out[1] = (in[0] >> shift) & mask;
	vpsrlq		\$29, $Y0, $Y0
	vpsllq		\$6, $Y1, $T2
	vpxor		$Y0, $T2, $T2
	vpand		$T7, $T2, $T2		# out[2] = ((in[0] >> (shift*2)) ^ (in[1] << (64-shift*2))) & mask;
	vpsrlq		\$23, $Y1, $Y1
	vpand		$T7, $Y1, $T3		# out[3] = (in[1] >> ((shift*3)%64)) & mask;
	vpsrlq		\$29, $Y1, $Y1
	vpsllq		\$12, $Y2, $T4
	vpxor		$Y1, $T4, $T4
	vpand		$T7, $T4, $T4		# out[4] = ((in[1] >> ((shift*4)%64)) ^ (in[2] << (64*2-shift*4))) & mask;
	vpsrlq		\$17, $Y2, $Y2
	vpand		$T7, $Y2, $T5		# out[5] = (in[2] >> ((shift*5)%64)) & mask;
	vpsrlq		\$29, $Y2, $Y2
	vpsllq		\$18, $Y3, $T6
	vpxor		$Y2, $T6, $T6
	vpand		$T7, $T6, $T6		# out[6] = ((in[2] >> ((shift*6)%64)) ^ (in[3] << (64*3-shift*6))) & mask;
	vpsrlq		\$11, $Y3, $Y3
	 vmovdqa	$T0, 32*9-448(%rax)
	vpand		$T7, $Y3, $T0		# out[7] = (in[3] >> ((shift*7)%64)) & mask;
	vpsrlq		\$29, $Y3, $Y3		# out[8] = (in[3] >> ((shift*8)%64)) & mask;

	vmovdqa		$T1, 32*10-448(%rax)
	vmovdqa		$T2, 32*11-448(%rax)
	vmovdqa		$T3, 32*12-448(%rax)
	vmovdqa		$T4, 32*13-448(%rax)
	vmovdqa		$T5, 32*14-448(%rax)
	vmovdqa		$T6, 32*15-448(%rax)
	vmovdqa		$T0, 32*16-448(%rax)
	vmovdqa		$Y3, 32*17-448(%rax)

	vzeroupper
___
$code.=<<___	if ($win64);
	movaps	16*0(%rsp), %xmm6
	movaps	16*1(%rsp), %xmm7
	movaps	16*2(%rsp), %xmm8
	movaps	16*3(%rsp), %xmm9
	movaps	16*4(%rsp), %xmm10
	movaps	16*5(%rsp), %xmm11
	movaps	16*6(%rsp), %xmm12
	movaps	16*7(%rsp), %xmm13
	movaps	16*8(%rsp), %xmm14
	movaps	16*9(%rsp), %xmm15
	lea	8+16*10(%rsp), %rsp
___
$code.=<<___;
	ret
.size	ecp_nistz256_avx2_transpose_convert,.-ecp_nistz256_avx2_transpose_convert
___
}
{
################################################################################
# This function receives a pointer to an array of four AVX2 formatted points
# (X, Y, Z) convert the data to normal representation, and rearranges the data

my ($D0,$D1,$D2,$D3, $D4,$D5,$D6,$D7, $D8)=map("%ymm$_",(0..8));
my ($T0,$T1,$T2,$T3, $T4,$T5,$T6)=map("%ymm$_",(9..15));

$code.=<<___;

.globl	ecp_nistz256_avx2_convert_transpose_back
.type	ecp_nistz256_avx2_convert_transpose_back,\@function,2
.align	32
ecp_nistz256_avx2_convert_transpose_back:
	vzeroupper
___
$code.=<<___	if ($win64);
	lea	-8-16*10(%rsp), %rsp
	vmovaps	%xmm6, -8-16*10(%rax)
	vmovaps	%xmm7, -8-16*9(%rax)
	vmovaps	%xmm8, -8-16*8(%rax)
	vmovaps	%xmm9, -8-16*7(%rax)
	vmovaps	%xmm10, -8-16*6(%rax)
	vmovaps	%xmm11, -8-16*5(%rax)
	vmovaps	%xmm12, -8-16*4(%rax)
	vmovaps	%xmm13, -8-16*3(%rax)
	vmovaps	%xmm14, -8-16*2(%rax)
	vmovaps	%xmm15, -8-16*1(%rax)
___
$code.=<<___;
	mov	\$3, %ecx

.Lconv_loop:
	vmovdqa		32*0(%rsi), $D0
	lea		160(%rsi), %rax		# size optimization
	vmovdqa		32*1(%rsi), $D1
	vmovdqa		32*2(%rsi), $D2
	vmovdqa		32*3(%rsi), $D3
	vmovdqa		32*4-160(%rax), $D4
	vmovdqa		32*5-160(%rax), $D5
	vmovdqa		32*6-160(%rax), $D6
	vmovdqa		32*7-160(%rax), $D7
	vmovdqa		32*8-160(%rax), $D8

	vpsllq		\$29, $D1, $D1
	vpsllq		\$58, $D2, $T0
	vpaddq		$D1, $D0, $D0
	vpaddq		$T0, $D0, $D0		# out[0] = (in[0]) ^ (in[1] << shift*1) ^ (in[2] << shift*2);

	vpsrlq		\$6, $D2, $D2
	vpsllq		\$23, $D3, $D3
	vpsllq		\$52, $D4, $T1
	vpaddq		$D2, $D3, $D3
	vpaddq		$D3, $T1, $D1		# out[1] = (in[2] >> (64*1-shift*2)) ^ (in[3] << shift*3%64) ^ (in[4] << shift*4%64);

	vpsrlq		\$12, $D4, $D4
	vpsllq		\$17, $D5, $D5
	vpsllq		\$46, $D6, $T2
	vpaddq		$D4, $D5, $D5
	vpaddq		$D5, $T2, $D2		# out[2] = (in[4] >> (64*2-shift*4)) ^ (in[5] << shift*5%64) ^ (in[6] << shift*6%64);

	vpsrlq		\$18, $D6, $D6
	vpsllq		\$11, $D7, $D7
	vpsllq		\$40, $D8, $T3
	vpaddq		$D6, $D7, $D7
	vpaddq		$D7, $T3, $D3		# out[3] = (in[6] >> (64*3-shift*6)) ^ (in[7] << shift*7%64) ^ (in[8] << shift*8%64);

	vpunpcklqdq	$D1, $D0, $T0		# T0 = [B2 A2 B0 A0]
	vpunpcklqdq	$D3, $D2, $T1		# T1 = [D2 C2 D0 C0]
	vpunpckhqdq	$D1, $D0, $T2		# T2 = [B3 A3 B1 A1]
	vpunpckhqdq	$D3, $D2, $T3		# T3 = [D3 C3 D1 C1]

	vperm2i128	\$0x20, $T1, $T0, $D0	# X0 = [D0 C0 B0 A0]
	vperm2i128	\$0x20, $T3, $T2, $D1	# X1 = [D1 C1 B1 A1]
	vperm2i128	\$0x31, $T1, $T0, $D2	# X2 = [D2 C2 B2 A2]
	vperm2i128	\$0x31, $T3, $T2, $D3	# X3 = [D3 C3 B3 A3]

	vmovdqa		$D0, 32*0(%rdi)
	vmovdqa		$D1, 32*3(%rdi)
	vmovdqa		$D2, 32*6(%rdi)
	vmovdqa		$D3, 32*9(%rdi)

	lea		32*9(%rsi), %rsi
	lea		32*1(%rdi), %rdi

	dec	%ecx
	jnz	.Lconv_loop

	vzeroupper
___
$code.=<<___	if ($win64);
	movaps	16*0(%rsp), %xmm6
	movaps	16*1(%rsp), %xmm7
	movaps	16*2(%rsp), %xmm8
	movaps	16*3(%rsp), %xmm9
	movaps	16*4(%rsp), %xmm10
	movaps	16*5(%rsp), %xmm11
	movaps	16*6(%rsp), %xmm12
	movaps	16*7(%rsp), %xmm13
	movaps	16*8(%rsp), %xmm14
	movaps	16*9(%rsp), %xmm15
	lea	8+16*10(%rsp), %rsp
___
$code.=<<___;
	ret
.size	ecp_nistz256_avx2_convert_transpose_back,.-ecp_nistz256_avx2_convert_transpose_back
___
}
{
my ($r_ptr,$a_ptr,$b_ptr,$itr)=("%rdi","%rsi","%rdx","%ecx");
my ($ACC0,$ACC1,$ACC2,$ACC3,$ACC4,$ACC5,$ACC6,$ACC7,$ACC8)=map("%ymm$_",(0..8));
my ($B,$Y,$T0,$AND_MASK,$OVERFLOW)=map("%ymm$_",(9..13));

sub NORMALIZE {
my $ret=<<___;
	vpsrlq		$digit_size, $ACC0, $T0
	vpand		$AND_MASK, $ACC0, $ACC0
	vpaddq		$T0, $ACC1, $ACC1

	vpsrlq		$digit_size, $ACC1, $T0
	vpand		$AND_MASK, $ACC1, $ACC1
	vpaddq		$T0, $ACC2, $ACC2

	vpsrlq		$digit_size, $ACC2, $T0
	vpand		$AND_MASK, $ACC2, $ACC2
	vpaddq		$T0, $ACC3, $ACC3

	vpsrlq		$digit_size, $ACC3, $T0
	vpand		$AND_MASK, $ACC3, $ACC3
	vpaddq		$T0, $ACC4, $ACC4

	vpsrlq		$digit_size, $ACC4, $T0
	vpand		$AND_MASK, $ACC4, $ACC4
	vpaddq		$T0, $ACC5, $ACC5

	vpsrlq		$digit_size, $ACC5, $T0
	vpand		$AND_MASK, $ACC5, $ACC5
	vpaddq		$T0, $ACC6, $ACC6

	vpsrlq		$digit_size, $ACC6, $T0
	vpand		$AND_MASK, $ACC6, $ACC6
	vpaddq		$T0, $ACC7, $ACC7

	vpsrlq		$digit_size, $ACC7, $T0
	vpand		$AND_MASK, $ACC7, $ACC7
	vpaddq		$T0, $ACC8, $ACC8
	#vpand		$AND_MASK, $ACC8, $ACC8
___
    $ret;
}

sub STORE {
my $ret=<<___;
	vmovdqa		$ACC0, 32*0(%rdi)
	lea		160(%rdi), %rax		# size optimization
	vmovdqa		$ACC1, 32*1(%rdi)
	vmovdqa		$ACC2, 32*2(%rdi)
	vmovdqa		$ACC3, 32*3(%rdi)
	vmovdqa		$ACC4, 32*4-160(%rax)
	vmovdqa		$ACC5, 32*5-160(%rax)
	vmovdqa		$ACC6, 32*6-160(%rax)
	vmovdqa		$ACC7, 32*7-160(%rax)
	vmovdqa		$ACC8, 32*8-160(%rax)
___
    $ret;
}

$code.=<<___;
.type	avx2_normalize,\@abi-omnipotent
.align	32
avx2_normalize:
	vpsrlq		$digit_size, $ACC0, $T0
	vpand		$AND_MASK, $ACC0, $ACC0
	vpaddq		$T0, $ACC1, $ACC1

	vpsrlq		$digit_size, $ACC1, $T0
	vpand		$AND_MASK, $ACC1, $ACC1
	vpaddq		$T0, $ACC2, $ACC2

	vpsrlq		$digit_size, $ACC2, $T0
	vpand		$AND_MASK, $ACC2, $ACC2
	vpaddq		$T0, $ACC3, $ACC3

	vpsrlq		$digit_size, $ACC3, $T0
	vpand		$AND_MASK, $ACC3, $ACC3
	vpaddq		$T0, $ACC4, $ACC4

	vpsrlq		$digit_size, $ACC4, $T0
	vpand		$AND_MASK, $ACC4, $ACC4
	vpaddq		$T0, $ACC5, $ACC5

	vpsrlq		$digit_size, $ACC5, $T0
	vpand		$AND_MASK, $ACC5, $ACC5
	vpaddq		$T0, $ACC6, $ACC6

	vpsrlq		$digit_size, $ACC6, $T0
	vpand		$AND_MASK, $ACC6, $ACC6
	vpaddq		$T0, $ACC7, $ACC7

	vpsrlq		$digit_size, $ACC7, $T0
	vpand		$AND_MASK, $ACC7, $ACC7
	vpaddq		$T0, $ACC8, $ACC8
	#vpand		$AND_MASK, $ACC8, $ACC8

	ret
.size	avx2_normalize,.-avx2_normalize

.type	avx2_normalize_n_store,\@abi-omnipotent
.align	32
avx2_normalize_n_store:
	vpsrlq		$digit_size, $ACC0, $T0
	vpand		$AND_MASK, $ACC0, $ACC0
	vpaddq		$T0, $ACC1, $ACC1

	vpsrlq		$digit_size, $ACC1, $T0
	vpand		$AND_MASK, $ACC1, $ACC1
	 vmovdqa	$ACC0, 32*0(%rdi)
	 lea		160(%rdi), %rax		# size optimization
	vpaddq		$T0, $ACC2, $ACC2

	vpsrlq		$digit_size, $ACC2, $T0
	vpand		$AND_MASK, $ACC2, $ACC2
	 vmovdqa	$ACC1, 32*1(%rdi)
	vpaddq		$T0, $ACC3, $ACC3

	vpsrlq		$digit_size, $ACC3, $T0
	vpand		$AND_MASK, $ACC3, $ACC3
	 vmovdqa	$ACC2, 32*2(%rdi)
	vpaddq		$T0, $ACC4, $ACC4

	vpsrlq		$digit_size, $ACC4, $T0
	vpand		$AND_MASK, $ACC4, $ACC4
	 vmovdqa	$ACC3, 32*3(%rdi)
	vpaddq		$T0, $ACC5, $ACC5

	vpsrlq		$digit_size, $ACC5, $T0
	vpand		$AND_MASK, $ACC5, $ACC5
	 vmovdqa	$ACC4, 32*4-160(%rax)
	vpaddq		$T0, $ACC6, $ACC6

	vpsrlq		$digit_size, $ACC6, $T0
	vpand		$AND_MASK, $ACC6, $ACC6
	 vmovdqa	$ACC5, 32*5-160(%rax)
	vpaddq		$T0, $ACC7, $ACC7

	vpsrlq		$digit_size, $ACC7, $T0
	vpand		$AND_MASK, $ACC7, $ACC7
	 vmovdqa	$ACC6, 32*6-160(%rax)
	vpaddq		$T0, $ACC8, $ACC8
	#vpand		$AND_MASK, $ACC8, $ACC8
	 vmovdqa	$ACC7, 32*7-160(%rax)
	 vmovdqa	$ACC8, 32*8-160(%rax)

	ret
.size	avx2_normalize_n_store,.-avx2_normalize_n_store

################################################################################
# void avx2_mul_x4(void* RESULTx4, void *Ax4, void *Bx4);
.type	avx2_mul_x4,\@abi-omnipotent
.align	32
avx2_mul_x4:
	lea	.LAVX2_POLY(%rip), %rax

	vpxor	$ACC0, $ACC0, $ACC0
	vpxor	$ACC1, $ACC1, $ACC1
	vpxor	$ACC2, $ACC2, $ACC2
	vpxor	$ACC3, $ACC3, $ACC3
	vpxor	$ACC4, $ACC4, $ACC4
	vpxor	$ACC5, $ACC5, $ACC5
	vpxor	$ACC6, $ACC6, $ACC6
	vpxor	$ACC7, $ACC7, $ACC7

	vmovdqa	32*7(%rax), %ymm14
	vmovdqa	32*8(%rax), %ymm15

	mov	$n_digits, $itr
	lea	-512($a_ptr), $a_ptr	# strategic bias to control u-op density
	jmp	.Lavx2_mul_x4_loop

.align	32
.Lavx2_mul_x4_loop:
	vmovdqa		32*0($b_ptr), $B
	lea		32*1($b_ptr), $b_ptr

	vpmuludq	32*0+512($a_ptr), $B, $T0
	vpmuludq	32*1+512($a_ptr), $B, $OVERFLOW	# borrow $OVERFLOW
	vpaddq		$T0, $ACC0, $ACC0
	vpmuludq	32*2+512($a_ptr), $B, $T0
	vpaddq		$OVERFLOW, $ACC1, $ACC1
	 vpand		$AND_MASK, $ACC0, $Y
	vpmuludq	32*3+512($a_ptr), $B, $OVERFLOW
	vpaddq		$T0, $ACC2, $ACC2
	vpmuludq	32*4+512($a_ptr), $B, $T0
	vpaddq		$OVERFLOW, $ACC3, $ACC3
	vpmuludq	32*5+512($a_ptr), $B, $OVERFLOW
	vpaddq		$T0, $ACC4, $ACC4
	vpmuludq	32*6+512($a_ptr), $B, $T0
	vpaddq		$OVERFLOW, $ACC5, $ACC5
	vpmuludq	32*7+512($a_ptr), $B, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC6

	# Skip some multiplications, optimizing for the constant poly
	vpmuludq	$AND_MASK, $Y, $T0
	 vpaddq		$OVERFLOW, $ACC7, $ACC7
	 vpmuludq	32*8+512($a_ptr), $B, $ACC8
	vpaddq		$T0, $ACC0, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	.byte		0x67
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $OVERFLOW
	.byte		0x67
	vmovdqa		$ACC5, $ACC4
	vpmuludq	%ymm14, $Y, $T0
	vpaddq		$OVERFLOW, $ACC6, $ACC5
	vpmuludq	%ymm15, $Y, $OVERFLOW
	vpaddq		$T0, $ACC7, $ACC6
	vpaddq		$OVERFLOW, $ACC8, $ACC7

	dec	$itr
	jnz	.Lavx2_mul_x4_loop

	vpxor	$ACC8, $ACC8, $ACC8

	ret
.size	avx2_mul_x4,.-avx2_mul_x4

# Function optimized for the constant 1
################################################################################
# void avx2_mul_by1_x4(void* RESULTx4, void *Ax4);
.type	avx2_mul_by1_x4,\@abi-omnipotent
.align	32
avx2_mul_by1_x4:
	lea	.LAVX2_POLY(%rip), %rax

	vpxor	$ACC0, $ACC0, $ACC0
	vpxor	$ACC1, $ACC1, $ACC1
	vpxor	$ACC2, $ACC2, $ACC2
	vpxor	$ACC3, $ACC3, $ACC3
	vpxor	$ACC4, $ACC4, $ACC4
	vpxor	$ACC5, $ACC5, $ACC5
	vpxor	$ACC6, $ACC6, $ACC6
	vpxor	$ACC7, $ACC7, $ACC7
	vpxor	$ACC8, $ACC8, $ACC8

	vmovdqa	32*3+.LONE(%rip), %ymm14
	vmovdqa	32*7+.LONE(%rip), %ymm15

	mov	$n_digits, $itr
	jmp	.Lavx2_mul_by1_x4_loop

.align	32
.Lavx2_mul_by1_x4_loop:
	vmovdqa		32*0($a_ptr), $B
	.byte		0x48,0x8d,0xb6,0x20,0,0,0	# lea	32*1($a_ptr), $a_ptr

	vpsllq		\$5, $B, $OVERFLOW
	vpmuludq	%ymm14, $B, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC3
	.byte		0x67
	vpmuludq	$AND_MASK, $B, $T0
	vpand		$AND_MASK, $ACC0, $Y
	vpaddq		$T0, $ACC4, $ACC4
	vpaddq		$T0, $ACC5, $ACC5
	vpaddq		$T0, $ACC6, $ACC6
	vpsllq		\$23, $B, $T0

	.byte		0x67,0x67
	vpmuludq	%ymm15, $B, $OVERFLOW
	vpsubq		$T0, $ACC6, $ACC6

	vpmuludq	$AND_MASK, $Y, $T0
	vpaddq		$OVERFLOW, $ACC7, $ACC7
	vpaddq		$T0, $ACC0, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	.byte		0x67,0x67
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $OVERFLOW
	vmovdqa		$ACC5, $ACC4
	vpmuludq	32*7(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC6, $ACC5
	vpaddq		$T0, $ACC7, $ACC6
	vpmuludq	32*8(%rax), $Y, $ACC7

	dec	$itr
	jnz	.Lavx2_mul_by1_x4_loop

	ret
.size	avx2_mul_by1_x4,.-avx2_mul_by1_x4

################################################################################
# void avx2_sqr_x4(void* RESULTx4, void *Ax4, void *Bx4);
.type	avx2_sqr_x4,\@abi-omnipotent
.align	32
avx2_sqr_x4:
	lea		.LAVX2_POLY(%rip), %rax

	vmovdqa		32*7(%rax), %ymm14
	vmovdqa		32*8(%rax), %ymm15

	vmovdqa		32*0($a_ptr), $B
	vmovdqa		32*1($a_ptr), $ACC1
	vmovdqa		32*2($a_ptr), $ACC2
	vmovdqa		32*3($a_ptr), $ACC3
	vmovdqa		32*4($a_ptr), $ACC4
	vmovdqa		32*5($a_ptr), $ACC5
	vmovdqa		32*6($a_ptr), $ACC6
	vmovdqa		32*7($a_ptr), $ACC7
	vpaddq		$ACC1, $ACC1, $ACC1	# 2*$ACC0..7
	vmovdqa		32*8($a_ptr), $ACC8
	vpaddq		$ACC2, $ACC2, $ACC2
	vmovdqa		$ACC1, 32*0(%rcx)
	vpaddq		$ACC3, $ACC3, $ACC3
	vmovdqa		$ACC2, 32*1(%rcx)
	vpaddq		$ACC4, $ACC4, $ACC4
	vmovdqa		$ACC3, 32*2(%rcx)
	vpaddq		$ACC5, $ACC5, $ACC5
	vmovdqa		$ACC4, 32*3(%rcx)
	vpaddq		$ACC6, $ACC6, $ACC6
	vmovdqa		$ACC5, 32*4(%rcx)
	vpaddq		$ACC7, $ACC7, $ACC7
	vmovdqa		$ACC6, 32*5(%rcx)
	vpaddq		$ACC8, $ACC8, $ACC8
	vmovdqa		$ACC7, 32*6(%rcx)
	vmovdqa		$ACC8, 32*7(%rcx)

	#itr		1
	vpmuludq	$B, $B, $ACC0
	vpmuludq	$B, $ACC1, $ACC1
	 vpand		$AND_MASK, $ACC0, $Y
	vpmuludq	$B, $ACC2, $ACC2
	vpmuludq	$B, $ACC3, $ACC3
	vpmuludq	$B, $ACC4, $ACC4
	vpmuludq	$B, $ACC5, $ACC5
	vpmuludq	$B, $ACC6, $ACC6
	 vpmuludq	$AND_MASK, $Y, $T0
	vpmuludq	$B, $ACC7, $ACC7
	vpmuludq	$B, $ACC8, $ACC8
	 vmovdqa	32*1($a_ptr), $B

	vpaddq		$T0, $ACC0, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $T0
	vmovdqa		$ACC5, $ACC4
	vpmuludq	%ymm14, $Y, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC5
	vpmuludq	%ymm15, $Y, $T0
	vpaddq		$OVERFLOW, $ACC7, $ACC6
	vpaddq		$T0, $ACC8, $ACC7

	#itr		2
	vpmuludq	$B, $B, $OVERFLOW
	 vpand		$AND_MASK, $ACC0, $Y
	vpmuludq	32*1(%rcx), $B, $T0
	vpaddq		$OVERFLOW, $ACC1, $ACC1
	vpmuludq	32*2(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC2, $ACC2
	vpmuludq	32*3(%rcx), $B, $T0
	vpaddq		$OVERFLOW, $ACC3, $ACC3
	vpmuludq	32*4(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC4, $ACC4
	vpmuludq	32*5(%rcx), $B, $T0
	vpaddq		$OVERFLOW, $ACC5, $ACC5
	vpmuludq	32*6(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC6

	vpmuludq	$AND_MASK, $Y, $T0
	 vpaddq		$OVERFLOW, $ACC7, $ACC7
	 vpmuludq	32*7(%rcx), $B, $ACC8
	 vmovdqa	32*2($a_ptr), $B
	vpaddq		$T0, $ACC0, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $T0
	vmovdqa		$ACC5, $ACC4
	vpmuludq	%ymm14, $Y, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC5
	vpmuludq	%ymm15, $Y, $T0
	vpaddq		$OVERFLOW, $ACC7, $ACC6
	vpaddq		$T0, $ACC8, $ACC7

	#itr		3
	vpmuludq	$B, $B, $T0
	 vpand		$AND_MASK, $ACC0, $Y
	vpmuludq	32*2(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC2, $ACC2
	vpmuludq	32*3(%rcx), $B, $T0
	vpaddq		$OVERFLOW, $ACC3, $ACC3
	vpmuludq	32*4(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC4, $ACC4
	vpmuludq	32*5(%rcx), $B, $T0
	vpaddq		$OVERFLOW, $ACC5, $ACC5
	vpmuludq	32*6(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC6

	vpmuludq	$AND_MASK, $Y, $T0
	 vpaddq		$OVERFLOW, $ACC7, $ACC7
	 vpmuludq	32*7(%rcx), $B, $ACC8
	 vmovdqa	32*3($a_ptr), $B
	vpaddq		$T0, $ACC0, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $T0
	vmovdqa		$ACC5, $ACC4
	vpmuludq	%ymm14, $Y, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC5
	vpmuludq	%ymm15, $Y, $T0
	 vpand		$AND_MASK, $ACC0, $Y
	vpaddq		$OVERFLOW, $ACC7, $ACC6
	vpaddq		$T0, $ACC8, $ACC7

	#itr		4
	vpmuludq	$B, $B, $OVERFLOW
	vpmuludq	32*3(%rcx), $B, $T0
	vpaddq		$OVERFLOW, $ACC3, $ACC3
	vpmuludq	32*4(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC4, $ACC4
	vpmuludq	32*5(%rcx), $B, $T0
	vpaddq		$OVERFLOW, $ACC5, $ACC5
	vpmuludq	32*6(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC6

	vpmuludq	$AND_MASK, $Y, $T0
	 vpaddq		$OVERFLOW, $ACC7, $ACC7
	 vpmuludq	32*7(%rcx), $B, $ACC8
	 vmovdqa	32*4($a_ptr), $B
	vpaddq		$T0, $ACC0, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $T0
	vmovdqa		$ACC5, $ACC4
	vpmuludq	%ymm14, $Y, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC5
	vpmuludq	%ymm15, $Y, $T0
	 vpand		$AND_MASK, $ACC0, $Y
	vpaddq		$OVERFLOW, $ACC7, $ACC6
	vpaddq		$T0, $ACC8, $ACC7

	#itr		5
	vpmuludq	$B, $B, $T0
	vpmuludq	32*4(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC4, $ACC4
	vpmuludq	32*5(%rcx), $B, $T0
	vpaddq		$OVERFLOW, $ACC5, $ACC5
	vpmuludq	32*6(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC6

	vpmuludq	$AND_MASK, $Y, $T0
	 vpaddq		$OVERFLOW, $ACC7, $ACC7
	 vpmuludq	32*7(%rcx), $B, $ACC8
	 vmovdqa	32*5($a_ptr), $B
	vpaddq		$T0, $ACC0, $OVERFLOW
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3+.LAVX2_POLY(%rip), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $T0
	vmovdqa		$ACC5, $ACC4
	vpmuludq	%ymm14, $Y, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC5
	vpmuludq	%ymm15, $Y, $T0
	 vpand		$AND_MASK, $ACC0, $Y
	vpaddq		$OVERFLOW, $ACC7, $ACC6
	vpaddq		$T0, $ACC8, $ACC7

	#itr		6
	vpmuludq	$B, $B, $OVERFLOW
	vpmuludq	32*5(%rcx), $B, $T0
	vpaddq		$OVERFLOW, $ACC5, $ACC5
	vpmuludq	32*6(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC6

	vpmuludq	$AND_MASK, $Y, $T0
	 vpaddq		$OVERFLOW, $ACC7, $ACC7
	 vpmuludq	32*7(%rcx), $B, $ACC8
	 vmovdqa	32*6($a_ptr), $B
	vpaddq		$T0, $ACC0, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $T0
	vmovdqa		$ACC5, $ACC4
	vpmuludq	%ymm14, $Y, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC5
	vpmuludq	%ymm15, $Y, $T0
	 vpand		$AND_MASK, $ACC0, $Y
	vpaddq		$OVERFLOW, $ACC7, $ACC6
	vpaddq		$T0, $ACC8, $ACC7

	#itr		7
	vpmuludq	$B, $B, $T0
	vpmuludq	32*6(%rcx), $B, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC6

	vpmuludq	$AND_MASK, $Y, $T0
	 vpaddq		$OVERFLOW, $ACC7, $ACC7
	 vpmuludq	32*7(%rcx), $B, $ACC8
	 vmovdqa	32*7($a_ptr), $B
	vpaddq		$T0, $ACC0, $OVERFLOW
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $T0
	vmovdqa		$ACC5, $ACC4
	vpmuludq	%ymm14, $Y, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC5
	vpmuludq	%ymm15, $Y, $T0
	 vpand		$AND_MASK, $ACC0, $Y
	vpaddq		$OVERFLOW, $ACC7, $ACC6
	vpaddq		$T0, $ACC8, $ACC7

	#itr		8
	vpmuludq	$B, $B, $OVERFLOW

	vpmuludq	$AND_MASK, $Y, $T0
	 vpaddq		$OVERFLOW, $ACC7, $ACC7
	 vpmuludq	32*7(%rcx), $B, $ACC8
	 vmovdqa	32*8($a_ptr), $B
	vpaddq		$T0, $ACC0, $OVERFLOW
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $T0
	vmovdqa		$ACC5, $ACC4
	vpmuludq	%ymm14, $Y, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC5
	vpmuludq	%ymm15, $Y, $T0
	 vpand		$AND_MASK, $ACC0, $Y
	vpaddq		$OVERFLOW, $ACC7, $ACC6
	vpaddq		$T0, $ACC8, $ACC7

	#itr		9
	vpmuludq	$B, $B, $ACC8

	vpmuludq	$AND_MASK, $Y, $T0
	vpaddq		$T0, $ACC0, $OVERFLOW
	vpsrlq		$digit_size, $OVERFLOW, $OVERFLOW
	vpaddq		$T0, $ACC1, $ACC0
	vpaddq		$T0, $ACC2, $ACC1
	vpmuludq	32*3(%rax), $Y, $T0
	vpaddq		$OVERFLOW, $ACC0, $ACC0
	vpaddq		$T0, $ACC3, $ACC2
	vmovdqa		$ACC4, $ACC3
	vpsllq		\$18, $Y, $T0
	vmovdqa		$ACC5, $ACC4
	vpmuludq	%ymm14, $Y, $OVERFLOW
	vpaddq		$T0, $ACC6, $ACC5
	vpmuludq	%ymm15, $Y, $T0
	vpaddq		$OVERFLOW, $ACC7, $ACC6
	vpaddq		$T0, $ACC8, $ACC7

	vpxor		$ACC8, $ACC8, $ACC8

	ret
.size	avx2_sqr_x4,.-avx2_sqr_x4

################################################################################
# void avx2_sub_x4(void* RESULTx4, void *Ax4, void *Bx4);
.type	avx2_sub_x4,\@abi-omnipotent
.align	32
avx2_sub_x4:
	vmovdqa	32*0($a_ptr), $ACC0
	lea	160($a_ptr), $a_ptr
	lea	.LAVX2_POLY_x8+128(%rip), %rax
	lea	128($b_ptr), $b_ptr
	vmovdqa	32*1-160($a_ptr), $ACC1
	vmovdqa	32*2-160($a_ptr), $ACC2
	vmovdqa	32*3-160($a_ptr), $ACC3
	vmovdqa	32*4-160($a_ptr), $ACC4
	vmovdqa	32*5-160($a_ptr), $ACC5
	vmovdqa	32*6-160($a_ptr), $ACC6
	vmovdqa	32*7-160($a_ptr), $ACC7
	vmovdqa	32*8-160($a_ptr), $ACC8

	vpaddq	32*0-128(%rax), $ACC0, $ACC0
	vpaddq	32*1-128(%rax), $ACC1, $ACC1
	vpaddq	32*2-128(%rax), $ACC2, $ACC2
	vpaddq	32*3-128(%rax), $ACC3, $ACC3
	vpaddq	32*4-128(%rax), $ACC4, $ACC4
	vpaddq	32*5-128(%rax), $ACC5, $ACC5
	vpaddq	32*6-128(%rax), $ACC6, $ACC6
	vpaddq	32*7-128(%rax), $ACC7, $ACC7
	vpaddq	32*8-128(%rax), $ACC8, $ACC8

	vpsubq	32*0-128($b_ptr), $ACC0, $ACC0
	vpsubq	32*1-128($b_ptr), $ACC1, $ACC1
	vpsubq	32*2-128($b_ptr), $ACC2, $ACC2
	vpsubq	32*3-128($b_ptr), $ACC3, $ACC3
	vpsubq	32*4-128($b_ptr), $ACC4, $ACC4
	vpsubq	32*5-128($b_ptr), $ACC5, $ACC5
	vpsubq	32*6-128($b_ptr), $ACC6, $ACC6
	vpsubq	32*7-128($b_ptr), $ACC7, $ACC7
	vpsubq	32*8-128($b_ptr), $ACC8, $ACC8

	ret
.size	avx2_sub_x4,.-avx2_sub_x4

.type	avx2_select_n_store,\@abi-omnipotent
.align	32
avx2_select_n_store:
	vmovdqa	`8+32*9*8`(%rsp), $Y
	vpor	`8+32*9*8+32`(%rsp), $Y, $Y

	vpandn	$ACC0, $Y, $ACC0
	vpandn	$ACC1, $Y, $ACC1
	vpandn	$ACC2, $Y, $ACC2
	vpandn	$ACC3, $Y, $ACC3
	vpandn	$ACC4, $Y, $ACC4
	vpandn	$ACC5, $Y, $ACC5
	vpandn	$ACC6, $Y, $ACC6
	vmovdqa	`8+32*9*8+32`(%rsp), $B
	vpandn	$ACC7, $Y, $ACC7
	vpandn	`8+32*9*8`(%rsp), $B, $B
	vpandn	$ACC8, $Y, $ACC8

	vpand	32*0(%rsi), $B, $T0
	lea	160(%rsi), %rax
	vpand	32*1(%rsi), $B, $Y
	vpxor	$T0, $ACC0, $ACC0
	vpand	32*2(%rsi), $B, $T0
	vpxor	$Y, $ACC1, $ACC1
	vpand	32*3(%rsi), $B, $Y
	vpxor	$T0, $ACC2, $ACC2
	vpand	32*4-160(%rax), $B, $T0
	vpxor	$Y, $ACC3, $ACC3
	vpand	32*5-160(%rax), $B, $Y
	vpxor	$T0, $ACC4, $ACC4
	vpand	32*6-160(%rax), $B, $T0
	vpxor	$Y, $ACC5, $ACC5
	vpand	32*7-160(%rax), $B, $Y
	vpxor	$T0, $ACC6, $ACC6
	vpand	32*8-160(%rax), $B, $T0
	vmovdqa	`8+32*9*8+32`(%rsp), $B
	vpxor	$Y, $ACC7, $ACC7

	vpand	32*0(%rdx), $B, $Y
	lea	160(%rdx), %rax
	vpxor	$T0, $ACC8, $ACC8
	vpand	32*1(%rdx), $B, $T0
	vpxor	$Y, $ACC0, $ACC0
	vpand	32*2(%rdx), $B, $Y
	vpxor	$T0, $ACC1, $ACC1
	vpand	32*3(%rdx), $B, $T0
	vpxor	$Y, $ACC2, $ACC2
	vpand	32*4-160(%rax), $B, $Y
	vpxor	$T0, $ACC3, $ACC3
	vpand	32*5-160(%rax), $B, $T0
	vpxor	$Y, $ACC4, $ACC4
	vpand	32*6-160(%rax), $B, $Y
	vpxor	$T0, $ACC5, $ACC5
	vpand	32*7-160(%rax), $B, $T0
	vpxor	$Y, $ACC6, $ACC6
	vpand	32*8-160(%rax), $B, $Y
	vpxor	$T0, $ACC7, $ACC7
	vpxor	$Y, $ACC8, $ACC8
	`&STORE`

	ret
.size	avx2_select_n_store,.-avx2_select_n_store
___
$code.=<<___	if (0);				# inlined
################################################################################
# void avx2_mul_by2_x4(void* RESULTx4, void *Ax4);
.type	avx2_mul_by2_x4,\@abi-omnipotent
.align	32
avx2_mul_by2_x4:
	vmovdqa	32*0($a_ptr), $ACC0
	lea	160($a_ptr), %rax
	vmovdqa	32*1($a_ptr), $ACC1
	vmovdqa	32*2($a_ptr), $ACC2
	vmovdqa	32*3($a_ptr), $ACC3
	vmovdqa	32*4-160(%rax), $ACC4
	vmovdqa	32*5-160(%rax), $ACC5
	vmovdqa	32*6-160(%rax), $ACC6
	vmovdqa	32*7-160(%rax), $ACC7
	vmovdqa	32*8-160(%rax), $ACC8

	vpaddq	$ACC0, $ACC0, $ACC0
	vpaddq	$ACC1, $ACC1, $ACC1
	vpaddq	$ACC2, $ACC2, $ACC2
	vpaddq	$ACC3, $ACC3, $ACC3
	vpaddq	$ACC4, $ACC4, $ACC4
	vpaddq	$ACC5, $ACC5, $ACC5
	vpaddq	$ACC6, $ACC6, $ACC6
	vpaddq	$ACC7, $ACC7, $ACC7
	vpaddq	$ACC8, $ACC8, $ACC8

	ret
.size	avx2_mul_by2_x4,.-avx2_mul_by2_x4
___
my ($r_ptr_in,$a_ptr_in,$b_ptr_in)=("%rdi","%rsi","%rdx");
my ($r_ptr,$a_ptr,$b_ptr)=("%r8","%r9","%r10");

$code.=<<___;
################################################################################
# void ecp_nistz256_avx2_point_add_affine_x4(void* RESULTx4, void *Ax4, void *Bx4);
.globl	ecp_nistz256_avx2_point_add_affine_x4
.type	ecp_nistz256_avx2_point_add_affine_x4,\@function,3
.align	32
ecp_nistz256_avx2_point_add_affine_x4:
	mov	%rsp, %rax
	push    %rbp
	vzeroupper
___
$code.=<<___	if ($win64);
	lea	-16*10(%rsp), %rsp
	vmovaps	%xmm6, -8-16*10(%rax)
	vmovaps	%xmm7, -8-16*9(%rax)
	vmovaps	%xmm8, -8-16*8(%rax)
	vmovaps	%xmm9, -8-16*7(%rax)
	vmovaps	%xmm10, -8-16*6(%rax)
	vmovaps	%xmm11, -8-16*5(%rax)
	vmovaps	%xmm12, -8-16*4(%rax)
	vmovaps	%xmm13, -8-16*3(%rax)
	vmovaps	%xmm14, -8-16*2(%rax)
	vmovaps	%xmm15, -8-16*1(%rax)
___
$code.=<<___;
	lea	-8(%rax), %rbp

# Result + 32*0 = Result.X
# Result + 32*9 = Result.Y
# Result + 32*18 = Result.Z

# A + 32*0 = A.X
# A + 32*9 = A.Y
# A + 32*18 = A.Z

# B + 32*0 = B.X
# B + 32*9 = B.Y

	sub	\$`32*9*8+32*2+32*8`, %rsp
	and	\$-64, %rsp

	mov	$r_ptr_in, $r_ptr
	mov	$a_ptr_in, $a_ptr
	mov	$b_ptr_in, $b_ptr

	vmovdqa	32*0($a_ptr_in), %ymm0
	vmovdqa	.LAVX2_AND_MASK(%rip), $AND_MASK
	vpxor	%ymm1, %ymm1, %ymm1
	lea	256($a_ptr_in), %rax		# size optimization
	vpor	32*1($a_ptr_in), %ymm0, %ymm0
	vpor	32*2($a_ptr_in), %ymm0, %ymm0
	vpor	32*3($a_ptr_in), %ymm0, %ymm0
	vpor	32*4-256(%rax), %ymm0, %ymm0
	lea	256(%rax), %rcx			# size optimization
	vpor	32*5-256(%rax), %ymm0, %ymm0
	vpor	32*6-256(%rax), %ymm0, %ymm0
	vpor	32*7-256(%rax), %ymm0, %ymm0
	vpor	32*8-256(%rax), %ymm0, %ymm0
	vpor	32*9-256(%rax), %ymm0, %ymm0
	vpor	32*10-256(%rax), %ymm0, %ymm0
	vpor	32*11-256(%rax), %ymm0, %ymm0
	vpor	32*12-512(%rcx), %ymm0, %ymm0
	vpor	32*13-512(%rcx), %ymm0, %ymm0
	vpor	32*14-512(%rcx), %ymm0, %ymm0
	vpor	32*15-512(%rcx), %ymm0, %ymm0
	vpor	32*16-512(%rcx), %ymm0, %ymm0
	vpor	32*17-512(%rcx), %ymm0, %ymm0
	vpcmpeqq %ymm1, %ymm0, %ymm0
	vmovdqa	%ymm0, `32*9*8`(%rsp)

	vpxor	%ymm1, %ymm1, %ymm1
	vmovdqa	32*0($b_ptr), %ymm0
	lea	256($b_ptr), %rax		# size optimization
	vpor	32*1($b_ptr), %ymm0, %ymm0
	vpor	32*2($b_ptr), %ymm0, %ymm0
	vpor	32*3($b_ptr), %ymm0, %ymm0
	vpor	32*4-256(%rax), %ymm0, %ymm0
	lea	256(%rax), %rcx			# size optimization
	vpor	32*5-256(%rax), %ymm0, %ymm0
	vpor	32*6-256(%rax), %ymm0, %ymm0
	vpor	32*7-256(%rax), %ymm0, %ymm0
	vpor	32*8-256(%rax), %ymm0, %ymm0
	vpor	32*9-256(%rax), %ymm0, %ymm0
	vpor	32*10-256(%rax), %ymm0, %ymm0
	vpor	32*11-256(%rax), %ymm0, %ymm0
	vpor	32*12-512(%rcx), %ymm0, %ymm0
	vpor	32*13-512(%rcx), %ymm0, %ymm0
	vpor	32*14-512(%rcx), %ymm0, %ymm0
	vpor	32*15-512(%rcx), %ymm0, %ymm0
	vpor	32*16-512(%rcx), %ymm0, %ymm0
	vpor	32*17-512(%rcx), %ymm0, %ymm0
	vpcmpeqq %ymm1, %ymm0, %ymm0
	vmovdqa	%ymm0, `32*9*8+32`(%rsp)

	#	Z1^2 = Z1*Z1
	lea	`32*9*2`($a_ptr), %rsi
	lea	`32*9*2`(%rsp), %rdi
	lea	`32*9*8+32*2`(%rsp), %rcx	# temporary vector
	call	avx2_sqr_x4
	call	avx2_normalize_n_store

	#	U2 = X2*Z1^2
	lea	`32*9*0`($b_ptr), %rsi
	lea	`32*9*2`(%rsp), %rdx
	lea	`32*9*0`(%rsp), %rdi
	call	avx2_mul_x4
	#call	avx2_normalize
	`&STORE`

	#	S2 = Z1*Z1^2 = Z1^3
	lea	`32*9*2`($a_ptr), %rsi
	lea	`32*9*2`(%rsp), %rdx
	lea	`32*9*1`(%rsp), %rdi
	call	avx2_mul_x4
	call	avx2_normalize_n_store

	#	S2 = S2*Y2 = Y2*Z1^3
	lea	`32*9*1`($b_ptr), %rsi
	lea	`32*9*1`(%rsp), %rdx
	lea	`32*9*1`(%rsp), %rdi
	call	avx2_mul_x4
	call	avx2_normalize_n_store

	#	H = U2 - U1 = U2 - X1
	lea	`32*9*0`(%rsp), %rsi
	lea	`32*9*0`($a_ptr), %rdx
	lea	`32*9*3`(%rsp), %rdi
	call	avx2_sub_x4
	call	avx2_normalize_n_store

	#	R = S2 - S1 = S2 - Y1
	lea	`32*9*1`(%rsp), %rsi
	lea	`32*9*1`($a_ptr), %rdx
	lea	`32*9*4`(%rsp), %rdi
	call	avx2_sub_x4
	call	avx2_normalize_n_store

	#	Z3 = H*Z1*Z2
	lea	`32*9*3`(%rsp), %rsi
	lea	`32*9*2`($a_ptr), %rdx
	lea	`32*9*2`($r_ptr), %rdi
	call	avx2_mul_x4
	call	avx2_normalize

	lea	.LONE(%rip), %rsi
	lea	`32*9*2`($a_ptr), %rdx
	call	avx2_select_n_store

	#	R^2 = R^2
	lea	`32*9*4`(%rsp), %rsi
	lea	`32*9*6`(%rsp), %rdi
	lea	`32*9*8+32*2`(%rsp), %rcx	# temporary vector
	call	avx2_sqr_x4
	call	avx2_normalize_n_store

	#	H^2 = H^2
	lea	`32*9*3`(%rsp), %rsi
	lea	`32*9*5`(%rsp), %rdi
	call	avx2_sqr_x4
	call	avx2_normalize_n_store

	#	H^3 = H^2*H
	lea	`32*9*3`(%rsp), %rsi
	lea	`32*9*5`(%rsp), %rdx
	lea	`32*9*7`(%rsp), %rdi
	call	avx2_mul_x4
	call	avx2_normalize_n_store

	#	U2 = U1*H^2
	lea	`32*9*0`($a_ptr), %rsi
	lea	`32*9*5`(%rsp), %rdx
	lea	`32*9*0`(%rsp), %rdi
	call	avx2_mul_x4
	#call	avx2_normalize
	`&STORE`

	#	Hsqr = U2*2
	#lea	32*9*0(%rsp), %rsi
	#lea	32*9*5(%rsp), %rdi
	#call	avx2_mul_by2_x4

	vpaddq	$ACC0, $ACC0, $ACC0	# inlined avx2_mul_by2_x4
	lea	`32*9*5`(%rsp), %rdi
	vpaddq	$ACC1, $ACC1, $ACC1
	vpaddq	$ACC2, $ACC2, $ACC2
	vpaddq	$ACC3, $ACC3, $ACC3
	vpaddq	$ACC4, $ACC4, $ACC4
	vpaddq	$ACC5, $ACC5, $ACC5
	vpaddq	$ACC6, $ACC6, $ACC6
	vpaddq	$ACC7, $ACC7, $ACC7
	vpaddq	$ACC8, $ACC8, $ACC8
	call	avx2_normalize_n_store

	#	X3 = R^2 - H^3
	#lea	32*9*6(%rsp), %rsi
	#lea	32*9*7(%rsp), %rdx
	#lea	32*9*5(%rsp), %rcx
	#lea	32*9*0($r_ptr), %rdi
	#call	avx2_sub_x4
	#NORMALIZE
	#STORE

	#	X3 = X3 - U2*2
	#lea	32*9*0($r_ptr), %rsi
	#lea	32*9*0($r_ptr), %rdi
	#call	avx2_sub_x4
	#NORMALIZE
	#STORE

	lea	`32*9*6+128`(%rsp), %rsi
	lea	.LAVX2_POLY_x2+128(%rip), %rax
	lea	`32*9*7+128`(%rsp), %rdx
	lea	`32*9*5+128`(%rsp), %rcx
	lea	`32*9*0`($r_ptr), %rdi

	vmovdqa	32*0-128(%rsi), $ACC0
	vmovdqa	32*1-128(%rsi), $ACC1
	vmovdqa	32*2-128(%rsi), $ACC2
	vmovdqa	32*3-128(%rsi), $ACC3
	vmovdqa	32*4-128(%rsi), $ACC4
	vmovdqa	32*5-128(%rsi), $ACC5
	vmovdqa	32*6-128(%rsi), $ACC6
	vmovdqa	32*7-128(%rsi), $ACC7
	vmovdqa	32*8-128(%rsi), $ACC8

	vpaddq	32*0-128(%rax), $ACC0, $ACC0
	vpaddq	32*1-128(%rax), $ACC1, $ACC1
	vpaddq	32*2-128(%rax), $ACC2, $ACC2
	vpaddq	32*3-128(%rax), $ACC3, $ACC3
	vpaddq	32*4-128(%rax), $ACC4, $ACC4
	vpaddq	32*5-128(%rax), $ACC5, $ACC5
	vpaddq	32*6-128(%rax), $ACC6, $ACC6
	vpaddq	32*7-128(%rax), $ACC7, $ACC7
	vpaddq	32*8-128(%rax), $ACC8, $ACC8

	vpsubq	32*0-128(%rdx), $ACC0, $ACC0
	vpsubq	32*1-128(%rdx), $ACC1, $ACC1
	vpsubq	32*2-128(%rdx), $ACC2, $ACC2
	vpsubq	32*3-128(%rdx), $ACC3, $ACC3
	vpsubq	32*4-128(%rdx), $ACC4, $ACC4
	vpsubq	32*5-128(%rdx), $ACC5, $ACC5
	vpsubq	32*6-128(%rdx), $ACC6, $ACC6
	vpsubq	32*7-128(%rdx), $ACC7, $ACC7
	vpsubq	32*8-128(%rdx), $ACC8, $ACC8

	vpsubq	32*0-128(%rcx), $ACC0, $ACC0
	vpsubq	32*1-128(%rcx), $ACC1, $ACC1
	vpsubq	32*2-128(%rcx), $ACC2, $ACC2
	vpsubq	32*3-128(%rcx), $ACC3, $ACC3
	vpsubq	32*4-128(%rcx), $ACC4, $ACC4
	vpsubq	32*5-128(%rcx), $ACC5, $ACC5
	vpsubq	32*6-128(%rcx), $ACC6, $ACC6
	vpsubq	32*7-128(%rcx), $ACC7, $ACC7
	vpsubq	32*8-128(%rcx), $ACC8, $ACC8
	call	avx2_normalize

	lea	32*0($b_ptr), %rsi
	lea	32*0($a_ptr), %rdx
	call	avx2_select_n_store

	#	H = U2 - X3
	lea	`32*9*0`(%rsp), %rsi
	lea	`32*9*0`($r_ptr), %rdx
	lea	`32*9*3`(%rsp), %rdi
	call	avx2_sub_x4
	call	avx2_normalize_n_store

	#
	lea	`32*9*3`(%rsp), %rsi
	lea	`32*9*4`(%rsp), %rdx
	lea	`32*9*3`(%rsp), %rdi
	call	avx2_mul_x4
	call	avx2_normalize_n_store

	#
	lea	`32*9*7`(%rsp), %rsi
	lea	`32*9*1`($a_ptr), %rdx
	lea	`32*9*1`(%rsp), %rdi
	call	avx2_mul_x4
	call	avx2_normalize_n_store

	#
	lea	`32*9*3`(%rsp), %rsi
	lea	`32*9*1`(%rsp), %rdx
	lea	`32*9*1`($r_ptr), %rdi
	call	avx2_sub_x4
	call	avx2_normalize

	lea	32*9($b_ptr), %rsi
	lea	32*9($a_ptr), %rdx
	call	avx2_select_n_store

	#lea	32*9*0($r_ptr), %rsi
	#lea	32*9*0($r_ptr), %rdi
	#call	avx2_mul_by1_x4
	#NORMALIZE
	#STORE

	lea	`32*9*1`($r_ptr), %rsi
	lea	`32*9*1`($r_ptr), %rdi
	call	avx2_mul_by1_x4
	call	avx2_normalize_n_store

	vzeroupper
___
$code.=<<___	if ($win64);
	movaps	%xmm6, -16*10(%rbp)
	movaps	%xmm7, -16*9(%rbp)
	movaps	%xmm8, -16*8(%rbp)
	movaps	%xmm9, -16*7(%rbp)
	movaps	%xmm10, -16*6(%rbp)
	movaps	%xmm11, -16*5(%rbp)
	movaps	%xmm12, -16*4(%rbp)
	movaps	%xmm13, -16*3(%rbp)
	movaps	%xmm14, -16*2(%rbp)
	movaps	%xmm15, -16*1(%rbp)
___
$code.=<<___;
	mov	%rbp, %rsp
	pop	%rbp
	ret
.size	ecp_nistz256_avx2_point_add_affine_x4,.-ecp_nistz256_avx2_point_add_affine_x4

################################################################################
# void ecp_nistz256_avx2_point_add_affines_x4(void* RESULTx4, void *Ax4, void *Bx4);
.globl	ecp_nistz256_avx2_point_add_affines_x4
.type	ecp_nistz256_avx2_point_add_affines_x4,\@function,3
.align	32
ecp_nistz256_avx2_point_add_affines_x4:
	mov	%rsp, %rax
	push    %rbp
	vzeroupper
___
$code.=<<___	if ($win64);
	lea	-16*10(%rsp), %rsp
	vmovaps	%xmm6, -8-16*10(%rax)
	vmovaps	%xmm7, -8-16*9(%rax)
	vmovaps	%xmm8, -8-16*8(%rax)
	vmovaps	%xmm9, -8-16*7(%rax)
	vmovaps	%xmm10, -8-16*6(%rax)
	vmovaps	%xmm11, -8-16*5(%rax)
	vmovaps	%xmm12, -8-16*4(%rax)
	vmovaps	%xmm13, -8-16*3(%rax)
	vmovaps	%xmm14, -8-16*2(%rax)
	vmovaps	%xmm15, -8-16*1(%rax)
___
$code.=<<___;
	lea	-8(%rax), %rbp

# Result + 32*0 = Result.X
# Result + 32*9 = Result.Y
# Result + 32*18 = Result.Z

# A + 32*0 = A.X
# A + 32*9 = A.Y

# B + 32*0 = B.X
# B + 32*9 = B.Y

	sub	\$`32*9*8+32*2+32*8`, %rsp
	and	\$-64, %rsp

	mov	$r_ptr_in, $r_ptr
	mov	$a_ptr_in, $a_ptr
	mov	$b_ptr_in, $b_ptr

	vmovdqa	32*0($a_ptr_in), %ymm0
	vmovdqa	.LAVX2_AND_MASK(%rip), $AND_MASK
	vpxor	%ymm1, %ymm1, %ymm1
	lea	256($a_ptr_in), %rax		# size optimization
	vpor	32*1($a_ptr_in), %ymm0, %ymm0
	vpor	32*2($a_ptr_in), %ymm0, %ymm0
	vpor	32*3($a_ptr_in), %ymm0, %ymm0
	vpor	32*4-256(%rax), %ymm0, %ymm0
	lea	256(%rax), %rcx			# size optimization
	vpor	32*5-256(%rax), %ymm0, %ymm0
	vpor	32*6-256(%rax), %ymm0, %ymm0
	vpor	32*7-256(%rax), %ymm0, %ymm0
	vpor	32*8-256(%rax), %ymm0, %ymm0
	vpor	32*9-256(%rax), %ymm0, %ymm0
	vpor	32*10-256(%rax), %ymm0, %ymm0
	vpor	32*11-256(%rax), %ymm0, %ymm0
	vpor	32*12-512(%rcx), %ymm0, %ymm0
	vpor	32*13-512(%rcx), %ymm0, %ymm0
	vpor	32*14-512(%rcx), %ymm0, %ymm0
	vpor	32*15-512(%rcx), %ymm0, %ymm0
	vpor	32*16-512(%rcx), %ymm0, %ymm0
	vpor	32*17-512(%rcx), %ymm0, %ymm0
	vpcmpeqq %ymm1, %ymm0, %ymm0
	vmovdqa	%ymm0, `32*9*8`(%rsp)

	vpxor	%ymm1, %ymm1, %ymm1
	vmovdqa	32*0($b_ptr), %ymm0
	lea	256($b_ptr), %rax		# size optimization
	vpor	32*1($b_ptr), %ymm0, %ymm0
	vpor	32*2($b_ptr), %ymm0, %ymm0
	vpor	32*3($b_ptr), %ymm0, %ymm0
	vpor	32*4-256(%rax), %ymm0, %ymm0
	lea	256(%rax), %rcx			# size optimization
	vpor	32*5-256(%rax), %ymm0, %ymm0
	vpor	32*6-256(%rax), %ymm0, %ymm0
	vpor	32*7-256(%rax), %ymm0, %ymm0
	vpor	32*8-256(%rax), %ymm0, %ymm0
	vpor	32*9-256(%rax), %ymm0, %ymm0
	vpor	32*10-256(%rax), %ymm0, %ymm0
	vpor	32*11-256(%rax), %ymm0, %ymm0
	vpor	32*12-512(%rcx), %ymm0, %ymm0
	vpor	32*13-512(%rcx), %ymm0, %ymm0
	vpor	32*14-512(%rcx), %ymm0, %ymm0
	vpor	32*15-512(%rcx), %ymm0, %ymm0
	vpor	32*16-512(%rcx), %ymm0, %ymm0
	vpor	32*17-512(%rcx), %ymm0, %ymm0
	vpcmpeqq %ymm1, %ymm0, %ymm0
	vmovdqa	%ymm0, `32*9*8+32`(%rsp)

	#	H = U2 - U1 = X2 - X1
	lea	`32*9*0`($b_ptr), %rsi
	lea	`32*9*0`($a_ptr), %rdx
	lea	`32*9*3`(%rsp), %rdi
	call	avx2_sub_x4
	call	avx2_normalize_n_store

	#	R = S2 - S1 = Y2 - Y1
	lea	`32*9*1`($b_ptr), %rsi
	lea	`32*9*1`($a_ptr), %rdx
	lea	`32*9*4`(%rsp), %rdi
	call	avx2_sub_x4
	call	avx2_normalize_n_store

	#	Z3 = H*Z1*Z2 = H
	lea	`32*9*3`(%rsp), %rsi
	lea	`32*9*2`($r_ptr), %rdi
	call	avx2_mul_by1_x4
	call	avx2_normalize

	vmovdqa	`32*9*8`(%rsp), $B
	vpor	`32*9*8+32`(%rsp), $B, $B

	vpandn	$ACC0, $B, $ACC0
	lea	.LONE+128(%rip), %rax
	vpandn	$ACC1, $B, $ACC1
	vpandn	$ACC2, $B, $ACC2
	vpandn	$ACC3, $B, $ACC3
	vpandn	$ACC4, $B, $ACC4
	vpandn	$ACC5, $B, $ACC5
	vpandn	$ACC6, $B, $ACC6
	vpandn	$ACC7, $B, $ACC7

	vpand	32*0-128(%rax), $B, $T0
	 vpandn	$ACC8, $B, $ACC8
	vpand	32*1-128(%rax), $B, $Y
	vpxor	$T0, $ACC0, $ACC0
	vpand	32*2-128(%rax), $B, $T0
	vpxor	$Y, $ACC1, $ACC1
	vpand	32*3-128(%rax), $B, $Y
	vpxor	$T0, $ACC2, $ACC2
	vpand	32*4-128(%rax), $B, $T0
	vpxor	$Y, $ACC3, $ACC3
	vpand	32*5-128(%rax), $B, $Y
	vpxor	$T0, $ACC4, $ACC4
	vpand	32*6-128(%rax), $B, $T0
	vpxor	$Y, $ACC5, $ACC5
	vpand	32*7-128(%rax), $B, $Y
	vpxor	$T0, $ACC6, $ACC6
	vpand	32*8-128(%rax), $B, $T0
	vpxor	$Y, $ACC7, $ACC7
	vpxor	$T0, $ACC8, $ACC8
	`&STORE`

	#	R^2 = R^2
	lea	`32*9*4`(%rsp), %rsi
	lea	`32*9*6`(%rsp), %rdi
	lea	`32*9*8+32*2`(%rsp), %rcx	# temporary vector
	call	avx2_sqr_x4
	call	avx2_normalize_n_store

	#	H^2 = H^2
	lea	`32*9*3`(%rsp), %rsi
	lea	`32*9*5`(%rsp), %rdi
	call	avx2_sqr_x4
	call	avx2_normalize_n_store

	#	H^3 = H^2*H
	lea	`32*9*3`(%rsp), %rsi
	lea	`32*9*5`(%rsp), %rdx
	lea	`32*9*7`(%rsp), %rdi
	call	avx2_mul_x4
	call	avx2_normalize_n_store

	#	U2 = U1*H^2
	lea	`32*9*0`($a_ptr), %rsi
	lea	`32*9*5`(%rsp), %rdx
	lea	`32*9*0`(%rsp), %rdi
	call	avx2_mul_x4
	#call	avx2_normalize
	`&STORE`

	#	Hsqr = U2*2
	#lea	32*9*0(%rsp), %rsi
	#lea	32*9*5(%rsp), %rdi
	#call	avx2_mul_by2_x4

	vpaddq	$ACC0, $ACC0, $ACC0	# inlined avx2_mul_by2_x4
	lea	`32*9*5`(%rsp), %rdi
	vpaddq	$ACC1, $ACC1, $ACC1
	vpaddq	$ACC2, $ACC2, $ACC2
	vpaddq	$ACC3, $ACC3, $ACC3
	vpaddq	$ACC4, $ACC4, $ACC4
	vpaddq	$ACC5, $ACC5, $ACC5
	vpaddq	$ACC6, $ACC6, $ACC6
	vpaddq	$ACC7, $ACC7, $ACC7
	vpaddq	$ACC8, $ACC8, $ACC8
	call	avx2_normalize_n_store

	#	X3 = R^2 - H^3
	#lea	32*9*6(%rsp), %rsi
	#lea	32*9*7(%rsp), %rdx
	#lea	32*9*5(%rsp), %rcx
	#lea	32*9*0($r_ptr), %rdi
	#call	avx2_sub_x4
	#NORMALIZE
	#STORE

	#	X3 = X3 - U2*2
	#lea	32*9*0($r_ptr), %rsi
	#lea	32*9*0($r_ptr), %rdi
	#call	avx2_sub_x4
	#NORMALIZE
	#STORE

	lea	`32*9*6+128`(%rsp), %rsi
	lea	.LAVX2_POLY_x2+128(%rip), %rax
	lea	`32*9*7+128`(%rsp), %rdx
	lea	`32*9*5+128`(%rsp), %rcx
	lea	`32*9*0`($r_ptr), %rdi

	vmovdqa	32*0-128(%rsi), $ACC0
	vmovdqa	32*1-128(%rsi), $ACC1
	vmovdqa	32*2-128(%rsi), $ACC2
	vmovdqa	32*3-128(%rsi), $ACC3
	vmovdqa	32*4-128(%rsi), $ACC4
	vmovdqa	32*5-128(%rsi), $ACC5
	vmovdqa	32*6-128(%rsi), $ACC6
	vmovdqa	32*7-128(%rsi), $ACC7
	vmovdqa	32*8-128(%rsi), $ACC8

	vpaddq	32*0-128(%rax), $ACC0, $ACC0
	vpaddq	32*1-128(%rax), $ACC1, $ACC1
	vpaddq	32*2-128(%rax), $ACC2, $ACC2
	vpaddq	32*3-128(%rax), $ACC3, $ACC3
	vpaddq	32*4-128(%rax), $ACC4, $ACC4
	vpaddq	32*5-128(%rax), $ACC5, $ACC5
	vpaddq	32*6-128(%rax), $ACC6, $ACC6
	vpaddq	32*7-128(%rax), $ACC7, $ACC7
	vpaddq	32*8-128(%rax), $ACC8, $ACC8

	vpsubq	32*0-128(%rdx), $ACC0, $ACC0
	vpsubq	32*1-128(%rdx), $ACC1, $ACC1
	vpsubq	32*2-128(%rdx), $ACC2, $ACC2
	vpsubq	32*3-128(%rdx), $ACC3, $ACC3
	vpsubq	32*4-128(%rdx), $ACC4, $ACC4
	vpsubq	32*5-128(%rdx), $ACC5, $ACC5
	vpsubq	32*6-128(%rdx), $ACC6, $ACC6
	vpsubq	32*7-128(%rdx), $ACC7, $ACC7
	vpsubq	32*8-128(%rdx), $ACC8, $ACC8

	vpsubq	32*0-128(%rcx), $ACC0, $ACC0
	vpsubq	32*1-128(%rcx), $ACC1, $ACC1
	vpsubq	32*2-128(%rcx), $ACC2, $ACC2
	vpsubq	32*3-128(%rcx), $ACC3, $ACC3
	vpsubq	32*4-128(%rcx), $ACC4, $ACC4
	vpsubq	32*5-128(%rcx), $ACC5, $ACC5
	vpsubq	32*6-128(%rcx), $ACC6, $ACC6
	vpsubq	32*7-128(%rcx), $ACC7, $ACC7
	vpsubq	32*8-128(%rcx), $ACC8, $ACC8
	call	avx2_normalize

	lea	32*0($b_ptr), %rsi
	lea	32*0($a_ptr), %rdx
	call	avx2_select_n_store

	#	H = U2 - X3
	lea	`32*9*0`(%rsp), %rsi
	lea	`32*9*0`($r_ptr), %rdx
	lea	`32*9*3`(%rsp), %rdi
	call	avx2_sub_x4
	call	avx2_normalize_n_store

	#	H = H*R
	lea	`32*9*3`(%rsp), %rsi
	lea	`32*9*4`(%rsp), %rdx
	lea	`32*9*3`(%rsp), %rdi
	call	avx2_mul_x4
	call	avx2_normalize_n_store

	#	S2 = S1 * H^3
	lea	`32*9*7`(%rsp), %rsi
	lea	`32*9*1`($a_ptr), %rdx
	lea	`32*9*1`(%rsp), %rdi
	call	avx2_mul_x4
	call	avx2_normalize_n_store

	#
	lea	`32*9*3`(%rsp), %rsi
	lea	`32*9*1`(%rsp), %rdx
	lea	`32*9*1`($r_ptr), %rdi
	call	avx2_sub_x4
	call	avx2_normalize

	lea	32*9($b_ptr), %rsi
	lea	32*9($a_ptr), %rdx
	call	avx2_select_n_store

	#lea	32*9*0($r_ptr), %rsi
	#lea	32*9*0($r_ptr), %rdi
	#call	avx2_mul_by1_x4
	#NORMALIZE
	#STORE

	lea	`32*9*1`($r_ptr), %rsi
	lea	`32*9*1`($r_ptr), %rdi
	call	avx2_mul_by1_x4
	call	avx2_normalize_n_store

	vzeroupper
___
$code.=<<___	if ($win64);
	movaps	%xmm6, -16*10(%rbp)
	movaps	%xmm7, -16*9(%rbp)
	movaps	%xmm8, -16*8(%rbp)
	movaps	%xmm9, -16*7(%rbp)
	movaps	%xmm10, -16*6(%rbp)
	movaps	%xmm11, -16*5(%rbp)
	movaps	%xmm12, -16*4(%rbp)
	movaps	%xmm13, -16*3(%rbp)
	movaps	%xmm14, -16*2(%rbp)
	movaps	%xmm15, -16*1(%rbp)
___
$code.=<<___;
	mov	%rbp, %rsp
	pop	%rbp
	ret
.size	ecp_nistz256_avx2_point_add_affines_x4,.-ecp_nistz256_avx2_point_add_affines_x4

################################################################################
# void ecp_nistz256_avx2_to_mont(void* RESULTx4, void *Ax4);
.globl	ecp_nistz256_avx2_to_mont
.type	ecp_nistz256_avx2_to_mont,\@function,2
.align	32
ecp_nistz256_avx2_to_mont:
	vzeroupper
___
$code.=<<___	if ($win64);
	lea	-8-16*10(%rsp), %rsp
	vmovaps	%xmm6, -8-16*10(%rax)
	vmovaps	%xmm7, -8-16*9(%rax)
	vmovaps	%xmm8, -8-16*8(%rax)
	vmovaps	%xmm9, -8-16*7(%rax)
	vmovaps	%xmm10, -8-16*6(%rax)
	vmovaps	%xmm11, -8-16*5(%rax)
	vmovaps	%xmm12, -8-16*4(%rax)
	vmovaps	%xmm13, -8-16*3(%rax)
	vmovaps	%xmm14, -8-16*2(%rax)
	vmovaps	%xmm15, -8-16*1(%rax)
___
$code.=<<___;
	vmovdqa	.LAVX2_AND_MASK(%rip), $AND_MASK
	lea	.LTO_MONT_AVX2(%rip), %rdx
	call	avx2_mul_x4
	call	avx2_normalize_n_store

	vzeroupper
___
$code.=<<___	if ($win64);
	movaps	16*0(%rsp), %xmm6
	movaps	16*1(%rsp), %xmm7
	movaps	16*2(%rsp), %xmm8
	movaps	16*3(%rsp), %xmm9
	movaps	16*4(%rsp), %xmm10
	movaps	16*5(%rsp), %xmm11
	movaps	16*6(%rsp), %xmm12
	movaps	16*7(%rsp), %xmm13
	movaps	16*8(%rsp), %xmm14
	movaps	16*9(%rsp), %xmm15
	lea	8+16*10(%rsp), %rsp
___
$code.=<<___;
	ret
.size	ecp_nistz256_avx2_to_mont,.-ecp_nistz256_avx2_to_mont

################################################################################
# void ecp_nistz256_avx2_from_mont(void* RESULTx4, void *Ax4);
.globl	ecp_nistz256_avx2_from_mont
.type	ecp_nistz256_avx2_from_mont,\@function,2
.align	32
ecp_nistz256_avx2_from_mont:
	vzeroupper
___
$code.=<<___	if ($win64);
	lea	-8-16*10(%rsp), %rsp
	vmovaps	%xmm6, -8-16*10(%rax)
	vmovaps	%xmm7, -8-16*9(%rax)
	vmovaps	%xmm8, -8-16*8(%rax)
	vmovaps	%xmm9, -8-16*7(%rax)
	vmovaps	%xmm10, -8-16*6(%rax)
	vmovaps	%xmm11, -8-16*5(%rax)
	vmovaps	%xmm12, -8-16*4(%rax)
	vmovaps	%xmm13, -8-16*3(%rax)
	vmovaps	%xmm14, -8-16*2(%rax)
	vmovaps	%xmm15, -8-16*1(%rax)
___
$code.=<<___;
	vmovdqa	.LAVX2_AND_MASK(%rip), $AND_MASK
	lea	.LFROM_MONT_AVX2(%rip), %rdx
	call	avx2_mul_x4
	call	avx2_normalize_n_store

	vzeroupper
___
$code.=<<___	if ($win64);
	movaps	16*0(%rsp), %xmm6
	movaps	16*1(%rsp), %xmm7
	movaps	16*2(%rsp), %xmm8
	movaps	16*3(%rsp), %xmm9
	movaps	16*4(%rsp), %xmm10
	movaps	16*5(%rsp), %xmm11
	movaps	16*6(%rsp), %xmm12
	movaps	16*7(%rsp), %xmm13
	movaps	16*8(%rsp), %xmm14
	movaps	16*9(%rsp), %xmm15
	lea	8+16*10(%rsp), %rsp
___
$code.=<<___;
	ret
.size	ecp_nistz256_avx2_from_mont,.-ecp_nistz256_avx2_from_mont

################################################################################
# void ecp_nistz256_avx2_set1(void* RESULTx4);
.globl	ecp_nistz256_avx2_set1
.type	ecp_nistz256_avx2_set1,\@function,1
.align	32
ecp_nistz256_avx2_set1:
	lea	.LONE+128(%rip), %rax
	lea	128(%rdi), %rdi
	vzeroupper
	vmovdqa	32*0-128(%rax), %ymm0
	vmovdqa	32*1-128(%rax), %ymm1
	vmovdqa	32*2-128(%rax), %ymm2
	vmovdqa	32*3-128(%rax), %ymm3
	vmovdqa	32*4-128(%rax), %ymm4
	vmovdqa	32*5-128(%rax), %ymm5
	vmovdqa	%ymm0, 32*0-128(%rdi)
	vmovdqa	32*6-128(%rax), %ymm0
	vmovdqa	%ymm1, 32*1-128(%rdi)
	vmovdqa	32*7-128(%rax), %ymm1
	vmovdqa	%ymm2, 32*2-128(%rdi)
	vmovdqa	32*8-128(%rax), %ymm2
	vmovdqa	%ymm3, 32*3-128(%rdi)
	vmovdqa	%ymm4, 32*4-128(%rdi)
	vmovdqa	%ymm5, 32*5-128(%rdi)
	vmovdqa	%ymm0, 32*6-128(%rdi)
	vmovdqa	%ymm1, 32*7-128(%rdi)
	vmovdqa	%ymm2, 32*8-128(%rdi)

	vzeroupper
	ret
.size	ecp_nistz256_avx2_set1,.-ecp_nistz256_avx2_set1
___
}
{
################################################################################
# void ecp_nistz256_avx2_multi_gather_w7(void* RESULT, void *in,
#			    int index0, int index1, int index2, int index3);
################################################################################

my ($val,$in_t,$index0,$index1,$index2,$index3)=("%rdi","%rsi","%edx","%ecx","%r8d","%r9d");
my ($INDEX0,$INDEX1,$INDEX2,$INDEX3)=map("%ymm$_",(0..3));
my ($R0a,$R0b,$R1a,$R1b,$R2a,$R2b,$R3a,$R3b)=map("%ymm$_",(4..11));
my ($M0,$T0,$T1,$TMP0)=map("%ymm$_",(12..15));

$code.=<<___;
.globl	ecp_nistz256_avx2_multi_gather_w7
.type	ecp_nistz256_avx2_multi_gather_w7,\@function,6
.align	32
ecp_nistz256_avx2_multi_gather_w7:
	vzeroupper
___
$code.=<<___	if ($win64);
	lea	-8-16*10(%rsp), %rsp
	vmovaps	%xmm6, -8-16*10(%rax)
	vmovaps	%xmm7, -8-16*9(%rax)
	vmovaps	%xmm8, -8-16*8(%rax)
	vmovaps	%xmm9, -8-16*7(%rax)
	vmovaps	%xmm10, -8-16*6(%rax)
	vmovaps	%xmm11, -8-16*5(%rax)
	vmovaps	%xmm12, -8-16*4(%rax)
	vmovaps	%xmm13, -8-16*3(%rax)
	vmovaps	%xmm14, -8-16*2(%rax)
	vmovaps	%xmm15, -8-16*1(%rax)
___
$code.=<<___;
	lea	.LIntOne(%rip), %rax

	vmovd	$index0, %xmm0
	vmovd	$index1, %xmm1
	vmovd	$index2, %xmm2
	vmovd	$index3, %xmm3

	vpxor	$R0a, $R0a, $R0a
	vpxor	$R0b, $R0b, $R0b
	vpxor	$R1a, $R1a, $R1a
	vpxor	$R1b, $R1b, $R1b
	vpxor	$R2a, $R2a, $R2a
	vpxor	$R2b, $R2b, $R2b
	vpxor	$R3a, $R3a, $R3a
	vpxor	$R3b, $R3b, $R3b
	vmovdqa	(%rax), $M0

	vpermd	$INDEX0, $R0a, $INDEX0
	vpermd	$INDEX1, $R0a, $INDEX1
	vpermd	$INDEX2, $R0a, $INDEX2
	vpermd	$INDEX3, $R0a, $INDEX3

	mov	\$64, %ecx
	lea	112($val), $val		# size optimization
	jmp	.Lmulti_select_loop_avx2

# INDEX=0, corresponds to the point at infty (0,0)
.align	32
.Lmulti_select_loop_avx2:
	vpcmpeqd	$INDEX0, $M0, $TMP0

	vmovdqa		`32*0+32*64*2*0`($in_t), $T0
	vmovdqa		`32*1+32*64*2*0`($in_t), $T1
	vpand		$TMP0, $T0, $T0
	vpand		$TMP0, $T1, $T1
	vpxor		$T0, $R0a, $R0a
	vpxor		$T1, $R0b, $R0b

	vpcmpeqd	$INDEX1, $M0, $TMP0

	vmovdqa		`32*0+32*64*2*1`($in_t), $T0
	vmovdqa		`32*1+32*64*2*1`($in_t), $T1
	vpand		$TMP0, $T0, $T0
	vpand		$TMP0, $T1, $T1
	vpxor		$T0, $R1a, $R1a
	vpxor		$T1, $R1b, $R1b

	vpcmpeqd	$INDEX2, $M0, $TMP0

	vmovdqa		`32*0+32*64*2*2`($in_t), $T0
	vmovdqa		`32*1+32*64*2*2`($in_t), $T1
	vpand		$TMP0, $T0, $T0
	vpand		$TMP0, $T1, $T1
	vpxor		$T0, $R2a, $R2a
	vpxor		$T1, $R2b, $R2b

	vpcmpeqd	$INDEX3, $M0, $TMP0

	vmovdqa		`32*0+32*64*2*3`($in_t), $T0
	vmovdqa		`32*1+32*64*2*3`($in_t), $T1
	vpand		$TMP0, $T0, $T0
	vpand		$TMP0, $T1, $T1
	vpxor		$T0, $R3a, $R3a
	vpxor		$T1, $R3b, $R3b

	vpaddd		(%rax), $M0, $M0	# increment
	lea		32*2($in_t), $in_t

        dec	%ecx
	jnz	.Lmulti_select_loop_avx2

	vmovdqu	$R0a, 32*0-112($val)
	vmovdqu	$R0b, 32*1-112($val)
	vmovdqu	$R1a, 32*2-112($val)
	vmovdqu	$R1b, 32*3-112($val)
	vmovdqu	$R2a, 32*4-112($val)
	vmovdqu	$R2b, 32*5-112($val)
	vmovdqu	$R3a, 32*6-112($val)
	vmovdqu	$R3b, 32*7-112($val)

	vzeroupper
___
$code.=<<___	if ($win64);
	movaps	16*0(%rsp), %xmm6
	movaps	16*1(%rsp), %xmm7
	movaps	16*2(%rsp), %xmm8
	movaps	16*3(%rsp), %xmm9
	movaps	16*4(%rsp), %xmm10
	movaps	16*5(%rsp), %xmm11
	movaps	16*6(%rsp), %xmm12
	movaps	16*7(%rsp), %xmm13
	movaps	16*8(%rsp), %xmm14
	movaps	16*9(%rsp), %xmm15
	lea	8+16*10(%rsp), %rsp
___
$code.=<<___;
	ret
.size	ecp_nistz256_avx2_multi_gather_w7,.-ecp_nistz256_avx2_multi_gather_w7

.extern	OPENSSL_ia32cap_P
.globl	ecp_nistz_avx2_eligible
.type	ecp_nistz_avx2_eligible,\@abi-omnipotent
.align	32
ecp_nistz_avx2_eligible:
	mov	OPENSSL_ia32cap_P+8(%rip),%eax
	shr	\$5,%eax
	and	\$1,%eax
	ret
.size	ecp_nistz_avx2_eligible,.-ecp_nistz_avx2_eligible
___
}
}} else {{	# assembler is too old
$code.=<<___;
.text

.globl	ecp_nistz256_avx2_transpose_convert
.globl	ecp_nistz256_avx2_convert_transpose_back
.globl	ecp_nistz256_avx2_point_add_affine_x4
.globl	ecp_nistz256_avx2_point_add_affines_x4
.globl	ecp_nistz256_avx2_to_mont
.globl	ecp_nistz256_avx2_from_mont
.globl	ecp_nistz256_avx2_set1
.globl	ecp_nistz256_avx2_multi_gather_w7
.type	ecp_nistz256_avx2_multi_gather_w7,\@abi-omnipotent
ecp_nistz256_avx2_transpose_convert:
ecp_nistz256_avx2_convert_transpose_back:
ecp_nistz256_avx2_point_add_affine_x4:
ecp_nistz256_avx2_point_add_affines_x4:
ecp_nistz256_avx2_to_mont:
ecp_nistz256_avx2_from_mont:
ecp_nistz256_avx2_set1:
ecp_nistz256_avx2_multi_gather_w7:
	.byte	0x0f,0x0b	# ud2
	ret
.size	ecp_nistz256_avx2_multi_gather_w7,.-ecp_nistz256_avx2_multi_gather_w7

.globl	ecp_nistz_avx2_eligible
.type	ecp_nistz_avx2_eligible,\@abi-omnipotent
ecp_nistz_avx2_eligible:
	xor	%eax,%eax
	ret
.size	ecp_nistz_avx2_eligible,.-ecp_nistz_avx2_eligible
___
}}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval($1)/geo;

	print $_,"\n";
}

close STDOUT;

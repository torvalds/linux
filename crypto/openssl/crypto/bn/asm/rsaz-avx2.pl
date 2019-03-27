#! /usr/bin/env perl
# Copyright 2013-2019 The OpenSSL Project Authors. All Rights Reserved.
# Copyright (c) 2012, Intel Corporation. All Rights Reserved.
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
# References:
# [1] S. Gueron, V. Krasnov: "Software Implementation of Modular
#     Exponentiation,  Using Advanced Vector Instructions Architectures",
#     F. Ozbudak and F. Rodriguez-Henriquez (Eds.): WAIFI 2012, LNCS 7369,
#     pp. 119?135, 2012. Springer-Verlag Berlin Heidelberg 2012
# [2] S. Gueron: "Efficient Software Implementations of Modular
#     Exponentiation", Journal of Cryptographic Engineering 2:31-43 (2012).
# [3] S. Gueron, V. Krasnov: "Speeding up Big-numbers Squaring",IEEE
#     Proceedings of 9th International Conference on Information Technology:
#     New Generations (ITNG 2012), pp.821-823 (2012)
# [4] S. Gueron, V. Krasnov: "[PATCH] Efficient and side channel analysis
#     resistant 1024-bit modular exponentiation, for optimizing RSA2048
#     on AVX2 capable x86_64 platforms",
#     http://rt.openssl.org/Ticket/Display.html?id=2850&user=guest&pass=guest
#
# +13% improvement over original submission by <appro@openssl.org>
#
# rsa2048 sign/sec	OpenSSL 1.0.1	scalar(*)	this
# 2.3GHz Haswell	621		765/+23%	1113/+79%
# 2.3GHz Broadwell(**)	688		1200(***)/+74%	1120/+63%
#
# (*)	if system doesn't support AVX2, for reference purposes;
# (**)	scaled to 2.3GHz to simplify comparison;
# (***)	scalar AD*X code is faster than AVX2 and is preferred code
#	path for Broadwell;

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

if (`$ENV{CC} -Wa,-v -c -o /dev/null -x assembler /dev/null 2>&1`
		=~ /GNU assembler version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.19) + ($1>=2.22);
	$addx = ($1>=2.23);
}

if (!$avx && $win64 && ($flavour =~ /nasm/ || $ENV{ASM} =~ /nasm/) &&
	    `nasm -v 2>&1` =~ /NASM version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.09) + ($1>=2.10);
	$addx = ($1>=2.10);
}

if (!$avx && $win64 && ($flavour =~ /masm/ || $ENV{ASM} =~ /ml64/) &&
	    `ml64 2>&1` =~ /Version ([0-9]+)\./) {
	$avx = ($1>=10) + ($1>=11);
	$addx = ($1>=11);
}

if (!$avx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|based on LLVM) ([3-9])\.([0-9]+)/) {
	my $ver = $2 + $3/100.0;	# 3.1->3.01, 3.10->3.10
	$avx = ($ver>=3.0) + ($ver>=3.01);
	$addx = ($ver>=3.03);
}

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT = *OUT;

if ($avx>1) {{{
{ # void AMS_WW(
my $rp="%rdi";	# BN_ULONG *rp,
my $ap="%rsi";	# const BN_ULONG *ap,
my $np="%rdx";	# const BN_ULONG *np,
my $n0="%ecx";	# const BN_ULONG n0,
my $rep="%r8d";	# int repeat);

# The registers that hold the accumulated redundant result
# The AMM works on 1024 bit operands, and redundant word size is 29
# Therefore: ceil(1024/29)/4 = 9
my $ACC0="%ymm0";
my $ACC1="%ymm1";
my $ACC2="%ymm2";
my $ACC3="%ymm3";
my $ACC4="%ymm4";
my $ACC5="%ymm5";
my $ACC6="%ymm6";
my $ACC7="%ymm7";
my $ACC8="%ymm8";
my $ACC9="%ymm9";
# Registers that hold the broadcasted words of bp, currently used
my $B1="%ymm10";
my $B2="%ymm11";
# Registers that hold the broadcasted words of Y, currently used
my $Y1="%ymm12";
my $Y2="%ymm13";
# Helper registers
my $TEMP1="%ymm14";
my $AND_MASK="%ymm15";
# alu registers that hold the first words of the ACC
my $r0="%r9";
my $r1="%r10";
my $r2="%r11";
my $r3="%r12";

my $i="%r14d";			# loop counter
my $tmp = "%r15";

my $FrameSize=32*18+32*8;	# place for A^2 and 2*A

my $aap=$r0;
my $tp0="%rbx";
my $tp1=$r3;
my $tpa=$tmp;

$np="%r13";			# reassigned argument

$code.=<<___;
.text

.globl	rsaz_1024_sqr_avx2
.type	rsaz_1024_sqr_avx2,\@function,5
.align	64
rsaz_1024_sqr_avx2:		# 702 cycles, 14% faster than rsaz_1024_mul_avx2
.cfi_startproc
	lea	(%rsp), %rax
.cfi_def_cfa_register	%rax
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	vzeroupper
___
$code.=<<___ if ($win64);
	lea	-0xa8(%rsp),%rsp
	vmovaps	%xmm6,-0xd8(%rax)
	vmovaps	%xmm7,-0xc8(%rax)
	vmovaps	%xmm8,-0xb8(%rax)
	vmovaps	%xmm9,-0xa8(%rax)
	vmovaps	%xmm10,-0x98(%rax)
	vmovaps	%xmm11,-0x88(%rax)
	vmovaps	%xmm12,-0x78(%rax)
	vmovaps	%xmm13,-0x68(%rax)
	vmovaps	%xmm14,-0x58(%rax)
	vmovaps	%xmm15,-0x48(%rax)
.Lsqr_1024_body:
___
$code.=<<___;
	mov	%rax,%rbp
.cfi_def_cfa_register	%rbp
	mov	%rdx, $np			# reassigned argument
	sub	\$$FrameSize, %rsp
	mov	$np, $tmp
	sub	\$-128, $rp			# size optimization
	sub	\$-128, $ap
	sub	\$-128, $np

	and	\$4095, $tmp			# see if $np crosses page
	add	\$32*10, $tmp
	shr	\$12, $tmp
	vpxor	$ACC9,$ACC9,$ACC9
	jz	.Lsqr_1024_no_n_copy

	# unaligned 256-bit load that crosses page boundary can
	# cause >2x performance degradation here, so if $np does
	# cross page boundary, copy it to stack and make sure stack
	# frame doesn't...
	sub		\$32*10,%rsp
	vmovdqu		32*0-128($np), $ACC0
	and		\$-2048, %rsp
	vmovdqu		32*1-128($np), $ACC1
	vmovdqu		32*2-128($np), $ACC2
	vmovdqu		32*3-128($np), $ACC3
	vmovdqu		32*4-128($np), $ACC4
	vmovdqu		32*5-128($np), $ACC5
	vmovdqu		32*6-128($np), $ACC6
	vmovdqu		32*7-128($np), $ACC7
	vmovdqu		32*8-128($np), $ACC8
	lea		$FrameSize+128(%rsp),$np
	vmovdqu		$ACC0, 32*0-128($np)
	vmovdqu		$ACC1, 32*1-128($np)
	vmovdqu		$ACC2, 32*2-128($np)
	vmovdqu		$ACC3, 32*3-128($np)
	vmovdqu		$ACC4, 32*4-128($np)
	vmovdqu		$ACC5, 32*5-128($np)
	vmovdqu		$ACC6, 32*6-128($np)
	vmovdqu		$ACC7, 32*7-128($np)
	vmovdqu		$ACC8, 32*8-128($np)
	vmovdqu		$ACC9, 32*9-128($np)	# $ACC9 is zero

.Lsqr_1024_no_n_copy:
	and		\$-1024, %rsp

	vmovdqu		32*1-128($ap), $ACC1
	vmovdqu		32*2-128($ap), $ACC2
	vmovdqu		32*3-128($ap), $ACC3
	vmovdqu		32*4-128($ap), $ACC4
	vmovdqu		32*5-128($ap), $ACC5
	vmovdqu		32*6-128($ap), $ACC6
	vmovdqu		32*7-128($ap), $ACC7
	vmovdqu		32*8-128($ap), $ACC8

	lea	192(%rsp), $tp0			# 64+128=192
	vmovdqu	.Land_mask(%rip), $AND_MASK
	jmp	.LOOP_GRANDE_SQR_1024

.align	32
.LOOP_GRANDE_SQR_1024:
	lea	32*18+128(%rsp), $aap		# size optimization
	lea	448(%rsp), $tp1			# 64+128+256=448

	# the squaring is performed as described in Variant B of
	# "Speeding up Big-Number Squaring", so start by calculating
	# the A*2=A+A vector
	vpaddq		$ACC1, $ACC1, $ACC1
	 vpbroadcastq	32*0-128($ap), $B1
	vpaddq		$ACC2, $ACC2, $ACC2
	vmovdqa		$ACC1, 32*0-128($aap)
	vpaddq		$ACC3, $ACC3, $ACC3
	vmovdqa		$ACC2, 32*1-128($aap)
	vpaddq		$ACC4, $ACC4, $ACC4
	vmovdqa		$ACC3, 32*2-128($aap)
	vpaddq		$ACC5, $ACC5, $ACC5
	vmovdqa		$ACC4, 32*3-128($aap)
	vpaddq		$ACC6, $ACC6, $ACC6
	vmovdqa		$ACC5, 32*4-128($aap)
	vpaddq		$ACC7, $ACC7, $ACC7
	vmovdqa		$ACC6, 32*5-128($aap)
	vpaddq		$ACC8, $ACC8, $ACC8
	vmovdqa		$ACC7, 32*6-128($aap)
	vpxor		$ACC9, $ACC9, $ACC9
	vmovdqa		$ACC8, 32*7-128($aap)

	vpmuludq	32*0-128($ap), $B1, $ACC0
	 vpbroadcastq	32*1-128($ap), $B2
	 vmovdqu	$ACC9, 32*9-192($tp0)	# zero upper half
	vpmuludq	$B1, $ACC1, $ACC1
	 vmovdqu	$ACC9, 32*10-448($tp1)
	vpmuludq	$B1, $ACC2, $ACC2
	 vmovdqu	$ACC9, 32*11-448($tp1)
	vpmuludq	$B1, $ACC3, $ACC3
	 vmovdqu	$ACC9, 32*12-448($tp1)
	vpmuludq	$B1, $ACC4, $ACC4
	 vmovdqu	$ACC9, 32*13-448($tp1)
	vpmuludq	$B1, $ACC5, $ACC5
	 vmovdqu	$ACC9, 32*14-448($tp1)
	vpmuludq	$B1, $ACC6, $ACC6
	 vmovdqu	$ACC9, 32*15-448($tp1)
	vpmuludq	$B1, $ACC7, $ACC7
	 vmovdqu	$ACC9, 32*16-448($tp1)
	vpmuludq	$B1, $ACC8, $ACC8
	 vpbroadcastq	32*2-128($ap), $B1
	 vmovdqu	$ACC9, 32*17-448($tp1)

	mov	$ap, $tpa
	mov 	\$4, $i
	jmp	.Lsqr_entry_1024
___
$TEMP0=$Y1;
$TEMP2=$Y2;
$code.=<<___;
.align	32
.LOOP_SQR_1024:
	 vpbroadcastq	32*1-128($tpa), $B2
	vpmuludq	32*0-128($ap), $B1, $ACC0
	vpaddq		32*0-192($tp0), $ACC0, $ACC0
	vpmuludq	32*0-128($aap), $B1, $ACC1
	vpaddq		32*1-192($tp0), $ACC1, $ACC1
	vpmuludq	32*1-128($aap), $B1, $ACC2
	vpaddq		32*2-192($tp0), $ACC2, $ACC2
	vpmuludq	32*2-128($aap), $B1, $ACC3
	vpaddq		32*3-192($tp0), $ACC3, $ACC3
	vpmuludq	32*3-128($aap), $B1, $ACC4
	vpaddq		32*4-192($tp0), $ACC4, $ACC4
	vpmuludq	32*4-128($aap), $B1, $ACC5
	vpaddq		32*5-192($tp0), $ACC5, $ACC5
	vpmuludq	32*5-128($aap), $B1, $ACC6
	vpaddq		32*6-192($tp0), $ACC6, $ACC6
	vpmuludq	32*6-128($aap), $B1, $ACC7
	vpaddq		32*7-192($tp0), $ACC7, $ACC7
	vpmuludq	32*7-128($aap), $B1, $ACC8
	 vpbroadcastq	32*2-128($tpa), $B1
	vpaddq		32*8-192($tp0), $ACC8, $ACC8
.Lsqr_entry_1024:
	vmovdqu		$ACC0, 32*0-192($tp0)
	vmovdqu		$ACC1, 32*1-192($tp0)

	vpmuludq	32*1-128($ap), $B2, $TEMP0
	vpaddq		$TEMP0, $ACC2, $ACC2
	vpmuludq	32*1-128($aap), $B2, $TEMP1
	vpaddq		$TEMP1, $ACC3, $ACC3
	vpmuludq	32*2-128($aap), $B2, $TEMP2
	vpaddq		$TEMP2, $ACC4, $ACC4
	vpmuludq	32*3-128($aap), $B2, $TEMP0
	vpaddq		$TEMP0, $ACC5, $ACC5
	vpmuludq	32*4-128($aap), $B2, $TEMP1
	vpaddq		$TEMP1, $ACC6, $ACC6
	vpmuludq	32*5-128($aap), $B2, $TEMP2
	vpaddq		$TEMP2, $ACC7, $ACC7
	vpmuludq	32*6-128($aap), $B2, $TEMP0
	vpaddq		$TEMP0, $ACC8, $ACC8
	vpmuludq	32*7-128($aap), $B2, $ACC0
	 vpbroadcastq	32*3-128($tpa), $B2
	vpaddq		32*9-192($tp0), $ACC0, $ACC0

	vmovdqu		$ACC2, 32*2-192($tp0)
	vmovdqu		$ACC3, 32*3-192($tp0)

	vpmuludq	32*2-128($ap), $B1, $TEMP2
	vpaddq		$TEMP2, $ACC4, $ACC4
	vpmuludq	32*2-128($aap), $B1, $TEMP0
	vpaddq		$TEMP0, $ACC5, $ACC5
	vpmuludq	32*3-128($aap), $B1, $TEMP1
	vpaddq		$TEMP1, $ACC6, $ACC6
	vpmuludq	32*4-128($aap), $B1, $TEMP2
	vpaddq		$TEMP2, $ACC7, $ACC7
	vpmuludq	32*5-128($aap), $B1, $TEMP0
	vpaddq		$TEMP0, $ACC8, $ACC8
	vpmuludq	32*6-128($aap), $B1, $TEMP1
	vpaddq		$TEMP1, $ACC0, $ACC0
	vpmuludq	32*7-128($aap), $B1, $ACC1
	 vpbroadcastq	32*4-128($tpa), $B1
	vpaddq		32*10-448($tp1), $ACC1, $ACC1

	vmovdqu		$ACC4, 32*4-192($tp0)
	vmovdqu		$ACC5, 32*5-192($tp0)

	vpmuludq	32*3-128($ap), $B2, $TEMP0
	vpaddq		$TEMP0, $ACC6, $ACC6
	vpmuludq	32*3-128($aap), $B2, $TEMP1
	vpaddq		$TEMP1, $ACC7, $ACC7
	vpmuludq	32*4-128($aap), $B2, $TEMP2
	vpaddq		$TEMP2, $ACC8, $ACC8
	vpmuludq	32*5-128($aap), $B2, $TEMP0
	vpaddq		$TEMP0, $ACC0, $ACC0
	vpmuludq	32*6-128($aap), $B2, $TEMP1
	vpaddq		$TEMP1, $ACC1, $ACC1
	vpmuludq	32*7-128($aap), $B2, $ACC2
	 vpbroadcastq	32*5-128($tpa), $B2
	vpaddq		32*11-448($tp1), $ACC2, $ACC2

	vmovdqu		$ACC6, 32*6-192($tp0)
	vmovdqu		$ACC7, 32*7-192($tp0)

	vpmuludq	32*4-128($ap), $B1, $TEMP0
	vpaddq		$TEMP0, $ACC8, $ACC8
	vpmuludq	32*4-128($aap), $B1, $TEMP1
	vpaddq		$TEMP1, $ACC0, $ACC0
	vpmuludq	32*5-128($aap), $B1, $TEMP2
	vpaddq		$TEMP2, $ACC1, $ACC1
	vpmuludq	32*6-128($aap), $B1, $TEMP0
	vpaddq		$TEMP0, $ACC2, $ACC2
	vpmuludq	32*7-128($aap), $B1, $ACC3
	 vpbroadcastq	32*6-128($tpa), $B1
	vpaddq		32*12-448($tp1), $ACC3, $ACC3

	vmovdqu		$ACC8, 32*8-192($tp0)
	vmovdqu		$ACC0, 32*9-192($tp0)
	lea		8($tp0), $tp0

	vpmuludq	32*5-128($ap), $B2, $TEMP2
	vpaddq		$TEMP2, $ACC1, $ACC1
	vpmuludq	32*5-128($aap), $B2, $TEMP0
	vpaddq		$TEMP0, $ACC2, $ACC2
	vpmuludq	32*6-128($aap), $B2, $TEMP1
	vpaddq		$TEMP1, $ACC3, $ACC3
	vpmuludq	32*7-128($aap), $B2, $ACC4
	 vpbroadcastq	32*7-128($tpa), $B2
	vpaddq		32*13-448($tp1), $ACC4, $ACC4

	vmovdqu		$ACC1, 32*10-448($tp1)
	vmovdqu		$ACC2, 32*11-448($tp1)

	vpmuludq	32*6-128($ap), $B1, $TEMP0
	vpaddq		$TEMP0, $ACC3, $ACC3
	vpmuludq	32*6-128($aap), $B1, $TEMP1
	 vpbroadcastq	32*8-128($tpa), $ACC0		# borrow $ACC0 for $B1
	vpaddq		$TEMP1, $ACC4, $ACC4
	vpmuludq	32*7-128($aap), $B1, $ACC5
	 vpbroadcastq	32*0+8-128($tpa), $B1		# for next iteration
	vpaddq		32*14-448($tp1), $ACC5, $ACC5

	vmovdqu		$ACC3, 32*12-448($tp1)
	vmovdqu		$ACC4, 32*13-448($tp1)
	lea		8($tpa), $tpa

	vpmuludq	32*7-128($ap), $B2, $TEMP0
	vpaddq		$TEMP0, $ACC5, $ACC5
	vpmuludq	32*7-128($aap), $B2, $ACC6
	vpaddq		32*15-448($tp1), $ACC6, $ACC6

	vpmuludq	32*8-128($ap), $ACC0, $ACC7
	vmovdqu		$ACC5, 32*14-448($tp1)
	vpaddq		32*16-448($tp1), $ACC7, $ACC7
	vmovdqu		$ACC6, 32*15-448($tp1)
	vmovdqu		$ACC7, 32*16-448($tp1)
	lea		8($tp1), $tp1

	dec	$i
	jnz	.LOOP_SQR_1024
___
$ZERO = $ACC9;
$TEMP0 = $B1;
$TEMP2 = $B2;
$TEMP3 = $Y1;
$TEMP4 = $Y2;
$code.=<<___;
	# we need to fix indices 32-39 to avoid overflow
	vmovdqu		32*8(%rsp), $ACC8		# 32*8-192($tp0),
	vmovdqu		32*9(%rsp), $ACC1		# 32*9-192($tp0)
	vmovdqu		32*10(%rsp), $ACC2		# 32*10-192($tp0)
	lea		192(%rsp), $tp0			# 64+128=192

	vpsrlq		\$29, $ACC8, $TEMP1
	vpand		$AND_MASK, $ACC8, $ACC8
	vpsrlq		\$29, $ACC1, $TEMP2
	vpand		$AND_MASK, $ACC1, $ACC1

	vpermq		\$0x93, $TEMP1, $TEMP1
	vpxor		$ZERO, $ZERO, $ZERO
	vpermq		\$0x93, $TEMP2, $TEMP2

	vpblendd	\$3, $ZERO, $TEMP1, $TEMP0
	vpblendd	\$3, $TEMP1, $TEMP2, $TEMP1
	vpaddq		$TEMP0, $ACC8, $ACC8
	vpblendd	\$3, $TEMP2, $ZERO, $TEMP2
	vpaddq		$TEMP1, $ACC1, $ACC1
	vpaddq		$TEMP2, $ACC2, $ACC2
	vmovdqu		$ACC1, 32*9-192($tp0)
	vmovdqu		$ACC2, 32*10-192($tp0)

	mov	(%rsp), %rax
	mov	8(%rsp), $r1
	mov	16(%rsp), $r2
	mov	24(%rsp), $r3
	vmovdqu	32*1(%rsp), $ACC1
	vmovdqu	32*2-192($tp0), $ACC2
	vmovdqu	32*3-192($tp0), $ACC3
	vmovdqu	32*4-192($tp0), $ACC4
	vmovdqu	32*5-192($tp0), $ACC5
	vmovdqu	32*6-192($tp0), $ACC6
	vmovdqu	32*7-192($tp0), $ACC7

	mov	%rax, $r0
	imull	$n0, %eax
	and	\$0x1fffffff, %eax
	vmovd	%eax, $Y1

	mov	%rax, %rdx
	imulq	-128($np), %rax
	 vpbroadcastq	$Y1, $Y1
	add	%rax, $r0
	mov	%rdx, %rax
	imulq	8-128($np), %rax
	shr	\$29, $r0
	add	%rax, $r1
	mov	%rdx, %rax
	imulq	16-128($np), %rax
	add	$r0, $r1
	add	%rax, $r2
	imulq	24-128($np), %rdx
	add	%rdx, $r3

	mov	$r1, %rax
	imull	$n0, %eax
	and	\$0x1fffffff, %eax

	mov \$9, $i
	jmp .LOOP_REDUCE_1024

.align	32
.LOOP_REDUCE_1024:
	vmovd	%eax, $Y2
	vpbroadcastq	$Y2, $Y2

	vpmuludq	32*1-128($np), $Y1, $TEMP0
	 mov	%rax, %rdx
	 imulq	-128($np), %rax
	vpaddq		$TEMP0, $ACC1, $ACC1
	 add	%rax, $r1
	vpmuludq	32*2-128($np), $Y1, $TEMP1
	 mov	%rdx, %rax
	 imulq	8-128($np), %rax
	vpaddq		$TEMP1, $ACC2, $ACC2
	vpmuludq	32*3-128($np), $Y1, $TEMP2
	 .byte	0x67
	 add	%rax, $r2
	 .byte	0x67
	 mov	%rdx, %rax
	 imulq	16-128($np), %rax
	 shr	\$29, $r1
	vpaddq		$TEMP2, $ACC3, $ACC3
	vpmuludq	32*4-128($np), $Y1, $TEMP0
	 add	%rax, $r3
	 add	$r1, $r2
	vpaddq		$TEMP0, $ACC4, $ACC4
	vpmuludq	32*5-128($np), $Y1, $TEMP1
	 mov	$r2, %rax
	 imull	$n0, %eax
	vpaddq		$TEMP1, $ACC5, $ACC5
	vpmuludq	32*6-128($np), $Y1, $TEMP2
	 and	\$0x1fffffff, %eax
	vpaddq		$TEMP2, $ACC6, $ACC6
	vpmuludq	32*7-128($np), $Y1, $TEMP0
	vpaddq		$TEMP0, $ACC7, $ACC7
	vpmuludq	32*8-128($np), $Y1, $TEMP1
	 vmovd	%eax, $Y1
	 #vmovdqu	32*1-8-128($np), $TEMP2		# moved below
	vpaddq		$TEMP1, $ACC8, $ACC8
	 #vmovdqu	32*2-8-128($np), $TEMP0		# moved below
	 vpbroadcastq	$Y1, $Y1

	vpmuludq	32*1-8-128($np), $Y2, $TEMP2	# see above
	vmovdqu		32*3-8-128($np), $TEMP1
	 mov	%rax, %rdx
	 imulq	-128($np), %rax
	vpaddq		$TEMP2, $ACC1, $ACC1
	vpmuludq	32*2-8-128($np), $Y2, $TEMP0	# see above
	vmovdqu		32*4-8-128($np), $TEMP2
	 add	%rax, $r2
	 mov	%rdx, %rax
	 imulq	8-128($np), %rax
	vpaddq		$TEMP0, $ACC2, $ACC2
	 add	$r3, %rax
	 shr	\$29, $r2
	vpmuludq	$Y2, $TEMP1, $TEMP1
	vmovdqu		32*5-8-128($np), $TEMP0
	 add	$r2, %rax
	vpaddq		$TEMP1, $ACC3, $ACC3
	vpmuludq	$Y2, $TEMP2, $TEMP2
	vmovdqu		32*6-8-128($np), $TEMP1
	 .byte	0x67
	 mov	%rax, $r3
	 imull	$n0, %eax
	vpaddq		$TEMP2, $ACC4, $ACC4
	vpmuludq	$Y2, $TEMP0, $TEMP0
	.byte	0xc4,0x41,0x7e,0x6f,0x9d,0x58,0x00,0x00,0x00	# vmovdqu		32*7-8-128($np), $TEMP2
	 and	\$0x1fffffff, %eax
	vpaddq		$TEMP0, $ACC5, $ACC5
	vpmuludq	$Y2, $TEMP1, $TEMP1
	vmovdqu		32*8-8-128($np), $TEMP0
	vpaddq		$TEMP1, $ACC6, $ACC6
	vpmuludq	$Y2, $TEMP2, $TEMP2
	vmovdqu		32*9-8-128($np), $ACC9
	 vmovd	%eax, $ACC0			# borrow ACC0 for Y2
	 imulq	-128($np), %rax
	vpaddq		$TEMP2, $ACC7, $ACC7
	vpmuludq	$Y2, $TEMP0, $TEMP0
	 vmovdqu	32*1-16-128($np), $TEMP1
	 vpbroadcastq	$ACC0, $ACC0
	vpaddq		$TEMP0, $ACC8, $ACC8
	vpmuludq	$Y2, $ACC9, $ACC9
	 vmovdqu	32*2-16-128($np), $TEMP2
	 add	%rax, $r3

___
($ACC0,$Y2)=($Y2,$ACC0);
$code.=<<___;
	 vmovdqu	32*1-24-128($np), $ACC0
	vpmuludq	$Y1, $TEMP1, $TEMP1
	vmovdqu		32*3-16-128($np), $TEMP0
	vpaddq		$TEMP1, $ACC1, $ACC1
	 vpmuludq	$Y2, $ACC0, $ACC0
	vpmuludq	$Y1, $TEMP2, $TEMP2
	.byte	0xc4,0x41,0x7e,0x6f,0xb5,0xf0,0xff,0xff,0xff	# vmovdqu		32*4-16-128($np), $TEMP1
	 vpaddq		$ACC1, $ACC0, $ACC0
	vpaddq		$TEMP2, $ACC2, $ACC2
	vpmuludq	$Y1, $TEMP0, $TEMP0
	vmovdqu		32*5-16-128($np), $TEMP2
	 .byte	0x67
	 vmovq		$ACC0, %rax
	 vmovdqu	$ACC0, (%rsp)		# transfer $r0-$r3
	vpaddq		$TEMP0, $ACC3, $ACC3
	vpmuludq	$Y1, $TEMP1, $TEMP1
	vmovdqu		32*6-16-128($np), $TEMP0
	vpaddq		$TEMP1, $ACC4, $ACC4
	vpmuludq	$Y1, $TEMP2, $TEMP2
	vmovdqu		32*7-16-128($np), $TEMP1
	vpaddq		$TEMP2, $ACC5, $ACC5
	vpmuludq	$Y1, $TEMP0, $TEMP0
	vmovdqu		32*8-16-128($np), $TEMP2
	vpaddq		$TEMP0, $ACC6, $ACC6
	vpmuludq	$Y1, $TEMP1, $TEMP1
	 shr	\$29, $r3
	vmovdqu		32*9-16-128($np), $TEMP0
	 add	$r3, %rax
	vpaddq		$TEMP1, $ACC7, $ACC7
	vpmuludq	$Y1, $TEMP2, $TEMP2
	 #vmovdqu	32*2-24-128($np), $TEMP1	# moved below
	 mov	%rax, $r0
	 imull	$n0, %eax
	vpaddq		$TEMP2, $ACC8, $ACC8
	vpmuludq	$Y1, $TEMP0, $TEMP0
	 and	\$0x1fffffff, %eax
	 vmovd	%eax, $Y1
	 vmovdqu	32*3-24-128($np), $TEMP2
	.byte	0x67
	vpaddq		$TEMP0, $ACC9, $ACC9
	 vpbroadcastq	$Y1, $Y1

	vpmuludq	32*2-24-128($np), $Y2, $TEMP1	# see above
	vmovdqu		32*4-24-128($np), $TEMP0
	 mov	%rax, %rdx
	 imulq	-128($np), %rax
	 mov	8(%rsp), $r1
	vpaddq		$TEMP1, $ACC2, $ACC1
	vpmuludq	$Y2, $TEMP2, $TEMP2
	vmovdqu		32*5-24-128($np), $TEMP1
	 add	%rax, $r0
	 mov	%rdx, %rax
	 imulq	8-128($np), %rax
	 .byte	0x67
	 shr	\$29, $r0
	 mov	16(%rsp), $r2
	vpaddq		$TEMP2, $ACC3, $ACC2
	vpmuludq	$Y2, $TEMP0, $TEMP0
	vmovdqu		32*6-24-128($np), $TEMP2
	 add	%rax, $r1
	 mov	%rdx, %rax
	 imulq	16-128($np), %rax
	vpaddq		$TEMP0, $ACC4, $ACC3
	vpmuludq	$Y2, $TEMP1, $TEMP1
	vmovdqu		32*7-24-128($np), $TEMP0
	 imulq	24-128($np), %rdx		# future $r3
	 add	%rax, $r2
	 lea	($r0,$r1), %rax
	vpaddq		$TEMP1, $ACC5, $ACC4
	vpmuludq	$Y2, $TEMP2, $TEMP2
	vmovdqu		32*8-24-128($np), $TEMP1
	 mov	%rax, $r1
	 imull	$n0, %eax
	vpmuludq	$Y2, $TEMP0, $TEMP0
	vpaddq		$TEMP2, $ACC6, $ACC5
	vmovdqu		32*9-24-128($np), $TEMP2
	 and	\$0x1fffffff, %eax
	vpaddq		$TEMP0, $ACC7, $ACC6
	vpmuludq	$Y2, $TEMP1, $TEMP1
	 add	24(%rsp), %rdx
	vpaddq		$TEMP1, $ACC8, $ACC7
	vpmuludq	$Y2, $TEMP2, $TEMP2
	vpaddq		$TEMP2, $ACC9, $ACC8
	 vmovq	$r3, $ACC9
	 mov	%rdx, $r3

	dec	$i
	jnz	.LOOP_REDUCE_1024
___
($ACC0,$Y2)=($Y2,$ACC0);
$code.=<<___;
	lea	448(%rsp), $tp1			# size optimization
	vpaddq	$ACC9, $Y2, $ACC0
	vpxor	$ZERO, $ZERO, $ZERO

	vpaddq		32*9-192($tp0), $ACC0, $ACC0
	vpaddq		32*10-448($tp1), $ACC1, $ACC1
	vpaddq		32*11-448($tp1), $ACC2, $ACC2
	vpaddq		32*12-448($tp1), $ACC3, $ACC3
	vpaddq		32*13-448($tp1), $ACC4, $ACC4
	vpaddq		32*14-448($tp1), $ACC5, $ACC5
	vpaddq		32*15-448($tp1), $ACC6, $ACC6
	vpaddq		32*16-448($tp1), $ACC7, $ACC7
	vpaddq		32*17-448($tp1), $ACC8, $ACC8

	vpsrlq		\$29, $ACC0, $TEMP1
	vpand		$AND_MASK, $ACC0, $ACC0
	vpsrlq		\$29, $ACC1, $TEMP2
	vpand		$AND_MASK, $ACC1, $ACC1
	vpsrlq		\$29, $ACC2, $TEMP3
	vpermq		\$0x93, $TEMP1, $TEMP1
	vpand		$AND_MASK, $ACC2, $ACC2
	vpsrlq		\$29, $ACC3, $TEMP4
	vpermq		\$0x93, $TEMP2, $TEMP2
	vpand		$AND_MASK, $ACC3, $ACC3
	vpermq		\$0x93, $TEMP3, $TEMP3

	vpblendd	\$3, $ZERO, $TEMP1, $TEMP0
	vpermq		\$0x93, $TEMP4, $TEMP4
	vpblendd	\$3, $TEMP1, $TEMP2, $TEMP1
	vpaddq		$TEMP0, $ACC0, $ACC0
	vpblendd	\$3, $TEMP2, $TEMP3, $TEMP2
	vpaddq		$TEMP1, $ACC1, $ACC1
	vpblendd	\$3, $TEMP3, $TEMP4, $TEMP3
	vpaddq		$TEMP2, $ACC2, $ACC2
	vpblendd	\$3, $TEMP4, $ZERO, $TEMP4
	vpaddq		$TEMP3, $ACC3, $ACC3
	vpaddq		$TEMP4, $ACC4, $ACC4

	vpsrlq		\$29, $ACC0, $TEMP1
	vpand		$AND_MASK, $ACC0, $ACC0
	vpsrlq		\$29, $ACC1, $TEMP2
	vpand		$AND_MASK, $ACC1, $ACC1
	vpsrlq		\$29, $ACC2, $TEMP3
	vpermq		\$0x93, $TEMP1, $TEMP1
	vpand		$AND_MASK, $ACC2, $ACC2
	vpsrlq		\$29, $ACC3, $TEMP4
	vpermq		\$0x93, $TEMP2, $TEMP2
	vpand		$AND_MASK, $ACC3, $ACC3
	vpermq		\$0x93, $TEMP3, $TEMP3

	vpblendd	\$3, $ZERO, $TEMP1, $TEMP0
	vpermq		\$0x93, $TEMP4, $TEMP4
	vpblendd	\$3, $TEMP1, $TEMP2, $TEMP1
	vpaddq		$TEMP0, $ACC0, $ACC0
	vpblendd	\$3, $TEMP2, $TEMP3, $TEMP2
	vpaddq		$TEMP1, $ACC1, $ACC1
	vmovdqu		$ACC0, 32*0-128($rp)
	vpblendd	\$3, $TEMP3, $TEMP4, $TEMP3
	vpaddq		$TEMP2, $ACC2, $ACC2
	vmovdqu		$ACC1, 32*1-128($rp)
	vpblendd	\$3, $TEMP4, $ZERO, $TEMP4
	vpaddq		$TEMP3, $ACC3, $ACC3
	vmovdqu		$ACC2, 32*2-128($rp)
	vpaddq		$TEMP4, $ACC4, $ACC4
	vmovdqu		$ACC3, 32*3-128($rp)
___
$TEMP5=$ACC0;
$code.=<<___;
	vpsrlq		\$29, $ACC4, $TEMP1
	vpand		$AND_MASK, $ACC4, $ACC4
	vpsrlq		\$29, $ACC5, $TEMP2
	vpand		$AND_MASK, $ACC5, $ACC5
	vpsrlq		\$29, $ACC6, $TEMP3
	vpermq		\$0x93, $TEMP1, $TEMP1
	vpand		$AND_MASK, $ACC6, $ACC6
	vpsrlq		\$29, $ACC7, $TEMP4
	vpermq		\$0x93, $TEMP2, $TEMP2
	vpand		$AND_MASK, $ACC7, $ACC7
	vpsrlq		\$29, $ACC8, $TEMP5
	vpermq		\$0x93, $TEMP3, $TEMP3
	vpand		$AND_MASK, $ACC8, $ACC8
	vpermq		\$0x93, $TEMP4, $TEMP4

	vpblendd	\$3, $ZERO, $TEMP1, $TEMP0
	vpermq		\$0x93, $TEMP5, $TEMP5
	vpblendd	\$3, $TEMP1, $TEMP2, $TEMP1
	vpaddq		$TEMP0, $ACC4, $ACC4
	vpblendd	\$3, $TEMP2, $TEMP3, $TEMP2
	vpaddq		$TEMP1, $ACC5, $ACC5
	vpblendd	\$3, $TEMP3, $TEMP4, $TEMP3
	vpaddq		$TEMP2, $ACC6, $ACC6
	vpblendd	\$3, $TEMP4, $TEMP5, $TEMP4
	vpaddq		$TEMP3, $ACC7, $ACC7
	vpaddq		$TEMP4, $ACC8, $ACC8

	vpsrlq		\$29, $ACC4, $TEMP1
	vpand		$AND_MASK, $ACC4, $ACC4
	vpsrlq		\$29, $ACC5, $TEMP2
	vpand		$AND_MASK, $ACC5, $ACC5
	vpsrlq		\$29, $ACC6, $TEMP3
	vpermq		\$0x93, $TEMP1, $TEMP1
	vpand		$AND_MASK, $ACC6, $ACC6
	vpsrlq		\$29, $ACC7, $TEMP4
	vpermq		\$0x93, $TEMP2, $TEMP2
	vpand		$AND_MASK, $ACC7, $ACC7
	vpsrlq		\$29, $ACC8, $TEMP5
	vpermq		\$0x93, $TEMP3, $TEMP3
	vpand		$AND_MASK, $ACC8, $ACC8
	vpermq		\$0x93, $TEMP4, $TEMP4

	vpblendd	\$3, $ZERO, $TEMP1, $TEMP0
	vpermq		\$0x93, $TEMP5, $TEMP5
	vpblendd	\$3, $TEMP1, $TEMP2, $TEMP1
	vpaddq		$TEMP0, $ACC4, $ACC4
	vpblendd	\$3, $TEMP2, $TEMP3, $TEMP2
	vpaddq		$TEMP1, $ACC5, $ACC5
	vmovdqu		$ACC4, 32*4-128($rp)
	vpblendd	\$3, $TEMP3, $TEMP4, $TEMP3
	vpaddq		$TEMP2, $ACC6, $ACC6
	vmovdqu		$ACC5, 32*5-128($rp)
	vpblendd	\$3, $TEMP4, $TEMP5, $TEMP4
	vpaddq		$TEMP3, $ACC7, $ACC7
	vmovdqu		$ACC6, 32*6-128($rp)
	vpaddq		$TEMP4, $ACC8, $ACC8
	vmovdqu		$ACC7, 32*7-128($rp)
	vmovdqu		$ACC8, 32*8-128($rp)

	mov	$rp, $ap
	dec	$rep
	jne	.LOOP_GRANDE_SQR_1024

	vzeroall
	mov	%rbp, %rax
.cfi_def_cfa_register	%rax
___
$code.=<<___ if ($win64);
.Lsqr_1024_in_tail:
	movaps	-0xd8(%rax),%xmm6
	movaps	-0xc8(%rax),%xmm7
	movaps	-0xb8(%rax),%xmm8
	movaps	-0xa8(%rax),%xmm9
	movaps	-0x98(%rax),%xmm10
	movaps	-0x88(%rax),%xmm11
	movaps	-0x78(%rax),%xmm12
	movaps	-0x68(%rax),%xmm13
	movaps	-0x58(%rax),%xmm14
	movaps	-0x48(%rax),%xmm15
___
$code.=<<___;
	mov	-48(%rax),%r15
.cfi_restore	%r15
	mov	-40(%rax),%r14
.cfi_restore	%r14
	mov	-32(%rax),%r13
.cfi_restore	%r13
	mov	-24(%rax),%r12
.cfi_restore	%r12
	mov	-16(%rax),%rbp
.cfi_restore	%rbp
	mov	-8(%rax),%rbx
.cfi_restore	%rbx
	lea	(%rax),%rsp		# restore %rsp
.cfi_def_cfa_register	%rsp
.Lsqr_1024_epilogue:
	ret
.cfi_endproc
.size	rsaz_1024_sqr_avx2,.-rsaz_1024_sqr_avx2
___
}

{ # void AMM_WW(
my $rp="%rdi";	# BN_ULONG *rp,
my $ap="%rsi";	# const BN_ULONG *ap,
my $bp="%rdx";	# const BN_ULONG *bp,
my $np="%rcx";	# const BN_ULONG *np,
my $n0="%r8d";	# unsigned int n0);

# The registers that hold the accumulated redundant result
# The AMM works on 1024 bit operands, and redundant word size is 29
# Therefore: ceil(1024/29)/4 = 9
my $ACC0="%ymm0";
my $ACC1="%ymm1";
my $ACC2="%ymm2";
my $ACC3="%ymm3";
my $ACC4="%ymm4";
my $ACC5="%ymm5";
my $ACC6="%ymm6";
my $ACC7="%ymm7";
my $ACC8="%ymm8";
my $ACC9="%ymm9";

# Registers that hold the broadcasted words of multiplier, currently used
my $Bi="%ymm10";
my $Yi="%ymm11";

# Helper registers
my $TEMP0=$ACC0;
my $TEMP1="%ymm12";
my $TEMP2="%ymm13";
my $ZERO="%ymm14";
my $AND_MASK="%ymm15";

# alu registers that hold the first words of the ACC
my $r0="%r9";
my $r1="%r10";
my $r2="%r11";
my $r3="%r12";

my $i="%r14d";
my $tmp="%r15";

$bp="%r13";	# reassigned argument

$code.=<<___;
.globl	rsaz_1024_mul_avx2
.type	rsaz_1024_mul_avx2,\@function,5
.align	64
rsaz_1024_mul_avx2:
.cfi_startproc
	lea	(%rsp), %rax
.cfi_def_cfa_register	%rax
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
___
$code.=<<___ if ($win64);
	vzeroupper
	lea	-0xa8(%rsp),%rsp
	vmovaps	%xmm6,-0xd8(%rax)
	vmovaps	%xmm7,-0xc8(%rax)
	vmovaps	%xmm8,-0xb8(%rax)
	vmovaps	%xmm9,-0xa8(%rax)
	vmovaps	%xmm10,-0x98(%rax)
	vmovaps	%xmm11,-0x88(%rax)
	vmovaps	%xmm12,-0x78(%rax)
	vmovaps	%xmm13,-0x68(%rax)
	vmovaps	%xmm14,-0x58(%rax)
	vmovaps	%xmm15,-0x48(%rax)
.Lmul_1024_body:
___
$code.=<<___;
	mov	%rax,%rbp
.cfi_def_cfa_register	%rbp
	vzeroall
	mov	%rdx, $bp	# reassigned argument
	sub	\$64,%rsp

	# unaligned 256-bit load that crosses page boundary can
	# cause severe performance degradation here, so if $ap does
	# cross page boundary, swap it with $bp [meaning that caller
	# is advised to lay down $ap and $bp next to each other, so
	# that only one can cross page boundary].
	.byte	0x67,0x67
	mov	$ap, $tmp
	and	\$4095, $tmp
	add	\$32*10, $tmp
	shr	\$12, $tmp
	mov	$ap, $tmp
	cmovnz	$bp, $ap
	cmovnz	$tmp, $bp

	mov	$np, $tmp
	sub	\$-128,$ap	# size optimization
	sub	\$-128,$np
	sub	\$-128,$rp

	and	\$4095, $tmp	# see if $np crosses page
	add	\$32*10, $tmp
	.byte	0x67,0x67
	shr	\$12, $tmp
	jz	.Lmul_1024_no_n_copy

	# unaligned 256-bit load that crosses page boundary can
	# cause severe performance degradation here, so if $np does
	# cross page boundary, copy it to stack and make sure stack
	# frame doesn't...
	sub		\$32*10,%rsp
	vmovdqu		32*0-128($np), $ACC0
	and		\$-512, %rsp
	vmovdqu		32*1-128($np), $ACC1
	vmovdqu		32*2-128($np), $ACC2
	vmovdqu		32*3-128($np), $ACC3
	vmovdqu		32*4-128($np), $ACC4
	vmovdqu		32*5-128($np), $ACC5
	vmovdqu		32*6-128($np), $ACC6
	vmovdqu		32*7-128($np), $ACC7
	vmovdqu		32*8-128($np), $ACC8
	lea		64+128(%rsp),$np
	vmovdqu		$ACC0, 32*0-128($np)
	vpxor		$ACC0, $ACC0, $ACC0
	vmovdqu		$ACC1, 32*1-128($np)
	vpxor		$ACC1, $ACC1, $ACC1
	vmovdqu		$ACC2, 32*2-128($np)
	vpxor		$ACC2, $ACC2, $ACC2
	vmovdqu		$ACC3, 32*3-128($np)
	vpxor		$ACC3, $ACC3, $ACC3
	vmovdqu		$ACC4, 32*4-128($np)
	vpxor		$ACC4, $ACC4, $ACC4
	vmovdqu		$ACC5, 32*5-128($np)
	vpxor		$ACC5, $ACC5, $ACC5
	vmovdqu		$ACC6, 32*6-128($np)
	vpxor		$ACC6, $ACC6, $ACC6
	vmovdqu		$ACC7, 32*7-128($np)
	vpxor		$ACC7, $ACC7, $ACC7
	vmovdqu		$ACC8, 32*8-128($np)
	vmovdqa		$ACC0, $ACC8
	vmovdqu		$ACC9, 32*9-128($np)	# $ACC9 is zero after vzeroall
.Lmul_1024_no_n_copy:
	and	\$-64,%rsp

	mov	($bp), %rbx
	vpbroadcastq ($bp), $Bi
	vmovdqu	$ACC0, (%rsp)			# clear top of stack
	xor	$r0, $r0
	.byte	0x67
	xor	$r1, $r1
	xor	$r2, $r2
	xor	$r3, $r3

	vmovdqu	.Land_mask(%rip), $AND_MASK
	mov	\$9, $i
	vmovdqu	$ACC9, 32*9-128($rp)		# $ACC9 is zero after vzeroall
	jmp	.Loop_mul_1024

.align	32
.Loop_mul_1024:
	 vpsrlq		\$29, $ACC3, $ACC9		# correct $ACC3(*)
	mov	%rbx, %rax
	imulq	-128($ap), %rax
	add	$r0, %rax
	mov	%rbx, $r1
	imulq	8-128($ap), $r1
	add	8(%rsp), $r1

	mov	%rax, $r0
	imull	$n0, %eax
	and	\$0x1fffffff, %eax

	 mov	%rbx, $r2
	 imulq	16-128($ap), $r2
	 add	16(%rsp), $r2

	 mov	%rbx, $r3
	 imulq	24-128($ap), $r3
	 add	24(%rsp), $r3
	vpmuludq	32*1-128($ap),$Bi,$TEMP0
	 vmovd		%eax, $Yi
	vpaddq		$TEMP0,$ACC1,$ACC1
	vpmuludq	32*2-128($ap),$Bi,$TEMP1
	 vpbroadcastq	$Yi, $Yi
	vpaddq		$TEMP1,$ACC2,$ACC2
	vpmuludq	32*3-128($ap),$Bi,$TEMP2
	 vpand		$AND_MASK, $ACC3, $ACC3		# correct $ACC3
	vpaddq		$TEMP2,$ACC3,$ACC3
	vpmuludq	32*4-128($ap),$Bi,$TEMP0
	vpaddq		$TEMP0,$ACC4,$ACC4
	vpmuludq	32*5-128($ap),$Bi,$TEMP1
	vpaddq		$TEMP1,$ACC5,$ACC5
	vpmuludq	32*6-128($ap),$Bi,$TEMP2
	vpaddq		$TEMP2,$ACC6,$ACC6
	vpmuludq	32*7-128($ap),$Bi,$TEMP0
	 vpermq		\$0x93, $ACC9, $ACC9		# correct $ACC3
	vpaddq		$TEMP0,$ACC7,$ACC7
	vpmuludq	32*8-128($ap),$Bi,$TEMP1
	 vpbroadcastq	8($bp), $Bi
	vpaddq		$TEMP1,$ACC8,$ACC8

	mov	%rax,%rdx
	imulq	-128($np),%rax
	add	%rax,$r0
	mov	%rdx,%rax
	imulq	8-128($np),%rax
	add	%rax,$r1
	mov	%rdx,%rax
	imulq	16-128($np),%rax
	add	%rax,$r2
	shr	\$29, $r0
	imulq	24-128($np),%rdx
	add	%rdx,$r3
	add	$r0, $r1

	vpmuludq	32*1-128($np),$Yi,$TEMP2
	 vmovq		$Bi, %rbx
	vpaddq		$TEMP2,$ACC1,$ACC1
	vpmuludq	32*2-128($np),$Yi,$TEMP0
	vpaddq		$TEMP0,$ACC2,$ACC2
	vpmuludq	32*3-128($np),$Yi,$TEMP1
	vpaddq		$TEMP1,$ACC3,$ACC3
	vpmuludq	32*4-128($np),$Yi,$TEMP2
	vpaddq		$TEMP2,$ACC4,$ACC4
	vpmuludq	32*5-128($np),$Yi,$TEMP0
	vpaddq		$TEMP0,$ACC5,$ACC5
	vpmuludq	32*6-128($np),$Yi,$TEMP1
	vpaddq		$TEMP1,$ACC6,$ACC6
	vpmuludq	32*7-128($np),$Yi,$TEMP2
	 vpblendd	\$3, $ZERO, $ACC9, $TEMP1	# correct $ACC3
	vpaddq		$TEMP2,$ACC7,$ACC7
	vpmuludq	32*8-128($np),$Yi,$TEMP0
	 vpaddq		$TEMP1, $ACC3, $ACC3		# correct $ACC3
	vpaddq		$TEMP0,$ACC8,$ACC8

	mov	%rbx, %rax
	imulq	-128($ap),%rax
	add	%rax,$r1
	 vmovdqu	-8+32*1-128($ap),$TEMP1
	mov	%rbx, %rax
	imulq	8-128($ap),%rax
	add	%rax,$r2
	 vmovdqu	-8+32*2-128($ap),$TEMP2

	mov	$r1, %rax
	 vpblendd	\$0xfc, $ZERO, $ACC9, $ACC9	# correct $ACC3
	imull	$n0, %eax
	 vpaddq		$ACC9,$ACC4,$ACC4		# correct $ACC3
	and	\$0x1fffffff, %eax

	 imulq	16-128($ap),%rbx
	 add	%rbx,$r3
	vpmuludq	$Bi,$TEMP1,$TEMP1
	 vmovd		%eax, $Yi
	vmovdqu		-8+32*3-128($ap),$TEMP0
	vpaddq		$TEMP1,$ACC1,$ACC1
	vpmuludq	$Bi,$TEMP2,$TEMP2
	 vpbroadcastq	$Yi, $Yi
	vmovdqu		-8+32*4-128($ap),$TEMP1
	vpaddq		$TEMP2,$ACC2,$ACC2
	vpmuludq	$Bi,$TEMP0,$TEMP0
	vmovdqu		-8+32*5-128($ap),$TEMP2
	vpaddq		$TEMP0,$ACC3,$ACC3
	vpmuludq	$Bi,$TEMP1,$TEMP1
	vmovdqu		-8+32*6-128($ap),$TEMP0
	vpaddq		$TEMP1,$ACC4,$ACC4
	vpmuludq	$Bi,$TEMP2,$TEMP2
	vmovdqu		-8+32*7-128($ap),$TEMP1
	vpaddq		$TEMP2,$ACC5,$ACC5
	vpmuludq	$Bi,$TEMP0,$TEMP0
	vmovdqu		-8+32*8-128($ap),$TEMP2
	vpaddq		$TEMP0,$ACC6,$ACC6
	vpmuludq	$Bi,$TEMP1,$TEMP1
	vmovdqu		-8+32*9-128($ap),$ACC9
	vpaddq		$TEMP1,$ACC7,$ACC7
	vpmuludq	$Bi,$TEMP2,$TEMP2
	vpaddq		$TEMP2,$ACC8,$ACC8
	vpmuludq	$Bi,$ACC9,$ACC9
	 vpbroadcastq	16($bp), $Bi

	mov	%rax,%rdx
	imulq	-128($np),%rax
	add	%rax,$r1
	 vmovdqu	-8+32*1-128($np),$TEMP0
	mov	%rdx,%rax
	imulq	8-128($np),%rax
	add	%rax,$r2
	 vmovdqu	-8+32*2-128($np),$TEMP1
	shr	\$29, $r1
	imulq	16-128($np),%rdx
	add	%rdx,$r3
	add	$r1, $r2

	vpmuludq	$Yi,$TEMP0,$TEMP0
	 vmovq		$Bi, %rbx
	vmovdqu		-8+32*3-128($np),$TEMP2
	vpaddq		$TEMP0,$ACC1,$ACC1
	vpmuludq	$Yi,$TEMP1,$TEMP1
	vmovdqu		-8+32*4-128($np),$TEMP0
	vpaddq		$TEMP1,$ACC2,$ACC2
	vpmuludq	$Yi,$TEMP2,$TEMP2
	vmovdqu		-8+32*5-128($np),$TEMP1
	vpaddq		$TEMP2,$ACC3,$ACC3
	vpmuludq	$Yi,$TEMP0,$TEMP0
	vmovdqu		-8+32*6-128($np),$TEMP2
	vpaddq		$TEMP0,$ACC4,$ACC4
	vpmuludq	$Yi,$TEMP1,$TEMP1
	vmovdqu		-8+32*7-128($np),$TEMP0
	vpaddq		$TEMP1,$ACC5,$ACC5
	vpmuludq	$Yi,$TEMP2,$TEMP2
	vmovdqu		-8+32*8-128($np),$TEMP1
	vpaddq		$TEMP2,$ACC6,$ACC6
	vpmuludq	$Yi,$TEMP0,$TEMP0
	vmovdqu		-8+32*9-128($np),$TEMP2
	vpaddq		$TEMP0,$ACC7,$ACC7
	vpmuludq	$Yi,$TEMP1,$TEMP1
	vpaddq		$TEMP1,$ACC8,$ACC8
	vpmuludq	$Yi,$TEMP2,$TEMP2
	vpaddq		$TEMP2,$ACC9,$ACC9

	 vmovdqu	-16+32*1-128($ap),$TEMP0
	mov	%rbx,%rax
	imulq	-128($ap),%rax
	add	$r2,%rax

	 vmovdqu	-16+32*2-128($ap),$TEMP1
	mov	%rax,$r2
	imull	$n0, %eax
	and	\$0x1fffffff, %eax

	 imulq	8-128($ap),%rbx
	 add	%rbx,$r3
	vpmuludq	$Bi,$TEMP0,$TEMP0
	 vmovd		%eax, $Yi
	vmovdqu		-16+32*3-128($ap),$TEMP2
	vpaddq		$TEMP0,$ACC1,$ACC1
	vpmuludq	$Bi,$TEMP1,$TEMP1
	 vpbroadcastq	$Yi, $Yi
	vmovdqu		-16+32*4-128($ap),$TEMP0
	vpaddq		$TEMP1,$ACC2,$ACC2
	vpmuludq	$Bi,$TEMP2,$TEMP2
	vmovdqu		-16+32*5-128($ap),$TEMP1
	vpaddq		$TEMP2,$ACC3,$ACC3
	vpmuludq	$Bi,$TEMP0,$TEMP0
	vmovdqu		-16+32*6-128($ap),$TEMP2
	vpaddq		$TEMP0,$ACC4,$ACC4
	vpmuludq	$Bi,$TEMP1,$TEMP1
	vmovdqu		-16+32*7-128($ap),$TEMP0
	vpaddq		$TEMP1,$ACC5,$ACC5
	vpmuludq	$Bi,$TEMP2,$TEMP2
	vmovdqu		-16+32*8-128($ap),$TEMP1
	vpaddq		$TEMP2,$ACC6,$ACC6
	vpmuludq	$Bi,$TEMP0,$TEMP0
	vmovdqu		-16+32*9-128($ap),$TEMP2
	vpaddq		$TEMP0,$ACC7,$ACC7
	vpmuludq	$Bi,$TEMP1,$TEMP1
	vpaddq		$TEMP1,$ACC8,$ACC8
	vpmuludq	$Bi,$TEMP2,$TEMP2
	 vpbroadcastq	24($bp), $Bi
	vpaddq		$TEMP2,$ACC9,$ACC9

	 vmovdqu	-16+32*1-128($np),$TEMP0
	mov	%rax,%rdx
	imulq	-128($np),%rax
	add	%rax,$r2
	 vmovdqu	-16+32*2-128($np),$TEMP1
	imulq	8-128($np),%rdx
	add	%rdx,$r3
	shr	\$29, $r2

	vpmuludq	$Yi,$TEMP0,$TEMP0
	 vmovq		$Bi, %rbx
	vmovdqu		-16+32*3-128($np),$TEMP2
	vpaddq		$TEMP0,$ACC1,$ACC1
	vpmuludq	$Yi,$TEMP1,$TEMP1
	vmovdqu		-16+32*4-128($np),$TEMP0
	vpaddq		$TEMP1,$ACC2,$ACC2
	vpmuludq	$Yi,$TEMP2,$TEMP2
	vmovdqu		-16+32*5-128($np),$TEMP1
	vpaddq		$TEMP2,$ACC3,$ACC3
	vpmuludq	$Yi,$TEMP0,$TEMP0
	vmovdqu		-16+32*6-128($np),$TEMP2
	vpaddq		$TEMP0,$ACC4,$ACC4
	vpmuludq	$Yi,$TEMP1,$TEMP1
	vmovdqu		-16+32*7-128($np),$TEMP0
	vpaddq		$TEMP1,$ACC5,$ACC5
	vpmuludq	$Yi,$TEMP2,$TEMP2
	vmovdqu		-16+32*8-128($np),$TEMP1
	vpaddq		$TEMP2,$ACC6,$ACC6
	vpmuludq	$Yi,$TEMP0,$TEMP0
	vmovdqu		-16+32*9-128($np),$TEMP2
	vpaddq		$TEMP0,$ACC7,$ACC7
	vpmuludq	$Yi,$TEMP1,$TEMP1
	 vmovdqu	-24+32*1-128($ap),$TEMP0
	vpaddq		$TEMP1,$ACC8,$ACC8
	vpmuludq	$Yi,$TEMP2,$TEMP2
	 vmovdqu	-24+32*2-128($ap),$TEMP1
	vpaddq		$TEMP2,$ACC9,$ACC9

	add	$r2, $r3
	imulq	-128($ap),%rbx
	add	%rbx,$r3

	mov	$r3, %rax
	imull	$n0, %eax
	and	\$0x1fffffff, %eax

	vpmuludq	$Bi,$TEMP0,$TEMP0
	 vmovd		%eax, $Yi
	vmovdqu		-24+32*3-128($ap),$TEMP2
	vpaddq		$TEMP0,$ACC1,$ACC1
	vpmuludq	$Bi,$TEMP1,$TEMP1
	 vpbroadcastq	$Yi, $Yi
	vmovdqu		-24+32*4-128($ap),$TEMP0
	vpaddq		$TEMP1,$ACC2,$ACC2
	vpmuludq	$Bi,$TEMP2,$TEMP2
	vmovdqu		-24+32*5-128($ap),$TEMP1
	vpaddq		$TEMP2,$ACC3,$ACC3
	vpmuludq	$Bi,$TEMP0,$TEMP0
	vmovdqu		-24+32*6-128($ap),$TEMP2
	vpaddq		$TEMP0,$ACC4,$ACC4
	vpmuludq	$Bi,$TEMP1,$TEMP1
	vmovdqu		-24+32*7-128($ap),$TEMP0
	vpaddq		$TEMP1,$ACC5,$ACC5
	vpmuludq	$Bi,$TEMP2,$TEMP2
	vmovdqu		-24+32*8-128($ap),$TEMP1
	vpaddq		$TEMP2,$ACC6,$ACC6
	vpmuludq	$Bi,$TEMP0,$TEMP0
	vmovdqu		-24+32*9-128($ap),$TEMP2
	vpaddq		$TEMP0,$ACC7,$ACC7
	vpmuludq	$Bi,$TEMP1,$TEMP1
	vpaddq		$TEMP1,$ACC8,$ACC8
	vpmuludq	$Bi,$TEMP2,$TEMP2
	 vpbroadcastq	32($bp), $Bi
	vpaddq		$TEMP2,$ACC9,$ACC9
	 add		\$32, $bp			# $bp++

	vmovdqu		-24+32*1-128($np),$TEMP0
	imulq	-128($np),%rax
	add	%rax,$r3
	shr	\$29, $r3

	vmovdqu		-24+32*2-128($np),$TEMP1
	vpmuludq	$Yi,$TEMP0,$TEMP0
	 vmovq		$Bi, %rbx
	vmovdqu		-24+32*3-128($np),$TEMP2
	vpaddq		$TEMP0,$ACC1,$ACC0		# $ACC0==$TEMP0
	vpmuludq	$Yi,$TEMP1,$TEMP1
	 vmovdqu	$ACC0, (%rsp)			# transfer $r0-$r3
	vpaddq		$TEMP1,$ACC2,$ACC1
	vmovdqu		-24+32*4-128($np),$TEMP0
	vpmuludq	$Yi,$TEMP2,$TEMP2
	vmovdqu		-24+32*5-128($np),$TEMP1
	vpaddq		$TEMP2,$ACC3,$ACC2
	vpmuludq	$Yi,$TEMP0,$TEMP0
	vmovdqu		-24+32*6-128($np),$TEMP2
	vpaddq		$TEMP0,$ACC4,$ACC3
	vpmuludq	$Yi,$TEMP1,$TEMP1
	vmovdqu		-24+32*7-128($np),$TEMP0
	vpaddq		$TEMP1,$ACC5,$ACC4
	vpmuludq	$Yi,$TEMP2,$TEMP2
	vmovdqu		-24+32*8-128($np),$TEMP1
	vpaddq		$TEMP2,$ACC6,$ACC5
	vpmuludq	$Yi,$TEMP0,$TEMP0
	vmovdqu		-24+32*9-128($np),$TEMP2
	 mov	$r3, $r0
	vpaddq		$TEMP0,$ACC7,$ACC6
	vpmuludq	$Yi,$TEMP1,$TEMP1
	 add	(%rsp), $r0
	vpaddq		$TEMP1,$ACC8,$ACC7
	vpmuludq	$Yi,$TEMP2,$TEMP2
	 vmovq	$r3, $TEMP1
	vpaddq		$TEMP2,$ACC9,$ACC8

	dec	$i
	jnz	.Loop_mul_1024
___

# (*)	Original implementation was correcting ACC1-ACC3 for overflow
#	after 7 loop runs, or after 28 iterations, or 56 additions.
#	But as we underutilize resources, it's possible to correct in
#	each iteration with marginal performance loss. But then, as
#	we do it in each iteration, we can correct less digits, and
#	avoid performance penalties completely.

$TEMP0 = $ACC9;
$TEMP3 = $Bi;
$TEMP4 = $Yi;
$code.=<<___;
	vpaddq		(%rsp), $TEMP1, $ACC0

	vpsrlq		\$29, $ACC0, $TEMP1
	vpand		$AND_MASK, $ACC0, $ACC0
	vpsrlq		\$29, $ACC1, $TEMP2
	vpand		$AND_MASK, $ACC1, $ACC1
	vpsrlq		\$29, $ACC2, $TEMP3
	vpermq		\$0x93, $TEMP1, $TEMP1
	vpand		$AND_MASK, $ACC2, $ACC2
	vpsrlq		\$29, $ACC3, $TEMP4
	vpermq		\$0x93, $TEMP2, $TEMP2
	vpand		$AND_MASK, $ACC3, $ACC3

	vpblendd	\$3, $ZERO, $TEMP1, $TEMP0
	vpermq		\$0x93, $TEMP3, $TEMP3
	vpblendd	\$3, $TEMP1, $TEMP2, $TEMP1
	vpermq		\$0x93, $TEMP4, $TEMP4
	vpaddq		$TEMP0, $ACC0, $ACC0
	vpblendd	\$3, $TEMP2, $TEMP3, $TEMP2
	vpaddq		$TEMP1, $ACC1, $ACC1
	vpblendd	\$3, $TEMP3, $TEMP4, $TEMP3
	vpaddq		$TEMP2, $ACC2, $ACC2
	vpblendd	\$3, $TEMP4, $ZERO, $TEMP4
	vpaddq		$TEMP3, $ACC3, $ACC3
	vpaddq		$TEMP4, $ACC4, $ACC4

	vpsrlq		\$29, $ACC0, $TEMP1
	vpand		$AND_MASK, $ACC0, $ACC0
	vpsrlq		\$29, $ACC1, $TEMP2
	vpand		$AND_MASK, $ACC1, $ACC1
	vpsrlq		\$29, $ACC2, $TEMP3
	vpermq		\$0x93, $TEMP1, $TEMP1
	vpand		$AND_MASK, $ACC2, $ACC2
	vpsrlq		\$29, $ACC3, $TEMP4
	vpermq		\$0x93, $TEMP2, $TEMP2
	vpand		$AND_MASK, $ACC3, $ACC3
	vpermq		\$0x93, $TEMP3, $TEMP3

	vpblendd	\$3, $ZERO, $TEMP1, $TEMP0
	vpermq		\$0x93, $TEMP4, $TEMP4
	vpblendd	\$3, $TEMP1, $TEMP2, $TEMP1
	vpaddq		$TEMP0, $ACC0, $ACC0
	vpblendd	\$3, $TEMP2, $TEMP3, $TEMP2
	vpaddq		$TEMP1, $ACC1, $ACC1
	vpblendd	\$3, $TEMP3, $TEMP4, $TEMP3
	vpaddq		$TEMP2, $ACC2, $ACC2
	vpblendd	\$3, $TEMP4, $ZERO, $TEMP4
	vpaddq		$TEMP3, $ACC3, $ACC3
	vpaddq		$TEMP4, $ACC4, $ACC4

	vmovdqu		$ACC0, 0-128($rp)
	vmovdqu		$ACC1, 32-128($rp)
	vmovdqu		$ACC2, 64-128($rp)
	vmovdqu		$ACC3, 96-128($rp)
___

$TEMP5=$ACC0;
$code.=<<___;
	vpsrlq		\$29, $ACC4, $TEMP1
	vpand		$AND_MASK, $ACC4, $ACC4
	vpsrlq		\$29, $ACC5, $TEMP2
	vpand		$AND_MASK, $ACC5, $ACC5
	vpsrlq		\$29, $ACC6, $TEMP3
	vpermq		\$0x93, $TEMP1, $TEMP1
	vpand		$AND_MASK, $ACC6, $ACC6
	vpsrlq		\$29, $ACC7, $TEMP4
	vpermq		\$0x93, $TEMP2, $TEMP2
	vpand		$AND_MASK, $ACC7, $ACC7
	vpsrlq		\$29, $ACC8, $TEMP5
	vpermq		\$0x93, $TEMP3, $TEMP3
	vpand		$AND_MASK, $ACC8, $ACC8
	vpermq		\$0x93, $TEMP4, $TEMP4

	vpblendd	\$3, $ZERO, $TEMP1, $TEMP0
	vpermq		\$0x93, $TEMP5, $TEMP5
	vpblendd	\$3, $TEMP1, $TEMP2, $TEMP1
	vpaddq		$TEMP0, $ACC4, $ACC4
	vpblendd	\$3, $TEMP2, $TEMP3, $TEMP2
	vpaddq		$TEMP1, $ACC5, $ACC5
	vpblendd	\$3, $TEMP3, $TEMP4, $TEMP3
	vpaddq		$TEMP2, $ACC6, $ACC6
	vpblendd	\$3, $TEMP4, $TEMP5, $TEMP4
	vpaddq		$TEMP3, $ACC7, $ACC7
	vpaddq		$TEMP4, $ACC8, $ACC8

	vpsrlq		\$29, $ACC4, $TEMP1
	vpand		$AND_MASK, $ACC4, $ACC4
	vpsrlq		\$29, $ACC5, $TEMP2
	vpand		$AND_MASK, $ACC5, $ACC5
	vpsrlq		\$29, $ACC6, $TEMP3
	vpermq		\$0x93, $TEMP1, $TEMP1
	vpand		$AND_MASK, $ACC6, $ACC6
	vpsrlq		\$29, $ACC7, $TEMP4
	vpermq		\$0x93, $TEMP2, $TEMP2
	vpand		$AND_MASK, $ACC7, $ACC7
	vpsrlq		\$29, $ACC8, $TEMP5
	vpermq		\$0x93, $TEMP3, $TEMP3
	vpand		$AND_MASK, $ACC8, $ACC8
	vpermq		\$0x93, $TEMP4, $TEMP4

	vpblendd	\$3, $ZERO, $TEMP1, $TEMP0
	vpermq		\$0x93, $TEMP5, $TEMP5
	vpblendd	\$3, $TEMP1, $TEMP2, $TEMP1
	vpaddq		$TEMP0, $ACC4, $ACC4
	vpblendd	\$3, $TEMP2, $TEMP3, $TEMP2
	vpaddq		$TEMP1, $ACC5, $ACC5
	vpblendd	\$3, $TEMP3, $TEMP4, $TEMP3
	vpaddq		$TEMP2, $ACC6, $ACC6
	vpblendd	\$3, $TEMP4, $TEMP5, $TEMP4
	vpaddq		$TEMP3, $ACC7, $ACC7
	vpaddq		$TEMP4, $ACC8, $ACC8

	vmovdqu		$ACC4, 128-128($rp)
	vmovdqu		$ACC5, 160-128($rp)
	vmovdqu		$ACC6, 192-128($rp)
	vmovdqu		$ACC7, 224-128($rp)
	vmovdqu		$ACC8, 256-128($rp)
	vzeroupper

	mov	%rbp, %rax
.cfi_def_cfa_register	%rax
___
$code.=<<___ if ($win64);
.Lmul_1024_in_tail:
	movaps	-0xd8(%rax),%xmm6
	movaps	-0xc8(%rax),%xmm7
	movaps	-0xb8(%rax),%xmm8
	movaps	-0xa8(%rax),%xmm9
	movaps	-0x98(%rax),%xmm10
	movaps	-0x88(%rax),%xmm11
	movaps	-0x78(%rax),%xmm12
	movaps	-0x68(%rax),%xmm13
	movaps	-0x58(%rax),%xmm14
	movaps	-0x48(%rax),%xmm15
___
$code.=<<___;
	mov	-48(%rax),%r15
.cfi_restore	%r15
	mov	-40(%rax),%r14
.cfi_restore	%r14
	mov	-32(%rax),%r13
.cfi_restore	%r13
	mov	-24(%rax),%r12
.cfi_restore	%r12
	mov	-16(%rax),%rbp
.cfi_restore	%rbp
	mov	-8(%rax),%rbx
.cfi_restore	%rbx
	lea	(%rax),%rsp		# restore %rsp
.cfi_def_cfa_register	%rsp
.Lmul_1024_epilogue:
	ret
.cfi_endproc
.size	rsaz_1024_mul_avx2,.-rsaz_1024_mul_avx2
___
}
{
my ($out,$inp) = $win64 ? ("%rcx","%rdx") : ("%rdi","%rsi");
my @T = map("%r$_",(8..11));

$code.=<<___;
.globl	rsaz_1024_red2norm_avx2
.type	rsaz_1024_red2norm_avx2,\@abi-omnipotent
.align	32
rsaz_1024_red2norm_avx2:
.cfi_startproc
	sub	\$-128,$inp	# size optimization
	xor	%rax,%rax
___

for ($j=0,$i=0; $i<16; $i++) {
    my $k=0;
    while (29*$j<64*($i+1)) {	# load data till boundary
	$code.="	mov	`8*$j-128`($inp), @T[0]\n";
	$j++; $k++; push(@T,shift(@T));
    }
    $l=$k;
    while ($k>1) {		# shift loaded data but last value
	$code.="	shl	\$`29*($j-$k)`,@T[-$k]\n";
	$k--;
    }
    $code.=<<___;		# shift last value
	mov	@T[-1], @T[0]
	shl	\$`29*($j-1)`, @T[-1]
	shr	\$`-29*($j-1)`, @T[0]
___
    while ($l) {		# accumulate all values
	$code.="	add	@T[-$l], %rax\n";
	$l--;
    }
	$code.=<<___;
	adc	\$0, @T[0]	# consume eventual carry
	mov	%rax, 8*$i($out)
	mov	@T[0], %rax
___
    push(@T,shift(@T));
}
$code.=<<___;
	ret
.cfi_endproc
.size	rsaz_1024_red2norm_avx2,.-rsaz_1024_red2norm_avx2

.globl	rsaz_1024_norm2red_avx2
.type	rsaz_1024_norm2red_avx2,\@abi-omnipotent
.align	32
rsaz_1024_norm2red_avx2:
.cfi_startproc
	sub	\$-128,$out	# size optimization
	mov	($inp),@T[0]
	mov	\$0x1fffffff,%eax
___
for ($j=0,$i=0; $i<16; $i++) {
    $code.="	mov	`8*($i+1)`($inp),@T[1]\n"	if ($i<15);
    $code.="	xor	@T[1],@T[1]\n"			if ($i==15);
    my $k=1;
    while (29*($j+1)<64*($i+1)) {
    	$code.=<<___;
	mov	@T[0],@T[-$k]
	shr	\$`29*$j`,@T[-$k]
	and	%rax,@T[-$k]				# &0x1fffffff
	mov	@T[-$k],`8*$j-128`($out)
___
	$j++; $k++;
    }
    $code.=<<___;
	shrd	\$`29*$j`,@T[1],@T[0]
	and	%rax,@T[0]
	mov	@T[0],`8*$j-128`($out)
___
    $j++;
    push(@T,shift(@T));
}
$code.=<<___;
	mov	@T[0],`8*$j-128`($out)			# zero
	mov	@T[0],`8*($j+1)-128`($out)
	mov	@T[0],`8*($j+2)-128`($out)
	mov	@T[0],`8*($j+3)-128`($out)
	ret
.cfi_endproc
.size	rsaz_1024_norm2red_avx2,.-rsaz_1024_norm2red_avx2
___
}
{
my ($out,$inp,$power) = $win64 ? ("%rcx","%rdx","%r8d") : ("%rdi","%rsi","%edx");

$code.=<<___;
.globl	rsaz_1024_scatter5_avx2
.type	rsaz_1024_scatter5_avx2,\@abi-omnipotent
.align	32
rsaz_1024_scatter5_avx2:
.cfi_startproc
	vzeroupper
	vmovdqu	.Lscatter_permd(%rip),%ymm5
	shl	\$4,$power
	lea	($out,$power),$out
	mov	\$9,%eax
	jmp	.Loop_scatter_1024

.align	32
.Loop_scatter_1024:
	vmovdqu		($inp),%ymm0
	lea		32($inp),$inp
	vpermd		%ymm0,%ymm5,%ymm0
	vmovdqu		%xmm0,($out)
	lea		16*32($out),$out
	dec	%eax
	jnz	.Loop_scatter_1024

	vzeroupper
	ret
.cfi_endproc
.size	rsaz_1024_scatter5_avx2,.-rsaz_1024_scatter5_avx2

.globl	rsaz_1024_gather5_avx2
.type	rsaz_1024_gather5_avx2,\@abi-omnipotent
.align	32
rsaz_1024_gather5_avx2:
.cfi_startproc
	vzeroupper
	mov	%rsp,%r11
.cfi_def_cfa_register	%r11
___
$code.=<<___ if ($win64);
	lea	-0x88(%rsp),%rax
.LSEH_begin_rsaz_1024_gather5:
	# I can't trust assembler to use specific encoding:-(
	.byte	0x48,0x8d,0x60,0xe0		# lea	-0x20(%rax),%rsp
	.byte	0xc5,0xf8,0x29,0x70,0xe0	# vmovaps %xmm6,-0x20(%rax)
	.byte	0xc5,0xf8,0x29,0x78,0xf0	# vmovaps %xmm7,-0x10(%rax)
	.byte	0xc5,0x78,0x29,0x40,0x00	# vmovaps %xmm8,0(%rax)
	.byte	0xc5,0x78,0x29,0x48,0x10	# vmovaps %xmm9,0x10(%rax)
	.byte	0xc5,0x78,0x29,0x50,0x20	# vmovaps %xmm10,0x20(%rax)
	.byte	0xc5,0x78,0x29,0x58,0x30	# vmovaps %xmm11,0x30(%rax)
	.byte	0xc5,0x78,0x29,0x60,0x40	# vmovaps %xmm12,0x40(%rax)
	.byte	0xc5,0x78,0x29,0x68,0x50	# vmovaps %xmm13,0x50(%rax)
	.byte	0xc5,0x78,0x29,0x70,0x60	# vmovaps %xmm14,0x60(%rax)
	.byte	0xc5,0x78,0x29,0x78,0x70	# vmovaps %xmm15,0x70(%rax)
___
$code.=<<___;
	lea	-0x100(%rsp),%rsp
	and	\$-32, %rsp
	lea	.Linc(%rip), %r10
	lea	-128(%rsp),%rax			# control u-op density

	vmovd		$power, %xmm4
	vmovdqa		(%r10),%ymm0
	vmovdqa		32(%r10),%ymm1
	vmovdqa		64(%r10),%ymm5
	vpbroadcastd	%xmm4,%ymm4

	vpaddd		%ymm5, %ymm0, %ymm2
	vpcmpeqd	%ymm4, %ymm0, %ymm0
	vpaddd		%ymm5, %ymm1, %ymm3
	vpcmpeqd	%ymm4, %ymm1, %ymm1
	vmovdqa		%ymm0, 32*0+128(%rax)
	vpaddd		%ymm5, %ymm2, %ymm0
	vpcmpeqd	%ymm4, %ymm2, %ymm2
	vmovdqa		%ymm1, 32*1+128(%rax)
	vpaddd		%ymm5, %ymm3, %ymm1
	vpcmpeqd	%ymm4, %ymm3, %ymm3
	vmovdqa		%ymm2, 32*2+128(%rax)
	vpaddd		%ymm5, %ymm0, %ymm2
	vpcmpeqd	%ymm4, %ymm0, %ymm0
	vmovdqa		%ymm3, 32*3+128(%rax)
	vpaddd		%ymm5, %ymm1, %ymm3
	vpcmpeqd	%ymm4, %ymm1, %ymm1
	vmovdqa		%ymm0, 32*4+128(%rax)
	vpaddd		%ymm5, %ymm2, %ymm8
	vpcmpeqd	%ymm4, %ymm2, %ymm2
	vmovdqa		%ymm1, 32*5+128(%rax)
	vpaddd		%ymm5, %ymm3, %ymm9
	vpcmpeqd	%ymm4, %ymm3, %ymm3
	vmovdqa		%ymm2, 32*6+128(%rax)
	vpaddd		%ymm5, %ymm8, %ymm10
	vpcmpeqd	%ymm4, %ymm8, %ymm8
	vmovdqa		%ymm3, 32*7+128(%rax)
	vpaddd		%ymm5, %ymm9, %ymm11
	vpcmpeqd	%ymm4, %ymm9, %ymm9
	vpaddd		%ymm5, %ymm10, %ymm12
	vpcmpeqd	%ymm4, %ymm10, %ymm10
	vpaddd		%ymm5, %ymm11, %ymm13
	vpcmpeqd	%ymm4, %ymm11, %ymm11
	vpaddd		%ymm5, %ymm12, %ymm14
	vpcmpeqd	%ymm4, %ymm12, %ymm12
	vpaddd		%ymm5, %ymm13, %ymm15
	vpcmpeqd	%ymm4, %ymm13, %ymm13
	vpcmpeqd	%ymm4, %ymm14, %ymm14
	vpcmpeqd	%ymm4, %ymm15, %ymm15

	vmovdqa	-32(%r10),%ymm7			# .Lgather_permd
	lea	128($inp), $inp
	mov	\$9,$power

.Loop_gather_1024:
	vmovdqa		32*0-128($inp),	%ymm0
	vmovdqa		32*1-128($inp),	%ymm1
	vmovdqa		32*2-128($inp),	%ymm2
	vmovdqa		32*3-128($inp),	%ymm3
	vpand		32*0+128(%rax),	%ymm0,	%ymm0
	vpand		32*1+128(%rax),	%ymm1,	%ymm1
	vpand		32*2+128(%rax),	%ymm2,	%ymm2
	vpor		%ymm0, %ymm1, %ymm4
	vpand		32*3+128(%rax),	%ymm3,	%ymm3
	vmovdqa		32*4-128($inp),	%ymm0
	vmovdqa		32*5-128($inp),	%ymm1
	vpor		%ymm2, %ymm3, %ymm5
	vmovdqa		32*6-128($inp),	%ymm2
	vmovdqa		32*7-128($inp),	%ymm3
	vpand		32*4+128(%rax),	%ymm0,	%ymm0
	vpand		32*5+128(%rax),	%ymm1,	%ymm1
	vpand		32*6+128(%rax),	%ymm2,	%ymm2
	vpor		%ymm0, %ymm4, %ymm4
	vpand		32*7+128(%rax),	%ymm3,	%ymm3
	vpand		32*8-128($inp),	%ymm8,	%ymm0
	vpor		%ymm1, %ymm5, %ymm5
	vpand		32*9-128($inp),	%ymm9,	%ymm1
	vpor		%ymm2, %ymm4, %ymm4
	vpand		32*10-128($inp),%ymm10,	%ymm2
	vpor		%ymm3, %ymm5, %ymm5
	vpand		32*11-128($inp),%ymm11,	%ymm3
	vpor		%ymm0, %ymm4, %ymm4
	vpand		32*12-128($inp),%ymm12,	%ymm0
	vpor		%ymm1, %ymm5, %ymm5
	vpand		32*13-128($inp),%ymm13,	%ymm1
	vpor		%ymm2, %ymm4, %ymm4
	vpand		32*14-128($inp),%ymm14,	%ymm2
	vpor		%ymm3, %ymm5, %ymm5
	vpand		32*15-128($inp),%ymm15,	%ymm3
	lea		32*16($inp), $inp
	vpor		%ymm0, %ymm4, %ymm4
	vpor		%ymm1, %ymm5, %ymm5
	vpor		%ymm2, %ymm4, %ymm4
	vpor		%ymm3, %ymm5, %ymm5

	vpor		%ymm5, %ymm4, %ymm4
	vextracti128	\$1, %ymm4, %xmm5	# upper half is cleared
	vpor		%xmm4, %xmm5, %xmm5
	vpermd		%ymm5,%ymm7,%ymm5
	vmovdqu		%ymm5,($out)
	lea		32($out),$out
	dec	$power
	jnz	.Loop_gather_1024

	vpxor	%ymm0,%ymm0,%ymm0
	vmovdqu	%ymm0,($out)
	vzeroupper
___
$code.=<<___ if ($win64);
	movaps	-0xa8(%r11),%xmm6
	movaps	-0x98(%r11),%xmm7
	movaps	-0x88(%r11),%xmm8
	movaps	-0x78(%r11),%xmm9
	movaps	-0x68(%r11),%xmm10
	movaps	-0x58(%r11),%xmm11
	movaps	-0x48(%r11),%xmm12
	movaps	-0x38(%r11),%xmm13
	movaps	-0x28(%r11),%xmm14
	movaps	-0x18(%r11),%xmm15
___
$code.=<<___;
	lea	(%r11),%rsp
.cfi_def_cfa_register	%rsp
	ret
.cfi_endproc
.LSEH_end_rsaz_1024_gather5:
.size	rsaz_1024_gather5_avx2,.-rsaz_1024_gather5_avx2
___
}

$code.=<<___;
.extern	OPENSSL_ia32cap_P
.globl	rsaz_avx2_eligible
.type	rsaz_avx2_eligible,\@abi-omnipotent
.align	32
rsaz_avx2_eligible:
	mov	OPENSSL_ia32cap_P+8(%rip),%eax
___
$code.=<<___	if ($addx);
	mov	\$`1<<8|1<<19`,%ecx
	mov	\$0,%edx
	and	%eax,%ecx
	cmp	\$`1<<8|1<<19`,%ecx	# check for BMI2+AD*X
	cmove	%edx,%eax
___
$code.=<<___;
	and	\$`1<<5`,%eax
	shr	\$5,%eax
	ret
.size	rsaz_avx2_eligible,.-rsaz_avx2_eligible

.align	64
.Land_mask:
	.quad	0x1fffffff,0x1fffffff,0x1fffffff,0x1fffffff
.Lscatter_permd:
	.long	0,2,4,6,7,7,7,7
.Lgather_permd:
	.long	0,7,1,7,2,7,3,7
.Linc:
	.long	0,0,0,0, 1,1,1,1
	.long	2,2,2,2, 3,3,3,3
	.long	4,4,4,4, 4,4,4,4
.align	64
___

if ($win64) {
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___
.extern	__imp_RtlVirtualUnwind
.type	rsaz_se_handler,\@abi-omnipotent
.align	16
rsaz_se_handler:
	push	%rsi
	push	%rdi
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	pushfq
	sub	\$64,%rsp

	mov	120($context),%rax	# pull context->Rax
	mov	248($context),%rbx	# pull context->Rip

	mov	8($disp),%rsi		# disp->ImageBase
	mov	56($disp),%r11		# disp->HandlerData

	mov	0(%r11),%r10d		# HandlerData[0]
	lea	(%rsi,%r10),%r10	# prologue label
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lcommon_seh_tail

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	mov	160($context),%rbp	# pull context->Rbp

	mov	8(%r11),%r10d		# HandlerData[2]
	lea	(%rsi,%r10),%r10	# "in tail" label
	cmp	%r10,%rbx		# context->Rip>="in tail" label
	cmovc	%rbp,%rax

	mov	-48(%rax),%r15
	mov	-40(%rax),%r14
	mov	-32(%rax),%r13
	mov	-24(%rax),%r12
	mov	-16(%rax),%rbp
	mov	-8(%rax),%rbx
	mov	%r15,240($context)
	mov	%r14,232($context)
	mov	%r13,224($context)
	mov	%r12,216($context)
	mov	%rbp,160($context)
	mov	%rbx,144($context)

	lea	-0xd8(%rax),%rsi	# %xmm save area
	lea	512($context),%rdi	# & context.Xmm6
	mov	\$20,%ecx		# 10*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq

.Lcommon_seh_tail:
	mov	8(%rax),%rdi
	mov	16(%rax),%rsi
	mov	%rax,152($context)	# restore context->Rsp
	mov	%rsi,168($context)	# restore context->Rsi
	mov	%rdi,176($context)	# restore context->Rdi

	mov	40($disp),%rdi		# disp->ContextRecord
	mov	$context,%rsi		# context
	mov	\$154,%ecx		# sizeof(CONTEXT)
	.long	0xa548f3fc		# cld; rep movsq

	mov	$disp,%rsi
	xor	%rcx,%rcx		# arg1, UNW_FLAG_NHANDLER
	mov	8(%rsi),%rdx		# arg2, disp->ImageBase
	mov	0(%rsi),%r8		# arg3, disp->ControlPc
	mov	16(%rsi),%r9		# arg4, disp->FunctionEntry
	mov	40(%rsi),%r10		# disp->ContextRecord
	lea	56(%rsi),%r11		# &disp->HandlerData
	lea	24(%rsi),%r12		# &disp->EstablisherFrame
	mov	%r10,32(%rsp)		# arg5
	mov	%r11,40(%rsp)		# arg6
	mov	%r12,48(%rsp)		# arg7
	mov	%rcx,56(%rsp)		# arg8, (NULL)
	call	*__imp_RtlVirtualUnwind(%rip)

	mov	\$1,%eax		# ExceptionContinueSearch
	add	\$64,%rsp
	popfq
	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	pop	%rbp
	pop	%rbx
	pop	%rdi
	pop	%rsi
	ret
.size	rsaz_se_handler,.-rsaz_se_handler

.section	.pdata
.align	4
	.rva	.LSEH_begin_rsaz_1024_sqr_avx2
	.rva	.LSEH_end_rsaz_1024_sqr_avx2
	.rva	.LSEH_info_rsaz_1024_sqr_avx2

	.rva	.LSEH_begin_rsaz_1024_mul_avx2
	.rva	.LSEH_end_rsaz_1024_mul_avx2
	.rva	.LSEH_info_rsaz_1024_mul_avx2

	.rva	.LSEH_begin_rsaz_1024_gather5
	.rva	.LSEH_end_rsaz_1024_gather5
	.rva	.LSEH_info_rsaz_1024_gather5
.section	.xdata
.align	8
.LSEH_info_rsaz_1024_sqr_avx2:
	.byte	9,0,0,0
	.rva	rsaz_se_handler
	.rva	.Lsqr_1024_body,.Lsqr_1024_epilogue,.Lsqr_1024_in_tail
	.long	0
.LSEH_info_rsaz_1024_mul_avx2:
	.byte	9,0,0,0
	.rva	rsaz_se_handler
	.rva	.Lmul_1024_body,.Lmul_1024_epilogue,.Lmul_1024_in_tail
	.long	0
.LSEH_info_rsaz_1024_gather5:
	.byte	0x01,0x36,0x17,0x0b
	.byte	0x36,0xf8,0x09,0x00	# vmovaps 0x90(rsp),xmm15
	.byte	0x31,0xe8,0x08,0x00	# vmovaps 0x80(rsp),xmm14
	.byte	0x2c,0xd8,0x07,0x00	# vmovaps 0x70(rsp),xmm13
	.byte	0x27,0xc8,0x06,0x00	# vmovaps 0x60(rsp),xmm12
	.byte	0x22,0xb8,0x05,0x00	# vmovaps 0x50(rsp),xmm11
	.byte	0x1d,0xa8,0x04,0x00	# vmovaps 0x40(rsp),xmm10
	.byte	0x18,0x98,0x03,0x00	# vmovaps 0x30(rsp),xmm9
	.byte	0x13,0x88,0x02,0x00	# vmovaps 0x20(rsp),xmm8
	.byte	0x0e,0x78,0x01,0x00	# vmovaps 0x10(rsp),xmm7
	.byte	0x09,0x68,0x00,0x00	# vmovaps 0x00(rsp),xmm6
	.byte	0x04,0x01,0x15,0x00	# sub	  rsp,0xa8
	.byte	0x00,0xb3,0x00,0x00	# set_frame r11
___
}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval($1)/ge;

	s/\b(sh[rl]d?\s+\$)(-?[0-9]+)/$1.$2%64/ge		or

	s/\b(vmov[dq])\b(.+)%ymm([0-9]+)/$1$2%xmm$3/go		or
	s/\b(vmovdqu)\b(.+)%x%ymm([0-9]+)/$1$2%xmm$3/go		or
	s/\b(vpinsr[qd])\b(.+)%ymm([0-9]+)/$1$2%xmm$3/go	or
	s/\b(vpextr[qd])\b(.+)%ymm([0-9]+)/$1$2%xmm$3/go	or
	s/\b(vpbroadcast[qd]\s+)%ymm([0-9]+)/$1%xmm$2/go;
	print $_,"\n";
}

}}} else {{{
print <<___;	# assembler is too old
.text

.globl	rsaz_avx2_eligible
.type	rsaz_avx2_eligible,\@abi-omnipotent
rsaz_avx2_eligible:
	xor	%eax,%eax
	ret
.size	rsaz_avx2_eligible,.-rsaz_avx2_eligible

.globl	rsaz_1024_sqr_avx2
.globl	rsaz_1024_mul_avx2
.globl	rsaz_1024_norm2red_avx2
.globl	rsaz_1024_red2norm_avx2
.globl	rsaz_1024_scatter5_avx2
.globl	rsaz_1024_gather5_avx2
.type	rsaz_1024_sqr_avx2,\@abi-omnipotent
rsaz_1024_sqr_avx2:
rsaz_1024_mul_avx2:
rsaz_1024_norm2red_avx2:
rsaz_1024_red2norm_avx2:
rsaz_1024_scatter5_avx2:
rsaz_1024_gather5_avx2:
	.byte	0x0f,0x0b	# ud2
	ret
.size	rsaz_1024_sqr_avx2,.-rsaz_1024_sqr_avx2
___
}}}

close STDOUT;

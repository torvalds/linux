#! /usr/bin/env perl
# Copyright 2014-2019 The OpenSSL Project Authors. All Rights Reserved.
# Copyright (c) 2014, Intel Corporation. All Rights Reserved.
# Copyright (c) 2015 CloudFlare, Inc.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html
#
# Originally written by Shay Gueron (1, 2), and Vlad Krasnov (1, 3)
# (1) Intel Corporation, Israel Development Center, Haifa, Israel
# (2) University of Haifa, Israel
# (3) CloudFlare, Inc.
#
# Reference:
# S.Gueron and V.Krasnov, "Fast Prime Field Elliptic Curve Cryptography with
#                          256 Bit Primes"

# Further optimization by <appro@openssl.org>:
#
#		this/original	with/without -DECP_NISTZ256_ASM(*)
# Opteron	+15-49%		+150-195%
# Bulldozer	+18-45%		+175-240%
# P4		+24-46%		+100-150%
# Westmere	+18-34%		+87-160%
# Sandy Bridge	+14-35%		+120-185%
# Ivy Bridge	+11-35%		+125-180%
# Haswell	+10-37%		+160-200%
# Broadwell	+24-58%		+210-270%
# Atom		+20-50%		+180-240%
# VIA Nano	+50-160%	+480-480%
#
# (*)	"without -DECP_NISTZ256_ASM" refers to build with
#	"enable-ec_nistp_64_gcc_128";
#
# Ranges denote minimum and maximum improvement coefficients depending
# on benchmark. In "this/original" column lower coefficient is for
# ECDSA sign, while in "with/without" - for ECDH key agreement, and
# higher - for ECDSA sign, relatively fastest server-side operation.
# Keep in mind that +100% means 2x improvement.

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
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

if (!$addx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|.*based on LLVM) ([3-9])\.([0-9]+)/) {
	my $ver = $2 + $3/100.0;	# 3.1->3.01, 3.10->3.10
	$avx = ($ver>=3.0) + ($ver>=3.01);
	$addx = ($ver>=3.03);
}

$code.=<<___;
.text
.extern	OPENSSL_ia32cap_P

# The polynomial
.align 64
.Lpoly:
.quad 0xffffffffffffffff, 0x00000000ffffffff, 0x0000000000000000, 0xffffffff00000001

# 2^512 mod P precomputed for NIST P256 polynomial
.LRR:
.quad 0x0000000000000003, 0xfffffffbffffffff, 0xfffffffffffffffe, 0x00000004fffffffd

.LOne:
.long 1,1,1,1,1,1,1,1
.LTwo:
.long 2,2,2,2,2,2,2,2
.LThree:
.long 3,3,3,3,3,3,3,3
.LONE_mont:
.quad 0x0000000000000001, 0xffffffff00000000, 0xffffffffffffffff, 0x00000000fffffffe

# Constants for computations modulo ord(p256)
.Lord:
.quad 0xf3b9cac2fc632551, 0xbce6faada7179e84, 0xffffffffffffffff, 0xffffffff00000000
.LordK:
.quad 0xccd1c8aaee00bc4f
___

{
################################################################################
# void ecp_nistz256_mul_by_2(uint64_t res[4], uint64_t a[4]);

my ($a0,$a1,$a2,$a3)=map("%r$_",(8..11));
my ($t0,$t1,$t2,$t3,$t4)=("%rax","%rdx","%rcx","%r12","%r13");
my ($r_ptr,$a_ptr,$b_ptr)=("%rdi","%rsi","%rdx");

$code.=<<___;

.globl	ecp_nistz256_mul_by_2
.type	ecp_nistz256_mul_by_2,\@function,2
.align	64
ecp_nistz256_mul_by_2:
.cfi_startproc
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
.Lmul_by_2_body:

	mov	8*0($a_ptr), $a0
	xor	$t4,$t4
	mov	8*1($a_ptr), $a1
	add	$a0, $a0		# a0:a3+a0:a3
	mov	8*2($a_ptr), $a2
	adc	$a1, $a1
	mov	8*3($a_ptr), $a3
	lea	.Lpoly(%rip), $a_ptr
	 mov	$a0, $t0
	adc	$a2, $a2
	adc	$a3, $a3
	 mov	$a1, $t1
	adc	\$0, $t4

	sub	8*0($a_ptr), $a0
	 mov	$a2, $t2
	sbb	8*1($a_ptr), $a1
	sbb	8*2($a_ptr), $a2
	 mov	$a3, $t3
	sbb	8*3($a_ptr), $a3
	sbb	\$0, $t4

	cmovc	$t0, $a0
	cmovc	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovc	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovc	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	mov	0(%rsp),%r13
.cfi_restore	%r13
	mov	8(%rsp),%r12
.cfi_restore	%r12
	lea	16(%rsp),%rsp
.cfi_adjust_cfa_offset	-16
.Lmul_by_2_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_mul_by_2,.-ecp_nistz256_mul_by_2

################################################################################
# void ecp_nistz256_div_by_2(uint64_t res[4], uint64_t a[4]);
.globl	ecp_nistz256_div_by_2
.type	ecp_nistz256_div_by_2,\@function,2
.align	32
ecp_nistz256_div_by_2:
.cfi_startproc
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
.Ldiv_by_2_body:

	mov	8*0($a_ptr), $a0
	mov	8*1($a_ptr), $a1
	mov	8*2($a_ptr), $a2
	 mov	$a0, $t0
	mov	8*3($a_ptr), $a3
	lea	.Lpoly(%rip), $a_ptr

	 mov	$a1, $t1
	xor	$t4, $t4
	add	8*0($a_ptr), $a0
	 mov	$a2, $t2
	adc	8*1($a_ptr), $a1
	adc	8*2($a_ptr), $a2
	 mov	$a3, $t3
	adc	8*3($a_ptr), $a3
	adc	\$0, $t4
	xor	$a_ptr, $a_ptr		# borrow $a_ptr
	test	\$1, $t0

	cmovz	$t0, $a0
	cmovz	$t1, $a1
	cmovz	$t2, $a2
	cmovz	$t3, $a3
	cmovz	$a_ptr, $t4

	mov	$a1, $t0		# a0:a3>>1
	shr	\$1, $a0
	shl	\$63, $t0
	mov	$a2, $t1
	shr	\$1, $a1
	or	$t0, $a0
	shl	\$63, $t1
	mov	$a3, $t2
	shr	\$1, $a2
	or	$t1, $a1
	shl	\$63, $t2
	shr	\$1, $a3
	shl	\$63, $t4
	or	$t2, $a2
	or	$t4, $a3

	mov	$a0, 8*0($r_ptr)
	mov	$a1, 8*1($r_ptr)
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	mov	0(%rsp),%r13
.cfi_restore	%r13
	mov	8(%rsp),%r12
.cfi_restore	%r12
	lea	16(%rsp),%rsp
.cfi_adjust_cfa_offset	-16
.Ldiv_by_2_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_div_by_2,.-ecp_nistz256_div_by_2

################################################################################
# void ecp_nistz256_mul_by_3(uint64_t res[4], uint64_t a[4]);
.globl	ecp_nistz256_mul_by_3
.type	ecp_nistz256_mul_by_3,\@function,2
.align	32
ecp_nistz256_mul_by_3:
.cfi_startproc
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
.Lmul_by_3_body:

	mov	8*0($a_ptr), $a0
	xor	$t4, $t4
	mov	8*1($a_ptr), $a1
	add	$a0, $a0		# a0:a3+a0:a3
	mov	8*2($a_ptr), $a2
	adc	$a1, $a1
	mov	8*3($a_ptr), $a3
	 mov	$a0, $t0
	adc	$a2, $a2
	adc	$a3, $a3
	 mov	$a1, $t1
	adc	\$0, $t4

	sub	\$-1, $a0
	 mov	$a2, $t2
	sbb	.Lpoly+8*1(%rip), $a1
	sbb	\$0, $a2
	 mov	$a3, $t3
	sbb	.Lpoly+8*3(%rip), $a3
	sbb	\$0, $t4

	cmovc	$t0, $a0
	cmovc	$t1, $a1
	cmovc	$t2, $a2
	cmovc	$t3, $a3

	xor	$t4, $t4
	add	8*0($a_ptr), $a0	# a0:a3+=a_ptr[0:3]
	adc	8*1($a_ptr), $a1
	 mov	$a0, $t0
	adc	8*2($a_ptr), $a2
	adc	8*3($a_ptr), $a3
	 mov	$a1, $t1
	adc	\$0, $t4

	sub	\$-1, $a0
	 mov	$a2, $t2
	sbb	.Lpoly+8*1(%rip), $a1
	sbb	\$0, $a2
	 mov	$a3, $t3
	sbb	.Lpoly+8*3(%rip), $a3
	sbb	\$0, $t4

	cmovc	$t0, $a0
	cmovc	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovc	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovc	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	mov	0(%rsp),%r13
.cfi_restore	%r13
	mov	8(%rsp),%r12
.cfi_restore	%r12
	lea	16(%rsp),%rsp
.cfi_adjust_cfa_offset	-16
.Lmul_by_3_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_mul_by_3,.-ecp_nistz256_mul_by_3

################################################################################
# void ecp_nistz256_add(uint64_t res[4], uint64_t a[4], uint64_t b[4]);
.globl	ecp_nistz256_add
.type	ecp_nistz256_add,\@function,3
.align	32
ecp_nistz256_add:
.cfi_startproc
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
.Ladd_body:

	mov	8*0($a_ptr), $a0
	xor	$t4, $t4
	mov	8*1($a_ptr), $a1
	mov	8*2($a_ptr), $a2
	mov	8*3($a_ptr), $a3
	lea	.Lpoly(%rip), $a_ptr

	add	8*0($b_ptr), $a0
	adc	8*1($b_ptr), $a1
	 mov	$a0, $t0
	adc	8*2($b_ptr), $a2
	adc	8*3($b_ptr), $a3
	 mov	$a1, $t1
	adc	\$0, $t4

	sub	8*0($a_ptr), $a0
	 mov	$a2, $t2
	sbb	8*1($a_ptr), $a1
	sbb	8*2($a_ptr), $a2
	 mov	$a3, $t3
	sbb	8*3($a_ptr), $a3
	sbb	\$0, $t4

	cmovc	$t0, $a0
	cmovc	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovc	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovc	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	mov	0(%rsp),%r13
.cfi_restore	%r13
	mov	8(%rsp),%r12
.cfi_restore	%r12
	lea	16(%rsp),%rsp
.cfi_adjust_cfa_offset	-16
.Ladd_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_add,.-ecp_nistz256_add

################################################################################
# void ecp_nistz256_sub(uint64_t res[4], uint64_t a[4], uint64_t b[4]);
.globl	ecp_nistz256_sub
.type	ecp_nistz256_sub,\@function,3
.align	32
ecp_nistz256_sub:
.cfi_startproc
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
.Lsub_body:

	mov	8*0($a_ptr), $a0
	xor	$t4, $t4
	mov	8*1($a_ptr), $a1
	mov	8*2($a_ptr), $a2
	mov	8*3($a_ptr), $a3
	lea	.Lpoly(%rip), $a_ptr

	sub	8*0($b_ptr), $a0
	sbb	8*1($b_ptr), $a1
	 mov	$a0, $t0
	sbb	8*2($b_ptr), $a2
	sbb	8*3($b_ptr), $a3
	 mov	$a1, $t1
	sbb	\$0, $t4

	add	8*0($a_ptr), $a0
	 mov	$a2, $t2
	adc	8*1($a_ptr), $a1
	adc	8*2($a_ptr), $a2
	 mov	$a3, $t3
	adc	8*3($a_ptr), $a3
	test	$t4, $t4

	cmovz	$t0, $a0
	cmovz	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovz	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovz	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	mov	0(%rsp),%r13
.cfi_restore	%r13
	mov	8(%rsp),%r12
.cfi_restore	%r12
	lea	16(%rsp),%rsp
.cfi_adjust_cfa_offset	-16
.Lsub_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_sub,.-ecp_nistz256_sub

################################################################################
# void ecp_nistz256_neg(uint64_t res[4], uint64_t a[4]);
.globl	ecp_nistz256_neg
.type	ecp_nistz256_neg,\@function,2
.align	32
ecp_nistz256_neg:
.cfi_startproc
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
.Lneg_body:

	xor	$a0, $a0
	xor	$a1, $a1
	xor	$a2, $a2
	xor	$a3, $a3
	xor	$t4, $t4

	sub	8*0($a_ptr), $a0
	sbb	8*1($a_ptr), $a1
	sbb	8*2($a_ptr), $a2
	 mov	$a0, $t0
	sbb	8*3($a_ptr), $a3
	lea	.Lpoly(%rip), $a_ptr
	 mov	$a1, $t1
	sbb	\$0, $t4

	add	8*0($a_ptr), $a0
	 mov	$a2, $t2
	adc	8*1($a_ptr), $a1
	adc	8*2($a_ptr), $a2
	 mov	$a3, $t3
	adc	8*3($a_ptr), $a3
	test	$t4, $t4

	cmovz	$t0, $a0
	cmovz	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovz	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovz	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	mov	0(%rsp),%r13
.cfi_restore	%r13
	mov	8(%rsp),%r12
.cfi_restore	%r12
	lea	16(%rsp),%rsp
.cfi_adjust_cfa_offset	-16
.Lneg_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_neg,.-ecp_nistz256_neg
___
}
{
my ($r_ptr,$a_ptr,$b_org,$b_ptr)=("%rdi","%rsi","%rdx","%rbx");
my ($acc0,$acc1,$acc2,$acc3,$acc4,$acc5,$acc6,$acc7)=map("%r$_",(8..15));
my ($t0,$t1,$t2,$t3,$t4)=("%rcx","%rbp","%rbx","%rdx","%rax");
my ($poly1,$poly3)=($acc6,$acc7);

$code.=<<___;
################################################################################
# void ecp_nistz256_ord_mul_mont(
#   uint64_t res[4],
#   uint64_t a[4],
#   uint64_t b[4]);

.globl	ecp_nistz256_ord_mul_mont
.type	ecp_nistz256_ord_mul_mont,\@function,3
.align	32
ecp_nistz256_ord_mul_mont:
.cfi_startproc
___
$code.=<<___	if ($addx);
	mov	\$0x80100, %ecx
	and	OPENSSL_ia32cap_P+8(%rip), %ecx
	cmp	\$0x80100, %ecx
	je	.Lecp_nistz256_ord_mul_montx
___
$code.=<<___;
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Lord_mul_body:

	mov	8*0($b_org), %rax
	mov	$b_org, $b_ptr
	lea	.Lord(%rip), %r14
	mov	.LordK(%rip), %r15

	################################# * b[0]
	mov	%rax, $t0
	mulq	8*0($a_ptr)
	mov	%rax, $acc0
	mov	$t0, %rax
	mov	%rdx, $acc1

	mulq	8*1($a_ptr)
	add	%rax, $acc1
	mov	$t0, %rax
	adc	\$0, %rdx
	mov	%rdx, $acc2

	mulq	8*2($a_ptr)
	add	%rax, $acc2
	mov	$t0, %rax
	adc	\$0, %rdx

	 mov	$acc0, $acc5
	 imulq	%r15,$acc0

	mov	%rdx, $acc3
	mulq	8*3($a_ptr)
	add	%rax, $acc3
	 mov	$acc0, %rax
	adc	\$0, %rdx
	mov	%rdx, $acc4

	################################# First reduction step
	mulq	8*0(%r14)
	mov	$acc0, $t1
	add	%rax, $acc5		# guaranteed to be zero
	mov	$acc0, %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	sub	$acc0, $acc2
	sbb	\$0, $acc0		# can't borrow

	mulq	8*1(%r14)
	add	$t0, $acc1
	adc	\$0, %rdx
	add	%rax, $acc1
	mov	$t1, %rax
	adc	%rdx, $acc2
	mov	$t1, %rdx
	adc	\$0, $acc0		# can't overflow

	shl	\$32, %rax
	shr	\$32, %rdx
	sub	%rax, $acc3
	 mov	8*1($b_ptr), %rax
	sbb	%rdx, $t1		# can't borrow

	add	$acc0, $acc3
	adc	$t1, $acc4
	adc	\$0, $acc5

	################################# * b[1]
	mov	%rax, $t0
	mulq	8*0($a_ptr)
	add	%rax, $acc1
	mov	$t0, %rax
	adc	\$0, %rdx
	mov	%rdx, $t1

	mulq	8*1($a_ptr)
	add	$t1, $acc2
	adc	\$0, %rdx
	add	%rax, $acc2
	mov	$t0, %rax
	adc	\$0, %rdx
	mov	%rdx, $t1

	mulq	8*2($a_ptr)
	add	$t1, $acc3
	adc	\$0, %rdx
	add	%rax, $acc3
	mov	$t0, %rax
	adc	\$0, %rdx

	 mov	$acc1, $t0
	 imulq	%r15, $acc1

	mov	%rdx, $t1
	mulq	8*3($a_ptr)
	add	$t1, $acc4
	adc	\$0, %rdx
	xor	$acc0, $acc0
	add	%rax, $acc4
	 mov	$acc1, %rax
	adc	%rdx, $acc5
	adc	\$0, $acc0

	################################# Second reduction step
	mulq	8*0(%r14)
	mov	$acc1, $t1
	add	%rax, $t0		# guaranteed to be zero
	mov	$acc1, %rax
	adc	%rdx, $t0

	sub	$acc1, $acc3
	sbb	\$0, $acc1		# can't borrow

	mulq	8*1(%r14)
	add	$t0, $acc2
	adc	\$0, %rdx
	add	%rax, $acc2
	mov	$t1, %rax
	adc	%rdx, $acc3
	mov	$t1, %rdx
	adc	\$0, $acc1		# can't overflow

	shl	\$32, %rax
	shr	\$32, %rdx
	sub	%rax, $acc4
	 mov	8*2($b_ptr), %rax
	sbb	%rdx, $t1		# can't borrow

	add	$acc1, $acc4
	adc	$t1, $acc5
	adc	\$0, $acc0

	################################## * b[2]
	mov	%rax, $t0
	mulq	8*0($a_ptr)
	add	%rax, $acc2
	mov	$t0, %rax
	adc	\$0, %rdx
	mov	%rdx, $t1

	mulq	8*1($a_ptr)
	add	$t1, $acc3
	adc	\$0, %rdx
	add	%rax, $acc3
	mov	$t0, %rax
	adc	\$0, %rdx
	mov	%rdx, $t1

	mulq	8*2($a_ptr)
	add	$t1, $acc4
	adc	\$0, %rdx
	add	%rax, $acc4
	mov	$t0, %rax
	adc	\$0, %rdx

	 mov	$acc2, $t0
	 imulq	%r15, $acc2

	mov	%rdx, $t1
	mulq	8*3($a_ptr)
	add	$t1, $acc5
	adc	\$0, %rdx
	xor	$acc1, $acc1
	add	%rax, $acc5
	 mov	$acc2, %rax
	adc	%rdx, $acc0
	adc	\$0, $acc1

	################################# Third reduction step
	mulq	8*0(%r14)
	mov	$acc2, $t1
	add	%rax, $t0		# guaranteed to be zero
	mov	$acc2, %rax
	adc	%rdx, $t0

	sub	$acc2, $acc4
	sbb	\$0, $acc2		# can't borrow

	mulq	8*1(%r14)
	add	$t0, $acc3
	adc	\$0, %rdx
	add	%rax, $acc3
	mov	$t1, %rax
	adc	%rdx, $acc4
	mov	$t1, %rdx
	adc	\$0, $acc2		# can't overflow

	shl	\$32, %rax
	shr	\$32, %rdx
	sub	%rax, $acc5
	 mov	8*3($b_ptr), %rax
	sbb	%rdx, $t1		# can't borrow

	add	$acc2, $acc5
	adc	$t1, $acc0
	adc	\$0, $acc1

	################################# * b[3]
	mov	%rax, $t0
	mulq	8*0($a_ptr)
	add	%rax, $acc3
	mov	$t0, %rax
	adc	\$0, %rdx
	mov	%rdx, $t1

	mulq	8*1($a_ptr)
	add	$t1, $acc4
	adc	\$0, %rdx
	add	%rax, $acc4
	mov	$t0, %rax
	adc	\$0, %rdx
	mov	%rdx, $t1

	mulq	8*2($a_ptr)
	add	$t1, $acc5
	adc	\$0, %rdx
	add	%rax, $acc5
	mov	$t0, %rax
	adc	\$0, %rdx

	 mov	$acc3, $t0
	 imulq	%r15, $acc3

	mov	%rdx, $t1
	mulq	8*3($a_ptr)
	add	$t1, $acc0
	adc	\$0, %rdx
	xor	$acc2, $acc2
	add	%rax, $acc0
	 mov	$acc3, %rax
	adc	%rdx, $acc1
	adc	\$0, $acc2

	################################# Last reduction step
	mulq	8*0(%r14)
	mov	$acc3, $t1
	add	%rax, $t0		# guaranteed to be zero
	mov	$acc3, %rax
	adc	%rdx, $t0

	sub	$acc3, $acc5
	sbb	\$0, $acc3		# can't borrow

	mulq	8*1(%r14)
	add	$t0, $acc4
	adc	\$0, %rdx
	add	%rax, $acc4
	mov	$t1, %rax
	adc	%rdx, $acc5
	mov	$t1, %rdx
	adc	\$0, $acc3		# can't overflow

	shl	\$32, %rax
	shr	\$32, %rdx
	sub	%rax, $acc0
	sbb	%rdx, $t1		# can't borrow

	add	$acc3, $acc0
	adc	$t1, $acc1
	adc	\$0, $acc2

	################################# Subtract ord
	 mov	$acc4, $a_ptr
	sub	8*0(%r14), $acc4
	 mov	$acc5, $acc3
	sbb	8*1(%r14), $acc5
	 mov	$acc0, $t0
	sbb	8*2(%r14), $acc0
	 mov	$acc1, $t1
	sbb	8*3(%r14), $acc1
	sbb	\$0, $acc2

	cmovc	$a_ptr, $acc4
	cmovc	$acc3, $acc5
	cmovc	$t0, $acc0
	cmovc	$t1, $acc1

	mov	$acc4, 8*0($r_ptr)
	mov	$acc5, 8*1($r_ptr)
	mov	$acc0, 8*2($r_ptr)
	mov	$acc1, 8*3($r_ptr)

	mov	0(%rsp),%r15
.cfi_restore	%r15
	mov	8(%rsp),%r14
.cfi_restore	%r14
	mov	16(%rsp),%r13
.cfi_restore	%r13
	mov	24(%rsp),%r12
.cfi_restore	%r12
	mov	32(%rsp),%rbx
.cfi_restore	%rbx
	mov	40(%rsp),%rbp
.cfi_restore	%rbp
	lea	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lord_mul_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_ord_mul_mont,.-ecp_nistz256_ord_mul_mont

################################################################################
# void ecp_nistz256_ord_sqr_mont(
#   uint64_t res[4],
#   uint64_t a[4],
#   int rep);

.globl	ecp_nistz256_ord_sqr_mont
.type	ecp_nistz256_ord_sqr_mont,\@function,3
.align	32
ecp_nistz256_ord_sqr_mont:
.cfi_startproc
___
$code.=<<___	if ($addx);
	mov	\$0x80100, %ecx
	and	OPENSSL_ia32cap_P+8(%rip), %ecx
	cmp	\$0x80100, %ecx
	je	.Lecp_nistz256_ord_sqr_montx
___
$code.=<<___;
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Lord_sqr_body:

	mov	8*0($a_ptr), $acc0
	mov	8*1($a_ptr), %rax
	mov	8*2($a_ptr), $acc6
	mov	8*3($a_ptr), $acc7
	lea	.Lord(%rip), $a_ptr	# pointer to modulus
	mov	$b_org, $b_ptr
	jmp	.Loop_ord_sqr

.align	32
.Loop_ord_sqr:
	################################# a[1:] * a[0]
	mov	%rax, $t1		# put aside a[1]
	mul	$acc0			# a[1] * a[0]
	mov	%rax, $acc1
	movq	$t1, %xmm1		# offload a[1]
	mov	$acc6, %rax
	mov	%rdx, $acc2

	mul	$acc0			# a[2] * a[0]
	add	%rax, $acc2
	mov	$acc7, %rax
	movq	$acc6, %xmm2		# offload a[2]
	adc	\$0, %rdx
	mov	%rdx, $acc3

	mul	$acc0			# a[3] * a[0]
	add	%rax, $acc3
	mov	$acc7, %rax
	movq	$acc7, %xmm3		# offload a[3]
	adc	\$0, %rdx
	mov	%rdx, $acc4

	################################# a[3] * a[2]
	mul	$acc6			# a[3] * a[2]
	mov	%rax, $acc5
	mov	$acc6, %rax
	mov	%rdx, $acc6

	################################# a[2:] * a[1]
	mul	$t1			# a[2] * a[1]
	add	%rax, $acc3
	mov	$acc7, %rax
	adc	\$0, %rdx
	mov	%rdx, $acc7

	mul	$t1			# a[3] * a[1]
	add	%rax, $acc4
	adc	\$0, %rdx

	add	$acc7, $acc4
	adc	%rdx, $acc5
	adc	\$0, $acc6		# can't overflow

	################################# *2
	xor	$acc7, $acc7
	mov	$acc0, %rax
	add	$acc1, $acc1
	adc	$acc2, $acc2
	adc	$acc3, $acc3
	adc	$acc4, $acc4
	adc	$acc5, $acc5
	adc	$acc6, $acc6
	adc	\$0, $acc7

	################################# Missing products
	mul	%rax			# a[0] * a[0]
	mov	%rax, $acc0
	movq	%xmm1, %rax
	mov	%rdx, $t1

	mul	%rax			# a[1] * a[1]
	add	$t1, $acc1
	adc	%rax, $acc2
	movq	%xmm2, %rax
	adc	\$0, %rdx
	mov	%rdx, $t1

	mul	%rax			# a[2] * a[2]
	add	$t1, $acc3
	adc	%rax, $acc4
	movq	%xmm3, %rax
	adc	\$0, %rdx
	mov	%rdx, $t1

	 mov	$acc0, $t0
	 imulq	8*4($a_ptr), $acc0	# *= .LordK

	mul	%rax			# a[3] * a[3]
	add	$t1, $acc5
	adc	%rax, $acc6
	 mov	8*0($a_ptr), %rax	# modulus[0]
	adc	%rdx, $acc7		# can't overflow

	################################# First reduction step
	mul	$acc0
	mov	$acc0, $t1
	add	%rax, $t0		# guaranteed to be zero
	mov	8*1($a_ptr), %rax	# modulus[1]
	adc	%rdx, $t0

	sub	$acc0, $acc2
	sbb	\$0, $t1		# can't borrow

	mul	$acc0
	add	$t0, $acc1
	adc	\$0, %rdx
	add	%rax, $acc1
	mov	$acc0, %rax
	adc	%rdx, $acc2
	mov	$acc0, %rdx
	adc	\$0, $t1		# can't overflow

	 mov	$acc1, $t0
	 imulq	8*4($a_ptr), $acc1	# *= .LordK

	shl	\$32, %rax
	shr	\$32, %rdx
	sub	%rax, $acc3
	 mov	8*0($a_ptr), %rax
	sbb	%rdx, $acc0		# can't borrow

	add	$t1, $acc3
	adc	\$0, $acc0		# can't overflow

	################################# Second reduction step
	mul	$acc1
	mov	$acc1, $t1
	add	%rax, $t0		# guaranteed to be zero
	mov	8*1($a_ptr), %rax
	adc	%rdx, $t0

	sub	$acc1, $acc3
	sbb	\$0, $t1		# can't borrow

	mul	$acc1
	add	$t0, $acc2
	adc	\$0, %rdx
	add	%rax, $acc2
	mov	$acc1, %rax
	adc	%rdx, $acc3
	mov	$acc1, %rdx
	adc	\$0, $t1		# can't overflow

	 mov	$acc2, $t0
	 imulq	8*4($a_ptr), $acc2	# *= .LordK

	shl	\$32, %rax
	shr	\$32, %rdx
	sub	%rax, $acc0
	 mov	8*0($a_ptr), %rax
	sbb	%rdx, $acc1		# can't borrow

	add	$t1, $acc0
	adc	\$0, $acc1		# can't overflow

	################################# Third reduction step
	mul	$acc2
	mov	$acc2, $t1
	add	%rax, $t0		# guaranteed to be zero
	mov	8*1($a_ptr), %rax
	adc	%rdx, $t0

	sub	$acc2, $acc0
	sbb	\$0, $t1		# can't borrow

	mul	$acc2
	add	$t0, $acc3
	adc	\$0, %rdx
	add	%rax, $acc3
	mov	$acc2, %rax
	adc	%rdx, $acc0
	mov	$acc2, %rdx
	adc	\$0, $t1		# can't overflow

	 mov	$acc3, $t0
	 imulq	8*4($a_ptr), $acc3	# *= .LordK

	shl	\$32, %rax
	shr	\$32, %rdx
	sub	%rax, $acc1
	 mov	8*0($a_ptr), %rax
	sbb	%rdx, $acc2		# can't borrow

	add	$t1, $acc1
	adc	\$0, $acc2		# can't overflow

	################################# Last reduction step
	mul	$acc3
	mov	$acc3, $t1
	add	%rax, $t0		# guaranteed to be zero
	mov	8*1($a_ptr), %rax
	adc	%rdx, $t0

	sub	$acc3, $acc1
	sbb	\$0, $t1		# can't borrow

	mul	$acc3
	add	$t0, $acc0
	adc	\$0, %rdx
	add	%rax, $acc0
	mov	$acc3, %rax
	adc	%rdx, $acc1
	mov	$acc3, %rdx
	adc	\$0, $t1		# can't overflow

	shl	\$32, %rax
	shr	\$32, %rdx
	sub	%rax, $acc2
	sbb	%rdx, $acc3		# can't borrow

	add	$t1, $acc2
	adc	\$0, $acc3		# can't overflow

	################################# Add bits [511:256] of the sqr result
	xor	%rdx, %rdx
	add	$acc4, $acc0
	adc	$acc5, $acc1
	 mov	$acc0, $acc4
	adc	$acc6, $acc2
	adc	$acc7, $acc3
	 mov	$acc1, %rax
	adc	\$0, %rdx

	################################# Compare to modulus
	sub	8*0($a_ptr), $acc0
	 mov	$acc2, $acc6
	sbb	8*1($a_ptr), $acc1
	sbb	8*2($a_ptr), $acc2
	 mov	$acc3, $acc7
	sbb	8*3($a_ptr), $acc3
	sbb	\$0, %rdx

	cmovc	$acc4, $acc0
	cmovnc	$acc1, %rax
	cmovnc	$acc2, $acc6
	cmovnc	$acc3, $acc7

	dec	$b_ptr
	jnz	.Loop_ord_sqr

	mov	$acc0, 8*0($r_ptr)
	mov	%rax,  8*1($r_ptr)
	pxor	%xmm1, %xmm1
	mov	$acc6, 8*2($r_ptr)
	pxor	%xmm2, %xmm2
	mov	$acc7, 8*3($r_ptr)
	pxor	%xmm3, %xmm3

	mov	0(%rsp),%r15
.cfi_restore	%r15
	mov	8(%rsp),%r14
.cfi_restore	%r14
	mov	16(%rsp),%r13
.cfi_restore	%r13
	mov	24(%rsp),%r12
.cfi_restore	%r12
	mov	32(%rsp),%rbx
.cfi_restore	%rbx
	mov	40(%rsp),%rbp
.cfi_restore	%rbp
	lea	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lord_sqr_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_ord_sqr_mont,.-ecp_nistz256_ord_sqr_mont
___

$code.=<<___	if ($addx);
################################################################################
.type	ecp_nistz256_ord_mul_montx,\@function,3
.align	32
ecp_nistz256_ord_mul_montx:
.cfi_startproc
.Lecp_nistz256_ord_mul_montx:
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Lord_mulx_body:

	mov	$b_org, $b_ptr
	mov	8*0($b_org), %rdx
	mov	8*0($a_ptr), $acc1
	mov	8*1($a_ptr), $acc2
	mov	8*2($a_ptr), $acc3
	mov	8*3($a_ptr), $acc4
	lea	-128($a_ptr), $a_ptr	# control u-op density
	lea	.Lord-128(%rip), %r14
	mov	.LordK(%rip), %r15

	################################# Multiply by b[0]
	mulx	$acc1, $acc0, $acc1
	mulx	$acc2, $t0, $acc2
	mulx	$acc3, $t1, $acc3
	add	$t0, $acc1
	mulx	$acc4, $t0, $acc4
	 mov	$acc0, %rdx
	 mulx	%r15, %rdx, %rax
	adc	$t1, $acc2
	adc	$t0, $acc3
	adc	\$0, $acc4

	################################# reduction
	xor	$acc5, $acc5		# $acc5=0, cf=0, of=0
	mulx	8*0+128(%r14), $t0, $t1
	adcx	$t0, $acc0		# guaranteed to be zero
	adox	$t1, $acc1

	mulx	8*1+128(%r14), $t0, $t1
	adcx	$t0, $acc1
	adox	$t1, $acc2

	mulx	8*2+128(%r14), $t0, $t1
	adcx	$t0, $acc2
	adox	$t1, $acc3

	mulx	8*3+128(%r14), $t0, $t1
	 mov	8*1($b_ptr), %rdx
	adcx	$t0, $acc3
	adox	$t1, $acc4
	adcx	$acc0, $acc4
	adox	$acc0, $acc5
	adc	\$0, $acc5		# cf=0, of=0

	################################# Multiply by b[1]
	mulx	8*0+128($a_ptr), $t0, $t1
	adcx	$t0, $acc1
	adox	$t1, $acc2

	mulx	8*1+128($a_ptr), $t0, $t1
	adcx	$t0, $acc2
	adox	$t1, $acc3

	mulx	8*2+128($a_ptr), $t0, $t1
	adcx	$t0, $acc3
	adox	$t1, $acc4

	mulx	8*3+128($a_ptr), $t0, $t1
	 mov	$acc1, %rdx
	 mulx	%r15, %rdx, %rax
	adcx	$t0, $acc4
	adox	$t1, $acc5

	adcx	$acc0, $acc5
	adox	$acc0, $acc0
	adc	\$0, $acc0		# cf=0, of=0

	################################# reduction
	mulx	8*0+128(%r14), $t0, $t1
	adcx	$t0, $acc1		# guaranteed to be zero
	adox	$t1, $acc2

	mulx	8*1+128(%r14), $t0, $t1
	adcx	$t0, $acc2
	adox	$t1, $acc3

	mulx	8*2+128(%r14), $t0, $t1
	adcx	$t0, $acc3
	adox	$t1, $acc4

	mulx	8*3+128(%r14), $t0, $t1
	 mov	8*2($b_ptr), %rdx
	adcx	$t0, $acc4
	adox	$t1, $acc5
	adcx	$acc1, $acc5
	adox	$acc1, $acc0
	adc	\$0, $acc0		# cf=0, of=0

	################################# Multiply by b[2]
	mulx	8*0+128($a_ptr), $t0, $t1
	adcx	$t0, $acc2
	adox	$t1, $acc3

	mulx	8*1+128($a_ptr), $t0, $t1
	adcx	$t0, $acc3
	adox	$t1, $acc4

	mulx	8*2+128($a_ptr), $t0, $t1
	adcx	$t0, $acc4
	adox	$t1, $acc5

	mulx	8*3+128($a_ptr), $t0, $t1
	 mov	$acc2, %rdx
	 mulx	%r15, %rdx, %rax
	adcx	$t0, $acc5
	adox	$t1, $acc0

	adcx	$acc1, $acc0
	adox	$acc1, $acc1
	adc	\$0, $acc1		# cf=0, of=0

	################################# reduction
	mulx	8*0+128(%r14), $t0, $t1
	adcx	$t0, $acc2		# guaranteed to be zero
	adox	$t1, $acc3

	mulx	8*1+128(%r14), $t0, $t1
	adcx	$t0, $acc3
	adox	$t1, $acc4

	mulx	8*2+128(%r14), $t0, $t1
	adcx	$t0, $acc4
	adox	$t1, $acc5

	mulx	8*3+128(%r14), $t0, $t1
	 mov	8*3($b_ptr), %rdx
	adcx	$t0, $acc5
	adox	$t1, $acc0
	adcx	$acc2, $acc0
	adox	$acc2, $acc1
	adc	\$0, $acc1		# cf=0, of=0

	################################# Multiply by b[3]
	mulx	8*0+128($a_ptr), $t0, $t1
	adcx	$t0, $acc3
	adox	$t1, $acc4

	mulx	8*1+128($a_ptr), $t0, $t1
	adcx	$t0, $acc4
	adox	$t1, $acc5

	mulx	8*2+128($a_ptr), $t0, $t1
	adcx	$t0, $acc5
	adox	$t1, $acc0

	mulx	8*3+128($a_ptr), $t0, $t1
	 mov	$acc3, %rdx
	 mulx	%r15, %rdx, %rax
	adcx	$t0, $acc0
	adox	$t1, $acc1

	adcx	$acc2, $acc1
	adox	$acc2, $acc2
	adc	\$0, $acc2		# cf=0, of=0

	################################# reduction
	mulx	8*0+128(%r14), $t0, $t1
	adcx	$t0, $acc3		# guranteed to be zero
	adox	$t1, $acc4

	mulx	8*1+128(%r14), $t0, $t1
	adcx	$t0, $acc4
	adox	$t1, $acc5

	mulx	8*2+128(%r14), $t0, $t1
	adcx	$t0, $acc5
	adox	$t1, $acc0

	mulx	8*3+128(%r14), $t0, $t1
	lea	128(%r14),%r14
	 mov	$acc4, $t2
	adcx	$t0, $acc0
	adox	$t1, $acc1
	 mov	$acc5, $t3
	adcx	$acc3, $acc1
	adox	$acc3, $acc2
	adc	\$0, $acc2

	#################################
	# Branch-less conditional subtraction of P
	 mov	$acc0, $t0
	sub	8*0(%r14), $acc4
	sbb	8*1(%r14), $acc5
	sbb	8*2(%r14), $acc0
	 mov	$acc1, $t1
	sbb	8*3(%r14), $acc1
	sbb	\$0, $acc2

	cmovc	$t2, $acc4
	cmovc	$t3, $acc5
	cmovc	$t0, $acc0
	cmovc	$t1, $acc1

	mov	$acc4, 8*0($r_ptr)
	mov	$acc5, 8*1($r_ptr)
	mov	$acc0, 8*2($r_ptr)
	mov	$acc1, 8*3($r_ptr)

	mov	0(%rsp),%r15
.cfi_restore	%r15
	mov	8(%rsp),%r14
.cfi_restore	%r14
	mov	16(%rsp),%r13
.cfi_restore	%r13
	mov	24(%rsp),%r12
.cfi_restore	%r12
	mov	32(%rsp),%rbx
.cfi_restore	%rbx
	mov	40(%rsp),%rbp
.cfi_restore	%rbp
	lea	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lord_mulx_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_ord_mul_montx,.-ecp_nistz256_ord_mul_montx

.type	ecp_nistz256_ord_sqr_montx,\@function,3
.align	32
ecp_nistz256_ord_sqr_montx:
.cfi_startproc
.Lecp_nistz256_ord_sqr_montx:
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Lord_sqrx_body:

	mov	$b_org, $b_ptr
	mov	8*0($a_ptr), %rdx
	mov	8*1($a_ptr), $acc6
	mov	8*2($a_ptr), $acc7
	mov	8*3($a_ptr), $acc0
	lea	.Lord(%rip), $a_ptr
	jmp	.Loop_ord_sqrx

.align	32
.Loop_ord_sqrx:
	mulx	$acc6, $acc1, $acc2	# a[0]*a[1]
	mulx	$acc7, $t0, $acc3	# a[0]*a[2]
	 mov	%rdx, %rax		# offload a[0]
	 movq	$acc6, %xmm1		# offload a[1]
	mulx	$acc0, $t1, $acc4	# a[0]*a[3]
	 mov	$acc6, %rdx
	add	$t0, $acc2
	 movq	$acc7, %xmm2		# offload a[2]
	adc	$t1, $acc3
	adc	\$0, $acc4
	xor	$acc5, $acc5		# $acc5=0,cf=0,of=0
	#################################
	mulx	$acc7, $t0, $t1		# a[1]*a[2]
	adcx	$t0, $acc3
	adox	$t1, $acc4

	mulx	$acc0, $t0, $t1		# a[1]*a[3]
	 mov	$acc7, %rdx
	adcx	$t0, $acc4
	adox	$t1, $acc5
	adc	\$0, $acc5
	#################################
	mulx	$acc0, $t0, $acc6	# a[2]*a[3]
	mov	%rax, %rdx
	 movq	$acc0, %xmm3		# offload a[3]
	xor	$acc7, $acc7		# $acc7=0,cf=0,of=0
	 adcx	$acc1, $acc1		# acc1:6<<1
	adox	$t0, $acc5
	 adcx	$acc2, $acc2
	adox	$acc7, $acc6		# of=0

	################################# a[i]*a[i]
	mulx	%rdx, $acc0, $t1
	movq	%xmm1, %rdx
	 adcx	$acc3, $acc3
	adox	$t1, $acc1
	 adcx	$acc4, $acc4
	mulx	%rdx, $t0, $t4
	movq	%xmm2, %rdx
	 adcx	$acc5, $acc5
	adox	$t0, $acc2
	 adcx	$acc6, $acc6
	mulx	%rdx, $t0, $t1
	.byte	0x67
	movq	%xmm3, %rdx
	adox	$t4, $acc3
	 adcx	$acc7, $acc7
	adox	$t0, $acc4
	adox	$t1, $acc5
	mulx	%rdx, $t0, $t4
	adox	$t0, $acc6
	adox	$t4, $acc7

	################################# reduction
	mov	$acc0, %rdx
	mulx	8*4($a_ptr), %rdx, $t0

	xor	%rax, %rax		# cf=0, of=0
	mulx	8*0($a_ptr), $t0, $t1
	adcx	$t0, $acc0		# guaranteed to be zero
	adox	$t1, $acc1
	mulx	8*1($a_ptr), $t0, $t1
	adcx	$t0, $acc1
	adox	$t1, $acc2
	mulx	8*2($a_ptr), $t0, $t1
	adcx	$t0, $acc2
	adox	$t1, $acc3
	mulx	8*3($a_ptr), $t0, $t1
	adcx	$t0, $acc3
	adox	$t1, $acc0		# of=0
	adcx	%rax, $acc0		# cf=0

	#################################
	mov	$acc1, %rdx
	mulx	8*4($a_ptr), %rdx, $t0

	mulx	8*0($a_ptr), $t0, $t1
	adox	$t0, $acc1		# guaranteed to be zero
	adcx	$t1, $acc2
	mulx	8*1($a_ptr), $t0, $t1
	adox	$t0, $acc2
	adcx	$t1, $acc3
	mulx	8*2($a_ptr), $t0, $t1
	adox	$t0, $acc3
	adcx	$t1, $acc0
	mulx	8*3($a_ptr), $t0, $t1
	adox	$t0, $acc0
	adcx	$t1, $acc1		# cf=0
	adox	%rax, $acc1		# of=0

	#################################
	mov	$acc2, %rdx
	mulx	8*4($a_ptr), %rdx, $t0

	mulx	8*0($a_ptr), $t0, $t1
	adcx	$t0, $acc2		# guaranteed to be zero
	adox	$t1, $acc3
	mulx	8*1($a_ptr), $t0, $t1
	adcx	$t0, $acc3
	adox	$t1, $acc0
	mulx	8*2($a_ptr), $t0, $t1
	adcx	$t0, $acc0
	adox	$t1, $acc1
	mulx	8*3($a_ptr), $t0, $t1
	adcx	$t0, $acc1
	adox	$t1, $acc2		# of=0
	adcx	%rax, $acc2		# cf=0

	#################################
	mov	$acc3, %rdx
	mulx	8*4($a_ptr), %rdx, $t0

	mulx	8*0($a_ptr), $t0, $t1
	adox	$t0, $acc3		# guaranteed to be zero
	adcx	$t1, $acc0
	mulx	8*1($a_ptr), $t0, $t1
	adox	$t0, $acc0
	adcx	$t1, $acc1
	mulx	8*2($a_ptr), $t0, $t1
	adox	$t0, $acc1
	adcx	$t1, $acc2
	mulx	8*3($a_ptr), $t0, $t1
	adox	$t0, $acc2
	adcx	$t1, $acc3
	adox	%rax, $acc3

	################################# accumulate upper half
	add	$acc0, $acc4		# add	$acc4, $acc0
	adc	$acc5, $acc1
	 mov	$acc4, %rdx
	adc	$acc6, $acc2
	adc	$acc7, $acc3
	 mov	$acc1, $acc6
	adc	\$0, %rax

	################################# compare to modulus
	sub	8*0($a_ptr), $acc4
	 mov	$acc2, $acc7
	sbb	8*1($a_ptr), $acc1
	sbb	8*2($a_ptr), $acc2
	 mov	$acc3, $acc0
	sbb	8*3($a_ptr), $acc3
	sbb	\$0, %rax

	cmovnc	$acc4, %rdx
	cmovnc	$acc1, $acc6
	cmovnc	$acc2, $acc7
	cmovnc	$acc3, $acc0

	dec	$b_ptr
	jnz	.Loop_ord_sqrx

	mov	%rdx, 8*0($r_ptr)
	mov	$acc6, 8*1($r_ptr)
	pxor	%xmm1, %xmm1
	mov	$acc7, 8*2($r_ptr)
	pxor	%xmm2, %xmm2
	mov	$acc0, 8*3($r_ptr)
	pxor	%xmm3, %xmm3

	mov	0(%rsp),%r15
.cfi_restore	%r15
	mov	8(%rsp),%r14
.cfi_restore	%r14
	mov	16(%rsp),%r13
.cfi_restore	%r13
	mov	24(%rsp),%r12
.cfi_restore	%r12
	mov	32(%rsp),%rbx
.cfi_restore	%rbx
	mov	40(%rsp),%rbp
.cfi_restore	%rbp
	lea	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lord_sqrx_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_ord_sqr_montx,.-ecp_nistz256_ord_sqr_montx
___

$code.=<<___;
################################################################################
# void ecp_nistz256_to_mont(
#   uint64_t res[4],
#   uint64_t in[4]);
.globl	ecp_nistz256_to_mont
.type	ecp_nistz256_to_mont,\@function,2
.align	32
ecp_nistz256_to_mont:
___
$code.=<<___	if ($addx);
	mov	\$0x80100, %ecx
	and	OPENSSL_ia32cap_P+8(%rip), %ecx
___
$code.=<<___;
	lea	.LRR(%rip), $b_org
	jmp	.Lmul_mont
.size	ecp_nistz256_to_mont,.-ecp_nistz256_to_mont

################################################################################
# void ecp_nistz256_mul_mont(
#   uint64_t res[4],
#   uint64_t a[4],
#   uint64_t b[4]);

.globl	ecp_nistz256_mul_mont
.type	ecp_nistz256_mul_mont,\@function,3
.align	32
ecp_nistz256_mul_mont:
.cfi_startproc
___
$code.=<<___	if ($addx);
	mov	\$0x80100, %ecx
	and	OPENSSL_ia32cap_P+8(%rip), %ecx
___
$code.=<<___;
.Lmul_mont:
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Lmul_body:
___
$code.=<<___	if ($addx);
	cmp	\$0x80100, %ecx
	je	.Lmul_montx
___
$code.=<<___;
	mov	$b_org, $b_ptr
	mov	8*0($b_org), %rax
	mov	8*0($a_ptr), $acc1
	mov	8*1($a_ptr), $acc2
	mov	8*2($a_ptr), $acc3
	mov	8*3($a_ptr), $acc4

	call	__ecp_nistz256_mul_montq
___
$code.=<<___	if ($addx);
	jmp	.Lmul_mont_done

.align	32
.Lmul_montx:
	mov	$b_org, $b_ptr
	mov	8*0($b_org), %rdx
	mov	8*0($a_ptr), $acc1
	mov	8*1($a_ptr), $acc2
	mov	8*2($a_ptr), $acc3
	mov	8*3($a_ptr), $acc4
	lea	-128($a_ptr), $a_ptr	# control u-op density

	call	__ecp_nistz256_mul_montx
___
$code.=<<___;
.Lmul_mont_done:
	mov	0(%rsp),%r15
.cfi_restore	%r15
	mov	8(%rsp),%r14
.cfi_restore	%r14
	mov	16(%rsp),%r13
.cfi_restore	%r13
	mov	24(%rsp),%r12
.cfi_restore	%r12
	mov	32(%rsp),%rbx
.cfi_restore	%rbx
	mov	40(%rsp),%rbp
.cfi_restore	%rbp
	lea	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lmul_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_mul_mont,.-ecp_nistz256_mul_mont

.type	__ecp_nistz256_mul_montq,\@abi-omnipotent
.align	32
__ecp_nistz256_mul_montq:
.cfi_startproc
	########################################################################
	# Multiply a by b[0]
	mov	%rax, $t1
	mulq	$acc1
	mov	.Lpoly+8*1(%rip),$poly1
	mov	%rax, $acc0
	mov	$t1, %rax
	mov	%rdx, $acc1

	mulq	$acc2
	mov	.Lpoly+8*3(%rip),$poly3
	add	%rax, $acc1
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $acc2

	mulq	$acc3
	add	%rax, $acc2
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $acc3

	mulq	$acc4
	add	%rax, $acc3
	 mov	$acc0, %rax
	adc	\$0, %rdx
	xor	$acc5, $acc5
	mov	%rdx, $acc4

	########################################################################
	# First reduction step
	# Basically now we want to multiply acc[0] by p256,
	# and add the result to the acc.
	# Due to the special form of p256 we do some optimizations
	#
	# acc[0] x p256[0..1] = acc[0] x 2^96 - acc[0]
	# then we add acc[0] and get acc[0] x 2^96

	mov	$acc0, $t1
	shl	\$32, $acc0
	mulq	$poly3
	shr	\$32, $t1
	add	$acc0, $acc1		# +=acc[0]<<96
	adc	$t1, $acc2
	adc	%rax, $acc3
	 mov	8*1($b_ptr), %rax
	adc	%rdx, $acc4
	adc	\$0, $acc5
	xor	$acc0, $acc0

	########################################################################
	# Multiply by b[1]
	mov	%rax, $t1
	mulq	8*0($a_ptr)
	add	%rax, $acc1
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	8*1($a_ptr)
	add	$t0, $acc2
	adc	\$0, %rdx
	add	%rax, $acc2
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	8*2($a_ptr)
	add	$t0, $acc3
	adc	\$0, %rdx
	add	%rax, $acc3
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	8*3($a_ptr)
	add	$t0, $acc4
	adc	\$0, %rdx
	add	%rax, $acc4
	 mov	$acc1, %rax
	adc	%rdx, $acc5
	adc	\$0, $acc0

	########################################################################
	# Second reduction step
	mov	$acc1, $t1
	shl	\$32, $acc1
	mulq	$poly3
	shr	\$32, $t1
	add	$acc1, $acc2
	adc	$t1, $acc3
	adc	%rax, $acc4
	 mov	8*2($b_ptr), %rax
	adc	%rdx, $acc5
	adc	\$0, $acc0
	xor	$acc1, $acc1

	########################################################################
	# Multiply by b[2]
	mov	%rax, $t1
	mulq	8*0($a_ptr)
	add	%rax, $acc2
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	8*1($a_ptr)
	add	$t0, $acc3
	adc	\$0, %rdx
	add	%rax, $acc3
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	8*2($a_ptr)
	add	$t0, $acc4
	adc	\$0, %rdx
	add	%rax, $acc4
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	8*3($a_ptr)
	add	$t0, $acc5
	adc	\$0, %rdx
	add	%rax, $acc5
	 mov	$acc2, %rax
	adc	%rdx, $acc0
	adc	\$0, $acc1

	########################################################################
	# Third reduction step
	mov	$acc2, $t1
	shl	\$32, $acc2
	mulq	$poly3
	shr	\$32, $t1
	add	$acc2, $acc3
	adc	$t1, $acc4
	adc	%rax, $acc5
	 mov	8*3($b_ptr), %rax
	adc	%rdx, $acc0
	adc	\$0, $acc1
	xor	$acc2, $acc2

	########################################################################
	# Multiply by b[3]
	mov	%rax, $t1
	mulq	8*0($a_ptr)
	add	%rax, $acc3
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	8*1($a_ptr)
	add	$t0, $acc4
	adc	\$0, %rdx
	add	%rax, $acc4
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	8*2($a_ptr)
	add	$t0, $acc5
	adc	\$0, %rdx
	add	%rax, $acc5
	mov	$t1, %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	8*3($a_ptr)
	add	$t0, $acc0
	adc	\$0, %rdx
	add	%rax, $acc0
	 mov	$acc3, %rax
	adc	%rdx, $acc1
	adc	\$0, $acc2

	########################################################################
	# Final reduction step
	mov	$acc3, $t1
	shl	\$32, $acc3
	mulq	$poly3
	shr	\$32, $t1
	add	$acc3, $acc4
	adc	$t1, $acc5
	 mov	$acc4, $t0
	adc	%rax, $acc0
	adc	%rdx, $acc1
	 mov	$acc5, $t1
	adc	\$0, $acc2

	########################################################################
	# Branch-less conditional subtraction of P
	sub	\$-1, $acc4		# .Lpoly[0]
	 mov	$acc0, $t2
	sbb	$poly1, $acc5		# .Lpoly[1]
	sbb	\$0, $acc0		# .Lpoly[2]
	 mov	$acc1, $t3
	sbb	$poly3, $acc1		# .Lpoly[3]
	sbb	\$0, $acc2

	cmovc	$t0, $acc4
	cmovc	$t1, $acc5
	mov	$acc4, 8*0($r_ptr)
	cmovc	$t2, $acc0
	mov	$acc5, 8*1($r_ptr)
	cmovc	$t3, $acc1
	mov	$acc0, 8*2($r_ptr)
	mov	$acc1, 8*3($r_ptr)

	ret
.cfi_endproc
.size	__ecp_nistz256_mul_montq,.-__ecp_nistz256_mul_montq

################################################################################
# void ecp_nistz256_sqr_mont(
#   uint64_t res[4],
#   uint64_t a[4]);

# we optimize the square according to S.Gueron and V.Krasnov,
# "Speeding up Big-Number Squaring"
.globl	ecp_nistz256_sqr_mont
.type	ecp_nistz256_sqr_mont,\@function,2
.align	32
ecp_nistz256_sqr_mont:
.cfi_startproc
___
$code.=<<___	if ($addx);
	mov	\$0x80100, %ecx
	and	OPENSSL_ia32cap_P+8(%rip), %ecx
___
$code.=<<___;
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Lsqr_body:
___
$code.=<<___	if ($addx);
	cmp	\$0x80100, %ecx
	je	.Lsqr_montx
___
$code.=<<___;
	mov	8*0($a_ptr), %rax
	mov	8*1($a_ptr), $acc6
	mov	8*2($a_ptr), $acc7
	mov	8*3($a_ptr), $acc0

	call	__ecp_nistz256_sqr_montq
___
$code.=<<___	if ($addx);
	jmp	.Lsqr_mont_done

.align	32
.Lsqr_montx:
	mov	8*0($a_ptr), %rdx
	mov	8*1($a_ptr), $acc6
	mov	8*2($a_ptr), $acc7
	mov	8*3($a_ptr), $acc0
	lea	-128($a_ptr), $a_ptr	# control u-op density

	call	__ecp_nistz256_sqr_montx
___
$code.=<<___;
.Lsqr_mont_done:
	mov	0(%rsp),%r15
.cfi_restore	%r15
	mov	8(%rsp),%r14
.cfi_restore	%r14
	mov	16(%rsp),%r13
.cfi_restore	%r13
	mov	24(%rsp),%r12
.cfi_restore	%r12
	mov	32(%rsp),%rbx
.cfi_restore	%rbx
	mov	40(%rsp),%rbp
.cfi_restore	%rbp
	lea	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lsqr_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_sqr_mont,.-ecp_nistz256_sqr_mont

.type	__ecp_nistz256_sqr_montq,\@abi-omnipotent
.align	32
__ecp_nistz256_sqr_montq:
.cfi_startproc
	mov	%rax, $acc5
	mulq	$acc6			# a[1]*a[0]
	mov	%rax, $acc1
	mov	$acc7, %rax
	mov	%rdx, $acc2

	mulq	$acc5			# a[0]*a[2]
	add	%rax, $acc2
	mov	$acc0, %rax
	adc	\$0, %rdx
	mov	%rdx, $acc3

	mulq	$acc5			# a[0]*a[3]
	add	%rax, $acc3
	 mov	$acc7, %rax
	adc	\$0, %rdx
	mov	%rdx, $acc4

	#################################
	mulq	$acc6			# a[1]*a[2]
	add	%rax, $acc3
	mov	$acc0, %rax
	adc	\$0, %rdx
	mov	%rdx, $t1

	mulq	$acc6			# a[1]*a[3]
	add	%rax, $acc4
	 mov	$acc0, %rax
	adc	\$0, %rdx
	add	$t1, $acc4
	mov	%rdx, $acc5
	adc	\$0, $acc5

	#################################
	mulq	$acc7			# a[2]*a[3]
	xor	$acc7, $acc7
	add	%rax, $acc5
	 mov	8*0($a_ptr), %rax
	mov	%rdx, $acc6
	adc	\$0, $acc6

	add	$acc1, $acc1		# acc1:6<<1
	adc	$acc2, $acc2
	adc	$acc3, $acc3
	adc	$acc4, $acc4
	adc	$acc5, $acc5
	adc	$acc6, $acc6
	adc	\$0, $acc7

	mulq	%rax
	mov	%rax, $acc0
	mov	8*1($a_ptr), %rax
	mov	%rdx, $t0

	mulq	%rax
	add	$t0, $acc1
	adc	%rax, $acc2
	mov	8*2($a_ptr), %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	%rax
	add	$t0, $acc3
	adc	%rax, $acc4
	mov	8*3($a_ptr), %rax
	adc	\$0, %rdx
	mov	%rdx, $t0

	mulq	%rax
	add	$t0, $acc5
	adc	%rax, $acc6
	 mov	$acc0, %rax
	adc	%rdx, $acc7

	mov	.Lpoly+8*1(%rip), $a_ptr
	mov	.Lpoly+8*3(%rip), $t1

	##########################################
	# Now the reduction
	# First iteration
	mov	$acc0, $t0
	shl	\$32, $acc0
	mulq	$t1
	shr	\$32, $t0
	add	$acc0, $acc1		# +=acc[0]<<96
	adc	$t0, $acc2
	adc	%rax, $acc3
	 mov	$acc1, %rax
	adc	\$0, %rdx

	##########################################
	# Second iteration
	mov	$acc1, $t0
	shl	\$32, $acc1
	mov	%rdx, $acc0
	mulq	$t1
	shr	\$32, $t0
	add	$acc1, $acc2
	adc	$t0, $acc3
	adc	%rax, $acc0
	 mov	$acc2, %rax
	adc	\$0, %rdx

	##########################################
	# Third iteration
	mov	$acc2, $t0
	shl	\$32, $acc2
	mov	%rdx, $acc1
	mulq	$t1
	shr	\$32, $t0
	add	$acc2, $acc3
	adc	$t0, $acc0
	adc	%rax, $acc1
	 mov	$acc3, %rax
	adc	\$0, %rdx

	###########################################
	# Last iteration
	mov	$acc3, $t0
	shl	\$32, $acc3
	mov	%rdx, $acc2
	mulq	$t1
	shr	\$32, $t0
	add	$acc3, $acc0
	adc	$t0, $acc1
	adc	%rax, $acc2
	adc	\$0, %rdx
	xor	$acc3, $acc3

	############################################
	# Add the rest of the acc
	add	$acc0, $acc4
	adc	$acc1, $acc5
	 mov	$acc4, $acc0
	adc	$acc2, $acc6
	adc	%rdx, $acc7
	 mov	$acc5, $acc1
	adc	\$0, $acc3

	sub	\$-1, $acc4		# .Lpoly[0]
	 mov	$acc6, $acc2
	sbb	$a_ptr, $acc5		# .Lpoly[1]
	sbb	\$0, $acc6		# .Lpoly[2]
	 mov	$acc7, $t0
	sbb	$t1, $acc7		# .Lpoly[3]
	sbb	\$0, $acc3

	cmovc	$acc0, $acc4
	cmovc	$acc1, $acc5
	mov	$acc4, 8*0($r_ptr)
	cmovc	$acc2, $acc6
	mov	$acc5, 8*1($r_ptr)
	cmovc	$t0, $acc7
	mov	$acc6, 8*2($r_ptr)
	mov	$acc7, 8*3($r_ptr)

	ret
.cfi_endproc
.size	__ecp_nistz256_sqr_montq,.-__ecp_nistz256_sqr_montq
___

if ($addx) {
$code.=<<___;
.type	__ecp_nistz256_mul_montx,\@abi-omnipotent
.align	32
__ecp_nistz256_mul_montx:
.cfi_startproc
	########################################################################
	# Multiply by b[0]
	mulx	$acc1, $acc0, $acc1
	mulx	$acc2, $t0, $acc2
	mov	\$32, $poly1
	xor	$acc5, $acc5		# cf=0
	mulx	$acc3, $t1, $acc3
	mov	.Lpoly+8*3(%rip), $poly3
	adc	$t0, $acc1
	mulx	$acc4, $t0, $acc4
	 mov	$acc0, %rdx
	adc	$t1, $acc2
	 shlx	$poly1,$acc0,$t1
	adc	$t0, $acc3
	 shrx	$poly1,$acc0,$t0
	adc	\$0, $acc4

	########################################################################
	# First reduction step
	add	$t1, $acc1
	adc	$t0, $acc2

	mulx	$poly3, $t0, $t1
	 mov	8*1($b_ptr), %rdx
	adc	$t0, $acc3
	adc	$t1, $acc4
	adc	\$0, $acc5
	xor	$acc0, $acc0		# $acc0=0,cf=0,of=0

	########################################################################
	# Multiply by b[1]
	mulx	8*0+128($a_ptr), $t0, $t1
	adcx	$t0, $acc1
	adox	$t1, $acc2

	mulx	8*1+128($a_ptr), $t0, $t1
	adcx	$t0, $acc2
	adox	$t1, $acc3

	mulx	8*2+128($a_ptr), $t0, $t1
	adcx	$t0, $acc3
	adox	$t1, $acc4

	mulx	8*3+128($a_ptr), $t0, $t1
	 mov	$acc1, %rdx
	adcx	$t0, $acc4
	 shlx	$poly1, $acc1, $t0
	adox	$t1, $acc5
	 shrx	$poly1, $acc1, $t1

	adcx	$acc0, $acc5
	adox	$acc0, $acc0
	adc	\$0, $acc0

	########################################################################
	# Second reduction step
	add	$t0, $acc2
	adc	$t1, $acc3

	mulx	$poly3, $t0, $t1
	 mov	8*2($b_ptr), %rdx
	adc	$t0, $acc4
	adc	$t1, $acc5
	adc	\$0, $acc0
	xor	$acc1 ,$acc1		# $acc1=0,cf=0,of=0

	########################################################################
	# Multiply by b[2]
	mulx	8*0+128($a_ptr), $t0, $t1
	adcx	$t0, $acc2
	adox	$t1, $acc3

	mulx	8*1+128($a_ptr), $t0, $t1
	adcx	$t0, $acc3
	adox	$t1, $acc4

	mulx	8*2+128($a_ptr), $t0, $t1
	adcx	$t0, $acc4
	adox	$t1, $acc5

	mulx	8*3+128($a_ptr), $t0, $t1
	 mov	$acc2, %rdx
	adcx	$t0, $acc5
	 shlx	$poly1, $acc2, $t0
	adox	$t1, $acc0
	 shrx	$poly1, $acc2, $t1

	adcx	$acc1, $acc0
	adox	$acc1, $acc1
	adc	\$0, $acc1

	########################################################################
	# Third reduction step
	add	$t0, $acc3
	adc	$t1, $acc4

	mulx	$poly3, $t0, $t1
	 mov	8*3($b_ptr), %rdx
	adc	$t0, $acc5
	adc	$t1, $acc0
	adc	\$0, $acc1
	xor	$acc2, $acc2		# $acc2=0,cf=0,of=0

	########################################################################
	# Multiply by b[3]
	mulx	8*0+128($a_ptr), $t0, $t1
	adcx	$t0, $acc3
	adox	$t1, $acc4

	mulx	8*1+128($a_ptr), $t0, $t1
	adcx	$t0, $acc4
	adox	$t1, $acc5

	mulx	8*2+128($a_ptr), $t0, $t1
	adcx	$t0, $acc5
	adox	$t1, $acc0

	mulx	8*3+128($a_ptr), $t0, $t1
	 mov	$acc3, %rdx
	adcx	$t0, $acc0
	 shlx	$poly1, $acc3, $t0
	adox	$t1, $acc1
	 shrx	$poly1, $acc3, $t1

	adcx	$acc2, $acc1
	adox	$acc2, $acc2
	adc	\$0, $acc2

	########################################################################
	# Fourth reduction step
	add	$t0, $acc4
	adc	$t1, $acc5

	mulx	$poly3, $t0, $t1
	 mov	$acc4, $t2
	mov	.Lpoly+8*1(%rip), $poly1
	adc	$t0, $acc0
	 mov	$acc5, $t3
	adc	$t1, $acc1
	adc	\$0, $acc2

	########################################################################
	# Branch-less conditional subtraction of P
	xor	%eax, %eax
	 mov	$acc0, $t0
	sbb	\$-1, $acc4		# .Lpoly[0]
	sbb	$poly1, $acc5		# .Lpoly[1]
	sbb	\$0, $acc0		# .Lpoly[2]
	 mov	$acc1, $t1
	sbb	$poly3, $acc1		# .Lpoly[3]
	sbb	\$0, $acc2

	cmovc	$t2, $acc4
	cmovc	$t3, $acc5
	mov	$acc4, 8*0($r_ptr)
	cmovc	$t0, $acc0
	mov	$acc5, 8*1($r_ptr)
	cmovc	$t1, $acc1
	mov	$acc0, 8*2($r_ptr)
	mov	$acc1, 8*3($r_ptr)

	ret
.cfi_endproc
.size	__ecp_nistz256_mul_montx,.-__ecp_nistz256_mul_montx

.type	__ecp_nistz256_sqr_montx,\@abi-omnipotent
.align	32
__ecp_nistz256_sqr_montx:
.cfi_startproc
	mulx	$acc6, $acc1, $acc2	# a[0]*a[1]
	mulx	$acc7, $t0, $acc3	# a[0]*a[2]
	xor	%eax, %eax
	adc	$t0, $acc2
	mulx	$acc0, $t1, $acc4	# a[0]*a[3]
	 mov	$acc6, %rdx
	adc	$t1, $acc3
	adc	\$0, $acc4
	xor	$acc5, $acc5		# $acc5=0,cf=0,of=0

	#################################
	mulx	$acc7, $t0, $t1		# a[1]*a[2]
	adcx	$t0, $acc3
	adox	$t1, $acc4

	mulx	$acc0, $t0, $t1		# a[1]*a[3]
	 mov	$acc7, %rdx
	adcx	$t0, $acc4
	adox	$t1, $acc5
	adc	\$0, $acc5

	#################################
	mulx	$acc0, $t0, $acc6	# a[2]*a[3]
	 mov	8*0+128($a_ptr), %rdx
	xor	$acc7, $acc7		# $acc7=0,cf=0,of=0
	 adcx	$acc1, $acc1		# acc1:6<<1
	adox	$t0, $acc5
	 adcx	$acc2, $acc2
	adox	$acc7, $acc6		# of=0

	mulx	%rdx, $acc0, $t1
	mov	8*1+128($a_ptr), %rdx
	 adcx	$acc3, $acc3
	adox	$t1, $acc1
	 adcx	$acc4, $acc4
	mulx	%rdx, $t0, $t4
	mov	8*2+128($a_ptr), %rdx
	 adcx	$acc5, $acc5
	adox	$t0, $acc2
	 adcx	$acc6, $acc6
	.byte	0x67
	mulx	%rdx, $t0, $t1
	mov	8*3+128($a_ptr), %rdx
	adox	$t4, $acc3
	 adcx	$acc7, $acc7
	adox	$t0, $acc4
	 mov	\$32, $a_ptr
	adox	$t1, $acc5
	.byte	0x67,0x67
	mulx	%rdx, $t0, $t4
	 mov	.Lpoly+8*3(%rip), %rdx
	adox	$t0, $acc6
	 shlx	$a_ptr, $acc0, $t0
	adox	$t4, $acc7
	 shrx	$a_ptr, $acc0, $t4
	mov	%rdx,$t1

	# reduction step 1
	add	$t0, $acc1
	adc	$t4, $acc2

	mulx	$acc0, $t0, $acc0
	adc	$t0, $acc3
	 shlx	$a_ptr, $acc1, $t0
	adc	\$0, $acc0
	 shrx	$a_ptr, $acc1, $t4

	# reduction step 2
	add	$t0, $acc2
	adc	$t4, $acc3

	mulx	$acc1, $t0, $acc1
	adc	$t0, $acc0
	 shlx	$a_ptr, $acc2, $t0
	adc	\$0, $acc1
	 shrx	$a_ptr, $acc2, $t4

	# reduction step 3
	add	$t0, $acc3
	adc	$t4, $acc0

	mulx	$acc2, $t0, $acc2
	adc	$t0, $acc1
	 shlx	$a_ptr, $acc3, $t0
	adc	\$0, $acc2
	 shrx	$a_ptr, $acc3, $t4

	# reduction step 4
	add	$t0, $acc0
	adc	$t4, $acc1

	mulx	$acc3, $t0, $acc3
	adc	$t0, $acc2
	adc	\$0, $acc3

	xor	$t3, $t3
	add	$acc0, $acc4		# accumulate upper half
	 mov	.Lpoly+8*1(%rip), $a_ptr
	adc	$acc1, $acc5
	 mov	$acc4, $acc0
	adc	$acc2, $acc6
	adc	$acc3, $acc7
	 mov	$acc5, $acc1
	adc	\$0, $t3

	sub	\$-1, $acc4		# .Lpoly[0]
	 mov	$acc6, $acc2
	sbb	$a_ptr, $acc5		# .Lpoly[1]
	sbb	\$0, $acc6		# .Lpoly[2]
	 mov	$acc7, $acc3
	sbb	$t1, $acc7		# .Lpoly[3]
	sbb	\$0, $t3

	cmovc	$acc0, $acc4
	cmovc	$acc1, $acc5
	mov	$acc4, 8*0($r_ptr)
	cmovc	$acc2, $acc6
	mov	$acc5, 8*1($r_ptr)
	cmovc	$acc3, $acc7
	mov	$acc6, 8*2($r_ptr)
	mov	$acc7, 8*3($r_ptr)

	ret
.cfi_endproc
.size	__ecp_nistz256_sqr_montx,.-__ecp_nistz256_sqr_montx
___
}
}
{
my ($r_ptr,$in_ptr)=("%rdi","%rsi");
my ($acc0,$acc1,$acc2,$acc3)=map("%r$_",(8..11));
my ($t0,$t1,$t2)=("%rcx","%r12","%r13");

$code.=<<___;
################################################################################
# void ecp_nistz256_from_mont(
#   uint64_t res[4],
#   uint64_t in[4]);
# This one performs Montgomery multiplication by 1, so we only need the reduction

.globl	ecp_nistz256_from_mont
.type	ecp_nistz256_from_mont,\@function,2
.align	32
ecp_nistz256_from_mont:
.cfi_startproc
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
.Lfrom_body:

	mov	8*0($in_ptr), %rax
	mov	.Lpoly+8*3(%rip), $t2
	mov	8*1($in_ptr), $acc1
	mov	8*2($in_ptr), $acc2
	mov	8*3($in_ptr), $acc3
	mov	%rax, $acc0
	mov	.Lpoly+8*1(%rip), $t1

	#########################################
	# First iteration
	mov	%rax, $t0
	shl	\$32, $acc0
	mulq	$t2
	shr	\$32, $t0
	add	$acc0, $acc1
	adc	$t0, $acc2
	adc	%rax, $acc3
	 mov	$acc1, %rax
	adc	\$0, %rdx

	#########################################
	# Second iteration
	mov	$acc1, $t0
	shl	\$32, $acc1
	mov	%rdx, $acc0
	mulq	$t2
	shr	\$32, $t0
	add	$acc1, $acc2
	adc	$t0, $acc3
	adc	%rax, $acc0
	 mov	$acc2, %rax
	adc	\$0, %rdx

	##########################################
	# Third iteration
	mov	$acc2, $t0
	shl	\$32, $acc2
	mov	%rdx, $acc1
	mulq	$t2
	shr	\$32, $t0
	add	$acc2, $acc3
	adc	$t0, $acc0
	adc	%rax, $acc1
	 mov	$acc3, %rax
	adc	\$0, %rdx

	###########################################
	# Last iteration
	mov	$acc3, $t0
	shl	\$32, $acc3
	mov	%rdx, $acc2
	mulq	$t2
	shr	\$32, $t0
	add	$acc3, $acc0
	adc	$t0, $acc1
	 mov	$acc0, $t0
	adc	%rax, $acc2
	 mov	$acc1, $in_ptr
	adc	\$0, %rdx

	###########################################
	# Branch-less conditional subtraction
	sub	\$-1, $acc0
	 mov	$acc2, %rax
	sbb	$t1, $acc1
	sbb	\$0, $acc2
	 mov	%rdx, $acc3
	sbb	$t2, %rdx
	sbb	$t2, $t2

	cmovnz	$t0, $acc0
	cmovnz	$in_ptr, $acc1
	mov	$acc0, 8*0($r_ptr)
	cmovnz	%rax, $acc2
	mov	$acc1, 8*1($r_ptr)
	cmovz	%rdx, $acc3
	mov	$acc2, 8*2($r_ptr)
	mov	$acc3, 8*3($r_ptr)

	mov	0(%rsp),%r13
.cfi_restore	%r13
	mov	8(%rsp),%r12
.cfi_restore	%r12
	lea	16(%rsp),%rsp
.cfi_adjust_cfa_offset	-16
.Lfrom_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_from_mont,.-ecp_nistz256_from_mont
___
}
{
my ($val,$in_t,$index)=$win64?("%rcx","%rdx","%r8d"):("%rdi","%rsi","%edx");
my ($ONE,$INDEX,$Ra,$Rb,$Rc,$Rd,$Re,$Rf)=map("%xmm$_",(0..7));
my ($M0,$T0a,$T0b,$T0c,$T0d,$T0e,$T0f,$TMP0)=map("%xmm$_",(8..15));
my ($M1,$T2a,$T2b,$TMP2,$M2,$T2a,$T2b,$TMP2)=map("%xmm$_",(8..15));

$code.=<<___;
################################################################################
# void ecp_nistz256_scatter_w5(uint64_t *val, uint64_t *in_t, int index);
.globl	ecp_nistz256_scatter_w5
.type	ecp_nistz256_scatter_w5,\@abi-omnipotent
.align	32
ecp_nistz256_scatter_w5:
	lea	-3($index,$index,2), $index
	movdqa	0x00($in_t), %xmm0
	shl	\$5, $index
	movdqa	0x10($in_t), %xmm1
	movdqa	0x20($in_t), %xmm2
	movdqa	0x30($in_t), %xmm3
	movdqa	0x40($in_t), %xmm4
	movdqa	0x50($in_t), %xmm5
	movdqa	%xmm0, 0x00($val,$index)
	movdqa	%xmm1, 0x10($val,$index)
	movdqa	%xmm2, 0x20($val,$index)
	movdqa	%xmm3, 0x30($val,$index)
	movdqa	%xmm4, 0x40($val,$index)
	movdqa	%xmm5, 0x50($val,$index)

	ret
.size	ecp_nistz256_scatter_w5,.-ecp_nistz256_scatter_w5

################################################################################
# void ecp_nistz256_gather_w5(uint64_t *val, uint64_t *in_t, int index);
.globl	ecp_nistz256_gather_w5
.type	ecp_nistz256_gather_w5,\@abi-omnipotent
.align	32
ecp_nistz256_gather_w5:
.cfi_startproc
___
$code.=<<___	if ($avx>1);
	mov	OPENSSL_ia32cap_P+8(%rip), %eax
	test	\$`1<<5`, %eax
	jnz	.Lavx2_gather_w5
___
$code.=<<___	if ($win64);
	lea	-0x88(%rsp), %rax
.LSEH_begin_ecp_nistz256_gather_w5:
	.byte	0x48,0x8d,0x60,0xe0		#lea	-0x20(%rax), %rsp
	.byte	0x0f,0x29,0x70,0xe0		#movaps	%xmm6, -0x20(%rax)
	.byte	0x0f,0x29,0x78,0xf0		#movaps	%xmm7, -0x10(%rax)
	.byte	0x44,0x0f,0x29,0x00		#movaps	%xmm8, 0(%rax)
	.byte	0x44,0x0f,0x29,0x48,0x10	#movaps	%xmm9, 0x10(%rax)
	.byte	0x44,0x0f,0x29,0x50,0x20	#movaps	%xmm10, 0x20(%rax)
	.byte	0x44,0x0f,0x29,0x58,0x30	#movaps	%xmm11, 0x30(%rax)
	.byte	0x44,0x0f,0x29,0x60,0x40	#movaps	%xmm12, 0x40(%rax)
	.byte	0x44,0x0f,0x29,0x68,0x50	#movaps	%xmm13, 0x50(%rax)
	.byte	0x44,0x0f,0x29,0x70,0x60	#movaps	%xmm14, 0x60(%rax)
	.byte	0x44,0x0f,0x29,0x78,0x70	#movaps	%xmm15, 0x70(%rax)
___
$code.=<<___;
	movdqa	.LOne(%rip), $ONE
	movd	$index, $INDEX

	pxor	$Ra, $Ra
	pxor	$Rb, $Rb
	pxor	$Rc, $Rc
	pxor	$Rd, $Rd
	pxor	$Re, $Re
	pxor	$Rf, $Rf

	movdqa	$ONE, $M0
	pshufd	\$0, $INDEX, $INDEX

	mov	\$16, %rax
.Lselect_loop_sse_w5:

	movdqa	$M0, $TMP0
	paddd	$ONE, $M0
	pcmpeqd $INDEX, $TMP0

	movdqa	16*0($in_t), $T0a
	movdqa	16*1($in_t), $T0b
	movdqa	16*2($in_t), $T0c
	movdqa	16*3($in_t), $T0d
	movdqa	16*4($in_t), $T0e
	movdqa	16*5($in_t), $T0f
	lea 16*6($in_t), $in_t

	pand	$TMP0, $T0a
	pand	$TMP0, $T0b
	por	$T0a, $Ra
	pand	$TMP0, $T0c
	por	$T0b, $Rb
	pand	$TMP0, $T0d
	por	$T0c, $Rc
	pand	$TMP0, $T0e
	por	$T0d, $Rd
	pand	$TMP0, $T0f
	por	$T0e, $Re
	por	$T0f, $Rf

	dec	%rax
	jnz	.Lselect_loop_sse_w5

	movdqu	$Ra, 16*0($val)
	movdqu	$Rb, 16*1($val)
	movdqu	$Rc, 16*2($val)
	movdqu	$Rd, 16*3($val)
	movdqu	$Re, 16*4($val)
	movdqu	$Rf, 16*5($val)
___
$code.=<<___	if ($win64);
	movaps	(%rsp), %xmm6
	movaps	0x10(%rsp), %xmm7
	movaps	0x20(%rsp), %xmm8
	movaps	0x30(%rsp), %xmm9
	movaps	0x40(%rsp), %xmm10
	movaps	0x50(%rsp), %xmm11
	movaps	0x60(%rsp), %xmm12
	movaps	0x70(%rsp), %xmm13
	movaps	0x80(%rsp), %xmm14
	movaps	0x90(%rsp), %xmm15
	lea	0xa8(%rsp), %rsp
___
$code.=<<___;
	ret
.cfi_endproc
.LSEH_end_ecp_nistz256_gather_w5:
.size	ecp_nistz256_gather_w5,.-ecp_nistz256_gather_w5

################################################################################
# void ecp_nistz256_scatter_w7(uint64_t *val, uint64_t *in_t, int index);
.globl	ecp_nistz256_scatter_w7
.type	ecp_nistz256_scatter_w7,\@abi-omnipotent
.align	32
ecp_nistz256_scatter_w7:
	movdqu	0x00($in_t), %xmm0
	shl	\$6, $index
	movdqu	0x10($in_t), %xmm1
	movdqu	0x20($in_t), %xmm2
	movdqu	0x30($in_t), %xmm3
	movdqa	%xmm0, 0x00($val,$index)
	movdqa	%xmm1, 0x10($val,$index)
	movdqa	%xmm2, 0x20($val,$index)
	movdqa	%xmm3, 0x30($val,$index)

	ret
.size	ecp_nistz256_scatter_w7,.-ecp_nistz256_scatter_w7

################################################################################
# void ecp_nistz256_gather_w7(uint64_t *val, uint64_t *in_t, int index);
.globl	ecp_nistz256_gather_w7
.type	ecp_nistz256_gather_w7,\@abi-omnipotent
.align	32
ecp_nistz256_gather_w7:
.cfi_startproc
___
$code.=<<___	if ($avx>1);
	mov	OPENSSL_ia32cap_P+8(%rip), %eax
	test	\$`1<<5`, %eax
	jnz	.Lavx2_gather_w7
___
$code.=<<___	if ($win64);
	lea	-0x88(%rsp), %rax
.LSEH_begin_ecp_nistz256_gather_w7:
	.byte	0x48,0x8d,0x60,0xe0		#lea	-0x20(%rax), %rsp
	.byte	0x0f,0x29,0x70,0xe0		#movaps	%xmm6, -0x20(%rax)
	.byte	0x0f,0x29,0x78,0xf0		#movaps	%xmm7, -0x10(%rax)
	.byte	0x44,0x0f,0x29,0x00		#movaps	%xmm8, 0(%rax)
	.byte	0x44,0x0f,0x29,0x48,0x10	#movaps	%xmm9, 0x10(%rax)
	.byte	0x44,0x0f,0x29,0x50,0x20	#movaps	%xmm10, 0x20(%rax)
	.byte	0x44,0x0f,0x29,0x58,0x30	#movaps	%xmm11, 0x30(%rax)
	.byte	0x44,0x0f,0x29,0x60,0x40	#movaps	%xmm12, 0x40(%rax)
	.byte	0x44,0x0f,0x29,0x68,0x50	#movaps	%xmm13, 0x50(%rax)
	.byte	0x44,0x0f,0x29,0x70,0x60	#movaps	%xmm14, 0x60(%rax)
	.byte	0x44,0x0f,0x29,0x78,0x70	#movaps	%xmm15, 0x70(%rax)
___
$code.=<<___;
	movdqa	.LOne(%rip), $M0
	movd	$index, $INDEX

	pxor	$Ra, $Ra
	pxor	$Rb, $Rb
	pxor	$Rc, $Rc
	pxor	$Rd, $Rd

	movdqa	$M0, $ONE
	pshufd	\$0, $INDEX, $INDEX
	mov	\$64, %rax

.Lselect_loop_sse_w7:
	movdqa	$M0, $TMP0
	paddd	$ONE, $M0
	movdqa	16*0($in_t), $T0a
	movdqa	16*1($in_t), $T0b
	pcmpeqd	$INDEX, $TMP0
	movdqa	16*2($in_t), $T0c
	movdqa	16*3($in_t), $T0d
	lea	16*4($in_t), $in_t

	pand	$TMP0, $T0a
	pand	$TMP0, $T0b
	por	$T0a, $Ra
	pand	$TMP0, $T0c
	por	$T0b, $Rb
	pand	$TMP0, $T0d
	por	$T0c, $Rc
	prefetcht0	255($in_t)
	por	$T0d, $Rd

	dec	%rax
	jnz	.Lselect_loop_sse_w7

	movdqu	$Ra, 16*0($val)
	movdqu	$Rb, 16*1($val)
	movdqu	$Rc, 16*2($val)
	movdqu	$Rd, 16*3($val)
___
$code.=<<___	if ($win64);
	movaps	(%rsp), %xmm6
	movaps	0x10(%rsp), %xmm7
	movaps	0x20(%rsp), %xmm8
	movaps	0x30(%rsp), %xmm9
	movaps	0x40(%rsp), %xmm10
	movaps	0x50(%rsp), %xmm11
	movaps	0x60(%rsp), %xmm12
	movaps	0x70(%rsp), %xmm13
	movaps	0x80(%rsp), %xmm14
	movaps	0x90(%rsp), %xmm15
	lea	0xa8(%rsp), %rsp
___
$code.=<<___;
	ret
.cfi_endproc
.LSEH_end_ecp_nistz256_gather_w7:
.size	ecp_nistz256_gather_w7,.-ecp_nistz256_gather_w7
___
}
if ($avx>1) {
my ($val,$in_t,$index)=$win64?("%rcx","%rdx","%r8d"):("%rdi","%rsi","%edx");
my ($TWO,$INDEX,$Ra,$Rb,$Rc)=map("%ymm$_",(0..4));
my ($M0,$T0a,$T0b,$T0c,$TMP0)=map("%ymm$_",(5..9));
my ($M1,$T1a,$T1b,$T1c,$TMP1)=map("%ymm$_",(10..14));

$code.=<<___;
################################################################################
# void ecp_nistz256_avx2_gather_w5(uint64_t *val, uint64_t *in_t, int index);
.type	ecp_nistz256_avx2_gather_w5,\@abi-omnipotent
.align	32
ecp_nistz256_avx2_gather_w5:
.cfi_startproc
.Lavx2_gather_w5:
	vzeroupper
___
$code.=<<___	if ($win64);
	lea	-0x88(%rsp), %rax
	mov	%rsp,%r11
.LSEH_begin_ecp_nistz256_avx2_gather_w5:
	.byte	0x48,0x8d,0x60,0xe0		# lea	-0x20(%rax), %rsp
	.byte	0xc5,0xf8,0x29,0x70,0xe0	# vmovaps %xmm6, -0x20(%rax)
	.byte	0xc5,0xf8,0x29,0x78,0xf0	# vmovaps %xmm7, -0x10(%rax)
	.byte	0xc5,0x78,0x29,0x40,0x00	# vmovaps %xmm8, 8(%rax)
	.byte	0xc5,0x78,0x29,0x48,0x10	# vmovaps %xmm9, 0x10(%rax)
	.byte	0xc5,0x78,0x29,0x50,0x20	# vmovaps %xmm10, 0x20(%rax)
	.byte	0xc5,0x78,0x29,0x58,0x30	# vmovaps %xmm11, 0x30(%rax)
	.byte	0xc5,0x78,0x29,0x60,0x40	# vmovaps %xmm12, 0x40(%rax)
	.byte	0xc5,0x78,0x29,0x68,0x50	# vmovaps %xmm13, 0x50(%rax)
	.byte	0xc5,0x78,0x29,0x70,0x60	# vmovaps %xmm14, 0x60(%rax)
	.byte	0xc5,0x78,0x29,0x78,0x70	# vmovaps %xmm15, 0x70(%rax)
___
$code.=<<___;
	vmovdqa	.LTwo(%rip), $TWO

	vpxor	$Ra, $Ra, $Ra
	vpxor	$Rb, $Rb, $Rb
	vpxor	$Rc, $Rc, $Rc

	vmovdqa .LOne(%rip), $M0
	vmovdqa .LTwo(%rip), $M1

	vmovd	$index, %xmm1
	vpermd	$INDEX, $Ra, $INDEX

	mov	\$8, %rax
.Lselect_loop_avx2_w5:

	vmovdqa	32*0($in_t), $T0a
	vmovdqa	32*1($in_t), $T0b
	vmovdqa	32*2($in_t), $T0c

	vmovdqa	32*3($in_t), $T1a
	vmovdqa	32*4($in_t), $T1b
	vmovdqa	32*5($in_t), $T1c

	vpcmpeqd	$INDEX, $M0, $TMP0
	vpcmpeqd	$INDEX, $M1, $TMP1

	vpaddd	$TWO, $M0, $M0
	vpaddd	$TWO, $M1, $M1
	lea	32*6($in_t), $in_t

	vpand	$TMP0, $T0a, $T0a
	vpand	$TMP0, $T0b, $T0b
	vpand	$TMP0, $T0c, $T0c
	vpand	$TMP1, $T1a, $T1a
	vpand	$TMP1, $T1b, $T1b
	vpand	$TMP1, $T1c, $T1c

	vpxor	$T0a, $Ra, $Ra
	vpxor	$T0b, $Rb, $Rb
	vpxor	$T0c, $Rc, $Rc
	vpxor	$T1a, $Ra, $Ra
	vpxor	$T1b, $Rb, $Rb
	vpxor	$T1c, $Rc, $Rc

	dec %rax
	jnz .Lselect_loop_avx2_w5

	vmovdqu $Ra, 32*0($val)
	vmovdqu $Rb, 32*1($val)
	vmovdqu $Rc, 32*2($val)
	vzeroupper
___
$code.=<<___	if ($win64);
	movaps	(%rsp), %xmm6
	movaps	0x10(%rsp), %xmm7
	movaps	0x20(%rsp), %xmm8
	movaps	0x30(%rsp), %xmm9
	movaps	0x40(%rsp), %xmm10
	movaps	0x50(%rsp), %xmm11
	movaps	0x60(%rsp), %xmm12
	movaps	0x70(%rsp), %xmm13
	movaps	0x80(%rsp), %xmm14
	movaps	0x90(%rsp), %xmm15
	lea	(%r11), %rsp
___
$code.=<<___;
	ret
.cfi_endproc
.LSEH_end_ecp_nistz256_avx2_gather_w5:
.size	ecp_nistz256_avx2_gather_w5,.-ecp_nistz256_avx2_gather_w5
___
}
if ($avx>1) {
my ($val,$in_t,$index)=$win64?("%rcx","%rdx","%r8d"):("%rdi","%rsi","%edx");
my ($THREE,$INDEX,$Ra,$Rb)=map("%ymm$_",(0..3));
my ($M0,$T0a,$T0b,$TMP0)=map("%ymm$_",(4..7));
my ($M1,$T1a,$T1b,$TMP1)=map("%ymm$_",(8..11));
my ($M2,$T2a,$T2b,$TMP2)=map("%ymm$_",(12..15));

$code.=<<___;

################################################################################
# void ecp_nistz256_avx2_gather_w7(uint64_t *val, uint64_t *in_t, int index);
.globl	ecp_nistz256_avx2_gather_w7
.type	ecp_nistz256_avx2_gather_w7,\@abi-omnipotent
.align	32
ecp_nistz256_avx2_gather_w7:
.cfi_startproc
.Lavx2_gather_w7:
	vzeroupper
___
$code.=<<___	if ($win64);
	mov	%rsp,%r11
	lea	-0x88(%rsp), %rax
.LSEH_begin_ecp_nistz256_avx2_gather_w7:
	.byte	0x48,0x8d,0x60,0xe0		# lea	-0x20(%rax), %rsp
	.byte	0xc5,0xf8,0x29,0x70,0xe0	# vmovaps %xmm6, -0x20(%rax)
	.byte	0xc5,0xf8,0x29,0x78,0xf0	# vmovaps %xmm7, -0x10(%rax)
	.byte	0xc5,0x78,0x29,0x40,0x00	# vmovaps %xmm8, 8(%rax)
	.byte	0xc5,0x78,0x29,0x48,0x10	# vmovaps %xmm9, 0x10(%rax)
	.byte	0xc5,0x78,0x29,0x50,0x20	# vmovaps %xmm10, 0x20(%rax)
	.byte	0xc5,0x78,0x29,0x58,0x30	# vmovaps %xmm11, 0x30(%rax)
	.byte	0xc5,0x78,0x29,0x60,0x40	# vmovaps %xmm12, 0x40(%rax)
	.byte	0xc5,0x78,0x29,0x68,0x50	# vmovaps %xmm13, 0x50(%rax)
	.byte	0xc5,0x78,0x29,0x70,0x60	# vmovaps %xmm14, 0x60(%rax)
	.byte	0xc5,0x78,0x29,0x78,0x70	# vmovaps %xmm15, 0x70(%rax)
___
$code.=<<___;
	vmovdqa	.LThree(%rip), $THREE

	vpxor	$Ra, $Ra, $Ra
	vpxor	$Rb, $Rb, $Rb

	vmovdqa .LOne(%rip), $M0
	vmovdqa .LTwo(%rip), $M1
	vmovdqa .LThree(%rip), $M2

	vmovd	$index, %xmm1
	vpermd	$INDEX, $Ra, $INDEX
	# Skip index = 0, because it is implicitly the point at infinity

	mov	\$21, %rax
.Lselect_loop_avx2_w7:

	vmovdqa	32*0($in_t), $T0a
	vmovdqa	32*1($in_t), $T0b

	vmovdqa	32*2($in_t), $T1a
	vmovdqa	32*3($in_t), $T1b

	vmovdqa	32*4($in_t), $T2a
	vmovdqa	32*5($in_t), $T2b

	vpcmpeqd	$INDEX, $M0, $TMP0
	vpcmpeqd	$INDEX, $M1, $TMP1
	vpcmpeqd	$INDEX, $M2, $TMP2

	vpaddd	$THREE, $M0, $M0
	vpaddd	$THREE, $M1, $M1
	vpaddd	$THREE, $M2, $M2
	lea	32*6($in_t), $in_t

	vpand	$TMP0, $T0a, $T0a
	vpand	$TMP0, $T0b, $T0b
	vpand	$TMP1, $T1a, $T1a
	vpand	$TMP1, $T1b, $T1b
	vpand	$TMP2, $T2a, $T2a
	vpand	$TMP2, $T2b, $T2b

	vpxor	$T0a, $Ra, $Ra
	vpxor	$T0b, $Rb, $Rb
	vpxor	$T1a, $Ra, $Ra
	vpxor	$T1b, $Rb, $Rb
	vpxor	$T2a, $Ra, $Ra
	vpxor	$T2b, $Rb, $Rb

	dec %rax
	jnz .Lselect_loop_avx2_w7


	vmovdqa	32*0($in_t), $T0a
	vmovdqa	32*1($in_t), $T0b

	vpcmpeqd	$INDEX, $M0, $TMP0

	vpand	$TMP0, $T0a, $T0a
	vpand	$TMP0, $T0b, $T0b

	vpxor	$T0a, $Ra, $Ra
	vpxor	$T0b, $Rb, $Rb

	vmovdqu $Ra, 32*0($val)
	vmovdqu $Rb, 32*1($val)
	vzeroupper
___
$code.=<<___	if ($win64);
	movaps	(%rsp), %xmm6
	movaps	0x10(%rsp), %xmm7
	movaps	0x20(%rsp), %xmm8
	movaps	0x30(%rsp), %xmm9
	movaps	0x40(%rsp), %xmm10
	movaps	0x50(%rsp), %xmm11
	movaps	0x60(%rsp), %xmm12
	movaps	0x70(%rsp), %xmm13
	movaps	0x80(%rsp), %xmm14
	movaps	0x90(%rsp), %xmm15
	lea	(%r11), %rsp
___
$code.=<<___;
	ret
.cfi_endproc
.LSEH_end_ecp_nistz256_avx2_gather_w7:
.size	ecp_nistz256_avx2_gather_w7,.-ecp_nistz256_avx2_gather_w7
___
} else {
$code.=<<___;
.globl	ecp_nistz256_avx2_gather_w7
.type	ecp_nistz256_avx2_gather_w7,\@function,3
.align	32
ecp_nistz256_avx2_gather_w7:
	.byte	0x0f,0x0b	# ud2
	ret
.size	ecp_nistz256_avx2_gather_w7,.-ecp_nistz256_avx2_gather_w7
___
}
{{{
########################################################################
# This block implements higher level point_double, point_add and
# point_add_affine. The key to performance in this case is to allow
# out-of-order execution logic to overlap computations from next step
# with tail processing from current step. By using tailored calling
# sequence we minimize inter-step overhead to give processor better
# shot at overlapping operations...
#
# You will notice that input data is copied to stack. Trouble is that
# there are no registers to spare for holding original pointers and
# reloading them, pointers, would create undesired dependencies on
# effective addresses calculation paths. In other words it's too done
# to favour out-of-order execution logic.
#						<appro@openssl.org>

my ($r_ptr,$a_ptr,$b_org,$b_ptr)=("%rdi","%rsi","%rdx","%rbx");
my ($acc0,$acc1,$acc2,$acc3,$acc4,$acc5,$acc6,$acc7)=map("%r$_",(8..15));
my ($t0,$t1,$t2,$t3,$t4)=("%rax","%rbp","%rcx",$acc4,$acc4);
my ($poly1,$poly3)=($acc6,$acc7);

sub load_for_mul () {
my ($a,$b,$src0) = @_;
my $bias = $src0 eq "%rax" ? 0 : -128;

"	mov	$b, $src0
	lea	$b, $b_ptr
	mov	8*0+$a, $acc1
	mov	8*1+$a, $acc2
	lea	$bias+$a, $a_ptr
	mov	8*2+$a, $acc3
	mov	8*3+$a, $acc4"
}

sub load_for_sqr () {
my ($a,$src0) = @_;
my $bias = $src0 eq "%rax" ? 0 : -128;

"	mov	8*0+$a, $src0
	mov	8*1+$a, $acc6
	lea	$bias+$a, $a_ptr
	mov	8*2+$a, $acc7
	mov	8*3+$a, $acc0"
}

									{
########################################################################
# operate in 4-5-0-1 "name space" that matches multiplication output
#
my ($a0,$a1,$a2,$a3,$t3,$t4)=($acc4,$acc5,$acc0,$acc1,$acc2,$acc3);

$code.=<<___;
.type	__ecp_nistz256_add_toq,\@abi-omnipotent
.align	32
__ecp_nistz256_add_toq:
.cfi_startproc
	xor	$t4,$t4
	add	8*0($b_ptr), $a0
	adc	8*1($b_ptr), $a1
	 mov	$a0, $t0
	adc	8*2($b_ptr), $a2
	adc	8*3($b_ptr), $a3
	 mov	$a1, $t1
	adc	\$0, $t4

	sub	\$-1, $a0
	 mov	$a2, $t2
	sbb	$poly1, $a1
	sbb	\$0, $a2
	 mov	$a3, $t3
	sbb	$poly3, $a3
	sbb	\$0, $t4

	cmovc	$t0, $a0
	cmovc	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovc	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovc	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	ret
.cfi_endproc
.size	__ecp_nistz256_add_toq,.-__ecp_nistz256_add_toq

.type	__ecp_nistz256_sub_fromq,\@abi-omnipotent
.align	32
__ecp_nistz256_sub_fromq:
.cfi_startproc
	sub	8*0($b_ptr), $a0
	sbb	8*1($b_ptr), $a1
	 mov	$a0, $t0
	sbb	8*2($b_ptr), $a2
	sbb	8*3($b_ptr), $a3
	 mov	$a1, $t1
	sbb	$t4, $t4

	add	\$-1, $a0
	 mov	$a2, $t2
	adc	$poly1, $a1
	adc	\$0, $a2
	 mov	$a3, $t3
	adc	$poly3, $a3
	test	$t4, $t4

	cmovz	$t0, $a0
	cmovz	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovz	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovz	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	ret
.cfi_endproc
.size	__ecp_nistz256_sub_fromq,.-__ecp_nistz256_sub_fromq

.type	__ecp_nistz256_subq,\@abi-omnipotent
.align	32
__ecp_nistz256_subq:
.cfi_startproc
	sub	$a0, $t0
	sbb	$a1, $t1
	 mov	$t0, $a0
	sbb	$a2, $t2
	sbb	$a3, $t3
	 mov	$t1, $a1
	sbb	$t4, $t4

	add	\$-1, $t0
	 mov	$t2, $a2
	adc	$poly1, $t1
	adc	\$0, $t2
	 mov	$t3, $a3
	adc	$poly3, $t3
	test	$t4, $t4

	cmovnz	$t0, $a0
	cmovnz	$t1, $a1
	cmovnz	$t2, $a2
	cmovnz	$t3, $a3

	ret
.cfi_endproc
.size	__ecp_nistz256_subq,.-__ecp_nistz256_subq

.type	__ecp_nistz256_mul_by_2q,\@abi-omnipotent
.align	32
__ecp_nistz256_mul_by_2q:
.cfi_startproc
	xor	$t4, $t4
	add	$a0, $a0		# a0:a3+a0:a3
	adc	$a1, $a1
	 mov	$a0, $t0
	adc	$a2, $a2
	adc	$a3, $a3
	 mov	$a1, $t1
	adc	\$0, $t4

	sub	\$-1, $a0
	 mov	$a2, $t2
	sbb	$poly1, $a1
	sbb	\$0, $a2
	 mov	$a3, $t3
	sbb	$poly3, $a3
	sbb	\$0, $t4

	cmovc	$t0, $a0
	cmovc	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovc	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovc	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	ret
.cfi_endproc
.size	__ecp_nistz256_mul_by_2q,.-__ecp_nistz256_mul_by_2q
___
									}
sub gen_double () {
    my $x = shift;
    my ($src0,$sfx,$bias);
    my ($S,$M,$Zsqr,$in_x,$tmp0)=map(32*$_,(0..4));

    if ($x ne "x") {
	$src0 = "%rax";
	$sfx  = "";
	$bias = 0;

$code.=<<___;
.globl	ecp_nistz256_point_double
.type	ecp_nistz256_point_double,\@function,2
.align	32
ecp_nistz256_point_double:
.cfi_startproc
___
$code.=<<___	if ($addx);
	mov	\$0x80100, %ecx
	and	OPENSSL_ia32cap_P+8(%rip), %ecx
	cmp	\$0x80100, %ecx
	je	.Lpoint_doublex
___
    } else {
	$src0 = "%rdx";
	$sfx  = "x";
	$bias = 128;

$code.=<<___;
.type	ecp_nistz256_point_doublex,\@function,2
.align	32
ecp_nistz256_point_doublex:
.cfi_startproc
.Lpoint_doublex:
___
    }
$code.=<<___;
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	sub	\$32*5+8, %rsp
.cfi_adjust_cfa_offset	32*5+8
.Lpoint_double${x}_body:

.Lpoint_double_shortcut$x:
	movdqu	0x00($a_ptr), %xmm0		# copy	*(P256_POINT *)$a_ptr.x
	mov	$a_ptr, $b_ptr			# backup copy
	movdqu	0x10($a_ptr), %xmm1
	 mov	0x20+8*0($a_ptr), $acc4		# load in_y in "5-4-0-1" order
	 mov	0x20+8*1($a_ptr), $acc5
	 mov	0x20+8*2($a_ptr), $acc0
	 mov	0x20+8*3($a_ptr), $acc1
	 mov	.Lpoly+8*1(%rip), $poly1
	 mov	.Lpoly+8*3(%rip), $poly3
	movdqa	%xmm0, $in_x(%rsp)
	movdqa	%xmm1, $in_x+0x10(%rsp)
	lea	0x20($r_ptr), $acc2
	lea	0x40($r_ptr), $acc3
	movq	$r_ptr, %xmm0
	movq	$acc2, %xmm1
	movq	$acc3, %xmm2

	lea	$S(%rsp), $r_ptr
	call	__ecp_nistz256_mul_by_2$x	# p256_mul_by_2(S, in_y);

	mov	0x40+8*0($a_ptr), $src0
	mov	0x40+8*1($a_ptr), $acc6
	mov	0x40+8*2($a_ptr), $acc7
	mov	0x40+8*3($a_ptr), $acc0
	lea	0x40-$bias($a_ptr), $a_ptr
	lea	$Zsqr(%rsp), $r_ptr
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(Zsqr, in_z);

	`&load_for_sqr("$S(%rsp)", "$src0")`
	lea	$S(%rsp), $r_ptr
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(S, S);

	mov	0x20($b_ptr), $src0		# $b_ptr is still valid
	mov	0x40+8*0($b_ptr), $acc1
	mov	0x40+8*1($b_ptr), $acc2
	mov	0x40+8*2($b_ptr), $acc3
	mov	0x40+8*3($b_ptr), $acc4
	lea	0x40-$bias($b_ptr), $a_ptr
	lea	0x20($b_ptr), $b_ptr
	movq	%xmm2, $r_ptr
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(res_z, in_z, in_y);
	call	__ecp_nistz256_mul_by_2$x	# p256_mul_by_2(res_z, res_z);

	mov	$in_x+8*0(%rsp), $acc4		# "5-4-0-1" order
	mov	$in_x+8*1(%rsp), $acc5
	lea	$Zsqr(%rsp), $b_ptr
	mov	$in_x+8*2(%rsp), $acc0
	mov	$in_x+8*3(%rsp), $acc1
	lea	$M(%rsp), $r_ptr
	call	__ecp_nistz256_add_to$x		# p256_add(M, in_x, Zsqr);

	mov	$in_x+8*0(%rsp), $acc4		# "5-4-0-1" order
	mov	$in_x+8*1(%rsp), $acc5
	lea	$Zsqr(%rsp), $b_ptr
	mov	$in_x+8*2(%rsp), $acc0
	mov	$in_x+8*3(%rsp), $acc1
	lea	$Zsqr(%rsp), $r_ptr
	call	__ecp_nistz256_sub_from$x	# p256_sub(Zsqr, in_x, Zsqr);

	`&load_for_sqr("$S(%rsp)", "$src0")`
	movq	%xmm1, $r_ptr
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(res_y, S);
___
{
######## ecp_nistz256_div_by_2(res_y, res_y); ##########################
# operate in 4-5-6-7 "name space" that matches squaring output
#
my ($poly1,$poly3)=($a_ptr,$t1);
my ($a0,$a1,$a2,$a3,$t3,$t4,$t1)=($acc4,$acc5,$acc6,$acc7,$acc0,$acc1,$acc2);

$code.=<<___;
	xor	$t4, $t4
	mov	$a0, $t0
	add	\$-1, $a0
	mov	$a1, $t1
	adc	$poly1, $a1
	mov	$a2, $t2
	adc	\$0, $a2
	mov	$a3, $t3
	adc	$poly3, $a3
	adc	\$0, $t4
	xor	$a_ptr, $a_ptr		# borrow $a_ptr
	test	\$1, $t0

	cmovz	$t0, $a0
	cmovz	$t1, $a1
	cmovz	$t2, $a2
	cmovz	$t3, $a3
	cmovz	$a_ptr, $t4

	mov	$a1, $t0		# a0:a3>>1
	shr	\$1, $a0
	shl	\$63, $t0
	mov	$a2, $t1
	shr	\$1, $a1
	or	$t0, $a0
	shl	\$63, $t1
	mov	$a3, $t2
	shr	\$1, $a2
	or	$t1, $a1
	shl	\$63, $t2
	mov	$a0, 8*0($r_ptr)
	shr	\$1, $a3
	mov	$a1, 8*1($r_ptr)
	shl	\$63, $t4
	or	$t2, $a2
	or	$t4, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)
___
}
$code.=<<___;
	`&load_for_mul("$M(%rsp)", "$Zsqr(%rsp)", "$src0")`
	lea	$M(%rsp), $r_ptr
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(M, M, Zsqr);

	lea	$tmp0(%rsp), $r_ptr
	call	__ecp_nistz256_mul_by_2$x

	lea	$M(%rsp), $b_ptr
	lea	$M(%rsp), $r_ptr
	call	__ecp_nistz256_add_to$x		# p256_mul_by_3(M, M);

	`&load_for_mul("$S(%rsp)", "$in_x(%rsp)", "$src0")`
	lea	$S(%rsp), $r_ptr
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(S, S, in_x);

	lea	$tmp0(%rsp), $r_ptr
	call	__ecp_nistz256_mul_by_2$x	# p256_mul_by_2(tmp0, S);

	`&load_for_sqr("$M(%rsp)", "$src0")`
	movq	%xmm0, $r_ptr
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(res_x, M);

	lea	$tmp0(%rsp), $b_ptr
	mov	$acc6, $acc0			# harmonize sqr output and sub input
	mov	$acc7, $acc1
	mov	$a_ptr, $poly1
	mov	$t1, $poly3
	call	__ecp_nistz256_sub_from$x	# p256_sub(res_x, res_x, tmp0);

	mov	$S+8*0(%rsp), $t0
	mov	$S+8*1(%rsp), $t1
	mov	$S+8*2(%rsp), $t2
	mov	$S+8*3(%rsp), $acc2		# "4-5-0-1" order
	lea	$S(%rsp), $r_ptr
	call	__ecp_nistz256_sub$x		# p256_sub(S, S, res_x);

	mov	$M(%rsp), $src0
	lea	$M(%rsp), $b_ptr
	mov	$acc4, $acc6			# harmonize sub output and mul input
	xor	%ecx, %ecx
	mov	$acc4, $S+8*0(%rsp)		# have to save:-(
	mov	$acc5, $acc2
	mov	$acc5, $S+8*1(%rsp)
	cmovz	$acc0, $acc3
	mov	$acc0, $S+8*2(%rsp)
	lea	$S-$bias(%rsp), $a_ptr
	cmovz	$acc1, $acc4
	mov	$acc1, $S+8*3(%rsp)
	mov	$acc6, $acc1
	lea	$S(%rsp), $r_ptr
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(S, S, M);

	movq	%xmm1, $b_ptr
	movq	%xmm1, $r_ptr
	call	__ecp_nistz256_sub_from$x	# p256_sub(res_y, S, res_y);

	lea	32*5+56(%rsp), %rsi
.cfi_def_cfa	%rsi,8
	mov	-48(%rsi),%r15
.cfi_restore	%r15
	mov	-40(%rsi),%r14
.cfi_restore	%r14
	mov	-32(%rsi),%r13
.cfi_restore	%r13
	mov	-24(%rsi),%r12
.cfi_restore	%r12
	mov	-16(%rsi),%rbx
.cfi_restore	%rbx
	mov	-8(%rsi),%rbp
.cfi_restore	%rbp
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lpoint_double${x}_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_point_double$sfx,.-ecp_nistz256_point_double$sfx
___
}
&gen_double("q");

sub gen_add () {
    my $x = shift;
    my ($src0,$sfx,$bias);
    my ($H,$Hsqr,$R,$Rsqr,$Hcub,
	$U1,$U2,$S1,$S2,
	$res_x,$res_y,$res_z,
	$in1_x,$in1_y,$in1_z,
	$in2_x,$in2_y,$in2_z)=map(32*$_,(0..17));
    my ($Z1sqr, $Z2sqr) = ($Hsqr, $Rsqr);

    if ($x ne "x") {
	$src0 = "%rax";
	$sfx  = "";
	$bias = 0;

$code.=<<___;
.globl	ecp_nistz256_point_add
.type	ecp_nistz256_point_add,\@function,3
.align	32
ecp_nistz256_point_add:
.cfi_startproc
___
$code.=<<___	if ($addx);
	mov	\$0x80100, %ecx
	and	OPENSSL_ia32cap_P+8(%rip), %ecx
	cmp	\$0x80100, %ecx
	je	.Lpoint_addx
___
    } else {
	$src0 = "%rdx";
	$sfx  = "x";
	$bias = 128;

$code.=<<___;
.type	ecp_nistz256_point_addx,\@function,3
.align	32
ecp_nistz256_point_addx:
.cfi_startproc
.Lpoint_addx:
___
    }
$code.=<<___;
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	sub	\$32*18+8, %rsp
.cfi_adjust_cfa_offset	32*18+8
.Lpoint_add${x}_body:

	movdqu	0x00($a_ptr), %xmm0		# copy	*(P256_POINT *)$a_ptr
	movdqu	0x10($a_ptr), %xmm1
	movdqu	0x20($a_ptr), %xmm2
	movdqu	0x30($a_ptr), %xmm3
	movdqu	0x40($a_ptr), %xmm4
	movdqu	0x50($a_ptr), %xmm5
	mov	$a_ptr, $b_ptr			# reassign
	mov	$b_org, $a_ptr			# reassign
	movdqa	%xmm0, $in1_x(%rsp)
	movdqa	%xmm1, $in1_x+0x10(%rsp)
	movdqa	%xmm2, $in1_y(%rsp)
	movdqa	%xmm3, $in1_y+0x10(%rsp)
	movdqa	%xmm4, $in1_z(%rsp)
	movdqa	%xmm5, $in1_z+0x10(%rsp)
	por	%xmm4, %xmm5

	movdqu	0x00($a_ptr), %xmm0		# copy	*(P256_POINT *)$b_ptr
	 pshufd	\$0xb1, %xmm5, %xmm3
	movdqu	0x10($a_ptr), %xmm1
	movdqu	0x20($a_ptr), %xmm2
	 por	%xmm3, %xmm5
	movdqu	0x30($a_ptr), %xmm3
	 mov	0x40+8*0($a_ptr), $src0		# load original in2_z
	 mov	0x40+8*1($a_ptr), $acc6
	 mov	0x40+8*2($a_ptr), $acc7
	 mov	0x40+8*3($a_ptr), $acc0
	movdqa	%xmm0, $in2_x(%rsp)
	 pshufd	\$0x1e, %xmm5, %xmm4
	movdqa	%xmm1, $in2_x+0x10(%rsp)
	movdqu	0x40($a_ptr),%xmm0		# in2_z again
	movdqu	0x50($a_ptr),%xmm1
	movdqa	%xmm2, $in2_y(%rsp)
	movdqa	%xmm3, $in2_y+0x10(%rsp)
	 por	%xmm4, %xmm5
	 pxor	%xmm4, %xmm4
	por	%xmm0, %xmm1
	 movq	$r_ptr, %xmm0			# save $r_ptr

	lea	0x40-$bias($a_ptr), $a_ptr	# $a_ptr is still valid
	 mov	$src0, $in2_z+8*0(%rsp)		# make in2_z copy
	 mov	$acc6, $in2_z+8*1(%rsp)
	 mov	$acc7, $in2_z+8*2(%rsp)
	 mov	$acc0, $in2_z+8*3(%rsp)
	lea	$Z2sqr(%rsp), $r_ptr		# Z2^2
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(Z2sqr, in2_z);

	pcmpeqd	%xmm4, %xmm5
	pshufd	\$0xb1, %xmm1, %xmm4
	por	%xmm1, %xmm4
	pshufd	\$0, %xmm5, %xmm5		# in1infty
	pshufd	\$0x1e, %xmm4, %xmm3
	por	%xmm3, %xmm4
	pxor	%xmm3, %xmm3
	pcmpeqd	%xmm3, %xmm4
	pshufd	\$0, %xmm4, %xmm4		# in2infty
	 mov	0x40+8*0($b_ptr), $src0		# load original in1_z
	 mov	0x40+8*1($b_ptr), $acc6
	 mov	0x40+8*2($b_ptr), $acc7
	 mov	0x40+8*3($b_ptr), $acc0
	movq	$b_ptr, %xmm1

	lea	0x40-$bias($b_ptr), $a_ptr
	lea	$Z1sqr(%rsp), $r_ptr		# Z1^2
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(Z1sqr, in1_z);

	`&load_for_mul("$Z2sqr(%rsp)", "$in2_z(%rsp)", "$src0")`
	lea	$S1(%rsp), $r_ptr		# S1 = Z2^3
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(S1, Z2sqr, in2_z);

	`&load_for_mul("$Z1sqr(%rsp)", "$in1_z(%rsp)", "$src0")`
	lea	$S2(%rsp), $r_ptr		# S2 = Z1^3
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(S2, Z1sqr, in1_z);

	`&load_for_mul("$S1(%rsp)", "$in1_y(%rsp)", "$src0")`
	lea	$S1(%rsp), $r_ptr		# S1 = Y1*Z2^3
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(S1, S1, in1_y);

	`&load_for_mul("$S2(%rsp)", "$in2_y(%rsp)", "$src0")`
	lea	$S2(%rsp), $r_ptr		# S2 = Y2*Z1^3
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(S2, S2, in2_y);

	lea	$S1(%rsp), $b_ptr
	lea	$R(%rsp), $r_ptr		# R = S2 - S1
	call	__ecp_nistz256_sub_from$x	# p256_sub(R, S2, S1);

	or	$acc5, $acc4			# see if result is zero
	movdqa	%xmm4, %xmm2
	or	$acc0, $acc4
	or	$acc1, $acc4
	por	%xmm5, %xmm2			# in1infty || in2infty
	movq	$acc4, %xmm3

	`&load_for_mul("$Z2sqr(%rsp)", "$in1_x(%rsp)", "$src0")`
	lea	$U1(%rsp), $r_ptr		# U1 = X1*Z2^2
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(U1, in1_x, Z2sqr);

	`&load_for_mul("$Z1sqr(%rsp)", "$in2_x(%rsp)", "$src0")`
	lea	$U2(%rsp), $r_ptr		# U2 = X2*Z1^2
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(U2, in2_x, Z1sqr);

	lea	$U1(%rsp), $b_ptr
	lea	$H(%rsp), $r_ptr		# H = U2 - U1
	call	__ecp_nistz256_sub_from$x	# p256_sub(H, U2, U1);

	or	$acc5, $acc4			# see if result is zero
	or	$acc0, $acc4
	or	$acc1, $acc4

	.byte	0x3e				# predict taken
	jnz	.Ladd_proceed$x			# is_equal(U1,U2)?
	movq	%xmm2, $acc0
	movq	%xmm3, $acc1
	test	$acc0, $acc0
	jnz	.Ladd_proceed$x			# (in1infty || in2infty)?
	test	$acc1, $acc1
	jz	.Ladd_double$x			# is_equal(S1,S2)?

	movq	%xmm0, $r_ptr			# restore $r_ptr
	pxor	%xmm0, %xmm0
	movdqu	%xmm0, 0x00($r_ptr)
	movdqu	%xmm0, 0x10($r_ptr)
	movdqu	%xmm0, 0x20($r_ptr)
	movdqu	%xmm0, 0x30($r_ptr)
	movdqu	%xmm0, 0x40($r_ptr)
	movdqu	%xmm0, 0x50($r_ptr)
	jmp	.Ladd_done$x

.align	32
.Ladd_double$x:
	movq	%xmm1, $a_ptr			# restore $a_ptr
	movq	%xmm0, $r_ptr			# restore $r_ptr
	add	\$`32*(18-5)`, %rsp		# difference in frame sizes
.cfi_adjust_cfa_offset	`-32*(18-5)`
	jmp	.Lpoint_double_shortcut$x
.cfi_adjust_cfa_offset	`32*(18-5)`

.align	32
.Ladd_proceed$x:
	`&load_for_sqr("$R(%rsp)", "$src0")`
	lea	$Rsqr(%rsp), $r_ptr		# R^2
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(Rsqr, R);

	`&load_for_mul("$H(%rsp)", "$in1_z(%rsp)", "$src0")`
	lea	$res_z(%rsp), $r_ptr		# Z3 = H*Z1*Z2
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(res_z, H, in1_z);

	`&load_for_sqr("$H(%rsp)", "$src0")`
	lea	$Hsqr(%rsp), $r_ptr		# H^2
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(Hsqr, H);

	`&load_for_mul("$res_z(%rsp)", "$in2_z(%rsp)", "$src0")`
	lea	$res_z(%rsp), $r_ptr		# Z3 = H*Z1*Z2
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(res_z, res_z, in2_z);

	`&load_for_mul("$Hsqr(%rsp)", "$H(%rsp)", "$src0")`
	lea	$Hcub(%rsp), $r_ptr		# H^3
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(Hcub, Hsqr, H);

	`&load_for_mul("$Hsqr(%rsp)", "$U1(%rsp)", "$src0")`
	lea	$U2(%rsp), $r_ptr		# U1*H^2
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(U2, U1, Hsqr);
___
{
#######################################################################
# operate in 4-5-0-1 "name space" that matches multiplication output
#
my ($acc0,$acc1,$acc2,$acc3,$t3,$t4)=($acc4,$acc5,$acc0,$acc1,$acc2,$acc3);
my ($poly1, $poly3)=($acc6,$acc7);

$code.=<<___;
	#lea	$U2(%rsp), $a_ptr
	#lea	$Hsqr(%rsp), $r_ptr	# 2*U1*H^2
	#call	__ecp_nistz256_mul_by_2	# ecp_nistz256_mul_by_2(Hsqr, U2);

	xor	$t4, $t4
	add	$acc0, $acc0		# a0:a3+a0:a3
	lea	$Rsqr(%rsp), $a_ptr
	adc	$acc1, $acc1
	 mov	$acc0, $t0
	adc	$acc2, $acc2
	adc	$acc3, $acc3
	 mov	$acc1, $t1
	adc	\$0, $t4

	sub	\$-1, $acc0
	 mov	$acc2, $t2
	sbb	$poly1, $acc1
	sbb	\$0, $acc2
	 mov	$acc3, $t3
	sbb	$poly3, $acc3
	sbb	\$0, $t4

	cmovc	$t0, $acc0
	mov	8*0($a_ptr), $t0
	cmovc	$t1, $acc1
	mov	8*1($a_ptr), $t1
	cmovc	$t2, $acc2
	mov	8*2($a_ptr), $t2
	cmovc	$t3, $acc3
	mov	8*3($a_ptr), $t3

	call	__ecp_nistz256_sub$x		# p256_sub(res_x, Rsqr, Hsqr);

	lea	$Hcub(%rsp), $b_ptr
	lea	$res_x(%rsp), $r_ptr
	call	__ecp_nistz256_sub_from$x	# p256_sub(res_x, res_x, Hcub);

	mov	$U2+8*0(%rsp), $t0
	mov	$U2+8*1(%rsp), $t1
	mov	$U2+8*2(%rsp), $t2
	mov	$U2+8*3(%rsp), $t3
	lea	$res_y(%rsp), $r_ptr

	call	__ecp_nistz256_sub$x		# p256_sub(res_y, U2, res_x);

	mov	$acc0, 8*0($r_ptr)		# save the result, as
	mov	$acc1, 8*1($r_ptr)		# __ecp_nistz256_sub doesn't
	mov	$acc2, 8*2($r_ptr)
	mov	$acc3, 8*3($r_ptr)
___
}
$code.=<<___;
	`&load_for_mul("$S1(%rsp)", "$Hcub(%rsp)", "$src0")`
	lea	$S2(%rsp), $r_ptr
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(S2, S1, Hcub);

	`&load_for_mul("$R(%rsp)", "$res_y(%rsp)", "$src0")`
	lea	$res_y(%rsp), $r_ptr
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(res_y, R, res_y);

	lea	$S2(%rsp), $b_ptr
	lea	$res_y(%rsp), $r_ptr
	call	__ecp_nistz256_sub_from$x	# p256_sub(res_y, res_y, S2);

	movq	%xmm0, $r_ptr		# restore $r_ptr

	movdqa	%xmm5, %xmm0		# copy_conditional(res_z, in2_z, in1infty);
	movdqa	%xmm5, %xmm1
	pandn	$res_z(%rsp), %xmm0
	movdqa	%xmm5, %xmm2
	pandn	$res_z+0x10(%rsp), %xmm1
	movdqa	%xmm5, %xmm3
	pand	$in2_z(%rsp), %xmm2
	pand	$in2_z+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3

	movdqa	%xmm4, %xmm0		# copy_conditional(res_z, in1_z, in2infty);
	movdqa	%xmm4, %xmm1
	pandn	%xmm2, %xmm0
	movdqa	%xmm4, %xmm2
	pandn	%xmm3, %xmm1
	movdqa	%xmm4, %xmm3
	pand	$in1_z(%rsp), %xmm2
	pand	$in1_z+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3
	movdqu	%xmm2, 0x40($r_ptr)
	movdqu	%xmm3, 0x50($r_ptr)

	movdqa	%xmm5, %xmm0		# copy_conditional(res_x, in2_x, in1infty);
	movdqa	%xmm5, %xmm1
	pandn	$res_x(%rsp), %xmm0
	movdqa	%xmm5, %xmm2
	pandn	$res_x+0x10(%rsp), %xmm1
	movdqa	%xmm5, %xmm3
	pand	$in2_x(%rsp), %xmm2
	pand	$in2_x+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3

	movdqa	%xmm4, %xmm0		# copy_conditional(res_x, in1_x, in2infty);
	movdqa	%xmm4, %xmm1
	pandn	%xmm2, %xmm0
	movdqa	%xmm4, %xmm2
	pandn	%xmm3, %xmm1
	movdqa	%xmm4, %xmm3
	pand	$in1_x(%rsp), %xmm2
	pand	$in1_x+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3
	movdqu	%xmm2, 0x00($r_ptr)
	movdqu	%xmm3, 0x10($r_ptr)

	movdqa	%xmm5, %xmm0		# copy_conditional(res_y, in2_y, in1infty);
	movdqa	%xmm5, %xmm1
	pandn	$res_y(%rsp), %xmm0
	movdqa	%xmm5, %xmm2
	pandn	$res_y+0x10(%rsp), %xmm1
	movdqa	%xmm5, %xmm3
	pand	$in2_y(%rsp), %xmm2
	pand	$in2_y+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3

	movdqa	%xmm4, %xmm0		# copy_conditional(res_y, in1_y, in2infty);
	movdqa	%xmm4, %xmm1
	pandn	%xmm2, %xmm0
	movdqa	%xmm4, %xmm2
	pandn	%xmm3, %xmm1
	movdqa	%xmm4, %xmm3
	pand	$in1_y(%rsp), %xmm2
	pand	$in1_y+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3
	movdqu	%xmm2, 0x20($r_ptr)
	movdqu	%xmm3, 0x30($r_ptr)

.Ladd_done$x:
	lea	32*18+56(%rsp), %rsi
.cfi_def_cfa	%rsi,8
	mov	-48(%rsi),%r15
.cfi_restore	%r15
	mov	-40(%rsi),%r14
.cfi_restore	%r14
	mov	-32(%rsi),%r13
.cfi_restore	%r13
	mov	-24(%rsi),%r12
.cfi_restore	%r12
	mov	-16(%rsi),%rbx
.cfi_restore	%rbx
	mov	-8(%rsi),%rbp
.cfi_restore	%rbp
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lpoint_add${x}_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_point_add$sfx,.-ecp_nistz256_point_add$sfx
___
}
&gen_add("q");

sub gen_add_affine () {
    my $x = shift;
    my ($src0,$sfx,$bias);
    my ($U2,$S2,$H,$R,$Hsqr,$Hcub,$Rsqr,
	$res_x,$res_y,$res_z,
	$in1_x,$in1_y,$in1_z,
	$in2_x,$in2_y)=map(32*$_,(0..14));
    my $Z1sqr = $S2;

    if ($x ne "x") {
	$src0 = "%rax";
	$sfx  = "";
	$bias = 0;

$code.=<<___;
.globl	ecp_nistz256_point_add_affine
.type	ecp_nistz256_point_add_affine,\@function,3
.align	32
ecp_nistz256_point_add_affine:
.cfi_startproc
___
$code.=<<___	if ($addx);
	mov	\$0x80100, %ecx
	and	OPENSSL_ia32cap_P+8(%rip), %ecx
	cmp	\$0x80100, %ecx
	je	.Lpoint_add_affinex
___
    } else {
	$src0 = "%rdx";
	$sfx  = "x";
	$bias = 128;

$code.=<<___;
.type	ecp_nistz256_point_add_affinex,\@function,3
.align	32
ecp_nistz256_point_add_affinex:
.cfi_startproc
.Lpoint_add_affinex:
___
    }
$code.=<<___;
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	sub	\$32*15+8, %rsp
.cfi_adjust_cfa_offset	32*15+8
.Ladd_affine${x}_body:

	movdqu	0x00($a_ptr), %xmm0	# copy	*(P256_POINT *)$a_ptr
	mov	$b_org, $b_ptr		# reassign
	movdqu	0x10($a_ptr), %xmm1
	movdqu	0x20($a_ptr), %xmm2
	movdqu	0x30($a_ptr), %xmm3
	movdqu	0x40($a_ptr), %xmm4
	movdqu	0x50($a_ptr), %xmm5
	 mov	0x40+8*0($a_ptr), $src0	# load original in1_z
	 mov	0x40+8*1($a_ptr), $acc6
	 mov	0x40+8*2($a_ptr), $acc7
	 mov	0x40+8*3($a_ptr), $acc0
	movdqa	%xmm0, $in1_x(%rsp)
	movdqa	%xmm1, $in1_x+0x10(%rsp)
	movdqa	%xmm2, $in1_y(%rsp)
	movdqa	%xmm3, $in1_y+0x10(%rsp)
	movdqa	%xmm4, $in1_z(%rsp)
	movdqa	%xmm5, $in1_z+0x10(%rsp)
	por	%xmm4, %xmm5

	movdqu	0x00($b_ptr), %xmm0	# copy	*(P256_POINT_AFFINE *)$b_ptr
	 pshufd	\$0xb1, %xmm5, %xmm3
	movdqu	0x10($b_ptr), %xmm1
	movdqu	0x20($b_ptr), %xmm2
	 por	%xmm3, %xmm5
	movdqu	0x30($b_ptr), %xmm3
	movdqa	%xmm0, $in2_x(%rsp)
	 pshufd	\$0x1e, %xmm5, %xmm4
	movdqa	%xmm1, $in2_x+0x10(%rsp)
	por	%xmm0, %xmm1
	 movq	$r_ptr, %xmm0		# save $r_ptr
	movdqa	%xmm2, $in2_y(%rsp)
	movdqa	%xmm3, $in2_y+0x10(%rsp)
	por	%xmm2, %xmm3
	 por	%xmm4, %xmm5
	 pxor	%xmm4, %xmm4
	por	%xmm1, %xmm3

	lea	0x40-$bias($a_ptr), $a_ptr	# $a_ptr is still valid
	lea	$Z1sqr(%rsp), $r_ptr		# Z1^2
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(Z1sqr, in1_z);

	pcmpeqd	%xmm4, %xmm5
	pshufd	\$0xb1, %xmm3, %xmm4
	 mov	0x00($b_ptr), $src0		# $b_ptr is still valid
	 #lea	0x00($b_ptr), $b_ptr
	 mov	$acc4, $acc1			# harmonize sqr output and mul input
	por	%xmm3, %xmm4
	pshufd	\$0, %xmm5, %xmm5		# in1infty
	pshufd	\$0x1e, %xmm4, %xmm3
	 mov	$acc5, $acc2
	por	%xmm3, %xmm4
	pxor	%xmm3, %xmm3
	 mov	$acc6, $acc3
	pcmpeqd	%xmm3, %xmm4
	pshufd	\$0, %xmm4, %xmm4		# in2infty

	lea	$Z1sqr-$bias(%rsp), $a_ptr
	mov	$acc7, $acc4
	lea	$U2(%rsp), $r_ptr		# U2 = X2*Z1^2
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(U2, Z1sqr, in2_x);

	lea	$in1_x(%rsp), $b_ptr
	lea	$H(%rsp), $r_ptr		# H = U2 - U1
	call	__ecp_nistz256_sub_from$x	# p256_sub(H, U2, in1_x);

	`&load_for_mul("$Z1sqr(%rsp)", "$in1_z(%rsp)", "$src0")`
	lea	$S2(%rsp), $r_ptr		# S2 = Z1^3
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(S2, Z1sqr, in1_z);

	`&load_for_mul("$H(%rsp)", "$in1_z(%rsp)", "$src0")`
	lea	$res_z(%rsp), $r_ptr		# Z3 = H*Z1*Z2
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(res_z, H, in1_z);

	`&load_for_mul("$S2(%rsp)", "$in2_y(%rsp)", "$src0")`
	lea	$S2(%rsp), $r_ptr		# S2 = Y2*Z1^3
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(S2, S2, in2_y);

	lea	$in1_y(%rsp), $b_ptr
	lea	$R(%rsp), $r_ptr		# R = S2 - S1
	call	__ecp_nistz256_sub_from$x	# p256_sub(R, S2, in1_y);

	`&load_for_sqr("$H(%rsp)", "$src0")`
	lea	$Hsqr(%rsp), $r_ptr		# H^2
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(Hsqr, H);

	`&load_for_sqr("$R(%rsp)", "$src0")`
	lea	$Rsqr(%rsp), $r_ptr		# R^2
	call	__ecp_nistz256_sqr_mont$x	# p256_sqr_mont(Rsqr, R);

	`&load_for_mul("$H(%rsp)", "$Hsqr(%rsp)", "$src0")`
	lea	$Hcub(%rsp), $r_ptr		# H^3
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(Hcub, Hsqr, H);

	`&load_for_mul("$Hsqr(%rsp)", "$in1_x(%rsp)", "$src0")`
	lea	$U2(%rsp), $r_ptr		# U1*H^2
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(U2, in1_x, Hsqr);
___
{
#######################################################################
# operate in 4-5-0-1 "name space" that matches multiplication output
#
my ($acc0,$acc1,$acc2,$acc3,$t3,$t4)=($acc4,$acc5,$acc0,$acc1,$acc2,$acc3);
my ($poly1, $poly3)=($acc6,$acc7);

$code.=<<___;
	#lea	$U2(%rsp), $a_ptr
	#lea	$Hsqr(%rsp), $r_ptr	# 2*U1*H^2
	#call	__ecp_nistz256_mul_by_2	# ecp_nistz256_mul_by_2(Hsqr, U2);

	xor	$t4, $t4
	add	$acc0, $acc0		# a0:a3+a0:a3
	lea	$Rsqr(%rsp), $a_ptr
	adc	$acc1, $acc1
	 mov	$acc0, $t0
	adc	$acc2, $acc2
	adc	$acc3, $acc3
	 mov	$acc1, $t1
	adc	\$0, $t4

	sub	\$-1, $acc0
	 mov	$acc2, $t2
	sbb	$poly1, $acc1
	sbb	\$0, $acc2
	 mov	$acc3, $t3
	sbb	$poly3, $acc3
	sbb	\$0, $t4

	cmovc	$t0, $acc0
	mov	8*0($a_ptr), $t0
	cmovc	$t1, $acc1
	mov	8*1($a_ptr), $t1
	cmovc	$t2, $acc2
	mov	8*2($a_ptr), $t2
	cmovc	$t3, $acc3
	mov	8*3($a_ptr), $t3

	call	__ecp_nistz256_sub$x		# p256_sub(res_x, Rsqr, Hsqr);

	lea	$Hcub(%rsp), $b_ptr
	lea	$res_x(%rsp), $r_ptr
	call	__ecp_nistz256_sub_from$x	# p256_sub(res_x, res_x, Hcub);

	mov	$U2+8*0(%rsp), $t0
	mov	$U2+8*1(%rsp), $t1
	mov	$U2+8*2(%rsp), $t2
	mov	$U2+8*3(%rsp), $t3
	lea	$H(%rsp), $r_ptr

	call	__ecp_nistz256_sub$x		# p256_sub(H, U2, res_x);

	mov	$acc0, 8*0($r_ptr)		# save the result, as
	mov	$acc1, 8*1($r_ptr)		# __ecp_nistz256_sub doesn't
	mov	$acc2, 8*2($r_ptr)
	mov	$acc3, 8*3($r_ptr)
___
}
$code.=<<___;
	`&load_for_mul("$Hcub(%rsp)", "$in1_y(%rsp)", "$src0")`
	lea	$S2(%rsp), $r_ptr
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(S2, Hcub, in1_y);

	`&load_for_mul("$H(%rsp)", "$R(%rsp)", "$src0")`
	lea	$H(%rsp), $r_ptr
	call	__ecp_nistz256_mul_mont$x	# p256_mul_mont(H, H, R);

	lea	$S2(%rsp), $b_ptr
	lea	$res_y(%rsp), $r_ptr
	call	__ecp_nistz256_sub_from$x	# p256_sub(res_y, H, S2);

	movq	%xmm0, $r_ptr		# restore $r_ptr

	movdqa	%xmm5, %xmm0		# copy_conditional(res_z, ONE, in1infty);
	movdqa	%xmm5, %xmm1
	pandn	$res_z(%rsp), %xmm0
	movdqa	%xmm5, %xmm2
	pandn	$res_z+0x10(%rsp), %xmm1
	movdqa	%xmm5, %xmm3
	pand	.LONE_mont(%rip), %xmm2
	pand	.LONE_mont+0x10(%rip), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3

	movdqa	%xmm4, %xmm0		# copy_conditional(res_z, in1_z, in2infty);
	movdqa	%xmm4, %xmm1
	pandn	%xmm2, %xmm0
	movdqa	%xmm4, %xmm2
	pandn	%xmm3, %xmm1
	movdqa	%xmm4, %xmm3
	pand	$in1_z(%rsp), %xmm2
	pand	$in1_z+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3
	movdqu	%xmm2, 0x40($r_ptr)
	movdqu	%xmm3, 0x50($r_ptr)

	movdqa	%xmm5, %xmm0		# copy_conditional(res_x, in2_x, in1infty);
	movdqa	%xmm5, %xmm1
	pandn	$res_x(%rsp), %xmm0
	movdqa	%xmm5, %xmm2
	pandn	$res_x+0x10(%rsp), %xmm1
	movdqa	%xmm5, %xmm3
	pand	$in2_x(%rsp), %xmm2
	pand	$in2_x+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3

	movdqa	%xmm4, %xmm0		# copy_conditional(res_x, in1_x, in2infty);
	movdqa	%xmm4, %xmm1
	pandn	%xmm2, %xmm0
	movdqa	%xmm4, %xmm2
	pandn	%xmm3, %xmm1
	movdqa	%xmm4, %xmm3
	pand	$in1_x(%rsp), %xmm2
	pand	$in1_x+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3
	movdqu	%xmm2, 0x00($r_ptr)
	movdqu	%xmm3, 0x10($r_ptr)

	movdqa	%xmm5, %xmm0		# copy_conditional(res_y, in2_y, in1infty);
	movdqa	%xmm5, %xmm1
	pandn	$res_y(%rsp), %xmm0
	movdqa	%xmm5, %xmm2
	pandn	$res_y+0x10(%rsp), %xmm1
	movdqa	%xmm5, %xmm3
	pand	$in2_y(%rsp), %xmm2
	pand	$in2_y+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3

	movdqa	%xmm4, %xmm0		# copy_conditional(res_y, in1_y, in2infty);
	movdqa	%xmm4, %xmm1
	pandn	%xmm2, %xmm0
	movdqa	%xmm4, %xmm2
	pandn	%xmm3, %xmm1
	movdqa	%xmm4, %xmm3
	pand	$in1_y(%rsp), %xmm2
	pand	$in1_y+0x10(%rsp), %xmm3
	por	%xmm0, %xmm2
	por	%xmm1, %xmm3
	movdqu	%xmm2, 0x20($r_ptr)
	movdqu	%xmm3, 0x30($r_ptr)

	lea	32*15+56(%rsp), %rsi
.cfi_def_cfa	%rsi,8
	mov	-48(%rsi),%r15
.cfi_restore	%r15
	mov	-40(%rsi),%r14
.cfi_restore	%r14
	mov	-32(%rsi),%r13
.cfi_restore	%r13
	mov	-24(%rsi),%r12
.cfi_restore	%r12
	mov	-16(%rsi),%rbx
.cfi_restore	%rbx
	mov	-8(%rsi),%rbp
.cfi_restore	%rbp
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Ladd_affine${x}_epilogue:
	ret
.cfi_endproc
.size	ecp_nistz256_point_add_affine$sfx,.-ecp_nistz256_point_add_affine$sfx
___
}
&gen_add_affine("q");

########################################################################
# AD*X magic
#
if ($addx) {								{
########################################################################
# operate in 4-5-0-1 "name space" that matches multiplication output
#
my ($a0,$a1,$a2,$a3,$t3,$t4)=($acc4,$acc5,$acc0,$acc1,$acc2,$acc3);

$code.=<<___;
.type	__ecp_nistz256_add_tox,\@abi-omnipotent
.align	32
__ecp_nistz256_add_tox:
.cfi_startproc
	xor	$t4, $t4
	adc	8*0($b_ptr), $a0
	adc	8*1($b_ptr), $a1
	 mov	$a0, $t0
	adc	8*2($b_ptr), $a2
	adc	8*3($b_ptr), $a3
	 mov	$a1, $t1
	adc	\$0, $t4

	xor	$t3, $t3
	sbb	\$-1, $a0
	 mov	$a2, $t2
	sbb	$poly1, $a1
	sbb	\$0, $a2
	 mov	$a3, $t3
	sbb	$poly3, $a3
	sbb	\$0, $t4

	cmovc	$t0, $a0
	cmovc	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovc	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovc	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	ret
.cfi_endproc
.size	__ecp_nistz256_add_tox,.-__ecp_nistz256_add_tox

.type	__ecp_nistz256_sub_fromx,\@abi-omnipotent
.align	32
__ecp_nistz256_sub_fromx:
.cfi_startproc
	xor	$t4, $t4
	sbb	8*0($b_ptr), $a0
	sbb	8*1($b_ptr), $a1
	 mov	$a0, $t0
	sbb	8*2($b_ptr), $a2
	sbb	8*3($b_ptr), $a3
	 mov	$a1, $t1
	sbb	\$0, $t4

	xor	$t3, $t3
	adc	\$-1, $a0
	 mov	$a2, $t2
	adc	$poly1, $a1
	adc	\$0, $a2
	 mov	$a3, $t3
	adc	$poly3, $a3

	bt	\$0, $t4
	cmovnc	$t0, $a0
	cmovnc	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovnc	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovnc	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	ret
.cfi_endproc
.size	__ecp_nistz256_sub_fromx,.-__ecp_nistz256_sub_fromx

.type	__ecp_nistz256_subx,\@abi-omnipotent
.align	32
__ecp_nistz256_subx:
.cfi_startproc
	xor	$t4, $t4
	sbb	$a0, $t0
	sbb	$a1, $t1
	 mov	$t0, $a0
	sbb	$a2, $t2
	sbb	$a3, $t3
	 mov	$t1, $a1
	sbb	\$0, $t4

	xor	$a3 ,$a3
	adc	\$-1, $t0
	 mov	$t2, $a2
	adc	$poly1, $t1
	adc	\$0, $t2
	 mov	$t3, $a3
	adc	$poly3, $t3

	bt	\$0, $t4
	cmovc	$t0, $a0
	cmovc	$t1, $a1
	cmovc	$t2, $a2
	cmovc	$t3, $a3

	ret
.cfi_endproc
.size	__ecp_nistz256_subx,.-__ecp_nistz256_subx

.type	__ecp_nistz256_mul_by_2x,\@abi-omnipotent
.align	32
__ecp_nistz256_mul_by_2x:
.cfi_startproc
	xor	$t4, $t4
	adc	$a0, $a0		# a0:a3+a0:a3
	adc	$a1, $a1
	 mov	$a0, $t0
	adc	$a2, $a2
	adc	$a3, $a3
	 mov	$a1, $t1
	adc	\$0, $t4

	xor	$t3, $t3
	sbb	\$-1, $a0
	 mov	$a2, $t2
	sbb	$poly1, $a1
	sbb	\$0, $a2
	 mov	$a3, $t3
	sbb	$poly3, $a3
	sbb	\$0, $t4

	cmovc	$t0, $a0
	cmovc	$t1, $a1
	mov	$a0, 8*0($r_ptr)
	cmovc	$t2, $a2
	mov	$a1, 8*1($r_ptr)
	cmovc	$t3, $a3
	mov	$a2, 8*2($r_ptr)
	mov	$a3, 8*3($r_ptr)

	ret
.cfi_endproc
.size	__ecp_nistz256_mul_by_2x,.-__ecp_nistz256_mul_by_2x
___
									}
&gen_double("x");
&gen_add("x");
&gen_add_affine("x");
}
}}}

# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
if ($win64) {
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___;
.extern	__imp_RtlVirtualUnwind

.type	short_handler,\@abi-omnipotent
.align	16
short_handler:
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
	lea	(%rsi,%r10),%r10	# end of prologue label
	cmp	%r10,%rbx		# context->Rip<end of prologue label
	jb	.Lcommon_seh_tail

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	lea	16(%rax),%rax

	mov	-8(%rax),%r12
	mov	-16(%rax),%r13
	mov	%r12,216($context)	# restore context->R12
	mov	%r13,224($context)	# restore context->R13

	jmp	.Lcommon_seh_tail
.size	short_handler,.-short_handler

.type	full_handler,\@abi-omnipotent
.align	16
full_handler:
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
	lea	(%rsi,%r10),%r10	# end of prologue label
	cmp	%r10,%rbx		# context->Rip<end of prologue label
	jb	.Lcommon_seh_tail

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	mov	8(%r11),%r10d		# HandlerData[2]
	lea	(%rax,%r10),%rax

	mov	-8(%rax),%rbp
	mov	-16(%rax),%rbx
	mov	-24(%rax),%r12
	mov	-32(%rax),%r13
	mov	-40(%rax),%r14
	mov	-48(%rax),%r15
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12
	mov	%r13,224($context)	# restore context->R13
	mov	%r14,232($context)	# restore context->R14
	mov	%r15,240($context)	# restore context->R15

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
.size	full_handler,.-full_handler

.section	.pdata
.align	4
	.rva	.LSEH_begin_ecp_nistz256_mul_by_2
	.rva	.LSEH_end_ecp_nistz256_mul_by_2
	.rva	.LSEH_info_ecp_nistz256_mul_by_2

	.rva	.LSEH_begin_ecp_nistz256_div_by_2
	.rva	.LSEH_end_ecp_nistz256_div_by_2
	.rva	.LSEH_info_ecp_nistz256_div_by_2

	.rva	.LSEH_begin_ecp_nistz256_mul_by_3
	.rva	.LSEH_end_ecp_nistz256_mul_by_3
	.rva	.LSEH_info_ecp_nistz256_mul_by_3

	.rva	.LSEH_begin_ecp_nistz256_add
	.rva	.LSEH_end_ecp_nistz256_add
	.rva	.LSEH_info_ecp_nistz256_add

	.rva	.LSEH_begin_ecp_nistz256_sub
	.rva	.LSEH_end_ecp_nistz256_sub
	.rva	.LSEH_info_ecp_nistz256_sub

	.rva	.LSEH_begin_ecp_nistz256_neg
	.rva	.LSEH_end_ecp_nistz256_neg
	.rva	.LSEH_info_ecp_nistz256_neg

	.rva	.LSEH_begin_ecp_nistz256_ord_mul_mont
	.rva	.LSEH_end_ecp_nistz256_ord_mul_mont
	.rva	.LSEH_info_ecp_nistz256_ord_mul_mont

	.rva	.LSEH_begin_ecp_nistz256_ord_sqr_mont
	.rva	.LSEH_end_ecp_nistz256_ord_sqr_mont
	.rva	.LSEH_info_ecp_nistz256_ord_sqr_mont
___
$code.=<<___	if ($addx);
	.rva	.LSEH_begin_ecp_nistz256_ord_mul_montx
	.rva	.LSEH_end_ecp_nistz256_ord_mul_montx
	.rva	.LSEH_info_ecp_nistz256_ord_mul_montx

	.rva	.LSEH_begin_ecp_nistz256_ord_sqr_montx
	.rva	.LSEH_end_ecp_nistz256_ord_sqr_montx
	.rva	.LSEH_info_ecp_nistz256_ord_sqr_montx
___
$code.=<<___;
	.rva	.LSEH_begin_ecp_nistz256_to_mont
	.rva	.LSEH_end_ecp_nistz256_to_mont
	.rva	.LSEH_info_ecp_nistz256_to_mont

	.rva	.LSEH_begin_ecp_nistz256_mul_mont
	.rva	.LSEH_end_ecp_nistz256_mul_mont
	.rva	.LSEH_info_ecp_nistz256_mul_mont

	.rva	.LSEH_begin_ecp_nistz256_sqr_mont
	.rva	.LSEH_end_ecp_nistz256_sqr_mont
	.rva	.LSEH_info_ecp_nistz256_sqr_mont

	.rva	.LSEH_begin_ecp_nistz256_from_mont
	.rva	.LSEH_end_ecp_nistz256_from_mont
	.rva	.LSEH_info_ecp_nistz256_from_mont

	.rva	.LSEH_begin_ecp_nistz256_gather_w5
	.rva	.LSEH_end_ecp_nistz256_gather_w5
	.rva	.LSEH_info_ecp_nistz256_gather_wX

	.rva	.LSEH_begin_ecp_nistz256_gather_w7
	.rva	.LSEH_end_ecp_nistz256_gather_w7
	.rva	.LSEH_info_ecp_nistz256_gather_wX
___
$code.=<<___	if ($avx>1);
	.rva	.LSEH_begin_ecp_nistz256_avx2_gather_w5
	.rva	.LSEH_end_ecp_nistz256_avx2_gather_w5
	.rva	.LSEH_info_ecp_nistz256_avx2_gather_wX

	.rva	.LSEH_begin_ecp_nistz256_avx2_gather_w7
	.rva	.LSEH_end_ecp_nistz256_avx2_gather_w7
	.rva	.LSEH_info_ecp_nistz256_avx2_gather_wX
___
$code.=<<___;
	.rva	.LSEH_begin_ecp_nistz256_point_double
	.rva	.LSEH_end_ecp_nistz256_point_double
	.rva	.LSEH_info_ecp_nistz256_point_double

	.rva	.LSEH_begin_ecp_nistz256_point_add
	.rva	.LSEH_end_ecp_nistz256_point_add
	.rva	.LSEH_info_ecp_nistz256_point_add

	.rva	.LSEH_begin_ecp_nistz256_point_add_affine
	.rva	.LSEH_end_ecp_nistz256_point_add_affine
	.rva	.LSEH_info_ecp_nistz256_point_add_affine
___
$code.=<<___ if ($addx);
	.rva	.LSEH_begin_ecp_nistz256_point_doublex
	.rva	.LSEH_end_ecp_nistz256_point_doublex
	.rva	.LSEH_info_ecp_nistz256_point_doublex

	.rva	.LSEH_begin_ecp_nistz256_point_addx
	.rva	.LSEH_end_ecp_nistz256_point_addx
	.rva	.LSEH_info_ecp_nistz256_point_addx

	.rva	.LSEH_begin_ecp_nistz256_point_add_affinex
	.rva	.LSEH_end_ecp_nistz256_point_add_affinex
	.rva	.LSEH_info_ecp_nistz256_point_add_affinex
___
$code.=<<___;

.section	.xdata
.align	8
.LSEH_info_ecp_nistz256_mul_by_2:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Lmul_by_2_body,.Lmul_by_2_epilogue	# HandlerData[]
.LSEH_info_ecp_nistz256_div_by_2:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Ldiv_by_2_body,.Ldiv_by_2_epilogue	# HandlerData[]
.LSEH_info_ecp_nistz256_mul_by_3:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Lmul_by_3_body,.Lmul_by_3_epilogue	# HandlerData[]
.LSEH_info_ecp_nistz256_add:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Ladd_body,.Ladd_epilogue		# HandlerData[]
.LSEH_info_ecp_nistz256_sub:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Lsub_body,.Lsub_epilogue		# HandlerData[]
.LSEH_info_ecp_nistz256_neg:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Lneg_body,.Lneg_epilogue		# HandlerData[]
.LSEH_info_ecp_nistz256_ord_mul_mont:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lord_mul_body,.Lord_mul_epilogue	# HandlerData[]
	.long	48,0
.LSEH_info_ecp_nistz256_ord_sqr_mont:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lord_sqr_body,.Lord_sqr_epilogue	# HandlerData[]
	.long	48,0
___
$code.=<<___ if ($addx);
.LSEH_info_ecp_nistz256_ord_mul_montx:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lord_mulx_body,.Lord_mulx_epilogue	# HandlerData[]
	.long	48,0
.LSEH_info_ecp_nistz256_ord_sqr_montx:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lord_sqrx_body,.Lord_sqrx_epilogue	# HandlerData[]
	.long	48,0
___
$code.=<<___;
.LSEH_info_ecp_nistz256_to_mont:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lmul_body,.Lmul_epilogue		# HandlerData[]
	.long	48,0
.LSEH_info_ecp_nistz256_mul_mont:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lmul_body,.Lmul_epilogue		# HandlerData[]
	.long	48,0
.LSEH_info_ecp_nistz256_sqr_mont:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lsqr_body,.Lsqr_epilogue		# HandlerData[]
	.long	48,0
.LSEH_info_ecp_nistz256_from_mont:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Lfrom_body,.Lfrom_epilogue		# HandlerData[]
.LSEH_info_ecp_nistz256_gather_wX:
	.byte	0x01,0x33,0x16,0x00
	.byte	0x33,0xf8,0x09,0x00	#movaps 0x90(rsp),xmm15
	.byte	0x2e,0xe8,0x08,0x00	#movaps 0x80(rsp),xmm14
	.byte	0x29,0xd8,0x07,0x00	#movaps 0x70(rsp),xmm13
	.byte	0x24,0xc8,0x06,0x00	#movaps 0x60(rsp),xmm12
	.byte	0x1f,0xb8,0x05,0x00	#movaps 0x50(rsp),xmm11
	.byte	0x1a,0xa8,0x04,0x00	#movaps 0x40(rsp),xmm10
	.byte	0x15,0x98,0x03,0x00	#movaps 0x30(rsp),xmm9
	.byte	0x10,0x88,0x02,0x00	#movaps 0x20(rsp),xmm8
	.byte	0x0c,0x78,0x01,0x00	#movaps 0x10(rsp),xmm7
	.byte	0x08,0x68,0x00,0x00	#movaps 0x00(rsp),xmm6
	.byte	0x04,0x01,0x15,0x00	#sub	rsp,0xa8
	.align	8
___
$code.=<<___	if ($avx>1);
.LSEH_info_ecp_nistz256_avx2_gather_wX:
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
	.align	8
___
$code.=<<___;
.LSEH_info_ecp_nistz256_point_double:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lpoint_doubleq_body,.Lpoint_doubleq_epilogue	# HandlerData[]
	.long	32*5+56,0
.LSEH_info_ecp_nistz256_point_add:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lpoint_addq_body,.Lpoint_addq_epilogue		# HandlerData[]
	.long	32*18+56,0
.LSEH_info_ecp_nistz256_point_add_affine:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Ladd_affineq_body,.Ladd_affineq_epilogue	# HandlerData[]
	.long	32*15+56,0
___
$code.=<<___ if ($addx);
.align	8
.LSEH_info_ecp_nistz256_point_doublex:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lpoint_doublex_body,.Lpoint_doublex_epilogue	# HandlerData[]
	.long	32*5+56,0
.LSEH_info_ecp_nistz256_point_addx:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lpoint_addx_body,.Lpoint_addx_epilogue		# HandlerData[]
	.long	32*18+56,0
.LSEH_info_ecp_nistz256_point_add_affinex:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Ladd_affinex_body,.Ladd_affinex_epilogue	# HandlerData[]
	.long	32*15+56,0
___
}

########################################################################
# Convert ecp_nistz256_table.c to layout expected by ecp_nistz_gather_w7
#
open TABLE,"<ecp_nistz256_table.c"		or
open TABLE,"<${dir}../ecp_nistz256_table.c"	or
die "failed to open ecp_nistz256_table.c:",$!;

use integer;

foreach(<TABLE>) {
	s/TOBN\(\s*(0x[0-9a-f]+),\s*(0x[0-9a-f]+)\s*\)/push @arr,hex($2),hex($1)/geo;
}
close TABLE;

die "insane number of elements" if ($#arr != 64*16*37-1);

print <<___;
.text
.globl	ecp_nistz256_precomputed
.type	ecp_nistz256_precomputed,\@object
.align	4096
ecp_nistz256_precomputed:
___
while (@line=splice(@arr,0,16)) {
	print ".long\t",join(',',map { sprintf "0x%08x",$_} @line),"\n";
}
print <<___;
.size	ecp_nistz256_precomputed,.-ecp_nistz256_precomputed
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

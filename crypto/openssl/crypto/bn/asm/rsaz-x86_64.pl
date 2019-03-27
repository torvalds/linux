#! /usr/bin/env perl
# Copyright 2013-2016 The OpenSSL Project Authors. All Rights Reserved.
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
# [1] S. Gueron, "Efficient Software Implementations of Modular
#     Exponentiation", http://eprint.iacr.org/2011/239
# [2] S. Gueron, V. Krasnov. "Speeding up Big-Numbers Squaring".
#     IEEE Proceedings of 9th International Conference on Information
#     Technology: New Generations (ITNG 2012), 821-823 (2012).
# [3] S. Gueron, Efficient Software Implementations of Modular Exponentiation
#     Journal of Cryptographic Engineering 2:31-43 (2012).
# [4] S. Gueron, V. Krasnov: "[PATCH] Efficient and side channel analysis
#     resistant 512-bit and 1024-bit modular exponentiation for optimizing
#     RSA1024 and RSA2048 on x86_64 platforms",
#     http://rt.openssl.org/Ticket/Display.html?id=2582&user=guest&pass=guest
#
# While original submission covers 512- and 1024-bit exponentiation,
# this module is limited to 512-bit version only (and as such
# accelerates RSA1024 sign). This is because improvement for longer
# keys is not high enough to justify the effort, highest measured
# was ~5% on Westmere. [This is relative to OpenSSL 1.0.2, upcoming
# for the moment of this writing!] Nor does this module implement
# "monolithic" complete exponentiation jumbo-subroutine, but adheres
# to more modular mixture of C and assembly. And it's optimized even
# for processors other than Intel Core family (see table below for
# improvement coefficients).
# 						<appro@openssl.org>
#
# RSA1024 sign/sec	this/original	|this/rsax(*)	this/fips(*)
#			----------------+---------------------------
# Opteron		+13%		|+5%		+20%
# Bulldozer		-0%		|-1%		+10%
# P4			+11%		|+7%		+8%
# Westmere		+5%		|+14%		+17%
# Sandy Bridge		+2%		|+12%		+29%
# Ivy Bridge		+1%		|+11%		+35%
# Haswell(**)		-0%		|+12%		+39%
# Atom			+13%		|+11%		+4%
# VIA Nano		+70%		|+9%		+25%
#
# (*)	rsax engine and fips numbers are presented for reference
#	purposes;
# (**)	MULX was attempted, but found to give only marginal improvement;

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
	$addx = ($1>=2.23);
}

if (!$addx && $win64 && ($flavour =~ /nasm/ || $ENV{ASM} =~ /nasm/) &&
	    `nasm -v 2>&1` =~ /NASM version ([2-9]\.[0-9]+)/) {
	$addx = ($1>=2.10);
}

if (!$addx && $win64 && ($flavour =~ /masm/ || $ENV{ASM} =~ /ml64/) &&
	    `ml64 2>&1` =~ /Version ([0-9]+)\./) {
	$addx = ($1>=12);
}

if (!$addx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|.*based on LLVM) ([3-9])\.([0-9]+)/) {
	my $ver = $2 + $3/100.0;	# 3.1->3.01, 3.10->3.10
	$addx = ($ver>=3.03);
}

($out, $inp, $mod) = ("%rdi", "%rsi", "%rbp");	# common internal API
{
my ($out,$inp,$mod,$n0,$times) = ("%rdi","%rsi","%rdx","%rcx","%r8d");

$code.=<<___;
.text

.extern	OPENSSL_ia32cap_P

.globl	rsaz_512_sqr
.type	rsaz_512_sqr,\@function,5
.align	32
rsaz_512_sqr:				# 25-29% faster than rsaz_512_mul
.cfi_startproc
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

	subq	\$128+24, %rsp
.cfi_adjust_cfa_offset	128+24
.Lsqr_body:
	movq	$mod, %rbp		# common argument
	movq	($inp), %rdx
	movq	8($inp), %rax
	movq	$n0, 128(%rsp)
___
$code.=<<___ if ($addx);
	movl	\$0x80100,%r11d
	andl	OPENSSL_ia32cap_P+8(%rip),%r11d
	cmpl	\$0x80100,%r11d		# check for MULX and ADO/CX
	je	.Loop_sqrx
___
$code.=<<___;
	jmp	.Loop_sqr

.align	32
.Loop_sqr:
	movl	$times,128+8(%rsp)
#first iteration
	movq	%rdx, %rbx
	mulq	%rdx
	movq	%rax, %r8
	movq	16($inp), %rax
	movq	%rdx, %r9

	mulq	%rbx
	addq	%rax, %r9
	movq	24($inp), %rax
	movq	%rdx, %r10
	adcq	\$0, %r10

	mulq	%rbx
	addq	%rax, %r10
	movq	32($inp), %rax
	movq	%rdx, %r11
	adcq	\$0, %r11

	mulq	%rbx
	addq	%rax, %r11
	movq	40($inp), %rax
	movq	%rdx, %r12
	adcq	\$0, %r12

	mulq	%rbx
	addq	%rax, %r12
	movq	48($inp), %rax
	movq	%rdx, %r13
	adcq	\$0, %r13

	mulq	%rbx
	addq	%rax, %r13
	movq	56($inp), %rax
	movq	%rdx, %r14
	adcq	\$0, %r14

	mulq	%rbx
	addq	%rax, %r14
	movq	%rbx, %rax
	movq	%rdx, %r15
	adcq	\$0, %r15

	addq	%r8, %r8		#shlq	\$1, %r8
	movq	%r9, %rcx
	adcq	%r9, %r9		#shld	\$1, %r8, %r9

	mulq	%rax
	movq	%rax, (%rsp)
	addq	%rdx, %r8
	adcq	\$0, %r9

	movq	%r8, 8(%rsp)
	shrq	\$63, %rcx

#second iteration
	movq	8($inp), %r8
	movq	16($inp), %rax
	mulq	%r8
	addq	%rax, %r10
	movq	24($inp), %rax
	movq	%rdx, %rbx
	adcq	\$0, %rbx

	mulq	%r8
	addq	%rax, %r11
	movq	32($inp), %rax
	adcq	\$0, %rdx
	addq	%rbx, %r11
	movq	%rdx, %rbx
	adcq	\$0, %rbx

	mulq	%r8
	addq	%rax, %r12
	movq	40($inp), %rax
	adcq	\$0, %rdx
	addq	%rbx, %r12
	movq	%rdx, %rbx
	adcq	\$0, %rbx

	mulq	%r8
	addq	%rax, %r13
	movq	48($inp), %rax
	adcq	\$0, %rdx
	addq	%rbx, %r13
	movq	%rdx, %rbx
	adcq	\$0, %rbx

	mulq	%r8
	addq	%rax, %r14
	movq	56($inp), %rax
	adcq	\$0, %rdx
	addq	%rbx, %r14
	movq	%rdx, %rbx
	adcq	\$0, %rbx

	mulq	%r8
	addq	%rax, %r15
	movq	%r8, %rax
	adcq	\$0, %rdx
	addq	%rbx, %r15
	movq	%rdx, %r8
	movq	%r10, %rdx
	adcq	\$0, %r8

	add	%rdx, %rdx
	lea	(%rcx,%r10,2), %r10	#shld	\$1, %rcx, %r10
	movq	%r11, %rbx
	adcq	%r11, %r11		#shld	\$1, %r10, %r11

	mulq	%rax
	addq	%rax, %r9
	adcq	%rdx, %r10
	adcq	\$0, %r11

	movq	%r9, 16(%rsp)
	movq	%r10, 24(%rsp)
	shrq	\$63, %rbx

#third iteration
	movq	16($inp), %r9
	movq	24($inp), %rax
	mulq	%r9
	addq	%rax, %r12
	movq	32($inp), %rax
	movq	%rdx, %rcx
	adcq	\$0, %rcx

	mulq	%r9
	addq	%rax, %r13
	movq	40($inp), %rax
	adcq	\$0, %rdx
	addq	%rcx, %r13
	movq	%rdx, %rcx
	adcq	\$0, %rcx

	mulq	%r9
	addq	%rax, %r14
	movq	48($inp), %rax
	adcq	\$0, %rdx
	addq	%rcx, %r14
	movq	%rdx, %rcx
	adcq	\$0, %rcx

	mulq	%r9
	 movq	%r12, %r10
	 lea	(%rbx,%r12,2), %r12	#shld	\$1, %rbx, %r12
	addq	%rax, %r15
	movq	56($inp), %rax
	adcq	\$0, %rdx
	addq	%rcx, %r15
	movq	%rdx, %rcx
	adcq	\$0, %rcx

	mulq	%r9
	 shrq	\$63, %r10
	addq	%rax, %r8
	movq	%r9, %rax
	adcq	\$0, %rdx
	addq	%rcx, %r8
	movq	%rdx, %r9
	adcq	\$0, %r9

	movq	%r13, %rcx
	leaq	(%r10,%r13,2), %r13	#shld	\$1, %r12, %r13

	mulq	%rax
	addq	%rax, %r11
	adcq	%rdx, %r12
	adcq	\$0, %r13

	movq	%r11, 32(%rsp)
	movq	%r12, 40(%rsp)
	shrq	\$63, %rcx

#fourth iteration
	movq	24($inp), %r10
	movq	32($inp), %rax
	mulq	%r10
	addq	%rax, %r14
	movq	40($inp), %rax
	movq	%rdx, %rbx
	adcq	\$0, %rbx

	mulq	%r10
	addq	%rax, %r15
	movq	48($inp), %rax
	adcq	\$0, %rdx
	addq	%rbx, %r15
	movq	%rdx, %rbx
	adcq	\$0, %rbx

	mulq	%r10
	 movq	%r14, %r12
	 leaq	(%rcx,%r14,2), %r14	#shld	\$1, %rcx, %r14
	addq	%rax, %r8
	movq	56($inp), %rax
	adcq	\$0, %rdx
	addq	%rbx, %r8
	movq	%rdx, %rbx
	adcq	\$0, %rbx

	mulq	%r10
	 shrq	\$63, %r12
	addq	%rax, %r9
	movq	%r10, %rax
	adcq	\$0, %rdx
	addq	%rbx, %r9
	movq	%rdx, %r10
	adcq	\$0, %r10

	movq	%r15, %rbx
	leaq	(%r12,%r15,2),%r15	#shld	\$1, %r14, %r15

	mulq	%rax
	addq	%rax, %r13
	adcq	%rdx, %r14
	adcq	\$0, %r15

	movq	%r13, 48(%rsp)
	movq	%r14, 56(%rsp)
	shrq	\$63, %rbx

#fifth iteration
	movq	32($inp), %r11
	movq	40($inp), %rax
	mulq	%r11
	addq	%rax, %r8
	movq	48($inp), %rax
	movq	%rdx, %rcx
	adcq	\$0, %rcx

	mulq	%r11
	addq	%rax, %r9
	movq	56($inp), %rax
	adcq	\$0, %rdx
	 movq	%r8, %r12
	 leaq	(%rbx,%r8,2), %r8	#shld	\$1, %rbx, %r8
	addq	%rcx, %r9
	movq	%rdx, %rcx
	adcq	\$0, %rcx

	mulq	%r11
	 shrq	\$63, %r12
	addq	%rax, %r10
	movq	%r11, %rax
	adcq	\$0, %rdx
	addq	%rcx, %r10
	movq	%rdx, %r11
	adcq	\$0, %r11

	movq	%r9, %rcx
	leaq	(%r12,%r9,2), %r9	#shld	\$1, %r8, %r9

	mulq	%rax
	addq	%rax, %r15
	adcq	%rdx, %r8
	adcq	\$0, %r9

	movq	%r15, 64(%rsp)
	movq	%r8, 72(%rsp)
	shrq	\$63, %rcx

#sixth iteration
	movq	40($inp), %r12
	movq	48($inp), %rax
	mulq	%r12
	addq	%rax, %r10
	movq	56($inp), %rax
	movq	%rdx, %rbx
	adcq	\$0, %rbx

	mulq	%r12
	addq	%rax, %r11
	movq	%r12, %rax
	 movq	%r10, %r15
	 leaq	(%rcx,%r10,2), %r10	#shld	\$1, %rcx, %r10
	adcq	\$0, %rdx
	 shrq	\$63, %r15
	addq	%rbx, %r11
	movq	%rdx, %r12
	adcq	\$0, %r12

	movq	%r11, %rbx
	leaq	(%r15,%r11,2), %r11	#shld	\$1, %r10, %r11

	mulq	%rax
	addq	%rax, %r9
	adcq	%rdx, %r10
	adcq	\$0, %r11

	movq	%r9, 80(%rsp)
	movq	%r10, 88(%rsp)

#seventh iteration
	movq	48($inp), %r13
	movq	56($inp), %rax
	mulq	%r13
	addq	%rax, %r12
	movq	%r13, %rax
	movq	%rdx, %r13
	adcq	\$0, %r13

	xorq	%r14, %r14
	shlq	\$1, %rbx
	adcq	%r12, %r12		#shld	\$1, %rbx, %r12
	adcq	%r13, %r13		#shld	\$1, %r12, %r13
	adcq	%r14, %r14		#shld	\$1, %r13, %r14

	mulq	%rax
	addq	%rax, %r11
	adcq	%rdx, %r12
	adcq	\$0, %r13

	movq	%r11, 96(%rsp)
	movq	%r12, 104(%rsp)

#eighth iteration
	movq	56($inp), %rax
	mulq	%rax
	addq	%rax, %r13
	adcq	\$0, %rdx

	addq	%rdx, %r14

	movq	%r13, 112(%rsp)
	movq	%r14, 120(%rsp)

	movq	(%rsp), %r8
	movq	8(%rsp), %r9
	movq	16(%rsp), %r10
	movq	24(%rsp), %r11
	movq	32(%rsp), %r12
	movq	40(%rsp), %r13
	movq	48(%rsp), %r14
	movq	56(%rsp), %r15

	call	__rsaz_512_reduce

	addq	64(%rsp), %r8
	adcq	72(%rsp), %r9
	adcq	80(%rsp), %r10
	adcq	88(%rsp), %r11
	adcq	96(%rsp), %r12
	adcq	104(%rsp), %r13
	adcq	112(%rsp), %r14
	adcq	120(%rsp), %r15
	sbbq	%rcx, %rcx

	call	__rsaz_512_subtract

	movq	%r8, %rdx
	movq	%r9, %rax
	movl	128+8(%rsp), $times
	movq	$out, $inp

	decl	$times
	jnz	.Loop_sqr
___
if ($addx) {
$code.=<<___;
	jmp	.Lsqr_tail

.align	32
.Loop_sqrx:
	movl	$times,128+8(%rsp)
	movq	$out, %xmm0		# off-load
	movq	%rbp, %xmm1		# off-load
#first iteration
	mulx	%rax, %r8, %r9

	mulx	16($inp), %rcx, %r10
	xor	%rbp, %rbp		# cf=0, of=0

	mulx	24($inp), %rax, %r11
	adcx	%rcx, %r9

	mulx	32($inp), %rcx, %r12
	adcx	%rax, %r10

	mulx	40($inp), %rax, %r13
	adcx	%rcx, %r11

	.byte	0xc4,0x62,0xf3,0xf6,0xb6,0x30,0x00,0x00,0x00	# mulx	48($inp), %rcx, %r14
	adcx	%rax, %r12
	adcx	%rcx, %r13

	.byte	0xc4,0x62,0xfb,0xf6,0xbe,0x38,0x00,0x00,0x00	# mulx	56($inp), %rax, %r15
	adcx	%rax, %r14
	adcx	%rbp, %r15		# %rbp is 0

	mov	%r9, %rcx
	shld	\$1, %r8, %r9
	shl	\$1, %r8

	xor	%ebp, %ebp
	mulx	%rdx, %rax, %rdx
	adcx	%rdx, %r8
	 mov	8($inp), %rdx
	adcx	%rbp, %r9

	mov	%rax, (%rsp)
	mov	%r8, 8(%rsp)

#second iteration
	mulx	16($inp), %rax, %rbx
	adox	%rax, %r10
	adcx	%rbx, %r11

	.byte	0xc4,0x62,0xc3,0xf6,0x86,0x18,0x00,0x00,0x00	# mulx	24($inp), $out, %r8
	adox	$out, %r11
	adcx	%r8, %r12

	mulx	32($inp), %rax, %rbx
	adox	%rax, %r12
	adcx	%rbx, %r13

	mulx	40($inp), $out, %r8
	adox	$out, %r13
	adcx	%r8, %r14

	.byte	0xc4,0xe2,0xfb,0xf6,0x9e,0x30,0x00,0x00,0x00	# mulx	48($inp), %rax, %rbx
	adox	%rax, %r14
	adcx	%rbx, %r15

	.byte	0xc4,0x62,0xc3,0xf6,0x86,0x38,0x00,0x00,0x00	# mulx	56($inp), $out, %r8
	adox	$out, %r15
	adcx	%rbp, %r8
	adox	%rbp, %r8

	mov	%r11, %rbx
	shld	\$1, %r10, %r11
	shld	\$1, %rcx, %r10

	xor	%ebp,%ebp
	mulx	%rdx, %rax, %rcx
	 mov	16($inp), %rdx
	adcx	%rax, %r9
	adcx	%rcx, %r10
	adcx	%rbp, %r11

	mov	%r9, 16(%rsp)
	.byte	0x4c,0x89,0x94,0x24,0x18,0x00,0x00,0x00		# mov	%r10, 24(%rsp)

#third iteration
	.byte	0xc4,0x62,0xc3,0xf6,0x8e,0x18,0x00,0x00,0x00	# mulx	24($inp), $out, %r9
	adox	$out, %r12
	adcx	%r9, %r13

	mulx	32($inp), %rax, %rcx
	adox	%rax, %r13
	adcx	%rcx, %r14

	mulx	40($inp), $out, %r9
	adox	$out, %r14
	adcx	%r9, %r15

	.byte	0xc4,0xe2,0xfb,0xf6,0x8e,0x30,0x00,0x00,0x00	# mulx	48($inp), %rax, %rcx
	adox	%rax, %r15
	adcx	%rcx, %r8

	.byte	0xc4,0x62,0xc3,0xf6,0x8e,0x38,0x00,0x00,0x00	# mulx	56($inp), $out, %r9
	adox	$out, %r8
	adcx	%rbp, %r9
	adox	%rbp, %r9

	mov	%r13, %rcx
	shld	\$1, %r12, %r13
	shld	\$1, %rbx, %r12

	xor	%ebp, %ebp
	mulx	%rdx, %rax, %rdx
	adcx	%rax, %r11
	adcx	%rdx, %r12
	 mov	24($inp), %rdx
	adcx	%rbp, %r13

	mov	%r11, 32(%rsp)
	.byte	0x4c,0x89,0xa4,0x24,0x28,0x00,0x00,0x00		# mov	%r12, 40(%rsp)

#fourth iteration
	.byte	0xc4,0xe2,0xfb,0xf6,0x9e,0x20,0x00,0x00,0x00	# mulx	32($inp), %rax, %rbx
	adox	%rax, %r14
	adcx	%rbx, %r15

	mulx	40($inp), $out, %r10
	adox	$out, %r15
	adcx	%r10, %r8

	mulx	48($inp), %rax, %rbx
	adox	%rax, %r8
	adcx	%rbx, %r9

	mulx	56($inp), $out, %r10
	adox	$out, %r9
	adcx	%rbp, %r10
	adox	%rbp, %r10

	.byte	0x66
	mov	%r15, %rbx
	shld	\$1, %r14, %r15
	shld	\$1, %rcx, %r14

	xor	%ebp, %ebp
	mulx	%rdx, %rax, %rdx
	adcx	%rax, %r13
	adcx	%rdx, %r14
	 mov	32($inp), %rdx
	adcx	%rbp, %r15

	mov	%r13, 48(%rsp)
	mov	%r14, 56(%rsp)

#fifth iteration
	.byte	0xc4,0x62,0xc3,0xf6,0x9e,0x28,0x00,0x00,0x00	# mulx	40($inp), $out, %r11
	adox	$out, %r8
	adcx	%r11, %r9

	mulx	48($inp), %rax, %rcx
	adox	%rax, %r9
	adcx	%rcx, %r10

	mulx	56($inp), $out, %r11
	adox	$out, %r10
	adcx	%rbp, %r11
	adox	%rbp, %r11

	mov	%r9, %rcx
	shld	\$1, %r8, %r9
	shld	\$1, %rbx, %r8

	xor	%ebp, %ebp
	mulx	%rdx, %rax, %rdx
	adcx	%rax, %r15
	adcx	%rdx, %r8
	 mov	40($inp), %rdx
	adcx	%rbp, %r9

	mov	%r15, 64(%rsp)
	mov	%r8, 72(%rsp)

#sixth iteration
	.byte	0xc4,0xe2,0xfb,0xf6,0x9e,0x30,0x00,0x00,0x00	# mulx	48($inp), %rax, %rbx
	adox	%rax, %r10
	adcx	%rbx, %r11

	.byte	0xc4,0x62,0xc3,0xf6,0xa6,0x38,0x00,0x00,0x00	# mulx	56($inp), $out, %r12
	adox	$out, %r11
	adcx	%rbp, %r12
	adox	%rbp, %r12

	mov	%r11, %rbx
	shld	\$1, %r10, %r11
	shld	\$1, %rcx, %r10

	xor	%ebp, %ebp
	mulx	%rdx, %rax, %rdx
	adcx	%rax, %r9
	adcx	%rdx, %r10
	 mov	48($inp), %rdx
	adcx	%rbp, %r11

	mov	%r9, 80(%rsp)
	mov	%r10, 88(%rsp)

#seventh iteration
	.byte	0xc4,0x62,0xfb,0xf6,0xae,0x38,0x00,0x00,0x00	# mulx	56($inp), %rax, %r13
	adox	%rax, %r12
	adox	%rbp, %r13

	xor	%r14, %r14
	shld	\$1, %r13, %r14
	shld	\$1, %r12, %r13
	shld	\$1, %rbx, %r12

	xor	%ebp, %ebp
	mulx	%rdx, %rax, %rdx
	adcx	%rax, %r11
	adcx	%rdx, %r12
	 mov	56($inp), %rdx
	adcx	%rbp, %r13

	.byte	0x4c,0x89,0x9c,0x24,0x60,0x00,0x00,0x00		# mov	%r11, 96(%rsp)
	.byte	0x4c,0x89,0xa4,0x24,0x68,0x00,0x00,0x00		# mov	%r12, 104(%rsp)

#eighth iteration
	mulx	%rdx, %rax, %rdx
	adox	%rax, %r13
	adox	%rbp, %rdx

	.byte	0x66
	add	%rdx, %r14

	movq	%r13, 112(%rsp)
	movq	%r14, 120(%rsp)
	movq	%xmm0, $out
	movq	%xmm1, %rbp

	movq	128(%rsp), %rdx		# pull $n0
	movq	(%rsp), %r8
	movq	8(%rsp), %r9
	movq	16(%rsp), %r10
	movq	24(%rsp), %r11
	movq	32(%rsp), %r12
	movq	40(%rsp), %r13
	movq	48(%rsp), %r14
	movq	56(%rsp), %r15

	call	__rsaz_512_reducex

	addq	64(%rsp), %r8
	adcq	72(%rsp), %r9
	adcq	80(%rsp), %r10
	adcq	88(%rsp), %r11
	adcq	96(%rsp), %r12
	adcq	104(%rsp), %r13
	adcq	112(%rsp), %r14
	adcq	120(%rsp), %r15
	sbbq	%rcx, %rcx

	call	__rsaz_512_subtract

	movq	%r8, %rdx
	movq	%r9, %rax
	movl	128+8(%rsp), $times
	movq	$out, $inp

	decl	$times
	jnz	.Loop_sqrx

.Lsqr_tail:
___
}
$code.=<<___;

	leaq	128+24+48(%rsp), %rax
.cfi_def_cfa	%rax,8
	movq	-48(%rax), %r15
.cfi_restore	%r15
	movq	-40(%rax), %r14
.cfi_restore	%r14
	movq	-32(%rax), %r13
.cfi_restore	%r13
	movq	-24(%rax), %r12
.cfi_restore	%r12
	movq	-16(%rax), %rbp
.cfi_restore	%rbp
	movq	-8(%rax), %rbx
.cfi_restore	%rbx
	leaq	(%rax), %rsp
.cfi_def_cfa_register	%rsp
.Lsqr_epilogue:
	ret
.cfi_endproc
.size	rsaz_512_sqr,.-rsaz_512_sqr
___
}
{
my ($out,$ap,$bp,$mod,$n0) = ("%rdi","%rsi","%rdx","%rcx","%r8");
$code.=<<___;
.globl	rsaz_512_mul
.type	rsaz_512_mul,\@function,5
.align	32
rsaz_512_mul:
.cfi_startproc
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

	subq	\$128+24, %rsp
.cfi_adjust_cfa_offset	128+24
.Lmul_body:
	movq	$out, %xmm0		# off-load arguments
	movq	$mod, %xmm1
	movq	$n0, 128(%rsp)
___
$code.=<<___ if ($addx);
	movl	\$0x80100,%r11d
	andl	OPENSSL_ia32cap_P+8(%rip),%r11d
	cmpl	\$0x80100,%r11d		# check for MULX and ADO/CX
	je	.Lmulx
___
$code.=<<___;
	movq	($bp), %rbx		# pass b[0]
	movq	$bp, %rbp		# pass argument
	call	__rsaz_512_mul

	movq	%xmm0, $out
	movq	%xmm1, %rbp

	movq	(%rsp), %r8
	movq	8(%rsp), %r9
	movq	16(%rsp), %r10
	movq	24(%rsp), %r11
	movq	32(%rsp), %r12
	movq	40(%rsp), %r13
	movq	48(%rsp), %r14
	movq	56(%rsp), %r15

	call	__rsaz_512_reduce
___
$code.=<<___ if ($addx);
	jmp	.Lmul_tail

.align	32
.Lmulx:
	movq	$bp, %rbp		# pass argument
	movq	($bp), %rdx		# pass b[0]
	call	__rsaz_512_mulx

	movq	%xmm0, $out
	movq	%xmm1, %rbp

	movq	128(%rsp), %rdx		# pull $n0
	movq	(%rsp), %r8
	movq	8(%rsp), %r9
	movq	16(%rsp), %r10
	movq	24(%rsp), %r11
	movq	32(%rsp), %r12
	movq	40(%rsp), %r13
	movq	48(%rsp), %r14
	movq	56(%rsp), %r15

	call	__rsaz_512_reducex
.Lmul_tail:
___
$code.=<<___;
	addq	64(%rsp), %r8
	adcq	72(%rsp), %r9
	adcq	80(%rsp), %r10
	adcq	88(%rsp), %r11
	adcq	96(%rsp), %r12
	adcq	104(%rsp), %r13
	adcq	112(%rsp), %r14
	adcq	120(%rsp), %r15
	sbbq	%rcx, %rcx

	call	__rsaz_512_subtract

	leaq	128+24+48(%rsp), %rax
.cfi_def_cfa	%rax,8
	movq	-48(%rax), %r15
.cfi_restore	%r15
	movq	-40(%rax), %r14
.cfi_restore	%r14
	movq	-32(%rax), %r13
.cfi_restore	%r13
	movq	-24(%rax), %r12
.cfi_restore	%r12
	movq	-16(%rax), %rbp
.cfi_restore	%rbp
	movq	-8(%rax), %rbx
.cfi_restore	%rbx
	leaq	(%rax), %rsp
.cfi_def_cfa_register	%rsp
.Lmul_epilogue:
	ret
.cfi_endproc
.size	rsaz_512_mul,.-rsaz_512_mul
___
}
{
my ($out,$ap,$bp,$mod,$n0,$pwr) = ("%rdi","%rsi","%rdx","%rcx","%r8","%r9d");
$code.=<<___;
.globl	rsaz_512_mul_gather4
.type	rsaz_512_mul_gather4,\@function,6
.align	32
rsaz_512_mul_gather4:
.cfi_startproc
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

	subq	\$`128+24+($win64?0xb0:0)`, %rsp
.cfi_adjust_cfa_offset	`128+24+($win64?0xb0:0)`
___
$code.=<<___	if ($win64);
	movaps	%xmm6,0xa0(%rsp)
	movaps	%xmm7,0xb0(%rsp)
	movaps	%xmm8,0xc0(%rsp)
	movaps	%xmm9,0xd0(%rsp)
	movaps	%xmm10,0xe0(%rsp)
	movaps	%xmm11,0xf0(%rsp)
	movaps	%xmm12,0x100(%rsp)
	movaps	%xmm13,0x110(%rsp)
	movaps	%xmm14,0x120(%rsp)
	movaps	%xmm15,0x130(%rsp)
___
$code.=<<___;
.Lmul_gather4_body:
	movd	$pwr,%xmm8
	movdqa	.Linc+16(%rip),%xmm1	# 00000002000000020000000200000002
	movdqa	.Linc(%rip),%xmm0	# 00000001000000010000000000000000

	pshufd	\$0,%xmm8,%xmm8		# broadcast $power
	movdqa	%xmm1,%xmm7
	movdqa	%xmm1,%xmm2
___
########################################################################
# calculate mask by comparing 0..15 to $power
#
for($i=0;$i<4;$i++) {
$code.=<<___;
	paddd	%xmm`$i`,%xmm`$i+1`
	pcmpeqd	%xmm8,%xmm`$i`
	movdqa	%xmm7,%xmm`$i+3`
___
}
for(;$i<7;$i++) {
$code.=<<___;
	paddd	%xmm`$i`,%xmm`$i+1`
	pcmpeqd	%xmm8,%xmm`$i`
___
}
$code.=<<___;
	pcmpeqd	%xmm8,%xmm7

	movdqa	16*0($bp),%xmm8
	movdqa	16*1($bp),%xmm9
	movdqa	16*2($bp),%xmm10
	movdqa	16*3($bp),%xmm11
	pand	%xmm0,%xmm8
	movdqa	16*4($bp),%xmm12
	pand	%xmm1,%xmm9
	movdqa	16*5($bp),%xmm13
	pand	%xmm2,%xmm10
	movdqa	16*6($bp),%xmm14
	pand	%xmm3,%xmm11
	movdqa	16*7($bp),%xmm15
	leaq	128($bp), %rbp
	pand	%xmm4,%xmm12
	pand	%xmm5,%xmm13
	pand	%xmm6,%xmm14
	pand	%xmm7,%xmm15
	por	%xmm10,%xmm8
	por	%xmm11,%xmm9
	por	%xmm12,%xmm8
	por	%xmm13,%xmm9
	por	%xmm14,%xmm8
	por	%xmm15,%xmm9

	por	%xmm9,%xmm8
	pshufd	\$0x4e,%xmm8,%xmm9
	por	%xmm9,%xmm8
___
$code.=<<___ if ($addx);
	movl	\$0x80100,%r11d
	andl	OPENSSL_ia32cap_P+8(%rip),%r11d
	cmpl	\$0x80100,%r11d		# check for MULX and ADO/CX
	je	.Lmulx_gather
___
$code.=<<___;
	movq	%xmm8,%rbx

	movq	$n0, 128(%rsp)		# off-load arguments
	movq	$out, 128+8(%rsp)
	movq	$mod, 128+16(%rsp)

	movq	($ap), %rax
	 movq	8($ap), %rcx
	mulq	%rbx			# 0 iteration
	movq	%rax, (%rsp)
	movq	%rcx, %rax
	movq	%rdx, %r8

	mulq	%rbx
	addq	%rax, %r8
	movq	16($ap), %rax
	movq	%rdx, %r9
	adcq	\$0, %r9

	mulq	%rbx
	addq	%rax, %r9
	movq	24($ap), %rax
	movq	%rdx, %r10
	adcq	\$0, %r10

	mulq	%rbx
	addq	%rax, %r10
	movq	32($ap), %rax
	movq	%rdx, %r11
	adcq	\$0, %r11

	mulq	%rbx
	addq	%rax, %r11
	movq	40($ap), %rax
	movq	%rdx, %r12
	adcq	\$0, %r12

	mulq	%rbx
	addq	%rax, %r12
	movq	48($ap), %rax
	movq	%rdx, %r13
	adcq	\$0, %r13

	mulq	%rbx
	addq	%rax, %r13
	movq	56($ap), %rax
	movq	%rdx, %r14
	adcq	\$0, %r14

	mulq	%rbx
	addq	%rax, %r14
	 movq	($ap), %rax
	movq	%rdx, %r15
	adcq	\$0, %r15

	leaq	8(%rsp), %rdi
	movl	\$7, %ecx
	jmp	.Loop_mul_gather

.align	32
.Loop_mul_gather:
	movdqa	16*0(%rbp),%xmm8
	movdqa	16*1(%rbp),%xmm9
	movdqa	16*2(%rbp),%xmm10
	movdqa	16*3(%rbp),%xmm11
	pand	%xmm0,%xmm8
	movdqa	16*4(%rbp),%xmm12
	pand	%xmm1,%xmm9
	movdqa	16*5(%rbp),%xmm13
	pand	%xmm2,%xmm10
	movdqa	16*6(%rbp),%xmm14
	pand	%xmm3,%xmm11
	movdqa	16*7(%rbp),%xmm15
	leaq	128(%rbp), %rbp
	pand	%xmm4,%xmm12
	pand	%xmm5,%xmm13
	pand	%xmm6,%xmm14
	pand	%xmm7,%xmm15
	por	%xmm10,%xmm8
	por	%xmm11,%xmm9
	por	%xmm12,%xmm8
	por	%xmm13,%xmm9
	por	%xmm14,%xmm8
	por	%xmm15,%xmm9

	por	%xmm9,%xmm8
	pshufd	\$0x4e,%xmm8,%xmm9
	por	%xmm9,%xmm8
	movq	%xmm8,%rbx

	mulq	%rbx
	addq	%rax, %r8
	movq	8($ap), %rax
	movq	%r8, (%rdi)
	movq	%rdx, %r8
	adcq	\$0, %r8

	mulq	%rbx
	addq	%rax, %r9
	movq	16($ap), %rax
	adcq	\$0, %rdx
	addq	%r9, %r8
	movq	%rdx, %r9
	adcq	\$0, %r9

	mulq	%rbx
	addq	%rax, %r10
	movq	24($ap), %rax
	adcq	\$0, %rdx
	addq	%r10, %r9
	movq	%rdx, %r10
	adcq	\$0, %r10

	mulq	%rbx
	addq	%rax, %r11
	movq	32($ap), %rax
	adcq	\$0, %rdx
	addq	%r11, %r10
	movq	%rdx, %r11
	adcq	\$0, %r11

	mulq	%rbx
	addq	%rax, %r12
	movq	40($ap), %rax
	adcq	\$0, %rdx
	addq	%r12, %r11
	movq	%rdx, %r12
	adcq	\$0, %r12

	mulq	%rbx
	addq	%rax, %r13
	movq	48($ap), %rax
	adcq	\$0, %rdx
	addq	%r13, %r12
	movq	%rdx, %r13
	adcq	\$0, %r13

	mulq	%rbx
	addq	%rax, %r14
	movq	56($ap), %rax
	adcq	\$0, %rdx
	addq	%r14, %r13
	movq	%rdx, %r14
	adcq	\$0, %r14

	mulq	%rbx
	addq	%rax, %r15
	 movq	($ap), %rax
	adcq	\$0, %rdx
	addq	%r15, %r14
	movq	%rdx, %r15
	adcq	\$0, %r15

	leaq	8(%rdi), %rdi

	decl	%ecx
	jnz	.Loop_mul_gather

	movq	%r8, (%rdi)
	movq	%r9, 8(%rdi)
	movq	%r10, 16(%rdi)
	movq	%r11, 24(%rdi)
	movq	%r12, 32(%rdi)
	movq	%r13, 40(%rdi)
	movq	%r14, 48(%rdi)
	movq	%r15, 56(%rdi)

	movq	128+8(%rsp), $out
	movq	128+16(%rsp), %rbp

	movq	(%rsp), %r8
	movq	8(%rsp), %r9
	movq	16(%rsp), %r10
	movq	24(%rsp), %r11
	movq	32(%rsp), %r12
	movq	40(%rsp), %r13
	movq	48(%rsp), %r14
	movq	56(%rsp), %r15

	call	__rsaz_512_reduce
___
$code.=<<___ if ($addx);
	jmp	.Lmul_gather_tail

.align	32
.Lmulx_gather:
	movq	%xmm8,%rdx

	mov	$n0, 128(%rsp)		# off-load arguments
	mov	$out, 128+8(%rsp)
	mov	$mod, 128+16(%rsp)

	mulx	($ap), %rbx, %r8	# 0 iteration
	mov	%rbx, (%rsp)
	xor	%edi, %edi		# cf=0, of=0

	mulx	8($ap), %rax, %r9

	mulx	16($ap), %rbx, %r10
	adcx	%rax, %r8

	mulx	24($ap), %rax, %r11
	adcx	%rbx, %r9

	mulx	32($ap), %rbx, %r12
	adcx	%rax, %r10

	mulx	40($ap), %rax, %r13
	adcx	%rbx, %r11

	mulx	48($ap), %rbx, %r14
	adcx	%rax, %r12

	mulx	56($ap), %rax, %r15
	adcx	%rbx, %r13
	adcx	%rax, %r14
	.byte	0x67
	mov	%r8, %rbx
	adcx	%rdi, %r15		# %rdi is 0

	mov	\$-7, %rcx
	jmp	.Loop_mulx_gather

.align	32
.Loop_mulx_gather:
	movdqa	16*0(%rbp),%xmm8
	movdqa	16*1(%rbp),%xmm9
	movdqa	16*2(%rbp),%xmm10
	movdqa	16*3(%rbp),%xmm11
	pand	%xmm0,%xmm8
	movdqa	16*4(%rbp),%xmm12
	pand	%xmm1,%xmm9
	movdqa	16*5(%rbp),%xmm13
	pand	%xmm2,%xmm10
	movdqa	16*6(%rbp),%xmm14
	pand	%xmm3,%xmm11
	movdqa	16*7(%rbp),%xmm15
	leaq	128(%rbp), %rbp
	pand	%xmm4,%xmm12
	pand	%xmm5,%xmm13
	pand	%xmm6,%xmm14
	pand	%xmm7,%xmm15
	por	%xmm10,%xmm8
	por	%xmm11,%xmm9
	por	%xmm12,%xmm8
	por	%xmm13,%xmm9
	por	%xmm14,%xmm8
	por	%xmm15,%xmm9

	por	%xmm9,%xmm8
	pshufd	\$0x4e,%xmm8,%xmm9
	por	%xmm9,%xmm8
	movq	%xmm8,%rdx

	.byte	0xc4,0x62,0xfb,0xf6,0x86,0x00,0x00,0x00,0x00	# mulx	($ap), %rax, %r8
	adcx	%rax, %rbx
	adox	%r9, %r8

	mulx	8($ap), %rax, %r9
	adcx	%rax, %r8
	adox	%r10, %r9

	mulx	16($ap), %rax, %r10
	adcx	%rax, %r9
	adox	%r11, %r10

	.byte	0xc4,0x62,0xfb,0xf6,0x9e,0x18,0x00,0x00,0x00	# mulx	24($ap), %rax, %r11
	adcx	%rax, %r10
	adox	%r12, %r11

	mulx	32($ap), %rax, %r12
	adcx	%rax, %r11
	adox	%r13, %r12

	mulx	40($ap), %rax, %r13
	adcx	%rax, %r12
	adox	%r14, %r13

	.byte	0xc4,0x62,0xfb,0xf6,0xb6,0x30,0x00,0x00,0x00	# mulx	48($ap), %rax, %r14
	adcx	%rax, %r13
	.byte	0x67
	adox	%r15, %r14

	mulx	56($ap), %rax, %r15
	 mov	%rbx, 64(%rsp,%rcx,8)
	adcx	%rax, %r14
	adox	%rdi, %r15
	mov	%r8, %rbx
	adcx	%rdi, %r15		# cf=0

	inc	%rcx			# of=0
	jnz	.Loop_mulx_gather

	mov	%r8, 64(%rsp)
	mov	%r9, 64+8(%rsp)
	mov	%r10, 64+16(%rsp)
	mov	%r11, 64+24(%rsp)
	mov	%r12, 64+32(%rsp)
	mov	%r13, 64+40(%rsp)
	mov	%r14, 64+48(%rsp)
	mov	%r15, 64+56(%rsp)

	mov	128(%rsp), %rdx		# pull arguments
	mov	128+8(%rsp), $out
	mov	128+16(%rsp), %rbp

	mov	(%rsp), %r8
	mov	8(%rsp), %r9
	mov	16(%rsp), %r10
	mov	24(%rsp), %r11
	mov	32(%rsp), %r12
	mov	40(%rsp), %r13
	mov	48(%rsp), %r14
	mov	56(%rsp), %r15

	call	__rsaz_512_reducex

.Lmul_gather_tail:
___
$code.=<<___;
	addq	64(%rsp), %r8
	adcq	72(%rsp), %r9
	adcq	80(%rsp), %r10
	adcq	88(%rsp), %r11
	adcq	96(%rsp), %r12
	adcq	104(%rsp), %r13
	adcq	112(%rsp), %r14
	adcq	120(%rsp), %r15
	sbbq	%rcx, %rcx

	call	__rsaz_512_subtract

	leaq	128+24+48(%rsp), %rax
___
$code.=<<___	if ($win64);
	movaps	0xa0-0xc8(%rax),%xmm6
	movaps	0xb0-0xc8(%rax),%xmm7
	movaps	0xc0-0xc8(%rax),%xmm8
	movaps	0xd0-0xc8(%rax),%xmm9
	movaps	0xe0-0xc8(%rax),%xmm10
	movaps	0xf0-0xc8(%rax),%xmm11
	movaps	0x100-0xc8(%rax),%xmm12
	movaps	0x110-0xc8(%rax),%xmm13
	movaps	0x120-0xc8(%rax),%xmm14
	movaps	0x130-0xc8(%rax),%xmm15
	lea	0xb0(%rax),%rax
___
$code.=<<___;
.cfi_def_cfa	%rax,8
	movq	-48(%rax), %r15
.cfi_restore	%r15
	movq	-40(%rax), %r14
.cfi_restore	%r14
	movq	-32(%rax), %r13
.cfi_restore	%r13
	movq	-24(%rax), %r12
.cfi_restore	%r12
	movq	-16(%rax), %rbp
.cfi_restore	%rbp
	movq	-8(%rax), %rbx
.cfi_restore	%rbx
	leaq	(%rax), %rsp
.cfi_def_cfa_register	%rsp
.Lmul_gather4_epilogue:
	ret
.cfi_endproc
.size	rsaz_512_mul_gather4,.-rsaz_512_mul_gather4
___
}
{
my ($out,$ap,$mod,$n0,$tbl,$pwr) = ("%rdi","%rsi","%rdx","%rcx","%r8","%r9d");
$code.=<<___;
.globl	rsaz_512_mul_scatter4
.type	rsaz_512_mul_scatter4,\@function,6
.align	32
rsaz_512_mul_scatter4:
.cfi_startproc
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

	mov	$pwr, $pwr
	subq	\$128+24, %rsp
.cfi_adjust_cfa_offset	128+24
.Lmul_scatter4_body:
	leaq	($tbl,$pwr,8), $tbl
	movq	$out, %xmm0		# off-load arguments
	movq	$mod, %xmm1
	movq	$tbl, %xmm2
	movq	$n0, 128(%rsp)

	movq	$out, %rbp
___
$code.=<<___ if ($addx);
	movl	\$0x80100,%r11d
	andl	OPENSSL_ia32cap_P+8(%rip),%r11d
	cmpl	\$0x80100,%r11d		# check for MULX and ADO/CX
	je	.Lmulx_scatter
___
$code.=<<___;
	movq	($out),%rbx		# pass b[0]
	call	__rsaz_512_mul

	movq	%xmm0, $out
	movq	%xmm1, %rbp

	movq	(%rsp), %r8
	movq	8(%rsp), %r9
	movq	16(%rsp), %r10
	movq	24(%rsp), %r11
	movq	32(%rsp), %r12
	movq	40(%rsp), %r13
	movq	48(%rsp), %r14
	movq	56(%rsp), %r15

	call	__rsaz_512_reduce
___
$code.=<<___ if ($addx);
	jmp	.Lmul_scatter_tail

.align	32
.Lmulx_scatter:
	movq	($out), %rdx		# pass b[0]
	call	__rsaz_512_mulx

	movq	%xmm0, $out
	movq	%xmm1, %rbp

	movq	128(%rsp), %rdx		# pull $n0
	movq	(%rsp), %r8
	movq	8(%rsp), %r9
	movq	16(%rsp), %r10
	movq	24(%rsp), %r11
	movq	32(%rsp), %r12
	movq	40(%rsp), %r13
	movq	48(%rsp), %r14
	movq	56(%rsp), %r15

	call	__rsaz_512_reducex

.Lmul_scatter_tail:
___
$code.=<<___;
	addq	64(%rsp), %r8
	adcq	72(%rsp), %r9
	adcq	80(%rsp), %r10
	adcq	88(%rsp), %r11
	adcq	96(%rsp), %r12
	adcq	104(%rsp), %r13
	adcq	112(%rsp), %r14
	adcq	120(%rsp), %r15
	movq	%xmm2, $inp
	sbbq	%rcx, %rcx

	call	__rsaz_512_subtract

	movq	%r8, 128*0($inp)	# scatter
	movq	%r9, 128*1($inp)
	movq	%r10, 128*2($inp)
	movq	%r11, 128*3($inp)
	movq	%r12, 128*4($inp)
	movq	%r13, 128*5($inp)
	movq	%r14, 128*6($inp)
	movq	%r15, 128*7($inp)

	leaq	128+24+48(%rsp), %rax
.cfi_def_cfa	%rax,8
	movq	-48(%rax), %r15
.cfi_restore	%r15
	movq	-40(%rax), %r14
.cfi_restore	%r14
	movq	-32(%rax), %r13
.cfi_restore	%r13
	movq	-24(%rax), %r12
.cfi_restore	%r12
	movq	-16(%rax), %rbp
.cfi_restore	%rbp
	movq	-8(%rax), %rbx
.cfi_restore	%rbx
	leaq	(%rax), %rsp
.cfi_def_cfa_register	%rsp
.Lmul_scatter4_epilogue:
	ret
.cfi_endproc
.size	rsaz_512_mul_scatter4,.-rsaz_512_mul_scatter4
___
}
{
my ($out,$inp,$mod,$n0) = ("%rdi","%rsi","%rdx","%rcx");
$code.=<<___;
.globl	rsaz_512_mul_by_one
.type	rsaz_512_mul_by_one,\@function,4
.align	32
rsaz_512_mul_by_one:
.cfi_startproc
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

	subq	\$128+24, %rsp
.cfi_adjust_cfa_offset	128+24
.Lmul_by_one_body:
___
$code.=<<___ if ($addx);
	movl	OPENSSL_ia32cap_P+8(%rip),%eax
___
$code.=<<___;
	movq	$mod, %rbp	# reassign argument
	movq	$n0, 128(%rsp)

	movq	($inp), %r8
	pxor	%xmm0, %xmm0
	movq	8($inp), %r9
	movq	16($inp), %r10
	movq	24($inp), %r11
	movq	32($inp), %r12
	movq	40($inp), %r13
	movq	48($inp), %r14
	movq	56($inp), %r15

	movdqa	%xmm0, (%rsp)
	movdqa	%xmm0, 16(%rsp)
	movdqa	%xmm0, 32(%rsp)
	movdqa	%xmm0, 48(%rsp)
	movdqa	%xmm0, 64(%rsp)
	movdqa	%xmm0, 80(%rsp)
	movdqa	%xmm0, 96(%rsp)
___
$code.=<<___ if ($addx);
	andl	\$0x80100,%eax
	cmpl	\$0x80100,%eax		# check for MULX and ADO/CX
	je	.Lby_one_callx
___
$code.=<<___;
	call	__rsaz_512_reduce
___
$code.=<<___ if ($addx);
	jmp	.Lby_one_tail
.align	32
.Lby_one_callx:
	movq	128(%rsp), %rdx		# pull $n0
	call	__rsaz_512_reducex
.Lby_one_tail:
___
$code.=<<___;
	movq	%r8, ($out)
	movq	%r9, 8($out)
	movq	%r10, 16($out)
	movq	%r11, 24($out)
	movq	%r12, 32($out)
	movq	%r13, 40($out)
	movq	%r14, 48($out)
	movq	%r15, 56($out)

	leaq	128+24+48(%rsp), %rax
.cfi_def_cfa	%rax,8
	movq	-48(%rax), %r15
.cfi_restore	%r15
	movq	-40(%rax), %r14
.cfi_restore	%r14
	movq	-32(%rax), %r13
.cfi_restore	%r13
	movq	-24(%rax), %r12
.cfi_restore	%r12
	movq	-16(%rax), %rbp
.cfi_restore	%rbp
	movq	-8(%rax), %rbx
.cfi_restore	%rbx
	leaq	(%rax), %rsp
.cfi_def_cfa_register	%rsp
.Lmul_by_one_epilogue:
	ret
.cfi_endproc
.size	rsaz_512_mul_by_one,.-rsaz_512_mul_by_one
___
}
{	# __rsaz_512_reduce
	#
	# input:	%r8-%r15, %rbp - mod, 128(%rsp) - n0
	# output:	%r8-%r15
	# clobbers:	everything except %rbp and %rdi
$code.=<<___;
.type	__rsaz_512_reduce,\@abi-omnipotent
.align	32
__rsaz_512_reduce:
	movq	%r8, %rbx
	imulq	128+8(%rsp), %rbx
	movq	0(%rbp), %rax
	movl	\$8, %ecx
	jmp	.Lreduction_loop

.align	32
.Lreduction_loop:
	mulq	%rbx
	movq	8(%rbp), %rax
	negq	%r8
	movq	%rdx, %r8
	adcq	\$0, %r8

	mulq	%rbx
	addq	%rax, %r9
	movq	16(%rbp), %rax
	adcq	\$0, %rdx
	addq	%r9, %r8
	movq	%rdx, %r9
	adcq	\$0, %r9

	mulq	%rbx
	addq	%rax, %r10
	movq	24(%rbp), %rax
	adcq	\$0, %rdx
	addq	%r10, %r9
	movq	%rdx, %r10
	adcq	\$0, %r10

	mulq	%rbx
	addq	%rax, %r11
	movq	32(%rbp), %rax
	adcq	\$0, %rdx
	addq	%r11, %r10
	 movq	128+8(%rsp), %rsi
	#movq	%rdx, %r11
	#adcq	\$0, %r11
	adcq	\$0, %rdx
	movq	%rdx, %r11

	mulq	%rbx
	addq	%rax, %r12
	movq	40(%rbp), %rax
	adcq	\$0, %rdx
	 imulq	%r8, %rsi
	addq	%r12, %r11
	movq	%rdx, %r12
	adcq	\$0, %r12

	mulq	%rbx
	addq	%rax, %r13
	movq	48(%rbp), %rax
	adcq	\$0, %rdx
	addq	%r13, %r12
	movq	%rdx, %r13
	adcq	\$0, %r13

	mulq	%rbx
	addq	%rax, %r14
	movq	56(%rbp), %rax
	adcq	\$0, %rdx
	addq	%r14, %r13
	movq	%rdx, %r14
	adcq	\$0, %r14

	mulq	%rbx
	 movq	%rsi, %rbx
	addq	%rax, %r15
	 movq	0(%rbp), %rax
	adcq	\$0, %rdx
	addq	%r15, %r14
	movq	%rdx, %r15
	adcq	\$0, %r15

	decl	%ecx
	jne	.Lreduction_loop

	ret
.size	__rsaz_512_reduce,.-__rsaz_512_reduce
___
}
if ($addx) {
	# __rsaz_512_reducex
	#
	# input:	%r8-%r15, %rbp - mod, 128(%rsp) - n0
	# output:	%r8-%r15
	# clobbers:	everything except %rbp and %rdi
$code.=<<___;
.type	__rsaz_512_reducex,\@abi-omnipotent
.align	32
__rsaz_512_reducex:
	#movq	128+8(%rsp), %rdx		# pull $n0
	imulq	%r8, %rdx
	xorq	%rsi, %rsi			# cf=0,of=0
	movl	\$8, %ecx
	jmp	.Lreduction_loopx

.align	32
.Lreduction_loopx:
	mov	%r8, %rbx
	mulx	0(%rbp), %rax, %r8
	adcx	%rbx, %rax
	adox	%r9, %r8

	mulx	8(%rbp), %rax, %r9
	adcx	%rax, %r8
	adox	%r10, %r9

	mulx	16(%rbp), %rbx, %r10
	adcx	%rbx, %r9
	adox	%r11, %r10

	mulx	24(%rbp), %rbx, %r11
	adcx	%rbx, %r10
	adox	%r12, %r11

	.byte	0xc4,0x62,0xe3,0xf6,0xa5,0x20,0x00,0x00,0x00	# mulx	32(%rbp), %rbx, %r12
	 mov	%rdx, %rax
	 mov	%r8, %rdx
	adcx	%rbx, %r11
	adox	%r13, %r12

	 mulx	128+8(%rsp), %rbx, %rdx
	 mov	%rax, %rdx

	mulx	40(%rbp), %rax, %r13
	adcx	%rax, %r12
	adox	%r14, %r13

	.byte	0xc4,0x62,0xfb,0xf6,0xb5,0x30,0x00,0x00,0x00	# mulx	48(%rbp), %rax, %r14
	adcx	%rax, %r13
	adox	%r15, %r14

	mulx	56(%rbp), %rax, %r15
	 mov	%rbx, %rdx
	adcx	%rax, %r14
	adox	%rsi, %r15			# %rsi is 0
	adcx	%rsi, %r15			# cf=0

	decl	%ecx				# of=0
	jne	.Lreduction_loopx

	ret
.size	__rsaz_512_reducex,.-__rsaz_512_reducex
___
}
{	# __rsaz_512_subtract
	# input: %r8-%r15, %rdi - $out, %rbp - $mod, %rcx - mask
	# output:
	# clobbers: everything but %rdi, %rsi and %rbp
$code.=<<___;
.type	__rsaz_512_subtract,\@abi-omnipotent
.align	32
__rsaz_512_subtract:
	movq	%r8, ($out)
	movq	%r9, 8($out)
	movq	%r10, 16($out)
	movq	%r11, 24($out)
	movq	%r12, 32($out)
	movq	%r13, 40($out)
	movq	%r14, 48($out)
	movq	%r15, 56($out)

	movq	0($mod), %r8
	movq	8($mod), %r9
	negq	%r8
	notq	%r9
	andq	%rcx, %r8
	movq	16($mod), %r10
	andq	%rcx, %r9
	notq	%r10
	movq	24($mod), %r11
	andq	%rcx, %r10
	notq	%r11
	movq	32($mod), %r12
	andq	%rcx, %r11
	notq	%r12
	movq	40($mod), %r13
	andq	%rcx, %r12
	notq	%r13
	movq	48($mod), %r14
	andq	%rcx, %r13
	notq	%r14
	movq	56($mod), %r15
	andq	%rcx, %r14
	notq	%r15
	andq	%rcx, %r15

	addq	($out), %r8
	adcq	8($out), %r9
	adcq	16($out), %r10
	adcq	24($out), %r11
	adcq	32($out), %r12
	adcq	40($out), %r13
	adcq	48($out), %r14
	adcq	56($out), %r15

	movq	%r8, ($out)
	movq	%r9, 8($out)
	movq	%r10, 16($out)
	movq	%r11, 24($out)
	movq	%r12, 32($out)
	movq	%r13, 40($out)
	movq	%r14, 48($out)
	movq	%r15, 56($out)

	ret
.size	__rsaz_512_subtract,.-__rsaz_512_subtract
___
}
{	# __rsaz_512_mul
	#
	# input: %rsi - ap, %rbp - bp
	# output:
	# clobbers: everything
my ($ap,$bp) = ("%rsi","%rbp");
$code.=<<___;
.type	__rsaz_512_mul,\@abi-omnipotent
.align	32
__rsaz_512_mul:
	leaq	8(%rsp), %rdi

	movq	($ap), %rax
	mulq	%rbx
	movq	%rax, (%rdi)
	movq	8($ap), %rax
	movq	%rdx, %r8

	mulq	%rbx
	addq	%rax, %r8
	movq	16($ap), %rax
	movq	%rdx, %r9
	adcq	\$0, %r9

	mulq	%rbx
	addq	%rax, %r9
	movq	24($ap), %rax
	movq	%rdx, %r10
	adcq	\$0, %r10

	mulq	%rbx
	addq	%rax, %r10
	movq	32($ap), %rax
	movq	%rdx, %r11
	adcq	\$0, %r11

	mulq	%rbx
	addq	%rax, %r11
	movq	40($ap), %rax
	movq	%rdx, %r12
	adcq	\$0, %r12

	mulq	%rbx
	addq	%rax, %r12
	movq	48($ap), %rax
	movq	%rdx, %r13
	adcq	\$0, %r13

	mulq	%rbx
	addq	%rax, %r13
	movq	56($ap), %rax
	movq	%rdx, %r14
	adcq	\$0, %r14

	mulq	%rbx
	addq	%rax, %r14
	 movq	($ap), %rax
	movq	%rdx, %r15
	adcq	\$0, %r15

	leaq	8($bp), $bp
	leaq	8(%rdi), %rdi

	movl	\$7, %ecx
	jmp	.Loop_mul

.align	32
.Loop_mul:
	movq	($bp), %rbx
	mulq	%rbx
	addq	%rax, %r8
	movq	8($ap), %rax
	movq	%r8, (%rdi)
	movq	%rdx, %r8
	adcq	\$0, %r8

	mulq	%rbx
	addq	%rax, %r9
	movq	16($ap), %rax
	adcq	\$0, %rdx
	addq	%r9, %r8
	movq	%rdx, %r9
	adcq	\$0, %r9

	mulq	%rbx
	addq	%rax, %r10
	movq	24($ap), %rax
	adcq	\$0, %rdx
	addq	%r10, %r9
	movq	%rdx, %r10
	adcq	\$0, %r10

	mulq	%rbx
	addq	%rax, %r11
	movq	32($ap), %rax
	adcq	\$0, %rdx
	addq	%r11, %r10
	movq	%rdx, %r11
	adcq	\$0, %r11

	mulq	%rbx
	addq	%rax, %r12
	movq	40($ap), %rax
	adcq	\$0, %rdx
	addq	%r12, %r11
	movq	%rdx, %r12
	adcq	\$0, %r12

	mulq	%rbx
	addq	%rax, %r13
	movq	48($ap), %rax
	adcq	\$0, %rdx
	addq	%r13, %r12
	movq	%rdx, %r13
	adcq	\$0, %r13

	mulq	%rbx
	addq	%rax, %r14
	movq	56($ap), %rax
	adcq	\$0, %rdx
	addq	%r14, %r13
	movq	%rdx, %r14
	 leaq	8($bp), $bp
	adcq	\$0, %r14

	mulq	%rbx
	addq	%rax, %r15
	 movq	($ap), %rax
	adcq	\$0, %rdx
	addq	%r15, %r14
	movq	%rdx, %r15
	adcq	\$0, %r15

	leaq	8(%rdi), %rdi

	decl	%ecx
	jnz	.Loop_mul

	movq	%r8, (%rdi)
	movq	%r9, 8(%rdi)
	movq	%r10, 16(%rdi)
	movq	%r11, 24(%rdi)
	movq	%r12, 32(%rdi)
	movq	%r13, 40(%rdi)
	movq	%r14, 48(%rdi)
	movq	%r15, 56(%rdi)

	ret
.size	__rsaz_512_mul,.-__rsaz_512_mul
___
}
if ($addx) {
	# __rsaz_512_mulx
	#
	# input: %rsi - ap, %rbp - bp
	# output:
	# clobbers: everything
my ($ap,$bp,$zero) = ("%rsi","%rbp","%rdi");
$code.=<<___;
.type	__rsaz_512_mulx,\@abi-omnipotent
.align	32
__rsaz_512_mulx:
	mulx	($ap), %rbx, %r8	# initial %rdx preloaded by caller
	mov	\$-6, %rcx

	mulx	8($ap), %rax, %r9
	movq	%rbx, 8(%rsp)

	mulx	16($ap), %rbx, %r10
	adc	%rax, %r8

	mulx	24($ap), %rax, %r11
	adc	%rbx, %r9

	mulx	32($ap), %rbx, %r12
	adc	%rax, %r10

	mulx	40($ap), %rax, %r13
	adc	%rbx, %r11

	mulx	48($ap), %rbx, %r14
	adc	%rax, %r12

	mulx	56($ap), %rax, %r15
	 mov	8($bp), %rdx
	adc	%rbx, %r13
	adc	%rax, %r14
	adc	\$0, %r15

	xor	$zero, $zero		# cf=0,of=0
	jmp	.Loop_mulx

.align	32
.Loop_mulx:
	movq	%r8, %rbx
	mulx	($ap), %rax, %r8
	adcx	%rax, %rbx
	adox	%r9, %r8

	mulx	8($ap), %rax, %r9
	adcx	%rax, %r8
	adox	%r10, %r9

	mulx	16($ap), %rax, %r10
	adcx	%rax, %r9
	adox	%r11, %r10

	mulx	24($ap), %rax, %r11
	adcx	%rax, %r10
	adox	%r12, %r11

	.byte	0x3e,0xc4,0x62,0xfb,0xf6,0xa6,0x20,0x00,0x00,0x00	# mulx	32($ap), %rax, %r12
	adcx	%rax, %r11
	adox	%r13, %r12

	mulx	40($ap), %rax, %r13
	adcx	%rax, %r12
	adox	%r14, %r13

	mulx	48($ap), %rax, %r14
	adcx	%rax, %r13
	adox	%r15, %r14

	mulx	56($ap), %rax, %r15
	 movq	64($bp,%rcx,8), %rdx
	 movq	%rbx, 8+64-8(%rsp,%rcx,8)
	adcx	%rax, %r14
	adox	$zero, %r15
	adcx	$zero, %r15		# cf=0

	inc	%rcx			# of=0
	jnz	.Loop_mulx

	movq	%r8, %rbx
	mulx	($ap), %rax, %r8
	adcx	%rax, %rbx
	adox	%r9, %r8

	.byte	0xc4,0x62,0xfb,0xf6,0x8e,0x08,0x00,0x00,0x00	# mulx	8($ap), %rax, %r9
	adcx	%rax, %r8
	adox	%r10, %r9

	.byte	0xc4,0x62,0xfb,0xf6,0x96,0x10,0x00,0x00,0x00	# mulx	16($ap), %rax, %r10
	adcx	%rax, %r9
	adox	%r11, %r10

	mulx	24($ap), %rax, %r11
	adcx	%rax, %r10
	adox	%r12, %r11

	mulx	32($ap), %rax, %r12
	adcx	%rax, %r11
	adox	%r13, %r12

	mulx	40($ap), %rax, %r13
	adcx	%rax, %r12
	adox	%r14, %r13

	.byte	0xc4,0x62,0xfb,0xf6,0xb6,0x30,0x00,0x00,0x00	# mulx	48($ap), %rax, %r14
	adcx	%rax, %r13
	adox	%r15, %r14

	.byte	0xc4,0x62,0xfb,0xf6,0xbe,0x38,0x00,0x00,0x00	# mulx	56($ap), %rax, %r15
	adcx	%rax, %r14
	adox	$zero, %r15
	adcx	$zero, %r15

	mov	%rbx, 8+64-8(%rsp)
	mov	%r8, 8+64(%rsp)
	mov	%r9, 8+64+8(%rsp)
	mov	%r10, 8+64+16(%rsp)
	mov	%r11, 8+64+24(%rsp)
	mov	%r12, 8+64+32(%rsp)
	mov	%r13, 8+64+40(%rsp)
	mov	%r14, 8+64+48(%rsp)
	mov	%r15, 8+64+56(%rsp)

	ret
.size	__rsaz_512_mulx,.-__rsaz_512_mulx
___
}
{
my ($out,$inp,$power)= $win64 ? ("%rcx","%rdx","%r8d") : ("%rdi","%rsi","%edx");
$code.=<<___;
.globl	rsaz_512_scatter4
.type	rsaz_512_scatter4,\@abi-omnipotent
.align	16
rsaz_512_scatter4:
	leaq	($out,$power,8), $out
	movl	\$8, %r9d
	jmp	.Loop_scatter
.align	16
.Loop_scatter:
	movq	($inp), %rax
	leaq	8($inp), $inp
	movq	%rax, ($out)
	leaq	128($out), $out
	decl	%r9d
	jnz	.Loop_scatter
	ret
.size	rsaz_512_scatter4,.-rsaz_512_scatter4

.globl	rsaz_512_gather4
.type	rsaz_512_gather4,\@abi-omnipotent
.align	16
rsaz_512_gather4:
___
$code.=<<___	if ($win64);
.LSEH_begin_rsaz_512_gather4:
	.byte	0x48,0x81,0xec,0xa8,0x00,0x00,0x00	# sub    $0xa8,%rsp
	.byte	0x0f,0x29,0x34,0x24			# movaps %xmm6,(%rsp)
	.byte	0x0f,0x29,0x7c,0x24,0x10		# movaps %xmm7,0x10(%rsp)
	.byte	0x44,0x0f,0x29,0x44,0x24,0x20		# movaps %xmm8,0x20(%rsp)
	.byte	0x44,0x0f,0x29,0x4c,0x24,0x30		# movaps %xmm9,0x30(%rsp)
	.byte	0x44,0x0f,0x29,0x54,0x24,0x40		# movaps %xmm10,0x40(%rsp)
	.byte	0x44,0x0f,0x29,0x5c,0x24,0x50		# movaps %xmm11,0x50(%rsp)
	.byte	0x44,0x0f,0x29,0x64,0x24,0x60		# movaps %xmm12,0x60(%rsp)
	.byte	0x44,0x0f,0x29,0x6c,0x24,0x70		# movaps %xmm13,0x70(%rsp)
	.byte	0x44,0x0f,0x29,0xb4,0x24,0x80,0,0,0	# movaps %xmm14,0x80(%rsp)
	.byte	0x44,0x0f,0x29,0xbc,0x24,0x90,0,0,0	# movaps %xmm15,0x90(%rsp)
___
$code.=<<___;
	movd	$power,%xmm8
	movdqa	.Linc+16(%rip),%xmm1	# 00000002000000020000000200000002
	movdqa	.Linc(%rip),%xmm0	# 00000001000000010000000000000000

	pshufd	\$0,%xmm8,%xmm8		# broadcast $power
	movdqa	%xmm1,%xmm7
	movdqa	%xmm1,%xmm2
___
########################################################################
# calculate mask by comparing 0..15 to $power
#
for($i=0;$i<4;$i++) {
$code.=<<___;
	paddd	%xmm`$i`,%xmm`$i+1`
	pcmpeqd	%xmm8,%xmm`$i`
	movdqa	%xmm7,%xmm`$i+3`
___
}
for(;$i<7;$i++) {
$code.=<<___;
	paddd	%xmm`$i`,%xmm`$i+1`
	pcmpeqd	%xmm8,%xmm`$i`
___
}
$code.=<<___;
	pcmpeqd	%xmm8,%xmm7
	movl	\$8, %r9d
	jmp	.Loop_gather
.align	16
.Loop_gather:
	movdqa	16*0($inp),%xmm8
	movdqa	16*1($inp),%xmm9
	movdqa	16*2($inp),%xmm10
	movdqa	16*3($inp),%xmm11
	pand	%xmm0,%xmm8
	movdqa	16*4($inp),%xmm12
	pand	%xmm1,%xmm9
	movdqa	16*5($inp),%xmm13
	pand	%xmm2,%xmm10
	movdqa	16*6($inp),%xmm14
	pand	%xmm3,%xmm11
	movdqa	16*7($inp),%xmm15
	leaq	128($inp), $inp
	pand	%xmm4,%xmm12
	pand	%xmm5,%xmm13
	pand	%xmm6,%xmm14
	pand	%xmm7,%xmm15
	por	%xmm10,%xmm8
	por	%xmm11,%xmm9
	por	%xmm12,%xmm8
	por	%xmm13,%xmm9
	por	%xmm14,%xmm8
	por	%xmm15,%xmm9

	por	%xmm9,%xmm8
	pshufd	\$0x4e,%xmm8,%xmm9
	por	%xmm9,%xmm8
	movq	%xmm8,($out)
	leaq	8($out), $out
	decl	%r9d
	jnz	.Loop_gather
___
$code.=<<___	if ($win64);
	movaps	0x00(%rsp),%xmm6
	movaps	0x10(%rsp),%xmm7
	movaps	0x20(%rsp),%xmm8
	movaps	0x30(%rsp),%xmm9
	movaps	0x40(%rsp),%xmm10
	movaps	0x50(%rsp),%xmm11
	movaps	0x60(%rsp),%xmm12
	movaps	0x70(%rsp),%xmm13
	movaps	0x80(%rsp),%xmm14
	movaps	0x90(%rsp),%xmm15
	add	\$0xa8,%rsp
___
$code.=<<___;
	ret
.LSEH_end_rsaz_512_gather4:
.size	rsaz_512_gather4,.-rsaz_512_gather4

.align	64
.Linc:
	.long	0,0, 1,1
	.long	2,2, 2,2
___
}

# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
if ($win64) {
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___;
.extern	__imp_RtlVirtualUnwind
.type	se_handler,\@abi-omnipotent
.align	16
se_handler:
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

	lea	128+24+48(%rax),%rax

	lea	.Lmul_gather4_epilogue(%rip),%rbx
	cmp	%r10,%rbx
	jne	.Lse_not_in_mul_gather4

	lea	0xb0(%rax),%rax

	lea	-48-0xa8(%rax),%rsi
	lea	512($context),%rdi
	mov	\$20,%ecx
	.long	0xa548f3fc		# cld; rep movsq

.Lse_not_in_mul_gather4:
	mov	-8(%rax),%rbx
	mov	-16(%rax),%rbp
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
.size	se_handler,.-se_handler

.section	.pdata
.align	4
	.rva	.LSEH_begin_rsaz_512_sqr
	.rva	.LSEH_end_rsaz_512_sqr
	.rva	.LSEH_info_rsaz_512_sqr

	.rva	.LSEH_begin_rsaz_512_mul
	.rva	.LSEH_end_rsaz_512_mul
	.rva	.LSEH_info_rsaz_512_mul

	.rva	.LSEH_begin_rsaz_512_mul_gather4
	.rva	.LSEH_end_rsaz_512_mul_gather4
	.rva	.LSEH_info_rsaz_512_mul_gather4

	.rva	.LSEH_begin_rsaz_512_mul_scatter4
	.rva	.LSEH_end_rsaz_512_mul_scatter4
	.rva	.LSEH_info_rsaz_512_mul_scatter4

	.rva	.LSEH_begin_rsaz_512_mul_by_one
	.rva	.LSEH_end_rsaz_512_mul_by_one
	.rva	.LSEH_info_rsaz_512_mul_by_one

	.rva	.LSEH_begin_rsaz_512_gather4
	.rva	.LSEH_end_rsaz_512_gather4
	.rva	.LSEH_info_rsaz_512_gather4

.section	.xdata
.align	8
.LSEH_info_rsaz_512_sqr:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lsqr_body,.Lsqr_epilogue			# HandlerData[]
.LSEH_info_rsaz_512_mul:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lmul_body,.Lmul_epilogue			# HandlerData[]
.LSEH_info_rsaz_512_mul_gather4:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lmul_gather4_body,.Lmul_gather4_epilogue	# HandlerData[]
.LSEH_info_rsaz_512_mul_scatter4:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lmul_scatter4_body,.Lmul_scatter4_epilogue	# HandlerData[]
.LSEH_info_rsaz_512_mul_by_one:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lmul_by_one_body,.Lmul_by_one_epilogue		# HandlerData[]
.LSEH_info_rsaz_512_gather4:
	.byte	0x01,0x46,0x16,0x00
	.byte	0x46,0xf8,0x09,0x00	# vmovaps 0x90(rsp),xmm15
	.byte	0x3d,0xe8,0x08,0x00	# vmovaps 0x80(rsp),xmm14
	.byte	0x34,0xd8,0x07,0x00	# vmovaps 0x70(rsp),xmm13
	.byte	0x2e,0xc8,0x06,0x00	# vmovaps 0x60(rsp),xmm12
	.byte	0x28,0xb8,0x05,0x00	# vmovaps 0x50(rsp),xmm11
	.byte	0x22,0xa8,0x04,0x00	# vmovaps 0x40(rsp),xmm10
	.byte	0x1c,0x98,0x03,0x00	# vmovaps 0x30(rsp),xmm9
	.byte	0x16,0x88,0x02,0x00	# vmovaps 0x20(rsp),xmm8
	.byte	0x10,0x78,0x01,0x00	# vmovaps 0x10(rsp),xmm7
	.byte	0x0b,0x68,0x00,0x00	# vmovaps 0x00(rsp),xmm6
	.byte	0x07,0x01,0x15,0x00	# sub     rsp,0xa8
___
}

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

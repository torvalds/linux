#!/usr/bin/env perl
# Copyright 2018 The OpenSSL Project Authors. All Rights Reserved.
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
# X25519 lower-level primitives for x86_64.
#
# February 2018.
#
# This module implements radix 2^51 multiplication and squaring, and
# radix 2^64 multiplication, squaring, addition, subtraction and final
# reduction. Latter radix is used on ADCX/ADOX-capable processors such
# as Broadwell. On related note one should mention that there are
# vector implementations that provide significantly better performance
# on some processors(*), but they are large and overly complex. Which
# in combination with them being effectively processor-specific makes
# the undertaking hard to justify. The goal for this implementation
# is rather versatility and simplicity [and ultimately formal
# verification].
#
# (*)	For example sandy2x should provide ~30% improvement on Sandy
#	Bridge, but only nominal ~5% on Haswell [and big loss on
#	Broadwell and successors].
#
######################################################################
# Improvement coefficients:
#
#			amd64-51(*)	gcc-5.x(**)
#
# P4			+22%		+40%
# Sandy Bridge		-3%		+11%
# Haswell		-1%		+13%
# Broadwell(***)	+30%		+35%
# Skylake(***)		+33%		+47%
# Silvermont		+20%		+26%
# Goldmont		+40%		+50%
# Bulldozer		+20%		+9%
# Ryzen(***)		+43%		+40%
# VIA			+170%		+120%
#
# (*)	amd64-51 is popular assembly implementation with 2^51 radix,
#	only multiplication and squaring subroutines were linked
#	for comparison, but not complete ladder step; gain on most
#	processors is because this module refrains from shld, and
#	minor regression on others is because this does result in
#	higher instruction count;
# (**)	compiler is free to inline functions, in assembly one would
#	need to implement ladder step to do that, and it will improve
#	performance by several percent;
# (***)	ADCX/ADOX result for 2^64 radix, there is no corresponding
#	C implementation, so that comparison is always against
#	2^51 radix;

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

$code.=<<___;
.text

.globl	x25519_fe51_mul
.type	x25519_fe51_mul,\@function,3
.align	32
x25519_fe51_mul:
.cfi_startproc
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
	lea	-8*5(%rsp),%rsp
.cfi_adjust_cfa_offset	40
.Lfe51_mul_body:

	mov	8*0(%rsi),%rax		# f[0]
	mov	8*0(%rdx),%r11		# load g[0-4]
	mov	8*1(%rdx),%r12
	mov	8*2(%rdx),%r13
	mov	8*3(%rdx),%rbp
	mov	8*4(%rdx),%r14

	mov	%rdi,8*4(%rsp)		# offload 1st argument
	mov	%rax,%rdi
	mulq	%r11			# f[0]*g[0]
	mov	%r11,8*0(%rsp)		# offload g[0]
	mov	%rax,%rbx		# %rbx:%rcx = h0
	mov	%rdi,%rax
	mov	%rdx,%rcx
	mulq	%r12			# f[0]*g[1]
	mov	%r12,8*1(%rsp)		# offload g[1]
	mov	%rax,%r8		# %r8:%r9 = h1
	mov	%rdi,%rax
	lea	(%r14,%r14,8),%r15
	mov	%rdx,%r9
	mulq	%r13			# f[0]*g[2]
	mov	%r13,8*2(%rsp)		# offload g[2]
	mov	%rax,%r10		# %r10:%r11 = h2
	mov	%rdi,%rax
	lea	(%r14,%r15,2),%rdi	# g[4]*19
	mov	%rdx,%r11
	mulq	%rbp			# f[0]*g[3]
	mov	%rax,%r12		# %r12:%r13 = h3
	mov	8*0(%rsi),%rax		# f[0]
	mov	%rdx,%r13
	mulq	%r14			# f[0]*g[4]
	mov	%rax,%r14		# %r14:%r15 = h4
	mov	8*1(%rsi),%rax		# f[1]
	mov	%rdx,%r15

	mulq	%rdi			# f[1]*g[4]*19
	add	%rax,%rbx
	mov	8*2(%rsi),%rax		# f[2]
	adc	%rdx,%rcx
	mulq	%rdi			# f[2]*g[4]*19
	add	%rax,%r8
	mov	8*3(%rsi),%rax		# f[3]
	adc	%rdx,%r9
	mulq	%rdi			# f[3]*g[4]*19
	add	%rax,%r10
	mov	8*4(%rsi),%rax		# f[4]
	adc	%rdx,%r11
	mulq	%rdi			# f[4]*g[4]*19
	imulq	\$19,%rbp,%rdi		# g[3]*19
	add	%rax,%r12
	mov	8*1(%rsi),%rax		# f[1]
	adc	%rdx,%r13
	mulq	%rbp			# f[1]*g[3]
	mov	8*2(%rsp),%rbp		# g[2]
	add	%rax,%r14
	mov	8*2(%rsi),%rax		# f[2]
	adc	%rdx,%r15

	mulq	%rdi			# f[2]*g[3]*19
	add	%rax,%rbx
	mov	8*3(%rsi),%rax		# f[3]
	adc	%rdx,%rcx
	mulq	%rdi			# f[3]*g[3]*19
	add	%rax,%r8
	mov	8*4(%rsi),%rax		# f[4]
	adc	%rdx,%r9
	mulq	%rdi			# f[4]*g[3]*19
	imulq	\$19,%rbp,%rdi		# g[2]*19
	add	%rax,%r10
	mov	8*1(%rsi),%rax		# f[1]
	adc	%rdx,%r11
	mulq	%rbp			# f[1]*g[2]
	add	%rax,%r12
	mov	8*2(%rsi),%rax		# f[2]
	adc	%rdx,%r13
	mulq	%rbp			# f[2]*g[2]
	mov	8*1(%rsp),%rbp		# g[1]
	add	%rax,%r14
	mov	8*3(%rsi),%rax		# f[3]
	adc	%rdx,%r15

	mulq	%rdi			# f[3]*g[2]*19
	add	%rax,%rbx
	mov	8*4(%rsi),%rax		# f[3]
	adc	%rdx,%rcx
	mulq	%rdi			# f[4]*g[2]*19
	add	%rax,%r8
	mov	8*1(%rsi),%rax		# f[1]
	adc	%rdx,%r9
	mulq	%rbp			# f[1]*g[1]
	imulq	\$19,%rbp,%rdi
	add	%rax,%r10
	mov	8*2(%rsi),%rax		# f[2]
	adc	%rdx,%r11
	mulq	%rbp			# f[2]*g[1]
	add	%rax,%r12
	mov	8*3(%rsi),%rax		# f[3]
	adc	%rdx,%r13
	mulq	%rbp			# f[3]*g[1]
	mov	8*0(%rsp),%rbp		# g[0]
	add	%rax,%r14
	mov	8*4(%rsi),%rax		# f[4]
	adc	%rdx,%r15

	mulq	%rdi			# f[4]*g[1]*19
	add	%rax,%rbx
	mov	8*1(%rsi),%rax		# f[1]
	adc	%rdx,%rcx
	mul	%rbp			# f[1]*g[0]
	add	%rax,%r8
	mov	8*2(%rsi),%rax		# f[2]
	adc	%rdx,%r9
	mul	%rbp			# f[2]*g[0]
	add	%rax,%r10
	mov	8*3(%rsi),%rax		# f[3]
	adc	%rdx,%r11
	mul	%rbp			# f[3]*g[0]
	add	%rax,%r12
	mov	8*4(%rsi),%rax		# f[4]
	adc	%rdx,%r13
	mulq	%rbp			# f[4]*g[0]
	add	%rax,%r14
	adc	%rdx,%r15

	mov	8*4(%rsp),%rdi		# restore 1st argument
	jmp	.Lreduce51
.Lfe51_mul_epilogue:
.cfi_endproc
.size	x25519_fe51_mul,.-x25519_fe51_mul

.globl	x25519_fe51_sqr
.type	x25519_fe51_sqr,\@function,2
.align	32
x25519_fe51_sqr:
.cfi_startproc
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
	lea	-8*5(%rsp),%rsp
.cfi_adjust_cfa_offset	40
.Lfe51_sqr_body:

	mov	8*0(%rsi),%rax		# g[0]
	mov	8*2(%rsi),%r15		# g[2]
	mov	8*4(%rsi),%rbp		# g[4]

	mov	%rdi,8*4(%rsp)		# offload 1st argument
	lea	(%rax,%rax),%r14
	mulq	%rax			# g[0]*g[0]
	mov	%rax,%rbx
	mov	8*1(%rsi),%rax		# g[1]
	mov	%rdx,%rcx
	mulq	%r14			# 2*g[0]*g[1]
	mov	%rax,%r8
	mov	%r15,%rax
	mov	%r15,8*0(%rsp)		# offload g[2]
	mov	%rdx,%r9
	mulq	%r14			# 2*g[0]*g[2]
	mov	%rax,%r10
	mov	8*3(%rsi),%rax
	mov	%rdx,%r11
	imulq	\$19,%rbp,%rdi		# g[4]*19
	mulq	%r14			# 2*g[0]*g[3]
	mov	%rax,%r12
	mov	%rbp,%rax
	mov	%rdx,%r13
	mulq	%r14			# 2*g[0]*g[4]
	mov	%rax,%r14
	mov	%rbp,%rax
	mov	%rdx,%r15

	mulq	%rdi			# g[4]*g[4]*19
	add	%rax,%r12
	mov	8*1(%rsi),%rax		# g[1]
	adc	%rdx,%r13

	mov	8*3(%rsi),%rsi		# g[3]
	lea	(%rax,%rax),%rbp
	mulq	%rax			# g[1]*g[1]
	add	%rax,%r10
	mov	8*0(%rsp),%rax		# g[2]
	adc	%rdx,%r11
	mulq	%rbp			# 2*g[1]*g[2]
	add	%rax,%r12
	mov	%rbp,%rax
	adc	%rdx,%r13
	mulq	%rsi			# 2*g[1]*g[3]
	add	%rax,%r14
	mov	%rbp,%rax
	adc	%rdx,%r15
	imulq	\$19,%rsi,%rbp		# g[3]*19
	mulq	%rdi			# 2*g[1]*g[4]*19
	add	%rax,%rbx
	lea	(%rsi,%rsi),%rax
	adc	%rdx,%rcx

	mulq	%rdi			# 2*g[3]*g[4]*19
	add	%rax,%r10
	mov	%rsi,%rax
	adc	%rdx,%r11
	mulq	%rbp			# g[3]*g[3]*19
	add	%rax,%r8
	mov	8*0(%rsp),%rax		# g[2]
	adc	%rdx,%r9

	lea	(%rax,%rax),%rsi
	mulq	%rax			# g[2]*g[2]
	add	%rax,%r14
	mov	%rbp,%rax
	adc	%rdx,%r15
	mulq	%rsi			# 2*g[2]*g[3]*19
	add	%rax,%rbx
	mov	%rsi,%rax
	adc	%rdx,%rcx
	mulq	%rdi			# 2*g[2]*g[4]*19
	add	%rax,%r8
	adc	%rdx,%r9

	mov	8*4(%rsp),%rdi		# restore 1st argument
	jmp	.Lreduce51

.align	32
.Lreduce51:
	mov	\$0x7ffffffffffff,%rbp

	mov	%r10,%rdx
	shr	\$51,%r10
	shl	\$13,%r11
	and	%rbp,%rdx		# %rdx = g2 = h2 & mask
	or	%r10,%r11		# h2>>51
	add	%r11,%r12
	adc	\$0,%r13		# h3 += h2>>51

	mov	%rbx,%rax
	shr	\$51,%rbx
	shl	\$13,%rcx
	and	%rbp,%rax		# %rax = g0 = h0 & mask
	or	%rbx,%rcx		# h0>>51
	add	%rcx,%r8		# h1 += h0>>51
	adc	\$0,%r9

	mov	%r12,%rbx
	shr	\$51,%r12
	shl	\$13,%r13
	and	%rbp,%rbx		# %rbx = g3 = h3 & mask
	or	%r12,%r13		# h3>>51
	add	%r13,%r14		# h4 += h3>>51
	adc	\$0,%r15

	mov	%r8,%rcx
	shr	\$51,%r8
	shl	\$13,%r9
	and	%rbp,%rcx		# %rcx = g1 = h1 & mask
	or	%r8,%r9
	add	%r9,%rdx		# g2 += h1>>51

	mov	%r14,%r10
	shr	\$51,%r14
	shl	\$13,%r15
	and	%rbp,%r10		# %r10 = g4 = h0 & mask
	or	%r14,%r15		# h0>>51

	lea	(%r15,%r15,8),%r14
	lea	(%r15,%r14,2),%r15
	add	%r15,%rax		# g0 += (h0>>51)*19

	mov	%rdx,%r8
	and	%rbp,%rdx		# g2 &= mask
	shr	\$51,%r8
	add	%r8,%rbx		# g3 += g2>>51

	mov	%rax,%r9
	and	%rbp,%rax		# g0 &= mask
	shr	\$51,%r9
	add	%r9,%rcx		# g1 += g0>>51

	mov	%rax,8*0(%rdi)		# save the result
	mov	%rcx,8*1(%rdi)
	mov	%rdx,8*2(%rdi)
	mov	%rbx,8*3(%rdi)
	mov	%r10,8*4(%rdi)

	mov	8*5(%rsp),%r15
.cfi_restore	%r15
	mov	8*6(%rsp),%r14
.cfi_restore	%r14
	mov	8*7(%rsp),%r13
.cfi_restore	%r13
	mov	8*8(%rsp),%r12
.cfi_restore	%r12
	mov	8*9(%rsp),%rbx
.cfi_restore	%rbx
	mov	8*10(%rsp),%rbp
.cfi_restore	%rbp
	lea	8*11(%rsp),%rsp
.cfi_adjust_cfa_offset	88
.Lfe51_sqr_epilogue:
	ret
.cfi_endproc
.size	x25519_fe51_sqr,.-x25519_fe51_sqr

.globl	x25519_fe51_mul121666
.type	x25519_fe51_mul121666,\@function,2
.align	32
x25519_fe51_mul121666:
.cfi_startproc
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
	lea	-8*5(%rsp),%rsp
.cfi_adjust_cfa_offset	40
.Lfe51_mul121666_body:
	mov	\$121666,%eax

	mulq	8*0(%rsi)
	mov	%rax,%rbx		# %rbx:%rcx = h0
	mov	\$121666,%eax
	mov	%rdx,%rcx
	mulq	8*1(%rsi)
	mov	%rax,%r8		# %r8:%r9 = h1
	mov	\$121666,%eax
	mov	%rdx,%r9
	mulq	8*2(%rsi)
	mov	%rax,%r10		# %r10:%r11 = h2
	mov	\$121666,%eax
	mov	%rdx,%r11
	mulq	8*3(%rsi)
	mov	%rax,%r12		# %r12:%r13 = h3
	mov	\$121666,%eax		# f[0]
	mov	%rdx,%r13
	mulq	8*4(%rsi)
	mov	%rax,%r14		# %r14:%r15 = h4
	mov	%rdx,%r15

	jmp	.Lreduce51
.Lfe51_mul121666_epilogue:
.cfi_endproc
.size	x25519_fe51_mul121666,.-x25519_fe51_mul121666
___
########################################################################
# Base 2^64 subroutines modulo 2*(2^255-19)
#
if ($addx) {
my ($acc0,$acc1,$acc2,$acc3,$acc4,$acc5,$acc6,$acc7) = map("%r$_",(8..15));

$code.=<<___;
.extern	OPENSSL_ia32cap_P
.globl	x25519_fe64_eligible
.type	x25519_fe64_eligible,\@abi-omnipotent
.align	32
x25519_fe64_eligible:
	mov	OPENSSL_ia32cap_P+8(%rip),%ecx
	xor	%eax,%eax
	and	\$0x80100,%ecx
	cmp	\$0x80100,%ecx
	cmove	%ecx,%eax
	ret
.size	x25519_fe64_eligible,.-x25519_fe64_eligible

.globl	x25519_fe64_mul
.type	x25519_fe64_mul,\@function,3
.align	32
x25519_fe64_mul:
.cfi_startproc
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
	push	%rdi			# offload dst
.cfi_push	%rdi
	lea	-8*2(%rsp),%rsp
.cfi_adjust_cfa_offset	16
.Lfe64_mul_body:

	mov	%rdx,%rax
	mov	8*0(%rdx),%rbp		# b[0]
	mov	8*0(%rsi),%rdx		# a[0]
	mov	8*1(%rax),%rcx		# b[1]
	mov	8*2(%rax),$acc6		# b[2]
	mov	8*3(%rax),$acc7		# b[3]

	mulx	%rbp,$acc0,%rax		# a[0]*b[0]
	xor	%edi,%edi		# cf=0,of=0
	mulx	%rcx,$acc1,%rbx		# a[0]*b[1]
	adcx	%rax,$acc1
	mulx	$acc6,$acc2,%rax	# a[0]*b[2]
	adcx	%rbx,$acc2
	mulx	$acc7,$acc3,$acc4	# a[0]*b[3]
	 mov	8*1(%rsi),%rdx		# a[1]
	adcx	%rax,$acc3
	mov	$acc6,(%rsp)		# offload b[2]
	adcx	%rdi,$acc4		# cf=0

	mulx	%rbp,%rax,%rbx		# a[1]*b[0]
	adox	%rax,$acc1
	adcx	%rbx,$acc2
	mulx	%rcx,%rax,%rbx		# a[1]*b[1]
	adox	%rax,$acc2
	adcx	%rbx,$acc3
	mulx	$acc6,%rax,%rbx		# a[1]*b[2]
	adox	%rax,$acc3
	adcx	%rbx,$acc4
	mulx	$acc7,%rax,$acc5	# a[1]*b[3]
	 mov	8*2(%rsi),%rdx		# a[2]
	adox	%rax,$acc4
	adcx	%rdi,$acc5		# cf=0
	adox	%rdi,$acc5		# of=0

	mulx	%rbp,%rax,%rbx		# a[2]*b[0]
	adcx	%rax,$acc2
	adox	%rbx,$acc3
	mulx	%rcx,%rax,%rbx		# a[2]*b[1]
	adcx	%rax,$acc3
	adox	%rbx,$acc4
	mulx	$acc6,%rax,%rbx		# a[2]*b[2]
	adcx	%rax,$acc4
	adox	%rbx,$acc5
	mulx	$acc7,%rax,$acc6	# a[2]*b[3]
	 mov	8*3(%rsi),%rdx		# a[3]
	adcx	%rax,$acc5
	adox	%rdi,$acc6		# of=0
	adcx	%rdi,$acc6		# cf=0

	mulx	%rbp,%rax,%rbx		# a[3]*b[0]
	adox	%rax,$acc3
	adcx	%rbx,$acc4
	mulx	%rcx,%rax,%rbx		# a[3]*b[1]
	adox	%rax,$acc4
	adcx	%rbx,$acc5
	mulx	(%rsp),%rax,%rbx	# a[3]*b[2]
	adox	%rax,$acc5
	adcx	%rbx,$acc6
	mulx	$acc7,%rax,$acc7	# a[3]*b[3]
	 mov	\$38,%edx
	adox	%rax,$acc6
	adcx	%rdi,$acc7		# cf=0
	adox	%rdi,$acc7		# of=0

	jmp	.Lreduce64
.Lfe64_mul_epilogue:
.cfi_endproc
.size	x25519_fe64_mul,.-x25519_fe64_mul

.globl	x25519_fe64_sqr
.type	x25519_fe64_sqr,\@function,2
.align	32
x25519_fe64_sqr:
.cfi_startproc
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
	push	%rdi			# offload dst
.cfi_push	%rdi
	lea	-8*2(%rsp),%rsp
.cfi_adjust_cfa_offset	16
.Lfe64_sqr_body:

	mov	8*0(%rsi),%rdx		# a[0]
	mov	8*1(%rsi),%rcx		# a[1]
	mov	8*2(%rsi),%rbp		# a[2]
	mov	8*3(%rsi),%rsi		# a[3]

	################################################################
	mulx	%rdx,$acc0,$acc7	# a[0]*a[0]
	mulx	%rcx,$acc1,%rax		# a[0]*a[1]
	xor	%edi,%edi		# cf=0,of=0
	mulx	%rbp,$acc2,%rbx		# a[0]*a[2]
	adcx	%rax,$acc2
	mulx	%rsi,$acc3,$acc4	# a[0]*a[3]
	 mov	%rcx,%rdx		# a[1]
	adcx	%rbx,$acc3
	adcx	%rdi,$acc4		# cf=0

	################################################################
	mulx	%rbp,%rax,%rbx		# a[1]*a[2]
	adox	%rax,$acc3
	adcx	%rbx,$acc4
	mulx	%rsi,%rax,$acc5		# a[1]*a[3]
	 mov	%rbp,%rdx		# a[2]
	adox	%rax,$acc4
	adcx	%rdi,$acc5

	################################################################
	mulx	%rsi,%rax,$acc6		# a[2]*a[3]
	 mov	%rcx,%rdx		# a[1]
	adox	%rax,$acc5
	adcx	%rdi,$acc6		# cf=0
	adox	%rdi,$acc6		# of=0

	 adcx	$acc1,$acc1		# acc1:6<<1
	adox	$acc7,$acc1
	 adcx	$acc2,$acc2
	mulx	%rdx,%rax,%rbx		# a[1]*a[1]
	 mov	%rbp,%rdx		# a[2]
	 adcx	$acc3,$acc3
	adox	%rax,$acc2
	 adcx	$acc4,$acc4
	adox	%rbx,$acc3
	mulx	%rdx,%rax,%rbx		# a[2]*a[2]
	 mov	%rsi,%rdx		# a[3]
	 adcx	$acc5,$acc5
	adox	%rax,$acc4
	 adcx	$acc6,$acc6
	adox	%rbx,$acc5
	mulx	%rdx,%rax,$acc7		# a[3]*a[3]
	 mov	\$38,%edx
	adox	%rax,$acc6
	adcx	%rdi,$acc7		# cf=0
	adox	%rdi,$acc7		# of=0
	jmp	.Lreduce64

.align	32
.Lreduce64:
	mulx	$acc4,%rax,%rbx
	adcx	%rax,$acc0
	adox	%rbx,$acc1
	mulx	$acc5,%rax,%rbx
	adcx	%rax,$acc1
	adox	%rbx,$acc2
	mulx	$acc6,%rax,%rbx
	adcx	%rax,$acc2
	adox	%rbx,$acc3
	mulx	$acc7,%rax,$acc4
	adcx	%rax,$acc3
	adox	%rdi,$acc4
	adcx	%rdi,$acc4

	mov	8*2(%rsp),%rdi		# restore dst
	imulq	%rdx,$acc4

	add	$acc4,$acc0
	adc	\$0,$acc1
	adc	\$0,$acc2
	adc	\$0,$acc3

	sbb	%rax,%rax		# cf -> mask
	and	\$38,%rax

	add	%rax,$acc0
	mov	$acc1,8*1(%rdi)
	mov	$acc2,8*2(%rdi)
	mov	$acc3,8*3(%rdi)
	mov	$acc0,8*0(%rdi)

	mov	8*3(%rsp),%r15
.cfi_restore	%r15
	mov	8*4(%rsp),%r14
.cfi_restore	%r14
	mov	8*5(%rsp),%r13
.cfi_restore	%r13
	mov	8*6(%rsp),%r12
.cfi_restore	%r12
	mov	8*7(%rsp),%rbx
.cfi_restore	%rbx
	mov	8*8(%rsp),%rbp
.cfi_restore	%rbp
	lea	8*9(%rsp),%rsp
.cfi_adjust_cfa_offset	88
.Lfe64_sqr_epilogue:
	ret
.cfi_endproc
.size	x25519_fe64_sqr,.-x25519_fe64_sqr

.globl	x25519_fe64_mul121666
.type	x25519_fe64_mul121666,\@function,2
.align	32
x25519_fe64_mul121666:
.Lfe64_mul121666_body:
	mov	\$121666,%edx
	mulx	8*0(%rsi),$acc0,%rcx
	mulx	8*1(%rsi),$acc1,%rax
	add	%rcx,$acc1
	mulx	8*2(%rsi),$acc2,%rcx
	adc	%rax,$acc2
	mulx	8*3(%rsi),$acc3,%rax
	adc	%rcx,$acc3
	adc	\$0,%rax

	imulq	\$38,%rax,%rax

	add	%rax,$acc0
	adc	\$0,$acc1
	adc	\$0,$acc2
	adc	\$0,$acc3

	sbb	%rax,%rax		# cf -> mask
	and	\$38,%rax

	add	%rax,$acc0
	mov	$acc1,8*1(%rdi)
	mov	$acc2,8*2(%rdi)
	mov	$acc3,8*3(%rdi)
	mov	$acc0,8*0(%rdi)

.Lfe64_mul121666_epilogue:
	ret
.size	x25519_fe64_mul121666,.-x25519_fe64_mul121666

.globl	x25519_fe64_add
.type	x25519_fe64_add,\@function,3
.align	32
x25519_fe64_add:
.Lfe64_add_body:
	mov	8*0(%rsi),$acc0
	mov	8*1(%rsi),$acc1
	mov	8*2(%rsi),$acc2
	mov	8*3(%rsi),$acc3

	add	8*0(%rdx),$acc0
	adc	8*1(%rdx),$acc1
	adc	8*2(%rdx),$acc2
	adc	8*3(%rdx),$acc3

	sbb	%rax,%rax		# cf -> mask
	and	\$38,%rax

	add	%rax,$acc0
	adc	\$0,$acc1
	adc	\$0,$acc2
	mov	$acc1,8*1(%rdi)
	adc	\$0,$acc3
	mov	$acc2,8*2(%rdi)
	sbb	%rax,%rax		# cf -> mask
	mov	$acc3,8*3(%rdi)
	and	\$38,%rax

	add	%rax,$acc0
	mov	$acc0,8*0(%rdi)

.Lfe64_add_epilogue:
	ret
.size	x25519_fe64_add,.-x25519_fe64_add

.globl	x25519_fe64_sub
.type	x25519_fe64_sub,\@function,3
.align	32
x25519_fe64_sub:
.Lfe64_sub_body:
	mov	8*0(%rsi),$acc0
	mov	8*1(%rsi),$acc1
	mov	8*2(%rsi),$acc2
	mov	8*3(%rsi),$acc3

	sub	8*0(%rdx),$acc0
	sbb	8*1(%rdx),$acc1
	sbb	8*2(%rdx),$acc2
	sbb	8*3(%rdx),$acc3

	sbb	%rax,%rax		# cf -> mask
	and	\$38,%rax

	sub	%rax,$acc0
	sbb	\$0,$acc1
	sbb	\$0,$acc2
	mov	$acc1,8*1(%rdi)
	sbb	\$0,$acc3
	mov	$acc2,8*2(%rdi)
	sbb	%rax,%rax		# cf -> mask
	mov	$acc3,8*3(%rdi)
	and	\$38,%rax

	sub	%rax,$acc0
	mov	$acc0,8*0(%rdi)

.Lfe64_sub_epilogue:
	ret
.size	x25519_fe64_sub,.-x25519_fe64_sub

.globl	x25519_fe64_tobytes
.type	x25519_fe64_tobytes,\@function,2
.align	32
x25519_fe64_tobytes:
.Lfe64_to_body:
	mov	8*0(%rsi),$acc0
	mov	8*1(%rsi),$acc1
	mov	8*2(%rsi),$acc2
	mov	8*3(%rsi),$acc3

	################################# reduction modulo 2^255-19
	lea	($acc3,$acc3),%rax
	sar	\$63,$acc3		# most significant bit -> mask
	shr	\$1,%rax		# most significant bit cleared
	and	\$19,$acc3
	add	\$19,$acc3		# compare to modulus in the same go

	add	$acc3,$acc0
	adc	\$0,$acc1
	adc	\$0,$acc2
	adc	\$0,%rax

	lea	(%rax,%rax),$acc3
	sar	\$63,%rax		# most significant bit -> mask
	shr	\$1,$acc3		# most significant bit cleared
	not	%rax
	and	\$19,%rax

	sub	%rax,$acc0
	sbb	\$0,$acc1
	sbb	\$0,$acc2
	sbb	\$0,$acc3

	mov	$acc0,8*0(%rdi)
	mov	$acc1,8*1(%rdi)
	mov	$acc2,8*2(%rdi)
	mov	$acc3,8*3(%rdi)

.Lfe64_to_epilogue:
	ret
.size	x25519_fe64_tobytes,.-x25519_fe64_tobytes
___
} else {
$code.=<<___;
.globl	x25519_fe64_eligible
.type	x25519_fe64_eligible,\@abi-omnipotent
.align	32
x25519_fe64_eligible:
	xor	%eax,%eax
	ret
.size	x25519_fe64_eligible,.-x25519_fe64_eligible

.globl	x25519_fe64_mul
.type	x25519_fe64_mul,\@abi-omnipotent
.globl	x25519_fe64_sqr
.globl	x25519_fe64_mul121666
.globl	x25519_fe64_add
.globl	x25519_fe64_sub
.globl	x25519_fe64_tobytes
x25519_fe64_mul:
x25519_fe64_sqr:
x25519_fe64_mul121666:
x25519_fe64_add:
x25519_fe64_sub:
x25519_fe64_tobytes:
	.byte	0x0f,0x0b	# ud2
	ret
.size	x25519_fe64_mul,.-x25519_fe64_mul
___
}
$code.=<<___;
.asciz	"X25519 primitives for x86_64, CRYPTOGAMS by <appro\@openssl.org>"
___

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
	.rva	.LSEH_begin_x25519_fe51_mul
	.rva	.LSEH_end_x25519_fe51_mul
	.rva	.LSEH_info_x25519_fe51_mul

	.rva	.LSEH_begin_x25519_fe51_sqr
	.rva	.LSEH_end_x25519_fe51_sqr
	.rva	.LSEH_info_x25519_fe51_sqr

	.rva	.LSEH_begin_x25519_fe51_mul121666
	.rva	.LSEH_end_x25519_fe51_mul121666
	.rva	.LSEH_info_x25519_fe51_mul121666
___
$code.=<<___	if ($addx);
	.rva	.LSEH_begin_x25519_fe64_mul
	.rva	.LSEH_end_x25519_fe64_mul
	.rva	.LSEH_info_x25519_fe64_mul

	.rva	.LSEH_begin_x25519_fe64_sqr
	.rva	.LSEH_end_x25519_fe64_sqr
	.rva	.LSEH_info_x25519_fe64_sqr

	.rva	.LSEH_begin_x25519_fe64_mul121666
	.rva	.LSEH_end_x25519_fe64_mul121666
	.rva	.LSEH_info_x25519_fe64_mul121666

	.rva	.LSEH_begin_x25519_fe64_add
	.rva	.LSEH_end_x25519_fe64_add
	.rva	.LSEH_info_x25519_fe64_add

	.rva	.LSEH_begin_x25519_fe64_sub
	.rva	.LSEH_end_x25519_fe64_sub
	.rva	.LSEH_info_x25519_fe64_sub

	.rva	.LSEH_begin_x25519_fe64_tobytes
	.rva	.LSEH_end_x25519_fe64_tobytes
	.rva	.LSEH_info_x25519_fe64_tobytes
___
$code.=<<___;
.section	.xdata
.align	8
.LSEH_info_x25519_fe51_mul:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lfe51_mul_body,.Lfe51_mul_epilogue	# HandlerData[]
	.long	88,0
.LSEH_info_x25519_fe51_sqr:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lfe51_sqr_body,.Lfe51_sqr_epilogue	# HandlerData[]
	.long	88,0
.LSEH_info_x25519_fe51_mul121666:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lfe51_mul121666_body,.Lfe51_mul121666_epilogue	# HandlerData[]
	.long	88,0
___
$code.=<<___	if ($addx);
.LSEH_info_x25519_fe64_mul:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lfe64_mul_body,.Lfe64_mul_epilogue	# HandlerData[]
	.long	72,0
.LSEH_info_x25519_fe64_sqr:
	.byte	9,0,0,0
	.rva	full_handler
	.rva	.Lfe64_sqr_body,.Lfe64_sqr_epilogue	# HandlerData[]
	.long	72,0
.LSEH_info_x25519_fe64_mul121666:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Lfe64_mul121666_body,.Lfe64_mul121666_epilogue	# HandlerData[]
.LSEH_info_x25519_fe64_add:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Lfe64_add_body,.Lfe64_add_epilogue	# HandlerData[]
.LSEH_info_x25519_fe64_sub:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Lfe64_sub_body,.Lfe64_sub_epilogue	# HandlerData[]
.LSEH_info_x25519_fe64_tobytes:
	.byte	9,0,0,0
	.rva	short_handler
	.rva	.Lfe64_to_body,.Lfe64_to_epilogue	# HandlerData[]
___
}

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

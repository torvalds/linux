#! /usr/bin/env perl
# Copyright 2013-2016 The OpenSSL Project Authors. All Rights Reserved.
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
#
# AES-NI-CTR+GHASH stitch.
#
# February 2013
#
# OpenSSL GCM implementation is organized in such way that its
# performance is rather close to the sum of its streamed components,
# in the context parallelized AES-NI CTR and modulo-scheduled
# PCLMULQDQ-enabled GHASH. Unfortunately, as no stitch implementation
# was observed to perform significantly better than the sum of the
# components on contemporary CPUs, the effort was deemed impossible to
# justify. This module is based on combination of Intel submissions,
# [1] and [2], with MOVBE twist suggested by Ilya Albrekht and Max
# Locktyukhin of Intel Corp. who verified that it reduces shuffles
# pressure with notable relative improvement, achieving 1.0 cycle per
# byte processed with 128-bit key on Haswell processor, 0.74 - on
# Broadwell, 0.63 - on Skylake... [Mentioned results are raw profiled
# measurements for favourable packet size, one divisible by 96.
# Applications using the EVP interface will observe a few percent
# worse performance.]
#
# Knights Landing processes 1 byte in 1.25 cycles (measured with EVP).
#
# [1] http://rt.openssl.org/Ticket/Display.html?id=2900&user=guest&pass=guest
# [2] http://www.intel.com/content/dam/www/public/us/en/documents/software-support/enabling-high-performance-gcm.pdf

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
	$avx = ($1>=2.20) + ($1>=2.22);
}

if (!$avx && $win64 && ($flavour =~ /nasm/ || $ENV{ASM} =~ /nasm/) &&
	    `nasm -v 2>&1` =~ /NASM version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.09) + ($1>=2.10);
}

if (!$avx && $win64 && ($flavour =~ /masm/ || $ENV{ASM} =~ /ml64/) &&
	    `ml64 2>&1` =~ /Version ([0-9]+)\./) {
	$avx = ($1>=10) + ($1>=11);
}

if (!$avx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|.*based on LLVM) ([3-9]\.[0-9]+)/) {
	$avx = ($2>=3.0) + ($2>3.0);
}

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

if ($avx>1) {{{

($inp,$out,$len,$key,$ivp,$Xip)=("%rdi","%rsi","%rdx","%rcx","%r8","%r9");

($Ii,$T1,$T2,$Hkey,
 $Z0,$Z1,$Z2,$Z3,$Xi) = map("%xmm$_",(0..8));

($inout0,$inout1,$inout2,$inout3,$inout4,$inout5,$rndkey) = map("%xmm$_",(9..15));

($counter,$rounds,$ret,$const,$in0,$end0)=("%ebx","%ebp","%r10","%r11","%r14","%r15");

$code=<<___;
.text

.type	_aesni_ctr32_ghash_6x,\@abi-omnipotent
.align	32
_aesni_ctr32_ghash_6x:
	vmovdqu		0x20($const),$T2	# borrow $T2, .Lone_msb
	sub		\$6,$len
	vpxor		$Z0,$Z0,$Z0		# $Z0   = 0
	vmovdqu		0x00-0x80($key),$rndkey
	vpaddb		$T2,$T1,$inout1
	vpaddb		$T2,$inout1,$inout2
	vpaddb		$T2,$inout2,$inout3
	vpaddb		$T2,$inout3,$inout4
	vpaddb		$T2,$inout4,$inout5
	vpxor		$rndkey,$T1,$inout0
	vmovdqu		$Z0,16+8(%rsp)		# "$Z3" = 0
	jmp		.Loop6x

.align	32
.Loop6x:
	add		\$`6<<24`,$counter
	jc		.Lhandle_ctr32		# discard $inout[1-5]?
	vmovdqu		0x00-0x20($Xip),$Hkey	# $Hkey^1
	  vpaddb	$T2,$inout5,$T1		# next counter value
	  vpxor		$rndkey,$inout1,$inout1
	  vpxor		$rndkey,$inout2,$inout2

.Lresume_ctr32:
	vmovdqu		$T1,($ivp)		# save next counter value
	vpclmulqdq	\$0x10,$Hkey,$Z3,$Z1
	  vpxor		$rndkey,$inout3,$inout3
	  vmovups	0x10-0x80($key),$T2	# borrow $T2 for $rndkey
	vpclmulqdq	\$0x01,$Hkey,$Z3,$Z2
	xor		%r12,%r12
	cmp		$in0,$end0

	  vaesenc	$T2,$inout0,$inout0
	vmovdqu		0x30+8(%rsp),$Ii	# I[4]
	  vpxor		$rndkey,$inout4,$inout4
	vpclmulqdq	\$0x00,$Hkey,$Z3,$T1
	  vaesenc	$T2,$inout1,$inout1
	  vpxor		$rndkey,$inout5,$inout5
	setnc		%r12b
	vpclmulqdq	\$0x11,$Hkey,$Z3,$Z3
	  vaesenc	$T2,$inout2,$inout2
	vmovdqu		0x10-0x20($Xip),$Hkey	# $Hkey^2
	neg		%r12
	  vaesenc	$T2,$inout3,$inout3
	 vpxor		$Z1,$Z2,$Z2
	vpclmulqdq	\$0x00,$Hkey,$Ii,$Z1
	 vpxor		$Z0,$Xi,$Xi		# modulo-scheduled
	  vaesenc	$T2,$inout4,$inout4
	 vpxor		$Z1,$T1,$Z0
	and		\$0x60,%r12
	  vmovups	0x20-0x80($key),$rndkey
	vpclmulqdq	\$0x10,$Hkey,$Ii,$T1
	  vaesenc	$T2,$inout5,$inout5

	vpclmulqdq	\$0x01,$Hkey,$Ii,$T2
	lea		($in0,%r12),$in0
	  vaesenc	$rndkey,$inout0,$inout0
	 vpxor		16+8(%rsp),$Xi,$Xi	# modulo-scheduled [vpxor $Z3,$Xi,$Xi]
	vpclmulqdq	\$0x11,$Hkey,$Ii,$Hkey
	 vmovdqu	0x40+8(%rsp),$Ii	# I[3]
	  vaesenc	$rndkey,$inout1,$inout1
	movbe		0x58($in0),%r13
	  vaesenc	$rndkey,$inout2,$inout2
	movbe		0x50($in0),%r12
	  vaesenc	$rndkey,$inout3,$inout3
	mov		%r13,0x20+8(%rsp)
	  vaesenc	$rndkey,$inout4,$inout4
	mov		%r12,0x28+8(%rsp)
	vmovdqu		0x30-0x20($Xip),$Z1	# borrow $Z1 for $Hkey^3
	  vaesenc	$rndkey,$inout5,$inout5

	  vmovups	0x30-0x80($key),$rndkey
	 vpxor		$T1,$Z2,$Z2
	vpclmulqdq	\$0x00,$Z1,$Ii,$T1
	  vaesenc	$rndkey,$inout0,$inout0
	 vpxor		$T2,$Z2,$Z2
	vpclmulqdq	\$0x10,$Z1,$Ii,$T2
	  vaesenc	$rndkey,$inout1,$inout1
	 vpxor		$Hkey,$Z3,$Z3
	vpclmulqdq	\$0x01,$Z1,$Ii,$Hkey
	  vaesenc	$rndkey,$inout2,$inout2
	vpclmulqdq	\$0x11,$Z1,$Ii,$Z1
	 vmovdqu	0x50+8(%rsp),$Ii	# I[2]
	  vaesenc	$rndkey,$inout3,$inout3
	  vaesenc	$rndkey,$inout4,$inout4
	 vpxor		$T1,$Z0,$Z0
	vmovdqu		0x40-0x20($Xip),$T1	# borrow $T1 for $Hkey^4
	  vaesenc	$rndkey,$inout5,$inout5

	  vmovups	0x40-0x80($key),$rndkey
	 vpxor		$T2,$Z2,$Z2
	vpclmulqdq	\$0x00,$T1,$Ii,$T2
	  vaesenc	$rndkey,$inout0,$inout0
	 vpxor		$Hkey,$Z2,$Z2
	vpclmulqdq	\$0x10,$T1,$Ii,$Hkey
	  vaesenc	$rndkey,$inout1,$inout1
	movbe		0x48($in0),%r13
	 vpxor		$Z1,$Z3,$Z3
	vpclmulqdq	\$0x01,$T1,$Ii,$Z1
	  vaesenc	$rndkey,$inout2,$inout2
	movbe		0x40($in0),%r12
	vpclmulqdq	\$0x11,$T1,$Ii,$T1
	 vmovdqu	0x60+8(%rsp),$Ii	# I[1]
	  vaesenc	$rndkey,$inout3,$inout3
	mov		%r13,0x30+8(%rsp)
	  vaesenc	$rndkey,$inout4,$inout4
	mov		%r12,0x38+8(%rsp)
	 vpxor		$T2,$Z0,$Z0
	vmovdqu		0x60-0x20($Xip),$T2	# borrow $T2 for $Hkey^5
	  vaesenc	$rndkey,$inout5,$inout5

	  vmovups	0x50-0x80($key),$rndkey
	 vpxor		$Hkey,$Z2,$Z2
	vpclmulqdq	\$0x00,$T2,$Ii,$Hkey
	  vaesenc	$rndkey,$inout0,$inout0
	 vpxor		$Z1,$Z2,$Z2
	vpclmulqdq	\$0x10,$T2,$Ii,$Z1
	  vaesenc	$rndkey,$inout1,$inout1
	movbe		0x38($in0),%r13
	 vpxor		$T1,$Z3,$Z3
	vpclmulqdq	\$0x01,$T2,$Ii,$T1
	 vpxor		0x70+8(%rsp),$Xi,$Xi	# accumulate I[0]
	  vaesenc	$rndkey,$inout2,$inout2
	movbe		0x30($in0),%r12
	vpclmulqdq	\$0x11,$T2,$Ii,$T2
	  vaesenc	$rndkey,$inout3,$inout3
	mov		%r13,0x40+8(%rsp)
	  vaesenc	$rndkey,$inout4,$inout4
	mov		%r12,0x48+8(%rsp)
	 vpxor		$Hkey,$Z0,$Z0
	 vmovdqu	0x70-0x20($Xip),$Hkey	# $Hkey^6
	  vaesenc	$rndkey,$inout5,$inout5

	  vmovups	0x60-0x80($key),$rndkey
	 vpxor		$Z1,$Z2,$Z2
	vpclmulqdq	\$0x10,$Hkey,$Xi,$Z1
	  vaesenc	$rndkey,$inout0,$inout0
	 vpxor		$T1,$Z2,$Z2
	vpclmulqdq	\$0x01,$Hkey,$Xi,$T1
	  vaesenc	$rndkey,$inout1,$inout1
	movbe		0x28($in0),%r13
	 vpxor		$T2,$Z3,$Z3
	vpclmulqdq	\$0x00,$Hkey,$Xi,$T2
	  vaesenc	$rndkey,$inout2,$inout2
	movbe		0x20($in0),%r12
	vpclmulqdq	\$0x11,$Hkey,$Xi,$Xi
	  vaesenc	$rndkey,$inout3,$inout3
	mov		%r13,0x50+8(%rsp)
	  vaesenc	$rndkey,$inout4,$inout4
	mov		%r12,0x58+8(%rsp)
	vpxor		$Z1,$Z2,$Z2
	  vaesenc	$rndkey,$inout5,$inout5
	vpxor		$T1,$Z2,$Z2

	  vmovups	0x70-0x80($key),$rndkey
	vpslldq		\$8,$Z2,$Z1
	vpxor		$T2,$Z0,$Z0
	vmovdqu		0x10($const),$Hkey	# .Lpoly

	  vaesenc	$rndkey,$inout0,$inout0
	vpxor		$Xi,$Z3,$Z3
	  vaesenc	$rndkey,$inout1,$inout1
	vpxor		$Z1,$Z0,$Z0
	movbe		0x18($in0),%r13
	  vaesenc	$rndkey,$inout2,$inout2
	movbe		0x10($in0),%r12
	vpalignr	\$8,$Z0,$Z0,$Ii		# 1st phase
	vpclmulqdq	\$0x10,$Hkey,$Z0,$Z0
	mov		%r13,0x60+8(%rsp)
	  vaesenc	$rndkey,$inout3,$inout3
	mov		%r12,0x68+8(%rsp)
	  vaesenc	$rndkey,$inout4,$inout4
	  vmovups	0x80-0x80($key),$T1	# borrow $T1 for $rndkey
	  vaesenc	$rndkey,$inout5,$inout5

	  vaesenc	$T1,$inout0,$inout0
	  vmovups	0x90-0x80($key),$rndkey
	  vaesenc	$T1,$inout1,$inout1
	vpsrldq		\$8,$Z2,$Z2
	  vaesenc	$T1,$inout2,$inout2
	vpxor		$Z2,$Z3,$Z3
	  vaesenc	$T1,$inout3,$inout3
	vpxor		$Ii,$Z0,$Z0
	movbe		0x08($in0),%r13
	  vaesenc	$T1,$inout4,$inout4
	movbe		0x00($in0),%r12
	  vaesenc	$T1,$inout5,$inout5
	  vmovups	0xa0-0x80($key),$T1
	  cmp		\$11,$rounds
	  jb		.Lenc_tail		# 128-bit key

	  vaesenc	$rndkey,$inout0,$inout0
	  vaesenc	$rndkey,$inout1,$inout1
	  vaesenc	$rndkey,$inout2,$inout2
	  vaesenc	$rndkey,$inout3,$inout3
	  vaesenc	$rndkey,$inout4,$inout4
	  vaesenc	$rndkey,$inout5,$inout5

	  vaesenc	$T1,$inout0,$inout0
	  vaesenc	$T1,$inout1,$inout1
	  vaesenc	$T1,$inout2,$inout2
	  vaesenc	$T1,$inout3,$inout3
	  vaesenc	$T1,$inout4,$inout4
	  vmovups	0xb0-0x80($key),$rndkey
	  vaesenc	$T1,$inout5,$inout5
	  vmovups	0xc0-0x80($key),$T1
	  je		.Lenc_tail		# 192-bit key

	  vaesenc	$rndkey,$inout0,$inout0
	  vaesenc	$rndkey,$inout1,$inout1
	  vaesenc	$rndkey,$inout2,$inout2
	  vaesenc	$rndkey,$inout3,$inout3
	  vaesenc	$rndkey,$inout4,$inout4
	  vaesenc	$rndkey,$inout5,$inout5

	  vaesenc	$T1,$inout0,$inout0
	  vaesenc	$T1,$inout1,$inout1
	  vaesenc	$T1,$inout2,$inout2
	  vaesenc	$T1,$inout3,$inout3
	  vaesenc	$T1,$inout4,$inout4
	  vmovups	0xd0-0x80($key),$rndkey
	  vaesenc	$T1,$inout5,$inout5
	  vmovups	0xe0-0x80($key),$T1
	  jmp		.Lenc_tail		# 256-bit key

.align	32
.Lhandle_ctr32:
	vmovdqu		($const),$Ii		# borrow $Ii for .Lbswap_mask
	  vpshufb	$Ii,$T1,$Z2		# byte-swap counter
	  vmovdqu	0x30($const),$Z1	# borrow $Z1, .Ltwo_lsb
	  vpaddd	0x40($const),$Z2,$inout1	# .Lone_lsb
	  vpaddd	$Z1,$Z2,$inout2
	vmovdqu		0x00-0x20($Xip),$Hkey	# $Hkey^1
	  vpaddd	$Z1,$inout1,$inout3
	  vpshufb	$Ii,$inout1,$inout1
	  vpaddd	$Z1,$inout2,$inout4
	  vpshufb	$Ii,$inout2,$inout2
	  vpxor		$rndkey,$inout1,$inout1
	  vpaddd	$Z1,$inout3,$inout5
	  vpshufb	$Ii,$inout3,$inout3
	  vpxor		$rndkey,$inout2,$inout2
	  vpaddd	$Z1,$inout4,$T1		# byte-swapped next counter value
	  vpshufb	$Ii,$inout4,$inout4
	  vpshufb	$Ii,$inout5,$inout5
	  vpshufb	$Ii,$T1,$T1		# next counter value
	jmp		.Lresume_ctr32

.align	32
.Lenc_tail:
	  vaesenc	$rndkey,$inout0,$inout0
	vmovdqu		$Z3,16+8(%rsp)		# postpone vpxor $Z3,$Xi,$Xi
	vpalignr	\$8,$Z0,$Z0,$Xi		# 2nd phase
	  vaesenc	$rndkey,$inout1,$inout1
	vpclmulqdq	\$0x10,$Hkey,$Z0,$Z0
	  vpxor		0x00($inp),$T1,$T2
	  vaesenc	$rndkey,$inout2,$inout2
	  vpxor		0x10($inp),$T1,$Ii
	  vaesenc	$rndkey,$inout3,$inout3
	  vpxor		0x20($inp),$T1,$Z1
	  vaesenc	$rndkey,$inout4,$inout4
	  vpxor		0x30($inp),$T1,$Z2
	  vaesenc	$rndkey,$inout5,$inout5
	  vpxor		0x40($inp),$T1,$Z3
	  vpxor		0x50($inp),$T1,$Hkey
	  vmovdqu	($ivp),$T1		# load next counter value

	  vaesenclast	$T2,$inout0,$inout0
	  vmovdqu	0x20($const),$T2	# borrow $T2, .Lone_msb
	  vaesenclast	$Ii,$inout1,$inout1
	 vpaddb		$T2,$T1,$Ii
	mov		%r13,0x70+8(%rsp)
	lea		0x60($inp),$inp
	  vaesenclast	$Z1,$inout2,$inout2
	 vpaddb		$T2,$Ii,$Z1
	mov		%r12,0x78+8(%rsp)
	lea		0x60($out),$out
	  vmovdqu	0x00-0x80($key),$rndkey
	  vaesenclast	$Z2,$inout3,$inout3
	 vpaddb		$T2,$Z1,$Z2
	  vaesenclast	$Z3, $inout4,$inout4
	 vpaddb		$T2,$Z2,$Z3
	  vaesenclast	$Hkey,$inout5,$inout5
	 vpaddb		$T2,$Z3,$Hkey

	add		\$0x60,$ret
	sub		\$0x6,$len
	jc		.L6x_done

	  vmovups	$inout0,-0x60($out)	# save output
	 vpxor		$rndkey,$T1,$inout0
	  vmovups	$inout1,-0x50($out)
	 vmovdqa	$Ii,$inout1		# 0 latency
	  vmovups	$inout2,-0x40($out)
	 vmovdqa	$Z1,$inout2		# 0 latency
	  vmovups	$inout3,-0x30($out)
	 vmovdqa	$Z2,$inout3		# 0 latency
	  vmovups	$inout4,-0x20($out)
	 vmovdqa	$Z3,$inout4		# 0 latency
	  vmovups	$inout5,-0x10($out)
	 vmovdqa	$Hkey,$inout5		# 0 latency
	vmovdqu		0x20+8(%rsp),$Z3	# I[5]
	jmp		.Loop6x

.L6x_done:
	vpxor		16+8(%rsp),$Xi,$Xi	# modulo-scheduled
	vpxor		$Z0,$Xi,$Xi		# modulo-scheduled

	ret
.size	_aesni_ctr32_ghash_6x,.-_aesni_ctr32_ghash_6x
___
######################################################################
#
# size_t aesni_gcm_[en|de]crypt(const void *inp, void *out, size_t len,
#		const AES_KEY *key, unsigned char iv[16],
#		struct { u128 Xi,H,Htbl[9]; } *Xip);
$code.=<<___;
.globl	aesni_gcm_decrypt
.type	aesni_gcm_decrypt,\@function,6
.align	32
aesni_gcm_decrypt:
.cfi_startproc
	xor	$ret,$ret
	cmp	\$0x60,$len			# minimal accepted length
	jb	.Lgcm_dec_abort

	lea	(%rsp),%rax			# save stack pointer
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
	lea	-0xa8(%rsp),%rsp
	movaps	%xmm6,-0xd8(%rax)
	movaps	%xmm7,-0xc8(%rax)
	movaps	%xmm8,-0xb8(%rax)
	movaps	%xmm9,-0xa8(%rax)
	movaps	%xmm10,-0x98(%rax)
	movaps	%xmm11,-0x88(%rax)
	movaps	%xmm12,-0x78(%rax)
	movaps	%xmm13,-0x68(%rax)
	movaps	%xmm14,-0x58(%rax)
	movaps	%xmm15,-0x48(%rax)
.Lgcm_dec_body:
___
$code.=<<___;
	vzeroupper

	vmovdqu		($ivp),$T1		# input counter value
	add		\$-128,%rsp
	mov		12($ivp),$counter
	lea		.Lbswap_mask(%rip),$const
	lea		-0x80($key),$in0	# borrow $in0
	mov		\$0xf80,$end0		# borrow $end0
	vmovdqu		($Xip),$Xi		# load Xi
	and		\$-128,%rsp		# ensure stack alignment
	vmovdqu		($const),$Ii		# borrow $Ii for .Lbswap_mask
	lea		0x80($key),$key		# size optimization
	lea		0x20+0x20($Xip),$Xip	# size optimization
	mov		0xf0-0x80($key),$rounds
	vpshufb		$Ii,$Xi,$Xi

	and		$end0,$in0
	and		%rsp,$end0
	sub		$in0,$end0
	jc		.Ldec_no_key_aliasing
	cmp		\$768,$end0
	jnc		.Ldec_no_key_aliasing
	sub		$end0,%rsp		# avoid aliasing with key
.Ldec_no_key_aliasing:

	vmovdqu		0x50($inp),$Z3		# I[5]
	lea		($inp),$in0
	vmovdqu		0x40($inp),$Z0
	lea		-0xc0($inp,$len),$end0
	vmovdqu		0x30($inp),$Z1
	shr		\$4,$len
	xor		$ret,$ret
	vmovdqu		0x20($inp),$Z2
	 vpshufb	$Ii,$Z3,$Z3		# passed to _aesni_ctr32_ghash_6x
	vmovdqu		0x10($inp),$T2
	 vpshufb	$Ii,$Z0,$Z0
	vmovdqu		($inp),$Hkey
	 vpshufb	$Ii,$Z1,$Z1
	vmovdqu		$Z0,0x30(%rsp)
	 vpshufb	$Ii,$Z2,$Z2
	vmovdqu		$Z1,0x40(%rsp)
	 vpshufb	$Ii,$T2,$T2
	vmovdqu		$Z2,0x50(%rsp)
	 vpshufb	$Ii,$Hkey,$Hkey
	vmovdqu		$T2,0x60(%rsp)
	vmovdqu		$Hkey,0x70(%rsp)

	call		_aesni_ctr32_ghash_6x

	vmovups		$inout0,-0x60($out)	# save output
	vmovups		$inout1,-0x50($out)
	vmovups		$inout2,-0x40($out)
	vmovups		$inout3,-0x30($out)
	vmovups		$inout4,-0x20($out)
	vmovups		$inout5,-0x10($out)

	vpshufb		($const),$Xi,$Xi	# .Lbswap_mask
	vmovdqu		$Xi,-0x40($Xip)		# output Xi

	vzeroupper
___
$code.=<<___ if ($win64);
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
.Lgcm_dec_abort:
	mov	$ret,%rax		# return value
	ret
.cfi_endproc
.size	aesni_gcm_decrypt,.-aesni_gcm_decrypt
___

$code.=<<___;
.type	_aesni_ctr32_6x,\@abi-omnipotent
.align	32
_aesni_ctr32_6x:
	vmovdqu		0x00-0x80($key),$Z0	# borrow $Z0 for $rndkey
	vmovdqu		0x20($const),$T2	# borrow $T2, .Lone_msb
	lea		-1($rounds),%r13
	vmovups		0x10-0x80($key),$rndkey
	lea		0x20-0x80($key),%r12
	vpxor		$Z0,$T1,$inout0
	add		\$`6<<24`,$counter
	jc		.Lhandle_ctr32_2
	vpaddb		$T2,$T1,$inout1
	vpaddb		$T2,$inout1,$inout2
	vpxor		$Z0,$inout1,$inout1
	vpaddb		$T2,$inout2,$inout3
	vpxor		$Z0,$inout2,$inout2
	vpaddb		$T2,$inout3,$inout4
	vpxor		$Z0,$inout3,$inout3
	vpaddb		$T2,$inout4,$inout5
	vpxor		$Z0,$inout4,$inout4
	vpaddb		$T2,$inout5,$T1
	vpxor		$Z0,$inout5,$inout5
	jmp		.Loop_ctr32

.align	16
.Loop_ctr32:
	vaesenc		$rndkey,$inout0,$inout0
	vaesenc		$rndkey,$inout1,$inout1
	vaesenc		$rndkey,$inout2,$inout2
	vaesenc		$rndkey,$inout3,$inout3
	vaesenc		$rndkey,$inout4,$inout4
	vaesenc		$rndkey,$inout5,$inout5
	vmovups		(%r12),$rndkey
	lea		0x10(%r12),%r12
	dec		%r13d
	jnz		.Loop_ctr32

	vmovdqu		(%r12),$Hkey		# last round key
	vaesenc		$rndkey,$inout0,$inout0
	vpxor		0x00($inp),$Hkey,$Z0
	vaesenc		$rndkey,$inout1,$inout1
	vpxor		0x10($inp),$Hkey,$Z1
	vaesenc		$rndkey,$inout2,$inout2
	vpxor		0x20($inp),$Hkey,$Z2
	vaesenc		$rndkey,$inout3,$inout3
	vpxor		0x30($inp),$Hkey,$Xi
	vaesenc		$rndkey,$inout4,$inout4
	vpxor		0x40($inp),$Hkey,$T2
	vaesenc		$rndkey,$inout5,$inout5
	vpxor		0x50($inp),$Hkey,$Hkey
	lea		0x60($inp),$inp

	vaesenclast	$Z0,$inout0,$inout0
	vaesenclast	$Z1,$inout1,$inout1
	vaesenclast	$Z2,$inout2,$inout2
	vaesenclast	$Xi,$inout3,$inout3
	vaesenclast	$T2,$inout4,$inout4
	vaesenclast	$Hkey,$inout5,$inout5
	vmovups		$inout0,0x00($out)
	vmovups		$inout1,0x10($out)
	vmovups		$inout2,0x20($out)
	vmovups		$inout3,0x30($out)
	vmovups		$inout4,0x40($out)
	vmovups		$inout5,0x50($out)
	lea		0x60($out),$out

	ret
.align	32
.Lhandle_ctr32_2:
	vpshufb		$Ii,$T1,$Z2		# byte-swap counter
	vmovdqu		0x30($const),$Z1	# borrow $Z1, .Ltwo_lsb
	vpaddd		0x40($const),$Z2,$inout1	# .Lone_lsb
	vpaddd		$Z1,$Z2,$inout2
	vpaddd		$Z1,$inout1,$inout3
	vpshufb		$Ii,$inout1,$inout1
	vpaddd		$Z1,$inout2,$inout4
	vpshufb		$Ii,$inout2,$inout2
	vpxor		$Z0,$inout1,$inout1
	vpaddd		$Z1,$inout3,$inout5
	vpshufb		$Ii,$inout3,$inout3
	vpxor		$Z0,$inout2,$inout2
	vpaddd		$Z1,$inout4,$T1		# byte-swapped next counter value
	vpshufb		$Ii,$inout4,$inout4
	vpxor		$Z0,$inout3,$inout3
	vpshufb		$Ii,$inout5,$inout5
	vpxor		$Z0,$inout4,$inout4
	vpshufb		$Ii,$T1,$T1		# next counter value
	vpxor		$Z0,$inout5,$inout5
	jmp	.Loop_ctr32
.size	_aesni_ctr32_6x,.-_aesni_ctr32_6x

.globl	aesni_gcm_encrypt
.type	aesni_gcm_encrypt,\@function,6
.align	32
aesni_gcm_encrypt:
.cfi_startproc
	xor	$ret,$ret
	cmp	\$0x60*3,$len			# minimal accepted length
	jb	.Lgcm_enc_abort

	lea	(%rsp),%rax			# save stack pointer
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
	lea	-0xa8(%rsp),%rsp
	movaps	%xmm6,-0xd8(%rax)
	movaps	%xmm7,-0xc8(%rax)
	movaps	%xmm8,-0xb8(%rax)
	movaps	%xmm9,-0xa8(%rax)
	movaps	%xmm10,-0x98(%rax)
	movaps	%xmm11,-0x88(%rax)
	movaps	%xmm12,-0x78(%rax)
	movaps	%xmm13,-0x68(%rax)
	movaps	%xmm14,-0x58(%rax)
	movaps	%xmm15,-0x48(%rax)
.Lgcm_enc_body:
___
$code.=<<___;
	vzeroupper

	vmovdqu		($ivp),$T1		# input counter value
	add		\$-128,%rsp
	mov		12($ivp),$counter
	lea		.Lbswap_mask(%rip),$const
	lea		-0x80($key),$in0	# borrow $in0
	mov		\$0xf80,$end0		# borrow $end0
	lea		0x80($key),$key		# size optimization
	vmovdqu		($const),$Ii		# borrow $Ii for .Lbswap_mask
	and		\$-128,%rsp		# ensure stack alignment
	mov		0xf0-0x80($key),$rounds

	and		$end0,$in0
	and		%rsp,$end0
	sub		$in0,$end0
	jc		.Lenc_no_key_aliasing
	cmp		\$768,$end0
	jnc		.Lenc_no_key_aliasing
	sub		$end0,%rsp		# avoid aliasing with key
.Lenc_no_key_aliasing:

	lea		($out),$in0
	lea		-0xc0($out,$len),$end0
	shr		\$4,$len

	call		_aesni_ctr32_6x
	vpshufb		$Ii,$inout0,$Xi		# save bswapped output on stack
	vpshufb		$Ii,$inout1,$T2
	vmovdqu		$Xi,0x70(%rsp)
	vpshufb		$Ii,$inout2,$Z0
	vmovdqu		$T2,0x60(%rsp)
	vpshufb		$Ii,$inout3,$Z1
	vmovdqu		$Z0,0x50(%rsp)
	vpshufb		$Ii,$inout4,$Z2
	vmovdqu		$Z1,0x40(%rsp)
	vpshufb		$Ii,$inout5,$Z3		# passed to _aesni_ctr32_ghash_6x
	vmovdqu		$Z2,0x30(%rsp)

	call		_aesni_ctr32_6x

	vmovdqu		($Xip),$Xi		# load Xi
	lea		0x20+0x20($Xip),$Xip	# size optimization
	sub		\$12,$len
	mov		\$0x60*2,$ret
	vpshufb		$Ii,$Xi,$Xi

	call		_aesni_ctr32_ghash_6x
	vmovdqu		0x20(%rsp),$Z3		# I[5]
	 vmovdqu	($const),$Ii		# borrow $Ii for .Lbswap_mask
	vmovdqu		0x00-0x20($Xip),$Hkey	# $Hkey^1
	vpunpckhqdq	$Z3,$Z3,$T1
	vmovdqu		0x20-0x20($Xip),$rndkey	# borrow $rndkey for $HK
	 vmovups	$inout0,-0x60($out)	# save output
	 vpshufb	$Ii,$inout0,$inout0	# but keep bswapped copy
	vpxor		$Z3,$T1,$T1
	 vmovups	$inout1,-0x50($out)
	 vpshufb	$Ii,$inout1,$inout1
	 vmovups	$inout2,-0x40($out)
	 vpshufb	$Ii,$inout2,$inout2
	 vmovups	$inout3,-0x30($out)
	 vpshufb	$Ii,$inout3,$inout3
	 vmovups	$inout4,-0x20($out)
	 vpshufb	$Ii,$inout4,$inout4
	 vmovups	$inout5,-0x10($out)
	 vpshufb	$Ii,$inout5,$inout5
	 vmovdqu	$inout0,0x10(%rsp)	# free $inout0
___
{ my ($HK,$T3)=($rndkey,$inout0);

$code.=<<___;
	 vmovdqu	0x30(%rsp),$Z2		# I[4]
	 vmovdqu	0x10-0x20($Xip),$Ii	# borrow $Ii for $Hkey^2
	 vpunpckhqdq	$Z2,$Z2,$T2
	vpclmulqdq	\$0x00,$Hkey,$Z3,$Z1
	 vpxor		$Z2,$T2,$T2
	vpclmulqdq	\$0x11,$Hkey,$Z3,$Z3
	vpclmulqdq	\$0x00,$HK,$T1,$T1

	 vmovdqu	0x40(%rsp),$T3		# I[3]
	vpclmulqdq	\$0x00,$Ii,$Z2,$Z0
	 vmovdqu	0x30-0x20($Xip),$Hkey	# $Hkey^3
	vpxor		$Z1,$Z0,$Z0
	 vpunpckhqdq	$T3,$T3,$Z1
	vpclmulqdq	\$0x11,$Ii,$Z2,$Z2
	 vpxor		$T3,$Z1,$Z1
	vpxor		$Z3,$Z2,$Z2
	vpclmulqdq	\$0x10,$HK,$T2,$T2
	 vmovdqu	0x50-0x20($Xip),$HK
	vpxor		$T1,$T2,$T2

	 vmovdqu	0x50(%rsp),$T1		# I[2]
	vpclmulqdq	\$0x00,$Hkey,$T3,$Z3
	 vmovdqu	0x40-0x20($Xip),$Ii	# borrow $Ii for $Hkey^4
	vpxor		$Z0,$Z3,$Z3
	 vpunpckhqdq	$T1,$T1,$Z0
	vpclmulqdq	\$0x11,$Hkey,$T3,$T3
	 vpxor		$T1,$Z0,$Z0
	vpxor		$Z2,$T3,$T3
	vpclmulqdq	\$0x00,$HK,$Z1,$Z1
	vpxor		$T2,$Z1,$Z1

	 vmovdqu	0x60(%rsp),$T2		# I[1]
	vpclmulqdq	\$0x00,$Ii,$T1,$Z2
	 vmovdqu	0x60-0x20($Xip),$Hkey	# $Hkey^5
	vpxor		$Z3,$Z2,$Z2
	 vpunpckhqdq	$T2,$T2,$Z3
	vpclmulqdq	\$0x11,$Ii,$T1,$T1
	 vpxor		$T2,$Z3,$Z3
	vpxor		$T3,$T1,$T1
	vpclmulqdq	\$0x10,$HK,$Z0,$Z0
	 vmovdqu	0x80-0x20($Xip),$HK
	vpxor		$Z1,$Z0,$Z0

	 vpxor		0x70(%rsp),$Xi,$Xi	# accumulate I[0]
	vpclmulqdq	\$0x00,$Hkey,$T2,$Z1
	 vmovdqu	0x70-0x20($Xip),$Ii	# borrow $Ii for $Hkey^6
	 vpunpckhqdq	$Xi,$Xi,$T3
	vpxor		$Z2,$Z1,$Z1
	vpclmulqdq	\$0x11,$Hkey,$T2,$T2
	 vpxor		$Xi,$T3,$T3
	vpxor		$T1,$T2,$T2
	vpclmulqdq	\$0x00,$HK,$Z3,$Z3
	vpxor		$Z0,$Z3,$Z0

	vpclmulqdq	\$0x00,$Ii,$Xi,$Z2
	 vmovdqu	0x00-0x20($Xip),$Hkey	# $Hkey^1
	 vpunpckhqdq	$inout5,$inout5,$T1
	vpclmulqdq	\$0x11,$Ii,$Xi,$Xi
	 vpxor		$inout5,$T1,$T1
	vpxor		$Z1,$Z2,$Z1
	vpclmulqdq	\$0x10,$HK,$T3,$T3
	 vmovdqu	0x20-0x20($Xip),$HK
	vpxor		$T2,$Xi,$Z3
	vpxor		$Z0,$T3,$Z2

	 vmovdqu	0x10-0x20($Xip),$Ii	# borrow $Ii for $Hkey^2
	  vpxor		$Z1,$Z3,$T3		# aggregated Karatsuba post-processing
	vpclmulqdq	\$0x00,$Hkey,$inout5,$Z0
	  vpxor		$T3,$Z2,$Z2
	 vpunpckhqdq	$inout4,$inout4,$T2
	vpclmulqdq	\$0x11,$Hkey,$inout5,$inout5
	 vpxor		$inout4,$T2,$T2
	  vpslldq	\$8,$Z2,$T3
	vpclmulqdq	\$0x00,$HK,$T1,$T1
	  vpxor		$T3,$Z1,$Xi
	  vpsrldq	\$8,$Z2,$Z2
	  vpxor		$Z2,$Z3,$Z3

	vpclmulqdq	\$0x00,$Ii,$inout4,$Z1
	 vmovdqu	0x30-0x20($Xip),$Hkey	# $Hkey^3
	vpxor		$Z0,$Z1,$Z1
	 vpunpckhqdq	$inout3,$inout3,$T3
	vpclmulqdq	\$0x11,$Ii,$inout4,$inout4
	 vpxor		$inout3,$T3,$T3
	vpxor		$inout5,$inout4,$inout4
	  vpalignr	\$8,$Xi,$Xi,$inout5	# 1st phase
	vpclmulqdq	\$0x10,$HK,$T2,$T2
	 vmovdqu	0x50-0x20($Xip),$HK
	vpxor		$T1,$T2,$T2

	vpclmulqdq	\$0x00,$Hkey,$inout3,$Z0
	 vmovdqu	0x40-0x20($Xip),$Ii	# borrow $Ii for $Hkey^4
	vpxor		$Z1,$Z0,$Z0
	 vpunpckhqdq	$inout2,$inout2,$T1
	vpclmulqdq	\$0x11,$Hkey,$inout3,$inout3
	 vpxor		$inout2,$T1,$T1
	vpxor		$inout4,$inout3,$inout3
	  vxorps	0x10(%rsp),$Z3,$Z3	# accumulate $inout0
	vpclmulqdq	\$0x00,$HK,$T3,$T3
	vpxor		$T2,$T3,$T3

	  vpclmulqdq	\$0x10,0x10($const),$Xi,$Xi
	  vxorps	$inout5,$Xi,$Xi

	vpclmulqdq	\$0x00,$Ii,$inout2,$Z1
	 vmovdqu	0x60-0x20($Xip),$Hkey	# $Hkey^5
	vpxor		$Z0,$Z1,$Z1
	 vpunpckhqdq	$inout1,$inout1,$T2
	vpclmulqdq	\$0x11,$Ii,$inout2,$inout2
	 vpxor		$inout1,$T2,$T2
	  vpalignr	\$8,$Xi,$Xi,$inout5	# 2nd phase
	vpxor		$inout3,$inout2,$inout2
	vpclmulqdq	\$0x10,$HK,$T1,$T1
	 vmovdqu	0x80-0x20($Xip),$HK
	vpxor		$T3,$T1,$T1

	  vxorps	$Z3,$inout5,$inout5
	  vpclmulqdq	\$0x10,0x10($const),$Xi,$Xi
	  vxorps	$inout5,$Xi,$Xi

	vpclmulqdq	\$0x00,$Hkey,$inout1,$Z0
	 vmovdqu	0x70-0x20($Xip),$Ii	# borrow $Ii for $Hkey^6
	vpxor		$Z1,$Z0,$Z0
	 vpunpckhqdq	$Xi,$Xi,$T3
	vpclmulqdq	\$0x11,$Hkey,$inout1,$inout1
	 vpxor		$Xi,$T3,$T3
	vpxor		$inout2,$inout1,$inout1
	vpclmulqdq	\$0x00,$HK,$T2,$T2
	vpxor		$T1,$T2,$T2

	vpclmulqdq	\$0x00,$Ii,$Xi,$Z1
	vpclmulqdq	\$0x11,$Ii,$Xi,$Z3
	vpxor		$Z0,$Z1,$Z1
	vpclmulqdq	\$0x10,$HK,$T3,$Z2
	vpxor		$inout1,$Z3,$Z3
	vpxor		$T2,$Z2,$Z2

	vpxor		$Z1,$Z3,$Z0		# aggregated Karatsuba post-processing
	vpxor		$Z0,$Z2,$Z2
	vpslldq		\$8,$Z2,$T1
	vmovdqu		0x10($const),$Hkey	# .Lpoly
	vpsrldq		\$8,$Z2,$Z2
	vpxor		$T1,$Z1,$Xi
	vpxor		$Z2,$Z3,$Z3

	vpalignr	\$8,$Xi,$Xi,$T2		# 1st phase
	vpclmulqdq	\$0x10,$Hkey,$Xi,$Xi
	vpxor		$T2,$Xi,$Xi

	vpalignr	\$8,$Xi,$Xi,$T2		# 2nd phase
	vpclmulqdq	\$0x10,$Hkey,$Xi,$Xi
	vpxor		$Z3,$T2,$T2
	vpxor		$T2,$Xi,$Xi
___
}
$code.=<<___;
	vpshufb		($const),$Xi,$Xi	# .Lbswap_mask
	vmovdqu		$Xi,-0x40($Xip)		# output Xi

	vzeroupper
___
$code.=<<___ if ($win64);
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
.Lgcm_enc_abort:
	mov	$ret,%rax		# return value
	ret
.cfi_endproc
.size	aesni_gcm_encrypt,.-aesni_gcm_encrypt
___

$code.=<<___;
.align	64
.Lbswap_mask:
	.byte	15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
.Lpoly:
	.byte	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xc2
.Lone_msb:
	.byte	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
.Ltwo_lsb:
	.byte	2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.Lone_lsb:
	.byte	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.asciz	"AES-NI GCM module for x86_64, CRYPTOGAMS by <appro\@openssl.org>"
.align	64
___
if ($win64) {
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___
.extern	__imp_RtlVirtualUnwind
.type	gcm_se_handler,\@abi-omnipotent
.align	16
gcm_se_handler:
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

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	mov	120($context),%rax	# pull context->Rax

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
.size	gcm_se_handler,.-gcm_se_handler

.section	.pdata
.align	4
	.rva	.LSEH_begin_aesni_gcm_decrypt
	.rva	.LSEH_end_aesni_gcm_decrypt
	.rva	.LSEH_gcm_dec_info

	.rva	.LSEH_begin_aesni_gcm_encrypt
	.rva	.LSEH_end_aesni_gcm_encrypt
	.rva	.LSEH_gcm_enc_info
.section	.xdata
.align	8
.LSEH_gcm_dec_info:
	.byte	9,0,0,0
	.rva	gcm_se_handler
	.rva	.Lgcm_dec_body,.Lgcm_dec_abort
.LSEH_gcm_enc_info:
	.byte	9,0,0,0
	.rva	gcm_se_handler
	.rva	.Lgcm_enc_body,.Lgcm_enc_abort
___
}
}}} else {{{
$code=<<___;	# assembler is too old
.text

.globl	aesni_gcm_encrypt
.type	aesni_gcm_encrypt,\@abi-omnipotent
aesni_gcm_encrypt:
	xor	%eax,%eax
	ret
.size	aesni_gcm_encrypt,.-aesni_gcm_encrypt

.globl	aesni_gcm_decrypt
.type	aesni_gcm_decrypt,\@abi-omnipotent
aesni_gcm_decrypt:
	xor	%eax,%eax
	ret
.size	aesni_gcm_decrypt,.-aesni_gcm_decrypt
___
}}}

$code =~ s/\`([^\`]*)\`/eval($1)/gem;

print $code;

close STDOUT;

#! /usr/bin/env perl
# Author: Marc Bevand <bevand_m (at) epita.fr>
# Copyright 2005-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

# MD5 optimized for AMD64.

use strict;

my $code;

# round1_step() does:
#   dst = x + ((dst + F(x,y,z) + X[k] + T_i) <<< s)
#   %r10d = X[k_next]
#   %r11d = z' (copy of z for the next step)
# Each round1_step() takes about 5.3 clocks (9 instructions, 1.7 IPC)
sub round1_step
{
    my ($pos, $dst, $x, $y, $z, $k_next, $T_i, $s) = @_;
    $code .= " mov	0*4(%rsi),	%r10d		/* (NEXT STEP) X[0] */\n" if ($pos == -1);
    $code .= " mov	%edx,		%r11d		/* (NEXT STEP) z' = %edx */\n" if ($pos == -1);
    $code .= <<EOF;
	xor	$y,		%r11d		/* y ^ ... */
	lea	$T_i($dst,%r10d),$dst		/* Const + dst + ... */
	and	$x,		%r11d		/* x & ... */
	mov	$k_next*4(%rsi),%r10d		/* (NEXT STEP) X[$k_next] */
	xor	$z,		%r11d		/* z ^ ... */
	add	%r11d,		$dst		/* dst += ... */
	rol	\$$s,		$dst		/* dst <<< s */
	mov	$y,		%r11d		/* (NEXT STEP) z' = $y */
	add	$x,		$dst		/* dst += x */
EOF
}

# round2_step() does:
#   dst = x + ((dst + G(x,y,z) + X[k] + T_i) <<< s)
#   %r10d = X[k_next]
#   %r11d = z' (copy of z for the next step)
#   %r12d = z' (copy of z for the next step)
# Each round2_step() takes about 5.4 clocks (11 instructions, 2.0 IPC)
sub round2_step
{
    my ($pos, $dst, $x, $y, $z, $k_next, $T_i, $s) = @_;
    $code .= " mov	%edx,		%r11d		/* (NEXT STEP) z' = %edx */\n" if ($pos == -1);
    $code .= " mov	%edx,		%r12d		/* (NEXT STEP) z' = %edx */\n" if ($pos == -1);
    $code .= <<EOF;
	not	%r11d				/* not z */
	and	$x,		%r12d		/* x & z */
	lea	$T_i($dst,%r10d),$dst		/* Const + dst + ... */
	and	$y,		%r11d		/* y & (not z) */
	mov	$k_next*4(%rsi),%r10d		/* (NEXT STEP) X[$k_next] */
	or	%r11d,		%r12d		/* (y & (not z)) | (x & z) */
	mov	$y,		%r11d		/* (NEXT STEP) z' = $y */
	add	%r12d,		$dst		/* dst += ... */
	mov	$y,		%r12d		/* (NEXT STEP) z' = $y */
	rol	\$$s,		$dst		/* dst <<< s */
	add	$x,		$dst		/* dst += x */
EOF
}

# round3_step() does:
#   dst = x + ((dst + H(x,y,z) + X[k] + T_i) <<< s)
#   %r10d = X[k_next]
#   %r11d = y' (copy of y for the next step)
# Each round3_step() takes about 4.2 clocks (8 instructions, 1.9 IPC)
{ my $round3_alter=0;
sub round3_step
{
    my ($pos, $dst, $x, $y, $z, $k_next, $T_i, $s) = @_;
    $code .= " mov	%ecx,		%r11d		/* (NEXT STEP) y' = %ecx */\n" if ($pos == -1);
    $code .= <<EOF;
	lea	$T_i($dst,%r10d),$dst		/* Const + dst + ... */
	xor	$z,		%r11d		/* z ^ ... */
	mov	$k_next*4(%rsi),%r10d		/* (NEXT STEP) X[$k_next] */
	xor	$x,		%r11d		/* x ^ ... */
	add	%r11d,		$dst		/* dst += ... */
EOF
    $code .= <<EOF if ($round3_alter);
	rol	\$$s,		$dst		/* dst <<< s */
	mov	$x,		%r11d		/* (NEXT STEP) y' = $x */
EOF
    $code .= <<EOF if (!$round3_alter);
	mov	$x,		%r11d		/* (NEXT STEP) y' = $x */
	rol	\$$s,		$dst		/* dst <<< s */
EOF
    $code .= <<EOF;
	add	$x,		$dst		/* dst += x */
EOF
    $round3_alter^=1;
}
}

# round4_step() does:
#   dst = x + ((dst + I(x,y,z) + X[k] + T_i) <<< s)
#   %r10d = X[k_next]
#   %r11d = not z' (copy of not z for the next step)
# Each round4_step() takes about 5.2 clocks (9 instructions, 1.7 IPC)
sub round4_step
{
    my ($pos, $dst, $x, $y, $z, $k_next, $T_i, $s) = @_;
    $code .= " mov	\$0xffffffff,	%r11d\n" if ($pos == -1);
    $code .= " xor	%edx,		%r11d		/* (NEXT STEP) not z' = not %edx*/\n"
    if ($pos == -1);
    $code .= <<EOF;
	lea	$T_i($dst,%r10d),$dst		/* Const + dst + ... */
	or	$x,		%r11d		/* x | ... */
	mov	$k_next*4(%rsi),%r10d		/* (NEXT STEP) X[$k_next] */
	xor	$y,		%r11d		/* y ^ ... */
	add	%r11d,		$dst		/* dst += ... */
	mov	\$0xffffffff,	%r11d
	rol	\$$s,		$dst		/* dst <<< s */
	xor	$y,		%r11d		/* (NEXT STEP) not z' = not $y */
	add	$x,		$dst		/* dst += x */
EOF
}

no warnings qw(uninitialized);
my $flavour = shift;
my $output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

my $win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; my $dir=$1; my $xlate;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

$code .= <<EOF;
.text
.align 16

.globl md5_block_asm_data_order
.type md5_block_asm_data_order,\@function,3
md5_block_asm_data_order:
.cfi_startproc
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
.Lprologue:

	# rdi = arg #1 (ctx, MD5_CTX pointer)
	# rsi = arg #2 (ptr, data pointer)
	# rdx = arg #3 (nbr, number of 16-word blocks to process)
	mov	%rdi,		%rbp	# rbp = ctx
	shl	\$6,		%rdx	# rdx = nbr in bytes
	lea	(%rsi,%rdx),	%rdi	# rdi = end
	mov	0*4(%rbp),	%eax	# eax = ctx->A
	mov	1*4(%rbp),	%ebx	# ebx = ctx->B
	mov	2*4(%rbp),	%ecx	# ecx = ctx->C
	mov	3*4(%rbp),	%edx	# edx = ctx->D
	# end is 'rdi'
	# ptr is 'rsi'
	# A is 'eax'
	# B is 'ebx'
	# C is 'ecx'
	# D is 'edx'

	cmp	%rdi,		%rsi		# cmp end with ptr
	je	.Lend				# jmp if ptr == end

	# BEGIN of loop over 16-word blocks
.Lloop:	# save old values of A, B, C, D
	mov	%eax,		%r8d
	mov	%ebx,		%r9d
	mov	%ecx,		%r14d
	mov	%edx,		%r15d
EOF
round1_step(-1,'%eax','%ebx','%ecx','%edx', '1','0xd76aa478', '7');
round1_step( 0,'%edx','%eax','%ebx','%ecx', '2','0xe8c7b756','12');
round1_step( 0,'%ecx','%edx','%eax','%ebx', '3','0x242070db','17');
round1_step( 0,'%ebx','%ecx','%edx','%eax', '4','0xc1bdceee','22');
round1_step( 0,'%eax','%ebx','%ecx','%edx', '5','0xf57c0faf', '7');
round1_step( 0,'%edx','%eax','%ebx','%ecx', '6','0x4787c62a','12');
round1_step( 0,'%ecx','%edx','%eax','%ebx', '7','0xa8304613','17');
round1_step( 0,'%ebx','%ecx','%edx','%eax', '8','0xfd469501','22');
round1_step( 0,'%eax','%ebx','%ecx','%edx', '9','0x698098d8', '7');
round1_step( 0,'%edx','%eax','%ebx','%ecx','10','0x8b44f7af','12');
round1_step( 0,'%ecx','%edx','%eax','%ebx','11','0xffff5bb1','17');
round1_step( 0,'%ebx','%ecx','%edx','%eax','12','0x895cd7be','22');
round1_step( 0,'%eax','%ebx','%ecx','%edx','13','0x6b901122', '7');
round1_step( 0,'%edx','%eax','%ebx','%ecx','14','0xfd987193','12');
round1_step( 0,'%ecx','%edx','%eax','%ebx','15','0xa679438e','17');
round1_step( 1,'%ebx','%ecx','%edx','%eax', '1','0x49b40821','22');

round2_step(-1,'%eax','%ebx','%ecx','%edx', '6','0xf61e2562', '5');
round2_step( 0,'%edx','%eax','%ebx','%ecx','11','0xc040b340', '9');
round2_step( 0,'%ecx','%edx','%eax','%ebx', '0','0x265e5a51','14');
round2_step( 0,'%ebx','%ecx','%edx','%eax', '5','0xe9b6c7aa','20');
round2_step( 0,'%eax','%ebx','%ecx','%edx','10','0xd62f105d', '5');
round2_step( 0,'%edx','%eax','%ebx','%ecx','15', '0x2441453', '9');
round2_step( 0,'%ecx','%edx','%eax','%ebx', '4','0xd8a1e681','14');
round2_step( 0,'%ebx','%ecx','%edx','%eax', '9','0xe7d3fbc8','20');
round2_step( 0,'%eax','%ebx','%ecx','%edx','14','0x21e1cde6', '5');
round2_step( 0,'%edx','%eax','%ebx','%ecx', '3','0xc33707d6', '9');
round2_step( 0,'%ecx','%edx','%eax','%ebx', '8','0xf4d50d87','14');
round2_step( 0,'%ebx','%ecx','%edx','%eax','13','0x455a14ed','20');
round2_step( 0,'%eax','%ebx','%ecx','%edx', '2','0xa9e3e905', '5');
round2_step( 0,'%edx','%eax','%ebx','%ecx', '7','0xfcefa3f8', '9');
round2_step( 0,'%ecx','%edx','%eax','%ebx','12','0x676f02d9','14');
round2_step( 1,'%ebx','%ecx','%edx','%eax', '5','0x8d2a4c8a','20');

round3_step(-1,'%eax','%ebx','%ecx','%edx', '8','0xfffa3942', '4');
round3_step( 0,'%edx','%eax','%ebx','%ecx','11','0x8771f681','11');
round3_step( 0,'%ecx','%edx','%eax','%ebx','14','0x6d9d6122','16');
round3_step( 0,'%ebx','%ecx','%edx','%eax', '1','0xfde5380c','23');
round3_step( 0,'%eax','%ebx','%ecx','%edx', '4','0xa4beea44', '4');
round3_step( 0,'%edx','%eax','%ebx','%ecx', '7','0x4bdecfa9','11');
round3_step( 0,'%ecx','%edx','%eax','%ebx','10','0xf6bb4b60','16');
round3_step( 0,'%ebx','%ecx','%edx','%eax','13','0xbebfbc70','23');
round3_step( 0,'%eax','%ebx','%ecx','%edx', '0','0x289b7ec6', '4');
round3_step( 0,'%edx','%eax','%ebx','%ecx', '3','0xeaa127fa','11');
round3_step( 0,'%ecx','%edx','%eax','%ebx', '6','0xd4ef3085','16');
round3_step( 0,'%ebx','%ecx','%edx','%eax', '9', '0x4881d05','23');
round3_step( 0,'%eax','%ebx','%ecx','%edx','12','0xd9d4d039', '4');
round3_step( 0,'%edx','%eax','%ebx','%ecx','15','0xe6db99e5','11');
round3_step( 0,'%ecx','%edx','%eax','%ebx', '2','0x1fa27cf8','16');
round3_step( 1,'%ebx','%ecx','%edx','%eax', '0','0xc4ac5665','23');

round4_step(-1,'%eax','%ebx','%ecx','%edx', '7','0xf4292244', '6');
round4_step( 0,'%edx','%eax','%ebx','%ecx','14','0x432aff97','10');
round4_step( 0,'%ecx','%edx','%eax','%ebx', '5','0xab9423a7','15');
round4_step( 0,'%ebx','%ecx','%edx','%eax','12','0xfc93a039','21');
round4_step( 0,'%eax','%ebx','%ecx','%edx', '3','0x655b59c3', '6');
round4_step( 0,'%edx','%eax','%ebx','%ecx','10','0x8f0ccc92','10');
round4_step( 0,'%ecx','%edx','%eax','%ebx', '1','0xffeff47d','15');
round4_step( 0,'%ebx','%ecx','%edx','%eax', '8','0x85845dd1','21');
round4_step( 0,'%eax','%ebx','%ecx','%edx','15','0x6fa87e4f', '6');
round4_step( 0,'%edx','%eax','%ebx','%ecx', '6','0xfe2ce6e0','10');
round4_step( 0,'%ecx','%edx','%eax','%ebx','13','0xa3014314','15');
round4_step( 0,'%ebx','%ecx','%edx','%eax', '4','0x4e0811a1','21');
round4_step( 0,'%eax','%ebx','%ecx','%edx','11','0xf7537e82', '6');
round4_step( 0,'%edx','%eax','%ebx','%ecx', '2','0xbd3af235','10');
round4_step( 0,'%ecx','%edx','%eax','%ebx', '9','0x2ad7d2bb','15');
round4_step( 1,'%ebx','%ecx','%edx','%eax', '0','0xeb86d391','21');
$code .= <<EOF;
	# add old values of A, B, C, D
	add	%r8d,	%eax
	add	%r9d,	%ebx
	add	%r14d,	%ecx
	add	%r15d,	%edx

	# loop control
	add	\$64,		%rsi		# ptr += 64
	cmp	%rdi,		%rsi		# cmp end with ptr
	jb	.Lloop				# jmp if ptr < end
	# END of loop over 16-word blocks

.Lend:
	mov	%eax,		0*4(%rbp)	# ctx->A = A
	mov	%ebx,		1*4(%rbp)	# ctx->B = B
	mov	%ecx,		2*4(%rbp)	# ctx->C = C
	mov	%edx,		3*4(%rbp)	# ctx->D = D

	mov	(%rsp),%r15
.cfi_restore	%r15
	mov	8(%rsp),%r14
.cfi_restore	%r14
	mov	16(%rsp),%r12
.cfi_restore	%r12
	mov	24(%rsp),%rbx
.cfi_restore	%rbx
	mov	32(%rsp),%rbp
.cfi_restore	%rbp
	add	\$40,%rsp
.cfi_adjust_cfa_offset	-40
.Lepilogue:
	ret
.cfi_endproc
.size md5_block_asm_data_order,.-md5_block_asm_data_order
EOF

# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
if ($win64) {
my $rec="%rcx";
my $frame="%rdx";
my $context="%r8";
my $disp="%r9";

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

	lea	.Lprologue(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<.Lprologue
	jb	.Lin_prologue

	mov	152($context),%rax	# pull context->Rsp

	lea	.Lepilogue(%rip),%r10
	cmp	%r10,%rbx		# context->Rip>=.Lepilogue
	jae	.Lin_prologue

	lea	40(%rax),%rax

	mov	-8(%rax),%rbp
	mov	-16(%rax),%rbx
	mov	-24(%rax),%r12
	mov	-32(%rax),%r14
	mov	-40(%rax),%r15
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12
	mov	%r14,232($context)	# restore context->R14
	mov	%r15,240($context)	# restore context->R15

.Lin_prologue:
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
	.rva	.LSEH_begin_md5_block_asm_data_order
	.rva	.LSEH_end_md5_block_asm_data_order
	.rva	.LSEH_info_md5_block_asm_data_order

.section	.xdata
.align	8
.LSEH_info_md5_block_asm_data_order:
	.byte	9,0,0,0
	.rva	se_handler
___
}

print $code;

close STDOUT;

#! /usr/bin/env perl
# Copyright 2014-2019 The OpenSSL Project Authors. All Rights Reserved.
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
# This module implements support for ARMv8 AES instructions. The
# module is endian-agnostic in sense that it supports both big- and
# little-endian cases. As does it support both 32- and 64-bit modes
# of operation. Latter is achieved by limiting amount of utilized
# registers to 16, which implies additional NEON load and integer
# instructions. This has no effect on mighty Apple A7, where results
# are literally equal to the theoretical estimates based on AES
# instruction latencies and issue rates. On Cortex-A53, an in-order
# execution core, this costs up to 10-15%, which is partially
# compensated by implementing dedicated code path for 128-bit
# CBC encrypt case. On Cortex-A57 parallelizable mode performance
# seems to be limited by sheer amount of NEON instructions...
#
# Performance in cycles per byte processed with 128-bit key:
#
#		CBC enc		CBC dec		CTR
# Apple A7	2.39		1.20		1.20
# Cortex-A53	1.32		1.29		1.46
# Cortex-A57(*)	1.95		0.85		0.93
# Denver	1.96		0.86		0.80
# Mongoose	1.33		1.20		1.20
# Kryo		1.26		0.94		1.00
#
# (*)	original 3.64/1.34/1.32 results were for r0p0 revision
#	and are still same even for updated module;

$flavour = shift;
$output  = shift;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/arm-xlate.pl" and -f $xlate) or
die "can't locate arm-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

$prefix="aes_v8";

$code=<<___;
#include "arm_arch.h"

#if __ARM_MAX_ARCH__>=7
.text
___
# $code.=".arch	armv8-a+crypto\n"			if ($flavour =~ /64/);
$code.=<<___						if ($flavour !~ /64/);
.arch	armv7-a	// don't confuse not-so-latest binutils with argv8 :-)
.fpu	neon
.code	32
#undef	__thumb2__
___

# Assembler mnemonics are an eclectic mix of 32- and 64-bit syntax,
# NEON is mostly 32-bit mnemonics, integer - mostly 64. Goal is to
# maintain both 32- and 64-bit codes within single module and
# transliterate common code to either flavour with regex vodoo.
#
{{{
my ($inp,$bits,$out,$ptr,$rounds)=("x0","w1","x2","x3","w12");
my ($zero,$rcon,$mask,$in0,$in1,$tmp,$key)=
	$flavour=~/64/? map("q$_",(0..6)) : map("q$_",(0..3,8..10));


$code.=<<___;
.align	5
.Lrcon:
.long	0x01,0x01,0x01,0x01
.long	0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d	// rotate-n-splat
.long	0x1b,0x1b,0x1b,0x1b

.globl	${prefix}_set_encrypt_key
.type	${prefix}_set_encrypt_key,%function
.align	5
${prefix}_set_encrypt_key:
.Lenc_key:
___
$code.=<<___	if ($flavour =~ /64/);
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
___
$code.=<<___;
	mov	$ptr,#-1
	cmp	$inp,#0
	b.eq	.Lenc_key_abort
	cmp	$out,#0
	b.eq	.Lenc_key_abort
	mov	$ptr,#-2
	cmp	$bits,#128
	b.lt	.Lenc_key_abort
	cmp	$bits,#256
	b.gt	.Lenc_key_abort
	tst	$bits,#0x3f
	b.ne	.Lenc_key_abort

	adr	$ptr,.Lrcon
	cmp	$bits,#192

	veor	$zero,$zero,$zero
	vld1.8	{$in0},[$inp],#16
	mov	$bits,#8		// reuse $bits
	vld1.32	{$rcon,$mask},[$ptr],#32

	b.lt	.Loop128
	b.eq	.L192
	b	.L256

.align	4
.Loop128:
	vtbl.8	$key,{$in0},$mask
	vext.8	$tmp,$zero,$in0,#12
	vst1.32	{$in0},[$out],#16
	aese	$key,$zero
	subs	$bits,$bits,#1

	veor	$in0,$in0,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	veor	$in0,$in0,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	 veor	$key,$key,$rcon
	veor	$in0,$in0,$tmp
	vshl.u8	$rcon,$rcon,#1
	veor	$in0,$in0,$key
	b.ne	.Loop128

	vld1.32	{$rcon},[$ptr]

	vtbl.8	$key,{$in0},$mask
	vext.8	$tmp,$zero,$in0,#12
	vst1.32	{$in0},[$out],#16
	aese	$key,$zero

	veor	$in0,$in0,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	veor	$in0,$in0,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	 veor	$key,$key,$rcon
	veor	$in0,$in0,$tmp
	vshl.u8	$rcon,$rcon,#1
	veor	$in0,$in0,$key

	vtbl.8	$key,{$in0},$mask
	vext.8	$tmp,$zero,$in0,#12
	vst1.32	{$in0},[$out],#16
	aese	$key,$zero

	veor	$in0,$in0,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	veor	$in0,$in0,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	 veor	$key,$key,$rcon
	veor	$in0,$in0,$tmp
	veor	$in0,$in0,$key
	vst1.32	{$in0},[$out]
	add	$out,$out,#0x50

	mov	$rounds,#10
	b	.Ldone

.align	4
.L192:
	vld1.8	{$in1},[$inp],#8
	vmov.i8	$key,#8			// borrow $key
	vst1.32	{$in0},[$out],#16
	vsub.i8	$mask,$mask,$key	// adjust the mask

.Loop192:
	vtbl.8	$key,{$in1},$mask
	vext.8	$tmp,$zero,$in0,#12
	vst1.32	{$in1},[$out],#8
	aese	$key,$zero
	subs	$bits,$bits,#1

	veor	$in0,$in0,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	veor	$in0,$in0,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	veor	$in0,$in0,$tmp

	vdup.32	$tmp,${in0}[3]
	veor	$tmp,$tmp,$in1
	 veor	$key,$key,$rcon
	vext.8	$in1,$zero,$in1,#12
	vshl.u8	$rcon,$rcon,#1
	veor	$in1,$in1,$tmp
	veor	$in0,$in0,$key
	veor	$in1,$in1,$key
	vst1.32	{$in0},[$out],#16
	b.ne	.Loop192

	mov	$rounds,#12
	add	$out,$out,#0x20
	b	.Ldone

.align	4
.L256:
	vld1.8	{$in1},[$inp]
	mov	$bits,#7
	mov	$rounds,#14
	vst1.32	{$in0},[$out],#16

.Loop256:
	vtbl.8	$key,{$in1},$mask
	vext.8	$tmp,$zero,$in0,#12
	vst1.32	{$in1},[$out],#16
	aese	$key,$zero
	subs	$bits,$bits,#1

	veor	$in0,$in0,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	veor	$in0,$in0,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	 veor	$key,$key,$rcon
	veor	$in0,$in0,$tmp
	vshl.u8	$rcon,$rcon,#1
	veor	$in0,$in0,$key
	vst1.32	{$in0},[$out],#16
	b.eq	.Ldone

	vdup.32	$key,${in0}[3]		// just splat
	vext.8	$tmp,$zero,$in1,#12
	aese	$key,$zero

	veor	$in1,$in1,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	veor	$in1,$in1,$tmp
	vext.8	$tmp,$zero,$tmp,#12
	veor	$in1,$in1,$tmp

	veor	$in1,$in1,$key
	b	.Loop256

.Ldone:
	str	$rounds,[$out]
	mov	$ptr,#0

.Lenc_key_abort:
	mov	x0,$ptr			// return value
	`"ldr	x29,[sp],#16"		if ($flavour =~ /64/)`
	ret
.size	${prefix}_set_encrypt_key,.-${prefix}_set_encrypt_key

.globl	${prefix}_set_decrypt_key
.type	${prefix}_set_decrypt_key,%function
.align	5
${prefix}_set_decrypt_key:
___
$code.=<<___	if ($flavour =~ /64/);
	.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
___
$code.=<<___	if ($flavour !~ /64/);
	stmdb	sp!,{r4,lr}
___
$code.=<<___;
	bl	.Lenc_key

	cmp	x0,#0
	b.ne	.Ldec_key_abort

	sub	$out,$out,#240		// restore original $out
	mov	x4,#-16
	add	$inp,$out,x12,lsl#4	// end of key schedule

	vld1.32	{v0.16b},[$out]
	vld1.32	{v1.16b},[$inp]
	vst1.32	{v0.16b},[$inp],x4
	vst1.32	{v1.16b},[$out],#16

.Loop_imc:
	vld1.32	{v0.16b},[$out]
	vld1.32	{v1.16b},[$inp]
	aesimc	v0.16b,v0.16b
	aesimc	v1.16b,v1.16b
	vst1.32	{v0.16b},[$inp],x4
	vst1.32	{v1.16b},[$out],#16
	cmp	$inp,$out
	b.hi	.Loop_imc

	vld1.32	{v0.16b},[$out]
	aesimc	v0.16b,v0.16b
	vst1.32	{v0.16b},[$inp]

	eor	x0,x0,x0		// return value
.Ldec_key_abort:
___
$code.=<<___	if ($flavour !~ /64/);
	ldmia	sp!,{r4,pc}
___
$code.=<<___	if ($flavour =~ /64/);
	ldp	x29,x30,[sp],#16
	.inst	0xd50323bf		// autiasp
	ret
___
$code.=<<___;
.size	${prefix}_set_decrypt_key,.-${prefix}_set_decrypt_key
___
}}}
{{{
sub gen_block () {
my $dir = shift;
my ($e,$mc) = $dir eq "en" ? ("e","mc") : ("d","imc");
my ($inp,$out,$key)=map("x$_",(0..2));
my $rounds="w3";
my ($rndkey0,$rndkey1,$inout)=map("q$_",(0..3));

$code.=<<___;
.globl	${prefix}_${dir}crypt
.type	${prefix}_${dir}crypt,%function
.align	5
${prefix}_${dir}crypt:
	ldr	$rounds,[$key,#240]
	vld1.32	{$rndkey0},[$key],#16
	vld1.8	{$inout},[$inp]
	sub	$rounds,$rounds,#2
	vld1.32	{$rndkey1},[$key],#16

.Loop_${dir}c:
	aes$e	$inout,$rndkey0
	aes$mc	$inout,$inout
	vld1.32	{$rndkey0},[$key],#16
	subs	$rounds,$rounds,#2
	aes$e	$inout,$rndkey1
	aes$mc	$inout,$inout
	vld1.32	{$rndkey1},[$key],#16
	b.gt	.Loop_${dir}c

	aes$e	$inout,$rndkey0
	aes$mc	$inout,$inout
	vld1.32	{$rndkey0},[$key]
	aes$e	$inout,$rndkey1
	veor	$inout,$inout,$rndkey0

	vst1.8	{$inout},[$out]
	ret
.size	${prefix}_${dir}crypt,.-${prefix}_${dir}crypt
___
}
&gen_block("en");
&gen_block("de");
}}}
{{{
my ($inp,$out,$len,$key,$ivp)=map("x$_",(0..4)); my $enc="w5";
my ($rounds,$cnt,$key_,$step,$step1)=($enc,"w6","x7","x8","x12");
my ($dat0,$dat1,$in0,$in1,$tmp0,$tmp1,$ivec,$rndlast)=map("q$_",(0..7));

my ($dat,$tmp,$rndzero_n_last)=($dat0,$tmp0,$tmp1);
my ($key4,$key5,$key6,$key7)=("x6","x12","x14",$key);

### q8-q15	preloaded key schedule

$code.=<<___;
.globl	${prefix}_cbc_encrypt
.type	${prefix}_cbc_encrypt,%function
.align	5
${prefix}_cbc_encrypt:
___
$code.=<<___	if ($flavour =~ /64/);
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
___
$code.=<<___	if ($flavour !~ /64/);
	mov	ip,sp
	stmdb	sp!,{r4-r8,lr}
	vstmdb	sp!,{d8-d15}            @ ABI specification says so
	ldmia	ip,{r4-r5}		@ load remaining args
___
$code.=<<___;
	subs	$len,$len,#16
	mov	$step,#16
	b.lo	.Lcbc_abort
	cclr	$step,eq

	cmp	$enc,#0			// en- or decrypting?
	ldr	$rounds,[$key,#240]
	and	$len,$len,#-16
	vld1.8	{$ivec},[$ivp]
	vld1.8	{$dat},[$inp],$step

	vld1.32	{q8-q9},[$key]		// load key schedule...
	sub	$rounds,$rounds,#6
	add	$key_,$key,x5,lsl#4	// pointer to last 7 round keys
	sub	$rounds,$rounds,#2
	vld1.32	{q10-q11},[$key_],#32
	vld1.32	{q12-q13},[$key_],#32
	vld1.32	{q14-q15},[$key_],#32
	vld1.32	{$rndlast},[$key_]

	add	$key_,$key,#32
	mov	$cnt,$rounds
	b.eq	.Lcbc_dec

	cmp	$rounds,#2
	veor	$dat,$dat,$ivec
	veor	$rndzero_n_last,q8,$rndlast
	b.eq	.Lcbc_enc128

	vld1.32	{$in0-$in1},[$key_]
	add	$key_,$key,#16
	add	$key4,$key,#16*4
	add	$key5,$key,#16*5
	aese	$dat,q8
	aesmc	$dat,$dat
	add	$key6,$key,#16*6
	add	$key7,$key,#16*7
	b	.Lenter_cbc_enc

.align	4
.Loop_cbc_enc:
	aese	$dat,q8
	aesmc	$dat,$dat
	 vst1.8	{$ivec},[$out],#16
.Lenter_cbc_enc:
	aese	$dat,q9
	aesmc	$dat,$dat
	aese	$dat,$in0
	aesmc	$dat,$dat
	vld1.32	{q8},[$key4]
	cmp	$rounds,#4
	aese	$dat,$in1
	aesmc	$dat,$dat
	vld1.32	{q9},[$key5]
	b.eq	.Lcbc_enc192

	aese	$dat,q8
	aesmc	$dat,$dat
	vld1.32	{q8},[$key6]
	aese	$dat,q9
	aesmc	$dat,$dat
	vld1.32	{q9},[$key7]
	nop

.Lcbc_enc192:
	aese	$dat,q8
	aesmc	$dat,$dat
	 subs	$len,$len,#16
	aese	$dat,q9
	aesmc	$dat,$dat
	 cclr	$step,eq
	aese	$dat,q10
	aesmc	$dat,$dat
	aese	$dat,q11
	aesmc	$dat,$dat
	 vld1.8	{q8},[$inp],$step
	aese	$dat,q12
	aesmc	$dat,$dat
	 veor	q8,q8,$rndzero_n_last
	aese	$dat,q13
	aesmc	$dat,$dat
	 vld1.32 {q9},[$key_]		// re-pre-load rndkey[1]
	aese	$dat,q14
	aesmc	$dat,$dat
	aese	$dat,q15
	veor	$ivec,$dat,$rndlast
	b.hs	.Loop_cbc_enc

	vst1.8	{$ivec},[$out],#16
	b	.Lcbc_done

.align	5
.Lcbc_enc128:
	vld1.32	{$in0-$in1},[$key_]
	aese	$dat,q8
	aesmc	$dat,$dat
	b	.Lenter_cbc_enc128
.Loop_cbc_enc128:
	aese	$dat,q8
	aesmc	$dat,$dat
	 vst1.8	{$ivec},[$out],#16
.Lenter_cbc_enc128:
	aese	$dat,q9
	aesmc	$dat,$dat
	 subs	$len,$len,#16
	aese	$dat,$in0
	aesmc	$dat,$dat
	 cclr	$step,eq
	aese	$dat,$in1
	aesmc	$dat,$dat
	aese	$dat,q10
	aesmc	$dat,$dat
	aese	$dat,q11
	aesmc	$dat,$dat
	 vld1.8	{q8},[$inp],$step
	aese	$dat,q12
	aesmc	$dat,$dat
	aese	$dat,q13
	aesmc	$dat,$dat
	aese	$dat,q14
	aesmc	$dat,$dat
	 veor	q8,q8,$rndzero_n_last
	aese	$dat,q15
	veor	$ivec,$dat,$rndlast
	b.hs	.Loop_cbc_enc128

	vst1.8	{$ivec},[$out],#16
	b	.Lcbc_done
___
{
my ($dat2,$in2,$tmp2)=map("q$_",(10,11,9));
$code.=<<___;
.align	5
.Lcbc_dec:
	vld1.8	{$dat2},[$inp],#16
	subs	$len,$len,#32		// bias
	add	$cnt,$rounds,#2
	vorr	$in1,$dat,$dat
	vorr	$dat1,$dat,$dat
	vorr	$in2,$dat2,$dat2
	b.lo	.Lcbc_dec_tail

	vorr	$dat1,$dat2,$dat2
	vld1.8	{$dat2},[$inp],#16
	vorr	$in0,$dat,$dat
	vorr	$in1,$dat1,$dat1
	vorr	$in2,$dat2,$dat2

.Loop3x_cbc_dec:
	aesd	$dat0,q8
	aesimc	$dat0,$dat0
	aesd	$dat1,q8
	aesimc	$dat1,$dat1
	aesd	$dat2,q8
	aesimc	$dat2,$dat2
	vld1.32	{q8},[$key_],#16
	subs	$cnt,$cnt,#2
	aesd	$dat0,q9
	aesimc	$dat0,$dat0
	aesd	$dat1,q9
	aesimc	$dat1,$dat1
	aesd	$dat2,q9
	aesimc	$dat2,$dat2
	vld1.32	{q9},[$key_],#16
	b.gt	.Loop3x_cbc_dec

	aesd	$dat0,q8
	aesimc	$dat0,$dat0
	aesd	$dat1,q8
	aesimc	$dat1,$dat1
	aesd	$dat2,q8
	aesimc	$dat2,$dat2
	 veor	$tmp0,$ivec,$rndlast
	 subs	$len,$len,#0x30
	 veor	$tmp1,$in0,$rndlast
	 mov.lo	x6,$len			// x6, $cnt, is zero at this point
	aesd	$dat0,q9
	aesimc	$dat0,$dat0
	aesd	$dat1,q9
	aesimc	$dat1,$dat1
	aesd	$dat2,q9
	aesimc	$dat2,$dat2
	 veor	$tmp2,$in1,$rndlast
	 add	$inp,$inp,x6		// $inp is adjusted in such way that
					// at exit from the loop $dat1-$dat2
					// are loaded with last "words"
	 vorr	$ivec,$in2,$in2
	 mov	$key_,$key
	aesd	$dat0,q12
	aesimc	$dat0,$dat0
	aesd	$dat1,q12
	aesimc	$dat1,$dat1
	aesd	$dat2,q12
	aesimc	$dat2,$dat2
	 vld1.8	{$in0},[$inp],#16
	aesd	$dat0,q13
	aesimc	$dat0,$dat0
	aesd	$dat1,q13
	aesimc	$dat1,$dat1
	aesd	$dat2,q13
	aesimc	$dat2,$dat2
	 vld1.8	{$in1},[$inp],#16
	aesd	$dat0,q14
	aesimc	$dat0,$dat0
	aesd	$dat1,q14
	aesimc	$dat1,$dat1
	aesd	$dat2,q14
	aesimc	$dat2,$dat2
	 vld1.8	{$in2},[$inp],#16
	aesd	$dat0,q15
	aesd	$dat1,q15
	aesd	$dat2,q15
	 vld1.32 {q8},[$key_],#16	// re-pre-load rndkey[0]
	 add	$cnt,$rounds,#2
	veor	$tmp0,$tmp0,$dat0
	veor	$tmp1,$tmp1,$dat1
	veor	$dat2,$dat2,$tmp2
	 vld1.32 {q9},[$key_],#16	// re-pre-load rndkey[1]
	vst1.8	{$tmp0},[$out],#16
	 vorr	$dat0,$in0,$in0
	vst1.8	{$tmp1},[$out],#16
	 vorr	$dat1,$in1,$in1
	vst1.8	{$dat2},[$out],#16
	 vorr	$dat2,$in2,$in2
	b.hs	.Loop3x_cbc_dec

	cmn	$len,#0x30
	b.eq	.Lcbc_done
	nop

.Lcbc_dec_tail:
	aesd	$dat1,q8
	aesimc	$dat1,$dat1
	aesd	$dat2,q8
	aesimc	$dat2,$dat2
	vld1.32	{q8},[$key_],#16
	subs	$cnt,$cnt,#2
	aesd	$dat1,q9
	aesimc	$dat1,$dat1
	aesd	$dat2,q9
	aesimc	$dat2,$dat2
	vld1.32	{q9},[$key_],#16
	b.gt	.Lcbc_dec_tail

	aesd	$dat1,q8
	aesimc	$dat1,$dat1
	aesd	$dat2,q8
	aesimc	$dat2,$dat2
	aesd	$dat1,q9
	aesimc	$dat1,$dat1
	aesd	$dat2,q9
	aesimc	$dat2,$dat2
	aesd	$dat1,q12
	aesimc	$dat1,$dat1
	aesd	$dat2,q12
	aesimc	$dat2,$dat2
	 cmn	$len,#0x20
	aesd	$dat1,q13
	aesimc	$dat1,$dat1
	aesd	$dat2,q13
	aesimc	$dat2,$dat2
	 veor	$tmp1,$ivec,$rndlast
	aesd	$dat1,q14
	aesimc	$dat1,$dat1
	aesd	$dat2,q14
	aesimc	$dat2,$dat2
	 veor	$tmp2,$in1,$rndlast
	aesd	$dat1,q15
	aesd	$dat2,q15
	b.eq	.Lcbc_dec_one
	veor	$tmp1,$tmp1,$dat1
	veor	$tmp2,$tmp2,$dat2
	 vorr	$ivec,$in2,$in2
	vst1.8	{$tmp1},[$out],#16
	vst1.8	{$tmp2},[$out],#16
	b	.Lcbc_done

.Lcbc_dec_one:
	veor	$tmp1,$tmp1,$dat2
	 vorr	$ivec,$in2,$in2
	vst1.8	{$tmp1},[$out],#16

.Lcbc_done:
	vst1.8	{$ivec},[$ivp]
.Lcbc_abort:
___
}
$code.=<<___	if ($flavour !~ /64/);
	vldmia	sp!,{d8-d15}
	ldmia	sp!,{r4-r8,pc}
___
$code.=<<___	if ($flavour =~ /64/);
	ldr	x29,[sp],#16
	ret
___
$code.=<<___;
.size	${prefix}_cbc_encrypt,.-${prefix}_cbc_encrypt
___
}}}
{{{
my ($inp,$out,$len,$key,$ivp)=map("x$_",(0..4));
my ($rounds,$cnt,$key_)=("w5","w6","x7");
my ($ctr,$tctr0,$tctr1,$tctr2)=map("w$_",(8..10,12));
my $step="x12";		# aliases with $tctr2

my ($dat0,$dat1,$in0,$in1,$tmp0,$tmp1,$ivec,$rndlast)=map("q$_",(0..7));
my ($dat2,$in2,$tmp2)=map("q$_",(10,11,9));

my ($dat,$tmp)=($dat0,$tmp0);

### q8-q15	preloaded key schedule

$code.=<<___;
.globl	${prefix}_ctr32_encrypt_blocks
.type	${prefix}_ctr32_encrypt_blocks,%function
.align	5
${prefix}_ctr32_encrypt_blocks:
___
$code.=<<___	if ($flavour =~ /64/);
	stp		x29,x30,[sp,#-16]!
	add		x29,sp,#0
___
$code.=<<___	if ($flavour !~ /64/);
	mov		ip,sp
	stmdb		sp!,{r4-r10,lr}
	vstmdb		sp!,{d8-d15}            @ ABI specification says so
	ldr		r4, [ip]		@ load remaining arg
___
$code.=<<___;
	ldr		$rounds,[$key,#240]

	ldr		$ctr, [$ivp, #12]
	vld1.32		{$dat0},[$ivp]

	vld1.32		{q8-q9},[$key]		// load key schedule...
	sub		$rounds,$rounds,#4
	mov		$step,#16
	cmp		$len,#2
	add		$key_,$key,x5,lsl#4	// pointer to last 5 round keys
	sub		$rounds,$rounds,#2
	vld1.32		{q12-q13},[$key_],#32
	vld1.32		{q14-q15},[$key_],#32
	vld1.32		{$rndlast},[$key_]
	add		$key_,$key,#32
	mov		$cnt,$rounds
	cclr		$step,lo
#ifndef __ARMEB__
	rev		$ctr, $ctr
#endif
	vorr		$dat1,$dat0,$dat0
	add		$tctr1, $ctr, #1
	vorr		$dat2,$dat0,$dat0
	add		$ctr, $ctr, #2
	vorr		$ivec,$dat0,$dat0
	rev		$tctr1, $tctr1
	vmov.32		${dat1}[3],$tctr1
	b.ls		.Lctr32_tail
	rev		$tctr2, $ctr
	sub		$len,$len,#3		// bias
	vmov.32		${dat2}[3],$tctr2
	b		.Loop3x_ctr32

.align	4
.Loop3x_ctr32:
	aese		$dat0,q8
	aesmc		$dat0,$dat0
	aese		$dat1,q8
	aesmc		$dat1,$dat1
	aese		$dat2,q8
	aesmc		$dat2,$dat2
	vld1.32		{q8},[$key_],#16
	subs		$cnt,$cnt,#2
	aese		$dat0,q9
	aesmc		$dat0,$dat0
	aese		$dat1,q9
	aesmc		$dat1,$dat1
	aese		$dat2,q9
	aesmc		$dat2,$dat2
	vld1.32		{q9},[$key_],#16
	b.gt		.Loop3x_ctr32

	aese		$dat0,q8
	aesmc		$tmp0,$dat0
	aese		$dat1,q8
	aesmc		$tmp1,$dat1
	 vld1.8		{$in0},[$inp],#16
	 vorr		$dat0,$ivec,$ivec
	aese		$dat2,q8
	aesmc		$dat2,$dat2
	 vld1.8		{$in1},[$inp],#16
	 vorr		$dat1,$ivec,$ivec
	aese		$tmp0,q9
	aesmc		$tmp0,$tmp0
	aese		$tmp1,q9
	aesmc		$tmp1,$tmp1
	 vld1.8		{$in2},[$inp],#16
	 mov		$key_,$key
	aese		$dat2,q9
	aesmc		$tmp2,$dat2
	 vorr		$dat2,$ivec,$ivec
	 add		$tctr0,$ctr,#1
	aese		$tmp0,q12
	aesmc		$tmp0,$tmp0
	aese		$tmp1,q12
	aesmc		$tmp1,$tmp1
	 veor		$in0,$in0,$rndlast
	 add		$tctr1,$ctr,#2
	aese		$tmp2,q12
	aesmc		$tmp2,$tmp2
	 veor		$in1,$in1,$rndlast
	 add		$ctr,$ctr,#3
	aese		$tmp0,q13
	aesmc		$tmp0,$tmp0
	aese		$tmp1,q13
	aesmc		$tmp1,$tmp1
	 veor		$in2,$in2,$rndlast
	 rev		$tctr0,$tctr0
	aese		$tmp2,q13
	aesmc		$tmp2,$tmp2
	 vmov.32	${dat0}[3], $tctr0
	 rev		$tctr1,$tctr1
	aese		$tmp0,q14
	aesmc		$tmp0,$tmp0
	aese		$tmp1,q14
	aesmc		$tmp1,$tmp1
	 vmov.32	${dat1}[3], $tctr1
	 rev		$tctr2,$ctr
	aese		$tmp2,q14
	aesmc		$tmp2,$tmp2
	 vmov.32	${dat2}[3], $tctr2
	 subs		$len,$len,#3
	aese		$tmp0,q15
	aese		$tmp1,q15
	aese		$tmp2,q15

	veor		$in0,$in0,$tmp0
	 vld1.32	 {q8},[$key_],#16	// re-pre-load rndkey[0]
	vst1.8		{$in0},[$out],#16
	veor		$in1,$in1,$tmp1
	 mov		$cnt,$rounds
	vst1.8		{$in1},[$out],#16
	veor		$in2,$in2,$tmp2
	 vld1.32	 {q9},[$key_],#16	// re-pre-load rndkey[1]
	vst1.8		{$in2},[$out],#16
	b.hs		.Loop3x_ctr32

	adds		$len,$len,#3
	b.eq		.Lctr32_done
	cmp		$len,#1
	mov		$step,#16
	cclr		$step,eq

.Lctr32_tail:
	aese		$dat0,q8
	aesmc		$dat0,$dat0
	aese		$dat1,q8
	aesmc		$dat1,$dat1
	vld1.32		{q8},[$key_],#16
	subs		$cnt,$cnt,#2
	aese		$dat0,q9
	aesmc		$dat0,$dat0
	aese		$dat1,q9
	aesmc		$dat1,$dat1
	vld1.32		{q9},[$key_],#16
	b.gt		.Lctr32_tail

	aese		$dat0,q8
	aesmc		$dat0,$dat0
	aese		$dat1,q8
	aesmc		$dat1,$dat1
	aese		$dat0,q9
	aesmc		$dat0,$dat0
	aese		$dat1,q9
	aesmc		$dat1,$dat1
	 vld1.8		{$in0},[$inp],$step
	aese		$dat0,q12
	aesmc		$dat0,$dat0
	aese		$dat1,q12
	aesmc		$dat1,$dat1
	 vld1.8		{$in1},[$inp]
	aese		$dat0,q13
	aesmc		$dat0,$dat0
	aese		$dat1,q13
	aesmc		$dat1,$dat1
	 veor		$in0,$in0,$rndlast
	aese		$dat0,q14
	aesmc		$dat0,$dat0
	aese		$dat1,q14
	aesmc		$dat1,$dat1
	 veor		$in1,$in1,$rndlast
	aese		$dat0,q15
	aese		$dat1,q15

	cmp		$len,#1
	veor		$in0,$in0,$dat0
	veor		$in1,$in1,$dat1
	vst1.8		{$in0},[$out],#16
	b.eq		.Lctr32_done
	vst1.8		{$in1},[$out]

.Lctr32_done:
___
$code.=<<___	if ($flavour !~ /64/);
	vldmia		sp!,{d8-d15}
	ldmia		sp!,{r4-r10,pc}
___
$code.=<<___	if ($flavour =~ /64/);
	ldr		x29,[sp],#16
	ret
___
$code.=<<___;
.size	${prefix}_ctr32_encrypt_blocks,.-${prefix}_ctr32_encrypt_blocks
___
}}}
$code.=<<___;
#endif
___
########################################
if ($flavour =~ /64/) {			######## 64-bit code
    my %opcode = (
	"aesd"	=>	0x4e285800,	"aese"	=>	0x4e284800,
	"aesimc"=>	0x4e287800,	"aesmc"	=>	0x4e286800	);

    local *unaes = sub {
	my ($mnemonic,$arg)=@_;

	$arg =~ m/[qv]([0-9]+)[^,]*,\s*[qv]([0-9]+)/o	&&
	sprintf ".inst\t0x%08x\t//%s %s",
			$opcode{$mnemonic}|$1|($2<<5),
			$mnemonic,$arg;
    };

    foreach(split("\n",$code)) {
	s/\`([^\`]*)\`/eval($1)/geo;

	s/\bq([0-9]+)\b/"v".($1<8?$1:$1+8).".16b"/geo;	# old->new registers
	s/@\s/\/\//o;			# old->new style commentary

	#s/[v]?(aes\w+)\s+([qv].*)/unaes($1,$2)/geo	or
	s/cclr\s+([wx])([^,]+),\s*([a-z]+)/csel	$1$2,$1zr,$1$2,$3/o	or
	s/mov\.([a-z]+)\s+([wx][0-9]+),\s*([wx][0-9]+)/csel	$2,$3,$2,$1/o	or
	s/vmov\.i8/movi/o	or	# fix up legacy mnemonics
	s/vext\.8/ext/o		or
	s/vrev32\.8/rev32/o	or
	s/vtst\.8/cmtst/o	or
	s/vshr/ushr/o		or
	s/^(\s+)v/$1/o		or	# strip off v prefix
	s/\bbx\s+lr\b/ret/o;

	# fix up remaining legacy suffixes
	s/\.[ui]?8//o;
	m/\],#8/o and s/\.16b/\.8b/go;
	s/\.[ui]?32//o and s/\.16b/\.4s/go;
	s/\.[ui]?64//o and s/\.16b/\.2d/go;
	s/\.[42]([sd])\[([0-3])\]/\.$1\[$2\]/o;

	print $_,"\n";
    }
} else {				######## 32-bit code
    my %opcode = (
	"aesd"	=>	0xf3b00340,	"aese"	=>	0xf3b00300,
	"aesimc"=>	0xf3b003c0,	"aesmc"	=>	0xf3b00380	);

    local *unaes = sub {
	my ($mnemonic,$arg)=@_;

	if ($arg =~ m/[qv]([0-9]+)[^,]*,\s*[qv]([0-9]+)/o) {
	    my $word = $opcode{$mnemonic}|(($1&7)<<13)|(($1&8)<<19)
					 |(($2&7)<<1) |(($2&8)<<2);
	    # since ARMv7 instructions are always encoded little-endian.
	    # correct solution is to use .inst directive, but older
	    # assemblers don't implement it:-(
	    sprintf ".byte\t0x%02x,0x%02x,0x%02x,0x%02x\t@ %s %s",
			$word&0xff,($word>>8)&0xff,
			($word>>16)&0xff,($word>>24)&0xff,
			$mnemonic,$arg;
	}
    };

    sub unvtbl {
	my $arg=shift;

	$arg =~ m/q([0-9]+),\s*\{q([0-9]+)\},\s*q([0-9]+)/o &&
	sprintf	"vtbl.8	d%d,{q%d},d%d\n\t".
		"vtbl.8	d%d,{q%d},d%d", 2*$1,$2,2*$3, 2*$1+1,$2,2*$3+1;
    }

    sub unvdup32 {
	my $arg=shift;

	$arg =~ m/q([0-9]+),\s*q([0-9]+)\[([0-3])\]/o &&
	sprintf	"vdup.32	q%d,d%d[%d]",$1,2*$2+($3>>1),$3&1;
    }

    sub unvmov32 {
	my $arg=shift;

	$arg =~ m/q([0-9]+)\[([0-3])\],(.*)/o &&
	sprintf	"vmov.32	d%d[%d],%s",2*$1+($2>>1),$2&1,$3;
    }

    foreach(split("\n",$code)) {
	s/\`([^\`]*)\`/eval($1)/geo;

	s/\b[wx]([0-9]+)\b/r$1/go;		# new->old registers
	s/\bv([0-9])\.[12468]+[bsd]\b/q$1/go;	# new->old registers
	s/\/\/\s?/@ /o;				# new->old style commentary

	# fix up remaining new-style suffixes
	s/\{q([0-9]+)\},\s*\[(.+)\],#8/sprintf "{d%d},[$2]!",2*$1/eo	or
	s/\],#[0-9]+/]!/o;

	s/[v]?(aes\w+)\s+([qv].*)/unaes($1,$2)/geo	or
	s/cclr\s+([^,]+),\s*([a-z]+)/mov$2	$1,#0/o	or
	s/vtbl\.8\s+(.*)/unvtbl($1)/geo			or
	s/vdup\.32\s+(.*)/unvdup32($1)/geo		or
	s/vmov\.32\s+(.*)/unvmov32($1)/geo		or
	s/^(\s+)b\./$1b/o				or
	s/^(\s+)mov\./$1mov/o				or
	s/^(\s+)ret/$1bx\tlr/o;

	print $_,"\n";
    }
}

close STDOUT;

#! /usr/bin/env perl
# Copyright 2007-2018 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# January 2007.

# Montgomery multiplication for ARMv4.
#
# Performance improvement naturally varies among CPU implementations
# and compilers. The code was observed to provide +65-35% improvement
# [depending on key length, less for longer keys] on ARM920T, and
# +115-80% on Intel IXP425. This is compared to pre-bn_mul_mont code
# base and compiler generated code with in-lined umull and even umlal
# instructions. The latter means that this code didn't really have an
# "advantage" of utilizing some "secret" instruction.
#
# The code is interoperable with Thumb ISA and is rather compact, less
# than 1/2KB. Windows CE port would be trivial, as it's exclusively
# about decorations, ABI and instruction syntax are identical.

# November 2013
#
# Add NEON code path, which handles lengths divisible by 8. RSA/DSA
# performance improvement on Cortex-A8 is ~45-100% depending on key
# length, more for longer keys. On Cortex-A15 the span is ~10-105%.
# On Snapdragon S4 improvement was measured to vary from ~70% to
# incredible ~380%, yes, 4.8x faster, for RSA4096 sign. But this is
# rather because original integer-only code seems to perform
# suboptimally on S4. Situation on Cortex-A9 is unfortunately
# different. It's being looked into, but the trouble is that
# performance for vectors longer than 256 bits is actually couple
# of percent worse than for integer-only code. The code is chosen
# for execution on all NEON-capable processors, because gain on
# others outweighs the marginal loss on Cortex-A9.

# September 2015
#
# Align Cortex-A9 performance with November 2013 improvements, i.e.
# NEON code is now ~20-105% faster than integer-only one on this
# processor. But this optimization further improved performance even
# on other processors: NEON code path is ~45-180% faster than original
# integer-only on Cortex-A8, ~10-210% on Cortex-A15, ~70-450% on
# Snapdragon S4.

$flavour = shift;
if ($flavour=~/\w[\w\-]*\.\w+$/) { $output=$flavour; undef $flavour; }
else { while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {} }

if ($flavour && $flavour ne "void") {
    $0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
    ( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
    ( $xlate="${dir}../../perlasm/arm-xlate.pl" and -f $xlate) or
    die "can't locate arm-xlate.pl";

    open STDOUT,"| \"$^X\" $xlate $flavour $output";
} else {
    open STDOUT,">$output";
}

$num="r0";	# starts as num argument, but holds &tp[num-1]
$ap="r1";
$bp="r2"; $bi="r2"; $rp="r2";
$np="r3";
$tp="r4";
$aj="r5";
$nj="r6";
$tj="r7";
$n0="r8";
###########	# r9 is reserved by ELF as platform specific, e.g. TLS pointer
$alo="r10";	# sl, gcc uses it to keep @GOT
$ahi="r11";	# fp
$nlo="r12";	# ip
###########	# r13 is stack pointer
$nhi="r14";	# lr
###########	# r15 is program counter

#### argument block layout relative to &tp[num-1], a.k.a. $num
$_rp="$num,#12*4";
# ap permanently resides in r1
$_bp="$num,#13*4";
# np permanently resides in r3
$_n0="$num,#14*4";
$_num="$num,#15*4";	$_bpend=$_num;

$code=<<___;
#include "arm_arch.h"

.text
#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif

#if __ARM_MAX_ARCH__>=7
.align	5
.LOPENSSL_armcap:
.word	OPENSSL_armcap_P-.Lbn_mul_mont
#endif

.global	bn_mul_mont
.type	bn_mul_mont,%function

.align	5
bn_mul_mont:
.Lbn_mul_mont:
	ldr	ip,[sp,#4]		@ load num
	stmdb	sp!,{r0,r2}		@ sp points at argument block
#if __ARM_MAX_ARCH__>=7
	tst	ip,#7
	bne	.Lialu
	adr	r0,.Lbn_mul_mont
	ldr	r2,.LOPENSSL_armcap
	ldr	r0,[r0,r2]
#ifdef	__APPLE__
	ldr	r0,[r0]
#endif
	tst	r0,#ARMV7_NEON		@ NEON available?
	ldmia	sp, {r0,r2}
	beq	.Lialu
	add	sp,sp,#8
	b	bn_mul8x_mont_neon
.align	4
.Lialu:
#endif
	cmp	ip,#2
	mov	$num,ip			@ load num
#ifdef	__thumb2__
	ittt	lt
#endif
	movlt	r0,#0
	addlt	sp,sp,#2*4
	blt	.Labrt

	stmdb	sp!,{r4-r12,lr}		@ save 10 registers

	mov	$num,$num,lsl#2		@ rescale $num for byte count
	sub	sp,sp,$num		@ alloca(4*num)
	sub	sp,sp,#4		@ +extra dword
	sub	$num,$num,#4		@ "num=num-1"
	add	$tp,$bp,$num		@ &bp[num-1]

	add	$num,sp,$num		@ $num to point at &tp[num-1]
	ldr	$n0,[$_n0]		@ &n0
	ldr	$bi,[$bp]		@ bp[0]
	ldr	$aj,[$ap],#4		@ ap[0],ap++
	ldr	$nj,[$np],#4		@ np[0],np++
	ldr	$n0,[$n0]		@ *n0
	str	$tp,[$_bpend]		@ save &bp[num]

	umull	$alo,$ahi,$aj,$bi	@ ap[0]*bp[0]
	str	$n0,[$_n0]		@ save n0 value
	mul	$n0,$alo,$n0		@ "tp[0]"*n0
	mov	$nlo,#0
	umlal	$alo,$nlo,$nj,$n0	@ np[0]*n0+"t[0]"
	mov	$tp,sp

.L1st:
	ldr	$aj,[$ap],#4		@ ap[j],ap++
	mov	$alo,$ahi
	ldr	$nj,[$np],#4		@ np[j],np++
	mov	$ahi,#0
	umlal	$alo,$ahi,$aj,$bi	@ ap[j]*bp[0]
	mov	$nhi,#0
	umlal	$nlo,$nhi,$nj,$n0	@ np[j]*n0
	adds	$nlo,$nlo,$alo
	str	$nlo,[$tp],#4		@ tp[j-1]=,tp++
	adc	$nlo,$nhi,#0
	cmp	$tp,$num
	bne	.L1st

	adds	$nlo,$nlo,$ahi
	ldr	$tp,[$_bp]		@ restore bp
	mov	$nhi,#0
	ldr	$n0,[$_n0]		@ restore n0
	adc	$nhi,$nhi,#0
	str	$nlo,[$num]		@ tp[num-1]=
	mov	$tj,sp
	str	$nhi,[$num,#4]		@ tp[num]=

.Louter:
	sub	$tj,$num,$tj		@ "original" $num-1 value
	sub	$ap,$ap,$tj		@ "rewind" ap to &ap[1]
	ldr	$bi,[$tp,#4]!		@ *(++bp)
	sub	$np,$np,$tj		@ "rewind" np to &np[1]
	ldr	$aj,[$ap,#-4]		@ ap[0]
	ldr	$alo,[sp]		@ tp[0]
	ldr	$nj,[$np,#-4]		@ np[0]
	ldr	$tj,[sp,#4]		@ tp[1]

	mov	$ahi,#0
	umlal	$alo,$ahi,$aj,$bi	@ ap[0]*bp[i]+tp[0]
	str	$tp,[$_bp]		@ save bp
	mul	$n0,$alo,$n0
	mov	$nlo,#0
	umlal	$alo,$nlo,$nj,$n0	@ np[0]*n0+"tp[0]"
	mov	$tp,sp

.Linner:
	ldr	$aj,[$ap],#4		@ ap[j],ap++
	adds	$alo,$ahi,$tj		@ +=tp[j]
	ldr	$nj,[$np],#4		@ np[j],np++
	mov	$ahi,#0
	umlal	$alo,$ahi,$aj,$bi	@ ap[j]*bp[i]
	mov	$nhi,#0
	umlal	$nlo,$nhi,$nj,$n0	@ np[j]*n0
	adc	$ahi,$ahi,#0
	ldr	$tj,[$tp,#8]		@ tp[j+1]
	adds	$nlo,$nlo,$alo
	str	$nlo,[$tp],#4		@ tp[j-1]=,tp++
	adc	$nlo,$nhi,#0
	cmp	$tp,$num
	bne	.Linner

	adds	$nlo,$nlo,$ahi
	mov	$nhi,#0
	ldr	$tp,[$_bp]		@ restore bp
	adc	$nhi,$nhi,#0
	ldr	$n0,[$_n0]		@ restore n0
	adds	$nlo,$nlo,$tj
	ldr	$tj,[$_bpend]		@ restore &bp[num]
	adc	$nhi,$nhi,#0
	str	$nlo,[$num]		@ tp[num-1]=
	str	$nhi,[$num,#4]		@ tp[num]=

	cmp	$tp,$tj
#ifdef	__thumb2__
	itt	ne
#endif
	movne	$tj,sp
	bne	.Louter

	ldr	$rp,[$_rp]		@ pull rp
	mov	$aj,sp
	add	$num,$num,#4		@ $num to point at &tp[num]
	sub	$aj,$num,$aj		@ "original" num value
	mov	$tp,sp			@ "rewind" $tp
	mov	$ap,$tp			@ "borrow" $ap
	sub	$np,$np,$aj		@ "rewind" $np to &np[0]

	subs	$tj,$tj,$tj		@ "clear" carry flag
.Lsub:	ldr	$tj,[$tp],#4
	ldr	$nj,[$np],#4
	sbcs	$tj,$tj,$nj		@ tp[j]-np[j]
	str	$tj,[$rp],#4		@ rp[j]=
	teq	$tp,$num		@ preserve carry
	bne	.Lsub
	sbcs	$nhi,$nhi,#0		@ upmost carry
	mov	$tp,sp			@ "rewind" $tp
	sub	$rp,$rp,$aj		@ "rewind" $rp

.Lcopy:	ldr	$tj,[$tp]		@ conditional copy
	ldr	$aj,[$rp]
	str	sp,[$tp],#4		@ zap tp
#ifdef	__thumb2__
	it	cc
#endif
	movcc	$aj,$tj
	str	$aj,[$rp],#4
	teq	$tp,$num		@ preserve carry
	bne	.Lcopy

	mov	sp,$num
	add	sp,sp,#4		@ skip over tp[num+1]
	ldmia	sp!,{r4-r12,lr}		@ restore registers
	add	sp,sp,#2*4		@ skip over {r0,r2}
	mov	r0,#1
.Labrt:
#if __ARM_ARCH__>=5
	ret				@ bx lr
#else
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	bn_mul_mont,.-bn_mul_mont
___
{
my ($A0,$A1,$A2,$A3)=map("d$_",(0..3));
my ($N0,$N1,$N2,$N3)=map("d$_",(4..7));
my ($Z,$Temp)=("q4","q5");
my @ACC=map("q$_",(6..13));
my ($Bi,$Ni,$M0)=map("d$_",(28..31));
my $zero="$Z#lo";
my $temp="$Temp#lo";

my ($rptr,$aptr,$bptr,$nptr,$n0,$num)=map("r$_",(0..5));
my ($tinptr,$toutptr,$inner,$outer,$bnptr)=map("r$_",(6..11));

$code.=<<___;
#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.type	bn_mul8x_mont_neon,%function
.align	5
bn_mul8x_mont_neon:
	mov	ip,sp
	stmdb	sp!,{r4-r11}
	vstmdb	sp!,{d8-d15}		@ ABI specification says so
	ldmia	ip,{r4-r5}		@ load rest of parameter block
	mov	ip,sp

	cmp	$num,#8
	bhi	.LNEON_8n

	@ special case for $num==8, everything is in register bank...

	vld1.32		{${Bi}[0]}, [$bptr,:32]!
	veor		$zero,$zero,$zero
	sub		$toutptr,sp,$num,lsl#4
	vld1.32		{$A0-$A3},  [$aptr]!		@ can't specify :32 :-(
	and		$toutptr,$toutptr,#-64
	vld1.32		{${M0}[0]}, [$n0,:32]
	mov		sp,$toutptr			@ alloca
	vzip.16		$Bi,$zero

	vmull.u32	@ACC[0],$Bi,${A0}[0]
	vmull.u32	@ACC[1],$Bi,${A0}[1]
	vmull.u32	@ACC[2],$Bi,${A1}[0]
	vshl.i64	$Ni,@ACC[0]#hi,#16
	vmull.u32	@ACC[3],$Bi,${A1}[1]

	vadd.u64	$Ni,$Ni,@ACC[0]#lo
	veor		$zero,$zero,$zero
	vmul.u32	$Ni,$Ni,$M0

	vmull.u32	@ACC[4],$Bi,${A2}[0]
	 vld1.32	{$N0-$N3}, [$nptr]!
	vmull.u32	@ACC[5],$Bi,${A2}[1]
	vmull.u32	@ACC[6],$Bi,${A3}[0]
	vzip.16		$Ni,$zero
	vmull.u32	@ACC[7],$Bi,${A3}[1]

	vmlal.u32	@ACC[0],$Ni,${N0}[0]
	sub		$outer,$num,#1
	vmlal.u32	@ACC[1],$Ni,${N0}[1]
	vmlal.u32	@ACC[2],$Ni,${N1}[0]
	vmlal.u32	@ACC[3],$Ni,${N1}[1]

	vmlal.u32	@ACC[4],$Ni,${N2}[0]
	vmov		$Temp,@ACC[0]
	vmlal.u32	@ACC[5],$Ni,${N2}[1]
	vmov		@ACC[0],@ACC[1]
	vmlal.u32	@ACC[6],$Ni,${N3}[0]
	vmov		@ACC[1],@ACC[2]
	vmlal.u32	@ACC[7],$Ni,${N3}[1]
	vmov		@ACC[2],@ACC[3]
	vmov		@ACC[3],@ACC[4]
	vshr.u64	$temp,$temp,#16
	vmov		@ACC[4],@ACC[5]
	vmov		@ACC[5],@ACC[6]
	vadd.u64	$temp,$temp,$Temp#hi
	vmov		@ACC[6],@ACC[7]
	veor		@ACC[7],@ACC[7]
	vshr.u64	$temp,$temp,#16

	b	.LNEON_outer8

.align	4
.LNEON_outer8:
	vld1.32		{${Bi}[0]}, [$bptr,:32]!
	veor		$zero,$zero,$zero
	vzip.16		$Bi,$zero
	vadd.u64	@ACC[0]#lo,@ACC[0]#lo,$temp

	vmlal.u32	@ACC[0],$Bi,${A0}[0]
	vmlal.u32	@ACC[1],$Bi,${A0}[1]
	vmlal.u32	@ACC[2],$Bi,${A1}[0]
	vshl.i64	$Ni,@ACC[0]#hi,#16
	vmlal.u32	@ACC[3],$Bi,${A1}[1]

	vadd.u64	$Ni,$Ni,@ACC[0]#lo
	veor		$zero,$zero,$zero
	subs		$outer,$outer,#1
	vmul.u32	$Ni,$Ni,$M0

	vmlal.u32	@ACC[4],$Bi,${A2}[0]
	vmlal.u32	@ACC[5],$Bi,${A2}[1]
	vmlal.u32	@ACC[6],$Bi,${A3}[0]
	vzip.16		$Ni,$zero
	vmlal.u32	@ACC[7],$Bi,${A3}[1]

	vmlal.u32	@ACC[0],$Ni,${N0}[0]
	vmlal.u32	@ACC[1],$Ni,${N0}[1]
	vmlal.u32	@ACC[2],$Ni,${N1}[0]
	vmlal.u32	@ACC[3],$Ni,${N1}[1]

	vmlal.u32	@ACC[4],$Ni,${N2}[0]
	vmov		$Temp,@ACC[0]
	vmlal.u32	@ACC[5],$Ni,${N2}[1]
	vmov		@ACC[0],@ACC[1]
	vmlal.u32	@ACC[6],$Ni,${N3}[0]
	vmov		@ACC[1],@ACC[2]
	vmlal.u32	@ACC[7],$Ni,${N3}[1]
	vmov		@ACC[2],@ACC[3]
	vmov		@ACC[3],@ACC[4]
	vshr.u64	$temp,$temp,#16
	vmov		@ACC[4],@ACC[5]
	vmov		@ACC[5],@ACC[6]
	vadd.u64	$temp,$temp,$Temp#hi
	vmov		@ACC[6],@ACC[7]
	veor		@ACC[7],@ACC[7]
	vshr.u64	$temp,$temp,#16

	bne	.LNEON_outer8

	vadd.u64	@ACC[0]#lo,@ACC[0]#lo,$temp
	mov		$toutptr,sp
	vshr.u64	$temp,@ACC[0]#lo,#16
	mov		$inner,$num
	vadd.u64	@ACC[0]#hi,@ACC[0]#hi,$temp
	add		$tinptr,sp,#96
	vshr.u64	$temp,@ACC[0]#hi,#16
	vzip.16		@ACC[0]#lo,@ACC[0]#hi

	b	.LNEON_tail_entry

.align	4
.LNEON_8n:
	veor		@ACC[0],@ACC[0],@ACC[0]
	 sub		$toutptr,sp,#128
	veor		@ACC[1],@ACC[1],@ACC[1]
	 sub		$toutptr,$toutptr,$num,lsl#4
	veor		@ACC[2],@ACC[2],@ACC[2]
	 and		$toutptr,$toutptr,#-64
	veor		@ACC[3],@ACC[3],@ACC[3]
	 mov		sp,$toutptr			@ alloca
	veor		@ACC[4],@ACC[4],@ACC[4]
	 add		$toutptr,$toutptr,#256
	veor		@ACC[5],@ACC[5],@ACC[5]
	 sub		$inner,$num,#8
	veor		@ACC[6],@ACC[6],@ACC[6]
	veor		@ACC[7],@ACC[7],@ACC[7]

.LNEON_8n_init:
	vst1.64		{@ACC[0]-@ACC[1]},[$toutptr,:256]!
	subs		$inner,$inner,#8
	vst1.64		{@ACC[2]-@ACC[3]},[$toutptr,:256]!
	vst1.64		{@ACC[4]-@ACC[5]},[$toutptr,:256]!
	vst1.64		{@ACC[6]-@ACC[7]},[$toutptr,:256]!
	bne		.LNEON_8n_init

	add		$tinptr,sp,#256
	vld1.32		{$A0-$A3},[$aptr]!
	add		$bnptr,sp,#8
	vld1.32		{${M0}[0]},[$n0,:32]
	mov		$outer,$num
	b		.LNEON_8n_outer

.align	4
.LNEON_8n_outer:
	vld1.32		{${Bi}[0]},[$bptr,:32]!	@ *b++
	veor		$zero,$zero,$zero
	vzip.16		$Bi,$zero
	add		$toutptr,sp,#128
	vld1.32		{$N0-$N3},[$nptr]!

	vmlal.u32	@ACC[0],$Bi,${A0}[0]
	vmlal.u32	@ACC[1],$Bi,${A0}[1]
	 veor		$zero,$zero,$zero
	vmlal.u32	@ACC[2],$Bi,${A1}[0]
	 vshl.i64	$Ni,@ACC[0]#hi,#16
	vmlal.u32	@ACC[3],$Bi,${A1}[1]
	 vadd.u64	$Ni,$Ni,@ACC[0]#lo
	vmlal.u32	@ACC[4],$Bi,${A2}[0]
	 vmul.u32	$Ni,$Ni,$M0
	vmlal.u32	@ACC[5],$Bi,${A2}[1]
	vst1.32		{$Bi},[sp,:64]		@ put aside smashed b[8*i+0]
	vmlal.u32	@ACC[6],$Bi,${A3}[0]
	 vzip.16	$Ni,$zero
	vmlal.u32	@ACC[7],$Bi,${A3}[1]
___
for ($i=0; $i<7;) {
$code.=<<___;
	vld1.32		{${Bi}[0]},[$bptr,:32]!	@ *b++
	vmlal.u32	@ACC[0],$Ni,${N0}[0]
	veor		$temp,$temp,$temp
	vmlal.u32	@ACC[1],$Ni,${N0}[1]
	vzip.16		$Bi,$temp
	vmlal.u32	@ACC[2],$Ni,${N1}[0]
	 vshr.u64	@ACC[0]#lo,@ACC[0]#lo,#16
	vmlal.u32	@ACC[3],$Ni,${N1}[1]
	vmlal.u32	@ACC[4],$Ni,${N2}[0]
	 vadd.u64	@ACC[0]#lo,@ACC[0]#lo,@ACC[0]#hi
	vmlal.u32	@ACC[5],$Ni,${N2}[1]
	 vshr.u64	@ACC[0]#lo,@ACC[0]#lo,#16
	vmlal.u32	@ACC[6],$Ni,${N3}[0]
	vmlal.u32	@ACC[7],$Ni,${N3}[1]
	 vadd.u64	@ACC[1]#lo,@ACC[1]#lo,@ACC[0]#lo
	vst1.32		{$Ni},[$bnptr,:64]!	@ put aside smashed m[8*i+$i]
___
	push(@ACC,shift(@ACC));	$i++;
$code.=<<___;
	vmlal.u32	@ACC[0],$Bi,${A0}[0]
	vld1.64		{@ACC[7]},[$tinptr,:128]!
	vmlal.u32	@ACC[1],$Bi,${A0}[1]
	 veor		$zero,$zero,$zero
	vmlal.u32	@ACC[2],$Bi,${A1}[0]
	 vshl.i64	$Ni,@ACC[0]#hi,#16
	vmlal.u32	@ACC[3],$Bi,${A1}[1]
	 vadd.u64	$Ni,$Ni,@ACC[0]#lo
	vmlal.u32	@ACC[4],$Bi,${A2}[0]
	 vmul.u32	$Ni,$Ni,$M0
	vmlal.u32	@ACC[5],$Bi,${A2}[1]
	vst1.32		{$Bi},[$bnptr,:64]!	@ put aside smashed b[8*i+$i]
	vmlal.u32	@ACC[6],$Bi,${A3}[0]
	 vzip.16	$Ni,$zero
	vmlal.u32	@ACC[7],$Bi,${A3}[1]
___
}
$code.=<<___;
	vld1.32		{$Bi},[sp,:64]		@ pull smashed b[8*i+0]
	vmlal.u32	@ACC[0],$Ni,${N0}[0]
	vld1.32		{$A0-$A3},[$aptr]!
	vmlal.u32	@ACC[1],$Ni,${N0}[1]
	vmlal.u32	@ACC[2],$Ni,${N1}[0]
	 vshr.u64	@ACC[0]#lo,@ACC[0]#lo,#16
	vmlal.u32	@ACC[3],$Ni,${N1}[1]
	vmlal.u32	@ACC[4],$Ni,${N2}[0]
	 vadd.u64	@ACC[0]#lo,@ACC[0]#lo,@ACC[0]#hi
	vmlal.u32	@ACC[5],$Ni,${N2}[1]
	 vshr.u64	@ACC[0]#lo,@ACC[0]#lo,#16
	vmlal.u32	@ACC[6],$Ni,${N3}[0]
	vmlal.u32	@ACC[7],$Ni,${N3}[1]
	 vadd.u64	@ACC[1]#lo,@ACC[1]#lo,@ACC[0]#lo
	vst1.32		{$Ni},[$bnptr,:64]	@ put aside smashed m[8*i+$i]
	add		$bnptr,sp,#8		@ rewind
___
	push(@ACC,shift(@ACC));
$code.=<<___;
	sub		$inner,$num,#8
	b		.LNEON_8n_inner

.align	4
.LNEON_8n_inner:
	subs		$inner,$inner,#8
	vmlal.u32	@ACC[0],$Bi,${A0}[0]
	vld1.64		{@ACC[7]},[$tinptr,:128]
	vmlal.u32	@ACC[1],$Bi,${A0}[1]
	vld1.32		{$Ni},[$bnptr,:64]!	@ pull smashed m[8*i+0]
	vmlal.u32	@ACC[2],$Bi,${A1}[0]
	vld1.32		{$N0-$N3},[$nptr]!
	vmlal.u32	@ACC[3],$Bi,${A1}[1]
	it		ne
	addne		$tinptr,$tinptr,#16	@ don't advance in last iteration
	vmlal.u32	@ACC[4],$Bi,${A2}[0]
	vmlal.u32	@ACC[5],$Bi,${A2}[1]
	vmlal.u32	@ACC[6],$Bi,${A3}[0]
	vmlal.u32	@ACC[7],$Bi,${A3}[1]
___
for ($i=1; $i<8; $i++) {
$code.=<<___;
	vld1.32		{$Bi},[$bnptr,:64]!	@ pull smashed b[8*i+$i]
	vmlal.u32	@ACC[0],$Ni,${N0}[0]
	vmlal.u32	@ACC[1],$Ni,${N0}[1]
	vmlal.u32	@ACC[2],$Ni,${N1}[0]
	vmlal.u32	@ACC[3],$Ni,${N1}[1]
	vmlal.u32	@ACC[4],$Ni,${N2}[0]
	vmlal.u32	@ACC[5],$Ni,${N2}[1]
	vmlal.u32	@ACC[6],$Ni,${N3}[0]
	vmlal.u32	@ACC[7],$Ni,${N3}[1]
	vst1.64		{@ACC[0]},[$toutptr,:128]!
___
	push(@ACC,shift(@ACC));
$code.=<<___;
	vmlal.u32	@ACC[0],$Bi,${A0}[0]
	vld1.64		{@ACC[7]},[$tinptr,:128]
	vmlal.u32	@ACC[1],$Bi,${A0}[1]
	vld1.32		{$Ni},[$bnptr,:64]!	@ pull smashed m[8*i+$i]
	vmlal.u32	@ACC[2],$Bi,${A1}[0]
	it		ne
	addne		$tinptr,$tinptr,#16	@ don't advance in last iteration
	vmlal.u32	@ACC[3],$Bi,${A1}[1]
	vmlal.u32	@ACC[4],$Bi,${A2}[0]
	vmlal.u32	@ACC[5],$Bi,${A2}[1]
	vmlal.u32	@ACC[6],$Bi,${A3}[0]
	vmlal.u32	@ACC[7],$Bi,${A3}[1]
___
}
$code.=<<___;
	it		eq
	subeq		$aptr,$aptr,$num,lsl#2	@ rewind
	vmlal.u32	@ACC[0],$Ni,${N0}[0]
	vld1.32		{$Bi},[sp,:64]		@ pull smashed b[8*i+0]
	vmlal.u32	@ACC[1],$Ni,${N0}[1]
	vld1.32		{$A0-$A3},[$aptr]!
	vmlal.u32	@ACC[2],$Ni,${N1}[0]
	add		$bnptr,sp,#8		@ rewind
	vmlal.u32	@ACC[3],$Ni,${N1}[1]
	vmlal.u32	@ACC[4],$Ni,${N2}[0]
	vmlal.u32	@ACC[5],$Ni,${N2}[1]
	vmlal.u32	@ACC[6],$Ni,${N3}[0]
	vst1.64		{@ACC[0]},[$toutptr,:128]!
	vmlal.u32	@ACC[7],$Ni,${N3}[1]

	bne		.LNEON_8n_inner
___
	push(@ACC,shift(@ACC));
$code.=<<___;
	add		$tinptr,sp,#128
	vst1.64		{@ACC[0]-@ACC[1]},[$toutptr,:256]!
	veor		q2,q2,q2		@ $N0-$N1
	vst1.64		{@ACC[2]-@ACC[3]},[$toutptr,:256]!
	veor		q3,q3,q3		@ $N2-$N3
	vst1.64		{@ACC[4]-@ACC[5]},[$toutptr,:256]!
	vst1.64		{@ACC[6]},[$toutptr,:128]

	subs		$outer,$outer,#8
	vld1.64		{@ACC[0]-@ACC[1]},[$tinptr,:256]!
	vld1.64		{@ACC[2]-@ACC[3]},[$tinptr,:256]!
	vld1.64		{@ACC[4]-@ACC[5]},[$tinptr,:256]!
	vld1.64		{@ACC[6]-@ACC[7]},[$tinptr,:256]!

	itt		ne
	subne		$nptr,$nptr,$num,lsl#2	@ rewind
	bne		.LNEON_8n_outer

	add		$toutptr,sp,#128
	vst1.64		{q2-q3}, [sp,:256]!	@ start wiping stack frame
	vshr.u64	$temp,@ACC[0]#lo,#16
	vst1.64		{q2-q3},[sp,:256]!
	vadd.u64	@ACC[0]#hi,@ACC[0]#hi,$temp
	vst1.64		{q2-q3}, [sp,:256]!
	vshr.u64	$temp,@ACC[0]#hi,#16
	vst1.64		{q2-q3}, [sp,:256]!
	vzip.16		@ACC[0]#lo,@ACC[0]#hi

	mov		$inner,$num
	b		.LNEON_tail_entry

.align	4
.LNEON_tail:
	vadd.u64	@ACC[0]#lo,@ACC[0]#lo,$temp
	vshr.u64	$temp,@ACC[0]#lo,#16
	vld1.64		{@ACC[2]-@ACC[3]}, [$tinptr, :256]!
	vadd.u64	@ACC[0]#hi,@ACC[0]#hi,$temp
	vld1.64		{@ACC[4]-@ACC[5]}, [$tinptr, :256]!
	vshr.u64	$temp,@ACC[0]#hi,#16
	vld1.64		{@ACC[6]-@ACC[7]}, [$tinptr, :256]!
	vzip.16		@ACC[0]#lo,@ACC[0]#hi

.LNEON_tail_entry:
___
for ($i=1; $i<8; $i++) {
$code.=<<___;
	vadd.u64	@ACC[1]#lo,@ACC[1]#lo,$temp
	vst1.32		{@ACC[0]#lo[0]}, [$toutptr, :32]!
	vshr.u64	$temp,@ACC[1]#lo,#16
	vadd.u64	@ACC[1]#hi,@ACC[1]#hi,$temp
	vshr.u64	$temp,@ACC[1]#hi,#16
	vzip.16		@ACC[1]#lo,@ACC[1]#hi
___
	push(@ACC,shift(@ACC));
}
	push(@ACC,shift(@ACC));
$code.=<<___;
	vld1.64		{@ACC[0]-@ACC[1]}, [$tinptr, :256]!
	subs		$inner,$inner,#8
	vst1.32		{@ACC[7]#lo[0]},   [$toutptr, :32]!
	bne	.LNEON_tail

	vst1.32	{${temp}[0]}, [$toutptr, :32]		@ top-most bit
	sub	$nptr,$nptr,$num,lsl#2			@ rewind $nptr
	subs	$aptr,sp,#0				@ clear carry flag
	add	$bptr,sp,$num,lsl#2

.LNEON_sub:
	ldmia	$aptr!, {r4-r7}
	ldmia	$nptr!, {r8-r11}
	sbcs	r8, r4,r8
	sbcs	r9, r5,r9
	sbcs	r10,r6,r10
	sbcs	r11,r7,r11
	teq	$aptr,$bptr				@ preserves carry
	stmia	$rptr!, {r8-r11}
	bne	.LNEON_sub

	ldr	r10, [$aptr]				@ load top-most bit
	mov	r11,sp
	veor	q0,q0,q0
	sub	r11,$bptr,r11				@ this is num*4
	veor	q1,q1,q1
	mov	$aptr,sp
	sub	$rptr,$rptr,r11				@ rewind $rptr
	mov	$nptr,$bptr				@ second 3/4th of frame
	sbcs	r10,r10,#0				@ result is carry flag

.LNEON_copy_n_zap:
	ldmia	$aptr!, {r4-r7}
	ldmia	$rptr,  {r8-r11}
	it	cc
	movcc	r8, r4
	vst1.64	{q0-q1}, [$nptr,:256]!			@ wipe
	itt	cc
	movcc	r9, r5
	movcc	r10,r6
	vst1.64	{q0-q1}, [$nptr,:256]!			@ wipe
	it	cc
	movcc	r11,r7
	ldmia	$aptr, {r4-r7}
	stmia	$rptr!, {r8-r11}
	sub	$aptr,$aptr,#16
	ldmia	$rptr, {r8-r11}
	it	cc
	movcc	r8, r4
	vst1.64	{q0-q1}, [$aptr,:256]!			@ wipe
	itt	cc
	movcc	r9, r5
	movcc	r10,r6
	vst1.64	{q0-q1}, [$nptr,:256]!			@ wipe
	it	cc
	movcc	r11,r7
	teq	$aptr,$bptr				@ preserves carry
	stmia	$rptr!, {r8-r11}
	bne	.LNEON_copy_n_zap

	mov	sp,ip
        vldmia  sp!,{d8-d15}
        ldmia   sp!,{r4-r11}
	ret						@ bx lr
.size	bn_mul8x_mont_neon,.-bn_mul8x_mont_neon
#endif
___
}
$code.=<<___;
.asciz	"Montgomery multiplication for ARMv4/NEON, CRYPTOGAMS by <appro\@openssl.org>"
.align	2
#if __ARM_MAX_ARCH__>=7
.comm	OPENSSL_armcap_P,4,4
#endif
___

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	s/\bq([0-9]+)#(lo|hi)/sprintf "d%d",2*$1+($2 eq "hi")/ge	or
	s/\bret\b/bx    lr/g						or
	s/\bbx\s+lr\b/.word\t0xe12fff1e/g;	# make it possible to compile with -march=armv4

	print $_,"\n";
}

close STDOUT;

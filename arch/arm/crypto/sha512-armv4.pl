#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0

# This code is taken from the OpenSSL project but the author (Andy Polyakov)
# has relicensed it under the GPLv2. Therefore this program is free software;
# you can redistribute it and/or modify it under the terms of the GNU General
# Public License version 2 as published by the Free Software Foundation.
#
# The original headers, including the original license headers, are
# included below for completeness.

# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# SHA512 block procedure for ARMv4. September 2007.

# This code is ~4.5 (four and a half) times faster than code generated
# by gcc 3.4 and it spends ~72 clock cycles per byte [on single-issue
# Xscale PXA250 core].
#
# July 2010.
#
# Rescheduling for dual-issue pipeline resulted in 6% improvement on
# Cortex A8 core and ~40 cycles per processed byte.

# February 2011.
#
# Profiler-assisted and platform-specific optimization resulted in 7%
# improvement on Coxtex A8 core and ~38 cycles per byte.

# March 2011.
#
# Add NEON implementation. On Cortex A8 it was measured to process
# one byte in 23.3 cycles or ~60% faster than integer-only code.

# August 2012.
#
# Improve NEON performance by 12% on Snapdragon S4. In absolute
# terms it's 22.6 cycles per byte, which is disappointing result.
# Technical writers asserted that 3-way S4 pipeline can sustain
# multiple NEON instructions per cycle, but dual NEON issue could
# not be observed, see http://www.openssl.org/~appro/Snapdragon-S4.html
# for further details. On side note Cortex-A15 processes one byte in
# 16 cycles.

# Byte order [in]dependence. =========================================
#
# Originally caller was expected to maintain specific *dword* order in
# h[0-7], namely with most significant dword at *lower* address, which
# was reflected in below two parameters as 0 and 4. Now caller is
# expected to maintain native byte order for whole 64-bit values.
$hi="HI";
$lo="LO";
# ====================================================================

while (($output=shift) && ($output!~/^\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

$ctx="r0";	# parameter block
$inp="r1";
$len="r2";

$Tlo="r3";
$Thi="r4";
$Alo="r5";
$Ahi="r6";
$Elo="r7";
$Ehi="r8";
$t0="r9";
$t1="r10";
$t2="r11";
$t3="r12";
############	r13 is stack pointer
$Ktbl="r14";
############	r15 is program counter

$Aoff=8*0;
$Boff=8*1;
$Coff=8*2;
$Doff=8*3;
$Eoff=8*4;
$Foff=8*5;
$Goff=8*6;
$Hoff=8*7;
$Xoff=8*8;

sub BODY_00_15() {
my $magic = shift;
$code.=<<___;
	@ Sigma1(x)	(ROTR((x),14) ^ ROTR((x),18)  ^ ROTR((x),41))
	@ LO		lo>>14^hi<<18 ^ lo>>18^hi<<14 ^ hi>>9^lo<<23
	@ HI		hi>>14^lo<<18 ^ hi>>18^lo<<14 ^ lo>>9^hi<<23
	mov	$t0,$Elo,lsr#14
	str	$Tlo,[sp,#$Xoff+0]
	mov	$t1,$Ehi,lsr#14
	str	$Thi,[sp,#$Xoff+4]
	eor	$t0,$t0,$Ehi,lsl#18
	ldr	$t2,[sp,#$Hoff+0]	@ h.lo
	eor	$t1,$t1,$Elo,lsl#18
	ldr	$t3,[sp,#$Hoff+4]	@ h.hi
	eor	$t0,$t0,$Elo,lsr#18
	eor	$t1,$t1,$Ehi,lsr#18
	eor	$t0,$t0,$Ehi,lsl#14
	eor	$t1,$t1,$Elo,lsl#14
	eor	$t0,$t0,$Ehi,lsr#9
	eor	$t1,$t1,$Elo,lsr#9
	eor	$t0,$t0,$Elo,lsl#23
	eor	$t1,$t1,$Ehi,lsl#23	@ Sigma1(e)
	adds	$Tlo,$Tlo,$t0
	ldr	$t0,[sp,#$Foff+0]	@ f.lo
	adc	$Thi,$Thi,$t1		@ T += Sigma1(e)
	ldr	$t1,[sp,#$Foff+4]	@ f.hi
	adds	$Tlo,$Tlo,$t2
	ldr	$t2,[sp,#$Goff+0]	@ g.lo
	adc	$Thi,$Thi,$t3		@ T += h
	ldr	$t3,[sp,#$Goff+4]	@ g.hi

	eor	$t0,$t0,$t2
	str	$Elo,[sp,#$Eoff+0]
	eor	$t1,$t1,$t3
	str	$Ehi,[sp,#$Eoff+4]
	and	$t0,$t0,$Elo
	str	$Alo,[sp,#$Aoff+0]
	and	$t1,$t1,$Ehi
	str	$Ahi,[sp,#$Aoff+4]
	eor	$t0,$t0,$t2
	ldr	$t2,[$Ktbl,#$lo]	@ K[i].lo
	eor	$t1,$t1,$t3		@ Ch(e,f,g)
	ldr	$t3,[$Ktbl,#$hi]	@ K[i].hi

	adds	$Tlo,$Tlo,$t0
	ldr	$Elo,[sp,#$Doff+0]	@ d.lo
	adc	$Thi,$Thi,$t1		@ T += Ch(e,f,g)
	ldr	$Ehi,[sp,#$Doff+4]	@ d.hi
	adds	$Tlo,$Tlo,$t2
	and	$t0,$t2,#0xff
	adc	$Thi,$Thi,$t3		@ T += K[i]
	adds	$Elo,$Elo,$Tlo
	ldr	$t2,[sp,#$Boff+0]	@ b.lo
	adc	$Ehi,$Ehi,$Thi		@ d += T
	teq	$t0,#$magic

	ldr	$t3,[sp,#$Coff+0]	@ c.lo
#if __ARM_ARCH__>=7
	it	eq			@ Thumb2 thing, sanity check in ARM
#endif
	orreq	$Ktbl,$Ktbl,#1
	@ Sigma0(x)	(ROTR((x),28) ^ ROTR((x),34) ^ ROTR((x),39))
	@ LO		lo>>28^hi<<4  ^ hi>>2^lo<<30 ^ hi>>7^lo<<25
	@ HI		hi>>28^lo<<4  ^ lo>>2^hi<<30 ^ lo>>7^hi<<25
	mov	$t0,$Alo,lsr#28
	mov	$t1,$Ahi,lsr#28
	eor	$t0,$t0,$Ahi,lsl#4
	eor	$t1,$t1,$Alo,lsl#4
	eor	$t0,$t0,$Ahi,lsr#2
	eor	$t1,$t1,$Alo,lsr#2
	eor	$t0,$t0,$Alo,lsl#30
	eor	$t1,$t1,$Ahi,lsl#30
	eor	$t0,$t0,$Ahi,lsr#7
	eor	$t1,$t1,$Alo,lsr#7
	eor	$t0,$t0,$Alo,lsl#25
	eor	$t1,$t1,$Ahi,lsl#25	@ Sigma0(a)
	adds	$Tlo,$Tlo,$t0
	and	$t0,$Alo,$t2
	adc	$Thi,$Thi,$t1		@ T += Sigma0(a)

	ldr	$t1,[sp,#$Boff+4]	@ b.hi
	orr	$Alo,$Alo,$t2
	ldr	$t2,[sp,#$Coff+4]	@ c.hi
	and	$Alo,$Alo,$t3
	and	$t3,$Ahi,$t1
	orr	$Ahi,$Ahi,$t1
	orr	$Alo,$Alo,$t0		@ Maj(a,b,c).lo
	and	$Ahi,$Ahi,$t2
	adds	$Alo,$Alo,$Tlo
	orr	$Ahi,$Ahi,$t3		@ Maj(a,b,c).hi
	sub	sp,sp,#8
	adc	$Ahi,$Ahi,$Thi		@ h += T
	tst	$Ktbl,#1
	add	$Ktbl,$Ktbl,#8
___
}
$code=<<___;
#ifndef __KERNEL__
# include "arm_arch.h"
# define VFP_ABI_PUSH	vstmdb	sp!,{d8-d15}
# define VFP_ABI_POP	vldmia	sp!,{d8-d15}
#else
# define __ARM_ARCH__ __LINUX_ARM_ARCH__
# define __ARM_MAX_ARCH__ 7
# define VFP_ABI_PUSH
# define VFP_ABI_POP
#endif

#ifdef __ARMEL__
# define LO 0
# define HI 4
# define WORD64(hi0,lo0,hi1,lo1)	.word	lo0,hi0, lo1,hi1
#else
# define HI 0
# define LO 4
# define WORD64(hi0,lo0,hi1,lo1)	.word	hi0,lo0, hi1,lo1
#endif

.text
#if __ARM_ARCH__<7
.code	32
#else
.syntax unified
# ifdef __thumb2__
#  define adrl adr
.thumb
# else
.code   32
# endif
#endif

.type	K512,%object
.align	5
K512:
WORD64(0x428a2f98,0xd728ae22, 0x71374491,0x23ef65cd)
WORD64(0xb5c0fbcf,0xec4d3b2f, 0xe9b5dba5,0x8189dbbc)
WORD64(0x3956c25b,0xf348b538, 0x59f111f1,0xb605d019)
WORD64(0x923f82a4,0xaf194f9b, 0xab1c5ed5,0xda6d8118)
WORD64(0xd807aa98,0xa3030242, 0x12835b01,0x45706fbe)
WORD64(0x243185be,0x4ee4b28c, 0x550c7dc3,0xd5ffb4e2)
WORD64(0x72be5d74,0xf27b896f, 0x80deb1fe,0x3b1696b1)
WORD64(0x9bdc06a7,0x25c71235, 0xc19bf174,0xcf692694)
WORD64(0xe49b69c1,0x9ef14ad2, 0xefbe4786,0x384f25e3)
WORD64(0x0fc19dc6,0x8b8cd5b5, 0x240ca1cc,0x77ac9c65)
WORD64(0x2de92c6f,0x592b0275, 0x4a7484aa,0x6ea6e483)
WORD64(0x5cb0a9dc,0xbd41fbd4, 0x76f988da,0x831153b5)
WORD64(0x983e5152,0xee66dfab, 0xa831c66d,0x2db43210)
WORD64(0xb00327c8,0x98fb213f, 0xbf597fc7,0xbeef0ee4)
WORD64(0xc6e00bf3,0x3da88fc2, 0xd5a79147,0x930aa725)
WORD64(0x06ca6351,0xe003826f, 0x14292967,0x0a0e6e70)
WORD64(0x27b70a85,0x46d22ffc, 0x2e1b2138,0x5c26c926)
WORD64(0x4d2c6dfc,0x5ac42aed, 0x53380d13,0x9d95b3df)
WORD64(0x650a7354,0x8baf63de, 0x766a0abb,0x3c77b2a8)
WORD64(0x81c2c92e,0x47edaee6, 0x92722c85,0x1482353b)
WORD64(0xa2bfe8a1,0x4cf10364, 0xa81a664b,0xbc423001)
WORD64(0xc24b8b70,0xd0f89791, 0xc76c51a3,0x0654be30)
WORD64(0xd192e819,0xd6ef5218, 0xd6990624,0x5565a910)
WORD64(0xf40e3585,0x5771202a, 0x106aa070,0x32bbd1b8)
WORD64(0x19a4c116,0xb8d2d0c8, 0x1e376c08,0x5141ab53)
WORD64(0x2748774c,0xdf8eeb99, 0x34b0bcb5,0xe19b48a8)
WORD64(0x391c0cb3,0xc5c95a63, 0x4ed8aa4a,0xe3418acb)
WORD64(0x5b9cca4f,0x7763e373, 0x682e6ff3,0xd6b2b8a3)
WORD64(0x748f82ee,0x5defb2fc, 0x78a5636f,0x43172f60)
WORD64(0x84c87814,0xa1f0ab72, 0x8cc70208,0x1a6439ec)
WORD64(0x90befffa,0x23631e28, 0xa4506ceb,0xde82bde9)
WORD64(0xbef9a3f7,0xb2c67915, 0xc67178f2,0xe372532b)
WORD64(0xca273ece,0xea26619c, 0xd186b8c7,0x21c0c207)
WORD64(0xeada7dd6,0xcde0eb1e, 0xf57d4f7f,0xee6ed178)
WORD64(0x06f067aa,0x72176fba, 0x0a637dc5,0xa2c898a6)
WORD64(0x113f9804,0xbef90dae, 0x1b710b35,0x131c471b)
WORD64(0x28db77f5,0x23047d84, 0x32caab7b,0x40c72493)
WORD64(0x3c9ebe0a,0x15c9bebc, 0x431d67c4,0x9c100d4c)
WORD64(0x4cc5d4be,0xcb3e42b6, 0x597f299c,0xfc657e2a)
WORD64(0x5fcb6fab,0x3ad6faec, 0x6c44198c,0x4a475817)
.size	K512,.-K512
#if __ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
.LOPENSSL_armcap:
.word	OPENSSL_armcap_P-sha512_block_data_order
.skip	32-4
#else
.skip	32
#endif

.global	sha512_block_data_order
.type	sha512_block_data_order,%function
sha512_block_data_order:
#if __ARM_ARCH__<7
	sub	r3,pc,#8		@ sha512_block_data_order
#else
	adr	r3,sha512_block_data_order
#endif
#if __ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
	ldr	r12,.LOPENSSL_armcap
	ldr	r12,[r3,r12]		@ OPENSSL_armcap_P
	tst	r12,#1
	bne	.LNEON
#endif
	add	$len,$inp,$len,lsl#7	@ len to point at the end of inp
	stmdb	sp!,{r4-r12,lr}
	sub	$Ktbl,r3,#672		@ K512
	sub	sp,sp,#9*8

	ldr	$Elo,[$ctx,#$Eoff+$lo]
	ldr	$Ehi,[$ctx,#$Eoff+$hi]
	ldr	$t0, [$ctx,#$Goff+$lo]
	ldr	$t1, [$ctx,#$Goff+$hi]
	ldr	$t2, [$ctx,#$Hoff+$lo]
	ldr	$t3, [$ctx,#$Hoff+$hi]
.Loop:
	str	$t0, [sp,#$Goff+0]
	str	$t1, [sp,#$Goff+4]
	str	$t2, [sp,#$Hoff+0]
	str	$t3, [sp,#$Hoff+4]
	ldr	$Alo,[$ctx,#$Aoff+$lo]
	ldr	$Ahi,[$ctx,#$Aoff+$hi]
	ldr	$Tlo,[$ctx,#$Boff+$lo]
	ldr	$Thi,[$ctx,#$Boff+$hi]
	ldr	$t0, [$ctx,#$Coff+$lo]
	ldr	$t1, [$ctx,#$Coff+$hi]
	ldr	$t2, [$ctx,#$Doff+$lo]
	ldr	$t3, [$ctx,#$Doff+$hi]
	str	$Tlo,[sp,#$Boff+0]
	str	$Thi,[sp,#$Boff+4]
	str	$t0, [sp,#$Coff+0]
	str	$t1, [sp,#$Coff+4]
	str	$t2, [sp,#$Doff+0]
	str	$t3, [sp,#$Doff+4]
	ldr	$Tlo,[$ctx,#$Foff+$lo]
	ldr	$Thi,[$ctx,#$Foff+$hi]
	str	$Tlo,[sp,#$Foff+0]
	str	$Thi,[sp,#$Foff+4]

.L00_15:
#if __ARM_ARCH__<7
	ldrb	$Tlo,[$inp,#7]
	ldrb	$t0, [$inp,#6]
	ldrb	$t1, [$inp,#5]
	ldrb	$t2, [$inp,#4]
	ldrb	$Thi,[$inp,#3]
	ldrb	$t3, [$inp,#2]
	orr	$Tlo,$Tlo,$t0,lsl#8
	ldrb	$t0, [$inp,#1]
	orr	$Tlo,$Tlo,$t1,lsl#16
	ldrb	$t1, [$inp],#8
	orr	$Tlo,$Tlo,$t2,lsl#24
	orr	$Thi,$Thi,$t3,lsl#8
	orr	$Thi,$Thi,$t0,lsl#16
	orr	$Thi,$Thi,$t1,lsl#24
#else
	ldr	$Tlo,[$inp,#4]
	ldr	$Thi,[$inp],#8
#ifdef __ARMEL__
	rev	$Tlo,$Tlo
	rev	$Thi,$Thi
#endif
#endif
___
	&BODY_00_15(0x94);
$code.=<<___;
	tst	$Ktbl,#1
	beq	.L00_15
	ldr	$t0,[sp,#`$Xoff+8*(16-1)`+0]
	ldr	$t1,[sp,#`$Xoff+8*(16-1)`+4]
	bic	$Ktbl,$Ktbl,#1
.L16_79:
	@ sigma0(x)	(ROTR((x),1)  ^ ROTR((x),8)  ^ ((x)>>7))
	@ LO		lo>>1^hi<<31  ^ lo>>8^hi<<24 ^ lo>>7^hi<<25
	@ HI		hi>>1^lo<<31  ^ hi>>8^lo<<24 ^ hi>>7
	mov	$Tlo,$t0,lsr#1
	ldr	$t2,[sp,#`$Xoff+8*(16-14)`+0]
	mov	$Thi,$t1,lsr#1
	ldr	$t3,[sp,#`$Xoff+8*(16-14)`+4]
	eor	$Tlo,$Tlo,$t1,lsl#31
	eor	$Thi,$Thi,$t0,lsl#31
	eor	$Tlo,$Tlo,$t0,lsr#8
	eor	$Thi,$Thi,$t1,lsr#8
	eor	$Tlo,$Tlo,$t1,lsl#24
	eor	$Thi,$Thi,$t0,lsl#24
	eor	$Tlo,$Tlo,$t0,lsr#7
	eor	$Thi,$Thi,$t1,lsr#7
	eor	$Tlo,$Tlo,$t1,lsl#25

	@ sigma1(x)	(ROTR((x),19) ^ ROTR((x),61) ^ ((x)>>6))
	@ LO		lo>>19^hi<<13 ^ hi>>29^lo<<3 ^ lo>>6^hi<<26
	@ HI		hi>>19^lo<<13 ^ lo>>29^hi<<3 ^ hi>>6
	mov	$t0,$t2,lsr#19
	mov	$t1,$t3,lsr#19
	eor	$t0,$t0,$t3,lsl#13
	eor	$t1,$t1,$t2,lsl#13
	eor	$t0,$t0,$t3,lsr#29
	eor	$t1,$t1,$t2,lsr#29
	eor	$t0,$t0,$t2,lsl#3
	eor	$t1,$t1,$t3,lsl#3
	eor	$t0,$t0,$t2,lsr#6
	eor	$t1,$t1,$t3,lsr#6
	ldr	$t2,[sp,#`$Xoff+8*(16-9)`+0]
	eor	$t0,$t0,$t3,lsl#26

	ldr	$t3,[sp,#`$Xoff+8*(16-9)`+4]
	adds	$Tlo,$Tlo,$t0
	ldr	$t0,[sp,#`$Xoff+8*16`+0]
	adc	$Thi,$Thi,$t1

	ldr	$t1,[sp,#`$Xoff+8*16`+4]
	adds	$Tlo,$Tlo,$t2
	adc	$Thi,$Thi,$t3
	adds	$Tlo,$Tlo,$t0
	adc	$Thi,$Thi,$t1
___
	&BODY_00_15(0x17);
$code.=<<___;
#if __ARM_ARCH__>=7
	ittt	eq			@ Thumb2 thing, sanity check in ARM
#endif
	ldreq	$t0,[sp,#`$Xoff+8*(16-1)`+0]
	ldreq	$t1,[sp,#`$Xoff+8*(16-1)`+4]
	beq	.L16_79
	bic	$Ktbl,$Ktbl,#1

	ldr	$Tlo,[sp,#$Boff+0]
	ldr	$Thi,[sp,#$Boff+4]
	ldr	$t0, [$ctx,#$Aoff+$lo]
	ldr	$t1, [$ctx,#$Aoff+$hi]
	ldr	$t2, [$ctx,#$Boff+$lo]
	ldr	$t3, [$ctx,#$Boff+$hi]
	adds	$t0,$Alo,$t0
	str	$t0, [$ctx,#$Aoff+$lo]
	adc	$t1,$Ahi,$t1
	str	$t1, [$ctx,#$Aoff+$hi]
	adds	$t2,$Tlo,$t2
	str	$t2, [$ctx,#$Boff+$lo]
	adc	$t3,$Thi,$t3
	str	$t3, [$ctx,#$Boff+$hi]

	ldr	$Alo,[sp,#$Coff+0]
	ldr	$Ahi,[sp,#$Coff+4]
	ldr	$Tlo,[sp,#$Doff+0]
	ldr	$Thi,[sp,#$Doff+4]
	ldr	$t0, [$ctx,#$Coff+$lo]
	ldr	$t1, [$ctx,#$Coff+$hi]
	ldr	$t2, [$ctx,#$Doff+$lo]
	ldr	$t3, [$ctx,#$Doff+$hi]
	adds	$t0,$Alo,$t0
	str	$t0, [$ctx,#$Coff+$lo]
	adc	$t1,$Ahi,$t1
	str	$t1, [$ctx,#$Coff+$hi]
	adds	$t2,$Tlo,$t2
	str	$t2, [$ctx,#$Doff+$lo]
	adc	$t3,$Thi,$t3
	str	$t3, [$ctx,#$Doff+$hi]

	ldr	$Tlo,[sp,#$Foff+0]
	ldr	$Thi,[sp,#$Foff+4]
	ldr	$t0, [$ctx,#$Eoff+$lo]
	ldr	$t1, [$ctx,#$Eoff+$hi]
	ldr	$t2, [$ctx,#$Foff+$lo]
	ldr	$t3, [$ctx,#$Foff+$hi]
	adds	$Elo,$Elo,$t0
	str	$Elo,[$ctx,#$Eoff+$lo]
	adc	$Ehi,$Ehi,$t1
	str	$Ehi,[$ctx,#$Eoff+$hi]
	adds	$t2,$Tlo,$t2
	str	$t2, [$ctx,#$Foff+$lo]
	adc	$t3,$Thi,$t3
	str	$t3, [$ctx,#$Foff+$hi]

	ldr	$Alo,[sp,#$Goff+0]
	ldr	$Ahi,[sp,#$Goff+4]
	ldr	$Tlo,[sp,#$Hoff+0]
	ldr	$Thi,[sp,#$Hoff+4]
	ldr	$t0, [$ctx,#$Goff+$lo]
	ldr	$t1, [$ctx,#$Goff+$hi]
	ldr	$t2, [$ctx,#$Hoff+$lo]
	ldr	$t3, [$ctx,#$Hoff+$hi]
	adds	$t0,$Alo,$t0
	str	$t0, [$ctx,#$Goff+$lo]
	adc	$t1,$Ahi,$t1
	str	$t1, [$ctx,#$Goff+$hi]
	adds	$t2,$Tlo,$t2
	str	$t2, [$ctx,#$Hoff+$lo]
	adc	$t3,$Thi,$t3
	str	$t3, [$ctx,#$Hoff+$hi]

	add	sp,sp,#640
	sub	$Ktbl,$Ktbl,#640

	teq	$inp,$len
	bne	.Loop

	add	sp,sp,#8*9		@ destroy frame
#if __ARM_ARCH__>=5
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	sha512_block_data_order,.-sha512_block_data_order
___

{
my @Sigma0=(28,34,39);
my @Sigma1=(14,18,41);
my @sigma0=(1, 8, 7);
my @sigma1=(19,61,6);

my $Ktbl="r3";
my $cnt="r12";	# volatile register known as ip, intra-procedure-call scratch

my @X=map("d$_",(0..15));
my @V=($A,$B,$C,$D,$E,$F,$G,$H)=map("d$_",(16..23));

sub NEON_00_15() {
my $i=shift;
my ($a,$b,$c,$d,$e,$f,$g,$h)=@_;
my ($t0,$t1,$t2,$T1,$K,$Ch,$Maj)=map("d$_",(24..31));	# temps

$code.=<<___ if ($i<16 || $i&1);
	vshr.u64	$t0,$e,#@Sigma1[0]	@ $i
#if $i<16
	vld1.64		{@X[$i%16]},[$inp]!	@ handles unaligned
#endif
	vshr.u64	$t1,$e,#@Sigma1[1]
#if $i>0
	 vadd.i64	$a,$Maj			@ h+=Maj from the past
#endif
	vshr.u64	$t2,$e,#@Sigma1[2]
___
$code.=<<___;
	vld1.64		{$K},[$Ktbl,:64]!	@ K[i++]
	vsli.64		$t0,$e,#`64-@Sigma1[0]`
	vsli.64		$t1,$e,#`64-@Sigma1[1]`
	vmov		$Ch,$e
	vsli.64		$t2,$e,#`64-@Sigma1[2]`
#if $i<16 && defined(__ARMEL__)
	vrev64.8	@X[$i],@X[$i]
#endif
	veor		$t1,$t0
	vbsl		$Ch,$f,$g		@ Ch(e,f,g)
	vshr.u64	$t0,$a,#@Sigma0[0]
	veor		$t2,$t1			@ Sigma1(e)
	vadd.i64	$T1,$Ch,$h
	vshr.u64	$t1,$a,#@Sigma0[1]
	vsli.64		$t0,$a,#`64-@Sigma0[0]`
	vadd.i64	$T1,$t2
	vshr.u64	$t2,$a,#@Sigma0[2]
	vadd.i64	$K,@X[$i%16]
	vsli.64		$t1,$a,#`64-@Sigma0[1]`
	veor		$Maj,$a,$b
	vsli.64		$t2,$a,#`64-@Sigma0[2]`
	veor		$h,$t0,$t1
	vadd.i64	$T1,$K
	vbsl		$Maj,$c,$b		@ Maj(a,b,c)
	veor		$h,$t2			@ Sigma0(a)
	vadd.i64	$d,$T1
	vadd.i64	$Maj,$T1
	@ vadd.i64	$h,$Maj
___
}

sub NEON_16_79() {
my $i=shift;

if ($i&1)	{ &NEON_00_15($i,@_); return; }

# 2x-vectorized, therefore runs every 2nd round
my @X=map("q$_",(0..7));			# view @X as 128-bit vector
my ($t0,$t1,$s0,$s1) = map("q$_",(12..15));	# temps
my ($d0,$d1,$d2) = map("d$_",(24..26));		# temps from NEON_00_15
my $e=@_[4];					# $e from NEON_00_15
$i /= 2;
$code.=<<___;
	vshr.u64	$t0,@X[($i+7)%8],#@sigma1[0]
	vshr.u64	$t1,@X[($i+7)%8],#@sigma1[1]
	 vadd.i64	@_[0],d30			@ h+=Maj from the past
	vshr.u64	$s1,@X[($i+7)%8],#@sigma1[2]
	vsli.64		$t0,@X[($i+7)%8],#`64-@sigma1[0]`
	vext.8		$s0,@X[$i%8],@X[($i+1)%8],#8	@ X[i+1]
	vsli.64		$t1,@X[($i+7)%8],#`64-@sigma1[1]`
	veor		$s1,$t0
	vshr.u64	$t0,$s0,#@sigma0[0]
	veor		$s1,$t1				@ sigma1(X[i+14])
	vshr.u64	$t1,$s0,#@sigma0[1]
	vadd.i64	@X[$i%8],$s1
	vshr.u64	$s1,$s0,#@sigma0[2]
	vsli.64		$t0,$s0,#`64-@sigma0[0]`
	vsli.64		$t1,$s0,#`64-@sigma0[1]`
	vext.8		$s0,@X[($i+4)%8],@X[($i+5)%8],#8	@ X[i+9]
	veor		$s1,$t0
	vshr.u64	$d0,$e,#@Sigma1[0]		@ from NEON_00_15
	vadd.i64	@X[$i%8],$s0
	vshr.u64	$d1,$e,#@Sigma1[1]		@ from NEON_00_15
	veor		$s1,$t1				@ sigma0(X[i+1])
	vshr.u64	$d2,$e,#@Sigma1[2]		@ from NEON_00_15
	vadd.i64	@X[$i%8],$s1
___
	&NEON_00_15(2*$i,@_);
}

$code.=<<___;
#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.global	sha512_block_data_order_neon
.type	sha512_block_data_order_neon,%function
.align	4
sha512_block_data_order_neon:
.LNEON:
	dmb				@ errata #451034 on early Cortex A8
	add	$len,$inp,$len,lsl#7	@ len to point at the end of inp
	VFP_ABI_PUSH
	adrl	$Ktbl,K512
	vldmia	$ctx,{$A-$H}		@ load context
.Loop_neon:
___
for($i=0;$i<16;$i++)	{ &NEON_00_15($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	mov		$cnt,#4
.L16_79_neon:
	subs		$cnt,#1
___
for(;$i<32;$i++)	{ &NEON_16_79($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	bne		.L16_79_neon

	 vadd.i64	$A,d30		@ h+=Maj from the past
	vldmia		$ctx,{d24-d31}	@ load context to temp
	vadd.i64	q8,q12		@ vectorized accumulate
	vadd.i64	q9,q13
	vadd.i64	q10,q14
	vadd.i64	q11,q15
	vstmia		$ctx,{$A-$H}	@ save context
	teq		$inp,$len
	sub		$Ktbl,#640	@ rewind K512
	bne		.Loop_neon

	VFP_ABI_POP
	ret				@ bx lr
.size	sha512_block_data_order_neon,.-sha512_block_data_order_neon
#endif
___
}
$code.=<<___;
.asciz	"SHA512 block transform for ARMv4/NEON, CRYPTOGAMS by <appro\@openssl.org>"
.align	2
#if __ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
.comm	OPENSSL_armcap_P,4,4
#endif
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
$code =~ s/\bbx\s+lr\b/.word\t0xe12fff1e/gm;	# make it possible to compile with -march=armv4
$code =~ s/\bret\b/bx	lr/gm;

open SELF,$0;
while(<SELF>) {
	next if (/^#!/);
	last if (!s/^#/@/ and !/^$/);
	print;
}
close SELF;

print $code;
close STDOUT; # enforce flush

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

# AES for ARMv4

# January 2007.
#
# Code uses single 1K S-box and is >2 times faster than code generated
# by gcc-3.4.1. This is thanks to unique feature of ARMv4 ISA, which
# allows to merge logical or arithmetic operation with shift or rotate
# in one instruction and emit combined result every cycle. The module
# is endian-neutral. The performance is ~42 cycles/byte for 128-bit
# key [on single-issue Xscale PXA250 core].

# May 2007.
#
# AES_set_[en|de]crypt_key is added.

# July 2010.
#
# Rescheduling for dual-issue pipeline resulted in 12% improvement on
# Cortex A8 core and ~25 cycles per byte processed with 128-bit key.

# February 2011.
#
# Profiler-assisted and platform-specific optimization resulted in 16%
# improvement on Cortex A8 core and ~21.5 cycles per byte.

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

$s0="r0";
$s1="r1";
$s2="r2";
$s3="r3";
$t1="r4";
$t2="r5";
$t3="r6";
$i1="r7";
$i2="r8";
$i3="r9";

$tbl="r10";
$key="r11";
$rounds="r12";

$code=<<___;
#ifndef __KERNEL__
# include "arm_arch.h"
#else
# define __ARM_ARCH__ __LINUX_ARM_ARCH__
#endif

.text
#if defined(__thumb2__) && !defined(__APPLE__)
.syntax	unified
.thumb
#else
.code	32
#undef __thumb2__
#endif

.type	AES_Te,%object
.align	5
AES_Te:
.word	0xc66363a5, 0xf87c7c84, 0xee777799, 0xf67b7b8d
.word	0xfff2f20d, 0xd66b6bbd, 0xde6f6fb1, 0x91c5c554
.word	0x60303050, 0x02010103, 0xce6767a9, 0x562b2b7d
.word	0xe7fefe19, 0xb5d7d762, 0x4dababe6, 0xec76769a
.word	0x8fcaca45, 0x1f82829d, 0x89c9c940, 0xfa7d7d87
.word	0xeffafa15, 0xb25959eb, 0x8e4747c9, 0xfbf0f00b
.word	0x41adadec, 0xb3d4d467, 0x5fa2a2fd, 0x45afafea
.word	0x239c9cbf, 0x53a4a4f7, 0xe4727296, 0x9bc0c05b
.word	0x75b7b7c2, 0xe1fdfd1c, 0x3d9393ae, 0x4c26266a
.word	0x6c36365a, 0x7e3f3f41, 0xf5f7f702, 0x83cccc4f
.word	0x6834345c, 0x51a5a5f4, 0xd1e5e534, 0xf9f1f108
.word	0xe2717193, 0xabd8d873, 0x62313153, 0x2a15153f
.word	0x0804040c, 0x95c7c752, 0x46232365, 0x9dc3c35e
.word	0x30181828, 0x379696a1, 0x0a05050f, 0x2f9a9ab5
.word	0x0e070709, 0x24121236, 0x1b80809b, 0xdfe2e23d
.word	0xcdebeb26, 0x4e272769, 0x7fb2b2cd, 0xea75759f
.word	0x1209091b, 0x1d83839e, 0x582c2c74, 0x341a1a2e
.word	0x361b1b2d, 0xdc6e6eb2, 0xb45a5aee, 0x5ba0a0fb
.word	0xa45252f6, 0x763b3b4d, 0xb7d6d661, 0x7db3b3ce
.word	0x5229297b, 0xdde3e33e, 0x5e2f2f71, 0x13848497
.word	0xa65353f5, 0xb9d1d168, 0x00000000, 0xc1eded2c
.word	0x40202060, 0xe3fcfc1f, 0x79b1b1c8, 0xb65b5bed
.word	0xd46a6abe, 0x8dcbcb46, 0x67bebed9, 0x7239394b
.word	0x944a4ade, 0x984c4cd4, 0xb05858e8, 0x85cfcf4a
.word	0xbbd0d06b, 0xc5efef2a, 0x4faaaae5, 0xedfbfb16
.word	0x864343c5, 0x9a4d4dd7, 0x66333355, 0x11858594
.word	0x8a4545cf, 0xe9f9f910, 0x04020206, 0xfe7f7f81
.word	0xa05050f0, 0x783c3c44, 0x259f9fba, 0x4ba8a8e3
.word	0xa25151f3, 0x5da3a3fe, 0x804040c0, 0x058f8f8a
.word	0x3f9292ad, 0x219d9dbc, 0x70383848, 0xf1f5f504
.word	0x63bcbcdf, 0x77b6b6c1, 0xafdada75, 0x42212163
.word	0x20101030, 0xe5ffff1a, 0xfdf3f30e, 0xbfd2d26d
.word	0x81cdcd4c, 0x180c0c14, 0x26131335, 0xc3ecec2f
.word	0xbe5f5fe1, 0x359797a2, 0x884444cc, 0x2e171739
.word	0x93c4c457, 0x55a7a7f2, 0xfc7e7e82, 0x7a3d3d47
.word	0xc86464ac, 0xba5d5de7, 0x3219192b, 0xe6737395
.word	0xc06060a0, 0x19818198, 0x9e4f4fd1, 0xa3dcdc7f
.word	0x44222266, 0x542a2a7e, 0x3b9090ab, 0x0b888883
.word	0x8c4646ca, 0xc7eeee29, 0x6bb8b8d3, 0x2814143c
.word	0xa7dede79, 0xbc5e5ee2, 0x160b0b1d, 0xaddbdb76
.word	0xdbe0e03b, 0x64323256, 0x743a3a4e, 0x140a0a1e
.word	0x924949db, 0x0c06060a, 0x4824246c, 0xb85c5ce4
.word	0x9fc2c25d, 0xbdd3d36e, 0x43acacef, 0xc46262a6
.word	0x399191a8, 0x319595a4, 0xd3e4e437, 0xf279798b
.word	0xd5e7e732, 0x8bc8c843, 0x6e373759, 0xda6d6db7
.word	0x018d8d8c, 0xb1d5d564, 0x9c4e4ed2, 0x49a9a9e0
.word	0xd86c6cb4, 0xac5656fa, 0xf3f4f407, 0xcfeaea25
.word	0xca6565af, 0xf47a7a8e, 0x47aeaee9, 0x10080818
.word	0x6fbabad5, 0xf0787888, 0x4a25256f, 0x5c2e2e72
.word	0x381c1c24, 0x57a6a6f1, 0x73b4b4c7, 0x97c6c651
.word	0xcbe8e823, 0xa1dddd7c, 0xe874749c, 0x3e1f1f21
.word	0x964b4bdd, 0x61bdbddc, 0x0d8b8b86, 0x0f8a8a85
.word	0xe0707090, 0x7c3e3e42, 0x71b5b5c4, 0xcc6666aa
.word	0x904848d8, 0x06030305, 0xf7f6f601, 0x1c0e0e12
.word	0xc26161a3, 0x6a35355f, 0xae5757f9, 0x69b9b9d0
.word	0x17868691, 0x99c1c158, 0x3a1d1d27, 0x279e9eb9
.word	0xd9e1e138, 0xebf8f813, 0x2b9898b3, 0x22111133
.word	0xd26969bb, 0xa9d9d970, 0x078e8e89, 0x339494a7
.word	0x2d9b9bb6, 0x3c1e1e22, 0x15878792, 0xc9e9e920
.word	0x87cece49, 0xaa5555ff, 0x50282878, 0xa5dfdf7a
.word	0x038c8c8f, 0x59a1a1f8, 0x09898980, 0x1a0d0d17
.word	0x65bfbfda, 0xd7e6e631, 0x844242c6, 0xd06868b8
.word	0x824141c3, 0x299999b0, 0x5a2d2d77, 0x1e0f0f11
.word	0x7bb0b0cb, 0xa85454fc, 0x6dbbbbd6, 0x2c16163a
@ Te4[256]
.byte	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5
.byte	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76
.byte	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0
.byte	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0
.byte	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc
.byte	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15
.byte	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a
.byte	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75
.byte	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0
.byte	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84
.byte	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b
.byte	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf
.byte	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85
.byte	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8
.byte	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5
.byte	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2
.byte	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17
.byte	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73
.byte	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88
.byte	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb
.byte	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c
.byte	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79
.byte	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9
.byte	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08
.byte	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6
.byte	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a
.byte	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e
.byte	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e
.byte	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94
.byte	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf
.byte	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68
.byte	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
@ rcon[]
.word	0x01000000, 0x02000000, 0x04000000, 0x08000000
.word	0x10000000, 0x20000000, 0x40000000, 0x80000000
.word	0x1B000000, 0x36000000, 0, 0, 0, 0, 0, 0
.size	AES_Te,.-AES_Te

@ void AES_encrypt(const unsigned char *in, unsigned char *out,
@ 		 const AES_KEY *key) {
.global AES_encrypt
.type   AES_encrypt,%function
.align	5
AES_encrypt:
#ifndef	__thumb2__
	sub	r3,pc,#8		@ AES_encrypt
#else
	adr	r3,.
#endif
	stmdb   sp!,{r1,r4-r12,lr}
#if defined(__thumb2__) || defined(__APPLE__)
	adr	$tbl,AES_Te
#else
	sub	$tbl,r3,#AES_encrypt-AES_Te	@ Te
#endif
	mov	$rounds,r0		@ inp
	mov	$key,r2
#if __ARM_ARCH__<7
	ldrb	$s0,[$rounds,#3]	@ load input data in endian-neutral
	ldrb	$t1,[$rounds,#2]	@ manner...
	ldrb	$t2,[$rounds,#1]
	ldrb	$t3,[$rounds,#0]
	orr	$s0,$s0,$t1,lsl#8
	ldrb	$s1,[$rounds,#7]
	orr	$s0,$s0,$t2,lsl#16
	ldrb	$t1,[$rounds,#6]
	orr	$s0,$s0,$t3,lsl#24
	ldrb	$t2,[$rounds,#5]
	ldrb	$t3,[$rounds,#4]
	orr	$s1,$s1,$t1,lsl#8
	ldrb	$s2,[$rounds,#11]
	orr	$s1,$s1,$t2,lsl#16
	ldrb	$t1,[$rounds,#10]
	orr	$s1,$s1,$t3,lsl#24
	ldrb	$t2,[$rounds,#9]
	ldrb	$t3,[$rounds,#8]
	orr	$s2,$s2,$t1,lsl#8
	ldrb	$s3,[$rounds,#15]
	orr	$s2,$s2,$t2,lsl#16
	ldrb	$t1,[$rounds,#14]
	orr	$s2,$s2,$t3,lsl#24
	ldrb	$t2,[$rounds,#13]
	ldrb	$t3,[$rounds,#12]
	orr	$s3,$s3,$t1,lsl#8
	orr	$s3,$s3,$t2,lsl#16
	orr	$s3,$s3,$t3,lsl#24
#else
	ldr	$s0,[$rounds,#0]
	ldr	$s1,[$rounds,#4]
	ldr	$s2,[$rounds,#8]
	ldr	$s3,[$rounds,#12]
#ifdef __ARMEL__
	rev	$s0,$s0
	rev	$s1,$s1
	rev	$s2,$s2
	rev	$s3,$s3
#endif
#endif
	bl	_armv4_AES_encrypt

	ldr	$rounds,[sp],#4		@ pop out
#if __ARM_ARCH__>=7
#ifdef __ARMEL__
	rev	$s0,$s0
	rev	$s1,$s1
	rev	$s2,$s2
	rev	$s3,$s3
#endif
	str	$s0,[$rounds,#0]
	str	$s1,[$rounds,#4]
	str	$s2,[$rounds,#8]
	str	$s3,[$rounds,#12]
#else
	mov	$t1,$s0,lsr#24		@ write output in endian-neutral
	mov	$t2,$s0,lsr#16		@ manner...
	mov	$t3,$s0,lsr#8
	strb	$t1,[$rounds,#0]
	strb	$t2,[$rounds,#1]
	mov	$t1,$s1,lsr#24
	strb	$t3,[$rounds,#2]
	mov	$t2,$s1,lsr#16
	strb	$s0,[$rounds,#3]
	mov	$t3,$s1,lsr#8
	strb	$t1,[$rounds,#4]
	strb	$t2,[$rounds,#5]
	mov	$t1,$s2,lsr#24
	strb	$t3,[$rounds,#6]
	mov	$t2,$s2,lsr#16
	strb	$s1,[$rounds,#7]
	mov	$t3,$s2,lsr#8
	strb	$t1,[$rounds,#8]
	strb	$t2,[$rounds,#9]
	mov	$t1,$s3,lsr#24
	strb	$t3,[$rounds,#10]
	mov	$t2,$s3,lsr#16
	strb	$s2,[$rounds,#11]
	mov	$t3,$s3,lsr#8
	strb	$t1,[$rounds,#12]
	strb	$t2,[$rounds,#13]
	strb	$t3,[$rounds,#14]
	strb	$s3,[$rounds,#15]
#endif
#if __ARM_ARCH__>=5
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia   sp!,{r4-r12,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	AES_encrypt,.-AES_encrypt

.type   _armv4_AES_encrypt,%function
.align	2
_armv4_AES_encrypt:
	str	lr,[sp,#-4]!		@ push lr
	ldmia	$key!,{$t1-$i1}
	eor	$s0,$s0,$t1
	ldr	$rounds,[$key,#240-16]
	eor	$s1,$s1,$t2
	eor	$s2,$s2,$t3
	eor	$s3,$s3,$i1
	sub	$rounds,$rounds,#1
	mov	lr,#255

	and	$i1,lr,$s0
	and	$i2,lr,$s0,lsr#8
	and	$i3,lr,$s0,lsr#16
	mov	$s0,$s0,lsr#24
.Lenc_loop:
	ldr	$t1,[$tbl,$i1,lsl#2]	@ Te3[s0>>0]
	and	$i1,lr,$s1,lsr#16	@ i0
	ldr	$t2,[$tbl,$i2,lsl#2]	@ Te2[s0>>8]
	and	$i2,lr,$s1
	ldr	$t3,[$tbl,$i3,lsl#2]	@ Te1[s0>>16]
	and	$i3,lr,$s1,lsr#8
	ldr	$s0,[$tbl,$s0,lsl#2]	@ Te0[s0>>24]
	mov	$s1,$s1,lsr#24

	ldr	$i1,[$tbl,$i1,lsl#2]	@ Te1[s1>>16]
	ldr	$i2,[$tbl,$i2,lsl#2]	@ Te3[s1>>0]
	ldr	$i3,[$tbl,$i3,lsl#2]	@ Te2[s1>>8]
	eor	$s0,$s0,$i1,ror#8
	ldr	$s1,[$tbl,$s1,lsl#2]	@ Te0[s1>>24]
	and	$i1,lr,$s2,lsr#8	@ i0
	eor	$t2,$t2,$i2,ror#8
	and	$i2,lr,$s2,lsr#16	@ i1
	eor	$t3,$t3,$i3,ror#8
	and	$i3,lr,$s2
	ldr	$i1,[$tbl,$i1,lsl#2]	@ Te2[s2>>8]
	eor	$s1,$s1,$t1,ror#24
	ldr	$i2,[$tbl,$i2,lsl#2]	@ Te1[s2>>16]
	mov	$s2,$s2,lsr#24

	ldr	$i3,[$tbl,$i3,lsl#2]	@ Te3[s2>>0]
	eor	$s0,$s0,$i1,ror#16
	ldr	$s2,[$tbl,$s2,lsl#2]	@ Te0[s2>>24]
	and	$i1,lr,$s3		@ i0
	eor	$s1,$s1,$i2,ror#8
	and	$i2,lr,$s3,lsr#8	@ i1
	eor	$t3,$t3,$i3,ror#16
	and	$i3,lr,$s3,lsr#16	@ i2
	ldr	$i1,[$tbl,$i1,lsl#2]	@ Te3[s3>>0]
	eor	$s2,$s2,$t2,ror#16
	ldr	$i2,[$tbl,$i2,lsl#2]	@ Te2[s3>>8]
	mov	$s3,$s3,lsr#24

	ldr	$i3,[$tbl,$i3,lsl#2]	@ Te1[s3>>16]
	eor	$s0,$s0,$i1,ror#24
	ldr	$i1,[$key],#16
	eor	$s1,$s1,$i2,ror#16
	ldr	$s3,[$tbl,$s3,lsl#2]	@ Te0[s3>>24]
	eor	$s2,$s2,$i3,ror#8
	ldr	$t1,[$key,#-12]
	eor	$s3,$s3,$t3,ror#8

	ldr	$t2,[$key,#-8]
	eor	$s0,$s0,$i1
	ldr	$t3,[$key,#-4]
	and	$i1,lr,$s0
	eor	$s1,$s1,$t1
	and	$i2,lr,$s0,lsr#8
	eor	$s2,$s2,$t2
	and	$i3,lr,$s0,lsr#16
	eor	$s3,$s3,$t3
	mov	$s0,$s0,lsr#24

	subs	$rounds,$rounds,#1
	bne	.Lenc_loop

	add	$tbl,$tbl,#2

	ldrb	$t1,[$tbl,$i1,lsl#2]	@ Te4[s0>>0]
	and	$i1,lr,$s1,lsr#16	@ i0
	ldrb	$t2,[$tbl,$i2,lsl#2]	@ Te4[s0>>8]
	and	$i2,lr,$s1
	ldrb	$t3,[$tbl,$i3,lsl#2]	@ Te4[s0>>16]
	and	$i3,lr,$s1,lsr#8
	ldrb	$s0,[$tbl,$s0,lsl#2]	@ Te4[s0>>24]
	mov	$s1,$s1,lsr#24

	ldrb	$i1,[$tbl,$i1,lsl#2]	@ Te4[s1>>16]
	ldrb	$i2,[$tbl,$i2,lsl#2]	@ Te4[s1>>0]
	ldrb	$i3,[$tbl,$i3,lsl#2]	@ Te4[s1>>8]
	eor	$s0,$i1,$s0,lsl#8
	ldrb	$s1,[$tbl,$s1,lsl#2]	@ Te4[s1>>24]
	and	$i1,lr,$s2,lsr#8	@ i0
	eor	$t2,$i2,$t2,lsl#8
	and	$i2,lr,$s2,lsr#16	@ i1
	eor	$t3,$i3,$t3,lsl#8
	and	$i3,lr,$s2
	ldrb	$i1,[$tbl,$i1,lsl#2]	@ Te4[s2>>8]
	eor	$s1,$t1,$s1,lsl#24
	ldrb	$i2,[$tbl,$i2,lsl#2]	@ Te4[s2>>16]
	mov	$s2,$s2,lsr#24

	ldrb	$i3,[$tbl,$i3,lsl#2]	@ Te4[s2>>0]
	eor	$s0,$i1,$s0,lsl#8
	ldrb	$s2,[$tbl,$s2,lsl#2]	@ Te4[s2>>24]
	and	$i1,lr,$s3		@ i0
	eor	$s1,$s1,$i2,lsl#16
	and	$i2,lr,$s3,lsr#8	@ i1
	eor	$t3,$i3,$t3,lsl#8
	and	$i3,lr,$s3,lsr#16	@ i2
	ldrb	$i1,[$tbl,$i1,lsl#2]	@ Te4[s3>>0]
	eor	$s2,$t2,$s2,lsl#24
	ldrb	$i2,[$tbl,$i2,lsl#2]	@ Te4[s3>>8]
	mov	$s3,$s3,lsr#24

	ldrb	$i3,[$tbl,$i3,lsl#2]	@ Te4[s3>>16]
	eor	$s0,$i1,$s0,lsl#8
	ldr	$i1,[$key,#0]
	ldrb	$s3,[$tbl,$s3,lsl#2]	@ Te4[s3>>24]
	eor	$s1,$s1,$i2,lsl#8
	ldr	$t1,[$key,#4]
	eor	$s2,$s2,$i3,lsl#16
	ldr	$t2,[$key,#8]
	eor	$s3,$t3,$s3,lsl#24
	ldr	$t3,[$key,#12]

	eor	$s0,$s0,$i1
	eor	$s1,$s1,$t1
	eor	$s2,$s2,$t2
	eor	$s3,$s3,$t3

	sub	$tbl,$tbl,#2
	ldr	pc,[sp],#4		@ pop and return
.size	_armv4_AES_encrypt,.-_armv4_AES_encrypt

.global AES_set_encrypt_key
.type   AES_set_encrypt_key,%function
.align	5
AES_set_encrypt_key:
_armv4_AES_set_encrypt_key:
#ifndef	__thumb2__
	sub	r3,pc,#8		@ AES_set_encrypt_key
#else
	adr	r3,.
#endif
	teq	r0,#0
#ifdef	__thumb2__
	itt	eq			@ Thumb2 thing, sanity check in ARM
#endif
	moveq	r0,#-1
	beq	.Labrt
	teq	r2,#0
#ifdef	__thumb2__
	itt	eq			@ Thumb2 thing, sanity check in ARM
#endif
	moveq	r0,#-1
	beq	.Labrt

	teq	r1,#128
	beq	.Lok
	teq	r1,#192
	beq	.Lok
	teq	r1,#256
#ifdef	__thumb2__
	itt	ne			@ Thumb2 thing, sanity check in ARM
#endif
	movne	r0,#-1
	bne	.Labrt

.Lok:	stmdb   sp!,{r4-r12,lr}
	mov	$rounds,r0		@ inp
	mov	lr,r1			@ bits
	mov	$key,r2			@ key

#if defined(__thumb2__) || defined(__APPLE__)
	adr	$tbl,AES_Te+1024				@ Te4
#else
	sub	$tbl,r3,#_armv4_AES_set_encrypt_key-AES_Te-1024	@ Te4
#endif

#if __ARM_ARCH__<7
	ldrb	$s0,[$rounds,#3]	@ load input data in endian-neutral
	ldrb	$t1,[$rounds,#2]	@ manner...
	ldrb	$t2,[$rounds,#1]
	ldrb	$t3,[$rounds,#0]
	orr	$s0,$s0,$t1,lsl#8
	ldrb	$s1,[$rounds,#7]
	orr	$s0,$s0,$t2,lsl#16
	ldrb	$t1,[$rounds,#6]
	orr	$s0,$s0,$t3,lsl#24
	ldrb	$t2,[$rounds,#5]
	ldrb	$t3,[$rounds,#4]
	orr	$s1,$s1,$t1,lsl#8
	ldrb	$s2,[$rounds,#11]
	orr	$s1,$s1,$t2,lsl#16
	ldrb	$t1,[$rounds,#10]
	orr	$s1,$s1,$t3,lsl#24
	ldrb	$t2,[$rounds,#9]
	ldrb	$t3,[$rounds,#8]
	orr	$s2,$s2,$t1,lsl#8
	ldrb	$s3,[$rounds,#15]
	orr	$s2,$s2,$t2,lsl#16
	ldrb	$t1,[$rounds,#14]
	orr	$s2,$s2,$t3,lsl#24
	ldrb	$t2,[$rounds,#13]
	ldrb	$t3,[$rounds,#12]
	orr	$s3,$s3,$t1,lsl#8
	str	$s0,[$key],#16
	orr	$s3,$s3,$t2,lsl#16
	str	$s1,[$key,#-12]
	orr	$s3,$s3,$t3,lsl#24
	str	$s2,[$key,#-8]
	str	$s3,[$key,#-4]
#else
	ldr	$s0,[$rounds,#0]
	ldr	$s1,[$rounds,#4]
	ldr	$s2,[$rounds,#8]
	ldr	$s3,[$rounds,#12]
#ifdef __ARMEL__
	rev	$s0,$s0
	rev	$s1,$s1
	rev	$s2,$s2
	rev	$s3,$s3
#endif
	str	$s0,[$key],#16
	str	$s1,[$key,#-12]
	str	$s2,[$key,#-8]
	str	$s3,[$key,#-4]
#endif

	teq	lr,#128
	bne	.Lnot128
	mov	$rounds,#10
	str	$rounds,[$key,#240-16]
	add	$t3,$tbl,#256			@ rcon
	mov	lr,#255

.L128_loop:
	and	$t2,lr,$s3,lsr#24
	and	$i1,lr,$s3,lsr#16
	ldrb	$t2,[$tbl,$t2]
	and	$i2,lr,$s3,lsr#8
	ldrb	$i1,[$tbl,$i1]
	and	$i3,lr,$s3
	ldrb	$i2,[$tbl,$i2]
	orr	$t2,$t2,$i1,lsl#24
	ldrb	$i3,[$tbl,$i3]
	orr	$t2,$t2,$i2,lsl#16
	ldr	$t1,[$t3],#4			@ rcon[i++]
	orr	$t2,$t2,$i3,lsl#8
	eor	$t2,$t2,$t1
	eor	$s0,$s0,$t2			@ rk[4]=rk[0]^...
	eor	$s1,$s1,$s0			@ rk[5]=rk[1]^rk[4]
	str	$s0,[$key],#16
	eor	$s2,$s2,$s1			@ rk[6]=rk[2]^rk[5]
	str	$s1,[$key,#-12]
	eor	$s3,$s3,$s2			@ rk[7]=rk[3]^rk[6]
	str	$s2,[$key,#-8]
	subs	$rounds,$rounds,#1
	str	$s3,[$key,#-4]
	bne	.L128_loop
	sub	r2,$key,#176
	b	.Ldone

.Lnot128:
#if __ARM_ARCH__<7
	ldrb	$i2,[$rounds,#19]
	ldrb	$t1,[$rounds,#18]
	ldrb	$t2,[$rounds,#17]
	ldrb	$t3,[$rounds,#16]
	orr	$i2,$i2,$t1,lsl#8
	ldrb	$i3,[$rounds,#23]
	orr	$i2,$i2,$t2,lsl#16
	ldrb	$t1,[$rounds,#22]
	orr	$i2,$i2,$t3,lsl#24
	ldrb	$t2,[$rounds,#21]
	ldrb	$t3,[$rounds,#20]
	orr	$i3,$i3,$t1,lsl#8
	orr	$i3,$i3,$t2,lsl#16
	str	$i2,[$key],#8
	orr	$i3,$i3,$t3,lsl#24
	str	$i3,[$key,#-4]
#else
	ldr	$i2,[$rounds,#16]
	ldr	$i3,[$rounds,#20]
#ifdef __ARMEL__
	rev	$i2,$i2
	rev	$i3,$i3
#endif
	str	$i2,[$key],#8
	str	$i3,[$key,#-4]
#endif

	teq	lr,#192
	bne	.Lnot192
	mov	$rounds,#12
	str	$rounds,[$key,#240-24]
	add	$t3,$tbl,#256			@ rcon
	mov	lr,#255
	mov	$rounds,#8

.L192_loop:
	and	$t2,lr,$i3,lsr#24
	and	$i1,lr,$i3,lsr#16
	ldrb	$t2,[$tbl,$t2]
	and	$i2,lr,$i3,lsr#8
	ldrb	$i1,[$tbl,$i1]
	and	$i3,lr,$i3
	ldrb	$i2,[$tbl,$i2]
	orr	$t2,$t2,$i1,lsl#24
	ldrb	$i3,[$tbl,$i3]
	orr	$t2,$t2,$i2,lsl#16
	ldr	$t1,[$t3],#4			@ rcon[i++]
	orr	$t2,$t2,$i3,lsl#8
	eor	$i3,$t2,$t1
	eor	$s0,$s0,$i3			@ rk[6]=rk[0]^...
	eor	$s1,$s1,$s0			@ rk[7]=rk[1]^rk[6]
	str	$s0,[$key],#24
	eor	$s2,$s2,$s1			@ rk[8]=rk[2]^rk[7]
	str	$s1,[$key,#-20]
	eor	$s3,$s3,$s2			@ rk[9]=rk[3]^rk[8]
	str	$s2,[$key,#-16]
	subs	$rounds,$rounds,#1
	str	$s3,[$key,#-12]
#ifdef	__thumb2__
	itt	eq				@ Thumb2 thing, sanity check in ARM
#endif
	subeq	r2,$key,#216
	beq	.Ldone

	ldr	$i1,[$key,#-32]
	ldr	$i2,[$key,#-28]
	eor	$i1,$i1,$s3			@ rk[10]=rk[4]^rk[9]
	eor	$i3,$i2,$i1			@ rk[11]=rk[5]^rk[10]
	str	$i1,[$key,#-8]
	str	$i3,[$key,#-4]
	b	.L192_loop

.Lnot192:
#if __ARM_ARCH__<7
	ldrb	$i2,[$rounds,#27]
	ldrb	$t1,[$rounds,#26]
	ldrb	$t2,[$rounds,#25]
	ldrb	$t3,[$rounds,#24]
	orr	$i2,$i2,$t1,lsl#8
	ldrb	$i3,[$rounds,#31]
	orr	$i2,$i2,$t2,lsl#16
	ldrb	$t1,[$rounds,#30]
	orr	$i2,$i2,$t3,lsl#24
	ldrb	$t2,[$rounds,#29]
	ldrb	$t3,[$rounds,#28]
	orr	$i3,$i3,$t1,lsl#8
	orr	$i3,$i3,$t2,lsl#16
	str	$i2,[$key],#8
	orr	$i3,$i3,$t3,lsl#24
	str	$i3,[$key,#-4]
#else
	ldr	$i2,[$rounds,#24]
	ldr	$i3,[$rounds,#28]
#ifdef __ARMEL__
	rev	$i2,$i2
	rev	$i3,$i3
#endif
	str	$i2,[$key],#8
	str	$i3,[$key,#-4]
#endif

	mov	$rounds,#14
	str	$rounds,[$key,#240-32]
	add	$t3,$tbl,#256			@ rcon
	mov	lr,#255
	mov	$rounds,#7

.L256_loop:
	and	$t2,lr,$i3,lsr#24
	and	$i1,lr,$i3,lsr#16
	ldrb	$t2,[$tbl,$t2]
	and	$i2,lr,$i3,lsr#8
	ldrb	$i1,[$tbl,$i1]
	and	$i3,lr,$i3
	ldrb	$i2,[$tbl,$i2]
	orr	$t2,$t2,$i1,lsl#24
	ldrb	$i3,[$tbl,$i3]
	orr	$t2,$t2,$i2,lsl#16
	ldr	$t1,[$t3],#4			@ rcon[i++]
	orr	$t2,$t2,$i3,lsl#8
	eor	$i3,$t2,$t1
	eor	$s0,$s0,$i3			@ rk[8]=rk[0]^...
	eor	$s1,$s1,$s0			@ rk[9]=rk[1]^rk[8]
	str	$s0,[$key],#32
	eor	$s2,$s2,$s1			@ rk[10]=rk[2]^rk[9]
	str	$s1,[$key,#-28]
	eor	$s3,$s3,$s2			@ rk[11]=rk[3]^rk[10]
	str	$s2,[$key,#-24]
	subs	$rounds,$rounds,#1
	str	$s3,[$key,#-20]
#ifdef	__thumb2__
	itt	eq				@ Thumb2 thing, sanity check in ARM
#endif
	subeq	r2,$key,#256
	beq	.Ldone

	and	$t2,lr,$s3
	and	$i1,lr,$s3,lsr#8
	ldrb	$t2,[$tbl,$t2]
	and	$i2,lr,$s3,lsr#16
	ldrb	$i1,[$tbl,$i1]
	and	$i3,lr,$s3,lsr#24
	ldrb	$i2,[$tbl,$i2]
	orr	$t2,$t2,$i1,lsl#8
	ldrb	$i3,[$tbl,$i3]
	orr	$t2,$t2,$i2,lsl#16
	ldr	$t1,[$key,#-48]
	orr	$t2,$t2,$i3,lsl#24

	ldr	$i1,[$key,#-44]
	ldr	$i2,[$key,#-40]
	eor	$t1,$t1,$t2			@ rk[12]=rk[4]^...
	ldr	$i3,[$key,#-36]
	eor	$i1,$i1,$t1			@ rk[13]=rk[5]^rk[12]
	str	$t1,[$key,#-16]
	eor	$i2,$i2,$i1			@ rk[14]=rk[6]^rk[13]
	str	$i1,[$key,#-12]
	eor	$i3,$i3,$i2			@ rk[15]=rk[7]^rk[14]
	str	$i2,[$key,#-8]
	str	$i3,[$key,#-4]
	b	.L256_loop

.align	2
.Ldone:	mov	r0,#0
	ldmia   sp!,{r4-r12,lr}
.Labrt:
#if __ARM_ARCH__>=5
	ret				@ bx lr
#else
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	AES_set_encrypt_key,.-AES_set_encrypt_key

.global AES_set_decrypt_key
.type   AES_set_decrypt_key,%function
.align	5
AES_set_decrypt_key:
	str	lr,[sp,#-4]!            @ push lr
	bl	_armv4_AES_set_encrypt_key
	teq	r0,#0
	ldr	lr,[sp],#4              @ pop lr
	bne	.Labrt

	mov	r0,r2			@ AES_set_encrypt_key preserves r2,
	mov	r1,r2			@ which is AES_KEY *key
	b	_armv4_AES_set_enc2dec_key
.size	AES_set_decrypt_key,.-AES_set_decrypt_key

@ void AES_set_enc2dec_key(const AES_KEY *inp,AES_KEY *out)
.global	AES_set_enc2dec_key
.type	AES_set_enc2dec_key,%function
.align	5
AES_set_enc2dec_key:
_armv4_AES_set_enc2dec_key:
	stmdb   sp!,{r4-r12,lr}

	ldr	$rounds,[r0,#240]
	mov	$i1,r0			@ input
	add	$i2,r0,$rounds,lsl#4
	mov	$key,r1			@ output
	add	$tbl,r1,$rounds,lsl#4
	str	$rounds,[r1,#240]

.Linv:	ldr	$s0,[$i1],#16
	ldr	$s1,[$i1,#-12]
	ldr	$s2,[$i1,#-8]
	ldr	$s3,[$i1,#-4]
	ldr	$t1,[$i2],#-16
	ldr	$t2,[$i2,#16+4]
	ldr	$t3,[$i2,#16+8]
	ldr	$i3,[$i2,#16+12]
	str	$s0,[$tbl],#-16
	str	$s1,[$tbl,#16+4]
	str	$s2,[$tbl,#16+8]
	str	$s3,[$tbl,#16+12]
	str	$t1,[$key],#16
	str	$t2,[$key,#-12]
	str	$t3,[$key,#-8]
	str	$i3,[$key,#-4]
	teq	$i1,$i2
	bne	.Linv

	ldr	$s0,[$i1]
	ldr	$s1,[$i1,#4]
	ldr	$s2,[$i1,#8]
	ldr	$s3,[$i1,#12]
	str	$s0,[$key]
	str	$s1,[$key,#4]
	str	$s2,[$key,#8]
	str	$s3,[$key,#12]
	sub	$key,$key,$rounds,lsl#3
___
$mask80=$i1;
$mask1b=$i2;
$mask7f=$i3;
$code.=<<___;
	ldr	$s0,[$key,#16]!		@ prefetch tp1
	mov	$mask80,#0x80
	mov	$mask1b,#0x1b
	orr	$mask80,$mask80,#0x8000
	orr	$mask1b,$mask1b,#0x1b00
	orr	$mask80,$mask80,$mask80,lsl#16
	orr	$mask1b,$mask1b,$mask1b,lsl#16
	sub	$rounds,$rounds,#1
	mvn	$mask7f,$mask80
	mov	$rounds,$rounds,lsl#2	@ (rounds-1)*4

.Lmix:	and	$t1,$s0,$mask80
	and	$s1,$s0,$mask7f
	sub	$t1,$t1,$t1,lsr#7
	and	$t1,$t1,$mask1b
	eor	$s1,$t1,$s1,lsl#1	@ tp2

	and	$t1,$s1,$mask80
	and	$s2,$s1,$mask7f
	sub	$t1,$t1,$t1,lsr#7
	and	$t1,$t1,$mask1b
	eor	$s2,$t1,$s2,lsl#1	@ tp4

	and	$t1,$s2,$mask80
	and	$s3,$s2,$mask7f
	sub	$t1,$t1,$t1,lsr#7
	and	$t1,$t1,$mask1b
	eor	$s3,$t1,$s3,lsl#1	@ tp8

	eor	$t1,$s1,$s2
	eor	$t2,$s0,$s3		@ tp9
	eor	$t1,$t1,$s3		@ tpe
	eor	$t1,$t1,$s1,ror#24
	eor	$t1,$t1,$t2,ror#24	@ ^= ROTATE(tpb=tp9^tp2,8)
	eor	$t1,$t1,$s2,ror#16
	eor	$t1,$t1,$t2,ror#16	@ ^= ROTATE(tpd=tp9^tp4,16)
	eor	$t1,$t1,$t2,ror#8	@ ^= ROTATE(tp9,24)

	ldr	$s0,[$key,#4]		@ prefetch tp1
	str	$t1,[$key],#4
	subs	$rounds,$rounds,#1
	bne	.Lmix

	mov	r0,#0
#if __ARM_ARCH__>=5
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia   sp!,{r4-r12,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	AES_set_enc2dec_key,.-AES_set_enc2dec_key

.type	AES_Td,%object
.align	5
AES_Td:
.word	0x51f4a750, 0x7e416553, 0x1a17a4c3, 0x3a275e96
.word	0x3bab6bcb, 0x1f9d45f1, 0xacfa58ab, 0x4be30393
.word	0x2030fa55, 0xad766df6, 0x88cc7691, 0xf5024c25
.word	0x4fe5d7fc, 0xc52acbd7, 0x26354480, 0xb562a38f
.word	0xdeb15a49, 0x25ba1b67, 0x45ea0e98, 0x5dfec0e1
.word	0xc32f7502, 0x814cf012, 0x8d4697a3, 0x6bd3f9c6
.word	0x038f5fe7, 0x15929c95, 0xbf6d7aeb, 0x955259da
.word	0xd4be832d, 0x587421d3, 0x49e06929, 0x8ec9c844
.word	0x75c2896a, 0xf48e7978, 0x99583e6b, 0x27b971dd
.word	0xbee14fb6, 0xf088ad17, 0xc920ac66, 0x7dce3ab4
.word	0x63df4a18, 0xe51a3182, 0x97513360, 0x62537f45
.word	0xb16477e0, 0xbb6bae84, 0xfe81a01c, 0xf9082b94
.word	0x70486858, 0x8f45fd19, 0x94de6c87, 0x527bf8b7
.word	0xab73d323, 0x724b02e2, 0xe31f8f57, 0x6655ab2a
.word	0xb2eb2807, 0x2fb5c203, 0x86c57b9a, 0xd33708a5
.word	0x302887f2, 0x23bfa5b2, 0x02036aba, 0xed16825c
.word	0x8acf1c2b, 0xa779b492, 0xf307f2f0, 0x4e69e2a1
.word	0x65daf4cd, 0x0605bed5, 0xd134621f, 0xc4a6fe8a
.word	0x342e539d, 0xa2f355a0, 0x058ae132, 0xa4f6eb75
.word	0x0b83ec39, 0x4060efaa, 0x5e719f06, 0xbd6e1051
.word	0x3e218af9, 0x96dd063d, 0xdd3e05ae, 0x4de6bd46
.word	0x91548db5, 0x71c45d05, 0x0406d46f, 0x605015ff
.word	0x1998fb24, 0xd6bde997, 0x894043cc, 0x67d99e77
.word	0xb0e842bd, 0x07898b88, 0xe7195b38, 0x79c8eedb
.word	0xa17c0a47, 0x7c420fe9, 0xf8841ec9, 0x00000000
.word	0x09808683, 0x322bed48, 0x1e1170ac, 0x6c5a724e
.word	0xfd0efffb, 0x0f853856, 0x3daed51e, 0x362d3927
.word	0x0a0fd964, 0x685ca621, 0x9b5b54d1, 0x24362e3a
.word	0x0c0a67b1, 0x9357e70f, 0xb4ee96d2, 0x1b9b919e
.word	0x80c0c54f, 0x61dc20a2, 0x5a774b69, 0x1c121a16
.word	0xe293ba0a, 0xc0a02ae5, 0x3c22e043, 0x121b171d
.word	0x0e090d0b, 0xf28bc7ad, 0x2db6a8b9, 0x141ea9c8
.word	0x57f11985, 0xaf75074c, 0xee99ddbb, 0xa37f60fd
.word	0xf701269f, 0x5c72f5bc, 0x44663bc5, 0x5bfb7e34
.word	0x8b432976, 0xcb23c6dc, 0xb6edfc68, 0xb8e4f163
.word	0xd731dcca, 0x42638510, 0x13972240, 0x84c61120
.word	0x854a247d, 0xd2bb3df8, 0xaef93211, 0xc729a16d
.word	0x1d9e2f4b, 0xdcb230f3, 0x0d8652ec, 0x77c1e3d0
.word	0x2bb3166c, 0xa970b999, 0x119448fa, 0x47e96422
.word	0xa8fc8cc4, 0xa0f03f1a, 0x567d2cd8, 0x223390ef
.word	0x87494ec7, 0xd938d1c1, 0x8ccaa2fe, 0x98d40b36
.word	0xa6f581cf, 0xa57ade28, 0xdab78e26, 0x3fadbfa4
.word	0x2c3a9de4, 0x5078920d, 0x6a5fcc9b, 0x547e4662
.word	0xf68d13c2, 0x90d8b8e8, 0x2e39f75e, 0x82c3aff5
.word	0x9f5d80be, 0x69d0937c, 0x6fd52da9, 0xcf2512b3
.word	0xc8ac993b, 0x10187da7, 0xe89c636e, 0xdb3bbb7b
.word	0xcd267809, 0x6e5918f4, 0xec9ab701, 0x834f9aa8
.word	0xe6956e65, 0xaaffe67e, 0x21bccf08, 0xef15e8e6
.word	0xbae79bd9, 0x4a6f36ce, 0xea9f09d4, 0x29b07cd6
.word	0x31a4b2af, 0x2a3f2331, 0xc6a59430, 0x35a266c0
.word	0x744ebc37, 0xfc82caa6, 0xe090d0b0, 0x33a7d815
.word	0xf104984a, 0x41ecdaf7, 0x7fcd500e, 0x1791f62f
.word	0x764dd68d, 0x43efb04d, 0xccaa4d54, 0xe49604df
.word	0x9ed1b5e3, 0x4c6a881b, 0xc12c1fb8, 0x4665517f
.word	0x9d5eea04, 0x018c355d, 0xfa877473, 0xfb0b412e
.word	0xb3671d5a, 0x92dbd252, 0xe9105633, 0x6dd64713
.word	0x9ad7618c, 0x37a10c7a, 0x59f8148e, 0xeb133c89
.word	0xcea927ee, 0xb761c935, 0xe11ce5ed, 0x7a47b13c
.word	0x9cd2df59, 0x55f2733f, 0x1814ce79, 0x73c737bf
.word	0x53f7cdea, 0x5ffdaa5b, 0xdf3d6f14, 0x7844db86
.word	0xcaaff381, 0xb968c43e, 0x3824342c, 0xc2a3405f
.word	0x161dc372, 0xbce2250c, 0x283c498b, 0xff0d9541
.word	0x39a80171, 0x080cb3de, 0xd8b4e49c, 0x6456c190
.word	0x7bcb8461, 0xd532b670, 0x486c5c74, 0xd0b85742
@ Td4[256]
.byte	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38
.byte	0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb
.byte	0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87
.byte	0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb
.byte	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d
.byte	0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e
.byte	0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2
.byte	0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25
.byte	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16
.byte	0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92
.byte	0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda
.byte	0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84
.byte	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a
.byte	0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06
.byte	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02
.byte	0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b
.byte	0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea
.byte	0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73
.byte	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85
.byte	0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e
.byte	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89
.byte	0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b
.byte	0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20
.byte	0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4
.byte	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31
.byte	0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f
.byte	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d
.byte	0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef
.byte	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0
.byte	0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61
.byte	0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26
.byte	0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
.size	AES_Td,.-AES_Td

@ void AES_decrypt(const unsigned char *in, unsigned char *out,
@ 		 const AES_KEY *key) {
.global AES_decrypt
.type   AES_decrypt,%function
.align	5
AES_decrypt:
#ifndef	__thumb2__
	sub	r3,pc,#8		@ AES_decrypt
#else
	adr	r3,.
#endif
	stmdb   sp!,{r1,r4-r12,lr}
#if defined(__thumb2__) || defined(__APPLE__)
	adr	$tbl,AES_Td
#else
	sub	$tbl,r3,#AES_decrypt-AES_Td	@ Td
#endif
	mov	$rounds,r0		@ inp
	mov	$key,r2
#if __ARM_ARCH__<7
	ldrb	$s0,[$rounds,#3]	@ load input data in endian-neutral
	ldrb	$t1,[$rounds,#2]	@ manner...
	ldrb	$t2,[$rounds,#1]
	ldrb	$t3,[$rounds,#0]
	orr	$s0,$s0,$t1,lsl#8
	ldrb	$s1,[$rounds,#7]
	orr	$s0,$s0,$t2,lsl#16
	ldrb	$t1,[$rounds,#6]
	orr	$s0,$s0,$t3,lsl#24
	ldrb	$t2,[$rounds,#5]
	ldrb	$t3,[$rounds,#4]
	orr	$s1,$s1,$t1,lsl#8
	ldrb	$s2,[$rounds,#11]
	orr	$s1,$s1,$t2,lsl#16
	ldrb	$t1,[$rounds,#10]
	orr	$s1,$s1,$t3,lsl#24
	ldrb	$t2,[$rounds,#9]
	ldrb	$t3,[$rounds,#8]
	orr	$s2,$s2,$t1,lsl#8
	ldrb	$s3,[$rounds,#15]
	orr	$s2,$s2,$t2,lsl#16
	ldrb	$t1,[$rounds,#14]
	orr	$s2,$s2,$t3,lsl#24
	ldrb	$t2,[$rounds,#13]
	ldrb	$t3,[$rounds,#12]
	orr	$s3,$s3,$t1,lsl#8
	orr	$s3,$s3,$t2,lsl#16
	orr	$s3,$s3,$t3,lsl#24
#else
	ldr	$s0,[$rounds,#0]
	ldr	$s1,[$rounds,#4]
	ldr	$s2,[$rounds,#8]
	ldr	$s3,[$rounds,#12]
#ifdef __ARMEL__
	rev	$s0,$s0
	rev	$s1,$s1
	rev	$s2,$s2
	rev	$s3,$s3
#endif
#endif
	bl	_armv4_AES_decrypt

	ldr	$rounds,[sp],#4		@ pop out
#if __ARM_ARCH__>=7
#ifdef __ARMEL__
	rev	$s0,$s0
	rev	$s1,$s1
	rev	$s2,$s2
	rev	$s3,$s3
#endif
	str	$s0,[$rounds,#0]
	str	$s1,[$rounds,#4]
	str	$s2,[$rounds,#8]
	str	$s3,[$rounds,#12]
#else
	mov	$t1,$s0,lsr#24		@ write output in endian-neutral
	mov	$t2,$s0,lsr#16		@ manner...
	mov	$t3,$s0,lsr#8
	strb	$t1,[$rounds,#0]
	strb	$t2,[$rounds,#1]
	mov	$t1,$s1,lsr#24
	strb	$t3,[$rounds,#2]
	mov	$t2,$s1,lsr#16
	strb	$s0,[$rounds,#3]
	mov	$t3,$s1,lsr#8
	strb	$t1,[$rounds,#4]
	strb	$t2,[$rounds,#5]
	mov	$t1,$s2,lsr#24
	strb	$t3,[$rounds,#6]
	mov	$t2,$s2,lsr#16
	strb	$s1,[$rounds,#7]
	mov	$t3,$s2,lsr#8
	strb	$t1,[$rounds,#8]
	strb	$t2,[$rounds,#9]
	mov	$t1,$s3,lsr#24
	strb	$t3,[$rounds,#10]
	mov	$t2,$s3,lsr#16
	strb	$s2,[$rounds,#11]
	mov	$t3,$s3,lsr#8
	strb	$t1,[$rounds,#12]
	strb	$t2,[$rounds,#13]
	strb	$t3,[$rounds,#14]
	strb	$s3,[$rounds,#15]
#endif
#if __ARM_ARCH__>=5
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia   sp!,{r4-r12,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	AES_decrypt,.-AES_decrypt

.type   _armv4_AES_decrypt,%function
.align	2
_armv4_AES_decrypt:
	str	lr,[sp,#-4]!		@ push lr
	ldmia	$key!,{$t1-$i1}
	eor	$s0,$s0,$t1
	ldr	$rounds,[$key,#240-16]
	eor	$s1,$s1,$t2
	eor	$s2,$s2,$t3
	eor	$s3,$s3,$i1
	sub	$rounds,$rounds,#1
	mov	lr,#255

	and	$i1,lr,$s0,lsr#16
	and	$i2,lr,$s0,lsr#8
	and	$i3,lr,$s0
	mov	$s0,$s0,lsr#24
.Ldec_loop:
	ldr	$t1,[$tbl,$i1,lsl#2]	@ Td1[s0>>16]
	and	$i1,lr,$s1		@ i0
	ldr	$t2,[$tbl,$i2,lsl#2]	@ Td2[s0>>8]
	and	$i2,lr,$s1,lsr#16
	ldr	$t3,[$tbl,$i3,lsl#2]	@ Td3[s0>>0]
	and	$i3,lr,$s1,lsr#8
	ldr	$s0,[$tbl,$s0,lsl#2]	@ Td0[s0>>24]
	mov	$s1,$s1,lsr#24

	ldr	$i1,[$tbl,$i1,lsl#2]	@ Td3[s1>>0]
	ldr	$i2,[$tbl,$i2,lsl#2]	@ Td1[s1>>16]
	ldr	$i3,[$tbl,$i3,lsl#2]	@ Td2[s1>>8]
	eor	$s0,$s0,$i1,ror#24
	ldr	$s1,[$tbl,$s1,lsl#2]	@ Td0[s1>>24]
	and	$i1,lr,$s2,lsr#8	@ i0
	eor	$t2,$i2,$t2,ror#8
	and	$i2,lr,$s2		@ i1
	eor	$t3,$i3,$t3,ror#8
	and	$i3,lr,$s2,lsr#16
	ldr	$i1,[$tbl,$i1,lsl#2]	@ Td2[s2>>8]
	eor	$s1,$s1,$t1,ror#8
	ldr	$i2,[$tbl,$i2,lsl#2]	@ Td3[s2>>0]
	mov	$s2,$s2,lsr#24

	ldr	$i3,[$tbl,$i3,lsl#2]	@ Td1[s2>>16]
	eor	$s0,$s0,$i1,ror#16
	ldr	$s2,[$tbl,$s2,lsl#2]	@ Td0[s2>>24]
	and	$i1,lr,$s3,lsr#16	@ i0
	eor	$s1,$s1,$i2,ror#24
	and	$i2,lr,$s3,lsr#8	@ i1
	eor	$t3,$i3,$t3,ror#8
	and	$i3,lr,$s3		@ i2
	ldr	$i1,[$tbl,$i1,lsl#2]	@ Td1[s3>>16]
	eor	$s2,$s2,$t2,ror#8
	ldr	$i2,[$tbl,$i2,lsl#2]	@ Td2[s3>>8]
	mov	$s3,$s3,lsr#24

	ldr	$i3,[$tbl,$i3,lsl#2]	@ Td3[s3>>0]
	eor	$s0,$s0,$i1,ror#8
	ldr	$i1,[$key],#16
	eor	$s1,$s1,$i2,ror#16
	ldr	$s3,[$tbl,$s3,lsl#2]	@ Td0[s3>>24]
	eor	$s2,$s2,$i3,ror#24

	ldr	$t1,[$key,#-12]
	eor	$s0,$s0,$i1
	ldr	$t2,[$key,#-8]
	eor	$s3,$s3,$t3,ror#8
	ldr	$t3,[$key,#-4]
	and	$i1,lr,$s0,lsr#16
	eor	$s1,$s1,$t1
	and	$i2,lr,$s0,lsr#8
	eor	$s2,$s2,$t2
	and	$i3,lr,$s0
	eor	$s3,$s3,$t3
	mov	$s0,$s0,lsr#24

	subs	$rounds,$rounds,#1
	bne	.Ldec_loop

	add	$tbl,$tbl,#1024

	ldr	$t2,[$tbl,#0]		@ prefetch Td4
	ldr	$t3,[$tbl,#32]
	ldr	$t1,[$tbl,#64]
	ldr	$t2,[$tbl,#96]
	ldr	$t3,[$tbl,#128]
	ldr	$t1,[$tbl,#160]
	ldr	$t2,[$tbl,#192]
	ldr	$t3,[$tbl,#224]

	ldrb	$s0,[$tbl,$s0]		@ Td4[s0>>24]
	ldrb	$t1,[$tbl,$i1]		@ Td4[s0>>16]
	and	$i1,lr,$s1		@ i0
	ldrb	$t2,[$tbl,$i2]		@ Td4[s0>>8]
	and	$i2,lr,$s1,lsr#16
	ldrb	$t3,[$tbl,$i3]		@ Td4[s0>>0]
	and	$i3,lr,$s1,lsr#8

	add	$s1,$tbl,$s1,lsr#24
	ldrb	$i1,[$tbl,$i1]		@ Td4[s1>>0]
	ldrb	$s1,[$s1]		@ Td4[s1>>24]
	ldrb	$i2,[$tbl,$i2]		@ Td4[s1>>16]
	eor	$s0,$i1,$s0,lsl#24
	ldrb	$i3,[$tbl,$i3]		@ Td4[s1>>8]
	eor	$s1,$t1,$s1,lsl#8
	and	$i1,lr,$s2,lsr#8	@ i0
	eor	$t2,$t2,$i2,lsl#8
	and	$i2,lr,$s2		@ i1
	ldrb	$i1,[$tbl,$i1]		@ Td4[s2>>8]
	eor	$t3,$t3,$i3,lsl#8
	ldrb	$i2,[$tbl,$i2]		@ Td4[s2>>0]
	and	$i3,lr,$s2,lsr#16

	add	$s2,$tbl,$s2,lsr#24
	ldrb	$s2,[$s2]		@ Td4[s2>>24]
	eor	$s0,$s0,$i1,lsl#8
	ldrb	$i3,[$tbl,$i3]		@ Td4[s2>>16]
	eor	$s1,$i2,$s1,lsl#16
	and	$i1,lr,$s3,lsr#16	@ i0
	eor	$s2,$t2,$s2,lsl#16
	and	$i2,lr,$s3,lsr#8	@ i1
	ldrb	$i1,[$tbl,$i1]		@ Td4[s3>>16]
	eor	$t3,$t3,$i3,lsl#16
	ldrb	$i2,[$tbl,$i2]		@ Td4[s3>>8]
	and	$i3,lr,$s3		@ i2

	add	$s3,$tbl,$s3,lsr#24
	ldrb	$i3,[$tbl,$i3]		@ Td4[s3>>0]
	ldrb	$s3,[$s3]		@ Td4[s3>>24]
	eor	$s0,$s0,$i1,lsl#16
	ldr	$i1,[$key,#0]
	eor	$s1,$s1,$i2,lsl#8
	ldr	$t1,[$key,#4]
	eor	$s2,$i3,$s2,lsl#8
	ldr	$t2,[$key,#8]
	eor	$s3,$t3,$s3,lsl#24
	ldr	$t3,[$key,#12]

	eor	$s0,$s0,$i1
	eor	$s1,$s1,$t1
	eor	$s2,$s2,$t2
	eor	$s3,$s3,$t3

	sub	$tbl,$tbl,#1024
	ldr	pc,[sp],#4		@ pop and return
.size	_armv4_AES_decrypt,.-_armv4_AES_decrypt
.asciz	"AES for ARMv4, CRYPTOGAMS by <appro\@openssl.org>"
.align	2
___

$code =~ s/\bbx\s+lr\b/.word\t0xe12fff1e/gm;	# make it possible to compile with -march=armv4
$code =~ s/\bret\b/bx\tlr/gm;

open SELF,$0;
while(<SELF>) {
	next if (/^#!/);
	last if (!s/^#/@/ and !/^$/);
	print;
}
close SELF;

print $code;
close STDOUT;	# enforce flush

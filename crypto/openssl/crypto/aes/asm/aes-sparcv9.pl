#! /usr/bin/env perl
# Copyright 2005-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. Rights for redistribution and usage in source and binary
# forms are granted according to the OpenSSL license.
# ====================================================================
#
# Version 1.1
#
# The major reason for undertaken effort was to mitigate the hazard of
# cache-timing attack. This is [currently and initially!] addressed in
# two ways. 1. S-boxes are compressed from 5KB to 2KB+256B size each.
# 2. References to them are scheduled for L2 cache latency, meaning
# that the tables don't have to reside in L1 cache. Once again, this
# is an initial draft and one should expect more countermeasures to
# be implemented...
#
# Version 1.1 prefetches T[ed]4 in order to mitigate attack on last
# round.
#
# Even though performance was not the primary goal [on the contrary,
# extra shifts "induced" by compressed S-box and longer loop epilogue
# "induced" by scheduling for L2 have negative effect on performance],
# the code turned out to run in ~23 cycles per processed byte en-/
# decrypted with 128-bit key. This is pretty good result for code
# with mentioned qualities and UltraSPARC core. Compared to Sun C
# generated code my encrypt procedure runs just few percents faster,
# while decrypt one - whole 50% faster [yes, Sun C failed to generate
# optimal decrypt procedure]. Compared to GNU C generated code both
# procedures are more than 60% faster:-)

$output = pop;
open STDOUT,">$output";

$frame="STACK_FRAME";
$bias="STACK_BIAS";
$locals=16;

$acc0="%l0";
$acc1="%o0";
$acc2="%o1";
$acc3="%o2";

$acc4="%l1";
$acc5="%o3";
$acc6="%o4";
$acc7="%o5";

$acc8="%l2";
$acc9="%o7";
$acc10="%g1";
$acc11="%g2";

$acc12="%l3";
$acc13="%g3";
$acc14="%g4";
$acc15="%g5";

$t0="%l4";
$t1="%l5";
$t2="%l6";
$t3="%l7";

$s0="%i0";
$s1="%i1";
$s2="%i2";
$s3="%i3";
$tbl="%i4";
$key="%i5";
$rounds="%i7";	# aliases with return address, which is off-loaded to stack

sub _data_word()
{ my $i;
    while(defined($i=shift)) { $code.=sprintf"\t.long\t0x%08x,0x%08x\n",$i,$i; }
}

$code.=<<___;
#include "sparc_arch.h"

#ifdef  __arch64__
.register	%g2,#scratch
.register	%g3,#scratch
#endif
.section	".text",#alloc,#execinstr

.align	256
AES_Te:
___
&_data_word(
	0xc66363a5, 0xf87c7c84, 0xee777799, 0xf67b7b8d,
	0xfff2f20d, 0xd66b6bbd, 0xde6f6fb1, 0x91c5c554,
	0x60303050, 0x02010103, 0xce6767a9, 0x562b2b7d,
	0xe7fefe19, 0xb5d7d762, 0x4dababe6, 0xec76769a,
	0x8fcaca45, 0x1f82829d, 0x89c9c940, 0xfa7d7d87,
	0xeffafa15, 0xb25959eb, 0x8e4747c9, 0xfbf0f00b,
	0x41adadec, 0xb3d4d467, 0x5fa2a2fd, 0x45afafea,
	0x239c9cbf, 0x53a4a4f7, 0xe4727296, 0x9bc0c05b,
	0x75b7b7c2, 0xe1fdfd1c, 0x3d9393ae, 0x4c26266a,
	0x6c36365a, 0x7e3f3f41, 0xf5f7f702, 0x83cccc4f,
	0x6834345c, 0x51a5a5f4, 0xd1e5e534, 0xf9f1f108,
	0xe2717193, 0xabd8d873, 0x62313153, 0x2a15153f,
	0x0804040c, 0x95c7c752, 0x46232365, 0x9dc3c35e,
	0x30181828, 0x379696a1, 0x0a05050f, 0x2f9a9ab5,
	0x0e070709, 0x24121236, 0x1b80809b, 0xdfe2e23d,
	0xcdebeb26, 0x4e272769, 0x7fb2b2cd, 0xea75759f,
	0x1209091b, 0x1d83839e, 0x582c2c74, 0x341a1a2e,
	0x361b1b2d, 0xdc6e6eb2, 0xb45a5aee, 0x5ba0a0fb,
	0xa45252f6, 0x763b3b4d, 0xb7d6d661, 0x7db3b3ce,
	0x5229297b, 0xdde3e33e, 0x5e2f2f71, 0x13848497,
	0xa65353f5, 0xb9d1d168, 0x00000000, 0xc1eded2c,
	0x40202060, 0xe3fcfc1f, 0x79b1b1c8, 0xb65b5bed,
	0xd46a6abe, 0x8dcbcb46, 0x67bebed9, 0x7239394b,
	0x944a4ade, 0x984c4cd4, 0xb05858e8, 0x85cfcf4a,
	0xbbd0d06b, 0xc5efef2a, 0x4faaaae5, 0xedfbfb16,
	0x864343c5, 0x9a4d4dd7, 0x66333355, 0x11858594,
	0x8a4545cf, 0xe9f9f910, 0x04020206, 0xfe7f7f81,
	0xa05050f0, 0x783c3c44, 0x259f9fba, 0x4ba8a8e3,
	0xa25151f3, 0x5da3a3fe, 0x804040c0, 0x058f8f8a,
	0x3f9292ad, 0x219d9dbc, 0x70383848, 0xf1f5f504,
	0x63bcbcdf, 0x77b6b6c1, 0xafdada75, 0x42212163,
	0x20101030, 0xe5ffff1a, 0xfdf3f30e, 0xbfd2d26d,
	0x81cdcd4c, 0x180c0c14, 0x26131335, 0xc3ecec2f,
	0xbe5f5fe1, 0x359797a2, 0x884444cc, 0x2e171739,
	0x93c4c457, 0x55a7a7f2, 0xfc7e7e82, 0x7a3d3d47,
	0xc86464ac, 0xba5d5de7, 0x3219192b, 0xe6737395,
	0xc06060a0, 0x19818198, 0x9e4f4fd1, 0xa3dcdc7f,
	0x44222266, 0x542a2a7e, 0x3b9090ab, 0x0b888883,
	0x8c4646ca, 0xc7eeee29, 0x6bb8b8d3, 0x2814143c,
	0xa7dede79, 0xbc5e5ee2, 0x160b0b1d, 0xaddbdb76,
	0xdbe0e03b, 0x64323256, 0x743a3a4e, 0x140a0a1e,
	0x924949db, 0x0c06060a, 0x4824246c, 0xb85c5ce4,
	0x9fc2c25d, 0xbdd3d36e, 0x43acacef, 0xc46262a6,
	0x399191a8, 0x319595a4, 0xd3e4e437, 0xf279798b,
	0xd5e7e732, 0x8bc8c843, 0x6e373759, 0xda6d6db7,
	0x018d8d8c, 0xb1d5d564, 0x9c4e4ed2, 0x49a9a9e0,
	0xd86c6cb4, 0xac5656fa, 0xf3f4f407, 0xcfeaea25,
	0xca6565af, 0xf47a7a8e, 0x47aeaee9, 0x10080818,
	0x6fbabad5, 0xf0787888, 0x4a25256f, 0x5c2e2e72,
	0x381c1c24, 0x57a6a6f1, 0x73b4b4c7, 0x97c6c651,
	0xcbe8e823, 0xa1dddd7c, 0xe874749c, 0x3e1f1f21,
	0x964b4bdd, 0x61bdbddc, 0x0d8b8b86, 0x0f8a8a85,
	0xe0707090, 0x7c3e3e42, 0x71b5b5c4, 0xcc6666aa,
	0x904848d8, 0x06030305, 0xf7f6f601, 0x1c0e0e12,
	0xc26161a3, 0x6a35355f, 0xae5757f9, 0x69b9b9d0,
	0x17868691, 0x99c1c158, 0x3a1d1d27, 0x279e9eb9,
	0xd9e1e138, 0xebf8f813, 0x2b9898b3, 0x22111133,
	0xd26969bb, 0xa9d9d970, 0x078e8e89, 0x339494a7,
	0x2d9b9bb6, 0x3c1e1e22, 0x15878792, 0xc9e9e920,
	0x87cece49, 0xaa5555ff, 0x50282878, 0xa5dfdf7a,
	0x038c8c8f, 0x59a1a1f8, 0x09898980, 0x1a0d0d17,
	0x65bfbfda, 0xd7e6e631, 0x844242c6, 0xd06868b8,
	0x824141c3, 0x299999b0, 0x5a2d2d77, 0x1e0f0f11,
	0x7bb0b0cb, 0xa85454fc, 0x6dbbbbd6, 0x2c16163a);
$code.=<<___;
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
.type	AES_Te,#object
.size	AES_Te,(.-AES_Te)

.align	64
.skip	16
_sparcv9_AES_encrypt:
	save	%sp,-$frame-$locals,%sp
	stx	%i7,[%sp+$bias+$frame+0]	! off-load return address
	ld	[$key+240],$rounds
	ld	[$key+0],$t0
	ld	[$key+4],$t1			!
	ld	[$key+8],$t2
	srl	$rounds,1,$rounds
	xor	$t0,$s0,$s0
	ld	[$key+12],$t3
	srl	$s0,21,$acc0
	xor	$t1,$s1,$s1
	ld	[$key+16],$t0
	srl	$s1,13,$acc1			!
	xor	$t2,$s2,$s2
	ld	[$key+20],$t1
	xor	$t3,$s3,$s3
	ld	[$key+24],$t2
	and	$acc0,2040,$acc0
	ld	[$key+28],$t3
	nop
.Lenc_loop:
	srl	$s2,5,$acc2			!
	and	$acc1,2040,$acc1
	ldx	[$tbl+$acc0],$acc0
	sll	$s3,3,$acc3
	and	$acc2,2040,$acc2
	ldx	[$tbl+$acc1],$acc1
	srl	$s1,21,$acc4
	and	$acc3,2040,$acc3
	ldx	[$tbl+$acc2],$acc2		!
	srl	$s2,13,$acc5
	and	$acc4,2040,$acc4
	ldx	[$tbl+$acc3],$acc3
	srl	$s3,5,$acc6
	and	$acc5,2040,$acc5
	ldx	[$tbl+$acc4],$acc4
	fmovs	%f0,%f0
	sll	$s0,3,$acc7			!
	and	$acc6,2040,$acc6
	ldx	[$tbl+$acc5],$acc5
	srl	$s2,21,$acc8
	and	$acc7,2040,$acc7
	ldx	[$tbl+$acc6],$acc6
	srl	$s3,13,$acc9
	and	$acc8,2040,$acc8
	ldx	[$tbl+$acc7],$acc7		!
	srl	$s0,5,$acc10
	and	$acc9,2040,$acc9
	ldx	[$tbl+$acc8],$acc8
	sll	$s1,3,$acc11
	and	$acc10,2040,$acc10
	ldx	[$tbl+$acc9],$acc9
	fmovs	%f0,%f0
	srl	$s3,21,$acc12			!
	and	$acc11,2040,$acc11
	ldx	[$tbl+$acc10],$acc10
	srl	$s0,13,$acc13
	and	$acc12,2040,$acc12
	ldx	[$tbl+$acc11],$acc11
	srl	$s1,5,$acc14
	and	$acc13,2040,$acc13
	ldx	[$tbl+$acc12],$acc12		!
	sll	$s2,3,$acc15
	and	$acc14,2040,$acc14
	ldx	[$tbl+$acc13],$acc13
	and	$acc15,2040,$acc15
	add	$key,32,$key
	ldx	[$tbl+$acc14],$acc14
	fmovs	%f0,%f0
	subcc	$rounds,1,$rounds		!
	ldx	[$tbl+$acc15],$acc15
	bz,a,pn	%icc,.Lenc_last
	add	$tbl,2048,$rounds

		srlx	$acc1,8,$acc1
		xor	$acc0,$t0,$t0
	ld	[$key+0],$s0
	fmovs	%f0,%f0
		srlx	$acc2,16,$acc2		!
		xor	$acc1,$t0,$t0
	ld	[$key+4],$s1
		srlx	$acc3,24,$acc3
		xor	$acc2,$t0,$t0
	ld	[$key+8],$s2
		srlx	$acc5,8,$acc5
		xor	$acc3,$t0,$t0
	ld	[$key+12],$s3			!
		srlx	$acc6,16,$acc6
		xor	$acc4,$t1,$t1
	fmovs	%f0,%f0
		srlx	$acc7,24,$acc7
		xor	$acc5,$t1,$t1
		srlx	$acc9,8,$acc9
		xor	$acc6,$t1,$t1
		srlx	$acc10,16,$acc10	!
		xor	$acc7,$t1,$t1
		srlx	$acc11,24,$acc11
		xor	$acc8,$t2,$t2
		srlx	$acc13,8,$acc13
		xor	$acc9,$t2,$t2
		srlx	$acc14,16,$acc14
		xor	$acc10,$t2,$t2
		srlx	$acc15,24,$acc15	!
		xor	$acc11,$t2,$t2
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$t3,$t3
	srl	$t0,21,$acc0
		xor	$acc14,$t3,$t3
	srl	$t1,13,$acc1
		xor	$acc15,$t3,$t3

	and	$acc0,2040,$acc0		!
	srl	$t2,5,$acc2
	and	$acc1,2040,$acc1
	ldx	[$tbl+$acc0],$acc0
	sll	$t3,3,$acc3
	and	$acc2,2040,$acc2
	ldx	[$tbl+$acc1],$acc1
	fmovs	%f0,%f0
	srl	$t1,21,$acc4			!
	and	$acc3,2040,$acc3
	ldx	[$tbl+$acc2],$acc2
	srl	$t2,13,$acc5
	and	$acc4,2040,$acc4
	ldx	[$tbl+$acc3],$acc3
	srl	$t3,5,$acc6
	and	$acc5,2040,$acc5
	ldx	[$tbl+$acc4],$acc4		!
	sll	$t0,3,$acc7
	and	$acc6,2040,$acc6
	ldx	[$tbl+$acc5],$acc5
	srl	$t2,21,$acc8
	and	$acc7,2040,$acc7
	ldx	[$tbl+$acc6],$acc6
	fmovs	%f0,%f0
	srl	$t3,13,$acc9			!
	and	$acc8,2040,$acc8
	ldx	[$tbl+$acc7],$acc7
	srl	$t0,5,$acc10
	and	$acc9,2040,$acc9
	ldx	[$tbl+$acc8],$acc8
	sll	$t1,3,$acc11
	and	$acc10,2040,$acc10
	ldx	[$tbl+$acc9],$acc9		!
	srl	$t3,21,$acc12
	and	$acc11,2040,$acc11
	ldx	[$tbl+$acc10],$acc10
	srl	$t0,13,$acc13
	and	$acc12,2040,$acc12
	ldx	[$tbl+$acc11],$acc11
	fmovs	%f0,%f0
	srl	$t1,5,$acc14			!
	and	$acc13,2040,$acc13
	ldx	[$tbl+$acc12],$acc12
	sll	$t2,3,$acc15
	and	$acc14,2040,$acc14
	ldx	[$tbl+$acc13],$acc13
		srlx	$acc1,8,$acc1
	and	$acc15,2040,$acc15
	ldx	[$tbl+$acc14],$acc14		!

		srlx	$acc2,16,$acc2
		xor	$acc0,$s0,$s0
	ldx	[$tbl+$acc15],$acc15
		srlx	$acc3,24,$acc3
		xor	$acc1,$s0,$s0
	ld	[$key+16],$t0
	fmovs	%f0,%f0
		srlx	$acc5,8,$acc5		!
		xor	$acc2,$s0,$s0
	ld	[$key+20],$t1
		srlx	$acc6,16,$acc6
		xor	$acc3,$s0,$s0
	ld	[$key+24],$t2
		srlx	$acc7,24,$acc7
		xor	$acc4,$s1,$s1
	ld	[$key+28],$t3			!
		srlx	$acc9,8,$acc9
		xor	$acc5,$s1,$s1
	ldx	[$tbl+2048+0],%g0		! prefetch te4
		srlx	$acc10,16,$acc10
		xor	$acc6,$s1,$s1
	ldx	[$tbl+2048+32],%g0		! prefetch te4
		srlx	$acc11,24,$acc11
		xor	$acc7,$s1,$s1
	ldx	[$tbl+2048+64],%g0		! prefetch te4
		srlx	$acc13,8,$acc13
		xor	$acc8,$s2,$s2
	ldx	[$tbl+2048+96],%g0		! prefetch te4
		srlx	$acc14,16,$acc14	!
		xor	$acc9,$s2,$s2
	ldx	[$tbl+2048+128],%g0		! prefetch te4
		srlx	$acc15,24,$acc15
		xor	$acc10,$s2,$s2
	ldx	[$tbl+2048+160],%g0		! prefetch te4
	srl	$s0,21,$acc0
		xor	$acc11,$s2,$s2
	ldx	[$tbl+2048+192],%g0		! prefetch te4
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$s3,$s3
	ldx	[$tbl+2048+224],%g0		! prefetch te4
	srl	$s1,13,$acc1			!
		xor	$acc14,$s3,$s3
		xor	$acc15,$s3,$s3
	ba	.Lenc_loop
	and	$acc0,2040,$acc0

.align	32
.Lenc_last:
		srlx	$acc1,8,$acc1		!
		xor	$acc0,$t0,$t0
	ld	[$key+0],$s0
		srlx	$acc2,16,$acc2
		xor	$acc1,$t0,$t0
	ld	[$key+4],$s1
		srlx	$acc3,24,$acc3
		xor	$acc2,$t0,$t0
	ld	[$key+8],$s2			!
		srlx	$acc5,8,$acc5
		xor	$acc3,$t0,$t0
	ld	[$key+12],$s3
		srlx	$acc6,16,$acc6
		xor	$acc4,$t1,$t1
		srlx	$acc7,24,$acc7
		xor	$acc5,$t1,$t1
		srlx	$acc9,8,$acc9		!
		xor	$acc6,$t1,$t1
		srlx	$acc10,16,$acc10
		xor	$acc7,$t1,$t1
		srlx	$acc11,24,$acc11
		xor	$acc8,$t2,$t2
		srlx	$acc13,8,$acc13
		xor	$acc9,$t2,$t2
		srlx	$acc14,16,$acc14	!
		xor	$acc10,$t2,$t2
		srlx	$acc15,24,$acc15
		xor	$acc11,$t2,$t2
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$t3,$t3
	srl	$t0,24,$acc0
		xor	$acc14,$t3,$t3
	srl	$t1,16,$acc1			!
		xor	$acc15,$t3,$t3

	srl	$t2,8,$acc2
	and	$acc1,255,$acc1
	ldub	[$rounds+$acc0],$acc0
	srl	$t1,24,$acc4
	and	$acc2,255,$acc2
	ldub	[$rounds+$acc1],$acc1
	srl	$t2,16,$acc5			!
	and	$t3,255,$acc3
	ldub	[$rounds+$acc2],$acc2
	ldub	[$rounds+$acc3],$acc3
	srl	$t3,8,$acc6
	and	$acc5,255,$acc5
	ldub	[$rounds+$acc4],$acc4
	fmovs	%f0,%f0
	srl	$t2,24,$acc8			!
	and	$acc6,255,$acc6
	ldub	[$rounds+$acc5],$acc5
	srl	$t3,16,$acc9
	and	$t0,255,$acc7
	ldub	[$rounds+$acc6],$acc6
	ldub	[$rounds+$acc7],$acc7
	fmovs	%f0,%f0
	srl	$t0,8,$acc10			!
	and	$acc9,255,$acc9
	ldub	[$rounds+$acc8],$acc8
	srl	$t3,24,$acc12
	and	$acc10,255,$acc10
	ldub	[$rounds+$acc9],$acc9
	srl	$t0,16,$acc13
	and	$t1,255,$acc11
	ldub	[$rounds+$acc10],$acc10		!
	srl	$t1,8,$acc14
	and	$acc13,255,$acc13
	ldub	[$rounds+$acc11],$acc11
	ldub	[$rounds+$acc12],$acc12
	and	$acc14,255,$acc14
	ldub	[$rounds+$acc13],$acc13
	and	$t2,255,$acc15
	ldub	[$rounds+$acc14],$acc14		!

		sll	$acc0,24,$acc0
		xor	$acc3,$s0,$s0
	ldub	[$rounds+$acc15],$acc15
		sll	$acc1,16,$acc1
		xor	$acc0,$s0,$s0
	ldx	[%sp+$bias+$frame+0],%i7	! restore return address
	fmovs	%f0,%f0
		sll	$acc2,8,$acc2		!
		xor	$acc1,$s0,$s0
		sll	$acc4,24,$acc4
		xor	$acc2,$s0,$s0
		sll	$acc5,16,$acc5
		xor	$acc7,$s1,$s1
		sll	$acc6,8,$acc6
		xor	$acc4,$s1,$s1
		sll	$acc8,24,$acc8		!
		xor	$acc5,$s1,$s1
		sll	$acc9,16,$acc9
		xor	$acc11,$s2,$s2
		sll	$acc10,8,$acc10
		xor	$acc6,$s1,$s1
		sll	$acc12,24,$acc12
		xor	$acc8,$s2,$s2
		sll	$acc13,16,$acc13	!
		xor	$acc9,$s2,$s2
		sll	$acc14,8,$acc14
		xor	$acc10,$s2,$s2
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$s3,$s3
		xor	$acc14,$s3,$s3
		xor	$acc15,$s3,$s3

	ret
	restore
.type	_sparcv9_AES_encrypt,#function
.size	_sparcv9_AES_encrypt,(.-_sparcv9_AES_encrypt)

.align	32
.globl	AES_encrypt
AES_encrypt:
	or	%o0,%o1,%g1
	andcc	%g1,3,%g0
	bnz,pn	%xcc,.Lunaligned_enc
	save	%sp,-$frame,%sp

	ld	[%i0+0],%o0
	ld	[%i0+4],%o1
	ld	[%i0+8],%o2
	ld	[%i0+12],%o3

1:	call	.+8
	add	%o7,AES_Te-1b,%o4
	call	_sparcv9_AES_encrypt
	mov	%i2,%o5

	st	%o0,[%i1+0]
	st	%o1,[%i1+4]
	st	%o2,[%i1+8]
	st	%o3,[%i1+12]

	ret
	restore

.align	32
.Lunaligned_enc:
	ldub	[%i0+0],%l0
	ldub	[%i0+1],%l1
	ldub	[%i0+2],%l2

	sll	%l0,24,%l0
	ldub	[%i0+3],%l3
	sll	%l1,16,%l1
	ldub	[%i0+4],%l4
	sll	%l2,8,%l2
	or	%l1,%l0,%l0
	ldub	[%i0+5],%l5
	sll	%l4,24,%l4
	or	%l3,%l2,%l2
	ldub	[%i0+6],%l6
	sll	%l5,16,%l5
	or	%l0,%l2,%o0
	ldub	[%i0+7],%l7

	sll	%l6,8,%l6
	or	%l5,%l4,%l4
	ldub	[%i0+8],%l0
	or	%l7,%l6,%l6
	ldub	[%i0+9],%l1
	or	%l4,%l6,%o1
	ldub	[%i0+10],%l2

	sll	%l0,24,%l0
	ldub	[%i0+11],%l3
	sll	%l1,16,%l1
	ldub	[%i0+12],%l4
	sll	%l2,8,%l2
	or	%l1,%l0,%l0
	ldub	[%i0+13],%l5
	sll	%l4,24,%l4
	or	%l3,%l2,%l2
	ldub	[%i0+14],%l6
	sll	%l5,16,%l5
	or	%l0,%l2,%o2
	ldub	[%i0+15],%l7

	sll	%l6,8,%l6
	or	%l5,%l4,%l4
	or	%l7,%l6,%l6
	or	%l4,%l6,%o3

1:	call	.+8
	add	%o7,AES_Te-1b,%o4
	call	_sparcv9_AES_encrypt
	mov	%i2,%o5

	srl	%o0,24,%l0
	srl	%o0,16,%l1
	stb	%l0,[%i1+0]
	srl	%o0,8,%l2
	stb	%l1,[%i1+1]
	stb	%l2,[%i1+2]
	srl	%o1,24,%l4
	stb	%o0,[%i1+3]

	srl	%o1,16,%l5
	stb	%l4,[%i1+4]
	srl	%o1,8,%l6
	stb	%l5,[%i1+5]
	stb	%l6,[%i1+6]
	srl	%o2,24,%l0
	stb	%o1,[%i1+7]

	srl	%o2,16,%l1
	stb	%l0,[%i1+8]
	srl	%o2,8,%l2
	stb	%l1,[%i1+9]
	stb	%l2,[%i1+10]
	srl	%o3,24,%l4
	stb	%o2,[%i1+11]

	srl	%o3,16,%l5
	stb	%l4,[%i1+12]
	srl	%o3,8,%l6
	stb	%l5,[%i1+13]
	stb	%l6,[%i1+14]
	stb	%o3,[%i1+15]

	ret
	restore
.type	AES_encrypt,#function
.size	AES_encrypt,(.-AES_encrypt)

___

$code.=<<___;
.align	256
AES_Td:
___
&_data_word(
	0x51f4a750, 0x7e416553, 0x1a17a4c3, 0x3a275e96,
	0x3bab6bcb, 0x1f9d45f1, 0xacfa58ab, 0x4be30393,
	0x2030fa55, 0xad766df6, 0x88cc7691, 0xf5024c25,
	0x4fe5d7fc, 0xc52acbd7, 0x26354480, 0xb562a38f,
	0xdeb15a49, 0x25ba1b67, 0x45ea0e98, 0x5dfec0e1,
	0xc32f7502, 0x814cf012, 0x8d4697a3, 0x6bd3f9c6,
	0x038f5fe7, 0x15929c95, 0xbf6d7aeb, 0x955259da,
	0xd4be832d, 0x587421d3, 0x49e06929, 0x8ec9c844,
	0x75c2896a, 0xf48e7978, 0x99583e6b, 0x27b971dd,
	0xbee14fb6, 0xf088ad17, 0xc920ac66, 0x7dce3ab4,
	0x63df4a18, 0xe51a3182, 0x97513360, 0x62537f45,
	0xb16477e0, 0xbb6bae84, 0xfe81a01c, 0xf9082b94,
	0x70486858, 0x8f45fd19, 0x94de6c87, 0x527bf8b7,
	0xab73d323, 0x724b02e2, 0xe31f8f57, 0x6655ab2a,
	0xb2eb2807, 0x2fb5c203, 0x86c57b9a, 0xd33708a5,
	0x302887f2, 0x23bfa5b2, 0x02036aba, 0xed16825c,
	0x8acf1c2b, 0xa779b492, 0xf307f2f0, 0x4e69e2a1,
	0x65daf4cd, 0x0605bed5, 0xd134621f, 0xc4a6fe8a,
	0x342e539d, 0xa2f355a0, 0x058ae132, 0xa4f6eb75,
	0x0b83ec39, 0x4060efaa, 0x5e719f06, 0xbd6e1051,
	0x3e218af9, 0x96dd063d, 0xdd3e05ae, 0x4de6bd46,
	0x91548db5, 0x71c45d05, 0x0406d46f, 0x605015ff,
	0x1998fb24, 0xd6bde997, 0x894043cc, 0x67d99e77,
	0xb0e842bd, 0x07898b88, 0xe7195b38, 0x79c8eedb,
	0xa17c0a47, 0x7c420fe9, 0xf8841ec9, 0x00000000,
	0x09808683, 0x322bed48, 0x1e1170ac, 0x6c5a724e,
	0xfd0efffb, 0x0f853856, 0x3daed51e, 0x362d3927,
	0x0a0fd964, 0x685ca621, 0x9b5b54d1, 0x24362e3a,
	0x0c0a67b1, 0x9357e70f, 0xb4ee96d2, 0x1b9b919e,
	0x80c0c54f, 0x61dc20a2, 0x5a774b69, 0x1c121a16,
	0xe293ba0a, 0xc0a02ae5, 0x3c22e043, 0x121b171d,
	0x0e090d0b, 0xf28bc7ad, 0x2db6a8b9, 0x141ea9c8,
	0x57f11985, 0xaf75074c, 0xee99ddbb, 0xa37f60fd,
	0xf701269f, 0x5c72f5bc, 0x44663bc5, 0x5bfb7e34,
	0x8b432976, 0xcb23c6dc, 0xb6edfc68, 0xb8e4f163,
	0xd731dcca, 0x42638510, 0x13972240, 0x84c61120,
	0x854a247d, 0xd2bb3df8, 0xaef93211, 0xc729a16d,
	0x1d9e2f4b, 0xdcb230f3, 0x0d8652ec, 0x77c1e3d0,
	0x2bb3166c, 0xa970b999, 0x119448fa, 0x47e96422,
	0xa8fc8cc4, 0xa0f03f1a, 0x567d2cd8, 0x223390ef,
	0x87494ec7, 0xd938d1c1, 0x8ccaa2fe, 0x98d40b36,
	0xa6f581cf, 0xa57ade28, 0xdab78e26, 0x3fadbfa4,
	0x2c3a9de4, 0x5078920d, 0x6a5fcc9b, 0x547e4662,
	0xf68d13c2, 0x90d8b8e8, 0x2e39f75e, 0x82c3aff5,
	0x9f5d80be, 0x69d0937c, 0x6fd52da9, 0xcf2512b3,
	0xc8ac993b, 0x10187da7, 0xe89c636e, 0xdb3bbb7b,
	0xcd267809, 0x6e5918f4, 0xec9ab701, 0x834f9aa8,
	0xe6956e65, 0xaaffe67e, 0x21bccf08, 0xef15e8e6,
	0xbae79bd9, 0x4a6f36ce, 0xea9f09d4, 0x29b07cd6,
	0x31a4b2af, 0x2a3f2331, 0xc6a59430, 0x35a266c0,
	0x744ebc37, 0xfc82caa6, 0xe090d0b0, 0x33a7d815,
	0xf104984a, 0x41ecdaf7, 0x7fcd500e, 0x1791f62f,
	0x764dd68d, 0x43efb04d, 0xccaa4d54, 0xe49604df,
	0x9ed1b5e3, 0x4c6a881b, 0xc12c1fb8, 0x4665517f,
	0x9d5eea04, 0x018c355d, 0xfa877473, 0xfb0b412e,
	0xb3671d5a, 0x92dbd252, 0xe9105633, 0x6dd64713,
	0x9ad7618c, 0x37a10c7a, 0x59f8148e, 0xeb133c89,
	0xcea927ee, 0xb761c935, 0xe11ce5ed, 0x7a47b13c,
	0x9cd2df59, 0x55f2733f, 0x1814ce79, 0x73c737bf,
	0x53f7cdea, 0x5ffdaa5b, 0xdf3d6f14, 0x7844db86,
	0xcaaff381, 0xb968c43e, 0x3824342c, 0xc2a3405f,
	0x161dc372, 0xbce2250c, 0x283c498b, 0xff0d9541,
	0x39a80171, 0x080cb3de, 0xd8b4e49c, 0x6456c190,
	0x7bcb8461, 0xd532b670, 0x486c5c74, 0xd0b85742);
$code.=<<___;
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
.type	AES_Td,#object
.size	AES_Td,(.-AES_Td)

.align	64
.skip	16
_sparcv9_AES_decrypt:
	save	%sp,-$frame-$locals,%sp
	stx	%i7,[%sp+$bias+$frame+0]	! off-load return address
	ld	[$key+240],$rounds
	ld	[$key+0],$t0
	ld	[$key+4],$t1			!
	ld	[$key+8],$t2
	ld	[$key+12],$t3
	srl	$rounds,1,$rounds
	xor	$t0,$s0,$s0
	ld	[$key+16],$t0
	xor	$t1,$s1,$s1
	ld	[$key+20],$t1
	srl	$s0,21,$acc0			!
	xor	$t2,$s2,$s2
	ld	[$key+24],$t2
	xor	$t3,$s3,$s3
	and	$acc0,2040,$acc0
	ld	[$key+28],$t3
	srl	$s3,13,$acc1
	nop
.Ldec_loop:
	srl	$s2,5,$acc2			!
	and	$acc1,2040,$acc1
	ldx	[$tbl+$acc0],$acc0
	sll	$s1,3,$acc3
	and	$acc2,2040,$acc2
	ldx	[$tbl+$acc1],$acc1
	srl	$s1,21,$acc4
	and	$acc3,2040,$acc3
	ldx	[$tbl+$acc2],$acc2		!
	srl	$s0,13,$acc5
	and	$acc4,2040,$acc4
	ldx	[$tbl+$acc3],$acc3
	srl	$s3,5,$acc6
	and	$acc5,2040,$acc5
	ldx	[$tbl+$acc4],$acc4
	fmovs	%f0,%f0
	sll	$s2,3,$acc7			!
	and	$acc6,2040,$acc6
	ldx	[$tbl+$acc5],$acc5
	srl	$s2,21,$acc8
	and	$acc7,2040,$acc7
	ldx	[$tbl+$acc6],$acc6
	srl	$s1,13,$acc9
	and	$acc8,2040,$acc8
	ldx	[$tbl+$acc7],$acc7		!
	srl	$s0,5,$acc10
	and	$acc9,2040,$acc9
	ldx	[$tbl+$acc8],$acc8
	sll	$s3,3,$acc11
	and	$acc10,2040,$acc10
	ldx	[$tbl+$acc9],$acc9
	fmovs	%f0,%f0
	srl	$s3,21,$acc12			!
	and	$acc11,2040,$acc11
	ldx	[$tbl+$acc10],$acc10
	srl	$s2,13,$acc13
	and	$acc12,2040,$acc12
	ldx	[$tbl+$acc11],$acc11
	srl	$s1,5,$acc14
	and	$acc13,2040,$acc13
	ldx	[$tbl+$acc12],$acc12		!
	sll	$s0,3,$acc15
	and	$acc14,2040,$acc14
	ldx	[$tbl+$acc13],$acc13
	and	$acc15,2040,$acc15
	add	$key,32,$key
	ldx	[$tbl+$acc14],$acc14
	fmovs	%f0,%f0
	subcc	$rounds,1,$rounds		!
	ldx	[$tbl+$acc15],$acc15
	bz,a,pn	%icc,.Ldec_last
	add	$tbl,2048,$rounds

		srlx	$acc1,8,$acc1
		xor	$acc0,$t0,$t0
	ld	[$key+0],$s0
	fmovs	%f0,%f0
		srlx	$acc2,16,$acc2		!
		xor	$acc1,$t0,$t0
	ld	[$key+4],$s1
		srlx	$acc3,24,$acc3
		xor	$acc2,$t0,$t0
	ld	[$key+8],$s2
		srlx	$acc5,8,$acc5
		xor	$acc3,$t0,$t0
	ld	[$key+12],$s3			!
		srlx	$acc6,16,$acc6
		xor	$acc4,$t1,$t1
	fmovs	%f0,%f0
		srlx	$acc7,24,$acc7
		xor	$acc5,$t1,$t1
		srlx	$acc9,8,$acc9
		xor	$acc6,$t1,$t1
		srlx	$acc10,16,$acc10	!
		xor	$acc7,$t1,$t1
		srlx	$acc11,24,$acc11
		xor	$acc8,$t2,$t2
		srlx	$acc13,8,$acc13
		xor	$acc9,$t2,$t2
		srlx	$acc14,16,$acc14
		xor	$acc10,$t2,$t2
		srlx	$acc15,24,$acc15	!
		xor	$acc11,$t2,$t2
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$t3,$t3
	srl	$t0,21,$acc0
		xor	$acc14,$t3,$t3
		xor	$acc15,$t3,$t3
	srl	$t3,13,$acc1

	and	$acc0,2040,$acc0		!
	srl	$t2,5,$acc2
	and	$acc1,2040,$acc1
	ldx	[$tbl+$acc0],$acc0
	sll	$t1,3,$acc3
	and	$acc2,2040,$acc2
	ldx	[$tbl+$acc1],$acc1
	fmovs	%f0,%f0
	srl	$t1,21,$acc4			!
	and	$acc3,2040,$acc3
	ldx	[$tbl+$acc2],$acc2
	srl	$t0,13,$acc5
	and	$acc4,2040,$acc4
	ldx	[$tbl+$acc3],$acc3
	srl	$t3,5,$acc6
	and	$acc5,2040,$acc5
	ldx	[$tbl+$acc4],$acc4		!
	sll	$t2,3,$acc7
	and	$acc6,2040,$acc6
	ldx	[$tbl+$acc5],$acc5
	srl	$t2,21,$acc8
	and	$acc7,2040,$acc7
	ldx	[$tbl+$acc6],$acc6
	fmovs	%f0,%f0
	srl	$t1,13,$acc9			!
	and	$acc8,2040,$acc8
	ldx	[$tbl+$acc7],$acc7
	srl	$t0,5,$acc10
	and	$acc9,2040,$acc9
	ldx	[$tbl+$acc8],$acc8
	sll	$t3,3,$acc11
	and	$acc10,2040,$acc10
	ldx	[$tbl+$acc9],$acc9		!
	srl	$t3,21,$acc12
	and	$acc11,2040,$acc11
	ldx	[$tbl+$acc10],$acc10
	srl	$t2,13,$acc13
	and	$acc12,2040,$acc12
	ldx	[$tbl+$acc11],$acc11
	fmovs	%f0,%f0
	srl	$t1,5,$acc14			!
	and	$acc13,2040,$acc13
	ldx	[$tbl+$acc12],$acc12
	sll	$t0,3,$acc15
	and	$acc14,2040,$acc14
	ldx	[$tbl+$acc13],$acc13
		srlx	$acc1,8,$acc1
	and	$acc15,2040,$acc15
	ldx	[$tbl+$acc14],$acc14		!

		srlx	$acc2,16,$acc2
		xor	$acc0,$s0,$s0
	ldx	[$tbl+$acc15],$acc15
		srlx	$acc3,24,$acc3
		xor	$acc1,$s0,$s0
	ld	[$key+16],$t0
	fmovs	%f0,%f0
		srlx	$acc5,8,$acc5		!
		xor	$acc2,$s0,$s0
	ld	[$key+20],$t1
		srlx	$acc6,16,$acc6
		xor	$acc3,$s0,$s0
	ld	[$key+24],$t2
		srlx	$acc7,24,$acc7
		xor	$acc4,$s1,$s1
	ld	[$key+28],$t3			!
		srlx	$acc9,8,$acc9
		xor	$acc5,$s1,$s1
	ldx	[$tbl+2048+0],%g0		! prefetch td4
		srlx	$acc10,16,$acc10
		xor	$acc6,$s1,$s1
	ldx	[$tbl+2048+32],%g0		! prefetch td4
		srlx	$acc11,24,$acc11
		xor	$acc7,$s1,$s1
	ldx	[$tbl+2048+64],%g0		! prefetch td4
		srlx	$acc13,8,$acc13
		xor	$acc8,$s2,$s2
	ldx	[$tbl+2048+96],%g0		! prefetch td4
		srlx	$acc14,16,$acc14	!
		xor	$acc9,$s2,$s2
	ldx	[$tbl+2048+128],%g0		! prefetch td4
		srlx	$acc15,24,$acc15
		xor	$acc10,$s2,$s2
	ldx	[$tbl+2048+160],%g0		! prefetch td4
	srl	$s0,21,$acc0
		xor	$acc11,$s2,$s2
	ldx	[$tbl+2048+192],%g0		! prefetch td4
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$s3,$s3
	ldx	[$tbl+2048+224],%g0		! prefetch td4
	and	$acc0,2040,$acc0		!
		xor	$acc14,$s3,$s3
		xor	$acc15,$s3,$s3
	ba	.Ldec_loop
	srl	$s3,13,$acc1

.align	32
.Ldec_last:
		srlx	$acc1,8,$acc1		!
		xor	$acc0,$t0,$t0
	ld	[$key+0],$s0
		srlx	$acc2,16,$acc2
		xor	$acc1,$t0,$t0
	ld	[$key+4],$s1
		srlx	$acc3,24,$acc3
		xor	$acc2,$t0,$t0
	ld	[$key+8],$s2			!
		srlx	$acc5,8,$acc5
		xor	$acc3,$t0,$t0
	ld	[$key+12],$s3
		srlx	$acc6,16,$acc6
		xor	$acc4,$t1,$t1
		srlx	$acc7,24,$acc7
		xor	$acc5,$t1,$t1
		srlx	$acc9,8,$acc9		!
		xor	$acc6,$t1,$t1
		srlx	$acc10,16,$acc10
		xor	$acc7,$t1,$t1
		srlx	$acc11,24,$acc11
		xor	$acc8,$t2,$t2
		srlx	$acc13,8,$acc13
		xor	$acc9,$t2,$t2
		srlx	$acc14,16,$acc14	!
		xor	$acc10,$t2,$t2
		srlx	$acc15,24,$acc15
		xor	$acc11,$t2,$t2
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$t3,$t3
	srl	$t0,24,$acc0
		xor	$acc14,$t3,$t3
		xor	$acc15,$t3,$t3		!
	srl	$t3,16,$acc1

	srl	$t2,8,$acc2
	and	$acc1,255,$acc1
	ldub	[$rounds+$acc0],$acc0
	srl	$t1,24,$acc4
	and	$acc2,255,$acc2
	ldub	[$rounds+$acc1],$acc1
	srl	$t0,16,$acc5			!
	and	$t1,255,$acc3
	ldub	[$rounds+$acc2],$acc2
	ldub	[$rounds+$acc3],$acc3
	srl	$t3,8,$acc6
	and	$acc5,255,$acc5
	ldub	[$rounds+$acc4],$acc4
	fmovs	%f0,%f0
	srl	$t2,24,$acc8			!
	and	$acc6,255,$acc6
	ldub	[$rounds+$acc5],$acc5
	srl	$t1,16,$acc9
	and	$t2,255,$acc7
	ldub	[$rounds+$acc6],$acc6
	ldub	[$rounds+$acc7],$acc7
	fmovs	%f0,%f0
	srl	$t0,8,$acc10			!
	and	$acc9,255,$acc9
	ldub	[$rounds+$acc8],$acc8
	srl	$t3,24,$acc12
	and	$acc10,255,$acc10
	ldub	[$rounds+$acc9],$acc9
	srl	$t2,16,$acc13
	and	$t3,255,$acc11
	ldub	[$rounds+$acc10],$acc10		!
	srl	$t1,8,$acc14
	and	$acc13,255,$acc13
	ldub	[$rounds+$acc11],$acc11
	ldub	[$rounds+$acc12],$acc12
	and	$acc14,255,$acc14
	ldub	[$rounds+$acc13],$acc13
	and	$t0,255,$acc15
	ldub	[$rounds+$acc14],$acc14		!

		sll	$acc0,24,$acc0
		xor	$acc3,$s0,$s0
	ldub	[$rounds+$acc15],$acc15
		sll	$acc1,16,$acc1
		xor	$acc0,$s0,$s0
	ldx	[%sp+$bias+$frame+0],%i7	! restore return address
	fmovs	%f0,%f0
		sll	$acc2,8,$acc2		!
		xor	$acc1,$s0,$s0
		sll	$acc4,24,$acc4
		xor	$acc2,$s0,$s0
		sll	$acc5,16,$acc5
		xor	$acc7,$s1,$s1
		sll	$acc6,8,$acc6
		xor	$acc4,$s1,$s1
		sll	$acc8,24,$acc8		!
		xor	$acc5,$s1,$s1
		sll	$acc9,16,$acc9
		xor	$acc11,$s2,$s2
		sll	$acc10,8,$acc10
		xor	$acc6,$s1,$s1
		sll	$acc12,24,$acc12
		xor	$acc8,$s2,$s2
		sll	$acc13,16,$acc13	!
		xor	$acc9,$s2,$s2
		sll	$acc14,8,$acc14
		xor	$acc10,$s2,$s2
		xor	$acc12,$acc14,$acc14
		xor	$acc13,$s3,$s3
		xor	$acc14,$s3,$s3
		xor	$acc15,$s3,$s3

	ret
	restore
.type	_sparcv9_AES_decrypt,#function
.size	_sparcv9_AES_decrypt,(.-_sparcv9_AES_decrypt)

.align	32
.globl	AES_decrypt
AES_decrypt:
	or	%o0,%o1,%g1
	andcc	%g1,3,%g0
	bnz,pn	%xcc,.Lunaligned_dec
	save	%sp,-$frame,%sp

	ld	[%i0+0],%o0
	ld	[%i0+4],%o1
	ld	[%i0+8],%o2
	ld	[%i0+12],%o3

1:	call	.+8
	add	%o7,AES_Td-1b,%o4
	call	_sparcv9_AES_decrypt
	mov	%i2,%o5

	st	%o0,[%i1+0]
	st	%o1,[%i1+4]
	st	%o2,[%i1+8]
	st	%o3,[%i1+12]

	ret
	restore

.align	32
.Lunaligned_dec:
	ldub	[%i0+0],%l0
	ldub	[%i0+1],%l1
	ldub	[%i0+2],%l2

	sll	%l0,24,%l0
	ldub	[%i0+3],%l3
	sll	%l1,16,%l1
	ldub	[%i0+4],%l4
	sll	%l2,8,%l2
	or	%l1,%l0,%l0
	ldub	[%i0+5],%l5
	sll	%l4,24,%l4
	or	%l3,%l2,%l2
	ldub	[%i0+6],%l6
	sll	%l5,16,%l5
	or	%l0,%l2,%o0
	ldub	[%i0+7],%l7

	sll	%l6,8,%l6
	or	%l5,%l4,%l4
	ldub	[%i0+8],%l0
	or	%l7,%l6,%l6
	ldub	[%i0+9],%l1
	or	%l4,%l6,%o1
	ldub	[%i0+10],%l2

	sll	%l0,24,%l0
	ldub	[%i0+11],%l3
	sll	%l1,16,%l1
	ldub	[%i0+12],%l4
	sll	%l2,8,%l2
	or	%l1,%l0,%l0
	ldub	[%i0+13],%l5
	sll	%l4,24,%l4
	or	%l3,%l2,%l2
	ldub	[%i0+14],%l6
	sll	%l5,16,%l5
	or	%l0,%l2,%o2
	ldub	[%i0+15],%l7

	sll	%l6,8,%l6
	or	%l5,%l4,%l4
	or	%l7,%l6,%l6
	or	%l4,%l6,%o3

1:	call	.+8
	add	%o7,AES_Td-1b,%o4
	call	_sparcv9_AES_decrypt
	mov	%i2,%o5

	srl	%o0,24,%l0
	srl	%o0,16,%l1
	stb	%l0,[%i1+0]
	srl	%o0,8,%l2
	stb	%l1,[%i1+1]
	stb	%l2,[%i1+2]
	srl	%o1,24,%l4
	stb	%o0,[%i1+3]

	srl	%o1,16,%l5
	stb	%l4,[%i1+4]
	srl	%o1,8,%l6
	stb	%l5,[%i1+5]
	stb	%l6,[%i1+6]
	srl	%o2,24,%l0
	stb	%o1,[%i1+7]

	srl	%o2,16,%l1
	stb	%l0,[%i1+8]
	srl	%o2,8,%l2
	stb	%l1,[%i1+9]
	stb	%l2,[%i1+10]
	srl	%o3,24,%l4
	stb	%o2,[%i1+11]

	srl	%o3,16,%l5
	stb	%l4,[%i1+12]
	srl	%o3,8,%l6
	stb	%l5,[%i1+13]
	stb	%l6,[%i1+14]
	stb	%o3,[%i1+15]

	ret
	restore
.type	AES_decrypt,#function
.size	AES_decrypt,(.-AES_decrypt)
___

# fmovs instructions substituting for FP nops were originally added
# to meet specific instruction alignment requirements to maximize ILP.
# As UltraSPARC T1, a.k.a. Niagara, has shared FPU, FP nops can have
# undesired effect, so just omit them and sacrifice some portion of
# percent in performance...
$code =~ s/fmovs.*$//gm;

print $code;
close STDOUT;	# ensure flush

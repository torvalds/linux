#! /usr/bin/env perl
# Copyright 2015-2019 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


######################################################################
## Constant-time SSSE3 AES core implementation.
## version 0.1
##
## By Mike Hamburg (Stanford University), 2009
## Public domain.
##
## For details see http://shiftleft.org/papers/vector_aes/ and
## http://crypto.stanford.edu/vpaes/.
##
######################################################################
# ARMv8 NEON adaptation by <appro@openssl.org>
#
# Reason for undertaken effort is that there is at least one popular
# SoC based on Cortex-A53 that doesn't have crypto extensions.
#
#                   CBC enc     ECB enc/dec(*)   [bit-sliced enc/dec]
# Cortex-A53        21.5        18.1/20.6        [17.5/19.8         ]
# Cortex-A57        36.0(**)    20.4/24.9(**)    [14.4/16.6         ]
# X-Gene            45.9(**)    45.8/57.7(**)    [33.1/37.6(**)     ]
# Denver(***)       16.6(**)    15.1/17.8(**)    [8.80/9.93         ]
# Apple A7(***)     22.7(**)    10.9/14.3        [8.45/10.0         ]
# Mongoose(***)     26.3(**)    21.0/25.0(**)    [13.3/16.8         ]
#
# (*)	ECB denotes approximate result for parallelizable modes
#	such as CBC decrypt, CTR, etc.;
# (**)	these results are worse than scalar compiler-generated
#	code, but it's constant-time and therefore preferred;
# (***)	presented for reference/comparison purposes;

$flavour = shift;
while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/arm-xlate.pl" and -f $xlate) or
die "can't locate arm-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

$code.=<<___;
.text

.type	_vpaes_consts,%object
.align	7	// totally strategic alignment
_vpaes_consts:
.Lk_mc_forward:	// mc_forward
	.quad	0x0407060500030201, 0x0C0F0E0D080B0A09
	.quad	0x080B0A0904070605, 0x000302010C0F0E0D
	.quad	0x0C0F0E0D080B0A09, 0x0407060500030201
	.quad	0x000302010C0F0E0D, 0x080B0A0904070605
.Lk_mc_backward:// mc_backward
	.quad	0x0605040702010003, 0x0E0D0C0F0A09080B
	.quad	0x020100030E0D0C0F, 0x0A09080B06050407
	.quad	0x0E0D0C0F0A09080B, 0x0605040702010003
	.quad	0x0A09080B06050407, 0x020100030E0D0C0F
.Lk_sr:		// sr
	.quad	0x0706050403020100, 0x0F0E0D0C0B0A0908
	.quad	0x030E09040F0A0500, 0x0B06010C07020D08
	.quad	0x0F060D040B020900, 0x070E050C030A0108
	.quad	0x0B0E0104070A0D00, 0x0306090C0F020508

//
// "Hot" constants
//
.Lk_inv:	// inv, inva
	.quad	0x0E05060F0D080180, 0x040703090A0B0C02
	.quad	0x01040A060F0B0780, 0x030D0E0C02050809
.Lk_ipt:	// input transform (lo, hi)
	.quad	0xC2B2E8985A2A7000, 0xCABAE09052227808
	.quad	0x4C01307D317C4D00, 0xCD80B1FCB0FDCC81
.Lk_sbo:	// sbou, sbot
	.quad	0xD0D26D176FBDC700, 0x15AABF7AC502A878
	.quad	0xCFE474A55FBB6A00, 0x8E1E90D1412B35FA
.Lk_sb1:	// sb1u, sb1t
	.quad	0x3618D415FAE22300, 0x3BF7CCC10D2ED9EF
	.quad	0xB19BE18FCB503E00, 0xA5DF7A6E142AF544
.Lk_sb2:	// sb2u, sb2t
	.quad	0x69EB88400AE12900, 0xC2A163C8AB82234A
	.quad	0xE27A93C60B712400, 0x5EB7E955BC982FCD

//
//  Decryption stuff
//
.Lk_dipt:	// decryption input transform
	.quad	0x0F505B040B545F00, 0x154A411E114E451A
	.quad	0x86E383E660056500, 0x12771772F491F194
.Lk_dsbo:	// decryption sbox final output
	.quad	0x1387EA537EF94000, 0xC7AA6DB9D4943E2D
	.quad	0x12D7560F93441D00, 0xCA4B8159D8C58E9C
.Lk_dsb9:	// decryption sbox output *9*u, *9*t
	.quad	0x851C03539A86D600, 0xCAD51F504F994CC9
	.quad	0xC03B1789ECD74900, 0x725E2C9EB2FBA565
.Lk_dsbd:	// decryption sbox output *D*u, *D*t
	.quad	0x7D57CCDFE6B1A200, 0xF56E9B13882A4439
	.quad	0x3CE2FAF724C6CB00, 0x2931180D15DEEFD3
.Lk_dsbb:	// decryption sbox output *B*u, *B*t
	.quad	0xD022649296B44200, 0x602646F6B0F2D404
	.quad	0xC19498A6CD596700, 0xF3FF0C3E3255AA6B
.Lk_dsbe:	// decryption sbox output *E*u, *E*t
	.quad	0x46F2929626D4D000, 0x2242600464B4F6B0
	.quad	0x0C55A6CDFFAAC100, 0x9467F36B98593E32

//
//  Key schedule constants
//
.Lk_dksd:	// decryption key schedule: invskew x*D
	.quad	0xFEB91A5DA3E44700, 0x0740E3A45A1DBEF9
	.quad	0x41C277F4B5368300, 0x5FDC69EAAB289D1E
.Lk_dksb:	// decryption key schedule: invskew x*B
	.quad	0x9A4FCA1F8550D500, 0x03D653861CC94C99
	.quad	0x115BEDA7B6FC4A00, 0xD993256F7E3482C8
.Lk_dkse:	// decryption key schedule: invskew x*E + 0x63
	.quad	0xD5031CCA1FC9D600, 0x53859A4C994F5086
	.quad	0xA23196054FDC7BE8, 0xCD5EF96A20B31487
.Lk_dks9:	// decryption key schedule: invskew x*9
	.quad	0xB6116FC87ED9A700, 0x4AED933482255BFC
	.quad	0x4576516227143300, 0x8BB89FACE9DAFDCE

.Lk_rcon:	// rcon
	.quad	0x1F8391B9AF9DEEB6, 0x702A98084D7C7D81

.Lk_opt:	// output transform
	.quad	0xFF9F4929D6B66000, 0xF7974121DEBE6808
	.quad	0x01EDBD5150BCEC00, 0xE10D5DB1B05C0CE0
.Lk_deskew:	// deskew tables: inverts the sbox's "skew"
	.quad	0x07E4A34047A4E300, 0x1DFEB95A5DBEF91A
	.quad	0x5F36B5DC83EA6900, 0x2841C2ABF49D1E77

.asciz  "Vector Permutation AES for ARMv8, Mike Hamburg (Stanford University)"
.size	_vpaes_consts,.-_vpaes_consts
.align	6
___

{
my ($inp,$out,$key) = map("x$_",(0..2));

my ($invlo,$invhi,$iptlo,$ipthi,$sbou,$sbot) = map("v$_.16b",(18..23));
my ($sb1u,$sb1t,$sb2u,$sb2t) = map("v$_.16b",(24..27));
my ($sb9u,$sb9t,$sbdu,$sbdt,$sbbu,$sbbt,$sbeu,$sbet)=map("v$_.16b",(24..31));

$code.=<<___;
##
##  _aes_preheat
##
##  Fills register %r10 -> .aes_consts (so you can -fPIC)
##  and %xmm9-%xmm15 as specified below.
##
.type	_vpaes_encrypt_preheat,%function
.align	4
_vpaes_encrypt_preheat:
	adr	x10, .Lk_inv
	movi	v17.16b, #0x0f
	ld1	{v18.2d-v19.2d}, [x10],#32	// .Lk_inv
	ld1	{v20.2d-v23.2d}, [x10],#64	// .Lk_ipt, .Lk_sbo
	ld1	{v24.2d-v27.2d}, [x10]		// .Lk_sb1, .Lk_sb2
	ret
.size	_vpaes_encrypt_preheat,.-_vpaes_encrypt_preheat

##
##  _aes_encrypt_core
##
##  AES-encrypt %xmm0.
##
##  Inputs:
##     %xmm0 = input
##     %xmm9-%xmm15 as in _vpaes_preheat
##    (%rdx) = scheduled keys
##
##  Output in %xmm0
##  Clobbers  %xmm1-%xmm5, %r9, %r10, %r11, %rax
##  Preserves %xmm6 - %xmm8 so you get some local vectors
##
##
.type	_vpaes_encrypt_core,%function
.align 4
_vpaes_encrypt_core:
	mov	x9, $key
	ldr	w8, [$key,#240]			// pull rounds
	adr	x11, .Lk_mc_forward+16
						// vmovdqa	.Lk_ipt(%rip),	%xmm2	# iptlo
	ld1	{v16.2d}, [x9], #16		// vmovdqu	(%r9),	%xmm5		# round0 key
	and	v1.16b, v7.16b, v17.16b		// vpand	%xmm9,	%xmm0,	%xmm1
	ushr	v0.16b, v7.16b, #4		// vpsrlb	\$4,	%xmm0,	%xmm0
	tbl	v1.16b, {$iptlo}, v1.16b	// vpshufb	%xmm1,	%xmm2,	%xmm1
						// vmovdqa	.Lk_ipt+16(%rip), %xmm3	# ipthi
	tbl	v2.16b, {$ipthi}, v0.16b	// vpshufb	%xmm0,	%xmm3,	%xmm2
	eor	v0.16b, v1.16b, v16.16b		// vpxor	%xmm5,	%xmm1,	%xmm0
	eor	v0.16b, v0.16b, v2.16b		// vpxor	%xmm2,	%xmm0,	%xmm0
	b	.Lenc_entry

.align 4
.Lenc_loop:
	// middle of middle round
	add	x10, x11, #0x40
	tbl	v4.16b, {$sb1t}, v2.16b		// vpshufb	%xmm2,	%xmm13,	%xmm4	# 4 = sb1u
	ld1	{v1.2d}, [x11], #16		// vmovdqa	-0x40(%r11,%r10), %xmm1	# .Lk_mc_forward[]
	tbl	v0.16b, {$sb1u}, v3.16b		// vpshufb	%xmm3,	%xmm12,	%xmm0	# 0 = sb1t
	eor	v4.16b, v4.16b, v16.16b		// vpxor	%xmm5,	%xmm4,	%xmm4	# 4 = sb1u + k
	tbl	v5.16b,	{$sb2t}, v2.16b		// vpshufb	%xmm2,	%xmm15,	%xmm5	# 4 = sb2u
	eor	v0.16b, v0.16b, v4.16b		// vpxor	%xmm4,	%xmm0,	%xmm0	# 0 = A
	tbl	v2.16b, {$sb2u}, v3.16b		// vpshufb	%xmm3,	%xmm14,	%xmm2	# 2 = sb2t
	ld1	{v4.2d}, [x10]			// vmovdqa	(%r11,%r10), %xmm4	# .Lk_mc_backward[]
	tbl	v3.16b, {v0.16b}, v1.16b	// vpshufb	%xmm1,	%xmm0,	%xmm3	# 0 = B
	eor	v2.16b, v2.16b, v5.16b		// vpxor	%xmm5,	%xmm2,	%xmm2	# 2 = 2A
	tbl	v0.16b, {v0.16b}, v4.16b	// vpshufb	%xmm4,	%xmm0,	%xmm0	# 3 = D
	eor	v3.16b, v3.16b, v2.16b		// vpxor	%xmm2,	%xmm3,	%xmm3	# 0 = 2A+B
	tbl	v4.16b, {v3.16b}, v1.16b	// vpshufb	%xmm1,	%xmm3,	%xmm4	# 0 = 2B+C
	eor	v0.16b, v0.16b, v3.16b		// vpxor	%xmm3,	%xmm0,	%xmm0	# 3 = 2A+B+D
	and	x11, x11, #~(1<<6)		// and		\$0x30,	%r11		# ... mod 4
	eor	v0.16b, v0.16b, v4.16b		// vpxor	%xmm4,	%xmm0, %xmm0	# 0 = 2A+3B+C+D
	sub	w8, w8, #1			// nr--

.Lenc_entry:
	// top of round
	and	v1.16b, v0.16b, v17.16b		// vpand	%xmm0,	%xmm9,	%xmm1   # 0 = k
	ushr	v0.16b, v0.16b, #4		// vpsrlb	\$4,	%xmm0,	%xmm0	# 1 = i
	tbl	v5.16b, {$invhi}, v1.16b	// vpshufb	%xmm1,	%xmm11,	%xmm5	# 2 = a/k
	eor	v1.16b, v1.16b, v0.16b		// vpxor	%xmm0,	%xmm1,	%xmm1	# 0 = j
	tbl	v3.16b, {$invlo}, v0.16b	// vpshufb	%xmm0, 	%xmm10,	%xmm3  	# 3 = 1/i
	tbl	v4.16b, {$invlo}, v1.16b	// vpshufb	%xmm1, 	%xmm10,	%xmm4  	# 4 = 1/j
	eor	v3.16b, v3.16b, v5.16b		// vpxor	%xmm5,	%xmm3,	%xmm3	# 3 = iak = 1/i + a/k
	eor	v4.16b, v4.16b, v5.16b		// vpxor	%xmm5,	%xmm4,	%xmm4  	# 4 = jak = 1/j + a/k
	tbl	v2.16b, {$invlo}, v3.16b	// vpshufb	%xmm3,	%xmm10,	%xmm2  	# 2 = 1/iak
	tbl	v3.16b, {$invlo}, v4.16b	// vpshufb	%xmm4,	%xmm10,	%xmm3	# 3 = 1/jak
	eor	v2.16b, v2.16b, v1.16b		// vpxor	%xmm1,	%xmm2,	%xmm2  	# 2 = io
	eor	v3.16b, v3.16b, v0.16b		// vpxor	%xmm0,	%xmm3,	%xmm3	# 3 = jo
	ld1	{v16.2d}, [x9],#16		// vmovdqu	(%r9),	%xmm5
	cbnz	w8, .Lenc_loop

	// middle of last round
	add	x10, x11, #0x80
						// vmovdqa	-0x60(%r10), %xmm4	# 3 : sbou	.Lk_sbo
						// vmovdqa	-0x50(%r10), %xmm0	# 0 : sbot	.Lk_sbo+16
	tbl	v4.16b, {$sbou}, v2.16b		// vpshufb	%xmm2,	%xmm4,	%xmm4	# 4 = sbou
	ld1	{v1.2d}, [x10]			// vmovdqa	0x40(%r11,%r10), %xmm1	# .Lk_sr[]
	tbl	v0.16b, {$sbot}, v3.16b		// vpshufb	%xmm3,	%xmm0,	%xmm0	# 0 = sb1t
	eor	v4.16b, v4.16b, v16.16b		// vpxor	%xmm5,	%xmm4,	%xmm4	# 4 = sb1u + k
	eor	v0.16b, v0.16b, v4.16b		// vpxor	%xmm4,	%xmm0,	%xmm0	# 0 = A
	tbl	v0.16b, {v0.16b}, v1.16b	// vpshufb	%xmm1,	%xmm0,	%xmm0
	ret
.size	_vpaes_encrypt_core,.-_vpaes_encrypt_core

.globl	vpaes_encrypt
.type	vpaes_encrypt,%function
.align	4
vpaes_encrypt:
	.inst	0xd503233f			// paciasp
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0

	ld1	{v7.16b}, [$inp]
	bl	_vpaes_encrypt_preheat
	bl	_vpaes_encrypt_core
	st1	{v0.16b}, [$out]

	ldp	x29,x30,[sp],#16
	.inst	0xd50323bf			// autiasp
	ret
.size	vpaes_encrypt,.-vpaes_encrypt

.type	_vpaes_encrypt_2x,%function
.align 4
_vpaes_encrypt_2x:
	mov	x9, $key
	ldr	w8, [$key,#240]			// pull rounds
	adr	x11, .Lk_mc_forward+16
						// vmovdqa	.Lk_ipt(%rip),	%xmm2	# iptlo
	ld1	{v16.2d}, [x9], #16		// vmovdqu	(%r9),	%xmm5		# round0 key
	and	v1.16b,  v14.16b,  v17.16b	// vpand	%xmm9,	%xmm0,	%xmm1
	ushr	v0.16b,  v14.16b,  #4		// vpsrlb	\$4,	%xmm0,	%xmm0
	 and	v9.16b,  v15.16b,  v17.16b
	 ushr	v8.16b,  v15.16b,  #4
	tbl	v1.16b,  {$iptlo}, v1.16b	// vpshufb	%xmm1,	%xmm2,	%xmm1
	 tbl	v9.16b,  {$iptlo}, v9.16b
						// vmovdqa	.Lk_ipt+16(%rip), %xmm3	# ipthi
	tbl	v2.16b,  {$ipthi}, v0.16b	// vpshufb	%xmm0,	%xmm3,	%xmm2
	 tbl	v10.16b, {$ipthi}, v8.16b
	eor	v0.16b,  v1.16b,   v16.16b	// vpxor	%xmm5,	%xmm1,	%xmm0
	 eor	v8.16b,  v9.16b,   v16.16b
	eor	v0.16b,  v0.16b,   v2.16b	// vpxor	%xmm2,	%xmm0,	%xmm0
	 eor	v8.16b,  v8.16b,   v10.16b
	b	.Lenc_2x_entry

.align 4
.Lenc_2x_loop:
	// middle of middle round
	add	x10, x11, #0x40
	tbl	v4.16b,  {$sb1t}, v2.16b	// vpshufb	%xmm2,	%xmm13,	%xmm4	# 4 = sb1u
	 tbl	v12.16b, {$sb1t}, v10.16b
	ld1	{v1.2d}, [x11], #16		// vmovdqa	-0x40(%r11,%r10), %xmm1	# .Lk_mc_forward[]
	tbl	v0.16b,  {$sb1u}, v3.16b	// vpshufb	%xmm3,	%xmm12,	%xmm0	# 0 = sb1t
	 tbl	v8.16b,  {$sb1u}, v11.16b
	eor	v4.16b,  v4.16b,  v16.16b	// vpxor	%xmm5,	%xmm4,	%xmm4	# 4 = sb1u + k
	 eor	v12.16b, v12.16b, v16.16b
	tbl	v5.16b,	 {$sb2t}, v2.16b	// vpshufb	%xmm2,	%xmm15,	%xmm5	# 4 = sb2u
	 tbl	v13.16b, {$sb2t}, v10.16b
	eor	v0.16b,  v0.16b,  v4.16b	// vpxor	%xmm4,	%xmm0,	%xmm0	# 0 = A
	 eor	v8.16b,  v8.16b,  v12.16b
	tbl	v2.16b,  {$sb2u}, v3.16b	// vpshufb	%xmm3,	%xmm14,	%xmm2	# 2 = sb2t
	 tbl	v10.16b, {$sb2u}, v11.16b
	ld1	{v4.2d}, [x10]			// vmovdqa	(%r11,%r10), %xmm4	# .Lk_mc_backward[]
	tbl	v3.16b,  {v0.16b}, v1.16b	// vpshufb	%xmm1,	%xmm0,	%xmm3	# 0 = B
	 tbl	v11.16b, {v8.16b}, v1.16b
	eor	v2.16b,  v2.16b,  v5.16b	// vpxor	%xmm5,	%xmm2,	%xmm2	# 2 = 2A
	 eor	v10.16b, v10.16b, v13.16b
	tbl	v0.16b,  {v0.16b}, v4.16b	// vpshufb	%xmm4,	%xmm0,	%xmm0	# 3 = D
	 tbl	v8.16b,  {v8.16b}, v4.16b
	eor	v3.16b,  v3.16b,  v2.16b	// vpxor	%xmm2,	%xmm3,	%xmm3	# 0 = 2A+B
	 eor	v11.16b, v11.16b, v10.16b
	tbl	v4.16b,  {v3.16b}, v1.16b	// vpshufb	%xmm1,	%xmm3,	%xmm4	# 0 = 2B+C
	 tbl	v12.16b, {v11.16b},v1.16b
	eor	v0.16b,  v0.16b,  v3.16b	// vpxor	%xmm3,	%xmm0,	%xmm0	# 3 = 2A+B+D
	 eor	v8.16b,  v8.16b,  v11.16b
	and	x11, x11, #~(1<<6)		// and		\$0x30,	%r11		# ... mod 4
	eor	v0.16b,  v0.16b,  v4.16b	// vpxor	%xmm4,	%xmm0, %xmm0	# 0 = 2A+3B+C+D
	 eor	v8.16b,  v8.16b,  v12.16b
	sub	w8, w8, #1			// nr--

.Lenc_2x_entry:
	// top of round
	and	v1.16b,  v0.16b, v17.16b	// vpand	%xmm0,	%xmm9,	%xmm1   # 0 = k
	ushr	v0.16b,  v0.16b, #4		// vpsrlb	\$4,	%xmm0,	%xmm0	# 1 = i
	 and	v9.16b,  v8.16b, v17.16b
	 ushr	v8.16b,  v8.16b, #4
	tbl	v5.16b,  {$invhi},v1.16b	// vpshufb	%xmm1,	%xmm11,	%xmm5	# 2 = a/k
	 tbl	v13.16b, {$invhi},v9.16b
	eor	v1.16b,  v1.16b,  v0.16b	// vpxor	%xmm0,	%xmm1,	%xmm1	# 0 = j
	 eor	v9.16b,  v9.16b,  v8.16b
	tbl	v3.16b,  {$invlo},v0.16b	// vpshufb	%xmm0, 	%xmm10,	%xmm3  	# 3 = 1/i
	 tbl	v11.16b, {$invlo},v8.16b
	tbl	v4.16b,  {$invlo},v1.16b	// vpshufb	%xmm1, 	%xmm10,	%xmm4  	# 4 = 1/j
	 tbl	v12.16b, {$invlo},v9.16b
	eor	v3.16b,  v3.16b,  v5.16b	// vpxor	%xmm5,	%xmm3,	%xmm3	# 3 = iak = 1/i + a/k
	 eor	v11.16b, v11.16b, v13.16b
	eor	v4.16b,  v4.16b,  v5.16b	// vpxor	%xmm5,	%xmm4,	%xmm4  	# 4 = jak = 1/j + a/k
	 eor	v12.16b, v12.16b, v13.16b
	tbl	v2.16b,  {$invlo},v3.16b	// vpshufb	%xmm3,	%xmm10,	%xmm2  	# 2 = 1/iak
	 tbl	v10.16b, {$invlo},v11.16b
	tbl	v3.16b,  {$invlo},v4.16b	// vpshufb	%xmm4,	%xmm10,	%xmm3	# 3 = 1/jak
	 tbl	v11.16b, {$invlo},v12.16b
	eor	v2.16b,  v2.16b,  v1.16b	// vpxor	%xmm1,	%xmm2,	%xmm2  	# 2 = io
	 eor	v10.16b, v10.16b, v9.16b
	eor	v3.16b,  v3.16b,  v0.16b	// vpxor	%xmm0,	%xmm3,	%xmm3	# 3 = jo
	 eor	v11.16b, v11.16b, v8.16b
	ld1	{v16.2d}, [x9],#16		// vmovdqu	(%r9),	%xmm5
	cbnz	w8, .Lenc_2x_loop

	// middle of last round
	add	x10, x11, #0x80
						// vmovdqa	-0x60(%r10), %xmm4	# 3 : sbou	.Lk_sbo
						// vmovdqa	-0x50(%r10), %xmm0	# 0 : sbot	.Lk_sbo+16
	tbl	v4.16b,  {$sbou}, v2.16b	// vpshufb	%xmm2,	%xmm4,	%xmm4	# 4 = sbou
	 tbl	v12.16b, {$sbou}, v10.16b
	ld1	{v1.2d}, [x10]			// vmovdqa	0x40(%r11,%r10), %xmm1	# .Lk_sr[]
	tbl	v0.16b,  {$sbot}, v3.16b	// vpshufb	%xmm3,	%xmm0,	%xmm0	# 0 = sb1t
	 tbl	v8.16b,  {$sbot}, v11.16b
	eor	v4.16b,  v4.16b,  v16.16b	// vpxor	%xmm5,	%xmm4,	%xmm4	# 4 = sb1u + k
	 eor	v12.16b, v12.16b, v16.16b
	eor	v0.16b,  v0.16b,  v4.16b	// vpxor	%xmm4,	%xmm0,	%xmm0	# 0 = A
	 eor	v8.16b,  v8.16b,  v12.16b
	tbl	v0.16b,  {v0.16b},v1.16b	// vpshufb	%xmm1,	%xmm0,	%xmm0
	 tbl	v1.16b,  {v8.16b},v1.16b
	ret
.size	_vpaes_encrypt_2x,.-_vpaes_encrypt_2x

.type	_vpaes_decrypt_preheat,%function
.align	4
_vpaes_decrypt_preheat:
	adr	x10, .Lk_inv
	movi	v17.16b, #0x0f
	adr	x11, .Lk_dipt
	ld1	{v18.2d-v19.2d}, [x10],#32	// .Lk_inv
	ld1	{v20.2d-v23.2d}, [x11],#64	// .Lk_dipt, .Lk_dsbo
	ld1	{v24.2d-v27.2d}, [x11],#64	// .Lk_dsb9, .Lk_dsbd
	ld1	{v28.2d-v31.2d}, [x11]		// .Lk_dsbb, .Lk_dsbe
	ret
.size	_vpaes_decrypt_preheat,.-_vpaes_decrypt_preheat

##
##  Decryption core
##
##  Same API as encryption core.
##
.type	_vpaes_decrypt_core,%function
.align	4
_vpaes_decrypt_core:
	mov	x9, $key
	ldr	w8, [$key,#240]			// pull rounds

						// vmovdqa	.Lk_dipt(%rip), %xmm2	# iptlo
	lsl	x11, x8, #4			// mov	%rax,	%r11;	shl	\$4, %r11
	eor	x11, x11, #0x30			// xor		\$0x30,	%r11
	adr	x10, .Lk_sr
	and	x11, x11, #0x30			// and		\$0x30,	%r11
	add	x11, x11, x10
	adr	x10, .Lk_mc_forward+48

	ld1	{v16.2d}, [x9],#16		// vmovdqu	(%r9),	%xmm4		# round0 key
	and	v1.16b, v7.16b, v17.16b		// vpand	%xmm9,	%xmm0,	%xmm1
	ushr	v0.16b, v7.16b, #4		// vpsrlb	\$4,	%xmm0,	%xmm0
	tbl	v2.16b, {$iptlo}, v1.16b	// vpshufb	%xmm1,	%xmm2,	%xmm2
	ld1	{v5.2d}, [x10]			// vmovdqa	.Lk_mc_forward+48(%rip), %xmm5
						// vmovdqa	.Lk_dipt+16(%rip), %xmm1 # ipthi
	tbl	v0.16b, {$ipthi}, v0.16b	// vpshufb	%xmm0,	%xmm1,	%xmm0
	eor	v2.16b, v2.16b, v16.16b		// vpxor	%xmm4,	%xmm2,	%xmm2
	eor	v0.16b, v0.16b, v2.16b		// vpxor	%xmm2,	%xmm0,	%xmm0
	b	.Ldec_entry

.align 4
.Ldec_loop:
//
//  Inverse mix columns
//
						// vmovdqa	-0x20(%r10),%xmm4		# 4 : sb9u
						// vmovdqa	-0x10(%r10),%xmm1		# 0 : sb9t
	tbl	v4.16b, {$sb9u}, v2.16b		// vpshufb	%xmm2,	%xmm4,	%xmm4		# 4 = sb9u
	tbl	v1.16b, {$sb9t}, v3.16b		// vpshufb	%xmm3,	%xmm1,	%xmm1		# 0 = sb9t
	eor	v0.16b, v4.16b, v16.16b		// vpxor	%xmm4,	%xmm0,	%xmm0
						// vmovdqa	0x00(%r10),%xmm4		# 4 : sbdu
	eor	v0.16b, v0.16b, v1.16b		// vpxor	%xmm1,	%xmm0,	%xmm0		# 0 = ch
						// vmovdqa	0x10(%r10),%xmm1		# 0 : sbdt

	tbl	v4.16b, {$sbdu}, v2.16b		// vpshufb	%xmm2,	%xmm4,	%xmm4		# 4 = sbdu
	tbl 	v0.16b, {v0.16b}, v5.16b	// vpshufb	%xmm5,	%xmm0,	%xmm0		# MC ch
	tbl	v1.16b, {$sbdt}, v3.16b		// vpshufb	%xmm3,	%xmm1,	%xmm1		# 0 = sbdt
	eor	v0.16b, v0.16b, v4.16b		// vpxor	%xmm4,	%xmm0,	%xmm0		# 4 = ch
						// vmovdqa	0x20(%r10),	%xmm4		# 4 : sbbu
	eor	v0.16b, v0.16b, v1.16b		// vpxor	%xmm1,	%xmm0,	%xmm0		# 0 = ch
						// vmovdqa	0x30(%r10),	%xmm1		# 0 : sbbt

	tbl	v4.16b, {$sbbu}, v2.16b		// vpshufb	%xmm2,	%xmm4,	%xmm4		# 4 = sbbu
	tbl	v0.16b, {v0.16b}, v5.16b	// vpshufb	%xmm5,	%xmm0,	%xmm0		# MC ch
	tbl	v1.16b, {$sbbt}, v3.16b		// vpshufb	%xmm3,	%xmm1,	%xmm1		# 0 = sbbt
	eor	v0.16b, v0.16b, v4.16b		// vpxor	%xmm4,	%xmm0,	%xmm0		# 4 = ch
						// vmovdqa	0x40(%r10),	%xmm4		# 4 : sbeu
	eor	v0.16b, v0.16b, v1.16b		// vpxor	%xmm1,	%xmm0,	%xmm0		# 0 = ch
						// vmovdqa	0x50(%r10),	%xmm1		# 0 : sbet

	tbl	v4.16b, {$sbeu}, v2.16b		// vpshufb	%xmm2,	%xmm4,	%xmm4		# 4 = sbeu
	tbl	v0.16b, {v0.16b}, v5.16b	// vpshufb	%xmm5,	%xmm0,	%xmm0		# MC ch
	tbl	v1.16b, {$sbet}, v3.16b		// vpshufb	%xmm3,	%xmm1,	%xmm1		# 0 = sbet
	eor	v0.16b, v0.16b, v4.16b		// vpxor	%xmm4,	%xmm0,	%xmm0		# 4 = ch
	ext	v5.16b, v5.16b, v5.16b, #12	// vpalignr \$12,	%xmm5,	%xmm5,	%xmm5
	eor	v0.16b, v0.16b, v1.16b		// vpxor	%xmm1,	%xmm0,	%xmm0		# 0 = ch
	sub	w8, w8, #1			// sub		\$1,%rax			# nr--

.Ldec_entry:
	// top of round
	and	v1.16b, v0.16b, v17.16b		// vpand	%xmm9,	%xmm0,	%xmm1	# 0 = k
	ushr	v0.16b, v0.16b, #4		// vpsrlb	\$4,	%xmm0,	%xmm0	# 1 = i
	tbl	v2.16b, {$invhi}, v1.16b	// vpshufb	%xmm1,	%xmm11,	%xmm2	# 2 = a/k
	eor	v1.16b,	v1.16b, v0.16b		// vpxor	%xmm0,	%xmm1,	%xmm1	# 0 = j
	tbl	v3.16b, {$invlo}, v0.16b	// vpshufb	%xmm0, 	%xmm10,	%xmm3	# 3 = 1/i
	tbl	v4.16b, {$invlo}, v1.16b	// vpshufb	%xmm1,	%xmm10,	%xmm4	# 4 = 1/j
	eor	v3.16b, v3.16b, v2.16b		// vpxor	%xmm2,	%xmm3,	%xmm3	# 3 = iak = 1/i + a/k
	eor	v4.16b, v4.16b, v2.16b		// vpxor	%xmm2, 	%xmm4,	%xmm4	# 4 = jak = 1/j + a/k
	tbl	v2.16b, {$invlo}, v3.16b	// vpshufb	%xmm3,	%xmm10,	%xmm2	# 2 = 1/iak
	tbl	v3.16b, {$invlo}, v4.16b	// vpshufb	%xmm4,  %xmm10,	%xmm3	# 3 = 1/jak
	eor	v2.16b, v2.16b, v1.16b		// vpxor	%xmm1,	%xmm2,	%xmm2	# 2 = io
	eor	v3.16b, v3.16b, v0.16b		// vpxor	%xmm0,  %xmm3,	%xmm3	# 3 = jo
	ld1	{v16.2d}, [x9],#16		// vmovdqu	(%r9),	%xmm0
	cbnz	w8, .Ldec_loop

	// middle of last round
						// vmovdqa	0x60(%r10),	%xmm4	# 3 : sbou
	tbl	v4.16b, {$sbou}, v2.16b		// vpshufb	%xmm2,	%xmm4,	%xmm4	# 4 = sbou
						// vmovdqa	0x70(%r10),	%xmm1	# 0 : sbot
	ld1	{v2.2d}, [x11]			// vmovdqa	-0x160(%r11),	%xmm2	# .Lk_sr-.Lk_dsbd=-0x160
	tbl	v1.16b, {$sbot}, v3.16b		// vpshufb	%xmm3,	%xmm1,	%xmm1	# 0 = sb1t
	eor	v4.16b, v4.16b, v16.16b		// vpxor	%xmm0,	%xmm4,	%xmm4	# 4 = sb1u + k
	eor	v0.16b, v1.16b, v4.16b		// vpxor	%xmm4,	%xmm1,	%xmm0	# 0 = A
	tbl	v0.16b, {v0.16b}, v2.16b	// vpshufb	%xmm2,	%xmm0,	%xmm0
	ret
.size	_vpaes_decrypt_core,.-_vpaes_decrypt_core

.globl	vpaes_decrypt
.type	vpaes_decrypt,%function
.align	4
vpaes_decrypt:
	.inst	0xd503233f			// paciasp
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0

	ld1	{v7.16b}, [$inp]
	bl	_vpaes_decrypt_preheat
	bl	_vpaes_decrypt_core
	st1	{v0.16b}, [$out]

	ldp	x29,x30,[sp],#16
	.inst	0xd50323bf			// autiasp
	ret
.size	vpaes_decrypt,.-vpaes_decrypt

// v14-v15 input, v0-v1 output
.type	_vpaes_decrypt_2x,%function
.align	4
_vpaes_decrypt_2x:
	mov	x9, $key
	ldr	w8, [$key,#240]			// pull rounds

						// vmovdqa	.Lk_dipt(%rip), %xmm2	# iptlo
	lsl	x11, x8, #4			// mov	%rax,	%r11;	shl	\$4, %r11
	eor	x11, x11, #0x30			// xor		\$0x30,	%r11
	adr	x10, .Lk_sr
	and	x11, x11, #0x30			// and		\$0x30,	%r11
	add	x11, x11, x10
	adr	x10, .Lk_mc_forward+48

	ld1	{v16.2d}, [x9],#16		// vmovdqu	(%r9),	%xmm4		# round0 key
	and	v1.16b,  v14.16b, v17.16b	// vpand	%xmm9,	%xmm0,	%xmm1
	ushr	v0.16b,  v14.16b, #4		// vpsrlb	\$4,	%xmm0,	%xmm0
	 and	v9.16b,  v15.16b, v17.16b
	 ushr	v8.16b,  v15.16b, #4
	tbl	v2.16b,  {$iptlo},v1.16b	// vpshufb	%xmm1,	%xmm2,	%xmm2
	 tbl	v10.16b, {$iptlo},v9.16b
	ld1	{v5.2d}, [x10]			// vmovdqa	.Lk_mc_forward+48(%rip), %xmm5
						// vmovdqa	.Lk_dipt+16(%rip), %xmm1 # ipthi
	tbl	v0.16b,  {$ipthi},v0.16b	// vpshufb	%xmm0,	%xmm1,	%xmm0
	 tbl	v8.16b,  {$ipthi},v8.16b
	eor	v2.16b,  v2.16b,  v16.16b	// vpxor	%xmm4,	%xmm2,	%xmm2
	 eor	v10.16b, v10.16b, v16.16b
	eor	v0.16b,  v0.16b,  v2.16b	// vpxor	%xmm2,	%xmm0,	%xmm0
	 eor	v8.16b,  v8.16b,  v10.16b
	b	.Ldec_2x_entry

.align 4
.Ldec_2x_loop:
//
//  Inverse mix columns
//
						// vmovdqa	-0x20(%r10),%xmm4		# 4 : sb9u
						// vmovdqa	-0x10(%r10),%xmm1		# 0 : sb9t
	tbl	v4.16b,  {$sb9u}, v2.16b	// vpshufb	%xmm2,	%xmm4,	%xmm4		# 4 = sb9u
	 tbl	v12.16b, {$sb9u}, v10.16b
	tbl	v1.16b,  {$sb9t}, v3.16b	// vpshufb	%xmm3,	%xmm1,	%xmm1		# 0 = sb9t
	 tbl	v9.16b,  {$sb9t}, v11.16b
	eor	v0.16b,  v4.16b,  v16.16b	// vpxor	%xmm4,	%xmm0,	%xmm0
	 eor	v8.16b,  v12.16b, v16.16b
						// vmovdqa	0x00(%r10),%xmm4		# 4 : sbdu
	eor	v0.16b,  v0.16b,  v1.16b	// vpxor	%xmm1,	%xmm0,	%xmm0		# 0 = ch
	 eor	v8.16b,  v8.16b,  v9.16b	// vpxor	%xmm1,	%xmm0,	%xmm0		# 0 = ch
						// vmovdqa	0x10(%r10),%xmm1		# 0 : sbdt

	tbl	v4.16b,  {$sbdu}, v2.16b	// vpshufb	%xmm2,	%xmm4,	%xmm4		# 4 = sbdu
	 tbl	v12.16b, {$sbdu}, v10.16b
	tbl 	v0.16b,  {v0.16b},v5.16b	// vpshufb	%xmm5,	%xmm0,	%xmm0		# MC ch
	 tbl 	v8.16b,  {v8.16b},v5.16b
	tbl	v1.16b,  {$sbdt}, v3.16b	// vpshufb	%xmm3,	%xmm1,	%xmm1		# 0 = sbdt
	 tbl	v9.16b,  {$sbdt}, v11.16b
	eor	v0.16b,  v0.16b,  v4.16b	// vpxor	%xmm4,	%xmm0,	%xmm0		# 4 = ch
	 eor	v8.16b,  v8.16b,  v12.16b
						// vmovdqa	0x20(%r10),	%xmm4		# 4 : sbbu
	eor	v0.16b,  v0.16b,  v1.16b	// vpxor	%xmm1,	%xmm0,	%xmm0		# 0 = ch
	 eor	v8.16b,  v8.16b,  v9.16b
						// vmovdqa	0x30(%r10),	%xmm1		# 0 : sbbt

	tbl	v4.16b,  {$sbbu}, v2.16b	// vpshufb	%xmm2,	%xmm4,	%xmm4		# 4 = sbbu
	 tbl	v12.16b, {$sbbu}, v10.16b
	tbl	v0.16b,  {v0.16b},v5.16b	// vpshufb	%xmm5,	%xmm0,	%xmm0		# MC ch
	 tbl	v8.16b,  {v8.16b},v5.16b
	tbl	v1.16b,  {$sbbt}, v3.16b	// vpshufb	%xmm3,	%xmm1,	%xmm1		# 0 = sbbt
	 tbl	v9.16b,  {$sbbt}, v11.16b
	eor	v0.16b,  v0.16b,  v4.16b	// vpxor	%xmm4,	%xmm0,	%xmm0		# 4 = ch
	 eor	v8.16b,  v8.16b,  v12.16b
						// vmovdqa	0x40(%r10),	%xmm4		# 4 : sbeu
	eor	v0.16b,  v0.16b,  v1.16b	// vpxor	%xmm1,	%xmm0,	%xmm0		# 0 = ch
	 eor	v8.16b,  v8.16b,  v9.16b
						// vmovdqa	0x50(%r10),	%xmm1		# 0 : sbet

	tbl	v4.16b,  {$sbeu}, v2.16b	// vpshufb	%xmm2,	%xmm4,	%xmm4		# 4 = sbeu
	 tbl	v12.16b, {$sbeu}, v10.16b
	tbl	v0.16b,  {v0.16b},v5.16b	// vpshufb	%xmm5,	%xmm0,	%xmm0		# MC ch
	 tbl	v8.16b,  {v8.16b},v5.16b
	tbl	v1.16b,  {$sbet}, v3.16b	// vpshufb	%xmm3,	%xmm1,	%xmm1		# 0 = sbet
	 tbl	v9.16b,  {$sbet}, v11.16b
	eor	v0.16b,  v0.16b,  v4.16b	// vpxor	%xmm4,	%xmm0,	%xmm0		# 4 = ch
	 eor	v8.16b,  v8.16b,  v12.16b
	ext	v5.16b,  v5.16b,  v5.16b, #12	// vpalignr \$12,	%xmm5,	%xmm5,	%xmm5
	eor	v0.16b,  v0.16b,  v1.16b	// vpxor	%xmm1,	%xmm0,	%xmm0		# 0 = ch
	 eor	v8.16b,  v8.16b,  v9.16b
	sub	w8, w8, #1			// sub		\$1,%rax			# nr--

.Ldec_2x_entry:
	// top of round
	and	v1.16b,  v0.16b,  v17.16b	// vpand	%xmm9,	%xmm0,	%xmm1	# 0 = k
	ushr	v0.16b,  v0.16b,  #4		// vpsrlb	\$4,	%xmm0,	%xmm0	# 1 = i
	 and	v9.16b,  v8.16b,  v17.16b
	 ushr	v8.16b,  v8.16b,  #4
	tbl	v2.16b,  {$invhi},v1.16b	// vpshufb	%xmm1,	%xmm11,	%xmm2	# 2 = a/k
	 tbl	v10.16b, {$invhi},v9.16b
	eor	v1.16b,	 v1.16b,  v0.16b	// vpxor	%xmm0,	%xmm1,	%xmm1	# 0 = j
	 eor	v9.16b,	 v9.16b,  v8.16b
	tbl	v3.16b,  {$invlo},v0.16b	// vpshufb	%xmm0, 	%xmm10,	%xmm3	# 3 = 1/i
	 tbl	v11.16b, {$invlo},v8.16b
	tbl	v4.16b,  {$invlo},v1.16b	// vpshufb	%xmm1,	%xmm10,	%xmm4	# 4 = 1/j
	 tbl	v12.16b, {$invlo},v9.16b
	eor	v3.16b,  v3.16b,  v2.16b	// vpxor	%xmm2,	%xmm3,	%xmm3	# 3 = iak = 1/i + a/k
	 eor	v11.16b, v11.16b, v10.16b
	eor	v4.16b,  v4.16b,  v2.16b	// vpxor	%xmm2, 	%xmm4,	%xmm4	# 4 = jak = 1/j + a/k
	 eor	v12.16b, v12.16b, v10.16b
	tbl	v2.16b,  {$invlo},v3.16b	// vpshufb	%xmm3,	%xmm10,	%xmm2	# 2 = 1/iak
	 tbl	v10.16b, {$invlo},v11.16b
	tbl	v3.16b,  {$invlo},v4.16b	// vpshufb	%xmm4,  %xmm10,	%xmm3	# 3 = 1/jak
	 tbl	v11.16b, {$invlo},v12.16b
	eor	v2.16b,  v2.16b,  v1.16b	// vpxor	%xmm1,	%xmm2,	%xmm2	# 2 = io
	 eor	v10.16b, v10.16b, v9.16b
	eor	v3.16b,  v3.16b,  v0.16b	// vpxor	%xmm0,  %xmm3,	%xmm3	# 3 = jo
	 eor	v11.16b, v11.16b, v8.16b
	ld1	{v16.2d}, [x9],#16		// vmovdqu	(%r9),	%xmm0
	cbnz	w8, .Ldec_2x_loop

	// middle of last round
						// vmovdqa	0x60(%r10),	%xmm4	# 3 : sbou
	tbl	v4.16b,  {$sbou}, v2.16b	// vpshufb	%xmm2,	%xmm4,	%xmm4	# 4 = sbou
	 tbl	v12.16b, {$sbou}, v10.16b
						// vmovdqa	0x70(%r10),	%xmm1	# 0 : sbot
	tbl	v1.16b,  {$sbot}, v3.16b	// vpshufb	%xmm3,	%xmm1,	%xmm1	# 0 = sb1t
	 tbl	v9.16b,  {$sbot}, v11.16b
	ld1	{v2.2d}, [x11]			// vmovdqa	-0x160(%r11),	%xmm2	# .Lk_sr-.Lk_dsbd=-0x160
	eor	v4.16b,  v4.16b,  v16.16b	// vpxor	%xmm0,	%xmm4,	%xmm4	# 4 = sb1u + k
	 eor	v12.16b, v12.16b, v16.16b
	eor	v0.16b,  v1.16b,  v4.16b	// vpxor	%xmm4,	%xmm1,	%xmm0	# 0 = A
	 eor	v8.16b,  v9.16b,  v12.16b
	tbl	v0.16b,  {v0.16b},v2.16b	// vpshufb	%xmm2,	%xmm0,	%xmm0
	 tbl	v1.16b,  {v8.16b},v2.16b
	ret
.size	_vpaes_decrypt_2x,.-_vpaes_decrypt_2x
___
}
{
my ($inp,$bits,$out,$dir)=("x0","w1","x2","w3");
my ($invlo,$invhi,$iptlo,$ipthi,$rcon) = map("v$_.16b",(18..21,8));

$code.=<<___;
########################################################
##                                                    ##
##                  AES key schedule                  ##
##                                                    ##
########################################################
.type	_vpaes_key_preheat,%function
.align	4
_vpaes_key_preheat:
	adr	x10, .Lk_inv
	movi	v16.16b, #0x5b			// .Lk_s63
	adr	x11, .Lk_sb1
	movi	v17.16b, #0x0f			// .Lk_s0F
	ld1	{v18.2d-v21.2d}, [x10]		// .Lk_inv, .Lk_ipt
	adr	x10, .Lk_dksd
	ld1	{v22.2d-v23.2d}, [x11]		// .Lk_sb1
	adr	x11, .Lk_mc_forward
	ld1	{v24.2d-v27.2d}, [x10],#64	// .Lk_dksd, .Lk_dksb
	ld1	{v28.2d-v31.2d}, [x10],#64	// .Lk_dkse, .Lk_dks9
	ld1	{v8.2d}, [x10]			// .Lk_rcon
	ld1	{v9.2d}, [x11]			// .Lk_mc_forward[0]
	ret
.size	_vpaes_key_preheat,.-_vpaes_key_preheat

.type	_vpaes_schedule_core,%function
.align	4
_vpaes_schedule_core:
	.inst	0xd503233f			// paciasp
	stp	x29, x30, [sp,#-16]!
	add	x29,sp,#0

	bl	_vpaes_key_preheat		// load the tables

	ld1	{v0.16b}, [$inp],#16		// vmovdqu	(%rdi),	%xmm0		# load key (unaligned)

	// input transform
	mov	v3.16b, v0.16b			// vmovdqa	%xmm0,	%xmm3
	bl	_vpaes_schedule_transform
	mov	v7.16b, v0.16b			// vmovdqa	%xmm0,	%xmm7

	adr	x10, .Lk_sr			// lea	.Lk_sr(%rip),%r10
	add	x8, x8, x10
	cbnz	$dir, .Lschedule_am_decrypting

	// encrypting, output zeroth round key after transform
	st1	{v0.2d}, [$out]			// vmovdqu	%xmm0,	(%rdx)
	b	.Lschedule_go

.Lschedule_am_decrypting:
	// decrypting, output zeroth round key after shiftrows
	ld1	{v1.2d}, [x8]			// vmovdqa	(%r8,%r10),	%xmm1
	tbl	v3.16b, {v3.16b}, v1.16b	// vpshufb  %xmm1,	%xmm3,	%xmm3
	st1	{v3.2d}, [$out]			// vmovdqu	%xmm3,	(%rdx)
	eor	x8, x8, #0x30			// xor	\$0x30, %r8

.Lschedule_go:
	cmp	$bits, #192			// cmp	\$192,	%esi
	b.hi	.Lschedule_256
	b.eq	.Lschedule_192
	// 128: fall though

##
##  .schedule_128
##
##  128-bit specific part of key schedule.
##
##  This schedule is really simple, because all its parts
##  are accomplished by the subroutines.
##
.Lschedule_128:
	mov	$inp, #10			// mov	\$10, %esi

.Loop_schedule_128:
	sub	$inp, $inp, #1			// dec	%esi
	bl 	_vpaes_schedule_round
	cbz 	$inp, .Lschedule_mangle_last
	bl	_vpaes_schedule_mangle		// write output
	b 	.Loop_schedule_128

##
##  .aes_schedule_192
##
##  192-bit specific part of key schedule.
##
##  The main body of this schedule is the same as the 128-bit
##  schedule, but with more smearing.  The long, high side is
##  stored in %xmm7 as before, and the short, low side is in
##  the high bits of %xmm6.
##
##  This schedule is somewhat nastier, however, because each
##  round produces 192 bits of key material, or 1.5 round keys.
##  Therefore, on each cycle we do 2 rounds and produce 3 round
##  keys.
##
.align	4
.Lschedule_192:
	sub	$inp, $inp, #8
	ld1	{v0.16b}, [$inp]		// vmovdqu	8(%rdi),%xmm0		# load key part 2 (very unaligned)
	bl	_vpaes_schedule_transform	// input transform
	mov	v6.16b, v0.16b			// vmovdqa	%xmm0,	%xmm6		# save short part
	eor	v4.16b, v4.16b, v4.16b		// vpxor	%xmm4,	%xmm4, %xmm4	# clear 4
	ins	v6.d[0], v4.d[0]		// vmovhlps	%xmm4,	%xmm6,	%xmm6		# clobber low side with zeros
	mov	$inp, #4			// mov	\$4,	%esi

.Loop_schedule_192:
	sub	$inp, $inp, #1			// dec	%esi
	bl	_vpaes_schedule_round
	ext	v0.16b, v6.16b, v0.16b, #8	// vpalignr	\$8,%xmm6,%xmm0,%xmm0
	bl	_vpaes_schedule_mangle		// save key n
	bl	_vpaes_schedule_192_smear
	bl	_vpaes_schedule_mangle		// save key n+1
	bl	_vpaes_schedule_round
	cbz 	$inp, .Lschedule_mangle_last
	bl	_vpaes_schedule_mangle		// save key n+2
	bl	_vpaes_schedule_192_smear
	b	.Loop_schedule_192

##
##  .aes_schedule_256
##
##  256-bit specific part of key schedule.
##
##  The structure here is very similar to the 128-bit
##  schedule, but with an additional "low side" in
##  %xmm6.  The low side's rounds are the same as the
##  high side's, except no rcon and no rotation.
##
.align	4
.Lschedule_256:
	ld1	{v0.16b}, [$inp]		// vmovdqu	16(%rdi),%xmm0		# load key part 2 (unaligned)
	bl	_vpaes_schedule_transform	// input transform
	mov	$inp, #7			// mov	\$7, %esi

.Loop_schedule_256:
	sub	$inp, $inp, #1			// dec	%esi
	bl	_vpaes_schedule_mangle		// output low result
	mov	v6.16b, v0.16b			// vmovdqa	%xmm0,	%xmm6		# save cur_lo in xmm6

	// high round
	bl	_vpaes_schedule_round
	cbz 	$inp, .Lschedule_mangle_last
	bl	_vpaes_schedule_mangle

	// low round. swap xmm7 and xmm6
	dup	v0.4s, v0.s[3]			// vpshufd	\$0xFF,	%xmm0,	%xmm0
	movi	v4.16b, #0
	mov	v5.16b, v7.16b			// vmovdqa	%xmm7,	%xmm5
	mov	v7.16b, v6.16b			// vmovdqa	%xmm6,	%xmm7
	bl	_vpaes_schedule_low_round
	mov	v7.16b, v5.16b			// vmovdqa	%xmm5,	%xmm7

	b	.Loop_schedule_256

##
##  .aes_schedule_mangle_last
##
##  Mangler for last round of key schedule
##  Mangles %xmm0
##    when encrypting, outputs out(%xmm0) ^ 63
##    when decrypting, outputs unskew(%xmm0)
##
##  Always called right before return... jumps to cleanup and exits
##
.align	4
.Lschedule_mangle_last:
	// schedule last round key from xmm0
	adr	x11, .Lk_deskew			// lea	.Lk_deskew(%rip),%r11	# prepare to deskew
	cbnz	$dir, .Lschedule_mangle_last_dec

	// encrypting
	ld1	{v1.2d}, [x8]			// vmovdqa	(%r8,%r10),%xmm1
	adr	x11, .Lk_opt			// lea	.Lk_opt(%rip),	%r11		# prepare to output transform
	add	$out, $out, #32			// add	\$32,	%rdx
	tbl	v0.16b, {v0.16b}, v1.16b	// vpshufb	%xmm1,	%xmm0,	%xmm0		# output permute

.Lschedule_mangle_last_dec:
	ld1	{v20.2d-v21.2d}, [x11]		// reload constants
	sub	$out, $out, #16			// add	\$-16,	%rdx
	eor	v0.16b, v0.16b, v16.16b		// vpxor	.Lk_s63(%rip),	%xmm0,	%xmm0
	bl	_vpaes_schedule_transform	// output transform
	st1	{v0.2d}, [$out]			// vmovdqu	%xmm0,	(%rdx)		# save last key

	// cleanup
	eor	v0.16b, v0.16b, v0.16b		// vpxor	%xmm0,	%xmm0,	%xmm0
	eor	v1.16b, v1.16b, v1.16b		// vpxor	%xmm1,	%xmm1,	%xmm1
	eor	v2.16b, v2.16b, v2.16b		// vpxor	%xmm2,	%xmm2,	%xmm2
	eor	v3.16b, v3.16b, v3.16b		// vpxor	%xmm3,	%xmm3,	%xmm3
	eor	v4.16b, v4.16b, v4.16b		// vpxor	%xmm4,	%xmm4,	%xmm4
	eor	v5.16b, v5.16b, v5.16b		// vpxor	%xmm5,	%xmm5,	%xmm5
	eor	v6.16b, v6.16b, v6.16b		// vpxor	%xmm6,	%xmm6,	%xmm6
	eor	v7.16b, v7.16b, v7.16b		// vpxor	%xmm7,	%xmm7,	%xmm7
	ldp	x29, x30, [sp],#16
	.inst	0xd50323bf			// autiasp
	ret
.size	_vpaes_schedule_core,.-_vpaes_schedule_core

##
##  .aes_schedule_192_smear
##
##  Smear the short, low side in the 192-bit key schedule.
##
##  Inputs:
##    %xmm7: high side, b  a  x  y
##    %xmm6:  low side, d  c  0  0
##    %xmm13: 0
##
##  Outputs:
##    %xmm6: b+c+d  b+c  0  0
##    %xmm0: b+c+d  b+c  b  a
##
.type	_vpaes_schedule_192_smear,%function
.align	4
_vpaes_schedule_192_smear:
	movi	v1.16b, #0
	dup	v0.4s, v7.s[3]
	ins	v1.s[3], v6.s[2]	// vpshufd	\$0x80,	%xmm6,	%xmm1	# d c 0 0 -> c 0 0 0
	ins	v0.s[0], v7.s[2]	// vpshufd	\$0xFE,	%xmm7,	%xmm0	# b a _ _ -> b b b a
	eor	v6.16b, v6.16b, v1.16b	// vpxor	%xmm1,	%xmm6,	%xmm6	# -> c+d c 0 0
	eor	v1.16b, v1.16b, v1.16b	// vpxor	%xmm1,	%xmm1,	%xmm1
	eor	v6.16b, v6.16b, v0.16b	// vpxor	%xmm0,	%xmm6,	%xmm6	# -> b+c+d b+c b a
	mov	v0.16b, v6.16b		// vmovdqa	%xmm6,	%xmm0
	ins	v6.d[0], v1.d[0]	// vmovhlps	%xmm1,	%xmm6,	%xmm6	# clobber low side with zeros
	ret
.size	_vpaes_schedule_192_smear,.-_vpaes_schedule_192_smear

##
##  .aes_schedule_round
##
##  Runs one main round of the key schedule on %xmm0, %xmm7
##
##  Specifically, runs subbytes on the high dword of %xmm0
##  then rotates it by one byte and xors into the low dword of
##  %xmm7.
##
##  Adds rcon from low byte of %xmm8, then rotates %xmm8 for
##  next rcon.
##
##  Smears the dwords of %xmm7 by xoring the low into the
##  second low, result into third, result into highest.
##
##  Returns results in %xmm7 = %xmm0.
##  Clobbers %xmm1-%xmm4, %r11.
##
.type	_vpaes_schedule_round,%function
.align	4
_vpaes_schedule_round:
	// extract rcon from xmm8
	movi	v4.16b, #0			// vpxor	%xmm4,	%xmm4,	%xmm4
	ext	v1.16b, $rcon, v4.16b, #15	// vpalignr	\$15,	%xmm8,	%xmm4,	%xmm1
	ext	$rcon, $rcon, $rcon, #15	// vpalignr	\$15,	%xmm8,	%xmm8,	%xmm8
	eor	v7.16b, v7.16b, v1.16b		// vpxor	%xmm1,	%xmm7,	%xmm7

	// rotate
	dup	v0.4s, v0.s[3]			// vpshufd	\$0xFF,	%xmm0,	%xmm0
	ext	v0.16b, v0.16b, v0.16b, #1	// vpalignr	\$1,	%xmm0,	%xmm0,	%xmm0

	// fall through...

	// low round: same as high round, but no rotation and no rcon.
_vpaes_schedule_low_round:
	// smear xmm7
	ext	v1.16b, v4.16b, v7.16b, #12	// vpslldq	\$4,	%xmm7,	%xmm1
	eor	v7.16b, v7.16b, v1.16b		// vpxor	%xmm1,	%xmm7,	%xmm7
	ext	v4.16b, v4.16b, v7.16b, #8	// vpslldq	\$8,	%xmm7,	%xmm4

	// subbytes
	and	v1.16b, v0.16b, v17.16b		// vpand	%xmm9,	%xmm0,	%xmm1		# 0 = k
	ushr	v0.16b, v0.16b, #4		// vpsrlb	\$4,	%xmm0,	%xmm0		# 1 = i
	 eor	v7.16b, v7.16b, v4.16b		// vpxor	%xmm4,	%xmm7,	%xmm7
	tbl	v2.16b, {$invhi}, v1.16b	// vpshufb	%xmm1,	%xmm11,	%xmm2		# 2 = a/k
	eor	v1.16b, v1.16b, v0.16b		// vpxor	%xmm0,	%xmm1,	%xmm1		# 0 = j
	tbl	v3.16b, {$invlo}, v0.16b	// vpshufb	%xmm0, 	%xmm10,	%xmm3		# 3 = 1/i
	eor	v3.16b, v3.16b, v2.16b		// vpxor	%xmm2,	%xmm3,	%xmm3		# 3 = iak = 1/i + a/k
	tbl	v4.16b, {$invlo}, v1.16b	// vpshufb	%xmm1,	%xmm10,	%xmm4		# 4 = 1/j
	 eor	v7.16b, v7.16b, v16.16b		// vpxor	.Lk_s63(%rip),	%xmm7,	%xmm7
	tbl	v3.16b, {$invlo}, v3.16b	// vpshufb	%xmm3,	%xmm10,	%xmm3		# 2 = 1/iak
	eor	v4.16b, v4.16b, v2.16b		// vpxor	%xmm2,	%xmm4,	%xmm4		# 4 = jak = 1/j + a/k
	tbl	v2.16b, {$invlo}, v4.16b	// vpshufb	%xmm4,	%xmm10,	%xmm2		# 3 = 1/jak
	eor	v3.16b, v3.16b, v1.16b		// vpxor	%xmm1,	%xmm3,	%xmm3		# 2 = io
	eor	v2.16b, v2.16b, v0.16b		// vpxor	%xmm0,	%xmm2,	%xmm2		# 3 = jo
	tbl	v4.16b, {v23.16b}, v3.16b	// vpshufb	%xmm3,	%xmm13,	%xmm4		# 4 = sbou
	tbl	v1.16b, {v22.16b}, v2.16b	// vpshufb	%xmm2,	%xmm12,	%xmm1		# 0 = sb1t
	eor	v1.16b, v1.16b, v4.16b		// vpxor	%xmm4,	%xmm1,	%xmm1		# 0 = sbox output

	// add in smeared stuff
	eor	v0.16b, v1.16b, v7.16b		// vpxor	%xmm7,	%xmm1,	%xmm0
	eor	v7.16b, v1.16b, v7.16b		// vmovdqa	%xmm0,	%xmm7
	ret
.size	_vpaes_schedule_round,.-_vpaes_schedule_round

##
##  .aes_schedule_transform
##
##  Linear-transform %xmm0 according to tables at (%r11)
##
##  Requires that %xmm9 = 0x0F0F... as in preheat
##  Output in %xmm0
##  Clobbers %xmm1, %xmm2
##
.type	_vpaes_schedule_transform,%function
.align	4
_vpaes_schedule_transform:
	and	v1.16b, v0.16b, v17.16b		// vpand	%xmm9,	%xmm0,	%xmm1
	ushr	v0.16b, v0.16b, #4		// vpsrlb	\$4,	%xmm0,	%xmm0
						// vmovdqa	(%r11),	%xmm2 	# lo
	tbl	v2.16b, {$iptlo}, v1.16b	// vpshufb	%xmm1,	%xmm2,	%xmm2
						// vmovdqa	16(%r11),	%xmm1 # hi
	tbl	v0.16b, {$ipthi}, v0.16b	// vpshufb	%xmm0,	%xmm1,	%xmm0
	eor	v0.16b, v0.16b, v2.16b		// vpxor	%xmm2,	%xmm0,	%xmm0
	ret
.size	_vpaes_schedule_transform,.-_vpaes_schedule_transform

##
##  .aes_schedule_mangle
##
##  Mangle xmm0 from (basis-transformed) standard version
##  to our version.
##
##  On encrypt,
##    xor with 0x63
##    multiply by circulant 0,1,1,1
##    apply shiftrows transform
##
##  On decrypt,
##    xor with 0x63
##    multiply by "inverse mixcolumns" circulant E,B,D,9
##    deskew
##    apply shiftrows transform
##
##
##  Writes out to (%rdx), and increments or decrements it
##  Keeps track of round number mod 4 in %r8
##  Preserves xmm0
##  Clobbers xmm1-xmm5
##
.type	_vpaes_schedule_mangle,%function
.align	4
_vpaes_schedule_mangle:
	mov	v4.16b, v0.16b			// vmovdqa	%xmm0,	%xmm4	# save xmm0 for later
						// vmovdqa	.Lk_mc_forward(%rip),%xmm5
	cbnz	$dir, .Lschedule_mangle_dec

	// encrypting
	eor	v4.16b, v0.16b, v16.16b		// vpxor	.Lk_s63(%rip),	%xmm0,	%xmm4
	add	$out, $out, #16			// add	\$16,	%rdx
	tbl	v4.16b, {v4.16b}, v9.16b	// vpshufb	%xmm5,	%xmm4,	%xmm4
	tbl	v1.16b, {v4.16b}, v9.16b	// vpshufb	%xmm5,	%xmm4,	%xmm1
	tbl	v3.16b, {v1.16b}, v9.16b	// vpshufb	%xmm5,	%xmm1,	%xmm3
	eor	v4.16b, v4.16b, v1.16b		// vpxor	%xmm1,	%xmm4,	%xmm4
	ld1	{v1.2d}, [x8]			// vmovdqa	(%r8,%r10),	%xmm1
	eor	v3.16b, v3.16b, v4.16b		// vpxor	%xmm4,	%xmm3,	%xmm3

	b	.Lschedule_mangle_both
.align	4
.Lschedule_mangle_dec:
	// inverse mix columns
						// lea	.Lk_dksd(%rip),%r11
	ushr	v1.16b, v4.16b, #4		// vpsrlb	\$4,	%xmm4,	%xmm1	# 1 = hi
	and	v4.16b, v4.16b, v17.16b		// vpand	%xmm9,	%xmm4,	%xmm4	# 4 = lo

						// vmovdqa	0x00(%r11),	%xmm2
	tbl	v2.16b, {v24.16b}, v4.16b	// vpshufb	%xmm4,	%xmm2,	%xmm2
						// vmovdqa	0x10(%r11),	%xmm3
	tbl	v3.16b,	{v25.16b}, v1.16b	// vpshufb	%xmm1,	%xmm3,	%xmm3
	eor	v3.16b, v3.16b, v2.16b		// vpxor	%xmm2,	%xmm3,	%xmm3
	tbl	v3.16b, {v3.16b}, v9.16b	// vpshufb	%xmm5,	%xmm3,	%xmm3

						// vmovdqa	0x20(%r11),	%xmm2
	tbl	v2.16b, {v26.16b}, v4.16b	// vpshufb	%xmm4,	%xmm2,	%xmm2
	eor	v2.16b, v2.16b, v3.16b		// vpxor	%xmm3,	%xmm2,	%xmm2
						// vmovdqa	0x30(%r11),	%xmm3
	tbl	v3.16b, {v27.16b}, v1.16b	// vpshufb	%xmm1,	%xmm3,	%xmm3
	eor	v3.16b, v3.16b, v2.16b		// vpxor	%xmm2,	%xmm3,	%xmm3
	tbl	v3.16b, {v3.16b}, v9.16b	// vpshufb	%xmm5,	%xmm3,	%xmm3

						// vmovdqa	0x40(%r11),	%xmm2
	tbl	v2.16b, {v28.16b}, v4.16b	// vpshufb	%xmm4,	%xmm2,	%xmm2
	eor	v2.16b, v2.16b, v3.16b		// vpxor	%xmm3,	%xmm2,	%xmm2
						// vmovdqa	0x50(%r11),	%xmm3
	tbl	v3.16b, {v29.16b}, v1.16b	// vpshufb	%xmm1,	%xmm3,	%xmm3
	eor	v3.16b, v3.16b, v2.16b		// vpxor	%xmm2,	%xmm3,	%xmm3

						// vmovdqa	0x60(%r11),	%xmm2
	tbl	v2.16b, {v30.16b}, v4.16b	// vpshufb	%xmm4,	%xmm2,	%xmm2
	tbl	v3.16b, {v3.16b}, v9.16b	// vpshufb	%xmm5,	%xmm3,	%xmm3
						// vmovdqa	0x70(%r11),	%xmm4
	tbl	v4.16b, {v31.16b}, v1.16b	// vpshufb	%xmm1,	%xmm4,	%xmm4
	ld1	{v1.2d}, [x8]			// vmovdqa	(%r8,%r10),	%xmm1
	eor	v2.16b, v2.16b, v3.16b		// vpxor	%xmm3,	%xmm2,	%xmm2
	eor	v3.16b, v4.16b, v2.16b		// vpxor	%xmm2,	%xmm4,	%xmm3

	sub	$out, $out, #16			// add	\$-16,	%rdx

.Lschedule_mangle_both:
	tbl	v3.16b, {v3.16b}, v1.16b	// vpshufb	%xmm1,	%xmm3,	%xmm3
	add	x8, x8, #64-16			// add	\$-16,	%r8
	and	x8, x8, #~(1<<6)		// and	\$0x30,	%r8
	st1	{v3.2d}, [$out]			// vmovdqu	%xmm3,	(%rdx)
	ret
.size	_vpaes_schedule_mangle,.-_vpaes_schedule_mangle

.globl	vpaes_set_encrypt_key
.type	vpaes_set_encrypt_key,%function
.align	4
vpaes_set_encrypt_key:
	.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
	stp	d8,d9,[sp,#-16]!	// ABI spec says so

	lsr	w9, $bits, #5		// shr	\$5,%eax
	add	w9, w9, #5		// \$5,%eax
	str	w9, [$out,#240]		// mov	%eax,240(%rdx)	# AES_KEY->rounds = nbits/32+5;

	mov	$dir, #0		// mov	\$0,%ecx
	mov	x8, #0x30		// mov	\$0x30,%r8d
	bl	_vpaes_schedule_core
	eor	x0, x0, x0

	ldp	d8,d9,[sp],#16
	ldp	x29,x30,[sp],#16
	.inst	0xd50323bf		// autiasp
	ret
.size	vpaes_set_encrypt_key,.-vpaes_set_encrypt_key

.globl	vpaes_set_decrypt_key
.type	vpaes_set_decrypt_key,%function
.align	4
vpaes_set_decrypt_key:
	.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
	stp	d8,d9,[sp,#-16]!	// ABI spec says so

	lsr	w9, $bits, #5		// shr	\$5,%eax
	add	w9, w9, #5		// \$5,%eax
	str	w9, [$out,#240]		// mov	%eax,240(%rdx)	# AES_KEY->rounds = nbits/32+5;
	lsl	w9, w9, #4		// shl	\$4,%eax
	add	$out, $out, #16		// lea	16(%rdx,%rax),%rdx
	add	$out, $out, x9

	mov	$dir, #1		// mov	\$1,%ecx
	lsr	w8, $bits, #1		// shr	\$1,%r8d
	and	x8, x8, #32		// and	\$32,%r8d
	eor	x8, x8, #32		// xor	\$32,%r8d	# nbits==192?0:32
	bl	_vpaes_schedule_core

	ldp	d8,d9,[sp],#16
	ldp	x29,x30,[sp],#16
	.inst	0xd50323bf		// autiasp
	ret
.size	vpaes_set_decrypt_key,.-vpaes_set_decrypt_key
___
}
{
my ($inp,$out,$len,$key,$ivec,$dir) = map("x$_",(0..5));

$code.=<<___;
.globl	vpaes_cbc_encrypt
.type	vpaes_cbc_encrypt,%function
.align	4
vpaes_cbc_encrypt:
	cbz	$len, .Lcbc_abort
	cmp	w5, #0			// check direction
	b.eq	vpaes_cbc_decrypt

	.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0

	mov	x17, $len		// reassign
	mov	x2,  $key		// reassign

	ld1	{v0.16b}, [$ivec]	// load ivec
	bl	_vpaes_encrypt_preheat
	b	.Lcbc_enc_loop

.align	4
.Lcbc_enc_loop:
	ld1	{v7.16b}, [$inp],#16	// load input
	eor	v7.16b, v7.16b, v0.16b	// xor with ivec
	bl	_vpaes_encrypt_core
	st1	{v0.16b}, [$out],#16	// save output
	subs	x17, x17, #16
	b.hi	.Lcbc_enc_loop

	st1	{v0.16b}, [$ivec]	// write ivec

	ldp	x29,x30,[sp],#16
	.inst	0xd50323bf		// autiasp
.Lcbc_abort:
	ret
.size	vpaes_cbc_encrypt,.-vpaes_cbc_encrypt

.type	vpaes_cbc_decrypt,%function
.align	4
vpaes_cbc_decrypt:
	.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
	stp	d8,d9,[sp,#-16]!	// ABI spec says so
	stp	d10,d11,[sp,#-16]!
	stp	d12,d13,[sp,#-16]!
	stp	d14,d15,[sp,#-16]!

	mov	x17, $len		// reassign
	mov	x2,  $key		// reassign
	ld1	{v6.16b}, [$ivec]	// load ivec
	bl	_vpaes_decrypt_preheat
	tst	x17, #16
	b.eq	.Lcbc_dec_loop2x

	ld1	{v7.16b}, [$inp], #16	// load input
	bl	_vpaes_decrypt_core
	eor	v0.16b, v0.16b, v6.16b	// xor with ivec
	orr	v6.16b, v7.16b, v7.16b	// next ivec value
	st1	{v0.16b}, [$out], #16
	subs	x17, x17, #16
	b.ls	.Lcbc_dec_done

.align	4
.Lcbc_dec_loop2x:
	ld1	{v14.16b,v15.16b}, [$inp], #32
	bl	_vpaes_decrypt_2x
	eor	v0.16b, v0.16b, v6.16b	// xor with ivec
	eor	v1.16b, v1.16b, v14.16b
	orr	v6.16b, v15.16b, v15.16b
	st1	{v0.16b,v1.16b}, [$out], #32
	subs	x17, x17, #32
	b.hi	.Lcbc_dec_loop2x

.Lcbc_dec_done:
	st1	{v6.16b}, [$ivec]

	ldp	d14,d15,[sp],#16
	ldp	d12,d13,[sp],#16
	ldp	d10,d11,[sp],#16
	ldp	d8,d9,[sp],#16
	ldp	x29,x30,[sp],#16
	.inst	0xd50323bf		// autiasp
	ret
.size	vpaes_cbc_decrypt,.-vpaes_cbc_decrypt
___
if (1) {
$code.=<<___;
.globl	vpaes_ecb_encrypt
.type	vpaes_ecb_encrypt,%function
.align	4
vpaes_ecb_encrypt:
	.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
	stp	d8,d9,[sp,#-16]!	// ABI spec says so
	stp	d10,d11,[sp,#-16]!
	stp	d12,d13,[sp,#-16]!
	stp	d14,d15,[sp,#-16]!

	mov	x17, $len
	mov	x2,  $key
	bl	_vpaes_encrypt_preheat
	tst	x17, #16
	b.eq	.Lecb_enc_loop

	ld1	{v7.16b}, [$inp],#16
	bl	_vpaes_encrypt_core
	st1	{v0.16b}, [$out],#16
	subs	x17, x17, #16
	b.ls	.Lecb_enc_done

.align	4
.Lecb_enc_loop:
	ld1	{v14.16b,v15.16b}, [$inp], #32
	bl	_vpaes_encrypt_2x
	st1	{v0.16b,v1.16b}, [$out], #32
	subs	x17, x17, #32
	b.hi	.Lecb_enc_loop

.Lecb_enc_done:
	ldp	d14,d15,[sp],#16
	ldp	d12,d13,[sp],#16
	ldp	d10,d11,[sp],#16
	ldp	d8,d9,[sp],#16
	ldp	x29,x30,[sp],#16
	.inst	0xd50323bf		// autiasp
	ret
.size	vpaes_ecb_encrypt,.-vpaes_ecb_encrypt

.globl	vpaes_ecb_decrypt
.type	vpaes_ecb_decrypt,%function
.align	4
vpaes_ecb_decrypt:
	.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
	stp	d8,d9,[sp,#-16]!	// ABI spec says so
	stp	d10,d11,[sp,#-16]!
	stp	d12,d13,[sp,#-16]!
	stp	d14,d15,[sp,#-16]!

	mov	x17, $len
	mov	x2,  $key
	bl	_vpaes_decrypt_preheat
	tst	x17, #16
	b.eq	.Lecb_dec_loop

	ld1	{v7.16b}, [$inp],#16
	bl	_vpaes_encrypt_core
	st1	{v0.16b}, [$out],#16
	subs	x17, x17, #16
	b.ls	.Lecb_dec_done

.align	4
.Lecb_dec_loop:
	ld1	{v14.16b,v15.16b}, [$inp], #32
	bl	_vpaes_decrypt_2x
	st1	{v0.16b,v1.16b}, [$out], #32
	subs	x17, x17, #32
	b.hi	.Lecb_dec_loop

.Lecb_dec_done:
	ldp	d14,d15,[sp],#16
	ldp	d12,d13,[sp],#16
	ldp	d10,d11,[sp],#16
	ldp	d8,d9,[sp],#16
	ldp	x29,x30,[sp],#16
	.inst	0xd50323bf		// autiasp
	ret
.size	vpaes_ecb_decrypt,.-vpaes_ecb_decrypt
___
}	}
print $code;

close STDOUT;

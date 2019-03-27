#! /usr/bin/env perl
# Copyright 2012-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Written by David S. Miller and Andy Polyakov.
# The module is licensed under 2-clause BSD license. October 2012.
# All rights reserved.
# ====================================================================

######################################################################
# AES for SPARC T4.
#
# AES round instructions complete in 3 cycles and can be issued every
# cycle. It means that round calculations should take 4*rounds cycles,
# because any given round instruction depends on result of *both*
# previous instructions:
#
#	|0 |1 |2 |3 |4
#	|01|01|01|
#	   |23|23|23|
#	            |01|01|...
#	               |23|...
#
# Provided that fxor [with IV] takes 3 cycles to complete, critical
# path length for CBC encrypt would be 3+4*rounds, or in other words
# it should process one byte in at least (3+4*rounds)/16 cycles. This
# estimate doesn't account for "collateral" instructions, such as
# fetching input from memory, xor-ing it with zero-round key and
# storing the result. Yet, *measured* performance [for data aligned
# at 64-bit boundary!] deviates from this equation by less than 0.5%:
#
#		128-bit key	192-		256-
# CBC encrypt	2.70/2.90(*)	3.20/3.40	3.70/3.90
#			 (*) numbers after slash are for
#			     misaligned data.
#
# Out-of-order execution logic managed to fully overlap "collateral"
# instructions with those on critical path. Amazing!
#
# As with Intel AES-NI, question is if it's possible to improve
# performance of parallelizable modes by interleaving round
# instructions. Provided round instruction latency and throughput
# optimal interleave factor is 2. But can we expect 2x performance
# improvement? Well, as round instructions can be issued one per
# cycle, they don't saturate the 2-way issue pipeline and therefore
# there is room for "collateral" calculations... Yet, 2x speed-up
# over CBC encrypt remains unattaintable:
#
#		128-bit key	192-		256-
# CBC decrypt	1.64/2.11	1.89/2.37	2.23/2.61
# CTR		1.64/2.08(*)	1.89/2.33	2.23/2.61
#			 (*) numbers after slash are for
#			     misaligned data.
#
# Estimates based on amount of instructions under assumption that
# round instructions are not pairable with any other instruction
# suggest that latter is the actual case and pipeline runs
# underutilized. It should be noted that T4 out-of-order execution
# logic is so capable that performance gain from 2x interleave is
# not even impressive, ~7-13% over non-interleaved code, largest
# for 256-bit keys.

# To anchor to something else, software implementation processes
# one byte in 29 cycles with 128-bit key on same processor. Intel
# Sandy Bridge encrypts byte in 5.07 cycles in CBC mode and decrypts
# in 0.93, naturally with AES-NI.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "sparcv9_modes.pl";

$output = pop;
open STDOUT,">$output";

$::evp=1;	# if $evp is set to 0, script generates module with
# AES_[en|de]crypt, AES_set_[en|de]crypt_key and AES_cbc_encrypt entry
# points. These however are not fully compatible with openssl/aes.h,
# because they expect AES_KEY to be aligned at 64-bit boundary. When
# used through EVP, alignment is arranged at EVP layer. Second thing
# that is arranged by EVP is at least 32-bit alignment of IV.

######################################################################
# single-round subroutines
#
{
my ($inp,$out,$key,$rounds,$tmp,$mask)=map("%o$_",(0..5));

$code.=<<___;
#include "sparc_arch.h"

#ifdef	__arch64__
.register	%g2,#scratch
.register	%g3,#scratch
#endif

.text

.globl	aes_t4_encrypt
.align	32
aes_t4_encrypt:
	andcc		$inp, 7, %g1		! is input aligned?
	andn		$inp, 7, $inp

	ldx		[$key + 0], %g4
	ldx		[$key + 8], %g5

	ldx		[$inp + 0], %o4
	bz,pt		%icc, 1f
	ldx		[$inp + 8], %o5
	ldx		[$inp + 16], $inp
	sll		%g1, 3, %g1
	sub		%g0, %g1, %o3
	sllx		%o4, %g1, %o4
	sllx		%o5, %g1, %g1
	srlx		%o5, %o3, %o5
	srlx		$inp, %o3, %o3
	or		%o5, %o4, %o4
	or		%o3, %g1, %o5
1:
	ld		[$key + 240], $rounds
	ldd		[$key + 16], %f12
	ldd		[$key + 24], %f14
	xor		%g4, %o4, %o4
	xor		%g5, %o5, %o5
	movxtod		%o4, %f0
	movxtod		%o5, %f2
	srl		$rounds, 1, $rounds
	ldd		[$key + 32], %f16
	sub		$rounds, 1, $rounds
	ldd		[$key + 40], %f18
	add		$key, 48, $key

.Lenc:
	aes_eround01	%f12, %f0, %f2, %f4
	aes_eround23	%f14, %f0, %f2, %f2
	ldd		[$key + 0], %f12
	ldd		[$key + 8], %f14
	sub		$rounds,1,$rounds
	aes_eround01	%f16, %f4, %f2, %f0
	aes_eround23	%f18, %f4, %f2, %f2
	ldd		[$key + 16], %f16
	ldd		[$key + 24], %f18
	brnz,pt		$rounds, .Lenc
	add		$key, 32, $key

	andcc		$out, 7, $tmp		! is output aligned?
	aes_eround01	%f12, %f0, %f2, %f4
	aes_eround23	%f14, %f0, %f2, %f2
	aes_eround01_l	%f16, %f4, %f2, %f0
	aes_eround23_l	%f18, %f4, %f2, %f2

	bnz,pn		%icc, 2f
	nop

	std		%f0, [$out + 0]
	retl
	std		%f2, [$out + 8]

2:	alignaddrl	$out, %g0, $out
	mov		0xff, $mask
	srl		$mask, $tmp, $mask

	faligndata	%f0, %f0, %f4
	faligndata	%f0, %f2, %f6
	faligndata	%f2, %f2, %f8

	stda		%f4, [$out + $mask]0xc0	! partial store
	std		%f6, [$out + 8]
	add		$out, 16, $out
	orn		%g0, $mask, $mask
	retl
	stda		%f8, [$out + $mask]0xc0	! partial store
.type	aes_t4_encrypt,#function
.size	aes_t4_encrypt,.-aes_t4_encrypt

.globl	aes_t4_decrypt
.align	32
aes_t4_decrypt:
	andcc		$inp, 7, %g1		! is input aligned?
	andn		$inp, 7, $inp

	ldx		[$key + 0], %g4
	ldx		[$key + 8], %g5

	ldx		[$inp + 0], %o4
	bz,pt		%icc, 1f
	ldx		[$inp + 8], %o5
	ldx		[$inp + 16], $inp
	sll		%g1, 3, %g1
	sub		%g0, %g1, %o3
	sllx		%o4, %g1, %o4
	sllx		%o5, %g1, %g1
	srlx		%o5, %o3, %o5
	srlx		$inp, %o3, %o3
	or		%o5, %o4, %o4
	or		%o3, %g1, %o5
1:
	ld		[$key + 240], $rounds
	ldd		[$key + 16], %f12
	ldd		[$key + 24], %f14
	xor		%g4, %o4, %o4
	xor		%g5, %o5, %o5
	movxtod		%o4, %f0
	movxtod		%o5, %f2
	srl		$rounds, 1, $rounds
	ldd		[$key + 32], %f16
	sub		$rounds, 1, $rounds
	ldd		[$key + 40], %f18
	add		$key, 48, $key

.Ldec:
	aes_dround01	%f12, %f0, %f2, %f4
	aes_dround23	%f14, %f0, %f2, %f2
	ldd		[$key + 0], %f12
	ldd		[$key + 8], %f14
	sub		$rounds,1,$rounds
	aes_dround01	%f16, %f4, %f2, %f0
	aes_dround23	%f18, %f4, %f2, %f2
	ldd		[$key + 16], %f16
	ldd		[$key + 24], %f18
	brnz,pt		$rounds, .Ldec
	add		$key, 32, $key

	andcc		$out, 7, $tmp		! is output aligned?
	aes_dround01	%f12, %f0, %f2, %f4
	aes_dround23	%f14, %f0, %f2, %f2
	aes_dround01_l	%f16, %f4, %f2, %f0
	aes_dround23_l	%f18, %f4, %f2, %f2

	bnz,pn		%icc, 2f
	nop

	std		%f0, [$out + 0]
	retl
	std		%f2, [$out + 8]

2:	alignaddrl	$out, %g0, $out
	mov		0xff, $mask
	srl		$mask, $tmp, $mask

	faligndata	%f0, %f0, %f4
	faligndata	%f0, %f2, %f6
	faligndata	%f2, %f2, %f8

	stda		%f4, [$out + $mask]0xc0	! partial store
	std		%f6, [$out + 8]
	add		$out, 16, $out
	orn		%g0, $mask, $mask
	retl
	stda		%f8, [$out + $mask]0xc0	! partial store
.type	aes_t4_decrypt,#function
.size	aes_t4_decrypt,.-aes_t4_decrypt
___
}

######################################################################
# key setup subroutines
#
{
my ($inp,$bits,$out,$tmp)=map("%o$_",(0..5));
$code.=<<___;
.globl	aes_t4_set_encrypt_key
.align	32
aes_t4_set_encrypt_key:
.Lset_encrypt_key:
	and		$inp, 7, $tmp
	alignaddr	$inp, %g0, $inp
	cmp		$bits, 192
	ldd		[$inp + 0], %f0
	bl,pt		%icc,.L128
	ldd		[$inp + 8], %f2

	be,pt		%icc,.L192
	ldd		[$inp + 16], %f4
	brz,pt		$tmp, .L256aligned
	ldd		[$inp + 24], %f6

	ldd		[$inp + 32], %f8
	faligndata	%f0, %f2, %f0
	faligndata	%f2, %f4, %f2
	faligndata	%f4, %f6, %f4
	faligndata	%f6, %f8, %f6
.L256aligned:
___
for ($i=0; $i<6; $i++) {
    $code.=<<___;
	std		%f0, [$out + `32*$i+0`]
	aes_kexpand1	%f0, %f6, $i, %f0
	std		%f2, [$out + `32*$i+8`]
	aes_kexpand2	%f2, %f0, %f2
	std		%f4, [$out + `32*$i+16`]
	aes_kexpand0	%f4, %f2, %f4
	std		%f6, [$out + `32*$i+24`]
	aes_kexpand2	%f6, %f4, %f6
___
}
$code.=<<___;
	std		%f0, [$out + `32*$i+0`]
	aes_kexpand1	%f0, %f6, $i, %f0
	std		%f2, [$out + `32*$i+8`]
	aes_kexpand2	%f2, %f0, %f2
	std		%f4, [$out + `32*$i+16`]
	std		%f6, [$out + `32*$i+24`]
	std		%f0, [$out + `32*$i+32`]
	std		%f2, [$out + `32*$i+40`]

	mov		14, $tmp
	st		$tmp, [$out + 240]
	retl
	xor		%o0, %o0, %o0

.align	16
.L192:
	brz,pt		$tmp, .L192aligned
	nop

	ldd		[$inp + 24], %f6
	faligndata	%f0, %f2, %f0
	faligndata	%f2, %f4, %f2
	faligndata	%f4, %f6, %f4
.L192aligned:
___
for ($i=0; $i<7; $i++) {
    $code.=<<___;
	std		%f0, [$out + `24*$i+0`]
	aes_kexpand1	%f0, %f4, $i, %f0
	std		%f2, [$out + `24*$i+8`]
	aes_kexpand2	%f2, %f0, %f2
	std		%f4, [$out + `24*$i+16`]
	aes_kexpand2	%f4, %f2, %f4
___
}
$code.=<<___;
	std		%f0, [$out + `24*$i+0`]
	aes_kexpand1	%f0, %f4, $i, %f0
	std		%f2, [$out + `24*$i+8`]
	aes_kexpand2	%f2, %f0, %f2
	std		%f4, [$out + `24*$i+16`]
	std		%f0, [$out + `24*$i+24`]
	std		%f2, [$out + `24*$i+32`]

	mov		12, $tmp
	st		$tmp, [$out + 240]
	retl
	xor		%o0, %o0, %o0

.align	16
.L128:
	brz,pt		$tmp, .L128aligned
	nop

	ldd		[$inp + 16], %f4
	faligndata	%f0, %f2, %f0
	faligndata	%f2, %f4, %f2
.L128aligned:
___
for ($i=0; $i<10; $i++) {
    $code.=<<___;
	std		%f0, [$out + `16*$i+0`]
	aes_kexpand1	%f0, %f2, $i, %f0
	std		%f2, [$out + `16*$i+8`]
	aes_kexpand2	%f2, %f0, %f2
___
}
$code.=<<___;
	std		%f0, [$out + `16*$i+0`]
	std		%f2, [$out + `16*$i+8`]

	mov		10, $tmp
	st		$tmp, [$out + 240]
	retl
	xor		%o0, %o0, %o0
.type	aes_t4_set_encrypt_key,#function
.size	aes_t4_set_encrypt_key,.-aes_t4_set_encrypt_key

.globl	aes_t4_set_decrypt_key
.align	32
aes_t4_set_decrypt_key:
	mov		%o7, %o5
	call		.Lset_encrypt_key
	nop

	mov		%o5, %o7
	sll		$tmp, 4, $inp		! $tmp is number of rounds
	add		$tmp, 2, $tmp
	add		$out, $inp, $inp	! $inp=$out+16*rounds
	srl		$tmp, 2, $tmp		! $tmp=(rounds+2)/4

.Lkey_flip:
	ldd		[$out + 0],  %f0
	ldd		[$out + 8],  %f2
	ldd		[$out + 16], %f4
	ldd		[$out + 24], %f6
	ldd		[$inp + 0],  %f8
	ldd		[$inp + 8],  %f10
	ldd		[$inp - 16], %f12
	ldd		[$inp - 8],  %f14
	sub		$tmp, 1, $tmp
	std		%f0, [$inp + 0]
	std		%f2, [$inp + 8]
	std		%f4, [$inp - 16]
	std		%f6, [$inp - 8]
	std		%f8, [$out + 0]
	std		%f10, [$out + 8]
	std		%f12, [$out + 16]
	std		%f14, [$out + 24]
	add		$out, 32, $out
	brnz		$tmp, .Lkey_flip
	sub		$inp, 32, $inp

	retl
	xor		%o0, %o0, %o0
.type	aes_t4_set_decrypt_key,#function
.size	aes_t4_set_decrypt_key,.-aes_t4_set_decrypt_key
___
}

{{{
my ($inp,$out,$len,$key,$ivec,$enc)=map("%i$_",(0..5));
my ($ileft,$iright,$ooff,$omask,$ivoff)=map("%l$_",(1..7));

$code.=<<___;
.align	32
_aes128_encrypt_1x:
___
for ($i=0; $i<4; $i++) {
    $code.=<<___;
	aes_eround01	%f`16+8*$i+0`, %f0, %f2, %f4
	aes_eround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_eround01	%f`16+8*$i+4`, %f4, %f2, %f0
	aes_eround23	%f`16+8*$i+6`, %f4, %f2, %f2
___
}
$code.=<<___;
	aes_eround01	%f48, %f0, %f2, %f4
	aes_eround23	%f50, %f0, %f2, %f2
	aes_eround01_l	%f52, %f4, %f2, %f0
	retl
	aes_eround23_l	%f54, %f4, %f2, %f2
.type	_aes128_encrypt_1x,#function
.size	_aes128_encrypt_1x,.-_aes128_encrypt_1x

.align	32
_aes128_encrypt_2x:
___
for ($i=0; $i<4; $i++) {
    $code.=<<___;
	aes_eround01	%f`16+8*$i+0`, %f0, %f2, %f8
	aes_eround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_eround01	%f`16+8*$i+0`, %f4, %f6, %f10
	aes_eround23	%f`16+8*$i+2`, %f4, %f6, %f6
	aes_eround01	%f`16+8*$i+4`, %f8, %f2, %f0
	aes_eround23	%f`16+8*$i+6`, %f8, %f2, %f2
	aes_eround01	%f`16+8*$i+4`, %f10, %f6, %f4
	aes_eround23	%f`16+8*$i+6`, %f10, %f6, %f6
___
}
$code.=<<___;
	aes_eround01	%f48, %f0, %f2, %f8
	aes_eround23	%f50, %f0, %f2, %f2
	aes_eround01	%f48, %f4, %f6, %f10
	aes_eround23	%f50, %f4, %f6, %f6
	aes_eround01_l	%f52, %f8, %f2, %f0
	aes_eround23_l	%f54, %f8, %f2, %f2
	aes_eround01_l	%f52, %f10, %f6, %f4
	retl
	aes_eround23_l	%f54, %f10, %f6, %f6
.type	_aes128_encrypt_2x,#function
.size	_aes128_encrypt_2x,.-_aes128_encrypt_2x

.align	32
_aes128_loadkey:
	ldx		[$key + 0], %g4
	ldx		[$key + 8], %g5
___
for ($i=2; $i<22;$i++) {			# load key schedule
    $code.=<<___;
	ldd		[$key + `8*$i`], %f`12+2*$i`
___
}
$code.=<<___;
	retl
	nop
.type	_aes128_loadkey,#function
.size	_aes128_loadkey,.-_aes128_loadkey
_aes128_load_enckey=_aes128_loadkey
_aes128_load_deckey=_aes128_loadkey

___

&alg_cbc_encrypt_implement("aes",128);
if ($::evp) {
    &alg_ctr32_implement("aes",128);
    &alg_xts_implement("aes",128,"en");
    &alg_xts_implement("aes",128,"de");
}
&alg_cbc_decrypt_implement("aes",128);

$code.=<<___;
.align	32
_aes128_decrypt_1x:
___
for ($i=0; $i<4; $i++) {
    $code.=<<___;
	aes_dround01	%f`16+8*$i+0`, %f0, %f2, %f4
	aes_dround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_dround01	%f`16+8*$i+4`, %f4, %f2, %f0
	aes_dround23	%f`16+8*$i+6`, %f4, %f2, %f2
___
}
$code.=<<___;
	aes_dround01	%f48, %f0, %f2, %f4
	aes_dround23	%f50, %f0, %f2, %f2
	aes_dround01_l	%f52, %f4, %f2, %f0
	retl
	aes_dround23_l	%f54, %f4, %f2, %f2
.type	_aes128_decrypt_1x,#function
.size	_aes128_decrypt_1x,.-_aes128_decrypt_1x

.align	32
_aes128_decrypt_2x:
___
for ($i=0; $i<4; $i++) {
    $code.=<<___;
	aes_dround01	%f`16+8*$i+0`, %f0, %f2, %f8
	aes_dround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_dround01	%f`16+8*$i+0`, %f4, %f6, %f10
	aes_dround23	%f`16+8*$i+2`, %f4, %f6, %f6
	aes_dround01	%f`16+8*$i+4`, %f8, %f2, %f0
	aes_dround23	%f`16+8*$i+6`, %f8, %f2, %f2
	aes_dround01	%f`16+8*$i+4`, %f10, %f6, %f4
	aes_dround23	%f`16+8*$i+6`, %f10, %f6, %f6
___
}
$code.=<<___;
	aes_dround01	%f48, %f0, %f2, %f8
	aes_dround23	%f50, %f0, %f2, %f2
	aes_dround01	%f48, %f4, %f6, %f10
	aes_dround23	%f50, %f4, %f6, %f6
	aes_dround01_l	%f52, %f8, %f2, %f0
	aes_dround23_l	%f54, %f8, %f2, %f2
	aes_dround01_l	%f52, %f10, %f6, %f4
	retl
	aes_dround23_l	%f54, %f10, %f6, %f6
.type	_aes128_decrypt_2x,#function
.size	_aes128_decrypt_2x,.-_aes128_decrypt_2x
___

$code.=<<___;
.align	32
_aes192_encrypt_1x:
___
for ($i=0; $i<5; $i++) {
    $code.=<<___;
	aes_eround01	%f`16+8*$i+0`, %f0, %f2, %f4
	aes_eround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_eround01	%f`16+8*$i+4`, %f4, %f2, %f0
	aes_eround23	%f`16+8*$i+6`, %f4, %f2, %f2
___
}
$code.=<<___;
	aes_eround01	%f56, %f0, %f2, %f4
	aes_eround23	%f58, %f0, %f2, %f2
	aes_eround01_l	%f60, %f4, %f2, %f0
	retl
	aes_eround23_l	%f62, %f4, %f2, %f2
.type	_aes192_encrypt_1x,#function
.size	_aes192_encrypt_1x,.-_aes192_encrypt_1x

.align	32
_aes192_encrypt_2x:
___
for ($i=0; $i<5; $i++) {
    $code.=<<___;
	aes_eround01	%f`16+8*$i+0`, %f0, %f2, %f8
	aes_eround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_eround01	%f`16+8*$i+0`, %f4, %f6, %f10
	aes_eround23	%f`16+8*$i+2`, %f4, %f6, %f6
	aes_eround01	%f`16+8*$i+4`, %f8, %f2, %f0
	aes_eround23	%f`16+8*$i+6`, %f8, %f2, %f2
	aes_eround01	%f`16+8*$i+4`, %f10, %f6, %f4
	aes_eround23	%f`16+8*$i+6`, %f10, %f6, %f6
___
}
$code.=<<___;
	aes_eround01	%f56, %f0, %f2, %f8
	aes_eround23	%f58, %f0, %f2, %f2
	aes_eround01	%f56, %f4, %f6, %f10
	aes_eround23	%f58, %f4, %f6, %f6
	aes_eround01_l	%f60, %f8, %f2, %f0
	aes_eround23_l	%f62, %f8, %f2, %f2
	aes_eround01_l	%f60, %f10, %f6, %f4
	retl
	aes_eround23_l	%f62, %f10, %f6, %f6
.type	_aes192_encrypt_2x,#function
.size	_aes192_encrypt_2x,.-_aes192_encrypt_2x

.align	32
_aes256_encrypt_1x:
	aes_eround01	%f16, %f0, %f2, %f4
	aes_eround23	%f18, %f0, %f2, %f2
	ldd		[$key + 208], %f16
	ldd		[$key + 216], %f18
	aes_eround01	%f20, %f4, %f2, %f0
	aes_eround23	%f22, %f4, %f2, %f2
	ldd		[$key + 224], %f20
	ldd		[$key + 232], %f22
___
for ($i=1; $i<6; $i++) {
    $code.=<<___;
	aes_eround01	%f`16+8*$i+0`, %f0, %f2, %f4
	aes_eround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_eround01	%f`16+8*$i+4`, %f4, %f2, %f0
	aes_eround23	%f`16+8*$i+6`, %f4, %f2, %f2
___
}
$code.=<<___;
	aes_eround01	%f16, %f0, %f2, %f4
	aes_eround23	%f18, %f0, %f2, %f2
	ldd		[$key + 16], %f16
	ldd		[$key + 24], %f18
	aes_eround01_l	%f20, %f4, %f2, %f0
	aes_eround23_l	%f22, %f4, %f2, %f2
	ldd		[$key + 32], %f20
	retl
	ldd		[$key + 40], %f22
.type	_aes256_encrypt_1x,#function
.size	_aes256_encrypt_1x,.-_aes256_encrypt_1x

.align	32
_aes256_encrypt_2x:
	aes_eround01	%f16, %f0, %f2, %f8
	aes_eround23	%f18, %f0, %f2, %f2
	aes_eround01	%f16, %f4, %f6, %f10
	aes_eround23	%f18, %f4, %f6, %f6
	ldd		[$key + 208], %f16
	ldd		[$key + 216], %f18
	aes_eround01	%f20, %f8, %f2, %f0
	aes_eround23	%f22, %f8, %f2, %f2
	aes_eround01	%f20, %f10, %f6, %f4
	aes_eround23	%f22, %f10, %f6, %f6
	ldd		[$key + 224], %f20
	ldd		[$key + 232], %f22
___
for ($i=1; $i<6; $i++) {
    $code.=<<___;
	aes_eround01	%f`16+8*$i+0`, %f0, %f2, %f8
	aes_eround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_eround01	%f`16+8*$i+0`, %f4, %f6, %f10
	aes_eround23	%f`16+8*$i+2`, %f4, %f6, %f6
	aes_eround01	%f`16+8*$i+4`, %f8, %f2, %f0
	aes_eround23	%f`16+8*$i+6`, %f8, %f2, %f2
	aes_eround01	%f`16+8*$i+4`, %f10, %f6, %f4
	aes_eround23	%f`16+8*$i+6`, %f10, %f6, %f6
___
}
$code.=<<___;
	aes_eround01	%f16, %f0, %f2, %f8
	aes_eround23	%f18, %f0, %f2, %f2
	aes_eround01	%f16, %f4, %f6, %f10
	aes_eround23	%f18, %f4, %f6, %f6
	ldd		[$key + 16], %f16
	ldd		[$key + 24], %f18
	aes_eround01_l	%f20, %f8, %f2, %f0
	aes_eround23_l	%f22, %f8, %f2, %f2
	aes_eround01_l	%f20, %f10, %f6, %f4
	aes_eround23_l	%f22, %f10, %f6, %f6
	ldd		[$key + 32], %f20
	retl
	ldd		[$key + 40], %f22
.type	_aes256_encrypt_2x,#function
.size	_aes256_encrypt_2x,.-_aes256_encrypt_2x

.align	32
_aes192_loadkey:
	ldx		[$key + 0], %g4
	ldx		[$key + 8], %g5
___
for ($i=2; $i<26;$i++) {			# load key schedule
    $code.=<<___;
	ldd		[$key + `8*$i`], %f`12+2*$i`
___
}
$code.=<<___;
	retl
	nop
.type	_aes192_loadkey,#function
.size	_aes192_loadkey,.-_aes192_loadkey
_aes256_loadkey=_aes192_loadkey
_aes192_load_enckey=_aes192_loadkey
_aes192_load_deckey=_aes192_loadkey
_aes256_load_enckey=_aes192_loadkey
_aes256_load_deckey=_aes192_loadkey
___

&alg_cbc_encrypt_implement("aes",256);
&alg_cbc_encrypt_implement("aes",192);
if ($::evp) {
    &alg_ctr32_implement("aes",256);
    &alg_xts_implement("aes",256,"en");
    &alg_xts_implement("aes",256,"de");
    &alg_ctr32_implement("aes",192);
}
&alg_cbc_decrypt_implement("aes",192);
&alg_cbc_decrypt_implement("aes",256);

$code.=<<___;
.align	32
_aes256_decrypt_1x:
	aes_dround01	%f16, %f0, %f2, %f4
	aes_dround23	%f18, %f0, %f2, %f2
	ldd		[$key + 208], %f16
	ldd		[$key + 216], %f18
	aes_dround01	%f20, %f4, %f2, %f0
	aes_dround23	%f22, %f4, %f2, %f2
	ldd		[$key + 224], %f20
	ldd		[$key + 232], %f22
___
for ($i=1; $i<6; $i++) {
    $code.=<<___;
	aes_dround01	%f`16+8*$i+0`, %f0, %f2, %f4
	aes_dround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_dround01	%f`16+8*$i+4`, %f4, %f2, %f0
	aes_dround23	%f`16+8*$i+6`, %f4, %f2, %f2
___
}
$code.=<<___;
	aes_dround01	%f16, %f0, %f2, %f4
	aes_dround23	%f18, %f0, %f2, %f2
	ldd		[$key + 16], %f16
	ldd		[$key + 24], %f18
	aes_dround01_l	%f20, %f4, %f2, %f0
	aes_dround23_l	%f22, %f4, %f2, %f2
	ldd		[$key + 32], %f20
	retl
	ldd		[$key + 40], %f22
.type	_aes256_decrypt_1x,#function
.size	_aes256_decrypt_1x,.-_aes256_decrypt_1x

.align	32
_aes256_decrypt_2x:
	aes_dround01	%f16, %f0, %f2, %f8
	aes_dround23	%f18, %f0, %f2, %f2
	aes_dround01	%f16, %f4, %f6, %f10
	aes_dround23	%f18, %f4, %f6, %f6
	ldd		[$key + 208], %f16
	ldd		[$key + 216], %f18
	aes_dround01	%f20, %f8, %f2, %f0
	aes_dround23	%f22, %f8, %f2, %f2
	aes_dround01	%f20, %f10, %f6, %f4
	aes_dround23	%f22, %f10, %f6, %f6
	ldd		[$key + 224], %f20
	ldd		[$key + 232], %f22
___
for ($i=1; $i<6; $i++) {
    $code.=<<___;
	aes_dround01	%f`16+8*$i+0`, %f0, %f2, %f8
	aes_dround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_dround01	%f`16+8*$i+0`, %f4, %f6, %f10
	aes_dround23	%f`16+8*$i+2`, %f4, %f6, %f6
	aes_dround01	%f`16+8*$i+4`, %f8, %f2, %f0
	aes_dround23	%f`16+8*$i+6`, %f8, %f2, %f2
	aes_dround01	%f`16+8*$i+4`, %f10, %f6, %f4
	aes_dround23	%f`16+8*$i+6`, %f10, %f6, %f6
___
}
$code.=<<___;
	aes_dround01	%f16, %f0, %f2, %f8
	aes_dround23	%f18, %f0, %f2, %f2
	aes_dround01	%f16, %f4, %f6, %f10
	aes_dround23	%f18, %f4, %f6, %f6
	ldd		[$key + 16], %f16
	ldd		[$key + 24], %f18
	aes_dround01_l	%f20, %f8, %f2, %f0
	aes_dround23_l	%f22, %f8, %f2, %f2
	aes_dround01_l	%f20, %f10, %f6, %f4
	aes_dround23_l	%f22, %f10, %f6, %f6
	ldd		[$key + 32], %f20
	retl
	ldd		[$key + 40], %f22
.type	_aes256_decrypt_2x,#function
.size	_aes256_decrypt_2x,.-_aes256_decrypt_2x

.align	32
_aes192_decrypt_1x:
___
for ($i=0; $i<5; $i++) {
    $code.=<<___;
	aes_dround01	%f`16+8*$i+0`, %f0, %f2, %f4
	aes_dround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_dround01	%f`16+8*$i+4`, %f4, %f2, %f0
	aes_dround23	%f`16+8*$i+6`, %f4, %f2, %f2
___
}
$code.=<<___;
	aes_dround01	%f56, %f0, %f2, %f4
	aes_dround23	%f58, %f0, %f2, %f2
	aes_dround01_l	%f60, %f4, %f2, %f0
	retl
	aes_dround23_l	%f62, %f4, %f2, %f2
.type	_aes192_decrypt_1x,#function
.size	_aes192_decrypt_1x,.-_aes192_decrypt_1x

.align	32
_aes192_decrypt_2x:
___
for ($i=0; $i<5; $i++) {
    $code.=<<___;
	aes_dround01	%f`16+8*$i+0`, %f0, %f2, %f8
	aes_dround23	%f`16+8*$i+2`, %f0, %f2, %f2
	aes_dround01	%f`16+8*$i+0`, %f4, %f6, %f10
	aes_dround23	%f`16+8*$i+2`, %f4, %f6, %f6
	aes_dround01	%f`16+8*$i+4`, %f8, %f2, %f0
	aes_dround23	%f`16+8*$i+6`, %f8, %f2, %f2
	aes_dround01	%f`16+8*$i+4`, %f10, %f6, %f4
	aes_dround23	%f`16+8*$i+6`, %f10, %f6, %f6
___
}
$code.=<<___;
	aes_dround01	%f56, %f0, %f2, %f8
	aes_dround23	%f58, %f0, %f2, %f2
	aes_dround01	%f56, %f4, %f6, %f10
	aes_dround23	%f58, %f4, %f6, %f6
	aes_dround01_l	%f60, %f8, %f2, %f0
	aes_dround23_l	%f62, %f8, %f2, %f2
	aes_dround01_l	%f60, %f10, %f6, %f4
	retl
	aes_dround23_l	%f62, %f10, %f6, %f6
.type	_aes192_decrypt_2x,#function
.size	_aes192_decrypt_2x,.-_aes192_decrypt_2x
___
}}}

if (!$::evp) {
$code.=<<___;
.global	AES_encrypt
AES_encrypt=aes_t4_encrypt
.global	AES_decrypt
AES_decrypt=aes_t4_decrypt
.global	AES_set_encrypt_key
.align	32
AES_set_encrypt_key:
	andcc		%o2, 7, %g0		! check alignment
	bnz,a,pn	%icc, 1f
	mov		-1, %o0
	brz,a,pn	%o0, 1f
	mov		-1, %o0
	brz,a,pn	%o2, 1f
	mov		-1, %o0
	andncc		%o1, 0x1c0, %g0
	bnz,a,pn	%icc, 1f
	mov		-2, %o0
	cmp		%o1, 128
	bl,a,pn		%icc, 1f
	mov		-2, %o0
	b		aes_t4_set_encrypt_key
	nop
1:	retl
	nop
.type	AES_set_encrypt_key,#function
.size	AES_set_encrypt_key,.-AES_set_encrypt_key

.global	AES_set_decrypt_key
.align	32
AES_set_decrypt_key:
	andcc		%o2, 7, %g0		! check alignment
	bnz,a,pn	%icc, 1f
	mov		-1, %o0
	brz,a,pn	%o0, 1f
	mov		-1, %o0
	brz,a,pn	%o2, 1f
	mov		-1, %o0
	andncc		%o1, 0x1c0, %g0
	bnz,a,pn	%icc, 1f
	mov		-2, %o0
	cmp		%o1, 128
	bl,a,pn		%icc, 1f
	mov		-2, %o0
	b		aes_t4_set_decrypt_key
	nop
1:	retl
	nop
.type	AES_set_decrypt_key,#function
.size	AES_set_decrypt_key,.-AES_set_decrypt_key
___

my ($inp,$out,$len,$key,$ivec,$enc)=map("%o$_",(0..5));

$code.=<<___;
.globl	AES_cbc_encrypt
.align	32
AES_cbc_encrypt:
	ld		[$key + 240], %g1
	nop
	brz		$enc, .Lcbc_decrypt
	cmp		%g1, 12

	bl,pt		%icc, aes128_t4_cbc_encrypt
	nop
	be,pn		%icc, aes192_t4_cbc_encrypt
	nop
	ba		aes256_t4_cbc_encrypt
	nop

.Lcbc_decrypt:
	bl,pt		%icc, aes128_t4_cbc_decrypt
	nop
	be,pn		%icc, aes192_t4_cbc_decrypt
	nop
	ba		aes256_t4_cbc_decrypt
	nop
.type	AES_cbc_encrypt,#function
.size	AES_cbc_encrypt,.-AES_cbc_encrypt
___
}
$code.=<<___;
.asciz	"AES for SPARC T4, David S. Miller, Andy Polyakov"
.align	4
___

&emit_assembler();

close STDOUT;

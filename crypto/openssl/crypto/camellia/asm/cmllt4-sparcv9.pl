#! /usr/bin/env perl
# Copyright 2012-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Written by David S. Miller and Andy Polyakov.
# The module is licensed under 2-clause BSD
# license. October 2012. All rights reserved.
# ====================================================================

######################################################################
# Camellia for SPARC T4.
#
# As with AES below results [for aligned data] are virtually identical
# to critical path lengths for 3-cycle instruction latency:
#
#		128-bit key	192/256-
# CBC encrypt	4.14/4.21(*)	5.46/5.52
#			 (*) numbers after slash are for
#			     misaligned data.
#
# As with Intel AES-NI, question is if it's possible to improve
# performance of parallelizable modes by interleaving round
# instructions. In Camellia every instruction is dependent on
# previous, which means that there is place for 2 additional ones
# in between two dependent. Can we expect 3x performance improvement?
# At least one can argue that it should be possible to break 2x
# barrier... For some reason not even 2x appears to be possible:
#
#		128-bit key	192/256-
# CBC decrypt	2.21/2.74	2.99/3.40
# CTR		2.15/2.68(*)	2.93/3.34
#			 (*) numbers after slash are for
#			     misaligned data.
#
# This is for 2x interleave. But compared to 1x interleave CBC decrypt
# improved by ... 0% for 128-bit key, and 11% for 192/256-bit one.
# So that out-of-order execution logic can take non-interleaved code
# to 1.87x, but can't take 2x interleaved one any further. There
# surely is some explanation... As result 3x interleave was not even
# attempted. Instead an effort was made to share specific modes
# implementations with AES module (therefore sparct4_modes.pl).
#
# To anchor to something else, software C implementation processes
# one byte in 38 cycles with 128-bit key on same processor.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "sparcv9_modes.pl";

$output = pop;
open STDOUT,">$output";

$::evp=1;	# if $evp is set to 0, script generates module with
# Camellia_[en|de]crypt, Camellia_set_key and Camellia_cbc_encrypt
# entry points. These are fully compatible with openssl/camellia.h.

######################################################################
# single-round subroutines
#
{
my ($inp,$out,$key,$rounds,$tmp,$mask)=map("%o$_",(0..5));

$code=<<___;
#include "sparc_arch.h"

.text

.globl	cmll_t4_encrypt
.align	32
cmll_t4_encrypt:
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
	ld		[$key + 272], $rounds	! grandRounds, 3 or 4
	ldd		[$key + 16], %f12
	ldd		[$key + 24], %f14
	xor		%g4, %o4, %o4
	xor		%g5, %o5, %o5
	ldd		[$key + 32], %f16
	ldd		[$key + 40], %f18
	movxtod		%o4, %f0
	movxtod		%o5, %f2
	ldd		[$key + 48], %f20
	ldd		[$key + 56], %f22
	sub		$rounds, 1, $rounds
	ldd		[$key + 64], %f24
	ldd		[$key + 72], %f26
	add		$key, 80, $key

.Lenc:
	camellia_f	%f12, %f2, %f0, %f2
	ldd		[$key + 0], %f12
	sub		$rounds,1,$rounds
	camellia_f	%f14, %f0, %f2, %f0
	ldd		[$key + 8], %f14
	camellia_f	%f16, %f2, %f0, %f2
	ldd		[$key + 16], %f16
	camellia_f	%f18, %f0, %f2, %f0
	ldd		[$key + 24], %f18
	camellia_f	%f20, %f2, %f0, %f2
	ldd		[$key + 32], %f20
	camellia_f	%f22, %f0, %f2, %f0
	ldd		[$key + 40], %f22
	camellia_fl	%f24, %f0, %f0
	ldd		[$key + 48], %f24
	camellia_fli	%f26, %f2, %f2
	ldd		[$key + 56], %f26
	brnz,pt		$rounds, .Lenc
	add		$key, 64, $key

	andcc		$out, 7, $tmp		! is output aligned?
	camellia_f	%f12, %f2, %f0, %f2
	camellia_f	%f14, %f0, %f2, %f0
	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f18, %f0, %f2, %f0
	camellia_f	%f20, %f2, %f0, %f4
	camellia_f	%f22, %f0, %f4, %f2
	fxor		%f24, %f4, %f0
	fxor		%f26, %f2, %f2

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
.type	cmll_t4_encrypt,#function
.size	cmll_t4_encrypt,.-cmll_t4_encrypt

.globl	cmll_t4_decrypt
.align	32
cmll_t4_decrypt:
	ld		[$key + 272], $rounds	! grandRounds, 3 or 4
	andcc		$inp, 7, %g1		! is input aligned?
	andn		$inp, 7, $inp

	sll		$rounds, 6, $rounds
	add		$rounds, $key, $key

	ldx		[$inp + 0], %o4
	bz,pt		%icc, 1f
	ldx		[$inp + 8], %o5
	ldx		[$inp + 16], $inp
	sll		%g1, 3, %g1
	sub		%g0, %g1, %g4
	sllx		%o4, %g1, %o4
	sllx		%o5, %g1, %g1
	srlx		%o5, %g4, %o5
	srlx		$inp, %g4, %g4
	or		%o5, %o4, %o4
	or		%g4, %g1, %o5
1:
	ldx		[$key + 0], %g4
	ldx		[$key + 8], %g5
	ldd		[$key - 8], %f12
	ldd		[$key - 16], %f14
	xor		%g4, %o4, %o4
	xor		%g5, %o5, %o5
	ldd		[$key - 24], %f16
	ldd		[$key - 32], %f18
	movxtod		%o4, %f0
	movxtod		%o5, %f2
	ldd		[$key - 40], %f20
	ldd		[$key - 48], %f22
	sub		$rounds, 64, $rounds
	ldd		[$key - 56], %f24
	ldd		[$key - 64], %f26
	sub		$key, 64, $key

.Ldec:
	camellia_f	%f12, %f2, %f0, %f2
	ldd		[$key - 8], %f12
	sub		$rounds, 64, $rounds
	camellia_f	%f14, %f0, %f2, %f0
	ldd		[$key - 16], %f14
	camellia_f	%f16, %f2, %f0, %f2
	ldd		[$key - 24], %f16
	camellia_f	%f18, %f0, %f2, %f0
	ldd		[$key - 32], %f18
	camellia_f	%f20, %f2, %f0, %f2
	ldd		[$key - 40], %f20
	camellia_f	%f22, %f0, %f2, %f0
	ldd		[$key - 48], %f22
	camellia_fl	%f24, %f0, %f0
	ldd		[$key - 56], %f24
	camellia_fli	%f26, %f2, %f2
	ldd		[$key - 64], %f26
	brnz,pt		$rounds, .Ldec
	sub		$key, 64, $key

	andcc		$out, 7, $tmp		! is output aligned?
	camellia_f	%f12, %f2, %f0, %f2
	camellia_f	%f14, %f0, %f2, %f0
	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f18, %f0, %f2, %f0
	camellia_f	%f20, %f2, %f0, %f4
	camellia_f	%f22, %f0, %f4, %f2
	fxor		%f26, %f4, %f0
	fxor		%f24, %f2, %f2

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
.type	cmll_t4_decrypt,#function
.size	cmll_t4_decrypt,.-cmll_t4_decrypt
___
}

######################################################################
# key setup subroutines
#
{
sub ROTL128 {
  my $rot = shift;

	"srlx	%o4, 64-$rot, %g4\n\t".
	"sllx	%o4, $rot, %o4\n\t".
	"srlx	%o5, 64-$rot, %g5\n\t".
	"sllx	%o5, $rot, %o5\n\t".
	"or	%o4, %g5, %o4\n\t".
	"or	%o5, %g4, %o5";
}

my ($inp,$bits,$out,$tmp)=map("%o$_",(0..5));
$code.=<<___;
.globl	cmll_t4_set_key
.align	32
cmll_t4_set_key:
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
	b		.L256aligned
	faligndata	%f6, %f8, %f6

.align	16
.L192:
	brz,a,pt	$tmp, .L256aligned
	fnot2		%f4, %f6

	ldd		[$inp + 24], %f6
	nop
	faligndata	%f0, %f2, %f0
	faligndata	%f2, %f4, %f2
	faligndata	%f4, %f6, %f4
	fnot2		%f4, %f6

.L256aligned:
	std		%f0, [$out + 0]		! k[0, 1]
	fsrc2		%f0, %f28
	std		%f2, [$out + 8]		! k[2, 3]
	fsrc2		%f2, %f30
	fxor		%f4, %f0, %f0
	b		.L128key
	fxor		%f6, %f2, %f2

.align	16
.L128:
	brz,pt		$tmp, .L128aligned
	nop

	ldd		[$inp + 16], %f4
	nop
	faligndata	%f0, %f2, %f0
	faligndata	%f2, %f4, %f2

.L128aligned:
	std		%f0, [$out + 0]		! k[0, 1]
	fsrc2		%f0, %f28
	std		%f2, [$out + 8]		! k[2, 3]
	fsrc2		%f2, %f30

.L128key:
	mov		%o7, %o5
1:	call		.+8
	add		%o7, SIGMA-1b, %o4
	mov		%o5, %o7

	ldd		[%o4 + 0], %f16
	ldd		[%o4 + 8], %f18
	ldd		[%o4 + 16], %f20
	ldd		[%o4 + 24], %f22

	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f18, %f0, %f2, %f0
	fxor		%f28, %f0, %f0
	fxor		%f30, %f2, %f2
	camellia_f	%f20, %f2, %f0, %f2
	camellia_f	%f22, %f0, %f2, %f0

	bge,pn		%icc, .L256key
	nop
	std	%f0, [$out + 0x10]	! k[ 4,  5]
	std	%f2, [$out + 0x18]	! k[ 6,  7]

	movdtox	%f0, %o4
	movdtox	%f2, %o5
	`&ROTL128(15)`
	stx	%o4, [$out + 0x30]	! k[12, 13]
	stx	%o5, [$out + 0x38]	! k[14, 15]
	`&ROTL128(15)`
	stx	%o4, [$out + 0x40]	! k[16, 17]
	stx	%o5, [$out + 0x48]	! k[18, 19]
	`&ROTL128(15)`
	stx	%o4, [$out + 0x60]	! k[24, 25]
	`&ROTL128(15)`
	stx	%o4, [$out + 0x70]	! k[28, 29]
	stx	%o5, [$out + 0x78]	! k[30, 31]
	`&ROTL128(34)`
	stx	%o4, [$out + 0xa0]	! k[40, 41]
	stx	%o5, [$out + 0xa8]	! k[42, 43]
	`&ROTL128(17)`
	stx	%o4, [$out + 0xc0]	! k[48, 49]
	stx	%o5, [$out + 0xc8]	! k[50, 51]

	movdtox	%f28, %o4		! k[ 0,  1]
	movdtox	%f30, %o5		! k[ 2,  3]
	`&ROTL128(15)`
	stx	%o4, [$out + 0x20]	! k[ 8,  9]
	stx	%o5, [$out + 0x28]	! k[10, 11]
	`&ROTL128(30)`
	stx	%o4, [$out + 0x50]	! k[20, 21]
	stx	%o5, [$out + 0x58]	! k[22, 23]
	`&ROTL128(15)`
	stx	%o5, [$out + 0x68]	! k[26, 27]
	`&ROTL128(17)`
	stx	%o4, [$out + 0x80]	! k[32, 33]
	stx	%o5, [$out + 0x88]	! k[34, 35]
	`&ROTL128(17)`
	stx	%o4, [$out + 0x90]	! k[36, 37]
	stx	%o5, [$out + 0x98]	! k[38, 39]
	`&ROTL128(17)`
	stx	%o4, [$out + 0xb0]	! k[44, 45]
	stx	%o5, [$out + 0xb8]	! k[46, 47]

	mov		3, $tmp
	st		$tmp, [$out + 0x110]
	retl
	xor		%o0, %o0, %o0

.align	16
.L256key:
	ldd		[%o4 + 32], %f24
	ldd		[%o4 + 40], %f26

	std		%f0, [$out + 0x30]	! k[12, 13]
	std		%f2, [$out + 0x38]	! k[14, 15]

	fxor		%f4, %f0, %f0
	fxor		%f6, %f2, %f2
	camellia_f	%f24, %f2, %f0, %f2
	camellia_f	%f26, %f0, %f2, %f0

	std	%f0, [$out + 0x10]	! k[ 4,  5]
	std	%f2, [$out + 0x18]	! k[ 6,  7]

	movdtox	%f0, %o4
	movdtox	%f2, %o5
	`&ROTL128(30)`
	stx	%o4, [$out + 0x50]	! k[20, 21]
	stx	%o5, [$out + 0x58]	! k[22, 23]
	`&ROTL128(30)`
	stx	%o4, [$out + 0xa0]	! k[40, 41]
	stx	%o5, [$out + 0xa8]	! k[42, 43]
	`&ROTL128(51)`
	stx	%o4, [$out + 0x100]	! k[64, 65]
	stx	%o5, [$out + 0x108]	! k[66, 67]

	movdtox	%f4, %o4		! k[ 8,  9]
	movdtox	%f6, %o5		! k[10, 11]
	`&ROTL128(15)`
	stx	%o4, [$out + 0x20]	! k[ 8,  9]
	stx	%o5, [$out + 0x28]	! k[10, 11]
	`&ROTL128(15)`
	stx	%o4, [$out + 0x40]	! k[16, 17]
	stx	%o5, [$out + 0x48]	! k[18, 19]
	`&ROTL128(30)`
	stx	%o4, [$out + 0x90]	! k[36, 37]
	stx	%o5, [$out + 0x98]	! k[38, 39]
	`&ROTL128(34)`
	stx	%o4, [$out + 0xd0]	! k[52, 53]
	stx	%o5, [$out + 0xd8]	! k[54, 55]
	ldx	[$out + 0x30], %o4	! k[12, 13]
	ldx	[$out + 0x38], %o5	! k[14, 15]
	`&ROTL128(15)`
	stx	%o4, [$out + 0x30]	! k[12, 13]
	stx	%o5, [$out + 0x38]	! k[14, 15]
	`&ROTL128(30)`
	stx	%o4, [$out + 0x70]	! k[28, 29]
	stx	%o5, [$out + 0x78]	! k[30, 31]
	srlx	%o4, 32, %g4
	srlx	%o5, 32, %g5
	st	%o4, [$out + 0xc0]	! k[48]
	st	%g5, [$out + 0xc4]	! k[49]
	st	%o5, [$out + 0xc8]	! k[50]
	st	%g4, [$out + 0xcc]	! k[51]
	`&ROTL128(49)`
	stx	%o4, [$out + 0xe0]	! k[56, 57]
	stx	%o5, [$out + 0xe8]	! k[58, 59]

	movdtox	%f28, %o4		! k[ 0,  1]
	movdtox	%f30, %o5		! k[ 2,  3]
	`&ROTL128(45)`
	stx	%o4, [$out + 0x60]	! k[24, 25]
	stx	%o5, [$out + 0x68]	! k[26, 27]
	`&ROTL128(15)`
	stx	%o4, [$out + 0x80]	! k[32, 33]
	stx	%o5, [$out + 0x88]	! k[34, 35]
	`&ROTL128(17)`
	stx	%o4, [$out + 0xb0]	! k[44, 45]
	stx	%o5, [$out + 0xb8]	! k[46, 47]
	`&ROTL128(34)`
	stx	%o4, [$out + 0xf0]	! k[60, 61]
	stx	%o5, [$out + 0xf8]	! k[62, 63]

	mov		4, $tmp
	st		$tmp, [$out + 0x110]
	retl
	xor		%o0, %o0, %o0
.type	cmll_t4_set_key,#function
.size	cmll_t4_set_key,.-cmll_t4_set_key
.align	32
SIGMA:
	.long	0xa09e667f, 0x3bcc908b, 0xb67ae858, 0x4caa73b2
	.long	0xc6ef372f, 0xe94f82be, 0x54ff53a5, 0xf1d36f1c
	.long	0x10e527fa, 0xde682d1d, 0xb05688c2, 0xb3e6c1fd
.type	SIGMA,#object
.size	SIGMA,.-SIGMA
.asciz	"Camellia for SPARC T4, David S. Miller, Andy Polyakov"
___
}

{{{
my ($inp,$out,$len,$key,$ivec,$enc)=map("%i$_",(0..5));
my ($ileft,$iright,$ooff,$omask,$ivoff)=map("%l$_",(1..7));

$code.=<<___;
.align	32
_cmll128_load_enckey:
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
.type	_cmll128_load_enckey,#function
.size	_cmll128_load_enckey,.-_cmll128_load_enckey
_cmll256_load_enckey=_cmll128_load_enckey

.align	32
_cmll256_load_deckey:
	ldd		[$key + 64], %f62
	ldd		[$key + 72], %f60
	b		.Load_deckey
	add		$key, 64, $key
_cmll128_load_deckey:
	ldd		[$key + 0], %f60
	ldd		[$key + 8], %f62
.Load_deckey:
___
for ($i=2; $i<24;$i++) {			# load key schedule
    $code.=<<___;
	ldd		[$key + `8*$i`], %f`62-2*$i`
___
}
$code.=<<___;
	ldx		[$key + 192], %g4
	retl
	ldx		[$key + 200], %g5
.type	_cmll256_load_deckey,#function
.size	_cmll256_load_deckey,.-_cmll256_load_deckey

.align	32
_cmll128_encrypt_1x:
___
for ($i=0; $i<3; $i++) {
    $code.=<<___;
	camellia_f	%f`16+16*$i+0`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+2`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+4`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+6`, %f0, %f2, %f0
___
$code.=<<___ if ($i<2);
	camellia_f	%f`16+16*$i+8`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+10`, %f0, %f2, %f0
	camellia_fl	%f`16+16*$i+12`, %f0,      %f0
	camellia_fli	%f`16+16*$i+14`, %f2,      %f2
___
}
$code.=<<___;
	camellia_f	%f56, %f2, %f0, %f4
	camellia_f	%f58, %f0, %f4, %f2
	fxor		%f60, %f4, %f0
	retl
	fxor		%f62, %f2, %f2
.type	_cmll128_encrypt_1x,#function
.size	_cmll128_encrypt_1x,.-_cmll128_encrypt_1x
_cmll128_decrypt_1x=_cmll128_encrypt_1x

.align	32
_cmll128_encrypt_2x:
___
for ($i=0; $i<3; $i++) {
    $code.=<<___;
	camellia_f	%f`16+16*$i+0`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+0`, %f6, %f4, %f6
	camellia_f	%f`16+16*$i+2`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+2`, %f4, %f6, %f4
	camellia_f	%f`16+16*$i+4`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+4`, %f6, %f4, %f6
	camellia_f	%f`16+16*$i+6`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+6`, %f4, %f6, %f4
___
$code.=<<___ if ($i<2);
	camellia_f	%f`16+16*$i+8`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+8`, %f6, %f4, %f6
	camellia_f	%f`16+16*$i+10`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+10`, %f4, %f6, %f4
	camellia_fl	%f`16+16*$i+12`, %f0,      %f0
	camellia_fl	%f`16+16*$i+12`, %f4,      %f4
	camellia_fli	%f`16+16*$i+14`, %f2,      %f2
	camellia_fli	%f`16+16*$i+14`, %f6,      %f6
___
}
$code.=<<___;
	camellia_f	%f56, %f2, %f0, %f8
	camellia_f	%f56, %f6, %f4, %f10
	camellia_f	%f58, %f0, %f8, %f2
	camellia_f	%f58, %f4, %f10, %f6
	fxor		%f60, %f8, %f0
	fxor		%f60, %f10, %f4
	fxor		%f62, %f2, %f2
	retl
	fxor		%f62, %f6, %f6
.type	_cmll128_encrypt_2x,#function
.size	_cmll128_encrypt_2x,.-_cmll128_encrypt_2x
_cmll128_decrypt_2x=_cmll128_encrypt_2x

.align	32
_cmll256_encrypt_1x:
	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f18, %f0, %f2, %f0
	ldd		[$key + 208], %f16
	ldd		[$key + 216], %f18
	camellia_f	%f20, %f2, %f0, %f2
	camellia_f	%f22, %f0, %f2, %f0
	ldd		[$key + 224], %f20
	ldd		[$key + 232], %f22
	camellia_f	%f24, %f2, %f0, %f2
	camellia_f	%f26, %f0, %f2, %f0
	ldd		[$key + 240], %f24
	ldd		[$key + 248], %f26
	camellia_fl	%f28, %f0, %f0
	camellia_fli	%f30, %f2, %f2
	ldd		[$key + 256], %f28
	ldd		[$key + 264], %f30
___
for ($i=1; $i<3; $i++) {
    $code.=<<___;
	camellia_f	%f`16+16*$i+0`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+2`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+4`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+6`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+8`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+10`, %f0, %f2, %f0
	camellia_fl	%f`16+16*$i+12`, %f0,      %f0
	camellia_fli	%f`16+16*$i+14`, %f2,      %f2
___
}
$code.=<<___;
	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f18, %f0, %f2, %f0
	ldd		[$key + 16], %f16
	ldd		[$key + 24], %f18
	camellia_f	%f20, %f2, %f0, %f2
	camellia_f	%f22, %f0, %f2, %f0
	ldd		[$key + 32], %f20
	ldd		[$key + 40], %f22
	camellia_f	%f24, %f2, %f0, %f4
	camellia_f	%f26, %f0, %f4, %f2
	ldd		[$key + 48], %f24
	ldd		[$key + 56], %f26
	fxor		%f28, %f4, %f0
	fxor		%f30, %f2, %f2
	ldd		[$key + 64], %f28
	retl
	ldd		[$key + 72], %f30
.type	_cmll256_encrypt_1x,#function
.size	_cmll256_encrypt_1x,.-_cmll256_encrypt_1x

.align	32
_cmll256_encrypt_2x:
	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f16, %f6, %f4, %f6
	camellia_f	%f18, %f0, %f2, %f0
	camellia_f	%f18, %f4, %f6, %f4
	ldd		[$key + 208], %f16
	ldd		[$key + 216], %f18
	camellia_f	%f20, %f2, %f0, %f2
	camellia_f	%f20, %f6, %f4, %f6
	camellia_f	%f22, %f0, %f2, %f0
	camellia_f	%f22, %f4, %f6, %f4
	ldd		[$key + 224], %f20
	ldd		[$key + 232], %f22
	camellia_f	%f24, %f2, %f0, %f2
	camellia_f	%f24, %f6, %f4, %f6
	camellia_f	%f26, %f0, %f2, %f0
	camellia_f	%f26, %f4, %f6, %f4
	ldd		[$key + 240], %f24
	ldd		[$key + 248], %f26
	camellia_fl	%f28, %f0, %f0
	camellia_fl	%f28, %f4, %f4
	camellia_fli	%f30, %f2, %f2
	camellia_fli	%f30, %f6, %f6
	ldd		[$key + 256], %f28
	ldd		[$key + 264], %f30
___
for ($i=1; $i<3; $i++) {
    $code.=<<___;
	camellia_f	%f`16+16*$i+0`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+0`, %f6, %f4, %f6
	camellia_f	%f`16+16*$i+2`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+2`, %f4, %f6, %f4
	camellia_f	%f`16+16*$i+4`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+4`, %f6, %f4, %f6
	camellia_f	%f`16+16*$i+6`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+6`, %f4, %f6, %f4
	camellia_f	%f`16+16*$i+8`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+8`, %f6, %f4, %f6
	camellia_f	%f`16+16*$i+10`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+10`, %f4, %f6, %f4
	camellia_fl	%f`16+16*$i+12`, %f0,      %f0
	camellia_fl	%f`16+16*$i+12`, %f4,      %f4
	camellia_fli	%f`16+16*$i+14`, %f2,      %f2
	camellia_fli	%f`16+16*$i+14`, %f6,      %f6
___
}
$code.=<<___;
	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f16, %f6, %f4, %f6
	camellia_f	%f18, %f0, %f2, %f0
	camellia_f	%f18, %f4, %f6, %f4
	ldd		[$key + 16], %f16
	ldd		[$key + 24], %f18
	camellia_f	%f20, %f2, %f0, %f2
	camellia_f	%f20, %f6, %f4, %f6
	camellia_f	%f22, %f0, %f2, %f0
	camellia_f	%f22, %f4, %f6, %f4
	ldd		[$key + 32], %f20
	ldd		[$key + 40], %f22
	camellia_f	%f24, %f2, %f0, %f8
	camellia_f	%f24, %f6, %f4, %f10
	camellia_f	%f26, %f0, %f8, %f2
	camellia_f	%f26, %f4, %f10, %f6
	ldd		[$key + 48], %f24
	ldd		[$key + 56], %f26
	fxor		%f28, %f8, %f0
	fxor		%f28, %f10, %f4
	fxor		%f30, %f2, %f2
	fxor		%f30, %f6, %f6
	ldd		[$key + 64], %f28
	retl
	ldd		[$key + 72], %f30
.type	_cmll256_encrypt_2x,#function
.size	_cmll256_encrypt_2x,.-_cmll256_encrypt_2x

.align	32
_cmll256_decrypt_1x:
	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f18, %f0, %f2, %f0
	ldd		[$key - 8], %f16
	ldd		[$key - 16], %f18
	camellia_f	%f20, %f2, %f0, %f2
	camellia_f	%f22, %f0, %f2, %f0
	ldd		[$key - 24], %f20
	ldd		[$key - 32], %f22
	camellia_f	%f24, %f2, %f0, %f2
	camellia_f	%f26, %f0, %f2, %f0
	ldd		[$key - 40], %f24
	ldd		[$key - 48], %f26
	camellia_fl	%f28, %f0, %f0
	camellia_fli	%f30, %f2, %f2
	ldd		[$key - 56], %f28
	ldd		[$key - 64], %f30
___
for ($i=1; $i<3; $i++) {
    $code.=<<___;
	camellia_f	%f`16+16*$i+0`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+2`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+4`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+6`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+8`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+10`, %f0, %f2, %f0
	camellia_fl	%f`16+16*$i+12`, %f0,      %f0
	camellia_fli	%f`16+16*$i+14`, %f2,      %f2
___
}
$code.=<<___;
	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f18, %f0, %f2, %f0
	ldd		[$key + 184], %f16
	ldd		[$key + 176], %f18
	camellia_f	%f20, %f2, %f0, %f2
	camellia_f	%f22, %f0, %f2, %f0
	ldd		[$key + 168], %f20
	ldd		[$key + 160], %f22
	camellia_f	%f24, %f2, %f0, %f4
	camellia_f	%f26, %f0, %f4, %f2
	ldd		[$key + 152], %f24
	ldd		[$key + 144], %f26
	fxor		%f30, %f4, %f0
	fxor		%f28, %f2, %f2
	ldd		[$key + 136], %f28
	retl
	ldd		[$key + 128], %f30
.type	_cmll256_decrypt_1x,#function
.size	_cmll256_decrypt_1x,.-_cmll256_decrypt_1x

.align	32
_cmll256_decrypt_2x:
	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f16, %f6, %f4, %f6
	camellia_f	%f18, %f0, %f2, %f0
	camellia_f	%f18, %f4, %f6, %f4
	ldd		[$key - 8], %f16
	ldd		[$key - 16], %f18
	camellia_f	%f20, %f2, %f0, %f2
	camellia_f	%f20, %f6, %f4, %f6
	camellia_f	%f22, %f0, %f2, %f0
	camellia_f	%f22, %f4, %f6, %f4
	ldd		[$key - 24], %f20
	ldd		[$key - 32], %f22
	camellia_f	%f24, %f2, %f0, %f2
	camellia_f	%f24, %f6, %f4, %f6
	camellia_f	%f26, %f0, %f2, %f0
	camellia_f	%f26, %f4, %f6, %f4
	ldd		[$key - 40], %f24
	ldd		[$key - 48], %f26
	camellia_fl	%f28, %f0, %f0
	camellia_fl	%f28, %f4, %f4
	camellia_fli	%f30, %f2, %f2
	camellia_fli	%f30, %f6, %f6
	ldd		[$key - 56], %f28
	ldd		[$key - 64], %f30
___
for ($i=1; $i<3; $i++) {
    $code.=<<___;
	camellia_f	%f`16+16*$i+0`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+0`, %f6, %f4, %f6
	camellia_f	%f`16+16*$i+2`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+2`, %f4, %f6, %f4
	camellia_f	%f`16+16*$i+4`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+4`, %f6, %f4, %f6
	camellia_f	%f`16+16*$i+6`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+6`, %f4, %f6, %f4
	camellia_f	%f`16+16*$i+8`, %f2, %f0, %f2
	camellia_f	%f`16+16*$i+8`, %f6, %f4, %f6
	camellia_f	%f`16+16*$i+10`, %f0, %f2, %f0
	camellia_f	%f`16+16*$i+10`, %f4, %f6, %f4
	camellia_fl	%f`16+16*$i+12`, %f0,      %f0
	camellia_fl	%f`16+16*$i+12`, %f4,      %f4
	camellia_fli	%f`16+16*$i+14`, %f2,      %f2
	camellia_fli	%f`16+16*$i+14`, %f6,      %f6
___
}
$code.=<<___;
	camellia_f	%f16, %f2, %f0, %f2
	camellia_f	%f16, %f6, %f4, %f6
	camellia_f	%f18, %f0, %f2, %f0
	camellia_f	%f18, %f4, %f6, %f4
	ldd		[$key + 184], %f16
	ldd		[$key + 176], %f18
	camellia_f	%f20, %f2, %f0, %f2
	camellia_f	%f20, %f6, %f4, %f6
	camellia_f	%f22, %f0, %f2, %f0
	camellia_f	%f22, %f4, %f6, %f4
	ldd		[$key + 168], %f20
	ldd		[$key + 160], %f22
	camellia_f	%f24, %f2, %f0, %f8
	camellia_f	%f24, %f6, %f4, %f10
	camellia_f	%f26, %f0, %f8, %f2
	camellia_f	%f26, %f4, %f10, %f6
	ldd		[$key + 152], %f24
	ldd		[$key + 144], %f26
	fxor		%f30, %f8, %f0
	fxor		%f30, %f10, %f4
	fxor		%f28, %f2, %f2
	fxor		%f28, %f6, %f6
	ldd		[$key + 136], %f28
	retl
	ldd		[$key + 128], %f30
.type	_cmll256_decrypt_2x,#function
.size	_cmll256_decrypt_2x,.-_cmll256_decrypt_2x
___

&alg_cbc_encrypt_implement("cmll",128);
&alg_cbc_encrypt_implement("cmll",256);

&alg_cbc_decrypt_implement("cmll",128);
&alg_cbc_decrypt_implement("cmll",256);

if ($::evp) {
    &alg_ctr32_implement("cmll",128);
    &alg_ctr32_implement("cmll",256);
}
}}}

if (!$::evp) {
$code.=<<___;
.global	Camellia_encrypt
Camellia_encrypt=cmll_t4_encrypt
.global	Camellia_decrypt
Camellia_decrypt=cmll_t4_decrypt
.global	Camellia_set_key
.align	32
Camellia_set_key:
	andcc		%o2, 7, %g0		! double-check alignment
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
	b		cmll_t4_set_key
	nop
1:	retl
	nop
.type	Camellia_set_key,#function
.size	Camellia_set_key,.-Camellia_set_key
___

my ($inp,$out,$len,$key,$ivec,$enc)=map("%o$_",(0..5));

$code.=<<___;
.globl	Camellia_cbc_encrypt
.align	32
Camellia_cbc_encrypt:
	ld		[$key + 272], %g1
	nop
	brz		$enc, .Lcbc_decrypt
	cmp		%g1, 3

	be,pt		%icc, cmll128_t4_cbc_encrypt
	nop
	ba		cmll256_t4_cbc_encrypt
	nop

.Lcbc_decrypt:
	be,pt		%icc, cmll128_t4_cbc_decrypt
	nop
	ba		cmll256_t4_cbc_decrypt
	nop
.type	Camellia_cbc_encrypt,#function
.size	Camellia_cbc_encrypt,.-Camellia_cbc_encrypt
___
}

&emit_assembler();

close STDOUT;

#! /usr/bin/env perl
# Copyright 2013-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Written by David S. Miller and Andy Polyakov.
# The module is licensed under 2-clause BSD
# license. March 2013. All rights reserved.
# ====================================================================

######################################################################
# DES for SPARC T4.
#
# As with other hardware-assisted ciphers CBC encrypt results [for
# aligned data] are virtually identical to critical path lengths:
#
#		DES		Triple-DES
# CBC encrypt	4.14/4.15(*)	11.7/11.7
# CBC decrypt	1.77/4.11(**)	6.42/7.47
#
#			 (*)	numbers after slash are for
#				misaligned data;
#			 (**)	this is result for largest
#				block size, unlike all other
#				cases smaller blocks results
#				are better[?];

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "sparcv9_modes.pl";

$output=pop;
open STDOUT,">$output";

$code.=<<___;
#include "sparc_arch.h"

#ifdef	__arch64__
.register       %g2,#scratch
.register       %g3,#scratch
#endif

.text
___

{ my ($inp,$out)=("%o0","%o1");

$code.=<<___;
.align	32
.globl	des_t4_key_expand
.type	des_t4_key_expand,#function
des_t4_key_expand:
	andcc		$inp, 0x7, %g0
	alignaddr	$inp, %g0, $inp
	bz,pt		%icc, 1f
	ldd		[$inp + 0x00], %f0
	ldd		[$inp + 0x08], %f2
	faligndata	%f0, %f2, %f0
1:	des_kexpand	%f0, 0, %f0
	des_kexpand	%f0, 1, %f2
	std		%f0, [$out + 0x00]
	des_kexpand	%f2, 3, %f6
	std		%f2, [$out + 0x08]
	des_kexpand	%f2, 2, %f4
	des_kexpand	%f6, 3, %f10
	std		%f6, [$out + 0x18]
	des_kexpand	%f6, 2, %f8
	std		%f4, [$out + 0x10]
	des_kexpand	%f10, 3, %f14
	std		%f10, [$out + 0x28]
	des_kexpand	%f10, 2, %f12
	std		%f8, [$out + 0x20]
	des_kexpand	%f14, 1, %f16
	std		%f14, [$out + 0x38]
	des_kexpand	%f16, 3, %f20
	std		%f12, [$out + 0x30]
	des_kexpand	%f16, 2, %f18
	std		%f16, [$out + 0x40]
	des_kexpand	%f20, 3, %f24
	std		%f20, [$out + 0x50]
	des_kexpand	%f20, 2, %f22
	std		%f18, [$out + 0x48]
	des_kexpand	%f24, 3, %f28
	std		%f24, [$out + 0x60]
	des_kexpand	%f24, 2, %f26
	std		%f22, [$out + 0x58]
	des_kexpand	%f28, 1, %f30
	std		%f28, [$out + 0x70]
	std		%f26, [$out + 0x68]
	retl
	std		%f30, [$out + 0x78]
.size	des_t4_key_expand,.-des_t4_key_expand
___
}
{ my ($inp,$out,$len,$key,$ivec) = map("%o$_",(0..4));
  my ($ileft,$iright,$omask) = map("%g$_",(1..3));

$code.=<<___;
.globl	des_t4_cbc_encrypt
.align	32
des_t4_cbc_encrypt:
	cmp		$len, 0
	be,pn		$::size_t_cc, .Lcbc_abort
	srln		$len, 0, $len		! needed on v8+, "nop" on v9
	ld		[$ivec + 0], %f0	! load ivec
	ld		[$ivec + 4], %f1

	and		$inp, 7, $ileft
	andn		$inp, 7, $inp
	sll		$ileft, 3, $ileft
	mov		0xff, $omask
	prefetch	[$inp], 20
	prefetch	[$inp + 63], 20
	sub		%g0, $ileft, $iright
	and		$out, 7, %g4
	alignaddrl	$out, %g0, $out
	srl		$omask, %g4, $omask
	srlx		$len, 3, $len
	movrz		%g4, 0, $omask
	prefetch	[$out], 22

	ldd		[$key + 0x00], %f4	! load key schedule
	ldd		[$key + 0x08], %f6
	ldd		[$key + 0x10], %f8
	ldd		[$key + 0x18], %f10
	ldd		[$key + 0x20], %f12
	ldd		[$key + 0x28], %f14
	ldd		[$key + 0x30], %f16
	ldd		[$key + 0x38], %f18
	ldd		[$key + 0x40], %f20
	ldd		[$key + 0x48], %f22
	ldd		[$key + 0x50], %f24
	ldd		[$key + 0x58], %f26
	ldd		[$key + 0x60], %f28
	ldd		[$key + 0x68], %f30
	ldd		[$key + 0x70], %f32
	ldd		[$key + 0x78], %f34

.Ldes_cbc_enc_loop:
	ldx		[$inp + 0], %g4
	brz,pt		$ileft, 4f
	nop

	ldx		[$inp + 8], %g5
	sllx		%g4, $ileft, %g4
	srlx		%g5, $iright, %g5
	or		%g5, %g4, %g4
4:
	movxtod		%g4, %f2
	prefetch	[$inp + 8+63], 20
	add		$inp, 8, $inp
	fxor		%f2, %f0, %f0		! ^= ivec
	prefetch	[$out + 63], 22

	des_ip		%f0, %f0
	des_round	%f4, %f6, %f0, %f0
	des_round	%f8, %f10, %f0, %f0
	des_round	%f12, %f14, %f0, %f0
	des_round	%f16, %f18, %f0, %f0
	des_round	%f20, %f22, %f0, %f0
	des_round	%f24, %f26, %f0, %f0
	des_round	%f28, %f30, %f0, %f0
	des_round	%f32, %f34, %f0, %f0
	des_iip		%f0, %f0

	brnz,pn		$omask, 2f
	sub		$len, 1, $len

	std		%f0, [$out + 0]
	brnz,pt		$len, .Ldes_cbc_enc_loop
	add		$out, 8, $out

	st		%f0, [$ivec + 0]	! write out ivec
	retl
	st		%f1, [$ivec + 4]
.Lcbc_abort:
	retl
	nop

.align	16
2:	ldxa		[$inp]0x82, %g4		! avoid read-after-write hazard
						! and ~4x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f2		! handle unaligned output

	stda		%f2, [$out + $omask]0xc0	! partial store
	add		$out, 8, $out
	orn		%g0, $omask, $omask
	stda		%f2, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .Ldes_cbc_enc_loop+4
	orn		%g0, $omask, $omask

	st		%f0, [$ivec + 0]	! write out ivec
	retl
	st		%f1, [$ivec + 4]
.type	des_t4_cbc_encrypt,#function
.size	des_t4_cbc_encrypt,.-des_t4_cbc_encrypt

.globl	des_t4_cbc_decrypt
.align	32
des_t4_cbc_decrypt:
	cmp		$len, 0
	be,pn		$::size_t_cc, .Lcbc_abort
	srln		$len, 0, $len		! needed on v8+, "nop" on v9
	ld		[$ivec + 0], %f2	! load ivec
	ld		[$ivec + 4], %f3

	and		$inp, 7, $ileft
	andn		$inp, 7, $inp
	sll		$ileft, 3, $ileft
	mov		0xff, $omask
	prefetch	[$inp], 20
	prefetch	[$inp + 63], 20
	sub		%g0, $ileft, $iright
	and		$out, 7, %g4
	alignaddrl	$out, %g0, $out
	srl		$omask, %g4, $omask
	srlx		$len, 3, $len
	movrz		%g4, 0, $omask
	prefetch	[$out], 22

	ldd		[$key + 0x78], %f4	! load key schedule
	ldd		[$key + 0x70], %f6
	ldd		[$key + 0x68], %f8
	ldd		[$key + 0x60], %f10
	ldd		[$key + 0x58], %f12
	ldd		[$key + 0x50], %f14
	ldd		[$key + 0x48], %f16
	ldd		[$key + 0x40], %f18
	ldd		[$key + 0x38], %f20
	ldd		[$key + 0x30], %f22
	ldd		[$key + 0x28], %f24
	ldd		[$key + 0x20], %f26
	ldd		[$key + 0x18], %f28
	ldd		[$key + 0x10], %f30
	ldd		[$key + 0x08], %f32
	ldd		[$key + 0x00], %f34

.Ldes_cbc_dec_loop:
	ldx		[$inp + 0], %g4
	brz,pt		$ileft, 4f
	nop

	ldx		[$inp + 8], %g5
	sllx		%g4, $ileft, %g4
	srlx		%g5, $iright, %g5
	or		%g5, %g4, %g4
4:
	movxtod		%g4, %f0
	prefetch	[$inp + 8+63], 20
	add		$inp, 8, $inp
	prefetch	[$out + 63], 22

	des_ip		%f0, %f0
	des_round	%f4, %f6, %f0, %f0
	des_round	%f8, %f10, %f0, %f0
	des_round	%f12, %f14, %f0, %f0
	des_round	%f16, %f18, %f0, %f0
	des_round	%f20, %f22, %f0, %f0
	des_round	%f24, %f26, %f0, %f0
	des_round	%f28, %f30, %f0, %f0
	des_round	%f32, %f34, %f0, %f0
	des_iip		%f0, %f0

	fxor		%f2, %f0, %f0		! ^= ivec
	movxtod		%g4, %f2

	brnz,pn		$omask, 2f
	sub		$len, 1, $len

	std		%f0, [$out + 0]
	brnz,pt		$len, .Ldes_cbc_dec_loop
	add		$out, 8, $out

	st		%f2, [$ivec + 0]	! write out ivec
	retl
	st		%f3, [$ivec + 4]

.align	16
2:	ldxa		[$inp]0x82, %g4		! avoid read-after-write hazard
						! and ~4x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f0		! handle unaligned output

	stda		%f0, [$out + $omask]0xc0	! partial store
	add		$out, 8, $out
	orn		%g0, $omask, $omask
	stda		%f0, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .Ldes_cbc_dec_loop+4
	orn		%g0, $omask, $omask

	st		%f2, [$ivec + 0]	! write out ivec
	retl
	st		%f3, [$ivec + 4]
.type	des_t4_cbc_decrypt,#function
.size	des_t4_cbc_decrypt,.-des_t4_cbc_decrypt
___

# One might wonder why does one have back-to-back des_iip/des_ip
# pairs between EDE passes. Indeed, aren't they inverse of each other?
# They almost are. Outcome of the pair is 32-bit words being swapped
# in target register. Consider pair of des_iip/des_ip as a way to
# perform the due swap, it's actually fastest way in this case.

$code.=<<___;
.globl	des_t4_ede3_cbc_encrypt
.align	32
des_t4_ede3_cbc_encrypt:
	cmp		$len, 0
	be,pn		$::size_t_cc, .Lcbc_abort
	srln		$len, 0, $len		! needed on v8+, "nop" on v9
	ld		[$ivec + 0], %f0	! load ivec
	ld		[$ivec + 4], %f1

	and		$inp, 7, $ileft
	andn		$inp, 7, $inp
	sll		$ileft, 3, $ileft
	mov		0xff, $omask
	prefetch	[$inp], 20
	prefetch	[$inp + 63], 20
	sub		%g0, $ileft, $iright
	and		$out, 7, %g4
	alignaddrl	$out, %g0, $out
	srl		$omask, %g4, $omask
	srlx		$len, 3, $len
	movrz		%g4, 0, $omask
	prefetch	[$out], 22

	ldd		[$key + 0x00], %f4	! load key schedule
	ldd		[$key + 0x08], %f6
	ldd		[$key + 0x10], %f8
	ldd		[$key + 0x18], %f10
	ldd		[$key + 0x20], %f12
	ldd		[$key + 0x28], %f14
	ldd		[$key + 0x30], %f16
	ldd		[$key + 0x38], %f18
	ldd		[$key + 0x40], %f20
	ldd		[$key + 0x48], %f22
	ldd		[$key + 0x50], %f24
	ldd		[$key + 0x58], %f26
	ldd		[$key + 0x60], %f28
	ldd		[$key + 0x68], %f30
	ldd		[$key + 0x70], %f32
	ldd		[$key + 0x78], %f34

.Ldes_ede3_cbc_enc_loop:
	ldx		[$inp + 0], %g4
	brz,pt		$ileft, 4f
	nop

	ldx		[$inp + 8], %g5
	sllx		%g4, $ileft, %g4
	srlx		%g5, $iright, %g5
	or		%g5, %g4, %g4
4:
	movxtod		%g4, %f2
	prefetch	[$inp + 8+63], 20
	add		$inp, 8, $inp
	fxor		%f2, %f0, %f0		! ^= ivec
	prefetch	[$out + 63], 22

	des_ip		%f0, %f0
	des_round	%f4, %f6, %f0, %f0
	des_round	%f8, %f10, %f0, %f0
	des_round	%f12, %f14, %f0, %f0
	des_round	%f16, %f18, %f0, %f0
	ldd		[$key + 0x100-0x08], %f36
	ldd		[$key + 0x100-0x10], %f38
	des_round	%f20, %f22, %f0, %f0
	ldd		[$key + 0x100-0x18], %f40
	ldd		[$key + 0x100-0x20], %f42
	des_round	%f24, %f26, %f0, %f0
	ldd		[$key + 0x100-0x28], %f44
	ldd		[$key + 0x100-0x30], %f46
	des_round	%f28, %f30, %f0, %f0
	ldd		[$key + 0x100-0x38], %f48
	ldd		[$key + 0x100-0x40], %f50
	des_round	%f32, %f34, %f0, %f0
	ldd		[$key + 0x100-0x48], %f52
	ldd		[$key + 0x100-0x50], %f54
	des_iip		%f0, %f0

	ldd		[$key + 0x100-0x58], %f56
	ldd		[$key + 0x100-0x60], %f58
	des_ip		%f0, %f0
	ldd		[$key + 0x100-0x68], %f60
	ldd		[$key + 0x100-0x70], %f62
	des_round	%f36, %f38, %f0, %f0
	ldd		[$key + 0x100-0x78], %f36
	ldd		[$key + 0x100-0x80], %f38
	des_round	%f40, %f42, %f0, %f0
	des_round	%f44, %f46, %f0, %f0
	des_round	%f48, %f50, %f0, %f0
	ldd		[$key + 0x100+0x00], %f40
	ldd		[$key + 0x100+0x08], %f42
	des_round	%f52, %f54, %f0, %f0
	ldd		[$key + 0x100+0x10], %f44
	ldd		[$key + 0x100+0x18], %f46
	des_round	%f56, %f58, %f0, %f0
	ldd		[$key + 0x100+0x20], %f48
	ldd		[$key + 0x100+0x28], %f50
	des_round	%f60, %f62, %f0, %f0
	ldd		[$key + 0x100+0x30], %f52
	ldd		[$key + 0x100+0x38], %f54
	des_round	%f36, %f38, %f0, %f0
	ldd		[$key + 0x100+0x40], %f56
	ldd		[$key + 0x100+0x48], %f58
	des_iip		%f0, %f0

	ldd		[$key + 0x100+0x50], %f60
	ldd		[$key + 0x100+0x58], %f62
	des_ip		%f0, %f0
	ldd		[$key + 0x100+0x60], %f36
	ldd		[$key + 0x100+0x68], %f38
	des_round	%f40, %f42, %f0, %f0
	ldd		[$key + 0x100+0x70], %f40
	ldd		[$key + 0x100+0x78], %f42
	des_round	%f44, %f46, %f0, %f0
	des_round	%f48, %f50, %f0, %f0
	des_round	%f52, %f54, %f0, %f0
	des_round	%f56, %f58, %f0, %f0
	des_round	%f60, %f62, %f0, %f0
	des_round	%f36, %f38, %f0, %f0
	des_round	%f40, %f42, %f0, %f0
	des_iip		%f0, %f0

	brnz,pn		$omask, 2f
	sub		$len, 1, $len

	std		%f0, [$out + 0]
	brnz,pt		$len, .Ldes_ede3_cbc_enc_loop
	add		$out, 8, $out

	st		%f0, [$ivec + 0]	! write out ivec
	retl
	st		%f1, [$ivec + 4]

.align	16
2:	ldxa		[$inp]0x82, %g4		! avoid read-after-write hazard
						! and ~2x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f2		! handle unaligned output

	stda		%f2, [$out + $omask]0xc0	! partial store
	add		$out, 8, $out
	orn		%g0, $omask, $omask
	stda		%f2, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .Ldes_ede3_cbc_enc_loop+4
	orn		%g0, $omask, $omask

	st		%f0, [$ivec + 0]	! write out ivec
	retl
	st		%f1, [$ivec + 4]
.type	des_t4_ede3_cbc_encrypt,#function
.size	des_t4_ede3_cbc_encrypt,.-des_t4_ede3_cbc_encrypt

.globl	des_t4_ede3_cbc_decrypt
.align	32
des_t4_ede3_cbc_decrypt:
	cmp		$len, 0
	be,pn		$::size_t_cc, .Lcbc_abort
	srln		$len, 0, $len		! needed on v8+, "nop" on v9
	ld		[$ivec + 0], %f2	! load ivec
	ld		[$ivec + 4], %f3

	and		$inp, 7, $ileft
	andn		$inp, 7, $inp
	sll		$ileft, 3, $ileft
	mov		0xff, $omask
	prefetch	[$inp], 20
	prefetch	[$inp + 63], 20
	sub		%g0, $ileft, $iright
	and		$out, 7, %g4
	alignaddrl	$out, %g0, $out
	srl		$omask, %g4, $omask
	srlx		$len, 3, $len
	movrz		%g4, 0, $omask
	prefetch	[$out], 22

	ldd		[$key + 0x100+0x78], %f4	! load key schedule
	ldd		[$key + 0x100+0x70], %f6
	ldd		[$key + 0x100+0x68], %f8
	ldd		[$key + 0x100+0x60], %f10
	ldd		[$key + 0x100+0x58], %f12
	ldd		[$key + 0x100+0x50], %f14
	ldd		[$key + 0x100+0x48], %f16
	ldd		[$key + 0x100+0x40], %f18
	ldd		[$key + 0x100+0x38], %f20
	ldd		[$key + 0x100+0x30], %f22
	ldd		[$key + 0x100+0x28], %f24
	ldd		[$key + 0x100+0x20], %f26
	ldd		[$key + 0x100+0x18], %f28
	ldd		[$key + 0x100+0x10], %f30
	ldd		[$key + 0x100+0x08], %f32
	ldd		[$key + 0x100+0x00], %f34

.Ldes_ede3_cbc_dec_loop:
	ldx		[$inp + 0], %g4
	brz,pt		$ileft, 4f
	nop

	ldx		[$inp + 8], %g5
	sllx		%g4, $ileft, %g4
	srlx		%g5, $iright, %g5
	or		%g5, %g4, %g4
4:
	movxtod		%g4, %f0
	prefetch	[$inp + 8+63], 20
	add		$inp, 8, $inp
	prefetch	[$out + 63], 22

	des_ip		%f0, %f0
	des_round	%f4, %f6, %f0, %f0
	des_round	%f8, %f10, %f0, %f0
	des_round	%f12, %f14, %f0, %f0
	des_round	%f16, %f18, %f0, %f0
	ldd		[$key + 0x80+0x00], %f36
	ldd		[$key + 0x80+0x08], %f38
	des_round	%f20, %f22, %f0, %f0
	ldd		[$key + 0x80+0x10], %f40
	ldd		[$key + 0x80+0x18], %f42
	des_round	%f24, %f26, %f0, %f0
	ldd		[$key + 0x80+0x20], %f44
	ldd		[$key + 0x80+0x28], %f46
	des_round	%f28, %f30, %f0, %f0
	ldd		[$key + 0x80+0x30], %f48
	ldd		[$key + 0x80+0x38], %f50
	des_round	%f32, %f34, %f0, %f0
	ldd		[$key + 0x80+0x40], %f52
	ldd		[$key + 0x80+0x48], %f54
	des_iip		%f0, %f0

	ldd		[$key + 0x80+0x50], %f56
	ldd		[$key + 0x80+0x58], %f58
	des_ip		%f0, %f0
	ldd		[$key + 0x80+0x60], %f60
	ldd		[$key + 0x80+0x68], %f62
	des_round	%f36, %f38, %f0, %f0
	ldd		[$key + 0x80+0x70], %f36
	ldd		[$key + 0x80+0x78], %f38
	des_round	%f40, %f42, %f0, %f0
	des_round	%f44, %f46, %f0, %f0
	des_round	%f48, %f50, %f0, %f0
	ldd		[$key + 0x80-0x08], %f40
	ldd		[$key + 0x80-0x10], %f42
	des_round	%f52, %f54, %f0, %f0
	ldd		[$key + 0x80-0x18], %f44
	ldd		[$key + 0x80-0x20], %f46
	des_round	%f56, %f58, %f0, %f0
	ldd		[$key + 0x80-0x28], %f48
	ldd		[$key + 0x80-0x30], %f50
	des_round	%f60, %f62, %f0, %f0
	ldd		[$key + 0x80-0x38], %f52
	ldd		[$key + 0x80-0x40], %f54
	des_round	%f36, %f38, %f0, %f0
	ldd		[$key + 0x80-0x48], %f56
	ldd		[$key + 0x80-0x50], %f58
	des_iip		%f0, %f0

	ldd		[$key + 0x80-0x58], %f60
	ldd		[$key + 0x80-0x60], %f62
	des_ip		%f0, %f0
	ldd		[$key + 0x80-0x68], %f36
	ldd		[$key + 0x80-0x70], %f38
	des_round	%f40, %f42, %f0, %f0
	ldd		[$key + 0x80-0x78], %f40
	ldd		[$key + 0x80-0x80], %f42
	des_round	%f44, %f46, %f0, %f0
	des_round	%f48, %f50, %f0, %f0
	des_round	%f52, %f54, %f0, %f0
	des_round	%f56, %f58, %f0, %f0
	des_round	%f60, %f62, %f0, %f0
	des_round	%f36, %f38, %f0, %f0
	des_round	%f40, %f42, %f0, %f0
	des_iip		%f0, %f0

	fxor		%f2, %f0, %f0		! ^= ivec
	movxtod		%g4, %f2

	brnz,pn		$omask, 2f
	sub		$len, 1, $len

	std		%f0, [$out + 0]
	brnz,pt		$len, .Ldes_ede3_cbc_dec_loop
	add		$out, 8, $out

	st		%f2, [$ivec + 0]	! write out ivec
	retl
	st		%f3, [$ivec + 4]

.align	16
2:	ldxa		[$inp]0x82, %g4		! avoid read-after-write hazard
						! and ~3x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f0		! handle unaligned output

	stda		%f0, [$out + $omask]0xc0	! partial store
	add		$out, 8, $out
	orn		%g0, $omask, $omask
	stda		%f0, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .Ldes_ede3_cbc_dec_loop+4
	orn		%g0, $omask, $omask

	st		%f2, [$ivec + 0]	! write out ivec
	retl
	st		%f3, [$ivec + 4]
.type	des_t4_ede3_cbc_decrypt,#function
.size	des_t4_ede3_cbc_decrypt,.-des_t4_ede3_cbc_decrypt
___
}
$code.=<<___;
.asciz  "DES for SPARC T4, David S. Miller, Andy Polyakov"
.align  4
___

&emit_assembler();

close STDOUT;

#! /usr/bin/env perl
# Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
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

# March 2016
#
# Initial support for Fujitsu SPARC64 X/X+ comprises minimally
# required key setup and single-block procedures.
#
# April 2016
#
# Add "teaser" CBC and CTR mode-specific subroutines. "Teaser" means
# that parallelizable nature of CBC decrypt and CTR is not utilized
# yet. CBC encrypt on the other hand is as good as it can possibly
# get processing one byte in 4.1 cycles with 128-bit key on SPARC64 X.
# This is ~6x faster than pure software implementation...
#
# July 2016
#
# Switch from faligndata to fshiftorx, which allows to omit alignaddr
# instructions and improve single-block and short-input performance
# with misaligned data.

$output = pop;
open STDOUT,">$output";

{
my ($inp,$out,$key,$rounds,$tmp,$mask) = map("%o$_",(0..5));

$code.=<<___;
#include "sparc_arch.h"

#define LOCALS (STACK_BIAS+STACK_FRAME)

.text

.globl	aes_fx_encrypt
.align	32
aes_fx_encrypt:
	and		$inp, 7, $tmp		! is input aligned?
	andn		$inp, 7, $inp
	ldd		[$key +  0], %f6	! round[0]
	ldd		[$key +  8], %f8
	mov		%o7, %g1
	ld		[$key + 240], $rounds

1:	call		.+8
	add		%o7, .Linp_align-1b, %o7

	sll		$tmp, 3, $tmp
	ldd		[$inp + 0], %f0		! load input
	brz,pt		$tmp, .Lenc_inp_aligned
	ldd		[$inp + 8], %f2

	ldd		[%o7 + $tmp], %f14	! shift left params
	ldd		[$inp + 16], %f4
	fshiftorx	%f0, %f2, %f14, %f0
	fshiftorx	%f2, %f4, %f14, %f2

.Lenc_inp_aligned:
	ldd		[$key + 16], %f10	! round[1]
	ldd		[$key + 24], %f12

	fxor		%f0, %f6, %f0		! ^=round[0]
	fxor		%f2, %f8, %f2
	ldd		[$key + 32], %f6	! round[2]
	ldd		[$key + 40], %f8
	add		$key, 32, $key
	sub		$rounds, 4, $rounds

.Loop_enc:
	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$key + 16], %f10
	ldd		[$key + 24], %f12
	add		$key, 32, $key

	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2
	ldd		[$key +  0], %f6
	ldd		[$key +  8], %f8

	brnz,a		$rounds, .Loop_enc
	sub		$rounds, 2, $rounds

	andcc		$out, 7, $tmp		! is output aligned?
	andn		$out, 7, $out
	mov		0xff, $mask
	srl		$mask, $tmp, $mask
	add		%o7, 64, %o7
	sll		$tmp, 3, $tmp

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[%o7 + $tmp], %f14	! shift right params

	fmovd		%f0, %f4
	faesenclx	%f2, %f6, %f0
	faesenclx	%f4, %f8, %f2

	bnz,pn		%icc, .Lenc_out_unaligned
	mov		%g1, %o7

	std		%f0, [$out + 0]
	retl
	std		%f2, [$out + 8]

.align	16
.Lenc_out_unaligned:
	add		$out, 16, $inp
	orn		%g0, $mask, $tmp
	fshiftorx	%f0, %f0, %f14, %f4
	fshiftorx	%f0, %f2, %f14, %f6
	fshiftorx	%f2, %f2, %f14, %f8

	stda		%f4, [$out + $mask]0xc0	! partial store
	std		%f6, [$out + 8]
	stda		%f8, [$inp + $tmp]0xc0	! partial store
	retl
	nop
.type	aes_fx_encrypt,#function
.size	aes_fx_encrypt,.-aes_fx_encrypt

.globl	aes_fx_decrypt
.align	32
aes_fx_decrypt:
	and		$inp, 7, $tmp		! is input aligned?
	andn		$inp, 7, $inp
	ldd		[$key +  0], %f6	! round[0]
	ldd		[$key +  8], %f8
	mov		%o7, %g1
	ld		[$key + 240], $rounds

1:	call		.+8
	add		%o7, .Linp_align-1b, %o7

	sll		$tmp, 3, $tmp
	ldd		[$inp + 0], %f0		! load input
	brz,pt		$tmp, .Ldec_inp_aligned
	ldd		[$inp + 8], %f2

	ldd		[%o7 + $tmp], %f14	! shift left params
	ldd		[$inp + 16], %f4
	fshiftorx	%f0, %f2, %f14, %f0
	fshiftorx	%f2, %f4, %f14, %f2

.Ldec_inp_aligned:
	ldd		[$key + 16], %f10	! round[1]
	ldd		[$key + 24], %f12

	fxor		%f0, %f6, %f0		! ^=round[0]
	fxor		%f2, %f8, %f2
	ldd		[$key + 32], %f6	! round[2]
	ldd		[$key + 40], %f8
	add		$key, 32, $key
	sub		$rounds, 4, $rounds

.Loop_dec:
	fmovd		%f0, %f4
	faesdecx	%f2, %f10, %f0
	faesdecx	%f4, %f12, %f2
	ldd		[$key + 16], %f10
	ldd		[$key + 24], %f12
	add		$key, 32, $key

	fmovd		%f0, %f4
	faesdecx	%f2, %f6, %f0
	faesdecx	%f4, %f8, %f2
	ldd		[$key +  0], %f6
	ldd		[$key +  8], %f8

	brnz,a		$rounds, .Loop_dec
	sub		$rounds, 2, $rounds

	andcc		$out, 7, $tmp		! is output aligned?
	andn		$out, 7, $out
	mov		0xff, $mask
	srl		$mask, $tmp, $mask
	add		%o7, 64, %o7
	sll		$tmp, 3, $tmp

	fmovd		%f0, %f4
	faesdecx	%f2, %f10, %f0
	faesdecx	%f4, %f12, %f2
	ldd		[%o7 + $tmp], %f14	! shift right params

	fmovd		%f0, %f4
	faesdeclx	%f2, %f6, %f0
	faesdeclx	%f4, %f8, %f2

	bnz,pn		%icc, .Ldec_out_unaligned
	mov		%g1, %o7

	std		%f0, [$out + 0]
	retl
	std		%f2, [$out + 8]

.align	16
.Ldec_out_unaligned:
	add		$out, 16, $inp
	orn		%g0, $mask, $tmp
	fshiftorx	%f0, %f0, %f14, %f4
	fshiftorx	%f0, %f2, %f14, %f6
	fshiftorx	%f2, %f2, %f14, %f8

	stda		%f4, [$out + $mask]0xc0	! partial store
	std		%f6, [$out + 8]
	stda		%f8, [$inp + $tmp]0xc0	! partial store
	retl
	nop
.type	aes_fx_decrypt,#function
.size	aes_fx_decrypt,.-aes_fx_decrypt
___
}
{
my ($inp,$bits,$out,$tmp,$inc) = map("%o$_",(0..5));
$code.=<<___;
.globl	aes_fx_set_decrypt_key
.align	32
aes_fx_set_decrypt_key:
	b		.Lset_encrypt_key
	mov		-1, $inc
	retl
	nop
.type	aes_fx_set_decrypt_key,#function
.size	aes_fx_set_decrypt_key,.-aes_fx_set_decrypt_key

.globl	aes_fx_set_encrypt_key
.align	32
aes_fx_set_encrypt_key:
	mov		1, $inc
	nop
.Lset_encrypt_key:
	and		$inp, 7, $tmp
	andn		$inp, 7, $inp
	sll		$tmp, 3, $tmp
	mov		%o7, %g1

1:	call		.+8
	add		%o7, .Linp_align-1b, %o7

	ldd		[%o7 + $tmp], %f10	! shift left params
	mov		%g1, %o7

	cmp		$bits, 192
	ldd		[$inp + 0], %f0
	bl,pt		%icc, .L128
	ldd		[$inp + 8], %f2

	be,pt		%icc, .L192
	ldd		[$inp + 16], %f4
	brz,pt		$tmp, .L256aligned
	ldd		[$inp + 24], %f6

	ldd		[$inp + 32], %f8
	fshiftorx	%f0, %f2, %f10, %f0
	fshiftorx	%f2, %f4, %f10, %f2
	fshiftorx	%f4, %f6, %f10, %f4
	fshiftorx	%f6, %f8, %f10, %f6

.L256aligned:
	mov		14, $bits
	and		$inc, `14*16`, $tmp
	st		$bits, [$out + 240]	! store rounds
	add		$out, $tmp, $out	! start or end of key schedule
	sllx		$inc, 4, $inc		! 16 or -16
___
for ($i=0; $i<6; $i++) {
    $code.=<<___;
	std		%f0, [$out + 0]
	faeskeyx	%f6, `0x10+$i`, %f0
	std		%f2, [$out + 8]
	add		$out, $inc, $out
	faeskeyx	%f0, 0x00, %f2
	std		%f4, [$out + 0]
	faeskeyx	%f2, 0x01, %f4
	std		%f6, [$out + 8]
	add		$out, $inc, $out
	faeskeyx	%f4, 0x00, %f6
___
}
$code.=<<___;
	std		%f0, [$out + 0]
	faeskeyx	%f6, `0x10+$i`, %f0
	std		%f2, [$out + 8]
	add		$out, $inc, $out
	faeskeyx	%f0, 0x00, %f2
	std		%f4,[$out + 0]
	std		%f6,[$out + 8]
	add		$out, $inc, $out
	std		%f0,[$out + 0]
	std		%f2,[$out + 8]
	retl
	xor		%o0, %o0, %o0		! return 0

.align	16
.L192:
	brz,pt		$tmp, .L192aligned
	nop

	ldd		[$inp + 24], %f6
	fshiftorx	%f0, %f2, %f10, %f0
	fshiftorx	%f2, %f4, %f10, %f2
	fshiftorx	%f4, %f6, %f10, %f4

.L192aligned:
	mov		12, $bits
	and		$inc, `12*16`, $tmp
	st		$bits, [$out + 240]	! store rounds
	add		$out, $tmp, $out	! start or end of key schedule
	sllx		$inc, 4, $inc		! 16 or -16
___
for ($i=0; $i<8; $i+=2) {
    $code.=<<___;
	std		%f0, [$out + 0]
	faeskeyx	%f4, `0x10+$i`, %f0
	std		%f2, [$out + 8]
	add		$out, $inc, $out
	faeskeyx	%f0, 0x00, %f2
	std		%f4, [$out + 0]
	faeskeyx	%f2, 0x00, %f4
	std		%f0, [$out + 8]
	add		$out, $inc, $out
	faeskeyx	%f4, `0x10+$i+1`, %f0
	std		%f2, [$out + 0]
	faeskeyx	%f0, 0x00, %f2
	std		%f4, [$out + 8]
	add		$out, $inc, $out
___
$code.=<<___		if ($i<6);
	faeskeyx	%f2, 0x00, %f4
___
}
$code.=<<___;
	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	retl
	xor		%o0, %o0, %o0		! return 0

.align	16
.L128:
	brz,pt		$tmp, .L128aligned
	nop

	ldd		[$inp + 16], %f4
	fshiftorx	%f0, %f2, %f10, %f0
	fshiftorx	%f2, %f4, %f10, %f2

.L128aligned:
	mov		10, $bits
	and		$inc, `10*16`, $tmp
	st		$bits, [$out + 240]	! store rounds
	add		$out, $tmp, $out	! start or end of key schedule
	sllx		$inc, 4, $inc		! 16 or -16
___
for ($i=0; $i<10; $i++) {
    $code.=<<___;
	std		%f0, [$out + 0]
	faeskeyx	%f2, `0x10+$i`, %f0
	std		%f2, [$out + 8]
	add		$out, $inc, $out
	faeskeyx	%f0, 0x00, %f2
___
}
$code.=<<___;
	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	retl
	xor		%o0, %o0, %o0		! return 0
.type	aes_fx_set_encrypt_key,#function
.size	aes_fx_set_encrypt_key,.-aes_fx_set_encrypt_key
___
}
{
my ($inp,$out,$len,$key,$ivp,$dir) = map("%i$_",(0..5));
my ($rounds,$inner,$end,$inc,$ialign,$oalign,$mask) = map("%l$_",(0..7));
my ($iv0,$iv1,$r0hi,$r0lo,$rlhi,$rllo,$in0,$in1,$intail,$outhead,$fshift)
   = map("%f$_",grep { !($_ & 1) } (16 .. 62));
my ($ileft,$iright) = ($ialign,$oalign);

$code.=<<___;
.globl	aes_fx_cbc_encrypt
.align	32
aes_fx_cbc_encrypt:
	save		%sp, -STACK_FRAME-16, %sp
	srln		$len, 4, $len
	and		$inp, 7, $ialign
	andn		$inp, 7, $inp
	brz,pn		$len, .Lcbc_no_data
	sll		$ialign, 3, $ileft

1:	call		.+8
	add		%o7, .Linp_align-1b, %o7

	ld		[$key + 240], $rounds
	and		$out, 7, $oalign
	ld		[$ivp + 0], %f0		! load ivec
	andn		$out, 7, $out
	ld		[$ivp + 4], %f1
	sll		$oalign, 3, $mask
	ld		[$ivp + 8], %f2
	ld		[$ivp + 12], %f3

	sll		$rounds, 4, $rounds
	add		$rounds, $key, $end
	ldd		[$key + 0], $r0hi	! round[0]
	ldd		[$key + 8], $r0lo

	add		$inp, 16, $inp
	sub		$len,  1, $len
	ldd		[$end + 0], $rlhi	! round[last]
	ldd		[$end + 8], $rllo

	mov		16, $inc
	movrz		$len, 0, $inc
	ldd		[$key + 16], %f10	! round[1]
	ldd		[$key + 24], %f12

	ldd		[%o7 + $ileft], $fshift	! shift left params
	add		%o7, 64, %o7
	ldd		[$inp - 16], $in0	! load input
	ldd		[$inp -  8], $in1
	ldda		[$inp]0x82, $intail	! non-faulting load
	brz		$dir, .Lcbc_decrypt
	add		$inp, $inc, $inp	! inp+=16

	fxor		$r0hi, %f0, %f0		! ivec^=round[0]
	fxor		$r0lo, %f2, %f2
	fshiftorx	$in0, $in1, $fshift, $in0
	fshiftorx	$in1, $intail, $fshift, $in1
	nop

.Loop_cbc_enc:
	fxor		$in0, %f0, %f0		! inp^ivec^round[0]
	fxor		$in1, %f2, %f2
	ldd		[$key + 32], %f6	! round[2]
	ldd		[$key + 40], %f8
	add		$key, 32, $end
	sub		$rounds, 16*6, $inner

.Lcbc_enc:
	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$end + 16], %f10
	ldd		[$end + 24], %f12
	add		$end, 32, $end

	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2
	ldd		[$end + 0], %f6
	ldd		[$end + 8], %f8

	brnz,a		$inner, .Lcbc_enc
	sub		$inner, 16*2, $inner

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$end + 16], %f10	! round[last-1]
	ldd		[$end + 24], %f12

	movrz		$len, 0, $inc
	fmovd		$intail, $in0
	ldd		[$inp - 8], $in1	! load next input block
	ldda		[$inp]0x82, $intail	! non-faulting load
	add		$inp, $inc, $inp	! inp+=16

	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2

	fshiftorx	$in0, $in1, $fshift, $in0
	fshiftorx	$in1, $intail, $fshift, $in1

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$key + 16], %f10	! round[1]
	ldd		[$key + 24], %f12

	fxor		$r0hi, $in0, $in0	! inp^=round[0]
	fxor		$r0lo, $in1, $in1

	fmovd		%f0, %f4
	faesenclx	%f2, $rlhi, %f0
	faesenclx	%f4, $rllo, %f2

	brnz,pn		$oalign, .Lcbc_enc_unaligned_out
	nop

	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	add		$out, 16, $out

	brnz,a		$len, .Loop_cbc_enc
	sub		$len, 1, $len

	st		%f0, [$ivp + 0]		! output ivec
	st		%f1, [$ivp + 4]
	st		%f2, [$ivp + 8]
	st		%f3, [$ivp + 12]

.Lcbc_no_data:
	ret
	restore

.align	32
.Lcbc_enc_unaligned_out:
	ldd		[%o7 + $mask], $fshift	! shift right params
	mov		0xff, $mask
	srl		$mask, $oalign, $mask
	sub		%g0, $ileft, $iright

	fshiftorx	%f0, %f0, $fshift, %f6
	fshiftorx	%f0, %f2, $fshift, %f8

	stda		%f6, [$out + $mask]0xc0	! partial store
	orn		%g0, $mask, $mask
	std		%f8, [$out + 8]
	add		$out, 16, $out
	brz		$len, .Lcbc_enc_unaligned_out_done
	sub		$len, 1, $len
	b		.Loop_cbc_enc_unaligned_out
	nop

.align	32
.Loop_cbc_enc_unaligned_out:
	fmovd		%f2, $outhead
	fxor		$in0, %f0, %f0		! inp^ivec^round[0]
	fxor		$in1, %f2, %f2
	ldd		[$key + 32], %f6	! round[2]
	ldd		[$key + 40], %f8

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$key + 48], %f10	! round[3]
	ldd		[$key + 56], %f12

	ldx		[$inp - 16], %o0
	ldx		[$inp -  8], %o1
	brz		$ileft, .Lcbc_enc_aligned_inp
	movrz		$len, 0, $inc

	ldx		[$inp], %o2
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	sllx		%o1, $ileft, %o1
	or		%g1, %o0, %o0
	srlx		%o2, $iright, %o2
	or		%o2, %o1, %o1

.Lcbc_enc_aligned_inp:
	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2
	ldd		[$key + 64], %f6	! round[4]
	ldd		[$key + 72], %f8
	add		$key, 64, $end
	sub		$rounds, 16*8, $inner

	stx		%o0, [%sp + LOCALS + 0]
	stx		%o1, [%sp + LOCALS + 8]
	add		$inp, $inc, $inp	! inp+=16
	nop

.Lcbc_enc_unaligned:
	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$end + 16], %f10
	ldd		[$end + 24], %f12
	add		$end, 32, $end

	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2
	ldd		[$end + 0], %f6
	ldd		[$end + 8], %f8

	brnz,a		$inner, .Lcbc_enc_unaligned
	sub		$inner, 16*2, $inner

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$end + 16], %f10	! round[last-1]
	ldd		[$end + 24], %f12

	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2

	ldd		[%sp + LOCALS + 0], $in0
	ldd		[%sp + LOCALS + 8], $in1

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$key + 16], %f10	! round[1]
	ldd		[$key + 24], %f12

	fxor		$r0hi, $in0, $in0	! inp^=round[0]
	fxor		$r0lo, $in1, $in1

	fmovd		%f0, %f4
	faesenclx	%f2, $rlhi, %f0
	faesenclx	%f4, $rllo, %f2

	fshiftorx	$outhead, %f0, $fshift, %f6
	fshiftorx	%f0, %f2, $fshift, %f8
	std		%f6, [$out + 0]
	std		%f8, [$out + 8]
	add		$out, 16, $out

	brnz,a		$len, .Loop_cbc_enc_unaligned_out
	sub		$len, 1, $len

.Lcbc_enc_unaligned_out_done:
	fshiftorx	%f2, %f2, $fshift, %f8
	stda		%f8, [$out + $mask]0xc0	! partial store

	st		%f0, [$ivp + 0]		! output ivec
	st		%f1, [$ivp + 4]
	st		%f2, [$ivp + 8]
	st		%f3, [$ivp + 12]

	ret
	restore

.align	32
.Lcbc_decrypt:
	fshiftorx	$in0, $in1, $fshift, $in0
	fshiftorx	$in1, $intail, $fshift, $in1
	fmovd		%f0, $iv0
	fmovd		%f2, $iv1

.Loop_cbc_dec:
	fxor		$in0, $r0hi, %f0	! inp^round[0]
	fxor		$in1, $r0lo, %f2
	ldd		[$key + 32], %f6	! round[2]
	ldd		[$key + 40], %f8
	add		$key, 32, $end
	sub		$rounds, 16*6, $inner

.Lcbc_dec:
	fmovd		%f0, %f4
	faesdecx	%f2, %f10, %f0
	faesdecx	%f4, %f12, %f2
	ldd		[$end + 16], %f10
	ldd		[$end + 24], %f12
	add		$end, 32, $end

	fmovd		%f0, %f4
	faesdecx	%f2, %f6, %f0
	faesdecx	%f4, %f8, %f2
	ldd		[$end + 0], %f6
	ldd		[$end + 8], %f8

	brnz,a		$inner, .Lcbc_dec
	sub		$inner, 16*2, $inner

	fmovd		%f0, %f4
	faesdecx	%f2, %f10, %f0
	faesdecx	%f4, %f12, %f2
	ldd		[$end + 16], %f10	! round[last-1]
	ldd		[$end + 24], %f12

	fmovd		%f0, %f4
	faesdecx	%f2, %f6, %f0
	faesdecx	%f4, %f8, %f2
	fxor		$iv0, $rlhi, %f6	! ivec^round[last]
	fxor		$iv1, $rllo, %f8
	fmovd		$in0, $iv0
	fmovd		$in1, $iv1

	movrz		$len, 0, $inc
	fmovd		$intail, $in0
	ldd		[$inp - 8], $in1	! load next input block
	ldda		[$inp]0x82, $intail	! non-faulting load
	add		$inp, $inc, $inp	! inp+=16

	fmovd		%f0, %f4
	faesdecx	%f2, %f10, %f0
	faesdecx	%f4, %f12, %f2
	ldd		[$key + 16], %f10	! round[1]
	ldd		[$key + 24], %f12

	fshiftorx	$in0, $in1, $fshift, $in0
	fshiftorx	$in1, $intail, $fshift, $in1

	fmovd		%f0, %f4
	faesdeclx	%f2, %f6, %f0
	faesdeclx	%f4, %f8, %f2

	brnz,pn		$oalign, .Lcbc_dec_unaligned_out
	nop

	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	add		$out, 16, $out

	brnz,a		$len, .Loop_cbc_dec
	sub		$len, 1, $len

	st		$iv0,    [$ivp + 0]	! output ivec
	st		$iv0#lo, [$ivp + 4]
	st		$iv1,    [$ivp + 8]
	st		$iv1#lo, [$ivp + 12]

	ret
	restore

.align	32
.Lcbc_dec_unaligned_out:
	ldd		[%o7 + $mask], $fshift	! shift right params
	mov		0xff, $mask
	srl		$mask, $oalign, $mask
	sub		%g0, $ileft, $iright

	fshiftorx	%f0, %f0, $fshift, %f6
	fshiftorx	%f0, %f2, $fshift, %f8

	stda		%f6, [$out + $mask]0xc0	! partial store
	orn		%g0, $mask, $mask
	std		%f8, [$out + 8]
	add		$out, 16, $out
	brz		$len, .Lcbc_dec_unaligned_out_done
	sub		$len, 1, $len
	b		.Loop_cbc_dec_unaligned_out
	nop

.align	32
.Loop_cbc_dec_unaligned_out:
	fmovd		%f2, $outhead
	fxor		$in0, $r0hi, %f0	! inp^round[0]
	fxor		$in1, $r0lo, %f2
	ldd		[$key + 32], %f6	! round[2]
	ldd		[$key + 40], %f8

	fmovd		%f0, %f4
	faesdecx	%f2, %f10, %f0
	faesdecx	%f4, %f12, %f2
	ldd		[$key + 48], %f10	! round[3]
	ldd		[$key + 56], %f12

	ldx		[$inp - 16], %o0
	ldx		[$inp - 8], %o1
	brz		$ileft, .Lcbc_dec_aligned_inp
	movrz		$len, 0, $inc

	ldx		[$inp], %o2
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	sllx		%o1, $ileft, %o1
	or		%g1, %o0, %o0
	srlx		%o2, $iright, %o2
	or		%o2, %o1, %o1

.Lcbc_dec_aligned_inp:
	fmovd		%f0, %f4
	faesdecx	%f2, %f6, %f0
	faesdecx	%f4, %f8, %f2
	ldd		[$key + 64], %f6	! round[4]
	ldd		[$key + 72], %f8
	add		$key, 64, $end
	sub		$rounds, 16*8, $inner

	stx		%o0, [%sp + LOCALS + 0]
	stx		%o1, [%sp + LOCALS + 8]
	add		$inp, $inc, $inp	! inp+=16
	nop

.Lcbc_dec_unaligned:
	fmovd		%f0, %f4
	faesdecx	%f2, %f10, %f0
	faesdecx	%f4, %f12, %f2
	ldd		[$end + 16], %f10
	ldd		[$end + 24], %f12
	add		$end, 32, $end

	fmovd		%f0, %f4
	faesdecx	%f2, %f6, %f0
	faesdecx	%f4, %f8, %f2
	ldd		[$end + 0], %f6
	ldd		[$end + 8], %f8

	brnz,a		$inner, .Lcbc_dec_unaligned
	sub		$inner, 16*2, $inner

	fmovd		%f0, %f4
	faesdecx	%f2, %f10, %f0
	faesdecx	%f4, %f12, %f2
	ldd		[$end + 16], %f10	! round[last-1]
	ldd		[$end + 24], %f12

	fmovd		%f0, %f4
	faesdecx	%f2, %f6, %f0
	faesdecx	%f4, %f8, %f2

	fxor		$iv0, $rlhi, %f6	! ivec^round[last]
	fxor		$iv1, $rllo, %f8
	fmovd		$in0, $iv0
	fmovd		$in1, $iv1
	ldd		[%sp + LOCALS + 0], $in0
	ldd		[%sp + LOCALS + 8], $in1

	fmovd		%f0, %f4
	faesdecx	%f2, %f10, %f0
	faesdecx	%f4, %f12, %f2
	ldd		[$key + 16], %f10	! round[1]
	ldd		[$key + 24], %f12

	fmovd		%f0, %f4
	faesdeclx	%f2, %f6, %f0
	faesdeclx	%f4, %f8, %f2

	fshiftorx	$outhead, %f0, $fshift, %f6
	fshiftorx	%f0, %f2, $fshift, %f8
	std		%f6, [$out + 0]
	std		%f8, [$out + 8]
	add		$out, 16, $out

	brnz,a		$len, .Loop_cbc_dec_unaligned_out
	sub		$len, 1, $len

.Lcbc_dec_unaligned_out_done:
	fshiftorx	%f2, %f2, $fshift, %f8
	stda		%f8, [$out + $mask]0xc0	! partial store

	st		$iv0,    [$ivp + 0]	! output ivec
	st		$iv0#lo, [$ivp + 4]
	st		$iv1,    [$ivp + 8]
	st		$iv1#lo, [$ivp + 12]

	ret
	restore
.type	aes_fx_cbc_encrypt,#function
.size	aes_fx_cbc_encrypt,.-aes_fx_cbc_encrypt
___
}
{
my ($inp,$out,$len,$key,$ivp) = map("%i$_",(0..5));
my ($rounds,$inner,$end,$inc,$ialign,$oalign,$mask) = map("%l$_",(0..7));
my ($ctr0,$ctr1,$r0hi,$r0lo,$rlhi,$rllo,$in0,$in1,$intail,$outhead,$fshift)
   = map("%f$_",grep { !($_ & 1) } (16 .. 62));
my ($ileft,$iright) = ($ialign, $oalign);
my $one = "%f14";

$code.=<<___;
.globl	aes_fx_ctr32_encrypt_blocks
.align	32
aes_fx_ctr32_encrypt_blocks:
	save		%sp, -STACK_FRAME-16, %sp
	srln		$len, 0, $len
	and		$inp, 7, $ialign
	andn		$inp, 7, $inp
	brz,pn		$len, .Lctr32_no_data
	sll		$ialign, 3, $ileft

.Lpic:	call		.+8
	add		%o7, .Linp_align - .Lpic, %o7

	ld		[$key + 240], $rounds
	and		$out, 7, $oalign
	ld		[$ivp +  0], $ctr0	! load counter
	andn		$out, 7, $out
	ld		[$ivp +  4], $ctr0#lo
	sll		$oalign, 3, $mask
	ld		[$ivp +  8], $ctr1
	ld		[$ivp + 12], $ctr1#lo
	ldd		[%o7 + 128], $one

	sll		$rounds, 4, $rounds
	add		$rounds, $key, $end
	ldd		[$key + 0], $r0hi	! round[0]
	ldd		[$key + 8], $r0lo

	add		$inp, 16, $inp
	sub		$len, 1, $len
	ldd		[$key + 16], %f10	! round[1]
	ldd		[$key + 24], %f12

	mov		16, $inc
	movrz		$len, 0, $inc
	ldd		[$end + 0], $rlhi	! round[last]
	ldd		[$end + 8], $rllo

	ldd		[%o7 + $ileft], $fshift	! shiftleft params
	add		%o7, 64, %o7
	ldd		[$inp - 16], $in0	! load input
	ldd		[$inp -  8], $in1
	ldda		[$inp]0x82, $intail	! non-faulting load
	add		$inp, $inc, $inp	! inp+=16

	fshiftorx	$in0, $in1, $fshift, $in0
	fshiftorx	$in1, $intail, $fshift, $in1

.Loop_ctr32:
	fxor		$ctr0, $r0hi, %f0	! counter^round[0]
	fxor		$ctr1, $r0lo, %f2
	ldd		[$key + 32], %f6	! round[2]
	ldd		[$key + 40], %f8
	add		$key, 32, $end
	sub		$rounds, 16*6, $inner

.Lctr32_enc:
	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$end + 16], %f10
	ldd		[$end + 24], %f12
	add		$end, 32, $end

	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2
	ldd		[$end + 0], %f6
	ldd		[$end + 8], %f8

	brnz,a		$inner, .Lctr32_enc
	sub		$inner, 16*2, $inner

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$end + 16], %f10	! round[last-1]
	ldd		[$end + 24], %f12

	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2
	fxor		$in0, $rlhi, %f6	! inp^round[last]
	fxor		$in1, $rllo, %f8

	movrz		$len, 0, $inc
	fmovd		$intail, $in0
	ldd		[$inp - 8], $in1	! load next input block
	ldda		[$inp]0x82, $intail	! non-faulting load
	add		$inp, $inc, $inp	! inp+=16

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$key + 16], %f10	! round[1]
	ldd		[$key + 24], %f12

	fshiftorx	$in0, $in1, $fshift, $in0
	fshiftorx	$in1, $intail, $fshift, $in1
	fpadd32		$ctr1, $one, $ctr1	! increment counter

	fmovd		%f0, %f4
	faesenclx	%f2, %f6, %f0
	faesenclx	%f4, %f8, %f2

	brnz,pn		$oalign, .Lctr32_unaligned_out
	nop

	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	add		$out, 16, $out

	brnz,a		$len, .Loop_ctr32
	sub		$len, 1, $len

.Lctr32_no_data:
	ret
	restore

.align	32
.Lctr32_unaligned_out:
	ldd		[%o7 + $mask], $fshift	! shift right params
	mov		0xff, $mask
	srl		$mask, $oalign, $mask
	sub		%g0, $ileft, $iright

	fshiftorx	%f0, %f0, $fshift, %f6
	fshiftorx	%f0, %f2, $fshift, %f8

	stda		%f6, [$out + $mask]0xc0	! partial store
	orn		%g0, $mask, $mask
	std		%f8, [$out + 8]
	add		$out, 16, $out
	brz		$len, .Lctr32_unaligned_out_done
	sub		$len, 1, $len
	b		.Loop_ctr32_unaligned_out
	nop

.align	32
.Loop_ctr32_unaligned_out:
	fmovd		%f2, $outhead
	fxor		$ctr0, $r0hi, %f0	! counter^round[0]
	fxor		$ctr1, $r0lo, %f2
	ldd		[$key + 32], %f6	! round[2]
	ldd		[$key + 40], %f8

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$key + 48], %f10	! round[3]
	ldd		[$key + 56], %f12

	ldx		[$inp - 16], %o0
	ldx		[$inp -  8], %o1
	brz		$ileft, .Lctr32_aligned_inp
	movrz		$len, 0, $inc

	ldx		[$inp], %o2
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	sllx		%o1, $ileft, %o1
	or		%g1, %o0, %o0
	srlx		%o2, $iright, %o2
	or		%o2, %o1, %o1

.Lctr32_aligned_inp:
	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2
	ldd		[$key + 64], %f6	! round[4]
	ldd		[$key + 72], %f8
	add		$key, 64, $end
	sub		$rounds, 16*8, $inner

	stx		%o0, [%sp + LOCALS + 0]
	stx		%o1, [%sp + LOCALS + 8]
	add		$inp, $inc, $inp	! inp+=16
	nop

.Lctr32_enc_unaligned:
	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$end + 16], %f10
	ldd		[$end + 24], %f12
	add		$end, 32, $end

	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2
	ldd		[$end + 0], %f6
	ldd		[$end + 8], %f8

	brnz,a		$inner, .Lctr32_enc_unaligned
	sub		$inner, 16*2, $inner

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$end + 16], %f10	! round[last-1]
	ldd		[$end + 24], %f12
	fpadd32		$ctr1, $one, $ctr1	! increment counter

	fmovd		%f0, %f4
	faesencx	%f2, %f6, %f0
	faesencx	%f4, %f8, %f2
	fxor		$in0, $rlhi, %f6	! inp^round[last]
	fxor		$in1, $rllo, %f8
	ldd		[%sp + LOCALS + 0], $in0
	ldd		[%sp + LOCALS + 8], $in1

	fmovd		%f0, %f4
	faesencx	%f2, %f10, %f0
	faesencx	%f4, %f12, %f2
	ldd		[$key + 16], %f10	! round[1]
	ldd		[$key + 24], %f12

	fmovd		%f0, %f4
	faesenclx	%f2, %f6, %f0
	faesenclx	%f4, %f8, %f2

	fshiftorx	$outhead, %f0, $fshift, %f6
	fshiftorx	%f0, %f2, $fshift, %f8
	std		%f6, [$out + 0]
	std		%f8, [$out + 8]
	add		$out, 16, $out

	brnz,a		$len, .Loop_ctr32_unaligned_out
	sub		$len, 1, $len

.Lctr32_unaligned_out_done:
	fshiftorx	%f2, %f2, $fshift, %f8
	stda		%f8, [$out + $mask]0xc0	! partial store

	ret
	restore
.type	aes_fx_ctr32_encrypt_blocks,#function
.size	aes_fx_ctr32_encrypt_blocks,.-aes_fx_ctr32_encrypt_blocks

.align	32
.Linp_align:		! fshiftorx parameters for left shift toward %rs1
	.byte	0, 0, 64,  0,	0, 64,  0, -64
	.byte	0, 0, 56,  8,	0, 56,  8, -56
	.byte	0, 0, 48, 16,	0, 48, 16, -48
	.byte	0, 0, 40, 24,	0, 40, 24, -40
	.byte	0, 0, 32, 32,	0, 32, 32, -32
	.byte	0, 0, 24, 40,	0, 24, 40, -24
	.byte	0, 0, 16, 48,	0, 16, 48, -16
	.byte	0, 0,  8, 56,	0,  8, 56, -8
.Lout_align:		! fshiftorx parameters for right shift toward %rs2
	.byte	0, 0,  0, 64,	0,  0, 64,   0
	.byte	0, 0,  8, 56,	0,  8, 56,  -8
	.byte	0, 0, 16, 48,	0, 16, 48, -16
	.byte	0, 0, 24, 40,	0, 24, 40, -24
	.byte	0, 0, 32, 32,	0, 32, 32, -32
	.byte	0, 0, 40, 24,	0, 40, 24, -40
	.byte	0, 0, 48, 16,	0, 48, 16, -48
	.byte	0, 0, 56,  8,	0, 56,  8, -56
.Lone:
	.word	0, 1
.asciz	"AES for Fujitsu SPARC64 X, CRYPTOGAMS by <appro\@openssl.org>"
.align	4
___
}
# Purpose of these subroutines is to explicitly encode VIS instructions,
# so that one can compile the module without having to specify VIS
# extensions on compiler command line, e.g. -xarch=v9 vs. -xarch=v9a.
# Idea is to reserve for option to produce "universal" binary and let
# programmer detect if current CPU is VIS capable at run-time.
sub unvis {
my ($mnemonic,$rs1,$rs2,$rd)=@_;
my ($ref,$opf);
my %visopf = (	"faligndata"	=> 0x048,
		"bshuffle"	=> 0x04c,
		"fpadd32"	=> 0x052,
		"fxor"		=> 0x06c,
		"fsrc2"		=> 0x078	);

    $ref = "$mnemonic\t$rs1,$rs2,$rd";

    if ($opf=$visopf{$mnemonic}) {
	foreach ($rs1,$rs2,$rd) {
	    return $ref if (!/%f([0-9]{1,2})/);
	    $_=$1;
	    if ($1>=32) {
		return $ref if ($1&1);
		# re-encode for upper double register addressing
		$_=($1|$1>>5)&31;
	    }
	}

	return	sprintf ".word\t0x%08x !%s",
			0x81b00000|$rd<<25|$rs1<<14|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

sub unvis3 {
my ($mnemonic,$rs1,$rs2,$rd)=@_;
my %bias = ( "g" => 0, "o" => 8, "l" => 16, "i" => 24 );
my ($ref,$opf);
my %visopf = (	"alignaddr"	=> 0x018,
		"bmask"		=> 0x019,
		"alignaddrl"	=> 0x01a	);

    $ref = "$mnemonic\t$rs1,$rs2,$rd";

    if ($opf=$visopf{$mnemonic}) {
	foreach ($rs1,$rs2,$rd) {
	    return $ref if (!/%([goli])([0-9])/);
	    $_=$bias{$1}+$2;
	}

	return	sprintf ".word\t0x%08x !%s",
			0x81b00000|$rd<<25|$rs1<<14|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

sub unfx {
my ($mnemonic,$rs1,$rs2,$rd)=@_;
my ($ref,$opf);
my %aesopf = (	"faesencx"	=> 0x90,
		"faesdecx"	=> 0x91,
		"faesenclx"	=> 0x92,
		"faesdeclx"	=> 0x93,
		"faeskeyx"	=> 0x94	);

    $ref = "$mnemonic\t$rs1,$rs2,$rd";

    if (defined($opf=$aesopf{$mnemonic})) {
	$rs2 = ($rs2 =~ /%f([0-6]*[02468])/) ? (($1|$1>>5)&31) : $rs2;
	$rs2 = oct($rs2) if ($rs2 =~ /^0/);

	foreach ($rs1,$rd) {
	    return $ref if (!/%f([0-9]{1,2})/);
	    $_=$1;
	    if ($1>=32) {
		return $ref if ($1&1);
		# re-encode for upper double register addressing
		$_=($1|$1>>5)&31;
	    }
	}

	return	sprintf ".word\t0x%08x !%s",
			2<<30|$rd<<25|0x36<<19|$rs1<<14|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

sub unfx3src {
my ($mnemonic,$rs1,$rs2,$rs3,$rd)=@_;
my ($ref,$opf);
my %aesopf = (	"fshiftorx"	=> 0x0b	);

    $ref = "$mnemonic\t$rs1,$rs2,$rs3,$rd";

    if (defined($opf=$aesopf{$mnemonic})) {
	foreach ($rs1,$rs2,$rs3,$rd) {
	    return $ref if (!/%f([0-9]{1,2})/);
	    $_=$1;
	    if ($1>=32) {
		return $ref if ($1&1);
		# re-encode for upper double register addressing
		$_=($1|$1>>5)&31;
	    }
	}

	return	sprintf ".word\t0x%08x !%s",
			2<<30|$rd<<25|0x37<<19|$rs1<<14|$rs3<<9|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

foreach (split("\n",$code)) {
    s/\`([^\`]*)\`/eval $1/ge;

    s/%f([0-9]+)#lo/sprintf "%%f%d",$1+1/ge;

    s/\b(faes[^x]{3,4}x)\s+(%f[0-9]{1,2}),\s*([%fx0-9]+),\s*(%f[0-9]{1,2})/
		&unfx($1,$2,$3,$4)
     /ge or
    s/\b([f][^\s]*)\s+(%f[0-9]{1,2}),\s*(%f[0-9]{1,2}),\s*(%f[0-9]{1,2}),\s*(%f[0-9]{1,2})/
		&unfx3src($1,$2,$3,$4,$5)
     /ge or
    s/\b([fb][^\s]*)\s+(%f[0-9]{1,2}),\s*(%f[0-9]{1,2}),\s*(%f[0-9]{1,2})/
		&unvis($1,$2,$3,$4)
     /ge or
    s/\b(alignaddr[l]*)\s+(%[goli][0-7]),\s*(%[goli][0-7]),\s*(%[goli][0-7])/
		&unvis3($1,$2,$3,$4)
     /ge;
    print $_,"\n";
}

close STDOUT;

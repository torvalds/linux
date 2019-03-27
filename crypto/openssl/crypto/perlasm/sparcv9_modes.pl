#! /usr/bin/env perl
# Copyright 2012-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# Specific modes implementations for SPARC Architecture 2011. There
# is T4 dependency though, an ASI value that is not specified in the
# Architecture Manual. But as SPARC universe is rather monocultural,
# we imply that processor capable of executing crypto instructions
# can handle the ASI in question as well. This means that we ought to
# keep eyes open when new processors emerge...
#
# As for above mentioned ASI. It's so called "block initializing
# store" which cancels "read" in "read-update-write" on cache lines.
# This is "cooperative" optimization, as it reduces overall pressure
# on memory interface. Benefits can't be observed/quantified with
# usual benchmarks, on the contrary you can notice that single-thread
# performance for parallelizable modes is ~1.5% worse for largest
# block sizes [though few percent better for not so long ones]. All
# this based on suggestions from David Miller.

$::bias="STACK_BIAS";
$::frame="STACK_FRAME";
$::size_t_cc="SIZE_T_CC";

sub asm_init {		# to be called with @ARGV as argument
    for (@_)		{ $::abibits=64 if (/\-m64/ || /\-xarch\=v9/); }
    if ($::abibits==64)	{ $::bias=2047; $::frame=192; $::size_t_cc="%xcc"; }
    else		{ $::bias=0;    $::frame=112; $::size_t_cc="%icc"; }
}

# unified interface
my ($inp,$out,$len,$key,$ivec)=map("%i$_",(0..5));
# local variables
my ($ileft,$iright,$ooff,$omask,$ivoff,$blk_init)=map("%l$_",(0..7));

sub alg_cbc_encrypt_implement {
my ($alg,$bits) = @_;

$::code.=<<___;
.globl	${alg}${bits}_t4_cbc_encrypt
.align	32
${alg}${bits}_t4_cbc_encrypt:
	save		%sp, -$::frame, %sp
	cmp		$len, 0
	be,pn		$::size_t_cc, .L${bits}_cbc_enc_abort
	srln		$len, 0, $len		! needed on v8+, "nop" on v9
	sub		$inp, $out, $blk_init	! $inp!=$out
___
$::code.=<<___ if (!$::evp);
	andcc		$ivec, 7, $ivoff
	alignaddr	$ivec, %g0, $ivec

	ldd		[$ivec + 0], %f0	! load ivec
	bz,pt		%icc, 1f
	ldd		[$ivec + 8], %f2
	ldd		[$ivec + 16], %f4
	faligndata	%f0, %f2, %f0
	faligndata	%f2, %f4, %f2
1:
___
$::code.=<<___ if ($::evp);
	ld		[$ivec + 0], %f0
	ld		[$ivec + 4], %f1
	ld		[$ivec + 8], %f2
	ld		[$ivec + 12], %f3
___
$::code.=<<___;
	prefetch	[$inp], 20
	prefetch	[$inp + 63], 20
	call		_${alg}${bits}_load_enckey
	and		$inp, 7, $ileft
	andn		$inp, 7, $inp
	sll		$ileft, 3, $ileft
	mov		64, $iright
	mov		0xff, $omask
	sub		$iright, $ileft, $iright
	and		$out, 7, $ooff
	cmp		$len, 127
	movrnz		$ooff, 0, $blk_init		! if (	$out&7 ||
	movleu		$::size_t_cc, 0, $blk_init	!	$len<128 ||
	brnz,pn		$blk_init, .L${bits}cbc_enc_blk	!	$inp==$out)
	srl		$omask, $ooff, $omask

	alignaddrl	$out, %g0, $out
	srlx		$len, 4, $len
	prefetch	[$out], 22

.L${bits}_cbc_enc_loop:
	ldx		[$inp + 0], %o0
	brz,pt		$ileft, 4f
	ldx		[$inp + 8], %o1

	ldx		[$inp + 16], %o2
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	sllx		%o1, $ileft, %o1
	or		%g1, %o0, %o0
	srlx		%o2, $iright, %o2
	or		%o2, %o1, %o1
4:
	xor		%g4, %o0, %o0		! ^= rk[0]
	xor		%g5, %o1, %o1
	movxtod		%o0, %f12
	movxtod		%o1, %f14

	fxor		%f12, %f0, %f0		! ^= ivec
	fxor		%f14, %f2, %f2
	prefetch	[$out + 63], 22
	prefetch	[$inp + 16+63], 20
	call		_${alg}${bits}_encrypt_1x
	add		$inp, 16, $inp

	brnz,pn		$ooff, 2f
	sub		$len, 1, $len

	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	brnz,pt		$len, .L${bits}_cbc_enc_loop
	add		$out, 16, $out
___
$::code.=<<___ if ($::evp);
	st		%f0, [$ivec + 0]
	st		%f1, [$ivec + 4]
	st		%f2, [$ivec + 8]
	st		%f3, [$ivec + 12]
___
$::code.=<<___ if (!$::evp);
	brnz,pn		$ivoff, 3f
	nop

	std		%f0, [$ivec + 0]	! write out ivec
	std		%f2, [$ivec + 8]
___
$::code.=<<___;
.L${bits}_cbc_enc_abort:
	ret
	restore

.align	16
2:	ldxa		[$inp]0x82, %o0		! avoid read-after-write hazard
						! and ~3x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f4		! handle unaligned output
	faligndata	%f0, %f2, %f6
	faligndata	%f2, %f2, %f8

	stda		%f4, [$out + $omask]0xc0	! partial store
	std		%f6, [$out + 8]
	add		$out, 16, $out
	orn		%g0, $omask, $omask
	stda		%f8, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .L${bits}_cbc_enc_loop+4
	orn		%g0, $omask, $omask
___
$::code.=<<___ if ($::evp);
	st		%f0, [$ivec + 0]
	st		%f1, [$ivec + 4]
	st		%f2, [$ivec + 8]
	st		%f3, [$ivec + 12]
___
$::code.=<<___ if (!$::evp);
	brnz,pn		$ivoff, 3f
	nop

	std		%f0, [$ivec + 0]	! write out ivec
	std		%f2, [$ivec + 8]
	ret
	restore

.align	16
3:	alignaddrl	$ivec, $ivoff, %g0	! handle unaligned ivec
	mov		0xff, $omask
	srl		$omask, $ivoff, $omask
	faligndata	%f0, %f0, %f4
	faligndata	%f0, %f2, %f6
	faligndata	%f2, %f2, %f8
	stda		%f4, [$ivec + $omask]0xc0
	std		%f6, [$ivec + 8]
	add		$ivec, 16, $ivec
	orn		%g0, $omask, $omask
	stda		%f8, [$ivec + $omask]0xc0
___
$::code.=<<___;
	ret
	restore

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
.align	32
.L${bits}cbc_enc_blk:
	add	$out, $len, $blk_init
	and	$blk_init, 63, $blk_init	! tail
	sub	$len, $blk_init, $len
	add	$blk_init, 15, $blk_init	! round up to 16n
	srlx	$len, 4, $len
	srl	$blk_init, 4, $blk_init

.L${bits}_cbc_enc_blk_loop:
	ldx		[$inp + 0], %o0
	brz,pt		$ileft, 5f
	ldx		[$inp + 8], %o1

	ldx		[$inp + 16], %o2
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	sllx		%o1, $ileft, %o1
	or		%g1, %o0, %o0
	srlx		%o2, $iright, %o2
	or		%o2, %o1, %o1
5:
	xor		%g4, %o0, %o0		! ^= rk[0]
	xor		%g5, %o1, %o1
	movxtod		%o0, %f12
	movxtod		%o1, %f14

	fxor		%f12, %f0, %f0		! ^= ivec
	fxor		%f14, %f2, %f2
	prefetch	[$inp + 16+63], 20
	call		_${alg}${bits}_encrypt_1x
	add		$inp, 16, $inp
	sub		$len, 1, $len

	stda		%f0, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	add		$out, 8, $out
	stda		%f2, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	brnz,pt		$len, .L${bits}_cbc_enc_blk_loop
	add		$out, 8, $out

	membar		#StoreLoad|#StoreStore
	brnz,pt		$blk_init, .L${bits}_cbc_enc_loop
	mov		$blk_init, $len
___
$::code.=<<___ if ($::evp);
	st		%f0, [$ivec + 0]
	st		%f1, [$ivec + 4]
	st		%f2, [$ivec + 8]
	st		%f3, [$ivec + 12]
___
$::code.=<<___ if (!$::evp);
	brnz,pn		$ivoff, 3b
	nop

	std		%f0, [$ivec + 0]	! write out ivec
	std		%f2, [$ivec + 8]
___
$::code.=<<___;
	ret
	restore
.type	${alg}${bits}_t4_cbc_encrypt,#function
.size	${alg}${bits}_t4_cbc_encrypt,.-${alg}${bits}_t4_cbc_encrypt
___
}

sub alg_cbc_decrypt_implement {
my ($alg,$bits) = @_;

$::code.=<<___;
.globl	${alg}${bits}_t4_cbc_decrypt
.align	32
${alg}${bits}_t4_cbc_decrypt:
	save		%sp, -$::frame, %sp
	cmp		$len, 0
	be,pn		$::size_t_cc, .L${bits}_cbc_dec_abort
	srln		$len, 0, $len		! needed on v8+, "nop" on v9
	sub		$inp, $out, $blk_init	! $inp!=$out
___
$::code.=<<___ if (!$::evp);
	andcc		$ivec, 7, $ivoff
	alignaddr	$ivec, %g0, $ivec

	ldd		[$ivec + 0], %f12	! load ivec
	bz,pt		%icc, 1f
	ldd		[$ivec + 8], %f14
	ldd		[$ivec + 16], %f0
	faligndata	%f12, %f14, %f12
	faligndata	%f14, %f0, %f14
1:
___
$::code.=<<___ if ($::evp);
	ld		[$ivec + 0], %f12	! load ivec
	ld		[$ivec + 4], %f13
	ld		[$ivec + 8], %f14
	ld		[$ivec + 12], %f15
___
$::code.=<<___;
	prefetch	[$inp], 20
	prefetch	[$inp + 63], 20
	call		_${alg}${bits}_load_deckey
	and		$inp, 7, $ileft
	andn		$inp, 7, $inp
	sll		$ileft, 3, $ileft
	mov		64, $iright
	mov		0xff, $omask
	sub		$iright, $ileft, $iright
	and		$out, 7, $ooff
	cmp		$len, 255
	movrnz		$ooff, 0, $blk_init		! if (	$out&7 ||
	movleu		$::size_t_cc, 0, $blk_init	!	$len<256 ||
	brnz,pn		$blk_init, .L${bits}cbc_dec_blk	!	$inp==$out)
	srl		$omask, $ooff, $omask

	andcc		$len, 16, %g0		! is number of blocks even?
	srlx		$len, 4, $len
	alignaddrl	$out, %g0, $out
	bz		%icc, .L${bits}_cbc_dec_loop2x
	prefetch	[$out], 22
.L${bits}_cbc_dec_loop:
	ldx		[$inp + 0], %o0
	brz,pt		$ileft, 4f
	ldx		[$inp + 8], %o1

	ldx		[$inp + 16], %o2
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	sllx		%o1, $ileft, %o1
	or		%g1, %o0, %o0
	srlx		%o2, $iright, %o2
	or		%o2, %o1, %o1
4:
	xor		%g4, %o0, %o2		! ^= rk[0]
	xor		%g5, %o1, %o3
	movxtod		%o2, %f0
	movxtod		%o3, %f2

	prefetch	[$out + 63], 22
	prefetch	[$inp + 16+63], 20
	call		_${alg}${bits}_decrypt_1x
	add		$inp, 16, $inp

	fxor		%f12, %f0, %f0		! ^= ivec
	fxor		%f14, %f2, %f2
	movxtod		%o0, %f12
	movxtod		%o1, %f14

	brnz,pn		$ooff, 2f
	sub		$len, 1, $len

	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	brnz,pt		$len, .L${bits}_cbc_dec_loop2x
	add		$out, 16, $out
___
$::code.=<<___ if ($::evp);
	st		%f12, [$ivec + 0]
	st		%f13, [$ivec + 4]
	st		%f14, [$ivec + 8]
	st		%f15, [$ivec + 12]
___
$::code.=<<___ if (!$::evp);
	brnz,pn		$ivoff, .L${bits}_cbc_dec_unaligned_ivec
	nop

	std		%f12, [$ivec + 0]	! write out ivec
	std		%f14, [$ivec + 8]
___
$::code.=<<___;
.L${bits}_cbc_dec_abort:
	ret
	restore

.align	16
2:	ldxa		[$inp]0x82, %o0		! avoid read-after-write hazard
						! and ~3x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f4		! handle unaligned output
	faligndata	%f0, %f2, %f6
	faligndata	%f2, %f2, %f8

	stda		%f4, [$out + $omask]0xc0	! partial store
	std		%f6, [$out + 8]
	add		$out, 16, $out
	orn		%g0, $omask, $omask
	stda		%f8, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .L${bits}_cbc_dec_loop2x+4
	orn		%g0, $omask, $omask
___
$::code.=<<___ if ($::evp);
	st		%f12, [$ivec + 0]
	st		%f13, [$ivec + 4]
	st		%f14, [$ivec + 8]
	st		%f15, [$ivec + 12]
___
$::code.=<<___ if (!$::evp);
	brnz,pn		$ivoff, .L${bits}_cbc_dec_unaligned_ivec
	nop

	std		%f12, [$ivec + 0]	! write out ivec
	std		%f14, [$ivec + 8]
___
$::code.=<<___;
	ret
	restore

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
.align	32
.L${bits}_cbc_dec_loop2x:
	ldx		[$inp + 0], %o0
	ldx		[$inp + 8], %o1
	ldx		[$inp + 16], %o2
	brz,pt		$ileft, 4f
	ldx		[$inp + 24], %o3

	ldx		[$inp + 32], %o4
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	or		%g1, %o0, %o0
	sllx		%o1, $ileft, %o1
	srlx		%o2, $iright, %g1
	or		%g1, %o1, %o1
	sllx		%o2, $ileft, %o2
	srlx		%o3, $iright, %g1
	or		%g1, %o2, %o2
	sllx		%o3, $ileft, %o3
	srlx		%o4, $iright, %o4
	or		%o4, %o3, %o3
4:
	xor		%g4, %o0, %o4		! ^= rk[0]
	xor		%g5, %o1, %o5
	movxtod		%o4, %f0
	movxtod		%o5, %f2
	xor		%g4, %o2, %o4
	xor		%g5, %o3, %o5
	movxtod		%o4, %f4
	movxtod		%o5, %f6

	prefetch	[$out + 63], 22
	prefetch	[$inp + 32+63], 20
	call		_${alg}${bits}_decrypt_2x
	add		$inp, 32, $inp

	movxtod		%o0, %f8
	movxtod		%o1, %f10
	fxor		%f12, %f0, %f0		! ^= ivec
	fxor		%f14, %f2, %f2
	movxtod		%o2, %f12
	movxtod		%o3, %f14
	fxor		%f8, %f4, %f4
	fxor		%f10, %f6, %f6

	brnz,pn		$ooff, 2f
	sub		$len, 2, $len

	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	std		%f4, [$out + 16]
	std		%f6, [$out + 24]
	brnz,pt		$len, .L${bits}_cbc_dec_loop2x
	add		$out, 32, $out
___
$::code.=<<___ if ($::evp);
	st		%f12, [$ivec + 0]
	st		%f13, [$ivec + 4]
	st		%f14, [$ivec + 8]
	st		%f15, [$ivec + 12]
___
$::code.=<<___ if (!$::evp);
	brnz,pn		$ivoff, .L${bits}_cbc_dec_unaligned_ivec
	nop

	std		%f12, [$ivec + 0]	! write out ivec
	std		%f14, [$ivec + 8]
___
$::code.=<<___;
	ret
	restore

.align	16
2:	ldxa		[$inp]0x82, %o0		! avoid read-after-write hazard
						! and ~3x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f8		! handle unaligned output
	faligndata	%f0, %f2, %f0
	faligndata	%f2, %f4, %f2
	faligndata	%f4, %f6, %f4
	faligndata	%f6, %f6, %f6
	stda		%f8, [$out + $omask]0xc0	! partial store
	std		%f0, [$out + 8]
	std		%f2, [$out + 16]
	std		%f4, [$out + 24]
	add		$out, 32, $out
	orn		%g0, $omask, $omask
	stda		%f6, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .L${bits}_cbc_dec_loop2x+4
	orn		%g0, $omask, $omask
___
$::code.=<<___ if ($::evp);
	st		%f12, [$ivec + 0]
	st		%f13, [$ivec + 4]
	st		%f14, [$ivec + 8]
	st		%f15, [$ivec + 12]
___
$::code.=<<___ if (!$::evp);
	brnz,pn		$ivoff, .L${bits}_cbc_dec_unaligned_ivec
	nop

	std		%f12, [$ivec + 0]	! write out ivec
	std		%f14, [$ivec + 8]
	ret
	restore

.align	16
.L${bits}_cbc_dec_unaligned_ivec:
	alignaddrl	$ivec, $ivoff, %g0	! handle unaligned ivec
	mov		0xff, $omask
	srl		$omask, $ivoff, $omask
	faligndata	%f12, %f12, %f0
	faligndata	%f12, %f14, %f2
	faligndata	%f14, %f14, %f4
	stda		%f0, [$ivec + $omask]0xc0
	std		%f2, [$ivec + 8]
	add		$ivec, 16, $ivec
	orn		%g0, $omask, $omask
	stda		%f4, [$ivec + $omask]0xc0
___
$::code.=<<___;
	ret
	restore

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
.align	32
.L${bits}cbc_dec_blk:
	add	$out, $len, $blk_init
	and	$blk_init, 63, $blk_init	! tail
	sub	$len, $blk_init, $len
	add	$blk_init, 15, $blk_init	! round up to 16n
	srlx	$len, 4, $len
	srl	$blk_init, 4, $blk_init
	sub	$len, 1, $len
	add	$blk_init, 1, $blk_init

.L${bits}_cbc_dec_blk_loop2x:
	ldx		[$inp + 0], %o0
	ldx		[$inp + 8], %o1
	ldx		[$inp + 16], %o2
	brz,pt		$ileft, 5f
	ldx		[$inp + 24], %o3

	ldx		[$inp + 32], %o4
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	or		%g1, %o0, %o0
	sllx		%o1, $ileft, %o1
	srlx		%o2, $iright, %g1
	or		%g1, %o1, %o1
	sllx		%o2, $ileft, %o2
	srlx		%o3, $iright, %g1
	or		%g1, %o2, %o2
	sllx		%o3, $ileft, %o3
	srlx		%o4, $iright, %o4
	or		%o4, %o3, %o3
5:
	xor		%g4, %o0, %o4		! ^= rk[0]
	xor		%g5, %o1, %o5
	movxtod		%o4, %f0
	movxtod		%o5, %f2
	xor		%g4, %o2, %o4
	xor		%g5, %o3, %o5
	movxtod		%o4, %f4
	movxtod		%o5, %f6

	prefetch	[$inp + 32+63], 20
	call		_${alg}${bits}_decrypt_2x
	add		$inp, 32, $inp
	subcc		$len, 2, $len

	movxtod		%o0, %f8
	movxtod		%o1, %f10
	fxor		%f12, %f0, %f0		! ^= ivec
	fxor		%f14, %f2, %f2
	movxtod		%o2, %f12
	movxtod		%o3, %f14
	fxor		%f8, %f4, %f4
	fxor		%f10, %f6, %f6

	stda		%f0, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	add		$out, 8, $out
	stda		%f2, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	add		$out, 8, $out
	stda		%f4, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	add		$out, 8, $out
	stda		%f6, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	bgu,pt		$::size_t_cc, .L${bits}_cbc_dec_blk_loop2x
	add		$out, 8, $out

	add		$blk_init, $len, $len
	andcc		$len, 1, %g0		! is number of blocks even?
	membar		#StoreLoad|#StoreStore
	bnz,pt		%icc, .L${bits}_cbc_dec_loop
	srl		$len, 0, $len
	brnz,pn		$len, .L${bits}_cbc_dec_loop2x
	nop
___
$::code.=<<___ if ($::evp);
	st		%f12, [$ivec + 0]	! write out ivec
	st		%f13, [$ivec + 4]
	st		%f14, [$ivec + 8]
	st		%f15, [$ivec + 12]
___
$::code.=<<___ if (!$::evp);
	brnz,pn		$ivoff, 3b
	nop

	std		%f12, [$ivec + 0]	! write out ivec
	std		%f14, [$ivec + 8]
___
$::code.=<<___;
	ret
	restore
.type	${alg}${bits}_t4_cbc_decrypt,#function
.size	${alg}${bits}_t4_cbc_decrypt,.-${alg}${bits}_t4_cbc_decrypt
___
}

sub alg_ctr32_implement {
my ($alg,$bits) = @_;

$::code.=<<___;
.globl	${alg}${bits}_t4_ctr32_encrypt
.align	32
${alg}${bits}_t4_ctr32_encrypt:
	save		%sp, -$::frame, %sp
	srln		$len, 0, $len		! needed on v8+, "nop" on v9

	prefetch	[$inp], 20
	prefetch	[$inp + 63], 20
	call		_${alg}${bits}_load_enckey
	sllx		$len, 4, $len

	ld		[$ivec + 0], %l4	! counter
	ld		[$ivec + 4], %l5
	ld		[$ivec + 8], %l6
	ld		[$ivec + 12], %l7

	sllx		%l4, 32, %o5
	or		%l5, %o5, %o5
	sllx		%l6, 32, %g1
	xor		%o5, %g4, %g4		! ^= rk[0]
	xor		%g1, %g5, %g5
	movxtod		%g4, %f14		! most significant 64 bits

	sub		$inp, $out, $blk_init	! $inp!=$out
	and		$inp, 7, $ileft
	andn		$inp, 7, $inp
	sll		$ileft, 3, $ileft
	mov		64, $iright
	mov		0xff, $omask
	sub		$iright, $ileft, $iright
	and		$out, 7, $ooff
	cmp		$len, 255
	movrnz		$ooff, 0, $blk_init		! if (	$out&7 ||
	movleu		$::size_t_cc, 0, $blk_init	!	$len<256 ||
	brnz,pn		$blk_init, .L${bits}_ctr32_blk	!	$inp==$out)
	srl		$omask, $ooff, $omask

	andcc		$len, 16, %g0		! is number of blocks even?
	alignaddrl	$out, %g0, $out
	bz		%icc, .L${bits}_ctr32_loop2x
	srlx		$len, 4, $len
.L${bits}_ctr32_loop:
	ldx		[$inp + 0], %o0
	brz,pt		$ileft, 4f
	ldx		[$inp + 8], %o1

	ldx		[$inp + 16], %o2
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	sllx		%o1, $ileft, %o1
	or		%g1, %o0, %o0
	srlx		%o2, $iright, %o2
	or		%o2, %o1, %o1
4:
	xor		%g5, %l7, %g1		! ^= rk[0]
	add		%l7, 1, %l7
	movxtod		%g1, %f2
	srl		%l7, 0, %l7		! clruw
	prefetch	[$out + 63], 22
	prefetch	[$inp + 16+63], 20
___
$::code.=<<___ if ($alg eq "aes");
	aes_eround01	%f16, %f14, %f2, %f4
	aes_eround23	%f18, %f14, %f2, %f2
___
$::code.=<<___ if ($alg eq "cmll");
	camellia_f	%f16, %f2, %f14, %f2
	camellia_f	%f18, %f14, %f2, %f0
___
$::code.=<<___;
	call		_${alg}${bits}_encrypt_1x+8
	add		$inp, 16, $inp

	movxtod		%o0, %f10
	movxtod		%o1, %f12
	fxor		%f10, %f0, %f0		! ^= inp
	fxor		%f12, %f2, %f2

	brnz,pn		$ooff, 2f
	sub		$len, 1, $len

	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	brnz,pt		$len, .L${bits}_ctr32_loop2x
	add		$out, 16, $out

	ret
	restore

.align	16
2:	ldxa		[$inp]0x82, %o0		! avoid read-after-write hazard
						! and ~3x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f4		! handle unaligned output
	faligndata	%f0, %f2, %f6
	faligndata	%f2, %f2, %f8
	stda		%f4, [$out + $omask]0xc0	! partial store
	std		%f6, [$out + 8]
	add		$out, 16, $out
	orn		%g0, $omask, $omask
	stda		%f8, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .L${bits}_ctr32_loop2x+4
	orn		%g0, $omask, $omask

	ret
	restore

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
.align	32
.L${bits}_ctr32_loop2x:
	ldx		[$inp + 0], %o0
	ldx		[$inp + 8], %o1
	ldx		[$inp + 16], %o2
	brz,pt		$ileft, 4f
	ldx		[$inp + 24], %o3

	ldx		[$inp + 32], %o4
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	or		%g1, %o0, %o0
	sllx		%o1, $ileft, %o1
	srlx		%o2, $iright, %g1
	or		%g1, %o1, %o1
	sllx		%o2, $ileft, %o2
	srlx		%o3, $iright, %g1
	or		%g1, %o2, %o2
	sllx		%o3, $ileft, %o3
	srlx		%o4, $iright, %o4
	or		%o4, %o3, %o3
4:
	xor		%g5, %l7, %g1		! ^= rk[0]
	add		%l7, 1, %l7
	movxtod		%g1, %f2
	srl		%l7, 0, %l7		! clruw
	xor		%g5, %l7, %g1
	add		%l7, 1, %l7
	movxtod		%g1, %f6
	srl		%l7, 0, %l7		! clruw
	prefetch	[$out + 63], 22
	prefetch	[$inp + 32+63], 20
___
$::code.=<<___ if ($alg eq "aes");
	aes_eround01	%f16, %f14, %f2, %f8
	aes_eround23	%f18, %f14, %f2, %f2
	aes_eround01	%f16, %f14, %f6, %f10
	aes_eround23	%f18, %f14, %f6, %f6
___
$::code.=<<___ if ($alg eq "cmll");
	camellia_f	%f16, %f2, %f14, %f2
	camellia_f	%f16, %f6, %f14, %f6
	camellia_f	%f18, %f14, %f2, %f0
	camellia_f	%f18, %f14, %f6, %f4
___
$::code.=<<___;
	call		_${alg}${bits}_encrypt_2x+16
	add		$inp, 32, $inp

	movxtod		%o0, %f8
	movxtod		%o1, %f10
	movxtod		%o2, %f12
	fxor		%f8, %f0, %f0		! ^= inp
	movxtod		%o3, %f8
	fxor		%f10, %f2, %f2
	fxor		%f12, %f4, %f4
	fxor		%f8, %f6, %f6

	brnz,pn		$ooff, 2f
	sub		$len, 2, $len

	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	std		%f4, [$out + 16]
	std		%f6, [$out + 24]
	brnz,pt		$len, .L${bits}_ctr32_loop2x
	add		$out, 32, $out

	ret
	restore

.align	16
2:	ldxa		[$inp]0x82, %o0		! avoid read-after-write hazard
						! and ~3x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f8		! handle unaligned output
	faligndata	%f0, %f2, %f0
	faligndata	%f2, %f4, %f2
	faligndata	%f4, %f6, %f4
	faligndata	%f6, %f6, %f6

	stda		%f8, [$out + $omask]0xc0	! partial store
	std		%f0, [$out + 8]
	std		%f2, [$out + 16]
	std		%f4, [$out + 24]
	add		$out, 32, $out
	orn		%g0, $omask, $omask
	stda		%f6, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .L${bits}_ctr32_loop2x+4
	orn		%g0, $omask, $omask

	ret
	restore

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
.align	32
.L${bits}_ctr32_blk:
	add	$out, $len, $blk_init
	and	$blk_init, 63, $blk_init	! tail
	sub	$len, $blk_init, $len
	add	$blk_init, 15, $blk_init	! round up to 16n
	srlx	$len, 4, $len
	srl	$blk_init, 4, $blk_init
	sub	$len, 1, $len
	add	$blk_init, 1, $blk_init

.L${bits}_ctr32_blk_loop2x:
	ldx		[$inp + 0], %o0
	ldx		[$inp + 8], %o1
	ldx		[$inp + 16], %o2
	brz,pt		$ileft, 5f
	ldx		[$inp + 24], %o3

	ldx		[$inp + 32], %o4
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	or		%g1, %o0, %o0
	sllx		%o1, $ileft, %o1
	srlx		%o2, $iright, %g1
	or		%g1, %o1, %o1
	sllx		%o2, $ileft, %o2
	srlx		%o3, $iright, %g1
	or		%g1, %o2, %o2
	sllx		%o3, $ileft, %o3
	srlx		%o4, $iright, %o4
	or		%o4, %o3, %o3
5:
	xor		%g5, %l7, %g1		! ^= rk[0]
	add		%l7, 1, %l7
	movxtod		%g1, %f2
	srl		%l7, 0, %l7		! clruw
	xor		%g5, %l7, %g1
	add		%l7, 1, %l7
	movxtod		%g1, %f6
	srl		%l7, 0, %l7		! clruw
	prefetch	[$inp + 32+63], 20
___
$::code.=<<___ if ($alg eq "aes");
	aes_eround01	%f16, %f14, %f2, %f8
	aes_eround23	%f18, %f14, %f2, %f2
	aes_eround01	%f16, %f14, %f6, %f10
	aes_eround23	%f18, %f14, %f6, %f6
___
$::code.=<<___ if ($alg eq "cmll");
	camellia_f	%f16, %f2, %f14, %f2
	camellia_f	%f16, %f6, %f14, %f6
	camellia_f	%f18, %f14, %f2, %f0
	camellia_f	%f18, %f14, %f6, %f4
___
$::code.=<<___;
	call		_${alg}${bits}_encrypt_2x+16
	add		$inp, 32, $inp
	subcc		$len, 2, $len

	movxtod		%o0, %f8
	movxtod		%o1, %f10
	movxtod		%o2, %f12
	fxor		%f8, %f0, %f0		! ^= inp
	movxtod		%o3, %f8
	fxor		%f10, %f2, %f2
	fxor		%f12, %f4, %f4
	fxor		%f8, %f6, %f6

	stda		%f0, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	add		$out, 8, $out
	stda		%f2, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	add		$out, 8, $out
	stda		%f4, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	add		$out, 8, $out
	stda		%f6, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	bgu,pt		$::size_t_cc, .L${bits}_ctr32_blk_loop2x
	add		$out, 8, $out

	add		$blk_init, $len, $len
	andcc		$len, 1, %g0		! is number of blocks even?
	membar		#StoreLoad|#StoreStore
	bnz,pt		%icc, .L${bits}_ctr32_loop
	srl		$len, 0, $len
	brnz,pn		$len, .L${bits}_ctr32_loop2x
	nop

	ret
	restore
.type	${alg}${bits}_t4_ctr32_encrypt,#function
.size	${alg}${bits}_t4_ctr32_encrypt,.-${alg}${bits}_t4_ctr32_encrypt
___
}

sub alg_xts_implement {
my ($alg,$bits,$dir) = @_;
my ($inp,$out,$len,$key1,$key2,$ivec)=map("%i$_",(0..5));
my $rem=$ivec;

$::code.=<<___;
.globl	${alg}${bits}_t4_xts_${dir}crypt
.align	32
${alg}${bits}_t4_xts_${dir}crypt:
	save		%sp, -$::frame-16, %sp
	srln		$len, 0, $len		! needed on v8+, "nop" on v9

	mov		$ivec, %o0
	add		%fp, $::bias-16, %o1
	call		${alg}_t4_encrypt
	mov		$key2, %o2

	add		%fp, $::bias-16, %l7
	ldxa		[%l7]0x88, %g2
	add		%fp, $::bias-8, %l7
	ldxa		[%l7]0x88, %g3		! %g3:%g2 is tweak

	sethi		%hi(0x76543210), %l7
	or		%l7, %lo(0x76543210), %l7
	bmask		%l7, %g0, %g0		! byte swap mask

	prefetch	[$inp], 20
	prefetch	[$inp + 63], 20
	call		_${alg}${bits}_load_${dir}ckey
	and		$len, 15,  $rem
	and		$len, -16, $len
___
$code.=<<___ if ($dir eq "de");
	mov		0, %l7
	movrnz		$rem, 16,  %l7
	sub		$len, %l7, $len
___
$code.=<<___;

	sub		$inp, $out, $blk_init	! $inp!=$out
	and		$inp, 7, $ileft
	andn		$inp, 7, $inp
	sll		$ileft, 3, $ileft
	mov		64, $iright
	mov		0xff, $omask
	sub		$iright, $ileft, $iright
	and		$out, 7, $ooff
	cmp		$len, 255
	movrnz		$ooff, 0, $blk_init		! if (	$out&7 ||
	movleu		$::size_t_cc, 0, $blk_init	!	$len<256 ||
	brnz,pn		$blk_init, .L${bits}_xts_${dir}blk !	$inp==$out)
	srl		$omask, $ooff, $omask

	andcc		$len, 16, %g0		! is number of blocks even?
___
$code.=<<___ if ($dir eq "de");
	brz,pn		$len, .L${bits}_xts_${dir}steal
___
$code.=<<___;
	alignaddrl	$out, %g0, $out
	bz		%icc, .L${bits}_xts_${dir}loop2x
	srlx		$len, 4, $len
.L${bits}_xts_${dir}loop:
	ldx		[$inp + 0], %o0
	brz,pt		$ileft, 4f
	ldx		[$inp + 8], %o1

	ldx		[$inp + 16], %o2
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	sllx		%o1, $ileft, %o1
	or		%g1, %o0, %o0
	srlx		%o2, $iright, %o2
	or		%o2, %o1, %o1
4:
	movxtod		%g2, %f12
	movxtod		%g3, %f14
	bshuffle	%f12, %f12, %f12
	bshuffle	%f14, %f14, %f14

	xor		%g4, %o0, %o0		! ^= rk[0]
	xor		%g5, %o1, %o1
	movxtod		%o0, %f0
	movxtod		%o1, %f2

	fxor		%f12, %f0, %f0		! ^= tweak[0]
	fxor		%f14, %f2, %f2

	prefetch	[$out + 63], 22
	prefetch	[$inp + 16+63], 20
	call		_${alg}${bits}_${dir}crypt_1x
	add		$inp, 16, $inp

	fxor		%f12, %f0, %f0		! ^= tweak[0]
	fxor		%f14, %f2, %f2

	srax		%g3, 63, %l7		! next tweak value
	addcc		%g2, %g2, %g2
	and		%l7, 0x87, %l7
	addxc		%g3, %g3, %g3
	xor		%l7, %g2, %g2

	brnz,pn		$ooff, 2f
	sub		$len, 1, $len

	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	brnz,pt		$len, .L${bits}_xts_${dir}loop2x
	add		$out, 16, $out

	brnz,pn		$rem, .L${bits}_xts_${dir}steal
	nop

	ret
	restore

.align	16
2:	ldxa		[$inp]0x82, %o0		! avoid read-after-write hazard
						! and ~3x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f4		! handle unaligned output
	faligndata	%f0, %f2, %f6
	faligndata	%f2, %f2, %f8
	stda		%f4, [$out + $omask]0xc0	! partial store
	std		%f6, [$out + 8]
	add		$out, 16, $out
	orn		%g0, $omask, $omask
	stda		%f8, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .L${bits}_xts_${dir}loop2x+4
	orn		%g0, $omask, $omask

	brnz,pn		$rem, .L${bits}_xts_${dir}steal
	nop

	ret
	restore

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
.align	32
.L${bits}_xts_${dir}loop2x:
	ldx		[$inp + 0], %o0
	ldx		[$inp + 8], %o1
	ldx		[$inp + 16], %o2
	brz,pt		$ileft, 4f
	ldx		[$inp + 24], %o3

	ldx		[$inp + 32], %o4
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	or		%g1, %o0, %o0
	sllx		%o1, $ileft, %o1
	srlx		%o2, $iright, %g1
	or		%g1, %o1, %o1
	sllx		%o2, $ileft, %o2
	srlx		%o3, $iright, %g1
	or		%g1, %o2, %o2
	sllx		%o3, $ileft, %o3
	srlx		%o4, $iright, %o4
	or		%o4, %o3, %o3
4:
	movxtod		%g2, %f12
	movxtod		%g3, %f14
	bshuffle	%f12, %f12, %f12
	bshuffle	%f14, %f14, %f14

	srax		%g3, 63, %l7		! next tweak value
	addcc		%g2, %g2, %g2
	and		%l7, 0x87, %l7
	addxc		%g3, %g3, %g3
	xor		%l7, %g2, %g2

	movxtod		%g2, %f8
	movxtod		%g3, %f10
	bshuffle	%f8,  %f8,  %f8
	bshuffle	%f10, %f10, %f10

	xor		%g4, %o0, %o0		! ^= rk[0]
	xor		%g5, %o1, %o1
	xor		%g4, %o2, %o2		! ^= rk[0]
	xor		%g5, %o3, %o3
	movxtod		%o0, %f0
	movxtod		%o1, %f2
	movxtod		%o2, %f4
	movxtod		%o3, %f6

	fxor		%f12, %f0, %f0		! ^= tweak[0]
	fxor		%f14, %f2, %f2
	fxor		%f8,  %f4, %f4		! ^= tweak[0]
	fxor		%f10, %f6, %f6

	prefetch	[$out + 63], 22
	prefetch	[$inp + 32+63], 20
	call		_${alg}${bits}_${dir}crypt_2x
	add		$inp, 32, $inp

	movxtod		%g2, %f8
	movxtod		%g3, %f10

	srax		%g3, 63, %l7		! next tweak value
	addcc		%g2, %g2, %g2
	and		%l7, 0x87, %l7
	addxc		%g3, %g3, %g3
	xor		%l7, %g2, %g2

	bshuffle	%f8,  %f8,  %f8
	bshuffle	%f10, %f10, %f10

	fxor		%f12, %f0, %f0		! ^= tweak[0]
	fxor		%f14, %f2, %f2
	fxor		%f8,  %f4, %f4
	fxor		%f10, %f6, %f6

	brnz,pn		$ooff, 2f
	sub		$len, 2, $len

	std		%f0, [$out + 0]
	std		%f2, [$out + 8]
	std		%f4, [$out + 16]
	std		%f6, [$out + 24]
	brnz,pt		$len, .L${bits}_xts_${dir}loop2x
	add		$out, 32, $out

	fsrc2		%f4, %f0
	fsrc2		%f6, %f2
	brnz,pn		$rem, .L${bits}_xts_${dir}steal
	nop

	ret
	restore

.align	16
2:	ldxa		[$inp]0x82, %o0		! avoid read-after-write hazard
						! and ~3x deterioration
						! in inp==out case
	faligndata	%f0, %f0, %f8		! handle unaligned output
	faligndata	%f0, %f2, %f10
	faligndata	%f2, %f4, %f12
	faligndata	%f4, %f6, %f14
	faligndata	%f6, %f6, %f0

	stda		%f8, [$out + $omask]0xc0	! partial store
	std		%f10, [$out + 8]
	std		%f12, [$out + 16]
	std		%f14, [$out + 24]
	add		$out, 32, $out
	orn		%g0, $omask, $omask
	stda		%f0, [$out + $omask]0xc0	! partial store

	brnz,pt		$len, .L${bits}_xts_${dir}loop2x+4
	orn		%g0, $omask, $omask

	fsrc2		%f4, %f0
	fsrc2		%f6, %f2
	brnz,pn		$rem, .L${bits}_xts_${dir}steal
	nop

	ret
	restore

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
.align	32
.L${bits}_xts_${dir}blk:
	add	$out, $len, $blk_init
	and	$blk_init, 63, $blk_init	! tail
	sub	$len, $blk_init, $len
	add	$blk_init, 15, $blk_init	! round up to 16n
	srlx	$len, 4, $len
	srl	$blk_init, 4, $blk_init
	sub	$len, 1, $len
	add	$blk_init, 1, $blk_init

.L${bits}_xts_${dir}blk2x:
	ldx		[$inp + 0], %o0
	ldx		[$inp + 8], %o1
	ldx		[$inp + 16], %o2
	brz,pt		$ileft, 5f
	ldx		[$inp + 24], %o3

	ldx		[$inp + 32], %o4
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	or		%g1, %o0, %o0
	sllx		%o1, $ileft, %o1
	srlx		%o2, $iright, %g1
	or		%g1, %o1, %o1
	sllx		%o2, $ileft, %o2
	srlx		%o3, $iright, %g1
	or		%g1, %o2, %o2
	sllx		%o3, $ileft, %o3
	srlx		%o4, $iright, %o4
	or		%o4, %o3, %o3
5:
	movxtod		%g2, %f12
	movxtod		%g3, %f14
	bshuffle	%f12, %f12, %f12
	bshuffle	%f14, %f14, %f14

	srax		%g3, 63, %l7		! next tweak value
	addcc		%g2, %g2, %g2
	and		%l7, 0x87, %l7
	addxc		%g3, %g3, %g3
	xor		%l7, %g2, %g2

	movxtod		%g2, %f8
	movxtod		%g3, %f10
	bshuffle	%f8,  %f8,  %f8
	bshuffle	%f10, %f10, %f10

	xor		%g4, %o0, %o0		! ^= rk[0]
	xor		%g5, %o1, %o1
	xor		%g4, %o2, %o2		! ^= rk[0]
	xor		%g5, %o3, %o3
	movxtod		%o0, %f0
	movxtod		%o1, %f2
	movxtod		%o2, %f4
	movxtod		%o3, %f6

	fxor		%f12, %f0, %f0		! ^= tweak[0]
	fxor		%f14, %f2, %f2
	fxor		%f8,  %f4, %f4		! ^= tweak[0]
	fxor		%f10, %f6, %f6

	prefetch	[$inp + 32+63], 20
	call		_${alg}${bits}_${dir}crypt_2x
	add		$inp, 32, $inp

	movxtod		%g2, %f8
	movxtod		%g3, %f10

	srax		%g3, 63, %l7		! next tweak value
	addcc		%g2, %g2, %g2
	and		%l7, 0x87, %l7
	addxc		%g3, %g3, %g3
	xor		%l7, %g2, %g2

	bshuffle	%f8,  %f8,  %f8
	bshuffle	%f10, %f10, %f10

	fxor		%f12, %f0, %f0		! ^= tweak[0]
	fxor		%f14, %f2, %f2
	fxor		%f8,  %f4, %f4
	fxor		%f10, %f6, %f6

	subcc		$len, 2, $len
	stda		%f0, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	add		$out, 8, $out
	stda		%f2, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	add		$out, 8, $out
	stda		%f4, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	add		$out, 8, $out
	stda		%f6, [$out]0xe2		! ASI_BLK_INIT, T4-specific
	bgu,pt		$::size_t_cc, .L${bits}_xts_${dir}blk2x
	add		$out, 8, $out

	add		$blk_init, $len, $len
	andcc		$len, 1, %g0		! is number of blocks even?
	membar		#StoreLoad|#StoreStore
	bnz,pt		%icc, .L${bits}_xts_${dir}loop
	srl		$len, 0, $len
	brnz,pn		$len, .L${bits}_xts_${dir}loop2x
	nop

	fsrc2		%f4, %f0
	fsrc2		%f6, %f2
	brnz,pn		$rem, .L${bits}_xts_${dir}steal
	nop

	ret
	restore
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
___
$code.=<<___ if ($dir eq "en");
.align	32
.L${bits}_xts_${dir}steal:
	std		%f0, [%fp + $::bias-16]	! copy of output
	std		%f2, [%fp + $::bias-8]

	srl		$ileft, 3, $ileft
	add		%fp, $::bias-16, %l7
	add		$inp, $ileft, $inp	! original $inp+$len&-15
	add		$out, $ooff, $out	! original $out+$len&-15
	mov		0, $ileft
	nop					! align

.L${bits}_xts_${dir}stealing:
	ldub		[$inp + $ileft], %o0
	ldub		[%l7  + $ileft], %o1
	dec		$rem
	stb		%o0, [%l7  + $ileft]
	stb		%o1, [$out + $ileft]
	brnz		$rem, .L${bits}_xts_${dir}stealing
	inc		$ileft

	mov		%l7, $inp
	sub		$out, 16, $out
	mov		0, $ileft
	sub		$out, $ooff, $out
	ba		.L${bits}_xts_${dir}loop	! one more time
	mov		1, $len				! $rem is 0
___
$code.=<<___ if ($dir eq "de");
.align	32
.L${bits}_xts_${dir}steal:
	ldx		[$inp + 0], %o0
	brz,pt		$ileft, 8f
	ldx		[$inp + 8], %o1

	ldx		[$inp + 16], %o2
	sllx		%o0, $ileft, %o0
	srlx		%o1, $iright, %g1
	sllx		%o1, $ileft, %o1
	or		%g1, %o0, %o0
	srlx		%o2, $iright, %o2
	or		%o2, %o1, %o1
8:
	srax		%g3, 63, %l7		! next tweak value
	addcc		%g2, %g2, %o2
	and		%l7, 0x87, %l7
	addxc		%g3, %g3, %o3
	xor		%l7, %o2, %o2

	movxtod		%o2, %f12
	movxtod		%o3, %f14
	bshuffle	%f12, %f12, %f12
	bshuffle	%f14, %f14, %f14

	xor		%g4, %o0, %o0		! ^= rk[0]
	xor		%g5, %o1, %o1
	movxtod		%o0, %f0
	movxtod		%o1, %f2

	fxor		%f12, %f0, %f0		! ^= tweak[0]
	fxor		%f14, %f2, %f2

	call		_${alg}${bits}_${dir}crypt_1x
	add		$inp, 16, $inp

	fxor		%f12, %f0, %f0		! ^= tweak[0]
	fxor		%f14, %f2, %f2

	std		%f0, [%fp + $::bias-16]
	std		%f2, [%fp + $::bias-8]

	srl		$ileft, 3, $ileft
	add		%fp, $::bias-16, %l7
	add		$inp, $ileft, $inp	! original $inp+$len&-15
	add		$out, $ooff, $out	! original $out+$len&-15
	mov		0, $ileft
	add		$out, 16, $out
	nop					! align

.L${bits}_xts_${dir}stealing:
	ldub		[$inp + $ileft], %o0
	ldub		[%l7  + $ileft], %o1
	dec		$rem
	stb		%o0, [%l7  + $ileft]
	stb		%o1, [$out + $ileft]
	brnz		$rem, .L${bits}_xts_${dir}stealing
	inc		$ileft

	mov		%l7, $inp
	sub		$out, 16, $out
	mov		0, $ileft
	sub		$out, $ooff, $out
	ba		.L${bits}_xts_${dir}loop	! one more time
	mov		1, $len				! $rem is 0
___
$code.=<<___;
	ret
	restore
.type	${alg}${bits}_t4_xts_${dir}crypt,#function
.size	${alg}${bits}_t4_xts_${dir}crypt,.-${alg}${bits}_t4_xts_${dir}crypt
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
		"fnot2"		=> 0x066,
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
my %visopf = (	"addxc"		=> 0x011,
		"addxccc"	=> 0x013,
		"umulxhi"	=> 0x016,
		"alignaddr"	=> 0x018,
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

sub unaes_round {	# 4-argument instructions
my ($mnemonic,$rs1,$rs2,$rs3,$rd)=@_;
my ($ref,$opf);
my %aesopf = (	"aes_eround01"	=> 0,
		"aes_eround23"	=> 1,
		"aes_dround01"	=> 2,
		"aes_dround23"	=> 3,
		"aes_eround01_l"=> 4,
		"aes_eround23_l"=> 5,
		"aes_dround01_l"=> 6,
		"aes_dround23_l"=> 7,
		"aes_kexpand1"	=> 8	);

    $ref = "$mnemonic\t$rs1,$rs2,$rs3,$rd";

    if (defined($opf=$aesopf{$mnemonic})) {
	$rs3 = ($rs3 =~ /%f([0-6]*[02468])/) ? (($1|$1>>5)&31) : $rs3;
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
			2<<30|$rd<<25|0x19<<19|$rs1<<14|$rs3<<9|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

sub unaes_kexpand {	# 3-argument instructions
my ($mnemonic,$rs1,$rs2,$rd)=@_;
my ($ref,$opf);
my %aesopf = (	"aes_kexpand0"	=> 0x130,
		"aes_kexpand2"	=> 0x131	);

    $ref = "$mnemonic\t$rs1,$rs2,$rd";

    if (defined($opf=$aesopf{$mnemonic})) {
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
			2<<30|$rd<<25|0x36<<19|$rs1<<14|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

sub uncamellia_f {	# 4-argument instructions
my ($mnemonic,$rs1,$rs2,$rs3,$rd)=@_;
my ($ref,$opf);

    $ref = "$mnemonic\t$rs1,$rs2,$rs3,$rd";

    if (1) {
	$rs3 = ($rs3 =~ /%f([0-6]*[02468])/) ? (($1|$1>>5)&31) : $rs3;
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
			2<<30|$rd<<25|0x19<<19|$rs1<<14|$rs3<<9|0xc<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

sub uncamellia3 {	# 3-argument instructions
my ($mnemonic,$rs1,$rs2,$rd)=@_;
my ($ref,$opf);
my %cmllopf = (	"camellia_fl"	=> 0x13c,
		"camellia_fli"	=> 0x13d	);

    $ref = "$mnemonic\t$rs1,$rs2,$rd";

    if (defined($opf=$cmllopf{$mnemonic})) {
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
			2<<30|$rd<<25|0x36<<19|$rs1<<14|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

sub unmovxtox {		# 2-argument instructions
my ($mnemonic,$rs,$rd)=@_;
my %bias = ( "g" => 0, "o" => 8, "l" => 16, "i" => 24, "f" => 0 );
my ($ref,$opf);
my %movxopf = (	"movdtox"	=> 0x110,
		"movstouw"	=> 0x111,
		"movstosw"	=> 0x113,
		"movxtod"	=> 0x118,
		"movwtos"	=> 0x119	);

    $ref = "$mnemonic\t$rs,$rd";

    if (defined($opf=$movxopf{$mnemonic})) {
	foreach ($rs,$rd) {
	    return $ref if (!/%([fgoli])([0-9]{1,2})/);
	    $_=$bias{$1}+$2;
	    if ($2>=32) {
		return $ref if ($2&1);
		# re-encode for upper double register addressing
		$_=($2|$2>>5)&31;
	    }
	}

	return	sprintf ".word\t0x%08x !%s",
			2<<30|$rd<<25|0x36<<19|$opf<<5|$rs,
			$ref;
    } else {
	return $ref;
    }
}

sub undes {
my ($mnemonic)=shift;
my @args=@_;
my ($ref,$opf);
my %desopf = (	"des_round"	=> 0b1001,
		"des_ip"	=> 0b100110100,
		"des_iip"	=> 0b100110101,
		"des_kexpand"	=> 0b100110110	);

    $ref = "$mnemonic\t".join(",",@_);

    if (defined($opf=$desopf{$mnemonic})) {	# 4-arg
	if ($mnemonic eq "des_round") {
	    foreach (@args[0..3]) {
		return $ref if (!/%f([0-9]{1,2})/);
		$_=$1;
		if ($1>=32) {
		    return $ref if ($1&1);
		    # re-encode for upper double register addressing
		    $_=($1|$1>>5)&31;
		}
	    }
	    return  sprintf ".word\t0x%08x !%s",
			    2<<30|0b011001<<19|$opf<<5|$args[0]<<14|$args[1]|$args[2]<<9|$args[3]<<25,
			    $ref;
	} elsif ($mnemonic eq "des_kexpand") {	# 3-arg
	    foreach (@args[0..2]) {
		return $ref if (!/(%f)?([0-9]{1,2})/);
		$_=$2;
		if ($2>=32) {
		    return $ref if ($2&1);
		    # re-encode for upper double register addressing
		    $_=($2|$2>>5)&31;
		}
	    }
	    return  sprintf ".word\t0x%08x !%s",
			    2<<30|0b110110<<19|$opf<<5|$args[0]<<14|$args[1]|$args[2]<<25,
			    $ref;
	} else {				# 2-arg
	    foreach (@args[0..1]) {
		return $ref if (!/%f([0-9]{1,2})/);
		$_=$1;
		if ($1>=32) {
		    return $ref if ($2&1);
		    # re-encode for upper double register addressing
		    $_=($1|$1>>5)&31;
		}
	    }
	    return  sprintf ".word\t0x%08x !%s",
			    2<<30|0b110110<<19|$opf<<5|$args[0]<<14|$args[1]<<25,
			    $ref;
	}
    } else {
	return $ref;
    }
}

sub emit_assembler {
    foreach (split("\n",$::code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	s/\b(f[a-z]+2[sd]*)\s+(%f[0-9]{1,2}),\s*(%f[0-9]{1,2})\s*$/$1\t%f0,$2,$3/go;

	s/\b(aes_[edk][^\s]*)\s+(%f[0-9]{1,2}),\s*(%f[0-9]{1,2}),\s*([%fx0-9]+),\s*(%f[0-9]{1,2})/
		&unaes_round($1,$2,$3,$4,$5)
	 /geo or
	s/\b(aes_kexpand[02])\s+(%f[0-9]{1,2}),\s*(%f[0-9]{1,2}),\s*(%f[0-9]{1,2})/
		&unaes_kexpand($1,$2,$3,$4)
	 /geo or
	s/\b(camellia_f)\s+(%f[0-9]{1,2}),\s*(%f[0-9]{1,2}),\s*([%fx0-9]+),\s*(%f[0-9]{1,2})/
		&uncamellia_f($1,$2,$3,$4,$5)
	 /geo or
	s/\b(camellia_[^s]+)\s+(%f[0-9]{1,2}),\s*(%f[0-9]{1,2}),\s*(%f[0-9]{1,2})/
		&uncamellia3($1,$2,$3,$4)
	 /geo or
	s/\b(des_\w+)\s+(%f[0-9]{1,2}),\s*([%fx0-9]+)(?:,\s*(%f[0-9]{1,2})(?:,\s*(%f[0-9]{1,2}))?)?/
		&undes($1,$2,$3,$4,$5)
	 /geo or
	s/\b(mov[ds]to\w+)\s+(%f[0-9]{1,2}),\s*(%[goli][0-7])/
		&unmovxtox($1,$2,$3)
	 /geo or
	s/\b(mov[xw]to[ds])\s+(%[goli][0-7]),\s*(%f[0-9]{1,2})/
		&unmovxtox($1,$2,$3)
	 /geo or
	s/\b([fb][^\s]*)\s+(%f[0-9]{1,2}),\s*(%f[0-9]{1,2}),\s*(%f[0-9]{1,2})/
		&unvis($1,$2,$3,$4)
	 /geo or
	s/\b(umulxhi|bmask|addxc[c]{0,2}|alignaddr[l]*)\s+(%[goli][0-7]),\s*(%[goli][0-7]),\s*(%[goli][0-7])/
		&unvis3($1,$2,$3,$4)
	 /geo;

	print $_,"\n";
    }
}

1;

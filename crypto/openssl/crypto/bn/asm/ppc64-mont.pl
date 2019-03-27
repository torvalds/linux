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

# December 2007

# The reason for undertaken effort is basically following. Even though
# Power 6 CPU operates at incredible 4.7GHz clock frequency, its PKI
# performance was observed to be less than impressive, essentially as
# fast as 1.8GHz PPC970, or 2.6 times(!) slower than one would hope.
# Well, it's not surprising that IBM had to make some sacrifices to
# boost the clock frequency that much, but no overall improvement?
# Having observed how much difference did switching to FPU make on
# UltraSPARC, playing same stunt on Power 6 appeared appropriate...
# Unfortunately the resulting performance improvement is not as
# impressive, ~30%, and in absolute terms is still very far from what
# one would expect from 4.7GHz CPU. There is a chance that I'm doing
# something wrong, but in the lack of assembler level micro-profiling
# data or at least decent platform guide I can't tell... Or better
# results might be achieved with VMX... Anyway, this module provides
# *worse* performance on other PowerPC implementations, ~40-15% slower
# on PPC970 depending on key length and ~40% slower on Power 5 for all
# key lengths. As it's obviously inappropriate as "best all-round"
# alternative, it has to be complemented with run-time CPU family
# detection. Oh! It should also be noted that unlike other PowerPC
# implementation IALU ppc-mont.pl module performs *suboptimally* on
# >=1024-bit key lengths on Power 6. It should also be noted that
# *everything* said so far applies to 64-bit builds! As far as 32-bit
# application executed on 64-bit CPU goes, this module is likely to
# become preferred choice, because it's easy to adapt it for such
# case and *is* faster than 32-bit ppc-mont.pl on *all* processors.

# February 2008

# Micro-profiling assisted optimization results in ~15% improvement
# over original ppc64-mont.pl version, or overall ~50% improvement
# over ppc.pl module on Power 6. If compared to ppc-mont.pl on same
# Power 6 CPU, this module is 5-150% faster depending on key length,
# [hereafter] more for longer keys. But if compared to ppc-mont.pl
# on 1.8GHz PPC970, it's only 5-55% faster. Still far from impressive
# in absolute terms, but it's apparently the way Power 6 is...

# December 2009

# Adapted for 32-bit build this module delivers 25-120%, yes, more
# than *twice* for longer keys, performance improvement over 32-bit
# ppc-mont.pl on 1.8GHz PPC970. However! This implementation utilizes
# even 64-bit integer operations and the trouble is that most PPC
# operating systems don't preserve upper halves of general purpose
# registers upon 32-bit signal delivery. They do preserve them upon
# context switch, but not signalling:-( This means that asynchronous
# signals have to be blocked upon entry to this subroutine. Signal
# masking (and of course complementary unmasking) has quite an impact
# on performance, naturally larger for shorter keys. It's so severe
# that 512-bit key performance can be as low as 1/3 of expected one.
# This is why this routine can be engaged for longer key operations
# only on these OSes, see crypto/ppccap.c for further details. MacOS X
# is an exception from this and doesn't require signal masking, and
# that's where above improvement coefficients were collected. For
# others alternative would be to break dependence on upper halves of
# GPRs by sticking to 32-bit integer operations...

# December 2012

# Remove above mentioned dependence on GPRs' upper halves in 32-bit
# build. No signal masking overhead, but integer instructions are
# *more* numerous... It's still "universally" faster than 32-bit
# ppc-mont.pl, but improvement coefficient is not as impressive
# for longer keys...

$flavour = shift;

if ($flavour =~ /32/) {
	$SIZE_T=4;
	$RZONE=	224;
	$fname=	"bn_mul_mont_fpu64";

	$STUX=	"stwux";	# store indexed and update
	$PUSH=	"stw";
	$POP=	"lwz";
} elsif ($flavour =~ /64/) {
	$SIZE_T=8;
	$RZONE=	288;
	$fname=	"bn_mul_mont_fpu64";

	# same as above, but 64-bit mnemonics...
	$STUX=	"stdux";	# store indexed and update
	$PUSH=	"std";
	$POP=	"ld";
} else { die "nonsense $flavour"; }

$LITTLE_ENDIAN = ($flavour=~/le$/) ? 4 : 0;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour ".shift || die "can't call $xlate: $!";

$FRAME=64;	# padded frame header
$TRANSFER=16*8;

$carry="r0";
$sp="r1";
$toc="r2";
$rp="r3";	$ovf="r3";
$ap="r4";
$bp="r5";
$np="r6";
$n0="r7";
$num="r8";
$rp="r9";	# $rp is reassigned
$tp="r10";
$j="r11";
$i="r12";
# non-volatile registers
$c1="r19";
$n1="r20";
$a1="r21";
$nap_d="r22";	# interleaved ap and np in double format
$a0="r23";	# ap[0]
$t0="r24";	# temporary registers
$t1="r25";
$t2="r26";
$t3="r27";
$t4="r28";
$t5="r29";
$t6="r30";
$t7="r31";

# PPC offers enough register bank capacity to unroll inner loops twice
#
#     ..A3A2A1A0
#           dcba
#    -----------
#            A0a
#           A0b
#          A0c
#         A0d
#          A1a
#         A1b
#        A1c
#       A1d
#        A2a
#       A2b
#      A2c
#     A2d
#      A3a
#     A3b
#    A3c
#   A3d
#    ..a
#   ..b
#
$ba="f0";	$bb="f1";	$bc="f2";	$bd="f3";
$na="f4";	$nb="f5";	$nc="f6";	$nd="f7";
$dota="f8";	$dotb="f9";
$A0="f10";	$A1="f11";	$A2="f12";	$A3="f13";
$N0="f20";	$N1="f21";	$N2="f22";	$N3="f23";
$T0a="f24";	$T0b="f25";
$T1a="f26";	$T1b="f27";
$T2a="f28";	$T2b="f29";
$T3a="f30";	$T3b="f31";

# sp----------->+-------------------------------+
#		| saved sp			|
#		+-------------------------------+
#		.				.
#   +64		+-------------------------------+
#		| 16 gpr<->fpr transfer zone	|
#		.				.
#		.				.
#   +16*8	+-------------------------------+
#		| __int64 tmp[-1]		|
#		+-------------------------------+
#		| __int64 tmp[num]		|
#		.				.
#		.				.
#		.				.
#   +(num+1)*8	+-------------------------------+
#		| padding to 64 byte boundary	|
#		.				.
#   +X		+-------------------------------+
#		| double nap_d[4*num]		|
#		.				.
#		.				.
#		.				.
#		+-------------------------------+
#		.				.
#   -13*size_t	+-------------------------------+
#		| 13 saved gpr, r19-r31		|
#		.				.
#		.				.
#   -12*8	+-------------------------------+
#		| 12 saved fpr, f20-f31		|
#		.				.
#		.				.
#		+-------------------------------+

$code=<<___;
.machine "any"
.text

.globl	.$fname
.align	5
.$fname:
	cmpwi	$num,`3*8/$SIZE_T`
	mr	$rp,r3		; $rp is reassigned
	li	r3,0		; possible "not handled" return code
	bltlr-
	andi.	r0,$num,`16/$SIZE_T-1`		; $num has to be "even"
	bnelr-

	slwi	$num,$num,`log($SIZE_T)/log(2)`	; num*=sizeof(BN_LONG)
	li	$i,-4096
	slwi	$tp,$num,2	; place for {an}p_{lh}[num], i.e. 4*num
	add	$tp,$tp,$num	; place for tp[num+1]
	addi	$tp,$tp,`$FRAME+$TRANSFER+8+64+$RZONE`
	subf	$tp,$tp,$sp	; $sp-$tp
	and	$tp,$tp,$i	; minimize TLB usage
	subf	$tp,$sp,$tp	; $tp-$sp
	mr	$i,$sp
	$STUX	$sp,$sp,$tp	; alloca

	$PUSH	r19,`-12*8-13*$SIZE_T`($i)
	$PUSH	r20,`-12*8-12*$SIZE_T`($i)
	$PUSH	r21,`-12*8-11*$SIZE_T`($i)
	$PUSH	r22,`-12*8-10*$SIZE_T`($i)
	$PUSH	r23,`-12*8-9*$SIZE_T`($i)
	$PUSH	r24,`-12*8-8*$SIZE_T`($i)
	$PUSH	r25,`-12*8-7*$SIZE_T`($i)
	$PUSH	r26,`-12*8-6*$SIZE_T`($i)
	$PUSH	r27,`-12*8-5*$SIZE_T`($i)
	$PUSH	r28,`-12*8-4*$SIZE_T`($i)
	$PUSH	r29,`-12*8-3*$SIZE_T`($i)
	$PUSH	r30,`-12*8-2*$SIZE_T`($i)
	$PUSH	r31,`-12*8-1*$SIZE_T`($i)
	stfd	f20,`-12*8`($i)
	stfd	f21,`-11*8`($i)
	stfd	f22,`-10*8`($i)
	stfd	f23,`-9*8`($i)
	stfd	f24,`-8*8`($i)
	stfd	f25,`-7*8`($i)
	stfd	f26,`-6*8`($i)
	stfd	f27,`-5*8`($i)
	stfd	f28,`-4*8`($i)
	stfd	f29,`-3*8`($i)
	stfd	f30,`-2*8`($i)
	stfd	f31,`-1*8`($i)

	addi	$tp,$sp,`$FRAME+$TRANSFER+8+64`
	li	$i,-64
	add	$nap_d,$tp,$num
	and	$nap_d,$nap_d,$i	; align to 64 bytes
	; nap_d is off by 1, because it's used with stfdu/lfdu
	addi	$nap_d,$nap_d,-8
	srwi	$j,$num,`3+1`	; counter register, num/2
	addi	$j,$j,-1
	addi	$tp,$sp,`$FRAME+$TRANSFER-8`
	li	$carry,0
	mtctr	$j
___

$code.=<<___ if ($SIZE_T==8);
	ld	$a0,0($ap)		; pull ap[0] value
	ld	$t3,0($bp)		; bp[0]
	ld	$n0,0($n0)		; pull n0[0] value

	mulld	$t7,$a0,$t3		; ap[0]*bp[0]
	; transfer bp[0] to FPU as 4x16-bit values
	extrdi	$t0,$t3,16,48
	extrdi	$t1,$t3,16,32
	extrdi	$t2,$t3,16,16
	extrdi	$t3,$t3,16,0
	std	$t0,`$FRAME+0`($sp)
	std	$t1,`$FRAME+8`($sp)
	std	$t2,`$FRAME+16`($sp)
	std	$t3,`$FRAME+24`($sp)

	mulld	$t7,$t7,$n0		; tp[0]*n0
	; transfer (ap[0]*bp[0])*n0 to FPU as 4x16-bit values
	extrdi	$t4,$t7,16,48
	extrdi	$t5,$t7,16,32
	extrdi	$t6,$t7,16,16
	extrdi	$t7,$t7,16,0
	std	$t4,`$FRAME+32`($sp)
	std	$t5,`$FRAME+40`($sp)
	std	$t6,`$FRAME+48`($sp)
	std	$t7,`$FRAME+56`($sp)

	extrdi	$t0,$a0,32,32		; lwz	$t0,4($ap)
	extrdi	$t1,$a0,32,0		; lwz	$t1,0($ap)
	lwz	$t2,`12^$LITTLE_ENDIAN`($ap)	; load a[1] as 32-bit word pair
	lwz	$t3,`8^$LITTLE_ENDIAN`($ap)
	lwz	$t4,`4^$LITTLE_ENDIAN`($np)	; load n[0] as 32-bit word pair
	lwz	$t5,`0^$LITTLE_ENDIAN`($np)
	lwz	$t6,`12^$LITTLE_ENDIAN`($np)	; load n[1] as 32-bit word pair
	lwz	$t7,`8^$LITTLE_ENDIAN`($np)
___
$code.=<<___ if ($SIZE_T==4);
	lwz	$a0,0($ap)		; pull ap[0,1] value
	mr	$n1,$n0
	lwz	$a1,4($ap)
	li	$c1,0
	lwz	$t1,0($bp)		; bp[0,1]
	lwz	$t3,4($bp)
	lwz	$n0,0($n1)		; pull n0[0,1] value
	lwz	$n1,4($n1)

	mullw	$t4,$a0,$t1		; mulld ap[0]*bp[0]
	mulhwu	$t5,$a0,$t1
	mullw	$t6,$a1,$t1
	mullw	$t7,$a0,$t3
	add	$t5,$t5,$t6
	add	$t5,$t5,$t7
	; transfer bp[0] to FPU as 4x16-bit values
	extrwi	$t0,$t1,16,16
	extrwi	$t1,$t1,16,0
	extrwi	$t2,$t3,16,16
	extrwi	$t3,$t3,16,0
	std	$t0,`$FRAME+0`($sp)	; yes, std in 32-bit build
	std	$t1,`$FRAME+8`($sp)
	std	$t2,`$FRAME+16`($sp)
	std	$t3,`$FRAME+24`($sp)

	mullw	$t0,$t4,$n0		; mulld tp[0]*n0
	mulhwu	$t1,$t4,$n0
	mullw	$t2,$t5,$n0
	mullw	$t3,$t4,$n1
	add	$t1,$t1,$t2
	add	$t1,$t1,$t3
	; transfer (ap[0]*bp[0])*n0 to FPU as 4x16-bit values
	extrwi	$t4,$t0,16,16
	extrwi	$t5,$t0,16,0
	extrwi	$t6,$t1,16,16
	extrwi	$t7,$t1,16,0
	std	$t4,`$FRAME+32`($sp)	; yes, std in 32-bit build
	std	$t5,`$FRAME+40`($sp)
	std	$t6,`$FRAME+48`($sp)
	std	$t7,`$FRAME+56`($sp)

	mr	$t0,$a0			; lwz	$t0,0($ap)
	mr	$t1,$a1			; lwz	$t1,4($ap)
	lwz	$t2,8($ap)		; load a[j..j+3] as 32-bit word pairs
	lwz	$t3,12($ap)
	lwz	$t4,0($np)		; load n[j..j+3] as 32-bit word pairs
	lwz	$t5,4($np)
	lwz	$t6,8($np)
	lwz	$t7,12($np)
___
$code.=<<___;
	lfd	$ba,`$FRAME+0`($sp)
	lfd	$bb,`$FRAME+8`($sp)
	lfd	$bc,`$FRAME+16`($sp)
	lfd	$bd,`$FRAME+24`($sp)
	lfd	$na,`$FRAME+32`($sp)
	lfd	$nb,`$FRAME+40`($sp)
	lfd	$nc,`$FRAME+48`($sp)
	lfd	$nd,`$FRAME+56`($sp)
	std	$t0,`$FRAME+64`($sp)	; yes, std even in 32-bit build
	std	$t1,`$FRAME+72`($sp)
	std	$t2,`$FRAME+80`($sp)
	std	$t3,`$FRAME+88`($sp)
	std	$t4,`$FRAME+96`($sp)
	std	$t5,`$FRAME+104`($sp)
	std	$t6,`$FRAME+112`($sp)
	std	$t7,`$FRAME+120`($sp)
	fcfid	$ba,$ba
	fcfid	$bb,$bb
	fcfid	$bc,$bc
	fcfid	$bd,$bd
	fcfid	$na,$na
	fcfid	$nb,$nb
	fcfid	$nc,$nc
	fcfid	$nd,$nd

	lfd	$A0,`$FRAME+64`($sp)
	lfd	$A1,`$FRAME+72`($sp)
	lfd	$A2,`$FRAME+80`($sp)
	lfd	$A3,`$FRAME+88`($sp)
	lfd	$N0,`$FRAME+96`($sp)
	lfd	$N1,`$FRAME+104`($sp)
	lfd	$N2,`$FRAME+112`($sp)
	lfd	$N3,`$FRAME+120`($sp)
	fcfid	$A0,$A0
	fcfid	$A1,$A1
	fcfid	$A2,$A2
	fcfid	$A3,$A3
	fcfid	$N0,$N0
	fcfid	$N1,$N1
	fcfid	$N2,$N2
	fcfid	$N3,$N3
	addi	$ap,$ap,16
	addi	$np,$np,16

	fmul	$T1a,$A1,$ba
	fmul	$T1b,$A1,$bb
	stfd	$A0,8($nap_d)		; save a[j] in double format
	stfd	$A1,16($nap_d)
	fmul	$T2a,$A2,$ba
	fmul	$T2b,$A2,$bb
	stfd	$A2,24($nap_d)		; save a[j+1] in double format
	stfd	$A3,32($nap_d)
	fmul	$T3a,$A3,$ba
	fmul	$T3b,$A3,$bb
	stfd	$N0,40($nap_d)		; save n[j] in double format
	stfd	$N1,48($nap_d)
	fmul	$T0a,$A0,$ba
	fmul	$T0b,$A0,$bb
	stfd	$N2,56($nap_d)		; save n[j+1] in double format
	stfdu	$N3,64($nap_d)

	fmadd	$T1a,$A0,$bc,$T1a
	fmadd	$T1b,$A0,$bd,$T1b
	fmadd	$T2a,$A1,$bc,$T2a
	fmadd	$T2b,$A1,$bd,$T2b
	fmadd	$T3a,$A2,$bc,$T3a
	fmadd	$T3b,$A2,$bd,$T3b
	fmul	$dota,$A3,$bc
	fmul	$dotb,$A3,$bd

	fmadd	$T1a,$N1,$na,$T1a
	fmadd	$T1b,$N1,$nb,$T1b
	fmadd	$T2a,$N2,$na,$T2a
	fmadd	$T2b,$N2,$nb,$T2b
	fmadd	$T3a,$N3,$na,$T3a
	fmadd	$T3b,$N3,$nb,$T3b
	fmadd	$T0a,$N0,$na,$T0a
	fmadd	$T0b,$N0,$nb,$T0b

	fmadd	$T1a,$N0,$nc,$T1a
	fmadd	$T1b,$N0,$nd,$T1b
	fmadd	$T2a,$N1,$nc,$T2a
	fmadd	$T2b,$N1,$nd,$T2b
	fmadd	$T3a,$N2,$nc,$T3a
	fmadd	$T3b,$N2,$nd,$T3b
	fmadd	$dota,$N3,$nc,$dota
	fmadd	$dotb,$N3,$nd,$dotb

	fctid	$T0a,$T0a
	fctid	$T0b,$T0b
	fctid	$T1a,$T1a
	fctid	$T1b,$T1b
	fctid	$T2a,$T2a
	fctid	$T2b,$T2b
	fctid	$T3a,$T3a
	fctid	$T3b,$T3b

	stfd	$T0a,`$FRAME+0`($sp)
	stfd	$T0b,`$FRAME+8`($sp)
	stfd	$T1a,`$FRAME+16`($sp)
	stfd	$T1b,`$FRAME+24`($sp)
	stfd	$T2a,`$FRAME+32`($sp)
	stfd	$T2b,`$FRAME+40`($sp)
	stfd	$T3a,`$FRAME+48`($sp)
	stfd	$T3b,`$FRAME+56`($sp)

.align	5
L1st:
___
$code.=<<___ if ($SIZE_T==8);
	lwz	$t0,`4^$LITTLE_ENDIAN`($ap)	; load a[j] as 32-bit word pair
	lwz	$t1,`0^$LITTLE_ENDIAN`($ap)
	lwz	$t2,`12^$LITTLE_ENDIAN`($ap)	; load a[j+1] as 32-bit word pair
	lwz	$t3,`8^$LITTLE_ENDIAN`($ap)
	lwz	$t4,`4^$LITTLE_ENDIAN`($np)	; load n[j] as 32-bit word pair
	lwz	$t5,`0^$LITTLE_ENDIAN`($np)
	lwz	$t6,`12^$LITTLE_ENDIAN`($np)	; load n[j+1] as 32-bit word pair
	lwz	$t7,`8^$LITTLE_ENDIAN`($np)
___
$code.=<<___ if ($SIZE_T==4);
	lwz	$t0,0($ap)		; load a[j..j+3] as 32-bit word pairs
	lwz	$t1,4($ap)
	lwz	$t2,8($ap)
	lwz	$t3,12($ap)
	lwz	$t4,0($np)		; load n[j..j+3] as 32-bit word pairs
	lwz	$t5,4($np)
	lwz	$t6,8($np)
	lwz	$t7,12($np)
___
$code.=<<___;
	std	$t0,`$FRAME+64`($sp)	; yes, std even in 32-bit build
	std	$t1,`$FRAME+72`($sp)
	std	$t2,`$FRAME+80`($sp)
	std	$t3,`$FRAME+88`($sp)
	std	$t4,`$FRAME+96`($sp)
	std	$t5,`$FRAME+104`($sp)
	std	$t6,`$FRAME+112`($sp)
	std	$t7,`$FRAME+120`($sp)
___
if ($SIZE_T==8 or $flavour =~ /osx/) {
$code.=<<___;
	ld	$t0,`$FRAME+0`($sp)
	ld	$t1,`$FRAME+8`($sp)
	ld	$t2,`$FRAME+16`($sp)
	ld	$t3,`$FRAME+24`($sp)
	ld	$t4,`$FRAME+32`($sp)
	ld	$t5,`$FRAME+40`($sp)
	ld	$t6,`$FRAME+48`($sp)
	ld	$t7,`$FRAME+56`($sp)
___
} else {
$code.=<<___;
	lwz	$t1,`$FRAME+0^$LITTLE_ENDIAN`($sp)
	lwz	$t0,`$FRAME+4^$LITTLE_ENDIAN`($sp)
	lwz	$t3,`$FRAME+8^$LITTLE_ENDIAN`($sp)
	lwz	$t2,`$FRAME+12^$LITTLE_ENDIAN`($sp)
	lwz	$t5,`$FRAME+16^$LITTLE_ENDIAN`($sp)
	lwz	$t4,`$FRAME+20^$LITTLE_ENDIAN`($sp)
	lwz	$t7,`$FRAME+24^$LITTLE_ENDIAN`($sp)
	lwz	$t6,`$FRAME+28^$LITTLE_ENDIAN`($sp)
___
}
$code.=<<___;
	lfd	$A0,`$FRAME+64`($sp)
	lfd	$A1,`$FRAME+72`($sp)
	lfd	$A2,`$FRAME+80`($sp)
	lfd	$A3,`$FRAME+88`($sp)
	lfd	$N0,`$FRAME+96`($sp)
	lfd	$N1,`$FRAME+104`($sp)
	lfd	$N2,`$FRAME+112`($sp)
	lfd	$N3,`$FRAME+120`($sp)
	fcfid	$A0,$A0
	fcfid	$A1,$A1
	fcfid	$A2,$A2
	fcfid	$A3,$A3
	fcfid	$N0,$N0
	fcfid	$N1,$N1
	fcfid	$N2,$N2
	fcfid	$N3,$N3
	addi	$ap,$ap,16
	addi	$np,$np,16

	fmul	$T1a,$A1,$ba
	fmul	$T1b,$A1,$bb
	fmul	$T2a,$A2,$ba
	fmul	$T2b,$A2,$bb
	stfd	$A0,8($nap_d)		; save a[j] in double format
	stfd	$A1,16($nap_d)
	fmul	$T3a,$A3,$ba
	fmul	$T3b,$A3,$bb
	fmadd	$T0a,$A0,$ba,$dota
	fmadd	$T0b,$A0,$bb,$dotb
	stfd	$A2,24($nap_d)		; save a[j+1] in double format
	stfd	$A3,32($nap_d)
___
if ($SIZE_T==8 or $flavour =~ /osx/) {
$code.=<<___;
	fmadd	$T1a,$A0,$bc,$T1a
	fmadd	$T1b,$A0,$bd,$T1b
	fmadd	$T2a,$A1,$bc,$T2a
	fmadd	$T2b,$A1,$bd,$T2b
	stfd	$N0,40($nap_d)		; save n[j] in double format
	stfd	$N1,48($nap_d)
	fmadd	$T3a,$A2,$bc,$T3a
	fmadd	$T3b,$A2,$bd,$T3b
	 add	$t0,$t0,$carry		; can not overflow
	fmul	$dota,$A3,$bc
	fmul	$dotb,$A3,$bd
	stfd	$N2,56($nap_d)		; save n[j+1] in double format
	stfdu	$N3,64($nap_d)
	 srdi	$carry,$t0,16
	 add	$t1,$t1,$carry
	 srdi	$carry,$t1,16

	fmadd	$T1a,$N1,$na,$T1a
	fmadd	$T1b,$N1,$nb,$T1b
	 insrdi	$t0,$t1,16,32
	fmadd	$T2a,$N2,$na,$T2a
	fmadd	$T2b,$N2,$nb,$T2b
	 add	$t2,$t2,$carry
	fmadd	$T3a,$N3,$na,$T3a
	fmadd	$T3b,$N3,$nb,$T3b
	 srdi	$carry,$t2,16
	fmadd	$T0a,$N0,$na,$T0a
	fmadd	$T0b,$N0,$nb,$T0b
	 insrdi	$t0,$t2,16,16
	 add	$t3,$t3,$carry
	 srdi	$carry,$t3,16

	fmadd	$T1a,$N0,$nc,$T1a
	fmadd	$T1b,$N0,$nd,$T1b
	 insrdi	$t0,$t3,16,0		; 0..63 bits
	fmadd	$T2a,$N1,$nc,$T2a
	fmadd	$T2b,$N1,$nd,$T2b
	 add	$t4,$t4,$carry
	fmadd	$T3a,$N2,$nc,$T3a
	fmadd	$T3b,$N2,$nd,$T3b
	 srdi	$carry,$t4,16
	fmadd	$dota,$N3,$nc,$dota
	fmadd	$dotb,$N3,$nd,$dotb
	 add	$t5,$t5,$carry
	 srdi	$carry,$t5,16
	 insrdi	$t4,$t5,16,32

	fctid	$T0a,$T0a
	fctid	$T0b,$T0b
	 add	$t6,$t6,$carry
	fctid	$T1a,$T1a
	fctid	$T1b,$T1b
	 srdi	$carry,$t6,16
	fctid	$T2a,$T2a
	fctid	$T2b,$T2b
	 insrdi	$t4,$t6,16,16
	fctid	$T3a,$T3a
	fctid	$T3b,$T3b
	 add	$t7,$t7,$carry
	 insrdi	$t4,$t7,16,0		; 64..127 bits
	 srdi	$carry,$t7,16		; upper 33 bits

	stfd	$T0a,`$FRAME+0`($sp)
	stfd	$T0b,`$FRAME+8`($sp)
	stfd	$T1a,`$FRAME+16`($sp)
	stfd	$T1b,`$FRAME+24`($sp)
	stfd	$T2a,`$FRAME+32`($sp)
	stfd	$T2b,`$FRAME+40`($sp)
	stfd	$T3a,`$FRAME+48`($sp)
	stfd	$T3b,`$FRAME+56`($sp)
	 std	$t0,8($tp)		; tp[j-1]
	 stdu	$t4,16($tp)		; tp[j]
___
} else {
$code.=<<___;
	fmadd	$T1a,$A0,$bc,$T1a
	fmadd	$T1b,$A0,$bd,$T1b
	 addc	$t0,$t0,$carry
	 adde	$t1,$t1,$c1
	 srwi	$carry,$t0,16
	fmadd	$T2a,$A1,$bc,$T2a
	fmadd	$T2b,$A1,$bd,$T2b
	stfd	$N0,40($nap_d)		; save n[j] in double format
	stfd	$N1,48($nap_d)
	 srwi	$c1,$t1,16
	 insrwi	$carry,$t1,16,0
	fmadd	$T3a,$A2,$bc,$T3a
	fmadd	$T3b,$A2,$bd,$T3b
	 addc	$t2,$t2,$carry
	 adde	$t3,$t3,$c1
	 srwi	$carry,$t2,16
	fmul	$dota,$A3,$bc
	fmul	$dotb,$A3,$bd
	stfd	$N2,56($nap_d)		; save n[j+1] in double format
	stfdu	$N3,64($nap_d)
	 insrwi	$t0,$t2,16,0		; 0..31 bits
	 srwi	$c1,$t3,16
	 insrwi	$carry,$t3,16,0

	fmadd	$T1a,$N1,$na,$T1a
	fmadd	$T1b,$N1,$nb,$T1b
	 lwz	$t3,`$FRAME+32^$LITTLE_ENDIAN`($sp)	; permuted $t1
	 lwz	$t2,`$FRAME+36^$LITTLE_ENDIAN`($sp)	; permuted $t0
	 addc	$t4,$t4,$carry
	 adde	$t5,$t5,$c1
	 srwi	$carry,$t4,16
	fmadd	$T2a,$N2,$na,$T2a
	fmadd	$T2b,$N2,$nb,$T2b
	 srwi	$c1,$t5,16
	 insrwi	$carry,$t5,16,0
	fmadd	$T3a,$N3,$na,$T3a
	fmadd	$T3b,$N3,$nb,$T3b
	 addc	$t6,$t6,$carry
	 adde	$t7,$t7,$c1
	 srwi	$carry,$t6,16
	fmadd	$T0a,$N0,$na,$T0a
	fmadd	$T0b,$N0,$nb,$T0b
	 insrwi	$t4,$t6,16,0		; 32..63 bits
	 srwi	$c1,$t7,16
	 insrwi	$carry,$t7,16,0

	fmadd	$T1a,$N0,$nc,$T1a
	fmadd	$T1b,$N0,$nd,$T1b
	 lwz	$t7,`$FRAME+40^$LITTLE_ENDIAN`($sp)	; permuted $t3
	 lwz	$t6,`$FRAME+44^$LITTLE_ENDIAN`($sp)	; permuted $t2
	 addc	$t2,$t2,$carry
	 adde	$t3,$t3,$c1
	 srwi	$carry,$t2,16
	fmadd	$T2a,$N1,$nc,$T2a
	fmadd	$T2b,$N1,$nd,$T2b
	 stw	$t0,12($tp)		; tp[j-1]
	 stw	$t4,8($tp)
	 srwi	$c1,$t3,16
	 insrwi	$carry,$t3,16,0
	fmadd	$T3a,$N2,$nc,$T3a
	fmadd	$T3b,$N2,$nd,$T3b
	 lwz	$t1,`$FRAME+48^$LITTLE_ENDIAN`($sp)	; permuted $t5
	 lwz	$t0,`$FRAME+52^$LITTLE_ENDIAN`($sp)	; permuted $t4
	 addc	$t6,$t6,$carry
	 adde	$t7,$t7,$c1
	 srwi	$carry,$t6,16
	fmadd	$dota,$N3,$nc,$dota
	fmadd	$dotb,$N3,$nd,$dotb
	 insrwi	$t2,$t6,16,0		; 64..95 bits
	 srwi	$c1,$t7,16
	 insrwi	$carry,$t7,16,0

	fctid	$T0a,$T0a
	fctid	$T0b,$T0b
	 lwz	$t5,`$FRAME+56^$LITTLE_ENDIAN`($sp)	; permuted $t7
	 lwz	$t4,`$FRAME+60^$LITTLE_ENDIAN`($sp)	; permuted $t6
	 addc	$t0,$t0,$carry
	 adde	$t1,$t1,$c1
	 srwi	$carry,$t0,16
	fctid	$T1a,$T1a
	fctid	$T1b,$T1b
	 srwi	$c1,$t1,16
	 insrwi	$carry,$t1,16,0
	fctid	$T2a,$T2a
	fctid	$T2b,$T2b
	 addc	$t4,$t4,$carry
	 adde	$t5,$t5,$c1
	 srwi	$carry,$t4,16
	fctid	$T3a,$T3a
	fctid	$T3b,$T3b
	 insrwi	$t0,$t4,16,0		; 96..127 bits
	 srwi	$c1,$t5,16
	 insrwi	$carry,$t5,16,0

	stfd	$T0a,`$FRAME+0`($sp)
	stfd	$T0b,`$FRAME+8`($sp)
	stfd	$T1a,`$FRAME+16`($sp)
	stfd	$T1b,`$FRAME+24`($sp)
	stfd	$T2a,`$FRAME+32`($sp)
	stfd	$T2b,`$FRAME+40`($sp)
	stfd	$T3a,`$FRAME+48`($sp)
	stfd	$T3b,`$FRAME+56`($sp)
	 stw	$t2,20($tp)		; tp[j]
	 stwu	$t0,16($tp)
___
}
$code.=<<___;
	bdnz	L1st

	fctid	$dota,$dota
	fctid	$dotb,$dotb
___
if ($SIZE_T==8 or $flavour =~ /osx/) {
$code.=<<___;
	ld	$t0,`$FRAME+0`($sp)
	ld	$t1,`$FRAME+8`($sp)
	ld	$t2,`$FRAME+16`($sp)
	ld	$t3,`$FRAME+24`($sp)
	ld	$t4,`$FRAME+32`($sp)
	ld	$t5,`$FRAME+40`($sp)
	ld	$t6,`$FRAME+48`($sp)
	ld	$t7,`$FRAME+56`($sp)
	stfd	$dota,`$FRAME+64`($sp)
	stfd	$dotb,`$FRAME+72`($sp)

	add	$t0,$t0,$carry		; can not overflow
	srdi	$carry,$t0,16
	add	$t1,$t1,$carry
	srdi	$carry,$t1,16
	insrdi	$t0,$t1,16,32
	add	$t2,$t2,$carry
	srdi	$carry,$t2,16
	insrdi	$t0,$t2,16,16
	add	$t3,$t3,$carry
	srdi	$carry,$t3,16
	insrdi	$t0,$t3,16,0		; 0..63 bits
	add	$t4,$t4,$carry
	srdi	$carry,$t4,16
	add	$t5,$t5,$carry
	srdi	$carry,$t5,16
	insrdi	$t4,$t5,16,32
	add	$t6,$t6,$carry
	srdi	$carry,$t6,16
	insrdi	$t4,$t6,16,16
	add	$t7,$t7,$carry
	insrdi	$t4,$t7,16,0		; 64..127 bits
	srdi	$carry,$t7,16		; upper 33 bits
	ld	$t6,`$FRAME+64`($sp)
	ld	$t7,`$FRAME+72`($sp)

	std	$t0,8($tp)		; tp[j-1]
	stdu	$t4,16($tp)		; tp[j]

	add	$t6,$t6,$carry		; can not overflow
	srdi	$carry,$t6,16
	add	$t7,$t7,$carry
	insrdi	$t6,$t7,48,0
	srdi	$ovf,$t7,48
	std	$t6,8($tp)		; tp[num-1]
___
} else {
$code.=<<___;
	lwz	$t1,`$FRAME+0^$LITTLE_ENDIAN`($sp)
	lwz	$t0,`$FRAME+4^$LITTLE_ENDIAN`($sp)
	lwz	$t3,`$FRAME+8^$LITTLE_ENDIAN`($sp)
	lwz	$t2,`$FRAME+12^$LITTLE_ENDIAN`($sp)
	lwz	$t5,`$FRAME+16^$LITTLE_ENDIAN`($sp)
	lwz	$t4,`$FRAME+20^$LITTLE_ENDIAN`($sp)
	lwz	$t7,`$FRAME+24^$LITTLE_ENDIAN`($sp)
	lwz	$t6,`$FRAME+28^$LITTLE_ENDIAN`($sp)
	stfd	$dota,`$FRAME+64`($sp)
	stfd	$dotb,`$FRAME+72`($sp)

	addc	$t0,$t0,$carry
	adde	$t1,$t1,$c1
	srwi	$carry,$t0,16
	insrwi	$carry,$t1,16,0
	srwi	$c1,$t1,16
	addc	$t2,$t2,$carry
	adde	$t3,$t3,$c1
	srwi	$carry,$t2,16
	 insrwi	$t0,$t2,16,0		; 0..31 bits
	insrwi	$carry,$t3,16,0
	srwi	$c1,$t3,16
	addc	$t4,$t4,$carry
	adde	$t5,$t5,$c1
	srwi	$carry,$t4,16
	insrwi	$carry,$t5,16,0
	srwi	$c1,$t5,16
	addc	$t6,$t6,$carry
	adde	$t7,$t7,$c1
	srwi	$carry,$t6,16
	 insrwi	$t4,$t6,16,0		; 32..63 bits
	insrwi	$carry,$t7,16,0
	srwi	$c1,$t7,16
	 stw	$t0,12($tp)		; tp[j-1]
	 stw	$t4,8($tp)

	lwz	$t3,`$FRAME+32^$LITTLE_ENDIAN`($sp)	; permuted $t1
	lwz	$t2,`$FRAME+36^$LITTLE_ENDIAN`($sp)	; permuted $t0
	lwz	$t7,`$FRAME+40^$LITTLE_ENDIAN`($sp)	; permuted $t3
	lwz	$t6,`$FRAME+44^$LITTLE_ENDIAN`($sp)	; permuted $t2
	lwz	$t1,`$FRAME+48^$LITTLE_ENDIAN`($sp)	; permuted $t5
	lwz	$t0,`$FRAME+52^$LITTLE_ENDIAN`($sp)	; permuted $t4
	lwz	$t5,`$FRAME+56^$LITTLE_ENDIAN`($sp)	; permuted $t7
	lwz	$t4,`$FRAME+60^$LITTLE_ENDIAN`($sp)	; permuted $t6

	addc	$t2,$t2,$carry
	adde	$t3,$t3,$c1
	srwi	$carry,$t2,16
	insrwi	$carry,$t3,16,0
	srwi	$c1,$t3,16
	addc	$t6,$t6,$carry
	adde	$t7,$t7,$c1
	srwi	$carry,$t6,16
	 insrwi	$t2,$t6,16,0		; 64..95 bits
	insrwi	$carry,$t7,16,0
	srwi	$c1,$t7,16
	addc	$t0,$t0,$carry
	adde	$t1,$t1,$c1
	srwi	$carry,$t0,16
	insrwi	$carry,$t1,16,0
	srwi	$c1,$t1,16
	addc	$t4,$t4,$carry
	adde	$t5,$t5,$c1
	srwi	$carry,$t4,16
	 insrwi	$t0,$t4,16,0		; 96..127 bits
	insrwi	$carry,$t5,16,0
	srwi	$c1,$t5,16
	 stw	$t2,20($tp)		; tp[j]
	 stwu	$t0,16($tp)

	lwz	$t7,`$FRAME+64^$LITTLE_ENDIAN`($sp)
	lwz	$t6,`$FRAME+68^$LITTLE_ENDIAN`($sp)
	lwz	$t5,`$FRAME+72^$LITTLE_ENDIAN`($sp)
	lwz	$t4,`$FRAME+76^$LITTLE_ENDIAN`($sp)

	addc	$t6,$t6,$carry
	adde	$t7,$t7,$c1
	srwi	$carry,$t6,16
	insrwi	$carry,$t7,16,0
	srwi	$c1,$t7,16
	addc	$t4,$t4,$carry
	adde	$t5,$t5,$c1

	insrwi	$t6,$t4,16,0
	srwi	$t4,$t4,16
	insrwi	$t4,$t5,16,0
	srwi	$ovf,$t5,16
	stw	$t6,12($tp)		; tp[num-1]
	stw	$t4,8($tp)
___
}
$code.=<<___;
	slwi	$t7,$num,2
	subf	$nap_d,$t7,$nap_d	; rewind pointer

	li	$i,8			; i=1
.align	5
Louter:
	addi	$tp,$sp,`$FRAME+$TRANSFER`
	li	$carry,0
	mtctr	$j
___
$code.=<<___ if ($SIZE_T==8);
	ldx	$t3,$bp,$i		; bp[i]

	ld	$t6,`$FRAME+$TRANSFER+8`($sp)	; tp[0]
	mulld	$t7,$a0,$t3		; ap[0]*bp[i]
	add	$t7,$t7,$t6		; ap[0]*bp[i]+tp[0]
	; transfer bp[i] to FPU as 4x16-bit values
	extrdi	$t0,$t3,16,48
	extrdi	$t1,$t3,16,32
	extrdi	$t2,$t3,16,16
	extrdi	$t3,$t3,16,0
	std	$t0,`$FRAME+0`($sp)
	std	$t1,`$FRAME+8`($sp)
	std	$t2,`$FRAME+16`($sp)
	std	$t3,`$FRAME+24`($sp)

	mulld	$t7,$t7,$n0		; tp[0]*n0
	; transfer (ap[0]*bp[i]+tp[0])*n0 to FPU as 4x16-bit values
	extrdi	$t4,$t7,16,48
	extrdi	$t5,$t7,16,32
	extrdi	$t6,$t7,16,16
	extrdi	$t7,$t7,16,0
	std	$t4,`$FRAME+32`($sp)
	std	$t5,`$FRAME+40`($sp)
	std	$t6,`$FRAME+48`($sp)
	std	$t7,`$FRAME+56`($sp)
___
$code.=<<___ if ($SIZE_T==4);
	add	$t0,$bp,$i
	li	$c1,0
	lwz	$t1,0($t0)		; bp[i,i+1]
	lwz	$t3,4($t0)

	mullw	$t4,$a0,$t1		; ap[0]*bp[i]
	lwz	$t0,`$FRAME+$TRANSFER+8+4`($sp)	; tp[0]
	mulhwu	$t5,$a0,$t1
	lwz	$t2,`$FRAME+$TRANSFER+8`($sp)	; tp[0]
	mullw	$t6,$a1,$t1
	mullw	$t7,$a0,$t3
	add	$t5,$t5,$t6
	add	$t5,$t5,$t7
	addc	$t4,$t4,$t0		; ap[0]*bp[i]+tp[0]
	adde	$t5,$t5,$t2
	; transfer bp[i] to FPU as 4x16-bit values
	extrwi	$t0,$t1,16,16
	extrwi	$t1,$t1,16,0
	extrwi	$t2,$t3,16,16
	extrwi	$t3,$t3,16,0
	std	$t0,`$FRAME+0`($sp)	; yes, std in 32-bit build
	std	$t1,`$FRAME+8`($sp)
	std	$t2,`$FRAME+16`($sp)
	std	$t3,`$FRAME+24`($sp)

	mullw	$t0,$t4,$n0		; mulld tp[0]*n0
	mulhwu	$t1,$t4,$n0
	mullw	$t2,$t5,$n0
	mullw	$t3,$t4,$n1
	add	$t1,$t1,$t2
	add	$t1,$t1,$t3
	; transfer (ap[0]*bp[i]+tp[0])*n0 to FPU as 4x16-bit values
	extrwi	$t4,$t0,16,16
	extrwi	$t5,$t0,16,0
	extrwi	$t6,$t1,16,16
	extrwi	$t7,$t1,16,0
	std	$t4,`$FRAME+32`($sp)	; yes, std in 32-bit build
	std	$t5,`$FRAME+40`($sp)
	std	$t6,`$FRAME+48`($sp)
	std	$t7,`$FRAME+56`($sp)
___
$code.=<<___;
	lfd	$A0,8($nap_d)		; load a[j] in double format
	lfd	$A1,16($nap_d)
	lfd	$A2,24($nap_d)		; load a[j+1] in double format
	lfd	$A3,32($nap_d)
	lfd	$N0,40($nap_d)		; load n[j] in double format
	lfd	$N1,48($nap_d)
	lfd	$N2,56($nap_d)		; load n[j+1] in double format
	lfdu	$N3,64($nap_d)

	lfd	$ba,`$FRAME+0`($sp)
	lfd	$bb,`$FRAME+8`($sp)
	lfd	$bc,`$FRAME+16`($sp)
	lfd	$bd,`$FRAME+24`($sp)
	lfd	$na,`$FRAME+32`($sp)
	lfd	$nb,`$FRAME+40`($sp)
	lfd	$nc,`$FRAME+48`($sp)
	lfd	$nd,`$FRAME+56`($sp)

	fcfid	$ba,$ba
	fcfid	$bb,$bb
	fcfid	$bc,$bc
	fcfid	$bd,$bd
	fcfid	$na,$na
	fcfid	$nb,$nb
	fcfid	$nc,$nc
	fcfid	$nd,$nd

	fmul	$T1a,$A1,$ba
	fmul	$T1b,$A1,$bb
	fmul	$T2a,$A2,$ba
	fmul	$T2b,$A2,$bb
	fmul	$T3a,$A3,$ba
	fmul	$T3b,$A3,$bb
	fmul	$T0a,$A0,$ba
	fmul	$T0b,$A0,$bb

	fmadd	$T1a,$A0,$bc,$T1a
	fmadd	$T1b,$A0,$bd,$T1b
	fmadd	$T2a,$A1,$bc,$T2a
	fmadd	$T2b,$A1,$bd,$T2b
	fmadd	$T3a,$A2,$bc,$T3a
	fmadd	$T3b,$A2,$bd,$T3b
	fmul	$dota,$A3,$bc
	fmul	$dotb,$A3,$bd

	fmadd	$T1a,$N1,$na,$T1a
	fmadd	$T1b,$N1,$nb,$T1b
	 lfd	$A0,8($nap_d)		; load a[j] in double format
	 lfd	$A1,16($nap_d)
	fmadd	$T2a,$N2,$na,$T2a
	fmadd	$T2b,$N2,$nb,$T2b
	 lfd	$A2,24($nap_d)		; load a[j+1] in double format
	 lfd	$A3,32($nap_d)
	fmadd	$T3a,$N3,$na,$T3a
	fmadd	$T3b,$N3,$nb,$T3b
	fmadd	$T0a,$N0,$na,$T0a
	fmadd	$T0b,$N0,$nb,$T0b

	fmadd	$T1a,$N0,$nc,$T1a
	fmadd	$T1b,$N0,$nd,$T1b
	fmadd	$T2a,$N1,$nc,$T2a
	fmadd	$T2b,$N1,$nd,$T2b
	fmadd	$T3a,$N2,$nc,$T3a
	fmadd	$T3b,$N2,$nd,$T3b
	fmadd	$dota,$N3,$nc,$dota
	fmadd	$dotb,$N3,$nd,$dotb

	fctid	$T0a,$T0a
	fctid	$T0b,$T0b
	fctid	$T1a,$T1a
	fctid	$T1b,$T1b
	fctid	$T2a,$T2a
	fctid	$T2b,$T2b
	fctid	$T3a,$T3a
	fctid	$T3b,$T3b

	stfd	$T0a,`$FRAME+0`($sp)
	stfd	$T0b,`$FRAME+8`($sp)
	stfd	$T1a,`$FRAME+16`($sp)
	stfd	$T1b,`$FRAME+24`($sp)
	stfd	$T2a,`$FRAME+32`($sp)
	stfd	$T2b,`$FRAME+40`($sp)
	stfd	$T3a,`$FRAME+48`($sp)
	stfd	$T3b,`$FRAME+56`($sp)

.align	5
Linner:
	fmul	$T1a,$A1,$ba
	fmul	$T1b,$A1,$bb
	fmul	$T2a,$A2,$ba
	fmul	$T2b,$A2,$bb
	lfd	$N0,40($nap_d)		; load n[j] in double format
	lfd	$N1,48($nap_d)
	fmul	$T3a,$A3,$ba
	fmul	$T3b,$A3,$bb
	fmadd	$T0a,$A0,$ba,$dota
	fmadd	$T0b,$A0,$bb,$dotb
	lfd	$N2,56($nap_d)		; load n[j+1] in double format
	lfdu	$N3,64($nap_d)

	fmadd	$T1a,$A0,$bc,$T1a
	fmadd	$T1b,$A0,$bd,$T1b
	fmadd	$T2a,$A1,$bc,$T2a
	fmadd	$T2b,$A1,$bd,$T2b
	 lfd	$A0,8($nap_d)		; load a[j] in double format
	 lfd	$A1,16($nap_d)
	fmadd	$T3a,$A2,$bc,$T3a
	fmadd	$T3b,$A2,$bd,$T3b
	fmul	$dota,$A3,$bc
	fmul	$dotb,$A3,$bd
	 lfd	$A2,24($nap_d)		; load a[j+1] in double format
	 lfd	$A3,32($nap_d)
___
if ($SIZE_T==8 or $flavour =~ /osx/) {
$code.=<<___;
	fmadd	$T1a,$N1,$na,$T1a
	fmadd	$T1b,$N1,$nb,$T1b
	 ld	$t0,`$FRAME+0`($sp)
	 ld	$t1,`$FRAME+8`($sp)
	fmadd	$T2a,$N2,$na,$T2a
	fmadd	$T2b,$N2,$nb,$T2b
	 ld	$t2,`$FRAME+16`($sp)
	 ld	$t3,`$FRAME+24`($sp)
	fmadd	$T3a,$N3,$na,$T3a
	fmadd	$T3b,$N3,$nb,$T3b
	 add	$t0,$t0,$carry		; can not overflow
	 ld	$t4,`$FRAME+32`($sp)
	 ld	$t5,`$FRAME+40`($sp)
	fmadd	$T0a,$N0,$na,$T0a
	fmadd	$T0b,$N0,$nb,$T0b
	 srdi	$carry,$t0,16
	 add	$t1,$t1,$carry
	 srdi	$carry,$t1,16
	 ld	$t6,`$FRAME+48`($sp)
	 ld	$t7,`$FRAME+56`($sp)

	fmadd	$T1a,$N0,$nc,$T1a
	fmadd	$T1b,$N0,$nd,$T1b
	 insrdi	$t0,$t1,16,32
	 ld	$t1,8($tp)		; tp[j]
	fmadd	$T2a,$N1,$nc,$T2a
	fmadd	$T2b,$N1,$nd,$T2b
	 add	$t2,$t2,$carry
	fmadd	$T3a,$N2,$nc,$T3a
	fmadd	$T3b,$N2,$nd,$T3b
	 srdi	$carry,$t2,16
	 insrdi	$t0,$t2,16,16
	fmadd	$dota,$N3,$nc,$dota
	fmadd	$dotb,$N3,$nd,$dotb
	 add	$t3,$t3,$carry
	 ldu	$t2,16($tp)		; tp[j+1]
	 srdi	$carry,$t3,16
	 insrdi	$t0,$t3,16,0		; 0..63 bits
	 add	$t4,$t4,$carry

	fctid	$T0a,$T0a
	fctid	$T0b,$T0b
	 srdi	$carry,$t4,16
	fctid	$T1a,$T1a
	fctid	$T1b,$T1b
	 add	$t5,$t5,$carry
	fctid	$T2a,$T2a
	fctid	$T2b,$T2b
	 srdi	$carry,$t5,16
	 insrdi	$t4,$t5,16,32
	fctid	$T3a,$T3a
	fctid	$T3b,$T3b
	 add	$t6,$t6,$carry
	 srdi	$carry,$t6,16
	 insrdi	$t4,$t6,16,16

	stfd	$T0a,`$FRAME+0`($sp)
	stfd	$T0b,`$FRAME+8`($sp)
	 add	$t7,$t7,$carry
	 addc	$t3,$t0,$t1
___
$code.=<<___ if ($SIZE_T==4);		# adjust XER[CA]
	extrdi	$t0,$t0,32,0
	extrdi	$t1,$t1,32,0
	adde	$t0,$t0,$t1
___
$code.=<<___;
	stfd	$T1a,`$FRAME+16`($sp)
	stfd	$T1b,`$FRAME+24`($sp)
	 insrdi	$t4,$t7,16,0		; 64..127 bits
	 srdi	$carry,$t7,16		; upper 33 bits
	stfd	$T2a,`$FRAME+32`($sp)
	stfd	$T2b,`$FRAME+40`($sp)
	 adde	$t5,$t4,$t2
___
$code.=<<___ if ($SIZE_T==4);		# adjust XER[CA]
	extrdi	$t4,$t4,32,0
	extrdi	$t2,$t2,32,0
	adde	$t4,$t4,$t2
___
$code.=<<___;
	stfd	$T3a,`$FRAME+48`($sp)
	stfd	$T3b,`$FRAME+56`($sp)
	 addze	$carry,$carry
	 std	$t3,-16($tp)		; tp[j-1]
	 std	$t5,-8($tp)		; tp[j]
___
} else {
$code.=<<___;
	fmadd	$T1a,$N1,$na,$T1a
	fmadd	$T1b,$N1,$nb,$T1b
	 lwz	$t1,`$FRAME+0^$LITTLE_ENDIAN`($sp)
	 lwz	$t0,`$FRAME+4^$LITTLE_ENDIAN`($sp)
	fmadd	$T2a,$N2,$na,$T2a
	fmadd	$T2b,$N2,$nb,$T2b
	 lwz	$t3,`$FRAME+8^$LITTLE_ENDIAN`($sp)
	 lwz	$t2,`$FRAME+12^$LITTLE_ENDIAN`($sp)
	fmadd	$T3a,$N3,$na,$T3a
	fmadd	$T3b,$N3,$nb,$T3b
	 lwz	$t5,`$FRAME+16^$LITTLE_ENDIAN`($sp)
	 lwz	$t4,`$FRAME+20^$LITTLE_ENDIAN`($sp)
	 addc	$t0,$t0,$carry
	 adde	$t1,$t1,$c1
	 srwi	$carry,$t0,16
	fmadd	$T0a,$N0,$na,$T0a
	fmadd	$T0b,$N0,$nb,$T0b
	 lwz	$t7,`$FRAME+24^$LITTLE_ENDIAN`($sp)
	 lwz	$t6,`$FRAME+28^$LITTLE_ENDIAN`($sp)
	 srwi	$c1,$t1,16
	 insrwi	$carry,$t1,16,0

	fmadd	$T1a,$N0,$nc,$T1a
	fmadd	$T1b,$N0,$nd,$T1b
	 addc	$t2,$t2,$carry
	 adde	$t3,$t3,$c1
	 srwi	$carry,$t2,16
	fmadd	$T2a,$N1,$nc,$T2a
	fmadd	$T2b,$N1,$nd,$T2b
	 insrwi	$t0,$t2,16,0		; 0..31 bits
	 srwi	$c1,$t3,16
	 insrwi	$carry,$t3,16,0
	fmadd	$T3a,$N2,$nc,$T3a
	fmadd	$T3b,$N2,$nd,$T3b
	 lwz	$t2,12($tp)		; tp[j]
	 lwz	$t3,8($tp)
	 addc	$t4,$t4,$carry
	 adde	$t5,$t5,$c1
	 srwi	$carry,$t4,16
	fmadd	$dota,$N3,$nc,$dota
	fmadd	$dotb,$N3,$nd,$dotb
	 srwi	$c1,$t5,16
	 insrwi	$carry,$t5,16,0

	fctid	$T0a,$T0a
	 addc	$t6,$t6,$carry
	 adde	$t7,$t7,$c1
	 srwi	$carry,$t6,16
	fctid	$T0b,$T0b
	 insrwi	$t4,$t6,16,0		; 32..63 bits
	 srwi	$c1,$t7,16
	 insrwi	$carry,$t7,16,0
	fctid	$T1a,$T1a
	 addc	$t0,$t0,$t2
	 adde	$t4,$t4,$t3
	 lwz	$t3,`$FRAME+32^$LITTLE_ENDIAN`($sp)	; permuted $t1
	 lwz	$t2,`$FRAME+36^$LITTLE_ENDIAN`($sp)	; permuted $t0
	fctid	$T1b,$T1b
	 addze	$carry,$carry
	 addze	$c1,$c1
	 stw	$t0,4($tp)		; tp[j-1]
	 stw	$t4,0($tp)
	fctid	$T2a,$T2a
	 addc	$t2,$t2,$carry
	 adde	$t3,$t3,$c1
	 srwi	$carry,$t2,16
	 lwz	$t7,`$FRAME+40^$LITTLE_ENDIAN`($sp)	; permuted $t3
	 lwz	$t6,`$FRAME+44^$LITTLE_ENDIAN`($sp)	; permuted $t2
	fctid	$T2b,$T2b
	 srwi	$c1,$t3,16
	 insrwi	$carry,$t3,16,0
	 lwz	$t1,`$FRAME+48^$LITTLE_ENDIAN`($sp)	; permuted $t5
	 lwz	$t0,`$FRAME+52^$LITTLE_ENDIAN`($sp)	; permuted $t4
	fctid	$T3a,$T3a
	 addc	$t6,$t6,$carry
	 adde	$t7,$t7,$c1
	 srwi	$carry,$t6,16
	 lwz	$t5,`$FRAME+56^$LITTLE_ENDIAN`($sp)	; permuted $t7
	 lwz	$t4,`$FRAME+60^$LITTLE_ENDIAN`($sp)	; permuted $t6
	fctid	$T3b,$T3b

	 insrwi	$t2,$t6,16,0		; 64..95 bits
	insrwi	$carry,$t7,16,0
	srwi	$c1,$t7,16
	 lwz	$t6,20($tp)
	 lwzu	$t7,16($tp)
	addc	$t0,$t0,$carry
	 stfd	$T0a,`$FRAME+0`($sp)
	adde	$t1,$t1,$c1
	srwi	$carry,$t0,16
	 stfd	$T0b,`$FRAME+8`($sp)
	insrwi	$carry,$t1,16,0
	srwi	$c1,$t1,16
	addc	$t4,$t4,$carry
	 stfd	$T1a,`$FRAME+16`($sp)
	adde	$t5,$t5,$c1
	srwi	$carry,$t4,16
	 insrwi	$t0,$t4,16,0		; 96..127 bits
	 stfd	$T1b,`$FRAME+24`($sp)
	insrwi	$carry,$t5,16,0
	srwi	$c1,$t5,16

	addc	$t2,$t2,$t6
	 stfd	$T2a,`$FRAME+32`($sp)
	adde	$t0,$t0,$t7
	 stfd	$T2b,`$FRAME+40`($sp)
	addze	$carry,$carry
	 stfd	$T3a,`$FRAME+48`($sp)
	addze	$c1,$c1
	 stfd	$T3b,`$FRAME+56`($sp)
	 stw	$t2,-4($tp)		; tp[j]
	 stw	$t0,-8($tp)
___
}
$code.=<<___;
	bdnz	Linner

	fctid	$dota,$dota
	fctid	$dotb,$dotb
___
if ($SIZE_T==8 or $flavour =~ /osx/) {
$code.=<<___;
	ld	$t0,`$FRAME+0`($sp)
	ld	$t1,`$FRAME+8`($sp)
	ld	$t2,`$FRAME+16`($sp)
	ld	$t3,`$FRAME+24`($sp)
	ld	$t4,`$FRAME+32`($sp)
	ld	$t5,`$FRAME+40`($sp)
	ld	$t6,`$FRAME+48`($sp)
	ld	$t7,`$FRAME+56`($sp)
	stfd	$dota,`$FRAME+64`($sp)
	stfd	$dotb,`$FRAME+72`($sp)

	add	$t0,$t0,$carry		; can not overflow
	srdi	$carry,$t0,16
	add	$t1,$t1,$carry
	srdi	$carry,$t1,16
	insrdi	$t0,$t1,16,32
	add	$t2,$t2,$carry
	ld	$t1,8($tp)		; tp[j]
	srdi	$carry,$t2,16
	insrdi	$t0,$t2,16,16
	add	$t3,$t3,$carry
	ldu	$t2,16($tp)		; tp[j+1]
	srdi	$carry,$t3,16
	insrdi	$t0,$t3,16,0		; 0..63 bits
	add	$t4,$t4,$carry
	srdi	$carry,$t4,16
	add	$t5,$t5,$carry
	srdi	$carry,$t5,16
	insrdi	$t4,$t5,16,32
	add	$t6,$t6,$carry
	srdi	$carry,$t6,16
	insrdi	$t4,$t6,16,16
	add	$t7,$t7,$carry
	insrdi	$t4,$t7,16,0		; 64..127 bits
	srdi	$carry,$t7,16		; upper 33 bits
	ld	$t6,`$FRAME+64`($sp)
	ld	$t7,`$FRAME+72`($sp)

	addc	$t3,$t0,$t1
___
$code.=<<___ if ($SIZE_T==4);		# adjust XER[CA]
	extrdi	$t0,$t0,32,0
	extrdi	$t1,$t1,32,0
	adde	$t0,$t0,$t1
___
$code.=<<___;
	adde	$t5,$t4,$t2
___
$code.=<<___ if ($SIZE_T==4);		# adjust XER[CA]
	extrdi	$t4,$t4,32,0
	extrdi	$t2,$t2,32,0
	adde	$t4,$t4,$t2
___
$code.=<<___;
	addze	$carry,$carry

	std	$t3,-16($tp)		; tp[j-1]
	std	$t5,-8($tp)		; tp[j]

	add	$carry,$carry,$ovf	; consume upmost overflow
	add	$t6,$t6,$carry		; can not overflow
	srdi	$carry,$t6,16
	add	$t7,$t7,$carry
	insrdi	$t6,$t7,48,0
	srdi	$ovf,$t7,48
	std	$t6,0($tp)		; tp[num-1]
___
} else {
$code.=<<___;
	lwz	$t1,`$FRAME+0^$LITTLE_ENDIAN`($sp)
	lwz	$t0,`$FRAME+4^$LITTLE_ENDIAN`($sp)
	lwz	$t3,`$FRAME+8^$LITTLE_ENDIAN`($sp)
	lwz	$t2,`$FRAME+12^$LITTLE_ENDIAN`($sp)
	lwz	$t5,`$FRAME+16^$LITTLE_ENDIAN`($sp)
	lwz	$t4,`$FRAME+20^$LITTLE_ENDIAN`($sp)
	lwz	$t7,`$FRAME+24^$LITTLE_ENDIAN`($sp)
	lwz	$t6,`$FRAME+28^$LITTLE_ENDIAN`($sp)
	stfd	$dota,`$FRAME+64`($sp)
	stfd	$dotb,`$FRAME+72`($sp)

	addc	$t0,$t0,$carry
	adde	$t1,$t1,$c1
	srwi	$carry,$t0,16
	insrwi	$carry,$t1,16,0
	srwi	$c1,$t1,16
	addc	$t2,$t2,$carry
	adde	$t3,$t3,$c1
	srwi	$carry,$t2,16
	 insrwi	$t0,$t2,16,0		; 0..31 bits
	 lwz	$t2,12($tp)		; tp[j]
	insrwi	$carry,$t3,16,0
	srwi	$c1,$t3,16
	 lwz	$t3,8($tp)
	addc	$t4,$t4,$carry
	adde	$t5,$t5,$c1
	srwi	$carry,$t4,16
	insrwi	$carry,$t5,16,0
	srwi	$c1,$t5,16
	addc	$t6,$t6,$carry
	adde	$t7,$t7,$c1
	srwi	$carry,$t6,16
	 insrwi	$t4,$t6,16,0		; 32..63 bits
	insrwi	$carry,$t7,16,0
	srwi	$c1,$t7,16

	addc	$t0,$t0,$t2
	adde	$t4,$t4,$t3
	addze	$carry,$carry
	addze	$c1,$c1
	 stw	$t0,4($tp)		; tp[j-1]
	 stw	$t4,0($tp)

	lwz	$t3,`$FRAME+32^$LITTLE_ENDIAN`($sp)	; permuted $t1
	lwz	$t2,`$FRAME+36^$LITTLE_ENDIAN`($sp)	; permuted $t0
	lwz	$t7,`$FRAME+40^$LITTLE_ENDIAN`($sp)	; permuted $t3
	lwz	$t6,`$FRAME+44^$LITTLE_ENDIAN`($sp)	; permuted $t2
	lwz	$t1,`$FRAME+48^$LITTLE_ENDIAN`($sp)	; permuted $t5
	lwz	$t0,`$FRAME+52^$LITTLE_ENDIAN`($sp)	; permuted $t4
	lwz	$t5,`$FRAME+56^$LITTLE_ENDIAN`($sp)	; permuted $t7
	lwz	$t4,`$FRAME+60^$LITTLE_ENDIAN`($sp)	; permuted $t6

	addc	$t2,$t2,$carry
	adde	$t3,$t3,$c1
	srwi	$carry,$t2,16
	insrwi	$carry,$t3,16,0
	srwi	$c1,$t3,16
	addc	$t6,$t6,$carry
	adde	$t7,$t7,$c1
	srwi	$carry,$t6,16
	 insrwi	$t2,$t6,16,0		; 64..95 bits
	 lwz	$t6,20($tp)
	insrwi	$carry,$t7,16,0
	srwi	$c1,$t7,16
	 lwzu	$t7,16($tp)
	addc	$t0,$t0,$carry
	adde	$t1,$t1,$c1
	srwi	$carry,$t0,16
	insrwi	$carry,$t1,16,0
	srwi	$c1,$t1,16
	addc	$t4,$t4,$carry
	adde	$t5,$t5,$c1
	srwi	$carry,$t4,16
	 insrwi	$t0,$t4,16,0		; 96..127 bits
	insrwi	$carry,$t5,16,0
	srwi	$c1,$t5,16

	addc	$t2,$t2,$t6
	adde	$t0,$t0,$t7
	 lwz	$t7,`$FRAME+64^$LITTLE_ENDIAN`($sp)
	 lwz	$t6,`$FRAME+68^$LITTLE_ENDIAN`($sp)
	addze	$carry,$carry
	addze	$c1,$c1
	 lwz	$t5,`$FRAME+72^$LITTLE_ENDIAN`($sp)
	 lwz	$t4,`$FRAME+76^$LITTLE_ENDIAN`($sp)

	addc	$t6,$t6,$carry
	adde	$t7,$t7,$c1
	 stw	$t2,-4($tp)		; tp[j]
	 stw	$t0,-8($tp)
	addc	$t6,$t6,$ovf
	addze	$t7,$t7
	srwi	$carry,$t6,16
	insrwi	$carry,$t7,16,0
	srwi	$c1,$t7,16
	addc	$t4,$t4,$carry
	adde	$t5,$t5,$c1

	insrwi	$t6,$t4,16,0
	srwi	$t4,$t4,16
	insrwi	$t4,$t5,16,0
	srwi	$ovf,$t5,16
	stw	$t6,4($tp)		; tp[num-1]
	stw	$t4,0($tp)
___
}
$code.=<<___;
	slwi	$t7,$num,2
	addi	$i,$i,8
	subf	$nap_d,$t7,$nap_d	; rewind pointer
	cmpw	$i,$num
	blt-	Louter
___

$code.=<<___ if ($SIZE_T==8);
	subf	$np,$num,$np	; rewind np
	addi	$j,$j,1		; restore counter
	subfc	$i,$i,$i	; j=0 and "clear" XER[CA]
	addi	$tp,$sp,`$FRAME+$TRANSFER+8`
	addi	$t4,$sp,`$FRAME+$TRANSFER+16`
	addi	$t5,$np,8
	addi	$t6,$rp,8
	mtctr	$j

.align	4
Lsub:	ldx	$t0,$tp,$i
	ldx	$t1,$np,$i
	ldx	$t2,$t4,$i
	ldx	$t3,$t5,$i
	subfe	$t0,$t1,$t0	; tp[j]-np[j]
	subfe	$t2,$t3,$t2	; tp[j+1]-np[j+1]
	stdx	$t0,$rp,$i
	stdx	$t2,$t6,$i
	addi	$i,$i,16
	bdnz	Lsub

	li	$i,0
	subfe	$ovf,$i,$ovf	; handle upmost overflow bit
	mtctr	$j

.align	4
Lcopy:				; conditional copy
	ldx	$t0,$tp,$i
	ldx	$t1,$t4,$i
	ldx	$t2,$rp,$i
	ldx	$t3,$t6,$i
	std	$i,8($nap_d)	; zap nap_d
	std	$i,16($nap_d)
	std	$i,24($nap_d)
	std	$i,32($nap_d)
	std	$i,40($nap_d)
	std	$i,48($nap_d)
	std	$i,56($nap_d)
	stdu	$i,64($nap_d)
	and	$t0,$t0,$ovf
	and	$t1,$t1,$ovf
	andc	$t2,$t2,$ovf
	andc	$t3,$t3,$ovf
	or	$t0,$t0,$t2
	or	$t1,$t1,$t3
	stdx	$t0,$rp,$i
	stdx	$t1,$t6,$i
	stdx	$i,$tp,$i	; zap tp at once
	stdx	$i,$t4,$i
	addi	$i,$i,16
	bdnz	Lcopy
___
$code.=<<___ if ($SIZE_T==4);
	subf	$np,$num,$np	; rewind np
	addi	$j,$j,1		; restore counter
	subfc	$i,$i,$i	; j=0 and "clear" XER[CA]
	addi	$tp,$sp,`$FRAME+$TRANSFER`
	addi	$np,$np,-4
	addi	$rp,$rp,-4
	addi	$ap,$sp,`$FRAME+$TRANSFER+4`
	mtctr	$j

.align	4
Lsub:	lwz	$t0,12($tp)	; load tp[j..j+3] in 64-bit word order
	lwz	$t1,8($tp)
	lwz	$t2,20($tp)
	lwzu	$t3,16($tp)
	lwz	$t4,4($np)	; load np[j..j+3] in 32-bit word order
	lwz	$t5,8($np)
	lwz	$t6,12($np)
	lwzu	$t7,16($np)
	subfe	$t4,$t4,$t0	; tp[j]-np[j]
	 stw	$t0,4($ap)	; save tp[j..j+3] in 32-bit word order
	subfe	$t5,$t5,$t1	; tp[j+1]-np[j+1]
	 stw	$t1,8($ap)
	subfe	$t6,$t6,$t2	; tp[j+2]-np[j+2]
	 stw	$t2,12($ap)
	subfe	$t7,$t7,$t3	; tp[j+3]-np[j+3]
	 stwu	$t3,16($ap)
	stw	$t4,4($rp)
	stw	$t5,8($rp)
	stw	$t6,12($rp)
	stwu	$t7,16($rp)
	bdnz	Lsub

	li	$i,0
	subfe	$ovf,$i,$ovf	; handle upmost overflow bit
	addi	$ap,$sp,`$FRAME+$TRANSFER+4`
	subf	$rp,$num,$rp	; rewind rp
	addi	$tp,$sp,`$FRAME+$TRANSFER`
	mtctr	$j

.align	4
Lcopy:				; conditional copy
	lwz	$t0,4($ap)
	lwz	$t1,8($ap)
	lwz	$t2,12($ap)
	lwzu	$t3,16($ap)
	lwz	$t4,4($rp)
	lwz	$t5,8($rp)
	lwz	$t6,12($rp)
	lwz	$t7,16($rp)
	std	$i,8($nap_d)	; zap nap_d
	std	$i,16($nap_d)
	std	$i,24($nap_d)
	std	$i,32($nap_d)
	std	$i,40($nap_d)
	std	$i,48($nap_d)
	std	$i,56($nap_d)
	stdu	$i,64($nap_d)
	and	$t0,$t0,$ovf
	and	$t1,$t1,$ovf
	and	$t2,$t2,$ovf
	and	$t3,$t3,$ovf
	andc	$t4,$t4,$ovf
	andc	$t5,$t5,$ovf
	andc	$t6,$t6,$ovf
	andc	$t7,$t7,$ovf
	or	$t0,$t0,$t4
	or	$t1,$t1,$t5
	or	$t2,$t2,$t6
	or	$t3,$t3,$t7
	stw	$t0,4($rp)
	stw	$t1,8($rp)
	stw	$t2,12($rp)
	stwu	$t3,16($rp)
	std	$i,8($tp)	; zap tp at once
	stdu	$i,16($tp)
	bdnz	Lcopy
___

$code.=<<___;
	$POP	$i,0($sp)
	li	r3,1	; signal "handled"
	$POP	r19,`-12*8-13*$SIZE_T`($i)
	$POP	r20,`-12*8-12*$SIZE_T`($i)
	$POP	r21,`-12*8-11*$SIZE_T`($i)
	$POP	r22,`-12*8-10*$SIZE_T`($i)
	$POP	r23,`-12*8-9*$SIZE_T`($i)
	$POP	r24,`-12*8-8*$SIZE_T`($i)
	$POP	r25,`-12*8-7*$SIZE_T`($i)
	$POP	r26,`-12*8-6*$SIZE_T`($i)
	$POP	r27,`-12*8-5*$SIZE_T`($i)
	$POP	r28,`-12*8-4*$SIZE_T`($i)
	$POP	r29,`-12*8-3*$SIZE_T`($i)
	$POP	r30,`-12*8-2*$SIZE_T`($i)
	$POP	r31,`-12*8-1*$SIZE_T`($i)
	lfd	f20,`-12*8`($i)
	lfd	f21,`-11*8`($i)
	lfd	f22,`-10*8`($i)
	lfd	f23,`-9*8`($i)
	lfd	f24,`-8*8`($i)
	lfd	f25,`-7*8`($i)
	lfd	f26,`-6*8`($i)
	lfd	f27,`-5*8`($i)
	lfd	f28,`-4*8`($i)
	lfd	f29,`-3*8`($i)
	lfd	f30,`-2*8`($i)
	lfd	f31,`-1*8`($i)
	mr	$sp,$i
	blr
	.long	0
	.byte	0,12,4,0,0x8c,13,6,0
	.long	0
.size	.$fname,.-.$fname

.asciz  "Montgomery Multiplication for PPC64, CRYPTOGAMS by <appro\@openssl.org>"
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

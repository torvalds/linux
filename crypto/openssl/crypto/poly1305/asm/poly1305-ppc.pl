#! /usr/bin/env perl
# Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
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
#
# This module implements Poly1305 hash for PowerPC.
#
# June 2015
#
# Numbers are cycles per processed byte with poly1305_blocks alone,
# and improvement coefficients relative to gcc-generated code.
#
#			-m32		-m64
#
# Freescale e300	14.8/+80%	-
# PPC74x0		7.60/+60%	-
# PPC970		7.00/+114%	3.51/+205%
# POWER7		3.75/+260%	1.93/+100%
# POWER8		-		2.03/+200%
# POWER9		-		2.00/+150%
#
# Do we need floating-point implementation for PPC? Results presented
# in poly1305_ieee754.c are tricky to compare to, because they are for
# compiler-generated code. On the other hand it's known that floating-
# point performance can be dominated by FPU latency, which means that
# there is limit even for ideally optimized (and even vectorized) code.
# And this limit is estimated to be higher than above -m64 results. Or
# in other words floating-point implementation can be meaningful to
# consider only in 32-bit application context. We probably have to
# recognize that 32-bit builds are getting less popular on high-end
# systems and therefore tend to target embedded ones, which might not
# even have FPU...
#
# On side note, Power ISA 2.07 enables vector base 2^26 implementation,
# and POWER8 might have capacity to break 1.0 cycle per byte barrier...

$flavour = shift;

if ($flavour =~ /64/) {
	$SIZE_T	=8;
	$LRSAVE	=2*$SIZE_T;
	$UCMP	="cmpld";
	$STU	="stdu";
	$POP	="ld";
	$PUSH	="std";
} elsif ($flavour =~ /32/) {
	$SIZE_T	=4;
	$LRSAVE	=$SIZE_T;
	$UCMP	="cmplw";
	$STU	="stwu";
	$POP	="lwz";
	$PUSH	="stw";
} else { die "nonsense $flavour"; }

# Define endianness based on flavour
# i.e.: linux64le
$LITTLE_ENDIAN = ($flavour=~/le$/) ? $SIZE_T : 0;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour ".shift || die "can't call $xlate: $!";

$FRAME=24*$SIZE_T;

$sp="r1";
my ($ctx,$inp,$len,$padbit) = map("r$_",(3..6));
my ($mac,$nonce)=($inp,$len);
my $mask = "r0";

$code=<<___;
.machine	"any"
.text
___
							if ($flavour =~ /64/) {
###############################################################################
# base 2^64 implementation

my ($h0,$h1,$h2,$d0,$d1,$d2, $r0,$r1,$s1, $t0,$t1) = map("r$_",(7..12,27..31));

$code.=<<___;
.globl	.poly1305_init_int
.align	4
.poly1305_init_int:
	xor	r0,r0,r0
	std	r0,0($ctx)		# zero hash value
	std	r0,8($ctx)
	std	r0,16($ctx)

	$UCMP	$inp,r0
	beq-	Lno_key
___
$code.=<<___	if ($LITTLE_ENDIAN);
	ld	$d0,0($inp)		# load key material
	ld	$d1,8($inp)
___
$code.=<<___	if (!$LITTLE_ENDIAN);
	li	$h0,4
	lwbrx	$d0,0,$inp		# load key material
	li	$d1,8
	lwbrx	$h0,$h0,$inp
	li	$h1,12
	lwbrx	$d1,$d1,$inp
	lwbrx	$h1,$h1,$inp
	insrdi	$d0,$h0,32,0
	insrdi	$d1,$h1,32,0
___
$code.=<<___;
	lis	$h1,0xfff		# 0x0fff0000
	ori	$h1,$h1,0xfffc		# 0x0ffffffc
	insrdi	$h1,$h1,32,0		# 0x0ffffffc0ffffffc
	ori	$h0,$h1,3		# 0x0ffffffc0fffffff

	and	$d0,$d0,$h0
	and	$d1,$d1,$h1

	std	$d0,32($ctx)		# store key
	std	$d1,40($ctx)

Lno_key:
	xor	r3,r3,r3
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,2,0
.size	.poly1305_init_int,.-.poly1305_init_int

.globl	.poly1305_blocks
.align	4
.poly1305_blocks:
	srdi.	$len,$len,4
	beq-	Labort

	$STU	$sp,-$FRAME($sp)
	mflr	r0
	$PUSH	r27,`$FRAME-$SIZE_T*5`($sp)
	$PUSH	r28,`$FRAME-$SIZE_T*4`($sp)
	$PUSH	r29,`$FRAME-$SIZE_T*3`($sp)
	$PUSH	r30,`$FRAME-$SIZE_T*2`($sp)
	$PUSH	r31,`$FRAME-$SIZE_T*1`($sp)
	$PUSH	r0,`$FRAME+$LRSAVE`($sp)

	ld	$r0,32($ctx)		# load key
	ld	$r1,40($ctx)

	ld	$h0,0($ctx)		# load hash value
	ld	$h1,8($ctx)
	ld	$h2,16($ctx)

	srdi	$s1,$r1,2
	mtctr	$len
	add	$s1,$s1,$r1		# s1 = r1 + r1>>2
	li	$mask,3
	b	Loop

.align	4
Loop:
___
$code.=<<___	if ($LITTLE_ENDIAN);
	ld	$t0,0($inp)		# load input
	ld	$t1,8($inp)
___
$code.=<<___	if (!$LITTLE_ENDIAN);
	li	$d0,4
	lwbrx	$t0,0,$inp		# load input
	li	$t1,8
	lwbrx	$d0,$d0,$inp
	li	$d1,12
	lwbrx	$t1,$t1,$inp
	lwbrx	$d1,$d1,$inp
	insrdi	$t0,$d0,32,0
	insrdi	$t1,$d1,32,0
___
$code.=<<___;
	addi	$inp,$inp,16

	addc	$h0,$h0,$t0		# accumulate input
	adde	$h1,$h1,$t1

	mulld	$d0,$h0,$r0		# h0*r0
	mulhdu	$d1,$h0,$r0
	adde	$h2,$h2,$padbit

	mulld	$t0,$h1,$s1		# h1*5*r1
	mulhdu	$t1,$h1,$s1
	addc	$d0,$d0,$t0
	adde	$d1,$d1,$t1

	mulld	$t0,$h0,$r1		# h0*r1
	mulhdu	$d2,$h0,$r1
	addc	$d1,$d1,$t0
	addze	$d2,$d2

	mulld	$t0,$h1,$r0		# h1*r0
	mulhdu	$t1,$h1,$r0
	addc	$d1,$d1,$t0
	adde	$d2,$d2,$t1

	mulld	$t0,$h2,$s1		# h2*5*r1
	mulld	$t1,$h2,$r0		# h2*r0
	addc	$d1,$d1,$t0
	adde	$d2,$d2,$t1

	andc	$t0,$d2,$mask		# final reduction step
	and	$h2,$d2,$mask
	srdi	$t1,$t0,2
	add	$t0,$t0,$t1
	addc	$h0,$d0,$t0
	addze	$h1,$d1
	addze	$h2,$h2

	bdnz	Loop

	std	$h0,0($ctx)		# store hash value
	std	$h1,8($ctx)
	std	$h2,16($ctx)

	$POP	r27,`$FRAME-$SIZE_T*5`($sp)
	$POP	r28,`$FRAME-$SIZE_T*4`($sp)
	$POP	r29,`$FRAME-$SIZE_T*3`($sp)
	$POP	r30,`$FRAME-$SIZE_T*2`($sp)
	$POP	r31,`$FRAME-$SIZE_T*1`($sp)
	addi	$sp,$sp,$FRAME
Labort:
	blr
	.long	0
	.byte	0,12,4,1,0x80,5,4,0
.size	.poly1305_blocks,.-.poly1305_blocks

.globl	.poly1305_emit
.align	4
.poly1305_emit:
	ld	$h0,0($ctx)		# load hash
	ld	$h1,8($ctx)
	ld	$h2,16($ctx)
	ld	$padbit,0($nonce)	# load nonce
	ld	$nonce,8($nonce)

	addic	$d0,$h0,5		# compare to modulus
	addze	$d1,$h1
	addze	$d2,$h2

	srdi	$mask,$d2,2		# did it carry/borrow?
	neg	$mask,$mask

	andc	$h0,$h0,$mask
	and	$d0,$d0,$mask
	andc	$h1,$h1,$mask
	and	$d1,$d1,$mask
	or	$h0,$h0,$d0
	or	$h1,$h1,$d1
___
$code.=<<___	if (!$LITTLE_ENDIAN);
	rotldi	$padbit,$padbit,32	# flip nonce words
	rotldi	$nonce,$nonce,32
___
$code.=<<___;
	addc	$h0,$h0,$padbit		# accumulate nonce
	adde	$h1,$h1,$nonce
___
$code.=<<___	if ($LITTLE_ENDIAN);
	std	$h0,0($mac)		# write result
	std	$h1,8($mac)
___
$code.=<<___	if (!$LITTLE_ENDIAN);
	extrdi	r0,$h0,32,0
	li	$d0,4
	stwbrx	$h0,0,$mac		# write result
	extrdi	$h0,$h1,32,0
	li	$d1,8
	stwbrx	r0,$d0,$mac
	li	$d2,12
	stwbrx	$h1,$d1,$mac
	stwbrx	$h0,$d2,$mac
___
$code.=<<___;
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
.size	.poly1305_emit,.-.poly1305_emit
___
							} else {
###############################################################################
# base 2^32 implementation

my ($h0,$h1,$h2,$h3,$h4, $r0,$r1,$r2,$r3, $s1,$s2,$s3,
    $t0,$t1,$t2,$t3, $D0,$D1,$D2,$D3, $d0,$d1,$d2,$d3
   ) = map("r$_",(7..12,14..31));

$code.=<<___;
.globl	.poly1305_init_int
.align	4
.poly1305_init_int:
	xor	r0,r0,r0
	stw	r0,0($ctx)		# zero hash value
	stw	r0,4($ctx)
	stw	r0,8($ctx)
	stw	r0,12($ctx)
	stw	r0,16($ctx)

	$UCMP	$inp,r0
	beq-	Lno_key
___
$code.=<<___	if ($LITTLE_ENDIAN);
	lw	$h0,0($inp)		# load key material
	lw	$h1,4($inp)
	lw	$h2,8($inp)
	lw	$h3,12($inp)
___
$code.=<<___	if (!$LITTLE_ENDIAN);
	li	$h1,4
	lwbrx	$h0,0,$inp		# load key material
	li	$h2,8
	lwbrx	$h1,$h1,$inp
	li	$h3,12
	lwbrx	$h2,$h2,$inp
	lwbrx	$h3,$h3,$inp
___
$code.=<<___;
	lis	$mask,0xf000		# 0xf0000000
	li	$r0,-4
	andc	$r0,$r0,$mask		# 0x0ffffffc

	andc	$h0,$h0,$mask
	and	$h1,$h1,$r0
	and	$h2,$h2,$r0
	and	$h3,$h3,$r0

	stw	$h0,32($ctx)		# store key
	stw	$h1,36($ctx)
	stw	$h2,40($ctx)
	stw	$h3,44($ctx)

Lno_key:
	xor	r3,r3,r3
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,2,0
.size	.poly1305_init_int,.-.poly1305_init_int

.globl	.poly1305_blocks
.align	4
.poly1305_blocks:
	srwi.	$len,$len,4
	beq-	Labort

	$STU	$sp,-$FRAME($sp)
	mflr	r0
	$PUSH	r14,`$FRAME-$SIZE_T*18`($sp)
	$PUSH	r15,`$FRAME-$SIZE_T*17`($sp)
	$PUSH	r16,`$FRAME-$SIZE_T*16`($sp)
	$PUSH	r17,`$FRAME-$SIZE_T*15`($sp)
	$PUSH	r18,`$FRAME-$SIZE_T*14`($sp)
	$PUSH	r19,`$FRAME-$SIZE_T*13`($sp)
	$PUSH	r20,`$FRAME-$SIZE_T*12`($sp)
	$PUSH	r21,`$FRAME-$SIZE_T*11`($sp)
	$PUSH	r22,`$FRAME-$SIZE_T*10`($sp)
	$PUSH	r23,`$FRAME-$SIZE_T*9`($sp)
	$PUSH	r24,`$FRAME-$SIZE_T*8`($sp)
	$PUSH	r25,`$FRAME-$SIZE_T*7`($sp)
	$PUSH	r26,`$FRAME-$SIZE_T*6`($sp)
	$PUSH	r27,`$FRAME-$SIZE_T*5`($sp)
	$PUSH	r28,`$FRAME-$SIZE_T*4`($sp)
	$PUSH	r29,`$FRAME-$SIZE_T*3`($sp)
	$PUSH	r30,`$FRAME-$SIZE_T*2`($sp)
	$PUSH	r31,`$FRAME-$SIZE_T*1`($sp)
	$PUSH	r0,`$FRAME+$LRSAVE`($sp)

	lwz	$r0,32($ctx)		# load key
	lwz	$r1,36($ctx)
	lwz	$r2,40($ctx)
	lwz	$r3,44($ctx)

	lwz	$h0,0($ctx)		# load hash value
	lwz	$h1,4($ctx)
	lwz	$h2,8($ctx)
	lwz	$h3,12($ctx)
	lwz	$h4,16($ctx)

	srwi	$s1,$r1,2
	srwi	$s2,$r2,2
	srwi	$s3,$r3,2
	add	$s1,$s1,$r1		# si = ri + ri>>2
	add	$s2,$s2,$r2
	add	$s3,$s3,$r3
	mtctr	$len
	li	$mask,3
	b	Loop

.align	4
Loop:
___
$code.=<<___	if ($LITTLE_ENDIAN);
	lwz	$d0,0($inp)		# load input
	lwz	$d1,4($inp)
	lwz	$d2,8($inp)
	lwz	$d3,12($inp)
___
$code.=<<___	if (!$LITTLE_ENDIAN);
	li	$d1,4
	lwbrx	$d0,0,$inp		# load input
	li	$d2,8
	lwbrx	$d1,$d1,$inp
	li	$d3,12
	lwbrx	$d2,$d2,$inp
	lwbrx	$d3,$d3,$inp
___
$code.=<<___;
	addi	$inp,$inp,16

	addc	$h0,$h0,$d0		# accumulate input
	adde	$h1,$h1,$d1
	adde	$h2,$h2,$d2

	mullw	$d0,$h0,$r0		# h0*r0
	mulhwu	$D0,$h0,$r0

	mullw	$d1,$h0,$r1		# h0*r1
	mulhwu	$D1,$h0,$r1

	mullw	$d2,$h0,$r2		# h0*r2
	mulhwu	$D2,$h0,$r2

	 adde	$h3,$h3,$d3
	 adde	$h4,$h4,$padbit

	mullw	$d3,$h0,$r3		# h0*r3
	mulhwu	$D3,$h0,$r3

	mullw	$t0,$h1,$s3		# h1*s3
	mulhwu	$t1,$h1,$s3

	mullw	$t2,$h1,$r0		# h1*r0
	mulhwu	$t3,$h1,$r0
	 addc	$d0,$d0,$t0
	 adde	$D0,$D0,$t1

	mullw	$t0,$h1,$r1		# h1*r1
	mulhwu	$t1,$h1,$r1
	 addc	$d1,$d1,$t2
	 adde	$D1,$D1,$t3

	mullw	$t2,$h1,$r2		# h1*r2
	mulhwu	$t3,$h1,$r2
	 addc	$d2,$d2,$t0
	 adde	$D2,$D2,$t1

	mullw	$t0,$h2,$s2		# h2*s2
	mulhwu	$t1,$h2,$s2
	 addc	$d3,$d3,$t2
	 adde	$D3,$D3,$t3

	mullw	$t2,$h2,$s3		# h2*s3
	mulhwu	$t3,$h2,$s3
	 addc	$d0,$d0,$t0
	 adde	$D0,$D0,$t1

	mullw	$t0,$h2,$r0		# h2*r0
	mulhwu	$t1,$h2,$r0
	 addc	$d1,$d1,$t2
	 adde	$D1,$D1,$t3

	mullw	$t2,$h2,$r1		# h2*r1
	mulhwu	$t3,$h2,$r1
	 addc	$d2,$d2,$t0
	 adde	$D2,$D2,$t1

	mullw	$t0,$h3,$s1		# h3*s1
	mulhwu	$t1,$h3,$s1
	 addc	$d3,$d3,$t2
	 adde	$D3,$D3,$t3

	mullw	$t2,$h3,$s2		# h3*s2
	mulhwu	$t3,$h3,$s2
	 addc	$d0,$d0,$t0
	 adde	$D0,$D0,$t1

	mullw	$t0,$h3,$s3		# h3*s3
	mulhwu	$t1,$h3,$s3
	 addc	$d1,$d1,$t2
	 adde	$D1,$D1,$t3

	mullw	$t2,$h3,$r0		# h3*r0
	mulhwu	$t3,$h3,$r0
	 addc	$d2,$d2,$t0
	 adde	$D2,$D2,$t1

	mullw	$t0,$h4,$s1		# h4*s1
	 addc	$d3,$d3,$t2
	 adde	$D3,$D3,$t3
	addc	$d1,$d1,$t0

	mullw	$t1,$h4,$s2		# h4*s2
	 addze	$D1,$D1
	addc	$d2,$d2,$t1
	addze	$D2,$D2

	mullw	$t2,$h4,$s3		# h4*s3
	addc	$d3,$d3,$t2
	addze	$D3,$D3

	mullw	$h4,$h4,$r0		# h4*r0

	addc	$h1,$d1,$D0
	adde	$h2,$d2,$D1
	adde	$h3,$d3,$D2
	adde	$h4,$h4,$D3

	andc	$D0,$h4,$mask		# final reduction step
	and	$h4,$h4,$mask
	srwi	$D1,$D0,2
	add	$D0,$D0,$D1
	addc	$h0,$d0,$D0
	addze	$h1,$h1
	addze	$h2,$h2
	addze	$h3,$h3
	addze	$h4,$h4

	bdnz	Loop

	stw	$h0,0($ctx)		# store hash value
	stw	$h1,4($ctx)
	stw	$h2,8($ctx)
	stw	$h3,12($ctx)
	stw	$h4,16($ctx)

	$POP	r14,`$FRAME-$SIZE_T*18`($sp)
	$POP	r15,`$FRAME-$SIZE_T*17`($sp)
	$POP	r16,`$FRAME-$SIZE_T*16`($sp)
	$POP	r17,`$FRAME-$SIZE_T*15`($sp)
	$POP	r18,`$FRAME-$SIZE_T*14`($sp)
	$POP	r19,`$FRAME-$SIZE_T*13`($sp)
	$POP	r20,`$FRAME-$SIZE_T*12`($sp)
	$POP	r21,`$FRAME-$SIZE_T*11`($sp)
	$POP	r22,`$FRAME-$SIZE_T*10`($sp)
	$POP	r23,`$FRAME-$SIZE_T*9`($sp)
	$POP	r24,`$FRAME-$SIZE_T*8`($sp)
	$POP	r25,`$FRAME-$SIZE_T*7`($sp)
	$POP	r26,`$FRAME-$SIZE_T*6`($sp)
	$POP	r27,`$FRAME-$SIZE_T*5`($sp)
	$POP	r28,`$FRAME-$SIZE_T*4`($sp)
	$POP	r29,`$FRAME-$SIZE_T*3`($sp)
	$POP	r30,`$FRAME-$SIZE_T*2`($sp)
	$POP	r31,`$FRAME-$SIZE_T*1`($sp)
	addi	$sp,$sp,$FRAME
Labort:
	blr
	.long	0
	.byte	0,12,4,1,0x80,18,4,0
.size	.poly1305_blocks,.-.poly1305_blocks

.globl	.poly1305_emit
.align	4
.poly1305_emit:
	$STU	$sp,-$FRAME($sp)
	mflr	r0
	$PUSH	r28,`$FRAME-$SIZE_T*4`($sp)
	$PUSH	r29,`$FRAME-$SIZE_T*3`($sp)
	$PUSH	r30,`$FRAME-$SIZE_T*2`($sp)
	$PUSH	r31,`$FRAME-$SIZE_T*1`($sp)
	$PUSH	r0,`$FRAME+$LRSAVE`($sp)

	lwz	$h0,0($ctx)		# load hash
	lwz	$h1,4($ctx)
	lwz	$h2,8($ctx)
	lwz	$h3,12($ctx)
	lwz	$h4,16($ctx)

	addic	$d0,$h0,5		# compare to modulus
	addze	$d1,$h1
	addze	$d2,$h2
	addze	$d3,$h3
	addze	$mask,$h4

	srwi	$mask,$mask,2		# did it carry/borrow?
	neg	$mask,$mask

	andc	$h0,$h0,$mask
	and	$d0,$d0,$mask
	andc	$h1,$h1,$mask
	and	$d1,$d1,$mask
	or	$h0,$h0,$d0
	lwz	$d0,0($nonce)		# load nonce
	andc	$h2,$h2,$mask
	and	$d2,$d2,$mask
	or	$h1,$h1,$d1
	lwz	$d1,4($nonce)
	andc	$h3,$h3,$mask
	and	$d3,$d3,$mask
	or	$h2,$h2,$d2
	lwz	$d2,8($nonce)
	or	$h3,$h3,$d3
	lwz	$d3,12($nonce)

	addc	$h0,$h0,$d0		# accumulate nonce
	adde	$h1,$h1,$d1
	adde	$h2,$h2,$d2
	adde	$h3,$h3,$d3
___
$code.=<<___	if ($LITTLE_ENDIAN);
	stw	$h0,0($mac)		# write result
	stw	$h1,4($mac)
	stw	$h2,8($mac)
	stw	$h3,12($mac)
___
$code.=<<___	if (!$LITTLE_ENDIAN);
	li	$d1,4
	stwbrx	$h0,0,$mac		# write result
	li	$d2,8
	stwbrx	$h1,$d1,$mac
	li	$d3,12
	stwbrx	$h2,$d2,$mac
	stwbrx	$h3,$d3,$mac
___
$code.=<<___;
	$POP	r28,`$FRAME-$SIZE_T*4`($sp)
	$POP	r29,`$FRAME-$SIZE_T*3`($sp)
	$POP	r30,`$FRAME-$SIZE_T*2`($sp)
	$POP	r31,`$FRAME-$SIZE_T*1`($sp)
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,4,1,0x80,4,3,0
.size	.poly1305_emit,.-.poly1305_emit
___
							}
$code.=<<___;
.asciz	"Poly1305 for PPC, CRYPTOGAMS by <appro\@openssl.org>"
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

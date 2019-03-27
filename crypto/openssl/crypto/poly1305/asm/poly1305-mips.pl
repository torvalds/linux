#! /usr/bin/env perl
# Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
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

# Poly1305 hash for MIPS64.
#
# May 2016
#
# Numbers are cycles per processed byte with poly1305_blocks alone.
#
#		IALU/gcc
# R1x000	5.64/+120%	(big-endian)
# Octeon II	3.80/+280%	(little-endian)

######################################################################
# There is a number of MIPS ABI in use, O32 and N32/64 are most
# widely used. Then there is a new contender: NUBI. It appears that if
# one picks the latter, it's possible to arrange code in ABI neutral
# manner. Therefore let's stick to NUBI register layout:
#
($zero,$at,$t0,$t1,$t2)=map("\$$_",(0..2,24,25));
($a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7)=map("\$$_",(4..11));
($s0,$s1,$s2,$s3,$s4,$s5,$s6,$s7,$s8,$s9,$s10,$s11)=map("\$$_",(12..23));
($gp,$tp,$sp,$fp,$ra)=map("\$$_",(3,28..31));
#
# The return value is placed in $a0. Following coding rules facilitate
# interoperability:
#
# - never ever touch $tp, "thread pointer", former $gp [o32 can be
#   excluded from the rule, because it's specified volatile];
# - copy return value to $t0, former $v0 [or to $a0 if you're adapting
#   old code];
# - on O32 populate $a4-$a7 with 'lw $aN,4*N($sp)' if necessary;
#
# For reference here is register layout for N32/64 MIPS ABIs:
#
# ($zero,$at,$v0,$v1)=map("\$$_",(0..3));
# ($a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7)=map("\$$_",(4..11));
# ($t0,$t1,$t2,$t3,$t8,$t9)=map("\$$_",(12..15,24,25));
# ($s0,$s1,$s2,$s3,$s4,$s5,$s6,$s7)=map("\$$_",(16..23));
# ($gp,$sp,$fp,$ra)=map("\$$_",(28..31));
#
# <appro@openssl.org>
#
######################################################################

$flavour = shift || "o32"; # supported flavours are o32,n32,64,nubi32,nubi64

die "MIPS64 only" unless ($flavour =~ /64|n32/i);

$v0 = ($flavour =~ /nubi/i) ? $a0 : $t0;
$SAVED_REGS_MASK = ($flavour =~ /nubi/i) ? "0x0003f000" : "0x00030000";

($ctx,$inp,$len,$padbit) = ($a0,$a1,$a2,$a3);
($in0,$in1,$tmp0,$tmp1,$tmp2,$tmp3,$tmp4) = ($a4,$a5,$a6,$a7,$at,$t0,$t1);

$code.=<<___;
#include "mips_arch.h"

#ifdef MIPSEB
# define MSB 0
# define LSB 7
#else
# define MSB 7
# define LSB 0
#endif

.text
.set	noat
.set	noreorder

.align	5
.globl	poly1305_init
.ent	poly1305_init
poly1305_init:
	.frame	$sp,0,$ra
	.set	reorder

	sd	$zero,0($ctx)
	sd	$zero,8($ctx)
	sd	$zero,16($ctx)

	beqz	$inp,.Lno_key

#if defined(_MIPS_ARCH_MIPS64R6)
	ld	$in0,0($inp)
	ld	$in1,8($inp)
#else
	ldl	$in0,0+MSB($inp)
	ldl	$in1,8+MSB($inp)
	ldr	$in0,0+LSB($inp)
	ldr	$in1,8+LSB($inp)
#endif
#ifdef	MIPSEB
# if defined(_MIPS_ARCH_MIPS64R2)
	dsbh	$in0,$in0		# byte swap
	 dsbh	$in1,$in1
	dshd	$in0,$in0
	 dshd	$in1,$in1
# else
	ori	$tmp0,$zero,0xFF
	dsll	$tmp2,$tmp0,32
	or	$tmp0,$tmp2		# 0x000000FF000000FF

	and	$tmp1,$in0,$tmp0	# byte swap
	 and	$tmp3,$in1,$tmp0
	dsrl	$tmp2,$in0,24
	 dsrl	$tmp4,$in1,24
	dsll	$tmp1,24
	 dsll	$tmp3,24
	and	$tmp2,$tmp0
	 and	$tmp4,$tmp0
	dsll	$tmp0,8			# 0x0000FF000000FF00
	or	$tmp1,$tmp2
	 or	$tmp3,$tmp4
	and	$tmp2,$in0,$tmp0
	 and	$tmp4,$in1,$tmp0
	dsrl	$in0,8
	 dsrl	$in1,8
	dsll	$tmp2,8
	 dsll	$tmp4,8
	and	$in0,$tmp0
	 and	$in1,$tmp0
	or	$tmp1,$tmp2
	 or	$tmp3,$tmp4
	or	$in0,$tmp1
	 or	$in1,$tmp3
	dsrl	$tmp1,$in0,32
	 dsrl	$tmp3,$in1,32
	dsll	$in0,32
	 dsll	$in1,32
	or	$in0,$tmp1
	 or	$in1,$tmp3
# endif
#endif
	li	$tmp0,1
	dsll	$tmp0,32
	daddiu	$tmp0,-63
	dsll	$tmp0,28
	daddiu	$tmp0,-1		# 0ffffffc0fffffff

	and	$in0,$tmp0
	daddiu	$tmp0,-3		# 0ffffffc0ffffffc
	and	$in1,$tmp0

	sd	$in0,24($ctx)
	dsrl	$tmp0,$in1,2
	sd	$in1,32($ctx)
	daddu	$tmp0,$in1		# s1 = r1 + (r1 >> 2)
	sd	$tmp0,40($ctx)

.Lno_key:
	li	$v0,0			# return 0
	jr	$ra
.end	poly1305_init
___
{
my ($h0,$h1,$h2,$r0,$r1,$s1,$d0,$d1,$d2) =
   ($s0,$s1,$s2,$s3,$s4,$s5,$in0,$in1,$t2);

$code.=<<___;
.align	5
.globl	poly1305_blocks
.ent	poly1305_blocks
poly1305_blocks:
	.set	noreorder
	dsrl	$len,4			# number of complete blocks
	bnez	$len,poly1305_blocks_internal
	nop
	jr	$ra
	nop
.end	poly1305_blocks

.align	5
.ent	poly1305_blocks_internal
poly1305_blocks_internal:
	.frame	$sp,6*8,$ra
	.mask	$SAVED_REGS_MASK,-8
	.set	noreorder
	dsubu	$sp,6*8
	sd	$s5,40($sp)
	sd	$s4,32($sp)
___
$code.=<<___ if ($flavour =~ /nubi/i);	# optimize non-nubi prologue
	sd	$s3,24($sp)
	sd	$s2,16($sp)
	sd	$s1,8($sp)
	sd	$s0,0($sp)
___
$code.=<<___;
	.set	reorder

	ld	$h0,0($ctx)		# load hash value
	ld	$h1,8($ctx)
	ld	$h2,16($ctx)

	ld	$r0,24($ctx)		# load key
	ld	$r1,32($ctx)
	ld	$s1,40($ctx)

.Loop:
#if defined(_MIPS_ARCH_MIPS64R6)
	ld	$in0,0($inp)		# load input
	ld	$in1,8($inp)
#else
	ldl	$in0,0+MSB($inp)	# load input
	ldl	$in1,8+MSB($inp)
	ldr	$in0,0+LSB($inp)
	ldr	$in1,8+LSB($inp)
#endif
	daddiu	$len,-1
	daddiu	$inp,16
#ifdef	MIPSEB
# if defined(_MIPS_ARCH_MIPS64R2)
	dsbh	$in0,$in0		# byte swap
	 dsbh	$in1,$in1
	dshd	$in0,$in0
	 dshd	$in1,$in1
# else
	ori	$tmp0,$zero,0xFF
	dsll	$tmp2,$tmp0,32
	or	$tmp0,$tmp2		# 0x000000FF000000FF

	and	$tmp1,$in0,$tmp0	# byte swap
	 and	$tmp3,$in1,$tmp0
	dsrl	$tmp2,$in0,24
	 dsrl	$tmp4,$in1,24
	dsll	$tmp1,24
	 dsll	$tmp3,24
	and	$tmp2,$tmp0
	 and	$tmp4,$tmp0
	dsll	$tmp0,8			# 0x0000FF000000FF00
	or	$tmp1,$tmp2
	 or	$tmp3,$tmp4
	and	$tmp2,$in0,$tmp0
	 and	$tmp4,$in1,$tmp0
	dsrl	$in0,8
	 dsrl	$in1,8
	dsll	$tmp2,8
	 dsll	$tmp4,8
	and	$in0,$tmp0
	 and	$in1,$tmp0
	or	$tmp1,$tmp2
	 or	$tmp3,$tmp4
	or	$in0,$tmp1
	 or	$in1,$tmp3
	dsrl	$tmp1,$in0,32
	 dsrl	$tmp3,$in1,32
	dsll	$in0,32
	 dsll	$in1,32
	or	$in0,$tmp1
	 or	$in1,$tmp3
# endif
#endif
	daddu	$h0,$in0		# accumulate input
	daddu	$h1,$in1
	sltu	$tmp0,$h0,$in0
	sltu	$tmp1,$h1,$in1
	daddu	$h1,$tmp0

	dmultu	($r0,$h0)		# h0*r0
	 daddu	$h2,$padbit
	 sltu	$tmp0,$h1,$tmp0
	mflo	($d0,$r0,$h0)
	mfhi	($d1,$r0,$h0)

	dmultu	($s1,$h1)		# h1*5*r1
	 daddu	$tmp0,$tmp1
	 daddu	$h2,$tmp0
	mflo	($tmp0,$s1,$h1)
	mfhi	($tmp1,$s1,$h1)

	dmultu	($r1,$h0)		# h0*r1
	 daddu	$d0,$tmp0
	 daddu	$d1,$tmp1
	mflo	($tmp2,$r1,$h0)
	mfhi	($d2,$r1,$h0)
	 sltu	$tmp0,$d0,$tmp0
	 daddu	$d1,$tmp0

	dmultu	($r0,$h1)		# h1*r0
	 daddu	$d1,$tmp2
	 sltu	$tmp2,$d1,$tmp2
	mflo	($tmp0,$r0,$h1)
	mfhi	($tmp1,$r0,$h1)
	 daddu	$d2,$tmp2

	dmultu	($s1,$h2)		# h2*5*r1
	 daddu	$d1,$tmp0
	 daddu	$d2,$tmp1
	mflo	($tmp2,$s1,$h2)

	dmultu	($r0,$h2)		# h2*r0
	 sltu	$tmp0,$d1,$tmp0
	 daddu	$d2,$tmp0
	mflo	($tmp3,$r0,$h2)

	daddu	$d1,$tmp2
	daddu	$d2,$tmp3
	sltu	$tmp2,$d1,$tmp2
	daddu	$d2,$tmp2

	li	$tmp0,-4		# final reduction
	and	$tmp0,$d2
	dsrl	$tmp1,$d2,2
	andi	$h2,$d2,3
	daddu	$tmp0,$tmp1
	daddu	$h0,$d0,$tmp0
	sltu	$tmp0,$h0,$tmp0
	daddu	$h1,$d1,$tmp0
	sltu	$tmp0,$h1,$tmp0
	daddu	$h2,$h2,$tmp0

	bnez	$len,.Loop

	sd	$h0,0($ctx)		# store hash value
	sd	$h1,8($ctx)
	sd	$h2,16($ctx)

	.set	noreorder
	ld	$s5,40($sp)		# epilogue
	ld	$s4,32($sp)
___
$code.=<<___ if ($flavour =~ /nubi/i);	# optimize non-nubi epilogue
	ld	$s3,24($sp)
	ld	$s2,16($sp)
	ld	$s1,8($sp)
	ld	$s0,0($sp)
___
$code.=<<___;
	jr	$ra
	daddu	$sp,6*8
.end	poly1305_blocks_internal
___
}
{
my ($ctx,$mac,$nonce) = ($a0,$a1,$a2);

$code.=<<___;
.align	5
.globl	poly1305_emit
.ent	poly1305_emit
poly1305_emit:
	.frame	$sp,0,$ra
	.set	reorder

	ld	$tmp0,0($ctx)
	ld	$tmp1,8($ctx)
	ld	$tmp2,16($ctx)

	daddiu	$in0,$tmp0,5		# compare to modulus
	sltiu	$tmp3,$in0,5
	daddu	$in1,$tmp1,$tmp3
	sltu	$tmp3,$in1,$tmp3
	daddu	$tmp2,$tmp2,$tmp3

	dsrl	$tmp2,2			# see if it carried/borrowed
	dsubu	$tmp2,$zero,$tmp2
	nor	$tmp3,$zero,$tmp2

	and	$in0,$tmp2
	and	$tmp0,$tmp3
	and	$in1,$tmp2
	and	$tmp1,$tmp3
	or	$in0,$tmp0
	or	$in1,$tmp1

	lwu	$tmp0,0($nonce)		# load nonce
	lwu	$tmp1,4($nonce)
	lwu	$tmp2,8($nonce)
	lwu	$tmp3,12($nonce)
	dsll	$tmp1,32
	dsll	$tmp3,32
	or	$tmp0,$tmp1
	or	$tmp2,$tmp3

	daddu	$in0,$tmp0		# accumulate nonce
	daddu	$in1,$tmp2
	sltu	$tmp0,$in0,$tmp0
	daddu	$in1,$tmp0

	dsrl	$tmp0,$in0,8		# write mac value
	dsrl	$tmp1,$in0,16
	dsrl	$tmp2,$in0,24
	sb	$in0,0($mac)
	dsrl	$tmp3,$in0,32
	sb	$tmp0,1($mac)
	dsrl	$tmp0,$in0,40
	sb	$tmp1,2($mac)
	dsrl	$tmp1,$in0,48
	sb	$tmp2,3($mac)
	dsrl	$tmp2,$in0,56
	sb	$tmp3,4($mac)
	dsrl	$tmp3,$in1,8
	sb	$tmp0,5($mac)
	dsrl	$tmp0,$in1,16
	sb	$tmp1,6($mac)
	dsrl	$tmp1,$in1,24
	sb	$tmp2,7($mac)

	sb	$in1,8($mac)
	dsrl	$tmp2,$in1,32
	sb	$tmp3,9($mac)
	dsrl	$tmp3,$in1,40
	sb	$tmp0,10($mac)
	dsrl	$tmp0,$in1,48
	sb	$tmp1,11($mac)
	dsrl	$tmp1,$in1,56
	sb	$tmp2,12($mac)
	sb	$tmp3,13($mac)
	sb	$tmp0,14($mac)
	sb	$tmp1,15($mac)

	jr	$ra
.end	poly1305_emit
.rdata
.asciiz	"Poly1305 for MIPS64, CRYPTOGAMS by <appro\@openssl.org>"
.align	2
___
}

$output=pop and open STDOUT,">$output";
print $code;
close STDOUT;


#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0

# This code is taken from the OpenSSL project but the author (Andy Polyakov)
# has relicensed it under the GPLv2. Therefore this program is free software;
# you can redistribute it and/or modify it under the terms of the GNU General
# Public License version 2 as published by the Free Software Foundation.
#
# The original headers, including the original license headers, are
# included below for completeness.

# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see https://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# GHASH for PowerISA v2.07.
#
# July 2014
#
# Accurate performance measurements are problematic, because it's
# always virtualized setup with possibly throttled processor.
# Relative comparison is therefore more informative. This initial
# version is ~2.1x slower than hardware-assisted AES-128-CTR, ~12x
# faster than "4-bit" integer-only compiler-generated 64-bit code.
# "Initial version" means that there is room for futher improvement.

$flavour=shift;
$output =shift;

if ($flavour =~ /64/) {
	$SIZE_T=8;
	$LRSAVE=2*$SIZE_T;
	$STU="stdu";
	$POP="ld";
	$PUSH="std";
} elsif ($flavour =~ /32/) {
	$SIZE_T=4;
	$LRSAVE=$SIZE_T;
	$STU="stwu";
	$POP="lwz";
	$PUSH="stw";
} else { die "nonsense $flavour"; }

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour $output" || die "can't call $xlate: $!";

my ($Xip,$Htbl,$inp,$len)=map("r$_",(3..6));	# argument block

my ($Xl,$Xm,$Xh,$IN)=map("v$_",(0..3));
my ($zero,$t0,$t1,$t2,$xC2,$H,$Hh,$Hl,$lemask)=map("v$_",(4..12));
my ($Xl1,$Xm1,$Xh1,$IN1,$H2,$H2h,$H2l)=map("v$_",(13..19));
my $vrsave="r12";
my ($t4,$t5,$t6) = ($Hl,$H,$Hh);

$code=<<___;
.machine	"any"

.text

.globl	.gcm_init_p8
	lis		r0,0xfff0
	li		r8,0x10
	mfspr		$vrsave,256
	li		r9,0x20
	mtspr		256,r0
	li		r10,0x30
	lvx_u		$H,0,r4			# load H
	le?xor		r7,r7,r7
	le?addi		r7,r7,0x8		# need a vperm start with 08
	le?lvsr		5,0,r7
	le?vspltisb	6,0x0f
	le?vxor		5,5,6			# set a b-endian mask
	le?vperm	$H,$H,$H,5

	vspltisb	$xC2,-16		# 0xf0
	vspltisb	$t0,1			# one
	vaddubm		$xC2,$xC2,$xC2		# 0xe0
	vxor		$zero,$zero,$zero
	vor		$xC2,$xC2,$t0		# 0xe1
	vsldoi		$xC2,$xC2,$zero,15	# 0xe1...
	vsldoi		$t1,$zero,$t0,1		# ...1
	vaddubm		$xC2,$xC2,$xC2		# 0xc2...
	vspltisb	$t2,7
	vor		$xC2,$xC2,$t1		# 0xc2....01
	vspltb		$t1,$H,0		# most significant byte
	vsl		$H,$H,$t0		# H<<=1
	vsrab		$t1,$t1,$t2		# broadcast carry bit
	vand		$t1,$t1,$xC2
	vxor		$H,$H,$t1		# twisted H

	vsldoi		$H,$H,$H,8		# twist even more ...
	vsldoi		$xC2,$zero,$xC2,8	# 0xc2.0
	vsldoi		$Hl,$zero,$H,8		# ... and split
	vsldoi		$Hh,$H,$zero,8

	stvx_u		$xC2,0,r3		# save pre-computed table
	stvx_u		$Hl,r8,r3
	stvx_u		$H, r9,r3
	stvx_u		$Hh,r10,r3

	mtspr		256,$vrsave
	blr
	.long		0
	.byte		0,12,0x14,0,0,0,2,0
	.long		0
.size	.gcm_init_p8,.-.gcm_init_p8

.globl	.gcm_init_htable
	lis		r0,0xfff0
	li		r8,0x10
	mfspr		$vrsave,256
	li		r9,0x20
	mtspr		256,r0
	li		r10,0x30
	lvx_u		$H,0,r4			# load H

	vspltisb	$xC2,-16		# 0xf0
	vspltisb	$t0,1			# one
	vaddubm		$xC2,$xC2,$xC2		# 0xe0
	vxor		$zero,$zero,$zero
	vor		$xC2,$xC2,$t0		# 0xe1
	vsldoi		$xC2,$xC2,$zero,15	# 0xe1...
	vsldoi		$t1,$zero,$t0,1		# ...1
	vaddubm		$xC2,$xC2,$xC2		# 0xc2...
	vspltisb	$t2,7
	vor		$xC2,$xC2,$t1		# 0xc2....01
	vspltb		$t1,$H,0		# most significant byte
	vsl		$H,$H,$t0		# H<<=1
	vsrab		$t1,$t1,$t2		# broadcast carry bit
	vand		$t1,$t1,$xC2
	vxor		$IN,$H,$t1		# twisted H

	vsldoi		$H,$IN,$IN,8		# twist even more ...
	vsldoi		$xC2,$zero,$xC2,8	# 0xc2.0
	vsldoi		$Hl,$zero,$H,8		# ... and split
	vsldoi		$Hh,$H,$zero,8

	stvx_u		$xC2,0,r3		# save pre-computed table
	stvx_u		$Hl,r8,r3
	li		r8,0x40
	stvx_u		$H, r9,r3
	li		r9,0x50
	stvx_u		$Hh,r10,r3
	li		r10,0x60

	vpmsumd		$Xl,$IN,$Hl		# H.lo·H.lo
	vpmsumd		$Xm,$IN,$H		# H.hi·H.lo+H.lo·H.hi
	vpmsumd		$Xh,$IN,$Hh		# H.hi·H.hi

	vpmsumd		$t2,$Xl,$xC2		# 1st reduction phase

	vsldoi		$t0,$Xm,$zero,8
	vsldoi		$t1,$zero,$Xm,8
	vxor		$Xl,$Xl,$t0
	vxor		$Xh,$Xh,$t1

	vsldoi		$Xl,$Xl,$Xl,8
	vxor		$Xl,$Xl,$t2

	vsldoi		$t1,$Xl,$Xl,8		# 2nd reduction phase
	vpmsumd		$Xl,$Xl,$xC2
	vxor		$t1,$t1,$Xh
	vxor		$IN1,$Xl,$t1

	vsldoi		$H2,$IN1,$IN1,8
	vsldoi		$H2l,$zero,$H2,8
	vsldoi		$H2h,$H2,$zero,8

	stvx_u		$H2l,r8,r3		# save H^2
	li		r8,0x70
	stvx_u		$H2,r9,r3
	li		r9,0x80
	stvx_u		$H2h,r10,r3
	li		r10,0x90

	vpmsumd		$Xl,$IN,$H2l		# H.lo·H^2.lo
	 vpmsumd	$Xl1,$IN1,$H2l		# H^2.lo·H^2.lo
	vpmsumd		$Xm,$IN,$H2		# H.hi·H^2.lo+H.lo·H^2.hi
	 vpmsumd	$Xm1,$IN1,$H2		# H^2.hi·H^2.lo+H^2.lo·H^2.hi
	vpmsumd		$Xh,$IN,$H2h		# H.hi·H^2.hi
	 vpmsumd	$Xh1,$IN1,$H2h		# H^2.hi·H^2.hi

	vpmsumd		$t2,$Xl,$xC2		# 1st reduction phase
	 vpmsumd	$t6,$Xl1,$xC2		# 1st reduction phase

	vsldoi		$t0,$Xm,$zero,8
	vsldoi		$t1,$zero,$Xm,8
	 vsldoi		$t4,$Xm1,$zero,8
	 vsldoi		$t5,$zero,$Xm1,8
	vxor		$Xl,$Xl,$t0
	vxor		$Xh,$Xh,$t1
	 vxor		$Xl1,$Xl1,$t4
	 vxor		$Xh1,$Xh1,$t5

	vsldoi		$Xl,$Xl,$Xl,8
	 vsldoi		$Xl1,$Xl1,$Xl1,8
	vxor		$Xl,$Xl,$t2
	 vxor		$Xl1,$Xl1,$t6

	vsldoi		$t1,$Xl,$Xl,8		# 2nd reduction phase
	 vsldoi		$t5,$Xl1,$Xl1,8		# 2nd reduction phase
	vpmsumd		$Xl,$Xl,$xC2
	 vpmsumd	$Xl1,$Xl1,$xC2
	vxor		$t1,$t1,$Xh
	 vxor		$t5,$t5,$Xh1
	vxor		$Xl,$Xl,$t1
	 vxor		$Xl1,$Xl1,$t5

	vsldoi		$H,$Xl,$Xl,8
	 vsldoi		$H2,$Xl1,$Xl1,8
	vsldoi		$Hl,$zero,$H,8
	vsldoi		$Hh,$H,$zero,8
	 vsldoi		$H2l,$zero,$H2,8
	 vsldoi		$H2h,$H2,$zero,8

	stvx_u		$Hl,r8,r3		# save H^3
	li		r8,0xa0
	stvx_u		$H,r9,r3
	li		r9,0xb0
	stvx_u		$Hh,r10,r3
	li		r10,0xc0
	 stvx_u		$H2l,r8,r3		# save H^4
	 stvx_u		$H2,r9,r3
	 stvx_u		$H2h,r10,r3

	mtspr		256,$vrsave
	blr
	.long		0
	.byte		0,12,0x14,0,0,0,2,0
	.long		0
.size	.gcm_init_htable,.-.gcm_init_htable

.globl	.gcm_gmult_p8
	lis		r0,0xfff8
	li		r8,0x10
	mfspr		$vrsave,256
	li		r9,0x20
	mtspr		256,r0
	li		r10,0x30
	lvx_u		$IN,0,$Xip		# load Xi

	lvx_u		$Hl,r8,$Htbl		# load pre-computed table
	 le?lvsl	$lemask,r0,r0
	lvx_u		$H, r9,$Htbl
	 le?vspltisb	$t0,0x07
	lvx_u		$Hh,r10,$Htbl
	 le?vxor	$lemask,$lemask,$t0
	lvx_u		$xC2,0,$Htbl
	 le?vperm	$IN,$IN,$IN,$lemask
	vxor		$zero,$zero,$zero

	vpmsumd		$Xl,$IN,$Hl		# H.lo·Xi.lo
	vpmsumd		$Xm,$IN,$H		# H.hi·Xi.lo+H.lo·Xi.hi
	vpmsumd		$Xh,$IN,$Hh		# H.hi·Xi.hi

	vpmsumd		$t2,$Xl,$xC2		# 1st phase

	vsldoi		$t0,$Xm,$zero,8
	vsldoi		$t1,$zero,$Xm,8
	vxor		$Xl,$Xl,$t0
	vxor		$Xh,$Xh,$t1

	vsldoi		$Xl,$Xl,$Xl,8
	vxor		$Xl,$Xl,$t2

	vsldoi		$t1,$Xl,$Xl,8		# 2nd phase
	vpmsumd		$Xl,$Xl,$xC2
	vxor		$t1,$t1,$Xh
	vxor		$Xl,$Xl,$t1

	le?vperm	$Xl,$Xl,$Xl,$lemask
	stvx_u		$Xl,0,$Xip		# write out Xi

	mtspr		256,$vrsave
	blr
	.long		0
	.byte		0,12,0x14,0,0,0,2,0
	.long		0
.size	.gcm_gmult_p8,.-.gcm_gmult_p8

.globl	.gcm_ghash_p8
	lis		r0,0xfff8
	li		r8,0x10
	mfspr		$vrsave,256
	li		r9,0x20
	mtspr		256,r0
	li		r10,0x30
	lvx_u		$Xl,0,$Xip		# load Xi

	lvx_u		$Hl,r8,$Htbl		# load pre-computed table
	 le?lvsl	$lemask,r0,r0
	lvx_u		$H, r9,$Htbl
	 le?vspltisb	$t0,0x07
	lvx_u		$Hh,r10,$Htbl
	 le?vxor	$lemask,$lemask,$t0
	lvx_u		$xC2,0,$Htbl
	 le?vperm	$Xl,$Xl,$Xl,$lemask
	vxor		$zero,$zero,$zero

	lvx_u		$IN,0,$inp
	addi		$inp,$inp,16
	subi		$len,$len,16
	 le?vperm	$IN,$IN,$IN,$lemask
	vxor		$IN,$IN,$Xl
	b		Loop

.align	5
Loop:
	 subic		$len,$len,16
	vpmsumd		$Xl,$IN,$Hl		# H.lo·Xi.lo
	 subfe.		r0,r0,r0		# borrow?-1:0
	vpmsumd		$Xm,$IN,$H		# H.hi·Xi.lo+H.lo·Xi.hi
	 and		r0,r0,$len
	vpmsumd		$Xh,$IN,$Hh		# H.hi·Xi.hi
	 add		$inp,$inp,r0

	vpmsumd		$t2,$Xl,$xC2		# 1st phase

	vsldoi		$t0,$Xm,$zero,8
	vsldoi		$t1,$zero,$Xm,8
	vxor		$Xl,$Xl,$t0
	vxor		$Xh,$Xh,$t1

	vsldoi		$Xl,$Xl,$Xl,8
	vxor		$Xl,$Xl,$t2
	 lvx_u		$IN,0,$inp
	 addi		$inp,$inp,16

	vsldoi		$t1,$Xl,$Xl,8		# 2nd phase
	vpmsumd		$Xl,$Xl,$xC2
	 le?vperm	$IN,$IN,$IN,$lemask
	vxor		$t1,$t1,$Xh
	vxor		$IN,$IN,$t1
	vxor		$IN,$IN,$Xl
	beq		Loop			# did $len-=16 borrow?

	vxor		$Xl,$Xl,$t1
	le?vperm	$Xl,$Xl,$Xl,$lemask
	stvx_u		$Xl,0,$Xip		# write out Xi

	mtspr		256,$vrsave
	blr
	.long		0
	.byte		0,12,0x14,0,0,0,4,0
	.long		0
.size	.gcm_ghash_p8,.-.gcm_ghash_p8

.asciz  "GHASH for PowerISA 2.07, CRYPTOGAMS by <appro\@openssl.org>"
.align  2
___

foreach (split("\n",$code)) {
	if ($flavour =~ /le$/o) {	# little-endian
	    s/le\?//o		or
	    s/be\?/#be#/o;
	} else {
	    s/le\?/#le#/o	or
	    s/be\?//o;
	}
	print $_,"\n";
}

close STDOUT; # enforce flush

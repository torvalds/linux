#! /usr/bin/env perl
# Copyright 2014-2018 The OpenSSL Project Authors. All Rights Reserved.
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
# GHASH for for PowerISA v2.07.
#
# July 2014
#
# Accurate performance measurements are problematic, because it's
# always virtualized setup with possibly throttled processor.
# Relative comparison is therefore more informative. This initial
# version is ~2.1x slower than hardware-assisted AES-128-CTR, ~12x
# faster than "4-bit" integer-only compiler-generated 64-bit code.
# "Initial version" means that there is room for further improvement.

# May 2016
#
# 2x aggregated reduction improves performance by 50% (resulting
# performance on POWER8 is 1 cycle per processed byte), and 4x
# aggregated reduction - by 170% or 2.7x (resulting in 0.55 cpb).
# POWER9 delivers 0.51 cpb.

$flavour=shift;
$output =shift;

if ($flavour =~ /64/) {
	$SIZE_T=8;
	$LRSAVE=2*$SIZE_T;
	$STU="stdu";
	$POP="ld";
	$PUSH="std";
	$UCMP="cmpld";
	$SHRI="srdi";
} elsif ($flavour =~ /32/) {
	$SIZE_T=4;
	$LRSAVE=$SIZE_T;
	$STU="stwu";
	$POP="lwz";
	$PUSH="stw";
	$UCMP="cmplw";
	$SHRI="srwi";
} else { die "nonsense $flavour"; }

$sp="r1";
$FRAME=6*$SIZE_T+13*16;	# 13*16 is for v20-v31 offload

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

$code=<<___;
.machine	"any"

.text

.globl	.gcm_init_p8
.align	5
.gcm_init_p8:
	li		r0,-4096
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
___
{
my ($t4,$t5,$t6) = ($Hl,$H,$Hh);
$code.=<<___;
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
.size	.gcm_init_p8,.-.gcm_init_p8
___
}
$code.=<<___;
.globl	.gcm_gmult_p8
.align	5
.gcm_gmult_p8:
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
.align	5
.gcm_ghash_p8:
	li		r0,-4096
	li		r8,0x10
	mfspr		$vrsave,256
	li		r9,0x20
	mtspr		256,r0
	li		r10,0x30
	lvx_u		$Xl,0,$Xip		# load Xi

	lvx_u		$Hl,r8,$Htbl		# load pre-computed table
	li		r8,0x40
	 le?lvsl	$lemask,r0,r0
	lvx_u		$H, r9,$Htbl
	li		r9,0x50
	 le?vspltisb	$t0,0x07
	lvx_u		$Hh,r10,$Htbl
	li		r10,0x60
	 le?vxor	$lemask,$lemask,$t0
	lvx_u		$xC2,0,$Htbl
	 le?vperm	$Xl,$Xl,$Xl,$lemask
	vxor		$zero,$zero,$zero

	${UCMP}i	$len,64
	bge		Lgcm_ghash_p8_4x

	lvx_u		$IN,0,$inp
	addi		$inp,$inp,16
	subic.		$len,$len,16
	 le?vperm	$IN,$IN,$IN,$lemask
	vxor		$IN,$IN,$Xl
	beq		Lshort

	lvx_u		$H2l,r8,$Htbl		# load H^2
	li		r8,16
	lvx_u		$H2, r9,$Htbl
	add		r9,$inp,$len		# end of input
	lvx_u		$H2h,r10,$Htbl
	be?b		Loop_2x

.align	5
Loop_2x:
	lvx_u		$IN1,0,$inp
	le?vperm	$IN1,$IN1,$IN1,$lemask

	 subic		$len,$len,32
	vpmsumd		$Xl,$IN,$H2l		# H^2.lo·Xi.lo
	 vpmsumd	$Xl1,$IN1,$Hl		# H.lo·Xi+1.lo
	 subfe		r0,r0,r0		# borrow?-1:0
	vpmsumd		$Xm,$IN,$H2		# H^2.hi·Xi.lo+H^2.lo·Xi.hi
	 vpmsumd	$Xm1,$IN1,$H		# H.hi·Xi+1.lo+H.lo·Xi+1.hi
	 and		r0,r0,$len
	vpmsumd		$Xh,$IN,$H2h		# H^2.hi·Xi.hi
	 vpmsumd	$Xh1,$IN1,$Hh		# H.hi·Xi+1.hi
	 add		$inp,$inp,r0

	vxor		$Xl,$Xl,$Xl1
	vxor		$Xm,$Xm,$Xm1

	vpmsumd		$t2,$Xl,$xC2		# 1st reduction phase

	vsldoi		$t0,$Xm,$zero,8
	vsldoi		$t1,$zero,$Xm,8
	 vxor		$Xh,$Xh,$Xh1
	vxor		$Xl,$Xl,$t0
	vxor		$Xh,$Xh,$t1

	vsldoi		$Xl,$Xl,$Xl,8
	vxor		$Xl,$Xl,$t2
	 lvx_u		$IN,r8,$inp
	 addi		$inp,$inp,32

	vsldoi		$t1,$Xl,$Xl,8		# 2nd reduction phase
	vpmsumd		$Xl,$Xl,$xC2
	 le?vperm	$IN,$IN,$IN,$lemask
	vxor		$t1,$t1,$Xh
	vxor		$IN,$IN,$t1
	vxor		$IN,$IN,$Xl
	$UCMP		r9,$inp
	bgt		Loop_2x			# done yet?

	cmplwi		$len,0
	bne		Leven

Lshort:
	vpmsumd		$Xl,$IN,$Hl		# H.lo·Xi.lo
	vpmsumd		$Xm,$IN,$H		# H.hi·Xi.lo+H.lo·Xi.hi
	vpmsumd		$Xh,$IN,$Hh		# H.hi·Xi.hi

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

Leven:
	vxor		$Xl,$Xl,$t1
	le?vperm	$Xl,$Xl,$Xl,$lemask
	stvx_u		$Xl,0,$Xip		# write out Xi

	mtspr		256,$vrsave
	blr
	.long		0
	.byte		0,12,0x14,0,0,0,4,0
	.long		0
___
{
my ($Xl3,$Xm2,$IN2,$H3l,$H3,$H3h,
    $Xh3,$Xm3,$IN3,$H4l,$H4,$H4h) = map("v$_",(20..31));
my $IN0=$IN;
my ($H21l,$H21h,$loperm,$hiperm) = ($Hl,$Hh,$H2l,$H2h);

$code.=<<___;
.align	5
.gcm_ghash_p8_4x:
Lgcm_ghash_p8_4x:
	$STU		$sp,-$FRAME($sp)
	li		r10,`15+6*$SIZE_T`
	li		r11,`31+6*$SIZE_T`
	stvx		v20,r10,$sp
	addi		r10,r10,32
	stvx		v21,r11,$sp
	addi		r11,r11,32
	stvx		v22,r10,$sp
	addi		r10,r10,32
	stvx		v23,r11,$sp
	addi		r11,r11,32
	stvx		v24,r10,$sp
	addi		r10,r10,32
	stvx		v25,r11,$sp
	addi		r11,r11,32
	stvx		v26,r10,$sp
	addi		r10,r10,32
	stvx		v27,r11,$sp
	addi		r11,r11,32
	stvx		v28,r10,$sp
	addi		r10,r10,32
	stvx		v29,r11,$sp
	addi		r11,r11,32
	stvx		v30,r10,$sp
	li		r10,0x60
	stvx		v31,r11,$sp
	li		r0,-1
	stw		$vrsave,`$FRAME-4`($sp)	# save vrsave
	mtspr		256,r0			# preserve all AltiVec registers

	lvsl		$t0,0,r8		# 0x0001..0e0f
	#lvx_u		$H2l,r8,$Htbl		# load H^2
	li		r8,0x70
	lvx_u		$H2, r9,$Htbl
	li		r9,0x80
	vspltisb	$t1,8			# 0x0808..0808
	#lvx_u		$H2h,r10,$Htbl
	li		r10,0x90
	lvx_u		$H3l,r8,$Htbl		# load H^3
	li		r8,0xa0
	lvx_u		$H3, r9,$Htbl
	li		r9,0xb0
	lvx_u		$H3h,r10,$Htbl
	li		r10,0xc0
	lvx_u		$H4l,r8,$Htbl		# load H^4
	li		r8,0x10
	lvx_u		$H4, r9,$Htbl
	li		r9,0x20
	lvx_u		$H4h,r10,$Htbl
	li		r10,0x30

	vsldoi		$t2,$zero,$t1,8		# 0x0000..0808
	vaddubm		$hiperm,$t0,$t2		# 0x0001..1617
	vaddubm		$loperm,$t1,$hiperm	# 0x0809..1e1f

	$SHRI		$len,$len,4		# this allows to use sign bit
						# as carry
	lvx_u		$IN0,0,$inp		# load input
	lvx_u		$IN1,r8,$inp
	subic.		$len,$len,8
	lvx_u		$IN2,r9,$inp
	lvx_u		$IN3,r10,$inp
	addi		$inp,$inp,0x40
	le?vperm	$IN0,$IN0,$IN0,$lemask
	le?vperm	$IN1,$IN1,$IN1,$lemask
	le?vperm	$IN2,$IN2,$IN2,$lemask
	le?vperm	$IN3,$IN3,$IN3,$lemask

	vxor		$Xh,$IN0,$Xl

	 vpmsumd	$Xl1,$IN1,$H3l
	 vpmsumd	$Xm1,$IN1,$H3
	 vpmsumd	$Xh1,$IN1,$H3h

	 vperm		$H21l,$H2,$H,$hiperm
	 vperm		$t0,$IN2,$IN3,$loperm
	 vperm		$H21h,$H2,$H,$loperm
	 vperm		$t1,$IN2,$IN3,$hiperm
	 vpmsumd	$Xm2,$IN2,$H2		# H^2.lo·Xi+2.hi+H^2.hi·Xi+2.lo
	 vpmsumd	$Xl3,$t0,$H21l		# H^2.lo·Xi+2.lo+H.lo·Xi+3.lo
	 vpmsumd	$Xm3,$IN3,$H		# H.hi·Xi+3.lo  +H.lo·Xi+3.hi
	 vpmsumd	$Xh3,$t1,$H21h		# H^2.hi·Xi+2.hi+H.hi·Xi+3.hi

	 vxor		$Xm2,$Xm2,$Xm1
	 vxor		$Xl3,$Xl3,$Xl1
	 vxor		$Xm3,$Xm3,$Xm2
	 vxor		$Xh3,$Xh3,$Xh1

	blt		Ltail_4x

Loop_4x:
	lvx_u		$IN0,0,$inp
	lvx_u		$IN1,r8,$inp
	subic.		$len,$len,4
	lvx_u		$IN2,r9,$inp
	lvx_u		$IN3,r10,$inp
	addi		$inp,$inp,0x40
	le?vperm	$IN1,$IN1,$IN1,$lemask
	le?vperm	$IN2,$IN2,$IN2,$lemask
	le?vperm	$IN3,$IN3,$IN3,$lemask
	le?vperm	$IN0,$IN0,$IN0,$lemask

	vpmsumd		$Xl,$Xh,$H4l		# H^4.lo·Xi.lo
	vpmsumd		$Xm,$Xh,$H4		# H^4.hi·Xi.lo+H^4.lo·Xi.hi
	vpmsumd		$Xh,$Xh,$H4h		# H^4.hi·Xi.hi
	 vpmsumd	$Xl1,$IN1,$H3l
	 vpmsumd	$Xm1,$IN1,$H3
	 vpmsumd	$Xh1,$IN1,$H3h

	vxor		$Xl,$Xl,$Xl3
	vxor		$Xm,$Xm,$Xm3
	vxor		$Xh,$Xh,$Xh3
	 vperm		$t0,$IN2,$IN3,$loperm
	 vperm		$t1,$IN2,$IN3,$hiperm

	vpmsumd		$t2,$Xl,$xC2		# 1st reduction phase
	 vpmsumd	$Xl3,$t0,$H21l		# H.lo·Xi+3.lo  +H^2.lo·Xi+2.lo
	 vpmsumd	$Xh3,$t1,$H21h		# H.hi·Xi+3.hi  +H^2.hi·Xi+2.hi

	vsldoi		$t0,$Xm,$zero,8
	vsldoi		$t1,$zero,$Xm,8
	vxor		$Xl,$Xl,$t0
	vxor		$Xh,$Xh,$t1

	vsldoi		$Xl,$Xl,$Xl,8
	vxor		$Xl,$Xl,$t2

	vsldoi		$t1,$Xl,$Xl,8		# 2nd reduction phase
	 vpmsumd	$Xm2,$IN2,$H2		# H^2.hi·Xi+2.lo+H^2.lo·Xi+2.hi
	 vpmsumd	$Xm3,$IN3,$H		# H.hi·Xi+3.lo  +H.lo·Xi+3.hi
	vpmsumd		$Xl,$Xl,$xC2

	 vxor		$Xl3,$Xl3,$Xl1
	 vxor		$Xh3,$Xh3,$Xh1
	vxor		$Xh,$Xh,$IN0
	 vxor		$Xm2,$Xm2,$Xm1
	vxor		$Xh,$Xh,$t1
	 vxor		$Xm3,$Xm3,$Xm2
	vxor		$Xh,$Xh,$Xl
	bge		Loop_4x

Ltail_4x:
	vpmsumd		$Xl,$Xh,$H4l		# H^4.lo·Xi.lo
	vpmsumd		$Xm,$Xh,$H4		# H^4.hi·Xi.lo+H^4.lo·Xi.hi
	vpmsumd		$Xh,$Xh,$H4h		# H^4.hi·Xi.hi

	vxor		$Xl,$Xl,$Xl3
	vxor		$Xm,$Xm,$Xm3

	vpmsumd		$t2,$Xl,$xC2		# 1st reduction phase

	vsldoi		$t0,$Xm,$zero,8
	vsldoi		$t1,$zero,$Xm,8
	 vxor		$Xh,$Xh,$Xh3
	vxor		$Xl,$Xl,$t0
	vxor		$Xh,$Xh,$t1

	vsldoi		$Xl,$Xl,$Xl,8
	vxor		$Xl,$Xl,$t2

	vsldoi		$t1,$Xl,$Xl,8		# 2nd reduction phase
	vpmsumd		$Xl,$Xl,$xC2
	vxor		$t1,$t1,$Xh
	vxor		$Xl,$Xl,$t1

	addic.		$len,$len,4
	beq		Ldone_4x

	lvx_u		$IN0,0,$inp
	${UCMP}i	$len,2
	li		$len,-4
	blt		Lone
	lvx_u		$IN1,r8,$inp
	beq		Ltwo

Lthree:
	lvx_u		$IN2,r9,$inp
	le?vperm	$IN0,$IN0,$IN0,$lemask
	le?vperm	$IN1,$IN1,$IN1,$lemask
	le?vperm	$IN2,$IN2,$IN2,$lemask

	vxor		$Xh,$IN0,$Xl
	vmr		$H4l,$H3l
	vmr		$H4, $H3
	vmr		$H4h,$H3h

	vperm		$t0,$IN1,$IN2,$loperm
	vperm		$t1,$IN1,$IN2,$hiperm
	vpmsumd		$Xm2,$IN1,$H2		# H^2.lo·Xi+1.hi+H^2.hi·Xi+1.lo
	vpmsumd		$Xm3,$IN2,$H		# H.hi·Xi+2.lo  +H.lo·Xi+2.hi
	vpmsumd		$Xl3,$t0,$H21l		# H^2.lo·Xi+1.lo+H.lo·Xi+2.lo
	vpmsumd		$Xh3,$t1,$H21h		# H^2.hi·Xi+1.hi+H.hi·Xi+2.hi

	vxor		$Xm3,$Xm3,$Xm2
	b		Ltail_4x

.align	4
Ltwo:
	le?vperm	$IN0,$IN0,$IN0,$lemask
	le?vperm	$IN1,$IN1,$IN1,$lemask

	vxor		$Xh,$IN0,$Xl
	vperm		$t0,$zero,$IN1,$loperm
	vperm		$t1,$zero,$IN1,$hiperm

	vsldoi		$H4l,$zero,$H2,8
	vmr		$H4, $H2
	vsldoi		$H4h,$H2,$zero,8

	vpmsumd		$Xl3,$t0, $H21l		# H.lo·Xi+1.lo
	vpmsumd		$Xm3,$IN1,$H		# H.hi·Xi+1.lo+H.lo·Xi+2.hi
	vpmsumd		$Xh3,$t1, $H21h		# H.hi·Xi+1.hi

	b		Ltail_4x

.align	4
Lone:
	le?vperm	$IN0,$IN0,$IN0,$lemask

	vsldoi		$H4l,$zero,$H,8
	vmr		$H4, $H
	vsldoi		$H4h,$H,$zero,8

	vxor		$Xh,$IN0,$Xl
	vxor		$Xl3,$Xl3,$Xl3
	vxor		$Xm3,$Xm3,$Xm3
	vxor		$Xh3,$Xh3,$Xh3

	b		Ltail_4x

Ldone_4x:
	le?vperm	$Xl,$Xl,$Xl,$lemask
	stvx_u		$Xl,0,$Xip		# write out Xi

	li		r10,`15+6*$SIZE_T`
	li		r11,`31+6*$SIZE_T`
	mtspr		256,$vrsave
	lvx		v20,r10,$sp
	addi		r10,r10,32
	lvx		v21,r11,$sp
	addi		r11,r11,32
	lvx		v22,r10,$sp
	addi		r10,r10,32
	lvx		v23,r11,$sp
	addi		r11,r11,32
	lvx		v24,r10,$sp
	addi		r10,r10,32
	lvx		v25,r11,$sp
	addi		r11,r11,32
	lvx		v26,r10,$sp
	addi		r10,r10,32
	lvx		v27,r11,$sp
	addi		r11,r11,32
	lvx		v28,r10,$sp
	addi		r10,r10,32
	lvx		v29,r11,$sp
	addi		r11,r11,32
	lvx		v30,r10,$sp
	lvx		v31,r11,$sp
	addi		$sp,$sp,$FRAME
	blr
	.long		0
	.byte		0,12,0x04,0,0x80,0,4,0
	.long		0
___
}
$code.=<<___;
.size	.gcm_ghash_p8,.-.gcm_ghash_p8

.asciz  "GHASH for PowerISA 2.07, CRYPTOGAMS by <appro\@openssl.org>"
.align  2
___

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/geo;

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

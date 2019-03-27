#! /usr/bin/env perl
# Copyright 2010-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project.
#
# Rights for redistribution and usage in source and binary forms are
# granted according to the OpenSSL license. Warranty of any kind is
# disclaimed.
# ====================================================================


# July 1999
#
# This is drop-in MIPS III/IV ISA replacement for crypto/bn/bn_asm.c.
#
# The module is designed to work with either of the "new" MIPS ABI(5),
# namely N32 or N64, offered by IRIX 6.x. It's not meant to work under
# IRIX 5.x not only because it doesn't support new ABIs but also
# because 5.x kernels put R4x00 CPU into 32-bit mode and all those
# 64-bit instructions (daddu, dmultu, etc.) found below gonna only
# cause illegal instruction exception:-(
#
# In addition the code depends on preprocessor flags set up by MIPSpro
# compiler driver (either as or cc) and therefore (probably?) can't be
# compiled by the GNU assembler. GNU C driver manages fine though...
# I mean as long as -mmips-as is specified or is the default option,
# because then it simply invokes /usr/bin/as which in turn takes
# perfect care of the preprocessor definitions. Another neat feature
# offered by the MIPSpro assembler is an optimization pass. This gave
# me the opportunity to have the code looking more regular as all those
# architecture dependent instruction rescheduling details were left to
# the assembler. Cool, huh?
#
# Performance improvement is astonishing! 'apps/openssl speed rsa dsa'
# goes way over 3 times faster!
#
#					<appro@openssl.org>

# October 2010
#
# Adapt the module even for 32-bit ABIs and other OSes. The former was
# achieved by mechanical replacement of 64-bit arithmetic instructions
# such as dmultu, daddu, etc. with their 32-bit counterparts and
# adjusting offsets denoting multiples of BN_ULONG. Above mentioned
# >3x performance improvement naturally does not apply to 32-bit code
# [because there is no instruction 32-bit compiler can't use], one
# has to content with 40-85% improvement depending on benchmark and
# key length, more for longer keys.

$flavour = shift || "o32";
while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

if ($flavour =~ /64|n32/i) {
	$LD="ld";
	$ST="sd";
	$MULTU="dmultu";
	$DIVU="ddivu";
	$ADDU="daddu";
	$SUBU="dsubu";
	$SRL="dsrl";
	$SLL="dsll";
	$BNSZ=8;
	$PTR_ADD="daddu";
	$PTR_SUB="dsubu";
	$SZREG=8;
	$REG_S="sd";
	$REG_L="ld";
} else {
	$LD="lw";
	$ST="sw";
	$MULTU="multu";
	$DIVU="divu";
	$ADDU="addu";
	$SUBU="subu";
	$SRL="srl";
	$SLL="sll";
	$BNSZ=4;
	$PTR_ADD="addu";
	$PTR_SUB="subu";
	$SZREG=4;
	$REG_S="sw";
	$REG_L="lw";
	$code=".set	mips2\n";
}

# Below is N32/64 register layout used in the original module.
#
($zero,$at,$v0,$v1)=map("\$$_",(0..3));
($a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7)=map("\$$_",(4..11));
($t0,$t1,$t2,$t3,$t8,$t9)=map("\$$_",(12..15,24,25));
($s0,$s1,$s2,$s3,$s4,$s5,$s6,$s7)=map("\$$_",(16..23));
($gp,$sp,$fp,$ra)=map("\$$_",(28..31));
($ta0,$ta1,$ta2,$ta3)=($a4,$a5,$a6,$a7);
#
# No special adaptation is required for O32. NUBI on the other hand
# is treated by saving/restoring ($v1,$t0..$t3).

$gp=$v1 if ($flavour =~ /nubi/i);

$minus4=$v1;

$code.=<<___;
#include "mips_arch.h"

#if defined(_MIPS_ARCH_MIPS64R6)
# define ddivu(rs,rt)
# define mfqt(rd,rs,rt)	ddivu	rd,rs,rt
# define mfrm(rd,rs,rt)	dmodu	rd,rs,rt
#elif defined(_MIPS_ARCH_MIPS32R6)
# define divu(rs,rt)
# define mfqt(rd,rs,rt)	divu	rd,rs,rt
# define mfrm(rd,rs,rt)	modu	rd,rs,rt
#else
# define $DIVU(rs,rt)	$DIVU	$zero,rs,rt
# define mfqt(rd,rs,rt)	mflo	rd
# define mfrm(rd,rs,rt)	mfhi	rd
#endif

.rdata
.asciiz	"mips3.s, Version 1.2"
.asciiz	"MIPS II/III/IV ISA artwork by Andy Polyakov <appro\@fy.chalmers.se>"

.text
.set	noat

.align	5
.globl	bn_mul_add_words
.ent	bn_mul_add_words
bn_mul_add_words:
	.set	noreorder
	bgtz	$a2,bn_mul_add_words_internal
	move	$v0,$zero
	jr	$ra
	move	$a0,$v0
.end	bn_mul_add_words

.align	5
.ent	bn_mul_add_words_internal
bn_mul_add_words_internal:
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x8000f008,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$ra,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___;
	.set	reorder
	li	$minus4,-4
	and	$ta0,$a2,$minus4
	beqz	$ta0,.L_bn_mul_add_words_tail

.L_bn_mul_add_words_loop:
	$LD	$t0,0($a1)
	$MULTU	($t0,$a3)
	$LD	$t1,0($a0)
	$LD	$t2,$BNSZ($a1)
	$LD	$t3,$BNSZ($a0)
	$LD	$ta0,2*$BNSZ($a1)
	$LD	$ta1,2*$BNSZ($a0)
	$ADDU	$t1,$v0
	sltu	$v0,$t1,$v0	# All manuals say it "compares 32-bit
				# values", but it seems to work fine
				# even on 64-bit registers.
	mflo	($at,$t0,$a3)
	mfhi	($t0,$t0,$a3)
	$ADDU	$t1,$at
	$ADDU	$v0,$t0
	 $MULTU	($t2,$a3)
	sltu	$at,$t1,$at
	$ST	$t1,0($a0)
	$ADDU	$v0,$at

	$LD	$ta2,3*$BNSZ($a1)
	$LD	$ta3,3*$BNSZ($a0)
	$ADDU	$t3,$v0
	sltu	$v0,$t3,$v0
	mflo	($at,$t2,$a3)
	mfhi	($t2,$t2,$a3)
	$ADDU	$t3,$at
	$ADDU	$v0,$t2
	 $MULTU	($ta0,$a3)
	sltu	$at,$t3,$at
	$ST	$t3,$BNSZ($a0)
	$ADDU	$v0,$at

	subu	$a2,4
	$PTR_ADD $a0,4*$BNSZ
	$PTR_ADD $a1,4*$BNSZ
	$ADDU	$ta1,$v0
	sltu	$v0,$ta1,$v0
	mflo	($at,$ta0,$a3)
	mfhi	($ta0,$ta0,$a3)
	$ADDU	$ta1,$at
	$ADDU	$v0,$ta0
	 $MULTU	($ta2,$a3)
	sltu	$at,$ta1,$at
	$ST	$ta1,-2*$BNSZ($a0)
	$ADDU	$v0,$at


	and	$ta0,$a2,$minus4
	$ADDU	$ta3,$v0
	sltu	$v0,$ta3,$v0
	mflo	($at,$ta2,$a3)
	mfhi	($ta2,$ta2,$a3)
	$ADDU	$ta3,$at
	$ADDU	$v0,$ta2
	sltu	$at,$ta3,$at
	$ST	$ta3,-$BNSZ($a0)
	.set	noreorder
	bgtz	$ta0,.L_bn_mul_add_words_loop
	$ADDU	$v0,$at

	beqz	$a2,.L_bn_mul_add_words_return
	nop

.L_bn_mul_add_words_tail:
	.set	reorder
	$LD	$t0,0($a1)
	$MULTU	($t0,$a3)
	$LD	$t1,0($a0)
	subu	$a2,1
	$ADDU	$t1,$v0
	sltu	$v0,$t1,$v0
	mflo	($at,$t0,$a3)
	mfhi	($t0,$t0,$a3)
	$ADDU	$t1,$at
	$ADDU	$v0,$t0
	sltu	$at,$t1,$at
	$ST	$t1,0($a0)
	$ADDU	$v0,$at
	beqz	$a2,.L_bn_mul_add_words_return

	$LD	$t0,$BNSZ($a1)
	$MULTU	($t0,$a3)
	$LD	$t1,$BNSZ($a0)
	subu	$a2,1
	$ADDU	$t1,$v0
	sltu	$v0,$t1,$v0
	mflo	($at,$t0,$a3)
	mfhi	($t0,$t0,$a3)
	$ADDU	$t1,$at
	$ADDU	$v0,$t0
	sltu	$at,$t1,$at
	$ST	$t1,$BNSZ($a0)
	$ADDU	$v0,$at
	beqz	$a2,.L_bn_mul_add_words_return

	$LD	$t0,2*$BNSZ($a1)
	$MULTU	($t0,$a3)
	$LD	$t1,2*$BNSZ($a0)
	$ADDU	$t1,$v0
	sltu	$v0,$t1,$v0
	mflo	($at,$t0,$a3)
	mfhi	($t0,$t0,$a3)
	$ADDU	$t1,$at
	$ADDU	$v0,$t0
	sltu	$at,$t1,$at
	$ST	$t1,2*$BNSZ($a0)
	$ADDU	$v0,$at

.L_bn_mul_add_words_return:
	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
	jr	$ra
	move	$a0,$v0
.end	bn_mul_add_words_internal

.align	5
.globl	bn_mul_words
.ent	bn_mul_words
bn_mul_words:
	.set	noreorder
	bgtz	$a2,bn_mul_words_internal
	move	$v0,$zero
	jr	$ra
	move	$a0,$v0
.end	bn_mul_words

.align	5
.ent	bn_mul_words_internal
bn_mul_words_internal:
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x8000f008,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$ra,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___;
	.set	reorder
	li	$minus4,-4
	and	$ta0,$a2,$minus4
	beqz	$ta0,.L_bn_mul_words_tail

.L_bn_mul_words_loop:
	$LD	$t0,0($a1)
	$MULTU	($t0,$a3)
	$LD	$t2,$BNSZ($a1)
	$LD	$ta0,2*$BNSZ($a1)
	$LD	$ta2,3*$BNSZ($a1)
	mflo	($at,$t0,$a3)
	mfhi	($t0,$t0,$a3)
	$ADDU	$v0,$at
	sltu	$t1,$v0,$at
	 $MULTU	($t2,$a3)
	$ST	$v0,0($a0)
	$ADDU	$v0,$t1,$t0

	subu	$a2,4
	$PTR_ADD $a0,4*$BNSZ
	$PTR_ADD $a1,4*$BNSZ
	mflo	($at,$t2,$a3)
	mfhi	($t2,$t2,$a3)
	$ADDU	$v0,$at
	sltu	$t3,$v0,$at
	 $MULTU	($ta0,$a3)
	$ST	$v0,-3*$BNSZ($a0)
	$ADDU	$v0,$t3,$t2

	mflo	($at,$ta0,$a3)
	mfhi	($ta0,$ta0,$a3)
	$ADDU	$v0,$at
	sltu	$ta1,$v0,$at
	 $MULTU	($ta2,$a3)
	$ST	$v0,-2*$BNSZ($a0)
	$ADDU	$v0,$ta1,$ta0

	and	$ta0,$a2,$minus4
	mflo	($at,$ta2,$a3)
	mfhi	($ta2,$ta2,$a3)
	$ADDU	$v0,$at
	sltu	$ta3,$v0,$at
	$ST	$v0,-$BNSZ($a0)
	.set	noreorder
	bgtz	$ta0,.L_bn_mul_words_loop
	$ADDU	$v0,$ta3,$ta2

	beqz	$a2,.L_bn_mul_words_return
	nop

.L_bn_mul_words_tail:
	.set	reorder
	$LD	$t0,0($a1)
	$MULTU	($t0,$a3)
	subu	$a2,1
	mflo	($at,$t0,$a3)
	mfhi	($t0,$t0,$a3)
	$ADDU	$v0,$at
	sltu	$t1,$v0,$at
	$ST	$v0,0($a0)
	$ADDU	$v0,$t1,$t0
	beqz	$a2,.L_bn_mul_words_return

	$LD	$t0,$BNSZ($a1)
	$MULTU	($t0,$a3)
	subu	$a2,1
	mflo	($at,$t0,$a3)
	mfhi	($t0,$t0,$a3)
	$ADDU	$v0,$at
	sltu	$t1,$v0,$at
	$ST	$v0,$BNSZ($a0)
	$ADDU	$v0,$t1,$t0
	beqz	$a2,.L_bn_mul_words_return

	$LD	$t0,2*$BNSZ($a1)
	$MULTU	($t0,$a3)
	mflo	($at,$t0,$a3)
	mfhi	($t0,$t0,$a3)
	$ADDU	$v0,$at
	sltu	$t1,$v0,$at
	$ST	$v0,2*$BNSZ($a0)
	$ADDU	$v0,$t1,$t0

.L_bn_mul_words_return:
	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
	jr	$ra
	move	$a0,$v0
.end	bn_mul_words_internal

.align	5
.globl	bn_sqr_words
.ent	bn_sqr_words
bn_sqr_words:
	.set	noreorder
	bgtz	$a2,bn_sqr_words_internal
	move	$v0,$zero
	jr	$ra
	move	$a0,$v0
.end	bn_sqr_words

.align	5
.ent	bn_sqr_words_internal
bn_sqr_words_internal:
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x8000f008,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$ra,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___;
	.set	reorder
	li	$minus4,-4
	and	$ta0,$a2,$minus4
	beqz	$ta0,.L_bn_sqr_words_tail

.L_bn_sqr_words_loop:
	$LD	$t0,0($a1)
	$MULTU	($t0,$t0)
	$LD	$t2,$BNSZ($a1)
	$LD	$ta0,2*$BNSZ($a1)
	$LD	$ta2,3*$BNSZ($a1)
	mflo	($t1,$t0,$t0)
	mfhi	($t0,$t0,$t0)
	$ST	$t1,0($a0)
	$ST	$t0,$BNSZ($a0)

	$MULTU	($t2,$t2)
	subu	$a2,4
	$PTR_ADD $a0,8*$BNSZ
	$PTR_ADD $a1,4*$BNSZ
	mflo	($t3,$t2,$t2)
	mfhi	($t2,$t2,$t2)
	$ST	$t3,-6*$BNSZ($a0)
	$ST	$t2,-5*$BNSZ($a0)

	$MULTU	($ta0,$ta0)
	mflo	($ta1,$ta0,$ta0)
	mfhi	($ta0,$ta0,$ta0)
	$ST	$ta1,-4*$BNSZ($a0)
	$ST	$ta0,-3*$BNSZ($a0)


	$MULTU	($ta2,$ta2)
	and	$ta0,$a2,$minus4
	mflo	($ta3,$ta2,$ta2)
	mfhi	($ta2,$ta2,$ta2)
	$ST	$ta3,-2*$BNSZ($a0)

	.set	noreorder
	bgtz	$ta0,.L_bn_sqr_words_loop
	$ST	$ta2,-$BNSZ($a0)

	beqz	$a2,.L_bn_sqr_words_return
	nop

.L_bn_sqr_words_tail:
	.set	reorder
	$LD	$t0,0($a1)
	$MULTU	($t0,$t0)
	subu	$a2,1
	mflo	($t1,$t0,$t0)
	mfhi	($t0,$t0,$t0)
	$ST	$t1,0($a0)
	$ST	$t0,$BNSZ($a0)
	beqz	$a2,.L_bn_sqr_words_return

	$LD	$t0,$BNSZ($a1)
	$MULTU	($t0,$t0)
	subu	$a2,1
	mflo	($t1,$t0,$t0)
	mfhi	($t0,$t0,$t0)
	$ST	$t1,2*$BNSZ($a0)
	$ST	$t0,3*$BNSZ($a0)
	beqz	$a2,.L_bn_sqr_words_return

	$LD	$t0,2*$BNSZ($a1)
	$MULTU	($t0,$t0)
	mflo	($t1,$t0,$t0)
	mfhi	($t0,$t0,$t0)
	$ST	$t1,4*$BNSZ($a0)
	$ST	$t0,5*$BNSZ($a0)

.L_bn_sqr_words_return:
	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
	jr	$ra
	move	$a0,$v0

.end	bn_sqr_words_internal

.align	5
.globl	bn_add_words
.ent	bn_add_words
bn_add_words:
	.set	noreorder
	bgtz	$a3,bn_add_words_internal
	move	$v0,$zero
	jr	$ra
	move	$a0,$v0
.end	bn_add_words

.align	5
.ent	bn_add_words_internal
bn_add_words_internal:
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x8000f008,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$ra,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___;
	.set	reorder
	li	$minus4,-4
	and	$at,$a3,$minus4
	beqz	$at,.L_bn_add_words_tail

.L_bn_add_words_loop:
	$LD	$t0,0($a1)
	$LD	$ta0,0($a2)
	subu	$a3,4
	$LD	$t1,$BNSZ($a1)
	and	$at,$a3,$minus4
	$LD	$t2,2*$BNSZ($a1)
	$PTR_ADD $a2,4*$BNSZ
	$LD	$t3,3*$BNSZ($a1)
	$PTR_ADD $a0,4*$BNSZ
	$LD	$ta1,-3*$BNSZ($a2)
	$PTR_ADD $a1,4*$BNSZ
	$LD	$ta2,-2*$BNSZ($a2)
	$LD	$ta3,-$BNSZ($a2)
	$ADDU	$ta0,$t0
	sltu	$t8,$ta0,$t0
	$ADDU	$t0,$ta0,$v0
	sltu	$v0,$t0,$ta0
	$ST	$t0,-4*$BNSZ($a0)
	$ADDU	$v0,$t8

	$ADDU	$ta1,$t1
	sltu	$t9,$ta1,$t1
	$ADDU	$t1,$ta1,$v0
	sltu	$v0,$t1,$ta1
	$ST	$t1,-3*$BNSZ($a0)
	$ADDU	$v0,$t9

	$ADDU	$ta2,$t2
	sltu	$t8,$ta2,$t2
	$ADDU	$t2,$ta2,$v0
	sltu	$v0,$t2,$ta2
	$ST	$t2,-2*$BNSZ($a0)
	$ADDU	$v0,$t8

	$ADDU	$ta3,$t3
	sltu	$t9,$ta3,$t3
	$ADDU	$t3,$ta3,$v0
	sltu	$v0,$t3,$ta3
	$ST	$t3,-$BNSZ($a0)

	.set	noreorder
	bgtz	$at,.L_bn_add_words_loop
	$ADDU	$v0,$t9

	beqz	$a3,.L_bn_add_words_return
	nop

.L_bn_add_words_tail:
	.set	reorder
	$LD	$t0,0($a1)
	$LD	$ta0,0($a2)
	$ADDU	$ta0,$t0
	subu	$a3,1
	sltu	$t8,$ta0,$t0
	$ADDU	$t0,$ta0,$v0
	sltu	$v0,$t0,$ta0
	$ST	$t0,0($a0)
	$ADDU	$v0,$t8
	beqz	$a3,.L_bn_add_words_return

	$LD	$t1,$BNSZ($a1)
	$LD	$ta1,$BNSZ($a2)
	$ADDU	$ta1,$t1
	subu	$a3,1
	sltu	$t9,$ta1,$t1
	$ADDU	$t1,$ta1,$v0
	sltu	$v0,$t1,$ta1
	$ST	$t1,$BNSZ($a0)
	$ADDU	$v0,$t9
	beqz	$a3,.L_bn_add_words_return

	$LD	$t2,2*$BNSZ($a1)
	$LD	$ta2,2*$BNSZ($a2)
	$ADDU	$ta2,$t2
	sltu	$t8,$ta2,$t2
	$ADDU	$t2,$ta2,$v0
	sltu	$v0,$t2,$ta2
	$ST	$t2,2*$BNSZ($a0)
	$ADDU	$v0,$t8

.L_bn_add_words_return:
	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
	jr	$ra
	move	$a0,$v0

.end	bn_add_words_internal

.align	5
.globl	bn_sub_words
.ent	bn_sub_words
bn_sub_words:
	.set	noreorder
	bgtz	$a3,bn_sub_words_internal
	move	$v0,$zero
	jr	$ra
	move	$a0,$zero
.end	bn_sub_words

.align	5
.ent	bn_sub_words_internal
bn_sub_words_internal:
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x8000f008,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$ra,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___;
	.set	reorder
	li	$minus4,-4
	and	$at,$a3,$minus4
	beqz	$at,.L_bn_sub_words_tail

.L_bn_sub_words_loop:
	$LD	$t0,0($a1)
	$LD	$ta0,0($a2)
	subu	$a3,4
	$LD	$t1,$BNSZ($a1)
	and	$at,$a3,$minus4
	$LD	$t2,2*$BNSZ($a1)
	$PTR_ADD $a2,4*$BNSZ
	$LD	$t3,3*$BNSZ($a1)
	$PTR_ADD $a0,4*$BNSZ
	$LD	$ta1,-3*$BNSZ($a2)
	$PTR_ADD $a1,4*$BNSZ
	$LD	$ta2,-2*$BNSZ($a2)
	$LD	$ta3,-$BNSZ($a2)
	sltu	$t8,$t0,$ta0
	$SUBU	$ta0,$t0,$ta0
	$SUBU	$t0,$ta0,$v0
	sgtu	$v0,$t0,$ta0
	$ST	$t0,-4*$BNSZ($a0)
	$ADDU	$v0,$t8

	sltu	$t9,$t1,$ta1
	$SUBU	$ta1,$t1,$ta1
	$SUBU	$t1,$ta1,$v0
	sgtu	$v0,$t1,$ta1
	$ST	$t1,-3*$BNSZ($a0)
	$ADDU	$v0,$t9


	sltu	$t8,$t2,$ta2
	$SUBU	$ta2,$t2,$ta2
	$SUBU	$t2,$ta2,$v0
	sgtu	$v0,$t2,$ta2
	$ST	$t2,-2*$BNSZ($a0)
	$ADDU	$v0,$t8

	sltu	$t9,$t3,$ta3
	$SUBU	$ta3,$t3,$ta3
	$SUBU	$t3,$ta3,$v0
	sgtu	$v0,$t3,$ta3
	$ST	$t3,-$BNSZ($a0)

	.set	noreorder
	bgtz	$at,.L_bn_sub_words_loop
	$ADDU	$v0,$t9

	beqz	$a3,.L_bn_sub_words_return
	nop

.L_bn_sub_words_tail:
	.set	reorder
	$LD	$t0,0($a1)
	$LD	$ta0,0($a2)
	subu	$a3,1
	sltu	$t8,$t0,$ta0
	$SUBU	$ta0,$t0,$ta0
	$SUBU	$t0,$ta0,$v0
	sgtu	$v0,$t0,$ta0
	$ST	$t0,0($a0)
	$ADDU	$v0,$t8
	beqz	$a3,.L_bn_sub_words_return

	$LD	$t1,$BNSZ($a1)
	subu	$a3,1
	$LD	$ta1,$BNSZ($a2)
	sltu	$t9,$t1,$ta1
	$SUBU	$ta1,$t1,$ta1
	$SUBU	$t1,$ta1,$v0
	sgtu	$v0,$t1,$ta1
	$ST	$t1,$BNSZ($a0)
	$ADDU	$v0,$t9
	beqz	$a3,.L_bn_sub_words_return

	$LD	$t2,2*$BNSZ($a1)
	$LD	$ta2,2*$BNSZ($a2)
	sltu	$t8,$t2,$ta2
	$SUBU	$ta2,$t2,$ta2
	$SUBU	$t2,$ta2,$v0
	sgtu	$v0,$t2,$ta2
	$ST	$t2,2*$BNSZ($a0)
	$ADDU	$v0,$t8

.L_bn_sub_words_return:
	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
	jr	$ra
	move	$a0,$v0
.end	bn_sub_words_internal

#if 0
/*
 * The bn_div_3_words entry point is re-used for constant-time interface.
 * Implementation is retained as hystorical reference.
 */
.align 5
.globl	bn_div_3_words
.ent	bn_div_3_words
bn_div_3_words:
	.set	noreorder
	move	$a3,$a0		# we know that bn_div_words does not
				# touch $a3, $ta2, $ta3 and preserves $a2
				# so that we can save two arguments
				# and return address in registers
				# instead of stack:-)

	$LD	$a0,($a3)
	move	$ta2,$a1
	bne	$a0,$a2,bn_div_3_words_internal
	$LD	$a1,-$BNSZ($a3)
	li	$v0,-1
	jr	$ra
	move	$a0,$v0
.end	bn_div_3_words

.align	5
.ent	bn_div_3_words_internal
bn_div_3_words_internal:
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x8000f008,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$ra,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___;
	.set	reorder
	move	$ta3,$ra
	bal	bn_div_words_internal
	move	$ra,$ta3
	$MULTU	($ta2,$v0)
	$LD	$t2,-2*$BNSZ($a3)
	move	$ta0,$zero
	mfhi	($t1,$ta2,$v0)
	mflo	($t0,$ta2,$v0)
	sltu	$t8,$t1,$a1
.L_bn_div_3_words_inner_loop:
	bnez	$t8,.L_bn_div_3_words_inner_loop_done
	sgeu	$at,$t2,$t0
	seq	$t9,$t1,$a1
	and	$at,$t9
	sltu	$t3,$t0,$ta2
	$ADDU	$a1,$a2
	$SUBU	$t1,$t3
	$SUBU	$t0,$ta2
	sltu	$t8,$t1,$a1
	sltu	$ta0,$a1,$a2
	or	$t8,$ta0
	.set	noreorder
	beqz	$at,.L_bn_div_3_words_inner_loop
	$SUBU	$v0,1
	$ADDU	$v0,1
	.set	reorder
.L_bn_div_3_words_inner_loop_done:
	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
	jr	$ra
	move	$a0,$v0
.end	bn_div_3_words_internal
#endif

.align	5
.globl	bn_div_words
.ent	bn_div_words
bn_div_words:
	.set	noreorder
	bnez	$a2,bn_div_words_internal
	li	$v0,-1		# I would rather signal div-by-zero
				# which can be done with 'break 7'
	jr	$ra
	move	$a0,$v0
.end	bn_div_words

.align	5
.ent	bn_div_words_internal
bn_div_words_internal:
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x8000f008,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$ra,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___;
	move	$v1,$zero
	bltz	$a2,.L_bn_div_words_body
	move	$t9,$v1
	$SLL	$a2,1
	bgtz	$a2,.-4
	addu	$t9,1

	.set	reorder
	negu	$t1,$t9
	li	$t2,-1
	$SLL	$t2,$t1
	and	$t2,$a0
	$SRL	$at,$a1,$t1
	.set	noreorder
	beqz	$t2,.+12
	nop
	break	6		# signal overflow
	.set	reorder
	$SLL	$a0,$t9
	$SLL	$a1,$t9
	or	$a0,$at
___
$QT=$ta0;
$HH=$ta1;
$DH=$v1;
$code.=<<___;
.L_bn_div_words_body:
	$SRL	$DH,$a2,4*$BNSZ	# bits
	sgeu	$at,$a0,$a2
	.set	noreorder
	beqz	$at,.+12
	nop
	$SUBU	$a0,$a2
	.set	reorder

	li	$QT,-1
	$SRL	$HH,$a0,4*$BNSZ	# bits
	$SRL	$QT,4*$BNSZ	# q=0xffffffff
	beq	$DH,$HH,.L_bn_div_words_skip_div1
	$DIVU	($a0,$DH)
	mfqt	($QT,$a0,$DH)
.L_bn_div_words_skip_div1:
	$MULTU	($a2,$QT)
	$SLL	$t3,$a0,4*$BNSZ	# bits
	$SRL	$at,$a1,4*$BNSZ	# bits
	or	$t3,$at
	mflo	($t0,$a2,$QT)
	mfhi	($t1,$a2,$QT)
.L_bn_div_words_inner_loop1:
	sltu	$t2,$t3,$t0
	seq	$t8,$HH,$t1
	sltu	$at,$HH,$t1
	and	$t2,$t8
	sltu	$v0,$t0,$a2
	or	$at,$t2
	.set	noreorder
	beqz	$at,.L_bn_div_words_inner_loop1_done
	$SUBU	$t1,$v0
	$SUBU	$t0,$a2
	b	.L_bn_div_words_inner_loop1
	$SUBU	$QT,1
	.set	reorder
.L_bn_div_words_inner_loop1_done:

	$SLL	$a1,4*$BNSZ	# bits
	$SUBU	$a0,$t3,$t0
	$SLL	$v0,$QT,4*$BNSZ	# bits

	li	$QT,-1
	$SRL	$HH,$a0,4*$BNSZ	# bits
	$SRL	$QT,4*$BNSZ	# q=0xffffffff
	beq	$DH,$HH,.L_bn_div_words_skip_div2
	$DIVU	($a0,$DH)
	mfqt	($QT,$a0,$DH)
.L_bn_div_words_skip_div2:
	$MULTU	($a2,$QT)
	$SLL	$t3,$a0,4*$BNSZ	# bits
	$SRL	$at,$a1,4*$BNSZ	# bits
	or	$t3,$at
	mflo	($t0,$a2,$QT)
	mfhi	($t1,$a2,$QT)
.L_bn_div_words_inner_loop2:
	sltu	$t2,$t3,$t0
	seq	$t8,$HH,$t1
	sltu	$at,$HH,$t1
	and	$t2,$t8
	sltu	$v1,$t0,$a2
	or	$at,$t2
	.set	noreorder
	beqz	$at,.L_bn_div_words_inner_loop2_done
	$SUBU	$t1,$v1
	$SUBU	$t0,$a2
	b	.L_bn_div_words_inner_loop2
	$SUBU	$QT,1
	.set	reorder
.L_bn_div_words_inner_loop2_done:

	$SUBU	$a0,$t3,$t0
	or	$v0,$QT
	$SRL	$v1,$a0,$t9	# $v1 contains remainder if anybody wants it
	$SRL	$a2,$t9		# restore $a2

	.set	noreorder
	move	$a1,$v1
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
	jr	$ra
	move	$a0,$v0
.end	bn_div_words_internal
___
undef $HH; undef $QT; undef $DH;

($a_0,$a_1,$a_2,$a_3)=($t0,$t1,$t2,$t3);
($b_0,$b_1,$b_2,$b_3)=($ta0,$ta1,$ta2,$ta3);

($a_4,$a_5,$a_6,$a_7)=($s0,$s2,$s4,$a1); # once we load a[7], no use for $a1
($b_4,$b_5,$b_6,$b_7)=($s1,$s3,$s5,$a2); # once we load b[7], no use for $a2

($t_1,$t_2,$c_1,$c_2,$c_3)=($t8,$t9,$v0,$v1,$a3);

$code.=<<___;

.align	5
.globl	bn_mul_comba8
.ent	bn_mul_comba8
bn_mul_comba8:
	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,12*$SZREG,$ra
	.mask	0x803ff008,-$SZREG
	$PTR_SUB $sp,12*$SZREG
	$REG_S	$ra,11*$SZREG($sp)
	$REG_S	$s5,10*$SZREG($sp)
	$REG_S	$s4,9*$SZREG($sp)
	$REG_S	$s3,8*$SZREG($sp)
	$REG_S	$s2,7*$SZREG($sp)
	$REG_S	$s1,6*$SZREG($sp)
	$REG_S	$s0,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___ if ($flavour !~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x003f0000,-$SZREG
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$s5,5*$SZREG($sp)
	$REG_S	$s4,4*$SZREG($sp)
	$REG_S	$s3,3*$SZREG($sp)
	$REG_S	$s2,2*$SZREG($sp)
	$REG_S	$s1,1*$SZREG($sp)
	$REG_S	$s0,0*$SZREG($sp)
___
$code.=<<___;

	.set	reorder
	$LD	$a_0,0($a1)	# If compiled with -mips3 option on
				# R5000 box assembler barks on this
				# 1ine with "should not have mult/div
				# as last instruction in bb (R10K
				# bug)" warning. If anybody out there
				# has a clue about how to circumvent
				# this do send me a note.
				#		<appro\@fy.chalmers.se>

	$LD	$b_0,0($a2)
	$LD	$a_1,$BNSZ($a1)
	$LD	$a_2,2*$BNSZ($a1)
	$MULTU	($a_0,$b_0)		# mul_add_c(a[0],b[0],c1,c2,c3);
	$LD	$a_3,3*$BNSZ($a1)
	$LD	$b_1,$BNSZ($a2)
	$LD	$b_2,2*$BNSZ($a2)
	$LD	$b_3,3*$BNSZ($a2)
	mflo	($c_1,$a_0,$b_0)
	mfhi	($c_2,$a_0,$b_0)

	$LD	$a_4,4*$BNSZ($a1)
	$LD	$a_5,5*$BNSZ($a1)
	$MULTU	($a_0,$b_1)		# mul_add_c(a[0],b[1],c2,c3,c1);
	$LD	$a_6,6*$BNSZ($a1)
	$LD	$a_7,7*$BNSZ($a1)
	$LD	$b_4,4*$BNSZ($a2)
	$LD	$b_5,5*$BNSZ($a2)
	mflo	($t_1,$a_0,$b_1)
	mfhi	($t_2,$a_0,$b_1)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_1,$b_0)		# mul_add_c(a[1],b[0],c2,c3,c1);
	$ADDU	$c_3,$t_2,$at
	$LD	$b_6,6*$BNSZ($a2)
	$LD	$b_7,7*$BNSZ($a2)
	$ST	$c_1,0($a0)	# r[0]=c1;
	mflo	($t_1,$a_1,$b_0)
	mfhi	($t_2,$a_1,$b_0)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	 $MULTU	($a_2,$b_0)		# mul_add_c(a[2],b[0],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$c_1,$c_3,$t_2
	$ST	$c_2,$BNSZ($a0)	# r[1]=c2;

	mflo	($t_1,$a_2,$b_0)
	mfhi	($t_2,$a_2,$b_0)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_1,$b_1)		# mul_add_c(a[1],b[1],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	mflo	($t_1,$a_1,$b_1)
	mfhi	($t_2,$a_1,$b_1)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_0,$b_2)		# mul_add_c(a[0],b[2],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$c_2,$c_1,$t_2
	mflo	($t_1,$a_0,$b_2)
	mfhi	($t_2,$a_0,$b_2)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	 $MULTU	($a_0,$b_3)		# mul_add_c(a[0],b[3],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	$ST	$c_3,2*$BNSZ($a0)	# r[2]=c3;

	mflo	($t_1,$a_0,$b_3)
	mfhi	($t_2,$a_0,$b_3)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_1,$b_2)		# mul_add_c(a[1],b[2],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$c_3,$c_2,$t_2
	mflo	($t_1,$a_1,$b_2)
	mfhi	($t_2,$a_1,$b_2)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_2,$b_1)		# mul_add_c(a[2],b[1],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_2,$b_1)
	mfhi	($t_2,$a_2,$b_1)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_3,$b_0)		# mul_add_c(a[3],b[0],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_3,$b_0)
	mfhi	($t_2,$a_3,$b_0)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	 $MULTU	($a_4,$b_0)		# mul_add_c(a[4],b[0],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	$ST	$c_1,3*$BNSZ($a0)	# r[3]=c1;

	mflo	($t_1,$a_4,$b_0)
	mfhi	($t_2,$a_4,$b_0)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_3,$b_1)		# mul_add_c(a[3],b[1],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$c_1,$c_3,$t_2
	mflo	($t_1,$a_3,$b_1)
	mfhi	($t_2,$a_3,$b_1)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_2,$b_2)		# mul_add_c(a[2],b[2],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_2,$b_2)
	mfhi	($t_2,$a_2,$b_2)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_1,$b_3)		# mul_add_c(a[1],b[3],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_1,$b_3)
	mfhi	($t_2,$a_1,$b_3)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_0,$b_4)		# mul_add_c(a[0],b[4],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_0,$b_4)
	mfhi	($t_2,$a_0,$b_4)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	 $MULTU	($a_0,$b_5)		# mul_add_c(a[0],b[5],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	$ST	$c_2,4*$BNSZ($a0)	# r[4]=c2;

	mflo	($t_1,$a_0,$b_5)
	mfhi	($t_2,$a_0,$b_5)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_1,$b_4)		# mul_add_c(a[1],b[4],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$c_2,$c_1,$t_2
	mflo	($t_1,$a_1,$b_4)
	mfhi	($t_2,$a_1,$b_4)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_2,$b_3)		# mul_add_c(a[2],b[3],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_2,$b_3)
	mfhi	($t_2,$a_2,$b_3)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_3,$b_2)		# mul_add_c(a[3],b[2],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_3,$b_2)
	mfhi	($t_2,$a_3,$b_2)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_4,$b_1)		# mul_add_c(a[4],b[1],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_4,$b_1)
	mfhi	($t_2,$a_4,$b_1)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_5,$b_0)		# mul_add_c(a[5],b[0],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_5,$b_0)
	mfhi	($t_2,$a_5,$b_0)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	 $MULTU	($a_6,$b_0)		# mul_add_c(a[6],b[0],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	$ST	$c_3,5*$BNSZ($a0)	# r[5]=c3;

	mflo	($t_1,$a_6,$b_0)
	mfhi	($t_2,$a_6,$b_0)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_5,$b_1)		# mul_add_c(a[5],b[1],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$c_3,$c_2,$t_2
	mflo	($t_1,$a_5,$b_1)
	mfhi	($t_2,$a_5,$b_1)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_4,$b_2)		# mul_add_c(a[4],b[2],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_4,$b_2)
	mfhi	($t_2,$a_4,$b_2)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_3,$b_3)		# mul_add_c(a[3],b[3],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_3,$b_3)
	mfhi	($t_2,$a_3,$b_3)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_2,$b_4)		# mul_add_c(a[2],b[4],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_2,$b_4)
	mfhi	($t_2,$a_2,$b_4)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_1,$b_5)		# mul_add_c(a[1],b[5],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_1,$b_5)
	mfhi	($t_2,$a_1,$b_5)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_0,$b_6)		# mul_add_c(a[0],b[6],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_0,$b_6)
	mfhi	($t_2,$a_0,$b_6)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	 $MULTU	($a_0,$b_7)		# mul_add_c(a[0],b[7],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	$ST	$c_1,6*$BNSZ($a0)	# r[6]=c1;

	mflo	($t_1,$a_0,$b_7)
	mfhi	($t_2,$a_0,$b_7)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_1,$b_6)		# mul_add_c(a[1],b[6],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$c_1,$c_3,$t_2
	mflo	($t_1,$a_1,$b_6)
	mfhi	($t_2,$a_1,$b_6)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_2,$b_5)		# mul_add_c(a[2],b[5],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_2,$b_5)
	mfhi	($t_2,$a_2,$b_5)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_3,$b_4)		# mul_add_c(a[3],b[4],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_3,$b_4)
	mfhi	($t_2,$a_3,$b_4)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_4,$b_3)		# mul_add_c(a[4],b[3],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_4,$b_3)
	mfhi	($t_2,$a_4,$b_3)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_5,$b_2)		# mul_add_c(a[5],b[2],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_5,$b_2)
	mfhi	($t_2,$a_5,$b_2)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_6,$b_1)		# mul_add_c(a[6],b[1],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_6,$b_1)
	mfhi	($t_2,$a_6,$b_1)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_7,$b_0)		# mul_add_c(a[7],b[0],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_7,$b_0)
	mfhi	($t_2,$a_7,$b_0)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	 $MULTU	($a_7,$b_1)		# mul_add_c(a[7],b[1],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	$ST	$c_2,7*$BNSZ($a0)	# r[7]=c2;

	mflo	($t_1,$a_7,$b_1)
	mfhi	($t_2,$a_7,$b_1)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_6,$b_2)		# mul_add_c(a[6],b[2],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$c_2,$c_1,$t_2
	mflo	($t_1,$a_6,$b_2)
	mfhi	($t_2,$a_6,$b_2)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_5,$b_3)		# mul_add_c(a[5],b[3],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_5,$b_3)
	mfhi	($t_2,$a_5,$b_3)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_4,$b_4)		# mul_add_c(a[4],b[4],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_4,$b_4)
	mfhi	($t_2,$a_4,$b_4)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_3,$b_5)		# mul_add_c(a[3],b[5],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_3,$b_5)
	mfhi	($t_2,$a_3,$b_5)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_2,$b_6)		# mul_add_c(a[2],b[6],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_2,$b_6)
	mfhi	($t_2,$a_2,$b_6)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_1,$b_7)		# mul_add_c(a[1],b[7],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_1,$b_7)
	mfhi	($t_2,$a_1,$b_7)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	 $MULTU	($a_2,$b_7)		# mul_add_c(a[2],b[7],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	$ST	$c_3,8*$BNSZ($a0)	# r[8]=c3;

	mflo	($t_1,$a_2,$b_7)
	mfhi	($t_2,$a_2,$b_7)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_3,$b_6)		# mul_add_c(a[3],b[6],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$c_3,$c_2,$t_2
	mflo	($t_1,$a_3,$b_6)
	mfhi	($t_2,$a_3,$b_6)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_4,$b_5)		# mul_add_c(a[4],b[5],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_4,$b_5)
	mfhi	($t_2,$a_4,$b_5)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_5,$b_4)		# mul_add_c(a[5],b[4],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_5,$b_4)
	mfhi	($t_2,$a_5,$b_4)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_6,$b_3)		# mul_add_c(a[6],b[3],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_6,$b_3)
	mfhi	($t_2,$a_6,$b_3)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_7,$b_2)		# mul_add_c(a[7],b[2],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_7,$b_2)
	mfhi	($t_2,$a_7,$b_2)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	 $MULTU	($a_7,$b_3)		# mul_add_c(a[7],b[3],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	$ST	$c_1,9*$BNSZ($a0)	# r[9]=c1;

	mflo	($t_1,$a_7,$b_3)
	mfhi	($t_2,$a_7,$b_3)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_6,$b_4)		# mul_add_c(a[6],b[4],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$c_1,$c_3,$t_2
	mflo	($t_1,$a_6,$b_4)
	mfhi	($t_2,$a_6,$b_4)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_5,$b_5)		# mul_add_c(a[5],b[5],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_5,$b_5)
	mfhi	($t_2,$a_5,$b_5)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_4,$b_6)		# mul_add_c(a[4],b[6],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_4,$b_6)
	mfhi	($t_2,$a_4,$b_6)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_3,$b_7)		# mul_add_c(a[3],b[7],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_3,$b_7)
	mfhi	($t_2,$a_3,$b_7)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_4,$b_7)		# mul_add_c(a[4],b[7],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	$ST	$c_2,10*$BNSZ($a0)	# r[10]=c2;

	mflo	($t_1,$a_4,$b_7)
	mfhi	($t_2,$a_4,$b_7)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_5,$b_6)		# mul_add_c(a[5],b[6],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$c_2,$c_1,$t_2
	mflo	($t_1,$a_5,$b_6)
	mfhi	($t_2,$a_5,$b_6)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_6,$b_5)		# mul_add_c(a[6],b[5],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_6,$b_5)
	mfhi	($t_2,$a_6,$b_5)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_7,$b_4)		# mul_add_c(a[7],b[4],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	mflo	($t_1,$a_7,$b_4)
	mfhi	($t_2,$a_7,$b_4)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	 $MULTU	($a_7,$b_5)		# mul_add_c(a[7],b[5],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	$ST	$c_3,11*$BNSZ($a0)	# r[11]=c3;

	mflo	($t_1,$a_7,$b_5)
	mfhi	($t_2,$a_7,$b_5)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_6,$b_6)		# mul_add_c(a[6],b[6],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$c_3,$c_2,$t_2
	mflo	($t_1,$a_6,$b_6)
	mfhi	($t_2,$a_6,$b_6)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_5,$b_7)		# mul_add_c(a[5],b[7],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_5,$b_7)
	mfhi	($t_2,$a_5,$b_7)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	 $MULTU	($a_6,$b_7)		# mul_add_c(a[6],b[7],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	$ST	$c_1,12*$BNSZ($a0)	# r[12]=c1;

	mflo	($t_1,$a_6,$b_7)
	mfhi	($t_2,$a_6,$b_7)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_7,$b_6)		# mul_add_c(a[7],b[6],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$c_1,$c_3,$t_2
	mflo	($t_1,$a_7,$b_6)
	mfhi	($t_2,$a_7,$b_6)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_7,$b_7)		# mul_add_c(a[7],b[7],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	$ST	$c_2,13*$BNSZ($a0)	# r[13]=c2;

	mflo	($t_1,$a_7,$b_7)
	mfhi	($t_2,$a_7,$b_7)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	$ST	$c_3,14*$BNSZ($a0)	# r[14]=c3;
	$ST	$c_1,15*$BNSZ($a0)	# r[15]=c1;

	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$s5,10*$SZREG($sp)
	$REG_L	$s4,9*$SZREG($sp)
	$REG_L	$s3,8*$SZREG($sp)
	$REG_L	$s2,7*$SZREG($sp)
	$REG_L	$s1,6*$SZREG($sp)
	$REG_L	$s0,5*$SZREG($sp)
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	jr	$ra
	$PTR_ADD $sp,12*$SZREG
___
$code.=<<___ if ($flavour !~ /nubi/i);
	$REG_L	$s5,5*$SZREG($sp)
	$REG_L	$s4,4*$SZREG($sp)
	$REG_L	$s3,3*$SZREG($sp)
	$REG_L	$s2,2*$SZREG($sp)
	$REG_L	$s1,1*$SZREG($sp)
	$REG_L	$s0,0*$SZREG($sp)
	jr	$ra
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
.end	bn_mul_comba8

.align	5
.globl	bn_mul_comba4
.ent	bn_mul_comba4
bn_mul_comba4:
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x8000f008,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$ra,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___;
	.set	reorder
	$LD	$a_0,0($a1)
	$LD	$b_0,0($a2)
	$LD	$a_1,$BNSZ($a1)
	$LD	$a_2,2*$BNSZ($a1)
	$MULTU	($a_0,$b_0)		# mul_add_c(a[0],b[0],c1,c2,c3);
	$LD	$a_3,3*$BNSZ($a1)
	$LD	$b_1,$BNSZ($a2)
	$LD	$b_2,2*$BNSZ($a2)
	$LD	$b_3,3*$BNSZ($a2)
	mflo	($c_1,$a_0,$b_0)
	mfhi	($c_2,$a_0,$b_0)
	$ST	$c_1,0($a0)

	$MULTU	($a_0,$b_1)		# mul_add_c(a[0],b[1],c2,c3,c1);
	mflo	($t_1,$a_0,$b_1)
	mfhi	($t_2,$a_0,$b_1)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_1,$b_0)		# mul_add_c(a[1],b[0],c2,c3,c1);
	$ADDU	$c_3,$t_2,$at
	mflo	($t_1,$a_1,$b_0)
	mfhi	($t_2,$a_1,$b_0)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	 $MULTU	($a_2,$b_0)		# mul_add_c(a[2],b[0],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$c_1,$c_3,$t_2
	$ST	$c_2,$BNSZ($a0)

	mflo	($t_1,$a_2,$b_0)
	mfhi	($t_2,$a_2,$b_0)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_1,$b_1)		# mul_add_c(a[1],b[1],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	mflo	($t_1,$a_1,$b_1)
	mfhi	($t_2,$a_1,$b_1)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_0,$b_2)		# mul_add_c(a[0],b[2],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$c_2,$c_1,$t_2
	mflo	($t_1,$a_0,$b_2)
	mfhi	($t_2,$a_0,$b_2)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	 $MULTU	($a_0,$b_3)		# mul_add_c(a[0],b[3],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	$ST	$c_3,2*$BNSZ($a0)

	mflo	($t_1,$a_0,$b_3)
	mfhi	($t_2,$a_0,$b_3)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_1,$b_2)		# mul_add_c(a[1],b[2],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$c_3,$c_2,$t_2
	mflo	($t_1,$a_1,$b_2)
	mfhi	($t_2,$a_1,$b_2)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_2,$b_1)		# mul_add_c(a[2],b[1],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_2,$b_1)
	mfhi	($t_2,$a_2,$b_1)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$MULTU	($a_3,$b_0)		# mul_add_c(a[3],b[0],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	mflo	($t_1,$a_3,$b_0)
	mfhi	($t_2,$a_3,$b_0)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	 $MULTU	($a_3,$b_1)		# mul_add_c(a[3],b[1],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	$ST	$c_1,3*$BNSZ($a0)

	mflo	($t_1,$a_3,$b_1)
	mfhi	($t_2,$a_3,$b_1)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_2,$b_2)		# mul_add_c(a[2],b[2],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$c_1,$c_3,$t_2
	mflo	($t_1,$a_2,$b_2)
	mfhi	($t_2,$a_2,$b_2)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$MULTU	($a_1,$b_3)		# mul_add_c(a[1],b[3],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	mflo	($t_1,$a_1,$b_3)
	mfhi	($t_2,$a_1,$b_3)
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	 $MULTU	($a_2,$b_3)		# mul_add_c(a[2],b[3],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	$ST	$c_2,4*$BNSZ($a0)

	mflo	($t_1,$a_2,$b_3)
	mfhi	($t_2,$a_2,$b_3)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$MULTU	($a_3,$b_2)		# mul_add_c(a[3],b[2],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$c_2,$c_1,$t_2
	mflo	($t_1,$a_3,$b_2)
	mfhi	($t_2,$a_3,$b_2)
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	 $MULTU	($a_3,$b_3)		# mul_add_c(a[3],b[3],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	$ST	$c_3,5*$BNSZ($a0)

	mflo	($t_1,$a_3,$b_3)
	mfhi	($t_2,$a_3,$b_3)
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	$ST	$c_1,6*$BNSZ($a0)
	$ST	$c_2,7*$BNSZ($a0)

	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
	jr	$ra
	nop
.end	bn_mul_comba4
___

($a_4,$a_5,$a_6,$a_7)=($b_0,$b_1,$b_2,$b_3);

sub add_c2 () {
my ($hi,$lo,$c0,$c1,$c2,
    $warm,      # !$warm denotes first call with specific sequence of
                # $c_[XYZ] when there is no Z-carry to accumulate yet;
    $an,$bn     # these two are arguments for multiplication which
                # result is used in *next* step [which is why it's
                # commented as "forward multiplication" below];
    )=@_;
$code.=<<___;
	$ADDU	$c0,$lo
	sltu	$at,$c0,$lo
	 $MULTU	($an,$bn)		# forward multiplication
	$ADDU	$c0,$lo
	$ADDU	$at,$hi
	sltu	$lo,$c0,$lo
	$ADDU	$c1,$at
	$ADDU	$hi,$lo
___
$code.=<<___	if (!$warm);
	sltu	$c2,$c1,$at
	$ADDU	$c1,$hi
___
$code.=<<___	if ($warm);
	sltu	$at,$c1,$at
	$ADDU	$c1,$hi
	$ADDU	$c2,$at
___
$code.=<<___;
	sltu	$hi,$c1,$hi
	$ADDU	$c2,$hi
	mflo	($lo,$an,$bn)
	mfhi	($hi,$an,$bn)
___
}

$code.=<<___;

.align	5
.globl	bn_sqr_comba8
.ent	bn_sqr_comba8
bn_sqr_comba8:
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x8000f008,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$ra,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___;
	.set	reorder
	$LD	$a_0,0($a1)
	$LD	$a_1,$BNSZ($a1)
	$LD	$a_2,2*$BNSZ($a1)
	$LD	$a_3,3*$BNSZ($a1)

	$MULTU	($a_0,$a_0)		# mul_add_c(a[0],b[0],c1,c2,c3);
	$LD	$a_4,4*$BNSZ($a1)
	$LD	$a_5,5*$BNSZ($a1)
	$LD	$a_6,6*$BNSZ($a1)
	$LD	$a_7,7*$BNSZ($a1)
	mflo	($c_1,$a_0,$a_0)
	mfhi	($c_2,$a_0,$a_0)
	$ST	$c_1,0($a0)

	$MULTU	($a_0,$a_1)		# mul_add_c2(a[0],b[1],c2,c3,c1);
	mflo	($t_1,$a_0,$a_1)
	mfhi	($t_2,$a_0,$a_1)
	slt	$c_1,$t_2,$zero
	$SLL	$t_2,1
	 $MULTU	($a_2,$a_0)		# mul_add_c2(a[2],b[0],c3,c1,c2);
	slt	$a2,$t_1,$zero
	$ADDU	$t_2,$a2
	$SLL	$t_1,1
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$ADDU	$c_3,$t_2,$at
	$ST	$c_2,$BNSZ($a0)
	mflo	($t_1,$a_2,$a_0)
	mfhi	($t_2,$a_2,$a_0)
___
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,0,
		$a_1,$a_1);		# mul_add_c(a[1],b[1],c3,c1,c2);
$code.=<<___;
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	 $MULTU	($a_0,$a_3)		# mul_add_c2(a[0],b[3],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	$ST	$c_3,2*$BNSZ($a0)
	mflo	($t_1,$a_0,$a_3)
	mfhi	($t_2,$a_0,$a_3)
___
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,0,
		$a_1,$a_2);		# mul_add_c2(a[1],b[2],c1,c2,c3);
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,1,
		$a_4,$a_0);		# mul_add_c2(a[4],b[0],c2,c3,c1);
$code.=<<___;
	$ST	$c_1,3*$BNSZ($a0)
___
	&add_c2($t_2,$t_1,$c_2,$c_3,$c_1,0,
		$a_3,$a_1);		# mul_add_c2(a[3],b[1],c2,c3,c1);
	&add_c2($t_2,$t_1,$c_2,$c_3,$c_1,1,
		$a_2,$a_2);		# mul_add_c(a[2],b[2],c2,c3,c1);
$code.=<<___;
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	 $MULTU	($a_0,$a_5)		# mul_add_c2(a[0],b[5],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	$ST	$c_2,4*$BNSZ($a0)
	mflo	($t_1,$a_0,$a_5)
	mfhi	($t_2,$a_0,$a_5)
___
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,0,
		$a_1,$a_4);		# mul_add_c2(a[1],b[4],c3,c1,c2);
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,1,
		$a_2,$a_3);		# mul_add_c2(a[2],b[3],c3,c1,c2);
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,1,
		$a_6,$a_0);		# mul_add_c2(a[6],b[0],c1,c2,c3);
$code.=<<___;
	$ST	$c_3,5*$BNSZ($a0)
___
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,0,
		$a_5,$a_1);		# mul_add_c2(a[5],b[1],c1,c2,c3);
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,1,
		$a_4,$a_2);		# mul_add_c2(a[4],b[2],c1,c2,c3);
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,1,
		$a_3,$a_3);		# mul_add_c(a[3],b[3],c1,c2,c3);
$code.=<<___;
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	 $MULTU	($a_0,$a_7)		# mul_add_c2(a[0],b[7],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	$ST	$c_1,6*$BNSZ($a0)
	mflo	($t_1,$a_0,$a_7)
	mfhi	($t_2,$a_0,$a_7)
___
	&add_c2($t_2,$t_1,$c_2,$c_3,$c_1,0,
		$a_1,$a_6);		# mul_add_c2(a[1],b[6],c2,c3,c1);
	&add_c2($t_2,$t_1,$c_2,$c_3,$c_1,1,
		$a_2,$a_5);		# mul_add_c2(a[2],b[5],c2,c3,c1);
	&add_c2($t_2,$t_1,$c_2,$c_3,$c_1,1,
		$a_3,$a_4);		# mul_add_c2(a[3],b[4],c2,c3,c1);
	&add_c2($t_2,$t_1,$c_2,$c_3,$c_1,1,
		$a_7,$a_1);		# mul_add_c2(a[7],b[1],c3,c1,c2);
$code.=<<___;
	$ST	$c_2,7*$BNSZ($a0)
___
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,0,
		$a_6,$a_2);		# mul_add_c2(a[6],b[2],c3,c1,c2);
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,1,
		$a_5,$a_3);		# mul_add_c2(a[5],b[3],c3,c1,c2);
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,1,
		$a_4,$a_4);		# mul_add_c(a[4],b[4],c3,c1,c2);
$code.=<<___;
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	 $MULTU	($a_2,$a_7)		# mul_add_c2(a[2],b[7],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	$ST	$c_3,8*$BNSZ($a0)
	mflo	($t_1,$a_2,$a_7)
	mfhi	($t_2,$a_2,$a_7)
___
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,0,
		$a_3,$a_6);		# mul_add_c2(a[3],b[6],c1,c2,c3);
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,1,
		$a_4,$a_5);		# mul_add_c2(a[4],b[5],c1,c2,c3);
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,1,
		$a_7,$a_3);		# mul_add_c2(a[7],b[3],c2,c3,c1);
$code.=<<___;
	$ST	$c_1,9*$BNSZ($a0)
___
	&add_c2($t_2,$t_1,$c_2,$c_3,$c_1,0,
		$a_6,$a_4);		# mul_add_c2(a[6],b[4],c2,c3,c1);
	&add_c2($t_2,$t_1,$c_2,$c_3,$c_1,1,
		$a_5,$a_5);		# mul_add_c(a[5],b[5],c2,c3,c1);
$code.=<<___;
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	 $MULTU	($a_4,$a_7)		# mul_add_c2(a[4],b[7],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	$ST	$c_2,10*$BNSZ($a0)
	mflo	($t_1,$a_4,$a_7)
	mfhi	($t_2,$a_4,$a_7)
___
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,0,
		$a_5,$a_6);		# mul_add_c2(a[5],b[6],c3,c1,c2);
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,1,
		$a_7,$a_5);		# mul_add_c2(a[7],b[5],c1,c2,c3);
$code.=<<___;
	$ST	$c_3,11*$BNSZ($a0)
___
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,0,
		$a_6,$a_6);		# mul_add_c(a[6],b[6],c1,c2,c3);
$code.=<<___;
	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	 $MULTU	($a_6,$a_7)		# mul_add_c2(a[6],b[7],c2,c3,c1);
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	sltu	$at,$c_2,$t_2
	$ADDU	$c_3,$at
	$ST	$c_1,12*$BNSZ($a0)
	mflo	($t_1,$a_6,$a_7)
	mfhi	($t_2,$a_6,$a_7)
___
	&add_c2($t_2,$t_1,$c_2,$c_3,$c_1,0,
		$a_7,$a_7);		# mul_add_c(a[7],b[7],c3,c1,c2);
$code.=<<___;
	$ST	$c_2,13*$BNSZ($a0)

	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	$ST	$c_3,14*$BNSZ($a0)
	$ST	$c_1,15*$BNSZ($a0)

	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
	jr	$ra
	nop
.end	bn_sqr_comba8

.align	5
.globl	bn_sqr_comba4
.ent	bn_sqr_comba4
bn_sqr_comba4:
___
$code.=<<___ if ($flavour =~ /nubi/i);
	.frame	$sp,6*$SZREG,$ra
	.mask	0x8000f008,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,6*$SZREG
	$REG_S	$ra,5*$SZREG($sp)
	$REG_S	$t3,4*$SZREG($sp)
	$REG_S	$t2,3*$SZREG($sp)
	$REG_S	$t1,2*$SZREG($sp)
	$REG_S	$t0,1*$SZREG($sp)
	$REG_S	$gp,0*$SZREG($sp)
___
$code.=<<___;
	.set	reorder
	$LD	$a_0,0($a1)
	$LD	$a_1,$BNSZ($a1)
	$MULTU	($a_0,$a_0)		# mul_add_c(a[0],b[0],c1,c2,c3);
	$LD	$a_2,2*$BNSZ($a1)
	$LD	$a_3,3*$BNSZ($a1)
	mflo	($c_1,$a_0,$a_0)
	mfhi	($c_2,$a_0,$a_0)
	$ST	$c_1,0($a0)

	$MULTU	($a_0,$a_1)		# mul_add_c2(a[0],b[1],c2,c3,c1);
	mflo	($t_1,$a_0,$a_1)
	mfhi	($t_2,$a_0,$a_1)
	slt	$c_1,$t_2,$zero
	$SLL	$t_2,1
	 $MULTU	($a_2,$a_0)		# mul_add_c2(a[2],b[0],c3,c1,c2);
	slt	$a2,$t_1,$zero
	$ADDU	$t_2,$a2
	$SLL	$t_1,1
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	$ADDU	$c_3,$t_2,$at
	$ST	$c_2,$BNSZ($a0)
	mflo	($t_1,$a_2,$a_0)
	mfhi	($t_2,$a_2,$a_0)
___
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,0,
		$a_1,$a_1);		# mul_add_c(a[1],b[1],c3,c1,c2);
$code.=<<___;
	$ADDU	$c_3,$t_1
	sltu	$at,$c_3,$t_1
	 $MULTU	($a_0,$a_3)		# mul_add_c2(a[0],b[3],c1,c2,c3);
	$ADDU	$t_2,$at
	$ADDU	$c_1,$t_2
	sltu	$at,$c_1,$t_2
	$ADDU	$c_2,$at
	$ST	$c_3,2*$BNSZ($a0)
	mflo	($t_1,$a_0,$a_3)
	mfhi	($t_2,$a_0,$a_3)
___
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,0,
		$a_1,$a_2);		# mul_add_c2(a2[1],b[2],c1,c2,c3);
	&add_c2($t_2,$t_1,$c_1,$c_2,$c_3,1,
		$a_3,$a_1);		# mul_add_c2(a[3],b[1],c2,c3,c1);
$code.=<<___;
	$ST	$c_1,3*$BNSZ($a0)
___
	&add_c2($t_2,$t_1,$c_2,$c_3,$c_1,0,
		$a_2,$a_2);		# mul_add_c(a[2],b[2],c2,c3,c1);
$code.=<<___;
	$ADDU	$c_2,$t_1
	sltu	$at,$c_2,$t_1
	 $MULTU	($a_2,$a_3)		# mul_add_c2(a[2],b[3],c3,c1,c2);
	$ADDU	$t_2,$at
	$ADDU	$c_3,$t_2
	sltu	$at,$c_3,$t_2
	$ADDU	$c_1,$at
	$ST	$c_2,4*$BNSZ($a0)
	mflo	($t_1,$a_2,$a_3)
	mfhi	($t_2,$a_2,$a_3)
___
	&add_c2($t_2,$t_1,$c_3,$c_1,$c_2,0,
		$a_3,$a_3);		# mul_add_c(a[3],b[3],c1,c2,c3);
$code.=<<___;
	$ST	$c_3,5*$BNSZ($a0)

	$ADDU	$c_1,$t_1
	sltu	$at,$c_1,$t_1
	$ADDU	$t_2,$at
	$ADDU	$c_2,$t_2
	$ST	$c_1,6*$BNSZ($a0)
	$ST	$c_2,7*$BNSZ($a0)

	.set	noreorder
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$t3,4*$SZREG($sp)
	$REG_L	$t2,3*$SZREG($sp)
	$REG_L	$t1,2*$SZREG($sp)
	$REG_L	$t0,1*$SZREG($sp)
	$REG_L	$gp,0*$SZREG($sp)
	$PTR_ADD $sp,6*$SZREG
___
$code.=<<___;
	jr	$ra
	nop
.end	bn_sqr_comba4
___
print $code;
close STDOUT;

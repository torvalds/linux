#! /usr/bin/env perl
# Copyright 2012-2018 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Written by David S. Miller and Andy Polyakov
# The module is licensed under 2-clause BSD license.
# November 2012. All rights reserved.
# ====================================================================

######################################################################
# Montgomery squaring-n-multiplication module for SPARC T4.
#
# The module consists of three parts:
#
# 1) collection of "single-op" subroutines that perform single
#    operation, Montgomery squaring or multiplication, on 512-,
#    1024-, 1536- and 2048-bit operands;
# 2) collection of "multi-op" subroutines that perform 5 squaring and
#    1 multiplication operations on operands of above lengths;
# 3) fall-back and helper VIS3 subroutines.
#
# RSA sign is dominated by multi-op subroutine, while RSA verify and
# DSA - by single-op. Special note about 4096-bit RSA verify result.
# Operands are too long for dedicated hardware and it's handled by
# VIS3 code, which is why you don't see any improvement. It's surely
# possible to improve it [by deploying 'mpmul' instruction], maybe in
# the future...
#
# Performance improvement.
#
# 64-bit process, VIS3:
#                   sign    verify    sign/s verify/s
# rsa 1024 bits 0.000628s 0.000028s   1592.4  35434.4
# rsa 2048 bits 0.003282s 0.000106s    304.7   9438.3
# rsa 4096 bits 0.025866s 0.000340s     38.7   2940.9
# dsa 1024 bits 0.000301s 0.000332s   3323.7   3013.9
# dsa 2048 bits 0.001056s 0.001233s    946.9    810.8
#
# 64-bit process, this module:
#                   sign    verify    sign/s verify/s
# rsa 1024 bits 0.000256s 0.000016s   3904.4  61411.9
# rsa 2048 bits 0.000946s 0.000029s   1056.8  34292.7
# rsa 4096 bits 0.005061s 0.000340s    197.6   2940.5
# dsa 1024 bits 0.000176s 0.000195s   5674.7   5130.5
# dsa 2048 bits 0.000296s 0.000354s   3383.2   2827.6
#
######################################################################
# 32-bit process, VIS3:
#                   sign    verify    sign/s verify/s
# rsa 1024 bits 0.000665s 0.000028s   1504.8  35233.3
# rsa 2048 bits 0.003349s 0.000106s    298.6   9433.4
# rsa 4096 bits 0.025959s 0.000341s     38.5   2934.8
# dsa 1024 bits 0.000320s 0.000341s   3123.3   2929.6
# dsa 2048 bits 0.001101s 0.001260s    908.2    793.4
#
# 32-bit process, this module:
#                   sign    verify    sign/s verify/s
# rsa 1024 bits 0.000301s 0.000017s   3317.1  60240.0
# rsa 2048 bits 0.001034s 0.000030s    966.9  33812.7
# rsa 4096 bits 0.005244s 0.000341s    190.7   2935.4
# dsa 1024 bits 0.000201s 0.000205s   4976.1   4879.2
# dsa 2048 bits 0.000328s 0.000360s   3051.1   2774.2
#
# 32-bit code is prone to performance degradation as interrupt rate
# dispatched to CPU executing the code grows. This is because in
# standard process of handling interrupt in 32-bit process context
# upper halves of most integer registers used as input or output are
# zeroed. This renders result invalid, and operation has to be re-run.
# If CPU is "bothered" with timer interrupts only, the penalty is
# hardly measurable. But in order to mitigate this problem for higher
# interrupt rates contemporary Linux kernel recognizes biased stack
# even in 32-bit process context and preserves full register contents.
# See http://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=517ffce4e1a03aea979fe3a18a3dd1761a24fafb
# for details.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "sparcv9_modes.pl";

$output = pop;
open STDOUT,">$output";

$code.=<<___;
#include "sparc_arch.h"

#ifdef	__arch64__
.register	%g2,#scratch
.register	%g3,#scratch
#endif

.section	".text",#alloc,#execinstr

#ifdef	__PIC__
SPARC_PIC_THUNK(%g1)
#endif
___

########################################################################
# Register layout for mont[mul|sqr] instructions.
# For details see "Oracle SPARC Architecture 2011" manual at
# http://www.oracle.com/technetwork/server-storage/sun-sparc-enterprise/documentation/.
#
my @R=map("%f".2*$_,(0..11,30,31,12..29));
my @N=(map("%l$_",(0..7)),map("%o$_",(0..5))); @N=(@N,@N,@N[0..3]);
my @A=(@N[0..13],@R[14..31]);
my @B=(map("%i$_",(0..5)),map("%l$_",(0..7))); @B=(@B,@B,map("%o$_",(0..3)));

########################################################################
# int bn_mul_mont_t4_$NUM(u64 *rp,const u64 *ap,const u64 *bp,
#			  const u64 *np,const BN_ULONG *n0);
#
sub generate_bn_mul_mont_t4() {
my $NUM=shift;
my ($rp,$ap,$bp,$np,$sentinel)=map("%g$_",(1..5));

$code.=<<___;
.globl	bn_mul_mont_t4_$NUM
.align	32
bn_mul_mont_t4_$NUM:
#ifdef	__arch64__
	mov	0,$sentinel
	mov	-128,%g4
#elif defined(SPARCV9_64BIT_STACK)
	SPARC_LOAD_ADDRESS_LEAF(OPENSSL_sparcv9cap_P,%g1,%g5)
	ld	[%g1+0],%g1	! OPENSSL_sparcv9_P[0]
	mov	-2047,%g4
	and	%g1,SPARCV9_64BIT_STACK,%g1
	movrz	%g1,0,%g4
	mov	-1,$sentinel
	add	%g4,-128,%g4
#else
	mov	-1,$sentinel
	mov	-128,%g4
#endif
	sllx	$sentinel,32,$sentinel
	save	%sp,%g4,%sp
#ifndef	__arch64__
	save	%sp,-128,%sp	! warm it up
	save	%sp,-128,%sp
	save	%sp,-128,%sp
	save	%sp,-128,%sp
	save	%sp,-128,%sp
	save	%sp,-128,%sp
	restore
	restore
	restore
	restore
	restore
	restore
#endif
	and	%sp,1,%g4
	or	$sentinel,%fp,%fp
	or	%g4,$sentinel,$sentinel

	! copy arguments to global registers
	mov	%i0,$rp
	mov	%i1,$ap
	mov	%i2,$bp
	mov	%i3,$np
	ld	[%i4+0],%f1	! load *n0
	ld	[%i4+4],%f0
	fsrc2	%f0,%f60
___

# load ap[$NUM] ########################################################
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for($i=0; $i<14 && $i<$NUM; $i++) {
my $lo=$i<13?@A[$i+1]:"%o7";
$code.=<<___;
	ld	[$ap+$i*8+0],$lo
	ld	[$ap+$i*8+4],@A[$i]
	sllx	@A[$i],32,@A[$i]
	or	$lo,@A[$i],@A[$i]
___
}
for(; $i<$NUM; $i++) {
my ($hi,$lo)=("%f".2*($i%4),"%f".(2*($i%4)+1));
$code.=<<___;
	ld	[$ap+$i*8+0],$lo
	ld	[$ap+$i*8+4],$hi
	fsrc2	$hi,@A[$i]
___
}
# load np[$NUM] ########################################################
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for($i=0; $i<14 && $i<$NUM; $i++) {
my $lo=$i<13?@N[$i+1]:"%o7";
$code.=<<___;
	ld	[$np+$i*8+0],$lo
	ld	[$np+$i*8+4],@N[$i]
	sllx	@N[$i],32,@N[$i]
	or	$lo,@N[$i],@N[$i]
___
}
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for(; $i<28 && $i<$NUM; $i++) {
my $lo=$i<27?@N[$i+1]:"%o7";
$code.=<<___;
	ld	[$np+$i*8+0],$lo
	ld	[$np+$i*8+4],@N[$i]
	sllx	@N[$i],32,@N[$i]
	or	$lo,@N[$i],@N[$i]
___
}
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for(; $i<$NUM; $i++) {
my $lo=($i<$NUM-1)?@N[$i+1]:"%o7";
$code.=<<___;
	ld	[$np+$i*8+0],$lo
	ld	[$np+$i*8+4],@N[$i]
	sllx	@N[$i],32,@N[$i]
	or	$lo,@N[$i],@N[$i]
___
}
$code.=<<___;
	cmp	$ap,$bp
	be	SIZE_T_CC,.Lmsquare_$NUM
	nop
___

# load bp[$NUM] ########################################################
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for($i=0; $i<14 && $i<$NUM; $i++) {
my $lo=$i<13?@B[$i+1]:"%o7";
$code.=<<___;
	ld	[$bp+$i*8+0],$lo
	ld	[$bp+$i*8+4],@B[$i]
	sllx	@B[$i],32,@B[$i]
	or	$lo,@B[$i],@B[$i]
___
}
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for(; $i<$NUM; $i++) {
my $lo=($i<$NUM-1)?@B[$i+1]:"%o7";
$code.=<<___;
	ld	[$bp+$i*8+0],$lo
	ld	[$bp+$i*8+4],@B[$i]
	sllx	@B[$i],32,@B[$i]
	or	$lo,@B[$i],@B[$i]
___
}
# magic ################################################################
$code.=<<___;
	.word	0x81b02920+$NUM-1	! montmul	$NUM-1
.Lmresume_$NUM:
	fbu,pn	%fcc3,.Lmabort_$NUM
#ifndef	__arch64__
	and	%fp,$sentinel,$sentinel
	brz,pn	$sentinel,.Lmabort_$NUM
#endif
	nop
#ifdef	__arch64__
	restore
	restore
	restore
	restore
	restore
#else
	restore;		and	%fp,$sentinel,$sentinel
	restore;		and	%fp,$sentinel,$sentinel
	restore;		and	%fp,$sentinel,$sentinel
	restore;		and	%fp,$sentinel,$sentinel
	 brz,pn	$sentinel,.Lmabort1_$NUM
	restore
#endif
___

# save tp[$NUM] ########################################################
for($i=0; $i<14 && $i<$NUM; $i++) {
$code.=<<___;
	movxtod	@A[$i],@R[$i]
___
}
$code.=<<___;
#ifdef	__arch64__
	restore
#else
	 and	%fp,$sentinel,$sentinel
	restore
	 and	$sentinel,1,%o7
	 and	%fp,$sentinel,$sentinel
	 srl	%fp,0,%fp		! just in case?
	 or	%o7,$sentinel,$sentinel
	brz,a,pn $sentinel,.Lmdone_$NUM
	mov	0,%i0		! return failure
#endif
___
for($i=0; $i<12 && $i<$NUM; $i++) {
@R[$i] =~ /%f([0-9]+)/;
my $lo = "%f".($1+1);
$code.=<<___;
	st	$lo,[$rp+$i*8+0]
	st	@R[$i],[$rp+$i*8+4]
___
}
for(; $i<$NUM; $i++) {
my ($hi,$lo)=("%f".2*($i%4),"%f".(2*($i%4)+1));
$code.=<<___;
	fsrc2	@R[$i],$hi
	st	$lo,[$rp+$i*8+0]
	st	$hi,[$rp+$i*8+4]
___
}
$code.=<<___;
	mov	1,%i0		! return success
.Lmdone_$NUM:
	ret
	restore

.Lmabort_$NUM:
	restore
	restore
	restore
	restore
	restore
.Lmabort1_$NUM:
	restore

	mov	0,%i0		! return failure
	ret
	restore

.align	32
.Lmsquare_$NUM:
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
	.word   0x81b02940+$NUM-1	! montsqr	$NUM-1
	ba	.Lmresume_$NUM
	nop
.type	bn_mul_mont_t4_$NUM, #function
.size	bn_mul_mont_t4_$NUM, .-bn_mul_mont_t4_$NUM
___
}

for ($i=8;$i<=32;$i+=8) {
	&generate_bn_mul_mont_t4($i);
}

########################################################################
#
sub load_ccr {
my ($ptbl,$pwr,$ccr,$skip_wr)=@_;
$code.=<<___;
	srl	$pwr,	2,	%o4
	and	$pwr,	3,	%o5
	and	%o4,	7,	%o4
	sll	%o5,	3,	%o5	! offset within first cache line
	add	%o5,	$ptbl,	$ptbl	! of the pwrtbl
	or	%g0,	1,	%o5
	sll	%o5,	%o4,	$ccr
___
$code.=<<___	if (!$skip_wr);
	wr	$ccr,	%g0,	%ccr
___
}
sub load_b_pair {
my ($pwrtbl,$B0,$B1)=@_;

$code.=<<___;
	ldx	[$pwrtbl+0*32],	$B0
	ldx	[$pwrtbl+8*32],	$B1
	ldx	[$pwrtbl+1*32],	%o4
	ldx	[$pwrtbl+9*32],	%o5
	movvs	%icc,	%o4,	$B0
	ldx	[$pwrtbl+2*32],	%o4
	movvs	%icc,	%o5,	$B1
	ldx	[$pwrtbl+10*32],%o5
	move	%icc,	%o4,	$B0
	ldx	[$pwrtbl+3*32],	%o4
	move	%icc,	%o5,	$B1
	ldx	[$pwrtbl+11*32],%o5
	movneg	%icc,	%o4,	$B0
	ldx	[$pwrtbl+4*32],	%o4
	movneg	%icc,	%o5,	$B1
	ldx	[$pwrtbl+12*32],%o5
	movcs	%xcc,	%o4,	$B0
	ldx	[$pwrtbl+5*32],%o4
	movcs	%xcc,	%o5,	$B1
	ldx	[$pwrtbl+13*32],%o5
	movvs	%xcc,	%o4,	$B0
	ldx	[$pwrtbl+6*32],	%o4
	movvs	%xcc,	%o5,	$B1
	ldx	[$pwrtbl+14*32],%o5
	move	%xcc,	%o4,	$B0
	ldx	[$pwrtbl+7*32],	%o4
	move	%xcc,	%o5,	$B1
	ldx	[$pwrtbl+15*32],%o5
	movneg	%xcc,	%o4,	$B0
	add	$pwrtbl,16*32,	$pwrtbl
	movneg	%xcc,	%o5,	$B1
___
}
sub load_b {
my ($pwrtbl,$Bi)=@_;

$code.=<<___;
	ldx	[$pwrtbl+0*32],	$Bi
	ldx	[$pwrtbl+1*32],	%o4
	ldx	[$pwrtbl+2*32],	%o5
	movvs	%icc,	%o4,	$Bi
	ldx	[$pwrtbl+3*32],	%o4
	move	%icc,	%o5,	$Bi
	ldx	[$pwrtbl+4*32],	%o5
	movneg	%icc,	%o4,	$Bi
	ldx	[$pwrtbl+5*32],	%o4
	movcs	%xcc,	%o5,	$Bi
	ldx	[$pwrtbl+6*32],	%o5
	movvs	%xcc,	%o4,	$Bi
	ldx	[$pwrtbl+7*32],	%o4
	move	%xcc,	%o5,	$Bi
	add	$pwrtbl,8*32,	$pwrtbl
	movneg	%xcc,	%o4,	$Bi
___
}

########################################################################
# int bn_pwr5_mont_t4_$NUM(u64 *tp,const u64 *np,const BN_ULONG *n0,
#			   const u64 *pwrtbl,int pwr,int stride);
#
sub generate_bn_pwr5_mont_t4() {
my $NUM=shift;
my ($tp,$np,$pwrtbl,$pwr,$sentinel)=map("%g$_",(1..5));

$code.=<<___;
.globl	bn_pwr5_mont_t4_$NUM
.align	32
bn_pwr5_mont_t4_$NUM:
#ifdef	__arch64__
	mov	0,$sentinel
	mov	-128,%g4
#elif defined(SPARCV9_64BIT_STACK)
	SPARC_LOAD_ADDRESS_LEAF(OPENSSL_sparcv9cap_P,%g1,%g5)
	ld	[%g1+0],%g1	! OPENSSL_sparcv9_P[0]
	mov	-2047,%g4
	and	%g1,SPARCV9_64BIT_STACK,%g1
	movrz	%g1,0,%g4
	mov	-1,$sentinel
	add	%g4,-128,%g4
#else
	mov	-1,$sentinel
	mov	-128,%g4
#endif
	sllx	$sentinel,32,$sentinel
	save	%sp,%g4,%sp
#ifndef	__arch64__
	save	%sp,-128,%sp	! warm it up
	save	%sp,-128,%sp
	save	%sp,-128,%sp
	save	%sp,-128,%sp
	save	%sp,-128,%sp
	save	%sp,-128,%sp
	restore
	restore
	restore
	restore
	restore
	restore
#endif
	and	%sp,1,%g4
	or	$sentinel,%fp,%fp
	or	%g4,$sentinel,$sentinel

	! copy arguments to global registers
	mov	%i0,$tp
	mov	%i1,$np
	ld	[%i2+0],%f1	! load *n0
	ld	[%i2+4],%f0
	mov	%i3,$pwrtbl
	srl	%i4,%g0,%i4	! pack last arguments
	sllx	%i5,32,$pwr
	or	%i4,$pwr,$pwr
	fsrc2	%f0,%f60
___

# load tp[$NUM] ########################################################
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for($i=0; $i<14 && $i<$NUM; $i++) {
$code.=<<___;
	ldx	[$tp+$i*8],@A[$i]
___
}
for(; $i<$NUM; $i++) {
$code.=<<___;
	ldd	[$tp+$i*8],@A[$i]
___
}
# load np[$NUM] ########################################################
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for($i=0; $i<14 && $i<$NUM; $i++) {
$code.=<<___;
	ldx	[$np+$i*8],@N[$i]
___
}
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for(; $i<28 && $i<$NUM; $i++) {
$code.=<<___;
	ldx	[$np+$i*8],@N[$i]
___
}
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for(; $i<$NUM; $i++) {
$code.=<<___;
	ldx	[$np+$i*8],@N[$i]
___
}
# load pwrtbl[pwr] ########################################################
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp

	srlx	$pwr,	32,	%o4		! unpack $pwr
	srl	$pwr,	%g0,	%o5
	sub	%o4,	5,	%o4
	mov	$pwrtbl,	%o7
	sllx	%o4,	32,	$pwr		! re-pack $pwr
	or	%o5,	$pwr,	$pwr
	srl	%o5,	%o4,	%o5
___
	&load_ccr("%o7","%o5","%o4");
$code.=<<___;
	b	.Lstride_$NUM
	nop
.align	16
.Lstride_$NUM:
___
for($i=0; $i<14 && $i<$NUM; $i+=2) {
	&load_b_pair("%o7",@B[$i],@B[$i+1]);
}
$code.=<<___;
	save	%sp,-128,%sp;		or	$sentinel,%fp,%fp
___
for(; $i<$NUM; $i+=2) {
	&load_b_pair("%i7",@B[$i],@B[$i+1]);
}
$code.=<<___;
	srax	$pwr,	32,	%o4		! unpack $pwr
	srl	$pwr,	%g0,	%o5
	sub	%o4,	5,	%o4
	mov	$pwrtbl,	%i7
	sllx	%o4,	32,	$pwr		! re-pack $pwr
	or	%o5,	$pwr,	$pwr
	srl	%o5,	%o4,	%o5
___
	&load_ccr("%i7","%o5","%o4",1);

# magic ################################################################
for($i=0; $i<5; $i++) {
$code.=<<___;
	.word	0x81b02940+$NUM-1	! montsqr	$NUM-1
	fbu,pn	%fcc3,.Labort_$NUM
#ifndef	__arch64__
	and	%fp,$sentinel,$sentinel
	brz,pn	$sentinel,.Labort_$NUM
#endif
	nop
___
}
$code.=<<___;
	wr	%o4,	%g0,	%ccr
	.word	0x81b02920+$NUM-1	! montmul	$NUM-1
	fbu,pn	%fcc3,.Labort_$NUM
#ifndef	__arch64__
	and	%fp,$sentinel,$sentinel
	brz,pn	$sentinel,.Labort_$NUM
#endif

	srax	$pwr,	32,	%o4
#ifdef	__arch64__
	brgez	%o4,.Lstride_$NUM
	restore
	restore
	restore
	restore
	restore
#else
	brgez	%o4,.Lstride_$NUM
	restore;		and	%fp,$sentinel,$sentinel
	restore;		and	%fp,$sentinel,$sentinel
	restore;		and	%fp,$sentinel,$sentinel
	restore;		and	%fp,$sentinel,$sentinel
	 brz,pn	$sentinel,.Labort1_$NUM
	restore
#endif
___

# save tp[$NUM] ########################################################
for($i=0; $i<14 && $i<$NUM; $i++) {
$code.=<<___;
	movxtod	@A[$i],@R[$i]
___
}
$code.=<<___;
#ifdef	__arch64__
	restore
#else
	 and	%fp,$sentinel,$sentinel
	restore
	 and	$sentinel,1,%o7
	 and	%fp,$sentinel,$sentinel
	 srl	%fp,0,%fp		! just in case?
	 or	%o7,$sentinel,$sentinel
	brz,a,pn $sentinel,.Ldone_$NUM
	mov	0,%i0		! return failure
#endif
___
for($i=0; $i<$NUM; $i++) {
$code.=<<___;
	std	@R[$i],[$tp+$i*8]
___
}
$code.=<<___;
	mov	1,%i0		! return success
.Ldone_$NUM:
	ret
	restore

.Labort_$NUM:
	restore
	restore
	restore
	restore
	restore
.Labort1_$NUM:
	restore

	mov	0,%i0		! return failure
	ret
	restore
.type	bn_pwr5_mont_t4_$NUM, #function
.size	bn_pwr5_mont_t4_$NUM, .-bn_pwr5_mont_t4_$NUM
___
}

for ($i=8;$i<=32;$i+=8) {
	&generate_bn_pwr5_mont_t4($i);
}

{
########################################################################
# Fall-back subroutines
#
# copy of bn_mul_mont_vis3 adjusted for vectors of 64-bit values
#
($n0,$m0,$m1,$lo0,$hi0, $lo1,$hi1,$aj,$alo,$nj,$nlo,$tj)=
	(map("%g$_",(1..5)),map("%o$_",(0..5,7)));

# int bn_mul_mont(
$rp="%o0";	# u64 *rp,
$ap="%o1";	# const u64 *ap,
$bp="%o2";	# const u64 *bp,
$np="%o3";	# const u64 *np,
$n0p="%o4";	# const BN_ULONG *n0,
$num="%o5";	# int num);	# caller ensures that num is >=3
$code.=<<___;
.globl	bn_mul_mont_t4
.align	32
bn_mul_mont_t4:
	add	%sp,	STACK_BIAS,	%g4	! real top of stack
	sll	$num,	3,	$num		! size in bytes
	add	$num,	63,	%g1
	andn	%g1,	63,	%g1		! buffer size rounded up to 64 bytes
	sub	%g4,	%g1,	%g1
	andn	%g1,	63,	%g1		! align at 64 byte
	sub	%g1,	STACK_FRAME,	%g1	! new top of stack
	sub	%g1,	%g4,	%g1

	save	%sp,	%g1,	%sp
___
#	+-------------------------------+<-----	%sp
#	.				.
#	+-------------------------------+<-----	aligned at 64 bytes
#	| __int64 tmp[0]		|
#	+-------------------------------+
#	.				.
#	.				.
#	+-------------------------------+<-----	aligned at 64 bytes
#	.				.
($rp,$ap,$bp,$np,$n0p,$num)=map("%i$_",(0..5));
($t0,$t1,$t2,$t3,$cnt,$tp,$bufsz)=map("%l$_",(0..7));
($ovf,$i)=($t0,$t1);
$code.=<<___;
	ld	[$n0p+0],	$t0	! pull n0[0..1] value
	ld	[$n0p+4],	$t1
	add	%sp, STACK_BIAS+STACK_FRAME, $tp
	ldx	[$bp+0],	$m0	! m0=bp[0]
	sllx	$t1,	32,	$n0
	add	$bp,	8,	$bp
	or	$t0,	$n0,	$n0

	ldx	[$ap+0],	$aj	! ap[0]

	mulx	$aj,	$m0,	$lo0	! ap[0]*bp[0]
	umulxhi	$aj,	$m0,	$hi0

	ldx	[$ap+8],	$aj	! ap[1]
	add	$ap,	16,	$ap
	ldx	[$np+0],	$nj	! np[0]

	mulx	$lo0,	$n0,	$m1	! "tp[0]"*n0

	mulx	$aj,	$m0,	$alo	! ap[1]*bp[0]
	umulxhi	$aj,	$m0,	$aj	! ahi=aj

	mulx	$nj,	$m1,	$lo1	! np[0]*m1
	umulxhi	$nj,	$m1,	$hi1

	ldx	[$np+8],	$nj	! np[1]

	addcc	$lo0,	$lo1,	$lo1
	add	$np,	16,	$np
	addxc	%g0,	$hi1,	$hi1

	mulx	$nj,	$m1,	$nlo	! np[1]*m1
	umulxhi	$nj,	$m1,	$nj	! nhi=nj

	ba	.L1st
	sub	$num,	24,	$cnt	! cnt=num-3

.align	16
.L1st:
	addcc	$alo,	$hi0,	$lo0
	addxc	$aj,	%g0,	$hi0

	ldx	[$ap+0],	$aj	! ap[j]
	addcc	$nlo,	$hi1,	$lo1
	add	$ap,	8,	$ap
	addxc	$nj,	%g0,	$hi1	! nhi=nj

	ldx	[$np+0],	$nj	! np[j]
	mulx	$aj,	$m0,	$alo	! ap[j]*bp[0]
	add	$np,	8,	$np
	umulxhi	$aj,	$m0,	$aj	! ahi=aj

	mulx	$nj,	$m1,	$nlo	! np[j]*m1
	addcc	$lo0,	$lo1,	$lo1	! np[j]*m1+ap[j]*bp[0]
	umulxhi	$nj,	$m1,	$nj	! nhi=nj
	addxc	%g0,	$hi1,	$hi1
	stxa	$lo1,	[$tp]0xe2	! tp[j-1]
	add	$tp,	8,	$tp	! tp++

	brnz,pt	$cnt,	.L1st
	sub	$cnt,	8,	$cnt	! j--
!.L1st
	addcc	$alo,	$hi0,	$lo0
	addxc	$aj,	%g0,	$hi0	! ahi=aj

	addcc	$nlo,	$hi1,	$lo1
	addxc	$nj,	%g0,	$hi1
	addcc	$lo0,	$lo1,	$lo1	! np[j]*m1+ap[j]*bp[0]
	addxc	%g0,	$hi1,	$hi1
	stxa	$lo1,	[$tp]0xe2	! tp[j-1]
	add	$tp,	8,	$tp

	addcc	$hi0,	$hi1,	$hi1
	addxc	%g0,	%g0,	$ovf	! upmost overflow bit
	stxa	$hi1,	[$tp]0xe2
	add	$tp,	8,	$tp

	ba	.Louter
	sub	$num,	16,	$i	! i=num-2

.align	16
.Louter:
	ldx	[$bp+0],	$m0	! m0=bp[i]
	add	$bp,	8,	$bp

	sub	$ap,	$num,	$ap	! rewind
	sub	$np,	$num,	$np
	sub	$tp,	$num,	$tp

	ldx	[$ap+0],	$aj	! ap[0]
	ldx	[$np+0],	$nj	! np[0]

	mulx	$aj,	$m0,	$lo0	! ap[0]*bp[i]
	ldx	[$tp],		$tj	! tp[0]
	umulxhi	$aj,	$m0,	$hi0
	ldx	[$ap+8],	$aj	! ap[1]
	addcc	$lo0,	$tj,	$lo0	! ap[0]*bp[i]+tp[0]
	mulx	$aj,	$m0,	$alo	! ap[1]*bp[i]
	addxc	%g0,	$hi0,	$hi0
	mulx	$lo0,	$n0,	$m1	! tp[0]*n0
	umulxhi	$aj,	$m0,	$aj	! ahi=aj
	mulx	$nj,	$m1,	$lo1	! np[0]*m1
	add	$ap,	16,	$ap
	umulxhi	$nj,	$m1,	$hi1
	ldx	[$np+8],	$nj	! np[1]
	add	$np,	16,	$np
	addcc	$lo1,	$lo0,	$lo1
	mulx	$nj,	$m1,	$nlo	! np[1]*m1
	addxc	%g0,	$hi1,	$hi1
	umulxhi	$nj,	$m1,	$nj	! nhi=nj

	ba	.Linner
	sub	$num,	24,	$cnt	! cnt=num-3
.align	16
.Linner:
	addcc	$alo,	$hi0,	$lo0
	ldx	[$tp+8],	$tj	! tp[j]
	addxc	$aj,	%g0,	$hi0	! ahi=aj
	ldx	[$ap+0],	$aj	! ap[j]
	add	$ap,	8,	$ap
	addcc	$nlo,	$hi1,	$lo1
	mulx	$aj,	$m0,	$alo	! ap[j]*bp[i]
	addxc	$nj,	%g0,	$hi1	! nhi=nj
	ldx	[$np+0],	$nj	! np[j]
	add	$np,	8,	$np
	umulxhi	$aj,	$m0,	$aj	! ahi=aj
	addcc	$lo0,	$tj,	$lo0	! ap[j]*bp[i]+tp[j]
	mulx	$nj,	$m1,	$nlo	! np[j]*m1
	addxc	%g0,	$hi0,	$hi0
	umulxhi	$nj,	$m1,	$nj	! nhi=nj
	addcc	$lo1,	$lo0,	$lo1	! np[j]*m1+ap[j]*bp[i]+tp[j]
	addxc	%g0,	$hi1,	$hi1
	stx	$lo1,	[$tp]		! tp[j-1]
	add	$tp,	8,	$tp
	brnz,pt	$cnt,	.Linner
	sub	$cnt,	8,	$cnt
!.Linner
	ldx	[$tp+8],	$tj	! tp[j]
	addcc	$alo,	$hi0,	$lo0
	addxc	$aj,	%g0,	$hi0	! ahi=aj
	addcc	$lo0,	$tj,	$lo0	! ap[j]*bp[i]+tp[j]
	addxc	%g0,	$hi0,	$hi0

	addcc	$nlo,	$hi1,	$lo1
	addxc	$nj,	%g0,	$hi1	! nhi=nj
	addcc	$lo1,	$lo0,	$lo1	! np[j]*m1+ap[j]*bp[i]+tp[j]
	addxc	%g0,	$hi1,	$hi1
	stx	$lo1,	[$tp]		! tp[j-1]

	subcc	%g0,	$ovf,	%g0	! move upmost overflow to CCR.xcc
	addxccc	$hi1,	$hi0,	$hi1
	addxc	%g0,	%g0,	$ovf
	stx	$hi1,	[$tp+8]
	add	$tp,	16,	$tp

	brnz,pt	$i,	.Louter
	sub	$i,	8,	$i

	sub	$ap,	$num,	$ap	! rewind
	sub	$np,	$num,	$np
	sub	$tp,	$num,	$tp
	ba	.Lsub
	subcc	$num,	8,	$cnt	! cnt=num-1 and clear CCR.xcc

.align	16
.Lsub:
	ldx	[$tp],		$tj
	add	$tp,	8,	$tp
	ldx	[$np+0],	$nj
	add	$np,	8,	$np
	subccc	$tj,	$nj,	$t2	! tp[j]-np[j]
	srlx	$tj,	32,	$tj
	srlx	$nj,	32,	$nj
	subccc	$tj,	$nj,	$t3
	add	$rp,	8,	$rp
	st	$t2,	[$rp-4]		! reverse order
	st	$t3,	[$rp-8]
	brnz,pt	$cnt,	.Lsub
	sub	$cnt,	8,	$cnt

	sub	$np,	$num,	$np	! rewind
	sub	$tp,	$num,	$tp
	sub	$rp,	$num,	$rp

	subccc	$ovf,	%g0,	$ovf	! handle upmost overflow bit
	ba	.Lcopy
	sub	$num,	8,	$cnt

.align	16
.Lcopy:					! conditional copy
	ldx	[$tp],		$tj
	ldx	[$rp+0],	$t2
	stx	%g0,	[$tp]		! zap
	add	$tp,	8,	$tp
	movcs	%icc,	$tj,	$t2
	stx	$t2,	[$rp+0]
	add	$rp,	8,	$rp
	brnz	$cnt,	.Lcopy
	sub	$cnt,	8,	$cnt

	mov	1,	%o0
	ret
	restore
.type	bn_mul_mont_t4, #function
.size	bn_mul_mont_t4, .-bn_mul_mont_t4
___

# int bn_mul_mont_gather5(
$rp="%o0";	# u64 *rp,
$ap="%o1";	# const u64 *ap,
$bp="%o2";	# const u64 *pwrtbl,
$np="%o3";	# const u64 *np,
$n0p="%o4";	# const BN_ULONG *n0,
$num="%o5";	# int num,	# caller ensures that num is >=3
		# int power);
$code.=<<___;
.globl	bn_mul_mont_gather5_t4
.align	32
bn_mul_mont_gather5_t4:
	add	%sp,	STACK_BIAS,	%g4	! real top of stack
	sll	$num,	3,	$num		! size in bytes
	add	$num,	63,	%g1
	andn	%g1,	63,	%g1		! buffer size rounded up to 64 bytes
	sub	%g4,	%g1,	%g1
	andn	%g1,	63,	%g1		! align at 64 byte
	sub	%g1,	STACK_FRAME,	%g1	! new top of stack
	sub	%g1,	%g4,	%g1
	LDPTR	[%sp+STACK_7thARG],	%g4	! load power, 7th argument

	save	%sp,	%g1,	%sp
___
#	+-------------------------------+<-----	%sp
#	.				.
#	+-------------------------------+<-----	aligned at 64 bytes
#	| __int64 tmp[0]		|
#	+-------------------------------+
#	.				.
#	.				.
#	+-------------------------------+<-----	aligned at 64 bytes
#	.				.
($rp,$ap,$bp,$np,$n0p,$num)=map("%i$_",(0..5));
($t0,$t1,$t2,$t3,$cnt,$tp,$bufsz,$ccr)=map("%l$_",(0..7));
($ovf,$i)=($t0,$t1);
	&load_ccr($bp,"%g4",$ccr);
	&load_b($bp,$m0,"%o7");		# m0=bp[0]

$code.=<<___;
	ld	[$n0p+0],	$t0	! pull n0[0..1] value
	ld	[$n0p+4],	$t1
	add	%sp, STACK_BIAS+STACK_FRAME, $tp
	sllx	$t1,	32,	$n0
	or	$t0,	$n0,	$n0

	ldx	[$ap+0],	$aj	! ap[0]

	mulx	$aj,	$m0,	$lo0	! ap[0]*bp[0]
	umulxhi	$aj,	$m0,	$hi0

	ldx	[$ap+8],	$aj	! ap[1]
	add	$ap,	16,	$ap
	ldx	[$np+0],	$nj	! np[0]

	mulx	$lo0,	$n0,	$m1	! "tp[0]"*n0

	mulx	$aj,	$m0,	$alo	! ap[1]*bp[0]
	umulxhi	$aj,	$m0,	$aj	! ahi=aj

	mulx	$nj,	$m1,	$lo1	! np[0]*m1
	umulxhi	$nj,	$m1,	$hi1

	ldx	[$np+8],	$nj	! np[1]

	addcc	$lo0,	$lo1,	$lo1
	add	$np,	16,	$np
	addxc	%g0,	$hi1,	$hi1

	mulx	$nj,	$m1,	$nlo	! np[1]*m1
	umulxhi	$nj,	$m1,	$nj	! nhi=nj

	ba	.L1st_g5
	sub	$num,	24,	$cnt	! cnt=num-3

.align	16
.L1st_g5:
	addcc	$alo,	$hi0,	$lo0
	addxc	$aj,	%g0,	$hi0

	ldx	[$ap+0],	$aj	! ap[j]
	addcc	$nlo,	$hi1,	$lo1
	add	$ap,	8,	$ap
	addxc	$nj,	%g0,	$hi1	! nhi=nj

	ldx	[$np+0],	$nj	! np[j]
	mulx	$aj,	$m0,	$alo	! ap[j]*bp[0]
	add	$np,	8,	$np
	umulxhi	$aj,	$m0,	$aj	! ahi=aj

	mulx	$nj,	$m1,	$nlo	! np[j]*m1
	addcc	$lo0,	$lo1,	$lo1	! np[j]*m1+ap[j]*bp[0]
	umulxhi	$nj,	$m1,	$nj	! nhi=nj
	addxc	%g0,	$hi1,	$hi1
	stxa	$lo1,	[$tp]0xe2	! tp[j-1]
	add	$tp,	8,	$tp	! tp++

	brnz,pt	$cnt,	.L1st_g5
	sub	$cnt,	8,	$cnt	! j--
!.L1st_g5
	addcc	$alo,	$hi0,	$lo0
	addxc	$aj,	%g0,	$hi0	! ahi=aj

	addcc	$nlo,	$hi1,	$lo1
	addxc	$nj,	%g0,	$hi1
	addcc	$lo0,	$lo1,	$lo1	! np[j]*m1+ap[j]*bp[0]
	addxc	%g0,	$hi1,	$hi1
	stxa	$lo1,	[$tp]0xe2	! tp[j-1]
	add	$tp,	8,	$tp

	addcc	$hi0,	$hi1,	$hi1
	addxc	%g0,	%g0,	$ovf	! upmost overflow bit
	stxa	$hi1,	[$tp]0xe2
	add	$tp,	8,	$tp

	ba	.Louter_g5
	sub	$num,	16,	$i	! i=num-2

.align	16
.Louter_g5:
	wr	$ccr,	%g0,	%ccr
___
	&load_b($bp,$m0);		# m0=bp[i]
$code.=<<___;
	sub	$ap,	$num,	$ap	! rewind
	sub	$np,	$num,	$np
	sub	$tp,	$num,	$tp

	ldx	[$ap+0],	$aj	! ap[0]
	ldx	[$np+0],	$nj	! np[0]

	mulx	$aj,	$m0,	$lo0	! ap[0]*bp[i]
	ldx	[$tp],		$tj	! tp[0]
	umulxhi	$aj,	$m0,	$hi0
	ldx	[$ap+8],	$aj	! ap[1]
	addcc	$lo0,	$tj,	$lo0	! ap[0]*bp[i]+tp[0]
	mulx	$aj,	$m0,	$alo	! ap[1]*bp[i]
	addxc	%g0,	$hi0,	$hi0
	mulx	$lo0,	$n0,	$m1	! tp[0]*n0
	umulxhi	$aj,	$m0,	$aj	! ahi=aj
	mulx	$nj,	$m1,	$lo1	! np[0]*m1
	add	$ap,	16,	$ap
	umulxhi	$nj,	$m1,	$hi1
	ldx	[$np+8],	$nj	! np[1]
	add	$np,	16,	$np
	addcc	$lo1,	$lo0,	$lo1
	mulx	$nj,	$m1,	$nlo	! np[1]*m1
	addxc	%g0,	$hi1,	$hi1
	umulxhi	$nj,	$m1,	$nj	! nhi=nj

	ba	.Linner_g5
	sub	$num,	24,	$cnt	! cnt=num-3
.align	16
.Linner_g5:
	addcc	$alo,	$hi0,	$lo0
	ldx	[$tp+8],	$tj	! tp[j]
	addxc	$aj,	%g0,	$hi0	! ahi=aj
	ldx	[$ap+0],	$aj	! ap[j]
	add	$ap,	8,	$ap
	addcc	$nlo,	$hi1,	$lo1
	mulx	$aj,	$m0,	$alo	! ap[j]*bp[i]
	addxc	$nj,	%g0,	$hi1	! nhi=nj
	ldx	[$np+0],	$nj	! np[j]
	add	$np,	8,	$np
	umulxhi	$aj,	$m0,	$aj	! ahi=aj
	addcc	$lo0,	$tj,	$lo0	! ap[j]*bp[i]+tp[j]
	mulx	$nj,	$m1,	$nlo	! np[j]*m1
	addxc	%g0,	$hi0,	$hi0
	umulxhi	$nj,	$m1,	$nj	! nhi=nj
	addcc	$lo1,	$lo0,	$lo1	! np[j]*m1+ap[j]*bp[i]+tp[j]
	addxc	%g0,	$hi1,	$hi1
	stx	$lo1,	[$tp]		! tp[j-1]
	add	$tp,	8,	$tp
	brnz,pt	$cnt,	.Linner_g5
	sub	$cnt,	8,	$cnt
!.Linner_g5
	ldx	[$tp+8],	$tj	! tp[j]
	addcc	$alo,	$hi0,	$lo0
	addxc	$aj,	%g0,	$hi0	! ahi=aj
	addcc	$lo0,	$tj,	$lo0	! ap[j]*bp[i]+tp[j]
	addxc	%g0,	$hi0,	$hi0

	addcc	$nlo,	$hi1,	$lo1
	addxc	$nj,	%g0,	$hi1	! nhi=nj
	addcc	$lo1,	$lo0,	$lo1	! np[j]*m1+ap[j]*bp[i]+tp[j]
	addxc	%g0,	$hi1,	$hi1
	stx	$lo1,	[$tp]		! tp[j-1]

	subcc	%g0,	$ovf,	%g0	! move upmost overflow to CCR.xcc
	addxccc	$hi1,	$hi0,	$hi1
	addxc	%g0,	%g0,	$ovf
	stx	$hi1,	[$tp+8]
	add	$tp,	16,	$tp

	brnz,pt	$i,	.Louter_g5
	sub	$i,	8,	$i

	sub	$ap,	$num,	$ap	! rewind
	sub	$np,	$num,	$np
	sub	$tp,	$num,	$tp
	ba	.Lsub_g5
	subcc	$num,	8,	$cnt	! cnt=num-1 and clear CCR.xcc

.align	16
.Lsub_g5:
	ldx	[$tp],		$tj
	add	$tp,	8,	$tp
	ldx	[$np+0],	$nj
	add	$np,	8,	$np
	subccc	$tj,	$nj,	$t2	! tp[j]-np[j]
	srlx	$tj,	32,	$tj
	srlx	$nj,	32,	$nj
	subccc	$tj,	$nj,	$t3
	add	$rp,	8,	$rp
	st	$t2,	[$rp-4]		! reverse order
	st	$t3,	[$rp-8]
	brnz,pt	$cnt,	.Lsub_g5
	sub	$cnt,	8,	$cnt

	sub	$np,	$num,	$np	! rewind
	sub	$tp,	$num,	$tp
	sub	$rp,	$num,	$rp

	subccc	$ovf,	%g0,	$ovf	! handle upmost overflow bit
	ba	.Lcopy_g5
	sub	$num,	8,	$cnt

.align	16
.Lcopy_g5:				! conditional copy
	ldx	[$tp],		$tj
	ldx	[$rp+0],	$t2
	stx	%g0,	[$tp]		! zap
	add	$tp,	8,	$tp
	movcs	%icc,	$tj,	$t2
	stx	$t2,	[$rp+0]
	add	$rp,	8,	$rp
	brnz	$cnt,	.Lcopy_g5
	sub	$cnt,	8,	$cnt

	mov	1,	%o0
	ret
	restore
.type	bn_mul_mont_gather5_t4, #function
.size	bn_mul_mont_gather5_t4, .-bn_mul_mont_gather5_t4
___
}

$code.=<<___;
.globl	bn_flip_t4
.align	32
bn_flip_t4:
.Loop_flip:
	ld	[%o1+0],	%o4
	sub	%o2,	1,	%o2
	ld	[%o1+4],	%o5
	add	%o1,	8,	%o1
	st	%o5,	[%o0+0]
	st	%o4,	[%o0+4]
	brnz	%o2,	.Loop_flip
	add	%o0,	8,	%o0
	retl
	nop
.type	bn_flip_t4, #function
.size	bn_flip_t4, .-bn_flip_t4

.globl	bn_flip_n_scatter5_t4
.align	32
bn_flip_n_scatter5_t4:
	sll	%o3,	3,	%o3
	srl	%o1,	1,	%o1
	add	%o3,	%o2,	%o2	! &pwrtbl[pwr]
	sub	%o1,	1,	%o1
.Loop_flip_n_scatter5:
	ld	[%o0+0],	%o4	! inp[i]
	ld	[%o0+4],	%o5
	add	%o0,	8,	%o0
	sllx	%o5,	32,	%o5
	or	%o4,	%o5,	%o5
	stx	%o5,	[%o2]
	add	%o2,	32*8,	%o2
	brnz	%o1,	.Loop_flip_n_scatter5
	sub	%o1,	1,	%o1
	retl
	nop
.type	bn_flip_n_scatter5_t4, #function
.size	bn_flip_n_scatter5_t4, .-bn_flip_n_scatter5_t4

.globl	bn_gather5_t4
.align	32
bn_gather5_t4:
___
	&load_ccr("%o2","%o3","%g1");
$code.=<<___;
	sub	%o1,	1,	%o1
.Loop_gather5:
___
	&load_b("%o2","%g1");
$code.=<<___;
	stx	%g1,	[%o0]
	add	%o0,	8,	%o0
	brnz	%o1,	.Loop_gather5
	sub	%o1,	1,	%o1

	retl
	nop
.type	bn_gather5_t4, #function
.size	bn_gather5_t4, .-bn_gather5_t4

.asciz	"Montgomery Multiplication for SPARC T4, David S. Miller, Andy Polyakov"
.align	4
___

&emit_assembler();

close STDOUT;

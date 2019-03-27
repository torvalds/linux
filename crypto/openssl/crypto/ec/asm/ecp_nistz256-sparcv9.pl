#! /usr/bin/env perl
# Copyright 2015-2018 The OpenSSL Project Authors. All Rights Reserved.
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
#
# ECP_NISTZ256 module for SPARCv9.
#
# February 2015.
#
# Original ECP_NISTZ256 submission targeting x86_64 is detailed in
# http://eprint.iacr.org/2013/816. In the process of adaptation
# original .c module was made 32-bit savvy in order to make this
# implementation possible.
#
#			with/without -DECP_NISTZ256_ASM
# UltraSPARC III	+12-18%
# SPARC T4		+99-550% (+66-150% on 32-bit Solaris)
#
# Ranges denote minimum and maximum improvement coefficients depending
# on benchmark. Lower coefficients are for ECDSA sign, server-side
# operation. Keep in mind that +200% means 3x improvement.

$output = pop;
open STDOUT,">$output";

$code.=<<___;
#include "sparc_arch.h"

#define LOCALS	(STACK_BIAS+STACK_FRAME)
#ifdef	__arch64__
.register	%g2,#scratch
.register	%g3,#scratch
# define STACK64_FRAME	STACK_FRAME
# define LOCALS64	LOCALS
#else
# define STACK64_FRAME	(2047+192)
# define LOCALS64	STACK64_FRAME
#endif

.section	".text",#alloc,#execinstr
___
########################################################################
# Convert ecp_nistz256_table.c to layout expected by ecp_nistz_gather_w7
#
$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
open TABLE,"<ecp_nistz256_table.c"		or
open TABLE,"<${dir}../ecp_nistz256_table.c"	or
die "failed to open ecp_nistz256_table.c:",$!;

use integer;

foreach(<TABLE>) {
	s/TOBN\(\s*(0x[0-9a-f]+),\s*(0x[0-9a-f]+)\s*\)/push @arr,hex($2),hex($1)/geo;
}
close TABLE;

# See ecp_nistz256_table.c for explanation for why it's 64*16*37.
# 64*16*37-1 is because $#arr returns last valid index or @arr, not
# amount of elements.
die "insane number of elements" if ($#arr != 64*16*37-1);

$code.=<<___;
.globl	ecp_nistz256_precomputed
.align	4096
ecp_nistz256_precomputed:
___
########################################################################
# this conversion smashes P256_POINT_AFFINE by individual bytes with
# 64 byte interval, similar to
#	1111222233334444
#	1234123412341234
for(1..37) {
	@tbl = splice(@arr,0,64*16);
	for($i=0;$i<64;$i++) {
		undef @line;
		for($j=0;$j<64;$j++) {
			push @line,(@tbl[$j*16+$i/4]>>(($i%4)*8))&0xff;
		}
		$code.=".byte\t";
		$code.=join(',',map { sprintf "0x%02x",$_} @line);
		$code.="\n";
	}
}

{{{
my ($rp,$ap,$bp)=map("%i$_",(0..2));
my @acc=map("%l$_",(0..7));
my ($t0,$t1,$t2,$t3,$t4,$t5,$t6,$t7)=(map("%o$_",(0..5)),"%g4","%g5");
my ($bi,$a0,$mask,$carry)=(map("%i$_",(3..5)),"%g1");
my ($rp_real,$ap_real)=("%g2","%g3");

$code.=<<___;
.type	ecp_nistz256_precomputed,#object
.size	ecp_nistz256_precomputed,.-ecp_nistz256_precomputed
.align	64
.LRR:	! 2^512 mod P precomputed for NIST P256 polynomial
.long	0x00000003, 0x00000000, 0xffffffff, 0xfffffffb
.long	0xfffffffe, 0xffffffff, 0xfffffffd, 0x00000004
.Lone:
.long	1,0,0,0,0,0,0,0
.asciz	"ECP_NISTZ256 for SPARCv9, CRYPTOGAMS by <appro\@openssl.org>"

! void	ecp_nistz256_to_mont(BN_ULONG %i0[8],const BN_ULONG %i1[8]);
.globl	ecp_nistz256_to_mont
.align	64
ecp_nistz256_to_mont:
	save	%sp,-STACK_FRAME,%sp
	nop
1:	call	.+8
	add	%o7,.LRR-1b,$bp
	call	__ecp_nistz256_mul_mont
	nop
	ret
	restore
.type	ecp_nistz256_to_mont,#function
.size	ecp_nistz256_to_mont,.-ecp_nistz256_to_mont

! void	ecp_nistz256_from_mont(BN_ULONG %i0[8],const BN_ULONG %i1[8]);
.globl	ecp_nistz256_from_mont
.align	32
ecp_nistz256_from_mont:
	save	%sp,-STACK_FRAME,%sp
	nop
1:	call	.+8
	add	%o7,.Lone-1b,$bp
	call	__ecp_nistz256_mul_mont
	nop
	ret
	restore
.type	ecp_nistz256_from_mont,#function
.size	ecp_nistz256_from_mont,.-ecp_nistz256_from_mont

! void	ecp_nistz256_mul_mont(BN_ULONG %i0[8],const BN_ULONG %i1[8],
!					      const BN_ULONG %i2[8]);
.globl	ecp_nistz256_mul_mont
.align	32
ecp_nistz256_mul_mont:
	save	%sp,-STACK_FRAME,%sp
	nop
	call	__ecp_nistz256_mul_mont
	nop
	ret
	restore
.type	ecp_nistz256_mul_mont,#function
.size	ecp_nistz256_mul_mont,.-ecp_nistz256_mul_mont

! void	ecp_nistz256_sqr_mont(BN_ULONG %i0[8],const BN_ULONG %i2[8]);
.globl	ecp_nistz256_sqr_mont
.align	32
ecp_nistz256_sqr_mont:
	save	%sp,-STACK_FRAME,%sp
	mov	$ap,$bp
	call	__ecp_nistz256_mul_mont
	nop
	ret
	restore
.type	ecp_nistz256_sqr_mont,#function
.size	ecp_nistz256_sqr_mont,.-ecp_nistz256_sqr_mont
___

########################################################################
# Special thing to keep in mind is that $t0-$t7 hold 64-bit values,
# while all others are meant to keep 32. "Meant to" means that additions
# to @acc[0-7] do "contaminate" upper bits, but they are cleared before
# they can affect outcome (follow 'and' with $mask). Also keep in mind
# that addition with carry is addition with 32-bit carry, even though
# CPU is 64-bit. [Addition with 64-bit carry was introduced in T3, see
# below for VIS3 code paths.]

$code.=<<___;
.align	32
__ecp_nistz256_mul_mont:
	ld	[$bp+0],$bi		! b[0]
	mov	-1,$mask
	ld	[$ap+0],$a0
	srl	$mask,0,$mask		! 0xffffffff
	ld	[$ap+4],$t1
	ld	[$ap+8],$t2
	ld	[$ap+12],$t3
	ld	[$ap+16],$t4
	ld	[$ap+20],$t5
	ld	[$ap+24],$t6
	ld	[$ap+28],$t7
	mulx	$a0,$bi,$t0		! a[0-7]*b[0], 64-bit results
	mulx	$t1,$bi,$t1
	mulx	$t2,$bi,$t2
	mulx	$t3,$bi,$t3
	mulx	$t4,$bi,$t4
	mulx	$t5,$bi,$t5
	mulx	$t6,$bi,$t6
	mulx	$t7,$bi,$t7
	srlx	$t0,32,@acc[1]		! extract high parts
	srlx	$t1,32,@acc[2]
	srlx	$t2,32,@acc[3]
	srlx	$t3,32,@acc[4]
	srlx	$t4,32,@acc[5]
	srlx	$t5,32,@acc[6]
	srlx	$t6,32,@acc[7]
	srlx	$t7,32,@acc[0]		! "@acc[8]"
	mov	0,$carry
___
for($i=1;$i<8;$i++) {
$code.=<<___;
	addcc	@acc[1],$t1,@acc[1]	! accumulate high parts
	ld	[$bp+4*$i],$bi		! b[$i]
	ld	[$ap+4],$t1		! re-load a[1-7]
	addccc	@acc[2],$t2,@acc[2]
	addccc	@acc[3],$t3,@acc[3]
	ld	[$ap+8],$t2
	ld	[$ap+12],$t3
	addccc	@acc[4],$t4,@acc[4]
	addccc	@acc[5],$t5,@acc[5]
	ld	[$ap+16],$t4
	ld	[$ap+20],$t5
	addccc	@acc[6],$t6,@acc[6]
	addccc	@acc[7],$t7,@acc[7]
	ld	[$ap+24],$t6
	ld	[$ap+28],$t7
	addccc	@acc[0],$carry,@acc[0]	! "@acc[8]"
	addc	%g0,%g0,$carry
___
	# Reduction iteration is normally performed by accumulating
	# result of multiplication of modulus by "magic" digit [and
	# omitting least significant word, which is guaranteed to
	# be 0], but thanks to special form of modulus and "magic"
	# digit being equal to least significant word, it can be
	# performed with additions and subtractions alone. Indeed:
	#
	#        ffff.0001.0000.0000.0000.ffff.ffff.ffff
	# *                                         abcd
	# + xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.abcd
	#
	# Now observing that ff..ff*x = (2^n-1)*x = 2^n*x-x, we
	# rewrite above as:
	#
	#   xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.abcd
	# + abcd.0000.abcd.0000.0000.abcd.0000.0000.0000
	# -      abcd.0000.0000.0000.0000.0000.0000.abcd
	#
	# or marking redundant operations:
	#
	#   xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.----
	# + abcd.0000.abcd.0000.0000.abcd.----.----.----
	# -      abcd.----.----.----.----.----.----.----

$code.=<<___;
	! multiplication-less reduction
	addcc	@acc[3],$t0,@acc[3]	! r[3]+=r[0]
	addccc	@acc[4],%g0,@acc[4]	! r[4]+=0
	 and	@acc[1],$mask,@acc[1]
	 and	@acc[2],$mask,@acc[2]
	addccc	@acc[5],%g0,@acc[5]	! r[5]+=0
	addccc	@acc[6],$t0,@acc[6]	! r[6]+=r[0]
	 and	@acc[3],$mask,@acc[3]
	 and	@acc[4],$mask,@acc[4]
	addccc	@acc[7],%g0,@acc[7]	! r[7]+=0
	addccc	@acc[0],$t0,@acc[0]	! r[8]+=r[0]	"@acc[8]"
	 and	@acc[5],$mask,@acc[5]
	 and	@acc[6],$mask,@acc[6]
	addc	$carry,%g0,$carry	! top-most carry
	subcc	@acc[7],$t0,@acc[7]	! r[7]-=r[0]
	subccc	@acc[0],%g0,@acc[0]	! r[8]-=0	"@acc[8]"
	subc	$carry,%g0,$carry	! top-most carry
	 and	@acc[7],$mask,@acc[7]
	 and	@acc[0],$mask,@acc[0]	! "@acc[8]"
___
	push(@acc,shift(@acc));		# rotate registers to "omit" acc[0]
$code.=<<___;
	mulx	$a0,$bi,$t0		! a[0-7]*b[$i], 64-bit results
	mulx	$t1,$bi,$t1
	mulx	$t2,$bi,$t2
	mulx	$t3,$bi,$t3
	mulx	$t4,$bi,$t4
	mulx	$t5,$bi,$t5
	mulx	$t6,$bi,$t6
	mulx	$t7,$bi,$t7
	add	@acc[0],$t0,$t0		! accumulate low parts, can't overflow
	add	@acc[1],$t1,$t1
	srlx	$t0,32,@acc[1]		! extract high parts
	add	@acc[2],$t2,$t2
	srlx	$t1,32,@acc[2]
	add	@acc[3],$t3,$t3
	srlx	$t2,32,@acc[3]
	add	@acc[4],$t4,$t4
	srlx	$t3,32,@acc[4]
	add	@acc[5],$t5,$t5
	srlx	$t4,32,@acc[5]
	add	@acc[6],$t6,$t6
	srlx	$t5,32,@acc[6]
	add	@acc[7],$t7,$t7
	srlx	$t6,32,@acc[7]
	srlx	$t7,32,@acc[0]		! "@acc[8]"
___
}
$code.=<<___;
	addcc	@acc[1],$t1,@acc[1]	! accumulate high parts
	addccc	@acc[2],$t2,@acc[2]
	addccc	@acc[3],$t3,@acc[3]
	addccc	@acc[4],$t4,@acc[4]
	addccc	@acc[5],$t5,@acc[5]
	addccc	@acc[6],$t6,@acc[6]
	addccc	@acc[7],$t7,@acc[7]
	addccc	@acc[0],$carry,@acc[0]	! "@acc[8]"
	addc	%g0,%g0,$carry

	addcc	@acc[3],$t0,@acc[3]	! multiplication-less reduction
	addccc	@acc[4],%g0,@acc[4]
	addccc	@acc[5],%g0,@acc[5]
	addccc	@acc[6],$t0,@acc[6]
	addccc	@acc[7],%g0,@acc[7]
	addccc	@acc[0],$t0,@acc[0]	! "@acc[8]"
	addc	$carry,%g0,$carry
	subcc	@acc[7],$t0,@acc[7]
	subccc	@acc[0],%g0,@acc[0]	! "@acc[8]"
	subc	$carry,%g0,$carry	! top-most carry
___
	push(@acc,shift(@acc));		# rotate registers to omit acc[0]
$code.=<<___;
	! Final step is "if result > mod, subtract mod", but we do it
	! "other way around", namely subtract modulus from result
	! and if it borrowed, add modulus back.

	subcc	@acc[0],-1,@acc[0]	! subtract modulus
	subccc	@acc[1],-1,@acc[1]
	subccc	@acc[2],-1,@acc[2]
	subccc	@acc[3],0,@acc[3]
	subccc	@acc[4],0,@acc[4]
	subccc	@acc[5],0,@acc[5]
	subccc	@acc[6],1,@acc[6]
	subccc	@acc[7],-1,@acc[7]
	subc	$carry,0,$carry		! broadcast borrow bit

	! Note that because mod has special form, i.e. consists of
	! 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	! using value of broadcasted borrow and the borrow bit itself.
	! To minimize dependency chain we first broadcast and then
	! extract the bit by negating (follow $bi).

	addcc	@acc[0],$carry,@acc[0]	! add modulus or zero
	addccc	@acc[1],$carry,@acc[1]
	neg	$carry,$bi
	st	@acc[0],[$rp]
	addccc	@acc[2],$carry,@acc[2]
	st	@acc[1],[$rp+4]
	addccc	@acc[3],0,@acc[3]
	st	@acc[2],[$rp+8]
	addccc	@acc[4],0,@acc[4]
	st	@acc[3],[$rp+12]
	addccc	@acc[5],0,@acc[5]
	st	@acc[4],[$rp+16]
	addccc	@acc[6],$bi,@acc[6]
	st	@acc[5],[$rp+20]
	addc	@acc[7],$carry,@acc[7]
	st	@acc[6],[$rp+24]
	retl
	st	@acc[7],[$rp+28]
.type	__ecp_nistz256_mul_mont,#function
.size	__ecp_nistz256_mul_mont,.-__ecp_nistz256_mul_mont

! void	ecp_nistz256_add(BN_ULONG %i0[8],const BN_ULONG %i1[8],
!					 const BN_ULONG %i2[8]);
.globl	ecp_nistz256_add
.align	32
ecp_nistz256_add:
	save	%sp,-STACK_FRAME,%sp
	ld	[$ap],@acc[0]
	ld	[$ap+4],@acc[1]
	ld	[$ap+8],@acc[2]
	ld	[$ap+12],@acc[3]
	ld	[$ap+16],@acc[4]
	ld	[$ap+20],@acc[5]
	ld	[$ap+24],@acc[6]
	call	__ecp_nistz256_add
	ld	[$ap+28],@acc[7]
	ret
	restore
.type	ecp_nistz256_add,#function
.size	ecp_nistz256_add,.-ecp_nistz256_add

.align	32
__ecp_nistz256_add:
	ld	[$bp+0],$t0		! b[0]
	ld	[$bp+4],$t1
	ld	[$bp+8],$t2
	ld	[$bp+12],$t3
	addcc	@acc[0],$t0,@acc[0]
	ld	[$bp+16],$t4
	ld	[$bp+20],$t5
	addccc	@acc[1],$t1,@acc[1]
	ld	[$bp+24],$t6
	ld	[$bp+28],$t7
	addccc	@acc[2],$t2,@acc[2]
	addccc	@acc[3],$t3,@acc[3]
	addccc	@acc[4],$t4,@acc[4]
	addccc	@acc[5],$t5,@acc[5]
	addccc	@acc[6],$t6,@acc[6]
	addccc	@acc[7],$t7,@acc[7]
	addc	%g0,%g0,$carry

.Lreduce_by_sub:

	! if a+b >= modulus, subtract modulus.
	!
	! But since comparison implies subtraction, we subtract
	! modulus and then add it back if subtraction borrowed.

	subcc	@acc[0],-1,@acc[0]
	subccc	@acc[1],-1,@acc[1]
	subccc	@acc[2],-1,@acc[2]
	subccc	@acc[3], 0,@acc[3]
	subccc	@acc[4], 0,@acc[4]
	subccc	@acc[5], 0,@acc[5]
	subccc	@acc[6], 1,@acc[6]
	subccc	@acc[7],-1,@acc[7]
	subc	$carry,0,$carry

	! Note that because mod has special form, i.e. consists of
	! 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	! using value of borrow and its negative.

	addcc	@acc[0],$carry,@acc[0]	! add synthesized modulus
	addccc	@acc[1],$carry,@acc[1]
	neg	$carry,$bi
	st	@acc[0],[$rp]
	addccc	@acc[2],$carry,@acc[2]
	st	@acc[1],[$rp+4]
	addccc	@acc[3],0,@acc[3]
	st	@acc[2],[$rp+8]
	addccc	@acc[4],0,@acc[4]
	st	@acc[3],[$rp+12]
	addccc	@acc[5],0,@acc[5]
	st	@acc[4],[$rp+16]
	addccc	@acc[6],$bi,@acc[6]
	st	@acc[5],[$rp+20]
	addc	@acc[7],$carry,@acc[7]
	st	@acc[6],[$rp+24]
	retl
	st	@acc[7],[$rp+28]
.type	__ecp_nistz256_add,#function
.size	__ecp_nistz256_add,.-__ecp_nistz256_add

! void	ecp_nistz256_mul_by_2(BN_ULONG %i0[8],const BN_ULONG %i1[8]);
.globl	ecp_nistz256_mul_by_2
.align	32
ecp_nistz256_mul_by_2:
	save	%sp,-STACK_FRAME,%sp
	ld	[$ap],@acc[0]
	ld	[$ap+4],@acc[1]
	ld	[$ap+8],@acc[2]
	ld	[$ap+12],@acc[3]
	ld	[$ap+16],@acc[4]
	ld	[$ap+20],@acc[5]
	ld	[$ap+24],@acc[6]
	call	__ecp_nistz256_mul_by_2
	ld	[$ap+28],@acc[7]
	ret
	restore
.type	ecp_nistz256_mul_by_2,#function
.size	ecp_nistz256_mul_by_2,.-ecp_nistz256_mul_by_2

.align	32
__ecp_nistz256_mul_by_2:
	addcc	@acc[0],@acc[0],@acc[0]	! a+a=2*a
	addccc	@acc[1],@acc[1],@acc[1]
	addccc	@acc[2],@acc[2],@acc[2]
	addccc	@acc[3],@acc[3],@acc[3]
	addccc	@acc[4],@acc[4],@acc[4]
	addccc	@acc[5],@acc[5],@acc[5]
	addccc	@acc[6],@acc[6],@acc[6]
	addccc	@acc[7],@acc[7],@acc[7]
	b	.Lreduce_by_sub
	addc	%g0,%g0,$carry
.type	__ecp_nistz256_mul_by_2,#function
.size	__ecp_nistz256_mul_by_2,.-__ecp_nistz256_mul_by_2

! void	ecp_nistz256_mul_by_3(BN_ULONG %i0[8],const BN_ULONG %i1[8]);
.globl	ecp_nistz256_mul_by_3
.align	32
ecp_nistz256_mul_by_3:
	save	%sp,-STACK_FRAME,%sp
	ld	[$ap],@acc[0]
	ld	[$ap+4],@acc[1]
	ld	[$ap+8],@acc[2]
	ld	[$ap+12],@acc[3]
	ld	[$ap+16],@acc[4]
	ld	[$ap+20],@acc[5]
	ld	[$ap+24],@acc[6]
	call	__ecp_nistz256_mul_by_3
	ld	[$ap+28],@acc[7]
	ret
	restore
.type	ecp_nistz256_mul_by_3,#function
.size	ecp_nistz256_mul_by_3,.-ecp_nistz256_mul_by_3

.align	32
__ecp_nistz256_mul_by_3:
	addcc	@acc[0],@acc[0],$t0	! a+a=2*a
	addccc	@acc[1],@acc[1],$t1
	addccc	@acc[2],@acc[2],$t2
	addccc	@acc[3],@acc[3],$t3
	addccc	@acc[4],@acc[4],$t4
	addccc	@acc[5],@acc[5],$t5
	addccc	@acc[6],@acc[6],$t6
	addccc	@acc[7],@acc[7],$t7
	addc	%g0,%g0,$carry

	subcc	$t0,-1,$t0		! .Lreduce_by_sub but without stores
	subccc	$t1,-1,$t1
	subccc	$t2,-1,$t2
	subccc	$t3, 0,$t3
	subccc	$t4, 0,$t4
	subccc	$t5, 0,$t5
	subccc	$t6, 1,$t6
	subccc	$t7,-1,$t7
	subc	$carry,0,$carry

	addcc	$t0,$carry,$t0		! add synthesized modulus
	addccc	$t1,$carry,$t1
	neg	$carry,$bi
	addccc	$t2,$carry,$t2
	addccc	$t3,0,$t3
	addccc	$t4,0,$t4
	addccc	$t5,0,$t5
	addccc	$t6,$bi,$t6
	addc	$t7,$carry,$t7

	addcc	$t0,@acc[0],@acc[0]	! 2*a+a=3*a
	addccc	$t1,@acc[1],@acc[1]
	addccc	$t2,@acc[2],@acc[2]
	addccc	$t3,@acc[3],@acc[3]
	addccc	$t4,@acc[4],@acc[4]
	addccc	$t5,@acc[5],@acc[5]
	addccc	$t6,@acc[6],@acc[6]
	addccc	$t7,@acc[7],@acc[7]
	b	.Lreduce_by_sub
	addc	%g0,%g0,$carry
.type	__ecp_nistz256_mul_by_3,#function
.size	__ecp_nistz256_mul_by_3,.-__ecp_nistz256_mul_by_3

! void	ecp_nistz256_sub(BN_ULONG %i0[8],const BN_ULONG %i1[8],
!				         const BN_ULONG %i2[8]);
.globl	ecp_nistz256_sub
.align	32
ecp_nistz256_sub:
	save	%sp,-STACK_FRAME,%sp
	ld	[$ap],@acc[0]
	ld	[$ap+4],@acc[1]
	ld	[$ap+8],@acc[2]
	ld	[$ap+12],@acc[3]
	ld	[$ap+16],@acc[4]
	ld	[$ap+20],@acc[5]
	ld	[$ap+24],@acc[6]
	call	__ecp_nistz256_sub_from
	ld	[$ap+28],@acc[7]
	ret
	restore
.type	ecp_nistz256_sub,#function
.size	ecp_nistz256_sub,.-ecp_nistz256_sub

! void	ecp_nistz256_neg(BN_ULONG %i0[8],const BN_ULONG %i1[8]);
.globl	ecp_nistz256_neg
.align	32
ecp_nistz256_neg:
	save	%sp,-STACK_FRAME,%sp
	mov	$ap,$bp
	mov	0,@acc[0]
	mov	0,@acc[1]
	mov	0,@acc[2]
	mov	0,@acc[3]
	mov	0,@acc[4]
	mov	0,@acc[5]
	mov	0,@acc[6]
	call	__ecp_nistz256_sub_from
	mov	0,@acc[7]
	ret
	restore
.type	ecp_nistz256_neg,#function
.size	ecp_nistz256_neg,.-ecp_nistz256_neg

.align	32
__ecp_nistz256_sub_from:
	ld	[$bp+0],$t0		! b[0]
	ld	[$bp+4],$t1
	ld	[$bp+8],$t2
	ld	[$bp+12],$t3
	subcc	@acc[0],$t0,@acc[0]
	ld	[$bp+16],$t4
	ld	[$bp+20],$t5
	subccc	@acc[1],$t1,@acc[1]
	subccc	@acc[2],$t2,@acc[2]
	ld	[$bp+24],$t6
	ld	[$bp+28],$t7
	subccc	@acc[3],$t3,@acc[3]
	subccc	@acc[4],$t4,@acc[4]
	subccc	@acc[5],$t5,@acc[5]
	subccc	@acc[6],$t6,@acc[6]
	subccc	@acc[7],$t7,@acc[7]
	subc	%g0,%g0,$carry		! broadcast borrow bit

.Lreduce_by_add:

	! if a-b borrows, add modulus.
	!
	! Note that because mod has special form, i.e. consists of
	! 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	! using value of broadcasted borrow and the borrow bit itself.
	! To minimize dependency chain we first broadcast and then
	! extract the bit by negating (follow $bi).

	addcc	@acc[0],$carry,@acc[0]	! add synthesized modulus
	addccc	@acc[1],$carry,@acc[1]
	neg	$carry,$bi
	st	@acc[0],[$rp]
	addccc	@acc[2],$carry,@acc[2]
	st	@acc[1],[$rp+4]
	addccc	@acc[3],0,@acc[3]
	st	@acc[2],[$rp+8]
	addccc	@acc[4],0,@acc[4]
	st	@acc[3],[$rp+12]
	addccc	@acc[5],0,@acc[5]
	st	@acc[4],[$rp+16]
	addccc	@acc[6],$bi,@acc[6]
	st	@acc[5],[$rp+20]
	addc	@acc[7],$carry,@acc[7]
	st	@acc[6],[$rp+24]
	retl
	st	@acc[7],[$rp+28]
.type	__ecp_nistz256_sub_from,#function
.size	__ecp_nistz256_sub_from,.-__ecp_nistz256_sub_from

.align	32
__ecp_nistz256_sub_morf:
	ld	[$bp+0],$t0		! b[0]
	ld	[$bp+4],$t1
	ld	[$bp+8],$t2
	ld	[$bp+12],$t3
	subcc	$t0,@acc[0],@acc[0]
	ld	[$bp+16],$t4
	ld	[$bp+20],$t5
	subccc	$t1,@acc[1],@acc[1]
	subccc	$t2,@acc[2],@acc[2]
	ld	[$bp+24],$t6
	ld	[$bp+28],$t7
	subccc	$t3,@acc[3],@acc[3]
	subccc	$t4,@acc[4],@acc[4]
	subccc	$t5,@acc[5],@acc[5]
	subccc	$t6,@acc[6],@acc[6]
	subccc	$t7,@acc[7],@acc[7]
	b	.Lreduce_by_add
	subc	%g0,%g0,$carry		! broadcast borrow bit
.type	__ecp_nistz256_sub_morf,#function
.size	__ecp_nistz256_sub_morf,.-__ecp_nistz256_sub_morf

! void	ecp_nistz256_div_by_2(BN_ULONG %i0[8],const BN_ULONG %i1[8]);
.globl	ecp_nistz256_div_by_2
.align	32
ecp_nistz256_div_by_2:
	save	%sp,-STACK_FRAME,%sp
	ld	[$ap],@acc[0]
	ld	[$ap+4],@acc[1]
	ld	[$ap+8],@acc[2]
	ld	[$ap+12],@acc[3]
	ld	[$ap+16],@acc[4]
	ld	[$ap+20],@acc[5]
	ld	[$ap+24],@acc[6]
	call	__ecp_nistz256_div_by_2
	ld	[$ap+28],@acc[7]
	ret
	restore
.type	ecp_nistz256_div_by_2,#function
.size	ecp_nistz256_div_by_2,.-ecp_nistz256_div_by_2

.align	32
__ecp_nistz256_div_by_2:
	! ret = (a is odd ? a+mod : a) >> 1

	and	@acc[0],1,$bi
	neg	$bi,$carry
	addcc	@acc[0],$carry,@acc[0]
	addccc	@acc[1],$carry,@acc[1]
	addccc	@acc[2],$carry,@acc[2]
	addccc	@acc[3],0,@acc[3]
	addccc	@acc[4],0,@acc[4]
	addccc	@acc[5],0,@acc[5]
	addccc	@acc[6],$bi,@acc[6]
	addccc	@acc[7],$carry,@acc[7]
	addc	%g0,%g0,$carry

	! ret >>= 1

	srl	@acc[0],1,@acc[0]
	sll	@acc[1],31,$t0
	srl	@acc[1],1,@acc[1]
	or	@acc[0],$t0,@acc[0]
	sll	@acc[2],31,$t1
	srl	@acc[2],1,@acc[2]
	or	@acc[1],$t1,@acc[1]
	sll	@acc[3],31,$t2
	st	@acc[0],[$rp]
	srl	@acc[3],1,@acc[3]
	or	@acc[2],$t2,@acc[2]
	sll	@acc[4],31,$t3
	st	@acc[1],[$rp+4]
	srl	@acc[4],1,@acc[4]
	or	@acc[3],$t3,@acc[3]
	sll	@acc[5],31,$t4
	st	@acc[2],[$rp+8]
	srl	@acc[5],1,@acc[5]
	or	@acc[4],$t4,@acc[4]
	sll	@acc[6],31,$t5
	st	@acc[3],[$rp+12]
	srl	@acc[6],1,@acc[6]
	or	@acc[5],$t5,@acc[5]
	sll	@acc[7],31,$t6
	st	@acc[4],[$rp+16]
	srl	@acc[7],1,@acc[7]
	or	@acc[6],$t6,@acc[6]
	sll	$carry,31,$t7
	st	@acc[5],[$rp+20]
	or	@acc[7],$t7,@acc[7]
	st	@acc[6],[$rp+24]
	retl
	st	@acc[7],[$rp+28]
.type	__ecp_nistz256_div_by_2,#function
.size	__ecp_nistz256_div_by_2,.-__ecp_nistz256_div_by_2
___

########################################################################
# following subroutines are "literal" implementation of those found in
# ecp_nistz256.c
#
########################################################################
# void ecp_nistz256_point_double(P256_POINT *out,const P256_POINT *inp);
#
{
my ($S,$M,$Zsqr,$tmp0)=map(32*$_,(0..3));
# above map() describes stack layout with 4 temporary
# 256-bit vectors on top.

$code.=<<___;
#ifdef __PIC__
SPARC_PIC_THUNK(%g1)
#endif

.globl	ecp_nistz256_point_double
.align	32
ecp_nistz256_point_double:
	SPARC_LOAD_ADDRESS_LEAF(OPENSSL_sparcv9cap_P,%g1,%g5)
	ld	[%g1],%g1		! OPENSSL_sparcv9cap_P[0]
	and	%g1,(SPARCV9_VIS3|SPARCV9_64BIT_STACK),%g1
	cmp	%g1,(SPARCV9_VIS3|SPARCV9_64BIT_STACK)
	be	ecp_nistz256_point_double_vis3
	nop

	save	%sp,-STACK_FRAME-32*4,%sp

	mov	$rp,$rp_real
	mov	$ap,$ap_real

.Lpoint_double_shortcut:
	ld	[$ap+32],@acc[0]
	ld	[$ap+32+4],@acc[1]
	ld	[$ap+32+8],@acc[2]
	ld	[$ap+32+12],@acc[3]
	ld	[$ap+32+16],@acc[4]
	ld	[$ap+32+20],@acc[5]
	ld	[$ap+32+24],@acc[6]
	ld	[$ap+32+28],@acc[7]
	call	__ecp_nistz256_mul_by_2	! p256_mul_by_2(S, in_y);
	add	%sp,LOCALS+$S,$rp

	add	$ap_real,64,$bp
	add	$ap_real,64,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(Zsqr, in_z);
	add	%sp,LOCALS+$Zsqr,$rp

	add	$ap_real,0,$bp
	call	__ecp_nistz256_add	! p256_add(M, Zsqr, in_x);
	add	%sp,LOCALS+$M,$rp

	add	%sp,LOCALS+$S,$bp
	add	%sp,LOCALS+$S,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(S, S);
	add	%sp,LOCALS+$S,$rp

	ld	[$ap_real],@acc[0]
	add	%sp,LOCALS+$Zsqr,$bp
	ld	[$ap_real+4],@acc[1]
	ld	[$ap_real+8],@acc[2]
	ld	[$ap_real+12],@acc[3]
	ld	[$ap_real+16],@acc[4]
	ld	[$ap_real+20],@acc[5]
	ld	[$ap_real+24],@acc[6]
	ld	[$ap_real+28],@acc[7]
	call	__ecp_nistz256_sub_from	! p256_sub(Zsqr, in_x, Zsqr);
	add	%sp,LOCALS+$Zsqr,$rp

	add	$ap_real,32,$bp
	add	$ap_real,64,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(tmp0, in_z, in_y);
	add	%sp,LOCALS+$tmp0,$rp

	call	__ecp_nistz256_mul_by_2	! p256_mul_by_2(res_z, tmp0);
	add	$rp_real,64,$rp

	add	%sp,LOCALS+$Zsqr,$bp
	add	%sp,LOCALS+$M,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(M, M, Zsqr);
	add	%sp,LOCALS+$M,$rp

	call	__ecp_nistz256_mul_by_3	! p256_mul_by_3(M, M);
	add	%sp,LOCALS+$M,$rp

	add	%sp,LOCALS+$S,$bp
	add	%sp,LOCALS+$S,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(tmp0, S);
	add	%sp,LOCALS+$tmp0,$rp

	call	__ecp_nistz256_div_by_2	! p256_div_by_2(res_y, tmp0);
	add	$rp_real,32,$rp

	add	$ap_real,0,$bp
	add	%sp,LOCALS+$S,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(S, S, in_x);
	add	%sp,LOCALS+$S,$rp

	call	__ecp_nistz256_mul_by_2	! p256_mul_by_2(tmp0, S);
	add	%sp,LOCALS+$tmp0,$rp

	add	%sp,LOCALS+$M,$bp
	add	%sp,LOCALS+$M,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(res_x, M);
	add	$rp_real,0,$rp

	add	%sp,LOCALS+$tmp0,$bp
	call	__ecp_nistz256_sub_from	! p256_sub(res_x, res_x, tmp0);
	add	$rp_real,0,$rp

	add	%sp,LOCALS+$S,$bp
	call	__ecp_nistz256_sub_morf	! p256_sub(S, S, res_x);
	add	%sp,LOCALS+$S,$rp

	add	%sp,LOCALS+$M,$bp
	add	%sp,LOCALS+$S,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(S, S, M);
	add	%sp,LOCALS+$S,$rp

	add	$rp_real,32,$bp
	call	__ecp_nistz256_sub_from	! p256_sub(res_y, S, res_y);
	add	$rp_real,32,$rp

	ret
	restore
.type	ecp_nistz256_point_double,#function
.size	ecp_nistz256_point_double,.-ecp_nistz256_point_double
___
}

########################################################################
# void ecp_nistz256_point_add(P256_POINT *out,const P256_POINT *in1,
#			      const P256_POINT *in2);
{
my ($res_x,$res_y,$res_z,
    $H,$Hsqr,$R,$Rsqr,$Hcub,
    $U1,$U2,$S1,$S2)=map(32*$_,(0..11));
my ($Z1sqr, $Z2sqr) = ($Hsqr, $Rsqr);

# above map() describes stack layout with 12 temporary
# 256-bit vectors on top. Then we reserve some space for
# !in1infty, !in2infty, result of check for zero and return pointer.

my $bp_real=$rp_real;

$code.=<<___;
.globl	ecp_nistz256_point_add
.align	32
ecp_nistz256_point_add:
	SPARC_LOAD_ADDRESS_LEAF(OPENSSL_sparcv9cap_P,%g1,%g5)
	ld	[%g1],%g1		! OPENSSL_sparcv9cap_P[0]
	and	%g1,(SPARCV9_VIS3|SPARCV9_64BIT_STACK),%g1
	cmp	%g1,(SPARCV9_VIS3|SPARCV9_64BIT_STACK)
	be	ecp_nistz256_point_add_vis3
	nop

	save	%sp,-STACK_FRAME-32*12-32,%sp

	stx	$rp,[%fp+STACK_BIAS-8]	! off-load $rp
	mov	$ap,$ap_real
	mov	$bp,$bp_real

	ld	[$bp+64],$t0		! in2_z
	ld	[$bp+64+4],$t1
	ld	[$bp+64+8],$t2
	ld	[$bp+64+12],$t3
	ld	[$bp+64+16],$t4
	ld	[$bp+64+20],$t5
	ld	[$bp+64+24],$t6
	ld	[$bp+64+28],$t7
	or	$t1,$t0,$t0
	or	$t3,$t2,$t2
	or	$t5,$t4,$t4
	or	$t7,$t6,$t6
	or	$t2,$t0,$t0
	or	$t6,$t4,$t4
	or	$t4,$t0,$t0		! !in2infty
	movrnz	$t0,-1,$t0
	st	$t0,[%fp+STACK_BIAS-12]

	ld	[$ap+64],$t0		! in1_z
	ld	[$ap+64+4],$t1
	ld	[$ap+64+8],$t2
	ld	[$ap+64+12],$t3
	ld	[$ap+64+16],$t4
	ld	[$ap+64+20],$t5
	ld	[$ap+64+24],$t6
	ld	[$ap+64+28],$t7
	or	$t1,$t0,$t0
	or	$t3,$t2,$t2
	or	$t5,$t4,$t4
	or	$t7,$t6,$t6
	or	$t2,$t0,$t0
	or	$t6,$t4,$t4
	or	$t4,$t0,$t0		! !in1infty
	movrnz	$t0,-1,$t0
	st	$t0,[%fp+STACK_BIAS-16]

	add	$bp_real,64,$bp
	add	$bp_real,64,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(Z2sqr, in2_z);
	add	%sp,LOCALS+$Z2sqr,$rp

	add	$ap_real,64,$bp
	add	$ap_real,64,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(Z1sqr, in1_z);
	add	%sp,LOCALS+$Z1sqr,$rp

	add	$bp_real,64,$bp
	add	%sp,LOCALS+$Z2sqr,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(S1, Z2sqr, in2_z);
	add	%sp,LOCALS+$S1,$rp

	add	$ap_real,64,$bp
	add	%sp,LOCALS+$Z1sqr,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(S2, Z1sqr, in1_z);
	add	%sp,LOCALS+$S2,$rp

	add	$ap_real,32,$bp
	add	%sp,LOCALS+$S1,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(S1, S1, in1_y);
	add	%sp,LOCALS+$S1,$rp

	add	$bp_real,32,$bp
	add	%sp,LOCALS+$S2,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(S2, S2, in2_y);
	add	%sp,LOCALS+$S2,$rp

	add	%sp,LOCALS+$S1,$bp
	call	__ecp_nistz256_sub_from	! p256_sub(R, S2, S1);
	add	%sp,LOCALS+$R,$rp

	or	@acc[1],@acc[0],@acc[0]	! see if result is zero
	or	@acc[3],@acc[2],@acc[2]
	or	@acc[5],@acc[4],@acc[4]
	or	@acc[7],@acc[6],@acc[6]
	or	@acc[2],@acc[0],@acc[0]
	or	@acc[6],@acc[4],@acc[4]
	or	@acc[4],@acc[0],@acc[0]
	st	@acc[0],[%fp+STACK_BIAS-20]

	add	$ap_real,0,$bp
	add	%sp,LOCALS+$Z2sqr,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(U1, in1_x, Z2sqr);
	add	%sp,LOCALS+$U1,$rp

	add	$bp_real,0,$bp
	add	%sp,LOCALS+$Z1sqr,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(U2, in2_x, Z1sqr);
	add	%sp,LOCALS+$U2,$rp

	add	%sp,LOCALS+$U1,$bp
	call	__ecp_nistz256_sub_from	! p256_sub(H, U2, U1);
	add	%sp,LOCALS+$H,$rp

	or	@acc[1],@acc[0],@acc[0]	! see if result is zero
	or	@acc[3],@acc[2],@acc[2]
	or	@acc[5],@acc[4],@acc[4]
	or	@acc[7],@acc[6],@acc[6]
	or	@acc[2],@acc[0],@acc[0]
	or	@acc[6],@acc[4],@acc[4]
	orcc	@acc[4],@acc[0],@acc[0]

	bne,pt	%icc,.Ladd_proceed	! is_equal(U1,U2)?
	nop

	ld	[%fp+STACK_BIAS-12],$t0
	ld	[%fp+STACK_BIAS-16],$t1
	ld	[%fp+STACK_BIAS-20],$t2
	andcc	$t0,$t1,%g0
	be,pt	%icc,.Ladd_proceed	! (in1infty || in2infty)?
	nop
	andcc	$t2,$t2,%g0
	be,pt	%icc,.Ladd_double	! is_equal(S1,S2)?
	nop

	ldx	[%fp+STACK_BIAS-8],$rp
	st	%g0,[$rp]
	st	%g0,[$rp+4]
	st	%g0,[$rp+8]
	st	%g0,[$rp+12]
	st	%g0,[$rp+16]
	st	%g0,[$rp+20]
	st	%g0,[$rp+24]
	st	%g0,[$rp+28]
	st	%g0,[$rp+32]
	st	%g0,[$rp+32+4]
	st	%g0,[$rp+32+8]
	st	%g0,[$rp+32+12]
	st	%g0,[$rp+32+16]
	st	%g0,[$rp+32+20]
	st	%g0,[$rp+32+24]
	st	%g0,[$rp+32+28]
	st	%g0,[$rp+64]
	st	%g0,[$rp+64+4]
	st	%g0,[$rp+64+8]
	st	%g0,[$rp+64+12]
	st	%g0,[$rp+64+16]
	st	%g0,[$rp+64+20]
	st	%g0,[$rp+64+24]
	st	%g0,[$rp+64+28]
	b	.Ladd_done
	nop

.align	16
.Ladd_double:
	ldx	[%fp+STACK_BIAS-8],$rp_real
	mov	$ap_real,$ap
	b	.Lpoint_double_shortcut
	add	%sp,32*(12-4)+32,%sp	! difference in frame sizes

.align	16
.Ladd_proceed:
	add	%sp,LOCALS+$R,$bp
	add	%sp,LOCALS+$R,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(Rsqr, R);
	add	%sp,LOCALS+$Rsqr,$rp

	add	$ap_real,64,$bp
	add	%sp,LOCALS+$H,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(res_z, H, in1_z);
	add	%sp,LOCALS+$res_z,$rp

	add	%sp,LOCALS+$H,$bp
	add	%sp,LOCALS+$H,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(Hsqr, H);
	add	%sp,LOCALS+$Hsqr,$rp

	add	$bp_real,64,$bp
	add	%sp,LOCALS+$res_z,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(res_z, res_z, in2_z);
	add	%sp,LOCALS+$res_z,$rp

	add	%sp,LOCALS+$H,$bp
	add	%sp,LOCALS+$Hsqr,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(Hcub, Hsqr, H);
	add	%sp,LOCALS+$Hcub,$rp

	add	%sp,LOCALS+$U1,$bp
	add	%sp,LOCALS+$Hsqr,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(U2, U1, Hsqr);
	add	%sp,LOCALS+$U2,$rp

	call	__ecp_nistz256_mul_by_2	! p256_mul_by_2(Hsqr, U2);
	add	%sp,LOCALS+$Hsqr,$rp

	add	%sp,LOCALS+$Rsqr,$bp
	call	__ecp_nistz256_sub_morf	! p256_sub(res_x, Rsqr, Hsqr);
	add	%sp,LOCALS+$res_x,$rp

	add	%sp,LOCALS+$Hcub,$bp
	call	__ecp_nistz256_sub_from	!  p256_sub(res_x, res_x, Hcub);
	add	%sp,LOCALS+$res_x,$rp

	add	%sp,LOCALS+$U2,$bp
	call	__ecp_nistz256_sub_morf	! p256_sub(res_y, U2, res_x);
	add	%sp,LOCALS+$res_y,$rp

	add	%sp,LOCALS+$Hcub,$bp
	add	%sp,LOCALS+$S1,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(S2, S1, Hcub);
	add	%sp,LOCALS+$S2,$rp

	add	%sp,LOCALS+$R,$bp
	add	%sp,LOCALS+$res_y,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(res_y, res_y, R);
	add	%sp,LOCALS+$res_y,$rp

	add	%sp,LOCALS+$S2,$bp
	call	__ecp_nistz256_sub_from	! p256_sub(res_y, res_y, S2);
	add	%sp,LOCALS+$res_y,$rp

	ld	[%fp+STACK_BIAS-16],$t1	! !in1infty
	ld	[%fp+STACK_BIAS-12],$t2	! !in2infty
	ldx	[%fp+STACK_BIAS-8],$rp
___
for($i=0;$i<96;$i+=8) {			# conditional moves
$code.=<<___;
	ld	[%sp+LOCALS+$i],@acc[0]		! res
	ld	[%sp+LOCALS+$i+4],@acc[1]
	ld	[$bp_real+$i],@acc[2]		! in2
	ld	[$bp_real+$i+4],@acc[3]
	ld	[$ap_real+$i],@acc[4]		! in1
	ld	[$ap_real+$i+4],@acc[5]
	movrz	$t1,@acc[2],@acc[0]
	movrz	$t1,@acc[3],@acc[1]
	movrz	$t2,@acc[4],@acc[0]
	movrz	$t2,@acc[5],@acc[1]
	st	@acc[0],[$rp+$i]
	st	@acc[1],[$rp+$i+4]
___
}
$code.=<<___;
.Ladd_done:
	ret
	restore
.type	ecp_nistz256_point_add,#function
.size	ecp_nistz256_point_add,.-ecp_nistz256_point_add
___
}

########################################################################
# void ecp_nistz256_point_add_affine(P256_POINT *out,const P256_POINT *in1,
#				     const P256_POINT_AFFINE *in2);
{
my ($res_x,$res_y,$res_z,
    $U2,$S2,$H,$R,$Hsqr,$Hcub,$Rsqr)=map(32*$_,(0..9));
my $Z1sqr = $S2;
# above map() describes stack layout with 10 temporary
# 256-bit vectors on top. Then we reserve some space for
# !in1infty, !in2infty, result of check for zero and return pointer.

my @ONE_mont=(1,0,0,-1,-1,-1,-2,0);
my $bp_real=$rp_real;

$code.=<<___;
.globl	ecp_nistz256_point_add_affine
.align	32
ecp_nistz256_point_add_affine:
	SPARC_LOAD_ADDRESS_LEAF(OPENSSL_sparcv9cap_P,%g1,%g5)
	ld	[%g1],%g1		! OPENSSL_sparcv9cap_P[0]
	and	%g1,(SPARCV9_VIS3|SPARCV9_64BIT_STACK),%g1
	cmp	%g1,(SPARCV9_VIS3|SPARCV9_64BIT_STACK)
	be	ecp_nistz256_point_add_affine_vis3
	nop

	save	%sp,-STACK_FRAME-32*10-32,%sp

	stx	$rp,[%fp+STACK_BIAS-8]	! off-load $rp
	mov	$ap,$ap_real
	mov	$bp,$bp_real

	ld	[$ap+64],$t0		! in1_z
	ld	[$ap+64+4],$t1
	ld	[$ap+64+8],$t2
	ld	[$ap+64+12],$t3
	ld	[$ap+64+16],$t4
	ld	[$ap+64+20],$t5
	ld	[$ap+64+24],$t6
	ld	[$ap+64+28],$t7
	or	$t1,$t0,$t0
	or	$t3,$t2,$t2
	or	$t5,$t4,$t4
	or	$t7,$t6,$t6
	or	$t2,$t0,$t0
	or	$t6,$t4,$t4
	or	$t4,$t0,$t0		! !in1infty
	movrnz	$t0,-1,$t0
	st	$t0,[%fp+STACK_BIAS-16]

	ld	[$bp],@acc[0]		! in2_x
	ld	[$bp+4],@acc[1]
	ld	[$bp+8],@acc[2]
	ld	[$bp+12],@acc[3]
	ld	[$bp+16],@acc[4]
	ld	[$bp+20],@acc[5]
	ld	[$bp+24],@acc[6]
	ld	[$bp+28],@acc[7]
	ld	[$bp+32],$t0		! in2_y
	ld	[$bp+32+4],$t1
	ld	[$bp+32+8],$t2
	ld	[$bp+32+12],$t3
	ld	[$bp+32+16],$t4
	ld	[$bp+32+20],$t5
	ld	[$bp+32+24],$t6
	ld	[$bp+32+28],$t7
	or	@acc[1],@acc[0],@acc[0]
	or	@acc[3],@acc[2],@acc[2]
	or	@acc[5],@acc[4],@acc[4]
	or	@acc[7],@acc[6],@acc[6]
	or	@acc[2],@acc[0],@acc[0]
	or	@acc[6],@acc[4],@acc[4]
	or	@acc[4],@acc[0],@acc[0]
	or	$t1,$t0,$t0
	or	$t3,$t2,$t2
	or	$t5,$t4,$t4
	or	$t7,$t6,$t6
	or	$t2,$t0,$t0
	or	$t6,$t4,$t4
	or	$t4,$t0,$t0
	or	@acc[0],$t0,$t0		! !in2infty
	movrnz	$t0,-1,$t0
	st	$t0,[%fp+STACK_BIAS-12]

	add	$ap_real,64,$bp
	add	$ap_real,64,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(Z1sqr, in1_z);
	add	%sp,LOCALS+$Z1sqr,$rp

	add	$bp_real,0,$bp
	add	%sp,LOCALS+$Z1sqr,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(U2, Z1sqr, in2_x);
	add	%sp,LOCALS+$U2,$rp

	add	$ap_real,0,$bp
	call	__ecp_nistz256_sub_from	! p256_sub(H, U2, in1_x);
	add	%sp,LOCALS+$H,$rp

	add	$ap_real,64,$bp
	add	%sp,LOCALS+$Z1sqr,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(S2, Z1sqr, in1_z);
	add	%sp,LOCALS+$S2,$rp

	add	$ap_real,64,$bp
	add	%sp,LOCALS+$H,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(res_z, H, in1_z);
	add	%sp,LOCALS+$res_z,$rp

	add	$bp_real,32,$bp
	add	%sp,LOCALS+$S2,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(S2, S2, in2_y);
	add	%sp,LOCALS+$S2,$rp

	add	$ap_real,32,$bp
	call	__ecp_nistz256_sub_from	! p256_sub(R, S2, in1_y);
	add	%sp,LOCALS+$R,$rp

	add	%sp,LOCALS+$H,$bp
	add	%sp,LOCALS+$H,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(Hsqr, H);
	add	%sp,LOCALS+$Hsqr,$rp

	add	%sp,LOCALS+$R,$bp
	add	%sp,LOCALS+$R,$ap
	call	__ecp_nistz256_mul_mont	! p256_sqr_mont(Rsqr, R);
	add	%sp,LOCALS+$Rsqr,$rp

	add	%sp,LOCALS+$H,$bp
	add	%sp,LOCALS+$Hsqr,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(Hcub, Hsqr, H);
	add	%sp,LOCALS+$Hcub,$rp

	add	$ap_real,0,$bp
	add	%sp,LOCALS+$Hsqr,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(U2, in1_x, Hsqr);
	add	%sp,LOCALS+$U2,$rp

	call	__ecp_nistz256_mul_by_2	! p256_mul_by_2(Hsqr, U2);
	add	%sp,LOCALS+$Hsqr,$rp

	add	%sp,LOCALS+$Rsqr,$bp
	call	__ecp_nistz256_sub_morf	! p256_sub(res_x, Rsqr, Hsqr);
	add	%sp,LOCALS+$res_x,$rp

	add	%sp,LOCALS+$Hcub,$bp
	call	__ecp_nistz256_sub_from	!  p256_sub(res_x, res_x, Hcub);
	add	%sp,LOCALS+$res_x,$rp

	add	%sp,LOCALS+$U2,$bp
	call	__ecp_nistz256_sub_morf	! p256_sub(res_y, U2, res_x);
	add	%sp,LOCALS+$res_y,$rp

	add	$ap_real,32,$bp
	add	%sp,LOCALS+$Hcub,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(S2, in1_y, Hcub);
	add	%sp,LOCALS+$S2,$rp

	add	%sp,LOCALS+$R,$bp
	add	%sp,LOCALS+$res_y,$ap
	call	__ecp_nistz256_mul_mont	! p256_mul_mont(res_y, res_y, R);
	add	%sp,LOCALS+$res_y,$rp

	add	%sp,LOCALS+$S2,$bp
	call	__ecp_nistz256_sub_from	! p256_sub(res_y, res_y, S2);
	add	%sp,LOCALS+$res_y,$rp

	ld	[%fp+STACK_BIAS-16],$t1	! !in1infty
	ld	[%fp+STACK_BIAS-12],$t2	! !in2infty
	ldx	[%fp+STACK_BIAS-8],$rp
___
for($i=0;$i<64;$i+=8) {			# conditional moves
$code.=<<___;
	ld	[%sp+LOCALS+$i],@acc[0]		! res
	ld	[%sp+LOCALS+$i+4],@acc[1]
	ld	[$bp_real+$i],@acc[2]		! in2
	ld	[$bp_real+$i+4],@acc[3]
	ld	[$ap_real+$i],@acc[4]		! in1
	ld	[$ap_real+$i+4],@acc[5]
	movrz	$t1,@acc[2],@acc[0]
	movrz	$t1,@acc[3],@acc[1]
	movrz	$t2,@acc[4],@acc[0]
	movrz	$t2,@acc[5],@acc[1]
	st	@acc[0],[$rp+$i]
	st	@acc[1],[$rp+$i+4]
___
}
for(;$i<96;$i+=8) {
my $j=($i-64)/4;
$code.=<<___;
	ld	[%sp+LOCALS+$i],@acc[0]		! res
	ld	[%sp+LOCALS+$i+4],@acc[1]
	ld	[$ap_real+$i],@acc[4]		! in1
	ld	[$ap_real+$i+4],@acc[5]
	movrz	$t1,@ONE_mont[$j],@acc[0]
	movrz	$t1,@ONE_mont[$j+1],@acc[1]
	movrz	$t2,@acc[4],@acc[0]
	movrz	$t2,@acc[5],@acc[1]
	st	@acc[0],[$rp+$i]
	st	@acc[1],[$rp+$i+4]
___
}
$code.=<<___;
	ret
	restore
.type	ecp_nistz256_point_add_affine,#function
.size	ecp_nistz256_point_add_affine,.-ecp_nistz256_point_add_affine
___
}								}}}
{{{
my ($out,$inp,$index)=map("%i$_",(0..2));
my $mask="%o0";

$code.=<<___;
! void	ecp_nistz256_scatter_w5(void *%i0,const P256_POINT *%i1,
!					  int %i2);
.globl	ecp_nistz256_scatter_w5
.align	32
ecp_nistz256_scatter_w5:
	save	%sp,-STACK_FRAME,%sp

	sll	$index,2,$index
	add	$out,$index,$out

	ld	[$inp],%l0		! X
	ld	[$inp+4],%l1
	ld	[$inp+8],%l2
	ld	[$inp+12],%l3
	ld	[$inp+16],%l4
	ld	[$inp+20],%l5
	ld	[$inp+24],%l6
	ld	[$inp+28],%l7
	add	$inp,32,$inp
	st	%l0,[$out+64*0-4]
	st	%l1,[$out+64*1-4]
	st	%l2,[$out+64*2-4]
	st	%l3,[$out+64*3-4]
	st	%l4,[$out+64*4-4]
	st	%l5,[$out+64*5-4]
	st	%l6,[$out+64*6-4]
	st	%l7,[$out+64*7-4]
	add	$out,64*8,$out

	ld	[$inp],%l0		! Y
	ld	[$inp+4],%l1
	ld	[$inp+8],%l2
	ld	[$inp+12],%l3
	ld	[$inp+16],%l4
	ld	[$inp+20],%l5
	ld	[$inp+24],%l6
	ld	[$inp+28],%l7
	add	$inp,32,$inp
	st	%l0,[$out+64*0-4]
	st	%l1,[$out+64*1-4]
	st	%l2,[$out+64*2-4]
	st	%l3,[$out+64*3-4]
	st	%l4,[$out+64*4-4]
	st	%l5,[$out+64*5-4]
	st	%l6,[$out+64*6-4]
	st	%l7,[$out+64*7-4]
	add	$out,64*8,$out

	ld	[$inp],%l0		! Z
	ld	[$inp+4],%l1
	ld	[$inp+8],%l2
	ld	[$inp+12],%l3
	ld	[$inp+16],%l4
	ld	[$inp+20],%l5
	ld	[$inp+24],%l6
	ld	[$inp+28],%l7
	st	%l0,[$out+64*0-4]
	st	%l1,[$out+64*1-4]
	st	%l2,[$out+64*2-4]
	st	%l3,[$out+64*3-4]
	st	%l4,[$out+64*4-4]
	st	%l5,[$out+64*5-4]
	st	%l6,[$out+64*6-4]
	st	%l7,[$out+64*7-4]

	ret
	restore
.type	ecp_nistz256_scatter_w5,#function
.size	ecp_nistz256_scatter_w5,.-ecp_nistz256_scatter_w5

! void	ecp_nistz256_gather_w5(P256_POINT *%i0,const void *%i1,
!					       int %i2);
.globl	ecp_nistz256_gather_w5
.align	32
ecp_nistz256_gather_w5:
	save	%sp,-STACK_FRAME,%sp

	neg	$index,$mask
	srax	$mask,63,$mask

	add	$index,$mask,$index
	sll	$index,2,$index
	add	$inp,$index,$inp

	ld	[$inp+64*0],%l0
	ld	[$inp+64*1],%l1
	ld	[$inp+64*2],%l2
	ld	[$inp+64*3],%l3
	ld	[$inp+64*4],%l4
	ld	[$inp+64*5],%l5
	ld	[$inp+64*6],%l6
	ld	[$inp+64*7],%l7
	add	$inp,64*8,$inp
	and	%l0,$mask,%l0
	and	%l1,$mask,%l1
	st	%l0,[$out]		! X
	and	%l2,$mask,%l2
	st	%l1,[$out+4]
	and	%l3,$mask,%l3
	st	%l2,[$out+8]
	and	%l4,$mask,%l4
	st	%l3,[$out+12]
	and	%l5,$mask,%l5
	st	%l4,[$out+16]
	and	%l6,$mask,%l6
	st	%l5,[$out+20]
	and	%l7,$mask,%l7
	st	%l6,[$out+24]
	st	%l7,[$out+28]
	add	$out,32,$out

	ld	[$inp+64*0],%l0
	ld	[$inp+64*1],%l1
	ld	[$inp+64*2],%l2
	ld	[$inp+64*3],%l3
	ld	[$inp+64*4],%l4
	ld	[$inp+64*5],%l5
	ld	[$inp+64*6],%l6
	ld	[$inp+64*7],%l7
	add	$inp,64*8,$inp
	and	%l0,$mask,%l0
	and	%l1,$mask,%l1
	st	%l0,[$out]		! Y
	and	%l2,$mask,%l2
	st	%l1,[$out+4]
	and	%l3,$mask,%l3
	st	%l2,[$out+8]
	and	%l4,$mask,%l4
	st	%l3,[$out+12]
	and	%l5,$mask,%l5
	st	%l4,[$out+16]
	and	%l6,$mask,%l6
	st	%l5,[$out+20]
	and	%l7,$mask,%l7
	st	%l6,[$out+24]
	st	%l7,[$out+28]
	add	$out,32,$out

	ld	[$inp+64*0],%l0
	ld	[$inp+64*1],%l1
	ld	[$inp+64*2],%l2
	ld	[$inp+64*3],%l3
	ld	[$inp+64*4],%l4
	ld	[$inp+64*5],%l5
	ld	[$inp+64*6],%l6
	ld	[$inp+64*7],%l7
	and	%l0,$mask,%l0
	and	%l1,$mask,%l1
	st	%l0,[$out]		! Z
	and	%l2,$mask,%l2
	st	%l1,[$out+4]
	and	%l3,$mask,%l3
	st	%l2,[$out+8]
	and	%l4,$mask,%l4
	st	%l3,[$out+12]
	and	%l5,$mask,%l5
	st	%l4,[$out+16]
	and	%l6,$mask,%l6
	st	%l5,[$out+20]
	and	%l7,$mask,%l7
	st	%l6,[$out+24]
	st	%l7,[$out+28]

	ret
	restore
.type	ecp_nistz256_gather_w5,#function
.size	ecp_nistz256_gather_w5,.-ecp_nistz256_gather_w5

! void	ecp_nistz256_scatter_w7(void *%i0,const P256_POINT_AFFINE *%i1,
!					  int %i2);
.globl	ecp_nistz256_scatter_w7
.align	32
ecp_nistz256_scatter_w7:
	save	%sp,-STACK_FRAME,%sp
	nop
	add	$out,$index,$out
	mov	64/4,$index
.Loop_scatter_w7:
	ld	[$inp],%l0
	add	$inp,4,$inp
	subcc	$index,1,$index
	stb	%l0,[$out+64*0]
	srl	%l0,8,%l1
	stb	%l1,[$out+64*1]
	srl	%l0,16,%l2
	stb	%l2,[$out+64*2]
	srl	%l0,24,%l3
	stb	%l3,[$out+64*3]
	bne	.Loop_scatter_w7
	add	$out,64*4,$out

	ret
	restore
.type	ecp_nistz256_scatter_w7,#function
.size	ecp_nistz256_scatter_w7,.-ecp_nistz256_scatter_w7

! void	ecp_nistz256_gather_w7(P256_POINT_AFFINE *%i0,const void *%i1,
!						      int %i2);
.globl	ecp_nistz256_gather_w7
.align	32
ecp_nistz256_gather_w7:
	save	%sp,-STACK_FRAME,%sp

	neg	$index,$mask
	srax	$mask,63,$mask

	add	$index,$mask,$index
	add	$inp,$index,$inp
	mov	64/4,$index

.Loop_gather_w7:
	ldub	[$inp+64*0],%l0
	prefetch [$inp+3840+64*0],1
	subcc	$index,1,$index
	ldub	[$inp+64*1],%l1
	prefetch [$inp+3840+64*1],1
	ldub	[$inp+64*2],%l2
	prefetch [$inp+3840+64*2],1
	ldub	[$inp+64*3],%l3
	prefetch [$inp+3840+64*3],1
	add	$inp,64*4,$inp
	sll	%l1,8,%l1
	sll	%l2,16,%l2
	or	%l0,%l1,%l0
	sll	%l3,24,%l3
	or	%l0,%l2,%l0
	or	%l0,%l3,%l0
	and	%l0,$mask,%l0
	st	%l0,[$out]
	bne	.Loop_gather_w7
	add	$out,4,$out

	ret
	restore
.type	ecp_nistz256_gather_w7,#function
.size	ecp_nistz256_gather_w7,.-ecp_nistz256_gather_w7
___
}}}
{{{
########################################################################
# Following subroutines are VIS3 counterparts of those above that
# implement ones found in ecp_nistz256.c. Key difference is that they
# use 128-bit multiplication and addition with 64-bit carry, and in order
# to do that they perform conversion from uin32_t[8] to uint64_t[4] upon
# entry and vice versa on return.
#
my ($rp,$ap,$bp)=map("%i$_",(0..2));
my ($t0,$t1,$t2,$t3,$a0,$a1,$a2,$a3)=map("%l$_",(0..7));
my ($acc0,$acc1,$acc2,$acc3,$acc4,$acc5)=map("%o$_",(0..5));
my ($bi,$poly1,$poly3,$minus1)=(map("%i$_",(3..5)),"%g1");
my ($rp_real,$ap_real)=("%g2","%g3");
my ($acc6,$acc7)=($bp,$bi);	# used in squaring

$code.=<<___;
.align	32
__ecp_nistz256_mul_by_2_vis3:
	addcc	$acc0,$acc0,$acc0
	addxccc	$acc1,$acc1,$acc1
	addxccc	$acc2,$acc2,$acc2
	addxccc	$acc3,$acc3,$acc3
	b	.Lreduce_by_sub_vis3
	addxc	%g0,%g0,$acc4		! did it carry?
.type	__ecp_nistz256_mul_by_2_vis3,#function
.size	__ecp_nistz256_mul_by_2_vis3,.-__ecp_nistz256_mul_by_2_vis3

.align	32
__ecp_nistz256_add_vis3:
	ldx	[$bp+0],$t0
	ldx	[$bp+8],$t1
	ldx	[$bp+16],$t2
	ldx	[$bp+24],$t3

__ecp_nistz256_add_noload_vis3:

	addcc	$t0,$acc0,$acc0
	addxccc	$t1,$acc1,$acc1
	addxccc	$t2,$acc2,$acc2
	addxccc	$t3,$acc3,$acc3
	addxc	%g0,%g0,$acc4		! did it carry?

.Lreduce_by_sub_vis3:

	addcc	$acc0,1,$t0		! add -modulus, i.e. subtract
	addxccc	$acc1,$poly1,$t1
	addxccc	$acc2,$minus1,$t2
	addxccc	$acc3,$poly3,$t3
	addxc	$acc4,$minus1,$acc4

	movrz	$acc4,$t0,$acc0		! ret = borrow ? ret : ret-modulus
	movrz	$acc4,$t1,$acc1
	stx	$acc0,[$rp]
	movrz	$acc4,$t2,$acc2
	stx	$acc1,[$rp+8]
	movrz	$acc4,$t3,$acc3
	stx	$acc2,[$rp+16]
	retl
	stx	$acc3,[$rp+24]
.type	__ecp_nistz256_add_vis3,#function
.size	__ecp_nistz256_add_vis3,.-__ecp_nistz256_add_vis3

! Trouble with subtraction is that there is no subtraction with 64-bit
! borrow, only with 32-bit one. For this reason we "decompose" 64-bit
! $acc0-$acc3 to 32-bit values and pick b[4] in 32-bit pieces. But
! recall that SPARC is big-endian, which is why you'll observe that
! b[4] is accessed as 4-0-12-8-20-16-28-24. And prior reduction we
! "collect" result back to 64-bit $acc0-$acc3.
.align	32
__ecp_nistz256_sub_from_vis3:
	ld	[$bp+4],$t0
	ld	[$bp+0],$t1
	ld	[$bp+12],$t2
	ld	[$bp+8],$t3

	srlx	$acc0,32,$acc4
	not	$poly1,$poly1
	srlx	$acc1,32,$acc5
	subcc	$acc0,$t0,$acc0
	ld	[$bp+20],$t0
	subccc	$acc4,$t1,$acc4
	ld	[$bp+16],$t1
	subccc	$acc1,$t2,$acc1
	ld	[$bp+28],$t2
	and	$acc0,$poly1,$acc0
	subccc	$acc5,$t3,$acc5
	ld	[$bp+24],$t3
	sllx	$acc4,32,$acc4
	and	$acc1,$poly1,$acc1
	sllx	$acc5,32,$acc5
	or	$acc0,$acc4,$acc0
	srlx	$acc2,32,$acc4
	or	$acc1,$acc5,$acc1
	srlx	$acc3,32,$acc5
	subccc	$acc2,$t0,$acc2
	subccc	$acc4,$t1,$acc4
	subccc	$acc3,$t2,$acc3
	and	$acc2,$poly1,$acc2
	subccc	$acc5,$t3,$acc5
	sllx	$acc4,32,$acc4
	and	$acc3,$poly1,$acc3
	sllx	$acc5,32,$acc5
	or	$acc2,$acc4,$acc2
	subc	%g0,%g0,$acc4		! did it borrow?
	b	.Lreduce_by_add_vis3
	or	$acc3,$acc5,$acc3
.type	__ecp_nistz256_sub_from_vis3,#function
.size	__ecp_nistz256_sub_from_vis3,.-__ecp_nistz256_sub_from_vis3

.align	32
__ecp_nistz256_sub_morf_vis3:
	ld	[$bp+4],$t0
	ld	[$bp+0],$t1
	ld	[$bp+12],$t2
	ld	[$bp+8],$t3

	srlx	$acc0,32,$acc4
	not	$poly1,$poly1
	srlx	$acc1,32,$acc5
	subcc	$t0,$acc0,$acc0
	ld	[$bp+20],$t0
	subccc	$t1,$acc4,$acc4
	ld	[$bp+16],$t1
	subccc	$t2,$acc1,$acc1
	ld	[$bp+28],$t2
	and	$acc0,$poly1,$acc0
	subccc	$t3,$acc5,$acc5
	ld	[$bp+24],$t3
	sllx	$acc4,32,$acc4
	and	$acc1,$poly1,$acc1
	sllx	$acc5,32,$acc5
	or	$acc0,$acc4,$acc0
	srlx	$acc2,32,$acc4
	or	$acc1,$acc5,$acc1
	srlx	$acc3,32,$acc5
	subccc	$t0,$acc2,$acc2
	subccc	$t1,$acc4,$acc4
	subccc	$t2,$acc3,$acc3
	and	$acc2,$poly1,$acc2
	subccc	$t3,$acc5,$acc5
	sllx	$acc4,32,$acc4
	and	$acc3,$poly1,$acc3
	sllx	$acc5,32,$acc5
	or	$acc2,$acc4,$acc2
	subc	%g0,%g0,$acc4		! did it borrow?
	or	$acc3,$acc5,$acc3

.Lreduce_by_add_vis3:

	addcc	$acc0,-1,$t0		! add modulus
	not	$poly3,$t3
	addxccc	$acc1,$poly1,$t1
	not	$poly1,$poly1		! restore $poly1
	addxccc	$acc2,%g0,$t2
	addxc	$acc3,$t3,$t3

	movrnz	$acc4,$t0,$acc0		! if a-b borrowed, ret = ret+mod
	movrnz	$acc4,$t1,$acc1
	stx	$acc0,[$rp]
	movrnz	$acc4,$t2,$acc2
	stx	$acc1,[$rp+8]
	movrnz	$acc4,$t3,$acc3
	stx	$acc2,[$rp+16]
	retl
	stx	$acc3,[$rp+24]
.type	__ecp_nistz256_sub_morf_vis3,#function
.size	__ecp_nistz256_sub_morf_vis3,.-__ecp_nistz256_sub_morf_vis3

.align	32
__ecp_nistz256_div_by_2_vis3:
	! ret = (a is odd ? a+mod : a) >> 1

	not	$poly1,$t1
	not	$poly3,$t3
	and	$acc0,1,$acc5
	addcc	$acc0,-1,$t0		! add modulus
	addxccc	$acc1,$t1,$t1
	addxccc	$acc2,%g0,$t2
	addxccc	$acc3,$t3,$t3
	addxc	%g0,%g0,$acc4		! carry bit

	movrnz	$acc5,$t0,$acc0
	movrnz	$acc5,$t1,$acc1
	movrnz	$acc5,$t2,$acc2
	movrnz	$acc5,$t3,$acc3
	movrz	$acc5,%g0,$acc4

	! ret >>= 1

	srlx	$acc0,1,$acc0
	sllx	$acc1,63,$t0
	srlx	$acc1,1,$acc1
	or	$acc0,$t0,$acc0
	sllx	$acc2,63,$t1
	srlx	$acc2,1,$acc2
	or	$acc1,$t1,$acc1
	sllx	$acc3,63,$t2
	stx	$acc0,[$rp]
	srlx	$acc3,1,$acc3
	or	$acc2,$t2,$acc2
	sllx	$acc4,63,$t3		! don't forget carry bit
	stx	$acc1,[$rp+8]
	or	$acc3,$t3,$acc3
	stx	$acc2,[$rp+16]
	retl
	stx	$acc3,[$rp+24]
.type	__ecp_nistz256_div_by_2_vis3,#function
.size	__ecp_nistz256_div_by_2_vis3,.-__ecp_nistz256_div_by_2_vis3

! compared to __ecp_nistz256_mul_mont it's almost 4x smaller and
! 4x faster [on T4]...
.align	32
__ecp_nistz256_mul_mont_vis3:
	mulx	$a0,$bi,$acc0
	not	$poly3,$poly3		! 0xFFFFFFFF00000001
	umulxhi	$a0,$bi,$t0
	mulx	$a1,$bi,$acc1
	umulxhi	$a1,$bi,$t1
	mulx	$a2,$bi,$acc2
	umulxhi	$a2,$bi,$t2
	mulx	$a3,$bi,$acc3
	umulxhi	$a3,$bi,$t3
	ldx	[$bp+8],$bi		! b[1]

	addcc	$acc1,$t0,$acc1		! accumulate high parts of multiplication
	 sllx	$acc0,32,$t0
	addxccc	$acc2,$t1,$acc2
	 srlx	$acc0,32,$t1
	addxccc	$acc3,$t2,$acc3
	addxc	%g0,$t3,$acc4
	mov	0,$acc5
___
for($i=1;$i<4;$i++) {
	# Reduction iteration is normally performed by accumulating
	# result of multiplication of modulus by "magic" digit [and
	# omitting least significant word, which is guaranteed to
	# be 0], but thanks to special form of modulus and "magic"
	# digit being equal to least significant word, it can be
	# performed with additions and subtractions alone. Indeed:
	#
	#            ffff0001.00000000.0000ffff.ffffffff
	# *                                     abcdefgh
	# + xxxxxxxx.xxxxxxxx.xxxxxxxx.xxxxxxxx.abcdefgh
	#
	# Now observing that ff..ff*x = (2^n-1)*x = 2^n*x-x, we
	# rewrite above as:
	#
	#   xxxxxxxx.xxxxxxxx.xxxxxxxx.xxxxxxxx.abcdefgh
	# + abcdefgh.abcdefgh.0000abcd.efgh0000.00000000
	# - 0000abcd.efgh0000.00000000.00000000.abcdefgh
	#
	# or marking redundant operations:
	#
	#   xxxxxxxx.xxxxxxxx.xxxxxxxx.xxxxxxxx.--------
	# + abcdefgh.abcdefgh.0000abcd.efgh0000.--------
	# - 0000abcd.efgh0000.--------.--------.--------
	#   ^^^^^^^^ but this word is calculated with umulxhi, because
	#            there is no subtract with 64-bit borrow:-(

$code.=<<___;
	sub	$acc0,$t0,$t2		! acc0*0xFFFFFFFF00000001, low part
	umulxhi	$acc0,$poly3,$t3	! acc0*0xFFFFFFFF00000001, high part
	addcc	$acc1,$t0,$acc0		! +=acc[0]<<96 and omit acc[0]
	mulx	$a0,$bi,$t0
	addxccc	$acc2,$t1,$acc1
	mulx	$a1,$bi,$t1
	addxccc	$acc3,$t2,$acc2		! +=acc[0]*0xFFFFFFFF00000001
	mulx	$a2,$bi,$t2
	addxccc	$acc4,$t3,$acc3
	mulx	$a3,$bi,$t3
	addxc	$acc5,%g0,$acc4

	addcc	$acc0,$t0,$acc0		! accumulate low parts of multiplication
	umulxhi	$a0,$bi,$t0
	addxccc	$acc1,$t1,$acc1
	umulxhi	$a1,$bi,$t1
	addxccc	$acc2,$t2,$acc2
	umulxhi	$a2,$bi,$t2
	addxccc	$acc3,$t3,$acc3
	umulxhi	$a3,$bi,$t3
	addxc	$acc4,%g0,$acc4
___
$code.=<<___	if ($i<3);
	ldx	[$bp+8*($i+1)],$bi	! bp[$i+1]
___
$code.=<<___;
	addcc	$acc1,$t0,$acc1		! accumulate high parts of multiplication
	 sllx	$acc0,32,$t0
	addxccc	$acc2,$t1,$acc2
	 srlx	$acc0,32,$t1
	addxccc	$acc3,$t2,$acc3
	addxccc	$acc4,$t3,$acc4
	addxc	%g0,%g0,$acc5
___
}
$code.=<<___;
	sub	$acc0,$t0,$t2		! acc0*0xFFFFFFFF00000001, low part
	umulxhi	$acc0,$poly3,$t3	! acc0*0xFFFFFFFF00000001, high part
	addcc	$acc1,$t0,$acc0		! +=acc[0]<<96 and omit acc[0]
	addxccc	$acc2,$t1,$acc1
	addxccc	$acc3,$t2,$acc2		! +=acc[0]*0xFFFFFFFF00000001
	addxccc	$acc4,$t3,$acc3
	b	.Lmul_final_vis3	! see below
	addxc	$acc5,%g0,$acc4
.type	__ecp_nistz256_mul_mont_vis3,#function
.size	__ecp_nistz256_mul_mont_vis3,.-__ecp_nistz256_mul_mont_vis3

! compared to above __ecp_nistz256_mul_mont_vis3 it's 21% less
! instructions, but only 14% faster [on T4]...
.align	32
__ecp_nistz256_sqr_mont_vis3:
	!  |  |  |  |  |  |a1*a0|  |
	!  |  |  |  |  |a2*a0|  |  |
	!  |  |a3*a2|a3*a0|  |  |  |
	!  |  |  |  |a2*a1|  |  |  |
	!  |  |  |a3*a1|  |  |  |  |
	! *|  |  |  |  |  |  |  | 2|
	! +|a3*a3|a2*a2|a1*a1|a0*a0|
	!  |--+--+--+--+--+--+--+--|
	!  |A7|A6|A5|A4|A3|A2|A1|A0|, where Ax is $accx, i.e. follow $accx
	!
	!  "can't overflow" below mark carrying into high part of
	!  multiplication result, which can't overflow, because it
	!  can never be all ones.

	mulx	$a1,$a0,$acc1		! a[1]*a[0]
	umulxhi	$a1,$a0,$t1
	mulx	$a2,$a0,$acc2		! a[2]*a[0]
	umulxhi	$a2,$a0,$t2
	mulx	$a3,$a0,$acc3		! a[3]*a[0]
	umulxhi	$a3,$a0,$acc4

	addcc	$acc2,$t1,$acc2		! accumulate high parts of multiplication
	mulx	$a2,$a1,$t0		! a[2]*a[1]
	umulxhi	$a2,$a1,$t1
	addxccc	$acc3,$t2,$acc3
	mulx	$a3,$a1,$t2		! a[3]*a[1]
	umulxhi	$a3,$a1,$t3
	addxc	$acc4,%g0,$acc4		! can't overflow

	mulx	$a3,$a2,$acc5		! a[3]*a[2]
	not	$poly3,$poly3		! 0xFFFFFFFF00000001
	umulxhi	$a3,$a2,$acc6

	addcc	$t2,$t1,$t1		! accumulate high parts of multiplication
	mulx	$a0,$a0,$acc0		! a[0]*a[0]
	addxc	$t3,%g0,$t2		! can't overflow

	addcc	$acc3,$t0,$acc3		! accumulate low parts of multiplication
	umulxhi	$a0,$a0,$a0
	addxccc	$acc4,$t1,$acc4
	mulx	$a1,$a1,$t1		! a[1]*a[1]
	addxccc	$acc5,$t2,$acc5
	umulxhi	$a1,$a1,$a1
	addxc	$acc6,%g0,$acc6		! can't overflow

	addcc	$acc1,$acc1,$acc1	! acc[1-6]*=2
	mulx	$a2,$a2,$t2		! a[2]*a[2]
	addxccc	$acc2,$acc2,$acc2
	umulxhi	$a2,$a2,$a2
	addxccc	$acc3,$acc3,$acc3
	mulx	$a3,$a3,$t3		! a[3]*a[3]
	addxccc	$acc4,$acc4,$acc4
	umulxhi	$a3,$a3,$a3
	addxccc	$acc5,$acc5,$acc5
	addxccc	$acc6,$acc6,$acc6
	addxc	%g0,%g0,$acc7

	addcc	$acc1,$a0,$acc1		! +a[i]*a[i]
	addxccc	$acc2,$t1,$acc2
	addxccc	$acc3,$a1,$acc3
	addxccc	$acc4,$t2,$acc4
	 sllx	$acc0,32,$t0
	addxccc	$acc5,$a2,$acc5
	 srlx	$acc0,32,$t1
	addxccc	$acc6,$t3,$acc6
	 sub	$acc0,$t0,$t2		! acc0*0xFFFFFFFF00000001, low part
	addxc	$acc7,$a3,$acc7
___
for($i=0;$i<3;$i++) {			# reductions, see commentary
					# in multiplication for details
$code.=<<___;
	umulxhi	$acc0,$poly3,$t3	! acc0*0xFFFFFFFF00000001, high part
	addcc	$acc1,$t0,$acc0		! +=acc[0]<<96 and omit acc[0]
	 sllx	$acc0,32,$t0
	addxccc	$acc2,$t1,$acc1
	 srlx	$acc0,32,$t1
	addxccc	$acc3,$t2,$acc2		! +=acc[0]*0xFFFFFFFF00000001
	 sub	$acc0,$t0,$t2		! acc0*0xFFFFFFFF00000001, low part
	addxc	%g0,$t3,$acc3		! can't overflow
___
}
$code.=<<___;
	umulxhi	$acc0,$poly3,$t3	! acc0*0xFFFFFFFF00000001, high part
	addcc	$acc1,$t0,$acc0		! +=acc[0]<<96 and omit acc[0]
	addxccc	$acc2,$t1,$acc1
	addxccc	$acc3,$t2,$acc2		! +=acc[0]*0xFFFFFFFF00000001
	addxc	%g0,$t3,$acc3		! can't overflow

	addcc	$acc0,$acc4,$acc0	! accumulate upper half
	addxccc	$acc1,$acc5,$acc1
	addxccc	$acc2,$acc6,$acc2
	addxccc	$acc3,$acc7,$acc3
	addxc	%g0,%g0,$acc4

.Lmul_final_vis3:

	! Final step is "if result > mod, subtract mod", but as comparison
	! means subtraction, we do the subtraction and then copy outcome
	! if it didn't borrow. But note that as we [have to] replace
	! subtraction with addition with negative, carry/borrow logic is
	! inverse.

	addcc	$acc0,1,$t0		! add -modulus, i.e. subtract
	not	$poly3,$poly3		! restore 0x00000000FFFFFFFE
	addxccc	$acc1,$poly1,$t1
	addxccc	$acc2,$minus1,$t2
	addxccc	$acc3,$poly3,$t3
	addxccc	$acc4,$minus1,%g0	! did it carry?

	movcs	%xcc,$t0,$acc0
	movcs	%xcc,$t1,$acc1
	stx	$acc0,[$rp]
	movcs	%xcc,$t2,$acc2
	stx	$acc1,[$rp+8]
	movcs	%xcc,$t3,$acc3
	stx	$acc2,[$rp+16]
	retl
	stx	$acc3,[$rp+24]
.type	__ecp_nistz256_sqr_mont_vis3,#function
.size	__ecp_nistz256_sqr_mont_vis3,.-__ecp_nistz256_sqr_mont_vis3
___

########################################################################
# void ecp_nistz256_point_double(P256_POINT *out,const P256_POINT *inp);
#
{
my ($res_x,$res_y,$res_z,
    $in_x,$in_y,$in_z,
    $S,$M,$Zsqr,$tmp0)=map(32*$_,(0..9));
# above map() describes stack layout with 10 temporary
# 256-bit vectors on top.

$code.=<<___;
.align	32
ecp_nistz256_point_double_vis3:
	save	%sp,-STACK64_FRAME-32*10,%sp

	mov	$rp,$rp_real
.Ldouble_shortcut_vis3:
	mov	-1,$minus1
	mov	-2,$poly3
	sllx	$minus1,32,$poly1		! 0xFFFFFFFF00000000
	srl	$poly3,0,$poly3			! 0x00000000FFFFFFFE

	! convert input to uint64_t[4]
	ld	[$ap],$a0			! in_x
	ld	[$ap+4],$t0
	ld	[$ap+8],$a1
	ld	[$ap+12],$t1
	ld	[$ap+16],$a2
	ld	[$ap+20],$t2
	ld	[$ap+24],$a3
	ld	[$ap+28],$t3
	sllx	$t0,32,$t0
	sllx	$t1,32,$t1
	ld	[$ap+32],$acc0			! in_y
	or	$a0,$t0,$a0
	ld	[$ap+32+4],$t0
	sllx	$t2,32,$t2
	ld	[$ap+32+8],$acc1
	or	$a1,$t1,$a1
	ld	[$ap+32+12],$t1
	sllx	$t3,32,$t3
	ld	[$ap+32+16],$acc2
	or	$a2,$t2,$a2
	ld	[$ap+32+20],$t2
	or	$a3,$t3,$a3
	ld	[$ap+32+24],$acc3
	sllx	$t0,32,$t0
	ld	[$ap+32+28],$t3
	sllx	$t1,32,$t1
	stx	$a0,[%sp+LOCALS64+$in_x]
	sllx	$t2,32,$t2
	stx	$a1,[%sp+LOCALS64+$in_x+8]
	sllx	$t3,32,$t3
	stx	$a2,[%sp+LOCALS64+$in_x+16]
	or	$acc0,$t0,$acc0
	stx	$a3,[%sp+LOCALS64+$in_x+24]
	or	$acc1,$t1,$acc1
	stx	$acc0,[%sp+LOCALS64+$in_y]
	or	$acc2,$t2,$acc2
	stx	$acc1,[%sp+LOCALS64+$in_y+8]
	or	$acc3,$t3,$acc3
	stx	$acc2,[%sp+LOCALS64+$in_y+16]
	stx	$acc3,[%sp+LOCALS64+$in_y+24]

	ld	[$ap+64],$a0			! in_z
	ld	[$ap+64+4],$t0
	ld	[$ap+64+8],$a1
	ld	[$ap+64+12],$t1
	ld	[$ap+64+16],$a2
	ld	[$ap+64+20],$t2
	ld	[$ap+64+24],$a3
	ld	[$ap+64+28],$t3
	sllx	$t0,32,$t0
	sllx	$t1,32,$t1
	or	$a0,$t0,$a0
	sllx	$t2,32,$t2
	or	$a1,$t1,$a1
	sllx	$t3,32,$t3
	or	$a2,$t2,$a2
	or	$a3,$t3,$a3
	sllx	$t0,32,$t0
	sllx	$t1,32,$t1
	stx	$a0,[%sp+LOCALS64+$in_z]
	sllx	$t2,32,$t2
	stx	$a1,[%sp+LOCALS64+$in_z+8]
	sllx	$t3,32,$t3
	stx	$a2,[%sp+LOCALS64+$in_z+16]
	stx	$a3,[%sp+LOCALS64+$in_z+24]

	! in_y is still in $acc0-$acc3
	call	__ecp_nistz256_mul_by_2_vis3	! p256_mul_by_2(S, in_y);
	add	%sp,LOCALS64+$S,$rp

	! in_z is still in $a0-$a3
	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(Zsqr, in_z);
	add	%sp,LOCALS64+$Zsqr,$rp

	mov	$acc0,$a0			! put Zsqr aside
	mov	$acc1,$a1
	mov	$acc2,$a2
	mov	$acc3,$a3

	add	%sp,LOCALS64+$in_x,$bp
	call	__ecp_nistz256_add_vis3		! p256_add(M, Zsqr, in_x);
	add	%sp,LOCALS64+$M,$rp

	mov	$a0,$acc0			! restore Zsqr
	ldx	[%sp+LOCALS64+$S],$a0		! forward load
	mov	$a1,$acc1
	ldx	[%sp+LOCALS64+$S+8],$a1
	mov	$a2,$acc2
	ldx	[%sp+LOCALS64+$S+16],$a2
	mov	$a3,$acc3
	ldx	[%sp+LOCALS64+$S+24],$a3

	add	%sp,LOCALS64+$in_x,$bp
	call	__ecp_nistz256_sub_morf_vis3	! p256_sub(Zsqr, in_x, Zsqr);
	add	%sp,LOCALS64+$Zsqr,$rp

	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(S, S);
	add	%sp,LOCALS64+$S,$rp

	ldx	[%sp+LOCALS64+$in_z],$bi
	ldx	[%sp+LOCALS64+$in_y],$a0
	ldx	[%sp+LOCALS64+$in_y+8],$a1
	ldx	[%sp+LOCALS64+$in_y+16],$a2
	ldx	[%sp+LOCALS64+$in_y+24],$a3
	add	%sp,LOCALS64+$in_z,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(tmp0, in_z, in_y);
	add	%sp,LOCALS64+$tmp0,$rp

	ldx	[%sp+LOCALS64+$M],$bi		! forward load
	ldx	[%sp+LOCALS64+$Zsqr],$a0
	ldx	[%sp+LOCALS64+$Zsqr+8],$a1
	ldx	[%sp+LOCALS64+$Zsqr+16],$a2
	ldx	[%sp+LOCALS64+$Zsqr+24],$a3

	call	__ecp_nistz256_mul_by_2_vis3	! p256_mul_by_2(res_z, tmp0);
	add	%sp,LOCALS64+$res_z,$rp

	add	%sp,LOCALS64+$M,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(M, M, Zsqr);
	add	%sp,LOCALS64+$M,$rp

	mov	$acc0,$a0			! put aside M
	mov	$acc1,$a1
	mov	$acc2,$a2
	mov	$acc3,$a3
	call	__ecp_nistz256_mul_by_2_vis3
	add	%sp,LOCALS64+$M,$rp
	mov	$a0,$t0				! copy M
	ldx	[%sp+LOCALS64+$S],$a0		! forward load
	mov	$a1,$t1
	ldx	[%sp+LOCALS64+$S+8],$a1
	mov	$a2,$t2
	ldx	[%sp+LOCALS64+$S+16],$a2
	mov	$a3,$t3
	ldx	[%sp+LOCALS64+$S+24],$a3
	call	__ecp_nistz256_add_noload_vis3	! p256_mul_by_3(M, M);
	add	%sp,LOCALS64+$M,$rp

	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(tmp0, S);
	add	%sp,LOCALS64+$tmp0,$rp

	ldx	[%sp+LOCALS64+$S],$bi		! forward load
	ldx	[%sp+LOCALS64+$in_x],$a0
	ldx	[%sp+LOCALS64+$in_x+8],$a1
	ldx	[%sp+LOCALS64+$in_x+16],$a2
	ldx	[%sp+LOCALS64+$in_x+24],$a3

	call	__ecp_nistz256_div_by_2_vis3	! p256_div_by_2(res_y, tmp0);
	add	%sp,LOCALS64+$res_y,$rp

	add	%sp,LOCALS64+$S,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(S, S, in_x);
	add	%sp,LOCALS64+$S,$rp

	ldx	[%sp+LOCALS64+$M],$a0		! forward load
	ldx	[%sp+LOCALS64+$M+8],$a1
	ldx	[%sp+LOCALS64+$M+16],$a2
	ldx	[%sp+LOCALS64+$M+24],$a3

	call	__ecp_nistz256_mul_by_2_vis3	! p256_mul_by_2(tmp0, S);
	add	%sp,LOCALS64+$tmp0,$rp

	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(res_x, M);
	add	%sp,LOCALS64+$res_x,$rp

	add	%sp,LOCALS64+$tmp0,$bp
	call	__ecp_nistz256_sub_from_vis3	! p256_sub(res_x, res_x, tmp0);
	add	%sp,LOCALS64+$res_x,$rp

	ldx	[%sp+LOCALS64+$M],$a0		! forward load
	ldx	[%sp+LOCALS64+$M+8],$a1
	ldx	[%sp+LOCALS64+$M+16],$a2
	ldx	[%sp+LOCALS64+$M+24],$a3

	add	%sp,LOCALS64+$S,$bp
	call	__ecp_nistz256_sub_morf_vis3	! p256_sub(S, S, res_x);
	add	%sp,LOCALS64+$S,$rp

	mov	$acc0,$bi
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(S, S, M);
	add	%sp,LOCALS64+$S,$rp

	ldx	[%sp+LOCALS64+$res_x],$a0	! forward load
	ldx	[%sp+LOCALS64+$res_x+8],$a1
	ldx	[%sp+LOCALS64+$res_x+16],$a2
	ldx	[%sp+LOCALS64+$res_x+24],$a3

	add	%sp,LOCALS64+$res_y,$bp
	call	__ecp_nistz256_sub_from_vis3	! p256_sub(res_y, S, res_y);
	add	%sp,LOCALS64+$res_y,$bp

	! convert output to uint_32[8]
	srlx	$a0,32,$t0
	srlx	$a1,32,$t1
	st	$a0,[$rp_real]			! res_x
	srlx	$a2,32,$t2
	st	$t0,[$rp_real+4]
	srlx	$a3,32,$t3
	st	$a1,[$rp_real+8]
	st	$t1,[$rp_real+12]
	st	$a2,[$rp_real+16]
	st	$t2,[$rp_real+20]
	st	$a3,[$rp_real+24]
	st	$t3,[$rp_real+28]

	ldx	[%sp+LOCALS64+$res_z],$a0	! forward load
	srlx	$acc0,32,$t0
	ldx	[%sp+LOCALS64+$res_z+8],$a1
	srlx	$acc1,32,$t1
	ldx	[%sp+LOCALS64+$res_z+16],$a2
	srlx	$acc2,32,$t2
	ldx	[%sp+LOCALS64+$res_z+24],$a3
	srlx	$acc3,32,$t3
	st	$acc0,[$rp_real+32]		! res_y
	st	$t0,  [$rp_real+32+4]
	st	$acc1,[$rp_real+32+8]
	st	$t1,  [$rp_real+32+12]
	st	$acc2,[$rp_real+32+16]
	st	$t2,  [$rp_real+32+20]
	st	$acc3,[$rp_real+32+24]
	st	$t3,  [$rp_real+32+28]

	srlx	$a0,32,$t0
	srlx	$a1,32,$t1
	st	$a0,[$rp_real+64]		! res_z
	srlx	$a2,32,$t2
	st	$t0,[$rp_real+64+4]
	srlx	$a3,32,$t3
	st	$a1,[$rp_real+64+8]
	st	$t1,[$rp_real+64+12]
	st	$a2,[$rp_real+64+16]
	st	$t2,[$rp_real+64+20]
	st	$a3,[$rp_real+64+24]
	st	$t3,[$rp_real+64+28]

	ret
	restore
.type	ecp_nistz256_point_double_vis3,#function
.size	ecp_nistz256_point_double_vis3,.-ecp_nistz256_point_double_vis3
___
}
########################################################################
# void ecp_nistz256_point_add(P256_POINT *out,const P256_POINT *in1,
#			      const P256_POINT *in2);
{
my ($res_x,$res_y,$res_z,
    $in1_x,$in1_y,$in1_z,
    $in2_x,$in2_y,$in2_z,
    $H,$Hsqr,$R,$Rsqr,$Hcub,
    $U1,$U2,$S1,$S2)=map(32*$_,(0..17));
my ($Z1sqr, $Z2sqr) = ($Hsqr, $Rsqr);

# above map() describes stack layout with 18 temporary
# 256-bit vectors on top. Then we reserve some space for
# !in1infty, !in2infty and result of check for zero.

$code.=<<___;
.globl	ecp_nistz256_point_add_vis3
.align	32
ecp_nistz256_point_add_vis3:
	save	%sp,-STACK64_FRAME-32*18-32,%sp

	mov	$rp,$rp_real
	mov	-1,$minus1
	mov	-2,$poly3
	sllx	$minus1,32,$poly1		! 0xFFFFFFFF00000000
	srl	$poly3,0,$poly3			! 0x00000000FFFFFFFE

	! convert input to uint64_t[4]
	ld	[$bp],$a0			! in2_x
	ld	[$bp+4],$t0
	ld	[$bp+8],$a1
	ld	[$bp+12],$t1
	ld	[$bp+16],$a2
	ld	[$bp+20],$t2
	ld	[$bp+24],$a3
	ld	[$bp+28],$t3
	sllx	$t0,32,$t0
	sllx	$t1,32,$t1
	ld	[$bp+32],$acc0			! in2_y
	or	$a0,$t0,$a0
	ld	[$bp+32+4],$t0
	sllx	$t2,32,$t2
	ld	[$bp+32+8],$acc1
	or	$a1,$t1,$a1
	ld	[$bp+32+12],$t1
	sllx	$t3,32,$t3
	ld	[$bp+32+16],$acc2
	or	$a2,$t2,$a2
	ld	[$bp+32+20],$t2
	or	$a3,$t3,$a3
	ld	[$bp+32+24],$acc3
	sllx	$t0,32,$t0
	ld	[$bp+32+28],$t3
	sllx	$t1,32,$t1
	stx	$a0,[%sp+LOCALS64+$in2_x]
	sllx	$t2,32,$t2
	stx	$a1,[%sp+LOCALS64+$in2_x+8]
	sllx	$t3,32,$t3
	stx	$a2,[%sp+LOCALS64+$in2_x+16]
	or	$acc0,$t0,$acc0
	stx	$a3,[%sp+LOCALS64+$in2_x+24]
	or	$acc1,$t1,$acc1
	stx	$acc0,[%sp+LOCALS64+$in2_y]
	or	$acc2,$t2,$acc2
	stx	$acc1,[%sp+LOCALS64+$in2_y+8]
	or	$acc3,$t3,$acc3
	stx	$acc2,[%sp+LOCALS64+$in2_y+16]
	stx	$acc3,[%sp+LOCALS64+$in2_y+24]

	ld	[$bp+64],$acc0			! in2_z
	ld	[$bp+64+4],$t0
	ld	[$bp+64+8],$acc1
	ld	[$bp+64+12],$t1
	ld	[$bp+64+16],$acc2
	ld	[$bp+64+20],$t2
	ld	[$bp+64+24],$acc3
	ld	[$bp+64+28],$t3
	sllx	$t0,32,$t0
	sllx	$t1,32,$t1
	ld	[$ap],$a0			! in1_x
	or	$acc0,$t0,$acc0
	ld	[$ap+4],$t0
	sllx	$t2,32,$t2
	ld	[$ap+8],$a1
	or	$acc1,$t1,$acc1
	ld	[$ap+12],$t1
	sllx	$t3,32,$t3
	ld	[$ap+16],$a2
	or	$acc2,$t2,$acc2
	ld	[$ap+20],$t2
	or	$acc3,$t3,$acc3
	ld	[$ap+24],$a3
	sllx	$t0,32,$t0
	ld	[$ap+28],$t3
	sllx	$t1,32,$t1
	stx	$acc0,[%sp+LOCALS64+$in2_z]
	sllx	$t2,32,$t2
	stx	$acc1,[%sp+LOCALS64+$in2_z+8]
	sllx	$t3,32,$t3
	stx	$acc2,[%sp+LOCALS64+$in2_z+16]
	stx	$acc3,[%sp+LOCALS64+$in2_z+24]

	or	$acc1,$acc0,$acc0
	or	$acc3,$acc2,$acc2
	or	$acc2,$acc0,$acc0
	movrnz	$acc0,-1,$acc0			! !in2infty
	stx	$acc0,[%fp+STACK_BIAS-8]

	or	$a0,$t0,$a0
	ld	[$ap+32],$acc0			! in1_y
	or	$a1,$t1,$a1
	ld	[$ap+32+4],$t0
	or	$a2,$t2,$a2
	ld	[$ap+32+8],$acc1
	or	$a3,$t3,$a3
	ld	[$ap+32+12],$t1
	ld	[$ap+32+16],$acc2
	ld	[$ap+32+20],$t2
	ld	[$ap+32+24],$acc3
	sllx	$t0,32,$t0
	ld	[$ap+32+28],$t3
	sllx	$t1,32,$t1
	stx	$a0,[%sp+LOCALS64+$in1_x]
	sllx	$t2,32,$t2
	stx	$a1,[%sp+LOCALS64+$in1_x+8]
	sllx	$t3,32,$t3
	stx	$a2,[%sp+LOCALS64+$in1_x+16]
	or	$acc0,$t0,$acc0
	stx	$a3,[%sp+LOCALS64+$in1_x+24]
	or	$acc1,$t1,$acc1
	stx	$acc0,[%sp+LOCALS64+$in1_y]
	or	$acc2,$t2,$acc2
	stx	$acc1,[%sp+LOCALS64+$in1_y+8]
	or	$acc3,$t3,$acc3
	stx	$acc2,[%sp+LOCALS64+$in1_y+16]
	stx	$acc3,[%sp+LOCALS64+$in1_y+24]

	ldx	[%sp+LOCALS64+$in2_z],$a0	! forward load
	ldx	[%sp+LOCALS64+$in2_z+8],$a1
	ldx	[%sp+LOCALS64+$in2_z+16],$a2
	ldx	[%sp+LOCALS64+$in2_z+24],$a3

	ld	[$ap+64],$acc0			! in1_z
	ld	[$ap+64+4],$t0
	ld	[$ap+64+8],$acc1
	ld	[$ap+64+12],$t1
	ld	[$ap+64+16],$acc2
	ld	[$ap+64+20],$t2
	ld	[$ap+64+24],$acc3
	ld	[$ap+64+28],$t3
	sllx	$t0,32,$t0
	sllx	$t1,32,$t1
	or	$acc0,$t0,$acc0
	sllx	$t2,32,$t2
	or	$acc1,$t1,$acc1
	sllx	$t3,32,$t3
	stx	$acc0,[%sp+LOCALS64+$in1_z]
	or	$acc2,$t2,$acc2
	stx	$acc1,[%sp+LOCALS64+$in1_z+8]
	or	$acc3,$t3,$acc3
	stx	$acc2,[%sp+LOCALS64+$in1_z+16]
	stx	$acc3,[%sp+LOCALS64+$in1_z+24]

	or	$acc1,$acc0,$acc0
	or	$acc3,$acc2,$acc2
	or	$acc2,$acc0,$acc0
	movrnz	$acc0,-1,$acc0			! !in1infty
	stx	$acc0,[%fp+STACK_BIAS-16]

	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(Z2sqr, in2_z);
	add	%sp,LOCALS64+$Z2sqr,$rp

	ldx	[%sp+LOCALS64+$in1_z],$a0
	ldx	[%sp+LOCALS64+$in1_z+8],$a1
	ldx	[%sp+LOCALS64+$in1_z+16],$a2
	ldx	[%sp+LOCALS64+$in1_z+24],$a3
	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(Z1sqr, in1_z);
	add	%sp,LOCALS64+$Z1sqr,$rp

	ldx	[%sp+LOCALS64+$Z2sqr],$bi
	ldx	[%sp+LOCALS64+$in2_z],$a0
	ldx	[%sp+LOCALS64+$in2_z+8],$a1
	ldx	[%sp+LOCALS64+$in2_z+16],$a2
	ldx	[%sp+LOCALS64+$in2_z+24],$a3
	add	%sp,LOCALS64+$Z2sqr,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(S1, Z2sqr, in2_z);
	add	%sp,LOCALS64+$S1,$rp

	ldx	[%sp+LOCALS64+$Z1sqr],$bi
	ldx	[%sp+LOCALS64+$in1_z],$a0
	ldx	[%sp+LOCALS64+$in1_z+8],$a1
	ldx	[%sp+LOCALS64+$in1_z+16],$a2
	ldx	[%sp+LOCALS64+$in1_z+24],$a3
	add	%sp,LOCALS64+$Z1sqr,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(S2, Z1sqr, in1_z);
	add	%sp,LOCALS64+$S2,$rp

	ldx	[%sp+LOCALS64+$S1],$bi
	ldx	[%sp+LOCALS64+$in1_y],$a0
	ldx	[%sp+LOCALS64+$in1_y+8],$a1
	ldx	[%sp+LOCALS64+$in1_y+16],$a2
	ldx	[%sp+LOCALS64+$in1_y+24],$a3
	add	%sp,LOCALS64+$S1,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(S1, S1, in1_y);
	add	%sp,LOCALS64+$S1,$rp

	ldx	[%sp+LOCALS64+$S2],$bi
	ldx	[%sp+LOCALS64+$in2_y],$a0
	ldx	[%sp+LOCALS64+$in2_y+8],$a1
	ldx	[%sp+LOCALS64+$in2_y+16],$a2
	ldx	[%sp+LOCALS64+$in2_y+24],$a3
	add	%sp,LOCALS64+$S2,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(S2, S2, in2_y);
	add	%sp,LOCALS64+$S2,$rp

	ldx	[%sp+LOCALS64+$Z2sqr],$bi	! forward load
	ldx	[%sp+LOCALS64+$in1_x],$a0
	ldx	[%sp+LOCALS64+$in1_x+8],$a1
	ldx	[%sp+LOCALS64+$in1_x+16],$a2
	ldx	[%sp+LOCALS64+$in1_x+24],$a3

	add	%sp,LOCALS64+$S1,$bp
	call	__ecp_nistz256_sub_from_vis3	! p256_sub(R, S2, S1);
	add	%sp,LOCALS64+$R,$rp

	or	$acc1,$acc0,$acc0		! see if result is zero
	or	$acc3,$acc2,$acc2
	or	$acc2,$acc0,$acc0
	stx	$acc0,[%fp+STACK_BIAS-24]

	add	%sp,LOCALS64+$Z2sqr,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(U1, in1_x, Z2sqr);
	add	%sp,LOCALS64+$U1,$rp

	ldx	[%sp+LOCALS64+$Z1sqr],$bi
	ldx	[%sp+LOCALS64+$in2_x],$a0
	ldx	[%sp+LOCALS64+$in2_x+8],$a1
	ldx	[%sp+LOCALS64+$in2_x+16],$a2
	ldx	[%sp+LOCALS64+$in2_x+24],$a3
	add	%sp,LOCALS64+$Z1sqr,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(U2, in2_x, Z1sqr);
	add	%sp,LOCALS64+$U2,$rp

	ldx	[%sp+LOCALS64+$R],$a0		! forward load
	ldx	[%sp+LOCALS64+$R+8],$a1
	ldx	[%sp+LOCALS64+$R+16],$a2
	ldx	[%sp+LOCALS64+$R+24],$a3

	add	%sp,LOCALS64+$U1,$bp
	call	__ecp_nistz256_sub_from_vis3	! p256_sub(H, U2, U1);
	add	%sp,LOCALS64+$H,$rp

	or	$acc1,$acc0,$acc0		! see if result is zero
	or	$acc3,$acc2,$acc2
	orcc	$acc2,$acc0,$acc0

	bne,pt	%xcc,.Ladd_proceed_vis3		! is_equal(U1,U2)?
	nop

	ldx	[%fp+STACK_BIAS-8],$t0
	ldx	[%fp+STACK_BIAS-16],$t1
	ldx	[%fp+STACK_BIAS-24],$t2
	andcc	$t0,$t1,%g0
	be,pt	%xcc,.Ladd_proceed_vis3		! (in1infty || in2infty)?
	nop
	andcc	$t2,$t2,%g0
	be,a,pt	%xcc,.Ldouble_shortcut_vis3	! is_equal(S1,S2)?
	add	%sp,32*(12-10)+32,%sp		! difference in frame sizes

	st	%g0,[$rp_real]
	st	%g0,[$rp_real+4]
	st	%g0,[$rp_real+8]
	st	%g0,[$rp_real+12]
	st	%g0,[$rp_real+16]
	st	%g0,[$rp_real+20]
	st	%g0,[$rp_real+24]
	st	%g0,[$rp_real+28]
	st	%g0,[$rp_real+32]
	st	%g0,[$rp_real+32+4]
	st	%g0,[$rp_real+32+8]
	st	%g0,[$rp_real+32+12]
	st	%g0,[$rp_real+32+16]
	st	%g0,[$rp_real+32+20]
	st	%g0,[$rp_real+32+24]
	st	%g0,[$rp_real+32+28]
	st	%g0,[$rp_real+64]
	st	%g0,[$rp_real+64+4]
	st	%g0,[$rp_real+64+8]
	st	%g0,[$rp_real+64+12]
	st	%g0,[$rp_real+64+16]
	st	%g0,[$rp_real+64+20]
	st	%g0,[$rp_real+64+24]
	st	%g0,[$rp_real+64+28]
	b	.Ladd_done_vis3
	nop

.align	16
.Ladd_proceed_vis3:
	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(Rsqr, R);
	add	%sp,LOCALS64+$Rsqr,$rp

	ldx	[%sp+LOCALS64+$H],$bi
	ldx	[%sp+LOCALS64+$in1_z],$a0
	ldx	[%sp+LOCALS64+$in1_z+8],$a1
	ldx	[%sp+LOCALS64+$in1_z+16],$a2
	ldx	[%sp+LOCALS64+$in1_z+24],$a3
	add	%sp,LOCALS64+$H,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(res_z, H, in1_z);
	add	%sp,LOCALS64+$res_z,$rp

	ldx	[%sp+LOCALS64+$H],$a0
	ldx	[%sp+LOCALS64+$H+8],$a1
	ldx	[%sp+LOCALS64+$H+16],$a2
	ldx	[%sp+LOCALS64+$H+24],$a3
	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(Hsqr, H);
	add	%sp,LOCALS64+$Hsqr,$rp

	ldx	[%sp+LOCALS64+$res_z],$bi
	ldx	[%sp+LOCALS64+$in2_z],$a0
	ldx	[%sp+LOCALS64+$in2_z+8],$a1
	ldx	[%sp+LOCALS64+$in2_z+16],$a2
	ldx	[%sp+LOCALS64+$in2_z+24],$a3
	add	%sp,LOCALS64+$res_z,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(res_z, res_z, in2_z);
	add	%sp,LOCALS64+$res_z,$rp

	ldx	[%sp+LOCALS64+$H],$bi
	ldx	[%sp+LOCALS64+$Hsqr],$a0
	ldx	[%sp+LOCALS64+$Hsqr+8],$a1
	ldx	[%sp+LOCALS64+$Hsqr+16],$a2
	ldx	[%sp+LOCALS64+$Hsqr+24],$a3
	add	%sp,LOCALS64+$H,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(Hcub, Hsqr, H);
	add	%sp,LOCALS64+$Hcub,$rp

	ldx	[%sp+LOCALS64+$U1],$bi
	ldx	[%sp+LOCALS64+$Hsqr],$a0
	ldx	[%sp+LOCALS64+$Hsqr+8],$a1
	ldx	[%sp+LOCALS64+$Hsqr+16],$a2
	ldx	[%sp+LOCALS64+$Hsqr+24],$a3
	add	%sp,LOCALS64+$U1,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(U2, U1, Hsqr);
	add	%sp,LOCALS64+$U2,$rp

	call	__ecp_nistz256_mul_by_2_vis3	! p256_mul_by_2(Hsqr, U2);
	add	%sp,LOCALS64+$Hsqr,$rp

	add	%sp,LOCALS64+$Rsqr,$bp
	call	__ecp_nistz256_sub_morf_vis3	! p256_sub(res_x, Rsqr, Hsqr);
	add	%sp,LOCALS64+$res_x,$rp

	add	%sp,LOCALS64+$Hcub,$bp
	call	__ecp_nistz256_sub_from_vis3	!  p256_sub(res_x, res_x, Hcub);
	add	%sp,LOCALS64+$res_x,$rp

	ldx	[%sp+LOCALS64+$S1],$bi		! forward load
	ldx	[%sp+LOCALS64+$Hcub],$a0
	ldx	[%sp+LOCALS64+$Hcub+8],$a1
	ldx	[%sp+LOCALS64+$Hcub+16],$a2
	ldx	[%sp+LOCALS64+$Hcub+24],$a3

	add	%sp,LOCALS64+$U2,$bp
	call	__ecp_nistz256_sub_morf_vis3	! p256_sub(res_y, U2, res_x);
	add	%sp,LOCALS64+$res_y,$rp

	add	%sp,LOCALS64+$S1,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(S2, S1, Hcub);
	add	%sp,LOCALS64+$S2,$rp

	ldx	[%sp+LOCALS64+$R],$bi
	ldx	[%sp+LOCALS64+$res_y],$a0
	ldx	[%sp+LOCALS64+$res_y+8],$a1
	ldx	[%sp+LOCALS64+$res_y+16],$a2
	ldx	[%sp+LOCALS64+$res_y+24],$a3
	add	%sp,LOCALS64+$R,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(res_y, res_y, R);
	add	%sp,LOCALS64+$res_y,$rp

	add	%sp,LOCALS64+$S2,$bp
	call	__ecp_nistz256_sub_from_vis3	! p256_sub(res_y, res_y, S2);
	add	%sp,LOCALS64+$res_y,$rp

	ldx	[%fp+STACK_BIAS-16],$t1		! !in1infty
	ldx	[%fp+STACK_BIAS-8],$t2		! !in2infty
___
for($i=0;$i<96;$i+=16) {			# conditional moves
$code.=<<___;
	ldx	[%sp+LOCALS64+$res_x+$i],$acc0	! res
	ldx	[%sp+LOCALS64+$res_x+$i+8],$acc1
	ldx	[%sp+LOCALS64+$in2_x+$i],$acc2	! in2
	ldx	[%sp+LOCALS64+$in2_x+$i+8],$acc3
	ldx	[%sp+LOCALS64+$in1_x+$i],$acc4	! in1
	ldx	[%sp+LOCALS64+$in1_x+$i+8],$acc5
	movrz	$t1,$acc2,$acc0
	movrz	$t1,$acc3,$acc1
	movrz	$t2,$acc4,$acc0
	movrz	$t2,$acc5,$acc1
	srlx	$acc0,32,$acc2
	srlx	$acc1,32,$acc3
	st	$acc0,[$rp_real+$i]
	st	$acc2,[$rp_real+$i+4]
	st	$acc1,[$rp_real+$i+8]
	st	$acc3,[$rp_real+$i+12]
___
}
$code.=<<___;
.Ladd_done_vis3:
	ret
	restore
.type	ecp_nistz256_point_add_vis3,#function
.size	ecp_nistz256_point_add_vis3,.-ecp_nistz256_point_add_vis3
___
}
########################################################################
# void ecp_nistz256_point_add_affine(P256_POINT *out,const P256_POINT *in1,
#				     const P256_POINT_AFFINE *in2);
{
my ($res_x,$res_y,$res_z,
    $in1_x,$in1_y,$in1_z,
    $in2_x,$in2_y,
    $U2,$S2,$H,$R,$Hsqr,$Hcub,$Rsqr)=map(32*$_,(0..14));
my $Z1sqr = $S2;
# above map() describes stack layout with 15 temporary
# 256-bit vectors on top. Then we reserve some space for
# !in1infty and !in2infty.

$code.=<<___;
.align	32
ecp_nistz256_point_add_affine_vis3:
	save	%sp,-STACK64_FRAME-32*15-32,%sp

	mov	$rp,$rp_real
	mov	-1,$minus1
	mov	-2,$poly3
	sllx	$minus1,32,$poly1		! 0xFFFFFFFF00000000
	srl	$poly3,0,$poly3			! 0x00000000FFFFFFFE

	! convert input to uint64_t[4]
	ld	[$bp],$a0			! in2_x
	ld	[$bp+4],$t0
	ld	[$bp+8],$a1
	ld	[$bp+12],$t1
	ld	[$bp+16],$a2
	ld	[$bp+20],$t2
	ld	[$bp+24],$a3
	ld	[$bp+28],$t3
	sllx	$t0,32,$t0
	sllx	$t1,32,$t1
	ld	[$bp+32],$acc0			! in2_y
	or	$a0,$t0,$a0
	ld	[$bp+32+4],$t0
	sllx	$t2,32,$t2
	ld	[$bp+32+8],$acc1
	or	$a1,$t1,$a1
	ld	[$bp+32+12],$t1
	sllx	$t3,32,$t3
	ld	[$bp+32+16],$acc2
	or	$a2,$t2,$a2
	ld	[$bp+32+20],$t2
	or	$a3,$t3,$a3
	ld	[$bp+32+24],$acc3
	sllx	$t0,32,$t0
	ld	[$bp+32+28],$t3
	sllx	$t1,32,$t1
	stx	$a0,[%sp+LOCALS64+$in2_x]
	sllx	$t2,32,$t2
	stx	$a1,[%sp+LOCALS64+$in2_x+8]
	sllx	$t3,32,$t3
	stx	$a2,[%sp+LOCALS64+$in2_x+16]
	or	$acc0,$t0,$acc0
	stx	$a3,[%sp+LOCALS64+$in2_x+24]
	or	$acc1,$t1,$acc1
	stx	$acc0,[%sp+LOCALS64+$in2_y]
	or	$acc2,$t2,$acc2
	stx	$acc1,[%sp+LOCALS64+$in2_y+8]
	or	$acc3,$t3,$acc3
	stx	$acc2,[%sp+LOCALS64+$in2_y+16]
	stx	$acc3,[%sp+LOCALS64+$in2_y+24]

	or	$a1,$a0,$a0
	or	$a3,$a2,$a2
	or	$acc1,$acc0,$acc0
	or	$acc3,$acc2,$acc2
	or	$a2,$a0,$a0
	or	$acc2,$acc0,$acc0
	or	$acc0,$a0,$a0
	movrnz	$a0,-1,$a0			! !in2infty
	stx	$a0,[%fp+STACK_BIAS-8]

	ld	[$ap],$a0			! in1_x
	ld	[$ap+4],$t0
	ld	[$ap+8],$a1
	ld	[$ap+12],$t1
	ld	[$ap+16],$a2
	ld	[$ap+20],$t2
	ld	[$ap+24],$a3
	ld	[$ap+28],$t3
	sllx	$t0,32,$t0
	sllx	$t1,32,$t1
	ld	[$ap+32],$acc0			! in1_y
	or	$a0,$t0,$a0
	ld	[$ap+32+4],$t0
	sllx	$t2,32,$t2
	ld	[$ap+32+8],$acc1
	or	$a1,$t1,$a1
	ld	[$ap+32+12],$t1
	sllx	$t3,32,$t3
	ld	[$ap+32+16],$acc2
	or	$a2,$t2,$a2
	ld	[$ap+32+20],$t2
	or	$a3,$t3,$a3
	ld	[$ap+32+24],$acc3
	sllx	$t0,32,$t0
	ld	[$ap+32+28],$t3
	sllx	$t1,32,$t1
	stx	$a0,[%sp+LOCALS64+$in1_x]
	sllx	$t2,32,$t2
	stx	$a1,[%sp+LOCALS64+$in1_x+8]
	sllx	$t3,32,$t3
	stx	$a2,[%sp+LOCALS64+$in1_x+16]
	or	$acc0,$t0,$acc0
	stx	$a3,[%sp+LOCALS64+$in1_x+24]
	or	$acc1,$t1,$acc1
	stx	$acc0,[%sp+LOCALS64+$in1_y]
	or	$acc2,$t2,$acc2
	stx	$acc1,[%sp+LOCALS64+$in1_y+8]
	or	$acc3,$t3,$acc3
	stx	$acc2,[%sp+LOCALS64+$in1_y+16]
	stx	$acc3,[%sp+LOCALS64+$in1_y+24]

	ld	[$ap+64],$a0			! in1_z
	ld	[$ap+64+4],$t0
	ld	[$ap+64+8],$a1
	ld	[$ap+64+12],$t1
	ld	[$ap+64+16],$a2
	ld	[$ap+64+20],$t2
	ld	[$ap+64+24],$a3
	ld	[$ap+64+28],$t3
	sllx	$t0,32,$t0
	sllx	$t1,32,$t1
	or	$a0,$t0,$a0
	sllx	$t2,32,$t2
	or	$a1,$t1,$a1
	sllx	$t3,32,$t3
	stx	$a0,[%sp+LOCALS64+$in1_z]
	or	$a2,$t2,$a2
	stx	$a1,[%sp+LOCALS64+$in1_z+8]
	or	$a3,$t3,$a3
	stx	$a2,[%sp+LOCALS64+$in1_z+16]
	stx	$a3,[%sp+LOCALS64+$in1_z+24]

	or	$a1,$a0,$t0
	or	$a3,$a2,$t2
	or	$t2,$t0,$t0
	movrnz	$t0,-1,$t0			! !in1infty
	stx	$t0,[%fp+STACK_BIAS-16]

	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(Z1sqr, in1_z);
	add	%sp,LOCALS64+$Z1sqr,$rp

	ldx	[%sp+LOCALS64+$in2_x],$bi
	mov	$acc0,$a0
	mov	$acc1,$a1
	mov	$acc2,$a2
	mov	$acc3,$a3
	add	%sp,LOCALS64+$in2_x,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(U2, Z1sqr, in2_x);
	add	%sp,LOCALS64+$U2,$rp

	ldx	[%sp+LOCALS64+$Z1sqr],$bi	! forward load
	ldx	[%sp+LOCALS64+$in1_z],$a0
	ldx	[%sp+LOCALS64+$in1_z+8],$a1
	ldx	[%sp+LOCALS64+$in1_z+16],$a2
	ldx	[%sp+LOCALS64+$in1_z+24],$a3

	add	%sp,LOCALS64+$in1_x,$bp
	call	__ecp_nistz256_sub_from_vis3	! p256_sub(H, U2, in1_x);
	add	%sp,LOCALS64+$H,$rp

	add	%sp,LOCALS64+$Z1sqr,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(S2, Z1sqr, in1_z);
	add	%sp,LOCALS64+$S2,$rp

	ldx	[%sp+LOCALS64+$H],$bi
	ldx	[%sp+LOCALS64+$in1_z],$a0
	ldx	[%sp+LOCALS64+$in1_z+8],$a1
	ldx	[%sp+LOCALS64+$in1_z+16],$a2
	ldx	[%sp+LOCALS64+$in1_z+24],$a3
	add	%sp,LOCALS64+$H,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(res_z, H, in1_z);
	add	%sp,LOCALS64+$res_z,$rp

	ldx	[%sp+LOCALS64+$S2],$bi
	ldx	[%sp+LOCALS64+$in2_y],$a0
	ldx	[%sp+LOCALS64+$in2_y+8],$a1
	ldx	[%sp+LOCALS64+$in2_y+16],$a2
	ldx	[%sp+LOCALS64+$in2_y+24],$a3
	add	%sp,LOCALS64+$S2,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(S2, S2, in2_y);
	add	%sp,LOCALS64+$S2,$rp

	ldx	[%sp+LOCALS64+$H],$a0		! forward load
	ldx	[%sp+LOCALS64+$H+8],$a1
	ldx	[%sp+LOCALS64+$H+16],$a2
	ldx	[%sp+LOCALS64+$H+24],$a3

	add	%sp,LOCALS64+$in1_y,$bp
	call	__ecp_nistz256_sub_from_vis3	! p256_sub(R, S2, in1_y);
	add	%sp,LOCALS64+$R,$rp

	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(Hsqr, H);
	add	%sp,LOCALS64+$Hsqr,$rp

	ldx	[%sp+LOCALS64+$R],$a0
	ldx	[%sp+LOCALS64+$R+8],$a1
	ldx	[%sp+LOCALS64+$R+16],$a2
	ldx	[%sp+LOCALS64+$R+24],$a3
	call	__ecp_nistz256_sqr_mont_vis3	! p256_sqr_mont(Rsqr, R);
	add	%sp,LOCALS64+$Rsqr,$rp

	ldx	[%sp+LOCALS64+$H],$bi
	ldx	[%sp+LOCALS64+$Hsqr],$a0
	ldx	[%sp+LOCALS64+$Hsqr+8],$a1
	ldx	[%sp+LOCALS64+$Hsqr+16],$a2
	ldx	[%sp+LOCALS64+$Hsqr+24],$a3
	add	%sp,LOCALS64+$H,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(Hcub, Hsqr, H);
	add	%sp,LOCALS64+$Hcub,$rp

	ldx	[%sp+LOCALS64+$Hsqr],$bi
	ldx	[%sp+LOCALS64+$in1_x],$a0
	ldx	[%sp+LOCALS64+$in1_x+8],$a1
	ldx	[%sp+LOCALS64+$in1_x+16],$a2
	ldx	[%sp+LOCALS64+$in1_x+24],$a3
	add	%sp,LOCALS64+$Hsqr,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(U2, in1_x, Hsqr);
	add	%sp,LOCALS64+$U2,$rp

	call	__ecp_nistz256_mul_by_2_vis3	! p256_mul_by_2(Hsqr, U2);
	add	%sp,LOCALS64+$Hsqr,$rp

	add	%sp,LOCALS64+$Rsqr,$bp
	call	__ecp_nistz256_sub_morf_vis3	! p256_sub(res_x, Rsqr, Hsqr);
	add	%sp,LOCALS64+$res_x,$rp

	add	%sp,LOCALS64+$Hcub,$bp
	call	__ecp_nistz256_sub_from_vis3	!  p256_sub(res_x, res_x, Hcub);
	add	%sp,LOCALS64+$res_x,$rp

	ldx	[%sp+LOCALS64+$Hcub],$bi	! forward load
	ldx	[%sp+LOCALS64+$in1_y],$a0
	ldx	[%sp+LOCALS64+$in1_y+8],$a1
	ldx	[%sp+LOCALS64+$in1_y+16],$a2
	ldx	[%sp+LOCALS64+$in1_y+24],$a3

	add	%sp,LOCALS64+$U2,$bp
	call	__ecp_nistz256_sub_morf_vis3	! p256_sub(res_y, U2, res_x);
	add	%sp,LOCALS64+$res_y,$rp

	add	%sp,LOCALS64+$Hcub,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(S2, in1_y, Hcub);
	add	%sp,LOCALS64+$S2,$rp

	ldx	[%sp+LOCALS64+$R],$bi
	ldx	[%sp+LOCALS64+$res_y],$a0
	ldx	[%sp+LOCALS64+$res_y+8],$a1
	ldx	[%sp+LOCALS64+$res_y+16],$a2
	ldx	[%sp+LOCALS64+$res_y+24],$a3
	add	%sp,LOCALS64+$R,$bp
	call	__ecp_nistz256_mul_mont_vis3	! p256_mul_mont(res_y, res_y, R);
	add	%sp,LOCALS64+$res_y,$rp

	add	%sp,LOCALS64+$S2,$bp
	call	__ecp_nistz256_sub_from_vis3	! p256_sub(res_y, res_y, S2);
	add	%sp,LOCALS64+$res_y,$rp

	ldx	[%fp+STACK_BIAS-16],$t1		! !in1infty
	ldx	[%fp+STACK_BIAS-8],$t2		! !in2infty
1:	call	.+8
	add	%o7,.Lone_mont_vis3-1b,$bp
___
for($i=0;$i<64;$i+=16) {			# conditional moves
$code.=<<___;
	ldx	[%sp+LOCALS64+$res_x+$i],$acc0	! res
	ldx	[%sp+LOCALS64+$res_x+$i+8],$acc1
	ldx	[%sp+LOCALS64+$in2_x+$i],$acc2	! in2
	ldx	[%sp+LOCALS64+$in2_x+$i+8],$acc3
	ldx	[%sp+LOCALS64+$in1_x+$i],$acc4	! in1
	ldx	[%sp+LOCALS64+$in1_x+$i+8],$acc5
	movrz	$t1,$acc2,$acc0
	movrz	$t1,$acc3,$acc1
	movrz	$t2,$acc4,$acc0
	movrz	$t2,$acc5,$acc1
	srlx	$acc0,32,$acc2
	srlx	$acc1,32,$acc3
	st	$acc0,[$rp_real+$i]
	st	$acc2,[$rp_real+$i+4]
	st	$acc1,[$rp_real+$i+8]
	st	$acc3,[$rp_real+$i+12]
___
}
for(;$i<96;$i+=16) {
$code.=<<___;
	ldx	[%sp+LOCALS64+$res_x+$i],$acc0	! res
	ldx	[%sp+LOCALS64+$res_x+$i+8],$acc1
	ldx	[$bp+$i-64],$acc2		! "in2"
	ldx	[$bp+$i-64+8],$acc3
	ldx	[%sp+LOCALS64+$in1_x+$i],$acc4	! in1
	ldx	[%sp+LOCALS64+$in1_x+$i+8],$acc5
	movrz	$t1,$acc2,$acc0
	movrz	$t1,$acc3,$acc1
	movrz	$t2,$acc4,$acc0
	movrz	$t2,$acc5,$acc1
	srlx	$acc0,32,$acc2
	srlx	$acc1,32,$acc3
	st	$acc0,[$rp_real+$i]
	st	$acc2,[$rp_real+$i+4]
	st	$acc1,[$rp_real+$i+8]
	st	$acc3,[$rp_real+$i+12]
___
}
$code.=<<___;
	ret
	restore
.type	ecp_nistz256_point_add_affine_vis3,#function
.size	ecp_nistz256_point_add_affine_vis3,.-ecp_nistz256_point_add_affine_vis3
.align	64
.Lone_mont_vis3:
.long	0x00000000,0x00000001, 0xffffffff,0x00000000
.long	0xffffffff,0xffffffff, 0x00000000,0xfffffffe
.align	64
___
}								}}}

# Purpose of these subroutines is to explicitly encode VIS instructions,
# so that one can compile the module without having to specify VIS
# extensions on compiler command line, e.g. -xarch=v9 vs. -xarch=v9a.
# Idea is to reserve for option to produce "universal" binary and let
# programmer detect if current CPU is VIS capable at run-time.
sub unvis3 {
my ($mnemonic,$rs1,$rs2,$rd)=@_;
my %bias = ( "g" => 0, "o" => 8, "l" => 16, "i" => 24 );
my ($ref,$opf);
my %visopf = (	"addxc"		=> 0x011,
		"addxccc"	=> 0x013,
		"umulxhi"	=> 0x016	);

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

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	s/\b(umulxhi|addxc[c]{0,2})\s+(%[goli][0-7]),\s*(%[goli][0-7]),\s*(%[goli][0-7])/
		&unvis3($1,$2,$3,$4)
	 /ge;

	print $_,"\n";
}

close STDOUT;

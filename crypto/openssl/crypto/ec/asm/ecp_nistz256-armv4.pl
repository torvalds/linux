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
# ECP_NISTZ256 module for ARMv4.
#
# October 2014.
#
# Original ECP_NISTZ256 submission targeting x86_64 is detailed in
# http://eprint.iacr.org/2013/816. In the process of adaptation
# original .c module was made 32-bit savvy in order to make this
# implementation possible.
#
#			with/without -DECP_NISTZ256_ASM
# Cortex-A8		+53-170%
# Cortex-A9		+76-205%
# Cortex-A15		+100-316%
# Snapdragon S4		+66-187%
#
# Ranges denote minimum and maximum improvement coefficients depending
# on benchmark. Lower coefficients are for ECDSA sign, server-side
# operation. Keep in mind that +200% means 3x improvement.

$flavour = shift;
if ($flavour=~/\w[\w\-]*\.\w+$/) { $output=$flavour; undef $flavour; }
else { while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {} }

if ($flavour && $flavour ne "void") {
    $0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
    ( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
    ( $xlate="${dir}../../perlasm/arm-xlate.pl" and -f $xlate) or
    die "can't locate arm-xlate.pl";

    open STDOUT,"| \"$^X\" $xlate $flavour $output";
} else {
    open STDOUT,">$output";
}

$code.=<<___;
#include "arm_arch.h"

.text
#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif
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
.type	ecp_nistz256_precomputed,%object
.align	12
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
$code.=<<___;
.size	ecp_nistz256_precomputed,.-ecp_nistz256_precomputed
.align	5
.LRR:	@ 2^512 mod P precomputed for NIST P256 polynomial
.long	0x00000003, 0x00000000, 0xffffffff, 0xfffffffb
.long	0xfffffffe, 0xffffffff, 0xfffffffd, 0x00000004
.Lone:
.long	1,0,0,0,0,0,0,0
.asciz	"ECP_NISTZ256 for ARMv4, CRYPTOGAMS by <appro\@openssl.org>"
.align	6
___

########################################################################
# common register layout, note that $t2 is link register, so that if
# internal subroutine uses $t2, then it has to offload lr...

($r_ptr,$a_ptr,$b_ptr,$ff,$a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7,$t1,$t2)=
		map("r$_",(0..12,14));
($t0,$t3)=($ff,$a_ptr);

$code.=<<___;
@ void	ecp_nistz256_to_mont(BN_ULONG r0[8],const BN_ULONG r1[8]);
.globl	ecp_nistz256_to_mont
.type	ecp_nistz256_to_mont,%function
ecp_nistz256_to_mont:
	adr	$b_ptr,.LRR
	b	.Lecp_nistz256_mul_mont
.size	ecp_nistz256_to_mont,.-ecp_nistz256_to_mont

@ void	ecp_nistz256_from_mont(BN_ULONG r0[8],const BN_ULONG r1[8]);
.globl	ecp_nistz256_from_mont
.type	ecp_nistz256_from_mont,%function
ecp_nistz256_from_mont:
	adr	$b_ptr,.Lone
	b	.Lecp_nistz256_mul_mont
.size	ecp_nistz256_from_mont,.-ecp_nistz256_from_mont

@ void	ecp_nistz256_mul_by_2(BN_ULONG r0[8],const BN_ULONG r1[8]);
.globl	ecp_nistz256_mul_by_2
.type	ecp_nistz256_mul_by_2,%function
.align	4
ecp_nistz256_mul_by_2:
	stmdb	sp!,{r4-r12,lr}
	bl	__ecp_nistz256_mul_by_2
#if __ARM_ARCH__>=5 || !defined(__thumb__)
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	ecp_nistz256_mul_by_2,.-ecp_nistz256_mul_by_2

.type	__ecp_nistz256_mul_by_2,%function
.align	4
__ecp_nistz256_mul_by_2:
	ldr	$a0,[$a_ptr,#0]
	ldr	$a1,[$a_ptr,#4]
	ldr	$a2,[$a_ptr,#8]
	adds	$a0,$a0,$a0		@ a[0:7]+=a[0:7], i.e. add with itself
	ldr	$a3,[$a_ptr,#12]
	adcs	$a1,$a1,$a1
	ldr	$a4,[$a_ptr,#16]
	adcs	$a2,$a2,$a2
	ldr	$a5,[$a_ptr,#20]
	adcs	$a3,$a3,$a3
	ldr	$a6,[$a_ptr,#24]
	adcs	$a4,$a4,$a4
	ldr	$a7,[$a_ptr,#28]
	adcs	$a5,$a5,$a5
	adcs	$a6,$a6,$a6
	mov	$ff,#0
	adcs	$a7,$a7,$a7
	adc	$ff,$ff,#0

	b	.Lreduce_by_sub
.size	__ecp_nistz256_mul_by_2,.-__ecp_nistz256_mul_by_2

@ void	ecp_nistz256_add(BN_ULONG r0[8],const BN_ULONG r1[8],
@					const BN_ULONG r2[8]);
.globl	ecp_nistz256_add
.type	ecp_nistz256_add,%function
.align	4
ecp_nistz256_add:
	stmdb	sp!,{r4-r12,lr}
	bl	__ecp_nistz256_add
#if __ARM_ARCH__>=5 || !defined(__thumb__)
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	ecp_nistz256_add,.-ecp_nistz256_add

.type	__ecp_nistz256_add,%function
.align	4
__ecp_nistz256_add:
	str	lr,[sp,#-4]!		@ push lr

	ldr	$a0,[$a_ptr,#0]
	ldr	$a1,[$a_ptr,#4]
	ldr	$a2,[$a_ptr,#8]
	ldr	$a3,[$a_ptr,#12]
	ldr	$a4,[$a_ptr,#16]
	 ldr	$t0,[$b_ptr,#0]
	ldr	$a5,[$a_ptr,#20]
	 ldr	$t1,[$b_ptr,#4]
	ldr	$a6,[$a_ptr,#24]
	 ldr	$t2,[$b_ptr,#8]
	ldr	$a7,[$a_ptr,#28]
	 ldr	$t3,[$b_ptr,#12]
	adds	$a0,$a0,$t0
	 ldr	$t0,[$b_ptr,#16]
	adcs	$a1,$a1,$t1
	 ldr	$t1,[$b_ptr,#20]
	adcs	$a2,$a2,$t2
	 ldr	$t2,[$b_ptr,#24]
	adcs	$a3,$a3,$t3
	 ldr	$t3,[$b_ptr,#28]
	adcs	$a4,$a4,$t0
	adcs	$a5,$a5,$t1
	adcs	$a6,$a6,$t2
	mov	$ff,#0
	adcs	$a7,$a7,$t3
	adc	$ff,$ff,#0
	ldr	lr,[sp],#4		@ pop lr

.Lreduce_by_sub:

	@ if a+b >= modulus, subtract modulus.
	@
	@ But since comparison implies subtraction, we subtract
	@ modulus and then add it back if subtraction borrowed.

	subs	$a0,$a0,#-1
	sbcs	$a1,$a1,#-1
	sbcs	$a2,$a2,#-1
	sbcs	$a3,$a3,#0
	sbcs	$a4,$a4,#0
	sbcs	$a5,$a5,#0
	sbcs	$a6,$a6,#1
	sbcs	$a7,$a7,#-1
	sbc	$ff,$ff,#0

	@ Note that because mod has special form, i.e. consists of
	@ 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	@ using value of borrow as a whole or extracting single bit.
	@ Follow $ff register...

	adds	$a0,$a0,$ff		@ add synthesized modulus
	adcs	$a1,$a1,$ff
	str	$a0,[$r_ptr,#0]
	adcs	$a2,$a2,$ff
	str	$a1,[$r_ptr,#4]
	adcs	$a3,$a3,#0
	str	$a2,[$r_ptr,#8]
	adcs	$a4,$a4,#0
	str	$a3,[$r_ptr,#12]
	adcs	$a5,$a5,#0
	str	$a4,[$r_ptr,#16]
	adcs	$a6,$a6,$ff,lsr#31
	str	$a5,[$r_ptr,#20]
	adcs	$a7,$a7,$ff
	str	$a6,[$r_ptr,#24]
	str	$a7,[$r_ptr,#28]

	mov	pc,lr
.size	__ecp_nistz256_add,.-__ecp_nistz256_add

@ void	ecp_nistz256_mul_by_3(BN_ULONG r0[8],const BN_ULONG r1[8]);
.globl	ecp_nistz256_mul_by_3
.type	ecp_nistz256_mul_by_3,%function
.align	4
ecp_nistz256_mul_by_3:
	stmdb	sp!,{r4-r12,lr}
	bl	__ecp_nistz256_mul_by_3
#if __ARM_ARCH__>=5 || !defined(__thumb__)
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	ecp_nistz256_mul_by_3,.-ecp_nistz256_mul_by_3

.type	__ecp_nistz256_mul_by_3,%function
.align	4
__ecp_nistz256_mul_by_3:
	str	lr,[sp,#-4]!		@ push lr

	@ As multiplication by 3 is performed as 2*n+n, below are inline
	@ copies of __ecp_nistz256_mul_by_2 and __ecp_nistz256_add, see
	@ corresponding subroutines for details.

	ldr	$a0,[$a_ptr,#0]
	ldr	$a1,[$a_ptr,#4]
	ldr	$a2,[$a_ptr,#8]
	adds	$a0,$a0,$a0		@ a[0:7]+=a[0:7]
	ldr	$a3,[$a_ptr,#12]
	adcs	$a1,$a1,$a1
	ldr	$a4,[$a_ptr,#16]
	adcs	$a2,$a2,$a2
	ldr	$a5,[$a_ptr,#20]
	adcs	$a3,$a3,$a3
	ldr	$a6,[$a_ptr,#24]
	adcs	$a4,$a4,$a4
	ldr	$a7,[$a_ptr,#28]
	adcs	$a5,$a5,$a5
	adcs	$a6,$a6,$a6
	mov	$ff,#0
	adcs	$a7,$a7,$a7
	adc	$ff,$ff,#0

	subs	$a0,$a0,#-1		@ .Lreduce_by_sub but without stores
	sbcs	$a1,$a1,#-1
	sbcs	$a2,$a2,#-1
	sbcs	$a3,$a3,#0
	sbcs	$a4,$a4,#0
	sbcs	$a5,$a5,#0
	sbcs	$a6,$a6,#1
	sbcs	$a7,$a7,#-1
	sbc	$ff,$ff,#0

	adds	$a0,$a0,$ff		@ add synthesized modulus
	adcs	$a1,$a1,$ff
	adcs	$a2,$a2,$ff
	adcs	$a3,$a3,#0
	adcs	$a4,$a4,#0
	 ldr	$b_ptr,[$a_ptr,#0]
	adcs	$a5,$a5,#0
	 ldr	$t1,[$a_ptr,#4]
	adcs	$a6,$a6,$ff,lsr#31
	 ldr	$t2,[$a_ptr,#8]
	adc	$a7,$a7,$ff

	ldr	$t0,[$a_ptr,#12]
	adds	$a0,$a0,$b_ptr		@ 2*a[0:7]+=a[0:7]
	ldr	$b_ptr,[$a_ptr,#16]
	adcs	$a1,$a1,$t1
	ldr	$t1,[$a_ptr,#20]
	adcs	$a2,$a2,$t2
	ldr	$t2,[$a_ptr,#24]
	adcs	$a3,$a3,$t0
	ldr	$t3,[$a_ptr,#28]
	adcs	$a4,$a4,$b_ptr
	adcs	$a5,$a5,$t1
	adcs	$a6,$a6,$t2
	mov	$ff,#0
	adcs	$a7,$a7,$t3
	adc	$ff,$ff,#0
	ldr	lr,[sp],#4		@ pop lr

	b	.Lreduce_by_sub
.size	ecp_nistz256_mul_by_3,.-ecp_nistz256_mul_by_3

@ void	ecp_nistz256_div_by_2(BN_ULONG r0[8],const BN_ULONG r1[8]);
.globl	ecp_nistz256_div_by_2
.type	ecp_nistz256_div_by_2,%function
.align	4
ecp_nistz256_div_by_2:
	stmdb	sp!,{r4-r12,lr}
	bl	__ecp_nistz256_div_by_2
#if __ARM_ARCH__>=5 || !defined(__thumb__)
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	ecp_nistz256_div_by_2,.-ecp_nistz256_div_by_2

.type	__ecp_nistz256_div_by_2,%function
.align	4
__ecp_nistz256_div_by_2:
	@ ret = (a is odd ? a+mod : a) >> 1

	ldr	$a0,[$a_ptr,#0]
	ldr	$a1,[$a_ptr,#4]
	ldr	$a2,[$a_ptr,#8]
	mov	$ff,$a0,lsl#31		@ place least significant bit to most
					@ significant position, now arithmetic
					@ right shift by 31 will produce -1 or
					@ 0, while logical right shift 1 or 0,
					@ this is how modulus is conditionally
					@ synthesized in this case...
	ldr	$a3,[$a_ptr,#12]
	adds	$a0,$a0,$ff,asr#31
	ldr	$a4,[$a_ptr,#16]
	adcs	$a1,$a1,$ff,asr#31
	ldr	$a5,[$a_ptr,#20]
	adcs	$a2,$a2,$ff,asr#31
	ldr	$a6,[$a_ptr,#24]
	adcs	$a3,$a3,#0
	ldr	$a7,[$a_ptr,#28]
	adcs	$a4,$a4,#0
	 mov	$a0,$a0,lsr#1		@ a[0:7]>>=1, we can start early
					@ because it doesn't affect flags
	adcs	$a5,$a5,#0
	 orr	$a0,$a0,$a1,lsl#31
	adcs	$a6,$a6,$ff,lsr#31
	mov	$b_ptr,#0
	adcs	$a7,$a7,$ff,asr#31
	 mov	$a1,$a1,lsr#1
	adc	$b_ptr,$b_ptr,#0	@ top-most carry bit from addition

	orr	$a1,$a1,$a2,lsl#31
	mov	$a2,$a2,lsr#1
	str	$a0,[$r_ptr,#0]
	orr	$a2,$a2,$a3,lsl#31
	mov	$a3,$a3,lsr#1
	str	$a1,[$r_ptr,#4]
	orr	$a3,$a3,$a4,lsl#31
	mov	$a4,$a4,lsr#1
	str	$a2,[$r_ptr,#8]
	orr	$a4,$a4,$a5,lsl#31
	mov	$a5,$a5,lsr#1
	str	$a3,[$r_ptr,#12]
	orr	$a5,$a5,$a6,lsl#31
	mov	$a6,$a6,lsr#1
	str	$a4,[$r_ptr,#16]
	orr	$a6,$a6,$a7,lsl#31
	mov	$a7,$a7,lsr#1
	str	$a5,[$r_ptr,#20]
	orr	$a7,$a7,$b_ptr,lsl#31	@ don't forget the top-most carry bit
	str	$a6,[$r_ptr,#24]
	str	$a7,[$r_ptr,#28]

	mov	pc,lr
.size	__ecp_nistz256_div_by_2,.-__ecp_nistz256_div_by_2

@ void	ecp_nistz256_sub(BN_ULONG r0[8],const BN_ULONG r1[8],
@				        const BN_ULONG r2[8]);
.globl	ecp_nistz256_sub
.type	ecp_nistz256_sub,%function
.align	4
ecp_nistz256_sub:
	stmdb	sp!,{r4-r12,lr}
	bl	__ecp_nistz256_sub
#if __ARM_ARCH__>=5 || !defined(__thumb__)
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	ecp_nistz256_sub,.-ecp_nistz256_sub

.type	__ecp_nistz256_sub,%function
.align	4
__ecp_nistz256_sub:
	str	lr,[sp,#-4]!		@ push lr

	ldr	$a0,[$a_ptr,#0]
	ldr	$a1,[$a_ptr,#4]
	ldr	$a2,[$a_ptr,#8]
	ldr	$a3,[$a_ptr,#12]
	ldr	$a4,[$a_ptr,#16]
	 ldr	$t0,[$b_ptr,#0]
	ldr	$a5,[$a_ptr,#20]
	 ldr	$t1,[$b_ptr,#4]
	ldr	$a6,[$a_ptr,#24]
	 ldr	$t2,[$b_ptr,#8]
	ldr	$a7,[$a_ptr,#28]
	 ldr	$t3,[$b_ptr,#12]
	subs	$a0,$a0,$t0
	 ldr	$t0,[$b_ptr,#16]
	sbcs	$a1,$a1,$t1
	 ldr	$t1,[$b_ptr,#20]
	sbcs	$a2,$a2,$t2
	 ldr	$t2,[$b_ptr,#24]
	sbcs	$a3,$a3,$t3
	 ldr	$t3,[$b_ptr,#28]
	sbcs	$a4,$a4,$t0
	sbcs	$a5,$a5,$t1
	sbcs	$a6,$a6,$t2
	sbcs	$a7,$a7,$t3
	sbc	$ff,$ff,$ff		@ broadcast borrow bit
	ldr	lr,[sp],#4		@ pop lr

.Lreduce_by_add:

	@ if a-b borrows, add modulus.
	@
	@ Note that because mod has special form, i.e. consists of
	@ 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	@ broadcasting borrow bit to a register, $ff, and using it as
	@ a whole or extracting single bit.

	adds	$a0,$a0,$ff		@ add synthesized modulus
	adcs	$a1,$a1,$ff
	str	$a0,[$r_ptr,#0]
	adcs	$a2,$a2,$ff
	str	$a1,[$r_ptr,#4]
	adcs	$a3,$a3,#0
	str	$a2,[$r_ptr,#8]
	adcs	$a4,$a4,#0
	str	$a3,[$r_ptr,#12]
	adcs	$a5,$a5,#0
	str	$a4,[$r_ptr,#16]
	adcs	$a6,$a6,$ff,lsr#31
	str	$a5,[$r_ptr,#20]
	adcs	$a7,$a7,$ff
	str	$a6,[$r_ptr,#24]
	str	$a7,[$r_ptr,#28]

	mov	pc,lr
.size	__ecp_nistz256_sub,.-__ecp_nistz256_sub

@ void	ecp_nistz256_neg(BN_ULONG r0[8],const BN_ULONG r1[8]);
.globl	ecp_nistz256_neg
.type	ecp_nistz256_neg,%function
.align	4
ecp_nistz256_neg:
	stmdb	sp!,{r4-r12,lr}
	bl	__ecp_nistz256_neg
#if __ARM_ARCH__>=5 || !defined(__thumb__)
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	ecp_nistz256_neg,.-ecp_nistz256_neg

.type	__ecp_nistz256_neg,%function
.align	4
__ecp_nistz256_neg:
	ldr	$a0,[$a_ptr,#0]
	eor	$ff,$ff,$ff
	ldr	$a1,[$a_ptr,#4]
	ldr	$a2,[$a_ptr,#8]
	subs	$a0,$ff,$a0
	ldr	$a3,[$a_ptr,#12]
	sbcs	$a1,$ff,$a1
	ldr	$a4,[$a_ptr,#16]
	sbcs	$a2,$ff,$a2
	ldr	$a5,[$a_ptr,#20]
	sbcs	$a3,$ff,$a3
	ldr	$a6,[$a_ptr,#24]
	sbcs	$a4,$ff,$a4
	ldr	$a7,[$a_ptr,#28]
	sbcs	$a5,$ff,$a5
	sbcs	$a6,$ff,$a6
	sbcs	$a7,$ff,$a7
	sbc	$ff,$ff,$ff

	b	.Lreduce_by_add
.size	__ecp_nistz256_neg,.-__ecp_nistz256_neg
___
{
my @acc=map("r$_",(3..11));
my ($t0,$t1,$bj,$t2,$t3)=map("r$_",(0,1,2,12,14));

$code.=<<___;
@ void	ecp_nistz256_sqr_mont(BN_ULONG r0[8],const BN_ULONG r1[8]);
.globl	ecp_nistz256_sqr_mont
.type	ecp_nistz256_sqr_mont,%function
.align	4
ecp_nistz256_sqr_mont:
	mov	$b_ptr,$a_ptr
	b	.Lecp_nistz256_mul_mont
.size	ecp_nistz256_sqr_mont,.-ecp_nistz256_sqr_mont

@ void	ecp_nistz256_mul_mont(BN_ULONG r0[8],const BN_ULONG r1[8],
@					     const BN_ULONG r2[8]);
.globl	ecp_nistz256_mul_mont
.type	ecp_nistz256_mul_mont,%function
.align	4
ecp_nistz256_mul_mont:
.Lecp_nistz256_mul_mont:
	stmdb	sp!,{r4-r12,lr}
	bl	__ecp_nistz256_mul_mont
#if __ARM_ARCH__>=5 || !defined(__thumb__)
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	ecp_nistz256_mul_mont,.-ecp_nistz256_mul_mont

.type	__ecp_nistz256_mul_mont,%function
.align	4
__ecp_nistz256_mul_mont:
	stmdb	sp!,{r0-r2,lr}			@ make a copy of arguments too

	ldr	$bj,[$b_ptr,#0]			@ b[0]
	ldmia	$a_ptr,{@acc[1]-@acc[8]}

	umull	@acc[0],$t3,@acc[1],$bj		@ r[0]=a[0]*b[0]
	stmdb	sp!,{$acc[1]-@acc[8]}		@ copy a[0-7] to stack, so
						@ that it can be addressed
						@ without spending register
						@ on address
	umull	@acc[1],$t0,@acc[2],$bj		@ r[1]=a[1]*b[0]
	umull	@acc[2],$t1,@acc[3],$bj
	adds	@acc[1],@acc[1],$t3		@ accumulate high part of mult
	umull	@acc[3],$t2,@acc[4],$bj
	adcs	@acc[2],@acc[2],$t0
	umull	@acc[4],$t3,@acc[5],$bj
	adcs	@acc[3],@acc[3],$t1
	umull	@acc[5],$t0,@acc[6],$bj
	adcs	@acc[4],@acc[4],$t2
	umull	@acc[6],$t1,@acc[7],$bj
	adcs	@acc[5],@acc[5],$t3
	umull	@acc[7],$t2,@acc[8],$bj
	adcs	@acc[6],@acc[6],$t0
	adcs	@acc[7],@acc[7],$t1
	eor	$t3,$t3,$t3			@ first overflow bit is zero
	adc	@acc[8],$t2,#0
___
for(my $i=1;$i<8;$i++) {
my $t4=@acc[0];

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
	@ multiplication-less reduction $i
	adds	@acc[3],@acc[3],@acc[0]		@ r[3]+=r[0]
	 ldr	$bj,[sp,#40]			@ restore b_ptr
	adcs	@acc[4],@acc[4],#0		@ r[4]+=0
	adcs	@acc[5],@acc[5],#0		@ r[5]+=0
	adcs	@acc[6],@acc[6],@acc[0]		@ r[6]+=r[0]
	 ldr	$t1,[sp,#0]			@ load a[0]
	adcs	@acc[7],@acc[7],#0		@ r[7]+=0
	 ldr	$bj,[$bj,#4*$i]			@ load b[i]
	adcs	@acc[8],@acc[8],@acc[0]		@ r[8]+=r[0]
	 eor	$t0,$t0,$t0
	adc	$t3,$t3,#0			@ overflow bit
	subs	@acc[7],@acc[7],@acc[0]		@ r[7]-=r[0]
	 ldr	$t2,[sp,#4]			@ a[1]
	sbcs	@acc[8],@acc[8],#0		@ r[8]-=0
	 umlal	@acc[1],$t0,$t1,$bj		@ "r[0]"+=a[0]*b[i]
	 eor	$t1,$t1,$t1
	sbc	@acc[0],$t3,#0			@ overflow bit, keep in mind
						@ that netto result is
						@ addition of a value which
						@ makes underflow impossible

	ldr	$t3,[sp,#8]			@ a[2]
	umlal	@acc[2],$t1,$t2,$bj		@ "r[1]"+=a[1]*b[i]
	 str	@acc[0],[sp,#36]		@ temporarily offload overflow
	eor	$t2,$t2,$t2
	ldr	$t4,[sp,#12]			@ a[3], $t4 is alias @acc[0]
	umlal	@acc[3],$t2,$t3,$bj		@ "r[2]"+=a[2]*b[i]
	eor	$t3,$t3,$t3
	adds	@acc[2],@acc[2],$t0		@ accumulate high part of mult
	ldr	$t0,[sp,#16]			@ a[4]
	umlal	@acc[4],$t3,$t4,$bj		@ "r[3]"+=a[3]*b[i]
	eor	$t4,$t4,$t4
	adcs	@acc[3],@acc[3],$t1
	ldr	$t1,[sp,#20]			@ a[5]
	umlal	@acc[5],$t4,$t0,$bj		@ "r[4]"+=a[4]*b[i]
	eor	$t0,$t0,$t0
	adcs	@acc[4],@acc[4],$t2
	ldr	$t2,[sp,#24]			@ a[6]
	umlal	@acc[6],$t0,$t1,$bj		@ "r[5]"+=a[5]*b[i]
	eor	$t1,$t1,$t1
	adcs	@acc[5],@acc[5],$t3
	ldr	$t3,[sp,#28]			@ a[7]
	umlal	@acc[7],$t1,$t2,$bj		@ "r[6]"+=a[6]*b[i]
	eor	$t2,$t2,$t2
	adcs	@acc[6],@acc[6],$t4
	 ldr	@acc[0],[sp,#36]		@ restore overflow bit
	umlal	@acc[8],$t2,$t3,$bj		@ "r[7]"+=a[7]*b[i]
	eor	$t3,$t3,$t3
	adcs	@acc[7],@acc[7],$t0
	adcs	@acc[8],@acc[8],$t1
	adcs	@acc[0],$acc[0],$t2
	adc	$t3,$t3,#0			@ new overflow bit
___
	push(@acc,shift(@acc));			# rotate registers, so that
						# "r[i]" becomes r[i]
}
$code.=<<___;
	@ last multiplication-less reduction
	adds	@acc[3],@acc[3],@acc[0]
	ldr	$r_ptr,[sp,#32]			@ restore r_ptr
	adcs	@acc[4],@acc[4],#0
	adcs	@acc[5],@acc[5],#0
	adcs	@acc[6],@acc[6],@acc[0]
	adcs	@acc[7],@acc[7],#0
	adcs	@acc[8],@acc[8],@acc[0]
	adc	$t3,$t3,#0
	subs	@acc[7],@acc[7],@acc[0]
	sbcs	@acc[8],@acc[8],#0
	sbc	@acc[0],$t3,#0			@ overflow bit

	@ Final step is "if result > mod, subtract mod", but we do it
	@ "other way around", namely subtract modulus from result
	@ and if it borrowed, add modulus back.

	adds	@acc[1],@acc[1],#1		@ subs	@acc[1],@acc[1],#-1
	adcs	@acc[2],@acc[2],#0		@ sbcs	@acc[2],@acc[2],#-1
	adcs	@acc[3],@acc[3],#0		@ sbcs	@acc[3],@acc[3],#-1
	sbcs	@acc[4],@acc[4],#0
	sbcs	@acc[5],@acc[5],#0
	sbcs	@acc[6],@acc[6],#0
	sbcs	@acc[7],@acc[7],#1
	adcs	@acc[8],@acc[8],#0		@ sbcs	@acc[8],@acc[8],#-1
	ldr	lr,[sp,#44]			@ restore lr
	sbc	@acc[0],@acc[0],#0		@ broadcast borrow bit
	add	sp,sp,#48

	@ Note that because mod has special form, i.e. consists of
	@ 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	@ broadcasting borrow bit to a register, @acc[0], and using it as
	@ a whole or extracting single bit.

	adds	@acc[1],@acc[1],@acc[0]		@ add modulus or zero
	adcs	@acc[2],@acc[2],@acc[0]
	str	@acc[1],[$r_ptr,#0]
	adcs	@acc[3],@acc[3],@acc[0]
	str	@acc[2],[$r_ptr,#4]
	adcs	@acc[4],@acc[4],#0
	str	@acc[3],[$r_ptr,#8]
	adcs	@acc[5],@acc[5],#0
	str	@acc[4],[$r_ptr,#12]
	adcs	@acc[6],@acc[6],#0
	str	@acc[5],[$r_ptr,#16]
	adcs	@acc[7],@acc[7],@acc[0],lsr#31
	str	@acc[6],[$r_ptr,#20]
	adc	@acc[8],@acc[8],@acc[0]
	str	@acc[7],[$r_ptr,#24]
	str	@acc[8],[$r_ptr,#28]

	mov	pc,lr
.size	__ecp_nistz256_mul_mont,.-__ecp_nistz256_mul_mont
___
}

{
my ($out,$inp,$index,$mask)=map("r$_",(0..3));
$code.=<<___;
@ void	ecp_nistz256_scatter_w5(void *r0,const P256_POINT *r1,
@					 int r2);
.globl	ecp_nistz256_scatter_w5
.type	ecp_nistz256_scatter_w5,%function
.align	5
ecp_nistz256_scatter_w5:
	stmdb	sp!,{r4-r11}

	add	$out,$out,$index,lsl#2

	ldmia	$inp!,{r4-r11}		@ X
	str	r4,[$out,#64*0-4]
	str	r5,[$out,#64*1-4]
	str	r6,[$out,#64*2-4]
	str	r7,[$out,#64*3-4]
	str	r8,[$out,#64*4-4]
	str	r9,[$out,#64*5-4]
	str	r10,[$out,#64*6-4]
	str	r11,[$out,#64*7-4]
	add	$out,$out,#64*8

	ldmia	$inp!,{r4-r11}		@ Y
	str	r4,[$out,#64*0-4]
	str	r5,[$out,#64*1-4]
	str	r6,[$out,#64*2-4]
	str	r7,[$out,#64*3-4]
	str	r8,[$out,#64*4-4]
	str	r9,[$out,#64*5-4]
	str	r10,[$out,#64*6-4]
	str	r11,[$out,#64*7-4]
	add	$out,$out,#64*8

	ldmia	$inp,{r4-r11}		@ Z
	str	r4,[$out,#64*0-4]
	str	r5,[$out,#64*1-4]
	str	r6,[$out,#64*2-4]
	str	r7,[$out,#64*3-4]
	str	r8,[$out,#64*4-4]
	str	r9,[$out,#64*5-4]
	str	r10,[$out,#64*6-4]
	str	r11,[$out,#64*7-4]

	ldmia	sp!,{r4-r11}
#if __ARM_ARCH__>=5 || defined(__thumb__)
	bx	lr
#else
	mov	pc,lr
#endif
.size	ecp_nistz256_scatter_w5,.-ecp_nistz256_scatter_w5

@ void	ecp_nistz256_gather_w5(P256_POINT *r0,const void *r1,
@					      int r2);
.globl	ecp_nistz256_gather_w5
.type	ecp_nistz256_gather_w5,%function
.align	5
ecp_nistz256_gather_w5:
	stmdb	sp!,{r4-r11}

	cmp	$index,#0
	mov	$mask,#0
#ifdef	__thumb2__
	itt	ne
#endif
	subne	$index,$index,#1
	movne	$mask,#-1
	add	$inp,$inp,$index,lsl#2

	ldr	r4,[$inp,#64*0]
	ldr	r5,[$inp,#64*1]
	ldr	r6,[$inp,#64*2]
	and	r4,r4,$mask
	ldr	r7,[$inp,#64*3]
	and	r5,r5,$mask
	ldr	r8,[$inp,#64*4]
	and	r6,r6,$mask
	ldr	r9,[$inp,#64*5]
	and	r7,r7,$mask
	ldr	r10,[$inp,#64*6]
	and	r8,r8,$mask
	ldr	r11,[$inp,#64*7]
	add	$inp,$inp,#64*8
	and	r9,r9,$mask
	and	r10,r10,$mask
	and	r11,r11,$mask
	stmia	$out!,{r4-r11}	@ X

	ldr	r4,[$inp,#64*0]
	ldr	r5,[$inp,#64*1]
	ldr	r6,[$inp,#64*2]
	and	r4,r4,$mask
	ldr	r7,[$inp,#64*3]
	and	r5,r5,$mask
	ldr	r8,[$inp,#64*4]
	and	r6,r6,$mask
	ldr	r9,[$inp,#64*5]
	and	r7,r7,$mask
	ldr	r10,[$inp,#64*6]
	and	r8,r8,$mask
	ldr	r11,[$inp,#64*7]
	add	$inp,$inp,#64*8
	and	r9,r9,$mask
	and	r10,r10,$mask
	and	r11,r11,$mask
	stmia	$out!,{r4-r11}	@ Y

	ldr	r4,[$inp,#64*0]
	ldr	r5,[$inp,#64*1]
	ldr	r6,[$inp,#64*2]
	and	r4,r4,$mask
	ldr	r7,[$inp,#64*3]
	and	r5,r5,$mask
	ldr	r8,[$inp,#64*4]
	and	r6,r6,$mask
	ldr	r9,[$inp,#64*5]
	and	r7,r7,$mask
	ldr	r10,[$inp,#64*6]
	and	r8,r8,$mask
	ldr	r11,[$inp,#64*7]
	and	r9,r9,$mask
	and	r10,r10,$mask
	and	r11,r11,$mask
	stmia	$out,{r4-r11}		@ Z

	ldmia	sp!,{r4-r11}
#if __ARM_ARCH__>=5 || defined(__thumb__)
	bx	lr
#else
	mov	pc,lr
#endif
.size	ecp_nistz256_gather_w5,.-ecp_nistz256_gather_w5

@ void	ecp_nistz256_scatter_w7(void *r0,const P256_POINT_AFFINE *r1,
@					 int r2);
.globl	ecp_nistz256_scatter_w7
.type	ecp_nistz256_scatter_w7,%function
.align	5
ecp_nistz256_scatter_w7:
	add	$out,$out,$index
	mov	$index,#64/4
.Loop_scatter_w7:
	ldr	$mask,[$inp],#4
	subs	$index,$index,#1
	strb	$mask,[$out,#64*0]
	mov	$mask,$mask,lsr#8
	strb	$mask,[$out,#64*1]
	mov	$mask,$mask,lsr#8
	strb	$mask,[$out,#64*2]
	mov	$mask,$mask,lsr#8
	strb	$mask,[$out,#64*3]
	add	$out,$out,#64*4
	bne	.Loop_scatter_w7

#if __ARM_ARCH__>=5 || defined(__thumb__)
	bx	lr
#else
	mov	pc,lr
#endif
.size	ecp_nistz256_scatter_w7,.-ecp_nistz256_scatter_w7

@ void	ecp_nistz256_gather_w7(P256_POINT_AFFINE *r0,const void *r1,
@						     int r2);
.globl	ecp_nistz256_gather_w7
.type	ecp_nistz256_gather_w7,%function
.align	5
ecp_nistz256_gather_w7:
	stmdb	sp!,{r4-r7}

	cmp	$index,#0
	mov	$mask,#0
#ifdef	__thumb2__
	itt	ne
#endif
	subne	$index,$index,#1
	movne	$mask,#-1
	add	$inp,$inp,$index
	mov	$index,#64/4
	nop
.Loop_gather_w7:
	ldrb	r4,[$inp,#64*0]
	subs	$index,$index,#1
	ldrb	r5,[$inp,#64*1]
	ldrb	r6,[$inp,#64*2]
	ldrb	r7,[$inp,#64*3]
	add	$inp,$inp,#64*4
	orr	r4,r4,r5,lsl#8
	orr	r4,r4,r6,lsl#16
	orr	r4,r4,r7,lsl#24
	and	r4,r4,$mask
	str	r4,[$out],#4
	bne	.Loop_gather_w7

	ldmia	sp!,{r4-r7}
#if __ARM_ARCH__>=5 || defined(__thumb__)
	bx	lr
#else
	mov	pc,lr
#endif
.size	ecp_nistz256_gather_w7,.-ecp_nistz256_gather_w7
___
}
if (0) {
# In comparison to integer-only equivalent of below subroutine:
#
# Cortex-A8	+10%
# Cortex-A9	-10%
# Snapdragon S4	+5%
#
# As not all time is spent in multiplication, overall impact is deemed
# too low to care about.

my ($A0,$A1,$A2,$A3,$Bi,$zero,$temp)=map("d$_",(0..7));
my $mask="q4";
my $mult="q5";
my @AxB=map("q$_",(8..15));

my ($rptr,$aptr,$bptr,$toutptr)=map("r$_",(0..3));

$code.=<<___;
#if __ARM_ARCH__>=7
.fpu	neon

.globl	ecp_nistz256_mul_mont_neon
.type	ecp_nistz256_mul_mont_neon,%function
.align	5
ecp_nistz256_mul_mont_neon:
	mov	ip,sp
	stmdb	sp!,{r4-r9}
	vstmdb	sp!,{q4-q5}		@ ABI specification says so

	sub		$toutptr,sp,#40
	vld1.32		{${Bi}[0]},[$bptr,:32]!
	veor		$zero,$zero,$zero
	vld1.32		{$A0-$A3}, [$aptr]		@ can't specify :32 :-(
	vzip.16		$Bi,$zero
	mov		sp,$toutptr			@ alloca
	vmov.i64	$mask,#0xffff

	vmull.u32	@AxB[0],$Bi,${A0}[0]
	vmull.u32	@AxB[1],$Bi,${A0}[1]
	vmull.u32	@AxB[2],$Bi,${A1}[0]
	vmull.u32	@AxB[3],$Bi,${A1}[1]
	 vshr.u64	$temp,@AxB[0]#lo,#16
	vmull.u32	@AxB[4],$Bi,${A2}[0]
	 vadd.u64	@AxB[0]#hi,@AxB[0]#hi,$temp
	vmull.u32	@AxB[5],$Bi,${A2}[1]
	 vshr.u64	$temp,@AxB[0]#hi,#16		@ upper 32 bits of a[0]*b[0]
	vmull.u32	@AxB[6],$Bi,${A3}[0]
	 vand.u64	@AxB[0],@AxB[0],$mask		@ lower 32 bits of a[0]*b[0]
	vmull.u32	@AxB[7],$Bi,${A3}[1]
___
for($i=1;$i<8;$i++) {
$code.=<<___;
	 vld1.32	{${Bi}[0]},[$bptr,:32]!
	 veor		$zero,$zero,$zero
	vadd.u64	@AxB[1]#lo,@AxB[1]#lo,$temp	@ reduction
	vshl.u64	$mult,@AxB[0],#32
	vadd.u64	@AxB[3],@AxB[3],@AxB[0]
	vsub.u64	$mult,$mult,@AxB[0]
	 vzip.16	$Bi,$zero
	vadd.u64	@AxB[6],@AxB[6],@AxB[0]
	vadd.u64	@AxB[7],@AxB[7],$mult
___
	push(@AxB,shift(@AxB));
$code.=<<___;
	vmlal.u32	@AxB[0],$Bi,${A0}[0]
	vmlal.u32	@AxB[1],$Bi,${A0}[1]
	vmlal.u32	@AxB[2],$Bi,${A1}[0]
	vmlal.u32	@AxB[3],$Bi,${A1}[1]
	 vshr.u64	$temp,@AxB[0]#lo,#16
	vmlal.u32	@AxB[4],$Bi,${A2}[0]
	 vadd.u64	@AxB[0]#hi,@AxB[0]#hi,$temp
	vmlal.u32	@AxB[5],$Bi,${A2}[1]
	 vshr.u64	$temp,@AxB[0]#hi,#16		@ upper 33 bits of a[0]*b[i]+t[0]
	vmlal.u32	@AxB[6],$Bi,${A3}[0]
	 vand.u64	@AxB[0],@AxB[0],$mask		@ lower 32 bits of a[0]*b[0]
	vmull.u32	@AxB[7],$Bi,${A3}[1]
___
}
$code.=<<___;
	vadd.u64	@AxB[1]#lo,@AxB[1]#lo,$temp	@ last reduction
	vshl.u64	$mult,@AxB[0],#32
	vadd.u64	@AxB[3],@AxB[3],@AxB[0]
	vsub.u64	$mult,$mult,@AxB[0]
	vadd.u64	@AxB[6],@AxB[6],@AxB[0]
	vadd.u64	@AxB[7],@AxB[7],$mult

	vshr.u64	$temp,@AxB[1]#lo,#16		@ convert
	vadd.u64	@AxB[1]#hi,@AxB[1]#hi,$temp
	vshr.u64	$temp,@AxB[1]#hi,#16
	vzip.16		@AxB[1]#lo,@AxB[1]#hi
___
foreach (2..7) {
$code.=<<___;
	vadd.u64	@AxB[$_]#lo,@AxB[$_]#lo,$temp
	vst1.32		{@AxB[$_-1]#lo[0]},[$toutptr,:32]!
	vshr.u64	$temp,@AxB[$_]#lo,#16
	vadd.u64	@AxB[$_]#hi,@AxB[$_]#hi,$temp
	vshr.u64	$temp,@AxB[$_]#hi,#16
	vzip.16		@AxB[$_]#lo,@AxB[$_]#hi
___
}
$code.=<<___;
	vst1.32		{@AxB[7]#lo[0]},[$toutptr,:32]!
	vst1.32		{$temp},[$toutptr]		@ upper 33 bits

	ldr	r1,[sp,#0]
	ldr	r2,[sp,#4]
	ldr	r3,[sp,#8]
	subs	r1,r1,#-1
	ldr	r4,[sp,#12]
	sbcs	r2,r2,#-1
	ldr	r5,[sp,#16]
	sbcs	r3,r3,#-1
	ldr	r6,[sp,#20]
	sbcs	r4,r4,#0
	ldr	r7,[sp,#24]
	sbcs	r5,r5,#0
	ldr	r8,[sp,#28]
	sbcs	r6,r6,#0
	ldr	r9,[sp,#32]				@ top-most bit
	sbcs	r7,r7,#1
	sub	sp,ip,#40+16
	sbcs	r8,r8,#-1
	sbc	r9,r9,#0
        vldmia  sp!,{q4-q5}

	adds	r1,r1,r9
	adcs	r2,r2,r9
	str	r1,[$rptr,#0]
	adcs	r3,r3,r9
	str	r2,[$rptr,#4]
	adcs	r4,r4,#0
	str	r3,[$rptr,#8]
	adcs	r5,r5,#0
	str	r4,[$rptr,#12]
	adcs	r6,r6,#0
	str	r5,[$rptr,#16]
	adcs	r7,r7,r9,lsr#31
	str	r6,[$rptr,#20]
	adcs	r8,r8,r9
	str	r7,[$rptr,#24]
	str	r8,[$rptr,#28]

        ldmia   sp!,{r4-r9}
	bx	lr
.size	ecp_nistz256_mul_mont_neon,.-ecp_nistz256_mul_mont_neon
#endif
___
}

{{{
########################################################################
# Below $aN assignment matches order in which 256-bit result appears in
# register bank at return from __ecp_nistz256_mul_mont, so that we can
# skip over reloading it from memory. This means that below functions
# use custom calling sequence accepting 256-bit input in registers,
# output pointer in r0, $r_ptr, and optional pointer in r2, $b_ptr.
#
# See their "normal" counterparts for insights on calculations.

my ($a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7,
    $t0,$t1,$t2,$t3)=map("r$_",(11,3..10,12,14,1));
my $ff=$b_ptr;

$code.=<<___;
.type	__ecp_nistz256_sub_from,%function
.align	5
__ecp_nistz256_sub_from:
	str	lr,[sp,#-4]!		@ push lr

	 ldr	$t0,[$b_ptr,#0]
	 ldr	$t1,[$b_ptr,#4]
	 ldr	$t2,[$b_ptr,#8]
	 ldr	$t3,[$b_ptr,#12]
	subs	$a0,$a0,$t0
	 ldr	$t0,[$b_ptr,#16]
	sbcs	$a1,$a1,$t1
	 ldr	$t1,[$b_ptr,#20]
	sbcs	$a2,$a2,$t2
	 ldr	$t2,[$b_ptr,#24]
	sbcs	$a3,$a3,$t3
	 ldr	$t3,[$b_ptr,#28]
	sbcs	$a4,$a4,$t0
	sbcs	$a5,$a5,$t1
	sbcs	$a6,$a6,$t2
	sbcs	$a7,$a7,$t3
	sbc	$ff,$ff,$ff		@ broadcast borrow bit
	ldr	lr,[sp],#4		@ pop lr

	adds	$a0,$a0,$ff		@ add synthesized modulus
	adcs	$a1,$a1,$ff
	str	$a0,[$r_ptr,#0]
	adcs	$a2,$a2,$ff
	str	$a1,[$r_ptr,#4]
	adcs	$a3,$a3,#0
	str	$a2,[$r_ptr,#8]
	adcs	$a4,$a4,#0
	str	$a3,[$r_ptr,#12]
	adcs	$a5,$a5,#0
	str	$a4,[$r_ptr,#16]
	adcs	$a6,$a6,$ff,lsr#31
	str	$a5,[$r_ptr,#20]
	adcs	$a7,$a7,$ff
	str	$a6,[$r_ptr,#24]
	str	$a7,[$r_ptr,#28]

	mov	pc,lr
.size	__ecp_nistz256_sub_from,.-__ecp_nistz256_sub_from

.type	__ecp_nistz256_sub_morf,%function
.align	5
__ecp_nistz256_sub_morf:
	str	lr,[sp,#-4]!		@ push lr

	 ldr	$t0,[$b_ptr,#0]
	 ldr	$t1,[$b_ptr,#4]
	 ldr	$t2,[$b_ptr,#8]
	 ldr	$t3,[$b_ptr,#12]
	subs	$a0,$t0,$a0
	 ldr	$t0,[$b_ptr,#16]
	sbcs	$a1,$t1,$a1
	 ldr	$t1,[$b_ptr,#20]
	sbcs	$a2,$t2,$a2
	 ldr	$t2,[$b_ptr,#24]
	sbcs	$a3,$t3,$a3
	 ldr	$t3,[$b_ptr,#28]
	sbcs	$a4,$t0,$a4
	sbcs	$a5,$t1,$a5
	sbcs	$a6,$t2,$a6
	sbcs	$a7,$t3,$a7
	sbc	$ff,$ff,$ff		@ broadcast borrow bit
	ldr	lr,[sp],#4		@ pop lr

	adds	$a0,$a0,$ff		@ add synthesized modulus
	adcs	$a1,$a1,$ff
	str	$a0,[$r_ptr,#0]
	adcs	$a2,$a2,$ff
	str	$a1,[$r_ptr,#4]
	adcs	$a3,$a3,#0
	str	$a2,[$r_ptr,#8]
	adcs	$a4,$a4,#0
	str	$a3,[$r_ptr,#12]
	adcs	$a5,$a5,#0
	str	$a4,[$r_ptr,#16]
	adcs	$a6,$a6,$ff,lsr#31
	str	$a5,[$r_ptr,#20]
	adcs	$a7,$a7,$ff
	str	$a6,[$r_ptr,#24]
	str	$a7,[$r_ptr,#28]

	mov	pc,lr
.size	__ecp_nistz256_sub_morf,.-__ecp_nistz256_sub_morf

.type	__ecp_nistz256_add_self,%function
.align	4
__ecp_nistz256_add_self:
	adds	$a0,$a0,$a0		@ a[0:7]+=a[0:7]
	adcs	$a1,$a1,$a1
	adcs	$a2,$a2,$a2
	adcs	$a3,$a3,$a3
	adcs	$a4,$a4,$a4
	adcs	$a5,$a5,$a5
	adcs	$a6,$a6,$a6
	mov	$ff,#0
	adcs	$a7,$a7,$a7
	adc	$ff,$ff,#0

	@ if a+b >= modulus, subtract modulus.
	@
	@ But since comparison implies subtraction, we subtract
	@ modulus and then add it back if subtraction borrowed.

	subs	$a0,$a0,#-1
	sbcs	$a1,$a1,#-1
	sbcs	$a2,$a2,#-1
	sbcs	$a3,$a3,#0
	sbcs	$a4,$a4,#0
	sbcs	$a5,$a5,#0
	sbcs	$a6,$a6,#1
	sbcs	$a7,$a7,#-1
	sbc	$ff,$ff,#0

	@ Note that because mod has special form, i.e. consists of
	@ 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	@ using value of borrow as a whole or extracting single bit.
	@ Follow $ff register...

	adds	$a0,$a0,$ff		@ add synthesized modulus
	adcs	$a1,$a1,$ff
	str	$a0,[$r_ptr,#0]
	adcs	$a2,$a2,$ff
	str	$a1,[$r_ptr,#4]
	adcs	$a3,$a3,#0
	str	$a2,[$r_ptr,#8]
	adcs	$a4,$a4,#0
	str	$a3,[$r_ptr,#12]
	adcs	$a5,$a5,#0
	str	$a4,[$r_ptr,#16]
	adcs	$a6,$a6,$ff,lsr#31
	str	$a5,[$r_ptr,#20]
	adcs	$a7,$a7,$ff
	str	$a6,[$r_ptr,#24]
	str	$a7,[$r_ptr,#28]

	mov	pc,lr
.size	__ecp_nistz256_add_self,.-__ecp_nistz256_add_self

___

########################################################################
# following subroutines are "literal" implementation of those found in
# ecp_nistz256.c
#
########################################################################
# void ecp_nistz256_point_double(P256_POINT *out,const P256_POINT *inp);
#
{
my ($S,$M,$Zsqr,$in_x,$tmp0)=map(32*$_,(0..4));
# above map() describes stack layout with 5 temporary
# 256-bit vectors on top. Then note that we push
# starting from r0, which means that we have copy of
# input arguments just below these temporary vectors.

$code.=<<___;
.globl	ecp_nistz256_point_double
.type	ecp_nistz256_point_double,%function
.align	5
ecp_nistz256_point_double:
	stmdb	sp!,{r0-r12,lr}		@ push from r0, unusual, but intentional
	sub	sp,sp,#32*5

.Lpoint_double_shortcut:
	add	r3,sp,#$in_x
	ldmia	$a_ptr!,{r4-r11}	@ copy in_x
	stmia	r3,{r4-r11}

	add	$r_ptr,sp,#$S
	bl	__ecp_nistz256_mul_by_2	@ p256_mul_by_2(S, in_y);

	add	$b_ptr,$a_ptr,#32
	add	$a_ptr,$a_ptr,#32
	add	$r_ptr,sp,#$Zsqr
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(Zsqr, in_z);

	add	$a_ptr,sp,#$S
	add	$b_ptr,sp,#$S
	add	$r_ptr,sp,#$S
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(S, S);

	ldr	$b_ptr,[sp,#32*5+4]
	add	$a_ptr,$b_ptr,#32
	add	$b_ptr,$b_ptr,#64
	add	$r_ptr,sp,#$tmp0
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(tmp0, in_z, in_y);

	ldr	$r_ptr,[sp,#32*5]
	add	$r_ptr,$r_ptr,#64
	bl	__ecp_nistz256_add_self	@ p256_mul_by_2(res_z, tmp0);

	add	$a_ptr,sp,#$in_x
	add	$b_ptr,sp,#$Zsqr
	add	$r_ptr,sp,#$M
	bl	__ecp_nistz256_add	@ p256_add(M, in_x, Zsqr);

	add	$a_ptr,sp,#$in_x
	add	$b_ptr,sp,#$Zsqr
	add	$r_ptr,sp,#$Zsqr
	bl	__ecp_nistz256_sub	@ p256_sub(Zsqr, in_x, Zsqr);

	add	$a_ptr,sp,#$S
	add	$b_ptr,sp,#$S
	add	$r_ptr,sp,#$tmp0
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(tmp0, S);

	add	$a_ptr,sp,#$Zsqr
	add	$b_ptr,sp,#$M
	add	$r_ptr,sp,#$M
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(M, M, Zsqr);

	ldr	$r_ptr,[sp,#32*5]
	add	$a_ptr,sp,#$tmp0
	add	$r_ptr,$r_ptr,#32
	bl	__ecp_nistz256_div_by_2	@ p256_div_by_2(res_y, tmp0);

	add	$a_ptr,sp,#$M
	add	$r_ptr,sp,#$M
	bl	__ecp_nistz256_mul_by_3	@ p256_mul_by_3(M, M);

	add	$a_ptr,sp,#$in_x
	add	$b_ptr,sp,#$S
	add	$r_ptr,sp,#$S
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(S, S, in_x);

	add	$r_ptr,sp,#$tmp0
	bl	__ecp_nistz256_add_self	@ p256_mul_by_2(tmp0, S);

	ldr	$r_ptr,[sp,#32*5]
	add	$a_ptr,sp,#$M
	add	$b_ptr,sp,#$M
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(res_x, M);

	add	$b_ptr,sp,#$tmp0
	bl	__ecp_nistz256_sub_from	@ p256_sub(res_x, res_x, tmp0);

	add	$b_ptr,sp,#$S
	add	$r_ptr,sp,#$S
	bl	__ecp_nistz256_sub_morf	@ p256_sub(S, S, res_x);

	add	$a_ptr,sp,#$M
	add	$b_ptr,sp,#$S
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(S, S, M);

	ldr	$r_ptr,[sp,#32*5]
	add	$b_ptr,$r_ptr,#32
	add	$r_ptr,$r_ptr,#32
	bl	__ecp_nistz256_sub_from	@ p256_sub(res_y, S, res_y);

	add	sp,sp,#32*5+16		@ +16 means "skip even over saved r0-r3"
#if __ARM_ARCH__>=5 || !defined(__thumb__)
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	ecp_nistz256_point_double,.-ecp_nistz256_point_double
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
# 256-bit vectors on top. Then note that we push
# starting from r0, which means that we have copy of
# input arguments just below these temporary vectors.
# We use three of them for !in1infty, !in2intfy and
# result of check for zero.

$code.=<<___;
.globl	ecp_nistz256_point_add
.type	ecp_nistz256_point_add,%function
.align	5
ecp_nistz256_point_add:
	stmdb	sp!,{r0-r12,lr}		@ push from r0, unusual, but intentional
	sub	sp,sp,#32*18+16

	ldmia	$b_ptr!,{r4-r11}	@ copy in2_x
	add	r3,sp,#$in2_x
	stmia	r3!,{r4-r11}
	ldmia	$b_ptr!,{r4-r11}	@ copy in2_y
	stmia	r3!,{r4-r11}
	ldmia	$b_ptr,{r4-r11}		@ copy in2_z
	orr	r12,r4,r5
	orr	r12,r12,r6
	orr	r12,r12,r7
	orr	r12,r12,r8
	orr	r12,r12,r9
	orr	r12,r12,r10
	orr	r12,r12,r11
	cmp	r12,#0
#ifdef	__thumb2__
	it	ne
#endif
	movne	r12,#-1
	stmia	r3,{r4-r11}
	str	r12,[sp,#32*18+8]	@ !in2infty

	ldmia	$a_ptr!,{r4-r11}	@ copy in1_x
	add	r3,sp,#$in1_x
	stmia	r3!,{r4-r11}
	ldmia	$a_ptr!,{r4-r11}	@ copy in1_y
	stmia	r3!,{r4-r11}
	ldmia	$a_ptr,{r4-r11}		@ copy in1_z
	orr	r12,r4,r5
	orr	r12,r12,r6
	orr	r12,r12,r7
	orr	r12,r12,r8
	orr	r12,r12,r9
	orr	r12,r12,r10
	orr	r12,r12,r11
	cmp	r12,#0
#ifdef	__thumb2__
	it	ne
#endif
	movne	r12,#-1
	stmia	r3,{r4-r11}
	str	r12,[sp,#32*18+4]	@ !in1infty

	add	$a_ptr,sp,#$in2_z
	add	$b_ptr,sp,#$in2_z
	add	$r_ptr,sp,#$Z2sqr
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(Z2sqr, in2_z);

	add	$a_ptr,sp,#$in1_z
	add	$b_ptr,sp,#$in1_z
	add	$r_ptr,sp,#$Z1sqr
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(Z1sqr, in1_z);

	add	$a_ptr,sp,#$in2_z
	add	$b_ptr,sp,#$Z2sqr
	add	$r_ptr,sp,#$S1
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(S1, Z2sqr, in2_z);

	add	$a_ptr,sp,#$in1_z
	add	$b_ptr,sp,#$Z1sqr
	add	$r_ptr,sp,#$S2
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(S2, Z1sqr, in1_z);

	add	$a_ptr,sp,#$in1_y
	add	$b_ptr,sp,#$S1
	add	$r_ptr,sp,#$S1
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(S1, S1, in1_y);

	add	$a_ptr,sp,#$in2_y
	add	$b_ptr,sp,#$S2
	add	$r_ptr,sp,#$S2
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(S2, S2, in2_y);

	add	$b_ptr,sp,#$S1
	add	$r_ptr,sp,#$R
	bl	__ecp_nistz256_sub_from	@ p256_sub(R, S2, S1);

	orr	$a0,$a0,$a1		@ see if result is zero
	orr	$a2,$a2,$a3
	orr	$a4,$a4,$a5
	orr	$a0,$a0,$a2
	orr	$a4,$a4,$a6
	orr	$a0,$a0,$a7
	 add	$a_ptr,sp,#$in1_x
	orr	$a0,$a0,$a4
	 add	$b_ptr,sp,#$Z2sqr
	str	$a0,[sp,#32*18+12]

	add	$r_ptr,sp,#$U1
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(U1, in1_x, Z2sqr);

	add	$a_ptr,sp,#$in2_x
	add	$b_ptr,sp,#$Z1sqr
	add	$r_ptr,sp,#$U2
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(U2, in2_x, Z1sqr);

	add	$b_ptr,sp,#$U1
	add	$r_ptr,sp,#$H
	bl	__ecp_nistz256_sub_from	@ p256_sub(H, U2, U1);

	orr	$a0,$a0,$a1		@ see if result is zero
	orr	$a2,$a2,$a3
	orr	$a4,$a4,$a5
	orr	$a0,$a0,$a2
	orr	$a4,$a4,$a6
	orr	$a0,$a0,$a7
	orrs	$a0,$a0,$a4

	bne	.Ladd_proceed		@ is_equal(U1,U2)?

	ldr	$t0,[sp,#32*18+4]
	ldr	$t1,[sp,#32*18+8]
	ldr	$t2,[sp,#32*18+12]
	tst	$t0,$t1
	beq	.Ladd_proceed		@ (in1infty || in2infty)?
	tst	$t2,$t2
	beq	.Ladd_double		@ is_equal(S1,S2)?

	ldr	$r_ptr,[sp,#32*18+16]
	eor	r4,r4,r4
	eor	r5,r5,r5
	eor	r6,r6,r6
	eor	r7,r7,r7
	eor	r8,r8,r8
	eor	r9,r9,r9
	eor	r10,r10,r10
	eor	r11,r11,r11
	stmia	$r_ptr!,{r4-r11}
	stmia	$r_ptr!,{r4-r11}
	stmia	$r_ptr!,{r4-r11}
	b	.Ladd_done

.align	4
.Ladd_double:
	ldr	$a_ptr,[sp,#32*18+20]
	add	sp,sp,#32*(18-5)+16	@ difference in frame sizes
	b	.Lpoint_double_shortcut

.align	4
.Ladd_proceed:
	add	$a_ptr,sp,#$R
	add	$b_ptr,sp,#$R
	add	$r_ptr,sp,#$Rsqr
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(Rsqr, R);

	add	$a_ptr,sp,#$H
	add	$b_ptr,sp,#$in1_z
	add	$r_ptr,sp,#$res_z
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(res_z, H, in1_z);

	add	$a_ptr,sp,#$H
	add	$b_ptr,sp,#$H
	add	$r_ptr,sp,#$Hsqr
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(Hsqr, H);

	add	$a_ptr,sp,#$in2_z
	add	$b_ptr,sp,#$res_z
	add	$r_ptr,sp,#$res_z
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(res_z, res_z, in2_z);

	add	$a_ptr,sp,#$H
	add	$b_ptr,sp,#$Hsqr
	add	$r_ptr,sp,#$Hcub
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(Hcub, Hsqr, H);

	add	$a_ptr,sp,#$Hsqr
	add	$b_ptr,sp,#$U1
	add	$r_ptr,sp,#$U2
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(U2, U1, Hsqr);

	add	$r_ptr,sp,#$Hsqr
	bl	__ecp_nistz256_add_self	@ p256_mul_by_2(Hsqr, U2);

	add	$b_ptr,sp,#$Rsqr
	add	$r_ptr,sp,#$res_x
	bl	__ecp_nistz256_sub_morf	@ p256_sub(res_x, Rsqr, Hsqr);

	add	$b_ptr,sp,#$Hcub
	bl	__ecp_nistz256_sub_from	@  p256_sub(res_x, res_x, Hcub);

	add	$b_ptr,sp,#$U2
	add	$r_ptr,sp,#$res_y
	bl	__ecp_nistz256_sub_morf	@ p256_sub(res_y, U2, res_x);

	add	$a_ptr,sp,#$Hcub
	add	$b_ptr,sp,#$S1
	add	$r_ptr,sp,#$S2
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(S2, S1, Hcub);

	add	$a_ptr,sp,#$R
	add	$b_ptr,sp,#$res_y
	add	$r_ptr,sp,#$res_y
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(res_y, res_y, R);

	add	$b_ptr,sp,#$S2
	bl	__ecp_nistz256_sub_from	@ p256_sub(res_y, res_y, S2);

	ldr	r11,[sp,#32*18+4]	@ !in1intfy
	ldr	r12,[sp,#32*18+8]	@ !in2intfy
	add	r1,sp,#$res_x
	add	r2,sp,#$in2_x
	and	r10,r11,r12
	mvn	r11,r11
	add	r3,sp,#$in1_x
	and	r11,r11,r12
	mvn	r12,r12
	ldr	$r_ptr,[sp,#32*18+16]
___
for($i=0;$i<96;$i+=8) {			# conditional moves
$code.=<<___;
	ldmia	r1!,{r4-r5}		@ res_x
	ldmia	r2!,{r6-r7}		@ in2_x
	ldmia	r3!,{r8-r9}		@ in1_x
	and	r4,r4,r10
	and	r5,r5,r10
	and	r6,r6,r11
	and	r7,r7,r11
	and	r8,r8,r12
	and	r9,r9,r12
	orr	r4,r4,r6
	orr	r5,r5,r7
	orr	r4,r4,r8
	orr	r5,r5,r9
	stmia	$r_ptr!,{r4-r5}
___
}
$code.=<<___;
.Ladd_done:
	add	sp,sp,#32*18+16+16	@ +16 means "skip even over saved r0-r3"
#if __ARM_ARCH__>=5 || !defined(__thumb__)
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	ecp_nistz256_point_add,.-ecp_nistz256_point_add
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
# above map() describes stack layout with 18 temporary
# 256-bit vectors on top. Then note that we push
# starting from r0, which means that we have copy of
# input arguments just below these temporary vectors.
# We use two of them for !in1infty, !in2intfy.

my @ONE_mont=(1,0,0,-1,-1,-1,-2,0);

$code.=<<___;
.globl	ecp_nistz256_point_add_affine
.type	ecp_nistz256_point_add_affine,%function
.align	5
ecp_nistz256_point_add_affine:
	stmdb	sp!,{r0-r12,lr}		@ push from r0, unusual, but intentional
	sub	sp,sp,#32*15

	ldmia	$a_ptr!,{r4-r11}	@ copy in1_x
	add	r3,sp,#$in1_x
	stmia	r3!,{r4-r11}
	ldmia	$a_ptr!,{r4-r11}	@ copy in1_y
	stmia	r3!,{r4-r11}
	ldmia	$a_ptr,{r4-r11}		@ copy in1_z
	orr	r12,r4,r5
	orr	r12,r12,r6
	orr	r12,r12,r7
	orr	r12,r12,r8
	orr	r12,r12,r9
	orr	r12,r12,r10
	orr	r12,r12,r11
	cmp	r12,#0
#ifdef	__thumb2__
	it	ne
#endif
	movne	r12,#-1
	stmia	r3,{r4-r11}
	str	r12,[sp,#32*15+4]	@ !in1infty

	ldmia	$b_ptr!,{r4-r11}	@ copy in2_x
	add	r3,sp,#$in2_x
	orr	r12,r4,r5
	orr	r12,r12,r6
	orr	r12,r12,r7
	orr	r12,r12,r8
	orr	r12,r12,r9
	orr	r12,r12,r10
	orr	r12,r12,r11
	stmia	r3!,{r4-r11}
	ldmia	$b_ptr!,{r4-r11}	@ copy in2_y
	orr	r12,r12,r4
	orr	r12,r12,r5
	orr	r12,r12,r6
	orr	r12,r12,r7
	orr	r12,r12,r8
	orr	r12,r12,r9
	orr	r12,r12,r10
	orr	r12,r12,r11
	stmia	r3!,{r4-r11}
	cmp	r12,#0
#ifdef	__thumb2__
	it	ne
#endif
	movne	r12,#-1
	str	r12,[sp,#32*15+8]	@ !in2infty

	add	$a_ptr,sp,#$in1_z
	add	$b_ptr,sp,#$in1_z
	add	$r_ptr,sp,#$Z1sqr
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(Z1sqr, in1_z);

	add	$a_ptr,sp,#$Z1sqr
	add	$b_ptr,sp,#$in2_x
	add	$r_ptr,sp,#$U2
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(U2, Z1sqr, in2_x);

	add	$b_ptr,sp,#$in1_x
	add	$r_ptr,sp,#$H
	bl	__ecp_nistz256_sub_from	@ p256_sub(H, U2, in1_x);

	add	$a_ptr,sp,#$Z1sqr
	add	$b_ptr,sp,#$in1_z
	add	$r_ptr,sp,#$S2
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(S2, Z1sqr, in1_z);

	add	$a_ptr,sp,#$H
	add	$b_ptr,sp,#$in1_z
	add	$r_ptr,sp,#$res_z
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(res_z, H, in1_z);

	add	$a_ptr,sp,#$in2_y
	add	$b_ptr,sp,#$S2
	add	$r_ptr,sp,#$S2
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(S2, S2, in2_y);

	add	$b_ptr,sp,#$in1_y
	add	$r_ptr,sp,#$R
	bl	__ecp_nistz256_sub_from	@ p256_sub(R, S2, in1_y);

	add	$a_ptr,sp,#$H
	add	$b_ptr,sp,#$H
	add	$r_ptr,sp,#$Hsqr
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(Hsqr, H);

	add	$a_ptr,sp,#$R
	add	$b_ptr,sp,#$R
	add	$r_ptr,sp,#$Rsqr
	bl	__ecp_nistz256_mul_mont	@ p256_sqr_mont(Rsqr, R);

	add	$a_ptr,sp,#$H
	add	$b_ptr,sp,#$Hsqr
	add	$r_ptr,sp,#$Hcub
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(Hcub, Hsqr, H);

	add	$a_ptr,sp,#$Hsqr
	add	$b_ptr,sp,#$in1_x
	add	$r_ptr,sp,#$U2
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(U2, in1_x, Hsqr);

	add	$r_ptr,sp,#$Hsqr
	bl	__ecp_nistz256_add_self	@ p256_mul_by_2(Hsqr, U2);

	add	$b_ptr,sp,#$Rsqr
	add	$r_ptr,sp,#$res_x
	bl	__ecp_nistz256_sub_morf	@ p256_sub(res_x, Rsqr, Hsqr);

	add	$b_ptr,sp,#$Hcub
	bl	__ecp_nistz256_sub_from	@  p256_sub(res_x, res_x, Hcub);

	add	$b_ptr,sp,#$U2
	add	$r_ptr,sp,#$res_y
	bl	__ecp_nistz256_sub_morf	@ p256_sub(res_y, U2, res_x);

	add	$a_ptr,sp,#$Hcub
	add	$b_ptr,sp,#$in1_y
	add	$r_ptr,sp,#$S2
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(S2, in1_y, Hcub);

	add	$a_ptr,sp,#$R
	add	$b_ptr,sp,#$res_y
	add	$r_ptr,sp,#$res_y
	bl	__ecp_nistz256_mul_mont	@ p256_mul_mont(res_y, res_y, R);

	add	$b_ptr,sp,#$S2
	bl	__ecp_nistz256_sub_from	@ p256_sub(res_y, res_y, S2);

	ldr	r11,[sp,#32*15+4]	@ !in1intfy
	ldr	r12,[sp,#32*15+8]	@ !in2intfy
	add	r1,sp,#$res_x
	add	r2,sp,#$in2_x
	and	r10,r11,r12
	mvn	r11,r11
	add	r3,sp,#$in1_x
	and	r11,r11,r12
	mvn	r12,r12
	ldr	$r_ptr,[sp,#32*15]
___
for($i=0;$i<64;$i+=8) {			# conditional moves
$code.=<<___;
	ldmia	r1!,{r4-r5}		@ res_x
	ldmia	r2!,{r6-r7}		@ in2_x
	ldmia	r3!,{r8-r9}		@ in1_x
	and	r4,r4,r10
	and	r5,r5,r10
	and	r6,r6,r11
	and	r7,r7,r11
	and	r8,r8,r12
	and	r9,r9,r12
	orr	r4,r4,r6
	orr	r5,r5,r7
	orr	r4,r4,r8
	orr	r5,r5,r9
	stmia	$r_ptr!,{r4-r5}
___
}
for(;$i<96;$i+=8) {
my $j=($i-64)/4;
$code.=<<___;
	ldmia	r1!,{r4-r5}		@ res_z
	ldmia	r3!,{r8-r9}		@ in1_z
	and	r4,r4,r10
	and	r5,r5,r10
	and	r6,r11,#@ONE_mont[$j]
	and	r7,r11,#@ONE_mont[$j+1]
	and	r8,r8,r12
	and	r9,r9,r12
	orr	r4,r4,r6
	orr	r5,r5,r7
	orr	r4,r4,r8
	orr	r5,r5,r9
	stmia	$r_ptr!,{r4-r5}
___
}
$code.=<<___;
	add	sp,sp,#32*15+16		@ +16 means "skip even over saved r0-r3"
#if __ARM_ARCH__>=5 || !defined(__thumb__)
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	ecp_nistz256_point_add_affine,.-ecp_nistz256_point_add_affine
___
}					}}}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/geo;

	s/\bq([0-9]+)#(lo|hi)/sprintf "d%d",2*$1+($2 eq "hi")/geo;

	print $_,"\n";
}
close STDOUT;	# enforce flush

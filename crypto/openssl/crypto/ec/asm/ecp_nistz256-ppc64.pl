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
# ECP_NISTZ256 module for PPC64.
#
# August 2016.
#
# Original ECP_NISTZ256 submission targeting x86_64 is detailed in
# http://eprint.iacr.org/2013/816.
#
#			with/without -DECP_NISTZ256_ASM
# POWER7		+260-530%
# POWER8		+220-340%

$flavour = shift;
while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

my $sp="r1";

{
my ($rp,$ap,$bp,$bi,$acc0,$acc1,$acc2,$acc3,$poly1,$poly3,
    $acc4,$acc5,$a0,$a1,$a2,$a3,$t0,$t1,$t2,$t3) =
    map("r$_",(3..12,22..31));

my ($acc6,$acc7)=($bp,$bi);	# used in __ecp_nistz256_sqr_mont

$code.=<<___;
.machine	"any"
.text
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
.type	ecp_nistz256_precomputed,\@object
.globl	ecp_nistz256_precomputed
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
.asciz	"ECP_NISTZ256 for PPC64, CRYPTOGAMS by <appro\@openssl.org>"

# void	ecp_nistz256_mul_mont(BN_ULONG x0[4],const BN_ULONG x1[4],
#					     const BN_ULONG x2[4]);
.globl	ecp_nistz256_mul_mont
.align	5
ecp_nistz256_mul_mont:
	stdu	$sp,-128($sp)
	mflr	r0
	std	r22,48($sp)
	std	r23,56($sp)
	std	r24,64($sp)
	std	r25,72($sp)
	std	r26,80($sp)
	std	r27,88($sp)
	std	r28,96($sp)
	std	r29,104($sp)
	std	r30,112($sp)
	std	r31,120($sp)

	ld	$a0,0($ap)
	ld	$bi,0($bp)
	ld	$a1,8($ap)
	ld	$a2,16($ap)
	ld	$a3,24($ap)

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001

	bl	__ecp_nistz256_mul_mont

	mtlr	r0
	ld	r22,48($sp)
	ld	r23,56($sp)
	ld	r24,64($sp)
	ld	r25,72($sp)
	ld	r26,80($sp)
	ld	r27,88($sp)
	ld	r28,96($sp)
	ld	r29,104($sp)
	ld	r30,112($sp)
	ld	r31,120($sp)
	addi	$sp,$sp,128
	blr
	.long	0
	.byte	0,12,4,0,0x80,10,3,0
	.long	0
.size	ecp_nistz256_mul_mont,.-ecp_nistz256_mul_mont

# void	ecp_nistz256_sqr_mont(BN_ULONG x0[4],const BN_ULONG x1[4]);
.globl	ecp_nistz256_sqr_mont
.align	4
ecp_nistz256_sqr_mont:
	stdu	$sp,-128($sp)
	mflr	r0
	std	r22,48($sp)
	std	r23,56($sp)
	std	r24,64($sp)
	std	r25,72($sp)
	std	r26,80($sp)
	std	r27,88($sp)
	std	r28,96($sp)
	std	r29,104($sp)
	std	r30,112($sp)
	std	r31,120($sp)

	ld	$a0,0($ap)
	ld	$a1,8($ap)
	ld	$a2,16($ap)
	ld	$a3,24($ap)

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001

	bl	__ecp_nistz256_sqr_mont

	mtlr	r0
	ld	r22,48($sp)
	ld	r23,56($sp)
	ld	r24,64($sp)
	ld	r25,72($sp)
	ld	r26,80($sp)
	ld	r27,88($sp)
	ld	r28,96($sp)
	ld	r29,104($sp)
	ld	r30,112($sp)
	ld	r31,120($sp)
	addi	$sp,$sp,128
	blr
	.long	0
	.byte	0,12,4,0,0x80,10,2,0
	.long	0
.size	ecp_nistz256_sqr_mont,.-ecp_nistz256_sqr_mont

# void	ecp_nistz256_add(BN_ULONG x0[4],const BN_ULONG x1[4],
#					const BN_ULONG x2[4]);
.globl	ecp_nistz256_add
.align	4
ecp_nistz256_add:
	stdu	$sp,-128($sp)
	mflr	r0
	std	r28,96($sp)
	std	r29,104($sp)
	std	r30,112($sp)
	std	r31,120($sp)

	ld	$acc0,0($ap)
	ld	$t0,  0($bp)
	ld	$acc1,8($ap)
	ld	$t1,  8($bp)
	ld	$acc2,16($ap)
	ld	$t2,  16($bp)
	ld	$acc3,24($ap)
	ld	$t3,  24($bp)

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001

	bl	__ecp_nistz256_add

	mtlr	r0
	ld	r28,96($sp)
	ld	r29,104($sp)
	ld	r30,112($sp)
	ld	r31,120($sp)
	addi	$sp,$sp,128
	blr
	.long	0
	.byte	0,12,4,0,0x80,4,3,0
	.long	0
.size	ecp_nistz256_add,.-ecp_nistz256_add

# void	ecp_nistz256_div_by_2(BN_ULONG x0[4],const BN_ULONG x1[4]);
.globl	ecp_nistz256_div_by_2
.align	4
ecp_nistz256_div_by_2:
	stdu	$sp,-128($sp)
	mflr	r0
	std	r28,96($sp)
	std	r29,104($sp)
	std	r30,112($sp)
	std	r31,120($sp)

	ld	$acc0,0($ap)
	ld	$acc1,8($ap)
	ld	$acc2,16($ap)
	ld	$acc3,24($ap)

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001

	bl	__ecp_nistz256_div_by_2

	mtlr	r0
	ld	r28,96($sp)
	ld	r29,104($sp)
	ld	r30,112($sp)
	ld	r31,120($sp)
	addi	$sp,$sp,128
	blr
	.long	0
	.byte	0,12,4,0,0x80,4,2,0
	.long	0
.size	ecp_nistz256_div_by_2,.-ecp_nistz256_div_by_2

# void	ecp_nistz256_mul_by_2(BN_ULONG x0[4],const BN_ULONG x1[4]);
.globl	ecp_nistz256_mul_by_2
.align	4
ecp_nistz256_mul_by_2:
	stdu	$sp,-128($sp)
	mflr	r0
	std	r28,96($sp)
	std	r29,104($sp)
	std	r30,112($sp)
	std	r31,120($sp)

	ld	$acc0,0($ap)
	ld	$acc1,8($ap)
	ld	$acc2,16($ap)
	ld	$acc3,24($ap)

	mr	$t0,$acc0
	mr	$t1,$acc1
	mr	$t2,$acc2
	mr	$t3,$acc3

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001

	bl	__ecp_nistz256_add	# ret = a+a	// 2*a

	mtlr	r0
	ld	r28,96($sp)
	ld	r29,104($sp)
	ld	r30,112($sp)
	ld	r31,120($sp)
	addi	$sp,$sp,128
	blr
	.long	0
	.byte	0,12,4,0,0x80,4,3,0
	.long	0
.size	ecp_nistz256_mul_by_2,.-ecp_nistz256_mul_by_2

# void	ecp_nistz256_mul_by_3(BN_ULONG x0[4],const BN_ULONG x1[4]);
.globl	ecp_nistz256_mul_by_3
.align	4
ecp_nistz256_mul_by_3:
	stdu	$sp,-128($sp)
	mflr	r0
	std	r28,96($sp)
	std	r29,104($sp)
	std	r30,112($sp)
	std	r31,120($sp)

	ld	$acc0,0($ap)
	ld	$acc1,8($ap)
	ld	$acc2,16($ap)
	ld	$acc3,24($ap)

	mr	$t0,$acc0
	std	$acc0,64($sp)
	mr	$t1,$acc1
	std	$acc1,72($sp)
	mr	$t2,$acc2
	std	$acc2,80($sp)
	mr	$t3,$acc3
	std	$acc3,88($sp)

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001

	bl	__ecp_nistz256_add	# ret = a+a	// 2*a

	ld	$t0,64($sp)
	ld	$t1,72($sp)
	ld	$t2,80($sp)
	ld	$t3,88($sp)

	bl	__ecp_nistz256_add	# ret += a	// 2*a+a=3*a

	mtlr	r0
	ld	r28,96($sp)
	ld	r29,104($sp)
	ld	r30,112($sp)
	ld	r31,120($sp)
	addi	$sp,$sp,128
	blr
	.long	0
	.byte	0,12,4,0,0x80,4,2,0
	.long	0
.size	ecp_nistz256_mul_by_3,.-ecp_nistz256_mul_by_3

# void	ecp_nistz256_sub(BN_ULONG x0[4],const BN_ULONG x1[4],
#				        const BN_ULONG x2[4]);
.globl	ecp_nistz256_sub
.align	4
ecp_nistz256_sub:
	stdu	$sp,-128($sp)
	mflr	r0
	std	r28,96($sp)
	std	r29,104($sp)
	std	r30,112($sp)
	std	r31,120($sp)

	ld	$acc0,0($ap)
	ld	$acc1,8($ap)
	ld	$acc2,16($ap)
	ld	$acc3,24($ap)

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001

	bl	__ecp_nistz256_sub_from

	mtlr	r0
	ld	r28,96($sp)
	ld	r29,104($sp)
	ld	r30,112($sp)
	ld	r31,120($sp)
	addi	$sp,$sp,128
	blr
	.long	0
	.byte	0,12,4,0,0x80,4,3,0
	.long	0
.size	ecp_nistz256_sub,.-ecp_nistz256_sub

# void	ecp_nistz256_neg(BN_ULONG x0[4],const BN_ULONG x1[4]);
.globl	ecp_nistz256_neg
.align	4
ecp_nistz256_neg:
	stdu	$sp,-128($sp)
	mflr	r0
	std	r28,96($sp)
	std	r29,104($sp)
	std	r30,112($sp)
	std	r31,120($sp)

	mr	$bp,$ap
	li	$acc0,0
	li	$acc1,0
	li	$acc2,0
	li	$acc3,0

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001

	bl	__ecp_nistz256_sub_from

	mtlr	r0
	ld	r28,96($sp)
	ld	r29,104($sp)
	ld	r30,112($sp)
	ld	r31,120($sp)
	addi	$sp,$sp,128
	blr
	.long	0
	.byte	0,12,4,0,0x80,4,2,0
	.long	0
.size	ecp_nistz256_neg,.-ecp_nistz256_neg

# note that __ecp_nistz256_mul_mont expects a[0-3] input pre-loaded
# to $a0-$a3 and b[0] - to $bi
.type	__ecp_nistz256_mul_mont,\@function
.align	4
__ecp_nistz256_mul_mont:
	mulld	$acc0,$a0,$bi		# a[0]*b[0]
	mulhdu	$t0,$a0,$bi

	mulld	$acc1,$a1,$bi		# a[1]*b[0]
	mulhdu	$t1,$a1,$bi

	mulld	$acc2,$a2,$bi		# a[2]*b[0]
	mulhdu	$t2,$a2,$bi

	mulld	$acc3,$a3,$bi		# a[3]*b[0]
	mulhdu	$t3,$a3,$bi
	ld	$bi,8($bp)		# b[1]

	addc	$acc1,$acc1,$t0		# accumulate high parts of multiplication
	 sldi	$t0,$acc0,32
	adde	$acc2,$acc2,$t1
	 srdi	$t1,$acc0,32
	adde	$acc3,$acc3,$t2
	addze	$acc4,$t3
	li	$acc5,0
___
for($i=1;$i<4;$i++) {
	################################################################
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

$code.=<<___;
	subfc	$t2,$t0,$acc0		# "*0xffff0001"
	subfe	$t3,$t1,$acc0
	addc	$acc0,$acc1,$t0		# +=acc[0]<<96 and omit acc[0]
	adde	$acc1,$acc2,$t1
	adde	$acc2,$acc3,$t2		# +=acc[0]*0xffff0001
	adde	$acc3,$acc4,$t3
	addze	$acc4,$acc5

	mulld	$t0,$a0,$bi		# lo(a[0]*b[i])
	mulld	$t1,$a1,$bi		# lo(a[1]*b[i])
	mulld	$t2,$a2,$bi		# lo(a[2]*b[i])
	mulld	$t3,$a3,$bi		# lo(a[3]*b[i])
	addc	$acc0,$acc0,$t0		# accumulate low parts of multiplication
	 mulhdu	$t0,$a0,$bi		# hi(a[0]*b[i])
	adde	$acc1,$acc1,$t1
	 mulhdu	$t1,$a1,$bi		# hi(a[1]*b[i])
	adde	$acc2,$acc2,$t2
	 mulhdu	$t2,$a2,$bi		# hi(a[2]*b[i])
	adde	$acc3,$acc3,$t3
	 mulhdu	$t3,$a3,$bi		# hi(a[3]*b[i])
	addze	$acc4,$acc4
___
$code.=<<___	if ($i<3);
	ld	$bi,8*($i+1)($bp)	# b[$i+1]
___
$code.=<<___;
	addc	$acc1,$acc1,$t0		# accumulate high parts of multiplication
	 sldi	$t0,$acc0,32
	adde	$acc2,$acc2,$t1
	 srdi	$t1,$acc0,32
	adde	$acc3,$acc3,$t2
	adde	$acc4,$acc4,$t3
	li	$acc5,0
	addze	$acc5,$acc5
___
}
$code.=<<___;
	# last reduction
	subfc	$t2,$t0,$acc0		# "*0xffff0001"
	subfe	$t3,$t1,$acc0
	addc	$acc0,$acc1,$t0		# +=acc[0]<<96 and omit acc[0]
	adde	$acc1,$acc2,$t1
	adde	$acc2,$acc3,$t2		# +=acc[0]*0xffff0001
	adde	$acc3,$acc4,$t3
	addze	$acc4,$acc5

	li	$t2,0
	addic	$acc0,$acc0,1		# ret -= modulus
	subfe	$acc1,$poly1,$acc1
	subfe	$acc2,$t2,$acc2
	subfe	$acc3,$poly3,$acc3
	subfe	$acc4,$t2,$acc4

	addc	$acc0,$acc0,$acc4	# ret += modulus if borrow
	and	$t1,$poly1,$acc4
	and	$t3,$poly3,$acc4
	adde	$acc1,$acc1,$t1
	addze	$acc2,$acc2
	adde	$acc3,$acc3,$t3

	std	$acc0,0($rp)
	std	$acc1,8($rp)
	std	$acc2,16($rp)
	std	$acc3,24($rp)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,1,0
	.long	0
.size	__ecp_nistz256_mul_mont,.-__ecp_nistz256_mul_mont

# note that __ecp_nistz256_sqr_mont expects a[0-3] input pre-loaded
# to $a0-$a3
.type	__ecp_nistz256_sqr_mont,\@function
.align	4
__ecp_nistz256_sqr_mont:
	################################################################
	#  |  |  |  |  |  |a1*a0|  |
	#  |  |  |  |  |a2*a0|  |  |
	#  |  |a3*a2|a3*a0|  |  |  |
	#  |  |  |  |a2*a1|  |  |  |
	#  |  |  |a3*a1|  |  |  |  |
	# *|  |  |  |  |  |  |  | 2|
	# +|a3*a3|a2*a2|a1*a1|a0*a0|
	#  |--+--+--+--+--+--+--+--|
	#  |A7|A6|A5|A4|A3|A2|A1|A0|, where Ax is $accx, i.e. follow $accx
	#
	#  "can't overflow" below mark carrying into high part of
	#  multiplication result, which can't overflow, because it
	#  can never be all ones.

	mulld	$acc1,$a1,$a0		# a[1]*a[0]
	mulhdu	$t1,$a1,$a0
	mulld	$acc2,$a2,$a0		# a[2]*a[0]
	mulhdu	$t2,$a2,$a0
	mulld	$acc3,$a3,$a0		# a[3]*a[0]
	mulhdu	$acc4,$a3,$a0

	addc	$acc2,$acc2,$t1		# accumulate high parts of multiplication
	 mulld	$t0,$a2,$a1		# a[2]*a[1]
	 mulhdu	$t1,$a2,$a1
	adde	$acc3,$acc3,$t2
	 mulld	$t2,$a3,$a1		# a[3]*a[1]
	 mulhdu	$t3,$a3,$a1
	addze	$acc4,$acc4		# can't overflow

	mulld	$acc5,$a3,$a2		# a[3]*a[2]
	mulhdu	$acc6,$a3,$a2

	addc	$t1,$t1,$t2		# accumulate high parts of multiplication
	addze	$t2,$t3			# can't overflow

	addc	$acc3,$acc3,$t0		# accumulate low parts of multiplication
	adde	$acc4,$acc4,$t1
	adde	$acc5,$acc5,$t2
	addze	$acc6,$acc6		# can't overflow

	addc	$acc1,$acc1,$acc1	# acc[1-6]*=2
	adde	$acc2,$acc2,$acc2
	adde	$acc3,$acc3,$acc3
	adde	$acc4,$acc4,$acc4
	adde	$acc5,$acc5,$acc5
	adde	$acc6,$acc6,$acc6
	li	$acc7,0
	addze	$acc7,$acc7

	mulld	$acc0,$a0,$a0		# a[0]*a[0]
	mulhdu	$a0,$a0,$a0
	mulld	$t1,$a1,$a1		# a[1]*a[1]
	mulhdu	$a1,$a1,$a1
	mulld	$t2,$a2,$a2		# a[2]*a[2]
	mulhdu	$a2,$a2,$a2
	mulld	$t3,$a3,$a3		# a[3]*a[3]
	mulhdu	$a3,$a3,$a3
	addc	$acc1,$acc1,$a0		# +a[i]*a[i]
	 sldi	$t0,$acc0,32
	adde	$acc2,$acc2,$t1
	 srdi	$t1,$acc0,32
	adde	$acc3,$acc3,$a1
	adde	$acc4,$acc4,$t2
	adde	$acc5,$acc5,$a2
	adde	$acc6,$acc6,$t3
	adde	$acc7,$acc7,$a3
___
for($i=0;$i<3;$i++) {			# reductions, see commentary in
					# multiplication for details
$code.=<<___;
	subfc	$t2,$t0,$acc0		# "*0xffff0001"
	subfe	$t3,$t1,$acc0
	addc	$acc0,$acc1,$t0		# +=acc[0]<<96 and omit acc[0]
	 sldi	$t0,$acc0,32
	adde	$acc1,$acc2,$t1
	 srdi	$t1,$acc0,32
	adde	$acc2,$acc3,$t2		# +=acc[0]*0xffff0001
	addze	$acc3,$t3		# can't overflow
___
}
$code.=<<___;
	subfc	$t2,$t0,$acc0		# "*0xffff0001"
	subfe	$t3,$t1,$acc0
	addc	$acc0,$acc1,$t0		# +=acc[0]<<96 and omit acc[0]
	adde	$acc1,$acc2,$t1
	adde	$acc2,$acc3,$t2		# +=acc[0]*0xffff0001
	addze	$acc3,$t3		# can't overflow

	addc	$acc0,$acc0,$acc4	# accumulate upper half
	adde	$acc1,$acc1,$acc5
	adde	$acc2,$acc2,$acc6
	adde	$acc3,$acc3,$acc7
	li	$t2,0
	addze	$acc4,$t2

	addic	$acc0,$acc0,1		# ret -= modulus
	subfe	$acc1,$poly1,$acc1
	subfe	$acc2,$t2,$acc2
	subfe	$acc3,$poly3,$acc3
	subfe	$acc4,$t2,$acc4

	addc	$acc0,$acc0,$acc4	# ret += modulus if borrow
	and	$t1,$poly1,$acc4
	and	$t3,$poly3,$acc4
	adde	$acc1,$acc1,$t1
	addze	$acc2,$acc2
	adde	$acc3,$acc3,$t3

	std	$acc0,0($rp)
	std	$acc1,8($rp)
	std	$acc2,16($rp)
	std	$acc3,24($rp)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,1,0
	.long	0
.size	__ecp_nistz256_sqr_mont,.-__ecp_nistz256_sqr_mont

# Note that __ecp_nistz256_add expects both input vectors pre-loaded to
# $a0-$a3 and $t0-$t3. This is done because it's used in multiple
# contexts, e.g. in multiplication by 2 and 3...
.type	__ecp_nistz256_add,\@function
.align	4
__ecp_nistz256_add:
	addc	$acc0,$acc0,$t0		# ret = a+b
	adde	$acc1,$acc1,$t1
	adde	$acc2,$acc2,$t2
	li	$t2,0
	adde	$acc3,$acc3,$t3
	addze	$t0,$t2

	# if a+b >= modulus, subtract modulus
	#
	# But since comparison implies subtraction, we subtract
	# modulus and then add it back if subtraction borrowed.

	subic	$acc0,$acc0,-1
	subfe	$acc1,$poly1,$acc1
	subfe	$acc2,$t2,$acc2
	subfe	$acc3,$poly3,$acc3
	subfe	$t0,$t2,$t0

	addc	$acc0,$acc0,$t0
	and	$t1,$poly1,$t0
	and	$t3,$poly3,$t0
	adde	$acc1,$acc1,$t1
	addze	$acc2,$acc2
	adde	$acc3,$acc3,$t3

	std	$acc0,0($rp)
	std	$acc1,8($rp)
	std	$acc2,16($rp)
	std	$acc3,24($rp)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	__ecp_nistz256_add,.-__ecp_nistz256_add

.type	__ecp_nistz256_sub_from,\@function
.align	4
__ecp_nistz256_sub_from:
	ld	$t0,0($bp)
	ld	$t1,8($bp)
	ld	$t2,16($bp)
	ld	$t3,24($bp)
	subfc	$acc0,$t0,$acc0		# ret = a-b
	subfe	$acc1,$t1,$acc1
	subfe	$acc2,$t2,$acc2
	subfe	$acc3,$t3,$acc3
	subfe	$t0,$t0,$t0		# t0 = borrow ? -1 : 0

	# if a-b borrowed, add modulus

	addc	$acc0,$acc0,$t0		# ret -= modulus & t0
	and	$t1,$poly1,$t0
	and	$t3,$poly3,$t0
	adde	$acc1,$acc1,$t1
	addze	$acc2,$acc2
	adde	$acc3,$acc3,$t3

	std	$acc0,0($rp)
	std	$acc1,8($rp)
	std	$acc2,16($rp)
	std	$acc3,24($rp)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	__ecp_nistz256_sub_from,.-__ecp_nistz256_sub_from

.type	__ecp_nistz256_sub_morf,\@function
.align	4
__ecp_nistz256_sub_morf:
	ld	$t0,0($bp)
	ld	$t1,8($bp)
	ld	$t2,16($bp)
	ld	$t3,24($bp)
	subfc	$acc0,$acc0,$t0 	# ret = b-a
	subfe	$acc1,$acc1,$t1
	subfe	$acc2,$acc2,$t2
	subfe	$acc3,$acc3,$t3
	subfe	$t0,$t0,$t0		# t0 = borrow ? -1 : 0

	# if b-a borrowed, add modulus

	addc	$acc0,$acc0,$t0		# ret -= modulus & t0
	and	$t1,$poly1,$t0
	and	$t3,$poly3,$t0
	adde	$acc1,$acc1,$t1
	addze	$acc2,$acc2
	adde	$acc3,$acc3,$t3

	std	$acc0,0($rp)
	std	$acc1,8($rp)
	std	$acc2,16($rp)
	std	$acc3,24($rp)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	__ecp_nistz256_sub_morf,.-__ecp_nistz256_sub_morf

.type	__ecp_nistz256_div_by_2,\@function
.align	4
__ecp_nistz256_div_by_2:
	andi.	$t0,$acc0,1
	addic	$acc0,$acc0,-1		# a += modulus
	 neg	$t0,$t0
	adde	$acc1,$acc1,$poly1
	 not	$t0,$t0
	addze	$acc2,$acc2
	 li	$t2,0
	adde	$acc3,$acc3,$poly3
	 and	$t1,$poly1,$t0
	addze	$ap,$t2			# ap = carry
	 and	$t3,$poly3,$t0

	subfc	$acc0,$t0,$acc0		# a -= modulus if a was even
	subfe	$acc1,$t1,$acc1
	subfe	$acc2,$t2,$acc2
	subfe	$acc3,$t3,$acc3
	subfe	$ap,  $t2,$ap

	srdi	$acc0,$acc0,1
	sldi	$t0,$acc1,63
	srdi	$acc1,$acc1,1
	sldi	$t1,$acc2,63
	srdi	$acc2,$acc2,1
	sldi	$t2,$acc3,63
	srdi	$acc3,$acc3,1
	sldi	$t3,$ap,63
	or	$acc0,$acc0,$t0
	or	$acc1,$acc1,$t1
	or	$acc2,$acc2,$t2
	or	$acc3,$acc3,$t3

	std	$acc0,0($rp)
	std	$acc1,8($rp)
	std	$acc2,16($rp)
	std	$acc3,24($rp)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,1,0
	.long	0
.size	__ecp_nistz256_div_by_2,.-__ecp_nistz256_div_by_2
___
########################################################################
# following subroutines are "literal" implementation of those found in
# ecp_nistz256.c
#
########################################################################
# void ecp_nistz256_point_double(P256_POINT *out,const P256_POINT *inp);
#
if (1) {
my $FRAME=64+32*4+12*8;
my ($S,$M,$Zsqr,$tmp0)=map(64+32*$_,(0..3));
# above map() describes stack layout with 4 temporary
# 256-bit vectors on top.
my ($rp_real,$ap_real) = map("r$_",(20,21));

$code.=<<___;
.globl	ecp_nistz256_point_double
.align	5
ecp_nistz256_point_double:
	stdu	$sp,-$FRAME($sp)
	mflr	r0
	std	r20,$FRAME-8*12($sp)
	std	r21,$FRAME-8*11($sp)
	std	r22,$FRAME-8*10($sp)
	std	r23,$FRAME-8*9($sp)
	std	r24,$FRAME-8*8($sp)
	std	r25,$FRAME-8*7($sp)
	std	r26,$FRAME-8*6($sp)
	std	r27,$FRAME-8*5($sp)
	std	r28,$FRAME-8*4($sp)
	std	r29,$FRAME-8*3($sp)
	std	r30,$FRAME-8*2($sp)
	std	r31,$FRAME-8*1($sp)

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001
.Ldouble_shortcut:
	ld	$acc0,32($ap)
	ld	$acc1,40($ap)
	ld	$acc2,48($ap)
	ld	$acc3,56($ap)
	mr	$t0,$acc0
	mr	$t1,$acc1
	mr	$t2,$acc2
	mr	$t3,$acc3
	 ld	$a0,64($ap)		# forward load for p256_sqr_mont
	 ld	$a1,72($ap)
	 ld	$a2,80($ap)
	 ld	$a3,88($ap)
	 mr	$rp_real,$rp
	 mr	$ap_real,$ap
	addi	$rp,$sp,$S
	bl	__ecp_nistz256_add	# p256_mul_by_2(S, in_y);

	addi	$rp,$sp,$Zsqr
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(Zsqr, in_z);

	ld	$t0,0($ap_real)
	ld	$t1,8($ap_real)
	ld	$t2,16($ap_real)
	ld	$t3,24($ap_real)
	mr	$a0,$acc0		# put Zsqr aside for p256_sub
	mr	$a1,$acc1
	mr	$a2,$acc2
	mr	$a3,$acc3
	addi	$rp,$sp,$M
	bl	__ecp_nistz256_add	# p256_add(M, Zsqr, in_x);

	addi	$bp,$ap_real,0
	mr	$acc0,$a0		# restore Zsqr
	mr	$acc1,$a1
	mr	$acc2,$a2
	mr	$acc3,$a3
	 ld	$a0,$S+0($sp)		# forward load for p256_sqr_mont
	 ld	$a1,$S+8($sp)
	 ld	$a2,$S+16($sp)
	 ld	$a3,$S+24($sp)
	addi	$rp,$sp,$Zsqr
	bl	__ecp_nistz256_sub_morf	# p256_sub(Zsqr, in_x, Zsqr);

	addi	$rp,$sp,$S
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(S, S);

	ld	$bi,32($ap_real)
	ld	$a0,64($ap_real)
	ld	$a1,72($ap_real)
	ld	$a2,80($ap_real)
	ld	$a3,88($ap_real)
	addi	$bp,$ap_real,32
	addi	$rp,$sp,$tmp0
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(tmp0, in_z, in_y);

	mr	$t0,$acc0
	mr	$t1,$acc1
	mr	$t2,$acc2
	mr	$t3,$acc3
	 ld	$a0,$S+0($sp)		# forward load for p256_sqr_mont
	 ld	$a1,$S+8($sp)
	 ld	$a2,$S+16($sp)
	 ld	$a3,$S+24($sp)
	addi	$rp,$rp_real,64
	bl	__ecp_nistz256_add	# p256_mul_by_2(res_z, tmp0);

	addi	$rp,$sp,$tmp0
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(tmp0, S);

	 ld	$bi,$Zsqr($sp)		# forward load for p256_mul_mont
	 ld	$a0,$M+0($sp)
	 ld	$a1,$M+8($sp)
	 ld	$a2,$M+16($sp)
	 ld	$a3,$M+24($sp)
	addi	$rp,$rp_real,32
	bl	__ecp_nistz256_div_by_2	# p256_div_by_2(res_y, tmp0);

	addi	$bp,$sp,$Zsqr
	addi	$rp,$sp,$M
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(M, M, Zsqr);

	mr	$t0,$acc0		# duplicate M
	mr	$t1,$acc1
	mr	$t2,$acc2
	mr	$t3,$acc3
	mr	$a0,$acc0		# put M aside
	mr	$a1,$acc1
	mr	$a2,$acc2
	mr	$a3,$acc3
	addi	$rp,$sp,$M
	bl	__ecp_nistz256_add
	mr	$t0,$a0			# restore M
	mr	$t1,$a1
	mr	$t2,$a2
	mr	$t3,$a3
	 ld	$bi,0($ap_real)		# forward load for p256_mul_mont
	 ld	$a0,$S+0($sp)
	 ld	$a1,$S+8($sp)
	 ld	$a2,$S+16($sp)
	 ld	$a3,$S+24($sp)
	bl	__ecp_nistz256_add	# p256_mul_by_3(M, M);

	addi	$bp,$ap_real,0
	addi	$rp,$sp,$S
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(S, S, in_x);

	mr	$t0,$acc0
	mr	$t1,$acc1
	mr	$t2,$acc2
	mr	$t3,$acc3
	 ld	$a0,$M+0($sp)		# forward load for p256_sqr_mont
	 ld	$a1,$M+8($sp)
	 ld	$a2,$M+16($sp)
	 ld	$a3,$M+24($sp)
	addi	$rp,$sp,$tmp0
	bl	__ecp_nistz256_add	# p256_mul_by_2(tmp0, S);

	addi	$rp,$rp_real,0
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(res_x, M);

	addi	$bp,$sp,$tmp0
	bl	__ecp_nistz256_sub_from	# p256_sub(res_x, res_x, tmp0);

	addi	$bp,$sp,$S
	addi	$rp,$sp,$S
	bl	__ecp_nistz256_sub_morf	# p256_sub(S, S, res_x);

	ld	$bi,$M($sp)
	mr	$a0,$acc0		# copy S
	mr	$a1,$acc1
	mr	$a2,$acc2
	mr	$a3,$acc3
	addi	$bp,$sp,$M
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(S, S, M);

	addi	$bp,$rp_real,32
	addi	$rp,$rp_real,32
	bl	__ecp_nistz256_sub_from	# p256_sub(res_y, S, res_y);

	mtlr	r0
	ld	r20,$FRAME-8*12($sp)
	ld	r21,$FRAME-8*11($sp)
	ld	r22,$FRAME-8*10($sp)
	ld	r23,$FRAME-8*9($sp)
	ld	r24,$FRAME-8*8($sp)
	ld	r25,$FRAME-8*7($sp)
	ld	r26,$FRAME-8*6($sp)
	ld	r27,$FRAME-8*5($sp)
	ld	r28,$FRAME-8*4($sp)
	ld	r29,$FRAME-8*3($sp)
	ld	r30,$FRAME-8*2($sp)
	ld	r31,$FRAME-8*1($sp)
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,4,0,0x80,12,2,0
	.long	0
.size	ecp_nistz256_point_double,.-ecp_nistz256_point_double
___
}

########################################################################
# void ecp_nistz256_point_add(P256_POINT *out,const P256_POINT *in1,
#			      const P256_POINT *in2);
if (1) {
my $FRAME = 64 + 32*12 + 16*8;
my ($res_x,$res_y,$res_z,
    $H,$Hsqr,$R,$Rsqr,$Hcub,
    $U1,$U2,$S1,$S2)=map(64+32*$_,(0..11));
my ($Z1sqr, $Z2sqr) = ($Hsqr, $Rsqr);
# above map() describes stack layout with 12 temporary
# 256-bit vectors on top.
my ($rp_real,$ap_real,$bp_real,$in1infty,$in2infty,$temp)=map("r$_",(16..21));

$code.=<<___;
.globl	ecp_nistz256_point_add
.align	5
ecp_nistz256_point_add:
	stdu	$sp,-$FRAME($sp)
	mflr	r0
	std	r16,$FRAME-8*16($sp)
	std	r17,$FRAME-8*15($sp)
	std	r18,$FRAME-8*14($sp)
	std	r19,$FRAME-8*13($sp)
	std	r20,$FRAME-8*12($sp)
	std	r21,$FRAME-8*11($sp)
	std	r22,$FRAME-8*10($sp)
	std	r23,$FRAME-8*9($sp)
	std	r24,$FRAME-8*8($sp)
	std	r25,$FRAME-8*7($sp)
	std	r26,$FRAME-8*6($sp)
	std	r27,$FRAME-8*5($sp)
	std	r28,$FRAME-8*4($sp)
	std	r29,$FRAME-8*3($sp)
	std	r30,$FRAME-8*2($sp)
	std	r31,$FRAME-8*1($sp)

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001

	ld	$a0,64($bp)		# in2_z
	ld	$a1,72($bp)
	ld	$a2,80($bp)
	ld	$a3,88($bp)
	 mr	$rp_real,$rp
	 mr	$ap_real,$ap
	 mr	$bp_real,$bp
	or	$t0,$a0,$a1
	or	$t2,$a2,$a3
	or	$in2infty,$t0,$t2
	neg	$t0,$in2infty
	or	$in2infty,$in2infty,$t0
	sradi	$in2infty,$in2infty,63	# !in2infty
	addi	$rp,$sp,$Z2sqr
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(Z2sqr, in2_z);

	ld	$a0,64($ap_real)	# in1_z
	ld	$a1,72($ap_real)
	ld	$a2,80($ap_real)
	ld	$a3,88($ap_real)
	or	$t0,$a0,$a1
	or	$t2,$a2,$a3
	or	$in1infty,$t0,$t2
	neg	$t0,$in1infty
	or	$in1infty,$in1infty,$t0
	sradi	$in1infty,$in1infty,63	# !in1infty
	addi	$rp,$sp,$Z1sqr
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(Z1sqr, in1_z);

	ld	$bi,64($bp_real)
	ld	$a0,$Z2sqr+0($sp)
	ld	$a1,$Z2sqr+8($sp)
	ld	$a2,$Z2sqr+16($sp)
	ld	$a3,$Z2sqr+24($sp)
	addi	$bp,$bp_real,64
	addi	$rp,$sp,$S1
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(S1, Z2sqr, in2_z);

	ld	$bi,64($ap_real)
	ld	$a0,$Z1sqr+0($sp)
	ld	$a1,$Z1sqr+8($sp)
	ld	$a2,$Z1sqr+16($sp)
	ld	$a3,$Z1sqr+24($sp)
	addi	$bp,$ap_real,64
	addi	$rp,$sp,$S2
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(S2, Z1sqr, in1_z);

	ld	$bi,32($ap_real)
	ld	$a0,$S1+0($sp)
	ld	$a1,$S1+8($sp)
	ld	$a2,$S1+16($sp)
	ld	$a3,$S1+24($sp)
	addi	$bp,$ap_real,32
	addi	$rp,$sp,$S1
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(S1, S1, in1_y);

	ld	$bi,32($bp_real)
	ld	$a0,$S2+0($sp)
	ld	$a1,$S2+8($sp)
	ld	$a2,$S2+16($sp)
	ld	$a3,$S2+24($sp)
	addi	$bp,$bp_real,32
	addi	$rp,$sp,$S2
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(S2, S2, in2_y);

	addi	$bp,$sp,$S1
	 ld	$bi,$Z2sqr($sp)		# forward load for p256_mul_mont
	 ld	$a0,0($ap_real)
	 ld	$a1,8($ap_real)
	 ld	$a2,16($ap_real)
	 ld	$a3,24($ap_real)
	addi	$rp,$sp,$R
	bl	__ecp_nistz256_sub_from	# p256_sub(R, S2, S1);

	or	$acc0,$acc0,$acc1	# see if result is zero
	or	$acc2,$acc2,$acc3
	or	$temp,$acc0,$acc2

	addi	$bp,$sp,$Z2sqr
	addi	$rp,$sp,$U1
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(U1, in1_x, Z2sqr);

	ld	$bi,$Z1sqr($sp)
	ld	$a0,0($bp_real)
	ld	$a1,8($bp_real)
	ld	$a2,16($bp_real)
	ld	$a3,24($bp_real)
	addi	$bp,$sp,$Z1sqr
	addi	$rp,$sp,$U2
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(U2, in2_x, Z1sqr);

	addi	$bp,$sp,$U1
	 ld	$a0,$R+0($sp)		# forward load for p256_sqr_mont
	 ld	$a1,$R+8($sp)
	 ld	$a2,$R+16($sp)
	 ld	$a3,$R+24($sp)
	addi	$rp,$sp,$H
	bl	__ecp_nistz256_sub_from	# p256_sub(H, U2, U1);

	or	$acc0,$acc0,$acc1	# see if result is zero
	or	$acc2,$acc2,$acc3
	or.	$acc0,$acc0,$acc2
	bne	.Ladd_proceed		# is_equal(U1,U2)?

	and.	$t0,$in1infty,$in2infty
	beq	.Ladd_proceed		# (in1infty || in2infty)?

	cmpldi	$temp,0
	beq	.Ladd_double		# is_equal(S1,S2)?

	xor	$a0,$a0,$a0
	std	$a0,0($rp_real)
	std	$a0,8($rp_real)
	std	$a0,16($rp_real)
	std	$a0,24($rp_real)
	std	$a0,32($rp_real)
	std	$a0,40($rp_real)
	std	$a0,48($rp_real)
	std	$a0,56($rp_real)
	std	$a0,64($rp_real)
	std	$a0,72($rp_real)
	std	$a0,80($rp_real)
	std	$a0,88($rp_real)
	b	.Ladd_done

.align	4
.Ladd_double:
	ld	$bp,0($sp)		# back-link
	mr	$ap,$ap_real
	mr	$rp,$rp_real
	ld	r16,$FRAME-8*16($sp)
	ld	r17,$FRAME-8*15($sp)
	ld	r18,$FRAME-8*14($sp)
	ld	r19,$FRAME-8*13($sp)
	stdu	$bp,$FRAME-288($sp)	# difference in stack frame sizes
	b	.Ldouble_shortcut

.align	4
.Ladd_proceed:
	addi	$rp,$sp,$Rsqr
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(Rsqr, R);

	ld	$bi,64($ap_real)
	ld	$a0,$H+0($sp)
	ld	$a1,$H+8($sp)
	ld	$a2,$H+16($sp)
	ld	$a3,$H+24($sp)
	addi	$bp,$ap_real,64
	addi	$rp,$sp,$res_z
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(res_z, H, in1_z);

	ld	$a0,$H+0($sp)
	ld	$a1,$H+8($sp)
	ld	$a2,$H+16($sp)
	ld	$a3,$H+24($sp)
	addi	$rp,$sp,$Hsqr
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(Hsqr, H);

	ld	$bi,64($bp_real)
	ld	$a0,$res_z+0($sp)
	ld	$a1,$res_z+8($sp)
	ld	$a2,$res_z+16($sp)
	ld	$a3,$res_z+24($sp)
	addi	$bp,$bp_real,64
	addi	$rp,$sp,$res_z
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(res_z, res_z, in2_z);

	ld	$bi,$H($sp)
	ld	$a0,$Hsqr+0($sp)
	ld	$a1,$Hsqr+8($sp)
	ld	$a2,$Hsqr+16($sp)
	ld	$a3,$Hsqr+24($sp)
	addi	$bp,$sp,$H
	addi	$rp,$sp,$Hcub
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(Hcub, Hsqr, H);

	ld	$bi,$Hsqr($sp)
	ld	$a0,$U1+0($sp)
	ld	$a1,$U1+8($sp)
	ld	$a2,$U1+16($sp)
	ld	$a3,$U1+24($sp)
	addi	$bp,$sp,$Hsqr
	addi	$rp,$sp,$U2
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(U2, U1, Hsqr);

	mr	$t0,$acc0
	mr	$t1,$acc1
	mr	$t2,$acc2
	mr	$t3,$acc3
	addi	$rp,$sp,$Hsqr
	bl	__ecp_nistz256_add	# p256_mul_by_2(Hsqr, U2);

	addi	$bp,$sp,$Rsqr
	addi	$rp,$sp,$res_x
	bl	__ecp_nistz256_sub_morf	# p256_sub(res_x, Rsqr, Hsqr);

	addi	$bp,$sp,$Hcub
	bl	__ecp_nistz256_sub_from	# p256_sub(res_x, res_x, Hcub);

	addi	$bp,$sp,$U2
	 ld	$bi,$Hcub($sp)		# forward load for p256_mul_mont
	 ld	$a0,$S1+0($sp)
	 ld	$a1,$S1+8($sp)
	 ld	$a2,$S1+16($sp)
	 ld	$a3,$S1+24($sp)
	addi	$rp,$sp,$res_y
	bl	__ecp_nistz256_sub_morf	# p256_sub(res_y, U2, res_x);

	addi	$bp,$sp,$Hcub
	addi	$rp,$sp,$S2
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(S2, S1, Hcub);

	ld	$bi,$R($sp)
	ld	$a0,$res_y+0($sp)
	ld	$a1,$res_y+8($sp)
	ld	$a2,$res_y+16($sp)
	ld	$a3,$res_y+24($sp)
	addi	$bp,$sp,$R
	addi	$rp,$sp,$res_y
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(res_y, res_y, R);

	addi	$bp,$sp,$S2
	bl	__ecp_nistz256_sub_from	# p256_sub(res_y, res_y, S2);

	ld	$t0,0($bp_real)		# in2
	ld	$t1,8($bp_real)
	ld	$t2,16($bp_real)
	ld	$t3,24($bp_real)
	ld	$a0,$res_x+0($sp)	# res
	ld	$a1,$res_x+8($sp)
	ld	$a2,$res_x+16($sp)
	ld	$a3,$res_x+24($sp)
___
for($i=0;$i<64;$i+=32) {		# conditional moves
$code.=<<___;
	ld	$acc0,$i+0($ap_real)	# in1
	ld	$acc1,$i+8($ap_real)
	ld	$acc2,$i+16($ap_real)
	ld	$acc3,$i+24($ap_real)
	andc	$t0,$t0,$in1infty
	andc	$t1,$t1,$in1infty
	andc	$t2,$t2,$in1infty
	andc	$t3,$t3,$in1infty
	and	$a0,$a0,$in1infty
	and	$a1,$a1,$in1infty
	and	$a2,$a2,$in1infty
	and	$a3,$a3,$in1infty
	or	$t0,$t0,$a0
	or	$t1,$t1,$a1
	or	$t2,$t2,$a2
	or	$t3,$t3,$a3
	andc	$acc0,$acc0,$in2infty
	andc	$acc1,$acc1,$in2infty
	andc	$acc2,$acc2,$in2infty
	andc	$acc3,$acc3,$in2infty
	and	$t0,$t0,$in2infty
	and	$t1,$t1,$in2infty
	and	$t2,$t2,$in2infty
	and	$t3,$t3,$in2infty
	or	$acc0,$acc0,$t0
	or	$acc1,$acc1,$t1
	or	$acc2,$acc2,$t2
	or	$acc3,$acc3,$t3

	ld	$t0,$i+32($bp_real)	# in2
	ld	$t1,$i+40($bp_real)
	ld	$t2,$i+48($bp_real)
	ld	$t3,$i+56($bp_real)
	ld	$a0,$res_x+$i+32($sp)
	ld	$a1,$res_x+$i+40($sp)
	ld	$a2,$res_x+$i+48($sp)
	ld	$a3,$res_x+$i+56($sp)
	std	$acc0,$i+0($rp_real)
	std	$acc1,$i+8($rp_real)
	std	$acc2,$i+16($rp_real)
	std	$acc3,$i+24($rp_real)
___
}
$code.=<<___;
	ld	$acc0,$i+0($ap_real)	# in1
	ld	$acc1,$i+8($ap_real)
	ld	$acc2,$i+16($ap_real)
	ld	$acc3,$i+24($ap_real)
	andc	$t0,$t0,$in1infty
	andc	$t1,$t1,$in1infty
	andc	$t2,$t2,$in1infty
	andc	$t3,$t3,$in1infty
	and	$a0,$a0,$in1infty
	and	$a1,$a1,$in1infty
	and	$a2,$a2,$in1infty
	and	$a3,$a3,$in1infty
	or	$t0,$t0,$a0
	or	$t1,$t1,$a1
	or	$t2,$t2,$a2
	or	$t3,$t3,$a3
	andc	$acc0,$acc0,$in2infty
	andc	$acc1,$acc1,$in2infty
	andc	$acc2,$acc2,$in2infty
	andc	$acc3,$acc3,$in2infty
	and	$t0,$t0,$in2infty
	and	$t1,$t1,$in2infty
	and	$t2,$t2,$in2infty
	and	$t3,$t3,$in2infty
	or	$acc0,$acc0,$t0
	or	$acc1,$acc1,$t1
	or	$acc2,$acc2,$t2
	or	$acc3,$acc3,$t3
	std	$acc0,$i+0($rp_real)
	std	$acc1,$i+8($rp_real)
	std	$acc2,$i+16($rp_real)
	std	$acc3,$i+24($rp_real)

.Ladd_done:
	mtlr	r0
	ld	r16,$FRAME-8*16($sp)
	ld	r17,$FRAME-8*15($sp)
	ld	r18,$FRAME-8*14($sp)
	ld	r19,$FRAME-8*13($sp)
	ld	r20,$FRAME-8*12($sp)
	ld	r21,$FRAME-8*11($sp)
	ld	r22,$FRAME-8*10($sp)
	ld	r23,$FRAME-8*9($sp)
	ld	r24,$FRAME-8*8($sp)
	ld	r25,$FRAME-8*7($sp)
	ld	r26,$FRAME-8*6($sp)
	ld	r27,$FRAME-8*5($sp)
	ld	r28,$FRAME-8*4($sp)
	ld	r29,$FRAME-8*3($sp)
	ld	r30,$FRAME-8*2($sp)
	ld	r31,$FRAME-8*1($sp)
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,4,0,0x80,16,3,0
	.long	0
.size	ecp_nistz256_point_add,.-ecp_nistz256_point_add
___
}

########################################################################
# void ecp_nistz256_point_add_affine(P256_POINT *out,const P256_POINT *in1,
#				     const P256_POINT_AFFINE *in2);
if (1) {
my $FRAME = 64 + 32*10 + 16*8;
my ($res_x,$res_y,$res_z,
    $U2,$S2,$H,$R,$Hsqr,$Hcub,$Rsqr)=map(64+32*$_,(0..9));
my $Z1sqr = $S2;
# above map() describes stack layout with 10 temporary
# 256-bit vectors on top.
my ($rp_real,$ap_real,$bp_real,$in1infty,$in2infty,$temp)=map("r$_",(16..21));

$code.=<<___;
.globl	ecp_nistz256_point_add_affine
.align	5
ecp_nistz256_point_add_affine:
	stdu	$sp,-$FRAME($sp)
	mflr	r0
	std	r16,$FRAME-8*16($sp)
	std	r17,$FRAME-8*15($sp)
	std	r18,$FRAME-8*14($sp)
	std	r19,$FRAME-8*13($sp)
	std	r20,$FRAME-8*12($sp)
	std	r21,$FRAME-8*11($sp)
	std	r22,$FRAME-8*10($sp)
	std	r23,$FRAME-8*9($sp)
	std	r24,$FRAME-8*8($sp)
	std	r25,$FRAME-8*7($sp)
	std	r26,$FRAME-8*6($sp)
	std	r27,$FRAME-8*5($sp)
	std	r28,$FRAME-8*4($sp)
	std	r29,$FRAME-8*3($sp)
	std	r30,$FRAME-8*2($sp)
	std	r31,$FRAME-8*1($sp)

	li	$poly1,-1
	srdi	$poly1,$poly1,32	# 0x00000000ffffffff
	li	$poly3,1
	orc	$poly3,$poly3,$poly1	# 0xffffffff00000001

	mr	$rp_real,$rp
	mr	$ap_real,$ap
	mr	$bp_real,$bp

	ld	$a0,64($ap)		# in1_z
	ld	$a1,72($ap)
	ld	$a2,80($ap)
	ld	$a3,88($ap)
	or	$t0,$a0,$a1
	or	$t2,$a2,$a3
	or	$in1infty,$t0,$t2
	neg	$t0,$in1infty
	or	$in1infty,$in1infty,$t0
	sradi	$in1infty,$in1infty,63	# !in1infty

	ld	$acc0,0($bp)		# in2_x
	ld	$acc1,8($bp)
	ld	$acc2,16($bp)
	ld	$acc3,24($bp)
	ld	$t0,32($bp)		# in2_y
	ld	$t1,40($bp)
	ld	$t2,48($bp)
	ld	$t3,56($bp)
	or	$acc0,$acc0,$acc1
	or	$acc2,$acc2,$acc3
	or	$acc0,$acc0,$acc2
	or	$t0,$t0,$t1
	or	$t2,$t2,$t3
	or	$t0,$t0,$t2
	or	$in2infty,$acc0,$t0
	neg	$t0,$in2infty
	or	$in2infty,$in2infty,$t0
	sradi	$in2infty,$in2infty,63	# !in2infty

	addi	$rp,$sp,$Z1sqr
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(Z1sqr, in1_z);

	mr	$a0,$acc0
	mr	$a1,$acc1
	mr	$a2,$acc2
	mr	$a3,$acc3
	ld	$bi,0($bp_real)
	addi	$bp,$bp_real,0
	addi	$rp,$sp,$U2
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(U2, Z1sqr, in2_x);

	addi	$bp,$ap_real,0
	 ld	$bi,64($ap_real)	# forward load for p256_mul_mont
	 ld	$a0,$Z1sqr+0($sp)
	 ld	$a1,$Z1sqr+8($sp)
	 ld	$a2,$Z1sqr+16($sp)
	 ld	$a3,$Z1sqr+24($sp)
	addi	$rp,$sp,$H
	bl	__ecp_nistz256_sub_from	# p256_sub(H, U2, in1_x);

	addi	$bp,$ap_real,64
	addi	$rp,$sp,$S2
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(S2, Z1sqr, in1_z);

	ld	$bi,64($ap_real)
	ld	$a0,$H+0($sp)
	ld	$a1,$H+8($sp)
	ld	$a2,$H+16($sp)
	ld	$a3,$H+24($sp)
	addi	$bp,$ap_real,64
	addi	$rp,$sp,$res_z
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(res_z, H, in1_z);

	ld	$bi,32($bp_real)
	ld	$a0,$S2+0($sp)
	ld	$a1,$S2+8($sp)
	ld	$a2,$S2+16($sp)
	ld	$a3,$S2+24($sp)
	addi	$bp,$bp_real,32
	addi	$rp,$sp,$S2
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(S2, S2, in2_y);

	addi	$bp,$ap_real,32
	 ld	$a0,$H+0($sp)		# forward load for p256_sqr_mont
	 ld	$a1,$H+8($sp)
	 ld	$a2,$H+16($sp)
	 ld	$a3,$H+24($sp)
	addi	$rp,$sp,$R
	bl	__ecp_nistz256_sub_from	# p256_sub(R, S2, in1_y);

	addi	$rp,$sp,$Hsqr
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(Hsqr, H);

	ld	$a0,$R+0($sp)
	ld	$a1,$R+8($sp)
	ld	$a2,$R+16($sp)
	ld	$a3,$R+24($sp)
	addi	$rp,$sp,$Rsqr
	bl	__ecp_nistz256_sqr_mont	# p256_sqr_mont(Rsqr, R);

	ld	$bi,$H($sp)
	ld	$a0,$Hsqr+0($sp)
	ld	$a1,$Hsqr+8($sp)
	ld	$a2,$Hsqr+16($sp)
	ld	$a3,$Hsqr+24($sp)
	addi	$bp,$sp,$H
	addi	$rp,$sp,$Hcub
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(Hcub, Hsqr, H);

	ld	$bi,0($ap_real)
	ld	$a0,$Hsqr+0($sp)
	ld	$a1,$Hsqr+8($sp)
	ld	$a2,$Hsqr+16($sp)
	ld	$a3,$Hsqr+24($sp)
	addi	$bp,$ap_real,0
	addi	$rp,$sp,$U2
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(U2, in1_x, Hsqr);

	mr	$t0,$acc0
	mr	$t1,$acc1
	mr	$t2,$acc2
	mr	$t3,$acc3
	addi	$rp,$sp,$Hsqr
	bl	__ecp_nistz256_add	# p256_mul_by_2(Hsqr, U2);

	addi	$bp,$sp,$Rsqr
	addi	$rp,$sp,$res_x
	bl	__ecp_nistz256_sub_morf	# p256_sub(res_x, Rsqr, Hsqr);

	addi	$bp,$sp,$Hcub
	bl	__ecp_nistz256_sub_from	#  p256_sub(res_x, res_x, Hcub);

	addi	$bp,$sp,$U2
	 ld	$bi,32($ap_real)	# forward load for p256_mul_mont
	 ld	$a0,$Hcub+0($sp)
	 ld	$a1,$Hcub+8($sp)
	 ld	$a2,$Hcub+16($sp)
	 ld	$a3,$Hcub+24($sp)
	addi	$rp,$sp,$res_y
	bl	__ecp_nistz256_sub_morf	# p256_sub(res_y, U2, res_x);

	addi	$bp,$ap_real,32
	addi	$rp,$sp,$S2
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(S2, in1_y, Hcub);

	ld	$bi,$R($sp)
	ld	$a0,$res_y+0($sp)
	ld	$a1,$res_y+8($sp)
	ld	$a2,$res_y+16($sp)
	ld	$a3,$res_y+24($sp)
	addi	$bp,$sp,$R
	addi	$rp,$sp,$res_y
	bl	__ecp_nistz256_mul_mont	# p256_mul_mont(res_y, res_y, R);

	addi	$bp,$sp,$S2
	bl	__ecp_nistz256_sub_from	# p256_sub(res_y, res_y, S2);

	ld	$t0,0($bp_real)		# in2
	ld	$t1,8($bp_real)
	ld	$t2,16($bp_real)
	ld	$t3,24($bp_real)
	ld	$a0,$res_x+0($sp)	# res
	ld	$a1,$res_x+8($sp)
	ld	$a2,$res_x+16($sp)
	ld	$a3,$res_x+24($sp)
___
for($i=0;$i<64;$i+=32) {		# conditional moves
$code.=<<___;
	ld	$acc0,$i+0($ap_real)	# in1
	ld	$acc1,$i+8($ap_real)
	ld	$acc2,$i+16($ap_real)
	ld	$acc3,$i+24($ap_real)
	andc	$t0,$t0,$in1infty
	andc	$t1,$t1,$in1infty
	andc	$t2,$t2,$in1infty
	andc	$t3,$t3,$in1infty
	and	$a0,$a0,$in1infty
	and	$a1,$a1,$in1infty
	and	$a2,$a2,$in1infty
	and	$a3,$a3,$in1infty
	or	$t0,$t0,$a0
	or	$t1,$t1,$a1
	or	$t2,$t2,$a2
	or	$t3,$t3,$a3
	andc	$acc0,$acc0,$in2infty
	andc	$acc1,$acc1,$in2infty
	andc	$acc2,$acc2,$in2infty
	andc	$acc3,$acc3,$in2infty
	and	$t0,$t0,$in2infty
	and	$t1,$t1,$in2infty
	and	$t2,$t2,$in2infty
	and	$t3,$t3,$in2infty
	or	$acc0,$acc0,$t0
	or	$acc1,$acc1,$t1
	or	$acc2,$acc2,$t2
	or	$acc3,$acc3,$t3
___
$code.=<<___	if ($i==0);
	ld	$t0,32($bp_real)	# in2
	ld	$t1,40($bp_real)
	ld	$t2,48($bp_real)
	ld	$t3,56($bp_real)
___
$code.=<<___	if ($i==32);
	li	$t0,1			# Lone_mont
	not	$t1,$poly1
	li	$t2,-1
	not	$t3,$poly3
___
$code.=<<___;
	ld	$a0,$res_x+$i+32($sp)
	ld	$a1,$res_x+$i+40($sp)
	ld	$a2,$res_x+$i+48($sp)
	ld	$a3,$res_x+$i+56($sp)
	std	$acc0,$i+0($rp_real)
	std	$acc1,$i+8($rp_real)
	std	$acc2,$i+16($rp_real)
	std	$acc3,$i+24($rp_real)
___
}
$code.=<<___;
	ld	$acc0,$i+0($ap_real)	# in1
	ld	$acc1,$i+8($ap_real)
	ld	$acc2,$i+16($ap_real)
	ld	$acc3,$i+24($ap_real)
	andc	$t0,$t0,$in1infty
	andc	$t1,$t1,$in1infty
	andc	$t2,$t2,$in1infty
	andc	$t3,$t3,$in1infty
	and	$a0,$a0,$in1infty
	and	$a1,$a1,$in1infty
	and	$a2,$a2,$in1infty
	and	$a3,$a3,$in1infty
	or	$t0,$t0,$a0
	or	$t1,$t1,$a1
	or	$t2,$t2,$a2
	or	$t3,$t3,$a3
	andc	$acc0,$acc0,$in2infty
	andc	$acc1,$acc1,$in2infty
	andc	$acc2,$acc2,$in2infty
	andc	$acc3,$acc3,$in2infty
	and	$t0,$t0,$in2infty
	and	$t1,$t1,$in2infty
	and	$t2,$t2,$in2infty
	and	$t3,$t3,$in2infty
	or	$acc0,$acc0,$t0
	or	$acc1,$acc1,$t1
	or	$acc2,$acc2,$t2
	or	$acc3,$acc3,$t3
	std	$acc0,$i+0($rp_real)
	std	$acc1,$i+8($rp_real)
	std	$acc2,$i+16($rp_real)
	std	$acc3,$i+24($rp_real)

	mtlr	r0
	ld	r16,$FRAME-8*16($sp)
	ld	r17,$FRAME-8*15($sp)
	ld	r18,$FRAME-8*14($sp)
	ld	r19,$FRAME-8*13($sp)
	ld	r20,$FRAME-8*12($sp)
	ld	r21,$FRAME-8*11($sp)
	ld	r22,$FRAME-8*10($sp)
	ld	r23,$FRAME-8*9($sp)
	ld	r24,$FRAME-8*8($sp)
	ld	r25,$FRAME-8*7($sp)
	ld	r26,$FRAME-8*6($sp)
	ld	r27,$FRAME-8*5($sp)
	ld	r28,$FRAME-8*4($sp)
	ld	r29,$FRAME-8*3($sp)
	ld	r30,$FRAME-8*2($sp)
	ld	r31,$FRAME-8*1($sp)
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,4,0,0x80,16,3,0
	.long	0
.size	ecp_nistz256_point_add_affine,.-ecp_nistz256_point_add_affine
___
}
if (1) {
my ($ordk,$ord0,$ord1,$t4) = map("r$_",(18..21));
my ($ord2,$ord3,$zr) = ($poly1,$poly3,"r0");

$code.=<<___;
########################################################################
# void ecp_nistz256_ord_mul_mont(uint64_t res[4], uint64_t a[4],
#                                uint64_t b[4]);
.globl	ecp_nistz256_ord_mul_mont
.align	5
ecp_nistz256_ord_mul_mont:
	stdu	$sp,-160($sp)
	std	r18,48($sp)
	std	r19,56($sp)
	std	r20,64($sp)
	std	r21,72($sp)
	std	r22,80($sp)
	std	r23,88($sp)
	std	r24,96($sp)
	std	r25,104($sp)
	std	r26,112($sp)
	std	r27,120($sp)
	std	r28,128($sp)
	std	r29,136($sp)
	std	r30,144($sp)
	std	r31,152($sp)

	ld	$a0,0($ap)
	ld	$bi,0($bp)
	ld	$a1,8($ap)
	ld	$a2,16($ap)
	ld	$a3,24($ap)

	lis	$ordk,0xccd1
	lis	$ord0,0xf3b9
	lis	$ord1,0xbce6
	ori	$ordk,$ordk,0xc8aa
	ori	$ord0,$ord0,0xcac2
	ori	$ord1,$ord1,0xfaad
	sldi	$ordk,$ordk,32
	sldi	$ord0,$ord0,32
	sldi	$ord1,$ord1,32
	oris	$ordk,$ordk,0xee00
	oris	$ord0,$ord0,0xfc63
	oris	$ord1,$ord1,0xa717
	ori	$ordk,$ordk,0xbc4f	# 0xccd1c8aaee00bc4f
	ori	$ord0,$ord0,0x2551	# 0xf3b9cac2fc632551
	ori	$ord1,$ord1,0x9e84	# 0xbce6faada7179e84
	li	$ord2,-1		# 0xffffffffffffffff
	sldi	$ord3,$ord2,32		# 0xffffffff00000000
	li	$zr,0

	mulld	$acc0,$a0,$bi		# a[0]*b[0]
	mulhdu	$t0,$a0,$bi

	mulld	$acc1,$a1,$bi		# a[1]*b[0]
	mulhdu	$t1,$a1,$bi

	mulld	$acc2,$a2,$bi		# a[2]*b[0]
	mulhdu	$t2,$a2,$bi

	mulld	$acc3,$a3,$bi		# a[3]*b[0]
	mulhdu	$acc4,$a3,$bi

	mulld	$t4,$acc0,$ordk

	addc	$acc1,$acc1,$t0		# accumulate high parts of multiplication
	adde	$acc2,$acc2,$t1
	adde	$acc3,$acc3,$t2
	addze	$acc4,$acc4
	li	$acc5,0
___
for ($i=1;$i<4;$i++) {
	################################################################
	#            ffff0000.ffffffff.yyyyyyyy.zzzzzzzz
	# *                                     abcdefgh
	# + xxxxxxxx.xxxxxxxx.xxxxxxxx.xxxxxxxx.xxxxxxxx
	#
	# Now observing that ff..ff*x = (2^n-1)*x = 2^n*x-x, we
	# rewrite above as:
	#
	#   xxxxxxxx.xxxxxxxx.xxxxxxxx.xxxxxxxx.xxxxxxxx
	# - 0000abcd.efgh0000.abcdefgh.00000000.00000000
	# + abcdefgh.abcdefgh.yzayzbyz.cyzdyzey.zfyzgyzh
$code.=<<___;
	ld	$bi,8*$i($bp)		# b[i]

	sldi	$t0,$t4,32
	subfc	$acc2,$t4,$acc2
	srdi	$t1,$t4,32
	subfe	$acc3,$t0,$acc3
	subfe	$acc4,$t1,$acc4
	subfe	$acc5,$zr,$acc5

	addic	$t0,$acc0,-1		# discarded
	mulhdu	$t1,$ord0,$t4
	mulld	$t2,$ord1,$t4
	mulhdu	$t3,$ord1,$t4

	adde	$t2,$t2,$t1
	 mulld	$t0,$a0,$bi
	addze	$t3,$t3
	 mulld	$t1,$a1,$bi

	addc	$acc0,$acc1,$t2
	 mulld	$t2,$a2,$bi
	adde	$acc1,$acc2,$t3
	 mulld	$t3,$a3,$bi
	adde	$acc2,$acc3,$t4
	adde	$acc3,$acc4,$t4
	addze	$acc4,$acc5

	addc	$acc0,$acc0,$t0		# accumulate low parts
	mulhdu	$t0,$a0,$bi
	adde	$acc1,$acc1,$t1
	mulhdu	$t1,$a1,$bi
	adde	$acc2,$acc2,$t2
	mulhdu	$t2,$a2,$bi
	adde	$acc3,$acc3,$t3
	mulhdu	$t3,$a3,$bi
	addze	$acc4,$acc4
	mulld	$t4,$acc0,$ordk
	addc	$acc1,$acc1,$t0		# accumulate high parts
	adde	$acc2,$acc2,$t1
	adde	$acc3,$acc3,$t2
	adde	$acc4,$acc4,$t3
	addze	$acc5,$zr
___
}
$code.=<<___;
	sldi	$t0,$t4,32		# last reduction
	subfc	$acc2,$t4,$acc2
	srdi	$t1,$t4,32
	subfe	$acc3,$t0,$acc3
	subfe	$acc4,$t1,$acc4
	subfe	$acc5,$zr,$acc5

	addic	$t0,$acc0,-1		# discarded
	mulhdu	$t1,$ord0,$t4
	mulld	$t2,$ord1,$t4
	mulhdu	$t3,$ord1,$t4

	adde	$t2,$t2,$t1
	addze	$t3,$t3

	addc	$acc0,$acc1,$t2
	adde	$acc1,$acc2,$t3
	adde	$acc2,$acc3,$t4
	adde	$acc3,$acc4,$t4
	addze	$acc4,$acc5

	subfc	$acc0,$ord0,$acc0	# ret -= modulus
	subfe	$acc1,$ord1,$acc1
	subfe	$acc2,$ord2,$acc2
	subfe	$acc3,$ord3,$acc3
	subfe	$acc4,$zr,$acc4

	and	$t0,$ord0,$acc4
	and	$t1,$ord1,$acc4
	addc	$acc0,$acc0,$t0		# ret += modulus if borrow
	and	$t3,$ord3,$acc4
	adde	$acc1,$acc1,$t1
	adde	$acc2,$acc2,$acc4
	adde	$acc3,$acc3,$t3

	std	$acc0,0($rp)
	std	$acc1,8($rp)
	std	$acc2,16($rp)
	std	$acc3,24($rp)

	ld	r18,48($sp)
	ld	r19,56($sp)
	ld	r20,64($sp)
	ld	r21,72($sp)
	ld	r22,80($sp)
	ld	r23,88($sp)
	ld	r24,96($sp)
	ld	r25,104($sp)
	ld	r26,112($sp)
	ld	r27,120($sp)
	ld	r28,128($sp)
	ld	r29,136($sp)
	ld	r30,144($sp)
	ld	r31,152($sp)
	addi	$sp,$sp,160
	blr
	.long	0
	.byte	0,12,4,0,0x80,14,3,0
	.long	0
.size	ecp_nistz256_ord_mul_mont,.-ecp_nistz256_ord_mul_mont

################################################################################
# void ecp_nistz256_ord_sqr_mont(uint64_t res[4], uint64_t a[4],
#                                int rep);
.globl	ecp_nistz256_ord_sqr_mont
.align	5
ecp_nistz256_ord_sqr_mont:
	stdu	$sp,-160($sp)
	std	r18,48($sp)
	std	r19,56($sp)
	std	r20,64($sp)
	std	r21,72($sp)
	std	r22,80($sp)
	std	r23,88($sp)
	std	r24,96($sp)
	std	r25,104($sp)
	std	r26,112($sp)
	std	r27,120($sp)
	std	r28,128($sp)
	std	r29,136($sp)
	std	r30,144($sp)
	std	r31,152($sp)

	mtctr	$bp

	ld	$a0,0($ap)
	ld	$a1,8($ap)
	ld	$a2,16($ap)
	ld	$a3,24($ap)

	lis	$ordk,0xccd1
	lis	$ord0,0xf3b9
	lis	$ord1,0xbce6
	ori	$ordk,$ordk,0xc8aa
	ori	$ord0,$ord0,0xcac2
	ori	$ord1,$ord1,0xfaad
	sldi	$ordk,$ordk,32
	sldi	$ord0,$ord0,32
	sldi	$ord1,$ord1,32
	oris	$ordk,$ordk,0xee00
	oris	$ord0,$ord0,0xfc63
	oris	$ord1,$ord1,0xa717
	ori	$ordk,$ordk,0xbc4f	# 0xccd1c8aaee00bc4f
	ori	$ord0,$ord0,0x2551	# 0xf3b9cac2fc632551
	ori	$ord1,$ord1,0x9e84	# 0xbce6faada7179e84
	li	$ord2,-1		# 0xffffffffffffffff
	sldi	$ord3,$ord2,32		# 0xffffffff00000000
	li	$zr,0
	b	.Loop_ord_sqr

.align	5
.Loop_ord_sqr:
	################################################################
	#  |  |  |  |  |  |a1*a0|  |
	#  |  |  |  |  |a2*a0|  |  |
	#  |  |a3*a2|a3*a0|  |  |  |
	#  |  |  |  |a2*a1|  |  |  |
	#  |  |  |a3*a1|  |  |  |  |
	# *|  |  |  |  |  |  |  | 2|
	# +|a3*a3|a2*a2|a1*a1|a0*a0|
	#  |--+--+--+--+--+--+--+--|
	#  |A7|A6|A5|A4|A3|A2|A1|A0|, where Ax is $accx, i.e. follow $accx
	#
	#  "can't overflow" below mark carrying into high part of
	#  multiplication result, which can't overflow, because it
	#  can never be all ones.

	mulld	$acc1,$a1,$a0		# a[1]*a[0]
	mulhdu	$t1,$a1,$a0
	mulld	$acc2,$a2,$a0		# a[2]*a[0]
	mulhdu	$t2,$a2,$a0
	mulld	$acc3,$a3,$a0		# a[3]*a[0]
	mulhdu	$acc4,$a3,$a0

	addc	$acc2,$acc2,$t1		# accumulate high parts of multiplication
	 mulld	$t0,$a2,$a1		# a[2]*a[1]
	 mulhdu	$t1,$a2,$a1
	adde	$acc3,$acc3,$t2
	 mulld	$t2,$a3,$a1		# a[3]*a[1]
	 mulhdu	$t3,$a3,$a1
	addze	$acc4,$acc4		# can't overflow

	mulld	$acc5,$a3,$a2		# a[3]*a[2]
	mulhdu	$acc6,$a3,$a2

	addc	$t1,$t1,$t2		# accumulate high parts of multiplication
	 mulld	$acc0,$a0,$a0		# a[0]*a[0]
	addze	$t2,$t3			# can't overflow

	addc	$acc3,$acc3,$t0		# accumulate low parts of multiplication
	 mulhdu	$a0,$a0,$a0
	adde	$acc4,$acc4,$t1
	 mulld	$t1,$a1,$a1		# a[1]*a[1]
	adde	$acc5,$acc5,$t2
	 mulhdu	$a1,$a1,$a1
	addze	$acc6,$acc6		# can't overflow

	addc	$acc1,$acc1,$acc1	# acc[1-6]*=2
	 mulld	$t2,$a2,$a2		# a[2]*a[2]
	adde	$acc2,$acc2,$acc2
	 mulhdu	$a2,$a2,$a2
	adde	$acc3,$acc3,$acc3
	 mulld	$t3,$a3,$a3		# a[3]*a[3]
	adde	$acc4,$acc4,$acc4
	 mulhdu	$a3,$a3,$a3
	adde	$acc5,$acc5,$acc5
	adde	$acc6,$acc6,$acc6
	addze	$acc7,$zr

	addc	$acc1,$acc1,$a0		# +a[i]*a[i]
	 mulld	$t4,$acc0,$ordk
	adde	$acc2,$acc2,$t1
	adde	$acc3,$acc3,$a1
	adde	$acc4,$acc4,$t2
	adde	$acc5,$acc5,$a2
	adde	$acc6,$acc6,$t3
	adde	$acc7,$acc7,$a3
___
for($i=0; $i<4; $i++) {			# reductions
$code.=<<___;
	addic	$t0,$acc0,-1		# discarded
	mulhdu	$t1,$ord0,$t4
	mulld	$t2,$ord1,$t4
	mulhdu	$t3,$ord1,$t4

	adde	$t2,$t2,$t1
	addze	$t3,$t3

	addc	$acc0,$acc1,$t2
	adde	$acc1,$acc2,$t3
	adde	$acc2,$acc3,$t4
	adde	$acc3,$zr,$t4		# can't overflow
___
$code.=<<___	if ($i<3);
	mulld	$t3,$acc0,$ordk
___
$code.=<<___;
	sldi	$t0,$t4,32
	subfc	$acc1,$t4,$acc1
	srdi	$t1,$t4,32
	subfe	$acc2,$t0,$acc2
	subfe	$acc3,$t1,$acc3		# can't borrow
___
	($t3,$t4) = ($t4,$t3);
}
$code.=<<___;
	addc	$acc0,$acc0,$acc4	# accumulate upper half
	adde	$acc1,$acc1,$acc5
	adde	$acc2,$acc2,$acc6
	adde	$acc3,$acc3,$acc7
	addze	$acc4,$zr

	subfc	$acc0,$ord0,$acc0	# ret -= modulus
	subfe	$acc1,$ord1,$acc1
	subfe	$acc2,$ord2,$acc2
	subfe	$acc3,$ord3,$acc3
	subfe	$acc4,$zr,$acc4

	and	$t0,$ord0,$acc4
	and	$t1,$ord1,$acc4
	addc	$a0,$acc0,$t0		# ret += modulus if borrow
	and	$t3,$ord3,$acc4
	adde	$a1,$acc1,$t1
	adde	$a2,$acc2,$acc4
	adde	$a3,$acc3,$t3

	bdnz	.Loop_ord_sqr

	std	$a0,0($rp)
	std	$a1,8($rp)
	std	$a2,16($rp)
	std	$a3,24($rp)

	ld	r18,48($sp)
	ld	r19,56($sp)
	ld	r20,64($sp)
	ld	r21,72($sp)
	ld	r22,80($sp)
	ld	r23,88($sp)
	ld	r24,96($sp)
	ld	r25,104($sp)
	ld	r26,112($sp)
	ld	r27,120($sp)
	ld	r28,128($sp)
	ld	r29,136($sp)
	ld	r30,144($sp)
	ld	r31,152($sp)
	addi	$sp,$sp,160
	blr
	.long	0
	.byte	0,12,4,0,0x80,14,3,0
	.long	0
.size	ecp_nistz256_ord_sqr_mont,.-ecp_nistz256_ord_sqr_mont
___
}	}

########################################################################
# scatter-gather subroutines
{
my ($out,$inp,$index,$mask)=map("r$_",(3..7));
$code.=<<___;
########################################################################
# void	ecp_nistz256_scatter_w5(void *out, const P256_POINT *inp,
#				int index);
.globl	ecp_nistz256_scatter_w5
.align	4
ecp_nistz256_scatter_w5:
	slwi	$index,$index,2
	add	$out,$out,$index

	ld	r8, 0($inp)		# X
	ld	r9, 8($inp)
	ld	r10,16($inp)
	ld	r11,24($inp)

	stw	r8, 64*0-4($out)
	srdi	r8, r8, 32
	stw	r9, 64*1-4($out)
	srdi	r9, r9, 32
	stw	r10,64*2-4($out)
	srdi	r10,r10,32
	stw	r11,64*3-4($out)
	srdi	r11,r11,32
	stw	r8, 64*4-4($out)
	stw	r9, 64*5-4($out)
	stw	r10,64*6-4($out)
	stw	r11,64*7-4($out)
	addi	$out,$out,64*8

	ld	r8, 32($inp)		# Y
	ld	r9, 40($inp)
	ld	r10,48($inp)
	ld	r11,56($inp)

	stw	r8, 64*0-4($out)
	srdi	r8, r8, 32
	stw	r9, 64*1-4($out)
	srdi	r9, r9, 32
	stw	r10,64*2-4($out)
	srdi	r10,r10,32
	stw	r11,64*3-4($out)
	srdi	r11,r11,32
	stw	r8, 64*4-4($out)
	stw	r9, 64*5-4($out)
	stw	r10,64*6-4($out)
	stw	r11,64*7-4($out)
	addi	$out,$out,64*8

	ld	r8, 64($inp)		# Z
	ld	r9, 72($inp)
	ld	r10,80($inp)
	ld	r11,88($inp)

	stw	r8, 64*0-4($out)
	srdi	r8, r8, 32
	stw	r9, 64*1-4($out)
	srdi	r9, r9, 32
	stw	r10,64*2-4($out)
	srdi	r10,r10,32
	stw	r11,64*3-4($out)
	srdi	r11,r11,32
	stw	r8, 64*4-4($out)
	stw	r9, 64*5-4($out)
	stw	r10,64*6-4($out)
	stw	r11,64*7-4($out)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	ecp_nistz256_scatter_w5,.-ecp_nistz256_scatter_w5

########################################################################
# void	ecp_nistz256_gather_w5(P256_POINT *out, const void *inp,
#				int index);
.globl	ecp_nistz256_gather_w5
.align	4
ecp_nistz256_gather_w5:
	neg	r0,$index
	sradi	r0,r0,63

	add	$index,$index,r0
	slwi	$index,$index,2
	add	$inp,$inp,$index

	lwz	r5, 64*0($inp)
	lwz	r6, 64*1($inp)
	lwz	r7, 64*2($inp)
	lwz	r8, 64*3($inp)
	lwz	r9, 64*4($inp)
	lwz	r10,64*5($inp)
	lwz	r11,64*6($inp)
	lwz	r12,64*7($inp)
	addi	$inp,$inp,64*8
	sldi	r9, r9, 32
	sldi	r10,r10,32
	sldi	r11,r11,32
	sldi	r12,r12,32
	or	r5,r5,r9
	or	r6,r6,r10
	or	r7,r7,r11
	or	r8,r8,r12
	and	r5,r5,r0
	and	r6,r6,r0
	and	r7,r7,r0
	and	r8,r8,r0
	std	r5,0($out)		# X
	std	r6,8($out)
	std	r7,16($out)
	std	r8,24($out)

	lwz	r5, 64*0($inp)
	lwz	r6, 64*1($inp)
	lwz	r7, 64*2($inp)
	lwz	r8, 64*3($inp)
	lwz	r9, 64*4($inp)
	lwz	r10,64*5($inp)
	lwz	r11,64*6($inp)
	lwz	r12,64*7($inp)
	addi	$inp,$inp,64*8
	sldi	r9, r9, 32
	sldi	r10,r10,32
	sldi	r11,r11,32
	sldi	r12,r12,32
	or	r5,r5,r9
	or	r6,r6,r10
	or	r7,r7,r11
	or	r8,r8,r12
	and	r5,r5,r0
	and	r6,r6,r0
	and	r7,r7,r0
	and	r8,r8,r0
	std	r5,32($out)		# Y
	std	r6,40($out)
	std	r7,48($out)
	std	r8,56($out)

	lwz	r5, 64*0($inp)
	lwz	r6, 64*1($inp)
	lwz	r7, 64*2($inp)
	lwz	r8, 64*3($inp)
	lwz	r9, 64*4($inp)
	lwz	r10,64*5($inp)
	lwz	r11,64*6($inp)
	lwz	r12,64*7($inp)
	sldi	r9, r9, 32
	sldi	r10,r10,32
	sldi	r11,r11,32
	sldi	r12,r12,32
	or	r5,r5,r9
	or	r6,r6,r10
	or	r7,r7,r11
	or	r8,r8,r12
	and	r5,r5,r0
	and	r6,r6,r0
	and	r7,r7,r0
	and	r8,r8,r0
	std	r5,64($out)		# Z
	std	r6,72($out)
	std	r7,80($out)
	std	r8,88($out)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	ecp_nistz256_gather_w5,.-ecp_nistz256_gather_w5

########################################################################
# void	ecp_nistz256_scatter_w7(void *out, const P256_POINT_AFFINE *inp,
#				int index);
.globl	ecp_nistz256_scatter_w7
.align	4
ecp_nistz256_scatter_w7:
	li	r0,8
	mtctr	r0
	add	$out,$out,$index
	subi	$inp,$inp,8

.Loop_scatter_w7:
	ldu	r0,8($inp)
	stb	r0,64*0($out)
	srdi	r0,r0,8
	stb	r0,64*1($out)
	srdi	r0,r0,8
	stb	r0,64*2($out)
	srdi	r0,r0,8
	stb	r0,64*3($out)
	srdi	r0,r0,8
	stb	r0,64*4($out)
	srdi	r0,r0,8
	stb	r0,64*5($out)
	srdi	r0,r0,8
	stb	r0,64*6($out)
	srdi	r0,r0,8
	stb	r0,64*7($out)
	addi	$out,$out,64*8
	bdnz	.Loop_scatter_w7

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	ecp_nistz256_scatter_w7,.-ecp_nistz256_scatter_w7

########################################################################
# void	ecp_nistz256_gather_w7(P256_POINT_AFFINE *out, const void *inp,
#				int index);
.globl	ecp_nistz256_gather_w7
.align	4
ecp_nistz256_gather_w7:
	li	r0,8
	mtctr	r0
	neg	r0,$index
	sradi	r0,r0,63

	add	$index,$index,r0
	add	$inp,$inp,$index
	subi	$out,$out,8

.Loop_gather_w7:
	lbz	r5, 64*0($inp)
	lbz	r6, 64*1($inp)
	lbz	r7, 64*2($inp)
	lbz	r8, 64*3($inp)
	lbz	r9, 64*4($inp)
	lbz	r10,64*5($inp)
	lbz	r11,64*6($inp)
	lbz	r12,64*7($inp)
	addi	$inp,$inp,64*8

	sldi	r6, r6, 8
	sldi	r7, r7, 16
	sldi	r8, r8, 24
	sldi	r9, r9, 32
	sldi	r10,r10,40
	sldi	r11,r11,48
	sldi	r12,r12,56

	or	r5,r5,r6
	or	r7,r7,r8
	or	r9,r9,r10
	or	r11,r11,r12
	or	r5,r5,r7
	or	r9,r9,r11
	or	r5,r5,r9
	and	r5,r5,r0
	stdu	r5,8($out)
	bdnz	.Loop_gather_w7

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	ecp_nistz256_gather_w7,.-ecp_nistz256_gather_w7
___
}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	print $_,"\n";
}
close STDOUT;	# enforce flush

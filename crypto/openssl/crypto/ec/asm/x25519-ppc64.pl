#! /usr/bin/env perl
# Copyright 2018 The OpenSSL Project Authors. All Rights Reserved.
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
# X25519 lower-level primitives for PPC64.
#
# July 2018.
#
# Base 2^64 is faster than base 2^51 on pre-POWER8, most notably ~15%
# faster on PPC970/G5. POWER8 on the other hand seems to trip on own
# shoelaces when handling longer carry chains. As base 2^51 has just
# single-carry pairs, it's 25% faster than base 2^64. Since PPC970 is
# pretty old, base 2^64 implementation is not engaged. Comparison to
# compiler-generated code is complicated by the fact that not all
# compilers support 128-bit integers. When compiler doesn't, like xlc,
# this module delivers more than 2x improvement, and when it does,
# from 12% to 30% improvement was measured...

$flavour = shift;
while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

my $sp = "r1";
my ($rp,$ap,$bp) = map("r$_",3..5);

####################################################### base 2^64
if (0) {
my ($bi,$a0,$a1,$a2,$a3,$t0,$t1, $t2,$t3,
    $acc0,$acc1,$acc2,$acc3,$acc4,$acc5,$acc6,$acc7) =
    map("r$_",(6..12,22..31));
my $zero = "r0";
my $FRAME = 16*8;

$code.=<<___;
.text

.globl	x25519_fe64_mul
.type	x25519_fe64_mul,\@function
.align	5
x25519_fe64_mul:
	stdu	$sp,-$FRAME($sp)
	std	r22,`$FRAME-8*10`($sp)
	std	r23,`$FRAME-8*9`($sp)
	std	r24,`$FRAME-8*8`($sp)
	std	r25,`$FRAME-8*7`($sp)
	std	r26,`$FRAME-8*6`($sp)
	std	r27,`$FRAME-8*5`($sp)
	std	r28,`$FRAME-8*4`($sp)
	std	r29,`$FRAME-8*3`($sp)
	std	r30,`$FRAME-8*2`($sp)
	std	r31,`$FRAME-8*1`($sp)

	ld	$bi,0($bp)
	ld	$a0,0($ap)
	xor	$zero,$zero,$zero
	ld	$a1,8($ap)
	ld	$a2,16($ap)
	ld	$a3,24($ap)

	mulld	$acc0,$a0,$bi		# a[0]*b[0]
	mulhdu	$t0,$a0,$bi
	mulld	$acc1,$a1,$bi		# a[1]*b[0]
	mulhdu	$t1,$a1,$bi
	mulld	$acc2,$a2,$bi		# a[2]*b[0]
	mulhdu	$t2,$a2,$bi
	mulld	$acc3,$a3,$bi		# a[3]*b[0]
	mulhdu	$t3,$a3,$bi
___
for(my @acc=($acc0,$acc1,$acc2,$acc3,$acc4,$acc5,$acc6,$acc7),
    my $i=1; $i<4; shift(@acc), $i++) {
my $acc4 = $i==1? $zero : @acc[4];

$code.=<<___;
	ld	$bi,`8*$i`($bp)
	addc	@acc[1],@acc[1],$t0	# accumulate high parts
	mulld	$t0,$a0,$bi
	adde	@acc[2],@acc[2],$t1
	mulld	$t1,$a1,$bi
	adde	@acc[3],@acc[3],$t2
	mulld	$t2,$a2,$bi
	adde	@acc[4],$acc4,$t3
	mulld	$t3,$a3,$bi
	addc	@acc[1],@acc[1],$t0	# accumulate low parts
	mulhdu	$t0,$a0,$bi
	adde	@acc[2],@acc[2],$t1
	mulhdu	$t1,$a1,$bi
	adde	@acc[3],@acc[3],$t2
	mulhdu	$t2,$a2,$bi
	adde	@acc[4],@acc[4],$t3
	mulhdu	$t3,$a3,$bi
	adde	@acc[5],$zero,$zero
___
}
$code.=<<___;
	li	$bi,38
	addc	$acc4,$acc4,$t0
	mulld	$t0,$acc4,$bi
	adde	$acc5,$acc5,$t1
	mulld	$t1,$acc5,$bi
	adde	$acc6,$acc6,$t2
	mulld	$t2,$acc6,$bi
	adde	$acc7,$acc7,$t3
	mulld	$t3,$acc7,$bi

	addc	$acc0,$acc0,$t0
	mulhdu	$t0,$acc4,$bi
	adde	$acc1,$acc1,$t1
	mulhdu	$t1,$acc5,$bi
	adde	$acc2,$acc2,$t2
	mulhdu	$t2,$acc6,$bi
	adde	$acc3,$acc3,$t3
	mulhdu	$t3,$acc7,$bi
	adde	$acc4,$zero,$zero

	addc	$acc1,$acc1,$t0
	adde	$acc2,$acc2,$t1
	adde	$acc3,$acc3,$t2
	adde	$acc4,$acc4,$t3

	mulld	$acc4,$acc4,$bi

	addc	$acc0,$acc0,$acc4
	addze	$acc1,$acc1
	addze	$acc2,$acc2
	addze	$acc3,$acc3

	subfe	$acc4,$acc4,$acc4	# carry -> ~mask
	std	$acc1,8($rp)
	andc	$acc4,$bi,$acc4
	std	$acc2,16($rp)
	add	$acc0,$acc0,$acc4
	std	$acc3,24($rp)
	std	$acc0,0($rp)

	ld	r22,`$FRAME-8*10`($sp)
	ld	r23,`$FRAME-8*9`($sp)
	ld	r24,`$FRAME-8*8`($sp)
	ld	r25,`$FRAME-8*7`($sp)
	ld	r26,`$FRAME-8*6`($sp)
	ld	r27,`$FRAME-8*5`($sp)
	ld	r28,`$FRAME-8*4`($sp)
	ld	r29,`$FRAME-8*3`($sp)
	ld	r30,`$FRAME-8*2`($sp)
	ld	r31,`$FRAME-8*1`($sp)
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,4,0,0x80,10,3,0
	.long	0
.size	x25519_fe64_mul,.-x25519_fe64_mul

.globl	x25519_fe64_sqr
.type	x25519_fe64_sqr,\@function
.align	5
x25519_fe64_sqr:
	stdu	$sp,-$FRAME($sp)
	std	r22,`$FRAME-8*10`($sp)
	std	r23,`$FRAME-8*9`($sp)
	std	r24,`$FRAME-8*8`($sp)
	std	r25,`$FRAME-8*7`($sp)
	std	r26,`$FRAME-8*6`($sp)
	std	r27,`$FRAME-8*5`($sp)
	std	r28,`$FRAME-8*4`($sp)
	std	r29,`$FRAME-8*3`($sp)
	std	r30,`$FRAME-8*2`($sp)
	std	r31,`$FRAME-8*1`($sp)

	ld	$a0,0($ap)
	xor	$zero,$zero,$zero
	ld	$a1,8($ap)
	ld	$a2,16($ap)
	ld	$a3,24($ap)

	################################
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
	addze	$acc7,$zero

	addc	$acc1,$acc1,$a0		# +a[i]*a[i]
	 li	$bi,38
	adde	$acc2,$acc2,$t1
	adde	$acc3,$acc3,$a1
	adde	$acc4,$acc4,$t2
	adde	$acc5,$acc5,$a2
	adde	$acc6,$acc6,$t3
	adde	$acc7,$acc7,$a3

	mulld	$t0,$acc4,$bi
	mulld	$t1,$acc5,$bi
	mulld	$t2,$acc6,$bi
	mulld	$t3,$acc7,$bi

	addc	$acc0,$acc0,$t0
	mulhdu	$t0,$acc4,$bi
	adde	$acc1,$acc1,$t1
	mulhdu	$t1,$acc5,$bi
	adde	$acc2,$acc2,$t2
	mulhdu	$t2,$acc6,$bi
	adde	$acc3,$acc3,$t3
	mulhdu	$t3,$acc7,$bi
	addze	$acc4,$zero

	addc	$acc1,$acc1,$t0
	adde	$acc2,$acc2,$t1
	adde	$acc3,$acc3,$t2
	adde	$acc4,$acc4,$t3

	mulld	$acc4,$acc4,$bi

	addc	$acc0,$acc0,$acc4
	addze	$acc1,$acc1
	addze	$acc2,$acc2
	addze	$acc3,$acc3

	subfe	$acc4,$acc4,$acc4	# carry -> ~mask
	std	$acc1,8($rp)
	andc	$acc4,$bi,$acc4
	std	$acc2,16($rp)
	add	$acc0,$acc0,$acc4
	std	$acc3,24($rp)
	std	$acc0,0($rp)

	ld	r22,`$FRAME-8*10`($sp)
	ld	r23,`$FRAME-8*9`($sp)
	ld	r24,`$FRAME-8*8`($sp)
	ld	r25,`$FRAME-8*7`($sp)
	ld	r26,`$FRAME-8*6`($sp)
	ld	r27,`$FRAME-8*5`($sp)
	ld	r28,`$FRAME-8*4`($sp)
	ld	r29,`$FRAME-8*3`($sp)
	ld	r30,`$FRAME-8*2`($sp)
	ld	r31,`$FRAME-8*1`($sp)
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,4,0,0x80,10,2,0
	.long	0
.size	x25519_fe64_sqr,.-x25519_fe64_sqr

.globl	x25519_fe64_mul121666
.type	x25519_fe64_mul121666,\@function
.align	5
x25519_fe64_mul121666:
	lis	$bi,`65536>>16`
	ori	$bi,$bi,`121666-65536`

	ld	$t0,0($ap)
	ld	$t1,8($ap)
	ld	$bp,16($ap)
	ld	$ap,24($ap)

	mulld	$a0,$t0,$bi
	mulhdu	$t0,$t0,$bi
	mulld	$a1,$t1,$bi
	mulhdu	$t1,$t1,$bi
	mulld	$a2,$bp,$bi
	mulhdu	$bp,$bp,$bi
	mulld	$a3,$ap,$bi
	mulhdu	$ap,$ap,$bi

	addc	$a1,$a1,$t0
	adde	$a2,$a2,$t1
	adde	$a3,$a3,$bp
	addze	$ap,    $ap

	mulli	$ap,$ap,38

	addc	$a0,$a0,$ap
	addze	$a1,$a1
	addze	$a2,$a2
	addze	$a3,$a3

	subfe	$t1,$t1,$t1		# carry -> ~mask
	std	$a1,8($rp)
	andc	$t0,$t0,$t1
	std	$a2,16($rp)
	add	$a0,$a0,$t0
	std	$a3,24($rp)
	std	$a0,0($rp)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,2,0
	.long	0
.size	x25519_fe64_mul121666,.-x25519_fe64_mul121666

.globl	x25519_fe64_add
.type	x25519_fe64_add,\@function
.align	5
x25519_fe64_add:
	ld	$a0,0($ap)
	ld	$t0,0($bp)
	ld	$a1,8($ap)
	ld	$t1,8($bp)
	ld	$a2,16($ap)
	ld	$bi,16($bp)
	ld	$a3,24($ap)
	ld	$bp,24($bp)

	addc	$a0,$a0,$t0
	adde	$a1,$a1,$t1
	adde	$a2,$a2,$bi
	adde	$a3,$a3,$bp

	li	$t0,38
	subfe	$t1,$t1,$t1		# carry -> ~mask
	andc	$t1,$t0,$t1

	addc	$a0,$a0,$t1
	addze	$a1,$a1
	addze	$a2,$a2
	addze	$a3,$a3

	subfe	$t1,$t1,$t1		# carry -> ~mask
	std	$a1,8($rp)
	andc	$t0,$t0,$t1
	std	$a2,16($rp)
	add	$a0,$a0,$t0
	std	$a3,24($rp)
	std	$a0,0($rp)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	x25519_fe64_add,.-x25519_fe64_add

.globl	x25519_fe64_sub
.type	x25519_fe64_sub,\@function
.align	5
x25519_fe64_sub:
	ld	$a0,0($ap)
	ld	$t0,0($bp)
	ld	$a1,8($ap)
	ld	$t1,8($bp)
	ld	$a2,16($ap)
	ld	$bi,16($bp)
	ld	$a3,24($ap)
	ld	$bp,24($bp)

	subfc	$a0,$t0,$a0
	subfe	$a1,$t1,$a1
	subfe	$a2,$bi,$a2
	subfe	$a3,$bp,$a3

	li	$t0,38
	subfe	$t1,$t1,$t1		# borrow -> mask
	xor	$zero,$zero,$zero
	and	$t1,$t0,$t1

	subfc	$a0,$t1,$a0
	subfe	$a1,$zero,$a1
	subfe	$a2,$zero,$a2
	subfe	$a3,$zero,$a3

	subfe	$t1,$t1,$t1		# borrow -> mask
	std	$a1,8($rp)
	and	$t0,$t0,$t1
	std	$a2,16($rp)
	subf	$a0,$t0,$a0
	std	$a3,24($rp)
	std	$a0,0($rp)

	blr
	.long	0
	.byte	0,12,0x14,0,0,0,3,0
	.long	0
.size	x25519_fe64_sub,.-x25519_fe64_sub

.globl	x25519_fe64_tobytes
.type	x25519_fe64_tobytes,\@function
.align	5
x25519_fe64_tobytes:
	ld	$a3,24($ap)
	ld	$a0,0($ap)
	ld	$a1,8($ap)
	ld	$a2,16($ap)

	sradi	$t0,$a3,63		# most significant bit -> mask
	li	$t1,19
	and	$t0,$t0,$t1
	sldi	$a3,$a3,1
	add	$t0,$t0,$t1		# compare to modulus in the same go
	srdi	$a3,$a3,1		# most signifcant bit cleared

	addc	$a0,$a0,$t0
	addze	$a1,$a1
	addze	$a2,$a2
	addze	$a3,$a3

	xor	$zero,$zero,$zero
	sradi	$t0,$a3,63		# most significant bit -> mask
	sldi	$a3,$a3,1
	andc	$t0,$t1,$t0
	srdi	$a3,$a3,1		# most signifcant bit cleared

	subi	$rp,$rp,1
	subfc	$a0,$t0,$a0
	subfe	$a1,$zero,$a1
	subfe	$a2,$zero,$a2
	subfe	$a3,$zero,$a3

___
for (my @a=($a0,$a1,$a2,$a3), my $i=0; $i<4; shift(@a), $i++) {
$code.=<<___;
	srdi	$t0,@a[0],8
	stbu	@a[0],1($rp)
	srdi	@a[0],@a[0],16
	stbu	$t0,1($rp)
	srdi	$t0,@a[0],8
	stbu	@a[0],1($rp)
	srdi	@a[0],@a[0],16
	stbu	$t0,1($rp)
	srdi	$t0,@a[0],8
	stbu	@a[0],1($rp)
	srdi	@a[0],@a[0],16
	stbu	$t0,1($rp)
	srdi	$t0,@a[0],8
	stbu	@a[0],1($rp)
	stbu	$t0,1($rp)
___
}
$code.=<<___;
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,2,0
	.long	0
.size	x25519_fe64_tobytes,.-x25519_fe64_tobytes
___
}
####################################################### base 2^51
{
my ($bi,$a0,$a1,$a2,$a3,$a4,$t0, $t1,
    $h0lo,$h0hi,$h1lo,$h1hi,$h2lo,$h2hi,$h3lo,$h3hi,$h4lo,$h4hi) =
    map("r$_",(6..12,21..31));
my $mask = "r0";
my $FRAME = 18*8;

$code.=<<___;
.text

.globl	x25519_fe51_mul
.type	x25519_fe51_mul,\@function
.align	5
x25519_fe51_mul:
	stdu	$sp,-$FRAME($sp)
	std	r21,`$FRAME-8*11`($sp)
	std	r22,`$FRAME-8*10`($sp)
	std	r23,`$FRAME-8*9`($sp)
	std	r24,`$FRAME-8*8`($sp)
	std	r25,`$FRAME-8*7`($sp)
	std	r26,`$FRAME-8*6`($sp)
	std	r27,`$FRAME-8*5`($sp)
	std	r28,`$FRAME-8*4`($sp)
	std	r29,`$FRAME-8*3`($sp)
	std	r30,`$FRAME-8*2`($sp)
	std	r31,`$FRAME-8*1`($sp)

	ld	$bi,0($bp)
	ld	$a0,0($ap)
	ld	$a1,8($ap)
	ld	$a2,16($ap)
	ld	$a3,24($ap)
	ld	$a4,32($ap)

	mulld	$h0lo,$a0,$bi		# a[0]*b[0]
	mulhdu	$h0hi,$a0,$bi

	mulld	$h1lo,$a1,$bi		# a[1]*b[0]
	mulhdu	$h1hi,$a1,$bi

	 mulld	$h4lo,$a4,$bi		# a[4]*b[0]
	 mulhdu	$h4hi,$a4,$bi
	 ld	$ap,8($bp)
	 mulli	$a4,$a4,19

	mulld	$h2lo,$a2,$bi		# a[2]*b[0]
	mulhdu	$h2hi,$a2,$bi

	mulld	$h3lo,$a3,$bi		# a[3]*b[0]
	mulhdu	$h3hi,$a3,$bi
___
for(my @a=($a0,$a1,$a2,$a3,$a4),
    my $i=1; $i<4; $i++) {
	($ap,$bi) = ($bi,$ap);
$code.=<<___;
	mulld	$t0,@a[4],$bi
	mulhdu	$t1,@a[4],$bi
	addc	$h0lo,$h0lo,$t0
	adde	$h0hi,$h0hi,$t1

	mulld	$t0,@a[0],$bi
	mulhdu	$t1,@a[0],$bi
	addc	$h1lo,$h1lo,$t0
	adde	$h1hi,$h1hi,$t1

	 mulld	$t0,@a[3],$bi
	 mulhdu	$t1,@a[3],$bi
	 ld	$ap,`8*($i+1)`($bp)
	 mulli	@a[3],@a[3],19
	 addc	$h4lo,$h4lo,$t0
	 adde	$h4hi,$h4hi,$t1

	mulld	$t0,@a[1],$bi
	mulhdu	$t1,@a[1],$bi
	addc	$h2lo,$h2lo,$t0
	adde	$h2hi,$h2hi,$t1

	mulld	$t0,@a[2],$bi
	mulhdu	$t1,@a[2],$bi
	addc	$h3lo,$h3lo,$t0
	adde	$h3hi,$h3hi,$t1
___
	unshift(@a,pop(@a));
}
	($ap,$bi) = ($bi,$ap);
$code.=<<___;
	mulld	$t0,$a1,$bi
	mulhdu	$t1,$a1,$bi
	addc	$h0lo,$h0lo,$t0
	adde	$h0hi,$h0hi,$t1

	mulld	$t0,$a2,$bi
	mulhdu	$t1,$a2,$bi
	addc	$h1lo,$h1lo,$t0
	adde	$h1hi,$h1hi,$t1

	mulld	$t0,$a3,$bi
	mulhdu	$t1,$a3,$bi
	addc	$h2lo,$h2lo,$t0
	adde	$h2hi,$h2hi,$t1

	mulld	$t0,$a4,$bi
	mulhdu	$t1,$a4,$bi
	addc	$h3lo,$h3lo,$t0
	adde	$h3hi,$h3hi,$t1

	mulld	$t0,$a0,$bi
	mulhdu	$t1,$a0,$bi
	addc	$h4lo,$h4lo,$t0
	adde	$h4hi,$h4hi,$t1

.Lfe51_reduce:
	li	$mask,-1
	srdi	$mask,$mask,13		# 0x7ffffffffffff

	srdi	$t0,$h2lo,51
	and	$a2,$h2lo,$mask
	insrdi	$t0,$h2hi,51,0		# h2>>51
	 srdi	$t1,$h0lo,51
	 and	$a0,$h0lo,$mask
	 insrdi	$t1,$h0hi,51,0		# h0>>51
	addc	$h3lo,$h3lo,$t0
	addze	$h3hi,$h3hi
	 addc	$h1lo,$h1lo,$t1
	 addze	$h1hi,$h1hi

	srdi	$t0,$h3lo,51
	and	$a3,$h3lo,$mask
	insrdi	$t0,$h3hi,51,0		# h3>>51
	 srdi	$t1,$h1lo,51
	 and	$a1,$h1lo,$mask
	 insrdi	$t1,$h1hi,51,0		# h1>>51
	addc	$h4lo,$h4lo,$t0
	addze	$h4hi,$h4hi
	 add	$a2,$a2,$t1

	srdi	$t0,$h4lo,51
	and	$a4,$h4lo,$mask
	insrdi	$t0,$h4hi,51,0
	mulli	$t0,$t0,19		# (h4 >> 51) * 19

	add	$a0,$a0,$t0

	srdi	$t1,$a2,51
	and	$a2,$a2,$mask
	add	$a3,$a3,$t1

	srdi	$t0,$a0,51
	and	$a0,$a0,$mask
	add	$a1,$a1,$t0

	std	$a2,16($rp)
	std	$a3,24($rp)
	std	$a4,32($rp)
	std	$a0,0($rp)
	std	$a1,8($rp)

	ld	r21,`$FRAME-8*11`($sp)
	ld	r22,`$FRAME-8*10`($sp)
	ld	r23,`$FRAME-8*9`($sp)
	ld	r24,`$FRAME-8*8`($sp)
	ld	r25,`$FRAME-8*7`($sp)
	ld	r26,`$FRAME-8*6`($sp)
	ld	r27,`$FRAME-8*5`($sp)
	ld	r28,`$FRAME-8*4`($sp)
	ld	r29,`$FRAME-8*3`($sp)
	ld	r30,`$FRAME-8*2`($sp)
	ld	r31,`$FRAME-8*1`($sp)
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,4,0,0x80,11,3,0
	.long	0
.size	x25519_fe51_mul,.-x25519_fe51_mul
___
{
my ($a0,$a1,$a2,$a3,$a4,$t0,$t1) = ($a0,$a1,$a2,$a3,$a4,$t0,$t1);
$code.=<<___;
.globl	x25519_fe51_sqr
.type	x25519_fe51_sqr,\@function
.align	5
x25519_fe51_sqr:
	stdu	$sp,-$FRAME($sp)
	std	r21,`$FRAME-8*11`($sp)
	std	r22,`$FRAME-8*10`($sp)
	std	r23,`$FRAME-8*9`($sp)
	std	r24,`$FRAME-8*8`($sp)
	std	r25,`$FRAME-8*7`($sp)
	std	r26,`$FRAME-8*6`($sp)
	std	r27,`$FRAME-8*5`($sp)
	std	r28,`$FRAME-8*4`($sp)
	std	r29,`$FRAME-8*3`($sp)
	std	r30,`$FRAME-8*2`($sp)
	std	r31,`$FRAME-8*1`($sp)

	ld	$a0,0($ap)
	ld	$a1,8($ap)
	ld	$a2,16($ap)
	ld	$a3,24($ap)
	ld	$a4,32($ap)

	add	$bi,$a0,$a0		# a[0]*2
	mulli	$t1,$a4,19		# a[4]*19

	mulld	$h0lo,$a0,$a0
	mulhdu	$h0hi,$a0,$a0
	mulld	$h1lo,$a1,$bi
	mulhdu	$h1hi,$a1,$bi
	mulld	$h2lo,$a2,$bi
	mulhdu	$h2hi,$a2,$bi
	mulld	$h3lo,$a3,$bi
	mulhdu	$h3hi,$a3,$bi
	mulld	$h4lo,$a4,$bi
	mulhdu	$h4hi,$a4,$bi
	add	$bi,$a1,$a1		# a[1]*2
___
	($a4,$t1) = ($t1,$a4);
$code.=<<___;
	mulld	$t0,$t1,$a4
	mulhdu	$t1,$t1,$a4
	addc	$h3lo,$h3lo,$t0
	adde	$h3hi,$h3hi,$t1

	mulli	$bp,$a3,19		# a[3]*19

	mulld	$t0,$a1,$a1
	mulhdu	$t1,$a1,$a1
	addc	$h2lo,$h2lo,$t0
	adde	$h2hi,$h2hi,$t1
	mulld	$t0,$a2,$bi
	mulhdu	$t1,$a2,$bi
	addc	$h3lo,$h3lo,$t0
	adde	$h3hi,$h3hi,$t1
	mulld	$t0,$a3,$bi
	mulhdu	$t1,$a3,$bi
	addc	$h4lo,$h4lo,$t0
	adde	$h4hi,$h4hi,$t1
	mulld	$t0,$a4,$bi
	mulhdu	$t1,$a4,$bi
	add	$bi,$a3,$a3		# a[3]*2
	addc	$h0lo,$h0lo,$t0
	adde	$h0hi,$h0hi,$t1
___
	($a3,$t1) = ($bp,$a3);
$code.=<<___;
	mulld	$t0,$t1,$a3
	mulhdu	$t1,$t1,$a3
	addc	$h1lo,$h1lo,$t0
	adde	$h1hi,$h1hi,$t1
	mulld	$t0,$bi,$a4
	mulhdu	$t1,$bi,$a4
	add	$bi,$a2,$a2		# a[2]*2
	addc	$h2lo,$h2lo,$t0
	adde	$h2hi,$h2hi,$t1

	mulld	$t0,$a2,$a2
	mulhdu	$t1,$a2,$a2
	addc	$h4lo,$h4lo,$t0
	adde	$h4hi,$h4hi,$t1
	mulld	$t0,$a3,$bi
	mulhdu	$t1,$a3,$bi
	addc	$h0lo,$h0lo,$t0
	adde	$h0hi,$h0hi,$t1
	mulld	$t0,$a4,$bi
	mulhdu	$t1,$a4,$bi
	addc	$h1lo,$h1lo,$t0
	adde	$h1hi,$h1hi,$t1

	b	.Lfe51_reduce
	.long	0
	.byte	0,12,4,0,0x80,11,2,0
	.long	0
.size	x25519_fe51_sqr,.-x25519_fe51_sqr
___
}
$code.=<<___;
.globl	x25519_fe51_mul121666
.type	x25519_fe51_mul121666,\@function
.align	5
x25519_fe51_mul121666:
	stdu	$sp,-$FRAME($sp)
	std	r21,`$FRAME-8*11`($sp)
	std	r22,`$FRAME-8*10`($sp)
	std	r23,`$FRAME-8*9`($sp)
	std	r24,`$FRAME-8*8`($sp)
	std	r25,`$FRAME-8*7`($sp)
	std	r26,`$FRAME-8*6`($sp)
	std	r27,`$FRAME-8*5`($sp)
	std	r28,`$FRAME-8*4`($sp)
	std	r29,`$FRAME-8*3`($sp)
	std	r30,`$FRAME-8*2`($sp)
	std	r31,`$FRAME-8*1`($sp)

	lis	$bi,`65536>>16`
	ori	$bi,$bi,`121666-65536`
	ld	$a0,0($ap)
	ld	$a1,8($ap)
	ld	$a2,16($ap)
	ld	$a3,24($ap)
	ld	$a4,32($ap)

	mulld	$h0lo,$a0,$bi		# a[0]*121666
	mulhdu	$h0hi,$a0,$bi
	mulld	$h1lo,$a1,$bi		# a[1]*121666
	mulhdu	$h1hi,$a1,$bi
	mulld	$h2lo,$a2,$bi		# a[2]*121666
	mulhdu	$h2hi,$a2,$bi
	mulld	$h3lo,$a3,$bi		# a[3]*121666
	mulhdu	$h3hi,$a3,$bi
	mulld	$h4lo,$a4,$bi		# a[4]*121666
	mulhdu	$h4hi,$a4,$bi

	b	.Lfe51_reduce
	.long	0
	.byte	0,12,4,0,0x80,11,2,0
	.long	0
.size	x25519_fe51_mul121666,.-x25519_fe51_mul121666
___
}

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

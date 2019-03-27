#! /usr/bin/env perl
# Copyright 2007-2018 The OpenSSL Project Authors. All Rights Reserved.
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

# April 2007.
#
# Performance improvement over vanilla C code varies from 85% to 45%
# depending on key length and benchmark. Unfortunately in this context
# these are not very impressive results [for code that utilizes "wide"
# 64x64=128-bit multiplication, which is not commonly available to C
# programmers], at least hand-coded bn_asm.c replacement is known to
# provide 30-40% better results for longest keys. Well, on a second
# thought it's not very surprising, because z-CPUs are single-issue
# and _strictly_ in-order execution, while bn_mul_mont is more or less
# dependent on CPU ability to pipe-line instructions and have several
# of them "in-flight" at the same time. I mean while other methods,
# for example Karatsuba, aim to minimize amount of multiplications at
# the cost of other operations increase, bn_mul_mont aim to neatly
# "overlap" multiplications and the other operations [and on most
# platforms even minimize the amount of the other operations, in
# particular references to memory]. But it's possible to improve this
# module performance by implementing dedicated squaring code-path and
# possibly by unrolling loops...

# January 2009.
#
# Reschedule to minimize/avoid Address Generation Interlock hazard,
# make inner loops counter-based.

# November 2010.
#
# Adapt for -m31 build. If kernel supports what's called "highgprs"
# feature on Linux [see /proc/cpuinfo], it's possible to use 64-bit
# instructions and achieve "64-bit" performance even in 31-bit legacy
# application context. The feature is not specific to any particular
# processor, as long as it's "z-CPU". Latter implies that the code
# remains z/Architecture specific. Compatibility with 32-bit BN_ULONG
# is achieved by swapping words after 64-bit loads, follow _dswap-s.
# On z990 it was measured to perform 2.6-2.2 times better than
# compiler-generated code, less for longer keys...

$flavour = shift;

if ($flavour =~ /3[12]/) {
	$SIZE_T=4;
	$g="";
} else {
	$SIZE_T=8;
	$g="g";
}

while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

$stdframe=16*$SIZE_T+4*8;

$mn0="%r0";
$num="%r1";

# int bn_mul_mont(
$rp="%r2";		# BN_ULONG *rp,
$ap="%r3";		# const BN_ULONG *ap,
$bp="%r4";		# const BN_ULONG *bp,
$np="%r5";		# const BN_ULONG *np,
$n0="%r6";		# const BN_ULONG *n0,
#$num="160(%r15)"	# int num);

$bi="%r2";	# zaps rp
$j="%r7";

$ahi="%r8";
$alo="%r9";
$nhi="%r10";
$nlo="%r11";
$AHI="%r12";
$NHI="%r13";
$count="%r14";
$sp="%r15";

$code.=<<___;
.text
.globl	bn_mul_mont
.type	bn_mul_mont,\@function
bn_mul_mont:
	lgf	$num,`$stdframe+$SIZE_T-4`($sp)	# pull $num
	sla	$num,`log($SIZE_T)/log(2)`	# $num to enumerate bytes
	la	$bp,0($num,$bp)

	st${g}	%r2,2*$SIZE_T($sp)

	cghi	$num,16		#
	lghi	%r2,0		#
	blr	%r14		# if($num<16) return 0;
___
$code.=<<___ if ($flavour =~ /3[12]/);
	tmll	$num,4
	bnzr	%r14		# if ($num&1) return 0;
___
$code.=<<___ if ($flavour !~ /3[12]/);
	cghi	$num,96		#
	bhr	%r14		# if($num>96) return 0;
___
$code.=<<___;
	stm${g}	%r3,%r15,3*$SIZE_T($sp)

	lghi	$rp,-$stdframe-8	# leave room for carry bit
	lcgr	$j,$num		# -$num
	lgr	%r0,$sp
	la	$rp,0($rp,$sp)
	la	$sp,0($j,$rp)	# alloca
	st${g}	%r0,0($sp)	# back chain

	sra	$num,3		# restore $num
	la	$bp,0($j,$bp)	# restore $bp
	ahi	$num,-1		# adjust $num for inner loop
	lg	$n0,0($n0)	# pull n0
	_dswap	$n0

	lg	$bi,0($bp)
	_dswap	$bi
	lg	$alo,0($ap)
	_dswap	$alo
	mlgr	$ahi,$bi	# ap[0]*bp[0]
	lgr	$AHI,$ahi

	lgr	$mn0,$alo	# "tp[0]"*n0
	msgr	$mn0,$n0

	lg	$nlo,0($np)	#
	_dswap	$nlo
	mlgr	$nhi,$mn0	# np[0]*m1
	algr	$nlo,$alo	# +="tp[0]"
	lghi	$NHI,0
	alcgr	$NHI,$nhi

	la	$j,8(%r0)	# j=1
	lr	$count,$num

.align	16
.L1st:
	lg	$alo,0($j,$ap)
	_dswap	$alo
	mlgr	$ahi,$bi	# ap[j]*bp[0]
	algr	$alo,$AHI
	lghi	$AHI,0
	alcgr	$AHI,$ahi

	lg	$nlo,0($j,$np)
	_dswap	$nlo
	mlgr	$nhi,$mn0	# np[j]*m1
	algr	$nlo,$NHI
	lghi	$NHI,0
	alcgr	$nhi,$NHI	# +="tp[j]"
	algr	$nlo,$alo
	alcgr	$NHI,$nhi

	stg	$nlo,$stdframe-8($j,$sp)	# tp[j-1]=
	la	$j,8($j)	# j++
	brct	$count,.L1st

	algr	$NHI,$AHI
	lghi	$AHI,0
	alcgr	$AHI,$AHI	# upmost overflow bit
	stg	$NHI,$stdframe-8($j,$sp)
	stg	$AHI,$stdframe($j,$sp)
	la	$bp,8($bp)	# bp++

.Louter:
	lg	$bi,0($bp)	# bp[i]
	_dswap	$bi
	lg	$alo,0($ap)
	_dswap	$alo
	mlgr	$ahi,$bi	# ap[0]*bp[i]
	alg	$alo,$stdframe($sp)	# +=tp[0]
	lghi	$AHI,0
	alcgr	$AHI,$ahi

	lgr	$mn0,$alo
	msgr	$mn0,$n0	# tp[0]*n0

	lg	$nlo,0($np)	# np[0]
	_dswap	$nlo
	mlgr	$nhi,$mn0	# np[0]*m1
	algr	$nlo,$alo	# +="tp[0]"
	lghi	$NHI,0
	alcgr	$NHI,$nhi

	la	$j,8(%r0)	# j=1
	lr	$count,$num

.align	16
.Linner:
	lg	$alo,0($j,$ap)
	_dswap	$alo
	mlgr	$ahi,$bi	# ap[j]*bp[i]
	algr	$alo,$AHI
	lghi	$AHI,0
	alcgr	$ahi,$AHI
	alg	$alo,$stdframe($j,$sp)# +=tp[j]
	alcgr	$AHI,$ahi

	lg	$nlo,0($j,$np)
	_dswap	$nlo
	mlgr	$nhi,$mn0	# np[j]*m1
	algr	$nlo,$NHI
	lghi	$NHI,0
	alcgr	$nhi,$NHI
	algr	$nlo,$alo	# +="tp[j]"
	alcgr	$NHI,$nhi

	stg	$nlo,$stdframe-8($j,$sp)	# tp[j-1]=
	la	$j,8($j)	# j++
	brct	$count,.Linner

	algr	$NHI,$AHI
	lghi	$AHI,0
	alcgr	$AHI,$AHI
	alg	$NHI,$stdframe($j,$sp)# accumulate previous upmost overflow bit
	lghi	$ahi,0
	alcgr	$AHI,$ahi	# new upmost overflow bit
	stg	$NHI,$stdframe-8($j,$sp)
	stg	$AHI,$stdframe($j,$sp)

	la	$bp,8($bp)	# bp++
	cl${g}	$bp,`$stdframe+8+4*$SIZE_T`($j,$sp)	# compare to &bp[num]
	jne	.Louter

	l${g}	$rp,`$stdframe+8+2*$SIZE_T`($j,$sp)	# reincarnate rp
	la	$ap,$stdframe($sp)
	ahi	$num,1		# restore $num, incidentally clears "borrow"

	la	$j,0(%r0)
	lr	$count,$num
.Lsub:	lg	$alo,0($j,$ap)
	lg	$nlo,0($j,$np)
	_dswap	$nlo
	slbgr	$alo,$nlo
	stg	$alo,0($j,$rp)
	la	$j,8($j)
	brct	$count,.Lsub
	lghi	$ahi,0
	slbgr	$AHI,$ahi	# handle upmost carry
	lghi	$NHI,-1
	xgr	$NHI,$AHI

	la	$j,0(%r0)
	lgr	$count,$num
.Lcopy:	lg	$ahi,$stdframe($j,$sp)	# conditional copy
	lg	$alo,0($j,$rp)
	ngr	$ahi,$AHI
	ngr	$alo,$NHI
	ogr	$alo,$ahi
	_dswap	$alo
	stg	$j,$stdframe($j,$sp)	# zap tp
	stg	$alo,0($j,$rp)
	la	$j,8($j)
	brct	$count,.Lcopy

	la	%r1,`$stdframe+8+6*$SIZE_T`($j,$sp)
	lm${g}	%r6,%r15,0(%r1)
	lghi	%r2,1		# signal "processed"
	br	%r14
.size	bn_mul_mont,.-bn_mul_mont
.string	"Montgomery Multiplication for s390x, CRYPTOGAMS by <appro\@openssl.org>"
___

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;
	s/_dswap\s+(%r[0-9]+)/sprintf("rllg\t%s,%s,32",$1,$1) if($SIZE_T==4)/e;
	print $_,"\n";
}
close STDOUT;

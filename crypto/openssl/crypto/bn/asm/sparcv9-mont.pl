#! /usr/bin/env perl
# Copyright 2005-2018 The OpenSSL Project Authors. All Rights Reserved.
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

# December 2005
#
# Pure SPARCv9/8+ and IALU-only bn_mul_mont implementation. The reasons
# for undertaken effort are multiple. First of all, UltraSPARC is not
# the whole SPARCv9 universe and other VIS-free implementations deserve
# optimized code as much. Secondly, newly introduced UltraSPARC T1,
# a.k.a. Niagara, has shared FPU and concurrent FPU-intensive paths,
# such as sparcv9a-mont, will simply sink it. Yes, T1 is equipped with
# several integrated RSA/DSA accelerator circuits accessible through
# kernel driver [only(*)], but having decent user-land software
# implementation is important too. Finally, reasons like desire to
# experiment with dedicated squaring procedure. Yes, this module
# implements one, because it was easiest to draft it in SPARCv9
# instructions...

# (*)	Engine accessing the driver in question is on my TODO list.
#	For reference, accelerator is estimated to give 6 to 10 times
#	improvement on single-threaded RSA sign. It should be noted
#	that 6-10x improvement coefficient does not actually mean
#	something extraordinary in terms of absolute [single-threaded]
#	performance, as SPARCv9 instruction set is by all means least
#	suitable for high performance crypto among other 64 bit
#	platforms. 6-10x factor simply places T1 in same performance
#	domain as say AMD64 and IA-64. Improvement of RSA verify don't
#	appear impressive at all, but it's the sign operation which is
#	far more critical/interesting.

# You might notice that inner loops are modulo-scheduled:-) This has
# essentially negligible impact on UltraSPARC performance, it's
# Fujitsu SPARC64 V users who should notice and hopefully appreciate
# the advantage... Currently this module surpasses sparcv9a-mont.pl
# by ~20% on UltraSPARC-III and later cores, but recall that sparcv9a
# module still have hidden potential [see TODO list there], which is
# estimated to be larger than 20%...

$output = pop;
open STDOUT,">$output";

# int bn_mul_mont(
$rp="%i0";	# BN_ULONG *rp,
$ap="%i1";	# const BN_ULONG *ap,
$bp="%i2";	# const BN_ULONG *bp,
$np="%i3";	# const BN_ULONG *np,
$n0="%i4";	# const BN_ULONG *n0,
$num="%i5";	# int num);

$frame="STACK_FRAME";
$bias="STACK_BIAS";

$car0="%o0";
$car1="%o1";
$car2="%o2";	# 1 bit
$acc0="%o3";
$acc1="%o4";
$mask="%g1";	# 32 bits, what a waste...
$tmp0="%g4";
$tmp1="%g5";

$i="%l0";
$j="%l1";
$mul0="%l2";
$mul1="%l3";
$tp="%l4";
$apj="%l5";
$npj="%l6";
$tpj="%l7";

$fname="bn_mul_mont_int";

$code=<<___;
#include "sparc_arch.h"

.section	".text",#alloc,#execinstr

.global	$fname
.align	32
$fname:
	cmp	%o5,4			! 128 bits minimum
	bge,pt	%icc,.Lenter
	sethi	%hi(0xffffffff),$mask
	retl
	clr	%o0
.align	32
.Lenter:
	save	%sp,-$frame,%sp
	sll	$num,2,$num		! num*=4
	or	$mask,%lo(0xffffffff),$mask
	ld	[$n0],$n0
	cmp	$ap,$bp
	and	$num,$mask,$num
	ld	[$bp],$mul0		! bp[0]
	nop

	add	%sp,$bias,%o7		! real top of stack
	ld	[$ap],$car0		! ap[0] ! redundant in squaring context
	sub	%o7,$num,%o7
	ld	[$ap+4],$apj		! ap[1]
	and	%o7,-1024,%o7
	ld	[$np],$car1		! np[0]
	sub	%o7,$bias,%sp		! alloca
	ld	[$np+4],$npj		! np[1]
	be,pt	SIZE_T_CC,.Lbn_sqr_mont
	mov	12,$j

	mulx	$car0,$mul0,$car0	! ap[0]*bp[0]
	mulx	$apj,$mul0,$tmp0	!prologue! ap[1]*bp[0]
	and	$car0,$mask,$acc0
	add	%sp,$bias+$frame,$tp
	ld	[$ap+8],$apj		!prologue!

	mulx	$n0,$acc0,$mul1		! "t[0]"*n0
	and	$mul1,$mask,$mul1

	mulx	$car1,$mul1,$car1	! np[0]*"t[0]"*n0
	mulx	$npj,$mul1,$acc1	!prologue! np[1]*"t[0]"*n0
	srlx	$car0,32,$car0
	add	$acc0,$car1,$car1
	ld	[$np+8],$npj		!prologue!
	srlx	$car1,32,$car1
	mov	$tmp0,$acc0		!prologue!

.L1st:
	mulx	$apj,$mul0,$tmp0
	mulx	$npj,$mul1,$tmp1
	add	$acc0,$car0,$car0
	ld	[$ap+$j],$apj		! ap[j]
	and	$car0,$mask,$acc0
	add	$acc1,$car1,$car1
	ld	[$np+$j],$npj		! np[j]
	srlx	$car0,32,$car0
	add	$acc0,$car1,$car1
	add	$j,4,$j			! j++
	mov	$tmp0,$acc0
	st	$car1,[$tp]
	cmp	$j,$num
	mov	$tmp1,$acc1
	srlx	$car1,32,$car1
	bl	%icc,.L1st
	add	$tp,4,$tp		! tp++
!.L1st

	mulx	$apj,$mul0,$tmp0	!epilogue!
	mulx	$npj,$mul1,$tmp1
	add	$acc0,$car0,$car0
	and	$car0,$mask,$acc0
	add	$acc1,$car1,$car1
	srlx	$car0,32,$car0
	add	$acc0,$car1,$car1
	st	$car1,[$tp]
	srlx	$car1,32,$car1

	add	$tmp0,$car0,$car0
	and	$car0,$mask,$acc0
	add	$tmp1,$car1,$car1
	srlx	$car0,32,$car0
	add	$acc0,$car1,$car1
	st	$car1,[$tp+4]
	srlx	$car1,32,$car1

	add	$car0,$car1,$car1
	st	$car1,[$tp+8]
	srlx	$car1,32,$car2

	mov	4,$i			! i++
	ld	[$bp+4],$mul0		! bp[1]
.Louter:
	add	%sp,$bias+$frame,$tp
	ld	[$ap],$car0		! ap[0]
	ld	[$ap+4],$apj		! ap[1]
	ld	[$np],$car1		! np[0]
	ld	[$np+4],$npj		! np[1]
	ld	[$tp],$tmp1		! tp[0]
	ld	[$tp+4],$tpj		! tp[1]
	mov	12,$j

	mulx	$car0,$mul0,$car0
	mulx	$apj,$mul0,$tmp0	!prologue!
	add	$tmp1,$car0,$car0
	ld	[$ap+8],$apj		!prologue!
	and	$car0,$mask,$acc0

	mulx	$n0,$acc0,$mul1
	and	$mul1,$mask,$mul1

	mulx	$car1,$mul1,$car1
	mulx	$npj,$mul1,$acc1	!prologue!
	srlx	$car0,32,$car0
	add	$acc0,$car1,$car1
	ld	[$np+8],$npj		!prologue!
	srlx	$car1,32,$car1
	mov	$tmp0,$acc0		!prologue!

.Linner:
	mulx	$apj,$mul0,$tmp0
	mulx	$npj,$mul1,$tmp1
	add	$tpj,$car0,$car0
	ld	[$ap+$j],$apj		! ap[j]
	add	$acc0,$car0,$car0
	add	$acc1,$car1,$car1
	ld	[$np+$j],$npj		! np[j]
	and	$car0,$mask,$acc0
	ld	[$tp+8],$tpj		! tp[j]
	srlx	$car0,32,$car0
	add	$acc0,$car1,$car1
	add	$j,4,$j			! j++
	mov	$tmp0,$acc0
	st	$car1,[$tp]		! tp[j-1]
	srlx	$car1,32,$car1
	mov	$tmp1,$acc1
	cmp	$j,$num
	bl	%icc,.Linner
	add	$tp,4,$tp		! tp++
!.Linner

	mulx	$apj,$mul0,$tmp0	!epilogue!
	mulx	$npj,$mul1,$tmp1
	add	$tpj,$car0,$car0
	add	$acc0,$car0,$car0
	ld	[$tp+8],$tpj		! tp[j]
	and	$car0,$mask,$acc0
	add	$acc1,$car1,$car1
	srlx	$car0,32,$car0
	add	$acc0,$car1,$car1
	st	$car1,[$tp]		! tp[j-1]
	srlx	$car1,32,$car1

	add	$tpj,$car0,$car0
	add	$tmp0,$car0,$car0
	and	$car0,$mask,$acc0
	add	$tmp1,$car1,$car1
	add	$acc0,$car1,$car1
	st	$car1,[$tp+4]		! tp[j-1]
	srlx	$car0,32,$car0
	add	$i,4,$i			! i++
	srlx	$car1,32,$car1

	add	$car0,$car1,$car1
	cmp	$i,$num
	add	$car2,$car1,$car1
	st	$car1,[$tp+8]

	srlx	$car1,32,$car2
	bl,a	%icc,.Louter
	ld	[$bp+$i],$mul0		! bp[i]
!.Louter

	add	$tp,12,$tp

.Ltail:
	add	$np,$num,$np
	add	$rp,$num,$rp
	sub	%g0,$num,%o7		! k=-num
	ba	.Lsub
	subcc	%g0,%g0,%g0		! clear %icc.c
.align	16
.Lsub:
	ld	[$tp+%o7],%o0
	ld	[$np+%o7],%o1
	subccc	%o0,%o1,%o1		! tp[j]-np[j]
	add	$rp,%o7,$i
	add	%o7,4,%o7
	brnz	%o7,.Lsub
	st	%o1,[$i]
	subccc	$car2,0,$car2		! handle upmost overflow bit
	sub	%g0,$num,%o7

.Lcopy:
	ld	[$tp+%o7],%o1		! conditional copy
	ld	[$rp+%o7],%o0
	st	%g0,[$tp+%o7]		! zap tp
	movcs	%icc,%o1,%o0
	st	%o0,[$rp+%o7]
	add	%o7,4,%o7
	brnz	%o7,.Lcopy
	nop
	mov	1,%i0
	ret
	restore
___

########
######## .Lbn_sqr_mont gives up to 20% *overall* improvement over
######## code without following dedicated squaring procedure.
########
$sbit="%o5";

$code.=<<___;
.align	32
.Lbn_sqr_mont:
	mulx	$mul0,$mul0,$car0		! ap[0]*ap[0]
	mulx	$apj,$mul0,$tmp0		!prologue!
	and	$car0,$mask,$acc0
	add	%sp,$bias+$frame,$tp
	ld	[$ap+8],$apj			!prologue!

	mulx	$n0,$acc0,$mul1			! "t[0]"*n0
	srlx	$car0,32,$car0
	and	$mul1,$mask,$mul1

	mulx	$car1,$mul1,$car1		! np[0]*"t[0]"*n0
	mulx	$npj,$mul1,$acc1		!prologue!
	and	$car0,1,$sbit
	ld	[$np+8],$npj			!prologue!
	srlx	$car0,1,$car0
	add	$acc0,$car1,$car1
	srlx	$car1,32,$car1
	mov	$tmp0,$acc0			!prologue!

.Lsqr_1st:
	mulx	$apj,$mul0,$tmp0
	mulx	$npj,$mul1,$tmp1
	add	$acc0,$car0,$car0		! ap[j]*a0+c0
	add	$acc1,$car1,$car1
	ld	[$ap+$j],$apj			! ap[j]
	and	$car0,$mask,$acc0
	ld	[$np+$j],$npj			! np[j]
	srlx	$car0,32,$car0
	add	$acc0,$acc0,$acc0
	or	$sbit,$acc0,$acc0
	mov	$tmp1,$acc1
	srlx	$acc0,32,$sbit
	add	$j,4,$j				! j++
	and	$acc0,$mask,$acc0
	cmp	$j,$num
	add	$acc0,$car1,$car1
	st	$car1,[$tp]
	mov	$tmp0,$acc0
	srlx	$car1,32,$car1
	bl	%icc,.Lsqr_1st
	add	$tp,4,$tp			! tp++
!.Lsqr_1st

	mulx	$apj,$mul0,$tmp0		! epilogue
	mulx	$npj,$mul1,$tmp1
	add	$acc0,$car0,$car0		! ap[j]*a0+c0
	add	$acc1,$car1,$car1
	and	$car0,$mask,$acc0
	srlx	$car0,32,$car0
	add	$acc0,$acc0,$acc0
	or	$sbit,$acc0,$acc0
	srlx	$acc0,32,$sbit
	and	$acc0,$mask,$acc0
	add	$acc0,$car1,$car1
	st	$car1,[$tp]
	srlx	$car1,32,$car1

	add	$tmp0,$car0,$car0		! ap[j]*a0+c0
	add	$tmp1,$car1,$car1
	and	$car0,$mask,$acc0
	srlx	$car0,32,$car0
	add	$acc0,$acc0,$acc0
	or	$sbit,$acc0,$acc0
	srlx	$acc0,32,$sbit
	and	$acc0,$mask,$acc0
	add	$acc0,$car1,$car1
	st	$car1,[$tp+4]
	srlx	$car1,32,$car1

	add	$car0,$car0,$car0
	or	$sbit,$car0,$car0
	add	$car0,$car1,$car1
	st	$car1,[$tp+8]
	srlx	$car1,32,$car2

	ld	[%sp+$bias+$frame],$tmp0	! tp[0]
	ld	[%sp+$bias+$frame+4],$tmp1	! tp[1]
	ld	[%sp+$bias+$frame+8],$tpj	! tp[2]
	ld	[$ap+4],$mul0			! ap[1]
	ld	[$ap+8],$apj			! ap[2]
	ld	[$np],$car1			! np[0]
	ld	[$np+4],$npj			! np[1]
	mulx	$n0,$tmp0,$mul1

	mulx	$mul0,$mul0,$car0
	and	$mul1,$mask,$mul1

	mulx	$car1,$mul1,$car1
	mulx	$npj,$mul1,$acc1
	add	$tmp0,$car1,$car1
	and	$car0,$mask,$acc0
	ld	[$np+8],$npj			! np[2]
	srlx	$car1,32,$car1
	add	$tmp1,$car1,$car1
	srlx	$car0,32,$car0
	add	$acc0,$car1,$car1
	and	$car0,1,$sbit
	add	$acc1,$car1,$car1
	srlx	$car0,1,$car0
	mov	12,$j
	st	$car1,[%sp+$bias+$frame]	! tp[0]=
	srlx	$car1,32,$car1
	add	%sp,$bias+$frame+4,$tp

.Lsqr_2nd:
	mulx	$apj,$mul0,$acc0
	mulx	$npj,$mul1,$acc1
	add	$acc0,$car0,$car0
	add	$tpj,$sbit,$sbit
	ld	[$ap+$j],$apj			! ap[j]
	and	$car0,$mask,$acc0
	ld	[$np+$j],$npj			! np[j]
	srlx	$car0,32,$car0
	add	$acc1,$car1,$car1
	ld	[$tp+8],$tpj			! tp[j]
	add	$acc0,$acc0,$acc0
	add	$j,4,$j				! j++
	add	$sbit,$acc0,$acc0
	srlx	$acc0,32,$sbit
	and	$acc0,$mask,$acc0
	cmp	$j,$num
	add	$acc0,$car1,$car1
	st	$car1,[$tp]			! tp[j-1]
	srlx	$car1,32,$car1
	bl	%icc,.Lsqr_2nd
	add	$tp,4,$tp			! tp++
!.Lsqr_2nd

	mulx	$apj,$mul0,$acc0
	mulx	$npj,$mul1,$acc1
	add	$acc0,$car0,$car0
	add	$tpj,$sbit,$sbit
	and	$car0,$mask,$acc0
	srlx	$car0,32,$car0
	add	$acc1,$car1,$car1
	add	$acc0,$acc0,$acc0
	add	$sbit,$acc0,$acc0
	srlx	$acc0,32,$sbit
	and	$acc0,$mask,$acc0
	add	$acc0,$car1,$car1
	st	$car1,[$tp]			! tp[j-1]
	srlx	$car1,32,$car1

	add	$car0,$car0,$car0
	add	$sbit,$car0,$car0
	add	$car0,$car1,$car1
	add	$car2,$car1,$car1
	st	$car1,[$tp+4]
	srlx	$car1,32,$car2

	ld	[%sp+$bias+$frame],$tmp1	! tp[0]
	ld	[%sp+$bias+$frame+4],$tpj	! tp[1]
	ld	[$ap+8],$mul0			! ap[2]
	ld	[$np],$car1			! np[0]
	ld	[$np+4],$npj			! np[1]
	mulx	$n0,$tmp1,$mul1
	and	$mul1,$mask,$mul1
	mov	8,$i

	mulx	$mul0,$mul0,$car0
	mulx	$car1,$mul1,$car1
	and	$car0,$mask,$acc0
	add	$tmp1,$car1,$car1
	srlx	$car0,32,$car0
	add	%sp,$bias+$frame,$tp
	srlx	$car1,32,$car1
	and	$car0,1,$sbit
	srlx	$car0,1,$car0
	mov	4,$j

.Lsqr_outer:
.Lsqr_inner1:
	mulx	$npj,$mul1,$acc1
	add	$tpj,$car1,$car1
	add	$j,4,$j
	ld	[$tp+8],$tpj
	cmp	$j,$i
	add	$acc1,$car1,$car1
	ld	[$np+$j],$npj
	st	$car1,[$tp]
	srlx	$car1,32,$car1
	bl	%icc,.Lsqr_inner1
	add	$tp,4,$tp
!.Lsqr_inner1

	add	$j,4,$j
	ld	[$ap+$j],$apj			! ap[j]
	mulx	$npj,$mul1,$acc1
	add	$tpj,$car1,$car1
	ld	[$np+$j],$npj			! np[j]
	srlx	$car1,32,$tmp0
	and	$car1,$mask,$car1
	add	$tmp0,$sbit,$sbit
	add	$acc0,$car1,$car1
	ld	[$tp+8],$tpj			! tp[j]
	add	$acc1,$car1,$car1
	st	$car1,[$tp]
	srlx	$car1,32,$car1

	add	$j,4,$j
	cmp	$j,$num
	be,pn	%icc,.Lsqr_no_inner2
	add	$tp,4,$tp

.Lsqr_inner2:
	mulx	$apj,$mul0,$acc0
	mulx	$npj,$mul1,$acc1
	add	$tpj,$sbit,$sbit
	add	$acc0,$car0,$car0
	ld	[$ap+$j],$apj			! ap[j]
	and	$car0,$mask,$acc0
	ld	[$np+$j],$npj			! np[j]
	srlx	$car0,32,$car0
	add	$acc0,$acc0,$acc0
	ld	[$tp+8],$tpj			! tp[j]
	add	$sbit,$acc0,$acc0
	add	$j,4,$j				! j++
	srlx	$acc0,32,$sbit
	and	$acc0,$mask,$acc0
	cmp	$j,$num
	add	$acc0,$car1,$car1
	add	$acc1,$car1,$car1
	st	$car1,[$tp]			! tp[j-1]
	srlx	$car1,32,$car1
	bl	%icc,.Lsqr_inner2
	add	$tp,4,$tp			! tp++

.Lsqr_no_inner2:
	mulx	$apj,$mul0,$acc0
	mulx	$npj,$mul1,$acc1
	add	$tpj,$sbit,$sbit
	add	$acc0,$car0,$car0
	and	$car0,$mask,$acc0
	srlx	$car0,32,$car0
	add	$acc0,$acc0,$acc0
	add	$sbit,$acc0,$acc0
	srlx	$acc0,32,$sbit
	and	$acc0,$mask,$acc0
	add	$acc0,$car1,$car1
	add	$acc1,$car1,$car1
	st	$car1,[$tp]			! tp[j-1]
	srlx	$car1,32,$car1

	add	$car0,$car0,$car0
	add	$sbit,$car0,$car0
	add	$car0,$car1,$car1
	add	$car2,$car1,$car1
	st	$car1,[$tp+4]
	srlx	$car1,32,$car2

	add	$i,4,$i				! i++
	ld	[%sp+$bias+$frame],$tmp1	! tp[0]
	ld	[%sp+$bias+$frame+4],$tpj	! tp[1]
	ld	[$ap+$i],$mul0			! ap[j]
	ld	[$np],$car1			! np[0]
	ld	[$np+4],$npj			! np[1]
	mulx	$n0,$tmp1,$mul1
	and	$mul1,$mask,$mul1
	add	$i,4,$tmp0

	mulx	$mul0,$mul0,$car0
	mulx	$car1,$mul1,$car1
	and	$car0,$mask,$acc0
	add	$tmp1,$car1,$car1
	srlx	$car0,32,$car0
	add	%sp,$bias+$frame,$tp
	srlx	$car1,32,$car1
	and	$car0,1,$sbit
	srlx	$car0,1,$car0

	cmp	$tmp0,$num			! i<num-1
	bl	%icc,.Lsqr_outer
	mov	4,$j

.Lsqr_last:
	mulx	$npj,$mul1,$acc1
	add	$tpj,$car1,$car1
	add	$j,4,$j
	ld	[$tp+8],$tpj
	cmp	$j,$i
	add	$acc1,$car1,$car1
	ld	[$np+$j],$npj
	st	$car1,[$tp]
	srlx	$car1,32,$car1
	bl	%icc,.Lsqr_last
	add	$tp,4,$tp
!.Lsqr_last

	mulx	$npj,$mul1,$acc1
	add	$tpj,$acc0,$acc0
	srlx	$acc0,32,$tmp0
	and	$acc0,$mask,$acc0
	add	$tmp0,$sbit,$sbit
	add	$acc0,$car1,$car1
	add	$acc1,$car1,$car1
	st	$car1,[$tp]
	srlx	$car1,32,$car1

	add	$car0,$car0,$car0		! recover $car0
	add	$sbit,$car0,$car0
	add	$car0,$car1,$car1
	add	$car2,$car1,$car1
	st	$car1,[$tp+4]
	srlx	$car1,32,$car2

	ba	.Ltail
	add	$tp,8,$tp
.type	$fname,#function
.size	$fname,(.-$fname)
.asciz	"Montgomery Multiplication for SPARCv9, CRYPTOGAMS by <appro\@openssl.org>"
.align	32
___
$code =~ s/\`([^\`]*)\`/eval($1)/gem;
print $code;
close STDOUT;

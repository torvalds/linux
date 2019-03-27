#! /usr/bin/env perl
# Copyright 2012-2018 The OpenSSL Project Authors. All Rights Reserved.
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

# October 2012.
#
# SPARCv9 VIS3 Montgomery multiplication procedure suitable for T3 and
# onward. There are three new instructions used here: umulxhi,
# addxc[cc] and initializing store. On T3 RSA private key operations
# are 1.54/1.87/2.11/2.26 times faster for 512/1024/2048/4096-bit key
# lengths. This is without dedicated squaring procedure. On T4
# corresponding coefficients are 1.47/2.10/2.80/2.90x, which is mostly
# for reference purposes, because T4 has dedicated Montgomery
# multiplication and squaring *instructions* that deliver even more.

$output = pop;
open STDOUT,">$output";

$frame = "STACK_FRAME";
$bias = "STACK_BIAS";

$code.=<<___;
#include "sparc_arch.h"

#ifdef	__arch64__
.register	%g2,#scratch
.register	%g3,#scratch
#endif

.section	".text",#alloc,#execinstr
___

($n0,$m0,$m1,$lo0,$hi0, $lo1,$hi1,$aj,$alo,$nj,$nlo,$tj)=
	(map("%g$_",(1..5)),map("%o$_",(0..5,7)));

# int bn_mul_mont(
$rp="%o0";	# BN_ULONG *rp,
$ap="%o1";	# const BN_ULONG *ap,
$bp="%o2";	# const BN_ULONG *bp,
$np="%o3";	# const BN_ULONG *np,
$n0p="%o4";	# const BN_ULONG *n0,
$num="%o5";	# int num);	# caller ensures that num is even
				# and >=6
$code.=<<___;
.globl	bn_mul_mont_vis3
.align	32
bn_mul_mont_vis3:
	add	%sp,	$bias,	%g4	! real top of stack
	sll	$num,	2,	$num	! size in bytes
	add	$num,	63,	%g5
	andn	%g5,	63,	%g5	! buffer size rounded up to 64 bytes
	add	%g5,	%g5,	%g1
	add	%g5,	%g1,	%g1	! 3*buffer size
	sub	%g4,	%g1,	%g1
	andn	%g1,	63,	%g1	! align at 64 byte
	sub	%g1,	$frame,	%g1	! new top of stack
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
#	+-------------------------------+<----- aligned at 64 bytes
#	| __int64 ap[1..0]		|	converted ap[]
#	+-------------------------------+
#	| __int64 np[1..0]		|	converted np[]
#	+-------------------------------+
#	| __int64 ap[3..2]		|
#	.				.
#	.				.
#	+-------------------------------+
($rp,$ap,$bp,$np,$n0p,$num)=map("%i$_",(0..5));
($t0,$t1,$t2,$t3,$cnt,$tp,$bufsz,$anp)=map("%l$_",(0..7));
($ovf,$i)=($t0,$t1);
$code.=<<___;
	ld	[$n0p+0],	$t0	! pull n0[0..1] value
	add	%sp, $bias+$frame, $tp
	ld	[$n0p+4],	$t1
	add	$tp,	%g5,	$anp
	ld	[$bp+0],	$t2	! m0=bp[0]
	sllx	$t1,	32,	$n0
	ld	[$bp+4],	$t3
	or	$t0,	$n0,	$n0
	add	$bp,	8,	$bp

	ld	[$ap+0],	$t0	! ap[0]
	sllx	$t3,	32,	$m0
	ld	[$ap+4],	$t1
	or	$t2,	$m0,	$m0

	ld	[$ap+8],	$t2	! ap[1]
	sllx	$t1,	32,	$aj
	ld	[$ap+12],	$t3
	or	$t0,	$aj,	$aj
	add	$ap,	16,	$ap
	stx	$aj,	[$anp]		! converted ap[0]

	mulx	$aj,	$m0,	$lo0	! ap[0]*bp[0]
	umulxhi	$aj,	$m0,	$hi0

	ld	[$np+0],	$t0	! np[0]
	sllx	$t3,	32,	$aj
	ld	[$np+4],	$t1
	or	$t2,	$aj,	$aj

	ld	[$np+8],	$t2	! np[1]
	sllx	$t1,	32,	$nj
	ld	[$np+12],	$t3
	or	$t0, $nj,	$nj
	add	$np,	16,	$np
	stx	$nj,	[$anp+8]	! converted np[0]

	mulx	$lo0,	$n0,	$m1	! "tp[0]"*n0
	stx	$aj,	[$anp+16]	! converted ap[1]

	mulx	$aj,	$m0,	$alo	! ap[1]*bp[0]
	umulxhi	$aj,	$m0,	$aj	! ahi=aj

	mulx	$nj,	$m1,	$lo1	! np[0]*m1
	umulxhi	$nj,	$m1,	$hi1

	sllx	$t3,	32,	$nj
	or	$t2,	$nj,	$nj
	stx	$nj,	[$anp+24]	! converted np[1]
	add	$anp,	32,	$anp

	addcc	$lo0,	$lo1,	$lo1
	addxc	%g0,	$hi1,	$hi1

	mulx	$nj,	$m1,	$nlo	! np[1]*m1
	umulxhi	$nj,	$m1,	$nj	! nhi=nj

	ba	.L1st
	sub	$num,	24,	$cnt	! cnt=num-3

.align	16
.L1st:
	ld	[$ap+0],	$t0	! ap[j]
	addcc	$alo,	$hi0,	$lo0
	ld	[$ap+4],	$t1
	addxc	$aj,	%g0,	$hi0

	sllx	$t1,	32,	$aj
	add	$ap,	8,	$ap
	or	$t0,	$aj,	$aj
	stx	$aj,	[$anp]		! converted ap[j]

	ld	[$np+0],	$t2	! np[j]
	addcc	$nlo,	$hi1,	$lo1
	ld	[$np+4],	$t3
	addxc	$nj,	%g0,	$hi1	! nhi=nj

	sllx	$t3,	32,	$nj
	add	$np,	8,	$np
	mulx	$aj,	$m0,	$alo	! ap[j]*bp[0]
	or	$t2,	$nj,	$nj
	umulxhi	$aj,	$m0,	$aj	! ahi=aj
	stx	$nj,	[$anp+8]	! converted np[j]
	add	$anp,	16,	$anp	! anp++

	mulx	$nj,	$m1,	$nlo	! np[j]*m1
	addcc	$lo0,	$lo1,	$lo1	! np[j]*m1+ap[j]*bp[0]
	umulxhi	$nj,	$m1,	$nj	! nhi=nj
	addxc	%g0,	$hi1,	$hi1
	stx	$lo1,	[$tp]		! tp[j-1]
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
	stx	$lo1,	[$tp]		! tp[j-1]
	add	$tp,	8,	$tp

	addcc	$hi0,	$hi1,	$hi1
	addxc	%g0,	%g0,	$ovf	! upmost overflow bit
	stx	$hi1,	[$tp]
	add	$tp,	8,	$tp

	ba	.Louter
	sub	$num,	16,	$i	! i=num-2

.align	16
.Louter:
	ld	[$bp+0],	$t2	! m0=bp[i]
	ld	[$bp+4],	$t3

	sub	$anp,	$num,	$anp	! rewind
	sub	$tp,	$num,	$tp
	sub	$anp,	$num,	$anp

	add	$bp,	8,	$bp
	sllx	$t3,	32,	$m0
	ldx	[$anp+0],	$aj	! ap[0]
	or	$t2,	$m0,	$m0
	ldx	[$anp+8],	$nj	! np[0]

	mulx	$aj,	$m0,	$lo0	! ap[0]*bp[i]
	ldx	[$tp],		$tj	! tp[0]
	umulxhi	$aj,	$m0,	$hi0
	ldx	[$anp+16],	$aj	! ap[1]
	addcc	$lo0,	$tj,	$lo0	! ap[0]*bp[i]+tp[0]
	mulx	$aj,	$m0,	$alo	! ap[1]*bp[i]
	addxc	%g0,	$hi0,	$hi0
	mulx	$lo0,	$n0,	$m1	! tp[0]*n0
	umulxhi	$aj,	$m0,	$aj	! ahi=aj
	mulx	$nj,	$m1,	$lo1	! np[0]*m1
	umulxhi	$nj,	$m1,	$hi1
	ldx	[$anp+24],	$nj	! np[1]
	add	$anp,	32,	$anp
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
	ldx	[$anp+0],	$aj	! ap[j]
	addcc	$nlo,	$hi1,	$lo1
	mulx	$aj,	$m0,	$alo	! ap[j]*bp[i]
	addxc	$nj,	%g0,	$hi1	! nhi=nj
	ldx	[$anp+8],	$nj	! np[j]
	add	$anp,	16,	$anp
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

	sub	$anp,	$num,	$anp	! rewind
	sub	$tp,	$num,	$tp
	sub	$anp,	$num,	$anp
	ba	.Lsub
	subcc	$num,	8,	$cnt	! cnt=num-1 and clear CCR.xcc

.align	16
.Lsub:
	ldx	[$tp],		$tj
	add	$tp,	8,	$tp
	ldx	[$anp+8],	$nj
	add	$anp,	16,	$anp
	subccc	$tj,	$nj,	$t2	! tp[j]-np[j]
	srlx	$tj,	32,	$tj
	srlx	$nj,	32,	$nj
	subccc	$tj,	$nj,	$t3
	add	$rp,	8,	$rp
	st	$t2,	[$rp-4]		! reverse order
	st	$t3,	[$rp-8]
	brnz,pt	$cnt,	.Lsub
	sub	$cnt,	8,	$cnt

	sub	$anp,	$num,	$anp	! rewind
	sub	$tp,	$num,	$tp
	sub	$anp,	$num,	$anp
	sub	$rp,	$num,	$rp

	subccc	$ovf,	%g0,	$ovf	! handle upmost overflow bit
	ba	.Lcopy
	sub	$num,	8,	$cnt

.align	16
.Lcopy:					! conditional copy
	ld	[$tp+0],	$t0
	ld	[$tp+4],	$t1
	ld	[$rp+0],	$t2
	ld	[$rp+4],	$t3
	stx	%g0,	[$tp]		! zap
	add	$tp,	8,	$tp
	stx	%g0,	[$anp]		! zap
	stx	%g0,	[$anp+8]
	add	$anp,	16,	$anp
	movcs	%icc,	$t0,	$t2
	movcs	%icc,	$t1,	$t3
	st	$t3,	[$rp+0]		! flip order
	st	$t2,	[$rp+4]
	add	$rp,	8,	$rp
	brnz	$cnt,	.Lcopy
	sub	$cnt,	8,	$cnt

	mov	1,	%o0
	ret
	restore
.type	bn_mul_mont_vis3, #function
.size	bn_mul_mont_vis3, .-bn_mul_mont_vis3
.asciz  "Montgomery Multiplication for SPARCv9 VIS3, CRYPTOGAMS by <appro\@openssl.org>"
.align	4
___

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

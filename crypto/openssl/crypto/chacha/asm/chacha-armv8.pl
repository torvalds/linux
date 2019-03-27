#! /usr/bin/env perl
# Copyright 2016-2019 The OpenSSL Project Authors. All Rights Reserved.
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
# June 2015
#
# ChaCha20 for ARMv8.
#
# Performance in cycles per byte out of large buffer.
#
#			IALU/gcc-4.9    3xNEON+1xIALU	6xNEON+2xIALU
#
# Apple A7		5.50/+49%       3.33            1.70
# Cortex-A53		8.40/+80%       4.72		4.72(*)
# Cortex-A57		8.06/+43%       4.90            4.43(**)
# Denver		4.50/+82%       2.63		2.67(*)
# X-Gene		9.50/+46%       8.82		8.89(*)
# Mongoose		8.00/+44%	3.64		3.25
# Kryo			8.17/+50%	4.83		4.65
#
# (*)	it's expected that doubling interleave factor doesn't help
#	all processors, only those with higher NEON latency and
#	higher instruction issue rate;
# (**)	expected improvement was actually higher;

$flavour=shift;
$output=shift;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/arm-xlate.pl" and -f $xlate) or
die "can't locate arm-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

sub AUTOLOAD()		# thunk [simplified] x86-style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://; $opcode =~ s/_/\./;
  my $arg = pop;
    $arg = "#$arg" if ($arg*1 eq $arg);
    $code .= "\t$opcode\t".join(',',@_,$arg)."\n";
}

my ($out,$inp,$len,$key,$ctr) = map("x$_",(0..4));

my @x=map("x$_",(5..17,19..21));
my @d=map("x$_",(22..28,30));

sub ROUND {
my ($a0,$b0,$c0,$d0)=@_;
my ($a1,$b1,$c1,$d1)=map(($_&~3)+(($_+1)&3),($a0,$b0,$c0,$d0));
my ($a2,$b2,$c2,$d2)=map(($_&~3)+(($_+1)&3),($a1,$b1,$c1,$d1));
my ($a3,$b3,$c3,$d3)=map(($_&~3)+(($_+1)&3),($a2,$b2,$c2,$d2));

    (
	"&add_32	(@x[$a0],@x[$a0],@x[$b0])",
	 "&add_32	(@x[$a1],@x[$a1],@x[$b1])",
	  "&add_32	(@x[$a2],@x[$a2],@x[$b2])",
	   "&add_32	(@x[$a3],@x[$a3],@x[$b3])",
	"&eor_32	(@x[$d0],@x[$d0],@x[$a0])",
	 "&eor_32	(@x[$d1],@x[$d1],@x[$a1])",
	  "&eor_32	(@x[$d2],@x[$d2],@x[$a2])",
	   "&eor_32	(@x[$d3],@x[$d3],@x[$a3])",
	"&ror_32	(@x[$d0],@x[$d0],16)",
	 "&ror_32	(@x[$d1],@x[$d1],16)",
	  "&ror_32	(@x[$d2],@x[$d2],16)",
	   "&ror_32	(@x[$d3],@x[$d3],16)",

	"&add_32	(@x[$c0],@x[$c0],@x[$d0])",
	 "&add_32	(@x[$c1],@x[$c1],@x[$d1])",
	  "&add_32	(@x[$c2],@x[$c2],@x[$d2])",
	   "&add_32	(@x[$c3],@x[$c3],@x[$d3])",
	"&eor_32	(@x[$b0],@x[$b0],@x[$c0])",
	 "&eor_32	(@x[$b1],@x[$b1],@x[$c1])",
	  "&eor_32	(@x[$b2],@x[$b2],@x[$c2])",
	   "&eor_32	(@x[$b3],@x[$b3],@x[$c3])",
	"&ror_32	(@x[$b0],@x[$b0],20)",
	 "&ror_32	(@x[$b1],@x[$b1],20)",
	  "&ror_32	(@x[$b2],@x[$b2],20)",
	   "&ror_32	(@x[$b3],@x[$b3],20)",

	"&add_32	(@x[$a0],@x[$a0],@x[$b0])",
	 "&add_32	(@x[$a1],@x[$a1],@x[$b1])",
	  "&add_32	(@x[$a2],@x[$a2],@x[$b2])",
	   "&add_32	(@x[$a3],@x[$a3],@x[$b3])",
	"&eor_32	(@x[$d0],@x[$d0],@x[$a0])",
	 "&eor_32	(@x[$d1],@x[$d1],@x[$a1])",
	  "&eor_32	(@x[$d2],@x[$d2],@x[$a2])",
	   "&eor_32	(@x[$d3],@x[$d3],@x[$a3])",
	"&ror_32	(@x[$d0],@x[$d0],24)",
	 "&ror_32	(@x[$d1],@x[$d1],24)",
	  "&ror_32	(@x[$d2],@x[$d2],24)",
	   "&ror_32	(@x[$d3],@x[$d3],24)",

	"&add_32	(@x[$c0],@x[$c0],@x[$d0])",
	 "&add_32	(@x[$c1],@x[$c1],@x[$d1])",
	  "&add_32	(@x[$c2],@x[$c2],@x[$d2])",
	   "&add_32	(@x[$c3],@x[$c3],@x[$d3])",
	"&eor_32	(@x[$b0],@x[$b0],@x[$c0])",
	 "&eor_32	(@x[$b1],@x[$b1],@x[$c1])",
	  "&eor_32	(@x[$b2],@x[$b2],@x[$c2])",
	   "&eor_32	(@x[$b3],@x[$b3],@x[$c3])",
	"&ror_32	(@x[$b0],@x[$b0],25)",
	 "&ror_32	(@x[$b1],@x[$b1],25)",
	  "&ror_32	(@x[$b2],@x[$b2],25)",
	   "&ror_32	(@x[$b3],@x[$b3],25)"
    );
}

$code.=<<___;
#include "arm_arch.h"

.text

.extern	OPENSSL_armcap_P

.align	5
.Lsigma:
.quad	0x3320646e61707865,0x6b20657479622d32		// endian-neutral
.Lone:
.long	1,0,0,0
.LOPENSSL_armcap_P:
#ifdef	__ILP32__
.long	OPENSSL_armcap_P-.
#else
.quad	OPENSSL_armcap_P-.
#endif
.asciz	"ChaCha20 for ARMv8, CRYPTOGAMS by <appro\@openssl.org>"

.globl	ChaCha20_ctr32
.type	ChaCha20_ctr32,%function
.align	5
ChaCha20_ctr32:
	cbz	$len,.Labort
	adr	@x[0],.LOPENSSL_armcap_P
	cmp	$len,#192
	b.lo	.Lshort
#ifdef	__ILP32__
	ldrsw	@x[1],[@x[0]]
#else
	ldr	@x[1],[@x[0]]
#endif
	ldr	w17,[@x[1],@x[0]]
	tst	w17,#ARMV7_NEON
	b.ne	ChaCha20_neon

.Lshort:
	.inst	0xd503233f			// paciasp
	stp	x29,x30,[sp,#-96]!
	add	x29,sp,#0

	adr	@x[0],.Lsigma
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]
	sub	sp,sp,#64

	ldp	@d[0],@d[1],[@x[0]]		// load sigma
	ldp	@d[2],@d[3],[$key]		// load key
	ldp	@d[4],@d[5],[$key,#16]
	ldp	@d[6],@d[7],[$ctr]		// load counter
#ifdef	__ARMEB__
	ror	@d[2],@d[2],#32
	ror	@d[3],@d[3],#32
	ror	@d[4],@d[4],#32
	ror	@d[5],@d[5],#32
	ror	@d[6],@d[6],#32
	ror	@d[7],@d[7],#32
#endif

.Loop_outer:
	mov.32	@x[0],@d[0]			// unpack key block
	lsr	@x[1],@d[0],#32
	mov.32	@x[2],@d[1]
	lsr	@x[3],@d[1],#32
	mov.32	@x[4],@d[2]
	lsr	@x[5],@d[2],#32
	mov.32	@x[6],@d[3]
	lsr	@x[7],@d[3],#32
	mov.32	@x[8],@d[4]
	lsr	@x[9],@d[4],#32
	mov.32	@x[10],@d[5]
	lsr	@x[11],@d[5],#32
	mov.32	@x[12],@d[6]
	lsr	@x[13],@d[6],#32
	mov.32	@x[14],@d[7]
	lsr	@x[15],@d[7],#32

	mov	$ctr,#10
	subs	$len,$len,#64
.Loop:
	sub	$ctr,$ctr,#1
___
	foreach (&ROUND(0, 4, 8,12)) { eval; }
	foreach (&ROUND(0, 5,10,15)) { eval; }
$code.=<<___;
	cbnz	$ctr,.Loop

	add.32	@x[0],@x[0],@d[0]		// accumulate key block
	add	@x[1],@x[1],@d[0],lsr#32
	add.32	@x[2],@x[2],@d[1]
	add	@x[3],@x[3],@d[1],lsr#32
	add.32	@x[4],@x[4],@d[2]
	add	@x[5],@x[5],@d[2],lsr#32
	add.32	@x[6],@x[6],@d[3]
	add	@x[7],@x[7],@d[3],lsr#32
	add.32	@x[8],@x[8],@d[4]
	add	@x[9],@x[9],@d[4],lsr#32
	add.32	@x[10],@x[10],@d[5]
	add	@x[11],@x[11],@d[5],lsr#32
	add.32	@x[12],@x[12],@d[6]
	add	@x[13],@x[13],@d[6],lsr#32
	add.32	@x[14],@x[14],@d[7]
	add	@x[15],@x[15],@d[7],lsr#32

	b.lo	.Ltail

	add	@x[0],@x[0],@x[1],lsl#32	// pack
	add	@x[2],@x[2],@x[3],lsl#32
	ldp	@x[1],@x[3],[$inp,#0]		// load input
	add	@x[4],@x[4],@x[5],lsl#32
	add	@x[6],@x[6],@x[7],lsl#32
	ldp	@x[5],@x[7],[$inp,#16]
	add	@x[8],@x[8],@x[9],lsl#32
	add	@x[10],@x[10],@x[11],lsl#32
	ldp	@x[9],@x[11],[$inp,#32]
	add	@x[12],@x[12],@x[13],lsl#32
	add	@x[14],@x[14],@x[15],lsl#32
	ldp	@x[13],@x[15],[$inp,#48]
	add	$inp,$inp,#64
#ifdef	__ARMEB__
	rev	@x[0],@x[0]
	rev	@x[2],@x[2]
	rev	@x[4],@x[4]
	rev	@x[6],@x[6]
	rev	@x[8],@x[8]
	rev	@x[10],@x[10]
	rev	@x[12],@x[12]
	rev	@x[14],@x[14]
#endif
	eor	@x[0],@x[0],@x[1]
	eor	@x[2],@x[2],@x[3]
	eor	@x[4],@x[4],@x[5]
	eor	@x[6],@x[6],@x[7]
	eor	@x[8],@x[8],@x[9]
	eor	@x[10],@x[10],@x[11]
	eor	@x[12],@x[12],@x[13]
	eor	@x[14],@x[14],@x[15]

	stp	@x[0],@x[2],[$out,#0]		// store output
	 add	@d[6],@d[6],#1			// increment counter
	stp	@x[4],@x[6],[$out,#16]
	stp	@x[8],@x[10],[$out,#32]
	stp	@x[12],@x[14],[$out,#48]
	add	$out,$out,#64

	b.hi	.Loop_outer

	ldp	x19,x20,[x29,#16]
	add	sp,sp,#64
	ldp	x21,x22,[x29,#32]
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldp	x29,x30,[sp],#96
	.inst	0xd50323bf			// autiasp
.Labort:
	ret

.align	4
.Ltail:
	add	$len,$len,#64
.Less_than_64:
	sub	$out,$out,#1
	add	$inp,$inp,$len
	add	$out,$out,$len
	add	$ctr,sp,$len
	neg	$len,$len

	add	@x[0],@x[0],@x[1],lsl#32	// pack
	add	@x[2],@x[2],@x[3],lsl#32
	add	@x[4],@x[4],@x[5],lsl#32
	add	@x[6],@x[6],@x[7],lsl#32
	add	@x[8],@x[8],@x[9],lsl#32
	add	@x[10],@x[10],@x[11],lsl#32
	add	@x[12],@x[12],@x[13],lsl#32
	add	@x[14],@x[14],@x[15],lsl#32
#ifdef	__ARMEB__
	rev	@x[0],@x[0]
	rev	@x[2],@x[2]
	rev	@x[4],@x[4]
	rev	@x[6],@x[6]
	rev	@x[8],@x[8]
	rev	@x[10],@x[10]
	rev	@x[12],@x[12]
	rev	@x[14],@x[14]
#endif
	stp	@x[0],@x[2],[sp,#0]
	stp	@x[4],@x[6],[sp,#16]
	stp	@x[8],@x[10],[sp,#32]
	stp	@x[12],@x[14],[sp,#48]

.Loop_tail:
	ldrb	w10,[$inp,$len]
	ldrb	w11,[$ctr,$len]
	add	$len,$len,#1
	eor	w10,w10,w11
	strb	w10,[$out,$len]
	cbnz	$len,.Loop_tail

	stp	xzr,xzr,[sp,#0]
	stp	xzr,xzr,[sp,#16]
	stp	xzr,xzr,[sp,#32]
	stp	xzr,xzr,[sp,#48]

	ldp	x19,x20,[x29,#16]
	add	sp,sp,#64
	ldp	x21,x22,[x29,#32]
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldp	x29,x30,[sp],#96
	.inst	0xd50323bf			// autiasp
	ret
.size	ChaCha20_ctr32,.-ChaCha20_ctr32
___

{{{
my ($A0,$B0,$C0,$D0,$A1,$B1,$C1,$D1,$A2,$B2,$C2,$D2,$T0,$T1,$T2,$T3) =
    map("v$_.4s",(0..7,16..23));
my (@K)=map("v$_.4s",(24..30));
my $ONE="v31.4s";

sub NEONROUND {
my $odd = pop;
my ($a,$b,$c,$d,$t)=@_;

	(
	"&add		('$a','$a','$b')",
	"&eor		('$d','$d','$a')",
	"&rev32_16	('$d','$d')",		# vrot ($d,16)

	"&add		('$c','$c','$d')",
	"&eor		('$t','$b','$c')",
	"&ushr		('$b','$t',20)",
	"&sli		('$b','$t',12)",

	"&add		('$a','$a','$b')",
	"&eor		('$t','$d','$a')",
	"&ushr		('$d','$t',24)",
	"&sli		('$d','$t',8)",

	"&add		('$c','$c','$d')",
	"&eor		('$t','$b','$c')",
	"&ushr		('$b','$t',25)",
	"&sli		('$b','$t',7)",

	"&ext		('$c','$c','$c',8)",
	"&ext		('$d','$d','$d',$odd?4:12)",
	"&ext		('$b','$b','$b',$odd?12:4)"
	);
}

$code.=<<___;

.type	ChaCha20_neon,%function
.align	5
ChaCha20_neon:
	.inst	0xd503233f			// paciasp
	stp	x29,x30,[sp,#-96]!
	add	x29,sp,#0

	adr	@x[0],.Lsigma
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]
	cmp	$len,#512
	b.hs	.L512_or_more_neon

	sub	sp,sp,#64

	ldp	@d[0],@d[1],[@x[0]]		// load sigma
	ld1	{@K[0]},[@x[0]],#16
	ldp	@d[2],@d[3],[$key]		// load key
	ldp	@d[4],@d[5],[$key,#16]
	ld1	{@K[1],@K[2]},[$key]
	ldp	@d[6],@d[7],[$ctr]		// load counter
	ld1	{@K[3]},[$ctr]
	ld1	{$ONE},[@x[0]]
#ifdef	__ARMEB__
	rev64	@K[0],@K[0]
	ror	@d[2],@d[2],#32
	ror	@d[3],@d[3],#32
	ror	@d[4],@d[4],#32
	ror	@d[5],@d[5],#32
	ror	@d[6],@d[6],#32
	ror	@d[7],@d[7],#32
#endif
	add	@K[3],@K[3],$ONE		// += 1
	add	@K[4],@K[3],$ONE
	add	@K[5],@K[4],$ONE
	shl	$ONE,$ONE,#2			// 1 -> 4

.Loop_outer_neon:
	mov.32	@x[0],@d[0]			// unpack key block
	lsr	@x[1],@d[0],#32
	 mov	$A0,@K[0]
	mov.32	@x[2],@d[1]
	lsr	@x[3],@d[1],#32
	 mov	$A1,@K[0]
	mov.32	@x[4],@d[2]
	lsr	@x[5],@d[2],#32
	 mov	$A2,@K[0]
	mov.32	@x[6],@d[3]
	 mov	$B0,@K[1]
	lsr	@x[7],@d[3],#32
	 mov	$B1,@K[1]
	mov.32	@x[8],@d[4]
	 mov	$B2,@K[1]
	lsr	@x[9],@d[4],#32
	 mov	$D0,@K[3]
	mov.32	@x[10],@d[5]
	 mov	$D1,@K[4]
	lsr	@x[11],@d[5],#32
	 mov	$D2,@K[5]
	mov.32	@x[12],@d[6]
	 mov	$C0,@K[2]
	lsr	@x[13],@d[6],#32
	 mov	$C1,@K[2]
	mov.32	@x[14],@d[7]
	 mov	$C2,@K[2]
	lsr	@x[15],@d[7],#32

	mov	$ctr,#10
	subs	$len,$len,#256
.Loop_neon:
	sub	$ctr,$ctr,#1
___
	my @thread0=&NEONROUND($A0,$B0,$C0,$D0,$T0,0);
	my @thread1=&NEONROUND($A1,$B1,$C1,$D1,$T1,0);
	my @thread2=&NEONROUND($A2,$B2,$C2,$D2,$T2,0);
	my @thread3=&ROUND(0,4,8,12);

	foreach (@thread0) {
		eval;			eval(shift(@thread3));
		eval(shift(@thread1));	eval(shift(@thread3));
		eval(shift(@thread2));	eval(shift(@thread3));
	}

	@thread0=&NEONROUND($A0,$B0,$C0,$D0,$T0,1);
	@thread1=&NEONROUND($A1,$B1,$C1,$D1,$T1,1);
	@thread2=&NEONROUND($A2,$B2,$C2,$D2,$T2,1);
	@thread3=&ROUND(0,5,10,15);

	foreach (@thread0) {
		eval;			eval(shift(@thread3));
		eval(shift(@thread1));	eval(shift(@thread3));
		eval(shift(@thread2));	eval(shift(@thread3));
	}
$code.=<<___;
	cbnz	$ctr,.Loop_neon

	add.32	@x[0],@x[0],@d[0]		// accumulate key block
	 add	$A0,$A0,@K[0]
	add	@x[1],@x[1],@d[0],lsr#32
	 add	$A1,$A1,@K[0]
	add.32	@x[2],@x[2],@d[1]
	 add	$A2,$A2,@K[0]
	add	@x[3],@x[3],@d[1],lsr#32
	 add	$C0,$C0,@K[2]
	add.32	@x[4],@x[4],@d[2]
	 add	$C1,$C1,@K[2]
	add	@x[5],@x[5],@d[2],lsr#32
	 add	$C2,$C2,@K[2]
	add.32	@x[6],@x[6],@d[3]
	 add	$D0,$D0,@K[3]
	add	@x[7],@x[7],@d[3],lsr#32
	add.32	@x[8],@x[8],@d[4]
	 add	$D1,$D1,@K[4]
	add	@x[9],@x[9],@d[4],lsr#32
	add.32	@x[10],@x[10],@d[5]
	 add	$D2,$D2,@K[5]
	add	@x[11],@x[11],@d[5],lsr#32
	add.32	@x[12],@x[12],@d[6]
	 add	$B0,$B0,@K[1]
	add	@x[13],@x[13],@d[6],lsr#32
	add.32	@x[14],@x[14],@d[7]
	 add	$B1,$B1,@K[1]
	add	@x[15],@x[15],@d[7],lsr#32
	 add	$B2,$B2,@K[1]

	b.lo	.Ltail_neon

	add	@x[0],@x[0],@x[1],lsl#32	// pack
	add	@x[2],@x[2],@x[3],lsl#32
	ldp	@x[1],@x[3],[$inp,#0]		// load input
	add	@x[4],@x[4],@x[5],lsl#32
	add	@x[6],@x[6],@x[7],lsl#32
	ldp	@x[5],@x[7],[$inp,#16]
	add	@x[8],@x[8],@x[9],lsl#32
	add	@x[10],@x[10],@x[11],lsl#32
	ldp	@x[9],@x[11],[$inp,#32]
	add	@x[12],@x[12],@x[13],lsl#32
	add	@x[14],@x[14],@x[15],lsl#32
	ldp	@x[13],@x[15],[$inp,#48]
	add	$inp,$inp,#64
#ifdef	__ARMEB__
	rev	@x[0],@x[0]
	rev	@x[2],@x[2]
	rev	@x[4],@x[4]
	rev	@x[6],@x[6]
	rev	@x[8],@x[8]
	rev	@x[10],@x[10]
	rev	@x[12],@x[12]
	rev	@x[14],@x[14]
#endif
	ld1.8	{$T0-$T3},[$inp],#64
	eor	@x[0],@x[0],@x[1]
	eor	@x[2],@x[2],@x[3]
	eor	@x[4],@x[4],@x[5]
	eor	@x[6],@x[6],@x[7]
	eor	@x[8],@x[8],@x[9]
	 eor	$A0,$A0,$T0
	eor	@x[10],@x[10],@x[11]
	 eor	$B0,$B0,$T1
	eor	@x[12],@x[12],@x[13]
	 eor	$C0,$C0,$T2
	eor	@x[14],@x[14],@x[15]
	 eor	$D0,$D0,$T3
	 ld1.8	{$T0-$T3},[$inp],#64

	stp	@x[0],@x[2],[$out,#0]		// store output
	 add	@d[6],@d[6],#4			// increment counter
	stp	@x[4],@x[6],[$out,#16]
	 add	@K[3],@K[3],$ONE		// += 4
	stp	@x[8],@x[10],[$out,#32]
	 add	@K[4],@K[4],$ONE
	stp	@x[12],@x[14],[$out,#48]
	 add	@K[5],@K[5],$ONE
	add	$out,$out,#64

	st1.8	{$A0-$D0},[$out],#64
	ld1.8	{$A0-$D0},[$inp],#64

	eor	$A1,$A1,$T0
	eor	$B1,$B1,$T1
	eor	$C1,$C1,$T2
	eor	$D1,$D1,$T3
	st1.8	{$A1-$D1},[$out],#64

	eor	$A2,$A2,$A0
	eor	$B2,$B2,$B0
	eor	$C2,$C2,$C0
	eor	$D2,$D2,$D0
	st1.8	{$A2-$D2},[$out],#64

	b.hi	.Loop_outer_neon

	ldp	x19,x20,[x29,#16]
	add	sp,sp,#64
	ldp	x21,x22,[x29,#32]
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldp	x29,x30,[sp],#96
	.inst	0xd50323bf			// autiasp
	ret

.Ltail_neon:
	add	$len,$len,#256
	cmp	$len,#64
	b.lo	.Less_than_64

	add	@x[0],@x[0],@x[1],lsl#32	// pack
	add	@x[2],@x[2],@x[3],lsl#32
	ldp	@x[1],@x[3],[$inp,#0]		// load input
	add	@x[4],@x[4],@x[5],lsl#32
	add	@x[6],@x[6],@x[7],lsl#32
	ldp	@x[5],@x[7],[$inp,#16]
	add	@x[8],@x[8],@x[9],lsl#32
	add	@x[10],@x[10],@x[11],lsl#32
	ldp	@x[9],@x[11],[$inp,#32]
	add	@x[12],@x[12],@x[13],lsl#32
	add	@x[14],@x[14],@x[15],lsl#32
	ldp	@x[13],@x[15],[$inp,#48]
	add	$inp,$inp,#64
#ifdef	__ARMEB__
	rev	@x[0],@x[0]
	rev	@x[2],@x[2]
	rev	@x[4],@x[4]
	rev	@x[6],@x[6]
	rev	@x[8],@x[8]
	rev	@x[10],@x[10]
	rev	@x[12],@x[12]
	rev	@x[14],@x[14]
#endif
	eor	@x[0],@x[0],@x[1]
	eor	@x[2],@x[2],@x[3]
	eor	@x[4],@x[4],@x[5]
	eor	@x[6],@x[6],@x[7]
	eor	@x[8],@x[8],@x[9]
	eor	@x[10],@x[10],@x[11]
	eor	@x[12],@x[12],@x[13]
	eor	@x[14],@x[14],@x[15]

	stp	@x[0],@x[2],[$out,#0]		// store output
	 add	@d[6],@d[6],#4			// increment counter
	stp	@x[4],@x[6],[$out,#16]
	stp	@x[8],@x[10],[$out,#32]
	stp	@x[12],@x[14],[$out,#48]
	add	$out,$out,#64
	b.eq	.Ldone_neon
	sub	$len,$len,#64
	cmp	$len,#64
	b.lo	.Less_than_128

	ld1.8	{$T0-$T3},[$inp],#64
	eor	$A0,$A0,$T0
	eor	$B0,$B0,$T1
	eor	$C0,$C0,$T2
	eor	$D0,$D0,$T3
	st1.8	{$A0-$D0},[$out],#64
	b.eq	.Ldone_neon
	sub	$len,$len,#64
	cmp	$len,#64
	b.lo	.Less_than_192

	ld1.8	{$T0-$T3},[$inp],#64
	eor	$A1,$A1,$T0
	eor	$B1,$B1,$T1
	eor	$C1,$C1,$T2
	eor	$D1,$D1,$T3
	st1.8	{$A1-$D1},[$out],#64
	b.eq	.Ldone_neon
	sub	$len,$len,#64

	st1.8	{$A2-$D2},[sp]
	b	.Last_neon

.Less_than_128:
	st1.8	{$A0-$D0},[sp]
	b	.Last_neon
.Less_than_192:
	st1.8	{$A1-$D1},[sp]
	b	.Last_neon

.align	4
.Last_neon:
	sub	$out,$out,#1
	add	$inp,$inp,$len
	add	$out,$out,$len
	add	$ctr,sp,$len
	neg	$len,$len

.Loop_tail_neon:
	ldrb	w10,[$inp,$len]
	ldrb	w11,[$ctr,$len]
	add	$len,$len,#1
	eor	w10,w10,w11
	strb	w10,[$out,$len]
	cbnz	$len,.Loop_tail_neon

	stp	xzr,xzr,[sp,#0]
	stp	xzr,xzr,[sp,#16]
	stp	xzr,xzr,[sp,#32]
	stp	xzr,xzr,[sp,#48]

.Ldone_neon:
	ldp	x19,x20,[x29,#16]
	add	sp,sp,#64
	ldp	x21,x22,[x29,#32]
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldp	x29,x30,[sp],#96
	.inst	0xd50323bf			// autiasp
	ret
.size	ChaCha20_neon,.-ChaCha20_neon
___
{
my ($T0,$T1,$T2,$T3,$T4,$T5)=@K;
my ($A0,$B0,$C0,$D0,$A1,$B1,$C1,$D1,$A2,$B2,$C2,$D2,
    $A3,$B3,$C3,$D3,$A4,$B4,$C4,$D4,$A5,$B5,$C5,$D5) = map("v$_.4s",(0..23));

$code.=<<___;
.type	ChaCha20_512_neon,%function
.align	5
ChaCha20_512_neon:
	.inst	0xd503233f			// paciasp
	stp	x29,x30,[sp,#-96]!
	add	x29,sp,#0

	adr	@x[0],.Lsigma
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]

.L512_or_more_neon:
	sub	sp,sp,#128+64

	ldp	@d[0],@d[1],[@x[0]]		// load sigma
	ld1	{@K[0]},[@x[0]],#16
	ldp	@d[2],@d[3],[$key]		// load key
	ldp	@d[4],@d[5],[$key,#16]
	ld1	{@K[1],@K[2]},[$key]
	ldp	@d[6],@d[7],[$ctr]		// load counter
	ld1	{@K[3]},[$ctr]
	ld1	{$ONE},[@x[0]]
#ifdef	__ARMEB__
	rev64	@K[0],@K[0]
	ror	@d[2],@d[2],#32
	ror	@d[3],@d[3],#32
	ror	@d[4],@d[4],#32
	ror	@d[5],@d[5],#32
	ror	@d[6],@d[6],#32
	ror	@d[7],@d[7],#32
#endif
	add	@K[3],@K[3],$ONE		// += 1
	stp	@K[0],@K[1],[sp,#0]		// off-load key block, invariant part
	add	@K[3],@K[3],$ONE		// not typo
	str	@K[2],[sp,#32]
	add	@K[4],@K[3],$ONE
	add	@K[5],@K[4],$ONE
	add	@K[6],@K[5],$ONE
	shl	$ONE,$ONE,#2			// 1 -> 4

	stp	d8,d9,[sp,#128+0]		// meet ABI requirements
	stp	d10,d11,[sp,#128+16]
	stp	d12,d13,[sp,#128+32]
	stp	d14,d15,[sp,#128+48]

	sub	$len,$len,#512			// not typo

.Loop_outer_512_neon:
	 mov	$A0,@K[0]
	 mov	$A1,@K[0]
	 mov	$A2,@K[0]
	 mov	$A3,@K[0]
	 mov	$A4,@K[0]
	 mov	$A5,@K[0]
	 mov	$B0,@K[1]
	mov.32	@x[0],@d[0]			// unpack key block
	 mov	$B1,@K[1]
	lsr	@x[1],@d[0],#32
	 mov	$B2,@K[1]
	mov.32	@x[2],@d[1]
	 mov	$B3,@K[1]
	lsr	@x[3],@d[1],#32
	 mov	$B4,@K[1]
	mov.32	@x[4],@d[2]
	 mov	$B5,@K[1]
	lsr	@x[5],@d[2],#32
	 mov	$D0,@K[3]
	mov.32	@x[6],@d[3]
	 mov	$D1,@K[4]
	lsr	@x[7],@d[3],#32
	 mov	$D2,@K[5]
	mov.32	@x[8],@d[4]
	 mov	$D3,@K[6]
	lsr	@x[9],@d[4],#32
	 mov	$C0,@K[2]
	mov.32	@x[10],@d[5]
	 mov	$C1,@K[2]
	lsr	@x[11],@d[5],#32
	 add	$D4,$D0,$ONE			// +4
	mov.32	@x[12],@d[6]
	 add	$D5,$D1,$ONE			// +4
	lsr	@x[13],@d[6],#32
	 mov	$C2,@K[2]
	mov.32	@x[14],@d[7]
	 mov	$C3,@K[2]
	lsr	@x[15],@d[7],#32
	 mov	$C4,@K[2]
	 stp	@K[3],@K[4],[sp,#48]		// off-load key block, variable part
	 mov	$C5,@K[2]
	 str	@K[5],[sp,#80]

	mov	$ctr,#5
	subs	$len,$len,#512
.Loop_upper_neon:
	sub	$ctr,$ctr,#1
___
	my @thread0=&NEONROUND($A0,$B0,$C0,$D0,$T0,0);
	my @thread1=&NEONROUND($A1,$B1,$C1,$D1,$T1,0);
	my @thread2=&NEONROUND($A2,$B2,$C2,$D2,$T2,0);
	my @thread3=&NEONROUND($A3,$B3,$C3,$D3,$T3,0);
	my @thread4=&NEONROUND($A4,$B4,$C4,$D4,$T4,0);
	my @thread5=&NEONROUND($A5,$B5,$C5,$D5,$T5,0);
	my @thread67=(&ROUND(0,4,8,12),&ROUND(0,5,10,15));
	my $diff = ($#thread0+1)*6 - $#thread67 - 1;
	my $i = 0;

	foreach (@thread0) {
		eval;			eval(shift(@thread67));
		eval(shift(@thread1));	eval(shift(@thread67));
		eval(shift(@thread2));	eval(shift(@thread67));
		eval(shift(@thread3));	eval(shift(@thread67));
		eval(shift(@thread4));	eval(shift(@thread67));
		eval(shift(@thread5));	eval(shift(@thread67));
	}

	@thread0=&NEONROUND($A0,$B0,$C0,$D0,$T0,1);
	@thread1=&NEONROUND($A1,$B1,$C1,$D1,$T1,1);
	@thread2=&NEONROUND($A2,$B2,$C2,$D2,$T2,1);
	@thread3=&NEONROUND($A3,$B3,$C3,$D3,$T3,1);
	@thread4=&NEONROUND($A4,$B4,$C4,$D4,$T4,1);
	@thread5=&NEONROUND($A5,$B5,$C5,$D5,$T5,1);
	@thread67=(&ROUND(0,4,8,12),&ROUND(0,5,10,15));

	foreach (@thread0) {
		eval;			eval(shift(@thread67));
		eval(shift(@thread1));	eval(shift(@thread67));
		eval(shift(@thread2));	eval(shift(@thread67));
		eval(shift(@thread3));	eval(shift(@thread67));
		eval(shift(@thread4));	eval(shift(@thread67));
		eval(shift(@thread5));	eval(shift(@thread67));
	}
$code.=<<___;
	cbnz	$ctr,.Loop_upper_neon

	add.32	@x[0],@x[0],@d[0]		// accumulate key block
	add	@x[1],@x[1],@d[0],lsr#32
	add.32	@x[2],@x[2],@d[1]
	add	@x[3],@x[3],@d[1],lsr#32
	add.32	@x[4],@x[4],@d[2]
	add	@x[5],@x[5],@d[2],lsr#32
	add.32	@x[6],@x[6],@d[3]
	add	@x[7],@x[7],@d[3],lsr#32
	add.32	@x[8],@x[8],@d[4]
	add	@x[9],@x[9],@d[4],lsr#32
	add.32	@x[10],@x[10],@d[5]
	add	@x[11],@x[11],@d[5],lsr#32
	add.32	@x[12],@x[12],@d[6]
	add	@x[13],@x[13],@d[6],lsr#32
	add.32	@x[14],@x[14],@d[7]
	add	@x[15],@x[15],@d[7],lsr#32

	add	@x[0],@x[0],@x[1],lsl#32	// pack
	add	@x[2],@x[2],@x[3],lsl#32
	ldp	@x[1],@x[3],[$inp,#0]		// load input
	add	@x[4],@x[4],@x[5],lsl#32
	add	@x[6],@x[6],@x[7],lsl#32
	ldp	@x[5],@x[7],[$inp,#16]
	add	@x[8],@x[8],@x[9],lsl#32
	add	@x[10],@x[10],@x[11],lsl#32
	ldp	@x[9],@x[11],[$inp,#32]
	add	@x[12],@x[12],@x[13],lsl#32
	add	@x[14],@x[14],@x[15],lsl#32
	ldp	@x[13],@x[15],[$inp,#48]
	add	$inp,$inp,#64
#ifdef	__ARMEB__
	rev	@x[0],@x[0]
	rev	@x[2],@x[2]
	rev	@x[4],@x[4]
	rev	@x[6],@x[6]
	rev	@x[8],@x[8]
	rev	@x[10],@x[10]
	rev	@x[12],@x[12]
	rev	@x[14],@x[14]
#endif
	eor	@x[0],@x[0],@x[1]
	eor	@x[2],@x[2],@x[3]
	eor	@x[4],@x[4],@x[5]
	eor	@x[6],@x[6],@x[7]
	eor	@x[8],@x[8],@x[9]
	eor	@x[10],@x[10],@x[11]
	eor	@x[12],@x[12],@x[13]
	eor	@x[14],@x[14],@x[15]

	 stp	@x[0],@x[2],[$out,#0]		// store output
	 add	@d[6],@d[6],#1			// increment counter
	mov.32	@x[0],@d[0]			// unpack key block
	lsr	@x[1],@d[0],#32
	 stp	@x[4],@x[6],[$out,#16]
	mov.32	@x[2],@d[1]
	lsr	@x[3],@d[1],#32
	 stp	@x[8],@x[10],[$out,#32]
	mov.32	@x[4],@d[2]
	lsr	@x[5],@d[2],#32
	 stp	@x[12],@x[14],[$out,#48]
	 add	$out,$out,#64
	mov.32	@x[6],@d[3]
	lsr	@x[7],@d[3],#32
	mov.32	@x[8],@d[4]
	lsr	@x[9],@d[4],#32
	mov.32	@x[10],@d[5]
	lsr	@x[11],@d[5],#32
	mov.32	@x[12],@d[6]
	lsr	@x[13],@d[6],#32
	mov.32	@x[14],@d[7]
	lsr	@x[15],@d[7],#32

	mov	$ctr,#5
.Loop_lower_neon:
	sub	$ctr,$ctr,#1
___
	@thread0=&NEONROUND($A0,$B0,$C0,$D0,$T0,0);
	@thread1=&NEONROUND($A1,$B1,$C1,$D1,$T1,0);
	@thread2=&NEONROUND($A2,$B2,$C2,$D2,$T2,0);
	@thread3=&NEONROUND($A3,$B3,$C3,$D3,$T3,0);
	@thread4=&NEONROUND($A4,$B4,$C4,$D4,$T4,0);
	@thread5=&NEONROUND($A5,$B5,$C5,$D5,$T5,0);
	@thread67=(&ROUND(0,4,8,12),&ROUND(0,5,10,15));

	foreach (@thread0) {
		eval;			eval(shift(@thread67));
		eval(shift(@thread1));	eval(shift(@thread67));
		eval(shift(@thread2));	eval(shift(@thread67));
		eval(shift(@thread3));	eval(shift(@thread67));
		eval(shift(@thread4));	eval(shift(@thread67));
		eval(shift(@thread5));	eval(shift(@thread67));
	}

	@thread0=&NEONROUND($A0,$B0,$C0,$D0,$T0,1);
	@thread1=&NEONROUND($A1,$B1,$C1,$D1,$T1,1);
	@thread2=&NEONROUND($A2,$B2,$C2,$D2,$T2,1);
	@thread3=&NEONROUND($A3,$B3,$C3,$D3,$T3,1);
	@thread4=&NEONROUND($A4,$B4,$C4,$D4,$T4,1);
	@thread5=&NEONROUND($A5,$B5,$C5,$D5,$T5,1);
	@thread67=(&ROUND(0,4,8,12),&ROUND(0,5,10,15));

	foreach (@thread0) {
		eval;			eval(shift(@thread67));
		eval(shift(@thread1));	eval(shift(@thread67));
		eval(shift(@thread2));	eval(shift(@thread67));
		eval(shift(@thread3));	eval(shift(@thread67));
		eval(shift(@thread4));	eval(shift(@thread67));
		eval(shift(@thread5));	eval(shift(@thread67));
	}
$code.=<<___;
	cbnz	$ctr,.Loop_lower_neon

	add.32	@x[0],@x[0],@d[0]		// accumulate key block
	 ldp	@K[0],@K[1],[sp,#0]
	add	@x[1],@x[1],@d[0],lsr#32
	 ldp	@K[2],@K[3],[sp,#32]
	add.32	@x[2],@x[2],@d[1]
	 ldp	@K[4],@K[5],[sp,#64]
	add	@x[3],@x[3],@d[1],lsr#32
	 add	$A0,$A0,@K[0]
	add.32	@x[4],@x[4],@d[2]
	 add	$A1,$A1,@K[0]
	add	@x[5],@x[5],@d[2],lsr#32
	 add	$A2,$A2,@K[0]
	add.32	@x[6],@x[6],@d[3]
	 add	$A3,$A3,@K[0]
	add	@x[7],@x[7],@d[3],lsr#32
	 add	$A4,$A4,@K[0]
	add.32	@x[8],@x[8],@d[4]
	 add	$A5,$A5,@K[0]
	add	@x[9],@x[9],@d[4],lsr#32
	 add	$C0,$C0,@K[2]
	add.32	@x[10],@x[10],@d[5]
	 add	$C1,$C1,@K[2]
	add	@x[11],@x[11],@d[5],lsr#32
	 add	$C2,$C2,@K[2]
	add.32	@x[12],@x[12],@d[6]
	 add	$C3,$C3,@K[2]
	add	@x[13],@x[13],@d[6],lsr#32
	 add	$C4,$C4,@K[2]
	add.32	@x[14],@x[14],@d[7]
	 add	$C5,$C5,@K[2]
	add	@x[15],@x[15],@d[7],lsr#32
	 add	$D4,$D4,$ONE			// +4
	add	@x[0],@x[0],@x[1],lsl#32	// pack
	 add	$D5,$D5,$ONE			// +4
	add	@x[2],@x[2],@x[3],lsl#32
	 add	$D0,$D0,@K[3]
	ldp	@x[1],@x[3],[$inp,#0]		// load input
	 add	$D1,$D1,@K[4]
	add	@x[4],@x[4],@x[5],lsl#32
	 add	$D2,$D2,@K[5]
	add	@x[6],@x[6],@x[7],lsl#32
	 add	$D3,$D3,@K[6]
	ldp	@x[5],@x[7],[$inp,#16]
	 add	$D4,$D4,@K[3]
	add	@x[8],@x[8],@x[9],lsl#32
	 add	$D5,$D5,@K[4]
	add	@x[10],@x[10],@x[11],lsl#32
	 add	$B0,$B0,@K[1]
	ldp	@x[9],@x[11],[$inp,#32]
	 add	$B1,$B1,@K[1]
	add	@x[12],@x[12],@x[13],lsl#32
	 add	$B2,$B2,@K[1]
	add	@x[14],@x[14],@x[15],lsl#32
	 add	$B3,$B3,@K[1]
	ldp	@x[13],@x[15],[$inp,#48]
	 add	$B4,$B4,@K[1]
	add	$inp,$inp,#64
	 add	$B5,$B5,@K[1]

#ifdef	__ARMEB__
	rev	@x[0],@x[0]
	rev	@x[2],@x[2]
	rev	@x[4],@x[4]
	rev	@x[6],@x[6]
	rev	@x[8],@x[8]
	rev	@x[10],@x[10]
	rev	@x[12],@x[12]
	rev	@x[14],@x[14]
#endif
	ld1.8	{$T0-$T3},[$inp],#64
	eor	@x[0],@x[0],@x[1]
	eor	@x[2],@x[2],@x[3]
	eor	@x[4],@x[4],@x[5]
	eor	@x[6],@x[6],@x[7]
	eor	@x[8],@x[8],@x[9]
	 eor	$A0,$A0,$T0
	eor	@x[10],@x[10],@x[11]
	 eor	$B0,$B0,$T1
	eor	@x[12],@x[12],@x[13]
	 eor	$C0,$C0,$T2
	eor	@x[14],@x[14],@x[15]
	 eor	$D0,$D0,$T3
	 ld1.8	{$T0-$T3},[$inp],#64

	stp	@x[0],@x[2],[$out,#0]		// store output
	 add	@d[6],@d[6],#7			// increment counter
	stp	@x[4],@x[6],[$out,#16]
	stp	@x[8],@x[10],[$out,#32]
	stp	@x[12],@x[14],[$out,#48]
	add	$out,$out,#64
	st1.8	{$A0-$D0},[$out],#64

	ld1.8	{$A0-$D0},[$inp],#64
	eor	$A1,$A1,$T0
	eor	$B1,$B1,$T1
	eor	$C1,$C1,$T2
	eor	$D1,$D1,$T3
	st1.8	{$A1-$D1},[$out],#64

	ld1.8	{$A1-$D1},[$inp],#64
	eor	$A2,$A2,$A0
	 ldp	@K[0],@K[1],[sp,#0]
	eor	$B2,$B2,$B0
	 ldp	@K[2],@K[3],[sp,#32]
	eor	$C2,$C2,$C0
	eor	$D2,$D2,$D0
	st1.8	{$A2-$D2},[$out],#64

	ld1.8	{$A2-$D2},[$inp],#64
	eor	$A3,$A3,$A1
	eor	$B3,$B3,$B1
	eor	$C3,$C3,$C1
	eor	$D3,$D3,$D1
	st1.8	{$A3-$D3},[$out],#64

	ld1.8	{$A3-$D3},[$inp],#64
	eor	$A4,$A4,$A2
	eor	$B4,$B4,$B2
	eor	$C4,$C4,$C2
	eor	$D4,$D4,$D2
	st1.8	{$A4-$D4},[$out],#64

	shl	$A0,$ONE,#1			// 4 -> 8
	eor	$A5,$A5,$A3
	eor	$B5,$B5,$B3
	eor	$C5,$C5,$C3
	eor	$D5,$D5,$D3
	st1.8	{$A5-$D5},[$out],#64

	add	@K[3],@K[3],$A0			// += 8
	add	@K[4],@K[4],$A0
	add	@K[5],@K[5],$A0
	add	@K[6],@K[6],$A0

	b.hs	.Loop_outer_512_neon

	adds	$len,$len,#512
	ushr	$A0,$ONE,#2			// 4 -> 1

	ldp	d8,d9,[sp,#128+0]		// meet ABI requirements
	ldp	d10,d11,[sp,#128+16]
	ldp	d12,d13,[sp,#128+32]
	ldp	d14,d15,[sp,#128+48]

	stp	@K[0],$ONE,[sp,#0]		// wipe off-load area
	stp	@K[0],$ONE,[sp,#32]
	stp	@K[0],$ONE,[sp,#64]

	b.eq	.Ldone_512_neon

	cmp	$len,#192
	sub	@K[3],@K[3],$A0			// -= 1
	sub	@K[4],@K[4],$A0
	sub	@K[5],@K[5],$A0
	add	sp,sp,#128
	b.hs	.Loop_outer_neon

	eor	@K[1],@K[1],@K[1]
	eor	@K[2],@K[2],@K[2]
	eor	@K[3],@K[3],@K[3]
	eor	@K[4],@K[4],@K[4]
	eor	@K[5],@K[5],@K[5]
	eor	@K[6],@K[6],@K[6]
	b	.Loop_outer

.Ldone_512_neon:
	ldp	x19,x20,[x29,#16]
	add	sp,sp,#128+64
	ldp	x21,x22,[x29,#32]
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldp	x29,x30,[sp],#96
	.inst	0xd50323bf			// autiasp
	ret
.size	ChaCha20_512_neon,.-ChaCha20_512_neon
___
}
}}}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/geo;

	(s/\b([a-z]+)\.32\b/$1/ and (s/x([0-9]+)/w$1/g or 1))	or
	(m/\b(eor|ext|mov)\b/ and (s/\.4s/\.16b/g or 1))	or
	(s/\b((?:ld|st)1)\.8\b/$1/ and (s/\.4s/\.16b/g or 1))	or
	(m/\b(ld|st)[rp]\b/ and (s/v([0-9]+)\.4s/q$1/g or 1))	or
	(s/\brev32\.16\b/rev32/ and (s/\.4s/\.8h/g or 1));

	#s/\bq([0-9]+)#(lo|hi)/sprintf "d%d",2*$1+($2 eq "hi")/geo;

	print $_,"\n";
}
close STDOUT;	# flush

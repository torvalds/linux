#! /usr/bin/env perl
# Copyright 2014-2016 The OpenSSL Project Authors. All Rights Reserved.
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
# SHA1 for ARMv8.
#
# Performance in cycles per processed byte and improvement coefficient
# over code generated with "default" compiler:
#
#		hardware-assisted	software(*)
# Apple A7	2.31			4.13 (+14%)
# Cortex-A53	2.24			8.03 (+97%)
# Cortex-A57	2.35			7.88 (+74%)
# Denver	2.13			3.97 (+0%)(**)
# X-Gene				8.80 (+200%)
# Mongoose	2.05			6.50 (+160%)
# Kryo		1.88			8.00 (+90%)
#
# (*)	Software results are presented mostly for reference purposes.
# (**)	Keep in mind that Denver relies on binary translation, which
#	optimizes compiler output at run-time.

$flavour = shift;
$output  = shift;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/arm-xlate.pl" and -f $xlate) or
die "can't locate arm-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

($ctx,$inp,$num)=("x0","x1","x2");
@Xw=map("w$_",(3..17,19));
@Xx=map("x$_",(3..17,19));
@V=($A,$B,$C,$D,$E)=map("w$_",(20..24));
($t0,$t1,$t2,$K)=map("w$_",(25..28));


sub BODY_00_19 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=($i+2)&15;

$code.=<<___ if ($i<15 && !($i&1));
	lsr	@Xx[$i+1],@Xx[$i],#32
___
$code.=<<___ if ($i<14 && !($i&1));
	ldr	@Xx[$i+2],[$inp,#`($i+2)*4-64`]
___
$code.=<<___ if ($i<14 && ($i&1));
#ifdef	__ARMEB__
	ror	@Xx[$i+1],@Xx[$i+1],#32
#else
	rev32	@Xx[$i+1],@Xx[$i+1]
#endif
___
$code.=<<___ if ($i<14);
	bic	$t0,$d,$b
	and	$t1,$c,$b
	ror	$t2,$a,#27
	add	$d,$d,$K		// future e+=K
	orr	$t0,$t0,$t1
	add	$e,$e,$t2		// e+=rot(a,5)
	ror	$b,$b,#2
	add	$d,$d,@Xw[($i+1)&15]	// future e+=X[i]
	add	$e,$e,$t0		// e+=F(b,c,d)
___
$code.=<<___ if ($i==19);
	movz	$K,#0xeba1
	movk	$K,#0x6ed9,lsl#16
___
$code.=<<___ if ($i>=14);
	 eor	@Xw[$j],@Xw[$j],@Xw[($j+2)&15]
	bic	$t0,$d,$b
	and	$t1,$c,$b
	ror	$t2,$a,#27
	 eor	@Xw[$j],@Xw[$j],@Xw[($j+8)&15]
	add	$d,$d,$K		// future e+=K
	orr	$t0,$t0,$t1
	add	$e,$e,$t2		// e+=rot(a,5)
	 eor	@Xw[$j],@Xw[$j],@Xw[($j+13)&15]
	ror	$b,$b,#2
	add	$d,$d,@Xw[($i+1)&15]	// future e+=X[i]
	add	$e,$e,$t0		// e+=F(b,c,d)
	 ror	@Xw[$j],@Xw[$j],#31
___
}

sub BODY_40_59 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=($i+2)&15;

$code.=<<___ if ($i==59);
	movz	$K,#0xc1d6
	movk	$K,#0xca62,lsl#16
___
$code.=<<___;
	orr	$t0,$b,$c
	and	$t1,$b,$c
	 eor	@Xw[$j],@Xw[$j],@Xw[($j+2)&15]
	ror	$t2,$a,#27
	and	$t0,$t0,$d
	add	$d,$d,$K		// future e+=K
	 eor	@Xw[$j],@Xw[$j],@Xw[($j+8)&15]
	add	$e,$e,$t2		// e+=rot(a,5)
	orr	$t0,$t0,$t1
	ror	$b,$b,#2
	 eor	@Xw[$j],@Xw[$j],@Xw[($j+13)&15]
	add	$d,$d,@Xw[($i+1)&15]	// future e+=X[i]
	add	$e,$e,$t0		// e+=F(b,c,d)
	 ror	@Xw[$j],@Xw[$j],#31
___
}

sub BODY_20_39 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=($i+2)&15;

$code.=<<___ if ($i==39);
	movz	$K,#0xbcdc
	movk	$K,#0x8f1b,lsl#16
___
$code.=<<___ if ($i<78);
	 eor	@Xw[$j],@Xw[$j],@Xw[($j+2)&15]
	eor	$t0,$d,$b
	ror	$t2,$a,#27
	add	$d,$d,$K		// future e+=K
	 eor	@Xw[$j],@Xw[$j],@Xw[($j+8)&15]
	eor	$t0,$t0,$c
	add	$e,$e,$t2		// e+=rot(a,5)
	ror	$b,$b,#2
	 eor	@Xw[$j],@Xw[$j],@Xw[($j+13)&15]
	add	$d,$d,@Xw[($i+1)&15]	// future e+=X[i]
	add	$e,$e,$t0		// e+=F(b,c,d)
	 ror	@Xw[$j],@Xw[$j],#31
___
$code.=<<___ if ($i==78);
	ldp	@Xw[1],@Xw[2],[$ctx]
	eor	$t0,$d,$b
	ror	$t2,$a,#27
	add	$d,$d,$K		// future e+=K
	eor	$t0,$t0,$c
	add	$e,$e,$t2		// e+=rot(a,5)
	ror	$b,$b,#2
	add	$d,$d,@Xw[($i+1)&15]	// future e+=X[i]
	add	$e,$e,$t0		// e+=F(b,c,d)
___
$code.=<<___ if ($i==79);
	ldp	@Xw[3],@Xw[4],[$ctx,#8]
	eor	$t0,$d,$b
	ror	$t2,$a,#27
	eor	$t0,$t0,$c
	add	$e,$e,$t2		// e+=rot(a,5)
	ror	$b,$b,#2
	ldr	@Xw[5],[$ctx,#16]
	add	$e,$e,$t0		// e+=F(b,c,d)
___
}

$code.=<<___;
#include "arm_arch.h"

.text

.extern	OPENSSL_armcap_P
.globl	sha1_block_data_order
.type	sha1_block_data_order,%function
.align	6
sha1_block_data_order:
#ifdef	__ILP32__
	ldrsw	x16,.LOPENSSL_armcap_P
#else
	ldr	x16,.LOPENSSL_armcap_P
#endif
	adr	x17,.LOPENSSL_armcap_P
	add	x16,x16,x17
	ldr	w16,[x16]
	tst	w16,#ARMV8_SHA1
	b.ne	.Lv8_entry

	stp	x29,x30,[sp,#-96]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]

	ldp	$A,$B,[$ctx]
	ldp	$C,$D,[$ctx,#8]
	ldr	$E,[$ctx,#16]

.Loop:
	ldr	@Xx[0],[$inp],#64
	movz	$K,#0x7999
	sub	$num,$num,#1
	movk	$K,#0x5a82,lsl#16
#ifdef	__ARMEB__
	ror	$Xx[0],@Xx[0],#32
#else
	rev32	@Xx[0],@Xx[0]
#endif
	add	$E,$E,$K		// warm it up
	add	$E,$E,@Xw[0]
___
for($i=0;$i<20;$i++)	{ &BODY_00_19($i,@V); unshift(@V,pop(@V)); }
for(;$i<40;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
for(;$i<60;$i++)	{ &BODY_40_59($i,@V); unshift(@V,pop(@V)); }
for(;$i<80;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	add	$B,$B,@Xw[2]
	add	$C,$C,@Xw[3]
	add	$A,$A,@Xw[1]
	add	$D,$D,@Xw[4]
	add	$E,$E,@Xw[5]
	stp	$A,$B,[$ctx]
	stp	$C,$D,[$ctx,#8]
	str	$E,[$ctx,#16]
	cbnz	$num,.Loop

	ldp	x19,x20,[sp,#16]
	ldp	x21,x22,[sp,#32]
	ldp	x23,x24,[sp,#48]
	ldp	x25,x26,[sp,#64]
	ldp	x27,x28,[sp,#80]
	ldr	x29,[sp],#96
	ret
.size	sha1_block_data_order,.-sha1_block_data_order
___
{{{
my ($ABCD,$E,$E0,$E1)=map("v$_.16b",(0..3));
my @MSG=map("v$_.16b",(4..7));
my @Kxx=map("v$_.4s",(16..19));
my ($W0,$W1)=("v20.4s","v21.4s");
my $ABCD_SAVE="v22.16b";

$code.=<<___;
.type	sha1_block_armv8,%function
.align	6
sha1_block_armv8:
.Lv8_entry:
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0

	adr	x4,.Lconst
	eor	$E,$E,$E
	ld1.32	{$ABCD},[$ctx],#16
	ld1.32	{$E}[0],[$ctx]
	sub	$ctx,$ctx,#16
	ld1.32	{@Kxx[0]-@Kxx[3]},[x4]

.Loop_hw:
	ld1	{@MSG[0]-@MSG[3]},[$inp],#64
	sub	$num,$num,#1
	rev32	@MSG[0],@MSG[0]
	rev32	@MSG[1],@MSG[1]

	add.i32	$W0,@Kxx[0],@MSG[0]
	rev32	@MSG[2],@MSG[2]
	orr	$ABCD_SAVE,$ABCD,$ABCD	// offload

	add.i32	$W1,@Kxx[0],@MSG[1]
	rev32	@MSG[3],@MSG[3]
	sha1h	$E1,$ABCD
	sha1c	$ABCD,$E,$W0		// 0
	add.i32	$W0,@Kxx[$j],@MSG[2]
	sha1su0	@MSG[0],@MSG[1],@MSG[2]
___
for ($j=0,$i=1;$i<20-3;$i++) {
my $f=("c","p","m","p")[$i/5];
$code.=<<___;
	sha1h	$E0,$ABCD		// $i
	sha1$f	$ABCD,$E1,$W1
	add.i32	$W1,@Kxx[$j],@MSG[3]
	sha1su1	@MSG[0],@MSG[3]
___
$code.=<<___ if ($i<20-4);
	sha1su0	@MSG[1],@MSG[2],@MSG[3]
___
	($E0,$E1)=($E1,$E0);		($W0,$W1)=($W1,$W0);
	push(@MSG,shift(@MSG));		$j++ if ((($i+3)%5)==0);
}
$code.=<<___;
	sha1h	$E0,$ABCD		// $i
	sha1p	$ABCD,$E1,$W1
	add.i32	$W1,@Kxx[$j],@MSG[3]

	sha1h	$E1,$ABCD		// 18
	sha1p	$ABCD,$E0,$W0

	sha1h	$E0,$ABCD		// 19
	sha1p	$ABCD,$E1,$W1

	add.i32	$E,$E,$E0
	add.i32	$ABCD,$ABCD,$ABCD_SAVE

	cbnz	$num,.Loop_hw

	st1.32	{$ABCD},[$ctx],#16
	st1.32	{$E}[0],[$ctx]

	ldr	x29,[sp],#16
	ret
.size	sha1_block_armv8,.-sha1_block_armv8
.align	6
.Lconst:
.long	0x5a827999,0x5a827999,0x5a827999,0x5a827999	//K_00_19
.long	0x6ed9eba1,0x6ed9eba1,0x6ed9eba1,0x6ed9eba1	//K_20_39
.long	0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc	//K_40_59
.long	0xca62c1d6,0xca62c1d6,0xca62c1d6,0xca62c1d6	//K_60_79
.LOPENSSL_armcap_P:
#ifdef	__ILP32__
.long	OPENSSL_armcap_P-.
#else
.quad	OPENSSL_armcap_P-.
#endif
.asciz	"SHA1 block transform for ARMv8, CRYPTOGAMS by <appro\@openssl.org>"
.align	2
.comm	OPENSSL_armcap_P,4,4
___
}}}

{   my	%opcode = (
	"sha1c"		=> 0x5e000000,	"sha1p"		=> 0x5e001000,
	"sha1m"		=> 0x5e002000,	"sha1su0"	=> 0x5e003000,
	"sha1h"		=> 0x5e280800,	"sha1su1"	=> 0x5e281800	);

    sub unsha1 {
	my ($mnemonic,$arg)=@_;

	$arg =~ m/[qv]([0-9]+)[^,]*,\s*[qv]([0-9]+)[^,]*(?:,\s*[qv]([0-9]+))?/o
	&&
	sprintf ".inst\t0x%08x\t//%s %s",
			$opcode{$mnemonic}|$1|($2<<5)|($3<<16),
			$mnemonic,$arg;
    }
}

foreach(split("\n",$code)) {

	s/\`([^\`]*)\`/eval($1)/geo;

	s/\b(sha1\w+)\s+([qv].*)/unsha1($1,$2)/geo;

	s/\.\w?32\b//o		and s/\.16b/\.4s/go;
	m/(ld|st)1[^\[]+\[0\]/o	and s/\.4s/\.s/go;

	print $_,"\n";
}

close STDOUT;

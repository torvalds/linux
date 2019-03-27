#! /usr/bin/env perl
# Copyright 2009-2018 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


$flavour = shift;
$output = shift;
open STDOUT,">$output";

if ($flavour =~ /64/) {
	$LEVEL		="2.0W";
	$SIZE_T		=8;
	$ST		="std";
} else {
	$LEVEL		="1.1";
	$SIZE_T		=4;
	$ST		="stw";
}

$rp="%r2";
$sp="%r30";
$rv="%r28";

$code=<<___;
	.LEVEL	$LEVEL
	.SPACE	\$TEXT\$
	.SUBSPA	\$CODE\$,QUAD=0,ALIGN=8,ACCESS=0x2C,CODE_ONLY

	.EXPORT	OPENSSL_cpuid_setup,ENTRY
	.ALIGN	8
OPENSSL_cpuid_setup
	.PROC
	.CALLINFO	NO_CALLS
	.ENTRY
	bv	($rp)
	.EXIT
	nop
	.PROCEND

	.EXPORT	OPENSSL_rdtsc,ENTRY
	.ALIGN	8
OPENSSL_rdtsc
	.PROC
	.CALLINFO	NO_CALLS
	.ENTRY
	mfctl	%cr16,$rv
	bv	($rp)
	.EXIT
	nop
	.PROCEND

	.EXPORT	OPENSSL_wipe_cpu,ENTRY
	.ALIGN	8
OPENSSL_wipe_cpu
	.PROC
	.CALLINFO	NO_CALLS
	.ENTRY
	xor		%r0,%r0,%r1
	fcpy,dbl	%fr0,%fr4
	xor		%r0,%r0,%r19
	fcpy,dbl	%fr0,%fr5
	xor		%r0,%r0,%r20
	fcpy,dbl	%fr0,%fr6
	xor		%r0,%r0,%r21
	fcpy,dbl	%fr0,%fr7
	xor		%r0,%r0,%r22
	fcpy,dbl	%fr0,%fr8
	xor		%r0,%r0,%r23
	fcpy,dbl	%fr0,%fr9
	xor		%r0,%r0,%r24
	fcpy,dbl	%fr0,%fr10
	xor		%r0,%r0,%r25
	fcpy,dbl	%fr0,%fr11
	xor		%r0,%r0,%r26
	fcpy,dbl	%fr0,%fr22
	xor		%r0,%r0,%r29
	fcpy,dbl	%fr0,%fr23
	xor		%r0,%r0,%r31
	fcpy,dbl	%fr0,%fr24
	fcpy,dbl	%fr0,%fr25
	fcpy,dbl	%fr0,%fr26
	fcpy,dbl	%fr0,%fr27
	fcpy,dbl	%fr0,%fr28
	fcpy,dbl	%fr0,%fr29
	fcpy,dbl	%fr0,%fr30
	fcpy,dbl	%fr0,%fr31
	bv		($rp)
	.EXIT
	ldo		0($sp),$rv
	.PROCEND
___
{
my $inp="%r26";
my $len="%r25";

$code.=<<___;
	.EXPORT	OPENSSL_cleanse,ENTRY,ARGW0=GR,ARGW1=GR
	.ALIGN	8
OPENSSL_cleanse
	.PROC
	.CALLINFO	NO_CALLS
	.ENTRY
	cmpib,*=	0,$len,L\$done
	nop
	cmpib,*>>=	15,$len,L\$ittle
	ldi		$SIZE_T-1,%r1

L\$align
	and,*<>		$inp,%r1,%r28
	b,n		L\$aligned
	stb		%r0,0($inp)
	ldo		-1($len),$len
	b		L\$align
	ldo		1($inp),$inp

L\$aligned
	andcm		$len,%r1,%r28
L\$ot
	$ST		%r0,0($inp)
	addib,*<>	-$SIZE_T,%r28,L\$ot
	ldo		$SIZE_T($inp),$inp

	and,*<>		$len,%r1,$len
	b,n		L\$done
L\$ittle
	stb		%r0,0($inp)
	addib,*<>	-1,$len,L\$ittle
	ldo		1($inp),$inp
L\$done
	bv		($rp)
	.EXIT
	nop
	.PROCEND
___
}
{
my ($in1,$in2,$len)=("%r26","%r25","%r24");

$code.=<<___;
	.EXPORT	CRYPTO_memcmp,ENTRY,ARGW0=GR,ARGW1=GR,ARGW1=GR
	.ALIGN	8
CRYPTO_memcmp
	.PROC
	.CALLINFO	NO_CALLS
	.ENTRY
	cmpib,*=	0,$len,L\$no_data
	xor		$rv,$rv,$rv

L\$oop_cmp
	ldb		0($in1),%r19
	ldb		0($in2),%r20
	ldo		1($in1),$in1
	ldo		1($in2),$in2
	xor		%r19,%r20,%r29
	addib,*<>	-1,$len,L\$oop_cmp
	or		%r29,$rv,$rv

	sub		%r0,$rv,%r29
	extru		%r29,0,1,$rv
L\$no_data
	bv		($rp)
	.EXIT
	nop
	.PROCEND
___
}
{
my ($out,$cnt,$max)=("%r26","%r25","%r24");
my ($tick,$lasttick)=("%r23","%r22");
my ($diff,$lastdiff)=("%r21","%r20");

$code.=<<___;
	.EXPORT	OPENSSL_instrument_bus,ENTRY,ARGW0=GR,ARGW1=GR
	.ALIGN	8
OPENSSL_instrument_bus
	.PROC
	.CALLINFO	NO_CALLS
	.ENTRY
	copy		$cnt,$rv
	mfctl		%cr16,$tick
	copy		$tick,$lasttick
	ldi		0,$diff

	fdc		0($out)
	ldw		0($out),$tick
	add		$diff,$tick,$tick
	stw		$tick,0($out)
L\$oop
	mfctl		%cr16,$tick
	sub		$tick,$lasttick,$diff
	copy		$tick,$lasttick

	fdc		0($out)
	ldw		0($out),$tick
	add		$diff,$tick,$tick
	stw		$tick,0($out)

	addib,<>	-1,$cnt,L\$oop
	addi		4,$out,$out

	bv		($rp)
	.EXIT
	sub		$rv,$cnt,$rv
	.PROCEND

	.EXPORT	OPENSSL_instrument_bus2,ENTRY,ARGW0=GR,ARGW1=GR
	.ALIGN	8
OPENSSL_instrument_bus2
	.PROC
	.CALLINFO	NO_CALLS
	.ENTRY
	copy		$cnt,$rv
	sub		%r0,$cnt,$cnt

	mfctl		%cr16,$tick
	copy		$tick,$lasttick
	ldi		0,$diff

	fdc		0($out)
	ldw		0($out),$tick
	add		$diff,$tick,$tick
	stw		$tick,0($out)

	mfctl		%cr16,$tick
	sub		$tick,$lasttick,$diff
	copy		$tick,$lasttick
L\$oop2
	copy		$diff,$lastdiff
	fdc		0($out)
	ldw		0($out),$tick
	add		$diff,$tick,$tick
	stw		$tick,0($out)

	addib,=		-1,$max,L\$done2
	nop

	mfctl		%cr16,$tick
	sub		$tick,$lasttick,$diff
	copy		$tick,$lasttick
	cmpclr,<>	$lastdiff,$diff,$tick
	ldi		1,$tick

	ldi		1,%r1
	xor		%r1,$tick,$tick
	addb,<>		$tick,$cnt,L\$oop2
	shladd,l	$tick,2,$out,$out
L\$done2
	bv		($rp)
	.EXIT
	add		$rv,$cnt,$rv
	.PROCEND
___
}

if (`$ENV{CC} -Wa,-v -c -o /dev/null -x assembler /dev/null 2>&1`
	=~ /GNU assembler/) {
    $gnuas = 1;
}

foreach(split("\n",$code)) {

	s/(\.LEVEL\s+2\.0)W/$1w/	if ($gnuas && $SIZE_T==8);
	s/\.SPACE\s+\$TEXT\$/.text/	if ($gnuas && $SIZE_T==8);
	s/\.SUBSPA.*//			if ($gnuas && $SIZE_T==8);
	s/cmpib,\*/comib,/		if ($SIZE_T==4);
	s/,\*/,/			if ($SIZE_T==4);
	s/\bbv\b/bve/			if ($SIZE_T==8);

	print $_,"\n";
}
close STDOUT;


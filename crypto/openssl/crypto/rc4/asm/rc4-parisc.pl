#! /usr/bin/env perl
# Copyright 2009-2018 The OpenSSL Project Authors. All Rights Reserved.
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

# RC4 for PA-RISC.

# June 2009.
#
# Performance is 33% better than gcc 3.2 generated code on PA-7100LC.
# For reference, [4x] unrolled loop is >40% faster than folded one.
# It's possible to unroll loop 8 times on PA-RISC 2.0, but improvement
# is believed to be not sufficient to justify the effort...
#
# Special thanks to polarhome.com for providing HP-UX account.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;

$flavour = shift;
$output = shift;
open STDOUT,">$output";

if ($flavour =~ /64/) {
	$LEVEL		="2.0W";
	$SIZE_T		=8;
	$FRAME_MARKER	=80;
	$SAVED_RP	=16;
	$PUSH		="std";
	$PUSHMA		="std,ma";
	$POP		="ldd";
	$POPMB		="ldd,mb";
} else {
	$LEVEL		="1.0";
	$SIZE_T		=4;
	$FRAME_MARKER	=48;
	$SAVED_RP	=20;
	$PUSH		="stw";
	$PUSHMA		="stwm";
	$POP		="ldw";
	$POPMB		="ldwm";
}

$FRAME=4*$SIZE_T+$FRAME_MARKER;	# 4 saved regs + frame marker
				#                [+ argument transfer]
$SZ=1;				# defaults to RC4_CHAR
if (open CONF,"<${dir}../../opensslconf.h") {
    while(<CONF>) {
	if (m/#\s*define\s+RC4_INT\s+(.*)/) {
	    $SZ = ($1=~/char$/) ? 1 : 4;
	    last;
	}
    }
    close CONF;
}

if ($SZ==1) {	# RC4_CHAR
    $LD="ldb";
    $LDX="ldbx";
    $MKX="addl";
    $ST="stb";
} else {	# RC4_INT (~5% faster than RC4_CHAR on PA-7100LC)
    $LD="ldw";
    $LDX="ldwx,s";
    $MKX="sh2addl";
    $ST="stw";
}

$key="%r26";
$len="%r25";
$inp="%r24";
$out="%r23";

@XX=("%r19","%r20");
@TX=("%r21","%r22");
$YY="%r28";
$TY="%r29";

$acc="%r1";
$ix="%r2";
$iy="%r3";
$dat0="%r4";
$dat1="%r5";
$rem="%r6";
$mask="%r31";

sub unrolledloopbody {
for ($i=0;$i<4;$i++) {
$code.=<<___;
	ldo	1($XX[0]),$XX[1]
	`sprintf("$LDX	%$TY(%$key),%$dat1") if ($i>0)`
	and	$mask,$XX[1],$XX[1]
	$LDX	$YY($key),$TY
	$MKX	$YY,$key,$ix
	$LDX	$XX[1]($key),$TX[1]
	$MKX	$XX[0],$key,$iy
	$ST	$TX[0],0($ix)
	comclr,<> $XX[1],$YY,%r0	; conditional
	copy	$TX[0],$TX[1]		; move
	`sprintf("%sdep	%$dat1,%d,8,%$acc",$i==1?"z":"",8*($i-1)+7) if ($i>0)`
	$ST	$TY,0($iy)
	addl	$TX[0],$TY,$TY
	addl	$TX[1],$YY,$YY
	and	$mask,$TY,$TY
	and	$mask,$YY,$YY
___
push(@TX,shift(@TX)); push(@XX,shift(@XX));	# "rotate" registers
} }

sub foldedloop {
my ($label,$count)=@_;
$code.=<<___;
$label
	$MKX	$YY,$key,$iy
	$LDX	$YY($key),$TY
	$MKX	$XX[0],$key,$ix
	$ST	$TX[0],0($iy)
	ldo	1($XX[0]),$XX[0]
	$ST	$TY,0($ix)
	addl	$TX[0],$TY,$TY
	ldbx	$inp($out),$dat1
	and	$mask,$TY,$TY
	and	$mask,$XX[0],$XX[0]
	$LDX	$TY($key),$acc
	$LDX	$XX[0]($key),$TX[0]
	ldo	1($out),$out
	xor	$dat1,$acc,$acc
	addl	$TX[0],$YY,$YY
	stb	$acc,-1($out)
	addib,<> -1,$count,$label	; $count is always small
	and	$mask,$YY,$YY
___
}

$code=<<___;
	.LEVEL	$LEVEL
	.SPACE	\$TEXT\$
	.SUBSPA	\$CODE\$,QUAD=0,ALIGN=8,ACCESS=0x2C,CODE_ONLY

	.EXPORT	RC4,ENTRY,ARGW0=GR,ARGW1=GR,ARGW2=GR,ARGW3=GR
RC4
	.PROC
	.CALLINFO	FRAME=`$FRAME-4*$SIZE_T`,NO_CALLS,SAVE_RP,ENTRY_GR=6
	.ENTRY
	$PUSH	%r2,-$SAVED_RP(%sp)	; standard prologue
	$PUSHMA	%r3,$FRAME(%sp)
	$PUSH	%r4,`-$FRAME+1*$SIZE_T`(%sp)
	$PUSH	%r5,`-$FRAME+2*$SIZE_T`(%sp)
	$PUSH	%r6,`-$FRAME+3*$SIZE_T`(%sp)

	cmpib,*= 0,$len,L\$abort
	sub	$inp,$out,$inp		; distance between $inp and $out

	$LD	`0*$SZ`($key),$XX[0]
	$LD	`1*$SZ`($key),$YY
	ldo	`2*$SZ`($key),$key

	ldi	0xff,$mask
	ldi	3,$dat0

	ldo	1($XX[0]),$XX[0]	; warm up loop
	and	$mask,$XX[0],$XX[0]
	$LDX	$XX[0]($key),$TX[0]
	addl	$TX[0],$YY,$YY
	cmpib,*>>= 6,$len,L\$oop1	; is $len large enough to bother?
	and	$mask,$YY,$YY

	and,<>	$out,$dat0,$rem		; is $out aligned?
	b	L\$alignedout
	subi	4,$rem,$rem
	sub	$len,$rem,$len
___
&foldedloop("L\$alignout",$rem);	# process till $out is aligned

$code.=<<___;
L\$alignedout				; $len is at least 4 here
	and,<>	$inp,$dat0,$acc		; is $inp aligned?
	b	L\$oop4
	sub	$inp,$acc,$rem		; align $inp

	sh3addl	$acc,%r0,$acc
	subi	32,$acc,$acc
	mtctl	$acc,%cr11		; load %sar with vshd align factor
	ldwx	$rem($out),$dat0
	ldo	4($rem),$rem
L\$oop4misalignedinp
___
&unrolledloopbody();
$code.=<<___;
	$LDX	$TY($key),$ix
	ldwx	$rem($out),$dat1
	ldo	-4($len),$len
	or	$ix,$acc,$acc		; last piece, no need to dep
	vshd	$dat0,$dat1,$iy		; align data
	copy	$dat1,$dat0
	xor	$iy,$acc,$acc
	stw	$acc,0($out)
	cmpib,*<< 3,$len,L\$oop4misalignedinp
	ldo	4($out),$out
	cmpib,*= 0,$len,L\$done
	nop
	b	L\$oop1
	nop

	.ALIGN	8
L\$oop4
___
&unrolledloopbody();
$code.=<<___;
	$LDX	$TY($key),$ix
	ldwx	$inp($out),$dat0
	ldo	-4($len),$len
	or	$ix,$acc,$acc		; last piece, no need to dep
	xor	$dat0,$acc,$acc
	stw	$acc,0($out)
	cmpib,*<< 3,$len,L\$oop4
	ldo	4($out),$out
	cmpib,*= 0,$len,L\$done
	nop
___
&foldedloop("L\$oop1",$len);
$code.=<<___;
L\$done
	$POP	`-$FRAME-$SAVED_RP`(%sp),%r2
	ldo	-1($XX[0]),$XX[0]	; chill out loop
	sub	$YY,$TX[0],$YY
	and	$mask,$XX[0],$XX[0]
	and	$mask,$YY,$YY
	$ST	$XX[0],`-2*$SZ`($key)
	$ST	$YY,`-1*$SZ`($key)
	$POP	`-$FRAME+1*$SIZE_T`(%sp),%r4
	$POP	`-$FRAME+2*$SIZE_T`(%sp),%r5
	$POP	`-$FRAME+3*$SIZE_T`(%sp),%r6
L\$abort
	bv	(%r2)
	.EXIT
	$POPMB	-$FRAME(%sp),%r3
	.PROCEND
___

$code.=<<___;

	.EXPORT	RC4_set_key,ENTRY,ARGW0=GR,ARGW1=GR,ARGW2=GR
	.ALIGN	8
RC4_set_key
	.PROC
	.CALLINFO	NO_CALLS
	.ENTRY
	$ST	%r0,`0*$SZ`($key)
	$ST	%r0,`1*$SZ`($key)
	ldo	`2*$SZ`($key),$key
	copy	%r0,@XX[0]
L\$1st
	$ST	@XX[0],0($key)
	ldo	1(@XX[0]),@XX[0]
	bb,>=	@XX[0],`31-8`,L\$1st	; @XX[0]<256
	ldo	$SZ($key),$key

	ldo	`-256*$SZ`($key),$key	; rewind $key
	addl	$len,$inp,$inp		; $inp to point at the end
	sub	%r0,$len,%r23		; inverse index
	copy	%r0,@XX[0]
	copy	%r0,@XX[1]
	ldi	0xff,$mask

L\$2nd
	$LDX	@XX[0]($key),@TX[0]
	ldbx	%r23($inp),@TX[1]
	addi,nuv 1,%r23,%r23		; increment and conditional
	sub	%r0,$len,%r23		; inverse index
	addl	@TX[0],@XX[1],@XX[1]
	addl	@TX[1],@XX[1],@XX[1]
	and	$mask,@XX[1],@XX[1]
	$MKX	@XX[0],$key,$TY
	$LDX	@XX[1]($key),@TX[1]
	$MKX	@XX[1],$key,$YY
	ldo	1(@XX[0]),@XX[0]
	$ST	@TX[0],0($YY)
	bb,>=	@XX[0],`31-8`,L\$2nd	; @XX[0]<256
	$ST	@TX[1],0($TY)

	bv,n	(%r2)
	.EXIT
	nop
	.PROCEND

	.EXPORT	RC4_options,ENTRY
	.ALIGN	8
RC4_options
	.PROC
	.CALLINFO	NO_CALLS
	.ENTRY
	blr	%r0,%r28
	ldi	3,%r1
L\$pic
	andcm	%r28,%r1,%r28
	bv	(%r2)
	.EXIT
	ldo	L\$opts-L\$pic(%r28),%r28
	.PROCEND
	.ALIGN	8
L\$opts
	.STRINGZ "rc4(4x,`$SZ==1?"char":"int"`)"
	.STRINGZ "RC4 for PA-RISC, CRYPTOGAMS by <appro\@openssl.org>"
___

if (`$ENV{CC} -Wa,-v -c -o /dev/null -x assembler /dev/null 2>&1`
	=~ /GNU assembler/) {
    $gnuas = 1;
}

foreach(split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	s/(\.LEVEL\s+2\.0)W/$1w/	if ($gnuas && $SIZE_T==8);
	s/\.SPACE\s+\$TEXT\$/.text/	if ($gnuas && $SIZE_T==8);
	s/\.SUBSPA.*//			if ($gnuas && $SIZE_T==8);
	s/cmpib,\*/comib,/		if ($SIZE_T==4);
	s/\bbv\b/bve/			if ($SIZE_T==8);

	print $_,"\n";
}
close STDOUT;

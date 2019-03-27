#! /usr/bin/env perl
# Copyright 2006-2016 The OpenSSL Project Authors. All Rights Reserved.
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

# I let hardware handle unaligned input(*), except on page boundaries
# (see below for details). Otherwise straightforward implementation
# with X vector in register bank.
#
# (*) this means that this module is inappropriate for PPC403? Does
#     anybody know if pre-POWER3 can sustain unaligned load?

# 			-m64	-m32
# ----------------------------------
# PPC970,gcc-4.0.0	+76%	+59%
# Power6,xlc-7		+68%	+33%

$flavour = shift;

if ($flavour =~ /64/) {
	$SIZE_T	=8;
	$LRSAVE	=2*$SIZE_T;
	$UCMP	="cmpld";
	$STU	="stdu";
	$POP	="ld";
	$PUSH	="std";
} elsif ($flavour =~ /32/) {
	$SIZE_T	=4;
	$LRSAVE	=$SIZE_T;
	$UCMP	="cmplw";
	$STU	="stwu";
	$POP	="lwz";
	$PUSH	="stw";
} else { die "nonsense $flavour"; }

# Define endianness based on flavour
# i.e.: linux64le
$LITTLE_ENDIAN = ($flavour=~/le$/) ? $SIZE_T : 0;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour ".shift || die "can't call $xlate: $!";

$FRAME=24*$SIZE_T+64;
$LOCALS=6*$SIZE_T;

$K  ="r0";
$sp ="r1";
$toc="r2";
$ctx="r3";
$inp="r4";
$num="r5";
$t0 ="r15";
$t1 ="r6";

$A  ="r7";
$B  ="r8";
$C  ="r9";
$D  ="r10";
$E  ="r11";
$T  ="r12";

@V=($A,$B,$C,$D,$E,$T);
@X=("r16","r17","r18","r19","r20","r21","r22","r23",
    "r24","r25","r26","r27","r28","r29","r30","r31");

sub loadbe {
my ($dst, $src, $temp_reg) = @_;
$code.=<<___ if (!$LITTLE_ENDIAN);
	lwz	$dst,$src
___
$code.=<<___ if ($LITTLE_ENDIAN);
	lwz	$temp_reg,$src
	rotlwi	$dst,$temp_reg,8
	rlwimi	$dst,$temp_reg,24,0,7
	rlwimi	$dst,$temp_reg,24,16,23
___
}

sub BODY_00_19 {
my ($i,$a,$b,$c,$d,$e,$f)=@_;
my $j=$i+1;

	# Since the last value of $f is discarded, we can use
	# it as a temp reg to swap byte-order when needed.
	loadbe("@X[$i]","`$i*4`($inp)",$f) if ($i==0);
	loadbe("@X[$j]","`$j*4`($inp)",$f) if ($i<15);
$code.=<<___ if ($i<15);
	add	$f,$K,$e
	rotlwi	$e,$a,5
	add	$f,$f,@X[$i]
	and	$t0,$c,$b
	add	$f,$f,$e
	andc	$t1,$d,$b
	rotlwi	$b,$b,30
	or	$t0,$t0,$t1
	add	$f,$f,$t0
___
$code.=<<___ if ($i>=15);
	add	$f,$K,$e
	rotlwi	$e,$a,5
	xor	@X[$j%16],@X[$j%16],@X[($j+2)%16]
	add	$f,$f,@X[$i%16]
	and	$t0,$c,$b
	xor	@X[$j%16],@X[$j%16],@X[($j+8)%16]
	add	$f,$f,$e
	andc	$t1,$d,$b
	rotlwi	$b,$b,30
	or	$t0,$t0,$t1
	xor	@X[$j%16],@X[$j%16],@X[($j+13)%16]
	add	$f,$f,$t0
	rotlwi	@X[$j%16],@X[$j%16],1
___
}

sub BODY_20_39 {
my ($i,$a,$b,$c,$d,$e,$f)=@_;
my $j=$i+1;
$code.=<<___ if ($i<79);
	add	$f,$K,$e
	xor	$t0,$b,$d
	rotlwi	$e,$a,5
	xor	@X[$j%16],@X[$j%16],@X[($j+2)%16]
	add	$f,$f,@X[$i%16]
	xor	$t0,$t0,$c
	xor	@X[$j%16],@X[$j%16],@X[($j+8)%16]
	add	$f,$f,$t0
	rotlwi	$b,$b,30
	xor	@X[$j%16],@X[$j%16],@X[($j+13)%16]
	add	$f,$f,$e
	rotlwi	@X[$j%16],@X[$j%16],1
___
$code.=<<___ if ($i==79);
	add	$f,$K,$e
	xor	$t0,$b,$d
	rotlwi	$e,$a,5
	lwz	r16,0($ctx)
	add	$f,$f,@X[$i%16]
	xor	$t0,$t0,$c
	lwz	r17,4($ctx)
	add	$f,$f,$t0
	rotlwi	$b,$b,30
	lwz	r18,8($ctx)
	lwz	r19,12($ctx)
	add	$f,$f,$e
	lwz	r20,16($ctx)
___
}

sub BODY_40_59 {
my ($i,$a,$b,$c,$d,$e,$f)=@_;
my $j=$i+1;
$code.=<<___;
	add	$f,$K,$e
	rotlwi	$e,$a,5
	xor	@X[$j%16],@X[$j%16],@X[($j+2)%16]
	add	$f,$f,@X[$i%16]
	and	$t0,$b,$c
	xor	@X[$j%16],@X[$j%16],@X[($j+8)%16]
	add	$f,$f,$e
	or	$t1,$b,$c
	rotlwi	$b,$b,30
	xor	@X[$j%16],@X[$j%16],@X[($j+13)%16]
	and	$t1,$t1,$d
	or	$t0,$t0,$t1
	rotlwi	@X[$j%16],@X[$j%16],1
	add	$f,$f,$t0
___
}

$code=<<___;
.machine	"any"
.text

.globl	.sha1_block_data_order
.align	4
.sha1_block_data_order:
	$STU	$sp,-$FRAME($sp)
	mflr	r0
	$PUSH	r15,`$FRAME-$SIZE_T*17`($sp)
	$PUSH	r16,`$FRAME-$SIZE_T*16`($sp)
	$PUSH	r17,`$FRAME-$SIZE_T*15`($sp)
	$PUSH	r18,`$FRAME-$SIZE_T*14`($sp)
	$PUSH	r19,`$FRAME-$SIZE_T*13`($sp)
	$PUSH	r20,`$FRAME-$SIZE_T*12`($sp)
	$PUSH	r21,`$FRAME-$SIZE_T*11`($sp)
	$PUSH	r22,`$FRAME-$SIZE_T*10`($sp)
	$PUSH	r23,`$FRAME-$SIZE_T*9`($sp)
	$PUSH	r24,`$FRAME-$SIZE_T*8`($sp)
	$PUSH	r25,`$FRAME-$SIZE_T*7`($sp)
	$PUSH	r26,`$FRAME-$SIZE_T*6`($sp)
	$PUSH	r27,`$FRAME-$SIZE_T*5`($sp)
	$PUSH	r28,`$FRAME-$SIZE_T*4`($sp)
	$PUSH	r29,`$FRAME-$SIZE_T*3`($sp)
	$PUSH	r30,`$FRAME-$SIZE_T*2`($sp)
	$PUSH	r31,`$FRAME-$SIZE_T*1`($sp)
	$PUSH	r0,`$FRAME+$LRSAVE`($sp)
	lwz	$A,0($ctx)
	lwz	$B,4($ctx)
	lwz	$C,8($ctx)
	lwz	$D,12($ctx)
	lwz	$E,16($ctx)
	andi.	r0,$inp,3
	bne	Lunaligned
Laligned:
	mtctr	$num
	bl	Lsha1_block_private
	b	Ldone

; PowerPC specification allows an implementation to be ill-behaved
; upon unaligned access which crosses page boundary. "Better safe
; than sorry" principle makes me treat it specially. But I don't
; look for particular offending word, but rather for 64-byte input
; block which crosses the boundary. Once found that block is aligned
; and hashed separately...
.align	4
Lunaligned:
	subfic	$t1,$inp,4096
	andi.	$t1,$t1,4095	; distance to closest page boundary
	srwi.	$t1,$t1,6	; t1/=64
	beq	Lcross_page
	$UCMP	$num,$t1
	ble	Laligned	; didn't cross the page boundary
	mtctr	$t1
	subfc	$num,$t1,$num
	bl	Lsha1_block_private
Lcross_page:
	li	$t1,16
	mtctr	$t1
	addi	r20,$sp,$LOCALS	; spot within the frame
Lmemcpy:
	lbz	r16,0($inp)
	lbz	r17,1($inp)
	lbz	r18,2($inp)
	lbz	r19,3($inp)
	addi	$inp,$inp,4
	stb	r16,0(r20)
	stb	r17,1(r20)
	stb	r18,2(r20)
	stb	r19,3(r20)
	addi	r20,r20,4
	bdnz	Lmemcpy

	$PUSH	$inp,`$FRAME-$SIZE_T*18`($sp)
	li	$t1,1
	addi	$inp,$sp,$LOCALS
	mtctr	$t1
	bl	Lsha1_block_private
	$POP	$inp,`$FRAME-$SIZE_T*18`($sp)
	addic.	$num,$num,-1
	bne	Lunaligned

Ldone:
	$POP	r0,`$FRAME+$LRSAVE`($sp)
	$POP	r15,`$FRAME-$SIZE_T*17`($sp)
	$POP	r16,`$FRAME-$SIZE_T*16`($sp)
	$POP	r17,`$FRAME-$SIZE_T*15`($sp)
	$POP	r18,`$FRAME-$SIZE_T*14`($sp)
	$POP	r19,`$FRAME-$SIZE_T*13`($sp)
	$POP	r20,`$FRAME-$SIZE_T*12`($sp)
	$POP	r21,`$FRAME-$SIZE_T*11`($sp)
	$POP	r22,`$FRAME-$SIZE_T*10`($sp)
	$POP	r23,`$FRAME-$SIZE_T*9`($sp)
	$POP	r24,`$FRAME-$SIZE_T*8`($sp)
	$POP	r25,`$FRAME-$SIZE_T*7`($sp)
	$POP	r26,`$FRAME-$SIZE_T*6`($sp)
	$POP	r27,`$FRAME-$SIZE_T*5`($sp)
	$POP	r28,`$FRAME-$SIZE_T*4`($sp)
	$POP	r29,`$FRAME-$SIZE_T*3`($sp)
	$POP	r30,`$FRAME-$SIZE_T*2`($sp)
	$POP	r31,`$FRAME-$SIZE_T*1`($sp)
	mtlr	r0
	addi	$sp,$sp,$FRAME
	blr
	.long	0
	.byte	0,12,4,1,0x80,18,3,0
	.long	0
___

# This is private block function, which uses tailored calling
# interface, namely upon entry SHA_CTX is pre-loaded to given
# registers and counter register contains amount of chunks to
# digest...
$code.=<<___;
.align	4
Lsha1_block_private:
___
$code.=<<___;	# load K_00_19
	lis	$K,0x5a82
	ori	$K,$K,0x7999
___
for($i=0;$i<20;$i++)	{ &BODY_00_19($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;	# load K_20_39
	lis	$K,0x6ed9
	ori	$K,$K,0xeba1
___
for(;$i<40;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;	# load K_40_59
	lis	$K,0x8f1b
	ori	$K,$K,0xbcdc
___
for(;$i<60;$i++)	{ &BODY_40_59($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;	# load K_60_79
	lis	$K,0xca62
	ori	$K,$K,0xc1d6
___
for(;$i<80;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	add	r16,r16,$E
	add	r17,r17,$T
	add	r18,r18,$A
	add	r19,r19,$B
	add	r20,r20,$C
	stw	r16,0($ctx)
	mr	$A,r16
	stw	r17,4($ctx)
	mr	$B,r17
	stw	r18,8($ctx)
	mr	$C,r18
	stw	r19,12($ctx)
	mr	$D,r19
	stw	r20,16($ctx)
	mr	$E,r20
	addi	$inp,$inp,`16*4`
	bdnz	Lsha1_block_private
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,0,0
.size	.sha1_block_data_order,.-.sha1_block_data_order
___
$code.=<<___;
.asciz	"SHA1 block transform for PPC, CRYPTOGAMS by <appro\@fy.chalmers.se>"
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

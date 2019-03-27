#! /usr/bin/env perl
# Copyright 2014-2018 The OpenSSL Project Authors. All Rights Reserved.
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

# SHA256/512 for PowerISA v2.07.
#
# Accurate performance measurements are problematic, because it's
# always virtualized setup with possibly throttled processor.
# Relative comparison is therefore more informative. This module is
# ~60% faster than integer-only sha512-ppc.pl. To anchor to something
# else, SHA256 is 24% slower than sha1-ppc.pl and 2.5x slower than
# hardware-assisted aes-128-cbc encrypt. SHA512 is 20% faster than
# sha1-ppc.pl and 1.6x slower than aes-128-cbc. Another interesting
# result is degree of computational resources' utilization. POWER8 is
# "massively multi-threaded chip" and difference between single- and
# maximum multi-process benchmark results tells that utilization is
# whooping 94%. For sha512-ppc.pl we get [not unimpressive] 84% and
# for sha1-ppc.pl - 73%. 100% means that multi-process result equals
# to single-process one, given that all threads end up on the same
# physical core.
#
######################################################################
# Believed-to-be-accurate results in cycles per processed byte [on
# little-endian system]. Numbers in square brackets are for 64-bit
# build of sha512-ppc.pl, presented for reference.
#
#		POWER8		POWER9
# SHA256	9.7 [15.8]	11.2 [12.5]
# SHA512	6.1 [10.3]	7.0 [7.9]

$flavour=shift;
$output =shift;

if ($flavour =~ /64/) {
	$SIZE_T=8;
	$LRSAVE=2*$SIZE_T;
	$STU="stdu";
	$POP="ld";
	$PUSH="std";
} elsif ($flavour =~ /32/) {
	$SIZE_T=4;
	$LRSAVE=$SIZE_T;
	$STU="stwu";
	$POP="lwz";
	$PUSH="stw";
} else { die "nonsense $flavour"; }

$LENDIAN=($flavour=~/le/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour $output" || die "can't call $xlate: $!";

if ($output =~ /512/) {
	$bits=512;
	$SZ=8;
	$sz="d";
	$rounds=80;
} else {
	$bits=256;
	$SZ=4;
	$sz="w";
	$rounds=64;
}

$func="sha${bits}_block_p8";
$LOCALS=8*$SIZE_T+8*16;
$FRAME=$LOCALS+9*16+6*$SIZE_T;

$sp ="r1";
$toc="r2";
$ctx="r3";
$inp="r4";
$num="r5";
$Tbl="r6";
$idx="r7";
$lrsave="r8";
$offload="r11";
$vrsave="r12";
@I = ($x00,$x10,$x20,$x30,$x40,$x50,$x60,$x70) = (0,map("r$_",(10,26..31)));

@V=($A,$B,$C,$D,$E,$F,$G,$H)=map("v$_",(0..7));
@X=map("v$_",(8..19,24..27));
($Ki,$Func,$Sigma,$lemask)=map("v$_",(28..31));

sub ROUND {
my ($i,$a,$b,$c,$d,$e,$f,$g,$h)=@_;
my $j=($i+1)%16;
my $k=($i+2)%8;

$code.=<<___		if ($i<15 && ($i%(16/$SZ))==(16/$SZ-1));
	lvx_u		@X[$i+1],0,$inp		; load X[i] in advance
	addi		$inp,$inp,16
___
$code.=<<___		if ($i<16 && ($i%(16/$SZ)));
	vsldoi		@X[$i],@X[$i-1],@X[$i-1],$SZ
___
$code.=<<___		if ($LENDIAN && $i<16 && ($i%(16/$SZ))==0);
	vperm		@X[$i],@X[$i],@X[$i],$lemask
___
$code.=<<___		if ($i>=15);
	vshasigma${sz}	$Sigma,@X[($j+1)%16],0,0
	vaddu${sz}m	@X[$j],@X[$j],$Sigma
	vshasigma${sz}	$Sigma,@X[($j+14)%16],0,15
	vaddu${sz}m	@X[$j],@X[$j],$Sigma
	vaddu${sz}m	@X[$j],@X[$j],@X[($j+9)%16]
___
$code.=<<___;
	vaddu${sz}m	$h,$h,@X[$i%16]		; h+=X[i]
	vsel		$Func,$g,$f,$e		; Ch(e,f,g)
	vaddu${sz}m	$g,$g,$Ki		; future h+=K[i]
	vaddu${sz}m	$h,$h,$Func		; h+=Ch(e,f,g)
	vshasigma${sz}	$Sigma,$e,1,15		; Sigma1(e)
	vaddu${sz}m	$h,$h,$Sigma		; h+=Sigma1(e)
	vxor		$Func,$a,$b
	vsel		$Func,$b,$c,$Func	; Maj(a,b,c)
	vaddu${sz}m	$d,$d,$h		; d+=h
	vshasigma${sz}	$Sigma,$a,1,0		; Sigma0(a)
	vaddu${sz}m	$Sigma,$Sigma,$Func	; Sigma0(a)+Maj(a,b,c)
	vaddu${sz}m	$h,$h,$Sigma		; h+=Sigma0(a)+Maj(a,b,c)
	lvx		$Ki,@I[$k],$idx		; load next K[i]
___
$code.=<<___		if ($k == 7);
	addi		$idx,$idx,0x80
___
}

$code=<<___;
.machine	"any"
.text

.globl	$func
.align	6
$func:
	$STU		$sp,-$FRAME($sp)
	mflr		$lrsave
	li		r10,`$LOCALS+15`
	li		r11,`$LOCALS+31`
	stvx		v24,r10,$sp		# ABI says so
	addi		r10,r10,32
	mfspr		$vrsave,256
	stvx		v25,r11,$sp
	addi		r11,r11,32
	stvx		v26,r10,$sp
	addi		r10,r10,32
	stvx		v27,r11,$sp
	addi		r11,r11,32
	stvx		v28,r10,$sp
	addi		r10,r10,32
	stvx		v29,r11,$sp
	addi		r11,r11,32
	stvx		v30,r10,$sp
	stvx		v31,r11,$sp
	li		r11,-4096+255		# 0xfffff0ff
	stw		$vrsave,`$FRAME-6*$SIZE_T-4`($sp)	# save vrsave
	li		$x10,0x10
	$PUSH		r26,`$FRAME-6*$SIZE_T`($sp)
	li		$x20,0x20
	$PUSH		r27,`$FRAME-5*$SIZE_T`($sp)
	li		$x30,0x30
	$PUSH		r28,`$FRAME-4*$SIZE_T`($sp)
	li		$x40,0x40
	$PUSH		r29,`$FRAME-3*$SIZE_T`($sp)
	li		$x50,0x50
	$PUSH		r30,`$FRAME-2*$SIZE_T`($sp)
	li		$x60,0x60
	$PUSH		r31,`$FRAME-1*$SIZE_T`($sp)
	li		$x70,0x70
	$PUSH		$lrsave,`$FRAME+$LRSAVE`($sp)
	mtspr		256,r11

	bl		LPICmeup
	addi		$offload,$sp,`8*$SIZE_T+15`
___
$code.=<<___		if ($LENDIAN);
	li		$idx,8
	lvsl		$lemask,0,$idx
	vspltisb	$Ki,0x0f
	vxor		$lemask,$lemask,$Ki
___
$code.=<<___		if ($SZ==4);
	lvx_4w		$A,$x00,$ctx
	lvx_4w		$E,$x10,$ctx
	vsldoi		$B,$A,$A,4		# unpack
	vsldoi		$C,$A,$A,8
	vsldoi		$D,$A,$A,12
	vsldoi		$F,$E,$E,4
	vsldoi		$G,$E,$E,8
	vsldoi		$H,$E,$E,12
___
$code.=<<___		if ($SZ==8);
	lvx_u		$A,$x00,$ctx
	lvx_u		$C,$x10,$ctx
	lvx_u		$E,$x20,$ctx
	vsldoi		$B,$A,$A,8		# unpack
	lvx_u		$G,$x30,$ctx
	vsldoi		$D,$C,$C,8
	vsldoi		$F,$E,$E,8
	vsldoi		$H,$G,$G,8
___
$code.=<<___;
	li		r0,`($rounds-16)/16`	# inner loop counter
	b		Loop
.align	5
Loop:
	lvx		$Ki,$x00,$Tbl
	lvx_u		@X[0],0,$inp
	addi		$inp,$inp,16
	mr		$idx,$Tbl		# copy $Tbl
	stvx		$A,$x00,$offload	# offload $A-$H
	stvx		$B,$x10,$offload
	stvx		$C,$x20,$offload
	stvx		$D,$x30,$offload
	stvx		$E,$x40,$offload
	stvx		$F,$x50,$offload
	stvx		$G,$x60,$offload
	stvx		$H,$x70,$offload
	vaddu${sz}m	$H,$H,$Ki		# h+K[i]
	lvx		$Ki,$x10,$Tbl
___
for ($i=0;$i<16;$i++)	{ &ROUND($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	mtctr		r0
	b		L16_xx
.align	5
L16_xx:
___
for (;$i<32;$i++)	{ &ROUND($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	bdnz		L16_xx

	lvx		@X[2],$x00,$offload
	subic.		$num,$num,1
	lvx		@X[3],$x10,$offload
	vaddu${sz}m	$A,$A,@X[2]
	lvx		@X[4],$x20,$offload
	vaddu${sz}m	$B,$B,@X[3]
	lvx		@X[5],$x30,$offload
	vaddu${sz}m	$C,$C,@X[4]
	lvx		@X[6],$x40,$offload
	vaddu${sz}m	$D,$D,@X[5]
	lvx		@X[7],$x50,$offload
	vaddu${sz}m	$E,$E,@X[6]
	lvx		@X[8],$x60,$offload
	vaddu${sz}m	$F,$F,@X[7]
	lvx		@X[9],$x70,$offload
	vaddu${sz}m	$G,$G,@X[8]
	vaddu${sz}m	$H,$H,@X[9]
	bne		Loop
___
$code.=<<___		if ($SZ==4);
	lvx		@X[0],$x20,$idx
	vperm		$A,$A,$B,$Ki		# pack the answer
	lvx		@X[1],$x30,$idx
	vperm		$E,$E,$F,$Ki
	vperm		$A,$A,$C,@X[0]
	vperm		$E,$E,$G,@X[0]
	vperm		$A,$A,$D,@X[1]
	vperm		$E,$E,$H,@X[1]
	stvx_4w		$A,$x00,$ctx
	stvx_4w		$E,$x10,$ctx
___
$code.=<<___		if ($SZ==8);
	vperm		$A,$A,$B,$Ki		# pack the answer
	vperm		$C,$C,$D,$Ki
	vperm		$E,$E,$F,$Ki
	vperm		$G,$G,$H,$Ki
	stvx_u		$A,$x00,$ctx
	stvx_u		$C,$x10,$ctx
	stvx_u		$E,$x20,$ctx
	stvx_u		$G,$x30,$ctx
___
$code.=<<___;
	addi		$offload,$sp,`$LOCALS+15`
	mtlr		$lrsave
	mtspr		256,$vrsave
	lvx		v24,$x00,$offload	# ABI says so
	lvx		v25,$x10,$offload
	lvx		v26,$x20,$offload
	lvx		v27,$x30,$offload
	lvx		v28,$x40,$offload
	lvx		v29,$x50,$offload
	lvx		v30,$x60,$offload
	lvx		v31,$x70,$offload
	$POP		r26,`$FRAME-6*$SIZE_T`($sp)
	$POP		r27,`$FRAME-5*$SIZE_T`($sp)
	$POP		r28,`$FRAME-4*$SIZE_T`($sp)
	$POP		r29,`$FRAME-3*$SIZE_T`($sp)
	$POP		r30,`$FRAME-2*$SIZE_T`($sp)
	$POP		r31,`$FRAME-1*$SIZE_T`($sp)
	addi		$sp,$sp,$FRAME
	blr
	.long		0
	.byte		0,12,4,1,0x80,6,3,0
	.long		0
.size	$func,.-$func
___

# Ugly hack here, because PPC assembler syntax seem to vary too
# much from platforms to platform...
$code.=<<___;
.align	6
LPICmeup:
	mflr	r0
	bcl	20,31,\$+4
	mflr	$Tbl	; vvvvvv "distance" between . and 1st data entry
	addi	$Tbl,$Tbl,`64-8`
	mtlr	r0
	blr
	.long	0
	.byte	0,12,0x14,0,0,0,0,0
	.space	`64-9*4`
___

if ($SZ==8) {
    local *table = sub {
	foreach(@_) { $code.=".quad	$_,$_\n"; }
    };
    table(
	"0x428a2f98d728ae22","0x7137449123ef65cd",
	"0xb5c0fbcfec4d3b2f","0xe9b5dba58189dbbc",
	"0x3956c25bf348b538","0x59f111f1b605d019",
	"0x923f82a4af194f9b","0xab1c5ed5da6d8118",
	"0xd807aa98a3030242","0x12835b0145706fbe",
	"0x243185be4ee4b28c","0x550c7dc3d5ffb4e2",
	"0x72be5d74f27b896f","0x80deb1fe3b1696b1",
	"0x9bdc06a725c71235","0xc19bf174cf692694",
	"0xe49b69c19ef14ad2","0xefbe4786384f25e3",
	"0x0fc19dc68b8cd5b5","0x240ca1cc77ac9c65",
	"0x2de92c6f592b0275","0x4a7484aa6ea6e483",
	"0x5cb0a9dcbd41fbd4","0x76f988da831153b5",
	"0x983e5152ee66dfab","0xa831c66d2db43210",
	"0xb00327c898fb213f","0xbf597fc7beef0ee4",
	"0xc6e00bf33da88fc2","0xd5a79147930aa725",
	"0x06ca6351e003826f","0x142929670a0e6e70",
	"0x27b70a8546d22ffc","0x2e1b21385c26c926",
	"0x4d2c6dfc5ac42aed","0x53380d139d95b3df",
	"0x650a73548baf63de","0x766a0abb3c77b2a8",
	"0x81c2c92e47edaee6","0x92722c851482353b",
	"0xa2bfe8a14cf10364","0xa81a664bbc423001",
	"0xc24b8b70d0f89791","0xc76c51a30654be30",
	"0xd192e819d6ef5218","0xd69906245565a910",
	"0xf40e35855771202a","0x106aa07032bbd1b8",
	"0x19a4c116b8d2d0c8","0x1e376c085141ab53",
	"0x2748774cdf8eeb99","0x34b0bcb5e19b48a8",
	"0x391c0cb3c5c95a63","0x4ed8aa4ae3418acb",
	"0x5b9cca4f7763e373","0x682e6ff3d6b2b8a3",
	"0x748f82ee5defb2fc","0x78a5636f43172f60",
	"0x84c87814a1f0ab72","0x8cc702081a6439ec",
	"0x90befffa23631e28","0xa4506cebde82bde9",
	"0xbef9a3f7b2c67915","0xc67178f2e372532b",
	"0xca273eceea26619c","0xd186b8c721c0c207",
	"0xeada7dd6cde0eb1e","0xf57d4f7fee6ed178",
	"0x06f067aa72176fba","0x0a637dc5a2c898a6",
	"0x113f9804bef90dae","0x1b710b35131c471b",
	"0x28db77f523047d84","0x32caab7b40c72493",
	"0x3c9ebe0a15c9bebc","0x431d67c49c100d4c",
	"0x4cc5d4becb3e42b6","0x597f299cfc657e2a",
	"0x5fcb6fab3ad6faec","0x6c44198c4a475817","0");
$code.=<<___	if (!$LENDIAN);
.quad	0x0001020304050607,0x1011121314151617
___
$code.=<<___	if ($LENDIAN);	# quad-swapped
.quad	0x1011121314151617,0x0001020304050607
___
} else {
    local *table = sub {
	foreach(@_) { $code.=".long	$_,$_,$_,$_\n"; }
    };
    table(
	"0x428a2f98","0x71374491","0xb5c0fbcf","0xe9b5dba5",
	"0x3956c25b","0x59f111f1","0x923f82a4","0xab1c5ed5",
	"0xd807aa98","0x12835b01","0x243185be","0x550c7dc3",
	"0x72be5d74","0x80deb1fe","0x9bdc06a7","0xc19bf174",
	"0xe49b69c1","0xefbe4786","0x0fc19dc6","0x240ca1cc",
	"0x2de92c6f","0x4a7484aa","0x5cb0a9dc","0x76f988da",
	"0x983e5152","0xa831c66d","0xb00327c8","0xbf597fc7",
	"0xc6e00bf3","0xd5a79147","0x06ca6351","0x14292967",
	"0x27b70a85","0x2e1b2138","0x4d2c6dfc","0x53380d13",
	"0x650a7354","0x766a0abb","0x81c2c92e","0x92722c85",
	"0xa2bfe8a1","0xa81a664b","0xc24b8b70","0xc76c51a3",
	"0xd192e819","0xd6990624","0xf40e3585","0x106aa070",
	"0x19a4c116","0x1e376c08","0x2748774c","0x34b0bcb5",
	"0x391c0cb3","0x4ed8aa4a","0x5b9cca4f","0x682e6ff3",
	"0x748f82ee","0x78a5636f","0x84c87814","0x8cc70208",
	"0x90befffa","0xa4506ceb","0xbef9a3f7","0xc67178f2","0");
$code.=<<___	if (!$LENDIAN);
.long	0x00010203,0x10111213,0x10111213,0x10111213
.long	0x00010203,0x04050607,0x10111213,0x10111213
.long	0x00010203,0x04050607,0x08090a0b,0x10111213
___
$code.=<<___	if ($LENDIAN);	# word-swapped
.long	0x10111213,0x10111213,0x10111213,0x00010203
.long	0x10111213,0x10111213,0x04050607,0x00010203
.long	0x10111213,0x08090a0b,0x04050607,0x00010203
___
}
$code.=<<___;
.asciz	"SHA${bits} for PowerISA 2.07, CRYPTOGAMS by <appro\@openssl.org>"
.align	2
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

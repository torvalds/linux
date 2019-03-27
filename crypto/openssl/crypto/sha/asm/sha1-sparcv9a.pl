#! /usr/bin/env perl
# Copyright 2009-2016 The OpenSSL Project Authors. All Rights Reserved.
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

# January 2009
#
# Provided that UltraSPARC VIS instructions are pipe-lined(*) and
# pairable(*) with IALU ones, offloading of Xupdate to the UltraSPARC
# Graphic Unit would make it possible to achieve higher instruction-
# level parallelism, ILP, and thus higher performance. It should be
# explicitly noted that ILP is the keyword, and it means that this
# code would be unsuitable for cores like UltraSPARC-Tx. The idea is
# not really novel, Sun had VIS-powered implementation for a while.
# Unlike Sun's implementation this one can process multiple unaligned
# input blocks, and as such works as drop-in replacement for OpenSSL
# sha1_block_data_order. Performance improvement was measured to be
# 40% over pure IALU sha1-sparcv9.pl on UltraSPARC-IIi, but 12% on
# UltraSPARC-III. See below for discussion...
#
# The module does not present direct interest for OpenSSL, because
# it doesn't provide better performance on contemporary SPARCv9 CPUs,
# UltraSPARC-Tx and SPARC64-V[II] to be specific. Those who feel they
# absolutely must score on UltraSPARC-I-IV can simply replace
# crypto/sha/asm/sha1-sparcv9.pl with this module.
#
# (*)	"Pipe-lined" means that even if it takes several cycles to
#	complete, next instruction using same functional unit [but not
#	depending on the result of the current instruction] can start
#	execution without having to wait for the unit. "Pairable"
#	means that two [or more] independent instructions can be
#	issued at the very same time.

$bits=32;
for (@ARGV)	{ $bits=64 if (/\-m64/ || /\-xarch\=v9/); }
if ($bits==64)	{ $bias=2047; $frame=192; }
else		{ $bias=0;    $frame=112; }

$output=shift;
open STDOUT,">$output";

$ctx="%i0";
$inp="%i1";
$len="%i2";
$tmp0="%i3";
$tmp1="%i4";
$tmp2="%i5";
$tmp3="%g5";

$base="%g1";
$align="%g4";
$Xfer="%o5";
$nXfer=$tmp3;
$Xi="%o7";

$A="%l0";
$B="%l1";
$C="%l2";
$D="%l3";
$E="%l4";
@V=($A,$B,$C,$D,$E);

$Actx="%o0";
$Bctx="%o1";
$Cctx="%o2";
$Dctx="%o3";
$Ectx="%o4";

$fmul="%f32";
$VK_00_19="%f34";
$VK_20_39="%f36";
$VK_40_59="%f38";
$VK_60_79="%f40";
@VK=($VK_00_19,$VK_20_39,$VK_40_59,$VK_60_79);
@X=("%f0", "%f1", "%f2", "%f3", "%f4", "%f5", "%f6", "%f7",
    "%f8", "%f9","%f10","%f11","%f12","%f13","%f14","%f15","%f16");

# This is reference 2x-parallelized VIS-powered Xupdate procedure. It
# covers even K_NN_MM addition...
sub Xupdate {
my ($i)=@_;
my $K=@VK[($i+16)/20];
my $j=($i+16)%16;

#	[ provided that GSR.alignaddr_offset is 5, $mul contains
#	  0x100ULL<<32|0x100 value and K_NN_MM are pre-loaded to
#	  chosen registers... ]
$code.=<<___;
	fxors		@X[($j+13)%16],@X[$j],@X[$j]	!-1/-1/-1:X[0]^=X[13]
	fxors		@X[($j+14)%16],@X[$j+1],@X[$j+1]! 0/ 0/ 0:X[1]^=X[14]
	fxor		@X[($j+2)%16],@X[($j+8)%16],%f18! 1/ 1/ 1:Tmp=X[2,3]^X[8,9]
	fxor		%f18,@X[$j],@X[$j]		! 2/ 4/ 3:X[0,1]^=X[2,3]^X[8,9]
	faligndata	@X[$j],@X[$j],%f18		! 3/ 7/ 5:Tmp=X[0,1]>>>24
	fpadd32		@X[$j],@X[$j],@X[$j]		! 4/ 8/ 6:X[0,1]<<=1
	fmul8ulx16	%f18,$fmul,%f18			! 5/10/ 7:Tmp>>=7, Tmp&=1
	![fxors		%f15,%f2,%f2]
	for		%f18,@X[$j],@X[$j]		! 8/14/10:X[0,1]|=Tmp
	![fxors		%f0,%f3,%f3]			!10/17/12:X[0] dependency
	fpadd32		$K,@X[$j],%f20
	std		%f20,[$Xfer+`4*$j`]
___
# The numbers delimited with slash are the earliest possible dispatch
# cycles for given instruction assuming 1 cycle latency for simple VIS
# instructions, such as on UltraSPARC-I&II, 3 cycles latency, such as
# on UltraSPARC-III&IV, and 2 cycles latency(*), respectively. Being
# 2x-parallelized the procedure is "worth" 5, 8.5 or 6 ticks per SHA1
# round. As [long as] FPU/VIS instructions are perfectly pairable with
# IALU ones, the round timing is defined by the maximum between VIS
# and IALU timings. The latter varies from round to round and averages
# out at 6.25 ticks. This means that USI&II should operate at IALU
# rate, while USIII&IV - at VIS rate. This explains why performance
# improvement varies among processors. Well, given that pure IALU
# sha1-sparcv9.pl module exhibits virtually uniform performance of
# ~9.3 cycles per SHA1 round. Timings mentioned above are theoretical
# lower limits. Real-life performance was measured to be 6.6 cycles
# per SHA1 round on USIIi and 8.3 on USIII. The latter is lower than
# half-round VIS timing, because there are 16 Xupdate-free rounds,
# which "push down" average theoretical timing to 8 cycles...

# (*)	SPARC64-V[II] was originally believed to have 2 cycles VIS
#	latency. Well, it might have, but it doesn't have dedicated
#	VIS-unit. Instead, VIS instructions are executed by other
#	functional units, ones used here - by IALU. This doesn't
#	improve effective ILP...
}

# The reference Xupdate procedure is then "strained" over *pairs* of
# BODY_NN_MM and kind of modulo-scheduled in respect to X[n]^=X[n+13]
# and K_NN_MM addition. It's "running" 15 rounds ahead, which leaves
# plenty of room to amortize for read-after-write hazard, as well as
# to fetch and align input for the next spin. The VIS instructions are
# scheduled for latency of 2 cycles, because there are not enough IALU
# instructions to schedule for latency of 3, while scheduling for 1
# would give no gain on USI&II anyway.

sub BODY_00_19 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i&~1;
my $k=($j+16+2)%16;	# ahead reference
my $l=($j+16-2)%16;	# behind reference
my $K=@VK[($j+16-2)/20];

$j=($j+16)%16;

$code.=<<___ if (!($i&1));
	sll		$a,5,$tmp0			!! $i
	and		$c,$b,$tmp3
	ld		[$Xfer+`4*($i%16)`],$Xi
	 fxors		@X[($j+14)%16],@X[$j+1],@X[$j+1]! 0/ 0/ 0:X[1]^=X[14]
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	 fxor		@X[($j+2)%16],@X[($j+8)%16],%f18! 1/ 1/ 1:Tmp=X[2,3]^X[8,9]
	sll		$b,30,$tmp2
	add		$tmp1,$e,$e
	andn		$d,$b,$tmp1
	add		$Xi,$e,$e
	 fxor		%f18,@X[$j],@X[$j]		! 2/ 4/ 3:X[0,1]^=X[2,3]^X[8,9]
	srl		$b,2,$b
	or		$tmp1,$tmp3,$tmp1
	or		$tmp2,$b,$b
	add		$tmp1,$e,$e
	 faligndata	@X[$j],@X[$j],%f18		! 3/ 7/ 5:Tmp=X[0,1]>>>24
___
$code.=<<___ if ($i&1);
	sll		$a,5,$tmp0			!! $i
	and		$c,$b,$tmp3
	ld		[$Xfer+`4*($i%16)`],$Xi
	 fpadd32	@X[$j],@X[$j],@X[$j]		! 4/ 8/ 6:X[0,1]<<=1
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	 fmul8ulx16	%f18,$fmul,%f18			! 5/10/ 7:Tmp>>=7, Tmp&=1
	sll		$b,30,$tmp2
	add		$tmp1,$e,$e
	 fpadd32	$K,@X[$l],%f20			!
	andn		$d,$b,$tmp1
	add		$Xi,$e,$e
	 fxors		@X[($k+13)%16],@X[$k],@X[$k]	!-1/-1/-1:X[0]^=X[13]
	srl		$b,2,$b
	or		$tmp1,$tmp3,$tmp1
	 fxor		%f18,@X[$j],@X[$j]		! 8/14/10:X[0,1]|=Tmp
	or		$tmp2,$b,$b
	add		$tmp1,$e,$e
___
$code.=<<___ if ($i&1 && $i>=2);
	 std		%f20,[$Xfer+`4*$l`]		!
___
}

sub BODY_20_39 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i&~1;
my $k=($j+16+2)%16;	# ahead reference
my $l=($j+16-2)%16;	# behind reference
my $K=@VK[($j+16-2)/20];

$j=($j+16)%16;

$code.=<<___ if (!($i&1) && $i<64);
	sll		$a,5,$tmp0			!! $i
	ld		[$Xfer+`4*($i%16)`],$Xi
	 fxors		@X[($j+14)%16],@X[$j+1],@X[$j+1]! 0/ 0/ 0:X[1]^=X[14]
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	 fxor		@X[($j+2)%16],@X[($j+8)%16],%f18! 1/ 1/ 1:Tmp=X[2,3]^X[8,9]
	xor		$c,$b,$tmp0
	add		$tmp1,$e,$e
	sll		$b,30,$tmp2
	xor		$d,$tmp0,$tmp1
	 fxor		%f18,@X[$j],@X[$j]		! 2/ 4/ 3:X[0,1]^=X[2,3]^X[8,9]
	srl		$b,2,$b
	add		$tmp1,$e,$e
	or		$tmp2,$b,$b
	add		$Xi,$e,$e
	 faligndata	@X[$j],@X[$j],%f18		! 3/ 7/ 5:Tmp=X[0,1]>>>24
___
$code.=<<___ if ($i&1 && $i<64);
	sll		$a,5,$tmp0			!! $i
	ld		[$Xfer+`4*($i%16)`],$Xi
	 fpadd32	@X[$j],@X[$j],@X[$j]		! 4/ 8/ 6:X[0,1]<<=1
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	 fmul8ulx16	%f18,$fmul,%f18			! 5/10/ 7:Tmp>>=7, Tmp&=1
	xor		$c,$b,$tmp0
	add		$tmp1,$e,$e
	 fpadd32	$K,@X[$l],%f20			!
	sll		$b,30,$tmp2
	xor		$d,$tmp0,$tmp1
	 fxors		@X[($k+13)%16],@X[$k],@X[$k]	!-1/-1/-1:X[0]^=X[13]
	srl		$b,2,$b
	add		$tmp1,$e,$e
	 fxor		%f18,@X[$j],@X[$j]		! 8/14/10:X[0,1]|=Tmp
	or		$tmp2,$b,$b
	add		$Xi,$e,$e
	 std		%f20,[$Xfer+`4*$l`]		!
___
$code.=<<___ if ($i==64);
	sll		$a,5,$tmp0			!! $i
	ld		[$Xfer+`4*($i%16)`],$Xi
	 fpadd32	$K,@X[$l],%f20
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	xor		$c,$b,$tmp0
	add		$tmp1,$e,$e
	sll		$b,30,$tmp2
	xor		$d,$tmp0,$tmp1
	 std		%f20,[$Xfer+`4*$l`]
	srl		$b,2,$b
	add		$tmp1,$e,$e
	or		$tmp2,$b,$b
	add		$Xi,$e,$e
___
$code.=<<___ if ($i>64);
	sll		$a,5,$tmp0			!! $i
	ld		[$Xfer+`4*($i%16)`],$Xi
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	xor		$c,$b,$tmp0
	add		$tmp1,$e,$e
	sll		$b,30,$tmp2
	xor		$d,$tmp0,$tmp1
	srl		$b,2,$b
	add		$tmp1,$e,$e
	or		$tmp2,$b,$b
	add		$Xi,$e,$e
___
}

sub BODY_40_59 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i&~1;
my $k=($j+16+2)%16;	# ahead reference
my $l=($j+16-2)%16;	# behind reference
my $K=@VK[($j+16-2)/20];

$j=($j+16)%16;

$code.=<<___ if (!($i&1));
	sll		$a,5,$tmp0			!! $i
	ld		[$Xfer+`4*($i%16)`],$Xi
	 fxors		@X[($j+14)%16],@X[$j+1],@X[$j+1]! 0/ 0/ 0:X[1]^=X[14]
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	 fxor		@X[($j+2)%16],@X[($j+8)%16],%f18! 1/ 1/ 1:Tmp=X[2,3]^X[8,9]
	and		$c,$b,$tmp0
	add		$tmp1,$e,$e
	sll		$b,30,$tmp2
	or		$c,$b,$tmp1
	 fxor		%f18,@X[$j],@X[$j]		! 2/ 4/ 3:X[0,1]^=X[2,3]^X[8,9]
	srl		$b,2,$b
	and		$d,$tmp1,$tmp1
	add		$Xi,$e,$e
	or		$tmp1,$tmp0,$tmp1
	 faligndata	@X[$j],@X[$j],%f18		! 3/ 7/ 5:Tmp=X[0,1]>>>24
	or		$tmp2,$b,$b
	add		$tmp1,$e,$e
	 fpadd32	@X[$j],@X[$j],@X[$j]		! 4/ 8/ 6:X[0,1]<<=1
___
$code.=<<___ if ($i&1);
	sll		$a,5,$tmp0			!! $i
	ld		[$Xfer+`4*($i%16)`],$Xi
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	 fmul8ulx16	%f18,$fmul,%f18			! 5/10/ 7:Tmp>>=7, Tmp&=1
	and		$c,$b,$tmp0
	add		$tmp1,$e,$e
	 fpadd32	$K,@X[$l],%f20			!
	sll		$b,30,$tmp2
	or		$c,$b,$tmp1
	 fxors		@X[($k+13)%16],@X[$k],@X[$k]	!-1/-1/-1:X[0]^=X[13]
	srl		$b,2,$b
	and		$d,$tmp1,$tmp1
	 fxor		%f18,@X[$j],@X[$j]		! 8/14/10:X[0,1]|=Tmp
	add		$Xi,$e,$e
	or		$tmp1,$tmp0,$tmp1
	or		$tmp2,$b,$b
	add		$tmp1,$e,$e
	 std		%f20,[$Xfer+`4*$l`]		!
___
}

# If there is more data to process, then we pre-fetch the data for
# next iteration in last ten rounds...
sub BODY_70_79 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i&~1;
my $m=($i%8)*2;

$j=($j+16)%16;

$code.=<<___ if ($i==70);
	sll		$a,5,$tmp0			!! $i
	ld		[$Xfer+`4*($i%16)`],$Xi
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	 ldd		[$inp+64],@X[0]
	xor		$c,$b,$tmp0
	add		$tmp1,$e,$e
	sll		$b,30,$tmp2
	xor		$d,$tmp0,$tmp1
	srl		$b,2,$b
	add		$tmp1,$e,$e
	or		$tmp2,$b,$b
	add		$Xi,$e,$e

	and		$inp,-64,$nXfer
	inc		64,$inp
	and		$nXfer,255,$nXfer
	alignaddr	%g0,$align,%g0
	add		$base,$nXfer,$nXfer
___
$code.=<<___ if ($i==71);
	sll		$a,5,$tmp0			!! $i
	ld		[$Xfer+`4*($i%16)`],$Xi
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	xor		$c,$b,$tmp0
	add		$tmp1,$e,$e
	sll		$b,30,$tmp2
	xor		$d,$tmp0,$tmp1
	srl		$b,2,$b
	add		$tmp1,$e,$e
	or		$tmp2,$b,$b
	add		$Xi,$e,$e
___
$code.=<<___ if ($i>=72);
	 faligndata	@X[$m],@X[$m+2],@X[$m]
	sll		$a,5,$tmp0			!! $i
	ld		[$Xfer+`4*($i%16)`],$Xi
	srl		$a,27,$tmp1
	add		$tmp0,$e,$e
	xor		$c,$b,$tmp0
	add		$tmp1,$e,$e
	 fpadd32	$VK_00_19,@X[$m],%f20
	sll		$b,30,$tmp2
	xor		$d,$tmp0,$tmp1
	srl		$b,2,$b
	add		$tmp1,$e,$e
	or		$tmp2,$b,$b
	add		$Xi,$e,$e
___
$code.=<<___ if ($i<77);
	 ldd		[$inp+`8*($i+1-70)`],@X[2*($i+1-70)]
___
$code.=<<___ if ($i==77);	# redundant if $inp was aligned
	 add		$align,63,$tmp0
	 and		$tmp0,-8,$tmp0
	 ldd		[$inp+$tmp0],@X[16]
___
$code.=<<___ if ($i>=72);
	 std		%f20,[$nXfer+`4*$m`]
___
}

$code.=<<___;
.section	".text",#alloc,#execinstr

.align	64
vis_const:
.long	0x5a827999,0x5a827999	! K_00_19
.long	0x6ed9eba1,0x6ed9eba1	! K_20_39
.long	0x8f1bbcdc,0x8f1bbcdc	! K_40_59
.long	0xca62c1d6,0xca62c1d6	! K_60_79
.long	0x00000100,0x00000100
.align	64
.type	vis_const,#object
.size	vis_const,(.-vis_const)

.globl	sha1_block_data_order
sha1_block_data_order:
	save	%sp,-$frame,%sp
	add	%fp,$bias-256,$base

1:	call	.+8
	add	%o7,vis_const-1b,$tmp0

	ldd	[$tmp0+0],$VK_00_19
	ldd	[$tmp0+8],$VK_20_39
	ldd	[$tmp0+16],$VK_40_59
	ldd	[$tmp0+24],$VK_60_79
	ldd	[$tmp0+32],$fmul

	ld	[$ctx+0],$Actx
	and	$base,-256,$base
	ld	[$ctx+4],$Bctx
	sub	$base,$bias+$frame,%sp
	ld	[$ctx+8],$Cctx
	and	$inp,7,$align
	ld	[$ctx+12],$Dctx
	and	$inp,-8,$inp
	ld	[$ctx+16],$Ectx

	! X[16] is maintained in FP register bank
	alignaddr	%g0,$align,%g0
	ldd		[$inp+0],@X[0]
	sub		$inp,-64,$Xfer
	ldd		[$inp+8],@X[2]
	and		$Xfer,-64,$Xfer
	ldd		[$inp+16],@X[4]
	and		$Xfer,255,$Xfer
	ldd		[$inp+24],@X[6]
	add		$base,$Xfer,$Xfer
	ldd		[$inp+32],@X[8]
	ldd		[$inp+40],@X[10]
	ldd		[$inp+48],@X[12]
	brz,pt		$align,.Laligned
	ldd		[$inp+56],@X[14]

	ldd		[$inp+64],@X[16]
	faligndata	@X[0],@X[2],@X[0]
	faligndata	@X[2],@X[4],@X[2]
	faligndata	@X[4],@X[6],@X[4]
	faligndata	@X[6],@X[8],@X[6]
	faligndata	@X[8],@X[10],@X[8]
	faligndata	@X[10],@X[12],@X[10]
	faligndata	@X[12],@X[14],@X[12]
	faligndata	@X[14],@X[16],@X[14]

.Laligned:
	mov		5,$tmp0
	dec		1,$len
	alignaddr	%g0,$tmp0,%g0
	fpadd32		$VK_00_19,@X[0],%f16
	fpadd32		$VK_00_19,@X[2],%f18
	fpadd32		$VK_00_19,@X[4],%f20
	fpadd32		$VK_00_19,@X[6],%f22
	fpadd32		$VK_00_19,@X[8],%f24
	fpadd32		$VK_00_19,@X[10],%f26
	fpadd32		$VK_00_19,@X[12],%f28
	fpadd32		$VK_00_19,@X[14],%f30
	std		%f16,[$Xfer+0]
	mov		$Actx,$A
	std		%f18,[$Xfer+8]
	mov		$Bctx,$B
	std		%f20,[$Xfer+16]
	mov		$Cctx,$C
	std		%f22,[$Xfer+24]
	mov		$Dctx,$D
	std		%f24,[$Xfer+32]
	mov		$Ectx,$E
	std		%f26,[$Xfer+40]
	fxors		@X[13],@X[0],@X[0]
	std		%f28,[$Xfer+48]
	ba		.Loop
	std		%f30,[$Xfer+56]
.align	32
.Loop:
___
for ($i=0;$i<20;$i++)	{ &BODY_00_19($i,@V); unshift(@V,pop(@V)); }
for (;$i<40;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
for (;$i<60;$i++)	{ &BODY_40_59($i,@V); unshift(@V,pop(@V)); }
for (;$i<70;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	tst		$len
	bz,pn		`$bits==32?"%icc":"%xcc"`,.Ltail
	nop
___
for (;$i<80;$i++)	{ &BODY_70_79($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	add		$A,$Actx,$Actx
	add		$B,$Bctx,$Bctx
	add		$C,$Cctx,$Cctx
	add		$D,$Dctx,$Dctx
	add		$E,$Ectx,$Ectx
	mov		5,$tmp0
	fxors		@X[13],@X[0],@X[0]
	mov		$Actx,$A
	mov		$Bctx,$B
	mov		$Cctx,$C
	mov		$Dctx,$D
	mov		$Ectx,$E
	alignaddr	%g0,$tmp0,%g0
	dec		1,$len
	ba		.Loop
	mov		$nXfer,$Xfer

.align	32
.Ltail:
___
for($i=70;$i<80;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	add	$A,$Actx,$Actx
	add	$B,$Bctx,$Bctx
	add	$C,$Cctx,$Cctx
	add	$D,$Dctx,$Dctx
	add	$E,$Ectx,$Ectx

	st	$Actx,[$ctx+0]
	st	$Bctx,[$ctx+4]
	st	$Cctx,[$ctx+8]
	st	$Dctx,[$ctx+12]
	st	$Ectx,[$ctx+16]

	ret
	restore
.type	sha1_block_data_order,#function
.size	sha1_block_data_order,(.-sha1_block_data_order)
.asciz	"SHA1 block transform for SPARCv9a, CRYPTOGAMS by <appro\@openssl.org>"
.align	4
___

# Purpose of these subroutines is to explicitly encode VIS instructions,
# so that one can compile the module without having to specify VIS
# extensions on compiler command line, e.g. -xarch=v9 vs. -xarch=v9a.
# Idea is to reserve for option to produce "universal" binary and let
# programmer detect if current CPU is VIS capable at run-time.
sub unvis {
my ($mnemonic,$rs1,$rs2,$rd)=@_;
my ($ref,$opf);
my %visopf = (	"fmul8ulx16"	=> 0x037,
		"faligndata"	=> 0x048,
		"fpadd32"	=> 0x052,
		"fxor"		=> 0x06c,
		"fxors"		=> 0x06d	);

    $ref = "$mnemonic\t$rs1,$rs2,$rd";

    if ($opf=$visopf{$mnemonic}) {
	foreach ($rs1,$rs2,$rd) {
	    return $ref if (!/%f([0-9]{1,2})/);
	    $_=$1;
	    if ($1>=32) {
		return $ref if ($1&1);
		# re-encode for upper double register addressing
		$_=($1|$1>>5)&31;
	    }
	}

	return	sprintf ".word\t0x%08x !%s",
			0x81b00000|$rd<<25|$rs1<<14|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}
sub unalignaddr {
my ($mnemonic,$rs1,$rs2,$rd)=@_;
my %bias = ( "g" => 0, "o" => 8, "l" => 16, "i" => 24 );
my $ref="$mnemonic\t$rs1,$rs2,$rd";

    foreach ($rs1,$rs2,$rd) {
	if (/%([goli])([0-7])/)	{ $_=$bias{$1}+$2; }
	else			{ return $ref; }
    }
    return  sprintf ".word\t0x%08x !%s",
		    0x81b00300|$rd<<25|$rs1<<14|$rs2,
		    $ref;
}

$code =~ s/\`([^\`]*)\`/eval $1/gem;
$code =~ s/\b(f[^\s]*)\s+(%f[0-9]{1,2}),(%f[0-9]{1,2}),(%f[0-9]{1,2})/
		&unvis($1,$2,$3,$4)
	  /gem;
$code =~ s/\b(alignaddr)\s+(%[goli][0-7]),(%[goli][0-7]),(%[goli][0-7])/
		&unalignaddr($1,$2,$3,$4)
	  /gem;
print $code;
close STDOUT;

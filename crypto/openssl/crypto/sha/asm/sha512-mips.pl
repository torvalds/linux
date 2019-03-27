#! /usr/bin/env perl
# Copyright 2010-2018 The OpenSSL Project Authors. All Rights Reserved.
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

# SHA2 block procedures for MIPS.

# October 2010.
#
# SHA256 performance improvement on MIPS R5000 CPU is ~27% over gcc-
# generated code in o32 build and ~55% in n32/64 build. SHA512 [which
# for now can only be compiled for MIPS64 ISA] improvement is modest
# ~17%, but it comes for free, because it's same instruction sequence.
# Improvement coefficients are for aligned input.

# September 2012.
#
# Add MIPS[32|64]R2 code (>25% less instructions).

######################################################################
# There is a number of MIPS ABI in use, O32 and N32/64 are most
# widely used. Then there is a new contender: NUBI. It appears that if
# one picks the latter, it's possible to arrange code in ABI neutral
# manner. Therefore let's stick to NUBI register layout:
#
($zero,$at,$t0,$t1,$t2)=map("\$$_",(0..2,24,25));
($a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7)=map("\$$_",(4..11));
($s0,$s1,$s2,$s3,$s4,$s5,$s6,$s7,$s8,$s9,$s10,$s11)=map("\$$_",(12..23));
($gp,$tp,$sp,$fp,$ra)=map("\$$_",(3,28..31));
#
# The return value is placed in $a0. Following coding rules facilitate
# interoperability:
#
# - never ever touch $tp, "thread pointer", former $gp [o32 can be
#   excluded from the rule, because it's specified volatile];
# - copy return value to $t0, former $v0 [or to $a0 if you're adapting
#   old code];
# - on O32 populate $a4-$a7 with 'lw $aN,4*N($sp)' if necessary;
#
# For reference here is register layout for N32/64 MIPS ABIs:
#
# ($zero,$at,$v0,$v1)=map("\$$_",(0..3));
# ($a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7)=map("\$$_",(4..11));
# ($t0,$t1,$t2,$t3,$t8,$t9)=map("\$$_",(12..15,24,25));
# ($s0,$s1,$s2,$s3,$s4,$s5,$s6,$s7)=map("\$$_",(16..23));
# ($gp,$sp,$fp,$ra)=map("\$$_",(28..31));
#
$flavour = shift || "o32"; # supported flavours are o32,n32,64,nubi32,nubi64

if ($flavour =~ /64|n32/i) {
	$PTR_LA="dla";
	$PTR_ADD="daddu";	# incidentally works even on n32
	$PTR_SUB="dsubu";	# incidentally works even on n32
	$REG_S="sd";
	$REG_L="ld";
	$PTR_SLL="dsll";	# incidentally works even on n32
	$SZREG=8;
} else {
	$PTR_LA="la";
	$PTR_ADD="addu";
	$PTR_SUB="subu";
	$REG_S="sw";
	$REG_L="lw";
	$PTR_SLL="sll";
	$SZREG=4;
}
$pf = ($flavour =~ /nubi/i) ? $t0 : $t2;
#
# <appro@openssl.org>
#
######################################################################

$big_endian=(`echo MIPSEB | $ENV{CC} -E -`=~/MIPSEB/)?0:1 if ($ENV{CC});

for (@ARGV) {	$output=$_ if (/\w[\w\-]*\.\w+$/);	}
open STDOUT,">$output";

if (!defined($big_endian)) { $big_endian=(unpack('L',pack('N',1))==1); }

if ($output =~ /512/) {
	$label="512";
	$SZ=8;
	$LD="ld";		# load from memory
	$ST="sd";		# store to memory
	$SLL="dsll";		# shift left logical
	$SRL="dsrl";		# shift right logical
	$ADDU="daddu";
	$ROTR="drotr";
	@Sigma0=(28,34,39);
	@Sigma1=(14,18,41);
	@sigma0=( 7, 1, 8);	# right shift first
	@sigma1=( 6,19,61);	# right shift first
	$lastK=0x817;
	$rounds=80;
} else {
	$label="256";
	$SZ=4;
	$LD="lw";		# load from memory
	$ST="sw";		# store to memory
	$SLL="sll";		# shift left logical
	$SRL="srl";		# shift right logical
	$ADDU="addu";
	$ROTR="rotr";
	@Sigma0=( 2,13,22);
	@Sigma1=( 6,11,25);
	@sigma0=( 3, 7,18);	# right shift first
	@sigma1=(10,17,19);	# right shift first
	$lastK=0x8f2;
	$rounds=64;
}

$MSB = $big_endian ? 0 : ($SZ-1);
$LSB = ($SZ-1)&~$MSB;

@V=($A,$B,$C,$D,$E,$F,$G,$H)=map("\$$_",(1,2,3,7,24,25,30,31));
@X=map("\$$_",(8..23));

$ctx=$a0;
$inp=$a1;
$len=$a2;	$Ktbl=$len;

sub BODY_00_15 {
my ($i,$a,$b,$c,$d,$e,$f,$g,$h)=@_;
my ($T1,$tmp0,$tmp1,$tmp2)=(@X[4],@X[5],@X[6],@X[7]);

$code.=<<___ if ($i<15);
#if defined(_MIPS_ARCH_MIPS32R6) || defined(_MIPS_ARCH_MIPS64R6)
	${LD}	@X[1],`($i+1)*$SZ`($inp)
#else
	${LD}l	@X[1],`($i+1)*$SZ+$MSB`($inp)
	${LD}r	@X[1],`($i+1)*$SZ+$LSB`($inp)
#endif
___
$code.=<<___	if (!$big_endian && $i<16 && $SZ==4);
#if defined(_MIPS_ARCH_MIPS32R2) || defined(_MIPS_ARCH_MIPS64R2)
	wsbh	@X[0],@X[0]		# byte swap($i)
	rotr	@X[0],@X[0],16
#else
	srl	$tmp0,@X[0],24		# byte swap($i)
	srl	$tmp1,@X[0],8
	andi	$tmp2,@X[0],0xFF00
	sll	@X[0],@X[0],24
	andi	$tmp1,0xFF00
	sll	$tmp2,$tmp2,8
	or	@X[0],$tmp0
	or	$tmp1,$tmp2
	or	@X[0],$tmp1
#endif
___
$code.=<<___	if (!$big_endian && $i<16 && $SZ==8);
#if defined(_MIPS_ARCH_MIPS64R2)
	dsbh	@X[0],@X[0]		# byte swap($i)
	dshd	@X[0],@X[0]
#else
	ori	$tmp0,$zero,0xFF
	dsll	$tmp2,$tmp0,32
	or	$tmp0,$tmp2		# 0x000000FF000000FF
	and	$tmp1,@X[0],$tmp0	# byte swap($i)
	dsrl	$tmp2,@X[0],24
	dsll	$tmp1,24
	and	$tmp2,$tmp0
	dsll	$tmp0,8			# 0x0000FF000000FF00
	or	$tmp1,$tmp2
	and	$tmp2,@X[0],$tmp0
	dsrl	@X[0],8
	dsll	$tmp2,8
	and	@X[0],$tmp0
	or	$tmp1,$tmp2
	or	@X[0],$tmp1
	dsrl	$tmp1,@X[0],32
	dsll	@X[0],32
	or	@X[0],$tmp1
#endif
___
$code.=<<___;
#if defined(_MIPS_ARCH_MIPS32R2) || defined(_MIPS_ARCH_MIPS64R2)
	xor	$tmp2,$f,$g			# $i
	$ROTR	$tmp0,$e,@Sigma1[0]
	$ADDU	$T1,$X[0],$h
	$ROTR	$tmp1,$e,@Sigma1[1]
	and	$tmp2,$e
	$ROTR	$h,$e,@Sigma1[2]
	xor	$tmp0,$tmp1
	$ROTR	$tmp1,$a,@Sigma0[0]
	xor	$tmp2,$g			# Ch(e,f,g)
	xor	$tmp0,$h			# Sigma1(e)

	$ROTR	$h,$a,@Sigma0[1]
	$ADDU	$T1,$tmp2
	$LD	$tmp2,`$i*$SZ`($Ktbl)		# K[$i]
	xor	$h,$tmp1
	$ROTR	$tmp1,$a,@Sigma0[2]
	$ADDU	$T1,$tmp0
	and	$tmp0,$b,$c
	xor	$h,$tmp1			# Sigma0(a)
	xor	$tmp1,$b,$c
#else
	$ADDU	$T1,$X[0],$h			# $i
	$SRL	$h,$e,@Sigma1[0]
	xor	$tmp2,$f,$g
	$SLL	$tmp1,$e,`$SZ*8-@Sigma1[2]`
	and	$tmp2,$e
	$SRL	$tmp0,$e,@Sigma1[1]
	xor	$h,$tmp1
	$SLL	$tmp1,$e,`$SZ*8-@Sigma1[1]`
	xor	$h,$tmp0
	$SRL	$tmp0,$e,@Sigma1[2]
	xor	$h,$tmp1
	$SLL	$tmp1,$e,`$SZ*8-@Sigma1[0]`
	xor	$h,$tmp0
	xor	$tmp2,$g			# Ch(e,f,g)
	xor	$tmp0,$tmp1,$h			# Sigma1(e)

	$SRL	$h,$a,@Sigma0[0]
	$ADDU	$T1,$tmp2
	$LD	$tmp2,`$i*$SZ`($Ktbl)		# K[$i]
	$SLL	$tmp1,$a,`$SZ*8-@Sigma0[2]`
	$ADDU	$T1,$tmp0
	$SRL	$tmp0,$a,@Sigma0[1]
	xor	$h,$tmp1
	$SLL	$tmp1,$a,`$SZ*8-@Sigma0[1]`
	xor	$h,$tmp0
	$SRL	$tmp0,$a,@Sigma0[2]
	xor	$h,$tmp1
	$SLL	$tmp1,$a,`$SZ*8-@Sigma0[0]`
	xor	$h,$tmp0
	and	$tmp0,$b,$c
	xor	$h,$tmp1			# Sigma0(a)
	xor	$tmp1,$b,$c
#endif
	$ST	@X[0],`($i%16)*$SZ`($sp)	# offload to ring buffer
	$ADDU	$h,$tmp0
	and	$tmp1,$a
	$ADDU	$T1,$tmp2			# +=K[$i]
	$ADDU	$h,$tmp1			# +=Maj(a,b,c)
	$ADDU	$d,$T1
	$ADDU	$h,$T1
___
$code.=<<___ if ($i>=13);
	$LD	@X[3],`(($i+3)%16)*$SZ`($sp)	# prefetch from ring buffer
___
}

sub BODY_16_XX {
my $i=@_[0];
my ($tmp0,$tmp1,$tmp2,$tmp3)=(@X[4],@X[5],@X[6],@X[7]);

$code.=<<___;
#if defined(_MIPS_ARCH_MIPS32R2) || defined(_MIPS_ARCH_MIPS64R2)
	$SRL	$tmp2,@X[1],@sigma0[0]		# Xupdate($i)
	$ROTR	$tmp0,@X[1],@sigma0[1]
	$ADDU	@X[0],@X[9]			# +=X[i+9]
	xor	$tmp2,$tmp0
	$ROTR	$tmp0,@X[1],@sigma0[2]

	$SRL	$tmp3,@X[14],@sigma1[0]
	$ROTR	$tmp1,@X[14],@sigma1[1]
	xor	$tmp2,$tmp0			# sigma0(X[i+1])
	$ROTR	$tmp0,@X[14],@sigma1[2]
	xor	$tmp3,$tmp1
	$ADDU	@X[0],$tmp2
#else
	$SRL	$tmp2,@X[1],@sigma0[0]		# Xupdate($i)
	$ADDU	@X[0],@X[9]			# +=X[i+9]
	$SLL	$tmp1,@X[1],`$SZ*8-@sigma0[2]`
	$SRL	$tmp0,@X[1],@sigma0[1]
	xor	$tmp2,$tmp1
	$SLL	$tmp1,`@sigma0[2]-@sigma0[1]`
	xor	$tmp2,$tmp0
	$SRL	$tmp0,@X[1],@sigma0[2]
	xor	$tmp2,$tmp1

	$SRL	$tmp3,@X[14],@sigma1[0]
	xor	$tmp2,$tmp0			# sigma0(X[i+1])
	$SLL	$tmp1,@X[14],`$SZ*8-@sigma1[2]`
	$ADDU	@X[0],$tmp2
	$SRL	$tmp0,@X[14],@sigma1[1]
	xor	$tmp3,$tmp1
	$SLL	$tmp1,`@sigma1[2]-@sigma1[1]`
	xor	$tmp3,$tmp0
	$SRL	$tmp0,@X[14],@sigma1[2]
	xor	$tmp3,$tmp1
#endif
	xor	$tmp3,$tmp0			# sigma1(X[i+14])
	$ADDU	@X[0],$tmp3
___
	&BODY_00_15(@_);
}

$FRAMESIZE=16*$SZ+16*$SZREG;
$SAVED_REGS_MASK = ($flavour =~ /nubi/i) ? "0xc0fff008" : "0xc0ff0000";

$code.=<<___;
#include "mips_arch.h"

.text
.set	noat
#if !defined(__mips_eabi) && (!defined(__vxworks) || defined(__pic__))
.option	pic2
#endif

.align	5
.globl	sha${label}_block_data_order
.ent	sha${label}_block_data_order
sha${label}_block_data_order:
	.frame	$sp,$FRAMESIZE,$ra
	.mask	$SAVED_REGS_MASK,-$SZREG
	.set	noreorder
___
$code.=<<___ if ($flavour =~ /o32/i);	# o32 PIC-ification
	.cpload	$pf
___
$code.=<<___;
	$PTR_SUB $sp,$FRAMESIZE
	$REG_S	$ra,$FRAMESIZE-1*$SZREG($sp)
	$REG_S	$fp,$FRAMESIZE-2*$SZREG($sp)
	$REG_S	$s11,$FRAMESIZE-3*$SZREG($sp)
	$REG_S	$s10,$FRAMESIZE-4*$SZREG($sp)
	$REG_S	$s9,$FRAMESIZE-5*$SZREG($sp)
	$REG_S	$s8,$FRAMESIZE-6*$SZREG($sp)
	$REG_S	$s7,$FRAMESIZE-7*$SZREG($sp)
	$REG_S	$s6,$FRAMESIZE-8*$SZREG($sp)
	$REG_S	$s5,$FRAMESIZE-9*$SZREG($sp)
	$REG_S	$s4,$FRAMESIZE-10*$SZREG($sp)
___
$code.=<<___ if ($flavour =~ /nubi/i);	# optimize non-nubi prologue
	$REG_S	$s3,$FRAMESIZE-11*$SZREG($sp)
	$REG_S	$s2,$FRAMESIZE-12*$SZREG($sp)
	$REG_S	$s1,$FRAMESIZE-13*$SZREG($sp)
	$REG_S	$s0,$FRAMESIZE-14*$SZREG($sp)
	$REG_S	$gp,$FRAMESIZE-15*$SZREG($sp)
___
$code.=<<___;
	$PTR_SLL @X[15],$len,`log(16*$SZ)/log(2)`
___
$code.=<<___ if ($flavour !~ /o32/i);	# non-o32 PIC-ification
	.cplocal	$Ktbl
	.cpsetup	$pf,$zero,sha${label}_block_data_order
___
$code.=<<___;
	.set	reorder
	$PTR_LA	$Ktbl,K${label}		# PIC-ified 'load address'

	$LD	$A,0*$SZ($ctx)		# load context
	$LD	$B,1*$SZ($ctx)
	$LD	$C,2*$SZ($ctx)
	$LD	$D,3*$SZ($ctx)
	$LD	$E,4*$SZ($ctx)
	$LD	$F,5*$SZ($ctx)
	$LD	$G,6*$SZ($ctx)
	$LD	$H,7*$SZ($ctx)

	$PTR_ADD @X[15],$inp		# pointer to the end of input
	$REG_S	@X[15],16*$SZ($sp)
	b	.Loop

.align	5
.Loop:
#if defined(_MIPS_ARCH_MIPS32R6) || defined(_MIPS_ARCH_MIPS64R6)
	${LD}	@X[0],($inp)
#else
	${LD}l	@X[0],$MSB($inp)
	${LD}r	@X[0],$LSB($inp)
#endif
___
for ($i=0;$i<16;$i++)
{ &BODY_00_15($i,@V); unshift(@V,pop(@V)); push(@X,shift(@X)); }
$code.=<<___;
	b	.L16_xx
.align	4
.L16_xx:
___
for (;$i<32;$i++)
{ &BODY_16_XX($i,@V); unshift(@V,pop(@V)); push(@X,shift(@X)); }
$code.=<<___;
	and	@X[6],0xfff
	li	@X[7],$lastK
	.set	noreorder
	bne	@X[6],@X[7],.L16_xx
	$PTR_ADD $Ktbl,16*$SZ		# Ktbl+=16

	$REG_L	@X[15],16*$SZ($sp)	# restore pointer to the end of input
	$LD	@X[0],0*$SZ($ctx)
	$LD	@X[1],1*$SZ($ctx)
	$LD	@X[2],2*$SZ($ctx)
	$PTR_ADD $inp,16*$SZ
	$LD	@X[3],3*$SZ($ctx)
	$ADDU	$A,@X[0]
	$LD	@X[4],4*$SZ($ctx)
	$ADDU	$B,@X[1]
	$LD	@X[5],5*$SZ($ctx)
	$ADDU	$C,@X[2]
	$LD	@X[6],6*$SZ($ctx)
	$ADDU	$D,@X[3]
	$LD	@X[7],7*$SZ($ctx)
	$ADDU	$E,@X[4]
	$ST	$A,0*$SZ($ctx)
	$ADDU	$F,@X[5]
	$ST	$B,1*$SZ($ctx)
	$ADDU	$G,@X[6]
	$ST	$C,2*$SZ($ctx)
	$ADDU	$H,@X[7]
	$ST	$D,3*$SZ($ctx)
	$ST	$E,4*$SZ($ctx)
	$ST	$F,5*$SZ($ctx)
	$ST	$G,6*$SZ($ctx)
	$ST	$H,7*$SZ($ctx)

	bne	$inp,@X[15],.Loop
	$PTR_SUB $Ktbl,`($rounds-16)*$SZ`	# rewind $Ktbl

	$REG_L	$ra,$FRAMESIZE-1*$SZREG($sp)
	$REG_L	$fp,$FRAMESIZE-2*$SZREG($sp)
	$REG_L	$s11,$FRAMESIZE-3*$SZREG($sp)
	$REG_L	$s10,$FRAMESIZE-4*$SZREG($sp)
	$REG_L	$s9,$FRAMESIZE-5*$SZREG($sp)
	$REG_L	$s8,$FRAMESIZE-6*$SZREG($sp)
	$REG_L	$s7,$FRAMESIZE-7*$SZREG($sp)
	$REG_L	$s6,$FRAMESIZE-8*$SZREG($sp)
	$REG_L	$s5,$FRAMESIZE-9*$SZREG($sp)
	$REG_L	$s4,$FRAMESIZE-10*$SZREG($sp)
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$s3,$FRAMESIZE-11*$SZREG($sp)
	$REG_L	$s2,$FRAMESIZE-12*$SZREG($sp)
	$REG_L	$s1,$FRAMESIZE-13*$SZREG($sp)
	$REG_L	$s0,$FRAMESIZE-14*$SZREG($sp)
	$REG_L	$gp,$FRAMESIZE-15*$SZREG($sp)
___
$code.=<<___;
	jr	$ra
	$PTR_ADD $sp,$FRAMESIZE
.end	sha${label}_block_data_order

.rdata
.align	5
K${label}:
___
if ($SZ==4) {
$code.=<<___;
	.word	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
	.word	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
	.word	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
	.word	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
	.word	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
	.word	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
	.word	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
	.word	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
	.word	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
	.word	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
	.word	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
	.word	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
	.word	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
	.word	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
	.word	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
	.word	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
___
} else {
$code.=<<___;
	.dword	0x428a2f98d728ae22, 0x7137449123ef65cd
	.dword	0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc
	.dword	0x3956c25bf348b538, 0x59f111f1b605d019
	.dword	0x923f82a4af194f9b, 0xab1c5ed5da6d8118
	.dword	0xd807aa98a3030242, 0x12835b0145706fbe
	.dword	0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2
	.dword	0x72be5d74f27b896f, 0x80deb1fe3b1696b1
	.dword	0x9bdc06a725c71235, 0xc19bf174cf692694
	.dword	0xe49b69c19ef14ad2, 0xefbe4786384f25e3
	.dword	0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65
	.dword	0x2de92c6f592b0275, 0x4a7484aa6ea6e483
	.dword	0x5cb0a9dcbd41fbd4, 0x76f988da831153b5
	.dword	0x983e5152ee66dfab, 0xa831c66d2db43210
	.dword	0xb00327c898fb213f, 0xbf597fc7beef0ee4
	.dword	0xc6e00bf33da88fc2, 0xd5a79147930aa725
	.dword	0x06ca6351e003826f, 0x142929670a0e6e70
	.dword	0x27b70a8546d22ffc, 0x2e1b21385c26c926
	.dword	0x4d2c6dfc5ac42aed, 0x53380d139d95b3df
	.dword	0x650a73548baf63de, 0x766a0abb3c77b2a8
	.dword	0x81c2c92e47edaee6, 0x92722c851482353b
	.dword	0xa2bfe8a14cf10364, 0xa81a664bbc423001
	.dword	0xc24b8b70d0f89791, 0xc76c51a30654be30
	.dword	0xd192e819d6ef5218, 0xd69906245565a910
	.dword	0xf40e35855771202a, 0x106aa07032bbd1b8
	.dword	0x19a4c116b8d2d0c8, 0x1e376c085141ab53
	.dword	0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8
	.dword	0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb
	.dword	0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3
	.dword	0x748f82ee5defb2fc, 0x78a5636f43172f60
	.dword	0x84c87814a1f0ab72, 0x8cc702081a6439ec
	.dword	0x90befffa23631e28, 0xa4506cebde82bde9
	.dword	0xbef9a3f7b2c67915, 0xc67178f2e372532b
	.dword	0xca273eceea26619c, 0xd186b8c721c0c207
	.dword	0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178
	.dword	0x06f067aa72176fba, 0x0a637dc5a2c898a6
	.dword	0x113f9804bef90dae, 0x1b710b35131c471b
	.dword	0x28db77f523047d84, 0x32caab7b40c72493
	.dword	0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c
	.dword	0x4cc5d4becb3e42b6, 0x597f299cfc657e2a
	.dword	0x5fcb6fab3ad6faec, 0x6c44198c4a475817
___
}
$code.=<<___;
.asciiz	"SHA${label} for MIPS, CRYPTOGAMS by <appro\@openssl.org>"
.align	5

___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

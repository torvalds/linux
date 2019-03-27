#! /usr/bin/env perl
# Copyright 2011-2019 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


###################################################################
### AES-128 [originally in CTR mode]				###
### bitsliced implementation for Intel Core 2 processors	###
### requires support of SSE extensions up to SSSE3		###
### Author: Emilia Käsper and Peter Schwabe			###
### Date: 2009-03-19						###
### Public domain						###
###								###
### See http://homes.esat.kuleuven.be/~ekasper/#software for	###
### further information.					###
###################################################################
#
# September 2011.
#
# Started as transliteration to "perlasm" the original code has
# undergone following changes:
#
# - code was made position-independent;
# - rounds were folded into a loop resulting in >5x size reduction
#   from 12.5KB to 2.2KB;
# - above was possibile thanks to mixcolumns() modification that
#   allowed to feed its output back to aesenc[last], this was
#   achieved at cost of two additional inter-registers moves;
# - some instruction reordering and interleaving;
# - this module doesn't implement key setup subroutine, instead it
#   relies on conversion of "conventional" key schedule as returned
#   by AES_set_encrypt_key (see discussion below);
# - first and last round keys are treated differently, which allowed
#   to skip one shiftrows(), reduce bit-sliced key schedule and
#   speed-up conversion by 22%;
# - support for 192- and 256-bit keys was added;
#
# Resulting performance in CPU cycles spent to encrypt one byte out
# of 4096-byte buffer with 128-bit key is:
#
#		Emilia's	this(*)		difference
#
# Core 2    	9.30		8.69		+7%
# Nehalem(**) 	7.63		6.88		+11%
# Atom	    	17.1		16.4		+4%
# Silvermont	-		12.9
# Goldmont	-		8.85
#
# (*)	Comparison is not completely fair, because "this" is ECB,
#	i.e. no extra processing such as counter values calculation
#	and xor-ing input as in Emilia's CTR implementation is
#	performed. However, the CTR calculations stand for not more
#	than 1% of total time, so comparison is *rather* fair.
#
# (**)	Results were collected on Westmere, which is considered to
#	be equivalent to Nehalem for this code.
#
# As for key schedule conversion subroutine. Interface to OpenSSL
# relies on per-invocation on-the-fly conversion. This naturally
# has impact on performance, especially for short inputs. Conversion
# time in CPU cycles and its ratio to CPU cycles spent in 8x block
# function is:
#
# 		conversion	conversion/8x block
# Core 2	240		0.22
# Nehalem	180		0.20
# Atom		430		0.20
#
# The ratio values mean that 128-byte blocks will be processed
# 16-18% slower, 256-byte blocks - 9-10%, 384-byte blocks - 6-7%,
# etc. Then keep in mind that input sizes not divisible by 128 are
# *effectively* slower, especially shortest ones, e.g. consecutive
# 144-byte blocks are processed 44% slower than one would expect,
# 272 - 29%, 400 - 22%, etc. Yet, despite all these "shortcomings"
# it's still faster than ["hyper-threading-safe" code path in]
# aes-x86_64.pl on all lengths above 64 bytes...
#
# October 2011.
#
# Add decryption procedure. Performance in CPU cycles spent to decrypt
# one byte out of 4096-byte buffer with 128-bit key is:
#
# Core 2	9.98
# Nehalem	7.80
# Atom		17.9
# Silvermont	14.0
# Goldmont	10.2
#
# November 2011.
#
# Add bsaes_xts_[en|de]crypt. Less-than-80-bytes-block performance is
# suboptimal, but XTS is meant to be used with larger blocks...
#
#						<appro@openssl.org>

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

my ($inp,$out,$len,$key,$ivp)=("%rdi","%rsi","%rdx","%rcx");
my @XMM=map("%xmm$_",(15,0..14));	# best on Atom, +10% over (0..15)
my $ecb=0;	# suppress unreferenced ECB subroutines, spare some space...

{
my ($key,$rounds,$const)=("%rax","%r10d","%r11");

sub Sbox {
# input in  lsb > [b0, b1, b2, b3, b4, b5, b6, b7] < msb
# output in lsb > [b0, b1, b4, b6, b3, b7, b2, b5] < msb
my @b=@_[0..7];
my @t=@_[8..11];
my @s=@_[12..15];
	&InBasisChange	(@b);
	&Inv_GF256	(@b[6,5,0,3,7,1,4,2],@t,@s);
	&OutBasisChange	(@b[7,1,4,2,6,5,0,3]);
}

sub InBasisChange {
# input in  lsb > [b0, b1, b2, b3, b4, b5, b6, b7] < msb
# output in lsb > [b6, b5, b0, b3, b7, b1, b4, b2] < msb
my @b=@_[0..7];
$code.=<<___;
	pxor	@b[6], @b[5]
	pxor	@b[1], @b[2]
	pxor	@b[0], @b[3]
	pxor	@b[2], @b[6]
	pxor 	@b[0], @b[5]

	pxor	@b[3], @b[6]
	pxor	@b[7], @b[3]
	pxor	@b[5], @b[7]
	pxor	@b[4], @b[3]
	pxor	@b[5], @b[4]
	pxor	@b[1], @b[3]

	pxor	@b[7], @b[2]
	pxor	@b[5], @b[1]
___
}

sub OutBasisChange {
# input in  lsb > [b0, b1, b2, b3, b4, b5, b6, b7] < msb
# output in lsb > [b6, b1, b2, b4, b7, b0, b3, b5] < msb
my @b=@_[0..7];
$code.=<<___;
	pxor	@b[6], @b[0]
	pxor	@b[4], @b[1]
	pxor	@b[0], @b[2]
	pxor	@b[6], @b[4]
	pxor	@b[1], @b[6]

	pxor	@b[5], @b[1]
	pxor	@b[3], @b[5]
	pxor	@b[7], @b[3]
	pxor	@b[5], @b[7]
	pxor	@b[5], @b[2]

	pxor	@b[7], @b[4]
___
}

sub InvSbox {
# input in lsb 	> [b0, b1, b2, b3, b4, b5, b6, b7] < msb
# output in lsb	> [b0, b1, b6, b4, b2, b7, b3, b5] < msb
my @b=@_[0..7];
my @t=@_[8..11];
my @s=@_[12..15];
	&InvInBasisChange	(@b);
	&Inv_GF256		(@b[5,1,2,6,3,7,0,4],@t,@s);
	&InvOutBasisChange	(@b[3,7,0,4,5,1,2,6]);
}

sub InvInBasisChange {		# OutBasisChange in reverse
my @b=@_[5,1,2,6,3,7,0,4];
$code.=<<___
	pxor	@b[7], @b[4]

	pxor	@b[5], @b[7]
	pxor	@b[5], @b[2]
	pxor	@b[7], @b[3]
	pxor	@b[3], @b[5]
	pxor	@b[5], @b[1]

	pxor	@b[1], @b[6]
	pxor	@b[0], @b[2]
	pxor	@b[6], @b[4]
	pxor	@b[6], @b[0]
	pxor	@b[4], @b[1]
___
}

sub InvOutBasisChange {		# InBasisChange in reverse
my @b=@_[2,5,7,3,6,1,0,4];
$code.=<<___;
	pxor	@b[5], @b[1]
	pxor	@b[7], @b[2]

	pxor	@b[1], @b[3]
	pxor	@b[5], @b[4]
	pxor	@b[5], @b[7]
	pxor	@b[4], @b[3]
	 pxor 	@b[0], @b[5]
	pxor	@b[7], @b[3]
	 pxor	@b[2], @b[6]
	 pxor	@b[1], @b[2]
	pxor	@b[3], @b[6]

	pxor	@b[0], @b[3]
	pxor	@b[6], @b[5]
___
}

sub Mul_GF4 {
#;*************************************************************
#;* Mul_GF4: Input x0-x1,y0-y1 Output x0-x1 Temp t0 (8) *
#;*************************************************************
my ($x0,$x1,$y0,$y1,$t0)=@_;
$code.=<<___;
	movdqa	$y0, $t0
	pxor 	$y1, $t0
	pand	$x0, $t0
	pxor	$x1, $x0
	pand	$y0, $x1
	pand	$y1, $x0
	pxor	$x1, $x0
	pxor	$t0, $x1
___
}

sub Mul_GF4_N {				# not used, see next subroutine
# multiply and scale by N
my ($x0,$x1,$y0,$y1,$t0)=@_;
$code.=<<___;
	movdqa	$y0, $t0
	pxor	$y1, $t0
	pand	$x0, $t0
	pxor	$x1, $x0
	pand	$y0, $x1
	pand	$y1, $x0
	pxor	$x0, $x1
	pxor	$t0, $x0
___
}

sub Mul_GF4_N_GF4 {
# interleaved Mul_GF4_N and Mul_GF4
my ($x0,$x1,$y0,$y1,$t0,
    $x2,$x3,$y2,$y3,$t1)=@_;
$code.=<<___;
	movdqa	$y0, $t0
	 movdqa	$y2, $t1
	pxor	$y1, $t0
	 pxor 	$y3, $t1
	pand	$x0, $t0
	 pand	$x2, $t1
	pxor	$x1, $x0
	 pxor	$x3, $x2
	pand	$y0, $x1
	 pand	$y2, $x3
	pand	$y1, $x0
	 pand	$y3, $x2
	pxor	$x0, $x1
	 pxor	$x3, $x2
	pxor	$t0, $x0
	 pxor	$t1, $x3
___
}
sub Mul_GF16_2 {
my @x=@_[0..7];
my @y=@_[8..11];
my @t=@_[12..15];
$code.=<<___;
	movdqa	@x[0], @t[0]
	movdqa	@x[1], @t[1]
___
	&Mul_GF4  	(@x[0], @x[1], @y[0], @y[1], @t[2]);
$code.=<<___;
	pxor	@x[2], @t[0]
	pxor	@x[3], @t[1]
	pxor	@y[2], @y[0]
	pxor	@y[3], @y[1]
___
	Mul_GF4_N_GF4	(@t[0], @t[1], @y[0], @y[1], @t[3],
			 @x[2], @x[3], @y[2], @y[3], @t[2]);
$code.=<<___;
	pxor	@t[0], @x[0]
	pxor	@t[0], @x[2]
	pxor	@t[1], @x[1]
	pxor	@t[1], @x[3]

	movdqa	@x[4], @t[0]
	movdqa	@x[5], @t[1]
	pxor	@x[6], @t[0]
	pxor	@x[7], @t[1]
___
	&Mul_GF4_N_GF4	(@t[0], @t[1], @y[0], @y[1], @t[3],
			 @x[6], @x[7], @y[2], @y[3], @t[2]);
$code.=<<___;
	pxor	@y[2], @y[0]
	pxor	@y[3], @y[1]
___
	&Mul_GF4  	(@x[4], @x[5], @y[0], @y[1], @t[3]);
$code.=<<___;
	pxor	@t[0], @x[4]
	pxor	@t[0], @x[6]
	pxor	@t[1], @x[5]
	pxor	@t[1], @x[7]
___
}
sub Inv_GF256 {
#;********************************************************************
#;* Inv_GF256: Input x0-x7 Output x0-x7 Temp t0-t3,s0-s3 (144)       *
#;********************************************************************
my @x=@_[0..7];
my @t=@_[8..11];
my @s=@_[12..15];
# direct optimizations from hardware
$code.=<<___;
	movdqa	@x[4], @t[3]
	movdqa	@x[5], @t[2]
	movdqa	@x[1], @t[1]
	movdqa	@x[7], @s[1]
	movdqa	@x[0], @s[0]

	pxor	@x[6], @t[3]
	pxor	@x[7], @t[2]
	pxor	@x[3], @t[1]
	 movdqa	@t[3], @s[2]
	pxor	@x[6], @s[1]
	 movdqa	@t[2], @t[0]
	pxor	@x[2], @s[0]
	 movdqa	@t[3], @s[3]

	por	@t[1], @t[2]
	por	@s[0], @t[3]
	pxor	@t[0], @s[3]
	pand	@s[0], @s[2]
	pxor	@t[1], @s[0]
	pand	@t[1], @t[0]
	pand	@s[0], @s[3]
	movdqa	@x[3], @s[0]
	pxor	@x[2], @s[0]
	pand	@s[0], @s[1]
	pxor	@s[1], @t[3]
	pxor	@s[1], @t[2]
	movdqa	@x[4], @s[1]
	movdqa	@x[1], @s[0]
	pxor	@x[5], @s[1]
	pxor	@x[0], @s[0]
	movdqa	@s[1], @t[1]
	pand	@s[0], @s[1]
	por	@s[0], @t[1]
	pxor	@s[1], @t[0]
	pxor	@s[3], @t[3]
	pxor	@s[2], @t[2]
	pxor	@s[3], @t[1]
	movdqa	@x[7], @s[0]
	pxor	@s[2], @t[0]
	movdqa	@x[6], @s[1]
	pxor	@s[2], @t[1]
	movdqa	@x[5], @s[2]
	pand	@x[3], @s[0]
	movdqa	@x[4], @s[3]
	pand	@x[2], @s[1]
	pand	@x[1], @s[2]
	por	@x[0], @s[3]
	pxor	@s[0], @t[3]
	pxor	@s[1], @t[2]
	pxor	@s[2], @t[1]
	pxor	@s[3], @t[0]

	#Inv_GF16 \t0, \t1, \t2, \t3, \s0, \s1, \s2, \s3

	# new smaller inversion

	movdqa	@t[3], @s[0]
	pand	@t[1], @t[3]
	pxor	@t[2], @s[0]

	movdqa	@t[0], @s[2]
	movdqa	@s[0], @s[3]
	pxor	@t[3], @s[2]
	pand	@s[2], @s[3]

	movdqa	@t[1], @s[1]
	pxor	@t[2], @s[3]
	pxor	@t[0], @s[1]

	pxor	@t[2], @t[3]

	pand	@t[3], @s[1]

	movdqa	@s[2], @t[2]
	pxor	@t[0], @s[1]

	pxor	@s[1], @t[2]
	pxor	@s[1], @t[1]

	pand	@t[0], @t[2]

	pxor	@t[2], @s[2]
	pxor	@t[2], @t[1]

	pand	@s[3], @s[2]

	pxor	@s[0], @s[2]
___
# output in s3, s2, s1, t1

# Mul_GF16_2 \x0, \x1, \x2, \x3, \x4, \x5, \x6, \x7, \t2, \t3, \t0, \t1, \s0, \s1, \s2, \s3

# Mul_GF16_2 \x0, \x1, \x2, \x3, \x4, \x5, \x6, \x7, \s3, \s2, \s1, \t1, \s0, \t0, \t2, \t3
	&Mul_GF16_2(@x,@s[3,2,1],@t[1],@s[0],@t[0,2,3]);

### output msb > [x3,x2,x1,x0,x7,x6,x5,x4] < lsb
}

# AES linear components

sub ShiftRows {
my @x=@_[0..7];
my $mask=pop;
$code.=<<___;
	pxor	0x00($key),@x[0]
	pxor	0x10($key),@x[1]
	pxor	0x20($key),@x[2]
	pxor	0x30($key),@x[3]
	pshufb	$mask,@x[0]
	pshufb	$mask,@x[1]
	pxor	0x40($key),@x[4]
	pxor	0x50($key),@x[5]
	pshufb	$mask,@x[2]
	pshufb	$mask,@x[3]
	pxor	0x60($key),@x[6]
	pxor	0x70($key),@x[7]
	pshufb	$mask,@x[4]
	pshufb	$mask,@x[5]
	pshufb	$mask,@x[6]
	pshufb	$mask,@x[7]
	lea	0x80($key),$key
___
}

sub MixColumns {
# modified to emit output in order suitable for feeding back to aesenc[last]
my @x=@_[0..7];
my @t=@_[8..15];
my $inv=@_[16];	# optional
$code.=<<___;
	pshufd	\$0x93, @x[0], @t[0]	# x0 <<< 32
	pshufd	\$0x93, @x[1], @t[1]
	 pxor	@t[0], @x[0]		# x0 ^ (x0 <<< 32)
	pshufd	\$0x93, @x[2], @t[2]
	 pxor	@t[1], @x[1]
	pshufd	\$0x93, @x[3], @t[3]
	 pxor	@t[2], @x[2]
	pshufd	\$0x93, @x[4], @t[4]
	 pxor	@t[3], @x[3]
	pshufd	\$0x93, @x[5], @t[5]
	 pxor	@t[4], @x[4]
	pshufd	\$0x93, @x[6], @t[6]
	 pxor	@t[5], @x[5]
	pshufd	\$0x93, @x[7], @t[7]
	 pxor	@t[6], @x[6]
	 pxor	@t[7], @x[7]

	pxor	@x[0], @t[1]
	pxor	@x[7], @t[0]
	pxor	@x[7], @t[1]
	 pshufd	\$0x4E, @x[0], @x[0] 	# (x0 ^ (x0 <<< 32)) <<< 64)
	pxor	@x[1], @t[2]
	 pshufd	\$0x4E, @x[1], @x[1]
	pxor	@x[4], @t[5]
	 pxor	@t[0], @x[0]
	pxor	@x[5], @t[6]
	 pxor	@t[1], @x[1]
	pxor	@x[3], @t[4]
	 pshufd	\$0x4E, @x[4], @t[0]
	pxor	@x[6], @t[7]
	 pshufd	\$0x4E, @x[5], @t[1]
	pxor	@x[2], @t[3]
	 pshufd	\$0x4E, @x[3], @x[4]
	pxor	@x[7], @t[3]
	 pshufd	\$0x4E, @x[7], @x[5]
	pxor	@x[7], @t[4]
	 pshufd	\$0x4E, @x[6], @x[3]
	pxor	@t[4], @t[0]
	 pshufd	\$0x4E, @x[2], @x[6]
	pxor	@t[5], @t[1]
___
$code.=<<___ if (!$inv);
	pxor	@t[3], @x[4]
	pxor	@t[7], @x[5]
	pxor	@t[6], @x[3]
	 movdqa	@t[0], @x[2]
	pxor	@t[2], @x[6]
	 movdqa	@t[1], @x[7]
___
$code.=<<___ if ($inv);
	pxor	@x[4], @t[3]
	pxor	@t[7], @x[5]
	pxor	@x[3], @t[6]
	 movdqa	@t[0], @x[3]
	pxor	@t[2], @x[6]
	 movdqa	@t[6], @x[2]
	 movdqa	@t[1], @x[7]
	 movdqa	@x[6], @x[4]
	 movdqa	@t[3], @x[6]
___
}

sub InvMixColumns_orig {
my @x=@_[0..7];
my @t=@_[8..15];

$code.=<<___;
	# multiplication by 0x0e
	pshufd	\$0x93, @x[7], @t[7]
	movdqa	@x[2], @t[2]
	pxor	@x[5], @x[7]		# 7 5
	pxor	@x[5], @x[2]		# 2 5
	pshufd	\$0x93, @x[0], @t[0]
	movdqa	@x[5], @t[5]
	pxor	@x[0], @x[5]		# 5 0		[1]
	pxor	@x[1], @x[0]		# 0 1
	pshufd	\$0x93, @x[1], @t[1]
	pxor	@x[2], @x[1]		# 1 25
	pxor	@x[6], @x[0]		# 01 6		[2]
	pxor	@x[3], @x[1]		# 125 3		[4]
	pshufd	\$0x93, @x[3], @t[3]
	pxor	@x[0], @x[2]		# 25 016	[3]
	pxor	@x[7], @x[3]		# 3 75
	pxor	@x[6], @x[7]		# 75 6		[0]
	pshufd	\$0x93, @x[6], @t[6]
	movdqa	@x[4], @t[4]
	pxor	@x[4], @x[6]		# 6 4
	pxor	@x[3], @x[4]		# 4 375		[6]
	pxor	@x[7], @x[3]		# 375 756=36
	pxor	@t[5], @x[6]		# 64 5		[7]
	pxor	@t[2], @x[3]		# 36 2
	pxor	@t[4], @x[3]		# 362 4		[5]
	pshufd	\$0x93, @t[5], @t[5]
___
					my @y = @x[7,5,0,2,1,3,4,6];
$code.=<<___;
	# multiplication by 0x0b
	pxor	@y[0], @y[1]
	pxor	@t[0], @y[0]
	pxor	@t[1], @y[1]
	pshufd	\$0x93, @t[2], @t[2]
	pxor	@t[5], @y[0]
	pxor	@t[6], @y[1]
	pxor	@t[7], @y[0]
	pshufd	\$0x93, @t[4], @t[4]
	pxor	@t[6], @t[7]		# clobber t[7]
	pxor	@y[0], @y[1]

	pxor	@t[0], @y[3]
	pshufd	\$0x93, @t[0], @t[0]
	pxor	@t[1], @y[2]
	pxor	@t[1], @y[4]
	pxor	@t[2], @y[2]
	pshufd	\$0x93, @t[1], @t[1]
	pxor	@t[2], @y[3]
	pxor	@t[2], @y[5]
	pxor	@t[7], @y[2]
	pshufd	\$0x93, @t[2], @t[2]
	pxor	@t[3], @y[3]
	pxor	@t[3], @y[6]
	pxor	@t[3], @y[4]
	pshufd	\$0x93, @t[3], @t[3]
	pxor	@t[4], @y[7]
	pxor	@t[4], @y[5]
	pxor	@t[7], @y[7]
	pxor	@t[5], @y[3]
	pxor	@t[4], @y[4]
	pxor	@t[5], @t[7]		# clobber t[7] even more

	pxor	@t[7], @y[5]
	pshufd	\$0x93, @t[4], @t[4]
	pxor	@t[7], @y[6]
	pxor	@t[7], @y[4]

	pxor	@t[5], @t[7]
	pshufd	\$0x93, @t[5], @t[5]
	pxor	@t[6], @t[7]		# restore t[7]

	# multiplication by 0x0d
	pxor	@y[7], @y[4]
	pxor	@t[4], @y[7]
	pshufd	\$0x93, @t[6], @t[6]
	pxor	@t[0], @y[2]
	pxor	@t[5], @y[7]
	pxor	@t[2], @y[2]
	pshufd	\$0x93, @t[7], @t[7]

	pxor	@y[1], @y[3]
	pxor	@t[1], @y[1]
	pxor	@t[0], @y[0]
	pxor	@t[0], @y[3]
	pxor	@t[5], @y[1]
	pxor	@t[5], @y[0]
	pxor	@t[7], @y[1]
	pshufd	\$0x93, @t[0], @t[0]
	pxor	@t[6], @y[0]
	pxor	@y[1], @y[3]
	pxor	@t[1], @y[4]
	pshufd	\$0x93, @t[1], @t[1]

	pxor	@t[7], @y[7]
	pxor	@t[2], @y[4]
	pxor	@t[2], @y[5]
	pshufd	\$0x93, @t[2], @t[2]
	pxor	@t[6], @y[2]
	pxor	@t[3], @t[6]		# clobber t[6]
	pxor	@y[7], @y[4]
	pxor	@t[6], @y[3]

	pxor	@t[6], @y[6]
	pxor	@t[5], @y[5]
	pxor	@t[4], @y[6]
	pshufd	\$0x93, @t[4], @t[4]
	pxor	@t[6], @y[5]
	pxor	@t[7], @y[6]
	pxor	@t[3], @t[6]		# restore t[6]

	pshufd	\$0x93, @t[5], @t[5]
	pshufd	\$0x93, @t[6], @t[6]
	pshufd	\$0x93, @t[7], @t[7]
	pshufd	\$0x93, @t[3], @t[3]

	# multiplication by 0x09
	pxor	@y[1], @y[4]
	pxor	@y[1], @t[1]		# t[1]=y[1]
	pxor	@t[5], @t[0]		# clobber t[0]
	pxor	@t[5], @t[1]
	pxor	@t[0], @y[3]
	pxor	@y[0], @t[0]		# t[0]=y[0]
	pxor	@t[6], @t[1]
	pxor	@t[7], @t[6]		# clobber t[6]
	pxor	@t[1], @y[4]
	pxor	@t[4], @y[7]
	pxor	@y[4], @t[4]		# t[4]=y[4]
	pxor	@t[3], @y[6]
	pxor	@y[3], @t[3]		# t[3]=y[3]
	pxor	@t[2], @y[5]
	pxor	@y[2], @t[2]		# t[2]=y[2]
	pxor	@t[7], @t[3]
	pxor	@y[5], @t[5]		# t[5]=y[5]
	pxor	@t[6], @t[2]
	pxor	@t[6], @t[5]
	pxor	@y[6], @t[6]		# t[6]=y[6]
	pxor	@y[7], @t[7]		# t[7]=y[7]

	movdqa	@t[0],@XMM[0]
	movdqa	@t[1],@XMM[1]
	movdqa	@t[2],@XMM[2]
	movdqa	@t[3],@XMM[3]
	movdqa	@t[4],@XMM[4]
	movdqa	@t[5],@XMM[5]
	movdqa	@t[6],@XMM[6]
	movdqa	@t[7],@XMM[7]
___
}

sub InvMixColumns {
my @x=@_[0..7];
my @t=@_[8..15];

# Thanks to Jussi Kivilinna for providing pointer to
#
# | 0e 0b 0d 09 |   | 02 03 01 01 |   | 05 00 04 00 |
# | 09 0e 0b 0d | = | 01 02 03 01 | x | 00 05 00 04 |
# | 0d 09 0e 0b |   | 01 01 02 03 |   | 04 00 05 00 |
# | 0b 0d 09 0e |   | 03 01 01 02 |   | 00 04 00 05 |

$code.=<<___;
	# multiplication by 0x05-0x00-0x04-0x00
	pshufd	\$0x4E, @x[0], @t[0]
	pshufd	\$0x4E, @x[6], @t[6]
	pxor	@x[0], @t[0]
	pshufd	\$0x4E, @x[7], @t[7]
	pxor	@x[6], @t[6]
	pshufd	\$0x4E, @x[1], @t[1]
	pxor	@x[7], @t[7]
	pshufd	\$0x4E, @x[2], @t[2]
	pxor	@x[1], @t[1]
	pshufd	\$0x4E, @x[3], @t[3]
	pxor	@x[2], @t[2]
	 pxor	@t[6], @x[0]
	 pxor	@t[6], @x[1]
	pshufd	\$0x4E, @x[4], @t[4]
	pxor	@x[3], @t[3]
	 pxor	@t[0], @x[2]
	 pxor	@t[1], @x[3]
	pshufd	\$0x4E, @x[5], @t[5]
	pxor	@x[4], @t[4]
	 pxor	@t[7], @x[1]
	 pxor	@t[2], @x[4]
	pxor	@x[5], @t[5]

	 pxor	@t[7], @x[2]
	 pxor	@t[6], @x[3]
	 pxor	@t[6], @x[4]
	 pxor	@t[3], @x[5]
	 pxor	@t[4], @x[6]
	 pxor	@t[7], @x[4]
	 pxor	@t[7], @x[5]
	 pxor	@t[5], @x[7]
___
	&MixColumns	(@x,@t,1);	# flipped 2<->3 and 4<->6
}

sub aesenc {				# not used
my @b=@_[0..7];
my @t=@_[8..15];
$code.=<<___;
	movdqa	0x30($const),@t[0]	# .LSR
___
	&ShiftRows	(@b,@t[0]);
	&Sbox		(@b,@t);
	&MixColumns	(@b[0,1,4,6,3,7,2,5],@t);
}

sub aesenclast {			# not used
my @b=@_[0..7];
my @t=@_[8..15];
$code.=<<___;
	movdqa	0x40($const),@t[0]	# .LSRM0
___
	&ShiftRows	(@b,@t[0]);
	&Sbox		(@b,@t);
$code.=<<___
	pxor	0x00($key),@b[0]
	pxor	0x10($key),@b[1]
	pxor	0x20($key),@b[4]
	pxor	0x30($key),@b[6]
	pxor	0x40($key),@b[3]
	pxor	0x50($key),@b[7]
	pxor	0x60($key),@b[2]
	pxor	0x70($key),@b[5]
___
}

sub swapmove {
my ($a,$b,$n,$mask,$t)=@_;
$code.=<<___;
	movdqa	$b,$t
	psrlq	\$$n,$b
	pxor  	$a,$b
	pand	$mask,$b
	pxor	$b,$a
	psllq	\$$n,$b
	pxor	$t,$b
___
}
sub swapmove2x {
my ($a0,$b0,$a1,$b1,$n,$mask,$t0,$t1)=@_;
$code.=<<___;
	movdqa	$b0,$t0
	psrlq	\$$n,$b0
	 movdqa	$b1,$t1
	 psrlq	\$$n,$b1
	pxor  	$a0,$b0
	 pxor  	$a1,$b1
	pand	$mask,$b0
	 pand	$mask,$b1
	pxor	$b0,$a0
	psllq	\$$n,$b0
	 pxor	$b1,$a1
	 psllq	\$$n,$b1
	pxor	$t0,$b0
	 pxor	$t1,$b1
___
}

sub bitslice {
my @x=reverse(@_[0..7]);
my ($t0,$t1,$t2,$t3)=@_[8..11];
$code.=<<___;
	movdqa	0x00($const),$t0	# .LBS0
	movdqa	0x10($const),$t1	# .LBS1
___
	&swapmove2x(@x[0,1,2,3],1,$t0,$t2,$t3);
	&swapmove2x(@x[4,5,6,7],1,$t0,$t2,$t3);
$code.=<<___;
	movdqa	0x20($const),$t0	# .LBS2
___
	&swapmove2x(@x[0,2,1,3],2,$t1,$t2,$t3);
	&swapmove2x(@x[4,6,5,7],2,$t1,$t2,$t3);

	&swapmove2x(@x[0,4,1,5],4,$t0,$t2,$t3);
	&swapmove2x(@x[2,6,3,7],4,$t0,$t2,$t3);
}

$code.=<<___;
.text

.extern	asm_AES_encrypt
.extern	asm_AES_decrypt

.type	_bsaes_encrypt8,\@abi-omnipotent
.align	64
_bsaes_encrypt8:
.cfi_startproc
	lea	.LBS0(%rip), $const	# constants table

	movdqa	($key), @XMM[9]		# round 0 key
	lea	0x10($key), $key
	movdqa	0x50($const), @XMM[8]	# .LM0SR
	pxor	@XMM[9], @XMM[0]	# xor with round0 key
	pxor	@XMM[9], @XMM[1]
	pxor	@XMM[9], @XMM[2]
	pxor	@XMM[9], @XMM[3]
	 pshufb	@XMM[8], @XMM[0]
	 pshufb	@XMM[8], @XMM[1]
	pxor	@XMM[9], @XMM[4]
	pxor	@XMM[9], @XMM[5]
	 pshufb	@XMM[8], @XMM[2]
	 pshufb	@XMM[8], @XMM[3]
	pxor	@XMM[9], @XMM[6]
	pxor	@XMM[9], @XMM[7]
	 pshufb	@XMM[8], @XMM[4]
	 pshufb	@XMM[8], @XMM[5]
	 pshufb	@XMM[8], @XMM[6]
	 pshufb	@XMM[8], @XMM[7]
_bsaes_encrypt8_bitslice:
___
	&bitslice	(@XMM[0..7, 8..11]);
$code.=<<___;
	dec	$rounds
	jmp	.Lenc_sbox
.align	16
.Lenc_loop:
___
	&ShiftRows	(@XMM[0..7, 8]);
$code.=".Lenc_sbox:\n";
	&Sbox		(@XMM[0..7, 8..15]);
$code.=<<___;
	dec	$rounds
	jl	.Lenc_done
___
	&MixColumns	(@XMM[0,1,4,6,3,7,2,5, 8..15]);
$code.=<<___;
	movdqa	0x30($const), @XMM[8]	# .LSR
	jnz	.Lenc_loop
	movdqa	0x40($const), @XMM[8]	# .LSRM0
	jmp	.Lenc_loop
.align	16
.Lenc_done:
___
	# output in lsb > [t0, t1, t4, t6, t3, t7, t2, t5] < msb
	&bitslice	(@XMM[0,1,4,6,3,7,2,5, 8..11]);
$code.=<<___;
	movdqa	($key), @XMM[8]		# last round key
	pxor	@XMM[8], @XMM[4]
	pxor	@XMM[8], @XMM[6]
	pxor	@XMM[8], @XMM[3]
	pxor	@XMM[8], @XMM[7]
	pxor	@XMM[8], @XMM[2]
	pxor	@XMM[8], @XMM[5]
	pxor	@XMM[8], @XMM[0]
	pxor	@XMM[8], @XMM[1]
	ret
.cfi_endproc
.size	_bsaes_encrypt8,.-_bsaes_encrypt8

.type	_bsaes_decrypt8,\@abi-omnipotent
.align	64
_bsaes_decrypt8:
.cfi_startproc
	lea	.LBS0(%rip), $const	# constants table

	movdqa	($key), @XMM[9]		# round 0 key
	lea	0x10($key), $key
	movdqa	-0x30($const), @XMM[8]	# .LM0ISR
	pxor	@XMM[9], @XMM[0]	# xor with round0 key
	pxor	@XMM[9], @XMM[1]
	pxor	@XMM[9], @XMM[2]
	pxor	@XMM[9], @XMM[3]
	 pshufb	@XMM[8], @XMM[0]
	 pshufb	@XMM[8], @XMM[1]
	pxor	@XMM[9], @XMM[4]
	pxor	@XMM[9], @XMM[5]
	 pshufb	@XMM[8], @XMM[2]
	 pshufb	@XMM[8], @XMM[3]
	pxor	@XMM[9], @XMM[6]
	pxor	@XMM[9], @XMM[7]
	 pshufb	@XMM[8], @XMM[4]
	 pshufb	@XMM[8], @XMM[5]
	 pshufb	@XMM[8], @XMM[6]
	 pshufb	@XMM[8], @XMM[7]
___
	&bitslice	(@XMM[0..7, 8..11]);
$code.=<<___;
	dec	$rounds
	jmp	.Ldec_sbox
.align	16
.Ldec_loop:
___
	&ShiftRows	(@XMM[0..7, 8]);
$code.=".Ldec_sbox:\n";
	&InvSbox	(@XMM[0..7, 8..15]);
$code.=<<___;
	dec	$rounds
	jl	.Ldec_done
___
	&InvMixColumns	(@XMM[0,1,6,4,2,7,3,5, 8..15]);
$code.=<<___;
	movdqa	-0x10($const), @XMM[8]	# .LISR
	jnz	.Ldec_loop
	movdqa	-0x20($const), @XMM[8]	# .LISRM0
	jmp	.Ldec_loop
.align	16
.Ldec_done:
___
	&bitslice	(@XMM[0,1,6,4,2,7,3,5, 8..11]);
$code.=<<___;
	movdqa	($key), @XMM[8]		# last round key
	pxor	@XMM[8], @XMM[6]
	pxor	@XMM[8], @XMM[4]
	pxor	@XMM[8], @XMM[2]
	pxor	@XMM[8], @XMM[7]
	pxor	@XMM[8], @XMM[3]
	pxor	@XMM[8], @XMM[5]
	pxor	@XMM[8], @XMM[0]
	pxor	@XMM[8], @XMM[1]
	ret
.cfi_endproc
.size	_bsaes_decrypt8,.-_bsaes_decrypt8
___
}
{
my ($out,$inp,$rounds,$const)=("%rax","%rcx","%r10d","%r11");

sub bitslice_key {
my @x=reverse(@_[0..7]);
my ($bs0,$bs1,$bs2,$t2,$t3)=@_[8..12];

	&swapmove	(@x[0,1],1,$bs0,$t2,$t3);
$code.=<<___;
	#&swapmove(@x[2,3],1,$t0,$t2,$t3);
	movdqa	@x[0], @x[2]
	movdqa	@x[1], @x[3]
___
	#&swapmove2x(@x[4,5,6,7],1,$t0,$t2,$t3);

	&swapmove2x	(@x[0,2,1,3],2,$bs1,$t2,$t3);
$code.=<<___;
	#&swapmove2x(@x[4,6,5,7],2,$t1,$t2,$t3);
	movdqa	@x[0], @x[4]
	movdqa	@x[2], @x[6]
	movdqa	@x[1], @x[5]
	movdqa	@x[3], @x[7]
___
	&swapmove2x	(@x[0,4,1,5],4,$bs2,$t2,$t3);
	&swapmove2x	(@x[2,6,3,7],4,$bs2,$t2,$t3);
}

$code.=<<___;
.type	_bsaes_key_convert,\@abi-omnipotent
.align	16
_bsaes_key_convert:
.cfi_startproc
	lea	.Lmasks(%rip), $const
	movdqu	($inp), %xmm7		# load round 0 key
	lea	0x10($inp), $inp
	movdqa	0x00($const), %xmm0	# 0x01...
	movdqa	0x10($const), %xmm1	# 0x02...
	movdqa	0x20($const), %xmm2	# 0x04...
	movdqa	0x30($const), %xmm3	# 0x08...
	movdqa	0x40($const), %xmm4	# .LM0
	pcmpeqd	%xmm5, %xmm5		# .LNOT

	movdqu	($inp), %xmm6		# load round 1 key
	movdqa	%xmm7, ($out)		# save round 0 key
	lea	0x10($out), $out
	dec	$rounds
	jmp	.Lkey_loop
.align	16
.Lkey_loop:
	pshufb	%xmm4, %xmm6		# .LM0

	movdqa	%xmm0,	%xmm8
	movdqa	%xmm1,	%xmm9

	pand	%xmm6,	%xmm8
	pand	%xmm6,	%xmm9
	movdqa	%xmm2,	%xmm10
	pcmpeqb	%xmm0,	%xmm8
	psllq	\$4,	%xmm0		# 0x10...
	movdqa	%xmm3,	%xmm11
	pcmpeqb	%xmm1,	%xmm9
	psllq	\$4,	%xmm1		# 0x20...

	pand	%xmm6,	%xmm10
	pand	%xmm6,	%xmm11
	movdqa	%xmm0,	%xmm12
	pcmpeqb	%xmm2,	%xmm10
	psllq	\$4,	%xmm2		# 0x40...
	movdqa	%xmm1,	%xmm13
	pcmpeqb	%xmm3,	%xmm11
	psllq	\$4,	%xmm3		# 0x80...

	movdqa	%xmm2,	%xmm14
	movdqa	%xmm3,	%xmm15
	 pxor	%xmm5,	%xmm8		# "pnot"
	 pxor	%xmm5,	%xmm9

	pand	%xmm6,	%xmm12
	pand	%xmm6,	%xmm13
	 movdqa	%xmm8, 0x00($out)	# write bit-sliced round key
	pcmpeqb	%xmm0,	%xmm12
	psrlq	\$4,	%xmm0		# 0x01...
	 movdqa	%xmm9, 0x10($out)
	pcmpeqb	%xmm1,	%xmm13
	psrlq	\$4,	%xmm1		# 0x02...
	 lea	0x10($inp), $inp

	pand	%xmm6,	%xmm14
	pand	%xmm6,	%xmm15
	 movdqa	%xmm10, 0x20($out)
	pcmpeqb	%xmm2,	%xmm14
	psrlq	\$4,	%xmm2		# 0x04...
	 movdqa	%xmm11, 0x30($out)
	pcmpeqb	%xmm3,	%xmm15
	psrlq	\$4,	%xmm3		# 0x08...
	 movdqu	($inp), %xmm6		# load next round key

	pxor	%xmm5, %xmm13		# "pnot"
	pxor	%xmm5, %xmm14
	movdqa	%xmm12, 0x40($out)
	movdqa	%xmm13, 0x50($out)
	movdqa	%xmm14, 0x60($out)
	movdqa	%xmm15, 0x70($out)
	lea	0x80($out),$out
	dec	$rounds
	jnz	.Lkey_loop

	movdqa	0x50($const), %xmm7	# .L63
	#movdqa	%xmm6, ($out)		# don't save last round key
	ret
.cfi_endproc
.size	_bsaes_key_convert,.-_bsaes_key_convert
___
}

if (0 && !$win64) {	# following four functions are unsupported interface
			# used for benchmarking...
$code.=<<___;
.globl	bsaes_enc_key_convert
.type	bsaes_enc_key_convert,\@function,2
.align	16
bsaes_enc_key_convert:
	mov	240($inp),%r10d		# pass rounds
	mov	$inp,%rcx		# pass key
	mov	$out,%rax		# pass key schedule
	call	_bsaes_key_convert
	pxor	%xmm6,%xmm7		# fix up last round key
	movdqa	%xmm7,(%rax)		# save last round key
	ret
.size	bsaes_enc_key_convert,.-bsaes_enc_key_convert

.globl	bsaes_encrypt_128
.type	bsaes_encrypt_128,\@function,4
.align	16
bsaes_encrypt_128:
.Lenc128_loop:
	movdqu	0x00($inp), @XMM[0]	# load input
	movdqu	0x10($inp), @XMM[1]
	movdqu	0x20($inp), @XMM[2]
	movdqu	0x30($inp), @XMM[3]
	movdqu	0x40($inp), @XMM[4]
	movdqu	0x50($inp), @XMM[5]
	movdqu	0x60($inp), @XMM[6]
	movdqu	0x70($inp), @XMM[7]
	mov	$key, %rax		# pass the $key
	lea	0x80($inp), $inp
	mov	\$10,%r10d

	call	_bsaes_encrypt8

	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[4], 0x20($out)
	movdqu	@XMM[6], 0x30($out)
	movdqu	@XMM[3], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	movdqu	@XMM[2], 0x60($out)
	movdqu	@XMM[5], 0x70($out)
	lea	0x80($out), $out
	sub	\$0x80,$len
	ja	.Lenc128_loop
	ret
.size	bsaes_encrypt_128,.-bsaes_encrypt_128

.globl	bsaes_dec_key_convert
.type	bsaes_dec_key_convert,\@function,2
.align	16
bsaes_dec_key_convert:
	mov	240($inp),%r10d		# pass rounds
	mov	$inp,%rcx		# pass key
	mov	$out,%rax		# pass key schedule
	call	_bsaes_key_convert
	pxor	($out),%xmm7		# fix up round 0 key
	movdqa	%xmm6,(%rax)		# save last round key
	movdqa	%xmm7,($out)
	ret
.size	bsaes_dec_key_convert,.-bsaes_dec_key_convert

.globl	bsaes_decrypt_128
.type	bsaes_decrypt_128,\@function,4
.align	16
bsaes_decrypt_128:
.Ldec128_loop:
	movdqu	0x00($inp), @XMM[0]	# load input
	movdqu	0x10($inp), @XMM[1]
	movdqu	0x20($inp), @XMM[2]
	movdqu	0x30($inp), @XMM[3]
	movdqu	0x40($inp), @XMM[4]
	movdqu	0x50($inp), @XMM[5]
	movdqu	0x60($inp), @XMM[6]
	movdqu	0x70($inp), @XMM[7]
	mov	$key, %rax		# pass the $key
	lea	0x80($inp), $inp
	mov	\$10,%r10d

	call	_bsaes_decrypt8

	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	movdqu	@XMM[2], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	movdqu	@XMM[3], 0x60($out)
	movdqu	@XMM[5], 0x70($out)
	lea	0x80($out), $out
	sub	\$0x80,$len
	ja	.Ldec128_loop
	ret
.size	bsaes_decrypt_128,.-bsaes_decrypt_128
___
}
{
######################################################################
#
# OpenSSL interface
#
my ($arg1,$arg2,$arg3,$arg4,$arg5,$arg6)=$win64	? ("%rcx","%rdx","%r8","%r9","%r10","%r11d")
						: ("%rdi","%rsi","%rdx","%rcx","%r8","%r9d");
my ($inp,$out,$len,$key)=("%r12","%r13","%r14","%r15");

if ($ecb) {
$code.=<<___;
.globl	bsaes_ecb_encrypt_blocks
.type	bsaes_ecb_encrypt_blocks,\@abi-omnipotent
.align	16
bsaes_ecb_encrypt_blocks:
.cfi_startproc
	mov	%rsp, %rax
.Lecb_enc_prologue:
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	lea	-0x48(%rsp),%rsp
.cfi_adjust_cfa_offset	0x48
___
$code.=<<___ if ($win64);
	lea	-0xa0(%rsp), %rsp
	movaps	%xmm6, 0x40(%rsp)
	movaps	%xmm7, 0x50(%rsp)
	movaps	%xmm8, 0x60(%rsp)
	movaps	%xmm9, 0x70(%rsp)
	movaps	%xmm10, 0x80(%rsp)
	movaps	%xmm11, 0x90(%rsp)
	movaps	%xmm12, 0xa0(%rsp)
	movaps	%xmm13, 0xb0(%rsp)
	movaps	%xmm14, 0xc0(%rsp)
	movaps	%xmm15, 0xd0(%rsp)
.Lecb_enc_body:
___
$code.=<<___;
	mov	%rsp,%rbp		# backup %rsp
.cfi_def_cfa_register	%rbp
	mov	240($arg4),%eax		# rounds
	mov	$arg1,$inp		# backup arguments
	mov	$arg2,$out
	mov	$arg3,$len
	mov	$arg4,$key
	cmp	\$8,$arg3
	jb	.Lecb_enc_short

	mov	%eax,%ebx		# backup rounds
	shl	\$7,%rax		# 128 bytes per inner round key
	sub	\$`128-32`,%rax		# size of bit-sliced key schedule
	sub	%rax,%rsp
	mov	%rsp,%rax		# pass key schedule
	mov	$key,%rcx		# pass key
	mov	%ebx,%r10d		# pass rounds
	call	_bsaes_key_convert
	pxor	%xmm6,%xmm7		# fix up last round key
	movdqa	%xmm7,(%rax)		# save last round key

	sub	\$8,$len
.Lecb_enc_loop:
	movdqu	0x00($inp), @XMM[0]	# load input
	movdqu	0x10($inp), @XMM[1]
	movdqu	0x20($inp), @XMM[2]
	movdqu	0x30($inp), @XMM[3]
	movdqu	0x40($inp), @XMM[4]
	movdqu	0x50($inp), @XMM[5]
	mov	%rsp, %rax		# pass key schedule
	movdqu	0x60($inp), @XMM[6]
	mov	%ebx,%r10d		# pass rounds
	movdqu	0x70($inp), @XMM[7]
	lea	0x80($inp), $inp

	call	_bsaes_encrypt8

	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[4], 0x20($out)
	movdqu	@XMM[6], 0x30($out)
	movdqu	@XMM[3], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	movdqu	@XMM[2], 0x60($out)
	movdqu	@XMM[5], 0x70($out)
	lea	0x80($out), $out
	sub	\$8,$len
	jnc	.Lecb_enc_loop

	add	\$8,$len
	jz	.Lecb_enc_done

	movdqu	0x00($inp), @XMM[0]	# load input
	mov	%rsp, %rax		# pass key schedule
	mov	%ebx,%r10d		# pass rounds
	cmp	\$2,$len
	jb	.Lecb_enc_one
	movdqu	0x10($inp), @XMM[1]
	je	.Lecb_enc_two
	movdqu	0x20($inp), @XMM[2]
	cmp	\$4,$len
	jb	.Lecb_enc_three
	movdqu	0x30($inp), @XMM[3]
	je	.Lecb_enc_four
	movdqu	0x40($inp), @XMM[4]
	cmp	\$6,$len
	jb	.Lecb_enc_five
	movdqu	0x50($inp), @XMM[5]
	je	.Lecb_enc_six
	movdqu	0x60($inp), @XMM[6]
	call	_bsaes_encrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[4], 0x20($out)
	movdqu	@XMM[6], 0x30($out)
	movdqu	@XMM[3], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	movdqu	@XMM[2], 0x60($out)
	jmp	.Lecb_enc_done
.align	16
.Lecb_enc_six:
	call	_bsaes_encrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[4], 0x20($out)
	movdqu	@XMM[6], 0x30($out)
	movdqu	@XMM[3], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	jmp	.Lecb_enc_done
.align	16
.Lecb_enc_five:
	call	_bsaes_encrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[4], 0x20($out)
	movdqu	@XMM[6], 0x30($out)
	movdqu	@XMM[3], 0x40($out)
	jmp	.Lecb_enc_done
.align	16
.Lecb_enc_four:
	call	_bsaes_encrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[4], 0x20($out)
	movdqu	@XMM[6], 0x30($out)
	jmp	.Lecb_enc_done
.align	16
.Lecb_enc_three:
	call	_bsaes_encrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[4], 0x20($out)
	jmp	.Lecb_enc_done
.align	16
.Lecb_enc_two:
	call	_bsaes_encrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	jmp	.Lecb_enc_done
.align	16
.Lecb_enc_one:
	call	_bsaes_encrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	jmp	.Lecb_enc_done
.align	16
.Lecb_enc_short:
	lea	($inp), $arg1
	lea	($out), $arg2
	lea	($key), $arg3
	call	asm_AES_encrypt
	lea	16($inp), $inp
	lea	16($out), $out
	dec	$len
	jnz	.Lecb_enc_short

.Lecb_enc_done:
	lea	(%rsp),%rax
	pxor	%xmm0, %xmm0
.Lecb_enc_bzero:			# wipe key schedule [if any]
	movdqa	%xmm0, 0x00(%rax)
	movdqa	%xmm0, 0x10(%rax)
	lea	0x20(%rax), %rax
	cmp	%rax, %rbp
	jb	.Lecb_enc_bzero

	lea	0x78(%rbp),%rax
.cfi_def_cfa	%rax,8
___
$code.=<<___ if ($win64);
	movaps	0x40(%rbp), %xmm6
	movaps	0x50(%rbp), %xmm7
	movaps	0x60(%rbp), %xmm8
	movaps	0x70(%rbp), %xmm9
	movaps	0x80(%rbp), %xmm10
	movaps	0x90(%rbp), %xmm11
	movaps	0xa0(%rbp), %xmm12
	movaps	0xb0(%rbp), %xmm13
	movaps	0xc0(%rbp), %xmm14
	movaps	0xd0(%rbp), %xmm15
	lea	0xa0(%rax), %rax
.Lecb_enc_tail:
___
$code.=<<___;
	mov	-48(%rax), %r15
.cfi_restore	%r15
	mov	-40(%rax), %r14
.cfi_restore	%r14
	mov	-32(%rax), %r13
.cfi_restore	%r13
	mov	-24(%rax), %r12
.cfi_restore	%r12
	mov	-16(%rax), %rbx
.cfi_restore	%rbx
	mov	-8(%rax), %rbp
.cfi_restore	%rbp
	lea	(%rax), %rsp		# restore %rsp
.cfi_def_cfa_register	%rsp
.Lecb_enc_epilogue:
	ret
.cfi_endproc
.size	bsaes_ecb_encrypt_blocks,.-bsaes_ecb_encrypt_blocks

.globl	bsaes_ecb_decrypt_blocks
.type	bsaes_ecb_decrypt_blocks,\@abi-omnipotent
.align	16
bsaes_ecb_decrypt_blocks:
.cfi_startproc
	mov	%rsp, %rax
.Lecb_dec_prologue:
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	lea	-0x48(%rsp),%rsp
.cfi_adjust_cfa_offset	0x48
___
$code.=<<___ if ($win64);
	lea	-0xa0(%rsp), %rsp
	movaps	%xmm6, 0x40(%rsp)
	movaps	%xmm7, 0x50(%rsp)
	movaps	%xmm8, 0x60(%rsp)
	movaps	%xmm9, 0x70(%rsp)
	movaps	%xmm10, 0x80(%rsp)
	movaps	%xmm11, 0x90(%rsp)
	movaps	%xmm12, 0xa0(%rsp)
	movaps	%xmm13, 0xb0(%rsp)
	movaps	%xmm14, 0xc0(%rsp)
	movaps	%xmm15, 0xd0(%rsp)
.Lecb_dec_body:
___
$code.=<<___;
	mov	%rsp,%rbp		# backup %rsp
.cfi_def_cfa_register	%rbp
	mov	240($arg4),%eax		# rounds
	mov	$arg1,$inp		# backup arguments
	mov	$arg2,$out
	mov	$arg3,$len
	mov	$arg4,$key
	cmp	\$8,$arg3
	jb	.Lecb_dec_short

	mov	%eax,%ebx		# backup rounds
	shl	\$7,%rax		# 128 bytes per inner round key
	sub	\$`128-32`,%rax		# size of bit-sliced key schedule
	sub	%rax,%rsp
	mov	%rsp,%rax		# pass key schedule
	mov	$key,%rcx		# pass key
	mov	%ebx,%r10d		# pass rounds
	call	_bsaes_key_convert
	pxor	(%rsp),%xmm7		# fix up 0 round key
	movdqa	%xmm6,(%rax)		# save last round key
	movdqa	%xmm7,(%rsp)

	sub	\$8,$len
.Lecb_dec_loop:
	movdqu	0x00($inp), @XMM[0]	# load input
	movdqu	0x10($inp), @XMM[1]
	movdqu	0x20($inp), @XMM[2]
	movdqu	0x30($inp), @XMM[3]
	movdqu	0x40($inp), @XMM[4]
	movdqu	0x50($inp), @XMM[5]
	mov	%rsp, %rax		# pass key schedule
	movdqu	0x60($inp), @XMM[6]
	mov	%ebx,%r10d		# pass rounds
	movdqu	0x70($inp), @XMM[7]
	lea	0x80($inp), $inp

	call	_bsaes_decrypt8

	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	movdqu	@XMM[2], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	movdqu	@XMM[3], 0x60($out)
	movdqu	@XMM[5], 0x70($out)
	lea	0x80($out), $out
	sub	\$8,$len
	jnc	.Lecb_dec_loop

	add	\$8,$len
	jz	.Lecb_dec_done

	movdqu	0x00($inp), @XMM[0]	# load input
	mov	%rsp, %rax		# pass key schedule
	mov	%ebx,%r10d		# pass rounds
	cmp	\$2,$len
	jb	.Lecb_dec_one
	movdqu	0x10($inp), @XMM[1]
	je	.Lecb_dec_two
	movdqu	0x20($inp), @XMM[2]
	cmp	\$4,$len
	jb	.Lecb_dec_three
	movdqu	0x30($inp), @XMM[3]
	je	.Lecb_dec_four
	movdqu	0x40($inp), @XMM[4]
	cmp	\$6,$len
	jb	.Lecb_dec_five
	movdqu	0x50($inp), @XMM[5]
	je	.Lecb_dec_six
	movdqu	0x60($inp), @XMM[6]
	call	_bsaes_decrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	movdqu	@XMM[2], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	movdqu	@XMM[3], 0x60($out)
	jmp	.Lecb_dec_done
.align	16
.Lecb_dec_six:
	call	_bsaes_decrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	movdqu	@XMM[2], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	jmp	.Lecb_dec_done
.align	16
.Lecb_dec_five:
	call	_bsaes_decrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	movdqu	@XMM[2], 0x40($out)
	jmp	.Lecb_dec_done
.align	16
.Lecb_dec_four:
	call	_bsaes_decrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	jmp	.Lecb_dec_done
.align	16
.Lecb_dec_three:
	call	_bsaes_decrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	jmp	.Lecb_dec_done
.align	16
.Lecb_dec_two:
	call	_bsaes_decrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	jmp	.Lecb_dec_done
.align	16
.Lecb_dec_one:
	call	_bsaes_decrypt8
	movdqu	@XMM[0], 0x00($out)	# write output
	jmp	.Lecb_dec_done
.align	16
.Lecb_dec_short:
	lea	($inp), $arg1
	lea	($out), $arg2
	lea	($key), $arg3
	call	asm_AES_decrypt
	lea	16($inp), $inp
	lea	16($out), $out
	dec	$len
	jnz	.Lecb_dec_short

.Lecb_dec_done:
	lea	(%rsp),%rax
	pxor	%xmm0, %xmm0
.Lecb_dec_bzero:			# wipe key schedule [if any]
	movdqa	%xmm0, 0x00(%rax)
	movdqa	%xmm0, 0x10(%rax)
	lea	0x20(%rax), %rax
	cmp	%rax, %rbp
	jb	.Lecb_dec_bzero

	lea	0x78(%rbp),%rax
.cfi_def_cfa	%rax,8
___
$code.=<<___ if ($win64);
	movaps	0x40(%rbp), %xmm6
	movaps	0x50(%rbp), %xmm7
	movaps	0x60(%rbp), %xmm8
	movaps	0x70(%rbp), %xmm9
	movaps	0x80(%rbp), %xmm10
	movaps	0x90(%rbp), %xmm11
	movaps	0xa0(%rbp), %xmm12
	movaps	0xb0(%rbp), %xmm13
	movaps	0xc0(%rbp), %xmm14
	movaps	0xd0(%rbp), %xmm15
	lea	0xa0(%rax), %rax
.Lecb_dec_tail:
___
$code.=<<___;
	mov	-48(%rax), %r15
.cfi_restore	%r15
	mov	-40(%rax), %r14
.cfi_restore	%r14
	mov	-32(%rax), %r13
.cfi_restore	%r13
	mov	-24(%rax), %r12
.cfi_restore	%r12
	mov	-16(%rax), %rbx
.cfi_restore	%rbx
	mov	-8(%rax), %rbp
.cfi_restore	%rbp
	lea	(%rax), %rsp		# restore %rsp
.cfi_def_cfa_register	%rsp
.Lecb_dec_epilogue:
	ret
.cfi_endproc
.size	bsaes_ecb_decrypt_blocks,.-bsaes_ecb_decrypt_blocks
___
}
$code.=<<___;
.extern	asm_AES_cbc_encrypt
.globl	bsaes_cbc_encrypt
.type	bsaes_cbc_encrypt,\@abi-omnipotent
.align	16
bsaes_cbc_encrypt:
.cfi_startproc
___
$code.=<<___ if ($win64);
	mov	48(%rsp),$arg6		# pull direction flag
___
$code.=<<___;
	cmp	\$0,$arg6
	jne	asm_AES_cbc_encrypt
	cmp	\$128,$arg3
	jb	asm_AES_cbc_encrypt

	mov	%rsp, %rax
.Lcbc_dec_prologue:
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	lea	-0x48(%rsp), %rsp
.cfi_adjust_cfa_offset	0x48
___
$code.=<<___ if ($win64);
	mov	0xa0(%rsp),$arg5	# pull ivp
	lea	-0xa0(%rsp), %rsp
	movaps	%xmm6, 0x40(%rsp)
	movaps	%xmm7, 0x50(%rsp)
	movaps	%xmm8, 0x60(%rsp)
	movaps	%xmm9, 0x70(%rsp)
	movaps	%xmm10, 0x80(%rsp)
	movaps	%xmm11, 0x90(%rsp)
	movaps	%xmm12, 0xa0(%rsp)
	movaps	%xmm13, 0xb0(%rsp)
	movaps	%xmm14, 0xc0(%rsp)
	movaps	%xmm15, 0xd0(%rsp)
.Lcbc_dec_body:
___
$code.=<<___;
	mov	%rsp, %rbp		# backup %rsp
.cfi_def_cfa_register	%rbp
	mov	240($arg4), %eax	# rounds
	mov	$arg1, $inp		# backup arguments
	mov	$arg2, $out
	mov	$arg3, $len
	mov	$arg4, $key
	mov	$arg5, %rbx
	shr	\$4, $len		# bytes to blocks

	mov	%eax, %edx		# rounds
	shl	\$7, %rax		# 128 bytes per inner round key
	sub	\$`128-32`, %rax	# size of bit-sliced key schedule
	sub	%rax, %rsp

	mov	%rsp, %rax		# pass key schedule
	mov	$key, %rcx		# pass key
	mov	%edx, %r10d		# pass rounds
	call	_bsaes_key_convert
	pxor	(%rsp),%xmm7		# fix up 0 round key
	movdqa	%xmm6,(%rax)		# save last round key
	movdqa	%xmm7,(%rsp)

	movdqu	(%rbx), @XMM[15]	# load IV
	sub	\$8,$len
.Lcbc_dec_loop:
	movdqu	0x00($inp), @XMM[0]	# load input
	movdqu	0x10($inp), @XMM[1]
	movdqu	0x20($inp), @XMM[2]
	movdqu	0x30($inp), @XMM[3]
	movdqu	0x40($inp), @XMM[4]
	movdqu	0x50($inp), @XMM[5]
	mov	%rsp, %rax		# pass key schedule
	movdqu	0x60($inp), @XMM[6]
	mov	%edx,%r10d		# pass rounds
	movdqu	0x70($inp), @XMM[7]
	movdqa	@XMM[15], 0x20(%rbp)	# put aside IV

	call	_bsaes_decrypt8

	pxor	0x20(%rbp), @XMM[0]	# ^= IV
	movdqu	0x00($inp), @XMM[8]	# re-load input
	movdqu	0x10($inp), @XMM[9]
	pxor	@XMM[8], @XMM[1]
	movdqu	0x20($inp), @XMM[10]
	pxor	@XMM[9], @XMM[6]
	movdqu	0x30($inp), @XMM[11]
	pxor	@XMM[10], @XMM[4]
	movdqu	0x40($inp), @XMM[12]
	pxor	@XMM[11], @XMM[2]
	movdqu	0x50($inp), @XMM[13]
	pxor	@XMM[12], @XMM[7]
	movdqu	0x60($inp), @XMM[14]
	pxor	@XMM[13], @XMM[3]
	movdqu	0x70($inp), @XMM[15]	# IV
	pxor	@XMM[14], @XMM[5]
	movdqu	@XMM[0], 0x00($out)	# write output
	lea	0x80($inp), $inp
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	movdqu	@XMM[2], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	movdqu	@XMM[3], 0x60($out)
	movdqu	@XMM[5], 0x70($out)
	lea	0x80($out), $out
	sub	\$8,$len
	jnc	.Lcbc_dec_loop

	add	\$8,$len
	jz	.Lcbc_dec_done

	movdqu	0x00($inp), @XMM[0]	# load input
	mov	%rsp, %rax		# pass key schedule
	mov	%edx, %r10d		# pass rounds
	cmp	\$2,$len
	jb	.Lcbc_dec_one
	movdqu	0x10($inp), @XMM[1]
	je	.Lcbc_dec_two
	movdqu	0x20($inp), @XMM[2]
	cmp	\$4,$len
	jb	.Lcbc_dec_three
	movdqu	0x30($inp), @XMM[3]
	je	.Lcbc_dec_four
	movdqu	0x40($inp), @XMM[4]
	cmp	\$6,$len
	jb	.Lcbc_dec_five
	movdqu	0x50($inp), @XMM[5]
	je	.Lcbc_dec_six
	movdqu	0x60($inp), @XMM[6]
	movdqa	@XMM[15], 0x20(%rbp)	# put aside IV
	call	_bsaes_decrypt8
	pxor	0x20(%rbp), @XMM[0]	# ^= IV
	movdqu	0x00($inp), @XMM[8]	# re-load input
	movdqu	0x10($inp), @XMM[9]
	pxor	@XMM[8], @XMM[1]
	movdqu	0x20($inp), @XMM[10]
	pxor	@XMM[9], @XMM[6]
	movdqu	0x30($inp), @XMM[11]
	pxor	@XMM[10], @XMM[4]
	movdqu	0x40($inp), @XMM[12]
	pxor	@XMM[11], @XMM[2]
	movdqu	0x50($inp), @XMM[13]
	pxor	@XMM[12], @XMM[7]
	movdqu	0x60($inp), @XMM[15]	# IV
	pxor	@XMM[13], @XMM[3]
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	movdqu	@XMM[2], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	movdqu	@XMM[3], 0x60($out)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_six:
	movdqa	@XMM[15], 0x20(%rbp)	# put aside IV
	call	_bsaes_decrypt8
	pxor	0x20(%rbp), @XMM[0]	# ^= IV
	movdqu	0x00($inp), @XMM[8]	# re-load input
	movdqu	0x10($inp), @XMM[9]
	pxor	@XMM[8], @XMM[1]
	movdqu	0x20($inp), @XMM[10]
	pxor	@XMM[9], @XMM[6]
	movdqu	0x30($inp), @XMM[11]
	pxor	@XMM[10], @XMM[4]
	movdqu	0x40($inp), @XMM[12]
	pxor	@XMM[11], @XMM[2]
	movdqu	0x50($inp), @XMM[15]	# IV
	pxor	@XMM[12], @XMM[7]
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	movdqu	@XMM[2], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_five:
	movdqa	@XMM[15], 0x20(%rbp)	# put aside IV
	call	_bsaes_decrypt8
	pxor	0x20(%rbp), @XMM[0]	# ^= IV
	movdqu	0x00($inp), @XMM[8]	# re-load input
	movdqu	0x10($inp), @XMM[9]
	pxor	@XMM[8], @XMM[1]
	movdqu	0x20($inp), @XMM[10]
	pxor	@XMM[9], @XMM[6]
	movdqu	0x30($inp), @XMM[11]
	pxor	@XMM[10], @XMM[4]
	movdqu	0x40($inp), @XMM[15]	# IV
	pxor	@XMM[11], @XMM[2]
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	movdqu	@XMM[2], 0x40($out)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_four:
	movdqa	@XMM[15], 0x20(%rbp)	# put aside IV
	call	_bsaes_decrypt8
	pxor	0x20(%rbp), @XMM[0]	# ^= IV
	movdqu	0x00($inp), @XMM[8]	# re-load input
	movdqu	0x10($inp), @XMM[9]
	pxor	@XMM[8], @XMM[1]
	movdqu	0x20($inp), @XMM[10]
	pxor	@XMM[9], @XMM[6]
	movdqu	0x30($inp), @XMM[15]	# IV
	pxor	@XMM[10], @XMM[4]
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_three:
	movdqa	@XMM[15], 0x20(%rbp)	# put aside IV
	call	_bsaes_decrypt8
	pxor	0x20(%rbp), @XMM[0]	# ^= IV
	movdqu	0x00($inp), @XMM[8]	# re-load input
	movdqu	0x10($inp), @XMM[9]
	pxor	@XMM[8], @XMM[1]
	movdqu	0x20($inp), @XMM[15]	# IV
	pxor	@XMM[9], @XMM[6]
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_two:
	movdqa	@XMM[15], 0x20(%rbp)	# put aside IV
	call	_bsaes_decrypt8
	pxor	0x20(%rbp), @XMM[0]	# ^= IV
	movdqu	0x00($inp), @XMM[8]	# re-load input
	movdqu	0x10($inp), @XMM[15]	# IV
	pxor	@XMM[8], @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_one:
	lea	($inp), $arg1
	lea	0x20(%rbp), $arg2	# buffer output
	lea	($key), $arg3
	call	asm_AES_decrypt		# doesn't touch %xmm
	pxor	0x20(%rbp), @XMM[15]	# ^= IV
	movdqu	@XMM[15], ($out)	# write output
	movdqa	@XMM[0], @XMM[15]	# IV

.Lcbc_dec_done:
	movdqu	@XMM[15], (%rbx)	# return IV
	lea	(%rsp), %rax
	pxor	%xmm0, %xmm0
.Lcbc_dec_bzero:			# wipe key schedule [if any]
	movdqa	%xmm0, 0x00(%rax)
	movdqa	%xmm0, 0x10(%rax)
	lea	0x20(%rax), %rax
	cmp	%rax, %rbp
	ja	.Lcbc_dec_bzero

	lea	0x78(%rbp),%rax
.cfi_def_cfa	%rax,8
___
$code.=<<___ if ($win64);
	movaps	0x40(%rbp), %xmm6
	movaps	0x50(%rbp), %xmm7
	movaps	0x60(%rbp), %xmm8
	movaps	0x70(%rbp), %xmm9
	movaps	0x80(%rbp), %xmm10
	movaps	0x90(%rbp), %xmm11
	movaps	0xa0(%rbp), %xmm12
	movaps	0xb0(%rbp), %xmm13
	movaps	0xc0(%rbp), %xmm14
	movaps	0xd0(%rbp), %xmm15
	lea	0xa0(%rax), %rax
.Lcbc_dec_tail:
___
$code.=<<___;
	mov	-48(%rax), %r15
.cfi_restore	%r15
	mov	-40(%rax), %r14
.cfi_restore	%r14
	mov	-32(%rax), %r13
.cfi_restore	%r13
	mov	-24(%rax), %r12
.cfi_restore	%r12
	mov	-16(%rax), %rbx
.cfi_restore	%rbx
	mov	-8(%rax), %rbp
.cfi_restore	%rbp
	lea	(%rax), %rsp		# restore %rsp
.cfi_def_cfa_register	%rsp
.Lcbc_dec_epilogue:
	ret
.cfi_endproc
.size	bsaes_cbc_encrypt,.-bsaes_cbc_encrypt

.globl	bsaes_ctr32_encrypt_blocks
.type	bsaes_ctr32_encrypt_blocks,\@abi-omnipotent
.align	16
bsaes_ctr32_encrypt_blocks:
.cfi_startproc
	mov	%rsp, %rax
.Lctr_enc_prologue:
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	lea	-0x48(%rsp), %rsp
.cfi_adjust_cfa_offset	0x48
___
$code.=<<___ if ($win64);
	mov	0xa0(%rsp),$arg5	# pull ivp
	lea	-0xa0(%rsp), %rsp
	movaps	%xmm6, 0x40(%rsp)
	movaps	%xmm7, 0x50(%rsp)
	movaps	%xmm8, 0x60(%rsp)
	movaps	%xmm9, 0x70(%rsp)
	movaps	%xmm10, 0x80(%rsp)
	movaps	%xmm11, 0x90(%rsp)
	movaps	%xmm12, 0xa0(%rsp)
	movaps	%xmm13, 0xb0(%rsp)
	movaps	%xmm14, 0xc0(%rsp)
	movaps	%xmm15, 0xd0(%rsp)
.Lctr_enc_body:
___
$code.=<<___;
	mov	%rsp, %rbp		# backup %rsp
.cfi_def_cfa_register	%rbp
	movdqu	($arg5), %xmm0		# load counter
	mov	240($arg4), %eax	# rounds
	mov	$arg1, $inp		# backup arguments
	mov	$arg2, $out
	mov	$arg3, $len
	mov	$arg4, $key
	movdqa	%xmm0, 0x20(%rbp)	# copy counter
	cmp	\$8, $arg3
	jb	.Lctr_enc_short

	mov	%eax, %ebx		# rounds
	shl	\$7, %rax		# 128 bytes per inner round key
	sub	\$`128-32`, %rax	# size of bit-sliced key schedule
	sub	%rax, %rsp

	mov	%rsp, %rax		# pass key schedule
	mov	$key, %rcx		# pass key
	mov	%ebx, %r10d		# pass rounds
	call	_bsaes_key_convert
	pxor	%xmm6,%xmm7		# fix up last round key
	movdqa	%xmm7,(%rax)		# save last round key

	movdqa	(%rsp), @XMM[9]		# load round0 key
	lea	.LADD1(%rip), %r11
	movdqa	0x20(%rbp), @XMM[0]	# counter copy
	movdqa	-0x20(%r11), @XMM[8]	# .LSWPUP
	pshufb	@XMM[8], @XMM[9]	# byte swap upper part
	pshufb	@XMM[8], @XMM[0]
	movdqa	@XMM[9], (%rsp)		# save adjusted round0 key
	jmp	.Lctr_enc_loop
.align	16
.Lctr_enc_loop:
	movdqa	@XMM[0], 0x20(%rbp)	# save counter
	movdqa	@XMM[0], @XMM[1]	# prepare 8 counter values
	movdqa	@XMM[0], @XMM[2]
	paddd	0x00(%r11), @XMM[1]	# .LADD1
	movdqa	@XMM[0], @XMM[3]
	paddd	0x10(%r11), @XMM[2]	# .LADD2
	movdqa	@XMM[0], @XMM[4]
	paddd	0x20(%r11), @XMM[3]	# .LADD3
	movdqa	@XMM[0], @XMM[5]
	paddd	0x30(%r11), @XMM[4]	# .LADD4
	movdqa	@XMM[0], @XMM[6]
	paddd	0x40(%r11), @XMM[5]	# .LADD5
	movdqa	@XMM[0], @XMM[7]
	paddd	0x50(%r11), @XMM[6]	# .LADD6
	paddd	0x60(%r11), @XMM[7]	# .LADD7

	# Borrow prologue from _bsaes_encrypt8 to use the opportunity
	# to flip byte order in 32-bit counter
	movdqa	(%rsp), @XMM[9]		# round 0 key
	lea	0x10(%rsp), %rax	# pass key schedule
	movdqa	-0x10(%r11), @XMM[8]	# .LSWPUPM0SR
	pxor	@XMM[9], @XMM[0]	# xor with round0 key
	pxor	@XMM[9], @XMM[1]
	pxor	@XMM[9], @XMM[2]
	pxor	@XMM[9], @XMM[3]
	 pshufb	@XMM[8], @XMM[0]
	 pshufb	@XMM[8], @XMM[1]
	pxor	@XMM[9], @XMM[4]
	pxor	@XMM[9], @XMM[5]
	 pshufb	@XMM[8], @XMM[2]
	 pshufb	@XMM[8], @XMM[3]
	pxor	@XMM[9], @XMM[6]
	pxor	@XMM[9], @XMM[7]
	 pshufb	@XMM[8], @XMM[4]
	 pshufb	@XMM[8], @XMM[5]
	 pshufb	@XMM[8], @XMM[6]
	 pshufb	@XMM[8], @XMM[7]
	lea	.LBS0(%rip), %r11	# constants table
	mov	%ebx,%r10d		# pass rounds

	call	_bsaes_encrypt8_bitslice

	sub	\$8,$len
	jc	.Lctr_enc_loop_done

	movdqu	0x00($inp), @XMM[8]	# load input
	movdqu	0x10($inp), @XMM[9]
	movdqu	0x20($inp), @XMM[10]
	movdqu	0x30($inp), @XMM[11]
	movdqu	0x40($inp), @XMM[12]
	movdqu	0x50($inp), @XMM[13]
	movdqu	0x60($inp), @XMM[14]
	movdqu	0x70($inp), @XMM[15]
	lea	0x80($inp),$inp
	pxor	@XMM[0], @XMM[8]
	movdqa	0x20(%rbp), @XMM[0]	# load counter
	pxor	@XMM[9], @XMM[1]
	movdqu	@XMM[8], 0x00($out)	# write output
	pxor	@XMM[10], @XMM[4]
	movdqu	@XMM[1], 0x10($out)
	pxor	@XMM[11], @XMM[6]
	movdqu	@XMM[4], 0x20($out)
	pxor	@XMM[12], @XMM[3]
	movdqu	@XMM[6], 0x30($out)
	pxor	@XMM[13], @XMM[7]
	movdqu	@XMM[3], 0x40($out)
	pxor	@XMM[14], @XMM[2]
	movdqu	@XMM[7], 0x50($out)
	pxor	@XMM[15], @XMM[5]
	movdqu	@XMM[2], 0x60($out)
	lea	.LADD1(%rip), %r11
	movdqu	@XMM[5], 0x70($out)
	lea	0x80($out), $out
	paddd	0x70(%r11), @XMM[0]	# .LADD8
	jnz	.Lctr_enc_loop

	jmp	.Lctr_enc_done
.align	16
.Lctr_enc_loop_done:
	add	\$8, $len
	movdqu	0x00($inp), @XMM[8]	# load input
	pxor	@XMM[8], @XMM[0]
	movdqu	@XMM[0], 0x00($out)	# write output
	cmp	\$2,$len
	jb	.Lctr_enc_done
	movdqu	0x10($inp), @XMM[9]
	pxor	@XMM[9], @XMM[1]
	movdqu	@XMM[1], 0x10($out)
	je	.Lctr_enc_done
	movdqu	0x20($inp), @XMM[10]
	pxor	@XMM[10], @XMM[4]
	movdqu	@XMM[4], 0x20($out)
	cmp	\$4,$len
	jb	.Lctr_enc_done
	movdqu	0x30($inp), @XMM[11]
	pxor	@XMM[11], @XMM[6]
	movdqu	@XMM[6], 0x30($out)
	je	.Lctr_enc_done
	movdqu	0x40($inp), @XMM[12]
	pxor	@XMM[12], @XMM[3]
	movdqu	@XMM[3], 0x40($out)
	cmp	\$6,$len
	jb	.Lctr_enc_done
	movdqu	0x50($inp), @XMM[13]
	pxor	@XMM[13], @XMM[7]
	movdqu	@XMM[7], 0x50($out)
	je	.Lctr_enc_done
	movdqu	0x60($inp), @XMM[14]
	pxor	@XMM[14], @XMM[2]
	movdqu	@XMM[2], 0x60($out)
	jmp	.Lctr_enc_done

.align	16
.Lctr_enc_short:
	lea	0x20(%rbp), $arg1
	lea	0x30(%rbp), $arg2
	lea	($key), $arg3
	call	asm_AES_encrypt
	movdqu	($inp), @XMM[1]
	lea	16($inp), $inp
	mov	0x2c(%rbp), %eax	# load 32-bit counter
	bswap	%eax
	pxor	0x30(%rbp), @XMM[1]
	inc	%eax			# increment
	movdqu	@XMM[1], ($out)
	bswap	%eax
	lea	16($out), $out
	mov	%eax, 0x2c(%rsp)	# save 32-bit counter
	dec	$len
	jnz	.Lctr_enc_short

.Lctr_enc_done:
	lea	(%rsp), %rax
	pxor	%xmm0, %xmm0
.Lctr_enc_bzero:			# wipe key schedule [if any]
	movdqa	%xmm0, 0x00(%rax)
	movdqa	%xmm0, 0x10(%rax)
	lea	0x20(%rax), %rax
	cmp	%rax, %rbp
	ja	.Lctr_enc_bzero

	lea	0x78(%rbp),%rax
.cfi_def_cfa	%rax,8
___
$code.=<<___ if ($win64);
	movaps	0x40(%rbp), %xmm6
	movaps	0x50(%rbp), %xmm7
	movaps	0x60(%rbp), %xmm8
	movaps	0x70(%rbp), %xmm9
	movaps	0x80(%rbp), %xmm10
	movaps	0x90(%rbp), %xmm11
	movaps	0xa0(%rbp), %xmm12
	movaps	0xb0(%rbp), %xmm13
	movaps	0xc0(%rbp), %xmm14
	movaps	0xd0(%rbp), %xmm15
	lea	0xa0(%rax), %rax
.Lctr_enc_tail:
___
$code.=<<___;
	mov	-48(%rax), %r15
.cfi_restore	%r15
	mov	-40(%rax), %r14
.cfi_restore	%r14
	mov	-32(%rax), %r13
.cfi_restore	%r13
	mov	-24(%rax), %r12
.cfi_restore	%r12
	mov	-16(%rax), %rbx
.cfi_restore	%rbx
	mov	-8(%rax), %rbp
.cfi_restore	%rbp
	lea	(%rax), %rsp		# restore %rsp
.cfi_def_cfa_register	%rsp
.Lctr_enc_epilogue:
	ret
.cfi_endproc
.size	bsaes_ctr32_encrypt_blocks,.-bsaes_ctr32_encrypt_blocks
___
######################################################################
# void bsaes_xts_[en|de]crypt(const char *inp,char *out,size_t len,
#	const AES_KEY *key1, const AES_KEY *key2,
#	const unsigned char iv[16]);
#
my ($twmask,$twres,$twtmp)=@XMM[13..15];
$arg6=~s/d$//;

$code.=<<___;
.globl	bsaes_xts_encrypt
.type	bsaes_xts_encrypt,\@abi-omnipotent
.align	16
bsaes_xts_encrypt:
.cfi_startproc
	mov	%rsp, %rax
.Lxts_enc_prologue:
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	lea	-0x48(%rsp), %rsp
.cfi_adjust_cfa_offset	0x48
___
$code.=<<___ if ($win64);
	mov	0xa0(%rsp),$arg5	# pull key2
	mov	0xa8(%rsp),$arg6	# pull ivp
	lea	-0xa0(%rsp), %rsp
	movaps	%xmm6, 0x40(%rsp)
	movaps	%xmm7, 0x50(%rsp)
	movaps	%xmm8, 0x60(%rsp)
	movaps	%xmm9, 0x70(%rsp)
	movaps	%xmm10, 0x80(%rsp)
	movaps	%xmm11, 0x90(%rsp)
	movaps	%xmm12, 0xa0(%rsp)
	movaps	%xmm13, 0xb0(%rsp)
	movaps	%xmm14, 0xc0(%rsp)
	movaps	%xmm15, 0xd0(%rsp)
.Lxts_enc_body:
___
$code.=<<___;
	mov	%rsp, %rbp		# backup %rsp
.cfi_def_cfa_register	%rbp
	mov	$arg1, $inp		# backup arguments
	mov	$arg2, $out
	mov	$arg3, $len
	mov	$arg4, $key

	lea	($arg6), $arg1
	lea	0x20(%rbp), $arg2
	lea	($arg5), $arg3
	call	asm_AES_encrypt		# generate initial tweak

	mov	240($key), %eax		# rounds
	mov	$len, %rbx		# backup $len

	mov	%eax, %edx		# rounds
	shl	\$7, %rax		# 128 bytes per inner round key
	sub	\$`128-32`, %rax	# size of bit-sliced key schedule
	sub	%rax, %rsp

	mov	%rsp, %rax		# pass key schedule
	mov	$key, %rcx		# pass key
	mov	%edx, %r10d		# pass rounds
	call	_bsaes_key_convert
	pxor	%xmm6, %xmm7		# fix up last round key
	movdqa	%xmm7, (%rax)		# save last round key

	and	\$-16, $len
	sub	\$0x80, %rsp		# place for tweak[8]
	movdqa	0x20(%rbp), @XMM[7]	# initial tweak

	pxor	$twtmp, $twtmp
	movdqa	.Lxts_magic(%rip), $twmask
	pcmpgtd	@XMM[7], $twtmp		# broadcast upper bits

	sub	\$0x80, $len
	jc	.Lxts_enc_short
	jmp	.Lxts_enc_loop

.align	16
.Lxts_enc_loop:
___
    for ($i=0;$i<7;$i++) {
    $code.=<<___;
	pshufd	\$0x13, $twtmp, $twres
	pxor	$twtmp, $twtmp
	movdqa	@XMM[7], @XMM[$i]
	movdqa	@XMM[7], `0x10*$i`(%rsp)# save tweak[$i]
	paddq	@XMM[7], @XMM[7]	# psllq	1,$tweak
	pand	$twmask, $twres		# isolate carry and residue
	pcmpgtd	@XMM[7], $twtmp		# broadcast upper bits
	pxor	$twres, @XMM[7]
___
    $code.=<<___ if ($i>=1);
	movdqu	`0x10*($i-1)`($inp), @XMM[8+$i-1]
___
    $code.=<<___ if ($i>=2);
	pxor	@XMM[8+$i-2], @XMM[$i-2]# input[] ^ tweak[]
___
    }
$code.=<<___;
	movdqu	0x60($inp), @XMM[8+6]
	pxor	@XMM[8+5], @XMM[5]
	movdqu	0x70($inp), @XMM[8+7]
	lea	0x80($inp), $inp
	movdqa	@XMM[7], 0x70(%rsp)
	pxor	@XMM[8+6], @XMM[6]
	lea	0x80(%rsp), %rax	# pass key schedule
	pxor	@XMM[8+7], @XMM[7]
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_encrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[4]
	movdqu	@XMM[1], 0x10($out)
	pxor	0x30(%rsp), @XMM[6]
	movdqu	@XMM[4], 0x20($out)
	pxor	0x40(%rsp), @XMM[3]
	movdqu	@XMM[6], 0x30($out)
	pxor	0x50(%rsp), @XMM[7]
	movdqu	@XMM[3], 0x40($out)
	pxor	0x60(%rsp), @XMM[2]
	movdqu	@XMM[7], 0x50($out)
	pxor	0x70(%rsp), @XMM[5]
	movdqu	@XMM[2], 0x60($out)
	movdqu	@XMM[5], 0x70($out)
	lea	0x80($out), $out

	movdqa	0x70(%rsp), @XMM[7]	# prepare next iteration tweak
	pxor	$twtmp, $twtmp
	movdqa	.Lxts_magic(%rip), $twmask
	pcmpgtd	@XMM[7], $twtmp
	pshufd	\$0x13, $twtmp, $twres
	pxor	$twtmp, $twtmp
	paddq	@XMM[7], @XMM[7]	# psllq	1,$tweak
	pand	$twmask, $twres		# isolate carry and residue
	pcmpgtd	@XMM[7], $twtmp		# broadcast upper bits
	pxor	$twres, @XMM[7]

	sub	\$0x80,$len
	jnc	.Lxts_enc_loop

.Lxts_enc_short:
	add	\$0x80, $len
	jz	.Lxts_enc_done
___
    for ($i=0;$i<7;$i++) {
    $code.=<<___;
	pshufd	\$0x13, $twtmp, $twres
	pxor	$twtmp, $twtmp
	movdqa	@XMM[7], @XMM[$i]
	movdqa	@XMM[7], `0x10*$i`(%rsp)# save tweak[$i]
	paddq	@XMM[7], @XMM[7]	# psllq	1,$tweak
	pand	$twmask, $twres		# isolate carry and residue
	pcmpgtd	@XMM[7], $twtmp		# broadcast upper bits
	pxor	$twres, @XMM[7]
___
    $code.=<<___ if ($i>=1);
	movdqu	`0x10*($i-1)`($inp), @XMM[8+$i-1]
	cmp	\$`0x10*$i`,$len
	je	.Lxts_enc_$i
___
    $code.=<<___ if ($i>=2);
	pxor	@XMM[8+$i-2], @XMM[$i-2]# input[] ^ tweak[]
___
    }
$code.=<<___;
	movdqu	0x60($inp), @XMM[8+6]
	pxor	@XMM[8+5], @XMM[5]
	movdqa	@XMM[7], 0x70(%rsp)
	lea	0x70($inp), $inp
	pxor	@XMM[8+6], @XMM[6]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_encrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[4]
	movdqu	@XMM[1], 0x10($out)
	pxor	0x30(%rsp), @XMM[6]
	movdqu	@XMM[4], 0x20($out)
	pxor	0x40(%rsp), @XMM[3]
	movdqu	@XMM[6], 0x30($out)
	pxor	0x50(%rsp), @XMM[7]
	movdqu	@XMM[3], 0x40($out)
	pxor	0x60(%rsp), @XMM[2]
	movdqu	@XMM[7], 0x50($out)
	movdqu	@XMM[2], 0x60($out)
	lea	0x70($out), $out

	movdqa	0x70(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_6:
	pxor	@XMM[8+4], @XMM[4]
	lea	0x60($inp), $inp
	pxor	@XMM[8+5], @XMM[5]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_encrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[4]
	movdqu	@XMM[1], 0x10($out)
	pxor	0x30(%rsp), @XMM[6]
	movdqu	@XMM[4], 0x20($out)
	pxor	0x40(%rsp), @XMM[3]
	movdqu	@XMM[6], 0x30($out)
	pxor	0x50(%rsp), @XMM[7]
	movdqu	@XMM[3], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	lea	0x60($out), $out

	movdqa	0x60(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_5:
	pxor	@XMM[8+3], @XMM[3]
	lea	0x50($inp), $inp
	pxor	@XMM[8+4], @XMM[4]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_encrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[4]
	movdqu	@XMM[1], 0x10($out)
	pxor	0x30(%rsp), @XMM[6]
	movdqu	@XMM[4], 0x20($out)
	pxor	0x40(%rsp), @XMM[3]
	movdqu	@XMM[6], 0x30($out)
	movdqu	@XMM[3], 0x40($out)
	lea	0x50($out), $out

	movdqa	0x50(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_4:
	pxor	@XMM[8+2], @XMM[2]
	lea	0x40($inp), $inp
	pxor	@XMM[8+3], @XMM[3]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_encrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[4]
	movdqu	@XMM[1], 0x10($out)
	pxor	0x30(%rsp), @XMM[6]
	movdqu	@XMM[4], 0x20($out)
	movdqu	@XMM[6], 0x30($out)
	lea	0x40($out), $out

	movdqa	0x40(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_3:
	pxor	@XMM[8+1], @XMM[1]
	lea	0x30($inp), $inp
	pxor	@XMM[8+2], @XMM[2]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_encrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[4]
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[4], 0x20($out)
	lea	0x30($out), $out

	movdqa	0x30(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_2:
	pxor	@XMM[8+0], @XMM[0]
	lea	0x20($inp), $inp
	pxor	@XMM[8+1], @XMM[1]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_encrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	lea	0x20($out), $out

	movdqa	0x20(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_1:
	pxor	@XMM[0], @XMM[8]
	lea	0x10($inp), $inp
	movdqa	@XMM[8], 0x20(%rbp)
	lea	0x20(%rbp), $arg1
	lea	0x20(%rbp), $arg2
	lea	($key), $arg3
	call	asm_AES_encrypt		# doesn't touch %xmm
	pxor	0x20(%rbp), @XMM[0]	# ^= tweak[]
	#pxor	@XMM[8], @XMM[0]
	#lea	0x80(%rsp), %rax	# pass key schedule
	#mov	%edx, %r10d		# pass rounds
	#call	_bsaes_encrypt8
	#pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	movdqu	@XMM[0], 0x00($out)	# write output
	lea	0x10($out), $out

	movdqa	0x10(%rsp), @XMM[7]	# next iteration tweak

.Lxts_enc_done:
	and	\$15, %ebx
	jz	.Lxts_enc_ret
	mov	$out, %rdx

.Lxts_enc_steal:
	movzb	($inp), %eax
	movzb	-16(%rdx), %ecx
	lea	1($inp), $inp
	mov	%al, -16(%rdx)
	mov	%cl, 0(%rdx)
	lea	1(%rdx), %rdx
	sub	\$1,%ebx
	jnz	.Lxts_enc_steal

	movdqu	-16($out), @XMM[0]
	lea	0x20(%rbp), $arg1
	pxor	@XMM[7], @XMM[0]
	lea	0x20(%rbp), $arg2
	movdqa	@XMM[0], 0x20(%rbp)
	lea	($key), $arg3
	call	asm_AES_encrypt		# doesn't touch %xmm
	pxor	0x20(%rbp), @XMM[7]
	movdqu	@XMM[7], -16($out)

.Lxts_enc_ret:
	lea	(%rsp), %rax
	pxor	%xmm0, %xmm0
.Lxts_enc_bzero:			# wipe key schedule [if any]
	movdqa	%xmm0, 0x00(%rax)
	movdqa	%xmm0, 0x10(%rax)
	lea	0x20(%rax), %rax
	cmp	%rax, %rbp
	ja	.Lxts_enc_bzero

	lea	0x78(%rbp),%rax
.cfi_def_cfa	%rax,8
___
$code.=<<___ if ($win64);
	movaps	0x40(%rbp), %xmm6
	movaps	0x50(%rbp), %xmm7
	movaps	0x60(%rbp), %xmm8
	movaps	0x70(%rbp), %xmm9
	movaps	0x80(%rbp), %xmm10
	movaps	0x90(%rbp), %xmm11
	movaps	0xa0(%rbp), %xmm12
	movaps	0xb0(%rbp), %xmm13
	movaps	0xc0(%rbp), %xmm14
	movaps	0xd0(%rbp), %xmm15
	lea	0xa0(%rax), %rax
.Lxts_enc_tail:
___
$code.=<<___;
	mov	-48(%rax), %r15
.cfi_restore	%r15
	mov	-40(%rax), %r14
.cfi_restore	%r14
	mov	-32(%rax), %r13
.cfi_restore	%r13
	mov	-24(%rax), %r12
.cfi_restore	%r12
	mov	-16(%rax), %rbx
.cfi_restore	%rbx
	mov	-8(%rax), %rbp
.cfi_restore	%rbp
	lea	(%rax), %rsp		# restore %rsp
.cfi_def_cfa_register	%rsp
.Lxts_enc_epilogue:
	ret
.cfi_endproc
.size	bsaes_xts_encrypt,.-bsaes_xts_encrypt

.globl	bsaes_xts_decrypt
.type	bsaes_xts_decrypt,\@abi-omnipotent
.align	16
bsaes_xts_decrypt:
.cfi_startproc
	mov	%rsp, %rax
.Lxts_dec_prologue:
	push	%rbp
.cfi_push	%rbp
	push	%rbx
.cfi_push	%rbx
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	lea	-0x48(%rsp), %rsp
.cfi_adjust_cfa_offset	0x48
___
$code.=<<___ if ($win64);
	mov	0xa0(%rsp),$arg5	# pull key2
	mov	0xa8(%rsp),$arg6	# pull ivp
	lea	-0xa0(%rsp), %rsp
	movaps	%xmm6, 0x40(%rsp)
	movaps	%xmm7, 0x50(%rsp)
	movaps	%xmm8, 0x60(%rsp)
	movaps	%xmm9, 0x70(%rsp)
	movaps	%xmm10, 0x80(%rsp)
	movaps	%xmm11, 0x90(%rsp)
	movaps	%xmm12, 0xa0(%rsp)
	movaps	%xmm13, 0xb0(%rsp)
	movaps	%xmm14, 0xc0(%rsp)
	movaps	%xmm15, 0xd0(%rsp)
.Lxts_dec_body:
___
$code.=<<___;
	mov	%rsp, %rbp		# backup %rsp
	mov	$arg1, $inp		# backup arguments
	mov	$arg2, $out
	mov	$arg3, $len
	mov	$arg4, $key

	lea	($arg6), $arg1
	lea	0x20(%rbp), $arg2
	lea	($arg5), $arg3
	call	asm_AES_encrypt		# generate initial tweak

	mov	240($key), %eax		# rounds
	mov	$len, %rbx		# backup $len

	mov	%eax, %edx		# rounds
	shl	\$7, %rax		# 128 bytes per inner round key
	sub	\$`128-32`, %rax	# size of bit-sliced key schedule
	sub	%rax, %rsp

	mov	%rsp, %rax		# pass key schedule
	mov	$key, %rcx		# pass key
	mov	%edx, %r10d		# pass rounds
	call	_bsaes_key_convert
	pxor	(%rsp), %xmm7		# fix up round 0 key
	movdqa	%xmm6, (%rax)		# save last round key
	movdqa	%xmm7, (%rsp)

	xor	%eax, %eax		# if ($len%16) len-=16;
	and	\$-16, $len
	test	\$15, %ebx
	setnz	%al
	shl	\$4, %rax
	sub	%rax, $len

	sub	\$0x80, %rsp		# place for tweak[8]
	movdqa	0x20(%rbp), @XMM[7]	# initial tweak

	pxor	$twtmp, $twtmp
	movdqa	.Lxts_magic(%rip), $twmask
	pcmpgtd	@XMM[7], $twtmp		# broadcast upper bits

	sub	\$0x80, $len
	jc	.Lxts_dec_short
	jmp	.Lxts_dec_loop

.align	16
.Lxts_dec_loop:
___
    for ($i=0;$i<7;$i++) {
    $code.=<<___;
	pshufd	\$0x13, $twtmp, $twres
	pxor	$twtmp, $twtmp
	movdqa	@XMM[7], @XMM[$i]
	movdqa	@XMM[7], `0x10*$i`(%rsp)# save tweak[$i]
	paddq	@XMM[7], @XMM[7]	# psllq	1,$tweak
	pand	$twmask, $twres		# isolate carry and residue
	pcmpgtd	@XMM[7], $twtmp		# broadcast upper bits
	pxor	$twres, @XMM[7]
___
    $code.=<<___ if ($i>=1);
	movdqu	`0x10*($i-1)`($inp), @XMM[8+$i-1]
___
    $code.=<<___ if ($i>=2);
	pxor	@XMM[8+$i-2], @XMM[$i-2]# input[] ^ tweak[]
___
    }
$code.=<<___;
	movdqu	0x60($inp), @XMM[8+6]
	pxor	@XMM[8+5], @XMM[5]
	movdqu	0x70($inp), @XMM[8+7]
	lea	0x80($inp), $inp
	movdqa	@XMM[7], 0x70(%rsp)
	pxor	@XMM[8+6], @XMM[6]
	lea	0x80(%rsp), %rax	# pass key schedule
	pxor	@XMM[8+7], @XMM[7]
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_decrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[6]
	movdqu	@XMM[1], 0x10($out)
	pxor	0x30(%rsp), @XMM[4]
	movdqu	@XMM[6], 0x20($out)
	pxor	0x40(%rsp), @XMM[2]
	movdqu	@XMM[4], 0x30($out)
	pxor	0x50(%rsp), @XMM[7]
	movdqu	@XMM[2], 0x40($out)
	pxor	0x60(%rsp), @XMM[3]
	movdqu	@XMM[7], 0x50($out)
	pxor	0x70(%rsp), @XMM[5]
	movdqu	@XMM[3], 0x60($out)
	movdqu	@XMM[5], 0x70($out)
	lea	0x80($out), $out

	movdqa	0x70(%rsp), @XMM[7]	# prepare next iteration tweak
	pxor	$twtmp, $twtmp
	movdqa	.Lxts_magic(%rip), $twmask
	pcmpgtd	@XMM[7], $twtmp
	pshufd	\$0x13, $twtmp, $twres
	pxor	$twtmp, $twtmp
	paddq	@XMM[7], @XMM[7]	# psllq	1,$tweak
	pand	$twmask, $twres		# isolate carry and residue
	pcmpgtd	@XMM[7], $twtmp		# broadcast upper bits
	pxor	$twres, @XMM[7]

	sub	\$0x80,$len
	jnc	.Lxts_dec_loop

.Lxts_dec_short:
	add	\$0x80, $len
	jz	.Lxts_dec_done
___
    for ($i=0;$i<7;$i++) {
    $code.=<<___;
	pshufd	\$0x13, $twtmp, $twres
	pxor	$twtmp, $twtmp
	movdqa	@XMM[7], @XMM[$i]
	movdqa	@XMM[7], `0x10*$i`(%rsp)# save tweak[$i]
	paddq	@XMM[7], @XMM[7]	# psllq	1,$tweak
	pand	$twmask, $twres		# isolate carry and residue
	pcmpgtd	@XMM[7], $twtmp		# broadcast upper bits
	pxor	$twres, @XMM[7]
___
    $code.=<<___ if ($i>=1);
	movdqu	`0x10*($i-1)`($inp), @XMM[8+$i-1]
	cmp	\$`0x10*$i`,$len
	je	.Lxts_dec_$i
___
    $code.=<<___ if ($i>=2);
	pxor	@XMM[8+$i-2], @XMM[$i-2]# input[] ^ tweak[]
___
    }
$code.=<<___;
	movdqu	0x60($inp), @XMM[8+6]
	pxor	@XMM[8+5], @XMM[5]
	movdqa	@XMM[7], 0x70(%rsp)
	lea	0x70($inp), $inp
	pxor	@XMM[8+6], @XMM[6]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_decrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[6]
	movdqu	@XMM[1], 0x10($out)
	pxor	0x30(%rsp), @XMM[4]
	movdqu	@XMM[6], 0x20($out)
	pxor	0x40(%rsp), @XMM[2]
	movdqu	@XMM[4], 0x30($out)
	pxor	0x50(%rsp), @XMM[7]
	movdqu	@XMM[2], 0x40($out)
	pxor	0x60(%rsp), @XMM[3]
	movdqu	@XMM[7], 0x50($out)
	movdqu	@XMM[3], 0x60($out)
	lea	0x70($out), $out

	movdqa	0x70(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_6:
	pxor	@XMM[8+4], @XMM[4]
	lea	0x60($inp), $inp
	pxor	@XMM[8+5], @XMM[5]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_decrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[6]
	movdqu	@XMM[1], 0x10($out)
	pxor	0x30(%rsp), @XMM[4]
	movdqu	@XMM[6], 0x20($out)
	pxor	0x40(%rsp), @XMM[2]
	movdqu	@XMM[4], 0x30($out)
	pxor	0x50(%rsp), @XMM[7]
	movdqu	@XMM[2], 0x40($out)
	movdqu	@XMM[7], 0x50($out)
	lea	0x60($out), $out

	movdqa	0x60(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_5:
	pxor	@XMM[8+3], @XMM[3]
	lea	0x50($inp), $inp
	pxor	@XMM[8+4], @XMM[4]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_decrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[6]
	movdqu	@XMM[1], 0x10($out)
	pxor	0x30(%rsp), @XMM[4]
	movdqu	@XMM[6], 0x20($out)
	pxor	0x40(%rsp), @XMM[2]
	movdqu	@XMM[4], 0x30($out)
	movdqu	@XMM[2], 0x40($out)
	lea	0x50($out), $out

	movdqa	0x50(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_4:
	pxor	@XMM[8+2], @XMM[2]
	lea	0x40($inp), $inp
	pxor	@XMM[8+3], @XMM[3]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_decrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[6]
	movdqu	@XMM[1], 0x10($out)
	pxor	0x30(%rsp), @XMM[4]
	movdqu	@XMM[6], 0x20($out)
	movdqu	@XMM[4], 0x30($out)
	lea	0x40($out), $out

	movdqa	0x40(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_3:
	pxor	@XMM[8+1], @XMM[1]
	lea	0x30($inp), $inp
	pxor	@XMM[8+2], @XMM[2]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_decrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	pxor	0x20(%rsp), @XMM[6]
	movdqu	@XMM[1], 0x10($out)
	movdqu	@XMM[6], 0x20($out)
	lea	0x30($out), $out

	movdqa	0x30(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_2:
	pxor	@XMM[8+0], @XMM[0]
	lea	0x20($inp), $inp
	pxor	@XMM[8+1], @XMM[1]
	lea	0x80(%rsp), %rax	# pass key schedule
	mov	%edx, %r10d		# pass rounds

	call	_bsaes_decrypt8

	pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	pxor	0x10(%rsp), @XMM[1]
	movdqu	@XMM[0], 0x00($out)	# write output
	movdqu	@XMM[1], 0x10($out)
	lea	0x20($out), $out

	movdqa	0x20(%rsp), @XMM[7]	# next iteration tweak
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_1:
	pxor	@XMM[0], @XMM[8]
	lea	0x10($inp), $inp
	movdqa	@XMM[8], 0x20(%rbp)
	lea	0x20(%rbp), $arg1
	lea	0x20(%rbp), $arg2
	lea	($key), $arg3
	call	asm_AES_decrypt		# doesn't touch %xmm
	pxor	0x20(%rbp), @XMM[0]	# ^= tweak[]
	#pxor	@XMM[8], @XMM[0]
	#lea	0x80(%rsp), %rax	# pass key schedule
	#mov	%edx, %r10d		# pass rounds
	#call	_bsaes_decrypt8
	#pxor	0x00(%rsp), @XMM[0]	# ^= tweak[]
	movdqu	@XMM[0], 0x00($out)	# write output
	lea	0x10($out), $out

	movdqa	0x10(%rsp), @XMM[7]	# next iteration tweak

.Lxts_dec_done:
	and	\$15, %ebx
	jz	.Lxts_dec_ret

	pxor	$twtmp, $twtmp
	movdqa	.Lxts_magic(%rip), $twmask
	pcmpgtd	@XMM[7], $twtmp
	pshufd	\$0x13, $twtmp, $twres
	movdqa	@XMM[7], @XMM[6]
	paddq	@XMM[7], @XMM[7]	# psllq 1,$tweak
	pand	$twmask, $twres		# isolate carry and residue
	movdqu	($inp), @XMM[0]
	pxor	$twres, @XMM[7]

	lea	0x20(%rbp), $arg1
	pxor	@XMM[7], @XMM[0]
	lea	0x20(%rbp), $arg2
	movdqa	@XMM[0], 0x20(%rbp)
	lea	($key), $arg3
	call	asm_AES_decrypt		# doesn't touch %xmm
	pxor	0x20(%rbp), @XMM[7]
	mov	$out, %rdx
	movdqu	@XMM[7], ($out)

.Lxts_dec_steal:
	movzb	16($inp), %eax
	movzb	(%rdx), %ecx
	lea	1($inp), $inp
	mov	%al, (%rdx)
	mov	%cl, 16(%rdx)
	lea	1(%rdx), %rdx
	sub	\$1,%ebx
	jnz	.Lxts_dec_steal

	movdqu	($out), @XMM[0]
	lea	0x20(%rbp), $arg1
	pxor	@XMM[6], @XMM[0]
	lea	0x20(%rbp), $arg2
	movdqa	@XMM[0], 0x20(%rbp)
	lea	($key), $arg3
	call	asm_AES_decrypt		# doesn't touch %xmm
	pxor	0x20(%rbp), @XMM[6]
	movdqu	@XMM[6], ($out)

.Lxts_dec_ret:
	lea	(%rsp), %rax
	pxor	%xmm0, %xmm0
.Lxts_dec_bzero:			# wipe key schedule [if any]
	movdqa	%xmm0, 0x00(%rax)
	movdqa	%xmm0, 0x10(%rax)
	lea	0x20(%rax), %rax
	cmp	%rax, %rbp
	ja	.Lxts_dec_bzero

	lea	0x78(%rbp),%rax
.cfi_def_cfa	%rax,8
___
$code.=<<___ if ($win64);
	movaps	0x40(%rbp), %xmm6
	movaps	0x50(%rbp), %xmm7
	movaps	0x60(%rbp), %xmm8
	movaps	0x70(%rbp), %xmm9
	movaps	0x80(%rbp), %xmm10
	movaps	0x90(%rbp), %xmm11
	movaps	0xa0(%rbp), %xmm12
	movaps	0xb0(%rbp), %xmm13
	movaps	0xc0(%rbp), %xmm14
	movaps	0xd0(%rbp), %xmm15
	lea	0xa0(%rax), %rax
.Lxts_dec_tail:
___
$code.=<<___;
	mov	-48(%rax), %r15
.cfi_restore	%r15
	mov	-40(%rax), %r14
.cfi_restore	%r14
	mov	-32(%rax), %r13
.cfi_restore	%r13
	mov	-24(%rax), %r12
.cfi_restore	%r12
	mov	-16(%rax), %rbx
.cfi_restore	%rbx
	mov	-8(%rax), %rbp
.cfi_restore	%rbp
	lea	(%rax), %rsp		# restore %rsp
.cfi_def_cfa_register	%rsp
.Lxts_dec_epilogue:
	ret
.cfi_endproc
.size	bsaes_xts_decrypt,.-bsaes_xts_decrypt
___
}
$code.=<<___;
.type	_bsaes_const,\@object
.align	64
_bsaes_const:
.LM0ISR:	# InvShiftRows constants
	.quad	0x0a0e0206070b0f03, 0x0004080c0d010509
.LISRM0:
	.quad	0x01040b0e0205080f, 0x0306090c00070a0d
.LISR:
	.quad	0x0504070602010003, 0x0f0e0d0c080b0a09
.LBS0:		# bit-slice constants
	.quad	0x5555555555555555, 0x5555555555555555
.LBS1:
	.quad	0x3333333333333333, 0x3333333333333333
.LBS2:
	.quad	0x0f0f0f0f0f0f0f0f, 0x0f0f0f0f0f0f0f0f
.LSR:		# shiftrows constants
	.quad	0x0504070600030201, 0x0f0e0d0c0a09080b
.LSRM0:
	.quad	0x0304090e00050a0f, 0x01060b0c0207080d
.LM0SR:
	.quad	0x0a0e02060f03070b, 0x0004080c05090d01
.LSWPUP:	# byte-swap upper dword
	.quad	0x0706050403020100, 0x0c0d0e0f0b0a0908
.LSWPUPM0SR:
	.quad	0x0a0d02060c03070b, 0x0004080f05090e01
.LADD1:		# counter increment constants
	.quad	0x0000000000000000, 0x0000000100000000
.LADD2:
	.quad	0x0000000000000000, 0x0000000200000000
.LADD3:
	.quad	0x0000000000000000, 0x0000000300000000
.LADD4:
	.quad	0x0000000000000000, 0x0000000400000000
.LADD5:
	.quad	0x0000000000000000, 0x0000000500000000
.LADD6:
	.quad	0x0000000000000000, 0x0000000600000000
.LADD7:
	.quad	0x0000000000000000, 0x0000000700000000
.LADD8:
	.quad	0x0000000000000000, 0x0000000800000000
.Lxts_magic:
	.long	0x87,0,1,0
.Lmasks:
	.quad	0x0101010101010101, 0x0101010101010101
	.quad	0x0202020202020202, 0x0202020202020202
	.quad	0x0404040404040404, 0x0404040404040404
	.quad	0x0808080808080808, 0x0808080808080808
.LM0:
	.quad	0x02060a0e03070b0f, 0x0004080c0105090d
.L63:
	.quad	0x6363636363636363, 0x6363636363636363
.asciz	"Bit-sliced AES for x86_64/SSSE3, Emilia Käsper, Peter Schwabe, Andy Polyakov"
.align	64
.size	_bsaes_const,.-_bsaes_const
___

# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
if ($win64) {
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___;
.extern	__imp_RtlVirtualUnwind
.type	se_handler,\@abi-omnipotent
.align	16
se_handler:
	push	%rsi
	push	%rdi
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	pushfq
	sub	\$64,%rsp

	mov	120($context),%rax	# pull context->Rax
	mov	248($context),%rbx	# pull context->Rip

	mov	8($disp),%rsi		# disp->ImageBase
	mov	56($disp),%r11		# disp->HandlerData

	mov	0(%r11),%r10d		# HandlerData[0]
	lea	(%rsi,%r10),%r10	# prologue label
	cmp	%r10,%rbx		# context->Rip<=prologue label
	jbe	.Lin_prologue

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lin_prologue

	mov	8(%r11),%r10d		# HandlerData[2]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=tail label
	jae	.Lin_tail

	mov	160($context),%rax	# pull context->Rbp

	lea	0x40(%rax),%rsi		# %xmm save area
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$20,%ecx		# 10*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq
	lea	0xa0+0x78(%rax),%rax	# adjust stack pointer

.Lin_tail:
	mov	-48(%rax),%rbp
	mov	-40(%rax),%rbx
	mov	-32(%rax),%r12
	mov	-24(%rax),%r13
	mov	-16(%rax),%r14
	mov	-8(%rax),%r15
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12
	mov	%r13,224($context)	# restore context->R13
	mov	%r14,232($context)	# restore context->R14
	mov	%r15,240($context)	# restore context->R15

.Lin_prologue:
	mov	%rax,152($context)	# restore context->Rsp

	mov	40($disp),%rdi		# disp->ContextRecord
	mov	$context,%rsi		# context
	mov	\$`1232/8`,%ecx		# sizeof(CONTEXT)
	.long	0xa548f3fc		# cld; rep movsq

	mov	$disp,%rsi
	xor	%rcx,%rcx		# arg1, UNW_FLAG_NHANDLER
	mov	8(%rsi),%rdx		# arg2, disp->ImageBase
	mov	0(%rsi),%r8		# arg3, disp->ControlPc
	mov	16(%rsi),%r9		# arg4, disp->FunctionEntry
	mov	40(%rsi),%r10		# disp->ContextRecord
	lea	56(%rsi),%r11		# &disp->HandlerData
	lea	24(%rsi),%r12		# &disp->EstablisherFrame
	mov	%r10,32(%rsp)		# arg5
	mov	%r11,40(%rsp)		# arg6
	mov	%r12,48(%rsp)		# arg7
	mov	%rcx,56(%rsp)		# arg8, (NULL)
	call	*__imp_RtlVirtualUnwind(%rip)

	mov	\$1,%eax		# ExceptionContinueSearch
	add	\$64,%rsp
	popfq
	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	pop	%rbp
	pop	%rbx
	pop	%rdi
	pop	%rsi
	ret
.size	se_handler,.-se_handler

.section	.pdata
.align	4
___
$code.=<<___ if ($ecb);
	.rva	.Lecb_enc_prologue
	.rva	.Lecb_enc_epilogue
	.rva	.Lecb_enc_info

	.rva	.Lecb_dec_prologue
	.rva	.Lecb_dec_epilogue
	.rva	.Lecb_dec_info
___
$code.=<<___;
	.rva	.Lcbc_dec_prologue
	.rva	.Lcbc_dec_epilogue
	.rva	.Lcbc_dec_info

	.rva	.Lctr_enc_prologue
	.rva	.Lctr_enc_epilogue
	.rva	.Lctr_enc_info

	.rva	.Lxts_enc_prologue
	.rva	.Lxts_enc_epilogue
	.rva	.Lxts_enc_info

	.rva	.Lxts_dec_prologue
	.rva	.Lxts_dec_epilogue
	.rva	.Lxts_dec_info

.section	.xdata
.align	8
___
$code.=<<___ if ($ecb);
.Lecb_enc_info:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lecb_enc_body,.Lecb_enc_epilogue	# HandlerData[]
	.rva	.Lecb_enc_tail
	.long	0
.Lecb_dec_info:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lecb_dec_body,.Lecb_dec_epilogue	# HandlerData[]
	.rva	.Lecb_dec_tail
	.long	0
___
$code.=<<___;
.Lcbc_dec_info:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lcbc_dec_body,.Lcbc_dec_epilogue	# HandlerData[]
	.rva	.Lcbc_dec_tail
	.long	0
.Lctr_enc_info:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lctr_enc_body,.Lctr_enc_epilogue	# HandlerData[]
	.rva	.Lctr_enc_tail
	.long	0
.Lxts_enc_info:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lxts_enc_body,.Lxts_enc_epilogue	# HandlerData[]
	.rva	.Lxts_enc_tail
	.long	0
.Lxts_dec_info:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lxts_dec_body,.Lxts_dec_epilogue	# HandlerData[]
	.rva	.Lxts_dec_tail
	.long	0
___
}

$code =~ s/\`([^\`]*)\`/eval($1)/gem;

print $code;

close STDOUT;

#! /usr/bin/env perl
# Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# Normal is the
# md5_block_x86(MD5_CTX *c, ULONG *X);
# version, non-normal is the
# md5_block_x86(MD5_CTX *c, ULONG *X,int blocks);

$normal=0;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$output=pop;
open STDOUT,">$output";

&asm_init($ARGV[0]);

$A="eax";
$B="ebx";
$C="ecx";
$D="edx";
$tmp1="edi";
$tmp2="ebp";
$X="esi";

# What we need to load into $tmp for the next round
%Ltmp1=("R0",&Np($C), "R1",&Np($C), "R2",&Np($C), "R3",&Np($D));
@xo=(
 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,	# R0
 1, 6, 11, 0, 5, 10, 15, 4, 9, 14, 3, 8, 13, 2, 7, 12,	# R1
 5, 8, 11, 14, 1, 4, 7, 10, 13, 0, 3, 6, 9, 12, 15, 2,	# R2
 0, 7, 14, 5, 12, 3, 10, 1, 8, 15, 6, 13, 4, 11, 2, 9,	# R3
 );

&md5_block("md5_block_asm_data_order");
&asm_finish();

close STDOUT;

sub Np
	{
	local($p)=@_;
	local(%n)=($A,$D,$B,$A,$C,$B,$D,$C);
	return($n{$p});
	}

sub R0
	{
	local($pos,$a,$b,$c,$d,$K,$ki,$s,$t)=@_;

	&mov($tmp1,$C)  if $pos < 0;
	&mov($tmp2,&DWP($xo[$ki]*4,$K,"",0)) if $pos < 0; # very first one

	# body proper

	&comment("R0 $ki");
	&xor($tmp1,$d); # F function - part 2

	&and($tmp1,$b); # F function - part 3
	&lea($a,&DWP($t,$a,$tmp2,1));

	&xor($tmp1,$d); # F function - part 4
	&mov($tmp2,&DWP($xo[$ki+1]*4,$K,"",0)) if ($pos != 2);

	&add($a,$tmp1);

	&rotl($a,$s);

	&mov($tmp1,&Np($c)) if $pos < 1;	# next tmp1 for R0
	&mov($tmp1,&Np($c)) if $pos == 1;	# next tmp1 for R1

	&add($a,$b);
	}

sub R1
	{
	local($pos,$a,$b,$c,$d,$K,$ki,$s,$t)=@_;

	&comment("R1 $ki");

	&xor($tmp1,$b); # G function - part 2
	&and($tmp1,$d); # G function - part 3
	&lea($a,&DWP($t,$a,$tmp2,1));

	&xor($tmp1,$c);			# G function - part 4
	&mov($tmp2,&DWP($xo[$ki+1]*4,$K,"",0)) if ($pos != 2);

	&add($a,$tmp1);
	&mov($tmp1,&Np($c)) if $pos < 1;	# G function - part 1
	&mov($tmp1,&Np($c)) if $pos == 1;	# G function - part 1

	&rotl($a,$s);

	&add($a,$b);
	}

sub R2
	{
	local($n,$pos,$a,$b,$c,$d,$K,$ki,$s,$t)=@_;
	# This one is different, only 3 logical operations

if (($n & 1) == 0)
	{
	&comment("R2 $ki");
	# make sure to do 'D' first, not 'B', else we clash with
	# the last add from the previous round.

	&xor($tmp1,$d); # H function - part 2

	&xor($tmp1,$b); # H function - part 3
	&lea($a,&DWP($t,$a,$tmp2,1));

	&add($a,$tmp1);
	&mov($tmp2,&DWP($xo[$ki+1]*4,$K,"",0));

	&rotl($a,$s);

	&mov($tmp1,&Np($c));
	}
else
	{
	&comment("R2 $ki");
	# make sure to do 'D' first, not 'B', else we clash with
	# the last add from the previous round.

	&add($b,$c);			# MOVED FORWARD
	&xor($tmp1,$d); # H function - part 2

	&lea($a,&DWP($t,$a,$tmp2,1));

	&xor($tmp1,$b); # H function - part 3
	&mov($tmp2,&DWP($xo[$ki+1]*4,$K,"",0)) if ($pos != 2);

	&add($a,$tmp1);
	&mov($tmp1,&Np($c)) if $pos < 1;	# H function - part 1
	&mov($tmp1,-1) if $pos == 1;		# I function - part 1

	&rotl($a,$s);

	&add($a,$b);
	}
	}

sub R3
	{
	local($pos,$a,$b,$c,$d,$K,$ki,$s,$t)=@_;

	&comment("R3 $ki");

	# &not($tmp1)
	&xor($tmp1,$d) if $pos < 0; 	# I function - part 2

	&or($tmp1,$b);				# I function - part 3
	&lea($a,&DWP($t,$a,$tmp2,1));

	&xor($tmp1,$c); 			# I function - part 4
	&mov($tmp2,&DWP($xo[$ki+1]*4,$K,"",0))	if $pos != 2; # load X/k value
	&mov($tmp2,&wparam(0)) if $pos == 2;

	&add($a,$tmp1);
	&mov($tmp1,-1) if $pos < 1;	# H function - part 1
	&add($K,64) if $pos >=1 && !$normal;

	&rotl($a,$s);

	&xor($tmp1,&Np($d)) if $pos <= 0; 	# I function - part = first time
	&mov($tmp1,&DWP( 0,$tmp2,"",0)) if $pos > 0;
	&add($a,$b);
	}


sub md5_block
	{
	local($name)=@_;

	&function_begin_B($name,"",3);

	# parameter 1 is the MD5_CTX structure.
	# A	0
	# B	4
	# C	8
	# D 	12

	&push("esi");
	 &push("edi");
	&mov($tmp1,	&wparam(0)); # edi
	 &mov($X,	&wparam(1)); # esi
	&mov($C,	&wparam(2));
	 &push("ebp");
	&shl($C,	6);
	&push("ebx");
	 &add($C,	$X); # offset we end at
	&sub($C,	64);
	 &mov($A,	&DWP( 0,$tmp1,"",0));
	&push($C);	# Put on the TOS
	 &mov($B,	&DWP( 4,$tmp1,"",0));
	&mov($C,	&DWP( 8,$tmp1,"",0));
	 &mov($D,	&DWP(12,$tmp1,"",0));

	&set_label("start") unless $normal;
	&comment("");
	&comment("R0 section");

	&R0(-2,$A,$B,$C,$D,$X, 0, 7,0xd76aa478);
	&R0( 0,$D,$A,$B,$C,$X, 1,12,0xe8c7b756);
	&R0( 0,$C,$D,$A,$B,$X, 2,17,0x242070db);
	&R0( 0,$B,$C,$D,$A,$X, 3,22,0xc1bdceee);
	&R0( 0,$A,$B,$C,$D,$X, 4, 7,0xf57c0faf);
	&R0( 0,$D,$A,$B,$C,$X, 5,12,0x4787c62a);
	&R0( 0,$C,$D,$A,$B,$X, 6,17,0xa8304613);
	&R0( 0,$B,$C,$D,$A,$X, 7,22,0xfd469501);
	&R0( 0,$A,$B,$C,$D,$X, 8, 7,0x698098d8);
	&R0( 0,$D,$A,$B,$C,$X, 9,12,0x8b44f7af);
	&R0( 0,$C,$D,$A,$B,$X,10,17,0xffff5bb1);
	&R0( 0,$B,$C,$D,$A,$X,11,22,0x895cd7be);
	&R0( 0,$A,$B,$C,$D,$X,12, 7,0x6b901122);
	&R0( 0,$D,$A,$B,$C,$X,13,12,0xfd987193);
	&R0( 0,$C,$D,$A,$B,$X,14,17,0xa679438e);
	&R0( 1,$B,$C,$D,$A,$X,15,22,0x49b40821);

	&comment("");
	&comment("R1 section");
	&R1(-1,$A,$B,$C,$D,$X,16, 5,0xf61e2562);
	&R1( 0,$D,$A,$B,$C,$X,17, 9,0xc040b340);
	&R1( 0,$C,$D,$A,$B,$X,18,14,0x265e5a51);
	&R1( 0,$B,$C,$D,$A,$X,19,20,0xe9b6c7aa);
	&R1( 0,$A,$B,$C,$D,$X,20, 5,0xd62f105d);
	&R1( 0,$D,$A,$B,$C,$X,21, 9,0x02441453);
	&R1( 0,$C,$D,$A,$B,$X,22,14,0xd8a1e681);
	&R1( 0,$B,$C,$D,$A,$X,23,20,0xe7d3fbc8);
	&R1( 0,$A,$B,$C,$D,$X,24, 5,0x21e1cde6);
	&R1( 0,$D,$A,$B,$C,$X,25, 9,0xc33707d6);
	&R1( 0,$C,$D,$A,$B,$X,26,14,0xf4d50d87);
	&R1( 0,$B,$C,$D,$A,$X,27,20,0x455a14ed);
	&R1( 0,$A,$B,$C,$D,$X,28, 5,0xa9e3e905);
	&R1( 0,$D,$A,$B,$C,$X,29, 9,0xfcefa3f8);
	&R1( 0,$C,$D,$A,$B,$X,30,14,0x676f02d9);
	&R1( 1,$B,$C,$D,$A,$X,31,20,0x8d2a4c8a);

	&comment("");
	&comment("R2 section");
	&R2( 0,-1,$A,$B,$C,$D,$X,32, 4,0xfffa3942);
	&R2( 1, 0,$D,$A,$B,$C,$X,33,11,0x8771f681);
	&R2( 2, 0,$C,$D,$A,$B,$X,34,16,0x6d9d6122);
	&R2( 3, 0,$B,$C,$D,$A,$X,35,23,0xfde5380c);
	&R2( 4, 0,$A,$B,$C,$D,$X,36, 4,0xa4beea44);
	&R2( 5, 0,$D,$A,$B,$C,$X,37,11,0x4bdecfa9);
	&R2( 6, 0,$C,$D,$A,$B,$X,38,16,0xf6bb4b60);
	&R2( 7, 0,$B,$C,$D,$A,$X,39,23,0xbebfbc70);
	&R2( 8, 0,$A,$B,$C,$D,$X,40, 4,0x289b7ec6);
	&R2( 9, 0,$D,$A,$B,$C,$X,41,11,0xeaa127fa);
	&R2(10, 0,$C,$D,$A,$B,$X,42,16,0xd4ef3085);
	&R2(11, 0,$B,$C,$D,$A,$X,43,23,0x04881d05);
	&R2(12, 0,$A,$B,$C,$D,$X,44, 4,0xd9d4d039);
	&R2(13, 0,$D,$A,$B,$C,$X,45,11,0xe6db99e5);
	&R2(14, 0,$C,$D,$A,$B,$X,46,16,0x1fa27cf8);
	&R2(15, 1,$B,$C,$D,$A,$X,47,23,0xc4ac5665);

	&comment("");
	&comment("R3 section");
	&R3(-1,$A,$B,$C,$D,$X,48, 6,0xf4292244);
	&R3( 0,$D,$A,$B,$C,$X,49,10,0x432aff97);
	&R3( 0,$C,$D,$A,$B,$X,50,15,0xab9423a7);
	&R3( 0,$B,$C,$D,$A,$X,51,21,0xfc93a039);
	&R3( 0,$A,$B,$C,$D,$X,52, 6,0x655b59c3);
	&R3( 0,$D,$A,$B,$C,$X,53,10,0x8f0ccc92);
	&R3( 0,$C,$D,$A,$B,$X,54,15,0xffeff47d);
	&R3( 0,$B,$C,$D,$A,$X,55,21,0x85845dd1);
	&R3( 0,$A,$B,$C,$D,$X,56, 6,0x6fa87e4f);
	&R3( 0,$D,$A,$B,$C,$X,57,10,0xfe2ce6e0);
	&R3( 0,$C,$D,$A,$B,$X,58,15,0xa3014314);
	&R3( 0,$B,$C,$D,$A,$X,59,21,0x4e0811a1);
	&R3( 0,$A,$B,$C,$D,$X,60, 6,0xf7537e82);
	&R3( 0,$D,$A,$B,$C,$X,61,10,0xbd3af235);
	&R3( 0,$C,$D,$A,$B,$X,62,15,0x2ad7d2bb);
	&R3( 2,$B,$C,$D,$A,$X,63,21,0xeb86d391);

	# &mov($tmp2,&wparam(0));	# done in the last R3
	# &mov($tmp1,	&DWP( 0,$tmp2,"",0)); # done is the last R3

	&add($A,$tmp1);
	 &mov($tmp1,	&DWP( 4,$tmp2,"",0));

	&add($B,$tmp1);
	&mov($tmp1,	&DWP( 8,$tmp2,"",0));

	&add($C,$tmp1);
	&mov($tmp1,	&DWP(12,$tmp2,"",0));

	&add($D,$tmp1);
	&mov(&DWP( 0,$tmp2,"",0),$A);

	&mov(&DWP( 4,$tmp2,"",0),$B);
	&mov($tmp1,&swtmp(0)) unless $normal;

	&mov(&DWP( 8,$tmp2,"",0),$C);
	 &mov(&DWP(12,$tmp2,"",0),$D);

	&cmp($tmp1,$X) unless $normal;			# check count
	 &jae(&label("start")) unless $normal;

	&pop("eax"); # pop the temp variable off the stack
	 &pop("ebx");
	&pop("ebp");
	 &pop("edi");
	&pop("esi");
	 &ret();
	&function_end_B($name);
	}


#! /usr/bin/env perl
# Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# Normal is the
# ripemd160_block_asm_data_order(RIPEMD160_CTX *c, ULONG *X,int blocks);

$normal=0;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$output=pop;
open STDOUT,">$output";

&asm_init($ARGV[0]);

$A="ecx";
$B="esi";
$C="edi";
$D="ebx";
$E="ebp";
$tmp1="eax";
$tmp2="edx";

$KL1=0x5A827999;
$KL2=0x6ED9EBA1;
$KL3=0x8F1BBCDC;
$KL4=0xA953FD4E;
$KR0=0x50A28BE6;
$KR1=0x5C4DD124;
$KR2=0x6D703EF3;
$KR3=0x7A6D76E9;


@wl=(	 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
	 7, 4,13, 1,10, 6,15, 3,12, 0, 9, 5, 2,14,11, 8,
	 3,10,14, 4, 9,15, 8, 1, 2, 7, 0, 6,13,11, 5,12,
	 1, 9,11,10, 0, 8,12, 4,13, 3, 7,15,14, 5, 6, 2,
	 4, 0, 5, 9, 7,12, 2,10,14, 1, 3, 8,11, 6,15,13,
	 );

@wr=(	 5,14, 7, 0, 9, 2,11, 4,13, 6,15, 8, 1,10, 3,12,
	 6,11, 3, 7, 0,13, 5,10,14,15, 8,12, 4, 9, 1, 2,
	15, 5, 1, 3, 7,14, 6, 9,11, 8,12, 2,10, 0, 4,13,
	 8, 6, 4, 1, 3,11,15, 0, 5,12, 2,13, 9, 7,10,14,
	12,15,10, 4, 1, 5, 8, 7, 6, 2,13,14, 0, 3, 9,11,
	);

@sl=(	11,14,15,12, 5, 8, 7, 9,11,13,14,15, 6, 7, 9, 8,
	 7, 6, 8,13,11, 9, 7,15, 7,12,15, 9,11, 7,13,12,
	11,13, 6, 7,14, 9,13,15,14, 8,13, 6, 5,12, 7, 5,
	11,12,14,15,14,15, 9, 8, 9,14, 5, 6, 8, 6, 5,12,
	 9,15, 5,11, 6, 8,13,12, 5,12,13,14,11, 8, 5, 6,
	 );

@sr=(	 8, 9, 9,11,13,15,15, 5, 7, 7, 8,11,14,14,12, 6,
	 9,13,15, 7,12, 8, 9,11, 7, 7,12, 7, 6,15,13,11,
	 9, 7,15,11, 8, 6, 6,14,12,13, 5,14,13,13, 7, 5,
	15, 5, 8,11,14,14, 6,14, 6, 9,12, 9,12, 5,15, 8,
	 8, 5,12, 9,12, 5,14, 6, 8,13, 6, 5,15,13,11,11,
 	);

&ripemd160_block("ripemd160_block_asm_data_order");
&asm_finish();

close STDOUT;

sub Xv
	{
	local($n)=@_;
	return(&swtmp($n));
	# tmp on stack
	}

sub Np
	{
	local($p)=@_;
	local(%n)=($A,$E,$B,$A,$C,$B,$D,$C,$E,$D);
	return($n{$p});
	}

sub RIP1
	{
	local($a,$b,$c,$d,$e,$pos,$s,$o,$pos2)=@_;

	&comment($p++);
	if ($p & 1)
		{
	 #&mov($tmp1,	$c) if $o == -1;
	&xor($tmp1,	$d) if $o == -1;
	 &mov($tmp2,	&Xv($pos));
	&xor($tmp1,	$b);
	 &add($a,	$tmp2);
	&rotl($c,	10);
	&add($a,	$tmp1);
	 &mov($tmp1,	&Np($c));	# NEXT
	 # XXX
	&rotl($a,	$s);
	&add($a,	$e);
		}
	else
		{
	 &xor($tmp1,	$d);
	&mov($tmp2,	&Xv($pos));
	 &xor($tmp1,	$b);
	&add($a,	$tmp1);
	 &mov($tmp1,	&Np($c)) if $o <= 0;
	 &mov($tmp1,	-1) if $o == 1;
	 # XXX if $o == 2;
	&rotl($c,	10);
	&add($a,	$tmp2);
	 &xor($tmp1,	&Np($d)) if $o <= 0;
	 &mov($tmp2,	&Xv($pos2)) if $o == 1;
	 &mov($tmp2,	&wparam(0)) if $o == 2;
	&rotl($a,	$s);
	&add($a,	$e);
		}
	}

sub RIP2
	{
	local($a,$b,$c,$d,$e,$pos,$pos2,$s,$K,$o)=@_;

# XXXXXX
	&comment($p++);
	if ($p & 1)
		{
#	 &mov($tmp2,	&Xv($pos)) if $o < -1;
#	&mov($tmp1,	-1) if $o < -1;

	 &add($a,	$tmp2);
	&mov($tmp2,	$c);
	 &sub($tmp1,	$b);
	&and($tmp2,	$b);
	 &and($tmp1,	$d);
	&or($tmp2,	$tmp1);
	 &mov($tmp1,	&Xv($pos2)) if $o <= 0; # XXXXXXXXXXXXXX
	 # XXX
	&rotl($c,	10);
	&lea($a,	&DWP($K,$a,$tmp2,1));
	 &mov($tmp2,	-1) if $o <= 0;
	 # XXX
	&rotl($a,	$s);
	&add($a,	$e);
		}
	else
		{
	 # XXX
	 &add($a,	$tmp1);
	&mov($tmp1,	$c);
	 &sub($tmp2,	$b);
	&and($tmp1,	$b);
	 &and($tmp2,	$d);
	if ($o != 2)
		{
	&or($tmp1,	$tmp2);
	 &mov($tmp2,	&Xv($pos2)) if $o <= 0;
	 &mov($tmp2,	-1) if $o == 1;
	&rotl($c,	10);
	&lea($a,	&DWP($K,$a,$tmp1,1));
	 &mov($tmp1,	-1) if $o <= 0;
	 &sub($tmp2,	&Np($c)) if $o == 1;
		} else {
	&or($tmp2,	$tmp1);
	 &mov($tmp1,	&Np($c));
	&rotl($c,	10);
	&lea($a,	&DWP($K,$a,$tmp2,1));
	 &xor($tmp1,	&Np($d));
		}
	&rotl($a,	$s);
	&add($a,	$e);
		}
	}

sub RIP3
	{
	local($a,$b,$c,$d,$e,$pos,$s,$K,$o,$pos2)=@_;

	&comment($p++);
	if ($p & 1)
		{
#	 &mov($tmp2,	-1) if $o < -1;
#	&sub($tmp2,	$c) if $o < -1;
	 &mov($tmp1,	&Xv($pos));
	&or($tmp2,	$b);
	 &add($a,	$tmp1);
	&xor($tmp2,	$d);
	 &mov($tmp1,	-1) if $o <= 0;		# NEXT
	 # XXX
	&rotl($c,	10);
	&lea($a,	&DWP($K,$a,$tmp2,1));
	 &sub($tmp1,	&Np($c)) if $o <= 0;	# NEXT
	 # XXX
	&rotl($a,	$s);
	&add($a,	$e);
		}
	else
		{
	 &mov($tmp2,	&Xv($pos));
	&or($tmp1,	$b);
	 &add($a,	$tmp2);
	&xor($tmp1,	$d);
	 &mov($tmp2,	-1) if $o <= 0;		# NEXT
	 &mov($tmp2,	-1) if $o == 1;
	 &mov($tmp2,	&Xv($pos2)) if $o == 2;
	&rotl($c,	10);
	&lea($a,	&DWP($K,$a,$tmp1,1));
	 &sub($tmp2,	&Np($c)) if $o <= 0;	# NEXT
	 &mov($tmp1,	&Np($d)) if $o == 1;
	 &mov($tmp1,	-1) if $o == 2;
	&rotl($a,	$s);
	&add($a,	$e);
		}
	}

sub RIP4
	{
	local($a,$b,$c,$d,$e,$pos,$s,$K,$o)=@_;

	&comment($p++);
	if ($p & 1)
		{
#	 &mov($tmp2,	-1) if $o == -2;
#	&mov($tmp1,	$d) if $o == -2;
	 &sub($tmp2,	$d);
	&and($tmp1,	$b);
	 &and($tmp2,	$c);
	&or($tmp2,	$tmp1);
	 &mov($tmp1,	&Xv($pos));
	&rotl($c,	10);
	&lea($a,	&DWP($K,$a,$tmp2));
	 &mov($tmp2,	-1) unless $o > 0;	# NEXT
	 # XXX
	&add($a,	$tmp1);
	 &mov($tmp1,	&Np($d)) unless $o > 0; # NEXT
	 # XXX
	&rotl($a,	$s);
	&add($a,	$e);
		}
	else
		{
	 &sub($tmp2,	$d);
	&and($tmp1,	$b);
	 &and($tmp2,	$c);
	&or($tmp2,	$tmp1);
	 &mov($tmp1,	&Xv($pos));
	&rotl($c,	10);
	&lea($a,	&DWP($K,$a,$tmp2));
	 &mov($tmp2,	-1) if $o == 0;	# NEXT
	 &mov($tmp2,	-1) if $o == 1;
	 &mov($tmp2,	-1) if $o == 2;
	 # XXX
	&add($a,	$tmp1);
	 &mov($tmp1,	&Np($d)) if $o == 0;	# NEXT
	 &sub($tmp2,	&Np($d)) if $o == 1;
	 &sub($tmp2,	&Np($c)) if $o == 2;
	 # XXX
	&rotl($a,	$s);
	&add($a,	$e);
		}
	}

sub RIP5
	{
	local($a,$b,$c,$d,$e,$pos,$s,$K,$o)=@_;

	&comment($p++);
	if ($p & 1)
		{
	 &mov($tmp2,	-1) if $o == -2;
	&sub($tmp2,	$d) if $o == -2;
	 &mov($tmp1,	&Xv($pos));
	&or($tmp2,	$c);
	 &add($a,	$tmp1);
	&xor($tmp2,	$b);
	 &mov($tmp1,	-1) if $o <= 0;
	 # XXX
	&rotl($c,	10);
	&lea($a,	&DWP($K,$a,$tmp2,1));
	 &sub($tmp1,	&Np($d)) if $o <= 0;
	 # XXX
	&rotl($a,	$s);
	&add($a,	$e);
		}
	else
		{
	 &mov($tmp2,	&Xv($pos));
	&or($tmp1,	$c);
	 &add($a,	$tmp2);
	&xor($tmp1,	$b);
	 &mov($tmp2,	-1) if $o <= 0;
	 &mov($tmp2,	&wparam(0)) if $o == 1;	# Middle code
	 &mov($tmp2,	-1) if $o == 2;
	&rotl($c,	10);
	&lea($a,	&DWP($K,$a,$tmp1,1));
	 &sub($tmp2,	&Np($d)) if $o <= 0;
	 &mov(&swtmp(16),	$A) if $o == 1;
	 &mov($tmp1,	&Np($d)) if $o == 2;
	&rotl($a,	$s);
	&add($a,	$e);
		}
	}

sub ripemd160_block
	{
	local($name)=@_;

	&function_begin_B($name,"",3);

	# parameter 1 is the RIPEMD160_CTX structure.
	# A	0
	# B	4
	# C	8
	# D 	12
	# E 	16

	&mov($tmp2,	&wparam(0));
	 &mov($tmp1,	&wparam(1));
	&push("esi");
	 &mov($A,	&DWP( 0,$tmp2,"",0));
	&push("edi");
	 &mov($B,	&DWP( 4,$tmp2,"",0));
	&push("ebp");
	 &mov($C,	&DWP( 8,$tmp2,"",0));
	&push("ebx");
	 &stack_push(16+5+6);
			  # Special comment about the figure of 6.
			  # Idea is to pad the current frame so
			  # that the top of the stack gets fairly
			  # aligned. Well, as you realize it would
			  # always depend on how the frame below is
			  # aligned. The good news are that gcc-2.95
			  # and later does keep first argument at
			  # least double-wise aligned.

	&set_label("start") unless $normal;
	&comment("");

	# &mov($tmp1,	&wparam(1)); # Done at end of loop
	# &mov($tmp2,	&wparam(0)); # Done at end of loop

	for ($z=0; $z<16; $z+=2)
		{
		&mov($D,		&DWP( $z*4,$tmp1,"",0));
		 &mov($E,		&DWP( ($z+1)*4,$tmp1,"",0));
		&mov(&swtmp($z),	$D);
		 &mov(&swtmp($z+1),	$E);
		}
	&mov($tmp1,	$C);
	 &mov($D,	&DWP(12,$tmp2,"",0));
	&mov($E,	&DWP(16,$tmp2,"",0));

	&RIP1($A,$B,$C,$D,$E,$wl[ 0],$sl[ 0],-1);
	&RIP1($E,$A,$B,$C,$D,$wl[ 1],$sl[ 1],0);
	&RIP1($D,$E,$A,$B,$C,$wl[ 2],$sl[ 2],0);
	&RIP1($C,$D,$E,$A,$B,$wl[ 3],$sl[ 3],0);
	&RIP1($B,$C,$D,$E,$A,$wl[ 4],$sl[ 4],0);
	&RIP1($A,$B,$C,$D,$E,$wl[ 5],$sl[ 5],0);
	&RIP1($E,$A,$B,$C,$D,$wl[ 6],$sl[ 6],0);
	&RIP1($D,$E,$A,$B,$C,$wl[ 7],$sl[ 7],0);
	&RIP1($C,$D,$E,$A,$B,$wl[ 8],$sl[ 8],0);
	&RIP1($B,$C,$D,$E,$A,$wl[ 9],$sl[ 9],0);
	&RIP1($A,$B,$C,$D,$E,$wl[10],$sl[10],0);
	&RIP1($E,$A,$B,$C,$D,$wl[11],$sl[11],0);
	&RIP1($D,$E,$A,$B,$C,$wl[12],$sl[12],0);
	&RIP1($C,$D,$E,$A,$B,$wl[13],$sl[13],0);
	&RIP1($B,$C,$D,$E,$A,$wl[14],$sl[14],0);
	&RIP1($A,$B,$C,$D,$E,$wl[15],$sl[15],1,$wl[16]);

	&RIP2($E,$A,$B,$C,$D,$wl[16],$wl[17],$sl[16],$KL1,-1);
	&RIP2($D,$E,$A,$B,$C,$wl[17],$wl[18],$sl[17],$KL1,0);
	&RIP2($C,$D,$E,$A,$B,$wl[18],$wl[19],$sl[18],$KL1,0);
	&RIP2($B,$C,$D,$E,$A,$wl[19],$wl[20],$sl[19],$KL1,0);
	&RIP2($A,$B,$C,$D,$E,$wl[20],$wl[21],$sl[20],$KL1,0);
	&RIP2($E,$A,$B,$C,$D,$wl[21],$wl[22],$sl[21],$KL1,0);
	&RIP2($D,$E,$A,$B,$C,$wl[22],$wl[23],$sl[22],$KL1,0);
	&RIP2($C,$D,$E,$A,$B,$wl[23],$wl[24],$sl[23],$KL1,0);
	&RIP2($B,$C,$D,$E,$A,$wl[24],$wl[25],$sl[24],$KL1,0);
	&RIP2($A,$B,$C,$D,$E,$wl[25],$wl[26],$sl[25],$KL1,0);
	&RIP2($E,$A,$B,$C,$D,$wl[26],$wl[27],$sl[26],$KL1,0);
	&RIP2($D,$E,$A,$B,$C,$wl[27],$wl[28],$sl[27],$KL1,0);
	&RIP2($C,$D,$E,$A,$B,$wl[28],$wl[29],$sl[28],$KL1,0);
	&RIP2($B,$C,$D,$E,$A,$wl[29],$wl[30],$sl[29],$KL1,0);
	&RIP2($A,$B,$C,$D,$E,$wl[30],$wl[31],$sl[30],$KL1,0);
	&RIP2($E,$A,$B,$C,$D,$wl[31],$wl[32],$sl[31],$KL1,1);

	&RIP3($D,$E,$A,$B,$C,$wl[32],$sl[32],$KL2,-1);
	&RIP3($C,$D,$E,$A,$B,$wl[33],$sl[33],$KL2,0);
	&RIP3($B,$C,$D,$E,$A,$wl[34],$sl[34],$KL2,0);
	&RIP3($A,$B,$C,$D,$E,$wl[35],$sl[35],$KL2,0);
	&RIP3($E,$A,$B,$C,$D,$wl[36],$sl[36],$KL2,0);
	&RIP3($D,$E,$A,$B,$C,$wl[37],$sl[37],$KL2,0);
	&RIP3($C,$D,$E,$A,$B,$wl[38],$sl[38],$KL2,0);
	&RIP3($B,$C,$D,$E,$A,$wl[39],$sl[39],$KL2,0);
	&RIP3($A,$B,$C,$D,$E,$wl[40],$sl[40],$KL2,0);
	&RIP3($E,$A,$B,$C,$D,$wl[41],$sl[41],$KL2,0);
	&RIP3($D,$E,$A,$B,$C,$wl[42],$sl[42],$KL2,0);
	&RIP3($C,$D,$E,$A,$B,$wl[43],$sl[43],$KL2,0);
	&RIP3($B,$C,$D,$E,$A,$wl[44],$sl[44],$KL2,0);
	&RIP3($A,$B,$C,$D,$E,$wl[45],$sl[45],$KL2,0);
	&RIP3($E,$A,$B,$C,$D,$wl[46],$sl[46],$KL2,0);
	&RIP3($D,$E,$A,$B,$C,$wl[47],$sl[47],$KL2,1);

	&RIP4($C,$D,$E,$A,$B,$wl[48],$sl[48],$KL3,-1);
	&RIP4($B,$C,$D,$E,$A,$wl[49],$sl[49],$KL3,0);
	&RIP4($A,$B,$C,$D,$E,$wl[50],$sl[50],$KL3,0);
	&RIP4($E,$A,$B,$C,$D,$wl[51],$sl[51],$KL3,0);
	&RIP4($D,$E,$A,$B,$C,$wl[52],$sl[52],$KL3,0);
	&RIP4($C,$D,$E,$A,$B,$wl[53],$sl[53],$KL3,0);
	&RIP4($B,$C,$D,$E,$A,$wl[54],$sl[54],$KL3,0);
	&RIP4($A,$B,$C,$D,$E,$wl[55],$sl[55],$KL3,0);
	&RIP4($E,$A,$B,$C,$D,$wl[56],$sl[56],$KL3,0);
	&RIP4($D,$E,$A,$B,$C,$wl[57],$sl[57],$KL3,0);
	&RIP4($C,$D,$E,$A,$B,$wl[58],$sl[58],$KL3,0);
	&RIP4($B,$C,$D,$E,$A,$wl[59],$sl[59],$KL3,0);
	&RIP4($A,$B,$C,$D,$E,$wl[60],$sl[60],$KL3,0);
	&RIP4($E,$A,$B,$C,$D,$wl[61],$sl[61],$KL3,0);
	&RIP4($D,$E,$A,$B,$C,$wl[62],$sl[62],$KL3,0);
	&RIP4($C,$D,$E,$A,$B,$wl[63],$sl[63],$KL3,1);

	&RIP5($B,$C,$D,$E,$A,$wl[64],$sl[64],$KL4,-1);
	&RIP5($A,$B,$C,$D,$E,$wl[65],$sl[65],$KL4,0);
	&RIP5($E,$A,$B,$C,$D,$wl[66],$sl[66],$KL4,0);
	&RIP5($D,$E,$A,$B,$C,$wl[67],$sl[67],$KL4,0);
	&RIP5($C,$D,$E,$A,$B,$wl[68],$sl[68],$KL4,0);
	&RIP5($B,$C,$D,$E,$A,$wl[69],$sl[69],$KL4,0);
	&RIP5($A,$B,$C,$D,$E,$wl[70],$sl[70],$KL4,0);
	&RIP5($E,$A,$B,$C,$D,$wl[71],$sl[71],$KL4,0);
	&RIP5($D,$E,$A,$B,$C,$wl[72],$sl[72],$KL4,0);
	&RIP5($C,$D,$E,$A,$B,$wl[73],$sl[73],$KL4,0);
	&RIP5($B,$C,$D,$E,$A,$wl[74],$sl[74],$KL4,0);
	&RIP5($A,$B,$C,$D,$E,$wl[75],$sl[75],$KL4,0);
	&RIP5($E,$A,$B,$C,$D,$wl[76],$sl[76],$KL4,0);
	&RIP5($D,$E,$A,$B,$C,$wl[77],$sl[77],$KL4,0);
	&RIP5($C,$D,$E,$A,$B,$wl[78],$sl[78],$KL4,0);
	&RIP5($B,$C,$D,$E,$A,$wl[79],$sl[79],$KL4,1);

	# &mov($tmp2,	&wparam(0)); # moved into last RIP5
	# &mov(&swtmp(16),	$A);
	 &mov($A,	&DWP( 0,$tmp2,"",0));
	&mov(&swtmp(16+1),	$B);
	 &mov(&swtmp(16+2),	$C);
	&mov($B,	&DWP( 4,$tmp2,"",0));
	 &mov(&swtmp(16+3),	$D);
	&mov($C,	&DWP( 8,$tmp2,"",0));
	 &mov(&swtmp(16+4),	$E);
	&mov($D,	&DWP(12,$tmp2,"",0));
	 &mov($E,	&DWP(16,$tmp2,"",0));

	&RIP5($A,$B,$C,$D,$E,$wr[ 0],$sr[ 0],$KR0,-2);
	&RIP5($E,$A,$B,$C,$D,$wr[ 1],$sr[ 1],$KR0,0);
	&RIP5($D,$E,$A,$B,$C,$wr[ 2],$sr[ 2],$KR0,0);
	&RIP5($C,$D,$E,$A,$B,$wr[ 3],$sr[ 3],$KR0,0);
	&RIP5($B,$C,$D,$E,$A,$wr[ 4],$sr[ 4],$KR0,0);
	&RIP5($A,$B,$C,$D,$E,$wr[ 5],$sr[ 5],$KR0,0);
	&RIP5($E,$A,$B,$C,$D,$wr[ 6],$sr[ 6],$KR0,0);
	&RIP5($D,$E,$A,$B,$C,$wr[ 7],$sr[ 7],$KR0,0);
	&RIP5($C,$D,$E,$A,$B,$wr[ 8],$sr[ 8],$KR0,0);
	&RIP5($B,$C,$D,$E,$A,$wr[ 9],$sr[ 9],$KR0,0);
	&RIP5($A,$B,$C,$D,$E,$wr[10],$sr[10],$KR0,0);
	&RIP5($E,$A,$B,$C,$D,$wr[11],$sr[11],$KR0,0);
	&RIP5($D,$E,$A,$B,$C,$wr[12],$sr[12],$KR0,0);
	&RIP5($C,$D,$E,$A,$B,$wr[13],$sr[13],$KR0,0);
	&RIP5($B,$C,$D,$E,$A,$wr[14],$sr[14],$KR0,0);
	&RIP5($A,$B,$C,$D,$E,$wr[15],$sr[15],$KR0,2);

	&RIP4($E,$A,$B,$C,$D,$wr[16],$sr[16],$KR1,-2);
	&RIP4($D,$E,$A,$B,$C,$wr[17],$sr[17],$KR1,0);
	&RIP4($C,$D,$E,$A,$B,$wr[18],$sr[18],$KR1,0);
	&RIP4($B,$C,$D,$E,$A,$wr[19],$sr[19],$KR1,0);
	&RIP4($A,$B,$C,$D,$E,$wr[20],$sr[20],$KR1,0);
	&RIP4($E,$A,$B,$C,$D,$wr[21],$sr[21],$KR1,0);
	&RIP4($D,$E,$A,$B,$C,$wr[22],$sr[22],$KR1,0);
	&RIP4($C,$D,$E,$A,$B,$wr[23],$sr[23],$KR1,0);
	&RIP4($B,$C,$D,$E,$A,$wr[24],$sr[24],$KR1,0);
	&RIP4($A,$B,$C,$D,$E,$wr[25],$sr[25],$KR1,0);
	&RIP4($E,$A,$B,$C,$D,$wr[26],$sr[26],$KR1,0);
	&RIP4($D,$E,$A,$B,$C,$wr[27],$sr[27],$KR1,0);
	&RIP4($C,$D,$E,$A,$B,$wr[28],$sr[28],$KR1,0);
	&RIP4($B,$C,$D,$E,$A,$wr[29],$sr[29],$KR1,0);
	&RIP4($A,$B,$C,$D,$E,$wr[30],$sr[30],$KR1,0);
	&RIP4($E,$A,$B,$C,$D,$wr[31],$sr[31],$KR1,2);

	&RIP3($D,$E,$A,$B,$C,$wr[32],$sr[32],$KR2,-2);
	&RIP3($C,$D,$E,$A,$B,$wr[33],$sr[33],$KR2,0);
	&RIP3($B,$C,$D,$E,$A,$wr[34],$sr[34],$KR2,0);
	&RIP3($A,$B,$C,$D,$E,$wr[35],$sr[35],$KR2,0);
	&RIP3($E,$A,$B,$C,$D,$wr[36],$sr[36],$KR2,0);
	&RIP3($D,$E,$A,$B,$C,$wr[37],$sr[37],$KR2,0);
	&RIP3($C,$D,$E,$A,$B,$wr[38],$sr[38],$KR2,0);
	&RIP3($B,$C,$D,$E,$A,$wr[39],$sr[39],$KR2,0);
	&RIP3($A,$B,$C,$D,$E,$wr[40],$sr[40],$KR2,0);
	&RIP3($E,$A,$B,$C,$D,$wr[41],$sr[41],$KR2,0);
	&RIP3($D,$E,$A,$B,$C,$wr[42],$sr[42],$KR2,0);
	&RIP3($C,$D,$E,$A,$B,$wr[43],$sr[43],$KR2,0);
	&RIP3($B,$C,$D,$E,$A,$wr[44],$sr[44],$KR2,0);
	&RIP3($A,$B,$C,$D,$E,$wr[45],$sr[45],$KR2,0);
	&RIP3($E,$A,$B,$C,$D,$wr[46],$sr[46],$KR2,0);
	&RIP3($D,$E,$A,$B,$C,$wr[47],$sr[47],$KR2,2,$wr[48]);

	&RIP2($C,$D,$E,$A,$B,$wr[48],$wr[49],$sr[48],$KR3,-2);
	&RIP2($B,$C,$D,$E,$A,$wr[49],$wr[50],$sr[49],$KR3,0);
	&RIP2($A,$B,$C,$D,$E,$wr[50],$wr[51],$sr[50],$KR3,0);
	&RIP2($E,$A,$B,$C,$D,$wr[51],$wr[52],$sr[51],$KR3,0);
	&RIP2($D,$E,$A,$B,$C,$wr[52],$wr[53],$sr[52],$KR3,0);
	&RIP2($C,$D,$E,$A,$B,$wr[53],$wr[54],$sr[53],$KR3,0);
	&RIP2($B,$C,$D,$E,$A,$wr[54],$wr[55],$sr[54],$KR3,0);
	&RIP2($A,$B,$C,$D,$E,$wr[55],$wr[56],$sr[55],$KR3,0);
	&RIP2($E,$A,$B,$C,$D,$wr[56],$wr[57],$sr[56],$KR3,0);
	&RIP2($D,$E,$A,$B,$C,$wr[57],$wr[58],$sr[57],$KR3,0);
	&RIP2($C,$D,$E,$A,$B,$wr[58],$wr[59],$sr[58],$KR3,0);
	&RIP2($B,$C,$D,$E,$A,$wr[59],$wr[60],$sr[59],$KR3,0);
	&RIP2($A,$B,$C,$D,$E,$wr[60],$wr[61],$sr[60],$KR3,0);
	&RIP2($E,$A,$B,$C,$D,$wr[61],$wr[62],$sr[61],$KR3,0);
	&RIP2($D,$E,$A,$B,$C,$wr[62],$wr[63],$sr[62],$KR3,0);
	&RIP2($C,$D,$E,$A,$B,$wr[63],$wr[64],$sr[63],$KR3,2);

	&RIP1($B,$C,$D,$E,$A,$wr[64],$sr[64],-2);
	&RIP1($A,$B,$C,$D,$E,$wr[65],$sr[65],0);
	&RIP1($E,$A,$B,$C,$D,$wr[66],$sr[66],0);
	&RIP1($D,$E,$A,$B,$C,$wr[67],$sr[67],0);
	&RIP1($C,$D,$E,$A,$B,$wr[68],$sr[68],0);
	&RIP1($B,$C,$D,$E,$A,$wr[69],$sr[69],0);
	&RIP1($A,$B,$C,$D,$E,$wr[70],$sr[70],0);
	&RIP1($E,$A,$B,$C,$D,$wr[71],$sr[71],0);
	&RIP1($D,$E,$A,$B,$C,$wr[72],$sr[72],0);
	&RIP1($C,$D,$E,$A,$B,$wr[73],$sr[73],0);
	&RIP1($B,$C,$D,$E,$A,$wr[74],$sr[74],0);
	&RIP1($A,$B,$C,$D,$E,$wr[75],$sr[75],0);
	&RIP1($E,$A,$B,$C,$D,$wr[76],$sr[76],0);
	&RIP1($D,$E,$A,$B,$C,$wr[77],$sr[77],0);
	&RIP1($C,$D,$E,$A,$B,$wr[78],$sr[78],0);
	&RIP1($B,$C,$D,$E,$A,$wr[79],$sr[79],2);

	# &mov($tmp2,	&wparam(0)); # Moved into last round

	 &mov($tmp1,	&DWP( 4,$tmp2,"",0));	# ctx->B
 	&add($D,	$tmp1);
	 &mov($tmp1,	&swtmp(16+2));		# $c
	&add($D,	$tmp1);

	 &mov($tmp1,	&DWP( 8,$tmp2,"",0));	# ctx->C
	&add($E,	$tmp1);
	 &mov($tmp1,	&swtmp(16+3));		# $d
	&add($E,	$tmp1);

	 &mov($tmp1,	&DWP(12,$tmp2,"",0));	# ctx->D
	&add($A,	$tmp1);
	 &mov($tmp1,	&swtmp(16+4));		# $e
	&add($A,	$tmp1);


	 &mov($tmp1,	&DWP(16,$tmp2,"",0));	# ctx->E
	&add($B,	$tmp1);
	 &mov($tmp1,	&swtmp(16+0));		# $a
	&add($B,	$tmp1);

	 &mov($tmp1,	&DWP( 0,$tmp2,"",0));	# ctx->A
	&add($C,	$tmp1);
	 &mov($tmp1,	&swtmp(16+1));		# $b
	&add($C,	$tmp1);

	 &mov($tmp1,	&wparam(2));

	&mov(&DWP( 0,$tmp2,"",0),	$D);
	 &mov(&DWP( 4,$tmp2,"",0),	$E);
	&mov(&DWP( 8,$tmp2,"",0),	$A);
	 &sub($tmp1,1);
	&mov(&DWP(12,$tmp2,"",0),	$B);
	 &mov(&DWP(16,$tmp2,"",0),	$C);

	&jle(&label("get_out"));

	&mov(&wparam(2),$tmp1);
	 &mov($C,	$A);
	&mov($tmp1,	&wparam(1));
	 &mov($A,	$D);
	&add($tmp1,	64);
	 &mov($B,	$E);
	&mov(&wparam(1),$tmp1);

	&jmp(&label("start"));

	&set_label("get_out");

	&stack_pop(16+5+6);

	&pop("ebx");
	&pop("ebp");
	&pop("edi");
	&pop("esi");
	&ret();
	&function_end_B($name);
	}


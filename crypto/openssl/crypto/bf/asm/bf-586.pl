#! /usr/bin/env perl
# Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";
require "cbc.pl";

$output = pop;
open STDOUT,">$output";

&asm_init($ARGV[0],$ARGV[$#ARGV] eq "386");

$BF_ROUNDS=16;
$BF_OFF=($BF_ROUNDS+2)*4;
$L="edi";
$R="esi";
$P="ebp";
$tmp1="eax";
$tmp2="ebx";
$tmp3="ecx";
$tmp4="edx";

&BF_encrypt("BF_encrypt",1);
&BF_encrypt("BF_decrypt",0);
&cbc("BF_cbc_encrypt","BF_encrypt","BF_decrypt",1,4,5,3,-1,-1);
&asm_finish();

close STDOUT;

sub BF_encrypt
	{
	local($name,$enc)=@_;

	&function_begin_B($name,"");

	&comment("");

	&push("ebp");
	&push("ebx");
	&mov($tmp2,&wparam(0));
	&mov($P,&wparam(1));
	&push("esi");
	&push("edi");

	&comment("Load the 2 words");
	&mov($L,&DWP(0,$tmp2,"",0));
	&mov($R,&DWP(4,$tmp2,"",0));

	&xor(	$tmp1,	$tmp1);

	# encrypting part

	if ($enc)
		{
		 &mov($tmp2,&DWP(0,$P,"",0));
		&xor(	$tmp3,	$tmp3);

		&xor($L,$tmp2);
		for ($i=0; $i<$BF_ROUNDS; $i+=2)
			{
			&comment("");
			&comment("Round $i");
			&BF_ENCRYPT($i+1,$R,$L,$P,$tmp1,$tmp2,$tmp3,$tmp4,1);

			&comment("");
			&comment("Round ".sprintf("%d",$i+1));
			&BF_ENCRYPT($i+2,$L,$R,$P,$tmp1,$tmp2,$tmp3,$tmp4,1);
			}
		# &mov($tmp1,&wparam(0)); In last loop
		&mov($tmp4,&DWP(($BF_ROUNDS+1)*4,$P,"",0));
		}
	else
		{
		 &mov($tmp2,&DWP(($BF_ROUNDS+1)*4,$P,"",0));
		&xor(	$tmp3,	$tmp3);

		&xor($L,$tmp2);
		for ($i=$BF_ROUNDS; $i>0; $i-=2)
			{
			&comment("");
			&comment("Round $i");
			&BF_ENCRYPT($i,$R,$L,$P,$tmp1,$tmp2,$tmp3,$tmp4,0);
			&comment("");
			&comment("Round ".sprintf("%d",$i-1));
			&BF_ENCRYPT($i-1,$L,$R,$P,$tmp1,$tmp2,$tmp3,$tmp4,0);
			}
		# &mov($tmp1,&wparam(0)); In last loop
		&mov($tmp4,&DWP(0,$P,"",0));
		}

	&xor($R,$tmp4);
	&mov(&DWP(4,$tmp1,"",0),$L);

	&mov(&DWP(0,$tmp1,"",0),$R);
	&function_end($name);
	}

sub BF_ENCRYPT
	{
	local($i,$L,$R,$P,$tmp1,$tmp2,$tmp3,$tmp4,$enc)=@_;

	&mov(	$tmp4,		&DWP(&n2a($i*4),$P,"",0)); # for next round

	&mov(	$tmp2,		$R);
	&xor(	$L,		$tmp4);

	&shr(	$tmp2,		16);
	&mov(	$tmp4,		$R);

	&movb(	&LB($tmp1),	&HB($tmp2));	# A
	&and(	$tmp2,		0xff);		# B

	&movb(	&LB($tmp3),	&HB($tmp4));	# C
	&and(	$tmp4,		0xff);		# D

	&mov(	$tmp1,		&DWP(&n2a($BF_OFF+0x0000),$P,$tmp1,4));
	&mov(	$tmp2,		&DWP(&n2a($BF_OFF+0x0400),$P,$tmp2,4));

	&add(	$tmp2,		$tmp1);
	&mov(	$tmp1,		&DWP(&n2a($BF_OFF+0x0800),$P,$tmp3,4));

	&xor(	$tmp2,		$tmp1);
	&mov(	$tmp4,		&DWP(&n2a($BF_OFF+0x0C00),$P,$tmp4,4));

	&add(	$tmp2,		$tmp4);
	if (($enc && ($i != 16)) || ((!$enc) && ($i != 1)))
		{ &xor(	$tmp1,		$tmp1); }
	else
		{
		&comment("Load parameter 0 ($i) enc=$enc");
		&mov($tmp1,&wparam(0));
		} # In last loop

	&xor(	$L,		$tmp2);
	# delay
	}

sub n2a
	{
	sprintf("%d",$_[0]);
	}


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

&asm_init($ARGV[0]);

$RC5_MAX_ROUNDS=16;
$RC5_32_OFF=($RC5_MAX_ROUNDS+2)*4;
$A="edi";
$B="esi";
$S="ebp";
$tmp1="eax";
$r="ebx";
$tmpc="ecx";
$tmp4="edx";

&RC5_32_encrypt("RC5_32_encrypt",1);
&RC5_32_encrypt("RC5_32_decrypt",0);
&cbc("RC5_32_cbc_encrypt","RC5_32_encrypt","RC5_32_decrypt",0,4,5,3,-1,-1);
&asm_finish();

close STDOUT;

sub RC5_32_encrypt
	{
	local($name,$enc)=@_;

	&function_begin_B($name,"");

	&comment("");

	&push("ebp");
	 &push("esi");
	&push("edi");
	 &mov($tmp4,&wparam(0));
	&mov($S,&wparam(1));

	&comment("Load the 2 words");
	 &mov($A,&DWP(0,$tmp4,"",0));
	&mov($B,&DWP(4,$tmp4,"",0));

	&push($r);
	 &mov($r,	&DWP(0,$S,"",0));

	# encrypting part

	if ($enc)
		{
		 &add($A,	&DWP(4+0,$S,"",0));
		&add($B,	&DWP(4+4,$S,"",0));

		for ($i=0; $i<$RC5_MAX_ROUNDS; $i++)
			{
			 &xor($A,	$B);
			&mov($tmp1,	&DWP(12+$i*8,$S,"",0));
			 &mov($tmpc,	$B);
			&rotl($A,	&LB("ecx"));
			&add($A,	$tmp1);

			 &xor($B,	$A);
			&mov($tmp1,	&DWP(16+$i*8,$S,"",0));
			 &mov($tmpc,	$A);
			&rotl($B,	&LB("ecx"));
			&add($B,	$tmp1);
			if (($i == 7) || ($i == 11))
				{
			 &cmp($r,	$i+1);
			&je(&label("rc5_exit"));
				}
			}
		}
	else
		{
		 &cmp($r,	12);
		&je(&label("rc5_dec_12"));
		 &cmp($r,	8);
		&je(&label("rc5_dec_8"));
		for ($i=$RC5_MAX_ROUNDS; $i > 0; $i--)
			{
			&set_label("rc5_dec_$i") if ($i == 12) || ($i == 8);
			 &mov($tmp1,	&DWP($i*8+8,$S,"",0));
			&sub($B,	$tmp1);
			 &mov($tmpc,	$A);
			&rotr($B,	&LB("ecx"));
			&xor($B,	$A);

			 &mov($tmp1,	&DWP($i*8+4,$S,"",0));
			&sub($A,	$tmp1);
			 &mov($tmpc,	$B);
			&rotr($A,	&LB("ecx"));
			&xor($A,	$B);
			}
		 &sub($B,	&DWP(4+4,$S,"",0));
		&sub($A,	&DWP(4+0,$S,"",0));
		}

	&set_label("rc5_exit");
	 &mov(&DWP(0,$tmp4,"",0),$A);
	&mov(&DWP(4,$tmp4,"",0),$B);

	 &pop("ebx");
	&pop("edi");
	 &pop("esi");
	&pop("ebp");
	 &ret();
	&function_end_B($name);
	}



#! /usr/bin/env perl
# Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# This flag makes the inner loop one cycle longer, but generates
# code that runs %30 faster on the pentium pro/II, 44% faster
# of PIII, while only %7 slower on the pentium.
# By default, this flag is on.
$ppro=1;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";
require "cbc.pl";

$output=pop;
open STDOUT,">$output";

&asm_init($ARGV[0],$ARGV[$#ARGV] eq "386");

$CAST_ROUNDS=16;
$L="edi";
$R="esi";
$K="ebp";
$tmp1="ecx";
$tmp2="ebx";
$tmp3="eax";
$tmp4="edx";
$S1="CAST_S_table0";
$S2="CAST_S_table1";
$S3="CAST_S_table2";
$S4="CAST_S_table3";

@F1=("add","xor","sub");
@F2=("xor","sub","add");
@F3=("sub","add","xor");

&CAST_encrypt("CAST_encrypt",1);
&CAST_encrypt("CAST_decrypt",0);
&cbc("CAST_cbc_encrypt","CAST_encrypt","CAST_decrypt",1,4,5,3,-1,-1);

&asm_finish();

close STDOUT;

sub CAST_encrypt {
    local($name,$enc)=@_;

    local($win_ex)=<<"EOF";
EXTERN	_CAST_S_table0:DWORD
EXTERN	_CAST_S_table1:DWORD
EXTERN	_CAST_S_table2:DWORD
EXTERN	_CAST_S_table3:DWORD
EOF
    &main::external_label(
			  "CAST_S_table0",
			  "CAST_S_table1",
			  "CAST_S_table2",
			  "CAST_S_table3",
			  );

    &function_begin_B($name,$win_ex);

    &comment("");

    &push("ebp");
    &push("ebx");
    &mov($tmp2,&wparam(0));
    &mov($K,&wparam(1));
    &push("esi");
    &push("edi");

    &comment("Load the 2 words");
    &mov($L,&DWP(0,$tmp2,"",0));
    &mov($R,&DWP(4,$tmp2,"",0));

    &comment('Get short key flag');
    &mov($tmp3,&DWP(128,$K,"",0));
    if($enc) {
	&push($tmp3);
    } else {
	&or($tmp3,$tmp3);
	&jnz(&label('cast_dec_skip'));
    }

    &xor($tmp3,	$tmp3);

    # encrypting part

    if ($enc) {
	&E_CAST( 0,$S,$L,$R,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 1,$S,$R,$L,$K,@F2,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 2,$S,$L,$R,$K,@F3,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 3,$S,$R,$L,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 4,$S,$L,$R,$K,@F2,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 5,$S,$R,$L,$K,@F3,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 6,$S,$L,$R,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 7,$S,$R,$L,$K,@F2,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 8,$S,$L,$R,$K,@F3,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 9,$S,$R,$L,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST(10,$S,$L,$R,$K,@F2,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST(11,$S,$R,$L,$K,@F3,$tmp1,$tmp2,$tmp3,$tmp4);
	&comment('test short key flag');
	&pop($tmp4);
	&or($tmp4,$tmp4);
	&jnz(&label('cast_enc_done'));
	&E_CAST(12,$S,$L,$R,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST(13,$S,$R,$L,$K,@F2,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST(14,$S,$L,$R,$K,@F3,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST(15,$S,$R,$L,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
    } else {
	&E_CAST(15,$S,$L,$R,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST(14,$S,$R,$L,$K,@F3,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST(13,$S,$L,$R,$K,@F2,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST(12,$S,$R,$L,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
	&set_label('cast_dec_skip');
	&E_CAST(11,$S,$L,$R,$K,@F3,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST(10,$S,$R,$L,$K,@F2,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 9,$S,$L,$R,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 8,$S,$R,$L,$K,@F3,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 7,$S,$L,$R,$K,@F2,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 6,$S,$R,$L,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 5,$S,$L,$R,$K,@F3,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 4,$S,$R,$L,$K,@F2,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 3,$S,$L,$R,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 2,$S,$R,$L,$K,@F3,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 1,$S,$L,$R,$K,@F2,$tmp1,$tmp2,$tmp3,$tmp4);
	&E_CAST( 0,$S,$R,$L,$K,@F1,$tmp1,$tmp2,$tmp3,$tmp4);
    }

    &set_label('cast_enc_done') if $enc;
# Why the nop? - Ben 17/1/99
    &nop();
    &mov($tmp3,&wparam(0));
    &mov(&DWP(4,$tmp3,"",0),$L);
    &mov(&DWP(0,$tmp3,"",0),$R);
    &function_end($name);
}

sub E_CAST {
    local($i,$S,$L,$R,$K,$OP1,$OP2,$OP3,$tmp1,$tmp2,$tmp3,$tmp4)=@_;
    # Ri needs to have 16 pre added.

    &comment("round $i");
    &mov(	$tmp4,		&DWP($i*8,$K,"",1));

    &mov(	$tmp1,		&DWP($i*8+4,$K,"",1));
    &$OP1(	$tmp4,		$R);

    &rotl(	$tmp4,		&LB($tmp1));

    if ($ppro) {
	&xor(	$tmp1,		$tmp1);
	&mov(	$tmp2,		0xff);

	&movb(	&LB($tmp1),	&HB($tmp4));	# A
	&and(	$tmp2,		$tmp4);

	&shr(	$tmp4,		16); 		#
	&xor(	$tmp3,		$tmp3);
    } else {
	&mov(	$tmp2,		$tmp4);		# B
	&movb(	&LB($tmp1),	&HB($tmp4));	# A	# BAD BAD BAD

	&shr(	$tmp4,		16); 		#
	&and(	$tmp2,		0xff);
    }

    &movb(	&LB($tmp3),	&HB($tmp4));	# C	# BAD BAD BAD
    &and(	$tmp4,		0xff);		# D

    &mov(	$tmp1,		&DWP($S1,"",$tmp1,4));
    &mov(	$tmp2,		&DWP($S2,"",$tmp2,4));

    &$OP2(	$tmp1,		$tmp2);
    &mov(	$tmp2,		&DWP($S3,"",$tmp3,4));

    &$OP3(	$tmp1,		$tmp2);
    &mov(	$tmp2,		&DWP($S4,"",$tmp4,4));

    &$OP1(	$tmp1,		$tmp2);
    # XXX

    &xor(	$L,		$tmp1);
    # XXX
}


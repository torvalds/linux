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

$output = pop;
open STDOUT,">$output";

&asm_init($ARGV[0]);

$sse2=0;
for (@ARGV) { $sse2=1 if (/-DOPENSSL_IA32_SSE2/); }

&external_label("OPENSSL_ia32cap_P") if ($sse2);

&bn_mul_add_words("bn_mul_add_words");
&bn_mul_words("bn_mul_words");
&bn_sqr_words("bn_sqr_words");
&bn_div_words("bn_div_words");
&bn_add_words("bn_add_words");
&bn_sub_words("bn_sub_words");
&bn_sub_part_words("bn_sub_part_words");

&asm_finish();

close STDOUT;

sub bn_mul_add_words
	{
	local($name)=@_;

	&function_begin_B($name,$sse2?"EXTRN\t_OPENSSL_ia32cap_P:DWORD":"");

	$r="eax";
	$a="edx";
	$c="ecx";

	if ($sse2) {
		&picmeup("eax","OPENSSL_ia32cap_P");
		&bt(&DWP(0,"eax"),26);
		&jnc(&label("maw_non_sse2"));

		&mov($r,&wparam(0));
		&mov($a,&wparam(1));
		&mov($c,&wparam(2));
		&movd("mm0",&wparam(3));	# mm0 = w
		&pxor("mm1","mm1");		# mm1 = carry_in
		&jmp(&label("maw_sse2_entry"));

	&set_label("maw_sse2_unrolled",16);
		&movd("mm3",&DWP(0,$r,"",0));	# mm3 = r[0]
		&paddq("mm1","mm3");		# mm1 = carry_in + r[0]
		&movd("mm2",&DWP(0,$a,"",0));	# mm2 = a[0]
		&pmuludq("mm2","mm0");		# mm2 = w*a[0]
		&movd("mm4",&DWP(4,$a,"",0));	# mm4 = a[1]
		&pmuludq("mm4","mm0");		# mm4 = w*a[1]
		&movd("mm6",&DWP(8,$a,"",0));	# mm6 = a[2]
		&pmuludq("mm6","mm0");		# mm6 = w*a[2]
		&movd("mm7",&DWP(12,$a,"",0));	# mm7 = a[3]
		&pmuludq("mm7","mm0");		# mm7 = w*a[3]
		&paddq("mm1","mm2");		# mm1 = carry_in + r[0] + w*a[0]
		&movd("mm3",&DWP(4,$r,"",0));	# mm3 = r[1]
		&paddq("mm3","mm4");		# mm3 = r[1] + w*a[1]
		&movd("mm5",&DWP(8,$r,"",0));	# mm5 = r[2]
		&paddq("mm5","mm6");		# mm5 = r[2] + w*a[2]
		&movd("mm4",&DWP(12,$r,"",0));	# mm4 = r[3]
		&paddq("mm7","mm4");		# mm7 = r[3] + w*a[3]
		&movd(&DWP(0,$r,"",0),"mm1");
		&movd("mm2",&DWP(16,$a,"",0));	# mm2 = a[4]
		&pmuludq("mm2","mm0");		# mm2 = w*a[4]
		&psrlq("mm1",32);		# mm1 = carry0
		&movd("mm4",&DWP(20,$a,"",0));	# mm4 = a[5]
		&pmuludq("mm4","mm0");		# mm4 = w*a[5]
		&paddq("mm1","mm3");		# mm1 = carry0 + r[1] + w*a[1]
		&movd("mm6",&DWP(24,$a,"",0));	# mm6 = a[6]
		&pmuludq("mm6","mm0");		# mm6 = w*a[6]
		&movd(&DWP(4,$r,"",0),"mm1");
		&psrlq("mm1",32);		# mm1 = carry1
		&movd("mm3",&DWP(28,$a,"",0));	# mm3 = a[7]
		&add($a,32);
		&pmuludq("mm3","mm0");		# mm3 = w*a[7]
		&paddq("mm1","mm5");		# mm1 = carry1 + r[2] + w*a[2]
		&movd("mm5",&DWP(16,$r,"",0));	# mm5 = r[4]
		&paddq("mm2","mm5");		# mm2 = r[4] + w*a[4]
		&movd(&DWP(8,$r,"",0),"mm1");
		&psrlq("mm1",32);		# mm1 = carry2
		&paddq("mm1","mm7");		# mm1 = carry2 + r[3] + w*a[3]
		&movd("mm5",&DWP(20,$r,"",0));	# mm5 = r[5]
		&paddq("mm4","mm5");		# mm4 = r[5] + w*a[5]
		&movd(&DWP(12,$r,"",0),"mm1");
		&psrlq("mm1",32);		# mm1 = carry3
		&paddq("mm1","mm2");		# mm1 = carry3 + r[4] + w*a[4]
		&movd("mm5",&DWP(24,$r,"",0));	# mm5 = r[6]
		&paddq("mm6","mm5");		# mm6 = r[6] + w*a[6]
		&movd(&DWP(16,$r,"",0),"mm1");
		&psrlq("mm1",32);		# mm1 = carry4
		&paddq("mm1","mm4");		# mm1 = carry4 + r[5] + w*a[5]
		&movd("mm5",&DWP(28,$r,"",0));	# mm5 = r[7]
		&paddq("mm3","mm5");		# mm3 = r[7] + w*a[7]
		&movd(&DWP(20,$r,"",0),"mm1");
		&psrlq("mm1",32);		# mm1 = carry5
		&paddq("mm1","mm6");		# mm1 = carry5 + r[6] + w*a[6]
		&movd(&DWP(24,$r,"",0),"mm1");
		&psrlq("mm1",32);		# mm1 = carry6
		&paddq("mm1","mm3");		# mm1 = carry6 + r[7] + w*a[7]
		&movd(&DWP(28,$r,"",0),"mm1");
		&lea($r,&DWP(32,$r));
		&psrlq("mm1",32);		# mm1 = carry_out

		&sub($c,8);
		&jz(&label("maw_sse2_exit"));
	&set_label("maw_sse2_entry");
		&test($c,0xfffffff8);
		&jnz(&label("maw_sse2_unrolled"));

	&set_label("maw_sse2_loop",4);
		&movd("mm2",&DWP(0,$a));	# mm2 = a[i]
		&movd("mm3",&DWP(0,$r));	# mm3 = r[i]
		&pmuludq("mm2","mm0");		# a[i] *= w
		&lea($a,&DWP(4,$a));
		&paddq("mm1","mm3");		# carry += r[i]
		&paddq("mm1","mm2");		# carry += a[i]*w
		&movd(&DWP(0,$r),"mm1");	# r[i] = carry_low
		&sub($c,1);
		&psrlq("mm1",32);		# carry = carry_high
		&lea($r,&DWP(4,$r));
		&jnz(&label("maw_sse2_loop"));
	&set_label("maw_sse2_exit");
		&movd("eax","mm1");		# c = carry_out
		&emms();
		&ret();

	&set_label("maw_non_sse2",16);
	}

	# function_begin prologue
	&push("ebp");
	&push("ebx");
	&push("esi");
	&push("edi");

	&comment("");
	$Low="eax";
	$High="edx";
	$a="ebx";
	$w="ebp";
	$r="edi";
	$c="esi";

	&xor($c,$c);		# clear carry
	&mov($r,&wparam(0));	#

	&mov("ecx",&wparam(2));	#
	&mov($a,&wparam(1));	#

	&and("ecx",0xfffffff8);	# num / 8
	&mov($w,&wparam(3));	#

	&push("ecx");		# Up the stack for a tmp variable

	&jz(&label("maw_finish"));

	&set_label("maw_loop",16);

	for ($i=0; $i<32; $i+=4)
		{
		&comment("Round $i");

		 &mov("eax",&DWP($i,$a)); 	# *a
		&mul($w);			# *a * w
		&add("eax",$c);			# L(t)+= c
		&adc("edx",0);			# H(t)+=carry
		 &add("eax",&DWP($i,$r));	# L(t)+= *r
		&adc("edx",0);			# H(t)+=carry
		 &mov(&DWP($i,$r),"eax");	# *r= L(t);
		&mov($c,"edx");			# c=  H(t);
		}

	&comment("");
	&sub("ecx",8);
	&lea($a,&DWP(32,$a));
	&lea($r,&DWP(32,$r));
	&jnz(&label("maw_loop"));

	&set_label("maw_finish",0);
	&mov("ecx",&wparam(2));	# get num
	&and("ecx",7);
	&jnz(&label("maw_finish2"));	# helps branch prediction
	&jmp(&label("maw_end"));

	&set_label("maw_finish2",1);
	for ($i=0; $i<7; $i++)
		{
		&comment("Tail Round $i");
		 &mov("eax",&DWP($i*4,$a));	# *a
		&mul($w);			# *a * w
		&add("eax",$c);			# L(t)+=c
		&adc("edx",0);			# H(t)+=carry
		 &add("eax",&DWP($i*4,$r));	# L(t)+= *r
		&adc("edx",0);			# H(t)+=carry
		 &dec("ecx") if ($i != 7-1);
		&mov(&DWP($i*4,$r),"eax");	# *r= L(t);
		 &mov($c,"edx");		# c=  H(t);
		&jz(&label("maw_end")) if ($i != 7-1);
		}
	&set_label("maw_end",0);
	&mov("eax",$c);

	&pop("ecx");	# clear variable from

	&function_end($name);
	}

sub bn_mul_words
	{
	local($name)=@_;

	&function_begin_B($name,$sse2?"EXTRN\t_OPENSSL_ia32cap_P:DWORD":"");

	$r="eax";
	$a="edx";
	$c="ecx";

	if ($sse2) {
		&picmeup("eax","OPENSSL_ia32cap_P");
		&bt(&DWP(0,"eax"),26);
		&jnc(&label("mw_non_sse2"));

		&mov($r,&wparam(0));
		&mov($a,&wparam(1));
		&mov($c,&wparam(2));
		&movd("mm0",&wparam(3));	# mm0 = w
		&pxor("mm1","mm1");		# mm1 = carry = 0

	&set_label("mw_sse2_loop",16);
		&movd("mm2",&DWP(0,$a));	# mm2 = a[i]
		&pmuludq("mm2","mm0");		# a[i] *= w
		&lea($a,&DWP(4,$a));
		&paddq("mm1","mm2");		# carry += a[i]*w
		&movd(&DWP(0,$r),"mm1");	# r[i] = carry_low
		&sub($c,1);
		&psrlq("mm1",32);		# carry = carry_high
		&lea($r,&DWP(4,$r));
		&jnz(&label("mw_sse2_loop"));

		&movd("eax","mm1");		# return carry
		&emms();
		&ret();
	&set_label("mw_non_sse2",16);
	}

	# function_begin prologue
	&push("ebp");
	&push("ebx");
	&push("esi");
	&push("edi");

	&comment("");
	$Low="eax";
	$High="edx";
	$a="ebx";
	$w="ecx";
	$r="edi";
	$c="esi";
	$num="ebp";

	&xor($c,$c);		# clear carry
	&mov($r,&wparam(0));	#
	&mov($a,&wparam(1));	#
	&mov($num,&wparam(2));	#
	&mov($w,&wparam(3));	#

	&and($num,0xfffffff8);	# num / 8
	&jz(&label("mw_finish"));

	&set_label("mw_loop",0);
	for ($i=0; $i<32; $i+=4)
		{
		&comment("Round $i");

		 &mov("eax",&DWP($i,$a,"",0)); 	# *a
		&mul($w);			# *a * w
		&add("eax",$c);			# L(t)+=c
		 # XXX

		&adc("edx",0);			# H(t)+=carry
		 &mov(&DWP($i,$r,"",0),"eax");	# *r= L(t);

		&mov($c,"edx");			# c=  H(t);
		}

	&comment("");
	&add($a,32);
	&add($r,32);
	&sub($num,8);
	&jz(&label("mw_finish"));
	&jmp(&label("mw_loop"));

	&set_label("mw_finish",0);
	&mov($num,&wparam(2));	# get num
	&and($num,7);
	&jnz(&label("mw_finish2"));
	&jmp(&label("mw_end"));

	&set_label("mw_finish2",1);
	for ($i=0; $i<7; $i++)
		{
		&comment("Tail Round $i");
		 &mov("eax",&DWP($i*4,$a,"",0));# *a
		&mul($w);			# *a * w
		&add("eax",$c);			# L(t)+=c
		 # XXX
		&adc("edx",0);			# H(t)+=carry
		 &mov(&DWP($i*4,$r,"",0),"eax");# *r= L(t);
		&mov($c,"edx");			# c=  H(t);
		 &dec($num) if ($i != 7-1);
		&jz(&label("mw_end")) if ($i != 7-1);
		}
	&set_label("mw_end",0);
	&mov("eax",$c);

	&function_end($name);
	}

sub bn_sqr_words
	{
	local($name)=@_;

	&function_begin_B($name,$sse2?"EXTRN\t_OPENSSL_ia32cap_P:DWORD":"");

	$r="eax";
	$a="edx";
	$c="ecx";

	if ($sse2) {
		&picmeup("eax","OPENSSL_ia32cap_P");
		&bt(&DWP(0,"eax"),26);
		&jnc(&label("sqr_non_sse2"));

		&mov($r,&wparam(0));
		&mov($a,&wparam(1));
		&mov($c,&wparam(2));

	&set_label("sqr_sse2_loop",16);
		&movd("mm0",&DWP(0,$a));	# mm0 = a[i]
		&pmuludq("mm0","mm0");		# a[i] *= a[i]
		&lea($a,&DWP(4,$a));		# a++
		&movq(&QWP(0,$r),"mm0");	# r[i] = a[i]*a[i]
		&sub($c,1);
		&lea($r,&DWP(8,$r));		# r += 2
		&jnz(&label("sqr_sse2_loop"));

		&emms();
		&ret();
	&set_label("sqr_non_sse2",16);
	}

	# function_begin prologue
	&push("ebp");
	&push("ebx");
	&push("esi");
	&push("edi");

	&comment("");
	$r="esi";
	$a="edi";
	$num="ebx";

	&mov($r,&wparam(0));	#
	&mov($a,&wparam(1));	#
	&mov($num,&wparam(2));	#

	&and($num,0xfffffff8);	# num / 8
	&jz(&label("sw_finish"));

	&set_label("sw_loop",0);
	for ($i=0; $i<32; $i+=4)
		{
		&comment("Round $i");
		&mov("eax",&DWP($i,$a,"",0)); 	# *a
		 # XXX
		&mul("eax");			# *a * *a
		&mov(&DWP($i*2,$r,"",0),"eax");	#
		 &mov(&DWP($i*2+4,$r,"",0),"edx");#
		}

	&comment("");
	&add($a,32);
	&add($r,64);
	&sub($num,8);
	&jnz(&label("sw_loop"));

	&set_label("sw_finish",0);
	&mov($num,&wparam(2));	# get num
	&and($num,7);
	&jz(&label("sw_end"));

	for ($i=0; $i<7; $i++)
		{
		&comment("Tail Round $i");
		&mov("eax",&DWP($i*4,$a,"",0));	# *a
		 # XXX
		&mul("eax");			# *a * *a
		&mov(&DWP($i*8,$r,"",0),"eax");	#
		 &dec($num) if ($i != 7-1);
		&mov(&DWP($i*8+4,$r,"",0),"edx");
		 &jz(&label("sw_end")) if ($i != 7-1);
		}
	&set_label("sw_end",0);

	&function_end($name);
	}

sub bn_div_words
	{
	local($name)=@_;

	&function_begin_B($name,"");
	&mov("edx",&wparam(0));	#
	&mov("eax",&wparam(1));	#
	&mov("ecx",&wparam(2));	#
	&div("ecx");
	&ret();
	&function_end_B($name);
	}

sub bn_add_words
	{
	local($name)=@_;

	&function_begin($name,"");

	&comment("");
	$a="esi";
	$b="edi";
	$c="eax";
	$r="ebx";
	$tmp1="ecx";
	$tmp2="edx";
	$num="ebp";

	&mov($r,&wparam(0));	# get r
	 &mov($a,&wparam(1));	# get a
	&mov($b,&wparam(2));	# get b
	 &mov($num,&wparam(3));	# get num
	&xor($c,$c);		# clear carry
	 &and($num,0xfffffff8);	# num / 8

	&jz(&label("aw_finish"));

	&set_label("aw_loop",0);
	for ($i=0; $i<8; $i++)
		{
		&comment("Round $i");

		&mov($tmp1,&DWP($i*4,$a,"",0)); 	# *a
		 &mov($tmp2,&DWP($i*4,$b,"",0)); 	# *b
		&add($tmp1,$c);
		 &mov($c,0);
		&adc($c,$c);
		 &add($tmp1,$tmp2);
		&adc($c,0);
		 &mov(&DWP($i*4,$r,"",0),$tmp1); 	# *r
		}

	&comment("");
	&add($a,32);
	 &add($b,32);
	&add($r,32);
	 &sub($num,8);
	&jnz(&label("aw_loop"));

	&set_label("aw_finish",0);
	&mov($num,&wparam(3));	# get num
	&and($num,7);
	 &jz(&label("aw_end"));

	for ($i=0; $i<7; $i++)
		{
		&comment("Tail Round $i");
		&mov($tmp1,&DWP($i*4,$a,"",0));	# *a
		 &mov($tmp2,&DWP($i*4,$b,"",0));# *b
		&add($tmp1,$c);
		 &mov($c,0);
		&adc($c,$c);
		 &add($tmp1,$tmp2);
		&adc($c,0);
		 &dec($num) if ($i != 6);
		&mov(&DWP($i*4,$r,"",0),$tmp1);	# *r
		 &jz(&label("aw_end")) if ($i != 6);
		}
	&set_label("aw_end",0);

#	&mov("eax",$c);		# $c is "eax"

	&function_end($name);
	}

sub bn_sub_words
	{
	local($name)=@_;

	&function_begin($name,"");

	&comment("");
	$a="esi";
	$b="edi";
	$c="eax";
	$r="ebx";
	$tmp1="ecx";
	$tmp2="edx";
	$num="ebp";

	&mov($r,&wparam(0));	# get r
	 &mov($a,&wparam(1));	# get a
	&mov($b,&wparam(2));	# get b
	 &mov($num,&wparam(3));	# get num
	&xor($c,$c);		# clear carry
	 &and($num,0xfffffff8);	# num / 8

	&jz(&label("aw_finish"));

	&set_label("aw_loop",0);
	for ($i=0; $i<8; $i++)
		{
		&comment("Round $i");

		&mov($tmp1,&DWP($i*4,$a,"",0)); 	# *a
		 &mov($tmp2,&DWP($i*4,$b,"",0)); 	# *b
		&sub($tmp1,$c);
		 &mov($c,0);
		&adc($c,$c);
		 &sub($tmp1,$tmp2);
		&adc($c,0);
		 &mov(&DWP($i*4,$r,"",0),$tmp1); 	# *r
		}

	&comment("");
	&add($a,32);
	 &add($b,32);
	&add($r,32);
	 &sub($num,8);
	&jnz(&label("aw_loop"));

	&set_label("aw_finish",0);
	&mov($num,&wparam(3));	# get num
	&and($num,7);
	 &jz(&label("aw_end"));

	for ($i=0; $i<7; $i++)
		{
		&comment("Tail Round $i");
		&mov($tmp1,&DWP($i*4,$a,"",0));	# *a
		 &mov($tmp2,&DWP($i*4,$b,"",0));# *b
		&sub($tmp1,$c);
		 &mov($c,0);
		&adc($c,$c);
		 &sub($tmp1,$tmp2);
		&adc($c,0);
		 &dec($num) if ($i != 6);
		&mov(&DWP($i*4,$r,"",0),$tmp1);	# *r
		 &jz(&label("aw_end")) if ($i != 6);
		}
	&set_label("aw_end",0);

#	&mov("eax",$c);		# $c is "eax"

	&function_end($name);
	}

sub bn_sub_part_words
	{
	local($name)=@_;

	&function_begin($name,"");

	&comment("");
	$a="esi";
	$b="edi";
	$c="eax";
	$r="ebx";
	$tmp1="ecx";
	$tmp2="edx";
	$num="ebp";

	&mov($r,&wparam(0));	# get r
	 &mov($a,&wparam(1));	# get a
	&mov($b,&wparam(2));	# get b
	 &mov($num,&wparam(3));	# get num
	&xor($c,$c);		# clear carry
	 &and($num,0xfffffff8);	# num / 8

	&jz(&label("aw_finish"));

	&set_label("aw_loop",0);
	for ($i=0; $i<8; $i++)
		{
		&comment("Round $i");

		&mov($tmp1,&DWP($i*4,$a,"",0)); 	# *a
		 &mov($tmp2,&DWP($i*4,$b,"",0)); 	# *b
		&sub($tmp1,$c);
		 &mov($c,0);
		&adc($c,$c);
		 &sub($tmp1,$tmp2);
		&adc($c,0);
		 &mov(&DWP($i*4,$r,"",0),$tmp1); 	# *r
		}

	&comment("");
	&add($a,32);
	 &add($b,32);
	&add($r,32);
	 &sub($num,8);
	&jnz(&label("aw_loop"));

	&set_label("aw_finish",0);
	&mov($num,&wparam(3));	# get num
	&and($num,7);
	 &jz(&label("aw_end"));

	for ($i=0; $i<7; $i++)
		{
		&comment("Tail Round $i");
		&mov($tmp1,&DWP(0,$a,"",0));	# *a
		 &mov($tmp2,&DWP(0,$b,"",0));# *b
		&sub($tmp1,$c);
		 &mov($c,0);
		&adc($c,$c);
		 &sub($tmp1,$tmp2);
		&adc($c,0);
		&mov(&DWP(0,$r,"",0),$tmp1);	# *r
		&add($a, 4);
		&add($b, 4);
		&add($r, 4);
		 &dec($num) if ($i != 6);
		 &jz(&label("aw_end")) if ($i != 6);
		}
	&set_label("aw_end",0);

	&cmp(&wparam(4),0);
	&je(&label("pw_end"));

	&mov($num,&wparam(4));	# get dl
	&cmp($num,0);
	&je(&label("pw_end"));
	&jge(&label("pw_pos"));

	&comment("pw_neg");
	&mov($tmp2,0);
	&sub($tmp2,$num);
	&mov($num,$tmp2);
	&and($num,0xfffffff8);	# num / 8
	&jz(&label("pw_neg_finish"));

	&set_label("pw_neg_loop",0);
	for ($i=0; $i<8; $i++)
	{
	    &comment("dl<0 Round $i");

	    &mov($tmp1,0);
	    &mov($tmp2,&DWP($i*4,$b,"",0)); 	# *b
	    &sub($tmp1,$c);
	    &mov($c,0);
	    &adc($c,$c);
	    &sub($tmp1,$tmp2);
	    &adc($c,0);
	    &mov(&DWP($i*4,$r,"",0),$tmp1); 	# *r
	}

	&comment("");
	&add($b,32);
	&add($r,32);
	&sub($num,8);
	&jnz(&label("pw_neg_loop"));

	&set_label("pw_neg_finish",0);
	&mov($tmp2,&wparam(4));	# get dl
	&mov($num,0);
	&sub($num,$tmp2);
	&and($num,7);
	&jz(&label("pw_end"));

	for ($i=0; $i<7; $i++)
	{
	    &comment("dl<0 Tail Round $i");
	    &mov($tmp1,0);
	    &mov($tmp2,&DWP($i*4,$b,"",0));# *b
	    &sub($tmp1,$c);
	    &mov($c,0);
	    &adc($c,$c);
	    &sub($tmp1,$tmp2);
	    &adc($c,0);
	    &dec($num) if ($i != 6);
	    &mov(&DWP($i*4,$r,"",0),$tmp1);	# *r
	    &jz(&label("pw_end")) if ($i != 6);
	}

	&jmp(&label("pw_end"));

	&set_label("pw_pos",0);

	&and($num,0xfffffff8);	# num / 8
	&jz(&label("pw_pos_finish"));

	&set_label("pw_pos_loop",0);

	for ($i=0; $i<8; $i++)
	{
	    &comment("dl>0 Round $i");

	    &mov($tmp1,&DWP($i*4,$a,"",0));	# *a
	    &sub($tmp1,$c);
	    &mov(&DWP($i*4,$r,"",0),$tmp1);	# *r
	    &jnc(&label("pw_nc".$i));
	}

	&comment("");
	&add($a,32);
	&add($r,32);
	&sub($num,8);
	&jnz(&label("pw_pos_loop"));

	&set_label("pw_pos_finish",0);
	&mov($num,&wparam(4));	# get dl
	&and($num,7);
	&jz(&label("pw_end"));

	for ($i=0; $i<7; $i++)
	{
	    &comment("dl>0 Tail Round $i");
	    &mov($tmp1,&DWP($i*4,$a,"",0));	# *a
	    &sub($tmp1,$c);
	    &mov(&DWP($i*4,$r,"",0),$tmp1);	# *r
	    &jnc(&label("pw_tail_nc".$i));
	    &dec($num) if ($i != 6);
	    &jz(&label("pw_end")) if ($i != 6);
	}
	&mov($c,1);
	&jmp(&label("pw_end"));

	&set_label("pw_nc_loop",0);
	for ($i=0; $i<8; $i++)
	{
	    &mov($tmp1,&DWP($i*4,$a,"",0));	# *a
	    &mov(&DWP($i*4,$r,"",0),$tmp1);	# *r
	    &set_label("pw_nc".$i,0);
	}

	&comment("");
	&add($a,32);
	&add($r,32);
	&sub($num,8);
	&jnz(&label("pw_nc_loop"));

	&mov($num,&wparam(4));	# get dl
	&and($num,7);
	&jz(&label("pw_nc_end"));

	for ($i=0; $i<7; $i++)
	{
	    &mov($tmp1,&DWP($i*4,$a,"",0));	# *a
	    &mov(&DWP($i*4,$r,"",0),$tmp1);	# *r
	    &set_label("pw_tail_nc".$i,0);
	    &dec($num) if ($i != 6);
	    &jz(&label("pw_nc_end")) if ($i != 6);
	}

	&set_label("pw_nc_end",0);
	&mov($c,0);

	&set_label("pw_end",0);

#	&mov("eax",$c);		# $c is "eax"

	&function_end($name);
	}

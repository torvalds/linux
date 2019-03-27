#! /usr/bin/env perl
# Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# void des_ncbc_encrypt(input, output, length, schedule, ivec, enc)
# des_cblock (*input);
# des_cblock (*output);
# long length;
# des_key_schedule schedule;
# des_cblock (*ivec);
# int enc;
#
# calls
# des_encrypt((DES_LONG *)tin,schedule,DES_ENCRYPT);
#

#&cbc("des_ncbc_encrypt","des_encrypt",0);
#&cbc("BF_cbc_encrypt","BF_encrypt","BF_encrypt",
#	1,4,5,3,5,-1);
#&cbc("des_ncbc_encrypt","des_encrypt","des_encrypt",
#	0,4,5,3,5,-1);
#&cbc("des_ede3_cbc_encrypt","des_encrypt3","des_decrypt3",
#	0,6,7,3,4,5);
#
# When doing a cipher that needs bigendian order,
# for encrypt, the iv is kept in bigendian form,
# while for decrypt, it is kept in little endian.
sub cbc
	{
	local($name,$enc_func,$dec_func,$swap,$iv_off,$enc_off,$p1,$p2,$p3)=@_;
	# name is the function name
	# enc_func and dec_func and the functions to call for encrypt/decrypt
	# swap is true if byte order needs to be reversed
	# iv_off is parameter number for the iv
	# enc_off is parameter number for the encrypt/decrypt flag
	# p1,p2,p3 are the offsets for parameters to be passed to the
	# underlying calls.

	&function_begin_B($name,"");
	&comment("");

	$in="esi";
	$out="edi";
	$count="ebp";

	&push("ebp");
	&push("ebx");
	&push("esi");
	&push("edi");

	$data_off=4;
	$data_off+=4 if ($p1 > 0);
	$data_off+=4 if ($p2 > 0);
	$data_off+=4 if ($p3 > 0);

	&mov($count,	&wparam(2));	# length

	&comment("getting iv ptr from parameter $iv_off");
	&mov("ebx",	&wparam($iv_off));	# Get iv ptr

	&mov($in,	&DWP(0,"ebx","",0));#	iv[0]
	&mov($out,	&DWP(4,"ebx","",0));#	iv[1]

	&push($out);
	&push($in);
	&push($out);	# used in decrypt for iv[1]
	&push($in);	# used in decrypt for iv[0]

	&mov("ebx",	"esp");		# This is the address of tin[2]

	&mov($in,	&wparam(0));	# in
	&mov($out,	&wparam(1));	# out

	# We have loaded them all, how lets push things
	&comment("getting encrypt flag from parameter $enc_off");
	&mov("ecx",	&wparam($enc_off));	# Get enc flag
	if ($p3 > 0)
		{
		&comment("get and push parameter $p3");
		if ($enc_off != $p3)
			{ &mov("eax",	&wparam($p3)); &push("eax"); }
		else	{ &push("ecx"); }
		}
	if ($p2 > 0)
		{
		&comment("get and push parameter $p2");
		if ($enc_off != $p2)
			{ &mov("eax",	&wparam($p2)); &push("eax"); }
		else	{ &push("ecx"); }
		}
	if ($p1 > 0)
		{
		&comment("get and push parameter $p1");
		if ($enc_off != $p1)
			{ &mov("eax",	&wparam($p1)); &push("eax"); }
		else	{ &push("ecx"); }
		}
	&push("ebx");		# push data/iv

	&cmp("ecx",0);
	&jz(&label("decrypt"));

	&and($count,0xfffffff8);
	&mov("eax",	&DWP($data_off,"esp","",0));	# load iv[0]
	&mov("ebx",	&DWP($data_off+4,"esp","",0));	# load iv[1]

	&jz(&label("encrypt_finish"));

	#############################################################

	&set_label("encrypt_loop");
	# encrypt start
	# "eax" and "ebx" hold iv (or the last cipher text)

	&mov("ecx",	&DWP(0,$in,"",0));	# load first 4 bytes
	&mov("edx",	&DWP(4,$in,"",0));	# second 4 bytes

	&xor("eax",	"ecx");
	&xor("ebx",	"edx");

	&bswap("eax")	if $swap;
	&bswap("ebx")	if $swap;

	&mov(&DWP($data_off,"esp","",0),	"eax");	# put in array for call
	&mov(&DWP($data_off+4,"esp","",0),	"ebx");	#

	&call($enc_func);

	&mov("eax",	&DWP($data_off,"esp","",0));
	&mov("ebx",	&DWP($data_off+4,"esp","",0));

	&bswap("eax")	if $swap;
	&bswap("ebx")	if $swap;

	&mov(&DWP(0,$out,"",0),"eax");
	&mov(&DWP(4,$out,"",0),"ebx");

	# eax and ebx are the next iv.

	&add($in,	8);
	&add($out,	8);

	&sub($count,	8);
	&jnz(&label("encrypt_loop"));

###################################################################3
	&set_label("encrypt_finish");
	&mov($count,	&wparam(2));	# length
	&and($count,	7);
	&jz(&label("finish"));
	&call(&label("PIC_point"));
&set_label("PIC_point");
	&blindpop("edx");
	&lea("ecx",&DWP(&label("cbc_enc_jmp_table")."-".&label("PIC_point"),"edx"));
	&mov($count,&DWP(0,"ecx",$count,4));
	&add($count,"edx");
	&xor("ecx","ecx");
	&xor("edx","edx");
	#&mov($count,&DWP(&label("cbc_enc_jmp_table"),"",$count,4));
	&jmp_ptr($count);

&set_label("ej7");
	&movb(&HB("edx"),	&BP(6,$in,"",0));
	&shl("edx",8);
&set_label("ej6");
	&movb(&HB("edx"),	&BP(5,$in,"",0));
&set_label("ej5");
	&movb(&LB("edx"),	&BP(4,$in,"",0));
&set_label("ej4");
	&mov("ecx",		&DWP(0,$in,"",0));
	&jmp(&label("ejend"));
&set_label("ej3");
	&movb(&HB("ecx"),	&BP(2,$in,"",0));
	&shl("ecx",8);
&set_label("ej2");
	&movb(&HB("ecx"),	&BP(1,$in,"",0));
&set_label("ej1");
	&movb(&LB("ecx"),	&BP(0,$in,"",0));
&set_label("ejend");

	&xor("eax",	"ecx");
	&xor("ebx",	"edx");

	&bswap("eax")	if $swap;
	&bswap("ebx")	if $swap;

	&mov(&DWP($data_off,"esp","",0),	"eax");	# put in array for call
	&mov(&DWP($data_off+4,"esp","",0),	"ebx");	#

	&call($enc_func);

	&mov("eax",	&DWP($data_off,"esp","",0));
	&mov("ebx",	&DWP($data_off+4,"esp","",0));

	&bswap("eax")	if $swap;
	&bswap("ebx")	if $swap;

	&mov(&DWP(0,$out,"",0),"eax");
	&mov(&DWP(4,$out,"",0),"ebx");

	&jmp(&label("finish"));

	#############################################################
	#############################################################
	&set_label("decrypt",1);
	# decrypt start
	&and($count,0xfffffff8);
	# The next 2 instructions are only for if the jz is taken
	&mov("eax",	&DWP($data_off+8,"esp","",0));	# get iv[0]
	&mov("ebx",	&DWP($data_off+12,"esp","",0));	# get iv[1]
	&jz(&label("decrypt_finish"));

	&set_label("decrypt_loop");
	&mov("eax",	&DWP(0,$in,"",0));	# load first 4 bytes
	&mov("ebx",	&DWP(4,$in,"",0));	# second 4 bytes

	&bswap("eax")	if $swap;
	&bswap("ebx")	if $swap;

	&mov(&DWP($data_off,"esp","",0),	"eax");	# put back
	&mov(&DWP($data_off+4,"esp","",0),	"ebx");	#

	&call($dec_func);

	&mov("eax",	&DWP($data_off,"esp","",0));	# get return
	&mov("ebx",	&DWP($data_off+4,"esp","",0));	#

	&bswap("eax")	if $swap;
	&bswap("ebx")	if $swap;

	&mov("ecx",	&DWP($data_off+8,"esp","",0));	# get iv[0]
	&mov("edx",	&DWP($data_off+12,"esp","",0));	# get iv[1]

	&xor("ecx",	"eax");
	&xor("edx",	"ebx");

	&mov("eax",	&DWP(0,$in,"",0));	# get old cipher text,
	&mov("ebx",	&DWP(4,$in,"",0));	# next iv actually

	&mov(&DWP(0,$out,"",0),"ecx");
	&mov(&DWP(4,$out,"",0),"edx");

	&mov(&DWP($data_off+8,"esp","",0),	"eax");	# save iv
	&mov(&DWP($data_off+12,"esp","",0),	"ebx");	#

	&add($in,	8);
	&add($out,	8);

	&sub($count,	8);
	&jnz(&label("decrypt_loop"));
############################ ENDIT #######################3
	&set_label("decrypt_finish");
	&mov($count,	&wparam(2));	# length
	&and($count,	7);
	&jz(&label("finish"));

	&mov("eax",	&DWP(0,$in,"",0));	# load first 4 bytes
	&mov("ebx",	&DWP(4,$in,"",0));	# second 4 bytes

	&bswap("eax")	if $swap;
	&bswap("ebx")	if $swap;

	&mov(&DWP($data_off,"esp","",0),	"eax");	# put back
	&mov(&DWP($data_off+4,"esp","",0),	"ebx");	#

	&call($dec_func);

	&mov("eax",	&DWP($data_off,"esp","",0));	# get return
	&mov("ebx",	&DWP($data_off+4,"esp","",0));	#

	&bswap("eax")	if $swap;
	&bswap("ebx")	if $swap;

	&mov("ecx",	&DWP($data_off+8,"esp","",0));	# get iv[0]
	&mov("edx",	&DWP($data_off+12,"esp","",0));	# get iv[1]

	&xor("ecx",	"eax");
	&xor("edx",	"ebx");

	# this is for when we exit
	&mov("eax",	&DWP(0,$in,"",0));	# get old cipher text,
	&mov("ebx",	&DWP(4,$in,"",0));	# next iv actually

&set_label("dj7");
	&rotr("edx",	16);
	&movb(&BP(6,$out,"",0),	&LB("edx"));
	&shr("edx",16);
&set_label("dj6");
	&movb(&BP(5,$out,"",0),	&HB("edx"));
&set_label("dj5");
	&movb(&BP(4,$out,"",0),	&LB("edx"));
&set_label("dj4");
	&mov(&DWP(0,$out,"",0),	"ecx");
	&jmp(&label("djend"));
&set_label("dj3");
	&rotr("ecx",	16);
	&movb(&BP(2,$out,"",0),	&LB("ecx"));
	&shl("ecx",16);
&set_label("dj2");
	&movb(&BP(1,$in,"",0),	&HB("ecx"));
&set_label("dj1");
	&movb(&BP(0,$in,"",0),	&LB("ecx"));
&set_label("djend");

	# final iv is still in eax:ebx
	&jmp(&label("finish"));


############################ FINISH #######################3
	&set_label("finish",1);
	&mov("ecx",	&wparam($iv_off));	# Get iv ptr

	#################################################
	$total=16+4;
	$total+=4 if ($p1 > 0);
	$total+=4 if ($p2 > 0);
	$total+=4 if ($p3 > 0);
	&add("esp",$total);

	&mov(&DWP(0,"ecx","",0),	"eax");	# save iv
	&mov(&DWP(4,"ecx","",0),	"ebx");	# save iv

	&function_end_A($name);

	&align(64);
	&set_label("cbc_enc_jmp_table");
	&data_word("0");
	&data_word(&label("ej1")."-".&label("PIC_point"));
	&data_word(&label("ej2")."-".&label("PIC_point"));
	&data_word(&label("ej3")."-".&label("PIC_point"));
	&data_word(&label("ej4")."-".&label("PIC_point"));
	&data_word(&label("ej5")."-".&label("PIC_point"));
	&data_word(&label("ej6")."-".&label("PIC_point"));
	&data_word(&label("ej7")."-".&label("PIC_point"));
	# not used
	#&set_label("cbc_dec_jmp_table",1);
	#&data_word("0");
	#&data_word(&label("dj1")."-".&label("PIC_point"));
	#&data_word(&label("dj2")."-".&label("PIC_point"));
	#&data_word(&label("dj3")."-".&label("PIC_point"));
	#&data_word(&label("dj4")."-".&label("PIC_point"));
	#&data_word(&label("dj5")."-".&label("PIC_point"));
	#&data_word(&label("dj6")."-".&label("PIC_point"));
	#&data_word(&label("dj7")."-".&label("PIC_point"));
	&align(64);

	&function_end_B($name);

	}

1;

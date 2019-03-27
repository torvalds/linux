#! /usr/bin/env perl
# Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


$L="edi";
$R="esi";

sub DES_encrypt3
	{
	local($name,$enc)=@_;

	&function_begin_B($name,"");
	&push("ebx");
	&mov("ebx",&wparam(0));

	&push("ebp");
	&push("esi");

	&push("edi");

	&comment("");
	&comment("Load the data words");
	&mov($L,&DWP(0,"ebx","",0));
	&mov($R,&DWP(4,"ebx","",0));
	&stack_push(3);

	&comment("");
	&comment("IP");
	&IP_new($L,$R,"edx",0);

	# put them back

	if ($enc)
		{
		&mov(&DWP(4,"ebx","",0),$R);
		 &mov("eax",&wparam(1));
		&mov(&DWP(0,"ebx","",0),"edx");
		 &mov("edi",&wparam(2));
		 &mov("esi",&wparam(3));
		}
	else
		{
		&mov(&DWP(4,"ebx","",0),$R);
		 &mov("esi",&wparam(1));
		&mov(&DWP(0,"ebx","",0),"edx");
		 &mov("edi",&wparam(2));
		 &mov("eax",&wparam(3));
		}
	&mov(&swtmp(2),	(DWC(($enc)?"1":"0")));
	&mov(&swtmp(1),	"eax");
	&mov(&swtmp(0),	"ebx");
	&call("DES_encrypt2");
	&mov(&swtmp(2),	(DWC(($enc)?"0":"1")));
	&mov(&swtmp(1),	"edi");
	&mov(&swtmp(0),	"ebx");
	&call("DES_encrypt2");
	&mov(&swtmp(2),	(DWC(($enc)?"1":"0")));
	&mov(&swtmp(1),	"esi");
	&mov(&swtmp(0),	"ebx");
	&call("DES_encrypt2");

	&stack_pop(3);
	&mov($L,&DWP(0,"ebx","",0));
	&mov($R,&DWP(4,"ebx","",0));

	&comment("");
	&comment("FP");
	&FP_new($L,$R,"eax",0);

	&mov(&DWP(0,"ebx","",0),"eax");
	&mov(&DWP(4,"ebx","",0),$R);

	&pop("edi");
	&pop("esi");
	&pop("ebp");
	&pop("ebx");
	&ret();
	&function_end_B($name);
	}



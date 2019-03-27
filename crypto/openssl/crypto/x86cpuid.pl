#! /usr/bin/env perl
# Copyright 2004-2018 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC, "${dir}perlasm", "perlasm");
require "x86asm.pl";

$output = pop;
open OUT,">$output";
*STDOUT=*OUT;

&asm_init($ARGV[0]);

for (@ARGV) { $sse2=1 if (/-DOPENSSL_IA32_SSE2/); }

&function_begin("OPENSSL_ia32_cpuid");
	&xor	("edx","edx");
	&pushf	();
	&pop	("eax");
	&mov	("ecx","eax");
	&xor	("eax",1<<21);
	&push	("eax");
	&popf	();
	&pushf	();
	&pop	("eax");
	&xor	("ecx","eax");
	&xor	("eax","eax");
	&mov	("esi",&wparam(0));
	&mov	(&DWP(8,"esi"),"eax");	# clear extended feature flags
	&bt	("ecx",21);
	&jnc	(&label("nocpuid"));
	&cpuid	();
	&mov	("edi","eax");		# max value for standard query level

	&xor	("eax","eax");
	&cmp	("ebx",0x756e6547);	# "Genu"
	&setne	(&LB("eax"));
	&mov	("ebp","eax");
	&cmp	("edx",0x49656e69);	# "ineI"
	&setne	(&LB("eax"));
	&or	("ebp","eax");
	&cmp	("ecx",0x6c65746e);	# "ntel"
	&setne	(&LB("eax"));
	&or	("ebp","eax");		# 0 indicates Intel CPU
	&jz	(&label("intel"));

	&cmp	("ebx",0x68747541);	# "Auth"
	&setne	(&LB("eax"));
	&mov	("esi","eax");
	&cmp	("edx",0x69746E65);	# "enti"
	&setne	(&LB("eax"));
	&or	("esi","eax");
	&cmp	("ecx",0x444D4163);	# "cAMD"
	&setne	(&LB("eax"));
	&or	("esi","eax");		# 0 indicates AMD CPU
	&jnz	(&label("intel"));

	# AMD specific
	&mov	("eax",0x80000000);
	&cpuid	();
	&cmp	("eax",0x80000001);
	&jb	(&label("intel"));
	&mov	("esi","eax");
	&mov	("eax",0x80000001);
	&cpuid	();
	&or	("ebp","ecx");
	&and	("ebp",1<<11|1);	# isolate XOP bit
	&cmp	("esi",0x80000008);
	&jb	(&label("intel"));

	&mov	("eax",0x80000008);
	&cpuid	();
	&movz	("esi",&LB("ecx"));	# number of cores - 1
	&inc	("esi");		# number of cores

	&mov	("eax",1);
	&xor	("ecx","ecx");
	&cpuid	();
	&bt	("edx",28);
	&jnc	(&label("generic"));
	&shr	("ebx",16);
	&and	("ebx",0xff);
	&cmp	("ebx","esi");
	&ja	(&label("generic"));
	&and	("edx",0xefffffff);	# clear hyper-threading bit
	&jmp	(&label("generic"));

&set_label("intel");
	&cmp	("edi",4);
	&mov	("esi",-1);
	&jb	(&label("nocacheinfo"));

	&mov	("eax",4);
	&mov	("ecx",0);		# query L1D
	&cpuid	();
	&mov	("esi","eax");
	&shr	("esi",14);
	&and	("esi",0xfff);		# number of cores -1 per L1D

&set_label("nocacheinfo");
	&mov	("eax",1);
	&xor	("ecx","ecx");
	&cpuid	();
	&and	("edx",0xbfefffff);	# force reserved bits #20, #30 to 0
	&cmp	("ebp",0);
	&jne	(&label("notintel"));
	&or	("edx",1<<30);		# set reserved bit#30 on Intel CPUs
	&and	(&HB("eax"),15);	# family ID
	&cmp	(&HB("eax"),15);	# P4?
	&jne	(&label("notintel"));
	&or	("edx",1<<20);		# set reserved bit#20 to engage RC4_CHAR
&set_label("notintel");
	&bt	("edx",28);		# test hyper-threading bit
	&jnc	(&label("generic"));
	&and	("edx",0xefffffff);
	&cmp	("esi",0);
	&je	(&label("generic"));

	&or	("edx",0x10000000);
	&shr	("ebx",16);
	&cmp	(&LB("ebx"),1);
	&ja	(&label("generic"));
	&and	("edx",0xefffffff);	# clear hyper-threading bit if not

&set_label("generic");
	&and	("ebp",1<<11);		# isolate AMD XOP flag
	&and	("ecx",0xfffff7ff);	# force 11th bit to 0
	&mov	("esi","edx");		# %ebp:%esi is copy of %ecx:%edx
	&or	("ebp","ecx");		# merge AMD XOP flag

	&cmp	("edi",7);
	&mov	("edi",&wparam(0));
	&jb	(&label("no_extended_info"));
	&mov	("eax",7);
	&xor	("ecx","ecx");
	&cpuid	();
	&mov	(&DWP(8,"edi"),"ebx");	# save extended feature flag
&set_label("no_extended_info");

	&bt	("ebp",27);		# check OSXSAVE bit
	&jnc	(&label("clear_avx"));
	&xor	("ecx","ecx");
	&data_byte(0x0f,0x01,0xd0);	# xgetbv
	&and	("eax",6);
	&cmp	("eax",6);
	&je	(&label("done"));
	&cmp	("eax",2);
	&je	(&label("clear_avx"));
&set_label("clear_xmm");
	&and	("ebp",0xfdfffffd);	# clear AESNI and PCLMULQDQ bits
	&and	("esi",0xfeffffff);	# clear FXSR
&set_label("clear_avx");
	&and	("ebp",0xefffe7ff);	# clear AVX, FMA and AMD XOP bits
	&and	(&DWP(8,"edi"),0xffffffdf);	# clear AVX2
&set_label("done");
	&mov	("eax","esi");
	&mov	("edx","ebp");
&set_label("nocpuid");
&function_end("OPENSSL_ia32_cpuid");

&external_label("OPENSSL_ia32cap_P");

&function_begin_B("OPENSSL_rdtsc","EXTRN\t_OPENSSL_ia32cap_P:DWORD");
	&xor	("eax","eax");
	&xor	("edx","edx");
	&picmeup("ecx","OPENSSL_ia32cap_P");
	&bt	(&DWP(0,"ecx"),4);
	&jnc	(&label("notsc"));
	&rdtsc	();
&set_label("notsc");
	&ret	();
&function_end_B("OPENSSL_rdtsc");

# This works in Ring 0 only [read DJGPP+MS-DOS+privileged DPMI host],
# but it's safe to call it on any [supported] 32-bit platform...
# Just check for [non-]zero return value...
&function_begin_B("OPENSSL_instrument_halt","EXTRN\t_OPENSSL_ia32cap_P:DWORD");
	&picmeup("ecx","OPENSSL_ia32cap_P");
	&bt	(&DWP(0,"ecx"),4);
	&jnc	(&label("nohalt"));	# no TSC

	&data_word(0x9058900e);		# push %cs; pop %eax
	&and	("eax",3);
	&jnz	(&label("nohalt"));	# not enough privileges

	&pushf	();
	&pop	("eax");
	&bt	("eax",9);
	&jnc	(&label("nohalt"));	# interrupts are disabled

	&rdtsc	();
	&push	("edx");
	&push	("eax");
	&halt	();
	&rdtsc	();

	&sub	("eax",&DWP(0,"esp"));
	&sbb	("edx",&DWP(4,"esp"));
	&add	("esp",8);
	&ret	();

&set_label("nohalt");
	&xor	("eax","eax");
	&xor	("edx","edx");
	&ret	();
&function_end_B("OPENSSL_instrument_halt");

# Essentially there is only one use for this function. Under DJGPP:
#
#	#include <go32.h>
#	...
#	i=OPENSSL_far_spin(_dos_ds,0x46c);
#	...
# to obtain the number of spins till closest timer interrupt.

&function_begin_B("OPENSSL_far_spin");
	&pushf	();
	&pop	("eax");
	&bt	("eax",9);
	&jnc	(&label("nospin"));	# interrupts are disabled

	&mov	("eax",&DWP(4,"esp"));
	&mov	("ecx",&DWP(8,"esp"));
	&data_word (0x90d88e1e);	# push %ds, mov %eax,%ds
	&xor	("eax","eax");
	&mov	("edx",&DWP(0,"ecx"));
	&jmp	(&label("spin"));

	&align	(16);
&set_label("spin");
	&inc	("eax");
	&cmp	("edx",&DWP(0,"ecx"));
	&je	(&label("spin"));

	&data_word (0x1f909090);	# pop	%ds
	&ret	();

&set_label("nospin");
	&xor	("eax","eax");
	&xor	("edx","edx");
	&ret	();
&function_end_B("OPENSSL_far_spin");

&function_begin_B("OPENSSL_wipe_cpu","EXTRN\t_OPENSSL_ia32cap_P:DWORD");
	&xor	("eax","eax");
	&xor	("edx","edx");
	&picmeup("ecx","OPENSSL_ia32cap_P");
	&mov	("ecx",&DWP(0,"ecx"));
	&bt	(&DWP(0,"ecx"),1);
	&jnc	(&label("no_x87"));
	if ($sse2) {
		&and	("ecx",1<<26|1<<24);	# check SSE2 and FXSR bits
		&cmp	("ecx",1<<26|1<<24);
		&jne	(&label("no_sse2"));
		&pxor	("xmm0","xmm0");
		&pxor	("xmm1","xmm1");
		&pxor	("xmm2","xmm2");
		&pxor	("xmm3","xmm3");
		&pxor	("xmm4","xmm4");
		&pxor	("xmm5","xmm5");
		&pxor	("xmm6","xmm6");
		&pxor	("xmm7","xmm7");
	&set_label("no_sse2");
	}
	# just a bunch of fldz to zap the fp/mm bank followed by finit...
	&data_word(0xeed9eed9,0xeed9eed9,0xeed9eed9,0xeed9eed9,0x90e3db9b);
&set_label("no_x87");
	&lea	("eax",&DWP(4,"esp"));
	&ret	();
&function_end_B("OPENSSL_wipe_cpu");

&function_begin_B("OPENSSL_atomic_add");
	&mov	("edx",&DWP(4,"esp"));	# fetch the pointer, 1st arg
	&mov	("ecx",&DWP(8,"esp"));	# fetch the increment, 2nd arg
	&push	("ebx");
	&nop	();
	&mov	("eax",&DWP(0,"edx"));
&set_label("spin");
	&lea	("ebx",&DWP(0,"eax","ecx"));
	&nop	();
	&data_word(0x1ab10ff0);	# lock;	cmpxchg	%ebx,(%edx)	# %eax is involved and is always reloaded
	&jne	(&label("spin"));
	&mov	("eax","ebx");	# OpenSSL expects the new value
	&pop	("ebx");
	&ret	();
&function_end_B("OPENSSL_atomic_add");

&function_begin_B("OPENSSL_cleanse");
	&mov	("edx",&wparam(0));
	&mov	("ecx",&wparam(1));
	&xor	("eax","eax");
	&cmp	("ecx",7);
	&jae	(&label("lot"));
	&cmp	("ecx",0);
	&je	(&label("ret"));
&set_label("little");
	&mov	(&BP(0,"edx"),"al");
	&sub	("ecx",1);
	&lea	("edx",&DWP(1,"edx"));
	&jnz	(&label("little"));
&set_label("ret");
	&ret	();

&set_label("lot",16);
	&test	("edx",3);
	&jz	(&label("aligned"));
	&mov	(&BP(0,"edx"),"al");
	&lea	("ecx",&DWP(-1,"ecx"));
	&lea	("edx",&DWP(1,"edx"));
	&jmp	(&label("lot"));
&set_label("aligned");
	&mov	(&DWP(0,"edx"),"eax");
	&lea	("ecx",&DWP(-4,"ecx"));
	&test	("ecx",-4);
	&lea	("edx",&DWP(4,"edx"));
	&jnz	(&label("aligned"));
	&cmp	("ecx",0);
	&jne	(&label("little"));
	&ret	();
&function_end_B("OPENSSL_cleanse");

&function_begin_B("CRYPTO_memcmp");
	&push	("esi");
	&push	("edi");
	&mov	("esi",&wparam(0));
	&mov	("edi",&wparam(1));
	&mov	("ecx",&wparam(2));
	&xor	("eax","eax");
	&xor	("edx","edx");
	&cmp	("ecx",0);
	&je	(&label("no_data"));
&set_label("loop");
	&mov	("dl",&BP(0,"esi"));
	&lea	("esi",&DWP(1,"esi"));
	&xor	("dl",&BP(0,"edi"));
	&lea	("edi",&DWP(1,"edi"));
	&or	("al","dl");
	&dec	("ecx");
	&jnz	(&label("loop"));
	&neg	("eax");
	&shr	("eax",31);
&set_label("no_data");
	&pop	("edi");
	&pop	("esi");
	&ret	();
&function_end_B("CRYPTO_memcmp");
{
my $lasttick = "esi";
my $lastdiff = "ebx";
my $out = "edi";
my $cnt = "ecx";
my $max = "ebp";

&function_begin("OPENSSL_instrument_bus");
    &mov	("eax",0);
    if ($sse2) {
	&picmeup("edx","OPENSSL_ia32cap_P");
	&bt	(&DWP(0,"edx"),4);
	&jnc	(&label("nogo"));	# no TSC
	&bt	(&DWP(0,"edx"),19);
	&jnc	(&label("nogo"));	# no CLFLUSH

	&mov	($out,&wparam(0));	# load arguments
	&mov	($cnt,&wparam(1));

	# collect 1st tick
	&rdtsc	();
	&mov	($lasttick,"eax");	# lasttick = tick
	&mov	($lastdiff,0);		# lastdiff = 0
	&clflush(&DWP(0,$out));
	&data_byte(0xf0);		# lock
	&add	(&DWP(0,$out),$lastdiff);
	&jmp	(&label("loop"));

&set_label("loop",16);
	&rdtsc	();
	&mov	("edx","eax");		# put aside tick (yes, I neglect edx)
	&sub	("eax",$lasttick);	# diff
	&mov	($lasttick,"edx");	# lasttick = tick
	&mov	($lastdiff,"eax");	# lastdiff = diff
	&clflush(&DWP(0,$out));
	&data_byte(0xf0);		# lock
	&add	(&DWP(0,$out),"eax");	# accumulate diff
	&lea	($out,&DWP(4,$out));	# ++$out
	&sub	($cnt,1);		# --$cnt
	&jnz	(&label("loop"));

	&mov	("eax",&wparam(1));
&set_label("nogo");
    }
&function_end("OPENSSL_instrument_bus");

&function_begin("OPENSSL_instrument_bus2");
    &mov	("eax",0);
    if ($sse2) {
	&picmeup("edx","OPENSSL_ia32cap_P");
	&bt	(&DWP(0,"edx"),4);
	&jnc	(&label("nogo"));	# no TSC
	&bt	(&DWP(0,"edx"),19);
	&jnc	(&label("nogo"));	# no CLFLUSH

	&mov	($out,&wparam(0));	# load arguments
	&mov	($cnt,&wparam(1));
	&mov	($max,&wparam(2));

	&rdtsc	();			# collect 1st tick
	&mov	($lasttick,"eax");	# lasttick = tick
	&mov	($lastdiff,0);		# lastdiff = 0

	&clflush(&DWP(0,$out));
	&data_byte(0xf0);		# lock
	&add	(&DWP(0,$out),$lastdiff);

	&rdtsc	();			# collect 1st diff
	&mov	("edx","eax");		# put aside tick (yes, I neglect edx)
	&sub	("eax",$lasttick);	# diff
	&mov	($lasttick,"edx");	# lasttick = tick
	&mov	($lastdiff,"eax");	# lastdiff = diff
	&jmp	(&label("loop2"));

&set_label("loop2",16);
	&clflush(&DWP(0,$out));
	&data_byte(0xf0);		# lock
	&add	(&DWP(0,$out),"eax");	# accumulate diff

	&sub	($max,1);
	&jz	(&label("done2"));

	&rdtsc	();
	&mov	("edx","eax");		# put aside tick (yes, I neglect edx)
	&sub	("eax",$lasttick);	# diff
	&mov	($lasttick,"edx");	# lasttick = tick
	&cmp	("eax",$lastdiff);
	&mov	($lastdiff,"eax");	# lastdiff = diff
	&mov	("edx",0);
	&setne	("dl");
	&sub	($cnt,"edx");		# conditional --$cnt
	&lea	($out,&DWP(0,$out,"edx",4));	# conditional ++$out
	&jnz	(&label("loop2"));

&set_label("done2");
	&mov	("eax",&wparam(1));
	&sub	("eax",$cnt);
&set_label("nogo");
    }
&function_end("OPENSSL_instrument_bus2");
}

sub gen_random {
my $rdop = shift;
&function_begin_B("OPENSSL_ia32_${rdop}_bytes");
	&push	("edi");
	&push	("ebx");
	&xor	("eax","eax");		# return value
	&mov	("edi",&wparam(0));
	&mov	("ebx",&wparam(1));

	&cmp	("ebx",0);
	&je	(&label("done"));

	&mov	("ecx",8);
&set_label("loop");
	&${rdop}("edx");
	&jc	(&label("break"));
	&loop	(&label("loop"));
	&jmp	(&label("done"));

&set_label("break",16);
	&cmp	("ebx",4);
	&jb	(&label("tail"));
	&mov	(&DWP(0,"edi"),"edx");
	&lea	("edi",&DWP(4,"edi"));
	&add	("eax",4);
	&sub	("ebx",4);
	&jz	(&label("done"));
	&mov	("ecx",8);
	&jmp	(&label("loop"));

&set_label("tail",16);
	&mov	(&BP(0,"edi"),"dl");
	&lea	("edi",&DWP(1,"edi"));
	&inc	("eax");
	&shr	("edx",8);
	&dec	("ebx");
	&jnz	(&label("tail"));

&set_label("done");
	&xor	("edx","edx");		# Clear random value from registers
	&pop	("ebx");
	&pop	("edi");
	&ret	();
&function_end_B("OPENSSL_ia32_${rdop}_bytes");
}
&gen_random("rdrand");
&gen_random("rdseed");

&initseg("OPENSSL_cpuid_setup");

&hidden("OPENSSL_cpuid_setup");
&hidden("OPENSSL_ia32cap_P");

&asm_finish();

close STDOUT;

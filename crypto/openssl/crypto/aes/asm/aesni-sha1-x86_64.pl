#! /usr/bin/env perl
# Copyright 2011-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# June 2011
#
# This is AESNI-CBC+SHA1 "stitch" implementation. The idea, as spelled
# in http://download.intel.com/design/intarch/papers/323686.pdf, is
# that since AESNI-CBC encrypt exhibit *very* low instruction-level
# parallelism, interleaving it with another algorithm would allow to
# utilize processor resources better and achieve better performance.
# SHA1 instruction sequences(*) are taken from sha1-x86_64.pl and
# AESNI code is weaved into it. Below are performance numbers in
# cycles per processed byte, less is better, for standalone AESNI-CBC
# encrypt, sum of the latter and standalone SHA1, and "stitched"
# subroutine:
#
#		AES-128-CBC	+SHA1		stitch      gain
# Westmere	3.77[+5.3]	9.07		6.55	    +38%
# Sandy Bridge	5.05[+5.0(6.1)]	10.06(11.15)	5.98(7.05)  +68%(+58%)
# Ivy Bridge	5.05[+4.6]	9.65		5.54        +74%
# Haswell	4.43[+3.6(4.2)]	8.00(8.58)	4.55(5.21)  +75%(+65%)
# Skylake	2.63[+3.5(4.1)]	6.17(6.69)	4.23(4.44)  +46%(+51%)
# Bulldozer	5.77[+6.0]	11.72		6.37        +84%
# Ryzen(**)	2.71[+1.93]	4.64		2.74        +69%
# Goldmont(**)	3.82[+1.70]	5.52		4.20        +31%
#
#		AES-192-CBC
# Westmere	4.51		9.81		6.80	    +44%
# Sandy Bridge	6.05		11.06(12.15)	6.11(7.19)  +81%(+69%)
# Ivy Bridge	6.05		10.65		6.07        +75%
# Haswell	5.29		8.86(9.44)	5.32(5.32)  +67%(+77%)
# Bulldozer	6.89		12.84		6.96        +84%
#
#		AES-256-CBC
# Westmere	5.25		10.55		7.21	    +46%
# Sandy Bridge	7.05		12.06(13.15)	7.12(7.72)  +69%(+70%)
# Ivy Bridge	7.05		11.65		7.12        +64%
# Haswell	6.19		9.76(10.34)	6.21(6.25)  +57%(+65%)
# Skylake	3.62		7.16(7.68)	4.56(4.76)  +57%(+61%)
# Bulldozer	8.00		13.95		8.25        +69%
# Ryzen(**)	3.71		5.64		3.72        +52%
# Goldmont(**)	5.35		7.05		5.76        +22%
#
# (*)	There are two code paths: SSSE3 and AVX. See sha1-568.pl for
#	background information. Above numbers in parentheses are SSSE3
#	results collected on AVX-capable CPU, i.e. apply on OSes that
#	don't support AVX.
# (**)	SHAEXT results.
#
# Needless to mention that it makes no sense to implement "stitched"
# *decrypt* subroutine. Because *both* AESNI-CBC decrypt and SHA1
# fully utilize parallelism, so stitching would not give any gain
# anyway. Well, there might be some, e.g. because of better cache
# locality... For reference, here are performance results for
# standalone AESNI-CBC decrypt:
#
#		AES-128-CBC	AES-192-CBC	AES-256-CBC
# Westmere	1.25		1.50		1.75
# Sandy Bridge	0.74		0.91		1.09
# Ivy Bridge	0.74		0.90		1.11
# Haswell	0.63		0.76		0.88
# Bulldozer	0.70		0.85		0.99

# And indeed:
#
#		AES-256-CBC	+SHA1		stitch      gain
# Westmere	1.75		7.20		6.68        +7.8%
# Sandy Bridge	1.09		6.09(7.22)	5.82(6.95)  +4.6%(+3.9%)
# Ivy Bridge	1.11		5.70		5.45        +4.6%
# Haswell	0.88		4.45(5.00)	4.39(4.69)  +1.4%(*)(+6.6%)
# Bulldozer	0.99		6.95		5.95        +17%(**)
#
# (*)	Tiny improvement coefficient on Haswell is because we compare
#	AVX1 stitch to sum with AVX2 SHA1.
# (**)	Execution is fully dominated by integer code sequence and
#	SIMD still hardly shows [in single-process benchmark;-]

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

$avx=1 if (`$ENV{CC} -Wa,-v -c -o /dev/null -x assembler /dev/null 2>&1`
		=~ /GNU assembler version ([2-9]\.[0-9]+)/ &&
	   $1>=2.19);
$avx=1 if (!$avx && $win64 && ($flavour =~ /nasm/ || $ENV{ASM} =~ /nasm/) &&
	   `nasm -v 2>&1` =~ /NASM version ([2-9]\.[0-9]+)/ &&
	   $1>=2.09);
$avx=1 if (!$avx && $win64 && ($flavour =~ /masm/ || $ENV{ASM} =~ /ml64/) &&
	   `ml64 2>&1` =~ /Version ([0-9]+)\./ &&
	   $1>=10);
$avx=1 if (!$avx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|.*based on LLVM) ([3-9]\.[0-9]+)/ && $2>=3.0);

$shaext=1;	### set to zero if compiling for 1.0.1

$stitched_decrypt=0;

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

# void aesni_cbc_sha1_enc(const void *inp,
#			void *out,
#			size_t length,
#			const AES_KEY *key,
#			unsigned char *iv,
#			SHA_CTX *ctx,
#			const void *in0);

$code.=<<___;
.text
.extern	OPENSSL_ia32cap_P

.globl	aesni_cbc_sha1_enc
.type	aesni_cbc_sha1_enc,\@abi-omnipotent
.align	32
aesni_cbc_sha1_enc:
	# caller should check for SSSE3 and AES-NI bits
	mov	OPENSSL_ia32cap_P+0(%rip),%r10d
	mov	OPENSSL_ia32cap_P+4(%rip),%r11
___
$code.=<<___ if ($shaext);
	bt	\$61,%r11		# check SHA bit
	jc	aesni_cbc_sha1_enc_shaext
___
$code.=<<___ if ($avx);
	and	\$`1<<28`,%r11d		# mask AVX bit
	and	\$`1<<30`,%r10d		# mask "Intel CPU" bit
	or	%r11d,%r10d
	cmp	\$`1<<28|1<<30`,%r10d
	je	aesni_cbc_sha1_enc_avx
___
$code.=<<___;
	jmp	aesni_cbc_sha1_enc_ssse3
	ret
.size	aesni_cbc_sha1_enc,.-aesni_cbc_sha1_enc
___

my ($in0,$out,$len,$key,$ivp,$ctx,$inp)=("%rdi","%rsi","%rdx","%rcx","%r8","%r9","%r10");

my $Xi=4;
my @X=map("%xmm$_",(4..7,0..3));
my @Tx=map("%xmm$_",(8..10));
my @V=($A,$B,$C,$D,$E)=("%eax","%ebx","%ecx","%edx","%ebp");	# size optimization
my @T=("%esi","%edi");
my $j=0; my $jj=0; my $r=0; my $sn=0; my $rx=0;
my $K_XX_XX="%r11";
my ($rndkey0,$iv,$in)=map("%xmm$_",(11..13));			# for enc
my @rndkey=("%xmm14","%xmm15");					# for enc
my ($inout0,$inout1,$inout2,$inout3)=map("%xmm$_",(12..15));	# for dec

if (1) {	# reassign for Atom Silvermont
    # The goal is to minimize amount of instructions with more than
    # 3 prefix bytes. Or in more practical terms to keep AES-NI *and*
    # SSSE3 instructions to upper half of the register bank.
    @X=map("%xmm$_",(8..11,4..7));
    @Tx=map("%xmm$_",(12,13,3));
    ($iv,$in,$rndkey0)=map("%xmm$_",(2,14,15));
    @rndkey=("%xmm0","%xmm1");
}

sub AUTOLOAD()		# thunk [simplified] 32-bit style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://;
  my $arg = pop;
    $arg = "\$$arg" if ($arg*1 eq $arg);
    $code .= "\t$opcode\t".join(',',$arg,reverse @_)."\n";
}

my $_rol=sub { &rol(@_) };
my $_ror=sub { &ror(@_) };

$code.=<<___;
.type	aesni_cbc_sha1_enc_ssse3,\@function,6
.align	32
aesni_cbc_sha1_enc_ssse3:
.cfi_startproc
	mov	`($win64?56:8)`(%rsp),$inp	# load 7th argument
	#shr	\$6,$len			# debugging artefact
	#jz	.Lepilogue_ssse3		# debugging artefact
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	lea	`-104-($win64?10*16:0)`(%rsp),%rsp
.cfi_adjust_cfa_offset	`104+($win64?10*16:0)`
	#mov	$in0,$inp			# debugging artefact
	#lea	64(%rsp),$ctx			# debugging artefact
___
$code.=<<___ if ($win64);
	movaps	%xmm6,96+0(%rsp)
	movaps	%xmm7,96+16(%rsp)
	movaps	%xmm8,96+32(%rsp)
	movaps	%xmm9,96+48(%rsp)
	movaps	%xmm10,96+64(%rsp)
	movaps	%xmm11,96+80(%rsp)
	movaps	%xmm12,96+96(%rsp)
	movaps	%xmm13,96+112(%rsp)
	movaps	%xmm14,96+128(%rsp)
	movaps	%xmm15,96+144(%rsp)
.Lprologue_ssse3:
___
$code.=<<___;
	mov	$in0,%r12			# reassign arguments
	mov	$out,%r13
	mov	$len,%r14
	lea	112($key),%r15			# size optimization
	movdqu	($ivp),$iv			# load IV
	mov	$ivp,88(%rsp)			# save $ivp
___
($in0,$out,$len,$key)=map("%r$_",(12..15));	# reassign arguments
my $rounds="${ivp}d";
$code.=<<___;
	shl	\$6,$len
	sub	$in0,$out
	mov	240-112($key),$rounds
	add	$inp,$len		# end of input

	lea	K_XX_XX(%rip),$K_XX_XX
	mov	0($ctx),$A		# load context
	mov	4($ctx),$B
	mov	8($ctx),$C
	mov	12($ctx),$D
	mov	$B,@T[0]		# magic seed
	mov	16($ctx),$E
	mov	$C,@T[1]
	xor	$D,@T[1]
	and	@T[1],@T[0]

	movdqa	64($K_XX_XX),@Tx[2]	# pbswap mask
	movdqa	0($K_XX_XX),@Tx[1]	# K_00_19
	movdqu	0($inp),@X[-4&7]	# load input to %xmm[0-3]
	movdqu	16($inp),@X[-3&7]
	movdqu	32($inp),@X[-2&7]
	movdqu	48($inp),@X[-1&7]
	pshufb	@Tx[2],@X[-4&7]		# byte swap
	pshufb	@Tx[2],@X[-3&7]
	pshufb	@Tx[2],@X[-2&7]
	add	\$64,$inp
	paddd	@Tx[1],@X[-4&7]		# add K_00_19
	pshufb	@Tx[2],@X[-1&7]
	paddd	@Tx[1],@X[-3&7]
	paddd	@Tx[1],@X[-2&7]
	movdqa	@X[-4&7],0(%rsp)	# X[]+K xfer to IALU
	psubd	@Tx[1],@X[-4&7]		# restore X[]
	movdqa	@X[-3&7],16(%rsp)
	psubd	@Tx[1],@X[-3&7]
	movdqa	@X[-2&7],32(%rsp)
	psubd	@Tx[1],@X[-2&7]
	movups	-112($key),$rndkey0	# $key[0]
	movups	16-112($key),$rndkey[0]	# forward reference
	jmp	.Loop_ssse3
___

my $aesenc=sub {
  use integer;
  my ($n,$k)=($r/10,$r%10);
    if ($k==0) {
      $code.=<<___;
	movups		`16*$n`($in0),$in		# load input
	xorps		$rndkey0,$in
___
      $code.=<<___ if ($n);
	movups		$iv,`16*($n-1)`($out,$in0)	# write output
___
      $code.=<<___;
	xorps		$in,$iv
	movups		`32+16*$k-112`($key),$rndkey[1]
	aesenc		$rndkey[0],$iv
___
    } elsif ($k==9) {
      $sn++;
      $code.=<<___;
	cmp		\$11,$rounds
	jb		.Laesenclast$sn
	movups		`32+16*($k+0)-112`($key),$rndkey[1]
	aesenc		$rndkey[0],$iv
	movups		`32+16*($k+1)-112`($key),$rndkey[0]
	aesenc		$rndkey[1],$iv
	je		.Laesenclast$sn
	movups		`32+16*($k+2)-112`($key),$rndkey[1]
	aesenc		$rndkey[0],$iv
	movups		`32+16*($k+3)-112`($key),$rndkey[0]
	aesenc		$rndkey[1],$iv
.Laesenclast$sn:
	aesenclast	$rndkey[0],$iv
	movups		16-112($key),$rndkey[1]		# forward reference
___
    } else {
      $code.=<<___;
	movups		`32+16*$k-112`($key),$rndkey[1]
	aesenc		$rndkey[0],$iv
___
    }
    $r++;	unshift(@rndkey,pop(@rndkey));
};

sub Xupdate_ssse3_16_31()		# recall that $Xi starts with 4
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 40 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));		# ror
	&pshufd	(@X[0],@X[-4&7],0xee);	# was &movdqa	(@X[0],@X[-3&7]);
	 eval(shift(@insns));
	&movdqa	(@Tx[0],@X[-1&7]);
	  &paddd	(@Tx[1],@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	&punpcklqdq(@X[0],@X[-3&7]);	# compose "X[-14]" in "X[0]", was &palignr(@X[0],@X[-4&7],8);
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	&psrldq	(@Tx[0],4);		# "X[-3]", 3 dwords
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pxor	(@X[0],@X[-4&7]);	# "X[0]"^="X[-16]"
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	&pxor	(@Tx[0],@X[-2&7]);	# "X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pxor	(@X[0],@Tx[0]);		# "X[0]"^="X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	  &movdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));

	&movdqa	(@Tx[2],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	&movdqa	(@Tx[0],@X[0]);
	 eval(shift(@insns));

	&pslldq	(@Tx[2],12);		# "X[0]"<<96, extract one dword
	&paddd	(@X[0],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	&psrld	(@Tx[0],31);
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	&movdqa	(@Tx[1],@Tx[2]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	&psrld	(@Tx[2],30);
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	&por	(@X[0],@Tx[0]);		# "X[0]"<<<=1
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pslld	(@Tx[1],2);
	&pxor	(@X[0],@Tx[2]);
	 eval(shift(@insns));
	  &movdqa	(@Tx[2],eval(16*(($Xi)/5))."($K_XX_XX)");	# K_XX_XX
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pxor	(@X[0],@Tx[1]);		# "X[0]"^=("X[0]">>96)<<<2
	&pshufd (@Tx[1],@X[-1&7],0xee)	if ($Xi==7);	# was &movdqa	(@Tx[0],@X[-1&7]) in Xupdate_ssse3_32_79

	 foreach (@insns) { eval; }	# remaining instructions [if any]

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
		push(@Tx,shift(@Tx));
}

sub Xupdate_ssse3_32_79()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 to 44 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns))		if ($Xi==8);
	&pxor	(@X[0],@X[-4&7]);	# "X[0]"="X[-32]"^"X[-16]"
	 eval(shift(@insns))		if ($Xi==8);
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns))		if (@insns[1] =~ /_ror/);
	 eval(shift(@insns))		if (@insns[0] =~ /_ror/);
	&punpcklqdq(@Tx[0],@X[-1&7]);	# compose "X[-6]", was &palignr(@Tx[0],@X[-2&7],8);
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol

	&pxor	(@X[0],@X[-7&7]);	# "X[0]"^="X[-28]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	if ($Xi%5) {
	  &movdqa	(@Tx[2],@Tx[1]);# "perpetuate" K_XX_XX...
	} else {			# ... or load next one
	  &movdqa	(@Tx[2],eval(16*($Xi/5))."($K_XX_XX)");
	}
	 eval(shift(@insns));		# ror
	  &paddd	(@Tx[1],@X[-1&7]);
	 eval(shift(@insns));

	&pxor	(@X[0],@Tx[0]);		# "X[0]"^="X[-6]"
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns))		if (@insns[0] =~ /_ror/);

	&movdqa	(@Tx[0],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &movdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer to IALU
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));
	 eval(shift(@insns));		# body_20_39

	&pslld	(@X[0],2);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&psrld	(@Tx[0],30);
	 eval(shift(@insns))		if (@insns[0] =~ /_rol/);# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror

	&por	(@X[0],@Tx[0]);		# "X[0]"<<<=2
	 eval(shift(@insns));
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns))		if (@insns[1] =~ /_rol/);
	 eval(shift(@insns))		if (@insns[0] =~ /_rol/);
	  &pshufd(@Tx[1],@X[-1&7],0xee)	if ($Xi<19);	# was &movdqa	(@Tx[1],@X[0])
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));

	 foreach (@insns) { eval; }	# remaining instructions

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
		push(@Tx,shift(@Tx));
}

sub Xuplast_ssse3_80()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &paddd	(@Tx[1],@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	  &movdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer IALU

	 foreach (@insns) { eval; }		# remaining instructions

	&cmp	($inp,$len);
	&je	(shift);

	unshift(@Tx,pop(@Tx));

	&movdqa	(@Tx[2],"64($K_XX_XX)");	# pbswap mask
	&movdqa	(@Tx[1],"0($K_XX_XX)");		# K_00_19
	&movdqu	(@X[-4&7],"0($inp)");		# load input
	&movdqu	(@X[-3&7],"16($inp)");
	&movdqu	(@X[-2&7],"32($inp)");
	&movdqu	(@X[-1&7],"48($inp)");
	&pshufb	(@X[-4&7],@Tx[2]);		# byte swap
	&add	($inp,64);

  $Xi=0;
}

sub Xloop_ssse3()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&pshufb	(@X[($Xi-3)&7],@Tx[2]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&paddd	(@X[($Xi-4)&7],@Tx[1]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&movdqa	(eval(16*$Xi)."(%rsp)",@X[($Xi-4)&7]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&psubd	(@X[($Xi-4)&7],@Tx[1]);

	foreach (@insns) { eval; }
  $Xi++;
}

sub Xtail_ssse3()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	foreach (@insns) { eval; }
}

my @body_00_19 = (
	'($a,$b,$c,$d,$e)=@V;'.
	'&$_ror	($b,$j?7:2);',	# $b>>>2
	'&xor	(@T[0],$d);',
	'&mov	(@T[1],$a);',	# $b for next round

	'&add	($e,eval(4*($j&15))."(%rsp)");',# X[]+K xfer
	'&xor	($b,$c);',	# $c^$d for next round

	'&$_rol	($a,5);',
	'&add	($e,@T[0]);',
	'&and	(@T[1],$b);',	# ($b&($c^$d)) for next round

	'&xor	($b,$c);',	# restore $b
	'&add	($e,$a);'	.'$j++; unshift(@V,pop(@V)); unshift(@T,pop(@T));'
	);

sub body_00_19 () {	# ((c^d)&b)^d
    # on start @T[0]=(c^d)&b
    return &body_20_39() if ($rx==19); $rx++;

    use integer;
    my ($k,$n);
    my @r=@body_00_19;

	$n = scalar(@r);
	$k = (($jj+1)*12/20)*20*$n/12;	# 12 aesencs per these 20 rounds
	@r[$k%$n].='&$aesenc();'	if ($jj==$k/$n);
	$jj++;

    return @r;
}

my @body_20_39 = (
	'($a,$b,$c,$d,$e)=@V;'.
	'&add	($e,eval(4*($j&15))."(%rsp)");',# X[]+K xfer
	'&xor	(@T[0],$d)	if($j==19);'.
	'&xor	(@T[0],$c)	if($j> 19);',	# ($b^$d^$c)
	'&mov	(@T[1],$a);',	# $b for next round

	'&$_rol	($a,5);',
	'&add	($e,@T[0]);',
	'&xor	(@T[1],$c)	if ($j< 79);',	# $b^$d for next round

	'&$_ror	($b,7);',	# $b>>>2
	'&add	($e,$a);'	.'$j++; unshift(@V,pop(@V)); unshift(@T,pop(@T));'
	);

sub body_20_39 () {	# b^d^c
    # on entry @T[0]=b^d
    return &body_40_59() if ($rx==39); $rx++;

    use integer;
    my ($k,$n);
    my @r=@body_20_39;

	$n = scalar(@r);
	$k = (($jj+1)*8/20)*20*$n/8;	# 8 aesencs per these 20 rounds
	@r[$k%$n].='&$aesenc();'	if ($jj==$k/$n && $rx!=20);
	$jj++;

    return @r;
}

my @body_40_59 = (
	'($a,$b,$c,$d,$e)=@V;'.
	'&add	($e,eval(4*($j&15))."(%rsp)");',# X[]+K xfer
	'&and	(@T[0],$c)	if ($j>=40);',	# (b^c)&(c^d)
	'&xor	($c,$d)		if ($j>=40);',	# restore $c

	'&$_ror	($b,7);',	# $b>>>2
	'&mov	(@T[1],$a);',	# $b for next round
	'&xor	(@T[0],$c);',

	'&$_rol	($a,5);',
	'&add	($e,@T[0]);',
	'&xor	(@T[1],$c)	if ($j==59);'.
	'&xor	(@T[1],$b)	if ($j< 59);',	# b^c for next round

	'&xor	($b,$c)		if ($j< 59);',	# c^d for next round
	'&add	($e,$a);'	.'$j++; unshift(@V,pop(@V)); unshift(@T,pop(@T));'
	);

sub body_40_59 () {	# ((b^c)&(c^d))^c
    # on entry @T[0]=(b^c), (c^=d)
    $rx++;

    use integer;
    my ($k,$n);
    my @r=@body_40_59;

	$n = scalar(@r);
	$k=(($jj+1)*12/20)*20*$n/12;	# 12 aesencs per these 20 rounds
	@r[$k%$n].='&$aesenc();'	if ($jj==$k/$n && $rx!=40);
	$jj++;

    return @r;
}
$code.=<<___;
.align	32
.Loop_ssse3:
___
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_32_79(\&body_00_19);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xuplast_ssse3_80(\&body_20_39,".Ldone_ssse3");	# can jump to "done"

				$saved_j=$j; @saved_V=@V;
				$saved_r=$r; @saved_rndkey=@rndkey;

	&Xloop_ssse3(\&body_20_39);
	&Xloop_ssse3(\&body_20_39);
	&Xloop_ssse3(\&body_20_39);

$code.=<<___;
	movups	$iv,48($out,$in0)		# write output
	lea	64($in0),$in0

	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	add	12($ctx),$D
	mov	$A,0($ctx)
	add	16($ctx),$E
	mov	@T[0],4($ctx)
	mov	@T[0],$B			# magic seed
	mov	$C,8($ctx)
	mov	$C,@T[1]
	mov	$D,12($ctx)
	xor	$D,@T[1]
	mov	$E,16($ctx)
	and	@T[1],@T[0]
	jmp	.Loop_ssse3

.Ldone_ssse3:
___
				$jj=$j=$saved_j; @V=@saved_V;
				$r=$saved_r;     @rndkey=@saved_rndkey;

	&Xtail_ssse3(\&body_20_39);
	&Xtail_ssse3(\&body_20_39);
	&Xtail_ssse3(\&body_20_39);

$code.=<<___;
	movups	$iv,48($out,$in0)		# write output
	mov	88(%rsp),$ivp			# restore $ivp

	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	mov	$A,0($ctx)
	add	12($ctx),$D
	mov	@T[0],4($ctx)
	add	16($ctx),$E
	mov	$C,8($ctx)
	mov	$D,12($ctx)
	mov	$E,16($ctx)
	movups	$iv,($ivp)			# write IV
___
$code.=<<___ if ($win64);
	movaps	96+0(%rsp),%xmm6
	movaps	96+16(%rsp),%xmm7
	movaps	96+32(%rsp),%xmm8
	movaps	96+48(%rsp),%xmm9
	movaps	96+64(%rsp),%xmm10
	movaps	96+80(%rsp),%xmm11
	movaps	96+96(%rsp),%xmm12
	movaps	96+112(%rsp),%xmm13
	movaps	96+128(%rsp),%xmm14
	movaps	96+144(%rsp),%xmm15
___
$code.=<<___;
	lea	`104+($win64?10*16:0)`(%rsp),%rsi
.cfi_def_cfa	%rsi,56
	mov	0(%rsi),%r15
.cfi_restore	%r15
	mov	8(%rsi),%r14
.cfi_restore	%r14
	mov	16(%rsi),%r13
.cfi_restore	%r13
	mov	24(%rsi),%r12
.cfi_restore	%r12
	mov	32(%rsi),%rbp
.cfi_restore	%rbp
	mov	40(%rsi),%rbx
.cfi_restore	%rbx
	lea	48(%rsi),%rsp
.cfi_def_cfa	%rsp,8
.Lepilogue_ssse3:
	ret
.cfi_endproc
.size	aesni_cbc_sha1_enc_ssse3,.-aesni_cbc_sha1_enc_ssse3
___

						if ($stitched_decrypt) {{{
# reset
($in0,$out,$len,$key,$ivp,$ctx,$inp)=("%rdi","%rsi","%rdx","%rcx","%r8","%r9","%r10");
$j=$jj=$r=$rx=0;
$Xi=4;

# reassign for Atom Silvermont (see above)
($inout0,$inout1,$inout2,$inout3,$rndkey0)=map("%xmm$_",(0..4));
@X=map("%xmm$_",(8..13,6,7));
@Tx=map("%xmm$_",(14,15,5));

my @aes256_dec = (
	'&movdqu($inout0,"0x00($in0)");',
	'&movdqu($inout1,"0x10($in0)");	&pxor	($inout0,$rndkey0);',
	'&movdqu($inout2,"0x20($in0)");	&pxor	($inout1,$rndkey0);',
	'&movdqu($inout3,"0x30($in0)");	&pxor	($inout2,$rndkey0);',

	'&pxor	($inout3,$rndkey0);	&movups	($rndkey0,"16-112($key)");',
	'&movaps("64(%rsp)",@X[2]);',	# save IV, originally @X[3]
	undef,undef
	);
for ($i=0;$i<13;$i++) {
    push (@aes256_dec,(
	'&aesdec	($inout0,$rndkey0);',
	'&aesdec	($inout1,$rndkey0);',
	'&aesdec	($inout2,$rndkey0);',
	'&aesdec	($inout3,$rndkey0);	&movups($rndkey0,"'.(16*($i+2)-112).'($key)");'
	));
    push (@aes256_dec,(undef,undef))	if (($i>=3 && $i<=5) || $i>=11);
    push (@aes256_dec,(undef,undef))	if ($i==5);
}
push(@aes256_dec,(
	'&aesdeclast	($inout0,$rndkey0);	&movups	(@X[0],"0x00($in0)");',
	'&aesdeclast	($inout1,$rndkey0);	&movups	(@X[1],"0x10($in0)");',
	'&aesdeclast	($inout2,$rndkey0);	&movups	(@X[2],"0x20($in0)");',
	'&aesdeclast	($inout3,$rndkey0);	&movups	(@X[3],"0x30($in0)");',

	'&xorps		($inout0,"64(%rsp)");	&movdqu	($rndkey0,"-112($key)");',
	'&xorps		($inout1,@X[0]);	&movups	("0x00($out,$in0)",$inout0);',
	'&xorps		($inout2,@X[1]);	&movups	("0x10($out,$in0)",$inout1);',
	'&xorps		($inout3,@X[2]);	&movups	("0x20($out,$in0)",$inout2);',

	'&movups	("0x30($out,$in0)",$inout3);'
	));

sub body_00_19_dec () {	# ((c^d)&b)^d
    # on start @T[0]=(c^d)&b
    return &body_20_39_dec() if ($rx==19);

    my @r=@body_00_19;

	unshift (@r,@aes256_dec[$rx])	if (@aes256_dec[$rx]);
	$rx++;

    return @r;
}

sub body_20_39_dec () {	# b^d^c
    # on entry @T[0]=b^d
    return &body_40_59_dec() if ($rx==39);

    my @r=@body_20_39;

	unshift (@r,@aes256_dec[$rx])	if (@aes256_dec[$rx]);
	$rx++;

    return @r;
}

sub body_40_59_dec () {	# ((b^c)&(c^d))^c
    # on entry @T[0]=(b^c), (c^=d)

    my @r=@body_40_59;

	unshift (@r,@aes256_dec[$rx])	if (@aes256_dec[$rx]);
	$rx++;

    return @r;
}

$code.=<<___;
.globl	aesni256_cbc_sha1_dec
.type	aesni256_cbc_sha1_dec,\@abi-omnipotent
.align	32
aesni256_cbc_sha1_dec:
	# caller should check for SSSE3 and AES-NI bits
	mov	OPENSSL_ia32cap_P+0(%rip),%r10d
	mov	OPENSSL_ia32cap_P+4(%rip),%r11d
___
$code.=<<___ if ($avx);
	and	\$`1<<28`,%r11d		# mask AVX bit
	and	\$`1<<30`,%r10d		# mask "Intel CPU" bit
	or	%r11d,%r10d
	cmp	\$`1<<28|1<<30`,%r10d
	je	aesni256_cbc_sha1_dec_avx
___
$code.=<<___;
	jmp	aesni256_cbc_sha1_dec_ssse3
	ret
.size	aesni256_cbc_sha1_dec,.-aesni256_cbc_sha1_dec

.type	aesni256_cbc_sha1_dec_ssse3,\@function,6
.align	32
aesni256_cbc_sha1_dec_ssse3:
.cfi_startproc
	mov	`($win64?56:8)`(%rsp),$inp	# load 7th argument
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	lea	`-104-($win64?10*16:0)`(%rsp),%rsp
.cfi_adjust_cfa_offset	`104+($win64?10*16:0)`
___
$code.=<<___ if ($win64);
	movaps	%xmm6,96+0(%rsp)
	movaps	%xmm7,96+16(%rsp)
	movaps	%xmm8,96+32(%rsp)
	movaps	%xmm9,96+48(%rsp)
	movaps	%xmm10,96+64(%rsp)
	movaps	%xmm11,96+80(%rsp)
	movaps	%xmm12,96+96(%rsp)
	movaps	%xmm13,96+112(%rsp)
	movaps	%xmm14,96+128(%rsp)
	movaps	%xmm15,96+144(%rsp)
.Lprologue_dec_ssse3:
___
$code.=<<___;
	mov	$in0,%r12			# reassign arguments
	mov	$out,%r13
	mov	$len,%r14
	lea	112($key),%r15			# size optimization
	movdqu	($ivp),@X[3]			# load IV
	#mov	$ivp,88(%rsp)			# save $ivp
___
($in0,$out,$len,$key)=map("%r$_",(12..15));	# reassign arguments
$code.=<<___;
	shl	\$6,$len
	sub	$in0,$out
	add	$inp,$len		# end of input

	lea	K_XX_XX(%rip),$K_XX_XX
	mov	0($ctx),$A		# load context
	mov	4($ctx),$B
	mov	8($ctx),$C
	mov	12($ctx),$D
	mov	$B,@T[0]		# magic seed
	mov	16($ctx),$E
	mov	$C,@T[1]
	xor	$D,@T[1]
	and	@T[1],@T[0]

	movdqa	64($K_XX_XX),@Tx[2]	# pbswap mask
	movdqa	0($K_XX_XX),@Tx[1]	# K_00_19
	movdqu	0($inp),@X[-4&7]	# load input to %xmm[0-3]
	movdqu	16($inp),@X[-3&7]
	movdqu	32($inp),@X[-2&7]
	movdqu	48($inp),@X[-1&7]
	pshufb	@Tx[2],@X[-4&7]		# byte swap
	add	\$64,$inp
	pshufb	@Tx[2],@X[-3&7]
	pshufb	@Tx[2],@X[-2&7]
	pshufb	@Tx[2],@X[-1&7]
	paddd	@Tx[1],@X[-4&7]		# add K_00_19
	paddd	@Tx[1],@X[-3&7]
	paddd	@Tx[1],@X[-2&7]
	movdqa	@X[-4&7],0(%rsp)	# X[]+K xfer to IALU
	psubd	@Tx[1],@X[-4&7]		# restore X[]
	movdqa	@X[-3&7],16(%rsp)
	psubd	@Tx[1],@X[-3&7]
	movdqa	@X[-2&7],32(%rsp)
	psubd	@Tx[1],@X[-2&7]
	movdqu	-112($key),$rndkey0	# $key[0]
	jmp	.Loop_dec_ssse3

.align	32
.Loop_dec_ssse3:
___
	&Xupdate_ssse3_16_31(\&body_00_19_dec);
	&Xupdate_ssse3_16_31(\&body_00_19_dec);
	&Xupdate_ssse3_16_31(\&body_00_19_dec);
	&Xupdate_ssse3_16_31(\&body_00_19_dec);
	&Xupdate_ssse3_32_79(\&body_00_19_dec);
	&Xupdate_ssse3_32_79(\&body_20_39_dec);
	&Xupdate_ssse3_32_79(\&body_20_39_dec);
	&Xupdate_ssse3_32_79(\&body_20_39_dec);
	&Xupdate_ssse3_32_79(\&body_20_39_dec);
	&Xupdate_ssse3_32_79(\&body_20_39_dec);
	&Xupdate_ssse3_32_79(\&body_40_59_dec);
	&Xupdate_ssse3_32_79(\&body_40_59_dec);
	&Xupdate_ssse3_32_79(\&body_40_59_dec);
	&Xupdate_ssse3_32_79(\&body_40_59_dec);
	&Xupdate_ssse3_32_79(\&body_40_59_dec);
	&Xupdate_ssse3_32_79(\&body_20_39_dec);
	&Xuplast_ssse3_80(\&body_20_39_dec,".Ldone_dec_ssse3");	# can jump to "done"

				$saved_j=$j;   @saved_V=@V;
				$saved_rx=$rx;

	&Xloop_ssse3(\&body_20_39_dec);
	&Xloop_ssse3(\&body_20_39_dec);
	&Xloop_ssse3(\&body_20_39_dec);

	eval(@aes256_dec[-1]);			# last store
$code.=<<___;
	lea	64($in0),$in0

	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	add	12($ctx),$D
	mov	$A,0($ctx)
	add	16($ctx),$E
	mov	@T[0],4($ctx)
	mov	@T[0],$B			# magic seed
	mov	$C,8($ctx)
	mov	$C,@T[1]
	mov	$D,12($ctx)
	xor	$D,@T[1]
	mov	$E,16($ctx)
	and	@T[1],@T[0]
	jmp	.Loop_dec_ssse3

.Ldone_dec_ssse3:
___
				$jj=$j=$saved_j; @V=@saved_V;
				$rx=$saved_rx;

	&Xtail_ssse3(\&body_20_39_dec);
	&Xtail_ssse3(\&body_20_39_dec);
	&Xtail_ssse3(\&body_20_39_dec);

	eval(@aes256_dec[-1]);			# last store
$code.=<<___;
	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	mov	$A,0($ctx)
	add	12($ctx),$D
	mov	@T[0],4($ctx)
	add	16($ctx),$E
	mov	$C,8($ctx)
	mov	$D,12($ctx)
	mov	$E,16($ctx)
	movups	@X[3],($ivp)			# write IV
___
$code.=<<___ if ($win64);
	movaps	96+0(%rsp),%xmm6
	movaps	96+16(%rsp),%xmm7
	movaps	96+32(%rsp),%xmm8
	movaps	96+48(%rsp),%xmm9
	movaps	96+64(%rsp),%xmm10
	movaps	96+80(%rsp),%xmm11
	movaps	96+96(%rsp),%xmm12
	movaps	96+112(%rsp),%xmm13
	movaps	96+128(%rsp),%xmm14
	movaps	96+144(%rsp),%xmm15
___
$code.=<<___;
	lea	`104+($win64?10*16:0)`(%rsp),%rsi
.cfi_cfa_def	%rsi,56
	mov	0(%rsi),%r15
.cfi_restore	%r15
	mov	8(%rsi),%r14
.cfi_restore	%r14
	mov	16(%rsi),%r13
.cfi_restore	%r13
	mov	24(%rsi),%r12
.cfi_restore	%r12
	mov	32(%rsi),%rbp
.cfi_restore	%rbp
	mov	40(%rsi),%rbx
.cfi_restore	%rbx
	lea	48(%rsi),%rsp
.cfi_cfa_def	%rsp,8
.Lepilogue_dec_ssse3:
	ret
.cfi_endproc
.size	aesni256_cbc_sha1_dec_ssse3,.-aesni256_cbc_sha1_dec_ssse3
___
						}}}
$j=$jj=$r=$rx=0;

if ($avx) {
my ($in0,$out,$len,$key,$ivp,$ctx,$inp)=("%rdi","%rsi","%rdx","%rcx","%r8","%r9","%r10");

my $Xi=4;
my @X=map("%xmm$_",(4..7,0..3));
my @Tx=map("%xmm$_",(8..10));
my @V=($A,$B,$C,$D,$E)=("%eax","%ebx","%ecx","%edx","%ebp");	# size optimization
my @T=("%esi","%edi");
my ($rndkey0,$iv,$in)=map("%xmm$_",(11..13));
my @rndkey=("%xmm14","%xmm15");
my ($inout0,$inout1,$inout2,$inout3)=map("%xmm$_",(12..15));	# for dec
my $Kx=@Tx[2];

my $_rol=sub { &shld(@_[0],@_) };
my $_ror=sub { &shrd(@_[0],@_) };

$code.=<<___;
.type	aesni_cbc_sha1_enc_avx,\@function,6
.align	32
aesni_cbc_sha1_enc_avx:
.cfi_startproc
	mov	`($win64?56:8)`(%rsp),$inp	# load 7th argument
	#shr	\$6,$len			# debugging artefact
	#jz	.Lepilogue_avx			# debugging artefact
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	lea	`-104-($win64?10*16:0)`(%rsp),%rsp
.cfi_adjust_cfa_offset	`104+($win64?10*16:0)`
	#mov	$in0,$inp			# debugging artefact
	#lea	64(%rsp),$ctx			# debugging artefact
___
$code.=<<___ if ($win64);
	movaps	%xmm6,96+0(%rsp)
	movaps	%xmm7,96+16(%rsp)
	movaps	%xmm8,96+32(%rsp)
	movaps	%xmm9,96+48(%rsp)
	movaps	%xmm10,96+64(%rsp)
	movaps	%xmm11,96+80(%rsp)
	movaps	%xmm12,96+96(%rsp)
	movaps	%xmm13,96+112(%rsp)
	movaps	%xmm14,96+128(%rsp)
	movaps	%xmm15,96+144(%rsp)
.Lprologue_avx:
___
$code.=<<___;
	vzeroall
	mov	$in0,%r12			# reassign arguments
	mov	$out,%r13
	mov	$len,%r14
	lea	112($key),%r15			# size optimization
	vmovdqu	($ivp),$iv			# load IV
	mov	$ivp,88(%rsp)			# save $ivp
___
($in0,$out,$len,$key)=map("%r$_",(12..15));	# reassign arguments
my $rounds="${ivp}d";
$code.=<<___;
	shl	\$6,$len
	sub	$in0,$out
	mov	240-112($key),$rounds
	add	$inp,$len		# end of input

	lea	K_XX_XX(%rip),$K_XX_XX
	mov	0($ctx),$A		# load context
	mov	4($ctx),$B
	mov	8($ctx),$C
	mov	12($ctx),$D
	mov	$B,@T[0]		# magic seed
	mov	16($ctx),$E
	mov	$C,@T[1]
	xor	$D,@T[1]
	and	@T[1],@T[0]

	vmovdqa	64($K_XX_XX),@X[2]	# pbswap mask
	vmovdqa	0($K_XX_XX),$Kx		# K_00_19
	vmovdqu	0($inp),@X[-4&7]	# load input to %xmm[0-3]
	vmovdqu	16($inp),@X[-3&7]
	vmovdqu	32($inp),@X[-2&7]
	vmovdqu	48($inp),@X[-1&7]
	vpshufb	@X[2],@X[-4&7],@X[-4&7]	# byte swap
	add	\$64,$inp
	vpshufb	@X[2],@X[-3&7],@X[-3&7]
	vpshufb	@X[2],@X[-2&7],@X[-2&7]
	vpshufb	@X[2],@X[-1&7],@X[-1&7]
	vpaddd	$Kx,@X[-4&7],@X[0]	# add K_00_19
	vpaddd	$Kx,@X[-3&7],@X[1]
	vpaddd	$Kx,@X[-2&7],@X[2]
	vmovdqa	@X[0],0(%rsp)		# X[]+K xfer to IALU
	vmovdqa	@X[1],16(%rsp)
	vmovdqa	@X[2],32(%rsp)
	vmovups	-112($key),$rndkey[1]	# $key[0]
	vmovups	16-112($key),$rndkey[0]	# forward reference
	jmp	.Loop_avx
___

my $aesenc=sub {
  use integer;
  my ($n,$k)=($r/10,$r%10);
    if ($k==0) {
      $code.=<<___;
	vmovdqu		`16*$n`($in0),$in		# load input
	vpxor		$rndkey[1],$in,$in
___
      $code.=<<___ if ($n);
	vmovups		$iv,`16*($n-1)`($out,$in0)	# write output
___
      $code.=<<___;
	vpxor		$in,$iv,$iv
	vaesenc		$rndkey[0],$iv,$iv
	vmovups		`32+16*$k-112`($key),$rndkey[1]
___
    } elsif ($k==9) {
      $sn++;
      $code.=<<___;
	cmp		\$11,$rounds
	jb		.Lvaesenclast$sn
	vaesenc		$rndkey[0],$iv,$iv
	vmovups		`32+16*($k+0)-112`($key),$rndkey[1]
	vaesenc		$rndkey[1],$iv,$iv
	vmovups		`32+16*($k+1)-112`($key),$rndkey[0]
	je		.Lvaesenclast$sn
	vaesenc		$rndkey[0],$iv,$iv
	vmovups		`32+16*($k+2)-112`($key),$rndkey[1]
	vaesenc		$rndkey[1],$iv,$iv
	vmovups		`32+16*($k+3)-112`($key),$rndkey[0]
.Lvaesenclast$sn:
	vaesenclast	$rndkey[0],$iv,$iv
	vmovups		-112($key),$rndkey[0]
	vmovups		16-112($key),$rndkey[1]		# forward reference
___
    } else {
      $code.=<<___;
	vaesenc		$rndkey[0],$iv,$iv
	vmovups		`32+16*$k-112`($key),$rndkey[1]
___
    }
    $r++;	unshift(@rndkey,pop(@rndkey));
};

sub Xupdate_avx_16_31()		# recall that $Xi starts with 4
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 40 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpalignr(@X[0],@X[-3&7],@X[-4&7],8);	# compose "X[-14]" in "X[0]"
	 eval(shift(@insns));
	 eval(shift(@insns));

	  &vpaddd	(@Tx[1],$Kx,@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpsrldq(@Tx[0],@X[-1&7],4);		# "X[-3]", 3 dwords
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpxor	(@X[0],@X[0],@X[-4&7]);		# "X[0]"^="X[-16]"
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@Tx[0],@Tx[0],@X[-2&7]);	# "X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@Tx[0]);		# "X[0]"^="X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &vmovdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpsrld	(@Tx[0],@X[0],31);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpslldq(@Tx[1],@X[0],12);		# "X[0]"<<96, extract one dword
	&vpaddd	(@X[0],@X[0],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpor	(@X[0],@X[0],@Tx[0]);		# "X[0]"<<<=1
	&vpsrld	(@Tx[0],@Tx[1],30);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpslld	(@Tx[1],@Tx[1],2);
	&vpxor	(@X[0],@X[0],@Tx[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@Tx[1]);		# "X[0]"^=("X[0]">>96)<<<2
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &vmovdqa	($Kx,eval(16*(($Xi)/5))."($K_XX_XX)")	if ($Xi%5==0);	# K_XX_XX
	 eval(shift(@insns));
	 eval(shift(@insns));


	 foreach (@insns) { eval; }	# remaining instructions [if any]

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
}

sub Xupdate_avx_32_79()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 to 48 instructions
  my ($a,$b,$c,$d,$e);

	&vpalignr(@Tx[0],@X[-1&7],@X[-2&7],8);	# compose "X[-6]"
	&vpxor	(@X[0],@X[0],@X[-4&7]);		# "X[0]"="X[-32]"^"X[-16]"
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol

	&vpxor	(@X[0],@X[0],@X[-7&7]);		# "X[0]"^="X[-28]"
	 eval(shift(@insns));
	 eval(shift(@insns))	if (@insns[0] !~ /&ro[rl]/);
	  &vpaddd	(@Tx[1],$Kx,@X[-1&7]);
	  &vmovdqa	($Kx,eval(16*($Xi/5))."($K_XX_XX)")	if ($Xi%5==0);
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@Tx[0]);		# "X[0]"^="X[-6]"
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol

	&vpsrld	(@Tx[0],@X[0],30);
	  &vmovdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&vpslld	(@X[0],@X[0],2);
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&vpor	(@X[0],@X[0],@Tx[0]);		# "X[0]"<<<=2
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));

	 foreach (@insns) { eval; }	# remaining instructions

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
}

sub Xuplast_avx_80()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	  &vpaddd	(@Tx[1],$Kx,@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	  &vmovdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer IALU

	 foreach (@insns) { eval; }		# remaining instructions

	&cmp	($inp,$len);
	&je	(shift);

	&vmovdqa(@Tx[1],"64($K_XX_XX)");	# pbswap mask
	&vmovdqa($Kx,"0($K_XX_XX)");		# K_00_19
	&vmovdqu(@X[-4&7],"0($inp)");		# load input
	&vmovdqu(@X[-3&7],"16($inp)");
	&vmovdqu(@X[-2&7],"32($inp)");
	&vmovdqu(@X[-1&7],"48($inp)");
	&vpshufb(@X[-4&7],@X[-4&7],@Tx[1]);	# byte swap
	&add	($inp,64);

  $Xi=0;
}

sub Xloop_avx()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpshufb(@X[($Xi-3)&7],@X[($Xi-3)&7],@Tx[1]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpaddd	(@Tx[0],@X[($Xi-4)&7],$Kx);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vmovdqa(eval(16*$Xi)."(%rsp)",@Tx[0]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));

	foreach (@insns) { eval; }
  $Xi++;
}

sub Xtail_avx()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	foreach (@insns) { eval; }
}

$code.=<<___;
.align	32
.Loop_avx:
___
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_32_79(\&body_00_19);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xuplast_avx_80(\&body_20_39,".Ldone_avx");	# can jump to "done"

				$saved_j=$j; @saved_V=@V;
				$saved_r=$r; @saved_rndkey=@rndkey;

	&Xloop_avx(\&body_20_39);
	&Xloop_avx(\&body_20_39);
	&Xloop_avx(\&body_20_39);

$code.=<<___;
	vmovups	$iv,48($out,$in0)		# write output
	lea	64($in0),$in0

	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	add	12($ctx),$D
	mov	$A,0($ctx)
	add	16($ctx),$E
	mov	@T[0],4($ctx)
	mov	@T[0],$B			# magic seed
	mov	$C,8($ctx)
	mov	$C,@T[1]
	mov	$D,12($ctx)
	xor	$D,@T[1]
	mov	$E,16($ctx)
	and	@T[1],@T[0]
	jmp	.Loop_avx

.Ldone_avx:
___
				$jj=$j=$saved_j; @V=@saved_V;
				$r=$saved_r;     @rndkey=@saved_rndkey;

	&Xtail_avx(\&body_20_39);
	&Xtail_avx(\&body_20_39);
	&Xtail_avx(\&body_20_39);

$code.=<<___;
	vmovups	$iv,48($out,$in0)		# write output
	mov	88(%rsp),$ivp			# restore $ivp

	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	mov	$A,0($ctx)
	add	12($ctx),$D
	mov	@T[0],4($ctx)
	add	16($ctx),$E
	mov	$C,8($ctx)
	mov	$D,12($ctx)
	mov	$E,16($ctx)
	vmovups	$iv,($ivp)			# write IV
	vzeroall
___
$code.=<<___ if ($win64);
	movaps	96+0(%rsp),%xmm6
	movaps	96+16(%rsp),%xmm7
	movaps	96+32(%rsp),%xmm8
	movaps	96+48(%rsp),%xmm9
	movaps	96+64(%rsp),%xmm10
	movaps	96+80(%rsp),%xmm11
	movaps	96+96(%rsp),%xmm12
	movaps	96+112(%rsp),%xmm13
	movaps	96+128(%rsp),%xmm14
	movaps	96+144(%rsp),%xmm15
___
$code.=<<___;
	lea	`104+($win64?10*16:0)`(%rsp),%rsi
.cfi_def_cfa	%rsi,56
	mov	0(%rsi),%r15
.cfi_restore	%r15
	mov	8(%rsi),%r14
.cfi_restore	%r14
	mov	16(%rsi),%r13
.cfi_restore	%r13
	mov	24(%rsi),%r12
.cfi_restore	%r12
	mov	32(%rsi),%rbp
.cfi_restore	%rbp
	mov	40(%rsi),%rbx
.cfi_restore	%rbx
	lea	48(%rsi),%rsp
.cfi_def_cfa	%rsp,8
.Lepilogue_avx:
	ret
.cfi_endproc
.size	aesni_cbc_sha1_enc_avx,.-aesni_cbc_sha1_enc_avx
___

						if ($stitched_decrypt) {{{
# reset
($in0,$out,$len,$key,$ivp,$ctx,$inp)=("%rdi","%rsi","%rdx","%rcx","%r8","%r9","%r10");

$j=$jj=$r=$rx=0;
$Xi=4;

@aes256_dec = (
	'&vpxor	($inout0,$rndkey0,"0x00($in0)");',
	'&vpxor	($inout1,$rndkey0,"0x10($in0)");',
	'&vpxor	($inout2,$rndkey0,"0x20($in0)");',
	'&vpxor	($inout3,$rndkey0,"0x30($in0)");',

	'&vmovups($rndkey0,"16-112($key)");',
	'&vmovups("64(%rsp)",@X[2]);',		# save IV, originally @X[3]
	undef,undef
	);
for ($i=0;$i<13;$i++) {
    push (@aes256_dec,(
	'&vaesdec	($inout0,$inout0,$rndkey0);',
	'&vaesdec	($inout1,$inout1,$rndkey0);',
	'&vaesdec	($inout2,$inout2,$rndkey0);',
	'&vaesdec	($inout3,$inout3,$rndkey0);	&vmovups($rndkey0,"'.(16*($i+2)-112).'($key)");'
	));
    push (@aes256_dec,(undef,undef))	if (($i>=3 && $i<=5) || $i>=11);
    push (@aes256_dec,(undef,undef))	if ($i==5);
}
push(@aes256_dec,(
	'&vaesdeclast	($inout0,$inout0,$rndkey0);	&vmovups(@X[0],"0x00($in0)");',
	'&vaesdeclast	($inout1,$inout1,$rndkey0);	&vmovups(@X[1],"0x10($in0)");',
	'&vaesdeclast	($inout2,$inout2,$rndkey0);	&vmovups(@X[2],"0x20($in0)");',
	'&vaesdeclast	($inout3,$inout3,$rndkey0);	&vmovups(@X[3],"0x30($in0)");',

	'&vxorps	($inout0,$inout0,"64(%rsp)");	&vmovdqu($rndkey0,"-112($key)");',
	'&vxorps	($inout1,$inout1,@X[0]);	&vmovups("0x00($out,$in0)",$inout0);',
	'&vxorps	($inout2,$inout2,@X[1]);	&vmovups("0x10($out,$in0)",$inout1);',
	'&vxorps	($inout3,$inout3,@X[2]);	&vmovups("0x20($out,$in0)",$inout2);',

	'&vmovups	("0x30($out,$in0)",$inout3);'
	));

$code.=<<___;
.type	aesni256_cbc_sha1_dec_avx,\@function,6
.align	32
aesni256_cbc_sha1_dec_avx:
.cfi_startproc
	mov	`($win64?56:8)`(%rsp),$inp	# load 7th argument
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	lea	`-104-($win64?10*16:0)`(%rsp),%rsp
.cfi_adjust_cfa_offset	`104+($win64?10*16:0)`
___
$code.=<<___ if ($win64);
	movaps	%xmm6,96+0(%rsp)
	movaps	%xmm7,96+16(%rsp)
	movaps	%xmm8,96+32(%rsp)
	movaps	%xmm9,96+48(%rsp)
	movaps	%xmm10,96+64(%rsp)
	movaps	%xmm11,96+80(%rsp)
	movaps	%xmm12,96+96(%rsp)
	movaps	%xmm13,96+112(%rsp)
	movaps	%xmm14,96+128(%rsp)
	movaps	%xmm15,96+144(%rsp)
.Lprologue_dec_avx:
___
$code.=<<___;
	vzeroall
	mov	$in0,%r12			# reassign arguments
	mov	$out,%r13
	mov	$len,%r14
	lea	112($key),%r15			# size optimization
	vmovdqu	($ivp),@X[3]			# load IV
___
($in0,$out,$len,$key)=map("%r$_",(12..15));	# reassign arguments
$code.=<<___;
	shl	\$6,$len
	sub	$in0,$out
	add	$inp,$len		# end of input

	lea	K_XX_XX(%rip),$K_XX_XX
	mov	0($ctx),$A		# load context
	mov	4($ctx),$B
	mov	8($ctx),$C
	mov	12($ctx),$D
	mov	$B,@T[0]		# magic seed
	mov	16($ctx),$E
	mov	$C,@T[1]
	xor	$D,@T[1]
	and	@T[1],@T[0]

	vmovdqa	64($K_XX_XX),@X[2]	# pbswap mask
	vmovdqa	0($K_XX_XX),$Kx		# K_00_19
	vmovdqu	0($inp),@X[-4&7]	# load input to %xmm[0-3]
	vmovdqu	16($inp),@X[-3&7]
	vmovdqu	32($inp),@X[-2&7]
	vmovdqu	48($inp),@X[-1&7]
	vpshufb	@X[2],@X[-4&7],@X[-4&7]	# byte swap
	add	\$64,$inp
	vpshufb	@X[2],@X[-3&7],@X[-3&7]
	vpshufb	@X[2],@X[-2&7],@X[-2&7]
	vpshufb	@X[2],@X[-1&7],@X[-1&7]
	vpaddd	$Kx,@X[-4&7],@X[0]	# add K_00_19
	vpaddd	$Kx,@X[-3&7],@X[1]
	vpaddd	$Kx,@X[-2&7],@X[2]
	vmovdqa	@X[0],0(%rsp)		# X[]+K xfer to IALU
	vmovdqa	@X[1],16(%rsp)
	vmovdqa	@X[2],32(%rsp)
	vmovups	-112($key),$rndkey0	# $key[0]
	jmp	.Loop_dec_avx

.align	32
.Loop_dec_avx:
___
	&Xupdate_avx_16_31(\&body_00_19_dec);
	&Xupdate_avx_16_31(\&body_00_19_dec);
	&Xupdate_avx_16_31(\&body_00_19_dec);
	&Xupdate_avx_16_31(\&body_00_19_dec);
	&Xupdate_avx_32_79(\&body_00_19_dec);
	&Xupdate_avx_32_79(\&body_20_39_dec);
	&Xupdate_avx_32_79(\&body_20_39_dec);
	&Xupdate_avx_32_79(\&body_20_39_dec);
	&Xupdate_avx_32_79(\&body_20_39_dec);
	&Xupdate_avx_32_79(\&body_20_39_dec);
	&Xupdate_avx_32_79(\&body_40_59_dec);
	&Xupdate_avx_32_79(\&body_40_59_dec);
	&Xupdate_avx_32_79(\&body_40_59_dec);
	&Xupdate_avx_32_79(\&body_40_59_dec);
	&Xupdate_avx_32_79(\&body_40_59_dec);
	&Xupdate_avx_32_79(\&body_20_39_dec);
	&Xuplast_avx_80(\&body_20_39_dec,".Ldone_dec_avx");	# can jump to "done"

				$saved_j=$j; @saved_V=@V;
				$saved_rx=$rx;

	&Xloop_avx(\&body_20_39_dec);
	&Xloop_avx(\&body_20_39_dec);
	&Xloop_avx(\&body_20_39_dec);

	eval(@aes256_dec[-1]);			# last store
$code.=<<___;
	lea	64($in0),$in0

	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	add	12($ctx),$D
	mov	$A,0($ctx)
	add	16($ctx),$E
	mov	@T[0],4($ctx)
	mov	@T[0],$B			# magic seed
	mov	$C,8($ctx)
	mov	$C,@T[1]
	mov	$D,12($ctx)
	xor	$D,@T[1]
	mov	$E,16($ctx)
	and	@T[1],@T[0]
	jmp	.Loop_dec_avx

.Ldone_dec_avx:
___
				$jj=$j=$saved_j; @V=@saved_V;
				$rx=$saved_rx;

	&Xtail_avx(\&body_20_39_dec);
	&Xtail_avx(\&body_20_39_dec);
	&Xtail_avx(\&body_20_39_dec);

	eval(@aes256_dec[-1]);			# last store
$code.=<<___;

	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	mov	$A,0($ctx)
	add	12($ctx),$D
	mov	@T[0],4($ctx)
	add	16($ctx),$E
	mov	$C,8($ctx)
	mov	$D,12($ctx)
	mov	$E,16($ctx)
	vmovups	@X[3],($ivp)			# write IV
	vzeroall
___
$code.=<<___ if ($win64);
	movaps	96+0(%rsp),%xmm6
	movaps	96+16(%rsp),%xmm7
	movaps	96+32(%rsp),%xmm8
	movaps	96+48(%rsp),%xmm9
	movaps	96+64(%rsp),%xmm10
	movaps	96+80(%rsp),%xmm11
	movaps	96+96(%rsp),%xmm12
	movaps	96+112(%rsp),%xmm13
	movaps	96+128(%rsp),%xmm14
	movaps	96+144(%rsp),%xmm15
___
$code.=<<___;
	lea	`104+($win64?10*16:0)`(%rsp),%rsi
.cfi_def_cfa	%rsi,56
	mov	0(%rsi),%r15
.cfi_restore	%r15
	mov	8(%rsi),%r14
.cfi_restore	%r14
	mov	16(%rsi),%r13
.cfi_restore	%r13
	mov	24(%rsi),%r12
.cfi_restore	%r12
	mov	32(%rsi),%rbp
.cfi_restore	%rbp
	mov	40(%rsi),%rbx
.cfi_restore	%rbx
	lea	48(%rsi),%rsp
.cfi_def_cfa	%rsp,8
.Lepilogue_dec_avx:
	ret
.cfi_endproc
.size	aesni256_cbc_sha1_dec_avx,.-aesni256_cbc_sha1_dec_avx
___
						}}}
}
$code.=<<___;
.align	64
K_XX_XX:
.long	0x5a827999,0x5a827999,0x5a827999,0x5a827999	# K_00_19
.long	0x6ed9eba1,0x6ed9eba1,0x6ed9eba1,0x6ed9eba1	# K_20_39
.long	0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc	# K_40_59
.long	0xca62c1d6,0xca62c1d6,0xca62c1d6,0xca62c1d6	# K_60_79
.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f	# pbswap mask
.byte	0xf,0xe,0xd,0xc,0xb,0xa,0x9,0x8,0x7,0x6,0x5,0x4,0x3,0x2,0x1,0x0

.asciz	"AESNI-CBC+SHA1 stitch for x86_64, CRYPTOGAMS by <appro\@openssl.org>"
.align	64
___
						if ($shaext) {{{
($in0,$out,$len,$key,$ivp,$ctx,$inp)=("%rdi","%rsi","%rdx","%rcx","%r8","%r9","%r10");

$rounds="%r11d";

($iv,$in,$rndkey0)=map("%xmm$_",(2,14,15));
@rndkey=("%xmm0","%xmm1");
$r=0;

my ($BSWAP,$ABCD,$E,$E_,$ABCD_SAVE,$E_SAVE)=map("%xmm$_",(7..12));
my @MSG=map("%xmm$_",(3..6));

$code.=<<___;
.type	aesni_cbc_sha1_enc_shaext,\@function,6
.align	32
aesni_cbc_sha1_enc_shaext:
	mov	`($win64?56:8)`(%rsp),$inp	# load 7th argument
___
$code.=<<___ if ($win64);
	lea	`-8-10*16`(%rsp),%rsp
	movaps	%xmm6,-8-10*16(%rax)
	movaps	%xmm7,-8-9*16(%rax)
	movaps	%xmm8,-8-8*16(%rax)
	movaps	%xmm9,-8-7*16(%rax)
	movaps	%xmm10,-8-6*16(%rax)
	movaps	%xmm11,-8-5*16(%rax)
	movaps	%xmm12,-8-4*16(%rax)
	movaps	%xmm13,-8-3*16(%rax)
	movaps	%xmm14,-8-2*16(%rax)
	movaps	%xmm15,-8-1*16(%rax)
.Lprologue_shaext:
___
$code.=<<___;
	movdqu	($ctx),$ABCD
	movd	16($ctx),$E
	movdqa	K_XX_XX+0x50(%rip),$BSWAP	# byte-n-word swap

	mov	240($key),$rounds
	sub	$in0,$out
	movups	($key),$rndkey0			# $key[0]
	movups	($ivp),$iv			# load IV
	movups	16($key),$rndkey[0]		# forward reference
	lea	112($key),$key			# size optimization

	pshufd	\$0b00011011,$ABCD,$ABCD	# flip word order
	pshufd	\$0b00011011,$E,$E		# flip word order
	jmp	.Loop_shaext

.align	16
.Loop_shaext:
___
	&$aesenc();
$code.=<<___;
	movdqu		($inp),@MSG[0]
	movdqa		$E,$E_SAVE		# offload $E
	pshufb		$BSWAP,@MSG[0]
	movdqu		0x10($inp),@MSG[1]
	movdqa		$ABCD,$ABCD_SAVE	# offload $ABCD
___
	&$aesenc();
$code.=<<___;
	pshufb		$BSWAP,@MSG[1]

	paddd		@MSG[0],$E
	movdqu		0x20($inp),@MSG[2]
	lea		0x40($inp),$inp
	pxor		$E_SAVE,@MSG[0]		# black magic
___
	&$aesenc();
$code.=<<___;
	pxor		$E_SAVE,@MSG[0]		# black magic
	movdqa		$ABCD,$E_
	pshufb		$BSWAP,@MSG[2]
	sha1rnds4	\$0,$E,$ABCD		# 0-3
	sha1nexte	@MSG[1],$E_
___
	&$aesenc();
$code.=<<___;
	sha1msg1	@MSG[1],@MSG[0]
	movdqu		-0x10($inp),@MSG[3]
	movdqa		$ABCD,$E
	pshufb		$BSWAP,@MSG[3]
___
	&$aesenc();
$code.=<<___;
	sha1rnds4	\$0,$E_,$ABCD		# 4-7
	sha1nexte	@MSG[2],$E
	pxor		@MSG[2],@MSG[0]
	sha1msg1	@MSG[2],@MSG[1]
___
	&$aesenc();

for($i=2;$i<20-4;$i++) {
$code.=<<___;
	movdqa		$ABCD,$E_
	sha1rnds4	\$`int($i/5)`,$E,$ABCD	# 8-11
	sha1nexte	@MSG[3],$E_
___
	&$aesenc();
$code.=<<___;
	sha1msg2	@MSG[3],@MSG[0]
	pxor		@MSG[3],@MSG[1]
	sha1msg1	@MSG[3],@MSG[2]
___
	($E,$E_)=($E_,$E);
	push(@MSG,shift(@MSG));

	&$aesenc();
}
$code.=<<___;
	movdqa		$ABCD,$E_
	sha1rnds4	\$3,$E,$ABCD		# 64-67
	sha1nexte	@MSG[3],$E_
	sha1msg2	@MSG[3],@MSG[0]
	pxor		@MSG[3],@MSG[1]
___
	&$aesenc();
$code.=<<___;
	movdqa		$ABCD,$E
	sha1rnds4	\$3,$E_,$ABCD		# 68-71
	sha1nexte	@MSG[0],$E
	sha1msg2	@MSG[0],@MSG[1]
___
	&$aesenc();
$code.=<<___;
	movdqa		$E_SAVE,@MSG[0]
	movdqa		$ABCD,$E_
	sha1rnds4	\$3,$E,$ABCD		# 72-75
	sha1nexte	@MSG[1],$E_
___
	&$aesenc();
$code.=<<___;
	movdqa		$ABCD,$E
	sha1rnds4	\$3,$E_,$ABCD		# 76-79
	sha1nexte	$MSG[0],$E
___
	while($r<40)	{ &$aesenc(); }		# remaining aesenc's
$code.=<<___;
	dec		$len

	paddd		$ABCD_SAVE,$ABCD
	movups		$iv,48($out,$in0)	# write output
	lea		64($in0),$in0
	jnz		.Loop_shaext

	pshufd	\$0b00011011,$ABCD,$ABCD
	pshufd	\$0b00011011,$E,$E
	movups	$iv,($ivp)			# write IV
	movdqu	$ABCD,($ctx)
	movd	$E,16($ctx)
___
$code.=<<___ if ($win64);
	movaps	-8-10*16(%rax),%xmm6
	movaps	-8-9*16(%rax),%xmm7
	movaps	-8-8*16(%rax),%xmm8
	movaps	-8-7*16(%rax),%xmm9
	movaps	-8-6*16(%rax),%xmm10
	movaps	-8-5*16(%rax),%xmm11
	movaps	-8-4*16(%rax),%xmm12
	movaps	-8-3*16(%rax),%xmm13
	movaps	-8-2*16(%rax),%xmm14
	movaps	-8-1*16(%rax),%xmm15
	mov	%rax,%rsp
.Lepilogue_shaext:
___
$code.=<<___;
	ret
.size	aesni_cbc_sha1_enc_shaext,.-aesni_cbc_sha1_enc_shaext
___
						}}}
# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
if ($win64) {
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___;
.extern	__imp_RtlVirtualUnwind
.type	ssse3_handler,\@abi-omnipotent
.align	16
ssse3_handler:
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
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lcommon_seh_tail

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail
___
$code.=<<___ if ($shaext);
	lea	aesni_cbc_sha1_enc_shaext(%rip),%r10
	cmp	%r10,%rbx
	jb	.Lseh_no_shaext

	lea	(%rax),%rsi
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$20,%ecx
	.long	0xa548f3fc		# cld; rep movsq
	lea	168(%rax),%rax		# adjust stack pointer
	jmp	.Lcommon_seh_tail
.Lseh_no_shaext:
___
$code.=<<___;
	lea	96(%rax),%rsi
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$20,%ecx
	.long	0xa548f3fc		# cld; rep movsq
	lea	`104+10*16`(%rax),%rax	# adjust stack pointer

	mov	0(%rax),%r15
	mov	8(%rax),%r14
	mov	16(%rax),%r13
	mov	24(%rax),%r12
	mov	32(%rax),%rbp
	mov	40(%rax),%rbx
	lea	48(%rax),%rax
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12
	mov	%r13,224($context)	# restore context->R13
	mov	%r14,232($context)	# restore context->R14
	mov	%r15,240($context)	# restore context->R15

.Lcommon_seh_tail:
	mov	8(%rax),%rdi
	mov	16(%rax),%rsi
	mov	%rax,152($context)	# restore context->Rsp
	mov	%rsi,168($context)	# restore context->Rsi
	mov	%rdi,176($context)	# restore context->Rdi

	mov	40($disp),%rdi		# disp->ContextRecord
	mov	$context,%rsi		# context
	mov	\$154,%ecx		# sizeof(CONTEXT)
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
.size	ssse3_handler,.-ssse3_handler

.section	.pdata
.align	4
	.rva	.LSEH_begin_aesni_cbc_sha1_enc_ssse3
	.rva	.LSEH_end_aesni_cbc_sha1_enc_ssse3
	.rva	.LSEH_info_aesni_cbc_sha1_enc_ssse3
___
$code.=<<___ if ($avx);
	.rva	.LSEH_begin_aesni_cbc_sha1_enc_avx
	.rva	.LSEH_end_aesni_cbc_sha1_enc_avx
	.rva	.LSEH_info_aesni_cbc_sha1_enc_avx
___
$code.=<<___ if ($shaext);
	.rva	.LSEH_begin_aesni_cbc_sha1_enc_shaext
	.rva	.LSEH_end_aesni_cbc_sha1_enc_shaext
	.rva	.LSEH_info_aesni_cbc_sha1_enc_shaext
___
$code.=<<___;
.section	.xdata
.align	8
.LSEH_info_aesni_cbc_sha1_enc_ssse3:
	.byte	9,0,0,0
	.rva	ssse3_handler
	.rva	.Lprologue_ssse3,.Lepilogue_ssse3	# HandlerData[]
___
$code.=<<___ if ($avx);
.LSEH_info_aesni_cbc_sha1_enc_avx:
	.byte	9,0,0,0
	.rva	ssse3_handler
	.rva	.Lprologue_avx,.Lepilogue_avx		# HandlerData[]
___
$code.=<<___ if ($shaext);
.LSEH_info_aesni_cbc_sha1_enc_shaext:
	.byte	9,0,0,0
	.rva	ssse3_handler
	.rva	.Lprologue_shaext,.Lepilogue_shaext	# HandlerData[]
___
}

####################################################################
sub rex {
  local *opcode=shift;
  my ($dst,$src)=@_;
  my $rex=0;

    $rex|=0x04			if($dst>=8);
    $rex|=0x01			if($src>=8);
    unshift @opcode,$rex|0x40	if($rex);
}

sub sha1rnds4 {
    if (@_[0] =~ /\$([x0-9a-f]+),\s*%xmm([0-9]+),\s*%xmm([0-9]+)/) {
      my @opcode=(0x0f,0x3a,0xcc);
	rex(\@opcode,$3,$2);
	push @opcode,0xc0|($2&7)|(($3&7)<<3);		# ModR/M
	my $c=$1;
	push @opcode,$c=~/^0/?oct($c):$c;
	return ".byte\t".join(',',@opcode);
    } else {
	return "sha1rnds4\t".@_[0];
    }
}

sub sha1op38 {
    my $instr = shift;
    my %opcodelet = (
		"sha1nexte" => 0xc8,
  		"sha1msg1"  => 0xc9,
		"sha1msg2"  => 0xca	);

    if (defined($opcodelet{$instr}) && @_[0] =~ /%xmm([0-9]+),\s*%xmm([0-9]+)/) {
      my @opcode=(0x0f,0x38);
	rex(\@opcode,$2,$1);
	push @opcode,$opcodelet{$instr};
	push @opcode,0xc0|($1&7)|(($2&7)<<3);		# ModR/M
	return ".byte\t".join(',',@opcode);
    } else {
	return $instr."\t".@_[0];
    }
}

sub aesni {
  my $line=shift;
  my @opcode=(0x0f,0x38);

    if ($line=~/(aes[a-z]+)\s+%xmm([0-9]+),\s*%xmm([0-9]+)/) {
	my %opcodelet = (
		"aesenc" => 0xdc,	"aesenclast" => 0xdd,
		"aesdec" => 0xde,	"aesdeclast" => 0xdf
	);
	return undef if (!defined($opcodelet{$1}));
	rex(\@opcode,$3,$2);
	push @opcode,$opcodelet{$1},0xc0|($2&7)|(($3&7)<<3);	# ModR/M
	unshift @opcode,0x66;
	return ".byte\t".join(',',@opcode);
    }
    return $line;
}

foreach (split("\n",$code)) {
        s/\`([^\`]*)\`/eval $1/geo;

	s/\b(sha1rnds4)\s+(.*)/sha1rnds4($2)/geo		or
	s/\b(sha1[^\s]*)\s+(.*)/sha1op38($1,$2)/geo		or
	s/\b(aes.*%xmm[0-9]+).*$/aesni($1)/geo;

	print $_,"\n";
}
close STDOUT;

#! /usr/bin/env perl
# Copyright 2013-2016 The OpenSSL Project Authors. All Rights Reserved.
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
# January 2013
#
# This is AESNI-CBC+SHA256 stitch implementation. The idea, as spelled
# in http://download.intel.com/design/intarch/papers/323686.pdf, is
# that since AESNI-CBC encrypt exhibit *very* low instruction-level
# parallelism, interleaving it with another algorithm would allow to
# utilize processor resources better and achieve better performance.
# SHA256 instruction sequences(*) are taken from sha512-x86_64.pl and
# AESNI code is weaved into it. As SHA256 dominates execution time,
# stitch performance does not depend on AES key length. Below are
# performance numbers in cycles per processed byte, less is better,
# for standalone AESNI-CBC encrypt, standalone SHA256, and stitched
# subroutine:
#
#		 AES-128/-192/-256+SHA256   this(**)	gain
# Sandy Bridge	    5.05/6.05/7.05+11.6	    13.0	+28%/36%/43%
# Ivy Bridge	    5.05/6.05/7.05+10.3	    11.6	+32%/41%/50%
# Haswell	    4.43/5.29/6.19+7.80	    8.79	+39%/49%/59%
# Skylake	    2.62/3.14/3.62+7.70	    8.10	+27%/34%/40%
# Bulldozer	    5.77/6.89/8.00+13.7	    13.7	+42%/50%/58%
# Ryzen(***)	    2.71/-/3.71+2.05	    2.74/-/3.73	+74%/-/54%
# Goldmont(***)	    3.82/-/5.35+4.16	    4.73/-/5.94	+69%/-/60%
#
# (*)	there are XOP, AVX1 and AVX2 code paths, meaning that
#	Westmere is omitted from loop, this is because gain was not
#	estimated high enough to justify the effort;
# (**)	these are EVP-free results, results obtained with 'speed
#	-evp aes-256-cbc-hmac-sha256' will vary by percent or two;
# (***)	these are SHAEXT results;

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

if (`$ENV{CC} -Wa,-v -c -o /dev/null -x assembler /dev/null 2>&1`
		=~ /GNU assembler version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.19) + ($1>=2.22);
}

if (!$avx && $win64 && ($flavour =~ /nasm/ || $ENV{ASM} =~ /nasm/) &&
	   `nasm -v 2>&1` =~ /NASM version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.09) + ($1>=2.10);
}

if (!$avx && $win64 && ($flavour =~ /masm/ || $ENV{ASM} =~ /ml64/) &&
	   `ml64 2>&1` =~ /Version ([0-9]+)\./) {
	$avx = ($1>=10) + ($1>=12);
}

if (!$avx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|.*based on LLVM) ([3-9]\.[0-9]+)/) {
	$avx = ($2>=3.0) + ($2>3.0);
}

$shaext=$avx;	### set to zero if compiling for 1.0.1
$avx=1		if (!$shaext && $avx);

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

$func="aesni_cbc_sha256_enc";
$TABLE="K256";
$SZ=4;
@ROT=($A,$B,$C,$D,$E,$F,$G,$H)=("%eax","%ebx","%ecx","%edx",
				"%r8d","%r9d","%r10d","%r11d");
($T1,$a0,$a1,$a2,$a3)=("%r12d","%r13d","%r14d","%r15d","%esi");
@Sigma0=( 2,13,22);
@Sigma1=( 6,11,25);
@sigma0=( 7,18, 3);
@sigma1=(17,19,10);
$rounds=64;

########################################################################
# void aesni_cbc_sha256_enc(const void *inp,
#			void *out,
#			size_t length,
#			const AES_KEY *key,
#			unsigned char *iv,
#			SHA256_CTX *ctx,
#			const void *in0);
($inp,  $out,  $len,  $key,  $ivp, $ctx, $in0) =
("%rdi","%rsi","%rdx","%rcx","%r8","%r9","%r10");

$Tbl="%rbp";

$_inp="16*$SZ+0*8(%rsp)";
$_out="16*$SZ+1*8(%rsp)";
$_end="16*$SZ+2*8(%rsp)";
$_key="16*$SZ+3*8(%rsp)";
$_ivp="16*$SZ+4*8(%rsp)";
$_ctx="16*$SZ+5*8(%rsp)";
$_in0="16*$SZ+6*8(%rsp)";
$_rsp="`16*$SZ+7*8`(%rsp)";
$framesz=16*$SZ+8*8;

$code=<<___;
.text

.extern	OPENSSL_ia32cap_P
.globl	$func
.type	$func,\@abi-omnipotent
.align	16
$func:
___
						if ($avx) {
$code.=<<___;
	lea	OPENSSL_ia32cap_P(%rip),%r11
	mov	\$1,%eax
	cmp	\$0,`$win64?"%rcx":"%rdi"`
	je	.Lprobe
	mov	0(%r11),%eax
	mov	4(%r11),%r10
___
$code.=<<___ if ($shaext);
	bt	\$61,%r10			# check for SHA
	jc	${func}_shaext
___
$code.=<<___;
	mov	%r10,%r11
	shr	\$32,%r11

	test	\$`1<<11`,%r10d			# check for XOP
	jnz	${func}_xop
___
$code.=<<___ if ($avx>1);
	and	\$`1<<8|1<<5|1<<3`,%r11d	# check for BMI2+AVX2+BMI1
	cmp	\$`1<<8|1<<5|1<<3`,%r11d
	je	${func}_avx2
___
$code.=<<___;
	and	\$`1<<28`,%r10d			# check for AVX
	jnz	${func}_avx
	ud2
___
						}
$code.=<<___;
	xor	%eax,%eax
	cmp	\$0,`$win64?"%rcx":"%rdi"`
	je	.Lprobe
	ud2
.Lprobe:
	ret
.size	$func,.-$func

.align	64
.type	$TABLE,\@object
$TABLE:
	.long	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	.long	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	.long	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	.long	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	.long	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	.long	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	.long	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	.long	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	.long	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	.long	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	.long	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	.long	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	.long	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	.long	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	.long	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	.long	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	.long	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	.long	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	.long	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	.long	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	.long	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	.long	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	.long	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	.long	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	.long	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	.long	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	.long	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	.long	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	.long	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	.long	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	.long	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
	.long	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2

	.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f
	.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f
	.long	0,0,0,0,   0,0,0,0,   -1,-1,-1,-1
	.long	0,0,0,0,   0,0,0,0
	.asciz	"AESNI-CBC+SHA256 stitch for x86_64, CRYPTOGAMS by <appro\@openssl.org>"
.align	64
___

######################################################################
# SIMD code paths
#
{{{
($iv,$inout,$roundkey,$temp,
 $mask10,$mask12,$mask14,$offload)=map("%xmm$_",(8..15));

$aesni_cbc_idx=0;
@aesni_cbc_block = (
##	&vmovdqu	($roundkey,"0x00-0x80($inp)");'
##	&vmovdqu	($inout,($inp));
##	&mov		($_inp,$inp);

	'&vpxor		($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0x10-0x80($inp)");',

	'&vpxor		($inout,$inout,$iv);',

	'&vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0x20-0x80($inp)");',

	'&vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0x30-0x80($inp)");',

	'&vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0x40-0x80($inp)");',

	'&vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0x50-0x80($inp)");',

	'&vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0x60-0x80($inp)");',

	'&vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0x70-0x80($inp)");',

	'&vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0x80-0x80($inp)");',

	'&vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0x90-0x80($inp)");',

	'&vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0xa0-0x80($inp)");',

	'&vaesenclast	($temp,$inout,$roundkey);'.
	' &vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0xb0-0x80($inp)");',

	'&vpand		($iv,$temp,$mask10);'.
	' &vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0xc0-0x80($inp)");',

	'&vaesenclast	($temp,$inout,$roundkey);'.
	' &vaesenc	($inout,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0xd0-0x80($inp)");',

	'&vpand		($temp,$temp,$mask12);'.
	' &vaesenc	($inout,$inout,$roundkey);'.
	 '&vmovdqu	($roundkey,"0xe0-0x80($inp)");',

	'&vpor		($iv,$iv,$temp);'.
	' &vaesenclast	($temp,$inout,$roundkey);'.
	' &vmovdqu	($roundkey,"0x00-0x80($inp)");'

##	&mov		($inp,$_inp);
##	&mov		($out,$_out);
##	&vpand		($temp,$temp,$mask14);
##	&vpor		($iv,$iv,$temp);
##	&vmovdqu	($iv,($out,$inp);
##	&lea		(inp,16($inp));
);

my $a4=$T1;
my ($a,$b,$c,$d,$e,$f,$g,$h);

sub AUTOLOAD()		# thunk [simplified] 32-bit style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://;
  my $arg = pop;
    $arg = "\$$arg" if ($arg*1 eq $arg);
    $code .= "\t$opcode\t".join(',',$arg,reverse @_)."\n";
}

sub body_00_15 () {
	(
	'($a,$b,$c,$d,$e,$f,$g,$h)=@ROT;'.

	'&ror	($a0,$Sigma1[2]-$Sigma1[1])',
	'&mov	($a,$a1)',
	'&mov	($a4,$f)',

	'&xor	($a0,$e)',
	'&ror	($a1,$Sigma0[2]-$Sigma0[1])',
	'&xor	($a4,$g)',			# f^g

	'&ror	($a0,$Sigma1[1]-$Sigma1[0])',
	'&xor	($a1,$a)',
	'&and	($a4,$e)',			# (f^g)&e

	@aesni_cbc_block[$aesni_cbc_idx++].
	'&xor	($a0,$e)',
	'&add	($h,$SZ*($i&15)."(%rsp)")',	# h+=X[i]+K[i]
	'&mov	($a2,$a)',

	'&ror	($a1,$Sigma0[1]-$Sigma0[0])',
	'&xor	($a4,$g)',			# Ch(e,f,g)=((f^g)&e)^g
	'&xor	($a2,$b)',			# a^b, b^c in next round

	'&ror	($a0,$Sigma1[0])',		# Sigma1(e)
	'&add	($h,$a4)',			# h+=Ch(e,f,g)
	'&and	($a3,$a2)',			# (b^c)&(a^b)

	'&xor	($a1,$a)',
	'&add	($h,$a0)',			# h+=Sigma1(e)
	'&xor	($a3,$b)',			# Maj(a,b,c)=Ch(a^b,c,b)

	'&add	($d,$h)',			# d+=h
	'&ror	($a1,$Sigma0[0])',		# Sigma0(a)
	'&add	($h,$a3)',			# h+=Maj(a,b,c)

	'&mov	($a0,$d)',
	'&add	($a1,$h);'.			# h+=Sigma0(a)
	'($a2,$a3) = ($a3,$a2); unshift(@ROT,pop(@ROT)); $i++;'
	);
}

if ($avx) {{
######################################################################
# XOP code path
#
$code.=<<___;
.type	${func}_xop,\@function,6
.align	64
${func}_xop:
.cfi_startproc
.Lxop_shortcut:
	mov	`($win64?56:8)`(%rsp),$in0	# load 7th parameter
	mov	%rsp,%rax		# copy %rsp
.cfi_def_cfa_register	%rax
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
	sub	\$`$framesz+$win64*16*10`,%rsp
	and	\$-64,%rsp		# align stack frame

	shl	\$6,$len
	sub	$inp,$out		# re-bias
	sub	$inp,$in0
	add	$inp,$len		# end of input

	#mov	$inp,$_inp		# saved later
	mov	$out,$_out
	mov	$len,$_end
	#mov	$key,$_key		# remains resident in $inp register
	mov	$ivp,$_ivp
	mov	$ctx,$_ctx
	mov	$in0,$_in0
	mov	%rax,$_rsp
.cfi_cfa_expression	$_rsp,deref,+8
___
$code.=<<___ if ($win64);
	movaps	%xmm6,`$framesz+16*0`(%rsp)
	movaps	%xmm7,`$framesz+16*1`(%rsp)
	movaps	%xmm8,`$framesz+16*2`(%rsp)
	movaps	%xmm9,`$framesz+16*3`(%rsp)
	movaps	%xmm10,`$framesz+16*4`(%rsp)
	movaps	%xmm11,`$framesz+16*5`(%rsp)
	movaps	%xmm12,`$framesz+16*6`(%rsp)
	movaps	%xmm13,`$framesz+16*7`(%rsp)
	movaps	%xmm14,`$framesz+16*8`(%rsp)
	movaps	%xmm15,`$framesz+16*9`(%rsp)
___
$code.=<<___;
.Lprologue_xop:
	vzeroall

	mov	$inp,%r12		# borrow $a4
	lea	0x80($key),$inp		# size optimization, reassign
	lea	$TABLE+`$SZ*2*$rounds+32`(%rip),%r13	# borrow $a0
	mov	0xf0-0x80($inp),%r14d	# rounds, borrow $a1
	mov	$ctx,%r15		# borrow $a2
	mov	$in0,%rsi		# borrow $a3
	vmovdqu	($ivp),$iv		# load IV
	sub	\$9,%r14

	mov	$SZ*0(%r15),$A
	mov	$SZ*1(%r15),$B
	mov	$SZ*2(%r15),$C
	mov	$SZ*3(%r15),$D
	mov	$SZ*4(%r15),$E
	mov	$SZ*5(%r15),$F
	mov	$SZ*6(%r15),$G
	mov	$SZ*7(%r15),$H

	vmovdqa	0x00(%r13,%r14,8),$mask14
	vmovdqa	0x10(%r13,%r14,8),$mask12
	vmovdqa	0x20(%r13,%r14,8),$mask10
	vmovdqu	0x00-0x80($inp),$roundkey
	jmp	.Lloop_xop
___
					if ($SZ==4) {	# SHA256
    my @X = map("%xmm$_",(0..3));
    my ($t0,$t1,$t2,$t3) = map("%xmm$_",(4..7));

$code.=<<___;
.align	16
.Lloop_xop:
	vmovdqa	$TABLE+`$SZ*2*$rounds`(%rip),$t3
	vmovdqu	0x00(%rsi,%r12),@X[0]
	vmovdqu	0x10(%rsi,%r12),@X[1]
	vmovdqu	0x20(%rsi,%r12),@X[2]
	vmovdqu	0x30(%rsi,%r12),@X[3]
	vpshufb	$t3,@X[0],@X[0]
	lea	$TABLE(%rip),$Tbl
	vpshufb	$t3,@X[1],@X[1]
	vpshufb	$t3,@X[2],@X[2]
	vpaddd	0x00($Tbl),@X[0],$t0
	vpshufb	$t3,@X[3],@X[3]
	vpaddd	0x20($Tbl),@X[1],$t1
	vpaddd	0x40($Tbl),@X[2],$t2
	vpaddd	0x60($Tbl),@X[3],$t3
	vmovdqa	$t0,0x00(%rsp)
	mov	$A,$a1
	vmovdqa	$t1,0x10(%rsp)
	mov	$B,$a3
	vmovdqa	$t2,0x20(%rsp)
	xor	$C,$a3			# magic
	vmovdqa	$t3,0x30(%rsp)
	mov	$E,$a0
	jmp	.Lxop_00_47

.align	16
.Lxop_00_47:
	sub	\$-16*2*$SZ,$Tbl	# size optimization
	vmovdqu	(%r12),$inout		# $a4
	mov	%r12,$_inp		# $a4
___
sub XOP_256_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body,&$body,&$body);	# 104 instructions

	&vpalignr	($t0,@X[1],@X[0],$SZ);	# X[1..4]
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpalignr	($t3,@X[3],@X[2],$SZ);	# X[9..12]
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vprotd		($t1,$t0,8*$SZ-$sigma0[1]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpsrld		($t0,$t0,$sigma0[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpaddd	(@X[0],@X[0],$t3);	# X[0..3] += X[9..12]
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vprotd		($t2,$t1,$sigma0[1]-$sigma0[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpxor		($t0,$t0,$t1);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vprotd	($t3,@X[3],8*$SZ-$sigma1[1]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpxor		($t0,$t0,$t2);		# sigma0(X[1..4])
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpsrld	($t2,@X[3],$sigma1[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddd		(@X[0],@X[0],$t0);	# X[0..3] += sigma0(X[1..4])
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vprotd	($t1,$t3,$sigma1[1]-$sigma1[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpxor		($t3,$t3,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpxor		($t3,$t3,$t1);		# sigma1(X[14..15])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpsrldq	($t3,$t3,8);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddd		(@X[0],@X[0],$t3);	# X[0..1] += sigma1(X[14..15])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vprotd	($t3,@X[0],8*$SZ-$sigma1[1]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpsrld	($t2,@X[0],$sigma1[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vprotd	($t1,$t3,$sigma1[1]-$sigma1[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpxor		($t3,$t3,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpxor		($t3,$t3,$t1);		# sigma1(X[16..17])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpslldq	($t3,$t3,8);		# 22 instructions
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddd		(@X[0],@X[0],$t3);	# X[2..3] += sigma1(X[16..17])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddd		($t2,@X[0],16*2*$j."($Tbl)");
	  foreach (@insns) { eval; }		# remaining instructions
	&vmovdqa	(16*$j."(%rsp)",$t2);
}

    $aesni_cbc_idx=0;
    for ($i=0,$j=0; $j<4; $j++) {
	&XOP_256_00_47($j,\&body_00_15,@X);
	push(@X,shift(@X));			# rotate(@X)
    }
    	&mov		("%r12",$_inp);		# borrow $a4
	&vpand		($temp,$temp,$mask14);
	&mov		("%r15",$_out);		# borrow $a2
	&vpor		($iv,$iv,$temp);
	&vmovdqu	("(%r15,%r12)",$iv);	# write output
	&lea		("%r12","16(%r12)");	# inp++

	&cmpb	($SZ-1+16*2*$SZ."($Tbl)",0);
	&jne	(".Lxop_00_47");

	&vmovdqu	($inout,"(%r12)");
	&mov		($_inp,"%r12");

    $aesni_cbc_idx=0;
    for ($i=0; $i<16; ) {
	foreach(body_00_15()) { eval; }
    }
					}
$code.=<<___;
	mov	$_inp,%r12		# borrow $a4
	mov	$_out,%r13		# borrow $a0
	mov	$_ctx,%r15		# borrow $a2
	mov	$_in0,%rsi		# borrow $a3

	vpand	$mask14,$temp,$temp
	mov	$a1,$A
	vpor	$temp,$iv,$iv
	vmovdqu	$iv,(%r13,%r12)		# write output
	lea	16(%r12),%r12		# inp++

	add	$SZ*0(%r15),$A
	add	$SZ*1(%r15),$B
	add	$SZ*2(%r15),$C
	add	$SZ*3(%r15),$D
	add	$SZ*4(%r15),$E
	add	$SZ*5(%r15),$F
	add	$SZ*6(%r15),$G
	add	$SZ*7(%r15),$H

	cmp	$_end,%r12

	mov	$A,$SZ*0(%r15)
	mov	$B,$SZ*1(%r15)
	mov	$C,$SZ*2(%r15)
	mov	$D,$SZ*3(%r15)
	mov	$E,$SZ*4(%r15)
	mov	$F,$SZ*5(%r15)
	mov	$G,$SZ*6(%r15)
	mov	$H,$SZ*7(%r15)

	jb	.Lloop_xop

	mov	$_ivp,$ivp
	mov	$_rsp,%rsi
.cfi_def_cfa	%rsi,8
	vmovdqu	$iv,($ivp)		# output IV
	vzeroall
___
$code.=<<___ if ($win64);
	movaps	`$framesz+16*0`(%rsp),%xmm6
	movaps	`$framesz+16*1`(%rsp),%xmm7
	movaps	`$framesz+16*2`(%rsp),%xmm8
	movaps	`$framesz+16*3`(%rsp),%xmm9
	movaps	`$framesz+16*4`(%rsp),%xmm10
	movaps	`$framesz+16*5`(%rsp),%xmm11
	movaps	`$framesz+16*6`(%rsp),%xmm12
	movaps	`$framesz+16*7`(%rsp),%xmm13
	movaps	`$framesz+16*8`(%rsp),%xmm14
	movaps	`$framesz+16*9`(%rsp),%xmm15
___
$code.=<<___;
	mov	-48(%rsi),%r15
.cfi_restore	%r15
	mov	-40(%rsi),%r14
.cfi_restore	%r14
	mov	-32(%rsi),%r13
.cfi_restore	%r13
	mov	-24(%rsi),%r12
.cfi_restore	%r12
	mov	-16(%rsi),%rbp
.cfi_restore	%rbp
	mov	-8(%rsi),%rbx
.cfi_restore	%rbx
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_xop:
	ret
.cfi_endproc
.size	${func}_xop,.-${func}_xop
___
######################################################################
# AVX+shrd code path
#
local *ror = sub { &shrd(@_[0],@_) };

$code.=<<___;
.type	${func}_avx,\@function,6
.align	64
${func}_avx:
.cfi_startproc
.Lavx_shortcut:
	mov	`($win64?56:8)`(%rsp),$in0	# load 7th parameter
	mov	%rsp,%rax		# copy %rsp
.cfi_def_cfa_register	%rax
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
	sub	\$`$framesz+$win64*16*10`,%rsp
	and	\$-64,%rsp		# align stack frame

	shl	\$6,$len
	sub	$inp,$out		# re-bias
	sub	$inp,$in0
	add	$inp,$len		# end of input

	#mov	$inp,$_inp		# saved later
	mov	$out,$_out
	mov	$len,$_end
	#mov	$key,$_key		# remains resident in $inp register
	mov	$ivp,$_ivp
	mov	$ctx,$_ctx
	mov	$in0,$_in0
	mov	%rax,$_rsp
.cfi_cfa_expression	$_rsp,deref,+8
___
$code.=<<___ if ($win64);
	movaps	%xmm6,`$framesz+16*0`(%rsp)
	movaps	%xmm7,`$framesz+16*1`(%rsp)
	movaps	%xmm8,`$framesz+16*2`(%rsp)
	movaps	%xmm9,`$framesz+16*3`(%rsp)
	movaps	%xmm10,`$framesz+16*4`(%rsp)
	movaps	%xmm11,`$framesz+16*5`(%rsp)
	movaps	%xmm12,`$framesz+16*6`(%rsp)
	movaps	%xmm13,`$framesz+16*7`(%rsp)
	movaps	%xmm14,`$framesz+16*8`(%rsp)
	movaps	%xmm15,`$framesz+16*9`(%rsp)
___
$code.=<<___;
.Lprologue_avx:
	vzeroall

	mov	$inp,%r12		# borrow $a4
	lea	0x80($key),$inp		# size optimization, reassign
	lea	$TABLE+`$SZ*2*$rounds+32`(%rip),%r13	# borrow $a0
	mov	0xf0-0x80($inp),%r14d	# rounds, borrow $a1
	mov	$ctx,%r15		# borrow $a2
	mov	$in0,%rsi		# borrow $a3
	vmovdqu	($ivp),$iv		# load IV
	sub	\$9,%r14

	mov	$SZ*0(%r15),$A
	mov	$SZ*1(%r15),$B
	mov	$SZ*2(%r15),$C
	mov	$SZ*3(%r15),$D
	mov	$SZ*4(%r15),$E
	mov	$SZ*5(%r15),$F
	mov	$SZ*6(%r15),$G
	mov	$SZ*7(%r15),$H

	vmovdqa	0x00(%r13,%r14,8),$mask14
	vmovdqa	0x10(%r13,%r14,8),$mask12
	vmovdqa	0x20(%r13,%r14,8),$mask10
	vmovdqu	0x00-0x80($inp),$roundkey
___
					if ($SZ==4) {	# SHA256
    my @X = map("%xmm$_",(0..3));
    my ($t0,$t1,$t2,$t3) = map("%xmm$_",(4..7));

$code.=<<___;
	jmp	.Lloop_avx
.align	16
.Lloop_avx:
	vmovdqa	$TABLE+`$SZ*2*$rounds`(%rip),$t3
	vmovdqu	0x00(%rsi,%r12),@X[0]
	vmovdqu	0x10(%rsi,%r12),@X[1]
	vmovdqu	0x20(%rsi,%r12),@X[2]
	vmovdqu	0x30(%rsi,%r12),@X[3]
	vpshufb	$t3,@X[0],@X[0]
	lea	$TABLE(%rip),$Tbl
	vpshufb	$t3,@X[1],@X[1]
	vpshufb	$t3,@X[2],@X[2]
	vpaddd	0x00($Tbl),@X[0],$t0
	vpshufb	$t3,@X[3],@X[3]
	vpaddd	0x20($Tbl),@X[1],$t1
	vpaddd	0x40($Tbl),@X[2],$t2
	vpaddd	0x60($Tbl),@X[3],$t3
	vmovdqa	$t0,0x00(%rsp)
	mov	$A,$a1
	vmovdqa	$t1,0x10(%rsp)
	mov	$B,$a3
	vmovdqa	$t2,0x20(%rsp)
	xor	$C,$a3			# magic
	vmovdqa	$t3,0x30(%rsp)
	mov	$E,$a0
	jmp	.Lavx_00_47

.align	16
.Lavx_00_47:
	sub	\$-16*2*$SZ,$Tbl	# size optimization
	vmovdqu	(%r12),$inout		# $a4
	mov	%r12,$_inp		# $a4
___
sub Xupdate_256_AVX () {
	(
	'&vpalignr	($t0,@X[1],@X[0],$SZ)',	# X[1..4]
	 '&vpalignr	($t3,@X[3],@X[2],$SZ)',	# X[9..12]
	'&vpsrld	($t2,$t0,$sigma0[0]);',
	 '&vpaddd	(@X[0],@X[0],$t3)',	# X[0..3] += X[9..12]
	'&vpsrld	($t3,$t0,$sigma0[2])',
	'&vpslld	($t1,$t0,8*$SZ-$sigma0[1]);',
	'&vpxor		($t0,$t3,$t2)',
	 '&vpshufd	($t3,@X[3],0b11111010)',# X[14..15]
	'&vpsrld	($t2,$t2,$sigma0[1]-$sigma0[0]);',
	'&vpxor		($t0,$t0,$t1)',
	'&vpslld	($t1,$t1,$sigma0[1]-$sigma0[0]);',
	'&vpxor		($t0,$t0,$t2)',
	 '&vpsrld	($t2,$t3,$sigma1[2]);',
	'&vpxor		($t0,$t0,$t1)',		# sigma0(X[1..4])
	 '&vpsrlq	($t3,$t3,$sigma1[0]);',
	'&vpaddd	(@X[0],@X[0],$t0)',	# X[0..3] += sigma0(X[1..4])
	 '&vpxor	($t2,$t2,$t3);',
	 '&vpsrlq	($t3,$t3,$sigma1[1]-$sigma1[0])',
	 '&vpxor	($t2,$t2,$t3)',		# sigma1(X[14..15])
	 '&vpshufd	($t2,$t2,0b10000100)',
	 '&vpsrldq	($t2,$t2,8)',
	'&vpaddd	(@X[0],@X[0],$t2)',	# X[0..1] += sigma1(X[14..15])
	 '&vpshufd	($t3,@X[0],0b01010000)',# X[16..17]
	 '&vpsrld	($t2,$t3,$sigma1[2])',
	 '&vpsrlq	($t3,$t3,$sigma1[0])',
	 '&vpxor	($t2,$t2,$t3);',
	 '&vpsrlq	($t3,$t3,$sigma1[1]-$sigma1[0])',
	 '&vpxor	($t2,$t2,$t3)',
	 '&vpshufd	($t2,$t2,0b11101000)',
	 '&vpslldq	($t2,$t2,8)',
	'&vpaddd	(@X[0],@X[0],$t2)'	# X[2..3] += sigma1(X[16..17])
	);
}

sub AVX_256_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body,&$body,&$body);	# 104 instructions

	foreach (Xupdate_256_AVX()) {		# 29 instructions
	    eval;
	    eval(shift(@insns));
	    eval(shift(@insns));
	    eval(shift(@insns));
	}
	&vpaddd		($t2,@X[0],16*2*$j."($Tbl)");
	  foreach (@insns) { eval; }		# remaining instructions
	&vmovdqa	(16*$j."(%rsp)",$t2);
}

    $aesni_cbc_idx=0;
    for ($i=0,$j=0; $j<4; $j++) {
	&AVX_256_00_47($j,\&body_00_15,@X);
	push(@X,shift(@X));			# rotate(@X)
    }
    	&mov		("%r12",$_inp);		# borrow $a4
	&vpand		($temp,$temp,$mask14);
	&mov		("%r15",$_out);		# borrow $a2
	&vpor		($iv,$iv,$temp);
	&vmovdqu	("(%r15,%r12)",$iv);	# write output
	&lea		("%r12","16(%r12)");	# inp++

	&cmpb	($SZ-1+16*2*$SZ."($Tbl)",0);
	&jne	(".Lavx_00_47");

	&vmovdqu	($inout,"(%r12)");
	&mov		($_inp,"%r12");

    $aesni_cbc_idx=0;
    for ($i=0; $i<16; ) {
	foreach(body_00_15()) { eval; }
    }

					}
$code.=<<___;
	mov	$_inp,%r12		# borrow $a4
	mov	$_out,%r13		# borrow $a0
	mov	$_ctx,%r15		# borrow $a2
	mov	$_in0,%rsi		# borrow $a3

	vpand	$mask14,$temp,$temp
	mov	$a1,$A
	vpor	$temp,$iv,$iv
	vmovdqu	$iv,(%r13,%r12)		# write output
	lea	16(%r12),%r12		# inp++

	add	$SZ*0(%r15),$A
	add	$SZ*1(%r15),$B
	add	$SZ*2(%r15),$C
	add	$SZ*3(%r15),$D
	add	$SZ*4(%r15),$E
	add	$SZ*5(%r15),$F
	add	$SZ*6(%r15),$G
	add	$SZ*7(%r15),$H

	cmp	$_end,%r12

	mov	$A,$SZ*0(%r15)
	mov	$B,$SZ*1(%r15)
	mov	$C,$SZ*2(%r15)
	mov	$D,$SZ*3(%r15)
	mov	$E,$SZ*4(%r15)
	mov	$F,$SZ*5(%r15)
	mov	$G,$SZ*6(%r15)
	mov	$H,$SZ*7(%r15)
	jb	.Lloop_avx

	mov	$_ivp,$ivp
	mov	$_rsp,%rsi
.cfi_def_cfa	%rsi,8
	vmovdqu	$iv,($ivp)		# output IV
	vzeroall
___
$code.=<<___ if ($win64);
	movaps	`$framesz+16*0`(%rsp),%xmm6
	movaps	`$framesz+16*1`(%rsp),%xmm7
	movaps	`$framesz+16*2`(%rsp),%xmm8
	movaps	`$framesz+16*3`(%rsp),%xmm9
	movaps	`$framesz+16*4`(%rsp),%xmm10
	movaps	`$framesz+16*5`(%rsp),%xmm11
	movaps	`$framesz+16*6`(%rsp),%xmm12
	movaps	`$framesz+16*7`(%rsp),%xmm13
	movaps	`$framesz+16*8`(%rsp),%xmm14
	movaps	`$framesz+16*9`(%rsp),%xmm15
___
$code.=<<___;
	mov	-48(%rsi),%r15
.cfi_restore	%r15
	mov	-40(%rsi),%r14
.cfi_restore	%r14
	mov	-32(%rsi),%r13
.cfi_restore	%r13
	mov	-24(%rsi),%r12
.cfi_restore	%r12
	mov	-16(%rsi),%rbp
.cfi_restore	%rbp
	mov	-8(%rsi),%rbx
.cfi_restore	%rbx
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_avx:
	ret
.cfi_endproc
.size	${func}_avx,.-${func}_avx
___

if ($avx>1) {{
######################################################################
# AVX2+BMI code path
#
my $a5=$SZ==4?"%esi":"%rsi";	# zap $inp
my $PUSH8=8*2*$SZ;
use integer;

sub bodyx_00_15 () {
	# at start $a1 should be zero, $a3 - $b^$c and $a4 copy of $f
	(
	'($a,$b,$c,$d,$e,$f,$g,$h)=@ROT;'.

	'&add	($h,(32*($i/(16/$SZ))+$SZ*($i%(16/$SZ)))%$PUSH8.$base)',    # h+=X[i]+K[i]
	'&and	($a4,$e)',		# f&e
	'&rorx	($a0,$e,$Sigma1[2])',
	'&rorx	($a2,$e,$Sigma1[1])',

	'&lea	($a,"($a,$a1)")',	# h+=Sigma0(a) from the past
	'&lea	($h,"($h,$a4)")',
	'&andn	($a4,$e,$g)',		# ~e&g
	'&xor	($a0,$a2)',

	'&rorx	($a1,$e,$Sigma1[0])',
	'&lea	($h,"($h,$a4)")',	# h+=Ch(e,f,g)=(e&f)+(~e&g)
	'&xor	($a0,$a1)',		# Sigma1(e)
	'&mov	($a2,$a)',

	'&rorx	($a4,$a,$Sigma0[2])',
	'&lea	($h,"($h,$a0)")',	# h+=Sigma1(e)
	'&xor	($a2,$b)',		# a^b, b^c in next round
	'&rorx	($a1,$a,$Sigma0[1])',

	'&rorx	($a0,$a,$Sigma0[0])',
	'&lea	($d,"($d,$h)")',	# d+=h
	'&and	($a3,$a2)',		# (b^c)&(a^b)
	@aesni_cbc_block[$aesni_cbc_idx++].
	'&xor	($a1,$a4)',

	'&xor	($a3,$b)',		# Maj(a,b,c)=Ch(a^b,c,b)
	'&xor	($a1,$a0)',		# Sigma0(a)
	'&lea	($h,"($h,$a3)");'.	# h+=Maj(a,b,c)
	'&mov	($a4,$e)',		# copy of f in future

	'($a2,$a3) = ($a3,$a2); unshift(@ROT,pop(@ROT)); $i++;'
	);
	# and at the finish one has to $a+=$a1
}

$code.=<<___;
.type	${func}_avx2,\@function,6
.align	64
${func}_avx2:
.cfi_startproc
.Lavx2_shortcut:
	mov	`($win64?56:8)`(%rsp),$in0	# load 7th parameter
	mov	%rsp,%rax		# copy %rsp
.cfi_def_cfa_register	%rax
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
	sub	\$`2*$SZ*$rounds+8*8+$win64*16*10`,%rsp
	and	\$-256*$SZ,%rsp		# align stack frame
	add	\$`2*$SZ*($rounds-8)`,%rsp

	shl	\$6,$len
	sub	$inp,$out		# re-bias
	sub	$inp,$in0
	add	$inp,$len		# end of input

	#mov	$inp,$_inp		# saved later
	#mov	$out,$_out		# kept in $offload
	mov	$len,$_end
	#mov	$key,$_key		# remains resident in $inp register
	mov	$ivp,$_ivp
	mov	$ctx,$_ctx
	mov	$in0,$_in0
	mov	%rax,$_rsp
.cfi_cfa_expression	$_rsp,deref,+8
___
$code.=<<___ if ($win64);
	movaps	%xmm6,`$framesz+16*0`(%rsp)
	movaps	%xmm7,`$framesz+16*1`(%rsp)
	movaps	%xmm8,`$framesz+16*2`(%rsp)
	movaps	%xmm9,`$framesz+16*3`(%rsp)
	movaps	%xmm10,`$framesz+16*4`(%rsp)
	movaps	%xmm11,`$framesz+16*5`(%rsp)
	movaps	%xmm12,`$framesz+16*6`(%rsp)
	movaps	%xmm13,`$framesz+16*7`(%rsp)
	movaps	%xmm14,`$framesz+16*8`(%rsp)
	movaps	%xmm15,`$framesz+16*9`(%rsp)
___
$code.=<<___;
.Lprologue_avx2:
	vzeroall

	mov	$inp,%r13		# borrow $a0
	vpinsrq	\$1,$out,$offload,$offload
	lea	0x80($key),$inp		# size optimization, reassign
	lea	$TABLE+`$SZ*2*$rounds+32`(%rip),%r12	# borrow $a4
	mov	0xf0-0x80($inp),%r14d	# rounds, borrow $a1
	mov	$ctx,%r15		# borrow $a2
	mov	$in0,%rsi		# borrow $a3
	vmovdqu	($ivp),$iv		# load IV
	lea	-9(%r14),%r14

	vmovdqa	0x00(%r12,%r14,8),$mask14
	vmovdqa	0x10(%r12,%r14,8),$mask12
	vmovdqa	0x20(%r12,%r14,8),$mask10

	sub	\$-16*$SZ,%r13		# inp++, size optimization
	mov	$SZ*0(%r15),$A
	lea	(%rsi,%r13),%r12	# borrow $a0
	mov	$SZ*1(%r15),$B
	cmp	$len,%r13		# $_end
	mov	$SZ*2(%r15),$C
	cmove	%rsp,%r12		# next block or random data
	mov	$SZ*3(%r15),$D
	mov	$SZ*4(%r15),$E
	mov	$SZ*5(%r15),$F
	mov	$SZ*6(%r15),$G
	mov	$SZ*7(%r15),$H
	vmovdqu	0x00-0x80($inp),$roundkey
___
					if ($SZ==4) {	# SHA256
    my @X = map("%ymm$_",(0..3));
    my ($t0,$t1,$t2,$t3) = map("%ymm$_",(4..7));

$code.=<<___;
	jmp	.Loop_avx2
.align	16
.Loop_avx2:
	vmovdqa	$TABLE+`$SZ*2*$rounds`(%rip),$t3
	vmovdqu	-16*$SZ+0(%rsi,%r13),%xmm0
	vmovdqu	-16*$SZ+16(%rsi,%r13),%xmm1
	vmovdqu	-16*$SZ+32(%rsi,%r13),%xmm2
	vmovdqu	-16*$SZ+48(%rsi,%r13),%xmm3

	vinserti128	\$1,(%r12),@X[0],@X[0]
	vinserti128	\$1,16(%r12),@X[1],@X[1]
	 vpshufb	$t3,@X[0],@X[0]
	vinserti128	\$1,32(%r12),@X[2],@X[2]
	 vpshufb	$t3,@X[1],@X[1]
	vinserti128	\$1,48(%r12),@X[3],@X[3]

	lea	$TABLE(%rip),$Tbl
	vpshufb	$t3,@X[2],@X[2]
	lea	-16*$SZ(%r13),%r13
	vpaddd	0x00($Tbl),@X[0],$t0
	vpshufb	$t3,@X[3],@X[3]
	vpaddd	0x20($Tbl),@X[1],$t1
	vpaddd	0x40($Tbl),@X[2],$t2
	vpaddd	0x60($Tbl),@X[3],$t3
	vmovdqa	$t0,0x00(%rsp)
	xor	$a1,$a1
	vmovdqa	$t1,0x20(%rsp)
	lea	-$PUSH8(%rsp),%rsp
	mov	$B,$a3
	vmovdqa	$t2,0x00(%rsp)
	xor	$C,$a3			# magic
	vmovdqa	$t3,0x20(%rsp)
	mov	$F,$a4
	sub	\$-16*2*$SZ,$Tbl	# size optimization
	jmp	.Lavx2_00_47

.align	16
.Lavx2_00_47:
	vmovdqu	(%r13),$inout
	vpinsrq	\$0,%r13,$offload,$offload
___

sub AVX2_256_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body,&$body,&$body);	# 96 instructions
my $base = "+2*$PUSH8(%rsp)";

	&lea	("%rsp","-$PUSH8(%rsp)")	if (($j%2)==0);
	foreach (Xupdate_256_AVX()) {		# 29 instructions
	    eval;
	    eval(shift(@insns));
	    eval(shift(@insns));
	    eval(shift(@insns));
	}
	&vpaddd		($t2,@X[0],16*2*$j."($Tbl)");
	  foreach (@insns) { eval; }		# remaining instructions
	&vmovdqa	((32*$j)%$PUSH8."(%rsp)",$t2);
}
    $aesni_cbc_idx=0;
    for ($i=0,$j=0; $j<4; $j++) {
	&AVX2_256_00_47($j,\&bodyx_00_15,@X);
	push(@X,shift(@X));			# rotate(@X)
    }
	&vmovq		("%r13",$offload);	# borrow $a0
	&vpextrq	("%r15",$offload,1);	# borrow $a2
	&vpand		($temp,$temp,$mask14);
	&vpor		($iv,$iv,$temp);
	&vmovdqu	("(%r15,%r13)",$iv);	# write output
	&lea		("%r13","16(%r13)");	# inp++

	&lea	($Tbl,16*2*$SZ."($Tbl)");
	&cmpb	(($SZ-1)."($Tbl)",0);
	&jne	(".Lavx2_00_47");

	&vmovdqu	($inout,"(%r13)");
	&vpinsrq	($offload,$offload,"%r13",0);

    $aesni_cbc_idx=0;
    for ($i=0; $i<16; ) {
	my $base=$i<8?"+$PUSH8(%rsp)":"(%rsp)";
	foreach(bodyx_00_15()) { eval; }
    }
					}
$code.=<<___;
	vpextrq	\$1,$offload,%r12		# $_out, borrow $a4
	vmovq	$offload,%r13			# $_inp, borrow $a0
	mov	`2*$SZ*$rounds+5*8`(%rsp),%r15	# $_ctx, borrow $a2
	add	$a1,$A
	lea	`2*$SZ*($rounds-8)`(%rsp),$Tbl

	vpand	$mask14,$temp,$temp
	vpor	$temp,$iv,$iv
	vmovdqu	$iv,(%r12,%r13)			# write output
	lea	16(%r13),%r13

	add	$SZ*0(%r15),$A
	add	$SZ*1(%r15),$B
	add	$SZ*2(%r15),$C
	add	$SZ*3(%r15),$D
	add	$SZ*4(%r15),$E
	add	$SZ*5(%r15),$F
	add	$SZ*6(%r15),$G
	add	$SZ*7(%r15),$H

	mov	$A,$SZ*0(%r15)
	mov	$B,$SZ*1(%r15)
	mov	$C,$SZ*2(%r15)
	mov	$D,$SZ*3(%r15)
	mov	$E,$SZ*4(%r15)
	mov	$F,$SZ*5(%r15)
	mov	$G,$SZ*6(%r15)
	mov	$H,$SZ*7(%r15)

	cmp	`$PUSH8+2*8`($Tbl),%r13		# $_end
	je	.Ldone_avx2

	xor	$a1,$a1
	mov	$B,$a3
	mov	$F,$a4
	xor	$C,$a3			# magic
	jmp	.Lower_avx2
.align	16
.Lower_avx2:
	vmovdqu	(%r13),$inout
	vpinsrq	\$0,%r13,$offload,$offload
___
    $aesni_cbc_idx=0;
    for ($i=0; $i<16; ) {
	my $base="+16($Tbl)";
	foreach(bodyx_00_15()) { eval; }
	&lea	($Tbl,"-$PUSH8($Tbl)")	if ($i==8);
    }
$code.=<<___;
	vmovq	$offload,%r13			# borrow $a0
	vpextrq	\$1,$offload,%r15		# borrow $a2
	vpand	$mask14,$temp,$temp
	vpor	$temp,$iv,$iv
	lea	-$PUSH8($Tbl),$Tbl
	vmovdqu	$iv,(%r15,%r13)			# write output
	lea	16(%r13),%r13			# inp++
	cmp	%rsp,$Tbl
	jae	.Lower_avx2

	mov	`2*$SZ*$rounds+5*8`(%rsp),%r15	# $_ctx, borrow $a2
	lea	16*$SZ(%r13),%r13
	mov	`2*$SZ*$rounds+6*8`(%rsp),%rsi	# $_in0, borrow $a3
	add	$a1,$A
	lea	`2*$SZ*($rounds-8)`(%rsp),%rsp

	add	$SZ*0(%r15),$A
	add	$SZ*1(%r15),$B
	add	$SZ*2(%r15),$C
	add	$SZ*3(%r15),$D
	add	$SZ*4(%r15),$E
	add	$SZ*5(%r15),$F
	add	$SZ*6(%r15),$G
	lea	(%rsi,%r13),%r12
	add	$SZ*7(%r15),$H

	cmp	$_end,%r13

	mov	$A,$SZ*0(%r15)
	cmove	%rsp,%r12		# next block or stale data
	mov	$B,$SZ*1(%r15)
	mov	$C,$SZ*2(%r15)
	mov	$D,$SZ*3(%r15)
	mov	$E,$SZ*4(%r15)
	mov	$F,$SZ*5(%r15)
	mov	$G,$SZ*6(%r15)
	mov	$H,$SZ*7(%r15)

	jbe	.Loop_avx2
	lea	(%rsp),$Tbl

.Ldone_avx2:
	lea	($Tbl),%rsp
	mov	$_ivp,$ivp
	mov	$_rsp,%rsi
.cfi_def_cfa	%rsi,8
	vmovdqu	$iv,($ivp)		# output IV
	vzeroall
___
$code.=<<___ if ($win64);
	movaps	`$framesz+16*0`(%rsp),%xmm6
	movaps	`$framesz+16*1`(%rsp),%xmm7
	movaps	`$framesz+16*2`(%rsp),%xmm8
	movaps	`$framesz+16*3`(%rsp),%xmm9
	movaps	`$framesz+16*4`(%rsp),%xmm10
	movaps	`$framesz+16*5`(%rsp),%xmm11
	movaps	`$framesz+16*6`(%rsp),%xmm12
	movaps	`$framesz+16*7`(%rsp),%xmm13
	movaps	`$framesz+16*8`(%rsp),%xmm14
	movaps	`$framesz+16*9`(%rsp),%xmm15
___
$code.=<<___;
	mov	-48(%rsi),%r15
.cfi_restore	%r15
	mov	-40(%rsi),%r14
.cfi_restore	%r14
	mov	-32(%rsi),%r13
.cfi_restore	%r13
	mov	-24(%rsi),%r12
.cfi_restore	%r12
	mov	-16(%rsi),%rbp
.cfi_restore	%rbp
	mov	-8(%rsi),%rbx
.cfi_restore	%rbx
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_avx2:
	ret
.cfi_endproc
.size	${func}_avx2,.-${func}_avx2
___
}}
}}
{{
my ($in0,$out,$len,$key,$ivp,$ctx,$inp)=("%rdi","%rsi","%rdx","%rcx","%r8","%r9","%r10");

my ($rounds,$Tbl)=("%r11d","%rbx");

my ($iv,$in,$rndkey0)=map("%xmm$_",(6,14,15));
my @rndkey=("%xmm4","%xmm5");
my $r=0;
my $sn=0;

my ($Wi,$ABEF,$CDGH,$TMP,$BSWAP,$ABEF_SAVE,$CDGH_SAVE)=map("%xmm$_",(0..3,7..9));
my @MSG=map("%xmm$_",(10..13));

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
	nop
___
    } else {
      $code.=<<___;
	movups		`32+16*$k-112`($key),$rndkey[1]
	aesenc		$rndkey[0],$iv
___
    }
    $r++;	unshift(@rndkey,pop(@rndkey));
};

if ($shaext) {
my $Tbl="%rax";

$code.=<<___;
.type	${func}_shaext,\@function,6
.align	32
${func}_shaext:
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
	lea		K256+0x80(%rip),$Tbl
	movdqu		($ctx),$ABEF		# DCBA
	movdqu		16($ctx),$CDGH		# HGFE
	movdqa		0x200-0x80($Tbl),$TMP	# byte swap mask

	mov		240($key),$rounds
	sub		$in0,$out
	movups		($key),$rndkey0		# $key[0]
	movups		($ivp),$iv		# load IV
	movups		16($key),$rndkey[0]	# forward reference
	lea		112($key),$key		# size optimization

	pshufd		\$0x1b,$ABEF,$Wi	# ABCD
	pshufd		\$0xb1,$ABEF,$ABEF	# CDAB
	pshufd		\$0x1b,$CDGH,$CDGH	# EFGH
	movdqa		$TMP,$BSWAP		# offload
	palignr		\$8,$CDGH,$ABEF		# ABEF
	punpcklqdq	$Wi,$CDGH		# CDGH

	jmp	.Loop_shaext

.align	16
.Loop_shaext:
	movdqu		($inp),@MSG[0]
	movdqu		0x10($inp),@MSG[1]
	movdqu		0x20($inp),@MSG[2]
	pshufb		$TMP,@MSG[0]
	movdqu		0x30($inp),@MSG[3]

	movdqa		0*32-0x80($Tbl),$Wi
	paddd		@MSG[0],$Wi
	pshufb		$TMP,@MSG[1]
	movdqa		$CDGH,$CDGH_SAVE	# offload
	movdqa		$ABEF,$ABEF_SAVE	# offload
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$ABEF,$CDGH		# 0-3
	pshufd		\$0x0e,$Wi,$Wi
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$CDGH,$ABEF

	movdqa		1*32-0x80($Tbl),$Wi
	paddd		@MSG[1],$Wi
	pshufb		$TMP,@MSG[2]
	lea		0x40($inp),$inp
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$ABEF,$CDGH		# 4-7
	pshufd		\$0x0e,$Wi,$Wi
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$CDGH,$ABEF

	movdqa		2*32-0x80($Tbl),$Wi
	paddd		@MSG[2],$Wi
	pshufb		$TMP,@MSG[3]
	sha256msg1	@MSG[1],@MSG[0]
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$ABEF,$CDGH		# 8-11
	pshufd		\$0x0e,$Wi,$Wi
	movdqa		@MSG[3],$TMP
	palignr		\$4,@MSG[2],$TMP
	paddd		$TMP,@MSG[0]
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$CDGH,$ABEF

	movdqa		3*32-0x80($Tbl),$Wi
	paddd		@MSG[3],$Wi
	sha256msg2	@MSG[3],@MSG[0]
	sha256msg1	@MSG[2],@MSG[1]
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$ABEF,$CDGH		# 12-15
	pshufd		\$0x0e,$Wi,$Wi
___
	&$aesenc();
$code.=<<___;
	movdqa		@MSG[0],$TMP
	palignr		\$4,@MSG[3],$TMP
	paddd		$TMP,@MSG[1]
	sha256rnds2	$CDGH,$ABEF
___
for($i=4;$i<16-3;$i++) {
	&$aesenc()	if (($r%10)==0);
$code.=<<___;
	movdqa		$i*32-0x80($Tbl),$Wi
	paddd		@MSG[0],$Wi
	sha256msg2	@MSG[0],@MSG[1]
	sha256msg1	@MSG[3],@MSG[2]
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$ABEF,$CDGH		# 16-19...
	pshufd		\$0x0e,$Wi,$Wi
	movdqa		@MSG[1],$TMP
	palignr		\$4,@MSG[0],$TMP
	paddd		$TMP,@MSG[2]
___
	&$aesenc();
	&$aesenc()	if ($r==19);
$code.=<<___;
	sha256rnds2	$CDGH,$ABEF
___
	push(@MSG,shift(@MSG));
}
$code.=<<___;
	movdqa		13*32-0x80($Tbl),$Wi
	paddd		@MSG[0],$Wi
	sha256msg2	@MSG[0],@MSG[1]
	sha256msg1	@MSG[3],@MSG[2]
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$ABEF,$CDGH		# 52-55
	pshufd		\$0x0e,$Wi,$Wi
	movdqa		@MSG[1],$TMP
	palignr		\$4,@MSG[0],$TMP
	paddd		$TMP,@MSG[2]
___
	&$aesenc();
	&$aesenc();
$code.=<<___;
	sha256rnds2	$CDGH,$ABEF

	movdqa		14*32-0x80($Tbl),$Wi
	paddd		@MSG[1],$Wi
	sha256msg2	@MSG[1],@MSG[2]
	movdqa		$BSWAP,$TMP
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$ABEF,$CDGH		# 56-59
	pshufd		\$0x0e,$Wi,$Wi
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$CDGH,$ABEF

	movdqa		15*32-0x80($Tbl),$Wi
	paddd		@MSG[2],$Wi
___
	&$aesenc();
	&$aesenc();
$code.=<<___;
	sha256rnds2	$ABEF,$CDGH		# 60-63
	pshufd		\$0x0e,$Wi,$Wi
___
	&$aesenc();
$code.=<<___;
	sha256rnds2	$CDGH,$ABEF
	#pxor		$CDGH,$rndkey0		# black magic
___
	while ($r<40)	{ &$aesenc(); }		# remaining aesenc's
$code.=<<___;
	#xorps		$CDGH,$rndkey0		# black magic
	paddd		$CDGH_SAVE,$CDGH
	paddd		$ABEF_SAVE,$ABEF

	dec		$len
	movups		$iv,48($out,$in0)	# write output
	lea		64($in0),$in0
	jnz		.Loop_shaext

	pshufd		\$0xb1,$CDGH,$CDGH	# DCHG
	pshufd		\$0x1b,$ABEF,$TMP	# FEBA
	pshufd		\$0xb1,$ABEF,$ABEF	# BAFE
	punpckhqdq	$CDGH,$ABEF		# DCBA
	palignr		\$8,$TMP,$CDGH		# HGFE

	movups		$iv,($ivp)		# write IV
	movdqu		$ABEF,($ctx)
	movdqu		$CDGH,16($ctx)
___
$code.=<<___ if ($win64);
	movaps	0*16(%rsp),%xmm6
	movaps	1*16(%rsp),%xmm7
	movaps	2*16(%rsp),%xmm8
	movaps	3*16(%rsp),%xmm9
	movaps	4*16(%rsp),%xmm10
	movaps	5*16(%rsp),%xmm11
	movaps	6*16(%rsp),%xmm12
	movaps	7*16(%rsp),%xmm13
	movaps	8*16(%rsp),%xmm14
	movaps	9*16(%rsp),%xmm15
	lea	8+10*16(%rsp),%rsp
.Lepilogue_shaext:
___
$code.=<<___;
	ret
.size	${func}_shaext,.-${func}_shaext
___
}
}}}}}

# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
if ($win64 && $avx) {
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___;
.extern	__imp_RtlVirtualUnwind
.type	se_handler,\@abi-omnipotent
.align	16
se_handler:
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
	mov	56($disp),%r11		# disp->HanderlData

	mov	0(%r11),%r10d		# HandlerData[0]
	lea	(%rsi,%r10),%r10	# prologue label
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lin_prologue

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lin_prologue
___
$code.=<<___ if ($shaext);
	lea	aesni_cbc_sha256_enc_shaext(%rip),%r10
	cmp	%r10,%rbx
	jb	.Lnot_in_shaext

	lea	(%rax),%rsi
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$20,%ecx
	.long	0xa548f3fc		# cld; rep movsq
	lea	168(%rax),%rax		# adjust stack pointer
	jmp	.Lin_prologue
.Lnot_in_shaext:
___
$code.=<<___ if ($avx>1);
	lea	.Lavx2_shortcut(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<avx2_shortcut
	jb	.Lnot_in_avx2

	and	\$-256*$SZ,%rax
	add	\$`2*$SZ*($rounds-8)`,%rax
.Lnot_in_avx2:
___
$code.=<<___;
	mov	%rax,%rsi		# put aside Rsp
	mov	16*$SZ+7*8(%rax),%rax	# pull $_rsp

	mov	-8(%rax),%rbx
	mov	-16(%rax),%rbp
	mov	-24(%rax),%r12
	mov	-32(%rax),%r13
	mov	-40(%rax),%r14
	mov	-48(%rax),%r15
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12
	mov	%r13,224($context)	# restore context->R13
	mov	%r14,232($context)	# restore context->R14
	mov	%r15,240($context)	# restore context->R15

	lea	16*$SZ+8*8(%rsi),%rsi	# Xmm6- save area
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$20,%ecx
	.long	0xa548f3fc		# cld; rep movsq

.Lin_prologue:
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
.size	se_handler,.-se_handler

.section	.pdata
	.rva	.LSEH_begin_${func}_xop
	.rva	.LSEH_end_${func}_xop
	.rva	.LSEH_info_${func}_xop

	.rva	.LSEH_begin_${func}_avx
	.rva	.LSEH_end_${func}_avx
	.rva	.LSEH_info_${func}_avx
___
$code.=<<___ if ($avx>1);
	.rva	.LSEH_begin_${func}_avx2
	.rva	.LSEH_end_${func}_avx2
	.rva	.LSEH_info_${func}_avx2
___
$code.=<<___ if ($shaext);
	.rva	.LSEH_begin_${func}_shaext
	.rva	.LSEH_end_${func}_shaext
	.rva	.LSEH_info_${func}_shaext
___
$code.=<<___;
.section	.xdata
.align	8
.LSEH_info_${func}_xop:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lprologue_xop,.Lepilogue_xop		# HandlerData[]

.LSEH_info_${func}_avx:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lprologue_avx,.Lepilogue_avx		# HandlerData[]
___
$code.=<<___ if ($avx>1);
.LSEH_info_${func}_avx2:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lprologue_avx2,.Lepilogue_avx2		# HandlerData[]
___
$code.=<<___ if ($shaext);
.LSEH_info_${func}_shaext:
	.byte	9,0,0,0
	.rva	se_handler
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

{
  my %opcodelet = (
		"sha256rnds2" => 0xcb,
  		"sha256msg1"  => 0xcc,
		"sha256msg2"  => 0xcd	);

  sub sha256op38 {
    my $instr = shift;

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
}

$code =~ s/\`([^\`]*)\`/eval $1/gem;
$code =~ s/\b(sha256[^\s]*)\s+(.*)/sha256op38($1,$2)/gem;
print $code;
close STDOUT;

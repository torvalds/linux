#! /usr/bin/env perl
# Copyright 2007-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# sha1_block procedure for ARMv4.
#
# January 2007.

# Size/performance trade-off
# ====================================================================
# impl		size in bytes	comp cycles[*]	measured performance
# ====================================================================
# thumb		304		3212		4420
# armv4-small	392/+29%	1958/+64%	2250/+96%
# armv4-compact	740/+89%	1552/+26%	1840/+22%
# armv4-large	1420/+92%	1307/+19%	1370/+34%[***]
# full unroll	~5100/+260%	~1260/+4%	~1300/+5%
# ====================================================================
# thumb		= same as 'small' but in Thumb instructions[**] and
#		  with recurring code in two private functions;
# small		= detached Xload/update, loops are folded;
# compact	= detached Xload/update, 5x unroll;
# large		= interleaved Xload/update, 5x unroll;
# full unroll	= interleaved Xload/update, full unroll, estimated[!];
#
# [*]	Manually counted instructions in "grand" loop body. Measured
#	performance is affected by prologue and epilogue overhead,
#	i-cache availability, branch penalties, etc.
# [**]	While each Thumb instruction is twice smaller, they are not as
#	diverse as ARM ones: e.g., there are only two arithmetic
#	instructions with 3 arguments, no [fixed] rotate, addressing
#	modes are limited. As result it takes more instructions to do
#	the same job in Thumb, therefore the code is never twice as
#	small and always slower.
# [***]	which is also ~35% better than compiler generated code. Dual-
#	issue Cortex A8 core was measured to process input block in
#	~990 cycles.

# August 2010.
#
# Rescheduling for dual-issue pipeline resulted in 13% improvement on
# Cortex A8 core and in absolute terms ~870 cycles per input block
# [or 13.6 cycles per byte].

# February 2011.
#
# Profiler-assisted and platform-specific optimization resulted in 10%
# improvement on Cortex A8 core and 12.2 cycles per byte.

# September 2013.
#
# Add NEON implementation (see sha1-586.pl for background info). On
# Cortex A8 it was measured to process one byte in 6.7 cycles or >80%
# faster than integer-only code. Because [fully unrolled] NEON code
# is ~2.5x larger and there are some redundant instructions executed
# when processing last block, improvement is not as big for smallest
# blocks, only ~30%. Snapdragon S4 is a tad faster, 6.4 cycles per
# byte, which is also >80% faster than integer-only code. Cortex-A15
# is even faster spending 5.6 cycles per byte outperforming integer-
# only code by factor of 2.

# May 2014.
#
# Add ARMv8 code path performing at 2.35 cpb on Apple A7.

$flavour = shift;
if ($flavour=~/\w[\w\-]*\.\w+$/) { $output=$flavour; undef $flavour; }
else { while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {} }

if ($flavour && $flavour ne "void") {
    $0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
    ( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
    ( $xlate="${dir}../../perlasm/arm-xlate.pl" and -f $xlate) or
    die "can't locate arm-xlate.pl";

    open STDOUT,"| \"$^X\" $xlate $flavour $output";
} else {
    open STDOUT,">$output";
}

$ctx="r0";
$inp="r1";
$len="r2";
$a="r3";
$b="r4";
$c="r5";
$d="r6";
$e="r7";
$K="r8";
$t0="r9";
$t1="r10";
$t2="r11";
$t3="r12";
$Xi="r14";
@V=($a,$b,$c,$d,$e);

sub Xupdate {
my ($a,$b,$c,$d,$e,$opt1,$opt2)=@_;
$code.=<<___;
	ldr	$t0,[$Xi,#15*4]
	ldr	$t1,[$Xi,#13*4]
	ldr	$t2,[$Xi,#7*4]
	add	$e,$K,$e,ror#2			@ E+=K_xx_xx
	ldr	$t3,[$Xi,#2*4]
	eor	$t0,$t0,$t1
	eor	$t2,$t2,$t3			@ 1 cycle stall
	eor	$t1,$c,$d			@ F_xx_xx
	mov	$t0,$t0,ror#31
	add	$e,$e,$a,ror#27			@ E+=ROR(A,27)
	eor	$t0,$t0,$t2,ror#31
	str	$t0,[$Xi,#-4]!
	$opt1					@ F_xx_xx
	$opt2					@ F_xx_xx
	add	$e,$e,$t0			@ E+=X[i]
___
}

sub BODY_00_15 {
my ($a,$b,$c,$d,$e)=@_;
$code.=<<___;
#if __ARM_ARCH__<7
	ldrb	$t1,[$inp,#2]
	ldrb	$t0,[$inp,#3]
	ldrb	$t2,[$inp,#1]
	add	$e,$K,$e,ror#2			@ E+=K_00_19
	ldrb	$t3,[$inp],#4
	orr	$t0,$t0,$t1,lsl#8
	eor	$t1,$c,$d			@ F_xx_xx
	orr	$t0,$t0,$t2,lsl#16
	add	$e,$e,$a,ror#27			@ E+=ROR(A,27)
	orr	$t0,$t0,$t3,lsl#24
#else
	ldr	$t0,[$inp],#4			@ handles unaligned
	add	$e,$K,$e,ror#2			@ E+=K_00_19
	eor	$t1,$c,$d			@ F_xx_xx
	add	$e,$e,$a,ror#27			@ E+=ROR(A,27)
#ifdef __ARMEL__
	rev	$t0,$t0				@ byte swap
#endif
#endif
	and	$t1,$b,$t1,ror#2
	add	$e,$e,$t0			@ E+=X[i]
	eor	$t1,$t1,$d,ror#2		@ F_00_19(B,C,D)
	str	$t0,[$Xi,#-4]!
	add	$e,$e,$t1			@ E+=F_00_19(B,C,D)
___
}

sub BODY_16_19 {
my ($a,$b,$c,$d,$e)=@_;
	&Xupdate(@_,"and $t1,$b,$t1,ror#2");
$code.=<<___;
	eor	$t1,$t1,$d,ror#2		@ F_00_19(B,C,D)
	add	$e,$e,$t1			@ E+=F_00_19(B,C,D)
___
}

sub BODY_20_39 {
my ($a,$b,$c,$d,$e)=@_;
	&Xupdate(@_,"eor $t1,$b,$t1,ror#2");
$code.=<<___;
	add	$e,$e,$t1			@ E+=F_20_39(B,C,D)
___
}

sub BODY_40_59 {
my ($a,$b,$c,$d,$e)=@_;
	&Xupdate(@_,"and $t1,$b,$t1,ror#2","and $t2,$c,$d");
$code.=<<___;
	add	$e,$e,$t1			@ E+=F_40_59(B,C,D)
	add	$e,$e,$t2,ror#2
___
}

$code=<<___;
#include "arm_arch.h"

.text
#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif

.global	sha1_block_data_order
.type	sha1_block_data_order,%function

.align	5
sha1_block_data_order:
#if __ARM_MAX_ARCH__>=7
.Lsha1_block:
	adr	r3,.Lsha1_block
	ldr	r12,.LOPENSSL_armcap
	ldr	r12,[r3,r12]		@ OPENSSL_armcap_P
#ifdef	__APPLE__
	ldr	r12,[r12]
#endif
	tst	r12,#ARMV8_SHA1
	bne	.LARMv8
	tst	r12,#ARMV7_NEON
	bne	.LNEON
#endif
	stmdb	sp!,{r4-r12,lr}
	add	$len,$inp,$len,lsl#6	@ $len to point at the end of $inp
	ldmia	$ctx,{$a,$b,$c,$d,$e}
.Lloop:
	ldr	$K,.LK_00_19
	mov	$Xi,sp
	sub	sp,sp,#15*4
	mov	$c,$c,ror#30
	mov	$d,$d,ror#30
	mov	$e,$e,ror#30		@ [6]
.L_00_15:
___
for($i=0;$i<5;$i++) {
	&BODY_00_15(@V);	unshift(@V,pop(@V));
}
$code.=<<___;
#if defined(__thumb2__)
	mov	$t3,sp
	teq	$Xi,$t3
#else
	teq	$Xi,sp
#endif
	bne	.L_00_15		@ [((11+4)*5+2)*3]
	sub	sp,sp,#25*4
___
	&BODY_00_15(@V);	unshift(@V,pop(@V));
	&BODY_16_19(@V);	unshift(@V,pop(@V));
	&BODY_16_19(@V);	unshift(@V,pop(@V));
	&BODY_16_19(@V);	unshift(@V,pop(@V));
	&BODY_16_19(@V);	unshift(@V,pop(@V));
$code.=<<___;

	ldr	$K,.LK_20_39		@ [+15+16*4]
	cmn	sp,#0			@ [+3], clear carry to denote 20_39
.L_20_39_or_60_79:
___
for($i=0;$i<5;$i++) {
	&BODY_20_39(@V);	unshift(@V,pop(@V));
}
$code.=<<___;
#if defined(__thumb2__)
	mov	$t3,sp
	teq	$Xi,$t3
#else
	teq	$Xi,sp			@ preserve carry
#endif
	bne	.L_20_39_or_60_79	@ [+((12+3)*5+2)*4]
	bcs	.L_done			@ [+((12+3)*5+2)*4], spare 300 bytes

	ldr	$K,.LK_40_59
	sub	sp,sp,#20*4		@ [+2]
.L_40_59:
___
for($i=0;$i<5;$i++) {
	&BODY_40_59(@V);	unshift(@V,pop(@V));
}
$code.=<<___;
#if defined(__thumb2__)
	mov	$t3,sp
	teq	$Xi,$t3
#else
	teq	$Xi,sp
#endif
	bne	.L_40_59		@ [+((12+5)*5+2)*4]

	ldr	$K,.LK_60_79
	sub	sp,sp,#20*4
	cmp	sp,#0			@ set carry to denote 60_79
	b	.L_20_39_or_60_79	@ [+4], spare 300 bytes
.L_done:
	add	sp,sp,#80*4		@ "deallocate" stack frame
	ldmia	$ctx,{$K,$t0,$t1,$t2,$t3}
	add	$a,$K,$a
	add	$b,$t0,$b
	add	$c,$t1,$c,ror#2
	add	$d,$t2,$d,ror#2
	add	$e,$t3,$e,ror#2
	stmia	$ctx,{$a,$b,$c,$d,$e}
	teq	$inp,$len
	bne	.Lloop			@ [+18], total 1307

#if __ARM_ARCH__>=5
	ldmia	sp!,{r4-r12,pc}
#else
	ldmia	sp!,{r4-r12,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	sha1_block_data_order,.-sha1_block_data_order

.align	5
.LK_00_19:	.word	0x5a827999
.LK_20_39:	.word	0x6ed9eba1
.LK_40_59:	.word	0x8f1bbcdc
.LK_60_79:	.word	0xca62c1d6
#if __ARM_MAX_ARCH__>=7
.LOPENSSL_armcap:
.word	OPENSSL_armcap_P-.Lsha1_block
#endif
.asciz	"SHA1 block transform for ARMv4/NEON/ARMv8, CRYPTOGAMS by <appro\@openssl.org>"
.align	5
___
#####################################################################
# NEON stuff
#
{{{
my @V=($a,$b,$c,$d,$e);
my ($K_XX_XX,$Ki,$t0,$t1,$Xfer,$saved_sp)=map("r$_",(8..12,14));
my $Xi=4;
my @X=map("q$_",(8..11,0..3));
my @Tx=("q12","q13");
my ($K,$zero)=("q14","q15");
my $j=0;

sub AUTOLOAD()          # thunk [simplified] x86-style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://; $opcode =~ s/_/\./;
  my $arg = pop;
    $arg = "#$arg" if ($arg*1 eq $arg);
    $code .= "\t$opcode\t".join(',',@_,$arg)."\n";
}

sub body_00_19 () {
	(
	'($a,$b,$c,$d,$e)=@V;'.		# '$code.="@ $j\n";'.
	'&bic	($t0,$d,$b)',
	'&add	($e,$e,$Ki)',		# e+=X[i]+K
	'&and	($t1,$c,$b)',
	'&ldr	($Ki,sprintf "[sp,#%d]",4*(($j+1)&15))',
	'&add	($e,$e,$a,"ror#27")',	# e+=ROR(A,27)
	'&eor	($t1,$t1,$t0)',		# F_00_19
	'&mov	($b,$b,"ror#2")',	# b=ROR(b,2)
	'&add	($e,$e,$t1);'.		# e+=F_00_19
	'$j++;	unshift(@V,pop(@V));'
	)
}
sub body_20_39 () {
	(
	'($a,$b,$c,$d,$e)=@V;'.		# '$code.="@ $j\n";'.
	'&eor	($t0,$b,$d)',
	'&add	($e,$e,$Ki)',		# e+=X[i]+K
	'&ldr	($Ki,sprintf "[sp,#%d]",4*(($j+1)&15)) if ($j<79)',
	'&eor	($t1,$t0,$c)',		# F_20_39
	'&add	($e,$e,$a,"ror#27")',	# e+=ROR(A,27)
	'&mov	($b,$b,"ror#2")',	# b=ROR(b,2)
	'&add	($e,$e,$t1);'.		# e+=F_20_39
	'$j++;	unshift(@V,pop(@V));'
	)
}
sub body_40_59 () {
	(
	'($a,$b,$c,$d,$e)=@V;'.		# '$code.="@ $j\n";'.
	'&add	($e,$e,$Ki)',		# e+=X[i]+K
	'&and	($t0,$c,$d)',
	'&ldr	($Ki,sprintf "[sp,#%d]",4*(($j+1)&15))',
	'&add	($e,$e,$a,"ror#27")',	# e+=ROR(A,27)
	'&eor	($t1,$c,$d)',
	'&add	($e,$e,$t0)',
	'&and	($t1,$t1,$b)',
	'&mov	($b,$b,"ror#2")',	# b=ROR(b,2)
	'&add	($e,$e,$t1);'.		# e+=F_40_59
	'$j++;	unshift(@V,pop(@V));'
	)
}

sub Xupdate_16_31 ()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);
  my ($a,$b,$c,$d,$e);

	&vext_8		(@X[0],@X[-4&7],@X[-3&7],8);	# compose "X[-14]" in "X[0]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &vadd_i32	(@Tx[1],@X[-1&7],$K);
	 eval(shift(@insns));
	  &vld1_32	("{$K\[]}","[$K_XX_XX,:32]!")	if ($Xi%5==0);
	 eval(shift(@insns));
	&vext_8		(@Tx[0],@X[-1&7],$zero,4);	# "X[-3]", 3 words
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&veor		(@X[0],@X[0],@X[-4&7]);		# "X[0]"^="X[-16]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	&veor		(@Tx[0],@Tx[0],@X[-2&7]);	# "X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	&veor		(@Tx[0],@Tx[0],@X[0]);		# "X[0]"^="X[-3]"^"X[-8]
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &vst1_32	("{@Tx[1]}","[$Xfer,:128]!");	# X[]+K xfer
	  &sub		($Xfer,$Xfer,64)		if ($Xi%4==0);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vext_8		(@Tx[1],$zero,@Tx[0],4);	# "X[0]"<<96, extract one dword
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vadd_i32	(@X[0],@Tx[0],@Tx[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vsri_32	(@X[0],@Tx[0],31);		# "X[0]"<<<=1
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vshr_u32	(@Tx[0],@Tx[1],30);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vshl_u32	(@Tx[1],@Tx[1],2);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&veor		(@X[0],@X[0],@Tx[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&veor		(@X[0],@X[0],@Tx[1]);		# "X[0]"^=("X[0]">>96)<<<2

	foreach (@insns) { eval; }	# remaining instructions [if any]

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
}

sub Xupdate_32_79 ()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);
  my ($a,$b,$c,$d,$e);

	&vext_8		(@Tx[0],@X[-2&7],@X[-1&7],8);	# compose "X[-6]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&veor		(@X[0],@X[0],@X[-4&7]);		# "X[0]"="X[-32]"^"X[-16]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	&veor		(@X[0],@X[0],@X[-7&7]);		# "X[0]"^="X[-28]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &vadd_i32	(@Tx[1],@X[-1&7],$K);
	 eval(shift(@insns));
	  &vld1_32	("{$K\[]}","[$K_XX_XX,:32]!")	if ($Xi%5==0);
	 eval(shift(@insns));
	&veor		(@Tx[0],@Tx[0],@X[0]);		# "X[-6]"^="X[0]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vshr_u32	(@X[0],@Tx[0],30);
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &vst1_32	("{@Tx[1]}","[$Xfer,:128]!");	# X[]+K xfer
	  &sub		($Xfer,$Xfer,64)		if ($Xi%4==0);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vsli_32	(@X[0],@Tx[0],2);		# "X[0]"="X[-6]"<<<2

	foreach (@insns) { eval; }	# remaining instructions [if any]

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
}

sub Xuplast_80 ()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);
  my ($a,$b,$c,$d,$e);

	&vadd_i32	(@Tx[1],@X[-1&7],$K);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vst1_32	("{@Tx[1]}","[$Xfer,:128]!");
	&sub		($Xfer,$Xfer,64);

	&teq		($inp,$len);
	&sub		($K_XX_XX,$K_XX_XX,16);	# rewind $K_XX_XX
	&it		("eq");
	&subeq		($inp,$inp,64);		# reload last block to avoid SEGV
	&vld1_8		("{@X[-4&7]-@X[-3&7]}","[$inp]!");
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vld1_8		("{@X[-2&7]-@X[-1&7]}","[$inp]!");
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vld1_32	("{$K\[]}","[$K_XX_XX,:32]!");	# load K_00_19
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vrev32_8	(@X[-4&7],@X[-4&7]);

	foreach (@insns) { eval; }		# remaining instructions

   $Xi=0;
}

sub Xloop()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);
  my ($a,$b,$c,$d,$e);

	&vrev32_8	(@X[($Xi-3)&7],@X[($Xi-3)&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vadd_i32	(@X[$Xi&7],@X[($Xi-4)&7],$K);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vst1_32	("{@X[$Xi&7]}","[$Xfer,:128]!");# X[]+K xfer to IALU

	foreach (@insns) { eval; }

  $Xi++;
}

$code.=<<___;
#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.type	sha1_block_data_order_neon,%function
.align	4
sha1_block_data_order_neon:
.LNEON:
	stmdb	sp!,{r4-r12,lr}
	add	$len,$inp,$len,lsl#6	@ $len to point at the end of $inp
	@ dmb				@ errata #451034 on early Cortex A8
	@ vstmdb	sp!,{d8-d15}	@ ABI specification says so
	mov	$saved_sp,sp
	sub	$Xfer,sp,#64
	adr	$K_XX_XX,.LK_00_19
	bic	$Xfer,$Xfer,#15		@ align for 128-bit stores

	ldmia	$ctx,{$a,$b,$c,$d,$e}	@ load context
	mov	sp,$Xfer		@ alloca

	vld1.8		{@X[-4&7]-@X[-3&7]},[$inp]!	@ handles unaligned
	veor		$zero,$zero,$zero
	vld1.8		{@X[-2&7]-@X[-1&7]},[$inp]!
	vld1.32		{${K}\[]},[$K_XX_XX,:32]!	@ load K_00_19
	vrev32.8	@X[-4&7],@X[-4&7]		@ yes, even on
	vrev32.8	@X[-3&7],@X[-3&7]		@ big-endian...
	vrev32.8	@X[-2&7],@X[-2&7]
	vadd.i32	@X[0],@X[-4&7],$K
	vrev32.8	@X[-1&7],@X[-1&7]
	vadd.i32	@X[1],@X[-3&7],$K
	vst1.32		{@X[0]},[$Xfer,:128]!
	vadd.i32	@X[2],@X[-2&7],$K
	vst1.32		{@X[1]},[$Xfer,:128]!
	vst1.32		{@X[2]},[$Xfer,:128]!
	ldr		$Ki,[sp]			@ big RAW stall

.Loop_neon:
___
	&Xupdate_16_31(\&body_00_19);
	&Xupdate_16_31(\&body_00_19);
	&Xupdate_16_31(\&body_00_19);
	&Xupdate_16_31(\&body_00_19);
	&Xupdate_32_79(\&body_00_19);
	&Xupdate_32_79(\&body_20_39);
	&Xupdate_32_79(\&body_20_39);
	&Xupdate_32_79(\&body_20_39);
	&Xupdate_32_79(\&body_20_39);
	&Xupdate_32_79(\&body_20_39);
	&Xupdate_32_79(\&body_40_59);
	&Xupdate_32_79(\&body_40_59);
	&Xupdate_32_79(\&body_40_59);
	&Xupdate_32_79(\&body_40_59);
	&Xupdate_32_79(\&body_40_59);
	&Xupdate_32_79(\&body_20_39);
	&Xuplast_80(\&body_20_39);
	&Xloop(\&body_20_39);
	&Xloop(\&body_20_39);
	&Xloop(\&body_20_39);
$code.=<<___;
	ldmia	$ctx,{$Ki,$t0,$t1,$Xfer}	@ accumulate context
	add	$a,$a,$Ki
	ldr	$Ki,[$ctx,#16]
	add	$b,$b,$t0
	add	$c,$c,$t1
	add	$d,$d,$Xfer
	it	eq
	moveq	sp,$saved_sp
	add	$e,$e,$Ki
	it	ne
	ldrne	$Ki,[sp]
	stmia	$ctx,{$a,$b,$c,$d,$e}
	itt	ne
	addne	$Xfer,sp,#3*16
	bne	.Loop_neon

	@ vldmia	sp!,{d8-d15}
	ldmia	sp!,{r4-r12,pc}
.size	sha1_block_data_order_neon,.-sha1_block_data_order_neon
#endif
___
}}}
#####################################################################
# ARMv8 stuff
#
{{{
my ($ABCD,$E,$E0,$E1)=map("q$_",(0..3));
my @MSG=map("q$_",(4..7));
my @Kxx=map("q$_",(8..11));
my ($W0,$W1,$ABCD_SAVE)=map("q$_",(12..14));

$code.=<<___;
#if __ARM_MAX_ARCH__>=7

# if defined(__thumb2__)
#  define INST(a,b,c,d)	.byte	c,d|0xf,a,b
# else
#  define INST(a,b,c,d)	.byte	a,b,c,d|0x10
# endif

.type	sha1_block_data_order_armv8,%function
.align	5
sha1_block_data_order_armv8:
.LARMv8:
	vstmdb	sp!,{d8-d15}		@ ABI specification says so

	veor	$E,$E,$E
	adr	r3,.LK_00_19
	vld1.32	{$ABCD},[$ctx]!
	vld1.32	{$E\[0]},[$ctx]
	sub	$ctx,$ctx,#16
	vld1.32	{@Kxx[0]\[]},[r3,:32]!
	vld1.32	{@Kxx[1]\[]},[r3,:32]!
	vld1.32	{@Kxx[2]\[]},[r3,:32]!
	vld1.32	{@Kxx[3]\[]},[r3,:32]

.Loop_v8:
	vld1.8		{@MSG[0]-@MSG[1]},[$inp]!
	vld1.8		{@MSG[2]-@MSG[3]},[$inp]!
	vrev32.8	@MSG[0],@MSG[0]
	vrev32.8	@MSG[1],@MSG[1]

	vadd.i32	$W0,@Kxx[0],@MSG[0]
	vrev32.8	@MSG[2],@MSG[2]
	vmov		$ABCD_SAVE,$ABCD	@ offload
	subs		$len,$len,#1

	vadd.i32	$W1,@Kxx[0],@MSG[1]
	vrev32.8	@MSG[3],@MSG[3]
	sha1h		$E1,$ABCD		@ 0
	sha1c		$ABCD,$E,$W0
	vadd.i32	$W0,@Kxx[$j],@MSG[2]
	sha1su0		@MSG[0],@MSG[1],@MSG[2]
___
for ($j=0,$i=1;$i<20-3;$i++) {
my $f=("c","p","m","p")[$i/5];
$code.=<<___;
	sha1h		$E0,$ABCD		@ $i
	sha1$f		$ABCD,$E1,$W1
	vadd.i32	$W1,@Kxx[$j],@MSG[3]
	sha1su1		@MSG[0],@MSG[3]
___
$code.=<<___ if ($i<20-4);
	sha1su0		@MSG[1],@MSG[2],@MSG[3]
___
	($E0,$E1)=($E1,$E0);	($W0,$W1)=($W1,$W0);
	push(@MSG,shift(@MSG));	$j++ if ((($i+3)%5)==0);
}
$code.=<<___;
	sha1h		$E0,$ABCD		@ $i
	sha1p		$ABCD,$E1,$W1
	vadd.i32	$W1,@Kxx[$j],@MSG[3]

	sha1h		$E1,$ABCD		@ 18
	sha1p		$ABCD,$E0,$W0

	sha1h		$E0,$ABCD		@ 19
	sha1p		$ABCD,$E1,$W1

	vadd.i32	$E,$E,$E0
	vadd.i32	$ABCD,$ABCD,$ABCD_SAVE
	bne		.Loop_v8

	vst1.32		{$ABCD},[$ctx]!
	vst1.32		{$E\[0]},[$ctx]

	vldmia	sp!,{d8-d15}
	ret					@ bx lr
.size	sha1_block_data_order_armv8,.-sha1_block_data_order_armv8
#endif
___
}}}
$code.=<<___;
#if __ARM_MAX_ARCH__>=7
.comm	OPENSSL_armcap_P,4,4
#endif
___

{   my  %opcode = (
	"sha1c"		=> 0xf2000c40,	"sha1p"		=> 0xf2100c40,
	"sha1m"		=> 0xf2200c40,	"sha1su0"	=> 0xf2300c40,
	"sha1h"		=> 0xf3b902c0,	"sha1su1"	=> 0xf3ba0380	);

    sub unsha1 {
	my ($mnemonic,$arg)=@_;

	if ($arg =~ m/q([0-9]+)(?:,\s*q([0-9]+))?,\s*q([0-9]+)/o) {
	    my $word = $opcode{$mnemonic}|(($1&7)<<13)|(($1&8)<<19)
					 |(($2&7)<<17)|(($2&8)<<4)
					 |(($3&7)<<1) |(($3&8)<<2);
	    # since ARMv7 instructions are always encoded little-endian.
	    # correct solution is to use .inst directive, but older
	    # assemblers don't implement it:-(

	    # this fix-up provides Thumb encoding in conjunction with INST
	    $word &= ~0x10000000 if (($word & 0x0f000000) == 0x02000000);
	    sprintf "INST(0x%02x,0x%02x,0x%02x,0x%02x)\t@ %s %s",
			$word&0xff,($word>>8)&0xff,
			($word>>16)&0xff,($word>>24)&0xff,
			$mnemonic,$arg;
	}
    }
}

foreach (split($/,$code)) {
	s/{q([0-9]+)\[\]}/sprintf "{d%d[],d%d[]}",2*$1,2*$1+1/eo	or
	s/{q([0-9]+)\[0\]}/sprintf "{d%d[0]}",2*$1/eo;

	s/\b(sha1\w+)\s+(q.*)/unsha1($1,$2)/geo;

	s/\bret\b/bx	lr/o		or
	s/\bbx\s+lr\b/.word\t0xe12fff1e/o;	# make it possible to compile with -march=armv4

	print $_,$/;
}

close STDOUT; # enforce flush

#! /usr/bin/env perl
# Copyright 2006-2018 The OpenSSL Project Authors. All Rights Reserved.
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

# April 2006

# "Teaser" Montgomery multiplication module for PowerPC. It's possible
# to gain a bit more by modulo-scheduling outer loop, then dedicated
# squaring procedure should give further 20% and code can be adapted
# for 32-bit application running on 64-bit CPU. As for the latter.
# It won't be able to achieve "native" 64-bit performance, because in
# 32-bit application context every addc instruction will have to be
# expanded as addc, twice right shift by 32 and finally adde, etc.
# So far RSA *sign* performance improvement over pre-bn_mul_mont asm
# for 64-bit application running on PPC970/G5 is:
#
# 512-bit	+65%
# 1024-bit	+35%
# 2048-bit	+18%
# 4096-bit	+4%

# September 2016
#
# Add multiplication procedure operating on lengths divisible by 4
# and squaring procedure operating on lengths divisible by 8. Length
# is expressed in number of limbs. RSA private key operations are
# ~35-50% faster (more for longer keys) on contemporary high-end POWER
# processors in 64-bit builds, [mysteriously enough] more in 32-bit
# builds. On low-end 32-bit processors performance improvement turned
# to be marginal...

$flavour = shift;

if ($flavour =~ /32/) {
	$BITS=	32;
	$BNSZ=	$BITS/8;
	$SIZE_T=4;
	$RZONE=	224;

	$LD=	"lwz";		# load
	$LDU=	"lwzu";		# load and update
	$LDX=	"lwzx";		# load indexed
	$ST=	"stw";		# store
	$STU=	"stwu";		# store and update
	$STX=	"stwx";		# store indexed
	$STUX=	"stwux";	# store indexed and update
	$UMULL=	"mullw";	# unsigned multiply low
	$UMULH=	"mulhwu";	# unsigned multiply high
	$UCMP=	"cmplw";	# unsigned compare
	$SHRI=	"srwi";		# unsigned shift right by immediate
	$SHLI=	"slwi";		# unsigned shift left by immediate
	$PUSH=	$ST;
	$POP=	$LD;
} elsif ($flavour =~ /64/) {
	$BITS=	64;
	$BNSZ=	$BITS/8;
	$SIZE_T=8;
	$RZONE=	288;

	# same as above, but 64-bit mnemonics...
	$LD=	"ld";		# load
	$LDU=	"ldu";		# load and update
	$LDX=	"ldx";		# load indexed
	$ST=	"std";		# store
	$STU=	"stdu";		# store and update
	$STX=	"stdx";		# store indexed
	$STUX=	"stdux";	# store indexed and update
	$UMULL=	"mulld";	# unsigned multiply low
	$UMULH=	"mulhdu";	# unsigned multiply high
	$UCMP=	"cmpld";	# unsigned compare
	$SHRI=	"srdi";		# unsigned shift right by immediate
	$SHLI=	"sldi";		# unsigned shift left by immediate
	$PUSH=	$ST;
	$POP=	$LD;
} else { die "nonsense $flavour"; }

$FRAME=8*$SIZE_T+$RZONE;
$LOCALS=8*$SIZE_T;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour ".shift || die "can't call $xlate: $!";

$sp="r1";
$toc="r2";
$rp="r3";
$ap="r4";
$bp="r5";
$np="r6";
$n0="r7";
$num="r8";

{
my $ovf=$rp;
my $rp="r9";	# $rp is reassigned
my $aj="r10";
my $nj="r11";
my $tj="r12";
# non-volatile registers
my $i="r20";
my $j="r21";
my $tp="r22";
my $m0="r23";
my $m1="r24";
my $lo0="r25";
my $hi0="r26";
my $lo1="r27";
my $hi1="r28";
my $alo="r29";
my $ahi="r30";
my $nlo="r31";
#
my $nhi="r0";

$code=<<___;
.machine "any"
.text

.globl	.bn_mul_mont_int
.align	5
.bn_mul_mont_int:
	mr	$rp,r3		; $rp is reassigned
	li	r3,0
___
$code.=<<___ if ($BNSZ==4);
	cmpwi	$num,32		; longer key performance is not better
	bgelr
___
$code.=<<___;
	slwi	$num,$num,`log($BNSZ)/log(2)`
	li	$tj,-4096
	addi	$ovf,$num,$FRAME
	subf	$ovf,$ovf,$sp	; $sp-$ovf
	and	$ovf,$ovf,$tj	; minimize TLB usage
	subf	$ovf,$sp,$ovf	; $ovf-$sp
	mr	$tj,$sp
	srwi	$num,$num,`log($BNSZ)/log(2)`
	$STUX	$sp,$sp,$ovf

	$PUSH	r20,`-12*$SIZE_T`($tj)
	$PUSH	r21,`-11*$SIZE_T`($tj)
	$PUSH	r22,`-10*$SIZE_T`($tj)
	$PUSH	r23,`-9*$SIZE_T`($tj)
	$PUSH	r24,`-8*$SIZE_T`($tj)
	$PUSH	r25,`-7*$SIZE_T`($tj)
	$PUSH	r26,`-6*$SIZE_T`($tj)
	$PUSH	r27,`-5*$SIZE_T`($tj)
	$PUSH	r28,`-4*$SIZE_T`($tj)
	$PUSH	r29,`-3*$SIZE_T`($tj)
	$PUSH	r30,`-2*$SIZE_T`($tj)
	$PUSH	r31,`-1*$SIZE_T`($tj)

	$LD	$n0,0($n0)	; pull n0[0] value
	addi	$num,$num,-2	; adjust $num for counter register

	$LD	$m0,0($bp)	; m0=bp[0]
	$LD	$aj,0($ap)	; ap[0]
	addi	$tp,$sp,$LOCALS
	$UMULL	$lo0,$aj,$m0	; ap[0]*bp[0]
	$UMULH	$hi0,$aj,$m0

	$LD	$aj,$BNSZ($ap)	; ap[1]
	$LD	$nj,0($np)	; np[0]

	$UMULL	$m1,$lo0,$n0	; "tp[0]"*n0

	$UMULL	$alo,$aj,$m0	; ap[1]*bp[0]
	$UMULH	$ahi,$aj,$m0

	$UMULL	$lo1,$nj,$m1	; np[0]*m1
	$UMULH	$hi1,$nj,$m1
	$LD	$nj,$BNSZ($np)	; np[1]
	addc	$lo1,$lo1,$lo0
	addze	$hi1,$hi1

	$UMULL	$nlo,$nj,$m1	; np[1]*m1
	$UMULH	$nhi,$nj,$m1

	mtctr	$num
	li	$j,`2*$BNSZ`
.align	4
L1st:
	$LDX	$aj,$ap,$j	; ap[j]
	addc	$lo0,$alo,$hi0
	$LDX	$nj,$np,$j	; np[j]
	addze	$hi0,$ahi
	$UMULL	$alo,$aj,$m0	; ap[j]*bp[0]
	addc	$lo1,$nlo,$hi1
	$UMULH	$ahi,$aj,$m0
	addze	$hi1,$nhi
	$UMULL	$nlo,$nj,$m1	; np[j]*m1
	addc	$lo1,$lo1,$lo0	; np[j]*m1+ap[j]*bp[0]
	$UMULH	$nhi,$nj,$m1
	addze	$hi1,$hi1
	$ST	$lo1,0($tp)	; tp[j-1]

	addi	$j,$j,$BNSZ	; j++
	addi	$tp,$tp,$BNSZ	; tp++
	bdnz	L1st
;L1st
	addc	$lo0,$alo,$hi0
	addze	$hi0,$ahi

	addc	$lo1,$nlo,$hi1
	addze	$hi1,$nhi
	addc	$lo1,$lo1,$lo0	; np[j]*m1+ap[j]*bp[0]
	addze	$hi1,$hi1
	$ST	$lo1,0($tp)	; tp[j-1]

	li	$ovf,0
	addc	$hi1,$hi1,$hi0
	addze	$ovf,$ovf	; upmost overflow bit
	$ST	$hi1,$BNSZ($tp)

	li	$i,$BNSZ
.align	4
Louter:
	$LDX	$m0,$bp,$i	; m0=bp[i]
	$LD	$aj,0($ap)	; ap[0]
	addi	$tp,$sp,$LOCALS
	$LD	$tj,$LOCALS($sp); tp[0]
	$UMULL	$lo0,$aj,$m0	; ap[0]*bp[i]
	$UMULH	$hi0,$aj,$m0
	$LD	$aj,$BNSZ($ap)	; ap[1]
	$LD	$nj,0($np)	; np[0]
	addc	$lo0,$lo0,$tj	; ap[0]*bp[i]+tp[0]
	$UMULL	$alo,$aj,$m0	; ap[j]*bp[i]
	addze	$hi0,$hi0
	$UMULL	$m1,$lo0,$n0	; tp[0]*n0
	$UMULH	$ahi,$aj,$m0
	$UMULL	$lo1,$nj,$m1	; np[0]*m1
	$UMULH	$hi1,$nj,$m1
	$LD	$nj,$BNSZ($np)	; np[1]
	addc	$lo1,$lo1,$lo0
	$UMULL	$nlo,$nj,$m1	; np[1]*m1
	addze	$hi1,$hi1
	$UMULH	$nhi,$nj,$m1

	mtctr	$num
	li	$j,`2*$BNSZ`
.align	4
Linner:
	$LDX	$aj,$ap,$j	; ap[j]
	addc	$lo0,$alo,$hi0
	$LD	$tj,$BNSZ($tp)	; tp[j]
	addze	$hi0,$ahi
	$LDX	$nj,$np,$j	; np[j]
	addc	$lo1,$nlo,$hi1
	$UMULL	$alo,$aj,$m0	; ap[j]*bp[i]
	addze	$hi1,$nhi
	$UMULH	$ahi,$aj,$m0
	addc	$lo0,$lo0,$tj	; ap[j]*bp[i]+tp[j]
	$UMULL	$nlo,$nj,$m1	; np[j]*m1
	addze	$hi0,$hi0
	$UMULH	$nhi,$nj,$m1
	addc	$lo1,$lo1,$lo0	; np[j]*m1+ap[j]*bp[i]+tp[j]
	addi	$j,$j,$BNSZ	; j++
	addze	$hi1,$hi1
	$ST	$lo1,0($tp)	; tp[j-1]
	addi	$tp,$tp,$BNSZ	; tp++
	bdnz	Linner
;Linner
	$LD	$tj,$BNSZ($tp)	; tp[j]
	addc	$lo0,$alo,$hi0
	addze	$hi0,$ahi
	addc	$lo0,$lo0,$tj	; ap[j]*bp[i]+tp[j]
	addze	$hi0,$hi0

	addc	$lo1,$nlo,$hi1
	addze	$hi1,$nhi
	addc	$lo1,$lo1,$lo0	; np[j]*m1+ap[j]*bp[i]+tp[j]
	addze	$hi1,$hi1
	$ST	$lo1,0($tp)	; tp[j-1]

	addic	$ovf,$ovf,-1	; move upmost overflow to XER[CA]
	li	$ovf,0
	adde	$hi1,$hi1,$hi0
	addze	$ovf,$ovf
	$ST	$hi1,$BNSZ($tp)
;
	slwi	$tj,$num,`log($BNSZ)/log(2)`
	$UCMP	$i,$tj
	addi	$i,$i,$BNSZ
	ble	Louter

	addi	$num,$num,2	; restore $num
	subfc	$j,$j,$j	; j=0 and "clear" XER[CA]
	addi	$tp,$sp,$LOCALS
	mtctr	$num

.align	4
Lsub:	$LDX	$tj,$tp,$j
	$LDX	$nj,$np,$j
	subfe	$aj,$nj,$tj	; tp[j]-np[j]
	$STX	$aj,$rp,$j
	addi	$j,$j,$BNSZ
	bdnz	Lsub

	li	$j,0
	mtctr	$num
	subfe	$ovf,$j,$ovf	; handle upmost overflow bit

.align	4
Lcopy:				; conditional copy
	$LDX	$tj,$tp,$j
	$LDX	$aj,$rp,$j
	and	$tj,$tj,$ovf
	andc	$aj,$aj,$ovf
	$STX	$j,$tp,$j	; zap at once
	or	$aj,$aj,$tj
	$STX	$aj,$rp,$j
	addi	$j,$j,$BNSZ
	bdnz	Lcopy

	$POP	$tj,0($sp)
	li	r3,1
	$POP	r20,`-12*$SIZE_T`($tj)
	$POP	r21,`-11*$SIZE_T`($tj)
	$POP	r22,`-10*$SIZE_T`($tj)
	$POP	r23,`-9*$SIZE_T`($tj)
	$POP	r24,`-8*$SIZE_T`($tj)
	$POP	r25,`-7*$SIZE_T`($tj)
	$POP	r26,`-6*$SIZE_T`($tj)
	$POP	r27,`-5*$SIZE_T`($tj)
	$POP	r28,`-4*$SIZE_T`($tj)
	$POP	r29,`-3*$SIZE_T`($tj)
	$POP	r30,`-2*$SIZE_T`($tj)
	$POP	r31,`-1*$SIZE_T`($tj)
	mr	$sp,$tj
	blr
	.long	0
	.byte	0,12,4,0,0x80,12,6,0
	.long	0
.size	.bn_mul_mont_int,.-.bn_mul_mont_int
___
}
if (1) {
my ($a0,$a1,$a2,$a3,
    $t0,$t1,$t2,$t3,
    $m0,$m1,$m2,$m3,
    $acc0,$acc1,$acc2,$acc3,$acc4,
    $bi,$mi,$tp,$ap_end,$cnt) = map("r$_",(9..12,14..31));
my  ($carry,$zero) = ($rp,"r0");

# sp----------->+-------------------------------+
#		| saved sp			|
#		+-------------------------------+
#		.				.
# +8*size_t	+-------------------------------+
#		| 4 "n0*t0"			|
#		.				.
#		.				.
# +12*size_t	+-------------------------------+
#		| size_t tmp[num]		|
#		.				.
#		.				.
#		.				.
#		+-------------------------------+
#		| topmost carry			|
#		.				.
# -18*size_t	+-------------------------------+
#		| 18 saved gpr, r14-r31		|
#		.				.
#		.				.
#		+-------------------------------+
$code.=<<___;
.globl	.bn_mul4x_mont_int
.align	5
.bn_mul4x_mont_int:
	andi.	r0,$num,7
	bne	.Lmul4x_do
	$UCMP	$ap,$bp
	bne	.Lmul4x_do
	b	.Lsqr8x_do
.Lmul4x_do:
	slwi	$num,$num,`log($SIZE_T)/log(2)`
	mr	$a0,$sp
	li	$a1,-32*$SIZE_T
	sub	$a1,$a1,$num
	$STUX	$sp,$sp,$a1		# alloca

	$PUSH	r14,-$SIZE_T*18($a0)
	$PUSH	r15,-$SIZE_T*17($a0)
	$PUSH	r16,-$SIZE_T*16($a0)
	$PUSH	r17,-$SIZE_T*15($a0)
	$PUSH	r18,-$SIZE_T*14($a0)
	$PUSH	r19,-$SIZE_T*13($a0)
	$PUSH	r20,-$SIZE_T*12($a0)
	$PUSH	r21,-$SIZE_T*11($a0)
	$PUSH	r22,-$SIZE_T*10($a0)
	$PUSH	r23,-$SIZE_T*9($a0)
	$PUSH	r24,-$SIZE_T*8($a0)
	$PUSH	r25,-$SIZE_T*7($a0)
	$PUSH	r26,-$SIZE_T*6($a0)
	$PUSH	r27,-$SIZE_T*5($a0)
	$PUSH	r28,-$SIZE_T*4($a0)
	$PUSH	r29,-$SIZE_T*3($a0)
	$PUSH	r30,-$SIZE_T*2($a0)
	$PUSH	r31,-$SIZE_T*1($a0)

	subi	$ap,$ap,$SIZE_T		# bias by -1
	subi	$np,$np,$SIZE_T		# bias by -1
	subi	$rp,$rp,$SIZE_T		# bias by -1
	$LD	$n0,0($n0)		# *n0

	add	$t0,$bp,$num
	add	$ap_end,$ap,$num
	subi	$t0,$t0,$SIZE_T*4	# &b[num-4]

	$LD	$bi,$SIZE_T*0($bp)	# b[0]
	li	$acc0,0
	$LD	$a0,$SIZE_T*1($ap)	# a[0..3]
	li	$acc1,0
	$LD	$a1,$SIZE_T*2($ap)
	li	$acc2,0
	$LD	$a2,$SIZE_T*3($ap)
	li	$acc3,0
	$LDU	$a3,$SIZE_T*4($ap)
	$LD	$m0,$SIZE_T*1($np)	# n[0..3]
	$LD	$m1,$SIZE_T*2($np)
	$LD	$m2,$SIZE_T*3($np)
	$LDU	$m3,$SIZE_T*4($np)

	$PUSH	$rp,$SIZE_T*6($sp)	# offload rp and &b[num-4]
	$PUSH	$t0,$SIZE_T*7($sp)
	li	$carry,0
	addic	$tp,$sp,$SIZE_T*7	# &t[-1], clear carry bit
	li	$cnt,0
	li	$zero,0
	b	.Loop_mul4x_1st_reduction

.align	5
.Loop_mul4x_1st_reduction:
	$UMULL	$t0,$a0,$bi		# lo(a[0..3]*b[0])
	addze	$carry,$carry		# modulo-scheduled
	$UMULL	$t1,$a1,$bi
	addi	$cnt,$cnt,$SIZE_T
	$UMULL	$t2,$a2,$bi
	andi.	$cnt,$cnt,$SIZE_T*4-1
	$UMULL	$t3,$a3,$bi
	addc	$acc0,$acc0,$t0
	$UMULH	$t0,$a0,$bi		# hi(a[0..3]*b[0])
	adde	$acc1,$acc1,$t1
	$UMULH	$t1,$a1,$bi
	adde	$acc2,$acc2,$t2
	$UMULL	$mi,$acc0,$n0		# t[0]*n0
	adde	$acc3,$acc3,$t3
	$UMULH	$t2,$a2,$bi
	addze	$acc4,$zero
	$UMULH	$t3,$a3,$bi
	$LDX	$bi,$bp,$cnt		# next b[i] (or b[0])
	addc	$acc1,$acc1,$t0
	# (*)	mul	$t0,$m0,$mi	# lo(n[0..3]*t[0]*n0)
	$STU	$mi,$SIZE_T($tp)	# put aside t[0]*n0 for tail processing
	adde	$acc2,$acc2,$t1
	$UMULL	$t1,$m1,$mi
	adde	$acc3,$acc3,$t2
	$UMULL	$t2,$m2,$mi
	adde	$acc4,$acc4,$t3		# can't overflow
	$UMULL	$t3,$m3,$mi
	# (*)	addc	$acc0,$acc0,$t0
	# (*)	As for removal of first multiplication and addition
	#	instructions. The outcome of first addition is
	#	guaranteed to be zero, which leaves two computationally
	#	significant outcomes: it either carries or not. Then
	#	question is when does it carry? Is there alternative
	#	way to deduce it? If you follow operations, you can
	#	observe that condition for carry is quite simple:
	#	$acc0 being non-zero. So that carry can be calculated
	#	by adding -1 to $acc0. That's what next instruction does.
	addic	$acc0,$acc0,-1		# (*), discarded
	$UMULH	$t0,$m0,$mi		# hi(n[0..3]*t[0]*n0)
	adde	$acc0,$acc1,$t1
	$UMULH	$t1,$m1,$mi
	adde	$acc1,$acc2,$t2
	$UMULH	$t2,$m2,$mi
	adde	$acc2,$acc3,$t3
	$UMULH	$t3,$m3,$mi
	adde	$acc3,$acc4,$carry
	addze	$carry,$zero
	addc	$acc0,$acc0,$t0
	adde	$acc1,$acc1,$t1
	adde	$acc2,$acc2,$t2
	adde	$acc3,$acc3,$t3
	#addze	$carry,$carry
	bne	.Loop_mul4x_1st_reduction

	$UCMP	$ap_end,$ap
	beq	.Lmul4x4_post_condition

	$LD	$a0,$SIZE_T*1($ap)	# a[4..7]
	$LD	$a1,$SIZE_T*2($ap)
	$LD	$a2,$SIZE_T*3($ap)
	$LDU	$a3,$SIZE_T*4($ap)
	$LD	$mi,$SIZE_T*8($sp)	# a[0]*n0
	$LD	$m0,$SIZE_T*1($np)	# n[4..7]
	$LD	$m1,$SIZE_T*2($np)
	$LD	$m2,$SIZE_T*3($np)
	$LDU	$m3,$SIZE_T*4($np)
	b	.Loop_mul4x_1st_tail

.align	5
.Loop_mul4x_1st_tail:
	$UMULL	$t0,$a0,$bi		# lo(a[4..7]*b[i])
	addze	$carry,$carry		# modulo-scheduled
	$UMULL	$t1,$a1,$bi
	addi	$cnt,$cnt,$SIZE_T
	$UMULL	$t2,$a2,$bi
	andi.	$cnt,$cnt,$SIZE_T*4-1
	$UMULL	$t3,$a3,$bi
	addc	$acc0,$acc0,$t0
	$UMULH	$t0,$a0,$bi		# hi(a[4..7]*b[i])
	adde	$acc1,$acc1,$t1
	$UMULH	$t1,$a1,$bi
	adde	$acc2,$acc2,$t2
	$UMULH	$t2,$a2,$bi
	adde	$acc3,$acc3,$t3
	$UMULH	$t3,$a3,$bi
	addze	$acc4,$zero
	$LDX	$bi,$bp,$cnt		# next b[i] (or b[0])
	addc	$acc1,$acc1,$t0
	$UMULL	$t0,$m0,$mi		# lo(n[4..7]*a[0]*n0)
	adde	$acc2,$acc2,$t1
	$UMULL	$t1,$m1,$mi
	adde	$acc3,$acc3,$t2
	$UMULL	$t2,$m2,$mi
	adde	$acc4,$acc4,$t3		# can't overflow
	$UMULL	$t3,$m3,$mi
	addc	$acc0,$acc0,$t0
	$UMULH	$t0,$m0,$mi		# hi(n[4..7]*a[0]*n0)
	adde	$acc1,$acc1,$t1
	$UMULH	$t1,$m1,$mi
	adde	$acc2,$acc2,$t2
	$UMULH	$t2,$m2,$mi
	adde	$acc3,$acc3,$t3
	adde	$acc4,$acc4,$carry
	$UMULH	$t3,$m3,$mi
	addze	$carry,$zero
	addi	$mi,$sp,$SIZE_T*8
	$LDX	$mi,$mi,$cnt		# next t[0]*n0
	$STU	$acc0,$SIZE_T($tp)	# word of result
	addc	$acc0,$acc1,$t0
	adde	$acc1,$acc2,$t1
	adde	$acc2,$acc3,$t2
	adde	$acc3,$acc4,$t3
	#addze	$carry,$carry
	bne	.Loop_mul4x_1st_tail

	sub	$t1,$ap_end,$num	# rewinded $ap
	$UCMP	$ap_end,$ap		# done yet?
	beq	.Lmul4x_proceed

	$LD	$a0,$SIZE_T*1($ap)
	$LD	$a1,$SIZE_T*2($ap)
	$LD	$a2,$SIZE_T*3($ap)
	$LDU	$a3,$SIZE_T*4($ap)
	$LD	$m0,$SIZE_T*1($np)
	$LD	$m1,$SIZE_T*2($np)
	$LD	$m2,$SIZE_T*3($np)
	$LDU	$m3,$SIZE_T*4($np)
	b	.Loop_mul4x_1st_tail

.align	5
.Lmul4x_proceed:
	$LDU	$bi,$SIZE_T*4($bp)	# *++b
	addze	$carry,$carry		# topmost carry
	$LD	$a0,$SIZE_T*1($t1)
	$LD	$a1,$SIZE_T*2($t1)
	$LD	$a2,$SIZE_T*3($t1)
	$LD	$a3,$SIZE_T*4($t1)
	addi	$ap,$t1,$SIZE_T*4
	sub	$np,$np,$num		# rewind np

	$ST	$acc0,$SIZE_T*1($tp)	# result
	$ST	$acc1,$SIZE_T*2($tp)
	$ST	$acc2,$SIZE_T*3($tp)
	$ST	$acc3,$SIZE_T*4($tp)
	$ST	$carry,$SIZE_T*5($tp)	# save topmost carry
	$LD	$acc0,$SIZE_T*12($sp)	# t[0..3]
	$LD	$acc1,$SIZE_T*13($sp)
	$LD	$acc2,$SIZE_T*14($sp)
	$LD	$acc3,$SIZE_T*15($sp)

	$LD	$m0,$SIZE_T*1($np)	# n[0..3]
	$LD	$m1,$SIZE_T*2($np)
	$LD	$m2,$SIZE_T*3($np)
	$LDU	$m3,$SIZE_T*4($np)
	addic	$tp,$sp,$SIZE_T*7	# &t[-1], clear carry bit
	li	$carry,0
	b	.Loop_mul4x_reduction

.align	5
.Loop_mul4x_reduction:
	$UMULL	$t0,$a0,$bi		# lo(a[0..3]*b[4])
	addze	$carry,$carry		# modulo-scheduled
	$UMULL	$t1,$a1,$bi
	addi	$cnt,$cnt,$SIZE_T
	$UMULL	$t2,$a2,$bi
	andi.	$cnt,$cnt,$SIZE_T*4-1
	$UMULL	$t3,$a3,$bi
	addc	$acc0,$acc0,$t0
	$UMULH	$t0,$a0,$bi		# hi(a[0..3]*b[4])
	adde	$acc1,$acc1,$t1
	$UMULH	$t1,$a1,$bi
	adde	$acc2,$acc2,$t2
	$UMULL	$mi,$acc0,$n0		# t[0]*n0
	adde	$acc3,$acc3,$t3
	$UMULH	$t2,$a2,$bi
	addze	$acc4,$zero
	$UMULH	$t3,$a3,$bi
	$LDX	$bi,$bp,$cnt		# next b[i]
	addc	$acc1,$acc1,$t0
	# (*)	mul	$t0,$m0,$mi
	$STU	$mi,$SIZE_T($tp)	# put aside t[0]*n0 for tail processing
	adde	$acc2,$acc2,$t1
	$UMULL	$t1,$m1,$mi		# lo(n[0..3]*t[0]*n0
	adde	$acc3,$acc3,$t2
	$UMULL	$t2,$m2,$mi
	adde	$acc4,$acc4,$t3		# can't overflow
	$UMULL	$t3,$m3,$mi
	# (*)	addc	$acc0,$acc0,$t0
	addic	$acc0,$acc0,-1		# (*), discarded
	$UMULH	$t0,$m0,$mi		# hi(n[0..3]*t[0]*n0
	adde	$acc0,$acc1,$t1
	$UMULH	$t1,$m1,$mi
	adde	$acc1,$acc2,$t2
	$UMULH	$t2,$m2,$mi
	adde	$acc2,$acc3,$t3
	$UMULH	$t3,$m3,$mi
	adde	$acc3,$acc4,$carry
	addze	$carry,$zero
	addc	$acc0,$acc0,$t0
	adde	$acc1,$acc1,$t1
	adde	$acc2,$acc2,$t2
	adde	$acc3,$acc3,$t3
	#addze	$carry,$carry
	bne	.Loop_mul4x_reduction

	$LD	$t0,$SIZE_T*5($tp)	# t[4..7]
	addze	$carry,$carry
	$LD	$t1,$SIZE_T*6($tp)
	$LD	$t2,$SIZE_T*7($tp)
	$LD	$t3,$SIZE_T*8($tp)
	$LD	$a0,$SIZE_T*1($ap)	# a[4..7]
	$LD	$a1,$SIZE_T*2($ap)
	$LD	$a2,$SIZE_T*3($ap)
	$LDU	$a3,$SIZE_T*4($ap)
	addc	$acc0,$acc0,$t0
	adde	$acc1,$acc1,$t1
	adde	$acc2,$acc2,$t2
	adde	$acc3,$acc3,$t3
	#addze	$carry,$carry

	$LD	$mi,$SIZE_T*8($sp)	# t[0]*n0
	$LD	$m0,$SIZE_T*1($np)	# n[4..7]
	$LD	$m1,$SIZE_T*2($np)
	$LD	$m2,$SIZE_T*3($np)
	$LDU	$m3,$SIZE_T*4($np)
	b	.Loop_mul4x_tail

.align	5
.Loop_mul4x_tail:
	$UMULL	$t0,$a0,$bi		# lo(a[4..7]*b[4])
	addze	$carry,$carry		# modulo-scheduled
	$UMULL	$t1,$a1,$bi
	addi	$cnt,$cnt,$SIZE_T
	$UMULL	$t2,$a2,$bi
	andi.	$cnt,$cnt,$SIZE_T*4-1
	$UMULL	$t3,$a3,$bi
	addc	$acc0,$acc0,$t0
	$UMULH	$t0,$a0,$bi		# hi(a[4..7]*b[4])
	adde	$acc1,$acc1,$t1
	$UMULH	$t1,$a1,$bi
	adde	$acc2,$acc2,$t2
	$UMULH	$t2,$a2,$bi
	adde	$acc3,$acc3,$t3
	$UMULH	$t3,$a3,$bi
	addze	$acc4,$zero
	$LDX	$bi,$bp,$cnt		# next b[i]
	addc	$acc1,$acc1,$t0
	$UMULL	$t0,$m0,$mi		# lo(n[4..7]*t[0]*n0)
	adde	$acc2,$acc2,$t1
	$UMULL	$t1,$m1,$mi
	adde	$acc3,$acc3,$t2
	$UMULL	$t2,$m2,$mi
	adde	$acc4,$acc4,$t3		# can't overflow
	$UMULL	$t3,$m3,$mi
	addc	$acc0,$acc0,$t0
	$UMULH	$t0,$m0,$mi		# hi(n[4..7]*t[0]*n0)
	adde	$acc1,$acc1,$t1
	$UMULH	$t1,$m1,$mi
	adde	$acc2,$acc2,$t2
	$UMULH	$t2,$m2,$mi
	adde	$acc3,$acc3,$t3
	$UMULH	$t3,$m3,$mi
	adde	$acc4,$acc4,$carry
	addi	$mi,$sp,$SIZE_T*8
	$LDX	$mi,$mi,$cnt		# next a[0]*n0
	addze	$carry,$zero
	$STU	$acc0,$SIZE_T($tp)	# word of result
	addc	$acc0,$acc1,$t0
	adde	$acc1,$acc2,$t1
	adde	$acc2,$acc3,$t2
	adde	$acc3,$acc4,$t3
	#addze	$carry,$carry
	bne	.Loop_mul4x_tail

	$LD	$t0,$SIZE_T*5($tp)	# next t[i] or topmost carry
	sub	$t1,$np,$num		# rewinded np?
	addze	$carry,$carry
	$UCMP	$ap_end,$ap		# done yet?
	beq	.Loop_mul4x_break

	$LD	$t1,$SIZE_T*6($tp)
	$LD	$t2,$SIZE_T*7($tp)
	$LD	$t3,$SIZE_T*8($tp)
	$LD	$a0,$SIZE_T*1($ap)
	$LD	$a1,$SIZE_T*2($ap)
	$LD	$a2,$SIZE_T*3($ap)
	$LDU	$a3,$SIZE_T*4($ap)
	addc	$acc0,$acc0,$t0
	adde	$acc1,$acc1,$t1
	adde	$acc2,$acc2,$t2
	adde	$acc3,$acc3,$t3
	#addze	$carry,$carry

	$LD	$m0,$SIZE_T*1($np)	# n[4..7]
	$LD	$m1,$SIZE_T*2($np)
	$LD	$m2,$SIZE_T*3($np)
	$LDU	$m3,$SIZE_T*4($np)
	b	.Loop_mul4x_tail

.align	5
.Loop_mul4x_break:
	$POP	$t2,$SIZE_T*6($sp)	# pull rp and &b[num-4]
	$POP	$t3,$SIZE_T*7($sp)
	addc	$a0,$acc0,$t0		# accumulate topmost carry
	$LD	$acc0,$SIZE_T*12($sp)	# t[0..3]
	addze	$a1,$acc1
	$LD	$acc1,$SIZE_T*13($sp)
	addze	$a2,$acc2
	$LD	$acc2,$SIZE_T*14($sp)
	addze	$a3,$acc3
	$LD	$acc3,$SIZE_T*15($sp)
	addze	$carry,$carry		# topmost carry
	$ST	$a0,$SIZE_T*1($tp)	# result
	sub	$ap,$ap_end,$num	# rewind ap
	$ST	$a1,$SIZE_T*2($tp)
	$ST	$a2,$SIZE_T*3($tp)
	$ST	$a3,$SIZE_T*4($tp)
	$ST	$carry,$SIZE_T*5($tp)	# store topmost carry

	$LD	$m0,$SIZE_T*1($t1)	# n[0..3]
	$LD	$m1,$SIZE_T*2($t1)
	$LD	$m2,$SIZE_T*3($t1)
	$LD	$m3,$SIZE_T*4($t1)
	addi	$np,$t1,$SIZE_T*4
	$UCMP	$bp,$t3			# done yet?
	beq	.Lmul4x_post

	$LDU	$bi,$SIZE_T*4($bp)
	$LD	$a0,$SIZE_T*1($ap)	# a[0..3]
	$LD	$a1,$SIZE_T*2($ap)
	$LD	$a2,$SIZE_T*3($ap)
	$LDU	$a3,$SIZE_T*4($ap)
	li	$carry,0
	addic	$tp,$sp,$SIZE_T*7	# &t[-1], clear carry bit
	b	.Loop_mul4x_reduction

.align	5
.Lmul4x_post:
	# Final step. We see if result is larger than modulus, and
	# if it is, subtract the modulus. But comparison implies
	# subtraction. So we subtract modulus, see if it borrowed,
	# and conditionally copy original value.
	srwi	$cnt,$num,`log($SIZE_T)/log(2)+2`
	mr	$bp,$t2			# &rp[-1]
	subi	$cnt,$cnt,1
	mr	$ap_end,$t2		# &rp[-1] copy
	subfc	$t0,$m0,$acc0
	addi	$tp,$sp,$SIZE_T*15
	subfe	$t1,$m1,$acc1

	mtctr	$cnt
.Lmul4x_sub:
	$LD	$m0,$SIZE_T*1($np)
	$LD	$acc0,$SIZE_T*1($tp)
	subfe	$t2,$m2,$acc2
	$LD	$m1,$SIZE_T*2($np)
	$LD	$acc1,$SIZE_T*2($tp)
	subfe	$t3,$m3,$acc3
	$LD	$m2,$SIZE_T*3($np)
	$LD	$acc2,$SIZE_T*3($tp)
	$LDU	$m3,$SIZE_T*4($np)
	$LDU	$acc3,$SIZE_T*4($tp)
	$ST	$t0,$SIZE_T*1($bp)
	$ST	$t1,$SIZE_T*2($bp)
	subfe	$t0,$m0,$acc0
	$ST	$t2,$SIZE_T*3($bp)
	$STU	$t3,$SIZE_T*4($bp)
	subfe	$t1,$m1,$acc1
	bdnz	.Lmul4x_sub

	 $LD	$a0,$SIZE_T*1($ap_end)
	$ST	$t0,$SIZE_T*1($bp)
	 $LD	$t0,$SIZE_T*12($sp)
	subfe	$t2,$m2,$acc2
	 $LD	$a1,$SIZE_T*2($ap_end)
	$ST	$t1,$SIZE_T*2($bp)
	 $LD	$t1,$SIZE_T*13($sp)
	subfe	$t3,$m3,$acc3
	subfe	$carry,$zero,$carry	# did it borrow?
	 addi	$tp,$sp,$SIZE_T*12
	 $LD	$a2,$SIZE_T*3($ap_end)
	$ST	$t2,$SIZE_T*3($bp)
	 $LD	$t2,$SIZE_T*14($sp)
	 $LD	$a3,$SIZE_T*4($ap_end)
	$ST	$t3,$SIZE_T*4($bp)
	 $LD	$t3,$SIZE_T*15($sp)

	mtctr	$cnt
.Lmul4x_cond_copy:
	and	$t0,$t0,$carry
	andc	$a0,$a0,$carry
	$ST	$zero,$SIZE_T*0($tp)	# wipe stack clean
	and	$t1,$t1,$carry
	andc	$a1,$a1,$carry
	$ST	$zero,$SIZE_T*1($tp)
	and	$t2,$t2,$carry
	andc	$a2,$a2,$carry
	$ST	$zero,$SIZE_T*2($tp)
	and	$t3,$t3,$carry
	andc	$a3,$a3,$carry
	$ST	$zero,$SIZE_T*3($tp)
	or	$acc0,$t0,$a0
	$LD	$a0,$SIZE_T*5($ap_end)
	$LD	$t0,$SIZE_T*4($tp)
	or	$acc1,$t1,$a1
	$LD	$a1,$SIZE_T*6($ap_end)
	$LD	$t1,$SIZE_T*5($tp)
	or	$acc2,$t2,$a2
	$LD	$a2,$SIZE_T*7($ap_end)
	$LD	$t2,$SIZE_T*6($tp)
	or	$acc3,$t3,$a3
	$LD	$a3,$SIZE_T*8($ap_end)
	$LD	$t3,$SIZE_T*7($tp)
	addi	$tp,$tp,$SIZE_T*4
	$ST	$acc0,$SIZE_T*1($ap_end)
	$ST	$acc1,$SIZE_T*2($ap_end)
	$ST	$acc2,$SIZE_T*3($ap_end)
	$STU	$acc3,$SIZE_T*4($ap_end)
	bdnz	.Lmul4x_cond_copy

	$POP	$bp,0($sp)		# pull saved sp
	and	$t0,$t0,$carry
	andc	$a0,$a0,$carry
	$ST	$zero,$SIZE_T*0($tp)
	and	$t1,$t1,$carry
	andc	$a1,$a1,$carry
	$ST	$zero,$SIZE_T*1($tp)
	and	$t2,$t2,$carry
	andc	$a2,$a2,$carry
	$ST	$zero,$SIZE_T*2($tp)
	and	$t3,$t3,$carry
	andc	$a3,$a3,$carry
	$ST	$zero,$SIZE_T*3($tp)
	or	$acc0,$t0,$a0
	or	$acc1,$t1,$a1
	$ST	$zero,$SIZE_T*4($tp)
	or	$acc2,$t2,$a2
	or	$acc3,$t3,$a3
	$ST	$acc0,$SIZE_T*1($ap_end)
	$ST	$acc1,$SIZE_T*2($ap_end)
	$ST	$acc2,$SIZE_T*3($ap_end)
	$ST	$acc3,$SIZE_T*4($ap_end)

	b	.Lmul4x_done

.align	4
.Lmul4x4_post_condition:
	$POP	$ap,$SIZE_T*6($sp)	# pull &rp[-1]
	$POP	$bp,0($sp)		# pull saved sp
	addze	$carry,$carry		# modulo-scheduled
	# $acc0-3,$carry hold result, $m0-3 hold modulus
	subfc	$a0,$m0,$acc0
	subfe	$a1,$m1,$acc1
	subfe	$a2,$m2,$acc2
	subfe	$a3,$m3,$acc3
	subfe	$carry,$zero,$carry	# did it borrow?

	and	$m0,$m0,$carry
	and	$m1,$m1,$carry
	addc	$a0,$a0,$m0
	and	$m2,$m2,$carry
	adde	$a1,$a1,$m1
	and	$m3,$m3,$carry
	adde	$a2,$a2,$m2
	adde	$a3,$a3,$m3

	$ST	$a0,$SIZE_T*1($ap)	# write result
	$ST	$a1,$SIZE_T*2($ap)
	$ST	$a2,$SIZE_T*3($ap)
	$ST	$a3,$SIZE_T*4($ap)

.Lmul4x_done:
	$ST	$zero,$SIZE_T*8($sp)	# wipe stack clean
	$ST	$zero,$SIZE_T*9($sp)
	$ST	$zero,$SIZE_T*10($sp)
	$ST	$zero,$SIZE_T*11($sp)
	li	r3,1			# signal "done"
	$POP	r14,-$SIZE_T*18($bp)
	$POP	r15,-$SIZE_T*17($bp)
	$POP	r16,-$SIZE_T*16($bp)
	$POP	r17,-$SIZE_T*15($bp)
	$POP	r18,-$SIZE_T*14($bp)
	$POP	r19,-$SIZE_T*13($bp)
	$POP	r20,-$SIZE_T*12($bp)
	$POP	r21,-$SIZE_T*11($bp)
	$POP	r22,-$SIZE_T*10($bp)
	$POP	r23,-$SIZE_T*9($bp)
	$POP	r24,-$SIZE_T*8($bp)
	$POP	r25,-$SIZE_T*7($bp)
	$POP	r26,-$SIZE_T*6($bp)
	$POP	r27,-$SIZE_T*5($bp)
	$POP	r28,-$SIZE_T*4($bp)
	$POP	r29,-$SIZE_T*3($bp)
	$POP	r30,-$SIZE_T*2($bp)
	$POP	r31,-$SIZE_T*1($bp)
	mr	$sp,$bp
	blr
	.long	0
	.byte	0,12,4,0x20,0x80,18,6,0
	.long	0
.size	.bn_mul4x_mont_int,.-.bn_mul4x_mont_int
___
}

if (1) {
########################################################################
# Following is PPC adaptation of sqrx8x_mont from x86_64-mont5 module.

my ($a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7)=map("r$_",(9..12,14..17));
my ($t0,$t1,$t2,$t3)=map("r$_",(18..21));
my ($acc0,$acc1,$acc2,$acc3,$acc4,$acc5,$acc6,$acc7)=map("r$_",(22..29));
my ($cnt,$carry,$zero)=("r30","r31","r0");
my ($tp,$ap_end,$na0)=($bp,$np,$carry);

# sp----------->+-------------------------------+
#		| saved sp			|
#		+-------------------------------+
#		.				.
# +12*size_t	+-------------------------------+
#		| size_t tmp[2*num]		|
#		.				.
#		.				.
#		.				.
#		+-------------------------------+
#		.				.
# -18*size_t	+-------------------------------+
#		| 18 saved gpr, r14-r31		|
#		.				.
#		.				.
#		+-------------------------------+
$code.=<<___;
.align	5
__bn_sqr8x_mont:
.Lsqr8x_do:
	mr	$a0,$sp
	slwi	$a1,$num,`log($SIZE_T)/log(2)+1`
	li	$a2,-32*$SIZE_T
	sub	$a1,$a2,$a1
	slwi	$num,$num,`log($SIZE_T)/log(2)`
	$STUX	$sp,$sp,$a1		# alloca

	$PUSH	r14,-$SIZE_T*18($a0)
	$PUSH	r15,-$SIZE_T*17($a0)
	$PUSH	r16,-$SIZE_T*16($a0)
	$PUSH	r17,-$SIZE_T*15($a0)
	$PUSH	r18,-$SIZE_T*14($a0)
	$PUSH	r19,-$SIZE_T*13($a0)
	$PUSH	r20,-$SIZE_T*12($a0)
	$PUSH	r21,-$SIZE_T*11($a0)
	$PUSH	r22,-$SIZE_T*10($a0)
	$PUSH	r23,-$SIZE_T*9($a0)
	$PUSH	r24,-$SIZE_T*8($a0)
	$PUSH	r25,-$SIZE_T*7($a0)
	$PUSH	r26,-$SIZE_T*6($a0)
	$PUSH	r27,-$SIZE_T*5($a0)
	$PUSH	r28,-$SIZE_T*4($a0)
	$PUSH	r29,-$SIZE_T*3($a0)
	$PUSH	r30,-$SIZE_T*2($a0)
	$PUSH	r31,-$SIZE_T*1($a0)

	subi	$ap,$ap,$SIZE_T		# bias by -1
	subi	$t0,$np,$SIZE_T		# bias by -1
	subi	$rp,$rp,$SIZE_T		# bias by -1
	$LD	$n0,0($n0)		# *n0
	li	$zero,0

	add	$ap_end,$ap,$num
	$LD	$a0,$SIZE_T*1($ap)
	#li	$acc0,0
	$LD	$a1,$SIZE_T*2($ap)
	li	$acc1,0
	$LD	$a2,$SIZE_T*3($ap)
	li	$acc2,0
	$LD	$a3,$SIZE_T*4($ap)
	li	$acc3,0
	$LD	$a4,$SIZE_T*5($ap)
	li	$acc4,0
	$LD	$a5,$SIZE_T*6($ap)
	li	$acc5,0
	$LD	$a6,$SIZE_T*7($ap)
	li	$acc6,0
	$LDU	$a7,$SIZE_T*8($ap)
	li	$acc7,0

	addi	$tp,$sp,$SIZE_T*11	# &tp[-1]
	subic.	$cnt,$num,$SIZE_T*8
	b	.Lsqr8x_zero_start

.align	5
.Lsqr8x_zero:
	subic.	$cnt,$cnt,$SIZE_T*8
	$ST	$zero,$SIZE_T*1($tp)
	$ST	$zero,$SIZE_T*2($tp)
	$ST	$zero,$SIZE_T*3($tp)
	$ST	$zero,$SIZE_T*4($tp)
	$ST	$zero,$SIZE_T*5($tp)
	$ST	$zero,$SIZE_T*6($tp)
	$ST	$zero,$SIZE_T*7($tp)
	$ST	$zero,$SIZE_T*8($tp)
.Lsqr8x_zero_start:
	$ST	$zero,$SIZE_T*9($tp)
	$ST	$zero,$SIZE_T*10($tp)
	$ST	$zero,$SIZE_T*11($tp)
	$ST	$zero,$SIZE_T*12($tp)
	$ST	$zero,$SIZE_T*13($tp)
	$ST	$zero,$SIZE_T*14($tp)
	$ST	$zero,$SIZE_T*15($tp)
	$STU	$zero,$SIZE_T*16($tp)
	bne	.Lsqr8x_zero

	$PUSH	$rp,$SIZE_T*6($sp)	# offload &rp[-1]
	$PUSH	$t0,$SIZE_T*7($sp)	# offload &np[-1]
	$PUSH	$n0,$SIZE_T*8($sp)	# offload n0
	$PUSH	$tp,$SIZE_T*9($sp)	# &tp[2*num-1]
	$PUSH	$zero,$SIZE_T*10($sp)	# initial top-most carry
	addi	$tp,$sp,$SIZE_T*11	# &tp[-1]

	# Multiply everything but a[i]*a[i]
.align	5
.Lsqr8x_outer_loop:
	#						  a[1]a[0]     (i)
	#					      a[2]a[0]
	#					  a[3]a[0]
	#				      a[4]a[0]
	#				  a[5]a[0]
	#			      a[6]a[0]
	#			  a[7]a[0]
	#					  a[2]a[1]	       (ii)
	#				      a[3]a[1]
	#				  a[4]a[1]
	#			      a[5]a[1]
	#			  a[6]a[1]
	#		      a[7]a[1]
	#				  a[3]a[2]		       (iii)
	#			      a[4]a[2]
	#			  a[5]a[2]
	#		      a[6]a[2]
	#		  a[7]a[2]
	#			  a[4]a[3]			       (iv)
	#		      a[5]a[3]
	#		  a[6]a[3]
	#	      a[7]a[3]
	#		  a[5]a[4]				       (v)
	#	      a[6]a[4]
	#	  a[7]a[4]
	#	  a[6]a[5]					       (vi)
	#     a[7]a[5]
	# a[7]a[6]						       (vii)

	$UMULL	$t0,$a1,$a0		# lo(a[1..7]*a[0])		(i)
	$UMULL	$t1,$a2,$a0
	$UMULL	$t2,$a3,$a0
	$UMULL	$t3,$a4,$a0
	addc	$acc1,$acc1,$t0		# t[1]+lo(a[1]*a[0])
	$UMULL	$t0,$a5,$a0
	adde	$acc2,$acc2,$t1
	$UMULL	$t1,$a6,$a0
	adde	$acc3,$acc3,$t2
	$UMULL	$t2,$a7,$a0
	adde	$acc4,$acc4,$t3
	$UMULH	$t3,$a1,$a0		# hi(a[1..7]*a[0])
	adde	$acc5,$acc5,$t0
	$UMULH	$t0,$a2,$a0
	adde	$acc6,$acc6,$t1
	$UMULH	$t1,$a3,$a0
	adde	$acc7,$acc7,$t2
	$UMULH	$t2,$a4,$a0
	$ST	$acc0,$SIZE_T*1($tp)	# t[0]
	addze	$acc0,$zero		# t[8]
	$ST	$acc1,$SIZE_T*2($tp)	# t[1]
	addc	$acc2,$acc2,$t3		# t[2]+lo(a[1]*a[0])
	$UMULH	$t3,$a5,$a0
	adde	$acc3,$acc3,$t0
	$UMULH	$t0,$a6,$a0
	adde	$acc4,$acc4,$t1
	$UMULH	$t1,$a7,$a0
	adde	$acc5,$acc5,$t2
	 $UMULL	$t2,$a2,$a1		# lo(a[2..7]*a[1])		(ii)
	adde	$acc6,$acc6,$t3
	 $UMULL	$t3,$a3,$a1
	adde	$acc7,$acc7,$t0
	 $UMULL	$t0,$a4,$a1
	adde	$acc0,$acc0,$t1

	$UMULL	$t1,$a5,$a1
	addc	$acc3,$acc3,$t2
	$UMULL	$t2,$a6,$a1
	adde	$acc4,$acc4,$t3
	$UMULL	$t3,$a7,$a1
	adde	$acc5,$acc5,$t0
	$UMULH	$t0,$a2,$a1		# hi(a[2..7]*a[1])
	adde	$acc6,$acc6,$t1
	$UMULH	$t1,$a3,$a1
	adde	$acc7,$acc7,$t2
	$UMULH	$t2,$a4,$a1
	adde	$acc0,$acc0,$t3
	$UMULH	$t3,$a5,$a1
	$ST	$acc2,$SIZE_T*3($tp)	# t[2]
	addze	$acc1,$zero		# t[9]
	$ST	$acc3,$SIZE_T*4($tp)	# t[3]
	addc	$acc4,$acc4,$t0
	$UMULH	$t0,$a6,$a1
	adde	$acc5,$acc5,$t1
	$UMULH	$t1,$a7,$a1
	adde	$acc6,$acc6,$t2
	 $UMULL	$t2,$a3,$a2		# lo(a[3..7]*a[2])		(iii)
	adde	$acc7,$acc7,$t3
	 $UMULL	$t3,$a4,$a2
	adde	$acc0,$acc0,$t0
	 $UMULL	$t0,$a5,$a2
	adde	$acc1,$acc1,$t1

	$UMULL	$t1,$a6,$a2
	addc	$acc5,$acc5,$t2
	$UMULL	$t2,$a7,$a2
	adde	$acc6,$acc6,$t3
	$UMULH	$t3,$a3,$a2		# hi(a[3..7]*a[2])
	adde	$acc7,$acc7,$t0
	$UMULH	$t0,$a4,$a2
	adde	$acc0,$acc0,$t1
	$UMULH	$t1,$a5,$a2
	adde	$acc1,$acc1,$t2
	$UMULH	$t2,$a6,$a2
	$ST	$acc4,$SIZE_T*5($tp)	# t[4]
	addze	$acc2,$zero		# t[10]
	$ST	$acc5,$SIZE_T*6($tp)	# t[5]
	addc	$acc6,$acc6,$t3
	$UMULH	$t3,$a7,$a2
	adde	$acc7,$acc7,$t0
	 $UMULL	$t0,$a4,$a3		# lo(a[4..7]*a[3])		(iv)
	adde	$acc0,$acc0,$t1
	 $UMULL	$t1,$a5,$a3
	adde	$acc1,$acc1,$t2
	 $UMULL	$t2,$a6,$a3
	adde	$acc2,$acc2,$t3

	$UMULL	$t3,$a7,$a3
	addc	$acc7,$acc7,$t0
	$UMULH	$t0,$a4,$a3		# hi(a[4..7]*a[3])
	adde	$acc0,$acc0,$t1
	$UMULH	$t1,$a5,$a3
	adde	$acc1,$acc1,$t2
	$UMULH	$t2,$a6,$a3
	adde	$acc2,$acc2,$t3
	$UMULH	$t3,$a7,$a3
	$ST	$acc6,$SIZE_T*7($tp)	# t[6]
	addze	$acc3,$zero		# t[11]
	$STU	$acc7,$SIZE_T*8($tp)	# t[7]
	addc	$acc0,$acc0,$t0
	 $UMULL	$t0,$a5,$a4		# lo(a[5..7]*a[4])		(v)
	adde	$acc1,$acc1,$t1
	 $UMULL	$t1,$a6,$a4
	adde	$acc2,$acc2,$t2
	 $UMULL	$t2,$a7,$a4
	adde	$acc3,$acc3,$t3

	$UMULH	$t3,$a5,$a4		# hi(a[5..7]*a[4])
	addc	$acc1,$acc1,$t0
	$UMULH	$t0,$a6,$a4
	adde	$acc2,$acc2,$t1
	$UMULH	$t1,$a7,$a4
	adde	$acc3,$acc3,$t2
	 $UMULL	$t2,$a6,$a5		# lo(a[6..7]*a[5])		(vi)
	addze	$acc4,$zero		# t[12]
	addc	$acc2,$acc2,$t3
	 $UMULL	$t3,$a7,$a5
	adde	$acc3,$acc3,$t0
	 $UMULH	$t0,$a6,$a5		# hi(a[6..7]*a[5])
	adde	$acc4,$acc4,$t1

	$UMULH	$t1,$a7,$a5
	addc	$acc3,$acc3,$t2
	 $UMULL	$t2,$a7,$a6		# lo(a[7]*a[6])			(vii)
	adde	$acc4,$acc4,$t3
	 $UMULH	$t3,$a7,$a6		# hi(a[7]*a[6])
	addze	$acc5,$zero		# t[13]
	addc	$acc4,$acc4,$t0
	$UCMP	$ap_end,$ap		# done yet?
	adde	$acc5,$acc5,$t1

	addc	$acc5,$acc5,$t2
	sub	$t0,$ap_end,$num	# rewinded ap
	addze	$acc6,$zero		# t[14]
	add	$acc6,$acc6,$t3

	beq	.Lsqr8x_outer_break

	mr	$n0,$a0
	$LD	$a0,$SIZE_T*1($tp)
	$LD	$a1,$SIZE_T*2($tp)
	$LD	$a2,$SIZE_T*3($tp)
	$LD	$a3,$SIZE_T*4($tp)
	$LD	$a4,$SIZE_T*5($tp)
	$LD	$a5,$SIZE_T*6($tp)
	$LD	$a6,$SIZE_T*7($tp)
	$LD	$a7,$SIZE_T*8($tp)
	addc	$acc0,$acc0,$a0
	$LD	$a0,$SIZE_T*1($ap)
	adde	$acc1,$acc1,$a1
	$LD	$a1,$SIZE_T*2($ap)
	adde	$acc2,$acc2,$a2
	$LD	$a2,$SIZE_T*3($ap)
	adde	$acc3,$acc3,$a3
	$LD	$a3,$SIZE_T*4($ap)
	adde	$acc4,$acc4,$a4
	$LD	$a4,$SIZE_T*5($ap)
	adde	$acc5,$acc5,$a5
	$LD	$a5,$SIZE_T*6($ap)
	adde	$acc6,$acc6,$a6
	$LD	$a6,$SIZE_T*7($ap)
	subi	$rp,$ap,$SIZE_T*7
	addze	$acc7,$a7
	$LDU	$a7,$SIZE_T*8($ap)
	#addze	$carry,$zero		# moved below
	li	$cnt,0
	b	.Lsqr8x_mul

	#                                                          a[8]a[0]
	#                                                      a[9]a[0]
	#                                                  a[a]a[0]
	#                                              a[b]a[0]
	#                                          a[c]a[0]
	#                                      a[d]a[0]
	#                                  a[e]a[0]
	#                              a[f]a[0]
	#                                                      a[8]a[1]
	#                          a[f]a[1]........................
	#                                                  a[8]a[2]
	#                      a[f]a[2]........................
	#                                              a[8]a[3]
	#                  a[f]a[3]........................
	#                                          a[8]a[4]
	#              a[f]a[4]........................
	#                                      a[8]a[5]
	#          a[f]a[5]........................
	#                                  a[8]a[6]
	#      a[f]a[6]........................
	#                              a[8]a[7]
	#  a[f]a[7]........................
.align	5
.Lsqr8x_mul:
	$UMULL	$t0,$a0,$n0
	addze	$carry,$zero		# carry bit, modulo-scheduled
	$UMULL	$t1,$a1,$n0
	addi	$cnt,$cnt,$SIZE_T
	$UMULL	$t2,$a2,$n0
	andi.	$cnt,$cnt,$SIZE_T*8-1
	$UMULL	$t3,$a3,$n0
	addc	$acc0,$acc0,$t0
	$UMULL	$t0,$a4,$n0
	adde	$acc1,$acc1,$t1
	$UMULL	$t1,$a5,$n0
	adde	$acc2,$acc2,$t2
	$UMULL	$t2,$a6,$n0
	adde	$acc3,$acc3,$t3
	$UMULL	$t3,$a7,$n0
	adde	$acc4,$acc4,$t0
	$UMULH	$t0,$a0,$n0
	adde	$acc5,$acc5,$t1
	$UMULH	$t1,$a1,$n0
	adde	$acc6,$acc6,$t2
	$UMULH	$t2,$a2,$n0
	adde	$acc7,$acc7,$t3
	$UMULH	$t3,$a3,$n0
	addze	$carry,$carry
	$STU	$acc0,$SIZE_T($tp)
	addc	$acc0,$acc1,$t0
	$UMULH	$t0,$a4,$n0
	adde	$acc1,$acc2,$t1
	$UMULH	$t1,$a5,$n0
	adde	$acc2,$acc3,$t2
	$UMULH	$t2,$a6,$n0
	adde	$acc3,$acc4,$t3
	$UMULH	$t3,$a7,$n0
	$LDX	$n0,$rp,$cnt
	adde	$acc4,$acc5,$t0
	adde	$acc5,$acc6,$t1
	adde	$acc6,$acc7,$t2
	adde	$acc7,$carry,$t3
	#addze	$carry,$zero		# moved above
	bne	.Lsqr8x_mul
					# note that carry flag is guaranteed
					# to be zero at this point
	$UCMP	$ap,$ap_end		# done yet?
	beq	.Lsqr8x_break

	$LD	$a0,$SIZE_T*1($tp)
	$LD	$a1,$SIZE_T*2($tp)
	$LD	$a2,$SIZE_T*3($tp)
	$LD	$a3,$SIZE_T*4($tp)
	$LD	$a4,$SIZE_T*5($tp)
	$LD	$a5,$SIZE_T*6($tp)
	$LD	$a6,$SIZE_T*7($tp)
	$LD	$a7,$SIZE_T*8($tp)
	addc	$acc0,$acc0,$a0
	$LD	$a0,$SIZE_T*1($ap)
	adde	$acc1,$acc1,$a1
	$LD	$a1,$SIZE_T*2($ap)
	adde	$acc2,$acc2,$a2
	$LD	$a2,$SIZE_T*3($ap)
	adde	$acc3,$acc3,$a3
	$LD	$a3,$SIZE_T*4($ap)
	adde	$acc4,$acc4,$a4
	$LD	$a4,$SIZE_T*5($ap)
	adde	$acc5,$acc5,$a5
	$LD	$a5,$SIZE_T*6($ap)
	adde	$acc6,$acc6,$a6
	$LD	$a6,$SIZE_T*7($ap)
	adde	$acc7,$acc7,$a7
	$LDU	$a7,$SIZE_T*8($ap)
	#addze	$carry,$zero		# moved above
	b	.Lsqr8x_mul

.align	5
.Lsqr8x_break:
	$LD	$a0,$SIZE_T*8($rp)
	addi	$ap,$rp,$SIZE_T*15
	$LD	$a1,$SIZE_T*9($rp)
	sub.	$t0,$ap_end,$ap		# is it last iteration?
	$LD	$a2,$SIZE_T*10($rp)
	sub	$t1,$tp,$t0
	$LD	$a3,$SIZE_T*11($rp)
	$LD	$a4,$SIZE_T*12($rp)
	$LD	$a5,$SIZE_T*13($rp)
	$LD	$a6,$SIZE_T*14($rp)
	$LD	$a7,$SIZE_T*15($rp)
	beq	.Lsqr8x_outer_loop

	$ST	$acc0,$SIZE_T*1($tp)
	$LD	$acc0,$SIZE_T*1($t1)
	$ST	$acc1,$SIZE_T*2($tp)
	$LD	$acc1,$SIZE_T*2($t1)
	$ST	$acc2,$SIZE_T*3($tp)
	$LD	$acc2,$SIZE_T*3($t1)
	$ST	$acc3,$SIZE_T*4($tp)
	$LD	$acc3,$SIZE_T*4($t1)
	$ST	$acc4,$SIZE_T*5($tp)
	$LD	$acc4,$SIZE_T*5($t1)
	$ST	$acc5,$SIZE_T*6($tp)
	$LD	$acc5,$SIZE_T*6($t1)
	$ST	$acc6,$SIZE_T*7($tp)
	$LD	$acc6,$SIZE_T*7($t1)
	$ST	$acc7,$SIZE_T*8($tp)
	$LD	$acc7,$SIZE_T*8($t1)
	mr	$tp,$t1
	b	.Lsqr8x_outer_loop

.align	5
.Lsqr8x_outer_break:
	####################################################################
	# Now multiply above result by 2 and add a[n-1]*a[n-1]|...|a[0]*a[0]
	$LD	$a1,$SIZE_T*1($t0)	# recall that $t0 is &a[-1]
	$LD	$a3,$SIZE_T*2($t0)
	$LD	$a5,$SIZE_T*3($t0)
	$LD	$a7,$SIZE_T*4($t0)
	addi	$ap,$t0,$SIZE_T*4
					# "tp[x]" comments are for num==8 case
	$LD	$t1,$SIZE_T*13($sp)	# =tp[1], t[0] is not interesting
	$LD	$t2,$SIZE_T*14($sp)
	$LD	$t3,$SIZE_T*15($sp)
	$LD	$t0,$SIZE_T*16($sp)

	$ST	$acc0,$SIZE_T*1($tp)	# tp[8]=
	srwi	$cnt,$num,`log($SIZE_T)/log(2)+2`
	$ST	$acc1,$SIZE_T*2($tp)
	subi	$cnt,$cnt,1
	$ST	$acc2,$SIZE_T*3($tp)
	$ST	$acc3,$SIZE_T*4($tp)
	$ST	$acc4,$SIZE_T*5($tp)
	$ST	$acc5,$SIZE_T*6($tp)
	$ST	$acc6,$SIZE_T*7($tp)
	#$ST	$acc7,$SIZE_T*8($tp)	# tp[15] is not interesting
	addi	$tp,$sp,$SIZE_T*11	# &tp[-1]
	$UMULL	$acc0,$a1,$a1
	$UMULH	$a1,$a1,$a1
	add	$acc1,$t1,$t1		# <<1
	$SHRI	$t1,$t1,$BITS-1
	$UMULL	$a2,$a3,$a3
	$UMULH	$a3,$a3,$a3
	addc	$acc1,$acc1,$a1
	add	$acc2,$t2,$t2
	$SHRI	$t2,$t2,$BITS-1
	add	$acc3,$t3,$t3
	$SHRI	$t3,$t3,$BITS-1
	or	$acc2,$acc2,$t1

	mtctr	$cnt
.Lsqr4x_shift_n_add:
	$UMULL	$a4,$a5,$a5
	$UMULH	$a5,$a5,$a5
	$LD	$t1,$SIZE_T*6($tp)	# =tp[5]
	$LD	$a1,$SIZE_T*1($ap)
	adde	$acc2,$acc2,$a2
	add	$acc4,$t0,$t0
	$SHRI	$t0,$t0,$BITS-1
	or	$acc3,$acc3,$t2
	$LD	$t2,$SIZE_T*7($tp)	# =tp[6]
	adde	$acc3,$acc3,$a3
	$LD	$a3,$SIZE_T*2($ap)
	add	$acc5,$t1,$t1
	$SHRI	$t1,$t1,$BITS-1
	or	$acc4,$acc4,$t3
	$LD	$t3,$SIZE_T*8($tp)	# =tp[7]
	$UMULL	$a6,$a7,$a7
	$UMULH	$a7,$a7,$a7
	adde	$acc4,$acc4,$a4
	add	$acc6,$t2,$t2
	$SHRI	$t2,$t2,$BITS-1
	or	$acc5,$acc5,$t0
	$LD	$t0,$SIZE_T*9($tp)	# =tp[8]
	adde	$acc5,$acc5,$a5
	$LD	$a5,$SIZE_T*3($ap)
	add	$acc7,$t3,$t3
	$SHRI	$t3,$t3,$BITS-1
	or	$acc6,$acc6,$t1
	$LD	$t1,$SIZE_T*10($tp)	# =tp[9]
	$UMULL	$a0,$a1,$a1
	$UMULH	$a1,$a1,$a1
	adde	$acc6,$acc6,$a6
	$ST	$acc0,$SIZE_T*1($tp)	# tp[0]=
	add	$acc0,$t0,$t0
	$SHRI	$t0,$t0,$BITS-1
	or	$acc7,$acc7,$t2
	$LD	$t2,$SIZE_T*11($tp)	# =tp[10]
	adde	$acc7,$acc7,$a7
	$LDU	$a7,$SIZE_T*4($ap)
	$ST	$acc1,$SIZE_T*2($tp)	# tp[1]=
	add	$acc1,$t1,$t1
	$SHRI	$t1,$t1,$BITS-1
	or	$acc0,$acc0,$t3
	$LD	$t3,$SIZE_T*12($tp)	# =tp[11]
	$UMULL	$a2,$a3,$a3
	$UMULH	$a3,$a3,$a3
	adde	$acc0,$acc0,$a0
	$ST	$acc2,$SIZE_T*3($tp)	# tp[2]=
	add	$acc2,$t2,$t2
	$SHRI	$t2,$t2,$BITS-1
	or	$acc1,$acc1,$t0
	$LD	$t0,$SIZE_T*13($tp)	# =tp[12]
	adde	$acc1,$acc1,$a1
	$ST	$acc3,$SIZE_T*4($tp)	# tp[3]=
	$ST	$acc4,$SIZE_T*5($tp)	# tp[4]=
	$ST	$acc5,$SIZE_T*6($tp)	# tp[5]=
	$ST	$acc6,$SIZE_T*7($tp)	# tp[6]=
	$STU	$acc7,$SIZE_T*8($tp)	# tp[7]=
	add	$acc3,$t3,$t3
	$SHRI	$t3,$t3,$BITS-1
	or	$acc2,$acc2,$t1
	bdnz	.Lsqr4x_shift_n_add
___
my ($np,$np_end)=($ap,$ap_end);
$code.=<<___;
	 $POP	$np,$SIZE_T*7($sp)	# pull &np[-1] and n0
	 $POP	$n0,$SIZE_T*8($sp)

	$UMULL	$a4,$a5,$a5
	$UMULH	$a5,$a5,$a5
	$ST	$acc0,$SIZE_T*1($tp)	# tp[8]=
	 $LD	$acc0,$SIZE_T*12($sp)	# =tp[0]
	$LD	$t1,$SIZE_T*6($tp)	# =tp[13]
	adde	$acc2,$acc2,$a2
	add	$acc4,$t0,$t0
	$SHRI	$t0,$t0,$BITS-1
	or	$acc3,$acc3,$t2
	$LD	$t2,$SIZE_T*7($tp)	# =tp[14]
	adde	$acc3,$acc3,$a3
	add	$acc5,$t1,$t1
	$SHRI	$t1,$t1,$BITS-1
	or	$acc4,$acc4,$t3
	$UMULL	$a6,$a7,$a7
	$UMULH	$a7,$a7,$a7
	adde	$acc4,$acc4,$a4
	add	$acc6,$t2,$t2
	$SHRI	$t2,$t2,$BITS-1
	or	$acc5,$acc5,$t0
	$ST	$acc1,$SIZE_T*2($tp)	# tp[9]=
	 $LD	$acc1,$SIZE_T*13($sp)	# =tp[1]
	adde	$acc5,$acc5,$a5
	or	$acc6,$acc6,$t1
	 $LD	$a0,$SIZE_T*1($np)
	 $LD	$a1,$SIZE_T*2($np)
	adde	$acc6,$acc6,$a6
	 $LD	$a2,$SIZE_T*3($np)
	 $LD	$a3,$SIZE_T*4($np)
	adde	$acc7,$a7,$t2
	 $LD	$a4,$SIZE_T*5($np)
	 $LD	$a5,$SIZE_T*6($np)

	################################################################
	# Reduce by 8 limbs per iteration
	$UMULL	$na0,$n0,$acc0		# t[0]*n0
	li	$cnt,8
	$LD	$a6,$SIZE_T*7($np)
	add	$np_end,$np,$num
	$LDU	$a7,$SIZE_T*8($np)
	$ST	$acc2,$SIZE_T*3($tp)	# tp[10]=
	$LD	$acc2,$SIZE_T*14($sp)
	$ST	$acc3,$SIZE_T*4($tp)	# tp[11]=
	$LD	$acc3,$SIZE_T*15($sp)
	$ST	$acc4,$SIZE_T*5($tp)	# tp[12]=
	$LD	$acc4,$SIZE_T*16($sp)
	$ST	$acc5,$SIZE_T*6($tp)	# tp[13]=
	$LD	$acc5,$SIZE_T*17($sp)
	$ST	$acc6,$SIZE_T*7($tp)	# tp[14]=
	$LD	$acc6,$SIZE_T*18($sp)
	$ST	$acc7,$SIZE_T*8($tp)	# tp[15]=
	$LD	$acc7,$SIZE_T*19($sp)
	addi	$tp,$sp,$SIZE_T*11	# &tp[-1]
	mtctr	$cnt
	b	.Lsqr8x_reduction

.align	5
.Lsqr8x_reduction:
	# (*)	$UMULL	$t0,$a0,$na0	# lo(n[0-7])*lo(t[0]*n0)
	$UMULL	$t1,$a1,$na0
	$UMULL	$t2,$a2,$na0
	$STU	$na0,$SIZE_T($tp)	# put aside t[0]*n0 for tail processing
	$UMULL	$t3,$a3,$na0
	# (*)	addc	$acc0,$acc0,$t0
	addic	$acc0,$acc0,-1		# (*)
	$UMULL	$t0,$a4,$na0
	adde	$acc0,$acc1,$t1
	$UMULL	$t1,$a5,$na0
	adde	$acc1,$acc2,$t2
	$UMULL	$t2,$a6,$na0
	adde	$acc2,$acc3,$t3
	$UMULL	$t3,$a7,$na0
	adde	$acc3,$acc4,$t0
	$UMULH	$t0,$a0,$na0		# hi(n[0-7])*lo(t[0]*n0)
	adde	$acc4,$acc5,$t1
	$UMULH	$t1,$a1,$na0
	adde	$acc5,$acc6,$t2
	$UMULH	$t2,$a2,$na0
	adde	$acc6,$acc7,$t3
	$UMULH	$t3,$a3,$na0
	addze	$acc7,$zero
	addc	$acc0,$acc0,$t0
	$UMULH	$t0,$a4,$na0
	adde	$acc1,$acc1,$t1
	$UMULH	$t1,$a5,$na0
	adde	$acc2,$acc2,$t2
	$UMULH	$t2,$a6,$na0
	adde	$acc3,$acc3,$t3
	$UMULH	$t3,$a7,$na0
	$UMULL	$na0,$n0,$acc0		# next t[0]*n0
	adde	$acc4,$acc4,$t0
	adde	$acc5,$acc5,$t1
	adde	$acc6,$acc6,$t2
	adde	$acc7,$acc7,$t3
	bdnz	.Lsqr8x_reduction

	$LD	$t0,$SIZE_T*1($tp)
	$LD	$t1,$SIZE_T*2($tp)
	$LD	$t2,$SIZE_T*3($tp)
	$LD	$t3,$SIZE_T*4($tp)
	subi	$rp,$tp,$SIZE_T*7
	$UCMP	$np_end,$np		# done yet?
	addc	$acc0,$acc0,$t0
	$LD	$t0,$SIZE_T*5($tp)
	adde	$acc1,$acc1,$t1
	$LD	$t1,$SIZE_T*6($tp)
	adde	$acc2,$acc2,$t2
	$LD	$t2,$SIZE_T*7($tp)
	adde	$acc3,$acc3,$t3
	$LD	$t3,$SIZE_T*8($tp)
	adde	$acc4,$acc4,$t0
	adde	$acc5,$acc5,$t1
	adde	$acc6,$acc6,$t2
	adde	$acc7,$acc7,$t3
	#addze	$carry,$zero		# moved below
	beq	.Lsqr8x8_post_condition

	$LD	$n0,$SIZE_T*0($rp)
	$LD	$a0,$SIZE_T*1($np)
	$LD	$a1,$SIZE_T*2($np)
	$LD	$a2,$SIZE_T*3($np)
	$LD	$a3,$SIZE_T*4($np)
	$LD	$a4,$SIZE_T*5($np)
	$LD	$a5,$SIZE_T*6($np)
	$LD	$a6,$SIZE_T*7($np)
	$LDU	$a7,$SIZE_T*8($np)
	li	$cnt,0

.align	5
.Lsqr8x_tail:
	$UMULL	$t0,$a0,$n0
	addze	$carry,$zero		# carry bit, modulo-scheduled
	$UMULL	$t1,$a1,$n0
	addi	$cnt,$cnt,$SIZE_T
	$UMULL	$t2,$a2,$n0
	andi.	$cnt,$cnt,$SIZE_T*8-1
	$UMULL	$t3,$a3,$n0
	addc	$acc0,$acc0,$t0
	$UMULL	$t0,$a4,$n0
	adde	$acc1,$acc1,$t1
	$UMULL	$t1,$a5,$n0
	adde	$acc2,$acc2,$t2
	$UMULL	$t2,$a6,$n0
	adde	$acc3,$acc3,$t3
	$UMULL	$t3,$a7,$n0
	adde	$acc4,$acc4,$t0
	$UMULH	$t0,$a0,$n0
	adde	$acc5,$acc5,$t1
	$UMULH	$t1,$a1,$n0
	adde	$acc6,$acc6,$t2
	$UMULH	$t2,$a2,$n0
	adde	$acc7,$acc7,$t3
	$UMULH	$t3,$a3,$n0
	addze	$carry,$carry
	$STU	$acc0,$SIZE_T($tp)
	addc	$acc0,$acc1,$t0
	$UMULH	$t0,$a4,$n0
	adde	$acc1,$acc2,$t1
	$UMULH	$t1,$a5,$n0
	adde	$acc2,$acc3,$t2
	$UMULH	$t2,$a6,$n0
	adde	$acc3,$acc4,$t3
	$UMULH	$t3,$a7,$n0
	$LDX	$n0,$rp,$cnt
	adde	$acc4,$acc5,$t0
	adde	$acc5,$acc6,$t1
	adde	$acc6,$acc7,$t2
	adde	$acc7,$carry,$t3
	#addze	$carry,$zero		# moved above
	bne	.Lsqr8x_tail
					# note that carry flag is guaranteed
					# to be zero at this point
	$LD	$a0,$SIZE_T*1($tp)
	$POP	$carry,$SIZE_T*10($sp)	# pull top-most carry in case we break
	$UCMP	$np_end,$np		# done yet?
	$LD	$a1,$SIZE_T*2($tp)
	sub	$t2,$np_end,$num	# rewinded np
	$LD	$a2,$SIZE_T*3($tp)
	$LD	$a3,$SIZE_T*4($tp)
	$LD	$a4,$SIZE_T*5($tp)
	$LD	$a5,$SIZE_T*6($tp)
	$LD	$a6,$SIZE_T*7($tp)
	$LD	$a7,$SIZE_T*8($tp)
	beq	.Lsqr8x_tail_break

	addc	$acc0,$acc0,$a0
	$LD	$a0,$SIZE_T*1($np)
	adde	$acc1,$acc1,$a1
	$LD	$a1,$SIZE_T*2($np)
	adde	$acc2,$acc2,$a2
	$LD	$a2,$SIZE_T*3($np)
	adde	$acc3,$acc3,$a3
	$LD	$a3,$SIZE_T*4($np)
	adde	$acc4,$acc4,$a4
	$LD	$a4,$SIZE_T*5($np)
	adde	$acc5,$acc5,$a5
	$LD	$a5,$SIZE_T*6($np)
	adde	$acc6,$acc6,$a6
	$LD	$a6,$SIZE_T*7($np)
	adde	$acc7,$acc7,$a7
	$LDU	$a7,$SIZE_T*8($np)
	#addze	$carry,$zero		# moved above
	b	.Lsqr8x_tail

.align	5
.Lsqr8x_tail_break:
	$POP	$n0,$SIZE_T*8($sp)	# pull n0
	$POP	$t3,$SIZE_T*9($sp)	# &tp[2*num-1]
	addi	$cnt,$tp,$SIZE_T*8	# end of current t[num] window

	addic	$carry,$carry,-1	# "move" top-most carry to carry bit
	adde	$t0,$acc0,$a0
	$LD	$acc0,$SIZE_T*8($rp)
	$LD	$a0,$SIZE_T*1($t2)	# recall that $t2 is &n[-1]
	adde	$t1,$acc1,$a1
	$LD	$acc1,$SIZE_T*9($rp)
	$LD	$a1,$SIZE_T*2($t2)
	adde	$acc2,$acc2,$a2
	$LD	$a2,$SIZE_T*3($t2)
	adde	$acc3,$acc3,$a3
	$LD	$a3,$SIZE_T*4($t2)
	adde	$acc4,$acc4,$a4
	$LD	$a4,$SIZE_T*5($t2)
	adde	$acc5,$acc5,$a5
	$LD	$a5,$SIZE_T*6($t2)
	adde	$acc6,$acc6,$a6
	$LD	$a6,$SIZE_T*7($t2)
	adde	$acc7,$acc7,$a7
	$LD	$a7,$SIZE_T*8($t2)
	addi	$np,$t2,$SIZE_T*8
	addze	$t2,$zero		# top-most carry
	$UMULL	$na0,$n0,$acc0
	$ST	$t0,$SIZE_T*1($tp)
	$UCMP	$cnt,$t3		# did we hit the bottom?
	$ST	$t1,$SIZE_T*2($tp)
	li	$cnt,8
	$ST	$acc2,$SIZE_T*3($tp)
	$LD	$acc2,$SIZE_T*10($rp)
	$ST	$acc3,$SIZE_T*4($tp)
	$LD	$acc3,$SIZE_T*11($rp)
	$ST	$acc4,$SIZE_T*5($tp)
	$LD	$acc4,$SIZE_T*12($rp)
	$ST	$acc5,$SIZE_T*6($tp)
	$LD	$acc5,$SIZE_T*13($rp)
	$ST	$acc6,$SIZE_T*7($tp)
	$LD	$acc6,$SIZE_T*14($rp)
	$ST	$acc7,$SIZE_T*8($tp)
	$LD	$acc7,$SIZE_T*15($rp)
	$PUSH	$t2,$SIZE_T*10($sp)	# off-load top-most carry
	addi	$tp,$rp,$SIZE_T*7	# slide the window
	mtctr	$cnt
	bne	.Lsqr8x_reduction

	################################################################
	# Final step. We see if result is larger than modulus, and
	# if it is, subtract the modulus. But comparison implies
	# subtraction. So we subtract modulus, see if it borrowed,
	# and conditionally copy original value.
	$POP	$rp,$SIZE_T*6($sp)	# pull &rp[-1]
	srwi	$cnt,$num,`log($SIZE_T)/log(2)+3`
	mr	$n0,$tp			# put tp aside
	addi	$tp,$tp,$SIZE_T*8
	subi	$cnt,$cnt,1
	subfc	$t0,$a0,$acc0
	subfe	$t1,$a1,$acc1
	mr	$carry,$t2
	mr	$ap_end,$rp		# $rp copy

	mtctr	$cnt
	b	.Lsqr8x_sub

.align	5
.Lsqr8x_sub:
	$LD	$a0,$SIZE_T*1($np)
	$LD	$acc0,$SIZE_T*1($tp)
	$LD	$a1,$SIZE_T*2($np)
	$LD	$acc1,$SIZE_T*2($tp)
	subfe	$t2,$a2,$acc2
	$LD	$a2,$SIZE_T*3($np)
	$LD	$acc2,$SIZE_T*3($tp)
	subfe	$t3,$a3,$acc3
	$LD	$a3,$SIZE_T*4($np)
	$LD	$acc3,$SIZE_T*4($tp)
	$ST	$t0,$SIZE_T*1($rp)
	subfe	$t0,$a4,$acc4
	$LD	$a4,$SIZE_T*5($np)
	$LD	$acc4,$SIZE_T*5($tp)
	$ST	$t1,$SIZE_T*2($rp)
	subfe	$t1,$a5,$acc5
	$LD	$a5,$SIZE_T*6($np)
	$LD	$acc5,$SIZE_T*6($tp)
	$ST	$t2,$SIZE_T*3($rp)
	subfe	$t2,$a6,$acc6
	$LD	$a6,$SIZE_T*7($np)
	$LD	$acc6,$SIZE_T*7($tp)
	$ST	$t3,$SIZE_T*4($rp)
	subfe	$t3,$a7,$acc7
	$LDU	$a7,$SIZE_T*8($np)
	$LDU	$acc7,$SIZE_T*8($tp)
	$ST	$t0,$SIZE_T*5($rp)
	subfe	$t0,$a0,$acc0
	$ST	$t1,$SIZE_T*6($rp)
	subfe	$t1,$a1,$acc1
	$ST	$t2,$SIZE_T*7($rp)
	$STU	$t3,$SIZE_T*8($rp)
	bdnz	.Lsqr8x_sub

	srwi	$cnt,$num,`log($SIZE_T)/log(2)+2`
	 $LD	$a0,$SIZE_T*1($ap_end)	# original $rp
	 $LD	$acc0,$SIZE_T*1($n0)	# original $tp
	subi	$cnt,$cnt,1
	 $LD	$a1,$SIZE_T*2($ap_end)
	 $LD	$acc1,$SIZE_T*2($n0)
	subfe	$t2,$a2,$acc2
	 $LD	$a2,$SIZE_T*3($ap_end)
	 $LD	$acc2,$SIZE_T*3($n0)
	subfe	$t3,$a3,$acc3
	 $LD	$a3,$SIZE_T*4($ap_end)
	 $LDU	$acc3,$SIZE_T*4($n0)
	$ST	$t0,$SIZE_T*1($rp)
	subfe	$t0,$a4,$acc4
	$ST	$t1,$SIZE_T*2($rp)
	subfe	$t1,$a5,$acc5
	$ST	$t2,$SIZE_T*3($rp)
	subfe	$t2,$a6,$acc6
	$ST	$t3,$SIZE_T*4($rp)
	subfe	$t3,$a7,$acc7
	$ST	$t0,$SIZE_T*5($rp)
	subfe	$carry,$zero,$carry	# did it borrow?
	$ST	$t1,$SIZE_T*6($rp)
	$ST	$t2,$SIZE_T*7($rp)
	$ST	$t3,$SIZE_T*8($rp)

	addi	$tp,$sp,$SIZE_T*11
	mtctr	$cnt

.Lsqr4x_cond_copy:
	andc	$a0,$a0,$carry
	 $ST	$zero,-$SIZE_T*3($n0)	# wipe stack clean
	and	$acc0,$acc0,$carry
	 $ST	$zero,-$SIZE_T*2($n0)
	andc	$a1,$a1,$carry
	 $ST	$zero,-$SIZE_T*1($n0)
	and	$acc1,$acc1,$carry
	 $ST	$zero,-$SIZE_T*0($n0)
	andc	$a2,$a2,$carry
	 $ST	$zero,$SIZE_T*1($tp)
	and	$acc2,$acc2,$carry
	 $ST	$zero,$SIZE_T*2($tp)
	andc	$a3,$a3,$carry
	 $ST	$zero,$SIZE_T*3($tp)
	and	$acc3,$acc3,$carry
	 $STU	$zero,$SIZE_T*4($tp)
	or	$t0,$a0,$acc0
	$LD	$a0,$SIZE_T*5($ap_end)
	$LD	$acc0,$SIZE_T*1($n0)
	or	$t1,$a1,$acc1
	$LD	$a1,$SIZE_T*6($ap_end)
	$LD	$acc1,$SIZE_T*2($n0)
	or	$t2,$a2,$acc2
	$LD	$a2,$SIZE_T*7($ap_end)
	$LD	$acc2,$SIZE_T*3($n0)
	or	$t3,$a3,$acc3
	$LD	$a3,$SIZE_T*8($ap_end)
	$LDU	$acc3,$SIZE_T*4($n0)
	$ST	$t0,$SIZE_T*1($ap_end)
	$ST	$t1,$SIZE_T*2($ap_end)
	$ST	$t2,$SIZE_T*3($ap_end)
	$STU	$t3,$SIZE_T*4($ap_end)
	bdnz	.Lsqr4x_cond_copy

	$POP	$ap,0($sp)		# pull saved sp
	andc	$a0,$a0,$carry
	and	$acc0,$acc0,$carry
	andc	$a1,$a1,$carry
	and	$acc1,$acc1,$carry
	andc	$a2,$a2,$carry
	and	$acc2,$acc2,$carry
	andc	$a3,$a3,$carry
	and	$acc3,$acc3,$carry
	or	$t0,$a0,$acc0
	or	$t1,$a1,$acc1
	or	$t2,$a2,$acc2
	or	$t3,$a3,$acc3
	$ST	$t0,$SIZE_T*1($ap_end)
	$ST	$t1,$SIZE_T*2($ap_end)
	$ST	$t2,$SIZE_T*3($ap_end)
	$ST	$t3,$SIZE_T*4($ap_end)

	b	.Lsqr8x_done

.align	5
.Lsqr8x8_post_condition:
	$POP	$rp,$SIZE_T*6($sp)	# pull rp
	$POP	$ap,0($sp)		# pull saved sp
	addze	$carry,$zero

	# $acc0-7,$carry hold result, $a0-7 hold modulus
	subfc	$acc0,$a0,$acc0
	subfe	$acc1,$a1,$acc1
	 $ST	$zero,$SIZE_T*12($sp)	# wipe stack clean
	 $ST	$zero,$SIZE_T*13($sp)
	subfe	$acc2,$a2,$acc2
	 $ST	$zero,$SIZE_T*14($sp)
	 $ST	$zero,$SIZE_T*15($sp)
	subfe	$acc3,$a3,$acc3
	 $ST	$zero,$SIZE_T*16($sp)
	 $ST	$zero,$SIZE_T*17($sp)
	subfe	$acc4,$a4,$acc4
	 $ST	$zero,$SIZE_T*18($sp)
	 $ST	$zero,$SIZE_T*19($sp)
	subfe	$acc5,$a5,$acc5
	 $ST	$zero,$SIZE_T*20($sp)
	 $ST	$zero,$SIZE_T*21($sp)
	subfe	$acc6,$a6,$acc6
	 $ST	$zero,$SIZE_T*22($sp)
	 $ST	$zero,$SIZE_T*23($sp)
	subfe	$acc7,$a7,$acc7
	 $ST	$zero,$SIZE_T*24($sp)
	 $ST	$zero,$SIZE_T*25($sp)
	subfe	$carry,$zero,$carry	# did it borrow?
	 $ST	$zero,$SIZE_T*26($sp)
	 $ST	$zero,$SIZE_T*27($sp)

	and	$a0,$a0,$carry
	and	$a1,$a1,$carry
	addc	$acc0,$acc0,$a0		# add modulus back if borrowed
	and	$a2,$a2,$carry
	adde	$acc1,$acc1,$a1
	and	$a3,$a3,$carry
	adde	$acc2,$acc2,$a2
	and	$a4,$a4,$carry
	adde	$acc3,$acc3,$a3
	and	$a5,$a5,$carry
	adde	$acc4,$acc4,$a4
	and	$a6,$a6,$carry
	adde	$acc5,$acc5,$a5
	and	$a7,$a7,$carry
	adde	$acc6,$acc6,$a6
	adde	$acc7,$acc7,$a7
	$ST	$acc0,$SIZE_T*1($rp)
	$ST	$acc1,$SIZE_T*2($rp)
	$ST	$acc2,$SIZE_T*3($rp)
	$ST	$acc3,$SIZE_T*4($rp)
	$ST	$acc4,$SIZE_T*5($rp)
	$ST	$acc5,$SIZE_T*6($rp)
	$ST	$acc6,$SIZE_T*7($rp)
	$ST	$acc7,$SIZE_T*8($rp)

.Lsqr8x_done:
	$PUSH	$zero,$SIZE_T*8($sp)
	$PUSH	$zero,$SIZE_T*10($sp)

	$POP	r14,-$SIZE_T*18($ap)
	li	r3,1			# signal "done"
	$POP	r15,-$SIZE_T*17($ap)
	$POP	r16,-$SIZE_T*16($ap)
	$POP	r17,-$SIZE_T*15($ap)
	$POP	r18,-$SIZE_T*14($ap)
	$POP	r19,-$SIZE_T*13($ap)
	$POP	r20,-$SIZE_T*12($ap)
	$POP	r21,-$SIZE_T*11($ap)
	$POP	r22,-$SIZE_T*10($ap)
	$POP	r23,-$SIZE_T*9($ap)
	$POP	r24,-$SIZE_T*8($ap)
	$POP	r25,-$SIZE_T*7($ap)
	$POP	r26,-$SIZE_T*6($ap)
	$POP	r27,-$SIZE_T*5($ap)
	$POP	r28,-$SIZE_T*4($ap)
	$POP	r29,-$SIZE_T*3($ap)
	$POP	r30,-$SIZE_T*2($ap)
	$POP	r31,-$SIZE_T*1($ap)
	mr	$sp,$ap
	blr
	.long	0
	.byte	0,12,4,0x20,0x80,18,6,0
	.long	0
.size	__bn_sqr8x_mont,.-__bn_sqr8x_mont
___
}
$code.=<<___;
.asciz  "Montgomery Multiplication for PPC, CRYPTOGAMS by <appro\@openssl.org>"
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;

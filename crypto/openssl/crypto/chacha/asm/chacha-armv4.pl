#! /usr/bin/env perl
# Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
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
# December 2014
#
# ChaCha20 for ARMv4.
#
# Performance in cycles per byte out of large buffer.
#
#			IALU/gcc-4.4    1xNEON      3xNEON+1xIALU
#
# Cortex-A5		19.3(*)/+95%    21.8        14.1
# Cortex-A8		10.5(*)/+160%   13.9        6.35
# Cortex-A9		12.9(**)/+110%  14.3        6.50
# Cortex-A15		11.0/+40%       16.0        5.00
# Snapdragon S4		11.5/+125%      13.6        4.90
#
# (*)	most "favourable" result for aligned data on little-endian
#	processor, result for misaligned data is 10-15% lower;
# (**)	this result is a trade-off: it can be improved by 20%,
#	but then Snapdragon S4 and Cortex-A8 results get
#	20-25% worse;

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

sub AUTOLOAD()		# thunk [simplified] x86-style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://; $opcode =~ s/_/\./;
  my $arg = pop;
    $arg = "#$arg" if ($arg*1 eq $arg);
    $code .= "\t$opcode\t".join(',',@_,$arg)."\n";
}

my @x=map("r$_",(0..7,"x","x","x","x",12,"x",14,"x"));
my @t=map("r$_",(8..11));

sub ROUND {
my ($a0,$b0,$c0,$d0)=@_;
my ($a1,$b1,$c1,$d1)=map(($_&~3)+(($_+1)&3),($a0,$b0,$c0,$d0));
my ($a2,$b2,$c2,$d2)=map(($_&~3)+(($_+1)&3),($a1,$b1,$c1,$d1));
my ($a3,$b3,$c3,$d3)=map(($_&~3)+(($_+1)&3),($a2,$b2,$c2,$d2));
my $odd = $d0&1;
my ($xc,$xc_) = (@t[0..1]);
my ($xd,$xd_) = $odd ? (@t[2],@x[$d1]) : (@x[$d0],@t[2]);
my @ret;

	# Consider order in which variables are addressed by their
	# index:
	#
	#       a   b   c   d
	#
	#       0   4   8  12 < even round
	#       1   5   9  13
	#       2   6  10  14
	#       3   7  11  15
	#       0   5  10  15 < odd round
	#       1   6  11  12
	#       2   7   8  13
	#       3   4   9  14
	#
	# 'a', 'b' are permanently allocated in registers, @x[0..7],
	# while 'c's and pair of 'd's are maintained in memory. If
	# you observe 'c' column, you'll notice that pair of 'c's is
	# invariant between rounds. This means that we have to reload
	# them once per round, in the middle. This is why you'll see
	# bunch of 'c' stores and loads in the middle, but none in
	# the beginning or end. If you observe 'd' column, you'll
	# notice that 15 and 13 are reused in next pair of rounds.
	# This is why these two are chosen for offloading to memory,
	# to make loads count more.
							push @ret,(
	"&add	(@x[$a0],@x[$a0],@x[$b0])",
	"&mov	($xd,$xd,'ror#16')",
	 "&add	(@x[$a1],@x[$a1],@x[$b1])",
	 "&mov	($xd_,$xd_,'ror#16')",
	"&eor	($xd,$xd,@x[$a0],'ror#16')",
	 "&eor	($xd_,$xd_,@x[$a1],'ror#16')",

	"&add	($xc,$xc,$xd)",
	"&mov	(@x[$b0],@x[$b0],'ror#20')",
	 "&add	($xc_,$xc_,$xd_)",
	 "&mov	(@x[$b1],@x[$b1],'ror#20')",
	"&eor	(@x[$b0],@x[$b0],$xc,'ror#20')",
	 "&eor	(@x[$b1],@x[$b1],$xc_,'ror#20')",

	"&add	(@x[$a0],@x[$a0],@x[$b0])",
	"&mov	($xd,$xd,'ror#24')",
	 "&add	(@x[$a1],@x[$a1],@x[$b1])",
	 "&mov	($xd_,$xd_,'ror#24')",
	"&eor	($xd,$xd,@x[$a0],'ror#24')",
	 "&eor	($xd_,$xd_,@x[$a1],'ror#24')",

	"&add	($xc,$xc,$xd)",
	"&mov	(@x[$b0],@x[$b0],'ror#25')"		);
							push @ret,(
	"&str	($xd,'[sp,#4*(16+$d0)]')",
	"&ldr	($xd,'[sp,#4*(16+$d2)]')"		) if ($odd);
							push @ret,(
	 "&add	($xc_,$xc_,$xd_)",
	 "&mov	(@x[$b1],@x[$b1],'ror#25')"		);
							push @ret,(
	 "&str	($xd_,'[sp,#4*(16+$d1)]')",
	 "&ldr	($xd_,'[sp,#4*(16+$d3)]')"		) if (!$odd);
							push @ret,(
	"&eor	(@x[$b0],@x[$b0],$xc,'ror#25')",
	 "&eor	(@x[$b1],@x[$b1],$xc_,'ror#25')"	);

	$xd=@x[$d2]					if (!$odd);
	$xd_=@x[$d3]					if ($odd);
							push @ret,(
	"&str	($xc,'[sp,#4*(16+$c0)]')",
	"&ldr	($xc,'[sp,#4*(16+$c2)]')",
	"&add	(@x[$a2],@x[$a2],@x[$b2])",
	"&mov	($xd,$xd,'ror#16')",
	 "&str	($xc_,'[sp,#4*(16+$c1)]')",
	 "&ldr	($xc_,'[sp,#4*(16+$c3)]')",
	 "&add	(@x[$a3],@x[$a3],@x[$b3])",
	 "&mov	($xd_,$xd_,'ror#16')",
	"&eor	($xd,$xd,@x[$a2],'ror#16')",
	 "&eor	($xd_,$xd_,@x[$a3],'ror#16')",

	"&add	($xc,$xc,$xd)",
	"&mov	(@x[$b2],@x[$b2],'ror#20')",
	 "&add	($xc_,$xc_,$xd_)",
	 "&mov	(@x[$b3],@x[$b3],'ror#20')",
	"&eor	(@x[$b2],@x[$b2],$xc,'ror#20')",
	 "&eor	(@x[$b3],@x[$b3],$xc_,'ror#20')",

	"&add	(@x[$a2],@x[$a2],@x[$b2])",
	"&mov	($xd,$xd,'ror#24')",
	 "&add	(@x[$a3],@x[$a3],@x[$b3])",
	 "&mov	($xd_,$xd_,'ror#24')",
	"&eor	($xd,$xd,@x[$a2],'ror#24')",
	 "&eor	($xd_,$xd_,@x[$a3],'ror#24')",

	"&add	($xc,$xc,$xd)",
	"&mov	(@x[$b2],@x[$b2],'ror#25')",
	 "&add	($xc_,$xc_,$xd_)",
	 "&mov	(@x[$b3],@x[$b3],'ror#25')",
	"&eor	(@x[$b2],@x[$b2],$xc,'ror#25')",
	 "&eor	(@x[$b3],@x[$b3],$xc_,'ror#25')"	);

	@ret;
}

$code.=<<___;
#include "arm_arch.h"

.text
#if defined(__thumb2__) || defined(__clang__)
.syntax	unified
#endif
#if defined(__thumb2__)
.thumb
#else
.code	32
#endif

#if defined(__thumb2__) || defined(__clang__)
#define ldrhsb	ldrbhs
#endif

.align	5
.Lsigma:
.long	0x61707865,0x3320646e,0x79622d32,0x6b206574	@ endian-neutral
.Lone:
.long	1,0,0,0
#if __ARM_MAX_ARCH__>=7
.LOPENSSL_armcap:
.word   OPENSSL_armcap_P-.LChaCha20_ctr32
#else
.word	-1
#endif

.globl	ChaCha20_ctr32
.type	ChaCha20_ctr32,%function
.align	5
ChaCha20_ctr32:
.LChaCha20_ctr32:
	ldr	r12,[sp,#0]		@ pull pointer to counter and nonce
	stmdb	sp!,{r0-r2,r4-r11,lr}
#if __ARM_ARCH__<7 && !defined(__thumb2__)
	sub	r14,pc,#16		@ ChaCha20_ctr32
#else
	adr	r14,.LChaCha20_ctr32
#endif
	cmp	r2,#0			@ len==0?
#ifdef	__thumb2__
	itt	eq
#endif
	addeq	sp,sp,#4*3
	beq	.Lno_data
#if __ARM_MAX_ARCH__>=7
	cmp	r2,#192			@ test len
	bls	.Lshort
	ldr	r4,[r14,#-32]
	ldr	r4,[r14,r4]
# ifdef	__APPLE__
	ldr	r4,[r4]
# endif
	tst	r4,#ARMV7_NEON
	bne	.LChaCha20_neon
.Lshort:
#endif
	ldmia	r12,{r4-r7}		@ load counter and nonce
	sub	sp,sp,#4*(16)		@ off-load area
	sub	r14,r14,#64		@ .Lsigma
	stmdb	sp!,{r4-r7}		@ copy counter and nonce
	ldmia	r3,{r4-r11}		@ load key
	ldmia	r14,{r0-r3}		@ load sigma
	stmdb	sp!,{r4-r11}		@ copy key
	stmdb	sp!,{r0-r3}		@ copy sigma
	str	r10,[sp,#4*(16+10)]	@ off-load "@x[10]"
	str	r11,[sp,#4*(16+11)]	@ off-load "@x[11]"
	b	.Loop_outer_enter

.align	4
.Loop_outer:
	ldmia	sp,{r0-r9}		@ load key material
	str	@t[3],[sp,#4*(32+2)]	@ save len
	str	r12,  [sp,#4*(32+1)]	@ save inp
	str	r14,  [sp,#4*(32+0)]	@ save out
.Loop_outer_enter:
	ldr	@t[3], [sp,#4*(15)]
	ldr	@x[12],[sp,#4*(12)]	@ modulo-scheduled load
	ldr	@t[2], [sp,#4*(13)]
	ldr	@x[14],[sp,#4*(14)]
	str	@t[3], [sp,#4*(16+15)]
	mov	@t[3],#10
	b	.Loop

.align	4
.Loop:
	subs	@t[3],@t[3],#1
___
	foreach (&ROUND(0, 4, 8,12)) { eval; }
	foreach (&ROUND(0, 5,10,15)) { eval; }
$code.=<<___;
	bne	.Loop

	ldr	@t[3],[sp,#4*(32+2)]	@ load len

	str	@t[0], [sp,#4*(16+8)]	@ modulo-scheduled store
	str	@t[1], [sp,#4*(16+9)]
	str	@x[12],[sp,#4*(16+12)]
	str	@t[2], [sp,#4*(16+13)]
	str	@x[14],[sp,#4*(16+14)]

	@ at this point we have first half of 512-bit result in
	@ @x[0-7] and second half at sp+4*(16+8)

	cmp	@t[3],#64		@ done yet?
#ifdef	__thumb2__
	itete	lo
#endif
	addlo	r12,sp,#4*(0)		@ shortcut or ...
	ldrhs	r12,[sp,#4*(32+1)]	@ ... load inp
	addlo	r14,sp,#4*(0)		@ shortcut or ...
	ldrhs	r14,[sp,#4*(32+0)]	@ ... load out

	ldr	@t[0],[sp,#4*(0)]	@ load key material
	ldr	@t[1],[sp,#4*(1)]

#if __ARM_ARCH__>=6 || !defined(__ARMEB__)
# if __ARM_ARCH__<7
	orr	@t[2],r12,r14
	tst	@t[2],#3		@ are input and output aligned?
	ldr	@t[2],[sp,#4*(2)]
	bne	.Lunaligned
	cmp	@t[3],#64		@ restore flags
# else
	ldr	@t[2],[sp,#4*(2)]
# endif
	ldr	@t[3],[sp,#4*(3)]

	add	@x[0],@x[0],@t[0]	@ accumulate key material
	add	@x[1],@x[1],@t[1]
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhs	@t[0],[r12],#16		@ load input
	ldrhs	@t[1],[r12,#-12]

	add	@x[2],@x[2],@t[2]
	add	@x[3],@x[3],@t[3]
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhs	@t[2],[r12,#-8]
	ldrhs	@t[3],[r12,#-4]
# if __ARM_ARCH__>=6 && defined(__ARMEB__)
	rev	@x[0],@x[0]
	rev	@x[1],@x[1]
	rev	@x[2],@x[2]
	rev	@x[3],@x[3]
# endif
# ifdef	__thumb2__
	itt	hs
# endif
	eorhs	@x[0],@x[0],@t[0]	@ xor with input
	eorhs	@x[1],@x[1],@t[1]
	 add	@t[0],sp,#4*(4)
	str	@x[0],[r14],#16		@ store output
# ifdef	__thumb2__
	itt	hs
# endif
	eorhs	@x[2],@x[2],@t[2]
	eorhs	@x[3],@x[3],@t[3]
	 ldmia	@t[0],{@t[0]-@t[3]}	@ load key material
	str	@x[1],[r14,#-12]
	str	@x[2],[r14,#-8]
	str	@x[3],[r14,#-4]

	add	@x[4],@x[4],@t[0]	@ accumulate key material
	add	@x[5],@x[5],@t[1]
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhs	@t[0],[r12],#16		@ load input
	ldrhs	@t[1],[r12,#-12]
	add	@x[6],@x[6],@t[2]
	add	@x[7],@x[7],@t[3]
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhs	@t[2],[r12,#-8]
	ldrhs	@t[3],[r12,#-4]
# if __ARM_ARCH__>=6 && defined(__ARMEB__)
	rev	@x[4],@x[4]
	rev	@x[5],@x[5]
	rev	@x[6],@x[6]
	rev	@x[7],@x[7]
# endif
# ifdef	__thumb2__
	itt	hs
# endif
	eorhs	@x[4],@x[4],@t[0]
	eorhs	@x[5],@x[5],@t[1]
	 add	@t[0],sp,#4*(8)
	str	@x[4],[r14],#16		@ store output
# ifdef	__thumb2__
	itt	hs
# endif
	eorhs	@x[6],@x[6],@t[2]
	eorhs	@x[7],@x[7],@t[3]
	str	@x[5],[r14,#-12]
	 ldmia	@t[0],{@t[0]-@t[3]}	@ load key material
	str	@x[6],[r14,#-8]
	 add	@x[0],sp,#4*(16+8)
	str	@x[7],[r14,#-4]

	ldmia	@x[0],{@x[0]-@x[7]}	@ load second half

	add	@x[0],@x[0],@t[0]	@ accumulate key material
	add	@x[1],@x[1],@t[1]
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhs	@t[0],[r12],#16		@ load input
	ldrhs	@t[1],[r12,#-12]
# ifdef	__thumb2__
	itt	hi
# endif
	 strhi	@t[2],[sp,#4*(16+10)]	@ copy "@x[10]" while at it
	 strhi	@t[3],[sp,#4*(16+11)]	@ copy "@x[11]" while at it
	add	@x[2],@x[2],@t[2]
	add	@x[3],@x[3],@t[3]
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhs	@t[2],[r12,#-8]
	ldrhs	@t[3],[r12,#-4]
# if __ARM_ARCH__>=6 && defined(__ARMEB__)
	rev	@x[0],@x[0]
	rev	@x[1],@x[1]
	rev	@x[2],@x[2]
	rev	@x[3],@x[3]
# endif
# ifdef	__thumb2__
	itt	hs
# endif
	eorhs	@x[0],@x[0],@t[0]
	eorhs	@x[1],@x[1],@t[1]
	 add	@t[0],sp,#4*(12)
	str	@x[0],[r14],#16		@ store output
# ifdef	__thumb2__
	itt	hs
# endif
	eorhs	@x[2],@x[2],@t[2]
	eorhs	@x[3],@x[3],@t[3]
	str	@x[1],[r14,#-12]
	 ldmia	@t[0],{@t[0]-@t[3]}	@ load key material
	str	@x[2],[r14,#-8]
	str	@x[3],[r14,#-4]

	add	@x[4],@x[4],@t[0]	@ accumulate key material
	add	@x[5],@x[5],@t[1]
# ifdef	__thumb2__
	itt	hi
# endif
	 addhi	@t[0],@t[0],#1		@ next counter value
	 strhi	@t[0],[sp,#4*(12)]	@ save next counter value
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhs	@t[0],[r12],#16		@ load input
	ldrhs	@t[1],[r12,#-12]
	add	@x[6],@x[6],@t[2]
	add	@x[7],@x[7],@t[3]
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhs	@t[2],[r12,#-8]
	ldrhs	@t[3],[r12,#-4]
# if __ARM_ARCH__>=6 && defined(__ARMEB__)
	rev	@x[4],@x[4]
	rev	@x[5],@x[5]
	rev	@x[6],@x[6]
	rev	@x[7],@x[7]
# endif
# ifdef	__thumb2__
	itt	hs
# endif
	eorhs	@x[4],@x[4],@t[0]
	eorhs	@x[5],@x[5],@t[1]
# ifdef	__thumb2__
	 it	ne
# endif
	 ldrne	@t[0],[sp,#4*(32+2)]	@ re-load len
# ifdef	__thumb2__
	itt	hs
# endif
	eorhs	@x[6],@x[6],@t[2]
	eorhs	@x[7],@x[7],@t[3]
	str	@x[4],[r14],#16		@ store output
	str	@x[5],[r14,#-12]
# ifdef	__thumb2__
	it	hs
# endif
	 subhs	@t[3],@t[0],#64		@ len-=64
	str	@x[6],[r14,#-8]
	str	@x[7],[r14,#-4]
	bhi	.Loop_outer

	beq	.Ldone
# if __ARM_ARCH__<7
	b	.Ltail

.align	4
.Lunaligned:				@ unaligned endian-neutral path
	cmp	@t[3],#64		@ restore flags
# endif
#endif
#if __ARM_ARCH__<7
	ldr	@t[3],[sp,#4*(3)]
___
for ($i=0;$i<16;$i+=4) {
my $j=$i&0x7;

$code.=<<___	if ($i==4);
	add	@x[0],sp,#4*(16+8)
___
$code.=<<___	if ($i==8);
	ldmia	@x[0],{@x[0]-@x[7]}		@ load second half
# ifdef	__thumb2__
	itt	hi
# endif
	strhi	@t[2],[sp,#4*(16+10)]		@ copy "@x[10]"
	strhi	@t[3],[sp,#4*(16+11)]		@ copy "@x[11]"
___
$code.=<<___;
	add	@x[$j+0],@x[$j+0],@t[0]		@ accumulate key material
___
$code.=<<___	if ($i==12);
# ifdef	__thumb2__
	itt	hi
# endif
	addhi	@t[0],@t[0],#1			@ next counter value
	strhi	@t[0],[sp,#4*(12)]		@ save next counter value
___
$code.=<<___;
	add	@x[$j+1],@x[$j+1],@t[1]
	add	@x[$j+2],@x[$j+2],@t[2]
# ifdef	__thumb2__
	itete	lo
# endif
	eorlo	@t[0],@t[0],@t[0]		@ zero or ...
	ldrhsb	@t[0],[r12],#16			@ ... load input
	eorlo	@t[1],@t[1],@t[1]
	ldrhsb	@t[1],[r12,#-12]

	add	@x[$j+3],@x[$j+3],@t[3]
# ifdef	__thumb2__
	itete	lo
# endif
	eorlo	@t[2],@t[2],@t[2]
	ldrhsb	@t[2],[r12,#-8]
	eorlo	@t[3],@t[3],@t[3]
	ldrhsb	@t[3],[r12,#-4]

	eor	@x[$j+0],@t[0],@x[$j+0]		@ xor with input (or zero)
	eor	@x[$j+1],@t[1],@x[$j+1]
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhsb	@t[0],[r12,#-15]		@ load more input
	ldrhsb	@t[1],[r12,#-11]
	eor	@x[$j+2],@t[2],@x[$j+2]
	 strb	@x[$j+0],[r14],#16		@ store output
	eor	@x[$j+3],@t[3],@x[$j+3]
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhsb	@t[2],[r12,#-7]
	ldrhsb	@t[3],[r12,#-3]
	 strb	@x[$j+1],[r14,#-12]
	eor	@x[$j+0],@t[0],@x[$j+0],lsr#8
	 strb	@x[$j+2],[r14,#-8]
	eor	@x[$j+1],@t[1],@x[$j+1],lsr#8
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhsb	@t[0],[r12,#-14]		@ load more input
	ldrhsb	@t[1],[r12,#-10]
	 strb	@x[$j+3],[r14,#-4]
	eor	@x[$j+2],@t[2],@x[$j+2],lsr#8
	 strb	@x[$j+0],[r14,#-15]
	eor	@x[$j+3],@t[3],@x[$j+3],lsr#8
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhsb	@t[2],[r12,#-6]
	ldrhsb	@t[3],[r12,#-2]
	 strb	@x[$j+1],[r14,#-11]
	eor	@x[$j+0],@t[0],@x[$j+0],lsr#8
	 strb	@x[$j+2],[r14,#-7]
	eor	@x[$j+1],@t[1],@x[$j+1],lsr#8
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhsb	@t[0],[r12,#-13]		@ load more input
	ldrhsb	@t[1],[r12,#-9]
	 strb	@x[$j+3],[r14,#-3]
	eor	@x[$j+2],@t[2],@x[$j+2],lsr#8
	 strb	@x[$j+0],[r14,#-14]
	eor	@x[$j+3],@t[3],@x[$j+3],lsr#8
# ifdef	__thumb2__
	itt	hs
# endif
	ldrhsb	@t[2],[r12,#-5]
	ldrhsb	@t[3],[r12,#-1]
	 strb	@x[$j+1],[r14,#-10]
	 strb	@x[$j+2],[r14,#-6]
	eor	@x[$j+0],@t[0],@x[$j+0],lsr#8
	 strb	@x[$j+3],[r14,#-2]
	eor	@x[$j+1],@t[1],@x[$j+1],lsr#8
	 strb	@x[$j+0],[r14,#-13]
	eor	@x[$j+2],@t[2],@x[$j+2],lsr#8
	 strb	@x[$j+1],[r14,#-9]
	eor	@x[$j+3],@t[3],@x[$j+3],lsr#8
	 strb	@x[$j+2],[r14,#-5]
	 strb	@x[$j+3],[r14,#-1]
___
$code.=<<___	if ($i<12);
	add	@t[0],sp,#4*(4+$i)
	ldmia	@t[0],{@t[0]-@t[3]}		@ load key material
___
}
$code.=<<___;
# ifdef	__thumb2__
	it	ne
# endif
	ldrne	@t[0],[sp,#4*(32+2)]		@ re-load len
# ifdef	__thumb2__
	it	hs
# endif
	subhs	@t[3],@t[0],#64			@ len-=64
	bhi	.Loop_outer

	beq	.Ldone
#endif

.Ltail:
	ldr	r12,[sp,#4*(32+1)]	@ load inp
	add	@t[1],sp,#4*(0)
	ldr	r14,[sp,#4*(32+0)]	@ load out

.Loop_tail:
	ldrb	@t[2],[@t[1]],#1	@ read buffer on stack
	ldrb	@t[3],[r12],#1		@ read input
	subs	@t[0],@t[0],#1
	eor	@t[3],@t[3],@t[2]
	strb	@t[3],[r14],#1		@ store output
	bne	.Loop_tail

.Ldone:
	add	sp,sp,#4*(32+3)
.Lno_data:
	ldmia	sp!,{r4-r11,pc}
.size	ChaCha20_ctr32,.-ChaCha20_ctr32
___

{{{
my ($a0,$b0,$c0,$d0,$a1,$b1,$c1,$d1,$a2,$b2,$c2,$d2,$t0,$t1,$t2,$t3) =
    map("q$_",(0..15));

sub NEONROUND {
my $odd = pop;
my ($a,$b,$c,$d,$t)=@_;

	(
	"&vadd_i32	($a,$a,$b)",
	"&veor		($d,$d,$a)",
	"&vrev32_16	($d,$d)",	# vrot ($d,16)

	"&vadd_i32	($c,$c,$d)",
	"&veor		($t,$b,$c)",
	"&vshr_u32	($b,$t,20)",
	"&vsli_32	($b,$t,12)",

	"&vadd_i32	($a,$a,$b)",
	"&veor		($t,$d,$a)",
	"&vshr_u32	($d,$t,24)",
	"&vsli_32	($d,$t,8)",

	"&vadd_i32	($c,$c,$d)",
	"&veor		($t,$b,$c)",
	"&vshr_u32	($b,$t,25)",
	"&vsli_32	($b,$t,7)",

	"&vext_8	($c,$c,$c,8)",
	"&vext_8	($b,$b,$b,$odd?12:4)",
	"&vext_8	($d,$d,$d,$odd?4:12)"
	);
}

$code.=<<___;
#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.type	ChaCha20_neon,%function
.align	5
ChaCha20_neon:
	ldr		r12,[sp,#0]		@ pull pointer to counter and nonce
	stmdb		sp!,{r0-r2,r4-r11,lr}
.LChaCha20_neon:
	adr		r14,.Lsigma
	vstmdb		sp!,{d8-d15}		@ ABI spec says so
	stmdb		sp!,{r0-r3}

	vld1.32		{$b0-$c0},[r3]		@ load key
	ldmia		r3,{r4-r11}		@ load key

	sub		sp,sp,#4*(16+16)
	vld1.32		{$d0},[r12]		@ load counter and nonce
	add		r12,sp,#4*8
	ldmia		r14,{r0-r3}		@ load sigma
	vld1.32		{$a0},[r14]!		@ load sigma
	vld1.32		{$t0},[r14]		@ one
	vst1.32		{$c0-$d0},[r12]		@ copy 1/2key|counter|nonce
	vst1.32		{$a0-$b0},[sp]		@ copy sigma|1/2key

	str		r10,[sp,#4*(16+10)]	@ off-load "@x[10]"
	str		r11,[sp,#4*(16+11)]	@ off-load "@x[11]"
	vshl.i32	$t1#lo,$t0#lo,#1	@ two
	vstr		$t0#lo,[sp,#4*(16+0)]
	vshl.i32	$t2#lo,$t0#lo,#2	@ four
	vstr		$t1#lo,[sp,#4*(16+2)]
	vmov		$a1,$a0
	vstr		$t2#lo,[sp,#4*(16+4)]
	vmov		$a2,$a0
	vmov		$b1,$b0
	vmov		$b2,$b0
	b		.Loop_neon_enter

.align	4
.Loop_neon_outer:
	ldmia		sp,{r0-r9}		@ load key material
	cmp		@t[3],#64*2		@ if len<=64*2
	bls		.Lbreak_neon		@ switch to integer-only
	vmov		$a1,$a0
	str		@t[3],[sp,#4*(32+2)]	@ save len
	vmov		$a2,$a0
	str		r12,  [sp,#4*(32+1)]	@ save inp
	vmov		$b1,$b0
	str		r14,  [sp,#4*(32+0)]	@ save out
	vmov		$b2,$b0
.Loop_neon_enter:
	ldr		@t[3], [sp,#4*(15)]
	vadd.i32	$d1,$d0,$t0		@ counter+1
	ldr		@x[12],[sp,#4*(12)]	@ modulo-scheduled load
	vmov		$c1,$c0
	ldr		@t[2], [sp,#4*(13)]
	vmov		$c2,$c0
	ldr		@x[14],[sp,#4*(14)]
	vadd.i32	$d2,$d1,$t0		@ counter+2
	str		@t[3], [sp,#4*(16+15)]
	mov		@t[3],#10
	add		@x[12],@x[12],#3	@ counter+3
	b		.Loop_neon

.align	4
.Loop_neon:
	subs		@t[3],@t[3],#1
___
	my @thread0=&NEONROUND($a0,$b0,$c0,$d0,$t0,0);
	my @thread1=&NEONROUND($a1,$b1,$c1,$d1,$t1,0);
	my @thread2=&NEONROUND($a2,$b2,$c2,$d2,$t2,0);
	my @thread3=&ROUND(0,4,8,12);

	foreach (@thread0) {
		eval;			eval(shift(@thread3));
		eval(shift(@thread1));	eval(shift(@thread3));
		eval(shift(@thread2));	eval(shift(@thread3));
	}

	@thread0=&NEONROUND($a0,$b0,$c0,$d0,$t0,1);
	@thread1=&NEONROUND($a1,$b1,$c1,$d1,$t1,1);
	@thread2=&NEONROUND($a2,$b2,$c2,$d2,$t2,1);
	@thread3=&ROUND(0,5,10,15);

	foreach (@thread0) {
		eval;			eval(shift(@thread3));
		eval(shift(@thread1));	eval(shift(@thread3));
		eval(shift(@thread2));	eval(shift(@thread3));
	}
$code.=<<___;
	bne		.Loop_neon

	add		@t[3],sp,#32
	vld1.32		{$t0-$t1},[sp]		@ load key material
	vld1.32		{$t2-$t3},[@t[3]]

	ldr		@t[3],[sp,#4*(32+2)]	@ load len

	str		@t[0], [sp,#4*(16+8)]	@ modulo-scheduled store
	str		@t[1], [sp,#4*(16+9)]
	str		@x[12],[sp,#4*(16+12)]
	str		@t[2], [sp,#4*(16+13)]
	str		@x[14],[sp,#4*(16+14)]

	@ at this point we have first half of 512-bit result in
	@ @x[0-7] and second half at sp+4*(16+8)

	ldr		r12,[sp,#4*(32+1)]	@ load inp
	ldr		r14,[sp,#4*(32+0)]	@ load out

	vadd.i32	$a0,$a0,$t0		@ accumulate key material
	vadd.i32	$a1,$a1,$t0
	vadd.i32	$a2,$a2,$t0
	vldr		$t0#lo,[sp,#4*(16+0)]	@ one

	vadd.i32	$b0,$b0,$t1
	vadd.i32	$b1,$b1,$t1
	vadd.i32	$b2,$b2,$t1
	vldr		$t1#lo,[sp,#4*(16+2)]	@ two

	vadd.i32	$c0,$c0,$t2
	vadd.i32	$c1,$c1,$t2
	vadd.i32	$c2,$c2,$t2
	vadd.i32	$d1#lo,$d1#lo,$t0#lo	@ counter+1
	vadd.i32	$d2#lo,$d2#lo,$t1#lo	@ counter+2

	vadd.i32	$d0,$d0,$t3
	vadd.i32	$d1,$d1,$t3
	vadd.i32	$d2,$d2,$t3

	cmp		@t[3],#64*4
	blo		.Ltail_neon

	vld1.8		{$t0-$t1},[r12]!	@ load input
	 mov		@t[3],sp
	vld1.8		{$t2-$t3},[r12]!
	veor		$a0,$a0,$t0		@ xor with input
	veor		$b0,$b0,$t1
	vld1.8		{$t0-$t1},[r12]!
	veor		$c0,$c0,$t2
	veor		$d0,$d0,$t3
	vld1.8		{$t2-$t3},[r12]!

	veor		$a1,$a1,$t0
	 vst1.8		{$a0-$b0},[r14]!	@ store output
	veor		$b1,$b1,$t1
	vld1.8		{$t0-$t1},[r12]!
	veor		$c1,$c1,$t2
	 vst1.8		{$c0-$d0},[r14]!
	veor		$d1,$d1,$t3
	vld1.8		{$t2-$t3},[r12]!

	veor		$a2,$a2,$t0
	 vld1.32	{$a0-$b0},[@t[3]]!	@ load for next iteration
	 veor		$t0#hi,$t0#hi,$t0#hi
	 vldr		$t0#lo,[sp,#4*(16+4)]	@ four
	veor		$b2,$b2,$t1
	 vld1.32	{$c0-$d0},[@t[3]]
	veor		$c2,$c2,$t2
	 vst1.8		{$a1-$b1},[r14]!
	veor		$d2,$d2,$t3
	 vst1.8		{$c1-$d1},[r14]!

	vadd.i32	$d0#lo,$d0#lo,$t0#lo	@ next counter value
	vldr		$t0#lo,[sp,#4*(16+0)]	@ one

	ldmia		sp,{@t[0]-@t[3]}	@ load key material
	add		@x[0],@x[0],@t[0]	@ accumulate key material
	ldr		@t[0],[r12],#16		@ load input
	 vst1.8		{$a2-$b2},[r14]!
	add		@x[1],@x[1],@t[1]
	ldr		@t[1],[r12,#-12]
	 vst1.8		{$c2-$d2},[r14]!
	add		@x[2],@x[2],@t[2]
	ldr		@t[2],[r12,#-8]
	add		@x[3],@x[3],@t[3]
	ldr		@t[3],[r12,#-4]
# ifdef	__ARMEB__
	rev		@x[0],@x[0]
	rev		@x[1],@x[1]
	rev		@x[2],@x[2]
	rev		@x[3],@x[3]
# endif
	eor		@x[0],@x[0],@t[0]	@ xor with input
	 add		@t[0],sp,#4*(4)
	eor		@x[1],@x[1],@t[1]
	str		@x[0],[r14],#16		@ store output
	eor		@x[2],@x[2],@t[2]
	str		@x[1],[r14,#-12]
	eor		@x[3],@x[3],@t[3]
	 ldmia		@t[0],{@t[0]-@t[3]}	@ load key material
	str		@x[2],[r14,#-8]
	str		@x[3],[r14,#-4]

	add		@x[4],@x[4],@t[0]	@ accumulate key material
	ldr		@t[0],[r12],#16		@ load input
	add		@x[5],@x[5],@t[1]
	ldr		@t[1],[r12,#-12]
	add		@x[6],@x[6],@t[2]
	ldr		@t[2],[r12,#-8]
	add		@x[7],@x[7],@t[3]
	ldr		@t[3],[r12,#-4]
# ifdef	__ARMEB__
	rev		@x[4],@x[4]
	rev		@x[5],@x[5]
	rev		@x[6],@x[6]
	rev		@x[7],@x[7]
# endif
	eor		@x[4],@x[4],@t[0]
	 add		@t[0],sp,#4*(8)
	eor		@x[5],@x[5],@t[1]
	str		@x[4],[r14],#16		@ store output
	eor		@x[6],@x[6],@t[2]
	str		@x[5],[r14,#-12]
	eor		@x[7],@x[7],@t[3]
	 ldmia		@t[0],{@t[0]-@t[3]}	@ load key material
	str		@x[6],[r14,#-8]
	 add		@x[0],sp,#4*(16+8)
	str		@x[7],[r14,#-4]

	ldmia		@x[0],{@x[0]-@x[7]}	@ load second half

	add		@x[0],@x[0],@t[0]	@ accumulate key material
	ldr		@t[0],[r12],#16		@ load input
	add		@x[1],@x[1],@t[1]
	ldr		@t[1],[r12,#-12]
# ifdef	__thumb2__
	it	hi
# endif
	 strhi		@t[2],[sp,#4*(16+10)]	@ copy "@x[10]" while at it
	add		@x[2],@x[2],@t[2]
	ldr		@t[2],[r12,#-8]
# ifdef	__thumb2__
	it	hi
# endif
	 strhi		@t[3],[sp,#4*(16+11)]	@ copy "@x[11]" while at it
	add		@x[3],@x[3],@t[3]
	ldr		@t[3],[r12,#-4]
# ifdef	__ARMEB__
	rev		@x[0],@x[0]
	rev		@x[1],@x[1]
	rev		@x[2],@x[2]
	rev		@x[3],@x[3]
# endif
	eor		@x[0],@x[0],@t[0]
	 add		@t[0],sp,#4*(12)
	eor		@x[1],@x[1],@t[1]
	str		@x[0],[r14],#16		@ store output
	eor		@x[2],@x[2],@t[2]
	str		@x[1],[r14,#-12]
	eor		@x[3],@x[3],@t[3]
	 ldmia		@t[0],{@t[0]-@t[3]}	@ load key material
	str		@x[2],[r14,#-8]
	str		@x[3],[r14,#-4]

	add		@x[4],@x[4],@t[0]	@ accumulate key material
	 add		@t[0],@t[0],#4		@ next counter value
	add		@x[5],@x[5],@t[1]
	 str		@t[0],[sp,#4*(12)]	@ save next counter value
	ldr		@t[0],[r12],#16		@ load input
	add		@x[6],@x[6],@t[2]
	 add		@x[4],@x[4],#3		@ counter+3
	ldr		@t[1],[r12,#-12]
	add		@x[7],@x[7],@t[3]
	ldr		@t[2],[r12,#-8]
	ldr		@t[3],[r12,#-4]
# ifdef	__ARMEB__
	rev		@x[4],@x[4]
	rev		@x[5],@x[5]
	rev		@x[6],@x[6]
	rev		@x[7],@x[7]
# endif
	eor		@x[4],@x[4],@t[0]
# ifdef	__thumb2__
	it	hi
# endif
	 ldrhi		@t[0],[sp,#4*(32+2)]	@ re-load len
	eor		@x[5],@x[5],@t[1]
	eor		@x[6],@x[6],@t[2]
	str		@x[4],[r14],#16		@ store output
	eor		@x[7],@x[7],@t[3]
	str		@x[5],[r14,#-12]
	 sub		@t[3],@t[0],#64*4	@ len-=64*4
	str		@x[6],[r14,#-8]
	str		@x[7],[r14,#-4]
	bhi		.Loop_neon_outer

	b		.Ldone_neon

.align	4
.Lbreak_neon:
	@ harmonize NEON and integer-only stack frames: load data
	@ from NEON frame, but save to integer-only one; distance
	@ between the two is 4*(32+4+16-32)=4*(20).

	str		@t[3], [sp,#4*(20+32+2)]	@ save len
	 add		@t[3],sp,#4*(32+4)
	str		r12,   [sp,#4*(20+32+1)]	@ save inp
	str		r14,   [sp,#4*(20+32+0)]	@ save out

	ldr		@x[12],[sp,#4*(16+10)]
	ldr		@x[14],[sp,#4*(16+11)]
	 vldmia		@t[3],{d8-d15}			@ fulfill ABI requirement
	str		@x[12],[sp,#4*(20+16+10)]	@ copy "@x[10]"
	str		@x[14],[sp,#4*(20+16+11)]	@ copy "@x[11]"

	ldr		@t[3], [sp,#4*(15)]
	ldr		@x[12],[sp,#4*(12)]		@ modulo-scheduled load
	ldr		@t[2], [sp,#4*(13)]
	ldr		@x[14],[sp,#4*(14)]
	str		@t[3], [sp,#4*(20+16+15)]
	add		@t[3],sp,#4*(20)
	vst1.32		{$a0-$b0},[@t[3]]!		@ copy key
	add		sp,sp,#4*(20)			@ switch frame
	vst1.32		{$c0-$d0},[@t[3]]
	mov		@t[3],#10
	b		.Loop				@ go integer-only

.align	4
.Ltail_neon:
	cmp		@t[3],#64*3
	bhs		.L192_or_more_neon
	cmp		@t[3],#64*2
	bhs		.L128_or_more_neon
	cmp		@t[3],#64*1
	bhs		.L64_or_more_neon

	add		@t[0],sp,#4*(8)
	vst1.8		{$a0-$b0},[sp]
	add		@t[2],sp,#4*(0)
	vst1.8		{$c0-$d0},[@t[0]]
	b		.Loop_tail_neon

.align	4
.L64_or_more_neon:
	vld1.8		{$t0-$t1},[r12]!
	vld1.8		{$t2-$t3},[r12]!
	veor		$a0,$a0,$t0
	veor		$b0,$b0,$t1
	veor		$c0,$c0,$t2
	veor		$d0,$d0,$t3
	vst1.8		{$a0-$b0},[r14]!
	vst1.8		{$c0-$d0},[r14]!

	beq		.Ldone_neon

	add		@t[0],sp,#4*(8)
	vst1.8		{$a1-$b1},[sp]
	add		@t[2],sp,#4*(0)
	vst1.8		{$c1-$d1},[@t[0]]
	sub		@t[3],@t[3],#64*1	@ len-=64*1
	b		.Loop_tail_neon

.align	4
.L128_or_more_neon:
	vld1.8		{$t0-$t1},[r12]!
	vld1.8		{$t2-$t3},[r12]!
	veor		$a0,$a0,$t0
	veor		$b0,$b0,$t1
	vld1.8		{$t0-$t1},[r12]!
	veor		$c0,$c0,$t2
	veor		$d0,$d0,$t3
	vld1.8		{$t2-$t3},[r12]!

	veor		$a1,$a1,$t0
	veor		$b1,$b1,$t1
	 vst1.8		{$a0-$b0},[r14]!
	veor		$c1,$c1,$t2
	 vst1.8		{$c0-$d0},[r14]!
	veor		$d1,$d1,$t3
	vst1.8		{$a1-$b1},[r14]!
	vst1.8		{$c1-$d1},[r14]!

	beq		.Ldone_neon

	add		@t[0],sp,#4*(8)
	vst1.8		{$a2-$b2},[sp]
	add		@t[2],sp,#4*(0)
	vst1.8		{$c2-$d2},[@t[0]]
	sub		@t[3],@t[3],#64*2	@ len-=64*2
	b		.Loop_tail_neon

.align	4
.L192_or_more_neon:
	vld1.8		{$t0-$t1},[r12]!
	vld1.8		{$t2-$t3},[r12]!
	veor		$a0,$a0,$t0
	veor		$b0,$b0,$t1
	vld1.8		{$t0-$t1},[r12]!
	veor		$c0,$c0,$t2
	veor		$d0,$d0,$t3
	vld1.8		{$t2-$t3},[r12]!

	veor		$a1,$a1,$t0
	veor		$b1,$b1,$t1
	vld1.8		{$t0-$t1},[r12]!
	veor		$c1,$c1,$t2
	 vst1.8		{$a0-$b0},[r14]!
	veor		$d1,$d1,$t3
	vld1.8		{$t2-$t3},[r12]!

	veor		$a2,$a2,$t0
	 vst1.8		{$c0-$d0},[r14]!
	veor		$b2,$b2,$t1
	 vst1.8		{$a1-$b1},[r14]!
	veor		$c2,$c2,$t2
	 vst1.8		{$c1-$d1},[r14]!
	veor		$d2,$d2,$t3
	vst1.8		{$a2-$b2},[r14]!
	vst1.8		{$c2-$d2},[r14]!

	beq		.Ldone_neon

	ldmia		sp,{@t[0]-@t[3]}	@ load key material
	add		@x[0],@x[0],@t[0]	@ accumulate key material
	 add		@t[0],sp,#4*(4)
	add		@x[1],@x[1],@t[1]
	add		@x[2],@x[2],@t[2]
	add		@x[3],@x[3],@t[3]
	 ldmia		@t[0],{@t[0]-@t[3]}	@ load key material

	add		@x[4],@x[4],@t[0]	@ accumulate key material
	 add		@t[0],sp,#4*(8)
	add		@x[5],@x[5],@t[1]
	add		@x[6],@x[6],@t[2]
	add		@x[7],@x[7],@t[3]
	 ldmia		@t[0],{@t[0]-@t[3]}	@ load key material
# ifdef	__ARMEB__
	rev		@x[0],@x[0]
	rev		@x[1],@x[1]
	rev		@x[2],@x[2]
	rev		@x[3],@x[3]
	rev		@x[4],@x[4]
	rev		@x[5],@x[5]
	rev		@x[6],@x[6]
	rev		@x[7],@x[7]
# endif
	stmia		sp,{@x[0]-@x[7]}
	 add		@x[0],sp,#4*(16+8)

	ldmia		@x[0],{@x[0]-@x[7]}	@ load second half

	add		@x[0],@x[0],@t[0]	@ accumulate key material
	 add		@t[0],sp,#4*(12)
	add		@x[1],@x[1],@t[1]
	add		@x[2],@x[2],@t[2]
	add		@x[3],@x[3],@t[3]
	 ldmia		@t[0],{@t[0]-@t[3]}	@ load key material

	add		@x[4],@x[4],@t[0]	@ accumulate key material
	 add		@t[0],sp,#4*(8)
	add		@x[5],@x[5],@t[1]
	 add		@x[4],@x[4],#3		@ counter+3
	add		@x[6],@x[6],@t[2]
	add		@x[7],@x[7],@t[3]
	 ldr		@t[3],[sp,#4*(32+2)]	@ re-load len
# ifdef	__ARMEB__
	rev		@x[0],@x[0]
	rev		@x[1],@x[1]
	rev		@x[2],@x[2]
	rev		@x[3],@x[3]
	rev		@x[4],@x[4]
	rev		@x[5],@x[5]
	rev		@x[6],@x[6]
	rev		@x[7],@x[7]
# endif
	stmia		@t[0],{@x[0]-@x[7]}
	 add		@t[2],sp,#4*(0)
	 sub		@t[3],@t[3],#64*3	@ len-=64*3

.Loop_tail_neon:
	ldrb		@t[0],[@t[2]],#1	@ read buffer on stack
	ldrb		@t[1],[r12],#1		@ read input
	subs		@t[3],@t[3],#1
	eor		@t[0],@t[0],@t[1]
	strb		@t[0],[r14],#1		@ store output
	bne		.Loop_tail_neon

.Ldone_neon:
	add		sp,sp,#4*(32+4)
	vldmia		sp,{d8-d15}
	add		sp,sp,#4*(16+3)
	ldmia		sp!,{r4-r11,pc}
.size	ChaCha20_neon,.-ChaCha20_neon
.comm	OPENSSL_armcap_P,4,4
#endif
___
}}}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/geo;

	s/\bq([0-9]+)#(lo|hi)/sprintf "d%d",2*$1+($2 eq "hi")/geo;

	print $_,"\n";
}
close STDOUT;

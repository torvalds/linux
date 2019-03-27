#! /usr/bin/env perl
# Copyright 2010-2016 The OpenSSL Project Authors. All Rights Reserved.
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

# March 2010
#
# The module implements "4-bit" GCM GHASH function and underlying
# single multiplication operation in GF(2^128). "4-bit" means that it
# uses 256 bytes per-key table [+128 bytes shared table]. Performance
# results are for streamed GHASH subroutine on UltraSPARC pre-Tx CPU
# and are expressed in cycles per processed byte, less is better:
#
#		gcc 3.3.x	cc 5.2		this assembler
#
# 32-bit build	81.4		43.3		12.6	(+546%/+244%)
# 64-bit build	20.2		21.2		12.6	(+60%/+68%)
#
# Here is data collected on UltraSPARC T1 system running Linux:
#
#		gcc 4.4.1			this assembler
#
# 32-bit build	566				50	(+1000%)
# 64-bit build	56				50	(+12%)
#
# I don't quite understand why difference between 32-bit and 64-bit
# compiler-generated code is so big. Compilers *were* instructed to
# generate code for UltraSPARC and should have used 64-bit registers
# for Z vector (see C code) even in 32-bit build... Oh well, it only
# means more impressive improvement coefficients for this assembler
# module;-) Loops are aggressively modulo-scheduled in respect to
# references to input data and Z.hi updates to achieve 12 cycles
# timing. To anchor to something else, sha1-sparcv9.pl spends 11.6
# cycles to process one byte on UltraSPARC pre-Tx CPU and ~24 on T1.
#
# October 2012
#
# Add VIS3 lookup-table-free implementation using polynomial
# multiplication xmulx[hi] and extended addition addxc[cc]
# instructions. 4.52/7.63x improvement on T3/T4 or in absolute
# terms 7.90/2.14 cycles per byte. On T4 multi-process benchmark
# saturates at ~15.5x single-process result on 8-core processor,
# or ~20.5GBps per 2.85GHz socket.

$output=pop;
open STDOUT,">$output";

$frame="STACK_FRAME";
$bias="STACK_BIAS";

$Zhi="%o0";	# 64-bit values
$Zlo="%o1";
$Thi="%o2";
$Tlo="%o3";
$rem="%o4";
$tmp="%o5";

$nhi="%l0";	# small values and pointers
$nlo="%l1";
$xi0="%l2";
$xi1="%l3";
$rem_4bit="%l4";
$remi="%l5";
$Htblo="%l6";
$cnt="%l7";

$Xi="%i0";	# input argument block
$Htbl="%i1";
$inp="%i2";
$len="%i3";

$code.=<<___;
#include "sparc_arch.h"

#ifdef  __arch64__
.register	%g2,#scratch
.register	%g3,#scratch
#endif

.section	".text",#alloc,#execinstr

.align	64
rem_4bit:
	.long	`0x0000<<16`,0,`0x1C20<<16`,0,`0x3840<<16`,0,`0x2460<<16`,0
	.long	`0x7080<<16`,0,`0x6CA0<<16`,0,`0x48C0<<16`,0,`0x54E0<<16`,0
	.long	`0xE100<<16`,0,`0xFD20<<16`,0,`0xD940<<16`,0,`0xC560<<16`,0
	.long	`0x9180<<16`,0,`0x8DA0<<16`,0,`0xA9C0<<16`,0,`0xB5E0<<16`,0
.type	rem_4bit,#object
.size	rem_4bit,(.-rem_4bit)

.globl	gcm_ghash_4bit
.align	32
gcm_ghash_4bit:
	save	%sp,-$frame,%sp
	ldub	[$inp+15],$nlo
	ldub	[$Xi+15],$xi0
	ldub	[$Xi+14],$xi1
	add	$len,$inp,$len
	add	$Htbl,8,$Htblo

1:	call	.+8
	add	%o7,rem_4bit-1b,$rem_4bit

.Louter:
	xor	$xi0,$nlo,$nlo
	and	$nlo,0xf0,$nhi
	and	$nlo,0x0f,$nlo
	sll	$nlo,4,$nlo
	ldx	[$Htblo+$nlo],$Zlo
	ldx	[$Htbl+$nlo],$Zhi

	ldub	[$inp+14],$nlo

	ldx	[$Htblo+$nhi],$Tlo
	and	$Zlo,0xf,$remi
	ldx	[$Htbl+$nhi],$Thi
	sll	$remi,3,$remi
	ldx	[$rem_4bit+$remi],$rem
	srlx	$Zlo,4,$Zlo
	mov	13,$cnt
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo

	xor	$xi1,$nlo,$nlo
	and	$Zlo,0xf,$remi
	and	$nlo,0xf0,$nhi
	and	$nlo,0x0f,$nlo
	ba	.Lghash_inner
	sll	$nlo,4,$nlo
.align	32
.Lghash_inner:
	ldx	[$Htblo+$nlo],$Tlo
	sll	$remi,3,$remi
	xor	$Thi,$Zhi,$Zhi
	ldx	[$Htbl+$nlo],$Thi
	srlx	$Zlo,4,$Zlo
	xor	$rem,$Zhi,$Zhi
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	ldub	[$inp+$cnt],$nlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	ldub	[$Xi+$cnt],$xi1
	xor	$Thi,$Zhi,$Zhi
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nhi],$Tlo
	sll	$remi,3,$remi
	xor	$rem,$Zhi,$Zhi
	ldx	[$Htbl+$nhi],$Thi
	srlx	$Zlo,4,$Zlo
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$xi1,$nlo,$nlo
	srlx	$Zhi,4,$Zhi
	and	$nlo,0xf0,$nhi
	addcc	$cnt,-1,$cnt
	xor	$Zlo,$tmp,$Zlo
	and	$nlo,0x0f,$nlo
	xor	$Tlo,$Zlo,$Zlo
	sll	$nlo,4,$nlo
	blu	.Lghash_inner
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nlo],$Tlo
	sll	$remi,3,$remi
	xor	$Thi,$Zhi,$Zhi
	ldx	[$Htbl+$nlo],$Thi
	srlx	$Zlo,4,$Zlo
	xor	$rem,$Zhi,$Zhi
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi

	add	$inp,16,$inp
	cmp	$inp,$len
	be,pn	SIZE_T_CC,.Ldone
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nhi],$Tlo
	sll	$remi,3,$remi
	xor	$rem,$Zhi,$Zhi
	ldx	[$Htbl+$nhi],$Thi
	srlx	$Zlo,4,$Zlo
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	ldub	[$inp+15],$nlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi
	stx	$Zlo,[$Xi+8]
	xor	$rem,$Zhi,$Zhi
	stx	$Zhi,[$Xi]
	srl	$Zlo,8,$xi1
	and	$Zlo,0xff,$xi0
	ba	.Louter
	and	$xi1,0xff,$xi1
.align	32
.Ldone:
	ldx	[$Htblo+$nhi],$Tlo
	sll	$remi,3,$remi
	xor	$rem,$Zhi,$Zhi
	ldx	[$Htbl+$nhi],$Thi
	srlx	$Zlo,4,$Zlo
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi
	stx	$Zlo,[$Xi+8]
	xor	$rem,$Zhi,$Zhi
	stx	$Zhi,[$Xi]

	ret
	restore
.type	gcm_ghash_4bit,#function
.size	gcm_ghash_4bit,(.-gcm_ghash_4bit)
___

undef $inp;
undef $len;

$code.=<<___;
.globl	gcm_gmult_4bit
.align	32
gcm_gmult_4bit:
	save	%sp,-$frame,%sp
	ldub	[$Xi+15],$nlo
	add	$Htbl,8,$Htblo

1:	call	.+8
	add	%o7,rem_4bit-1b,$rem_4bit

	and	$nlo,0xf0,$nhi
	and	$nlo,0x0f,$nlo
	sll	$nlo,4,$nlo
	ldx	[$Htblo+$nlo],$Zlo
	ldx	[$Htbl+$nlo],$Zhi

	ldub	[$Xi+14],$nlo

	ldx	[$Htblo+$nhi],$Tlo
	and	$Zlo,0xf,$remi
	ldx	[$Htbl+$nhi],$Thi
	sll	$remi,3,$remi
	ldx	[$rem_4bit+$remi],$rem
	srlx	$Zlo,4,$Zlo
	mov	13,$cnt
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo

	and	$Zlo,0xf,$remi
	and	$nlo,0xf0,$nhi
	and	$nlo,0x0f,$nlo
	ba	.Lgmult_inner
	sll	$nlo,4,$nlo
.align	32
.Lgmult_inner:
	ldx	[$Htblo+$nlo],$Tlo
	sll	$remi,3,$remi
	xor	$Thi,$Zhi,$Zhi
	ldx	[$Htbl+$nlo],$Thi
	srlx	$Zlo,4,$Zlo
	xor	$rem,$Zhi,$Zhi
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	ldub	[$Xi+$cnt],$nlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nhi],$Tlo
	sll	$remi,3,$remi
	xor	$rem,$Zhi,$Zhi
	ldx	[$Htbl+$nhi],$Thi
	srlx	$Zlo,4,$Zlo
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	srlx	$Zhi,4,$Zhi
	and	$nlo,0xf0,$nhi
	addcc	$cnt,-1,$cnt
	xor	$Zlo,$tmp,$Zlo
	and	$nlo,0x0f,$nlo
	xor	$Tlo,$Zlo,$Zlo
	sll	$nlo,4,$nlo
	blu	.Lgmult_inner
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nlo],$Tlo
	sll	$remi,3,$remi
	xor	$Thi,$Zhi,$Zhi
	ldx	[$Htbl+$nlo],$Thi
	srlx	$Zlo,4,$Zlo
	xor	$rem,$Zhi,$Zhi
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nhi],$Tlo
	sll	$remi,3,$remi
	xor	$rem,$Zhi,$Zhi
	ldx	[$Htbl+$nhi],$Thi
	srlx	$Zlo,4,$Zlo
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi
	stx	$Zlo,[$Xi+8]
	xor	$rem,$Zhi,$Zhi
	stx	$Zhi,[$Xi]

	ret
	restore
.type	gcm_gmult_4bit,#function
.size	gcm_gmult_4bit,(.-gcm_gmult_4bit)
___

{{{
# Straightforward 128x128-bit multiplication using Karatsuba algorithm
# followed by pair of 64-bit reductions [with a shortcut in first one,
# which allowed to break dependency between reductions and remove one
# multiplication from critical path]. While it might be suboptimal
# with regard to sheer number of multiplications, other methods [such
# as aggregate reduction] would require more 64-bit registers, which
# we don't have in 32-bit application context.

($Xip,$Htable,$inp,$len)=map("%i$_",(0..3));

($Hhl,$Hlo,$Hhi,$Xlo,$Xhi,$xE1,$sqr, $C0,$C1,$C2,$C3,$V)=
	(map("%o$_",(0..5,7)),map("%g$_",(1..5)));

($shl,$shr)=map("%l$_",(0..7));

# For details regarding "twisted H" see ghash-x86.pl.
$code.=<<___;
.globl	gcm_init_vis3
.align	32
gcm_init_vis3:
	save	%sp,-$frame,%sp

	ldx	[%i1+0],$Hhi
	ldx	[%i1+8],$Hlo
	mov	0xE1,$Xhi
	mov	1,$Xlo
	sllx	$Xhi,57,$Xhi
	srax	$Hhi,63,$C0		! broadcast carry
	addcc	$Hlo,$Hlo,$Hlo		! H<<=1
	addxc	$Hhi,$Hhi,$Hhi
	and	$C0,$Xlo,$Xlo
	and	$C0,$Xhi,$Xhi
	xor	$Xlo,$Hlo,$Hlo
	xor	$Xhi,$Hhi,$Hhi
	stx	$Hlo,[%i0+8]		! save twisted H
	stx	$Hhi,[%i0+0]

	sethi	%hi(0xA0406080),$V
	sethi	%hi(0x20C0E000),%l0
	or	$V,%lo(0xA0406080),$V
	or	%l0,%lo(0x20C0E000),%l0
	sllx	$V,32,$V
	or	%l0,$V,$V		! (0xE0·i)&0xff=0xA040608020C0E000
	stx	$V,[%i0+16]

	ret
	restore
.type	gcm_init_vis3,#function
.size	gcm_init_vis3,.-gcm_init_vis3

.globl	gcm_gmult_vis3
.align	32
gcm_gmult_vis3:
	save	%sp,-$frame,%sp

	ldx	[$Xip+8],$Xlo		! load Xi
	ldx	[$Xip+0],$Xhi
	ldx	[$Htable+8],$Hlo	! load twisted H
	ldx	[$Htable+0],$Hhi

	mov	0xE1,%l7
	sllx	%l7,57,$xE1		! 57 is not a typo
	ldx	[$Htable+16],$V		! (0xE0·i)&0xff=0xA040608020C0E000

	xor	$Hhi,$Hlo,$Hhl		! Karatsuba pre-processing
	xmulx	$Xlo,$Hlo,$C0
	xor	$Xlo,$Xhi,$C2		! Karatsuba pre-processing
	xmulx	$C2,$Hhl,$C1
	xmulxhi	$Xlo,$Hlo,$Xlo
	xmulxhi	$C2,$Hhl,$C2
	xmulxhi	$Xhi,$Hhi,$C3
	xmulx	$Xhi,$Hhi,$Xhi

	sll	$C0,3,$sqr
	srlx	$V,$sqr,$sqr		! ·0xE0 [implicit &(7<<3)]
	xor	$C0,$sqr,$sqr
	sllx	$sqr,57,$sqr		! ($C0·0xE1)<<1<<56 [implicit &0x7f]

	xor	$C0,$C1,$C1		! Karatsuba post-processing
	xor	$Xlo,$C2,$C2
	 xor	$sqr,$Xlo,$Xlo		! real destination is $C1
	xor	$C3,$C2,$C2
	xor	$Xlo,$C1,$C1
	xor	$Xhi,$C2,$C2
	xor	$Xhi,$C1,$C1

	xmulxhi	$C0,$xE1,$Xlo		! ·0xE1<<1<<56
	 xor	$C0,$C2,$C2
	xmulx	$C1,$xE1,$C0
	 xor	$C1,$C3,$C3
	xmulxhi	$C1,$xE1,$C1

	xor	$Xlo,$C2,$C2
	xor	$C0,$C2,$C2
	xor	$C1,$C3,$C3

	stx	$C2,[$Xip+8]		! save Xi
	stx	$C3,[$Xip+0]

	ret
	restore
.type	gcm_gmult_vis3,#function
.size	gcm_gmult_vis3,.-gcm_gmult_vis3

.globl	gcm_ghash_vis3
.align	32
gcm_ghash_vis3:
	save	%sp,-$frame,%sp
	nop
	srln	$len,0,$len		! needed on v8+, "nop" on v9

	ldx	[$Xip+8],$C2		! load Xi
	ldx	[$Xip+0],$C3
	ldx	[$Htable+8],$Hlo	! load twisted H
	ldx	[$Htable+0],$Hhi

	mov	0xE1,%l7
	sllx	%l7,57,$xE1		! 57 is not a typo
	ldx	[$Htable+16],$V		! (0xE0·i)&0xff=0xA040608020C0E000

	and	$inp,7,$shl
	andn	$inp,7,$inp
	sll	$shl,3,$shl
	prefetch [$inp+63], 20
	sub	%g0,$shl,$shr

	xor	$Hhi,$Hlo,$Hhl		! Karatsuba pre-processing
.Loop:
	ldx	[$inp+8],$Xlo
	brz,pt	$shl,1f
	ldx	[$inp+0],$Xhi

	ldx	[$inp+16],$C1		! align data
	srlx	$Xlo,$shr,$C0
	sllx	$Xlo,$shl,$Xlo
	sllx	$Xhi,$shl,$Xhi
	srlx	$C1,$shr,$C1
	or	$C0,$Xhi,$Xhi
	or	$C1,$Xlo,$Xlo
1:
	add	$inp,16,$inp
	sub	$len,16,$len
	xor	$C2,$Xlo,$Xlo
	xor	$C3,$Xhi,$Xhi
	prefetch [$inp+63], 20

	xmulx	$Xlo,$Hlo,$C0
	xor	$Xlo,$Xhi,$C2		! Karatsuba pre-processing
	xmulx	$C2,$Hhl,$C1
	xmulxhi	$Xlo,$Hlo,$Xlo
	xmulxhi	$C2,$Hhl,$C2
	xmulxhi	$Xhi,$Hhi,$C3
	xmulx	$Xhi,$Hhi,$Xhi

	sll	$C0,3,$sqr
	srlx	$V,$sqr,$sqr		! ·0xE0 [implicit &(7<<3)]
	xor	$C0,$sqr,$sqr
	sllx	$sqr,57,$sqr		! ($C0·0xE1)<<1<<56 [implicit &0x7f]

	xor	$C0,$C1,$C1		! Karatsuba post-processing
	xor	$Xlo,$C2,$C2
	 xor	$sqr,$Xlo,$Xlo		! real destination is $C1
	xor	$C3,$C2,$C2
	xor	$Xlo,$C1,$C1
	xor	$Xhi,$C2,$C2
	xor	$Xhi,$C1,$C1

	xmulxhi	$C0,$xE1,$Xlo		! ·0xE1<<1<<56
	 xor	$C0,$C2,$C2
	xmulx	$C1,$xE1,$C0
	 xor	$C1,$C3,$C3
	xmulxhi	$C1,$xE1,$C1

	xor	$Xlo,$C2,$C2
	xor	$C0,$C2,$C2
	brnz,pt	$len,.Loop
	xor	$C1,$C3,$C3

	stx	$C2,[$Xip+8]		! save Xi
	stx	$C3,[$Xip+0]

	ret
	restore
.type	gcm_ghash_vis3,#function
.size	gcm_ghash_vis3,.-gcm_ghash_vis3
___
}}}
$code.=<<___;
.asciz	"GHASH for SPARCv9/VIS3, CRYPTOGAMS by <appro\@openssl.org>"
.align	4
___


# Purpose of these subroutines is to explicitly encode VIS instructions,
# so that one can compile the module without having to specify VIS
# extensions on compiler command line, e.g. -xarch=v9 vs. -xarch=v9a.
# Idea is to reserve for option to produce "universal" binary and let
# programmer detect if current CPU is VIS capable at run-time.
sub unvis3 {
my ($mnemonic,$rs1,$rs2,$rd)=@_;
my %bias = ( "g" => 0, "o" => 8, "l" => 16, "i" => 24 );
my ($ref,$opf);
my %visopf = (	"addxc"		=> 0x011,
		"addxccc"	=> 0x013,
		"xmulx"		=> 0x115,
		"xmulxhi"	=> 0x116	);

    $ref = "$mnemonic\t$rs1,$rs2,$rd";

    if ($opf=$visopf{$mnemonic}) {
	foreach ($rs1,$rs2,$rd) {
	    return $ref if (!/%([goli])([0-9])/);
	    $_=$bias{$1}+$2;
	}

	return	sprintf ".word\t0x%08x !%s",
			0x81b00000|$rd<<25|$rs1<<14|$opf<<5|$rs2,
			$ref;
    } else {
	return $ref;
    }
}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/ge;

	s/\b(xmulx[hi]*|addxc[c]{0,2})\s+(%[goli][0-7]),\s*(%[goli][0-7]),\s*(%[goli][0-7])/
		&unvis3($1,$2,$3,$4)
	 /ge;

	print $_,"\n";
}

close STDOUT;

#! /usr/bin/env perl
# Copyright 2012-2016 The OpenSSL Project Authors. All Rights Reserved.
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
# October 2012
#
# The module implements bn_GF2m_mul_2x2 polynomial multiplication used
# in bn_gf2m.c. It's kind of low-hanging mechanical port from C for
# the time being... Except that it has two code paths: one suitable
# for all SPARCv9 processors and one for VIS3-capable ones. Former
# delivers ~25-45% more, more for longer keys, heaviest DH and DSA
# verify operations on venerable UltraSPARC II. On T4 VIS3 code is
# ~100-230% faster than gcc-generated code and ~35-90% faster than
# the pure SPARCv9 code path.

$output = pop;
open STDOUT,">$output";

$locals=16*8;

$tab="%l0";

@T=("%g2","%g3");
@i=("%g4","%g5");

($a1,$a2,$a4,$a8,$a12,$a48)=map("%o$_",(0..5));
($lo,$hi,$b)=("%g1",$a8,"%o7"); $a=$lo;

$code.=<<___;
#include <sparc_arch.h>

#ifdef __arch64__
.register	%g2,#scratch
.register	%g3,#scratch
#endif

#ifdef __PIC__
SPARC_PIC_THUNK(%g1)
#endif

.globl	bn_GF2m_mul_2x2
.align	16
bn_GF2m_mul_2x2:
        SPARC_LOAD_ADDRESS_LEAF(OPENSSL_sparcv9cap_P,%g1,%g5)
        ld	[%g1+0],%g1             	! OPENSSL_sparcv9cap_P[0]

        andcc	%g1, SPARCV9_VIS3, %g0
        bz,pn	%icc,.Lsoftware
        nop

	sllx	%o1, 32, %o1
	sllx	%o3, 32, %o3
	or	%o2, %o1, %o1
	or	%o4, %o3, %o3
	.word	0x95b262ab			! xmulx   %o1, %o3, %o2
	.word	0x99b262cb			! xmulxhi %o1, %o3, %o4
	srlx	%o2, 32, %o1			! 13 cycles later
	st	%o2, [%o0+0]
	st	%o1, [%o0+4]
	srlx	%o4, 32, %o3
	st	%o4, [%o0+8]
	retl
	st	%o3, [%o0+12]

.align	16
.Lsoftware:
	save	%sp,-STACK_FRAME-$locals,%sp

	sllx	%i1,32,$a
	mov	-1,$a12
	sllx	%i3,32,$b
	or	%i2,$a,$a
	srlx	$a12,1,$a48			! 0x7fff...
	or	%i4,$b,$b
	srlx	$a12,2,$a12			! 0x3fff...
	add	%sp,STACK_BIAS+STACK_FRAME,$tab

	sllx	$a,2,$a4
	mov	$a,$a1
	sllx	$a,1,$a2

	srax	$a4,63,@i[1]			! broadcast 61st bit
	and	$a48,$a4,$a4			! (a<<2)&0x7fff...
	srlx	$a48,2,$a48
	srax	$a2,63,@i[0]			! broadcast 62nd bit
	and	$a12,$a2,$a2			! (a<<1)&0x3fff...
	srax	$a1,63,$lo			! broadcast 63rd bit
	and	$a48,$a1,$a1			! (a<<0)&0x1fff...

	sllx	$a1,3,$a8
	and	$b,$lo,$lo
	and	$b,@i[0],@i[0]
	and	$b,@i[1],@i[1]

	stx	%g0,[$tab+0*8]			! tab[0]=0
	xor	$a1,$a2,$a12
	stx	$a1,[$tab+1*8]			! tab[1]=a1
	stx	$a2,[$tab+2*8]			! tab[2]=a2
	 xor	$a4,$a8,$a48
	stx	$a12,[$tab+3*8]			! tab[3]=a1^a2
	 xor	$a4,$a1,$a1

	stx	$a4,[$tab+4*8]			! tab[4]=a4
	xor	$a4,$a2,$a2
	stx	$a1,[$tab+5*8]			! tab[5]=a1^a4
	xor	$a4,$a12,$a12
	stx	$a2,[$tab+6*8]			! tab[6]=a2^a4
	 xor	$a48,$a1,$a1
	stx	$a12,[$tab+7*8]			! tab[7]=a1^a2^a4
	 xor	$a48,$a2,$a2

	stx	$a8,[$tab+8*8]			! tab[8]=a8
	xor	$a48,$a12,$a12
	stx	$a1,[$tab+9*8]			! tab[9]=a1^a8
	 xor	$a4,$a1,$a1
	stx	$a2,[$tab+10*8]			! tab[10]=a2^a8
	 xor	$a4,$a2,$a2
	stx	$a12,[$tab+11*8]		! tab[11]=a1^a2^a8

	xor	$a4,$a12,$a12
	stx	$a48,[$tab+12*8]		! tab[12]=a4^a8
	 srlx	$lo,1,$hi
	stx	$a1,[$tab+13*8]			! tab[13]=a1^a4^a8
	 sllx	$lo,63,$lo
	stx	$a2,[$tab+14*8]			! tab[14]=a2^a4^a8
	 srlx	@i[0],2,@T[0]
	stx	$a12,[$tab+15*8]		! tab[15]=a1^a2^a4^a8

	sllx	@i[0],62,$a1
	 sllx	$b,3,@i[0]
	srlx	@i[1],3,@T[1]
	 and	@i[0],`0xf<<3`,@i[0]
	sllx	@i[1],61,$a2
	 ldx	[$tab+@i[0]],@i[0]
	 srlx	$b,4-3,@i[1]
	xor	@T[0],$hi,$hi
	 and	@i[1],`0xf<<3`,@i[1]
	xor	$a1,$lo,$lo
	 ldx	[$tab+@i[1]],@i[1]
	xor	@T[1],$hi,$hi

	xor	@i[0],$lo,$lo
	srlx	$b,8-3,@i[0]
	 xor	$a2,$lo,$lo
	and	@i[0],`0xf<<3`,@i[0]
___
for($n=1;$n<14;$n++) {
$code.=<<___;
	sllx	@i[1],`$n*4`,@T[0]
	ldx	[$tab+@i[0]],@i[0]
	srlx	@i[1],`64-$n*4`,@T[1]
	xor	@T[0],$lo,$lo
	srlx	$b,`($n+2)*4`-3,@i[1]
	xor	@T[1],$hi,$hi
	and	@i[1],`0xf<<3`,@i[1]
___
	push(@i,shift(@i)); push(@T,shift(@T));
}
$code.=<<___;
	sllx	@i[1],`$n*4`,@T[0]
	ldx	[$tab+@i[0]],@i[0]
	srlx	@i[1],`64-$n*4`,@T[1]
	xor	@T[0],$lo,$lo

	sllx	@i[0],`($n+1)*4`,@T[0]
	 xor	@T[1],$hi,$hi
	srlx	@i[0],`64-($n+1)*4`,@T[1]
	xor	@T[0],$lo,$lo
	xor	@T[1],$hi,$hi

	srlx	$lo,32,%i1
	st	$lo,[%i0+0]
	st	%i1,[%i0+4]
	srlx	$hi,32,%i2
	st	$hi,[%i0+8]
	st	%i2,[%i0+12]

	ret
	restore
.type	bn_GF2m_mul_2x2,#function
.size	bn_GF2m_mul_2x2,.-bn_GF2m_mul_2x2
.asciz	"GF(2^m) Multiplication for SPARCv9, CRYPTOGAMS by <appro\@openssl.org>"
.align	4
___

$code =~ s/\`([^\`]*)\`/eval($1)/gem;
print $code;
close STDOUT;

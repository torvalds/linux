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
# May 2011
#
# The module implements bn_GF2m_mul_2x2 polynomial multiplication used
# in bn_gf2m.c. It's kind of low-hanging mechanical port from C for
# the time being... gcc 4.3 appeared to generate poor code, therefore
# the effort. And indeed, the module delivers 55%-90%(*) improvement
# on heaviest ECDSA verify and ECDH benchmarks for 163- and 571-bit
# key lengths on z990, 30%-55%(*) - on z10, and 70%-110%(*) - on z196.
# This is for 64-bit build. In 32-bit "highgprs" case improvement is
# even higher, for example on z990 it was measured 80%-150%. ECDSA
# sign is modest 9%-12% faster. Keep in mind that these coefficients
# are not ones for bn_GF2m_mul_2x2 itself, as not all CPU time is
# burnt in it...
#
# (*)	gcc 4.1 was observed to deliver better results than gcc 4.3,
#	so that improvement coefficients can vary from one specific
#	setup to another.

$flavour = shift;

if ($flavour =~ /3[12]/) {
        $SIZE_T=4;
        $g="";
} else {
        $SIZE_T=8;
        $g="g";
}

while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

$stdframe=16*$SIZE_T+4*8;

$rp="%r2";
$a1="%r3";
$a0="%r4";
$b1="%r5";
$b0="%r6";

$ra="%r14";
$sp="%r15";

@T=("%r0","%r1");
@i=("%r12","%r13");

($a1,$a2,$a4,$a8,$a12,$a48)=map("%r$_",(6..11));
($lo,$hi,$b)=map("%r$_",(3..5)); $a=$lo; $mask=$a8;

$code.=<<___;
.text

.type	_mul_1x1,\@function
.align	16
_mul_1x1:
	lgr	$a1,$a
	sllg	$a2,$a,1
	sllg	$a4,$a,2
	sllg	$a8,$a,3

	srag	$lo,$a1,63			# broadcast 63rd bit
	nihh	$a1,0x1fff
	srag	@i[0],$a2,63			# broadcast 62nd bit
	nihh	$a2,0x3fff
	srag	@i[1],$a4,63			# broadcast 61st bit
	nihh	$a4,0x7fff
	ngr	$lo,$b
	ngr	@i[0],$b
	ngr	@i[1],$b

	lghi	@T[0],0
	lgr	$a12,$a1
	stg	@T[0],`$stdframe+0*8`($sp)	# tab[0]=0
	xgr	$a12,$a2
	stg	$a1,`$stdframe+1*8`($sp)	# tab[1]=a1
	 lgr	$a48,$a4
	stg	$a2,`$stdframe+2*8`($sp)	# tab[2]=a2
	 xgr	$a48,$a8
	stg	$a12,`$stdframe+3*8`($sp)	# tab[3]=a1^a2
	 xgr	$a1,$a4

	stg	$a4,`$stdframe+4*8`($sp)	# tab[4]=a4
	xgr	$a2,$a4
	stg	$a1,`$stdframe+5*8`($sp)	# tab[5]=a1^a4
	xgr	$a12,$a4
	stg	$a2,`$stdframe+6*8`($sp)	# tab[6]=a2^a4
	 xgr	$a1,$a48
	stg	$a12,`$stdframe+7*8`($sp)	# tab[7]=a1^a2^a4
	 xgr	$a2,$a48

	stg	$a8,`$stdframe+8*8`($sp)	# tab[8]=a8
	xgr	$a12,$a48
	stg	$a1,`$stdframe+9*8`($sp)	# tab[9]=a1^a8
	 xgr	$a1,$a4
	stg	$a2,`$stdframe+10*8`($sp)	# tab[10]=a2^a8
	 xgr	$a2,$a4
	stg	$a12,`$stdframe+11*8`($sp)	# tab[11]=a1^a2^a8

	xgr	$a12,$a4
	stg	$a48,`$stdframe+12*8`($sp)	# tab[12]=a4^a8
	 srlg	$hi,$lo,1
	stg	$a1,`$stdframe+13*8`($sp)	# tab[13]=a1^a4^a8
	 sllg	$lo,$lo,63
	stg	$a2,`$stdframe+14*8`($sp)	# tab[14]=a2^a4^a8
	 srlg	@T[0],@i[0],2
	stg	$a12,`$stdframe+15*8`($sp)	# tab[15]=a1^a2^a4^a8

	lghi	$mask,`0xf<<3`
	sllg	$a1,@i[0],62
	 sllg	@i[0],$b,3
	srlg	@T[1],@i[1],3
	 ngr	@i[0],$mask
	sllg	$a2,@i[1],61
	 srlg	@i[1],$b,4-3
	xgr	$hi,@T[0]
	 ngr	@i[1],$mask
	xgr	$lo,$a1
	xgr	$hi,@T[1]
	xgr	$lo,$a2

	xg	$lo,$stdframe(@i[0],$sp)
	srlg	@i[0],$b,8-3
	ngr	@i[0],$mask
___
for($n=1;$n<14;$n++) {
$code.=<<___;
	lg	@T[1],$stdframe(@i[1],$sp)
	srlg	@i[1],$b,`($n+2)*4`-3
	sllg	@T[0],@T[1],`$n*4`
	ngr	@i[1],$mask
	srlg	@T[1],@T[1],`64-$n*4`
	xgr	$lo,@T[0]
	xgr	$hi,@T[1]
___
	push(@i,shift(@i)); push(@T,shift(@T));
}
$code.=<<___;
	lg	@T[1],$stdframe(@i[1],$sp)
	sllg	@T[0],@T[1],`$n*4`
	srlg	@T[1],@T[1],`64-$n*4`
	xgr	$lo,@T[0]
	xgr	$hi,@T[1]

	lg	@T[0],$stdframe(@i[0],$sp)
	sllg	@T[1],@T[0],`($n+1)*4`
	srlg	@T[0],@T[0],`64-($n+1)*4`
	xgr	$lo,@T[1]
	xgr	$hi,@T[0]

	br	$ra
.size	_mul_1x1,.-_mul_1x1

.globl	bn_GF2m_mul_2x2
.type	bn_GF2m_mul_2x2,\@function
.align	16
bn_GF2m_mul_2x2:
	stm${g}	%r3,%r15,3*$SIZE_T($sp)

	lghi	%r1,-$stdframe-128
	la	%r0,0($sp)
	la	$sp,0(%r1,$sp)			# alloca
	st${g}	%r0,0($sp)			# back chain
___
if ($SIZE_T==8) {
my @r=map("%r$_",(6..9));
$code.=<<___;
	bras	$ra,_mul_1x1			# a1·b1
	stmg	$lo,$hi,16($rp)

	lg	$a,`$stdframe+128+4*$SIZE_T`($sp)
	lg	$b,`$stdframe+128+6*$SIZE_T`($sp)
	bras	$ra,_mul_1x1			# a0·b0
	stmg	$lo,$hi,0($rp)

	lg	$a,`$stdframe+128+3*$SIZE_T`($sp)
	lg	$b,`$stdframe+128+5*$SIZE_T`($sp)
	xg	$a,`$stdframe+128+4*$SIZE_T`($sp)
	xg	$b,`$stdframe+128+6*$SIZE_T`($sp)
	bras	$ra,_mul_1x1			# (a0+a1)·(b0+b1)
	lmg	@r[0],@r[3],0($rp)

	xgr	$lo,$hi
	xgr	$hi,@r[1]
	xgr	$lo,@r[0]
	xgr	$hi,@r[2]
	xgr	$lo,@r[3]
	xgr	$hi,@r[3]
	xgr	$lo,$hi
	stg	$hi,16($rp)
	stg	$lo,8($rp)
___
} else {
$code.=<<___;
	sllg	%r3,%r3,32
	sllg	%r5,%r5,32
	or	%r3,%r4
	or	%r5,%r6
	bras	$ra,_mul_1x1
	rllg	$lo,$lo,32
	rllg	$hi,$hi,32
	stmg	$lo,$hi,0($rp)
___
}
$code.=<<___;
	lm${g}	%r6,%r15,`$stdframe+128+6*$SIZE_T`($sp)
	br	$ra
.size	bn_GF2m_mul_2x2,.-bn_GF2m_mul_2x2
.string	"GF(2^m) Multiplication for s390x, CRYPTOGAMS by <appro\@openssl.org>"
___

$code =~ s/\`([^\`]*)\`/eval($1)/gem;
print $code;
close STDOUT;

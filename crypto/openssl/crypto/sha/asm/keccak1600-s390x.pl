#!/usr/bin/env perl
# Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
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
# Keccak-1600 for s390x.
#
# June 2017.
#
# Below code is [lane complementing] KECCAK_2X implementation (see
# sha/keccak1600.c) with C[5] and D[5] held in register bank. Though
# instead of actually unrolling the loop pair-wise I simply flip
# pointers to T[][] and A[][] at the end of round. Since number of
# rounds is even, last round writes to A[][] and everything works out.
# In the nutshell it's transliteration of x86_64 module, because both
# architectures have similar capabilities/limitations. Performance
# measurement is problematic as I don't have access to an idle system.
# It looks like z13 processes one byte [out of long message] in ~14
# cycles. At least the result is consistent with estimate based on
# amount of instruction and assumed instruction issue rate. It's ~2.5x
# faster than compiler-generated code.

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

my @A = map([ 8*$_, 8*($_+1), 8*($_+2), 8*($_+3), 8*($_+4) ], (0,5,10,15,20));

my @C = map("%r$_",(0,1,5..7));
my @D = map("%r$_",(8..12));
my @T = map("%r$_",(13..14));
my ($src,$dst,$iotas) = map("%r$_",(2..4));
my $sp = "%r15";

$stdframe=16*$SIZE_T+4*8;
$frame=$stdframe+25*8;

my @rhotates = ([  0,  1, 62, 28, 27 ],
                [ 36, 44,  6, 55, 20 ],
                [  3, 10, 43, 25, 39 ],
                [ 41, 45, 15, 21,  8 ],
                [ 18,  2, 61, 56, 14 ]);

{ my @C = @C;	# copy, because we mess them up...
  my @D = @D;

$code.=<<___;
.text

.type	__KeccakF1600,\@function
.align	32
__KeccakF1600:
	st${g}	%r14,$SIZE_T*14($sp)
	lg	@C[0],$A[4][0]($src)
	lg	@C[1],$A[4][1]($src)
	lg	@C[2],$A[4][2]($src)
	lg	@C[3],$A[4][3]($src)
	lg	@C[4],$A[4][4]($src)
	larl	$iotas,iotas
	j	.Loop

.align	16
.Loop:
	lg	@D[0],$A[0][0]($src)
	lg	@D[1],$A[1][1]($src)
	lg	@D[2],$A[2][2]($src)
	lg	@D[3],$A[3][3]($src)

	xgr	@C[0],@D[0]
	xg	@C[1],$A[0][1]($src)
	xg	@C[2],$A[0][2]($src)
	xg	@C[3],$A[0][3]($src)
	lgr	@D[4],@C[4]
	xg	@C[4],$A[0][4]($src)

	xg	@C[0],$A[1][0]($src)
	xgr	@C[1],@D[1]
	xg	@C[2],$A[1][2]($src)
	xg	@C[3],$A[1][3]($src)
	xg	@C[4],$A[1][4]($src)

	xg	@C[0],$A[2][0]($src)
	xg	@C[1],$A[2][1]($src)
	xgr	@C[2],@D[2]
	xg	@C[3],$A[2][3]($src)
	xg	@C[4],$A[2][4]($src)

	xg	@C[0],$A[3][0]($src)
	xg	@C[1],$A[3][1]($src)
	xg	@C[2],$A[3][2]($src)
	xgr	@C[3],@D[3]
	xg	@C[4],$A[3][4]($src)

	lgr	@T[0],@C[2]
	rllg	@C[2],@C[2],1
	xgr	@C[2],@C[0]		# D[1] = ROL64(C[2], 1) ^ C[0]

	rllg	@C[0],@C[0],1
	xgr	@C[0],@C[3]		# D[4] = ROL64(C[0], 1) ^ C[3]

	rllg	@C[3],@C[3],1
	xgr	@C[3],@C[1]		# D[2] = ROL64(C[3], 1) ^ C[1]

	rllg	@C[1],@C[1],1
	xgr	@C[1],@C[4]		# D[0] = ROL64(C[1], 1) ^ C[4]

	rllg	@C[4],@C[4],1
	xgr	@C[4],@T[0]		# D[3] = ROL64(C[4], 1) ^ C[2]
___
	(@D[0..4], @C) = (@C[1..4,0], @D);
$code.=<<___;
	xgr	@C[1],@D[1]
	xgr	@C[2],@D[2]
	xgr	@C[3],@D[3]
	 rllg	@C[1],@C[1],$rhotates[1][1]
	xgr	@C[4],@D[4]
	 rllg	@C[2],@C[2],$rhotates[2][2]
	xgr	@C[0],@D[0]

	lgr	@T[0],@C[1]
	ogr	@C[1],@C[2]
	 rllg	@C[3],@C[3],$rhotates[3][3]
	xgr	@C[1],@C[0]		#	    C[0] ^ ( C[1] | C[2])
	 rllg	@C[4],@C[4],$rhotates[4][4]
	xg	@C[1],0($iotas)
	la	$iotas,8($iotas)
	stg	@C[1],$A[0][0]($dst)	# R[0][0] = C[0] ^ ( C[1] | C[2]) ^ iotas[i]

	lgr	@T[1],@C[4]
	ngr	@C[4],@C[3]
	 lghi	@C[1],-1		# no 'not' instruction :-(
	xgr	@C[4],@C[2]		#	    C[2] ^ ( C[4] & C[3])
	 xgr	@C[2],@C[1]		# not	@C[2]
	stg	@C[4],$A[0][2]($dst)	# R[0][2] = C[2] ^ ( C[4] & C[3])
	 ogr	@C[2],@C[3]
	 xgr	@C[2],@T[0]		#	    C[1] ^ (~C[2] | C[3])

	ngr	@T[0],@C[0]
	 stg	@C[2],$A[0][1]($dst)	# R[0][1] = C[1] ^ (~C[2] | C[3])
	xgr	@T[0],@T[1]		#	    C[4] ^ ( C[1] & C[0])
	 ogr	@T[1],@C[0]
	stg	@T[0],$A[0][4]($dst)	# R[0][4] = C[4] ^ ( C[1] & C[0])
	 xgr	@T[1],@C[3]		#	    C[3] ^ ( C[4] | C[0])
	 stg	@T[1],$A[0][3]($dst)	# R[0][3] = C[3] ^ ( C[4] | C[0])


	lg	@C[0],$A[0][3]($src)
	lg	@C[4],$A[4][2]($src)
	lg	@C[3],$A[3][1]($src)
	lg	@C[1],$A[1][4]($src)
	lg	@C[2],$A[2][0]($src)

	xgr	@C[0],@D[3]
	xgr	@C[4],@D[2]
	 rllg	@C[0],@C[0],$rhotates[0][3]
	xgr	@C[3],@D[1]
	 rllg	@C[4],@C[4],$rhotates[4][2]
	xgr	@C[1],@D[4]
	 rllg	@C[3],@C[3],$rhotates[3][1]
	xgr	@C[2],@D[0]

	lgr	@T[0],@C[0]
	ogr	@C[0],@C[4]
	 rllg	@C[1],@C[1],$rhotates[1][4]
	xgr	@C[0],@C[3]		#	    C[3] ^ (C[0] |  C[4])
	 rllg	@C[2],@C[2],$rhotates[2][0]
	stg	@C[0],$A[1][3]($dst)	# R[1][3] = C[3] ^ (C[0] |  C[4])

	lgr	@T[1],@C[1]
	ngr	@C[1],@T[0]
	 lghi	@C[0],-1		# no 'not' instruction :-(
	xgr	@C[1],@C[4]		#	    C[4] ^ (C[1] &  C[0])
	 xgr	@C[4],@C[0]		# not	@C[4]
	stg	@C[1],$A[1][4]($dst)	# R[1][4] = C[4] ^ (C[1] &  C[0])

	 ogr	@C[4],@C[3]
	 xgr	@C[4],@C[2]		#	    C[2] ^ (~C[4] | C[3])

	ngr	@C[3],@C[2]
	 stg	@C[4],$A[1][2]($dst)	# R[1][2] = C[2] ^ (~C[4] | C[3])
	xgr	@C[3],@T[1]		#	    C[1] ^ (C[3] &  C[2])
	 ogr	@T[1],@C[2]
	stg	@C[3],$A[1][1]($dst)	# R[1][1] = C[1] ^ (C[3] &  C[2])
	 xgr	@T[1],@T[0]		#	    C[0] ^ (C[1] |  C[2])
	 stg	@T[1],$A[1][0]($dst)	# R[1][0] = C[0] ^ (C[1] |  C[2])


	lg	@C[2],$A[2][3]($src)
	lg	@C[3],$A[3][4]($src)
	lg	@C[1],$A[1][2]($src)
	lg	@C[4],$A[4][0]($src)
	lg	@C[0],$A[0][1]($src)

	xgr	@C[2],@D[3]
	xgr	@C[3],@D[4]
	 rllg	@C[2],@C[2],$rhotates[2][3]
	xgr	@C[1],@D[2]
	 rllg	@C[3],@C[3],$rhotates[3][4]
	xgr	@C[4],@D[0]
	 rllg	@C[1],@C[1],$rhotates[1][2]
	xgr	@C[0],@D[1]

	lgr	@T[0],@C[2]
	ngr	@C[2],@C[3]
	 rllg	@C[4],@C[4],$rhotates[4][0]
	xgr	@C[2],@C[1]		#	     C[1] ^ ( C[2] & C[3])
	lghi	@T[1],-1		# no 'not' instruction :-(
	stg	@C[2],$A[2][1]($dst)	# R[2][1] =  C[1] ^ ( C[2] & C[3])

	xgr	@C[3],@T[1]		# not	@C[3]
	lgr	@T[1],@C[4]
	ngr	@C[4],@C[3]
	 rllg	@C[0],@C[0],$rhotates[0][1]
	xgr	@C[4],@T[0]		#	     C[2] ^ ( C[4] & ~C[3])
	 ogr	@T[0],@C[1]
	stg	@C[4],$A[2][2]($dst)	# R[2][2] =  C[2] ^ ( C[4] & ~C[3])
	 xgr	@T[0],@C[0]		#	     C[0] ^ ( C[2] | C[1])

	ngr	@C[1],@C[0]
	 stg	@T[0],$A[2][0]($dst)	# R[2][0] =  C[0] ^ ( C[2] | C[1])
	xgr	@C[1],@T[1]		#	     C[4] ^ ( C[1] & C[0])
	 ogr	@C[0],@T[1]
	stg	@C[1],$A[2][4]($dst)	# R[2][4] =  C[4] ^ ( C[1] & C[0])
	 xgr	@C[0],@C[3]		#	    ~C[3] ^ ( C[0] | C[4])
	 stg	@C[0],$A[2][3]($dst)	# R[2][3] = ~C[3] ^ ( C[0] | C[4])


	lg	@C[2],$A[2][1]($src)
	lg	@C[3],$A[3][2]($src)
	lg	@C[1],$A[1][0]($src)
	lg	@C[4],$A[4][3]($src)
	lg	@C[0],$A[0][4]($src)

	xgr	@C[2],@D[1]
	xgr	@C[3],@D[2]
	 rllg	@C[2],@C[2],$rhotates[2][1]
	xgr	@C[1],@D[0]
	 rllg	@C[3],@C[3],$rhotates[3][2]
	xgr	@C[4],@D[3]
	 rllg	@C[1],@C[1],$rhotates[1][0]
	xgr	@C[0],@D[4]
	 rllg	@C[4],@C[4],$rhotates[4][3]

	lgr	@T[0],@C[2]
	ogr	@C[2],@C[3]
	lghi	@T[1],-1		# no 'not' instruction :-(
	xgr	@C[2],@C[1]		#	     C[1] ^ ( C[2] | C[3])
	xgr	@C[3],@T[1]		# not	@C[3]
	stg	@C[2],$A[3][1]($dst)	# R[3][1] =  C[1] ^ ( C[2] | C[3])

	lgr	@T[1],@C[4]
	ogr	@C[4],@C[3]
	 rllg	@C[0],@C[0],$rhotates[0][4]
	xgr	@C[4],@T[0]		#	     C[2] ^ ( C[4] | ~C[3])
	 ngr	@T[0],@C[1]
	stg	@C[4],$A[3][2]($dst)	# R[3][2] =  C[2] ^ ( C[4] | ~C[3])
	 xgr	@T[0],@C[0]		#	     C[0] ^ ( C[2] & C[1])

	ogr	@C[1],@C[0]
	 stg	@T[0],$A[3][0]($dst)	# R[3][0] =  C[0] ^ ( C[2] & C[1])
	xgr	@C[1],@T[1]		#	     C[4] ^ ( C[1] | C[0])
	 ngr	@C[0],@T[1]
	stg	@C[1],$A[3][4]($dst)	# R[3][4] =  C[4] ^ ( C[1] | C[0])
	 xgr	@C[0],@C[3]		#	    ~C[3] ^ ( C[0] & C[4])
	 stg	@C[0],$A[3][3]($dst)	# R[3][3] = ~C[3] ^ ( C[0] & C[4])


	xg	@D[2],$A[0][2]($src)
	xg	@D[3],$A[1][3]($src)
	xg	@D[1],$A[4][1]($src)
	xg	@D[4],$A[2][4]($src)
	xgr	$dst,$src		# xchg	$dst,$src
	 rllg	@D[2],@D[2],$rhotates[0][2]
	xg	@D[0],$A[3][0]($src)
	 rllg	@D[3],@D[3],$rhotates[1][3]
	xgr	$src,$dst
	 rllg	@D[1],@D[1],$rhotates[4][1]
	xgr	$dst,$src
	 rllg	@D[4],@D[4],$rhotates[2][4]
___
	@C = @D[2..4,0,1];
$code.=<<___;
	lgr	@T[0],@C[0]
	ngr	@C[0],@C[1]
	lghi	@T[1],-1		# no 'not' instruction :-(
	xgr	@C[0],@C[4]		#	     C[4] ^ ( C[0] & C[1])
	xgr	@C[1],@T[1]		# not	@C[1]
	stg	@C[0],$A[4][4]($src)	# R[4][4] =  C[4] ^ ( C[0] & C[1])

	lgr	@T[1],@C[2]
	ngr	@C[2],@C[1]
	 rllg	@D[0],@D[0],$rhotates[3][0]
	xgr	@C[2],@T[0]		#	     C[0] ^ ( C[2] & ~C[1])
	 ogr	@T[0],@C[4]
	stg	@C[2],$A[4][0]($src)	# R[4][0] =  C[0] ^ ( C[2] & ~C[1])
	 xgr	@T[0],@C[3]		#	     C[3] ^ ( C[0] | C[4])

	ngr	@C[4],@C[3]
	 stg	@T[0],$A[4][3]($src)	# R[4][3] =  C[3] ^ ( C[0] | C[4])
	xgr	@C[4],@T[1]		#	     C[2] ^ ( C[4] & C[3])
	 ogr	@C[3],@T[1]
	stg	@C[4],$A[4][2]($src)	# R[4][2] =  C[2] ^ ( C[4] & C[3])
	 xgr	@C[3],@C[1]		#	    ~C[1] ^ ( C[2] | C[3])

	lgr	@C[1],@C[0]		# harmonize with the loop top
	lgr	@C[0],@T[0]
	 stg	@C[3],$A[4][1]($src)	# R[4][1] = ~C[1] ^ ( C[2] | C[3])

	tmll	$iotas,255
	jnz	.Loop

	l${g}	%r14,$SIZE_T*14($sp)
	br	%r14
.size	__KeccakF1600,.-__KeccakF1600
___
}
{
$code.=<<___;
.type	KeccakF1600,\@function
.align	32
KeccakF1600:
.LKeccakF1600:
	lghi	%r1,-$frame
	stm${g}	%r6,%r15,$SIZE_T*6($sp)
	lgr	%r0,$sp
	la	$sp,0(%r1,$sp)
	st${g}	%r0,0($sp)

	lghi	@D[0],-1		# no 'not' instruction :-(
	lghi	@D[1],-1
	lghi	@D[2],-1
	lghi	@D[3],-1
	lghi	@D[4],-1
	lghi	@T[0],-1
	xg	@D[0],$A[0][1]($src)
	xg	@D[1],$A[0][2]($src)
	xg	@D[2],$A[1][3]($src)
	xg	@D[3],$A[2][2]($src)
	xg	@D[4],$A[3][2]($src)
	xg	@T[0],$A[4][0]($src)
	stmg	@D[0],@D[1],$A[0][1]($src)
	stg	@D[2],$A[1][3]($src)
	stg	@D[3],$A[2][2]($src)
	stg	@D[4],$A[3][2]($src)
	stg	@T[0],$A[4][0]($src)

	la	$dst,$stdframe($sp)

	bras	%r14,__KeccakF1600

	lghi	@D[0],-1		# no 'not' instruction :-(
	lghi	@D[1],-1
	lghi	@D[2],-1
	lghi	@D[3],-1
	lghi	@D[4],-1
	lghi	@T[0],-1
	xg	@D[0],$A[0][1]($src)
	xg	@D[1],$A[0][2]($src)
	xg	@D[2],$A[1][3]($src)
	xg	@D[3],$A[2][2]($src)
	xg	@D[4],$A[3][2]($src)
	xg	@T[0],$A[4][0]($src)
	stmg	@D[0],@D[1],$A[0][1]($src)
	stg	@D[2],$A[1][3]($src)
	stg	@D[3],$A[2][2]($src)
	stg	@D[4],$A[3][2]($src)
	stg	@T[0],$A[4][0]($src)

	lm${g}	%r6,%r15,$frame+6*$SIZE_T($sp)
	br	%r14
.size	KeccakF1600,.-KeccakF1600
___
}
{ my ($A_flat,$inp,$len,$bsz) = map("%r$_",(2..5));

$code.=<<___;
.globl	SHA3_absorb
.type	SHA3_absorb,\@function
.align	32
SHA3_absorb:
	lghi	%r1,-$frame
	stm${g}	%r5,%r15,$SIZE_T*5($sp)
	lgr	%r0,$sp
	la	$sp,0(%r1,$sp)
	st${g}	%r0,0($sp)

	lghi	@D[0],-1		# no 'not' instruction :-(
	lghi	@D[1],-1
	lghi	@D[2],-1
	lghi	@D[3],-1
	lghi	@D[4],-1
	lghi	@T[0],-1
	xg	@D[0],$A[0][1]($src)
	xg	@D[1],$A[0][2]($src)
	xg	@D[2],$A[1][3]($src)
	xg	@D[3],$A[2][2]($src)
	xg	@D[4],$A[3][2]($src)
	xg	@T[0],$A[4][0]($src)
	stmg	@D[0],@D[1],$A[0][1]($src)
	stg	@D[2],$A[1][3]($src)
	stg	@D[3],$A[2][2]($src)
	stg	@D[4],$A[3][2]($src)
	stg	@T[0],$A[4][0]($src)

.Loop_absorb:
	cl${g}r	$len,$bsz
	jl	.Ldone_absorb

	srl${g}	$bsz,3
	la	%r1,0($A_flat)

.Lblock_absorb:
	lrvg	%r0,0($inp)
	la	$inp,8($inp)
	xg	%r0,0(%r1)
	a${g}hi	$len,-8
	stg	%r0,0(%r1)
	la	%r1,8(%r1)
	brct	$bsz,.Lblock_absorb

	stm${g}	$inp,$len,$frame+3*$SIZE_T($sp)
	la	$dst,$stdframe($sp)
	bras	%r14,__KeccakF1600
	lm${g}	$inp,$bsz,$frame+3*$SIZE_T($sp)
	j	.Loop_absorb

.align	16
.Ldone_absorb:
	lghi	@D[0],-1		# no 'not' instruction :-(
	lghi	@D[1],-1
	lghi	@D[2],-1
	lghi	@D[3],-1
	lghi	@D[4],-1
	lghi	@T[0],-1
	xg	@D[0],$A[0][1]($src)
	xg	@D[1],$A[0][2]($src)
	xg	@D[2],$A[1][3]($src)
	xg	@D[3],$A[2][2]($src)
	xg	@D[4],$A[3][2]($src)
	xg	@T[0],$A[4][0]($src)
	stmg	@D[0],@D[1],$A[0][1]($src)
	stg	@D[2],$A[1][3]($src)
	stg	@D[3],$A[2][2]($src)
	stg	@D[4],$A[3][2]($src)
	stg	@T[0],$A[4][0]($src)

	lgr	%r2,$len		# return value

	lm${g}	%r6,%r15,$frame+6*$SIZE_T($sp)
	br	%r14
.size	SHA3_absorb,.-SHA3_absorb
___
}
{ my ($A_flat,$out,$len,$bsz) = map("%r$_",(2..5));

$code.=<<___;
.globl	SHA3_squeeze
.type	SHA3_squeeze,\@function
.align	32
SHA3_squeeze:
	srl${g}	$bsz,3
	st${g}	%r14,2*$SIZE_T($sp)
	lghi	%r14,8
	st${g}	$bsz,5*$SIZE_T($sp)
	la	%r1,0($A_flat)

	j	.Loop_squeeze

.align	16
.Loop_squeeze:
	cl${g}r $len,%r14
	jl	.Ltail_squeeze

	lrvg	%r0,0(%r1)
	la	%r1,8(%r1)
	stg	%r0,0($out)
	la	$out,8($out)
	a${g}hi	$len,-8			# len -= 8
	jz	.Ldone_squeeze

	brct	$bsz,.Loop_squeeze	# bsz--

	stm${g}	$out,$len,3*$SIZE_T($sp)
	bras	%r14,.LKeccakF1600
	lm${g}	$out,$bsz,3*$SIZE_T($sp)
	lghi	%r14,8
	la	%r1,0($A_flat)
	j	.Loop_squeeze

.Ltail_squeeze:
	lg	%r0,0(%r1)
.Loop_tail_squeeze:
	stc	%r0,0($out)
	la	$out,1($out)
	srlg	%r0,8
	brct	$len,.Loop_tail_squeeze

.Ldone_squeeze:
	l${g}	%r14,2*$SIZE_T($sp)
	br	%r14
.size	SHA3_squeeze,.-SHA3_squeeze
___
}
$code.=<<___;
.align	256
	.quad	0,0,0,0,0,0,0,0
.type	iotas,\@object
iotas:
	.quad	0x0000000000000001
	.quad	0x0000000000008082
	.quad	0x800000000000808a
	.quad	0x8000000080008000
	.quad	0x000000000000808b
	.quad	0x0000000080000001
	.quad	0x8000000080008081
	.quad	0x8000000000008009
	.quad	0x000000000000008a
	.quad	0x0000000000000088
	.quad	0x0000000080008009
	.quad	0x000000008000000a
	.quad	0x000000008000808b
	.quad	0x800000000000008b
	.quad	0x8000000000008089
	.quad	0x8000000000008003
	.quad	0x8000000000008002
	.quad	0x8000000000000080
	.quad	0x000000000000800a
	.quad	0x800000008000000a
	.quad	0x8000000080008081
	.quad	0x8000000000008080
	.quad	0x0000000080000001
	.quad	0x8000000080008008
.size	iotas,.-iotas
.asciz	"Keccak-1600 absorb and squeeze for s390x, CRYPTOGAMS by <appro\@openssl.org>"
___

# unlike 32-bit shift 64-bit one takes three arguments
$code =~ s/(srlg\s+)(%r[0-9]+),/$1$2,$2,/gm;

print $code;
close STDOUT;

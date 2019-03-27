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
# [ABI- and endian-neutral] Keccak-1600 for C64x.
#
# June 2017.
#
# This is straightforward KECCAK_1X_ALT variant (see sha/keccak1600.c)
# with bit interleaving. 64-bit values are simply split between A- and
# B-files, with A-file holding least significant halves. This works
# out perfectly, because all operations including cross-communications
# [in rotate operations] are always complementary. Performance is
# [incredible for a 32-bit processor] 10.9 cycles per processed byte
# for r=1088, which corresponds to SHA3-256. This is >15x faster than
# compiler-generated KECCAK_1X_ALT code, and >10x than other variants.
# On average processor ends up issuing ~4.5 instructions per cycle...

my @A = map([ $_, ($_+1), ($_+2), ($_+3), ($_+4) ], (5,10,16,21,26));
   $A[1][4] = 31;	# B14 is reserved, A14 is used as iota[]
   ($A[3][0],$A[4][1]) = ($A[4][1],$A[3][0]);
my @C = (0..4,$A[3][0],$A[4][0]);
my $iotas = "A14";

my @rhotates = ([  0,  1, 62, 28, 27 ],
                [ 36, 44,  6, 55, 20 ],
                [  3, 10, 43, 25, 39 ],
                [ 41, 45, 15, 21,  8 ],
                [ 18,  2, 61, 56, 14 ]);

sub ROL64 {
    my ($src,$rot,$dst,$p) = @_;

    if ($rot&1) {
$code.=<<___;
$p	ROTL	B$src,$rot/2+1,A$dst
||	ROTL	A$src,$rot/2,  B$dst
___
    } else {
$code.=<<___;
$p	ROTL	A$src,$rot/2,A$dst
||	ROTL	B$src,$rot/2,B$dst
___
    }
}

########################################################################
# Stack frame layout
#
# SP--->+------+------+
#       |      |      |
# +1--->+------+------+<- -9	below 4 slots are used by KeccakF1600_int
#       |      |      |
# +2--->+------+------+<- -8
#       |      |      |
# +3--->+------+------+<- -7
#       | A2   | A3   |		A3:A2 are preserved by KeccakF1600_int
# +4--->+------+------+<- -6
#       | B2   | B3   |		B3:B2 are preserved by KeccakF1600_int
# +5--->+------+------+<- -5	below is ABI-compliant layout
#       | A10  | A11  |
# +6--->+------+------+<- -4
#       | A12  | A13  |
# +7--->+------+------+<- -3
#       | A14  | B3   |
# +8--->+------+------+<- -2
#       | B10  | B11  |
# +9--->+------+------+<- -1
#       | B12  | B13  |
#       +------+------+<---FP
#       | A15  |
#       +------+--

$code.=<<___;
	.text

	.if	.ASSEMBLER_VERSION<7000000
	.asg	0,__TI_EABI__
	.endif
	.if	__TI_EABI__
	.nocmp
	.asg	KeccakF1600,_KeccakF1600
	.asg	SHA3_absorb,_SHA3_absorb
	.asg	SHA3_squeeze,_SHA3_squeeze
	.endif

	.asg	B3,RA
	.asg	A15,FP
	.asg	B15,SP

	.align	32
_KeccakF1600_int:
	.asmfunc
	STDW	A3:A2,*FP[-7]
||	STDW	B3:B2,*SP[4]
_KeccakF1600_cheat:
	.if	__TI_EABI__
	ADDKPC	_KeccakF1600_int,B0
||	MVKL	\$PCR_OFFSET(iotas,_KeccakF1600_int),$iotas
	MVKH	\$PCR_OFFSET(iotas,_KeccakF1600_int),$iotas
	.else
	ADDKPC	_KeccakF1600_int,B0
||	MVKL	(iotas-_KeccakF1600_int),$iotas
	MVKH	(iotas-_KeccakF1600_int),$iotas
	.endif
	ADD	B0,$iotas,$iotas
loop?:
	XOR	A$A[0][2],A$A[1][2],A$C[2]	; Theta
||	XOR	B$A[0][2],B$A[1][2],B$C[2]
||	XOR	A$A[0][3],A$A[1][3],A$C[3]
||	XOR	B$A[0][3],B$A[1][3],B$C[3]
||	XOR	A$A[0][0],A$A[1][0],A$C[0]
||	XOR	B$A[0][0],B$A[1][0],B$C[0]
	XOR	A$A[2][2],A$C[2],A$C[2]
||	XOR	B$A[2][2],B$C[2],B$C[2]
||	XOR	A$A[2][3],A$C[3],A$C[3]
||	XOR	B$A[2][3],B$C[3],B$C[3]
||	XOR	A$A[2][0],A$C[0],A$C[0]
||	XOR	B$A[2][0],B$C[0],B$C[0]
	XOR	A$A[3][2],A$C[2],A$C[2]
||	XOR	B$A[3][2],B$C[2],B$C[2]
||	XOR	A$A[3][3],A$C[3],A$C[3]
||	XOR	B$A[3][3],B$C[3],B$C[3]
||	XOR	A$A[3][0],A$C[0],A$C[0]
||	XOR	B$A[3][0],B$C[0],B$C[0]
	XOR	A$A[4][2],A$C[2],A$C[2]
||	XOR	B$A[4][2],B$C[2],B$C[2]
||	XOR	A$A[4][3],A$C[3],A$C[3]
||	XOR	B$A[4][3],B$C[3],B$C[3]
||	XOR	A$A[4][0],A$C[0],A$C[0]
||	XOR	B$A[4][0],B$C[0],B$C[0]
	XOR	A$A[0][4],A$A[1][4],A$C[4]
||	XOR	B$A[0][4],B$A[1][4],B$C[4]
||	XOR	A$A[0][1],A$A[1][1],A$C[1]
||	XOR	B$A[0][1],B$A[1][1],B$C[1]
||	STDW	A$A[3][0]:A$A[4][0],*SP[1]	; offload some data
	STDW	B$A[3][0]:B$A[4][0],*SP[2]
||	XOR	A$A[2][4],A$C[4],A$C[4]
||	XOR	B$A[2][4],B$C[4],B$C[4]
||	XOR	A$A[2][1],A$C[1],A$C[1]
||	XOR	B$A[2][1],B$C[1],B$C[1]
||	ROTL	B$C[2],1,A$C[5]			; ROL64(C[2],1)
||	ROTL	A$C[2],0,B$C[5]
	XOR	A$A[3][4],A$C[4],A$C[4]
||	XOR	B$A[3][4],B$C[4],B$C[4]
||	XOR	A$A[3][1],A$C[1],A$C[1]
||	XOR	B$A[3][1],B$C[1],B$C[1]
||	ROTL	B$C[3],1,A$C[6]			; ROL64(C[3],1)
||	ROTL	A$C[3],0,B$C[6]
	XOR	A$A[4][4],A$C[4],A$C[4]
||	XOR	B$A[4][4],B$C[4],B$C[4]
||	XOR	A$A[4][1],A$C[1],A$C[1]
||	XOR	B$A[4][1],B$C[1],B$C[1]
||	XOR	A$C[0],A$C[5],A$C[5]		; C[0] ^ ROL64(C[2],1)
||	XOR	B$C[0],B$C[5],B$C[5]
	XOR	A$C[5],A$A[0][1],A$A[0][1]
||	XOR	B$C[5],B$A[0][1],B$A[0][1]
||	XOR	A$C[5],A$A[1][1],A$A[1][1]
||	XOR	B$C[5],B$A[1][1],B$A[1][1]
||	XOR	A$C[5],A$A[2][1],A$A[2][1]
||	XOR	B$C[5],B$A[2][1],B$A[2][1]
	XOR	A$C[5],A$A[3][1],A$A[3][1]
||	XOR	B$C[5],B$A[3][1],B$A[3][1]
||	XOR	A$C[5],A$A[4][1],A$A[4][1]
||	XOR	B$C[5],B$A[4][1],B$A[4][1]
||	ROTL	B$C[4],1,A$C[5]			; ROL64(C[4],1)
||	ROTL	A$C[4],0,B$C[5]
||	XOR	A$C[1],A$C[6],A$C[6]		; C[1] ^ ROL64(C[3],1)
||	XOR	B$C[1],B$C[6],B$C[6]
	XOR	A$C[6],A$A[0][2],A$A[0][2]
||	XOR	B$C[6],B$A[0][2],B$A[0][2]
||	XOR	A$C[6],A$A[1][2],A$A[1][2]
||	XOR	B$C[6],B$A[1][2],B$A[1][2]
||	XOR	A$C[6],A$A[2][2],A$A[2][2]
||	XOR	B$C[6],B$A[2][2],B$A[2][2]
||	ROTL	B$C[1],1,A$C[1]			; ROL64(C[1],1)
||	ROTL	A$C[1],0,B$C[1]
	XOR	A$C[6],A$A[3][2],A$A[3][2]
||	XOR	B$C[6],B$A[3][2],B$A[3][2]
||	XOR	A$C[6],A$A[4][2],A$A[4][2]
||	XOR	B$C[6],B$A[4][2],B$A[4][2]
||	ROTL	B$C[0],1,A$C[6]			; ROL64(C[0],1)
||	ROTL	A$C[0],0,B$C[6]
||	XOR	A$C[5],A$C[2],A$C[2]		; C[2] ^= ROL64(C[4],1)
||	XOR	B$C[5],B$C[2],B$C[2]
	XOR	A$C[2],A$A[0][3],A$A[0][3]
||	XOR	B$C[2],B$A[0][3],B$A[0][3]
||	XOR	A$C[2],A$A[1][3],A$A[1][3]
||	XOR	B$C[2],B$A[1][3],B$A[1][3]
||	XOR	A$C[2],A$A[2][3],A$A[2][3]
||	XOR	B$C[2],B$A[2][3],B$A[2][3]
	XOR	A$C[6],A$C[3],A$C[3]		; C[3] ^= ROL64(C[0],1)
||	XOR	B$C[6],B$C[3],B$C[3]
||	LDDW	*FP[-9],A$A[3][0]:A$A[4][0]	; restore offloaded data
||	LDDW	*SP[2],B$A[3][0]:B$A[4][0]
||	XOR	A$C[2],A$A[3][3],A$A[3][3]
||	XOR	B$C[2],B$A[3][3],B$A[3][3]
	XOR	A$C[2],A$A[4][3],A$A[4][3]
||	XOR	B$C[2],B$A[4][3],B$A[4][3]
||	XOR	A$C[3],A$A[0][4],A$A[0][4]
||	XOR	B$C[3],B$A[0][4],B$A[0][4]
||	XOR	A$C[3],A$A[1][4],A$A[1][4]
||	XOR	B$C[3],B$A[1][4],B$A[1][4]
	XOR	A$C[3],A$A[2][4],A$A[2][4]
||	XOR	B$C[3],B$A[2][4],B$A[2][4]
||	XOR	A$C[3],A$A[3][4],A$A[3][4]
||	XOR	B$C[3],B$A[3][4],B$A[3][4]
||	XOR	A$C[3],A$A[4][4],A$A[4][4]
||	XOR	B$C[3],B$A[4][4],B$A[4][4]
	XOR	A$C[1],A$C[4],A$C[4]		; C[4] ^= ROL64(C[1],1)
||	XOR	B$C[1],B$C[4],B$C[4]
||	MV	A$A[0][1],A$C[1]		; Rho+Pi, "early start"
||	MV	B$A[0][1],B$C[1]
___
	&ROL64	($A[1][1],$rhotates[1][1],$A[0][1],"||");
$code.=<<___;
	XOR	A$C[4],A$A[0][0],A$A[0][0]
||	XOR	B$C[4],B$A[0][0],B$A[0][0]
||	XOR	A$C[4],A$A[1][0],A$A[1][0]
||	XOR	B$C[4],B$A[1][0],B$A[1][0]
||	MV	A$A[0][3],A$C[3]
||	MV	B$A[0][3],B$C[3]
___
	&ROL64	($A[3][3],$rhotates[3][3],$A[0][3],"||");
$code.=<<___;
	XOR	A$C[4],A$A[2][0],A$A[2][0]
||	XOR	B$C[4],B$A[2][0],B$A[2][0]
||	XOR	A$C[4],A$A[3][0],A$A[3][0]
||	XOR	B$C[4],B$A[3][0],B$A[3][0]
||	MV	A$A[0][2],A$C[2]
||	MV	B$A[0][2],B$C[2]
___
	&ROL64	($A[2][2],$rhotates[2][2],$A[0][2],"||");
$code.=<<___;
	XOR	A$C[4],A$A[4][0],A$A[4][0]
||	XOR	B$C[4],B$A[4][0],B$A[4][0]
||	MV	A$A[0][4],A$C[4]
||	MV	B$A[0][4],B$C[4]
___
	&ROL64	($A[4][4],$rhotates[4][4],$A[0][4],"||");

	&ROL64	($A[1][4],$rhotates[1][4],$A[1][1]);
$code.=<<___;
||	LDW	*${iotas}++[2],A$C[0]
___
	&ROL64	($A[2][3],$rhotates[2][3],$A[2][2]);
$code.=<<___;
||	LDW	*${iotas}[-1],B$C[0]
___
	&ROL64	($A[3][2],$rhotates[3][2],$A[3][3]);
	&ROL64	($A[4][1],$rhotates[4][1],$A[4][4]);

	&ROL64	($A[4][2],$rhotates[4][2],$A[1][4]);
	&ROL64	($A[3][4],$rhotates[3][4],$A[2][3]);
	&ROL64	($A[2][1],$rhotates[2][1],$A[3][2]);
	&ROL64	($A[1][3],$rhotates[1][3],$A[4][1]);

	&ROL64	($A[2][4],$rhotates[2][4],$A[4][2]);
	&ROL64	($A[4][3],$rhotates[4][3],$A[3][4]);
	&ROL64	($A[1][2],$rhotates[1][2],$A[2][1]);
	&ROL64	($A[3][1],$rhotates[3][1],$A[1][3]);

	&ROL64	($A[4][0],$rhotates[4][0],$A[2][4]);
	&ROL64	($A[3][0],$rhotates[3][0],$A[4][3]);
	&ROL64	($A[2][0],$rhotates[2][0],$A[1][2]);
	&ROL64	($A[1][0],$rhotates[1][0],$A[3][1]);

	#&ROL64	($C[3],   $rhotates[0][3],$A[1][0]);	# moved below
	&ROL64	($C[1],   $rhotates[0][1],$A[2][0]);
	&ROL64	($C[4],   $rhotates[0][4],$A[3][0]);
	&ROL64	($C[2],   $rhotates[0][2],$A[4][0]);
$code.=<<___;
||	ANDN	A$A[0][2],A$A[0][1],A$C[4]	; Chi+Iota
||	ANDN	B$A[0][2],B$A[0][1],B$C[4]
||	ANDN	A$A[0][3],A$A[0][2],A$C[1]
||	ANDN	B$A[0][3],B$A[0][2],B$C[1]
||	ANDN	A$A[0][4],A$A[0][3],A$C[2]
||	ANDN	B$A[0][4],B$A[0][3],B$C[2]
___
	&ROL64	($C[3],   $rhotates[0][3],$A[1][0]);
$code.=<<___;
||	ANDN	A$A[0][0],A$A[0][4],A$C[3]
||	ANDN	B$A[0][0],B$A[0][4],B$C[3]
||	XOR	A$C[4],A$A[0][0],A$A[0][0]
||	XOR	B$C[4],B$A[0][0],B$A[0][0]
||	ANDN	A$A[0][1],A$A[0][0],A$C[4]
||	ANDN	B$A[0][1],B$A[0][0],B$C[4]
	XOR	A$C[1],A$A[0][1],A$A[0][1]
||	XOR	B$C[1],B$A[0][1],B$A[0][1]
||	XOR	A$C[2],A$A[0][2],A$A[0][2]
||	XOR	B$C[2],B$A[0][2],B$A[0][2]
||	XOR	A$C[3],A$A[0][3],A$A[0][3]
||	XOR	B$C[3],B$A[0][3],B$A[0][3]
	XOR	A$C[4],A$A[0][4],A$A[0][4]
||	XOR	B$C[4],B$A[0][4],B$A[0][4]
||	XOR	A$C[0],A$A[0][0],A$A[0][0]	; A[0][0] ^= iotas[i++];
||	XOR	B$C[0],B$A[0][0],B$A[0][0]
||	EXTU	$iotas,24,24,A0			; A0 is A$C[0], as we done?

	ANDN	A$A[1][2],A$A[1][1],A$C[4]
||	ANDN	B$A[1][2],B$A[1][1],B$C[4]
||	ANDN	A$A[1][3],A$A[1][2],A$C[1]
||	ANDN	B$A[1][3],B$A[1][2],B$C[1]
||	ANDN	A$A[1][4],A$A[1][3],A$C[2]
||	ANDN	B$A[1][4],B$A[1][3],B$C[2]
	ANDN	A$A[1][0],A$A[1][4],A$C[3]
||	ANDN	B$A[1][0],B$A[1][4],B$C[3]
||	XOR	A$C[4],A$A[1][0],A$A[1][0]
||	XOR	B$C[4],B$A[1][0],B$A[1][0]
||	ANDN	A$A[1][1],A$A[1][0],A$C[4]
||	ANDN	B$A[1][1],B$A[1][0],B$C[4]
	XOR	A$C[1],A$A[1][1],A$A[1][1]
||	XOR	B$C[1],B$A[1][1],B$A[1][1]
||	XOR	A$C[2],A$A[1][2],A$A[1][2]
||	XOR	B$C[2],B$A[1][2],B$A[1][2]
||	XOR	A$C[3],A$A[1][3],A$A[1][3]
||	XOR	B$C[3],B$A[1][3],B$A[1][3]
	XOR	A$C[4],A$A[1][4],A$A[1][4]
||	XOR	B$C[4],B$A[1][4],B$A[1][4]

||	ANDN	A$A[2][2],A$A[2][1],A$C[4]
||	ANDN	B$A[2][2],B$A[2][1],B$C[4]
||	ANDN	A$A[2][3],A$A[2][2],A$C[1]
||	ANDN	B$A[2][3],B$A[2][2],B$C[1]
	ANDN	A$A[2][4],A$A[2][3],A$C[2]
||	ANDN	B$A[2][4],B$A[2][3],B$C[2]
||	ANDN	A$A[2][0],A$A[2][4],A$C[3]
||	ANDN	B$A[2][0],B$A[2][4],B$C[3]
||	XOR	A$C[4],A$A[2][0],A$A[2][0]
||	XOR	B$C[4],B$A[2][0],B$A[2][0]
	ANDN	A$A[2][1],A$A[2][0],A$C[4]
||	ANDN	B$A[2][1],B$A[2][0],B$C[4]
||	XOR	A$C[1],A$A[2][1],A$A[2][1]
||	XOR	B$C[1],B$A[2][1],B$A[2][1]
||	XOR	A$C[2],A$A[2][2],A$A[2][2]
||	XOR	B$C[2],B$A[2][2],B$A[2][2]
	XOR	A$C[3],A$A[2][3],A$A[2][3]
||	XOR	B$C[3],B$A[2][3],B$A[2][3]
||	XOR	A$C[4],A$A[2][4],A$A[2][4]
||	XOR	B$C[4],B$A[2][4],B$A[2][4]

	ANDN	A$A[3][2],A$A[3][1],A$C[4]
||	ANDN	B$A[3][2],B$A[3][1],B$C[4]
||	ANDN	A$A[3][3],A$A[3][2],A$C[1]
||	ANDN	B$A[3][3],B$A[3][2],B$C[1]
||	ANDN	A$A[3][4],A$A[3][3],A$C[2]
||	ANDN	B$A[3][4],B$A[3][3],B$C[2]
	ANDN	A$A[3][0],A$A[3][4],A$C[3]
||	ANDN	B$A[3][0],B$A[3][4],B$C[3]
||	XOR	A$C[4],A$A[3][0],A$A[3][0]
||	XOR	B$C[4],B$A[3][0],B$A[3][0]
||	ANDN	A$A[3][1],A$A[3][0],A$C[4]
||	ANDN	B$A[3][1],B$A[3][0],B$C[4]
	XOR	A$C[1],A$A[3][1],A$A[3][1]
||	XOR	B$C[1],B$A[3][1],B$A[3][1]
||	XOR	A$C[2],A$A[3][2],A$A[3][2]
||	XOR	B$C[2],B$A[3][2],B$A[3][2]
||	XOR	A$C[3],A$A[3][3],A$A[3][3]
||[A0]	BNOP	loop?
	XOR	B$C[3],B$A[3][3],B$A[3][3]
||	XOR	A$C[4],A$A[3][4],A$A[3][4]
||	XOR	B$C[4],B$A[3][4],B$A[3][4]
||[!A0]	LDDW	*FP[-7],A3:A2
||[!A0]	LDDW	*SP[4], RA:B2

	ANDN	A$A[4][2],A$A[4][1],A$C[4]
||	ANDN	B$A[4][2],B$A[4][1],B$C[4]
||	ANDN	A$A[4][3],A$A[4][2],A$C[1]
||	ANDN	B$A[4][3],B$A[4][2],B$C[1]
||	ANDN	A$A[4][4],A$A[4][3],A$C[2]
||	ANDN	B$A[4][4],B$A[4][3],B$C[2]
	ANDN	A$A[4][0],A$A[4][4],A$C[3]
||	ANDN	B$A[4][0],B$A[4][4],B$C[3]
||	XOR	A$C[4],A$A[4][0],A$A[4][0]
||	XOR	B$C[4],B$A[4][0],B$A[4][0]
||	ANDN	A$A[4][1],A$A[4][0],A$C[4]
||	ANDN	B$A[4][1],B$A[4][0],B$C[4]
	XOR	A$C[1],A$A[4][1],A$A[4][1]
||	XOR	B$C[1],B$A[4][1],B$A[4][1]
||	XOR	A$C[2],A$A[4][2],A$A[4][2]
||	XOR	B$C[2],B$A[4][2],B$A[4][2]
||	XOR	A$C[3],A$A[4][3],A$A[4][3]
||	XOR	B$C[3],B$A[4][3],B$A[4][3]
	XOR	A$C[4],A$A[4][4],A$A[4][4]
||	XOR	B$C[4],B$A[4][4],B$A[4][4]
;;===== branch to loop? is taken here

	BNOP	RA,5
	.endasmfunc

	.newblock
	.global	_KeccakF1600
	.align	32
_KeccakF1600:
	.asmfunc stack_usage(80)
	STW	FP,*SP--(80)			; save frame pointer
||	MV	SP,FP
	STDW	B13:B12,*SP[9]
||	STDW	A13:A12,*FP[-4]
	STDW	B11:B10,*SP[8]
||	STDW	A11:A10,*FP[-5]
	STW	RA, *SP[15]
||	STW	A14,*FP[-6]
||	MV	A4,A2
||	ADD	4,A4,B2

	LDW	*A2++[2],A$A[0][0]		; load A[5][5]
||	LDW	*B2++[2],B$A[0][0]
	LDW	*A2++[2],A$A[0][1]
||	LDW	*B2++[2],B$A[0][1]
	LDW	*A2++[2],A$A[0][2]
||	LDW	*B2++[2],B$A[0][2]
	LDW	*A2++[2],A$A[0][3]
||	LDW	*B2++[2],B$A[0][3]
	LDW	*A2++[2],A$A[0][4]
||	LDW	*B2++[2],B$A[0][4]

	LDW	*A2++[2],A$A[1][0]
||	LDW	*B2++[2],B$A[1][0]
	LDW	*A2++[2],A$A[1][1]
||	LDW	*B2++[2],B$A[1][1]
	LDW	*A2++[2],A$A[1][2]
||	LDW	*B2++[2],B$A[1][2]
	LDW	*A2++[2],A$A[1][3]
||	LDW	*B2++[2],B$A[1][3]
	LDW	*A2++[2],A$A[1][4]
||	LDW	*B2++[2],B$A[1][4]

	LDW	*A2++[2],A$A[2][0]
||	LDW	*B2++[2],B$A[2][0]
	LDW	*A2++[2],A$A[2][1]
||	LDW	*B2++[2],B$A[2][1]
	LDW	*A2++[2],A$A[2][2]
||	LDW	*B2++[2],B$A[2][2]
	LDW	*A2++[2],A$A[2][3]
||	LDW	*B2++[2],B$A[2][3]
	LDW	*A2++[2],A$A[2][4]
||	LDW	*B2++[2],B$A[2][4]

	LDW	*A2++[2],A$A[3][0]
||	LDW	*B2++[2],B$A[3][0]
	LDW	*A2++[2],A$A[3][1]
||	LDW	*B2++[2],B$A[3][1]
	LDW	*A2++[2],A$A[3][2]
||	LDW	*B2++[2],B$A[3][2]
	LDW	*A2++[2],A$A[3][3]
||	LDW	*B2++[2],B$A[3][3]
	LDW	*A2++[2],A$A[3][4]
||	LDW	*B2++[2],B$A[3][4]
||	BNOP	_KeccakF1600_int

	ADDKPC	ret?,RA
||	LDW	*A2++[2],A$A[4][0]
||	LDW	*B2++[2],B$A[4][0]
	LDW	*A2++[2],A$A[4][1]
||	LDW	*B2++[2],B$A[4][1]
	LDW	*A2++[2],A$A[4][2]
||	LDW	*B2++[2],B$A[4][2]
	LDW	*A2++[2],A$A[4][3]
||	LDW	*B2++[2],B$A[4][3]
	LDW	*A2,A$A[4][4]
||	LDW	*B2,B$A[4][4]
||	ADDK	-192,A2				; rewind
||	ADDK	-192,B2

	.align	16
ret?:
	STW	A$A[0][0],*A2++[2]		; store A[5][5]
||	STW	B$A[0][0],*B2++[2]
	STW	A$A[0][1],*A2++[2]
||	STW	B$A[0][1],*B2++[2]
	STW	A$A[0][2],*A2++[2]
||	STW	B$A[0][2],*B2++[2]
	STW	A$A[0][3],*A2++[2]
||	STW	B$A[0][3],*B2++[2]
	STW	A$A[0][4],*A2++[2]
||	STW	B$A[0][4],*B2++[2]

	STW	A$A[1][0],*A2++[2]
||	STW	B$A[1][0],*B2++[2]
	STW	A$A[1][1],*A2++[2]
||	STW	B$A[1][1],*B2++[2]
	STW	A$A[1][2],*A2++[2]
||	STW	B$A[1][2],*B2++[2]
	STW	A$A[1][3],*A2++[2]
||	STW	B$A[1][3],*B2++[2]
	STW	A$A[1][4],*A2++[2]
||	STW	B$A[1][4],*B2++[2]

	STW	A$A[2][0],*A2++[2]
||	STW	B$A[2][0],*B2++[2]
	STW	A$A[2][1],*A2++[2]
||	STW	B$A[2][1],*B2++[2]
	STW	A$A[2][2],*A2++[2]
||	STW	B$A[2][2],*B2++[2]
	STW	A$A[2][3],*A2++[2]
||	STW	B$A[2][3],*B2++[2]
	STW	A$A[2][4],*A2++[2]
||	STW	B$A[2][4],*B2++[2]

	STW	A$A[3][0],*A2++[2]
||	STW	B$A[3][0],*B2++[2]
	STW	A$A[3][1],*A2++[2]
||	STW	B$A[3][1],*B2++[2]
	STW	A$A[3][2],*A2++[2]
||	STW	B$A[3][2],*B2++[2]
	STW	A$A[3][3],*A2++[2]
||	STW	B$A[3][3],*B2++[2]
	STW	A$A[3][4],*A2++[2]
||	STW	B$A[3][4],*B2++[2]

	LDW	*SP[15],RA
||	LDW	*FP[-6],A14

	STW	A$A[4][0],*A2++[2]
||	STW	B$A[4][0],*B2++[2]
	STW	A$A[4][1],*A2++[2]
||	STW	B$A[4][1],*B2++[2]
	STW	A$A[4][2],*A2++[2]
||	STW	B$A[4][2],*B2++[2]
	STW	A$A[4][3],*A2++[2]
||	STW	B$A[4][3],*B2++[2]
	STW	A$A[4][4],*A2
||	STW	B$A[4][4],*B2
||	ADDK	-192,A2				; rewind

	MV	A2,A4				; return original A4
||	LDDW	*SP[8], B11:B10
||	LDDW	*FP[-5],A11:A10
	LDDW	*SP[9], B13:B12
||	LDDW	*FP[-4],A13:A12
||	BNOP	RA
	LDW	*++SP(80),FP			; restore frame pointer
	NOP	4				; wait till FP is committed
	.endasmfunc

	.newblock
	.asg	B2,BSZ
	.asg	A2,INP
	.asg	A3,LEN
	.global	_SHA3_absorb
	.align	32
_SHA3_absorb:
	.asmfunc stack_usage(80)
	STW	FP,*SP--(80)			; save frame pointer
||	MV	SP,FP
	STDW	B13:B12,*SP[9]
||	STDW	A13:A12,*FP[-4]
	STDW	B11:B10,*SP[8]
||	STDW	A11:A10,*FP[-5]
	STW	RA, *SP[15]
||	STW	A14,*FP[-6]

	STW	A4,*SP[1]			; save A[][]
||	MV	B4,INP				; reassign arguments
||	MV	A6,LEN
||	MV	B6,BSZ
||	ADD	4,A4,B4

	LDW	*A4++[2],A$A[0][0]		; load A[5][5]
||	LDW	*B4++[2],B$A[0][0]
	LDW	*A4++[2],A$A[0][1]
||	LDW	*B4++[2],B$A[0][1]
	LDW	*A4++[2],A$A[0][2]
||	LDW	*B4++[2],B$A[0][2]
	LDW	*A4++[2],A$A[0][3]
||	LDW	*B4++[2],B$A[0][3]
	LDW	*A4++[2],A$A[0][4]
||	LDW	*B4++[2],B$A[0][4]

	LDW	*A4++[2],A$A[1][0]
||	LDW	*B4++[2],B$A[1][0]
	LDW	*A4++[2],A$A[1][1]
||	LDW	*B4++[2],B$A[1][1]
	LDW	*A4++[2],A$A[1][2]
||	LDW	*B4++[2],B$A[1][2]
	LDW	*A4++[2],A$A[1][3]
||	LDW	*B4++[2],B$A[1][3]
	LDW	*A4++[2],A$A[1][4]
||	LDW	*B4++[2],B$A[1][4]

	LDW	*A4++[2],A$A[2][0]
||	LDW	*B4++[2],B$A[2][0]
	LDW	*A4++[2],A$A[2][1]
||	LDW	*B4++[2],B$A[2][1]
	LDW	*A4++[2],A$A[2][2]
||	LDW	*B4++[2],B$A[2][2]
	LDW	*A4++[2],A$A[2][3]
||	LDW	*B4++[2],B$A[2][3]
	LDW	*A4++[2],A$A[2][4]
||	LDW	*B4++[2],B$A[2][4]

	LDW	*A4++[2],A$A[3][0]
||	LDW	*B4++[2],B$A[3][0]
	LDW	*A4++[2],A$A[3][1]
||	LDW	*B4++[2],B$A[3][1]
	LDW	*A4++[2],A$A[3][2]
||	LDW	*B4++[2],B$A[3][2]
	LDW	*A4++[2],A$A[3][3]
||	LDW	*B4++[2],B$A[3][3]
	LDW	*A4++[2],A$A[3][4]
||	LDW	*B4++[2],B$A[3][4]

	LDW	*A4++[2],A$A[4][0]
||	LDW	*B4++[2],B$A[4][0]
	LDW	*A4++[2],A$A[4][1]
||	LDW	*B4++[2],B$A[4][1]
	LDW	*A4++[2],A$A[4][2]
||	LDW	*B4++[2],B$A[4][2]
	LDW	*A4++[2],A$A[4][3]
||	LDW	*B4++[2],B$A[4][3]
	LDW	*A4,A$A[4][4]
||	LDW	*B4,B$A[4][4]
||	ADDKPC	loop?,RA
	STDW	RA:BSZ,*SP[4]

loop?:
	CMPLTU	LEN,BSZ,A0			; len < bsz?
||	SHRU	BSZ,3,BSZ
  [A0]	BNOP	ret?
||[A0]	ZERO	BSZ
||[A0]	LDW	*SP[1],A2			; pull A[][]
  [BSZ]	LDNDW	*INP++,A1:A0
||[BSZ]	SUB	LEN,8,LEN
||[BSZ]	SUB	BSZ,1,BSZ
	NOP	4
___
for ($y = 0; $y < 5; $y++) {
    for ($x = 0; $x < ($y<4 ? 5 : 4); $x++) {
$code.=<<___;
	.if	.BIG_ENDIAN
	SWAP2	A0,A1
||	SWAP2	A1,A0
	SWAP4	A0,A0
	SWAP4	A1,A1
||[!BSZ]BNOP	_KeccakF1600_cheat
||[!BSZ]STDW	LEN:INP,*SP[3]
||	DEAL	A0,A0
	.else
  [!BSZ]BNOP	_KeccakF1600_cheat
||[!BSZ]STDW	LEN:INP,*SP[3]
||	DEAL	A0,A0
	.endif
  [BSZ]	LDNDW	*INP++,A1:A0
||	DEAL	A1,A1
  [BSZ]	SUB	LEN,8,LEN
||[BSZ]	SUB	BSZ,1,BSZ
	PACK2	A1,A0,A0
||	PACKH2	A1,A0,A1
	XOR	A0,A$A[$y][$x],A$A[$y][$x]
	XOR	A1,B$A[$y][$x],B$A[$y][$x]
___
    }
}
$code.=<<___;
	.if	.BIG_ENDIAN
	SWAP2	A0,A1
||	SWAP2	A1,A0
	SWAP4	A0,A0
	SWAP4	A1,A1
	.endif
	BNOP	_KeccakF1600_cheat
||	STDW	LEN:INP,*SP[3]
||	DEAL	A0,A0
	DEAL	A1,A1
	NOP
	PACK2	A1,A0,A0
||	PACKH2	A1,A0,A1
	XOR	A0,A$A[4][4],A$A[4][4]
	XOR	A1,B$A[4][4],B$A[4][4]

	.align	16
ret?:
	MV	LEN,A4				; return value
||	ADD	4,A2,B2

	STW	A$A[0][0],*A2++[2]		; store A[5][5]
||	STW	B$A[0][0],*B2++[2]
	STW	A$A[0][1],*A2++[2]
||	STW	B$A[0][1],*B2++[2]
	STW	A$A[0][2],*A2++[2]
||	STW	B$A[0][2],*B2++[2]
	STW	A$A[0][3],*A2++[2]
||	STW	B$A[0][3],*B2++[2]
	STW	A$A[0][4],*A2++[2]
||	STW	B$A[0][4],*B2++[2]

	STW	A$A[1][0],*A2++[2]
||	STW	B$A[1][0],*B2++[2]
	STW	A$A[1][1],*A2++[2]
||	STW	B$A[1][1],*B2++[2]
	STW	A$A[1][2],*A2++[2]
||	STW	B$A[1][2],*B2++[2]
	STW	A$A[1][3],*A2++[2]
||	STW	B$A[1][3],*B2++[2]
	STW	A$A[1][4],*A2++[2]
||	STW	B$A[1][4],*B2++[2]

	STW	A$A[2][0],*A2++[2]
||	STW	B$A[2][0],*B2++[2]
	STW	A$A[2][1],*A2++[2]
||	STW	B$A[2][1],*B2++[2]
	STW	A$A[2][2],*A2++[2]
||	STW	B$A[2][2],*B2++[2]
	STW	A$A[2][3],*A2++[2]
||	STW	B$A[2][3],*B2++[2]
	STW	A$A[2][4],*A2++[2]
||	STW	B$A[2][4],*B2++[2]

	LDW	*SP[15],RA
||	LDW	*FP[-6],A14

	STW	A$A[3][0],*A2++[2]
||	STW	B$A[3][0],*B2++[2]
	STW	A$A[3][1],*A2++[2]
||	STW	B$A[3][1],*B2++[2]
	STW	A$A[3][2],*A2++[2]
||	STW	B$A[3][2],*B2++[2]
	STW	A$A[3][3],*A2++[2]
||	STW	B$A[3][3],*B2++[2]
	STW	A$A[3][4],*A2++[2]
||	STW	B$A[3][4],*B2++[2]

	LDDW	*SP[8], B11:B10
||	LDDW	*FP[-5],A11:A10
	LDDW	*SP[9], B13:B12
||	LDDW	*FP[-4],A13:A12
	BNOP	RA
||	LDW	*++SP(80),FP			; restore frame pointer

	STW	A$A[4][0],*A2++[2]
||	STW	B$A[4][0],*B2++[2]
	STW	A$A[4][1],*A2++[2]
||	STW	B$A[4][1],*B2++[2]
	STW	A$A[4][2],*A2++[2]
||	STW	B$A[4][2],*B2++[2]
	STW	A$A[4][3],*A2++[2]
||	STW	B$A[4][3],*B2++[2]
	STW	A$A[4][4],*A2++[2]
||	STW	B$A[4][4],*B2++[2]
	.endasmfunc

	.newblock
	.global	_SHA3_squeeze
	.asg	A12,OUT
	.asg	A13,LEN
	.asg	A14,BSZ
	.align	32
_SHA3_squeeze:
	.asmfunc stack_usage(24)
	STW	FP,*SP--(24)			; save frame pointer
||	MV	SP,FP
	STW	RA, *SP[5]
||	STW	A14,*FP[-2]
	STDW	A13:A12,*FP[-2]
||	MV	B4,OUT				; reassign arguments
	MV	A6,LEN
||	MV	B6,BSZ

loop?:
	LDW	*SP[5],RA			; reload RA
||	SHRU	BSZ,3,A1
||	MV	A4,A8
||	ADD	4,A4,B8
block?:
	CMPLTU	LEN,8,A0			; len < 8?
  [A0]	BNOP	tail?
	LDW	*A8++[2],A9
||	LDW	*B8++[2],B9
||	SUB	LEN,8,LEN			; len -= 8
	MV	LEN,A0
||	SUB	A1,1,A1				; bsz--
||	NOP	4
	.if	.BIG_ENDIAN
	SWAP4	A9,A9
||	SWAP4	B9,B9
	SWAP2	A9,A9
||	SWAP2	B9,B9
	.endif
  [!A0]	BNOP	ret?
||[!A0]	ZERO	A1
	PACK2	B9,A9,B7
||[A1]	BNOP	block?
	PACKH2	B9,A9,B9
||	SHFL	B7,B7
	SHFL	B9,B9
	STNW	B7,*OUT++
	STNW	B9,*OUT++
	NOP

	BNOP	_KeccakF1600,4
	ADDKPC	loop?,RA

	.align	16
tail?:
	.if	.BIG_ENDIAN
	SWAP4	A9,A9
||	SWAP4	B9,B9
	SWAP2	A9,A9
||	SWAP2	B9,B9
	.endif
	PACK2	B9,A9,B7
	PACKH2	B9,A9,B9
||	SHFL	B7,B7
	SHFL	B9,B9

	STB	B7,*OUT++
||	SHRU	B7,8,B7
||	ADD	LEN,7,A0
  [A0]	STB	B7,*OUT++
||[A0]	SHRU	B7,8,B7
||[A0]	SUB	A0,1,A0
  [A0]	STB	B7,*OUT++
||[A0]	SHRU	B7,8,B7
||[A0]	SUB	A0,1,A0
  [A0]	STB	B7,*OUT++
||[A0]	SUB	A0,1,A0
  [A0]	STB	B9,*OUT++
||[A0]	SHRU	B9,8,B9
||[A0]	SUB	A0,1,A0
  [A0]	STB	B9,*OUT++
||[A0]	SHRU	B9,8,B9
||[A0]	SUB	A0,1,A0
  [A0]	STB	B9,*OUT++

ret?:
	LDDW	*FP[-2],A13:A12
	BNOP	RA
||	LDW	*FP[-2],A14
	LDW	*++SP(24),FP			; restore frame pointer
	NOP	4				; wait till FP is committed
	.endasmfunc

	.if	__TI_EABI__
	.sect	".text:sha_asm.const"
	.else
	.sect	".const:sha_asm"
	.endif
	.align	256
	.uword	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
iotas:
	.uword	0x00000001, 0x00000000
	.uword	0x00000000, 0x00000089
	.uword	0x00000000, 0x8000008b
	.uword	0x00000000, 0x80008080
	.uword	0x00000001, 0x0000008b
	.uword	0x00000001, 0x00008000
	.uword	0x00000001, 0x80008088
	.uword	0x00000001, 0x80000082
	.uword	0x00000000, 0x0000000b
	.uword	0x00000000, 0x0000000a
	.uword	0x00000001, 0x00008082
	.uword	0x00000000, 0x00008003
	.uword	0x00000001, 0x0000808b
	.uword	0x00000001, 0x8000000b
	.uword	0x00000001, 0x8000008a
	.uword	0x00000001, 0x80000081
	.uword	0x00000000, 0x80000081
	.uword	0x00000000, 0x80000008
	.uword	0x00000000, 0x00000083
	.uword	0x00000000, 0x80008003
	.uword	0x00000001, 0x80008088
	.uword	0x00000000, 0x80000088
	.uword	0x00000001, 0x00008000
	.uword	0x00000000, 0x80008082

	.cstring "Keccak-1600 absorb and squeeze for C64x, CRYPTOGAMS by <appro\@openssl.org>"
	.align	4
___

$output=pop;
open STDOUT,">$output";
print $code;
close STDOUT;

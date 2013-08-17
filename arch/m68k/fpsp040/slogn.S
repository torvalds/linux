|
|	slogn.sa 3.1 12/10/90
|
|	slogn computes the natural logarithm of an
|	input value. slognd does the same except the input value is a
|	denormalized number. slognp1 computes log(1+X), and slognp1d
|	computes log(1+X) for denormalized X.
|
|	Input: Double-extended value in memory location pointed to by address
|		register a0.
|
|	Output:	log(X) or log(1+X) returned in floating-point register Fp0.
|
|	Accuracy and Monotonicity: The returned result is within 2 ulps in
|		64 significant bit, i.e. within 0.5001 ulp to 53 bits if the
|		result is subsequently rounded to double precision. The
|		result is provably monotonic in double precision.
|
|	Speed: The program slogn takes approximately 190 cycles for input
|		argument X such that |X-1| >= 1/16, which is the usual
|		situation. For those arguments, slognp1 takes approximately
|		 210 cycles. For the less common arguments, the program will
|		 run no worse than 10% slower.
|
|	Algorithm:
|	LOGN:
|	Step 1. If |X-1| < 1/16, approximate log(X) by an odd polynomial in
|		u, where u = 2(X-1)/(X+1). Otherwise, move on to Step 2.
|
|	Step 2. X = 2**k * Y where 1 <= Y < 2. Define F to be the first seven
|		significant bits of Y plus 2**(-7), i.e. F = 1.xxxxxx1 in base
|		2 where the six "x" match those of Y. Note that |Y-F| <= 2**(-7).
|
|	Step 3. Define u = (Y-F)/F. Approximate log(1+u) by a polynomial in u,
|		log(1+u) = poly.
|
|	Step 4. Reconstruct log(X) = log( 2**k * Y ) = k*log(2) + log(F) + log(1+u)
|		by k*log(2) + (log(F) + poly). The values of log(F) are calculated
|		beforehand and stored in the program.
|
|	lognp1:
|	Step 1: If |X| < 1/16, approximate log(1+X) by an odd polynomial in
|		u where u = 2X/(2+X). Otherwise, move on to Step 2.
|
|	Step 2: Let 1+X = 2**k * Y, where 1 <= Y < 2. Define F as done in Step 2
|		of the algorithm for LOGN and compute log(1+X) as
|		k*log(2) + log(F) + poly where poly approximates log(1+u),
|		u = (Y-F)/F.
|
|	Implementation Notes:
|	Note 1. There are 64 different possible values for F, thus 64 log(F)'s
|		need to be tabulated. Moreover, the values of 1/F are also
|		tabulated so that the division in (Y-F)/F can be performed by a
|		multiplication.
|
|	Note 2. In Step 2 of lognp1, in order to preserved accuracy, the value
|		Y-F has to be calculated carefully when 1/2 <= X < 3/2.
|
|	Note 3. To fully exploit the pipeline, polynomials are usually separated
|		into two parts evaluated independently before being added up.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|slogn	idnt	2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

BOUNDS1:  .long 0x3FFEF07D,0x3FFF8841
BOUNDS2:  .long 0x3FFE8000,0x3FFFC000

LOGOF2:	.long 0x3FFE0000,0xB17217F7,0xD1CF79AC,0x00000000

one:	.long 0x3F800000
zero:	.long 0x00000000
infty:	.long 0x7F800000
negone:	.long 0xBF800000

LOGA6:	.long 0x3FC2499A,0xB5E4040B
LOGA5:	.long 0xBFC555B5,0x848CB7DB

LOGA4:	.long 0x3FC99999,0x987D8730
LOGA3:	.long 0xBFCFFFFF,0xFF6F7E97

LOGA2:	.long 0x3FD55555,0x555555a4
LOGA1:	.long 0xBFE00000,0x00000008

LOGB5:	.long 0x3F175496,0xADD7DAD6
LOGB4:	.long 0x3F3C71C2,0xFE80C7E0

LOGB3:	.long 0x3F624924,0x928BCCFF
LOGB2:	.long 0x3F899999,0x999995EC

LOGB1:	.long 0x3FB55555,0x55555555
TWO:	.long 0x40000000,0x00000000

LTHOLD:	.long 0x3f990000,0x80000000,0x00000000,0x00000000

LOGTBL:
	.long  0x3FFE0000,0xFE03F80F,0xE03F80FE,0x00000000
	.long  0x3FF70000,0xFF015358,0x833C47E2,0x00000000
	.long  0x3FFE0000,0xFA232CF2,0x52138AC0,0x00000000
	.long  0x3FF90000,0xBDC8D83E,0xAD88D549,0x00000000
	.long  0x3FFE0000,0xF6603D98,0x0F6603DA,0x00000000
	.long  0x3FFA0000,0x9CF43DCF,0xF5EAFD48,0x00000000
	.long  0x3FFE0000,0xF2B9D648,0x0F2B9D65,0x00000000
	.long  0x3FFA0000,0xDA16EB88,0xCB8DF614,0x00000000
	.long  0x3FFE0000,0xEF2EB71F,0xC4345238,0x00000000
	.long  0x3FFB0000,0x8B29B775,0x1BD70743,0x00000000
	.long  0x3FFE0000,0xEBBDB2A5,0xC1619C8C,0x00000000
	.long  0x3FFB0000,0xA8D839F8,0x30C1FB49,0x00000000
	.long  0x3FFE0000,0xE865AC7B,0x7603A197,0x00000000
	.long  0x3FFB0000,0xC61A2EB1,0x8CD907AD,0x00000000
	.long  0x3FFE0000,0xE525982A,0xF70C880E,0x00000000
	.long  0x3FFB0000,0xE2F2A47A,0xDE3A18AF,0x00000000
	.long  0x3FFE0000,0xE1FC780E,0x1FC780E2,0x00000000
	.long  0x3FFB0000,0xFF64898E,0xDF55D551,0x00000000
	.long  0x3FFE0000,0xDEE95C4C,0xA037BA57,0x00000000
	.long  0x3FFC0000,0x8DB956A9,0x7B3D0148,0x00000000
	.long  0x3FFE0000,0xDBEB61EE,0xD19C5958,0x00000000
	.long  0x3FFC0000,0x9B8FE100,0xF47BA1DE,0x00000000
	.long  0x3FFE0000,0xD901B203,0x6406C80E,0x00000000
	.long  0x3FFC0000,0xA9372F1D,0x0DA1BD17,0x00000000
	.long  0x3FFE0000,0xD62B80D6,0x2B80D62C,0x00000000
	.long  0x3FFC0000,0xB6B07F38,0xCE90E46B,0x00000000
	.long  0x3FFE0000,0xD3680D36,0x80D3680D,0x00000000
	.long  0x3FFC0000,0xC3FD0329,0x06488481,0x00000000
	.long  0x3FFE0000,0xD0B69FCB,0xD2580D0B,0x00000000
	.long  0x3FFC0000,0xD11DE0FF,0x15AB18CA,0x00000000
	.long  0x3FFE0000,0xCE168A77,0x25080CE1,0x00000000
	.long  0x3FFC0000,0xDE1433A1,0x6C66B150,0x00000000
	.long  0x3FFE0000,0xCB8727C0,0x65C393E0,0x00000000
	.long  0x3FFC0000,0xEAE10B5A,0x7DDC8ADD,0x00000000
	.long  0x3FFE0000,0xC907DA4E,0x871146AD,0x00000000
	.long  0x3FFC0000,0xF7856E5E,0xE2C9B291,0x00000000
	.long  0x3FFE0000,0xC6980C69,0x80C6980C,0x00000000
	.long  0x3FFD0000,0x82012CA5,0xA68206D7,0x00000000
	.long  0x3FFE0000,0xC4372F85,0x5D824CA6,0x00000000
	.long  0x3FFD0000,0x882C5FCD,0x7256A8C5,0x00000000
	.long  0x3FFE0000,0xC1E4BBD5,0x95F6E947,0x00000000
	.long  0x3FFD0000,0x8E44C60B,0x4CCFD7DE,0x00000000
	.long  0x3FFE0000,0xBFA02FE8,0x0BFA02FF,0x00000000
	.long  0x3FFD0000,0x944AD09E,0xF4351AF6,0x00000000
	.long  0x3FFE0000,0xBD691047,0x07661AA3,0x00000000
	.long  0x3FFD0000,0x9A3EECD4,0xC3EAA6B2,0x00000000
	.long  0x3FFE0000,0xBB3EE721,0xA54D880C,0x00000000
	.long  0x3FFD0000,0xA0218434,0x353F1DE8,0x00000000
	.long  0x3FFE0000,0xB92143FA,0x36F5E02E,0x00000000
	.long  0x3FFD0000,0xA5F2FCAB,0xBBC506DA,0x00000000
	.long  0x3FFE0000,0xB70FBB5A,0x19BE3659,0x00000000
	.long  0x3FFD0000,0xABB3B8BA,0x2AD362A5,0x00000000
	.long  0x3FFE0000,0xB509E68A,0x9B94821F,0x00000000
	.long  0x3FFD0000,0xB1641795,0xCE3CA97B,0x00000000
	.long  0x3FFE0000,0xB30F6352,0x8917C80B,0x00000000
	.long  0x3FFD0000,0xB7047551,0x5D0F1C61,0x00000000
	.long  0x3FFE0000,0xB11FD3B8,0x0B11FD3C,0x00000000
	.long  0x3FFD0000,0xBC952AFE,0xEA3D13E1,0x00000000
	.long  0x3FFE0000,0xAF3ADDC6,0x80AF3ADE,0x00000000
	.long  0x3FFD0000,0xC2168ED0,0xF458BA4A,0x00000000
	.long  0x3FFE0000,0xAD602B58,0x0AD602B6,0x00000000
	.long  0x3FFD0000,0xC788F439,0xB3163BF1,0x00000000
	.long  0x3FFE0000,0xAB8F69E2,0x8359CD11,0x00000000
	.long  0x3FFD0000,0xCCECAC08,0xBF04565D,0x00000000
	.long  0x3FFE0000,0xA9C84A47,0xA07F5638,0x00000000
	.long  0x3FFD0000,0xD2420487,0x2DD85160,0x00000000
	.long  0x3FFE0000,0xA80A80A8,0x0A80A80B,0x00000000
	.long  0x3FFD0000,0xD7894992,0x3BC3588A,0x00000000
	.long  0x3FFE0000,0xA655C439,0x2D7B73A8,0x00000000
	.long  0x3FFD0000,0xDCC2C4B4,0x9887DACC,0x00000000
	.long  0x3FFE0000,0xA4A9CF1D,0x96833751,0x00000000
	.long  0x3FFD0000,0xE1EEBD3E,0x6D6A6B9E,0x00000000
	.long  0x3FFE0000,0xA3065E3F,0xAE7CD0E0,0x00000000
	.long  0x3FFD0000,0xE70D785C,0x2F9F5BDC,0x00000000
	.long  0x3FFE0000,0xA16B312E,0xA8FC377D,0x00000000
	.long  0x3FFD0000,0xEC1F392C,0x5179F283,0x00000000
	.long  0x3FFE0000,0x9FD809FD,0x809FD80A,0x00000000
	.long  0x3FFD0000,0xF12440D3,0xE36130E6,0x00000000
	.long  0x3FFE0000,0x9E4CAD23,0xDD5F3A20,0x00000000
	.long  0x3FFD0000,0xF61CCE92,0x346600BB,0x00000000
	.long  0x3FFE0000,0x9CC8E160,0xC3FB19B9,0x00000000
	.long  0x3FFD0000,0xFB091FD3,0x8145630A,0x00000000
	.long  0x3FFE0000,0x9B4C6F9E,0xF03A3CAA,0x00000000
	.long  0x3FFD0000,0xFFE97042,0xBFA4C2AD,0x00000000
	.long  0x3FFE0000,0x99D722DA,0xBDE58F06,0x00000000
	.long  0x3FFE0000,0x825EFCED,0x49369330,0x00000000
	.long  0x3FFE0000,0x9868C809,0x868C8098,0x00000000
	.long  0x3FFE0000,0x84C37A7A,0xB9A905C9,0x00000000
	.long  0x3FFE0000,0x97012E02,0x5C04B809,0x00000000
	.long  0x3FFE0000,0x87224C2E,0x8E645FB7,0x00000000
	.long  0x3FFE0000,0x95A02568,0x095A0257,0x00000000
	.long  0x3FFE0000,0x897B8CAC,0x9F7DE298,0x00000000
	.long  0x3FFE0000,0x94458094,0x45809446,0x00000000
	.long  0x3FFE0000,0x8BCF55DE,0xC4CD05FE,0x00000000
	.long  0x3FFE0000,0x92F11384,0x0497889C,0x00000000
	.long  0x3FFE0000,0x8E1DC0FB,0x89E125E5,0x00000000
	.long  0x3FFE0000,0x91A2B3C4,0xD5E6F809,0x00000000
	.long  0x3FFE0000,0x9066E68C,0x955B6C9B,0x00000000
	.long  0x3FFE0000,0x905A3863,0x3E06C43B,0x00000000
	.long  0x3FFE0000,0x92AADE74,0xC7BE59E0,0x00000000
	.long  0x3FFE0000,0x8F1779D9,0xFDC3A219,0x00000000
	.long  0x3FFE0000,0x94E9BFF6,0x15845643,0x00000000
	.long  0x3FFE0000,0x8DDA5202,0x37694809,0x00000000
	.long  0x3FFE0000,0x9723A1B7,0x20134203,0x00000000
	.long  0x3FFE0000,0x8CA29C04,0x6514E023,0x00000000
	.long  0x3FFE0000,0x995899C8,0x90EB8990,0x00000000
	.long  0x3FFE0000,0x8B70344A,0x139BC75A,0x00000000
	.long  0x3FFE0000,0x9B88BDAA,0x3A3DAE2F,0x00000000
	.long  0x3FFE0000,0x8A42F870,0x5669DB46,0x00000000
	.long  0x3FFE0000,0x9DB4224F,0xFFE1157C,0x00000000
	.long  0x3FFE0000,0x891AC73A,0xE9819B50,0x00000000
	.long  0x3FFE0000,0x9FDADC26,0x8B7A12DA,0x00000000
	.long  0x3FFE0000,0x87F78087,0xF78087F8,0x00000000
	.long  0x3FFE0000,0xA1FCFF17,0xCE733BD4,0x00000000
	.long  0x3FFE0000,0x86D90544,0x7A34ACC6,0x00000000
	.long  0x3FFE0000,0xA41A9E8F,0x5446FB9F,0x00000000
	.long  0x3FFE0000,0x85BF3761,0x2CEE3C9B,0x00000000
	.long  0x3FFE0000,0xA633CD7E,0x6771CD8B,0x00000000
	.long  0x3FFE0000,0x84A9F9C8,0x084A9F9D,0x00000000
	.long  0x3FFE0000,0xA8489E60,0x0B435A5E,0x00000000
	.long  0x3FFE0000,0x83993052,0x3FBE3368,0x00000000
	.long  0x3FFE0000,0xAA59233C,0xCCA4BD49,0x00000000
	.long  0x3FFE0000,0x828CBFBE,0xB9A020A3,0x00000000
	.long  0x3FFE0000,0xAC656DAE,0x6BCC4985,0x00000000
	.long  0x3FFE0000,0x81848DA8,0xFAF0D277,0x00000000
	.long  0x3FFE0000,0xAE6D8EE3,0x60BB2468,0x00000000
	.long  0x3FFE0000,0x80808080,0x80808081,0x00000000
	.long  0x3FFE0000,0xB07197A2,0x3C46C654,0x00000000

	.set	ADJK,L_SCR1

	.set	X,FP_SCR1
	.set	XDCARE,X+2
	.set	XFRAC,X+4

	.set	F,FP_SCR2
	.set	FFRAC,F+4

	.set	KLOG2,FP_SCR3

	.set	SAVEU,FP_SCR4

	| xref	t_frcinx
	|xref	t_extdnrm
	|xref	t_operr
	|xref	t_dz

	.global	slognd
slognd:
|--ENTRY POINT FOR LOG(X) FOR DENORMALIZED INPUT

	movel		#-100,ADJK(%a6)	| ...INPUT = 2^(ADJK) * FP0

|----normalize the input value by left shifting k bits (k to be determined
|----below), adjusting exponent and storing -k to  ADJK
|----the value TWOTO100 is no longer needed.
|----Note that this code assumes the denormalized input is NON-ZERO.

     moveml	%d2-%d7,-(%a7)		| ...save some registers
     movel	#0x00000000,%d3		| ...D3 is exponent of smallest norm. #
     movel	4(%a0),%d4
     movel	8(%a0),%d5		| ...(D4,D5) is (Hi_X,Lo_X)
     clrl	%d2			| ...D2 used for holding K

     tstl	%d4
     bnes	HiX_not0

HiX_0:
     movel	%d5,%d4
     clrl	%d5
     movel	#32,%d2
     clrl	%d6
     bfffo      %d4{#0:#32},%d6
     lsll      %d6,%d4
     addl	%d6,%d2			| ...(D3,D4,D5) is normalized

     movel	%d3,X(%a6)
     movel	%d4,XFRAC(%a6)
     movel	%d5,XFRAC+4(%a6)
     negl	%d2
     movel	%d2,ADJK(%a6)
     fmovex	X(%a6),%fp0
     moveml	(%a7)+,%d2-%d7		| ...restore registers
     lea	X(%a6),%a0
     bras	LOGBGN			| ...begin regular log(X)


HiX_not0:
     clrl	%d6
     bfffo	%d4{#0:#32},%d6		| ...find first 1
     movel	%d6,%d2			| ...get k
     lsll	%d6,%d4
     movel	%d5,%d7			| ...a copy of D5
     lsll	%d6,%d5
     negl	%d6
     addil	#32,%d6
     lsrl	%d6,%d7
     orl	%d7,%d4			| ...(D3,D4,D5) normalized

     movel	%d3,X(%a6)
     movel	%d4,XFRAC(%a6)
     movel	%d5,XFRAC+4(%a6)
     negl	%d2
     movel	%d2,ADJK(%a6)
     fmovex	X(%a6),%fp0
     moveml	(%a7)+,%d2-%d7		| ...restore registers
     lea	X(%a6),%a0
     bras	LOGBGN			| ...begin regular log(X)


	.global	slogn
slogn:
|--ENTRY POINT FOR LOG(X) FOR X FINITE, NON-ZERO, NOT NAN'S

	fmovex		(%a0),%fp0	| ...LOAD INPUT
	movel		#0x00000000,ADJK(%a6)

LOGBGN:
|--FPCR SAVED AND CLEARED, INPUT IS 2^(ADJK)*FP0, FP0 CONTAINS
|--A FINITE, NON-ZERO, NORMALIZED NUMBER.

	movel	(%a0),%d0
	movew	4(%a0),%d0

	movel	(%a0),X(%a6)
	movel	4(%a0),X+4(%a6)
	movel	8(%a0),X+8(%a6)

	cmpil	#0,%d0		| ...CHECK IF X IS NEGATIVE
	blt	LOGNEG		| ...LOG OF NEGATIVE ARGUMENT IS INVALID
	cmp2l	BOUNDS1,%d0	| ...X IS POSITIVE, CHECK IF X IS NEAR 1
	bcc	LOGNEAR1	| ...BOUNDS IS ROUGHLY [15/16, 17/16]

LOGMAIN:
|--THIS SHOULD BE THE USUAL CASE, X NOT VERY CLOSE TO 1

|--X = 2^(K) * Y, 1 <= Y < 2. THUS, Y = 1.XXXXXXXX....XX IN BINARY.
|--WE DEFINE F = 1.XXXXXX1, I.E. FIRST 7 BITS OF Y AND ATTACH A 1.
|--THE IDEA IS THAT LOG(X) = K*LOG2 + LOG(Y)
|--			 = K*LOG2 + LOG(F) + LOG(1 + (Y-F)/F).
|--NOTE THAT U = (Y-F)/F IS VERY SMALL AND THUS APPROXIMATING
|--LOG(1+U) CAN BE VERY EFFICIENT.
|--ALSO NOTE THAT THE VALUE 1/F IS STORED IN A TABLE SO THAT NO
|--DIVISION IS NEEDED TO CALCULATE (Y-F)/F.

|--GET K, Y, F, AND ADDRESS OF 1/F.
	asrl	#8,%d0
	asrl	#8,%d0		| ...SHIFTED 16 BITS, BIASED EXPO. OF X
	subil	#0x3FFF,%d0	| ...THIS IS K
	addl	ADJK(%a6),%d0	| ...ADJUST K, ORIGINAL INPUT MAY BE  DENORM.
	lea	LOGTBL,%a0	| ...BASE ADDRESS OF 1/F AND LOG(F)
	fmovel	%d0,%fp1		| ...CONVERT K TO FLOATING-POINT FORMAT

|--WHILE THE CONVERSION IS GOING ON, WE GET F AND ADDRESS OF 1/F
	movel	#0x3FFF0000,X(%a6)	| ...X IS NOW Y, I.E. 2^(-K)*X
	movel	XFRAC(%a6),FFRAC(%a6)
	andil	#0xFE000000,FFRAC(%a6) | ...FIRST 7 BITS OF Y
	oril	#0x01000000,FFRAC(%a6) | ...GET F: ATTACH A 1 AT THE EIGHTH BIT
	movel	FFRAC(%a6),%d0	| ...READY TO GET ADDRESS OF 1/F
	andil	#0x7E000000,%d0
	asrl	#8,%d0
	asrl	#8,%d0
	asrl	#4,%d0		| ...SHIFTED 20, D0 IS THE DISPLACEMENT
	addal	%d0,%a0		| ...A0 IS THE ADDRESS FOR 1/F

	fmovex	X(%a6),%fp0
	movel	#0x3fff0000,F(%a6)
	clrl	F+8(%a6)
	fsubx	F(%a6),%fp0		| ...Y-F
	fmovemx %fp2-%fp2/%fp3,-(%sp)	| ...SAVE FP2 WHILE FP0 IS NOT READY
|--SUMMARY: FP0 IS Y-F, A0 IS ADDRESS OF 1/F, FP1 IS K
|--REGISTERS SAVED: FPCR, FP1, FP2

LP1CONT1:
|--AN RE-ENTRY POINT FOR LOGNP1
	fmulx	(%a0),%fp0	| ...FP0 IS U = (Y-F)/F
	fmulx	LOGOF2,%fp1	| ...GET K*LOG2 WHILE FP0 IS NOT READY
	fmovex	%fp0,%fp2
	fmulx	%fp2,%fp2		| ...FP2 IS V=U*U
	fmovex	%fp1,KLOG2(%a6)	| ...PUT K*LOG2 IN MEMORY, FREE FP1

|--LOG(1+U) IS APPROXIMATED BY
|--U + V*(A1+U*(A2+U*(A3+U*(A4+U*(A5+U*A6))))) WHICH IS
|--[U + V*(A1+V*(A3+V*A5))]  +  [U*V*(A2+V*(A4+V*A6))]

	fmovex	%fp2,%fp3
	fmovex	%fp2,%fp1

	fmuld	LOGA6,%fp1	| ...V*A6
	fmuld	LOGA5,%fp2	| ...V*A5

	faddd	LOGA4,%fp1	| ...A4+V*A6
	faddd	LOGA3,%fp2	| ...A3+V*A5

	fmulx	%fp3,%fp1		| ...V*(A4+V*A6)
	fmulx	%fp3,%fp2		| ...V*(A3+V*A5)

	faddd	LOGA2,%fp1	| ...A2+V*(A4+V*A6)
	faddd	LOGA1,%fp2	| ...A1+V*(A3+V*A5)

	fmulx	%fp3,%fp1		| ...V*(A2+V*(A4+V*A6))
	addal	#16,%a0		| ...ADDRESS OF LOG(F)
	fmulx	%fp3,%fp2		| ...V*(A1+V*(A3+V*A5)), FP3 RELEASED

	fmulx	%fp0,%fp1		| ...U*V*(A2+V*(A4+V*A6))
	faddx	%fp2,%fp0		| ...U+V*(A1+V*(A3+V*A5)), FP2 RELEASED

	faddx	(%a0),%fp1	| ...LOG(F)+U*V*(A2+V*(A4+V*A6))
	fmovemx  (%sp)+,%fp2-%fp2/%fp3	| ...RESTORE FP2
	faddx	%fp1,%fp0		| ...FP0 IS LOG(F) + LOG(1+U)

	fmovel	%d1,%fpcr
	faddx	KLOG2(%a6),%fp0	| ...FINAL ADD
	bra	t_frcinx


LOGNEAR1:
|--REGISTERS SAVED: FPCR, FP1. FP0 CONTAINS THE INPUT.
	fmovex	%fp0,%fp1
	fsubs	one,%fp1		| ...FP1 IS X-1
	fadds	one,%fp0		| ...FP0 IS X+1
	faddx	%fp1,%fp1		| ...FP1 IS 2(X-1)
|--LOG(X) = LOG(1+U/2)-LOG(1-U/2) WHICH IS AN ODD POLYNOMIAL
|--IN U, U = 2(X-1)/(X+1) = FP1/FP0

LP1CONT2:
|--THIS IS AN RE-ENTRY POINT FOR LOGNP1
	fdivx	%fp0,%fp1		| ...FP1 IS U
	fmovemx %fp2-%fp2/%fp3,-(%sp)	 | ...SAVE FP2
|--REGISTERS SAVED ARE NOW FPCR,FP1,FP2,FP3
|--LET V=U*U, W=V*V, CALCULATE
|--U + U*V*(B1 + V*(B2 + V*(B3 + V*(B4 + V*B5)))) BY
|--U + U*V*(  [B1 + W*(B3 + W*B5)]  +  [V*(B2 + W*B4)]  )
	fmovex	%fp1,%fp0
	fmulx	%fp0,%fp0	| ...FP0 IS V
	fmovex	%fp1,SAVEU(%a6) | ...STORE U IN MEMORY, FREE FP1
	fmovex	%fp0,%fp1
	fmulx	%fp1,%fp1	| ...FP1 IS W

	fmoved	LOGB5,%fp3
	fmoved	LOGB4,%fp2

	fmulx	%fp1,%fp3	| ...W*B5
	fmulx	%fp1,%fp2	| ...W*B4

	faddd	LOGB3,%fp3 | ...B3+W*B5
	faddd	LOGB2,%fp2 | ...B2+W*B4

	fmulx	%fp3,%fp1	| ...W*(B3+W*B5), FP3 RELEASED

	fmulx	%fp0,%fp2	| ...V*(B2+W*B4)

	faddd	LOGB1,%fp1 | ...B1+W*(B3+W*B5)
	fmulx	SAVEU(%a6),%fp0 | ...FP0 IS U*V

	faddx	%fp2,%fp1	| ...B1+W*(B3+W*B5) + V*(B2+W*B4), FP2 RELEASED
	fmovemx (%sp)+,%fp2-%fp2/%fp3 | ...FP2 RESTORED

	fmulx	%fp1,%fp0	| ...U*V*( [B1+W*(B3+W*B5)] + [V*(B2+W*B4)] )

	fmovel	%d1,%fpcr
	faddx	SAVEU(%a6),%fp0
	bra	t_frcinx
	rts

LOGNEG:
|--REGISTERS SAVED FPCR. LOG(-VE) IS INVALID
	bra	t_operr

	.global	slognp1d
slognp1d:
|--ENTRY POINT FOR LOG(1+Z) FOR DENORMALIZED INPUT
| Simply return the denorm

	bra	t_extdnrm

	.global	slognp1
slognp1:
|--ENTRY POINT FOR LOG(1+X) FOR X FINITE, NON-ZERO, NOT NAN'S

	fmovex	(%a0),%fp0	| ...LOAD INPUT
	fabsx	%fp0		|test magnitude
	fcmpx	LTHOLD,%fp0	|compare with min threshold
	fbgt	LP1REAL		|if greater, continue
	fmovel	#0,%fpsr		|clr N flag from compare
	fmovel	%d1,%fpcr
	fmovex	(%a0),%fp0	|return signed argument
	bra	t_frcinx

LP1REAL:
	fmovex	(%a0),%fp0	| ...LOAD INPUT
	movel	#0x00000000,ADJK(%a6)
	fmovex	%fp0,%fp1	| ...FP1 IS INPUT Z
	fadds	one,%fp0	| ...X := ROUND(1+Z)
	fmovex	%fp0,X(%a6)
	movew	XFRAC(%a6),XDCARE(%a6)
	movel	X(%a6),%d0
	cmpil	#0,%d0
	ble	LP1NEG0	| ...LOG OF ZERO OR -VE
	cmp2l	BOUNDS2,%d0
	bcs	LOGMAIN	| ...BOUNDS2 IS [1/2,3/2]
|--IF 1+Z > 3/2 OR 1+Z < 1/2, THEN X, WHICH IS ROUNDING 1+Z,
|--CONTAINS AT LEAST 63 BITS OF INFORMATION OF Z. IN THAT CASE,
|--SIMPLY INVOKE LOG(X) FOR LOG(1+Z).

LP1NEAR1:
|--NEXT SEE IF EXP(-1/16) < X < EXP(1/16)
	cmp2l	BOUNDS1,%d0
	bcss	LP1CARE

LP1ONE16:
|--EXP(-1/16) < X < EXP(1/16). LOG(1+Z) = LOG(1+U/2) - LOG(1-U/2)
|--WHERE U = 2Z/(2+Z) = 2Z/(1+X).
	faddx	%fp1,%fp1	| ...FP1 IS 2Z
	fadds	one,%fp0	| ...FP0 IS 1+X
|--U = FP1/FP0
	bra	LP1CONT2

LP1CARE:
|--HERE WE USE THE USUAL TABLE DRIVEN APPROACH. CARE HAS TO BE
|--TAKEN BECAUSE 1+Z CAN HAVE 67 BITS OF INFORMATION AND WE MUST
|--PRESERVE ALL THE INFORMATION. BECAUSE 1+Z IS IN [1/2,3/2],
|--THERE ARE ONLY TWO CASES.
|--CASE 1: 1+Z < 1, THEN K = -1 AND Y-F = (2-F) + 2Z
|--CASE 2: 1+Z > 1, THEN K = 0  AND Y-F = (1-F) + Z
|--ON RETURNING TO LP1CONT1, WE MUST HAVE K IN FP1, ADDRESS OF
|--(1/F) IN A0, Y-F IN FP0, AND FP2 SAVED.

	movel	XFRAC(%a6),FFRAC(%a6)
	andil	#0xFE000000,FFRAC(%a6)
	oril	#0x01000000,FFRAC(%a6)	| ...F OBTAINED
	cmpil	#0x3FFF8000,%d0	| ...SEE IF 1+Z > 1
	bges	KISZERO

KISNEG1:
	fmoves	TWO,%fp0
	movel	#0x3fff0000,F(%a6)
	clrl	F+8(%a6)
	fsubx	F(%a6),%fp0	| ...2-F
	movel	FFRAC(%a6),%d0
	andil	#0x7E000000,%d0
	asrl	#8,%d0
	asrl	#8,%d0
	asrl	#4,%d0		| ...D0 CONTAINS DISPLACEMENT FOR 1/F
	faddx	%fp1,%fp1		| ...GET 2Z
	fmovemx %fp2-%fp2/%fp3,-(%sp)	| ...SAVE FP2
	faddx	%fp1,%fp0		| ...FP0 IS Y-F = (2-F)+2Z
	lea	LOGTBL,%a0	| ...A0 IS ADDRESS OF 1/F
	addal	%d0,%a0
	fmoves	negone,%fp1	| ...FP1 IS K = -1
	bra	LP1CONT1

KISZERO:
	fmoves	one,%fp0
	movel	#0x3fff0000,F(%a6)
	clrl	F+8(%a6)
	fsubx	F(%a6),%fp0		| ...1-F
	movel	FFRAC(%a6),%d0
	andil	#0x7E000000,%d0
	asrl	#8,%d0
	asrl	#8,%d0
	asrl	#4,%d0
	faddx	%fp1,%fp0		| ...FP0 IS Y-F
	fmovemx %fp2-%fp2/%fp3,-(%sp)	| ...FP2 SAVED
	lea	LOGTBL,%a0
	addal	%d0,%a0		| ...A0 IS ADDRESS OF 1/F
	fmoves	zero,%fp1	| ...FP1 IS K = 0
	bra	LP1CONT1

LP1NEG0:
|--FPCR SAVED. D0 IS X IN COMPACT FORM.
	cmpil	#0,%d0
	blts	LP1NEG
LP1ZERO:
	fmoves	negone,%fp0

	fmovel	%d1,%fpcr
	bra t_dz

LP1NEG:
	fmoves	zero,%fp0

	fmovel	%d1,%fpcr
	bra	t_operr

	|end

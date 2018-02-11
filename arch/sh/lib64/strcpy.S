/* SPDX-License-Identifier: GPL-2.0 */
/* Cloned and hacked for uClibc by Paul Mundt, December 2003 */
/* Modified by SuperH, Inc. September 2003 */
! Entry: arg0: destination
!        arg1: source
! Exit:  result: destination
!
! SH5 code Copyright 2002 SuperH Ltd.

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define SHHI shlld
#define SHLO shlrd
#else
#define SHHI shlrd
#define SHLO shlld
#endif

	.section .text..SHmedia32,"ax"
	.globl	strcpy
	.type	strcpy, @function
	.align 5

strcpy:

	pta/l shortstring,tr1
	ldlo.q r3,0,r4
	ptabs r18,tr4
	shlli r3,3,r7
	addi r2, 8, r0
	mcmpeq.b r4,r63,r6
	SHHI r6,r7,r6
	bnei/u r6,0,tr1 // shortstring
	pta/l no_lddst, tr2
	ori r3,-8,r23
	sub r2, r23, r0
	sub r3, r2, r21
	addi r21, 8, r20
	ldx.q r0, r21, r5
	pta/l loop, tr0
	ori r2,-8,r22
	mcmpeq.b r5, r63, r6
	bgt/u r22, r23, tr2 // no_lddst

	// r22 < r23 :  Need to do a load from the destination.
	// r22 == r23 : Doesn't actually need to load from destination,
	//              but still can be handled here.
	ldlo.q r2, 0, r9
	movi -1, r8
	SHLO r8, r7, r8
	mcmv r4, r8, r9
	stlo.q r2, 0, r9
	beqi/l r6, 0, tr0 // loop

	add r5, r63, r4
	addi r0, 8, r0
	blink tr1, r63 // shortstring
no_lddst:
	// r22 > r23: note that for r22 == r23 the sthi.q would clobber
	//            bytes before the destination region.
	stlo.q r2, 0, r4
	SHHI r4, r7, r4
	sthi.q r0, -1, r4
	beqi/l r6, 0, tr0 // loop

	add r5, r63, r4
	addi r0, 8, r0
shortstring:
#if __BYTE_ORDER != __LITTLE_ENDIAN
	pta/l shortstring2,tr1
	byterev r4,r4
#endif
shortstring2:
	st.b r0,-8,r4
	andi r4,0xff,r5
	shlri r4,8,r4
	addi r0,1,r0
	bnei/l r5,0,tr1
	blink tr4,r63 // return
	
	.balign 8
loop:
	stlo.q r0, 0, r5
	ldx.q r0, r20, r4
	addi r0, 16, r0
	sthi.q r0, -9, r5
	mcmpeq.b r4, r63, r6
	bnei/u r6, 0, tr1 // shortstring
	ldx.q r0, r21, r5
	stlo.q r0, -8, r4
	sthi.q r0, -1, r4
	mcmpeq.b r5, r63, r6
	beqi/l r6, 0, tr0 // loop

	add r5, r63, r4
	addi r0, 8, r0
	blink tr1, r63 // shortstring

	.size	strcpy,.-strcpy

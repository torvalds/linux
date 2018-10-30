/* SPDX-License-Identifier: GPL-2.0 */
/* Cloned and hacked for uClibc by Paul Mundt, December 2003 */
/* Modified by SuperH, Inc. September 2003 */
!
! Fast SH memset
!
! by Toshiyasu Morita (tm@netcom.com)
!
! SH5 code by J"orn Rennecke (joern.rennecke@superh.com)
! Copyright 2002 SuperH Ltd.
!

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define SHHI shlld
#define SHLO shlrd
#else
#define SHHI shlrd
#define SHLO shlld
#endif

	.section .text..SHmedia32,"ax"
	.globl	memset
	.type	memset, @function

	.align 5

memset:
	pta/l multiquad, tr0
	andi r2, 7, r22
	ptabs r18, tr2
	mshflo.b r3,r3,r3
	add r4, r22, r23
	mperm.w r3, r63, r3	// Fill pattern now in every byte of r3

	movi 8, r9
	bgtu/u r23, r9, tr0 // multiquad

	beqi/u r4, 0, tr2       // Return with size 0 - ensures no mem accesses
	ldlo.q r2, 0, r7
	shlli r4, 2, r4
	movi -1, r8
	SHHI r8, r4, r8
	SHHI r8, r4, r8
	mcmv r7, r8, r3
	stlo.q r2, 0, r3
	blink tr2, r63

multiquad:
	pta/l lastquad, tr0
	stlo.q r2, 0, r3
	shlri r23, 3, r24
	add r2, r4, r5
	beqi/u r24, 1, tr0 // lastquad
	pta/l loop, tr1
	sub r2, r22, r25
	andi r5, -8, r20   // calculate end address and
	addi r20, -7*8, r8 // loop end address; This might overflow, so we need
	                   // to use a different test before we start the loop
	bge/u r24, r9, tr1 // loop
	st.q r25, 8, r3
	st.q r20, -8, r3
	shlri r24, 1, r24
	beqi/u r24, 1, tr0 // lastquad
	st.q r25, 16, r3
	st.q r20, -16, r3
	beqi/u r24, 2, tr0 // lastquad
	st.q r25, 24, r3
	st.q r20, -24, r3
lastquad:
	sthi.q r5, -1, r3
	blink tr2,r63

loop:
!!!	alloco r25, 32	// QQQ comment out for short-term fix to SHUK #3895.
			// QQQ commenting out is locically correct, but sub-optimal
			// QQQ Sean McGoogan - 4th April 2003.
	st.q r25, 8, r3
	st.q r25, 16, r3
	st.q r25, 24, r3
	st.q r25, 32, r3
	addi r25, 32, r25
	bgeu/l r8, r25, tr1 // loop

	st.q r20, -40, r3
	st.q r20, -32, r3
	st.q r20, -24, r3
	st.q r20, -16, r3
	st.q r20, -8, r3
	sthi.q r5, -1, r3
	blink tr2,r63

	.size	memset,.-memset

/* SPDX-License-Identifier: GPL-2.0 */
/* Cloned and hacked for uClibc by Paul Mundt, December 2003 */
/* Modified by SuperH, Inc. September 2003 */
!
! Fast SH memcpy
!
! by Toshiyasu Morita (tm@netcom.com)
! hacked by J"orn Rernnecke (joern.rennecke@superh.com) ("o for o-umlaut)
! SH5 code Copyright 2002 SuperH Ltd.
!
! Entry: ARG0: destination pointer
!        ARG1: source pointer
!        ARG2: byte count
!
! Exit:  RESULT: destination pointer
!        any other registers in the range r0-r7: trashed
!
! Notes: Usually one wants to do small reads and write a longword, but
!        unfortunately it is difficult in some cases to concatanate bytes
!        into a longword on the SH, so this does a longword read and small
!        writes.
!
! This implementation makes two assumptions about how it is called:
!
! 1.: If the byte count is nonzero, the address of the last byte to be
!     copied is unsigned greater than the address of the first byte to
!     be copied.  This could be easily swapped for a signed comparison,
!     but the algorithm used needs some comparison.
!
! 2.: When there are two or three bytes in the last word of an 11-or-more
!     bytes memory chunk to b copied, the rest of the word can be read
!     without side effects.
!     This could be easily changed by increasing the minimum size of
!     a fast memcpy and the amount subtracted from r7 before L_2l_loop be 2,
!     however, this would cost a few extra cyles on average.
!     For SHmedia, the assumption is that any quadword can be read in its
!     enirety if at least one byte is included in the copy.
!

	.section .text..SHmedia32,"ax"
	.globl	memcpy
	.type	memcpy, @function
	.align	5

memcpy:

#define LDUAQ(P,O,D0,D1) ldlo.q P,O,D0; ldhi.q P,O+7,D1
#define STUAQ(P,O,D0,D1) stlo.q P,O,D0; sthi.q P,O+7,D1
#define LDUAL(P,O,D0,D1) ldlo.l P,O,D0; ldhi.l P,O+3,D1
#define STUAL(P,O,D0,D1) stlo.l P,O,D0; sthi.l P,O+3,D1

	ld.b r3,0,r63
	pta/l Large,tr0
	movi 25,r0
	bgeu/u r4,r0,tr0
	nsb r4,r0
	shlli r0,5,r0
	movi (L1-L0+63*32 + 1) & 0xffff,r1
	sub r1, r0, r0
L0:	ptrel r0,tr0
	add r2,r4,r5
	ptabs r18,tr1
	add r3,r4,r6
	blink tr0,r63
	
/* Rearranged to make cut2 safe */
	.balign 8
L4_7:	/* 4..7 byte memcpy cntd. */
	stlo.l r2, 0, r0
	or r6, r7, r6
	sthi.l r5, -1, r6
	stlo.l r5, -4, r6
	blink tr1,r63

	.balign 8
L1:	/* 0 byte memcpy */
	nop
	blink tr1,r63
	nop
	nop
	nop
	nop

L2_3:	/* 2 or 3 byte memcpy cntd. */
	st.b r5,-1,r6
	blink tr1,r63

	/* 1 byte memcpy */
	ld.b r3,0,r0
	st.b r2,0,r0
	blink tr1,r63

L8_15:	/* 8..15 byte memcpy cntd. */
	stlo.q r2, 0, r0
	or r6, r7, r6
	sthi.q r5, -1, r6
	stlo.q r5, -8, r6
	blink tr1,r63
	
	/* 2 or 3 byte memcpy */
	ld.b r3,0,r0
	ld.b r2,0,r63
	ld.b r3,1,r1
	st.b r2,0,r0
	pta/l L2_3,tr0
	ld.b r6,-1,r6
	st.b r2,1,r1
	blink tr0, r63

	/* 4 .. 7 byte memcpy */
	LDUAL (r3, 0, r0, r1)
	pta L4_7, tr0
	ldlo.l r6, -4, r7
	or r0, r1, r0
	sthi.l r2, 3, r0
	ldhi.l r6, -1, r6
	blink tr0, r63

	/* 8 .. 15 byte memcpy */
	LDUAQ (r3, 0, r0, r1)
	pta L8_15, tr0
	ldlo.q r6, -8, r7
	or r0, r1, r0
	sthi.q r2, 7, r0
	ldhi.q r6, -1, r6
	blink tr0, r63

	/* 16 .. 24 byte memcpy */
	LDUAQ (r3, 0, r0, r1)
	LDUAQ (r3, 8, r8, r9)
	or r0, r1, r0
	sthi.q r2, 7, r0
	or r8, r9, r8
	sthi.q r2, 15, r8
	ldlo.q r6, -8, r7
	ldhi.q r6, -1, r6
	stlo.q r2, 8, r8
	stlo.q r2, 0, r0
	or r6, r7, r6
	sthi.q r5, -1, r6
	stlo.q r5, -8, r6
	blink tr1,r63

Large:
	ld.b r2, 0, r63
	pta/l  Loop_ua, tr1
	ori r3, -8, r7
	sub r2, r7, r22
	sub r3, r2, r6
	add r2, r4, r5
	ldlo.q r3, 0, r0
	addi r5, -16, r5
	movi 64+8, r27 // could subtract r7 from that.
	stlo.q r2, 0, r0
	sthi.q r2, 7, r0
	ldx.q r22, r6, r0
	bgtu/l r27, r4, tr1

	addi r5, -48, r27
	pta/l Loop_line, tr0
	addi r6, 64, r36
	addi r6, -24, r19
	addi r6, -16, r20
	addi r6, -8, r21

Loop_line:
	ldx.q r22, r36, r63
	alloco r22, 32
	addi r22, 32, r22
	ldx.q r22, r19, r23
	sthi.q r22, -25, r0
	ldx.q r22, r20, r24
	ldx.q r22, r21, r25
	stlo.q r22, -32, r0
	ldx.q r22, r6,  r0
	sthi.q r22, -17, r23
	sthi.q r22,  -9, r24
	sthi.q r22,  -1, r25
	stlo.q r22, -24, r23
	stlo.q r22, -16, r24
	stlo.q r22,  -8, r25
	bgeu r27, r22, tr0

Loop_ua:
	addi r22, 8, r22
	sthi.q r22, -1, r0
	stlo.q r22, -8, r0
	ldx.q r22, r6, r0
	bgtu/l r5, r22, tr1

	add r3, r4, r7
	ldlo.q r7, -8, r1
	sthi.q r22, 7, r0
	ldhi.q r7, -1, r7
	ptabs r18,tr1
	stlo.q r22, 0, r0
	or r1, r7, r1
	sthi.q r5, 15, r1
	stlo.q r5, 8, r1
	blink tr1, r63

	.size memcpy,.-memcpy

/*
   Copyright 2003 Richard Curnow, SuperH (UK) Ltd.

   This file is subject to the terms and conditions of the GNU General Public
   License.  See the file "COPYING" in the main directory of this archive
   for more details.

   Tight version of mempy for the case of just copying a page.
   Prefetch strategy empirically optimised against RTL simulations
   of SH5-101 cut2 eval chip with Cayman board DDR memory.

   Parameters:
   r2 : destination effective address (start of page)
   r3 : source effective address (start of page)

   Always copies 4096 bytes.

   Points to review.
   * Currently the prefetch is 4 lines ahead and the alloco is 2 lines ahead.
     It seems like the prefetch needs to be at at least 4 lines ahead to get
     the data into the cache in time, and the allocos contend with outstanding
     prefetches for the same cache set, so it's better to have the numbers
     different.
   */

	.section .text..SHmedia32,"ax"
	.little

	.balign 8
	.global copy_page
copy_page:

	/* Copy 4096 bytes worth of data from r3 to r2.
	   Do prefetches 4 lines ahead.
	   Do alloco 2 lines ahead */

	pta 1f, tr1
	pta 2f, tr2
	pta 3f, tr3
	ptabs r18, tr0

#if 0
	/* TAKum03020 */
	ld.q r3, 0x00, r63
	ld.q r3, 0x20, r63
	ld.q r3, 0x40, r63
	ld.q r3, 0x60, r63
#endif
	alloco r2, 0x00
	synco		! TAKum03020
	alloco r2, 0x20
	synco		! TAKum03020

	movi 3968, r6
	add  r2, r6, r6
	addi r6, 64, r7
	addi r7, 64, r8
	sub r3, r2, r60
	addi r60, 8, r61
	addi r61, 8, r62
	addi r62, 8, r23
	addi r60, 0x80, r22

/* Minimal code size.  The extra branches inside the loop don't cost much
   because they overlap with the time spent waiting for prefetches to
   complete. */
1:
#if 0
	/* TAKum03020 */
	bge/u r2, r6, tr2  ! skip prefetch for last 4 lines
	ldx.q r2, r22, r63 ! prefetch 4 lines hence
#endif
2:
	bge/u r2, r7, tr3  ! skip alloco for last 2 lines
	alloco r2, 0x40    ! alloc destination line 2 lines ahead
	synco		! TAKum03020
3:
	ldx.q r2, r60, r36
	ldx.q r2, r61, r37
	ldx.q r2, r62, r38
	ldx.q r2, r23, r39
	st.q  r2,   0, r36
	st.q  r2,   8, r37
	st.q  r2,  16, r38
	st.q  r2,  24, r39
	addi r2, 32, r2
	bgt/l r8, r2, tr1

	blink tr0, r63	   ! return

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>

#if LONGSIZE == 4
#define LONG_S_L swl
#define LONG_S_R swr
#else
#define LONG_S_L sdl
#define LONG_S_R sdr
#endif

#define EX(insn,reg,addr,handler)			\
9:	insn	reg, addr;				\
	.section __ex_table,"a"; 			\
	PTR	9b, handler; 				\
	.previous

	.macro	f_fill64 dst, offset, val, fixup
	EX(LONG_S, \val, (\offset +  0 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  1 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  2 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  3 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  4 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  5 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  6 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  7 * LONGSIZE)(\dst), \fixup)
#if LONGSIZE == 4
	EX(LONG_S, \val, (\offset +  8 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  9 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 10 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 11 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 12 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 13 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 14 * LONGSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 15 * LONGSIZE)(\dst), \fixup)
#endif
	.endm

/*
 * memset(void *s, int c, size_t n)
 *
 * a0: start of area to clear
 * a1: char to fill with
 * a2: size of area to clear
 */
	.set	noreorder
	.align	5
LEAF(memset)
	beqz		a1, 1f
	 move		v0, a0			/* result */

	andi		a1, 0xff		/* spread fillword */
	LONG_SLL		t1, a1, 8
	or		a1, t1
	LONG_SLL		t1, a1, 16
#if LONGSIZE == 8
	or		a1, t1
	LONG_SLL		t1, a1, 32
#endif
	or		a1, t1
1:

FEXPORT(__bzero)
	sltiu		t0, a2, LONGSIZE	/* very small region? */
	bnez		t0, small_memset
	 andi		t0, a0, LONGMASK	/* aligned? */

	beqz		t0, 1f
	 PTR_SUBU	t0, LONGSIZE		/* alignment in bytes */

#ifdef __MIPSEB__
	EX(LONG_S_L, a1, (a0), first_fixup)	/* make word/dword aligned */
#endif
#ifdef __MIPSEL__
	EX(LONG_S_R, a1, (a0), first_fixup)	/* make word/dword aligned */
#endif
	PTR_SUBU	a0, t0			/* long align ptr */
	PTR_ADDU	a2, t0			/* correct size */

1:	ori		t1, a2, 0x3f		/* # of full blocks */
	xori		t1, 0x3f
	beqz		t1, memset_partial	/* no block to fill */
	 andi		t0, a2, 0x40-LONGSIZE

	PTR_ADDU	t1, a0			/* end address */
	.set		reorder
1:	PTR_ADDIU	a0, 64
	f_fill64 a0, -64, a1, fwd_fixup
	bne		t1, a0, 1b
	.set		noreorder

memset_partial:
	PTR_LA		t1, 2f			/* where to start */
#if LONGSIZE == 4
	PTR_SUBU	t1, t0
#else
	.set		noat
	LONG_SRL		AT, t0, 1
	PTR_SUBU	t1, AT
	.set		noat
#endif
	jr		t1
	 PTR_ADDU	a0, t0			/* dest ptr */

	.set		push
	.set		noreorder
	.set		nomacro
	f_fill64 a0, -64, a1, partial_fixup	/* ... but first do longs ... */
2:	.set		pop
	andi		a2, LONGMASK		/* At most one long to go */

	beqz		a2, 1f
	 PTR_ADDU	a0, a2			/* What's left */
#ifdef __MIPSEB__
	EX(LONG_S_R, a1, -1(a0), last_fixup)
#endif
#ifdef __MIPSEL__
	EX(LONG_S_L, a1, -1(a0), last_fixup)
#endif
1:	jr		ra
	 move		a2, zero

small_memset:
	beqz		a2, 2f
	 PTR_ADDU	t1, a0, a2

1:	PTR_ADDIU	a0, 1			/* fill bytewise */
	bne		t1, a0, 1b
	 sb		a1, -1(a0)

2:	jr		ra			/* done */
	 move		a2, zero
	END(memset)

first_fixup:
	jr	ra
	 nop

fwd_fixup:
	PTR_L		t0, TI_TASK($28)
	LONG_L		t0, THREAD_BUADDR(t0)
	andi		a2, 0x3f
	LONG_ADDU	a2, t1
	jr		ra
	 LONG_SUBU	a2, t0

partial_fixup:
	PTR_L		t0, TI_TASK($28)
	LONG_L		t0, THREAD_BUADDR(t0)
	andi		a2, LONGMASK
	LONG_ADDU	a2, t1
	jr		ra
	 LONG_SUBU	a2, t0

last_fixup:
	jr		ra
	 andi		v1, a2, LONGMASK

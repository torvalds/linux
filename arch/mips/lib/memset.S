/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2007 by Maciej W. Rozycki
 * Copyright (C) 2011, 2012 MIPS Technologies, Inc.
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

#ifdef CONFIG_CPU_MICROMIPS
#define STORSIZE (LONGSIZE * 2)
#define STORMASK (STORSIZE - 1)
#define FILL64RG t8
#define FILLPTRG t7
#undef  LONG_S
#define LONG_S LONG_SP
#else
#define STORSIZE LONGSIZE
#define STORMASK LONGMASK
#define FILL64RG a1
#define FILLPTRG t0
#endif

#define LEGACY_MODE 1
#define EVA_MODE    2

/*
 * No need to protect it with EVA #ifdefery. The generated block of code
 * will never be assembled if EVA is not enabled.
 */
#define __EVAFY(insn, reg, addr) __BUILD_EVA_INSN(insn##e, reg, addr)
#define ___BUILD_EVA_INSN(insn, reg, addr) __EVAFY(insn, reg, addr)

#define EX(insn,reg,addr,handler)			\
	.if \mode == LEGACY_MODE;			\
9:		insn	reg, addr;			\
	.else;						\
9:		___BUILD_EVA_INSN(insn, reg, addr);	\
	.endif;						\
	.section __ex_table,"a";			\
	PTR	9b, handler;				\
	.previous

	.macro	f_fill64 dst, offset, val, fixup, mode
	EX(LONG_S, \val, (\offset +  0 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  1 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  2 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  3 * STORSIZE)(\dst), \fixup)
#if ((defined(CONFIG_CPU_MICROMIPS) && (LONGSIZE == 4)) || !defined(CONFIG_CPU_MICROMIPS))
	EX(LONG_S, \val, (\offset +  4 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  5 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  6 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  7 * STORSIZE)(\dst), \fixup)
#endif
#if (!defined(CONFIG_CPU_MICROMIPS) && (LONGSIZE == 4))
	EX(LONG_S, \val, (\offset +  8 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset +  9 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 10 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 11 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 12 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 13 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 14 * STORSIZE)(\dst), \fixup)
	EX(LONG_S, \val, (\offset + 15 * STORSIZE)(\dst), \fixup)
#endif
	.endm

	.set	noreorder
	.align	5

	/*
	 * Macro to generate the __bzero{,_user} symbol
	 * Arguments:
	 * mode: LEGACY_MODE or EVA_MODE
	 */
	.macro __BUILD_BZERO mode
	/* Initialize __memset if this is the first time we call this macro */
	.ifnotdef __memset
	.set __memset, 1
	.hidden __memset /* Make sure it does not leak */
	.endif

	sltiu		t0, a2, STORSIZE	/* very small region? */
	bnez		t0, .Lsmall_memset\@
	andi		t0, a0, STORMASK	/* aligned? */

#ifdef CONFIG_CPU_MICROMIPS
	move		t8, a1			/* used by 'swp' instruction */
	move		t9, a1
#endif
#ifndef CONFIG_CPU_DADDI_WORKAROUNDS
	beqz		t0, 1f
	PTR_SUBU	t0, STORSIZE		/* alignment in bytes */
#else
	.set		noat
	li		AT, STORSIZE
	beqz		t0, 1f
	PTR_SUBU	t0, AT			/* alignment in bytes */
	.set		at
#endif

#ifndef CONFIG_CPU_MIPSR6
	R10KCBARRIER(0(ra))
#ifdef __MIPSEB__
	EX(LONG_S_L, a1, (a0), .Lfirst_fixup\@)	/* make word/dword aligned */
#else
	EX(LONG_S_R, a1, (a0), .Lfirst_fixup\@)	/* make word/dword aligned */
#endif
	PTR_SUBU	a0, t0			/* long align ptr */
	PTR_ADDU	a2, t0			/* correct size */

#else /* CONFIG_CPU_MIPSR6 */
#define STORE_BYTE(N)				\
	EX(sb, a1, N(a0), .Lbyte_fixup\@);	\
	beqz		t0, 0f;			\
	PTR_ADDU	t0, 1;

	PTR_ADDU	a2, t0			/* correct size */
	PTR_ADDU	t0, 1
	STORE_BYTE(0)
	STORE_BYTE(1)
#if LONGSIZE == 4
	EX(sb, a1, 2(a0), .Lbyte_fixup\@)
#else
	STORE_BYTE(2)
	STORE_BYTE(3)
	STORE_BYTE(4)
	STORE_BYTE(5)
	EX(sb, a1, 6(a0), .Lbyte_fixup\@)
#endif
0:
	ori		a0, STORMASK
	xori		a0, STORMASK
	PTR_ADDIU	a0, STORSIZE
#endif /* CONFIG_CPU_MIPSR6 */
1:	ori		t1, a2, 0x3f		/* # of full blocks */
	xori		t1, 0x3f
	beqz		t1, .Lmemset_partial\@	/* no block to fill */
	andi		t0, a2, 0x40-STORSIZE

	PTR_ADDU	t1, a0			/* end address */
	.set		reorder
1:	PTR_ADDIU	a0, 64
	R10KCBARRIER(0(ra))
	f_fill64 a0, -64, FILL64RG, .Lfwd_fixup\@, \mode
	bne		t1, a0, 1b
	.set		noreorder

.Lmemset_partial\@:
	R10KCBARRIER(0(ra))
	PTR_LA		t1, 2f			/* where to start */
#ifdef CONFIG_CPU_MICROMIPS
	LONG_SRL	t7, t0, 1
#endif
#if LONGSIZE == 4
	PTR_SUBU	t1, FILLPTRG
#else
	.set		noat
	LONG_SRL	AT, FILLPTRG, 1
	PTR_SUBU	t1, AT
	.set		at
#endif
	jr		t1
	PTR_ADDU	a0, t0			/* dest ptr */

	.set		push
	.set		noreorder
	.set		nomacro
	/* ... but first do longs ... */
	f_fill64 a0, -64, FILL64RG, .Lpartial_fixup\@, \mode
2:	.set		pop
	andi		a2, STORMASK		/* At most one long to go */

	beqz		a2, 1f
#ifndef CONFIG_CPU_MIPSR6
	PTR_ADDU	a0, a2			/* What's left */
	R10KCBARRIER(0(ra))
#ifdef __MIPSEB__
	EX(LONG_S_R, a1, -1(a0), .Llast_fixup\@)
#else
	EX(LONG_S_L, a1, -1(a0), .Llast_fixup\@)
#endif
#else
	PTR_SUBU	t0, $0, a2
	PTR_ADDIU	t0, 1
	STORE_BYTE(0)
	STORE_BYTE(1)
#if LONGSIZE == 4
	EX(sb, a1, 2(a0), .Lbyte_fixup\@)
#else
	STORE_BYTE(2)
	STORE_BYTE(3)
	STORE_BYTE(4)
	STORE_BYTE(5)
	EX(sb, a1, 6(a0), .Lbyte_fixup\@)
#endif
0:
#endif
1:	jr		ra
	move		a2, zero

.Lsmall_memset\@:
	beqz		a2, 2f
	PTR_ADDU	t1, a0, a2

1:	PTR_ADDIU	a0, 1			/* fill bytewise */
	R10KCBARRIER(0(ra))
	bne		t1, a0, 1b
	sb		a1, -1(a0)

2:	jr		ra			/* done */
	move		a2, zero
	.if __memset == 1
	END(memset)
	.set __memset, 0
	.hidden __memset
	.endif

.Lbyte_fixup\@:
	PTR_SUBU	a2, $0, t0
	jr		ra
	 PTR_ADDIU	a2, 1

.Lfirst_fixup\@:
	jr	ra
	nop

.Lfwd_fixup\@:
	PTR_L		t0, TI_TASK($28)
	andi		a2, 0x3f
	LONG_L		t0, THREAD_BUADDR(t0)
	LONG_ADDU	a2, t1
	jr		ra
	LONG_SUBU	a2, t0

.Lpartial_fixup\@:
	PTR_L		t0, TI_TASK($28)
	andi		a2, STORMASK
	LONG_L		t0, THREAD_BUADDR(t0)
	LONG_ADDU	a2, t1
	jr		ra
	LONG_SUBU	a2, t0

.Llast_fixup\@:
	jr		ra
	andi		v1, a2, STORMASK

	.endm

/*
 * memset(void *s, int c, size_t n)
 *
 * a0: start of area to clear
 * a1: char to fill with
 * a2: size of area to clear
 */

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
#ifndef CONFIG_EVA
FEXPORT(__bzero)
#endif
	__BUILD_BZERO LEGACY_MODE

#ifdef CONFIG_EVA
LEAF(__bzero)
	__BUILD_BZERO EVA_MODE
END(__bzero)
#endif

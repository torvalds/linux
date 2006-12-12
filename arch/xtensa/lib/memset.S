/*
 *  arch/xtensa/lib/memset.S
 *
 *  ANSI C standard library function memset
 *  (Well, almost.  .fixup code might return zero.)
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file "COPYING" in the main directory of
 *  this archive for more details.
 *
 *  Copyright (C) 2002 Tensilica Inc.
 */

#include <asm/variant/core.h>

/*
 * void *memset(void *dst, int c, size_t length)
 *
 * The algorithm is as follows:
 *   Create a word with c in all byte positions
 *   If the destination is aligned,
 *     do 16B chucks with a loop, and then finish up with
 *     8B, 4B, 2B, and 1B stores conditional on the length.
 *   If destination is unaligned, align it by conditionally
 *     setting 1B and 2B and then go to aligned case.
 *   This code tries to use fall-through branches for the common
 *     case of an aligned destination (except for the branches to
 *     the alignment labels).
 */

/* Load or store instructions that may cause exceptions use the EX macro. */

#define EX(insn,reg1,reg2,offset,handler)	\
9:	insn	reg1, reg2, offset;		\
	.section __ex_table, "a";		\
	.word	9b, handler;			\
	.previous


.text
.align	4
.global	memset
.type	memset,@function
memset:
	entry	sp, 16		# minimal stack frame
	# a2/ dst, a3/ c, a4/ length
	extui	a3, a3, 0, 8	# mask to just 8 bits
	slli	a7, a3, 8	# duplicate character in all bytes of word
	or	a3, a3, a7	# ...
	slli	a7, a3, 16	# ...
	or	a3, a3, a7	# ...
	mov	a5, a2		# copy dst so that a2 is return value
	movi	a6, 3		# for alignment tests
	bany	a2, a6, .Ldstunaligned # if dst is unaligned
.L0:	# return here from .Ldstunaligned when dst is aligned
	srli	a7, a4, 4	# number of loop iterations with 16B
				# per iteration
	bnez	a4, .Laligned
	retw

/*
 * Destination is word-aligned.
 */
	# set 16 bytes per iteration for word-aligned dst
	.align	4		# 1 mod 4 alignment for LOOPNEZ
	.byte	0		# (0 mod 4 alignment for LBEG)
.Laligned:
#if XCHAL_HAVE_LOOPS
	loopnez	a7, .Loop1done
#else /* !XCHAL_HAVE_LOOPS */
	beqz	a7, .Loop1done
	slli	a6, a7, 4
	add	a6, a6, a5	# a6 = end of last 16B chunk
#endif /* !XCHAL_HAVE_LOOPS */
.Loop1:
	EX(s32i, a3, a5,  0, memset_fixup)
	EX(s32i, a3, a5,  4, memset_fixup)
	EX(s32i, a3, a5,  8, memset_fixup)
	EX(s32i, a3, a5, 12, memset_fixup)
	addi	a5, a5, 16
#if !XCHAL_HAVE_LOOPS
	blt	a5, a6, .Loop1
#endif /* !XCHAL_HAVE_LOOPS */
.Loop1done:
	bbci.l	a4, 3, .L2
	# set 8 bytes
	EX(s32i, a3, a5,  0, memset_fixup)
	EX(s32i, a3, a5,  4, memset_fixup)
	addi	a5, a5,  8
.L2:
	bbci.l	a4, 2, .L3
	# set 4 bytes
	EX(s32i, a3, a5,  0, memset_fixup)
	addi	a5, a5,  4
.L3:
	bbci.l	a4, 1, .L4
	# set 2 bytes
	EX(s16i, a3, a5,  0, memset_fixup)
	addi	a5, a5,  2
.L4:
	bbci.l	a4, 0, .L5
	# set 1 byte
	EX(s8i, a3, a5,  0, memset_fixup)
.L5:
.Lret1:
	retw

/*
 * Destination is unaligned
 */

.Ldstunaligned:
	bltui	a4, 8, .Lbyteset	# do short copies byte by byte
	bbci.l	a5, 0, .L20		# branch if dst alignment half-aligned
	# dst is only byte aligned
	# set 1 byte
	EX(s8i, a3, a5,  0, memset_fixup)
	addi	a5, a5,  1
	addi	a4, a4, -1
	# now retest if dst aligned
	bbci.l	a5, 1, .L0	# if now aligned, return to main algorithm
.L20:
	# dst half-aligned
	# set 2 bytes
	EX(s16i, a3, a5,  0, memset_fixup)
	addi	a5, a5,  2
	addi	a4, a4, -2
	j	.L0		# dst is now aligned, return to main algorithm

/*
 * Byte by byte set
 */
	.align	4
	.byte	0		# 1 mod 4 alignment for LOOPNEZ
				# (0 mod 4 alignment for LBEG)
.Lbyteset:
#if XCHAL_HAVE_LOOPS
	loopnez	a4, .Lbytesetdone
#else /* !XCHAL_HAVE_LOOPS */
	beqz	a4, .Lbytesetdone
	add	a6, a5, a4	# a6 = ending address
#endif /* !XCHAL_HAVE_LOOPS */
.Lbyteloop:
	EX(s8i, a3, a5, 0, memset_fixup)
	addi	a5, a5, 1
#if !XCHAL_HAVE_LOOPS
	blt	a5, a6, .Lbyteloop
#endif /* !XCHAL_HAVE_LOOPS */
.Lbytesetdone:
	retw


	.section .fixup, "ax"
	.align	4

/* We return zero if a failure occurred. */

memset_fixup:
	movi	a2, 0
	retw

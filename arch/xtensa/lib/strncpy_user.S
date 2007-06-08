/*
 *  arch/xtensa/lib/strncpy_user.S
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file "COPYING" in the main directory of
 *  this archive for more details.
 *
 *  Returns: -EFAULT if exception before terminator, N if the entire
 *  buffer filled, else strlen.
 *
 *  Copyright (C) 2002 Tensilica Inc.
 */

#include <asm/variant/core.h>
#include <linux/errno.h>

/* Load or store instructions that may cause exceptions use the EX macro. */

#define EX(insn,reg1,reg2,offset,handler)	\
9:	insn	reg1, reg2, offset;		\
	.section __ex_table, "a";		\
	.word	9b, handler;			\
	.previous

/*
 * char *__strncpy_user(char *dst, const char *src, size_t len)
 */

#ifdef __XTENSA_EB__
# define MASK0 0xff000000
# define MASK1 0x00ff0000
# define MASK2 0x0000ff00
# define MASK3 0x000000ff
#else
# define MASK0 0x000000ff
# define MASK1 0x0000ff00
# define MASK2 0x00ff0000
# define MASK3 0xff000000
#endif

# Register use
#   a0/ return address
#   a1/ stack pointer
#   a2/ return value
#   a3/ src
#   a4/ len
#   a5/ mask0
#   a6/ mask1
#   a7/ mask2
#   a8/ mask3
#   a9/ tmp
#   a10/ tmp
#   a11/ dst
#   a12/ tmp

.text
.align	4
.global	__strncpy_user
.type	__strncpy_user,@function
__strncpy_user:
	entry	sp, 16		# minimal stack frame
	# a2/ dst, a3/ src, a4/ len
	mov	a11, a2		# leave dst in return value register
	beqz	a4, .Lret	# if len is zero
	movi	a5, MASK0	# mask for byte 0
	movi	a6, MASK1	# mask for byte 1
	movi	a7, MASK2	# mask for byte 2
	movi	a8, MASK3	# mask for byte 3
	bbsi.l	a3, 0, .Lsrc1mod2 # if only  8-bit aligned
	bbsi.l	a3, 1, .Lsrc2mod4 # if only 16-bit aligned
.Lsrcaligned:	# return here when src is word-aligned
	srli	a12, a4, 2	# number of loop iterations with 4B per loop
	movi	a9, 3
	bnone	a11, a9, .Laligned
	j	.Ldstunaligned

.Lsrc1mod2:	# src address is odd
	EX(l8ui, a9, a3, 0, fixup_l)	# get byte 0
	addi	a3, a3, 1		# advance src pointer
	EX(s8i, a9, a11, 0, fixup_s)	# store byte 0
	beqz	a9, .Lret		# if byte 0 is zero
	addi	a11, a11, 1		# advance dst pointer
	addi	a4, a4, -1		# decrement len
	beqz	a4, .Lret		# if len is zero
	bbci.l	a3, 1, .Lsrcaligned	# if src is now word-aligned

.Lsrc2mod4:	# src address is 2 mod 4
	EX(l8ui, a9, a3, 0, fixup_l)	# get byte 0
	/* 1-cycle interlock */
	EX(s8i, a9, a11, 0, fixup_s)	# store byte 0
	beqz	a9, .Lret		# if byte 0 is zero
	addi	a11, a11, 1		# advance dst pointer
	addi	a4, a4, -1		# decrement len
	beqz	a4, .Lret		# if len is zero
	EX(l8ui, a9, a3, 1, fixup_l)	# get byte 0
	addi	a3, a3, 2		# advance src pointer
	EX(s8i, a9, a11, 0, fixup_s)	# store byte 0
	beqz	a9, .Lret		# if byte 0 is zero
	addi	a11, a11, 1		# advance dst pointer
	addi	a4, a4, -1		# decrement len
	bnez	a4, .Lsrcaligned	# if len is nonzero
.Lret:
	sub	a2, a11, a2		# compute strlen
	retw

/*
 * dst is word-aligned, src is word-aligned
 */
	.align	4		# 1 mod 4 alignment for LOOPNEZ
	.byte	0		# (0 mod 4 alignment for LBEG)
.Laligned:
#if XCHAL_HAVE_LOOPS
	loopnez	a12, .Loop1done
#else
	beqz	a12, .Loop1done
	slli	a12, a12, 2
	add	a12, a12, a11	# a12 = end of last 4B chunck
#endif
.Loop1:
	EX(l32i, a9, a3, 0, fixup_l)	# get word from src
	addi	a3, a3, 4		# advance src pointer
	bnone	a9, a5, .Lz0		# if byte 0 is zero
	bnone	a9, a6, .Lz1		# if byte 1 is zero
	bnone	a9, a7, .Lz2		# if byte 2 is zero
	EX(s32i, a9, a11, 0, fixup_s)	# store word to dst
	bnone	a9, a8, .Lz3		# if byte 3 is zero
	addi	a11, a11, 4		# advance dst pointer
#if !XCHAL_HAVE_LOOPS
	blt	a11, a12, .Loop1
#endif

.Loop1done:
	bbci.l	a4, 1, .L100
	# copy 2 bytes
	EX(l16ui, a9, a3, 0, fixup_l)
	addi	a3, a3, 2		# advance src pointer
#ifdef __XTENSA_EB__
	bnone	a9, a7, .Lz0		# if byte 2 is zero
	bnone	a9, a8, .Lz1		# if byte 3 is zero
#else
	bnone	a9, a5, .Lz0		# if byte 0 is zero
	bnone	a9, a6, .Lz1		# if byte 1 is zero
#endif
	EX(s16i, a9, a11, 0, fixup_s)
	addi	a11, a11, 2		# advance dst pointer
.L100:
	bbci.l	a4, 0, .Lret
	EX(l8ui, a9, a3, 0, fixup_l)
	/* slot */
	EX(s8i, a9, a11, 0, fixup_s)
	beqz	a9, .Lret		# if byte is zero
	addi	a11, a11, 1-3		# advance dst ptr 1, but also cancel
					# the effect of adding 3 in .Lz3 code
	/* fall thru to .Lz3 and "retw" */

.Lz3:	# byte 3 is zero
	addi	a11, a11, 3		# advance dst pointer
	sub	a2, a11, a2		# compute strlen
	retw
.Lz0:	# byte 0 is zero
#ifdef __XTENSA_EB__
	movi	a9, 0
#endif /* __XTENSA_EB__ */
	EX(s8i, a9, a11, 0, fixup_s)
	sub	a2, a11, a2		# compute strlen
	retw
.Lz1:	# byte 1 is zero
#ifdef __XTENSA_EB__
        extui   a9, a9, 16, 16
#endif /* __XTENSA_EB__ */
	EX(s16i, a9, a11, 0, fixup_s)
	addi	a11, a11, 1		# advance dst pointer
	sub	a2, a11, a2		# compute strlen
	retw
.Lz2:	# byte 2 is zero
#ifdef __XTENSA_EB__
        extui   a9, a9, 16, 16
#endif /* __XTENSA_EB__ */
	EX(s16i, a9, a11, 0, fixup_s)
	movi	a9, 0
	EX(s8i, a9, a11, 2, fixup_s)
	addi	a11, a11, 2		# advance dst pointer
	sub	a2, a11, a2		# compute strlen
	retw

	.align	4		# 1 mod 4 alignment for LOOPNEZ
	.byte	0		# (0 mod 4 alignment for LBEG)
.Ldstunaligned:
/*
 * for now just use byte copy loop
 */
#if XCHAL_HAVE_LOOPS
	loopnez	a4, .Lunalignedend
#else
	beqz	a4, .Lunalignedend
	add	a12, a11, a4		# a12 = ending address
#endif /* XCHAL_HAVE_LOOPS */
.Lnextbyte:
	EX(l8ui, a9, a3, 0, fixup_l)
	addi	a3, a3, 1
	EX(s8i, a9, a11, 0, fixup_s)
	beqz	a9, .Lunalignedend
	addi	a11, a11, 1
#if !XCHAL_HAVE_LOOPS
	blt	a11, a12, .Lnextbyte
#endif

.Lunalignedend:
	sub	a2, a11, a2		# compute strlen
	retw


	.section .fixup, "ax"
	.align	4

	/* For now, just return -EFAULT.  Future implementations might
	 * like to clear remaining kernel space, like the fixup
	 * implementation in memset().  Thus, we differentiate between
	 * load/store fixups. */

fixup_s:
fixup_l:
	movi	a2, -EFAULT
	retw


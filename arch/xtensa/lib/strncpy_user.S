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

#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

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

.text
ENTRY(__strncpy_user)

	abi_entry_default
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
	srli	a10, a4, 2	# number of loop iterations with 4B per loop
	movi	a9, 3
	bnone	a11, a9, .Laligned
	j	.Ldstunaligned

.Lsrc1mod2:	# src address is odd
EX(11f)	l8ui	a9, a3, 0		# get byte 0
	addi	a3, a3, 1		# advance src pointer
EX(10f)	s8i	a9, a11, 0		# store byte 0
	beqz	a9, .Lret		# if byte 0 is zero
	addi	a11, a11, 1		# advance dst pointer
	addi	a4, a4, -1		# decrement len
	beqz	a4, .Lret		# if len is zero
	bbci.l	a3, 1, .Lsrcaligned	# if src is now word-aligned

.Lsrc2mod4:	# src address is 2 mod 4
EX(11f)	l8ui	a9, a3, 0		# get byte 0
	/* 1-cycle interlock */
EX(10f)	s8i	a9, a11, 0		# store byte 0
	beqz	a9, .Lret		# if byte 0 is zero
	addi	a11, a11, 1		# advance dst pointer
	addi	a4, a4, -1		# decrement len
	beqz	a4, .Lret		# if len is zero
EX(11f)	l8ui	a9, a3, 1		# get byte 0
	addi	a3, a3, 2		# advance src pointer
EX(10f)	s8i	a9, a11, 0		# store byte 0
	beqz	a9, .Lret		# if byte 0 is zero
	addi	a11, a11, 1		# advance dst pointer
	addi	a4, a4, -1		# decrement len
	bnez	a4, .Lsrcaligned	# if len is nonzero
.Lret:
	sub	a2, a11, a2		# compute strlen
	abi_ret_default

/*
 * dst is word-aligned, src is word-aligned
 */
	.align	4		# 1 mod 4 alignment for LOOPNEZ
	.byte	0		# (0 mod 4 alignment for LBEG)
.Laligned:
#if XCHAL_HAVE_LOOPS
	loopnez	a10, .Loop1done
#else
	beqz	a10, .Loop1done
	slli	a10, a10, 2
	add	a10, a10, a11	# a10 = end of last 4B chunck
#endif
.Loop1:
EX(11f)	l32i	a9, a3, 0		# get word from src
	addi	a3, a3, 4		# advance src pointer
	bnone	a9, a5, .Lz0		# if byte 0 is zero
	bnone	a9, a6, .Lz1		# if byte 1 is zero
	bnone	a9, a7, .Lz2		# if byte 2 is zero
EX(10f)	s32i	a9, a11, 0		# store word to dst
	bnone	a9, a8, .Lz3		# if byte 3 is zero
	addi	a11, a11, 4		# advance dst pointer
#if !XCHAL_HAVE_LOOPS
	blt	a11, a10, .Loop1
#endif

.Loop1done:
	bbci.l	a4, 1, .L100
	# copy 2 bytes
EX(11f)	l16ui	a9, a3, 0
	addi	a3, a3, 2		# advance src pointer
#ifdef __XTENSA_EB__
	bnone	a9, a7, .Lz0		# if byte 2 is zero
	bnone	a9, a8, .Lz1		# if byte 3 is zero
#else
	bnone	a9, a5, .Lz0		# if byte 0 is zero
	bnone	a9, a6, .Lz1		# if byte 1 is zero
#endif
EX(10f)	s16i	a9, a11, 0
	addi	a11, a11, 2		# advance dst pointer
.L100:
	bbci.l	a4, 0, .Lret
EX(11f)	l8ui	a9, a3, 0
	/* slot */
EX(10f)	s8i	a9, a11, 0
	beqz	a9, .Lret		# if byte is zero
	addi	a11, a11, 1-3		# advance dst ptr 1, but also cancel
					# the effect of adding 3 in .Lz3 code
	/* fall thru to .Lz3 and "retw" */

.Lz3:	# byte 3 is zero
	addi	a11, a11, 3		# advance dst pointer
	sub	a2, a11, a2		# compute strlen
	abi_ret_default
.Lz0:	# byte 0 is zero
#ifdef __XTENSA_EB__
	movi	a9, 0
#endif /* __XTENSA_EB__ */
EX(10f)	s8i	a9, a11, 0
	sub	a2, a11, a2		# compute strlen
	abi_ret_default
.Lz1:	# byte 1 is zero
#ifdef __XTENSA_EB__
	extui   a9, a9, 16, 16
#endif /* __XTENSA_EB__ */
EX(10f)	s16i	a9, a11, 0
	addi	a11, a11, 1		# advance dst pointer
	sub	a2, a11, a2		# compute strlen
	abi_ret_default
.Lz2:	# byte 2 is zero
#ifdef __XTENSA_EB__
	extui   a9, a9, 16, 16
#endif /* __XTENSA_EB__ */
EX(10f)	s16i	a9, a11, 0
	movi	a9, 0
EX(10f)	s8i	a9, a11, 2
	addi	a11, a11, 2		# advance dst pointer
	sub	a2, a11, a2		# compute strlen
	abi_ret_default

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
	add	a10, a11, a4		# a10 = ending address
#endif /* XCHAL_HAVE_LOOPS */
.Lnextbyte:
EX(11f)	l8ui	a9, a3, 0
	addi	a3, a3, 1
EX(10f)	s8i	a9, a11, 0
	beqz	a9, .Lunalignedend
	addi	a11, a11, 1
#if !XCHAL_HAVE_LOOPS
	blt	a11, a10, .Lnextbyte
#endif

.Lunalignedend:
	sub	a2, a11, a2		# compute strlen
	abi_ret_default

ENDPROC(__strncpy_user)
EXPORT_SYMBOL(__strncpy_user)

	.section .fixup, "ax"
	.align	4

	/* For now, just return -EFAULT.  Future implementations might
	 * like to clear remaining kernel space, like the fixup
	 * implementation in memset().  Thus, we differentiate between
	 * load/store fixups. */

10:
11:
	movi	a2, -EFAULT
	abi_ret_default

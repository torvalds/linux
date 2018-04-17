/*
 *  arch/xtensa/lib/usercopy.S
 *
 *  Copy to/from user space (derived from arch/xtensa/lib/hal/memcopy.S)
 *
 *  DO NOT COMBINE this function with <arch/xtensa/lib/hal/memcopy.S>.
 *  It needs to remain separate and distinct.  The hal files are part
 *  of the Xtensa link-time HAL, and those files may differ per
 *  processor configuration.  Patching the kernel for another
 *  processor configuration includes replacing the hal files, and we
 *  could lose the special functionality for accessing user-space
 *  memory during such a patch.  We sacrifice a little code space here
 *  in favor to simplify code maintenance.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file "COPYING" in the main directory of
 *  this archive for more details.
 *
 *  Copyright (C) 2002 Tensilica Inc.
 */


/*
 * size_t __xtensa_copy_user (void *dst, const void *src, size_t len);
 *
 * The returned value is the number of bytes not copied.  Implies zero
 * is success.
 *
 * The general case algorithm is as follows:
 *   If the destination and source are both aligned,
 *     do 16B chunks with a loop, and then finish up with
 *     8B, 4B, 2B, and 1B copies conditional on the length.
 *   If destination is aligned and source unaligned,
 *     do the same, but use SRC to align the source data.
 *   If destination is unaligned, align it by conditionally
 *     copying 1B and 2B and then retest.
 *   This code tries to use fall-through braches for the common
 *     case of aligned destinations (except for the branches to
 *     the alignment label).
 *
 * Register use:
 *	a0/ return address
 *	a1/ stack pointer
 *	a2/ return value
 *	a3/ src
 *	a4/ length
 *	a5/ dst
 *	a6/ tmp
 *	a7/ tmp
 *	a8/ tmp
 *	a9/ tmp
 *	a10/ tmp
 *	a11/ original length
 */

#include <linux/linkage.h>
#include <variant/core.h>
#include <asm/asmmacro.h>

	.text
ENTRY(__xtensa_copy_user)

	entry	sp, 16		# minimal stack frame
	# a2/ dst, a3/ src, a4/ len
	mov	a5, a2		# copy dst so that a2 is return value
	mov	a11, a4		# preserve original len for error case
.Lcommon:
	bbsi.l	a2, 0, .Ldst1mod2 # if dst is 1 mod 2
	bbsi.l	a2, 1, .Ldst2mod4 # if dst is 2 mod 4
.Ldstaligned:	# return here from .Ldstunaligned when dst is aligned
	srli	a7, a4, 4	# number of loop iterations with 16B
				# per iteration
	movi	a8, 3		  # if source is also aligned,
	bnone	a3, a8, .Laligned # then use word copy
	__ssa8	a3		# set shift amount from byte offset
	bnez	a4, .Lsrcunaligned
	movi	a2, 0		# return success for len==0
	retw

/*
 * Destination is unaligned
 */

.Ldst1mod2:	# dst is only byte aligned
	bltui	a4, 7, .Lbytecopy	# do short copies byte by byte

	# copy 1 byte
EX(10f)	l8ui	a6, a3, 0
	addi	a3, a3,  1
EX(10f)	s8i	a6, a5,  0
	addi	a5, a5,  1
	addi	a4, a4, -1
	bbci.l	a5, 1, .Ldstaligned	# if dst is now aligned, then
					# return to main algorithm
.Ldst2mod4:	# dst 16-bit aligned
	# copy 2 bytes
	bltui	a4, 6, .Lbytecopy	# do short copies byte by byte
EX(10f)	l8ui	a6, a3, 0
EX(10f)	l8ui	a7, a3, 1
	addi	a3, a3,  2
EX(10f)	s8i	a6, a5,  0
EX(10f)	s8i	a7, a5,  1
	addi	a5, a5,  2
	addi	a4, a4, -2
	j	.Ldstaligned	# dst is now aligned, return to main algorithm

/*
 * Byte by byte copy
 */
	.align	4
	.byte	0		# 1 mod 4 alignment for LOOPNEZ
				# (0 mod 4 alignment for LBEG)
.Lbytecopy:
#if XCHAL_HAVE_LOOPS
	loopnez	a4, .Lbytecopydone
#else /* !XCHAL_HAVE_LOOPS */
	beqz	a4, .Lbytecopydone
	add	a7, a3, a4	# a7 = end address for source
#endif /* !XCHAL_HAVE_LOOPS */
.Lnextbyte:
EX(10f)	l8ui	a6, a3, 0
	addi	a3, a3, 1
EX(10f)	s8i	a6, a5, 0
	addi	a5, a5, 1
#if !XCHAL_HAVE_LOOPS
	blt	a3, a7, .Lnextbyte
#endif /* !XCHAL_HAVE_LOOPS */
.Lbytecopydone:
	movi	a2, 0		# return success for len bytes copied
	retw

/*
 * Destination and source are word-aligned.
 */
	# copy 16 bytes per iteration for word-aligned dst and word-aligned src
	.align	4		# 1 mod 4 alignment for LOOPNEZ
	.byte	0		# (0 mod 4 alignment for LBEG)
.Laligned:
#if XCHAL_HAVE_LOOPS
	loopnez	a7, .Loop1done
#else /* !XCHAL_HAVE_LOOPS */
	beqz	a7, .Loop1done
	slli	a8, a7, 4
	add	a8, a8, a3	# a8 = end of last 16B source chunk
#endif /* !XCHAL_HAVE_LOOPS */
.Loop1:
EX(10f)	l32i	a6, a3,  0
EX(10f)	l32i	a7, a3,  4
EX(10f)	s32i	a6, a5,  0
EX(10f)	l32i	a6, a3,  8
EX(10f)	s32i	a7, a5,  4
EX(10f)	l32i	a7, a3, 12
EX(10f)	s32i	a6, a5,  8
	addi	a3, a3, 16
EX(10f)	s32i	a7, a5, 12
	addi	a5, a5, 16
#if !XCHAL_HAVE_LOOPS
	blt	a3, a8, .Loop1
#endif /* !XCHAL_HAVE_LOOPS */
.Loop1done:
	bbci.l	a4, 3, .L2
	# copy 8 bytes
EX(10f)	l32i	a6, a3,  0
EX(10f)	l32i	a7, a3,  4
	addi	a3, a3,  8
EX(10f)	s32i	a6, a5,  0
EX(10f)	s32i	a7, a5,  4
	addi	a5, a5,  8
.L2:
	bbci.l	a4, 2, .L3
	# copy 4 bytes
EX(10f)	l32i	a6, a3,  0
	addi	a3, a3,  4
EX(10f)	s32i	a6, a5,  0
	addi	a5, a5,  4
.L3:
	bbci.l	a4, 1, .L4
	# copy 2 bytes
EX(10f)	l16ui	a6, a3,  0
	addi	a3, a3,  2
EX(10f)	s16i	a6, a5,  0
	addi	a5, a5,  2
.L4:
	bbci.l	a4, 0, .L5
	# copy 1 byte
EX(10f)	l8ui	a6, a3,  0
EX(10f)	s8i	a6, a5,  0
.L5:
	movi	a2, 0		# return success for len bytes copied
	retw

/*
 * Destination is aligned, Source is unaligned
 */

	.align	4
	.byte	0		# 1 mod 4 alignement for LOOPNEZ
				# (0 mod 4 alignment for LBEG)
.Lsrcunaligned:
	# copy 16 bytes per iteration for word-aligned dst and unaligned src
	and	a10, a3, a8	# save unalignment offset for below
	sub	a3, a3, a10	# align a3 (to avoid sim warnings only; not needed for hardware)
EX(10f)	l32i	a6, a3, 0	# load first word
#if XCHAL_HAVE_LOOPS
	loopnez	a7, .Loop2done
#else /* !XCHAL_HAVE_LOOPS */
	beqz	a7, .Loop2done
	slli	a12, a7, 4
	add	a12, a12, a3	# a12 = end of last 16B source chunk
#endif /* !XCHAL_HAVE_LOOPS */
.Loop2:
EX(10f)	l32i	a7, a3,  4
EX(10f)	l32i	a8, a3,  8
	__src_b	a6, a6, a7
EX(10f)	s32i	a6, a5,  0
EX(10f)	l32i	a9, a3, 12
	__src_b	a7, a7, a8
EX(10f)	s32i	a7, a5,  4
EX(10f)	l32i	a6, a3, 16
	__src_b	a8, a8, a9
EX(10f)	s32i	a8, a5,  8
	addi	a3, a3, 16
	__src_b	a9, a9, a6
EX(10f)	s32i	a9, a5, 12
	addi	a5, a5, 16
#if !XCHAL_HAVE_LOOPS
	blt	a3, a12, .Loop2
#endif /* !XCHAL_HAVE_LOOPS */
.Loop2done:
	bbci.l	a4, 3, .L12
	# copy 8 bytes
EX(10f)	l32i	a7, a3,  4
EX(10f)	l32i	a8, a3,  8
	__src_b	a6, a6, a7
EX(10f)	s32i	a6, a5,  0
	addi	a3, a3,  8
	__src_b	a7, a7, a8
EX(10f)	s32i	a7, a5,  4
	addi	a5, a5,  8
	mov	a6, a8
.L12:
	bbci.l	a4, 2, .L13
	# copy 4 bytes
EX(10f)	l32i	a7, a3,  4
	addi	a3, a3,  4
	__src_b	a6, a6, a7
EX(10f)	s32i	a6, a5,  0
	addi	a5, a5,  4
	mov	a6, a7
.L13:
	add	a3, a3, a10	# readjust a3 with correct misalignment
	bbci.l	a4, 1, .L14
	# copy 2 bytes
EX(10f)	l8ui	a6, a3,  0
EX(10f)	l8ui	a7, a3,  1
	addi	a3, a3,  2
EX(10f)	s8i	a6, a5,  0
EX(10f)	s8i	a7, a5,  1
	addi	a5, a5,  2
.L14:
	bbci.l	a4, 0, .L15
	# copy 1 byte
EX(10f)	l8ui	a6, a3,  0
EX(10f)	s8i	a6, a5,  0
.L15:
	movi	a2, 0		# return success for len bytes copied
	retw

ENDPROC(__xtensa_copy_user)

	.section .fixup, "ax"
	.align	4

/* a2 = original dst; a5 = current dst; a11= original len
 * bytes_copied = a5 - a2
 * retval = bytes_not_copied = original len - bytes_copied
 * retval = a11 - (a5 - a2)
 */


10:
	sub	a2, a5, a2	/* a2 <-- bytes copied */
	sub	a2, a11, a2	/* a2 <-- bytes not copied */
	retw

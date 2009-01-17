/*
 * arch/xtensa/lib/hal/memcopy.S -- Core HAL library functions
 * xthal_memcpy and xthal_bcopy
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002 - 2005 Tensilica Inc.
 */

#include <variant/core.h>

	.macro	src_b	r, w0, w1
#ifdef __XTENSA_EB__
	src	\r, \w0, \w1
#else
	src	\r, \w1, \w0
#endif
	.endm

	.macro	ssa8	r
#ifdef __XTENSA_EB__
	ssa8b	\r
#else
	ssa8l	\r
#endif
	.endm


/*
 * void *memcpy(void *dst, const void *src, size_t len);
 * void *memmove(void *dst, const void *src, size_t len);
 * void *bcopy(const void *src, void *dst, size_t len);
 *
 * This function is intended to do the same thing as the standard
 * library function memcpy() (or bcopy()) for most cases.
 * However, where the source and/or destination references
 * an instruction RAM or ROM or a data RAM or ROM, that
 * source and/or destination will always be accessed with
 * 32-bit load and store instructions (as required for these
 * types of devices).
 *
 * !!!!!!!  XTFIXME:
 * !!!!!!!  Handling of IRAM/IROM has not yet
 * !!!!!!!  been implemented.
 *
 * The bcopy version is provided here to avoid the overhead
 * of an extra call, for callers that require this convention.
 *
 * The (general case) algorithm is as follows:
 *   If destination is unaligned, align it by conditionally
 *     copying 1 and 2 bytes.
 *   If source is aligned,
 *     do 16 bytes with a loop, and then finish up with
 *     8, 4, 2, and 1 byte copies conditional on the length;
 *   else (if source is unaligned),
 *     do the same, but use SRC to align the source data.
 *   This code tries to use fall-through branches for the common
 *     case of aligned source and destination and multiple
 *     of 4 (or 8) length.
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
 *	a11/ tmp
 */

	.text
	.align	4
	.global	bcopy
	.type   bcopy,@function
bcopy:
	entry	sp, 16		# minimal stack frame
	# a2=src, a3=dst, a4=len
	mov	a5, a3		# copy dst so that a2 is return value
	mov	a3, a2
	mov	a2, a5
	j	.Lcommon	# go to common code for memcpy+bcopy


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
	l8ui	a6, a3, 0
	addi	a3, a3, 1
	s8i	a6, a5, 0
	addi	a5, a5, 1
#if !XCHAL_HAVE_LOOPS
	blt	a3, a7, .Lnextbyte
#endif /* !XCHAL_HAVE_LOOPS */
.Lbytecopydone:
	retw

/*
 * Destination is unaligned
 */

	.align	4
.Ldst1mod2:	# dst is only byte aligned
	_bltui	a4, 7, .Lbytecopy	# do short copies byte by byte

	# copy 1 byte
	l8ui	a6, a3,  0
	addi	a3, a3,  1
	addi	a4, a4, -1
	s8i	a6, a5,  0
	addi	a5, a5,  1
	_bbci.l	a5, 1, .Ldstaligned	# if dst is now aligned, then
					# return to main algorithm
.Ldst2mod4:	# dst 16-bit aligned
	# copy 2 bytes
	_bltui	a4, 6, .Lbytecopy	# do short copies byte by byte
	l8ui	a6, a3,  0
	l8ui	a7, a3,  1
	addi	a3, a3,  2
	addi	a4, a4, -2
	s8i	a6, a5,  0
	s8i	a7, a5,  1
	addi	a5, a5,  2
	j	.Ldstaligned	# dst is now aligned, return to main algorithm

	.align	4
	.global	memcpy
	.type   memcpy,@function
memcpy:
	.global	memmove
	.type   memmove,@function
memmove:

	entry	sp, 16		# minimal stack frame
	# a2/ dst, a3/ src, a4/ len
	mov	a5, a2		# copy dst so that a2 is return value
.Lcommon:
	_bbsi.l	a2, 0, .Ldst1mod2	# if dst is 1 mod 2
	_bbsi.l	a2, 1, .Ldst2mod4	# if dst is 2 mod 4
.Ldstaligned:	# return here from .Ldst?mod? once dst is aligned
	srli	a7, a4, 4	# number of loop iterations with 16B
				# per iteration
	movi	a8, 3		# if source is not aligned,
	_bany	a3, a8, .Lsrcunaligned	# then use shifting copy
	/*
	 * Destination and source are word-aligned, use word copy.
	 */
	# copy 16 bytes per iteration for word-aligned dst and word-aligned src
#if XCHAL_HAVE_LOOPS
	loopnez	a7, .Loop1done
#else /* !XCHAL_HAVE_LOOPS */
	beqz	a7, .Loop1done
	slli	a8, a7, 4
	add	a8, a8, a3	# a8 = end of last 16B source chunk
#endif /* !XCHAL_HAVE_LOOPS */
.Loop1:
	l32i	a6, a3,  0
	l32i	a7, a3,  4
	s32i	a6, a5,  0
	l32i	a6, a3,  8
	s32i	a7, a5,  4
	l32i	a7, a3, 12
	s32i	a6, a5,  8
	addi	a3, a3, 16
	s32i	a7, a5, 12
	addi	a5, a5, 16
#if !XCHAL_HAVE_LOOPS
	blt	a3, a8, .Loop1
#endif /* !XCHAL_HAVE_LOOPS */
.Loop1done:
	bbci.l	a4, 3, .L2
	# copy 8 bytes
	l32i	a6, a3,  0
	l32i	a7, a3,  4
	addi	a3, a3,  8
	s32i	a6, a5,  0
	s32i	a7, a5,  4
	addi	a5, a5,  8
.L2:
	bbsi.l	a4, 2, .L3
	bbsi.l	a4, 1, .L4
	bbsi.l	a4, 0, .L5
	retw
.L3:
	# copy 4 bytes
	l32i	a6, a3,  0
	addi	a3, a3,  4
	s32i	a6, a5,  0
	addi	a5, a5,  4
	bbsi.l	a4, 1, .L4
	bbsi.l	a4, 0, .L5
	retw
.L4:
	# copy 2 bytes
	l16ui	a6, a3,  0
	addi	a3, a3,  2
	s16i	a6, a5,  0
	addi	a5, a5,  2
	bbsi.l	a4, 0, .L5
	retw
.L5:
	# copy 1 byte
	l8ui	a6, a3,  0
	s8i	a6, a5,  0
	retw

/*
 * Destination is aligned, Source is unaligned
 */

	.align	4
.Lsrcunaligned:
	_beqz	a4, .Ldone	# avoid loading anything for zero-length copies
	# copy 16 bytes per iteration for word-aligned dst and unaligned src
	ssa8	a3		# set shift amount from byte offset
#define SIM_CHECKS_ALIGNMENT	1	/* set to 1 when running on ISS (simulator) with the
					   lint or ferret client, or 0 to save a few cycles */
#if XCHAL_UNALIGNED_LOAD_EXCEPTION || SIM_CHECKS_ALIGNMENT
	and	a11, a3, a8	# save unalignment offset for below
	sub	a3, a3, a11	# align a3
#endif
	l32i	a6, a3, 0	# load first word
#if XCHAL_HAVE_LOOPS
	loopnez	a7, .Loop2done
#else /* !XCHAL_HAVE_LOOPS */
	beqz	a7, .Loop2done
	slli	a10, a7, 4
	add	a10, a10, a3	# a10 = end of last 16B source chunk
#endif /* !XCHAL_HAVE_LOOPS */
.Loop2:
	l32i	a7, a3,  4
	l32i	a8, a3,  8
	src_b	a6, a6, a7
	s32i	a6, a5,  0
	l32i	a9, a3, 12
	src_b	a7, a7, a8
	s32i	a7, a5,  4
	l32i	a6, a3, 16
	src_b	a8, a8, a9
	s32i	a8, a5,  8
	addi	a3, a3, 16
	src_b	a9, a9, a6
	s32i	a9, a5, 12
	addi	a5, a5, 16
#if !XCHAL_HAVE_LOOPS
	blt	a3, a10, .Loop2
#endif /* !XCHAL_HAVE_LOOPS */
.Loop2done:
	bbci.l	a4, 3, .L12
	# copy 8 bytes
	l32i	a7, a3,  4
	l32i	a8, a3,  8
	src_b	a6, a6, a7
	s32i	a6, a5,  0
	addi	a3, a3,  8
	src_b	a7, a7, a8
	s32i	a7, a5,  4
	addi	a5, a5,  8
	mov	a6, a8
.L12:
	bbci.l	a4, 2, .L13
	# copy 4 bytes
	l32i	a7, a3,  4
	addi	a3, a3,  4
	src_b	a6, a6, a7
	s32i	a6, a5,  0
	addi	a5, a5,  4
	mov	a6, a7
.L13:
#if XCHAL_UNALIGNED_LOAD_EXCEPTION || SIM_CHECKS_ALIGNMENT
	add	a3, a3, a11	# readjust a3 with correct misalignment
#endif
	bbsi.l	a4, 1, .L14
	bbsi.l	a4, 0, .L15
.Ldone:	retw
.L14:
	# copy 2 bytes
	l8ui	a6, a3,  0
	l8ui	a7, a3,  1
	addi	a3, a3,  2
	s8i	a6, a5,  0
	s8i	a7, a5,  1
	addi	a5, a5,  2
	bbsi.l	a4, 0, .L15
	retw
.L15:
	# copy 1 byte
	l8ui	a6, a3,  0
	s8i	a6, a5,  0
	retw

/*
 * Local Variables:
 * mode:fundamental
 * comment-start: "# "
 * comment-start-skip: "# *"
 * End:
 */

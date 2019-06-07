/*
 * arch/xtensa/lib/hal/memcopy.S -- Core HAL library functions
 * xthal_memcpy and xthal_bcopy
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002 - 2012 Tensilica Inc.
 */

#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

/*
 * void *memcpy(void *dst, const void *src, size_t len);
 *
 * This function is intended to do the same thing as the standard
 * library function memcpy() for most cases.
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
	bne	a3, a7, .Lnextbyte # continue loop if $a3:src != $a7:src_end
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

ENTRY(__memcpy)
WEAK(memcpy)

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
	bne	a3, a8, .Loop1  # continue loop if a3:src != a8:src_end
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
	__ssa8	a3		# set shift amount from byte offset

/* set to 1 when running on ISS (simulator) with the
   lint or ferret client, or 0 to save a few cycles */
#define SIM_CHECKS_ALIGNMENT	1
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
	__src_b	a6, a6, a7
	s32i	a6, a5,  0
	l32i	a9, a3, 12
	__src_b	a7, a7, a8
	s32i	a7, a5,  4
	l32i	a6, a3, 16
	__src_b	a8, a8, a9
	s32i	a8, a5,  8
	addi	a3, a3, 16
	__src_b	a9, a9, a6
	s32i	a9, a5, 12
	addi	a5, a5, 16
#if !XCHAL_HAVE_LOOPS
	bne	a3, a10, .Loop2 # continue loop if a3:src != a10:src_end
#endif /* !XCHAL_HAVE_LOOPS */
.Loop2done:
	bbci.l	a4, 3, .L12
	# copy 8 bytes
	l32i	a7, a3,  4
	l32i	a8, a3,  8
	__src_b	a6, a6, a7
	s32i	a6, a5,  0
	addi	a3, a3,  8
	__src_b	a7, a7, a8
	s32i	a7, a5,  4
	addi	a5, a5,  8
	mov	a6, a8
.L12:
	bbci.l	a4, 2, .L13
	# copy 4 bytes
	l32i	a7, a3,  4
	addi	a3, a3,  4
	__src_b	a6, a6, a7
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

ENDPROC(__memcpy)

/*
 * void bcopy(const void *src, void *dest, size_t n);
 */

ENTRY(bcopy)

	entry	sp, 16		# minimal stack frame
	# a2=src, a3=dst, a4=len
	mov	a5, a3
	mov	a3, a2
	mov	a2, a5
	j	.Lmovecommon	# go to common code for memmove+bcopy

ENDPROC(bcopy)

/*
 * void *memmove(void *dst, const void *src, size_t len);
 *
 * This function is intended to do the same thing as the standard
 * library function memmove() for most cases.
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
 * The (general case) algorithm is as follows:
 *   If end of source doesn't overlap destination then use memcpy.
 *   Otherwise do memcpy backwards.
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

/*
 * Byte by byte copy
 */
	.align	4
	.byte	0		# 1 mod 4 alignment for LOOPNEZ
				# (0 mod 4 alignment for LBEG)
.Lbackbytecopy:
#if XCHAL_HAVE_LOOPS
	loopnez	a4, .Lbackbytecopydone
#else /* !XCHAL_HAVE_LOOPS */
	beqz	a4, .Lbackbytecopydone
	sub	a7, a3, a4	# a7 = start address for source
#endif /* !XCHAL_HAVE_LOOPS */
.Lbacknextbyte:
	addi	a3, a3, -1
	l8ui	a6, a3, 0
	addi	a5, a5, -1
	s8i	a6, a5, 0
#if !XCHAL_HAVE_LOOPS
	bne	a3, a7, .Lbacknextbyte # continue loop if
				       # $a3:src != $a7:src_start
#endif /* !XCHAL_HAVE_LOOPS */
.Lbackbytecopydone:
	retw

/*
 * Destination is unaligned
 */

	.align	4
.Lbackdst1mod2:	# dst is only byte aligned
	_bltui	a4, 7, .Lbackbytecopy	# do short copies byte by byte

	# copy 1 byte
	addi	a3, a3, -1
	l8ui	a6, a3,  0
	addi	a5, a5, -1
	s8i	a6, a5,  0
	addi	a4, a4, -1
	_bbci.l	a5, 1, .Lbackdstaligned	# if dst is now aligned, then
					# return to main algorithm
.Lbackdst2mod4:	# dst 16-bit aligned
	# copy 2 bytes
	_bltui	a4, 6, .Lbackbytecopy	# do short copies byte by byte
	addi	a3, a3, -2
	l8ui	a6, a3,  0
	l8ui	a7, a3,  1
	addi	a5, a5, -2
	s8i	a6, a5,  0
	s8i	a7, a5,  1
	addi	a4, a4, -2
	j	.Lbackdstaligned	# dst is now aligned,
					# return to main algorithm

ENTRY(__memmove)
WEAK(memmove)

	entry	sp, 16		# minimal stack frame
	# a2/ dst, a3/ src, a4/ len
	mov	a5, a2		# copy dst so that a2 is return value
.Lmovecommon:
	sub	a6, a5, a3
	bgeu	a6, a4, .Lcommon

	add	a5, a5, a4
	add	a3, a3, a4

	_bbsi.l	a5, 0, .Lbackdst1mod2	# if dst is 1 mod 2
	_bbsi.l	a5, 1, .Lbackdst2mod4	# if dst is 2 mod 4
.Lbackdstaligned:	# return here from .Lbackdst?mod? once dst is aligned
	srli	a7, a4, 4	# number of loop iterations with 16B
				# per iteration
	movi	a8, 3		# if source is not aligned,
	_bany	a3, a8, .Lbacksrcunaligned	# then use shifting copy
	/*
	 * Destination and source are word-aligned, use word copy.
	 */
	# copy 16 bytes per iteration for word-aligned dst and word-aligned src
#if XCHAL_HAVE_LOOPS
	loopnez	a7, .backLoop1done
#else /* !XCHAL_HAVE_LOOPS */
	beqz	a7, .backLoop1done
	slli	a8, a7, 4
	sub	a8, a3, a8	# a8 = start of first 16B source chunk
#endif /* !XCHAL_HAVE_LOOPS */
.backLoop1:
	addi	a3, a3, -16
	l32i	a7, a3, 12
	l32i	a6, a3,  8
	addi	a5, a5, -16
	s32i	a7, a5, 12
	l32i	a7, a3,  4
	s32i	a6, a5,  8
	l32i	a6, a3,  0
	s32i	a7, a5,  4
	s32i	a6, a5,  0
#if !XCHAL_HAVE_LOOPS
	bne	a3, a8, .backLoop1  # continue loop if a3:src != a8:src_start
#endif /* !XCHAL_HAVE_LOOPS */
.backLoop1done:
	bbci.l	a4, 3, .Lback2
	# copy 8 bytes
	addi	a3, a3, -8
	l32i	a6, a3,  0
	l32i	a7, a3,  4
	addi	a5, a5, -8
	s32i	a6, a5,  0
	s32i	a7, a5,  4
.Lback2:
	bbsi.l	a4, 2, .Lback3
	bbsi.l	a4, 1, .Lback4
	bbsi.l	a4, 0, .Lback5
	retw
.Lback3:
	# copy 4 bytes
	addi	a3, a3, -4
	l32i	a6, a3,  0
	addi	a5, a5, -4
	s32i	a6, a5,  0
	bbsi.l	a4, 1, .Lback4
	bbsi.l	a4, 0, .Lback5
	retw
.Lback4:
	# copy 2 bytes
	addi	a3, a3, -2
	l16ui	a6, a3,  0
	addi	a5, a5, -2
	s16i	a6, a5,  0
	bbsi.l	a4, 0, .Lback5
	retw
.Lback5:
	# copy 1 byte
	addi	a3, a3, -1
	l8ui	a6, a3,  0
	addi	a5, a5, -1
	s8i	a6, a5,  0
	retw

/*
 * Destination is aligned, Source is unaligned
 */

	.align	4
.Lbacksrcunaligned:
	_beqz	a4, .Lbackdone	# avoid loading anything for zero-length copies
	# copy 16 bytes per iteration for word-aligned dst and unaligned src
	__ssa8	a3		# set shift amount from byte offset
#define SIM_CHECKS_ALIGNMENT	1	/* set to 1 when running on ISS with
					 * the lint or ferret client, or 0
					 * to save a few cycles */
#if XCHAL_UNALIGNED_LOAD_EXCEPTION || SIM_CHECKS_ALIGNMENT
	and	a11, a3, a8	# save unalignment offset for below
	sub	a3, a3, a11	# align a3
#endif
	l32i	a6, a3, 0	# load first word
#if XCHAL_HAVE_LOOPS
	loopnez	a7, .backLoop2done
#else /* !XCHAL_HAVE_LOOPS */
	beqz	a7, .backLoop2done
	slli	a10, a7, 4
	sub	a10, a3, a10	# a10 = start of first 16B source chunk
#endif /* !XCHAL_HAVE_LOOPS */
.backLoop2:
	addi	a3, a3, -16
	l32i	a7, a3, 12
	l32i	a8, a3,  8
	addi	a5, a5, -16
	__src_b	a6, a7, a6
	s32i	a6, a5, 12
	l32i	a9, a3,  4
	__src_b	a7, a8, a7
	s32i	a7, a5,  8
	l32i	a6, a3,  0
	__src_b	a8, a9, a8
	s32i	a8, a5,  4
	__src_b	a9, a6, a9
	s32i	a9, a5,  0
#if !XCHAL_HAVE_LOOPS
	bne	a3, a10, .backLoop2 # continue loop if a3:src != a10:src_start
#endif /* !XCHAL_HAVE_LOOPS */
.backLoop2done:
	bbci.l	a4, 3, .Lback12
	# copy 8 bytes
	addi	a3, a3, -8
	l32i	a7, a3,  4
	l32i	a8, a3,  0
	addi	a5, a5, -8
	__src_b	a6, a7, a6
	s32i	a6, a5,  4
	__src_b	a7, a8, a7
	s32i	a7, a5,  0
	mov	a6, a8
.Lback12:
	bbci.l	a4, 2, .Lback13
	# copy 4 bytes
	addi	a3, a3, -4
	l32i	a7, a3,  0
	addi	a5, a5, -4
	__src_b	a6, a7, a6
	s32i	a6, a5,  0
	mov	a6, a7
.Lback13:
#if XCHAL_UNALIGNED_LOAD_EXCEPTION || SIM_CHECKS_ALIGNMENT
	add	a3, a3, a11	# readjust a3 with correct misalignment
#endif
	bbsi.l	a4, 1, .Lback14
	bbsi.l	a4, 0, .Lback15
.Lbackdone:
	retw
.Lback14:
	# copy 2 bytes
	addi	a3, a3, -2
	l8ui	a6, a3,  0
	l8ui	a7, a3,  1
	addi	a5, a5, -2
	s8i	a6, a5,  0
	s8i	a7, a5,  1
	bbsi.l	a4, 0, .Lback15
	retw
.Lback15:
	# copy 1 byte
	addi	a3, a3, -1
	addi	a5, a5, -1
	l8ui	a6, a3,  0
	s8i	a6, a5,  0
	retw

ENDPROC(__memmove)

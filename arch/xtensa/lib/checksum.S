/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines
 *
 * Xtensa version:  Copyright (C) 2001 Tensilica, Inc. by Kevin Chea
 *                  Optimized by Joe Taylor
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <variant/core.h>
#include <asm/asmmacro.h>

/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */

/*
 * unsigned int csum_partial(const unsigned char *buf, int len,
 *                           unsigned int sum);
 *    a2 = buf
 *    a3 = len
 *    a4 = sum
 *
 * This function assumes 2- or 4-byte alignment.  Other alignments will fail!
 */

/* ONES_ADD converts twos-complement math to ones-complement. */
#define ONES_ADD(sum, val)	  \
	add	sum, sum, val	; \
	bgeu	sum, val, 99f	; \
	addi	sum, sum, 1	; \
99:				;

.text
ENTRY(csum_partial)

	/*
	 * Experiments with Ethernet and SLIP connections show that buf
	 * is aligned on either a 2-byte or 4-byte boundary.
	 */
	entry	sp, 32
	extui	a5, a2, 0, 2
	bnez	a5, 8f		/* branch if 2-byte aligned */
	/* Fall-through on common case, 4-byte alignment */
1:
	srli	a5, a3, 5	/* 32-byte chunks */
#if XCHAL_HAVE_LOOPS
	loopgtz	a5, 2f
#else
	beqz	a5, 2f
	slli	a5, a5, 5
	add	a5, a5, a2	/* a5 = end of last 32-byte chunk */
.Loop1:
#endif
	l32i	a6, a2, 0
	l32i	a7, a2, 4
	ONES_ADD(a4, a6)
	ONES_ADD(a4, a7)
	l32i	a6, a2, 8
	l32i	a7, a2, 12
	ONES_ADD(a4, a6)
	ONES_ADD(a4, a7)
	l32i	a6, a2, 16
	l32i	a7, a2, 20
	ONES_ADD(a4, a6)
	ONES_ADD(a4, a7)
	l32i	a6, a2, 24
	l32i	a7, a2, 28
	ONES_ADD(a4, a6)
	ONES_ADD(a4, a7)
	addi	a2, a2, 4*8
#if !XCHAL_HAVE_LOOPS
	blt	a2, a5, .Loop1
#endif
2:
	extui	a5, a3, 2, 3	/* remaining 4-byte chunks */
#if XCHAL_HAVE_LOOPS
	loopgtz	a5, 3f
#else
	beqz	a5, 3f
	slli	a5, a5, 2
	add	a5, a5, a2	/* a5 = end of last 4-byte chunk */
.Loop2:
#endif
	l32i	a6, a2, 0
	ONES_ADD(a4, a6)
	addi	a2, a2, 4
#if !XCHAL_HAVE_LOOPS
	blt	a2, a5, .Loop2
#endif
3:
	_bbci.l	a3, 1, 5f	/* remaining 2-byte chunk */
	l16ui	a6, a2, 0
	ONES_ADD(a4, a6)
	addi	a2, a2, 2
5:
	_bbci.l	a3, 0, 7f	/* remaining 1-byte chunk */
6:	l8ui	a6, a2, 0
#ifdef __XTENSA_EB__
	slli	a6, a6, 8	/* load byte into bits 8..15 */
#endif
	ONES_ADD(a4, a6)
7:
	mov	a2, a4
	retw

	/* uncommon case, buf is 2-byte aligned */
8:
	beqz	a3, 7b		/* branch if len == 0 */
	beqi	a3, 1, 6b	/* branch if len == 1 */

	extui	a5, a2, 0, 1
	bnez	a5, 8f		/* branch if 1-byte aligned */

	l16ui	a6, a2, 0	/* common case, len >= 2 */
	ONES_ADD(a4, a6)
	addi	a2, a2, 2	/* adjust buf */
	addi	a3, a3, -2	/* adjust len */
	j	1b		/* now buf is 4-byte aligned */

	/* case: odd-byte aligned, len > 1
	 * This case is dog slow, so don't give us an odd address.
	 * (I don't think this ever happens, but just in case.)
	 */
8:
	srli	a5, a3, 2	/* 4-byte chunks */
#if XCHAL_HAVE_LOOPS
	loopgtz	a5, 2f
#else
	beqz	a5, 2f
	slli	a5, a5, 2
	add	a5, a5, a2	/* a5 = end of last 4-byte chunk */
.Loop3:
#endif
	l8ui	a6, a2, 0	/* bits 24..31 */
	l16ui	a7, a2, 1	/* bits  8..23 */
	l8ui	a8, a2, 3	/* bits  0.. 8 */
#ifdef	__XTENSA_EB__
	slli	a6, a6, 24
#else
	slli	a8, a8, 24
#endif
	slli	a7, a7, 8
	or	a7, a7, a6
	or	a7, a7, a8
	ONES_ADD(a4, a7)
	addi	a2, a2, 4
#if !XCHAL_HAVE_LOOPS
	blt	a2, a5, .Loop3
#endif
2:
	_bbci.l	a3, 1, 3f	/* remaining 2-byte chunk, still odd addr */
	l8ui	a6, a2, 0
	l8ui	a7, a2, 1
#ifdef	__XTENSA_EB__
	slli	a6, a6, 8
#else
	slli	a7, a7, 8
#endif
	or	a7, a7, a6
	ONES_ADD(a4, a7)
	addi	a2, a2, 2
3:
	j	5b		/* branch to handle the remaining byte */

ENDPROC(csum_partial)

/*
 * Copy from ds while checksumming, otherwise like csum_partial
 */

/*
unsigned int csum_partial_copy_generic (const char *src, char *dst, int len,
					int sum, int *src_err_ptr, int *dst_err_ptr)
	a2  = src
	a3  = dst
	a4  = len
	a5  = sum
	a6  = src_err_ptr
	a7  = dst_err_ptr
	a8  = temp
	a9  = temp
	a10 = temp
	a11 = original len for exception handling
	a12 = original dst for exception handling

    This function is optimized for 4-byte aligned addresses.  Other
    alignments work, but not nearly as efficiently.
 */

ENTRY(csum_partial_copy_generic)

	entry	sp, 32
	mov	a12, a3
	mov	a11, a4
	or	a10, a2, a3

	/* We optimize the following alignment tests for the 4-byte
	aligned case.  Two bbsi.l instructions might seem more optimal
	(commented out below).  However, both labels 5: and 3: are out
	of the imm8 range, so the assembler relaxes them into
	equivalent bbci.l, j combinations, which is actually
	slower. */

	extui	a9, a10, 0, 2
	beqz	a9, 1f		/* branch if both are 4-byte aligned */
	bbsi.l	a10, 0, 5f	/* branch if one address is odd */
	j	3f		/* one address is 2-byte aligned */

/*	_bbsi.l	a10, 0, 5f */	/* branch if odd address */
/*	_bbsi.l	a10, 1, 3f */	/* branch if 2-byte-aligned address */

1:
	/* src and dst are both 4-byte aligned */
	srli	a10, a4, 5	/* 32-byte chunks */
#if XCHAL_HAVE_LOOPS
	loopgtz	a10, 2f
#else
	beqz	a10, 2f
	slli	a10, a10, 5
	add	a10, a10, a2	/* a10 = end of last 32-byte src chunk */
.Loop5:
#endif
EX(10f)	l32i	a9, a2, 0
EX(10f)	l32i	a8, a2, 4
EX(11f)	s32i	a9, a3, 0
EX(11f)	s32i	a8, a3, 4
	ONES_ADD(a5, a9)
	ONES_ADD(a5, a8)
EX(10f)	l32i	a9, a2, 8
EX(10f)	l32i	a8, a2, 12
EX(11f)	s32i	a9, a3, 8
EX(11f)	s32i	a8, a3, 12
	ONES_ADD(a5, a9)
	ONES_ADD(a5, a8)
EX(10f)	l32i	a9, a2, 16
EX(10f)	l32i	a8, a2, 20
EX(11f)	s32i	a9, a3, 16
EX(11f)	s32i	a8, a3, 20
	ONES_ADD(a5, a9)
	ONES_ADD(a5, a8)
EX(10f)	l32i	a9, a2, 24
EX(10f)	l32i	a8, a2, 28
EX(11f)	s32i	a9, a3, 24
EX(11f)	s32i	a8, a3, 28
	ONES_ADD(a5, a9)
	ONES_ADD(a5, a8)
	addi	a2, a2, 32
	addi	a3, a3, 32
#if !XCHAL_HAVE_LOOPS
	blt	a2, a10, .Loop5
#endif
2:
	extui	a10, a4, 2, 3	/* remaining 4-byte chunks */
	extui	a4, a4, 0, 2	/* reset len for general-case, 2-byte chunks */
#if XCHAL_HAVE_LOOPS
	loopgtz	a10, 3f
#else
	beqz	a10, 3f
	slli	a10, a10, 2
	add	a10, a10, a2	/* a10 = end of last 4-byte src chunk */
.Loop6:
#endif
EX(10f)	l32i	a9, a2, 0
EX(11f)	s32i	a9, a3, 0
	ONES_ADD(a5, a9)
	addi	a2, a2, 4
	addi	a3, a3, 4
#if !XCHAL_HAVE_LOOPS
	blt	a2, a10, .Loop6
#endif
3:
	/*
	Control comes to here in two cases: (1) It may fall through
	to here from the 4-byte alignment case to process, at most,
	one 2-byte chunk.  (2) It branches to here from above if
	either src or dst is 2-byte aligned, and we process all bytes
	here, except for perhaps a trailing odd byte.  It's
	inefficient, so align your addresses to 4-byte boundaries.

	a2 = src
	a3 = dst
	a4 = len
	a5 = sum
	*/
	srli	a10, a4, 1	/* 2-byte chunks */
#if XCHAL_HAVE_LOOPS
	loopgtz	a10, 4f
#else
	beqz	a10, 4f
	slli	a10, a10, 1
	add	a10, a10, a2	/* a10 = end of last 2-byte src chunk */
.Loop7:
#endif
EX(10f)	l16ui	a9, a2, 0
EX(11f)	s16i	a9, a3, 0
	ONES_ADD(a5, a9)
	addi	a2, a2, 2
	addi	a3, a3, 2
#if !XCHAL_HAVE_LOOPS
	blt	a2, a10, .Loop7
#endif
4:
	/* This section processes a possible trailing odd byte. */
	_bbci.l	a4, 0, 8f	/* 1-byte chunk */
EX(10f)	l8ui	a9, a2, 0
EX(11f)	s8i	a9, a3, 0
#ifdef __XTENSA_EB__
	slli	a9, a9, 8	/* shift byte to bits 8..15 */
#endif
	ONES_ADD(a5, a9)
8:
	mov	a2, a5
	retw

5:
	/* Control branch to here when either src or dst is odd.  We
	process all bytes using 8-bit accesses.  Grossly inefficient,
	so don't feed us an odd address. */

	srli	a10, a4, 1	/* handle in pairs for 16-bit csum */
#if XCHAL_HAVE_LOOPS
	loopgtz	a10, 6f
#else
	beqz	a10, 6f
	slli	a10, a10, 1
	add	a10, a10, a2	/* a10 = end of last odd-aligned, 2-byte src chunk */
.Loop8:
#endif
EX(10f)	l8ui	a9, a2, 0
EX(10f)	l8ui	a8, a2, 1
EX(11f)	s8i	a9, a3, 0
EX(11f)	s8i	a8, a3, 1
#ifdef __XTENSA_EB__
	slli	a9, a9, 8	/* combine into a single 16-bit value */
#else				/* for checksum computation */
	slli	a8, a8, 8
#endif
	or	a9, a9, a8
	ONES_ADD(a5, a9)
	addi	a2, a2, 2
	addi	a3, a3, 2
#if !XCHAL_HAVE_LOOPS
	blt	a2, a10, .Loop8
#endif
6:
	j	4b		/* process the possible trailing odd byte */

ENDPROC(csum_partial_copy_generic)


# Exception handler:
.section .fixup, "ax"
/*
	a6  = src_err_ptr
	a7  = dst_err_ptr
	a11 = original len for exception handling
	a12 = original dst for exception handling
*/

10:
	_movi	a2, -EFAULT
	s32i	a2, a6, 0	/* src_err_ptr */

	# clear the complete destination - computing the rest
	# is too much work
	movi	a2, 0
#if XCHAL_HAVE_LOOPS
	loopgtz	a11, 2f
#else
	beqz	a11, 2f
	add	a11, a11, a12	/* a11 = ending address */
.Leloop:
#endif
	s8i	a2, a12, 0
	addi	a12, a12, 1
#if !XCHAL_HAVE_LOOPS
	blt	a12, a11, .Leloop
#endif
2:
	retw

11:
	movi	a2, -EFAULT
	s32i	a2, a7, 0	/* dst_err_ptr */
	movi	a2, 0
	retw

.previous

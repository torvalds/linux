// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copy memory to memory until the specified number of bytes
 * has been copied.  Overlap is NOT handled correctly.
 * Copyright (C) 1991-2020 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 * Contributed by Torbjorn Granlund (tege@sics.se).
 *
 * The GNU C Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * The GNU C Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the GNU C Library; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 */

#define __NO_FORTIFY
#include <linux/types.h>
#include <linux/module.h>

#define MERGE(w0, sh_1, w1, sh_2) (((w0) >> (sh_1)) | ((w1) << (sh_2)))
#define OP_T_THRES      16
#define op_t    unsigned long
#define OPSIZ   (sizeof(op_t))
#define OPSIZ_MASK   (sizeof(op_t) - 1)
#define FAST_COPY_THRES  (128)
#define byte    unsigned char

static void _wordcopy_fwd_aligned(long dstp, long srcp, size_t len)
{
	op_t a0, a1;

	switch (len % 8) {
	case 2:
		a0 = ((op_t *) srcp)[0];
		srcp -= 6 * OPSIZ;
		dstp -= 7 * OPSIZ;
		len += 6;
		goto do1;
	case 3:
		a1 = ((op_t *) srcp)[0];
		srcp -= 5 * OPSIZ;
		dstp -= 6 * OPSIZ;
		len += 5;
		goto do2;
	case 4:
		a0 = ((op_t *) srcp)[0];
		srcp -= 4 * OPSIZ;
		dstp -= 5 * OPSIZ;
		len += 4;
		goto do3;
	case 5:
		a1 = ((op_t *) srcp)[0];
		srcp -= 3 * OPSIZ;
		dstp -= 4 * OPSIZ;
		len += 3;
		goto do4;
	case 6:
		a0 = ((op_t *) srcp)[0];
		srcp -= 2 * OPSIZ;
		dstp -= 3 * OPSIZ;
		len += 2;
		goto do5;
	case 7:
		a1 = ((op_t *) srcp)[0];
		srcp -= 1 * OPSIZ;
		dstp -= 2 * OPSIZ;
		len += 1;
		goto do6;

	case 0:
		if (OP_T_THRES <= 3 * OPSIZ && len == 0)
			return;
		a0 = ((op_t *) srcp)[0];
		srcp -= 0 * OPSIZ;
		dstp -= 1 * OPSIZ;
		goto do7;
	case 1:
		a1 = ((op_t *) srcp)[0];
		srcp -= -1 * OPSIZ;
		dstp -= 0 * OPSIZ;
		len -= 1;
		if (OP_T_THRES <= 3 * OPSIZ && len == 0)
			goto do0;
		goto do8;                 /* No-op.  */
	}

	do {
do8:
		a0 = ((op_t *) srcp)[0];
		((op_t *) dstp)[0] = a1;
do7:
		a1 = ((op_t *) srcp)[1];
		((op_t *) dstp)[1] = a0;
do6:
		a0 = ((op_t *) srcp)[2];
		((op_t *) dstp)[2] = a1;
do5:
		a1 = ((op_t *) srcp)[3];
		((op_t *) dstp)[3] = a0;
do4:
		a0 = ((op_t *) srcp)[4];
		((op_t *) dstp)[4] = a1;
do3:
		a1 = ((op_t *) srcp)[5];
		((op_t *) dstp)[5] = a0;
do2:
		a0 = ((op_t *) srcp)[6];
		((op_t *) dstp)[6] = a1;
do1:
		a1 = ((op_t *) srcp)[7];
		((op_t *) dstp)[7] = a0;

		srcp += 8 * OPSIZ;
		dstp += 8 * OPSIZ;
		len -= 8;
	} while (len != 0);

	/* This is the right position for do0.  Please don't move
	 * it into the loop.
	 */
do0:
	((op_t *) dstp)[0] = a1;
}

static void _wordcopy_fwd_dest_aligned(long dstp, long srcp, size_t len)
{
	op_t a0, a1, a2, a3;
	int sh_1, sh_2;

	/* Calculate how to shift a word read at the memory operation
	 * aligned srcp to make it aligned for copy.
	 */

	sh_1 = 8 * (srcp % OPSIZ);
	sh_2 = 8 * OPSIZ - sh_1;

	/* Make SRCP aligned by rounding it down to the beginning of the `op_t'
	 * it points in the middle of.
	 */
	srcp &= -OPSIZ;

	switch (len % 4) {
	case 2:
		a1 = ((op_t *) srcp)[0];
		a2 = ((op_t *) srcp)[1];
		srcp -= 1 * OPSIZ;
		dstp -= 3 * OPSIZ;
		len += 2;
		goto do1;
	case 3:
		a0 = ((op_t *) srcp)[0];
		a1 = ((op_t *) srcp)[1];
		srcp -= 0 * OPSIZ;
		dstp -= 2 * OPSIZ;
		len += 1;
		goto do2;
	case 0:
		if (OP_T_THRES <= 3 * OPSIZ && len == 0)
			return;
		a3 = ((op_t *) srcp)[0];
		a0 = ((op_t *) srcp)[1];
		srcp -= -1 * OPSIZ;
		dstp -= 1 * OPSIZ;
		len += 0;
		goto do3;
	case 1:
		a2 = ((op_t *) srcp)[0];
		a3 = ((op_t *) srcp)[1];
		srcp -= -2 * OPSIZ;
		dstp -= 0 * OPSIZ;
		len -= 1;
		if (OP_T_THRES <= 3 * OPSIZ && len == 0)
			goto do0;
		goto do4;                 /* No-op.  */
	}

	do {
do4:
		a0 = ((op_t *) srcp)[0];
		((op_t *) dstp)[0] = MERGE(a2, sh_1, a3, sh_2);
do3:
		a1 = ((op_t *) srcp)[1];
		((op_t *) dstp)[1] = MERGE(a3, sh_1, a0, sh_2);
do2:
		a2 = ((op_t *) srcp)[2];
		((op_t *) dstp)[2] = MERGE(a0, sh_1, a1, sh_2);
do1:
		a3 = ((op_t *) srcp)[3];
		((op_t *) dstp)[3] = MERGE(a1, sh_1, a2, sh_2);

		srcp += 4 * OPSIZ;
		dstp += 4 * OPSIZ;
		len -= 4;
	} while (len != 0);

	/* This is the right position for do0.  Please don't move
	 * it into the loop.
	 */
do0:
	((op_t *) dstp)[0] = MERGE(a2, sh_1, a3, sh_2);
}

#define BYTE_COPY_FWD(dst_bp, src_bp, nbytes)		\
do {							\
	size_t __nbytes = (nbytes);			\
	while (__nbytes > 0) {						\
		byte __x = ((byte *) src_bp)[0];		\
		src_bp += 1;				\
		__nbytes -= 1;				\
		((byte *) dst_bp)[0] = __x;		\
		dst_bp += 1;				\
	}						\
} while (0)

#define WORD_COPY_FWD(dst_bp, src_bp, nbytes_left, nbytes)			\
do {										\
	if (src_bp % OPSIZ == 0)						\
		_wordcopy_fwd_aligned(dst_bp, src_bp, (nbytes) / OPSIZ);	\
	else									\
		_wordcopy_fwd_dest_aligned(dst_bp, src_bp, (nbytes) / OPSIZ);	\
	src_bp += (nbytes) & -OPSIZ;						\
	dst_bp += (nbytes) & -OPSIZ;						\
	(nbytes_left) = (nbytes) % OPSIZ;					\
} while (0)

extern void *__memcpy_aligned(void *dest, const void *src, size_t len);
void *__memcpy(void *dest, const void *src, size_t len)
{
	unsigned long dstp = (long) dest;
	unsigned long srcp = (long) src;

	/* If there not too few bytes to copy, use word copy.  */
	if (len >= OP_T_THRES) {
		if ((len >= FAST_COPY_THRES) && ((dstp & OPSIZ_MASK) == 0) &&
			((srcp & OPSIZ_MASK) == 0)) {
			__memcpy_aligned(dest, src, len);
			return dest;
		}
		/* Copy just a few bytes to make DSTP aligned.  */
		len -= (-dstp) % OPSIZ;
		BYTE_COPY_FWD(dstp, srcp, (-dstp) % OPSIZ);

		/* Copy from SRCP to DSTP taking advantage of the known alignment of
		 * DSTP.  Number of bytes remaining is put in the third argument,
		 * i.e. in LEN.  This number may vary from machine to machine.
		 */
		WORD_COPY_FWD(dstp, srcp, len, len);
	/* Fall out and copy the tail.  */
	}

	/* There are just a few bytes to copy.  Use byte memory operations.  */
	BYTE_COPY_FWD(dstp, srcp, len);

	return dest;
}
EXPORT_SYMBOL(__memcpy);

void *memcpy(void *dest, const void *src, size_t len) __weak __alias(__memcpy);
EXPORT_SYMBOL(memcpy);

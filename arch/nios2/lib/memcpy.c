/* Extracted from GLIBC memcpy.c and memcopy.h, which is:
   Copyright (C) 1991, 1992, 1993, 1997, 2004 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Torbjorn Granlund (tege@sics.se).

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <linux/types.h>

/* Type to use for aligned memory operations.
   This should normally be the biggest type supported by a single load
   and store.  */
#define	op_t	unsigned long int
#define OPSIZ	(sizeof(op_t))

/* Optimal type for storing bytes in registers.  */
#define	reg_char	char

#define MERGE(w0, sh_1, w1, sh_2) (((w0) >> (sh_1)) | ((w1) << (sh_2)))

/* Copy exactly NBYTES bytes from SRC_BP to DST_BP,
   without any assumptions about alignment of the pointers.  */
#define BYTE_COPY_FWD(dst_bp, src_bp, nbytes)				\
do {									\
	size_t __nbytes = (nbytes);					\
	while (__nbytes > 0) {						\
		unsigned char __x = ((unsigned char *) src_bp)[0];	\
		src_bp += 1;						\
		__nbytes -= 1;						\
		((unsigned char *) dst_bp)[0] = __x;			\
		dst_bp += 1;						\
	}								\
} while (0)

/* Copy *up to* NBYTES bytes from SRC_BP to DST_BP, with
   the assumption that DST_BP is aligned on an OPSIZ multiple.  If
   not all bytes could be easily copied, store remaining number of bytes
   in NBYTES_LEFT, otherwise store 0.  */
/* extern void _wordcopy_fwd_aligned __P ((long int, long int, size_t)); */
/* extern void _wordcopy_fwd_dest_aligned __P ((long int, long int, size_t)); */
#define WORD_COPY_FWD(dst_bp, src_bp, nbytes_left, nbytes)		\
do {									\
	if (src_bp % OPSIZ == 0)					\
		_wordcopy_fwd_aligned(dst_bp, src_bp, (nbytes) / OPSIZ);\
	else								\
		_wordcopy_fwd_dest_aligned(dst_bp, src_bp, (nbytes) / OPSIZ);\
	src_bp += (nbytes) & -OPSIZ;					\
	dst_bp += (nbytes) & -OPSIZ;					\
	(nbytes_left) = (nbytes) % OPSIZ;				\
} while (0)


/* Threshold value for when to enter the unrolled loops.  */
#define	OP_T_THRES	16

/* _wordcopy_fwd_aligned -- Copy block beginning at SRCP to
   block beginning at DSTP with LEN `op_t' words (not LEN bytes!).
   Both SRCP and DSTP should be aligned for memory operations on `op_t's.  */
/* stream-lined (read x8 + write x8) */
static void _wordcopy_fwd_aligned(long int dstp, long int srcp, size_t len)
{
	while (len > 7) {
		register op_t a0, a1, a2, a3, a4, a5, a6, a7;
		a0 = ((op_t *) srcp)[0];
		a1 = ((op_t *) srcp)[1];
		a2 = ((op_t *) srcp)[2];
		a3 = ((op_t *) srcp)[3];
		a4 = ((op_t *) srcp)[4];
		a5 = ((op_t *) srcp)[5];
		a6 = ((op_t *) srcp)[6];
		a7 = ((op_t *) srcp)[7];
		((op_t *) dstp)[0] = a0;
		((op_t *) dstp)[1] = a1;
		((op_t *) dstp)[2] = a2;
		((op_t *) dstp)[3] = a3;
		((op_t *) dstp)[4] = a4;
		((op_t *) dstp)[5] = a5;
		((op_t *) dstp)[6] = a6;
		((op_t *) dstp)[7] = a7;

		srcp += 8 * OPSIZ;
		dstp += 8 * OPSIZ;
		len -= 8;
	}
	while (len > 0) {
		*(op_t *)dstp = *(op_t *)srcp;

		srcp += OPSIZ;
		dstp += OPSIZ;
		len -= 1;
	}
}

/* _wordcopy_fwd_dest_aligned -- Copy block beginning at SRCP to
   block beginning at DSTP with LEN `op_t' words (not LEN bytes!).
   DSTP should be aligned for memory operations on `op_t's, but SRCP must
   *not* be aligned.  */
/* stream-lined (read x4 + write x4) */
static void _wordcopy_fwd_dest_aligned(long int dstp, long int srcp,
					size_t len)
{
	op_t ap;
	int sh_1, sh_2;

	/* Calculate how to shift a word read at the memory operation
	aligned srcp to make it aligned for copy. */

	sh_1 = 8 * (srcp % OPSIZ);
	sh_2 = 8 * OPSIZ - sh_1;

	/* Make SRCP aligned by rounding it down to the beginning of the `op_t'
	it points in the middle of. */
	srcp &= -OPSIZ;
	ap = ((op_t *) srcp)[0];
	srcp += OPSIZ;

	while (len > 3) {
		op_t a0, a1, a2, a3;
		a0 = ((op_t *) srcp)[0];
		a1 = ((op_t *) srcp)[1];
		a2 = ((op_t *) srcp)[2];
		a3 = ((op_t *) srcp)[3];
		((op_t *) dstp)[0] = MERGE(ap, sh_1, a0, sh_2);
		((op_t *) dstp)[1] = MERGE(a0, sh_1, a1, sh_2);
		((op_t *) dstp)[2] = MERGE(a1, sh_1, a2, sh_2);
		((op_t *) dstp)[3] = MERGE(a2, sh_1, a3, sh_2);

		ap = a3;
		srcp += 4 * OPSIZ;
		dstp += 4 * OPSIZ;
		len -= 4;
	}
	while (len > 0) {
		register op_t a0;
		a0 = ((op_t *) srcp)[0];
		((op_t *) dstp)[0] = MERGE(ap, sh_1, a0, sh_2);

		ap = a0;
		srcp += OPSIZ;
		dstp += OPSIZ;
		len -= 1;
	}
}

void *memcpy(void *dstpp, const void *srcpp, size_t len)
{
	unsigned long int dstp = (long int) dstpp;
	unsigned long int srcp = (long int) srcpp;

	/* Copy from the beginning to the end.  */

	/* If there not too few bytes to copy, use word copy.  */
	if (len >= OP_T_THRES) {
		/* Copy just a few bytes to make DSTP aligned.  */
		len -= (-dstp) % OPSIZ;
		BYTE_COPY_FWD(dstp, srcp, (-dstp) % OPSIZ);

		/* Copy whole pages from SRCP to DSTP by virtual address
		   manipulation, as much as possible.  */

		/* PAGE_COPY_FWD_MAYBE (dstp, srcp, len, len); */

		/* Copy from SRCP to DSTP taking advantage of the known
		   alignment of DSTP. Number of bytes remaining is put in the
		   third argument, i.e. in LEN.  This number may vary from
		   machine to machine. */

		WORD_COPY_FWD(dstp, srcp, len, len);

		/* Fall out and copy the tail. */
	}

	/* There are just a few bytes to copy.  Use byte memory operations. */
	BYTE_COPY_FWD(dstp, srcp, len);

	return dstpp;
}

void *memcpyb(void *dstpp, const void *srcpp, unsigned len)
{
	unsigned long int dstp = (long int) dstpp;
	unsigned long int srcp = (long int) srcpp;

	BYTE_COPY_FWD(dstp, srcp, len);

	return dstpp;
}

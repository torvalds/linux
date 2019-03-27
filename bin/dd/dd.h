/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego and Lance
 * Visser of Convex Computer Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)dd.h	8.3 (Berkeley) 4/2/94
 * $FreeBSD$
 */

/* Input/output stream state. */
typedef struct {
	u_char		*db;		/* buffer address */
	u_char		*dbp;		/* current buffer I/O address */
	ssize_t		dbcnt;		/* current buffer byte count */
	ssize_t		dbrcnt;		/* last read byte count */
	ssize_t		dbsz;		/* block size */

#define	ISCHR		0x01		/* character device (warn on short) */
#define	ISPIPE		0x02		/* pipe-like (see position.c) */
#define	ISTAPE		0x04		/* tape */
#define	ISSEEK		0x08		/* valid to seek on */
#define	NOREAD		0x10		/* not readable */
#define	ISTRUNC		0x20		/* valid to ftruncate() */
	u_int		flags;

	const char	*name;		/* name */
	int		fd;		/* file descriptor */
	off_t		offset;		/* # of blocks to skip */
	off_t		seek_offset;	/* offset of last seek past output hole */
} IO;

typedef struct {
	uintmax_t	in_full;	/* # of full input blocks */
	uintmax_t	in_part;	/* # of partial input blocks */
	uintmax_t	out_full;	/* # of full output blocks */
	uintmax_t	out_part;	/* # of partial output blocks */
	uintmax_t	trunc;		/* # of truncated records */
	uintmax_t	swab;		/* # of odd-length swab blocks */
	uintmax_t	bytes;		/* # of bytes written */
	struct timespec	start;		/* start time of dd */
} STAT;

/* Flags (in ddflags). */
#define	C_ASCII		0x00000001
#define	C_BLOCK		0x00000002
#define	C_BS		0x00000004
#define	C_CBS		0x00000008
#define	C_COUNT		0x00000010
#define	C_EBCDIC	0x00000020
#define	C_FILES		0x00000040
#define	C_IBS		0x00000080
#define	C_IF		0x00000100
#define	C_LCASE		0x00000200
#define	C_NOERROR	0x00000400
#define	C_NOTRUNC	0x00000800
#define	C_OBS		0x00001000
#define	C_OF		0x00002000
#define	C_OSYNC		0x00004000
#define	C_PAREVEN	0x00008000
#define	C_PARNONE	0x00010000
#define	C_PARODD	0x00020000
#define	C_PARSET	0x00040000
#define	C_SEEK		0x00080000
#define	C_SKIP		0x00100000
#define	C_SPARSE	0x00200000
#define	C_SWAB		0x00400000
#define	C_SYNC		0x00800000
#define	C_UCASE		0x01000000
#define	C_UNBLOCK	0x02000000
#define	C_FILL		0x04000000
#define	C_STATUS	0x08000000
#define	C_NOXFER	0x10000000
#define	C_NOINFO	0x20000000
#define	C_PROGRESS	0x40000000

#define	C_PARITY	(C_PAREVEN | C_PARODD | C_PARNONE | C_PARSET)

#define	BISZERO(p, s)	((s) > 0 && *((const char *)p) == 0 && !memcmp( \
			    (const void *)(p), (const void *) \
			    ((const char *)p + 1), (s) - 1))

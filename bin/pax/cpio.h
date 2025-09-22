/*	$OpenBSD: cpio.h,v 1.4 2003/06/02 23:32:08 millert Exp $	*/
/*	$NetBSD: cpio.h,v 1.3 1995/03/21 09:07:15 cgd Exp $	*/

/*-
 * Copyright (c) 1992 Keith Muller.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego.
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
 *	@(#)cpio.h	8.1 (Berkeley) 5/31/93
 */

/*
 * Defines common to all versions of cpio
 */
#define TRAILER		"TRAILER!!!"	/* name in last archive record */

/*
 * Header encoding of the different file types
 */
#define	C_ISDIR		 040000		/* Directory */
#define	C_ISFIFO	 010000		/* FIFO */
#define	C_ISREG		0100000		/* Regular file */
#define	C_ISBLK		 060000		/* Block special file */
#define	C_ISCHR		 020000		/* Character special file */
#define	C_ISCTG		0110000		/* Reserved for contiguous files */
#define	C_ISLNK		0120000		/* Reserved for symbolic links */
#define	C_ISOCK		0140000		/* Reserved for sockets */
#define C_IFMT		0170000		/* type of file */

/*
 * Data Interchange Format - Extended cpio header format - POSIX 1003.1-1990
 */
typedef struct {
	char	c_magic[6];		/* magic cookie */
	char	c_dev[6];		/* device number */
	char	c_ino[6];		/* inode number */
	char	c_mode[6];		/* file type/access */
	char	c_uid[6];		/* owners uid */
	char	c_gid[6];		/* owners gid */
	char	c_nlink[6];		/* # of links at archive creation */
	char	c_rdev[6];		/* block/char major/minor # */
	char	c_mtime[11];		/* modification time */
	char	c_namesize[6];		/* length of pathname */
	char	c_filesize[11];		/* length of file in bytes */
} HD_CPIO;

#define	MAGIC		070707		/* transportable archive id */

#ifdef _PAX_
#define	AMAGIC		"070707"	/* ascii equivalent string of MAGIC */
#define CPIO_MASK	0x3ffff		/* bits valid in the dev/ino fields */
					/* used for dev/inode remaps */
#endif /* _PAX_ */

/*
 * Binary cpio header structure
 *
 * CAUTION! CAUTION! CAUTION!
 * Each field really represents a 16 bit short (NOT ASCII). Described as
 * an array of chars in an attempt to improve portability!!
 */
typedef struct {
	u_char	h_magic[2];
	u_char	h_dev[2];
	u_char	h_ino[2];
	u_char	h_mode[2];
	u_char	h_uid[2];
	u_char	h_gid[2];
	u_char	h_nlink[2];
	u_char	h_rdev[2];
	u_char	h_mtime_1[2];
	u_char	h_mtime_2[2];
	u_char	h_namesize[2];
	u_char	h_filesize_1[2];
	u_char	h_filesize_2[2];
} HD_BCPIO;

#ifdef _PAX_
/*
 * extraction and creation macros for binary cpio
 */
#define SHRT_EXT(ch)	((((unsigned)(ch)[0])<<8) | (((unsigned)(ch)[1])&0xff))
#define RSHRT_EXT(ch)	((((unsigned)(ch)[1])<<8) | (((unsigned)(ch)[0])&0xff))
#define CHR_WR_0(val)	((char)(((val) >> 24) & 0xff))
#define CHR_WR_1(val)	((char)(((val) >> 16) & 0xff))
#define CHR_WR_2(val)	((char)(((val) >> 8) & 0xff))
#define CHR_WR_3(val)	((char)((val) & 0xff))

/*
 * binary cpio masks and pads
 */
#define BCPIO_PAD(x)	((2 - ((x) & 1)) & 1)	/* pad to next 2 byte word */
#define BCPIO_MASK	0xffff			/* mask for dev/ino fields */
#endif /* _PAX_ */

/*
 * System VR4 cpio header structure (with/without file data crc)
 */
typedef struct {
	char	c_magic[6];		/* magic cookie */
	char	c_ino[8];		/* inode number */
	char	c_mode[8];		/* file type/access */
	char	c_uid[8];		/* owners uid */
	char	c_gid[8];		/* owners gid */
	char	c_nlink[8];		/* # of links at archive creation */
	char	c_mtime[8];		/* modification time */
	char	c_filesize[8];		/* length of file in bytes */
	char	c_maj[8];		/* block/char major # */
	char	c_min[8];		/* block/char minor # */
	char	c_rmaj[8];		/* special file major # */
	char	c_rmin[8];		/* special file minor # */
	char	c_namesize[8];		/* length of pathname */
	char	c_chksum[8];		/* 0 OR CRC of bytes of FILE data */
} HD_VCPIO;

#define	VMAGIC		070701		/* sVr4 new portable archive id */
#define	VCMAGIC		070702		/* sVr4 new portable archive id CRC */
#ifdef _PAX_
#define	AVMAGIC		"070701"	/* ascii string of above */
#define	AVCMAGIC	"070702"	/* ascii string of above */
#define VCPIO_PAD(x)	((4 - ((x) & 3)) & 3)	/* pad to next 4 byte word */
#define VCPIO_MASK	0xffffffff	/* mask for dev/ino fields */
#endif /* _PAX_ */

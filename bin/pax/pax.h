/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)pax.h	8.2 (Berkeley) 4/18/94
 * $FreeBSD$
 */

/*
 * BSD PAX global data structures and constants.
 */

#define	MAXBLK		64512	/* MAX blocksize supported (posix SPEC) */
				/* WARNING: increasing MAXBLK past 32256 */
				/* will violate posix spec. */
#define	MAXBLK_POSIX	32256	/* MAX blocksize supported as per POSIX */
#define BLKMULT		512	/* blocksize must be even mult of 512 bytes */
				/* Don't even think of changing this */
#define DEVBLK		8192	/* default read blksize for devices */
#define FILEBLK		10240	/* default read blksize for files */
#define PAXPATHLEN	3072	/* maximum path length for pax. MUST be */
				/* longer than the system PATH_MAX */

/*
 * Pax modes of operation
 */
#define	LIST		0	/* List the file in an archive */
#define	EXTRACT		1	/* extract the files in an archive */
#define ARCHIVE		2	/* write a new archive */
#define APPND		3	/* append to the end of an archive */
#define	COPY		4	/* copy files to destination dir */
#define DEFOP		LIST	/* if no flags default is to LIST */

/*
 * Device type of the current archive volume
 */
#define ISREG		0	/* regular file */
#define ISCHR		1	/* character device */
#define ISBLK		2	/* block device */
#define ISTAPE		3	/* tape drive */
#define ISPIPE		4	/* pipe/socket */

typedef struct archd ARCHD;
typedef struct fsub FSUB;
typedef struct oplist OPLIST;
typedef struct pattern PATTERN;

/*
 * Format Specific Routine Table
 *
 * The format specific routine table allows new archive formats to be quickly
 * added. Overall pax operation is independent of the actual format used to
 * form the archive. Only those routines which deal directly with the archive
 * are tailored to the oddities of the specific format. All other routines are
 * independent of the archive format. Data flow in and out of the format
 * dependent routines pass pointers to ARCHD structure (described below).
 */
struct fsub {
	const char *name;	/* name of format, this is the name the user */
				/* gives to -x option to select it. */
	int bsz;		/* default block size. used when the user */
				/* does not specify a blocksize for writing */
				/* Appends continue to with the blocksize */
				/* the archive is currently using. */
	int hsz;		/* Header size in bytes. this is the size of */
				/* the smallest header this format supports. */
				/* Headers are assumed to fit in a BLKMULT. */
				/* If they are bigger, get_head() and */
				/* get_arc() must be adjusted */
	int udev;		/* does append require unique dev/ino? some */
				/* formats use the device and inode fields */
				/* to specify hard links. when members in */
				/* the archive have the same inode/dev they */
				/* are assumed to be hard links. During */
				/* append we may have to generate unique ids */
				/* to avoid creating incorrect hard links */
	int hlk;		/* does archive store hard links info? if */
				/* not, we do not bother to look for them */
				/* during archive write operations */
	int blkalgn;		/* writes must be aligned to blkalgn boundary */
	int inhead;		/* is the trailer encoded in a valid header? */
				/* if not, trailers are assumed to be found */
				/* in invalid headers (i.e like tar) */
	int (*id)(char *, int);	/* checks if a buffer is a valid header */
				/* returns 1 if it is, o.w. returns a 0 */
	int (*st_rd)(void);	/* initialize routine for read. so format */
				/* can set up tables etc before it starts */
				/* reading an archive */
	int (*rd)(ARCHD *, char *);
				/* read header routine. passed a pointer to */
				/* ARCHD. It must extract the info from the */
				/* format and store it in the ARCHD struct. */
				/* This routine is expected to fill all the */
				/* fields in the ARCHD (including stat buf) */
				/* 0 is returned when a valid header is */
				/* found. -1 when not valid. This routine */
				/* set the skip and pad fields so the format */
				/* independent routines know the amount of */
				/* padding and the number of bytes of data */
				/* which follow the header. This info is */
				/* used skip to the next file header */
	off_t (*end_rd)(void);	/* read cleanup. Allows format to clean up */
				/* and MUST RETURN THE LENGTH OF THE TRAILER */
				/* RECORD (so append knows how many bytes */
				/* to move back to rewrite the trailer) */
	int (*st_wr)(void);	/* initialize routine for write operations */
	int (*wr)(ARCHD *);	/* write archive header. Passed an ARCHD */
				/* filled with the specs on the next file to */
				/* archived. Returns a 1 if no file data is */
				/* is to be stored; 0 if file data is to be */
				/* added. A -1 is returned if a write */
				/* operation to the archive failed. this */
				/* function sets the skip and pad fields so */
				/* the proper padding can be added after */
				/* file data. This routine must NEVER write */
				/* a flawed archive header. */
	int (*end_wr)(void);	/* end write. write the trailer and do any */
				/* other format specific functions needed */
				/* at the end of an archive write */
	int (*trail_cpio)(ARCHD *);
	int (*trail_tar)(char *, int, int *);
				/* returns 0 if a valid trailer, -1 if not */
				/* For formats which encode the trailer */
				/* outside of a valid header, a return value */
				/* of 1 indicates that the block passed to */
				/* it can never contain a valid header (skip */
				/* this block, no point in looking at it)  */
	int (*rd_data)(ARCHD *, int, off_t *);
				/* read/process file data from the archive */
	int (*wr_data)(ARCHD *, int, off_t *);
				/* write/process file data to the archive */
	int (*options)(void);	/* process format specific options (-o) */
};

/*
 * Pattern matching structure
 *
 * Used to store command line patterns
 */
struct pattern {
	char		*pstr;		/* pattern to match, user supplied */
	char		*pend;		/* end of a prefix match */
	char		*chdname;	/* the dir to change to if not NULL.  */
	int		plen;		/* length of pstr */
	int		flgs;		/* processing/state flags */
#define MTCH		0x1		/* pattern has been matched */
#define DIR_MTCH	0x2		/* pattern matched a directory */
	struct pattern	*fow;		/* next pattern */
};

/*
 * General Archive Structure (used internal to pax)
 *
 * This structure is used to pass information about archive members between
 * the format independent routines and the format specific routines. When
 * new archive formats are added, they must accept requests and supply info
 * encoded in a structure of this type. The name fields are declared statically
 * here, as there is only ONE of these floating around, size is not a major
 * consideration. Eventually converting the name fields to a dynamic length
 * may be required if and when the supporting operating system removes all
 * restrictions on the length of pathnames it will resolve.
 */
struct archd {
	int nlen;			/* file name length */
	char name[PAXPATHLEN+1];	/* file name */
	int ln_nlen;			/* link name length */
	char ln_name[PAXPATHLEN+1];	/* name to link to (if any) */
	char *org_name;			/* orig name in file system */
	PATTERN *pat;			/* ptr to pattern match (if any) */
	struct stat sb;			/* stat buffer see stat(2) */
	off_t pad;			/* bytes of padding after file xfer */
	off_t skip;			/* bytes of real data after header */
					/* IMPORTANT. The st_size field does */
					/* not always indicate the amount of */
					/* data following the header. */
	u_long crc;			/* file crc */
	int type;			/* type of file node */
#define PAX_DIR		1		/* directory */
#define PAX_CHR		2		/* character device */
#define PAX_BLK		3		/* block device */
#define PAX_REG		4		/* regular file */
#define PAX_SLK		5		/* symbolic link */
#define PAX_SCK		6		/* socket */
#define PAX_FIF		7		/* fifo */
#define PAX_HLK		8		/* hard link */
#define PAX_HRG		9		/* hard link to a regular file */
#define PAX_CTG		10		/* high performance file */
};

/*
 * Format Specific Options List
 *
 * Used to pass format options to the format options handler
 */
struct oplist {
	char		*name;		/* option variable name e.g. name= */
	char		*value;		/* value for option variable */
	struct oplist	*fow;		/* next option */
};

/*
 * General Macros
 */
#ifndef MIN
#define	       MIN(a,b) (((a)<(b))?(a):(b))
#endif
#define MAJOR(x)	major(x)
#define MINOR(x)	minor(x)
#define TODEV(x, y)	makedev((x), (y))

/*
 * General Defines
 */
#define HEX		16
#define OCT		8
#define _PAX_		1
#define _TFILE_BASE	"paxXXXXXXXXXX"

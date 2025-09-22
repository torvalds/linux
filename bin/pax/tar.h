/*	$OpenBSD: tar.h,v 1.9 2014/01/08 06:43:34 deraadt Exp $	*/
/*	$NetBSD: tar.h,v 1.3 1995/03/21 09:07:51 cgd Exp $	*/

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
 *	@(#)tar.h	8.2 (Berkeley) 4/18/94
 */

/*
 * defines and data structures common to all tar formats
 */
#define CHK_LEN		8		/* length of checksum field */
#define TNMSZ		100		/* size of name field */
#ifdef _PAX_
#define NULLCNT		2		/* number of null blocks in trailer */
#define CHK_OFFSET	148		/* start of chksum field */
#define BLNKSUM		256L		/* sum of checksum field using ' ' */
#endif /* _PAX_ */

/*
 * Values used in typeflag field in all tar formats
 * (only REGTYPE, LNKTYPE and SYMTYPE are used in old bsd tar headers)
 */
#define	REGTYPE		'0'		/* Regular File */
#define	AREGTYPE	'\0'		/* Regular File */
#define	LNKTYPE		'1'		/* Link */
#define	SYMTYPE		'2'		/* Symlink */
#define	CHRTYPE		'3'		/* Character Special File */
#define	BLKTYPE		'4'		/* Block Special File */
#define	DIRTYPE		'5'		/* Directory */
#define	FIFOTYPE	'6'		/* FIFO */
#define	CONTTYPE	'7'		/* high perf file */

/*
 * Extended header - POSIX.1-2001
 */
#define	XHDRTYPE	'x'		/* Extended header */
#define	GHDRTYPE	'g'		/* Global header*/

/*
 * GNU tar compatibility;
 */
#define	LONGLINKTYPE	'K'		/* Long Symlink */
#define	LONGNAMETYPE	'L'		/* Long File */

/*
 * Mode field encoding of the different file types - values in octal
 */
#define TSUID		04000		/* Set UID on execution */
#define TSGID		02000		/* Set GID on execution */
#define TSVTX		01000		/* Reserved */
#define TUREAD		00400		/* Read by owner */
#define TUWRITE		00200		/* Write by owner */
#define TUEXEC		00100		/* Execute/Search by owner */
#define TGREAD		00040		/* Read by group */
#define TGWRITE		00020		/* Write by group */
#define TGEXEC		00010		/* Execute/Search by group */
#define TOREAD		00004		/* Read by other */
#define TOWRITE		00002		/* Write by other */
#define TOEXEC		00001		/* Execute/Search by other */

#ifdef _PAX_
/*
 * Pad with a bit mask, much faster than doing a mod but only works on powers
 * of 2. Macro below is for block of 512 bytes.
 */
#define TAR_PAD(x)	((512 - ((x) & 511)) & 511)
#endif /* _PAX_ */

/*
 * structure of an old tar header as it appeared in BSD releases
 */
typedef struct {
	char name[TNMSZ];		/* name of entry */
	char mode[8];			/* mode */
	char uid[8];			/* uid */
	char gid[8];			/* gid */
	char size[12];			/* size */
	char mtime[12];			/* modification time */
	char chksum[CHK_LEN];		/* checksum */
	char linkflag;			/* norm, hard, or sym. */
	char linkname[TNMSZ];		/* linked to name */
} HD_TAR;

#ifdef _PAX_
/*
 * -o options for BSD tar to not write directories to the archive
 */
#define TAR_NODIR	"nodir"
#define TAR_OPTION	"write_opt"

/*
 * default device names
 */
#define	DEV_0		"/dev/rst0"
#define	DEV_1		"/dev/rst1"
#define	DEV_4		"/dev/rst4"
#define	DEV_5		"/dev/rst5"
#define	DEV_7		"/dev/rst7"
#define	DEV_8		"/dev/rst8"
#endif /* _PAX_ */

/*
 * Data Interchange Format - Extended tar header format - POSIX 1003.1-1990
 */
#define TPFSZ		155
#define	TMAGIC		"ustar"		/* ustar and a null */
#define	TMAGLEN		6
#define	TVERSION	"00"		/* 00 and no null */
#define	TVERSLEN	2

typedef struct {
	char name[TNMSZ];		/* name of entry */
	char mode[8];			/* mode */
	char uid[8];			/* uid */
	char gid[8];			/* gid */
	char size[12];			/* size */
	char mtime[12];			/* modification time */
	char chksum[CHK_LEN];		/* checksum */
	char typeflag;			/* type of file. */
	char linkname[TNMSZ];		/* linked to name */
	char magic[TMAGLEN];		/* magic cookie */
	char version[TVERSLEN];		/* version */
	char uname[32];			/* ascii owner name */
	char gname[32];			/* ascii group name */
	char devmajor[8];		/* major device number */
	char devminor[8];		/* minor device number */
	char prefix[TPFSZ];		/* linked to name */
} HD_USTAR;

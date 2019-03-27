/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Michael Fischbein.
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
 *	from: @(#)ls.h	8.1 (Berkeley) 5/31/93
 * $FreeBSD$
 */

#define NO_PRINT	1

#define HUMANVALSTR_LEN	5

extern long blocksize;		/* block size units */

extern int f_accesstime;	/* use time of last access */
extern int f_birthtime;	/* use time of file creation */
extern int f_flags;		/* show flags associated with a file */
extern int f_humanval;		/* show human-readable file sizes */
extern int f_label;		/* show MAC label */
extern int f_inode;		/* print inode */
extern int f_longform;		/* long listing format */
extern int f_octal;		/* print unprintables in octal */
extern int f_octal_escape;	/* like f_octal but use C escapes if possible */
extern int f_nonprint;		/* show unprintables as ? */
extern int f_samesort;		/* sort time and name in same direction */
extern int f_sectime;		/* print the real time for all files */
extern int f_size;		/* list size in short listing */
extern int f_slash;		/* append a '/' if the file is a directory */
extern int f_sortacross;	/* sort across rows, not down columns */
extern int f_statustime;	/* use time of last mode change */
extern int f_thousands;		/* show file sizes with thousands separators */
extern char *f_timeformat;	/* user-specified time format */
extern int f_notabs;		/* don't use tab-separated multi-col output */
extern int f_type;		/* add type character for non-regular files */
#ifdef COLORLS
extern int f_color;		/* add type in color for non-regular files */
#endif

typedef struct {
	FTSENT *list;
	u_long btotal;
	int entries;
	int maxlen;
	u_int s_block;
	u_int s_flags;
	u_int s_label;
	u_int s_group;
	u_int s_inode;
	u_int s_nlink;
	u_int s_size;
	u_int s_user;
} DISPLAY;

typedef struct {
	char *user;
	char *group;
	char *flags;
	char *label;
	char data[1];
} NAMES;

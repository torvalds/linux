/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Guido van Rossum.
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
 *	@(#)glob.h	8.1 (Berkeley) 6/2/93
 */

#ifndef _GLOB_H_
#define	_GLOB_H_

#ifndef ROKEN_LIB_FUNCTION
#ifdef _WIN32
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL _stdcall
#else
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define glob_t rk_glob_t
#define	glob rk_glob
#define	globfree rk_globfree

struct stat;
typedef struct {
	int gl_pathc;		/* Count of total paths so far. */
	int gl_matchc;		/* Count of paths matching pattern. */
	int gl_offs;		/* Reserved at beginning of gl_pathv. */
	int gl_flags;		/* Copy of flags parameter to glob. */
	char **gl_pathv;	/* List of paths matching pattern. */
				/* Copy of errfunc parameter to glob. */
	int (*gl_errfunc) (const char *, int);

	/*
	 * Alternate filesystem access methods for glob; replacement
	 * versions of closedir(3), readdir(3), opendir(3), stat(2)
	 * and lstat(2).
	 */
	void (*gl_closedir) (void *);
	struct dirent *(*gl_readdir) (void *);	
	void *(*gl_opendir) (const char *);
	int (*gl_lstat) (const char *, struct stat *);
	int (*gl_stat) (const char *, struct stat *);
} glob_t;

#define	GLOB_APPEND	0x0001	/* Append to output from previous call. */
#define	GLOB_DOOFFS	0x0002	/* Use gl_offs. */
#define	GLOB_ERR	0x0004	/* Return on error. */
#define	GLOB_MARK	0x0008	/* Append / to matching directories. */
#define	GLOB_NOCHECK	0x0010	/* Return pattern itself if nothing matches. */
#define	GLOB_NOSORT	0x0020	/* Don't sort. */

#define	GLOB_ALTDIRFUNC	0x0040	/* Use alternately specified directory funcs. */
#define	GLOB_BRACE	0x0080	/* Expand braces ala csh. */
#define	GLOB_MAGCHAR	0x0100	/* Pattern had globbing characters. */
#define	GLOB_NOMAGIC	0x0200	/* GLOB_NOCHECK without magic chars (csh). */
#define	GLOB_QUOTE	0x0400	/* Quote special chars with \. */
#define	GLOB_TILDE	0x0800	/* Expand tilde names from the passwd file. */
#define GLOB_LIMIT	0x1000	/* Limit memory used by matches to ARG_MAX */

#define	GLOB_NOSPACE	(-1)	/* Malloc call failed. */
#define	GLOB_ABEND	(-2)	/* Unignored error. */

int ROKEN_LIB_FUNCTION
glob (const char *, int, int (*)(const char *, int), glob_t *);

void ROKEN_LIB_FUNCTION
globfree (glob_t *);

#ifdef __cplusplus
}
#endif

#endif /* !_GLOB_H_ */

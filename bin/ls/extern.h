/*	$OpenBSD: extern.h,v 1.10 2015/12/01 18:36:13 schwarze Exp $	*/
/*	$NetBSD: extern.h,v 1.5 1995/03/21 09:06:24 cgd Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)extern.h	8.1 (Berkeley) 5/31/93
 */

extern char *__progname;

int	 acccmp(const FTSENT *, const FTSENT *);
int	 revacccmp(const FTSENT *, const FTSENT *);
int	 modcmp(const FTSENT *, const FTSENT *);
int	 revmodcmp(const FTSENT *, const FTSENT *);
int	 namecmp(const FTSENT *, const FTSENT *);
int	 revnamecmp(const FTSENT *, const FTSENT *);
int	 statcmp(const FTSENT *, const FTSENT *);
int	 revstatcmp(const FTSENT *, const FTSENT *);
int	 sizecmp(const FTSENT *, const FTSENT *);
int	 revsizecmp(const FTSENT *, const FTSENT *);

int	 mbsprint(const char *, int);
void	 printcol(DISPLAY *);
void	 printacol(DISPLAY *);
void	 printlong(DISPLAY *);
void	 printscol(DISPLAY *);
void	 printstream(DISPLAY *);
void	 usage(void);

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	from: @(#)extern.h	8.1 (Berkeley) 5/31/93
 * $FreeBSD$
 */

#include <stdbool.h>

int	 acccmp(const FTSENT *, const FTSENT *);
int	 revacccmp(const FTSENT *, const FTSENT *);
int	 birthcmp(const FTSENT *, const FTSENT *);
int	 revbirthcmp(const FTSENT *, const FTSENT *);
int	 modcmp(const FTSENT *, const FTSENT *);
int	 revmodcmp(const FTSENT *, const FTSENT *);
int	 namecmp(const FTSENT *, const FTSENT *);
int	 revnamecmp(const FTSENT *, const FTSENT *);
int	 statcmp(const FTSENT *, const FTSENT *);
int	 revstatcmp(const FTSENT *, const FTSENT *);
int	 sizecmp(const FTSENT *, const FTSENT *);
int	 revsizecmp(const FTSENT *, const FTSENT *);

void	 printcol(const DISPLAY *);
void	 printlong(const DISPLAY *);
int	 printname(const char *);
void	 printscol(const DISPLAY *);
void	 printstream(const DISPLAY *);
void	 usage(void);
int	 prn_normal(const char *);
size_t	 len_octal(const char *, int);
int	 prn_octal(const char *);
int	 prn_printable(const char *);
#ifdef COLORLS
void	 parsecolors(const char *cs);
void	 colorquit(int);

extern	char	*ansi_fgcol;
extern	char	*ansi_bgcol;
extern	char	*ansi_coloff;
extern	char	*attrs_off;
extern	char	*enter_bold;

extern int	 colorflag;
extern bool	 explicitansi;

#define	COLORFLAG_NEVER		0
#define	COLORFLAG_AUTO		1
#define	COLORFLAG_ALWAYS	2
#endif
extern int	termwidth;

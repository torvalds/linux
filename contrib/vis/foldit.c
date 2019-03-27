/*	$NetBSD: foldit.c,v 1.7 2009/02/10 23:06:31 christos Exp $	*/

/*-
 * Copyright (c) 1990, 1993
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
 */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)foldit.c	8.1 (Berkeley) 6/6/93";
#endif
__RCSID("$NetBSD: foldit.c,v 1.7 2009/02/10 23:06:31 christos Exp $");
#endif /* not lint */

#include <stdio.h>
#include <vis.h>
#include "extern.h"

int
foldit(const char *chunk, int col, int max, int flags)
{
	const char *cp;

	/*
	 * Keep track of column position. Insert hidden newline
	 * if this chunk puts us over the limit.
	 */
again:
	cp = chunk;
	while (*cp) {
		switch(*cp) {
		case '\n':
		case '\r':
			col = 0;
			break;
		case '\t':
			col = (col + 8) &~ 07;
			break;
		case '\b':
			col = col ? col - 1 : 0;
			break;
		default:
			col++;
		}
		if (col > (max - 2)) {
			printf(flags & VIS_MIMESTYLE ? "=\n" : "\\\n");
			col = 0;
			goto again;
		} 
		cp++;
	}
	return (col);
}

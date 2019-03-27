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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

__FBSDID("$FreeBSD$");

#ifndef lint
#if 0
static const char sccsid[] = "@(#)genget.c	8.2 (Berkeley) 5/30/95";
#endif
#endif /* not lint */


#include <ctype.h>

#include "misc-proto.h"

#define	LOWER(x) (isupper(x) ? tolower(x) : (x))
/*
 * The prefix function returns 0 if *s1 is not a prefix
 * of *s2.  If *s1 exactly matches *s2, the negative of
 * the length is returned.  If *s1 is a prefix of *s2,
 * the length of *s1 is returned.
 */
int
isprefix(char *s1, const char *s2)
{
	char *os1;
	char c1, c2;

	if (*s1 == '\0')
		return(-1);
	os1 = s1;
	c1 = *s1;
	c2 = *s2;
	while (LOWER(c1) == LOWER(c2)) {
		if (c1 == '\0')
			break;
		c1 = *++s1;
		c2 = *++s2;
	}
	return(*s1 ? 0 : (*s2 ? (s1 - os1) : (os1 - s1)));
}

static char *ambiguous;		/* special return value for command routines */

char **
genget(char *name, char **table, int stlen)
{
	char **c, **found;
	int n;

	if (name == 0)
	    return 0;

	found = 0;
	for (c = table; *c != 0; c = (char **)((char *)c + stlen)) {
		if ((n = isprefix(name, *c)) == 0)
			continue;
		if (n < 0)		/* exact match */
			return(c);
		if (found)
			return(&ambiguous);
		found = c;
	}
	return(found);
}

/*
 * Function call version of Ambiguous()
 */
int
Ambiguous(char **s)
{
	return(s == &ambiguous);
}

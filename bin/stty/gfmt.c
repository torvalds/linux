/*	$OpenBSD: gfmt.c,v 1.9 2016/03/23 14:52:42 mmcc Exp $	*/
/*	$NetBSD: gfmt.c,v 1.10 1996/05/07 18:20:08 jtc Exp $	*/

/*-
 * Copyright (c) 1991, 1993, 1994
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

#include <sys/types.h>

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

#include "stty.h"
#include "extern.h"

static void
gerr(char *s)
{
	if (s)
		errx(1, "illegal gfmt1 option -- %s", s);
	else
		errx(1, "illegal gfmt1 option");
}

void
gprint(struct termios *tp, struct winsize *wp, int ldisc)
{
	const struct cchar *cp;

	(void)printf("gfmt1:cflag=%x:iflag=%x:lflag=%x:oflag=%x:",
	    tp->c_cflag, tp->c_iflag, tp->c_lflag, tp->c_oflag);
	for (cp = cchars1; cp->name; ++cp)
		(void)printf("%s=%x:", cp->name, tp->c_cc[cp->sub]);
	(void)printf("ispeed=%d:ospeed=%d\n", cfgetispeed(tp), cfgetospeed(tp));
}

void
gread(struct termios *tp, char *s)
{
	const struct cchar *cp;
	char *ep, *p;
	long tmp;

	if ((s = strchr(s, ':')) == NULL)
		gerr(NULL);
	for (++s; s != NULL;) {
		p = strsep(&s, ":\0");
		if (!p || !*p)
			break;
		if (!(ep = strchr(p, '=')))
			gerr(p);
		*ep++ = '\0';
		(void)sscanf(ep, "%lx", &tmp);

#define	CHK(s)	(*p == s[0] && !strcmp(p, s))
		if (CHK("cflag")) {
			tp->c_cflag = tmp;
			continue;
		}
		if (CHK("iflag")) {
			tp->c_iflag = tmp;
			continue;
		}
		if (CHK("ispeed")) {
			(void)sscanf(ep, "%ld", &tmp);
			tp->c_ispeed = tmp;
			continue;
		}
		if (CHK("lflag")) {
			tp->c_lflag = tmp;
			continue;
		}
		if (CHK("oflag")) {
			tp->c_oflag = tmp;
			continue;
		}
		if (CHK("ospeed")) {
			(void)sscanf(ep, "%ld", &tmp);
			tp->c_ospeed = tmp;
			continue;
		}
		for (cp = cchars1; cp->name != NULL; ++cp)
			if (CHK(cp->name)) {
				tp->c_cc[cp->sub] = tmp;
				break;
			}
		if (cp->name == NULL)
			gerr(p);
	}
}

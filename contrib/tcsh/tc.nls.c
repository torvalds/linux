/* $Header: /p/tcsh/cvsroot/tcsh/tc.nls.c,v 3.27 2016/07/17 15:02:44 christos Exp $ */
/*
 * tc.nls.c: NLS handling
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
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
#include "sh.h"

RCSID("$tcsh: tc.nls.c,v 3.27 2016/07/17 15:02:44 christos Exp $")


#ifdef WIDE_STRINGS
# ifdef HAVE_WCWIDTH
#  ifdef UTF16_STRINGS
int
xwcwidth (wint_t wchar)
{
  wchar_t ws[2];

  if (wchar <= 0xffff)
    return wcwidth ((wchar_t) wchar);
  /* UTF-16 systems can't handle these values directly in calls to wcwidth.
     However, they can handle them as surrogate pairs in calls to wcswidth.
     What we do here is to convert UTF-32 values >= 0x10000 into surrogate
     pairs and compute the width by calling wcswidth. */
  wchar -= 0x10000;
  ws[0] = 0xd800 | (wchar >> 10);
  ws[1] = 0xdc00 | (wchar & 0x3ff);
  return wcswidth (ws, 2);
}
#  else
#define xwcwidth wcwidth
#  endif /* !UTF16_STRINGS */
# endif /* HAVE_WCWIDTH */

int
NLSWidth(Char c)
{
# ifdef HAVE_WCWIDTH
    int l;
#if INVALID_BYTE != 0
    if ((c & INVALID_BYTE) == INVALID_BYTE)	/* c >= INVALID_BYTE */
#else
    if (c & INVALID_BYTE)
#endif
	return 1;
    l = xwcwidth((wchar_t) c);
    return l >= 0 ? l : 0;
# else
    return iswprint(c) != 0;
# endif
}

int
NLSStringWidth(const Char *s)
{
    int w = 0, l;
    Char c;

    while (*s) {
	c = *s++;
#ifdef HAVE_WCWIDTH
	if ((l = xwcwidth((wchar_t) c)) < 0)
		l = 2;
#else
	l = iswprint(c) != 0;
#endif
	w += l;
    }
    return w;
}
#endif

Char *
NLSChangeCase(const Char *p, int mode)
{
    Char c, *n, c2 = 0;
    const Char *op = p;

    for (; (c = *p) != 0; p++) {
        if (mode == 0 && Islower(c)) {
	    c2 = Toupper(c);
	    break;
        } else if (mode && Isupper(c)) {
	    c2 = Tolower(c);
	    break;
	}
    }
    if (!*p)
	return 0;
    n = Strsave(op);
    n[p - op] = c2;
    return n;
}

int
NLSClassify(Char c, int nocomb, int drawPrompt)
{
    int w;
#ifndef SHORT_STRINGS
    if ((c & 0x80) != 0)		/* c >= 0x80 */
	return NLSCLASS_ILLEGAL;
#endif
    if (!drawPrompt) {			/* draw command-line */
#if INVALID_BYTE != 0
	if ((c & INVALID_BYTE) == INVALID_BYTE)		/* c >= INVALID_BYTE */
	    return NLSCLASS_ILLEGAL;
	if ((c & INVALID_BYTE) == QUOTE && (c & 0x80) == 0)	/* c >= QUOTE */
	    return 1;
	if (c >= 0x10000000)		/* U+10000000 = FC 90 80 80 80 80 */
	    return NLSCLASS_ILLEGAL5;
	if (c >= 0x1000000)		/*  U+1000000 = F9 80 80 80 80 */
	    return NLSCLASS_ILLEGAL4;
	if (c >= 0x100000)		/*   U+100000 = F4 80 80 80 */
	    return NLSCLASS_ILLEGAL3;
#endif
	if (c >= 0x10000)		/*    U+10000 = F0 90 80 80 */
	    return NLSCLASS_ILLEGAL2;
    }
    if (Iscntrl(c) && (c & CHAR) < 0x100) {
	if (c == '\n')
	    return NLSCLASS_NL;
	if (c == '\t')
	    return NLSCLASS_TAB;
	return NLSCLASS_CTRL;
    }
    w = NLSWidth(c);
    if (drawPrompt) {			/* draw prompt */
	if (w > 0)
	    return w;
	if (w == 0)
	    return 1;
    }
    if ((w > 0 && !(Iscntrl(c) && (c & CHAR) < 0x100)) || (Isprint(c) && !nocomb))
	return w;
    return NLSCLASS_ILLEGAL;
}

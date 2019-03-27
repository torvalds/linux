/*	$NetBSD: unvis.c,v 1.44 2014/09/26 15:43:36 roy Exp $	*/

/*-
 * Copyright (c) 1989, 1993
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
#if defined(LIBC_SCCS) && !defined(lint)
#if 0
static char sccsid[] = "@(#)unvis.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: unvis.c,v 1.44 2014/09/26 15:43:36 roy Exp $");
#endif
#endif /* LIBC_SCCS and not lint */
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <vis.h>

#define	_DIAGASSERT(x)	assert(x)

/*
 * Return the number of elements in a statically-allocated array,
 * __x.
 */
#define	__arraycount(__x)	(sizeof(__x) / sizeof(__x[0]))

#ifdef __weak_alias
__weak_alias(strnunvisx,_strnunvisx)
#endif

#if !HAVE_VIS
/*
 * decode driven by state machine
 */
#define	S_GROUND	0	/* haven't seen escape char */
#define	S_START		1	/* start decoding special sequence */
#define	S_META		2	/* metachar started (M) */
#define	S_META1		3	/* metachar more, regular char (-) */
#define	S_CTRL		4	/* control char started (^) */
#define	S_OCTAL2	5	/* octal digit 2 */
#define	S_OCTAL3	6	/* octal digit 3 */
#define	S_HEX		7	/* mandatory hex digit */
#define	S_HEX1		8	/* http hex digit */
#define	S_HEX2		9	/* http hex digit 2 */
#define	S_MIME1		10	/* mime hex digit 1 */
#define	S_MIME2		11	/* mime hex digit 2 */
#define	S_EATCRNL	12	/* mime eating CRNL */
#define	S_AMP		13	/* seen & */
#define	S_NUMBER	14	/* collecting number */
#define	S_STRING	15	/* collecting string */

#define	isoctal(c)	(((u_char)(c)) >= '0' && ((u_char)(c)) <= '7')
#define	xtod(c)		(isdigit(c) ? (c - '0') : ((tolower(c) - 'a') + 10))
#define	XTOD(c)		(isdigit(c) ? (c - '0') : ((c - 'A') + 10))

/*
 * RFC 1866
 */
static const struct nv {
	char name[7];
	uint8_t value;
} nv[] = {
	{ "AElig",	198 }, /* capital AE diphthong (ligature)  */
	{ "Aacute",	193 }, /* capital A, acute accent  */
	{ "Acirc",	194 }, /* capital A, circumflex accent  */
	{ "Agrave",	192 }, /* capital A, grave accent  */
	{ "Aring",	197 }, /* capital A, ring  */
	{ "Atilde",	195 }, /* capital A, tilde  */
	{ "Auml",	196 }, /* capital A, dieresis or umlaut mark  */
	{ "Ccedil",	199 }, /* capital C, cedilla  */
	{ "ETH",	208 }, /* capital Eth, Icelandic  */
	{ "Eacute",	201 }, /* capital E, acute accent  */
	{ "Ecirc",	202 }, /* capital E, circumflex accent  */
	{ "Egrave",	200 }, /* capital E, grave accent  */
	{ "Euml",	203 }, /* capital E, dieresis or umlaut mark  */
	{ "Iacute",	205 }, /* capital I, acute accent  */
	{ "Icirc",	206 }, /* capital I, circumflex accent  */
	{ "Igrave",	204 }, /* capital I, grave accent  */
	{ "Iuml",	207 }, /* capital I, dieresis or umlaut mark  */
	{ "Ntilde",	209 }, /* capital N, tilde  */
	{ "Oacute",	211 }, /* capital O, acute accent  */
	{ "Ocirc",	212 }, /* capital O, circumflex accent  */
	{ "Ograve",	210 }, /* capital O, grave accent  */
	{ "Oslash",	216 }, /* capital O, slash  */
	{ "Otilde",	213 }, /* capital O, tilde  */
	{ "Ouml",	214 }, /* capital O, dieresis or umlaut mark  */
	{ "THORN",	222 }, /* capital THORN, Icelandic  */
	{ "Uacute",	218 }, /* capital U, acute accent  */
	{ "Ucirc",	219 }, /* capital U, circumflex accent  */
	{ "Ugrave",	217 }, /* capital U, grave accent  */
	{ "Uuml",	220 }, /* capital U, dieresis or umlaut mark  */
	{ "Yacute",	221 }, /* capital Y, acute accent  */
	{ "aacute",	225 }, /* small a, acute accent  */
	{ "acirc",	226 }, /* small a, circumflex accent  */
	{ "acute",	180 }, /* acute accent  */
	{ "aelig",	230 }, /* small ae diphthong (ligature)  */
	{ "agrave",	224 }, /* small a, grave accent  */
	{ "amp",	 38 }, /* ampersand  */
	{ "aring",	229 }, /* small a, ring  */
	{ "atilde",	227 }, /* small a, tilde  */
	{ "auml",	228 }, /* small a, dieresis or umlaut mark  */
	{ "brvbar",	166 }, /* broken (vertical) bar  */
	{ "ccedil",	231 }, /* small c, cedilla  */
	{ "cedil",	184 }, /* cedilla  */
	{ "cent",	162 }, /* cent sign  */
	{ "copy",	169 }, /* copyright sign  */
	{ "curren",	164 }, /* general currency sign  */
	{ "deg",	176 }, /* degree sign  */
	{ "divide",	247 }, /* divide sign  */
	{ "eacute",	233 }, /* small e, acute accent  */
	{ "ecirc",	234 }, /* small e, circumflex accent  */
	{ "egrave",	232 }, /* small e, grave accent  */
	{ "eth",	240 }, /* small eth, Icelandic  */
	{ "euml",	235 }, /* small e, dieresis or umlaut mark  */
	{ "frac12",	189 }, /* fraction one-half  */
	{ "frac14",	188 }, /* fraction one-quarter  */
	{ "frac34",	190 }, /* fraction three-quarters  */
	{ "gt",		 62 }, /* greater than  */
	{ "iacute",	237 }, /* small i, acute accent  */
	{ "icirc",	238 }, /* small i, circumflex accent  */
	{ "iexcl",	161 }, /* inverted exclamation mark  */
	{ "igrave",	236 }, /* small i, grave accent  */
	{ "iquest",	191 }, /* inverted question mark  */
	{ "iuml",	239 }, /* small i, dieresis or umlaut mark  */
	{ "laquo",	171 }, /* angle quotation mark, left  */
	{ "lt",		 60 }, /* less than  */
	{ "macr",	175 }, /* macron  */
	{ "micro",	181 }, /* micro sign  */
	{ "middot",	183 }, /* middle dot  */
	{ "nbsp",	160 }, /* no-break space  */
	{ "not",	172 }, /* not sign  */
	{ "ntilde",	241 }, /* small n, tilde  */
	{ "oacute",	243 }, /* small o, acute accent  */
	{ "ocirc",	244 }, /* small o, circumflex accent  */
	{ "ograve",	242 }, /* small o, grave accent  */
	{ "ordf",	170 }, /* ordinal indicator, feminine  */
	{ "ordm",	186 }, /* ordinal indicator, masculine  */
	{ "oslash",	248 }, /* small o, slash  */
	{ "otilde",	245 }, /* small o, tilde  */
	{ "ouml",	246 }, /* small o, dieresis or umlaut mark  */
	{ "para",	182 }, /* pilcrow (paragraph sign)  */
	{ "plusmn",	177 }, /* plus-or-minus sign  */
	{ "pound",	163 }, /* pound sterling sign  */
	{ "quot",	 34 }, /* double quote  */
	{ "raquo",	187 }, /* angle quotation mark, right  */
	{ "reg",	174 }, /* registered sign  */
	{ "sect",	167 }, /* section sign  */
	{ "shy",	173 }, /* soft hyphen  */
	{ "sup1",	185 }, /* superscript one  */
	{ "sup2",	178 }, /* superscript two  */
	{ "sup3",	179 }, /* superscript three  */
	{ "szlig",	223 }, /* small sharp s, German (sz ligature)  */
	{ "thorn",	254 }, /* small thorn, Icelandic  */
	{ "times",	215 }, /* multiply sign  */
	{ "uacute",	250 }, /* small u, acute accent  */
	{ "ucirc",	251 }, /* small u, circumflex accent  */
	{ "ugrave",	249 }, /* small u, grave accent  */
	{ "uml",	168 }, /* umlaut (dieresis)  */
	{ "uuml",	252 }, /* small u, dieresis or umlaut mark  */
	{ "yacute",	253 }, /* small y, acute accent  */
	{ "yen",	165 }, /* yen sign  */
	{ "yuml",	255 }, /* small y, dieresis or umlaut mark  */
};

/*
 * unvis - decode characters previously encoded by vis
 */
int
unvis(char *cp, int c, int *astate, int flag)
{
	unsigned char uc = (unsigned char)c;
	unsigned char st, ia, is, lc;

/*
 * Bottom 8 bits of astate hold the state machine state.
 * Top 8 bits hold the current character in the http 1866 nv string decoding
 */
#define GS(a)		((a) & 0xff)
#define SS(a, b)	(((uint32_t)(a) << 24) | (b))
#define GI(a)		((uint32_t)(a) >> 24)

	_DIAGASSERT(cp != NULL);
	_DIAGASSERT(astate != NULL);
	st = GS(*astate);

	if (flag & UNVIS_END) {
		switch (st) {
		case S_OCTAL2:
		case S_OCTAL3:
		case S_HEX2:
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case S_GROUND:
			return UNVIS_NOCHAR;
		default:
			return UNVIS_SYNBAD;
		}
	}

	switch (st) {

	case S_GROUND:
		*cp = 0;
		if ((flag & VIS_NOESCAPE) == 0 && c == '\\') {
			*astate = SS(0, S_START);
			return UNVIS_NOCHAR;
		}
		if ((flag & VIS_HTTP1808) && c == '%') {
			*astate = SS(0, S_HEX1);
			return UNVIS_NOCHAR;
		}
		if ((flag & VIS_HTTP1866) && c == '&') {
			*astate = SS(0, S_AMP);
			return UNVIS_NOCHAR;
		}
		if ((flag & VIS_MIMESTYLE) && c == '=') {
			*astate = SS(0, S_MIME1);
			return UNVIS_NOCHAR;
		}
		*cp = c;
		return UNVIS_VALID;

	case S_START:
		switch(c) {
		case '\\':
			*cp = c;
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
			*cp = (c - '0');
			*astate = SS(0, S_OCTAL2);
			return UNVIS_NOCHAR;
		case 'M':
			*cp = (char)0200;
			*astate = SS(0, S_META);
			return UNVIS_NOCHAR;
		case '^':
			*astate = SS(0, S_CTRL);
			return UNVIS_NOCHAR;
		case 'n':
			*cp = '\n';
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case 'r':
			*cp = '\r';
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case 'b':
			*cp = '\b';
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case 'a':
			*cp = '\007';
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case 'v':
			*cp = '\v';
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case 't':
			*cp = '\t';
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case 'f':
			*cp = '\f';
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case 's':
			*cp = ' ';
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case 'E':
			*cp = '\033';
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		case 'x':
			*astate = SS(0, S_HEX);
			return UNVIS_NOCHAR;
		case '\n':
			/*
			 * hidden newline
			 */
			*astate = SS(0, S_GROUND);
			return UNVIS_NOCHAR;
		case '$':
			/*
			 * hidden marker
			 */
			*astate = SS(0, S_GROUND);
			return UNVIS_NOCHAR;
		default:
			if (isgraph(c)) {
				*cp = c;
				*astate = SS(0, S_GROUND);
				return UNVIS_VALID;
			}
		}
		goto bad;

	case S_META:
		if (c == '-')
			*astate = SS(0, S_META1);
		else if (c == '^')
			*astate = SS(0, S_CTRL);
		else 
			goto bad;
		return UNVIS_NOCHAR;

	case S_META1:
		*astate = SS(0, S_GROUND);
		*cp |= c;
		return UNVIS_VALID;

	case S_CTRL:
		if (c == '?')
			*cp |= 0177;
		else
			*cp |= c & 037;
		*astate = SS(0, S_GROUND);
		return UNVIS_VALID;

	case S_OCTAL2:	/* second possible octal digit */
		if (isoctal(uc)) {
			/*
			 * yes - and maybe a third
			 */
			*cp = (*cp << 3) + (c - '0');
			*astate = SS(0, S_OCTAL3);
			return UNVIS_NOCHAR;
		}
		/*
		 * no - done with current sequence, push back passed char
		 */
		*astate = SS(0, S_GROUND);
		return UNVIS_VALIDPUSH;

	case S_OCTAL3:	/* third possible octal digit */
		*astate = SS(0, S_GROUND);
		if (isoctal(uc)) {
			*cp = (*cp << 3) + (c - '0');
			return UNVIS_VALID;
		}
		/*
		 * we were done, push back passed char
		 */
		return UNVIS_VALIDPUSH;

	case S_HEX:
		if (!isxdigit(uc))
			goto bad;
		/*FALLTHROUGH*/
	case S_HEX1:
		if (isxdigit(uc)) {
			*cp = xtod(uc);
			*astate = SS(0, S_HEX2);
			return UNVIS_NOCHAR;
		}
		/*
		 * no - done with current sequence, push back passed char
		 */
		*astate = SS(0, S_GROUND);
		return UNVIS_VALIDPUSH;

	case S_HEX2:
		*astate = S_GROUND;
		if (isxdigit(uc)) {
			*cp = xtod(uc) | (*cp << 4);
			return UNVIS_VALID;
		}
		return UNVIS_VALIDPUSH;

	case S_MIME1:
		if (uc == '\n' || uc == '\r') {
			*astate = SS(0, S_EATCRNL);
			return UNVIS_NOCHAR;
		}
		if (isxdigit(uc) && (isdigit(uc) || isupper(uc))) {
			*cp = XTOD(uc);
			*astate = SS(0, S_MIME2);
			return UNVIS_NOCHAR;
		}
		goto bad;

	case S_MIME2:
		if (isxdigit(uc) && (isdigit(uc) || isupper(uc))) {
			*astate = SS(0, S_GROUND);
			*cp = XTOD(uc) | (*cp << 4);
			return UNVIS_VALID;
		}
		goto bad;

	case S_EATCRNL:
		switch (uc) {
		case '\r':
		case '\n':
			return UNVIS_NOCHAR;
		case '=':
			*astate = SS(0, S_MIME1);
			return UNVIS_NOCHAR;
		default:
			*cp = uc;
			*astate = SS(0, S_GROUND);
			return UNVIS_VALID;
		}

	case S_AMP:
		*cp = 0;
		if (uc == '#') {
			*astate = SS(0, S_NUMBER);
			return UNVIS_NOCHAR;
		}
		*astate = SS(0, S_STRING);
		/*FALLTHROUGH*/

	case S_STRING:
		ia = *cp;		/* index in the array */
		is = GI(*astate);	/* index in the string */
		lc = is == 0 ? 0 : nv[ia].name[is - 1];	/* last character */

		if (uc == ';')
			uc = '\0';

		for (; ia < __arraycount(nv); ia++) {
			if (is != 0 && nv[ia].name[is - 1] != lc)
				goto bad;
			if (nv[ia].name[is] == uc)
				break;
		}

		if (ia == __arraycount(nv))
			goto bad;

		if (uc != 0) {
			*cp = ia;
			*astate = SS(is + 1, S_STRING);
			return UNVIS_NOCHAR;
		}

		*cp = nv[ia].value;
		*astate = SS(0, S_GROUND);
		return UNVIS_VALID;

	case S_NUMBER:
		if (uc == ';')
			return UNVIS_VALID;
		if (!isdigit(uc))
			goto bad;
		*cp += (*cp * 10) + uc - '0';
		return UNVIS_NOCHAR;

	default:
	bad:
		/*
		 * decoder in unknown state - (probably uninitialized)
		 */
		*astate = SS(0, S_GROUND);
		return UNVIS_SYNBAD;
	}
}

/*
 * strnunvisx - decode src into dst
 *
 *	Number of chars decoded into dst is returned, -1 on error.
 *	Dst is null terminated.
 */

int
strnunvisx(char *dst, size_t dlen, const char *src, int flag)
{
	char c;
	char t = '\0', *start = dst;
	int state = 0;

	_DIAGASSERT(src != NULL);
	_DIAGASSERT(dst != NULL);
#define CHECKSPACE() \
	do { \
		if (dlen-- == 0) { \
			errno = ENOSPC; \
			return -1; \
		} \
	} while (/*CONSTCOND*/0)

	while ((c = *src++) != '\0') {
 again:
		switch (unvis(&t, c, &state, flag)) {
		case UNVIS_VALID:
			CHECKSPACE();
			*dst++ = t;
			break;
		case UNVIS_VALIDPUSH:
			CHECKSPACE();
			*dst++ = t;
			goto again;
		case 0:
		case UNVIS_NOCHAR:
			break;
		case UNVIS_SYNBAD:
			errno = EINVAL;
			return -1;
		default:
			_DIAGASSERT(/*CONSTCOND*/0);
			errno = EINVAL;
			return -1;
		}
	}
	if (unvis(&t, c, &state, UNVIS_END) == UNVIS_VALID) {
		CHECKSPACE();
		*dst++ = t;
	}
	CHECKSPACE();
	*dst = '\0';
	return (int)(dst - start);
}

int
strunvisx(char *dst, const char *src, int flag)
{
	return strnunvisx(dst, (size_t)~0, src, flag);
}

int
strunvis(char *dst, const char *src)
{
	return strnunvisx(dst, (size_t)~0, src, 0);
}

int
strnunvis(char *dst, size_t dlen, const char *src)
{
	return strnunvisx(dst, dlen, src, 0);
}
#endif

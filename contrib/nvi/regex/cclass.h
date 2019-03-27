/*	$NetBSD: cclass.h,v 1.2 2008/12/05 22:51:42 christos Exp $ */

/*-
 * Copyright (c) 1992, 1993, 1994 Henry Spencer.
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Henry Spencer of the University of Toronto.
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
 *	@(#)cclass.h	8.2 (Berkeley) 3/16/94
 */

RCHAR_T ALNUM[] = {'a','l','n','u','m',0};
RCHAR_T ALPHA[] = {'a','l','p','h','a',0};
RCHAR_T BLANK[] = {'b','l','a','n','k',0};
RCHAR_T CNTRL[] = {'c','n','t','r','l',0};
RCHAR_T DIGIT[] = {'d','i','g','i','t',0};
RCHAR_T GRAPH[] = {'g','r','a','p','h',0};
RCHAR_T LOWER[] = {'l','o','w','e','r',0};
RCHAR_T PRINT[] = {'p','r','i','n','t',0};
RCHAR_T PUNCT[] = {'p','u','n','c','t',0};
RCHAR_T SPACE[] = {'s','p','a','c','e',0};
RCHAR_T UPPER[] = {'u','p','p','e','r',0};
RCHAR_T XDIGIT[] = {'x','d','i','g','i','t',0};

/* character-class table */
static struct cclass {
	RCHAR_T *name;
	const char *chars;
	const char *multis;
} cclasses[] = {
	{ ALNUM,	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\
0123456789",				"" },
	{ ALPHA,	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
					"" },
	{ BLANK,	" \t",		"" },
	{ CNTRL,	"\007\b\t\n\v\f\r\1\2\3\4\5\6\16\17\20\21\22\23\24\
\25\26\27\30\31\32\33\34\35\36\37\177",	"" },
	{ DIGIT,	"0123456789",	"" },
	{ GRAPH,	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\
0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
					"" },
	{ LOWER,	"abcdefghijklmnopqrstuvwxyz",
					"" },
	{ PRINT,	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\
0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ ",
					"" },
	{ PUNCT,	"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
					"" },
	{ SPACE,	"\t\n\v\f\r ",	"" },
	{ UPPER,	"ABCDEFGHIJKLMNOPQRSTUVWXYZ",
					"" },
	{ XDIGIT,	"0123456789ABCDEFabcdef",
					"" },
	{ NULL,		0,		"" },
};

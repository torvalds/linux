%{
/*-
 * Copyright (c) 2008 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "_elftc.h"

ELFTC_VCSID("$Id: acplex.l 3174 2015-03-27 17:13:41Z emaste $");

#include "acpyacc.h"

#define YY_NO_UNPUT
#if !defined(ELFTC_BROKEN_YY_NO_INPUT)
#define YY_NO_INPUT
#endif

int lineno = 1;

int	yylex(void);

%}

%option nounput
%option noyywrap

%%

ADDLIB|addlib		{ return (ADDLIB); }
ADDMOD|addmod		{ return (ADDMOD); }
CLEAR|clear		{ return (CLEAR); }
CREATE|create		{ return (CREATE); }
DELETE|delete		{ return (DELETE); }
DIRECTORY|directory	{ return (DIRECTORY); }
END|end			{ return (END); }
EXTRACT|extract		{ return (EXTRACT); }
LIST|list		{ return (LIST); }
OPEN|open		{ return (OPEN); }
REPLACE|replace		{ return (REPLACE); }
VERBOSE|verbose		{ return (VERBOSE); }
SAVE|save		{ return (SAVE); }
"("			{ return (LP); }
")"			{ return (RP); }
","			{ return (COMMA); }

[-_A-Za-z0-9/:$.\\]+	{
	yylval.str = strdup(yytext);
	if (yylval.str == NULL)
		err(EXIT_FAILURE, "strdup failed");
	return (FNAME);
}

[ \t]			/* whitespace */
"*".*			/* comment */
";".*			/* comment */
"+\n"			{ lineno++; /* '+' is line continuation char */ }
"\n"			{ lineno++; return (EOL); }

%{
/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config/config.h>

#include <stdio.h>
#include <string.h>

#ifndef HAVE_STRNDUP
#include "strndup.h"
#endif

#include "auditdistd.h"

#include "parse.h"

#define	SECTION_GLOBAL		0
#define	SECTION_SENDER		1
#define	SECTION_RECEIVER	2

int cursection;
int depth;
int lineno;

#define	DP	do { } while (0)
#define	YY_DECL	int yylex(void)
%}

%option noinput
%option nounput
%option noyywrap

%%
certfile		{ DP; return CERTFILE; }
directory		{ DP; return DIRECTORY; }
fingerprint		{ DP; return FINGERPRINT; }
host			{ DP; return HOST; }
keyfile			{ DP; return KEYFILE; }
listen			{ DP; return LISTEN; }
name			{ DP; return NAME; }
password		{ DP; return PASSWORD; }
pidfile			{ DP; return PIDFILE; }
receiver		{ DP; return RECEIVER; }
remote			{ DP; return REMOTE; }
sender			{ DP; return SENDER; }
source			{ DP; return SOURCE; }
timeout			{ DP; return TIMEOUT; }
[0-9]+			{ DP; yylval.num = atoi(yytext); return NUM; }
\"[a-zA-Z0-9_/ !@#\$%\^\&\*\(\)\+\=\|\;\?\,\.\[\]\-\:]*\" { DP; yylval.str = strndup(yytext + 1, strlen(yytext) - 2); return STR; }
\{			{ DP; depth++; return OB; }
\}			{ DP; depth--; return CB; }
#.*$			/* ignore comments */;
\n			{ lineno++; }
[ \t]+			/* ignore whitespace */;
%%

%{
/*
 * Copyright (c) 2004, 2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#undef ECHO

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "sel.h"
#include "sel-gram.h"
unsigned lineno = 1;

static char * handle_string(void);
static int lex_input(char *, int);
static int lex_classic_input(void);

struct hx_expr_input _hx509_expr_input;

#ifndef YY_NULL
#define YY_NULL 0
#endif

#define YY_NO_UNPUT 1

#undef YY_INPUT
#define YY_INPUT(buf,res,maxsize) (res = lex_input(buf, maxsize))

#undef ECHO

%}
%%

TRUE			{ return kw_TRUE; }
FALSE			{ return kw_FALSE; }
AND			{ return kw_AND; }
OR			{ return kw_OR; }
IN			{ return kw_IN; }
TAILMATCH		{ return kw_TAILMATCH; }

[A-Za-z][-A-Za-z0-9_]*	{
			  yylval.string = strdup ((const char *)yytext);
			  return IDENTIFIER;
			}
"\""			{ yylval.string = handle_string(); return STRING; }
\n			{ ++lineno; }
[,.!={}()%]		{ return *yytext; }
[ \t]			;
%%

static char *
handle_string(void)
{
    char x[1024];
    int i = 0;
    int c;
    int quote = 0;
    while((c = input()) != EOF){
	if(quote) {
	    x[i++] = '\\';
	    x[i++] = c;
	    quote = 0;
	    continue;
	}
	if(c == '\n'){
	    _hx509_sel_yyerror("unterminated string");
	    lineno++;
	    break;
	}
	if(c == '\\'){
	    quote++;
	    continue;
	}
	if(c == '\"')
	    break;
	x[i++] = c;
    }
    x[i] = '\0';
    return strdup(x);
}

int
yywrap ()
{
     return 1;
}

static int
lex_input(char *buf, int max_size)
{
    int n;

    n = _hx509_expr_input.length - _hx509_expr_input.offset;
    if (max_size < n)
        n = max_size;
    if (n <= 0)
	return YY_NULL;

    memcpy(buf, _hx509_expr_input.buf + _hx509_expr_input.offset, n);
    _hx509_expr_input.offset += n;

    return n;
}

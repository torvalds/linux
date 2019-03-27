%{
/*
 * Copyright (c) 2004 Kungliga Tekniska Högskolan
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

#undef ECHO

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "slc.h"
#include "slc-gram.h"
unsigned lineno = 1;

static void handle_comment(void);
static char * handle_string(void);

#undef ECHO

%}

%option nounput

%%
[A-Za-z][-A-Za-z0-9_]*	{
			  yylval.string = strdup ((const char *)yytext);
			  return LITERAL;
			}
"\""			{ yylval.string = handle_string(); return STRING; }
\n			{ ++lineno; }
\/\*			{ handle_comment(); }
[={}]			{ return *yytext; }
[ \t]			;
%%

void
error_message (const char *format, ...)
{
     va_list args;

     va_start (args, format);
     fprintf (stderr, "%s:%d: ", filename, lineno);
     vfprintf (stderr, format, args);
     va_end (args);
     error_flag++;
}

void
yyerror (char *s)
{
    error_message("%s\n", s);
}

static void
handle_comment(void)
{
    int c;
    int start_lineno = lineno;
    int level = 1;
    int seen_star = 0;
    int seen_slash = 0;
    while((c = input()) != EOF) {
	if(c == '/') {
	    if(seen_star) {
		if(--level == 0)
		    return;
		seen_star = 0;
		continue;
	    }
	    seen_slash = 1;
	    continue;
	} else if(c == '*') {
	    if(seen_slash) {
		level++;
		seen_star = seen_slash = 0;
		continue;
	    }
	    seen_star = 1;
	    continue;
	}
	seen_star = seen_slash = 0;
	if(c == '\n') {
	    lineno++;
	    continue;
	}
    }
    if(c == EOF)
	error_message("unterminated comment, possibly started on line %d\n", start_lineno);
}

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
	    error_message("unterminated string");
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

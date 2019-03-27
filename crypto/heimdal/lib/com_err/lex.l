%{
/*
 * Copyright (c) 1998 - 2000 Kungliga Tekniska Högskolan
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

/*
 * This is to handle the definition of this symbol in some AIX
 * headers, which will conflict with the definition that lex will
 * generate for it.  It's only a problem for AIX lex.
 */

#undef ECHO

#include "compile_et.h"
#include "parse.h"
#include "lex.h"

static unsigned lineno = 1;
static int getstring(void);

#define YY_NO_UNPUT

#undef ECHO

%}

%option nounput

%%
et			{ return ET; }
error_table		{ return ET; }
ec			{ return EC; }
error_code		{ return EC; }
prefix			{ return PREFIX; }
index			{ return INDEX; }
id			{ return ID; }
end			{ return END; }
[0-9]+			{ yylval.number = atoi(yytext); return NUMBER; }
#[^\n]*			;
[ \t]			;
\n			{ lineno++; }
\"			{ return getstring(); }
[a-zA-Z0-9_]+		{ yylval.string = strdup(yytext); return STRING; }
.			{ return *yytext; }
%%

#ifndef yywrap /* XXX */
int
yywrap ()
{
     return 1;
}
#endif

static int
getstring(void)
{
    char x[128];
    int i = 0;
    int c;
    int quote = 0;
    while(i < sizeof(x) - 1 && (c = input()) != EOF){
	if(quote) {
	    x[i++] = c;
	    quote = 0;
	    continue;
	}
	if(c == '\n'){
	    _lex_error_message("unterminated string");
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
    yylval.string = strdup(x);
    if (yylval.string == NULL)
        err(1, "malloc");
    return STRING;
}

void
_lex_error_message (const char *format, ...)
{
     va_list args;

     va_start (args, format);
     fprintf (stderr, "%s:%d:", filename, lineno);
     vfprintf (stderr, format, args);
     va_end (args);
     numerror++;
}

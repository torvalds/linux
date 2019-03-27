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

#include "compile_et.h"
#include "lex.h"

void yyerror (char *s);
static long name2number(const char *str);

extern char *yytext;

/* This is for bison */

#if !defined(alloca) && !defined(HAVE_ALLOCA)
#define alloca(x) malloc(x)
#endif

#define YYMALLOC malloc
#define YYFREE free

%}

%union {
  char *string;
  int number;
}

%token ET INDEX PREFIX EC ID END
%token <string> STRING
%token <number> NUMBER

%%

file		: /* */
		| header statements
		;

header		: id et
		| et
		;

id		: ID STRING
		{
		    id_str = $2;
		}
		;

et		: ET STRING
		{
		    base_id = name2number($2);
		    strlcpy(name, $2, sizeof(name));
		    free($2);
		}
		| ET STRING STRING
		{
		    base_id = name2number($2);
		    strlcpy(name, $3, sizeof(name));
		    free($2);
		    free($3);
		}
		;

statements	: statement
		| statements statement
		;

statement	: INDEX NUMBER
		{
			number = $2;
		}
		| PREFIX STRING
		{
		    free(prefix);
		    asprintf (&prefix, "%s_", $2);
		    if (prefix == NULL)
			errx(1, "malloc");
		    free($2);
		}
		| PREFIX
		{
		    prefix = realloc(prefix, 1);
		    if (prefix == NULL)
			errx(1, "malloc");
		    *prefix = '\0';
		}
		| EC STRING ',' STRING
		{
		    struct error_code *ec = malloc(sizeof(*ec));

		    if (ec == NULL)
			errx(1, "malloc");

		    ec->next = NULL;
		    ec->number = number;
		    if(prefix && *prefix != '\0') {
			asprintf (&ec->name, "%s%s", prefix, $2);
			if (ec->name == NULL)
			    errx(1, "malloc");
			free($2);
		    } else
			ec->name = $2;
		    ec->string = $4;
		    APPEND(codes, ec);
		    number++;
		}
		| END
		{
			YYACCEPT;
		}
		;

%%

static long
name2number(const char *str)
{
    const char *p;
    long num = 0;
    const char *x = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz0123456789_";
    if(strlen(str) > 4) {
	yyerror("table name too long");
	return 0;
    }
    for(p = str; *p; p++){
	char *q = strchr(x, *p);
	if(q == NULL) {
	    yyerror("invalid character in table name");
	    return 0;
	}
	num = (num << 6) + (q - x) + 1;
    }
    num <<= 8;
    if(num > 0x7fffffff)
	num = -(0xffffffff - num + 1);
    return num;
}

void
yyerror (char *s)
{
     _lex_error_message ("%s\n", s);
}

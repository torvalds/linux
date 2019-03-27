%{
/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#undef ECHO
#include "symbol.h"
#include "asn1parse.h"
#include "lex.h"
#include "gen_locl.h"

static unsigned lineno = 1;

#undef ECHO

static void unterminated(const char *, unsigned);

%}

/* This is for broken old lexes (solaris 10 and hpux) */
%e 2000
%p 5000
%a 5000
%n 1000
%o 10000

%%
ABSENT			{ return kw_ABSENT; }
ABSTRACT-SYNTAX		{ return kw_ABSTRACT_SYNTAX; }
ALL			{ return kw_ALL; }
APPLICATION		{ return kw_APPLICATION; }
AUTOMATIC		{ return kw_AUTOMATIC; }
BEGIN			{ return kw_BEGIN; }
BIT			{ return kw_BIT; }
BMPString		{ return kw_BMPString; }
BOOLEAN			{ return kw_BOOLEAN; }
BY			{ return kw_BY; }
CHARACTER		{ return kw_CHARACTER; }
CHOICE			{ return kw_CHOICE; }
CLASS			{ return kw_CLASS; }
COMPONENT		{ return kw_COMPONENT; }
COMPONENTS		{ return kw_COMPONENTS; }
CONSTRAINED		{ return kw_CONSTRAINED; }
CONTAINING		{ return kw_CONTAINING; }
DEFAULT			{ return kw_DEFAULT; }
DEFINITIONS		{ return kw_DEFINITIONS; }
EMBEDDED		{ return kw_EMBEDDED; }
ENCODED			{ return kw_ENCODED; }
END			{ return kw_END; }
ENUMERATED		{ return kw_ENUMERATED; }
EXCEPT			{ return kw_EXCEPT; }
EXPLICIT		{ return kw_EXPLICIT; }
EXPORTS			{ return kw_EXPORTS; }
EXTENSIBILITY		{ return kw_EXTENSIBILITY; }
EXTERNAL		{ return kw_EXTERNAL; }
FALSE			{ return kw_FALSE; }
FROM			{ return kw_FROM; }
GeneralString		{ return kw_GeneralString; }
GeneralizedTime		{ return kw_GeneralizedTime; }
GraphicString		{ return kw_GraphicString; }
IA5String		{ return kw_IA5String; }
IDENTIFIER		{ return kw_IDENTIFIER; }
IMPLICIT		{ return kw_IMPLICIT; }
IMPLIED			{ return kw_IMPLIED; }
IMPORTS			{ return kw_IMPORTS; }
INCLUDES		{ return kw_INCLUDES; }
INSTANCE		{ return kw_INSTANCE; }
INTEGER			{ return kw_INTEGER; }
INTERSECTION		{ return kw_INTERSECTION; }
ISO646String		{ return kw_ISO646String; }
MAX			{ return kw_MAX; }
MIN			{ return kw_MIN; }
MINUS-INFINITY		{ return kw_MINUS_INFINITY; }
NULL			{ return kw_NULL; }
NumericString		{ return kw_NumericString; }
OBJECT			{ return kw_OBJECT; }
OCTET			{ return kw_OCTET; }
OF			{ return kw_OF; }
OPTIONAL		{ return kw_OPTIONAL; }
ObjectDescriptor	{ return kw_ObjectDescriptor; }
PATTERN			{ return kw_PATTERN; }
PDV			{ return kw_PDV; }
PLUS-INFINITY		{ return kw_PLUS_INFINITY; }
PRESENT			{ return kw_PRESENT; }
PRIVATE			{ return kw_PRIVATE; }
PrintableString		{ return kw_PrintableString; }
REAL			{ return kw_REAL; }
RELATIVE_OID		{ return kw_RELATIVE_OID; }
SEQUENCE		{ return kw_SEQUENCE; }
SET			{ return kw_SET; }
SIZE			{ return kw_SIZE; }
STRING			{ return kw_STRING; }
SYNTAX			{ return kw_SYNTAX; }
T61String		{ return kw_T61String; }
TAGS			{ return kw_TAGS; }
TRUE			{ return kw_TRUE; }
TYPE-IDENTIFIER		{ return kw_TYPE_IDENTIFIER; }
TeletexString		{ return kw_TeletexString; }
UNION			{ return kw_UNION; }
UNIQUE			{ return kw_UNIQUE; }
UNIVERSAL		{ return kw_UNIVERSAL; }
UTCTime			{ return kw_UTCTime; }
UTF8String		{ return kw_UTF8String; }
UniversalString		{ return kw_UniversalString; }
VideotexString		{ return kw_VideotexString; }
VisibleString		{ return kw_VisibleString; }
WITH			{ return kw_WITH; }
[-,;{}()|]		{ return *yytext; }
"["			{ return *yytext; }
"]"			{ return *yytext; }
::=			{ return EEQUAL; }
--			{
			    int c, start_lineno = lineno;
			    int f = 0;
			    while((c = input()) != EOF) {
				if(f && c == '-')
				    break;
				if(c == '-') {
				    f = 1;
				    continue;
				}
				if(c == '\n') {
				    lineno++;
				    break;
				}
				f = 0;
			    }
			    if(c == EOF)
				unterminated("comment", start_lineno);
			}
\/\*			{
			    int c, start_lineno = lineno;
			    int level = 1;
			    int seen_star = 0;
			    int seen_slash = 0;
			    while((c = input()) != EOF) {
				if(c == '/') {
				    if(seen_star) {
					if(--level == 0)
					    break;
					seen_star = 0;
					continue;
				    }
				    seen_slash = 1;
				    continue;
				}
				if(seen_star && c == '/') {
				    if(--level == 0)
					break;
				    seen_star = 0;
				    continue;
				}
				if(c == '*') {
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
				unterminated("comment", start_lineno);
			}
"\""			{
			    int start_lineno = lineno;
			    int c;
			    char buf[1024];
			    char *p = buf;
			    int f = 0;
			    int skip_ws = 0;

			    while((c = input()) != EOF) {
				if(isspace(c) && skip_ws) {
				    if(c == '\n')
					lineno++;
				    continue;
				}
				skip_ws = 0;

				if(c == '"') {
				    if(f) {
					*p++ = '"';
					f = 0;
				    } else
					f = 1;
				    continue;
				}
				if(f == 1) {
				    unput(c);
				    break;
				}
				if(c == '\n') {
				    lineno++;
				    while(p > buf && isspace((unsigned char)p[-1]))
					p--;
				    skip_ws = 1;
				    continue;
				}
				*p++ = c;
			    }
			    if(c == EOF)
				unterminated("string", start_lineno);
			    *p++ = '\0';
			    fprintf(stderr, "string -- %s\n", buf);
			    yylval.name = estrdup(buf);
			    return STRING;
			}

-?0x[0-9A-Fa-f]+|-?[0-9]+ { char *e, *y = yytext;
			  yylval.constant = strtol((const char *)yytext,
						   &e, 0);
			  if(e == y)
			    lex_error_message("malformed constant (%s)", yytext);
			  else
			    return NUMBER;
			}
[A-Za-z][-A-Za-z0-9_]*	{
			  yylval.name =  estrdup ((const char *)yytext);
			  return IDENTIFIER;
			}
[ \t]			;
\n			{ ++lineno; }
\.\.\.			{ return ELLIPSIS; }
\.\.			{ return RANGE; }
.			{ lex_error_message("Ignoring char(%c)\n", *yytext); }
%%

#ifndef yywrap /* XXX */
int
yywrap ()
{
     return 1;
}
#endif

void
lex_error_message (const char *format, ...)
{
    va_list args;

    va_start (args, format);
    fprintf (stderr, "%s:%d: ", get_filename(), lineno);
    vfprintf (stderr, format, args);
    va_end (args);
    error_flag++;
}

static void
unterminated(const char *type, unsigned start_lineno)
{
    lex_error_message("unterminated %s, possibly started on line %d\n", type, start_lineno);
}

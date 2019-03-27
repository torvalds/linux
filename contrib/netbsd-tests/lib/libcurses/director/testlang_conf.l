%{
/*	$NetBSD: testlang_conf.l,v 1.7 2013/11/21 11:06:04 blymn Exp $ 	*/

/*-
 * Copyright 2009 Brett Lymn <blymn@NetBSD.org>
 *
 * All rights reserved.
 *
 * This code has been donated to The NetBSD Foundation by the Author.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */

#include <curses.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <err.h>
#include "returns.h"
#include "testlang_parse.h"

#define MAX_INCLUDES 32 /* limit for the number of nested includes */

int yylex(void);

extern size_t line;
extern char *include_path; 	/* from director.c */
extern char *cur_file;		/* from director.c */

static int include_stack[MAX_INCLUDES];
static char *include_files[MAX_INCLUDES];
static int include_ptr = 0;

static char *
dequote(const char *s, size_t *len)
{
	const unsigned char *p;
	char *buf, *q;

	*len = 0;
	p = (const unsigned char *)s;
	while (*p) {
		if (*p == '\\' && *(p+1)) {
			if (isdigit(*(p+1)) && *(p+2) && isdigit(*(p+2)) &&
			    *(p+3) && isdigit(*(p+3)))
				p += 3;
			else
				++p;
		}
		++(*len);
		++p;
	}

	buf = malloc(*len + 1);
	if (buf == NULL)
		return NULL;

	p = (const unsigned char *)s;
	q = buf;
	while (*p) {
		if (*p == '\\' && *(p+1)) {
			++p;
			if (isdigit(*p)) {
				if (*(p+1) && isdigit(*(p+1)) && *(p+2) &&
				    isdigit(*(p+2))) {
					*q++ = ((*p - '0') * 8 + (*(p+1) - '0')) * 8 + (*(p+2) - '0');
					p += 3;
				} else {
					*q++ = *p++;
				}
			} else {
				switch (*p) {
				case 'e':
					/* escape */
					*q++ = '\e';
					p++;
					break;

				case 'n':
					/* newline */
					*q++ = '\n';
					p++;
					break;

				case 'r':
					/* carriage return */
					*q++ = '\r';
					p++;
					break;

				case 't':
					/* tab */
					*q++ = '\t';
					p++;
					break;

				case '\\':
					/* backslash */
					*q++ = '\\';
					p++;
					break;

				default:
					*q++ = *p++;
				}
			}
		} else
			*q++ = *p++;
	}
	*q++ = '\0';

	return buf;
}
%}

HEX		0[xX][0-9a-zA-Z]+
STRING		[0-9a-z!#-&(-^ \t%._\\]+
numeric		[-0-9]+
PCHAR           (\\.|[^ \t\n])
ASSIGN		[aA][sS][sS][iI][gG][nN]
CALL2		[cC][aA][lL][lL]2
CALL3		[cC][aA][lL][lL]3
CALL4		[cC][aA][lL][lL]4
CALL		[cC][aA][lL][lL]
CHECK		[cC][hH][eE][cC][kK]
DELAY		[dD][eE][lL][aA][yY]
INPUT		[iI][nN][pP][uU][tT]
NOINPUT		[nN][oO][iI][nN][pP][uU][tT]
OK_RET		[oO][kK]
ERR_RET		[eE][rR][rR]
COMPARE		[cC][oO][mM][pP][aA][rR][eE]
COMPAREND	[cC][oO][mM][pP][aA][rR][eE][Nn][Dd]
FILENAME	[A-Za-z0-9.][A-Za-z0-9./_-]+
VARNAME		[A-Za-z][A-Za-z0-9_-]+
NULL_RET	NULL
NON_NULL	NON_NULL
BYTE		BYTE
OR		\|
LHB		\(
RHB		\)

%x incl
%option noinput nounput

%%

include		BEGIN(incl);

<incl>[ \t]*      /* eat the whitespace */
<incl>[^ \t\n]+   { /* got the include file name */
		char inc_file[MAXPATHLEN];

		if (include_ptr > MAX_INCLUDES) {
			fprintf(stderr,
				"Maximum number of nested includes exceeded "
				"at line %zu of file %s\n", line, cur_file);
				exit(2);
		}

		if (yytext[0] != '/') {
			if (strlcpy(inc_file, include_path, sizeof(inc_file))
			    >= sizeof(inc_file))
				err(2, "CHECK_PATH too long");
			if ((include_path[strlen(include_path) - 1] != '/') &&
			    ((strlcat(inc_file, "/", sizeof(inc_file))
			    >= sizeof(inc_file))))
				err(2, "Could not append / to include file path");
		} else {
			inc_file[0] = '\0';
		}

		if (strlcat(inc_file, yytext, sizeof(inc_file))
		    >= sizeof(inc_file))
			err(2, "Path to include file path overflowed");

		yyin = fopen(inc_file, "r" );

		if (!yyin)
			err(1, "Error opening %s", inc_file);

		yypush_buffer_state(yy_create_buffer(yyin, YY_BUF_SIZE));

		include_stack[include_ptr] = line;
		include_files[include_ptr++] = cur_file;
		cur_file = strdup(inc_file);
		if (cur_file == NULL)
			err(2, "Cannot allocate new include file string");
		line = 0;
		BEGIN(INITIAL);
	}

<<EOF>>	{
		yypop_buffer_state();

		if ( !YY_CURRENT_BUFFER )
		{
			yyterminate();
		}

		if (--include_ptr < 0)
			err(2, "Include stack underflow");

		free(cur_file);
		cur_file = include_files[include_ptr];
		line = include_stack[include_ptr];
	}

{ASSIGN}	{
			return ASSIGN;
		}

{CALL2}		{
			return CALL2;
		}

{CALL3}		{
			return CALL3;
		}

{CALL4}		{
			return CALL4;
		}

{CALL}		{
			return CALL;
		}

{CHECK}		{
			return CHECK;
		}

{DELAY}		{
			return DELAY;
		}

{INPUT}		{
			return INPUT;
		}

{NOINPUT}		{
			return NOINPUT;
		}

{COMPARE}	{
			return COMPARE;
		}

{COMPAREND}	{
			return COMPAREND;
		}

{NON_NULL}	{
			return NON_NULL;
		}

{NULL_RET}		{
			return NULL_RET;
		}

{OK_RET}		{
			return OK_RET;
		}

{ERR_RET}		{
			return ERR_RET;
		}

{OR}		{
			return OR;
		}

{LHB}		{
			return LHB;
		}

{RHB}		{
			return RHB;
		}

{HEX}		{
			/* Hex value, convert to decimal and return numeric */
			unsigned long val;

			if (sscanf(yytext, "%lx", &val) != 1)
				err(1, "Bad hex conversion");

			asprintf(&yylval.string, "%ld", val);
			return numeric;
		}


{numeric}		{
			if ((yylval.string = strdup(yytext)) == NULL)
				err(1, "Cannot allocate numeric string");
			return numeric;
}

{VARNAME}	{
			if ((yylval.string = strdup(yytext)) == NULL)
				err(1, "Cannot allocate string for varname");
			return VARNAME;
		}

{FILENAME}	{
			size_t len;

			if ((yylval.string = dequote(yytext, &len)) == NULL)
				err(1, "Cannot allocate filename string");
			return FILENAME;
		}

	/* path */
\/{PCHAR}+	{
			size_t len;
			if ((yylval.string = dequote(yytext, &len)) == NULL)
				err(1, "Cannot allocate string");
			return PATH;
		}

\'{STRING}\' 	{
			char *p;
			size_t len;

			if ((yylval.retval = malloc(sizeof(returns_t))) == NULL)
				err(1, "Cannot allocate return struct");
			p = yytext;
			p++; /* skip the leading ' */
			if ((yylval.retval->return_value = dequote(p, &len))
			     == NULL)
				err(1, "Cannot allocate string");

			yylval.retval->return_type = ret_byte;
			/* trim trailing ' */
			yylval.retval->return_len = len - 1;
			return BYTE;
		}

\`{STRING}\` 	{
			char *p, *str;
			size_t len, chlen;
			size_t i;
			chtype *rv;

			if ((yylval.retval = malloc(sizeof(returns_t))) == NULL)
				err(1, "Cannot allocate return struct");
			p = yytext;
			p++; /* skip the leading ' */
			if ((str = dequote(p, &len)) == NULL)
				err(1, "Cannot allocate string");
			len--; /* trim trailing ` */
			if ((len % 2) != 0)
				len--;

			chlen = ((len / 2) + 1) * sizeof(chtype);
			if ((yylval.retval->return_value = malloc(chlen))
			    == NULL)
				err(1, "Cannot allocate chtype array");

			rv = yylval.retval->return_value;
			for (i = 0; i < len; i += 2)
				*rv++ = (str[i] << 8) | str[i+1];
			*rv = __NORMAL | '\0'; /* terminates chtype array */	
			yylval.retval->return_type = ret_byte;
			yylval.retval->return_len = chlen;
			return BYTE;
		}

\"{STRING}\" 	{
			char *p;
			size_t len;

			p = yytext;
			p++; /* skip the leading " */
			if ((yylval.string = dequote(p, &len)) == NULL)
				err(1, "Cannot allocate string");

			/* remove trailing " */
			yylval.string[len - 1] = '\0';
			return STRING;
		}

\${VARNAME}	{
			char *p;

			p = yytext;
			p++; /* skip $ before var name */
			if ((yylval.string = strdup(p)) == NULL)
				err(1, "Cannot allocate string for varname");
			return VARIABLE;
		}
	
	/* comments, white-outs */
[ \t\r]		|
#.*		;
^#.*\n		|
#.*\n		|
\\\n		|
^\n		{ 
line++; }

	/* eol on a line with data. need to process, return eol */
\n		{
			line++;
			return EOL;
		}

.		{
		}

%%

int
yywrap(void)
{
	return 1;
}

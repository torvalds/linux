/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *
 * File: am-utils/amd/conf_parse.y
 *
 */

%{
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

extern char *yytext;
extern int ayylineno;
extern int yylex(void);

static int yyerror(const char *s);
static int retval;
static char *header_section = NULL; /* start with no header section */

#define YYDEBUG 1

#define PARSE_DEBUG 0

#if PARSE_DEBUG
# define dprintf(f,s) fprintf(stderr, (f), ayylineno, (s))
# define amu_return(v)
#else /* not PARSE_DEBUG */
# define dprintf(f,s)
# define amu_return(v) return((v))
#endif /* not PARSE_DEBUG */

%}

%union {
char *strtype;
}

%token LEFT_BRACKET RIGHT_BRACKET EQUAL
%token NEWLINE
%token <strtype> NONWS_STRING
%token <strtype> NONWSEQ_STRING
%token <strtype> QUOTED_NONWSEQ_STRING

%start file
%%

/****************************************************************************/
file		: { yydebug = PARSE_DEBUG; } newlines map_sections
		| { yydebug = PARSE_DEBUG; } map_sections
		;

newlines	: NEWLINE
		| NEWLINE newlines
		;

map_sections	: map_section
		| map_section map_sections
		;

map_section	: sec_header kv_pairs
		;

sec_header	: LEFT_BRACKET NONWS_STRING RIGHT_BRACKET NEWLINE
		{
		  if (yydebug)
		    fprintf(stderr, "sec_header1 = \"%s\"\n", $2);
		  header_section = $2;
		}
		;

kv_pairs	: kv_pair
		| kv_pair kv_pairs
		;

kv_pair		: NONWS_STRING EQUAL NONWS_STRING NEWLINE
		{
		  if (yydebug)
		    fprintf(stderr,"parse1: key=\"%s\", val=\"%s\"\n", $1, $3);
		  retval = set_conf_kv(header_section, $1, $3);
		  if (retval != 0) {
		    yyerror("syntax error");
		    YYABORT;
		  }
		}
		| NONWS_STRING EQUAL NONWSEQ_STRING NEWLINE
		{
		  if (yydebug)
		    fprintf(stderr,"parse2: key=\"%s\", val=\"%s\"\n", $1, $3);
		  retval = set_conf_kv(header_section, $1, $3);
		  if (retval != 0) {
		    yyerror("syntax error");
		    YYABORT;
		  }
		}
		| NONWS_STRING EQUAL QUOTED_NONWSEQ_STRING NEWLINE
		{
		  if (yydebug)
		    fprintf(stderr,"parse3: key=\"%s\", val=\"%s\"\n", $1, $3);
		  retval = set_conf_kv(header_section, $1, $3);
		  if (retval != 0) {
		    yyerror("syntax error");
		    YYABORT;
		  }
		}
		| NEWLINE
		;

/****************************************************************************/
%%

static int
yyerror(const char *s)
{
  fprintf(stderr, "AMDCONF: %s on line %d (section %s)\n",
	  s, ayylineno,
	  (header_section ? header_section : "null"));
  exit(1);
  return 1;	/* to full compilers that insist on a return statement */
}

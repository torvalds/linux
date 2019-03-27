%{
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
 * File: am-utils/amd/conf_tok.l
 *
 */

/*
 * Lexical analyzer for AMD configuration parser.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
/*
 * Some systems include a definition for the macro ECHO in <sys/ioctl.h>,
 * and their (bad) version of lex defines it too at the very beginning of
 * the generated lex.yy.c file (before it can be easily undefined),
 * resulting in a conflict.  So undefine it here before needed.
 * Luckily, it does not appear that this macro is actually used in the rest
 * of the generated lex.yy.c file.
 */
#ifdef ECHO
# undef ECHO
#endif /* ECHO */
#include <am_defs.h>
#include <amd.h>
#include <conf_parse.h>
/* and once again undefine this, just in case */
#ifdef ECHO
# undef ECHO
#endif /* ECHO */

/*
 * There are some things that need to be defined only if using GNU flex.
 * These must not be defined if using standard lex
 */
#ifdef FLEX_SCANNER
# ifndef ECHO
#  define ECHO __IGNORE(fwrite( yytext, yyleng, 1, yyout ))
# endif /* not ECHO */
#endif /* FLEX_SCANNER */

int ayylineno = 0;

int yylex(void);
/*
 * some systems such as DU-4.x have a different GNU flex in /usr/bin
 * which automatically generates yywrap macros and symbols.  So I must
 * distinguish between them and when yywrap is actually needed.
 */
#if !defined(yywrap) || defined(yylex)
int yywrap(void);
#endif /* not yywrap or yylex */

#define TOK_DEBUG 0

#if TOK_DEBUG
# define dprintf(f,s) fprintf(stderr, (f), ayylineno, (s))
# define amu_return(v)
#else /* not TOK_DEBUG */
# define dprintf(f,s)
# define amu_return(v) return((v))
#endif /* not TOK_DEBUG */

/* no need to use yywrap() */
#define YY_SKIP_YYWRAP

%}

/* This option causes Solaris lex to fail.  Use flex.  See BUGS file */
/* no need to use yyunput() */
%option nounput
%option noinput

/* allocate more output slots so lex scanners don't run out of mem */
%o 1024

DIGIT		[0-9]
ALPHA		[A-Za-z]
ALPHANUM	[A-Za-z0-9]
SYMBOL		[A-Za-z0-9_-]
PATH		[A-Za-z0-9_-/]
NONWSCHAR	[^ \t\n\[\]=]
NONWSEQCHAR	[^ \t\n\[\]]
NONNL		[^\n]
NONQUOTE	[^\"]

%%

\n			{
			ayylineno++;
			amu_return(NEWLINE);
			}

\[			{
			dprintf("%8d: Left bracket \"%s\"\n", yytext);
			conf_lval.strtype = xstrdup(yytext);
			amu_return(LEFT_BRACKET);
			}

\]			{
			dprintf("%8d: Right bracket \"%s\"\n", yytext);
			conf_lval.strtype = xstrdup(yytext);
			amu_return(RIGHT_BRACKET);
			}

=			{
			dprintf("%8d: Equal \"%s\"\n", yytext);
			conf_lval.strtype = xstrdup(yytext);
			amu_return(EQUAL);
			}

[ \t]*			{
			dprintf("%8d: Whitespace \"%s\"\n", yytext);
			}
"#"[^\n]*\n		{
			/* a comment line includes the terminating \n */
			ayylineno++;
			yytext[strlen((char *)yytext)-1] = '\0';
			dprintf("%8d: Comment \"%s\"\n", yytext);
			}

{NONWSCHAR}{NONWSCHAR}*	{
			dprintf("%8d: Non-WS string \"%s\"\n", yytext);
			conf_lval.strtype = xstrdup(yytext);
			amu_return(NONWS_STRING);
			}

\"{NONQUOTE}{NONQUOTE}*\"	{
			dprintf("%8d: QUOTED-Non-WS-EQ string \"%s\"\n", yytext);
			/* must strip quotes */
			yytext[strlen((char *)yytext)-1] = '\0';
			conf_lval.strtype = xstrdup(&yytext[1]);
			amu_return(QUOTED_NONWSEQ_STRING);
			}

{NONWSEQCHAR}{NONWSEQCHAR}*	{
			dprintf("%8d: Non-WS-EQ string \"%s\"\n", yytext);
			conf_lval.strtype = xstrdup(yytext);
			amu_return(NONWSEQ_STRING);
			}

%%

/*
 * some systems such as DU-4.x have a different GNU flex in /usr/bin
 * which automatically generates yywrap macros and symbols.  So I must
 * distinguish between them and when yywrap is actually needed.
 */
#if !defined(yywrap) || defined(yylex)
int yywrap(void)
{
  return 1;
}
#endif /* not yywrap or yylex */

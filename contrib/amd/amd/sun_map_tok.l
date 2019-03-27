%{
/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 2005 Daniel P. Ottavio
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
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
 * File: am-utils/amd/sun_map_tok.l
 *
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
#include <sun_map_parse.h>
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

int yylex(void);
int sun_map_error(const char *);

/*
 * We need to configure lex to parse from a string
 * instead of a file. Each version of lex has it's
 * own way of doing this (sigh).
 */

/* assign the buffer to parse */
void sun_map_tok_setbuff(const char* buff);

/* buffer that contains the string to parse */
const char *sun_map_tok_buff = NULL;

#ifdef FLEX_SCANNER
/*
 * The flex scanner uses the YY_INPUT to parse the input.
 * We need to redefine it so that it can parse strings.
 * In addition to the above string buffer we need to have
 * a position pointer and a end pointer.
 */

/* current position of the buffer */
const char *sun_map_tok_pos = NULL;

/* size of the buffer */
const char *sun_map_tok_end = NULL;

/* copies the current position + maxsize into buff */
int sun_map_input(char *buff, int maxsize);

# undef YY_INPUT
# define YY_INPUT(buff,result,maxsize) (result = sun_map_input(buff,maxsize))

#else
/*
 * If this is not Flex than fall back to an AT&T style lex.
 * We can parse strings by redefining input and unput.
 */
#undef input
#undef unput
#define input()  (*(char *)sun_map_tok_buff++)
#define unput(c) (*(char *)--sun_map_tok_buff = c)

#endif /* FLEX_SCANNER */

/*
 * some systems such as DU-4.x have a different GNU flex in /usr/bin
 * which automatically generates yywrap macros and symbols.  So I must
 * distinguish between them and when yywrap is actually needed.
 */
#if !defined(yywrap) || defined(yylex)
int yywrap(void);
#endif /* not yywrap or yylex */

/* no need to use yywrap() */
#define YY_SKIP_YYWRAP

int sun_map_line = 1;
int sun_map_tokpos = 1;

%}

/* This option causes Solaris lex to fail.  Use flex.  See BUGS file */
/* no need to use yyunput() */
%option nounput
%option noinput

/* allocate more output slots so lex scanners don't run out of mem */
%o 1024

WORD_REX       [A-Za-z0-9_/&\.$=]+[A-Za-z0-9_/&\.$=-]*
COMMENT_REX    ^#.*\n
WSPACE_REX     [ \t]*
NEWLINE_REX    [ \t]*\n
CONTINUE_REX   "\\"\n

%%

{WORD_REX}      {
                  sun_map_tokpos += yyleng;
                  xstrlcpy((char *)sun_map_lval.strval,(const char *)yytext,sizeof(sun_map_lval.strval));
                  return WORD;
                }

{WSPACE_REX}    {
                  sun_map_tokpos += yyleng;
                  return WSPACE;
                }

{NEWLINE_REX}   {
                  sun_map_tokpos = 0;
                  sun_map_line++;
                  return NEWLINE;
                }

{CONTINUE_REX}  {
                  sun_map_tokpos = 0;
                  sun_map_line++;
                }

{COMMENT_REX}   {
                  sun_map_line++;
                }

.               {
                  return yytext[0];
                }

%%


int
sun_map_error(const char* s)
{
  return 1;
}

#ifdef FLEX_SCANNER
void
sun_map_tok_setbuff(const char* buff)
{
  sun_map_tok_end = buff + strlen(buff);
  sun_map_tok_pos = buff;
  sun_map_tok_buff = buff;
}


int
sun_map_input(char *buff, int maxsize)
{
  int size = MIN(maxsize, (sun_map_tok_end - sun_map_tok_pos));
  if (size > 0) {
    memcpy(buff,sun_map_tok_pos,size);
    sun_map_tok_pos += size;
  }

  return size;
}
#else
void
sun_map_tok_setbuff(const char* buff)
{
  sun_map_tok_buff = buff;
}

#endif /* FLEX_SCANNER */

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

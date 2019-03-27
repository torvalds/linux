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
 * File: am-utils/fsinfo/fsi_lex.l
 *
 */

/*
 * Lexical analyzer for fsinfo.
 * TODO: Needs rewriting.
 */

static int ayylineno;

#ifdef FLEX_SCANNER
# define INIT_STATE {				\
		switch ((yy_start - 1) / 2) {	\
		case 0:				\
			BEGIN F;		\
			break;			\
		}				\
}

#else /* not FLEX_SCANNER */

/*
 * Using old lex...
 */
# define	INIT_STATE {			\
		switch (yybgin - yysvec - 1) {	\
		case 0:				\
			BEGIN F;		\
			break;			\
		}				\
}

#endif /* end FLEX_SCANNER */

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
#include <fsi_data.h>
#include <fsinfo.h>
#include <fsi_gram.h>
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

/*
 * some systems such as DU-4.x have a different GNU flex in /usr/bin
 * which automatically generates yywrap macros and symbols.  So I must
 * distinguish between them and when yywrap is actually needed.
 */
#if !defined(yywrap) || defined(yylex)
int yywrap(void);
#endif /* not yywrap or yylex */

int fsi_error(const char *, ...);

YYSTYPE yylval;
static char *fsi_filename;
static char *optr;
static char ostr[1024];
static int find_resword(char *);
static int quoted;

struct r {
  char *rw;
  int tok;
} rr[] = {
  { "->", tEQ },
  { "arch", tARCH },
  { "as", tAS },
  { "automount", tAUTOMOUNT },
  { "cluster", tCLUSTER },
  { "config", tCONFIG },
  { "direct", tDIRECT },
  { "dumpset", tDUMPSET },
  { "exportfs", tEXPORTFS },
  { "freq", tFREQ },
  { "from", tFROM },
  { "fs", tFS },
  { "fstype", tFSTYPE },
  { "host", tHOST },
  { "hwaddr", tHWADDR },
  { "inaddr", tINADDR },
  { "localhost", tLOCALHOST },
  { "log", tLOG },
  { "mount", tMOUNT },
  { "netif", tNETIF },
  { "netmask", tNETMASK },
  { "nfsalias", tNFSEQ },
  { "opts", tOPTS },
  { "os", tOS },
  { "passno", tPASSNO },
  { "sel", tSEL },
  { "volname", tVOLNAME },
  { NULL, 0 },
};
#define	NRES_WORDS (sizeof(rr)/sizeof(rr[0])-1)

%}

/* This option causes Solaris lex to fail.  Use flex.  See BUGS file */
/* no need to use yyunput() */
%option nounput
%option noinput

/* allocate more output slots so lex scanners don't run out of mem */
%o 1024

%start F Q

%%
		INIT_STATE;	/* witchcraft */

<F>[^ \t\n"={}]+	{ return find_resword(yytext); } /* dummy " */
<F>[ \t]		;
<F>"\n"			{ ayylineno++; }
<F>[={}]		{ return *yytext; }

<F>\"			{ BEGIN Q; optr = ostr; quoted = 1; }
<Q>\n			{ ayylineno++; fsi_error("\" expected"); BEGIN F; }
<Q>\\b			{ *optr++ = '\b'; /* escape */ }
<Q>\\t			{ *optr++ = '\t'; /* escape */ }
<Q>\\\"			{ *optr++ = '\"'; /* escape */ }
<Q>\\\\			{ *optr++ = '\\'; /* escape */ }
<Q>\\\n			{ ayylineno++; /* continue */ }
<Q>\\r			{ *optr++ = '\r'; /* escape */ }
<Q>\\n			{ *optr++ = '\n'; /* escape */ }
<Q>\\f			{ *optr++ = '\f'; /* escape */ }
<Q>"\\ "		{ *optr++ = ' '; /* force space */ }
<Q>\\.			{ fsi_error("Unknown \\ sequence"); }
<Q>([ \t]|"\\\n"){2,}	{ char *p = (char *) yytext-1; while ((p = strchr(p+1, '\n'))) ayylineno++; }
<Q>\"			{ BEGIN F; quoted = 0;
				*optr = '\0';
				yylval.s = xstrdup(ostr);
				return tSTR;
			}
<Q>.			{ *optr++ = *yytext; }

%%


static int
find_resword(char *s)
{
  int tok = 0;
  int l = 0, m = NRES_WORDS/2, h = NRES_WORDS-1;
  int rc = 0;

  m = NRES_WORDS/2;

#define FSTRCMP(p, q) ((*(p) == *(q)) ? strcmp((p)+1, (q)+1) : *(p) - *(q))

  while ((l <= h) && (rc = FSTRCMP(s, rr[m].rw))) {
    if (rc < 0)
      h = m - 1;
    else
      l = m + 1;
    m = (h + l) / 2;
  }

  if (rc == 0)
    tok = rr[m].tok;

  switch (tok) {
  case tLOCALHOST:
    s = "${host}";
    /*FALLTHROUGH*/
  case 0:
    yylval.s = xstrdup(s);
    tok = tSTR;
    /*FALLTHROUGH*/
  default:
    return tok;
  }
}


int
fsi_error(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  col_cleanup(0);
  fprintf(stderr, "%s:%d: ", fsi_filename ? fsi_filename : "/dev/stdin", ayylineno);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  parse_errors++;
  va_end(ap);
  return 0;
}


ioloc *
current_location(void)
{
  ioloc *ip = CALLOC(struct ioloc);
  ip->i_line = ayylineno;
  ip->i_file = fsi_filename;
  return ip;
}


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

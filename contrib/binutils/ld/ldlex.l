%{

/* Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2007
   Free Software Foundation, Inc.

   This file is part of GLD, the Gnu Linker.

   GLD is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GLD is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GLD; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/*
This was written by steve chamberlain
                    sac@cygnus.com
*/

#include "sysdep.h"
#include "bfd.h"
#include "safe-ctype.h"
#include "bfdlink.h"
#include "ld.h"
#include "ldmisc.h"
#include "ldexp.h"
#include "ldlang.h"
#include <ldgram.h>
#include "ldfile.h"
#include "ldlex.h"
#include "ldmain.h"
#include "libiberty.h"

/* The type of top-level parser input.
   yylex and yyparse (indirectly) both check this.  */
input_type parser_input;

/* Line number in the current input file.
   (FIXME Actually, it doesn't appear to get reset for each file?)  */
unsigned int lineno = 1;

/* The string we are currently lexing, or NULL if we are reading a
   file.  */
const char *lex_string = NULL;

/* Support for flex reading from more than one input file (stream).
   `include_stack' is flex's input state for each open file;
   `file_name_stack' is the file names.  `lineno_stack' is the current
   line numbers.

   If `include_stack_ptr' is 0, we haven't started reading anything yet.
   Otherwise, stack elements 0 through `include_stack_ptr - 1' are valid.  */

#undef YY_INPUT
#define YY_INPUT(buf,result,max_size) yy_input (buf, &result, max_size)

#define MAX_INCLUDE_DEPTH 10
static YY_BUFFER_STATE include_stack[MAX_INCLUDE_DEPTH];
static const char *file_name_stack[MAX_INCLUDE_DEPTH];
static unsigned int lineno_stack[MAX_INCLUDE_DEPTH];
static unsigned int include_stack_ptr = 0;
static int vers_node_nesting = 0;

static void yy_input (char *, yy_size_t *, yy_size_t);
static void comment (void);
static void lex_warn_invalid (char *where, char *what);

/* STATES
	EXPRESSION	definitely in an expression
	SCRIPT		definitely in a script
	BOTH		either EXPRESSION or SCRIPT
	DEFSYMEXP	in an argument to -defsym
        MRI             in an MRI script
	VERS_START	starting a Sun style mapfile
	VERS_SCRIPT	a Sun style mapfile
	VERS_NODE	a node within a Sun style mapfile
*/
#define RTOKEN(x)  {  yylval.token = x; return x; }

/* Some versions of flex want this.  */
#ifndef yywrap
int yywrap (void) { return 1; }
#endif
%}

%option nounput

%a 4000
%o 5000

CMDFILENAMECHAR   [_a-zA-Z0-9\/\.\\_\+\$\:\[\]\\\,\=\&\!\<\>\-\~]
CMDFILENAMECHAR1  [_a-zA-Z0-9\/\.\\_\+\$\:\[\]\\\,\=\&\!\<\>\~]
FILENAMECHAR1	[_a-zA-Z\/\.\\\$\_\~]
SYMBOLCHARN     [_a-zA-Z\/\.\\\$\_\~0-9]
FILENAMECHAR	[_a-zA-Z0-9\/\.\-\_\+\=\$\:\[\]\\\,\~]
WILDCHAR	[_a-zA-Z0-9\/\.\-\_\+\=\$\:\[\]\\\,\~\?\*]
WHITE		[ \t\n\r]+

NOCFILENAMECHAR	[_a-zA-Z0-9\/\.\-\_\+\$\:\[\]\\\~]

V_TAG [.$_a-zA-Z][._a-zA-Z0-9]*
V_IDENTIFIER [*?.$_a-zA-Z\[\]\-\!\^\\]([*?.$_a-zA-Z0-9\[\]\-\!\^\\]|::)*

%s SCRIPT
%s EXPRESSION
%s BOTH
%s DEFSYMEXP
%s MRI
%s VERS_START
%s VERS_SCRIPT
%s VERS_NODE
%%

  if (parser_input != input_selected)
    {
      /* The first token of the input determines the initial parser state.  */
      input_type t = parser_input;
      parser_input = input_selected;
      switch (t)
	{
	case input_script: return INPUT_SCRIPT; break;
	case input_mri_script: return INPUT_MRI_SCRIPT; break;
	case input_version_script: return INPUT_VERSION_SCRIPT; break;
	case input_dynamic_list: return INPUT_DYNAMIC_LIST; break;
	case input_defsym: return INPUT_DEFSYM; break;
	default: abort ();
	}
    }

<BOTH,SCRIPT,EXPRESSION,VERS_START,VERS_NODE,VERS_SCRIPT>"/*"	{ comment (); }


<DEFSYMEXP>"-"                  { RTOKEN('-');}
<DEFSYMEXP>"+"                  { RTOKEN('+');}
<DEFSYMEXP>{FILENAMECHAR1}{SYMBOLCHARN}*   { yylval.name = xstrdup (yytext); return NAME; }
<DEFSYMEXP>"="                  { RTOKEN('='); }

<MRI,EXPRESSION>"$"([0-9A-Fa-f])+ {
  				yylval.integer = bfd_scan_vma (yytext + 1, 0, 16);
				yylval.bigint.str = NULL;
				return INT;
			}

<MRI,EXPRESSION>([0-9A-Fa-f])+(H|h|X|x|B|b|O|o|D|d) {
				   int ibase ;
				   switch (yytext[yyleng - 1]) {
				    case 'X':
				    case 'x':
				    case 'H':
				    case 'h':
				     ibase = 16;
				     break;
				    case 'O':
				    case 'o':
				     ibase = 8;
				     break;
				    case 'B':
				    case 'b':
				     ibase = 2;
				     break;
				    default:
				     ibase = 10;
				   }
				   yylval.integer = bfd_scan_vma (yytext, 0,
								  ibase);
				   yylval.bigint.str = NULL;
				   return INT;
				 }
<SCRIPT,DEFSYMEXP,MRI,BOTH,EXPRESSION>((("$"|0[xX])([0-9A-Fa-f])+)|(([0-9])+))(M|K|m|k)? {
				  char *s = yytext;
				  int ibase = 0;

				  if (*s == '$')
				    {
				      ++s;
				      ibase = 16;
				    }
				  yylval.integer = bfd_scan_vma (s, 0, ibase);
				  yylval.bigint.str = NULL;
				  if (yytext[yyleng - 1] == 'M'
				      || yytext[yyleng - 1] == 'm')
				    {
				      yylval.integer *= 1024 * 1024;
				    }
				  else if (yytext[yyleng - 1] == 'K'
				      || yytext[yyleng - 1]=='k')
				    {
				      yylval.integer *= 1024;
				    }
				  else if (yytext[0] == '0'
					   && (yytext[1] == 'x'
					       || yytext[1] == 'X'))
				    {
				      yylval.bigint.str = xstrdup (yytext + 2);
				    }
				  return INT;
				}
<BOTH,SCRIPT,EXPRESSION,MRI>"]"		{ RTOKEN(']');}
<BOTH,SCRIPT,EXPRESSION,MRI>"["		{ RTOKEN('[');}
<BOTH,SCRIPT,EXPRESSION,MRI>"<<="	{ RTOKEN(LSHIFTEQ);}
<BOTH,SCRIPT,EXPRESSION,MRI>">>="	{ RTOKEN(RSHIFTEQ);}
<BOTH,SCRIPT,EXPRESSION,MRI>"||"	{ RTOKEN(OROR);}
<BOTH,SCRIPT,EXPRESSION,MRI>"=="	{ RTOKEN(EQ);}
<BOTH,SCRIPT,EXPRESSION,MRI>"!="	{ RTOKEN(NE);}
<BOTH,SCRIPT,EXPRESSION,MRI>">="	{ RTOKEN(GE);}
<BOTH,SCRIPT,EXPRESSION,MRI>"<="	{ RTOKEN(LE);}
<BOTH,SCRIPT,EXPRESSION,MRI>"<<"	{ RTOKEN(LSHIFT);}
<BOTH,SCRIPT,EXPRESSION,MRI>">>"	{ RTOKEN(RSHIFT);}
<BOTH,SCRIPT,EXPRESSION,MRI>"+="	{ RTOKEN(PLUSEQ);}
<BOTH,SCRIPT,EXPRESSION,MRI>"-="	{ RTOKEN(MINUSEQ);}
<BOTH,SCRIPT,EXPRESSION,MRI>"*="	{ RTOKEN(MULTEQ);}
<BOTH,SCRIPT,EXPRESSION,MRI>"/="	{ RTOKEN(DIVEQ);}
<BOTH,SCRIPT,EXPRESSION,MRI>"&="	{ RTOKEN(ANDEQ);}
<BOTH,SCRIPT,EXPRESSION,MRI>"|="	{ RTOKEN(OREQ);}
<BOTH,SCRIPT,EXPRESSION,MRI>"&&"	{ RTOKEN(ANDAND);}
<BOTH,SCRIPT,EXPRESSION,MRI>">"		{ RTOKEN('>');}
<BOTH,SCRIPT,EXPRESSION,MRI>","		{ RTOKEN(',');}
<BOTH,SCRIPT,EXPRESSION,MRI>"&"		{ RTOKEN('&');}
<BOTH,SCRIPT,EXPRESSION,MRI>"|"		{ RTOKEN('|');}
<BOTH,SCRIPT,EXPRESSION,MRI>"~"		{ RTOKEN('~');}
<BOTH,SCRIPT,EXPRESSION,MRI>"!"		{ RTOKEN('!');}
<BOTH,SCRIPT,EXPRESSION,MRI>"?"		{ RTOKEN('?');}
<BOTH,SCRIPT,EXPRESSION,MRI>"*"		{ RTOKEN('*');}
<BOTH,SCRIPT,EXPRESSION,MRI>"+"		{ RTOKEN('+');}
<BOTH,SCRIPT,EXPRESSION,MRI>"-"		{ RTOKEN('-');}
<BOTH,SCRIPT,EXPRESSION,MRI>"/"		{ RTOKEN('/');}
<BOTH,SCRIPT,EXPRESSION,MRI>"%"		{ RTOKEN('%');}
<BOTH,SCRIPT,EXPRESSION,MRI>"<"		{ RTOKEN('<');}
<BOTH,SCRIPT,EXPRESSION,MRI>"="         { RTOKEN('=');}
<BOTH,SCRIPT,EXPRESSION,MRI>"}"		{ RTOKEN('}') ; }
<BOTH,SCRIPT,EXPRESSION,MRI>"{"		{ RTOKEN('{'); }
<BOTH,SCRIPT,EXPRESSION,MRI>")"		{ RTOKEN(')');}
<BOTH,SCRIPT,EXPRESSION,MRI>"("		{ RTOKEN('(');}
<BOTH,SCRIPT,EXPRESSION,MRI>":"		{ RTOKEN(':'); }
<BOTH,SCRIPT,EXPRESSION,MRI>";"		{ RTOKEN(';');}
<BOTH,SCRIPT>"MEMORY"			{ RTOKEN(MEMORY);}
<BOTH,SCRIPT,EXPRESSION>"ORIGIN"	{ RTOKEN(ORIGIN);}
<BOTH,SCRIPT>"VERSION"			{ RTOKEN(VERSIONK);}
<EXPRESSION,BOTH,SCRIPT>"BLOCK"		{ RTOKEN(BLOCK);}
<EXPRESSION,BOTH,SCRIPT>"BIND"		{ RTOKEN(BIND);}
<BOTH,SCRIPT,EXPRESSION>"LENGTH"	{ RTOKEN(LENGTH);}
<EXPRESSION,BOTH,SCRIPT>"ALIGN"		{ RTOKEN(ALIGN_K);}
<EXPRESSION,BOTH,SCRIPT>"DATA_SEGMENT_ALIGN"	{ RTOKEN(DATA_SEGMENT_ALIGN);}
<EXPRESSION,BOTH,SCRIPT>"DATA_SEGMENT_RELRO_END"	{ RTOKEN(DATA_SEGMENT_RELRO_END);}
<EXPRESSION,BOTH,SCRIPT>"DATA_SEGMENT_END"	{ RTOKEN(DATA_SEGMENT_END);}
<EXPRESSION,BOTH,SCRIPT>"ADDR"		{ RTOKEN(ADDR);}
<EXPRESSION,BOTH,SCRIPT>"LOADADDR"	{ RTOKEN(LOADADDR);}
<EXPRESSION,BOTH,SCRIPT>"ALIGNOF"	{ RTOKEN(ALIGNOF); }
<EXPRESSION,BOTH>"MAX"			{ RTOKEN(MAX_K); }
<EXPRESSION,BOTH>"MIN"			{ RTOKEN(MIN_K); }
<EXPRESSION,BOTH,SCRIPT>"ASSERT"	{ RTOKEN(ASSERT_K); }
<BOTH,SCRIPT>"ENTRY"			{ RTOKEN(ENTRY);}
<BOTH,SCRIPT,MRI>"EXTERN"		{ RTOKEN(EXTERN);}
<EXPRESSION,BOTH,SCRIPT>"NEXT"		{ RTOKEN(NEXT);}
<EXPRESSION,BOTH,SCRIPT>"sizeof_headers"	{ RTOKEN(SIZEOF_HEADERS);}
<EXPRESSION,BOTH,SCRIPT>"SIZEOF_HEADERS"	{ RTOKEN(SIZEOF_HEADERS);}
<EXPRESSION,BOTH,SCRIPT>"SEGMENT_START" { RTOKEN(SEGMENT_START);}
<BOTH,SCRIPT>"MAP"			{ RTOKEN(MAP);}
<EXPRESSION,BOTH,SCRIPT>"SIZEOF"	{ RTOKEN(SIZEOF);}
<BOTH,SCRIPT>"TARGET"			{ RTOKEN(TARGET_K);}
<BOTH,SCRIPT>"SEARCH_DIR"		{ RTOKEN(SEARCH_DIR);}
<BOTH,SCRIPT>"OUTPUT"			{ RTOKEN(OUTPUT);}
<BOTH,SCRIPT>"INPUT"			{ RTOKEN(INPUT);}
<EXPRESSION,BOTH,SCRIPT>"GROUP"		{ RTOKEN(GROUP);}
<EXPRESSION,BOTH,SCRIPT>"AS_NEEDED"	{ RTOKEN(AS_NEEDED);}
<EXPRESSION,BOTH,SCRIPT>"DEFINED"	{ RTOKEN(DEFINED);}
<BOTH,SCRIPT>"CREATE_OBJECT_SYMBOLS"	{ RTOKEN(CREATE_OBJECT_SYMBOLS);}
<BOTH,SCRIPT>"CONSTRUCTORS"		{ RTOKEN( CONSTRUCTORS);}
<BOTH,SCRIPT>"FORCE_COMMON_ALLOCATION"	{ RTOKEN(FORCE_COMMON_ALLOCATION);}
<BOTH,SCRIPT>"INHIBIT_COMMON_ALLOCATION" { RTOKEN(INHIBIT_COMMON_ALLOCATION);}
<BOTH,SCRIPT>"SECTIONS"			{ RTOKEN(SECTIONS);}
<BOTH,SCRIPT>"FILL"			{ RTOKEN(FILL);}
<BOTH,SCRIPT>"STARTUP"			{ RTOKEN(STARTUP);}
<BOTH,SCRIPT>"OUTPUT_FORMAT"		{ RTOKEN(OUTPUT_FORMAT);}
<BOTH,SCRIPT>"OUTPUT_ARCH"		{ RTOKEN( OUTPUT_ARCH);}
<BOTH,SCRIPT>"HLL"			{ RTOKEN(HLL);}
<BOTH,SCRIPT>"SYSLIB"			{ RTOKEN(SYSLIB);}
<BOTH,SCRIPT>"FLOAT"			{ RTOKEN(FLOAT);}
<BOTH,SCRIPT>"QUAD"			{ RTOKEN( QUAD);}
<BOTH,SCRIPT>"SQUAD"			{ RTOKEN( SQUAD);}
<BOTH,SCRIPT>"LONG"			{ RTOKEN( LONG);}
<BOTH,SCRIPT>"SHORT"			{ RTOKEN( SHORT);}
<BOTH,SCRIPT>"BYTE"			{ RTOKEN( BYTE);}
<BOTH,SCRIPT>"NOFLOAT"			{ RTOKEN(NOFLOAT);}
<EXPRESSION,BOTH,SCRIPT>"NOCROSSREFS"	{ RTOKEN(NOCROSSREFS);}
<BOTH,SCRIPT>"OVERLAY"			{ RTOKEN(OVERLAY); }
<BOTH,SCRIPT>"SORT_BY_NAME"		{ RTOKEN(SORT_BY_NAME); }
<BOTH,SCRIPT>"SORT_BY_ALIGNMENT"	{ RTOKEN(SORT_BY_ALIGNMENT); }
<BOTH,SCRIPT>"SORT"			{ RTOKEN(SORT_BY_NAME); }
<EXPRESSION,BOTH,SCRIPT>"NOLOAD"	{ RTOKEN(NOLOAD);}
<EXPRESSION,BOTH,SCRIPT>"DSECT"		{ RTOKEN(DSECT);}
<EXPRESSION,BOTH,SCRIPT>"COPY"		{ RTOKEN(COPY);}
<EXPRESSION,BOTH,SCRIPT>"INFO"		{ RTOKEN(INFO);}
<EXPRESSION,BOTH,SCRIPT>"OVERLAY"	{ RTOKEN(OVERLAY);}
<EXPRESSION,BOTH,SCRIPT>"ONLY_IF_RO"	{ RTOKEN(ONLY_IF_RO); }
<EXPRESSION,BOTH,SCRIPT>"ONLY_IF_RW"	{ RTOKEN(ONLY_IF_RW); }
<EXPRESSION,BOTH,SCRIPT>"SPECIAL"	{ RTOKEN(SPECIAL); }
<BOTH,SCRIPT>"o"			{ RTOKEN(ORIGIN);}
<BOTH,SCRIPT>"org"			{ RTOKEN(ORIGIN);}
<BOTH,SCRIPT>"l"			{ RTOKEN( LENGTH);}
<BOTH,SCRIPT>"len"			{ RTOKEN( LENGTH);}
<BOTH,SCRIPT>"INCLUDE"			{ RTOKEN(INCLUDE);}
<BOTH,SCRIPT>"PHDRS"			{ RTOKEN (PHDRS); }
<EXPRESSION,BOTH,SCRIPT>"AT"		{ RTOKEN(AT);}
<EXPRESSION,BOTH,SCRIPT>"SUBALIGN"	{ RTOKEN(SUBALIGN);}
<EXPRESSION,BOTH,SCRIPT>"PROVIDE"	{ RTOKEN(PROVIDE); }
<EXPRESSION,BOTH,SCRIPT>"PROVIDE_HIDDEN" { RTOKEN(PROVIDE_HIDDEN); }
<EXPRESSION,BOTH,SCRIPT>"KEEP"		{ RTOKEN(KEEP); }
<EXPRESSION,BOTH,SCRIPT>"EXCLUDE_FILE"  { RTOKEN(EXCLUDE_FILE); }
<EXPRESSION,BOTH,SCRIPT>"CONSTANT"	{ RTOKEN(CONSTANT);}
<MRI>"#".*\n?			{ ++ lineno; }
<MRI>"\n"	                { ++ lineno;  RTOKEN(NEWLINE); }
<MRI>"*".*			{ /* Mri comment line */ }
<MRI>";".*			{ /* Mri comment line */ }
<MRI>"END"                      { RTOKEN(ENDWORD); }
<MRI>"ALIGNMOD"			{ RTOKEN(ALIGNMOD);}
<MRI>"ALIGN"			{ RTOKEN(ALIGN_K);}
<MRI>"CHIP"                     { RTOKEN(CHIP); }
<MRI>"BASE"                     { RTOKEN(BASE); }
<MRI>"ALIAS"                    { RTOKEN(ALIAS); }
<MRI>"TRUNCATE"                 { RTOKEN(TRUNCATE); }
<MRI>"LOAD"                     { RTOKEN(LOAD); }
<MRI>"PUBLIC"                   { RTOKEN(PUBLIC); }
<MRI>"ORDER"                    { RTOKEN(ORDER); }
<MRI>"NAME"                     { RTOKEN(NAMEWORD); }
<MRI>"FORMAT"                   { RTOKEN(FORMAT); }
<MRI>"CASE"                     { RTOKEN(CASE); }
<MRI>"START"                    { RTOKEN(START); }
<MRI>"LIST".*                   { RTOKEN(LIST); /* LIST and ignore to end of line */ }
<MRI>"SECT"			{ RTOKEN(SECT); }
<EXPRESSION,BOTH,SCRIPT,MRI>"ABSOLUTE"			{ RTOKEN(ABSOLUTE); }
<MRI>"end"                      { RTOKEN(ENDWORD); }
<MRI>"alignmod"			{ RTOKEN(ALIGNMOD);}
<MRI>"align"			{ RTOKEN(ALIGN_K);}
<MRI>"chip"                     { RTOKEN(CHIP); }
<MRI>"base"                     { RTOKEN(BASE); }
<MRI>"alias"                    { RTOKEN(ALIAS); }
<MRI>"truncate"                 { RTOKEN(TRUNCATE); }
<MRI>"load"                     { RTOKEN(LOAD); }
<MRI>"public"                   { RTOKEN(PUBLIC); }
<MRI>"order"                    { RTOKEN(ORDER); }
<MRI>"name"                     { RTOKEN(NAMEWORD); }
<MRI>"format"                   { RTOKEN(FORMAT); }
<MRI>"case"                     { RTOKEN(CASE); }
<MRI>"extern"                   { RTOKEN(EXTERN); }
<MRI>"start"                    { RTOKEN(START); }
<MRI>"list".*                   { RTOKEN(LIST); /* LIST and ignore to end of line */ }
<MRI>"sect"			{ RTOKEN(SECT); }
<EXPRESSION,BOTH,SCRIPT,MRI>"absolute"			{ RTOKEN(ABSOLUTE); }

<MRI>{FILENAMECHAR1}{NOCFILENAMECHAR}*	{
/* Filename without commas, needed to parse mri stuff */
				 yylval.name = xstrdup (yytext);
				  return NAME;
				}


<BOTH>{FILENAMECHAR1}{FILENAMECHAR}*	{
				 yylval.name = xstrdup (yytext);
				  return NAME;
				}
<BOTH>"-l"{FILENAMECHAR}+ {
				  yylval.name = xstrdup (yytext + 2);
				  return LNAME;
				}
<EXPRESSION>{FILENAMECHAR1}{NOCFILENAMECHAR}*	{
				 yylval.name = xstrdup (yytext);
				  return NAME;
				}
<EXPRESSION>"-l"{NOCFILENAMECHAR}+ {
				  yylval.name = xstrdup (yytext + 2);
				  return LNAME;
				}
<SCRIPT>{WILDCHAR}* {
		/* Annoyingly, this pattern can match comments, and we have
		   longest match issues to consider.  So if the first two
		   characters are a comment opening, put the input back and
		   try again.  */
		if (yytext[0] == '/' && yytext[1] == '*')
		  {
		    yyless (2);
		    comment ();
		  }
		else
		  {
		    yylval.name = xstrdup (yytext);
		    return NAME;
		  }
	}

<EXPRESSION,BOTH,SCRIPT,VERS_NODE>"\""[^\"]*"\"" {
					/* No matter the state, quotes
					   give what's inside */
					yylval.name = xstrdup (yytext + 1);
					yylval.name[yyleng - 2] = 0;
					return NAME;
				}
<BOTH,SCRIPT,EXPRESSION>"\n"		{ lineno++;}
<MRI,BOTH,SCRIPT,EXPRESSION>[ \t\r]+	{ }

<VERS_NODE,VERS_SCRIPT>[:,;]	{ return *yytext; }

<VERS_NODE>global		{ RTOKEN(GLOBAL); }

<VERS_NODE>local		{ RTOKEN(LOCAL); }

<VERS_NODE>extern		{ RTOKEN(EXTERN); }

<VERS_NODE>{V_IDENTIFIER}	{ yylval.name = xstrdup (yytext);
				  return VERS_IDENTIFIER; }

<VERS_SCRIPT>{V_TAG}		{ yylval.name = xstrdup (yytext);
				  return VERS_TAG; }

<VERS_START>"{"			{ BEGIN(VERS_SCRIPT); return *yytext; }

<VERS_SCRIPT>"{"		{ BEGIN(VERS_NODE);
				  vers_node_nesting = 0;
				  return *yytext;
				}
<VERS_SCRIPT>"}"		{ return *yytext; }
<VERS_NODE>"{"			{ vers_node_nesting++; return *yytext; }
<VERS_NODE>"}"			{ if (--vers_node_nesting < 0)
				    BEGIN(VERS_SCRIPT);
				  return *yytext;
				}

<VERS_START,VERS_NODE,VERS_SCRIPT>[\n]		{ lineno++; }

<VERS_START,VERS_NODE,VERS_SCRIPT>#.*		{ /* Eat up comments */ }

<VERS_START,VERS_NODE,VERS_SCRIPT>[ \t\r]+   	{ /* Eat up whitespace */ }

<<EOF>> {
  include_stack_ptr--;

  if (include_stack_ptr == 0)
  {
    yyterminate ();
  }
  else
  {
    yy_switch_to_buffer (include_stack[include_stack_ptr]);
  }

  ldfile_input_filename = file_name_stack[include_stack_ptr - 1];
  lineno = lineno_stack[include_stack_ptr];

  return END;
}

<SCRIPT,MRI,VERS_START,VERS_SCRIPT,VERS_NODE>.	lex_warn_invalid (" in script", yytext);
<EXPRESSION,DEFSYMEXP,BOTH>.	lex_warn_invalid (" in expression", yytext);

%%


/* Switch flex to reading script file NAME, open on FILE,
   saving the current input info on the include stack.  */

void
lex_push_file (FILE *file, const char *name)
{
  if (include_stack_ptr >= MAX_INCLUDE_DEPTH)
    {
      einfo ("%F:includes nested too deeply\n");
    }
  file_name_stack[include_stack_ptr] = name;
  lineno_stack[include_stack_ptr] = lineno;
  include_stack[include_stack_ptr] = YY_CURRENT_BUFFER;

  include_stack_ptr++;
  lineno = 1;
  yyin = file;
  yy_switch_to_buffer (yy_create_buffer (yyin, YY_BUF_SIZE));
}

/* Return a newly created flex input buffer containing STRING,
   which is SIZE bytes long.  */

static YY_BUFFER_STATE
yy_create_string_buffer (const char *string, size_t size)
{
  YY_BUFFER_STATE b;

  /* Calls to m-alloc get turned by sed into xm-alloc.  */
  b = malloc (sizeof (struct yy_buffer_state));
  b->yy_input_file = 0;
  b->yy_buf_size = size;

  /* yy_ch_buf has to be 2 characters longer than the size given because
     we need to put in 2 end-of-buffer characters.  */
  b->yy_ch_buf = malloc ((unsigned) (b->yy_buf_size + 3));

  b->yy_ch_buf[0] = '\n';
  strcpy (b->yy_ch_buf+1, string);
  b->yy_ch_buf[size+1] = YY_END_OF_BUFFER_CHAR;
  b->yy_ch_buf[size+2] = YY_END_OF_BUFFER_CHAR;
  b->yy_n_chars = size+1;
  b->yy_buf_pos = &b->yy_ch_buf[1];

  b->yy_is_our_buffer = 1;
  b->yy_is_interactive = 0;
  b->yy_at_bol = 1;
  b->yy_fill_buffer = 0;

  /* flex 2.4.7 changed the interface.  FIXME: We should not be using
     a flex internal interface in the first place!  */
#ifdef YY_BUFFER_NEW
  b->yy_buffer_status = YY_BUFFER_NEW;
#else
  b->yy_eof_status = EOF_NOT_SEEN;
#endif

  return b;
}

/* Switch flex to reading from STRING, saving the current input info
   on the include stack.  */

void
lex_redirect (const char *string)
{
  YY_BUFFER_STATE tmp;

  yy_init = 0;
  if (include_stack_ptr >= MAX_INCLUDE_DEPTH)
    {
      einfo("%F: macros nested too deeply\n");
    }
  file_name_stack[include_stack_ptr] = "redirect";
  lineno_stack[include_stack_ptr] = lineno;
  include_stack[include_stack_ptr] = YY_CURRENT_BUFFER;
  include_stack_ptr++;
  lineno = 1;
  tmp = yy_create_string_buffer (string, strlen (string));
  yy_switch_to_buffer (tmp);
}

/* Functions to switch to a different flex start condition,
   saving the current start condition on `state_stack'.  */

static int state_stack[MAX_INCLUDE_DEPTH * 2];
static int *state_stack_p = state_stack;

void
ldlex_script (void)
{
  *(state_stack_p)++ = yy_start;
  BEGIN (SCRIPT);
}

void
ldlex_mri_script (void)
{
  *(state_stack_p)++ = yy_start;
  BEGIN (MRI);
}

void
ldlex_version_script (void)
{
  *(state_stack_p)++ = yy_start;
  BEGIN (VERS_START);
}

void
ldlex_version_file (void)
{
  *(state_stack_p)++ = yy_start;
  BEGIN (VERS_SCRIPT);
}

void
ldlex_defsym (void)
{
  *(state_stack_p)++ = yy_start;
  BEGIN (DEFSYMEXP);
}

void
ldlex_expression (void)
{
  *(state_stack_p)++ = yy_start;
  BEGIN (EXPRESSION);
}

void
ldlex_both (void)
{
  *(state_stack_p)++ = yy_start;
  BEGIN (BOTH);
}

void
ldlex_popstate (void)
{
  yy_start = *(--state_stack_p);
}


/* Place up to MAX_SIZE characters in BUF and return in *RESULT
   either the number of characters read, or 0 to indicate EOF.  */

static void
yy_input (char *buf, yy_size_t *result, yy_size_t max_size)
{
  *result = 0;
  if (YY_CURRENT_BUFFER->yy_input_file)
    {
      if (yyin)
	{
	  *result = fread (buf, 1, max_size, yyin);
	  if (*result < max_size && ferror (yyin))
	    einfo ("%F%P: read in flex scanner failed\n");
	}
    }
}

/* Eat the rest of a C-style comment.  */

static void
comment (void)
{
  int c;

  while (1)
  {
    c = input();
    while (c != '*' && c != EOF)
    {
      if (c == '\n')
	lineno++;
      c = input();
    }

    if (c == '*')
    {
      c = input();
      while (c == '*')
       c = input();
      if (c == '/')
       break;			/* found the end */
    }

    if (c == '\n')
      lineno++;

    if (c == EOF)
    {
      einfo( "%F%P: EOF in comment\n");
      break;
    }
  }
}

/* Warn the user about a garbage character WHAT in the input
   in context WHERE.  */

static void
lex_warn_invalid (char *where, char *what)
{
  char buf[5];

  /* If we have found an input file whose format we do not recognize,
     and we are therefore treating it as a linker script, and we find
     an invalid character, then most likely this is a real object file
     of some different format.  Treat it as such.  */
  if (ldfile_assumed_script)
    {
      bfd_set_error (bfd_error_file_not_recognized);
      einfo ("%F%s: file not recognized: %E\n", ldfile_input_filename);
    }

  if (! ISPRINT (*what))
    {
      sprintf (buf, "\\%03o", (unsigned int) *what);
      what = buf;
    }

  einfo ("%P:%S: ignoring invalid character `%s'%s\n", what, where);
}

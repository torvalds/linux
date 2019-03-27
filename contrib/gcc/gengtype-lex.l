/* -*- indented-text -*- */
/* Process source files and output type information.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

%{
#include "bconfig.h"
#include "coretypes.h"
#include "system.h"

#define malloc xmalloc
#define realloc xrealloc

#include "gengtype.h"
#include "gengtype-yacc.h"

#define YY_INPUT(BUF,RESULT,SIZE) ((RESULT) = macro_input (BUF,SIZE))

static unsigned macro_input (char *buffer, unsigned);
static const char *push_macro_expansion (const char *, unsigned,
					 const char *, unsigned);
static char *mangle_macro_name (const char *, unsigned,
       			        const char *, unsigned);
static void update_lineno (const char *l, size_t len);

struct fileloc lexer_line;
int lexer_toplevel_done;

static void 
update_lineno (const char *l, size_t len)
{
  while (len-- > 0)
    if (*l++ == '\n')
      lexer_line.line++;
}

%}

ID	[[:alpha:]_][[:alnum:]_]*
WS	[[:space:]]+
IWORD	short|long|(un)?signed|char|int|HOST_WIDE_INT|HOST_WIDEST_INT|bool|size_t|BOOL_BITFIELD
ITYPE	{IWORD}({WS}{IWORD})*

%x in_struct in_struct_comment in_comment in_yacc_escape
%option warn noyywrap nounput nodefault perf-report
%option 8bit never-interactive
%%

[^[:alnum:]_]typedef{WS}(struct|union){WS}{ID}{WS}?[*[:space:]]{WS}?{ID}{WS}?";" {
  char *tagstart;
  size_t taglen;
  char *namestart;
  size_t namelen;
  int is_pointer = 0;
  struct type *t;
  int union_p;

  tagstart = yytext + strlen (" typedef ");
  while (ISSPACE (*tagstart))
    tagstart++;
  union_p = tagstart[0] == 'u';
  tagstart += strlen ("union ");
  while (ISSPACE (*tagstart))
    tagstart++;
  for (taglen = 1; ISIDNUM (tagstart[taglen]); taglen++)
    ;
  for (namestart = tagstart + taglen; 
       ! ISIDNUM (*namestart);
       namestart++)
    if (*namestart == '*')
      is_pointer = 1;
  for (namelen = 1; ISIDNUM (namestart[namelen]); namelen++)
    ;
  t = find_structure ((const char *) xmemdup (tagstart, taglen, taglen+1),
		      union_p);
  if (is_pointer)
    t = create_pointer (t);
  namestart = (char *) xmemdup (namestart, namelen, namelen+1);
#ifdef USE_MAPPED_LOCATION
  /* temporary kludge - gentype doesn't handle cpp conditionals */
  if (strcmp (namestart, "location_t") != 0
      && strcmp (namestart, "expanded_location") != 0)
#endif
  do_typedef (namestart, t, &lexer_line);
  update_lineno (yytext, yyleng);
}

[^[:alnum:]_]typedef{WS}{ITYPE}{WS}{ID}{WS}?";" {

  char *namestart;
  size_t namelen;
  struct type *t;
  char *typestart;
  size_t typelen;

  for (namestart = yytext + yyleng - 2; ISSPACE (*namestart); namestart--)
    ;
  for (namelen = 1; !ISSPACE (namestart[-namelen]); namelen++)
    ;
  namestart -= namelen - 1;
  for (typestart = yytext + strlen (" typedef "); 
       ISSPACE(*typestart);
       typestart++)
    ;
  for (typelen = namestart - typestart;
       ISSPACE (typestart[typelen-1]);
       typelen--)
    ;

  t = create_scalar_type (typestart, typelen);
  do_typedef ((const char *) xmemdup (namestart, namelen, namelen+1), t,
	      &lexer_line);
  update_lineno (yytext, yyleng);
}

[^[:alnum:]_]typedef{WS}{ID}{WS}{ID}{WS}PARAMS {
  char *namestart;
  size_t namelen;
  struct type *t;

  for (namestart = yytext + yyleng - 7; ISSPACE (*namestart); namestart--)
    ;
  for (namelen = 1; !ISSPACE (namestart[-namelen]); namelen++)
    ;
  namestart -= namelen - 1;

  t = create_scalar_type ("function type", sizeof ("function type")-1);
  do_typedef ((const char *) xmemdup (namestart, namelen, namelen+1), t,
	      &lexer_line);
  update_lineno (yytext, yyleng);
}

[^[:alnum:]_]typedef{WS}{ID}{WS}{ID}{WS}"(" {
  char *namestart;
  size_t namelen;
  struct type *t;

  for (namestart = yytext + yyleng - 2; ISSPACE (*namestart); namestart--)
    ;
  for (namelen = 1; !ISSPACE (namestart[-namelen]); namelen++)
    ;
  namestart -= namelen - 1;

  t = create_scalar_type ("function type", sizeof ("function type")-1);
  do_typedef ((const char *) xmemdup (namestart, namelen, namelen+1), t,
	      &lexer_line);
  update_lineno (yytext, yyleng);
}

[^[:alnum:]_]typedef{WS}{ID}{WS}?"*"?{WS}?"("{WS}?"*"{WS}?{ID}{WS}?")"{WS}?PARAMS {
  char *namestart;
  size_t namelen;
  struct type *t;

  for (namestart = yytext + yyleng - 7; !ISIDNUM (*namestart); namestart--)
    ;
  for (namelen = 1; ISIDNUM (namestart[-namelen]); namelen++)
    ;
  namestart -= namelen - 1;

  t = create_scalar_type ("function type", sizeof ("function type")-1);
  do_typedef ((const char *) xmemdup (namestart, namelen, namelen+1), t,
	      &lexer_line);
  update_lineno (yytext, yyleng);
}

[^[:alnum:]_]typedef{WS}{ID}{WS}?"*"?{WS}?"("{WS}?"*"{WS}?{ID}{WS}?")"{WS}?"(" {
  char *namestart;
  size_t namelen;
  struct type *t;

  for (namestart = yytext + yyleng - 2; !ISIDNUM (*namestart); namestart--)
    ;
  for (namelen = 1; ISIDNUM (namestart[-namelen]); namelen++)
    ;
  namestart -= namelen - 1;

  t = create_scalar_type ("function type", sizeof ("function type")-1);
  do_typedef ((const char *) xmemdup (namestart, namelen, namelen+1), t,
	      &lexer_line);
  update_lineno (yytext, yyleng);
}

[^[:alnum:]_](typedef{WS})?(struct|union){WS}{ID}{WS}/"GTY" {
  char *tagstart;
  size_t taglen;
  int typedef_p;
  int union_p;

  typedef_p = yytext[1] == 't';
  if (typedef_p)
    for (tagstart = yytext + strlen (" typedef "); 
	 ISSPACE(*tagstart);
	 tagstart++)
      ;
  else
    tagstart = yytext + 1;

  union_p = tagstart[0] == 'u';
  tagstart += strlen ("union ");
  while (ISSPACE (*tagstart))
    tagstart++;
  for (taglen = 1; ISIDNUM (tagstart[taglen]); taglen++)
    ;

  yylval.t = find_structure ((const char *) xmemdup (tagstart, taglen,
						     taglen + 1),
			     union_p);
  BEGIN(in_struct);
  update_lineno (yytext, yyleng);
  return typedef_p ? ENT_TYPEDEF_STRUCT : ENT_STRUCT;
}

[^[:alnum:]_](extern|static){WS}/"GTY" {
  BEGIN(in_struct);
  update_lineno (yytext, yyleng);
  return ENT_EXTERNSTATIC;
}

^"%union"{WS}"{"{WS}/"GTY" {
  BEGIN(in_struct);
  update_lineno (yytext, yyleng);
  return ENT_YACCUNION;
}

^"DEF_VEC_"[[:alnum:]_]*{WS}?"("{WS}?{ID}{WS}?(","{WS}?{ID}{WS}?)*")" {
  char *macro, *arg;
  unsigned macro_len, arg_len;
  char *ptr = yytext;
  const char *additional;
  type_p t;

  /* Find the macro name.  */
  for (macro = ptr; *ptr != '(' && !ISSPACE (*ptr); ptr++)
    continue;
  for (macro_len = ptr - macro; !(ISALNUM (*ptr) || *ptr == '_'); ptr++)
    continue;

  /* Find the argument(s).  */
  for (arg = ptr; *ptr != ')'; ptr++)
    continue;
  arg_len = ptr - arg;

  /* Create the struct and typedef.  */
  ptr = mangle_macro_name ("VEC", 3, arg, arg_len);

  t = find_structure (ptr, 0);
  do_typedef (ptr, t, &lexer_line);

  /* Push the macro for later expansion.  */
  additional = push_macro_expansion (macro, macro_len, arg, arg_len);

  if (additional)
    {
      ptr = mangle_macro_name (ptr, strlen (ptr),
			       additional, strlen (additional));
      t = find_structure (ptr, 0);
      do_typedef (ptr, t, &lexer_line);
    }
}

<in_struct>{

"/*"				{ BEGIN(in_struct_comment); }

^"%{"				{ BEGIN(in_yacc_escape); } /* } */

{WS}				{ update_lineno (yytext, yyleng); }

"const"/[^[:alnum:]_]		/* don't care */
"GTY"/[^[:alnum:]_]		{ return GTY_TOKEN; }
"union"/[^[:alnum:]_]		{ return UNION; }
"struct"/[^[:alnum:]_]		{ return STRUCT; }
"enum"/[^[:alnum:]_]		{ return ENUM; }
"ptr_alias"/[^[:alnum:]_]	{ return ALIAS; }
"nested_ptr"/[^[:alnum:]_]	{ return NESTED_PTR; }
[0-9]+				{ return NUM; }
"param"[0-9]*"_is"/[^[:alnum:]_]		{
  yylval.s = (const char *) xmemdup (yytext, yyleng, yyleng+1);
  return PARAM_IS;
}

{IWORD}({WS}{IWORD})*/[^[:alnum:]_]		|
"ENUM_BITFIELD"{WS}?"("{WS}?{ID}{WS}?")"	{
  size_t len;

  for (len = yyleng; ISSPACE (yytext[len-1]); len--)
    ;

  yylval.t = create_scalar_type (yytext, len);
  update_lineno (yytext, yyleng);
  return SCALAR;
}

"VEC"{WS}?"("{WS}?{ID}{WS}?(","{WS}?{ID}{WS}?)*")" {
  char *macro, *arg;
  unsigned macro_len, arg_len;
  char *ptr = yytext;

  /* Find the macro name */
  for (macro = ptr; *ptr != '(' && !ISSPACE (*ptr); ptr++)
    continue;
  for (macro_len = ptr - macro; !(ISALNUM(*ptr) || *ptr == '_'); ptr++)
    continue;

  /* Find the arguments.  */
  for (arg = ptr; *ptr != ')'; ptr++)
    continue;
  arg_len = ptr - arg;

  ptr = mangle_macro_name (macro, macro_len, arg, arg_len);
  yylval.s = ptr;
  return ID;
}

{ID}/[^[:alnum:]_]		{
  yylval.s = (const char *) xmemdup (yytext, yyleng, yyleng+1);
  return ID;
}

\"([^"\\]|\\.)*\"		{
  yylval.s = (const char *) xmemdup (yytext+1, yyleng-2, yyleng-1);
  return STRING;
}
"["[^\[\]]*"]"			{
  yylval.s = (const char *) xmemdup (yytext+1, yyleng-2, yyleng-1);
  return ARRAY;
}
^"%"{ID}			{
  yylval.s = (const char *) xmemdup (yytext+1, yyleng-1, yyleng);
  return PERCENT_ID;
}
"'"("\\".|[^\\])"'"		{
  yylval.s = (const char *) xmemdup (yytext+1, yyleng-2, yyleng);
  return CHAR;
}

[(){},*:<>]			{ return yytext[0]; }

[;=]				{
  if (lexer_toplevel_done)
    {
      BEGIN(INITIAL);
      lexer_toplevel_done = 0;
    }
  return yytext[0];
}

^"%%"				{
  BEGIN(INITIAL);
  return PERCENTPERCENT;
}

"#define"[^\n]*\n		{lexer_line.line++;}

.				{
  error_at_line (&lexer_line, "unexpected character `%s'", yytext);
}
}

"/*"			{ BEGIN(in_comment); }
\n			{ lexer_line.line++; }
{ID}			|
"'"("\\".|[^\\])"'"	|
[^"/\n]			/* do nothing */
\"([^"\\]|\\.|\\\n)*\"	{ update_lineno (yytext, yyleng); }
"/"/[^*]		/* do nothing */

<in_comment,in_struct_comment>{
\n		{ lexer_line.line++; }
[^*\n]{16}	|
[^*\n]		/* do nothing */
"*"/[^/]	/* do nothing */
}
<in_comment>"*/"	{ BEGIN(INITIAL); } 
<in_struct_comment>"*/"	{ BEGIN(in_struct); }

<in_yacc_escape>{
\n		{ lexer_line.line++; }
[^%]{16}	|
[^%]		/* do nothing */
"%"/[^}]	/* do nothing */
"%}"		{ BEGIN(in_struct); }
"%"		{
  error_at_line (&lexer_line, 
		 "unterminated %%{; unexpected EOF");
}
}


["/]    		|
<in_struct_comment,in_comment>"*"	{
  error_at_line (&lexer_line, 
		 "unterminated comment or string; unexpected EOF");
}

^"#define"{WS}"GTY(" /* do nothing */
{WS}"GTY"{WS}?"("	{
  error_at_line (&lexer_line, "stray GTY marker");
}

%%

/* Deal with the expansion caused by the DEF_VEC_x macros.  */

/* Mangle a macro and argument list as done by cpp concatenation in
   the compiler proper.  */
static char *
mangle_macro_name (const char *macro, unsigned macro_len,
		   const char *arg, unsigned arg_len)
{
  char *ptr = (char *) xmemdup (macro, macro_len, macro_len + arg_len + 2);

  /* Now copy and concatenate each argument */
  while (arg_len)
    {
      ptr[macro_len++] = '_';
      for (; arg_len && (ISALNUM(*arg) || *arg == '_'); arg_len--)
        ptr[macro_len++] = *arg++;
      for (; arg_len && !(ISALNUM(*arg) || *arg == '_'); arg_len--)
        arg++;
    }
  ptr[macro_len] = 0;

  return ptr;
}

typedef struct macro_def
{
  const char *name;
  const char *expansion;
  const char *additional;
} macro_def_t;

typedef struct macro
{
  const macro_def_t *def;
  struct macro *next;
  const char *args[10];
} macro_t;

static const macro_def_t macro_defs[] = 
{
#define IN_GENGTYPE 1
#include "vec.h"
  {NULL, NULL, NULL}
};

/* Chain of macro expansions to do at end of scanning.  */
static macro_t *macro_expns;
static macro_t *macro_expns_end;

/* Push macro NAME (NAME_LEN) with argument ARG (ARG_LEN) onto the
   expansion queue.  We ensure NAME is known at this point.  */

static const char *
push_macro_expansion (const char *name, unsigned name_len,
		      const char *arg, unsigned arg_len)
{
  unsigned ix;

  for (ix = 0; macro_defs[ix].name; ix++)
    if (strlen (macro_defs[ix].name) == name_len
        && !memcmp (name, macro_defs[ix].name, name_len))
      {
        macro_t *expansion = XNEW (macro_t);
        char *args;
	unsigned argno, last_arg;

	expansion->def = &macro_defs[ix];
	expansion->next = NULL;
	args = (char *) xmemdup (arg, arg_len, arg_len+1);
	args[arg_len] = 0;
        for (argno = 0; *args;)
	  {
   	    expansion->args[argno++] = args;
	    while (*args && (ISALNUM (*args) || *args == '_'))
	      args++;
	    if (argno == 1)
	      expansion->args[argno++] = "base";
	    if (!*args)
	      break;
	    *args++ = 0;
	    while (*args && !(ISALNUM (*args) || *args == '_'))
	      args++;
          }
	last_arg = argno;
        for (; argno != 10; argno++)
	  expansion->args[argno] = NULL;
	if (macro_expns_end)
          macro_expns_end->next = expansion;
	else
	  macro_expns = expansion;
	macro_expns_end = expansion;
	if (macro_defs[ix].additional)
	  {
	    macro_t *expn2 = XNEW (macro_t);
            memcpy (expn2, expansion, sizeof (*expn2));
	    expansion = expn2;
	    expansion->def += 1;
	    expansion->args[last_arg++] = macro_defs[ix].additional;
	    macro_expns_end->next = expansion;
	    macro_expns_end = expansion;
	  }
        if (last_arg > 2 && strcmp (expansion->args[last_arg - 1], "heap"))
	  expansion->args[last_arg++] = "GTY (())";
	return macro_defs[ix].additional;
      }
  error_at_line (&lexer_line, "unrecognized macro `%.*s(%.*s)'",
		 name_len, name, arg_len, arg);
  return NULL;
}

/* Attempt to read some input.  Use fread until we're at the end of
   file.  At end of file expand the next queued macro.  We presume the
   buffer is large enough for the entire expansion.  */

static unsigned
macro_input (char *buffer, unsigned size)
{
  unsigned result;

  result = fread (buffer, 1, size, yyin);
  if (result)
    /*NOP*/;
  else if (ferror (yyin))
    YY_FATAL_ERROR ("read of source file failed");
  else if (macro_expns)
    {
      const char *expn;
      unsigned len;

      for (expn = macro_expns->def->expansion; *expn; expn++)
        {
	  if (*expn == '#')
	    {
	      int argno;

	      argno = expn[1] - '0';
	      expn += 1;

	      /* Remove inserted space? */
	      if (buffer[result-1] == ' ' && buffer[result-2] == '_')
	        result--;

	      /* Insert the argument value */
	      if (macro_expns->args[argno])
	        {
		  len = strlen (macro_expns->args[argno]);
		  memcpy (&buffer[result], macro_expns->args[argno], len);
		  result += len;
		}

	      /* Skip next space? */
	      if (expn[1] == ' ' && expn[2] == '_')
	        expn++;
	    }
	  else
	    {
	      buffer[result++] = *expn;
	      if (*expn == ';' || *expn == '{')
	        buffer[result++] = '\n';
	    }
        }
      if (result > size)
        YY_FATAL_ERROR ("buffer too small to expand macro");
      macro_expns = macro_expns->next;
      if (!macro_expns)
        macro_expns_end = NULL;
    }
  return result;
}

void
yyerror (const char *s)
{
  error_at_line (&lexer_line, "%s", s);
}

void
parse_file (const char *fname)
{
  yyin = fopen (fname, "r");
  lexer_line.file = fname;
  lexer_line.line = 1;
  if (yyin == NULL)
    {
      perror (fname);
      exit (1);
    }
  if (yyparse() != 0)
    exit (1);
  fclose (yyin);
}

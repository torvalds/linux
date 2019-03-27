%{ /* deffilep.y - parser for .def files */

/*   Copyright 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2005, 2007
     Free Software Foundation, Inc.

     This file is part of GNU Binutils.

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 2 of the License, or
     (at your option) any later version.

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "libiberty.h"
#include "safe-ctype.h"
#include "bfd.h"
#include "ld.h"
#include "ldmisc.h"
#include "deffile.h"

#define TRACE 0

#define ROUND_UP(a, b) (((a)+((b)-1))&~((b)-1))

/* Remap normal yacc parser interface names (yyparse, yylex, yyerror, etc),
   as well as gratuitiously global symbol names, so we can have multiple
   yacc generated parsers in ld.  Note that these are only the variables
   produced by yacc.  If other parser generators (bison, byacc, etc) produce
   additional global names that conflict at link time, then those parser
   generators need to be fixed instead of adding those names to this list.  */

#define	yymaxdepth def_maxdepth
#define	yyparse	def_parse
#define	yylex	def_lex
#define	yyerror	def_error
#define	yylval	def_lval
#define	yychar	def_char
#define	yydebug	def_debug
#define	yypact	def_pact	
#define	yyr1	def_r1			
#define	yyr2	def_r2			
#define	yydef	def_def		
#define	yychk	def_chk		
#define	yypgo	def_pgo		
#define	yyact	def_act		
#define	yyexca	def_exca
#define yyerrflag def_errflag
#define yynerrs	def_nerrs
#define	yyps	def_ps
#define	yypv	def_pv
#define	yys	def_s
#define	yy_yys	def_yys
#define	yystate	def_state
#define	yytmp	def_tmp
#define	yyv	def_v
#define	yy_yyv	def_yyv
#define	yyval	def_val
#define	yylloc	def_lloc
#define yyreds	def_reds		/* With YYDEBUG defined.  */
#define yytoks	def_toks		/* With YYDEBUG defined.  */
#define yylhs	def_yylhs
#define yylen	def_yylen
#define yydefred def_yydefred
#define yydgoto	def_yydgoto
#define yysindex def_yysindex
#define yyrindex def_yyrindex
#define yygindex def_yygindex
#define yytable	 def_yytable
#define yycheck	 def_yycheck

static void def_description (const char *);
static void def_exports (const char *, const char *, int, int);
static void def_heapsize (int, int);
static void def_import (const char *, const char *, const char *, const char *,
			int);
static void def_image_name (const char *, int, int);
static void def_section (const char *, int);
static void def_section_alt (const char *, const char *);
static void def_stacksize (int, int);
static void def_version (int, int);
static void def_directive (char *);
static int def_parse (void);
static int def_error (const char *);
static int def_lex (void);

static int lex_forced_token = 0;
static const char *lex_parse_string = 0;
static const char *lex_parse_string_end = 0;

%}

%union {
  char *id;
  int number;
};

%token NAME LIBRARY DESCRIPTION STACKSIZE HEAPSIZE CODE DATAU DATAL
%token SECTIONS EXPORTS IMPORTS VERSIONK BASE CONSTANTU CONSTANTL
%token PRIVATEU PRIVATEL
%token READ WRITE EXECUTE SHARED NONAMEU NONAMEL DIRECTIVE
%token <id> ID
%token <number> NUMBER
%type  <number> opt_base opt_ordinal
%type  <number> attr attr_list opt_number exp_opt_list exp_opt
%type  <id> opt_name opt_equal_name dot_name 

%%

start: start command
	| command
	;

command: 
		NAME opt_name opt_base { def_image_name ($2, $3, 0); }
	|	LIBRARY opt_name opt_base { def_image_name ($2, $3, 1); }
	|	DESCRIPTION ID { def_description ($2);}
	|	STACKSIZE NUMBER opt_number { def_stacksize ($2, $3);}
	|	HEAPSIZE NUMBER opt_number { def_heapsize ($2, $3);}
	|	CODE attr_list { def_section ("CODE", $2);}
	|	DATAU attr_list  { def_section ("DATA", $2);}
	|	SECTIONS seclist
	|	EXPORTS explist 
	|	IMPORTS implist
	|	VERSIONK NUMBER { def_version ($2, 0);}
	|	VERSIONK NUMBER '.' NUMBER { def_version ($2, $4);}
	|	DIRECTIVE ID { def_directive ($2);}
	;


explist:
		/* EMPTY */
	|	expline
	|	explist expline
	;

expline:
		/* The opt_comma is necessary to support both the usual
		  DEF file syntax as well as .drectve syntax which
		  mandates <expsym>,<expoptlist>.  */
		dot_name opt_equal_name opt_ordinal opt_comma exp_opt_list
			{ def_exports ($1, $2, $3, $5); }
	;
exp_opt_list:
		/* The opt_comma is necessary to support both the usual
		   DEF file syntax as well as .drectve syntax which
		   allows for comma separated opt list.  */
		exp_opt opt_comma exp_opt_list { $$ = $1 | $3; }
	|	{ $$ = 0; }
	;
exp_opt:
		NONAMEU		{ $$ = 1; }
	|	NONAMEL		{ $$ = 1; }
	|	CONSTANTU	{ $$ = 2; }
	|	CONSTANTL	{ $$ = 2; }
	|	DATAU		{ $$ = 4; }
	|	DATAL		{ $$ = 4; }
	|	PRIVATEU	{ $$ = 8; }
	|	PRIVATEL	{ $$ = 8; }
	;
implist:	
		implist impline
	|	impline
	;

impline:
               ID '=' ID '.' ID '.' ID     { def_import ($1, $3, $5, $7, -1); }
       |       ID '=' ID '.' ID '.' NUMBER { def_import ($1, $3, $5,  0, $7); }
       |       ID '=' ID '.' ID            { def_import ($1, $3,  0, $5, -1); }
       |       ID '=' ID '.' NUMBER        { def_import ($1, $3,  0,  0, $5); }
       |       ID '.' ID '.' ID            { def_import ( 0, $1, $3, $5, -1); }
       |       ID '.' ID                   { def_import ( 0, $1,  0, $3, -1); }
;

seclist:
		seclist secline
	|	secline
	;

secline:
	ID attr_list { def_section ($1, $2);}
	| ID ID { def_section_alt ($1, $2);}
	;

attr_list:
	attr_list opt_comma attr { $$ = $1 | $3; }
	| attr { $$ = $1; }
	;

opt_comma:
	','
	| 
	;
opt_number: ',' NUMBER { $$=$2;}
	|	   { $$=-1;}
	;
	
attr:
		READ	{ $$ = 1;}
	|	WRITE	{ $$ = 2;}	
	|	EXECUTE	{ $$=4;}
	|	SHARED	{ $$=8;}
	;

opt_name: ID		{ $$ = $1; }
	| ID '.' ID	
	  { 
	    char *name = xmalloc (strlen ($1) + 1 + strlen ($3) + 1);
	    sprintf (name, "%s.%s", $1, $3);
	    $$ = name;
	  }
	|		{ $$ = ""; }
	;

opt_ordinal: 
	  '@' NUMBER     { $$ = $2;}
	|                { $$ = -1;}
	;

opt_equal_name:
          '=' dot_name	{ $$ = $2; }
        | 		{ $$ =  0; }			 
	;

opt_base: BASE	'=' NUMBER	{ $$ = $3;}
	|	{ $$ = -1;}
	;

dot_name: ID		{ $$ = $1; }
	| dot_name '.' ID	
	  { 
	    char *name = xmalloc (strlen ($1) + 1 + strlen ($3) + 1);
	    sprintf (name, "%s.%s", $1, $3);
	    $$ = name;
	  }
	;
	

%%

/*****************************************************************************
 API
 *****************************************************************************/

static FILE *the_file;
static const char *def_filename;
static int linenumber;
static def_file *def;
static int saw_newline;

struct directive
  {
    struct directive *next;
    char *name;
    int len;
  };

static struct directive *directives = 0;

def_file *
def_file_empty (void)
{
  def_file *rv = xmalloc (sizeof (def_file));
  memset (rv, 0, sizeof (def_file));
  rv->is_dll = -1;
  rv->base_address = (bfd_vma) -1;
  rv->stack_reserve = rv->stack_commit = -1;
  rv->heap_reserve = rv->heap_commit = -1;
  rv->version_major = rv->version_minor = -1;
  return rv;
}

def_file *
def_file_parse (const char *filename, def_file *add_to)
{
  struct directive *d;

  the_file = fopen (filename, "r");
  def_filename = filename;
  linenumber = 1;
  if (!the_file)
    {
      perror (filename);
      return 0;
    }
  if (add_to)
    {
      def = add_to;
    }
  else
    {
      def = def_file_empty ();
    }

  saw_newline = 1;
  if (def_parse ())
    {
      def_file_free (def);
      fclose (the_file);
      return 0;
    }

  fclose (the_file);

  for (d = directives; d; d = d->next)
    {
#if TRACE
      printf ("Adding directive %08x `%s'\n", d->name, d->name);
#endif
      def_file_add_directive (def, d->name, d->len);
    }

  return def;
}

void
def_file_free (def_file *def)
{
  int i;

  if (!def)
    return;
  if (def->name)
    free (def->name);
  if (def->description)
    free (def->description);

  if (def->section_defs)
    {
      for (i = 0; i < def->num_section_defs; i++)
	{
	  if (def->section_defs[i].name)
	    free (def->section_defs[i].name);
	  if (def->section_defs[i].class)
	    free (def->section_defs[i].class);
	}
      free (def->section_defs);
    }

  if (def->exports)
    {
      for (i = 0; i < def->num_exports; i++)
	{
	  if (def->exports[i].internal_name
	      && def->exports[i].internal_name != def->exports[i].name)
	    free (def->exports[i].internal_name);
	  if (def->exports[i].name)
	    free (def->exports[i].name);
	}
      free (def->exports);
    }

  if (def->imports)
    {
      for (i = 0; i < def->num_imports; i++)
	{
	  if (def->imports[i].internal_name
	      && def->imports[i].internal_name != def->imports[i].name)
	    free (def->imports[i].internal_name);
	  if (def->imports[i].name)
	    free (def->imports[i].name);
	}
      free (def->imports);
    }

  while (def->modules)
    {
      def_file_module *m = def->modules;
      def->modules = def->modules->next;
      free (m);
    }

  free (def);
}

#ifdef DEF_FILE_PRINT
void
def_file_print (FILE *file, def_file *def)
{
  int i;

  fprintf (file, ">>>> def_file at 0x%08x\n", def);
  if (def->name)
    fprintf (file, "  name: %s\n", def->name ? def->name : "(unspecified)");
  if (def->is_dll != -1)
    fprintf (file, "  is dll: %s\n", def->is_dll ? "yes" : "no");
  if (def->base_address != (bfd_vma) -1)
    fprintf (file, "  base address: 0x%08x\n", def->base_address);
  if (def->description)
    fprintf (file, "  description: `%s'\n", def->description);
  if (def->stack_reserve != -1)
    fprintf (file, "  stack reserve: 0x%08x\n", def->stack_reserve);
  if (def->stack_commit != -1)
    fprintf (file, "  stack commit: 0x%08x\n", def->stack_commit);
  if (def->heap_reserve != -1)
    fprintf (file, "  heap reserve: 0x%08x\n", def->heap_reserve);
  if (def->heap_commit != -1)
    fprintf (file, "  heap commit: 0x%08x\n", def->heap_commit);

  if (def->num_section_defs > 0)
    {
      fprintf (file, "  section defs:\n");

      for (i = 0; i < def->num_section_defs; i++)
	{
	  fprintf (file, "    name: `%s', class: `%s', flags:",
		   def->section_defs[i].name, def->section_defs[i].class);
	  if (def->section_defs[i].flag_read)
	    fprintf (file, " R");
	  if (def->section_defs[i].flag_write)
	    fprintf (file, " W");
	  if (def->section_defs[i].flag_execute)
	    fprintf (file, " X");
	  if (def->section_defs[i].flag_shared)
	    fprintf (file, " S");
	  fprintf (file, "\n");
	}
    }

  if (def->num_exports > 0)
    {
      fprintf (file, "  exports:\n");

      for (i = 0; i < def->num_exports; i++)
	{
	  fprintf (file, "    name: `%s', int: `%s', ordinal: %d, flags:",
		   def->exports[i].name, def->exports[i].internal_name,
		   def->exports[i].ordinal);
	  if (def->exports[i].flag_private)
	    fprintf (file, " P");
	  if (def->exports[i].flag_constant)
	    fprintf (file, " C");
	  if (def->exports[i].flag_noname)
	    fprintf (file, " N");
	  if (def->exports[i].flag_data)
	    fprintf (file, " D");
	  fprintf (file, "\n");
	}
    }

  if (def->num_imports > 0)
    {
      fprintf (file, "  imports:\n");

      for (i = 0; i < def->num_imports; i++)
	{
	  fprintf (file, "    int: %s, from: `%s', name: `%s', ordinal: %d\n",
		   def->imports[i].internal_name,
		   def->imports[i].module,
		   def->imports[i].name,
		   def->imports[i].ordinal);
	}
    }

  if (def->version_major != -1)
    fprintf (file, "  version: %d.%d\n", def->version_major, def->version_minor);

  fprintf (file, "<<<< def_file at 0x%08x\n", def);
}
#endif

def_file_export *
def_file_add_export (def_file *def,
		     const char *external_name,
		     const char *internal_name,
		     int ordinal)
{
  def_file_export *e;
  int max_exports = ROUND_UP(def->num_exports, 32);

  if (def->num_exports >= max_exports)
    {
      max_exports = ROUND_UP(def->num_exports + 1, 32);
      if (def->exports)
	def->exports = xrealloc (def->exports,
				 max_exports * sizeof (def_file_export));
      else
	def->exports = xmalloc (max_exports * sizeof (def_file_export));
    }
  e = def->exports + def->num_exports;
  memset (e, 0, sizeof (def_file_export));
  if (internal_name && !external_name)
    external_name = internal_name;
  if (external_name && !internal_name)
    internal_name = external_name;
  e->name = xstrdup (external_name);
  e->internal_name = xstrdup (internal_name);
  e->ordinal = ordinal;
  def->num_exports++;
  return e;
}

def_file_module *
def_get_module (def_file *def, const char *name)
{
  def_file_module *s;

  for (s = def->modules; s; s = s->next)
    if (strcmp (s->name, name) == 0)
      return s;

  return NULL;
}

static def_file_module *
def_stash_module (def_file *def, const char *name)
{
  def_file_module *s;

  if ((s = def_get_module (def, name)) != NULL)
      return s;
  s = xmalloc (sizeof (def_file_module) + strlen (name));
  s->next = def->modules;
  def->modules = s;
  s->user_data = 0;
  strcpy (s->name, name);
  return s;
}

def_file_import *
def_file_add_import (def_file *def,
		     const char *name,
		     const char *module,
		     int ordinal,
		     const char *internal_name)
{
  def_file_import *i;
  int max_imports = ROUND_UP (def->num_imports, 16);

  if (def->num_imports >= max_imports)
    {
      max_imports = ROUND_UP (def->num_imports+1, 16);

      if (def->imports)
	def->imports = xrealloc (def->imports,
				 max_imports * sizeof (def_file_import));
      else
	def->imports = xmalloc (max_imports * sizeof (def_file_import));
    }
  i = def->imports + def->num_imports;
  memset (i, 0, sizeof (def_file_import));
  if (name)
    i->name = xstrdup (name);
  if (module)
    i->module = def_stash_module (def, module);
  i->ordinal = ordinal;
  if (internal_name)
    i->internal_name = xstrdup (internal_name);
  else
    i->internal_name = i->name;
  def->num_imports++;

  return i;
}

struct
{
  char *param;
  int token;
}
diropts[] =
{
  { "-heap", HEAPSIZE },
  { "-stack", STACKSIZE },
  { "-attr", SECTIONS },
  { "-export", EXPORTS },
  { 0, 0 }
};

void
def_file_add_directive (def_file *my_def, const char *param, int len)
{
  def_file *save_def = def;
  const char *pend = param + len;
  char * tend = (char *) param;
  int i;

  def = my_def;

  while (param < pend)
    {
      while (param < pend
	     && (ISSPACE (*param) || *param == '\n' || *param == 0))
	param++;

      if (param == pend)
	break;

      /* Scan forward until we encounter any of:
          - the end of the buffer
	  - the start of a new option
	  - a newline seperating options
          - a NUL seperating options.  */
      for (tend = (char *) (param + 1);
	   (tend < pend
	    && !(ISSPACE (tend[-1]) && *tend == '-')
	    && *tend != '\n' && *tend != 0);
	   tend++)
	;

      for (i = 0; diropts[i].param; i++)
	{
	  int len = strlen (diropts[i].param);

	  if (tend - param >= len
	      && strncmp (param, diropts[i].param, len) == 0
	      && (param[len] == ':' || param[len] == ' '))
	    {
	      lex_parse_string_end = tend;
	      lex_parse_string = param + len + 1;
	      lex_forced_token = diropts[i].token;
	      saw_newline = 0;
	      if (def_parse ())
		continue;
	      break;
	    }
	}

      if (!diropts[i].param)
	{
	  char saved;

	  saved = * tend;
	  * tend = 0;
	  /* xgettext:c-format */
	  einfo (_("Warning: .drectve `%s' unrecognized\n"), param);
	  * tend = saved;
	}

      lex_parse_string = 0;
      param = tend;
    }

  def = save_def;
}

/* Parser Callbacks.  */

static void
def_image_name (const char *name, int base, int is_dll)
{
  /* If a LIBRARY or NAME statement is specified without a name, there is nothing
     to do here.  We retain the output filename specified on command line.  */
  if (*name)
    {
      const char* image_name = lbasename (name);
      if (image_name != name)
	einfo ("%s:%d: Warning: path components stripped from %s, '%s'\n",
	       def_filename, linenumber, is_dll ? "LIBRARY" : "NAME",
	       name);
      if (def->name)
	free (def->name);
      /* Append the default suffix, if none specified.  */ 
      if (strchr (image_name, '.') == 0)
	{
	  const char * suffix = is_dll ? ".dll" : ".exe";

	  def->name = xmalloc (strlen (image_name) + strlen (suffix) + 1);
	  sprintf (def->name, "%s%s", image_name, suffix);
        }
      else
	def->name = xstrdup (image_name);
    }

  /* Honor a BASE address statement, even if LIBRARY string is empty.  */
  def->base_address = base;
  def->is_dll = is_dll;
}

static void
def_description (const char *text)
{
  int len = def->description ? strlen (def->description) : 0;

  len += strlen (text) + 1;
  if (def->description)
    {
      def->description = xrealloc (def->description, len);
      strcat (def->description, text);
    }
  else
    {
      def->description = xmalloc (len);
      strcpy (def->description, text);
    }
}

static void
def_stacksize (int reserve, int commit)
{
  def->stack_reserve = reserve;
  def->stack_commit = commit;
}

static void
def_heapsize (int reserve, int commit)
{
  def->heap_reserve = reserve;
  def->heap_commit = commit;
}

static void
def_section (const char *name, int attr)
{
  def_file_section *s;
  int max_sections = ROUND_UP (def->num_section_defs, 4);

  if (def->num_section_defs >= max_sections)
    {
      max_sections = ROUND_UP (def->num_section_defs+1, 4);

      if (def->section_defs)
	def->section_defs = xrealloc (def->section_defs,
				      max_sections * sizeof (def_file_import));
      else
	def->section_defs = xmalloc (max_sections * sizeof (def_file_import));
    }
  s = def->section_defs + def->num_section_defs;
  memset (s, 0, sizeof (def_file_section));
  s->name = xstrdup (name);
  if (attr & 1)
    s->flag_read = 1;
  if (attr & 2)
    s->flag_write = 1;
  if (attr & 4)
    s->flag_execute = 1;
  if (attr & 8)
    s->flag_shared = 1;

  def->num_section_defs++;
}

static void
def_section_alt (const char *name, const char *attr)
{
  int aval = 0;

  for (; *attr; attr++)
    {
      switch (*attr)
	{
	case 'R':
	case 'r':
	  aval |= 1;
	  break;
	case 'W':
	case 'w':
	  aval |= 2;
	  break;
	case 'X':
	case 'x':
	  aval |= 4;
	  break;
	case 'S':
	case 's':
	  aval |= 8;
	  break;
	}
    }
  def_section (name, aval);
}

static void
def_exports (const char *external_name,
	     const char *internal_name,
	     int ordinal,
	     int flags)
{
  def_file_export *dfe;

  if (!internal_name && external_name)
    internal_name = external_name;
#if TRACE
  printf ("def_exports, ext=%s int=%s\n", external_name, internal_name);
#endif

  dfe = def_file_add_export (def, external_name, internal_name, ordinal);
  if (flags & 1)
    dfe->flag_noname = 1;
  if (flags & 2)
    dfe->flag_constant = 1;
  if (flags & 4)
    dfe->flag_data = 1;
  if (flags & 8)
    dfe->flag_private = 1;
}

static void
def_import (const char *internal_name,
	    const char *module,
	    const char *dllext,
	    const char *name,
	    int ordinal)
{
  char *buf = 0;
  const char *ext = dllext ? dllext : "dll";    
   
  buf = xmalloc (strlen (module) + strlen (ext) + 2);
  sprintf (buf, "%s.%s", module, ext);
  module = buf;

  def_file_add_import (def, name, module, ordinal, internal_name);
  if (buf)
    free (buf);
}

static void
def_version (int major, int minor)
{
  def->version_major = major;
  def->version_minor = minor;
}

static void
def_directive (char *str)
{
  struct directive *d = xmalloc (sizeof (struct directive));

  d->next = directives;
  directives = d;
  d->name = xstrdup (str);
  d->len = strlen (str);
}

static int
def_error (const char *err)
{
  einfo ("%P: %s:%d: %s\n",
	 def_filename ? def_filename : "<unknown-file>", linenumber, err);
  return 0;
}


/* Lexical Scanner.  */

#undef TRACE
#define TRACE 0

/* Never freed, but always reused as needed, so no real leak.  */
static char *buffer = 0;
static int buflen = 0;
static int bufptr = 0;

static void
put_buf (char c)
{
  if (bufptr == buflen)
    {
      buflen += 50;		/* overly reasonable, eh?  */
      if (buffer)
	buffer = xrealloc (buffer, buflen + 1);
      else
	buffer = xmalloc (buflen + 1);
    }
  buffer[bufptr++] = c;
  buffer[bufptr] = 0;		/* not optimal, but very convenient.  */
}

static struct
{
  char *name;
  int token;
}
tokens[] =
{
  { "BASE", BASE },
  { "CODE", CODE },
  { "CONSTANT", CONSTANTU },
  { "constant", CONSTANTL },
  { "DATA", DATAU },
  { "data", DATAL },
  { "DESCRIPTION", DESCRIPTION },
  { "DIRECTIVE", DIRECTIVE },
  { "EXECUTE", EXECUTE },
  { "EXPORTS", EXPORTS },
  { "HEAPSIZE", HEAPSIZE },
  { "IMPORTS", IMPORTS },
  { "LIBRARY", LIBRARY },
  { "NAME", NAME },
  { "NONAME", NONAMEU },
  { "noname", NONAMEL },
  { "PRIVATE", PRIVATEU },
  { "private", PRIVATEL },
  { "READ", READ },
  { "SECTIONS", SECTIONS },
  { "SEGMENTS", SECTIONS },
  { "SHARED", SHARED },
  { "STACKSIZE", STACKSIZE },
  { "VERSION", VERSIONK },
  { "WRITE", WRITE },
  { 0, 0 }
};

static int
def_getc (void)
{
  int rv;

  if (lex_parse_string)
    {
      if (lex_parse_string >= lex_parse_string_end)
	rv = EOF;
      else
	rv = *lex_parse_string++;
    }
  else
    {
      rv = fgetc (the_file);
    }
  if (rv == '\n')
    saw_newline = 1;
  return rv;
}

static int
def_ungetc (int c)
{
  if (lex_parse_string)
    {
      lex_parse_string--;
      return c;
    }
  else
    return ungetc (c, the_file);
}

static int
def_lex (void)
{
  int c, i, q;

  if (lex_forced_token)
    {
      i = lex_forced_token;
      lex_forced_token = 0;
#if TRACE
      printf ("lex: forcing token %d\n", i);
#endif
      return i;
    }

  c = def_getc ();

  /* Trim leading whitespace.  */
  while (c != EOF && (c == ' ' || c == '\t') && saw_newline)
    c = def_getc ();

  if (c == EOF)
    {
#if TRACE
      printf ("lex: EOF\n");
#endif
      return 0;
    }

  if (saw_newline && c == ';')
    {
      do
	{
	  c = def_getc ();
	}
      while (c != EOF && c != '\n');
      if (c == '\n')
	return def_lex ();
      return 0;
    }

  /* Must be something else.  */
  saw_newline = 0;

  if (ISDIGIT (c))
    {
      bufptr = 0;
      while (c != EOF && (ISXDIGIT (c) || (c == 'x')))
	{
	  put_buf (c);
	  c = def_getc ();
	}
      if (c != EOF)
	def_ungetc (c);
      yylval.number = strtoul (buffer, 0, 0);
#if TRACE
      printf ("lex: `%s' returns NUMBER %d\n", buffer, yylval.number);
#endif
      return NUMBER;
    }

  if (ISALPHA (c) || strchr ("$:-_?@", c))
    {
      bufptr = 0;
      q = c;
      put_buf (c);
      c = def_getc ();

      if (q == '@')
	{
          if (ISBLANK (c) ) /* '@' followed by whitespace.  */
	    return (q);
          else if (ISDIGIT (c)) /* '@' followed by digit.  */
            {
	      def_ungetc (c);
              return (q);
	    }
#if TRACE
	  printf ("lex: @ returns itself\n");
#endif
	}

      while (c != EOF && (ISALNUM (c) || strchr ("$:-_?/@", c)))
	{
	  put_buf (c);
	  c = def_getc ();
	}
      if (c != EOF)
	def_ungetc (c);
      if (ISALPHA (q)) /* Check for tokens.  */
	{
          for (i = 0; tokens[i].name; i++)
	    if (strcmp (tokens[i].name, buffer) == 0)
	      {
#if TRACE
	        printf ("lex: `%s' is a string token\n", buffer);
#endif
	        return tokens[i].token;
	      }
	}
#if TRACE
      printf ("lex: `%s' returns ID\n", buffer);
#endif
      yylval.id = xstrdup (buffer);
      return ID;
    }

  if (c == '\'' || c == '"')
    {
      q = c;
      c = def_getc ();
      bufptr = 0;

      while (c != EOF && c != q)
	{
	  put_buf (c);
	  c = def_getc ();
	}
      yylval.id = xstrdup (buffer);
#if TRACE
      printf ("lex: `%s' returns ID\n", buffer);
#endif
      return ID;
    }

  if (c == '=' || c == '.' || c == ',')
    {
#if TRACE
      printf ("lex: `%c' returns itself\n", c);
#endif
      return c;
    }

  if (c == '\n')
    {
      linenumber++;
      saw_newline = 1;
    }

  /*printf ("lex: 0x%02x ignored\n", c); */
  return def_lex ();
}

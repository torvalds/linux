/* Demangler for GNU C++ - main program
   Copyright 1989, 1991, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2005, 2007 Free Software Foundation, Inc.
   Written by James Clark (jjc@jclark.uucp)
   Rewritten by Fred Fish (fnf@cygnus.com) for ARM and Lucid demangling
   Modified by Satish Pai (pai@apollo.hp.com) for HP demangling

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
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libiberty.h"
#include "demangle.h"
#include "getopt.h"
#include "safe-ctype.h"
#include "bucomm.h"

static int flags = DMGL_PARAMS | DMGL_ANSI | DMGL_VERBOSE;
static int strip_underscore = TARGET_PREPENDS_UNDERSCORE;

static const struct option long_options[] =
{
  {"strip-underscore", no_argument, NULL, '_'},
  {"format", required_argument, NULL, 's'},
  {"help", no_argument, NULL, 'h'},
  {"no-params", no_argument, NULL, 'p'},
  {"no-strip-underscores", no_argument, NULL, 'n'},
  {"no-verbose", no_argument, NULL, 'i'},
  {"types", no_argument, NULL, 't'},
  {"version", no_argument, NULL, 'v'},
  {NULL, no_argument, NULL, 0}
};

static void
demangle_it (char *mangled_name)
{
  char *result;
  unsigned int skip_first = 0;

  /* _ and $ are sometimes found at the start of function names
     in assembler sources in order to distinguish them from other
     names (eg register names).  So skip them here.  */
  if (mangled_name[0] == '.' || mangled_name[0] == '$')
    ++skip_first;
  if (strip_underscore && mangled_name[skip_first] == '_')
    ++skip_first;

  result = cplus_demangle (mangled_name + skip_first, flags);

  if (result == NULL)
    printf ("%s",mangled_name);
  else
    {
      if (mangled_name[0] == '.')
	putchar ('.');
      printf ("%s",result);
      free (result);
    }
}

static void
print_demangler_list (FILE *stream)
{
  const struct demangler_engine *demangler;

  fprintf (stream, "{%s", libiberty_demanglers->demangling_style_name);

  for (demangler = libiberty_demanglers + 1;
       demangler->demangling_style != unknown_demangling;
       ++demangler)
    fprintf (stream, ",%s", demangler->demangling_style_name);

  fprintf (stream, "}");
}

static void
usage (FILE *stream, int status)
{
  fprintf (stream, "\
Usage: %s [options] [mangled names]\n", program_name);
  fprintf (stream, "\
Options are:\n\
  [-_|--strip-underscore]     Ignore first leading underscore%s\n",
	   TARGET_PREPENDS_UNDERSCORE ? " (default)" : "");
  fprintf (stream, "\
  [-n|--no-strip-underscore]  Do not ignore a leading underscore%s\n",
	   TARGET_PREPENDS_UNDERSCORE ? "" : " (default)");
  fprintf (stream, "\
  [-p|--no-params]            Do not display function arguments\n\
  [-i|--no-verbose]           Do not show implementation details (if any)\n\
  [-t|--types]                Also attempt to demangle type encodings\n\
  [-s|--format ");
  print_demangler_list (stream);
  fprintf (stream, "]\n");

  fprintf (stream, "\
  [@<file>]                   Read extra options from <file>\n\
  [-h|--help]                 Display this information\n\
  [-v|--version]              Show the version information\n\
Demangled names are displayed to stdout.\n\
If a name cannot be demangled it is just echoed to stdout.\n\
If no names are provided on the command line, stdin is read.\n");
  if (REPORT_BUGS_TO[0] && status == 0)
    fprintf (stream, _("Report bugs to %s.\n"), REPORT_BUGS_TO);
  exit (status);
}

/* Return the string of non-alnum characters that may occur
   as a valid symbol component, in the standard assembler symbol
   syntax.  */

static const char *
standard_symbol_characters (void)
{
  return "_$.";
}

/* Return the string of non-alnum characters that may occur
   as a valid symbol name component in an HP object file.

   Note that, since HP's compiler generates object code straight from
   C++ source, without going through an assembler, its mangled
   identifiers can use all sorts of characters that no assembler would
   tolerate, so the alphabet this function creates is a little odd.
   Here are some sample mangled identifiers offered by HP:

	typeid*__XT24AddressIndExpClassMember_
	[Vftptr]key:__dt__32OrdinaryCompareIndExpClassMemberFv
	__ct__Q2_9Elf64_Dyn18{unnamed.union.#1}Fv

   This still seems really weird to me, since nowhere else in this
   file is there anything to recognize curly brackets, parens, etc.
   I've talked with Srikanth <srikanth@cup.hp.com>, and he assures me
   this is right, but I still strongly suspect that there's a
   misunderstanding here.

   If we decide it's better for c++filt to use HP's assembler syntax
   to scrape identifiers out of its input, here's the definition of
   the symbol name syntax from the HP assembler manual:

       Symbols are composed of uppercase and lowercase letters, decimal
       digits, dollar symbol, period (.), ampersand (&), pound sign(#) and
       underscore (_). A symbol can begin with a letter, digit underscore or
       dollar sign. If a symbol begins with a digit, it must contain a
       non-digit character.

   So have fun.  */
static const char *
hp_symbol_characters (void)
{
  return "_$.<>#,*&[]:(){}";
}

extern int main (int, char **);

int
main (int argc, char **argv)
{
  int c;
  const char *valid_symbols;
  enum demangling_styles style = auto_demangling;

  program_name = argv[0];
  xmalloc_set_program_name (program_name);

  expandargv (&argc, &argv);

  while ((c = getopt_long (argc, argv, "_hinps:tv", long_options, (int *) 0)) != EOF)
    {
      switch (c)
	{
	case '?':
	  usage (stderr, 1);
	  break;
	case 'h':
	  usage (stdout, 0);
	case 'n':
	  strip_underscore = 0;
	  break;
	case 'p':
	  flags &= ~ DMGL_PARAMS;
	  break;
	case 't':
	  flags |= DMGL_TYPES;
	  break;
	case 'i':
	  flags &= ~ DMGL_VERBOSE;
	  break;
	case 'v':
	  print_version ("c++filt");
	  return 0;
	case '_':
	  strip_underscore = 1;
	  break;
	case 's':
	  style = cplus_demangle_name_to_style (optarg);
	  if (style == unknown_demangling)
	    {
	      fprintf (stderr, "%s: unknown demangling style `%s'\n",
		       program_name, optarg);
	      return 1;
	    }
	  cplus_demangle_set_style (style);
	  break;
	}
    }

  if (optind < argc)
    {
      for ( ; optind < argc; optind++)
	{
	  demangle_it (argv[optind]);
	  putchar ('\n');
	}

      return 0;
    }

  switch (current_demangling_style)
    {
    case gnu_demangling:
    case lucid_demangling:
    case arm_demangling:
    case java_demangling:
    case edg_demangling:
    case gnat_demangling:
    case gnu_v3_demangling:
    case auto_demangling:
      valid_symbols = standard_symbol_characters ();
      break;
    case hp_demangling:
      valid_symbols = hp_symbol_characters ();
      break;
    default:
      /* Folks should explicitly indicate the appropriate alphabet for
	 each demangling.  Providing a default would allow the
	 question to go unconsidered.  */
      fatal ("Internal error: no symbol alphabet for current style");
    }

  for (;;)
    {
      static char mbuffer[32767];
      unsigned i = 0;

      c = getchar ();
      /* Try to read a mangled name.  */
      while (c != EOF && (ISALNUM (c) || strchr (valid_symbols, c)))
	{
	  if (i >= sizeof (mbuffer) - 1)
	    break;
	  mbuffer[i++] = c;
	  c = getchar ();
	}

      if (i > 0)
	{
	  mbuffer[i] = 0;
	  demangle_it (mbuffer);
	}

      if (c == EOF)
	break;

      /* Echo the whitespace characters so that the output looks
	 like the input, only with the mangled names demangled.  */
      putchar (c);
      if (c == '\n')
	fflush (stdout);
    }

  fflush (stdout);
  return 0;
}

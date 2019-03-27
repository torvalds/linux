/* Specific flags and argument handling of the C preprocessor.
   Copyright (C) 1999 Free Software Foundation, Inc.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "gcc.h"

/* The `cpp' executable installed in $(bindir) and $(cpp_install_dir)
   is a customized version of the gcc driver.  It forces -E; -S and -c
   are errors.  It defaults to -x c for files with unrecognized
   extensions, unless -x options appear in argv, in which case we
   assume the user knows what they're doing.  If no explicit input is
   mentioned, it will read stdin.  */

#ifndef SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR) DEFAULT_SWITCH_TAKES_ARG(CHAR)
#endif

#ifndef WORD_SWITCH_TAKES_ARG
#define WORD_SWITCH_TAKES_ARG(STR) DEFAULT_WORD_SWITCH_TAKES_ARG (STR)
#endif

/* Suffixes for known sorts of input files.  Note that we do not list
   files which are normally considered to have been preprocessed already,
   since the user's expectation is that `cpp' always preprocesses.  */
static const char *const known_suffixes[] =
{
  ".c",  ".C",   ".S",   ".m",
  ".cc", ".cxx", ".cpp", ".cp",  ".c++",
  NULL
};

/* Filter argc and argv before processing by the gcc driver proper.  */
void
lang_specific_driver (int *in_argc, const char *const **in_argv,
		      int *in_added_libraries ATTRIBUTE_UNUSED)
{
  int argc = *in_argc;
  const char *const *argv = *in_argv;

  /* Do we need to read stdin? */
  int read_stdin = 1;

  /* Do we need to insert -E? */
  int need_E = 1;

  /* Have we seen an input file? */
  int seen_input = 0;

  /* Positions to insert -xc, -xassembler-with-cpp, and -o, if necessary.
     0 means unnecessary.  */
  int lang_c_here = 0;
  int lang_S_here = 0;
  int o_here = 0;

  /* Do we need to fix up an input file with an unrecognized suffix? */
  int need_fixups = 1;

  int i, j, quote = 0;
  const char **new_argv;
  int new_argc;
  extern int is_cpp_driver;

  is_cpp_driver = 1;

  /* First pass.  If we see an -S or -c, barf.  If we see an input file,
     turn off read_stdin.  If we see a second input file, it is actually
     the output file.  If we see a third input file, barf.  */
  for (i = 1; i < argc; i++)
    {
      if (quote == 1)
	{
	  quote = 0;
	  continue;
	}

      if (argv[i][0] == '-')
	{
	  if (argv[i][1] == '\0')
	    read_stdin = 0;
	  else if (argv[i][2] == '\0')
	    {
	      if (argv[i][1] == 'E')
		need_E = 0;
	      else if (argv[i][1] == 'S' || argv[i][1] == 'c')
		{
		  fatal ("\"%s\" is not a valid option to the preprocessor",
			 argv[i]);
		  return;
		}
	      else if (argv[i][1] == 'x')
		{
		  need_fixups = 0;
		  quote = 1;
		}
	      else if (SWITCH_TAKES_ARG (argv[i][1]))
		quote = 1;
	    }
	  else if (argv[i][1] == 'x')
	    need_fixups = 0;
	  else if (WORD_SWITCH_TAKES_ARG (&argv[i][1]))
	    quote = 1;
	}
      else /* not an option */
	{
	  seen_input++;
	  if (seen_input == 3)
	    {
	      fatal ("too many input files");
	      return;
	    }
	  else if (seen_input == 2)
	    {
	      o_here = i;
	    }
	  else
	    {
	      read_stdin = 0;
	      if (need_fixups)
		{
		  int l = strlen (argv[i]);
		  int known = 0;
		  const char *const *suff;

		  for (suff = known_suffixes; *suff; suff++)
		    if (!strcmp (*suff, &argv[i][l - strlen(*suff)]))
		      {
			known = 1;
			break;
		      }

		  if (! known)
		    {
		      /* .s files are a special case; we have to treat
			 them like .S files so -D__ASSEMBLER__ will be
			 in effect.  */
		      if (!strcmp (".s", &argv[i][l - 2]))
			lang_S_here = i;
		      else
			lang_c_here = i;
		    }
		}
	    }
	}
    }

  /* If we don't need to edit the command line, we can bail early.  */

  new_argc = argc + need_E + read_stdin
    + !!o_here + !!lang_c_here + !!lang_S_here;

  if (new_argc == argc)
    return;

  /* One more slot for a terminating null.  */
  new_argv = XNEWVEC (const char *, new_argc + 1);

  new_argv[0] = argv[0];
  j = 1;

  if (need_E)
    new_argv[j++] = "-E";

  for (i = 1; i < argc; i++, j++)
    {
      if (i == lang_c_here)
	new_argv[j++] = "-xc";
      else if (i == lang_S_here)
	new_argv[j++] = "-xassembler-with-cpp";
      else if (i == o_here)
	new_argv[j++] = "-o";

      new_argv[j] = argv[i];
    }

  if (read_stdin)
    new_argv[j++] = "-";

  new_argv[j] = NULL;
  *in_argc = new_argc;
  *in_argv = new_argv;
}

/* Called before linking.  Returns 0 on success and -1 on failure.  */
int lang_specific_pre_link (void)
{
  return 0;  /* Not used for cpp.  */
}

/* Number of extra output files that lang_specific_pre_link may generate.  */
int lang_specific_extra_outfiles = 0;  /* Not used for cpp.  */

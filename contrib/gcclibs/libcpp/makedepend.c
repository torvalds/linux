/* Dependency generator utility.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Contributed by Zack Weinberg, May 2004

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

 In other words, you are welcome to use, share and improve this program.
 You are forbidden to forbid anyone else to use, share and improve
 what you give them.   Help stamp out software-hoarding!  */

#include "config.h"
#include "system.h"
#include "line-map.h"
#include "cpplib.h"
#include "getopt.h"
#include "mkdeps.h"

const char *progname;
const char *vpath;

static const char *output_file;
static bool had_errors;

/* Option lists, to give to cpplib before each input file.  */
struct cmd_line_macro
{
  struct cmd_line_macro *next;
  bool is_undef;
  const char *macro;
};

static struct cmd_line_macro *cmd_line_macros;
static cpp_dir *cmd_line_searchpath;

static void
add_clm (const char *macro, bool is_undef)
{
  struct cmd_line_macro *clm = XNEW (struct cmd_line_macro);
  clm->next = cmd_line_macros;
  clm->is_undef = is_undef;
  clm->macro = macro;
  cmd_line_macros = clm;
}

static void
add_dir (char *name, bool sysp)
{
  cpp_dir *dir = XNEW (cpp_dir);
  dir->next = cmd_line_searchpath;
  dir->name = name;
  dir->sysp = sysp;
  dir->construct = 0;
  dir->user_supplied_p = 1;
  cmd_line_searchpath = dir;
}

/* Command line processing.  */

static void ATTRIBUTE_NORETURN
usage (int errcode)
{
  fprintf (stderr,
"usage: %s [-vh] [-V vpath] [-Dname[=def]...] [-Uname] [-Idir...] [-o file] sources...\n",
	   progname);
  exit (errcode);
}

static int
parse_options (int argc, char **argv)
{
  static const struct option longopts[] = {
    { "--help", no_argument, 0, 'h' },
    { 0, 0, 0, 0 }
  };

  for (;;)
    switch (getopt_long (argc, argv, "hD:U:I:J:o:V:", longopts, 0))
      {
      case 'h': usage (0);
      case 'D': add_clm (optarg, false); break;
      case 'U': add_clm (optarg, true);  break;
      case 'I': add_dir (optarg, false); break;
      case 'J': add_dir (optarg, true);  break;
      case 'o':
	if (output_file)
	  {
	    fprintf (stderr, "%s: too many output files\n", progname);
	    usage (2);
	  }
	output_file = optarg;
	break;
      case 'V':
	if (vpath)
	  {
	    fprintf (stderr, "%s: too many vpaths\n", progname);
	    usage (2);
	  }
	vpath = optarg;
	break;
      case '?':
	usage (2);  /* getopt has issued the error message.  */

      case -1: /* end of options */
	if (optind == argc)
	  {
	    fprintf (stderr, "%s: no input files\n", progname);
	    usage (2);
	  }
	return optind;

      default:
	abort ();
      }
}

/* Set up cpplib from command line options.  */
static cpp_reader *
reader_init (struct line_maps *line_table)
{
  cpp_reader *reader;
  cpp_options *options;

  linemap_init (line_table);
  reader = cpp_create_reader (CLK_GNUC89, 0, line_table);

  /* Ignore warnings and errors (we don't have access to system
     headers).  Request dependency output.  */
  options = cpp_get_options (reader);
  options->inhibit_warnings = 1;
  options->inhibit_errors = 1;
  options->deps.style = DEPS_USER;

  /* Further initialization.  */
  cpp_post_options (reader);
  cpp_init_iconv (reader);
  cpp_set_include_chains (reader, cmd_line_searchpath, cmd_line_searchpath,
			  false);
  if (vpath)
    {
      struct deps *deps = cpp_get_deps (reader);
      deps_add_vpath (deps, vpath);
    }

  return reader;
}

/* Process one input source file.  */
static void
process_file (const char *file)
{
  struct line_maps line_table;
  cpp_reader *reader = reader_init (&line_table);

  if (!cpp_read_main_file (reader, file))
    had_errors = true;
  else
    {
      struct cmd_line_macro *clm;

      cpp_init_builtins (reader, true);
      for (clm = cmd_line_macros; clm; clm = clm->next)
	(clm->is_undef ? cpp_undef : cpp_define) (reader, clm->macro);

      cpp_scan_nooutput (reader);
      if (cpp_finish (reader, stdout))
	had_errors = true;
    }
  cpp_destroy (reader);
  linemap_free (&line_table);
}

/* Master control.  */

int
main(int argc, char **argv)
{
  int first_input, i;

  progname = argv[0];
  xmalloc_set_program_name (progname);

  first_input = parse_options (argc, argv);
  if (output_file)
    if (!freopen (output_file, "w", stdout))
      {
	perror (output_file);
	return 1;
      }

  for (i = first_input; i < argc; i++)
    process_file (argv[i]);

  return had_errors;
}

/* Main program of GNU linker.
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Written by Steve Chamberlain steve@cygnus.com

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

#include "sysdep.h"
#include "bfd.h"
#include "safe-ctype.h"
#include "libiberty.h"
#include "progress.h"
#include "bfdlink.h"
#include "filenames.h"

#include "ld.h"
#include "ldmain.h"
#include "ldmisc.h"
#include "ldwrite.h"
#include "ldexp.h"
#include "ldlang.h"
#include <ldgram.h>
#include "ldlex.h"
#include "ldfile.h"
#include "ldemul.h"
#include "ldctor.h"

/* Somewhere above, sys/stat.h got included.  */
#if !defined(S_ISDIR) && defined(S_IFDIR)
#define	S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#include <string.h>

#ifdef HAVE_SBRK
#if !HAVE_DECL_SBRK
extern void *sbrk ();
#endif
#endif

#ifndef TARGET_SYSTEM_ROOT
#define TARGET_SYSTEM_ROOT ""
#endif

/* EXPORTS */

char *default_target;
const char *output_filename = "a.out";

/* Name this program was invoked by.  */
char *program_name;

/* The prefix for system library directories.  */
const char *ld_sysroot;

/* The canonical representation of ld_sysroot.  */
char * ld_canon_sysroot;
int ld_canon_sysroot_len;

/* The file that we're creating.  */
bfd *output_bfd = 0;

/* Set by -G argument, for MIPS ECOFF target.  */
int g_switch_value = 8;

/* Nonzero means print names of input files as processed.  */
bfd_boolean trace_files;

/* Nonzero means same, but note open failures, too.  */
bfd_boolean trace_file_tries;

/* Nonzero means version number was printed, so exit successfully
   instead of complaining if no input files are given.  */
bfd_boolean version_printed;

/* Nonzero means link in every member of an archive.  */
bfd_boolean whole_archive;

/* Nonzero means create DT_NEEDED entries only if a dynamic library
   actually satisfies some reference in a regular object.  */
bfd_boolean as_needed;

/* Nonzero means never create DT_NEEDED entries for dynamic libraries
   in DT_NEEDED tags.  */
bfd_boolean add_needed = FALSE;

/* TRUE if we should demangle symbol names.  */
bfd_boolean demangling;

args_type command_line;

ld_config_type config;

sort_type sort_section;

static const char *get_sysroot
  (int, char **);
static char *get_emulation
  (int, char **);
static void set_scripts_dir
  (void);
static bfd_boolean add_archive_element
  (struct bfd_link_info *, bfd *, const char *);
static bfd_boolean multiple_definition
  (struct bfd_link_info *, const char *, bfd *, asection *, bfd_vma,
   bfd *, asection *, bfd_vma);
static bfd_boolean multiple_common
  (struct bfd_link_info *, const char *, bfd *, enum bfd_link_hash_type,
   bfd_vma, bfd *, enum bfd_link_hash_type, bfd_vma);
static bfd_boolean add_to_set
  (struct bfd_link_info *, struct bfd_link_hash_entry *,
   bfd_reloc_code_real_type, bfd *, asection *, bfd_vma);
static bfd_boolean constructor_callback
  (struct bfd_link_info *, bfd_boolean, const char *, bfd *,
   asection *, bfd_vma);
static bfd_boolean warning_callback
  (struct bfd_link_info *, const char *, const char *, bfd *,
   asection *, bfd_vma);
static void warning_find_reloc
  (bfd *, asection *, void *);
static bfd_boolean undefined_symbol
  (struct bfd_link_info *, const char *, bfd *, asection *, bfd_vma,
   bfd_boolean);
static bfd_boolean reloc_overflow
  (struct bfd_link_info *, struct bfd_link_hash_entry *, const char *,
   const char *, bfd_vma, bfd *, asection *, bfd_vma);
static bfd_boolean reloc_dangerous
  (struct bfd_link_info *, const char *, bfd *, asection *, bfd_vma);
static bfd_boolean unattached_reloc
  (struct bfd_link_info *, const char *, bfd *, asection *, bfd_vma);
static bfd_boolean notice
  (struct bfd_link_info *, const char *, bfd *, asection *, bfd_vma);

static struct bfd_link_callbacks link_callbacks =
{
  add_archive_element,
  multiple_definition,
  multiple_common,
  add_to_set,
  constructor_callback,
  warning_callback,
  undefined_symbol,
  reloc_overflow,
  reloc_dangerous,
  unattached_reloc,
  notice,
  einfo,
  info_msg,
  minfo,
  ldlang_override_segment_assignment
};

struct bfd_link_info link_info;

static void
remove_output (void)
{
  if (output_filename)
    {
      if (output_bfd)
	bfd_cache_close (output_bfd);
      if (delete_output_file_on_failure)
	unlink_if_ordinary (output_filename);
    }
}

int
main (int argc, char **argv)
{
  char *emulation;
  long start_time = get_run_time ();

#if defined (HAVE_SETLOCALE) && defined (HAVE_LC_MESSAGES)
  setlocale (LC_MESSAGES, "");
#endif
#if defined (HAVE_SETLOCALE)
  setlocale (LC_CTYPE, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  program_name = argv[0];
  xmalloc_set_program_name (program_name);

  START_PROGRESS (program_name, 0);

  expandargv (&argc, &argv);

  bfd_init ();

  bfd_set_error_program_name (program_name);

  xatexit (remove_output);

  /* Set up the sysroot directory.  */
  ld_sysroot = get_sysroot (argc, argv);
  if (*ld_sysroot)
    {
      if (*TARGET_SYSTEM_ROOT == 0)
	{
	  einfo ("%P%F: this linker was not configured to use sysroots\n");
	  ld_sysroot = "";
	}
      else
	ld_canon_sysroot = lrealpath (ld_sysroot);
    }
  if (ld_canon_sysroot)
    ld_canon_sysroot_len = strlen (ld_canon_sysroot);
  else
    ld_canon_sysroot_len = -1;

  /* Set the default BFD target based on the configured target.  Doing
     this permits the linker to be configured for a particular target,
     and linked against a shared BFD library which was configured for
     a different target.  The macro TARGET is defined by Makefile.  */
  if (! bfd_set_default_target (TARGET))
    {
      einfo (_("%X%P: can't set BFD default target to `%s': %E\n"), TARGET);
      xexit (1);
    }

#if YYDEBUG
  {
    extern int yydebug;
    yydebug = 1;
  }
#endif

  config.build_constructors = TRUE;
  config.rpath_separator = ':';
  config.split_by_reloc = (unsigned) -1;
  config.split_by_file = (bfd_size_type) -1;
  config.make_executable = TRUE;
  config.magic_demand_paged = TRUE;
  config.text_read_only = TRUE;

  command_line.warn_mismatch = TRUE;
  command_line.warn_search_mismatch = TRUE;
  command_line.check_section_addresses = TRUE;

  /* We initialize DEMANGLING based on the environment variable
     COLLECT_NO_DEMANGLE.  The gcc collect2 program will demangle the
     output of the linker, unless COLLECT_NO_DEMANGLE is set in the
     environment.  Acting the same way here lets us provide the same
     interface by default.  */
  demangling = getenv ("COLLECT_NO_DEMANGLE") == NULL;

  link_info.allow_undefined_version = TRUE;
  link_info.keep_memory = TRUE;
  link_info.combreloc = TRUE;
  link_info.strip_discarded = TRUE;
  link_info.emit_hash = TRUE;
  link_info.callbacks = &link_callbacks;
  link_info.input_bfds_tail = &link_info.input_bfds;
  /* SVR4 linkers seem to set DT_INIT and DT_FINI based on magic _init
     and _fini symbols.  We are compatible.  */
  link_info.init_function = "_init";
  link_info.fini_function = "_fini";
  link_info.relax_pass = 1;
  link_info.pei386_auto_import = -1;
  link_info.spare_dynamic_tags = 5;

  ldfile_add_arch ("");
  emulation = get_emulation (argc, argv);
  ldemul_choose_mode (emulation);
  default_target = ldemul_choose_target (argc, argv);
  lang_init ();
  ldemul_before_parse ();
  lang_has_input_file = FALSE;
  parse_args (argc, argv);

  if (config.hash_table_size != 0)
    bfd_hash_set_default_size (config.hash_table_size);

  ldemul_set_symbols ();

  if (link_info.relocatable)
    {
      if (link_info.gc_sections)
	einfo ("%P%F: --gc-sections and -r may not be used together\n");
      else if (command_line.relax)
	einfo (_("%P%F: --relax and -r may not be used together\n"));
      if (link_info.shared)
	einfo (_("%P%F: -r and -shared may not be used together\n"));
    }

  /* We may have -Bsymbolic, -Bsymbolic-functions, --dynamic-list-data,
     --dynamic-list-cpp-new, --dynamic-list-cpp-typeinfo and
     --dynamic-list FILE.  -Bsymbolic and -Bsymbolic-functions are
     for shared libraries.  -Bsymbolic overrides all others and vice
     versa.  */
  switch (command_line.symbolic)
    {
    case symbolic_unset:
      break;
    case symbolic:
      /* -Bsymbolic is for shared library only.  */
      if (link_info.shared)
	{
	  link_info.symbolic = TRUE;
	  /* Should we free the unused memory?  */
	  link_info.dynamic_list = NULL;
	  command_line.dynamic_list = dynamic_list_unset;
	}
      break;
    case symbolic_functions:
      /* -Bsymbolic-functions is for shared library only.  */
      if (link_info.shared)
	command_line.dynamic_list = dynamic_list_data;
      break;
    }

  switch (command_line.dynamic_list)
    {
    case dynamic_list_unset:
      break;
    case dynamic_list_data:
      link_info.dynamic_data = TRUE;
    case dynamic_list:
      link_info.dynamic = TRUE;
      break;
    }

  if (! link_info.shared)
    {
      if (command_line.filter_shlib)
	einfo (_("%P%F: -F may not be used without -shared\n"));
      if (command_line.auxiliary_filters)
	einfo (_("%P%F: -f may not be used without -shared\n"));
    }

  if (! link_info.shared || link_info.pie)
    link_info.executable = TRUE;

  /* Treat ld -r -s as ld -r -S -x (i.e., strip all local symbols).  I
     don't see how else this can be handled, since in this case we
     must preserve all externally visible symbols.  */
  if (link_info.relocatable && link_info.strip == strip_all)
    {
      link_info.strip = strip_debugger;
      if (link_info.discard == discard_sec_merge)
	link_info.discard = discard_all;
    }

  /* This essentially adds another -L directory so this must be done after
     the -L's in argv have been processed.  */
  set_scripts_dir ();

  /* If we have not already opened and parsed a linker script,
     try the default script from command line first.  */
  if (saved_script_handle == NULL
      && command_line.default_script != NULL)
    {
      ldfile_open_command_file (command_line.default_script);
      parser_input = input_script;
      yyparse ();
    }

  /* If we have not already opened and parsed a linker script
     read the emulation's appropriate default script.  */
  if (saved_script_handle == NULL)
    {
      int isfile;
      char *s = ldemul_get_script (&isfile);

      if (isfile)
	ldfile_open_command_file (s);
      else
	{
	  lex_string = s;
	  lex_redirect (s);
	}
      parser_input = input_script;
      yyparse ();
      lex_string = NULL;
    }

  if (trace_file_tries)
    {
      if (saved_script_handle)
	info_msg (_("using external linker script:"));
      else
	info_msg (_("using internal linker script:"));
      info_msg ("\n==================================================\n");

      if (saved_script_handle)
	{
	  static const int ld_bufsz = 8193;
	  size_t n;
	  char *buf = xmalloc (ld_bufsz);

	  rewind (saved_script_handle);
	  while ((n = fread (buf, 1, ld_bufsz - 1, saved_script_handle)) > 0)
	    {
	      buf[n] = 0;
	      info_msg (buf);
	    }
	  rewind (saved_script_handle);
	  free (buf);
	}
      else
	{
	  int isfile;

	  info_msg (ldemul_get_script (&isfile));
	}

      info_msg ("\n==================================================\n");
    }

  lang_final ();

  if (!lang_has_input_file)
    {
      if (version_printed)
	xexit (0);
      einfo (_("%P%F: no input files\n"));
    }

  if (trace_files)
    info_msg (_("%P: mode %s\n"), emulation);

  ldemul_after_parse ();

  if (config.map_filename)
    {
      if (strcmp (config.map_filename, "-") == 0)
	{
	  config.map_file = stdout;
	}
      else
	{
	  config.map_file = fopen (config.map_filename, FOPEN_WT);
	  if (config.map_file == (FILE *) NULL)
	    {
	      bfd_set_error (bfd_error_system_call);
	      einfo (_("%P%F: cannot open map file %s: %E\n"),
		     config.map_filename);
	    }
	}
    }

  lang_process ();

  /* Print error messages for any missing symbols, for any warning
     symbols, and possibly multiple definitions.  */
  if (link_info.relocatable)
    output_bfd->flags &= ~EXEC_P;
  else
    output_bfd->flags |= EXEC_P;

  ldwrite ();

  if (config.map_file != NULL)
    lang_map ();
  if (command_line.cref)
    output_cref (config.map_file != NULL ? config.map_file : stdout);
  if (nocrossref_list != NULL)
    check_nocrossrefs ();

  lang_finish ();

  /* Even if we're producing relocatable output, some non-fatal errors should
     be reported in the exit status.  (What non-fatal errors, if any, do we
     want to ignore for relocatable output?)  */
  if (!config.make_executable && !force_make_executable)
    {
      if (trace_files)
	einfo (_("%P: link errors found, deleting executable `%s'\n"),
	       output_filename);

      /* The file will be removed by remove_output.  */
      xexit (1);
    }
  else
    {
      if (! bfd_close (output_bfd))
	einfo (_("%F%B: final close failed: %E\n"), output_bfd);

      /* If the --force-exe-suffix is enabled, and we're making an
	 executable file and it doesn't end in .exe, copy it to one
	 which does.  */
      if (! link_info.relocatable && command_line.force_exe_suffix)
	{
	  int len = strlen (output_filename);

	  if (len < 4
	      || (strcasecmp (output_filename + len - 4, ".exe") != 0
		  && strcasecmp (output_filename + len - 4, ".dll") != 0))
	    {
	      FILE *src;
	      FILE *dst;
	      const int bsize = 4096;
	      char *buf = xmalloc (bsize);
	      int l;
	      char *dst_name = xmalloc (len + 5);

	      strcpy (dst_name, output_filename);
	      strcat (dst_name, ".exe");
	      src = fopen (output_filename, FOPEN_RB);
	      dst = fopen (dst_name, FOPEN_WB);

	      if (!src)
		einfo (_("%X%P: unable to open for source of copy `%s'\n"),
		       output_filename);
	      if (!dst)
		einfo (_("%X%P: unable to open for destination of copy `%s'\n"),
		       dst_name);
	      while ((l = fread (buf, 1, bsize, src)) > 0)
		{
		  int done = fwrite (buf, 1, l, dst);

		  if (done != l)
		    einfo (_("%P: Error writing file `%s'\n"), dst_name);
		}

	      fclose (src);
	      if (fclose (dst) == EOF)
		einfo (_("%P: Error closing file `%s'\n"), dst_name);
	      free (dst_name);
	      free (buf);
	    }
	}
    }

  END_PROGRESS (program_name);

  if (config.stats)
    {
#ifdef HAVE_SBRK
      char *lim = sbrk (0);
#endif
      long run_time = get_run_time () - start_time;

      fprintf (stderr, _("%s: total time in link: %ld.%06ld\n"),
	       program_name, run_time / 1000000, run_time % 1000000);
#ifdef HAVE_SBRK
      fprintf (stderr, _("%s: data size %ld\n"), program_name,
	       (long) (lim - (char *) &environ));
#endif
    }

  /* Prevent remove_output from doing anything, after a successful link.  */
  output_filename = NULL;

  xexit (0);
  return 0;
}

/* If the configured sysroot is relocatable, try relocating it based on
   default prefix FROM.  Return the relocated directory if it exists,
   otherwise return null.  */

static char *
get_relative_sysroot (const char *from ATTRIBUTE_UNUSED)
{
#ifdef TARGET_SYSTEM_ROOT_RELOCATABLE
  char *path;
  struct stat s;

  path = make_relative_prefix (program_name, from, TARGET_SYSTEM_ROOT);
  if (path)
    {
      if (stat (path, &s) == 0 && S_ISDIR (s.st_mode))
	return path;
      free (path);
    }
#endif
  return 0;
}

/* Return the sysroot directory.  Return "" if no sysroot is being used.  */

static const char *
get_sysroot (int argc, char **argv)
{
  int i;
  const char *path;

  for (i = 1; i < argc; i++)
    if (CONST_STRNEQ (argv[i], "--sysroot="))
      return argv[i] + strlen ("--sysroot=");

  path = get_relative_sysroot (BINDIR);
  if (path)
    return path;

  path = get_relative_sysroot (TOOLBINDIR);
  if (path)
    return path;

  return TARGET_SYSTEM_ROOT;
}

/* We need to find any explicitly given emulation in order to initialize the
   state that's needed by the lex&yacc argument parser (parse_args).  */

static char *
get_emulation (int argc, char **argv)
{
  char *emulation;
  int i;

  emulation = getenv (EMULATION_ENVIRON);
  if (emulation == NULL)
    emulation = DEFAULT_EMULATION;

  for (i = 1; i < argc; i++)
    {
      if (CONST_STRNEQ (argv[i], "-m"))
	{
	  if (argv[i][2] == '\0')
	    {
	      /* -m EMUL */
	      if (i < argc - 1)
		{
		  emulation = argv[i + 1];
		  i++;
		}
	      else
		einfo (_("%P%F: missing argument to -m\n"));
	    }
	  else if (strcmp (argv[i], "-mips1") == 0
		   || strcmp (argv[i], "-mips2") == 0
		   || strcmp (argv[i], "-mips3") == 0
		   || strcmp (argv[i], "-mips4") == 0
		   || strcmp (argv[i], "-mips5") == 0
		   || strcmp (argv[i], "-mips32") == 0
		   || strcmp (argv[i], "-mips32r2") == 0
		   || strcmp (argv[i], "-mips64") == 0
		   || strcmp (argv[i], "-mips64r2") == 0)
	    {
	      /* FIXME: The arguments -mips1, -mips2, -mips3, etc. are
		 passed to the linker by some MIPS compilers.  They
		 generally tell the linker to use a slightly different
		 library path.  Perhaps someday these should be
		 implemented as emulations; until then, we just ignore
		 the arguments and hope that nobody ever creates
		 emulations named ips1, ips2 or ips3.  */
	    }
	  else if (strcmp (argv[i], "-m486") == 0)
	    {
	      /* FIXME: The argument -m486 is passed to the linker on
		 some Linux systems.  Hope that nobody creates an
		 emulation named 486.  */
	    }
	  else
	    {
	      /* -mEMUL */
	      emulation = &argv[i][2];
	    }
	}
    }

  return emulation;
}

/* If directory DIR contains an "ldscripts" subdirectory,
   add DIR to the library search path and return TRUE,
   else return FALSE.  */

static bfd_boolean
check_for_scripts_dir (char *dir)
{
  size_t dirlen;
  char *buf;
  struct stat s;
  bfd_boolean res;

  dirlen = strlen (dir);
  /* sizeof counts the terminating NUL.  */
  buf = xmalloc (dirlen + sizeof ("/ldscripts"));
  sprintf (buf, "%s/ldscripts", dir);

  res = stat (buf, &s) == 0 && S_ISDIR (s.st_mode);
  free (buf);
  if (res)
    ldfile_add_library_path (dir, FALSE);
  return res;
}

/* Set the default directory for finding script files.
   Libraries will be searched for here too, but that's ok.
   We look for the "ldscripts" directory in:

   SCRIPTDIR (passed from Makefile)
	     (adjusted according to the current location of the binary)
   SCRIPTDIR (passed from Makefile)
   the dir where this program is (for using it from the build tree)
   the dir where this program is/../lib
	     (for installing the tool suite elsewhere).  */

static void
set_scripts_dir (void)
{
  char *end, *dir;
  size_t dirlen;
  bfd_boolean found;

  dir = make_relative_prefix (program_name, BINDIR, SCRIPTDIR);
  if (dir)
    {
      found = check_for_scripts_dir (dir);
      free (dir);
      if (found)
	return;
    }

  dir = make_relative_prefix (program_name, TOOLBINDIR, SCRIPTDIR);
  if (dir)
    {
      found = check_for_scripts_dir (dir);
      free (dir);
      if (found)
	return;
    }

  if (check_for_scripts_dir (SCRIPTDIR))
    /* We've been installed normally.  */
    return;

  /* Look for "ldscripts" in the dir where our binary is.  */
  end = strrchr (program_name, '/');
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
  {
    /* We could have \foo\bar, or /foo\bar.  */
    char *bslash = strrchr (program_name, '\\');

    if (end == NULL || (bslash != NULL && bslash > end))
      end = bslash;
  }
#endif

  if (end == NULL)
    /* Don't look for ldscripts in the current directory.  There is
       too much potential for confusion.  */
    return;

  dirlen = end - program_name;
  /* Make a copy of program_name in dir.
     Leave room for later "/../lib".  */
  dir = xmalloc (dirlen + 8);
  strncpy (dir, program_name, dirlen);
  dir[dirlen] = '\0';

  if (check_for_scripts_dir (dir))
    {
      free (dir);
      return;
    }

  /* Look for "ldscripts" in <the dir where our binary is>/../lib.  */
  strcpy (dir + dirlen, "/../lib");
  check_for_scripts_dir (dir);
  free (dir);
}

void
add_ysym (const char *name)
{
  if (link_info.notice_hash == NULL)
    {
      link_info.notice_hash = xmalloc (sizeof (struct bfd_hash_table));
      if (!bfd_hash_table_init_n (link_info.notice_hash,
				  bfd_hash_newfunc,
				  sizeof (struct bfd_hash_entry),
				  61))
	einfo (_("%P%F: bfd_hash_table_init failed: %E\n"));
    }

  if (bfd_hash_lookup (link_info.notice_hash, name, TRUE, TRUE) == NULL)
    einfo (_("%P%F: bfd_hash_lookup failed: %E\n"));
}

/* Record a symbol to be wrapped, from the --wrap option.  */

void
add_wrap (const char *name)
{
  if (link_info.wrap_hash == NULL)
    {
      link_info.wrap_hash = xmalloc (sizeof (struct bfd_hash_table));
      if (!bfd_hash_table_init_n (link_info.wrap_hash,
				  bfd_hash_newfunc,
				  sizeof (struct bfd_hash_entry),
				  61))
	einfo (_("%P%F: bfd_hash_table_init failed: %E\n"));
    }

  if (bfd_hash_lookup (link_info.wrap_hash, name, TRUE, TRUE) == NULL)
    einfo (_("%P%F: bfd_hash_lookup failed: %E\n"));
}

/* Handle the -retain-symbols-file option.  */

void
add_keepsyms_file (const char *filename)
{
  FILE *file;
  char *buf;
  size_t bufsize;
  int c;

  if (link_info.strip == strip_some)
    einfo (_("%X%P: error: duplicate retain-symbols-file\n"));

  file = fopen (filename, "r");
  if (file == NULL)
    {
      bfd_set_error (bfd_error_system_call);
      einfo ("%X%P: %s: %E\n", filename);
      return;
    }

  link_info.keep_hash = xmalloc (sizeof (struct bfd_hash_table));
  if (!bfd_hash_table_init (link_info.keep_hash, bfd_hash_newfunc,
			    sizeof (struct bfd_hash_entry)))
    einfo (_("%P%F: bfd_hash_table_init failed: %E\n"));

  bufsize = 100;
  buf = xmalloc (bufsize);

  c = getc (file);
  while (c != EOF)
    {
      while (ISSPACE (c))
	c = getc (file);

      if (c != EOF)
	{
	  size_t len = 0;

	  while (! ISSPACE (c) && c != EOF)
	    {
	      buf[len] = c;
	      ++len;
	      if (len >= bufsize)
		{
		  bufsize *= 2;
		  buf = xrealloc (buf, bufsize);
		}
	      c = getc (file);
	    }

	  buf[len] = '\0';

	  if (bfd_hash_lookup (link_info.keep_hash, buf, TRUE, TRUE) == NULL)
	    einfo (_("%P%F: bfd_hash_lookup for insertion failed: %E\n"));
	}
    }

  if (link_info.strip != strip_none)
    einfo (_("%P: `-retain-symbols-file' overrides `-s' and `-S'\n"));

  free (buf);
  link_info.strip = strip_some;
}

/* Callbacks from the BFD linker routines.  */

/* This is called when BFD has decided to include an archive member in
   a link.  */

static bfd_boolean
add_archive_element (struct bfd_link_info *info,
		     bfd *abfd,
		     const char *name)
{
  lang_input_statement_type *input;

  input = xmalloc (sizeof (lang_input_statement_type));
  input->filename = abfd->filename;
  input->local_sym_name = abfd->filename;
  input->the_bfd = abfd;
  input->asymbols = NULL;
  input->next = NULL;
  input->just_syms_flag = FALSE;
  input->loaded = FALSE;
  input->search_dirs_flag = FALSE;

  /* FIXME: The following fields are not set: header.next,
     header.type, closed, passive_position, symbol_count,
     next_real_file, is_archive, target, real.  This bit of code is
     from the old decode_library_subfile function.  I don't know
     whether any of those fields matters.  */

  ldlang_add_file (input);

  if (config.map_file != NULL)
    {
      static bfd_boolean header_printed;
      struct bfd_link_hash_entry *h;
      bfd *from;
      int len;

      h = bfd_link_hash_lookup (info->hash, name, FALSE, FALSE, TRUE);

      if (h == NULL)
	from = NULL;
      else
	{
	  switch (h->type)
	    {
	    default:
	      from = NULL;
	      break;

	    case bfd_link_hash_defined:
	    case bfd_link_hash_defweak:
	      from = h->u.def.section->owner;
	      break;

	    case bfd_link_hash_undefined:
	    case bfd_link_hash_undefweak:
	      from = h->u.undef.abfd;
	      break;

	    case bfd_link_hash_common:
	      from = h->u.c.p->section->owner;
	      break;
	    }
	}

      if (! header_printed)
	{
	  char buf[100];

	  sprintf (buf, _("Archive member included because of file (symbol)\n\n"));
	  minfo ("%s", buf);
	  header_printed = TRUE;
	}

      if (bfd_my_archive (abfd) == NULL)
	{
	  minfo ("%s", bfd_get_filename (abfd));
	  len = strlen (bfd_get_filename (abfd));
	}
      else
	{
	  minfo ("%s(%s)", bfd_get_filename (bfd_my_archive (abfd)),
		 bfd_get_filename (abfd));
	  len = (strlen (bfd_get_filename (bfd_my_archive (abfd)))
		 + strlen (bfd_get_filename (abfd))
		 + 2);
	}

      if (len >= 29)
	{
	  print_nl ();
	  len = 0;
	}
      while (len < 30)
	{
	  print_space ();
	  ++len;
	}

      if (from != NULL)
	minfo ("%B ", from);
      if (h != NULL)
	minfo ("(%T)\n", h->root.string);
      else
	minfo ("(%s)\n", name);
    }

  if (trace_files || trace_file_tries)
    info_msg ("%I\n", input);

  return TRUE;
}

/* This is called when BFD has discovered a symbol which is defined
   multiple times.  */

static bfd_boolean
multiple_definition (struct bfd_link_info *info ATTRIBUTE_UNUSED,
		     const char *name,
		     bfd *obfd,
		     asection *osec,
		     bfd_vma oval,
		     bfd *nbfd,
		     asection *nsec,
		     bfd_vma nval)
{
  /* If either section has the output_section field set to
     bfd_abs_section_ptr, it means that the section is being
     discarded, and this is not really a multiple definition at all.
     FIXME: It would be cleaner to somehow ignore symbols defined in
     sections which are being discarded.  */
  if ((osec->output_section != NULL
       && ! bfd_is_abs_section (osec)
       && bfd_is_abs_section (osec->output_section))
      || (nsec->output_section != NULL
	  && ! bfd_is_abs_section (nsec)
	  && bfd_is_abs_section (nsec->output_section)))
    return TRUE;

  einfo (_("%X%C: multiple definition of `%T'\n"),
	 nbfd, nsec, nval, name);
  if (obfd != NULL)
    einfo (_("%D: first defined here\n"), obfd, osec, oval);

  if (command_line.relax)
    {
      einfo (_("%P: Disabling relaxation: it will not work with multiple definitions\n"));
      command_line.relax = 0;
    }

  return TRUE;
}

/* This is called when there is a definition of a common symbol, or
   when a common symbol is found for a symbol that is already defined,
   or when two common symbols are found.  We only do something if
   -warn-common was used.  */

static bfd_boolean
multiple_common (struct bfd_link_info *info ATTRIBUTE_UNUSED,
		 const char *name,
		 bfd *obfd,
		 enum bfd_link_hash_type otype,
		 bfd_vma osize,
		 bfd *nbfd,
		 enum bfd_link_hash_type ntype,
		 bfd_vma nsize)
{
  if (! config.warn_common)
    return TRUE;

  if (ntype == bfd_link_hash_defined
      || ntype == bfd_link_hash_defweak
      || ntype == bfd_link_hash_indirect)
    {
      ASSERT (otype == bfd_link_hash_common);
      einfo (_("%B: warning: definition of `%T' overriding common\n"),
	     nbfd, name);
      if (obfd != NULL)
	einfo (_("%B: warning: common is here\n"), obfd);
    }
  else if (otype == bfd_link_hash_defined
	   || otype == bfd_link_hash_defweak
	   || otype == bfd_link_hash_indirect)
    {
      ASSERT (ntype == bfd_link_hash_common);
      einfo (_("%B: warning: common of `%T' overridden by definition\n"),
	     nbfd, name);
      if (obfd != NULL)
	einfo (_("%B: warning: defined here\n"), obfd);
    }
  else
    {
      ASSERT (otype == bfd_link_hash_common && ntype == bfd_link_hash_common);
      if (osize > nsize)
	{
	  einfo (_("%B: warning: common of `%T' overridden by larger common\n"),
		 nbfd, name);
	  if (obfd != NULL)
	    einfo (_("%B: warning: larger common is here\n"), obfd);
	}
      else if (nsize > osize)
	{
	  einfo (_("%B: warning: common of `%T' overriding smaller common\n"),
		 nbfd, name);
	  if (obfd != NULL)
	    einfo (_("%B: warning: smaller common is here\n"), obfd);
	}
      else
	{
	  einfo (_("%B: warning: multiple common of `%T'\n"), nbfd, name);
	  if (obfd != NULL)
	    einfo (_("%B: warning: previous common is here\n"), obfd);
	}
    }

  return TRUE;
}

/* This is called when BFD has discovered a set element.  H is the
   entry in the linker hash table for the set.  SECTION and VALUE
   represent a value which should be added to the set.  */

static bfd_boolean
add_to_set (struct bfd_link_info *info ATTRIBUTE_UNUSED,
	    struct bfd_link_hash_entry *h,
	    bfd_reloc_code_real_type reloc,
	    bfd *abfd,
	    asection *section,
	    bfd_vma value)
{
  if (config.warn_constructors)
    einfo (_("%P: warning: global constructor %s used\n"),
	   h->root.string);

  if (! config.build_constructors)
    return TRUE;

  ldctor_add_set_entry (h, reloc, NULL, section, value);

  if (h->type == bfd_link_hash_new)
    {
      h->type = bfd_link_hash_undefined;
      h->u.undef.abfd = abfd;
      /* We don't call bfd_link_add_undef to add this to the list of
	 undefined symbols because we are going to define it
	 ourselves.  */
    }

  return TRUE;
}

/* This is called when BFD has discovered a constructor.  This is only
   called for some object file formats--those which do not handle
   constructors in some more clever fashion.  This is similar to
   adding an element to a set, but less general.  */

static bfd_boolean
constructor_callback (struct bfd_link_info *info,
		      bfd_boolean constructor,
		      const char *name,
		      bfd *abfd,
		      asection *section,
		      bfd_vma value)
{
  char *s;
  struct bfd_link_hash_entry *h;
  char set_name[1 + sizeof "__CTOR_LIST__"];

  if (config.warn_constructors)
    einfo (_("%P: warning: global constructor %s used\n"), name);

  if (! config.build_constructors)
    return TRUE;

  /* Ensure that BFD_RELOC_CTOR exists now, so that we can give a
     useful error message.  */
  if (bfd_reloc_type_lookup (output_bfd, BFD_RELOC_CTOR) == NULL
      && (info->relocatable
	  || bfd_reloc_type_lookup (abfd, BFD_RELOC_CTOR) == NULL))
    einfo (_("%P%F: BFD backend error: BFD_RELOC_CTOR unsupported\n"));

  s = set_name;
  if (bfd_get_symbol_leading_char (abfd) != '\0')
    *s++ = bfd_get_symbol_leading_char (abfd);
  if (constructor)
    strcpy (s, "__CTOR_LIST__");
  else
    strcpy (s, "__DTOR_LIST__");

  h = bfd_link_hash_lookup (info->hash, set_name, TRUE, TRUE, TRUE);
  if (h == (struct bfd_link_hash_entry *) NULL)
    einfo (_("%P%F: bfd_link_hash_lookup failed: %E\n"));
  if (h->type == bfd_link_hash_new)
    {
      h->type = bfd_link_hash_undefined;
      h->u.undef.abfd = abfd;
      /* We don't call bfd_link_add_undef to add this to the list of
	 undefined symbols because we are going to define it
	 ourselves.  */
    }

  ldctor_add_set_entry (h, BFD_RELOC_CTOR, name, section, value);
  return TRUE;
}

/* A structure used by warning_callback to pass information through
   bfd_map_over_sections.  */

struct warning_callback_info
{
  bfd_boolean found;
  const char *warning;
  const char *symbol;
  asymbol **asymbols;
};

/* This is called when there is a reference to a warning symbol.  */

static bfd_boolean
warning_callback (struct bfd_link_info *info ATTRIBUTE_UNUSED,
		  const char *warning,
		  const char *symbol,
		  bfd *abfd,
		  asection *section,
		  bfd_vma address)
{
  /* This is a hack to support warn_multiple_gp.  FIXME: This should
     have a cleaner interface, but what?  */
  if (! config.warn_multiple_gp
      && strcmp (warning, "using multiple gp values") == 0)
    return TRUE;

  if (section != NULL)
    einfo ("%C: %s%s\n", abfd, section, address, _("warning: "), warning);
  else if (abfd == NULL)
    einfo ("%P: %s%s\n", _("warning: "), warning);
  else if (symbol == NULL)
    einfo ("%B: %s%s\n", abfd, _("warning: "), warning);
  else
    {
      lang_input_statement_type *entry;
      asymbol **asymbols;
      struct warning_callback_info info;

      /* Look through the relocs to see if we can find a plausible
	 address.  */
      entry = (lang_input_statement_type *) abfd->usrdata;
      if (entry != NULL && entry->asymbols != NULL)
	asymbols = entry->asymbols;
      else
	{
	  long symsize;
	  long symbol_count;

	  symsize = bfd_get_symtab_upper_bound (abfd);
	  if (symsize < 0)
	    einfo (_("%B%F: could not read symbols: %E\n"), abfd);
	  asymbols = xmalloc (symsize);
	  symbol_count = bfd_canonicalize_symtab (abfd, asymbols);
	  if (symbol_count < 0)
	    einfo (_("%B%F: could not read symbols: %E\n"), abfd);
	  if (entry != NULL)
	    {
	      entry->asymbols = asymbols;
	      entry->symbol_count = symbol_count;
	    }
	}

      info.found = FALSE;
      info.warning = warning;
      info.symbol = symbol;
      info.asymbols = asymbols;
      bfd_map_over_sections (abfd, warning_find_reloc, &info);

      if (! info.found)
	einfo ("%B: %s%s\n", abfd, _("warning: "), warning);

      if (entry == NULL)
	free (asymbols);
    }

  return TRUE;
}

/* This is called by warning_callback for each section.  It checks the
   relocs of the section to see if it can find a reference to the
   symbol which triggered the warning.  If it can, it uses the reloc
   to give an error message with a file and line number.  */

static void
warning_find_reloc (bfd *abfd, asection *sec, void *iarg)
{
  struct warning_callback_info *info = iarg;
  long relsize;
  arelent **relpp;
  long relcount;
  arelent **p, **pend;

  if (info->found)
    return;

  relsize = bfd_get_reloc_upper_bound (abfd, sec);
  if (relsize < 0)
    einfo (_("%B%F: could not read relocs: %E\n"), abfd);
  if (relsize == 0)
    return;

  relpp = xmalloc (relsize);
  relcount = bfd_canonicalize_reloc (abfd, sec, relpp, info->asymbols);
  if (relcount < 0)
    einfo (_("%B%F: could not read relocs: %E\n"), abfd);

  p = relpp;
  pend = p + relcount;
  for (; p < pend && *p != NULL; p++)
    {
      arelent *q = *p;

      if (q->sym_ptr_ptr != NULL
	  && *q->sym_ptr_ptr != NULL
	  && strcmp (bfd_asymbol_name (*q->sym_ptr_ptr), info->symbol) == 0)
	{
	  /* We found a reloc for the symbol we are looking for.  */
	  einfo ("%C: %s%s\n", abfd, sec, q->address, _("warning: "),
		 info->warning);
	  info->found = TRUE;
	  break;
	}
    }

  free (relpp);
}

/* This is called when an undefined symbol is found.  */

static bfd_boolean
undefined_symbol (struct bfd_link_info *info ATTRIBUTE_UNUSED,
		  const char *name,
		  bfd *abfd,
		  asection *section,
		  bfd_vma address,
		  bfd_boolean error)
{
  static char *error_name;
  static unsigned int error_count;

#define MAX_ERRORS_IN_A_ROW 5

  if (config.warn_once)
    {
      static struct bfd_hash_table *hash;

      /* Only warn once about a particular undefined symbol.  */
      if (hash == NULL)
	{
	  hash = xmalloc (sizeof (struct bfd_hash_table));
	  if (!bfd_hash_table_init (hash, bfd_hash_newfunc,
				    sizeof (struct bfd_hash_entry)))
	    einfo (_("%F%P: bfd_hash_table_init failed: %E\n"));
	}

      if (bfd_hash_lookup (hash, name, FALSE, FALSE) != NULL)
	return TRUE;

      if (bfd_hash_lookup (hash, name, TRUE, TRUE) == NULL)
	einfo (_("%F%P: bfd_hash_lookup failed: %E\n"));
    }

  /* We never print more than a reasonable number of errors in a row
     for a single symbol.  */
  if (error_name != NULL
      && strcmp (name, error_name) == 0)
    ++error_count;
  else
    {
      error_count = 0;
      if (error_name != NULL)
	free (error_name);
      error_name = xstrdup (name);
    }

  if (section != NULL)
    {
      if (error_count < MAX_ERRORS_IN_A_ROW)
	{
	  if (error)
	    einfo (_("%X%C: undefined reference to `%T'\n"),
		   abfd, section, address, name);
	  else
	    einfo (_("%C: warning: undefined reference to `%T'\n"),
		   abfd, section, address, name);
	}
      else if (error_count == MAX_ERRORS_IN_A_ROW)
	{
	  if (error)
	    einfo (_("%X%D: more undefined references to `%T' follow\n"),
		   abfd, section, address, name);
	  else
	    einfo (_("%D: warning: more undefined references to `%T' follow\n"),
		   abfd, section, address, name);
	}
      else if (error)
	einfo ("%X");
    }
  else
    {
      if (error_count < MAX_ERRORS_IN_A_ROW)
	{
	  if (error)
	    einfo (_("%X%B: undefined reference to `%T'\n"),
		   abfd, name);
	  else
	    einfo (_("%B: warning: undefined reference to `%T'\n"),
		   abfd, name);
	}
      else if (error_count == MAX_ERRORS_IN_A_ROW)
	{
	  if (error)
	    einfo (_("%X%B: more undefined references to `%T' follow\n"),
		   abfd, name);
	  else
	    einfo (_("%B: warning: more undefined references to `%T' follow\n"),
		   abfd, name);
	}
      else if (error)
	einfo ("%X");
    }

  return TRUE;
}

/* Counter to limit the number of relocation overflow error messages
   to print.  Errors are printed as it is decremented.  When it's
   called and the counter is zero, a final message is printed
   indicating more relocations were omitted.  When it gets to -1, no
   such errors are printed.  If it's initially set to a value less
   than -1, all such errors will be printed (--verbose does this).  */

int overflow_cutoff_limit = 10;

/* This is called when a reloc overflows.  */

static bfd_boolean
reloc_overflow (struct bfd_link_info *info ATTRIBUTE_UNUSED,
		struct bfd_link_hash_entry *entry,
		const char *name,
		const char *reloc_name,
		bfd_vma addend,
		bfd *abfd,
		asection *section,
		bfd_vma address)
{
  if (overflow_cutoff_limit == -1)
    return TRUE;

  einfo ("%X%C:", abfd, section, address);

  if (overflow_cutoff_limit >= 0
      && overflow_cutoff_limit-- == 0)
    {
      einfo (_(" additional relocation overflows omitted from the output\n"));
      return TRUE;
    }

  if (entry)
    {
      while (entry->type == bfd_link_hash_indirect
	     || entry->type == bfd_link_hash_warning)
	entry = entry->u.i.link;
      switch (entry->type)
	{
	case bfd_link_hash_undefined:
	case bfd_link_hash_undefweak:
	  einfo (_(" relocation truncated to fit: %s against undefined symbol `%T'"),
		 reloc_name, entry->root.string);
	  break;
	case bfd_link_hash_defined:
	case bfd_link_hash_defweak:
	  einfo (_(" relocation truncated to fit: %s against symbol `%T' defined in %A section in %B"),
		 reloc_name, entry->root.string,
		 entry->u.def.section,
		 entry->u.def.section == bfd_abs_section_ptr
		 ? output_bfd : entry->u.def.section->owner);
	  break;
	default:
	  abort ();
	  break;
	}
    }
  else
    einfo (_(" relocation truncated to fit: %s against `%T'"),
	   reloc_name, name);
  if (addend != 0)
    einfo ("+%v", addend);
  einfo ("\n");
  return TRUE;
}

/* This is called when a dangerous relocation is made.  */

static bfd_boolean
reloc_dangerous (struct bfd_link_info *info ATTRIBUTE_UNUSED,
		 const char *message,
		 bfd *abfd,
		 asection *section,
		 bfd_vma address)
{
  einfo (_("%X%C: dangerous relocation: %s\n"),
	 abfd, section, address, message);
  return TRUE;
}

/* This is called when a reloc is being generated attached to a symbol
   that is not being output.  */

static bfd_boolean
unattached_reloc (struct bfd_link_info *info ATTRIBUTE_UNUSED,
		  const char *name,
		  bfd *abfd,
		  asection *section,
		  bfd_vma address)
{
  einfo (_("%X%C: reloc refers to symbol `%T' which is not being output\n"),
	 abfd, section, address, name);
  return TRUE;
}

/* This is called if link_info.notice_all is set, or when a symbol in
   link_info.notice_hash is found.  Symbols are put in notice_hash
   using the -y option.  */

static bfd_boolean
notice (struct bfd_link_info *info,
	const char *name,
	bfd *abfd,
	asection *section,
	bfd_vma value)
{
  if (name == NULL)
    {
      if (command_line.cref || nocrossref_list != NULL)
	return handle_asneeded_cref (abfd, value);
      return TRUE;
    }

  if (! info->notice_all
      || (info->notice_hash != NULL
	  && bfd_hash_lookup (info->notice_hash, name, FALSE, FALSE) != NULL))
    {
      if (bfd_is_und_section (section))
	einfo ("%B: reference to %s\n", abfd, name);
      else
	einfo ("%B: definition of %s\n", abfd, name);
    }

  if (command_line.cref || nocrossref_list != NULL)
    add_cref (name, abfd, section, value);

  return TRUE;
}

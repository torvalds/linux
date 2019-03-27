/* GDB CLI commands.

   Copyright 2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "readline/readline.h"
#include "readline/tilde.h"
#include "completer.h"
#include "target.h"	 /* For baud_rate, remote_debug and remote_timeout */
#include "gdb_wait.h"		/* For shell escape implementation */
#include "gdb_regex.h"		/* Used by apropos_command */
#include "gdb_string.h"
#include "gdb_vfork.h"
#include "linespec.h"
#include "expression.h"
#include "frame.h"
#include "value.h"
#include "language.h"
#include "filenames.h"		/* for DOSish file names */
#include "objfiles.h"
#include "source.h"
#include "disasm.h"

#include "ui-out.h"

#include "top.h"
#include "cli/cli-decode.h"
#include "cli/cli-script.h"
#include "cli/cli-setshow.h"
#include "cli/cli-cmds.h"

#ifdef TUI
#include "tui/tui.h"		/* For tui_active et.al.   */
#endif

#ifndef GDBINIT_FILENAME
#define GDBINIT_FILENAME        ".gdbinit"
#endif

/* Prototypes for local command functions */

static void complete_command (char *, int);

static void echo_command (char *, int);

static void pwd_command (char *, int);

static void show_version (char *, int);

static void help_command (char *, int);

static void show_command (char *, int);

static void info_command (char *, int);

static void show_debug (char *, int);

static void set_debug (char *, int);

static void show_user (char *, int);

static void make_command (char *, int);

static void shell_escape (char *, int);

static void edit_command (char *, int);

static void list_command (char *, int);

void apropos_command (char *, int);

/* Prototypes for local utility functions */

static void ambiguous_line_spec (struct symtabs_and_lines *);

/* Limit the call depth of user-defined commands */
int max_user_call_depth;

/* Define all cmd_list_elements.  */

/* Chain containing all defined commands.  */

struct cmd_list_element *cmdlist;

/* Chain containing all defined info subcommands.  */

struct cmd_list_element *infolist;

/* Chain containing all defined enable subcommands. */

struct cmd_list_element *enablelist;

/* Chain containing all defined disable subcommands. */

struct cmd_list_element *disablelist;

/* Chain containing all defined toggle subcommands. */

struct cmd_list_element *togglelist;

/* Chain containing all defined stop subcommands. */

struct cmd_list_element *stoplist;

/* Chain containing all defined delete subcommands. */

struct cmd_list_element *deletelist;

/* Chain containing all defined "enable breakpoint" subcommands. */

struct cmd_list_element *enablebreaklist;

/* Chain containing all defined set subcommands */

struct cmd_list_element *setlist;

/* Chain containing all defined unset subcommands */

struct cmd_list_element *unsetlist;

/* Chain containing all defined show subcommands.  */

struct cmd_list_element *showlist;

/* Chain containing all defined \"set history\".  */

struct cmd_list_element *sethistlist;

/* Chain containing all defined \"show history\".  */

struct cmd_list_element *showhistlist;

/* Chain containing all defined \"unset history\".  */

struct cmd_list_element *unsethistlist;

/* Chain containing all defined maintenance subcommands. */

struct cmd_list_element *maintenancelist;

/* Chain containing all defined "maintenance info" subcommands. */

struct cmd_list_element *maintenanceinfolist;

/* Chain containing all defined "maintenance print" subcommands. */

struct cmd_list_element *maintenanceprintlist;

struct cmd_list_element *setprintlist;

struct cmd_list_element *showprintlist;

struct cmd_list_element *setdebuglist;

struct cmd_list_element *showdebuglist;

struct cmd_list_element *setchecklist;

struct cmd_list_element *showchecklist;

/* Utility used everywhere when at least one argument is needed and
   none is supplied. */

void
error_no_arg (char *why)
{
  error ("Argument required (%s).", why);
}

/* The "info" command is defined as a prefix, with allow_unknown = 0.
   Therefore, its own definition is called only for "info" with no args.  */

static void
info_command (char *arg, int from_tty)
{
  printf_unfiltered ("\"info\" must be followed by the name of an info command.\n");
  help_list (infolist, "info ", -1, gdb_stdout);
}

/* The "show" command with no arguments shows all the settings.  */

static void
show_command (char *arg, int from_tty)
{
  cmd_show_list (showlist, from_tty, "");
}

/* Provide documentation on command or list given by COMMAND.  FROM_TTY
   is ignored.  */

static void
help_command (char *command, int from_tty)
{
  help_cmd (command, gdb_stdout);
}

/* String compare function for qsort.  */
static int
compare_strings (const void *arg1, const void *arg2)
{
  const char **s1 = (const char **) arg1;
  const char **s2 = (const char **) arg2;
  return strcmp (*s1, *s2);
}

/* The "complete" command is used by Emacs to implement completion.  */

static void
complete_command (char *arg, int from_tty)
{
  int i;
  int argpoint;
  char **completions, *point, *arg_prefix;

  dont_repeat ();

  if (arg == NULL)
    arg = "";
  argpoint = strlen (arg);

  /* complete_line assumes that its first argument is somewhere within,
     and except for filenames at the beginning of, the word to be completed.
     The following crude imitation of readline's word-breaking tries to
     accomodate this.  */
  point = arg + argpoint;
  while (point > arg)
    {
      if (strchr (rl_completer_word_break_characters, point[-1]) != 0)
        break;
      point--;
    }

  arg_prefix = alloca (point - arg + 1);
  memcpy (arg_prefix, arg, point - arg);
  arg_prefix[point - arg] = 0;

  completions = complete_line (point, arg, argpoint);

  if (completions)
    {
      int item, size;

      for (size = 0; completions[size]; ++size)
	;
      qsort (completions, size, sizeof (char *), compare_strings);

      /* We do extra processing here since we only want to print each
	 unique item once.  */
      item = 0;
      while (item < size)
	{
	  int next_item;
	  printf_unfiltered ("%s%s\n", arg_prefix, completions[item]);
	  next_item = item + 1;
	  while (next_item < size
		 && ! strcmp (completions[item], completions[next_item]))
	    {
	      xfree (completions[next_item]);
	      ++next_item;
	    }

	  xfree (completions[item]);
	  item = next_item;
	}

      xfree (completions);
    }
}

int
is_complete_command (struct cmd_list_element *c)
{
  return cmd_cfunc_eq (c, complete_command);
}

static void
show_version (char *args, int from_tty)
{
  immediate_quit++;
  print_gdb_version (gdb_stdout);
  printf_filtered ("\n");
  immediate_quit--;
}

/* Handle the quit command.  */

void
quit_command (char *args, int from_tty)
{
  if (!quit_confirm ())
    error ("Not confirmed.");
  quit_force (args, from_tty);
}

static void
pwd_command (char *args, int from_tty)
{
  if (args)
    error ("The \"pwd\" command does not take an argument: %s", args);
  getcwd (gdb_dirbuf, sizeof (gdb_dirbuf));

  if (strcmp (gdb_dirbuf, current_directory) != 0)
    printf_unfiltered ("Working directory %s\n (canonically %s).\n",
		       current_directory, gdb_dirbuf);
  else
    printf_unfiltered ("Working directory %s.\n", current_directory);
}

void
cd_command (char *dir, int from_tty)
{
  int len;
  /* Found something other than leading repetitions of "/..".  */
  int found_real_path;
  char *p;

  /* If the new directory is absolute, repeat is a no-op; if relative,
     repeat might be useful but is more likely to be a mistake.  */
  dont_repeat ();

  if (dir == 0)
    error_no_arg ("new working directory");

  dir = tilde_expand (dir);
  make_cleanup (xfree, dir);

  if (chdir (dir) < 0)
    perror_with_name (dir);

#ifdef HAVE_DOS_BASED_FILE_SYSTEM
  /* There's too much mess with DOSish names like "d:", "d:.",
     "d:./foo" etc.  Instead of having lots of special #ifdef'ed code,
     simply get the canonicalized name of the current directory.  */
  dir = getcwd (gdb_dirbuf, sizeof (gdb_dirbuf));
#endif

  len = strlen (dir);
  if (IS_DIR_SEPARATOR (dir[len - 1]))
    {
      /* Remove the trailing slash unless this is a root directory
         (including a drive letter on non-Unix systems).  */
      if (!(len == 1)		/* "/" */
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
	  && !(len == 3 && dir[1] == ':') /* "d:/" */
#endif
	  )
	len--;
    }

  dir = savestring (dir, len);
  if (IS_ABSOLUTE_PATH (dir))
    current_directory = dir;
  else
    {
      if (IS_DIR_SEPARATOR (current_directory[strlen (current_directory) - 1]))
	current_directory = concat (current_directory, dir, NULL);
      else
	current_directory = concat (current_directory, SLASH_STRING, dir, NULL);
      xfree (dir);
    }

  /* Now simplify any occurrences of `.' and `..' in the pathname.  */

  found_real_path = 0;
  for (p = current_directory; *p;)
    {
      if (IS_DIR_SEPARATOR (p[0]) && p[1] == '.'
	  && (p[2] == 0 || IS_DIR_SEPARATOR (p[2])))
	strcpy (p, p + 2);
      else if (IS_DIR_SEPARATOR (p[0]) && p[1] == '.' && p[2] == '.'
	       && (p[3] == 0 || IS_DIR_SEPARATOR (p[3])))
	{
	  if (found_real_path)
	    {
	      /* Search backwards for the directory just before the "/.."
	         and obliterate it and the "/..".  */
	      char *q = p;
	      while (q != current_directory && !IS_DIR_SEPARATOR (q[-1]))
		--q;

	      if (q == current_directory)
		/* current_directory is
		   a relative pathname ("can't happen"--leave it alone).  */
		++p;
	      else
		{
		  strcpy (q - 1, p + 3);
		  p = q - 1;
		}
	    }
	  else
	    /* We are dealing with leading repetitions of "/..", for example
	       "/../..", which is the Mach super-root.  */
	    p += 3;
	}
      else
	{
	  found_real_path = 1;
	  ++p;
	}
    }

  forget_cached_source_info ();

  if (from_tty)
    pwd_command ((char *) 0, 1);
}

void
source_command (char *args, int from_tty)
{
  FILE *stream;
  struct cleanup *old_cleanups;
  char *file = args;

  if (file == NULL)
    {
      error ("source command requires pathname of file to source.");
    }

  file = tilde_expand (file);
  old_cleanups = make_cleanup (xfree, file);

  stream = fopen (file, FOPEN_RT);
  if (!stream)
    {
      if (from_tty)
	perror_with_name (file);
      else
	return;
    }

  script_from_file (stream, file);

  do_cleanups (old_cleanups);
}

static void
echo_command (char *text, int from_tty)
{
  char *p = text;
  int c;

  if (text)
    while ((c = *p++) != '\0')
      {
	if (c == '\\')
	  {
	    /* \ at end of argument is used after spaces
	       so they won't be lost.  */
	    if (*p == 0)
	      return;

	    c = parse_escape (&p);
	    if (c >= 0)
	      printf_filtered ("%c", c);
	  }
	else
	  printf_filtered ("%c", c);
      }

  /* Force this output to appear now.  */
  wrap_here ("");
  gdb_flush (gdb_stdout);
}

static void
shell_escape (char *arg, int from_tty)
{
#ifdef CANT_FORK
  /* If ARG is NULL, they want an inferior shell, but `system' just
     reports if the shell is available when passed a NULL arg.  */
  int rc = system (arg ? arg : "");

  if (!arg)
    arg = "inferior shell";

  if (rc == -1)
    {
      fprintf_unfiltered (gdb_stderr, "Cannot execute %s: %s\n", arg,
			  safe_strerror (errno));
      gdb_flush (gdb_stderr);
    }
  else if (rc)
    {
      fprintf_unfiltered (gdb_stderr, "%s exited with status %d\n", arg, rc);
      gdb_flush (gdb_stderr);
    }
#ifdef GLOBAL_CURDIR
  /* Make sure to return to the directory GDB thinks it is, in case the
     shell command we just ran changed it.  */
  chdir (current_directory);
#endif
#else /* Can fork.  */
  int rc, status, pid;

  if ((pid = vfork ()) == 0)
    {
      char *p, *user_shell;

      if ((user_shell = (char *) getenv ("SHELL")) == NULL)
	user_shell = "/bin/sh";

      /* Get the name of the shell for arg0 */
      if ((p = strrchr (user_shell, '/')) == NULL)
	p = user_shell;
      else
	p++;			/* Get past '/' */

      if (!arg)
	execl (user_shell, p, (char *) 0);
      else
	execl (user_shell, p, "-c", arg, (char *) 0);

      fprintf_unfiltered (gdb_stderr, "Cannot execute %s: %s\n", user_shell,
			  safe_strerror (errno));
      gdb_flush (gdb_stderr);
      _exit (0177);
    }

  if (pid != -1)
    while ((rc = wait (&status)) != pid && rc != -1)
      ;
  else
    error ("Fork failed");
#endif /* Can fork.  */
}

static void
edit_command (char *arg, int from_tty)
{
  struct symtabs_and_lines sals;
  struct symtab_and_line sal;
  struct symbol *sym;
  char *arg1;
  int cmdlen, log10;
  unsigned m;
  char *editor;
  char *p;

  /* Pull in the current default source line if necessary */
  if (arg == 0)
    {
      set_default_source_symtab_and_line ();
      sal = get_current_source_symtab_and_line ();
    }

  /* bare "edit" edits file with present line.  */

  if (arg == 0)
    {
      if (sal.symtab == 0)
	error ("No default source file yet.");
      sal.line += get_lines_to_list () / 2;
    }
  else
    {

      /* Now should only be one argument -- decode it in SAL */

      arg1 = arg;
      sals = decode_line_1 (&arg1, 0, 0, 0, 0, 0);

      if (! sals.nelts) return;  /*  C++  */
      if (sals.nelts > 1) {
        ambiguous_line_spec (&sals);
        xfree (sals.sals);
        return;
      }

      sal = sals.sals[0];
      xfree (sals.sals);

      if (*arg1)
        error ("Junk at end of line specification.");

      /* if line was specified by address,
         first print exactly which line, and which file.
         In this case, sal.symtab == 0 means address is outside
         of all known source files, not that user failed to give a filename.  */
      if (*arg == '*')
        {
          if (sal.symtab == 0)
	    /* FIXME-32x64--assumes sal.pc fits in long.  */
	    error ("No source file for address %s.",
		   local_hex_string((unsigned long) sal.pc));
          sym = find_pc_function (sal.pc);
          if (sym)
	    {
	      print_address_numeric (sal.pc, 1, gdb_stdout);
	      printf_filtered (" is in ");
	      fputs_filtered (SYMBOL_PRINT_NAME (sym), gdb_stdout);
	      printf_filtered (" (%s:%d).\n", sal.symtab->filename, sal.line);
	    }
          else
	    {
	      print_address_numeric (sal.pc, 1, gdb_stdout);
	      printf_filtered (" is at %s:%d.\n",
			       sal.symtab->filename, sal.line);
	    }
        }

      /* If what was given does not imply a symtab, it must be an undebuggable
         symbol which means no source code.  */

      if (sal.symtab == 0)
        error ("No line number known for %s.", arg);
    }

  if ((editor = (char *) getenv ("EDITOR")) == NULL)
      editor = "/bin/ex";
  
  /* Approximate base-10 log of line to 1 unit for digit count */
  for(log10=32, m=0x80000000; !(sal.line & m) && log10>0; log10--, m=m>>1);
  log10 = 1 + (int)((log10 + (0 == ((m-1) & sal.line)))/3.32192809);

  cmdlen = strlen(editor) + 1
         + (NULL == sal.symtab->dirname ? 0 : strlen(sal.symtab->dirname) + 1)
	 + (NULL == sal.symtab->filename? 0 : strlen(sal.symtab->filename)+ 1)
	 + log10 + 2;
  
  p = xmalloc(cmdlen);
  sprintf(p,"%s +%d %s%s",editor,sal.line,
     (NULL == sal.symtab->dirname ? "./" :
        (NULL != sal.symtab->filename && *(sal.symtab->filename) != '/') ?
	   sal.symtab->dirname : ""),
     (NULL == sal.symtab->filename ? "unknown" : sal.symtab->filename)
  );
  shell_escape(p, from_tty);

  xfree(p);
}

static void
list_command (char *arg, int from_tty)
{
  struct symtabs_and_lines sals, sals_end;
  struct symtab_and_line sal, sal_end, cursal;
  struct symbol *sym;
  char *arg1;
  int no_end = 1;
  int dummy_end = 0;
  int dummy_beg = 0;
  int linenum_beg = 0;
  char *p;

  /* Pull in the current default source line if necessary */
  if (arg == 0 || arg[0] == '+' || arg[0] == '-')
    {
      set_default_source_symtab_and_line ();
      cursal = get_current_source_symtab_and_line ();
    }

  /* "l" or "l +" lists next ten lines.  */

  if (arg == 0 || strcmp (arg, "+") == 0)
    {
      print_source_lines (cursal.symtab, cursal.line,
			  cursal.line + get_lines_to_list (), 0);
      return;
    }

  /* "l -" lists previous ten lines, the ones before the ten just listed.  */
  if (strcmp (arg, "-") == 0)
    {
      print_source_lines (cursal.symtab,
			  max (get_first_line_listed () - get_lines_to_list (), 1),
			  get_first_line_listed (), 0);
      return;
    }

  /* Now if there is only one argument, decode it in SAL
     and set NO_END.
     If there are two arguments, decode them in SAL and SAL_END
     and clear NO_END; however, if one of the arguments is blank,
     set DUMMY_BEG or DUMMY_END to record that fact.  */

  if (!have_full_symbols () && !have_partial_symbols ())
    error ("No symbol table is loaded.  Use the \"file\" command.");

  arg1 = arg;
  if (*arg1 == ',')
    dummy_beg = 1;
  else
    {
      sals = decode_line_1 (&arg1, 0, 0, 0, 0, 0);

      if (!sals.nelts)
	return;			/*  C++  */
      if (sals.nelts > 1)
	{
	  ambiguous_line_spec (&sals);
	  xfree (sals.sals);
	  return;
	}

      sal = sals.sals[0];
      xfree (sals.sals);
    }

  /* Record whether the BEG arg is all digits.  */

  for (p = arg; p != arg1 && *p >= '0' && *p <= '9'; p++);
  linenum_beg = (p == arg1);

  while (*arg1 == ' ' || *arg1 == '\t')
    arg1++;
  if (*arg1 == ',')
    {
      no_end = 0;
      arg1++;
      while (*arg1 == ' ' || *arg1 == '\t')
	arg1++;
      if (*arg1 == 0)
	dummy_end = 1;
      else
	{
	  if (dummy_beg)
	    sals_end = decode_line_1 (&arg1, 0, 0, 0, 0, 0);
	  else
	    sals_end = decode_line_1 (&arg1, 0, sal.symtab, sal.line, 0, 0);
	  if (sals_end.nelts == 0)
	    return;
	  if (sals_end.nelts > 1)
	    {
	      ambiguous_line_spec (&sals_end);
	      xfree (sals_end.sals);
	      return;
	    }
	  sal_end = sals_end.sals[0];
	  xfree (sals_end.sals);
	}
    }

  if (*arg1)
    error ("Junk at end of line specification.");

  if (!no_end && !dummy_beg && !dummy_end
      && sal.symtab != sal_end.symtab)
    error ("Specified start and end are in different files.");
  if (dummy_beg && dummy_end)
    error ("Two empty args do not say what lines to list.");

  /* if line was specified by address,
     first print exactly which line, and which file.
     In this case, sal.symtab == 0 means address is outside
     of all known source files, not that user failed to give a filename.  */
  if (*arg == '*')
    {
      if (sal.symtab == 0)
	/* FIXME-32x64--assumes sal.pc fits in long.  */
	error ("No source file for address %s.",
	       local_hex_string ((unsigned long) sal.pc));
      sym = find_pc_function (sal.pc);
      if (sym)
	{
	  print_address_numeric (sal.pc, 1, gdb_stdout);
	  printf_filtered (" is in ");
	  fputs_filtered (SYMBOL_PRINT_NAME (sym), gdb_stdout);
	  printf_filtered (" (%s:%d).\n", sal.symtab->filename, sal.line);
	}
      else
	{
	  print_address_numeric (sal.pc, 1, gdb_stdout);
	  printf_filtered (" is at %s:%d.\n",
			   sal.symtab->filename, sal.line);
	}
    }

  /* If line was not specified by just a line number,
     and it does not imply a symtab, it must be an undebuggable symbol
     which means no source code.  */

  if (!linenum_beg && sal.symtab == 0)
    error ("No line number known for %s.", arg);

  /* If this command is repeated with RET,
     turn it into the no-arg variant.  */

  if (from_tty)
    *arg = 0;

  if (dummy_beg && sal_end.symtab == 0)
    error ("No default source file yet.  Do \"help list\".");
  if (dummy_beg)
    print_source_lines (sal_end.symtab,
			max (sal_end.line - (get_lines_to_list () - 1), 1),
			sal_end.line + 1, 0);
  else if (sal.symtab == 0)
    error ("No default source file yet.  Do \"help list\".");
  else if (no_end)
    {
      int first_line = sal.line - get_lines_to_list () / 2;

      if (first_line < 1) first_line = 1;

      print_source_lines (sal.symtab,
		          first_line,
			  first_line + get_lines_to_list (),
			  0);
    }
  else
    print_source_lines (sal.symtab, sal.line,
			(dummy_end
			 ? sal.line + get_lines_to_list ()
			 : sal_end.line + 1),
			0);
}

/* Dump a specified section of assembly code.  With no command line
   arguments, this command will dump the assembly code for the
   function surrounding the pc value in the selected frame.  With one
   argument, it will dump the assembly code surrounding that pc value.
   Two arguments are interpeted as bounds within which to dump
   assembly.  */

static void
disassemble_command (char *arg, int from_tty)
{
  CORE_ADDR low, high;
  char *name;
  CORE_ADDR pc, pc_masked;
  char *space_index;
#if 0
  asection *section;
#endif

  name = NULL;
  if (!arg)
    {
      if (!deprecated_selected_frame)
	error ("No frame selected.\n");

      pc = get_frame_pc (deprecated_selected_frame);
      if (find_pc_partial_function (pc, &name, &low, &high) == 0)
	error ("No function contains program counter for selected frame.\n");
#if defined(TUI)
      /* NOTE: cagney/2003-02-13 The `tui_active' was previously
	 `tui_version'.  */
      if (tui_active)
	/* FIXME: cagney/2004-02-07: This should be an observer.  */
	low = tui_get_low_disassembly_address (low, pc);
#endif
      low += FUNCTION_START_OFFSET;
    }
  else if (!(space_index = (char *) strchr (arg, ' ')))
    {
      /* One argument.  */
      pc = parse_and_eval_address (arg);
      if (find_pc_partial_function (pc, &name, &low, &high) == 0)
	error ("No function contains specified address.\n");
#if defined(TUI)
      /* NOTE: cagney/2003-02-13 The `tui_active' was previously
	 `tui_version'.  */
      if (tui_active)
	/* FIXME: cagney/2004-02-07: This should be an observer.  */
	low = tui_get_low_disassembly_address (low, pc);
#endif
      low += FUNCTION_START_OFFSET;
    }
  else
    {
      /* Two arguments.  */
      *space_index = '\0';
      low = parse_and_eval_address (arg);
      high = parse_and_eval_address (space_index + 1);
    }

#if defined(TUI)
  if (!tui_is_window_visible (DISASSEM_WIN))
#endif
    {
      printf_filtered ("Dump of assembler code ");
      if (name != NULL)
	{
	  printf_filtered ("for function %s:\n", name);
	}
      else
	{
	  printf_filtered ("from ");
	  print_address_numeric (low, 1, gdb_stdout);
	  printf_filtered (" to ");
	  print_address_numeric (high, 1, gdb_stdout);
	  printf_filtered (":\n");
	}

      /* Dump the specified range.  */
      gdb_disassembly (uiout, 0, 0, 0, -1, low, high);

      printf_filtered ("End of assembler dump.\n");
      gdb_flush (gdb_stdout);
    }
#if defined(TUI)
  else
    {
      tui_show_assembly (low);
    }
#endif
}

static void
make_command (char *arg, int from_tty)
{
  char *p;

  if (arg == 0)
    p = "make";
  else
    {
      p = xmalloc (sizeof ("make ") + strlen (arg));
      strcpy (p, "make ");
      strcpy (p + sizeof ("make ") - 1, arg);
    }

  shell_escape (p, from_tty);
}

static void
show_user (char *args, int from_tty)
{
  struct cmd_list_element *c;
  extern struct cmd_list_element *cmdlist;

  if (args)
    {
      c = lookup_cmd (&args, cmdlist, "", 0, 1);
      if (c->class != class_user)
	error ("Not a user command.");
      show_user_1 (c, gdb_stdout);
    }
  else
    {
      for (c = cmdlist; c; c = c->next)
	{
	  if (c->class == class_user)
	    show_user_1 (c, gdb_stdout);
	}
    }
}

/* Search through names of commands and documentations for a certain
   regular expression.
*/
void 
apropos_command (char *searchstr, int from_tty)
{
  extern struct cmd_list_element *cmdlist; /*This is the main command list*/
  regex_t pattern;
  char *pattern_fastmap;
  char errorbuffer[512];
  pattern_fastmap = xcalloc (256, sizeof (char));
  if (searchstr == NULL)
      error("REGEXP string is empty");

  if (regcomp(&pattern,searchstr,REG_ICASE) == 0)
    {
      pattern.fastmap=pattern_fastmap;
      re_compile_fastmap(&pattern);
      apropos_cmd (gdb_stdout,cmdlist,&pattern,"");
    }
  else
    {
      regerror(regcomp(&pattern,searchstr,REG_ICASE),NULL,errorbuffer,512);
      error("Error in regular expression:%s",errorbuffer);
    }
  xfree (pattern_fastmap);
}

/* Print a list of files and line numbers which a user may choose from
   in order to list a function which was specified ambiguously (as with
   `list classname::overloadedfuncname', for example).  The vector in
   SALS provides the filenames and line numbers.  */

static void
ambiguous_line_spec (struct symtabs_and_lines *sals)
{
  int i;

  for (i = 0; i < sals->nelts; ++i)
    printf_filtered ("file: \"%s\", line number: %d\n",
		     sals->sals[i].symtab->filename, sals->sals[i].line);
}

static void
set_debug (char *arg, int from_tty)
{
  printf_unfiltered ("\"set debug\" must be followed by the name of a print subcommand.\n");
  help_list (setdebuglist, "set debug ", -1, gdb_stdout);
}

static void
show_debug (char *args, int from_tty)
{
  cmd_show_list (showdebuglist, from_tty, "");
}

void
init_cmd_lists (void)
{
  max_user_call_depth = 1024;

  cmdlist = NULL;
  infolist = NULL;
  enablelist = NULL;
  disablelist = NULL;
  togglelist = NULL;
  stoplist = NULL;
  deletelist = NULL;
  enablebreaklist = NULL;
  setlist = NULL;
  unsetlist = NULL;
  showlist = NULL;
  sethistlist = NULL;
  showhistlist = NULL;
  unsethistlist = NULL;
  maintenancelist = NULL;
  maintenanceinfolist = NULL;
  maintenanceprintlist = NULL;
  setprintlist = NULL;
  showprintlist = NULL;
  setchecklist = NULL;
  showchecklist = NULL;
}


void
init_cli_cmds (void)
{
  struct cmd_list_element *c;

  /* Define the classes of commands.
     They will appear in the help list in the reverse of this order.  */

  add_cmd ("internals", class_maintenance, NULL,
	   "Maintenance commands.\n\
Some gdb commands are provided just for use by gdb maintainers.\n\
These commands are subject to frequent change, and may not be as\n\
well documented as user commands.",
	   &cmdlist);
  add_cmd ("obscure", class_obscure, NULL, "Obscure features.", &cmdlist);
  add_cmd ("aliases", class_alias, NULL, "Aliases of other commands.", &cmdlist);
  add_cmd ("user-defined", class_user, NULL, "User-defined commands.\n\
The commands in this class are those defined by the user.\n\
Use the \"define\" command to define a command.", &cmdlist);
  add_cmd ("support", class_support, NULL, "Support facilities.", &cmdlist);
  if (!dbx_commands)
    add_cmd ("status", class_info, NULL, "Status inquiries.", &cmdlist);
  add_cmd ("files", class_files, NULL, "Specifying and examining files.", &cmdlist);
  add_cmd ("breakpoints", class_breakpoint, NULL, "Making program stop at certain points.", &cmdlist);
  add_cmd ("data", class_vars, NULL, "Examining data.", &cmdlist);
  add_cmd ("stack", class_stack, NULL, "Examining the stack.\n\
The stack is made up of stack frames.  Gdb assigns numbers to stack frames\n\
counting from zero for the innermost (currently executing) frame.\n\n\
At any time gdb identifies one frame as the \"selected\" frame.\n\
Variable lookups are done with respect to the selected frame.\n\
When the program being debugged stops, gdb selects the innermost frame.\n\
The commands below can be used to select other frames by number or address.",
	   &cmdlist);
  add_cmd ("running", class_run, NULL, "Running the program.", &cmdlist);

  /* Define general commands. */

  add_com ("pwd", class_files, pwd_command,
	"Print working directory.  This is used for your program as well.");
  c = add_cmd ("cd", class_files, cd_command,
	       "Set working directory to DIR for debugger and program being debugged.\n\
The change does not take effect for the program being debugged\n\
until the next time it is started.", &cmdlist);
  set_cmd_completer (c, filename_completer);

  add_com ("echo", class_support, echo_command,
	   "Print a constant string.  Give string as argument.\n\
C escape sequences may be used in the argument.\n\
No newline is added at the end of the argument;\n\
use \"\\n\" if you want a newline to be printed.\n\
Since leading and trailing whitespace are ignored in command arguments,\n\
if you want to print some you must use \"\\\" before leading whitespace\n\
to be printed or after trailing whitespace.");
  add_com ("document", class_support, document_command,
	   "Document a user-defined command.\n\
Give command name as argument.  Give documentation on following lines.\n\
End with a line of just \"end\".");
  add_com ("define", class_support, define_command,
	   "Define a new command name.  Command name is argument.\n\
Definition appears on following lines, one command per line.\n\
End with a line of just \"end\".\n\
Use the \"document\" command to give documentation for the new command.\n\
Commands defined in this way may have up to ten arguments.");

  c = add_cmd ("source", class_support, source_command,
	       "Read commands from a file named FILE.\n\
Note that the file \"" GDBINIT_FILENAME "\" is read automatically in this way\n\
when gdb is started.", &cmdlist);
  set_cmd_completer (c, filename_completer);

  add_com ("quit", class_support, quit_command, "Exit gdb.");
  c = add_com ("help", class_support, help_command, "Print list of commands.");
  set_cmd_completer (c, command_completer);
  add_com_alias ("q", "quit", class_support, 1);
  add_com_alias ("h", "help", class_support, 1);

  c = add_set_cmd ("verbose", class_support, var_boolean, (char *) &info_verbose,
		   "Set ",
		   &setlist),
    add_show_from_set (c, &showlist);
  set_cmd_sfunc (c, set_verbose);
  set_verbose (NULL, 0, c);

  add_prefix_cmd ("history", class_support, set_history,
		  "Generic command for setting command history parameters.",
		  &sethistlist, "set history ", 0, &setlist);
  add_prefix_cmd ("history", class_support, show_history,
		  "Generic command for showing command history parameters.",
		  &showhistlist, "show history ", 0, &showlist);

  add_show_from_set
    (add_set_cmd ("expansion", no_class, var_boolean, (char *) &history_expansion_p,
		  "Set history expansion on command input.\n\
Without an argument, history expansion is enabled.", &sethistlist),
     &showhistlist);

  add_prefix_cmd ("info", class_info, info_command,
     "Generic command for showing things about the program being debugged.",
		  &infolist, "info ", 0, &cmdlist);
  add_com_alias ("i", "info", class_info, 1);

  add_com ("complete", class_obscure, complete_command,
	   "List the completions for the rest of the line as a command.");

  add_prefix_cmd ("show", class_info, show_command,
		  "Generic command for showing things about the debugger.",
		  &showlist, "show ", 0, &cmdlist);
  /* Another way to get at the same thing.  */
  add_info ("set", show_command, "Show all GDB settings.");

  add_cmd ("commands", no_class, show_commands,
	   "Show the history of commands you typed.\n\
You can supply a command number to start with, or a `+' to start after\n\
the previous command number shown.",
	   &showlist);

  add_cmd ("version", no_class, show_version,
	   "Show what version of GDB this is.", &showlist);

  add_com ("while", class_support, while_command,
	   "Execute nested commands WHILE the conditional expression is non zero.\n\
The conditional expression must follow the word `while' and must in turn be\n\
followed by a new line.  The nested commands must be entered one per line,\n\
and should be terminated by the word `end'.");

  add_com ("if", class_support, if_command,
	   "Execute nested commands once IF the conditional expression is non zero.\n\
The conditional expression must follow the word `if' and must in turn be\n\
followed by a new line.  The nested commands must be entered one per line,\n\
and should be terminated by the word 'else' or `end'.  If an else clause\n\
is used, the same rules apply to its nested commands as to the first ones.");

  /* If target is open when baud changes, it doesn't take effect until the
     next open (I think, not sure).  */
  add_show_from_set (add_set_cmd ("remotebaud", no_class,
				  var_zinteger, (char *) &baud_rate,
				  "Set baud rate for remote serial I/O.\n\
This value is used to set the speed of the serial port when debugging\n\
using remote targets.", &setlist),
		     &showlist);

  c = add_set_cmd ("remotedebug", no_class, var_zinteger,
		   (char *) &remote_debug,
		   "Set debugging of remote protocol.\n\
When enabled, each packet sent or received with the remote target\n\
is displayed.", &setlist);
  deprecate_cmd (c, "set debug remote");
  deprecate_cmd (add_show_from_set (c, &showlist), "show debug remote");

  add_show_from_set (add_set_cmd ("remote", no_class, var_zinteger,
				  (char *) &remote_debug,
				  "Set debugging of remote protocol.\n\
When enabled, each packet sent or received with the remote target\n\
is displayed.", &setdebuglist),
		     &showdebuglist);

  add_show_from_set (
		      add_set_cmd ("remotetimeout", no_class, var_integer, (char *) &remote_timeout,
				   "Set timeout limit to wait for target to respond.\n\
This value is used to set the time limit for gdb to wait for a response\n\
from the target.", &setlist),
		      &showlist);

  add_prefix_cmd ("debug", no_class, set_debug,
		  "Generic command for setting gdb debugging flags",
		  &setdebuglist, "set debug ", 0, &setlist);

  add_prefix_cmd ("debug", no_class, show_debug,
		  "Generic command for showing gdb debugging flags",
		  &showdebuglist, "show debug ", 0, &showlist);

  c = add_com ("shell", class_support, shell_escape,
	       "Execute the rest of the line as a shell command.\n\
With no arguments, run an inferior shell.");
  set_cmd_completer (c, filename_completer);

  c = add_com ("edit", class_files, edit_command,
           concat ("Edit specified file or function.\n\
With no argument, edits file containing most recent line listed.\n\
", "\
Editing targets can be specified in these ways:\n\
  FILE:LINENUM, to edit at that line in that file,\n\
  FUNCTION, to edit at the beginning of that function,\n\
  FILE:FUNCTION, to distinguish among like-named static functions.\n\
  *ADDRESS, to edit at the line containing that address.\n\
Uses EDITOR environment variable contents as editor (or ex as default).",NULL));

  c->completer = location_completer;

  add_com ("list", class_files, list_command,
	   concat ("List specified function or line.\n\
With no argument, lists ten more lines after or around previous listing.\n\
\"list -\" lists the ten lines before a previous ten-line listing.\n\
One argument specifies a line, and ten lines are listed around that line.\n\
Two arguments with comma between specify starting and ending lines to list.\n\
", "\
Lines can be specified in these ways:\n\
  LINENUM, to list around that line in current file,\n\
  FILE:LINENUM, to list around that line in that file,\n\
  FUNCTION, to list around beginning of that function,\n\
  FILE:FUNCTION, to distinguish among like-named static functions.\n\
  *ADDRESS, to list around the line containing that address.\n\
With two args if one is empty it stands for ten lines away from the other arg.", NULL));

  if (!xdb_commands)
    add_com_alias ("l", "list", class_files, 1);
  else
    add_com_alias ("v", "list", class_files, 1);

  if (dbx_commands)
    add_com_alias ("file", "list", class_files, 1);

  c = add_com ("disassemble", class_vars, disassemble_command,
	       "Disassemble a specified section of memory.\n\
Default is the function surrounding the pc of the selected frame.\n\
With a single argument, the function surrounding that address is dumped.\n\
Two arguments are taken as a range of memory to dump.");
  set_cmd_completer (c, location_completer);
  if (xdb_commands)
    add_com_alias ("va", "disassemble", class_xdb, 0);

  /* NOTE: cagney/2000-03-20: Being able to enter ``(gdb) !ls'' would
     be a really useful feature.  Unfortunately, the below wont do
     this.  Instead it adds support for the form ``(gdb) ! ls''
     (i.e. the space is required).  If the ``!'' command below is
     added the complains about no ``!'' command would be replaced by
     complains about how the ``!'' command is broken :-) */
  if (xdb_commands)
    add_com_alias ("!", "shell", class_support, 0);

  c = add_com ("make", class_support, make_command,
          "Run the ``make'' program using the rest of the line as arguments.");
  set_cmd_completer (c, filename_completer);
  add_cmd ("user", no_class, show_user,
	   "Show definitions of user defined commands.\n\
Argument is the name of the user defined command.\n\
With no argument, show definitions of all user defined commands.", &showlist);
  add_com ("apropos", class_support, apropos_command, "Search for commands matching a REGEXP");

  add_show_from_set (
		      add_set_cmd ("max-user-call-depth", no_class, var_integer, 
				   (char *) &max_user_call_depth,
				   "Set the max call depth for user-defined commands.\n", 
				   &setlist),
		      &showlist);
}

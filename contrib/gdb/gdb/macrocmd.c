/* C preprocessor macro expansion commands for GDB.
   Copyright 2002 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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
#include "macrotab.h"
#include "macroexp.h"
#include "macroscope.h"
#include "command.h"
#include "gdbcmd.h"


/* The `macro' prefix command.  */

static struct cmd_list_element *macrolist;

static void
macro_command (char *arg, int from_tty)
{
  printf_unfiltered
    ("\"macro\" must be followed by the name of a macro command.\n");
  help_list (macrolist, "macro ", -1, gdb_stdout);
}



/* Macro expansion commands.  */


static void
macro_expand_command (char *exp, int from_tty)
{
  struct macro_scope *ms = NULL;
  char *expanded = NULL;
  struct cleanup *cleanup_chain = make_cleanup (free_current_contents, &ms);
  make_cleanup (free_current_contents, &expanded);

  /* You know, when the user doesn't specify any expression, it would be
     really cool if this defaulted to the last expression evaluated.
     Then it would be easy to ask, "Hey, what did I just evaluate?"  But
     at the moment, the `print' commands don't save the last expression
     evaluated, just its value.  */
  if (! exp || ! *exp)
    error ("You must follow the `macro expand' command with the"
           " expression you\n"
           "want to expand.");

  ms = default_macro_scope ();
  if (ms)
    {
      expanded = macro_expand (exp, standard_macro_lookup, ms);
      fputs_filtered ("expands to: ", gdb_stdout);
      fputs_filtered (expanded, gdb_stdout);
      fputs_filtered ("\n", gdb_stdout);
    }
  else
    fputs_filtered ("GDB has no preprocessor macro information for "
                    "that code.\n",
                    gdb_stdout);

  do_cleanups (cleanup_chain);
  return;
}


static void
macro_expand_once_command (char *exp, int from_tty)
{
  struct macro_scope *ms = NULL;
  char *expanded = NULL;
  struct cleanup *cleanup_chain = make_cleanup (free_current_contents, &ms);
  make_cleanup (free_current_contents, &expanded);

  /* You know, when the user doesn't specify any expression, it would be
     really cool if this defaulted to the last expression evaluated.
     And it should set the once-expanded text as the new `last
     expression'.  That way, you could just hit return over and over and
     see the expression expanded one level at a time.  */
  if (! exp || ! *exp)
    error ("You must follow the `macro expand-once' command with"
           " the expression\n"
           "you want to expand.");

  ms = default_macro_scope ();
  if (ms)
    {
      expanded = macro_expand_once (exp, standard_macro_lookup, ms);
      fputs_filtered ("expands to: ", gdb_stdout);
      fputs_filtered (expanded, gdb_stdout);
      fputs_filtered ("\n", gdb_stdout);
    }
  else
    fputs_filtered ("GDB has no preprocessor macro information for "
                    "that code.\n",
                    gdb_stdout);

  do_cleanups (cleanup_chain);
  return;
}


static void
show_pp_source_pos (struct ui_file *stream,
                    struct macro_source_file *file,
                    int line)
{
  fprintf_filtered (stream, "%s:%d\n", file->filename, line);

  while (file->included_by)
    {
      fprintf_filtered (gdb_stdout, "  included at %s:%d\n",
                        file->included_by->filename,
                        file->included_at_line);
      file = file->included_by;
    }
}


static void
info_macro_command (char *name, int from_tty)
{
  struct macro_scope *ms = NULL;
  struct cleanup *cleanup_chain = make_cleanup (free_current_contents, &ms);
  struct macro_definition *d;
  
  if (! name || ! *name)
    error ("You must follow the `info macro' command with the name"
           " of the macro\n"
           "whose definition you want to see.");

  ms = default_macro_scope ();
  if (! ms)
    error ("GDB has no preprocessor macro information for that code.");

  d = macro_lookup_definition (ms->file, ms->line, name);
  if (d)
    {
      int line;
      struct macro_source_file *file
        = macro_definition_location (ms->file, ms->line, name, &line);

      fprintf_filtered (gdb_stdout, "Defined at ");
      show_pp_source_pos (gdb_stdout, file, line);
      fprintf_filtered (gdb_stdout, "#define %s", name);
      if (d->kind == macro_function_like)
        {
          int i;

          fputs_filtered ("(", gdb_stdout);
          for (i = 0; i < d->argc; i++)
            {
              fputs_filtered (d->argv[i], gdb_stdout);
              if (i + 1 < d->argc)
                fputs_filtered (", ", gdb_stdout);
            }
          fputs_filtered (")", gdb_stdout);
        }
      fprintf_filtered (gdb_stdout, " %s\n", d->replacement);
    }
  else
    {
      fprintf_filtered (gdb_stdout,
                        "The symbol `%s' has no definition as a C/C++"
                        " preprocessor macro\n"
                        "at ", name);
      show_pp_source_pos (gdb_stdout, ms->file, ms->line);
    }

  do_cleanups (cleanup_chain);
}



/* User-defined macros.  */

/* A table of user-defined macros.  Unlike the macro tables used for
   symtabs, this one uses xmalloc for all its allocation, not an
   obstack, and it doesn't bcache anything; it just xmallocs things.  So
   it's perfectly possible to remove things from this, or redefine
   things.  */
static struct macro_table *user_macros;

static void
macro_define_command (char *exp, int from_tty)
{
  error ("Command not implemented yet.");
}


static void
macro_undef_command (char *exp, int from_tty)
{
  error ("Command not implemented yet.");
}


static void
macro_list_command (char *exp, int from_tty)
{
  error ("Command not implemented yet.");
}



/* Initializing the `macrocmd' module.  */

extern initialize_file_ftype _initialize_macrocmd; /* -Wmissing-prototypes */

void
_initialize_macrocmd (void)
{
  struct cmd_list_element *c;

  /* We introduce a new command prefix, `macro', under which we'll put
     the various commands for working with preprocessor macros.  */
  add_prefix_cmd
    ("macro", class_info, macro_command,
     "Prefix for commands dealing with C preprocessor macros.",
     &macrolist, "macro ", 0, &cmdlist);

  add_cmd
    ("expand", no_class, macro_expand_command,
     "Fully expand any C/C++ preprocessor macro invocations in EXPRESSION.\n"
     "Show the expanded expression.",
     &macrolist);
  add_alias_cmd ("exp", "expand", no_class, 1, &macrolist);
  add_cmd
    ("expand-once", no_class, macro_expand_once_command,
     "Expand C/C++ preprocessor macro invocations appearing directly in"
     " EXPRESSION.\n"
     "Show the expanded expression.\n"
     "\n"
     "This command differs from `macro expand' in that it only expands macro\n"
     "invocations that appear directly in EXPRESSION; if expanding a macro\n"
     "introduces further macro invocations, those are left unexpanded.\n"
     "\n"
     "`macro expand-once' helps you see how a particular macro expands,\n"
     "whereas `macro expand' shows you how all the macros involved in an\n"
     "expression work together to yield a pre-processed expression.",
     &macrolist);
  add_alias_cmd ("exp1", "expand-once", no_class, 1, &macrolist);

  add_cmd
    ("macro", no_class, info_macro_command,
     "Show the definition of MACRO, and its source location.",
     &infolist);

  add_cmd
    ("define", no_class, macro_define_command,
     "Define a new C/C++ preprocessor macro.\n"
     "The GDB command `macro define DEFINITION' is equivalent to placing a\n"
     "preprocessor directive of the form `#define DEFINITION' such that the\n"
     "definition is visible in all the inferior's source files.\n"
     "For example:\n"
     "  (gdb) macro define PI (3.1415926)\n"
     "  (gdb) macro define MIN(x,y) ((x) < (y) ? (x) : (y))",
     &macrolist);

  add_cmd
    ("undef", no_class, macro_undef_command,
     "Remove the definition of the C/C++ preprocessor macro with the"
     " given name.",
     &macrolist);

  add_cmd
    ("list", no_class, macro_list_command,
     "List all the macros defined using the `macro define' command.",
     &macrolist);

  user_macros = new_macro_table (0, 0);
}

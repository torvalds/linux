/* Memory-access and commands for "inferior" process, for GDB.
   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.

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
#include <signal.h>
#include "gdb_string.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "frame.h"
#include "inferior.h"
#include "environ.h"
#include "value.h"
#include "gdbcmd.h"
#include "symfile.h"
#include "gdbcore.h"
#include "target.h"
#include "language.h"
#include "symfile.h"
#include "objfiles.h"
#include "completer.h"
#include "ui-out.h"
#include "event-top.h"
#include "parser-defs.h"
#include "regcache.h"
#include "reggroups.h"
#include "block.h"
#include <ctype.h>
#include "gdb_assert.h"

/* Functions exported for general use, in inferior.h: */

void all_registers_info (char *, int);

void registers_info (char *, int);

void nexti_command (char *, int);

void stepi_command (char *, int);

void continue_command (char *, int);

void interrupt_target_command (char *args, int from_tty);

/* Local functions: */

static void nofp_registers_info (char *, int);

static void print_return_value (int struct_return, struct type *value_type);

static void finish_command_continuation (struct continuation_arg *);

static void until_next_command (int);

static void until_command (char *, int);

static void path_info (char *, int);

static void path_command (char *, int);

static void unset_command (char *, int);

static void float_info (char *, int);

static void detach_command (char *, int);

static void disconnect_command (char *, int);

static void unset_environment_command (char *, int);

static void set_environment_command (char *, int);

static void environment_info (char *, int);

static void program_info (char *, int);

static void finish_command (char *, int);

static void signal_command (char *, int);

static void jump_command (char *, int);

static void step_1 (int, int, char *);
static void step_once (int skip_subroutines, int single_inst, int count);
static void step_1_continuation (struct continuation_arg *arg);

static void next_command (char *, int);

static void step_command (char *, int);

static void run_command (char *, int);

static void run_no_args_command (char *args, int from_tty);

static void go_command (char *line_no, int from_tty);

static int strip_bg_char (char **);

void _initialize_infcmd (void);

#define GO_USAGE   "Usage: go <location>\n"

#define ERROR_NO_INFERIOR \
   if (!target_has_execution) error ("The program is not being run.");

/* String containing arguments to give to the program, separated by spaces.
   Empty string (pointer to '\0') means no args.  */

static char *inferior_args;

/* The inferior arguments as a vector.  If INFERIOR_ARGC is nonzero,
   then we must compute INFERIOR_ARGS from this (via the target).  */

static int inferior_argc;
static char **inferior_argv;

/* File name for default use for standard in/out in the inferior.  */

char *inferior_io_terminal;

/* Pid of our debugged inferior, or 0 if no inferior now.
   Since various parts of infrun.c test this to see whether there is a program
   being debugged it should be nonzero (currently 3 is used) for remote
   debugging.  */

ptid_t inferior_ptid;

/* Last signal that the inferior received (why it stopped).  */

enum target_signal stop_signal;

/* Address at which inferior stopped.  */

CORE_ADDR stop_pc;

/* Chain containing status of breakpoint(s) that we have stopped at.  */

bpstat stop_bpstat;

/* Flag indicating that a command has proceeded the inferior past the
   current breakpoint.  */

int breakpoint_proceeded;

/* Nonzero if stopped due to a step command.  */

int stop_step;

/* Nonzero if stopped due to completion of a stack dummy routine.  */

int stop_stack_dummy;

/* Nonzero if stopped due to a random (unexpected) signal in inferior
   process.  */

int stopped_by_random_signal;

/* Range to single step within.
   If this is nonzero, respond to a single-step signal
   by continuing to step if the pc is in this range.  */

CORE_ADDR step_range_start;	/* Inclusive */
CORE_ADDR step_range_end;	/* Exclusive */

/* Stack frame address as of when stepping command was issued.
   This is how we know when we step into a subroutine call,
   and how to set the frame for the breakpoint used to step out.  */

struct frame_id step_frame_id;

/* Our notion of the current stack pointer.  */

CORE_ADDR step_sp;

enum step_over_calls_kind step_over_calls;

/* If stepping, nonzero means step count is > 1
   so don't print frame next time inferior stops
   if it stops due to stepping.  */

int step_multi;

/* Environment to use for running inferior,
   in format described in environ.h.  */

struct environ *inferior_environ;

/* Accessor routines. */

char *
get_inferior_args (void)
{
  if (inferior_argc != 0)
    {
      char *n, *old;

      n = gdbarch_construct_inferior_arguments (current_gdbarch,
						inferior_argc, inferior_argv);
      old = set_inferior_args (n);
      xfree (old);
    }

  if (inferior_args == NULL)
    inferior_args = xstrdup ("");

  return inferior_args;
}

char *
set_inferior_args (char *newargs)
{
  char *saved_args = inferior_args;

  inferior_args = newargs;
  inferior_argc = 0;
  inferior_argv = 0;

  return saved_args;
}

void
set_inferior_args_vector (int argc, char **argv)
{
  inferior_argc = argc;
  inferior_argv = argv;
}

/* Notice when `set args' is run.  */
static void
notice_args_set (char *args, int from_tty, struct cmd_list_element *c)
{
  inferior_argc = 0;
  inferior_argv = 0;
}

/* Notice when `show args' is run.  */
static void
notice_args_read (char *args, int from_tty, struct cmd_list_element *c)
{
  /* Might compute the value.  */
  get_inferior_args ();
}


/* Compute command-line string given argument vector.  This does the
   same shell processing as fork_inferior.  */
char *
construct_inferior_arguments (struct gdbarch *gdbarch, int argc, char **argv)
{
  char *result;

  if (STARTUP_WITH_SHELL)
    {
      /* This holds all the characters considered special to the
	 typical Unix shells.  We include `^' because the SunOS
	 /bin/sh treats it as a synonym for `|'.  */
      char *special = "\"!#$&*()\\|[]{}<>?'\"`~^; \t\n";
      int i;
      int length = 0;
      char *out, *cp;

      /* We over-compute the size.  It shouldn't matter.  */
      for (i = 0; i < argc; ++i)
	length += 2 * strlen (argv[i]) + 1 + 2 * (argv[i][0] == '\0');

      result = (char *) xmalloc (length);
      out = result;

      for (i = 0; i < argc; ++i)
	{
	  if (i > 0)
	    *out++ = ' ';

	  /* Need to handle empty arguments specially.  */
	  if (argv[i][0] == '\0')
	    {
	      *out++ = '\'';
	      *out++ = '\'';
	    }
	  else
	    {
	      for (cp = argv[i]; *cp; ++cp)
		{
		  if (strchr (special, *cp) != NULL)
		    *out++ = '\\';
		  *out++ = *cp;
		}
	    }
	}
      *out = '\0';
    }
  else
    {
      /* In this case we can't handle arguments that contain spaces,
	 tabs, or newlines -- see breakup_args().  */
      int i;
      int length = 0;

      for (i = 0; i < argc; ++i)
	{
	  char *cp = strchr (argv[i], ' ');
	  if (cp == NULL)
	    cp = strchr (argv[i], '\t');
	  if (cp == NULL)
	    cp = strchr (argv[i], '\n');
	  if (cp != NULL)
	    error ("can't handle command-line argument containing whitespace");
	  length += strlen (argv[i]) + 1;
	}

      result = (char *) xmalloc (length);
      result[0] = '\0';
      for (i = 0; i < argc; ++i)
	{
	  if (i > 0)
	    strcat (result, " ");
	  strcat (result, argv[i]);
	}
    }

  return result;
}


/* This function detects whether or not a '&' character (indicating
   background execution) has been added as *the last* of the arguments ARGS
   of a command. If it has, it removes it and returns 1. Otherwise it
   does nothing and returns 0. */
static int
strip_bg_char (char **args)
{
  char *p = NULL;

  p = strchr (*args, '&');

  if (p)
    {
      if (p == (*args + strlen (*args) - 1))
	{
	  if (strlen (*args) > 1)
	    {
	      do
		p--;
	      while (*p == ' ' || *p == '\t');
	      *(p + 1) = '\0';
	    }
	  else
	    *args = 0;
	  return 1;
	}
    }
  return 0;
}

void
tty_command (char *file, int from_tty)
{
  if (file == 0)
    error_no_arg ("terminal name for running target process");

  inferior_io_terminal = savestring (file, strlen (file));
}

static void
run_command (char *args, int from_tty)
{
  char *exec_file;

  dont_repeat ();

  if (! ptid_equal (inferior_ptid, null_ptid) && target_has_execution)
    {
      if (from_tty
	  && !query ("The program being debugged has been started already.\n\
Start it from the beginning? "))
	error ("Program not restarted.");
      target_kill ();
#if defined(SOLIB_RESTART)
      SOLIB_RESTART ();
#endif
      init_wait_for_inferior ();
    }

  clear_breakpoint_hit_counts ();

  /* Purge old solib objfiles. */
  objfile_purge_solibs ();

  do_run_cleanups (NULL);

  /* The comment here used to read, "The exec file is re-read every
     time we do a generic_mourn_inferior, so we just have to worry
     about the symbol file."  The `generic_mourn_inferior' function
     gets called whenever the program exits.  However, suppose the
     program exits, and *then* the executable file changes?  We need
     to check again here.  Since reopen_exec_file doesn't do anything
     if the timestamp hasn't changed, I don't see the harm.  */
  reopen_exec_file ();
  reread_symbols ();

  exec_file = (char *) get_exec_file (0);

  /* We keep symbols from add-symbol-file, on the grounds that the
     user might want to add some symbols before running the program
     (right?).  But sometimes (dynamic loading where the user manually
     introduces the new symbols with add-symbol-file), the code which
     the symbols describe does not persist between runs.  Currently
     the user has to manually nuke all symbols between runs if they
     want them to go away (PR 2207).  This is probably reasonable.  */

  if (!args)
    {
      if (event_loop_p && target_can_async_p ())
	async_disable_stdin ();
    }
  else
    {
      int async_exec = strip_bg_char (&args);

      /* If we get a request for running in the bg but the target
         doesn't support it, error out. */
      if (event_loop_p && async_exec && !target_can_async_p ())
	error ("Asynchronous execution not supported on this target.");

      /* If we don't get a request of running in the bg, then we need
         to simulate synchronous (fg) execution. */
      if (event_loop_p && !async_exec && target_can_async_p ())
	{
	  /* Simulate synchronous execution */
	  async_disable_stdin ();
	}

      /* If there were other args, beside '&', process them. */
      if (args)
	{
          char *old_args = set_inferior_args (xstrdup (args));
          xfree (old_args);
	}
    }

  if (from_tty)
    {
      ui_out_field_string (uiout, NULL, "Starting program");
      ui_out_text (uiout, ": ");
      if (exec_file)
	ui_out_field_string (uiout, "execfile", exec_file);
      ui_out_spaces (uiout, 1);
      /* We call get_inferior_args() because we might need to compute
	 the value now.  */
      ui_out_field_string (uiout, "infargs", get_inferior_args ());
      ui_out_text (uiout, "\n");
      ui_out_flush (uiout);
    }

  /* We call get_inferior_args() because we might need to compute
     the value now.  */
  target_create_inferior (exec_file, get_inferior_args (),
			  environ_vector (inferior_environ));
}


static void
run_no_args_command (char *args, int from_tty)
{
  char *old_args = set_inferior_args (xstrdup (""));
  xfree (old_args);
}


void
continue_command (char *proc_count_exp, int from_tty)
{
  int async_exec = 0;
  ERROR_NO_INFERIOR;

  /* Find out whether we must run in the background. */
  if (proc_count_exp != NULL)
    async_exec = strip_bg_char (&proc_count_exp);

  /* If we must run in the background, but the target can't do it,
     error out. */
  if (event_loop_p && async_exec && !target_can_async_p ())
    error ("Asynchronous execution not supported on this target.");

  /* If we are not asked to run in the bg, then prepare to run in the
     foreground, synchronously. */
  if (event_loop_p && !async_exec && target_can_async_p ())
    {
      /* Simulate synchronous execution */
      async_disable_stdin ();
    }

  /* If have argument (besides '&'), set proceed count of breakpoint
     we stopped at.  */
  if (proc_count_exp != NULL)
    {
      bpstat bs = stop_bpstat;
      int num = bpstat_num (&bs);
      if (num == 0 && from_tty)
	{
	  printf_filtered
	    ("Not stopped at any breakpoint; argument ignored.\n");
	}
      while (num != 0)
	{
	  set_ignore_count (num,
			    parse_and_eval_long (proc_count_exp) - 1,
			    from_tty);
	  /* set_ignore_count prints a message ending with a period.
	     So print two spaces before "Continuing.".  */
	  if (from_tty)
	    printf_filtered ("  ");
	  num = bpstat_num (&bs);
	}
    }

  if (from_tty)
    printf_filtered ("Continuing.\n");

  clear_proceed_status ();

  proceed ((CORE_ADDR) -1, TARGET_SIGNAL_DEFAULT, 0);
}

/* Step until outside of current statement.  */

static void
step_command (char *count_string, int from_tty)
{
  step_1 (0, 0, count_string);
}

/* Likewise, but skip over subroutine calls as if single instructions.  */

static void
next_command (char *count_string, int from_tty)
{
  step_1 (1, 0, count_string);
}

/* Likewise, but step only one instruction.  */

void
stepi_command (char *count_string, int from_tty)
{
  step_1 (0, 1, count_string);
}

void
nexti_command (char *count_string, int from_tty)
{
  step_1 (1, 1, count_string);
}

static void
disable_longjmp_breakpoint_cleanup (void *ignore)
{
  disable_longjmp_breakpoint ();
}

static void
step_1 (int skip_subroutines, int single_inst, char *count_string)
{
  int count = 1;
  struct frame_info *frame;
  struct cleanup *cleanups = 0;
  int async_exec = 0;

  ERROR_NO_INFERIOR;

  if (count_string)
    async_exec = strip_bg_char (&count_string);

  /* If we get a request for running in the bg but the target
     doesn't support it, error out. */
  if (event_loop_p && async_exec && !target_can_async_p ())
    error ("Asynchronous execution not supported on this target.");

  /* If we don't get a request of running in the bg, then we need
     to simulate synchronous (fg) execution. */
  if (event_loop_p && !async_exec && target_can_async_p ())
    {
      /* Simulate synchronous execution */
      async_disable_stdin ();
    }

  count = count_string ? parse_and_eval_long (count_string) : 1;

  if (!single_inst || skip_subroutines)		/* leave si command alone */
    {
      enable_longjmp_breakpoint ();
      if (!event_loop_p || !target_can_async_p ())
	cleanups = make_cleanup (disable_longjmp_breakpoint_cleanup, 0 /*ignore*/);
      else
        make_exec_cleanup (disable_longjmp_breakpoint_cleanup, 0 /*ignore*/);
    }

  /* In synchronous case, all is well, just use the regular for loop. */
  if (!event_loop_p || !target_can_async_p ())
    {
      for (; count > 0; count--)
	{
	  clear_proceed_status ();

	  frame = get_current_frame ();
	  if (!frame)		/* Avoid coredump here.  Why tho? */
	    error ("No current frame");
	  step_frame_id = get_frame_id (frame);
	  step_sp = read_sp ();

	  if (!single_inst)
	    {
	      find_pc_line_pc_range (stop_pc, &step_range_start, &step_range_end);
	      if (step_range_end == 0)
		{
		  char *name;
		  if (find_pc_partial_function (stop_pc, &name, &step_range_start,
						&step_range_end) == 0)
		    error ("Cannot find bounds of current function");

		  target_terminal_ours ();
		  printf_filtered ("\
Single stepping until exit from function %s, \n\
which has no line number information.\n", name);
		}
	    }
	  else
	    {
	      /* Say we are stepping, but stop after one insn whatever it does.  */
	      step_range_start = step_range_end = 1;
	      if (!skip_subroutines)
		/* It is stepi.
		   Don't step over function calls, not even to functions lacking
		   line numbers.  */
		step_over_calls = STEP_OVER_NONE;
	    }

	  if (skip_subroutines)
	    step_over_calls = STEP_OVER_ALL;

	  step_multi = (count > 1);
	  proceed ((CORE_ADDR) -1, TARGET_SIGNAL_DEFAULT, 1);

	  if (!stop_step)
	    break;
	}

      if (!single_inst || skip_subroutines)
	do_cleanups (cleanups);
      return;
    }
  /* In case of asynchronous target things get complicated, do only
     one step for now, before returning control to the event loop. Let
     the continuation figure out how many other steps we need to do,
     and handle them one at the time, through step_once(). */
  else
    {
      if (event_loop_p && target_can_async_p ())
	step_once (skip_subroutines, single_inst, count);
    }
}

/* Called after we are done with one step operation, to check whether
   we need to step again, before we print the prompt and return control
   to the user. If count is > 1, we will need to do one more call to
   proceed(), via step_once(). Basically it is like step_once and
   step_1_continuation are co-recursive. */
static void
step_1_continuation (struct continuation_arg *arg)
{
  int count;
  int skip_subroutines;
  int single_inst;

  skip_subroutines = arg->data.integer;
  single_inst      = arg->next->data.integer;
  count            = arg->next->next->data.integer;

  if (stop_step)
    step_once (skip_subroutines, single_inst, count - 1);
  else
    if (!single_inst || skip_subroutines)
      do_exec_cleanups (ALL_CLEANUPS);
}

/* Do just one step operation. If count >1 we will have to set up a
   continuation to be done after the target stops (after this one
   step). This is useful to implement the 'step n' kind of commands, in
   case of asynchronous targets. We had to split step_1 into two parts,
   one to be done before proceed() and one afterwards. This function is
   called in case of step n with n>1, after the first step operation has
   been completed.*/
static void 
step_once (int skip_subroutines, int single_inst, int count)
{ 
  struct continuation_arg *arg1; 
  struct continuation_arg *arg2;
  struct continuation_arg *arg3; 
  struct frame_info *frame;

  if (count > 0)
    {
      clear_proceed_status ();

      frame = get_current_frame ();
      if (!frame)		/* Avoid coredump here.  Why tho? */
	error ("No current frame");
      step_frame_id = get_frame_id (frame);
      step_sp = read_sp ();

      if (!single_inst)
	{
	  find_pc_line_pc_range (stop_pc, &step_range_start, &step_range_end);

	  /* If we have no line info, switch to stepi mode.  */
	  if (step_range_end == 0 && step_stop_if_no_debug)
	    {
	      step_range_start = step_range_end = 1;
	    }
	  else if (step_range_end == 0)
	    {
	      char *name;
	      if (find_pc_partial_function (stop_pc, &name, &step_range_start,
					    &step_range_end) == 0)
		error ("Cannot find bounds of current function");

	      target_terminal_ours ();
	      printf_filtered ("\
Single stepping until exit from function %s, \n\
which has no line number information.\n", name);
	    }
	}
      else
	{
	  /* Say we are stepping, but stop after one insn whatever it does.  */
	  step_range_start = step_range_end = 1;
	  if (!skip_subroutines)
	    /* It is stepi.
	       Don't step over function calls, not even to functions lacking
	       line numbers.  */
	    step_over_calls = STEP_OVER_NONE;
	}

      if (skip_subroutines)
	step_over_calls = STEP_OVER_ALL;

      step_multi = (count > 1);
      arg1 =
	(struct continuation_arg *) xmalloc (sizeof (struct continuation_arg));
      arg2 =
	(struct continuation_arg *) xmalloc (sizeof (struct continuation_arg));
      arg3 =
	(struct continuation_arg *) xmalloc (sizeof (struct continuation_arg));
      arg1->next = arg2;
      arg1->data.integer = skip_subroutines;
      arg2->next = arg3;
      arg2->data.integer = single_inst;
      arg3->next = NULL;
      arg3->data.integer = count;
      add_intermediate_continuation (step_1_continuation, arg1);
      proceed ((CORE_ADDR) -1, TARGET_SIGNAL_DEFAULT, 1);
    }
}


/* Continue program at specified address.  */

static void
jump_command (char *arg, int from_tty)
{
  CORE_ADDR addr;
  struct symtabs_and_lines sals;
  struct symtab_and_line sal;
  struct symbol *fn;
  struct symbol *sfn;
  int async_exec = 0;

  ERROR_NO_INFERIOR;

  /* Find out whether we must run in the background. */
  if (arg != NULL)
    async_exec = strip_bg_char (&arg);

  /* If we must run in the background, but the target can't do it,
     error out. */
  if (event_loop_p && async_exec && !target_can_async_p ())
    error ("Asynchronous execution not supported on this target.");

  /* If we are not asked to run in the bg, then prepare to run in the
     foreground, synchronously. */
  if (event_loop_p && !async_exec && target_can_async_p ())
    {
      /* Simulate synchronous execution */
      async_disable_stdin ();
    }

  if (!arg)
    error_no_arg ("starting address");

  sals = decode_line_spec_1 (arg, 1);
  if (sals.nelts != 1)
    {
      error ("Unreasonable jump request");
    }

  sal = sals.sals[0];
  xfree (sals.sals);

  if (sal.symtab == 0 && sal.pc == 0)
    error ("No source file has been specified.");

  resolve_sal_pc (&sal);	/* May error out */

  /* See if we are trying to jump to another function. */
  fn = get_frame_function (get_current_frame ());
  sfn = find_pc_function (sal.pc);
  if (fn != NULL && sfn != fn)
    {
      if (!query ("Line %d is not in `%s'.  Jump anyway? ", sal.line,
		  SYMBOL_PRINT_NAME (fn)))
	{
	  error ("Not confirmed.");
	  /* NOTREACHED */
	}
    }

  if (sfn != NULL)
    {
      fixup_symbol_section (sfn, 0);
      if (section_is_overlay (SYMBOL_BFD_SECTION (sfn)) &&
	  !section_is_mapped (SYMBOL_BFD_SECTION (sfn)))
	{
	  if (!query ("WARNING!!!  Destination is in unmapped overlay!  Jump anyway? "))
	    {
	      error ("Not confirmed.");
	      /* NOTREACHED */
	    }
	}
    }

  addr = sal.pc;

  if (from_tty)
    {
      printf_filtered ("Continuing at ");
      print_address_numeric (addr, 1, gdb_stdout);
      printf_filtered (".\n");
    }

  clear_proceed_status ();
  proceed (addr, TARGET_SIGNAL_0, 0);
}


/* Go to line or address in current procedure */
static void
go_command (char *line_no, int from_tty)
{
  if (line_no == (char *) NULL || !*line_no)
    printf_filtered (GO_USAGE);
  else
    {
      tbreak_command (line_no, from_tty);
      jump_command (line_no, from_tty);
    }
}


/* Continue program giving it specified signal.  */

static void
signal_command (char *signum_exp, int from_tty)
{
  enum target_signal oursig;

  dont_repeat ();		/* Too dangerous.  */
  ERROR_NO_INFERIOR;

  if (!signum_exp)
    error_no_arg ("signal number");

  /* It would be even slicker to make signal names be valid expressions,
     (the type could be "enum $signal" or some such), then the user could
     assign them to convenience variables.  */
  oursig = target_signal_from_name (signum_exp);

  if (oursig == TARGET_SIGNAL_UNKNOWN)
    {
      /* No, try numeric.  */
      int num = parse_and_eval_long (signum_exp);

      if (num == 0)
	oursig = TARGET_SIGNAL_0;
      else
	oursig = target_signal_from_command (num);
    }

  if (from_tty)
    {
      if (oursig == TARGET_SIGNAL_0)
	printf_filtered ("Continuing with no signal.\n");
      else
	printf_filtered ("Continuing with signal %s.\n",
			 target_signal_to_name (oursig));
    }

  clear_proceed_status ();
  /* "signal 0" should not get stuck if we are stopped at a breakpoint.
     FIXME: Neither should "signal foo" but when I tried passing
     (CORE_ADDR)-1 unconditionally I got a testsuite failure which I haven't
     tried to track down yet.  */
  proceed (oursig == TARGET_SIGNAL_0 ? (CORE_ADDR) -1 : stop_pc, oursig, 0);
}

/* Proceed until we reach a different source line with pc greater than
   our current one or exit the function.  We skip calls in both cases.

   Note that eventually this command should probably be changed so
   that only source lines are printed out when we hit the breakpoint
   we set.  This may involve changes to wait_for_inferior and the
   proceed status code.  */

static void
until_next_command (int from_tty)
{
  struct frame_info *frame;
  CORE_ADDR pc;
  struct symbol *func;
  struct symtab_and_line sal;

  clear_proceed_status ();

  frame = get_current_frame ();

  /* Step until either exited from this function or greater
     than the current line (if in symbolic section) or pc (if
     not). */

  pc = read_pc ();
  func = find_pc_function (pc);

  if (!func)
    {
      struct minimal_symbol *msymbol = lookup_minimal_symbol_by_pc (pc);

      if (msymbol == NULL)
	error ("Execution is not within a known function.");

      step_range_start = SYMBOL_VALUE_ADDRESS (msymbol);
      step_range_end = pc;
    }
  else
    {
      sal = find_pc_line (pc, 0);

      step_range_start = BLOCK_START (SYMBOL_BLOCK_VALUE (func));
      step_range_end = sal.end;
    }

  step_over_calls = STEP_OVER_ALL;
  step_frame_id = get_frame_id (frame);
  step_sp = read_sp ();

  step_multi = 0;		/* Only one call to proceed */

  proceed ((CORE_ADDR) -1, TARGET_SIGNAL_DEFAULT, 1);
}

static void
until_command (char *arg, int from_tty)
{
  int async_exec = 0;

  if (!target_has_execution)
    error ("The program is not running.");

  /* Find out whether we must run in the background. */
  if (arg != NULL)
    async_exec = strip_bg_char (&arg);

  /* If we must run in the background, but the target can't do it,
     error out. */
  if (event_loop_p && async_exec && !target_can_async_p ())
    error ("Asynchronous execution not supported on this target.");

  /* If we are not asked to run in the bg, then prepare to run in the
     foreground, synchronously. */
  if (event_loop_p && !async_exec && target_can_async_p ())
    {
      /* Simulate synchronous execution */
      async_disable_stdin ();
    }

  if (arg)
    until_break_command (arg, from_tty, 0);
  else
    until_next_command (from_tty);
}

static void
advance_command (char *arg, int from_tty)
{
  int async_exec = 0;

  if (!target_has_execution)
    error ("The program is not running.");

  if (arg == NULL)
    error_no_arg ("a location");

  /* Find out whether we must run in the background.  */
  if (arg != NULL)
    async_exec = strip_bg_char (&arg);

  /* If we must run in the background, but the target can't do it,
     error out.  */
  if (event_loop_p && async_exec && !target_can_async_p ())
    error ("Asynchronous execution not supported on this target.");

  /* If we are not asked to run in the bg, then prepare to run in the
     foreground, synchronously.  */
  if (event_loop_p && !async_exec && target_can_async_p ())
    {
      /* Simulate synchronous execution.  */
      async_disable_stdin ();
    }

  until_break_command (arg, from_tty, 1);
}


/* Print the result of a function at the end of a 'finish' command.  */

static void
print_return_value (int struct_return, struct type *value_type)
{
  struct cleanup *old_chain;
  struct ui_stream *stb;
  struct value *value;

  if (!struct_return)
    {
      /* The return value can be found in the inferior's registers.  */
      value = register_value_being_returned (value_type, stop_registers);
    }
  /* FIXME: cagney/2004-01-17: When both return_value and
     extract_returned_value_address are available, should use that to
     find the address of and then extract the returned value.  */
  /* FIXME: 2003-09-27: When returning from a nested inferior function
     call, it's possible (with no help from the architecture vector)
     to locate and return/print a "struct return" value.  This is just
     a more complicated case of what is already being done in in the
     inferior function call code.  In fact, when inferior function
     calls are made async, this will likely be made the norm.  */
  else if (gdbarch_return_value_p (current_gdbarch))
    /* We cannot determine the contents of the structure because it is
       on the stack, and we don't know where, since we did not
       initiate the call, as opposed to the call_function_by_hand
       case.  */
    {
      gdb_assert (gdbarch_return_value (current_gdbarch, value_type,
					NULL, NULL, NULL)
		  == RETURN_VALUE_STRUCT_CONVENTION);
      ui_out_text (uiout, "Value returned has type: ");
      ui_out_field_string (uiout, "return-type", TYPE_NAME (value_type));
      ui_out_text (uiout, ".");
      ui_out_text (uiout, " Cannot determine contents\n");
      return;
    }
  else
    {
      if (DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS_P ())
	{
	  CORE_ADDR addr = DEPRECATED_EXTRACT_STRUCT_VALUE_ADDRESS (stop_registers);
	  if (!addr)
	    error ("Function return value unknown.");
	  value = value_at (value_type, addr, NULL);
	}
      else
	{
	  /* It is "struct return" yet the value is being extracted,
             presumably from registers, using EXTRACT_RETURN_VALUE.
             This doesn't make sense.  Unfortunately, the legacy
             interfaces allowed this behavior.  Sigh!  */
	  value = allocate_value (value_type);
	  CHECK_TYPEDEF (value_type);
	  /* If the function returns void, don't bother fetching the
	     return value.  */
	  EXTRACT_RETURN_VALUE (value_type, stop_registers,
				VALUE_CONTENTS_RAW (value));
	}
    }

  /* Print it.  */
  stb = ui_out_stream_new (uiout);
  old_chain = make_cleanup_ui_out_stream_delete (stb);
  ui_out_text (uiout, "Value returned is ");
  ui_out_field_fmt (uiout, "gdb-result-var", "$%d",
		    record_latest_value (value));
  ui_out_text (uiout, " = ");
  value_print (value, stb->stream, 0, Val_no_prettyprint);
  ui_out_field_stream (uiout, "return-value", stb);
  ui_out_text (uiout, "\n");
  do_cleanups (old_chain);
}

/* Stuff that needs to be done by the finish command after the target
   has stopped.  In asynchronous mode, we wait for the target to stop
   in the call to poll or select in the event loop, so it is
   impossible to do all the stuff as part of the finish_command
   function itself.  The only chance we have to complete this command
   is in fetch_inferior_event, which is called by the event loop as
   soon as it detects that the target has stopped. This function is
   called via the cmd_continuation pointer.  */

static void
finish_command_continuation (struct continuation_arg *arg)
{
  struct symbol *function;
  struct breakpoint *breakpoint;
  struct cleanup *cleanups;

  breakpoint = (struct breakpoint *) arg->data.pointer;
  function = (struct symbol *) arg->next->data.pointer;
  cleanups = (struct cleanup *) arg->next->next->data.pointer;

  if (bpstat_find_breakpoint (stop_bpstat, breakpoint) != NULL
      && function != NULL)
    {
      struct type *value_type;
      int struct_return;
      int gcc_compiled;

      value_type = TYPE_TARGET_TYPE (SYMBOL_TYPE (function));
      if (!value_type)
	internal_error (__FILE__, __LINE__,
			"finish_command: function has no target type");

      if (TYPE_CODE (value_type) == TYPE_CODE_VOID)
	{
	  do_exec_cleanups (cleanups);
	  return;
	}

      CHECK_TYPEDEF (value_type);
      gcc_compiled = BLOCK_GCC_COMPILED (SYMBOL_BLOCK_VALUE (function));
      struct_return = using_struct_return (value_type, gcc_compiled);

      print_return_value (struct_return, value_type); 
    }

  do_exec_cleanups (cleanups);
}

/* "finish": Set a temporary breakpoint at the place the selected
   frame will return to, then continue.  */

static void
finish_command (char *arg, int from_tty)
{
  struct symtab_and_line sal;
  struct frame_info *frame;
  struct symbol *function;
  struct breakpoint *breakpoint;
  struct cleanup *old_chain;
  struct continuation_arg *arg1, *arg2, *arg3;

  int async_exec = 0;

  /* Find out whether we must run in the background.  */
  if (arg != NULL)
    async_exec = strip_bg_char (&arg);

  /* If we must run in the background, but the target can't do it,
     error out.  */
  if (event_loop_p && async_exec && !target_can_async_p ())
    error ("Asynchronous execution not supported on this target.");

  /* If we are not asked to run in the bg, then prepare to run in the
     foreground, synchronously.  */
  if (event_loop_p && !async_exec && target_can_async_p ())
    {
      /* Simulate synchronous execution.  */
      async_disable_stdin ();
    }

  if (arg)
    error ("The \"finish\" command does not take any arguments.");
  if (!target_has_execution)
    error ("The program is not running.");
  if (deprecated_selected_frame == NULL)
    error ("No selected frame.");

  frame = get_prev_frame (deprecated_selected_frame);
  if (frame == 0)
    error ("\"finish\" not meaningful in the outermost frame.");

  clear_proceed_status ();

  sal = find_pc_line (get_frame_pc (frame), 0);
  sal.pc = get_frame_pc (frame);

  breakpoint = set_momentary_breakpoint (sal, get_frame_id (frame), bp_finish);

  if (!event_loop_p || !target_can_async_p ())
    old_chain = make_cleanup_delete_breakpoint (breakpoint);
  else
    old_chain = make_exec_cleanup_delete_breakpoint (breakpoint);

  /* Find the function we will return from.  */

  function = find_pc_function (get_frame_pc (deprecated_selected_frame));

  /* Print info on the selected frame, including level number but not
     source.  */
  if (from_tty)
    {
      printf_filtered ("Run till exit from ");
      print_stack_frame (deprecated_selected_frame,
			 frame_relative_level (deprecated_selected_frame), 0);
    }

  /* If running asynchronously and the target support asynchronous
     execution, set things up for the rest of the finish command to be
     completed later on, when gdb has detected that the target has
     stopped, in fetch_inferior_event.  */
  if (event_loop_p && target_can_async_p ())
    {
      arg1 =
	(struct continuation_arg *) xmalloc (sizeof (struct continuation_arg));
      arg2 =
	(struct continuation_arg *) xmalloc (sizeof (struct continuation_arg));
      arg3 =
	(struct continuation_arg *) xmalloc (sizeof (struct continuation_arg));
      arg1->next = arg2;
      arg2->next = arg3;
      arg3->next = NULL;
      arg1->data.pointer = breakpoint;
      arg2->data.pointer = function;
      arg3->data.pointer = old_chain;
      add_continuation (finish_command_continuation, arg1);
    }

  proceed_to_finish = 1;	/* We want stop_registers, please...  */
  proceed ((CORE_ADDR) -1, TARGET_SIGNAL_DEFAULT, 0);

  /* Do this only if not running asynchronously or if the target
     cannot do async execution.  Otherwise, complete this command when
     the target actually stops, in fetch_inferior_event.  */
  if (!event_loop_p || !target_can_async_p ())
    {
      /* Did we stop at our breakpoint?  */
      if (bpstat_find_breakpoint (stop_bpstat, breakpoint) != NULL
	  && function != NULL)
	{
	  struct type *value_type;
	  int struct_return;
	  int gcc_compiled;

	  value_type = TYPE_TARGET_TYPE (SYMBOL_TYPE (function));
	  if (!value_type)
	    internal_error (__FILE__, __LINE__,
			    "finish_command: function has no target type");

	  /* FIXME: Shouldn't we do the cleanups before returning?  */
	  if (TYPE_CODE (value_type) == TYPE_CODE_VOID)
	    return;

	  CHECK_TYPEDEF (value_type);
	  gcc_compiled = BLOCK_GCC_COMPILED (SYMBOL_BLOCK_VALUE (function));
	  struct_return = using_struct_return (value_type, gcc_compiled);

	  print_return_value (struct_return, value_type); 
	}

      do_cleanups (old_chain);
    }
}


static void
program_info (char *args, int from_tty)
{
  bpstat bs = stop_bpstat;
  int num = bpstat_num (&bs);

  if (!target_has_execution)
    {
      printf_filtered ("The program being debugged is not being run.\n");
      return;
    }

  target_files_info ();
  printf_filtered ("Program stopped at %s.\n",
		   local_hex_string ((unsigned long) stop_pc));
  if (stop_step)
    printf_filtered ("It stopped after being stepped.\n");
  else if (num != 0)
    {
      /* There may be several breakpoints in the same place, so this
         isn't as strange as it seems.  */
      while (num != 0)
	{
	  if (num < 0)
	    {
	      printf_filtered ("It stopped at a breakpoint that has ");
	      printf_filtered ("since been deleted.\n");
	    }
	  else
	    printf_filtered ("It stopped at breakpoint %d.\n", num);
	  num = bpstat_num (&bs);
	}
    }
  else if (stop_signal != TARGET_SIGNAL_0)
    {
      printf_filtered ("It stopped with signal %s, %s.\n",
		       target_signal_to_name (stop_signal),
		       target_signal_to_string (stop_signal));
    }

  if (!from_tty)
    {
      printf_filtered ("Type \"info stack\" or \"info registers\" ");
      printf_filtered ("for more information.\n");
    }
}

static void
environment_info (char *var, int from_tty)
{
  if (var)
    {
      char *val = get_in_environ (inferior_environ, var);
      if (val)
	{
	  puts_filtered (var);
	  puts_filtered (" = ");
	  puts_filtered (val);
	  puts_filtered ("\n");
	}
      else
	{
	  puts_filtered ("Environment variable \"");
	  puts_filtered (var);
	  puts_filtered ("\" not defined.\n");
	}
    }
  else
    {
      char **vector = environ_vector (inferior_environ);
      while (*vector)
	{
	  puts_filtered (*vector++);
	  puts_filtered ("\n");
	}
    }
}

static void
set_environment_command (char *arg, int from_tty)
{
  char *p, *val, *var;
  int nullset = 0;

  if (arg == 0)
    error_no_arg ("environment variable and value");

  /* Find seperation between variable name and value */
  p = (char *) strchr (arg, '=');
  val = (char *) strchr (arg, ' ');

  if (p != 0 && val != 0)
    {
      /* We have both a space and an equals.  If the space is before the
         equals, walk forward over the spaces til we see a nonspace 
         (possibly the equals). */
      if (p > val)
	while (*val == ' ')
	  val++;

      /* Now if the = is after the char following the spaces,
         take the char following the spaces.  */
      if (p > val)
	p = val - 1;
    }
  else if (val != 0 && p == 0)
    p = val;

  if (p == arg)
    error_no_arg ("environment variable to set");

  if (p == 0 || p[1] == 0)
    {
      nullset = 1;
      if (p == 0)
	p = arg + strlen (arg);	/* So that savestring below will work */
    }
  else
    {
      /* Not setting variable value to null */
      val = p + 1;
      while (*val == ' ' || *val == '\t')
	val++;
    }

  while (p != arg && (p[-1] == ' ' || p[-1] == '\t'))
    p--;

  var = savestring (arg, p - arg);
  if (nullset)
    {
      printf_filtered ("Setting environment variable ");
      printf_filtered ("\"%s\" to null value.\n", var);
      set_in_environ (inferior_environ, var, "");
    }
  else
    set_in_environ (inferior_environ, var, val);
  xfree (var);
}

static void
unset_environment_command (char *var, int from_tty)
{
  if (var == 0)
    {
      /* If there is no argument, delete all environment variables.
         Ask for confirmation if reading from the terminal.  */
      if (!from_tty || query ("Delete all environment variables? "))
	{
	  free_environ (inferior_environ);
	  inferior_environ = make_environ ();
	}
    }
  else
    unset_in_environ (inferior_environ, var);
}

/* Handle the execution path (PATH variable) */

static const char path_var_name[] = "PATH";

static void
path_info (char *args, int from_tty)
{
  puts_filtered ("Executable and object file path: ");
  puts_filtered (get_in_environ (inferior_environ, path_var_name));
  puts_filtered ("\n");
}

/* Add zero or more directories to the front of the execution path.  */

static void
path_command (char *dirname, int from_tty)
{
  char *exec_path;
  char *env;
  dont_repeat ();
  env = get_in_environ (inferior_environ, path_var_name);
  /* Can be null if path is not set */
  if (!env)
    env = "";
  exec_path = xstrdup (env);
  mod_path (dirname, &exec_path);
  set_in_environ (inferior_environ, path_var_name, exec_path);
  xfree (exec_path);
  if (from_tty)
    path_info ((char *) NULL, from_tty);
}


/* Print out the machine register regnum. If regnum is -1, print all
   registers (print_all == 1) or all non-float and non-vector
   registers (print_all == 0).

   For most machines, having all_registers_info() print the
   register(s) one per line is good enough.  If a different format is
   required, (eg, for MIPS or Pyramid 90x, which both have lots of
   regs), or there is an existing convention for showing all the
   registers, define the architecture method PRINT_REGISTERS_INFO to
   provide that format.  */

void
default_print_registers_info (struct gdbarch *gdbarch,
			      struct ui_file *file,
			      struct frame_info *frame,
			      int regnum, int print_all)
{
  int i;
  const int numregs = NUM_REGS + NUM_PSEUDO_REGS;
  char raw_buffer[MAX_REGISTER_SIZE];
  char virtual_buffer[MAX_REGISTER_SIZE];

  if (DEPRECATED_DO_REGISTERS_INFO_P ())
    {
      DEPRECATED_DO_REGISTERS_INFO (regnum, print_all);
      return;
    }

  for (i = 0; i < numregs; i++)
    {
      /* Decide between printing all regs, non-float / vector regs, or
         specific reg.  */
      if (regnum == -1)
	{
	  if (print_all)
	    {
	      if (!gdbarch_register_reggroup_p (gdbarch, i, all_reggroup))
		continue;
	    }
	  else
	    {
	      if (!gdbarch_register_reggroup_p (gdbarch, i, general_reggroup))
		continue;
	    }
	}
      else
	{
	  if (i != regnum)
	    continue;
	}

      /* If the register name is empty, it is undefined for this
         processor, so don't display anything.  */
      if (REGISTER_NAME (i) == NULL || *(REGISTER_NAME (i)) == '\0')
	continue;

      fputs_filtered (REGISTER_NAME (i), file);
      print_spaces_filtered (15 - strlen (REGISTER_NAME (i)), file);

      /* Get the data in raw format.  */
      if (! frame_register_read (frame, i, raw_buffer))
	{
	  fprintf_filtered (file, "*value not available*\n");
	  continue;
	}

      /* FIXME: cagney/2002-08-03: This code shouldn't be necessary.
         The function frame_register_read() should have returned the
         pre-cooked register so no conversion is necessary.  */
      /* Convert raw data to virtual format if necessary.  */
      if (DEPRECATED_REGISTER_CONVERTIBLE_P ()
	  && DEPRECATED_REGISTER_CONVERTIBLE (i))
	{
	  DEPRECATED_REGISTER_CONVERT_TO_VIRTUAL (i, register_type (current_gdbarch, i),
				       raw_buffer, virtual_buffer);
	}
      else
	{
	  memcpy (virtual_buffer, raw_buffer,
		  DEPRECATED_REGISTER_VIRTUAL_SIZE (i));
	}

      /* If virtual format is floating, print it that way, and in raw
         hex.  */
      if (TYPE_CODE (register_type (current_gdbarch, i)) == TYPE_CODE_FLT)
	{
	  int j;

	  val_print (register_type (current_gdbarch, i), virtual_buffer, 0, 0,
		     file, 0, 1, 0, Val_pretty_default);

	  fprintf_filtered (file, "\t(raw 0x");
	  for (j = 0; j < DEPRECATED_REGISTER_RAW_SIZE (i); j++)
	    {
	      int idx;
	      if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
		idx = j;
	      else
		idx = DEPRECATED_REGISTER_RAW_SIZE (i) - 1 - j;
	      fprintf_filtered (file, "%02x", (unsigned char) raw_buffer[idx]);
	    }
	  fprintf_filtered (file, ")");
	}
      else
	{
	  /* Print the register in hex.  */
	  val_print (register_type (current_gdbarch, i), virtual_buffer, 0, 0,
		     file, 'x', 1, 0, Val_pretty_default);
          /* If not a vector register, print it also according to its
             natural format.  */
	  if (TYPE_VECTOR (register_type (current_gdbarch, i)) == 0)
	    {
	      fprintf_filtered (file, "\t");
	      val_print (register_type (current_gdbarch, i), virtual_buffer, 0, 0,
			 file, 0, 1, 0, Val_pretty_default);
	    }
	}

      fprintf_filtered (file, "\n");
    }
}

void
registers_info (char *addr_exp, int fpregs)
{
  int regnum, numregs;
  char *end;

  if (!target_has_registers)
    error ("The program has no registers now.");
  if (deprecated_selected_frame == NULL)
    error ("No selected frame.");

  if (!addr_exp)
    {
      gdbarch_print_registers_info (current_gdbarch, gdb_stdout,
				    deprecated_selected_frame, -1, fpregs);
      return;
    }

  while (*addr_exp != '\0')
    {
      char *start;
      const char *end;

      /* Keep skipping leading white space.  */
      if (isspace ((*addr_exp)))
	{
	  addr_exp++;
	  continue;
	}

      /* Discard any leading ``$''.  Check that there is something
         resembling a register following it.  */
      if (addr_exp[0] == '$')
	addr_exp++;
      if (isspace ((*addr_exp)) || (*addr_exp) == '\0')
	error ("Missing register name");

      /* Find the start/end of this register name/num/group.  */
      start = addr_exp;
      while ((*addr_exp) != '\0' && !isspace ((*addr_exp)))
	addr_exp++;
      end = addr_exp;
      
      /* Figure out what we've found and display it.  */

      /* A register name?  */
      {
	int regnum = frame_map_name_to_regnum (deprecated_selected_frame,
					       start, end - start);
	if (regnum >= 0)
	  {
	    gdbarch_print_registers_info (current_gdbarch, gdb_stdout,
					  deprecated_selected_frame, regnum, fpregs);
	    continue;
	  }
      }
	
      /* A register number?  (how portable is this one?).  */
      {
	char *endptr;
	int regnum = strtol (start, &endptr, 0);
	if (endptr == end
	    && regnum >= 0
	    && regnum < NUM_REGS + NUM_PSEUDO_REGS)
	  {
	    gdbarch_print_registers_info (current_gdbarch, gdb_stdout,
					  deprecated_selected_frame, regnum, fpregs);
	    continue;
	  }
      }

      /* A register group?  */
      {
	struct reggroup *group;
	for (group = reggroup_next (current_gdbarch, NULL);
	     group != NULL;
	     group = reggroup_next (current_gdbarch, group))
	  {
	    /* Don't bother with a length check.  Should the user
	       enter a short register group name, go with the first
	       group that matches.  */
	    if (strncmp (start, reggroup_name (group), end - start) == 0)
	      break;
	  }
	if (group != NULL)
	  {
	    int regnum;
	    for (regnum = 0; regnum < NUM_REGS + NUM_PSEUDO_REGS; regnum++)
	      {
		if (gdbarch_register_reggroup_p (current_gdbarch, regnum,
						 group))
		  gdbarch_print_registers_info (current_gdbarch,
						gdb_stdout, deprecated_selected_frame,
						regnum, fpregs);
	      }
	    continue;
	  }
      }

      /* Nothing matched.  */
      error ("Invalid register `%.*s'", (int) (end - start), start);
    }
}

void
all_registers_info (char *addr_exp, int from_tty)
{
  registers_info (addr_exp, 1);
}

static void
nofp_registers_info (char *addr_exp, int from_tty)
{
  registers_info (addr_exp, 0);
}

static void
print_vector_info (struct gdbarch *gdbarch, struct ui_file *file,
		   struct frame_info *frame, const char *args)
{
  if (!target_has_registers)
    error ("The program has no registers now.");
  if (deprecated_selected_frame == NULL)
    error ("No selected frame.");

  if (gdbarch_print_vector_info_p (gdbarch))
    gdbarch_print_vector_info (gdbarch, file, frame, args);
  else
    {
      int regnum;
      int printed_something = 0;

      for (regnum = 0; regnum < NUM_REGS + NUM_PSEUDO_REGS; regnum++)
	{
	  if (gdbarch_register_reggroup_p (gdbarch, regnum, vector_reggroup))
	    {
	      printed_something = 1;
	      gdbarch_print_registers_info (gdbarch, file, frame, regnum, 1);
	    }
	}
      if (!printed_something)
	fprintf_filtered (file, "No vector information\n");
    }
}

static void
vector_info (char *args, int from_tty)
{
  print_vector_info (current_gdbarch, gdb_stdout, deprecated_selected_frame, args);
}


/*
 * TODO:
 * Should save/restore the tty state since it might be that the
 * program to be debugged was started on this tty and it wants
 * the tty in some state other than what we want.  If it's running
 * on another terminal or without a terminal, then saving and
 * restoring the tty state is a harmless no-op.
 * This only needs to be done if we are attaching to a process.
 */

/*
   attach_command --
   takes a program started up outside of gdb and ``attaches'' to it.
   This stops it cold in its tracks and allows us to start debugging it.
   and wait for the trace-trap that results from attaching.  */

void
attach_command (char *args, int from_tty)
{
  char *exec_file;
  char *full_exec_path = NULL;

  dont_repeat ();		/* Not for the faint of heart */

  if (target_has_execution)
    {
      if (query ("A program is being debugged already.  Kill it? "))
	target_kill ();
      else
	error ("Not killed.");
    }

  target_attach (args, from_tty);

  /* Set up the "saved terminal modes" of the inferior
     based on what modes we are starting it with.  */
  target_terminal_init ();

  /* Install inferior's terminal modes.  */
  target_terminal_inferior ();

  /* Set up execution context to know that we should return from
     wait_for_inferior as soon as the target reports a stop.  */
  init_wait_for_inferior ();
  clear_proceed_status ();

  /* No traps are generated when attaching to inferior under Mach 3
     or GNU hurd.  */
#ifndef ATTACH_NO_WAIT
  /* Careful here. See comments in inferior.h.  Basically some OSes
     don't ignore SIGSTOPs on continue requests anymore.  We need a
     way for handle_inferior_event to reset the stop_signal variable
     after an attach, and this is what STOP_QUIETLY_NO_SIGSTOP is for.  */
  stop_soon = STOP_QUIETLY_NO_SIGSTOP;
  wait_for_inferior ();
  stop_soon = NO_STOP_QUIETLY;
#endif

  /*
   * If no exec file is yet known, try to determine it from the
   * process itself.
   */
  exec_file = (char *) get_exec_file (0);
  if (!exec_file)
    {
      exec_file = target_pid_to_exec_file (PIDGET (inferior_ptid));
      if (exec_file)
	{
	  /* It's possible we don't have a full path, but rather just a
	     filename.  Some targets, such as HP-UX, don't provide the
	     full path, sigh.

	     Attempt to qualify the filename against the source path.
	     (If that fails, we'll just fall back on the original
	     filename.  Not much more we can do...)
	   */
	  if (!source_full_path_of (exec_file, &full_exec_path))
	    full_exec_path = savestring (exec_file, strlen (exec_file));

	  exec_file_attach (full_exec_path, from_tty);
	  symbol_file_add_main (full_exec_path, from_tty);
	}
    }

#ifdef SOLIB_ADD
  /* Add shared library symbols from the newly attached process, if any.  */
  SOLIB_ADD ((char *) 0, from_tty, &current_target, auto_solib_add);
  re_enable_breakpoints_in_shlibs ();
#endif

  /* Take any necessary post-attaching actions for this platform.
   */
  target_post_attach (PIDGET (inferior_ptid));

  normal_stop ();

  if (attach_hook)
    attach_hook ();
}

/*
 * detach_command --
 * takes a program previously attached to and detaches it.
 * The program resumes execution and will no longer stop
 * on signals, etc.  We better not have left any breakpoints
 * in the program or it'll die when it hits one.  For this
 * to work, it may be necessary for the process to have been
 * previously attached.  It *might* work if the program was
 * started via the normal ptrace (PTRACE_TRACEME).
 */

static void
detach_command (char *args, int from_tty)
{
  dont_repeat ();		/* Not for the faint of heart */
  target_detach (args, from_tty);
#if defined(SOLIB_RESTART)
  SOLIB_RESTART ();
#endif
  if (detach_hook)
    detach_hook ();
}

/* Disconnect from the current target without resuming it (leaving it
   waiting for a debugger).

   We'd better not have left any breakpoints in the program or the
   next debugger will get confused.  Currently only supported for some
   remote targets, since the normal attach mechanisms don't work on
   stopped processes on some native platforms (e.g. GNU/Linux).  */

static void
disconnect_command (char *args, int from_tty)
{
  dont_repeat ();		/* Not for the faint of heart */
  target_disconnect (args, from_tty);
#if defined(SOLIB_RESTART)
  SOLIB_RESTART ();
#endif
  if (detach_hook)
    detach_hook ();
}

/* Stop the execution of the target while running in async mode, in
   the backgound. */
void
interrupt_target_command (char *args, int from_tty)
{
  if (event_loop_p && target_can_async_p ())
    {
      dont_repeat ();		/* Not for the faint of heart */
      target_stop ();
    }
}

static void
print_float_info (struct gdbarch *gdbarch, struct ui_file *file,
		  struct frame_info *frame, const char *args)
{
  if (!target_has_registers)
    error ("The program has no registers now.");
  if (deprecated_selected_frame == NULL)
    error ("No selected frame.");

  if (gdbarch_print_float_info_p (gdbarch))
    gdbarch_print_float_info (gdbarch, file, frame, args);
  else
    {
      int regnum;
      int printed_something = 0;

      for (regnum = 0; regnum < NUM_REGS + NUM_PSEUDO_REGS; regnum++)
	{
	  if (gdbarch_register_reggroup_p (gdbarch, regnum, float_reggroup))
	    {
	      printed_something = 1;
	      gdbarch_print_registers_info (gdbarch, file, frame, regnum, 1);
	    }
	}
      if (!printed_something)
	fprintf_filtered (file, "\
No floating-point info available for this processor.\n");
    }
}

static void
float_info (char *args, int from_tty)
{
  print_float_info (current_gdbarch, gdb_stdout, deprecated_selected_frame, args);
}

static void
unset_command (char *args, int from_tty)
{
  printf_filtered ("\"unset\" must be followed by the name of ");
  printf_filtered ("an unset subcommand.\n");
  help_list (unsetlist, "unset ", -1, gdb_stdout);
}

void
_initialize_infcmd (void)
{
  struct cmd_list_element *c;

  c = add_com ("tty", class_run, tty_command,
	       "Set terminal for future runs of program being debugged.");
  set_cmd_completer (c, filename_completer);

  c = add_set_cmd ("args", class_run, var_string_noescape,
		   (char *) &inferior_args,
		   "Set argument list to give program being debugged when it is started.\n\
Follow this command with any number of args, to be passed to the program.",
		   &setlist);
  set_cmd_completer (c, filename_completer);
  set_cmd_sfunc (c, notice_args_set);
  c = add_show_from_set (c, &showlist);
  set_cmd_sfunc (c, notice_args_read);

  c = add_cmd
    ("environment", no_class, environment_info,
     "The environment to give the program, or one variable's value.\n\
With an argument VAR, prints the value of environment variable VAR to\n\
give the program being debugged.  With no arguments, prints the entire\n\
environment to be given to the program.", &showlist);
  set_cmd_completer (c, noop_completer);

  add_prefix_cmd ("unset", no_class, unset_command,
		  "Complement to certain \"set\" commands.",
		  &unsetlist, "unset ", 0, &cmdlist);

  c = add_cmd ("environment", class_run, unset_environment_command,
	       "Cancel environment variable VAR for the program.\n\
This does not affect the program until the next \"run\" command.",
	       &unsetlist);
  set_cmd_completer (c, noop_completer);

  c = add_cmd ("environment", class_run, set_environment_command,
	       "Set environment variable value to give the program.\n\
Arguments are VAR VALUE where VAR is variable name and VALUE is value.\n\
VALUES of environment variables are uninterpreted strings.\n\
This does not affect the program until the next \"run\" command.",
	       &setlist);
  set_cmd_completer (c, noop_completer);

  c = add_com ("path", class_files, path_command,
	       "Add directory DIR(s) to beginning of search path for object files.\n\
$cwd in the path means the current working directory.\n\
This path is equivalent to the $PATH shell variable.  It is a list of\n\
directories, separated by colons.  These directories are searched to find\n\
fully linked executable files and separately compiled object files as needed.");
  set_cmd_completer (c, filename_completer);

  c = add_cmd ("paths", no_class, path_info,
	       "Current search path for finding object files.\n\
$cwd in the path means the current working directory.\n\
This path is equivalent to the $PATH shell variable.  It is a list of\n\
directories, separated by colons.  These directories are searched to find\n\
fully linked executable files and separately compiled object files as needed.",
	       &showlist);
  set_cmd_completer (c, noop_completer);

  add_com ("attach", class_run, attach_command,
	   "Attach to a process or file outside of GDB.\n\
This command attaches to another target, of the same type as your last\n\
\"target\" command (\"info files\" will show your target stack).\n\
The command may take as argument a process id or a device file.\n\
For a process id, you must have permission to send the process a signal,\n\
and it must have the same effective uid as the debugger.\n\
When using \"attach\" with a process id, the debugger finds the\n\
program running in the process, looking first in the current working\n\
directory, or (if not found there) using the source file search path\n\
(see the \"directory\" command).  You can also use the \"file\" command\n\
to specify the program, and to load its symbol table.");

  add_com ("detach", class_run, detach_command,
	   "Detach a process or file previously attached.\n\
If a process, it is no longer traced, and it continues its execution.  If\n\
you were debugging a file, the file is closed and gdb no longer accesses it.");

  add_com ("disconnect", class_run, disconnect_command,
	   "Disconnect from a target.\n\
The target will wait for another debugger to connect.  Not available for\n\
all targets.");

  add_com ("signal", class_run, signal_command,
	   "Continue program giving it signal specified by the argument.\n\
An argument of \"0\" means continue program without giving it a signal.");

  add_com ("stepi", class_run, stepi_command,
	   "Step one instruction exactly.\n\
Argument N means do this N times (or till program stops for another reason).");
  add_com_alias ("si", "stepi", class_alias, 0);

  add_com ("nexti", class_run, nexti_command,
	   "Step one instruction, but proceed through subroutine calls.\n\
Argument N means do this N times (or till program stops for another reason).");
  add_com_alias ("ni", "nexti", class_alias, 0);

  add_com ("finish", class_run, finish_command,
	   "Execute until selected stack frame returns.\n\
Upon return, the value returned is printed and put in the value history.");

  add_com ("next", class_run, next_command,
	   "Step program, proceeding through subroutine calls.\n\
Like the \"step\" command as long as subroutine calls do not happen;\n\
when they do, the call is treated as one instruction.\n\
Argument N means do this N times (or till program stops for another reason).");
  add_com_alias ("n", "next", class_run, 1);
  if (xdb_commands)
    add_com_alias ("S", "next", class_run, 1);

  add_com ("step", class_run, step_command,
	   "Step program until it reaches a different source line.\n\
Argument N means do this N times (or till program stops for another reason).");
  add_com_alias ("s", "step", class_run, 1);

  c = add_com ("until", class_run, until_command,
	       "Execute until the program reaches a source line greater than the current\n\
or a specified location (same args as break command) within the current frame.");
  set_cmd_completer (c, location_completer);
  add_com_alias ("u", "until", class_run, 1);

  c = add_com ("advance", class_run, advance_command,
	       "Continue the program up to the given location (same form as args for break command).\n\
Execution will also stop upon exit from the current stack frame.");
  set_cmd_completer (c, location_completer);

  c = add_com ("jump", class_run, jump_command,
	       "Continue program being debugged at specified line or address.\n\
Give as argument either LINENUM or *ADDR, where ADDR is an expression\n\
for an address to start at.");
  set_cmd_completer (c, location_completer);

  if (xdb_commands)
    {
      c = add_com ("go", class_run, go_command,
		   "Usage: go <location>\n\
Continue program being debugged, stopping at specified line or \n\
address.\n\
Give as argument either LINENUM or *ADDR, where ADDR is an \n\
expression for an address to start at.\n\
This command is a combination of tbreak and jump.");
      set_cmd_completer (c, location_completer);
    }

  if (xdb_commands)
    add_com_alias ("g", "go", class_run, 1);

  add_com ("continue", class_run, continue_command,
	   "Continue program being debugged, after signal or breakpoint.\n\
If proceeding from breakpoint, a number N may be used as an argument,\n\
which means to set the ignore count of that breakpoint to N - 1 (so that\n\
the breakpoint won't break until the Nth time it is reached).");
  add_com_alias ("c", "cont", class_run, 1);
  add_com_alias ("fg", "cont", class_run, 1);

  c = add_com ("run", class_run, run_command,
	   "Start debugged program.  You may specify arguments to give it.\n\
Args may include \"*\", or \"[...]\"; they are expanded using \"sh\".\n\
Input and output redirection with \">\", \"<\", or \">>\" are also allowed.\n\n\
With no arguments, uses arguments last specified (with \"run\" or \"set args\").\n\
To cancel previous arguments and run with no arguments,\n\
use \"set args\" without arguments.");
  set_cmd_completer (c, filename_completer);
  add_com_alias ("r", "run", class_run, 1);
  if (xdb_commands)
    add_com ("R", class_run, run_no_args_command,
	     "Start debugged program with no arguments.");

  add_com ("interrupt", class_run, interrupt_target_command,
	   "Interrupt the execution of the debugged program.");

  add_info ("registers", nofp_registers_info,
	    "List of integer registers and their contents, for selected stack frame.\n\
Register name as argument means describe only that register.");
  add_info_alias ("r", "registers", 1);

  if (xdb_commands)
    add_com ("lr", class_info, nofp_registers_info,
	     "List of integer registers and their contents, for selected stack frame.\n\
  Register name as argument means describe only that register.");
  add_info ("all-registers", all_registers_info,
	    "List of all registers and their contents, for selected stack frame.\n\
Register name as argument means describe only that register.");

  add_info ("program", program_info,
	    "Execution status of the program.");

  add_info ("float", float_info,
	    "Print the status of the floating point unit\n");

  add_info ("vector", vector_info,
	    "Print the status of the vector unit\n");

  inferior_environ = make_environ ();
  init_environ (inferior_environ);
}

/* Tracing functionality for remote targets in custom GDB protocol

   Copyright 1997, 1998, 1999, 2000, 2001, 2002, 2003 Free Software
   Foundation, Inc.

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
#include "symtab.h"
#include "frame.h"
#include "gdbtypes.h"
#include "expression.h"
#include "gdbcmd.h"
#include "value.h"
#include "target.h"
#include "language.h"
#include "gdb_string.h"
#include "inferior.h"
#include "tracepoint.h"
#include "remote.h"
#include "linespec.h"
#include "regcache.h"
#include "completer.h"
#include "gdb-events.h"
#include "block.h"
#include "dictionary.h"

#include "ax.h"
#include "ax-gdb.h"

/* readline include files */
#include "readline/readline.h"
#include "readline/history.h"

/* readline defines this.  */
#undef savestring

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* maximum length of an agent aexpression.
   this accounts for the fact that packets are limited to 400 bytes
   (which includes everything -- including the checksum), and assumes
   the worst case of maximum length for each of the pieces of a
   continuation packet.

   NOTE: expressions get mem2hex'ed otherwise this would be twice as
   large.  (400 - 31)/2 == 184 */
#define MAX_AGENT_EXPR_LEN	184


extern void (*readline_begin_hook) (char *, ...);
extern char *(*readline_hook) (char *);
extern void (*readline_end_hook) (void);
extern void x_command (char *, int);
extern int addressprint;	/* Print machine addresses? */

/* GDB commands implemented in other modules:
 */  

extern void output_command (char *, int);

/* 
   Tracepoint.c:

   This module defines the following debugger commands:
   trace            : set a tracepoint on a function, line, or address.
   info trace       : list all debugger-defined tracepoints.
   delete trace     : delete one or more tracepoints.
   enable trace     : enable one or more tracepoints.
   disable trace    : disable one or more tracepoints.
   actions          : specify actions to be taken at a tracepoint.
   passcount        : specify a pass count for a tracepoint.
   tstart           : start a trace experiment.
   tstop            : stop a trace experiment.
   tstatus          : query the status of a trace experiment.
   tfind            : find a trace frame in the trace buffer.
   tdump            : print everything collected at the current tracepoint.
   save-tracepoints : write tracepoint setup into a file.

   This module defines the following user-visible debugger variables:
   $trace_frame : sequence number of trace frame currently being debugged.
   $trace_line  : source line of trace frame currently being debugged.
   $trace_file  : source file of trace frame currently being debugged.
   $tracepoint  : tracepoint number of trace frame currently being debugged.
 */


/* ======= Important global variables: ======= */

/* Chain of all tracepoints defined.  */
struct tracepoint *tracepoint_chain;

/* Number of last tracepoint made.  */
static int tracepoint_count;

/* Number of last traceframe collected.  */
static int traceframe_number;

/* Tracepoint for last traceframe collected.  */
static int tracepoint_number;

/* Symbol for function for last traceframe collected */
static struct symbol *traceframe_fun;

/* Symtab and line for last traceframe collected */
static struct symtab_and_line traceframe_sal;

/* Tracing command lists */
static struct cmd_list_element *tfindlist;

/* ======= Important command functions: ======= */
static void trace_command (char *, int);
static void tracepoints_info (char *, int);
static void delete_trace_command (char *, int);
static void enable_trace_command (char *, int);
static void disable_trace_command (char *, int);
static void trace_pass_command (char *, int);
static void trace_actions_command (char *, int);
static void trace_start_command (char *, int);
static void trace_stop_command (char *, int);
static void trace_status_command (char *, int);
static void trace_find_command (char *, int);
static void trace_find_pc_command (char *, int);
static void trace_find_tracepoint_command (char *, int);
static void trace_find_line_command (char *, int);
static void trace_find_range_command (char *, int);
static void trace_find_outside_command (char *, int);
static void tracepoint_save_command (char *, int);
static void trace_dump_command (char *, int);

/* support routines */
static void trace_mention (struct tracepoint *);

struct collection_list;
static void add_aexpr (struct collection_list *, struct agent_expr *);
static unsigned char *mem2hex (unsigned char *, unsigned char *, int);
static void add_register (struct collection_list *collection,
			  unsigned int regno);
static struct cleanup *make_cleanup_free_actions (struct tracepoint *t);
static void free_actions_list (char **actions_list);
static void free_actions_list_cleanup_wrapper (void *);

extern void _initialize_tracepoint (void);

/* Utility: returns true if "target remote" */
static int
target_is_remote (void)
{
  if (current_target.to_shortname &&
      strcmp (current_target.to_shortname, "remote") == 0)
    return 1;
  else
    return 0;
}

/* Utility: generate error from an incoming stub packet.  */
static void
trace_error (char *buf)
{
  if (*buf++ != 'E')
    return;			/* not an error msg */
  switch (*buf)
    {
    case '1':			/* malformed packet error */
      if (*++buf == '0')	/*   general case: */
	error ("tracepoint.c: error in outgoing packet.");
      else
	error ("tracepoint.c: error in outgoing packet at field #%ld.",
	       strtol (buf, NULL, 16));
    case '2':
      error ("trace API error 0x%s.", ++buf);
    default:
      error ("Target returns error code '%s'.", buf);
    }
}

/* Utility: wait for reply from stub, while accepting "O" packets */
static char *
remote_get_noisy_reply (char *buf,
			long sizeof_buf)
{
  do				/* loop on reply from remote stub */
    {
      QUIT;			/* allow user to bail out with ^C */
      getpkt (buf, sizeof_buf, 0);
      if (buf[0] == 0)
	error ("Target does not support this command.");
      else if (buf[0] == 'E')
	trace_error (buf);
      else if (buf[0] == 'O' &&
	       buf[1] != 'K')
	remote_console_output (buf + 1);	/* 'O' message from stub */
      else
	return buf;		/* here's the actual reply */
    }
  while (1);
}

/* Set tracepoint count to NUM.  */
static void
set_tracepoint_count (int num)
{
  tracepoint_count = num;
  set_internalvar (lookup_internalvar ("tpnum"),
		   value_from_longest (builtin_type_int, (LONGEST) num));
}

/* Set traceframe number to NUM.  */
static void
set_traceframe_num (int num)
{
  traceframe_number = num;
  set_internalvar (lookup_internalvar ("trace_frame"),
		   value_from_longest (builtin_type_int, (LONGEST) num));
}

/* Set tracepoint number to NUM.  */
static void
set_tracepoint_num (int num)
{
  tracepoint_number = num;
  set_internalvar (lookup_internalvar ("tracepoint"),
		   value_from_longest (builtin_type_int, (LONGEST) num));
}

/* Set externally visible debug variables for querying/printing
   the traceframe context (line, function, file) */

static void
set_traceframe_context (CORE_ADDR trace_pc)
{
  static struct type *func_string, *file_string;
  static struct type *func_range, *file_range;
  struct value *func_val;
  struct value *file_val;
  static struct type *charstar;
  int len;

  if (charstar == (struct type *) NULL)
    charstar = lookup_pointer_type (builtin_type_char);

  if (trace_pc == -1)		/* cease debugging any trace buffers */
    {
      traceframe_fun = 0;
      traceframe_sal.pc = traceframe_sal.line = 0;
      traceframe_sal.symtab = NULL;
      set_internalvar (lookup_internalvar ("trace_func"),
		       value_from_pointer (charstar, (LONGEST) 0));
      set_internalvar (lookup_internalvar ("trace_file"),
		       value_from_pointer (charstar, (LONGEST) 0));
      set_internalvar (lookup_internalvar ("trace_line"),
		       value_from_longest (builtin_type_int, (LONGEST) - 1));
      return;
    }

  /* save as globals for internal use */
  traceframe_sal = find_pc_line (trace_pc, 0);
  traceframe_fun = find_pc_function (trace_pc);

  /* save linenumber as "$trace_line", a debugger variable visible to users */
  set_internalvar (lookup_internalvar ("trace_line"),
		   value_from_longest (builtin_type_int,
				       (LONGEST) traceframe_sal.line));

  /* save func name as "$trace_func", a debugger variable visible to users */
  if (traceframe_fun == NULL ||
      DEPRECATED_SYMBOL_NAME (traceframe_fun) == NULL)
    set_internalvar (lookup_internalvar ("trace_func"),
		     value_from_pointer (charstar, (LONGEST) 0));
  else
    {
      len = strlen (DEPRECATED_SYMBOL_NAME (traceframe_fun));
      func_range = create_range_type (func_range,
				      builtin_type_int, 0, len - 1);
      func_string = create_array_type (func_string,
				       builtin_type_char, func_range);
      func_val = allocate_value (func_string);
      VALUE_TYPE (func_val) = func_string;
      memcpy (VALUE_CONTENTS_RAW (func_val),
	      DEPRECATED_SYMBOL_NAME (traceframe_fun),
	      len);
      func_val->modifiable = 0;
      set_internalvar (lookup_internalvar ("trace_func"), func_val);
    }

  /* save file name as "$trace_file", a debugger variable visible to users */
  if (traceframe_sal.symtab == NULL ||
      traceframe_sal.symtab->filename == NULL)
    set_internalvar (lookup_internalvar ("trace_file"),
		     value_from_pointer (charstar, (LONGEST) 0));
  else
    {
      len = strlen (traceframe_sal.symtab->filename);
      file_range = create_range_type (file_range,
				      builtin_type_int, 0, len - 1);
      file_string = create_array_type (file_string,
				       builtin_type_char, file_range);
      file_val = allocate_value (file_string);
      VALUE_TYPE (file_val) = file_string;
      memcpy (VALUE_CONTENTS_RAW (file_val),
	      traceframe_sal.symtab->filename,
	      len);
      file_val->modifiable = 0;
      set_internalvar (lookup_internalvar ("trace_file"), file_val);
    }
}

/* Low level routine to set a tracepoint.
   Returns the tracepoint object so caller can set other things.
   Does not set the tracepoint number!
   Does not print anything.

   ==> This routine should not be called if there is a chance of later
   error(); otherwise it leaves a bogus tracepoint on the chain.  Validate
   your arguments BEFORE calling this routine!  */

static struct tracepoint *
set_raw_tracepoint (struct symtab_and_line sal)
{
  struct tracepoint *t, *tc;
  struct cleanup *old_chain;

  t = (struct tracepoint *) xmalloc (sizeof (struct tracepoint));
  old_chain = make_cleanup (xfree, t);
  memset (t, 0, sizeof (*t));
  t->address = sal.pc;
  if (sal.symtab == NULL)
    t->source_file = NULL;
  else
    t->source_file = savestring (sal.symtab->filename,
				 strlen (sal.symtab->filename));

  t->section = sal.section;
  t->language = current_language->la_language;
  t->input_radix = input_radix;
  t->line_number = sal.line;
  t->enabled_p = 1;
  t->next = 0;
  t->step_count = 0;
  t->pass_count = 0;
  t->addr_string = NULL;

  /* Add this tracepoint to the end of the chain
     so that a list of tracepoints will come out in order
     of increasing numbers.  */

  tc = tracepoint_chain;
  if (tc == 0)
    tracepoint_chain = t;
  else
    {
      while (tc->next)
	tc = tc->next;
      tc->next = t;
    }
  discard_cleanups (old_chain);
  return t;
}

/* Set a tracepoint according to ARG (function, linenum or *address) */
static void
trace_command (char *arg, int from_tty)
{
  char **canonical = (char **) NULL;
  struct symtabs_and_lines sals;
  struct symtab_and_line sal;
  struct tracepoint *t;
  char *addr_start = 0, *addr_end = 0;
  int i;

  if (!arg || !*arg)
    error ("trace command requires an argument");

  if (from_tty && info_verbose)
    printf_filtered ("TRACE %s\n", arg);

  addr_start = arg;
  sals = decode_line_1 (&arg, 1, (struct symtab *) NULL, 0, &canonical, NULL);
  addr_end = arg;
  if (!sals.nelts)
    return;			/* ??? Presumably decode_line_1 has already warned? */

  /* Resolve all line numbers to PC's */
  for (i = 0; i < sals.nelts; i++)
    resolve_sal_pc (&sals.sals[i]);

  /* Now set all the tracepoints.  */
  for (i = 0; i < sals.nelts; i++)
    {
      sal = sals.sals[i];

      t = set_raw_tracepoint (sal);
      set_tracepoint_count (tracepoint_count + 1);
      t->number = tracepoint_count;

      /* If a canonical line spec is needed use that instead of the
         command string.  */
      if (canonical != (char **) NULL && canonical[i] != NULL)
	t->addr_string = canonical[i];
      else if (addr_start)
	t->addr_string = savestring (addr_start, addr_end - addr_start);

      trace_mention (t);
    }

  if (sals.nelts > 1)
    {
      printf_filtered ("Multiple tracepoints were set.\n");
      printf_filtered ("Use 'delete trace' to delete unwanted tracepoints.\n");
    }
}

/* Tell the user we have just set a tracepoint TP. */

static void
trace_mention (struct tracepoint *tp)
{
  printf_filtered ("Tracepoint %d", tp->number);

  if (addressprint || (tp->source_file == NULL))
    {
      printf_filtered (" at ");
      print_address_numeric (tp->address, 1, gdb_stdout);
    }
  if (tp->source_file)
    printf_filtered (": file %s, line %d.",
		     tp->source_file, tp->line_number);

  printf_filtered ("\n");
}

/* Print information on tracepoint number TPNUM_EXP, or all if omitted.  */

static void
tracepoints_info (char *tpnum_exp, int from_tty)
{
  struct tracepoint *t;
  struct action_line *action;
  int found_a_tracepoint = 0;
  char wrap_indent[80];
  struct symbol *sym;
  int tpnum = -1;

  if (tpnum_exp)
    tpnum = parse_and_eval_long (tpnum_exp);

  ALL_TRACEPOINTS (t)
    if (tpnum == -1 || tpnum == t->number)
    {
      extern int addressprint;	/* print machine addresses? */

      if (!found_a_tracepoint++)
	{
	  printf_filtered ("Num Enb ");
	  if (addressprint)
	    {
	      if (TARGET_ADDR_BIT <= 32)
		printf_filtered ("Address    ");
	      else
		printf_filtered ("Address            ");
	    }
	  printf_filtered ("PassC StepC What\n");
	}
      strcpy (wrap_indent, "                           ");
      if (addressprint)
	{
	  if (TARGET_ADDR_BIT <= 32)
	    strcat (wrap_indent, "           ");
	  else
	    strcat (wrap_indent, "                   ");
	}

      printf_filtered ("%-3d %-3s ", t->number,
		       t->enabled_p ? "y" : "n");
      if (addressprint)
	{
	  char *tmp;

	  if (TARGET_ADDR_BIT <= 32)
	    tmp = local_hex_string_custom (t->address
					   & (CORE_ADDR) 0xffffffff, 
					   "08l");
	  else
	    tmp = local_hex_string_custom (t->address, "016l");

	  printf_filtered ("%s ", tmp);
	}
      printf_filtered ("%-5d %-5ld ", t->pass_count, t->step_count);

      if (t->source_file)
	{
	  sym = find_pc_sect_function (t->address, t->section);
	  if (sym)
	    {
	      fputs_filtered ("in ", gdb_stdout);
	      fputs_filtered (SYMBOL_PRINT_NAME (sym), gdb_stdout);
	      wrap_here (wrap_indent);
	      fputs_filtered (" at ", gdb_stdout);
	    }
	  fputs_filtered (t->source_file, gdb_stdout);
	  printf_filtered (":%d", t->line_number);
	}
      else
	print_address_symbolic (t->address, gdb_stdout, demangle, " ");

      printf_filtered ("\n");
      if (t->actions)
	{
	  printf_filtered ("  Actions for tracepoint %d: \n", t->number);
	  for (action = t->actions; action; action = action->next)
	    {
	      printf_filtered ("\t%s\n", action->action);
	    }
	}
    }
  if (!found_a_tracepoint)
    {
      if (tpnum == -1)
	printf_filtered ("No tracepoints.\n");
      else
	printf_filtered ("No tracepoint number %d.\n", tpnum);
    }
}

/* Optimization: the code to parse an enable, disable, or delete TP command
   is virtually identical except for whether it performs an enable, disable,
   or delete.  Therefore I've combined them into one function with an opcode.
 */
enum tracepoint_opcode
{
  enable_op,
  disable_op,
  delete_op
};

/* This function implements enable, disable and delete commands. */
static void
tracepoint_operation (struct tracepoint *t, int from_tty,
		      enum tracepoint_opcode opcode)
{
  struct tracepoint *t2;

  if (t == NULL)	/* no tracepoint operand */
    return;

  switch (opcode)
    {
    case enable_op:
      t->enabled_p = 1;
      tracepoint_modify_event (t->number);
      break;
    case disable_op:
      t->enabled_p = 0;
      tracepoint_modify_event (t->number);
      break;
    case delete_op:
      if (tracepoint_chain == t)
	tracepoint_chain = t->next;

      ALL_TRACEPOINTS (t2)
	if (t2->next == t)
	{
	  tracepoint_delete_event (t2->number);
	  t2->next = t->next;
	  break;
	}

      if (t->addr_string)
	xfree (t->addr_string);
      if (t->source_file)
	xfree (t->source_file);
      if (t->actions)
	free_actions (t);

      xfree (t);
      break;
    }
}

/* Utility: parse a tracepoint number and look it up in the list.
   If MULTI_P is true, there might be a range of tracepoints in ARG.
   if OPTIONAL_P is true, then if the argument is missing, the most
   recent tracepoint (tracepoint_count) is returned.  */
struct tracepoint *
get_tracepoint_by_number (char **arg, int multi_p, int optional_p)
{
  struct tracepoint *t;
  int tpnum;
  char *instring = arg == NULL ? NULL : *arg;

  if (arg == NULL || *arg == NULL || ! **arg)
    {
      if (optional_p)
	tpnum = tracepoint_count;
      else
	error_no_arg ("tracepoint number");
    }
  else
    tpnum = multi_p ? get_number_or_range (arg) : get_number (arg);

  if (tpnum <= 0)
    {
      if (instring && *instring)
	printf_filtered ("bad tracepoint number at or near '%s'\n", instring);
      else
	printf_filtered ("Tracepoint argument missing and no previous tracepoint\n");
      return NULL;
    }

  ALL_TRACEPOINTS (t)
    if (t->number == tpnum)
    {
      return t;
    }

  /* FIXME: if we are in the middle of a range we don't want to give
     a message.  The current interface to get_number_or_range doesn't
     allow us to discover this.  */
  printf_unfiltered ("No tracepoint number %d.\n", tpnum);
  return NULL;
}

/* Utility: parse a list of tracepoint numbers, and call a func for each. */
static void
map_args_over_tracepoints (char *args, int from_tty,
			   enum tracepoint_opcode opcode)
{
  struct tracepoint *t, *tmp;

  if (args == 0 || *args == 0)	/* do them all */
    ALL_TRACEPOINTS_SAFE (t, tmp)
      tracepoint_operation (t, from_tty, opcode);
  else
    while (*args)
      {
	QUIT;			/* give user option to bail out with ^C */
	t = get_tracepoint_by_number (&args, 1, 0);
	tracepoint_operation (t, from_tty, opcode);
	while (*args == ' ' || *args == '\t')
	  args++;
      }
}

/* The 'enable trace' command enables tracepoints.  Not supported by all targets.  */
static void
enable_trace_command (char *args, int from_tty)
{
  dont_repeat ();
  map_args_over_tracepoints (args, from_tty, enable_op);
}

/* The 'disable trace' command enables tracepoints.  Not supported by all targets.  */
static void
disable_trace_command (char *args, int from_tty)
{
  dont_repeat ();
  map_args_over_tracepoints (args, from_tty, disable_op);
}

/* Remove a tracepoint (or all if no argument) */
static void
delete_trace_command (char *args, int from_tty)
{
  dont_repeat ();
  if (!args || !*args)		/* No args implies all tracepoints; */
    if (from_tty)		/* confirm only if from_tty... */
      if (tracepoint_chain)	/* and if there are tracepoints to delete! */
	if (!query ("Delete all tracepoints? "))
	  return;

  map_args_over_tracepoints (args, from_tty, delete_op);
}

/* Set passcount for tracepoint.

   First command argument is passcount, second is tracepoint number.
   If tracepoint number omitted, apply to most recently defined.
   Also accepts special argument "all".  */

static void
trace_pass_command (char *args, int from_tty)
{
  struct tracepoint *t1 = (struct tracepoint *) -1, *t2;
  unsigned int count;
  int all = 0;

  if (args == 0 || *args == 0)
    error ("passcount command requires an argument (count + optional TP num)");

  count = strtoul (args, &args, 10);	/* count comes first, then TP num */

  while (*args && isspace ((int) *args))
    args++;

  if (*args && strncasecmp (args, "all", 3) == 0)
    {
      args += 3;			/* skip special argument "all" */
      all = 1;
      if (*args)
	error ("Junk at end of arguments.");
    }
  else
    t1 = get_tracepoint_by_number (&args, 1, 1);

  do
    {
      if (t1)
	{
	  ALL_TRACEPOINTS (t2)
	    if (t1 == (struct tracepoint *) -1 || t1 == t2)
	      {
		t2->pass_count = count;
		tracepoint_modify_event (t2->number);
		if (from_tty)
		  printf_filtered ("Setting tracepoint %d's passcount to %d\n",
				   t2->number, count);
	      }
	  if (! all && *args)
	    t1 = get_tracepoint_by_number (&args, 1, 0);
	}
    }
  while (*args);
}

/* ACTIONS functions: */

/* Prototypes for action-parsing utility commands  */
static void read_actions (struct tracepoint *);

/* The three functions:
   collect_pseudocommand, 
   while_stepping_pseudocommand, and 
   end_actions_pseudocommand
   are placeholders for "commands" that are actually ONLY to be used
   within a tracepoint action list.  If the actual function is ever called,
   it means that somebody issued the "command" at the top level,
   which is always an error.  */

static void
end_actions_pseudocommand (char *args, int from_tty)
{
  error ("This command cannot be used at the top level.");
}

static void
while_stepping_pseudocommand (char *args, int from_tty)
{
  error ("This command can only be used in a tracepoint actions list.");
}

static void
collect_pseudocommand (char *args, int from_tty)
{
  error ("This command can only be used in a tracepoint actions list.");
}

/* Enter a list of actions for a tracepoint.  */
static void
trace_actions_command (char *args, int from_tty)
{
  struct tracepoint *t;
  char tmpbuf[128];
  char *end_msg = "End with a line saying just \"end\".";

  t = get_tracepoint_by_number (&args, 0, 1);
  if (t)
    {
      sprintf (tmpbuf, "Enter actions for tracepoint %d, one per line.",
	       t->number);

      if (from_tty)
	{
	  if (readline_begin_hook)
	    (*readline_begin_hook) ("%s  %s\n", tmpbuf, end_msg);
	  else if (input_from_terminal_p ())
	    printf_filtered ("%s\n%s\n", tmpbuf, end_msg);
	}

      free_actions (t);
      t->step_count = 0;	/* read_actions may set this */
      read_actions (t);

      if (readline_end_hook)
	(*readline_end_hook) ();
      /* tracepoints_changed () */
    }
  /* else just return */
}

/* worker function */
static void
read_actions (struct tracepoint *t)
{
  char *line;
  char *prompt1 = "> ", *prompt2 = "  > ";
  char *prompt = prompt1;
  enum actionline_type linetype;
  extern FILE *instream;
  struct action_line *next = NULL, *temp;
  struct cleanup *old_chain;

  /* Control-C quits instantly if typed while in this loop
     since it should not wait until the user types a newline.  */
  immediate_quit++;
  /* FIXME: kettenis/20010823: Something is wrong here.  In this file
     STOP_SIGNAL is never defined.  So this code has been left out, at
     least for quite a while now.  Replacing STOP_SIGNAL with SIGTSTP
     leads to compilation failures since the variable job_control
     isn't declared.  Leave this alone for now.  */
#ifdef STOP_SIGNAL
  if (job_control)
    {
      if (event_loop_p)
	signal (STOP_SIGNAL, handle_stop_sig);
      else
	signal (STOP_SIGNAL, stop_sig);
    }
#endif
  old_chain = make_cleanup_free_actions (t);
  while (1)
    {
      /* Make sure that all output has been output.  Some machines may let
         you get away with leaving out some of the gdb_flush, but not all.  */
      wrap_here ("");
      gdb_flush (gdb_stdout);
      gdb_flush (gdb_stderr);

      if (readline_hook && instream == NULL)
	line = (*readline_hook) (prompt);
      else if (instream == stdin && ISATTY (instream))
	{
	  line = gdb_readline_wrapper (prompt);
	  if (line && *line)	/* add it to command history */
	    add_history (line);
	}
      else
	line = gdb_readline (0);

      linetype = validate_actionline (&line, t);
      if (linetype == BADLINE)
	continue;		/* already warned -- collect another line */

      temp = xmalloc (sizeof (struct action_line));
      temp->next = NULL;
      temp->action = line;

      if (next == NULL)		/* first action for this tracepoint? */
	t->actions = next = temp;
      else
	{
	  next->next = temp;
	  next = temp;
	}

      if (linetype == STEPPING)	/* begin "while-stepping" */
	{
	  if (prompt == prompt2)
	    {
	      warning ("Already processing 'while-stepping'");
	      continue;
	    }
	  else
	    prompt = prompt2;	/* change prompt for stepping actions */
	}
      else if (linetype == END)
	{
	  if (prompt == prompt2)
	    {
	      prompt = prompt1;	/* end of single-stepping actions */
	    }
	  else
	    {			/* end of actions */
	      if (t->actions->next == NULL)
		{
		  /* an "end" all by itself with no other actions means
		     this tracepoint has no actions.  Discard empty list. */
		  free_actions (t);
		}
	      break;
	    }
	}
    }
#ifdef STOP_SIGNAL
  if (job_control)
    signal (STOP_SIGNAL, SIG_DFL);
#endif
  immediate_quit--;
  discard_cleanups (old_chain);
}

/* worker function */
enum actionline_type
validate_actionline (char **line, struct tracepoint *t)
{
  struct cmd_list_element *c;
  struct expression *exp = NULL;
  struct cleanup *old_chain = NULL;
  char *p;

  /* if EOF is typed, *line is NULL */
  if (*line == NULL)
    return END;

  for (p = *line; isspace ((int) *p);)
    p++;

  /* symbol lookup etc. */
  if (*p == '\0')		/* empty line: just prompt for another line. */
    return BADLINE;

  if (*p == '#')		/* comment line */
    return GENERIC;

  c = lookup_cmd (&p, cmdlist, "", -1, 1);
  if (c == 0)
    {
      warning ("'%s' is not an action that I know, or is ambiguous.", p);
      return BADLINE;
    }

  if (cmd_cfunc_eq (c, collect_pseudocommand))
    {
      struct agent_expr *aexpr;
      struct agent_reqs areqs;

      do
	{			/* repeat over a comma-separated list */
	  QUIT;			/* allow user to bail out with ^C */
	  while (isspace ((int) *p))
	    p++;

	  if (*p == '$')	/* look for special pseudo-symbols */
	    {
	      if ((0 == strncasecmp ("reg", p + 1, 3)) ||
		  (0 == strncasecmp ("arg", p + 1, 3)) ||
		  (0 == strncasecmp ("loc", p + 1, 3)))
		{
		  p = strchr (p, ',');
		  continue;
		}
	      /* else fall thru, treat p as an expression and parse it! */
	    }
	  exp = parse_exp_1 (&p, block_for_pc (t->address), 1);
	  old_chain = make_cleanup (free_current_contents, &exp);

	  if (exp->elts[0].opcode == OP_VAR_VALUE)
	    {
	      if (SYMBOL_CLASS (exp->elts[2].symbol) == LOC_CONST)
		{
		  warning ("constant %s (value %ld) will not be collected.",
			   DEPRECATED_SYMBOL_NAME (exp->elts[2].symbol),
			   SYMBOL_VALUE (exp->elts[2].symbol));
		  return BADLINE;
		}
	      else if (SYMBOL_CLASS (exp->elts[2].symbol) == LOC_OPTIMIZED_OUT)
		{
		  warning ("%s is optimized away and cannot be collected.",
			   DEPRECATED_SYMBOL_NAME (exp->elts[2].symbol));
		  return BADLINE;
		}
	    }

	  /* we have something to collect, make sure that the expr to
	     bytecode translator can handle it and that it's not too long */
	  aexpr = gen_trace_for_expr (t->address, exp);
	  make_cleanup_free_agent_expr (aexpr);

	  if (aexpr->len > MAX_AGENT_EXPR_LEN)
	    error ("expression too complicated, try simplifying");

	  ax_reqs (aexpr, &areqs);
	  (void) make_cleanup (xfree, areqs.reg_mask);

	  if (areqs.flaw != agent_flaw_none)
	    error ("malformed expression");

	  if (areqs.min_height < 0)
	    error ("gdb: Internal error: expression has min height < 0");

	  if (areqs.max_height > 20)
	    error ("expression too complicated, try simplifying");

	  do_cleanups (old_chain);
	}
      while (p && *p++ == ',');
      return GENERIC;
    }
  else if (cmd_cfunc_eq (c, while_stepping_pseudocommand))
    {
      char *steparg;		/* in case warning is necessary */

      while (isspace ((int) *p))
	p++;
      steparg = p;

      if (*p == '\0' ||
	  (t->step_count = strtol (p, &p, 0)) == 0)
	{
	  warning ("'%s': bad step-count; command ignored.", *line);
	  return BADLINE;
	}
      return STEPPING;
    }
  else if (cmd_cfunc_eq (c, end_actions_pseudocommand))
    return END;
  else
    {
      warning ("'%s' is not a supported tracepoint action.", *line);
      return BADLINE;
    }
}

/* worker function */
void
free_actions (struct tracepoint *t)
{
  struct action_line *line, *next;

  for (line = t->actions; line; line = next)
    {
      next = line->next;
      if (line->action)
	xfree (line->action);
      xfree (line);
    }
  t->actions = NULL;
}

static void
do_free_actions_cleanup (void *t)
{
  free_actions (t);
}

static struct cleanup *
make_cleanup_free_actions (struct tracepoint *t)
{
  return make_cleanup (do_free_actions_cleanup, t);
}

struct memrange
{
  int type;		/* 0 for absolute memory range, else basereg number */
  bfd_signed_vma start;
  bfd_signed_vma end;
};

struct collection_list
  {
    unsigned char regs_mask[8];	/* room for up to 256 regs */
    long listsize;
    long next_memrange;
    struct memrange *list;
    long aexpr_listsize;	/* size of array pointed to by expr_list elt */
    long next_aexpr_elt;
    struct agent_expr **aexpr_list;

  }
tracepoint_list, stepping_list;

/* MEMRANGE functions: */

static int memrange_cmp (const void *, const void *);

/* compare memranges for qsort */
static int
memrange_cmp (const void *va, const void *vb)
{
  const struct memrange *a = va, *b = vb;

  if (a->type < b->type)
    return -1;
  if (a->type > b->type)
    return 1;
  if (a->type == 0)
    {
      if ((bfd_vma) a->start < (bfd_vma) b->start)
	return -1;
      if ((bfd_vma) a->start > (bfd_vma) b->start)
	return 1;
    }
  else
    {
      if (a->start < b->start)
	return -1;
      if (a->start > b->start)
	return 1;
    }
  return 0;
}

/* Sort the memrange list using qsort, and merge adjacent memranges */
static void
memrange_sortmerge (struct collection_list *memranges)
{
  int a, b;

  qsort (memranges->list, memranges->next_memrange,
	 sizeof (struct memrange), memrange_cmp);
  if (memranges->next_memrange > 0)
    {
      for (a = 0, b = 1; b < memranges->next_memrange; b++)
	{
	  if (memranges->list[a].type == memranges->list[b].type &&
	      memranges->list[b].start - memranges->list[a].end <=
	      MAX_REGISTER_SIZE)
	    {
	      /* memrange b starts before memrange a ends; merge them.  */
	      if (memranges->list[b].end > memranges->list[a].end)
		memranges->list[a].end = memranges->list[b].end;
	      continue;		/* next b, same a */
	    }
	  a++;			/* next a */
	  if (a != b)
	    memcpy (&memranges->list[a], &memranges->list[b],
		    sizeof (struct memrange));
	}
      memranges->next_memrange = a + 1;
    }
}

/* Add a register to a collection list */
static void
add_register (struct collection_list *collection, unsigned int regno)
{
  if (info_verbose)
    printf_filtered ("collect register %d\n", regno);
  if (regno > (8 * sizeof (collection->regs_mask)))
    error ("Internal: register number %d too large for tracepoint",
	   regno);
  collection->regs_mask[regno / 8] |= 1 << (regno % 8);
}

/* Add a memrange to a collection list */
static void
add_memrange (struct collection_list *memranges, int type, bfd_signed_vma base,
	      unsigned long len)
{
  if (info_verbose)
    {
      printf_filtered ("(%d,", type);
      printf_vma (base);
      printf_filtered (",%ld)\n", len);
    }

  /* type: 0 == memory, n == basereg */
  memranges->list[memranges->next_memrange].type = type;
  /* base: addr if memory, offset if reg relative. */
  memranges->list[memranges->next_memrange].start = base;
  /* len: we actually save end (base + len) for convenience */
  memranges->list[memranges->next_memrange].end = base + len;
  memranges->next_memrange++;
  if (memranges->next_memrange >= memranges->listsize)
    {
      memranges->listsize *= 2;
      memranges->list = xrealloc (memranges->list,
				  memranges->listsize);
    }

  if (type != -1)		/* better collect the base register! */
    add_register (memranges, type);
}

/* Add a symbol to a collection list */
static void
collect_symbol (struct collection_list *collect, struct symbol *sym,
		long frame_regno, long frame_offset)
{
  unsigned long len;
  unsigned int reg;
  bfd_signed_vma offset;

  len = TYPE_LENGTH (check_typedef (SYMBOL_TYPE (sym)));
  switch (SYMBOL_CLASS (sym))
    {
    default:
      printf_filtered ("%s: don't know symbol class %d\n",
		       DEPRECATED_SYMBOL_NAME (sym), SYMBOL_CLASS (sym));
      break;
    case LOC_CONST:
      printf_filtered ("constant %s (value %ld) will not be collected.\n",
		       DEPRECATED_SYMBOL_NAME (sym), SYMBOL_VALUE (sym));
      break;
    case LOC_STATIC:
      offset = SYMBOL_VALUE_ADDRESS (sym);
      if (info_verbose)
	{
	  char tmp[40];

	  sprintf_vma (tmp, offset);
	  printf_filtered ("LOC_STATIC %s: collect %ld bytes at %s.\n",
			   DEPRECATED_SYMBOL_NAME (sym), len, tmp /* address */);
	}
      add_memrange (collect, -1, offset, len);	/* 0 == memory */
      break;
    case LOC_REGISTER:
    case LOC_REGPARM:
      reg = SYMBOL_VALUE (sym);
      if (info_verbose)
	printf_filtered ("LOC_REG[parm] %s: ", DEPRECATED_SYMBOL_NAME (sym));
      add_register (collect, reg);
      /* check for doubles stored in two registers */
      /* FIXME: how about larger types stored in 3 or more regs? */
      if (TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_FLT &&
	  len > DEPRECATED_REGISTER_RAW_SIZE (reg))
	add_register (collect, reg + 1);
      break;
    case LOC_REF_ARG:
      printf_filtered ("Sorry, don't know how to do LOC_REF_ARG yet.\n");
      printf_filtered ("       (will not collect %s)\n",
		       DEPRECATED_SYMBOL_NAME (sym));
      break;
    case LOC_ARG:
      reg = frame_regno;
      offset = frame_offset + SYMBOL_VALUE (sym);
      if (info_verbose)
	{
	  printf_filtered ("LOC_LOCAL %s: Collect %ld bytes at offset ",
			   DEPRECATED_SYMBOL_NAME (sym), len);
	  printf_vma (offset);
	  printf_filtered (" from frame ptr reg %d\n", reg);
	}
      add_memrange (collect, reg, offset, len);
      break;
    case LOC_REGPARM_ADDR:
      reg = SYMBOL_VALUE (sym);
      offset = 0;
      if (info_verbose)
	{
	  printf_filtered ("LOC_REGPARM_ADDR %s: Collect %ld bytes at offset ",
			   DEPRECATED_SYMBOL_NAME (sym), len);
	  printf_vma (offset);
	  printf_filtered (" from reg %d\n", reg);
	}
      add_memrange (collect, reg, offset, len);
      break;
    case LOC_LOCAL:
    case LOC_LOCAL_ARG:
      reg = frame_regno;
      offset = frame_offset + SYMBOL_VALUE (sym);
      if (info_verbose)
	{
	  printf_filtered ("LOC_LOCAL %s: Collect %ld bytes at offset ",
			   DEPRECATED_SYMBOL_NAME (sym), len);
	  printf_vma (offset);
	  printf_filtered (" from frame ptr reg %d\n", reg);
	}
      add_memrange (collect, reg, offset, len);
      break;
    case LOC_BASEREG:
    case LOC_BASEREG_ARG:
      reg = SYMBOL_BASEREG (sym);
      offset = SYMBOL_VALUE (sym);
      if (info_verbose)
	{
	  printf_filtered ("LOC_BASEREG %s: collect %ld bytes at offset ",
			   DEPRECATED_SYMBOL_NAME (sym), len);
	  printf_vma (offset);
	  printf_filtered (" from basereg %d\n", reg);
	}
      add_memrange (collect, reg, offset, len);
      break;
    case LOC_UNRESOLVED:
      printf_filtered ("Don't know LOC_UNRESOLVED %s\n", DEPRECATED_SYMBOL_NAME (sym));
      break;
    case LOC_OPTIMIZED_OUT:
      printf_filtered ("%s has been optimized out of existence.\n",
		       DEPRECATED_SYMBOL_NAME (sym));
      break;
    }
}

/* Add all locals (or args) symbols to collection list */
static void
add_local_symbols (struct collection_list *collect, CORE_ADDR pc,
		   long frame_regno, long frame_offset, int type)
{
  struct symbol *sym;
  struct block *block;
  struct dict_iterator iter;
  int count = 0;

  block = block_for_pc (pc);
  while (block != 0)
    {
      QUIT;			/* allow user to bail out with ^C */
      ALL_BLOCK_SYMBOLS (block, iter, sym)
	{
	  switch (SYMBOL_CLASS (sym))
	    {
	    default:
	      warning ("don't know how to trace local symbol %s", 
		       DEPRECATED_SYMBOL_NAME (sym));
	    case LOC_LOCAL:
	    case LOC_STATIC:
	    case LOC_REGISTER:
	    case LOC_BASEREG:
	      if (type == 'L')	/* collecting Locals */
		{
		  count++;
		  collect_symbol (collect, sym, frame_regno, frame_offset);
		}
	      break;
	    case LOC_ARG:
	    case LOC_LOCAL_ARG:
	    case LOC_REF_ARG:
	    case LOC_REGPARM:
	    case LOC_REGPARM_ADDR:
	    case LOC_BASEREG_ARG:
	      if (type == 'A')	/* collecting Arguments */
		{
		  count++;
		  collect_symbol (collect, sym, frame_regno, frame_offset);
		}
	    }
	}
      if (BLOCK_FUNCTION (block))
	break;
      else
	block = BLOCK_SUPERBLOCK (block);
    }
  if (count == 0)
    warning ("No %s found in scope.", type == 'L' ? "locals" : "args");
}

/* worker function */
static void
clear_collection_list (struct collection_list *list)
{
  int ndx;

  list->next_memrange = 0;
  for (ndx = 0; ndx < list->next_aexpr_elt; ndx++)
    {
      free_agent_expr (list->aexpr_list[ndx]);
      list->aexpr_list[ndx] = NULL;
    }
  list->next_aexpr_elt = 0;
  memset (list->regs_mask, 0, sizeof (list->regs_mask));
}

/* reduce a collection list to string form (for gdb protocol) */
static char **
stringify_collection_list (struct collection_list *list, char *string)
{
  char temp_buf[2048];
  char tmp2[40];
  int count;
  int ndx = 0;
  char *(*str_list)[];
  char *end;
  long i;

  count = 1 + list->next_memrange + list->next_aexpr_elt + 1;
  str_list = (char *(*)[]) xmalloc (count * sizeof (char *));

  for (i = sizeof (list->regs_mask) - 1; i > 0; i--)
    if (list->regs_mask[i] != 0)	/* skip leading zeroes in regs_mask */
      break;
  if (list->regs_mask[i] != 0)	/* prepare to send regs_mask to the stub */
    {
      if (info_verbose)
	printf_filtered ("\nCollecting registers (mask): 0x");
      end = temp_buf;
      *end++ = 'R';
      for (; i >= 0; i--)
	{
	  QUIT;			/* allow user to bail out with ^C */
	  if (info_verbose)
	    printf_filtered ("%02X", list->regs_mask[i]);
	  sprintf (end, "%02X", list->regs_mask[i]);
	  end += 2;
	}
      (*str_list)[ndx] = savestring (temp_buf, end - temp_buf);
      ndx++;
    }
  if (info_verbose)
    printf_filtered ("\n");
  if (list->next_memrange > 0 && info_verbose)
    printf_filtered ("Collecting memranges: \n");
  for (i = 0, count = 0, end = temp_buf; i < list->next_memrange; i++)
    {
      QUIT;			/* allow user to bail out with ^C */
      sprintf_vma (tmp2, list->list[i].start);
      if (info_verbose)
	{
	  printf_filtered ("(%d, %s, %ld)\n", 
			   list->list[i].type, 
			   tmp2, 
			   (long) (list->list[i].end - list->list[i].start));
	}
      if (count + 27 > MAX_AGENT_EXPR_LEN)
	{
	  (*str_list)[ndx] = savestring (temp_buf, count);
	  ndx++;
	  count = 0;
	  end = temp_buf;
	}

      sprintf (end, "M%X,%s,%lX", 
	       list->list[i].type,
	       tmp2,
	       (long) (list->list[i].end - list->list[i].start));

      count += strlen (end);
      end += count;
    }

  for (i = 0; i < list->next_aexpr_elt; i++)
    {
      QUIT;			/* allow user to bail out with ^C */
      if ((count + 10 + 2 * list->aexpr_list[i]->len) > MAX_AGENT_EXPR_LEN)
	{
	  (*str_list)[ndx] = savestring (temp_buf, count);
	  ndx++;
	  count = 0;
	  end = temp_buf;
	}
      sprintf (end, "X%08X,", list->aexpr_list[i]->len);
      end += 10;		/* 'X' + 8 hex digits + ',' */
      count += 10;

      end = mem2hex (list->aexpr_list[i]->buf, end, list->aexpr_list[i]->len);
      count += 2 * list->aexpr_list[i]->len;
    }

  if (count != 0)
    {
      (*str_list)[ndx] = savestring (temp_buf, count);
      ndx++;
      count = 0;
      end = temp_buf;
    }
  (*str_list)[ndx] = NULL;

  if (ndx == 0)
    return NULL;
  else
    return *str_list;
}

static void
free_actions_list_cleanup_wrapper (void *al)
{
  free_actions_list (al);
}

static void
free_actions_list (char **actions_list)
{
  int ndx;

  if (actions_list == 0)
    return;

  for (ndx = 0; actions_list[ndx]; ndx++)
    xfree (actions_list[ndx]);

  xfree (actions_list);
}

/* render all actions into gdb protocol */
static void
encode_actions (struct tracepoint *t, char ***tdp_actions,
		char ***stepping_actions)
{
  static char tdp_buff[2048], step_buff[2048];
  char *action_exp;
  struct expression *exp = NULL;
  struct action_line *action;
  int i;
  struct value *tempval;
  struct collection_list *collect;
  struct cmd_list_element *cmd;
  struct agent_expr *aexpr;
  int frame_reg;
  LONGEST frame_offset;


  clear_collection_list (&tracepoint_list);
  clear_collection_list (&stepping_list);
  collect = &tracepoint_list;

  *tdp_actions = NULL;
  *stepping_actions = NULL;

  TARGET_VIRTUAL_FRAME_POINTER (t->address, &frame_reg, &frame_offset);

  for (action = t->actions; action; action = action->next)
    {
      QUIT;			/* allow user to bail out with ^C */
      action_exp = action->action;
      while (isspace ((int) *action_exp))
	action_exp++;

      if (*action_exp == '#')	/* comment line */
	return;

      cmd = lookup_cmd (&action_exp, cmdlist, "", -1, 1);
      if (cmd == 0)
	error ("Bad action list item: %s", action_exp);

      if (cmd_cfunc_eq (cmd, collect_pseudocommand))
	{
	  do
	    {			/* repeat over a comma-separated list */
	      QUIT;		/* allow user to bail out with ^C */
	      while (isspace ((int) *action_exp))
		action_exp++;

	      if (0 == strncasecmp ("$reg", action_exp, 4))
		{
		  for (i = 0; i < NUM_REGS; i++)
		    add_register (collect, i);
		  action_exp = strchr (action_exp, ',');	/* more? */
		}
	      else if (0 == strncasecmp ("$arg", action_exp, 4))
		{
		  add_local_symbols (collect,
				     t->address,
				     frame_reg,
				     frame_offset,
				     'A');
		  action_exp = strchr (action_exp, ',');	/* more? */
		}
	      else if (0 == strncasecmp ("$loc", action_exp, 4))
		{
		  add_local_symbols (collect,
				     t->address,
				     frame_reg,
				     frame_offset,
				     'L');
		  action_exp = strchr (action_exp, ',');	/* more? */
		}
	      else
		{
		  unsigned long addr, len;
		  struct cleanup *old_chain = NULL;
		  struct cleanup *old_chain1 = NULL;
		  struct agent_reqs areqs;

		  exp = parse_exp_1 (&action_exp, 
				     block_for_pc (t->address), 1);
		  old_chain = make_cleanup (free_current_contents, &exp);

		  switch (exp->elts[0].opcode)
		    {
		    case OP_REGISTER:
		      i = exp->elts[1].longconst;
		      if (info_verbose)
			printf_filtered ("OP_REGISTER: ");
		      add_register (collect, i);
		      break;

		    case UNOP_MEMVAL:
		      /* safe because we know it's a simple expression */
		      tempval = evaluate_expression (exp);
		      addr = VALUE_ADDRESS (tempval) + VALUE_OFFSET (tempval);
		      len = TYPE_LENGTH (check_typedef (exp->elts[1].type));
		      add_memrange (collect, -1, addr, len);
		      break;

		    case OP_VAR_VALUE:
		      collect_symbol (collect,
				      exp->elts[2].symbol,
				      frame_reg,
				      frame_offset);
		      break;

		    default:	/* full-fledged expression */
		      aexpr = gen_trace_for_expr (t->address, exp);

		      old_chain1 = make_cleanup_free_agent_expr (aexpr);

		      ax_reqs (aexpr, &areqs);
		      if (areqs.flaw != agent_flaw_none)
			error ("malformed expression");

		      if (areqs.min_height < 0)
			error ("gdb: Internal error: expression has min height < 0");
		      if (areqs.max_height > 20)
			error ("expression too complicated, try simplifying");

		      discard_cleanups (old_chain1);
		      add_aexpr (collect, aexpr);

		      /* take care of the registers */
		      if (areqs.reg_mask_len > 0)
			{
			  int ndx1;
			  int ndx2;

			  for (ndx1 = 0; ndx1 < areqs.reg_mask_len; ndx1++)
			    {
			      QUIT;	/* allow user to bail out with ^C */
			      if (areqs.reg_mask[ndx1] != 0)
				{
				  /* assume chars have 8 bits */
				  for (ndx2 = 0; ndx2 < 8; ndx2++)
				    if (areqs.reg_mask[ndx1] & (1 << ndx2))
				      /* it's used -- record it */
				      add_register (collect, ndx1 * 8 + ndx2);
				}
			    }
			}
		      break;
		    }		/* switch */
		  do_cleanups (old_chain);
		}		/* do */
	    }
	  while (action_exp && *action_exp++ == ',');
	}			/* if */
      else if (cmd_cfunc_eq (cmd, while_stepping_pseudocommand))
	{
	  collect = &stepping_list;
	}
      else if (cmd_cfunc_eq (cmd, end_actions_pseudocommand))
	{
	  if (collect == &stepping_list)	/* end stepping actions */
	    collect = &tracepoint_list;
	  else
	    break;		/* end tracepoint actions */
	}
    }				/* for */
  memrange_sortmerge (&tracepoint_list);
  memrange_sortmerge (&stepping_list);

  *tdp_actions = stringify_collection_list (&tracepoint_list, tdp_buff);
  *stepping_actions = stringify_collection_list (&stepping_list, step_buff);
}

static void
add_aexpr (struct collection_list *collect, struct agent_expr *aexpr)
{
  if (collect->next_aexpr_elt >= collect->aexpr_listsize)
    {
      collect->aexpr_list =
	xrealloc (collect->aexpr_list,
		2 * collect->aexpr_listsize * sizeof (struct agent_expr *));
      collect->aexpr_listsize *= 2;
    }
  collect->aexpr_list[collect->next_aexpr_elt] = aexpr;
  collect->next_aexpr_elt++;
}

static char target_buf[2048];

/* Set "transparent" memory ranges

   Allow trace mechanism to treat text-like sections
   (and perhaps all read-only sections) transparently, 
   i.e. don't reject memory requests from these address ranges
   just because they haven't been collected.  */

static void
remote_set_transparent_ranges (void)
{
  extern bfd *exec_bfd;
  asection *s;
  bfd_size_type size;
  bfd_vma lma;
  int anysecs = 0;

  if (!exec_bfd)
    return;			/* no information to give. */

  strcpy (target_buf, "QTro");
  for (s = exec_bfd->sections; s; s = s->next)
    {
      char tmp1[40], tmp2[40];

      if ((s->flags & SEC_LOAD) == 0 ||
      /* (s->flags & SEC_CODE)     == 0 || */
	  (s->flags & SEC_READONLY) == 0)
	continue;

      anysecs = 1;
      lma = s->lma;
      size = bfd_get_section_size (s);
      sprintf_vma (tmp1, lma);
      sprintf_vma (tmp2, lma + size);
      sprintf (target_buf + strlen (target_buf), 
	       ":%s,%s", tmp1, tmp2);
    }
  if (anysecs)
    {
      putpkt (target_buf);
      getpkt (target_buf, sizeof (target_buf), 0);
    }
}

/* tstart command:

   Tell target to clear any previous trace experiment.
   Walk the list of tracepoints, and send them (and their actions)
   to the target.  If no errors, 
   Tell target to start a new trace experiment.  */

static void
trace_start_command (char *args, int from_tty)
{				/* STUB_COMM MOSTLY_IMPLEMENTED */
  struct tracepoint *t;
  char buf[2048];
  char **tdp_actions;
  char **stepping_actions;
  int ndx;
  struct cleanup *old_chain = NULL;

  dont_repeat ();		/* like "run", dangerous to repeat accidentally */

  if (target_is_remote ())
    {
      putpkt ("QTinit");
      remote_get_noisy_reply (target_buf, sizeof (target_buf));
      if (strcmp (target_buf, "OK"))
	error ("Target does not support this command.");

      ALL_TRACEPOINTS (t)
      {
	char tmp[40];

	sprintf_vma (tmp, t->address);
	sprintf (buf, "QTDP:%x:%s:%c:%lx:%x", t->number, tmp, /* address */
		 t->enabled_p ? 'E' : 'D',
		 t->step_count, t->pass_count);

	if (t->actions)
	  strcat (buf, "-");
	putpkt (buf);
	remote_get_noisy_reply (target_buf, sizeof (target_buf));
	if (strcmp (target_buf, "OK"))
	  error ("Target does not support tracepoints.");

	if (t->actions)
	  {
	    encode_actions (t, &tdp_actions, &stepping_actions);
	    old_chain = make_cleanup (free_actions_list_cleanup_wrapper,
				      tdp_actions);
	    (void) make_cleanup (free_actions_list_cleanup_wrapper,
				 stepping_actions);

	    /* do_single_steps (t); */
	    if (tdp_actions)
	      {
		for (ndx = 0; tdp_actions[ndx]; ndx++)
		  {
		    QUIT;	/* allow user to bail out with ^C */
		    sprintf (buf, "QTDP:-%x:%s:%s%c",
			     t->number, tmp, /* address */
			     tdp_actions[ndx],
			     ((tdp_actions[ndx + 1] || stepping_actions)
			      ? '-' : 0));
		    putpkt (buf);
		    remote_get_noisy_reply (target_buf, sizeof (target_buf));
		    if (strcmp (target_buf, "OK"))
		      error ("Error on target while setting tracepoints.");
		  }
	      }
	    if (stepping_actions)
	      {
		for (ndx = 0; stepping_actions[ndx]; ndx++)
		  {
		    QUIT;	/* allow user to bail out with ^C */
		    sprintf (buf, "QTDP:-%x:%s:%s%s%s",
			     t->number, tmp, /* address */
			     ((ndx == 0) ? "S" : ""),
			     stepping_actions[ndx],
			     (stepping_actions[ndx + 1] ? "-" : ""));
		    putpkt (buf);
		    remote_get_noisy_reply (target_buf, sizeof (target_buf));
		    if (strcmp (target_buf, "OK"))
		      error ("Error on target while setting tracepoints.");
		  }
	      }

	    do_cleanups (old_chain);
	  }
      }
      /* Tell target to treat text-like sections as transparent */
      remote_set_transparent_ranges ();
      /* Now insert traps and begin collecting data */
      putpkt ("QTStart");
      remote_get_noisy_reply (target_buf, sizeof (target_buf));
      if (strcmp (target_buf, "OK"))
	error ("Bogus reply from target: %s", target_buf);
      set_traceframe_num (-1);	/* all old traceframes invalidated */
      set_tracepoint_num (-1);
      set_traceframe_context (-1);
      trace_running_p = 1;
      if (trace_start_stop_hook)
	trace_start_stop_hook (1, from_tty);

    }
  else
    error ("Trace can only be run on remote targets.");
}

/* tstop command */
static void
trace_stop_command (char *args, int from_tty)
{				/* STUB_COMM IS_IMPLEMENTED */
  if (target_is_remote ())
    {
      putpkt ("QTStop");
      remote_get_noisy_reply (target_buf, sizeof (target_buf));
      if (strcmp (target_buf, "OK"))
	error ("Bogus reply from target: %s", target_buf);
      trace_running_p = 0;
      if (trace_start_stop_hook)
	trace_start_stop_hook (0, from_tty);
    }
  else
    error ("Trace can only be run on remote targets.");
}

unsigned long trace_running_p;

/* tstatus command */
static void
trace_status_command (char *args, int from_tty)
{				/* STUB_COMM IS_IMPLEMENTED */
  if (target_is_remote ())
    {
      putpkt ("qTStatus");
      remote_get_noisy_reply (target_buf, sizeof (target_buf));

      if (target_buf[0] != 'T' ||
	  (target_buf[1] != '0' && target_buf[1] != '1'))
	error ("Bogus reply from target: %s", target_buf);

      /* exported for use by the GUI */
      trace_running_p = (target_buf[1] == '1');
    }
  else
    error ("Trace can only be run on remote targets.");
}

/* Worker function for the various flavors of the tfind command */
static void
finish_tfind_command (char *msg,
		      long sizeof_msg,
		      int from_tty)
{
  int target_frameno = -1, target_tracept = -1;
  CORE_ADDR old_frame_addr;
  struct symbol *old_func;
  char *reply;

  old_frame_addr = get_frame_base (get_current_frame ());
  old_func = find_pc_function (read_pc ());

  putpkt (msg);
  reply = remote_get_noisy_reply (msg, sizeof_msg);

  while (reply && *reply)
    switch (*reply)
      {
      case 'F':
	if ((target_frameno = (int) strtol (++reply, &reply, 16)) == -1)
	  {
	    /* A request for a non-existant trace frame has failed.
	       Our response will be different, depending on FROM_TTY:

	       If FROM_TTY is true, meaning that this command was 
	       typed interactively by the user, then give an error
	       and DO NOT change the state of traceframe_number etc.

	       However if FROM_TTY is false, meaning that we're either
	       in a script, a loop, or a user-defined command, then 
	       DON'T give an error, but DO change the state of
	       traceframe_number etc. to invalid.

	       The rationalle is that if you typed the command, you
	       might just have committed a typo or something, and you'd
	       like to NOT lose your current debugging state.  However
	       if you're in a user-defined command or especially in a
	       loop, then you need a way to detect that the command
	       failed WITHOUT aborting.  This allows you to write
	       scripts that search thru the trace buffer until the end,
	       and then continue on to do something else.  */

	    if (from_tty)
	      error ("Target failed to find requested trace frame.");
	    else
	      {
		if (info_verbose)
		  printf_filtered ("End of trace buffer.\n");
		/* The following will not recurse, since it's special-cased */
		trace_find_command ("-1", from_tty);
		reply = NULL;	/* break out of loop, 
				   (avoid recursive nonsense) */
	      }
	  }
	break;
      case 'T':
	if ((target_tracept = (int) strtol (++reply, &reply, 16)) == -1)
	  error ("Target failed to find requested trace frame.");
	break;
      case 'O':		/* "OK"? */
	if (reply[1] == 'K' && reply[2] == '\0')
	  reply += 2;
	else
	  error ("Bogus reply from target: %s", reply);
	break;
      default:
	error ("Bogus reply from target: %s", reply);
      }

  flush_cached_frames ();
  registers_changed ();
  select_frame (get_current_frame ());
  set_traceframe_num (target_frameno);
  set_tracepoint_num (target_tracept);
  if (target_frameno == -1)
    set_traceframe_context (-1);
  else
    set_traceframe_context (read_pc ());

  if (from_tty)
    {
      int source_only;

      /* NOTE: in immitation of the step command, try to determine
         whether we have made a transition from one function to another.
         If so, we'll print the "stack frame" (ie. the new function and
         it's arguments) -- otherwise we'll just show the new source line.

         This determination is made by checking (1) whether the current
         function has changed, and (2) whether the current FP has changed.
         Hack: if the FP wasn't collected, either at the current or the
         previous frame, assume that the FP has NOT changed.  */

      if (old_func == find_pc_function (read_pc ()) &&
	  (old_frame_addr == 0 ||
	   get_frame_base (get_current_frame ()) == 0 ||
	   old_frame_addr == get_frame_base (get_current_frame ())))
	source_only = -1;
      else
	source_only = 1;

      print_stack_frame (deprecated_selected_frame,
			 frame_relative_level (deprecated_selected_frame),
			 source_only);
      do_displays ();
    }
}

/* trace_find_command takes a trace frame number n, 
   sends "QTFrame:<n>" to the target, 
   and accepts a reply that may contain several optional pieces
   of information: a frame number, a tracepoint number, and an
   indication of whether this is a trap frame or a stepping frame.

   The minimal response is just "OK" (which indicates that the 
   target does not give us a frame number or a tracepoint number).
   Instead of that, the target may send us a string containing
   any combination of:
   F<hexnum>    (gives the selected frame number)
   T<hexnum>    (gives the selected tracepoint number)
 */

/* tfind command */
static void
trace_find_command (char *args, int from_tty)
{				/* STUB_COMM PART_IMPLEMENTED */
  /* this should only be called with a numeric argument */
  int frameno = -1;

  if (target_is_remote ())
    {
      if (trace_find_hook)
	trace_find_hook (args, from_tty);

      if (args == 0 || *args == 0)
	{			/* TFIND with no args means find NEXT trace frame. */
	  if (traceframe_number == -1)
	    frameno = 0;	/* "next" is first one */
	  else
	    frameno = traceframe_number + 1;
	}
      else if (0 == strcmp (args, "-"))
	{
	  if (traceframe_number == -1)
	    error ("not debugging trace buffer");
	  else if (from_tty && traceframe_number == 0)
	    error ("already at start of trace buffer");

	  frameno = traceframe_number - 1;
	}
      else
	frameno = parse_and_eval_long (args);

      if (frameno < -1)
	error ("invalid input (%d is less than zero)", frameno);

      sprintf (target_buf, "QTFrame:%x", frameno);
      finish_tfind_command (target_buf, sizeof (target_buf), from_tty);
    }
  else
    error ("Trace can only be run on remote targets.");
}

/* tfind end */
static void
trace_find_end_command (char *args, int from_tty)
{
  trace_find_command ("-1", from_tty);
}

/* tfind none */
static void
trace_find_none_command (char *args, int from_tty)
{
  trace_find_command ("-1", from_tty);
}

/* tfind start */
static void
trace_find_start_command (char *args, int from_tty)
{
  trace_find_command ("0", from_tty);
}

/* tfind pc command */
static void
trace_find_pc_command (char *args, int from_tty)
{				/* STUB_COMM PART_IMPLEMENTED */
  CORE_ADDR pc;
  char tmp[40];

  if (target_is_remote ())
    {
      if (args == 0 || *args == 0)
	pc = read_pc ();	/* default is current pc */
      else
	pc = parse_and_eval_address (args);

      sprintf_vma (tmp, pc);
      sprintf (target_buf, "QTFrame:pc:%s", tmp);
      finish_tfind_command (target_buf, sizeof (target_buf), from_tty);
    }
  else
    error ("Trace can only be run on remote targets.");
}

/* tfind tracepoint command */
static void
trace_find_tracepoint_command (char *args, int from_tty)
{				/* STUB_COMM PART_IMPLEMENTED */
  int tdp;

  if (target_is_remote ())
    {
      if (args == 0 || *args == 0)
	{
	  if (tracepoint_number == -1)
	    error ("No current tracepoint -- please supply an argument.");
	  else
	    tdp = tracepoint_number;	/* default is current TDP */
	}
      else
	tdp = parse_and_eval_long (args);

      sprintf (target_buf, "QTFrame:tdp:%x", tdp);
      finish_tfind_command (target_buf, sizeof (target_buf), from_tty);
    }
  else
    error ("Trace can only be run on remote targets.");
}

/* TFIND LINE command:

   This command will take a sourceline for argument, just like BREAK
   or TRACE (ie. anything that "decode_line_1" can handle).  

   With no argument, this command will find the next trace frame 
   corresponding to a source line OTHER THAN THE CURRENT ONE.  */

static void
trace_find_line_command (char *args, int from_tty)
{				/* STUB_COMM PART_IMPLEMENTED */
  static CORE_ADDR start_pc, end_pc;
  struct symtabs_and_lines sals;
  struct symtab_and_line sal;
  struct cleanup *old_chain;
  char   startpc_str[40], endpc_str[40];

  if (target_is_remote ())
    {
      if (args == 0 || *args == 0)
	{
	  sal = find_pc_line (get_frame_pc (get_current_frame ()), 0);
	  sals.nelts = 1;
	  sals.sals = (struct symtab_and_line *)
	    xmalloc (sizeof (struct symtab_and_line));
	  sals.sals[0] = sal;
	}
      else
	{
	  sals = decode_line_spec (args, 1);
	  sal = sals.sals[0];
	}

      old_chain = make_cleanup (xfree, sals.sals);
      if (sal.symtab == 0)
	{
	  printf_filtered ("TFIND: No line number information available");
	  if (sal.pc != 0)
	    {
	      /* This is useful for "info line *0x7f34".  If we can't tell the
	         user about a source line, at least let them have the symbolic
	         address.  */
	      printf_filtered (" for address ");
	      wrap_here ("  ");
	      print_address (sal.pc, gdb_stdout);
	      printf_filtered (";\n -- will attempt to find by PC. \n");
	    }
	  else
	    {
	      printf_filtered (".\n");
	      return;		/* no line, no PC; what can we do? */
	    }
	}
      else if (sal.line > 0
	       && find_line_pc_range (sal, &start_pc, &end_pc))
	{
	  if (start_pc == end_pc)
	    {
	      printf_filtered ("Line %d of \"%s\"",
			       sal.line, sal.symtab->filename);
	      wrap_here ("  ");
	      printf_filtered (" is at address ");
	      print_address (start_pc, gdb_stdout);
	      wrap_here ("  ");
	      printf_filtered (" but contains no code.\n");
	      sal = find_pc_line (start_pc, 0);
	      if (sal.line > 0 &&
		  find_line_pc_range (sal, &start_pc, &end_pc) &&
		  start_pc != end_pc)
		printf_filtered ("Attempting to find line %d instead.\n",
				 sal.line);
	      else
		error ("Cannot find a good line.");
	    }
	}
      else
	/* Is there any case in which we get here, and have an address
	   which the user would want to see?  If we have debugging symbols
	   and no line numbers?  */
	error ("Line number %d is out of range for \"%s\".\n",
	       sal.line, sal.symtab->filename);

      sprintf_vma (startpc_str, start_pc);
      sprintf_vma (endpc_str, end_pc - 1);
      if (args && *args)	/* find within range of stated line */
	sprintf (target_buf, "QTFrame:range:%s:%s", startpc_str, endpc_str);
      else			/* find OUTSIDE OF range of CURRENT line */
	sprintf (target_buf, "QTFrame:outside:%s:%s", startpc_str, endpc_str);
      finish_tfind_command (target_buf, sizeof (target_buf), from_tty);
      do_cleanups (old_chain);
    }
  else
    error ("Trace can only be run on remote targets.");
}

/* tfind range command */
static void
trace_find_range_command (char *args, int from_tty)
{
  static CORE_ADDR start, stop;
  char start_str[40], stop_str[40];
  char *tmp;

  if (target_is_remote ())
    {
      if (args == 0 || *args == 0)
	{		/* XXX FIXME: what should default behavior be? */
	  printf_filtered ("Usage: tfind range <startaddr>,<endaddr>\n");
	  return;
	}

      if (0 != (tmp = strchr (args, ',')))
	{
	  *tmp++ = '\0';	/* terminate start address */
	  while (isspace ((int) *tmp))
	    tmp++;
	  start = parse_and_eval_address (args);
	  stop = parse_and_eval_address (tmp);
	}
      else
	{			/* no explicit end address? */
	  start = parse_and_eval_address (args);
	  stop = start + 1;	/* ??? */
	}

      sprintf_vma (start_str, start);
      sprintf_vma (stop_str, stop);
      sprintf (target_buf, "QTFrame:range:%s:%s", start_str, stop_str);
      finish_tfind_command (target_buf, sizeof (target_buf), from_tty);
    }
  else
    error ("Trace can only be run on remote targets.");
}

/* tfind outside command */
static void
trace_find_outside_command (char *args, int from_tty)
{
  CORE_ADDR start, stop;
  char start_str[40], stop_str[40];
  char *tmp;

  if (target_is_remote ())
    {
      if (args == 0 || *args == 0)
	{		/* XXX FIXME: what should default behavior be? */
	  printf_filtered ("Usage: tfind outside <startaddr>,<endaddr>\n");
	  return;
	}

      if (0 != (tmp = strchr (args, ',')))
	{
	  *tmp++ = '\0';	/* terminate start address */
	  while (isspace ((int) *tmp))
	    tmp++;
	  start = parse_and_eval_address (args);
	  stop = parse_and_eval_address (tmp);
	}
      else
	{			/* no explicit end address? */
	  start = parse_and_eval_address (args);
	  stop = start + 1;	/* ??? */
	}

      sprintf_vma (start_str, start);
      sprintf_vma (stop_str, stop);
      sprintf (target_buf, "QTFrame:outside:%s:%s", start_str, stop_str);
      finish_tfind_command (target_buf, sizeof (target_buf), from_tty);
    }
  else
    error ("Trace can only be run on remote targets.");
}

/* save-tracepoints command */
static void
tracepoint_save_command (char *args, int from_tty)
{
  struct tracepoint *tp;
  struct action_line *line;
  FILE *fp;
  char *i1 = "    ", *i2 = "      ";
  char *indent, *actionline, *pathname;
  char tmp[40];

  if (args == 0 || *args == 0)
    error ("Argument required (file name in which to save tracepoints");

  if (tracepoint_chain == 0)
    {
      warning ("save-tracepoints: no tracepoints to save.\n");
      return;
    }

  pathname = tilde_expand (args);
  if (!(fp = fopen (pathname, "w")))
    error ("Unable to open file '%s' for saving tracepoints (%s)",
	   args, safe_strerror (errno));
  xfree (pathname);
  
  ALL_TRACEPOINTS (tp)
  {
    if (tp->addr_string)
      fprintf (fp, "trace %s\n", tp->addr_string);
    else
      {
	sprintf_vma (tmp, tp->address);
	fprintf (fp, "trace *0x%s\n", tmp);
      }

    if (tp->pass_count)
      fprintf (fp, "  passcount %d\n", tp->pass_count);

    if (tp->actions)
      {
	fprintf (fp, "  actions\n");
	indent = i1;
	for (line = tp->actions; line; line = line->next)
	  {
	    struct cmd_list_element *cmd;

	    QUIT;		/* allow user to bail out with ^C */
	    actionline = line->action;
	    while (isspace ((int) *actionline))
	      actionline++;

	    fprintf (fp, "%s%s\n", indent, actionline);
	    if (*actionline != '#')	/* skip for comment lines */
	      {
		cmd = lookup_cmd (&actionline, cmdlist, "", -1, 1);
		if (cmd == 0)
		  error ("Bad action list item: %s", actionline);
		if (cmd_cfunc_eq (cmd, while_stepping_pseudocommand))
		  indent = i2;
		else if (cmd_cfunc_eq (cmd, end_actions_pseudocommand))
		  indent = i1;
	      }
	  }
      }
  }
  fclose (fp);
  if (from_tty)
    printf_filtered ("Tracepoints saved to file '%s'.\n", args);
  return;
}

/* info scope command: list the locals for a scope.  */
static void
scope_info (char *args, int from_tty)
{
  struct symtabs_and_lines sals;
  struct symbol *sym;
  struct minimal_symbol *msym;
  struct block *block;
  char **canonical, *symname, *save_args = args;
  struct dict_iterator iter;
  int j, count = 0;

  if (args == 0 || *args == 0)
    error ("requires an argument (function, line or *addr) to define a scope");

  sals = decode_line_1 (&args, 1, NULL, 0, &canonical, NULL);
  if (sals.nelts == 0)
    return;			/* presumably decode_line_1 has already warned */

  /* Resolve line numbers to PC */
  resolve_sal_pc (&sals.sals[0]);
  block = block_for_pc (sals.sals[0].pc);

  while (block != 0)
    {
      QUIT;			/* allow user to bail out with ^C */
      ALL_BLOCK_SYMBOLS (block, iter, sym)
	{
	  QUIT;			/* allow user to bail out with ^C */
	  if (count == 0)
	    printf_filtered ("Scope for %s:\n", save_args);
	  count++;

	  symname = DEPRECATED_SYMBOL_NAME (sym);
	  if (symname == NULL || *symname == '\0')
	    continue;		/* probably botched, certainly useless */

	  printf_filtered ("Symbol %s is ", symname);
	  switch (SYMBOL_CLASS (sym))
	    {
	    default:
	    case LOC_UNDEF:	/* messed up symbol? */
	      printf_filtered ("a bogus symbol, class %d.\n",
			       SYMBOL_CLASS (sym));
	      count--;		/* don't count this one */
	      continue;
	    case LOC_CONST:
	      printf_filtered ("a constant with value %ld (0x%lx)",
			       SYMBOL_VALUE (sym), SYMBOL_VALUE (sym));
	      break;
	    case LOC_CONST_BYTES:
	      printf_filtered ("constant bytes: ");
	      if (SYMBOL_TYPE (sym))
		for (j = 0; j < TYPE_LENGTH (SYMBOL_TYPE (sym)); j++)
		  fprintf_filtered (gdb_stdout, " %02x",
				    (unsigned) SYMBOL_VALUE_BYTES (sym)[j]);
	      break;
	    case LOC_STATIC:
	      printf_filtered ("in static storage at address ");
	      print_address_numeric (SYMBOL_VALUE_ADDRESS (sym), 1, gdb_stdout);
	      break;
	    case LOC_REGISTER:
	      printf_filtered ("a local variable in register $%s",
			       REGISTER_NAME (SYMBOL_VALUE (sym)));
	      break;
	    case LOC_ARG:
	    case LOC_LOCAL_ARG:
	      printf_filtered ("an argument at stack/frame offset %ld",
			       SYMBOL_VALUE (sym));
	      break;
	    case LOC_LOCAL:
	      printf_filtered ("a local variable at frame offset %ld",
			       SYMBOL_VALUE (sym));
	      break;
	    case LOC_REF_ARG:
	      printf_filtered ("a reference argument at offset %ld",
			       SYMBOL_VALUE (sym));
	      break;
	    case LOC_REGPARM:
	      printf_filtered ("an argument in register $%s",
			       REGISTER_NAME (SYMBOL_VALUE (sym)));
	      break;
	    case LOC_REGPARM_ADDR:
	      printf_filtered ("the address of an argument, in register $%s",
			       REGISTER_NAME (SYMBOL_VALUE (sym)));
	      break;
	    case LOC_TYPEDEF:
	      printf_filtered ("a typedef.\n");
	      continue;
	    case LOC_LABEL:
	      printf_filtered ("a label at address ");
	      print_address_numeric (SYMBOL_VALUE_ADDRESS (sym), 1, gdb_stdout);
	      break;
	    case LOC_BLOCK:
	      printf_filtered ("a function at address ");
	      print_address_numeric (BLOCK_START (SYMBOL_BLOCK_VALUE (sym)), 1,
				     gdb_stdout);
	      break;
	    case LOC_BASEREG:
	      printf_filtered ("a variable at offset %ld from register $%s",
			       SYMBOL_VALUE (sym),
			       REGISTER_NAME (SYMBOL_BASEREG (sym)));
	      break;
	    case LOC_BASEREG_ARG:
	      printf_filtered ("an argument at offset %ld from register $%s",
			       SYMBOL_VALUE (sym),
			       REGISTER_NAME (SYMBOL_BASEREG (sym)));
	      break;
	    case LOC_UNRESOLVED:
	      msym = lookup_minimal_symbol (DEPRECATED_SYMBOL_NAME (sym), NULL, NULL);
	      if (msym == NULL)
		printf_filtered ("Unresolved Static");
	      else
		{
		  printf_filtered ("static storage at address ");
		  print_address_numeric (SYMBOL_VALUE_ADDRESS (msym), 1,
					 gdb_stdout);
		}
	      break;
	    case LOC_OPTIMIZED_OUT:
	      printf_filtered ("optimized out.\n");
	      continue;
	    }
	  if (SYMBOL_TYPE (sym))
	    printf_filtered (", length %d.\n",
			   TYPE_LENGTH (check_typedef (SYMBOL_TYPE (sym))));
	}
      if (BLOCK_FUNCTION (block))
	break;
      else
	block = BLOCK_SUPERBLOCK (block);
    }
  if (count <= 0)
    printf_filtered ("Scope for %s contains no locals or arguments.\n",
		     save_args);
}

/* worker function (cleanup) */
static void
replace_comma (void *data)
{
  char *comma = data;
  *comma = ',';
}

/* tdump command */
static void
trace_dump_command (char *args, int from_tty)
{
  struct tracepoint *t;
  struct action_line *action;
  char *action_exp, *next_comma;
  struct cleanup *old_cleanups;
  int stepping_actions = 0;
  int stepping_frame = 0;

  if (!target_is_remote ())
    {
      error ("Trace can only be run on remote targets.");
      return;
    }

  if (tracepoint_number == -1)
    {
      warning ("No current trace frame.");
      return;
    }

  ALL_TRACEPOINTS (t)
    if (t->number == tracepoint_number)
    break;

  if (t == NULL)
    error ("No known tracepoint matches 'current' tracepoint #%d.",
	   tracepoint_number);

  old_cleanups = make_cleanup (null_cleanup, NULL);

  printf_filtered ("Data collected at tracepoint %d, trace frame %d:\n",
		   tracepoint_number, traceframe_number);

  /* The current frame is a trap frame if the frame PC is equal
     to the tracepoint PC.  If not, then the current frame was
     collected during single-stepping.  */

  stepping_frame = (t->address != (read_pc () - DECR_PC_AFTER_BREAK));

  for (action = t->actions; action; action = action->next)
    {
      struct cmd_list_element *cmd;

      QUIT;			/* allow user to bail out with ^C */
      action_exp = action->action;
      while (isspace ((int) *action_exp))
	action_exp++;

      /* The collection actions to be done while stepping are
         bracketed by the commands "while-stepping" and "end".  */

      if (*action_exp == '#')	/* comment line */
	continue;

      cmd = lookup_cmd (&action_exp, cmdlist, "", -1, 1);
      if (cmd == 0)
	error ("Bad action list item: %s", action_exp);

      if (cmd_cfunc_eq (cmd, while_stepping_pseudocommand))
	stepping_actions = 1;
      else if (cmd_cfunc_eq (cmd, end_actions_pseudocommand))
	stepping_actions = 0;
      else if (cmd_cfunc_eq (cmd, collect_pseudocommand))
	{
	  /* Display the collected data.
	     For the trap frame, display only what was collected at the trap.
	     Likewise for stepping frames, display only what was collected
	     while stepping.  This means that the two boolean variables,
	     STEPPING_FRAME and STEPPING_ACTIONS should be equal.  */
	  if (stepping_frame == stepping_actions)
	    {
	      do
		{		/* repeat over a comma-separated list */
		  QUIT;		/* allow user to bail out with ^C */
		  if (*action_exp == ',')
		    action_exp++;
		  while (isspace ((int) *action_exp))
		    action_exp++;

		  next_comma = strchr (action_exp, ',');

		  if (0 == strncasecmp (action_exp, "$reg", 4))
		    registers_info (NULL, from_tty);
		  else if (0 == strncasecmp (action_exp, "$loc", 4))
		    locals_info (NULL, from_tty);
		  else if (0 == strncasecmp (action_exp, "$arg", 4))
		    args_info (NULL, from_tty);
		  else
		    {		/* variable */
		      if (next_comma)
			{
			  make_cleanup (replace_comma, next_comma);
			  *next_comma = '\0';
			}
		      printf_filtered ("%s = ", action_exp);
		      output_command (action_exp, from_tty);
		      printf_filtered ("\n");
		    }
		  if (next_comma)
		    *next_comma = ',';
		  action_exp = next_comma;
		}
	      while (action_exp && *action_exp == ',');
	    }
	}
    }
  discard_cleanups (old_cleanups);
}

/* Convert the memory pointed to by mem into hex, placing result in buf.
 * Return a pointer to the last char put in buf (null)
 * "stolen" from sparc-stub.c
 */

static const char hexchars[] = "0123456789abcdef";

static unsigned char *
mem2hex (unsigned char *mem, unsigned char *buf, int count)
{
  unsigned char ch;

  while (count-- > 0)
    {
      ch = *mem++;

      *buf++ = hexchars[ch >> 4];
      *buf++ = hexchars[ch & 0xf];
    }

  *buf = 0;

  return buf;
}

int
get_traceframe_number (void)
{
  return traceframe_number;
}


/* module initialization */
void
_initialize_tracepoint (void)
{
  struct cmd_list_element *c;

  tracepoint_chain = 0;
  tracepoint_count = 0;
  traceframe_number = -1;
  tracepoint_number = -1;

  set_internalvar (lookup_internalvar ("tpnum"),
		   value_from_longest (builtin_type_int, (LONGEST) 0));
  set_internalvar (lookup_internalvar ("trace_frame"),
		   value_from_longest (builtin_type_int, (LONGEST) - 1));

  if (tracepoint_list.list == NULL)
    {
      tracepoint_list.listsize = 128;
      tracepoint_list.list = xmalloc
	(tracepoint_list.listsize * sizeof (struct memrange));
    }
  if (tracepoint_list.aexpr_list == NULL)
    {
      tracepoint_list.aexpr_listsize = 128;
      tracepoint_list.aexpr_list = xmalloc
	(tracepoint_list.aexpr_listsize * sizeof (struct agent_expr *));
    }

  if (stepping_list.list == NULL)
    {
      stepping_list.listsize = 128;
      stepping_list.list = xmalloc
	(stepping_list.listsize * sizeof (struct memrange));
    }

  if (stepping_list.aexpr_list == NULL)
    {
      stepping_list.aexpr_listsize = 128;
      stepping_list.aexpr_list = xmalloc
	(stepping_list.aexpr_listsize * sizeof (struct agent_expr *));
    }

  add_info ("scope", scope_info,
	    "List the variables local to a scope");

  add_cmd ("tracepoints", class_trace, NULL,
	   "Tracing of program execution without stopping the program.",
	   &cmdlist);

  add_info ("tracepoints", tracepoints_info,
	    "Status of tracepoints, or tracepoint number NUMBER.\n\
Convenience variable \"$tpnum\" contains the number of the\n\
last tracepoint set.");

  add_info_alias ("tp", "tracepoints", 1);

  c = add_com ("save-tracepoints", class_trace, tracepoint_save_command,
	       "Save current tracepoint definitions as a script.\n\
Use the 'source' command in another debug session to restore them.");
  set_cmd_completer (c, filename_completer);

  add_com ("tdump", class_trace, trace_dump_command,
	   "Print everything collected at the current tracepoint.");

  add_prefix_cmd ("tfind", class_trace, trace_find_command,
		  "Select a trace frame;\n\
No argument means forward by one frame; '-' meand backward by one frame.",
		  &tfindlist, "tfind ", 1, &cmdlist);

  add_cmd ("outside", class_trace, trace_find_outside_command,
	   "Select a trace frame whose PC is outside the given \
range.\nUsage: tfind outside addr1, addr2",
	   &tfindlist);

  add_cmd ("range", class_trace, trace_find_range_command,
	   "Select a trace frame whose PC is in the given range.\n\
Usage: tfind range addr1,addr2",
	   &tfindlist);

  add_cmd ("line", class_trace, trace_find_line_command,
	   "Select a trace frame by source line.\n\
Argument can be a line number (with optional source file), \n\
a function name, or '*' followed by an address.\n\
Default argument is 'the next source line that was traced'.",
	   &tfindlist);

  add_cmd ("tracepoint", class_trace, trace_find_tracepoint_command,
	   "Select a trace frame by tracepoint number.\n\
Default is the tracepoint for the current trace frame.",
	   &tfindlist);

  add_cmd ("pc", class_trace, trace_find_pc_command,
	   "Select a trace frame by PC.\n\
Default is the current PC, or the PC of the current trace frame.",
	   &tfindlist);

  add_cmd ("end", class_trace, trace_find_end_command,
	   "Synonym for 'none'.\n\
De-select any trace frame and resume 'live' debugging.",
	   &tfindlist);

  add_cmd ("none", class_trace, trace_find_none_command,
	   "De-select any trace frame and resume 'live' debugging.",
	   &tfindlist);

  add_cmd ("start", class_trace, trace_find_start_command,
	   "Select the first trace frame in the trace buffer.",
	   &tfindlist);

  add_com ("tstatus", class_trace, trace_status_command,
	   "Display the status of the current trace data collection.");

  add_com ("tstop", class_trace, trace_stop_command,
	   "Stop trace data collection.");

  add_com ("tstart", class_trace, trace_start_command,
	   "Start trace data collection.");

  add_com ("passcount", class_trace, trace_pass_command,
	   "Set the passcount for a tracepoint.\n\
The trace will end when the tracepoint has been passed 'count' times.\n\
Usage: passcount COUNT TPNUM, where TPNUM may also be \"all\";\n\
if TPNUM is omitted, passcount refers to the last tracepoint defined.");

  add_com ("end", class_trace, end_actions_pseudocommand,
	   "Ends a list of commands or actions.\n\
Several GDB commands allow you to enter a list of commands or actions.\n\
Entering \"end\" on a line by itself is the normal way to terminate\n\
such a list.\n\n\
Note: the \"end\" command cannot be used at the gdb prompt.");

  add_com ("while-stepping", class_trace, while_stepping_pseudocommand,
	   "Specify single-stepping behavior at a tracepoint.\n\
Argument is number of instructions to trace in single-step mode\n\
following the tracepoint.  This command is normally followed by\n\
one or more \"collect\" commands, to specify what to collect\n\
while single-stepping.\n\n\
Note: this command can only be used in a tracepoint \"actions\" list.");

  add_com_alias ("ws", "while-stepping", class_alias, 0);
  add_com_alias ("stepping", "while-stepping", class_alias, 0);

  add_com ("collect", class_trace, collect_pseudocommand,
	   "Specify one or more data items to be collected at a tracepoint.\n\
Accepts a comma-separated list of (one or more) expressions.  GDB will\n\
collect all data (variables, registers) referenced by that expression.\n\
Also accepts the following special arguments:\n\
    $regs   -- all registers.\n\
    $args   -- all function arguments.\n\
    $locals -- all variables local to the block/function scope.\n\
Note: this command can only be used in a tracepoint \"actions\" list.");

  add_com ("actions", class_trace, trace_actions_command,
	   "Specify the actions to be taken at a tracepoint.\n\
Tracepoint actions may include collecting of specified data, \n\
single-stepping, or enabling/disabling other tracepoints, \n\
depending on target's capabilities.");

  add_cmd ("tracepoints", class_trace, delete_trace_command,
	   "Delete specified tracepoints.\n\
Arguments are tracepoint numbers, separated by spaces.\n\
No argument means delete all tracepoints.",
	   &deletelist);

  add_cmd ("tracepoints", class_trace, disable_trace_command,
	   "Disable specified tracepoints.\n\
Arguments are tracepoint numbers, separated by spaces.\n\
No argument means disable all tracepoints.",
	   &disablelist);

  add_cmd ("tracepoints", class_trace, enable_trace_command,
	   "Enable specified tracepoints.\n\
Arguments are tracepoint numbers, separated by spaces.\n\
No argument means enable all tracepoints.",
	   &enablelist);

  c = add_com ("trace", class_trace, trace_command,
	       "Set a tracepoint at a specified line or function or address.\n\
Argument may be a line number, function name, or '*' plus an address.\n\
For a line number or function, trace at the start of its code.\n\
If an address is specified, trace at that exact address.\n\n\
Do \"help tracepoints\" for info on other tracepoint commands.");
  set_cmd_completer (c, location_completer);

  add_com_alias ("tp", "trace", class_alias, 0);
  add_com_alias ("tr", "trace", class_alias, 1);
  add_com_alias ("tra", "trace", class_alias, 1);
  add_com_alias ("trac", "trace", class_alias, 1);
}

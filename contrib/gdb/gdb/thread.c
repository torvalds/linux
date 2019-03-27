/* Multi-process/thread control for GDB, the GNU debugger.

   Copyright 1986, 1987, 1988, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003 Free Software Foundation, Inc.

   Contributed by Lynx Real-Time Systems, Inc.  Los Gatos, CA.

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
#include "inferior.h"
#include "environ.h"
#include "value.h"
#include "target.h"
#include "gdbthread.h"
#include "command.h"
#include "gdbcmd.h"
#include "regcache.h"
#include "gdb.h"
#include "gdb_string.h"

#include <ctype.h>
#include <sys/types.h>
#include <signal.h>
#include "ui-out.h"

/*#include "lynxos-core.h" */

/* Definition of struct thread_info exported to gdbthread.h */

/* Prototypes for exported functions. */

void _initialize_thread (void);

/* Prototypes for local functions. */

static struct thread_info *thread_list = NULL;
static int highest_thread_num;

static struct thread_info *find_thread_id (int num);

static void thread_command (char *tidstr, int from_tty);
static void thread_apply_all_command (char *, int);
static int thread_alive (struct thread_info *);
static void info_threads_command (char *, int);
static void thread_apply_command (char *, int);
static void restore_current_thread (ptid_t);
static void switch_to_thread (ptid_t ptid);
static void prune_threads (void);

void
delete_step_resume_breakpoint (void *arg)
{
  struct breakpoint **breakpointp = (struct breakpoint **) arg;
  struct thread_info *tp;

  if (*breakpointp != NULL)
    {
      delete_breakpoint (*breakpointp);
      for (tp = thread_list; tp; tp = tp->next)
	if (tp->step_resume_breakpoint == *breakpointp)
	  tp->step_resume_breakpoint = NULL;

      *breakpointp = NULL;
    }
}

static void
free_thread (struct thread_info *tp)
{
  /* NOTE: this will take care of any left-over step_resume breakpoints,
     but not any user-specified thread-specific breakpoints. */
  if (tp->step_resume_breakpoint)
    delete_breakpoint (tp->step_resume_breakpoint);

  /* FIXME: do I ever need to call the back-end to give it a
     chance at this private data before deleting the thread?  */
  if (tp->private)
    xfree (tp->private);

  xfree (tp);
}

void
init_thread_list (void)
{
  struct thread_info *tp, *tpnext;

  highest_thread_num = 0;
  if (!thread_list)
    return;

  for (tp = thread_list; tp; tp = tpnext)
    {
      tpnext = tp->next;
      free_thread (tp);
    }

  thread_list = NULL;
}

/* add_thread now returns a pointer to the new thread_info, 
   so that back_ends can initialize their private data.  */

struct thread_info *
add_thread (ptid_t ptid)
{
  struct thread_info *tp;

  tp = (struct thread_info *) xmalloc (sizeof (*tp));
  memset (tp, 0, sizeof (*tp));
  tp->ptid = ptid;
  tp->num = ++highest_thread_num;
  tp->next = thread_list;
  thread_list = tp;
  return tp;
}

void
delete_thread (ptid_t ptid)
{
  struct thread_info *tp, *tpprev;

  tpprev = NULL;

  for (tp = thread_list; tp; tpprev = tp, tp = tp->next)
    if (ptid_equal (tp->ptid, ptid))
      break;

  if (!tp)
    return;

  if (tpprev)
    tpprev->next = tp->next;
  else
    thread_list = tp->next;

  free_thread (tp);
}

static struct thread_info *
find_thread_id (int num)
{
  struct thread_info *tp;

  for (tp = thread_list; tp; tp = tp->next)
    if (tp->num == num)
      return tp;

  return NULL;
}

/* Find a thread_info by matching PTID.  */
struct thread_info *
find_thread_pid (ptid_t ptid)
{
  struct thread_info *tp;

  for (tp = thread_list; tp; tp = tp->next)
    if (ptid_equal (tp->ptid, ptid))
      return tp;

  return NULL;
}

/*
 * Thread iterator function.
 *
 * Calls a callback function once for each thread, so long as
 * the callback function returns false.  If the callback function
 * returns true, the iteration will end and the current thread
 * will be returned.  This can be useful for implementing a 
 * search for a thread with arbitrary attributes, or for applying
 * some operation to every thread.
 *
 * FIXME: some of the existing functionality, such as 
 * "Thread apply all", might be rewritten using this functionality.
 */

struct thread_info *
iterate_over_threads (int (*callback) (struct thread_info *, void *),
		      void *data)
{
  struct thread_info *tp;

  for (tp = thread_list; tp; tp = tp->next)
    if ((*callback) (tp, data))
      return tp;

  return NULL;
}

int
valid_thread_id (int num)
{
  struct thread_info *tp;

  for (tp = thread_list; tp; tp = tp->next)
    if (tp->num == num)
      return 1;

  return 0;
}

int
pid_to_thread_id (ptid_t ptid)
{
  struct thread_info *tp;

  for (tp = thread_list; tp; tp = tp->next)
    if (ptid_equal (tp->ptid, ptid))
      return tp->num;

  return 0;
}

ptid_t
thread_id_to_pid (int num)
{
  struct thread_info *thread = find_thread_id (num);
  if (thread)
    return thread->ptid;
  else
    return pid_to_ptid (-1);
}

int
in_thread_list (ptid_t ptid)
{
  struct thread_info *tp;

  for (tp = thread_list; tp; tp = tp->next)
    if (ptid_equal (tp->ptid, ptid))
      return 1;

  return 0;			/* Never heard of 'im */
}

/* Print a list of thread ids currently known, and the total number of
   threads. To be used from within catch_errors. */
static int
do_captured_list_thread_ids (struct ui_out *uiout, void *arg)
{
  struct thread_info *tp;
  int num = 0;
  struct cleanup *cleanup_chain;

  prune_threads ();
  target_find_new_threads ();

  cleanup_chain = make_cleanup_ui_out_tuple_begin_end (uiout, "thread-ids");

  for (tp = thread_list; tp; tp = tp->next)
    {
      num++;
      ui_out_field_int (uiout, "thread-id", tp->num);
    }

  do_cleanups (cleanup_chain);
  ui_out_field_int (uiout, "number-of-threads", num);
  return GDB_RC_OK;
}

/* Official gdblib interface function to get a list of thread ids and
   the total number. */
enum gdb_rc
gdb_list_thread_ids (struct ui_out *uiout)
{
  return catch_exceptions (uiout, do_captured_list_thread_ids, NULL,
			   NULL, RETURN_MASK_ALL);
}

/* Load infrun state for the thread PID.  */

void
load_infrun_state (ptid_t ptid,
		   CORE_ADDR *prev_pc,
		   int *trap_expected,
		   struct breakpoint **step_resume_breakpoint,
		   struct breakpoint **through_sigtramp_breakpoint,
		   CORE_ADDR *step_range_start,
		   CORE_ADDR *step_range_end,
		   struct frame_id *step_frame_id,
		   int *handling_longjmp,
		   int *another_trap,
		   int *stepping_through_solib_after_catch,
		   bpstat *stepping_through_solib_catchpoints,
		   int *stepping_through_sigtramp,
		   int *current_line,
		   struct symtab **current_symtab, CORE_ADDR *step_sp)
{
  struct thread_info *tp;

  /* If we can't find the thread, then we're debugging a single threaded
     process.  No need to do anything in that case.  */
  tp = find_thread_id (pid_to_thread_id (ptid));
  if (tp == NULL)
    return;

  *prev_pc = tp->prev_pc;
  *trap_expected = tp->trap_expected;
  *step_resume_breakpoint = tp->step_resume_breakpoint;
  *through_sigtramp_breakpoint = tp->through_sigtramp_breakpoint;
  *step_range_start = tp->step_range_start;
  *step_range_end = tp->step_range_end;
  *step_frame_id = tp->step_frame_id;
  *handling_longjmp = tp->handling_longjmp;
  *another_trap = tp->another_trap;
  *stepping_through_solib_after_catch =
    tp->stepping_through_solib_after_catch;
  *stepping_through_solib_catchpoints =
    tp->stepping_through_solib_catchpoints;
  *stepping_through_sigtramp = tp->stepping_through_sigtramp;
  *current_line = tp->current_line;
  *current_symtab = tp->current_symtab;
  *step_sp = tp->step_sp;
}

/* Save infrun state for the thread PID.  */

void
save_infrun_state (ptid_t ptid,
		   CORE_ADDR prev_pc,
		   int trap_expected,
		   struct breakpoint *step_resume_breakpoint,
		   struct breakpoint *through_sigtramp_breakpoint,
		   CORE_ADDR step_range_start,
		   CORE_ADDR step_range_end,
		   const struct frame_id *step_frame_id,
		   int handling_longjmp,
		   int another_trap,
		   int stepping_through_solib_after_catch,
		   bpstat stepping_through_solib_catchpoints,
		   int stepping_through_sigtramp,
		   int current_line,
		   struct symtab *current_symtab, CORE_ADDR step_sp)
{
  struct thread_info *tp;

  /* If we can't find the thread, then we're debugging a single-threaded
     process.  Nothing to do in that case.  */
  tp = find_thread_id (pid_to_thread_id (ptid));
  if (tp == NULL)
    return;

  tp->prev_pc = prev_pc;
  tp->trap_expected = trap_expected;
  tp->step_resume_breakpoint = step_resume_breakpoint;
  tp->through_sigtramp_breakpoint = through_sigtramp_breakpoint;
  tp->step_range_start = step_range_start;
  tp->step_range_end = step_range_end;
  tp->step_frame_id = (*step_frame_id);
  tp->handling_longjmp = handling_longjmp;
  tp->another_trap = another_trap;
  tp->stepping_through_solib_after_catch = stepping_through_solib_after_catch;
  tp->stepping_through_solib_catchpoints = stepping_through_solib_catchpoints;
  tp->stepping_through_sigtramp = stepping_through_sigtramp;
  tp->current_line = current_line;
  tp->current_symtab = current_symtab;
  tp->step_sp = step_sp;
}

/* Return true if TP is an active thread. */
static int
thread_alive (struct thread_info *tp)
{
  if (PIDGET (tp->ptid) == -1)
    return 0;
  if (!target_thread_alive (tp->ptid))
    {
      tp->ptid = pid_to_ptid (-1);	/* Mark it as dead */
      return 0;
    }
  return 1;
}

static void
prune_threads (void)
{
  struct thread_info *tp, *next;

  for (tp = thread_list; tp; tp = next)
    {
      next = tp->next;
      if (!thread_alive (tp))
	delete_thread (tp->ptid);
    }
}

/* Print information about currently known threads 

 * Note: this has the drawback that it _really_ switches
 *       threads, which frees the frame cache.  A no-side
 *       effects info-threads command would be nicer.
 */

static void
info_threads_command (char *arg, int from_tty)
{
  struct thread_info *tp;
  ptid_t current_ptid;
  struct frame_info *cur_frame;
  int saved_frame_level = frame_relative_level (get_selected_frame ());
  int counter;
  char *extra_info;

  /* Check that there really is a frame.  This happens when a simulator
     is connected but not loaded or running, for instance.  */
  if (legacy_frame_p (current_gdbarch) && saved_frame_level < 0)
    error ("No frame.");

  prune_threads ();
  target_find_new_threads ();
  current_ptid = inferior_ptid;
  for (tp = thread_list; tp; tp = tp->next)
    {
      if (ptid_equal (tp->ptid, current_ptid))
	printf_filtered ("* ");
      else
	printf_filtered ("  ");

#ifdef HPUXHPPA
      printf_filtered ("%d %s", tp->num, target_tid_to_str (tp->ptid));
#else
      printf_filtered ("%d %s", tp->num, target_pid_to_str (tp->ptid));
#endif

      extra_info = target_extra_thread_info (tp);
      if (extra_info)
	printf_filtered (" (%s)", extra_info);
      puts_filtered ("  ");

      switch_to_thread (tp->ptid);
      print_stack_frame (get_selected_frame (), -1, 0);
    }

  switch_to_thread (current_ptid);

  /* Code below copied from "up_silently_base" in "stack.c".
   * It restores the frame set by the user before the "info threads"
   * command.  We have finished the info-threads display by switching
   * back to the current thread.  That switch has put us at the top
   * of the stack (leaf frame).
   */
  counter = saved_frame_level;
  cur_frame = find_relative_frame (get_selected_frame (), &counter);
  if (counter != 0)
    {
      /* Ooops, can't restore, tell user where we are. */
      warning ("Couldn't restore frame in current thread, at frame 0");
      print_stack_frame (get_selected_frame (), -1, 0);
    }
  else
    {
      select_frame (cur_frame);
    }

  /* re-show current frame. */
  show_stack_frame (cur_frame);
}

/* Switch from one thread to another. */

static void
switch_to_thread (ptid_t ptid)
{
  if (ptid_equal (ptid, inferior_ptid))
    return;

  inferior_ptid = ptid;
  flush_cached_frames ();
  registers_changed ();
  stop_pc = read_pc ();
  select_frame (get_current_frame ());
}

static void
restore_current_thread (ptid_t ptid)
{
  if (!ptid_equal (ptid, inferior_ptid))
    {
      switch_to_thread (ptid);
      print_stack_frame (get_current_frame (), 0, -1);
    }
}

struct current_thread_cleanup
{
  ptid_t inferior_ptid;
};

static void
do_restore_current_thread_cleanup (void *arg)
{
  struct current_thread_cleanup *old = arg;
  restore_current_thread (old->inferior_ptid);
  xfree (old);
}

static struct cleanup *
make_cleanup_restore_current_thread (ptid_t inferior_ptid)
{
  struct current_thread_cleanup *old
    = xmalloc (sizeof (struct current_thread_cleanup));
  old->inferior_ptid = inferior_ptid;
  return make_cleanup (do_restore_current_thread_cleanup, old);
}

/* Apply a GDB command to a list of threads.  List syntax is a whitespace
   seperated list of numbers, or ranges, or the keyword `all'.  Ranges consist
   of two numbers seperated by a hyphen.  Examples:

   thread apply 1 2 7 4 backtrace       Apply backtrace cmd to threads 1,2,7,4
   thread apply 2-7 9 p foo(1)  Apply p foo(1) cmd to threads 2->7 & 9
   thread apply all p x/i $pc   Apply x/i $pc cmd to all threads
 */

static void
thread_apply_all_command (char *cmd, int from_tty)
{
  struct thread_info *tp;
  struct cleanup *old_chain;
  struct cleanup *saved_cmd_cleanup_chain;
  char *saved_cmd;

  if (cmd == NULL || *cmd == '\000')
    error ("Please specify a command following the thread ID list");

  old_chain = make_cleanup_restore_current_thread (inferior_ptid);

  /* It is safe to update the thread list now, before
     traversing it for "thread apply all".  MVS */
  target_find_new_threads ();

  /* Save a copy of the command in case it is clobbered by
     execute_command */
  saved_cmd = xstrdup (cmd);
  saved_cmd_cleanup_chain = make_cleanup (xfree, (void *) saved_cmd);
  for (tp = thread_list; tp; tp = tp->next)
    if (thread_alive (tp))
      {
	switch_to_thread (tp->ptid);
#ifdef HPUXHPPA
	printf_filtered ("\nThread %d (%s):\n",
			 tp->num, target_tid_to_str (inferior_ptid));
#else
	printf_filtered ("\nThread %d (%s):\n", tp->num,
			 target_pid_to_str (inferior_ptid));
#endif
	execute_command (cmd, from_tty);
	strcpy (cmd, saved_cmd);	/* Restore exact command used previously */
      }

  do_cleanups (saved_cmd_cleanup_chain);
  do_cleanups (old_chain);
}

static void
thread_apply_command (char *tidlist, int from_tty)
{
  char *cmd;
  char *p;
  struct cleanup *old_chain;
  struct cleanup *saved_cmd_cleanup_chain;
  char *saved_cmd;

  if (tidlist == NULL || *tidlist == '\000')
    error ("Please specify a thread ID list");

  for (cmd = tidlist; *cmd != '\000' && !isalpha (*cmd); cmd++);

  if (*cmd == '\000')
    error ("Please specify a command following the thread ID list");

  old_chain = make_cleanup_restore_current_thread (inferior_ptid);

  /* Save a copy of the command in case it is clobbered by
     execute_command */
  saved_cmd = xstrdup (cmd);
  saved_cmd_cleanup_chain = make_cleanup (xfree, (void *) saved_cmd);
  while (tidlist < cmd)
    {
      struct thread_info *tp;
      int start, end;

      start = strtol (tidlist, &p, 10);
      if (p == tidlist)
	error ("Error parsing %s", tidlist);
      tidlist = p;

      while (*tidlist == ' ' || *tidlist == '\t')
	tidlist++;

      if (*tidlist == '-')	/* Got a range of IDs? */
	{
	  tidlist++;		/* Skip the - */
	  end = strtol (tidlist, &p, 10);
	  if (p == tidlist)
	    error ("Error parsing %s", tidlist);
	  tidlist = p;

	  while (*tidlist == ' ' || *tidlist == '\t')
	    tidlist++;
	}
      else
	end = start;

      for (; start <= end; start++)
	{
	  tp = find_thread_id (start);

	  if (!tp)
	    warning ("Unknown thread %d.", start);
	  else if (!thread_alive (tp))
	    warning ("Thread %d has terminated.", start);
	  else
	    {
	      switch_to_thread (tp->ptid);
#ifdef HPUXHPPA
	      printf_filtered ("\nThread %d (%s):\n", tp->num,
			       target_tid_to_str (inferior_ptid));
#else
	      printf_filtered ("\nThread %d (%s):\n", tp->num,
			       target_pid_to_str (inferior_ptid));
#endif
	      execute_command (cmd, from_tty);
	      strcpy (cmd, saved_cmd);	/* Restore exact command used previously */
	    }
	}
    }

  do_cleanups (saved_cmd_cleanup_chain);
  do_cleanups (old_chain);
}

/* Switch to the specified thread.  Will dispatch off to thread_apply_command
   if prefix of arg is `apply'.  */

static void
thread_command (char *tidstr, int from_tty)
{
  if (!tidstr)
    {
      /* Don't generate an error, just say which thread is current. */
      if (target_has_stack)
	printf_filtered ("[Current thread is %d (%s)]\n",
			 pid_to_thread_id (inferior_ptid),
#if defined(HPUXHPPA)
			 target_tid_to_str (inferior_ptid)
#else
			 target_pid_to_str (inferior_ptid)
#endif
	  );
      else
	error ("No stack.");
      return;
    }

  gdb_thread_select (uiout, tidstr);
}

static int
do_captured_thread_select (struct ui_out *uiout, void *tidstr)
{
  int num;
  struct thread_info *tp;

  num = value_as_long (parse_and_eval (tidstr));

  tp = find_thread_id (num);

  if (!tp)
    error ("Thread ID %d not known.", num);

  if (!thread_alive (tp))
    error ("Thread ID %d has terminated.\n", num);

  switch_to_thread (tp->ptid);

  ui_out_text (uiout, "[Switching to thread ");
  ui_out_field_int (uiout, "new-thread-id", pid_to_thread_id (inferior_ptid));
  ui_out_text (uiout, " (");
#if defined(HPUXHPPA)
  ui_out_text (uiout, target_tid_to_str (inferior_ptid));
#else
  ui_out_text (uiout, target_pid_to_str (inferior_ptid));
#endif
  ui_out_text (uiout, ")]");

  print_stack_frame (deprecated_selected_frame,
		     frame_relative_level (deprecated_selected_frame), 1);
  return GDB_RC_OK;
}

enum gdb_rc
gdb_thread_select (struct ui_out *uiout, char *tidstr)
{
  return catch_exceptions (uiout, do_captured_thread_select, tidstr,
			   NULL, RETURN_MASK_ALL);
}

/* Commands with a prefix of `thread'.  */
struct cmd_list_element *thread_cmd_list = NULL;

void
_initialize_thread (void)
{
  static struct cmd_list_element *thread_apply_list = NULL;

  add_info ("threads", info_threads_command,
	    "IDs of currently known threads.");

  add_prefix_cmd ("thread", class_run, thread_command,
		  "Use this command to switch between threads.\n\
The new thread ID must be currently known.", &thread_cmd_list, "thread ", 1, &cmdlist);

  add_prefix_cmd ("apply", class_run, thread_apply_command,
		  "Apply a command to a list of threads.",
		  &thread_apply_list, "apply ", 1, &thread_cmd_list);

  add_cmd ("all", class_run, thread_apply_all_command,
	   "Apply a command to all threads.", &thread_apply_list);

  if (!xdb_commands)
    add_com_alias ("t", "thread", class_run, 1);
}

/* Multi-process/thread control defs for GDB, the GNU debugger.
   Copyright 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1997, 1998, 1999,
   2000
   Free Software Foundation, Inc.
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

#ifndef GDBTHREAD_H
#define GDBTHREAD_H

struct breakpoint;
struct frame_id;
struct symtab;

/* For bpstat */
#include "breakpoint.h"

/* For struct frame_id.  */
#include "frame.h"

struct thread_info
{
  struct thread_info *next;
  ptid_t ptid;			/* "Actual process id";
				    In fact, this may be overloaded with 
				    kernel thread id, etc.  */
  int num;			/* Convenient handle (GDB thread id) */
  /* State from wait_for_inferior */
  CORE_ADDR prev_pc;
  struct breakpoint *step_resume_breakpoint;
  struct breakpoint *through_sigtramp_breakpoint;
  CORE_ADDR step_range_start;
  CORE_ADDR step_range_end;
  struct frame_id step_frame_id;
  CORE_ADDR step_sp;
  int current_line;
  struct symtab *current_symtab;
  int trap_expected;
  int handling_longjmp;
  int another_trap;

  /* This is set TRUE when a catchpoint of a shared library event
     triggers.  Since we don't wish to leave the inferior in the
     solib hook when we report the event, we step the inferior
     back to user code before stopping and reporting the event.  */
  int stepping_through_solib_after_catch;

  /* When stepping_through_solib_after_catch is TRUE, this is a
     list of the catchpoints that should be reported as triggering
     when we finally do stop stepping.  */
  bpstat stepping_through_solib_catchpoints;

  /* This is set to TRUE when this thread is in a signal handler
     trampoline and we're single-stepping through it.  */
  int stepping_through_sigtramp;

  /* Private data used by the target vector implementation.  */
  struct private_thread_info *private;
};

/* Create an empty thread list, or empty the existing one.  */
extern void init_thread_list (void);

/* Add a thread to the thread list.
   Note that add_thread now returns the handle of the new thread,
   so that the caller may initialize the private thread data.  */
extern struct thread_info *add_thread (ptid_t ptid);

/* Delete an existing thread list entry.  */
extern void delete_thread (ptid_t);

/* Delete a step_resume_breakpoint from the thread database. */
extern void delete_step_resume_breakpoint (void *);

/* Translate the integer thread id (GDB's homegrown id, not the system's)
   into a "pid" (which may be overloaded with extra thread information).  */
extern ptid_t thread_id_to_pid (int);

/* Translate a 'pid' (which may be overloaded with extra thread information) 
   into the integer thread id (GDB's homegrown id, not the system's).  */
extern int pid_to_thread_id (ptid_t ptid);

/* Boolean test for an already-known pid (which may be overloaded with
   extra thread information).  */
extern int in_thread_list (ptid_t ptid);

/* Boolean test for an already-known thread id (GDB's homegrown id, 
   not the system's).  */
extern int valid_thread_id (int thread);

/* Search function to lookup a thread by 'pid'.  */
extern struct thread_info *find_thread_pid (ptid_t ptid);

/* Iterator function to call a user-provided callback function
   once for each known thread.  */
typedef int (*thread_callback_func) (struct thread_info *, void *);
extern struct thread_info *iterate_over_threads (thread_callback_func, void *);

/* infrun context switch: save the debugger state for the given thread.  */
extern void save_infrun_state (ptid_t ptid,
			       CORE_ADDR prev_pc,
			       int       trap_expected,
			       struct breakpoint *step_resume_breakpoint,
			       struct breakpoint *through_sigtramp_breakpoint,
			       CORE_ADDR step_range_start,
			       CORE_ADDR step_range_end,
			       const struct frame_id *step_frame_id,
			       int       handling_longjmp,
			       int       another_trap,
			       int       stepping_through_solib_after_catch,
			       bpstat    stepping_through_solib_catchpoints,
			       int       stepping_through_sigtramp,
			       int       current_line,
			       struct symtab *current_symtab,
			       CORE_ADDR step_sp);

/* infrun context switch: load the debugger state previously saved
   for the given thread.  */
extern void load_infrun_state (ptid_t ptid,
			       CORE_ADDR *prev_pc,
			       int       *trap_expected,
			       struct breakpoint **step_resume_breakpoint,
			       struct breakpoint **through_sigtramp_breakpoint,
			       CORE_ADDR *step_range_start,
			       CORE_ADDR *step_range_end,
			       struct frame_id *step_frame_id,
			       int       *handling_longjmp,
			       int       *another_trap,
			       int       *stepping_through_solib_affter_catch,
			       bpstat    *stepping_through_solib_catchpoints,
			       int       *stepping_through_sigtramp,
			       int       *current_line,
			       struct symtab **current_symtab,
			       CORE_ADDR *step_sp);

/* Commands with a prefix of `thread'.  */
extern struct cmd_list_element *thread_cmd_list;

#endif /* GDBTHREAD_H */

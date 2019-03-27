/* Common things used by the various *gnu-nat.c files
   Copyright 1995, 1996, 1997, 1999, 2000 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef __GNU_NAT_H__
#define __GNU_NAT_H__

#include <unistd.h>
#include <mach.h>

struct inf;

extern struct inf *current_inferior;

/* Converts a GDB pid to a struct proc.  */
struct proc *inf_tid_to_thread (struct inf *inf, int tid);

/* Makes sure that INF's thread list is synced with the actual process.  */
int inf_update_procs (struct inf *inf);

/* A proc is either a thread, or the task (there can only be one task proc
   because it always has the same TID, PROC_TID_TASK).  */
struct proc
  {
    thread_t port;		/* The task or thread port.  */
    int tid;			/* The GDB pid (actually a thread id).  */
    int num;			/* An id number for threads, to print.  */

    mach_port_t saved_exc_port;	/* The task/thread's real exception port.  */
    mach_port_t exc_port;	/* Our replacement, which for.  */

    int sc;			/* Desired suspend count.   */
    int cur_sc;			/* Implemented suspend count.  */
    int run_sc;			/* Default sc when the program is running. */
    int pause_sc;		/* Default sc when gdb has control. */
    int resume_sc;		/* Sc resulting from the last resume. */
    int detach_sc;		/* SC to leave around when detaching
				   from program. */

    thread_state_data_t state;	/* Registers, &c. */
    int state_valid:1;		/* True if STATE is up to date. */
    int state_changed:1;

    int aborted:1;		/* True if thread_abort has been called.  */
    int dead:1;			/* We happen to know it's actually dead. */

    /* Bit mask of registers fetched by gdb.  This is used when we re-fetch
       STATE after aborting the thread, to detect that gdb may have out-of-date
       information.  */
    unsigned long fetched_regs;

    struct inf *inf;		/* Where we come from.  */

    struct proc *next;
  };

/* The task has a thread entry with this TID.  */
#define PROC_TID_TASK 	(-1)

#define proc_is_task(proc) ((proc)->tid == PROC_TID_TASK)
#define proc_is_thread(proc) ((proc)->tid != PROC_TID_TASK)

extern int __proc_pid (struct proc *proc);

/* Make sure that the state field in PROC is up to date, and return a
   pointer to it, or 0 if something is wrong.  If WILL_MODIFY is true,
   makes sure that the thread is stopped and aborted first, and sets
   the state_changed field in PROC to true.  */
extern thread_state_t proc_get_state (struct proc *proc, int will_modify);

/* Return printable description of proc.  */
extern char *proc_string (struct proc *proc);

#define proc_debug(_proc, msg, args...) \
  do { struct proc *__proc = (_proc); \
       debug ("{proc %d/%d %p}: " msg, \
	      __proc_pid (__proc), __proc->tid, __proc , ##args); } while (0)

extern int gnu_debug_flag;

#define debug(msg, args...) \
 do { if (gnu_debug_flag) \
        fprintf_unfiltered (gdb_stdlog, "%s:%d: " msg "\r\n", __FILE__ , __LINE__ , ##args); } while (0)

#endif /* __GNU_NAT_H__ */

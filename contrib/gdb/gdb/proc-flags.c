/* Machine independent support for SVR4 /proc (process file system) for GDB.
   Copyright 1999, 2000 Free Software Foundation, Inc.
   Written by Michael Snyder at Cygnus Solutions.
   Based on work by Fred Fish, Stu Grossman, Geoff Noer, and others.

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
along with this program; if not, write to the Free Software Foundation, 
Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/*
 * Pretty-print the prstatus flags.
 * 
 * Arguments: unsigned long flags, int verbose
 *
 */

#include "defs.h"

#if defined (NEW_PROC_API)
#define _STRUCTURED_PROC 1
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/procfs.h>

/*  Much of the information used in the /proc interface, particularly for
    printing status information, is kept as tables of structures of the
    following form.  These tables can be used to map numeric values to
    their symbolic names and to a string that describes their specific use. */

struct trans {
  int value;                    /* The numeric value */
  char *name;                   /* The equivalent symbolic value */
  char *desc;                   /* Short description of value */
};

/* Translate bits in the pr_flags member of the prstatus structure,
   into the names and desc information. */

static struct trans pr_flag_table[] =
{
#if defined (PR_STOPPED)
  /* Sol2.5: lwp is stopped
   * Sol2.6: lwp is stopped
   * Sol2.7: lwp is stopped
   * IRIX6:  process is stopped
   * OSF:    task/thread is stopped
   * UW:     LWP is stopped
   */
  { PR_STOPPED, "PR_STOPPED", "Process (LWP) is stopped" },
#endif
#if defined (PR_ISTOP)
  /* Sol2.5: lwp is stopped on an event of interest
   * Sol2.6: lwp is stopped on an event of interest
   * Sol2.7: lwp is stopped on an event of interest
   * IRIX6:  process is stopped on event of interest
   * OSF:    task/thread stopped on event of interest
   * UW:     LWP stopped on an event of interest
   */
  { PR_ISTOP, "PR_ISTOP", "Stopped on an event of interest" },
#endif
#if defined (PR_DSTOP)
  /* Sol2.5: lwp has a stop directive in effect
   * Sol2.6: lwp has a stop directive in effect
   * Sol2.7: lwp has a stop directive in effect
   * IRIX6:  process has stop directive in effect
   * OSF:    task/thread has stop directive in effect
   * UW:     A stop directive is in effect
   */
  { PR_DSTOP, "PR_DSTOP", "A stop directive is in effect" },
#endif
#if defined (PR_STEP)
  /* Sol2.5: lwp has a single-step directive in effect
   * Sol2.6: lwp has a single-step directive in effect
   * Sol2.7: lwp has a single-step directive in effect
   * IRIX6:  process has single step pending
   */
  { PR_STEP, "PR_STEP", "A single step directive is in effect" },
#endif
#if defined (PR_ASLEEP)
  /* Sol2.5: lwp is sleeping in a system call
   * Sol2.6: lwp is sleeping in a system call
   * Sol2.7: lwp is sleeping in a system call
   * IRIX6:  process is in an interruptible sleep
   * OSF:    task/thread is asleep within a system call
   * UW:     LWP is sleep()ing in a system call
   */
  { PR_ASLEEP, "PR_ASLEEP", "Sleeping in an (interruptible) system call" },
#endif
#if defined (PR_PCINVAL)
  /* Sol2.5: contents of pr_instr undefined
   * Sol2.6: contents of pr_instr undefined
   * Sol2.7: contents of pr_instr undefined
   * IRIX6:  current pc is invalid
   * OSF:    program counter contains invalid address
   * UW:     %pc refers to an invalid virtual address
   */
  { PR_PCINVAL, "PR_PCINVAL", "PC (pr_instr) is invalid" },
#endif
#if defined (PR_ASLWP)
  /* Sol2.5: this lwp is the aslwp
   * Sol2.6: this lwp is the aslwp
   * Sol2.7: this lwp is the aslwp
   */
  { PR_ASLWP, "PR_ASLWP", "This is the asynchronous signal LWP" },
#endif
#if defined (PR_AGENT)
  /* Sol2.6: this lwp is the /proc agent lwp
   * Sol2.7: this lwp is the /proc agent lwp
   */
  { PR_AGENT, "PR_AGENT", "This is the /proc agent LWP" },
#endif
#if defined (PR_ISSYS)
  /* Sol2.5: system process
   * Sol2.6: this is a system process
   * Sol2.7: this is a system process
   * IRIX6:  process is a system process
   * OSF:    task/thread is a system task/thread
   * UW:     System process
   */
  { PR_ISSYS, "PR_ISSYS", "Is a system process/thread" },
#endif
#if defined (PR_VFORKP)
  /* Sol2.6: process is the parent of a vfork()d child
   * Sol2.7: process is the parent of a vfork()d child
   */
  { PR_VFORKP, "PR_VFORKP", "Process is the parent of a vforked child" },
#endif
#ifdef PR_ORPHAN
  /* Sol2.6: process's process group is orphaned
   * Sol2.7: process's process group is orphaned
   */
  { PR_ORPHAN, "PR_ORPHAN", "Process's process group is orphaned" },
#endif
#if defined (PR_FORK)
  /* Sol2.5: inherit-on-fork is in effect
   * Sol2.6: inherit-on-fork is in effect
   * Sol2.7: inherit-on-fork is in effect
   * IRIX6:  process has inherit-on-fork flag set
   * OSF:    task/thread has inherit-on-fork flag set
   * UW:     inherit-on-fork is in effect
   */
  { PR_FORK, "PR_FORK", "Inherit-on-fork is in effect" },
#endif
#if defined (PR_RLC)
  /* Sol2.5: run-on-last-close is in effect
   * Sol2.6: run-on-last-close is in effect
   * Sol2.7: run-on-last-close is in effect
   * IRIX6:  process has run-on-last-close flag set
   * OSF:    task/thread has run-on-last-close flag set
   * UW:     Run-on-last-close is in effect
   */
  { PR_RLC, "PR_RLC", "Run-on-last-close is in effect" },
#endif
#if defined (PR_KLC)
  /* Sol2.5: kill-on-last-close is in effect
   * Sol2.6: kill-on-last-close is in effect
   * Sol2.7: kill-on-last-close is in effect
   * IRIX6:  process has kill-on-last-close flag set
   * OSF:    kill-on-last-close, superceeds RLC
   * UW:     kill-on-last-close is in effect
   */
  { PR_KLC, "PR_KLC", "Kill-on-last-close is in effect" },
#endif
#if defined (PR_ASYNC)
  /* Sol2.5: asynchronous-stop is in effect
   * Sol2.6: asynchronous-stop is in effect
   * Sol2.7: asynchronous-stop is in effect
   * OSF:    asynchronous stop mode is in effect
   * UW:     asynchronous stop mode is in effect
   */
  { PR_ASYNC, "PR_ASYNC", "Asynchronous stop is in effect" },
#endif
#if defined (PR_MSACCT)
  /* Sol2.5: micro-state usage accounting is in effect
   * Sol2.6: micro-state usage accounting is in effect
   * Sol2.7: micro-state usage accounting is in effect
   */
  { PR_MSACCT, "PR_MSACCT", "Microstate accounting enabled" },
#endif
#if defined (PR_BPTADJ)
  /* Sol2.5: breakpoint trap pc adjustment is in effect
   * Sol2.6: breakpoint trap pc adjustment is in effect
   * Sol2.7: breakpoint trap pc adjustment is in effect
   */
  { PR_BPTADJ, "PR_BPTADJ", "Breakpoint PC adjustment in effect" },
#endif
#if defined (PR_PTRACE)
  /* Note: different meanings on Solaris and Irix 6
   * Sol2.5: obsolete, never set in SunOS5.0
   * Sol2.6: ptrace-compatibility mode is in effect
   * Sol2.7: ptrace-compatibility mode is in effect
   * IRIX6:  process is traced with ptrace() too
   * OSF:    task/thread is being traced by ptrace
   * UW:     Process is being controlled by ptrace(2)
   */
  { PR_PTRACE, "PR_PTRACE", "Process is being controlled by ptrace" },
#endif
#if defined (PR_PCOMPAT)
  /* Note: PCOMPAT on Sol2.5 means same thing as PTRACE on Sol2.6
   * Sol2.5 (only): ptrace-compatibility mode is in effect
   */
  { PR_PCOMPAT, "PR_PCOMPAT", "Ptrace compatibility mode in effect" },
#endif
#ifdef PR_MSFORK
  /* Sol2.6: micro-state accounting inherited on fork
   * Sol2.7: micro-state accounting inherited on fork
   */
  { PR_MSFORK, "PR_PCOMPAT", "Micro-state accounting inherited on fork" },
#endif

#ifdef PR_ISKTHREAD
  /* Irix6: process is a kernel thread */
  { PR_ISKTHREAD, "PR_KTHREAD", "Process is a kernel thread" },
#endif

#ifdef PR_ABORT
  /* OSF (only): abort the current stop condition */
  { PR_ABORT, "PR_ABORT", "Abort the current stop condition" },
#endif

#ifdef PR_TRACING
  /* OSF: task is traced */
  { PR_TRACING, "PR_TRACING", "Task is traced" },
#endif

#ifdef PR_STOPFORK
  /* OSF: stop child on fork */
  { PR_STOPFORK, "PR_STOPFORK", "Stop child on fork" },
#endif

#ifdef PR_STOPEXEC
  /* OSF: stop on exec */
  { PR_STOPEXEC, "PR_STOPEXEC", "Stop on exec" },
#endif

#ifdef PR_STOPTERM
  /* OSF: stop on task exit */
  { PR_STOPTERM, "PR_STOPTERM", "Stop on task exit" },
#endif

#ifdef PR_STOPTCR
  /* OSF: stop on thread creation */
  { PR_STOPTCR, "PR_STOPTCR", "Stop on thread creation" },
#endif

#ifdef PR_STOPTTERM
  /* OSF: stop on thread exit */
  { PR_STOPTTERM, "PR_STOPTTERM", "Stop on thread exit" },
#endif

#ifdef PR_USCHED
  /* OSF: user level scheduling is in effect */
  { PR_USCHED, "PR_USCHED", "User level scheduling is in effect" },
#endif
};

void
proc_prettyfprint_flags (FILE *file, unsigned long flags, int verbose)
{
  int i;

  for (i = 0; i < sizeof (pr_flag_table) / sizeof (pr_flag_table[0]); i++)
    if (flags & pr_flag_table[i].value)
      {
	fprintf (file, "%s ", pr_flag_table[i].name);
	if (verbose)
	  fprintf (file, "%s\n", pr_flag_table[i].desc);
      }
  if (!verbose)
    fprintf (file, "\n");
}

void
proc_prettyprint_flags (unsigned long flags, int verbose)
{
  proc_prettyfprint_flags (stdout, flags, verbose);
}

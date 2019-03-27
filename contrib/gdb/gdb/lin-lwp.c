/* Multi-threaded debugging support for GNU/Linux (LWP layer).
   Copyright 2000, 2001, 2002, 2003 Free Software Foundation, Inc.

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

#include "gdb_assert.h"
#include "gdb_string.h"
#include <errno.h>
#include <signal.h>
#ifdef HAVE_TKILL_SYSCALL
#include <unistd.h>
#include <sys/syscall.h>
#endif
#include <sys/ptrace.h>
#include "gdb_wait.h"

#include "gdbthread.h"
#include "inferior.h"
#include "target.h"
#include "regcache.h"
#include "gdbcmd.h"

static int debug_lin_lwp;
extern char *strsignal (int sig);

#include "linux-nat.h"

/* On GNU/Linux there are no real LWP's.  The closest thing to LWP's
   are processes sharing the same VM space.  A multi-threaded process
   is basically a group of such processes.  However, such a grouping
   is almost entirely a user-space issue; the kernel doesn't enforce
   such a grouping at all (this might change in the future).  In
   general, we'll rely on the threads library (i.e. the GNU/Linux
   Threads library) to provide such a grouping.

   It is perfectly well possible to write a multi-threaded application
   without the assistance of a threads library, by using the clone
   system call directly.  This module should be able to give some
   rudimentary support for debugging such applications if developers
   specify the CLONE_PTRACE flag in the clone system call, and are
   using the Linux kernel 2.4 or above.

   Note that there are some peculiarities in GNU/Linux that affect
   this code:

   - In general one should specify the __WCLONE flag to waitpid in
     order to make it report events for any of the cloned processes
     (and leave it out for the initial process).  However, if a cloned
     process has exited the exit status is only reported if the
     __WCLONE flag is absent.  Linux kernel 2.4 has a __WALL flag, but
     we cannot use it since GDB must work on older systems too.

   - When a traced, cloned process exits and is waited for by the
     debugger, the kernel reassigns it to the original parent and
     keeps it around as a "zombie".  Somehow, the GNU/Linux Threads
     library doesn't notice this, which leads to the "zombie problem":
     When debugged a multi-threaded process that spawns a lot of
     threads will run out of processes, even if the threads exit,
     because the "zombies" stay around.  */

/* List of known LWPs.  */
static struct lwp_info *lwp_list;

/* Number of LWPs in the list.  */
static int num_lwps;

/* Non-zero if we're running in "threaded" mode.  */
static int threaded;


#define GET_LWP(ptid)		ptid_get_lwp (ptid)
#define GET_PID(ptid)		ptid_get_pid (ptid)
#define is_lwp(ptid)		(GET_LWP (ptid) != 0)
#define BUILD_LWP(lwp, pid)	ptid_build (pid, lwp, 0)

/* If the last reported event was a SIGTRAP, this variable is set to
   the process id of the LWP/thread that got it.  */
ptid_t trap_ptid;


/* This module's target-specific operations.  */
static struct target_ops lin_lwp_ops;

/* The standard child operations.  */
extern struct target_ops child_ops;

/* Since we cannot wait (in lin_lwp_wait) for the initial process and
   any cloned processes with a single call to waitpid, we have to use
   the WNOHANG flag and call waitpid in a loop.  To optimize
   things a bit we use `sigsuspend' to wake us up when a process has
   something to report (it will send us a SIGCHLD if it has).  To make
   this work we have to juggle with the signal mask.  We save the
   original signal mask such that we can restore it before creating a
   new process in order to avoid blocking certain signals in the
   inferior.  We then block SIGCHLD during the waitpid/sigsuspend
   loop.  */

/* Original signal mask.  */
static sigset_t normal_mask;

/* Signal mask for use with sigsuspend in lin_lwp_wait, initialized in
   _initialize_lin_lwp.  */
static sigset_t suspend_mask;

/* Signals to block to make that sigsuspend work.  */
static sigset_t blocked_mask;


/* Prototypes for local functions.  */
static int stop_wait_callback (struct lwp_info *lp, void *data);
static int lin_lwp_thread_alive (ptid_t ptid);

/* Convert wait status STATUS to a string.  Used for printing debug
   messages only.  */

static char *
status_to_str (int status)
{
  static char buf[64];

  if (WIFSTOPPED (status))
    snprintf (buf, sizeof (buf), "%s (stopped)",
	      strsignal (WSTOPSIG (status)));
  else if (WIFSIGNALED (status))
    snprintf (buf, sizeof (buf), "%s (terminated)",
	      strsignal (WSTOPSIG (status)));
  else
    snprintf (buf, sizeof (buf), "%d (exited)", WEXITSTATUS (status));

  return buf;
}

/* Initialize the list of LWPs.  Note that this module, contrary to
   what GDB's generic threads layer does for its thread list,
   re-initializes the LWP lists whenever we mourn or detach (which
   doesn't involve mourning) the inferior.  */

static void
init_lwp_list (void)
{
  struct lwp_info *lp, *lpnext;

  for (lp = lwp_list; lp; lp = lpnext)
    {
      lpnext = lp->next;
      xfree (lp);
    }

  lwp_list = NULL;
  num_lwps = 0;
  threaded = 0;
}

/* Add the LWP specified by PID to the list.  If this causes the
   number of LWPs to become larger than one, go into "threaded" mode.
   Return a pointer to the structure describing the new LWP.  */

static struct lwp_info *
add_lwp (ptid_t ptid)
{
  struct lwp_info *lp;

  gdb_assert (is_lwp (ptid));

  lp = (struct lwp_info *) xmalloc (sizeof (struct lwp_info));

  memset (lp, 0, sizeof (struct lwp_info));

  lp->ptid = ptid;

  lp->next = lwp_list;
  lwp_list = lp;
  if (++num_lwps > 1)
    threaded = 1;

  return lp;
}

/* Remove the LWP specified by PID from the list.  */

static void
delete_lwp (ptid_t ptid)
{
  struct lwp_info *lp, *lpprev;

  lpprev = NULL;

  for (lp = lwp_list; lp; lpprev = lp, lp = lp->next)
    if (ptid_equal (lp->ptid, ptid))
      break;

  if (!lp)
    return;

  /* We don't go back to "non-threaded" mode if the number of threads
     becomes less than two.  */
  num_lwps--;

  if (lpprev)
    lpprev->next = lp->next;
  else
    lwp_list = lp->next;

  xfree (lp);
}

/* Return a pointer to the structure describing the LWP corresponding
   to PID.  If no corresponding LWP could be found, return NULL.  */

static struct lwp_info *
find_lwp_pid (ptid_t ptid)
{
  struct lwp_info *lp;
  int lwp;

  if (is_lwp (ptid))
    lwp = GET_LWP (ptid);
  else
    lwp = GET_PID (ptid);

  for (lp = lwp_list; lp; lp = lp->next)
    if (lwp == GET_LWP (lp->ptid))
      return lp;

  return NULL;
}

/* Call CALLBACK with its second argument set to DATA for every LWP in
   the list.  If CALLBACK returns 1 for a particular LWP, return a
   pointer to the structure describing that LWP immediately.
   Otherwise return NULL.  */

struct lwp_info *
iterate_over_lwps (int (*callback) (struct lwp_info *, void *), void *data)
{
  struct lwp_info *lp, *lpnext;

  for (lp = lwp_list; lp; lp = lpnext)
    {
      lpnext = lp->next;
      if ((*callback) (lp, data))
	return lp;
    }

  return NULL;
}


#if 0
static void
lin_lwp_open (char *args, int from_tty)
{
  push_target (&lin_lwp_ops);
}
#endif

/* Attach to the LWP specified by PID.  If VERBOSE is non-zero, print
   a message telling the user that a new LWP has been added to the
   process.  */

void
lin_lwp_attach_lwp (ptid_t ptid, int verbose)
{
  struct lwp_info *lp;

  gdb_assert (is_lwp (ptid));

  /* Make sure SIGCHLD is blocked.  We don't want SIGCHLD events
     to interrupt either the ptrace() or waitpid() calls below.  */
  if (!sigismember (&blocked_mask, SIGCHLD))
    {
      sigaddset (&blocked_mask, SIGCHLD);
      sigprocmask (SIG_BLOCK, &blocked_mask, NULL);
    }

  if (verbose)
    printf_filtered ("[New %s]\n", target_pid_to_str (ptid));

  lp = find_lwp_pid (ptid);
  if (lp == NULL)
    lp = add_lwp (ptid);

  /* We assume that we're already attached to any LWP that has an
     id equal to the overall process id.  */
  if (GET_LWP (ptid) != GET_PID (ptid))
    {
      pid_t pid;
      int status;

      if (ptrace (PTRACE_ATTACH, GET_LWP (ptid), 0, 0) < 0)
	error ("Can't attach %s: %s", target_pid_to_str (ptid),
	       safe_strerror (errno));

      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stdlog,
			    "LLAL: PTRACE_ATTACH %s, 0, 0 (OK)\n",
			    target_pid_to_str (ptid));

      pid = waitpid (GET_LWP (ptid), &status, 0);
      if (pid == -1 && errno == ECHILD)
	{
	  /* Try again with __WCLONE to check cloned processes.  */
	  pid = waitpid (GET_LWP (ptid), &status, __WCLONE);
	  lp->cloned = 1;
	}

      gdb_assert (pid == GET_LWP (ptid)
		  && WIFSTOPPED (status) && WSTOPSIG (status));

      child_post_attach (pid);

      lp->stopped = 1;

      if (debug_lin_lwp)
	{
	  fprintf_unfiltered (gdb_stdlog,
			      "LLAL: waitpid %s received %s\n",
			      target_pid_to_str (ptid),
			      status_to_str (status));
	}
    }
  else
    {
      /* We assume that the LWP representing the original process
         is already stopped.  Mark it as stopped in the data structure
         that the lin-lwp layer uses to keep track of threads.  Note
         that this won't have already been done since the main thread
         will have, we assume, been stopped by an attach from a
         different layer.  */
      lp->stopped = 1;
    }
}

static void
lin_lwp_attach (char *args, int from_tty)
{
  struct lwp_info *lp;
  pid_t pid;
  int status;

  /* FIXME: We should probably accept a list of process id's, and
     attach all of them.  */
  child_ops.to_attach (args, from_tty);

  /* Add the initial process as the first LWP to the list.  */
  lp = add_lwp (BUILD_LWP (GET_PID (inferior_ptid), GET_PID (inferior_ptid)));

  /* Make sure the initial process is stopped.  The user-level threads
     layer might want to poke around in the inferior, and that won't
     work if things haven't stabilized yet.  */
  pid = waitpid (GET_PID (inferior_ptid), &status, 0);
  if (pid == -1 && errno == ECHILD)
    {
      warning ("%s is a cloned process", target_pid_to_str (inferior_ptid));

      /* Try again with __WCLONE to check cloned processes.  */
      pid = waitpid (GET_PID (inferior_ptid), &status, __WCLONE);
      lp->cloned = 1;
    }

  gdb_assert (pid == GET_PID (inferior_ptid)
	      && WIFSTOPPED (status) && WSTOPSIG (status) == SIGSTOP);

  lp->stopped = 1;

  /* Fake the SIGSTOP that core GDB expects.  */
  lp->status = W_STOPCODE (SIGSTOP);
  lp->resumed = 1;
  if (debug_lin_lwp)
    {
      fprintf_unfiltered (gdb_stdlog,
			  "LLA: waitpid %ld, faking SIGSTOP\n", (long) pid);
    }
}

static int
detach_callback (struct lwp_info *lp, void *data)
{
  gdb_assert (lp->status == 0 || WIFSTOPPED (lp->status));

  if (debug_lin_lwp && lp->status)
    fprintf_unfiltered (gdb_stdlog, "DC:  Pending %s for %s on detach.\n",
			strsignal (WSTOPSIG (lp->status)),
			target_pid_to_str (lp->ptid));

  while (lp->signalled && lp->stopped)
    {
      errno = 0;
      if (ptrace (PTRACE_CONT, GET_LWP (lp->ptid), 0,
		  WSTOPSIG (lp->status)) < 0)
	error ("Can't continue %s: %s", target_pid_to_str (lp->ptid),
	       safe_strerror (errno));

      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stdlog,
			    "DC:  PTRACE_CONTINUE (%s, 0, %s) (OK)\n",
			    target_pid_to_str (lp->ptid),
			    status_to_str (lp->status));

      lp->stopped = 0;
      lp->signalled = 0;
      lp->status = 0;
      /* FIXME drow/2003-08-26: There was a call to stop_wait_callback
	 here.  But since lp->signalled was cleared above,
	 stop_wait_callback didn't do anything; the process was left
	 running.  Shouldn't we be waiting for it to stop?
	 I've removed the call, since stop_wait_callback now does do
	 something when called with lp->signalled == 0.  */

      gdb_assert (lp->status == 0 || WIFSTOPPED (lp->status));
    }

  /* We don't actually detach from the LWP that has an id equal to the
     overall process id just yet.  */
  if (GET_LWP (lp->ptid) != GET_PID (lp->ptid))
    {
      errno = 0;
      if (ptrace (PTRACE_DETACH, GET_LWP (lp->ptid), 0,
		  WSTOPSIG (lp->status)) < 0)
	error ("Can't detach %s: %s", target_pid_to_str (lp->ptid),
	       safe_strerror (errno));

      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stdlog,
			    "PTRACE_DETACH (%s, %s, 0) (OK)\n",
			    target_pid_to_str (lp->ptid),
			    strsignal (WSTOPSIG (lp->status)));

      delete_lwp (lp->ptid);
    }

  return 0;
}

static void
lin_lwp_detach (char *args, int from_tty)
{
  iterate_over_lwps (detach_callback, NULL);

  /* Only the initial process should be left right now.  */
  gdb_assert (num_lwps == 1);

  trap_ptid = null_ptid;

  /* Destroy LWP info; it's no longer valid.  */
  init_lwp_list ();

  /* Restore the original signal mask.  */
  sigprocmask (SIG_SETMASK, &normal_mask, NULL);
  sigemptyset (&blocked_mask);

  inferior_ptid = pid_to_ptid (GET_PID (inferior_ptid));
  child_ops.to_detach (args, from_tty);
}


/* Resume LP.  */

static int
resume_callback (struct lwp_info *lp, void *data)
{
  if (lp->stopped && lp->status == 0)
    {
      struct thread_info *tp;

      child_resume (pid_to_ptid (GET_LWP (lp->ptid)), 0, TARGET_SIGNAL_0);
      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stdlog,
			    "RC:  PTRACE_CONT %s, 0, 0 (resume sibling)\n",
			    target_pid_to_str (lp->ptid));
      lp->stopped = 0;
      lp->step = 0;
    }

  return 0;
}

static int
resume_clear_callback (struct lwp_info *lp, void *data)
{
  lp->resumed = 0;
  return 0;
}

static int
resume_set_callback (struct lwp_info *lp, void *data)
{
  lp->resumed = 1;
  return 0;
}

static void
lin_lwp_resume (ptid_t ptid, int step, enum target_signal signo)
{
  struct lwp_info *lp;
  int resume_all;

  /* A specific PTID means `step only this process id'.  */
  resume_all = (PIDGET (ptid) == -1);

  if (resume_all)
    iterate_over_lwps (resume_set_callback, NULL);
  else
    iterate_over_lwps (resume_clear_callback, NULL);

  /* If PID is -1, it's the current inferior that should be
     handled specially.  */
  if (PIDGET (ptid) == -1)
    ptid = inferior_ptid;

  lp = find_lwp_pid (ptid);
  if (lp)
    {
      ptid = pid_to_ptid (GET_LWP (lp->ptid));

      /* Remember if we're stepping.  */
      lp->step = step;

      /* Mark this LWP as resumed.  */
      lp->resumed = 1;

      /* If we have a pending wait status for this thread, there is no
         point in resuming the process.  */
      if (lp->status)
	{
	  /* FIXME: What should we do if we are supposed to continue
	     this thread with a signal?  */
	  gdb_assert (signo == TARGET_SIGNAL_0);
	  return;
	}

      /* Mark LWP as not stopped to prevent it from being continued by
         resume_callback.  */
      lp->stopped = 0;
    }

  if (resume_all)
    iterate_over_lwps (resume_callback, NULL);

  child_resume (ptid, step, signo);
  if (debug_lin_lwp)
    fprintf_unfiltered (gdb_stdlog,
			"LLR: %s %s, %s (resume event thread)\n",
			step ? "PTRACE_SINGLESTEP" : "PTRACE_CONT",
			target_pid_to_str (ptid),
			signo ? strsignal (signo) : "0");
}


/* Issue kill to specified lwp.  */

static int tkill_failed;

static int
kill_lwp (int lwpid, int signo)
{
  errno = 0;

/* Use tkill, if possible, in case we are using nptl threads.  If tkill
   fails, then we are not using nptl threads and we should be using kill.  */

#ifdef HAVE_TKILL_SYSCALL
  if (!tkill_failed)
    {
      int ret = syscall (__NR_tkill, lwpid, signo);
      if (errno != ENOSYS)
	return ret;
      errno = 0;
      tkill_failed = 1;
    }
#endif

  return kill (lwpid, signo);
}

/* Wait for LP to stop.  Returns the wait status, or 0 if the LWP has
   exited.  */

static int
wait_lwp (struct lwp_info *lp)
{
  pid_t pid;
  int status;
  int thread_dead = 0;

  gdb_assert (!lp->stopped);
  gdb_assert (lp->status == 0);

  pid = waitpid (GET_LWP (lp->ptid), &status, 0);
  if (pid == -1 && errno == ECHILD)
    {
      pid = waitpid (GET_LWP (lp->ptid), &status, __WCLONE);
      if (pid == -1 && errno == ECHILD)
	{
	  /* The thread has previously exited.  We need to delete it now
	     because in the case of NPTL threads, there won't be an
	     exit event unless it is the main thread.  */
	  thread_dead = 1;
	  if (debug_lin_lwp)
	    fprintf_unfiltered (gdb_stdlog, "WL: %s vanished.\n",
				target_pid_to_str (lp->ptid));
	}
    }

  if (!thread_dead)
    {
      gdb_assert (pid == GET_LWP (lp->ptid));

      if (debug_lin_lwp)
	{
	  fprintf_unfiltered (gdb_stdlog,
			      "WL: waitpid %s received %s\n",
			      target_pid_to_str (lp->ptid),
			      status_to_str (status));
	}
    }

  /* Check if the thread has exited.  */
  if (WIFEXITED (status) || WIFSIGNALED (status))
    {
      thread_dead = 1;
      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stdlog, "WL: %s exited.\n",
			    target_pid_to_str (lp->ptid));
    }

  if (thread_dead)
    {
      if (in_thread_list (lp->ptid))
	{
	  /* Core GDB cannot deal with us deleting the current thread.  */
	  if (!ptid_equal (lp->ptid, inferior_ptid))
	    delete_thread (lp->ptid);
	  printf_unfiltered ("[%s exited]\n",
			     target_pid_to_str (lp->ptid));
	}

      delete_lwp (lp->ptid);
      return 0;
    }

  gdb_assert (WIFSTOPPED (status));

  return status;
}

/* Send a SIGSTOP to LP.  */

static int
stop_callback (struct lwp_info *lp, void *data)
{
  if (!lp->stopped && !lp->signalled)
    {
      int ret;

      if (debug_lin_lwp)
	{
	  fprintf_unfiltered (gdb_stdlog,
			      "SC:  kill %s **<SIGSTOP>**\n",
			      target_pid_to_str (lp->ptid));
	}
      errno = 0;
      ret = kill_lwp (GET_LWP (lp->ptid), SIGSTOP);
      if (debug_lin_lwp)
	{
	  fprintf_unfiltered (gdb_stdlog,
			      "SC:  lwp kill %d %s\n",
			      ret,
			      errno ? safe_strerror (errno) : "ERRNO-OK");
	}

      lp->signalled = 1;
      gdb_assert (lp->status == 0);
    }

  return 0;
}

/* Wait until LP is stopped.  If DATA is non-null it is interpreted as
   a pointer to a set of signals to be flushed immediately.  */

static int
stop_wait_callback (struct lwp_info *lp, void *data)
{
  sigset_t *flush_mask = data;

  if (!lp->stopped)
    {
      int status;

      status = wait_lwp (lp);
      if (status == 0)
	return 0;

      /* Ignore any signals in FLUSH_MASK.  */
      if (flush_mask && sigismember (flush_mask, WSTOPSIG (status)))
	{
	  if (!lp->signalled)
	    {
	      lp->stopped = 1;
	      return 0;
	    }

	  errno = 0;
	  ptrace (PTRACE_CONT, GET_LWP (lp->ptid), 0, 0);
	  if (debug_lin_lwp)
	    fprintf_unfiltered (gdb_stdlog,
				"PTRACE_CONT %s, 0, 0 (%s)\n",
				target_pid_to_str (lp->ptid),
				errno ? safe_strerror (errno) : "OK");

	  return stop_wait_callback (lp, flush_mask);
	}

      if (WSTOPSIG (status) != SIGSTOP)
	{
	  if (WSTOPSIG (status) == SIGTRAP)
	    {
	      /* If a LWP other than the LWP that we're reporting an
	         event for has hit a GDB breakpoint (as opposed to
	         some random trap signal), then just arrange for it to
	         hit it again later.  We don't keep the SIGTRAP status
	         and don't forward the SIGTRAP signal to the LWP.  We
	         will handle the current event, eventually we will
	         resume all LWPs, and this one will get its breakpoint
	         trap again.

	         If we do not do this, then we run the risk that the
	         user will delete or disable the breakpoint, but the
	         thread will have already tripped on it.  */

	      /* Now resume this LWP and get the SIGSTOP event. */
	      errno = 0;
	      ptrace (PTRACE_CONT, GET_LWP (lp->ptid), 0, 0);
	      if (debug_lin_lwp)
		{
		  fprintf_unfiltered (gdb_stdlog,
				      "PTRACE_CONT %s, 0, 0 (%s)\n",
				      target_pid_to_str (lp->ptid),
				      errno ? safe_strerror (errno) : "OK");

		  fprintf_unfiltered (gdb_stdlog,
				      "SWC: Candidate SIGTRAP event in %s\n",
				      target_pid_to_str (lp->ptid));
		}
	      /* Hold the SIGTRAP for handling by lin_lwp_wait. */
	      stop_wait_callback (lp, data);
	      /* If there's another event, throw it back into the queue. */
	      if (lp->status)
		{
		  if (debug_lin_lwp)
		    {
		      fprintf_unfiltered (gdb_stdlog,
					  "SWC: kill %s, %s\n",
					  target_pid_to_str (lp->ptid),
					  status_to_str ((int) status));
		    }
		  kill_lwp (GET_LWP (lp->ptid), WSTOPSIG (lp->status));
		}
	      /* Save the sigtrap event. */
	      lp->status = status;
	      return 0;
	    }
	  else
	    {
	      /* The thread was stopped with a signal other than
	         SIGSTOP, and didn't accidentally trip a breakpoint. */

	      if (debug_lin_lwp)
		{
		  fprintf_unfiltered (gdb_stdlog,
				      "SWC: Pending event %s in %s\n",
				      status_to_str ((int) status),
				      target_pid_to_str (lp->ptid));
		}
	      /* Now resume this LWP and get the SIGSTOP event. */
	      errno = 0;
	      ptrace (PTRACE_CONT, GET_LWP (lp->ptid), 0, 0);
	      if (debug_lin_lwp)
		fprintf_unfiltered (gdb_stdlog,
				    "SWC: PTRACE_CONT %s, 0, 0 (%s)\n",
				    target_pid_to_str (lp->ptid),
				    errno ? safe_strerror (errno) : "OK");

	      /* Hold this event/waitstatus while we check to see if
	         there are any more (we still want to get that SIGSTOP). */
	      stop_wait_callback (lp, data);
	      /* If the lp->status field is still empty, use it to hold
	         this event.  If not, then this event must be returned
	         to the event queue of the LWP.  */
	      if (lp->status == 0)
		lp->status = status;
	      else
		{
		  if (debug_lin_lwp)
		    {
		      fprintf_unfiltered (gdb_stdlog,
					  "SWC: kill %s, %s\n",
					  target_pid_to_str (lp->ptid),
					  status_to_str ((int) status));
		    }
		  kill_lwp (GET_LWP (lp->ptid), WSTOPSIG (status));
		}
	      return 0;
	    }
	}
      else
	{
	  /* We caught the SIGSTOP that we intended to catch, so
	     there's no SIGSTOP pending.  */
	  lp->stopped = 1;
	  lp->signalled = 0;
	}
    }

  return 0;
}

/* Check whether PID has any pending signals in FLUSH_MASK.  If so set
   the appropriate bits in PENDING, and return 1 - otherwise return 0.  */

static int
lin_lwp_has_pending (int pid, sigset_t *pending, sigset_t *flush_mask)
{
  sigset_t blocked, ignored;
  int i;

  linux_proc_pending_signals (pid, pending, &blocked, &ignored);

  if (!flush_mask)
    return 0;

  for (i = 1; i < NSIG; i++)
    if (sigismember (pending, i))
      if (!sigismember (flush_mask, i)
	  || sigismember (&blocked, i)
	  || sigismember (&ignored, i))
	sigdelset (pending, i);

  if (sigisemptyset (pending))
    return 0;

  return 1;
}

/* DATA is interpreted as a mask of signals to flush.  If LP has
   signals pending, and they are all in the flush mask, then arrange
   to flush them.  LP should be stopped, as should all other threads
   it might share a signal queue with.  */

static int
flush_callback (struct lwp_info *lp, void *data)
{
  sigset_t *flush_mask = data;
  sigset_t pending, intersection, blocked, ignored;
  int pid, status;

  /* Normally, when an LWP exits, it is removed from the LWP list.  The
     last LWP isn't removed till later, however.  So if there is only
     one LWP on the list, make sure it's alive.  */
  if (lwp_list == lp && lp->next == NULL)
    if (!lin_lwp_thread_alive (lp->ptid))
      return 0;

  /* Just because the LWP is stopped doesn't mean that new signals
     can't arrive from outside, so this function must be careful of
     race conditions.  However, because all threads are stopped, we
     can assume that the pending mask will not shrink unless we resume
     the LWP, and that it will then get another signal.  We can't
     control which one, however.  */

  if (lp->status)
    {
      if (debug_lin_lwp)
	printf_unfiltered ("FC: LP has pending status %06x\n", lp->status);
      if (WIFSTOPPED (lp->status) && sigismember (flush_mask, WSTOPSIG (lp->status)))
	lp->status = 0;
    }

  while (lin_lwp_has_pending (GET_LWP (lp->ptid), &pending, flush_mask))
    {
      int ret;
      
      errno = 0;
      ret = ptrace (PTRACE_CONT, GET_LWP (lp->ptid), 0, 0);
      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stderr,
			    "FC: Sent PTRACE_CONT, ret %d %d\n", ret, errno);

      lp->stopped = 0;
      stop_wait_callback (lp, flush_mask);
      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stderr,
			    "FC: Wait finished; saved status is %d\n",
			    lp->status);
    }

  return 0;
}

/* Return non-zero if LP has a wait status pending.  */

static int
status_callback (struct lwp_info *lp, void *data)
{
  /* Only report a pending wait status if we pretend that this has
     indeed been resumed.  */
  return (lp->status != 0 && lp->resumed);
}

/* Return non-zero if LP isn't stopped.  */

static int
running_callback (struct lwp_info *lp, void *data)
{
  return (lp->stopped == 0 || (lp->status != 0 && lp->resumed));
}

/* Count the LWP's that have had events.  */

static int
count_events_callback (struct lwp_info *lp, void *data)
{
  int *count = data;

  gdb_assert (count != NULL);

  /* Count only LWPs that have a SIGTRAP event pending.  */
  if (lp->status != 0
      && WIFSTOPPED (lp->status) && WSTOPSIG (lp->status) == SIGTRAP)
    (*count)++;

  return 0;
}

/* Select the LWP (if any) that is currently being single-stepped.  */

static int
select_singlestep_lwp_callback (struct lwp_info *lp, void *data)
{
  if (lp->step && lp->status != 0)
    return 1;
  else
    return 0;
}

/* Select the Nth LWP that has had a SIGTRAP event.  */

static int
select_event_lwp_callback (struct lwp_info *lp, void *data)
{
  int *selector = data;

  gdb_assert (selector != NULL);

  /* Select only LWPs that have a SIGTRAP event pending. */
  if (lp->status != 0
      && WIFSTOPPED (lp->status) && WSTOPSIG (lp->status) == SIGTRAP)
    if ((*selector)-- == 0)
      return 1;

  return 0;
}

static int
cancel_breakpoints_callback (struct lwp_info *lp, void *data)
{
  struct lwp_info *event_lp = data;

  /* Leave the LWP that has been elected to receive a SIGTRAP alone.  */
  if (lp == event_lp)
    return 0;

  /* If a LWP other than the LWP that we're reporting an event for has
     hit a GDB breakpoint (as opposed to some random trap signal),
     then just arrange for it to hit it again later.  We don't keep
     the SIGTRAP status and don't forward the SIGTRAP signal to the
     LWP.  We will handle the current event, eventually we will resume
     all LWPs, and this one will get its breakpoint trap again.

     If we do not do this, then we run the risk that the user will
     delete or disable the breakpoint, but the LWP will have already
     tripped on it.  */

  if (lp->status != 0
      && WIFSTOPPED (lp->status) && WSTOPSIG (lp->status) == SIGTRAP
      && breakpoint_inserted_here_p (read_pc_pid (lp->ptid) -
				     DECR_PC_AFTER_BREAK))
    {
      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stdlog,
			    "CBC: Push back breakpoint for %s\n",
			    target_pid_to_str (lp->ptid));

      /* Back up the PC if necessary.  */
      if (DECR_PC_AFTER_BREAK)
	write_pc_pid (read_pc_pid (lp->ptid) - DECR_PC_AFTER_BREAK, lp->ptid);

      /* Throw away the SIGTRAP.  */
      lp->status = 0;
    }

  return 0;
}

/* Select one LWP out of those that have events pending.  */

static void
select_event_lwp (struct lwp_info **orig_lp, int *status)
{
  int num_events = 0;
  int random_selector;
  struct lwp_info *event_lp;

  /* Record the wait status for the origional LWP.  */
  (*orig_lp)->status = *status;

  /* Give preference to any LWP that is being single-stepped.  */
  event_lp = iterate_over_lwps (select_singlestep_lwp_callback, NULL);
  if (event_lp != NULL)
    {
      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stdlog,
			    "SEL: Select single-step %s\n",
			    target_pid_to_str (event_lp->ptid));
    }
  else
    {
      /* No single-stepping LWP.  Select one at random, out of those
         which have had SIGTRAP events.  */

      /* First see how many SIGTRAP events we have.  */
      iterate_over_lwps (count_events_callback, &num_events);

      /* Now randomly pick a LWP out of those that have had a SIGTRAP.  */
      random_selector = (int)
	((num_events * (double) rand ()) / (RAND_MAX + 1.0));

      if (debug_lin_lwp && num_events > 1)
	fprintf_unfiltered (gdb_stdlog,
			    "SEL: Found %d SIGTRAP events, selecting #%d\n",
			    num_events, random_selector);

      event_lp = iterate_over_lwps (select_event_lwp_callback,
				    &random_selector);
    }

  if (event_lp != NULL)
    {
      /* Switch the event LWP.  */
      *orig_lp = event_lp;
      *status = event_lp->status;
    }

  /* Flush the wait status for the event LWP.  */
  (*orig_lp)->status = 0;
}

/* Return non-zero if LP has been resumed.  */

static int
resumed_callback (struct lwp_info *lp, void *data)
{
  return lp->resumed;
}

#ifdef CHILD_WAIT

/* We need to override child_wait to support attaching to cloned
   processes, since a normal wait (as done by the default version)
   ignores those processes.  */

/* Wait for child PTID to do something.  Return id of the child,
   minus_one_ptid in case of error; store status into *OURSTATUS.  */

ptid_t
child_wait (ptid_t ptid, struct target_waitstatus *ourstatus)
{
  int save_errno;
  int status;
  pid_t pid;

  do
    {
      set_sigint_trap ();	/* Causes SIGINT to be passed on to the
				   attached process.  */
      set_sigio_trap ();

      pid = waitpid (GET_PID (ptid), &status, 0);
      if (pid == -1 && errno == ECHILD)
	/* Try again with __WCLONE to check cloned processes.  */
	pid = waitpid (GET_PID (ptid), &status, __WCLONE);

      if (debug_lin_lwp)
	{
	  fprintf_unfiltered (gdb_stdlog,
			      "CW:  waitpid %ld received %s\n",
			      (long) pid, status_to_str (status));
	}

      save_errno = errno;

      /* Make sure we don't report an event for the exit of the
         original program, if we've detached from it.  */
      if (pid != -1 && !WIFSTOPPED (status) && pid != GET_PID (inferior_ptid))
	{
	  pid = -1;
	  save_errno = EINTR;
	}

      /* Check for stop events reported by a process we didn't already
	 know about - in this case, anything other than inferior_ptid.

	 If we're expecting to receive stopped processes after fork,
	 vfork, and clone events, then we'll just add the new one to
	 our list and go back to waiting for the event to be reported
	 - the stopped process might be returned from waitpid before
	 or after the event is.  If we want to handle debugging of
	 CLONE_PTRACE processes we need to do more here, i.e. switch
	 to multi-threaded mode.  */
      if (pid != -1 && WIFSTOPPED (status) && WSTOPSIG (status) == SIGSTOP
	  && pid != GET_PID (inferior_ptid))
	{
	  linux_record_stopped_pid (pid);
	  pid = -1;
	  save_errno = EINTR;
	}

      clear_sigio_trap ();
      clear_sigint_trap ();
    }
  while (pid == -1 && save_errno == EINTR);

  if (pid == -1)
    {
      warning ("Child process unexpectedly missing: %s",
	       safe_strerror (errno));

      /* Claim it exited with unknown signal.  */
      ourstatus->kind = TARGET_WAITKIND_SIGNALLED;
      ourstatus->value.sig = TARGET_SIGNAL_UNKNOWN;
      return minus_one_ptid;
    }

  /* Handle GNU/Linux's extended waitstatus for trace events.  */
  if (WIFSTOPPED (status) && WSTOPSIG (status) == SIGTRAP && status >> 16 != 0)
    return linux_handle_extended_wait (pid, status, ourstatus);

  store_waitstatus (ourstatus, status);
  return pid_to_ptid (pid);
}

#endif

/* Stop an active thread, verify it still exists, then resume it.  */

static int
stop_and_resume_callback (struct lwp_info *lp, void *data)
{
  struct lwp_info *ptr;

  if (!lp->stopped && !lp->signalled)
    {
      stop_callback (lp, NULL);
      stop_wait_callback (lp, NULL);
      /* Resume if the lwp still exists.  */
      for (ptr = lwp_list; ptr; ptr = ptr->next)
	if (lp == ptr)
	  {
	    resume_callback (lp, NULL);
	    resume_set_callback (lp, NULL);
	  }
    }
  return 0;
}

static ptid_t
lin_lwp_wait (ptid_t ptid, struct target_waitstatus *ourstatus)
{
  struct lwp_info *lp = NULL;
  int options = 0;
  int status = 0;
  pid_t pid = PIDGET (ptid);
  sigset_t flush_mask;

  sigemptyset (&flush_mask);

  /* Make sure SIGCHLD is blocked.  */
  if (!sigismember (&blocked_mask, SIGCHLD))
    {
      sigaddset (&blocked_mask, SIGCHLD);
      sigprocmask (SIG_BLOCK, &blocked_mask, NULL);
    }

retry:

  /* Make sure there is at least one LWP that has been resumed, at
     least if there are any LWPs at all.  */
  gdb_assert (num_lwps == 0 || iterate_over_lwps (resumed_callback, NULL));

  /* First check if there is a LWP with a wait status pending.  */
  if (pid == -1)
    {
      /* Any LWP that's been resumed will do.  */
      lp = iterate_over_lwps (status_callback, NULL);
      if (lp)
	{
	  status = lp->status;
	  lp->status = 0;

	  if (debug_lin_lwp && status)
	    fprintf_unfiltered (gdb_stdlog,
				"LLW: Using pending wait status %s for %s.\n",
				status_to_str (status),
				target_pid_to_str (lp->ptid));
	}

      /* But if we don't fine one, we'll have to wait, and check both
         cloned and uncloned processes.  We start with the cloned
         processes.  */
      options = __WCLONE | WNOHANG;
    }
  else if (is_lwp (ptid))
    {
      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stdlog,
			    "LLW: Waiting for specific LWP %s.\n",
			    target_pid_to_str (ptid));

      /* We have a specific LWP to check.  */
      lp = find_lwp_pid (ptid);
      gdb_assert (lp);
      status = lp->status;
      lp->status = 0;

      if (debug_lin_lwp && status)
	fprintf_unfiltered (gdb_stdlog,
			    "LLW: Using pending wait status %s for %s.\n",
			    status_to_str (status),
			    target_pid_to_str (lp->ptid));

      /* If we have to wait, take into account whether PID is a cloned
         process or not.  And we have to convert it to something that
         the layer beneath us can understand.  */
      options = lp->cloned ? __WCLONE : 0;
      pid = GET_LWP (ptid);
    }

  if (status && lp->signalled)
    {
      /* A pending SIGSTOP may interfere with the normal stream of
         events.  In a typical case where interference is a problem,
         we have a SIGSTOP signal pending for LWP A while
         single-stepping it, encounter an event in LWP B, and take the
         pending SIGSTOP while trying to stop LWP A.  After processing
         the event in LWP B, LWP A is continued, and we'll never see
         the SIGTRAP associated with the last time we were
         single-stepping LWP A.  */

      /* Resume the thread.  It should halt immediately returning the
         pending SIGSTOP.  */
      registers_changed ();
      child_resume (pid_to_ptid (GET_LWP (lp->ptid)), lp->step,
		    TARGET_SIGNAL_0);
      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stdlog,
			    "LLW: %s %s, 0, 0 (expect SIGSTOP)\n",
			    lp->step ? "PTRACE_SINGLESTEP" : "PTRACE_CONT",
			    target_pid_to_str (lp->ptid));
      lp->stopped = 0;
      gdb_assert (lp->resumed);

      /* This should catch the pending SIGSTOP.  */
      stop_wait_callback (lp, NULL);
    }

  set_sigint_trap ();		/* Causes SIGINT to be passed on to the
				   attached process. */
  set_sigio_trap ();

  while (status == 0)
    {
      pid_t lwpid;

      lwpid = waitpid (pid, &status, options);
      if (lwpid > 0)
	{
	  gdb_assert (pid == -1 || lwpid == pid);

	  if (debug_lin_lwp)
	    {
	      fprintf_unfiltered (gdb_stdlog,
				  "LLW: waitpid %ld received %s\n",
				  (long) lwpid, status_to_str (status));
	    }

	  lp = find_lwp_pid (pid_to_ptid (lwpid));

	  /* Check for stop events reported by a process we didn't
	     already know about - anything not already in our LWP
	     list.

	     If we're expecting to receive stopped processes after
	     fork, vfork, and clone events, then we'll just add the
	     new one to our list and go back to waiting for the event
	     to be reported - the stopped process might be returned
	     from waitpid before or after the event is.  */
	  if (WIFSTOPPED (status) && !lp)
	    {
	      linux_record_stopped_pid (lwpid);
	      status = 0;
	      continue;
	    }

	  /* Make sure we don't report an event for the exit of an LWP not in
	     our list, i.e.  not part of the current process.  This can happen
	     if we detach from a program we original forked and then it
	     exits.  */
	  if (!WIFSTOPPED (status) && !lp)
	    {
	      status = 0;
	      continue;
	    }

	  /* NOTE drow/2003-06-17: This code seems to be meant for debugging
	     CLONE_PTRACE processes which do not use the thread library -
	     otherwise we wouldn't find the new LWP this way.  That doesn't
	     currently work, and the following code is currently unreachable
	     due to the two blocks above.  If it's fixed some day, this code
	     should be broken out into a function so that we can also pick up
	     LWPs from the new interface.  */
	  if (!lp)
	    {
	      lp = add_lwp (BUILD_LWP (lwpid, GET_PID (inferior_ptid)));
	      if (options & __WCLONE)
		lp->cloned = 1;

	      if (threaded)
		{
		  gdb_assert (WIFSTOPPED (status)
			      && WSTOPSIG (status) == SIGSTOP);
		  lp->signalled = 1;

		  if (!in_thread_list (inferior_ptid))
		    {
		      inferior_ptid = BUILD_LWP (GET_PID (inferior_ptid),
						 GET_PID (inferior_ptid));
		      add_thread (inferior_ptid);
		    }

		  add_thread (lp->ptid);
		  printf_unfiltered ("[New %s]\n",
				     target_pid_to_str (lp->ptid));
		}
	    }

	  /* Check if the thread has exited.  */
	  if ((WIFEXITED (status) || WIFSIGNALED (status)) && num_lwps > 1)
	    {
	      if (in_thread_list (lp->ptid))
		{
		  /* Core GDB cannot deal with us deleting the current
		     thread.  */
		  if (!ptid_equal (lp->ptid, inferior_ptid))
		    delete_thread (lp->ptid);
		  printf_unfiltered ("[%s exited]\n",
				     target_pid_to_str (lp->ptid));
		}

	      /* If this is the main thread, we must stop all threads and
	         verify if they are still alive.  This is because in the nptl
	         thread model, there is no signal issued for exiting LWPs
	         other than the main thread.  We only get the main thread
	         exit signal once all child threads have already exited.
	         If we stop all the threads and use the stop_wait_callback
	         to check if they have exited we can determine whether this
	         signal should be ignored or whether it means the end of the
	         debugged application, regardless of which threading model
	         is being used.  */
	      if (GET_PID (lp->ptid) == GET_LWP (lp->ptid))
		{
		  lp->stopped = 1;
		  iterate_over_lwps (stop_and_resume_callback, NULL);
		}

	      if (debug_lin_lwp)
		fprintf_unfiltered (gdb_stdlog,
				    "LLW: %s exited.\n",
				    target_pid_to_str (lp->ptid));

	      delete_lwp (lp->ptid);

	      /* If there is at least one more LWP, then the exit signal
	         was not the end of the debugged application and should be
	         ignored.  */
	      if (num_lwps > 0)
		{
		  /* Make sure there is at least one thread running.  */
		  gdb_assert (iterate_over_lwps (running_callback, NULL));

		  /* Discard the event.  */
		  status = 0;
		  continue;
		}
	    }

	  /* Check if the current LWP has previously exited.  In the nptl
	     thread model, LWPs other than the main thread do not issue
	     signals when they exit so we must check whenever the thread
	     has stopped.  A similar check is made in stop_wait_callback().  */
	  if (num_lwps > 1 && !lin_lwp_thread_alive (lp->ptid))
	    {
	      if (in_thread_list (lp->ptid))
		{
		  /* Core GDB cannot deal with us deleting the current
		     thread.  */
		  if (!ptid_equal (lp->ptid, inferior_ptid))
		    delete_thread (lp->ptid);
		  printf_unfiltered ("[%s exited]\n",
				     target_pid_to_str (lp->ptid));
		}
	      if (debug_lin_lwp)
		fprintf_unfiltered (gdb_stdlog,
				    "LLW: %s exited.\n",
				    target_pid_to_str (lp->ptid));

	      delete_lwp (lp->ptid);

	      /* Make sure there is at least one thread running.  */
	      gdb_assert (iterate_over_lwps (running_callback, NULL));

	      /* Discard the event.  */
	      status = 0;
	      continue;
	    }

	  /* Make sure we don't report a SIGSTOP that we sent
	     ourselves in an attempt to stop an LWP.  */
	  if (lp->signalled
	      && WIFSTOPPED (status) && WSTOPSIG (status) == SIGSTOP)
	    {
	      if (debug_lin_lwp)
		fprintf_unfiltered (gdb_stdlog,
				    "LLW: Delayed SIGSTOP caught for %s.\n",
				    target_pid_to_str (lp->ptid));

	      /* This is a delayed SIGSTOP.  */
	      lp->signalled = 0;

	      registers_changed ();
	      child_resume (pid_to_ptid (GET_LWP (lp->ptid)), lp->step,
			    TARGET_SIGNAL_0);
	      if (debug_lin_lwp)
		fprintf_unfiltered (gdb_stdlog,
				    "LLW: %s %s, 0, 0 (discard SIGSTOP)\n",
				    lp->step ?
				    "PTRACE_SINGLESTEP" : "PTRACE_CONT",
				    target_pid_to_str (lp->ptid));

	      lp->stopped = 0;
	      gdb_assert (lp->resumed);

	      /* Discard the event.  */
	      status = 0;
	      continue;
	    }

	  break;
	}

      if (pid == -1)
	{
	  /* Alternate between checking cloned and uncloned processes.  */
	  options ^= __WCLONE;

	  /* And suspend every time we have checked both.  */
	  if (options & __WCLONE)
	    sigsuspend (&suspend_mask);
	}

      /* We shouldn't end up here unless we want to try again.  */
      gdb_assert (status == 0);
    }

  clear_sigio_trap ();
  clear_sigint_trap ();

  gdb_assert (lp);

  /* Don't report signals that GDB isn't interested in, such as
     signals that are neither printed nor stopped upon.  Stopping all
     threads can be a bit time-consuming so if we want decent
     performance with heavily multi-threaded programs, especially when
     they're using a high frequency timer, we'd better avoid it if we
     can.  */

  if (WIFSTOPPED (status))
    {
      int signo = target_signal_from_host (WSTOPSIG (status));

      if (signal_stop_state (signo) == 0
	  && signal_print_state (signo) == 0
	  && signal_pass_state (signo) == 1)
	{
	  /* FIMXE: kettenis/2001-06-06: Should we resume all threads
	     here?  It is not clear we should.  GDB may not expect
	     other threads to run.  On the other hand, not resuming
	     newly attached threads may cause an unwanted delay in
	     getting them running.  */
	  registers_changed ();
	  child_resume (pid_to_ptid (GET_LWP (lp->ptid)), lp->step, signo);
	  if (debug_lin_lwp)
	    fprintf_unfiltered (gdb_stdlog,
				"LLW: %s %s, %s (preempt 'handle')\n",
				lp->step ?
				"PTRACE_SINGLESTEP" : "PTRACE_CONT",
				target_pid_to_str (lp->ptid),
				signo ? strsignal (signo) : "0");
	  lp->stopped = 0;
	  status = 0;
	  goto retry;
	}

      if (signo == TARGET_SIGNAL_INT && signal_pass_state (signo) == 0)
	{
	  /* If ^C/BREAK is typed at the tty/console, SIGINT gets
	     forwarded to the entire process group, that is, all LWP's
	     will receive it.  Since we only want to report it once,
	     we try to flush it from all LWPs except this one.  */
	  sigaddset (&flush_mask, SIGINT);
	}
    }

  /* This LWP is stopped now.  */
  lp->stopped = 1;

  if (debug_lin_lwp)
    fprintf_unfiltered (gdb_stdlog, "LLW: Candidate event %s in %s.\n",
			status_to_str (status), target_pid_to_str (lp->ptid));

  /* Now stop all other LWP's ...  */
  iterate_over_lwps (stop_callback, NULL);

  /* ... and wait until all of them have reported back that they're no
     longer running.  */
  iterate_over_lwps (stop_wait_callback, &flush_mask);
  iterate_over_lwps (flush_callback, &flush_mask);

  /* If we're not waiting for a specific LWP, choose an event LWP from
     among those that have had events.  Giving equal priority to all
     LWPs that have had events helps prevent starvation.  */
  if (pid == -1)
    select_event_lwp (&lp, &status);

  /* Now that we've selected our final event LWP, cancel any
     breakpoints in other LWPs that have hit a GDB breakpoint.  See
     the comment in cancel_breakpoints_callback to find out why.  */
  iterate_over_lwps (cancel_breakpoints_callback, lp);

  /* If we're not running in "threaded" mode, we'll report the bare
     process id.  */

  if (WIFSTOPPED (status) && WSTOPSIG (status) == SIGTRAP)
    {
      trap_ptid = (threaded ? lp->ptid : pid_to_ptid (GET_LWP (lp->ptid)));
      if (debug_lin_lwp)
	fprintf_unfiltered (gdb_stdlog,
			    "LLW: trap_ptid is %s.\n",
			    target_pid_to_str (trap_ptid));
    }
  else
    trap_ptid = null_ptid;

  /* Handle GNU/Linux's extended waitstatus for trace events.  */
  if (WIFSTOPPED (status) && WSTOPSIG (status) == SIGTRAP && status >> 16 != 0)
    {
      linux_handle_extended_wait (ptid_get_pid (trap_ptid),
				  status, ourstatus);
      return trap_ptid;
    }

  store_waitstatus (ourstatus, status);
  return (threaded ? lp->ptid : pid_to_ptid (GET_LWP (lp->ptid)));
}

static int
kill_callback (struct lwp_info *lp, void *data)
{
  errno = 0;
  ptrace (PTRACE_KILL, GET_LWP (lp->ptid), 0, 0);
  if (debug_lin_lwp)
    fprintf_unfiltered (gdb_stdlog,
			"KC:  PTRACE_KILL %s, 0, 0 (%s)\n",
			target_pid_to_str (lp->ptid),
			errno ? safe_strerror (errno) : "OK");

  return 0;
}

static int
kill_wait_callback (struct lwp_info *lp, void *data)
{
  pid_t pid;

  /* We must make sure that there are no pending events (delayed
     SIGSTOPs, pending SIGTRAPs, etc.) to make sure the current
     program doesn't interfere with any following debugging session.  */

  /* For cloned processes we must check both with __WCLONE and
     without, since the exit status of a cloned process isn't reported
     with __WCLONE.  */
  if (lp->cloned)
    {
      do
	{
	  pid = waitpid (GET_LWP (lp->ptid), NULL, __WCLONE);
	  if (pid != (pid_t) -1 && debug_lin_lwp)
	    {
	      fprintf_unfiltered (gdb_stdlog,
				  "KWC: wait %s received unknown.\n",
				  target_pid_to_str (lp->ptid));
	    }
	}
      while (pid == GET_LWP (lp->ptid));

      gdb_assert (pid == -1 && errno == ECHILD);
    }

  do
    {
      pid = waitpid (GET_LWP (lp->ptid), NULL, 0);
      if (pid != (pid_t) -1 && debug_lin_lwp)
	{
	  fprintf_unfiltered (gdb_stdlog,
			      "KWC: wait %s received unk.\n",
			      target_pid_to_str (lp->ptid));
	}
    }
  while (pid == GET_LWP (lp->ptid));

  gdb_assert (pid == -1 && errno == ECHILD);
  return 0;
}

static void
lin_lwp_kill (void)
{
  /* Kill all LWP's ...  */
  iterate_over_lwps (kill_callback, NULL);

  /* ... and wait until we've flushed all events.  */
  iterate_over_lwps (kill_wait_callback, NULL);

  target_mourn_inferior ();
}

static void
lin_lwp_create_inferior (char *exec_file, char *allargs, char **env)
{
  child_ops.to_create_inferior (exec_file, allargs, env);
}

static void
lin_lwp_mourn_inferior (void)
{
  trap_ptid = null_ptid;

  /* Destroy LWP info; it's no longer valid.  */
  init_lwp_list ();

  /* Restore the original signal mask.  */
  sigprocmask (SIG_SETMASK, &normal_mask, NULL);
  sigemptyset (&blocked_mask);

  child_ops.to_mourn_inferior ();
}

static int
lin_lwp_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int write,
		     struct mem_attrib *attrib, struct target_ops *target)
{
  struct cleanup *old_chain = save_inferior_ptid ();
  int xfer;

  if (is_lwp (inferior_ptid))
    inferior_ptid = pid_to_ptid (GET_LWP (inferior_ptid));

  xfer = linux_proc_xfer_memory (memaddr, myaddr, len, write, attrib, target);
  if (xfer == 0)
    xfer = child_xfer_memory (memaddr, myaddr, len, write, attrib, target);

  do_cleanups (old_chain);
  return xfer;
}

static int
lin_lwp_thread_alive (ptid_t ptid)
{
  gdb_assert (is_lwp (ptid));

  errno = 0;
  ptrace (PTRACE_PEEKUSER, GET_LWP (ptid), 0, 0);
  if (debug_lin_lwp)
    fprintf_unfiltered (gdb_stdlog,
			"LLTA: PTRACE_PEEKUSER %s, 0, 0 (%s)\n",
			target_pid_to_str (ptid),
			errno ? safe_strerror (errno) : "OK");
  if (errno)
    return 0;

  return 1;
}

static char *
lin_lwp_pid_to_str (ptid_t ptid)
{
  static char buf[64];

  if (is_lwp (ptid))
    {
      snprintf (buf, sizeof (buf), "LWP %ld", GET_LWP (ptid));
      return buf;
    }

  return normal_pid_to_str (ptid);
}

static void
init_lin_lwp_ops (void)
{
#if 0
  lin_lwp_ops.to_open = lin_lwp_open;
#endif
  lin_lwp_ops.to_shortname = "lwp-layer";
  lin_lwp_ops.to_longname = "lwp-layer";
  lin_lwp_ops.to_doc = "Low level threads support (LWP layer)";
  lin_lwp_ops.to_attach = lin_lwp_attach;
  lin_lwp_ops.to_detach = lin_lwp_detach;
  lin_lwp_ops.to_resume = lin_lwp_resume;
  lin_lwp_ops.to_wait = lin_lwp_wait;
  /* fetch_inferior_registers and store_inferior_registers will
     honor the LWP id, so we can use them directly.  */
  lin_lwp_ops.to_fetch_registers = fetch_inferior_registers;
  lin_lwp_ops.to_store_registers = store_inferior_registers;
  lin_lwp_ops.to_xfer_memory = lin_lwp_xfer_memory;
  lin_lwp_ops.to_kill = lin_lwp_kill;
  lin_lwp_ops.to_create_inferior = lin_lwp_create_inferior;
  lin_lwp_ops.to_mourn_inferior = lin_lwp_mourn_inferior;
  lin_lwp_ops.to_thread_alive = lin_lwp_thread_alive;
  lin_lwp_ops.to_pid_to_str = lin_lwp_pid_to_str;
  lin_lwp_ops.to_post_startup_inferior = child_post_startup_inferior;
  lin_lwp_ops.to_post_attach = child_post_attach;
  lin_lwp_ops.to_insert_fork_catchpoint = child_insert_fork_catchpoint;
  lin_lwp_ops.to_insert_vfork_catchpoint = child_insert_vfork_catchpoint;
  lin_lwp_ops.to_insert_exec_catchpoint = child_insert_exec_catchpoint;

  lin_lwp_ops.to_stratum = thread_stratum;
  lin_lwp_ops.to_has_thread_control = tc_schedlock;
  lin_lwp_ops.to_magic = OPS_MAGIC;
}

static void
sigchld_handler (int signo)
{
  /* Do nothing.  The only reason for this handler is that it allows
     us to use sigsuspend in lin_lwp_wait above to wait for the
     arrival of a SIGCHLD.  */
}

void
_initialize_lin_lwp (void)
{
  struct sigaction action;

  extern void thread_db_init (struct target_ops *);

  init_lin_lwp_ops ();
  add_target (&lin_lwp_ops);
  thread_db_init (&lin_lwp_ops);

  /* Save the original signal mask.  */
  sigprocmask (SIG_SETMASK, NULL, &normal_mask);

  action.sa_handler = sigchld_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGCHLD, &action, NULL);

  /* Make sure we don't block SIGCHLD during a sigsuspend.  */
  sigprocmask (SIG_SETMASK, NULL, &suspend_mask);
  sigdelset (&suspend_mask, SIGCHLD);

  sigemptyset (&blocked_mask);

  add_show_from_set (add_set_cmd ("lin-lwp", no_class, var_zinteger,
				  (char *) &debug_lin_lwp,
				  "Set debugging of GNU/Linux lwp module.\n\
Enables printf debugging output.\n", &setdebuglist), &showdebuglist);
}


/* FIXME: kettenis/2000-08-26: The stuff on this page is specific to
   the GNU/Linux Threads library and therefore doesn't really belong
   here.  */

/* Read variable NAME in the target and return its value if found.
   Otherwise return zero.  It is assumed that the type of the variable
   is `int'.  */

static int
get_signo (const char *name)
{
  struct minimal_symbol *ms;
  int signo;

  ms = lookup_minimal_symbol (name, NULL, NULL);
  if (ms == NULL)
    return 0;

  if (target_read_memory (SYMBOL_VALUE_ADDRESS (ms), (char *) &signo,
			  sizeof (signo)) != 0)
    return 0;

  return signo;
}

/* Return the set of signals used by the threads library in *SET.  */

void
lin_thread_get_thread_signals (sigset_t *set)
{
  struct sigaction action;
  int restart, cancel;

  sigemptyset (set);

  restart = get_signo ("__pthread_sig_restart");
  if (restart == 0)
    return;

  cancel = get_signo ("__pthread_sig_cancel");
  if (cancel == 0)
    return;

  sigaddset (set, restart);
  sigaddset (set, cancel);

  /* The GNU/Linux Threads library makes terminating threads send a
     special "cancel" signal instead of SIGCHLD.  Make sure we catch
     those (to prevent them from terminating GDB itself, which is
     likely to be their default action) and treat them the same way as
     SIGCHLD.  */

  action.sa_handler = sigchld_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (cancel, &action, NULL);

  /* We block the "cancel" signal throughout this code ...  */
  sigaddset (&blocked_mask, cancel);
  sigprocmask (SIG_BLOCK, &blocked_mask, NULL);

  /* ... except during a sigsuspend.  */
  sigdelset (&suspend_mask, cancel);
}

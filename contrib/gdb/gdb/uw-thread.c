/* Low level interface for debugging UnixWare user-mode threads for
   GDB, the GNU debugger.

   Copyright 1999, 2000, 2001 Free Software Foundation, Inc.
   Written by Nick Duffek <nsd@cygnus.com>.

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


/* Like many systems, UnixWare implements two classes of threads:
   kernel-mode threads, which are scheduled by the kernel; and
   user-mode threads, which are scheduled by a library.  UnixWare
   calls these two classes lightweight processes (LWPs) and threads,
   respectively.

   This module deals with user-mode threads.  It calls procfs_ops
   functions to deal with LWPs and processes and core_ops functions to
   deal with core files.

   As of this writing, the user-mode thread debugging interface is not
   documented beyond the comments in <thread.h>.  The following
   description has been gleaned from experience and from information
   provided by SCO.

   libthread.so, against which all UnixWare user-mode thread programs
   link, provides a global thread_debug structure named _thr_debug.
   It has three fields:

     (1) thr_map is a pointer to a pointer to an element of a
	 thread_map ring.  A thread_map contains a single thread's id
	 number, state, LWP pointer, recent register state, and other
	 useful information.

     (2) thr_brk is a pointer to a stub function that libthread.so
	 calls when it changes a thread's state, e.g. by creating it,
	 switching it to an LWP, or causing it to exit.

     (3) thr_debug_on controls whether libthread.so calls thr_brk().

   Debuggers are able to track thread activity by setting a private
   breakpoint on thr_brk() and setting thr_debug_on to 1.

   thr_brk() receives two arguments:

     (1) a pointer to a thread_map describing the thread being
	 changed; and

     (2) an enum thread_change specifying one of the following
	 changes:

	 invalid		 unknown
	 thread_create		 thread has just been created
	 thread_exit		 thread has just exited
	 switch_begin		 thread will be switched to an LWP
	 switch_complete	 thread has been switched to an LWP
	 cancel_complete	 thread wasn't switched to an LWP
	 thread_suspend		 thread has been thr_suspend()ed
	 thread_suspend_pending	 thread will be thr_suspend()ed
	 thread_continue	 thread has been thr_continue()d

   The thread_map argument to thr_brk() is NULL under the following
   circumstances:

     - The main thread is being acted upon.  The main thread always
       has id 1, so its thread_map is easy to find by scanning through
       _thr_debug.thr_map.

     - A "switch_complete" change is occurring, which means that the
       thread specified in the most recent "switch_begin" change has
       moved to an LWP.

     - A "cancel_complete" change is occurring, which means that the
       thread specified in the most recent "switch_begin" change has
       not moved to an LWP after all.

     - A spurious "switch_begin" change is occurring after a
       "thread_exit" change.

   Between switch_begin and switch_complete or cancel_complete, the
   affected thread's LWP pointer is not reliable.  It is possible that
   other parts of the thread's thread_map are also unreliable during
   that time. */


#include "defs.h"
#include "gdbthread.h"
#include "target.h"
#include "inferior.h"
#include "regcache.h"
#include <fcntl.h>

/* <thread.h> includes <sys/priocntl.h>, which requires boolean_t from
   <sys/types.h>, which doesn't typedef boolean_t with gcc. */

#define boolean_t int
#include <thread.h>
#undef boolean_t

#include <synch.h>		/* for UnixWare 2.x */

/* Prototypes for supply_gregset etc. */
#include "gregset.h"

/* Offset from SP to first arg on stack at first instruction of a
   function.  We provide a default here that's right for most, if not
   all, targets that use this file.  */

#ifndef SP_ARG0
#define SP_ARG0 (1 * 4)
#endif

/* Whether to emit debugging output. */

#define DEBUG 0

/* Default debugging output file, overridden by envvar UWTHR_DEBUG. */

#define DEBUG_FILE "/dev/tty"

/* #if DEBUG, write string S to the debugging output channel. */

#if !DEBUG
# define DBG(fmt_and_args)
# define DBG2(fmt_and_args)
#else
# define DBG(fmt_and_args) dbg fmt_and_args
# define DBG2(fmt_and_args)
#endif

/* Back end to CALL_BASE() and TRY_BASE(): evaluate CALL, then convert
   inferior_ptid to a composite thread/process id. */

#define CALL_BASE_1(call)		\
do {					\
  DBG2(("CALL_BASE(" #call ")"));	\
  call;					\
  do_cleanups (infpid_cleanup);		\
} while (0)

/* If inferior_ptid can be converted to a composite lwp/process id, do so,
   evaluate base_ops function CALL, and then convert inferior_ptid back to a
   composite thread/process id.

   Otherwise, issue an error message and return nonlocally. */

#define CALL_BASE(call)			\
do {					\
  if (!lwp_infpid ())			\
    error ("uw-thread: no lwp");	\
  CALL_BASE_1 (call);			\
} while (0)

/* Like CALL_BASE(), but instead of returning nonlocally on error, set
   *CALLED to whether the inferior_ptid conversion was successful. */

#define TRY_BASE(call, called)		\
do {					\
  if ((*(called) = lwp_infpid ()))	\
    CALL_BASE_1 (call);			\
} while (0)

/* Information passed by thread_iter() to its callback parameter. */

typedef struct {
  struct thread_map map;
  __lwp_desc_t lwp;
  CORE_ADDR mapp;
} iter_t;

/* Private thread data for the thread_info struct. */

struct private_thread_info {
  int stable;		/* 0 if libthread.so is modifying thread map */
  int thrid;		/* thread id assigned by libthread.so */
  int lwpid;		/* thread's lwp if .stable, 0 means no lwp */
  CORE_ADDR mapp;	/* address of thread's map structure */
};


/* procfs.c's target-specific operations. */
extern struct target_ops procfs_ops;

/* Flag to prevent procfs.c from starting inferior processes. */
extern int procfs_suppress_run;

/* This module's target-specific operations. */
static struct target_ops uw_thread_ops;

/* Copy of the target over which uw_thread_ops is pushed.  This is
   more convenient than a pointer to procfs_ops or core_ops, because
   they lack current_target's default callbacks. */
static struct target_ops base_ops;

/* Saved pointer to previous owner of target_new_objfile_hook. */
static void (*target_new_objfile_chain)(struct objfile *);

/* Whether we are debugging a user-space thread program.  This isn't
   set until after libthread.so is loaded by the program being
   debugged.

   Except for module one-time intialization and where otherwise
   documented, no functions in this module get called when
   !uw_thread_active. */
static int uw_thread_active;

/* For efficiency, cache the addresses of libthread.so's _thr_debug
   structure, its thr_brk stub function, and the main thread's map. */
static CORE_ADDR thr_debug_addr;
static CORE_ADDR thr_brk_addr;
static CORE_ADDR thr_map_main;

/* Remember the thread most recently marked as switching.  Necessary because
   libthread.so passes null map when calling stub with tc_*_complete. */
static struct thread_info *switchto_thread;

/* Cleanup chain for safely restoring inferior_ptid after CALL_BASE. */
static struct cleanup *infpid_cleanup;


#if DEBUG
/* Helper function for DBG() macro: if printf-style FMT is non-null, format it
   with args and display the result on the debugging output channel. */

static void
dbg (char *fmt, ...)
{
  static int fd = -1, len;
  va_list args;
  char buf[1024];
  char *path;

  if (!fmt)
    return;

  if (fd < 0)
    {
      path = getenv ("UWTHR_DEBUG");
      if (!path)
	path = DEBUG_FILE;
      if ((fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0664)) < 0)
	error ("can't open %s\n", path);
    }

  va_start (args, fmt);
  vsprintf (buf, fmt, args);
  va_end (args);

  len = strlen (buf);
  buf[len] = '\n';
  (void)write (fd, buf, len + 1);
}

#if 0
/* Return a string representing composite PID's components. */

static char *
dbgpid (ptid_t ptid)
{
  static char *buf, buf1[80], buf2[80];
  if (!buf || buf == buf2)
    buf = buf1;
  else
    buf = buf2;

  if (PIDGET (ptid) <= 0)
    sprintf (buf, "%d", PIDGET (ptid));
  else
    sprintf (buf, "%s %ld/%d", ISTID (pid) ? "thr" : "lwp",
	     TIDGET (pid), PIDGET (pid));

  return buf;
}

/* Return a string representing thread state CHANGE. */

static char *
dbgchange (enum thread_change change)
{
  switch (change) {
  case tc_invalid:			return "invalid";
  case tc_thread_create:		return "thread_create";
  case tc_thread_exit:			return "thread_exit";
  case tc_switch_begin:			return "switch_begin";
  case tc_switch_complete:		return "switch_complete";
  case tc_cancel_complete:		return "cancel_complete";
  case tc_thread_suspend:		return "thread_suspend";
  case tc_thread_suspend_pending:	return "thread_suspend_pending";
  case tc_thread_continue:		return "thread_continue";
  default:				return "unknown";
  }
}

/* Return a string representing thread STATE. */

static char *
dbgstate (int state)
{
  switch (state) {
  case TS_ONPROC:	return "running";
  case TS_SLEEP:	return "sleeping";
  case TS_RUNNABLE:	return "runnable";
  case TS_ZOMBIE:	return "zombie";
  case TS_SUSPENDED:	return "suspended";
#ifdef TS_FORK
  case TS_FORK:		return "forking";
#endif
  default:		return "confused";
  }
}
#endif  /* 0 */
#endif  /* DEBUG */


/* Read the contents of _thr_debug into *DEBUGP.  Return success. */

static int
read_thr_debug (struct thread_debug *debugp)
{
  return base_ops.to_xfer_memory (thr_debug_addr, (char *)debugp,
				  sizeof (*debugp), 0, NULL, &base_ops);
}

/* Read into MAP the contents of the thread map at inferior process address
   MAPP.  Return success. */

static int
read_map (CORE_ADDR mapp, struct thread_map *map)
{
  return base_ops.to_xfer_memory ((CORE_ADDR)THR_MAP (mapp), (char *)map,
				  sizeof (*map), 0, NULL, &base_ops);
}

/* Read into LWP the contents of the lwp decriptor at inferior process address
   LWPP.  Return success. */

static int
read_lwp (CORE_ADDR lwpp, __lwp_desc_t *lwp)
{
  return base_ops.to_xfer_memory (lwpp, (char *)lwp,
				  sizeof (*lwp), 0, NULL, &base_ops);
}

/* Iterate through all user threads, applying FUNC(<map>, <lwp>, DATA) until
     (a) FUNC returns nonzero,
     (b) FUNC has been applied to all threads, or
     (c) an error occurs,
   where <map> is the thread's struct thread_map and <lwp> if non-null is the
   thread's current __lwp_desc_t.

   If a call to FUNC returns nonzero, return that value; otherwise, return 0. */

static int
thread_iter (int (*func)(iter_t *, void *), void *data)
{
  struct thread_debug debug;
  CORE_ADDR first, mapp;
  iter_t iter;
  int ret;

  if (!read_thr_debug (&debug))
    return 0;
  if (!base_ops.to_xfer_memory ((CORE_ADDR)debug.thr_map, (char *)&mapp,
				sizeof (mapp), 0, NULL, &base_ops))
    return 0;
  if (!mapp)
    return 0;

  for (first = mapp;;)
    {
      if (!read_map (mapp, &iter.map))
	return 0;

      if (iter.map.thr_lwpp)
	if (!read_lwp ((CORE_ADDR)iter.map.thr_lwpp, &iter.lwp))
	  return 0;

      iter.mapp = mapp;
      if ((ret = func (&iter, data)))
	return ret;

      mapp = (CORE_ADDR)iter.map.thr_next;
      if (mapp == first)
	return 0;
    }
}

/* Deactivate user-mode thread support. */

static void
deactivate_uw_thread (void)
{
  remove_thread_event_breakpoints ();
  uw_thread_active = 0;
  unpush_target (&uw_thread_ops);
}

/* Return the composite lwp/process id corresponding to composite
   id PID.  If PID is a thread with no lwp, return 0. */

static ptid_t
thr_to_lwp (ptid_t ptid)
{
  struct thread_info *info;
  ptid_t lid;

  if (!ISTID (ptid))
    lid = ptid;
  else if (!(info = find_thread_pid (ptid)))
    lid = null_ptid;
  else if (!info->private->lwpid)
    lid = null_ptid;
  else
    lid = MKLID (PIDGET (ptid), info->private->lwpid);

  DBG2(("  thr_to_lwp(%s) = %s", dbgpid (pid), dbgpid (lid)));
  return lid;
}

/* find_thread_lwp() callback: return whether TP describes a thread
   associated with lwp id DATA. */

static int
find_thread_lwp_callback (struct thread_info *tp, void *data)
{
  int lwpid = (int)data;

  if (!ISTID (tp->ptid))
    return 0;
  if (!tp->private->stable)
    return 0;
  if (lwpid != tp->private->lwpid)
    return 0;

  /* match */
  return 1;
}

/* If a thread is associated with lwp id LWPID, return the corresponding
   member of the global thread list; otherwise, return null. */

static struct thread_info *
find_thread_lwp (int lwpid)
{
  return iterate_over_threads (find_thread_lwp_callback, (void *)lwpid);
}

/* Return the composite thread/process id corresponding to composite
   id PID.  If PID is an lwp with no thread, return PID. */

static ptid_t
lwp_to_thr (ptid_t ptid)
{
  struct thread_info *info;
  int lwpid;
  ptid_t tid = ptid;

  if (ISTID (ptid))
    goto done;
  if (!(lwpid = LIDGET (ptid)))
    goto done;
  if (!(info = find_thread_lwp (lwpid)))
    goto done;
  tid = MKTID (PIDGET (ptid), info->private->thrid);

 done:
  DBG2((ISTID (tid) ? NULL : "lwp_to_thr: no thr for %s", dbgpid (ptid)));
  return tid;
}

/* do_cleanups() callback: convert inferior_ptid to a composite
   thread/process id after having made a procfs call. */

static void
thr_infpid (void *unused)
{
  ptid_t ptid = lwp_to_thr (inferior_ptid);
  DBG2((" inferior_ptid from procfs: %s => %s",
	dbgpid (inferior_ptid), dbgpid (ptid)));
  inferior_ptid = ptid;
}

/* If possible, convert inferior_ptid to a composite lwp/process id in
   preparation for making a procfs call.  Return success. */

static int
lwp_infpid (void)
{
  ptid_t ptid = thr_to_lwp (inferior_ptid);
  DBG2((" inferior_ptid to procfs: %s => %s",
	dbgpid (inferior_ptid), dbgpid (ptid)));

  if (ptid_equal (ptid, null_ptid))
    return 0;

  inferior_ptid = ptid;
  infpid_cleanup = make_cleanup (thr_infpid, NULL);
  return 1;
}

/* Add to the global thread list a new user-mode thread with system id THRID,
   lwp id LWPID, map address MAPP, and composite thread/process PID. */

static void
add_thread_uw (int thrid, int lwpid, CORE_ADDR mapp, ptid_t ptid)
{
  struct thread_info *newthread;

  if ((newthread = add_thread (ptid)) == NULL)
    error ("failed to create new thread structure");

  newthread->private = xmalloc (sizeof (struct private_thread_info));
  newthread->private->stable = 1;
  newthread->private->thrid = thrid;
  newthread->private->lwpid = lwpid;
  newthread->private->mapp = mapp;

  if (target_has_execution)
    printf_unfiltered ("[New %s]\n", target_pid_to_str (ptid));
}

/* notice_threads() and find_main() callback: if the thread list doesn't
   already contain the thread described by ITER, add it if it's the main
   thread or if !DATA. */

static int
notice_thread (iter_t *iter, void *data)
{
  int thrid = iter->map.thr_tid;
  int lwpid = !iter->map.thr_lwpp ? 0 : iter->lwp.lwp_id;
  ptid_t ptid = MKTID (PIDGET (inferior_ptid), thrid);

  if (!find_thread_pid (ptid) && (!data || thrid == 1))
    add_thread_uw (thrid, lwpid, iter->mapp, ptid);

  return 0;
}

/* Add to the thread list any threads it doesn't already contain. */

static void
notice_threads (void)
{
  thread_iter (notice_thread, NULL);
}

/* Return the address of the main thread's map.  On error, return 0. */

static CORE_ADDR
find_main (void)
{
  if (!thr_map_main)
    {
      struct thread_info *info;
      thread_iter (notice_thread, (void *)1);
      if ((info = find_thread_pid (MKTID (PIDGET (inferior_ptid), 1))))
	thr_map_main = info->private->mapp;
    }
  return thr_map_main;
}

/* Attach to process specified by ARGS, then initialize for debugging it
   and wait for the trace-trap that results from attaching.

   This function only gets called with uw_thread_active == 0. */

static void
uw_thread_attach (char *args, int from_tty)
{
  procfs_ops.to_attach (args, from_tty);
  if (uw_thread_active)
    thr_infpid (NULL);
}

/* Detach from the process attached to by uw_thread_attach(). */

static void
uw_thread_detach (char *args, int from_tty)
{
  deactivate_uw_thread ();
  base_ops.to_detach (args, from_tty);
}

/* Tell the inferior process to continue running thread PID if >= 0
   and all threads otherwise. */

static void
uw_thread_resume (ptid_t ptid, int step, enum target_signal signo)
{
  if (PIDGET (ptid) > 0)
    {
      ptid = thr_to_lwp (ptid);
      if (ptid_equal (ptid, null_ptid))
	ptid = pid_to_ptid (-1);
    }

  CALL_BASE (base_ops.to_resume (ptid, step, signo));
}

/* If the trap we just received from lwp PID was due to a breakpoint
   on the libthread.so debugging stub, update this module's state
   accordingly. */

static void
libthread_stub (ptid_t ptid)
{
  CORE_ADDR sp, mapp, mapp_main;
  enum thread_change change;
  struct thread_map map;
  __lwp_desc_t lwp;
  int lwpid;
  ptid_t tid = null_ptid;
  struct thread_info *info;

  /* Check for stub breakpoint. */
  if (read_pc_pid (ptid) - DECR_PC_AFTER_BREAK != thr_brk_addr)
    return;

  /* Retrieve stub args. */
  sp = read_register_pid (SP_REGNUM, ptid);
  if (!base_ops.to_xfer_memory (sp + SP_ARG0, (char *)&mapp,
				sizeof (mapp), 0, NULL, &base_ops))
    goto err;
  if (!base_ops.to_xfer_memory (sp + SP_ARG0 + sizeof (mapp), (char *)&change,
				sizeof (change), 0, NULL, &base_ops))
    goto err;

  /* create_inferior() may not have finished yet, so notice the main
     thread to ensure that it's displayed first by add_thread(). */
  mapp_main = find_main ();

  /* Notice thread creation, deletion, or stability change. */
  switch (change) {
  case tc_switch_begin:
    if (!mapp)				/* usually means main thread */
      mapp = mapp_main;
    /* fall through */

  case tc_thread_create:
  case tc_thread_exit:
    if (!mapp)
      break;
    if (!read_map (mapp, &map))
      goto err;
    tid = MKTID (PIDGET (ptid), map.thr_tid);

    switch (change) {
    case tc_thread_create:		/* new thread */
      if (!map.thr_lwpp)
	lwpid = 0;
      else if (!read_lwp ((CORE_ADDR)map.thr_lwpp, &lwp))
	goto err;
      else
	lwpid = lwp.lwp_id;
      add_thread_uw (map.thr_tid, lwpid, mapp, tid);
      break;

    case tc_thread_exit:		/* thread has exited */
      printf_unfiltered ("[Exited %s]\n", target_pid_to_str (tid));
      delete_thread (tid);
      if (ptid_equal (tid, inferior_ptid))
	inferior_ptid = ptid;
      break;

    case tc_switch_begin:		/* lwp is switching threads */
      if (switchto_thread)
	goto err;
      if (!(switchto_thread = find_thread_pid (tid)))
	goto err;
      switchto_thread->private->stable = 0;
      break;

    default:
      break;
    }
    break;

  case tc_switch_complete:		/* lwp has switched threads */
  case tc_cancel_complete:		/* lwp didn't switch threads */
    if (!switchto_thread)
      goto err;

    if (change == tc_switch_complete)
      {
	/* If switchto_thread is the main thread, then (a) the corresponding
	   tc_switch_begin probably received a null map argument and therefore
	   (b) it may have been a spurious switch following a tc_thread_exit.

	   Therefore, explicitly query the thread's lwp before caching it in
	   its thread list entry. */

	if (!read_map (switchto_thread->private->mapp, &map))
	  goto err;
	if (map.thr_lwpp)
	  {
	    if (!read_lwp ((CORE_ADDR)map.thr_lwpp, &lwp))
	      goto err;
	    if ((info = find_thread_lwp (lwp.lwp_id)))
	      info->private->lwpid = 0;
	    switchto_thread->private->lwpid = lwp.lwp_id;
	  }
      }

    switchto_thread->private->stable = 1;
    switchto_thread = NULL;
    break;

  case tc_invalid:
  case tc_thread_suspend:
  case tc_thread_suspend_pending:
  case tc_thread_continue:
  err:
    DBG(("unexpected condition in libthread_stub()"));
    break;
  }

  DBG2(("libthread_stub(%s): %s %s %s", dbgpid (pid), dbgpid (tid),
	dbgchange (change), tid ? dbgstate (map.thr_state) : ""));
}

/* Wait for thread/lwp/process ID if >= 0 or for any thread otherwise. */

static ptid_t
uw_thread_wait (ptid_t ptid, struct target_waitstatus *status)
{
  if (PIDGET (ptid) > 0)
    ptid = thr_to_lwp (ptid);
  if (PIDGET (ptid) <= 0)
    ptid = pid_to_ptid (-1);

  CALL_BASE (ptid = base_ops.to_wait (ptid, status));

  if (status->kind == TARGET_WAITKIND_STOPPED &&
      status->value.sig == TARGET_SIGNAL_TRAP)
    libthread_stub (ptid);

  return lwp_to_thr (ptid);
}

/* Tell gdb about the registers in the thread/lwp/process specified by
   inferior_ptid. */

static void
uw_thread_fetch_registers (int regno)
{
  int called;
  struct thread_info *info;
  struct thread_map map;

  TRY_BASE (base_ops.to_fetch_registers (regno), &called);
  if (called)
    return;

  if (!(info = find_thread_pid (inferior_ptid)))
    return;
  if (!read_map (info->private->mapp, &map))
    return;

  supply_gregset (&map.thr_ucontext.uc_mcontext.gregs);
  supply_fpregset (&map.thr_ucontext.uc_mcontext.fpregs);
}

/* Store gdb's current view of the register set into the thread/lwp/process
   specified by inferior_ptid. */

static void
uw_thread_store_registers (int regno)
{
  CALL_BASE (base_ops.to_store_registers (regno));
}

/* Prepare to modify the registers array. */

static void
uw_thread_prepare_to_store (void)
{
  CALL_BASE (base_ops.to_prepare_to_store ());
}

/* Fork an inferior process and start debugging it.

   This function only gets called with uw_thread_active == 0. */

static void
uw_thread_create_inferior (char *exec_file, char *allargs, char **env)
{
  if (uw_thread_active)
    deactivate_uw_thread ();

  procfs_ops.to_create_inferior (exec_file, allargs, env);
  if (uw_thread_active)
    {
      find_main ();
      thr_infpid (NULL);
    }
}

/* Kill and forget about the inferior process. */

static void
uw_thread_kill (void)
{
  base_ops.to_kill ();
}

/* Clean up after the inferior exits. */

static void
uw_thread_mourn_inferior (void)
{
  deactivate_uw_thread ();
  base_ops.to_mourn_inferior ();
}

/* Return whether this module can attach to and run processes.

   This function only gets called with uw_thread_active == 0. */

static int
uw_thread_can_run (void)
{
  return procfs_suppress_run;
}

/* Return whether thread PID is still valid. */

static int
uw_thread_alive (ptid_t ptid)
{
  if (!ISTID (ptid))
    return base_ops.to_thread_alive (ptid);

  /* If it's in the thread list, it's valid, because otherwise
     libthread_stub() would have deleted it. */
  return in_thread_list (ptid);
}

/* Add to the thread list any threads and lwps it doesn't already contain. */

static void
uw_thread_find_new_threads (void)
{
  CALL_BASE (if (base_ops.to_find_new_threads)
	       base_ops.to_find_new_threads ());
  notice_threads ();
}

/* Return a string for pretty-printing PID in "info threads" output.
   This may be called by either procfs.c or by generic gdb. */

static char *
uw_thread_pid_to_str (ptid_t ptid)
{
#define FMT "Thread %ld"
  static char buf[sizeof (FMT) + 3 * sizeof (long)];

  if (!ISTID (ptid))
    /* core_ops says "process foo", so call procfs_ops explicitly. */
    return procfs_ops.to_pid_to_str (ptid);

  sprintf (buf, FMT, TIDGET (ptid));
#undef FMT
  return buf;
}

/* Return a string displaying INFO state information in "info threads"
   output. */

static char *
uw_extra_thread_info (struct thread_info *info)
{
  static char buf[80];
  struct thread_map map;
  __lwp_desc_t lwp;
  int lwpid;
  char *name;

  if (!ISTID (info->ptid))
    return NULL;

  if (!info->private->stable)
    return "switching";

  if (!read_map (info->private->mapp, &map))
    return NULL;

  if (!map.thr_lwpp || !read_lwp ((CORE_ADDR)map.thr_lwpp, &lwp))
    lwpid = 0;
  else
    lwpid = lwp.lwp_id;

  switch (map.thr_state) {
  case TS_ONPROC:	name = "running";	break;
  case TS_SLEEP:	name = "sleeping";	break;
  case TS_RUNNABLE:	name = "runnable";	break;
  case TS_ZOMBIE:	name = "zombie";	break;
  case TS_SUSPENDED:	name = "suspended";	break;
#ifdef TS_FORK
  case TS_FORK:		name = "forking";	break;
#endif
  default:		name = "confused";	break;
  }

  if (!lwpid)
    return name;

  sprintf (buf, "%s, LWP %d", name, lwpid);
  return buf;
}

/* Check whether libthread.so has just been loaded, and if so, try to
   initialize user-space thread debugging support.

   libthread.so loading happens while (a) an inferior process is being
   started by procfs and (b) a core image is being loaded.

   This function often gets called with uw_thread_active == 0. */

static void
libthread_init (void)
{
  struct minimal_symbol *ms;
  struct thread_debug debug;
  CORE_ADDR onp;
  struct breakpoint *b;
  int one = 1;

  /* Don't initialize twice. */
  if (uw_thread_active)
    return;

  /* Check whether libthread.so has been loaded. */
  if (!(ms = lookup_minimal_symbol ("_thr_debug", NULL, NULL)))
    return;

  /* Cache _thr_debug's address. */
  if (!(thr_debug_addr = SYMBOL_VALUE_ADDRESS (ms)))
    return;

  /* Initialize base_ops.to_xfer_memory(). */
  base_ops = current_target;

  /* Load _thr_debug's current contents. */
  if (!read_thr_debug (&debug))
    return;

  /* User code (e.g. my test programs) may dereference _thr_debug,
     making it availble to GDB before shared libs are loaded. */
  if (!debug.thr_map)
    return;

  /* libthread.so has been loaded, and the current_target should now
     reflect core_ops or procfs_ops. */
  push_target (&uw_thread_ops);		/* must precede notice_threads() */
  uw_thread_active = 1;

  if (!target_has_execution)

    /* Locate threads in core file. */
    notice_threads ();

  else
    {
      /* Set a breakpoint on the stub function provided by libthread.so. */
      thr_brk_addr = (CORE_ADDR)debug.thr_brk;
      if (!(b = create_thread_event_breakpoint (thr_brk_addr)))
	goto err;

      /* Activate the stub function. */
      onp = (CORE_ADDR)&((struct thread_debug *)thr_debug_addr)->thr_debug_on;
      if (!base_ops.to_xfer_memory ((CORE_ADDR)onp, (char *)&one,
				    sizeof (one), 1, NULL, &base_ops))
	{
	  delete_breakpoint (b);
	  goto err;
	}

      /* Prepare for finding the main thread, which doesn't yet exist. */
      thr_map_main = 0;
    }

  return;

 err:
  warning ("uw-thread: unable to initialize user-mode thread debugging\n");
  deactivate_uw_thread ();
}

/* target_new_objfile_hook callback.

   If OBJFILE is non-null, check whether libthread.so was just loaded,
   and if so, prepare for user-mode thread debugging.

   If OBJFILE is null, libthread.so has gone away, so stop debugging
   user-mode threads.

   This function often gets called with uw_thread_active == 0. */

static void
uw_thread_new_objfile (struct objfile *objfile)
{
  if (objfile)
    libthread_init ();

  else if (uw_thread_active)
    deactivate_uw_thread ();

  if (target_new_objfile_chain)
    target_new_objfile_chain (objfile);
}

/* Initialize uw_thread_ops. */

static void
init_uw_thread_ops (void)
{
  uw_thread_ops.to_shortname          = "unixware-threads";
  uw_thread_ops.to_longname           = "UnixWare threads and pthread.";
  uw_thread_ops.to_doc        = "UnixWare threads and pthread support.";
  uw_thread_ops.to_attach             = uw_thread_attach;
  uw_thread_ops.to_detach             = uw_thread_detach;
  uw_thread_ops.to_resume             = uw_thread_resume;
  uw_thread_ops.to_wait               = uw_thread_wait;
  uw_thread_ops.to_fetch_registers    = uw_thread_fetch_registers;
  uw_thread_ops.to_store_registers    = uw_thread_store_registers;
  uw_thread_ops.to_prepare_to_store   = uw_thread_prepare_to_store;
  uw_thread_ops.to_create_inferior    = uw_thread_create_inferior;
  uw_thread_ops.to_kill               = uw_thread_kill;
  uw_thread_ops.to_mourn_inferior     = uw_thread_mourn_inferior;
  uw_thread_ops.to_can_run            = uw_thread_can_run;
  uw_thread_ops.to_thread_alive       = uw_thread_alive;
  uw_thread_ops.to_find_new_threads   = uw_thread_find_new_threads;
  uw_thread_ops.to_pid_to_str         = uw_thread_pid_to_str;
  uw_thread_ops.to_extra_thread_info  = uw_extra_thread_info;
  uw_thread_ops.to_stratum            = thread_stratum;
  uw_thread_ops.to_magic              = OPS_MAGIC;
}

/* Module startup initialization function, automagically called by
   init.c. */

void
_initialize_uw_thread (void)
{
  init_uw_thread_ops ();
  add_target (&uw_thread_ops);

  procfs_suppress_run = 1;

  /* Notice when libthread.so gets loaded. */
  target_new_objfile_chain = target_new_objfile_hook;
  target_new_objfile_hook = uw_thread_new_objfile;
}

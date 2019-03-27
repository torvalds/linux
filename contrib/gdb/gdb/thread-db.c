/* libthread_db assisted debugging support, generic parts.

   Copyright 1999, 2000, 2001, 2003 Free Software Foundation, Inc.

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
#include <dlfcn.h>
#include "gdb_proc_service.h"
#include "gdb_thread_db.h"

#include "bfd.h"
#include "gdbthread.h"
#include "inferior.h"
#include "symfile.h"
#include "objfiles.h"
#include "target.h"
#include "regcache.h"
#include "solib-svr4.h"

#ifndef LIBTHREAD_DB_SO
#define LIBTHREAD_DB_SO "libthread_db.so.1"
#endif

/* If we're running on GNU/Linux, we must explicitly attach to any new
   threads.  */

/* FIXME: There is certainly some room for improvements:
   - Cache LWP ids.
   - Bypass libthread_db when fetching or storing registers for
   threads bound to a LWP.  */

/* This module's target vector.  */
static struct target_ops thread_db_ops;

/* The target vector that we call for things this module can't handle.  */
static struct target_ops *target_beneath;

/* Pointer to the next function on the objfile event chain.  */
static void (*target_new_objfile_chain) (struct objfile * objfile);

/* Non-zero if we're using this module's target vector.  */
static int using_thread_db;

/* Non-zero if we have to keep this module's target vector active
   across re-runs.  */
static int keep_thread_db;

/* Non-zero if we have determined the signals used by the threads
   library.  */
static int thread_signals;
static sigset_t thread_stop_set;
static sigset_t thread_print_set;

/* Structure that identifies the child process for the
   <proc_service.h> interface.  */
static struct ps_prochandle proc_handle;

/* Connection to the libthread_db library.  */
static td_thragent_t *thread_agent;

/* Pointers to the libthread_db functions.  */

static td_err_e (*td_init_p) (void);

static td_err_e (*td_ta_new_p) (struct ps_prochandle * ps,
				td_thragent_t **ta);
static td_err_e (*td_ta_map_id2thr_p) (const td_thragent_t *ta, thread_t pt,
				       td_thrhandle_t *__th);
static td_err_e (*td_ta_map_lwp2thr_p) (const td_thragent_t *ta,
					lwpid_t lwpid, td_thrhandle_t *th);
static td_err_e (*td_ta_thr_iter_p) (const td_thragent_t *ta,
				     td_thr_iter_f *callback, void *cbdata_p,
				     td_thr_state_e state, int ti_pri,
				     sigset_t *ti_sigmask_p,
				     unsigned int ti_user_flags);
static td_err_e (*td_ta_event_addr_p) (const td_thragent_t *ta,
				       td_event_e event, td_notify_t *ptr);
static td_err_e (*td_ta_set_event_p) (const td_thragent_t *ta,
				      td_thr_events_t *event);
static td_err_e (*td_ta_event_getmsg_p) (const td_thragent_t *ta,
					 td_event_msg_t *msg);

static td_err_e (*td_thr_validate_p) (const td_thrhandle_t *th);
static td_err_e (*td_thr_get_info_p) (const td_thrhandle_t *th,
				      td_thrinfo_t *infop);
static td_err_e (*td_thr_getfpregs_p) (const td_thrhandle_t *th,
				       gdb_prfpregset_t *regset);
static td_err_e (*td_thr_getgregs_p) (const td_thrhandle_t *th,
				      prgregset_t gregs);
static td_err_e (*td_thr_setfpregs_p) (const td_thrhandle_t *th,
				       const gdb_prfpregset_t *fpregs);
static td_err_e (*td_thr_setgregs_p) (const td_thrhandle_t *th,
				      prgregset_t gregs);
static td_err_e (*td_thr_event_enable_p) (const td_thrhandle_t *th,
					  int event);

static td_err_e (*td_thr_tls_get_addr_p) (const td_thrhandle_t *th,
					  void *map_address,
					  size_t offset, void **address);

/* Location of the thread creation event breakpoint.  The code at this
   location in the child process will be called by the pthread library
   whenever a new thread is created.  By setting a special breakpoint
   at this location, GDB can detect when a new thread is created.  We
   obtain this location via the td_ta_event_addr call.  */
static CORE_ADDR td_create_bp_addr;

/* Location of the thread death event breakpoint.  */
static CORE_ADDR td_death_bp_addr;

/* Prototypes for local functions.  */
static void thread_db_find_new_threads (void);
static void attach_thread (ptid_t ptid, const td_thrhandle_t *th_p,
			   const td_thrinfo_t *ti_p, int verbose);


/* Building process ids.  */

#define GET_PID(ptid)		ptid_get_pid (ptid)
#define GET_LWP(ptid)		ptid_get_lwp (ptid)
#define GET_THREAD(ptid)	ptid_get_tid (ptid)

#define is_lwp(ptid)		(GET_LWP (ptid) != 0)
#define is_thread(ptid)		(GET_THREAD (ptid) != 0)

#define BUILD_LWP(lwp, pid)	ptid_build (pid, lwp, 0)
#define BUILD_THREAD(tid, pid)	ptid_build (pid, 0, tid)


/* Use "struct private_thread_info" to cache thread state.  This is
   a substantial optimization.  */

struct private_thread_info
{
  /* Cached thread state.  */
  unsigned int th_valid:1;
  unsigned int ti_valid:1;

  td_thrhandle_t th;
  td_thrinfo_t ti;
};


static char *
thread_db_err_str (td_err_e err)
{
  static char buf[64];

  switch (err)
    {
    case TD_OK:
      return "generic 'call succeeded'";
    case TD_ERR:
      return "generic error";
    case TD_NOTHR:
      return "no thread to satisfy query";
    case TD_NOSV:
      return "no sync handle to satisfy query";
    case TD_NOLWP:
      return "no LWP to satisfy query";
    case TD_BADPH:
      return "invalid process handle";
    case TD_BADTH:
      return "invalid thread handle";
    case TD_BADSH:
      return "invalid synchronization handle";
    case TD_BADTA:
      return "invalid thread agent";
    case TD_BADKEY:
      return "invalid key";
    case TD_NOMSG:
      return "no event message for getmsg";
    case TD_NOFPREGS:
      return "FPU register set not available";
    case TD_NOLIBTHREAD:
      return "application not linked with libthread";
    case TD_NOEVENT:
      return "requested event is not supported";
    case TD_NOCAPAB:
      return "capability not available";
    case TD_DBERR:
      return "debugger service failed";
    case TD_NOAPLIC:
      return "operation not applicable to";
    case TD_NOTSD:
      return "no thread-specific data for this thread";
    case TD_MALLOC:
      return "malloc failed";
    case TD_PARTIALREG:
      return "only part of register set was written/read";
    case TD_NOXREGS:
      return "X register set not available for this thread";
    default:
      snprintf (buf, sizeof (buf), "unknown thread_db error '%d'", err);
      return buf;
    }
}

static char *
thread_db_state_str (td_thr_state_e state)
{
  static char buf[64];

  switch (state)
    {
    case TD_THR_STOPPED:
      return "stopped by debugger";
    case TD_THR_RUN:
      return "runnable";
    case TD_THR_ACTIVE:
      return "active";
    case TD_THR_ZOMBIE:
      return "zombie";
    case TD_THR_SLEEP:
      return "sleeping";
    case TD_THR_STOPPED_ASLEEP:
      return "stopped by debugger AND blocked";
    default:
      snprintf (buf, sizeof (buf), "unknown thread_db state %d", state);
      return buf;
    }
}

/* A callback function for td_ta_thr_iter, which we use to map all
   threads to LWPs.

   THP is a handle to the current thread; if INFOP is not NULL, the
   struct thread_info associated with this thread is returned in
   *INFOP.  */

static int
thread_get_info_callback (const td_thrhandle_t *thp, void *infop)
{
  td_thrinfo_t ti;
  td_err_e err;
  struct thread_info *thread_info;
  ptid_t thread_ptid;

  err = td_thr_get_info_p (thp, &ti);
  if (err != TD_OK)
    error ("thread_get_info_callback: cannot get thread info: %s",
	   thread_db_err_str (err));

  /* Fill the cache.  */
  thread_ptid = BUILD_THREAD (ti.ti_tid, GET_PID (inferior_ptid));
  thread_info = find_thread_pid (thread_ptid);

  if (thread_info == NULL)
    {
      /* New thread.  Attach to it now (why wait?).  */
      attach_thread (thread_ptid, thp, &ti, 1);
      thread_info = find_thread_pid (thread_ptid);
      gdb_assert (thread_info != NULL);
    }

  memcpy (&thread_info->private->th, thp, sizeof (*thp));
  thread_info->private->th_valid = 1;
  memcpy (&thread_info->private->ti, &ti, sizeof (ti));
  thread_info->private->ti_valid = 1;

  if (infop != NULL)
    *(struct thread_info **) infop = thread_info;

  return 0;
}

/* Accessor functions for the thread_db information, with caching.  */

static void
thread_db_map_id2thr (struct thread_info *thread_info, int fatal)
{
  td_err_e err;

  if (thread_info->private->th_valid)
    return;

  err = td_ta_map_id2thr_p (thread_agent, GET_THREAD (thread_info->ptid),
			    &thread_info->private->th);
  if (err != TD_OK)
    {
      if (fatal)
	error ("Cannot find thread %ld: %s",
	       (long) GET_THREAD (thread_info->ptid),
	       thread_db_err_str (err));
    }
  else
    thread_info->private->th_valid = 1;
}

static td_thrinfo_t *
thread_db_get_info (struct thread_info *thread_info)
{
  td_err_e err;

  if (thread_info->private->ti_valid)
    return &thread_info->private->ti;

  if (!thread_info->private->th_valid)
    thread_db_map_id2thr (thread_info, 1);

  err =
    td_thr_get_info_p (&thread_info->private->th, &thread_info->private->ti);
  if (err != TD_OK)
    error ("thread_db_get_info: cannot get thread info: %s",
	   thread_db_err_str (err));

  thread_info->private->ti_valid = 1;
  return &thread_info->private->ti;
}

/* Convert between user-level thread ids and LWP ids.  */

static ptid_t
thread_from_lwp (ptid_t ptid)
{
  td_thrhandle_t th;
  td_err_e err;
  struct thread_info *thread_info;
  ptid_t thread_ptid;

  if (GET_LWP (ptid) == 0)
    ptid = BUILD_LWP (GET_PID (ptid), GET_PID (ptid));

  gdb_assert (is_lwp (ptid));

  err = td_ta_map_lwp2thr_p (thread_agent, GET_LWP (ptid), &th);
  if (err != TD_OK)
    error ("Cannot find user-level thread for LWP %ld: %s",
	   GET_LWP (ptid), thread_db_err_str (err));

  thread_info = NULL;
  thread_get_info_callback (&th, &thread_info);
  gdb_assert (thread_info && thread_info->private->ti_valid);

  return BUILD_THREAD (thread_info->private->ti.ti_tid, GET_PID (ptid));
}

static ptid_t
lwp_from_thread (ptid_t ptid)
{
  struct thread_info *thread_info;
  ptid_t thread_ptid;

  if (!is_thread (ptid))
    return ptid;

  thread_info = find_thread_pid (ptid);
  thread_db_get_info (thread_info);

  return BUILD_LWP (thread_info->private->ti.ti_lid, GET_PID (ptid));
}


void
thread_db_init (struct target_ops *target)
{
  target_beneath = target;
}

static void *
verbose_dlsym (void *handle, const char *name)
{
  void *sym = dlsym (handle, name);
  if (sym == NULL)
    warning ("Symbol \"%s\" not found in libthread_db: %s", name, dlerror ());
  return sym;
}

static int
thread_db_load (void)
{
  void *handle;
  td_err_e err;

  handle = dlopen (LIBTHREAD_DB_SO, RTLD_NOW);
  if (handle == NULL)
    {
      fprintf_filtered (gdb_stderr, "\n\ndlopen failed on '%s' - %s\n",
			LIBTHREAD_DB_SO, dlerror ());
      fprintf_filtered (gdb_stderr,
			"GDB will not be able to debug pthreads.\n\n");
      return 0;
    }

  /* Initialize pointers to the dynamic library functions we will use.
     Essential functions first.  */

  td_init_p = verbose_dlsym (handle, "td_init");
  if (td_init_p == NULL)
    return 0;

  td_ta_new_p = verbose_dlsym (handle, "td_ta_new");
  if (td_ta_new_p == NULL)
    return 0;

  td_ta_map_id2thr_p = verbose_dlsym (handle, "td_ta_map_id2thr");
  if (td_ta_map_id2thr_p == NULL)
    return 0;

  td_ta_map_lwp2thr_p = verbose_dlsym (handle, "td_ta_map_lwp2thr");
  if (td_ta_map_lwp2thr_p == NULL)
    return 0;

  td_ta_thr_iter_p = verbose_dlsym (handle, "td_ta_thr_iter");
  if (td_ta_thr_iter_p == NULL)
    return 0;

  td_thr_validate_p = verbose_dlsym (handle, "td_thr_validate");
  if (td_thr_validate_p == NULL)
    return 0;

  td_thr_get_info_p = verbose_dlsym (handle, "td_thr_get_info");
  if (td_thr_get_info_p == NULL)
    return 0;

  td_thr_getfpregs_p = verbose_dlsym (handle, "td_thr_getfpregs");
  if (td_thr_getfpregs_p == NULL)
    return 0;

  td_thr_getgregs_p = verbose_dlsym (handle, "td_thr_getgregs");
  if (td_thr_getgregs_p == NULL)
    return 0;

  td_thr_setfpregs_p = verbose_dlsym (handle, "td_thr_setfpregs");
  if (td_thr_setfpregs_p == NULL)
    return 0;

  td_thr_setgregs_p = verbose_dlsym (handle, "td_thr_setgregs");
  if (td_thr_setgregs_p == NULL)
    return 0;

  /* Initialize the library.  */
  err = td_init_p ();
  if (err != TD_OK)
    {
      warning ("Cannot initialize libthread_db: %s", thread_db_err_str (err));
      return 0;
    }

  /* These are not essential.  */
  td_ta_event_addr_p = dlsym (handle, "td_ta_event_addr");
  td_ta_set_event_p = dlsym (handle, "td_ta_set_event");
  td_ta_event_getmsg_p = dlsym (handle, "td_ta_event_getmsg");
  td_thr_event_enable_p = dlsym (handle, "td_thr_event_enable");
  td_thr_tls_get_addr_p = dlsym (handle, "td_thr_tls_get_addr");

  return 1;
}

static td_err_e
enable_thread_event (td_thragent_t *thread_agent, int event, CORE_ADDR *bp)
{
  td_notify_t notify;
  td_err_e err;

  /* Get the breakpoint address for thread EVENT.  */
  err = td_ta_event_addr_p (thread_agent, event, &notify);
  if (err != TD_OK)
    return err;

  /* Set up the breakpoint.  */
  (*bp) = gdbarch_convert_from_func_ptr_addr (current_gdbarch,
					      (CORE_ADDR) notify.u.bptaddr,
					      &current_target);
  create_thread_event_breakpoint ((*bp));

  return TD_OK;
}

static void
enable_thread_event_reporting (void)
{
  td_thr_events_t events;
  td_notify_t notify;
  td_err_e err;

  /* We cannot use the thread event reporting facility if these
     functions aren't available.  */
  if (td_ta_event_addr_p == NULL || td_ta_set_event_p == NULL
      || td_ta_event_getmsg_p == NULL || td_thr_event_enable_p == NULL)
    return;

  /* Set the process wide mask saying which events we're interested in.  */
  td_event_emptyset (&events);
  td_event_addset (&events, TD_CREATE);
#if 0
  /* FIXME: kettenis/2000-04-23: The event reporting facility is
     broken for TD_DEATH events in glibc 2.1.3, so don't enable it for
     now.  */
  td_event_addset (&events, TD_DEATH);
#endif

  err = td_ta_set_event_p (thread_agent, &events);
  if (err != TD_OK)
    {
      warning ("Unable to set global thread event mask: %s",
	       thread_db_err_str (err));
      return;
    }

  /* Delete previous thread event breakpoints, if any.  */
  remove_thread_event_breakpoints ();
  td_create_bp_addr = 0;
  td_death_bp_addr = 0;

  /* Set up the thread creation event.  */
  err = enable_thread_event (thread_agent, TD_CREATE, &td_create_bp_addr);
  if (err != TD_OK)
    {
      warning ("Unable to get location for thread creation breakpoint: %s",
	       thread_db_err_str (err));
      return;
    }

  /* Set up the thread death event.  */
  err = enable_thread_event (thread_agent, TD_DEATH, &td_death_bp_addr);
  if (err != TD_OK)
    {
      warning ("Unable to get location for thread death breakpoint: %s",
	       thread_db_err_str (err));
      return;
    }
}

static void
disable_thread_event_reporting (void)
{
  td_thr_events_t events;

  /* Set the process wide mask saying we aren't interested in any
     events anymore.  */
  td_event_emptyset (&events);
  td_ta_set_event_p (thread_agent, &events);

  /* Delete thread event breakpoints, if any.  */
  remove_thread_event_breakpoints ();
  td_create_bp_addr = 0;
  td_death_bp_addr = 0;
}

static void
check_thread_signals (void)
{
#ifdef GET_THREAD_SIGNALS
  if (!thread_signals)
    {
      sigset_t mask;
      int i;

      GET_THREAD_SIGNALS (&mask);
      sigemptyset (&thread_stop_set);
      sigemptyset (&thread_print_set);

      for (i = 1; i < NSIG; i++)
	{
	  if (sigismember (&mask, i))
	    {
	      if (signal_stop_update (target_signal_from_host (i), 0))
		sigaddset (&thread_stop_set, i);
	      if (signal_print_update (target_signal_from_host (i), 0))
		sigaddset (&thread_print_set, i);
	      thread_signals = 1;
	    }
	}
    }
#endif
}

static void
thread_db_new_objfile (struct objfile *objfile)
{
  td_err_e err;

  /* First time through, report that libthread_db was successfuly
     loaded.  Can't print this in in thread_db_load as, at that stage,
     the interpreter and it's console haven't started.  The real
     problem here is that libthread_db is loaded too early - it should
     only be loaded when there is a program to debug.  */
  {
    static int dejavu;
    if (!dejavu)
      {
	Dl_info info;
	const char *library = NULL;
	/* Try dladdr.  */
	if (dladdr ((*td_ta_new_p), &info) != 0)
	  library = info.dli_fname;
	/* Try dlinfo?  */
	if (library == NULL)
	  /* Paranoid - don't let a NULL path slip through.  */
	  library = LIBTHREAD_DB_SO;
	printf_unfiltered ("Using host libthread_db library \"%s\".\n",
			   library);
	dejavu = 1;
      }
  }

  /* Don't attempt to use thread_db on targets which can not run
     (core files).  */
  if (objfile == NULL || !target_has_execution)
    {
      /* All symbols have been discarded.  If the thread_db target is
         active, deactivate it now.  */
      if (using_thread_db)
	{
	  gdb_assert (proc_handle.pid == 0);
	  unpush_target (&thread_db_ops);
	  using_thread_db = 0;
	}

      keep_thread_db = 0;

      goto quit;
    }

  if (using_thread_db)
    /* Nothing to do.  The thread library was already detected and the
       target vector was already activated.  */
    goto quit;

  /* Initialize the structure that identifies the child process.  Note
     that at this point there is no guarantee that we actually have a
     child process.  */
  proc_handle.pid = GET_PID (inferior_ptid);

  /* Now attempt to open a connection to the thread library.  */
  err = td_ta_new_p (&proc_handle, &thread_agent);
  switch (err)
    {
    case TD_NOLIBTHREAD:
      /* No thread library was detected.  */
      break;

    case TD_OK:
      printf_unfiltered ("[Thread debugging using libthread_db enabled]\n");

      /* The thread library was detected.  Activate the thread_db target.  */
      push_target (&thread_db_ops);
      using_thread_db = 1;

      /* If the thread library was detected in the main symbol file
         itself, we assume that the program was statically linked
         against the thread library and well have to keep this
         module's target vector activated until forever...  Well, at
         least until all symbols have been discarded anyway (see
         above).  */
      if (objfile == symfile_objfile)
	{
	  gdb_assert (proc_handle.pid == 0);
	  keep_thread_db = 1;
	}

      /* We can only poke around if there actually is a child process.
         If there is no child process alive, postpone the steps below
         until one has been created.  */
      if (proc_handle.pid != 0)
	{
	  enable_thread_event_reporting ();
	  thread_db_find_new_threads ();
	}
      break;

    default:
      warning ("Cannot initialize thread debugging library: %s",
	       thread_db_err_str (err));
      break;
    }

quit:
  if (target_new_objfile_chain)
    target_new_objfile_chain (objfile);
}

static void
attach_thread (ptid_t ptid, const td_thrhandle_t *th_p,
	       const td_thrinfo_t *ti_p, int verbose)
{
  struct thread_info *tp;
  td_err_e err;

  check_thread_signals ();

  /* Add the thread to GDB's thread list.  */
  tp = add_thread (ptid);
  tp->private = xmalloc (sizeof (struct private_thread_info));
  memset (tp->private, 0, sizeof (struct private_thread_info));

  if (verbose)
    printf_unfiltered ("[New %s]\n", target_pid_to_str (ptid));

  if (ti_p->ti_state == TD_THR_UNKNOWN || ti_p->ti_state == TD_THR_ZOMBIE)
    return;			/* A zombie thread -- do not attach.  */

  /* Under GNU/Linux, we have to attach to each and every thread.  */
#ifdef ATTACH_LWP
  ATTACH_LWP (BUILD_LWP (ti_p->ti_lid, GET_PID (ptid)), 0);
#endif

  /* Enable thread event reporting for this thread.  */
  err = td_thr_event_enable_p (th_p, 1);
  if (err != TD_OK)
    error ("Cannot enable thread event reporting for %s: %s",
	   target_pid_to_str (ptid), thread_db_err_str (err));
}

static void
thread_db_attach (char *args, int from_tty)
{
  target_beneath->to_attach (args, from_tty);

  /* Destroy thread info; it's no longer valid.  */
  init_thread_list ();

  /* The child process is now the actual multi-threaded
     program.  Snatch its process ID...  */
  proc_handle.pid = GET_PID (inferior_ptid);

  /* ...and perform the remaining initialization steps.  */
  enable_thread_event_reporting ();
  thread_db_find_new_threads ();
}

static void
detach_thread (ptid_t ptid, int verbose)
{
  if (verbose)
    printf_unfiltered ("[%s exited]\n", target_pid_to_str (ptid));
}

static void
thread_db_detach (char *args, int from_tty)
{
  disable_thread_event_reporting ();

  /* There's no need to save & restore inferior_ptid here, since the
     inferior is supposed to be survive this function call.  */
  inferior_ptid = lwp_from_thread (inferior_ptid);

  /* Forget about the child's process ID.  We shouldn't need it
     anymore.  */
  proc_handle.pid = 0;

  target_beneath->to_detach (args, from_tty);
}

static int
clear_lwpid_callback (struct thread_info *thread, void *dummy)
{
  /* If we know that our thread implementation is 1-to-1, we could save
     a certain amount of information; it's not clear how much, so we
     are always conservative.  */

  thread->private->th_valid = 0;
  thread->private->ti_valid = 0;

  return 0;
}

static void
thread_db_resume (ptid_t ptid, int step, enum target_signal signo)
{
  struct cleanup *old_chain = save_inferior_ptid ();

  if (GET_PID (ptid) == -1)
    inferior_ptid = lwp_from_thread (inferior_ptid);
  else if (is_thread (ptid))
    ptid = lwp_from_thread (ptid);

  /* Clear cached data which may not be valid after the resume.  */
  iterate_over_threads (clear_lwpid_callback, NULL);

  target_beneath->to_resume (ptid, step, signo);

  do_cleanups (old_chain);
}

/* Check if PID is currently stopped at the location of a thread event
   breakpoint location.  If it is, read the event message and act upon
   the event.  */

static void
check_event (ptid_t ptid)
{
  td_event_msg_t msg;
  td_thrinfo_t ti;
  td_err_e err;
  CORE_ADDR stop_pc;
  int loop = 0;

  /* Bail out early if we're not at a thread event breakpoint.  */
  stop_pc = read_pc_pid (ptid) - DECR_PC_AFTER_BREAK;
  if (stop_pc != td_create_bp_addr && stop_pc != td_death_bp_addr)
    return;

  /* If we are at a create breakpoint, we do not know what new lwp
     was created and cannot specifically locate the event message for it.
     We have to call td_ta_event_getmsg() to get
     the latest message.  Since we have no way of correlating whether
     the event message we get back corresponds to our breakpoint, we must
     loop and read all event messages, processing them appropriately.
     This guarantees we will process the correct message before continuing
     from the breakpoint.

     Currently, death events are not enabled.  If they are enabled,
     the death event can use the td_thr_event_getmsg() interface to
     get the message specifically for that lwp and avoid looping
     below.  */

  loop = 1;

  do
    {
      err = td_ta_event_getmsg_p (thread_agent, &msg);
      if (err != TD_OK)
	{
	  if (err == TD_NOMSG)
	    return;

	  error ("Cannot get thread event message: %s",
		 thread_db_err_str (err));
	}

      err = td_thr_get_info_p (msg.th_p, &ti);
      if (err != TD_OK)
	error ("Cannot get thread info: %s", thread_db_err_str (err));

      ptid = BUILD_THREAD (ti.ti_tid, GET_PID (ptid));

      switch (msg.event)
	{
	case TD_CREATE:

	  /* We may already know about this thread, for instance when the
	     user has issued the `info threads' command before the SIGTRAP
	     for hitting the thread creation breakpoint was reported.  */
	  if (!in_thread_list (ptid))
	    attach_thread (ptid, msg.th_p, &ti, 1);

	  break;

	case TD_DEATH:

	  if (!in_thread_list (ptid))
	    error ("Spurious thread death event.");

	  detach_thread (ptid, 1);

	  break;

	default:
	  error ("Spurious thread event.");
	}
    }
  while (loop);
}

static ptid_t
thread_db_wait (ptid_t ptid, struct target_waitstatus *ourstatus)
{
  extern ptid_t trap_ptid;

  if (GET_PID (ptid) != -1 && is_thread (ptid))
    ptid = lwp_from_thread (ptid);

  ptid = target_beneath->to_wait (ptid, ourstatus);

  if (proc_handle.pid == 0)
    /* The current child process isn't the actual multi-threaded
       program yet, so don't try to do any special thread-specific
       post-processing and bail out early.  */
    return ptid;

  if (ourstatus->kind == TARGET_WAITKIND_EXITED)
    return pid_to_ptid (-1);

  if (ourstatus->kind == TARGET_WAITKIND_STOPPED
      && ourstatus->value.sig == TARGET_SIGNAL_TRAP)
    /* Check for a thread event.  */
    check_event (ptid);

  if (!ptid_equal (trap_ptid, null_ptid))
    trap_ptid = thread_from_lwp (trap_ptid);

  return thread_from_lwp (ptid);
}

static int
thread_db_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int write,
		       struct mem_attrib *attrib, struct target_ops *target)
{
  struct cleanup *old_chain = save_inferior_ptid ();
  int xfer;

  if (is_thread (inferior_ptid))
    {
      /* FIXME: This seems to be necessary to make sure breakpoints
         are removed.  */
      if (!target_thread_alive (inferior_ptid))
	inferior_ptid = pid_to_ptid (GET_PID (inferior_ptid));
      else
	inferior_ptid = lwp_from_thread (inferior_ptid);
    }

  xfer =
    target_beneath->to_xfer_memory (memaddr, myaddr, len, write, attrib,
				    target);

  do_cleanups (old_chain);
  return xfer;
}

static void
thread_db_fetch_registers (int regno)
{
  struct thread_info *thread_info;
  prgregset_t gregset;
  gdb_prfpregset_t fpregset;
  td_err_e err;

  if (!is_thread (inferior_ptid))
    {
      /* Pass the request to the target beneath us.  */
      target_beneath->to_fetch_registers (regno);
      return;
    }

  thread_info = find_thread_pid (inferior_ptid);
  thread_db_map_id2thr (thread_info, 1);

  err = td_thr_getgregs_p (&thread_info->private->th, gregset);
  if (err != TD_OK)
    error ("Cannot fetch general-purpose registers for thread %ld: %s",
	   (long) GET_THREAD (inferior_ptid), thread_db_err_str (err));

  err = td_thr_getfpregs_p (&thread_info->private->th, &fpregset);
  if (err != TD_OK)
    error ("Cannot get floating-point registers for thread %ld: %s",
	   (long) GET_THREAD (inferior_ptid), thread_db_err_str (err));

  /* Note that we must call supply_gregset after calling the thread_db
     routines because the thread_db routines call ps_lgetgregs and
     friends which clobber GDB's register cache.  */
  supply_gregset ((gdb_gregset_t *) gregset);
  supply_fpregset (&fpregset);
}

static void
thread_db_store_registers (int regno)
{
  prgregset_t gregset;
  gdb_prfpregset_t fpregset;
  td_err_e err;
  struct thread_info *thread_info;

  if (!is_thread (inferior_ptid))
    {
      /* Pass the request to the target beneath us.  */
      target_beneath->to_store_registers (regno);
      return;
    }

  thread_info = find_thread_pid (inferior_ptid);
  thread_db_map_id2thr (thread_info, 1);

  if (regno != -1)
    {
      char raw[MAX_REGISTER_SIZE];

      deprecated_read_register_gen (regno, raw);
      thread_db_fetch_registers (-1);
      supply_register (regno, raw);
    }

  fill_gregset ((gdb_gregset_t *) gregset, -1);
  fill_fpregset (&fpregset, -1);

  err = td_thr_setgregs_p (&thread_info->private->th, gregset);
  if (err != TD_OK)
    error ("Cannot store general-purpose registers for thread %ld: %s",
	   (long) GET_THREAD (inferior_ptid), thread_db_err_str (err));
  err = td_thr_setfpregs_p (&thread_info->private->th, &fpregset);
  if (err != TD_OK)
    error ("Cannot store floating-point registers  for thread %ld: %s",
	   (long) GET_THREAD (inferior_ptid), thread_db_err_str (err));
}

static void
thread_db_kill (void)
{
  /* There's no need to save & restore inferior_ptid here, since the
     inferior isn't supposed to survive this function call.  */
  inferior_ptid = lwp_from_thread (inferior_ptid);
  target_beneath->to_kill ();
}

static void
thread_db_create_inferior (char *exec_file, char *allargs, char **env)
{
  if (!keep_thread_db)
    {
      unpush_target (&thread_db_ops);
      using_thread_db = 0;
    }

  target_beneath->to_create_inferior (exec_file, allargs, env);
}

static void
thread_db_post_startup_inferior (ptid_t ptid)
{
  if (proc_handle.pid == 0)
    {
      /* The child process is now the actual multi-threaded
         program.  Snatch its process ID...  */
      proc_handle.pid = GET_PID (ptid);

      /* ...and perform the remaining initialization steps.  */
      enable_thread_event_reporting ();
      thread_db_find_new_threads ();
    }
}

static void
thread_db_mourn_inferior (void)
{
  remove_thread_event_breakpoints ();

  /* Forget about the child's process ID.  We shouldn't need it
     anymore.  */
  proc_handle.pid = 0;

  target_beneath->to_mourn_inferior ();

  /* Detach thread_db target ops if not dealing with a statically
     linked threaded program.  This allows a corefile to be debugged
     after finishing debugging of a threaded program.  At present,
     debugging a statically-linked threaded program is broken, but
     the check is added below in the event that it is fixed in the
     future.  */
  if (!keep_thread_db)
    {
      unpush_target (&thread_db_ops);
      using_thread_db = 0;
    }
}

static int
thread_db_thread_alive (ptid_t ptid)
{
  td_thrhandle_t th;
  td_err_e err;

  if (is_thread (ptid))
    {
      struct thread_info *thread_info;
      thread_info = find_thread_pid (ptid);

      thread_db_map_id2thr (thread_info, 0);
      if (!thread_info->private->th_valid)
	return 0;

      err = td_thr_validate_p (&thread_info->private->th);
      if (err != TD_OK)
	return 0;

      if (!thread_info->private->ti_valid)
	{
	  err =
	    td_thr_get_info_p (&thread_info->private->th,
			       &thread_info->private->ti);
	  if (err != TD_OK)
	    return 0;
	  thread_info->private->ti_valid = 1;
	}

      if (thread_info->private->ti.ti_state == TD_THR_UNKNOWN
	  || thread_info->private->ti.ti_state == TD_THR_ZOMBIE)
	return 0;		/* A zombie thread.  */

      return 1;
    }

  if (target_beneath->to_thread_alive)
    return target_beneath->to_thread_alive (ptid);

  return 0;
}

static int
find_new_threads_callback (const td_thrhandle_t *th_p, void *data)
{
  td_thrinfo_t ti;
  td_err_e err;
  ptid_t ptid;

  err = td_thr_get_info_p (th_p, &ti);
  if (err != TD_OK)
    error ("find_new_threads_callback: cannot get thread info: %s",
	   thread_db_err_str (err));

  if (ti.ti_state == TD_THR_UNKNOWN || ti.ti_state == TD_THR_ZOMBIE)
    return 0;			/* A zombie -- ignore.  */

  ptid = BUILD_THREAD (ti.ti_tid, GET_PID (inferior_ptid));

  if (!in_thread_list (ptid))
    attach_thread (ptid, th_p, &ti, 1);

  return 0;
}

static void
thread_db_find_new_threads (void)
{
  td_err_e err;

  /* Iterate over all user-space threads to discover new threads.  */
  err = td_ta_thr_iter_p (thread_agent, find_new_threads_callback, NULL,
			  TD_THR_ANY_STATE, TD_THR_LOWEST_PRIORITY,
			  TD_SIGNO_MASK, TD_THR_ANY_USER_FLAGS);
  if (err != TD_OK)
    error ("Cannot find new threads: %s", thread_db_err_str (err));
}

static char *
thread_db_pid_to_str (ptid_t ptid)
{
  if (is_thread (ptid))
    {
      static char buf[64];
      td_thrinfo_t *ti_p;
      td_err_e err;
      struct thread_info *thread_info;

      thread_info = find_thread_pid (ptid);
      thread_db_map_id2thr (thread_info, 0);
      if (!thread_info->private->th_valid)
	{
	  snprintf (buf, sizeof (buf), "Thread %ld (Missing)",
		    GET_THREAD (ptid));
	  return buf;
	}

      ti_p = thread_db_get_info (thread_info);

      if (ti_p->ti_state == TD_THR_ACTIVE && ti_p->ti_lid != 0)
	{
	  snprintf (buf, sizeof (buf), "Thread %ld (LWP %d)",
		    (long) ti_p->ti_tid, ti_p->ti_lid);
	}
      else
	{
	  snprintf (buf, sizeof (buf), "Thread %ld (%s)",
		    (long) ti_p->ti_tid,
		    thread_db_state_str (ti_p->ti_state));
	}

      return buf;
    }

  if (target_beneath->to_pid_to_str (ptid))
    return target_beneath->to_pid_to_str (ptid);

  return normal_pid_to_str (ptid);
}

/* Get the address of the thread local variable in OBJFILE which is
   stored at OFFSET within the thread local storage for thread PTID.  */

static CORE_ADDR
thread_db_get_thread_local_address (ptid_t ptid, struct objfile *objfile,
				    CORE_ADDR offset)
{
  if (is_thread (ptid))
    {
      int objfile_is_library = (objfile->flags & OBJF_SHARED);
      td_err_e err;
      void *address;
      CORE_ADDR lm;
      struct thread_info *thread_info;

      /* glibc doesn't provide the needed interface.  */
      if (!td_thr_tls_get_addr_p)
	error ("Cannot find thread-local variables in this thread library.");

      /* Get the address of the link map for this objfile.  */
      lm = svr4_fetch_objfile_link_map (objfile);

      /* Whoops, we couldn't find one. Bail out.  */
      if (!lm)
	{
	  if (objfile_is_library)
	    error ("Cannot find shared library `%s' link_map in dynamic"
		   " linker's module list", objfile->name);
	  else
	    error ("Cannot find executable file `%s' link_map in dynamic"
		   " linker's module list", objfile->name);
	}

      /* Get info about the thread.  */
      thread_info = find_thread_pid (ptid);
      thread_db_map_id2thr (thread_info, 1);

      /* Finally, get the address of the variable.  */
      err = td_thr_tls_get_addr_p (&thread_info->private->th, (void *) lm,
				   offset, &address);

#ifdef THREAD_DB_HAS_TD_NOTALLOC
      /* The memory hasn't been allocated, yet.  */
      if (err == TD_NOTALLOC)
	{
	  /* Now, if libthread_db provided the initialization image's
	     address, we *could* try to build a non-lvalue value from
	     the initialization image.  */
	  if (objfile_is_library)
	    error ("The inferior has not yet allocated storage for"
		   " thread-local variables in\n"
		   "the shared library `%s'\n"
		   "for the thread %ld",
		   objfile->name, (long) GET_THREAD (ptid));
	  else
	    error ("The inferior has not yet allocated storage for"
		   " thread-local variables in\n"
		   "the executable `%s'\n"
		   "for the thread %ld",
		   objfile->name, (long) GET_THREAD (ptid));
	}
#endif

      /* Something else went wrong.  */
      if (err != TD_OK)
	{
	  if (objfile_is_library)
	    error ("Cannot find thread-local storage for thread %ld, "
		   "shared library %s:\n%s",
		   (long) GET_THREAD (ptid),
		   objfile->name, thread_db_err_str (err));
	  else
	    error ("Cannot find thread-local storage for thread %ld, "
		   "executable file %s:\n%s",
		   (long) GET_THREAD (ptid),
		   objfile->name, thread_db_err_str (err));
	}

      /* Cast assuming host == target.  Joy.  */
      return (CORE_ADDR) address;
    }

  if (target_beneath->to_get_thread_local_address)
    return target_beneath->to_get_thread_local_address (ptid, objfile,
							offset);

  error ("Cannot find thread-local values on this target.");
}

static void
init_thread_db_ops (void)
{
  thread_db_ops.to_shortname = "multi-thread";
  thread_db_ops.to_longname = "multi-threaded child process.";
  thread_db_ops.to_doc = "Threads and pthreads support.";
  thread_db_ops.to_attach = thread_db_attach;
  thread_db_ops.to_detach = thread_db_detach;
  thread_db_ops.to_resume = thread_db_resume;
  thread_db_ops.to_wait = thread_db_wait;
  thread_db_ops.to_fetch_registers = thread_db_fetch_registers;
  thread_db_ops.to_store_registers = thread_db_store_registers;
  thread_db_ops.to_xfer_memory = thread_db_xfer_memory;
  thread_db_ops.to_kill = thread_db_kill;
  thread_db_ops.to_create_inferior = thread_db_create_inferior;
  thread_db_ops.to_post_startup_inferior = thread_db_post_startup_inferior;
  thread_db_ops.to_mourn_inferior = thread_db_mourn_inferior;
  thread_db_ops.to_thread_alive = thread_db_thread_alive;
  thread_db_ops.to_find_new_threads = thread_db_find_new_threads;
  thread_db_ops.to_pid_to_str = thread_db_pid_to_str;
  thread_db_ops.to_stratum = thread_stratum;
  thread_db_ops.to_has_thread_control = tc_schedlock;
  thread_db_ops.to_get_thread_local_address
    = thread_db_get_thread_local_address;
  thread_db_ops.to_magic = OPS_MAGIC;
}

void
_initialize_thread_db (void)
{
  /* Only initialize the module if we can load libthread_db.  */
  if (thread_db_load ())
    {
      init_thread_db_ops ();
      add_target (&thread_db_ops);

      /* Add ourselves to objfile event chain.  */
      target_new_objfile_chain = target_new_objfile_hook;
      target_new_objfile_hook = thread_db_new_objfile;
    }
}

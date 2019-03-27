/* Thread management interface, for the remote server for GDB.
   Copyright 2002
   Free Software Foundation, Inc.

   Contributed by MontaVista Software.

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

#include "server.h"

#include "linux-low.h"

extern int debug_threads;

#ifdef HAVE_THREAD_DB_H
#include <thread_db.h>
#endif

/* Correct for all GNU/Linux targets (for quite some time).  */
#define GDB_GREGSET_T elf_gregset_t
#define GDB_FPREGSET_T elf_fpregset_t

#ifndef HAVE_ELF_FPREGSET_T
/* Make sure we have said types.  Not all platforms bring in <linux/elf.h>
   via <sys/procfs.h>.  */
#ifdef HAVE_LINUX_ELF_H
#include <linux/elf.h>
#endif
#endif

#include "../gdb_proc_service.h"

/* Structure that identifies the child process for the
   <proc_service.h> interface.  */
static struct ps_prochandle proc_handle;

/* Connection to the libthread_db library.  */
static td_thragent_t *thread_agent;

static int find_new_threads_callback (const td_thrhandle_t *th_p, void *data);

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

#if 0
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
#endif

static void
thread_db_create_event (CORE_ADDR where)
{
  td_event_msg_t msg;
  td_err_e err;
  struct inferior_linux_data *tdata;

  if (debug_threads)
    fprintf (stderr, "Thread creation event.\n");

  tdata = inferior_target_data (current_inferior);

  /* FIXME: This assumes we don't get another event.
     In the LinuxThreads implementation, this is safe,
     because all events come from the manager thread
     (except for its own creation, of course).  */
  err = td_ta_event_getmsg (thread_agent, &msg);
  if (err != TD_OK)
    fprintf (stderr, "thread getmsg err: %s\n",
	     thread_db_err_str (err));

  /* msg.event == TD_EVENT_CREATE */

  find_new_threads_callback (msg.th_p, NULL);
}

#if 0
static void
thread_db_death_event (CORE_ADDR where)
{
  if (debug_threads)
    fprintf (stderr, "Thread death event.\n");
}
#endif

static int
thread_db_enable_reporting ()
{
  td_thr_events_t events;
  td_notify_t notify;
  td_err_e err;

  /* Set the process wide mask saying which events we're interested in.  */
  td_event_emptyset (&events);
  td_event_addset (&events, TD_CREATE);

#if 0
  /* This is reported to be broken in glibc 2.1.3.  A different approach
     will be necessary to support that.  */
  td_event_addset (&events, TD_DEATH);
#endif

  err = td_ta_set_event (thread_agent, &events);
  if (err != TD_OK)
    {
      warning ("Unable to set global thread event mask: %s",
               thread_db_err_str (err));
      return 0;
    }

  /* Get address for thread creation breakpoint.  */
  err = td_ta_event_addr (thread_agent, TD_CREATE, &notify);
  if (err != TD_OK)
    {
      warning ("Unable to get location for thread creation breakpoint: %s",
	       thread_db_err_str (err));
      return 0;
    }
  set_breakpoint_at ((CORE_ADDR) (unsigned long) notify.u.bptaddr,
		     thread_db_create_event);

#if 0
  /* Don't concern ourselves with reported thread deaths, only
     with actual thread deaths (via wait).  */

  /* Get address for thread death breakpoint.  */
  err = td_ta_event_addr (thread_agent, TD_DEATH, &notify);
  if (err != TD_OK)
    {
      warning ("Unable to get location for thread death breakpoint: %s",
	       thread_db_err_str (err));
      return;
    }
  set_breakpoint_at ((CORE_ADDR) (unsigned long) notify.u.bptaddr,
		     thread_db_death_event);
#endif

  return 1;
}

static void
maybe_attach_thread (const td_thrhandle_t *th_p, td_thrinfo_t *ti_p)
{
  td_err_e err;
  struct thread_info *inferior;
  struct process_info *process;

  /* If we are attaching to our first thread, things are a little
     different.  */
  if (all_threads.head == all_threads.tail)
    {
      inferior = (struct thread_info *) all_threads.head;
      process = get_thread_process (inferior);
      if (process->thread_known == 0)
	{
	  /* Switch to indexing the threads list by TID.  */
	  change_inferior_id (&all_threads, ti_p->ti_tid);
	  goto found;
	}
    }
  
  inferior = (struct thread_info *) find_inferior_id (&all_threads,
						      ti_p->ti_tid);
  if (inferior != NULL)
    return;

  if (debug_threads)
    fprintf (stderr, "Attaching to thread %ld (LWP %d)\n",
	     ti_p->ti_tid, ti_p->ti_lid);
  linux_attach_lwp (ti_p->ti_lid, ti_p->ti_tid);
  inferior = (struct thread_info *) find_inferior_id (&all_threads,
						      ti_p->ti_tid);
  if (inferior == NULL)
    {
      warning ("Could not attach to thread %ld (LWP %d)\n",
	       ti_p->ti_tid, ti_p->ti_lid);
      return;
    }

  process = inferior_target_data (inferior);

found:
  new_thread_notify (ti_p->ti_tid);

  process->tid = ti_p->ti_tid;
  process->lwpid = ti_p->ti_lid;

  process->thread_known = 1;
  err = td_thr_event_enable (th_p, 1);
  if (err != TD_OK)
    error ("Cannot enable thread event reporting for %d: %s",
           ti_p->ti_lid, thread_db_err_str (err));
}

static int
find_new_threads_callback (const td_thrhandle_t *th_p, void *data)
{
  td_thrinfo_t ti;
  td_err_e err;

  err = td_thr_get_info (th_p, &ti);
  if (err != TD_OK)
    error ("Cannot get thread info: %s", thread_db_err_str (err));

  /* Check for zombies.  */
  if (ti.ti_state == TD_THR_UNKNOWN || ti.ti_state == TD_THR_ZOMBIE)
    return 0;

  maybe_attach_thread (th_p, &ti);

  return 0;
}

static void
thread_db_find_new_threads (void)
{
  td_err_e err;

  /* Iterate over all user-space threads to discover new threads.  */
  err = td_ta_thr_iter (thread_agent, find_new_threads_callback, NULL,
			TD_THR_ANY_STATE, TD_THR_LOWEST_PRIORITY,
			TD_SIGNO_MASK, TD_THR_ANY_USER_FLAGS);
  if (err != TD_OK)
    error ("Cannot find new threads: %s", thread_db_err_str (err));
}

int
thread_db_init ()
{
  int err;

  proc_handle.pid = ((struct inferior_list_entry *)current_inferior)->id;

  err = td_ta_new (&proc_handle, &thread_agent);
  switch (err)
    {
    case TD_NOLIBTHREAD:
      /* No thread library was detected.  */
      return 0;

    case TD_OK:
      /* The thread library was detected.  */

      if (thread_db_enable_reporting () == 0)
	return 0;
      thread_db_find_new_threads ();
      return 1;

    default:
      warning ("error initializing thread_db library.");
    }

  return 0;
}

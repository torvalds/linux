/* Low level Unix child interface to ttrace, for GDB when running under HP-UX.
   Copyright 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1998,
   1999, 2000, 2001, 2003
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
#include "frame.h"
#include "inferior.h"
#include "target.h"
#include "gdb_string.h"
#include "gdb_wait.h"
#include "command.h"
#include "gdbthread.h"

/* We need pstat functionality so that we can get the exec file
   for a process we attach to.

   According to HP, we should use the 64bit interfaces, so we
   define _PSTAT64 to achieve this.  */
#define _PSTAT64
#include <sys/pstat.h>

/* Some hackery to work around a use of the #define name NO_FLAGS
 * in both gdb and HPUX (bfd.h and /usr/include/machine/vmparam.h).
 */
#ifdef  NO_FLAGS
#define INFTTRACE_TEMP_HACK NO_FLAGS
#undef  NO_FLAGS
#endif

#ifdef USG
#include <sys/types.h>
#endif

#include <sys/param.h>
#include <sys/dir.h>
#include <signal.h>
#include <sys/ioctl.h>

#include <sys/ttrace.h>
#include <sys/mman.h>

#ifndef NO_PTRACE_H
#ifdef PTRACE_IN_WRONG_PLACE
#include <ptrace.h>
#else
#include <sys/ptrace.h>
#endif
#endif /* NO_PTRACE_H */

/* Second half of the hackery above.  Non-ANSI C, so
 * we can't use "#error", alas.
 */
#ifdef NO_FLAGS
#if (NO_FLAGS != INFTTRACE_TEMP_HACK )
  /* #error "Hackery to remove warning didn't work right" */
#else
  /* Ok, new def'n of NO_FLAGS is same as old one; no action needed. */
#endif
#else
  /* #error "Didn't get expected re-definition of NO_FLAGS" */
#define NO_FLAGS INFTTRACE_TEMP_HACK
#endif

#if !defined (PT_SETTRC)
#define PT_SETTRC	0	/* Make process traceable by parent */
#endif
#if !defined (PT_READ_I)
#define PT_READ_I	1	/* Read word from text space */
#endif
#if !defined (PT_READ_D)
#define	PT_READ_D	2	/* Read word from data space */
#endif
#if !defined (PT_READ_U)
#define PT_READ_U	3	/* Read word from kernel user struct */
#endif
#if !defined (PT_WRITE_I)
#define PT_WRITE_I	4	/* Write word to text space */
#endif
#if !defined (PT_WRITE_D)
#define PT_WRITE_D	5	/* Write word to data space */
#endif
#if !defined (PT_WRITE_U)
#define PT_WRITE_U	6	/* Write word to kernel user struct */
#endif
#if !defined (PT_CONTINUE)
#define PT_CONTINUE	7	/* Continue after signal */
#endif
#if !defined (PT_STEP)
#define PT_STEP		9	/* Set flag for single stepping */
#endif
#if !defined (PT_KILL)
#define PT_KILL		8	/* Send child a SIGKILL signal */
#endif

#ifndef PT_ATTACH
#define PT_ATTACH PTRACE_ATTACH
#endif
#ifndef PT_DETACH
#define PT_DETACH PTRACE_DETACH
#endif

#include "gdbcore.h"
#ifndef	NO_SYS_FILE
#include <sys/file.h>
#endif

/* This semaphore is used to coordinate the child and parent processes
   after a fork(), and before an exec() by the child.  See parent_attach_all
   for details.
 */
typedef struct
  {
    int parent_channel[2];	/* Parent "talks" to [1], child "listens" to [0] */
    int child_channel[2];	/* Child "talks" to [1], parent "listens" to [0] */
  }
startup_semaphore_t;

#define SEM_TALK (1)
#define SEM_LISTEN (0)

static startup_semaphore_t startup_semaphore;

/* See can_touch_threads_of_process for details. */
static int vforking_child_pid = 0;
static int vfork_in_flight = 0;

/* 1 if ok as results of a ttrace or ttrace_wait call, 0 otherwise.
 */
#define TT_OK( _status, _errno ) \
    (((_status) == 1) && ((_errno) == 0))

#define TTRACE_ARG_TYPE uint64_t

/* When supplied as the "addr" operand, ttrace interprets this
   to mean, "from the current address".
 */
#define TT_USE_CURRENT_PC ((TTRACE_ARG_TYPE) TT_NOPC)

/* When supplied as the "addr", "data" or "addr2" operand for most
   requests, ttrace interprets this to mean, "pay no heed to this
   argument".
 */
#define TT_NIL ((TTRACE_ARG_TYPE) TT_NULLARG)

/* This is capable of holding the value of a 32-bit register.  The
   value is always left-aligned in the buffer; i.e., [0] contains
   the most-significant byte of the register's value, and [sizeof(reg)]
   contains the least-significant value.

   ??rehrauer: Yes, this assumes that an int is 32-bits on HP-UX, and
   that registers are 32-bits on HP-UX.  The latter assumption changes
   with PA2.0.
 */
typedef int register_value_t;

/********************************************************************

                 How this works:

   1.  Thread numbers

   The rest of GDB sees threads as being things with different
   "pid" (process id) values.  See "thread.c" for details.  The
   separate threads will be seen and reacted to if infttrace passes
   back different pid values (for _events_).  See wait_for_inferior
   in inftarg.c.

   So infttrace is going to use thread ids externally, pretending
   they are process ids, and keep track internally so that it can
   use the real process id (and thread id) when calling ttrace.

   The data structure that supports this is a linked list of the
   current threads.  Since at some date infttrace will have to
   deal with multiple processes, each list element records its
   corresponding pid, rather than having a single global.

   Note that the list is only approximately current; that's ok, as
   it's up to date when we need it (we hope!).  Also, it can contain
   dead threads, as there's no harm if it does.

   The approach taken here is to bury the translation from external
   to internal inside "call_ttrace" and a few other places.

   There are some wrinkles:

   o  When GDB forks itself to create the debug target process,
      there's only a pid of 0 around in the child, so the
      TT_PROC_SETTRC operation uses a more direct call to ttrace;
      Similiarly, the initial setting of the event mask happens
      early as  well, and so is also special-cased, and an attach
      uses a real pid;

   o  We define an unthreaded application as having a "pseudo"
      thread;

   o  To keep from confusing the rest of GDB, we don't switch
      the PID for the pseudo thread to a TID.  A table will help:

      Rest of GDB sees these PIDs:     pid   tid1  tid2  tid3 ...
                                        
      Our thread list stores:          pid   pid   pid   pid  ...
                                       tid0  tid1  tid2  tid3
      
      Ttrace sees these TIDS:          tid0  tid1  tid2  tid3 ...

      Both pid and tid0 will map to tid0, as there are infttrace.c-internal
      calls to ttrace using tid0.

   2. Step and Continue

   Since we're implementing the "stop the world" model, sub-model
   "other threads run during step", we have some stuff to do:

   o  User steps require continuing all threads other than the
      one the user is stepping;

   o  Internal debugger steps (such as over a breakpoint or watchpoint,
      but not out of a library load thunk) require stepping only
      the selected thread; this means that we have to report the
      step finish on that thread, which can lead to complications;

   o  When a thread is created, it is created running, rather
      than stopped--so we have to stop it.

   The OS doesn't guarantee the stopped thread list will be stable,
   no does it guarantee where on the stopped thread list a thread
   that is single-stepped will wind up: it's possible that it will
   be off the list for a while, it's possible the step will complete
   and it will be re-posted to the end...

   This means we have to scan the stopped thread list, build up
   a work-list, and then run down the work list; we can't do the
   step/continue during the scan.

   3. Buffering events

   Then there's the issue of waiting for an event.  We do this by
   noticing how many events are reported at the end of each wait.
   From then on, we "fake" all resumes and steps, returning instantly,
   and don't do another wait.  Once all pending events are reported,
   we can really resume again.

   To keep this hidden, all the routines which know about tids and
   pids or real events and simulated ones are static (file-local).

   This code can make lots of calls to ttrace, in particular it
   can spin down the list of thread states more than once.  If this
   becomes a performance hit, the spin could be done once and the
   various "tsp" blocks saved, keeping all later spins in this
   process.

   The O/S doesn't promise to keep the list straight, and so we must
   re-scan a lot.  By observation, it looks like a single-step/wait
   puts the stepped thread at the end of the list but doesn't change
   it otherwise.

****************************************************************
*/

/* Uncomment these to turn on various debugging output */
/* #define THREAD_DEBUG */
/* #define WAIT_BUFFER_DEBUG */
/* #define PARANOIA */


#define INFTTRACE_ALL_THREADS (-1)
#define INFTTRACE_STEP        (1)
#define INFTTRACE_CONTINUE    (0)

/* FIX: this is used in inftarg.c/child_wait, in a hack.
 */
extern int not_same_real_pid;

/* This is used to count buffered events.
 */
static unsigned int more_events_left = 0;

/* Process state.
 */
typedef enum process_state_enum
  {
    STOPPED,
    FAKE_STEPPING,
    FAKE_CONTINUE,		/* For later use */
    RUNNING,
    FORKING,
    VFORKING
  }
process_state_t;

static process_state_t process_state = STOPPED;

/* User-specified stepping modality.
 */
typedef enum stepping_mode_enum
  {
    DO_DEFAULT,			/* ...which is a continue! */
    DO_STEP,
    DO_CONTINUE
  }
stepping_mode_t;

/* Action to take on an attach, depends on
 * what kind (user command, fork, vfork).
 *
 * At the moment, this is either:
 *
 * o  continue with a SIGTRAP signal, or
 *
 * o  leave stopped.
 */
typedef enum attach_continue_enum
  {
    DO_ATTACH_CONTINUE,
    DONT_ATTACH_CONTINUE
  }
attach_continue_t;

/* This flag is true if we are doing a step-over-bpt
 * with buffered events.  We will have to be sure to
 * report the right thread, as otherwise the spaghetti
 * code in "infrun.c/wait_for_inferior" will get
 * confused.
 */
static int doing_fake_step = 0;
static lwpid_t fake_step_tid = 0;


/****************************************************
 * Thread information structure routines and types. *
 ****************************************************
 */
typedef
struct thread_info_struct
  {
    int am_pseudo;		/* This is a pseudo-thread for the process. */
    int pid;			/* Process ID */
    lwpid_t tid;		/* Thread  ID */
    int handled;		/* 1 if a buffered event was handled. */
    int seen;			/* 1 if this thread was seen on a traverse. */
    int terminated;		/* 1 if thread has terminated. */
    int have_signal;		/* 1 if signal to be sent */
    enum target_signal signal_value;	/* Signal to send */
    int have_start;		/* 1 if alternate starting address */
    stepping_mode_t stepping_mode;	/* Whether to step or continue */
    CORE_ADDR start;		/* Where to start */
    int have_state;		/* 1 if the event state has been set */
    ttstate_t last_stop_state;	/* The most recently-waited event for this thread. */
    struct thread_info_struct
     *next;			/* All threads are linked via this field. */
    struct thread_info_struct
     *next_pseudo;		/* All pseudo-threads are linked via this field. */
  }
thread_info;

typedef
struct thread_info_header_struct
  {
    int count;
    thread_info *head;
    thread_info *head_pseudo;

  }
thread_info_header;

static thread_info_header thread_head =
{0, NULL, NULL};
static thread_info_header deleted_threads =
{0, NULL, NULL};

static ptid_t saved_real_ptid;


/*************************************************
 *          Debugging support functions          *
 *************************************************
 */
CORE_ADDR
get_raw_pc (lwpid_t ttid)
{
  unsigned long pc_val;
  int offset;
  int res;

  offset = register_addr (PC_REGNUM, U_REGS_OFFSET);
  res = read_from_register_save_state (
					ttid,
					(TTRACE_ARG_TYPE) offset,
					(char *) &pc_val,
					sizeof (pc_val));
  if (res <= 0)
    {
      return (CORE_ADDR) pc_val;
    }
  else
    {
      return (CORE_ADDR) 0;
    }
}

static char *
get_printable_name_of_stepping_mode (stepping_mode_t mode)
{
  switch (mode)
    {
    case DO_DEFAULT:
      return "DO_DEFAULT";
    case DO_STEP:
      return "DO_STEP";
    case DO_CONTINUE:
      return "DO_CONTINUE";
    default:
      return "?unknown mode?";
    }
}

/* This function returns a pointer to a string describing the
 * ttrace event being reported.
 */
char *
get_printable_name_of_ttrace_event (ttevents_t event)
{
  /* This enumeration is "gappy", so don't use a table. */
  switch (event)
    {

    case TTEVT_NONE:
      return "TTEVT_NONE";
    case TTEVT_SIGNAL:
      return "TTEVT_SIGNAL";
    case TTEVT_FORK:
      return "TTEVT_FORK";
    case TTEVT_EXEC:
      return "TTEVT_EXEC";
    case TTEVT_EXIT:
      return "TTEVT_EXIT";
    case TTEVT_VFORK:
      return "TTEVT_VFORK";
    case TTEVT_SYSCALL_RETURN:
      return "TTEVT_SYSCALL_RETURN";
    case TTEVT_LWP_CREATE:
      return "TTEVT_LWP_CREATE";
    case TTEVT_LWP_TERMINATE:
      return "TTEVT_LWP_TERMINATE";
    case TTEVT_LWP_EXIT:
      return "TTEVT_LWP_EXIT";
    case TTEVT_LWP_ABORT_SYSCALL:
      return "TTEVT_LWP_ABORT_SYSCALL";
    case TTEVT_SYSCALL_ENTRY:
      return "TTEVT_SYSCALL_ENTRY";
    case TTEVT_SYSCALL_RESTART:
      return "TTEVT_SYSCALL_RESTART";
    default:
      return "?new event?";
    }
}


/* This function translates the ttrace request enumeration into
 * a character string that is its printable (aka "human readable")
 * name.
 */
char *
get_printable_name_of_ttrace_request (ttreq_t request)
{
  if (!IS_TTRACE_REQ (request))
    return "?bad req?";

  /* This enumeration is "gappy", so don't use a table. */
  switch (request)
    {
    case TT_PROC_SETTRC:
      return "TT_PROC_SETTRC";
    case TT_PROC_ATTACH:
      return "TT_PROC_ATTACH";
    case TT_PROC_DETACH:
      return "TT_PROC_DETACH";
    case TT_PROC_RDTEXT:
      return "TT_PROC_RDTEXT";
    case TT_PROC_WRTEXT:
      return "TT_PROC_WRTEXT";
    case TT_PROC_RDDATA:
      return "TT_PROC_RDDATA";
    case TT_PROC_WRDATA:
      return "TT_PROC_WRDATA";
    case TT_PROC_STOP:
      return "TT_PROC_STOP";
    case TT_PROC_CONTINUE:
      return "TT_PROC_CONTINUE";
    case TT_PROC_GET_PATHNAME:
      return "TT_PROC_GET_PATHNAME";
    case TT_PROC_GET_EVENT_MASK:
      return "TT_PROC_GET_EVENT_MASK";
    case TT_PROC_SET_EVENT_MASK:
      return "TT_PROC_SET_EVENT_MASK";
    case TT_PROC_GET_FIRST_LWP_STATE:
      return "TT_PROC_GET_FIRST_LWP_STATE";
    case TT_PROC_GET_NEXT_LWP_STATE:
      return "TT_PROC_GET_NEXT_LWP_STATE";
    case TT_PROC_EXIT:
      return "TT_PROC_EXIT";
    case TT_PROC_GET_MPROTECT:
      return "TT_PROC_GET_MPROTECT";
    case TT_PROC_SET_MPROTECT:
      return "TT_PROC_SET_MPROTECT";
    case TT_PROC_SET_SCBM:
      return "TT_PROC_SET_SCBM";
    case TT_LWP_STOP:
      return "TT_LWP_STOP";
    case TT_LWP_CONTINUE:
      return "TT_LWP_CONTINUE";
    case TT_LWP_SINGLE:
      return "TT_LWP_SINGLE";
    case TT_LWP_RUREGS:
      return "TT_LWP_RUREGS";
    case TT_LWP_WUREGS:
      return "TT_LWP_WUREGS";
    case TT_LWP_GET_EVENT_MASK:
      return "TT_LWP_GET_EVENT_MASK";
    case TT_LWP_SET_EVENT_MASK:
      return "TT_LWP_SET_EVENT_MASK";
    case TT_LWP_GET_STATE:
      return "TT_LWP_GET_STATE";
    default:
      return "?new req?";
    }
}


/* This function translates the process state enumeration into
 * a character string that is its printable (aka "human readable")
 * name.
 */
static char *
get_printable_name_of_process_state (process_state_t process_state)
{
  switch (process_state)
    {
    case STOPPED:
      return "STOPPED";
    case FAKE_STEPPING:
      return "FAKE_STEPPING";
    case RUNNING:
      return "RUNNING";
    case FORKING:
      return "FORKING";
    case VFORKING:
      return "VFORKING";
    default:
      return "?some unknown state?";
    }
}

/* Set a ttrace thread state to a safe, initial state.
 */
static void
clear_ttstate_t (ttstate_t *tts)
{
  tts->tts_pid = 0;
  tts->tts_lwpid = 0;
  tts->tts_user_tid = 0;
  tts->tts_event = TTEVT_NONE;
}

/* Copy ttrace thread state TTS_FROM into TTS_TO.
 */
static void
copy_ttstate_t (ttstate_t *tts_to, ttstate_t *tts_from)
{
  memcpy ((char *) tts_to, (char *) tts_from, sizeof (*tts_to));
}

/* Are there any live threads we know about?
 */
static int
any_thread_records (void)
{
  return (thread_head.count > 0);
}

/* Create, fill in and link in a thread descriptor.
 */
static thread_info *
create_thread_info (int pid, lwpid_t tid)
{
  thread_info *new_p;
  thread_info *p;
  int thread_count_of_pid;

  new_p = xmalloc (sizeof (thread_info));
  new_p->pid = pid;
  new_p->tid = tid;
  new_p->have_signal = 0;
  new_p->have_start = 0;
  new_p->have_state = 0;
  clear_ttstate_t (&new_p->last_stop_state);
  new_p->am_pseudo = 0;
  new_p->handled = 0;
  new_p->seen = 0;
  new_p->terminated = 0;
  new_p->next = NULL;
  new_p->next_pseudo = NULL;
  new_p->stepping_mode = DO_DEFAULT;

  if (0 == thread_head.count)
    {
#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("First thread, pid %d tid %d!\n", pid, tid);
#endif
      saved_real_ptid = inferior_ptid;
    }
  else
    {
#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("Subsequent thread, pid %d tid %d\n", pid, tid);
#endif
    }

  /* Another day, another thread...
   */
  thread_head.count++;

  /* The new thread always goes at the head of the list.
   */
  new_p->next = thread_head.head;
  thread_head.head = new_p;

  /* Is this the "pseudo" thread of a process?  It is if there's
   * no other thread for this process on the list.  (Note that this
   * accomodates multiple processes, such as we see even for simple
   * cases like forking "non-threaded" programs.)
   */
  p = thread_head.head;
  thread_count_of_pid = 0;
  while (p)
    {
      if (p->pid == new_p->pid)
	thread_count_of_pid++;
      p = p->next;
    }

  /* Did we see any other threads for this pid?  (Recall that we just
   * added this thread to the list...)
   */
  if (thread_count_of_pid == 1)
    {
      new_p->am_pseudo = 1;
      new_p->next_pseudo = thread_head.head_pseudo;
      thread_head.head_pseudo = new_p;
    }

  return new_p;
}

/* Get rid of our thread info.
 */
static void
clear_thread_info (void)
{
  thread_info *p;
  thread_info *q;

#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("Clearing all thread info\n");
#endif

  p = thread_head.head;
  while (p)
    {
      q = p;
      p = p->next;
      xfree (q);
    }

  thread_head.head = NULL;
  thread_head.head_pseudo = NULL;
  thread_head.count = 0;

  p = deleted_threads.head;
  while (p)
    {
      q = p;
      p = p->next;
      xfree (q);
    }

  deleted_threads.head = NULL;
  deleted_threads.head_pseudo = NULL;
  deleted_threads.count = 0;

  /* No threads, so can't have pending events.
   */
  more_events_left = 0;
}

/* Given a tid, find the thread block for it.
 */
static thread_info *
find_thread_info (lwpid_t tid)
{
  thread_info *p;

  for (p = thread_head.head; p; p = p->next)
    {
      if (p->tid == tid)
	{
	  return p;
	}
    }

  for (p = deleted_threads.head; p; p = p->next)
    {
      if (p->tid == tid)
	{
	  return p;
	}
    }

  return NULL;
}

/* For any but the pseudo thread, this maps to the
 * thread ID.  For the pseudo thread, if you pass either
 * the thread id or the PID, you get the pseudo thread ID.
 *
 * We have to be prepared for core gdb to ask about
 * deleted threads.  We do the map, but we don't like it.
 */
static lwpid_t
map_from_gdb_tid (lwpid_t gdb_tid)
{
  thread_info *p;

  /* First assume gdb_tid really is a tid, and try to find a
   * matching entry on the threads list.
   */
  for (p = thread_head.head; p; p = p->next)
    {
      if (p->tid == gdb_tid)
	return gdb_tid;
    }

  /* It doesn't appear to be a tid; perhaps it's really a pid?
   * Try to find a "pseudo" thread entry on the threads list.
   */
  for (p = thread_head.head_pseudo; p != NULL; p = p->next_pseudo)
    {
      if (p->pid == gdb_tid)
	return p->tid;
    }

  /* Perhaps it's the tid of a deleted thread we may still
   * have some knowledge of?
   */
  for (p = deleted_threads.head; p; p = p->next)
    {
      if (p->tid == gdb_tid)
	return gdb_tid;
    }

  /* Or perhaps it's the pid of a deleted process we may still
   * have knowledge of?
   */
  for (p = deleted_threads.head_pseudo; p != NULL; p = p->next_pseudo)
    {
      if (p->pid == gdb_tid)
	return p->tid;
    }

  return 0;			/* Error? */
}

/* Map the other way: from a real tid to the
 * "pid" known by core gdb.  This tid may be
 * for a thread that just got deleted, so we
 * also need to consider deleted threads.
 */
static lwpid_t
map_to_gdb_tid (lwpid_t real_tid)
{
  thread_info *p;

  for (p = thread_head.head; p; p = p->next)
    {
      if (p->tid == real_tid)
	{
	  if (p->am_pseudo)
	    return p->pid;
	  else
	    return real_tid;
	}
    }

  for (p = deleted_threads.head; p; p = p->next)
    {
      if (p->tid == real_tid)
	if (p->am_pseudo)
	  return p->pid;	/* Error? */
	else
	  return real_tid;
    }

  return 0;			/* Error?  Never heard of this thread! */
}

/* Do any threads have saved signals?
 */
static int
saved_signals_exist (void)
{
  thread_info *p;

  for (p = thread_head.head; p; p = p->next)
    {
      if (p->have_signal)
	{
	  return 1;
	}
    }

  return 0;
}

/* Is this the tid for the zero-th thread?
 */
static int
is_pseudo_thread (lwpid_t tid)
{
  thread_info *p = find_thread_info (tid);
  if (NULL == p || p->terminated)
    return 0;
  else
    return p->am_pseudo;
}

/* Is this thread terminated?
 */
static int
is_terminated (lwpid_t tid)
{
  thread_info *p = find_thread_info (tid);

  if (NULL != p)
    return p->terminated;

  return 0;
}

/* Is this pid a real PID or a TID?
 */
static int
is_process_id (int pid)
{
  lwpid_t tid;
  thread_info *tinfo;
  pid_t this_pid;
  int this_pid_count;

  /* What does PID really represent?
   */
  tid = map_from_gdb_tid (pid);
  if (tid <= 0)
    return 0;			/* Actually, is probably an error... */

  tinfo = find_thread_info (tid);

  /* Does it appear to be a true thread?
   */
  if (!tinfo->am_pseudo)
    return 0;

  /* Else, it looks like it may be a process.  See if there's any other
   * threads with the same process ID, though.  If there are, then TID
   * just happens to be the first thread of several for this process.
   */
  this_pid = tinfo->pid;
  this_pid_count = 0;
  for (tinfo = thread_head.head; tinfo; tinfo = tinfo->next)
    {
      if (tinfo->pid == this_pid)
	this_pid_count++;
    }

  return (this_pid_count == 1);
}


/* Add a thread to our info.  Prevent duplicate entries.
 */
static thread_info *
add_tthread (int pid, lwpid_t tid)
{
  thread_info *p;

  p = find_thread_info (tid);
  if (NULL == p)
    p = create_thread_info (pid, tid);

  return p;
}

/* Notice that a thread was deleted.
 */
static void
del_tthread (lwpid_t tid)
{
  thread_info *p;
  thread_info *chase;

  if (thread_head.count <= 0)
    {
      error ("Internal error in thread database.");
      return;
    }

  chase = NULL;
  for (p = thread_head.head; p; p = p->next)
    {
      if (p->tid == tid)
	{

#ifdef THREAD_DEBUG
	  if (debug_on)
	    printf ("Delete here: %d \n", tid);
#endif

	  if (p->am_pseudo)
	    {
	      /*
	       * Deleting a main thread is ok if we're doing
	       * a parent-follow on a child; this is odd but
	       * not wrong.  It apparently _doesn't_ happen
	       * on the child-follow, as we don't just delete
	       * the pseudo while keeping the rest of the
	       * threads around--instead, we clear out the whole
	       * thread list at once.
	       */
	      thread_info *q;
	      thread_info *q_chase;

	      q_chase = NULL;
	      for (q = thread_head.head_pseudo; q; q = q->next)
		{
		  if (q == p)
		    {
		      /* Remove from pseudo list.
		       */
		      if (q_chase == NULL)
			thread_head.head_pseudo = p->next_pseudo;
		      else
			q_chase->next = p->next_pseudo;
		    }
		  else
		    q_chase = q;
		}
	    }

	  /* Remove from live list.
	   */
	  thread_head.count--;

	  if (NULL == chase)
	    thread_head.head = p->next;
	  else
	    chase->next = p->next;

	  /* Add to deleted thread list.
	   */
	  p->next = deleted_threads.head;
	  deleted_threads.head = p;
	  deleted_threads.count++;
	  if (p->am_pseudo)
	    {
	      p->next_pseudo = deleted_threads.head_pseudo;
	      deleted_threads.head_pseudo = p;
	    }
	  p->terminated = 1;

	  return;
	}

      else
	chase = p;
    }
}

/* Get the pid for this tid. (Has to be a real TID!).
 */
static int
get_pid_for (lwpid_t tid)
{
  thread_info *p;

  for (p = thread_head.head; p; p = p->next)
    {
      if (p->tid == tid)
	{
	  return p->pid;
	}
    }

  for (p = deleted_threads.head; p; p = p->next)
    {
      if (p->tid == tid)
	{
	  return p->pid;
	}
    }

  return 0;
}

/* Note that this thread's current event has been handled.
 */
static void
set_handled (int pid, lwpid_t tid)
{
  thread_info *p;

  p = find_thread_info (tid);
  if (NULL == p)
    p = add_tthread (pid, tid);

  p->handled = 1;
}

/* Was this thread's current event handled?
 */
static int
was_handled (lwpid_t tid)
{
  thread_info *p;

  p = find_thread_info (tid);
  if (NULL != p)
    return p->handled;

  return 0;			/* New threads have not been handled */
}

/* Set this thread to unhandled.
 */
static void
clear_handled (lwpid_t tid)
{
  thread_info *p;

#ifdef WAIT_BUFFER_DEBUG
  if (debug_on)
    printf ("clear_handled %d\n", (int) tid);
#endif

  p = find_thread_info (tid);
  if (p == NULL)
    error ("Internal error: No thread state to clear?");

  p->handled = 0;
}

/* Set all threads to unhandled.
 */
static void
clear_all_handled (void)
{
  thread_info *p;

#ifdef WAIT_BUFFER_DEBUG
  if (debug_on)
    printf ("clear_all_handled\n");
#endif

  for (p = thread_head.head; p; p = p->next)
    {
      p->handled = 0;
    }

  for (p = deleted_threads.head; p; p = p->next)
    {
      p->handled = 0;
    }
}

/* Set this thread to default stepping mode.
 */
static void
clear_stepping_mode (lwpid_t tid)
{
  thread_info *p;

#ifdef WAIT_BUFFER_DEBUG
  if (debug_on)
    printf ("clear_stepping_mode %d\n", (int) tid);
#endif

  p = find_thread_info (tid);
  if (p == NULL)
    error ("Internal error: No thread state to clear?");

  p->stepping_mode = DO_DEFAULT;
}

/* Set all threads to do default continue on resume.
 */
static void
clear_all_stepping_mode (void)
{
  thread_info *p;

#ifdef WAIT_BUFFER_DEBUG
  if (debug_on)
    printf ("clear_all_stepping_mode\n");
#endif

  for (p = thread_head.head; p; p = p->next)
    {
      p->stepping_mode = DO_DEFAULT;
    }

  for (p = deleted_threads.head; p; p = p->next)
    {
      p->stepping_mode = DO_DEFAULT;
    }
}

/* Set all threads to unseen on this pass.
 */
static void
set_all_unseen (void)
{
  thread_info *p;

  for (p = thread_head.head; p; p = p->next)
    {
      p->seen = 0;
    }
}

#if (defined( THREAD_DEBUG ) || defined( PARANOIA ))
/* debugging routine.
 */
static void
print_tthread (thread_info *p)
{
  printf (" Thread pid %d, tid %d", p->pid, p->tid);
  if (p->have_state)
    printf (", event is %s",
	 get_printable_name_of_ttrace_event (p->last_stop_state.tts_event));

  if (p->am_pseudo)
    printf (", pseudo thread");

  if (p->have_signal)
    printf (", have signal 0x%x", p->signal_value);

  if (p->have_start)
    printf (", have start at 0x%x", p->start);

  printf (", step is %s", get_printable_name_of_stepping_mode (p->stepping_mode));

  if (p->handled)
    printf (", handled");
  else
    printf (", not handled");

  if (p->seen)
    printf (", seen");
  else
    printf (", not seen");

  printf ("\n");
}

static void
print_tthreads (void)
{
  thread_info *p;

  if (thread_head.count == 0)
    printf ("Thread list is empty\n");
  else
    {
      printf ("Thread list has ");
      if (thread_head.count == 1)
	printf ("1 entry:\n");
      else
	printf ("%d entries:\n", thread_head.count);
      for (p = thread_head.head; p; p = p->next)
	{
	  print_tthread (p);
	}
    }

  if (deleted_threads.count == 0)
    printf ("Deleted thread list is empty\n");
  else
    {
      printf ("Deleted thread list has ");
      if (deleted_threads.count == 1)
	printf ("1 entry:\n");
      else
	printf ("%d entries:\n", deleted_threads.count);

      for (p = deleted_threads.head; p; p = p->next)
	{
	  print_tthread (p);
	}
    }
}
#endif

/* Update the thread list based on the "seen" bits.
 */
static void
update_thread_list (void)
{
  thread_info *p;
  thread_info *chase;

  chase = NULL;
  for (p = thread_head.head; p; p = p->next)
    {
      /* Is this an "unseen" thread which really happens to be a process?
         If so, is it inferior_ptid and is a vfork in flight?  If yes to
         all, then DON'T REMOVE IT!  We're in the midst of moving a vfork
         operation, which is a multiple step thing, to the point where we
         can touch the parent again.  We've most likely stopped to examine
         the child at a late stage in the vfork, and if we're not following
         the child, we'd best not treat the parent as a dead "thread"...
       */
      if ((!p->seen) && p->am_pseudo && vfork_in_flight
	  && (p->pid != vforking_child_pid))
	p->seen = 1;

      if (!p->seen)
	{
	  /* Remove this one
	   */

#ifdef THREAD_DEBUG
	  if (debug_on)
	    printf ("Delete unseen thread: %d \n", p->tid);
#endif
	  del_tthread (p->tid);
	}
    }
}



/************************************************
 *            O/S call wrappers                 *
 ************************************************
 */

/* This function simply calls ttrace with the given arguments.  
 * It exists so that all calls to ttrace are isolated.  All
 * parameters should be as specified by "man 2 ttrace".
 *
 * No other "raw" calls to ttrace should exist in this module.
 */
static int
call_real_ttrace (ttreq_t request, pid_t pid, lwpid_t tid, TTRACE_ARG_TYPE addr,
		  TTRACE_ARG_TYPE data, TTRACE_ARG_TYPE addr2)
{
  int tt_status;

  errno = 0;
  tt_status = ttrace (request, pid, tid, addr, data, addr2);

#ifdef THREAD_DEBUG
  if (errno)
    {
      /* Don't bother for a known benign error: if you ask for the
       * first thread state, but there is only one thread and it's
       * not stopped, ttrace complains.
       *
       * We have this inside the #ifdef because our caller will do
       * this check for real.
       */
      if (request != TT_PROC_GET_FIRST_LWP_STATE
	  || errno != EPROTO)
	{
	  if (debug_on)
	    printf ("TT fail for %s, with pid %d, tid %d, status %d \n",
		    get_printable_name_of_ttrace_request (request),
		    pid, tid, tt_status);
	}
    }
#endif

#if 0
  /* ??rehrauer: It would probably be most robust to catch and report
   * failed requests here.  However, some clients of this interface
   * seem to expect to catch & deal with them, so we'd best not.
   */
  if (errno)
    {
      strcpy (reason_for_failure, "ttrace (");
      strcat (reason_for_failure, get_printable_name_of_ttrace_request (request));
      strcat (reason_for_failure, ")");
      printf ("ttrace error, errno = %d\n", errno);
      perror_with_name (reason_for_failure);
    }
#endif

  return tt_status;
}


/* This function simply calls ttrace_wait with the given arguments.  
 * It exists so that all calls to ttrace_wait are isolated.
 *
 * No "raw" calls to ttrace_wait should exist elsewhere.
 */
static int
call_real_ttrace_wait (int pid, lwpid_t tid, ttwopt_t option, ttstate_t *tsp,
		       size_t tsp_size)
{
  int ttw_status;
  thread_info *tinfo = NULL;

  errno = 0;
  ttw_status = ttrace_wait (pid, tid, option, tsp, tsp_size);

  if (errno)
    {
#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("TW fail with pid %d, tid %d \n", pid, tid);
#endif

      perror_with_name ("ttrace wait");
    }

  return ttw_status;
}


/* A process may have one or more kernel threads, of which all or
   none may be stopped.  This function returns the ID of the first
   kernel thread in a stopped state, or 0 if none are stopped.

   This function can be used with get_process_next_stopped_thread_id
   to iterate over the IDs of all stopped threads of this process.
 */
static lwpid_t
get_process_first_stopped_thread_id (int pid, ttstate_t *thread_state)
{
  int tt_status;

  tt_status = call_real_ttrace (TT_PROC_GET_FIRST_LWP_STATE,
				(pid_t) pid,
				(lwpid_t) TT_NIL,
				(TTRACE_ARG_TYPE) thread_state,
				(TTRACE_ARG_TYPE) sizeof (*thread_state),
				TT_NIL);

  if (errno)
    {
      if (errno == EPROTO)
	{
	  /* This is an error we can handle: there isn't any stopped
	   * thread.  This happens when we're re-starting the application
	   * and it has only one thread.  GET_NEXT handles the case of
	   * no more stopped threads well; GET_FIRST doesn't.  (A ttrace
	   * "feature".)
	   */
	  tt_status = 1;
	  errno = 0;
	  return 0;
	}
      else
	perror_with_name ("ttrace");
    }

  if (tt_status < 0)
    /* Failed somehow.
     */
    return 0;

  return thread_state->tts_lwpid;
}


/* This function returns the ID of the "next" kernel thread in a
   stopped state, or 0 if there are none.  "Next" refers to the
   thread following that of the last successful call to this
   function or to get_process_first_stopped_thread_id, using
   the value of thread_state returned by that call.

   This function can be used with get_process_first_stopped_thread_id
   to iterate over the IDs of all stopped threads of this process.
 */
static lwpid_t
get_process_next_stopped_thread_id (int pid, ttstate_t *thread_state)
{
  int tt_status;

  tt_status = call_real_ttrace (
				 TT_PROC_GET_NEXT_LWP_STATE,
				 (pid_t) pid,
				 (lwpid_t) TT_NIL,
				 (TTRACE_ARG_TYPE) thread_state,
				 (TTRACE_ARG_TYPE) sizeof (*thread_state),
				 TT_NIL);
  if (errno)
    perror_with_name ("ttrace");

  if (tt_status < 0)
    /* Failed
     */
    return 0;

  else if (tt_status == 0)
    {
      /* End of list, no next state.  Don't return the
       * tts_lwpid, as it's a meaningless "240".
       *
       * This is an HPUX "feature".
       */
      return 0;
    }

  return thread_state->tts_lwpid;
}

/* ??rehrauer: Eventually this function perhaps should be calling
   pid_to_thread_id.  However, that function currently does nothing
   for HP-UX.  Even then, I'm not clear whether that function
   will return a "kernel" thread ID, or a "user" thread ID.  If
   the former, we can just call it here.  If the latter, we must
   map from the "user" tid to a "kernel" tid.

   NOTE: currently not called.
 */
static lwpid_t
get_active_tid_of_pid (int pid)
{
  ttstate_t thread_state;

  return get_process_first_stopped_thread_id (pid, &thread_state);
}

/* This function returns 1 if tt_request is a ttrace request that
 * operates upon all threads of a (i.e., the entire) process.
 */
int
is_process_ttrace_request (ttreq_t tt_request)
{
  return IS_TTRACE_PROCREQ (tt_request);
}


/* This function translates a thread ttrace request into
 * the equivalent process request for a one-thread process.
 */
static ttreq_t
make_process_version (ttreq_t request)
{
  if (!IS_TTRACE_REQ (request))
    {
      error ("Internal error, bad ttrace request made\n");
      return -1;
    }

  switch (request)
    {
    case TT_LWP_STOP:
      return TT_PROC_STOP;

    case TT_LWP_CONTINUE:
      return TT_PROC_CONTINUE;

    case TT_LWP_GET_EVENT_MASK:
      return TT_PROC_GET_EVENT_MASK;

    case TT_LWP_SET_EVENT_MASK:
      return TT_PROC_SET_EVENT_MASK;

    case TT_LWP_SINGLE:
    case TT_LWP_RUREGS:
    case TT_LWP_WUREGS:
    case TT_LWP_GET_STATE:
      return -1;		/* No equivalent */

    default:
      return request;
    }
}


/* This function translates the "pid" used by the rest of
 * gdb to a real pid and a tid.  It then calls "call_real_ttrace"
 * with the given arguments.
 *
 * In general, other parts of this module should call this
 * function when they are dealing with external users, who only
 * have tids to pass (but they call it "pid" for historical
 * reasons).
 */
static int
call_ttrace (ttreq_t request, int gdb_tid, TTRACE_ARG_TYPE addr,
	     TTRACE_ARG_TYPE data, TTRACE_ARG_TYPE addr2)
{
  lwpid_t real_tid;
  int real_pid;
  ttreq_t new_request;
  int tt_status;
  char reason_for_failure[100];	/* Arbitrary size, should be big enough. */

#ifdef THREAD_DEBUG
  int is_interesting = 0;

  if (TT_LWP_RUREGS == request)
    {
      is_interesting = 1;	/* Adjust code here as desired */
    }

  if (is_interesting && 0 && debug_on)
    {
      if (!is_process_ttrace_request (request))
	{
	  printf ("TT: Thread request, tid is %d", gdb_tid);
	  printf ("== SINGLE at %x", addr);
	}
      else
	{
	  printf ("TT: Process request, tid is %d\n", gdb_tid);
	  printf ("==! SINGLE at %x", addr);
	}
    }
#endif

  /* The initial SETTRC and SET_EVENT_MASK calls (and all others
   * which happen before any threads get set up) should go
   * directly to "call_real_ttrace", so they don't happen here.
   *
   * But hardware watchpoints do a SET_EVENT_MASK, so we can't
   * rule them out....
   */
#ifdef THREAD_DEBUG
  if (request == TT_PROC_SETTRC && debug_on)
    printf ("Unexpected call for TT_PROC_SETTRC\n");
#endif

  /* Sometimes we get called with a bogus tid (e.g., if a
   * thread has terminated, we return 0; inftarg later asks
   * whether the thread has exited/forked/vforked).
   */
  if (gdb_tid == 0)
    {
      errno = ESRCH;		/* ttrace's response would probably be "No such process". */
      return -1;
    }

  /* All other cases should be able to expect that there are
   * thread records.
   */
  if (!any_thread_records ())
    {
#ifdef THREAD_DEBUG
      if (debug_on)
	warning ("No thread records for ttrace call");
#endif
      errno = ESRCH;		/* ttrace's response would be "No such process". */
      return -1;
    }

  /* OK, now the task is to translate the incoming tid into
   * a pid/tid pair.
   */
  real_tid = map_from_gdb_tid (gdb_tid);
  real_pid = get_pid_for (real_tid);

  /* Now check the result.  "Real_pid" is NULL if our list
   * didn't find it.  We have some tricks we can play to fix
   * this, however.
   */
  if (0 == real_pid)
    {
      ttstate_t thread_state;

#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("No saved pid for tid %d\n", gdb_tid);
#endif

      if (is_process_ttrace_request (request))
	{

	  /* Ok, we couldn't get a tid.  Try to translate to
	   * the equivalent process operation.  We expect this
	   * NOT to happen, so this is a desparation-type
	   * move.  It can happen if there is an internal
	   * error and so no "wait()" call is ever done.
	   */
	  new_request = make_process_version (request);
	  if (new_request == -1)
	    {

#ifdef THREAD_DEBUG
	      if (debug_on)
		printf ("...and couldn't make process version of thread operation\n");
#endif

	      /* Use hacky saved pid, which won't always be correct
	       * in the multi-process future.  Use tid as thread,
	       * probably dooming this to failure.  FIX!
	       */
	      if (! ptid_equal (saved_real_ptid, null_ptid))
		{
#ifdef THREAD_DEBUG
		  if (debug_on)
		    printf ("...using saved pid %d\n",
		            PIDGET (saved_real_ptid));
#endif

		  real_pid = PIDGET (saved_real_ptid);
		  real_tid = gdb_tid;
		}

	      else
		error ("Unable to perform thread operation");
	    }

	  else
	    {
	      /* Sucessfully translated this to a process request,
	       * which needs no thread value.
	       */
	      real_pid = gdb_tid;
	      real_tid = 0;
	      request = new_request;

#ifdef THREAD_DEBUG
	      if (debug_on)
		{
		  printf ("Translated thread request to process request\n");
		  if (ptid_equal (saved_real_ptid, null_ptid))
		    printf ("...but there's no saved pid\n");

		  else
		    {
		      if (gdb_tid != PIDGET (saved_real_ptid))
			printf ("...but have the wrong pid (%d rather than %d)\n",
				gdb_tid, PIDGET (saved_real_ptid));
		    }
		}
#endif
	    }			/* Translated to a process request */
	}			/* Is a process request */

      else
	{
	  /* We have to have a thread.  Ooops.
	   */
	  error ("Thread request with no threads (%s)",
		 get_printable_name_of_ttrace_request (request));
	}
    }

  /* Ttrace doesn't like to see tid values on process requests,
   * even if we have the right one.
   */
  if (is_process_ttrace_request (request))
    {
      real_tid = 0;
    }

#ifdef THREAD_DEBUG
  if (is_interesting && 0 && debug_on)
    {
      printf ("    now tid %d, pid %d\n", real_tid, real_pid);
      printf ("    request is %s\n", get_printable_name_of_ttrace_request (request));
    }
#endif

  /* Finally, the (almost) real call.
   */
  tt_status = call_real_ttrace (request, real_pid, real_tid, addr, data, addr2);

#ifdef THREAD_DEBUG
  if (is_interesting && debug_on)
    {
      if (!TT_OK (tt_status, errno)
	  && !(tt_status == 0 & errno == 0))
	printf (" got error (errno==%d, status==%d)\n", errno, tt_status);
    }
#endif

  return tt_status;
}


/* Stop all the threads of a process.

 * NOTE: use of TT_PROC_STOP can cause a thread with a real event
 *       to get a TTEVT_NONE event, discarding the old event.  Be
 *       very careful, and only call TT_PROC_STOP when you mean it!
 */
static void
stop_all_threads_of_process (pid_t real_pid)
{
  int ttw_status;

  ttw_status = call_real_ttrace (TT_PROC_STOP,
				 (pid_t) real_pid,
				 (lwpid_t) TT_NIL,
				 (TTRACE_ARG_TYPE) TT_NIL,
				 (TTRACE_ARG_TYPE) TT_NIL,
				 TT_NIL);
  if (errno)
    perror_with_name ("ttrace stop of other threads");
}


/* Under some circumstances, it's unsafe to attempt to stop, or even
   query the state of, a process' threads.

   In ttrace-based HP-UX, an example is a vforking child process.  The
   vforking parent and child are somewhat fragile, w/r/t what we can do
   what we can do to them with ttrace, until after the child exits or
   execs, or until the parent's vfork event is delivered.  Until that
   time, we must not try to stop the process' threads, or inquire how
   many there are, or even alter its data segments, or it typically dies
   with a SIGILL.  Sigh.

   This function returns 1 if this stopped process, and the event that
   we're told was responsible for its current stopped state, cannot safely
   have its threads examined.
 */
#define CHILD_VFORKED(evt,pid) \
  (((evt) == TTEVT_VFORK) && ((pid) != PIDGET (inferior_ptid)))
#define CHILD_URPED(evt,pid) \
  ((((evt) == TTEVT_EXEC) || ((evt) == TTEVT_EXIT)) && ((pid) != vforking_child_pid))
#define PARENT_VFORKED(evt,pid) \
  (((evt) == TTEVT_VFORK) && ((pid) == PIDGET (inferior_ptid)))

static int
can_touch_threads_of_process (int pid, ttevents_t stopping_event)
{
  if (CHILD_VFORKED (stopping_event, pid))
    {
      vforking_child_pid = pid;
      vfork_in_flight = 1;
    }

  else if (vfork_in_flight &&
	   (PARENT_VFORKED (stopping_event, pid) ||
	    CHILD_URPED (stopping_event, pid)))
    {
      vfork_in_flight = 0;
      vforking_child_pid = 0;
    }

  return !vfork_in_flight;
}


/* If we can find an as-yet-unhandled thread state of a
 * stopped thread of this process return 1 and set "tsp".
 * Return 0 if we can't.
 *
 * If this function is used when the threads of PIS haven't
 * been stopped, undefined behaviour is guaranteed!
 */
static int
select_stopped_thread_of_process (int pid, ttstate_t *tsp)
{
  lwpid_t candidate_tid, tid;
  ttstate_t candidate_tstate, tstate;

  /* If we're not allowed to touch the process now, then just
   * return the current value of *TSP.
   *
   * This supports "vfork".  It's ok, really, to double the
   * current event (the child EXEC, we hope!).
   */
  if (!can_touch_threads_of_process (pid, tsp->tts_event))
    return 1;

  /* Decide which of (possibly more than one) events to
   * return as the first one.  We scan them all so that
   * we always return the result of a fake-step first.
   */
  candidate_tid = 0;
  for (tid = get_process_first_stopped_thread_id (pid, &tstate);
       tid != 0;
       tid = get_process_next_stopped_thread_id (pid, &tstate))
    {
      /* TTEVT_NONE events are uninteresting to our clients.  They're
       * an artifact of our "stop the world" model--the thread is
       * stopped because we stopped it.
       */
      if (tstate.tts_event == TTEVT_NONE)
	{
	  set_handled (pid, tstate.tts_lwpid);
	}

      /* Did we just single-step a single thread, without letting any
       * of the others run?  Is this an event for that thread?
       *
       * If so, we believe our client would prefer to see this event
       * over any others.  (Typically the client wants to just push
       * one thread a little farther forward, and then go around
       * checking for what all threads are doing.)
       */
      else if (doing_fake_step && (tstate.tts_lwpid == fake_step_tid))
	{
#ifdef WAIT_BUFFER_DEBUG
	  /* It's possible here to see either a SIGTRAP (due to
	   * successful completion of a step) or a SYSCALL_ENTRY
	   * (due to a step completion with active hardware
	   * watchpoints).
	   */
	  if (debug_on)
	    printf ("Ending fake step with tid %d, state %s\n",
		    tstate.tts_lwpid,
		    get_printable_name_of_ttrace_event (tstate.tts_event));
#endif

	  /* Remember this one, and throw away any previous
	   * candidate.
	   */
	  candidate_tid = tstate.tts_lwpid;
	  candidate_tstate = tstate;
	}

#ifdef FORGET_DELETED_BPTS

      /* We can't just do this, as if we do, and then wind
       * up the loop with no unhandled events, we need to
       * handle that case--the appropriate reaction is to
       * just continue, but there's no easy way to do that.
       *
       * Better to put this in the ttrace_wait call--if, when
       * we fake a wait, we update our events based on the
       * breakpoint_here_pc call and find there are no more events,
       * then we better continue and so on.
       *
       * Or we could put it in the next/continue fake.
       * But it has to go in the buffering code, not in the
       * real go/wait code.
       */
      else if ((TTEVT_SIGNAL == tstate.tts_event)
	       && (5 == tstate.tts_u.tts_signal.tts_signo)
	       && (0 != get_raw_pc (tstate.tts_lwpid))
	       && !breakpoint_here_p (get_raw_pc (tstate.tts_lwpid)))
	{
	  /*
	   * If the user deleted a breakpoint while this
	   * breakpoint-hit event was buffered, we can forget
	   * it now.
	   */
#ifdef WAIT_BUFFER_DEBUG
	  if (debug_on)
	    printf ("Forgetting deleted bp hit for thread %d\n",
		    tstate.tts_lwpid);
#endif

	  set_handled (pid, tstate.tts_lwpid);
	}
#endif

      /* Else, is this the first "unhandled" event?  If so,
       * we believe our client wants to see it (if we don't
       * see a fake-step later on in the scan).
       */
      else if (!was_handled (tstate.tts_lwpid) && candidate_tid == 0)
	{
	  candidate_tid = tstate.tts_lwpid;
	  candidate_tstate = tstate;
	}

      /* This is either an event that has already been "handled",
       * and thus we believe is uninteresting to our client, or we
       * already have a candidate event.  Ignore it...
       */
    }

  /* What do we report?
   */
  if (doing_fake_step)
    {
      if (candidate_tid == fake_step_tid)
	{
	  /* Fake step.
	   */
	  tstate = candidate_tstate;
	}
      else
	{
	  warning ("Internal error: fake-step failed to complete.");
	  return 0;
	}
    }
  else if (candidate_tid != 0)
    {
      /* Found a candidate unhandled event.
       */
      tstate = candidate_tstate;
    }
  else if (tid != 0)
    {
      warning ("Internal error in call of ttrace_wait.");
      return 0;
    }
  else
    {
      warning ("Internal error: no unhandled thread event to select");
      return 0;
    }

  copy_ttstate_t (tsp, &tstate);
  return 1;
}				/* End of select_stopped_thread_of_process */

#ifdef PARANOIA
/* Check our internal thread data against the real thing.
 */
static void
check_thread_consistency (pid_t real_pid)
{
  int tid;			/* really lwpid_t */
  ttstate_t tstate;
  thread_info *p;

  /* Spin down the O/S list of threads, checking that they
   * match what we've got.
   */
  for (tid = get_process_first_stopped_thread_id (real_pid, &tstate);
       tid != 0;
       tid = get_process_next_stopped_thread_id (real_pid, &tstate))
    {

      p = find_thread_info (tid);

      if (NULL == p)
	{
	  warning ("No internal thread data for thread %d.", tid);
	  continue;
	}

      if (!p->seen)
	{
	  warning ("Inconsistent internal thread data for thread %d.", tid);
	}

      if (p->terminated)
	{
	  warning ("Thread %d is not terminated, internal error.", tid);
	  continue;
	}


#define TT_COMPARE( fld ) \
            tstate.fld != p->last_stop_state.fld

      if (p->have_state)
	{
	  if (TT_COMPARE (tts_pid)
	      || TT_COMPARE (tts_lwpid)
	      || TT_COMPARE (tts_user_tid)
	      || TT_COMPARE (tts_event)
	      || TT_COMPARE (tts_flags)
	      || TT_COMPARE (tts_scno)
	      || TT_COMPARE (tts_scnargs))
	    {
	      warning ("Internal thread data for thread %d is wrong.", tid);
	      continue;
	    }
	}
    }
}
#endif /* PARANOIA */


/* This function wraps calls to "call_real_ttrace_wait" so
 * that a actual wait is only done when all pending events
 * have been reported.
 *
 * Note that typically it is called with a pid of "0", i.e. 
 * the "don't care" value.
 *
 * Return value is the status of the pseudo wait.
 */
static int
call_ttrace_wait (int pid, ttwopt_t option, ttstate_t *tsp, size_t tsp_size)
{
  /* This holds the actual, for-real, true process ID.
   */
  static int real_pid;

  /* As an argument to ttrace_wait, zero pid
   * means "Any process", and zero tid means
   * "Any thread of the specified process".
   */
  int wait_pid = 0;
  lwpid_t wait_tid = 0;
  lwpid_t real_tid;

  int ttw_status = 0;		/* To be returned */

  thread_info *tinfo = NULL;

  if (pid != 0)
    {
      /* Unexpected case.
       */
#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("TW: Pid to wait on is %d\n", pid);
#endif

      if (!any_thread_records ())
	error ("No thread records for ttrace call w. specific pid");

      /* OK, now the task is to translate the incoming tid into
       * a pid/tid pair.
       */
      real_tid = map_from_gdb_tid (pid);
      real_pid = get_pid_for (real_tid);
#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("==TW: real pid %d, real tid %d\n", real_pid, real_tid);
#endif
    }


  /* Sanity checks and set-up.
   *                             Process State
   *
   *                        Stopped   Running    Fake-step  (v)Fork
   *                      \________________________________________
   *                      |
   *  No buffered events  |  error     wait       wait      wait
   *                      |
   *  Buffered events     |  debuffer  error      wait      debuffer (?)
   *
   */
  if (more_events_left == 0)
    {

      if (process_state == RUNNING)
	{
	  /* OK--normal call of ttrace_wait with no buffered events.
	   */
	  ;
	}
      else if (process_state == FAKE_STEPPING)
	{
	  /* Ok--call of ttrace_wait to support
	   * fake stepping with no buffered events.
	   *
	   * But we better be fake-stepping!
	   */
	  if (!doing_fake_step)
	    {
	      warning ("Inconsistent thread state.");
	    }
	}
      else if ((process_state == FORKING)
	       || (process_state == VFORKING))
	{
	  /* Ok--there are two processes, so waiting
	   * for the second while the first is stopped
	   * is ok.  Handled bits stay as they were.
	   */
	  ;
	}
      else if (process_state == STOPPED)
	{
	  warning ("Process not running at wait call.");
	}
      else
	/* No known state.
	 */
	warning ("Inconsistent process state.");
    }

  else
    {
      /* More events left
       */
      if (process_state == STOPPED)
	{
	  /* OK--buffered events being unbuffered.
	   */
	  ;
	}
      else if (process_state == RUNNING)
	{
	  /* An error--shouldn't have buffered events
	   * when running.
	   */
	  warning ("Trying to continue with buffered events:");
	}
      else if (process_state == FAKE_STEPPING)
	{
	  /*
	   * Better be fake-stepping!
	   */
	  if (!doing_fake_step)
	    {
	      warning ("Losing buffered thread events!\n");
	    }
	}
      else if ((process_state == FORKING)
	       || (process_state == VFORKING))
	{
	  /* Ok--there are two processes, so waiting
	   * for the second while the first is stopped
	   * is ok.  Handled bits stay as they were.
	   */
	  ;
	}
      else
	warning ("Process in unknown state with buffered events.");
    }

  /* Sometimes we have to wait for a particular thread
   * (if we're stepping over a bpt).  In that case, we
   * _know_ it's going to complete the single-step we
   * asked for (because we're only doing the step under
   * certain very well-understood circumstances), so it
   * can't block.
   */
  if (doing_fake_step)
    {
      wait_tid = fake_step_tid;
      wait_pid = get_pid_for (fake_step_tid);

#ifdef WAIT_BUFFER_DEBUG
      if (debug_on)
	printf ("Doing a wait after a fake-step for %d, pid %d\n",
		wait_tid, wait_pid);
#endif
    }

  if (more_events_left == 0	/* No buffered events, need real ones. */
      || process_state != STOPPED)
    {
      /* If there are no buffered events, and so we need
       * real ones, or if we are FORKING, VFORKING, 
       * FAKE_STEPPING or RUNNING, and thus have to do
       * a real wait, then do a real wait.
       */

#ifdef WAIT_BUFFER_DEBUG
      /* Normal case... */
      if (debug_on)
	printf ("TW: do it for real; pid %d, tid %d\n", wait_pid, wait_tid);
#endif

      /* The actual wait call.
       */
      ttw_status = call_real_ttrace_wait (wait_pid, wait_tid, option, tsp, tsp_size);

      /* Note that the routines we'll call will be using "call_real_ttrace",
       * not "call_ttrace", and thus need the real pid rather than the pseudo-tid
       * the rest of the world uses (which is actually the tid).
       */
      real_pid = tsp->tts_pid;

      /* For most events: Stop the world!

       * It's sometimes not safe to stop all threads of a process.
       * Sometimes it's not even safe to ask for the thread state
       * of a process!
       */
      if (can_touch_threads_of_process (real_pid, tsp->tts_event))
	{
	  /* If we're really only stepping a single thread, then don't
	   * try to stop all the others -- we only do this single-stepping
	   * business when all others were already stopped...and the stop
	   * would mess up other threads' events.
	   *
	   * Similiarly, if there are other threads with events,
	   * don't do the stop.
	   */
	  if (!doing_fake_step)
	    {
	      if (more_events_left > 0)
		warning ("Internal error in stopping process");

	      stop_all_threads_of_process (real_pid);

	      /* At this point, we could scan and update_thread_list(),
	       * and only use the local list for the rest of the
	       * module! We'd get rid of the scans in the various
	       * continue routines (adding one in attach).  It'd
	       * be great--UPGRADE ME!
	       */
	    }
	}

#ifdef PARANOIA
      else if (debug_on)
	{
	  if (more_events_left > 0)
	    printf ("== Can't stop process; more events!\n");
	  else
	    printf ("== Can't stop process!\n");
	}
#endif

      process_state = STOPPED;

#ifdef WAIT_BUFFER_DEBUG
      if (debug_on)
	printf ("Process set to STOPPED\n");
#endif
    }

  else
    {
      /* Fake a call to ttrace_wait.  The process must be
       * STOPPED, as we aren't going to do any wait.
       */
#ifdef WAIT_BUFFER_DEBUG
      if (debug_on)
	printf ("TW: fake it\n");
#endif

      if (process_state != STOPPED)
	{
	  warning ("Process not stopped at wait call, in state '%s'.\n",
		   get_printable_name_of_process_state (process_state));
	}

      if (doing_fake_step)
	error ("Internal error in stepping over breakpoint");

      ttw_status = 0;		/* Faking it is always successful! */
    }				/* End of fake or not? if */

  /* Pick an event to pass to our caller.  Be paranoid.
   */
  if (!select_stopped_thread_of_process (real_pid, tsp))
    warning ("Can't find event, using previous event.");

  else if (tsp->tts_event == TTEVT_NONE)
    warning ("Internal error: no thread has a real event.");

  else if (doing_fake_step)
    {
      if (fake_step_tid != tsp->tts_lwpid)
	warning ("Internal error in stepping over breakpoint.");

      /* This wait clears the (current) fake-step if there was one.
       */
      doing_fake_step = 0;
      fake_step_tid = 0;
    }

  /* We now have a correct tsp and ttw_status for the thread
   * which we want to report.  So it's "handled"!  This call
   * will add it to our list if it's not there already.
   */
  set_handled (real_pid, tsp->tts_lwpid);

  /* Save a copy of the ttrace state of this thread, in our local
     thread descriptor.

     This caches the state.  The implementation of queries like
     hpux_has_execd can then use this cached state, rather than
     be forced to make an explicit ttrace call to get it.

     (Guard against the condition that this is the first time we've
     waited on, i.e., seen this thread, and so haven't yet entered
     it into our list of threads.)
   */
  tinfo = find_thread_info (tsp->tts_lwpid);
  if (tinfo != NULL)
    {
      copy_ttstate_t (&tinfo->last_stop_state, tsp);
      tinfo->have_state = 1;
    }

  return ttw_status;
}				/* call_ttrace_wait */

#if defined(CHILD_REPORTED_EXEC_EVENTS_PER_EXEC_CALL)
int
child_reported_exec_events_per_exec_call (void)
{
  return 1;			/* ttrace reports the event once per call. */
}
#endif



/* Our implementation of hardware watchpoints involves making memory
   pages write-protected.  We must remember a page's original permissions,
   and we must also know when it is appropriate to restore a page's
   permissions to its original state.

   We use a "dictionary" of hardware-watched pages to do this.  Each
   hardware-watched page is recorded in the dictionary.  Each page's
   dictionary entry contains the original permissions and a reference
   count.  Pages are hashed into the dictionary by their start address.

   When hardware watchpoint is set on page X for the first time, page X
   is added to the dictionary with a reference count of 1.  If other
   hardware watchpoints are subsequently set on page X, its reference
   count is incremented.  When hardware watchpoints are removed from
   page X, its reference count is decremented.  If a page's reference
   count drops to 0, it's permissions are restored and the page's entry
   is thrown out of the dictionary.
 */
typedef struct memory_page
{
  CORE_ADDR page_start;
  int reference_count;
  int original_permissions;
  struct memory_page *next;
  struct memory_page *previous;
}
memory_page_t;

#define MEMORY_PAGE_DICTIONARY_BUCKET_COUNT  128

static struct
  {
    LONGEST page_count;
    int page_size;
    int page_protections_allowed;
    /* These are just the heads of chains of actual page descriptors. */
    memory_page_t buckets[MEMORY_PAGE_DICTIONARY_BUCKET_COUNT];
  }
memory_page_dictionary;


static void
require_memory_page_dictionary (void)
{
  int i;

  /* Is the memory page dictionary ready for use?  If so, we're done. */
  if (memory_page_dictionary.page_count >= (LONGEST) 0)
    return;

  /* Else, initialize it. */
  memory_page_dictionary.page_count = (LONGEST) 0;

  for (i = 0; i < MEMORY_PAGE_DICTIONARY_BUCKET_COUNT; i++)
    {
      memory_page_dictionary.buckets[i].page_start = (CORE_ADDR) 0;
      memory_page_dictionary.buckets[i].reference_count = 0;
      memory_page_dictionary.buckets[i].next = NULL;
      memory_page_dictionary.buckets[i].previous = NULL;
    }
}


static void
retire_memory_page_dictionary (void)
{
  memory_page_dictionary.page_count = (LONGEST) - 1;
}


/* Write-protect the memory page that starts at this address.

   Returns the original permissions of the page.
 */
static int
write_protect_page (int pid, CORE_ADDR page_start)
{
  int tt_status;
  int original_permissions;
  int new_permissions;

  tt_status = call_ttrace (TT_PROC_GET_MPROTECT,
			   pid,
			   (TTRACE_ARG_TYPE) page_start,
			   TT_NIL,
			   (TTRACE_ARG_TYPE) & original_permissions);
  if (errno || (tt_status < 0))
    {
      return 0;			/* What else can we do? */
    }

  /* We'll also write-protect the page now, if that's allowed. */
  if (memory_page_dictionary.page_protections_allowed)
    {
      new_permissions = original_permissions & ~PROT_WRITE;
      tt_status = call_ttrace (TT_PROC_SET_MPROTECT,
			       pid,
			       (TTRACE_ARG_TYPE) page_start,
			 (TTRACE_ARG_TYPE) memory_page_dictionary.page_size,
			       (TTRACE_ARG_TYPE) new_permissions);
      if (errno || (tt_status < 0))
	{
	  return 0;		/* What else can we do? */
	}
    }

  return original_permissions;
}


/* Unwrite-protect the memory page that starts at this address, restoring
   (what we must assume are) its original permissions.
 */
static void
unwrite_protect_page (int pid, CORE_ADDR page_start, int original_permissions)
{
  int tt_status;

  tt_status = call_ttrace (TT_PROC_SET_MPROTECT,
			   pid,
			   (TTRACE_ARG_TYPE) page_start,
			 (TTRACE_ARG_TYPE) memory_page_dictionary.page_size,
			   (TTRACE_ARG_TYPE) original_permissions);
  if (errno || (tt_status < 0))
    {
      return;			/* What else can we do? */
    }
}


/* Memory page-protections are used to implement "hardware" watchpoints
   on HP-UX.

   For every memory page that is currently being watched (i.e., that
   presently should be write-protected), write-protect it.
 */
void
hppa_enable_page_protection_events (int pid)
{
  int bucket;

  memory_page_dictionary.page_protections_allowed = 1;

  for (bucket = 0; bucket < MEMORY_PAGE_DICTIONARY_BUCKET_COUNT; bucket++)
    {
      memory_page_t *page;

      page = memory_page_dictionary.buckets[bucket].next;
      while (page != NULL)
	{
	  page->original_permissions = write_protect_page (pid, page->page_start);
	  page = page->next;
	}
    }
}


/* Memory page-protections are used to implement "hardware" watchpoints
   on HP-UX.

   For every memory page that is currently being watched (i.e., that
   presently is or should be write-protected), un-write-protect it.
 */
void
hppa_disable_page_protection_events (int pid)
{
  int bucket;

  for (bucket = 0; bucket < MEMORY_PAGE_DICTIONARY_BUCKET_COUNT; bucket++)
    {
      memory_page_t *page;

      page = memory_page_dictionary.buckets[bucket].next;
      while (page != NULL)
	{
	  unwrite_protect_page (pid, page->page_start, page->original_permissions);
	  page = page->next;
	}
    }

  memory_page_dictionary.page_protections_allowed = 0;
}

/* Count the number of outstanding events.  At this
 * point, we have selected one thread and its event
 * as the one to be "reported" upwards to core gdb.
 * That thread is already marked as "handled".
 *
 * Note: we could just scan our own thread list.  FIXME!
 */
static int
count_unhandled_events (int real_pid, lwpid_t real_tid)
{
  ttstate_t tstate;
  lwpid_t ttid;
  int events_left;

  /* Ok, find out how many threads have real events to report.
   */
  events_left = 0;
  ttid = get_process_first_stopped_thread_id (real_pid, &tstate);

#ifdef THREAD_DEBUG
  if (debug_on)
    {
      if (ttid == 0)
	printf ("Process %d has no threads\n", real_pid);
      else
	printf ("Process %d has these threads:\n", real_pid);
    }
#endif

  while (ttid > 0)
    {
      if (tstate.tts_event != TTEVT_NONE
	  && !was_handled (ttid))
	{
	  /* TTEVT_NONE implies we just stopped it ourselves
	   * because we're the stop-the-world guys, so it's
	   * not an event from our point of view.
	   *
	   * If "was_handled" is true, this is an event we
	   * already handled, so don't count it.
	   *
	   * Note that we don't count the thread with the
	   * currently-reported event, as it's already marked
	   * as handled.
	   */
	  events_left++;
	}

#if defined( THREAD_DEBUG ) || defined( WAIT_BUFFER_DEBUG )
      if (debug_on)
	{
	  if (ttid == real_tid)
	    printf ("*");	/* Thread we're reporting */
	  else
	    printf (" ");

	  if (tstate.tts_event != TTEVT_NONE)
	    printf ("+");	/* Thread with a real event */
	  else
	    printf (" ");

	  if (was_handled (ttid))
	    printf ("h");	/* Thread has been handled */
	  else
	    printf (" ");

	  printf (" %d, with event %s", ttid,
		  get_printable_name_of_ttrace_event (tstate.tts_event));

	  if (tstate.tts_event == TTEVT_SIGNAL
	      && 5 == tstate.tts_u.tts_signal.tts_signo)
	    {
	      CORE_ADDR pc_val;

	      pc_val = get_raw_pc (ttid);

	      if (pc_val > 0)
		printf (" breakpoint at 0x%x\n", pc_val);
	      else
		printf (" bpt, can't fetch pc.\n");
	    }
	  else
	    printf ("\n");
	}
#endif

      ttid = get_process_next_stopped_thread_id (real_pid, &tstate);
    }

#if defined( THREAD_DEBUG ) || defined( WAIT_BUFFER_DEBUG )
  if (debug_on)
    if (events_left > 0)
      printf ("There are thus %d pending events\n", events_left);
#endif

  return events_left;
}

/* This function is provided as a sop to clients that are calling
 * ptrace_wait to wait for a process to stop.  (see the
 * implementation of child_wait.)  Return value is the pid for
 * the event that ended the wait.
 *
 * Note: used by core gdb and so uses the pseudo-pid (really tid).
 */
int
ptrace_wait (ptid_t ptid, int *status)
{
  ttstate_t tsp;
  int ttwait_return;
  int real_pid;
  ttstate_t state;
  lwpid_t real_tid;
  int return_pid;

  /* The ptrace implementation of this also ignores pid.
   */
  *status = 0;

  ttwait_return = call_ttrace_wait (0, TTRACE_WAITOK, &tsp, sizeof (tsp));
  if (ttwait_return < 0)
    {
      /* ??rehrauer: It appears that if our inferior exits and we
         haven't asked for exit events, that we're not getting any
         indication save a negative return from ttrace_wait and an
         errno set to ESRCH?
       */
      if (errno == ESRCH)
	{
	  *status = 0;		/* WIFEXITED */
	  return PIDGET (inferior_ptid);
	}

      warning ("Call of ttrace_wait returned with errno %d.",
	       errno);
      *status = ttwait_return;
      return PIDGET (inferior_ptid);
    }

  real_pid = tsp.tts_pid;
  real_tid = tsp.tts_lwpid;

  /* One complication is that the "tts_event" structure has
   * a set of flags, and more than one can be set.  So we
   * either have to force an order (as we do here), or handle
   * more than one flag at a time.
   */
  if (tsp.tts_event & TTEVT_LWP_CREATE)
    {

      /* Unlike what you might expect, this event is reported in
       * the _creating_ thread, and the _created_ thread (whose tid
       * we have) is still running.  So we have to stop it.  This
       * has already been done in "call_ttrace_wait", but should we
       * ever abandon the "stop-the-world" model, here's the command
       * to use:
       *
       *    call_ttrace( TT_LWP_STOP, real_tid, TT_NIL, TT_NIL, TT_NIL );
       *
       * Note that this would depend on being called _after_ "add_tthread"
       * below for the tid-to-pid translation to be done in "call_ttrace".
       */

#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("New thread: pid %d, tid %d, creator tid %d\n",
		real_pid, tsp.tts_u.tts_thread.tts_target_lwpid,
		real_tid);
#endif

      /* Now we have to return the tid of the created thread, not
       * the creating thread, or "wait_for_inferior" won't know we
       * have a new "process" (thread).  Plus we should record it
       * right, too.
       */
      real_tid = tsp.tts_u.tts_thread.tts_target_lwpid;

      add_tthread (real_pid, real_tid);
    }

  else if ((tsp.tts_event & TTEVT_LWP_TERMINATE)
	   || (tsp.tts_event & TTEVT_LWP_EXIT))
    {

#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("Thread dies: %d\n", real_tid);
#endif

      del_tthread (real_tid);
    }

  else if (tsp.tts_event & TTEVT_EXEC)
    {

#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("Pid %d has zero'th thread %d; inferior pid is %d\n",
		real_pid, real_tid, PIDGET (inferior_ptid));
#endif

      add_tthread (real_pid, real_tid);
    }

#ifdef THREAD_DEBUG
  else if (debug_on)
    {
      printf ("Process-level event %s, using tid %d\n",
	      get_printable_name_of_ttrace_event (tsp.tts_event),
	      real_tid);

      /* OK to do this, as "add_tthread" won't add
       * duplicate entries.  Also OK not to do it,
       * as this event isn't one which can change the
       * thread state.
       */
      add_tthread (real_pid, real_tid);
    }
#endif


  /* How many events are left to report later?
   * In a non-stop-the-world model, this isn't needed.
   *
   * Note that it's not always safe to query the thread state of a process,
   * which is what count_unhandled_events does.  (If unsafe, we're left with
   * no other resort than to assume that no more events remain...)
   */
  if (can_touch_threads_of_process (real_pid, tsp.tts_event))
    more_events_left = count_unhandled_events (real_pid, real_tid);

  else
    {
      if (more_events_left > 0)
	warning ("Vfork or fork causing loss of %d buffered events.",
		 more_events_left);

      more_events_left = 0;
    }

  /* Attempt to translate the ttrace_wait-returned status into the
     ptrace equivalent.

     ??rehrauer: This is somewhat fragile.  We really ought to rewrite
     clients that expect to pick apart a ptrace wait status, to use
     something a little more abstract.
   */
  if ((tsp.tts_event & TTEVT_EXEC)
      || (tsp.tts_event & TTEVT_FORK)
      || (tsp.tts_event & TTEVT_VFORK))
    {
      /* Forks come in pairs (parent and child), so core gdb
       * will do two waits.  Be ready to notice this.
       */
      if (tsp.tts_event & TTEVT_FORK)
	{
	  process_state = FORKING;

#ifdef WAIT_BUFFER_DEBUG
	  if (debug_on)
	    printf ("Process set to FORKING\n");
#endif
	}
      else if (tsp.tts_event & TTEVT_VFORK)
	{
	  process_state = VFORKING;

#ifdef WAIT_BUFFER_DEBUG
	  if (debug_on)
	    printf ("Process set to VFORKING\n");
#endif
	}

      /* Make an exec or fork look like a breakpoint.  Definitely a hack,
         but I don't think non HP-UX-specific clients really carefully
         inspect the first events they get after inferior startup, so
         it probably almost doesn't matter what we claim this is.
       */

#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("..a process 'event'\n");
#endif

      /* Also make fork and exec events look like bpts, so they can be caught.
       */
      *status = 0177 | (_SIGTRAP << 8);
    }

  /* Special-cases: We ask for syscall entry and exit events to implement
     "fast" (aka "hardware") watchpoints.

     When we get a syscall entry, we want to disable page-protections,
     and resume the inferior; this isn't an event we wish for
     wait_for_inferior to see.  Note that we must resume ONLY the
     thread that reported the syscall entry; we don't want to allow
     other threads to run with the page protections off, as they might
     then be able to write to watch memory without it being caught.

     When we get a syscall exit, we want to reenable page-protections,
     but we don't want to resume the inferior; this is an event we wish
     wait_for_inferior to see.  Make it look like the signal we normally
     get for a single-step completion.  This should cause wait_for_inferior
     to evaluate whether any watchpoint triggered.

     Or rather, that's what we'd LIKE to do for syscall exit; we can't,
     due to some HP-UX "features".  Some syscalls have problems with
     write-protections on some pages, and some syscalls seem to have
     pending writes to those pages at the time we're getting the return
     event.  So, we'll single-step the inferior to get out of the syscall,
     and then reenable protections.

     Note that we're intentionally allowing the syscall exit case to
     fall through into the succeeding cases, as sometimes we single-
     step out of one syscall only to immediately enter another...
   */
  else if ((tsp.tts_event & TTEVT_SYSCALL_ENTRY)
	   || (tsp.tts_event & TTEVT_SYSCALL_RETURN))
    {
      /* Make a syscall event look like a breakpoint.  Same comments
         as for exec & fork events.
       */
#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("..a syscall 'event'\n");
#endif

      /* Also make syscall events look like bpts, so they can be caught.
       */
      *status = 0177 | (_SIGTRAP << 8);
    }

  else if ((tsp.tts_event & TTEVT_LWP_CREATE)
	   || (tsp.tts_event & TTEVT_LWP_TERMINATE)
	   || (tsp.tts_event & TTEVT_LWP_EXIT))
    {
      /* Make a thread event look like a breakpoint.  Same comments
       * as for exec & fork events.
       */
#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("..a thread 'event'\n");
#endif

      /* Also make thread events look like bpts, so they can be caught.
       */
      *status = 0177 | (_SIGTRAP << 8);
    }

  else if ((tsp.tts_event & TTEVT_EXIT))
    {				/* WIFEXITED */

#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("..an exit\n");
#endif

      /* Prevent rest of gdb from thinking this is
       * a new thread if for some reason it's never
       * seen the main thread before.
       */
      inferior_ptid = pid_to_ptid (map_to_gdb_tid (real_tid));	/* HACK, FIX */

      *status = 0 | (tsp.tts_u.tts_exit.tts_exitcode);
    }

  else if (tsp.tts_event & TTEVT_SIGNAL)
    {				/* WIFSTOPPED */
#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("..a signal, %d\n", tsp.tts_u.tts_signal.tts_signo);
#endif

      *status = 0177 | (tsp.tts_u.tts_signal.tts_signo << 8);
    }

  else
    {				/* !WIFSTOPPED */

      /* This means the process or thread terminated.  But we should've
         caught an explicit exit/termination above.  So warn (this is
         really an internal error) and claim the process or thread
         terminated with a SIGTRAP.
       */

      warning ("process_wait: unknown process state");

#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("Process-level event %s, using tid %d\n",
		get_printable_name_of_ttrace_event (tsp.tts_event),
		real_tid);
#endif

      *status = _SIGTRAP;
    }

  target_post_wait (pid_to_ptid (tsp.tts_pid), *status);


#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("Done waiting, pid is %d, tid %d\n", real_pid, real_tid);
#endif

  /* All code external to this module uses the tid, but calls
   * it "pid".  There's some tweaking so that the outside sees
   * the first thread as having the same number as the starting
   * pid.
   */
  return_pid = map_to_gdb_tid (real_tid);

  if (real_tid == 0 || return_pid == 0)
    {
      warning ("Internal error: process-wait failed.");
    }

  return return_pid;
}


/* This function causes the caller's process to be traced by its
   parent.  This is intended to be called after GDB forks itself,
   and before the child execs the target.  Despite the name, it
   is called by the child.

   Note that HP-UX ttrace is rather funky in how this is done.
   If the parent wants to get the initial exec event of a child,
   it must set the ttrace event mask of the child to include execs.
   (The child cannot do this itself.)  This must be done after the
   child is forked, but before it execs.

   To coordinate the parent and child, we implement a semaphore using
   pipes.  After SETTRC'ing itself, the child tells the parent that
   it is now traceable by the parent, and waits for the parent's
   acknowledgement.  The parent can then set the child's event mask,
   and notify the child that it can now exec.

   (The acknowledgement by parent happens as a result of a call to
   child_acknowledge_created_inferior.)
 */
int
parent_attach_all (int p1, PTRACE_ARG3_TYPE p2, int p3)
{
  int tt_status;

  /* We need a memory home for a constant, to pass it to ttrace.
     The value of the constant is arbitrary, so long as both
     parent and child use the same value.  Might as well use the
     "magic" constant provided by ttrace...
   */
  uint64_t tc_magic_child = TT_VERSION;
  uint64_t tc_magic_parent = 0;

  tt_status = call_real_ttrace (
				 TT_PROC_SETTRC,
				 (int) TT_NIL,
				 (lwpid_t) TT_NIL,
				 TT_NIL,
				 (TTRACE_ARG_TYPE) TT_VERSION,
				 TT_NIL);

  if (tt_status < 0)
    return tt_status;

  /* Notify the parent that we're potentially ready to exec(). */
  write (startup_semaphore.child_channel[SEM_TALK],
	 &tc_magic_child,
	 sizeof (tc_magic_child));

  /* Wait for acknowledgement from the parent. */
  read (startup_semaphore.parent_channel[SEM_LISTEN],
	&tc_magic_parent,
	sizeof (tc_magic_parent));

  if (tc_magic_child != tc_magic_parent)
    warning ("mismatched semaphore magic");

  /* Discard our copy of the semaphore. */
  (void) close (startup_semaphore.parent_channel[SEM_LISTEN]);
  (void) close (startup_semaphore.parent_channel[SEM_TALK]);
  (void) close (startup_semaphore.child_channel[SEM_LISTEN]);
  (void) close (startup_semaphore.child_channel[SEM_TALK]);

  return tt_status;
}

/* Despite being file-local, this routine is dealing with
 * actual process IDs, not thread ids.  That's because it's
 * called before the first "wait" call, and there's no map
 * yet from tids to pids.
 *
 * When it is called, a forked child is running, but waiting on
 * the semaphore.  If you stop the child and re-start it,
 * things get confused, so don't do that!  An attached child is
 * stopped.
 *
 * Since this is called after either attach or run, we
 * have to be the common part of both.
 */
static void
require_notification_of_events (int real_pid)
{
  int tt_status;
  ttevent_t notifiable_events;

  lwpid_t tid;
  ttstate_t thread_state;

#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("Require notif, pid is %d\n", real_pid);
#endif

  /* Temporary HACK: tell inftarg.c/child_wait to not
   * loop until pids are the same.
   */
  not_same_real_pid = 0;

  sigemptyset (&notifiable_events.tte_signals);
  notifiable_events.tte_opts = TTEO_NONE;

  /* This ensures that forked children inherit their parent's
   * event mask, which we're setting here.
   *
   * NOTE: if you debug gdb with itself, then the ultimate
   *       debuggee gets flags set by the outermost gdb, as
   *       a child of a child will still inherit.
   */
  notifiable_events.tte_opts |= TTEO_PROC_INHERIT;

  notifiable_events.tte_events = TTEVT_DEFAULT;
  notifiable_events.tte_events |= TTEVT_SIGNAL;
  notifiable_events.tte_events |= TTEVT_EXEC;
  notifiable_events.tte_events |= TTEVT_EXIT;
  notifiable_events.tte_events |= TTEVT_FORK;
  notifiable_events.tte_events |= TTEVT_VFORK;
  notifiable_events.tte_events |= TTEVT_LWP_CREATE;
  notifiable_events.tte_events |= TTEVT_LWP_EXIT;
  notifiable_events.tte_events |= TTEVT_LWP_TERMINATE;

  tt_status = call_real_ttrace (
				 TT_PROC_SET_EVENT_MASK,
				 real_pid,
				 (lwpid_t) TT_NIL,
				 (TTRACE_ARG_TYPE) & notifiable_events,
			       (TTRACE_ARG_TYPE) sizeof (notifiable_events),
				 TT_NIL);
}

static void
require_notification_of_exec_events (int real_pid)
{
  int tt_status;
  ttevent_t notifiable_events;

  lwpid_t tid;
  ttstate_t thread_state;

#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("Require notif, pid is %d\n", real_pid);
#endif

  /* Temporary HACK: tell inftarg.c/child_wait to not
   * loop until pids are the same.
   */
  not_same_real_pid = 0;

  sigemptyset (&notifiable_events.tte_signals);
  notifiable_events.tte_opts = TTEO_NOSTRCCHLD;

  /* This ensures that forked children don't inherit their parent's
   * event mask, which we're setting here.
   */
  notifiable_events.tte_opts &= ~TTEO_PROC_INHERIT;

  notifiable_events.tte_events = TTEVT_DEFAULT;
  notifiable_events.tte_events |= TTEVT_EXEC;
  notifiable_events.tte_events |= TTEVT_EXIT;

  tt_status = call_real_ttrace (
				 TT_PROC_SET_EVENT_MASK,
				 real_pid,
				 (lwpid_t) TT_NIL,
				 (TTRACE_ARG_TYPE) & notifiable_events,
			       (TTRACE_ARG_TYPE) sizeof (notifiable_events),
				 TT_NIL);
}


/* This function is called by the parent process, with pid being the
 * ID of the child process, after the debugger has forked.
 */
void
child_acknowledge_created_inferior (int pid)
{
  /* We need a memory home for a constant, to pass it to ttrace.
     The value of the constant is arbitrary, so long as both
     parent and child use the same value.  Might as well use the
     "magic" constant provided by ttrace...
   */
  uint64_t tc_magic_parent = TT_VERSION;
  uint64_t tc_magic_child = 0;

  /* Wait for the child to tell us that it has forked. */
  read (startup_semaphore.child_channel[SEM_LISTEN],
	&tc_magic_child,
	sizeof (tc_magic_child));

  /* Clear thread info now.  We'd like to do this in
   * "require...", but that messes up attach.
   */
  clear_thread_info ();

  /* Tell the "rest of gdb" that the initial thread exists.
   * This isn't really a hack.  Other thread-based versions
   * of gdb (e.g. gnu-nat.c) seem to do the same thing.
   *
   * Q: Why don't we also add this thread to the local
   *    list via "add_tthread"?
   *
   * A: Because we don't know the tid, and can't stop the
   *    the process safely to ask what it is.  Anyway, we'll
   *    add it when it gets the EXEC event.
   */
  add_thread (pid_to_ptid (pid));		/* in thread.c */

  /* We can now set the child's ttrace event mask.
   */
  require_notification_of_exec_events (pid);

  /* Tell ourselves that the process is running.
   */
  process_state = RUNNING;

  /* Notify the child that it can exec. */
  write (startup_semaphore.parent_channel[SEM_TALK],
	 &tc_magic_parent,
	 sizeof (tc_magic_parent));

  /* Discard our copy of the semaphore. */
  (void) close (startup_semaphore.parent_channel[SEM_LISTEN]);
  (void) close (startup_semaphore.parent_channel[SEM_TALK]);
  (void) close (startup_semaphore.child_channel[SEM_LISTEN]);
  (void) close (startup_semaphore.child_channel[SEM_TALK]);
}


/*
 * arrange for notification of all events by
 * calling require_notification_of_events.
 */
void
child_post_startup_inferior (ptid_t ptid)
{
  require_notification_of_events (PIDGET (ptid));
}

/* From here on, we should expect tids rather than pids.
 */
static void
hppa_enable_catch_fork (int tid)
{
  int tt_status;
  ttevent_t ttrace_events;

  /* Get the set of events that are currently enabled.
   */
  tt_status = call_ttrace (TT_PROC_GET_EVENT_MASK,
			   tid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);
  if (errno)
    perror_with_name ("ttrace");

  /* Add forks to that set. */
  ttrace_events.tte_events |= TTEVT_FORK;

#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("enable fork, tid is %d\n", tid);
#endif

  tt_status = call_ttrace (TT_PROC_SET_EVENT_MASK,
			   tid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);
  if (errno)
    perror_with_name ("ttrace");
}


static void
hppa_disable_catch_fork (int tid)
{
  int tt_status;
  ttevent_t ttrace_events;

  /* Get the set of events that are currently enabled.
   */
  tt_status = call_ttrace (TT_PROC_GET_EVENT_MASK,
			   tid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);

  if (errno)
    perror_with_name ("ttrace");

  /* Remove forks from that set. */
  ttrace_events.tte_events &= ~TTEVT_FORK;

#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("disable fork, tid is %d\n", tid);
#endif

  tt_status = call_ttrace (TT_PROC_SET_EVENT_MASK,
			   tid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);

  if (errno)
    perror_with_name ("ttrace");
}


#if defined(CHILD_INSERT_FORK_CATCHPOINT)
int
child_insert_fork_catchpoint (int tid)
{
  /* Enable reporting of fork events from the kernel. */
  /* ??rehrauer: For the moment, we're always enabling these events,
     and just ignoring them if there's no catchpoint to catch them.
   */
  return 0;
}
#endif


#if defined(CHILD_REMOVE_FORK_CATCHPOINT)
int
child_remove_fork_catchpoint (int tid)
{
  /* Disable reporting of fork events from the kernel. */
  /* ??rehrauer: For the moment, we're always enabling these events,
     and just ignoring them if there's no catchpoint to catch them.
   */
  return 0;
}
#endif


static void
hppa_enable_catch_vfork (int tid)
{
  int tt_status;
  ttevent_t ttrace_events;

  /* Get the set of events that are currently enabled.
   */
  tt_status = call_ttrace (TT_PROC_GET_EVENT_MASK,
			   tid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);

  if (errno)
    perror_with_name ("ttrace");

  /* Add vforks to that set. */
  ttrace_events.tte_events |= TTEVT_VFORK;

#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("enable vfork, tid is %d\n", tid);
#endif

  tt_status = call_ttrace (TT_PROC_SET_EVENT_MASK,
			   tid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);

  if (errno)
    perror_with_name ("ttrace");
}


static void
hppa_disable_catch_vfork (int tid)
{
  int tt_status;
  ttevent_t ttrace_events;

  /* Get the set of events that are currently enabled. */
  tt_status = call_ttrace (TT_PROC_GET_EVENT_MASK,
			   tid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);

  if (errno)
    perror_with_name ("ttrace");

  /* Remove vforks from that set. */
  ttrace_events.tte_events &= ~TTEVT_VFORK;

#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("disable vfork, tid is %d\n", tid);
#endif
  tt_status = call_ttrace (TT_PROC_SET_EVENT_MASK,
			   tid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);

  if (errno)
    perror_with_name ("ttrace");
}


#if defined(CHILD_INSERT_VFORK_CATCHPOINT)
int
child_insert_vfork_catchpoint (int tid)
{
  /* Enable reporting of vfork events from the kernel. */
  /* ??rehrauer: For the moment, we're always enabling these events,
     and just ignoring them if there's no catchpoint to catch them.
   */
  return 0;
}
#endif


#if defined(CHILD_REMOVE_VFORK_CATCHPOINT)
int
child_remove_vfork_catchpoint (int tid)
{
  /* Disable reporting of vfork events from the kernel. */
  /* ??rehrauer: For the moment, we're always enabling these events,
     and just ignoring them if there's no catchpoint to catch them.
   */
  return 0;
}
#endif

/* Q: Do we need to map the returned process ID to a thread ID?

 * A: I don't think so--here we want a _real_ pid.  Any later
 *    operations will call "require_notification_of_events" and
 *    start the mapping.
 */
int
hpux_has_forked (int tid, int *childpid)
{
  int tt_status;
  ttstate_t ttrace_state;
  thread_info *tinfo;

  /* Do we have cached thread state that we can consult?  If so, use it. */
  tinfo = find_thread_info (map_from_gdb_tid (tid));
  if (tinfo != NULL)
    {
      copy_ttstate_t (&ttrace_state, &tinfo->last_stop_state);
    }

  /* Nope, must read the thread's current state */
  else
    {
      tt_status = call_ttrace (TT_LWP_GET_STATE,
			       tid,
			       (TTRACE_ARG_TYPE) & ttrace_state,
			       (TTRACE_ARG_TYPE) sizeof (ttrace_state),
			       TT_NIL);

      if (errno)
	perror_with_name ("ttrace");

      if (tt_status < 0)
	return 0;
    }

  if (ttrace_state.tts_event & TTEVT_FORK)
    {
      *childpid = ttrace_state.tts_u.tts_fork.tts_fpid;
      return 1;
    }

  return 0;
}

/* See hpux_has_forked for pid discussion.
 */
int
hpux_has_vforked (int tid, int *childpid)
{
  int tt_status;
  ttstate_t ttrace_state;
  thread_info *tinfo;

  /* Do we have cached thread state that we can consult?  If so, use it. */
  tinfo = find_thread_info (map_from_gdb_tid (tid));
  if (tinfo != NULL)
    copy_ttstate_t (&ttrace_state, &tinfo->last_stop_state);

  /* Nope, must read the thread's current state */
  else
    {
      tt_status = call_ttrace (TT_LWP_GET_STATE,
			       tid,
			       (TTRACE_ARG_TYPE) & ttrace_state,
			       (TTRACE_ARG_TYPE) sizeof (ttrace_state),
			       TT_NIL);

      if (errno)
	perror_with_name ("ttrace");

      if (tt_status < 0)
	return 0;
    }

  if (ttrace_state.tts_event & TTEVT_VFORK)
    {
      *childpid = ttrace_state.tts_u.tts_fork.tts_fpid;
      return 1;
    }

  return 0;
}


#if defined(CHILD_INSERT_EXEC_CATCHPOINT)
int
child_insert_exec_catchpoint (int tid)
{
  /* Enable reporting of exec events from the kernel. */
  /* ??rehrauer: For the moment, we're always enabling these events,
     and just ignoring them if there's no catchpoint to catch them.
   */
  return 0;
}
#endif


#if defined(CHILD_REMOVE_EXEC_CATCHPOINT)
int
child_remove_exec_catchpoint (int tid)
{
  /* Disable reporting of execevents from the kernel. */
  /* ??rehrauer: For the moment, we're always enabling these events,
     and just ignoring them if there's no catchpoint to catch them.
   */
  return 0;
}
#endif


int
hpux_has_execd (int tid, char **execd_pathname)
{
  int tt_status;
  ttstate_t ttrace_state;
  thread_info *tinfo;

  /* Do we have cached thread state that we can consult?  If so, use it. */
  tinfo = find_thread_info (map_from_gdb_tid (tid));
  if (tinfo != NULL)
    copy_ttstate_t (&ttrace_state, &tinfo->last_stop_state);

  /* Nope, must read the thread's current state */
  else
    {
      tt_status = call_ttrace (TT_LWP_GET_STATE,
			       tid,
			       (TTRACE_ARG_TYPE) & ttrace_state,
			       (TTRACE_ARG_TYPE) sizeof (ttrace_state),
			       TT_NIL);

      if (errno)
	perror_with_name ("ttrace");

      if (tt_status < 0)
	return 0;
    }

  if (ttrace_state.tts_event & TTEVT_EXEC)
    {
      /* See child_pid_to_exec_file in this file: this is a macro.
       */
      char *exec_file = target_pid_to_exec_file (tid);

      *execd_pathname = savestring (exec_file, strlen (exec_file));
      return 1;
    }

  return 0;
}


int
hpux_has_syscall_event (int pid, enum target_waitkind *kind, int *syscall_id)
{
  int tt_status;
  ttstate_t ttrace_state;
  thread_info *tinfo;

  /* Do we have cached thread state that we can consult?  If so, use it. */
  tinfo = find_thread_info (map_from_gdb_tid (pid));
  if (tinfo != NULL)
    copy_ttstate_t (&ttrace_state, &tinfo->last_stop_state);

  /* Nope, must read the thread's current state */
  else
    {
      tt_status = call_ttrace (TT_LWP_GET_STATE,
			       pid,
			       (TTRACE_ARG_TYPE) & ttrace_state,
			       (TTRACE_ARG_TYPE) sizeof (ttrace_state),
			       TT_NIL);

      if (errno)
	perror_with_name ("ttrace");

      if (tt_status < 0)
	return 0;
    }

  *kind = TARGET_WAITKIND_SPURIOUS;	/* Until proven otherwise... */
  *syscall_id = -1;

  if (ttrace_state.tts_event & TTEVT_SYSCALL_ENTRY)
    *kind = TARGET_WAITKIND_SYSCALL_ENTRY;
  else if (ttrace_state.tts_event & TTEVT_SYSCALL_RETURN)
    *kind = TARGET_WAITKIND_SYSCALL_RETURN;
  else
    return 0;

  *syscall_id = ttrace_state.tts_scno;
  return 1;
}



#if defined(CHILD_THREAD_ALIVE)

/* Check to see if the given thread is alive.

 * We'll trust the thread list, as the more correct
 * approach of stopping the process and spinning down
 * the OS's thread list is _very_ expensive.
 *
 * May need a FIXME for that reason.
 */
int
child_thread_alive (ptid_t ptid)
{
  lwpid_t gdb_tid = PIDGET (ptid);
  lwpid_t tid;

  /* This spins down the lists twice.
   * Possible peformance improvement here!
   */
  tid = map_from_gdb_tid (gdb_tid);
  return !is_terminated (tid);
}

#endif



/* This function attempts to read the specified number of bytes from the
   save_state_t that is our view into the hardware registers, starting at
   ss_offset, and ending at ss_offset + sizeof_buf - 1

   If this function succeeds, it deposits the fetched bytes into buf,
   and returns 0.

   If it fails, it returns a negative result.  The contents of buf are
   undefined it this function fails.
 */
int
read_from_register_save_state (int tid, TTRACE_ARG_TYPE ss_offset, char *buf,
			       int sizeof_buf)
{
  int tt_status;
  register_value_t register_value = 0;

  tt_status = call_ttrace (TT_LWP_RUREGS,
			   tid,
			   ss_offset,
			   (TTRACE_ARG_TYPE) sizeof_buf,
			   (TTRACE_ARG_TYPE) buf);

  if (tt_status == 1)
    /* Map ttrace's version of success to our version.
     * Sometime ttrace returns 0, but that's ok here.
     */
    return 0;

  return tt_status;
}


/* This function attempts to write the specified number of bytes to the
   save_state_t that is our view into the hardware registers, starting at
   ss_offset, and ending at ss_offset + sizeof_buf - 1

   If this function succeeds, it deposits the bytes in buf, and returns 0.

   If it fails, it returns a negative result.  The contents of the save_state_t
   are undefined it this function fails.
 */
int
write_to_register_save_state (int tid, TTRACE_ARG_TYPE ss_offset, char *buf,
			      int sizeof_buf)
{
  int tt_status;
  register_value_t register_value = 0;

  tt_status = call_ttrace (TT_LWP_WUREGS,
			   tid,
			   ss_offset,
			   (TTRACE_ARG_TYPE) sizeof_buf,
			   (TTRACE_ARG_TYPE) buf);
  return tt_status;
}


/* This function is a sop to the largeish number of direct calls
   to call_ptrace that exist in other files.  Rather than create
   functions whose name abstracts away from ptrace, and change all
   the present callers of call_ptrace, we'll do the expedient (and
   perhaps only practical) thing.

   Note HP-UX explicitly disallows a mix of ptrace & ttrace on a traced
   process.  Thus, we must translate all ptrace requests into their
   process-specific, ttrace equivalents.
 */
int
call_ptrace (int pt_request, int gdb_tid, PTRACE_ARG3_TYPE addr, int data)
{
  ttreq_t tt_request;
  TTRACE_ARG_TYPE tt_addr = (TTRACE_ARG_TYPE) addr;
  TTRACE_ARG_TYPE tt_data = (TTRACE_ARG_TYPE) data;
  TTRACE_ARG_TYPE tt_addr2 = TT_NIL;
  int tt_status;
  register_value_t register_value;
  int read_buf;

  /* Perform the necessary argument translation.  Note that some
     cases are funky enough in the ttrace realm that we handle them
     very specially.
   */
  switch (pt_request)
    {
      /* The following cases cannot conveniently be handled conveniently
         by merely adjusting the ptrace arguments and feeding into the
         generic call to ttrace at the bottom of this function.

         Note that because all branches of this switch end in "return",
         there's no need for any "break" statements.
       */
    case PT_SETTRC:
      return parent_attach_all (0, 0, 0);

    case PT_RUREGS:
      tt_status = read_from_register_save_state (gdb_tid,
						 tt_addr,
						 &register_value,
						 sizeof (register_value));
      if (tt_status < 0)
	return tt_status;
      return register_value;

    case PT_WUREGS:
      register_value = (int) tt_data;
      tt_status = write_to_register_save_state (gdb_tid,
						tt_addr,
						&register_value,
						sizeof (register_value));
      return tt_status;
      break;

    case PT_READ_I:
      tt_status = call_ttrace (TT_PROC_RDTEXT,	/* Implicit 4-byte xfer becomes block-xfer. */
			       gdb_tid,
			       tt_addr,
			       (TTRACE_ARG_TYPE) 4,
			       (TTRACE_ARG_TYPE) & read_buf);
      if (tt_status < 0)
	return tt_status;
      return read_buf;

    case PT_READ_D:
      tt_status = call_ttrace (TT_PROC_RDDATA,	/* Implicit 4-byte xfer becomes block-xfer. */
			       gdb_tid,
			       tt_addr,
			       (TTRACE_ARG_TYPE) 4,
			       (TTRACE_ARG_TYPE) & read_buf);
      if (tt_status < 0)
	return tt_status;
      return read_buf;

    case PT_ATTACH:
      tt_status = call_real_ttrace (TT_PROC_ATTACH,
				    map_from_gdb_tid (gdb_tid),
				    (lwpid_t) TT_NIL,
				    tt_addr,
				    (TTRACE_ARG_TYPE) TT_VERSION,
				    tt_addr2);
      if (tt_status < 0)
	return tt_status;
      return tt_status;

      /* The following cases are handled by merely adjusting the ptrace
         arguments and feeding into the generic call to ttrace.
       */
    case PT_DETACH:
      tt_request = TT_PROC_DETACH;
      break;

    case PT_WRITE_I:
      tt_request = TT_PROC_WRTEXT;	/* Translates 4-byte xfer to block-xfer. */
      tt_data = 4;		/* This many bytes. */
      tt_addr2 = (TTRACE_ARG_TYPE) & data;	/* Address of xfer source. */
      break;

    case PT_WRITE_D:
      tt_request = TT_PROC_WRDATA;	/* Translates 4-byte xfer to block-xfer. */
      tt_data = 4;		/* This many bytes. */
      tt_addr2 = (TTRACE_ARG_TYPE) & data;	/* Address of xfer source. */
      break;

    case PT_RDTEXT:
      tt_request = TT_PROC_RDTEXT;
      break;

    case PT_RDDATA:
      tt_request = TT_PROC_RDDATA;
      break;

    case PT_WRTEXT:
      tt_request = TT_PROC_WRTEXT;
      break;

    case PT_WRDATA:
      tt_request = TT_PROC_WRDATA;
      break;

    case PT_CONTINUE:
      tt_request = TT_PROC_CONTINUE;
      break;

    case PT_STEP:
      tt_request = TT_LWP_SINGLE;	/* Should not be making this request? */
      break;

    case PT_KILL:
      tt_request = TT_PROC_EXIT;
      break;

    case PT_GET_PROCESS_PATHNAME:
      tt_request = TT_PROC_GET_PATHNAME;
      break;

    default:
      tt_request = pt_request;	/* Let ttrace be the one to complain. */
      break;
    }

  return call_ttrace (tt_request,
		      gdb_tid,
		      tt_addr,
		      tt_data,
		      tt_addr2);
}

/* Kill that pesky process!
 */
void
kill_inferior (void)
{
  int tid;
  int wait_status;
  thread_info *t;
  thread_info **paranoia;
  int para_count, i;

  if (PIDGET (inferior_ptid) == 0)
    return;

  /* Walk the list of "threads", some of which are "pseudo threads",
     aka "processes".  For each that is NOT inferior_ptid, stop it,
     and detach it.

     You see, we may not have just a single process to kill.  If we're
     restarting or quitting or detaching just after the inferior has
     forked, then we've actually two processes to clean up.

     But we can't just call target_mourn_inferior() for each, since that
     zaps the target vector.
   */

  paranoia = (thread_info **) xmalloc (thread_head.count *
				       sizeof (thread_info *));
  para_count = 0;

  t = thread_head.head;
  while (t)
    {

      paranoia[para_count] = t;
      for (i = 0; i < para_count; i++)
	{
	  if (t->next == paranoia[i])
	    {
	      warning ("Bad data in gdb's thread data; repairing.");
	      t->next = 0;
	    }
	}
      para_count++;

      if (t->am_pseudo && (t->pid != PIDGET (inferior_ptid)))
	{
	  call_ttrace (TT_PROC_EXIT,
		       t->pid,
		       TT_NIL,
		       TT_NIL,
		       TT_NIL);
	}
      t = t->next;
    }

  xfree (paranoia);

  call_ttrace (TT_PROC_EXIT,
	       PIDGET (inferior_ptid),
	       TT_NIL,
	       TT_NIL,
	       TT_NIL);
  target_mourn_inferior ();
  clear_thread_info ();
}


#ifndef CHILD_RESUME

/* Sanity check a thread about to be continued.
 */
static void
thread_dropping_event_check (thread_info *p)
{
  if (!p->handled)
    {
      /*
       * This seems to happen when we "next" over a
       * "fork()" while following the parent.  If it's
       * the FORK event, that's ok.  If it's a SIGNAL
       * in the unfollowed child, that's ok to--but
       * how can we know that's what's going on?
       *
       * FIXME!
       */
      if (p->have_state)
	{
	  if (p->last_stop_state.tts_event == TTEVT_FORK)
	    {
	      /* Ok */
	      ;
	    }
	  else if (p->last_stop_state.tts_event == TTEVT_SIGNAL)
	    {
	      /* Ok, close eyes and let it happen.
	       */
	      ;
	    }
	  else
	    {
	      /* This shouldn't happen--we're dropping a
	       * real event.
	       */
	      warning ("About to continue process %d, thread %d with unhandled event %s.",
		       p->pid, p->tid,
		       get_printable_name_of_ttrace_event (
					     p->last_stop_state.tts_event));

#ifdef PARANOIA
	      if (debug_on)
		print_tthread (p);
#endif
	    }
	}
      else
	{
	  /* No saved state, have to assume it failed.
	   */
	  warning ("About to continue process %d, thread %d with unhandled event.",
		   p->pid, p->tid);
#ifdef PARANOIA
	  if (debug_on)
	    print_tthread (p);
#endif
	}
    }

}				/* thread_dropping_event_check */

/* Use a loop over the threads to continue all the threads but
 * the one specified, which is to be stepped.
 */
static void
threads_continue_all_but_one (lwpid_t gdb_tid, int signal)
{
  thread_info *p;
  int thread_signal;
  lwpid_t real_tid;
  lwpid_t scan_tid;
  ttstate_t state;
  int real_pid;

#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("Using loop over threads to step/resume with signals\n");
#endif

  /* First update the thread list.
   */
  set_all_unseen ();
  real_tid = map_from_gdb_tid (gdb_tid);
  real_pid = get_pid_for (real_tid);

  scan_tid = get_process_first_stopped_thread_id (real_pid, &state);
  while (0 != scan_tid)
    {

#ifdef THREAD_DEBUG
      /* FIX: later should check state is stopped;
       * state.tts_flags & TTS_STATEMASK == TTS_WASSUSPENDED
       */
      if (debug_on)
 	if ((state.tts_flags & TTS_STATEMASK) != TTS_WASSUSPENDED)
	  printf ("About to continue non-stopped thread %d\n", scan_tid);
#endif

      p = find_thread_info (scan_tid);
      if (NULL == p)
	{
	  add_tthread (real_pid, scan_tid);
	  p = find_thread_info (scan_tid);

	  /* This is either a newly-created thread or the
	   * result of a fork; in either case there's no
	   * actual event to worry about.
	   */
	  p->handled = 1;

	  if (state.tts_event != TTEVT_NONE)
	    {
	      /* Oops, do need to worry!
	       */
	      warning ("Unexpected thread with \"%s\" event.",
		       get_printable_name_of_ttrace_event (state.tts_event));
	    }
	}
      else if (scan_tid != p->tid)
	error ("Bad data in thread database.");

#ifdef THREAD_DEBUG
      if (debug_on)
	if (p->terminated)
	  printf ("Why are we continuing a dead thread?\n");
#endif

      p->seen = 1;

      scan_tid = get_process_next_stopped_thread_id (real_pid, &state);
    }

  /* Remove unseen threads.
   */
  update_thread_list ();

  /* Now run down the thread list and continue or step.
   */
  for (p = thread_head.head; p; p = p->next)
    {

      /* Sanity check.
       */
      thread_dropping_event_check (p);

      /* Pass the correct signals along.
       */
      if (p->have_signal)
	{
	  thread_signal = p->signal_value;
	  p->have_signal = 0;
	}
      else
	thread_signal = 0;

      if (p->tid != real_tid)
	{
	  /*
	   * Not the thread of interest, so continue it
	   * as the user expects.
	   */
	  if (p->stepping_mode == DO_STEP)
	    {
	      /* Just step this thread.
	       */
	      call_ttrace (
			    TT_LWP_SINGLE,
			    p->tid,
			    TT_USE_CURRENT_PC,
			    (TTRACE_ARG_TYPE) target_signal_to_host (signal),
			    TT_NIL);
	    }
	  else
	    {
	      /* Regular continue (default case).
	       */
	      call_ttrace (
			    TT_LWP_CONTINUE,
			    p->tid,
			    TT_USE_CURRENT_PC,
		    (TTRACE_ARG_TYPE) target_signal_to_host (thread_signal),
			    TT_NIL);
	    }
	}
      else
	{
	  /* Step the thread of interest.
	   */
	  call_ttrace (
			TT_LWP_SINGLE,
			real_tid,
			TT_USE_CURRENT_PC,
			(TTRACE_ARG_TYPE) target_signal_to_host (signal),
			TT_NIL);
	}
    }				/* Loop over threads */
}				/* End threads_continue_all_but_one */

/* Use a loop over the threads to continue all the threads.
 * This is done when a signal must be sent to any of the threads.
 */
static void
threads_continue_all_with_signals (lwpid_t gdb_tid, int signal)
{
  thread_info *p;
  int thread_signal;
  lwpid_t real_tid;
  lwpid_t scan_tid;
  ttstate_t state;
  int real_pid;

#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("Using loop over threads to resume with signals\n");
#endif

  /* Scan and update thread list.
   */
  set_all_unseen ();
  real_tid = map_from_gdb_tid (gdb_tid);
  real_pid = get_pid_for (real_tid);

  scan_tid = get_process_first_stopped_thread_id (real_pid, &state);
  while (0 != scan_tid)
    {

#ifdef THREAD_DEBUG
      if (debug_on)
	if ((state.tts_flags & TTS_STATEMASK) != TTS_WASSUSPENDED)
	  warning ("About to continue non-stopped thread %d\n", scan_tid);
#endif

      p = find_thread_info (scan_tid);
      if (NULL == p)
	{
	  add_tthread (real_pid, scan_tid);
	  p = find_thread_info (scan_tid);

	  /* This is either a newly-created thread or the
	   * result of a fork; in either case there's no
	   * actual event to worry about.
	   */
	  p->handled = 1;

	  if (state.tts_event != TTEVT_NONE)
	    {
	      /* Oops, do need to worry!
	       */
	      warning ("Unexpected thread with \"%s\" event.",
		       get_printable_name_of_ttrace_event (state.tts_event));
	    }
	}

#ifdef THREAD_DEBUG
      if (debug_on)
	if (p->terminated)
	  printf ("Why are we continuing a dead thread? (1)\n");
#endif

      p->seen = 1;

      scan_tid = get_process_next_stopped_thread_id (real_pid, &state);
    }

  /* Remove unseen threads from our list.
   */
  update_thread_list ();

  /* Continue the threads.
   */
  for (p = thread_head.head; p; p = p->next)
    {

      /* Sanity check.
       */
      thread_dropping_event_check (p);

      /* Pass the correct signals along.
       */
      if (p->tid == real_tid)
	{
	  thread_signal = signal;
	  p->have_signal = 0;
	}
      else if (p->have_signal)
	{
	  thread_signal = p->signal_value;
	  p->have_signal = 0;
	}
      else
	thread_signal = 0;

      if (p->stepping_mode == DO_STEP)
	{
	  call_ttrace (
			TT_LWP_SINGLE,
			p->tid,
			TT_USE_CURRENT_PC,
			(TTRACE_ARG_TYPE) target_signal_to_host (signal),
			TT_NIL);
	}
      else
	{
	  /* Continue this thread (default case).
	   */
	  call_ttrace (
			TT_LWP_CONTINUE,
			p->tid,
			TT_USE_CURRENT_PC,
		    (TTRACE_ARG_TYPE) target_signal_to_host (thread_signal),
			TT_NIL);
	}
    }
}				/* End threads_continue_all_with_signals */

/* Step one thread only.  
 */
static void
thread_fake_step (lwpid_t tid, enum target_signal signal)
{
  thread_info *p;

#ifdef THREAD_DEBUG
  if (debug_on)
    {
      printf ("Doing a fake-step over a bpt, etc. for %d\n", tid);

      if (is_terminated (tid))
	printf ("Why are we continuing a dead thread? (4)\n");
    }
#endif

  if (doing_fake_step)
    warning ("Step while step already in progress.");

  /* See if there's a saved signal value for this
   * thread to be passed on, but no current signal.
   */
  p = find_thread_info (tid);
  if (p != NULL)
    {
      if (p->have_signal && signal == TARGET_SIGNAL_0)
	{
	  /* Pass on a saved signal.
	   */
	  signal = p->signal_value;
	}

      p->have_signal = 0;
    }

  if (!p->handled)
    warning ("Internal error: continuing unhandled thread.");

  call_ttrace (TT_LWP_SINGLE,
	       tid,
	       TT_USE_CURRENT_PC,
	       (TTRACE_ARG_TYPE) target_signal_to_host (signal),
	       TT_NIL);

  /* Do bookkeeping so "call_ttrace_wait" knows it has to wait
   * for this thread only, and clear any saved signal info.
   */
  doing_fake_step = 1;
  fake_step_tid = tid;

}				/* End thread_fake_step */

/* Continue one thread when a signal must be sent to it.
 */
static void
threads_continue_one_with_signal (lwpid_t gdb_tid, int signal)
{
  thread_info *p;
  lwpid_t real_tid;
  int real_pid;

#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("Continuing one thread with a signal\n");
#endif

  real_tid = map_from_gdb_tid (gdb_tid);
  real_pid = get_pid_for (real_tid);

  p = find_thread_info (real_tid);
  if (NULL == p)
    {
      add_tthread (real_pid, real_tid);
    }

#ifdef THREAD_DEBUG
  if (debug_on)
    if (p->terminated)
      printf ("Why are we continuing a dead thread? (2)\n");
#endif

  if (!p->handled)
    warning ("Internal error: continuing unhandled thread.");

  p->have_signal = 0;

  call_ttrace (TT_LWP_CONTINUE,
	       gdb_tid,
	       TT_USE_CURRENT_PC,
	       (TTRACE_ARG_TYPE) target_signal_to_host (signal),
	       TT_NIL);
}
#endif

#ifndef CHILD_RESUME

/* Resume execution of the inferior process.

 * This routine is in charge of setting the "handled" bits. 
 *
 *   If STEP is zero,      continue it.
 *   If STEP is nonzero,   single-step it.
 *   
 *   If SIGNAL is nonzero, give it that signal.
 *
 *   If TID is -1,         apply to all threads.
 *   If TID is not -1,     apply to specified thread.
 *   
 *           STEP
 *      \      !0                        0
 *  TID  \________________________________________________
 *       |
 *   -1  |   Step current            Continue all threads
 *       |   thread and              (but which gets any
 *       |   continue others         signal?--We look at
 *       |                           "inferior_ptid")
 *       |
 *    N  |   Step _this_ thread      Continue _this_ thread
 *       |   and leave others        and leave others 
 *       |   stopped; internally     stopped; used only for
 *       |   used by gdb, never      hardware watchpoints
 *       |   a user command.         and attach, never a
 *       |                           user command.
 */
void
child_resume (ptid_t ptid, int step, enum target_signal signal)
{
  int resume_all_threads;
  lwpid_t tid;
  process_state_t new_process_state;
  lwpid_t gdb_tid = PIDGET (ptid);

  resume_all_threads =
    (gdb_tid == INFTTRACE_ALL_THREADS) ||
    (vfork_in_flight);

  if (resume_all_threads)
    {
      /* Resume all threads, but first pick a tid value
       * so we can get the pid when in call_ttrace doing
       * the map.
       */
      if (vfork_in_flight)
	tid = vforking_child_pid;
      else
	tid = map_from_gdb_tid (PIDGET (inferior_ptid));
    }
  else
    tid = map_from_gdb_tid (gdb_tid);

#ifdef THREAD_DEBUG
  if (debug_on)
    {
      if (more_events_left)
	printf ("More events; ");

      if (signal != 0)
	printf ("Sending signal %d; ", signal);

      if (resume_all_threads)
	{
	  if (step == 0)
	    printf ("Continue process %d\n", tid);
	  else
	    printf ("Step/continue thread %d\n", tid);
	}
      else
	{
	  if (step == 0)
	    printf ("Continue thread %d\n", tid);
	  else
	    printf ("Step just thread %d\n", tid);
	}

      if (vfork_in_flight)
	printf ("Vfork in flight\n");
    }
#endif

  if (process_state == RUNNING)
    warning ("Internal error in resume logic; doing resume or step anyway.");

  if (!step			/* Asked to continue...       */
      && resume_all_threads	/* whole process..            */
      && signal != 0		/* with a signal...           */
      && more_events_left > 0)
    {				/* but we can't yet--save it! */

      /* Continue with signal means we have to set the pending
       * signal value for this thread.
       */
      thread_info *k;

#ifdef THREAD_DEBUG
      if (debug_on)
	printf ("Saving signal %d for thread %d\n", signal, tid);
#endif

      k = find_thread_info (tid);
      if (k != NULL)
	{
	  k->have_signal = 1;
	  k->signal_value = signal;

#ifdef THREAD_DEBUG
	  if (debug_on)
	    if (k->terminated)
	      printf ("Why are we continuing a dead thread? (3)\n");
#endif

	}

#ifdef THREAD_DEBUG
      else if (debug_on)
	{
	  printf ("No thread info for tid %d\n", tid);
	}
#endif
    }

  /* Are we faking this "continue" or "step"?

   * We used to do steps by continuing all the threads for 
   * which the events had been handled already.  While
   * conceptually nicer (hides it all in a lower level), this
   * can lead to starvation and a hang (e.g. all but one thread
   * are unhandled at a breakpoint just before a "join" operation,
   * and one thread is in the join, and the user wants to step that
   * thread).
   */
  if (resume_all_threads	/* Whole process, therefore user command */
      && more_events_left > 0)
    {				/* But we can't do this yet--fake it! */
      thread_info *p;

      if (!step)
	{
	  /* No need to do any notes on a per-thread
	   * basis--we're done!
	   */
#ifdef WAIT_BUFFER_DEBUG
	  if (debug_on)
	    printf ("Faking a process resume.\n");
#endif

	  return;
	}
      else
	{

#ifdef WAIT_BUFFER_DEBUG
	  if (debug_on)
	    printf ("Faking a process step.\n");
#endif

	}

      p = find_thread_info (tid);
      if (p == NULL)
	{
	  warning ("No thread information for tid %d, 'next' command ignored.\n", tid);
	  return;
	}
      else
	{

#ifdef THREAD_DEBUG
	  if (debug_on)
	    if (p->terminated)
	      printf ("Why are we continuing a dead thread? (3.5)\n");
#endif

	  if (p->stepping_mode != DO_DEFAULT)
	    {
	      warning ("Step or continue command applied to thread which is already stepping or continuing; command ignored.");

	      return;
	    }

	  if (step)
	    p->stepping_mode = DO_STEP;
	  else
	    p->stepping_mode = DO_CONTINUE;

	  return;
	}			/* Have thread info */
    }				/* Must fake step or go */

  /* Execept for fake-steps, from here on we know we are
   * going to wind up with a running process which will
   * need a real wait.
   */
  new_process_state = RUNNING;

  /* An address of TT_USE_CURRENT_PC tells ttrace to continue from where
   * it was.  (If GDB wanted it to start some other way, we have already
   * written a new PC value to the child.)
   *
   * If this system does not support PT_STEP, a higher level function will
   * have called single_step() to transmute the step request into a
   * continue request (by setting breakpoints on all possible successor
   * instructions), so we don't have to worry about that here.
   */
  if (step)
    {
      if (resume_all_threads)
	{
	  /*
	   * Regular user step: other threads get a "continue".
	   */
	  threads_continue_all_but_one (tid, signal);
	  clear_all_handled ();
	  clear_all_stepping_mode ();
	}

      else
	{
	  /* "Fake step": gdb is stepping one thread over a
	   * breakpoint, watchpoint, or out of a library load
	   * event, etc.  The rest just stay where they are.
	   *
	   * Also used when there are pending events: we really
	   * step the current thread, but leave the rest stopped.
	   * Users can't request this, but "wait_for_inferior"
	   * does--a lot!
	   */
	  thread_fake_step (tid, signal);

	  /* Clear the "handled" state of this thread, because
	   * we'll soon get a new event for it.  Other events
	   * stay as they were.
	   */
	  clear_handled (tid);
	  clear_stepping_mode (tid);
	  new_process_state = FAKE_STEPPING;
	}
    }

  else
    {
      /* TT_LWP_CONTINUE can pass signals to threads, TT_PROC_CONTINUE can't.
	 Therefore, we really can't use TT_PROC_CONTINUE here.

	 Consider a process which stopped due to signal which gdb decides
	 to handle and not pass on to the inferior.  In that case we must
	 clear the pending signal by restarting the inferior using
	 TT_LWP_CONTINUE and pass zero as the signal number.  Else the
	 pending signal will be passed to the inferior.  interrupt.exp
	 in the testsuite does this precise thing and fails due to the
	 unwanted signal delivery to the inferior.  */
      /* drow/2002-12-05: However, note that we must use TT_PROC_CONTINUE
	 if we are tracing a vfork.  */
      if (vfork_in_flight)
	{
	  call_ttrace (TT_PROC_CONTINUE, tid, TT_NIL, TT_NIL, TT_NIL);
	  clear_all_handled ();
	  clear_all_stepping_mode ();
	}
      else if (resume_all_threads)
	{
#ifdef THREAD_DEBUG
	  if (debug_on)
	    printf ("Doing a continue by loop of all threads\n");
#endif

	  threads_continue_all_with_signals (tid, signal);

	  clear_all_handled ();
	  clear_all_stepping_mode ();
	}
      else
	{
#ifdef THREAD_DEBUG
	  printf ("Doing a continue w/signal of just thread %d\n", tid);
#endif

	  threads_continue_one_with_signal (tid, signal);

	  /* Clear the "handled" state of this thread, because we
	     will soon get a new event for it.  Other events can
	     stay as they were.  */
	  clear_handled (tid);
	  clear_stepping_mode (tid);
	}
    }

  process_state = new_process_state;

#ifdef WAIT_BUFFER_DEBUG
  if (debug_on)
    printf ("Process set to %s\n",
	    get_printable_name_of_process_state (process_state));
#endif

}
#endif /* CHILD_RESUME */


#ifdef ATTACH_DETACH
/*
 * Like it says.
 *
 * One worry is that we may not be attaching to "inferior_ptid"
 * and thus may not want to clear out our data.  FIXME?
 * 
 */
static void
update_thread_state_after_attach (int pid, attach_continue_t kind_of_go)
{
  int tt_status;
  ttstate_t thread_state;
  lwpid_t a_thread;
  lwpid_t tid;

  /* The process better be stopped.
   */
  if (process_state != STOPPED
      && process_state != VFORKING)
    warning ("Internal error attaching.");

  /* Clear out old tthread info and start over.  This has the
   * side effect of ensuring that the TRAP is reported as being
   * in the right thread (re-mapped from tid to pid).
   *
   * It's because we need to add the tthread _now_ that we
   * need to call "clear_thread_info" _now_, and that's why
   * "require_notification_of_events" doesn't clear the thread
   * info (it's called later than this routine).
   */
  clear_thread_info ();
  a_thread = 0;

  for (tid = get_process_first_stopped_thread_id (pid, &thread_state);
       tid != 0;
       tid = get_process_next_stopped_thread_id (pid, &thread_state))
    {
      thread_info *p;

      if (a_thread == 0)
	{
	  a_thread = tid;
#ifdef THREAD_DEBUG
	  if (debug_on)
	    printf ("Attaching to process %d, thread %d\n",
		    pid, a_thread);
#endif
	}

      /* Tell ourselves and the "rest of gdb" that this thread
       * exists.
       *
       * This isn't really a hack.  Other thread-based versions
       * of gdb (e.g. gnu-nat.c) seem to do the same thing.
       *
       * We don't need to do mapping here, as we know this
       * is the first thread and thus gets the real pid
       * (and is "inferior_ptid").
       *
       * NOTE: it probably isn't the originating thread,
       *       but that doesn't matter (we hope!).
       */
      add_tthread (pid, tid);
      p = find_thread_info (tid);
      if (NULL == p)		/* ?We just added it! */
	error ("Internal error adding a thread on attach.");

      copy_ttstate_t (&p->last_stop_state, &thread_state);
      p->have_state = 1;

      if (DO_ATTACH_CONTINUE == kind_of_go)
	{
	  /*
	   * If we are going to CONTINUE afterwards,
	   * raising a SIGTRAP, don't bother trying to
	   * handle this event.  But check first!
	   */
	  switch (p->last_stop_state.tts_event)
	    {

	    case TTEVT_NONE:
	      /* Ok to set this handled.
	       */
	      break;

	    default:
	      warning ("Internal error; skipping event %s on process %d, thread %d.",
		       get_printable_name_of_ttrace_event (
					      p->last_stop_state.tts_event),
		       p->pid, p->tid);
	    }

	  set_handled (pid, tid);

	}
      else
	{
	  /* There will be no "continue" opertion, so the
	   * process remains stopped.  Don't set any events
	   * handled except the "gimmies".
	   */
	  switch (p->last_stop_state.tts_event)
	    {

	    case TTEVT_NONE:
	      /* Ok to ignore this.
	       */
	      set_handled (pid, tid);
	      break;

	    case TTEVT_EXEC:
	    case TTEVT_FORK:
	      /* Expected "other" FORK or EXEC event from a
	       * fork or vfork.
	       */
	      break;

	    default:
	      printf ("Internal error: failed to handle event %s on process %d, thread %d.",
		      get_printable_name_of_ttrace_event (
					      p->last_stop_state.tts_event),
		      p->pid, p->tid);
	    }
	}

      add_thread (pid_to_ptid (pid));		/* in thread.c */
    }

#ifdef PARANOIA
  if (debug_on)
    print_tthreads ();
#endif

  /* One mustn't call ttrace_wait() after attaching via ttrace,
     'cause the process is stopped already.

     However, the upper layers of gdb's execution control will
     want to wait after attaching (but not after forks, in
     which case they will be doing a "target_resume", anticipating
     a later TTEVT_EXEC or TTEVT_FORK event).

     To make this attach() implementation more compatible with
     others, we'll make the attached-to process raise a SIGTRAP.

     Issue: this continues only one thread.  That could be
     dangerous if the thread is blocked--the process won't run
     and no trap will be raised.  FIX! (check state.tts_flags?
     need one that's either TTS_WASRUNNING--but we've stopped
     it and made it TTS_WASSUSPENDED.  Hum...FIXME!)
   */
  if (DO_ATTACH_CONTINUE == kind_of_go)
    {
      tt_status = call_real_ttrace (
				     TT_LWP_CONTINUE,
				     pid,
				     a_thread,
				     TT_USE_CURRENT_PC,
	       (TTRACE_ARG_TYPE) target_signal_to_host (TARGET_SIGNAL_TRAP),
				     TT_NIL);
      if (errno)
	perror_with_name ("ttrace");

      clear_handled (a_thread);	/* So TRAP will be reported. */

      /* Now running.
       */
      process_state = RUNNING;
    }

  attach_flag = 1;
}
#endif /* ATTACH_DETACH */


#ifdef ATTACH_DETACH
/* Start debugging the process whose number is PID.
 * (A _real_ pid).
 */
int
attach (int pid)
{
  int tt_status;

  tt_status = call_real_ttrace (
				 TT_PROC_ATTACH,
				 pid,
				 (lwpid_t) TT_NIL,
				 TT_NIL,
				 (TTRACE_ARG_TYPE) TT_VERSION,
				 TT_NIL);
  if (errno)
    perror_with_name ("ttrace attach");

  /* If successful, the process is now stopped.
   */
  process_state = STOPPED;

  /* Our caller ("attach_command" in "infcmd.c")
   * expects to do a "wait_for_inferior" after
   * the attach, so make sure the inferior is
   * running when we're done.
   */
  update_thread_state_after_attach (pid, DO_ATTACH_CONTINUE);

  return pid;
}


#if defined(CHILD_POST_ATTACH)
void
child_post_attach (int pid)
{
#ifdef THREAD_DEBUG
  if (debug_on)
    printf ("child-post-attach call\n");
#endif

  require_notification_of_events (pid);
}
#endif


/* Stop debugging the process whose number is PID
   and continue it with signal number SIGNAL.
   SIGNAL = 0 means just continue it.
 */
void
detach (int signal)
{
  errno = 0;
  call_ttrace (TT_PROC_DETACH,
	       PIDGET (inferior_ptid),
	       TT_NIL,
	       (TTRACE_ARG_TYPE) signal,
	       TT_NIL);
  attach_flag = 0;

  clear_thread_info ();

  /* Process-state? */
}
#endif /* ATTACH_DETACH */


/* Default the type of the ttrace transfer to int.  */
#ifndef TTRACE_XFER_TYPE
#define TTRACE_XFER_TYPE int
#endif

void
_initialize_kernel_u_addr (void)
{
}

#if !defined (CHILD_XFER_MEMORY)
/* NOTE! I tried using TTRACE_READDATA, etc., to read and write memory
   in the NEW_SUN_TTRACE case.
   It ought to be straightforward.  But it appears that writing did
   not write the data that I specified.  I cannot understand where
   it got the data that it actually did write.  */

/* Copy LEN bytes to or from inferior's memory starting at MEMADDR
   to debugger memory starting at MYADDR.   Copy to inferior if
   WRITE is nonzero.  TARGET is ignored.

   Returns the length copied, which is either the LEN argument or zero.
   This xfer function does not do partial moves, since child_ops
   doesn't allow memory operations to cross below us in the target stack
   anyway.  */

int
child_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int write,
		   struct mem_attrib *attrib,
		   struct target_ops *target)
{
  int i;
  /* Round starting address down to longword boundary.  */
  CORE_ADDR addr = memaddr & -(CORE_ADDR) sizeof (TTRACE_XFER_TYPE);
  /* Round ending address up; get number of longwords that makes.  */
  int count
  = (((memaddr + len) - addr) + sizeof (TTRACE_XFER_TYPE) - 1)
  / sizeof (TTRACE_XFER_TYPE);
  /* Allocate buffer of that many longwords.  */
  /* FIXME (alloca): This code, cloned from infptrace.c, is unsafe
     because it uses alloca to allocate a buffer of arbitrary size.
     For very large xfers, this could crash GDB's stack.  */
  TTRACE_XFER_TYPE *buffer
    = (TTRACE_XFER_TYPE *) alloca (count * sizeof (TTRACE_XFER_TYPE));

  if (write)
    {
      /* Fill start and end extra bytes of buffer with existing memory data.  */

      if (addr != memaddr || len < (int) sizeof (TTRACE_XFER_TYPE))
	{
	  /* Need part of initial word -- fetch it.  */
	  buffer[0] = call_ttrace (TT_LWP_RDTEXT,
				   PIDGET (inferior_ptid),
				   (TTRACE_ARG_TYPE) addr,
				   TT_NIL,
				   TT_NIL);
	}

      if (count > 1)		/* FIXME, avoid if even boundary */
	{
	  buffer[count - 1] = call_ttrace (TT_LWP_RDTEXT,
					   PIDGET (inferior_ptid),
					   ((TTRACE_ARG_TYPE)
			  (addr + (count - 1) * sizeof (TTRACE_XFER_TYPE))),
					   TT_NIL,
					   TT_NIL);
	}

      /* Copy data to be written over corresponding part of buffer */

      memcpy ((char *) buffer + (memaddr & (sizeof (TTRACE_XFER_TYPE) - 1)),
	      myaddr,
	      len);

      /* Write the entire buffer.  */

      for (i = 0; i < count; i++, addr += sizeof (TTRACE_XFER_TYPE))
	{
	  errno = 0;
	  call_ttrace (TT_LWP_WRDATA,
		       PIDGET (inferior_ptid),
		       (TTRACE_ARG_TYPE) addr,
		       (TTRACE_ARG_TYPE) buffer[i],
		       TT_NIL);
	  if (errno)
	    {
	      /* Using the appropriate one (I or D) is necessary for
	         Gould NP1, at least.  */
	      errno = 0;
	      call_ttrace (TT_LWP_WRTEXT,
			   PIDGET (inferior_ptid),
			   (TTRACE_ARG_TYPE) addr,
			   (TTRACE_ARG_TYPE) buffer[i],
			   TT_NIL);
	    }
	  if (errno)
	    return 0;
	}
    }
  else
    {
      /* Read all the longwords */
      for (i = 0; i < count; i++, addr += sizeof (TTRACE_XFER_TYPE))
	{
	  errno = 0;
	  buffer[i] = call_ttrace (TT_LWP_RDTEXT,
				   PIDGET (inferior_ptid),
				   (TTRACE_ARG_TYPE) addr,
				   TT_NIL,
				   TT_NIL);
	  if (errno)
	    return 0;
	  QUIT;
	}

      /* Copy appropriate bytes out of the buffer.  */
      memcpy (myaddr,
	      (char *) buffer + (memaddr & (sizeof (TTRACE_XFER_TYPE) - 1)),
	      len);
    }
  return len;
}


static void
udot_info (void)
{
  int udot_off;			/* Offset into user struct */
  int udot_val;			/* Value from user struct at udot_off */
  char mess[128];		/* For messages */

  if (!target_has_execution)
    {
      error ("The program is not being run.");
    }

#if !defined (KERNEL_U_SIZE)

  /* Adding support for this command is easy.  Typically you just add a
     routine, called "kernel_u_size" that returns the size of the user
     struct, to the appropriate *-nat.c file and then add to the native
     config file "#define KERNEL_U_SIZE kernel_u_size()" */
  error ("Don't know how large ``struct user'' is in this version of gdb.");

#else

  for (udot_off = 0; udot_off < KERNEL_U_SIZE; udot_off += sizeof (udot_val))
    {
      if ((udot_off % 24) == 0)
	{
	  if (udot_off > 0)
	    {
	      printf_filtered ("\n");
	    }
	  printf_filtered ("%04x:", udot_off);
	}
      udot_val = call_ttrace (TT_LWP_RUREGS,
			      PIDGET (inferior_ptid),
			      (TTRACE_ARG_TYPE) udot_off,
			      TT_NIL,
			      TT_NIL);
      if (errno != 0)
	{
	  sprintf (mess, "\nreading user struct at offset 0x%x", udot_off);
	  perror_with_name (mess);
	}
      /* Avoid using nonportable (?) "*" in print specs */
      printf_filtered (sizeof (int) == 4 ? " 0x%08x" : " 0x%16x", udot_val);
    }
  printf_filtered ("\n");

#endif
}
#endif /* !defined (CHILD_XFER_MEMORY).  */


/* TTrace version of "target_pid_to_exec_file"
 */
char *
child_pid_to_exec_file (int tid)
{
  int tt_status;
  static char exec_file_buffer[1024];
  pid_t pid;
  static struct pst_status buf;

  /* On various versions of hpux11, this may fail due to a supposed
     kernel bug.  We have alternate methods to get this information
     (ie pstat).  */
  tt_status = call_ttrace (TT_PROC_GET_PATHNAME,
			   tid,
			   (uint64_t) exec_file_buffer,
			   sizeof (exec_file_buffer) - 1,
			   0);
  if (tt_status >= 0)
    return exec_file_buffer;

  /* Try to get process information via pstat and extract the filename
     from the pst_cmd field within the pst_status structure.  */
  if (pstat_getproc (&buf, sizeof (struct pst_status), 0, tid) != -1)
    {
      char *p = buf.pst_cmd;

      while (*p && *p != ' ')
	p++;
      *p = 0;

      return (buf.pst_cmd);
    }

  return (NULL);
}

void
pre_fork_inferior (void)
{
  int status;

  status = pipe (startup_semaphore.parent_channel);
  if (status < 0)
    {
      warning ("error getting parent pipe for startup semaphore");
      return;
    }

  status = pipe (startup_semaphore.child_channel);
  if (status < 0)
    {
      warning ("error getting child pipe for startup semaphore");
      return;
    }
}

/* Called from child_follow_fork in hppah-nat.c.
 *
 * This seems to be intended to attach after a fork or
 * vfork, while "attach" is used to attach to a pid
 * given by the user.  The check for an existing attach
 * seems odd--it always fails in our test system.
 */
int
hppa_require_attach (int pid)
{
  int tt_status;
  CORE_ADDR pc;
  CORE_ADDR pc_addr;
  unsigned int regs_offset;
  process_state_t old_process_state = process_state;

  /* Are we already attached?  There appears to be no explicit
   * way to answer this via ttrace, so we try something which
   * should be innocuous if we are attached.  If that fails,
   * then we assume we're not attached, and so attempt to make
   * it so.
   */
  errno = 0;
  tt_status = call_real_ttrace (TT_PROC_STOP,
				pid,
				(lwpid_t) TT_NIL,
				(TTRACE_ARG_TYPE) TT_NIL,
				(TTRACE_ARG_TYPE) TT_NIL,
				TT_NIL);

  if (errno)
    {
      /* No change to process-state!
       */
      errno = 0;
      pid = attach (pid);
    }
  else
    {
      /* If successful, the process is now stopped.  But if
       * we're VFORKING, the parent is still running, so don't
       * change the process state.
       */
      if (process_state != VFORKING)
	process_state = STOPPED;

      /* If we were already attached, you'd think that we
       * would need to start going again--but you'd be wrong,
       * as the fork-following code is actually in the middle
       * of the "resume" routine in in "infrun.c" and so
       * will (almost) immediately do a resume.
       *
       * On the other hand, if we are VFORKING, which means
       * that the child and the parent share a process for a
       * while, we know that "resume" won't be resuming
       * until the child EXEC event is seen.  But we still
       * don't want to continue, as the event is already
       * there waiting.
       */
      update_thread_state_after_attach (pid, DONT_ATTACH_CONTINUE);
    }				/* STOP succeeded */

  return pid;
}

int
hppa_require_detach (int pid, int signal)
{
  int tt_status;

  /* If signal is non-zero, we must pass the signal on to the active
     thread prior to detaching.  We do this by continuing the threads
     with the signal.
   */
  if (signal != 0)
    {
      errno = 0;
      threads_continue_all_with_signals (pid, signal);
    }

  errno = 0;
  tt_status = call_ttrace (TT_PROC_DETACH,
			   pid,
			   TT_NIL,
			   TT_NIL,
			   TT_NIL);

  errno = 0;			/* Ignore any errors. */

  /* process_state? */

  return pid;
}

/* Given the starting address of a memory page, hash it to a bucket in
   the memory page dictionary.
 */
static int
get_dictionary_bucket_of_page (CORE_ADDR page_start)
{
  int hash;

  hash = (page_start / memory_page_dictionary.page_size);
  hash = hash % MEMORY_PAGE_DICTIONARY_BUCKET_COUNT;

  return hash;
}


/* Given a memory page's starting address, get (i.e., find an existing
   or create a new) dictionary entry for the page.  The page will be
   write-protected when this function returns, but may have a reference
   count of 0 (if the page was newly-added to the dictionary).
 */
static memory_page_t *
get_dictionary_entry_of_page (int pid, CORE_ADDR page_start)
{
  int bucket;
  memory_page_t *page = NULL;
  memory_page_t *previous_page = NULL;

  /* We're going to be using the dictionary now, than-kew. */
  require_memory_page_dictionary ();

  /* Try to find an existing dictionary entry for this page.  Hash
     on the page's starting address.
   */
  bucket = get_dictionary_bucket_of_page (page_start);
  page = &memory_page_dictionary.buckets[bucket];
  while (page != NULL)
    {
      if (page->page_start == page_start)
	break;
      previous_page = page;
      page = page->next;
    }

  /* Did we find a dictionary entry for this page?  If not, then
     add it to the dictionary now.
   */
  if (page == NULL)
    {
      /* Create a new entry. */
      page = (memory_page_t *) xmalloc (sizeof (memory_page_t));
      page->page_start = page_start;
      page->reference_count = 0;
      page->next = NULL;
      page->previous = NULL;

      /* We'll write-protect the page now, if that's allowed. */
      page->original_permissions = write_protect_page (pid, page_start);

      /* Add the new entry to the dictionary. */
      page->previous = previous_page;
      previous_page->next = page;

      memory_page_dictionary.page_count++;
    }

  return page;
}


static void
remove_dictionary_entry_of_page (int pid, memory_page_t *page)
{
  /* Restore the page's original permissions. */
  unwrite_protect_page (pid, page->page_start, page->original_permissions);

  /* Kick the page out of the dictionary. */
  if (page->previous != NULL)
    page->previous->next = page->next;
  if (page->next != NULL)
    page->next->previous = page->previous;

  /* Just in case someone retains a handle to this after it's freed. */
  page->page_start = (CORE_ADDR) 0;

  memory_page_dictionary.page_count--;

  xfree (page);
}


static void
hppa_enable_syscall_events (int pid)
{
  int tt_status;
  ttevent_t ttrace_events;

  /* Get the set of events that are currently enabled. */
  tt_status = call_ttrace (TT_PROC_GET_EVENT_MASK,
			   pid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);
  if (errno)
    perror_with_name ("ttrace");

  /* Add syscall events to that set. */
  ttrace_events.tte_events |= TTEVT_SYSCALL_ENTRY;
  ttrace_events.tte_events |= TTEVT_SYSCALL_RETURN;

  tt_status = call_ttrace (TT_PROC_SET_EVENT_MASK,
			   pid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);
  if (errno)
    perror_with_name ("ttrace");
}


static void
hppa_disable_syscall_events (int pid)
{
  int tt_status;
  ttevent_t ttrace_events;

  /* Get the set of events that are currently enabled. */
  tt_status = call_ttrace (TT_PROC_GET_EVENT_MASK,
			   pid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);
  if (errno)
    perror_with_name ("ttrace");

  /* Remove syscall events from that set. */
  ttrace_events.tte_events &= ~TTEVT_SYSCALL_ENTRY;
  ttrace_events.tte_events &= ~TTEVT_SYSCALL_RETURN;

  tt_status = call_ttrace (TT_PROC_SET_EVENT_MASK,
			   pid,
			   (TTRACE_ARG_TYPE) & ttrace_events,
			   (TTRACE_ARG_TYPE) sizeof (ttrace_events),
			   TT_NIL);
  if (errno)
    perror_with_name ("ttrace");
}


/* The address range beginning with START and ending with START+LEN-1
   (inclusive) is to be watched via page-protection by a new watchpoint.
   Set protection for all pages that overlap that range.

   Note that our caller sets TYPE to:
   0 for a bp_hardware_watchpoint,
   1 for a bp_read_watchpoint,
   2 for a bp_access_watchpoint

   (Yes, this is intentionally (though lord only knows why) different
   from the TYPE that is passed to hppa_remove_hw_watchpoint.)
 */
int
hppa_insert_hw_watchpoint (int pid, CORE_ADDR start, LONGEST len, int type)
{
  CORE_ADDR page_start;
  int dictionary_was_empty;
  int page_size;
  int page_id;
  LONGEST range_size_in_pages;

  if (type != 0)
    error ("read or access hardware watchpoints not supported on HP-UX");

  /* Examine all pages in the address range. */
  require_memory_page_dictionary ();

  dictionary_was_empty = (memory_page_dictionary.page_count == (LONGEST) 0);

  page_size = memory_page_dictionary.page_size;
  page_start = (start / page_size) * page_size;
  range_size_in_pages = ((LONGEST) len + (LONGEST) page_size - 1) / (LONGEST) page_size;

  for (page_id = 0; page_id < range_size_in_pages; page_id++, page_start += page_size)
    {
      memory_page_t *page;

      /* This gets the page entered into the dictionary if it was
         not already entered.
       */
      page = get_dictionary_entry_of_page (pid, page_start);
      page->reference_count++;
    }

  /* Our implementation depends on seeing calls to kernel code, for the
     following reason.  Here we ask to be notified of syscalls.

     When a protected page is accessed by user code, HP-UX raises a SIGBUS.
     Fine.

     But when kernel code accesses the page, it doesn't give a SIGBUS.
     Rather, the system call that touched the page fails, with errno=EFAULT.
     Not good for us.

     We could accomodate this "feature" by asking to be notified of syscall
     entries & exits; upon getting an entry event, disabling page-protections;
     upon getting an exit event, reenabling page-protections and then checking
     if any watchpoints triggered.

     However, this turns out to be a real performance loser.  syscalls are
     usually a frequent occurrence.  Having to unprotect-reprotect all watched
     pages, and also to then read all watched memory locations and compare for
     triggers, can be quite expensive.

     Instead, we'll only ask to be notified of syscall exits.  When we get
     one, we'll check whether errno is set.  If not, or if it's not EFAULT,
     we can just continue the inferior.

     If errno is set upon syscall exit to EFAULT, we must perform some fairly
     hackish stuff to determine whether the failure really was due to a
     page-protect trap on a watched location.
   */
  if (dictionary_was_empty)
    hppa_enable_syscall_events (pid);

  return 1;
}


/* The address range beginning with START and ending with START+LEN-1
   (inclusive) was being watched via page-protection by a watchpoint
   which has been removed.  Remove protection for all pages that
   overlap that range, which are not also being watched by other
   watchpoints.
 */
int
hppa_remove_hw_watchpoint (int pid, CORE_ADDR start, LONGEST len, int type)
{
  CORE_ADDR page_start;
  int dictionary_is_empty;
  int page_size;
  int page_id;
  LONGEST range_size_in_pages;

  if (type != 0)
    error ("read or access hardware watchpoints not supported on HP-UX");

  /* Examine all pages in the address range. */
  require_memory_page_dictionary ();

  page_size = memory_page_dictionary.page_size;
  page_start = (start / page_size) * page_size;
  range_size_in_pages = ((LONGEST) len + (LONGEST) page_size - 1) / (LONGEST) page_size;

  for (page_id = 0; page_id < range_size_in_pages; page_id++, page_start += page_size)
    {
      memory_page_t *page;

      page = get_dictionary_entry_of_page (pid, page_start);
      page->reference_count--;

      /* Was this the last reference of this page?  If so, then we
         must scrub the entry from the dictionary, and also restore
         the page's original permissions.
       */
      if (page->reference_count == 0)
	remove_dictionary_entry_of_page (pid, page);
    }

  dictionary_is_empty = (memory_page_dictionary.page_count == (LONGEST) 0);

  /* If write protections are currently disallowed, then that implies that
     wait_for_inferior believes that the inferior is within a system call.
     Since we want to see both syscall entry and return, it's clearly not
     good to disable syscall events in this state!

     ??rehrauer: Yeah, it'd be better if we had a specific flag that said,
     "inferior is between syscall events now".  Oh well.
   */
  if (dictionary_is_empty && memory_page_dictionary.page_protections_allowed)
    hppa_disable_syscall_events (pid);

  return 1;
}


/* Could we implement a watchpoint of this type via our available
   hardware support?

   This query does not consider whether a particular address range
   could be so watched, but just whether support is generally available
   for such things.  See hppa_range_profitable_for_hw_watchpoint for a
   query that answers whether a particular range should be watched via
   hardware support.
 */
int
hppa_can_use_hw_watchpoint (int type, int cnt, int ot)
{
  return (type == bp_hardware_watchpoint);
}


/* Assuming we could set a hardware watchpoint on this address, do
   we think it would be profitable ("a good idea") to do so?  If not,
   we can always set a regular (aka single-step & test) watchpoint
   on the address...
 */
int
hppa_range_profitable_for_hw_watchpoint (int pid, CORE_ADDR start, LONGEST len)
{
  int range_is_stack_based;
  int range_is_accessible;
  CORE_ADDR page_start;
  int page_size;
  int page;
  LONGEST range_size_in_pages;

  /* ??rehrauer: For now, say that all addresses are potentially
     profitable.  Possibly later we'll want to test the address
     for "stackness"?
   */
  range_is_stack_based = 0;

  /* If any page in the range is inaccessible, then we cannot
     really use hardware watchpointing, even though our client
     thinks we can.  In that case, it's actually an error to
     attempt to use hw watchpoints, so we'll tell our client
     that the range is "unprofitable", and hope that they listen...
   */
  range_is_accessible = 1;	/* Until proven otherwise. */

  /* Examine all pages in the address range. */
  errno = 0;
  page_size = sysconf (_SC_PAGE_SIZE);

  /* If we can't determine page size, we're hosed.  Tell our
     client it's unprofitable to use hw watchpoints for this
     range.
   */
  if (errno || (page_size <= 0))
    {
      errno = 0;
      return 0;
    }

  page_start = (start / page_size) * page_size;
  range_size_in_pages = len / (LONGEST) page_size;

  for (page = 0; page < range_size_in_pages; page++, page_start += page_size)
    {
      int tt_status;
      int page_permissions;

      /* Is this page accessible? */
      errno = 0;
      tt_status = call_ttrace (TT_PROC_GET_MPROTECT,
			       pid,
			       (TTRACE_ARG_TYPE) page_start,
			       TT_NIL,
			       (TTRACE_ARG_TYPE) & page_permissions);
      if (errno || (tt_status < 0))
	{
	  errno = 0;
	  range_is_accessible = 0;
	  break;
	}

      /* Yes, go for another... */
    }

  return (!range_is_stack_based && range_is_accessible);
}


char *
hppa_pid_or_tid_to_str (ptid_t ptid)
{
  static char buf[100];		/* Static because address returned. */
  pid_t id = PIDGET (ptid);

  /* Does this appear to be a process?  If so, print it that way. */
  if (is_process_id (id))
    return child_pid_to_str (ptid);

  /* Else, print both the GDB thread number and the system thread id. */
  sprintf (buf, "thread %d (", pid_to_thread_id (ptid));
  strcat (buf, hppa_tid_to_str (ptid));
  strcat (buf, ")\0");

  return buf;
}


void
hppa_ensure_vforking_parent_remains_stopped (int pid)
{
  /* Nothing to do when using ttrace.  Only the ptrace-based implementation
     must do real work.
   */
}


int
hppa_resume_execd_vforking_child_to_get_parent_vfork (void)
{
  return 0;			/* No, the parent vfork is available now. */
}


/* Write a register as a 64bit value.  This may be necessary if the
   native OS is too braindamaged to allow some (or all) registers to
   be written in 32bit hunks such as hpux11 and the PC queue registers.

   This is horribly gross and disgusting.  */
 
int
ttrace_write_reg_64 (int gdb_tid, CORE_ADDR dest_addr, CORE_ADDR src_addr)
{
  pid_t 	pid;
  lwpid_t 	tid;
  int  		tt_status;

  tid = map_from_gdb_tid (gdb_tid);
  pid = get_pid_for (tid);

  errno = 0;
  tt_status = ttrace (TT_LWP_WUREGS, 
		      pid, 
		      tid, 
		      (TTRACE_ARG_TYPE) dest_addr, 
		      8, 
		      (TTRACE_ARG_TYPE) src_addr );

#ifdef THREAD_DEBUG
  if (errno)
    {
      /* Don't bother for a known benign error: if you ask for the
         first thread state, but there is only one thread and it's
         not stopped, ttrace complains.
        
         We have this inside the #ifdef because our caller will do
         this check for real.  */
      if( request != TT_PROC_GET_FIRST_LWP_STATE
          ||  errno   != EPROTO )
        {
          if( debug_on )
            printf( "TT fail for %s, with pid %d, tid %d, status %d \n",
                    get_printable_name_of_ttrace_request (TT_LWP_WUREGS),
                    pid, tid, tt_status );
        }
    }
#endif

  return tt_status;
}

void
_initialize_infttrace (void)
{
  /* Initialize the ttrace-based hardware watchpoint implementation. */
  memory_page_dictionary.page_count = (LONGEST) - 1;
  memory_page_dictionary.page_protections_allowed = 1;

  errno = 0;
  memory_page_dictionary.page_size = sysconf (_SC_PAGE_SIZE);

  /* We do a lot of casts from pointers to TTRACE_ARG_TYPE; make sure
     this is okay.  */
  if (sizeof (TTRACE_ARG_TYPE) < sizeof (void *))
    internal_error (__FILE__, __LINE__, "failed internal consistency check");

  if (errno || (memory_page_dictionary.page_size <= 0))
    perror_with_name ("sysconf");
}

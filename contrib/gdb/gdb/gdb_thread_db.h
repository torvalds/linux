#ifdef HAVE_THREAD_DB_H
#include <thread_db.h>
#else

#ifdef HAVE_STDINT_H
#include <stdint.h>
typedef uint32_t gdb_uint32_t;
#define GDB_UINT32_C(c)	UINT32_C(c)
#else
typedef unsigned int gdb_uint32_t;
#define GDB_UINT32_C(c)	c ## U
#endif

/* Copyright 1999, 2000 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef _THREAD_DB_H
#define _THREAD_DB_H	1

/* This is the debugger interface for the LinuxThreads library.  It is
   modelled closely after the interface with same names in Solaris with
   the goal to share the same code in the debugger.  */
#include <pthread.h>
#include <sys/types.h>
#include <sys/procfs.h>


/* Error codes of the library.  */
typedef enum
{
  TD_OK,	  /* No error.  */
  TD_ERR,	  /* No further specified error.  */
  TD_NOTHR,	  /* No matching thread found.  */
  TD_NOSV,	  /* No matching synchronization handle found.  */
  TD_NOLWP,	  /* No matching light-weighted process found.  */
  TD_BADPH,	  /* Invalid process handle.  */
  TD_BADTH,	  /* Invalid thread handle.  */
  TD_BADSH,	  /* Invalid synchronization handle.  */
  TD_BADTA,	  /* Invalid thread agent.  */
  TD_BADKEY,	  /* Invalid key.  */
  TD_NOMSG,	  /* No event available.  */
  TD_NOFPREGS,	  /* No floating-point register content available.  */
  TD_NOLIBTHREAD, /* Application not linked with thread library.  */
  TD_NOEVENT,	  /* Requested event is not supported.  */
  TD_NOCAPAB,	  /* Capability not available.  */
  TD_DBERR,	  /* Internal debug library error.  */
  TD_NOAPLIC,	  /* Operation is not applicable.  */
  TD_NOTSD,	  /* No thread-specific data available.  */
  TD_MALLOC,	  /* Out of memory.  */
  TD_PARTIALREG,  /* Not entire register set was read or written.  */
  TD_NOXREGS,	  /* X register set not available for given thread.  */
  TD_NOTALLOC	  /* TLS memory not yet allocated.  */
} td_err_e;


/* Possible thread states.  TD_THR_ANY_STATE is a pseudo-state used to
   select threads regardless of state in td_ta_thr_iter().  */
typedef enum
{
  TD_THR_ANY_STATE,
  TD_THR_UNKNOWN,
  TD_THR_STOPPED,
  TD_THR_RUN,
  TD_THR_ACTIVE,
  TD_THR_ZOMBIE,
  TD_THR_SLEEP,
  TD_THR_STOPPED_ASLEEP
} td_thr_state_e;

/* Thread type: user or system.  TD_THR_ANY_TYPE is a pseudo-type used
   to select threads regardless of type in td_ta_thr_iter().  */
typedef enum
{
  TD_THR_ANY_TYPE,
  TD_THR_USER,
  TD_THR_SYSTEM
} td_thr_type_e;


/* Types of the debugging library.  */

/* Handle for a process.  This type is opaque.  */
typedef struct td_thragent td_thragent_t;

/* The actual thread handle type.  This is also opaque.  */
typedef struct td_thrhandle
{
  td_thragent_t *th_ta_p;
  psaddr_t th_unique;
} td_thrhandle_t;


/* Flags for `td_ta_thr_iter'.  */
#define TD_THR_ANY_USER_FLAGS	0xffffffff
#define TD_THR_LOWEST_PRIORITY	-20
#define TD_SIGNO_MASK		NULL


#define TD_EVENTSIZE	2
#define BT_UISHIFT	5 /* log base 2 of BT_NBIPUI, to extract word index */
#define BT_NBIPUI	(1 << BT_UISHIFT)       /* n bits per uint */
#define BT_UIMASK	(BT_NBIPUI - 1)         /* to extract bit index */

/* Bitmask of enabled events. */
typedef struct td_thr_events
{
  gdb_uint32_t event_bits[TD_EVENTSIZE];
} td_thr_events_t;

/* Event set manipulation macros. */
#define __td_eventmask(n) \
  (GDB_UINT32_C (1) << (((n) - 1) & BT_UIMASK))
#define __td_eventword(n) \
  ((GDB_UINT32_C ((n) - 1)) >> BT_UISHIFT)

#define td_event_emptyset(setp) \
  do {									      \
    int __i;								      \
    for (__i = TD_EVENTSIZE; __i > 0; --__i)				      \
      (setp)->event_bits[__i - 1] = 0;					      \
  } while (0)

#define td_event_fillset(setp) \
  do {									      \
    int __i;								      \
    for (__i = TD_EVENTSIZE; __i > 0; --__i)				      \
      (setp)->event_bits[__i - 1] = GDB_UINT32_C (0xffffffff);		      \
  } while (0)

#define td_event_addset(setp, n) \
  (((setp)->event_bits[__td_eventword (n)]) |= __td_eventmask (n))
#define td_event_delset(setp, n) \
  (((setp)->event_bits[__td_eventword (n)]) &= ~__td_eventmask (n))
#define td_eventismember(setp, n) \
  (__td_eventmask (n) & ((setp)->event_bits[__td_eventword (n)]))
#if TD_EVENTSIZE == 2
# define td_eventisempty(setp) \
  (!((setp)->event_bits[0]) && !((setp)->event_bits[1]))
#else
# error "td_eventisempty must be changed to match TD_EVENTSIZE"
#endif

/* Events reportable by the thread implementation.  */
typedef enum
{
  TD_ALL_EVENTS,		 /* Pseudo-event number.  */
  TD_EVENT_NONE = TD_ALL_EVENTS, /* Depends on context.  */
  TD_READY,			 /* Is executable now. */
  TD_SLEEP,			 /* Blocked in a synchronization obj.  */
  TD_SWITCHTO,			 /* Now assigned to a process.  */
  TD_SWITCHFROM,		 /* Not anymore assigned to a process.  */
  TD_LOCK_TRY,			 /* Trying to get an unavailable lock.  */
  TD_CATCHSIG,			 /* Signal posted to the thread.  */
  TD_IDLE,			 /* Process getting idle.  */
  TD_CREATE,			 /* New thread created.  */
  TD_DEATH,			 /* Thread terminated.  */
  TD_PREEMPT,			 /* Preempted.  */
  TD_PRI_INHERIT,		 /* Inherited elevated priority.  */
  TD_REAP,			 /* Reaped.  */
  TD_CONCURRENCY,		 /* Number of processes changing.  */
  TD_TIMEOUT,			 /* Conditional variable wait timed out.  */
  TD_MIN_EVENT_NUM = TD_READY,
  TD_MAX_EVENT_NUM = TD_TIMEOUT,
  TD_EVENTS_ENABLE = 31		/* Event reporting enabled.  */
} td_event_e;

/* Values representing the different ways events are reported.  */
typedef enum
{
  NOTIFY_BPT,			/* User must insert breakpoint at u.bptaddr. */
  NOTIFY_AUTOBPT,		/* Breakpoint at u.bptaddr is automatically
				   inserted.  */
  NOTIFY_SYSCALL		/* System call u.syscallno will be invoked.  */
} td_notify_e;

/* Description how event type is reported.  */
typedef struct td_notify
{
  td_notify_e type;		/* Way the event is reported.  */
  union
  {
    psaddr_t bptaddr;		/* Address of breakpoint.  */
    int syscallno;		/* Number of system call used.  */
  } u;
} td_notify_t;

/* Some people still have libc5 or old glibc with no uintptr_t.
   They lose.  glibc 2.1.3 was released on 2000-02-25, and it has
   uintptr_t, so it's reasonable to force these people to upgrade.  */

#ifndef HAVE_UINTPTR_T
#error No uintptr_t available; your C library is too old.
/* Inhibit further compilation errors after this error.  */
#define uintptr_t void *
#endif

/* Structure used to report event.  */
typedef struct td_event_msg
{
  td_event_e event;		/* Event type being reported.  */
  const td_thrhandle_t *th_p;	/* Thread reporting the event.  */
  union
  {
#if 0
    td_synchandle_t *sh;	/* Handle of synchronization object.  */
#endif
    uintptr_t data;		/* Event specific data.  */
  } msg;
} td_event_msg_t;

/* Structure containing event data available in each thread structure.  */
typedef struct
{
  td_thr_events_t eventmask;	/* Mask of enabled events.  */
  td_event_e eventnum;		/* Number of last event.  */
  void *eventdata;		/* Data associated with event.  */
} td_eventbuf_t;


/* Gathered statistics about the process.  */
typedef struct td_ta_stats
{
  int nthreads;       		/* Total number of threads in use.  */
  int r_concurrency;		/* Concurrency level requested by user.  */
  int nrunnable_num;		/* Average runnable threads, numerator.  */
  int nrunnable_den;		/* Average runnable threads, denominator.  */
  int a_concurrency_num;	/* Achieved concurrency level, numerator.  */
  int a_concurrency_den;	/* Achieved concurrency level, denominator.  */
  int nlwps_num;		/* Average number of processes in use,
				   numerator.  */
  int nlwps_den;		/* Average number of processes in use,
				   denominator.  */
  int nidle_num;		/* Average number of idling processes,
				   numerator.  */
  int nidle_den;		/* Average number of idling processes,
				   denominator.  */
} td_ta_stats_t;


/* Since Sun's library is based on Solaris threads we have to define a few
   types to map them to POSIX threads.  */
typedef pthread_t thread_t;
typedef pthread_key_t thread_key_t;


/* Callback for iteration over threads.  */
typedef int td_thr_iter_f (const td_thrhandle_t *, void *);

/* Callback for iteration over thread local data.  */
typedef int td_key_iter_f (thread_key_t, void (*) (void *), void *);



/* Forward declaration.  This has to be defined by the user.  */
struct ps_prochandle;


/* Information about the thread.  */
typedef struct td_thrinfo
{
  td_thragent_t *ti_ta_p;		/* Process handle.  */
  unsigned int ti_user_flags;		/* Unused.  */
  thread_t ti_tid;			/* Thread ID returned by
					   pthread_create().  */
  char *ti_tls;				/* Pointer to thread-local data.  */
  psaddr_t ti_startfunc;		/* Start function passed to
					   pthread_create().  */
  psaddr_t ti_stkbase;			/* Base of thread's stack.  */
  long int ti_stksize;			/* Size of thread's stack.  */
  psaddr_t ti_ro_area;			/* Unused.  */
  int ti_ro_size;			/* Unused.  */
  td_thr_state_e ti_state;		/* Thread state.  */
  unsigned char ti_db_suspended;	/* Nonzero if suspended by debugger. */
  td_thr_type_e ti_type;		/* Type of the thread (system vs
					   user thread).  */
  intptr_t ti_pc;			/* Unused.  */
  intptr_t ti_sp;			/* Unused.  */
  short int ti_flags;			/* Unused.  */
  int ti_pri;				/* Thread priority.  */
  lwpid_t ti_lid;			/* Unused.  */
  sigset_t ti_sigmask;			/* Signal mask.  */
  unsigned char ti_traceme;		/* Nonzero if event reporting
					   enabled.  */
  unsigned char ti_preemptflag;		/* Unused.  */
  unsigned char ti_pirecflag;		/* Unused.  */
  sigset_t ti_pending;			/* Set of pending signals.  */
  td_thr_events_t ti_events;		/* Set of enabled events.  */
} td_thrinfo_t;



/* Prototypes for exported library functions.  */

/* Initialize the thread debug support library.  */
extern td_err_e td_init (void);

/* Historical relict.  Should not be used anymore.  */
extern td_err_e td_log (void);

/* Generate new thread debug library handle for process PS.  */
extern td_err_e td_ta_new (struct ps_prochandle *__ps, td_thragent_t **__ta);

/* Free resources allocated for TA.  */
extern td_err_e td_ta_delete (td_thragent_t *__ta);

/* Get number of currently running threads in process associated with TA.  */
extern td_err_e td_ta_get_nthreads (const td_thragent_t *__ta, int *__np);

/* Return process handle passed in `td_ta_new' for process associated with
   TA.  */
extern td_err_e td_ta_get_ph (const td_thragent_t *__ta,
			      struct ps_prochandle **__ph);

/* Map thread library handle PT to thread debug library handle for process
   associated with TA and store result in *TH.  */
extern td_err_e td_ta_map_id2thr (const td_thragent_t *__ta, pthread_t __pt,
				  td_thrhandle_t *__th);

/* Map process ID LWPID to thread debug library handle for process
   associated with TA and store result in *TH.  */
extern td_err_e td_ta_map_lwp2thr (const td_thragent_t *__ta, lwpid_t __lwpid,
				   td_thrhandle_t *__th);


/* Call for each thread in a process associated with TA the callback function
   CALLBACK.  */
extern td_err_e td_ta_thr_iter (const td_thragent_t *__ta,
				td_thr_iter_f *__callback, void *__cbdata_p,
				td_thr_state_e __state, int __ti_pri,
				sigset_t *__ti_sigmask_p,
				unsigned int __ti_user_flags);

/* Call for each defined thread local data entry the callback function KI.  */
extern td_err_e td_ta_tsd_iter (const td_thragent_t *__ta, td_key_iter_f *__ki,
				void *__p);


/* Get event address for EVENT.  */
extern td_err_e td_ta_event_addr (const td_thragent_t *__ta,
				  td_event_e __event, td_notify_t *__ptr);

/* Enable EVENT in global mask.  */
extern td_err_e td_ta_set_event (const td_thragent_t *__ta,
				 td_thr_events_t *__event);

/* Disable EVENT in global mask.  */
extern td_err_e td_ta_clear_event (const td_thragent_t *__ta,
				   td_thr_events_t *__event);

/* Return information about last event.  */
extern td_err_e td_ta_event_getmsg (const td_thragent_t *__ta,
				    td_event_msg_t *msg);


/* Set suggested concurrency level for process associated with TA.  */
extern td_err_e td_ta_setconcurrency (const td_thragent_t *__ta, int __level);


/* Enable collecting statistics for process associated with TA.  */
extern td_err_e td_ta_enable_stats (const td_thragent_t *__ta, int __enable);

/* Reset statistics.  */
extern td_err_e td_ta_reset_stats (const td_thragent_t *__ta);

/* Retrieve statistics from process associated with TA.  */
extern td_err_e td_ta_get_stats (const td_thragent_t *__ta,
				 td_ta_stats_t *__statsp);


/* Validate that TH is a thread handle.  */
extern td_err_e td_thr_validate (const td_thrhandle_t *__th);

/* Return information about thread TH.  */
extern td_err_e td_thr_get_info (const td_thrhandle_t *__th,
				 td_thrinfo_t *__infop);

/* Retrieve floating-point register contents of process running thread TH.  */
extern td_err_e td_thr_getfpregs (const td_thrhandle_t *__th,
				  prfpregset_t *__regset);

/* Retrieve general register contents of process running thread TH.  */
extern td_err_e td_thr_getgregs (const td_thrhandle_t *__th,
				 prgregset_t __gregs);

/* Retrieve extended register contents of process running thread TH.  */
extern td_err_e td_thr_getxregs (const td_thrhandle_t *__th, void *__xregs);

/* Get size of extended register set of process running thread TH.  */
extern td_err_e td_thr_getxregsize (const td_thrhandle_t *__th, int *__sizep);

/* Set floating-point register contents of process running thread TH.  */
extern td_err_e td_thr_setfpregs (const td_thrhandle_t *__th,
				  const prfpregset_t *__fpregs);

/* Set general register contents of process running thread TH.  */
extern td_err_e td_thr_setgregs (const td_thrhandle_t *__th,
				 prgregset_t __gregs);

/* Set extended register contents of process running thread TH.  */
extern td_err_e td_thr_setxregs (const td_thrhandle_t *__th,
				 const void *__addr);


/* Enable reporting for EVENT for thread TH.  */
extern td_err_e td_thr_event_enable (const td_thrhandle_t *__th, int __event);

/* Enable EVENT for thread TH.  */
extern td_err_e td_thr_set_event (const td_thrhandle_t *__th,
				  td_thr_events_t *__event);

/* Disable EVENT for thread TH.  */
extern td_err_e td_thr_clear_event (const td_thrhandle_t *__th,
				    td_thr_events_t *__event);

/* Get event message for thread TH.  */
extern td_err_e td_thr_event_getmsg (const td_thrhandle_t *__th,
				     td_event_msg_t *__msg);


/* Set priority of thread TH.  */
extern td_err_e td_thr_setprio (const td_thrhandle_t *__th, int __prio);


/* Set pending signals for thread TH.  */
extern td_err_e td_thr_setsigpending (const td_thrhandle_t *__th,
				      unsigned char __n, const sigset_t *__ss);

/* Set signal mask for thread TH.  */
extern td_err_e td_thr_sigsetmask (const td_thrhandle_t *__th,
				   const sigset_t *__ss);


/* Return thread local data associated with key TK in thread TH.  */
extern td_err_e td_thr_tsd (const td_thrhandle_t *__th,
			    const thread_key_t __tk, void **__data);


/* Suspend execution of thread TH.  */
extern td_err_e td_thr_dbsuspend (const td_thrhandle_t *__th);

/* Resume execution of thread TH.  */
extern td_err_e td_thr_dbresume (const td_thrhandle_t *__th);

#endif	/* thread_db.h */

#endif /* HAVE_THREAD_DB_H */

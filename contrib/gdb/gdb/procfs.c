/* Machine independent support for SVR4 /proc (process file system) for GDB.

   Copyright 1999, 2000, 2001, 2002, 2003 Free Software Foundation,
   Inc.

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

#include "defs.h"
#include "inferior.h"
#include "target.h"
#include "gdbcore.h"
#include "elf-bfd.h"		/* for elfcore_write_* */
#include "gdbcmd.h"
#include "gdbthread.h"

#if defined (NEW_PROC_API)
#define _STRUCTURED_PROC 1	/* Should be done by configure script. */
#endif

#include <sys/procfs.h>
#ifdef HAVE_SYS_FAULT_H
#include <sys/fault.h>
#endif
#ifdef HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif
#include <sys/errno.h>
#include "gdb_wait.h"
#include <signal.h>
#include <ctype.h>
#include "gdb_string.h"
#include "gdb_assert.h"
#include "inflow.h"
#include "auxv.h"

/*
 * PROCFS.C
 *
 * This module provides the interface between GDB and the
 * /proc file system, which is used on many versions of Unix
 * as a means for debuggers to control other processes.
 * Examples of the systems that use this interface are:
 *   Irix
 *   Solaris
 *   OSF
 *   Unixware
 *   AIX5
 *
 * /proc works by imitating a file system: you open a simulated file
 * that represents the process you wish to interact with, and
 * perform operations on that "file" in order to examine or change
 * the state of the other process.
 *
 * The most important thing to know about /proc and this module
 * is that there are two very different interfaces to /proc:
 *   One that uses the ioctl system call, and
 *   another that uses read and write system calls.
 * This module has to support both /proc interfaces.  This means
 * that there are two different ways of doing every basic operation.
 *
 * In order to keep most of the code simple and clean, I have
 * defined an interface "layer" which hides all these system calls.
 * An ifdef (NEW_PROC_API) determines which interface we are using,
 * and most or all occurrances of this ifdef should be confined to
 * this interface layer.
 */


/* Determine which /proc API we are using:
   The ioctl API defines PIOCSTATUS, while
   the read/write (multiple fd) API never does.  */

#ifdef NEW_PROC_API
#include <sys/types.h>
#include "gdb_dirent.h"	/* opendir/readdir, for listing the LWP's */
#endif

#include <fcntl.h>	/* for O_RDONLY */
#include <unistd.h>	/* for "X_OK" */
#include "gdb_stat.h"	/* for struct stat */

/* Note: procfs-utils.h must be included after the above system header
   files, because it redefines various system calls using macros.
   This may be incompatible with the prototype declarations.  */

#include "proc-utils.h"

/* Prototypes for supply_gregset etc. */
#include "gregset.h"

/* =================== TARGET_OPS "MODULE" =================== */

/*
 * This module defines the GDB target vector and its methods.
 */

static void procfs_open (char *, int);
static void procfs_attach (char *, int);
static void procfs_detach (char *, int);
static void procfs_resume (ptid_t, int, enum target_signal);
static int procfs_can_run (void);
static void procfs_stop (void);
static void procfs_files_info (struct target_ops *);
static void procfs_fetch_registers (int);
static void procfs_store_registers (int);
static void procfs_notice_signals (ptid_t);
static void procfs_prepare_to_store (void);
static void procfs_kill_inferior (void);
static void procfs_mourn_inferior (void);
static void procfs_create_inferior (char *, char *, char **);
static ptid_t procfs_wait (ptid_t, struct target_waitstatus *);
static int procfs_xfer_memory (CORE_ADDR, char *, int, int,
			       struct mem_attrib *attrib,
			       struct target_ops *);
static LONGEST procfs_xfer_partial (struct target_ops *ops,
				    enum target_object object,
				    const char *annex,
				    void *readbuf, const void *writebuf,
				    ULONGEST offset, LONGEST len);

static int procfs_thread_alive (ptid_t);

void procfs_find_new_threads (void);
char *procfs_pid_to_str (ptid_t);

static int proc_find_memory_regions (int (*) (CORE_ADDR,
					      unsigned long,
					      int, int, int,
					      void *),
				     void *);

static char * procfs_make_note_section (bfd *, int *);

static int procfs_can_use_hw_breakpoint (int, int, int);

struct target_ops procfs_ops;		/* the target vector */

static void
init_procfs_ops (void)
{
  procfs_ops.to_shortname           = "procfs";
  procfs_ops.to_longname            = "Unix /proc child process";
  procfs_ops.to_doc                 =
    "Unix /proc child process (started by the \"run\" command).";
  procfs_ops.to_open                = procfs_open;
  procfs_ops.to_can_run             = procfs_can_run;
  procfs_ops.to_create_inferior     = procfs_create_inferior;
  procfs_ops.to_kill                = procfs_kill_inferior;
  procfs_ops.to_mourn_inferior      = procfs_mourn_inferior;
  procfs_ops.to_attach              = procfs_attach;
  procfs_ops.to_detach              = procfs_detach;
  procfs_ops.to_wait                = procfs_wait;
  procfs_ops.to_resume              = procfs_resume;
  procfs_ops.to_prepare_to_store    = procfs_prepare_to_store;
  procfs_ops.to_fetch_registers     = procfs_fetch_registers;
  procfs_ops.to_store_registers     = procfs_store_registers;
  procfs_ops.to_xfer_partial        = procfs_xfer_partial;
  procfs_ops.to_xfer_memory         = procfs_xfer_memory;
  procfs_ops.to_insert_breakpoint   =  memory_insert_breakpoint;
  procfs_ops.to_remove_breakpoint   =  memory_remove_breakpoint;
  procfs_ops.to_notice_signals      = procfs_notice_signals;
  procfs_ops.to_files_info          = procfs_files_info;
  procfs_ops.to_stop                = procfs_stop;

  procfs_ops.to_terminal_init       = terminal_init_inferior;
  procfs_ops.to_terminal_inferior   = terminal_inferior;
  procfs_ops.to_terminal_ours_for_output = terminal_ours_for_output;
  procfs_ops.to_terminal_ours       = terminal_ours;
  procfs_ops.to_terminal_save_ours  = terminal_save_ours;
  procfs_ops.to_terminal_info       = child_terminal_info;

  procfs_ops.to_find_new_threads    = procfs_find_new_threads;
  procfs_ops.to_thread_alive        = procfs_thread_alive;
  procfs_ops.to_pid_to_str          = procfs_pid_to_str;

  procfs_ops.to_has_all_memory      = 1;
  procfs_ops.to_has_memory          = 1;
  procfs_ops.to_has_execution       = 1;
  procfs_ops.to_has_stack           = 1;
  procfs_ops.to_has_registers       = 1;
  procfs_ops.to_stratum             = process_stratum;
  procfs_ops.to_has_thread_control  = tc_schedlock;
  procfs_ops.to_find_memory_regions = proc_find_memory_regions;
  procfs_ops.to_make_corefile_notes = procfs_make_note_section;
  procfs_ops.to_can_use_hw_breakpoint = procfs_can_use_hw_breakpoint;
  procfs_ops.to_magic               = OPS_MAGIC;
}

/* =================== END, TARGET_OPS "MODULE" =================== */

/*
 * World Unification:
 *
 * Put any typedefs, defines etc. here that are required for
 * the unification of code that handles different versions of /proc.
 */

#ifdef NEW_PROC_API		/* Solaris 7 && 8 method for watchpoints */
#ifdef WA_READ
     enum { READ_WATCHFLAG  = WA_READ,
	    WRITE_WATCHFLAG = WA_WRITE,
	    EXEC_WATCHFLAG  = WA_EXEC,
	    AFTER_WATCHFLAG = WA_TRAPAFTER
     };
#endif
#else				/* Irix method for watchpoints */
     enum { READ_WATCHFLAG  = MA_READ,
	    WRITE_WATCHFLAG = MA_WRITE,
	    EXEC_WATCHFLAG  = MA_EXEC,
	    AFTER_WATCHFLAG = 0		/* trapafter not implemented */
     };
#endif

/* gdb_sigset_t */
#ifdef HAVE_PR_SIGSET_T
typedef pr_sigset_t gdb_sigset_t;
#else
typedef sigset_t gdb_sigset_t;
#endif

/* sigaction */
#ifdef HAVE_PR_SIGACTION64_T
typedef pr_sigaction64_t gdb_sigaction_t;
#else
typedef struct sigaction gdb_sigaction_t;
#endif

/* siginfo */
#ifdef HAVE_PR_SIGINFO64_T
typedef pr_siginfo64_t gdb_siginfo_t;
#else
typedef struct siginfo gdb_siginfo_t;
#endif

/* gdb_premptysysset */
#ifdef premptysysset
#define gdb_premptysysset premptysysset
#else
#define gdb_premptysysset premptyset
#endif

/* praddsysset */
#ifdef praddsysset
#define gdb_praddsysset praddsysset
#else
#define gdb_praddsysset praddset
#endif

/* prdelsysset */
#ifdef prdelsysset
#define gdb_prdelsysset prdelsysset
#else
#define gdb_prdelsysset prdelset
#endif

/* prissyssetmember */
#ifdef prissyssetmember
#define gdb_pr_issyssetmember prissyssetmember
#else
#define gdb_pr_issyssetmember prismember
#endif

/* As a feature test, saying ``#if HAVE_PRSYSENT_T'' everywhere isn't
   as intuitively descriptive as it could be, so we'll define
   DYNAMIC_SYSCALLS to mean the same thing.  Anyway, at the time of
   this writing, this feature is only found on AIX5 systems and
   basically means that the set of syscalls is not fixed.  I.e,
   there's no nice table that one can #include to get all of the
   syscall numbers.  Instead, they're stored in /proc/PID/sysent
   for each process.  We are at least guaranteed that they won't
   change over the lifetime of the process.  But each process could
   (in theory) have different syscall numbers.
*/
#ifdef HAVE_PRSYSENT_T
#define DYNAMIC_SYSCALLS
#endif



/* =================== STRUCT PROCINFO "MODULE" =================== */

     /* FIXME: this comment will soon be out of date W.R.T. threads.  */

/* The procinfo struct is a wrapper to hold all the state information
   concerning a /proc process.  There should be exactly one procinfo
   for each process, and since GDB currently can debug only one
   process at a time, that means there should be only one procinfo.
   All of the LWP's of a process can be accessed indirectly thru the
   single process procinfo.

   However, against the day when GDB may debug more than one process,
   this data structure is kept in a list (which for now will hold no
   more than one member), and many functions will have a pointer to a
   procinfo as an argument.

   There will be a separate procinfo structure for use by the (not yet
   implemented) "info proc" command, so that we can print useful
   information about any random process without interfering with the
   inferior's procinfo information. */

#ifdef NEW_PROC_API
/* format strings for /proc paths */
# ifndef CTL_PROC_NAME_FMT
#  define MAIN_PROC_NAME_FMT   "/proc/%d"
#  define CTL_PROC_NAME_FMT    "/proc/%d/ctl"
#  define AS_PROC_NAME_FMT     "/proc/%d/as"
#  define MAP_PROC_NAME_FMT    "/proc/%d/map"
#  define STATUS_PROC_NAME_FMT "/proc/%d/status"
#  define MAX_PROC_NAME_SIZE sizeof("/proc/99999/lwp/8096/lstatus")
# endif
/* the name of the proc status struct depends on the implementation */
typedef pstatus_t   gdb_prstatus_t;
typedef lwpstatus_t gdb_lwpstatus_t;
#else /* ! NEW_PROC_API */
/* format strings for /proc paths */
# ifndef CTL_PROC_NAME_FMT
#  define MAIN_PROC_NAME_FMT   "/proc/%05d"
#  define CTL_PROC_NAME_FMT    "/proc/%05d"
#  define AS_PROC_NAME_FMT     "/proc/%05d"
#  define MAP_PROC_NAME_FMT    "/proc/%05d"
#  define STATUS_PROC_NAME_FMT "/proc/%05d"
#  define MAX_PROC_NAME_SIZE sizeof("/proc/ttttppppp")
# endif
/* the name of the proc status struct depends on the implementation */
typedef prstatus_t gdb_prstatus_t;
typedef prstatus_t gdb_lwpstatus_t;
#endif /* NEW_PROC_API */

typedef struct procinfo {
  struct procinfo *next;
  int pid;			/* Process ID    */
  int tid;			/* Thread/LWP id */

  /* process state */
  int was_stopped;
  int ignore_next_sigstop;

  /* The following four fd fields may be identical, or may contain
     several different fd's, depending on the version of /proc
     (old ioctl or new read/write).  */

  int ctl_fd;			/* File descriptor for /proc control file */
  /*
   * The next three file descriptors are actually only needed in the
   * read/write, multiple-file-descriptor implemenation (NEW_PROC_API).
   * However, to avoid a bunch of #ifdefs in the code, we will use
   * them uniformly by (in the case of the ioctl single-file-descriptor
   * implementation) filling them with copies of the control fd.
   */
  int status_fd;		/* File descriptor for /proc status file */
  int as_fd;			/* File descriptor for /proc as file */

  char pathname[MAX_PROC_NAME_SIZE];	/* Pathname to /proc entry */

  fltset_t saved_fltset;	/* Saved traced hardware fault set */
  gdb_sigset_t saved_sigset;	/* Saved traced signal set */
  gdb_sigset_t saved_sighold;	/* Saved held signal set */
  sysset_t *saved_exitset;	/* Saved traced system call exit set */
  sysset_t *saved_entryset;	/* Saved traced system call entry set */

  gdb_prstatus_t prstatus;	/* Current process status info */

#ifndef NEW_PROC_API
  gdb_fpregset_t fpregset;	/* Current floating point registers */
#endif

#ifdef DYNAMIC_SYSCALLS
  int num_syscalls;		/* Total number of syscalls */
  char **syscall_names;		/* Syscall number to name map */
#endif

  struct procinfo *thread_list;

  int status_valid : 1;
  int gregs_valid  : 1;
  int fpregs_valid : 1;
  int threads_valid: 1;
} procinfo;

static char errmsg[128];	/* shared error msg buffer */

/* Function prototypes for procinfo module: */

static procinfo *find_procinfo_or_die (int pid, int tid);
static procinfo *find_procinfo (int pid, int tid);
static procinfo *create_procinfo (int pid, int tid);
static void destroy_procinfo (procinfo * p);
static void do_destroy_procinfo_cleanup (void *);
static void dead_procinfo (procinfo * p, char *msg, int killp);
static int open_procinfo_files (procinfo * p, int which);
static void close_procinfo_files (procinfo * p);
static int sysset_t_size (procinfo *p);
static sysset_t *sysset_t_alloc (procinfo * pi);
#ifdef DYNAMIC_SYSCALLS
static void load_syscalls (procinfo *pi);
static void free_syscalls (procinfo *pi);
static int find_syscall (procinfo *pi, char *name);
#endif /* DYNAMIC_SYSCALLS */

/* The head of the procinfo list: */
static procinfo * procinfo_list;

/*
 * Function: find_procinfo
 *
 * Search the procinfo list.
 *
 * Returns: pointer to procinfo, or NULL if not found.
 */

static procinfo *
find_procinfo (int pid, int tid)
{
  procinfo *pi;

  for (pi = procinfo_list; pi; pi = pi->next)
    if (pi->pid == pid)
      break;

  if (pi)
    if (tid)
      {
	/* Don't check threads_valid.  If we're updating the
	   thread_list, we want to find whatever threads are already
	   here.  This means that in general it is the caller's
	   responsibility to check threads_valid and update before
	   calling find_procinfo, if the caller wants to find a new
	   thread. */

	for (pi = pi->thread_list; pi; pi = pi->next)
	  if (pi->tid == tid)
	    break;
      }

  return pi;
}

/*
 * Function: find_procinfo_or_die
 *
 * Calls find_procinfo, but errors on failure.
 */

static procinfo *
find_procinfo_or_die (int pid, int tid)
{
  procinfo *pi = find_procinfo (pid, tid);

  if (pi == NULL)
    {
      if (tid)
	error ("procfs: couldn't find pid %d (kernel thread %d) in procinfo list.",
	       pid, tid);
      else
	error ("procfs: couldn't find pid %d in procinfo list.", pid);
    }
  return pi;
}

/* open_with_retry() is a wrapper for open().  The appropriate
   open() call is attempted; if unsuccessful, it will be retried as
   many times as needed for the EAGAIN and EINTR conditions.

   For other conditions, open_with_retry() will retry the open() a
   limited number of times.  In addition, a short sleep is imposed
   prior to retrying the open().  The reason for this sleep is to give
   the kernel a chance to catch up and create the file in question in
   the event that GDB "wins" the race to open a file before the kernel
   has created it.  */

static int
open_with_retry (const char *pathname, int flags)
{
  int retries_remaining, status;

  retries_remaining = 2;

  while (1)
    {
      status = open (pathname, flags);

      if (status >= 0 || retries_remaining == 0)
	break;
      else if (errno != EINTR && errno != EAGAIN)
	{
	  retries_remaining--;
	  sleep (1);
	}
    }

  return status;
}

/*
 * Function: open_procinfo_files
 *
 * Open the file descriptor for the process or LWP.
 * ifdef NEW_PROC_API, we only open the control file descriptor;
 * the others are opened lazily as needed.
 * else (if not NEW_PROC_API), there is only one real
 * file descriptor, but we keep multiple copies of it so that
 * the code that uses them does not have to be #ifdef'd.
 *
 * Return: file descriptor, or zero for failure.
 */

enum { FD_CTL, FD_STATUS, FD_AS };

static int
open_procinfo_files (procinfo *pi, int which)
{
#ifdef NEW_PROC_API
  char tmp[MAX_PROC_NAME_SIZE];
#endif
  int  fd;

  /*
   * This function is getting ALMOST long enough to break up into several.
   * Here is some rationale:
   *
   * NEW_PROC_API (Solaris 2.6, Solaris 2.7, Unixware):
   *   There are several file descriptors that may need to be open
   *   for any given process or LWP.  The ones we're intereted in are:
   *     - control	 (ctl)	  write-only	change the state
   *     - status	 (status) read-only	query the state
   *     - address space (as)     read/write	access memory
   *     - map           (map)    read-only     virtual addr map
   *   Most of these are opened lazily as they are needed.
   *   The pathnames for the 'files' for an LWP look slightly
   *   different from those of a first-class process:
   *     Pathnames for a process (<proc-id>):
   *       /proc/<proc-id>/ctl
   *       /proc/<proc-id>/status
   *       /proc/<proc-id>/as
   *       /proc/<proc-id>/map
   *     Pathnames for an LWP (lwp-id):
   *       /proc/<proc-id>/lwp/<lwp-id>/lwpctl
   *       /proc/<proc-id>/lwp/<lwp-id>/lwpstatus
   *   An LWP has no map or address space file descriptor, since
   *   the memory map and address space are shared by all LWPs.
   *
   * Everyone else (Solaris 2.5, Irix, OSF)
   *   There is only one file descriptor for each process or LWP.
   *   For convenience, we copy the same file descriptor into all
   *   three fields of the procinfo struct (ctl_fd, status_fd, and
   *   as_fd, see NEW_PROC_API above) so that code that uses them
   *   doesn't need any #ifdef's.
   *     Pathname for all:
   *       /proc/<proc-id>
   *
   *   Solaris 2.5 LWP's:
   *     Each LWP has an independent file descriptor, but these
   *     are not obtained via the 'open' system call like the rest:
   *     instead, they're obtained thru an ioctl call (PIOCOPENLWP)
   *     to the file descriptor of the parent process.
   *
   *   OSF threads:
   *     These do not even have their own independent file descriptor.
   *     All operations are carried out on the file descriptor of the
   *     parent process.  Therefore we just call open again for each
   *     thread, getting a new handle for the same 'file'.
   */

#ifdef NEW_PROC_API
  /*
   * In this case, there are several different file descriptors that
   * we might be asked to open.  The control file descriptor will be
   * opened early, but the others will be opened lazily as they are
   * needed.
   */

  strcpy (tmp, pi->pathname);
  switch (which) {	/* which file descriptor to open? */
  case FD_CTL:
    if (pi->tid)
      strcat (tmp, "/lwpctl");
    else
      strcat (tmp, "/ctl");
    fd = open_with_retry (tmp, O_WRONLY);
    if (fd <= 0)
      return 0;		/* fail */
    pi->ctl_fd = fd;
    break;
  case FD_AS:
    if (pi->tid)
      return 0;		/* there is no 'as' file descriptor for an lwp */
    strcat (tmp, "/as");
    fd = open_with_retry (tmp, O_RDWR);
    if (fd <= 0)
      return 0;		/* fail */
    pi->as_fd = fd;
    break;
  case FD_STATUS:
    if (pi->tid)
      strcat (tmp, "/lwpstatus");
    else
      strcat (tmp, "/status");
    fd = open_with_retry (tmp, O_RDONLY);
    if (fd <= 0)
      return 0;		/* fail */
    pi->status_fd = fd;
    break;
  default:
    return 0;		/* unknown file descriptor */
  }
#else  /* not NEW_PROC_API */
  /*
   * In this case, there is only one file descriptor for each procinfo
   * (ie. each process or LWP).  In fact, only the file descriptor for
   * the process can actually be opened by an 'open' system call.
   * The ones for the LWPs have to be obtained thru an IOCTL call
   * on the process's file descriptor.
   *
   * For convenience, we copy each procinfo's single file descriptor
   * into all of the fields occupied by the several file descriptors
   * of the NEW_PROC_API implementation.  That way, the code that uses
   * them can be written without ifdefs.
   */


#ifdef PIOCTSTATUS	/* OSF */
  /* Only one FD; just open it. */
  if ((fd = open_with_retry (pi->pathname, O_RDWR)) == 0)
    return 0;
#else			/* Sol 2.5, Irix, other? */
  if (pi->tid == 0)	/* Master procinfo for the process */
    {
      fd = open_with_retry (pi->pathname, O_RDWR);
      if (fd <= 0)
	return 0;	/* fail */
    }
  else			/* LWP thread procinfo */
    {
#ifdef PIOCOPENLWP	/* Sol 2.5, thread/LWP */
      procinfo *process;
      int lwpid = pi->tid;

      /* Find the procinfo for the entire process. */
      if ((process = find_procinfo (pi->pid, 0)) == NULL)
	return 0;	/* fail */

      /* Now obtain the file descriptor for the LWP. */
      if ((fd = ioctl (process->ctl_fd, PIOCOPENLWP, &lwpid)) <= 0)
	return 0;	/* fail */
#else			/* Irix, other? */
      return 0;		/* Don't know how to open threads */
#endif	/* Sol 2.5 PIOCOPENLWP */
    }
#endif	/* OSF     PIOCTSTATUS */
  pi->ctl_fd = pi->as_fd = pi->status_fd = fd;
#endif	/* NEW_PROC_API */

  return 1;		/* success */
}

/*
 * Function: create_procinfo
 *
 * Allocate a data structure and link it into the procinfo list.
 * (First tries to find a pre-existing one (FIXME: why?)
 *
 * Return: pointer to new procinfo struct.
 */

static procinfo *
create_procinfo (int pid, int tid)
{
  procinfo *pi, *parent;

  if ((pi = find_procinfo (pid, tid)))
    return pi;			/* Already exists, nothing to do. */

  /* find parent before doing malloc, to save having to cleanup */
  if (tid != 0)
    parent = find_procinfo_or_die (pid, 0);	/* FIXME: should I
						   create it if it
						   doesn't exist yet? */

  pi = (procinfo *) xmalloc (sizeof (procinfo));
  memset (pi, 0, sizeof (procinfo));
  pi->pid = pid;
  pi->tid = tid;

#ifdef DYNAMIC_SYSCALLS
  load_syscalls (pi);
#endif

  pi->saved_entryset = sysset_t_alloc (pi);
  pi->saved_exitset = sysset_t_alloc (pi);

  /* Chain into list.  */
  if (tid == 0)
    {
      sprintf (pi->pathname, MAIN_PROC_NAME_FMT, pid);
      pi->next = procinfo_list;
      procinfo_list = pi;
    }
  else
    {
#ifdef NEW_PROC_API
      sprintf (pi->pathname, "/proc/%05d/lwp/%d", pid, tid);
#else
      sprintf (pi->pathname, MAIN_PROC_NAME_FMT, pid);
#endif
      pi->next = parent->thread_list;
      parent->thread_list = pi;
    }
  return pi;
}

/*
 * Function: close_procinfo_files
 *
 * Close all file descriptors associated with the procinfo
 */

static void
close_procinfo_files (procinfo *pi)
{
  if (pi->ctl_fd > 0)
    close (pi->ctl_fd);
#ifdef NEW_PROC_API
  if (pi->as_fd > 0)
    close (pi->as_fd);
  if (pi->status_fd > 0)
    close (pi->status_fd);
#endif
  pi->ctl_fd = pi->as_fd = pi->status_fd = 0;
}

/*
 * Function: destroy_procinfo
 *
 * Destructor function.  Close, unlink and deallocate the object.
 */

static void
destroy_one_procinfo (procinfo **list, procinfo *pi)
{
  procinfo *ptr;

  /* Step one: unlink the procinfo from its list */
  if (pi == *list)
    *list = pi->next;
  else
    for (ptr = *list; ptr; ptr = ptr->next)
      if (ptr->next == pi)
	{
	  ptr->next =  pi->next;
	  break;
	}

  /* Step two: close any open file descriptors */
  close_procinfo_files (pi);

  /* Step three: free the memory. */
#ifdef DYNAMIC_SYSCALLS
  free_syscalls (pi);
#endif
  xfree (pi->saved_entryset);
  xfree (pi->saved_exitset);
  xfree (pi);
}

static void
destroy_procinfo (procinfo *pi)
{
  procinfo *tmp;

  if (pi->tid != 0)	/* destroy a thread procinfo */
    {
      tmp = find_procinfo (pi->pid, 0);	/* find the parent process */
      destroy_one_procinfo (&tmp->thread_list, pi);
    }
  else			/* destroy a process procinfo and all its threads */
    {
      /* First destroy the children, if any; */
      while (pi->thread_list != NULL)
	destroy_one_procinfo (&pi->thread_list, pi->thread_list);
      /* Then destroy the parent.  Genocide!!!  */
      destroy_one_procinfo (&procinfo_list, pi);
    }
}

static void
do_destroy_procinfo_cleanup (void *pi)
{
  destroy_procinfo (pi);
}

enum { NOKILL, KILL };

/*
 * Function: dead_procinfo
 *
 * To be called on a non_recoverable error for a procinfo.
 * Prints error messages, optionally sends a SIGKILL to the process,
 * then destroys the data structure.
 */

static void
dead_procinfo (procinfo *pi, char *msg, int kill_p)
{
  char procfile[80];

  if (pi->pathname)
    {
      print_sys_errmsg (pi->pathname, errno);
    }
  else
    {
      sprintf (procfile, "process %d", pi->pid);
      print_sys_errmsg (procfile, errno);
    }
  if (kill_p == KILL)
    kill (pi->pid, SIGKILL);

  destroy_procinfo (pi);
  error (msg);
}

/*
 * Function: sysset_t_size
 *
 * Returns the (complete) size of a sysset_t struct.  Normally, this
 * is just sizeof (syset_t), but in the case of Monterey/64, the actual
 * size of sysset_t isn't known until runtime.
 */

static int
sysset_t_size (procinfo * pi)
{
#ifndef DYNAMIC_SYSCALLS
  return sizeof (sysset_t);
#else
  return sizeof (sysset_t) - sizeof (uint64_t)
    + sizeof (uint64_t) * ((pi->num_syscalls + (8 * sizeof (uint64_t) - 1))
			   / (8 * sizeof (uint64_t)));
#endif
}

/* Function: sysset_t_alloc

   Allocate and (partially) initialize a sysset_t struct.  */

static sysset_t *
sysset_t_alloc (procinfo * pi)
{
  sysset_t *ret;
  int size = sysset_t_size (pi);
  ret = xmalloc (size);
#ifdef DYNAMIC_SYSCALLS
  ret->pr_size = (pi->num_syscalls + (8 * sizeof (uint64_t) - 1))
                 / (8 * sizeof (uint64_t));
#endif
  return ret;
}

#ifdef DYNAMIC_SYSCALLS

/* Function: load_syscalls

   Extract syscall numbers and names from /proc/<pid>/sysent.  Initialize
   pi->num_syscalls with the number of syscalls and pi->syscall_names
   with the names.  (Certain numbers may be skipped in which case the
   names for these numbers will be left as NULL.) */

#define MAX_SYSCALL_NAME_LENGTH 256
#define MAX_SYSCALLS 65536

static void
load_syscalls (procinfo *pi)
{
  char pathname[MAX_PROC_NAME_SIZE];
  int sysent_fd;
  prsysent_t header;
  prsyscall_t *syscalls;
  int i, size, maxcall;

  pi->num_syscalls = 0;
  pi->syscall_names = 0;

  /* Open the file descriptor for the sysent file */
  sprintf (pathname, "/proc/%d/sysent", pi->pid);
  sysent_fd = open_with_retry (pathname, O_RDONLY);
  if (sysent_fd < 0)
    {
      error ("load_syscalls: Can't open /proc/%d/sysent", pi->pid);
    }

  size = sizeof header - sizeof (prsyscall_t);
  if (read (sysent_fd, &header, size) != size)
    {
      error ("load_syscalls: Error reading /proc/%d/sysent", pi->pid);
    }

  if (header.pr_nsyscalls == 0)
    {
      error ("load_syscalls: /proc/%d/sysent contains no syscalls!", pi->pid);
    }

  size = header.pr_nsyscalls * sizeof (prsyscall_t);
  syscalls = xmalloc (size);

  if (read (sysent_fd, syscalls, size) != size)
    {
      xfree (syscalls);
      error ("load_syscalls: Error reading /proc/%d/sysent", pi->pid);
    }

  /* Find maximum syscall number.  This may not be the same as
     pr_nsyscalls since that value refers to the number of entries
     in the table.  (Also, the docs indicate that some system
     call numbers may be skipped.) */

  maxcall = syscalls[0].pr_number;

  for (i = 1; i <  header.pr_nsyscalls; i++)
    if (syscalls[i].pr_number > maxcall
        && syscalls[i].pr_nameoff > 0
	&& syscalls[i].pr_number < MAX_SYSCALLS)
      maxcall = syscalls[i].pr_number;

  pi->num_syscalls = maxcall+1;
  pi->syscall_names = xmalloc (pi->num_syscalls * sizeof (char *));

  for (i = 0; i < pi->num_syscalls; i++)
    pi->syscall_names[i] = NULL;

  /* Read the syscall names in */
  for (i = 0; i < header.pr_nsyscalls; i++)
    {
      char namebuf[MAX_SYSCALL_NAME_LENGTH];
      int nread;
      int callnum;

      if (syscalls[i].pr_number >= MAX_SYSCALLS
          || syscalls[i].pr_number < 0
	  || syscalls[i].pr_nameoff <= 0
	  || (lseek (sysent_fd, (off_t) syscalls[i].pr_nameoff, SEEK_SET)
                                       != (off_t) syscalls[i].pr_nameoff))
	continue;

      nread = read (sysent_fd, namebuf, sizeof namebuf);
      if (nread <= 0)
	continue;

      callnum = syscalls[i].pr_number;

      if (pi->syscall_names[callnum] != NULL)
	{
	  /* FIXME: Generate warning */
	  continue;
	}

      namebuf[nread-1] = '\0';
      size = strlen (namebuf) + 1;
      pi->syscall_names[callnum] = xmalloc (size);
      strncpy (pi->syscall_names[callnum], namebuf, size-1);
      pi->syscall_names[callnum][size-1] = '\0';
    }

  close (sysent_fd);
  xfree (syscalls);
}

/* Function: free_syscalls

   Free the space allocated for the syscall names from the procinfo
   structure.  */

static void
free_syscalls (procinfo *pi)
{
  if (pi->syscall_names)
    {
      int i;

      for (i = 0; i < pi->num_syscalls; i++)
	if (pi->syscall_names[i] != NULL)
	  xfree (pi->syscall_names[i]);

      xfree (pi->syscall_names);
      pi->syscall_names = 0;
    }
}

/* Function: find_syscall

   Given a name, look up (and return) the corresponding syscall number.
   If no match is found, return -1.  */

static int
find_syscall (procinfo *pi, char *name)
{
  int i;
  for (i = 0; i < pi->num_syscalls; i++)
    {
      if (pi->syscall_names[i] && strcmp (name, pi->syscall_names[i]) == 0)
	return i;
    }
  return -1;
}
#endif

/* =================== END, STRUCT PROCINFO "MODULE" =================== */

/* ===================  /proc  "MODULE" =================== */

/*
 * This "module" is the interface layer between the /proc system API
 * and the gdb target vector functions.  This layer consists of
 * access functions that encapsulate each of the basic operations
 * that we need to use from the /proc API.
 *
 * The main motivation for this layer is to hide the fact that
 * there are two very different implementations of the /proc API.
 * Rather than have a bunch of #ifdefs all thru the gdb target vector
 * functions, we do our best to hide them all in here.
 */

int proc_get_status (procinfo * pi);
long proc_flags (procinfo * pi);
int proc_why (procinfo * pi);
int proc_what (procinfo * pi);
int proc_set_run_on_last_close (procinfo * pi);
int proc_unset_run_on_last_close (procinfo * pi);
int proc_set_inherit_on_fork (procinfo * pi);
int proc_unset_inherit_on_fork (procinfo * pi);
int proc_set_async (procinfo * pi);
int proc_unset_async (procinfo * pi);
int proc_stop_process (procinfo * pi);
int proc_trace_signal (procinfo * pi, int signo);
int proc_ignore_signal (procinfo * pi, int signo);
int proc_clear_current_fault (procinfo * pi);
int proc_set_current_signal (procinfo * pi, int signo);
int proc_clear_current_signal (procinfo * pi);
int proc_set_gregs (procinfo * pi);
int proc_set_fpregs (procinfo * pi);
int proc_wait_for_stop (procinfo * pi);
int proc_run_process (procinfo * pi, int step, int signo);
int proc_kill (procinfo * pi, int signo);
int proc_parent_pid (procinfo * pi);
int proc_get_nthreads (procinfo * pi);
int proc_get_current_thread (procinfo * pi);
int proc_set_held_signals (procinfo * pi, gdb_sigset_t * sighold);
int proc_set_traced_sysexit (procinfo * pi, sysset_t * sysset);
int proc_set_traced_sysentry (procinfo * pi, sysset_t * sysset);
int proc_set_traced_faults (procinfo * pi, fltset_t * fltset);
int proc_set_traced_signals (procinfo * pi, gdb_sigset_t * sigset);

int proc_update_threads (procinfo * pi);
int proc_iterate_over_threads (procinfo * pi,
			       int (*func) (procinfo *, procinfo *, void *),
			       void *ptr);

gdb_gregset_t *proc_get_gregs (procinfo * pi);
gdb_fpregset_t *proc_get_fpregs (procinfo * pi);
sysset_t *proc_get_traced_sysexit (procinfo * pi, sysset_t * save);
sysset_t *proc_get_traced_sysentry (procinfo * pi, sysset_t * save);
fltset_t *proc_get_traced_faults (procinfo * pi, fltset_t * save);
gdb_sigset_t *proc_get_traced_signals (procinfo * pi, gdb_sigset_t * save);
gdb_sigset_t *proc_get_held_signals (procinfo * pi, gdb_sigset_t * save);
gdb_sigset_t *proc_get_pending_signals (procinfo * pi, gdb_sigset_t * save);
gdb_sigaction_t *proc_get_signal_actions (procinfo * pi, gdb_sigaction_t *save);

void proc_warn (procinfo * pi, char *func, int line);
void proc_error (procinfo * pi, char *func, int line);

void
proc_warn (procinfo *pi, char *func, int line)
{
  sprintf (errmsg, "procfs: %s line %d, %s", func, line, pi->pathname);
  print_sys_errmsg (errmsg, errno);
}

void
proc_error (procinfo *pi, char *func, int line)
{
  sprintf (errmsg, "procfs: %s line %d, %s", func, line, pi->pathname);
  perror_with_name (errmsg);
}

/*
 * Function: proc_get_status
 *
 * Updates the status struct in the procinfo.
 * There is a 'valid' flag, to let other functions know when
 * this function needs to be called (so the status is only
 * read when it is needed).  The status file descriptor is
 * also only opened when it is needed.
 *
 * Return: non-zero for success, zero for failure.
 */

int
proc_get_status (procinfo *pi)
{
  /* Status file descriptor is opened "lazily" */
  if (pi->status_fd == 0 &&
      open_procinfo_files (pi, FD_STATUS) == 0)
    {
      pi->status_valid = 0;
      return 0;
    }

#ifdef NEW_PROC_API
  if (lseek (pi->status_fd, 0, SEEK_SET) < 0)
    pi->status_valid = 0;			/* fail */
  else
    {
      /* Sigh... I have to read a different data structure,
	 depending on whether this is a main process or an LWP. */
      if (pi->tid)
	pi->status_valid = (read (pi->status_fd,
				  (char *) &pi->prstatus.pr_lwp,
				  sizeof (lwpstatus_t))
			    == sizeof (lwpstatus_t));
      else
	{
	  pi->status_valid = (read (pi->status_fd,
				    (char *) &pi->prstatus,
				    sizeof (gdb_prstatus_t))
			      == sizeof (gdb_prstatus_t));
#if 0 /*def UNIXWARE*/
	  if (pi->status_valid &&
	      (pi->prstatus.pr_lwp.pr_flags & PR_ISTOP) &&
	      pi->prstatus.pr_lwp.pr_why == PR_REQUESTED)
	    /* Unixware peculiarity -- read the damn thing again! */
	    pi->status_valid = (read (pi->status_fd,
				      (char *) &pi->prstatus,
				      sizeof (gdb_prstatus_t))
				== sizeof (gdb_prstatus_t));
#endif /* UNIXWARE */
	}
    }
#else	/* ioctl method */
#ifdef PIOCTSTATUS	/* osf */
  if (pi->tid == 0)	/* main process */
    {
      /* Just read the danged status.  Now isn't that simple? */
      pi->status_valid =
	(ioctl (pi->status_fd, PIOCSTATUS, &pi->prstatus) >= 0);
    }
  else
    {
      int win;
      struct {
	long pr_count;
	tid_t pr_error_thread;
	struct prstatus status;
      } thread_status;

      thread_status.pr_count = 1;
      thread_status.status.pr_tid = pi->tid;
      win = (ioctl (pi->status_fd, PIOCTSTATUS, &thread_status) >= 0);
      if (win)
	{
	  memcpy (&pi->prstatus, &thread_status.status,
		  sizeof (pi->prstatus));
	  pi->status_valid = 1;
	}
    }
#else
  /* Just read the danged status.  Now isn't that simple? */
  pi->status_valid = (ioctl (pi->status_fd, PIOCSTATUS, &pi->prstatus) >= 0);
#endif
#endif

  if (pi->status_valid)
    {
      PROC_PRETTYFPRINT_STATUS (proc_flags (pi),
				proc_why (pi),
				proc_what (pi),
				proc_get_current_thread (pi));
    }

  /* The status struct includes general regs, so mark them valid too */
  pi->gregs_valid  = pi->status_valid;
#ifdef NEW_PROC_API
  /* In the read/write multiple-fd model,
     the status struct includes the fp regs too, so mark them valid too */
  pi->fpregs_valid = pi->status_valid;
#endif
  return pi->status_valid;	/* True if success, false if failure. */
}

/*
 * Function: proc_flags
 *
 * returns the process flags (pr_flags field).
 */

long
proc_flags (procinfo *pi)
{
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return 0;	/* FIXME: not a good failure value (but what is?) */

#ifdef NEW_PROC_API
# ifdef UNIXWARE
  /* UnixWare 7.1 puts process status flags, e.g. PR_ASYNC, in
     pstatus_t and LWP status flags, e.g. PR_STOPPED, in lwpstatus_t.
     The two sets of flags don't overlap. */
  return pi->prstatus.pr_flags | pi->prstatus.pr_lwp.pr_flags;
# else
  return pi->prstatus.pr_lwp.pr_flags;
# endif
#else
  return pi->prstatus.pr_flags;
#endif
}

/*
 * Function: proc_why
 *
 * returns the pr_why field (why the process stopped).
 */

int
proc_why (procinfo *pi)
{
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return 0;	/* FIXME: not a good failure value (but what is?) */

#ifdef NEW_PROC_API
  return pi->prstatus.pr_lwp.pr_why;
#else
  return pi->prstatus.pr_why;
#endif
}

/*
 * Function: proc_what
 *
 * returns the pr_what field (details of why the process stopped).
 */

int
proc_what (procinfo *pi)
{
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return 0;	/* FIXME: not a good failure value (but what is?) */

#ifdef NEW_PROC_API
  return pi->prstatus.pr_lwp.pr_what;
#else
  return pi->prstatus.pr_what;
#endif
}

#ifndef PIOCSSPCACT	/* The following is not supported on OSF.  */
/*
 * Function: proc_nsysarg
 *
 * returns the pr_nsysarg field (number of args to the current syscall).
 */

int
proc_nsysarg (procinfo *pi)
{
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return 0;

#ifdef NEW_PROC_API
  return pi->prstatus.pr_lwp.pr_nsysarg;
#else
  return pi->prstatus.pr_nsysarg;
#endif
}

/*
 * Function: proc_sysargs
 *
 * returns the pr_sysarg field (pointer to the arguments of current syscall).
 */

long *
proc_sysargs (procinfo *pi)
{
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return NULL;

#ifdef NEW_PROC_API
  return (long *) &pi->prstatus.pr_lwp.pr_sysarg;
#else
  return (long *) &pi->prstatus.pr_sysarg;
#endif
}

/*
 * Function: proc_syscall
 *
 * returns the pr_syscall field (id of current syscall if we are in one).
 */

int
proc_syscall (procinfo *pi)
{
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return 0;

#ifdef NEW_PROC_API
  return pi->prstatus.pr_lwp.pr_syscall;
#else
  return pi->prstatus.pr_syscall;
#endif
}
#endif /* PIOCSSPCACT */

/*
 * Function: proc_cursig:
 *
 * returns the pr_cursig field (current signal).
 */

long
proc_cursig (struct procinfo *pi)
{
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return 0;	/* FIXME: not a good failure value (but what is?) */

#ifdef NEW_PROC_API
  return pi->prstatus.pr_lwp.pr_cursig;
#else
  return pi->prstatus.pr_cursig;
#endif
}

/*
 * Function: proc_modify_flag
 *
 *  === I appologize for the messiness of this function.
 *  === This is an area where the different versions of
 *  === /proc are more inconsistent than usual.     MVS
 *
 * Set or reset any of the following process flags:
 *    PR_FORK	-- forked child will inherit trace flags
 *    PR_RLC	-- traced process runs when last /proc file closed.
 *    PR_KLC    -- traced process is killed when last /proc file closed.
 *    PR_ASYNC	-- LWP's get to run/stop independently.
 *
 * There are three methods for doing this function:
 * 1) Newest: read/write [PCSET/PCRESET/PCUNSET]
 *    [Sol6, Sol7, UW]
 * 2) Middle: PIOCSET/PIOCRESET
 *    [Irix, Sol5]
 * 3) Oldest: PIOCSFORK/PIOCRFORK/PIOCSRLC/PIOCRRLC
 *    [OSF, Sol5]
 *
 * Note: Irix does not define PR_ASYNC.
 * Note: OSF  does not define PR_KLC.
 * Note: OSF  is the only one that can ONLY use the oldest method.
 *
 * Arguments:
 *    pi   -- the procinfo
 *    flag -- one of PR_FORK, PR_RLC, or PR_ASYNC
 *    mode -- 1 for set, 0 for reset.
 *
 * Returns non-zero for success, zero for failure.
 */

enum { FLAG_RESET, FLAG_SET };

static int
proc_modify_flag (procinfo *pi, long flag, long mode)
{
  long win = 0;		/* default to fail */

  /*
   * These operations affect the process as a whole, and applying
   * them to an individual LWP has the same meaning as applying them
   * to the main process.  Therefore, if we're ever called with a
   * pointer to an LWP's procinfo, let's substitute the process's
   * procinfo and avoid opening the LWP's file descriptor
   * unnecessarily.
   */

  if (pi->pid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API	/* Newest method: UnixWare and newer Solarii */
  /* First normalize the PCUNSET/PCRESET command opcode
     (which for no obvious reason has a different definition
     from one operating system to the next...)  */
#ifdef  PCUNSET
#define GDBRESET PCUNSET
#else
#ifdef  PCRESET
#define GDBRESET PCRESET
#endif
#endif
  {
    procfs_ctl_t arg[2];

    if (mode == FLAG_SET)	/* Set the flag (RLC, FORK, or ASYNC) */
      arg[0] = PCSET;
    else			/* Reset the flag */
      arg[0] = GDBRESET;

    arg[1] = flag;
    win = (write (pi->ctl_fd, (void *) &arg, sizeof (arg)) == sizeof (arg));
  }
#else
#ifdef PIOCSET		/* Irix/Sol5 method */
  if (mode == FLAG_SET)	/* Set the flag (hopefully RLC, FORK, or ASYNC) */
    {
      win = (ioctl (pi->ctl_fd, PIOCSET, &flag)   >= 0);
    }
  else			/* Reset the flag */
    {
      win = (ioctl (pi->ctl_fd, PIOCRESET, &flag) >= 0);
    }

#else
#ifdef PIOCSRLC		/* Oldest method: OSF */
  switch (flag) {
  case PR_RLC:
    if (mode == FLAG_SET)	/* Set run-on-last-close */
      {
	win = (ioctl (pi->ctl_fd, PIOCSRLC, NULL) >= 0);
      }
    else			/* Clear run-on-last-close */
      {
	win = (ioctl (pi->ctl_fd, PIOCRRLC, NULL) >= 0);
      }
    break;
  case PR_FORK:
    if (mode == FLAG_SET)	/* Set inherit-on-fork */
      {
	win = (ioctl (pi->ctl_fd, PIOCSFORK, NULL) >= 0);
      }
    else			/* Clear inherit-on-fork */
      {
	win = (ioctl (pi->ctl_fd, PIOCRFORK, NULL) >= 0);
      }
    break;
  default:
    win = 0;		/* fail -- unknown flag (can't do PR_ASYNC) */
    break;
  }
#endif
#endif
#endif
#undef GDBRESET
  /* The above operation renders the procinfo's cached pstatus obsolete. */
  pi->status_valid = 0;

  if (!win)
    warning ("procfs: modify_flag failed to turn %s %s",
	     flag == PR_FORK  ? "PR_FORK"  :
	     flag == PR_RLC   ? "PR_RLC"   :
#ifdef PR_ASYNC
	     flag == PR_ASYNC ? "PR_ASYNC" :
#endif
#ifdef PR_KLC
	     flag == PR_KLC   ? "PR_KLC"   :
#endif
	     "<unknown flag>",
	     mode == FLAG_RESET ? "off" : "on");

  return win;
}

/*
 * Function: proc_set_run_on_last_close
 *
 * Set the run_on_last_close flag.
 * Process with all threads will become runnable
 * when debugger closes all /proc fds.
 *
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_run_on_last_close (procinfo *pi)
{
  return proc_modify_flag (pi, PR_RLC, FLAG_SET);
}

/*
 * Function: proc_unset_run_on_last_close
 *
 * Reset the run_on_last_close flag.
 * Process will NOT become runnable
 * when debugger closes its file handles.
 *
 * Returns non-zero for success, zero for failure.
 */

int
proc_unset_run_on_last_close (procinfo *pi)
{
  return proc_modify_flag (pi, PR_RLC, FLAG_RESET);
}

#ifdef PR_KLC
/*
 * Function: proc_set_kill_on_last_close
 *
 * Set the kill_on_last_close flag.
 * Process with all threads will be killed when debugger
 * closes all /proc fds (or debugger exits or dies).
 *
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_kill_on_last_close (procinfo *pi)
{
  return proc_modify_flag (pi, PR_KLC, FLAG_SET);
}

/*
 * Function: proc_unset_kill_on_last_close
 *
 * Reset the kill_on_last_close flag.
 * Process will NOT be killed when debugger
 * closes its file handles (or exits or dies).
 *
 * Returns non-zero for success, zero for failure.
 */

int
proc_unset_kill_on_last_close (procinfo *pi)
{
  return proc_modify_flag (pi, PR_KLC, FLAG_RESET);
}
#endif /* PR_KLC */

/*
 * Function: proc_set_inherit_on_fork
 *
 * Set inherit_on_fork flag.
 * If the process forks a child while we are registered for events
 * in the parent, then we will also recieve events from the child.
 *
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_inherit_on_fork (procinfo *pi)
{
  return proc_modify_flag (pi, PR_FORK, FLAG_SET);
}

/*
 * Function: proc_unset_inherit_on_fork
 *
 * Reset inherit_on_fork flag.
 * If the process forks a child while we are registered for events
 * in the parent, then we will NOT recieve events from the child.
 *
 * Returns non-zero for success, zero for failure.
 */

int
proc_unset_inherit_on_fork (procinfo *pi)
{
  return proc_modify_flag (pi, PR_FORK, FLAG_RESET);
}

#ifdef PR_ASYNC
/*
 * Function: proc_set_async
 *
 * Set PR_ASYNC flag.
 * If one LWP stops because of a debug event (signal etc.),
 * the remaining LWPs will continue to run.
 *
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_async (procinfo *pi)
{
  return proc_modify_flag (pi, PR_ASYNC, FLAG_SET);
}

/*
 * Function: proc_unset_async
 *
 * Reset PR_ASYNC flag.
 * If one LWP stops because of a debug event (signal etc.),
 * then all other LWPs will stop as well.
 *
 * Returns non-zero for success, zero for failure.
 */

int
proc_unset_async (procinfo *pi)
{
  return proc_modify_flag (pi, PR_ASYNC, FLAG_RESET);
}
#endif /* PR_ASYNC */

/*
 * Function: proc_stop_process
 *
 * Request the process/LWP to stop.  Does not wait.
 * Returns non-zero for success, zero for failure.
 */

int
proc_stop_process (procinfo *pi)
{
  int win;

  /*
   * We might conceivably apply this operation to an LWP, and
   * the LWP's ctl file descriptor might not be open.
   */

  if (pi->ctl_fd == 0 &&
      open_procinfo_files (pi, FD_CTL) == 0)
    return 0;
  else
    {
#ifdef NEW_PROC_API
      procfs_ctl_t cmd = PCSTOP;
      win = (write (pi->ctl_fd, (char *) &cmd, sizeof (cmd)) == sizeof (cmd));
#else	/* ioctl method */
      win = (ioctl (pi->ctl_fd, PIOCSTOP, &pi->prstatus) >= 0);
      /* Note: the call also reads the prstatus.  */
      if (win)
	{
	  pi->status_valid = 1;
	  PROC_PRETTYFPRINT_STATUS (proc_flags (pi),
				    proc_why (pi),
				    proc_what (pi),
				    proc_get_current_thread (pi));
	}
#endif
    }

  return win;
}

/*
 * Function: proc_wait_for_stop
 *
 * Wait for the process or LWP to stop (block until it does).
 * Returns non-zero for success, zero for failure.
 */

int
proc_wait_for_stop (procinfo *pi)
{
  int win;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  {
    procfs_ctl_t cmd = PCWSTOP;
    win = (write (pi->ctl_fd, (char *) &cmd, sizeof (cmd)) == sizeof (cmd));
    /* We been runnin' and we stopped -- need to update status.  */
    pi->status_valid = 0;
  }
#else	/* ioctl method */
  win = (ioctl (pi->ctl_fd, PIOCWSTOP, &pi->prstatus) >= 0);
  /* Above call also refreshes the prstatus.  */
  if (win)
    {
      pi->status_valid = 1;
      PROC_PRETTYFPRINT_STATUS (proc_flags (pi),
				proc_why (pi),
				proc_what (pi),
				proc_get_current_thread (pi));
    }
#endif

  return win;
}

/*
 * Function: proc_run_process
 *
 * Make the process or LWP runnable.
 * Options (not all are implemented):
 *   - single-step
 *   - clear current fault
 *   - clear current signal
 *   - abort the current system call
 *   - stop as soon as finished with system call
 *   - (ioctl): set traced signal set
 *   - (ioctl): set held   signal set
 *   - (ioctl): set traced fault  set
 *   - (ioctl): set start pc (vaddr)
 * Always clear the current fault.
 * Clear the current signal if 'signo' is zero.
 *
 * Arguments:
 *   pi		the process or LWP to operate on.
 *   step	if true, set the process or LWP to trap after one instr.
 *   signo	if zero, clear the current signal if any.
 *		if non-zero, set the current signal to this one.
 *
 * Returns non-zero for success, zero for failure.
 */

int
proc_run_process (procinfo *pi, int step, int signo)
{
  int win;
  int runflags;

  /*
   * We will probably have to apply this operation to individual threads,
   * so make sure the control file descriptor is open.
   */

  if (pi->ctl_fd == 0 &&
      open_procinfo_files (pi, FD_CTL) == 0)
    {
      return 0;
    }

  runflags    = PRCFAULT;	/* always clear current fault  */
  if (step)
    runflags |= PRSTEP;
  if (signo == 0)
    runflags |= PRCSIG;
  else if (signo != -1)		/* -1 means do nothing W.R.T. signals */
    proc_set_current_signal (pi, signo);

#ifdef NEW_PROC_API
  {
    procfs_ctl_t cmd[2];

    cmd[0]  = PCRUN;
    cmd[1]  = runflags;
    win = (write (pi->ctl_fd, (char *) &cmd, sizeof (cmd)) == sizeof (cmd));
  }
#else	/* ioctl method */
  {
    prrun_t prrun;

    memset (&prrun, 0, sizeof (prrun));
    prrun.pr_flags  = runflags;
    win = (ioctl (pi->ctl_fd, PIOCRUN, &prrun) >= 0);
  }
#endif

  return win;
}

/*
 * Function: proc_set_traced_signals
 *
 * Register to trace signals in the process or LWP.
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_traced_signals (procinfo *pi, gdb_sigset_t *sigset)
{
  int win;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  {
    struct {
      procfs_ctl_t cmd;
      /* Use char array to avoid alignment issues.  */
      char sigset[sizeof (gdb_sigset_t)];
    } arg;

    arg.cmd = PCSTRACE;
    memcpy (&arg.sigset, sigset, sizeof (gdb_sigset_t));

    win = (write (pi->ctl_fd, (char *) &arg, sizeof (arg)) == sizeof (arg));
  }
#else	/* ioctl method */
  win = (ioctl (pi->ctl_fd, PIOCSTRACE, sigset) >= 0);
#endif
  /* The above operation renders the procinfo's cached pstatus obsolete. */
  pi->status_valid = 0;

  if (!win)
    warning ("procfs: set_traced_signals failed");
  return win;
}

/*
 * Function: proc_set_traced_faults
 *
 * Register to trace hardware faults in the process or LWP.
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_traced_faults (procinfo *pi, fltset_t *fltset)
{
  int win;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  {
    struct {
      procfs_ctl_t cmd;
      /* Use char array to avoid alignment issues.  */
      char fltset[sizeof (fltset_t)];
    } arg;

    arg.cmd = PCSFAULT;
    memcpy (&arg.fltset, fltset, sizeof (fltset_t));

    win = (write (pi->ctl_fd, (char *) &arg, sizeof (arg)) == sizeof (arg));
  }
#else	/* ioctl method */
  win = (ioctl (pi->ctl_fd, PIOCSFAULT, fltset) >= 0);
#endif
  /* The above operation renders the procinfo's cached pstatus obsolete. */
  pi->status_valid = 0;

  return win;
}

/*
 * Function: proc_set_traced_sysentry
 *
 * Register to trace entry to system calls in the process or LWP.
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_traced_sysentry (procinfo *pi, sysset_t *sysset)
{
  int win;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  {
    struct gdb_proc_ctl_pcsentry {
      procfs_ctl_t cmd;
      /* Use char array to avoid alignment issues.  */
      char sysset[sizeof (sysset_t)];
    } *argp;
    int argp_size = sizeof (struct gdb_proc_ctl_pcsentry)
                  - sizeof (sysset_t)
		  + sysset_t_size (pi);

    argp = xmalloc (argp_size);

    argp->cmd = PCSENTRY;
    memcpy (&argp->sysset, sysset, sysset_t_size (pi));

    win = (write (pi->ctl_fd, (char *) argp, argp_size) == argp_size);
    xfree (argp);
  }
#else	/* ioctl method */
  win = (ioctl (pi->ctl_fd, PIOCSENTRY, sysset) >= 0);
#endif
  /* The above operation renders the procinfo's cached pstatus obsolete. */
  pi->status_valid = 0;

  return win;
}

/*
 * Function: proc_set_traced_sysexit
 *
 * Register to trace exit from system calls in the process or LWP.
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_traced_sysexit (procinfo *pi, sysset_t *sysset)
{
  int win;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  {
    struct gdb_proc_ctl_pcsexit {
      procfs_ctl_t cmd;
      /* Use char array to avoid alignment issues.  */
      char sysset[sizeof (sysset_t)];
    } *argp;
    int argp_size = sizeof (struct gdb_proc_ctl_pcsexit)
                  - sizeof (sysset_t)
		  + sysset_t_size (pi);

    argp = xmalloc (argp_size);

    argp->cmd = PCSEXIT;
    memcpy (&argp->sysset, sysset, sysset_t_size (pi));

    win = (write (pi->ctl_fd, (char *) argp, argp_size) == argp_size);
    xfree (argp);
  }
#else	/* ioctl method */
  win = (ioctl (pi->ctl_fd, PIOCSEXIT, sysset) >= 0);
#endif
  /* The above operation renders the procinfo's cached pstatus obsolete. */
  pi->status_valid = 0;

  return win;
}

/*
 * Function: proc_set_held_signals
 *
 * Specify the set of blocked / held signals in the process or LWP.
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_held_signals (procinfo *pi, gdb_sigset_t *sighold)
{
  int win;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  {
    struct {
      procfs_ctl_t cmd;
      /* Use char array to avoid alignment issues.  */
      char hold[sizeof (gdb_sigset_t)];
    } arg;

    arg.cmd  = PCSHOLD;
    memcpy (&arg.hold, sighold, sizeof (gdb_sigset_t));
    win = (write (pi->ctl_fd, (void *) &arg, sizeof (arg)) == sizeof (arg));
  }
#else
  win = (ioctl (pi->ctl_fd, PIOCSHOLD, sighold) >= 0);
#endif
  /* The above operation renders the procinfo's cached pstatus obsolete. */
  pi->status_valid = 0;

  return win;
}

/*
 * Function: proc_get_pending_signals
 *
 * returns the set of signals that are pending in the process or LWP.
 * Will also copy the sigset if 'save' is non-zero.
 */

gdb_sigset_t *
proc_get_pending_signals (procinfo *pi, gdb_sigset_t *save)
{
  gdb_sigset_t *ret = NULL;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return NULL;

#ifdef NEW_PROC_API
  ret = &pi->prstatus.pr_lwp.pr_lwppend;
#else
  ret = &pi->prstatus.pr_sigpend;
#endif
  if (save && ret)
    memcpy (save, ret, sizeof (gdb_sigset_t));

  return ret;
}

/*
 * Function: proc_get_signal_actions
 *
 * returns the set of signal actions.
 * Will also copy the sigactionset if 'save' is non-zero.
 */

gdb_sigaction_t *
proc_get_signal_actions (procinfo *pi, gdb_sigaction_t *save)
{
  gdb_sigaction_t *ret = NULL;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return NULL;

#ifdef NEW_PROC_API
  ret = &pi->prstatus.pr_lwp.pr_action;
#else
  ret = &pi->prstatus.pr_action;
#endif
  if (save && ret)
    memcpy (save, ret, sizeof (gdb_sigaction_t));

  return ret;
}

/*
 * Function: proc_get_held_signals
 *
 * returns the set of signals that are held / blocked.
 * Will also copy the sigset if 'save' is non-zero.
 */

gdb_sigset_t *
proc_get_held_signals (procinfo *pi, gdb_sigset_t *save)
{
  gdb_sigset_t *ret = NULL;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return NULL;

#ifdef UNIXWARE
  ret = &pi->prstatus.pr_lwp.pr_context.uc_sigmask;
#else
  ret = &pi->prstatus.pr_lwp.pr_lwphold;
#endif /* UNIXWARE */
#else  /* not NEW_PROC_API */
  {
    static gdb_sigset_t sigheld;

    if (ioctl (pi->ctl_fd, PIOCGHOLD, &sigheld) >= 0)
      ret = &sigheld;
  }
#endif /* NEW_PROC_API */
  if (save && ret)
    memcpy (save, ret, sizeof (gdb_sigset_t));

  return ret;
}

/*
 * Function: proc_get_traced_signals
 *
 * returns the set of signals that are traced / debugged.
 * Will also copy the sigset if 'save' is non-zero.
 */

gdb_sigset_t *
proc_get_traced_signals (procinfo *pi, gdb_sigset_t *save)
{
  gdb_sigset_t *ret = NULL;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return NULL;

  ret = &pi->prstatus.pr_sigtrace;
#else
  {
    static gdb_sigset_t sigtrace;

    if (ioctl (pi->ctl_fd, PIOCGTRACE, &sigtrace) >= 0)
      ret = &sigtrace;
  }
#endif
  if (save && ret)
    memcpy (save, ret, sizeof (gdb_sigset_t));

  return ret;
}

/*
 * Function: proc_trace_signal
 *
 * Add 'signo' to the set of signals that are traced.
 * Returns non-zero for success, zero for failure.
 */

int
proc_trace_signal (procinfo *pi, int signo)
{
  gdb_sigset_t temp;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

  if (pi)
    {
      if (proc_get_traced_signals (pi, &temp))
	{
	  praddset (&temp, signo);
	  return proc_set_traced_signals (pi, &temp);
	}
    }

  return 0;	/* failure */
}

/*
 * Function: proc_ignore_signal
 *
 * Remove 'signo' from the set of signals that are traced.
 * Returns non-zero for success, zero for failure.
 */

int
proc_ignore_signal (procinfo *pi, int signo)
{
  gdb_sigset_t temp;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

  if (pi)
    {
      if (proc_get_traced_signals (pi, &temp))
	{
	  prdelset (&temp, signo);
	  return proc_set_traced_signals (pi, &temp);
	}
    }

  return 0;	/* failure */
}

/*
 * Function: proc_get_traced_faults
 *
 * returns the set of hardware faults that are traced /debugged.
 * Will also copy the faultset if 'save' is non-zero.
 */

fltset_t *
proc_get_traced_faults (procinfo *pi, fltset_t *save)
{
  fltset_t *ret = NULL;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return NULL;

  ret = &pi->prstatus.pr_flttrace;
#else
  {
    static fltset_t flttrace;

    if (ioctl (pi->ctl_fd, PIOCGFAULT, &flttrace) >= 0)
      ret = &flttrace;
  }
#endif
  if (save && ret)
    memcpy (save, ret, sizeof (fltset_t));

  return ret;
}

/*
 * Function: proc_get_traced_sysentry
 *
 * returns the set of syscalls that are traced /debugged on entry.
 * Will also copy the syscall set if 'save' is non-zero.
 */

sysset_t *
proc_get_traced_sysentry (procinfo *pi, sysset_t *save)
{
  sysset_t *ret = NULL;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return NULL;

#ifndef DYNAMIC_SYSCALLS
  ret = &pi->prstatus.pr_sysentry;
#else /* DYNAMIC_SYSCALLS */
  {
    static sysset_t *sysentry;
    size_t size;

    if (!sysentry)
      sysentry = sysset_t_alloc (pi);
    ret = sysentry;
    if (pi->status_fd == 0 && open_procinfo_files (pi, FD_STATUS) == 0)
      return NULL;
    if (pi->prstatus.pr_sysentry_offset == 0)
      {
	gdb_premptysysset (sysentry);
      }
    else
      {
	int rsize;

	if (lseek (pi->status_fd, (off_t) pi->prstatus.pr_sysentry_offset,
	           SEEK_SET)
	    != (off_t) pi->prstatus.pr_sysentry_offset)
	  return NULL;
	size = sysset_t_size (pi);
	gdb_premptysysset (sysentry);
	rsize = read (pi->status_fd, sysentry, size);
	if (rsize < 0)
	  return NULL;
      }
  }
#endif /* DYNAMIC_SYSCALLS */
#else /* !NEW_PROC_API */
  {
    static sysset_t sysentry;

    if (ioctl (pi->ctl_fd, PIOCGENTRY, &sysentry) >= 0)
      ret = &sysentry;
  }
#endif /* NEW_PROC_API */
  if (save && ret)
    memcpy (save, ret, sysset_t_size (pi));

  return ret;
}

/*
 * Function: proc_get_traced_sysexit
 *
 * returns the set of syscalls that are traced /debugged on exit.
 * Will also copy the syscall set if 'save' is non-zero.
 */

sysset_t *
proc_get_traced_sysexit (procinfo *pi, sysset_t *save)
{
  sysset_t * ret = NULL;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return NULL;

#ifndef DYNAMIC_SYSCALLS
  ret = &pi->prstatus.pr_sysexit;
#else /* DYNAMIC_SYSCALLS */
  {
    static sysset_t *sysexit;
    size_t size;

    if (!sysexit)
      sysexit = sysset_t_alloc (pi);
    ret = sysexit;
    if (pi->status_fd == 0 && open_procinfo_files (pi, FD_STATUS) == 0)
      return NULL;
    if (pi->prstatus.pr_sysexit_offset == 0)
      {
	gdb_premptysysset (sysexit);
      }
    else
      {
	int rsize;

	if (lseek (pi->status_fd, (off_t) pi->prstatus.pr_sysexit_offset, SEEK_SET)
	    != (off_t) pi->prstatus.pr_sysexit_offset)
	  return NULL;
	size = sysset_t_size (pi);
	gdb_premptysysset (sysexit);
	rsize = read (pi->status_fd, sysexit, size);
	if (rsize < 0)
	  return NULL;
      }
  }
#endif /* DYNAMIC_SYSCALLS */
#else
  {
    static sysset_t sysexit;

    if (ioctl (pi->ctl_fd, PIOCGEXIT, &sysexit) >= 0)
      ret = &sysexit;
  }
#endif
  if (save && ret)
    memcpy (save, ret, sysset_t_size (pi));

  return ret;
}

/*
 * Function: proc_clear_current_fault
 *
 * The current fault (if any) is cleared; the associated signal
 * will not be sent to the process or LWP when it resumes.
 * Returns non-zero for success,  zero for failure.
 */

int
proc_clear_current_fault (procinfo *pi)
{
  int win;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  {
    procfs_ctl_t cmd = PCCFAULT;
    win = (write (pi->ctl_fd, (void *) &cmd, sizeof (cmd)) == sizeof (cmd));
  }
#else
  win = (ioctl (pi->ctl_fd, PIOCCFAULT, 0) >= 0);
#endif

  return win;
}

/*
 * Function: proc_set_current_signal
 *
 * Set the "current signal" that will be delivered next to the process.
 * NOTE: semantics are different from those of KILL.
 * This signal will be delivered to the process or LWP
 * immediately when it is resumed (even if the signal is held/blocked);
 * it will NOT immediately cause another event of interest, and will NOT
 * first trap back to the debugger.
 *
 * Returns non-zero for success,  zero for failure.
 */

int
proc_set_current_signal (procinfo *pi, int signo)
{
  int win;
  struct {
    procfs_ctl_t cmd;
    /* Use char array to avoid alignment issues.  */
    char sinfo[sizeof (gdb_siginfo_t)];
  } arg;
  gdb_siginfo_t *mysinfo;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef PROCFS_DONT_PIOCSSIG_CURSIG
  /* With Alpha OSF/1 procfs, the kernel gets really confused if it
   * receives a PIOCSSIG with a signal identical to the current signal,
   * it messes up the current signal. Work around the kernel bug.
   */
  if (signo > 0 &&
      signo == proc_cursig (pi))
    return 1;           /* I assume this is a success? */
#endif

  /* The pointer is just a type alias.  */
  mysinfo = (gdb_siginfo_t *) &arg.sinfo;
  mysinfo->si_signo = signo;
  mysinfo->si_code  = 0;
  mysinfo->si_pid   = getpid ();       /* ?why? */
  mysinfo->si_uid   = getuid ();       /* ?why? */

#ifdef NEW_PROC_API
  arg.cmd = PCSSIG;
  win = (write (pi->ctl_fd, (void *) &arg, sizeof (arg))  == sizeof (arg));
#else
  win = (ioctl (pi->ctl_fd, PIOCSSIG, (void *) &arg.sinfo) >= 0);
#endif

  return win;
}

/*
 * Function: proc_clear_current_signal
 *
 * The current signal (if any) is cleared, and
 * is not sent to the process or LWP when it resumes.
 * Returns non-zero for success,  zero for failure.
 */

int
proc_clear_current_signal (procinfo *pi)
{
  int win;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

#ifdef NEW_PROC_API
  {
    struct {
      procfs_ctl_t cmd;
      /* Use char array to avoid alignment issues.  */
      char sinfo[sizeof (gdb_siginfo_t)];
    } arg;
    gdb_siginfo_t *mysinfo;

    arg.cmd = PCSSIG;
    /* The pointer is just a type alias.  */
    mysinfo = (gdb_siginfo_t *) &arg.sinfo;
    mysinfo->si_signo = 0;
    mysinfo->si_code  = 0;
    mysinfo->si_errno = 0;
    mysinfo->si_pid   = getpid ();       /* ?why? */
    mysinfo->si_uid   = getuid ();       /* ?why? */

    win = (write (pi->ctl_fd, (void *) &arg, sizeof (arg)) == sizeof (arg));
  }
#else
  win = (ioctl (pi->ctl_fd, PIOCSSIG, 0) >= 0);
#endif

  return win;
}

/*
 * Function: proc_get_gregs
 *
 * Get the general registers for the process or LWP.
 * Returns non-zero for success, zero for failure.
 */

gdb_gregset_t *
proc_get_gregs (procinfo *pi)
{
  if (!pi->status_valid || !pi->gregs_valid)
    if (!proc_get_status (pi))
      return NULL;

  /*
   * OK, sorry about the ifdef's.
   * There's three cases instead of two, because
   * in this instance Unixware and Solaris/RW differ.
   */

#ifdef NEW_PROC_API
#ifdef UNIXWARE		/* ugh, a true architecture dependency */
  return &pi->prstatus.pr_lwp.pr_context.uc_mcontext.gregs;
#else	/* not Unixware */
  return &pi->prstatus.pr_lwp.pr_reg;
#endif	/* Unixware */
#else	/* not NEW_PROC_API */
  return &pi->prstatus.pr_reg;
#endif	/* NEW_PROC_API */
}

/*
 * Function: proc_get_fpregs
 *
 * Get the floating point registers for the process or LWP.
 * Returns non-zero for success, zero for failure.
 */

gdb_fpregset_t *
proc_get_fpregs (procinfo *pi)
{
#ifdef NEW_PROC_API
  if (!pi->status_valid || !pi->fpregs_valid)
    if (!proc_get_status (pi))
      return NULL;

#ifdef UNIXWARE		/* a true architecture dependency */
  return &pi->prstatus.pr_lwp.pr_context.uc_mcontext.fpregs;
#else
  return &pi->prstatus.pr_lwp.pr_fpreg;
#endif	/* Unixware */

#else	/* not NEW_PROC_API */
  if (pi->fpregs_valid)
    return &pi->fpregset;	/* already got 'em */
  else
    {
      if (pi->ctl_fd == 0 &&
	  open_procinfo_files (pi, FD_CTL) == 0)
	{
	  return NULL;
	}
      else
	{
#ifdef PIOCTGFPREG
	  struct {
	    long pr_count;
	    tid_t pr_error_thread;
	    tfpregset_t thread_1;
	  } thread_fpregs;

	  thread_fpregs.pr_count = 1;
	  thread_fpregs.thread_1.tid = pi->tid;

	  if (pi->tid == 0 &&
	      ioctl (pi->ctl_fd, PIOCGFPREG, &pi->fpregset) >= 0)
	    {
	      pi->fpregs_valid = 1;
	      return &pi->fpregset;	/* got 'em now! */
	    }
	  else if (pi->tid != 0 &&
		   ioctl (pi->ctl_fd, PIOCTGFPREG, &thread_fpregs) >= 0)
	    {
	      memcpy (&pi->fpregset, &thread_fpregs.thread_1.pr_fpregs,
		      sizeof (pi->fpregset));
	      pi->fpregs_valid = 1;
	      return &pi->fpregset;	/* got 'em now! */
	    }
	  else
	    {
	      return NULL;
	    }
#else
	  if (ioctl (pi->ctl_fd, PIOCGFPREG, &pi->fpregset) >= 0)
	    {
	      pi->fpregs_valid = 1;
	      return &pi->fpregset;	/* got 'em now! */
	    }
	  else
	    {
	      return NULL;
	    }
#endif
	}
    }
#endif
}

/*
 * Function: proc_set_gregs
 *
 * Write the general registers back to the process or LWP.
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_gregs (procinfo *pi)
{
  gdb_gregset_t *gregs;
  int win;

  if ((gregs = proc_get_gregs (pi)) == NULL)
    return 0;	/* get_regs has already warned */

  if (pi->ctl_fd == 0 &&
      open_procinfo_files (pi, FD_CTL) == 0)
    {
      return 0;
    }
  else
    {
#ifdef NEW_PROC_API
      struct {
	procfs_ctl_t cmd;
	/* Use char array to avoid alignment issues.  */
	char gregs[sizeof (gdb_gregset_t)];
      } arg;

      arg.cmd   = PCSREG;
      memcpy (&arg.gregs, gregs, sizeof (arg.gregs));
      win = (write (pi->ctl_fd, (void *) &arg, sizeof (arg)) == sizeof (arg));
#else
      win = (ioctl (pi->ctl_fd, PIOCSREG, gregs) >= 0);
#endif
    }

  /* Policy: writing the regs invalidates our cache. */
  pi->gregs_valid = 0;
  return win;
}

/*
 * Function: proc_set_fpregs
 *
 * Modify the floating point register set of the process or LWP.
 * Returns non-zero for success, zero for failure.
 */

int
proc_set_fpregs (procinfo *pi)
{
  gdb_fpregset_t *fpregs;
  int win;

  if ((fpregs = proc_get_fpregs (pi)) == NULL)
    return 0;		/* get_fpregs has already warned */

  if (pi->ctl_fd == 0 &&
      open_procinfo_files (pi, FD_CTL) == 0)
    {
      return 0;
    }
  else
    {
#ifdef NEW_PROC_API
      struct {
	procfs_ctl_t cmd;
	/* Use char array to avoid alignment issues.  */
	char fpregs[sizeof (gdb_fpregset_t)];
      } arg;

      arg.cmd   = PCSFPREG;
      memcpy (&arg.fpregs, fpregs, sizeof (arg.fpregs));
      win = (write (pi->ctl_fd, (void *) &arg, sizeof (arg)) == sizeof (arg));
#else
#ifdef PIOCTSFPREG
      if (pi->tid == 0)
	win = (ioctl (pi->ctl_fd, PIOCSFPREG, fpregs) >= 0);
      else
	{
	  struct {
	    long pr_count;
	    tid_t pr_error_thread;
	    tfpregset_t thread_1;
	  } thread_fpregs;

	  thread_fpregs.pr_count = 1;
	  thread_fpregs.thread_1.tid = pi->tid;
	  memcpy (&thread_fpregs.thread_1.pr_fpregs, fpregs,
		  sizeof (*fpregs));
	  win = (ioctl (pi->ctl_fd, PIOCTSFPREG, &thread_fpregs) >= 0);
	}
#else
      win = (ioctl (pi->ctl_fd, PIOCSFPREG, fpregs) >= 0);
#endif	/* osf PIOCTSFPREG */
#endif	/* NEW_PROC_API */
    }

  /* Policy: writing the regs invalidates our cache. */
  pi->fpregs_valid = 0;
  return win;
}

/*
 * Function: proc_kill
 *
 * Send a signal to the proc or lwp with the semantics of "kill()".
 * Returns non-zero for success,  zero for failure.
 */

int
proc_kill (procinfo *pi, int signo)
{
  int win;

  /*
   * We might conceivably apply this operation to an LWP, and
   * the LWP's ctl file descriptor might not be open.
   */

  if (pi->ctl_fd == 0 &&
      open_procinfo_files (pi, FD_CTL) == 0)
    {
      return 0;
    }
  else
    {
#ifdef NEW_PROC_API
      procfs_ctl_t cmd[2];

      cmd[0] = PCKILL;
      cmd[1] = signo;
      win = (write (pi->ctl_fd, (char *) &cmd, sizeof (cmd)) == sizeof (cmd));
#else   /* ioctl method */
      /* FIXME: do I need the Alpha OSF fixups present in
	 procfs.c/unconditionally_kill_inferior?  Perhaps only for SIGKILL? */
      win = (ioctl (pi->ctl_fd, PIOCKILL, &signo) >= 0);
#endif
  }

  return win;
}

/*
 * Function: proc_parent_pid
 *
 * Find the pid of the process that started this one.
 * Returns the parent process pid, or zero.
 */

int
proc_parent_pid (procinfo *pi)
{
  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return 0;

  return pi->prstatus.pr_ppid;
}


/* Convert a target address (a.k.a. CORE_ADDR) into a host address
   (a.k.a void pointer)!  */

static void *
procfs_address_to_host_pointer (CORE_ADDR addr)
{
  void *ptr;

  gdb_assert (sizeof (ptr) == TYPE_LENGTH (builtin_type_void_data_ptr));
  ADDRESS_TO_POINTER (builtin_type_void_data_ptr, &ptr, addr);
  return ptr;
}

/*
 * Function: proc_set_watchpoint
 *
 */

int
proc_set_watchpoint (procinfo *pi, CORE_ADDR addr, int len, int wflags)
{
#if !defined (TARGET_HAS_HARDWARE_WATCHPOINTS)
  return 0;
#else
/* Horrible hack!  Detect Solaris 2.5, because this doesn't work on 2.5 */
#if defined (PIOCOPENLWP) || defined (UNIXWARE)	/* Solaris 2.5: bail out */
  return 0;
#else
  struct {
    procfs_ctl_t cmd;
    char watch[sizeof (prwatch_t)];
  } arg;
  prwatch_t *pwatch;

  pwatch            = (prwatch_t *) &arg.watch;
  /* NOTE: cagney/2003-02-01: Even more horrible hack.  Need to
     convert a target address into something that can be stored in a
     native data structure.  */
#ifdef PCAGENT	/* Horrible hack: only defined on Solaris 2.6+ */
  pwatch->pr_vaddr  = (uintptr_t) procfs_address_to_host_pointer (addr);
#else
  pwatch->pr_vaddr  = (caddr_t) procfs_address_to_host_pointer (addr);
#endif
  pwatch->pr_size   = len;
  pwatch->pr_wflags = wflags;
#if defined(NEW_PROC_API) && defined (PCWATCH)
  arg.cmd = PCWATCH;
  return (write (pi->ctl_fd, &arg, sizeof (arg)) == sizeof (arg));
#else
#if defined (PIOCSWATCH)
  return (ioctl (pi->ctl_fd, PIOCSWATCH, pwatch) >= 0);
#else
  return 0;	/* Fail */
#endif
#endif
#endif
#endif
}

#ifdef TM_I386SOL2_H		/* Is it hokey to use this? */

#include <sys/sysi86.h>

/*
 * Function: proc_get_LDT_entry
 *
 * Inputs:
 *   procinfo *pi;
 *   int key;
 *
 * The 'key' is actually the value of the lower 16 bits of
 * the GS register for the LWP that we're interested in.
 *
 * Return: matching ssh struct (LDT entry).
 */

struct ssd *
proc_get_LDT_entry (procinfo *pi, int key)
{
  static struct ssd *ldt_entry = NULL;
#ifdef NEW_PROC_API
  char pathname[MAX_PROC_NAME_SIZE];
  struct cleanup *old_chain = NULL;
  int  fd;

  /* Allocate space for one LDT entry.
     This alloc must persist, because we return a pointer to it.  */
  if (ldt_entry == NULL)
    ldt_entry = (struct ssd *) xmalloc (sizeof (struct ssd));

  /* Open the file descriptor for the LDT table.  */
  sprintf (pathname, "/proc/%d/ldt", pi->pid);
  if ((fd = open_with_retry (pathname, O_RDONLY)) < 0)
    {
      proc_warn (pi, "proc_get_LDT_entry (open)", __LINE__);
      return NULL;
    }
  /* Make sure it gets closed again! */
  old_chain = make_cleanup_close (fd);

  /* Now 'read' thru the table, find a match and return it.  */
  while (read (fd, ldt_entry, sizeof (struct ssd)) == sizeof (struct ssd))
    {
      if (ldt_entry->sel == 0 &&
	  ldt_entry->bo  == 0 &&
	  ldt_entry->acc1 == 0 &&
	  ldt_entry->acc2 == 0)
	break;	/* end of table */
      /* If key matches, return this entry. */
      if (ldt_entry->sel == key)
	return ldt_entry;
    }
  /* Loop ended, match not found. */
  return NULL;
#else
  int nldt, i;
  static int nalloc = 0;

  /* Get the number of LDT entries.  */
  if (ioctl (pi->ctl_fd, PIOCNLDT, &nldt) < 0)
    {
      proc_warn (pi, "proc_get_LDT_entry (PIOCNLDT)", __LINE__);
      return NULL;
    }

  /* Allocate space for the number of LDT entries. */
  /* This alloc has to persist, 'cause we return a pointer to it. */
  if (nldt > nalloc)
    {
      ldt_entry = (struct ssd *)
	xrealloc (ldt_entry, (nldt + 1) * sizeof (struct ssd));
      nalloc = nldt;
    }

  /* Read the whole table in one gulp.  */
  if (ioctl (pi->ctl_fd, PIOCLDT, ldt_entry) < 0)
    {
      proc_warn (pi, "proc_get_LDT_entry (PIOCLDT)", __LINE__);
      return NULL;
    }

  /* Search the table and return the (first) entry matching 'key'. */
  for (i = 0; i < nldt; i++)
    if (ldt_entry[i].sel == key)
      return &ldt_entry[i];

  /* Loop ended, match not found. */
  return NULL;
#endif
}

#endif /* TM_I386SOL2_H */

/* =============== END, non-thread part of /proc  "MODULE" =============== */

/* =================== Thread "MODULE" =================== */

/* NOTE: you'll see more ifdefs and duplication of functions here,
   since there is a different way to do threads on every OS.  */

/*
 * Function: proc_get_nthreads
 *
 * Return the number of threads for the process
 */

#if defined (PIOCNTHR) && defined (PIOCTLIST)
/*
 * OSF version
 */
int
proc_get_nthreads (procinfo *pi)
{
  int nthreads = 0;

  if (ioctl (pi->ctl_fd, PIOCNTHR, &nthreads) < 0)
    proc_warn (pi, "procfs: PIOCNTHR failed", __LINE__);

  return nthreads;
}

#else
#if defined (SYS_lwpcreate) || defined (SYS_lwp_create) /* FIXME: multiple */
/*
 * Solaris and Unixware version
 */
int
proc_get_nthreads (procinfo *pi)
{
  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return 0;

  /*
   * NEW_PROC_API: only works for the process procinfo,
   * because the LWP procinfos do not get prstatus filled in.
   */
#ifdef NEW_PROC_API
  if (pi->tid != 0)	/* find the parent process procinfo */
    pi = find_procinfo_or_die (pi->pid, 0);
#endif
  return pi->prstatus.pr_nlwp;
}

#else
/*
 * Default version
 */
int
proc_get_nthreads (procinfo *pi)
{
  return 0;
}
#endif
#endif

/*
 * Function: proc_get_current_thread (LWP version)
 *
 * Return the ID of the thread that had an event of interest.
 * (ie. the one that hit a breakpoint or other traced event).
 * All other things being equal, this should be the ID of a
 * thread that is currently executing.
 */

#if defined (SYS_lwpcreate) || defined (SYS_lwp_create) /* FIXME: multiple */
/*
 * Solaris and Unixware version
 */
int
proc_get_current_thread (procinfo *pi)
{
  /*
   * Note: this should be applied to the root procinfo for the process,
   * not to the procinfo for an LWP.  If applied to the procinfo for
   * an LWP, it will simply return that LWP's ID.  In that case,
   * find the parent process procinfo.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

  if (!pi->status_valid)
    if (!proc_get_status (pi))
      return 0;

#ifdef NEW_PROC_API
  return pi->prstatus.pr_lwp.pr_lwpid;
#else
  return pi->prstatus.pr_who;
#endif
}

#else
#if defined (PIOCNTHR) && defined (PIOCTLIST)
/*
 * OSF version
 */
int
proc_get_current_thread (procinfo *pi)
{
#if 0	/* FIXME: not ready for prime time? */
  return pi->prstatus.pr_tid;
#else
  return 0;
#endif
}

#else
/*
 * Default version
 */
int
proc_get_current_thread (procinfo *pi)
{
  return 0;
}

#endif
#endif

/*
 * Function: proc_update_threads
 *
 * Discover the IDs of all the threads within the process, and
 * create a procinfo for each of them (chained to the parent).
 *
 * This unfortunately requires a different method on every OS.
 *
 * Return: non-zero for success, zero for failure.
 */

int
proc_delete_dead_threads (procinfo *parent, procinfo *thread, void *ignore)
{
  if (thread && parent)	/* sanity */
    {
      thread->status_valid = 0;
      if (!proc_get_status (thread))
	destroy_one_procinfo (&parent->thread_list, thread);
    }
  return 0;	/* keep iterating */
}

#if defined (PIOCLSTATUS)
/*
 * Solaris 2.5 (ioctl) version
 */
int
proc_update_threads (procinfo *pi)
{
  gdb_prstatus_t *prstatus;
  struct cleanup *old_chain = NULL;
  procinfo *thread;
  int nlwp, i;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

  proc_iterate_over_threads (pi, proc_delete_dead_threads, NULL);

  if ((nlwp = proc_get_nthreads (pi)) <= 1)
    return 1;	/* Process is not multi-threaded; nothing to do.  */

  prstatus = xmalloc (sizeof (gdb_prstatus_t) * (nlwp + 1));

  old_chain = make_cleanup (xfree, prstatus);
  if (ioctl (pi->ctl_fd, PIOCLSTATUS, prstatus) < 0)
    proc_error (pi, "update_threads (PIOCLSTATUS)", __LINE__);

  /* Skip element zero, which represents the process as a whole. */
  for (i = 1; i < nlwp + 1; i++)
    {
      if ((thread = create_procinfo (pi->pid, prstatus[i].pr_who)) == NULL)
	proc_error (pi, "update_threads, create_procinfo", __LINE__);

      memcpy (&thread->prstatus, &prstatus[i], sizeof (*prstatus));
      thread->status_valid = 1;
    }
  pi->threads_valid = 1;
  do_cleanups (old_chain);
  return 1;
}
#else
#ifdef NEW_PROC_API
/*
 * Unixware and Solaris 6 (and later) version
 */
static void
do_closedir_cleanup (void *dir)
{
  closedir (dir);
}

int
proc_update_threads (procinfo *pi)
{
  char pathname[MAX_PROC_NAME_SIZE + 16];
  struct dirent *direntry;
  struct cleanup *old_chain = NULL;
  procinfo *thread;
  DIR *dirp;
  int lwpid;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

  proc_iterate_over_threads (pi, proc_delete_dead_threads, NULL);

  /*
   * Unixware
   *
   * Note: this brute-force method is the only way I know of
   * to accomplish this task on Unixware.  This method will
   * also work on Solaris 2.6 and 2.7.  There is a much simpler
   * and more elegant way to do this on Solaris, but the margins
   * of this manuscript are too small to write it here...  ;-)
   */

  strcpy (pathname, pi->pathname);
  strcat (pathname, "/lwp");
  if ((dirp = opendir (pathname)) == NULL)
    proc_error (pi, "update_threads, opendir", __LINE__);

  old_chain = make_cleanup (do_closedir_cleanup, dirp);
  while ((direntry = readdir (dirp)) != NULL)
    if (direntry->d_name[0] != '.')		/* skip '.' and '..' */
      {
	lwpid = atoi (&direntry->d_name[0]);
	if ((thread = create_procinfo (pi->pid, lwpid)) == NULL)
	  proc_error (pi, "update_threads, create_procinfo", __LINE__);
      }
  pi->threads_valid = 1;
  do_cleanups (old_chain);
  return 1;
}
#else
#ifdef PIOCTLIST
/*
 * OSF version
 */
int
proc_update_threads (procinfo *pi)
{
  int nthreads, i;
  tid_t *threads;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

  proc_iterate_over_threads (pi, proc_delete_dead_threads, NULL);

  nthreads = proc_get_nthreads (pi);
  if (nthreads < 2)
    return 0;		/* nothing to do for 1 or fewer threads */

  threads = xmalloc (nthreads * sizeof (tid_t));

  if (ioctl (pi->ctl_fd, PIOCTLIST, threads) < 0)
    proc_error (pi, "procfs: update_threads (PIOCTLIST)", __LINE__);

  for (i = 0; i < nthreads; i++)
    {
      if (!find_procinfo (pi->pid, threads[i]))
	if (!create_procinfo  (pi->pid, threads[i]))
	  proc_error (pi, "update_threads, create_procinfo", __LINE__);
    }
  pi->threads_valid = 1;
  return 1;
}
#else
/*
 * Default version
 */
int
proc_update_threads (procinfo *pi)
{
  return 0;
}
#endif	/* OSF PIOCTLIST */
#endif  /* NEW_PROC_API   */
#endif  /* SOL 2.5 PIOCLSTATUS */

/*
 * Function: proc_iterate_over_threads
 *
 * Description:
 *   Given a pointer to a function, call that function once
 *   for each lwp in the procinfo list, until the function
 *   returns non-zero, in which event return the value
 *   returned by the function.
 *
 * Note: this function does NOT call update_threads.
 * If you want to discover new threads first, you must
 * call that function explicitly.  This function just makes
 * a quick pass over the currently-known procinfos.
 *
 * Arguments:
 *   pi		- parent process procinfo
 *   func	- per-thread function
 *   ptr	- opaque parameter for function.
 *
 * Return:
 *   First non-zero return value from the callee, or zero.
 */

int
proc_iterate_over_threads (procinfo *pi,
			   int (*func) (procinfo *, procinfo *, void *),
			   void *ptr)
{
  procinfo *thread, *next;
  int retval = 0;

  /*
   * We should never have to apply this operation to any procinfo
   * except the one for the main process.  If that ever changes
   * for any reason, then take out the following clause and
   * replace it with one that makes sure the ctl_fd is open.
   */

  if (pi->tid != 0)
    pi = find_procinfo_or_die (pi->pid, 0);

  for (thread = pi->thread_list; thread != NULL; thread = next)
    {
      next = thread->next;	/* in case thread is destroyed */
      if ((retval = (*func) (pi, thread, ptr)) != 0)
	break;
    }

  return retval;
}

/* =================== END, Thread "MODULE" =================== */

/* =================== END, /proc  "MODULE" =================== */

/* ===================  GDB  "MODULE" =================== */

/*
 * Here are all of the gdb target vector functions and their friends.
 */

static ptid_t do_attach (ptid_t ptid);
static void do_detach (int signo);
static int register_gdb_signals (procinfo *, gdb_sigset_t *);

/*
 * Function: procfs_debug_inferior
 *
 * Sets up the inferior to be debugged.
 * Registers to trace signals, hardware faults, and syscalls.
 * Note: does not set RLC flag: caller may want to customize that.
 *
 * Returns: zero for success (note! unlike most functions in this module)
 *   On failure, returns the LINE NUMBER where it failed!
 */

static int
procfs_debug_inferior (procinfo *pi)
{
  fltset_t traced_faults;
  gdb_sigset_t traced_signals;
  sysset_t *traced_syscall_entries;
  sysset_t *traced_syscall_exits;
  int status;

#ifdef PROCFS_DONT_TRACE_FAULTS
  /* On some systems (OSF), we don't trace hardware faults.
     Apparently it's enough that we catch them as signals.
     Wonder why we don't just do that in general? */
  premptyset (&traced_faults);		/* don't trace faults. */
#else
  /* Register to trace hardware faults in the child. */
  prfillset (&traced_faults);		/* trace all faults... */
  prdelset  (&traced_faults, FLTPAGE);	/* except page fault.  */
#endif
  if (!proc_set_traced_faults  (pi, &traced_faults))
    return __LINE__;

  /* Register to trace selected signals in the child. */
  premptyset (&traced_signals);
  if (!register_gdb_signals (pi, &traced_signals))
    return __LINE__;


  /* Register to trace the 'exit' system call (on entry).  */
  traced_syscall_entries = sysset_t_alloc (pi);
  gdb_premptysysset (traced_syscall_entries);
#ifdef SYS_exit
  gdb_praddsysset (traced_syscall_entries, SYS_exit);
#endif
#ifdef SYS_lwpexit
  gdb_praddsysset (traced_syscall_entries, SYS_lwpexit);	/* And _lwp_exit... */
#endif
#ifdef SYS_lwp_exit
  gdb_praddsysset (traced_syscall_entries, SYS_lwp_exit);
#endif
#ifdef DYNAMIC_SYSCALLS
  {
    int callnum = find_syscall (pi, "_exit");
    if (callnum >= 0)
      gdb_praddsysset (traced_syscall_entries, callnum);
  }
#endif

  status = proc_set_traced_sysentry (pi, traced_syscall_entries);
  xfree (traced_syscall_entries);
  if (!status)
    return __LINE__;

#ifdef PRFS_STOPEXEC	/* defined on OSF */
  /* OSF method for tracing exec syscalls.  Quoting:
     Under Alpha OSF/1 we have to use a PIOCSSPCACT ioctl to trace
     exits from exec system calls because of the user level loader.  */
  /* FIXME: make nice and maybe move into an access function. */
  {
    int prfs_flags;

    if (ioctl (pi->ctl_fd, PIOCGSPCACT, &prfs_flags) < 0)
      return __LINE__;

    prfs_flags |= PRFS_STOPEXEC;

    if (ioctl (pi->ctl_fd, PIOCSSPCACT, &prfs_flags) < 0)
      return __LINE__;
  }
#else /* not PRFS_STOPEXEC */
  /* Everyone else's (except OSF) method for tracing exec syscalls */
  /* GW: Rationale...
     Not all systems with /proc have all the exec* syscalls with the same
     names.  On the SGI, for example, there is no SYS_exec, but there
     *is* a SYS_execv.  So, we try to account for that. */

  traced_syscall_exits = sysset_t_alloc (pi);
  gdb_premptysysset (traced_syscall_exits);
#ifdef SYS_exec
  gdb_praddsysset (traced_syscall_exits, SYS_exec);
#endif
#ifdef SYS_execve
  gdb_praddsysset (traced_syscall_exits, SYS_execve);
#endif
#ifdef SYS_execv
  gdb_praddsysset (traced_syscall_exits, SYS_execv);
#endif

#ifdef SYS_lwpcreate
  gdb_praddsysset (traced_syscall_exits, SYS_lwpcreate);
  gdb_praddsysset (traced_syscall_exits, SYS_lwpexit);
#endif

#ifdef SYS_lwp_create	/* FIXME: once only, please */
  gdb_praddsysset (traced_syscall_exits, SYS_lwp_create);
  gdb_praddsysset (traced_syscall_exits, SYS_lwp_exit);
#endif

#ifdef DYNAMIC_SYSCALLS
  {
    int callnum = find_syscall (pi, "execve");
    if (callnum >= 0)
      gdb_praddsysset (traced_syscall_exits, callnum);
    callnum = find_syscall (pi, "ra_execve");
    if (callnum >= 0)
      gdb_praddsysset (traced_syscall_exits, callnum);
  }
#endif

  status = proc_set_traced_sysexit (pi, traced_syscall_exits);
  xfree (traced_syscall_exits);
  if (!status)
    return __LINE__;

#endif /* PRFS_STOPEXEC */
  return 0;
}

static void
procfs_attach (char *args, int from_tty)
{
  char *exec_file;
  int   pid;

  if (!args)
    error_no_arg ("process-id to attach");

  pid = atoi (args);
  if (pid == getpid ())
    error ("Attaching GDB to itself is not a good idea...");

  if (from_tty)
    {
      exec_file = get_exec_file (0);

      if (exec_file)
	printf_filtered ("Attaching to program `%s', %s\n",
			 exec_file, target_pid_to_str (pid_to_ptid (pid)));
      else
	printf_filtered ("Attaching to %s\n",
	                 target_pid_to_str (pid_to_ptid (pid)));

      fflush (stdout);
    }
  inferior_ptid = do_attach (pid_to_ptid (pid));
  push_target (&procfs_ops);
}

static void
procfs_detach (char *args, int from_tty)
{
  char *exec_file;
  int   signo = 0;

  if (from_tty)
    {
      exec_file = get_exec_file (0);
      if (exec_file == 0)
	exec_file = "";
      printf_filtered ("Detaching from program: %s %s\n",
	      exec_file, target_pid_to_str (inferior_ptid));
      fflush (stdout);
    }
  if (args)
    signo = atoi (args);

  do_detach (signo);
  inferior_ptid = null_ptid;
  unpush_target (&procfs_ops);		/* Pop out of handling an inferior */
}

static ptid_t
do_attach (ptid_t ptid)
{
  procinfo *pi;
  int fail;

  if ((pi = create_procinfo (PIDGET (ptid), 0)) == NULL)
    perror ("procfs: out of memory in 'attach'");

  if (!open_procinfo_files (pi, FD_CTL))
    {
      fprintf_filtered (gdb_stderr, "procfs:%d -- ", __LINE__);
      sprintf (errmsg, "do_attach: couldn't open /proc file for process %d",
	       PIDGET (ptid));
      dead_procinfo (pi, errmsg, NOKILL);
    }

  /* Stop the process (if it isn't already stopped).  */
  if (proc_flags (pi) & (PR_STOPPED | PR_ISTOP))
    {
      pi->was_stopped = 1;
      proc_prettyprint_why (proc_why (pi), proc_what (pi), 1);
    }
  else
    {
      pi->was_stopped = 0;
      /* Set the process to run again when we close it.  */
      if (!proc_set_run_on_last_close (pi))
	dead_procinfo (pi, "do_attach: couldn't set RLC.", NOKILL);

      /* Now stop the process. */
      if (!proc_stop_process (pi))
	dead_procinfo (pi, "do_attach: couldn't stop the process.", NOKILL);
      pi->ignore_next_sigstop = 1;
    }
  /* Save some of the /proc state to be restored if we detach.  */
  if (!proc_get_traced_faults   (pi, &pi->saved_fltset))
    dead_procinfo (pi, "do_attach: couldn't save traced faults.", NOKILL);
  if (!proc_get_traced_signals  (pi, &pi->saved_sigset))
    dead_procinfo (pi, "do_attach: couldn't save traced signals.", NOKILL);
  if (!proc_get_traced_sysentry (pi, pi->saved_entryset))
    dead_procinfo (pi, "do_attach: couldn't save traced syscall entries.",
		   NOKILL);
  if (!proc_get_traced_sysexit  (pi, pi->saved_exitset))
    dead_procinfo (pi, "do_attach: couldn't save traced syscall exits.",
		   NOKILL);
  if (!proc_get_held_signals    (pi, &pi->saved_sighold))
    dead_procinfo (pi, "do_attach: couldn't save held signals.", NOKILL);

  if ((fail = procfs_debug_inferior (pi)) != 0)
    dead_procinfo (pi, "do_attach: failed in procfs_debug_inferior", NOKILL);

  /* Let GDB know that the inferior was attached.  */
  attach_flag = 1;
  return MERGEPID (pi->pid, proc_get_current_thread (pi));
}

static void
do_detach (int signo)
{
  procinfo *pi;

  /* Find procinfo for the main process */
  pi = find_procinfo_or_die (PIDGET (inferior_ptid), 0); /* FIXME: threads */
  if (signo)
    if (!proc_set_current_signal (pi, signo))
      proc_warn (pi, "do_detach, set_current_signal", __LINE__);

  if (!proc_set_traced_signals (pi, &pi->saved_sigset))
    proc_warn (pi, "do_detach, set_traced_signal", __LINE__);

  if (!proc_set_traced_faults (pi, &pi->saved_fltset))
    proc_warn (pi, "do_detach, set_traced_faults", __LINE__);

  if (!proc_set_traced_sysentry (pi, pi->saved_entryset))
    proc_warn (pi, "do_detach, set_traced_sysentry", __LINE__);

  if (!proc_set_traced_sysexit (pi, pi->saved_exitset))
    proc_warn (pi, "do_detach, set_traced_sysexit", __LINE__);

  if (!proc_set_held_signals (pi, &pi->saved_sighold))
    proc_warn (pi, "do_detach, set_held_signals", __LINE__);

  if (signo || (proc_flags (pi) & (PR_STOPPED | PR_ISTOP)))
    if (signo || !(pi->was_stopped) ||
	query ("Was stopped when attached, make it runnable again? "))
      {
	/* Clear any pending signal.  */
	if (!proc_clear_current_fault (pi))
	  proc_warn (pi, "do_detach, clear_current_fault", __LINE__);

	if (signo == 0 && !proc_clear_current_signal (pi))
	  proc_warn (pi, "do_detach, clear_current_signal", __LINE__);

	if (!proc_set_run_on_last_close (pi))
	  proc_warn (pi, "do_detach, set_rlc", __LINE__);
      }

  attach_flag = 0;
  destroy_procinfo (pi);
}

/*
 * fetch_registers
 *
 * Since the /proc interface cannot give us individual registers,
 * we pay no attention to the (regno) argument, and just fetch them all.
 * This results in the possibility that we will do unnecessarily many
 * fetches, since we may be called repeatedly for individual registers.
 * So we cache the results, and mark the cache invalid when the process
 * is resumed.
 */

static void
procfs_fetch_registers (int regno)
{
  gdb_fpregset_t *fpregs;
  gdb_gregset_t  *gregs;
  procinfo       *pi;
  int            pid;
  int            tid;

  pid = PIDGET (inferior_ptid);
  tid = TIDGET (inferior_ptid);

  /* First look up procinfo for the main process. */
  pi  = find_procinfo_or_die (pid, 0);

  /* If the event thread is not the same as GDB's requested thread
     (ie. inferior_ptid), then look up procinfo for the requested
     thread.  */
  if ((tid != 0) &&
      (tid != proc_get_current_thread (pi)))
    pi = find_procinfo_or_die (pid, tid);

  if (pi == NULL)
    error ("procfs: fetch_registers failed to find procinfo for %s",
	   target_pid_to_str (inferior_ptid));

  if ((gregs = proc_get_gregs (pi)) == NULL)
    proc_error (pi, "fetch_registers, get_gregs", __LINE__);

  supply_gregset (gregs);

  if (FP0_REGNUM >= 0)	/* need floating point? */
    {
      if ((regno >= 0 && regno < FP0_REGNUM)
	  || regno == PC_REGNUM
	  || regno == DEPRECATED_FP_REGNUM
	  || regno == SP_REGNUM)
	return;			/* not a floating point register */

      if ((fpregs = proc_get_fpregs (pi)) == NULL)
	proc_error (pi, "fetch_registers, get_fpregs", __LINE__);

      supply_fpregset (fpregs);
    }
}

/* Get ready to modify the registers array.  On machines which store
   individual registers, this doesn't need to do anything.  On
   machines which store all the registers in one fell swoop, such as
   /proc, this makes sure that registers contains all the registers
   from the program being debugged.  */

static void
procfs_prepare_to_store (void)
{
#ifdef CHILD_PREPARE_TO_STORE
  CHILD_PREPARE_TO_STORE ();
#endif
}

/*
 * store_registers
 *
 * Since the /proc interface will not read individual registers,
 * we will cache these requests until the process is resumed, and
 * only then write them back to the inferior process.
 *
 * FIXME: is that a really bad idea?  Have to think about cases
 * where writing one register might affect the value of others, etc.
 */

static void
procfs_store_registers (int regno)
{
  gdb_fpregset_t *fpregs;
  gdb_gregset_t  *gregs;
  procinfo       *pi;
  int            pid;
  int            tid;

  pid = PIDGET (inferior_ptid);
  tid = TIDGET (inferior_ptid);

  /* First find procinfo for main process */
  pi  = find_procinfo_or_die (pid, 0);

  /* If current lwp for process is not the same as requested thread
     (ie. inferior_ptid), then find procinfo for the requested thread.  */

  if ((tid != 0) &&
      (tid != proc_get_current_thread (pi)))
    pi = find_procinfo_or_die (pid, tid);

  if (pi == NULL)
    error ("procfs: store_registers: failed to find procinfo for %s",
	   target_pid_to_str (inferior_ptid));

  if ((gregs = proc_get_gregs (pi)) == NULL)
    proc_error (pi, "store_registers, get_gregs", __LINE__);

  fill_gregset (gregs, regno);
  if (!proc_set_gregs (pi))
    proc_error (pi, "store_registers, set_gregs", __LINE__);

  if (FP0_REGNUM >= 0)		/* need floating point? */
    {
      if ((regno >= 0 && regno < FP0_REGNUM)
	  || regno == PC_REGNUM
	  || regno == DEPRECATED_FP_REGNUM
	  || regno == SP_REGNUM)
	return;			/* not a floating point register */

      if ((fpregs = proc_get_fpregs (pi)) == NULL)
	proc_error (pi, "store_registers, get_fpregs", __LINE__);

      fill_fpregset (fpregs, regno);
      if (!proc_set_fpregs (pi))
	proc_error (pi, "store_registers, set_fpregs", __LINE__);
    }
}

static int
syscall_is_lwp_exit (procinfo *pi, int scall)
{

#ifdef SYS_lwp_exit
  if (scall == SYS_lwp_exit)
    return 1;
#endif
#ifdef SYS_lwpexit
  if (scall == SYS_lwpexit)
    return 1;
#endif
  return 0;
}

static int
syscall_is_exit (procinfo *pi, int scall)
{
#ifdef SYS_exit
  if (scall == SYS_exit)
    return 1;
#endif
#ifdef DYNAMIC_SYSCALLS
  if (find_syscall (pi, "_exit") == scall)
    return 1;
#endif
  return 0;
}

static int
syscall_is_exec (procinfo *pi, int scall)
{
#ifdef SYS_exec
  if (scall == SYS_exec)
    return 1;
#endif
#ifdef SYS_execv
  if (scall == SYS_execv)
    return 1;
#endif
#ifdef SYS_execve
  if (scall == SYS_execve)
    return 1;
#endif
#ifdef DYNAMIC_SYSCALLS
  if (find_syscall (pi, "_execve"))
    return 1;
  if (find_syscall (pi, "ra_execve"))
    return 1;
#endif
  return 0;
}

static int
syscall_is_lwp_create (procinfo *pi, int scall)
{
#ifdef SYS_lwp_create
  if (scall == SYS_lwp_create)
    return 1;
#endif
#ifdef SYS_lwpcreate
  if (scall == SYS_lwpcreate)
    return 1;
#endif
  return 0;
}

/*
 * Function: target_wait
 *
 * Retrieve the next stop event from the child process.
 * If child has not stopped yet, wait for it to stop.
 * Translate /proc eventcodes (or possibly wait eventcodes)
 * into gdb internal event codes.
 *
 * Return: id of process (and possibly thread) that incurred the event.
 *         event codes are returned thru a pointer parameter.
 */

static ptid_t
procfs_wait (ptid_t ptid, struct target_waitstatus *status)
{
  /* First cut: loosely based on original version 2.1 */
  procinfo *pi;
  int       wstat;
  int       temp_tid;
  ptid_t    retval, temp_ptid;
  int       why, what, flags;
  int       retry = 0;

wait_again:

  retry++;
  wstat    = 0;
  retval   = pid_to_ptid (-1);

  /* Find procinfo for main process */
  pi = find_procinfo_or_die (PIDGET (inferior_ptid), 0);
  if (pi)
    {
      /* We must assume that the status is stale now... */
      pi->status_valid = 0;
      pi->gregs_valid  = 0;
      pi->fpregs_valid = 0;

#if 0	/* just try this out... */
      flags = proc_flags (pi);
      why   = proc_why (pi);
      if ((flags & PR_STOPPED) && (why == PR_REQUESTED))
	pi->status_valid = 0;	/* re-read again, IMMEDIATELY... */
#endif
      /* If child is not stopped, wait for it to stop.  */
      if (!(proc_flags (pi) & (PR_STOPPED | PR_ISTOP)) &&
	  !proc_wait_for_stop (pi))
	{
	  /* wait_for_stop failed: has the child terminated? */
	  if (errno == ENOENT)
	    {
	      int wait_retval;

	      /* /proc file not found; presumably child has terminated. */
	      wait_retval = wait (&wstat); /* "wait" for the child's exit  */

	      if (wait_retval != PIDGET (inferior_ptid)) /* wrong child? */
		error ("procfs: couldn't stop process %d: wait returned %d\n",
		       PIDGET (inferior_ptid), wait_retval);
	      /* FIXME: might I not just use waitpid?
		 Or try find_procinfo to see if I know about this child? */
	      retval = pid_to_ptid (wait_retval);
	    }
	  else if (errno == EINTR)
	    goto wait_again;
	  else
	    {
	      /* Unknown error from wait_for_stop. */
	      proc_error (pi, "target_wait (wait_for_stop)", __LINE__);
	    }
	}
      else
	{
	  /* This long block is reached if either:
	     a) the child was already stopped, or
	     b) we successfully waited for the child with wait_for_stop.
	     This block will analyze the /proc status, and translate it
	     into a waitstatus for GDB.

	     If we actually had to call wait because the /proc file
	     is gone (child terminated), then we skip this block,
	     because we already have a waitstatus.  */

	  flags = proc_flags (pi);
	  why   = proc_why (pi);
	  what  = proc_what (pi);

	  if (flags & (PR_STOPPED | PR_ISTOP))
	    {
#ifdef PR_ASYNC
	      /* If it's running async (for single_thread control),
		 set it back to normal again.  */
	      if (flags & PR_ASYNC)
		if (!proc_unset_async (pi))
		  proc_error (pi, "target_wait, unset_async", __LINE__);
#endif

	      if (info_verbose)
		proc_prettyprint_why (why, what, 1);

	      /* The 'pid' we will return to GDB is composed of
		 the process ID plus the lwp ID.  */
	      retval = MERGEPID (pi->pid, proc_get_current_thread (pi));

	      switch (why) {
	      case PR_SIGNALLED:
		wstat = (what << 8) | 0177;
		break;
	      case PR_SYSENTRY:
		if (syscall_is_lwp_exit (pi, what))
		  {
		    printf_filtered ("[%s exited]\n",
				     target_pid_to_str (retval));
		    delete_thread (retval);
		    status->kind = TARGET_WAITKIND_SPURIOUS;
		    return retval;
		  }
		else if (syscall_is_exit (pi, what))
		  {
		    /* Handle SYS_exit call only */
		    /* Stopped at entry to SYS_exit.
		       Make it runnable, resume it, then use
		       the wait system call to get its exit code.
		       Proc_run_process always clears the current
		       fault and signal.
		       Then return its exit status.  */
		    pi->status_valid = 0;
		    wstat = 0;
		    /* FIXME: what we should do is return
		       TARGET_WAITKIND_SPURIOUS.  */
		    if (!proc_run_process (pi, 0, 0))
		      proc_error (pi, "target_wait, run_process", __LINE__);
		    if (attach_flag)
		      {
			/* Don't call wait: simulate waiting for exit,
			   return a "success" exit code.  Bogus: what if
			   it returns something else?  */
			wstat = 0;
			retval = inferior_ptid;  /* ? ? ? */
		      }
		    else
		      {
			int temp = wait (&wstat);

			/* FIXME: shouldn't I make sure I get the right
			   event from the right process?  If (for
			   instance) I have killed an earlier inferior
			   process but failed to clean up after it
			   somehow, I could get its termination event
			   here.  */

			/* If wait returns -1, that's what we return to GDB. */
			if (temp < 0)
			  retval = pid_to_ptid (temp);
		      }
		  }
		else
		  {
		    printf_filtered ("procfs: trapped on entry to ");
		    proc_prettyprint_syscall (proc_what (pi), 0);
		    printf_filtered ("\n");
#ifndef PIOCSSPCACT
		    {
		      long i, nsysargs, *sysargs;

		      if ((nsysargs = proc_nsysarg (pi)) > 0 &&
			  (sysargs  = proc_sysargs (pi)) != NULL)
			{
			  printf_filtered ("%ld syscall arguments:\n", nsysargs);
			  for (i = 0; i < nsysargs; i++)
			    printf_filtered ("#%ld: 0x%08lx\n",
					     i, sysargs[i]);
			}

		    }
#endif
		    if (status)
		      {
			/* How to exit gracefully, returning "unknown event" */
			status->kind = TARGET_WAITKIND_SPURIOUS;
			return inferior_ptid;
		      }
		    else
		      {
			/* How to keep going without returning to wfi: */
			target_resume (ptid, 0, TARGET_SIGNAL_0);
			goto wait_again;
		      }
		  }
		break;
	      case PR_SYSEXIT:
		if (syscall_is_exec (pi, what))
		  {
		    /* Hopefully this is our own "fork-child" execing
		       the real child.  Hoax this event into a trap, and
		       GDB will see the child about to execute its start
		       address. */
		    wstat = (SIGTRAP << 8) | 0177;
		  }
		else if (syscall_is_lwp_create (pi, what))
		  {
		    /*
		     * This syscall is somewhat like fork/exec.
		     * We will get the event twice: once for the parent LWP,
		     * and once for the child.  We should already know about
		     * the parent LWP, but the child will be new to us.  So,
		     * whenever we get this event, if it represents a new
		     * thread, simply add the thread to the list.
		     */

		    /* If not in procinfo list, add it.  */
		    temp_tid = proc_get_current_thread (pi);
		    if (!find_procinfo (pi->pid, temp_tid))
		      create_procinfo  (pi->pid, temp_tid);

		    temp_ptid = MERGEPID (pi->pid, temp_tid);
		    /* If not in GDB's thread list, add it.  */
		    if (!in_thread_list (temp_ptid))
		      {
			printf_filtered ("[New %s]\n",
					 target_pid_to_str (temp_ptid));
			add_thread (temp_ptid);
		      }
		    /* Return to WFI, but tell it to immediately resume. */
		    status->kind = TARGET_WAITKIND_SPURIOUS;
		    return inferior_ptid;
		  }
		else if (syscall_is_lwp_exit (pi, what))
		  {
		    printf_filtered ("[%s exited]\n",
				     target_pid_to_str (retval));
		    delete_thread (retval);
		    status->kind = TARGET_WAITKIND_SPURIOUS;
		    return retval;
		  }
		else if (0)
		  {
		    /* FIXME:  Do we need to handle SYS_sproc,
		       SYS_fork, or SYS_vfork here?  The old procfs
		       seemed to use this event to handle threads on
		       older (non-LWP) systems, where I'm assuming
		       that threads were actually separate processes.
		       Irix, maybe?  Anyway, low priority for now.  */
		  }
		else
		  {
		    printf_filtered ("procfs: trapped on exit from ");
		    proc_prettyprint_syscall (proc_what (pi), 0);
		    printf_filtered ("\n");
#ifndef PIOCSSPCACT
		    {
		      long i, nsysargs, *sysargs;

		      if ((nsysargs = proc_nsysarg (pi)) > 0 &&
			  (sysargs  = proc_sysargs (pi)) != NULL)
			{
			  printf_filtered ("%ld syscall arguments:\n", nsysargs);
			  for (i = 0; i < nsysargs; i++)
			    printf_filtered ("#%ld: 0x%08lx\n",
					     i, sysargs[i]);
			}
		    }
#endif
		    status->kind = TARGET_WAITKIND_SPURIOUS;
		    return inferior_ptid;
		  }
		break;
	      case PR_REQUESTED:
#if 0	/* FIXME */
		wstat = (SIGSTOP << 8) | 0177;
		break;
#else
		if (retry < 5)
		  {
		    printf_filtered ("Retry #%d:\n", retry);
		    pi->status_valid = 0;
		    goto wait_again;
		  }
		else
		  {
		    /* If not in procinfo list, add it.  */
		    temp_tid = proc_get_current_thread (pi);
		    if (!find_procinfo (pi->pid, temp_tid))
		      create_procinfo  (pi->pid, temp_tid);

		    /* If not in GDB's thread list, add it.  */
		    temp_ptid = MERGEPID (pi->pid, temp_tid);
		    if (!in_thread_list (temp_ptid))
		      {
			printf_filtered ("[New %s]\n",
					 target_pid_to_str (temp_ptid));
			add_thread (temp_ptid);
		      }

		    status->kind = TARGET_WAITKIND_STOPPED;
		    status->value.sig = 0;
		    return retval;
		  }
#endif
	      case PR_JOBCONTROL:
		wstat = (what << 8) | 0177;
		break;
	      case PR_FAULTED:
		switch (what) {
#ifdef FLTWATCH
		case FLTWATCH:
		  wstat = (SIGTRAP << 8) | 0177;
		  break;
#endif
#ifdef FLTKWATCH
		case FLTKWATCH:
		  wstat = (SIGTRAP << 8) | 0177;
		  break;
#endif
		  /* FIXME: use si_signo where possible. */
		case FLTPRIV:
#if (FLTILL != FLTPRIV)		/* avoid "duplicate case" error */
		case FLTILL:
#endif
		  wstat = (SIGILL << 8) | 0177;
		  break;
		case FLTBPT:
#if (FLTTRACE != FLTBPT)	/* avoid "duplicate case" error */
		case FLTTRACE:
#endif
		  wstat = (SIGTRAP << 8) | 0177;
		  break;
		case FLTSTACK:
		case FLTACCESS:
#if (FLTBOUNDS != FLTSTACK)	/* avoid "duplicate case" error */
		case FLTBOUNDS:
#endif
		  wstat = (SIGSEGV << 8) | 0177;
		  break;
		case FLTIOVF:
		case FLTIZDIV:
#if (FLTFPE != FLTIOVF)		/* avoid "duplicate case" error */
		case FLTFPE:
#endif
		  wstat = (SIGFPE << 8) | 0177;
		  break;
		case FLTPAGE:		/* Recoverable page fault */
		default:	 /* FIXME: use si_signo if possible for fault */
		  retval = pid_to_ptid (-1);
		  printf_filtered ("procfs:%d -- ", __LINE__);
		  printf_filtered ("child stopped for unknown reason:\n");
		  proc_prettyprint_why (why, what, 1);
		  error ("... giving up...");
		  break;
		}
		break;	/* case PR_FAULTED: */
	      default:	/* switch (why) unmatched */
		printf_filtered ("procfs:%d -- ", __LINE__);
		printf_filtered ("child stopped for unknown reason:\n");
		proc_prettyprint_why (why, what, 1);
		error ("... giving up...");
		break;
	      }
	      /*
	       * Got this far without error:
	       * If retval isn't in the threads database, add it.
	       */
	      if (PIDGET (retval) > 0 &&
		  !ptid_equal (retval, inferior_ptid) &&
		  !in_thread_list (retval))
		{
		  /*
		   * We have a new thread.
		   * We need to add it both to GDB's list and to our own.
		   * If we don't create a procinfo, resume may be unhappy
		   * later.
		   */
		  printf_filtered ("[New %s]\n", target_pid_to_str (retval));
		  add_thread (retval);
		  if (find_procinfo (PIDGET (retval), TIDGET (retval)) == NULL)
		    create_procinfo (PIDGET (retval), TIDGET (retval));

		  /* In addition, it's possible that this is the first
		   * new thread we've seen, in which case we may not
		   * have created entries for inferior_ptid yet.
		   */
		  if (TIDGET (inferior_ptid) != 0)
		    {
		      if (!in_thread_list (inferior_ptid))
			add_thread (inferior_ptid);
		      if (find_procinfo (PIDGET (inferior_ptid),
					 TIDGET (inferior_ptid)) == NULL)
			create_procinfo (PIDGET (inferior_ptid),
					 TIDGET (inferior_ptid));
		    }
		}
	    }
	  else	/* flags do not indicate STOPPED */
	    {
	      /* surely this can't happen... */
	      printf_filtered ("procfs:%d -- process not stopped.\n",
			       __LINE__);
	      proc_prettyprint_flags (flags, 1);
	      error ("procfs: ...giving up...");
	    }
	}

      if (status)
	store_waitstatus (status, wstat);
    }

  return retval;
}

/* Perform a partial transfer to/from the specified object.  For
   memory transfers, fall back to the old memory xfer functions.  */

static LONGEST
procfs_xfer_partial (struct target_ops *ops, enum target_object object,
		     const char *annex, void *readbuf,
		     const void *writebuf, ULONGEST offset, LONGEST len)
{
  switch (object)
    {
    case TARGET_OBJECT_MEMORY:
      if (readbuf)
	return (*ops->to_xfer_memory) (offset, readbuf, len, 0/*write*/,
				       NULL, ops);
      if (writebuf)
	return (*ops->to_xfer_memory) (offset, readbuf, len, 1/*write*/,
				       NULL, ops);
      return -1;

#ifdef NEW_PROC_API
    case TARGET_OBJECT_AUXV:
      return procfs_xfer_auxv (ops, object, annex, readbuf, writebuf,
			       offset, len);
#endif

    default:
      if (ops->beneath != NULL)
	return ops->beneath->to_xfer_partial (ops->beneath, object, annex,
					      readbuf, writebuf, offset, len);
      return -1;
    }
}


/* Transfer LEN bytes between GDB address MYADDR and target address
   MEMADDR.  If DOWRITE is non-zero, transfer them to the target,
   otherwise transfer them from the target.  TARGET is unused.

   The return value is 0 if an error occurred or no bytes were
   transferred.  Otherwise, it will be a positive value which
   indicates the number of bytes transferred between gdb and the
   target.  (Note that the interface also makes provisions for
   negative values, but this capability isn't implemented here.) */

static int
procfs_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int dowrite,
		    struct mem_attrib *attrib, struct target_ops *target)
{
  procinfo *pi;
  int nbytes = 0;

  /* Find procinfo for main process */
  pi = find_procinfo_or_die (PIDGET (inferior_ptid), 0);
  if (pi->as_fd == 0 &&
      open_procinfo_files (pi, FD_AS) == 0)
    {
      proc_warn (pi, "xfer_memory, open_proc_files", __LINE__);
      return 0;
    }

  if (lseek (pi->as_fd, (off_t) memaddr, SEEK_SET) == (off_t) memaddr)
    {
      if (dowrite)
	{
#ifdef NEW_PROC_API
	  PROCFS_NOTE ("write memory: ");
#else
	  PROCFS_NOTE ("write memory: \n");
#endif
	  nbytes = write (pi->as_fd, myaddr, len);
	}
      else
	{
	  PROCFS_NOTE ("read  memory: \n");
	  nbytes = read (pi->as_fd, myaddr, len);
	}
      if (nbytes < 0)
	{
	  nbytes = 0;
	}
    }
  return nbytes;
}

/*
 * Function: invalidate_cache
 *
 * Called by target_resume before making child runnable.
 * Mark cached registers and status's invalid.
 * If there are "dirty" caches that need to be written back
 * to the child process, do that.
 *
 * File descriptors are also cached.
 * As they are a limited resource, we cannot hold onto them indefinitely.
 * However, as they are expensive to open, we don't want to throw them
 * away indescriminately either.  As a compromise, we will keep the
 * file descriptors for the parent process, but discard any file
 * descriptors we may have accumulated for the threads.
 *
 * Return value:
 * As this function is called by iterate_over_threads, it always
 * returns zero (so that iterate_over_threads will keep iterating).
 */


static int
invalidate_cache (procinfo *parent, procinfo *pi, void *ptr)
{
  /*
   * About to run the child; invalidate caches and do any other cleanup.
   */

#if 0
  if (pi->gregs_dirty)
    if (parent == NULL ||
	proc_get_current_thread (parent) != pi->tid)
      if (!proc_set_gregs (pi))	/* flush gregs cache */
	proc_warn (pi, "target_resume, set_gregs",
		   __LINE__);
  if (FP0_REGNUM >= 0)
    if (pi->fpregs_dirty)
      if (parent == NULL ||
	  proc_get_current_thread (parent) != pi->tid)
	if (!proc_set_fpregs (pi))	/* flush fpregs cache */
	  proc_warn (pi, "target_resume, set_fpregs",
		     __LINE__);
#endif

  if (parent != NULL)
    {
      /* The presence of a parent indicates that this is an LWP.
	 Close any file descriptors that it might have open.
	 We don't do this to the master (parent) procinfo.  */

      close_procinfo_files (pi);
    }
  pi->gregs_valid   = 0;
  pi->fpregs_valid  = 0;
#if 0
  pi->gregs_dirty   = 0;
  pi->fpregs_dirty  = 0;
#endif
  pi->status_valid  = 0;
  pi->threads_valid = 0;

  return 0;
}

#if 0
/*
 * Function: make_signal_thread_runnable
 *
 * A callback function for iterate_over_threads.
 * Find the asynchronous signal thread, and make it runnable.
 * See if that helps matters any.
 */

static int
make_signal_thread_runnable (procinfo *process, procinfo *pi, void *ptr)
{
#ifdef PR_ASLWP
  if (proc_flags (pi) & PR_ASLWP)
    {
      if (!proc_run_process (pi, 0, -1))
	proc_error (pi, "make_signal_thread_runnable", __LINE__);
      return 1;
    }
#endif
  return 0;
}
#endif

/*
 * Function: target_resume
 *
 * Make the child process runnable.  Normally we will then call
 * procfs_wait and wait for it to stop again (unles gdb is async).
 *
 * Arguments:
 *  step:  if true, then arrange for the child to stop again
 *         after executing a single instruction.
 *  signo: if zero, then cancel any pending signal.
 *         If non-zero, then arrange for the indicated signal
 *         to be delivered to the child when it runs.
 *  pid:   if -1, then allow any child thread to run.
 *         if non-zero, then allow only the indicated thread to run.
 *******   (not implemented yet)
 */

static void
procfs_resume (ptid_t ptid, int step, enum target_signal signo)
{
  procinfo *pi, *thread;
  int native_signo;

  /* 2.1:
     prrun.prflags |= PRSVADDR;
     prrun.pr_vaddr = $PC;	   set resume address
     prrun.prflags |= PRSTRACE;    trace signals in pr_trace (all)
     prrun.prflags |= PRSFAULT;    trace faults in pr_fault (all but PAGE)
     prrun.prflags |= PRCFAULT;    clear current fault.

     PRSTRACE and PRSFAULT can be done by other means
     	(proc_trace_signals, proc_trace_faults)
     PRSVADDR is unnecessary.
     PRCFAULT may be replaced by a PIOCCFAULT call (proc_clear_current_fault)
     This basically leaves PRSTEP and PRCSIG.
     PRCSIG is like PIOCSSIG (proc_clear_current_signal).
     So basically PR_STEP is the sole argument that must be passed
     to proc_run_process (for use in the prrun struct by ioctl). */

  /* Find procinfo for main process */
  pi = find_procinfo_or_die (PIDGET (inferior_ptid), 0);

  /* First cut: ignore pid argument */
  errno = 0;

  /* Convert signal to host numbering.  */
  if (signo == 0 ||
      (signo == TARGET_SIGNAL_STOP && pi->ignore_next_sigstop))
    native_signo = 0;
  else
    native_signo = target_signal_to_host (signo);

  pi->ignore_next_sigstop = 0;

  /* Running the process voids all cached registers and status. */
  /* Void the threads' caches first */
  proc_iterate_over_threads (pi, invalidate_cache, NULL);
  /* Void the process procinfo's caches.  */
  invalidate_cache (NULL, pi, NULL);

  if (PIDGET (ptid) != -1)
    {
      /* Resume a specific thread, presumably suppressing the others. */
      thread = find_procinfo (PIDGET (ptid), TIDGET (ptid));
      if (thread != NULL)
	{
	  if (thread->tid != 0)
	    {
	      /* We're to resume a specific thread, and not the others.
	       * Set the child process's PR_ASYNC flag.
	       */
#ifdef PR_ASYNC
	      if (!proc_set_async (pi))
		proc_error (pi, "target_resume, set_async", __LINE__);
#endif
#if 0
	      proc_iterate_over_threads (pi,
					 make_signal_thread_runnable,
					 NULL);
#endif
	      pi = thread;	/* substitute the thread's procinfo for run */
	    }
	}
    }

  if (!proc_run_process (pi, step, native_signo))
    {
      if (errno == EBUSY)
	warning ("resume: target already running.  Pretend to resume, and hope for the best!\n");
      else
	proc_error (pi, "target_resume", __LINE__);
    }
}

/*
 * Function: register_gdb_signals
 *
 * Traverse the list of signals that GDB knows about
 * (see "handle" command), and arrange for the target
 * to be stopped or not, according to these settings.
 *
 * Returns non-zero for success, zero for failure.
 */

static int
register_gdb_signals (procinfo *pi, gdb_sigset_t *signals)
{
  int signo;

  for (signo = 0; signo < NSIG; signo ++)
    if (signal_stop_state  (target_signal_from_host (signo)) == 0 &&
	signal_print_state (target_signal_from_host (signo)) == 0 &&
	signal_pass_state  (target_signal_from_host (signo)) == 1)
      prdelset (signals, signo);
    else
      praddset (signals, signo);

  return proc_set_traced_signals (pi, signals);
}

/*
 * Function: target_notice_signals
 *
 * Set up to trace signals in the child process.
 */

static void
procfs_notice_signals (ptid_t ptid)
{
  gdb_sigset_t signals;
  procinfo *pi = find_procinfo_or_die (PIDGET (ptid), 0);

  if (proc_get_traced_signals (pi, &signals) &&
      register_gdb_signals    (pi, &signals))
    return;
  else
    proc_error (pi, "notice_signals", __LINE__);
}

/*
 * Function: target_files_info
 *
 * Print status information about the child process.
 */

static void
procfs_files_info (struct target_ops *ignore)
{
  printf_filtered ("\tUsing the running image of %s %s via /proc.\n",
		   attach_flag? "attached": "child",
		   target_pid_to_str (inferior_ptid));
}

/*
 * Function: target_open
 *
 * A dummy: you don't open procfs.
 */

static void
procfs_open (char *args, int from_tty)
{
  error ("Use the \"run\" command to start a Unix child process.");
}

/*
 * Function: target_can_run
 *
 * This tells GDB that this target vector can be invoked
 * for "run" or "attach".
 */

int procfs_suppress_run = 0;	/* Non-zero if procfs should pretend not to
				   be a runnable target.  Used by targets
				   that can sit atop procfs, such as solaris
				   thread support.  */


static int
procfs_can_run (void)
{
  /* This variable is controlled by modules that sit atop procfs that
     may layer their own process structure atop that provided here.
     sol-thread.c does this because of the Solaris two-level thread
     model.  */

  /* NOTE: possibly obsolete -- use the thread_stratum approach instead. */

  return !procfs_suppress_run;
}

/*
 * Function: target_stop
 *
 * Stop the child process asynchronously, as when the
 * gdb user types control-c or presses a "stop" button.
 *
 * Works by sending kill(SIGINT) to the child's process group.
 */

static void
procfs_stop (void)
{
  kill (-inferior_process_group, SIGINT);
}

/*
 * Function: unconditionally_kill_inferior
 *
 * Make it die.  Wait for it to die.  Clean up after it.
 * Note: this should only be applied to the real process,
 * not to an LWP, because of the check for parent-process.
 * If we need this to work for an LWP, it needs some more logic.
 */

static void
unconditionally_kill_inferior (procinfo *pi)
{
  int parent_pid;

  parent_pid = proc_parent_pid (pi);
#ifdef PROCFS_NEED_CLEAR_CURSIG_FOR_KILL
  /* FIXME: use access functions */
  /* Alpha OSF/1-3.x procfs needs a clear of the current signal
     before the PIOCKILL, otherwise it might generate a corrupted core
     file for the inferior.  */
  if (ioctl (pi->ctl_fd, PIOCSSIG, NULL) < 0)
    {
      printf_filtered ("unconditionally_kill: SSIG failed!\n");
    }
#endif
#ifdef PROCFS_NEED_PIOCSSIG_FOR_KILL
  /* Alpha OSF/1-2.x procfs needs a PIOCSSIG call with a SIGKILL signal
     to kill the inferior, otherwise it might remain stopped with a
     pending SIGKILL.
     We do not check the result of the PIOCSSIG, the inferior might have
     died already.  */
  {
    gdb_siginfo_t newsiginfo;

    memset ((char *) &newsiginfo, 0, sizeof (newsiginfo));
    newsiginfo.si_signo = SIGKILL;
    newsiginfo.si_code = 0;
    newsiginfo.si_errno = 0;
    newsiginfo.si_pid = getpid ();
    newsiginfo.si_uid = getuid ();
    /* FIXME: use proc_set_current_signal */
    ioctl (pi->ctl_fd, PIOCSSIG, &newsiginfo);
  }
#else /* PROCFS_NEED_PIOCSSIG_FOR_KILL */
  if (!proc_kill (pi, SIGKILL))
    proc_error (pi, "unconditionally_kill, proc_kill", __LINE__);
#endif /* PROCFS_NEED_PIOCSSIG_FOR_KILL */
  destroy_procinfo (pi);

  /* If pi is GDB's child, wait for it to die.  */
  if (parent_pid == getpid ())
    /* FIXME: should we use waitpid to make sure we get the right event?
       Should we check the returned event?  */
    {
#if 0
      int status, ret;

      ret = waitpid (pi->pid, &status, 0);
#else
      wait (NULL);
#endif
    }
}

/*
 * Function: target_kill_inferior
 *
 * We're done debugging it, and we want it to go away.
 * Then we want GDB to forget all about it.
 */

static void
procfs_kill_inferior (void)
{
  if (!ptid_equal (inferior_ptid, null_ptid)) /* ? */
    {
      /* Find procinfo for main process */
      procinfo *pi = find_procinfo (PIDGET (inferior_ptid), 0);

      if (pi)
	unconditionally_kill_inferior (pi);
      target_mourn_inferior ();
    }
}

/*
 * Function: target_mourn_inferior
 *
 * Forget we ever debugged this thing!
 */

static void
procfs_mourn_inferior (void)
{
  procinfo *pi;

  if (!ptid_equal (inferior_ptid, null_ptid))
    {
      /* Find procinfo for main process */
      pi = find_procinfo (PIDGET (inferior_ptid), 0);
      if (pi)
	destroy_procinfo (pi);
    }
  unpush_target (&procfs_ops);
  generic_mourn_inferior ();
}

/*
 * Function: init_inferior
 *
 * When GDB forks to create a runnable inferior process,
 * this function is called on the parent side of the fork.
 * It's job is to do whatever is necessary to make the child
 * ready to be debugged, and then wait for the child to synchronize.
 */

static void
procfs_init_inferior (int pid)
{
  procinfo *pi;
  gdb_sigset_t signals;
  int fail;

  /* This routine called on the parent side (GDB side)
     after GDB forks the inferior.  */

  push_target (&procfs_ops);

  if ((pi = create_procinfo (pid, 0)) == NULL)
    perror ("procfs: out of memory in 'init_inferior'");

  if (!open_procinfo_files (pi, FD_CTL))
    proc_error (pi, "init_inferior, open_proc_files", __LINE__);

  /*
    xmalloc			// done
    open_procinfo_files		// done
    link list			// done
    prfillset (trace)
    procfs_notice_signals
    prfillset (fault)
    prdelset (FLTPAGE)
    PIOCWSTOP
    PIOCSFAULT
    */

  /* If not stopped yet, wait for it to stop. */
  if (!(proc_flags (pi) & PR_STOPPED) &&
      !(proc_wait_for_stop (pi)))
    dead_procinfo (pi, "init_inferior: wait_for_stop failed", KILL);

  /* Save some of the /proc state to be restored if we detach.  */
  /* FIXME: Why?  In case another debugger was debugging it?
     We're it's parent, for Ghu's sake! */
  if (!proc_get_traced_signals  (pi, &pi->saved_sigset))
    proc_error (pi, "init_inferior, get_traced_signals", __LINE__);
  if (!proc_get_held_signals    (pi, &pi->saved_sighold))
    proc_error (pi, "init_inferior, get_held_signals", __LINE__);
  if (!proc_get_traced_faults   (pi, &pi->saved_fltset))
    proc_error (pi, "init_inferior, get_traced_faults", __LINE__);
  if (!proc_get_traced_sysentry (pi, pi->saved_entryset))
    proc_error (pi, "init_inferior, get_traced_sysentry", __LINE__);
  if (!proc_get_traced_sysexit  (pi, pi->saved_exitset))
    proc_error (pi, "init_inferior, get_traced_sysexit", __LINE__);

  /* Register to trace selected signals in the child. */
  prfillset (&signals);
  if (!register_gdb_signals (pi, &signals))
    proc_error (pi, "init_inferior, register_signals", __LINE__);

  if ((fail = procfs_debug_inferior (pi)) != 0)
    proc_error (pi, "init_inferior (procfs_debug_inferior)", fail);

  /* FIXME: logically, we should really be turning OFF run-on-last-close,
     and possibly even turning ON kill-on-last-close at this point.  But
     I can't make that change without careful testing which I don't have
     time to do right now...  */
  /* Turn on run-on-last-close flag so that the child
     will die if GDB goes away for some reason.  */
  if (!proc_set_run_on_last_close (pi))
    proc_error (pi, "init_inferior, set_RLC", __LINE__);

  /* The 'process ID' we return to GDB is composed of
     the actual process ID plus the lwp ID. */
  inferior_ptid = MERGEPID (pi->pid, proc_get_current_thread (pi));

  /* Typically two, one trap to exec the shell, one to exec the
     program being debugged.  Defined by "inferior.h".  */
  startup_inferior (START_INFERIOR_TRAPS_EXPECTED);
}

/*
 * Function: set_exec_trap
 *
 * When GDB forks to create a new process, this function is called
 * on the child side of the fork before GDB exec's the user program.
 * Its job is to make the child minimally debuggable, so that the
 * parent GDB process can connect to the child and take over.
 * This function should do only the minimum to make that possible,
 * and to synchronize with the parent process.  The parent process
 * should take care of the details.
 */

static void
procfs_set_exec_trap (void)
{
  /* This routine called on the child side (inferior side)
     after GDB forks the inferior.  It must use only local variables,
     because it may be sharing data space with its parent.  */

  procinfo *pi;
  sysset_t *exitset;

  if ((pi = create_procinfo (getpid (), 0)) == NULL)
    perror_with_name ("procfs: create_procinfo failed in child.");

  if (open_procinfo_files (pi, FD_CTL) == 0)
    {
      proc_warn (pi, "set_exec_trap, open_proc_files", __LINE__);
      gdb_flush (gdb_stderr);
      /* no need to call "dead_procinfo", because we're going to exit. */
      _exit (127);
    }

#ifdef PRFS_STOPEXEC	/* defined on OSF */
  /* OSF method for tracing exec syscalls.  Quoting:
     Under Alpha OSF/1 we have to use a PIOCSSPCACT ioctl to trace
     exits from exec system calls because of the user level loader.  */
  /* FIXME: make nice and maybe move into an access function. */
  {
    int prfs_flags;

    if (ioctl (pi->ctl_fd, PIOCGSPCACT, &prfs_flags) < 0)
      {
	proc_warn (pi, "set_exec_trap (PIOCGSPCACT)", __LINE__);
	gdb_flush (gdb_stderr);
	_exit (127);
      }
    prfs_flags |= PRFS_STOPEXEC;

    if (ioctl (pi->ctl_fd, PIOCSSPCACT, &prfs_flags) < 0)
      {
	proc_warn (pi, "set_exec_trap (PIOCSSPCACT)", __LINE__);
	gdb_flush (gdb_stderr);
	_exit (127);
      }
  }
#else /* not PRFS_STOPEXEC */
  /* Everyone else's (except OSF) method for tracing exec syscalls */
  /* GW: Rationale...
     Not all systems with /proc have all the exec* syscalls with the same
     names.  On the SGI, for example, there is no SYS_exec, but there
     *is* a SYS_execv.  So, we try to account for that. */

  exitset = sysset_t_alloc (pi);
  gdb_premptysysset (exitset);
#ifdef SYS_exec
  gdb_praddsysset (exitset, SYS_exec);
#endif
#ifdef SYS_execve
  gdb_praddsysset (exitset, SYS_execve);
#endif
#ifdef SYS_execv
  gdb_praddsysset (exitset, SYS_execv);
#endif
#ifdef DYNAMIC_SYSCALLS
  {
    int callnum = find_syscall (pi, "execve");

    if (callnum >= 0)
      gdb_praddsysset (exitset, callnum);

    callnum = find_syscall (pi, "ra_execve");
    if (callnum >= 0)
      gdb_praddsysset (exitset, callnum);
  }
#endif /* DYNAMIC_SYSCALLS */

  if (!proc_set_traced_sysexit (pi, exitset))
    {
      proc_warn (pi, "set_exec_trap, set_traced_sysexit", __LINE__);
      gdb_flush (gdb_stderr);
      _exit (127);
    }
#endif /* PRFS_STOPEXEC */

  /* FIXME: should this be done in the parent instead? */
  /* Turn off inherit on fork flag so that all grand-children
     of gdb start with tracing flags cleared.  */
  if (!proc_unset_inherit_on_fork (pi))
    proc_warn (pi, "set_exec_trap, unset_inherit", __LINE__);

  /* Turn off run on last close flag, so that the child process
     cannot run away just because we close our handle on it.
     We want it to wait for the parent to attach.  */
  if (!proc_unset_run_on_last_close (pi))
    proc_warn (pi, "set_exec_trap, unset_RLC", __LINE__);

  /* FIXME: No need to destroy the procinfo --
     we have our own address space, and we're about to do an exec! */
  /*destroy_procinfo (pi);*/
}

/*
 * Function: create_inferior
 *
 * This function is called BEFORE gdb forks the inferior process.
 * Its only real responsibility is to set things up for the fork,
 * and tell GDB which two functions to call after the fork (one
 * for the parent, and one for the child).
 *
 * This function does a complicated search for a unix shell program,
 * which it then uses to parse arguments and environment variables
 * to be sent to the child.  I wonder whether this code could not
 * be abstracted out and shared with other unix targets such as
 * infptrace?
 */

static void
procfs_create_inferior (char *exec_file, char *allargs, char **env)
{
  char *shell_file = getenv ("SHELL");
  char *tryname;
  if (shell_file != NULL && strchr (shell_file, '/') == NULL)
    {

      /* We will be looking down the PATH to find shell_file.  If we
	 just do this the normal way (via execlp, which operates by
	 attempting an exec for each element of the PATH until it
	 finds one which succeeds), then there will be an exec for
	 each failed attempt, each of which will cause a PR_SYSEXIT
	 stop, and we won't know how to distinguish the PR_SYSEXIT's
	 for these failed execs with the ones for successful execs
	 (whether the exec has succeeded is stored at that time in the
	 carry bit or some such architecture-specific and
	 non-ABI-specified place).

	 So I can't think of anything better than to search the PATH
	 now.  This has several disadvantages: (1) There is a race
	 condition; if we find a file now and it is deleted before we
	 exec it, we lose, even if the deletion leaves a valid file
	 further down in the PATH, (2) there is no way to know exactly
	 what an executable (in the sense of "capable of being
	 exec'd") file is.  Using access() loses because it may lose
	 if the caller is the superuser; failing to use it loses if
	 there are ACLs or some such.  */

      char *p;
      char *p1;
      /* FIXME-maybe: might want "set path" command so user can change what
	 path is used from within GDB.  */
      char *path = getenv ("PATH");
      int len;
      struct stat statbuf;

      if (path == NULL)
	path = "/bin:/usr/bin";

      tryname = alloca (strlen (path) + strlen (shell_file) + 2);
      for (p = path; p != NULL; p = p1 ? p1 + 1: NULL)
	{
	  p1 = strchr (p, ':');
	  if (p1 != NULL)
	    len = p1 - p;
	  else
	    len = strlen (p);
	  strncpy (tryname, p, len);
	  tryname[len] = '\0';
	  strcat (tryname, "/");
	  strcat (tryname, shell_file);
	  if (access (tryname, X_OK) < 0)
	    continue;
	  if (stat (tryname, &statbuf) < 0)
	    continue;
	  if (!S_ISREG (statbuf.st_mode))
	    /* We certainly need to reject directories.  I'm not quite
	       as sure about FIFOs, sockets, etc., but I kind of doubt
	       that people want to exec() these things.  */
	    continue;
	  break;
	}
      if (p == NULL)
	/* Not found.  This must be an error rather than merely passing
	   the file to execlp(), because execlp() would try all the
	   exec()s, causing GDB to get confused.  */
	error ("procfs:%d -- Can't find shell %s in PATH",
	       __LINE__, shell_file);

      shell_file = tryname;
    }

  fork_inferior (exec_file, allargs, env, procfs_set_exec_trap,
		 procfs_init_inferior, NULL, shell_file);

  /* We are at the first instruction we care about.  */
  /* Pedal to the metal... */

  proceed ((CORE_ADDR) -1, TARGET_SIGNAL_0, 0);
}

/*
 * Function: notice_thread
 *
 * Callback for find_new_threads.
 * Calls "add_thread".
 */

static int
procfs_notice_thread (procinfo *pi, procinfo *thread, void *ptr)
{
  ptid_t gdb_threadid = MERGEPID (pi->pid, thread->tid);

  if (!in_thread_list (gdb_threadid))
    add_thread (gdb_threadid);

  return 0;
}

/*
 * Function: target_find_new_threads
 *
 * Query all the threads that the target knows about,
 * and give them back to GDB to add to its list.
 */

void
procfs_find_new_threads (void)
{
  procinfo *pi;

  /* Find procinfo for main process */
  pi = find_procinfo_or_die (PIDGET (inferior_ptid), 0);
  proc_update_threads (pi);
  proc_iterate_over_threads (pi, procfs_notice_thread, NULL);
}

/*
 * Function: target_thread_alive
 *
 * Return true if the thread is still 'alive'.
 *
 * This guy doesn't really seem to be doing his job.
 * Got to investigate how to tell when a thread is really gone.
 */

static int
procfs_thread_alive (ptid_t ptid)
{
  int proc, thread;
  procinfo *pi;

  proc    = PIDGET (ptid);
  thread  = TIDGET (ptid);
  /* If I don't know it, it ain't alive! */
  if ((pi = find_procinfo (proc, thread)) == NULL)
    return 0;

  /* If I can't get its status, it ain't alive!
     What's more, I need to forget about it!  */
  if (!proc_get_status (pi))
    {
      destroy_procinfo (pi);
      return 0;
    }
  /* I couldn't have got its status if it weren't alive, so it's alive.  */
  return 1;
}

/*
 * Function: target_pid_to_str
 *
 * Return a string to be used to identify the thread in
 * the "info threads" display.
 */

char *
procfs_pid_to_str (ptid_t ptid)
{
  static char buf[80];
  int proc, thread;
  procinfo *pi;

  proc    = PIDGET (ptid);
  thread  = TIDGET (ptid);
  pi      = find_procinfo (proc, thread);

  if (thread == 0)
    sprintf (buf, "Process %d", proc);
  else
    sprintf (buf, "LWP %d", thread);
  return &buf[0];
}

/*
 * Function: procfs_set_watchpoint
 * Insert a watchpoint
 */

int
procfs_set_watchpoint (ptid_t ptid, CORE_ADDR addr, int len, int rwflag,
                       int after)
{
#ifndef UNIXWARE
#ifndef AIX5
  int       pflags = 0;
  procinfo *pi;

  pi = find_procinfo_or_die (PIDGET (ptid) == -1 ?
			     PIDGET (inferior_ptid) : PIDGET (ptid), 0);

  /* Translate from GDB's flags to /proc's */
  if (len > 0)	/* len == 0 means delete watchpoint */
    {
      switch (rwflag) {		/* FIXME: need an enum! */
      case hw_write:		/* default watchpoint (write) */
	pflags = WRITE_WATCHFLAG;
	break;
      case hw_read:		/* read watchpoint */
	pflags = READ_WATCHFLAG;
	break;
      case hw_access:		/* access watchpoint */
	pflags = READ_WATCHFLAG | WRITE_WATCHFLAG;
	break;
      case hw_execute:		/* execution HW breakpoint */
	pflags = EXEC_WATCHFLAG;
	break;
      default:			/* Something weird.  Return error. */
	return -1;
      }
      if (after)		/* Stop after r/w access is completed. */
	pflags |= AFTER_WATCHFLAG;
    }

  if (!proc_set_watchpoint (pi, addr, len, pflags))
    {
      if (errno == E2BIG)	/* Typical error for no resources */
	return -1;		/* fail */
      /* GDB may try to remove the same watchpoint twice.
	 If a remove request returns no match, don't error.  */
      if (errno == ESRCH && len == 0)
	return 0;		/* ignore */
      proc_error (pi, "set_watchpoint", __LINE__);
    }
#endif /* AIX5 */
#endif /* UNIXWARE */
  return 0;
}

/* Return non-zero if we can set a hardware watchpoint of type TYPE.  TYPE
   is one of bp_hardware_watchpoint, bp_read_watchpoint, bp_write_watchpoint,
   or bp_hardware_watchpoint.  CNT is the number of watchpoints used so
   far.

   Note:  procfs_can_use_hw_breakpoint() is not yet used by all
   procfs.c targets due to the fact that some of them still define
   TARGET_CAN_USE_HARDWARE_WATCHPOINT.  */

static int
procfs_can_use_hw_breakpoint (int type, int cnt, int othertype)
{
#ifndef TARGET_HAS_HARDWARE_WATCHPOINTS
  return 0;
#else
  /* Due to the way that proc_set_watchpoint() is implemented, host
     and target pointers must be of the same size.  If they are not,
     we can't use hardware watchpoints.  This limitation is due to the
     fact that proc_set_watchpoint() calls
     procfs_address_to_host_pointer(); a close inspection of
     procfs_address_to_host_pointer will reveal that an internal error
     will be generated when the host and target pointer sizes are
     different.  */
  if (sizeof (void *) != TYPE_LENGTH (builtin_type_void_data_ptr))
    return 0;

  /* Other tests here???  */

  return 1;
#endif
}

/*
 * Function: stopped_by_watchpoint
 *
 * Returns non-zero if process is stopped on a hardware watchpoint fault,
 * else returns zero.
 */

int
procfs_stopped_by_watchpoint (ptid_t ptid)
{
  procinfo *pi;

  pi = find_procinfo_or_die (PIDGET (ptid) == -1 ?
			     PIDGET (inferior_ptid) : PIDGET (ptid), 0);

  if (!pi)	/* If no process, then not stopped by watchpoint!  */
    return 0;

  if (proc_flags (pi) & (PR_STOPPED | PR_ISTOP))
    {
      if (proc_why (pi) == PR_FAULTED)
	{
#ifdef FLTWATCH
	  if (proc_what (pi) == FLTWATCH)
	    return 1;
#endif
#ifdef FLTKWATCH
	  if (proc_what (pi) == FLTKWATCH)
	    return 1;
#endif
	}
    }
  return 0;
}

#ifdef TM_I386SOL2_H
/*
 * Function: procfs_find_LDT_entry
 *
 * Input:
 *   ptid_t ptid;	// The GDB-style pid-plus-LWP.
 *
 * Return:
 *   pointer to the corresponding LDT entry.
 */

struct ssd *
procfs_find_LDT_entry (ptid_t ptid)
{
  gdb_gregset_t *gregs;
  int            key;
  procinfo      *pi;

  /* Find procinfo for the lwp. */
  if ((pi = find_procinfo (PIDGET (ptid), TIDGET (ptid))) == NULL)
    {
      warning ("procfs_find_LDT_entry: could not find procinfo for %d:%d.",
	       PIDGET (ptid), TIDGET (ptid));
      return NULL;
    }
  /* get its general registers. */
  if ((gregs = proc_get_gregs (pi)) == NULL)
    {
      warning ("procfs_find_LDT_entry: could not read gregs for %d:%d.",
	       PIDGET (ptid), TIDGET (ptid));
      return NULL;
    }
  /* Now extract the GS register's lower 16 bits. */
  key = (*gregs)[GS] & 0xffff;

  /* Find the matching entry and return it. */
  return proc_get_LDT_entry (pi, key);
}
#endif /* TM_I386SOL2_H */

/*
 * Memory Mappings Functions:
 */

/*
 * Function: iterate_over_mappings
 *
 * Call a callback function once for each mapping, passing it the mapping,
 * an optional secondary callback function, and some optional opaque data.
 * Quit and return the first non-zero value returned from the callback.
 *
 * Arguments:
 *   pi   -- procinfo struct for the process to be mapped.
 *   func -- callback function to be called by this iterator.
 *   data -- optional opaque data to be passed to the callback function.
 *   child_func -- optional secondary function pointer to be passed
 *                 to the child function.
 *
 * Return: First non-zero return value from the callback function,
 *         or zero.
 */

static int
iterate_over_mappings (procinfo *pi, int (*child_func) (), void *data,
		       int (*func) (struct prmap *map,
				    int (*child_func) (),
				    void *data))
{
  char pathname[MAX_PROC_NAME_SIZE];
  struct prmap *prmaps;
  struct prmap *prmap;
  int funcstat;
  int map_fd;
  int nmap;
#ifdef NEW_PROC_API
  struct stat sbuf;
#endif

  /* Get the number of mappings, allocate space,
     and read the mappings into prmaps.  */
#ifdef NEW_PROC_API
  /* Open map fd. */
  sprintf (pathname, "/proc/%d/map", pi->pid);
  if ((map_fd = open (pathname, O_RDONLY)) < 0)
    proc_error (pi, "iterate_over_mappings (open)", __LINE__);

  /* Make sure it gets closed again. */
  make_cleanup_close (map_fd);

  /* Use stat to determine the file size, and compute
     the number of prmap_t objects it contains.  */
  if (fstat (map_fd, &sbuf) != 0)
    proc_error (pi, "iterate_over_mappings (fstat)", __LINE__);

  nmap = sbuf.st_size / sizeof (prmap_t);
  prmaps = (struct prmap *) alloca ((nmap + 1) * sizeof (*prmaps));
  if (read (map_fd, (char *) prmaps, nmap * sizeof (*prmaps))
      != (nmap * sizeof (*prmaps)))
    proc_error (pi, "iterate_over_mappings (read)", __LINE__);
#else
  /* Use ioctl command PIOCNMAP to get number of mappings.  */
  if (ioctl (pi->ctl_fd, PIOCNMAP, &nmap) != 0)
    proc_error (pi, "iterate_over_mappings (PIOCNMAP)", __LINE__);

  prmaps = (struct prmap *) alloca ((nmap + 1) * sizeof (*prmaps));
  if (ioctl (pi->ctl_fd, PIOCMAP, prmaps) != 0)
    proc_error (pi, "iterate_over_mappings (PIOCMAP)", __LINE__);
#endif

  for (prmap = prmaps; nmap > 0; prmap++, nmap--)
    if ((funcstat = (*func) (prmap, child_func, data)) != 0)
      return funcstat;

  return 0;
}

/*
 * Function: solib_mappings_callback
 *
 * Calls the supplied callback function once for each mapped address
 * space in the process.  The callback function  receives an open
 * file descriptor for the file corresponding to that mapped
 * address space (if there is one), and the base address of the
 * mapped space.  Quit when the callback function returns a
 * nonzero value, or at teh end of the mappings.
 *
 * Returns: the first non-zero return value of the callback function,
 * or zero.
 */

int solib_mappings_callback (struct prmap *map,
			     int (*func) (int, CORE_ADDR),
			     void *data)
{
  procinfo *pi = data;
  int fd;

#ifdef NEW_PROC_API
  char name[MAX_PROC_NAME_SIZE + sizeof (map->pr_mapname)];

  if (map->pr_vaddr == 0 && map->pr_size == 0)
    return -1;		/* sanity */

  if (map->pr_mapname[0] == 0)
    {
      fd = -1;	/* no map file */
    }
  else
    {
      sprintf (name, "/proc/%d/object/%s", pi->pid, map->pr_mapname);
      /* Note: caller's responsibility to close this fd!  */
      fd = open_with_retry (name, O_RDONLY);
      /* Note: we don't test the above call for failure;
	 we just pass the FD on as given.  Sometimes there is
	 no file, so the open may return failure, but that's
	 not a problem.  */
    }
#else
  fd = ioctl (pi->ctl_fd, PIOCOPENM, &map->pr_vaddr);
  /* Note: we don't test the above call for failure;
     we just pass the FD on as given.  Sometimes there is
     no file, so the ioctl may return failure, but that's
     not a problem.  */
#endif
  return (*func) (fd, (CORE_ADDR) map->pr_vaddr);
}

/*
 * Function: proc_iterate_over_mappings
 *
 * Uses the unified "iterate_over_mappings" function
 * to implement the exported interface to solib-svr4.c.
 *
 * Given a pointer to a function, call that function once for every
 * mapped address space in the process.  The callback function
 * receives an open file descriptor for the file corresponding to
 * that mapped address space (if there is one), and the base address
 * of the mapped space.  Quit when the callback function returns a
 * nonzero value, or at teh end of the mappings.
 *
 * Returns: the first non-zero return value of the callback function,
 * or zero.
 */

int
proc_iterate_over_mappings (int (*func) (int, CORE_ADDR))
{
  procinfo *pi = find_procinfo_or_die (PIDGET (inferior_ptid), 0);

  return iterate_over_mappings (pi, func, pi, solib_mappings_callback);
}

/*
 * Function: find_memory_regions_callback
 *
 * Implements the to_find_memory_regions method.
 * Calls an external function for each memory region.
 * External function will have the signiture:
 *
 *   int callback (CORE_ADDR vaddr,
 *                 unsigned long size,
 *                 int read, int write, int execute,
 *                 void *data);
 *
 * Returns the integer value returned by the callback.
 */

static int
find_memory_regions_callback (struct prmap *map,
			      int (*func) (CORE_ADDR,
					   unsigned long,
					   int, int, int,
					   void *),
			      void *data)
{
  return (*func) ((CORE_ADDR) map->pr_vaddr,
		  map->pr_size,
		  (map->pr_mflags & MA_READ) != 0,
		  (map->pr_mflags & MA_WRITE) != 0,
		  (map->pr_mflags & MA_EXEC) != 0,
		  data);
}

/*
 * Function: proc_find_memory_regions
 *
 * External interface.  Calls a callback function once for each
 * mapped memory region in the child process, passing as arguments
 *	CORE_ADDR virtual_address,
 *	unsigned long size,
 *	int read, 	TRUE if region is readable by the child
 *	int write, 	TRUE if region is writable by the child
 *	int execute	TRUE if region is executable by the child.
 *
 * Stops iterating and returns the first non-zero value
 * returned by the callback.
 */

static int
proc_find_memory_regions (int (*func) (CORE_ADDR,
				       unsigned long,
				       int, int, int,
				       void *),
			  void *data)
{
  procinfo *pi = find_procinfo_or_die (PIDGET (inferior_ptid), 0);

  return iterate_over_mappings (pi, func, data,
				find_memory_regions_callback);
}

/*
 * Function: mappingflags
 *
 * Returns an ascii representation of a memory mapping's flags.
 */

static char *
mappingflags (long flags)
{
  static char asciiflags[8];

  strcpy (asciiflags, "-------");
#if defined (MA_PHYS)
  if (flags & MA_PHYS)
    asciiflags[0] = 'd';
#endif
  if (flags & MA_STACK)
    asciiflags[1] = 's';
  if (flags & MA_BREAK)
    asciiflags[2] = 'b';
  if (flags & MA_SHARED)
    asciiflags[3] = 's';
  if (flags & MA_READ)
    asciiflags[4] = 'r';
  if (flags & MA_WRITE)
    asciiflags[5] = 'w';
  if (flags & MA_EXEC)
    asciiflags[6] = 'x';
  return (asciiflags);
}

/*
 * Function: info_mappings_callback
 *
 * Callback function, does the actual work for 'info proc mappings'.
 */

static int
info_mappings_callback (struct prmap *map, int (*ignore) (), void *unused)
{
  char *data_fmt_string;

  if (TARGET_ADDR_BIT == 32)
    data_fmt_string   = "\t%#10lx %#10lx %#10x %#10x %7s\n";
  else
    data_fmt_string   = "  %#18lx %#18lx %#10x %#10x %7s\n";

  printf_filtered (data_fmt_string,
		   (unsigned long) map->pr_vaddr,
		   (unsigned long) map->pr_vaddr + map->pr_size - 1,
		   map->pr_size,
#ifdef PCAGENT	/* Horrible hack: only defined on Solaris 2.6+ */
		   (unsigned int) map->pr_offset,
#else
		   map->pr_off,
#endif
		   mappingflags (map->pr_mflags));

  return 0;
}

/*
 * Function: info_proc_mappings
 *
 * Implement the "info proc mappings" subcommand.
 */

static void
info_proc_mappings (procinfo *pi, int summary)
{
  char *header_fmt_string;

  if (TARGET_PTR_BIT == 32)
    header_fmt_string = "\t%10s %10s %10s %10s %7s\n";
  else
    header_fmt_string = "  %18s %18s %10s %10s %7s\n";

  if (summary)
    return;	/* No output for summary mode. */

  printf_filtered ("Mapped address spaces:\n\n");
  printf_filtered (header_fmt_string,
		   "Start Addr",
		   "  End Addr",
		   "      Size",
		   "    Offset",
		   "Flags");

  iterate_over_mappings (pi, NULL, NULL, info_mappings_callback);
  printf_filtered ("\n");
}

/*
 * Function: info_proc_cmd
 *
 * Implement the "info proc" command.
 */

static void
info_proc_cmd (char *args, int from_tty)
{
  struct cleanup *old_chain;
  procinfo *process  = NULL;
  procinfo *thread   = NULL;
  char    **argv     = NULL;
  char     *tmp      = NULL;
  int       pid      = 0;
  int       tid      = 0;
  int       mappings = 0;

  old_chain = make_cleanup (null_cleanup, 0);
  if (args)
    {
      if ((argv = buildargv (args)) == NULL)
	nomem (0);
      else
	make_cleanup_freeargv (argv);
    }
  while (argv != NULL && *argv != NULL)
    {
      if (isdigit (argv[0][0]))
	{
	  pid = strtoul (argv[0], &tmp, 10);
	  if (*tmp == '/')
	    tid = strtoul (++tmp, NULL, 10);
	}
      else if (argv[0][0] == '/')
	{
	  tid = strtoul (argv[0] + 1, NULL, 10);
	}
      else if (strncmp (argv[0], "mappings", strlen (argv[0])) == 0)
	{
	  mappings = 1;
	}
      else
	{
	  /* [...] */
	}
      argv++;
    }
  if (pid == 0)
    pid = PIDGET (inferior_ptid);
  if (pid == 0)
    error ("No current process: you must name one.");
  else
    {
      /* Have pid, will travel.
	 First see if it's a process we're already debugging. */
      process = find_procinfo (pid, 0);
       if (process == NULL)
	 {
	   /* No.  So open a procinfo for it, but
	      remember to close it again when finished.  */
	   process = create_procinfo (pid, 0);
	   make_cleanup (do_destroy_procinfo_cleanup, process);
	   if (!open_procinfo_files (process, FD_CTL))
	     proc_error (process, "info proc, open_procinfo_files", __LINE__);
	 }
    }
  if (tid != 0)
    thread = create_procinfo (pid, tid);

  if (process)
    {
      printf_filtered ("process %d flags:\n", process->pid);
      proc_prettyprint_flags (proc_flags (process), 1);
      if (proc_flags (process) & (PR_STOPPED | PR_ISTOP))
	proc_prettyprint_why (proc_why (process), proc_what (process), 1);
      if (proc_get_nthreads (process) > 1)
	printf_filtered ("Process has %d threads.\n",
			 proc_get_nthreads (process));
    }
  if (thread)
    {
      printf_filtered ("thread %d flags:\n", thread->tid);
      proc_prettyprint_flags (proc_flags (thread), 1);
      if (proc_flags (thread) & (PR_STOPPED | PR_ISTOP))
	proc_prettyprint_why (proc_why (thread), proc_what (thread), 1);
    }

  if (mappings)
    {
      info_proc_mappings (process, 0);
    }

  do_cleanups (old_chain);
}

static void
proc_trace_syscalls (char *args, int from_tty, int entry_or_exit, int mode)
{
  procinfo *pi;
  sysset_t *sysset;
  int       syscallnum = 0;

  if (PIDGET (inferior_ptid) <= 0)
    error ("you must be debugging a process to use this command.");

  if (args == NULL || args[0] == 0)
    error_no_arg ("system call to trace");

  pi = find_procinfo_or_die (PIDGET (inferior_ptid), 0);
  if (isdigit (args[0]))
    {
      syscallnum = atoi (args);
      if (entry_or_exit == PR_SYSENTRY)
	sysset = proc_get_traced_sysentry (pi, NULL);
      else
	sysset = proc_get_traced_sysexit (pi, NULL);

      if (sysset == NULL)
	proc_error (pi, "proc-trace, get_traced_sysset", __LINE__);

      if (mode == FLAG_SET)
	gdb_praddsysset (sysset, syscallnum);
      else
	gdb_prdelsysset (sysset, syscallnum);

      if (entry_or_exit == PR_SYSENTRY)
	{
	  if (!proc_set_traced_sysentry (pi, sysset))
	    proc_error (pi, "proc-trace, set_traced_sysentry", __LINE__);
	}
      else
	{
	  if (!proc_set_traced_sysexit (pi, sysset))
	    proc_error (pi, "proc-trace, set_traced_sysexit", __LINE__);
	}
    }
}

static void
proc_trace_sysentry_cmd (char *args, int from_tty)
{
  proc_trace_syscalls (args, from_tty, PR_SYSENTRY, FLAG_SET);
}

static void
proc_trace_sysexit_cmd (char *args, int from_tty)
{
  proc_trace_syscalls (args, from_tty, PR_SYSEXIT, FLAG_SET);
}

static void
proc_untrace_sysentry_cmd (char *args, int from_tty)
{
  proc_trace_syscalls (args, from_tty, PR_SYSENTRY, FLAG_RESET);
}

static void
proc_untrace_sysexit_cmd (char *args, int from_tty)
{
  proc_trace_syscalls (args, from_tty, PR_SYSEXIT, FLAG_RESET);
}


void
_initialize_procfs (void)
{
  init_procfs_ops ();
  add_target (&procfs_ops);
  add_info ("proc", info_proc_cmd,
	    "Show /proc process information about any running process.\n\
Specify process id, or use the program being debugged by default.\n\
Specify keyword 'mappings' for detailed info on memory mappings.");
  add_com ("proc-trace-entry", no_class, proc_trace_sysentry_cmd,
	   "Give a trace of entries into the syscall.");
  add_com ("proc-trace-exit", no_class, proc_trace_sysexit_cmd,
	   "Give a trace of exits from the syscall.");
  add_com ("proc-untrace-entry", no_class, proc_untrace_sysentry_cmd,
	   "Cancel a trace of entries into the syscall.");
  add_com ("proc-untrace-exit", no_class, proc_untrace_sysexit_cmd,
	   "Cancel a trace of exits from the syscall.");
}

/* =================== END, GDB  "MODULE" =================== */



/* miscellaneous stubs:                                             */
/* The following satisfy a few random symbols mostly created by    */
/* the solaris threads implementation, which I will chase down     */
/* later.        */

/*
 * Return a pid for which we guarantee
 * we will be able to find a 'live' procinfo.
 */

ptid_t
procfs_first_available (void)
{
  return pid_to_ptid (procinfo_list ? procinfo_list->pid : -1);
}

/* ===================  GCORE .NOTE "MODULE" =================== */
#if defined (UNIXWARE) || defined (PIOCOPENLWP) || defined (PCAGENT)
/* gcore only implemented on solaris and unixware (so far) */

static char *
procfs_do_thread_registers (bfd *obfd, ptid_t ptid,
			    char *note_data, int *note_size)
{
  gdb_gregset_t gregs;
  gdb_fpregset_t fpregs;
  unsigned long merged_pid;

  merged_pid = TIDGET (ptid) << 16 | PIDGET (ptid);

  fill_gregset (&gregs, -1);
#if defined (UNIXWARE)
  note_data = (char *) elfcore_write_lwpstatus (obfd,
						note_data,
						note_size,
						merged_pid,
						stop_signal,
						&gregs);
#else
  note_data = (char *) elfcore_write_prstatus (obfd,
					       note_data,
					       note_size,
					       merged_pid,
					       stop_signal,
					       &gregs);
#endif
  fill_fpregset (&fpregs, -1);
  note_data = (char *) elfcore_write_prfpreg (obfd,
					      note_data,
					      note_size,
					      &fpregs,
					      sizeof (fpregs));
  return note_data;
}

struct procfs_corefile_thread_data {
  bfd *obfd;
  char *note_data;
  int *note_size;
};

static int
procfs_corefile_thread_callback (procinfo *pi, procinfo *thread, void *data)
{
  struct procfs_corefile_thread_data *args = data;

  if (pi != NULL && thread->tid != 0)
    {
      ptid_t saved_ptid = inferior_ptid;
      inferior_ptid = MERGEPID (pi->pid, thread->tid);
      args->note_data = procfs_do_thread_registers (args->obfd, inferior_ptid,
						    args->note_data,
						    args->note_size);
      inferior_ptid = saved_ptid;
    }
  return 0;
}

static char *
procfs_make_note_section (bfd *obfd, int *note_size)
{
  struct cleanup *old_chain;
  gdb_gregset_t gregs;
  gdb_fpregset_t fpregs;
  char fname[16] = {'\0'};
  char psargs[80] = {'\0'};
  procinfo *pi = find_procinfo_or_die (PIDGET (inferior_ptid), 0);
  char *note_data = NULL;
  char *inf_args;
  struct procfs_corefile_thread_data thread_args;
  char *auxv;
  int auxv_len;

  if (get_exec_file (0))
    {
      strncpy (fname, strrchr (get_exec_file (0), '/') + 1, sizeof (fname));
      strncpy (psargs, get_exec_file (0),
	       sizeof (psargs));

      inf_args = get_inferior_args ();
      if (inf_args && *inf_args &&
	  strlen (inf_args) < ((int) sizeof (psargs) - (int) strlen (psargs)))
	{
	  strncat (psargs, " ",
		   sizeof (psargs) - strlen (psargs));
	  strncat (psargs, inf_args,
		   sizeof (psargs) - strlen (psargs));
	}
    }

  note_data = (char *) elfcore_write_prpsinfo (obfd,
					       note_data,
					       note_size,
					       fname,
					       psargs);

#ifdef UNIXWARE
  fill_gregset (&gregs, -1);
  note_data = elfcore_write_pstatus (obfd, note_data, note_size,
				     PIDGET (inferior_ptid),
				     stop_signal, &gregs);
#endif

  thread_args.obfd = obfd;
  thread_args.note_data = note_data;
  thread_args.note_size = note_size;
  proc_iterate_over_threads (pi, procfs_corefile_thread_callback, &thread_args);

  if (thread_args.note_data == note_data)
    {
      /* iterate_over_threads didn't come up with any threads;
	 just use inferior_ptid. */
      note_data = procfs_do_thread_registers (obfd, inferior_ptid,
					      note_data, note_size);
    }
  else
    {
      note_data = thread_args.note_data;
    }

  auxv_len = target_auxv_read (&current_target, &auxv);
  if (auxv_len > 0)
    {
      note_data = elfcore_write_note (obfd, note_data, note_size,
				      "CORE", NT_AUXV, auxv, auxv_len);
      xfree (auxv);
    }

  make_cleanup (xfree, note_data);
  return note_data;
}
#else /* !(Solaris or Unixware) */
static char *
procfs_make_note_section (bfd *obfd, int *note_size)
{
  error ("gcore not implemented for this host.");
  return NULL;	/* lint */
}
#endif /* Solaris or Unixware */
/* ===================  END GCORE .NOTE "MODULE" =================== */

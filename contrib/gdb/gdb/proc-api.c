/* Machine independent support for SVR4 /proc (process file system) for GDB.

   Copyright 1999, 2000, 2001, 2003 Free Software Foundation, Inc.

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
 * Pretty-print trace of api calls to the /proc api
 * (ioctl or read/write calls).
 * 
 */

#include "defs.h"
#include "gdbcmd.h"
#include "completer.h"

#if defined (NEW_PROC_API)
#define _STRUCTURED_PROC 1
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/procfs.h>
#ifdef HAVE_SYS_PROC_H
#include <sys/proc.h>	/* for struct proc */
#endif
#ifdef HAVE_SYS_USER_H
#include <sys/user.h>	/* for struct user */
#endif
#include <fcntl.h>	/* for O_RDWR etc. */
#include "gdb_wait.h"

#include "proc-utils.h"

/*  Much of the information used in the /proc interface, particularly for
    printing status information, is kept as tables of structures of the
    following form.  These tables can be used to map numeric values to
    their symbolic names and to a string that describes their specific use. */

struct trans {
  long value;                   /* The numeric value */
  char *name;                   /* The equivalent symbolic value */
  char *desc;                   /* Short description of value */
};

static int   procfs_trace    = 0;
static FILE *procfs_file     = NULL;
static char *procfs_filename = "procfs_trace";

static void
prepare_to_trace (void)
{
  if (procfs_trace)			/* if procfs tracing turned on */
    if (procfs_file == NULL)		/* if output file not yet open */
      if (procfs_filename != NULL)	/* if output filename known */
	procfs_file = fopen (procfs_filename, "a");	/* open output file */
}

static void
set_procfs_trace_cmd (char *args, int from_tty, struct cmd_list_element *c)
{
#if 0	/* not sure what I might actually need to do here, if anything */
  if (procfs_file)
    fflush (procfs_file);
#endif
}

static void
set_procfs_file_cmd (char *args, int from_tty, struct cmd_list_element *c)
{
  /* Just changed the filename for procfs tracing.
     If a file was already open, close it.  */
  if (procfs_file)
    fclose (procfs_file);
  procfs_file = NULL;
}


#ifndef NEW_PROC_API

static struct trans ioctl_table[] = {
#ifdef PIOCACINFO			/* irix */
  { PIOCACINFO,    "PIOCACINFO",   "get process account info" },
#endif
  { PIOCACTION,    "PIOCACTION",   "get signal action structs" },
#ifdef PIOCARGUMENTS			/* osf */
  { PIOCARGUMENTS, "PIOCARGUMENTS", "command line args" },
#endif
#ifdef PIOCAUXV				/* solaris aux vectors */
  { PIOCAUXV,      "PIOCAUXV",     "get aux vector" },
  { PIOCNAUXV,     "PIOCNAUXV",    "get number of aux vector entries" },
#endif /* AUXV */
  { PIOCCFAULT,    "PIOCCFAULT",   "clear current fault" },
  { PIOCCRED,      "PIOCCRED",     "get process credentials" },
#ifdef PIOCENEVCTRS			/* irix event counters */
  { PIOCENEVCTRS,    "PIOCENEVCTRS",    "acquire and start event counters" },
  { PIOCGETEVCTRL,   "PIOCGETEVCTRL",   "get control info of event counters" },
  { PIOCGETEVCTRS,   "PIOCGETEVCTRS",   "dump event counters" },
  { PIOCGETPREVCTRS, "PIOCGETPREVCTRS", "dump event counters & prusage info" },
  { PIOCRELEVCTRS,   "PIOCRELEVCTRS",   "release/stop event counters" },
  { PIOCSETEVCTRL,   "PIOCSETEVCTRL",   "set control info of event counters" },
  { PIOCGETPTIMER,   "PIOCGETPTIMER",   "get process timers" },
#endif	/* irix event counters */
  { PIOCGENTRY,    "PIOCGENTRY",   "get traced syscall entry set" },
#if defined (PIOCGETPR)
  { PIOCGETPR,     "PIOCGETPR",    "read struct proc" },
#endif
#if defined (PIOCGETU)
  { PIOCGETU,      "PIOCGETU",     "read user area" },
#endif
#if defined (PIOCGETUTK) && (defined(KERNEL) || defined(SHOW_UTT)) /* osf */
  { PIOCGETUTK,  "PIOCGETUTK", "get the utask struct" },
#endif
  { PIOCGEXIT,     "PIOCGEXIT",    "get traced syscall exit  set" },
  { PIOCGFAULT,    "PIOCGFAULT",   "get traced fault set" },
#ifdef PIOCGFPCR			/* osf */
  { PIOCGFPCR,     "PIOCGFPCR",    "get FP control register" },
  { PIOCSFPCR,     "PIOCSFPCR",    "set FP conrtol register" },
#endif
  { PIOCGFPREG,    "PIOCGFPREG",   "get floating point registers" },
  { PIOCGHOLD,     "PIOCGHOLD",    "get held signal set" },
  { PIOCGREG,      "PIOCGREG",     "get general registers" },
  { PIOCGROUPS,    "PIOCGROUPS",   "get supplementary groups" },
#ifdef PIOCGSPCACT			/* osf */
  { PIOCGSPCACT,   "PIOCGSPCACT",  "get special action" },
  { PIOCSSPCACT,   "PIOCSSPCACT",  "set special action" },
#endif
  { PIOCGTRACE,    "PIOCGTRACE",   "get traced signal set" },
#ifdef PIOCGWATCH			/* irix watchpoints */
  { PIOCGWATCH,    "PIOCGWATCH",   "get watchpoint" },
  { PIOCSWATCH,    "PIOCSWATCH",   "set watchpoint" },
  { PIOCNWATCH,    "PIOCNWATCH",   "get number of watchpoints" },
#endif	/* irix watchpoints */
#ifdef PIOCGWIN				/* solaris sparc */
  { PIOCGWIN,      "PIOCGWIN",     "get gwindows_t" },
#endif
#ifdef PIOCGXREG			/* solaris sparc extra regs */
  { PIOCGXREGSIZE, "PIOCXREGSIZE", "get extra register state size" },
  { PIOCGXREG,     "PIOCGXREG",    "get extra register state" },
  { PIOCSXREG,     "PIOCSXREG",    "set extra register state" },
#endif /* XREG */
  { PIOCKILL,      "PIOCKILL",     "send signal" },
#ifdef PIOCLDT				/* solaris i386 */
  { PIOCLDT,       "PIOCLDT",      "get LDT" },
  { PIOCNLDT,      "PIOCNLDT",     "get number of LDT entries" },
#endif
#ifdef PIOCLSTATUS			/* solaris and unixware */
  { PIOCLSTATUS,   "PIOCLSTATUS",  "get status of all lwps" },
  { PIOCLUSAGE,    "PIOCLUSAGE",   "get resource usage of all lwps" },
  { PIOCOPENLWP,   "PIOCOPENLWP",  "get lwp file descriptor" },
  { PIOCLWPIDS,    "PIOCLWPIDS",   "get lwp identifiers" },
#endif /* LWP */
  { PIOCMAP,       "PIOCMAP",      "get memory map information" },
  { PIOCMAXSIG,    "PIOCMAXSIG",   "get max signal number" },
  { PIOCNICE,      "PIOCNICE",     "set nice priority" },
  { PIOCNMAP,      "PIOCNMAP",     "get number of memory mappings" },
  { PIOCOPENM,     "PIOCOPENM",    "open mapped object for reading" },
#ifdef PIOCOPENMOBS			/* osf */
  { PIOCOPENMOBS,  "PIOCOPENMOBS", "open mapped object" },
#endif
#ifdef PIOCOPENPD	/* solaris */
  { PIOCOPENPD,    "PIOCOPENPD",   "get page data file descriptor" },
#endif
  { PIOCPSINFO,    "PIOCPSINFO",   "get ps(1) information" },
  { PIOCRESET,     "PIOCRESET",    "reset process flags" },
  { PIOCRFORK,     "PIOCRFORK",    "reset inherit-on-fork flag" },
  { PIOCRRLC,      "PIOCRRLC",     "reset run-on-last-close flag" },
  { PIOCRUN,       "PIOCRUN",      "make process runnable" },
#ifdef PIOCSAVECCNTRS			/* irix */
  { PIOCSAVECCNTRS, "PIOCSAVECCNTRS", "parent gets child cntrs" },
#endif
  { PIOCSENTRY,    "PIOCSENTRY",   "set traced syscall entry set" },
  { PIOCSET,       "PIOCSET",      "set process flags" },
  { PIOCSEXIT,     "PIOCSEXIT",    "set traced syscall exit  set" },
  { PIOCSFAULT,    "PIOCSFAULT",   "set traced fault set" },
  { PIOCSFORK,     "PIOCSFORK",    "set inherit-on-fork flag" },
  { PIOCSFPREG,    "PIOCSFPREG",   "set floating point registers" },
  { PIOCSHOLD,     "PIOCSHOLD",    "set held signal set" },
  { PIOCSREG,      "PIOCSREG",     "set general registers" },
  { PIOCSRLC,      "PIOCSRLC",     "set run-on-last-close flag" },
  { PIOCSSIG,      "PIOCSSIG",     "set current signal" },
  { PIOCSTATUS,    "PIOCSTATUS",   "get process status" },
  { PIOCSTOP,      "PIOCSTOP",     "post stop request" },
  { PIOCSTRACE,    "PIOCSTRACE",   "set traced signal set" },
  { PIOCUNKILL,    "PIOCUNKILL",   "delete a signal" },
#ifdef PIOCUSAGE	/* solaris */
  { PIOCUSAGE,     "PIOCUSAGE",    "get resource usage" },
#endif
  { PIOCWSTOP,     "PIOCWSTOP",    "wait for process to stop" },

#ifdef PIOCNTHR				/* osf threads */
  { PIOCNTHR,      "PIOCNTHR",     "get thread count" },
  { PIOCRTINH,     "PIOCRTINH",    "reset inherit-on-thread-creation" },
  { PIOCSTINH,     "PIOCSTINH",    "set   inherit-on-thread-creation" },
  { PIOCTLIST,     "PIOCTLIST",    "get thread ids" },
  { PIOCXPTH,      "PIOCXPTH",     "translate port to thread handle" },
  { PIOCTRUN,      "PIOCTRUN",     "make thread runnable" },
  { PIOCTSTATUS,   "PIOCTSTATUS",  "get thread status" },
  { PIOCTSTOP,     "PIOCTSTOP",    "stop a thread" },
  /* ... TGTRACE TSTRACE TSSIG TKILL TUNKILL TCFAULT TGFAULT TSFAULT
     TGFPREG TSFPREG TGREG TSREG TACTION TTERM TABRUN TGENTRY TSENTRY
     TGEXIT TSEXIT TSHOLD ... thread functions */
#endif /* osf threads */
  { -1,            NULL,           NULL }
};

int
ioctl_with_trace (int fd, long opcode, void *ptr, char *file, int line)
{
  int i = 0;
  int ret;
  int arg1;

  prepare_to_trace ();

  if (procfs_trace)
    {
      for (i = 0; ioctl_table[i].name != NULL; i++)
	if (ioctl_table[i].value == opcode)
	  break;

      if (info_verbose)
	fprintf (procfs_file ? procfs_file : stdout, 
		 "%s:%d -- ", file, line);
      switch (opcode) {
      case PIOCSET:
	arg1 = ptr ? *(long *) ptr : 0;
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (PIOCSET,   %s) %s\n", 
		 arg1 == PR_FORK  ? "PR_FORK"  :
		 arg1 == PR_RLC   ? "PR_RLC"   :
#ifdef PR_ASYNC
		 arg1 == PR_ASYNC ? "PR_ASYNC" :
#endif
		 "<unknown flag>",
		 info_verbose ? ioctl_table[i].desc : "");
	break;
      case PIOCRESET:
	arg1 = ptr ? *(long *) ptr : 0;
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (PIOCRESET, %s) %s\n", 
		 arg1 == PR_FORK  ? "PR_FORK"  :
		 arg1 == PR_RLC   ? "PR_RLC"   :
#ifdef PR_ASYNC
		 arg1 == PR_ASYNC ? "PR_ASYNC" :
#endif
		 "<unknown flag>",
		 info_verbose ? ioctl_table[i].desc : "");
	break;
      case PIOCSTRACE:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (PIOCSTRACE) ");
	proc_prettyfprint_signalset (procfs_file ? procfs_file : stdout,
				     (sigset_t *) ptr, 0);
	break;
      case PIOCSFAULT:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (%s) ", 
		 opcode == PIOCSFAULT ? "PIOCSFAULT" : "PIOCGFAULT");
	proc_prettyfprint_faultset (procfs_file ? procfs_file : stdout,
				    (fltset_t *) ptr, 0);
	break;
      case PIOCSENTRY:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (%s) ", 
		 opcode == PIOCSENTRY ? "PIOCSENTRY" : "PIOCGENTRY");
	proc_prettyfprint_syscalls (procfs_file ? procfs_file : stdout,
				    (sysset_t *) ptr, 0);
	break;
      case PIOCSEXIT:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (%s) ", 
		 opcode == PIOCSEXIT ? "PIOCSEXIT" : "PIOCGEXIT");
	proc_prettyfprint_syscalls (procfs_file ? procfs_file : stdout,
				    (sysset_t *) ptr, 0);
	break;
      case PIOCSHOLD:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (%s) ", 
		 opcode == PIOCSHOLD ? "PIOCSHOLD" : "PIOCGHOLD");
	proc_prettyfprint_signalset (procfs_file ? procfs_file : stdout,
				     (sigset_t *) ptr, 0);
	break;
      case PIOCSSIG:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (PIOCSSIG) ");
	proc_prettyfprint_signal (procfs_file ? procfs_file : stdout,
				  ptr ? ((siginfo_t *) ptr)->si_signo : 0, 
				  0);
	fprintf (procfs_file ? procfs_file : stdout, "\n");
	break;
      case PIOCRUN:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (PIOCRUN) ");
	
	arg1 = ptr ? *(long *) ptr : 0;
	if (arg1 & PRCSIG)
	  fprintf (procfs_file ? procfs_file : stdout, "clearSig ");
	if (arg1 & PRCFAULT)
	  fprintf (procfs_file ? procfs_file : stdout, "clearFlt ");
	if (arg1 & PRSTRACE)
	  fprintf (procfs_file ? procfs_file : stdout, "setTrace ");
	if (arg1 & PRSHOLD)
	  fprintf (procfs_file ? procfs_file : stdout, "setHold ");
	if (arg1 & PRSFAULT)
	  fprintf (procfs_file ? procfs_file : stdout, "setFlt ");
	if (arg1 & PRSVADDR)
	  fprintf (procfs_file ? procfs_file : stdout, "setVaddr ");
	if (arg1 & PRSTEP)
	  fprintf (procfs_file ? procfs_file : stdout, "step ");
	if (arg1 & PRSABORT)
	  fprintf (procfs_file ? procfs_file : stdout, "syscallAbort ");
	if (arg1 & PRSTOP)
	  fprintf (procfs_file ? procfs_file : stdout, "stopReq ");
	  
	fprintf (procfs_file ? procfs_file : stdout, "\n");
	break;
      case PIOCKILL:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (PIOCKILL) ");
	proc_prettyfprint_signal (procfs_file ? procfs_file : stdout,
				  ptr ? *(long *) ptr : 0, 0);
	fprintf (procfs_file ? procfs_file : stdout, "\n");
	break;
#ifdef PIOCSSPCACT
      case PIOCSSPCACT:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "ioctl (PIOCSSPCACT) ");
	arg1 = ptr ? *(long *) ptr : 0;
	if (arg1 & PRFS_STOPFORK)
	  fprintf (procfs_file ? procfs_file : stdout, "stopFork ");
	if (arg1 & PRFS_STOPEXEC)
	  fprintf (procfs_file ? procfs_file : stdout, "stopExec ");
	if (arg1 & PRFS_STOPTERM)
	  fprintf (procfs_file ? procfs_file : stdout, "stopTerm ");
	if (arg1 & PRFS_STOPTCR)
	  fprintf (procfs_file ? procfs_file : stdout, "stopThreadCreate ");
	if (arg1 & PRFS_STOPTTERM)
	  fprintf (procfs_file ? procfs_file : stdout, "stopThreadTerm ");
	if (arg1 & PRFS_KOLC)
	  fprintf (procfs_file ? procfs_file : stdout, "killOnLastClose ");
	fprintf (procfs_file ? procfs_file : stdout, "\n");
	break;
#endif /* PIOCSSPCACT */
      default:
	if (ioctl_table[i].name)
	  fprintf (procfs_file ? procfs_file : stdout, 
		   "ioctl (%s) %s\n", 
		   ioctl_table[i].name,
		   info_verbose ? ioctl_table[i].desc : "");
	else
	  fprintf (procfs_file ? procfs_file : stdout, 
		   "ioctl (<unknown %ld (0x%lx)) \n", opcode, opcode);
	break;
      }
      if (procfs_file)
	fflush (procfs_file);
    }
  errno = 0;
  ret = ioctl (fd, opcode, ptr);
  if (procfs_trace && ret < 0)
    {
      fprintf (procfs_file ? procfs_file : stdout, 
	       "[ioctl (%s) FAILED! (%s)]\n",
	       ioctl_table[i].name != NULL ? 
	       ioctl_table[i].name : "<unknown>",
	       safe_strerror (errno));
      if (procfs_file)
	fflush (procfs_file);
    }

  return ret;
}

#else	/* NEW_PROC_API */

static struct trans rw_table[] = {
#ifdef PCAGENT			/* solaris */
  { PCAGENT,  "PCAGENT",  "create agent lwp with regs from argument" },
#endif
  { PCCFAULT, "PCCFAULT", "clear current fault" },
#ifdef PCCSIG			/* solaris */
  { PCCSIG,   "PCCSIG",   "clear current signal" },
#endif
#ifdef PCDSTOP			/* solaris */
  { PCDSTOP,  "PCDSTOP",  "post stop request" },
#endif
  { PCKILL,   "PCKILL",   "post a signal" },
#ifdef PCNICE			/* solaris */
  { PCNICE,   "PCNICE",   "set nice priority" },
#endif
#ifdef PCREAD			/* solaris */
  { PCREAD,   "PCREAD",   "read from the address space" },
  { PCWRITE,  "PCWRITE",  "write to the address space" },
#endif
#ifdef PCRESET			/* unixware */
  { PCRESET,  "PCRESET",  "unset modes" },
#endif
  { PCRUN,    "PCRUN",    "make process/lwp runnable" },
#ifdef PCSASRS			/* solaris 2.7 only */
  { PCSASRS,  "PCSASRS",  "set ancillary state registers" },
#endif
#ifdef PCSCRED			/* solaris */
  { PCSCRED,  "PCSCRED",  "set process credentials" },
#endif
  { PCSENTRY, "PCSENTRY", "set traced syscall entry set" },
  { PCSET,    "PCSET",    "set modes" },
  { PCSEXIT,  "PCSEXIT",  "set traced syscall exit  set" },
  { PCSFAULT, "PCSFAULT", "set traced fault set" },
  { PCSFPREG, "PCSFPREG", "set floating point registers" },
#ifdef PCSHOLD			/* solaris */
  { PCSHOLD,  "PCSHOLD",  "set signal mask" },
#endif
  { PCSREG,   "PCSREG",   "set general registers" },
  { PCSSIG,   "PCSSIG",   "set current signal" },
  { PCSTOP,   "PCSTOP",   "post stop request and wait" },
  { PCSTRACE, "PCSTRACE", "set traced signal set" },
#ifdef PCSVADDR			/* solaris */
  { PCSVADDR, "PCSVADDR", "set pc virtual address" },
#endif
#ifdef PCSXREG			/* solaris sparc only */
  { PCSXREG,  "PCSXREG",  "set extra registers" },
#endif
#ifdef PCTWSTOP			/* solaris */
  { PCTWSTOP, "PCTWSTOP", "wait for stop, with timeout arg" },
#endif
#ifdef PCUNKILL			/* solaris */
  { PCUNKILL, "PCUNKILL", "delete a pending signal" },
#endif
#ifdef PCUNSET			/* solaris */
  { PCUNSET,  "PCUNSET",  "unset modes" },
#endif
#ifdef PCWATCH			/* solaris */
  { PCWATCH,  "PCWATCH",  "set/unset watched memory area" },
#endif
  { PCWSTOP,  "PCWSTOP",  "wait for process/lwp to stop, no timeout" },
  { 0,        NULL,      NULL }
};

static off_t lseek_offset;

int
write_with_trace (int fd, void *varg, size_t len, char *file, int line)
{
  int i = ARRAY_SIZE (rw_table) - 1;
  int ret;
  procfs_ctl_t *arg = (procfs_ctl_t *) varg;

  prepare_to_trace ();
  if (procfs_trace)
    {
      procfs_ctl_t opcode = arg[0];
      for (i = 0; rw_table[i].name != NULL; i++)
	if (rw_table[i].value == opcode)
	  break;

      if (info_verbose)
	fprintf (procfs_file ? procfs_file : stdout, 
		 "%s:%d -- ", file, line);
      switch (opcode) {
      case PCSET:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "write (PCSET,   %s) %s\n", 
		 arg[1] == PR_FORK  ? "PR_FORK"  :
		 arg[1] == PR_RLC   ? "PR_RLC"   :
#ifdef PR_ASYNC
		 arg[1] == PR_ASYNC ? "PR_ASYNC" :
#endif
		 "<unknown flag>",
		 info_verbose ? rw_table[i].desc : "");
	break;
#ifdef PCUNSET
      case PCUNSET:
#endif
#ifdef PCRESET
#if PCRESET != PCUNSET
      case PCRESET:
#endif
#endif
	fprintf (procfs_file ? procfs_file : stdout, 
		 "write (PCRESET, %s) %s\n", 
		 arg[1] == PR_FORK  ? "PR_FORK"  :
		 arg[1] == PR_RLC   ? "PR_RLC"   :
#ifdef PR_ASYNC
		 arg[1] == PR_ASYNC ? "PR_ASYNC" :
#endif
		 "<unknown flag>",
		 info_verbose ? rw_table[i].desc : "");
	break;
      case PCSTRACE:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "write (PCSTRACE) ");
	proc_prettyfprint_signalset (procfs_file ? procfs_file : stdout,
				     (sigset_t *) &arg[1], 0);
	break;
      case PCSFAULT:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "write (PCSFAULT) ");
	proc_prettyfprint_faultset (procfs_file ? procfs_file : stdout,
				    (fltset_t *) &arg[1], 0);
	break;
      case PCSENTRY:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "write (PCSENTRY) ");
	proc_prettyfprint_syscalls (procfs_file ? procfs_file : stdout,
				    (sysset_t *) &arg[1], 0);
	break;
      case PCSEXIT:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "write (PCSEXIT) ");
	proc_prettyfprint_syscalls (procfs_file ? procfs_file : stdout,
				    (sysset_t *) &arg[1], 0);
	break;
#ifdef PCSHOLD
      case PCSHOLD:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "write (PCSHOLD) ");
	proc_prettyfprint_signalset (procfs_file ? procfs_file : stdout,
				     (sigset_t *) &arg[1], 0);
	break;
#endif
      case PCSSIG:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "write (PCSSIG) ");
	proc_prettyfprint_signal (procfs_file ? procfs_file : stdout,
				  arg[1] ? ((siginfo_t *) &arg[1])->si_signo 
				         : 0, 
				  0);
	fprintf (procfs_file ? procfs_file : stdout, "\n");
	break;
      case PCRUN:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "write (PCRUN) ");
	if (arg[1] & PRCSIG)
	  fprintf (procfs_file ? procfs_file : stdout, "clearSig ");
	if (arg[1] & PRCFAULT)
	  fprintf (procfs_file ? procfs_file : stdout, "clearFlt ");
	if (arg[1] & PRSTEP)
	  fprintf (procfs_file ? procfs_file : stdout, "step ");
#ifdef PRSABORT
	if (arg[1] & PRSABORT)
	  fprintf (procfs_file ? procfs_file : stdout, "syscallAbort ");
#endif
#ifdef PRSTOP
	if (arg[1] & PRSTOP)
	  fprintf (procfs_file ? procfs_file : stdout, "stopReq ");
#endif
	  
	fprintf (procfs_file ? procfs_file : stdout, "\n");
	break;
      case PCKILL:
	fprintf (procfs_file ? procfs_file : stdout, 
		 "write (PCKILL) ");
	proc_prettyfprint_signal (procfs_file ? procfs_file : stdout,
				  arg[1], 0);
	fprintf (procfs_file ? procfs_file : stdout, "\n");
	break;
      default:
	{
	  if (rw_table[i].name)
	    fprintf (procfs_file ? procfs_file : stdout, 
		     "write (%s) %s\n", 
		     rw_table[i].name, 
		     info_verbose ? rw_table[i].desc : "");
	  else
	    {
	      if (lseek_offset != -1)
		fprintf (procfs_file ? procfs_file : stdout, 
			 "write (<unknown>, %lud bytes at 0x%08lx) \n", 
			 (unsigned long) len, (unsigned long) lseek_offset);
	      else
		fprintf (procfs_file ? procfs_file : stdout, 
			 "write (<unknown>, %lud bytes) \n", 
			 (unsigned long) len);
	    }
	  break;
	}
      }
      if (procfs_file)
	fflush (procfs_file);
    }
  errno = 0;
  ret = write (fd, (void *) arg, len);
  if (procfs_trace && ret != len)
    {
      fprintf (procfs_file ? procfs_file : stdout, 
	       "[write (%s) FAILED! (%s)]\n",
	       rw_table[i].name != NULL ? 
	       rw_table[i].name : "<unknown>", 
	       safe_strerror (errno));
      if (procfs_file)
	fflush (procfs_file);
    }

  lseek_offset = -1;
  return ret;
}

off_t
lseek_with_trace (int fd, off_t offset, int whence, char *file, int line)
{
  off_t ret;

  prepare_to_trace ();
  errno = 0;
  ret = lseek (fd, offset, whence);
  lseek_offset = ret;
  if (procfs_trace && (ret == -1 || errno != 0))
    {
      fprintf (procfs_file ? procfs_file : stdout, 
	       "[lseek (0x%08lx) FAILED! (%s)]\n", 
	       (unsigned long) offset, safe_strerror (errno));
      if (procfs_file)
	fflush (procfs_file);
    }

  return ret;
}

#endif /* NEW_PROC_API */

int
open_with_trace (char *filename, int mode, char *file, int line)
{
  int ret;

  prepare_to_trace ();
  errno = 0;
  ret = open (filename, mode);
  if (procfs_trace)
    {
      if (info_verbose)
	fprintf (procfs_file ? procfs_file : stdout, 
		 "%s:%d -- ", file, line);

      if (errno)
	{
	  fprintf (procfs_file ? procfs_file : stdout, 
		   "[open FAILED! (%s) line %d]\\n", 
		   safe_strerror (errno), line);
	}
      else
	{
	  fprintf (procfs_file ? procfs_file : stdout, 
		   "%d = open (%s, ", ret, filename);
	  if (mode == O_RDONLY)
	    fprintf (procfs_file ? procfs_file : stdout, "O_RDONLY) %d\n",
		     line);
	  else if (mode == O_WRONLY)
	    fprintf (procfs_file ? procfs_file : stdout, "O_WRONLY) %d\n",
		     line);
	  else if (mode == O_RDWR)
	    fprintf (procfs_file ? procfs_file : stdout, "O_RDWR)   %d\n",
		     line);
	}
      if (procfs_file)
	fflush (procfs_file);
    }

  return ret;
}

int
close_with_trace (int fd, char *file, int line)
{
  int ret;

  prepare_to_trace ();
  errno = 0;
  ret = close (fd);
  if (procfs_trace)
    {
      if (info_verbose)
	fprintf (procfs_file ? procfs_file : stdout, 
		 "%s:%d -- ", file, line);
      if (errno)
	fprintf (procfs_file ? procfs_file : stdout, 
		 "[close FAILED! (%s)]\n", safe_strerror (errno));
      else
	fprintf (procfs_file ? procfs_file : stdout, 
		 "%d = close (%d)\n", ret, fd);
      if (procfs_file)
	fflush (procfs_file);
    }

  return ret;
}

pid_t
wait_with_trace (int *wstat, char *file, int line)
{
  int ret, lstat = 0;

  prepare_to_trace ();
  if (procfs_trace)
    {
      if (info_verbose)
	fprintf (procfs_file ? procfs_file : stdout, 
		 "%s:%d -- ", file, line);
      fprintf (procfs_file ? procfs_file : stdout, 
	       "wait (line %d) ", line);
      if (procfs_file)
	fflush (procfs_file);
    }
  errno = 0;
  ret = wait (&lstat);
  if (procfs_trace)
    {
      if (errno)
	fprintf (procfs_file ? procfs_file : stdout, 
		 "[wait FAILED! (%s)]\n", safe_strerror (errno));
      else
	fprintf (procfs_file ? procfs_file : stdout, 
		 "returned pid %d, status 0x%x\n", ret, lstat);
      if (procfs_file)
	fflush (procfs_file);
    }
  if (wstat)
    *wstat = lstat;

  return ret;
}

void
procfs_note (char *msg, char *file, int line)
{
  prepare_to_trace ();
  if (procfs_trace)
    {
      if (info_verbose)
	fprintf (procfs_file ? procfs_file : stdout, 
		 "%s:%d -- ", file, line);
      fprintf (procfs_file ? procfs_file : stdout, "%s", msg);
      if (procfs_file)
	fflush (procfs_file);
    }
}

void
proc_prettyfprint_status (long flags, int why, int what, int thread)
{
  prepare_to_trace ();
  if (procfs_trace)
    {
      if (thread)
	fprintf (procfs_file ? procfs_file : stdout,
		 "Thread %d: ", thread);

      proc_prettyfprint_flags (procfs_file ? procfs_file : stdout, 
			       flags, 0);

      if (flags & (PR_STOPPED | PR_ISTOP))
	proc_prettyfprint_why (procfs_file ? procfs_file : stdout, 
			       why, what, 0);
      if (procfs_file)
	fflush (procfs_file);
    }
}


void
_initialize_proc_api (void)
{
  struct cmd_list_element *c;

  c = add_set_cmd ("procfs-trace", no_class,
		   var_boolean, (char *) &procfs_trace, 
		   "Set tracing for /proc api calls.\n", &setlist);

  add_show_from_set (c, &showlist);
  set_cmd_sfunc (c, set_procfs_trace_cmd);
  set_cmd_completer (c, filename_completer);

  c = add_set_cmd ("procfs-file", no_class, var_filename,
		   (char *) &procfs_filename, 
		   "Set filename for /proc tracefile.\n", &setlist);

  add_show_from_set (c, &showlist);
  set_cmd_sfunc (c, set_procfs_file_cmd);
}

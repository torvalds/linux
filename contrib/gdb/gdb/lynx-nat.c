/* Native-dependent code for LynxOS.

   Copyright 1993, 1994, 1995, 1996, 1999, 2000, 2001, 2003 Free
   Software Foundation, Inc.

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
#include "gdbcore.h"
#include "regcache.h"

#include <sys/ptrace.h>
#Include "gdb_wait.h"
#include <sys/fpp.h>

static unsigned long registers_addr (int pid);
static void fetch_core_registers (char *, unsigned, int, CORE_ADDR);

#define X(ENTRY)(offsetof(struct econtext, ENTRY))

#ifdef I386
/* Mappings from tm-i386v.h */

static int regmap[] =
{
  X (eax),
  X (ecx),
  X (edx),
  X (ebx),
  X (esp),			/* sp */
  X (ebp),			/* fp */
  X (esi),
  X (edi),
  X (eip),			/* pc */
  X (flags),			/* ps */
  X (cs),
  X (ss),
  X (ds),
  X (es),
  X (ecode),			/* Lynx doesn't give us either fs or gs, so */
  X (fault),			/* we just substitute these two in the hopes
				   that they are useful. */
};
#endif /* I386 */

#ifdef M68K
/* Mappings from tm-m68k.h */

static int regmap[] =
{
  X (regs[0]),			/* d0 */
  X (regs[1]),			/* d1 */
  X (regs[2]),			/* d2 */
  X (regs[3]),			/* d3 */
  X (regs[4]),			/* d4 */
  X (regs[5]),			/* d5 */
  X (regs[6]),			/* d6 */
  X (regs[7]),			/* d7 */
  X (regs[8]),			/* a0 */
  X (regs[9]),			/* a1 */
  X (regs[10]),			/* a2 */
  X (regs[11]),			/* a3 */
  X (regs[12]),			/* a4 */
  X (regs[13]),			/* a5 */
  X (regs[14]),			/* fp */
  offsetof (st_t, usp) - offsetof (st_t, ec),	/* sp */
  X (status),			/* ps */
  X (pc),

  X (fregs[0 * 3]),		/* fp0 */
  X (fregs[1 * 3]),		/* fp1 */
  X (fregs[2 * 3]),		/* fp2 */
  X (fregs[3 * 3]),		/* fp3 */
  X (fregs[4 * 3]),		/* fp4 */
  X (fregs[5 * 3]),		/* fp5 */
  X (fregs[6 * 3]),		/* fp6 */
  X (fregs[7 * 3]),		/* fp7 */

  X (fcregs[0]),		/* fpcontrol */
  X (fcregs[1]),		/* fpstatus */
  X (fcregs[2]),		/* fpiaddr */
  X (ssw),			/* fpcode */
  X (fault),			/* fpflags */
};
#endif /* M68K */

#ifdef SPARC
/* Mappings from tm-sparc.h */

#define FX(ENTRY)(offsetof(struct fcontext, ENTRY))

static int regmap[] =
{
  -1,				/* g0 */
  X (g1),
  X (g2),
  X (g3),
  X (g4),
  -1,				/* g5->g7 aren't saved by Lynx */
  -1,
  -1,

  X (o[0]),
  X (o[1]),
  X (o[2]),
  X (o[3]),
  X (o[4]),
  X (o[5]),
  X (o[6]),			/* sp */
  X (o[7]),			/* ra */

  -1, -1, -1, -1, -1, -1, -1, -1,	/* l0 -> l7 */

  -1, -1, -1, -1, -1, -1, -1, -1,	/* i0 -> i7 */

  FX (f.fregs[0]),		/* f0 */
  FX (f.fregs[1]),
  FX (f.fregs[2]),
  FX (f.fregs[3]),
  FX (f.fregs[4]),
  FX (f.fregs[5]),
  FX (f.fregs[6]),
  FX (f.fregs[7]),
  FX (f.fregs[8]),
  FX (f.fregs[9]),
  FX (f.fregs[10]),
  FX (f.fregs[11]),
  FX (f.fregs[12]),
  FX (f.fregs[13]),
  FX (f.fregs[14]),
  FX (f.fregs[15]),
  FX (f.fregs[16]),
  FX (f.fregs[17]),
  FX (f.fregs[18]),
  FX (f.fregs[19]),
  FX (f.fregs[20]),
  FX (f.fregs[21]),
  FX (f.fregs[22]),
  FX (f.fregs[23]),
  FX (f.fregs[24]),
  FX (f.fregs[25]),
  FX (f.fregs[26]),
  FX (f.fregs[27]),
  FX (f.fregs[28]),
  FX (f.fregs[29]),
  FX (f.fregs[30]),
  FX (f.fregs[31]),

  X (y),
  X (psr),
  X (wim),
  X (tbr),
  X (pc),
  X (npc),
  FX (fsr),			/* fpsr */
  -1,				/* cpsr */
};
#endif /* SPARC */

#ifdef rs6000

static int regmap[] =
{
  X (iregs[0]),			/* r0 */
  X (iregs[1]),
  X (iregs[2]),
  X (iregs[3]),
  X (iregs[4]),
  X (iregs[5]),
  X (iregs[6]),
  X (iregs[7]),
  X (iregs[8]),
  X (iregs[9]),
  X (iregs[10]),
  X (iregs[11]),
  X (iregs[12]),
  X (iregs[13]),
  X (iregs[14]),
  X (iregs[15]),
  X (iregs[16]),
  X (iregs[17]),
  X (iregs[18]),
  X (iregs[19]),
  X (iregs[20]),
  X (iregs[21]),
  X (iregs[22]),
  X (iregs[23]),
  X (iregs[24]),
  X (iregs[25]),
  X (iregs[26]),
  X (iregs[27]),
  X (iregs[28]),
  X (iregs[29]),
  X (iregs[30]),
  X (iregs[31]),

  X (fregs[0]),			/* f0 */
  X (fregs[1]),
  X (fregs[2]),
  X (fregs[3]),
  X (fregs[4]),
  X (fregs[5]),
  X (fregs[6]),
  X (fregs[7]),
  X (fregs[8]),
  X (fregs[9]),
  X (fregs[10]),
  X (fregs[11]),
  X (fregs[12]),
  X (fregs[13]),
  X (fregs[14]),
  X (fregs[15]),
  X (fregs[16]),
  X (fregs[17]),
  X (fregs[18]),
  X (fregs[19]),
  X (fregs[20]),
  X (fregs[21]),
  X (fregs[22]),
  X (fregs[23]),
  X (fregs[24]),
  X (fregs[25]),
  X (fregs[26]),
  X (fregs[27]),
  X (fregs[28]),
  X (fregs[29]),
  X (fregs[30]),
  X (fregs[31]),

  X (srr0),			/* IAR (PC) */
  X (srr1),			/* MSR (PS) */
  X (cr),			/* CR */
  X (lr),			/* LR */
  X (ctr),			/* CTR */
  X (xer),			/* XER */
  X (mq)			/* MQ */
};

#endif /* rs6000 */

#if defined (I386) || defined (M68K) || defined (rs6000)

/* Return the offset relative to the start of the per-thread data to the
   saved context block.  */

static unsigned long
registers_addr (int pid)
{
  CORE_ADDR stblock;
  int ecpoff = offsetof (st_t, ecp);
  CORE_ADDR ecp;

  errno = 0;
  stblock = (CORE_ADDR) ptrace (PTRACE_THREADUSER, pid, (PTRACE_ARG3_TYPE) 0,
				0);
  if (errno)
    perror_with_name ("ptrace(PTRACE_THREADUSER)");

  ecp = (CORE_ADDR) ptrace (PTRACE_PEEKTHREAD, pid, (PTRACE_ARG3_TYPE) ecpoff,
			    0);
  if (errno)
    perror_with_name ("ptrace(PTRACE_PEEKTHREAD)");

  return ecp - stblock;
}

/* Fetch one or more registers from the inferior.  REGNO == -1 to get
   them all.  We actually fetch more than requested, when convenient,
   marking them as valid so we won't fetch them again.  */

void
fetch_inferior_registers (int regno)
{
  int reglo, reghi;
  int i;
  unsigned long ecp;

  if (regno == -1)
    {
      reglo = 0;
      reghi = NUM_REGS - 1;
    }
  else
    reglo = reghi = regno;

  ecp = registers_addr (PIDGET (inferior_ptid));

  {
    char buf[MAX_REGISTER_SIZE];
    for (regno = reglo; regno <= reghi; regno++)
      {
	int ptrace_fun = PTRACE_PEEKTHREAD;
	
#ifdef M68K
	ptrace_fun = regno == SP_REGNUM ? PTRACE_PEEKUSP : PTRACE_PEEKTHREAD;
#endif
	
	for (i = 0; i < DEPRECATED_REGISTER_RAW_SIZE (regno); i += sizeof (int))
	  {
	    unsigned int reg;
	    
	    errno = 0;
	    reg = ptrace (ptrace_fun, PIDGET (inferior_ptid),
			  (PTRACE_ARG3_TYPE) (ecp + regmap[regno] + i), 0);
	    if (errno)
	      perror_with_name ("ptrace(PTRACE_PEEKUSP)");
	    
	    *(int *) &buf[i] = reg;
	  }
	supply_register (regno, buf);
      }
  }
}

/* Store our register values back into the inferior.
   If REGNO is -1, do this for all registers.
   Otherwise, REGNO specifies which register (so we can save time).  */

void
store_inferior_registers (int regno)
{
  int reglo, reghi;
  int i;
  unsigned long ecp;

  if (regno == -1)
    {
      reglo = 0;
      reghi = NUM_REGS - 1;
    }
  else
    reglo = reghi = regno;

  ecp = registers_addr (PIDGET (inferior_ptid));

  for (regno = reglo; regno <= reghi; regno++)
    {
      int ptrace_fun = PTRACE_POKEUSER;

      if (CANNOT_STORE_REGISTER (regno))
	continue;

#ifdef M68K
      ptrace_fun = regno == SP_REGNUM ? PTRACE_POKEUSP : PTRACE_POKEUSER;
#endif

      for (i = 0; i < DEPRECATED_REGISTER_RAW_SIZE (regno); i += sizeof (int))
	{
	  unsigned int reg;

	  reg = *(unsigned int *) &deprecated_registers[DEPRECATED_REGISTER_BYTE (regno) + i];

	  errno = 0;
	  ptrace (ptrace_fun, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) (ecp + regmap[regno] + i), reg);
	  if (errno)
	    perror_with_name ("ptrace(PTRACE_POKEUSP)");
	}
    }
}
#endif /* defined (I386) || defined (M68K) || defined (rs6000) */

/* Wait for child to do something.  Return pid of child, or -1 in case
   of error; store status through argument pointer OURSTATUS.  */

ptid_t
child_wait (ptid_t ptid, struct target_waitstatus *ourstatus)
{
  int save_errno;
  int thread;
  union wait status;
  int pid;

  while (1)
    {
      int sig;

      set_sigint_trap ();	/* Causes SIGINT to be passed on to the
				   attached process. */
      pid = wait (&status);

      save_errno = errno;

      clear_sigint_trap ();

      if (pid == -1)
	{
	  if (save_errno == EINTR)
	    continue;
	  fprintf_unfiltered (gdb_stderr, "Child process unexpectedly missing: %s.\n",
			      safe_strerror (save_errno));
	  /* Claim it exited with unknown signal.  */
	  ourstatus->kind = TARGET_WAITKIND_SIGNALLED;
	  ourstatus->value.sig = TARGET_SIGNAL_UNKNOWN;
	  return -1;
	}

      if (pid != PIDGET (inferior_ptid))	/* Some other process?!? */
	continue;

      thread = status.w_tid;	/* Get thread id from status */

      /* Initial thread value can only be acquired via wait, so we have to
         resort to this hack.  */

      if (TIDGET (inferior_ptid) == 0 && thread != 0)
	{
	  inferior_ptid = MERGEPID (PIDGET (inferior_ptid), thread);
	  add_thread (inferior_ptid);
	}

      ptid = BUILDPID (pid, thread);

      /* We've become a single threaded process again.  */
      if (thread == 0)
	inferior_ptid = ptid;

      /* Check for thread creation.  */
      if (WIFSTOPPED (status)
	  && WSTOPSIG (status) == SIGTRAP
	  && !in_thread_list (ptid))
	{
	  int realsig;

	  realsig = ptrace (PTRACE_GETTRACESIG, PIDGET (ptid),
	                    (PTRACE_ARG3_TYPE) 0, 0);

	  if (realsig == SIGNEWTHREAD)
	    {
	      /* It's a new thread notification.  We don't want to much with
	         realsig -- the code in wait_for_inferior expects SIGTRAP. */
	      ourstatus->kind = TARGET_WAITKIND_SPURIOUS;
	      ourstatus->value.sig = TARGET_SIGNAL_0;
	      return ptid;
	    }
	  else
	    error ("Signal for unknown thread was not SIGNEWTHREAD");
	}

      /* Check for thread termination.  */
      else if (WIFSTOPPED (status)
	       && WSTOPSIG (status) == SIGTRAP
	       && in_thread_list (ptid))
	{
	  int realsig;

	  realsig = ptrace (PTRACE_GETTRACESIG, PIDGET (ptid),
	                    (PTRACE_ARG3_TYPE) 0, 0);

	  if (realsig == SIGTHREADEXIT)
	    {
	      ptrace (PTRACE_CONT, PIDGET (ptid), (PTRACE_ARG3_TYPE) 0, 0);
	      continue;
	    }
	}

#ifdef SPARC
      /* SPARC Lynx uses an byte reversed wait status; we must use the
         host macros to access it.  These lines just a copy of
         store_waitstatus.  We can't use CHILD_SPECIAL_WAITSTATUS
         because target.c can't include the Lynx <sys/wait.h>.  */
      if (WIFEXITED (status))
	{
	  ourstatus->kind = TARGET_WAITKIND_EXITED;
	  ourstatus->value.integer = WEXITSTATUS (status);
	}
      else if (!WIFSTOPPED (status))
	{
	  ourstatus->kind = TARGET_WAITKIND_SIGNALLED;
	  ourstatus->value.sig =
	    target_signal_from_host (WTERMSIG (status));
	}
      else
	{
	  ourstatus->kind = TARGET_WAITKIND_STOPPED;
	  ourstatus->value.sig =
	    target_signal_from_host (WSTOPSIG (status));
	}
#else
      store_waitstatus (ourstatus, status.w_status);
#endif

      return ptid;
    }
}

/* Return nonzero if the given thread is still alive.  */
int
child_thread_alive (ptid_t ptid)
{
  int pid = PIDGET (ptid);

  /* Arggh.  Apparently pthread_kill only works for threads within
     the process that calls pthread_kill.

     We want to avoid the lynx signal extensions as they simply don't
     map well to the generic gdb interface we want to keep.

     All we want to do is determine if a particular thread is alive;
     it appears as if we can just make a harmless thread specific
     ptrace call to do that.  */
  return (ptrace (PTRACE_THREADUSER, pid, 0, 0) != -1);
}

/* Resume execution of the inferior process.
   If STEP is nonzero, single-step it.
   If SIGNAL is nonzero, give it that signal.  */

void
child_resume (ptid_t ptid, int step, enum target_signal signal)
{
  int func;
  int pid = PIDGET (ptid);

  errno = 0;

  /* If pid == -1, then we want to step/continue all threads, else
     we only want to step/continue a single thread.  */
  if (pid == -1)
    {
      pid = PIDGET (inferior_ptid);
      func = step ? PTRACE_SINGLESTEP : PTRACE_CONT;
    }
  else
    func = step ? PTRACE_SINGLESTEP_ONE : PTRACE_CONT_ONE;


  /* An address of (PTRACE_ARG3_TYPE)1 tells ptrace to continue from where
     it was.  (If GDB wanted it to start some other way, we have already
     written a new PC value to the child.)

     If this system does not support PT_STEP, a higher level function will
     have called single_step() to transmute the step request into a
     continue request (by setting breakpoints on all possible successor
     instructions), so we don't have to worry about that here.  */

  ptrace (func, pid, (PTRACE_ARG3_TYPE) 1, target_signal_to_host (signal));

  if (errno)
    perror_with_name ("ptrace");
}

/* Convert a Lynx process ID to a string.  Returns the string in a static
   buffer.  */

char *
child_pid_to_str (ptid_t ptid)
{
  static char buf[40];

  sprintf (buf, "process %d thread %d", PIDGET (ptid), TIDGET (ptid));

  return buf;
}

/* Extract the register values out of the core file and store
   them where `read_register' will find them.

   CORE_REG_SECT points to the register values themselves, read into memory.
   CORE_REG_SIZE is the size of that area.
   WHICH says which set of registers we are handling (0 = int, 2 = float
   on machines where they are discontiguous).
   REG_ADDR is the offset from u.u_ar0 to the register values relative to
   core_reg_sect.  This is used with old-fashioned core files to
   locate the registers in a large upage-plus-stack ".reg" section.
   Original upage address X is at location core_reg_sect+x+reg_addr.
 */

static void
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size, int which,
		      CORE_ADDR reg_addr)
{
  struct st_entry s;
  unsigned int regno;

  for (regno = 0; regno < NUM_REGS; regno++)
    if (regmap[regno] != -1)
      supply_register (regno, core_reg_sect + offsetof (st_t, ec)
		       + regmap[regno]);

#ifdef SPARC
/* Fetching this register causes all of the I & L regs to be read from the
   stack and validated.  */

  fetch_inferior_registers (I0_REGNUM);
#endif
}


/* Register that we are able to handle lynx core file formats.
   FIXME: is this really bfd_target_unknown_flavour? */

static struct core_fns lynx_core_fns =
{
  bfd_target_unknown_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_core_registers,			/* core_read_registers */
  NULL					/* next */
};

void
_initialize_core_lynx (void)
{
  add_core_fns (&lynx_core_fns);
}

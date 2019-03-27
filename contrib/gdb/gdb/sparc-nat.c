/* Native-dependent code for SPARC.

   Copyright 2003, 2004 Free Software Foundation, Inc.

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
#include "inferior.h"
#include "regcache.h"
#include "target.h"

#include "gdb_assert.h"
#include <signal.h>
#include "gdb_string.h"
#include <sys/ptrace.h>
#include "gdb_wait.h"
#ifdef HAVE_MACHINE_REG_H
#include <machine/reg.h>
#endif

#include "sparc-tdep.h"
#include "sparc-nat.h"

/* With some trickery we can use the code in this file for most (if
   not all) ptrace(2) based SPARC systems, which includes SunOS 4,
   GNU/Linux and the various SPARC BSD's.

   First, we need a data structure for use with ptrace(2).  SunOS has
   `struct regs' and `struct fp_status' in <machine/reg.h>.  BSD's
   have `struct reg' and `struct fpreg' in <machine/reg.h>.  GNU/Linux
   has the same structures as SunOS 4, but they're in <asm/reg.h>,
   which is a kernel header.  As a general rule we avoid including
   GNU/Linux kernel headers.  Fortunately GNU/Linux has a `gregset_t'
   and a `fpregset_t' that are equivalent to `struct regs' and `struct
   fp_status' in <sys/ucontext.h>, which is automatically included by
   <signal.h>.  Settling on using the `gregset_t' and `fpregset_t'
   typedefs, providing them for the other systems, therefore solves
   the puzzle.  */

#ifdef HAVE_MACHINE_REG_H
#ifdef HAVE_STRUCT_REG
typedef struct reg gregset_t;
typedef struct fpreg fpregset_t;
#else 
typedef struct regs gregset_t;
typedef struct fp_status fpregset_t;
#endif
#endif

/* Second, we need to remap the BSD ptrace(2) requests to their SunOS
   equivalents.  GNU/Linux already follows SunOS here.  */

#ifndef PTRACE_GETREGS
#define PTRACE_GETREGS PT_GETREGS
#endif

#ifndef PTRACE_SETREGS
#define PTRACE_SETREGS PT_SETREGS
#endif

#ifndef PTRACE_GETFPREGS
#define PTRACE_GETFPREGS PT_GETFPREGS
#endif

#ifndef PTRACE_SETFPREGS
#define PTRACE_SETFPREGS PT_SETFPREGS
#endif

/* Register set description.  */
const struct sparc_gregset *sparc_gregset;
void (*sparc_supply_gregset) (const struct sparc_gregset *,
			      struct regcache *, int , const void *);
void (*sparc_collect_gregset) (const struct sparc_gregset *,
			       const struct regcache *, int, void *);
void (*sparc_supply_fpregset) (struct regcache *, int , const void *);
void (*sparc_collect_fpregset) (const struct regcache *, int , void *);
int (*sparc_gregset_supplies_p) (int);
int (*sparc_fpregset_supplies_p) (int);

/* Determine whether `gregset_t' contains register REGNUM.  */

int
sparc32_gregset_supplies_p (int regnum)
{
  /* Integer registers.  */
  if ((regnum >= SPARC_G1_REGNUM && regnum <= SPARC_G7_REGNUM)
      || (regnum >= SPARC_O0_REGNUM && regnum <= SPARC_O7_REGNUM)
      || (regnum >= SPARC_L0_REGNUM && regnum <= SPARC_L7_REGNUM)
      || (regnum >= SPARC_I0_REGNUM && regnum <= SPARC_I7_REGNUM))
    return 1;

  /* Control registers.  */
  if (regnum == SPARC32_PC_REGNUM
      || regnum == SPARC32_NPC_REGNUM
      || regnum == SPARC32_PSR_REGNUM
      || regnum == SPARC32_Y_REGNUM)
    return 1;

  return 0;
}

/* Determine whether `fpregset_t' contains register REGNUM.  */

int
sparc32_fpregset_supplies_p (int regnum)
{
  /* Floating-point registers.  */
  if (regnum >= SPARC_F0_REGNUM && regnum <= SPARC_F31_REGNUM)
    return 1;

  /* Control registers.  */
  if (regnum == SPARC32_FSR_REGNUM)
    return 1;

  return 0;
}

/* Fetch register REGNUM from the inferior.  If REGNUM is -1, do this
   for all registers (including the floating-point registers).  */

void
fetch_inferior_registers (int regnum)
{
  struct regcache *regcache = current_regcache;
  int pid;

  /* NOTE: cagney/2002-12-03: This code assumes that the currently
     selected light weight processes' registers can be written
     directly into the selected thread's register cache.  This works
     fine when given an 1:1 LWP:thread model (such as found on
     GNU/Linux) but will, likely, have problems when used on an N:1
     (userland threads) or N:M (userland multiple LWP) model.  In the
     case of the latter two, the LWP's registers do not necessarily
     belong to the selected thread (the LWP could be in the middle of
     executing the thread switch code).

     These functions should instead be paramaterized with an explicit
     object (struct regcache, struct thread_info?) into which the LWPs
     registers can be written.  */
  pid = TIDGET (inferior_ptid);
  if (pid == 0)
    pid = PIDGET (inferior_ptid);

  if (regnum == SPARC_G0_REGNUM)
    {
      regcache_raw_supply (regcache, SPARC_G0_REGNUM, NULL);
      return;
    }

  if (regnum == -1 || sparc_gregset_supplies_p (regnum))
    {
      gregset_t regs;

      if (ptrace (PTRACE_GETREGS, pid, (PTRACE_ARG3_TYPE) &regs, 0) == -1)
	perror_with_name ("Couldn't get registers");

      sparc_supply_gregset (sparc_gregset, regcache, -1, &regs);
      if (regnum != -1)
	return;
    }

  if (regnum == -1 || sparc_fpregset_supplies_p (regnum))
    {
      fpregset_t fpregs;

      if (ptrace (PTRACE_GETFPREGS, pid, (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name ("Couldn't get floating point status");

      sparc_supply_fpregset (regcache, -1, &fpregs);
    }
}

void
store_inferior_registers (int regnum)
{
  struct regcache *regcache = current_regcache;
  int pid;

  /* NOTE: cagney/2002-12-02: See comment in fetch_inferior_registers
     about threaded assumptions.  */
  pid = TIDGET (inferior_ptid);
  if (pid == 0)
    pid = PIDGET (inferior_ptid);

  if (regnum == -1 || sparc_gregset_supplies_p (regnum))
    {
      gregset_t regs;

      if (ptrace (PTRACE_GETREGS, pid, (PTRACE_ARG3_TYPE) &regs, 0) == -1)
	perror_with_name ("Couldn't get registers");

      sparc_collect_gregset (sparc_gregset, regcache, regnum, &regs);

      if (ptrace (PTRACE_SETREGS, pid, (PTRACE_ARG3_TYPE) &regs, 0) == -1)
	perror_with_name ("Couldn't write registers");

      /* Deal with the stack regs.  */
      if (regnum == -1 || regnum == SPARC_SP_REGNUM
	  || (regnum >= SPARC_L0_REGNUM && regnum <= SPARC_I7_REGNUM))
	{
	  ULONGEST sp;

	  regcache_cooked_read_unsigned (regcache, SPARC_SP_REGNUM, &sp);
	  sparc_collect_rwindow (regcache, sp, regnum);
	}

      if (regnum != -1)
	return;
    }

  if (regnum == -1 || sparc_fpregset_supplies_p (regnum))
    {
      fpregset_t fpregs, saved_fpregs;

      if (ptrace (PTRACE_GETFPREGS, pid, (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name ("Couldn't get floating-point registers");

      memcpy (&saved_fpregs, &fpregs, sizeof (fpregs));
      sparc_collect_fpregset (regcache, regnum, &fpregs);

      /* Writing the floating-point registers will fail on NetBSD with
	 EINVAL if the inferior process doesn't have an FPU state
	 (i.e. if it didn't use the FPU yet).  Therefore we don't try
	 to write the registers if nothing changed.  */
      if (memcmp (&saved_fpregs, &fpregs, sizeof (fpregs)) != 0)
	{
	  if (ptrace (PTRACE_SETFPREGS, pid,
		      (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	    perror_with_name ("Couldn't write floating-point registers");
	}

      if (regnum != -1)
	return;
    }
}


/* Fetch StackGhost Per-Process XOR cookie.  */

LONGEST
sparc_xfer_wcookie (struct target_ops *ops, enum target_object object,
		    const char *annex, void *readbuf, const void *writebuf,
		    ULONGEST offset, LONGEST len)
{
  unsigned long wcookie = 0;
  char *buf = (char *)&wcookie;

  gdb_assert (object == TARGET_OBJECT_WCOOKIE);
  gdb_assert (readbuf && writebuf == NULL);

  if (offset >= sizeof (unsigned long))
    return -1;

#ifdef PT_WCOOKIE
  /* If PT_WCOOKIE is defined (by <sys/ptrace.h>), assume we're
     running on an OpenBSD release that uses StackGhost (3.1 or
     later).  As of release 3.4, OpenBSD doesn't use a randomized
     cookie yet, but a future release probably will.  */
  {
    int pid;

    pid = TIDGET (inferior_ptid);
    if (pid == 0)
      pid = PIDGET (inferior_ptid);

    /* Sanity check.  The proper type for a cookie is register_t, but
       we can't assume that this type exists on all systems supported
       by the code in this file.  */
    gdb_assert (sizeof (wcookie) == sizeof (register_t));

    /* Fetch the cookie.  */
    if (ptrace (PT_WCOOKIE, pid, (PTRACE_ARG3_TYPE) &wcookie, 0) == -1)
      {
	if (errno != EINVAL)
	  perror_with_name ("Couldn't get StackGhost cookie");

	/* Although PT_WCOOKIE is defined on OpenBSD 3.1 and later,
	   the request wasn't implemented until after OpenBSD 3.4.  If
	   the kernel doesn't support the PT_WCOOKIE request, assume
	   we're running on a kernel that uses non-randomized cookies.  */
	wcookie = 0x3;
      }
  }
#endif /* PT_WCOOKIE */

  if (len > sizeof (unsigned long) - offset)
    len = sizeof (unsigned long) - offset;

  memcpy (readbuf, buf + offset, len);
  return len;
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_sparc_nat (void);

void
_initialize_sparc_nat (void)
{
  /* Deafult to using SunOS 4 register sets.  */
  if (sparc_gregset == NULL)
    sparc_gregset = &sparc32_sunos4_gregset;
  if (sparc_supply_gregset == NULL)
    sparc_supply_gregset = sparc32_supply_gregset;
  if (sparc_collect_gregset == NULL)
    sparc_collect_gregset = sparc32_collect_gregset;
  if (sparc_supply_fpregset == NULL)
    sparc_supply_fpregset = sparc32_supply_fpregset;
  if (sparc_collect_fpregset == NULL)
    sparc_collect_fpregset = sparc32_collect_fpregset;
  if (sparc_gregset_supplies_p == NULL)
    sparc_gregset_supplies_p = sparc32_gregset_supplies_p;
  if (sparc_fpregset_supplies_p == NULL)
    sparc_fpregset_supplies_p = sparc32_fpregset_supplies_p;
}

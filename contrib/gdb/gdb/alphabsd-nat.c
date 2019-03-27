/* Native-dependent code for Alpha BSD's.
   Copyright 2000, 2001, 2002 Free Software Foundation, Inc.

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

#include "alpha-tdep.h"
#include "alphabsd-tdep.h"

#include <sys/types.h>
#include <sys/ptrace.h>
#include <machine/reg.h>

#ifdef HAVE_SYS_PROCFS_H
#include <sys/procfs.h>
#endif

#ifndef HAVE_GREGSET_T
typedef struct reg gregset_t;
#endif

#ifndef HAVE_FPREGSET_T 
typedef struct fpreg fpregset_t; 
#endif 

#include "gregset.h"

/* Provide *regset() wrappers around the generic Alpha BSD register
   supply/fill routines.  */

void
supply_gregset (gregset_t *gregsetp)
{
  alphabsd_supply_reg ((char *) gregsetp, -1);
}

void
fill_gregset (gregset_t *gregsetp, int regno)
{
  alphabsd_fill_reg ((char *) gregsetp, regno);
}

void
supply_fpregset (fpregset_t *fpregsetp)
{
  alphabsd_supply_fpreg ((char *) fpregsetp, -1);
}

void
fill_fpregset (fpregset_t *fpregsetp, int regno)
{
  alphabsd_fill_fpreg ((char *) fpregsetp, regno);
}

/* Determine if PT_GETREGS fetches this register.  */

static int
getregs_supplies (int regno)
{
  return ((regno >= ALPHA_V0_REGNUM && regno <= ALPHA_ZERO_REGNUM)
	  || regno >= PC_REGNUM);
}


/* Fetch register REGNO from the inferior.  If REGNO is -1, do this
   for all registers (including the floating point registers).  */

void
fetch_inferior_registers (int regno)
{
  if (regno == -1 || getregs_supplies (regno))
    {
      struct reg gregs;

      if (ptrace (PT_GETREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &gregs, 0) == -1)
	perror_with_name ("Couldn't get registers");

      alphabsd_supply_reg ((char *) &gregs, regno);
      if (regno != -1)
	return;
    }

  if (regno == -1 || regno >= FP0_REGNUM)
    {
      struct fpreg fpregs;

      if (ptrace (PT_GETFPREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name ("Couldn't get floating point status");

      alphabsd_supply_fpreg ((char *) &fpregs, regno);
    }
}

/* Store register REGNO back into the inferior.  If REGNO is -1, do
   this for all registers (including the floating point registers).  */

void
store_inferior_registers (int regno)
{
  if (regno == -1 || getregs_supplies (regno))
    {
      struct reg gregs;
      if (ptrace (PT_GETREGS, PIDGET (inferior_ptid),
                  (PTRACE_ARG3_TYPE) &gregs, 0) == -1)
        perror_with_name ("Couldn't get registers");

      alphabsd_fill_reg ((char *) &gregs, regno);

      if (ptrace (PT_SETREGS, PIDGET (inferior_ptid),
                  (PTRACE_ARG3_TYPE) &gregs, 0) == -1)
        perror_with_name ("Couldn't write registers");

      if (regno != -1)
	return;
    }

  if (regno == -1 || regno >= FP0_REGNUM)
    {
      struct fpreg fpregs;

      if (ptrace (PT_GETFPREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name ("Couldn't get floating point status");

      alphabsd_fill_fpreg ((char *) &fpregs, regno);

      if (ptrace (PT_SETFPREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name ("Couldn't write floating point status");
    }
}

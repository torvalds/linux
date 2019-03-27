/* Native-dependent code for PowerPC's running FreeBSD, for GDB.
   Copyright 2002, 2004 Free Software Foundation, Inc.
   Contributed by Wasabi Systems, Inc.

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

#include <sys/types.h>
#include <sys/ptrace.h>
#include <machine/reg.h>
#include <machine/frame.h>

#include "defs.h"
#include "inferior.h"
#include "gdb_assert.h"
#include "gdbcore.h"
#include "regcache.h"

#include "ppc-tdep.h"
#include "ppcfbsd-tdep.h"

/* Returns true if PT_GETREGS fetches this register.  */
static int
getregs_supplies (int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  return ((regno >= tdep->ppc_gp0_regnum && regno <= tdep->ppc_gplast_regnum)
          || regno == tdep->ppc_lr_regnum
          || regno == tdep->ppc_cr_regnum
          || regno == tdep->ppc_xer_regnum
          || regno == tdep->ppc_ctr_regnum
	  || regno == PC_REGNUM);
}

/* Like above, but for PT_GETFPREGS.  */
static int
getfpregs_supplies (int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);

  /* FIXME: jimb/2004-05-05: Some PPC variants don't have floating
     point registers.  Traditionally, GDB's register set has still
     listed the floating point registers for such machines, so this
     code is harmless.  However, the new E500 port actually omits the
     floating point registers entirely from the register set --- they
     don't even have register numbers assigned to them.

     It's not clear to me how best to update this code, so this assert
     will alert the first person to encounter the NetBSD/E500
     combination to the problem.  */
  gdb_assert (ppc_floating_point_unit_p (current_gdbarch));

  return ((regno >= FP0_REGNUM && regno <= FPLAST_REGNUM)
	  || regno == tdep->ppc_fpscr_regnum);
}

void
fetch_inferior_registers (int regno)
{
  if (regno == -1 || getregs_supplies (regno))
    {
      struct reg regs;

      if (ptrace (PT_GETREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &regs, 0) == -1)
        perror_with_name (_("Couldn't get registers"));

      ppcfbsd_supply_reg ((char *) &regs, regno);
      if (regno != -1)
	return;
    }

  if (regno == -1 || getfpregs_supplies (regno))
    {
      struct fpreg fpregs;

      if (ptrace (PT_GETFPREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name (_("Couldn't get FP registers"));

      ppcfbsd_supply_fpreg ((char *) &fpregs, regno);
      if (regno != -1)
	return;
    }
}

void
store_inferior_registers (int regno)
{
  if (regno == -1 || getregs_supplies (regno))
    {
      struct reg regs;

      if (ptrace (PT_GETREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &regs, 0) == -1)
	perror_with_name (_("Couldn't get registers"));

      ppcfbsd_fill_reg ((char *) &regs, regno);

      if (ptrace (PT_SETREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &regs, 0) == -1)
	perror_with_name (_("Couldn't write registers"));

      if (regno != -1)
	return;
    }

  if (regno == -1 || getfpregs_supplies (regno))
    {
      struct fpreg fpregs;

      if (ptrace (PT_GETFPREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name (_("Couldn't get FP registers"));

      ppcfbsd_fill_fpreg ((char *) &fpregs, regno);
      
      if (ptrace (PT_SETFPREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name (_("Couldn't set FP registers"));
    }
}

void
fill_gregset (char *regs, int regnum)
{
  ppcfbsd_fill_reg (regs, regnum);
}

void
supply_gregset (char *regs)
{
  ppcfbsd_supply_reg (regs, -1);
}

void
fill_fpregset (char *fpregs, int regnum)
{
  ppcfbsd_fill_fpreg (fpregs, regnum);
}

void
supply_fpregset (char *fpregs)
{
  ppcfbsd_supply_fpreg (fpregs, -1);
}

/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_ppcfbsd_nat (void);

void
_initialize_ppcfbsd_nat (void)
{
}

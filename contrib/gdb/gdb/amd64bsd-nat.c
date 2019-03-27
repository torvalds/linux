/* Native-dependent code for AMD64 BSD's.

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

/* We include <signal.h> to make sure `struct fxsave64' is defined on
   NetBSD, since NetBSD's <machine/reg.h> needs it.  */
#include "gdb_assert.h"
#include <signal.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <machine/reg.h>

#include "amd64-tdep.h"
#include "amd64-nat.h"


/* Fetch register REGNUM from the inferior.  If REGNUM is -1, do this
   for all registers (including the floating-point registers).  */

void
fetch_inferior_registers (int regnum)
{
  if (regnum == -1 || amd64_native_gregset_supplies_p (regnum))
    {
      struct reg regs;

      if (ptrace (PT_GETREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &regs, 0) == -1)
	perror_with_name ("Couldn't get registers");

      amd64_supply_native_gregset (current_regcache, &regs, -1);
      if (regnum != -1)
	return;
    }

  if (regnum == -1 || regnum >= AMD64_ST0_REGNUM)
    {
      struct fpreg fpregs;

      if (ptrace (PT_GETFPREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name ("Couldn't get floating point status");

      amd64_supply_fxsave (current_regcache, -1, &fpregs);
    }
}

/* Store register REGNUM back into the inferior.  If REGNUM is -1, do
   this for all registers (including the floating-point registers).  */

void
store_inferior_registers (int regnum)
{
  if (regnum == -1 || amd64_native_gregset_supplies_p (regnum))
    {
      struct reg regs;

      if (ptrace (PT_GETREGS, PIDGET (inferior_ptid),
                  (PTRACE_ARG3_TYPE) &regs, 0) == -1)
        perror_with_name ("Couldn't get registers");

      amd64_collect_native_gregset (current_regcache, &regs, regnum);

      if (ptrace (PT_SETREGS, PIDGET (inferior_ptid),
	          (PTRACE_ARG3_TYPE) &regs, 0) == -1)
        perror_with_name ("Couldn't write registers");

      if (regnum != -1)
	return;
    }

  if (regnum == -1 || regnum >= AMD64_ST0_REGNUM)
    {
      struct fpreg fpregs;

      if (ptrace (PT_GETFPREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name ("Couldn't get floating point status");

      amd64_fill_fxsave ((char *) &fpregs, regnum);

      if (ptrace (PT_SETFPREGS, PIDGET (inferior_ptid),
		  (PTRACE_ARG3_TYPE) &fpregs, 0) == -1)
	perror_with_name ("Couldn't write floating point status");
    }
}

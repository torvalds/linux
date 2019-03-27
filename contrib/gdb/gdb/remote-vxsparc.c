/* SPARC-specific portions of the RPC protocol for VxWorks.

   Contributed by Wind River Systems.

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
#include "regcache.h"

#include "gdb_string.h"

#include "sparc-tdep.h"

#include "vx-share/ptrace.h"
#include "vx-share/regPacket.h"

#define SPARC_R_G1	(SPARC_R_G0 + SPARC_GREG_SIZE)

const struct sparc_gregset vxsparc_gregset =
{
  SPARC_R_PSR,			/* %psr */
  SPARC_R_PC,			/* %pc */
  SPARC_R_NPC,			/* %npc */
  SPARC_R_Y,			/* %y */
  SPARC_R_WIM,			/* %wim */
  SPARC_R_TBR,			/* %tbr */
  SPARC_R_G1,			/* %g1 */
  SPARC_R_I0			/* %l0 */
};

/* Flag set if target has an FPU.  */

extern int target_has_fp;

/* Generic register read/write routines in remote-vx.c.  */

extern void net_read_registers ();
extern void net_write_registers ();

/* Read a register or registers from the VxWorks target.  REGNUM is
   the register to read, or -1 for all; currently, it is ignored.
   FIXME: Look at REGNUM to improve efficiency.  */

void
vx_read_register (int regnum)
{
  struct regcache *regcache = current_regcache;
  char gregs[SPARC_GREG_PLEN];
  char fpregs[SPARC_FPREG_PLEN];
  CORE_ADDR sp;

  /* Get the general-purpose registers.  */
  net_read_registers (gregs, SPARC_GREG_PLEN, PTRACE_GETREGS);
  sparc32_supply_gregset (&vxsparc_gregset, regcache, -1, gregs);

  /* If the target has floating-point registers, fetch them.
     Otherwise, zero the floating-point register values in GDB's
     register cache for good measure, even though we might not need
     to.  */
  if (target_has_fp)
    net_read_registers (fpregs, SPARC_FPREG_PLEN, PTRACE_GETFPREGS);
  else
    memset (fpregs, 0, SPARC_FPREG_PLEN);
  sparc32_supply_fpregset (regcache, -1, fpregs);
}

/* Store a register or registers into the VxWorks target.  REGNUM is
   the register to store, or -1 for all; currently, it is ignored.
   FIXME: Look at REGNUM to improve efficiency.  */

void
vx_write_register (int regnum)
{
  struct regcache *regcache = current_regcache;
  char gregs[SPARC_GREG_PLEN];
  char fpregs[SPARC_FPREG_PLEN];
  int gregs_p = 1;
  int fpregs_p = 1;
  CORE_ADDR sp;

  if (regnum != -1)
    {
      if ((SPARC_G0_REGNUM <= regnum && regnum <= SPARC_I7_REGNUM)
	  || (SPARC32_Y_REGNUM <= regnum && regnum <= SPARC32_NPC_REGNUM))
	fpregs_p = 0;
      else
	gregs_p = 0;
    }

  /* Store the general-purpose registers.  */
  if (gregs_p)
    {
      sparc32_collect_gregset (&vxsparc_gregset, regcache, -1, gregs);
      net_write_registers (gregs, SPARC_GREG_PLEN, PTRACE_SETREGS);

      /* Deal with the stack regs.  */
      if (regnum == -1 || regnum == SPARC_SP_REGNUM
	  || (regnum >= SPARC_L0_REGNUM && regnum <= SPARC_I7_REGNUM))
	{
	  ULONGEST sp;

	  regcache_cooked_read_unsigned (regcache, SPARC_SP_REGNUM, &sp);
	  sparc_collect_rwindow (regcache, sp, regnum);
	}
    }

  /* Store the floating-point registers if the target has them.  */
  if (fpregs_p && target_has_fp)
    {
      sparc32_collect_fpregset (regcache, -1, fpregs);
      net_write_registers (fpregs, SPARC_FPREG_PLEN, PTRACE_SETFPREGS);
    }
}

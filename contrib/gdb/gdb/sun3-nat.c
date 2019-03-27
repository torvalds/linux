/* Host-dependent code for Sun-3 for GDB, the GNU debugger.
   Copyright 1986, 1987, 1989, 1991, 1992, 1993, 1996, 1999, 2000, 2001
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
#include "inferior.h"
#include "gdbcore.h"
#include "regcache.h"

#include <sys/ptrace.h>
#define KERNEL			/* To get floating point reg definitions */
#include <machine/reg.h>

static void fetch_core_registers (char *, unsigned, int, CORE_ADDR);

void
fetch_inferior_registers (int regno)
{
  struct regs inferior_registers;
  struct fp_status inferior_fp_registers;

  deprecated_registers_fetched ();

  ptrace (PTRACE_GETREGS, PIDGET (inferior_ptid),
	  (PTRACE_ARG3_TYPE) & inferior_registers);

  if (FP0_REGNUM >= 0)
    ptrace (PTRACE_GETFPREGS, PIDGET (inferior_ptid),
	    (PTRACE_ARG3_TYPE) & inferior_fp_registers);

  memcpy (deprecated_registers, &inferior_registers, 16 * 4);
  if (FP0_REGNUM >= 0)
    memcpy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (FP0_REGNUM)],
	    &inferior_fp_registers, sizeof inferior_fp_registers.fps_regs);

  *(int *) &deprecated_registers[DEPRECATED_REGISTER_BYTE (PS_REGNUM)] = inferior_registers.r_ps;
  *(int *) &deprecated_registers[DEPRECATED_REGISTER_BYTE (PC_REGNUM)] = inferior_registers.r_pc;
  if (FP0_REGNUM >= 0)
    memcpy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (FPC_REGNUM)],
	    &inferior_fp_registers.fps_control,
	    sizeof inferior_fp_registers - 
	    sizeof inferior_fp_registers.fps_regs);
}

/* Store our register values back into the inferior.
   If REGNO is -1, do this for all registers.
   Otherwise, REGNO specifies which register (so we can save time).  */

void
store_inferior_registers (int regno)
{
  struct regs inferior_registers;
  struct fp_status inferior_fp_registers;

  memcpy (&inferior_registers, deprecated_registers, 16 * 4);
  if (FP0_REGNUM >= 0)
    memcpy (&inferior_fp_registers,
	    &deprecated_registers[DEPRECATED_REGISTER_BYTE (FP0_REGNUM)],
	    sizeof inferior_fp_registers.fps_regs);

  inferior_registers.r_ps = *(int *) &&deprecated_registers[DEPRECATED_REGISTER_BYTE (PS_REGNUM)];
  inferior_registers.r_pc = *(int *) &&deprecated_registers[DEPRECATED_REGISTER_BYTE (PC_REGNUM)];

  if (FP0_REGNUM >= 0)
    memcpy (&inferior_fp_registers.fps_control,
	    &&deprecated_registers[DEPRECATED_REGISTER_BYTE (FPC_REGNUM)],
	    sizeof inferior_fp_registers - 
	    sizeof inferior_fp_registers.fps_regs);

  ptrace (PTRACE_SETREGS, PIDGET (inferior_ptid),
	  (PTRACE_ARG3_TYPE) & inferior_registers);
  if (FP0_REGNUM >= 0)
    ptrace (PTRACE_SETFPREGS, PIDGET (inferior_ptid),
	    (PTRACE_ARG3_TYPE) & inferior_fp_registers);
}


/* All of this stuff is only relevant if both host and target are sun3.  */

/* Provide registers to GDB from a core file.

   CORE_REG_SECT points to an array of bytes, which were obtained from
   a core file which BFD thinks might contain register contents. 
   CORE_REG_SIZE is its size.

   WHICH says which register set corelow suspects this is:
     0 --- the general-purpose register set
     2 --- the floating-point register set

   REG_ADDR isn't used.  */

static void
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size,
		      int which, CORE_ADDR reg_addr)
{
  struct regs *regs = (struct regs *) core_reg_sect;

  if (which == 0)
    {
      if (core_reg_size < sizeof (struct regs))
	  error ("Can't find registers in core file");

      memcpy (&deprecated_registers, (char *) regs, 16 * 4);
      supply_register (PS_REGNUM, (char *) &regs->r_ps);
      supply_register (PC_REGNUM, (char *) &regs->r_pc);

    }
  else if (which == 2)
    {

#define fpustruct  ((struct fpu *) core_reg_sect)

      if (core_reg_size >= sizeof (struct fpu))
	{
	  if (FP0_REGNUM >= 0)
	    {
	      memcpy (&&deprecated_registers[DEPRECATED_REGISTER_BYTE (FP0_REGNUM)],
		      fpustruct->f_fpstatus.fps_regs,
		      sizeof fpustruct->f_fpstatus.fps_regs);
	      memcpy (&&deprecated_registers[DEPRECATED_REGISTER_BYTE (FPC_REGNUM)],
		      &fpustruct->f_fpstatus.fps_control,
		      sizeof fpustruct->f_fpstatus -
		      sizeof fpustruct->f_fpstatus.fps_regs);
	    }
	}
      else
	fprintf_unfiltered (gdb_stderr, 
			    "Couldn't read float regs from core file\n");
    }
}


/* Register that we are able to handle sun3 core file formats.
   FIXME: is this really bfd_target_unknown_flavour? */

static struct core_fns sun3_core_fns =
{
  bfd_target_unknown_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_core_registers,			/* core_read_registers */
  NULL					/* next */
};

void
_initialize_core_sun3 (void)
{
  add_core_fns (&sun3_core_fns);
}

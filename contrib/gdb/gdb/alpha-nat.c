/* Low level Alpha interface, for GDB when running native.
   Copyright 1993, 1995, 1996, 1998, 1999, 2000, 2001, 2003
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
#include "gdb_string.h"
#include "inferior.h"
#include "gdbcore.h"
#include "target.h"
#include "regcache.h"

#include "alpha-tdep.h"

#include <sys/ptrace.h>
#ifdef __linux__
#include <asm/reg.h>
#include <alpha/ptrace.h>
#else
#include <alpha/coreregs.h>
#endif
#include <sys/user.h>

/* Prototypes for local functions. */

static void fetch_osf_core_registers (char *, unsigned, int, CORE_ADDR);
static void fetch_elf_core_registers (char *, unsigned, int, CORE_ADDR);

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
fetch_osf_core_registers (char *core_reg_sect, unsigned core_reg_size,
			  int which, CORE_ADDR reg_addr)
{
  int regno;
  int addr;
  int bad_reg = -1;

  /* Table to map a gdb regnum to an index in the core register
     section.  The floating point register values are garbage in
     OSF/1.2 core files.  OSF5 uses different names for the register
     enum list, need to handle two cases.  The actual values are the
     same.  */
  static int const core_reg_mapping[ALPHA_NUM_REGS] =
  {
#ifdef NCF_REGS
#define EFL NCF_REGS
    CF_V0, CF_T0, CF_T1, CF_T2, CF_T3, CF_T4, CF_T5, CF_T6,
    CF_T7, CF_S0, CF_S1, CF_S2, CF_S3, CF_S4, CF_S5, CF_S6,
    CF_A0, CF_A1, CF_A2, CF_A3, CF_A4, CF_A5, CF_T8, CF_T9,
    CF_T10, CF_T11, CF_RA, CF_T12, CF_AT, CF_GP, CF_SP, -1,
    EFL + 0, EFL + 1, EFL + 2, EFL + 3, EFL + 4, EFL + 5, EFL + 6, EFL + 7,
    EFL + 8, EFL + 9, EFL + 10, EFL + 11, EFL + 12, EFL + 13, EFL + 14, EFL + 15,
    EFL + 16, EFL + 17, EFL + 18, EFL + 19, EFL + 20, EFL + 21, EFL + 22, EFL + 23,
    EFL + 24, EFL + 25, EFL + 26, EFL + 27, EFL + 28, EFL + 29, EFL + 30, EFL + 31,
    CF_PC, -1
#else
#define EFL (EF_SIZE / 8)
    EF_V0, EF_T0, EF_T1, EF_T2, EF_T3, EF_T4, EF_T5, EF_T6,
    EF_T7, EF_S0, EF_S1, EF_S2, EF_S3, EF_S4, EF_S5, EF_S6,
    EF_A0, EF_A1, EF_A2, EF_A3, EF_A4, EF_A5, EF_T8, EF_T9,
    EF_T10, EF_T11, EF_RA, EF_T12, EF_AT, EF_GP, EF_SP, -1,
    EFL + 0, EFL + 1, EFL + 2, EFL + 3, EFL + 4, EFL + 5, EFL + 6, EFL + 7,
    EFL + 8, EFL + 9, EFL + 10, EFL + 11, EFL + 12, EFL + 13, EFL + 14, EFL + 15,
    EFL + 16, EFL + 17, EFL + 18, EFL + 19, EFL + 20, EFL + 21, EFL + 22, EFL + 23,
    EFL + 24, EFL + 25, EFL + 26, EFL + 27, EFL + 28, EFL + 29, EFL + 30, EFL + 31,
    EF_PC, -1
#endif
  };

  for (regno = 0; regno < ALPHA_NUM_REGS; regno++)
    {
      if (CANNOT_FETCH_REGISTER (regno))
	{
	  supply_register (regno, NULL);
	  continue;
	}
      addr = 8 * core_reg_mapping[regno];
      if (addr < 0 || addr >= core_reg_size)
	{
	  /* ??? UNIQUE is a new addition.  Don't generate an error.  */
	  if (regno == ALPHA_UNIQUE_REGNUM)
	    {
	      supply_register (regno, NULL);
	      continue;
	    }
	  if (bad_reg < 0)
	    bad_reg = regno;
	}
      else
	{
	  supply_register (regno, core_reg_sect + addr);
	}
    }
  if (bad_reg >= 0)
    {
      error ("Register %s not found in core file.", REGISTER_NAME (bad_reg));
    }
}

static void
fetch_elf_core_registers (char *core_reg_sect, unsigned core_reg_size,
			  int which, CORE_ADDR reg_addr)
{
  if (core_reg_size < 32 * 8)
    {
      error ("Core file register section too small (%u bytes).", core_reg_size);
      return;
    }

  switch (which)
    {
    case 0: /* integer registers */
      /* PC is in slot 32; UNIQUE is in slot 33, if present.  */
      alpha_supply_int_regs (-1, core_reg_sect, core_reg_sect + 31*8,
			     (core_reg_size >= 33 * 8
			      ? core_reg_sect + 32*8 : NULL));
      break;

    case 2: /* floating-point registers */
      /* FPCR is in slot 32.  */
      alpha_supply_fp_regs (-1, core_reg_sect, core_reg_sect + 31*8);
      break;

    default:
      break;
    }
}


/* Map gdb internal register number to a ptrace ``address''.
   These ``addresses'' are defined in <sys/ptrace.h>, with
   the exception of ALPHA_UNIQUE_PTRACE_ADDR.  */

#ifndef ALPHA_UNIQUE_PTRACE_ADDR
#define ALPHA_UNIQUE_PTRACE_ADDR 0
#endif

CORE_ADDR
register_addr (int regno, CORE_ADDR blockend)
{
  if (regno == PC_REGNUM)
    return PC;
  if (regno == ALPHA_UNIQUE_REGNUM)
    return ALPHA_UNIQUE_PTRACE_ADDR;
  if (regno < FP0_REGNUM)
    return GPR_BASE + regno;
  else
    return FPR_BASE + regno - FP0_REGNUM;
}

int
kernel_u_size (void)
{
  return (sizeof (struct user));
}

#if defined(USE_PROC_FS) || defined(HAVE_GREGSET_T)
#include <sys/procfs.h>

/* Prototypes for supply_gregset etc. */
#include "gregset.h"

/* Locate the UNIQUE value within the gregset_t.  */
#ifndef ALPHA_REGSET_UNIQUE
#define ALPHA_REGSET_UNIQUE(ptr) NULL
#endif

/*
 * See the comment in m68k-tdep.c regarding the utility of these functions.
 */

void
supply_gregset (gdb_gregset_t *gregsetp)
{
  long *regp = ALPHA_REGSET_BASE (gregsetp);
  void *unique = ALPHA_REGSET_UNIQUE (gregsetp);

  /* PC is in slot 32.  */
  alpha_supply_int_regs (-1, regp, regp + 31, unique);
}

void
fill_gregset (gdb_gregset_t *gregsetp, int regno)
{
  long *regp = ALPHA_REGSET_BASE (gregsetp);
  void *unique = ALPHA_REGSET_UNIQUE (gregsetp);

  /* PC is in slot 32.  */
  alpha_fill_int_regs (regno, regp, regp + 31, unique);
}

/*
 * Now we do the same thing for floating-point registers.
 * Again, see the comments in m68k-tdep.c.
 */

void
supply_fpregset (gdb_fpregset_t *fpregsetp)
{
  long *regp = ALPHA_REGSET_BASE (fpregsetp);

  /* FPCR is in slot 32.  */
  alpha_supply_fp_regs (-1, regp, regp + 31);
}

void
fill_fpregset (gdb_fpregset_t *fpregsetp, int regno)
{
  long *regp = ALPHA_REGSET_BASE (fpregsetp);

  /* FPCR is in slot 32.  */
  alpha_fill_fp_regs (regno, regp, regp + 31);
}
#endif


/* Register that we are able to handle alpha core file formats. */

static struct core_fns alpha_osf_core_fns =
{
  /* This really is bfd_target_unknown_flavour.  */

  bfd_target_unknown_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_osf_core_registers,		/* core_read_registers */
  NULL					/* next */
};

static struct core_fns alpha_elf_core_fns =
{
  bfd_target_elf_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_elf_core_registers,		/* core_read_registers */
  NULL					/* next */
};

void
_initialize_core_alpha (void)
{
  add_core_fns (&alpha_osf_core_fns);
  add_core_fns (&alpha_elf_core_fns);
}

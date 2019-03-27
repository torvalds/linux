/* Target-dependent code for PowerPC systems running NetBSD.
   Copyright 2002, 2003 Free Software Foundation, Inc.
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

#include "defs.h"
#include "gdbcore.h"
#include "regcache.h"
#include "target.h"
#include "breakpoint.h"
#include "value.h"
#include "osabi.h"

#include "ppc-tdep.h"
#include "ppcnbsd-tdep.h"
#include "nbsd-tdep.h"

#include "solib-svr4.h"

#define REG_FIXREG_OFFSET(x)	((x) * 4)
#define REG_LR_OFFSET		(32 * 4)
#define REG_CR_OFFSET		(33 * 4)
#define REG_XER_OFFSET		(34 * 4)
#define REG_CTR_OFFSET		(35 * 4)
#define REG_PC_OFFSET		(36 * 4)
#define SIZEOF_STRUCT_REG	(37 * 4)

#define FPREG_FPR_OFFSET(x)	((x) * 8)
#define FPREG_FPSCR_OFFSET	(32 * 8)
#define SIZEOF_STRUCT_FPREG	(33 * 8)

void
ppcnbsd_supply_reg (char *regs, int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int i;

  for (i = 0; i <= 31; i++)
    {
      if (regno == i || regno == -1)
	supply_register (i, regs + REG_FIXREG_OFFSET (i));
    }

  if (regno == tdep->ppc_lr_regnum || regno == -1)
    supply_register (tdep->ppc_lr_regnum, regs + REG_LR_OFFSET);

  if (regno == tdep->ppc_cr_regnum || regno == -1)
    supply_register (tdep->ppc_cr_regnum, regs + REG_CR_OFFSET);

  if (regno == tdep->ppc_xer_regnum || regno == -1)
    supply_register (tdep->ppc_xer_regnum, regs + REG_XER_OFFSET);

  if (regno == tdep->ppc_ctr_regnum || regno == -1)
    supply_register (tdep->ppc_ctr_regnum, regs + REG_CTR_OFFSET);

  if (regno == PC_REGNUM || regno == -1)
    supply_register (PC_REGNUM, regs + REG_PC_OFFSET);
}

void
ppcnbsd_fill_reg (char *regs, int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int i;

  for (i = 0; i <= 31; i++)
    {
      if (regno == i || regno == -1)
	regcache_collect (i, regs + REG_FIXREG_OFFSET (i));
    }

  if (regno == tdep->ppc_lr_regnum || regno == -1)
    regcache_collect (tdep->ppc_lr_regnum, regs + REG_LR_OFFSET);

  if (regno == tdep->ppc_cr_regnum || regno == -1)
    regcache_collect (tdep->ppc_cr_regnum, regs + REG_CR_OFFSET);

  if (regno == tdep->ppc_xer_regnum || regno == -1)
    regcache_collect (tdep->ppc_xer_regnum, regs + REG_XER_OFFSET);

  if (regno == tdep->ppc_ctr_regnum || regno == -1)
    regcache_collect (tdep->ppc_ctr_regnum, regs + REG_CTR_OFFSET);

  if (regno == PC_REGNUM || regno == -1)
    regcache_collect (PC_REGNUM, regs + REG_PC_OFFSET);
}

void
ppcnbsd_supply_fpreg (char *fpregs, int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int i;

  for (i = FP0_REGNUM; i <= FP0_REGNUM + 31; i++)
    {
      if (regno == i || regno == -1)
	supply_register (i, fpregs + FPREG_FPR_OFFSET (i - FP0_REGNUM));
    }

  if (regno == tdep->ppc_fpscr_regnum || regno == -1)
    supply_register (tdep->ppc_fpscr_regnum, fpregs + FPREG_FPSCR_OFFSET);
}

void
ppcnbsd_fill_fpreg (char *fpregs, int regno)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (current_gdbarch);
  int i;

  for (i = FP0_REGNUM; i <= FP0_REGNUM + 31; i++)
    {
      if (regno == i || regno == -1)
	regcache_collect (i, fpregs + FPREG_FPR_OFFSET (i - FP0_REGNUM));
    }

  if (regno == tdep->ppc_fpscr_regnum || regno == -1)
    regcache_collect (tdep->ppc_fpscr_regnum, fpregs + FPREG_FPSCR_OFFSET);
}

static void
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size, int which,
                      CORE_ADDR ignore)
{
  char *regs, *fpregs;

  /* We get everything from one section.  */
  if (which != 0)
    return;

  regs = core_reg_sect;
  fpregs = core_reg_sect + SIZEOF_STRUCT_REG;

  /* Integer registers.  */
  ppcnbsd_supply_reg (regs, -1);

  /* Floating point registers.  */
  ppcnbsd_supply_fpreg (fpregs, -1);
}

static void
fetch_elfcore_registers (char *core_reg_sect, unsigned core_reg_size, int which,
                         CORE_ADDR ignore)
{
  switch (which)
    {
    case 0:  /* Integer registers.  */
      if (core_reg_size != SIZEOF_STRUCT_REG)
	warning ("Wrong size register set in core file.");
      else
	ppcnbsd_supply_reg (core_reg_sect, -1);
      break;

    case 2:  /* Floating point registers.  */
      if (core_reg_size != SIZEOF_STRUCT_FPREG)
	warning ("Wrong size FP register set in core file.");
      else
	ppcnbsd_supply_fpreg (core_reg_sect, -1);
      break;

    default:
      /* Don't know what kind of register request this is; just ignore it.  */
      break;
    }
}

static struct core_fns ppcnbsd_core_fns =
{
  bfd_target_unknown_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_core_registers,			/* core_read_registers */
  NULL					/* next */
};

static struct core_fns ppcnbsd_elfcore_fns =
{
  bfd_target_elf_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_elfcore_registers,		/* core_read_registers */
  NULL					/* next */
};

static int
ppcnbsd_pc_in_sigtramp (CORE_ADDR pc, char *func_name)
{
  /* FIXME: Need to add support for kernel-provided signal trampolines.  */
  return (nbsd_pc_in_sigtramp (pc, func_name));
}

/* NetBSD is confused.  It appears that 1.5 was using the correct SVr4
   convention but, 1.6 switched to the below broken convention.  For
   the moment use the broken convention.  Ulgh!.  */

static enum return_value_convention
ppcnbsd_return_value (struct gdbarch *gdbarch, struct type *valtype,
		      struct regcache *regcache, void *readbuf,
		      const void *writebuf)
{
  if ((TYPE_CODE (valtype) == TYPE_CODE_STRUCT
       || TYPE_CODE (valtype) == TYPE_CODE_UNION)
      && !((TYPE_LENGTH (valtype) == 16 || TYPE_LENGTH (valtype) == 8)
	    && TYPE_VECTOR (valtype))
      && !(TYPE_LENGTH (valtype) == 1
	   || TYPE_LENGTH (valtype) == 2
	   || TYPE_LENGTH (valtype) == 4
	   || TYPE_LENGTH (valtype) == 8))
    return RETURN_VALUE_STRUCT_CONVENTION;
  else
    return ppc_sysv_abi_broken_return_value (gdbarch, valtype, regcache,
					     readbuf, writebuf);
}

static void
ppcnbsd_init_abi (struct gdbarch_info info,
                  struct gdbarch *gdbarch)
{
  set_gdbarch_pc_in_sigtramp (gdbarch, ppcnbsd_pc_in_sigtramp);
  /* For NetBSD, this is an on again, off again thing.  Some systems
     do use the broken struct convention, and some don't.  */
  set_gdbarch_return_value (gdbarch, ppcnbsd_return_value);
  set_solib_svr4_fetch_link_map_offsets (gdbarch,
                                nbsd_ilp32_solib_svr4_fetch_link_map_offsets);
}

void
_initialize_ppcnbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_powerpc, 0, GDB_OSABI_NETBSD_ELF,
			  ppcnbsd_init_abi);

  add_core_fns (&ppcnbsd_core_fns);
  add_core_fns (&ppcnbsd_elfcore_fns);
}

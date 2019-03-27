/* Native-dependent code for NetBSD/sparc64.

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

#include "sparc64-tdep.h"
#include "sparc-nat.h"

/* NetBSD is different from the other OSes that support both SPARC and
   UltraSPARC in that the result of ptrace(2) depends on whether the
   traced process is 32-bit or 64-bit.  */

static void
sparc64nbsd_supply_gregset (const struct sparc_gregset *gregset,
			    struct regcache *regcache,
			    int regnum, const void *gregs)
{
  int sparc32 = (gdbarch_ptr_bit (current_gdbarch) == 32);

  if (sparc32)
    sparc32_supply_gregset (&sparc32nbsd_gregset, regcache, regnum, gregs);
  else
    sparc64_supply_gregset (&sparc64nbsd_gregset, regcache, regnum, gregs);
}

static void
sparc64nbsd_collect_gregset (const struct sparc_gregset *gregset,
			     const struct regcache *regcache,
			     int regnum, void *gregs)
{
  int sparc32 = (gdbarch_ptr_bit (current_gdbarch) == 32);

  if (sparc32)
    sparc32_collect_gregset (&sparc32nbsd_gregset, regcache, regnum, gregs);
  else
    sparc64_collect_gregset (&sparc64nbsd_gregset, regcache, regnum, gregs);
}

static void
sparc64nbsd_supply_fpregset (struct regcache *regcache,
			     int regnum, const void *fpregs)
{
  int sparc32 = (gdbarch_ptr_bit (current_gdbarch) == 32);

  if (sparc32)
    sparc32_supply_fpregset (regcache, regnum, fpregs);
  else
    sparc64_supply_fpregset (regcache, regnum, fpregs);
}

static void
sparc64nbsd_collect_fpregset (const struct regcache *regcache,
			      int regnum, void *fpregs)
{
  int sparc32 = (gdbarch_ptr_bit (current_gdbarch) == 32);

  if (sparc32)
    sparc32_collect_fpregset (regcache, regnum, fpregs);
  else
    sparc64_collect_fpregset (regcache, regnum, fpregs);
}

/* Determine whether `gregset_t' contains register REGNUM.  */

static int
sparc64nbsd_gregset_supplies_p (int regnum)
{
  if (gdbarch_ptr_bit (current_gdbarch) == 32)
    return sparc32_gregset_supplies_p (regnum);

  /* Integer registers.  */
  if ((regnum >= SPARC_G1_REGNUM && regnum <= SPARC_G7_REGNUM)
      || (regnum >= SPARC_O0_REGNUM && regnum <= SPARC_O7_REGNUM)
      || (regnum >= SPARC_L0_REGNUM && regnum <= SPARC_L7_REGNUM)
      || (regnum >= SPARC_I0_REGNUM && regnum <= SPARC_I7_REGNUM))
    return 1;

  /* Control registers.  */
  if (regnum == SPARC64_PC_REGNUM
      || regnum == SPARC64_NPC_REGNUM
      || regnum == SPARC64_STATE_REGNUM
      || regnum == SPARC64_Y_REGNUM)
    return 1;

  return 0;
}

/* Determine whether `fpregset_t' contains register REGNUM.  */

static int
sparc64nbsd_fpregset_supplies_p (int regnum)
{
  if (gdbarch_ptr_bit (current_gdbarch) == 32)
    return sparc32_fpregset_supplies_p (regnum);

  /* Floating-point registers.  */
  if ((regnum >= SPARC_F0_REGNUM && regnum <= SPARC_F31_REGNUM)
      || (regnum >= SPARC64_F32_REGNUM && regnum <= SPARC64_F62_REGNUM))
    return 1;

  /* Control registers.  */
  if (regnum == SPARC64_FSR_REGNUM)
    return 1;

  return 0;
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_sparcnbsd_nat (void);

void
_initialize_sparcnbsd_nat (void)
{
  sparc_supply_gregset = sparc64nbsd_supply_gregset;
  sparc_collect_gregset = sparc64nbsd_collect_gregset;
  sparc_supply_fpregset = sparc64nbsd_supply_fpregset;
  sparc_collect_fpregset = sparc64nbsd_collect_fpregset;
  sparc_gregset_supplies_p = sparc64nbsd_gregset_supplies_p;
  sparc_fpregset_supplies_p = sparc64nbsd_fpregset_supplies_p;
}

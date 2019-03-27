/* Common target dependent code for GDB on Alpha systems running BSD.
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

#include "alpha-tdep.h"
#include "alphabsd-tdep.h"

/* Conviently, GDB uses the same register numbering as the
   ptrace register structure used by BSD on Alpha.  */

void
alphabsd_supply_reg (char *regs, int regno)
{
  /* PC is at slot 32; UNIQUE not present.  */
  alpha_supply_int_regs (regno, regs, regs + 31*8, NULL);
}

void
alphabsd_fill_reg (char *regs, int regno)
{
  /* PC is at slot 32; UNIQUE not present.  */
  alpha_fill_int_regs (regno, regs, regs + 31*8, NULL);
}

void
alphabsd_supply_fpreg (char *fpregs, int regno)
{
  /* FPCR is at slot 33; slot 32 unused.  */
  alpha_supply_fp_regs (regno, fpregs, fpregs + 32*8);
}

void
alphabsd_fill_fpreg (char *fpregs, int regno)
{
  /* FPCR is at slot 33; slot 32 unused.  */
  alpha_fill_fp_regs (regno, fpregs, fpregs + 32*8);
}

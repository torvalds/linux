/* Traditional frame unwind support, for GDB the GNU Debugger.

   Copyright 2003 Free Software Foundation, Inc.

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
#include "frame.h"
#include "trad-frame.h"
#include "regcache.h"

/* A traditional frame is unwound by analysing the function prologue
   and using the information gathered to track registers.  For
   non-optimized frames, the technique is reliable (just need to check
   for all potential instruction sequences).  */

struct trad_frame_saved_reg *
trad_frame_alloc_saved_regs (struct frame_info *next_frame)
{
  int regnum;
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  int numregs = NUM_REGS + NUM_PSEUDO_REGS;
  struct trad_frame_saved_reg *this_saved_regs
    = FRAME_OBSTACK_CALLOC (numregs, struct trad_frame_saved_reg);
  for (regnum = 0; regnum < numregs; regnum++)
    {
      this_saved_regs[regnum].realreg = regnum;
      this_saved_regs[regnum].addr = -1;
    }      
  return this_saved_regs;
}

enum { REG_VALUE = -1, REG_UNKNOWN = -2 };

int
trad_frame_value_p (struct trad_frame_saved_reg this_saved_regs[], int regnum)
{
  return (this_saved_regs[regnum].realreg == REG_VALUE);
}

int
trad_frame_addr_p (struct trad_frame_saved_reg this_saved_regs[], int regnum)
{
  return (this_saved_regs[regnum].realreg >= 0
	  && this_saved_regs[regnum].addr != -1);
}

int
trad_frame_realreg_p (struct trad_frame_saved_reg this_saved_regs[],
		      int regnum)
{
  return (this_saved_regs[regnum].realreg >= 0
	  && this_saved_regs[regnum].addr == -1);
}

void
trad_frame_set_value (struct trad_frame_saved_reg this_saved_regs[],
		      int regnum, LONGEST val)
{
  /* Make the REALREG invalid, indicating that the ADDR contains the
     register's value.  */
  this_saved_regs[regnum].realreg = REG_VALUE;
  this_saved_regs[regnum].addr = val;
}

void
trad_frame_set_unknown (struct trad_frame_saved_reg this_saved_regs[],
			int regnum)
{
  /* Make the REALREG invalid, indicating that the value is not known.  */
  this_saved_regs[regnum].realreg = REG_UNKNOWN;
  this_saved_regs[regnum].addr = -1;
}

void
trad_frame_prev_register (struct frame_info *next_frame,
			  struct trad_frame_saved_reg this_saved_regs[],
			  int regnum, int *optimizedp,
			  enum lval_type *lvalp, CORE_ADDR *addrp,
			  int *realregp, void *bufferp)
{
  struct gdbarch *gdbarch = get_frame_arch (next_frame);
  if (trad_frame_addr_p (this_saved_regs, regnum))
    {
      /* The register was saved in memory.  */
      *optimizedp = 0;
      *lvalp = lval_memory;
      *addrp = this_saved_regs[regnum].addr;
      *realregp = -1;
      if (bufferp != NULL)
	{
	  /* Read the value in from memory.  */
	  get_frame_memory (next_frame, this_saved_regs[regnum].addr, bufferp,
			    register_size (gdbarch, regnum));
	}
    }
  else if (trad_frame_realreg_p (this_saved_regs, regnum))
    {
      /* Ask the next frame to return the value of the register.  */
      frame_register_unwind (next_frame, this_saved_regs[regnum].realreg,
			     optimizedp, lvalp, addrp, realregp, bufferp);
    }
  else if (trad_frame_value_p (this_saved_regs, regnum))
    {
      /* The register's value is available.  */
      *optimizedp = 0;
      *lvalp = not_lval;
      *addrp = 0;
      *realregp = -1;
      if (bufferp != NULL)
	store_unsigned_integer (bufferp, register_size (gdbarch, regnum),
				this_saved_regs[regnum].addr);
    }
  else
    {
      error ("Register %s not available",
	     gdbarch_register_name (gdbarch, regnum));
    }
}

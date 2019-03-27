/* Extract registers from a "standard" core file, for GDB.
   Copyright 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1998,
   1999, 2000, 2001 Free Software Foundation, Inc.

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

/* Typically used on systems that have a.out format executables.
   corefile.c is supposed to contain the more machine-independent
   aspects of reading registers from core files, while this file is
   more machine specific.  */

#include "defs.h"

#ifdef HAVE_PTRACE_H
#include <ptrace.h>
#else
#ifdef HAVE_SYS_PTRACE_H
#include <sys/ptrace.h>
#endif
#endif

#include <sys/types.h>
#include <sys/param.h>
#include "gdbcore.h"
#include "value.h"		/* For supply_register.  */
#include "regcache.h"

/* These are needed on various systems to expand REGISTER_U_ADDR.  */
#ifndef USG
#include "gdb_dirent.h"
#include <sys/file.h>
#include "gdb_stat.h"
#include <sys/user.h>
#endif

#ifndef CORE_REGISTER_ADDR
#define CORE_REGISTER_ADDR(regno, regptr) register_addr(regno, regptr)
#endif /* CORE_REGISTER_ADDR */

#ifdef NEED_SYS_CORE_H
#include <sys/core.h>
#endif

static void fetch_core_registers (char *, unsigned, int, CORE_ADDR);

void _initialize_core_aout (void);

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
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size, int which,
		      CORE_ADDR reg_addr)
{
  int regno;
  CORE_ADDR addr;
  int bad_reg = -1;
  CORE_ADDR reg_ptr = -reg_addr;	/* Original u.u_ar0 is -reg_addr. */
  int numregs = NUM_REGS;

  /* If u.u_ar0 was an absolute address in the core file, relativize it now,
     so we can use it as an offset into core_reg_sect.  When we're done,
     "register 0" will be at core_reg_sect+reg_ptr, and we can use
     CORE_REGISTER_ADDR to offset to the other registers.  If this is a modern
     core file without a upage, reg_ptr will be zero and this is all a big
     NOP.  */
  if (reg_ptr > core_reg_size)
    reg_ptr -= KERNEL_U_ADDR;

  for (regno = 0; regno < numregs; regno++)
    {
      addr = CORE_REGISTER_ADDR (regno, reg_ptr);
      if (addr >= core_reg_size
	  && bad_reg < 0)
	bad_reg = regno;
      else
	supply_register (regno, core_reg_sect + addr);
    }

  if (bad_reg >= 0)
    error ("Register %s not found in core file.", REGISTER_NAME (bad_reg));
}


#ifdef REGISTER_U_ADDR

/* Return the address in the core dump or inferior of register REGNO.
   BLOCKEND is the address of the end of the user structure.  */

CORE_ADDR
register_addr (int regno, CORE_ADDR blockend)
{
  CORE_ADDR addr;

  if (regno < 0 || regno >= NUM_REGS)
    error ("Invalid register number %d.", regno);

  REGISTER_U_ADDR (addr, blockend, regno);

  return addr;
}

#endif /* REGISTER_U_ADDR */


/* Register that we are able to handle aout (trad-core) file formats.  */

static struct core_fns aout_core_fns =
{
  bfd_target_unknown_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_core_registers,			/* core_read_registers */
  NULL					/* next */
};

void
_initialize_core_aout (void)
{
  add_core_fns (&aout_core_fns);
}

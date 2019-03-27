/* Machine independent GDB support for core files on systems using "regsets".

   Copyright 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2003
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

/* This file is used by most systems that use ELF for their core
   dumps.  This includes most systems that have SVR4-ish variant of
   /proc.  For these systems, the registers are laid out the same way
   in core files as in the gregset_t and fpregset_t structures that
   are used in the interaction with /proc (Irix 4 is an exception and
   therefore doesn't use this file).  Quite a few systems without a
   SVR4-ish /proc define these structures too, and can make use of
   this code too.  */

#include "defs.h"
#include "command.h"
#include "gdbcore.h"
#include "inferior.h"
#include "target.h"

#include <fcntl.h>
#include <errno.h>
#include "gdb_string.h"
#include <time.h>
#ifdef HAVE_SYS_PROCFS_H
#include <sys/procfs.h>
#endif

/* Prototypes for supply_gregset etc.  */
#include "gregset.h"

/* Provide registers to GDB from a core file.

   CORE_REG_SECT points to an array of bytes, which are the contents
   of a `note' from a core file which BFD thinks might contain
   register contents.  CORE_REG_SIZE is its size.

   WHICH says which register set corelow suspects this is:
     0 --- the general-purpose register set, in gregset_t format
     2 --- the floating-point register set, in fpregset_t format

   REG_ADDR is ignored.  */

static void
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size, int which,
		      CORE_ADDR reg_addr)
{
  gdb_gregset_t gregset;
  gdb_fpregset_t fpregset;

  switch (which)
    {
    case 0:
      if (core_reg_size != sizeof (gregset))
	warning ("Wrong size gregset in core file.");
      else
	{
	  memcpy (&gregset, core_reg_sect, sizeof (gregset));
	  supply_gregset (&gregset);
	}
      break;

    case 2:
      if (core_reg_size != sizeof (fpregset))
	warning ("Wrong size fpregset in core file.");
      else
	{
	  memcpy (&fpregset, core_reg_sect, sizeof (fpregset));
	  if (FP0_REGNUM >= 0)
	    supply_fpregset (&fpregset);
	}
      break;

    default:
      /* We've covered all the kinds of registers we know about here,
         so this must be something we wouldn't know what to do with
         anyway.  Just ignore it.  */
      break;
    }
}


/* Register that we are able to handle ELF core file formats using
   standard procfs "regset" structures.  */

static struct core_fns regset_core_fns =
{
  bfd_target_elf_flavour,		/* core_flavour */
  default_check_format,			/* check_format */
  default_core_sniffer,			/* core_sniffer */
  fetch_core_registers,			/* core_read_registers */
  NULL					/* next */
};

/* Provide a prototype to silence -Wmissing-prototypes.  */
extern void _initialize_core_regset (void);

void
_initialize_core_regset (void)
{
  add_core_fns (&regset_core_fns);
}

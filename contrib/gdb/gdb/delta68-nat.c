/* Functions specific to running gdb native on a Motorola Delta Series sysV68.
   Copyright 1993, 1996, 1998, 2000 Free Software Foundation, Inc.

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
#include <sys/signal.h>		/* for MAXSIG in sys/user.h */
#include <sys/types.h>		/* for ushort in sys/dir.h */
#include <sys/dir.h>		/* for struct direct in sys/user.h */
#include <sys/user.h>

#include <nlist.h>

#if !defined (offsetof)
#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)
#endif

/* Return the address in the core dump or inferior of register REGNO.
   BLOCKEND is the address of the end of the user structure.  */

CORE_ADDR
register_addr (int regno, CORE_ADDR blockend)
{
  static int sysv68reg[] =
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, -1, 15, 16};

  if (regno >= 0 && regno < sizeof (sysv68reg) / sizeof (sysv68reg[0]))
    return blockend + sysv68reg[regno] * 4;
  else if (regno < FPC_REGNUM)
    return offsetof (struct user, u_fpu.regs.reg[regno - FP0_REGNUM][0]);
  else if (regno == FPC_REGNUM)
    return offsetof (struct user, u_fpu.regs.control);
  else if (regno == FPS_REGNUM)
    return offsetof (struct user, u_fpu.regs.status);
  else if (regno == FPI_REGNUM)
    return offsetof (struct user, u_fpu.regs.iaddr);
  else
    {
      fprintf_unfiltered (gdb_stderr, "\
Internal error: invalid register number %d in REGISTER_U_ADDR\n",
			  regno);
      return blockend;
    }
}

CORE_ADDR kernel_u_addr;

/* Read the value of the u area from the kernel.  */
void
_initialize_delta68_nat (void)
{
  struct nlist nl[2];

  nl[0].n_name = "u";
  nl[1].n_name = NULL;
  if (nlist ("/sysV68", nl) == 0 && nl[0].n_scnum != 0)
    kernel_u_addr = nl[0].n_value;
  else
    {
      perror ("Cannot get kernel u area address");
      exit (1);
    }
}

clear_insn_cache (void)
{
#ifdef MCT_TEXT			/* in sys/signal.h on sysV68 R3V7.1 */
  memctl (0, 4096, MCT_TEXT);
#endif
}

kernel_u_size (void)
{
  return sizeof (struct user);
}

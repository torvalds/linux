/* Native-dependent code for OpenBSD/i386.

   Copyright 2002, 2003, 2004 Free Software Foundation, Inc.

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

#include <sys/param.h>
#include <sys/sysctl.h>

#include "i386-tdep.h"

/* Prevent warning from -Wmissing-prototypes.  */
void _initialize_i386obsd_nat (void);

void
_initialize_i386obsd_nat (void)
{
  /* OpenBSD provides a vm.psstrings sysctl that we can use to locate
     the sigtramp.  That way we can still recognize a sigtramp if its
     location is changed in a new kernel.  This is especially
     important for OpenBSD, since it uses a different memory layout
     than NetBSD, yet we cannot distinguish between the two.

     Of course this is still based on the assumption that the sigtramp
     is placed directly under the location where the program arguments
     and environment can be found.  */
#ifdef VM_PSSTRINGS
  {
    struct _ps_strings _ps;
    int mib[2];
    size_t len;

    mib[0] = CTL_VM;
    mib[1] = VM_PSSTRINGS;
    len = sizeof (_ps);
    if (sysctl (mib, 2, &_ps, &len, NULL, 0) == 0)
      {
	i386obsd_sigtramp_start_addr = (CORE_ADDR)_ps.val - 128;
	i386obsd_sigtramp_end_addr = (CORE_ADDR)_ps.val;
      }
  }
#endif
}

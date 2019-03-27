/* Target-dependent code for the ia64.

   Copyright 2004 Free Software Foundation, Inc.

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

#ifndef IA64_TDEP_H
#define IA64_TDEP_H

#include "osabi.h"

/* Target-dependent structure in gdbarch.  */
struct gdbarch_tdep
{
  enum gdb_osabi osabi;		/* OS/ABI of inferior.  */

  CORE_ADDR (*sigcontext_register_address) (CORE_ADDR, int);
    			/* OS specific function which, given a frame address
			   and register number, returns the offset to the
			   given register from the start of the frame. */
  CORE_ADDR (*find_global_pointer) (CORE_ADDR);
};

#define SIGCONTEXT_REGISTER_ADDRESS \
  (gdbarch_tdep (current_gdbarch)->sigcontext_register_address)
#define FIND_GLOBAL_POINTER \
  (gdbarch_tdep (current_gdbarch)->find_global_pointer)

extern CORE_ADDR ia64_generic_find_global_pointer (CORE_ADDR);

#endif /* IA64_TDEP_H */

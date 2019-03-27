/* Common target-dependent code for NetBSD systems.
   Copyright 2002 Free Software Foundation, Inc.
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
#include "gdb_string.h"
#include "solib-svr4.h"

/* Fetch (and possibly build) an appropriate link_map_offsets
   structure for NetBSD targets using the struct offsets defined
   in <link.h> (but without actual reference to that file).
  
   This makes it possible to access NetBSD shared libraries from a
   GDB that was not built on the same platform (for cross debugging).

   We provide versions for ILP32 and LP64 NetBSD targets here.  */
  
struct link_map_offsets *
nbsd_ilp32_solib_svr4_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      lmo.r_debug_size = 16;

      lmo.r_map_offset = 4;
      lmo.r_map_size   = 4;

      lmo.link_map_size = 20;

      lmo.l_addr_offset = 0;
      lmo.l_addr_size   = 4;

      lmo.l_name_offset = 4;
      lmo.l_name_size   = 4;

      lmo.l_next_offset = 12;
      lmo.l_next_size   = 4;

      lmo.l_prev_offset = 16;
      lmo.l_prev_size   = 4;
    }

  return lmp;
}

struct link_map_offsets *
nbsd_lp64_solib_svr4_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      lmo.r_debug_size = 32;

      lmo.r_map_offset = 8;
      lmo.r_map_size   = 8;

      lmo.link_map_size = 40;

      lmo.l_addr_offset = 0;
      lmo.l_addr_size   = 8;

      lmo.l_name_offset = 8;
      lmo.l_name_size   = 8;

      lmo.l_next_offset = 24;
      lmo.l_next_size   = 8;

      lmo.l_prev_offset = 32;
      lmo.l_prev_size   = 8;
    }

  return lmp;
}

int
nbsd_pc_in_sigtramp (CORE_ADDR pc, char *func_name)
{
  /* Check for libc-provided signal trampoline.  All such trampolines
     have function names which begin with "__sigtramp".  */

  return (func_name != NULL
	  && strncmp (func_name, "__sigtramp", 10) == 0);
}

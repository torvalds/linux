/* Provide legacy r_debug and link_map support for SVR4-like native targets.
   Copyright 2000, 2001
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

#include "defs.h"
#include "gdbcore.h"
#include "solib-svr4.h"

#ifdef HAVE_LINK_H

#ifdef HAVE_NLIST_H
/* nlist.h needs to be included before link.h on some older *BSD systems. */
#include <nlist.h>
#endif

#include <link.h>

/* Fetch (and possibly build) an appropriate link_map_offsets structure
   for native targets using struct definitions from link.h.  */

static struct link_map_offsets *
legacy_svr4_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = 0;
#if defined (HAVE_STRUCT_LINK_MAP32)
  static struct link_map_offsets lmo32;
  static struct link_map_offsets *lmp32 = 0;
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)
#endif
#define fieldsize(TYPE, MEMBER) (sizeof (((TYPE *)0)->MEMBER))

  if (lmp == 0)
    {
      lmp = &lmo;

#ifdef HAVE_STRUCT_LINK_MAP_WITH_L_MEMBERS
      lmo.r_debug_size = sizeof (struct r_debug);

      lmo.r_map_offset = offsetof (struct r_debug, r_map);
      lmo.r_map_size = fieldsize (struct r_debug, r_map);

      lmo.link_map_size = sizeof (struct link_map);

      lmo.l_addr_offset = offsetof (struct link_map, l_addr);
      lmo.l_addr_size = fieldsize (struct link_map, l_addr);

      lmo.l_next_offset = offsetof (struct link_map, l_next);
      lmo.l_next_size = fieldsize (struct link_map, l_next);

      lmo.l_prev_offset = offsetof (struct link_map, l_prev);
      lmo.l_prev_size = fieldsize (struct link_map, l_prev);

      lmo.l_name_offset = offsetof (struct link_map, l_name);
      lmo.l_name_size = fieldsize (struct link_map, l_name);
#else /* !defined(HAVE_STRUCT_LINK_MAP_WITH_L_MEMBERS) */
#ifdef HAVE_STRUCT_LINK_MAP_WITH_LM_MEMBERS
      lmo.link_map_size = sizeof (struct link_map);

      lmo.l_addr_offset = offsetof (struct link_map, lm_addr);
      lmo.l_addr_size = fieldsize (struct link_map, lm_addr);

      lmo.l_next_offset = offsetof (struct link_map, lm_next);
      lmo.l_next_size = fieldsize (struct link_map, lm_next);

      lmo.l_name_offset = offsetof (struct link_map, lm_name);
      lmo.l_name_size = fieldsize (struct link_map, lm_name);
#else /* !defined(HAVE_STRUCT_LINK_MAP_WITH_LM_MEMBERS) */
#if HAVE_STRUCT_SO_MAP_WITH_SOM_MEMBERS
      lmo.link_map_size = sizeof (struct so_map);

      lmo.l_addr_offset = offsetof (struct so_map, som_addr);
      lmo.l_addr_size = fieldsize (struct so_map, som_addr);

      lmo.l_next_offset = offsetof (struct so_map, som_next);
      lmo.l_next_size = fieldsize (struct so_map, som_next);

      lmo.l_name_offset = offsetof (struct so_map, som_path);
      lmo.l_name_size = fieldsize (struct so_map, som_path);
#endif /* HAVE_STRUCT_SO_MAP_WITH_SOM_MEMBERS */
#endif /* HAVE_STRUCT_LINK_MAP_WITH_LM_MEMBERS */
#endif /* HAVE_STRUCT_LINK_MAP_WITH_L_MEMBERS */
    }

#if defined (HAVE_STRUCT_LINK_MAP32)
  if (lmp32 == 0)
    {
      lmp32 = &lmo32;

      lmo32.r_debug_size = sizeof (struct r_debug32);

      lmo32.r_map_offset = offsetof (struct r_debug32, r_map);
      lmo32.r_map_size = fieldsize (struct r_debug32, r_map);

      lmo32.link_map_size = sizeof (struct link_map32);

      lmo32.l_addr_offset = offsetof (struct link_map32, l_addr);
      lmo32.l_addr_size = fieldsize (struct link_map32, l_addr);

      lmo32.l_next_offset = offsetof (struct link_map32, l_next);
      lmo32.l_next_size = fieldsize (struct link_map32, l_next);

      lmo32.l_prev_offset = offsetof (struct link_map32, l_prev);
      lmo32.l_prev_size = fieldsize (struct link_map32, l_prev);

      lmo32.l_name_offset = offsetof (struct link_map32, l_name);
      lmo32.l_name_size = fieldsize (struct link_map32, l_name);
    }
#endif /* defined (HAVE_STRUCT_LINK_MAP32) */

#if defined (HAVE_STRUCT_LINK_MAP32)
  if (exec_bfd != NULL)
    {
      if (bfd_get_arch_size (exec_bfd) == 32)
	return lmp32;
    }
  if (TARGET_PTR_BIT == 32)
    return lmp32;
#endif
  return lmp;
}

#endif /* HAVE_LINK_H */

extern initialize_file_ftype _initialize_svr4_lm; /* -Wmissing-prototypes */

void
_initialize_svr4_lm (void)
{
#ifdef HAVE_LINK_H
  legacy_svr4_fetch_link_map_offsets_hook = legacy_svr4_fetch_link_map_offsets;
#endif /* HAVE_LINK_H */
}

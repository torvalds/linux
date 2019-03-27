/* Common target-dependent definitions for NetBSD systems.
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

#ifndef NBSD_TDEP_H
#define NBSD_TDEP_H

struct link_map_offsets *nbsd_ilp32_solib_svr4_fetch_link_map_offsets (void);
struct link_map_offsets *nbsd_lp64_solib_svr4_fetch_link_map_offsets (void);

int nbsd_pc_in_sigtramp (CORE_ADDR, char *);

#endif /* NBSD_TDEP_H */

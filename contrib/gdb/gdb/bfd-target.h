/* Very simple "bfd" target, for GDB, the GNU debugger.

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

#ifndef BFD_TARGET_H
#define BFD_TARGET_H

struct bfd;
struct target_ops;

/* Given an existing BFD, re-open it as a "struct target_ops".  On
   close, it will also close the corresponding BFD (which is like
   freopen and fdopen).  */
struct target_ops *target_bfd_reopen (struct bfd *bfd);

/* Map over ABFD's sections, creating corresponding entries in the
   target's section table.  */

void build_target_sections_from_bfd (struct target_ops *targ,
				     struct bfd *abfd);

#endif

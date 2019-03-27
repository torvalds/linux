/* dwarf2out.h - Various declarations for functions found in dwarf2out.c
   Copyright (C) 1998, 1999, 2000, 2003
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

extern void dwarf2out_decl (tree);
extern void dwarf2out_frame_debug (rtx, bool);

extern void debug_dwarf (void);
struct die_struct;
extern void debug_dwarf_die (struct die_struct *);
extern void dwarf2out_set_demangle_name_func (const char *(*) (const char *));

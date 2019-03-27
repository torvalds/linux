/* Definition of functions in convert.c.
   Copyright (C) 1993, 2000, 2003, 2004 Free Software Foundation, Inc.

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

#ifndef GCC_CONVERT_H
#define GCC_CONVERT_H

extern tree convert_to_integer (tree, tree);
extern tree convert_to_pointer (tree, tree);
extern tree convert_to_real (tree, tree);
extern tree convert_to_complex (tree, tree);
extern tree convert_to_vector (tree, tree);
/* APPLE LOCAL blocks */
extern tree convert_to_block_pointer (tree, tree);

#endif /* GCC_CONVERT_H */

/* Function integration definitions for GCC
   Copyright (C) 1990, 1995, 1998, 1999, 2000, 2001, 2003, 2004, 2005
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

#include "varray.h"

extern rtx get_hard_reg_initial_val (enum machine_mode, unsigned int);
extern rtx has_hard_reg_initial_val (enum machine_mode, unsigned int);
/* If a pseudo represents an initial hard reg (or expression), return
   it, else return NULL_RTX.  */
extern rtx get_hard_reg_initial_reg (struct function *, rtx);
/* Called from rest_of_compilation.  */
extern unsigned int emit_initial_value_sets (void);
extern void allocate_initial_values (rtx *);

/* Check whether there's any attribute in a function declaration that
   makes the function uninlinable.  Returns false if it finds any,
   true otherwise.  */
extern bool function_attribute_inlinable_p (tree);


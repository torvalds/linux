/* Basic block reordering routines for the GNU compiler.
   Copyright (C) 2000, 2003, 2004 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef GCC_CFGLAYOUT_H
#define GCC_CFGLAYOUT_H

#include "basic-block.h"

extern rtx cfg_layout_function_footer;

extern void cfg_layout_initialize (unsigned int);
extern void cfg_layout_finalize (void);
extern unsigned int insn_locators_initialize (void);
extern void reemit_insn_block_notes (void);
extern bool can_copy_bbs_p (basic_block *, unsigned);
extern void copy_bbs (basic_block *, unsigned, basic_block *,
		      edge *, unsigned, edge *, struct loop *,
		      basic_block);
extern rtx duplicate_insn_chain (rtx, rtx);

#endif /* GCC_CFGLAYOUT_H */

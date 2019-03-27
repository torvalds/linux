/* dw2gencfi.h - Support for generating Dwarf2 CFI information.
   Copyright 2003 Free Software Foundation, Inc.
   Contributed by Michal Ludvig <mludvig@suse.cz>

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef DW2GENCFI_H
#define DW2GENCFI_H

#include "elf/dwarf2.h"

struct symbol;

extern const pseudo_typeS cfi_pseudo_table[];

/* cfi_finish() is called at the end of file. It will complain if
   the last CFI wasn't properly closed by .cfi_endproc.  */
extern void cfi_finish (void);

/* Entry points for backends to add unwind information.  */
extern void cfi_new_fde (struct symbol *);
extern void cfi_end_fde (struct symbol *);
extern void cfi_set_return_column (unsigned);
extern void cfi_add_advance_loc (struct symbol *);

extern void cfi_add_CFA_offset (unsigned, offsetT);
extern void cfi_add_CFA_def_cfa (unsigned, offsetT);
extern void cfi_add_CFA_register (unsigned, unsigned);
extern void cfi_add_CFA_def_cfa_register (unsigned);
extern void cfi_add_CFA_def_cfa_offset (offsetT);
extern void cfi_add_CFA_restore (unsigned);
extern void cfi_add_CFA_undefined (unsigned);
extern void cfi_add_CFA_same_value (unsigned);
extern void cfi_add_CFA_remember_state (void);
extern void cfi_add_CFA_restore_state (void);

#endif /* DW2GENCFI_H */

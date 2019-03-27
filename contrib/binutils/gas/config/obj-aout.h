/* obj-aout.h, a.out object file format for gas, the assembler.
   Copyright 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1998, 2000,
   2002, 2003, 2005 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2,
   or (at your option) any later version.

   GAS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
   the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* Tag to validate a.out object file format processing */
#define OBJ_AOUT 1

#include "targ-cpu.h"

#include "bfd/libaout.h"

#define OUTPUT_FLAVOR bfd_target_aout_flavour

extern const pseudo_typeS aout_pseudo_table[];

#ifndef obj_pop_insert
#define obj_pop_insert() pop_insert (aout_pseudo_table)
#endif

/* Symbol table entry data type.  */

typedef struct nlist obj_symbol_type;	/* Symbol table entry.  */

/* Symbol table macros and constants */

#define S_SET_OTHER(S,V) \
  (aout_symbol (symbol_get_bfdsym (S))->other = (V))
#define S_SET_TYPE(S,T) \
  (aout_symbol (symbol_get_bfdsym (S))->type = (T))
#define S_SET_DESC(S,D)	\
  (aout_symbol (symbol_get_bfdsym (S))->desc = (D))
#define S_GET_OTHER(S) \
  (aout_symbol (symbol_get_bfdsym (S))->other)
#define S_GET_TYPE(S) \
  (aout_symbol (symbol_get_bfdsym (S))->type)
#define S_GET_DESC(S) \
  (aout_symbol (symbol_get_bfdsym (S))->desc)

asection *text_section, *data_section, *bss_section;

#define obj_frob_symbol(S,PUNT)	obj_aout_frob_symbol (S, &PUNT)
#define obj_frob_file_before_fix() obj_aout_frob_file_before_fix ()

extern void obj_aout_frob_symbol (symbolS *, int *);
extern void obj_aout_frob_file_before_fix (void);

#define obj_sec_sym_ok_for_reloc(SEC)	1

#define obj_read_begin_hook()	{;}
#define obj_symbol_new_hook(s)	{;}

#define EMIT_SECTION_SYMBOLS		0

#define AOUT_STABS

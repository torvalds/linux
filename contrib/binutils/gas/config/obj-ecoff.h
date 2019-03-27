/* ECOFF object file format header file.
   Copyright 1993, 1994, 1995, 1996, 1997, 1999, 2002, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Cygnus Support.
   Written by Ian Lance Taylor <ian@cygnus.com>.

   This file is part of GAS.

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

#define OBJ_ECOFF 1

/* Use the generic ECOFF debugging code.  */
#define ECOFF_DEBUGGING 1

#define OUTPUT_FLAVOR bfd_target_ecoff_flavour

#include "targ-cpu.h"

#include "ecoff.h"

/* For each gas symbol we keep track of which file it came from, of
   whether we have generated an ECOFF symbol for it, and whether the
   symbols is undefined (this last is needed to distinguish a .extern
   symbols from a .comm symbol).  */

struct ecoff_sy_obj
{
  struct efdr *ecoff_file;
  struct localsym *ecoff_symbol;
  valueT ecoff_extern_size;
};

#define OBJ_SYMFIELD_TYPE struct ecoff_sy_obj

/* Modify the ECOFF symbol.  */
#define obj_frob_symbol(symp, punt) ecoff_frob_symbol (symp)

/* Set section VMAs and GP.  */
#define obj_frob_file_before_fix() ecoff_frob_file_before_fix ()

/* This is used to write the symbolic data in the format that BFD
   expects it.  */
#define obj_frob_file() ecoff_frob_file ()

/* We use the ECOFF functions as our hooks.  */
#define obj_read_begin_hook ecoff_read_begin_hook
#define obj_symbol_new_hook ecoff_symbol_new_hook

/* Record file switches in the ECOFF symbol table.  */
#define obj_app_file(name, app) ecoff_new_file (name, app)

/* At the moment we don't want to do any stabs processing in read.c.  */
#define OBJ_PROCESS_STAB(seg, what, string, type, other, desc) \
  ecoff_stab ((seg), (what), (string), (type), (other), (desc))

#define EMIT_SECTION_SYMBOLS		0
#define obj_sec_sym_ok_for_reloc(SEC)	1

#define obj_ecoff_set_ext ecoff_set_ext

extern void ecoff_frob_file_before_fix (void);
extern void ecoff_frob_file (void);
extern void obj_ecoff_set_ext (symbolS *, EXTR *);

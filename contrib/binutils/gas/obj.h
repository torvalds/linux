/* obj.h - defines the object dependent hooks for all object
   format backends.

   Copyright 1987, 1990, 1991, 1992, 1993, 1995, 1996, 1997, 1999, 2000,
   2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

char *obj_default_output_file_name (void);
void obj_emit_relocations (char **where, fixS * fixP,
			   relax_addressT segment_address_in_file);
void obj_emit_strings (char **where);
void obj_emit_symbols (char **where, symbolS * symbols);
#ifndef obj_read_begin_hook
void obj_read_begin_hook (void);
#endif

#ifndef obj_symbol_new_hook
void obj_symbol_new_hook (symbolS * symbolP);
#endif

void obj_symbol_to_chars (char **where, symbolS * symbolP);

extern const pseudo_typeS obj_pseudo_table[];

struct format_ops {
  int flavor;
  unsigned dfl_leading_underscore : 1;
  unsigned emit_section_symbols : 1;
  void (*begin) (void);
  void (*app_file) (const char *, int);
  void (*frob_symbol) (symbolS *, int *);
  void (*frob_file) (void);
  void (*frob_file_before_adjust) (void);
  void (*frob_file_before_fix) (void);
  void (*frob_file_after_relocs) (void);
  bfd_vma (*s_get_size) (symbolS *);
  void (*s_set_size) (symbolS *, bfd_vma);
  bfd_vma (*s_get_align) (symbolS *);
  void (*s_set_align) (symbolS *, bfd_vma);
  int (*s_get_other) (symbolS *);
  void (*s_set_other) (symbolS *, int);
  int (*s_get_desc) (symbolS *);
  void (*s_set_desc) (symbolS *, int);
  int (*s_get_type) (symbolS *);
  void (*s_set_type) (symbolS *, int);
  void (*copy_symbol_attributes) (symbolS *, symbolS *);
  void (*generate_asm_lineno) (void);
  void (*process_stab) (segT, int, const char *, int, int, int);
  int (*separate_stab_sections) (void);
  void (*init_stab_section) (segT);
  int (*sec_sym_ok_for_reloc) (asection *);
  void (*pop_insert) (void);
  /* For configurations using ECOFF_DEBUGGING, this callback is used.  */
  void (*ecoff_set_ext) (symbolS *, struct ecoff_extr *);

  void (*read_begin_hook) (void);
  void (*symbol_new_hook) (symbolS *);
};

extern const struct format_ops elf_format_ops;
extern const struct format_ops ecoff_format_ops;
extern const struct format_ops coff_format_ops;
extern const struct format_ops aout_format_ops;

#ifndef this_format
COMMON const struct format_ops *this_format;
#endif

/* end of obj.h */

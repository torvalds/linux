/* IEEE-695 object file formats:  definitions internal to BFD.
   Copyright 1990, 1991, 1992, 1994, 1996, 2001, 2002
   Free Software Foundation, Inc.
   Written by Cygnus Support.  Mostly Steve Chamberlain's fault.

This file is part of BFD, the Binary File Descriptor library.

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
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

typedef struct {
  unsigned int index:24;
  char letter;
} ieee_symbol_index_type;

typedef struct ct {
  bfd *this;
  struct ct *next;
} bfd_chain_type;

typedef struct ieee_symbol
{
  asymbol symbol;
  struct ieee_symbol *next;

  unsigned int index;
} ieee_symbol_type;


typedef struct ieee_reloc {
  arelent relent;
  struct ieee_reloc *next;
  ieee_symbol_index_type symbol;

} ieee_reloc_type;

#define ieee_symbol(x) ((ieee_symbol_type *)(x))

typedef struct ieee_per_section
{
  asection *section;
  bfd_byte *data;
  bfd_vma offset;
  bfd_vma pc;
  /* For output */
  file_ptr current_pos;
  unsigned int current_byte;
  bfd_boolean initialized;
  ieee_reloc_type **reloc_tail_ptr;
} ieee_per_section_type;

#define ieee_per_section(x) ((ieee_per_section_type *)((x)->used_by_bfd))

typedef struct {
  unsigned char *input_p;
  unsigned char *first_byte;
  unsigned char *last_byte;
  bfd *abfd;
} common_header_type ;

typedef struct ieee_data_struct
{
  common_header_type h;
  bfd_boolean read_symbols;
  bfd_boolean read_data;
  file_ptr output_cursor;
  /* Map of section indexes to section ptrs */
  asection **section_table;
  unsigned int section_table_size;
  ieee_address_descriptor_type ad;
  ieee_module_begin_type mb;
  ieee_w_variable_type w;

  unsigned int section_count;

  unsigned int map_idx;
  /* List of GLOBAL EXPORT symbols */
  ieee_symbol_type *external_symbols;
  /* List of UNDEFINED symbols */
  ieee_symbol_type *external_reference;

  /* When the symbols have been canonicalized, they are in a
    * special order, we remember various bases here.. */
  unsigned int external_symbol_max_index;
  unsigned int external_symbol_min_index;
  unsigned int external_symbol_count;
  int external_symbol_base_offset;

  unsigned int external_reference_max_index;
  unsigned int external_reference_min_index;
  unsigned int external_reference_count;
  int external_reference_base_offset;


  bfd_boolean symbol_table_full;


bfd_boolean done_debug;


bfd_chain_type *chain_head;
bfd_chain_type *chain_root;

} ieee_data_type;

typedef struct {
  file_ptr file_offset;
  bfd *abfd;
} ieee_ar_obstack_type;

typedef struct ieee_ar_data_struct
{
  common_header_type h;
  ieee_ar_obstack_type *elements;

  unsigned  int element_index ;
  unsigned int element_count;

} ieee_ar_data_type;

#define IEEE_DATA(abfd) ((abfd)->tdata.ieee_data)
#define IEEE_AR_DATA(abfd) ((abfd)->tdata.ieee_ar_data)

#define ptr(abfd) (ieee_data(abfd)->input_p)

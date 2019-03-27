/* BFD ECOFF object file private structure.
   Copyright 1993, 1994, 1995, 1996, 1999, 2001, 2002, 2003, 2004,
   2005, 2006 Free Software Foundation, Inc.
   Written by Ian Lance Taylor, Cygnus Support.

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

#include "bfdlink.h"

#ifndef ECOFF_H
#include "coff/ecoff.h"
#endif

/* This is the backend information kept for ECOFF files.  This
   structure is constant for a particular backend.  The first element
   is the COFF backend data structure, so that ECOFF targets can use
   the generic COFF code.  */

#define ecoff_backend(abfd) \
  ((struct ecoff_backend_data *) (abfd)->xvec->backend_data)

struct ecoff_backend_data
{
  /* COFF backend information.  This must be the first field.  */
  bfd_coff_backend_data coff;
  /* Supported architecture.  */
  enum bfd_architecture arch;
  /* Initial portion of armap string.  */
  const char *armap_start;
  /* The page boundary used to align sections in a demand-paged
     executable file.  E.g., 0x1000.  */
  bfd_vma round;
  /* TRUE if the .rdata section is part of the text segment, as on the
     Alpha.  FALSE if .rdata is part of the data segment, as on the
     MIPS.  */
  bfd_boolean rdata_in_text;
  /* Bitsize of constructor entries.  */
  unsigned int constructor_bitsize;
  /* Reloc to use for constructor entries.  */
  reloc_howto_type *constructor_reloc;
  /* How to swap debugging information.  */
  struct ecoff_debug_swap debug_swap;
  /* External reloc size.  */
  bfd_size_type external_reloc_size;
  /* Reloc swapping functions.  */
  void (*swap_reloc_in) (bfd *, void *, struct internal_reloc *);
  void (*swap_reloc_out) (bfd *, const struct internal_reloc *, void *);
  /* Backend reloc tweaking.  */
  void (*adjust_reloc_in)
    (bfd *, const struct internal_reloc *, arelent *);
  void (*adjust_reloc_out)
    (bfd *, const arelent *, struct internal_reloc *);
  /* Relocate section contents while linking.  */
  bfd_boolean (*relocate_section)
    (bfd *, struct bfd_link_info *, bfd *, asection *, bfd_byte *, void *);
  /* Do final adjustments to filehdr and aouthdr.  */
  bfd_boolean (*adjust_headers)
    (bfd *, struct internal_filehdr *, struct internal_aouthdr *);
  /* Read an element from an archive at a given file position.  This
     is needed because OSF/1 3.2 uses a weird archive format.  */
  bfd *(*get_elt_at_filepos) (bfd *, file_ptr);
};

/* This is the target specific information kept for ECOFF files.  */

#define ecoff_data(abfd) ((abfd)->tdata.ecoff_obj_data)

typedef struct ecoff_tdata
{
  /* The reloc file position, set by
     ecoff_compute_section_file_positions.  */
  file_ptr reloc_filepos;

  /* The symbol table file position, set by _bfd_ecoff_mkobject_hook.  */
  file_ptr sym_filepos;

  /* The start and end of the text segment.  Only valid for an
     existing file, not for one we are creating.  */
  unsigned long text_start;
  unsigned long text_end;

  /* The cached gp value.  This is used when relocating.  */
  bfd_vma gp;

  /* The maximum size of objects to optimize using gp.  This is
     typically set by the -G option to the compiler, assembler or
     linker.  */
  unsigned int gp_size;

  /* The register masks.  When linking, all the masks found in the
     input files are combined into the masks of the output file.
     These are not all used for all targets, but that's OK, because
     the relevant ones are the only ones swapped in and out.  */
  unsigned long gprmask;
  unsigned long fprmask;
  unsigned long cprmask[4];

  /* The ECOFF symbolic debugging information.  */
  struct ecoff_debug_info debug_info;

  /* The unswapped ECOFF symbolic information.  */
  void * raw_syments;

  /* The canonical BFD symbols.  */
  struct ecoff_symbol_struct *canonical_symbols;

  /* A mapping from external symbol numbers to entries in the linker
     hash table, used when linking.  */
  struct ecoff_link_hash_entry **sym_hashes;

  /* A mapping from reloc symbol indices to sections, used when
     linking.  */
  asection **symndx_to_section;

  /* TRUE if this BFD was written by the backend linker.  */
  bfd_boolean linker;

  /* TRUE if a warning that multiple global pointer values are
     needed in the output binary was issued already.  */
  bfd_boolean issued_multiple_gp_warning;

  /* Used by find_nearest_line entry point.  The structure could be
     included directly in this one, but there's no point to wasting
     the memory just for the infrequently called find_nearest_line.  */
  struct ecoff_find_line *find_line_info;

  /* Whether the .rdata section is in the text segment for this
     particular ECOFF file.  This is not valid until
     ecoff_compute_section_file_positions is called.  */
  bfd_boolean rdata_in_text;

} ecoff_data_type;

/* Each canonical asymbol really looks like this.  */

typedef struct ecoff_symbol_struct
{
  /* The actual symbol which the rest of BFD works with */
  asymbol symbol;

  /* The fdr for this symbol.  */
  FDR *fdr;

  /* TRUE if this is a local symbol rather than an external one.  */
  bfd_boolean local;

  /* A pointer to the unswapped hidden information for this symbol.
     This is either a struct sym_ext or a struct ext_ext, depending on
     the value of the local field above.  */
  void * native;
} ecoff_symbol_type;

/* We take the address of the first element of an asymbol to ensure that the
   macro is only ever applied to an asymbol.  */
#define ecoffsymbol(asymbol) ((ecoff_symbol_type *) (&((asymbol)->the_bfd)))

/* We need to save the index of an external symbol when we write it
   out so that can set the symbol index correctly when we write out
   the relocs.  */
#define ecoff_get_sym_index(symbol) ((symbol)->udata.i)
#define ecoff_set_sym_index(symbol, idx) ((symbol)->udata.i = (idx))

/* A pointer to this structure is put in the used_by_bfd pointer of
   a section to keep track of any per-section data.
   The user_by_bfd pointer will be NULL if the information was not
   needed.  */

struct ecoff_section_tdata
{
  /* When producing an executable (i.e., final, non-relocatable link)
     on the Alpha, we may need to use multiple global pointer values
     to span the entire .lita section.  In essence, we allow each
     input .lita section to have its own gp value.  To support this,
     we need to keep track of the gp values that we picked for each
     input .lita section . */
  bfd_vma gp;
};

/* An accessor macro for the ecoff_section_tdata structure.  */
#define ecoff_section_data(abfd, sec) \
  ((struct ecoff_section_tdata *) (sec)->used_by_bfd)

/* ECOFF linker hash table entries.  */

struct ecoff_link_hash_entry
{
  struct bfd_link_hash_entry root;
  /* Symbol index in output file.  */
  long indx;
  /* BFD that ext field value came from.  */
  bfd *abfd;
  /* ECOFF external symbol information.  */
  EXTR esym;
  /* Nonzero if this symbol has been written out.  */
  char written;
  /* Nonzero if this symbol was referred to as small undefined.  */
  char small;
};

/* ECOFF linker hash table.  */

struct ecoff_link_hash_table
{
  struct bfd_link_hash_table root;
};

/* Make an ECOFF object.  */
extern bfd_boolean _bfd_ecoff_mkobject (bfd *);

/* Read in the ECOFF symbolic debugging information.  */
extern bfd_boolean _bfd_ecoff_slurp_symbolic_info
  (bfd *, asection *, struct ecoff_debug_info *);

/* Generic ECOFF BFD backend vectors.  */

extern bfd_boolean _bfd_ecoff_write_object_contents (bfd *);
extern const bfd_target *_bfd_ecoff_archive_p (bfd *);

#define	_bfd_ecoff_close_and_cleanup _bfd_generic_close_and_cleanup
#define _bfd_ecoff_bfd_free_cached_info _bfd_generic_bfd_free_cached_info
extern bfd_boolean _bfd_ecoff_new_section_hook
  (bfd *, asection *);
extern bfd_boolean _bfd_ecoff_get_section_contents
  (bfd *, asection *, void * location, file_ptr, bfd_size_type);

#define _bfd_ecoff_bfd_link_split_section _bfd_generic_link_split_section

extern bfd_boolean _bfd_ecoff_bfd_copy_private_bfd_data
  (bfd *, bfd *);
#define _bfd_ecoff_bfd_copy_private_section_data \
  _bfd_generic_bfd_copy_private_section_data

#define _bfd_ecoff_bfd_copy_private_symbol_data \
  _bfd_generic_bfd_copy_private_symbol_data

#define _bfd_ecoff_bfd_copy_private_header_data \
  _bfd_generic_bfd_copy_private_header_data

#define _bfd_ecoff_bfd_print_private_bfd_data \
  _bfd_generic_bfd_print_private_bfd_data

#define _bfd_ecoff_bfd_merge_private_bfd_data \
  _bfd_generic_bfd_merge_private_bfd_data

#define _bfd_ecoff_bfd_set_private_flags _bfd_generic_bfd_set_private_flags
extern bfd_boolean _bfd_ecoff_slurp_armap (bfd *);
#define _bfd_ecoff_slurp_extended_name_table _bfd_slurp_extended_name_table
#define _bfd_ecoff_construct_extended_name_table \
  _bfd_archive_bsd_construct_extended_name_table
#define _bfd_ecoff_truncate_arname bfd_dont_truncate_arname
extern bfd_boolean _bfd_ecoff_write_armap
  (bfd *, unsigned int, struct orl *, unsigned int, int);
#define _bfd_ecoff_read_ar_hdr _bfd_generic_read_ar_hdr
#define _bfd_ecoff_openr_next_archived_file \
  bfd_generic_openr_next_archived_file
#define _bfd_ecoff_get_elt_at_index _bfd_generic_get_elt_at_index
#define _bfd_ecoff_generic_stat_arch_elt bfd_generic_stat_arch_elt
#define _bfd_ecoff_update_armap_timestamp bfd_true
#define _bfd_ecoff_bfd_is_target_special_symbol  \
  ((bfd_boolean (*) (bfd *, asymbol *)) bfd_false)

extern long _bfd_ecoff_get_symtab_upper_bound (bfd *);
extern long _bfd_ecoff_canonicalize_symtab (bfd *, asymbol **);
extern asymbol *_bfd_ecoff_make_empty_symbol (bfd *);
extern void _bfd_ecoff_print_symbol
  (bfd *, void *, asymbol *, bfd_print_symbol_type);
extern void _bfd_ecoff_get_symbol_info
  (bfd *, asymbol *, symbol_info *);
extern bfd_boolean _bfd_ecoff_bfd_is_local_label_name
  (bfd *, const char *);
#define _bfd_ecoff_get_lineno _bfd_nosymbols_get_lineno
extern bfd_boolean _bfd_ecoff_find_nearest_line
  (bfd *, asection *, asymbol **, bfd_vma, const char **, const char **,
   unsigned int *);
#define _bfd_ecoff_bfd_make_debug_symbol _bfd_nosymbols_bfd_make_debug_symbol
#define _bfd_ecoff_read_minisymbols _bfd_generic_read_minisymbols
#define _bfd_ecoff_minisymbol_to_symbol _bfd_generic_minisymbol_to_symbol
#define _bfd_ecoff_find_inliner_info _bfd_nosymbols_find_inliner_info

#define _bfd_ecoff_get_reloc_upper_bound coff_get_reloc_upper_bound
extern long _bfd_ecoff_canonicalize_reloc
  (bfd *, asection *, arelent **, asymbol **symbols);
/* ecoff_bfd_reloc_type_lookup defined by backend. */

extern bfd_boolean _bfd_ecoff_set_arch_mach
  (bfd *, enum bfd_architecture, unsigned long);
extern bfd_boolean _bfd_ecoff_set_section_contents
  (bfd *, asection *, const void * location, file_ptr, bfd_size_type);

extern int _bfd_ecoff_sizeof_headers (bfd *, struct bfd_link_info *);
/* ecoff_bfd_get_relocated_section_contents defined by backend.  */
/* ecoff_bfd_relax_section defined by backend.  */
extern struct bfd_link_hash_table *_bfd_ecoff_bfd_link_hash_table_create
  (bfd *);
#define _bfd_ecoff_bfd_link_hash_table_free _bfd_generic_link_hash_table_free
extern bfd_boolean _bfd_ecoff_bfd_link_add_symbols
  (bfd *, struct bfd_link_info *);
#define _bfd_ecoff_bfd_link_just_syms _bfd_generic_link_just_syms
extern bfd_boolean _bfd_ecoff_bfd_final_link
  (bfd *, struct bfd_link_info *);

/* Hook functions for the generic COFF section reading code.  */

extern void * _bfd_ecoff_mkobject_hook (bfd *, void *, void *);
#define _bfd_ecoff_set_alignment_hook \
  ((void (*) (bfd *, asection *, void *)) bfd_void)
extern bfd_boolean _bfd_ecoff_set_arch_mach_hook
  (bfd *, void *);
extern bfd_boolean _bfd_ecoff_styp_to_sec_flags
  (bfd *, void *, const char *, asection *, flagword *);
extern bfd_boolean _bfd_ecoff_slurp_symbol_table (bfd *);

/* ECOFF auxiliary information swapping routines.  These are the same
   for all ECOFF targets, so they are defined in ecofflink.c.  */

extern void _bfd_ecoff_swap_tir_in
  (int, const struct tir_ext *, TIR *);
extern void _bfd_ecoff_swap_tir_out
  (int, const TIR *, struct tir_ext *);
extern void _bfd_ecoff_swap_rndx_in
  (int, const struct rndx_ext *, RNDXR *);
extern void _bfd_ecoff_swap_rndx_out
  (int, const RNDXR *, struct rndx_ext *);

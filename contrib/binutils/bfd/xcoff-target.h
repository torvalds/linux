/* Common definitions for backends based on IBM RS/6000 "XCOFF64" files.
   Copyright 2000, 2001, 2002, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Cygnus Support.

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

/* Internalcoff.h and coffcode.h modify themselves based on this flag.  */
#define RS6000COFF_C 1

#define SELECT_RELOC(internal, howto)					\
  {									\
    internal.r_type = howto->type;					\
    internal.r_size =							\
      ((howto->complain_on_overflow == complain_overflow_signed		\
	? 0x80								\
	: 0)								\
       | (howto->bitsize - 1));						\
  }

#define COFF_DEFAULT_SECTION_ALIGNMENT_POWER 3

#define COFF_LONG_FILENAMES

#define NO_COFF_SYMBOLS

#define RTYPE2HOWTO(cache_ptr, dst) _bfd_xcoff_rtype2howto (cache_ptr, dst)

#define coff_mkobject                                  _bfd_xcoff_mkobject
#define coff_bfd_copy_private_bfd_data                 _bfd_xcoff_copy_private_bfd_data
#define coff_bfd_is_local_label_name                   _bfd_xcoff_is_local_label_name
#define coff_bfd_is_target_special_symbol  ((bfd_boolean (*) (bfd *, asymbol *)) bfd_false)
#define coff_bfd_reloc_type_lookup                     _bfd_xcoff_reloc_type_lookup
#define coff_relocate_section                          _bfd_ppc_xcoff_relocate_section
#define coff_core_file_failing_command                 _bfd_nocore_core_file_failing_command
#define coff_core_file_failing_signal                  _bfd_nocore_core_file_failing_signal
#define coff_core_file_matches_executable_p            _bfd_nocore_core_file_matches_executable_p
#define _bfd_xcoff_bfd_get_relocated_section_contents  coff_bfd_get_relocated_section_contents
#define _bfd_xcoff_bfd_relax_section                   coff_bfd_relax_section
#define _bfd_xcoff_bfd_gc_sections                     coff_bfd_gc_sections
#define _bfd_xcoff_bfd_merge_sections                  coff_bfd_merge_sections
#define _bfd_xcoff_bfd_discard_group                   bfd_generic_discard_group
#define _bfd_xcoff_section_already_linked              _bfd_generic_section_already_linked
#define _bfd_xcoff_bfd_link_split_section              coff_bfd_link_split_section

#define CORE_FILE_P _bfd_dummy_target

#ifdef AIX_CORE

#undef  CORE_FILE_P
#define CORE_FILE_P rs6000coff_core_p
extern const bfd_target * rs6000coff_core_p (bfd *);
extern bfd_boolean rs6000coff_core_file_matches_executable_p (bfd *, bfd*);

#undef	coff_core_file_matches_executable_p
#define coff_core_file_matches_executable_p  \
				     rs6000coff_core_file_matches_executable_p

extern char *rs6000coff_core_file_failing_command (bfd *);
#undef  coff_core_file_failing_command
#define coff_core_file_failing_command rs6000coff_core_file_failing_command

extern int rs6000coff_core_file_failing_signal (bfd *);
#undef  coff_core_file_failing_signal
#define coff_core_file_failing_signal rs6000coff_core_file_failing_signal
#endif /* AIX_CORE */

#ifdef LYNX_CORE

#undef  CORE_FILE_P
#define CORE_FILE_P lynx_core_file_p
extern const bfd_target *lynx_core_file_p (bfd *);

extern bfd_boolean lynx_core_file_matches_executable_p (bfd *, bfd *);
#undef	coff_core_file_matches_executable_p
#define coff_core_file_matches_executable_p lynx_core_file_matches_executable_p

extern char *lynx_core_file_failing_command (bfd *);
#undef  coff_core_file_failing_command
#define coff_core_file_failing_command lynx_core_file_failing_command

extern int lynx_core_file_failing_signal (bfd *);
#undef  coff_core_file_failing_signal
#define coff_core_file_failing_signal lynx_core_file_failing_signal

#endif /* LYNX_CORE */

/* XCOFF archives do not have anything which corresponds to an
   extended name table.  */

#define _bfd_xcoff_slurp_extended_name_table bfd_false
#define _bfd_xcoff_construct_extended_name_table \
  ((bfd_boolean (*) (bfd *, char **, bfd_size_type *, const char **)) bfd_false)
#define _bfd_xcoff_truncate_arname bfd_dont_truncate_arname

/* We can use the standard get_elt_at_index routine.  */

#define _bfd_xcoff_get_elt_at_index _bfd_generic_get_elt_at_index

/* XCOFF archives do not have a timestamp.  */

#define _bfd_xcoff_update_armap_timestamp bfd_true

extern bfd_boolean        _bfd_xcoff_mkobject (bfd *);
extern bfd_boolean        _bfd_xcoff_copy_private_bfd_data (bfd *, bfd *);
extern bfd_boolean        _bfd_xcoff_is_local_label_name (bfd *, const char *);
extern void               _bfd_xcoff_rtype2howto  (arelent *, struct internal_reloc *);
extern bfd_boolean        _bfd_xcoff_slurp_armap (bfd *);
extern void *             _bfd_xcoff_read_ar_hdr (bfd *);
extern bfd *              _bfd_xcoff_openr_next_archived_file (bfd *, bfd *);
extern int                _bfd_xcoff_generic_stat_arch_elt (bfd *, struct stat *);
extern bfd_boolean        _bfd_xcoff_write_armap  (bfd *, unsigned int, struct orl *, unsigned int, int);
extern bfd_boolean        _bfd_xcoff_write_archive_contents (bfd *);
extern int                _bfd_xcoff_sizeof_headers (bfd *, struct bfd_link_info *);
extern void               _bfd_xcoff_swap_sym_in (bfd *, void *, void *);
extern unsigned int       _bfd_xcoff_swap_sym_out (bfd *, void *, void *);
extern void               _bfd_xcoff_swap_aux_in (bfd *, void *, int, int, int, int, void *);
extern unsigned int       _bfd_xcoff_swap_aux_out (bfd *, void *, int, int, int, int, void *);
extern reloc_howto_type * _bfd_xcoff_reloc_type_lookup  (bfd *, bfd_reloc_code_real_type);
extern const bfd_target * _bfd_xcoff_archive_p (bfd *);

#ifndef coff_SWAP_sym_in
#define coff_SWAP_sym_in  _bfd_xcoff_swap_sym_in
#define coff_SWAP_sym_out _bfd_xcoff_swap_sym_out
#define coff_SWAP_aux_in  _bfd_xcoff_swap_aux_in
#define coff_SWAP_aux_out _bfd_xcoff_swap_aux_out
#endif

#include "coffcode.h"

/* The transfer vector that leads the outside world to all of the above.  */

const bfd_target TARGET_SYM =
{
  TARGET_NAME,
  bfd_target_xcoff_flavour,
  BFD_ENDIAN_BIG,		/* Data byte order is big.  */
  BFD_ENDIAN_BIG,		/* Header byte order is big.  */

  (HAS_RELOC | EXEC_P |		/* Object flags.  */
   HAS_LINENO | HAS_DEBUG | DYNAMIC |
   HAS_SYMS | HAS_LOCALS | WP_TEXT),

  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC), /* Section flags.  */
  0,				/* Leading char.  */
  '/',				/* AR_pad_char.  */
  15,				/* AR_max_namelen??? FIXME.  */

  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
     bfd_getb32, bfd_getb_signed_32, bfd_putb32,
     bfd_getb16, bfd_getb_signed_16, bfd_putb16,/* Data.  */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
     bfd_getb32, bfd_getb_signed_32, bfd_putb32,
     bfd_getb16, bfd_getb_signed_16, bfd_putb16,/* Headers.  */

  {_bfd_dummy_target, coff_object_p, 		/* bfd_check_format.  */
     _bfd_xcoff_archive_p, CORE_FILE_P},
  {bfd_false, coff_mkobject,			/* bfd_set_format.  */
     _bfd_generic_mkarchive, bfd_false},
  {bfd_false, coff_write_object_contents,	/* bfd_write_contents.  */
     _bfd_xcoff_write_archive_contents, bfd_false},

     BFD_JUMP_TABLE_GENERIC (coff),
     BFD_JUMP_TABLE_COPY (coff),
     BFD_JUMP_TABLE_CORE (coff),
     BFD_JUMP_TABLE_ARCHIVE (_bfd_xcoff),
     BFD_JUMP_TABLE_SYMBOLS (coff),
     BFD_JUMP_TABLE_RELOCS (coff),
     BFD_JUMP_TABLE_WRITE (coff),
     BFD_JUMP_TABLE_LINK (_bfd_xcoff),
     BFD_JUMP_TABLE_DYNAMIC (_bfd_xcoff),

  NULL,

  COFF_SWAP_TABLE
};

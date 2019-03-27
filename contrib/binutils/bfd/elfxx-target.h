/* Target definitions for NN-bit ELF
   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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

/* This structure contains everything that BFD knows about a target.
   It includes things like its byte order, name, what routines to call
   to do various operations, etc.  Every BFD points to a target structure
   with its "xvec" member.

   There are two such structures here:  one for big-endian machines and
   one for little-endian machines.   */

#ifndef bfd_elfNN_close_and_cleanup
#define	bfd_elfNN_close_and_cleanup _bfd_elf_close_and_cleanup
#endif
#ifndef bfd_elfNN_bfd_free_cached_info
#define bfd_elfNN_bfd_free_cached_info _bfd_free_cached_info
#endif
#ifndef bfd_elfNN_get_section_contents
#define bfd_elfNN_get_section_contents _bfd_generic_get_section_contents
#endif

#define bfd_elfNN_canonicalize_dynamic_symtab \
  _bfd_elf_canonicalize_dynamic_symtab
#ifndef bfd_elfNN_get_synthetic_symtab
#define bfd_elfNN_get_synthetic_symtab \
  _bfd_elf_get_synthetic_symtab
#endif
#ifndef bfd_elfNN_canonicalize_reloc
#define bfd_elfNN_canonicalize_reloc	_bfd_elf_canonicalize_reloc
#endif
#ifndef bfd_elfNN_find_nearest_line
#define bfd_elfNN_find_nearest_line	_bfd_elf_find_nearest_line
#endif
#ifndef bfd_elfNN_find_inliner_info
#define bfd_elfNN_find_inliner_info	_bfd_elf_find_inliner_info
#endif
#define bfd_elfNN_read_minisymbols	_bfd_elf_read_minisymbols
#define bfd_elfNN_minisymbol_to_symbol	_bfd_elf_minisymbol_to_symbol
#define bfd_elfNN_get_dynamic_symtab_upper_bound \
  _bfd_elf_get_dynamic_symtab_upper_bound
#define bfd_elfNN_get_lineno		_bfd_elf_get_lineno
#ifndef bfd_elfNN_get_reloc_upper_bound
#define bfd_elfNN_get_reloc_upper_bound _bfd_elf_get_reloc_upper_bound
#endif
#ifndef bfd_elfNN_get_symbol_info
#define bfd_elfNN_get_symbol_info	_bfd_elf_get_symbol_info
#endif
#define bfd_elfNN_canonicalize_symtab	_bfd_elf_canonicalize_symtab
#define bfd_elfNN_get_symtab_upper_bound _bfd_elf_get_symtab_upper_bound
#define bfd_elfNN_make_empty_symbol	_bfd_elf_make_empty_symbol
#ifndef bfd_elfNN_new_section_hook
#define bfd_elfNN_new_section_hook	_bfd_elf_new_section_hook
#endif
#define bfd_elfNN_set_arch_mach		_bfd_elf_set_arch_mach
#ifndef bfd_elfNN_set_section_contents
#define bfd_elfNN_set_section_contents	_bfd_elf_set_section_contents
#endif
#define bfd_elfNN_sizeof_headers	_bfd_elf_sizeof_headers
#define bfd_elfNN_write_object_contents _bfd_elf_write_object_contents
#define bfd_elfNN_write_corefile_contents _bfd_elf_write_corefile_contents

#define bfd_elfNN_get_section_contents_in_window \
  _bfd_generic_get_section_contents_in_window

#ifndef elf_backend_can_refcount
#define elf_backend_can_refcount 0
#endif
#ifndef elf_backend_want_got_plt
#define elf_backend_want_got_plt 0
#endif
#ifndef elf_backend_plt_readonly
#define elf_backend_plt_readonly 0
#endif
#ifndef elf_backend_want_plt_sym
#define elf_backend_want_plt_sym 0
#endif
#ifndef elf_backend_plt_not_loaded
#define elf_backend_plt_not_loaded 0
#endif
#ifndef elf_backend_plt_alignment
#define elf_backend_plt_alignment 2
#endif
#ifndef elf_backend_want_dynbss
#define elf_backend_want_dynbss 1
#endif
#ifndef elf_backend_want_p_paddr_set_to_zero
#define elf_backend_want_p_paddr_set_to_zero 0
#endif
#ifndef elf_backend_default_execstack
#define elf_backend_default_execstack 1
#endif

#define bfd_elfNN_bfd_debug_info_start	bfd_void
#define bfd_elfNN_bfd_debug_info_end	bfd_void
#define bfd_elfNN_bfd_debug_info_accumulate \
  ((void (*) (bfd*, struct bfd_section *)) bfd_void)

#ifndef bfd_elfNN_bfd_get_relocated_section_contents
#define bfd_elfNN_bfd_get_relocated_section_contents \
  bfd_generic_get_relocated_section_contents
#endif

#ifndef bfd_elfNN_bfd_relax_section
#define bfd_elfNN_bfd_relax_section bfd_generic_relax_section
#endif

#ifndef elf_backend_can_gc_sections
#define elf_backend_can_gc_sections 0
#endif
#ifndef elf_backend_can_refcount
#define elf_backend_can_refcount 0
#endif
#ifndef elf_backend_want_got_sym
#define elf_backend_want_got_sym 1
#endif
#ifndef elf_backend_gc_mark_dynamic_ref
#define elf_backend_gc_mark_dynamic_ref	bfd_elf_gc_mark_dynamic_ref_symbol
#endif
#ifndef elf_backend_gc_mark_hook
#define elf_backend_gc_mark_hook	_bfd_elf_gc_mark_hook
#endif
#ifndef elf_backend_gc_mark_extra_sections
#define elf_backend_gc_mark_extra_sections	NULL
#endif
#ifndef elf_backend_gc_sweep_hook
#define elf_backend_gc_sweep_hook	NULL
#endif
#ifndef bfd_elfNN_bfd_gc_sections
#define bfd_elfNN_bfd_gc_sections bfd_elf_gc_sections
#endif

#ifndef bfd_elfNN_bfd_merge_sections
#define bfd_elfNN_bfd_merge_sections \
  _bfd_elf_merge_sections
#endif

#ifndef bfd_elfNN_bfd_is_group_section
#define bfd_elfNN_bfd_is_group_section bfd_elf_is_group_section
#endif

#ifndef bfd_elfNN_bfd_discard_group
#define bfd_elfNN_bfd_discard_group bfd_generic_discard_group
#endif

#ifndef bfd_elfNN_section_already_linked
#define bfd_elfNN_section_already_linked \
  _bfd_elf_section_already_linked
#endif

#ifndef bfd_elfNN_bfd_make_debug_symbol
#define bfd_elfNN_bfd_make_debug_symbol \
  ((asymbol * (*) (bfd *, void *, unsigned long)) bfd_nullvoidptr)
#endif

#ifndef bfd_elfNN_bfd_copy_private_symbol_data
#define bfd_elfNN_bfd_copy_private_symbol_data \
  _bfd_elf_copy_private_symbol_data
#endif

#ifndef bfd_elfNN_bfd_copy_private_section_data
#define bfd_elfNN_bfd_copy_private_section_data \
  _bfd_elf_copy_private_section_data
#endif
#ifndef bfd_elfNN_bfd_copy_private_header_data
#define bfd_elfNN_bfd_copy_private_header_data \
  _bfd_elf_copy_private_header_data
#endif
#ifndef bfd_elfNN_bfd_copy_private_bfd_data
#define bfd_elfNN_bfd_copy_private_bfd_data \
  _bfd_elf_copy_private_bfd_data
#endif
#ifndef bfd_elfNN_bfd_print_private_bfd_data
#define bfd_elfNN_bfd_print_private_bfd_data \
  _bfd_elf_print_private_bfd_data
#endif
#ifndef bfd_elfNN_bfd_merge_private_bfd_data
#define bfd_elfNN_bfd_merge_private_bfd_data \
  ((bfd_boolean (*) (bfd *, bfd *)) bfd_true)
#endif
#ifndef bfd_elfNN_bfd_set_private_flags
#define bfd_elfNN_bfd_set_private_flags \
  ((bfd_boolean (*) (bfd *, flagword)) bfd_true)
#endif
#ifndef bfd_elfNN_bfd_is_local_label_name
#define bfd_elfNN_bfd_is_local_label_name _bfd_elf_is_local_label_name
#endif
#ifndef bfd_elfNN_bfd_is_target_special_symbol
#define bfd_elfNN_bfd_is_target_special_symbol \
  ((bfd_boolean (*) (bfd *, asymbol *)) bfd_false)
#endif

#ifndef bfd_elfNN_get_dynamic_reloc_upper_bound
#define bfd_elfNN_get_dynamic_reloc_upper_bound \
  _bfd_elf_get_dynamic_reloc_upper_bound
#endif
#ifndef bfd_elfNN_canonicalize_dynamic_reloc
#define bfd_elfNN_canonicalize_dynamic_reloc \
  _bfd_elf_canonicalize_dynamic_reloc
#endif

#ifndef bfd_elfNN_bfd_link_hash_table_free
#define bfd_elfNN_bfd_link_hash_table_free _bfd_generic_link_hash_table_free
#endif

#ifdef elf_backend_relocate_section
#ifndef bfd_elfNN_bfd_link_hash_table_create
#define bfd_elfNN_bfd_link_hash_table_create _bfd_elf_link_hash_table_create
#endif
#ifndef bfd_elfNN_bfd_link_add_symbols
#define bfd_elfNN_bfd_link_add_symbols	bfd_elf_link_add_symbols
#endif
#ifndef bfd_elfNN_bfd_final_link
#define bfd_elfNN_bfd_final_link	bfd_elf_final_link
#endif
#else /* ! defined (elf_backend_relocate_section) */
/* If no backend relocate_section routine, use the generic linker.
   Note - this will prevent the port from being able to use some of
   the other features of the ELF linker, because the generic hash structure
   does not have the fields needed by the ELF linker.  In particular it
   means that linking directly to S-records will not work.  */
#ifndef bfd_elfNN_bfd_link_hash_table_create
#define bfd_elfNN_bfd_link_hash_table_create \
  _bfd_generic_link_hash_table_create
#endif
#ifndef bfd_elfNN_bfd_link_add_symbols
#define bfd_elfNN_bfd_link_add_symbols	_bfd_generic_link_add_symbols
#endif
#ifndef bfd_elfNN_bfd_final_link
#define bfd_elfNN_bfd_final_link	_bfd_generic_final_link
#endif
#endif /* ! defined (elf_backend_relocate_section) */

#ifndef bfd_elfNN_bfd_link_just_syms
#define bfd_elfNN_bfd_link_just_syms	_bfd_elf_link_just_syms
#endif

#ifndef bfd_elfNN_bfd_link_split_section
#define bfd_elfNN_bfd_link_split_section _bfd_generic_link_split_section
#endif

#ifndef bfd_elfNN_archive_p
#define bfd_elfNN_archive_p bfd_generic_archive_p
#endif

#ifndef bfd_elfNN_write_archive_contents
#define bfd_elfNN_write_archive_contents _bfd_write_archive_contents
#endif

#ifndef bfd_elfNN_mkobject
#define bfd_elfNN_mkobject bfd_elf_mkobject
#endif

#ifndef bfd_elfNN_mkcorefile
#define bfd_elfNN_mkcorefile bfd_elf_mkcorefile
#endif

#ifndef bfd_elfNN_mkarchive
#define bfd_elfNN_mkarchive _bfd_generic_mkarchive
#endif

#ifndef bfd_elfNN_print_symbol
#define bfd_elfNN_print_symbol bfd_elf_print_symbol
#endif

#ifndef elf_symbol_leading_char
#define elf_symbol_leading_char 0
#endif

#ifndef elf_info_to_howto
#define elf_info_to_howto 0
#endif

#ifndef elf_info_to_howto_rel
#define elf_info_to_howto_rel 0
#endif

#ifndef ELF_OSABI
#define ELF_OSABI ELFOSABI_NONE
#endif

#ifndef ELF_MAXPAGESIZE
  #error ELF_MAXPAGESIZE is not defined
#define ELF_MAXPAGESIZE 1
#endif

#ifndef ELF_MINPAGESIZE
#define ELF_MINPAGESIZE ELF_MAXPAGESIZE
#endif

#ifndef ELF_COMMONPAGESIZE
#define ELF_COMMONPAGESIZE ELF_MAXPAGESIZE
#endif

#ifndef ELF_DYNAMIC_SEC_FLAGS
/* Note that we set the SEC_IN_MEMORY flag for these sections.  */
#define ELF_DYNAMIC_SEC_FLAGS			\
  (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS	\
   | SEC_IN_MEMORY | SEC_LINKER_CREATED)
#endif

#ifndef elf_backend_collect
#define elf_backend_collect FALSE
#endif
#ifndef elf_backend_type_change_ok
#define elf_backend_type_change_ok FALSE
#endif

#ifndef elf_backend_sym_is_global
#define elf_backend_sym_is_global	0
#endif
#ifndef elf_backend_object_p
#define elf_backend_object_p		0
#endif
#ifndef elf_backend_symbol_processing
#define elf_backend_symbol_processing	0
#endif
#ifndef elf_backend_symbol_table_processing
#define elf_backend_symbol_table_processing	0
#endif
#ifndef elf_backend_get_symbol_type
#define elf_backend_get_symbol_type 0
#endif
#ifndef elf_backend_archive_symbol_lookup
#define elf_backend_archive_symbol_lookup _bfd_elf_archive_symbol_lookup
#endif
#ifndef elf_backend_name_local_section_symbols
#define elf_backend_name_local_section_symbols	0
#endif
#ifndef elf_backend_section_processing
#define elf_backend_section_processing	0
#endif
#ifndef elf_backend_section_from_shdr
#define elf_backend_section_from_shdr	_bfd_elf_make_section_from_shdr
#endif
#ifndef elf_backend_section_flags
#define elf_backend_section_flags	0
#endif
#ifndef elf_backend_get_sec_type_attr
#define elf_backend_get_sec_type_attr	_bfd_elf_get_sec_type_attr
#endif
#ifndef elf_backend_section_from_phdr
#define elf_backend_section_from_phdr	_bfd_elf_make_section_from_phdr
#endif
#ifndef elf_backend_fake_sections
#define elf_backend_fake_sections	0
#endif
#ifndef elf_backend_section_from_bfd_section
#define elf_backend_section_from_bfd_section	0
#endif
#ifndef elf_backend_add_symbol_hook
#define elf_backend_add_symbol_hook	0
#endif
#ifndef elf_backend_link_output_symbol_hook
#define elf_backend_link_output_symbol_hook 0
#endif
#ifndef elf_backend_create_dynamic_sections
#define elf_backend_create_dynamic_sections 0
#endif
#ifndef elf_backend_omit_section_dynsym
#define elf_backend_omit_section_dynsym _bfd_elf_link_omit_section_dynsym
#endif
#ifndef elf_backend_relocs_compatible
#define elf_backend_relocs_compatible _bfd_elf_default_relocs_compatible
#endif
#ifndef elf_backend_check_relocs
#define elf_backend_check_relocs	0
#endif
#ifndef elf_backend_check_directives
#define elf_backend_check_directives	0
#endif
#ifndef elf_backend_as_needed_cleanup
#define elf_backend_as_needed_cleanup	0
#endif
#ifndef elf_backend_adjust_dynamic_symbol
#define elf_backend_adjust_dynamic_symbol 0
#endif
#ifndef elf_backend_always_size_sections
#define elf_backend_always_size_sections 0
#endif
#ifndef elf_backend_size_dynamic_sections
#define elf_backend_size_dynamic_sections 0
#endif
#ifndef elf_backend_init_index_section
#define elf_backend_init_index_section \
 ((void (*) (bfd *, struct bfd_link_info *)) bfd_void)
#endif
#ifndef elf_backend_relocate_section
#define elf_backend_relocate_section	0
#endif
#ifndef elf_backend_finish_dynamic_symbol
#define elf_backend_finish_dynamic_symbol	0
#endif
#ifndef elf_backend_finish_dynamic_sections
#define elf_backend_finish_dynamic_sections	0
#endif
#ifndef elf_backend_begin_write_processing
#define elf_backend_begin_write_processing	0
#endif
#ifndef elf_backend_final_write_processing
#define elf_backend_final_write_processing	0
#endif
#ifndef elf_backend_additional_program_headers
#define elf_backend_additional_program_headers	0
#endif
#ifndef elf_backend_modify_segment_map
#define elf_backend_modify_segment_map	0
#endif
#ifndef elf_backend_modify_program_headers
#define elf_backend_modify_program_headers	0
#endif
#ifndef elf_backend_ecoff_debug_swap
#define elf_backend_ecoff_debug_swap	0
#endif
#ifndef elf_backend_bfd_from_remote_memory
#define elf_backend_bfd_from_remote_memory _bfd_elfNN_bfd_from_remote_memory
#endif
#ifndef elf_backend_got_header_size
#define elf_backend_got_header_size	0
#endif
#ifndef elf_backend_obj_attrs_vendor
#define elf_backend_obj_attrs_vendor		NULL
#endif
#ifndef elf_backend_obj_attrs_section
#define elf_backend_obj_attrs_section		NULL
#endif
#ifndef elf_backend_obj_attrs_arg_type
#define elf_backend_obj_attrs_arg_type		NULL
#endif
#ifndef elf_backend_obj_attrs_section_type
#define elf_backend_obj_attrs_section_type		SHT_GNU_ATTRIBUTES
#endif
#ifndef elf_backend_post_process_headers
#define elf_backend_post_process_headers	NULL
#endif
#ifndef elf_backend_print_symbol_all
#define elf_backend_print_symbol_all		NULL
#endif
#ifndef elf_backend_output_arch_local_syms
#define elf_backend_output_arch_local_syms	NULL
#endif
#ifndef elf_backend_output_arch_syms
#define elf_backend_output_arch_syms		NULL
#endif
#ifndef elf_backend_copy_indirect_symbol
#define elf_backend_copy_indirect_symbol  _bfd_elf_link_hash_copy_indirect
#endif
#ifndef elf_backend_hide_symbol
#define elf_backend_hide_symbol		_bfd_elf_link_hash_hide_symbol
#endif
#ifndef elf_backend_fixup_symbol
#define elf_backend_fixup_symbol		NULL	
#endif
#ifndef elf_backend_merge_symbol_attribute
#define elf_backend_merge_symbol_attribute	NULL
#endif
#ifndef elf_backend_ignore_undef_symbol
#define elf_backend_ignore_undef_symbol		NULL
#endif
#ifndef elf_backend_emit_relocs
#define elf_backend_emit_relocs			_bfd_elf_link_output_relocs
#endif
#ifndef elf_backend_count_relocs
#define elf_backend_count_relocs		NULL
#endif
#ifndef elf_backend_grok_prstatus
#define elf_backend_grok_prstatus		NULL
#endif
#ifndef elf_backend_grok_psinfo
#define elf_backend_grok_psinfo			NULL
#endif
#ifndef elf_backend_write_core_note
#define elf_backend_write_core_note		NULL
#endif
#ifndef elf_backend_sprintf_vma
#define elf_backend_sprintf_vma			_bfd_elf_sprintf_vma
#endif
#ifndef elf_backend_fprintf_vma
#define elf_backend_fprintf_vma			_bfd_elf_fprintf_vma
#endif
#ifndef elf_backend_reloc_type_class
#define elf_backend_reloc_type_class		_bfd_elf_reloc_type_class
#endif
#ifndef elf_backend_discard_info
#define elf_backend_discard_info		NULL
#endif
#ifndef elf_backend_ignore_discarded_relocs
#define elf_backend_ignore_discarded_relocs	NULL
#endif
#ifndef elf_backend_action_discarded
#define elf_backend_action_discarded _bfd_elf_default_action_discarded
#endif
#ifndef elf_backend_eh_frame_address_size
#define elf_backend_eh_frame_address_size _bfd_elf_eh_frame_address_size
#endif
#ifndef elf_backend_can_make_relative_eh_frame
#define elf_backend_can_make_relative_eh_frame	_bfd_elf_can_make_relative
#endif
#ifndef elf_backend_can_make_lsda_relative_eh_frame
#define elf_backend_can_make_lsda_relative_eh_frame	_bfd_elf_can_make_relative
#endif
#ifndef elf_backend_encode_eh_address
#define elf_backend_encode_eh_address		_bfd_elf_encode_eh_address
#endif
#ifndef elf_backend_write_section
#define elf_backend_write_section		NULL
#endif
#ifndef elf_backend_mips_irix_compat
#define elf_backend_mips_irix_compat		NULL
#endif
#ifndef elf_backend_mips_rtype_to_howto
#define elf_backend_mips_rtype_to_howto		NULL
#endif

/* Previously, backends could only use SHT_REL or SHT_RELA relocation
   sections, but not both.  They defined USE_REL to indicate SHT_REL
   sections, and left it undefined to indicated SHT_RELA sections.
   For backwards compatibility, we still support this usage.  */
#ifndef USE_REL
#define USE_REL 0
#endif

/* Use these in new code.  */
#ifndef elf_backend_may_use_rel_p
#define elf_backend_may_use_rel_p USE_REL
#endif
#ifndef elf_backend_may_use_rela_p
#define elf_backend_may_use_rela_p !USE_REL
#endif
#ifndef elf_backend_default_use_rela_p
#define elf_backend_default_use_rela_p !USE_REL
#endif

#ifndef elf_backend_rela_normal
#define elf_backend_rela_normal 0
#endif

#ifndef elf_backend_plt_sym_val
#define elf_backend_plt_sym_val NULL
#endif
#ifndef elf_backend_relplt_name
#define elf_backend_relplt_name NULL
#endif

#ifndef ELF_MACHINE_ALT1
#define ELF_MACHINE_ALT1 0
#endif

#ifndef ELF_MACHINE_ALT2
#define ELF_MACHINE_ALT2 0
#endif

#ifndef elf_backend_size_info
#define elf_backend_size_info _bfd_elfNN_size_info
#endif

#ifndef elf_backend_special_sections
#define elf_backend_special_sections NULL
#endif

#ifndef elf_backend_sign_extend_vma
#define elf_backend_sign_extend_vma 0
#endif

#ifndef elf_backend_link_order_error_handler
#define elf_backend_link_order_error_handler _bfd_default_error_handler
#endif

#ifndef elf_backend_common_definition
#define elf_backend_common_definition _bfd_elf_common_definition
#endif

#ifndef elf_backend_common_section_index
#define elf_backend_common_section_index _bfd_elf_common_section_index
#endif

#ifndef elf_backend_common_section
#define elf_backend_common_section _bfd_elf_common_section
#endif

#ifndef elf_backend_merge_symbol
#define elf_backend_merge_symbol NULL
#endif

#ifndef elf_backend_hash_symbol
#define elf_backend_hash_symbol _bfd_elf_hash_symbol
#endif

#ifndef elf_backend_is_function_type
#define elf_backend_is_function_type _bfd_elf_is_function_type
#endif

extern const struct elf_size_info _bfd_elfNN_size_info;

static struct elf_backend_data elfNN_bed =
{
  ELF_ARCH,			/* arch */
  ELF_MACHINE_CODE,		/* elf_machine_code */
  ELF_OSABI,			/* elf_osabi  */
  ELF_MAXPAGESIZE,		/* maxpagesize */
  ELF_MINPAGESIZE,		/* minpagesize */
  ELF_COMMONPAGESIZE,		/* commonpagesize */
  ELF_DYNAMIC_SEC_FLAGS,	/* dynamic_sec_flags */
  elf_info_to_howto,
  elf_info_to_howto_rel,
  elf_backend_sym_is_global,
  elf_backend_object_p,
  elf_backend_symbol_processing,
  elf_backend_symbol_table_processing,
  elf_backend_get_symbol_type,
  elf_backend_archive_symbol_lookup,
  elf_backend_name_local_section_symbols,
  elf_backend_section_processing,
  elf_backend_section_from_shdr,
  elf_backend_section_flags,
  elf_backend_get_sec_type_attr,
  elf_backend_section_from_phdr,
  elf_backend_fake_sections,
  elf_backend_section_from_bfd_section,
  elf_backend_add_symbol_hook,
  elf_backend_link_output_symbol_hook,
  elf_backend_create_dynamic_sections,
  elf_backend_omit_section_dynsym,
  elf_backend_relocs_compatible,
  elf_backend_check_relocs,
  elf_backend_check_directives,
  elf_backend_as_needed_cleanup,
  elf_backend_adjust_dynamic_symbol,
  elf_backend_always_size_sections,
  elf_backend_size_dynamic_sections,
  elf_backend_init_index_section,
  elf_backend_relocate_section,
  elf_backend_finish_dynamic_symbol,
  elf_backend_finish_dynamic_sections,
  elf_backend_begin_write_processing,
  elf_backend_final_write_processing,
  elf_backend_additional_program_headers,
  elf_backend_modify_segment_map,
  elf_backend_modify_program_headers,
  elf_backend_gc_mark_dynamic_ref,
  elf_backend_gc_mark_hook,
  elf_backend_gc_mark_extra_sections,
  elf_backend_gc_sweep_hook,
  elf_backend_post_process_headers,
  elf_backend_print_symbol_all,
  elf_backend_output_arch_local_syms,
  elf_backend_output_arch_syms,
  elf_backend_copy_indirect_symbol,
  elf_backend_hide_symbol,
  elf_backend_fixup_symbol,
  elf_backend_merge_symbol_attribute,
  elf_backend_ignore_undef_symbol,
  elf_backend_emit_relocs,
  elf_backend_count_relocs,
  elf_backend_grok_prstatus,
  elf_backend_grok_psinfo,
  elf_backend_write_core_note,
  elf_backend_sprintf_vma,
  elf_backend_fprintf_vma,
  elf_backend_reloc_type_class,
  elf_backend_discard_info,
  elf_backend_ignore_discarded_relocs,
  elf_backend_action_discarded,
  elf_backend_eh_frame_address_size,
  elf_backend_can_make_relative_eh_frame,
  elf_backend_can_make_lsda_relative_eh_frame,
  elf_backend_encode_eh_address,
  elf_backend_write_section,
  elf_backend_mips_irix_compat,
  elf_backend_mips_rtype_to_howto,
  elf_backend_ecoff_debug_swap,
  elf_backend_bfd_from_remote_memory,
  elf_backend_plt_sym_val,
  elf_backend_common_definition,
  elf_backend_common_section_index,
  elf_backend_common_section,
  elf_backend_merge_symbol,
  elf_backend_hash_symbol,
  elf_backend_is_function_type,
  elf_backend_link_order_error_handler,
  elf_backend_relplt_name,
  ELF_MACHINE_ALT1,
  ELF_MACHINE_ALT2,
  &elf_backend_size_info,
  elf_backend_special_sections,
  elf_backend_got_header_size,
  elf_backend_obj_attrs_vendor,
  elf_backend_obj_attrs_section,
  elf_backend_obj_attrs_arg_type,
  elf_backend_obj_attrs_section_type,
  elf_backend_collect,
  elf_backend_type_change_ok,
  elf_backend_may_use_rel_p,
  elf_backend_may_use_rela_p,
  elf_backend_default_use_rela_p,
  elf_backend_rela_normal,
  elf_backend_sign_extend_vma,
  elf_backend_want_got_plt,
  elf_backend_plt_readonly,
  elf_backend_want_plt_sym,
  elf_backend_plt_not_loaded,
  elf_backend_plt_alignment,
  elf_backend_can_gc_sections,
  elf_backend_can_refcount,
  elf_backend_want_got_sym,
  elf_backend_want_dynbss,
  elf_backend_want_p_paddr_set_to_zero,
  elf_backend_default_execstack
};

/* Forward declaration for use when initialising alternative_target field.  */
#ifdef TARGET_LITTLE_SYM
extern const bfd_target TARGET_LITTLE_SYM;
#endif

#ifdef TARGET_BIG_SYM
const bfd_target TARGET_BIG_SYM =
{
  /* name: identify kind of target */
  TARGET_BIG_NAME,

  /* flavour: general indication about file */
  bfd_target_elf_flavour,

  /* byteorder: data is big endian */
  BFD_ENDIAN_BIG,

  /* header_byteorder: header is also big endian */
  BFD_ENDIAN_BIG,

  /* object_flags: mask of all file flags */
  (HAS_RELOC | EXEC_P | HAS_LINENO | HAS_DEBUG | HAS_SYMS | HAS_LOCALS
   | DYNAMIC | WP_TEXT | D_PAGED),

  /* section_flags: mask of all section flags */
  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC | SEC_READONLY
   | SEC_CODE | SEC_DATA | SEC_DEBUGGING | SEC_EXCLUDE | SEC_SORT_ENTRIES
   | SEC_SMALL_DATA | SEC_MERGE | SEC_STRINGS | SEC_GROUP),

   /* leading_symbol_char: is the first char of a user symbol
      predictable, and if so what is it */
  elf_symbol_leading_char,

  /* ar_pad_char: pad character for filenames within an archive header
     FIXME:  this really has nothing to do with ELF, this is a characteristic
     of the archiver and/or os and should be independently tunable */
  '/',

  /* ar_max_namelen: maximum number of characters in an archive header
     FIXME:  this really has nothing to do with ELF, this is a characteristic
     of the archiver and should be independently tunable.  The System V ABI,
     Chapter 7 (Formats & Protocols), Archive section sets this as 15.  */
  15,

  /* Routines to byte-swap various sized integers from the data sections */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
    bfd_getb32, bfd_getb_signed_32, bfd_putb32,
    bfd_getb16, bfd_getb_signed_16, bfd_putb16,

  /* Routines to byte-swap various sized integers from the file headers */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
    bfd_getb32, bfd_getb_signed_32, bfd_putb32,
    bfd_getb16, bfd_getb_signed_16, bfd_putb16,

  /* bfd_check_format: check the format of a file being read */
  { _bfd_dummy_target,		/* unknown format */
    bfd_elfNN_object_p,		/* assembler/linker output (object file) */
    bfd_elfNN_archive_p,	/* an archive */
    bfd_elfNN_core_file_p	/* a core file */
  },

  /* bfd_set_format: set the format of a file being written */
  { bfd_false,
    bfd_elfNN_mkobject,
    bfd_elfNN_mkarchive,
    bfd_elfNN_mkcorefile
  },

  /* bfd_write_contents: write cached information into a file being written */
  { bfd_false,
    bfd_elfNN_write_object_contents,
    bfd_elfNN_write_archive_contents,
    bfd_elfNN_write_corefile_contents,
  },

  BFD_JUMP_TABLE_GENERIC (bfd_elfNN),
  BFD_JUMP_TABLE_COPY (bfd_elfNN),
  BFD_JUMP_TABLE_CORE (bfd_elfNN),
#ifdef bfd_elfNN_archive_functions
  BFD_JUMP_TABLE_ARCHIVE (bfd_elfNN_archive),
#else
  BFD_JUMP_TABLE_ARCHIVE (_bfd_archive_coff),
#endif
  BFD_JUMP_TABLE_SYMBOLS (bfd_elfNN),
  BFD_JUMP_TABLE_RELOCS (bfd_elfNN),
  BFD_JUMP_TABLE_WRITE (bfd_elfNN),
  BFD_JUMP_TABLE_LINK (bfd_elfNN),
  BFD_JUMP_TABLE_DYNAMIC (bfd_elfNN),

  /* Alternative endian target.  */
#ifdef TARGET_LITTLE_SYM
  & TARGET_LITTLE_SYM,
#else
  NULL,
#endif

  /* backend_data: */
  &elfNN_bed
};
#endif

#ifdef TARGET_LITTLE_SYM
const bfd_target TARGET_LITTLE_SYM =
{
  /* name: identify kind of target */
  TARGET_LITTLE_NAME,

  /* flavour: general indication about file */
  bfd_target_elf_flavour,

  /* byteorder: data is little endian */
  BFD_ENDIAN_LITTLE,

  /* header_byteorder: header is also little endian */
  BFD_ENDIAN_LITTLE,

  /* object_flags: mask of all file flags */
  (HAS_RELOC | EXEC_P | HAS_LINENO | HAS_DEBUG | HAS_SYMS | HAS_LOCALS
   | DYNAMIC | WP_TEXT | D_PAGED),

  /* section_flags: mask of all section flags */
  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC | SEC_READONLY
   | SEC_CODE | SEC_DATA | SEC_DEBUGGING | SEC_EXCLUDE | SEC_SORT_ENTRIES
   | SEC_SMALL_DATA | SEC_MERGE | SEC_STRINGS | SEC_GROUP),

   /* leading_symbol_char: is the first char of a user symbol
      predictable, and if so what is it */
  elf_symbol_leading_char,

  /* ar_pad_char: pad character for filenames within an archive header
     FIXME:  this really has nothing to do with ELF, this is a characteristic
     of the archiver and/or os and should be independently tunable */
  '/',

  /* ar_max_namelen: maximum number of characters in an archive header
     FIXME:  this really has nothing to do with ELF, this is a characteristic
     of the archiver and should be independently tunable.  The System V ABI,
     Chapter 7 (Formats & Protocols), Archive section sets this as 15.  */
  15,

  /* Routines to byte-swap various sized integers from the data sections */
  bfd_getl64, bfd_getl_signed_64, bfd_putl64,
    bfd_getl32, bfd_getl_signed_32, bfd_putl32,
    bfd_getl16, bfd_getl_signed_16, bfd_putl16,

  /* Routines to byte-swap various sized integers from the file headers */
  bfd_getl64, bfd_getl_signed_64, bfd_putl64,
    bfd_getl32, bfd_getl_signed_32, bfd_putl32,
    bfd_getl16, bfd_getl_signed_16, bfd_putl16,

  /* bfd_check_format: check the format of a file being read */
  { _bfd_dummy_target,		/* unknown format */
    bfd_elfNN_object_p,		/* assembler/linker output (object file) */
    bfd_elfNN_archive_p,	/* an archive */
    bfd_elfNN_core_file_p	/* a core file */
  },

  /* bfd_set_format: set the format of a file being written */
  { bfd_false,
    bfd_elfNN_mkobject,
    bfd_elfNN_mkarchive,
    bfd_elfNN_mkcorefile
  },

  /* bfd_write_contents: write cached information into a file being written */
  { bfd_false,
    bfd_elfNN_write_object_contents,
    bfd_elfNN_write_archive_contents,
    bfd_elfNN_write_corefile_contents,
  },

  BFD_JUMP_TABLE_GENERIC (bfd_elfNN),
  BFD_JUMP_TABLE_COPY (bfd_elfNN),
  BFD_JUMP_TABLE_CORE (bfd_elfNN),
#ifdef bfd_elfNN_archive_functions
  BFD_JUMP_TABLE_ARCHIVE (bfd_elfNN_archive),
#else
  BFD_JUMP_TABLE_ARCHIVE (_bfd_archive_coff),
#endif
  BFD_JUMP_TABLE_SYMBOLS (bfd_elfNN),
  BFD_JUMP_TABLE_RELOCS (bfd_elfNN),
  BFD_JUMP_TABLE_WRITE (bfd_elfNN),
  BFD_JUMP_TABLE_LINK (bfd_elfNN),
  BFD_JUMP_TABLE_DYNAMIC (bfd_elfNN),

  /* Alternative endian target.  */
#ifdef TARGET_BIG_SYM
  & TARGET_BIG_SYM,
#else
  NULL,
#endif

  /* backend_data: */
  &elfNN_bed
};
#endif

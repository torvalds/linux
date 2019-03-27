/* DO NOT EDIT!  -*- buffer-read-only: t -*-  This file is automatically 
   generated from "libbfd-in.h", "init.c", "libbfd.c", "bfdio.c", 
   "bfdwin.c", "cache.c", "reloc.c", "archures.c" and "elf.c".
   Run "make headers" in your build bfd/ to regenerate.  */

/* libbfd.h -- Declarations used by bfd library *implementation*.
   (This include file is not for users of the library.)

   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

   Written by Cygnus Support.

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

#include "hashtab.h"

/* Align an address upward to a boundary, expressed as a number of bytes.
   E.g. align to an 8-byte boundary with argument of 8.  Take care never
   to wrap around if the address is within boundary-1 of the end of the
   address space.  */
#define BFD_ALIGN(this, boundary)					  \
  ((((bfd_vma) (this) + (boundary) - 1) >= (bfd_vma) (this))		  \
   ? (((bfd_vma) (this) + ((boundary) - 1)) & ~ (bfd_vma) ((boundary)-1)) \
   : ~ (bfd_vma) 0)

/* If you want to read and write large blocks, you might want to do it
   in quanta of this amount */
#define DEFAULT_BUFFERSIZE 8192

/* Set a tdata field.  Can't use the other macros for this, since they
   do casts, and casting to the left of assignment isn't portable.  */
#define set_tdata(bfd, v) ((bfd)->tdata.any = (v))

/* If BFD_IN_MEMORY is set for a BFD, then the iostream fields points
   to an instance of this structure.  */

struct bfd_in_memory
{
  /* Size of buffer.  */
  bfd_size_type size;
  /* Buffer holding contents of BFD.  */
  bfd_byte *buffer;
};

struct section_hash_entry
{
  struct bfd_hash_entry root;
  asection section;
};

/* tdata for an archive.  For an input archive, cache
   needs to be free()'d.  For an output archive, symdefs do.  */

struct artdata {
  file_ptr first_file_filepos;
  /* Speed up searching the armap */
  htab_t cache;
  bfd *archive_head;		/* Only interesting in output routines */
  carsym *symdefs;		/* the symdef entries */
  symindex symdef_count;	/* how many there are */
  char *extended_names;		/* clever intel extension */
  bfd_size_type extended_names_size; /* Size of extended names */
  /* when more compilers are standard C, this can be a time_t */
  long  armap_timestamp;	/* Timestamp value written into armap.
				   This is used for BSD archives to check
				   that the timestamp is recent enough
				   for the BSD linker to not complain,
				   just before we finish writing an
				   archive.  */
  file_ptr armap_datepos;	/* Position within archive to seek to
				   rewrite the date field.  */
  void *tdata;			/* Backend specific information.  */
};

#define bfd_ardata(bfd) ((bfd)->tdata.aout_ar_data)

/* Goes in bfd's arelt_data slot */
struct areltdata {
  char * arch_header;		/* it's actually a string */
  unsigned int parsed_size;	/* octets of filesize not including ar_hdr */
  char *filename;		/* null-terminated */
};

#define arelt_size(bfd) (((struct areltdata *)((bfd)->arelt_data))->parsed_size)

extern void *bfd_malloc
  (bfd_size_type);
extern void *bfd_realloc
  (void *, bfd_size_type);
extern void *bfd_zmalloc
  (bfd_size_type);
extern void *bfd_malloc2
  (bfd_size_type, bfd_size_type);
extern void *bfd_realloc2
  (void *, bfd_size_type, bfd_size_type);
extern void *bfd_zmalloc2
  (bfd_size_type, bfd_size_type);

extern void _bfd_default_error_handler (const char *s, ...);
extern bfd_error_handler_type _bfd_error_handler;

/* These routines allocate and free things on the BFD's objalloc.  */

extern void *bfd_alloc
  (bfd *, bfd_size_type);
extern void *bfd_zalloc
  (bfd *, bfd_size_type);
extern void *bfd_alloc2
  (bfd *, bfd_size_type, bfd_size_type);
extern void *bfd_zalloc2
  (bfd *, bfd_size_type, bfd_size_type);
extern void bfd_release
  (bfd *, void *);

bfd * _bfd_create_empty_archive_element_shell
  (bfd *obfd);
bfd * _bfd_look_for_bfd_in_cache
  (bfd *, file_ptr);
bfd_boolean _bfd_add_bfd_to_archive_cache
  (bfd *, file_ptr, bfd *);
bfd_boolean _bfd_generic_mkarchive
  (bfd *abfd);
const bfd_target *bfd_generic_archive_p
  (bfd *abfd);
bfd_boolean bfd_slurp_armap
  (bfd *abfd);
bfd_boolean bfd_slurp_bsd_armap_f2
  (bfd *abfd);
#define bfd_slurp_bsd_armap bfd_slurp_armap
#define bfd_slurp_coff_armap bfd_slurp_armap
bfd_boolean _bfd_slurp_extended_name_table
  (bfd *abfd);
extern bfd_boolean _bfd_construct_extended_name_table
  (bfd *, bfd_boolean, char **, bfd_size_type *);
bfd_boolean _bfd_write_archive_contents
  (bfd *abfd);
bfd_boolean _bfd_compute_and_write_armap
  (bfd *, unsigned int elength);
bfd *_bfd_get_elt_at_filepos
  (bfd *archive, file_ptr filepos);
extern bfd *_bfd_generic_get_elt_at_index
  (bfd *, symindex);
bfd * _bfd_new_bfd
  (void);
void _bfd_delete_bfd
  (bfd *);
bfd_boolean _bfd_free_cached_info
  (bfd *);

bfd_boolean bfd_false
  (bfd *ignore);
bfd_boolean bfd_true
  (bfd *ignore);
void *bfd_nullvoidptr
  (bfd *ignore);
int bfd_0
  (bfd *ignore);
unsigned int bfd_0u
  (bfd *ignore);
long bfd_0l
  (bfd *ignore);
long _bfd_n1
  (bfd *ignore);
void bfd_void
  (bfd *ignore);

bfd *_bfd_new_bfd_contained_in
  (bfd *);
const bfd_target *_bfd_dummy_target
  (bfd *abfd);

void bfd_dont_truncate_arname
  (bfd *abfd, const char *filename, char *hdr);
void bfd_bsd_truncate_arname
  (bfd *abfd, const char *filename, char *hdr);
void bfd_gnu_truncate_arname
  (bfd *abfd, const char *filename, char *hdr);

bfd_boolean bsd_write_armap
  (bfd *arch, unsigned int elength, struct orl *map, unsigned int orl_count,
   int stridx);

bfd_boolean coff_write_armap
  (bfd *arch, unsigned int elength, struct orl *map, unsigned int orl_count,
   int stridx);

extern void *_bfd_generic_read_ar_hdr
  (bfd *);
extern void _bfd_ar_spacepad
  (char *, size_t, const char *, long);

extern void *_bfd_generic_read_ar_hdr_mag
  (bfd *, const char *);

bfd * bfd_generic_openr_next_archived_file
  (bfd *archive, bfd *last_file);

int bfd_generic_stat_arch_elt
  (bfd *, struct stat *);

#define _bfd_read_ar_hdr(abfd) \
  BFD_SEND (abfd, _bfd_read_ar_hdr_fn, (abfd))

/* Generic routines to use for BFD_JUMP_TABLE_GENERIC.  Use
   BFD_JUMP_TABLE_GENERIC (_bfd_generic).  */

#define _bfd_generic_close_and_cleanup bfd_true
#define _bfd_generic_bfd_free_cached_info bfd_true
extern bfd_boolean _bfd_generic_new_section_hook
  (bfd *, asection *);
extern bfd_boolean _bfd_generic_get_section_contents
  (bfd *, asection *, void *, file_ptr, bfd_size_type);
extern bfd_boolean _bfd_generic_get_section_contents_in_window
  (bfd *, asection *, bfd_window *, file_ptr, bfd_size_type);

/* Generic routines to use for BFD_JUMP_TABLE_COPY.  Use
   BFD_JUMP_TABLE_COPY (_bfd_generic).  */

#define _bfd_generic_bfd_copy_private_bfd_data \
  ((bfd_boolean (*) (bfd *, bfd *)) bfd_true)
#define _bfd_generic_bfd_merge_private_bfd_data \
  ((bfd_boolean (*) (bfd *, bfd *)) bfd_true)
#define _bfd_generic_bfd_set_private_flags \
  ((bfd_boolean (*) (bfd *, flagword)) bfd_true)
#define _bfd_generic_bfd_copy_private_section_data \
  ((bfd_boolean (*) (bfd *, asection *, bfd *, asection *)) bfd_true)
#define _bfd_generic_bfd_copy_private_symbol_data \
  ((bfd_boolean (*) (bfd *, asymbol *, bfd *, asymbol *)) bfd_true)
#define _bfd_generic_bfd_copy_private_header_data \
  ((bfd_boolean (*) (bfd *, bfd *)) bfd_true)
#define _bfd_generic_bfd_print_private_bfd_data \
  ((bfd_boolean (*) (bfd *, void *)) bfd_true)

extern bfd_boolean _bfd_generic_init_private_section_data
  (bfd *, asection *, bfd *, asection *, struct bfd_link_info *);

/* Routines to use for BFD_JUMP_TABLE_CORE when there is no core file
   support.  Use BFD_JUMP_TABLE_CORE (_bfd_nocore).  */

extern char *_bfd_nocore_core_file_failing_command
  (bfd *);
extern int _bfd_nocore_core_file_failing_signal
  (bfd *);
extern bfd_boolean _bfd_nocore_core_file_matches_executable_p
  (bfd *, bfd *);

/* Routines to use for BFD_JUMP_TABLE_ARCHIVE when there is no archive
   file support.  Use BFD_JUMP_TABLE_ARCHIVE (_bfd_noarchive).  */

#define _bfd_noarchive_slurp_armap bfd_false
#define _bfd_noarchive_slurp_extended_name_table bfd_false
#define _bfd_noarchive_construct_extended_name_table \
  ((bfd_boolean (*) (bfd *, char **, bfd_size_type *, const char **)) \
   bfd_false)
#define _bfd_noarchive_truncate_arname \
  ((void (*) (bfd *, const char *, char *)) bfd_void)
#define _bfd_noarchive_write_armap \
  ((bfd_boolean (*) (bfd *, unsigned int, struct orl *, unsigned int, int)) \
   bfd_false)
#define _bfd_noarchive_read_ar_hdr bfd_nullvoidptr
#define _bfd_noarchive_openr_next_archived_file \
  ((bfd *(*) (bfd *, bfd *)) bfd_nullvoidptr)
#define _bfd_noarchive_get_elt_at_index \
  ((bfd *(*) (bfd *, symindex)) bfd_nullvoidptr)
#define _bfd_noarchive_generic_stat_arch_elt bfd_generic_stat_arch_elt
#define _bfd_noarchive_update_armap_timestamp bfd_false

/* Routines to use for BFD_JUMP_TABLE_ARCHIVE to get BSD style
   archives.  Use BFD_JUMP_TABLE_ARCHIVE (_bfd_archive_bsd).  */

#define _bfd_archive_bsd_slurp_armap bfd_slurp_bsd_armap
#define _bfd_archive_bsd_slurp_extended_name_table \
  _bfd_slurp_extended_name_table
extern bfd_boolean _bfd_archive_bsd_construct_extended_name_table
  (bfd *, char **, bfd_size_type *, const char **);
#define _bfd_archive_bsd_truncate_arname bfd_bsd_truncate_arname
#define _bfd_archive_bsd_write_armap bsd_write_armap
#define _bfd_archive_bsd_read_ar_hdr _bfd_generic_read_ar_hdr
#define _bfd_archive_bsd_openr_next_archived_file \
  bfd_generic_openr_next_archived_file
#define _bfd_archive_bsd_get_elt_at_index _bfd_generic_get_elt_at_index
#define _bfd_archive_bsd_generic_stat_arch_elt \
  bfd_generic_stat_arch_elt
extern bfd_boolean _bfd_archive_bsd_update_armap_timestamp
  (bfd *);

/* Routines to use for BFD_JUMP_TABLE_ARCHIVE to get COFF style
   archives.  Use BFD_JUMP_TABLE_ARCHIVE (_bfd_archive_coff).  */

#define _bfd_archive_coff_slurp_armap bfd_slurp_coff_armap
#define _bfd_archive_coff_slurp_extended_name_table \
  _bfd_slurp_extended_name_table
extern bfd_boolean _bfd_archive_coff_construct_extended_name_table
  (bfd *, char **, bfd_size_type *, const char **);
#define _bfd_archive_coff_truncate_arname bfd_dont_truncate_arname
#define _bfd_archive_coff_write_armap coff_write_armap
#define _bfd_archive_coff_read_ar_hdr _bfd_generic_read_ar_hdr
#define _bfd_archive_coff_openr_next_archived_file \
  bfd_generic_openr_next_archived_file
#define _bfd_archive_coff_get_elt_at_index _bfd_generic_get_elt_at_index
#define _bfd_archive_coff_generic_stat_arch_elt \
  bfd_generic_stat_arch_elt
#define _bfd_archive_coff_update_armap_timestamp bfd_true

/* Routines to use for BFD_JUMP_TABLE_SYMBOLS where there is no symbol
   support.  Use BFD_JUMP_TABLE_SYMBOLS (_bfd_nosymbols).  */

#define _bfd_nosymbols_get_symtab_upper_bound _bfd_n1
#define _bfd_nosymbols_canonicalize_symtab \
  ((long (*) (bfd *, asymbol **)) _bfd_n1)
#define _bfd_nosymbols_make_empty_symbol _bfd_generic_make_empty_symbol
#define _bfd_nosymbols_print_symbol \
  ((void (*) (bfd *, void *, asymbol *, bfd_print_symbol_type)) bfd_void)
#define _bfd_nosymbols_get_symbol_info \
  ((void (*) (bfd *, asymbol *, symbol_info *)) bfd_void)
#define _bfd_nosymbols_bfd_is_local_label_name \
  ((bfd_boolean (*) (bfd *, const char *)) bfd_false)
#define _bfd_nosymbols_bfd_is_target_special_symbol \
  ((bfd_boolean (*) (bfd *, asymbol *)) bfd_false)
#define _bfd_nosymbols_get_lineno \
  ((alent *(*) (bfd *, asymbol *)) bfd_nullvoidptr)
#define _bfd_nosymbols_find_nearest_line \
  ((bfd_boolean (*) (bfd *, asection *, asymbol **, bfd_vma, const char **, \
		     const char **, unsigned int *)) \
   bfd_false)
#define _bfd_nosymbols_find_inliner_info \
  ((bfd_boolean (*) (bfd *, const char **, const char **, unsigned int *)) \
   bfd_false)
#define _bfd_nosymbols_bfd_make_debug_symbol \
  ((asymbol *(*) (bfd *, void *, unsigned long)) bfd_nullvoidptr)
#define _bfd_nosymbols_read_minisymbols \
  ((long (*) (bfd *, bfd_boolean, void **, unsigned int *)) _bfd_n1)
#define _bfd_nosymbols_minisymbol_to_symbol \
  ((asymbol *(*) (bfd *, bfd_boolean, const void *, asymbol *)) \
   bfd_nullvoidptr)

/* Routines to use for BFD_JUMP_TABLE_RELOCS when there is no reloc
   support.  Use BFD_JUMP_TABLE_RELOCS (_bfd_norelocs).  */

extern long _bfd_norelocs_get_reloc_upper_bound (bfd *, asection *);
extern long _bfd_norelocs_canonicalize_reloc (bfd *, asection *,
					      arelent **, asymbol **);
#define _bfd_norelocs_bfd_reloc_type_lookup \
  ((reloc_howto_type *(*) (bfd *, bfd_reloc_code_real_type)) bfd_nullvoidptr)
#define _bfd_norelocs_bfd_reloc_name_lookup \
  ((reloc_howto_type *(*) (bfd *, const char *)) bfd_nullvoidptr)

/* Routines to use for BFD_JUMP_TABLE_WRITE for targets which may not
   be written.  Use BFD_JUMP_TABLE_WRITE (_bfd_nowrite).  */

#define _bfd_nowrite_set_arch_mach \
  ((bfd_boolean (*) (bfd *, enum bfd_architecture, unsigned long)) \
   bfd_false)
#define _bfd_nowrite_set_section_contents \
  ((bfd_boolean (*) (bfd *, asection *, const void *, file_ptr, bfd_size_type)) \
   bfd_false)

/* Generic routines to use for BFD_JUMP_TABLE_WRITE.  Use
   BFD_JUMP_TABLE_WRITE (_bfd_generic).  */

#define _bfd_generic_set_arch_mach bfd_default_set_arch_mach
extern bfd_boolean _bfd_generic_set_section_contents
  (bfd *, asection *, const void *, file_ptr, bfd_size_type);

/* Routines to use for BFD_JUMP_TABLE_LINK for targets which do not
   support linking.  Use BFD_JUMP_TABLE_LINK (_bfd_nolink).  */

#define _bfd_nolink_sizeof_headers \
  ((int (*) (bfd *, struct bfd_link_info *)) bfd_0)
#define _bfd_nolink_bfd_get_relocated_section_contents \
  ((bfd_byte *(*) (bfd *, struct bfd_link_info *, struct bfd_link_order *, \
		   bfd_byte *, bfd_boolean, asymbol **)) \
   bfd_nullvoidptr)
#define _bfd_nolink_bfd_relax_section \
  ((bfd_boolean (*) \
    (bfd *, asection *, struct bfd_link_info *, bfd_boolean *)) \
   bfd_false)
#define _bfd_nolink_bfd_gc_sections \
  ((bfd_boolean (*) (bfd *, struct bfd_link_info *)) \
   bfd_false)
#define _bfd_nolink_bfd_merge_sections \
  ((bfd_boolean (*) (bfd *, struct bfd_link_info *)) \
   bfd_false)
#define _bfd_nolink_bfd_is_group_section \
  ((bfd_boolean (*) (bfd *, const struct bfd_section *)) \
   bfd_false)
#define _bfd_nolink_bfd_discard_group \
  ((bfd_boolean (*) (bfd *, struct bfd_section *)) \
   bfd_false)
#define _bfd_nolink_bfd_link_hash_table_create \
  ((struct bfd_link_hash_table *(*) (bfd *)) bfd_nullvoidptr)
#define _bfd_nolink_bfd_link_hash_table_free \
  ((void (*) (struct bfd_link_hash_table *)) bfd_void)
#define _bfd_nolink_bfd_link_add_symbols \
  ((bfd_boolean (*) (bfd *, struct bfd_link_info *)) bfd_false)
#define _bfd_nolink_bfd_link_just_syms \
  ((void (*) (asection *, struct bfd_link_info *)) bfd_void)
#define _bfd_nolink_bfd_final_link \
  ((bfd_boolean (*) (bfd *, struct bfd_link_info *)) bfd_false)
#define _bfd_nolink_bfd_link_split_section \
  ((bfd_boolean (*) (bfd *, struct bfd_section *)) bfd_false)
#define _bfd_nolink_section_already_linked \
  ((void (*) (bfd *, struct bfd_section *, struct bfd_link_info *)) bfd_void)

/* Routines to use for BFD_JUMP_TABLE_DYNAMIC for targets which do not
   have dynamic symbols or relocs.  Use BFD_JUMP_TABLE_DYNAMIC
   (_bfd_nodynamic).  */

#define _bfd_nodynamic_get_dynamic_symtab_upper_bound _bfd_n1
#define _bfd_nodynamic_canonicalize_dynamic_symtab \
  ((long (*) (bfd *, asymbol **)) _bfd_n1)
#define _bfd_nodynamic_get_synthetic_symtab \
  ((long (*) (bfd *, long, asymbol **, long, asymbol **, asymbol **)) _bfd_n1)
#define _bfd_nodynamic_get_dynamic_reloc_upper_bound _bfd_n1
#define _bfd_nodynamic_canonicalize_dynamic_reloc \
  ((long (*) (bfd *, arelent **, asymbol **)) _bfd_n1)

/* Generic routine to determine of the given symbol is a local
   label.  */
extern bfd_boolean bfd_generic_is_local_label_name
  (bfd *, const char *);

/* Generic minisymbol routines.  */
extern long _bfd_generic_read_minisymbols
  (bfd *, bfd_boolean, void **, unsigned int *);
extern asymbol *_bfd_generic_minisymbol_to_symbol
  (bfd *, bfd_boolean, const void *, asymbol *);

/* Find the nearest line using .stab/.stabstr sections.  */
extern bfd_boolean _bfd_stab_section_find_nearest_line
  (bfd *, asymbol **, asection *, bfd_vma, bfd_boolean *,
   const char **, const char **, unsigned int *, void **);

/* Find the nearest line using DWARF 1 debugging information.  */
extern bfd_boolean _bfd_dwarf1_find_nearest_line
  (bfd *, asection *, asymbol **, bfd_vma, const char **,
   const char **, unsigned int *);

/* Find the nearest line using DWARF 2 debugging information.  */
extern bfd_boolean _bfd_dwarf2_find_nearest_line
  (bfd *, asection *, asymbol **, bfd_vma, const char **, const char **,
   unsigned int *, unsigned int, void **);

/* Find the line using DWARF 2 debugging information.  */
extern bfd_boolean _bfd_dwarf2_find_line
  (bfd *, asymbol **, asymbol *, const char **,
   unsigned int *, unsigned int, void **);

bfd_boolean _bfd_generic_find_line
  (bfd *, asymbol **, asymbol *, const char **, unsigned int *);

/* Find inliner info after calling bfd_find_nearest_line. */
extern bfd_boolean _bfd_dwarf2_find_inliner_info
  (bfd *, const char **, const char **, unsigned int *, void **);
  
/* Create a new section entry.  */
extern struct bfd_hash_entry *bfd_section_hash_newfunc
  (struct bfd_hash_entry *, struct bfd_hash_table *, const char *);

/* A routine to create entries for a bfd_link_hash_table.  */
extern struct bfd_hash_entry *_bfd_link_hash_newfunc
  (struct bfd_hash_entry *entry, struct bfd_hash_table *table,
   const char *string);

/* Initialize a bfd_link_hash_table.  */
extern bfd_boolean _bfd_link_hash_table_init
  (struct bfd_link_hash_table *, bfd *,
   struct bfd_hash_entry *(*) (struct bfd_hash_entry *,
			       struct bfd_hash_table *,
			       const char *),
   unsigned int);

/* Generic link hash table creation routine.  */
extern struct bfd_link_hash_table *_bfd_generic_link_hash_table_create
  (bfd *);

/* Generic link hash table destruction routine.  */
extern void _bfd_generic_link_hash_table_free
  (struct bfd_link_hash_table *);

/* Generic add symbol routine.  */
extern bfd_boolean _bfd_generic_link_add_symbols
  (bfd *, struct bfd_link_info *);

/* Generic add symbol routine.  This version is used by targets for
   which the linker must collect constructors and destructors by name,
   as the collect2 program does.  */
extern bfd_boolean _bfd_generic_link_add_symbols_collect
  (bfd *, struct bfd_link_info *);

/* Generic archive add symbol routine.  */
extern bfd_boolean _bfd_generic_link_add_archive_symbols
  (bfd *, struct bfd_link_info *,
   bfd_boolean (*) (bfd *, struct bfd_link_info *, bfd_boolean *));

/* Forward declaration to avoid prototype errors.  */
typedef struct bfd_link_hash_entry _bfd_link_hash_entry;

/* Generic routine to add a single symbol.  */
extern bfd_boolean _bfd_generic_link_add_one_symbol
  (struct bfd_link_info *, bfd *, const char *name, flagword,
   asection *, bfd_vma, const char *, bfd_boolean copy,
   bfd_boolean constructor, struct bfd_link_hash_entry **);

/* Generic routine to mark section as supplying symbols only.  */
extern void _bfd_generic_link_just_syms
  (asection *, struct bfd_link_info *);

/* Generic link routine.  */
extern bfd_boolean _bfd_generic_final_link
  (bfd *, struct bfd_link_info *);

extern bfd_boolean _bfd_generic_link_split_section
  (bfd *, struct bfd_section *);

extern void _bfd_generic_section_already_linked
  (bfd *, struct bfd_section *, struct bfd_link_info *);

/* Generic reloc_link_order processing routine.  */
extern bfd_boolean _bfd_generic_reloc_link_order
  (bfd *, struct bfd_link_info *, asection *, struct bfd_link_order *);

/* Default link order processing routine.  */
extern bfd_boolean _bfd_default_link_order
  (bfd *, struct bfd_link_info *, asection *, struct bfd_link_order *);

/* Count the number of reloc entries in a link order list.  */
extern unsigned int _bfd_count_link_order_relocs
  (struct bfd_link_order *);

/* Final link relocation routine.  */
extern bfd_reloc_status_type _bfd_final_link_relocate
  (reloc_howto_type *, bfd *, asection *, bfd_byte *,
   bfd_vma, bfd_vma, bfd_vma);

/* Relocate a particular location by a howto and a value.  */
extern bfd_reloc_status_type _bfd_relocate_contents
  (reloc_howto_type *, bfd *, bfd_vma, bfd_byte *);

/* Clear a given location using a given howto.  */
extern void _bfd_clear_contents (reloc_howto_type *howto, bfd *input_bfd,
				 bfd_byte *location);

/* Link stabs in sections in the first pass.  */

extern bfd_boolean _bfd_link_section_stabs
  (bfd *, struct stab_info *, asection *, asection *, void **,
   bfd_size_type *);

/* Eliminate stabs for discarded functions and symbols.  */
extern bfd_boolean _bfd_discard_section_stabs
  (bfd *, asection *, void *, bfd_boolean (*) (bfd_vma, void *), void *);

/* Write out the .stab section when linking stabs in sections.  */

extern bfd_boolean _bfd_write_section_stabs
  (bfd *, struct stab_info *, asection *, void **, bfd_byte *);

/* Write out the .stabstr string table when linking stabs in sections.  */

extern bfd_boolean _bfd_write_stab_strings
  (bfd *, struct stab_info *);

/* Find an offset within a .stab section when linking stabs in
   sections.  */

extern bfd_vma _bfd_stab_section_offset
  (asection *, void *, bfd_vma);

/* Register a SEC_MERGE section as a candidate for merging.  */

extern bfd_boolean _bfd_add_merge_section
  (bfd *, void **, asection *, void **);

/* Attempt to merge SEC_MERGE sections.  */

extern bfd_boolean _bfd_merge_sections
  (bfd *, struct bfd_link_info *, void *, void (*) (bfd *, asection *));

/* Write out a merged section.  */

extern bfd_boolean _bfd_write_merged_section
  (bfd *, asection *, void *);

/* Find an offset within a modified SEC_MERGE section.  */

extern bfd_vma _bfd_merged_section_offset
  (bfd *, asection **, void *, bfd_vma);

/* Create a string table.  */
extern struct bfd_strtab_hash *_bfd_stringtab_init
  (void);

/* Create an XCOFF .debug section style string table.  */
extern struct bfd_strtab_hash *_bfd_xcoff_stringtab_init
  (void);

/* Free a string table.  */
extern void _bfd_stringtab_free
  (struct bfd_strtab_hash *);

/* Get the size of a string table.  */
extern bfd_size_type _bfd_stringtab_size
  (struct bfd_strtab_hash *);

/* Add a string to a string table.  */
extern bfd_size_type _bfd_stringtab_add
  (struct bfd_strtab_hash *, const char *, bfd_boolean hash, bfd_boolean copy);

/* Write out a string table.  */
extern bfd_boolean _bfd_stringtab_emit
  (bfd *, struct bfd_strtab_hash *);

/* Check that endianness of input and output file match.  */
extern bfd_boolean _bfd_generic_verify_endian_match
  (bfd *, bfd *);

/* Macros to tell if bfds are read or write enabled.

   Note that bfds open for read may be scribbled into if the fd passed
   to bfd_fdopenr is actually open both for read and write
   simultaneously.  However an output bfd will never be open for
   read.  Therefore sometimes you want to check bfd_read_p or
   !bfd_read_p, and only sometimes bfd_write_p.
*/

#define	bfd_read_p(abfd) \
  ((abfd)->direction == read_direction || (abfd)->direction == both_direction)
#define	bfd_write_p(abfd) \
  ((abfd)->direction == write_direction || (abfd)->direction == both_direction)

void bfd_assert
  (const char*,int);

#define BFD_ASSERT(x) \
  do { if (!(x)) bfd_assert(__FILE__,__LINE__); } while (0)

#define BFD_FAIL() \
  do { bfd_assert(__FILE__,__LINE__); } while (0)

extern void _bfd_abort
  (const char *, int, const char *) ATTRIBUTE_NORETURN;

/* if gcc >= 2.6, we can give a function name, too */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 6)
#define __PRETTY_FUNCTION__  ((char *) NULL)
#endif

#undef abort
#define abort() _bfd_abort (__FILE__, __LINE__, __PRETTY_FUNCTION__)

/* Manipulate a system FILE but using BFD's "file_ptr", rather than
   the system "off_t" or "off64_t", as the offset.  */
extern file_ptr real_ftell (FILE *file);
extern int real_fseek (FILE *file, file_ptr offset, int whence);
extern FILE *real_fopen (const char *filename, const char *modes);

/* List of supported target vectors, and the default vector (if
   bfd_default_vector[0] is NULL, there is no default).  */
extern const bfd_target * const *bfd_target_vector;
extern const bfd_target *bfd_default_vector[];

/* List of associated target vectors.  */
extern const bfd_target * const *bfd_associated_vector;

/* Functions shared by the ECOFF and MIPS ELF backends, which have no
   other common header files.  */

#if defined(__STDC__) || defined(ALMOST_STDC)
struct ecoff_find_line;
#endif

extern bfd_boolean _bfd_ecoff_locate_line
  (bfd *, asection *, bfd_vma, struct ecoff_debug_info * const,
   const struct ecoff_debug_swap * const, struct ecoff_find_line *,
   const char **, const char **, unsigned int *);
extern bfd_boolean _bfd_ecoff_get_accumulated_pdr
  (void *, bfd_byte *);
extern bfd_boolean _bfd_ecoff_get_accumulated_sym
  (void *, bfd_byte *);
extern bfd_boolean _bfd_ecoff_get_accumulated_ss
  (void *, bfd_byte *);

extern bfd_vma _bfd_get_gp_value
  (bfd *);
extern void _bfd_set_gp_value
  (bfd *, bfd_vma);

/* Function shared by the COFF and ELF SH backends, which have no
   other common header files.  */

#ifndef _bfd_sh_align_load_span
extern bfd_boolean _bfd_sh_align_load_span
  (bfd *, asection *, bfd_byte *,
   bfd_boolean (*) (bfd *, asection *, void *, bfd_byte *, bfd_vma),
   void *, bfd_vma **, bfd_vma *, bfd_vma, bfd_vma, bfd_boolean *);
#endif

/* This is the shape of the elements inside the already_linked hash
   table. It maps a name onto a list of already_linked elements with
   the same name.  */

struct bfd_section_already_linked_hash_entry
{
  struct bfd_hash_entry root;
  struct bfd_section_already_linked *entry;
};

struct bfd_section_already_linked
{
  struct bfd_section_already_linked *next;
  asection *sec;
};

extern struct bfd_section_already_linked_hash_entry *
  bfd_section_already_linked_table_lookup (const char *);
extern void bfd_section_already_linked_table_insert
  (struct bfd_section_already_linked_hash_entry *, asection *);
extern void bfd_section_already_linked_table_traverse
  (bfd_boolean (*) (struct bfd_section_already_linked_hash_entry *,
		    void *), void *);

extern bfd_vma read_unsigned_leb128 (bfd *, bfd_byte *, unsigned int *);
extern bfd_signed_vma read_signed_leb128 (bfd *, bfd_byte *, unsigned int *);

/* Extracted from init.c.  */
/* Extracted from libbfd.c.  */
bfd_boolean bfd_write_bigendian_4byte_int (bfd *, unsigned int);

unsigned int bfd_log2 (bfd_vma x);

/* Extracted from bfdio.c.  */
struct bfd_iovec
{
  /* To avoid problems with macros, a "b" rather than "f"
     prefix is prepended to each method name.  */
  /* Attempt to read/write NBYTES on ABFD's IOSTREAM storing/fetching
     bytes starting at PTR.  Return the number of bytes actually
     transfered (a read past end-of-file returns less than NBYTES),
     or -1 (setting <<bfd_error>>) if an error occurs.  */
  file_ptr (*bread) (struct bfd *abfd, void *ptr, file_ptr nbytes);
  file_ptr (*bwrite) (struct bfd *abfd, const void *ptr,
                      file_ptr nbytes);
  /* Return the current IOSTREAM file offset, or -1 (setting <<bfd_error>>
     if an error occurs.  */
  file_ptr (*btell) (struct bfd *abfd);
  /* For the following, on successful completion a value of 0 is returned.
     Otherwise, a value of -1 is returned (and  <<bfd_error>> is set).  */
  int (*bseek) (struct bfd *abfd, file_ptr offset, int whence);
  int (*bclose) (struct bfd *abfd);
  int (*bflush) (struct bfd *abfd);
  int (*bstat) (struct bfd *abfd, struct stat *sb);
};
/* Extracted from bfdwin.c.  */
struct _bfd_window_internal {
  struct _bfd_window_internal *next;
  void *data;
  bfd_size_type size;
  int refcount : 31;           /* should be enough...  */
  unsigned mapped : 1;         /* 1 = mmap, 0 = malloc */
};
/* Extracted from cache.c.  */
bfd_boolean bfd_cache_init (bfd *abfd);

bfd_boolean bfd_cache_close (bfd *abfd);

FILE* bfd_open_file (bfd *abfd);

/* Extracted from reloc.c.  */
#ifdef _BFD_MAKE_TABLE_bfd_reloc_code_real

static const char *const bfd_reloc_code_real_names[] = { "@@uninitialized@@",

  "BFD_RELOC_64",
  "BFD_RELOC_32",
  "BFD_RELOC_26",
  "BFD_RELOC_24",
  "BFD_RELOC_16",
  "BFD_RELOC_14",
  "BFD_RELOC_8",
  "BFD_RELOC_64_PCREL",
  "BFD_RELOC_32_PCREL",
  "BFD_RELOC_24_PCREL",
  "BFD_RELOC_16_PCREL",
  "BFD_RELOC_12_PCREL",
  "BFD_RELOC_8_PCREL",
  "BFD_RELOC_32_SECREL",
  "BFD_RELOC_32_GOT_PCREL",
  "BFD_RELOC_16_GOT_PCREL",
  "BFD_RELOC_8_GOT_PCREL",
  "BFD_RELOC_32_GOTOFF",
  "BFD_RELOC_16_GOTOFF",
  "BFD_RELOC_LO16_GOTOFF",
  "BFD_RELOC_HI16_GOTOFF",
  "BFD_RELOC_HI16_S_GOTOFF",
  "BFD_RELOC_8_GOTOFF",
  "BFD_RELOC_64_PLT_PCREL",
  "BFD_RELOC_32_PLT_PCREL",
  "BFD_RELOC_24_PLT_PCREL",
  "BFD_RELOC_16_PLT_PCREL",
  "BFD_RELOC_8_PLT_PCREL",
  "BFD_RELOC_64_PLTOFF",
  "BFD_RELOC_32_PLTOFF",
  "BFD_RELOC_16_PLTOFF",
  "BFD_RELOC_LO16_PLTOFF",
  "BFD_RELOC_HI16_PLTOFF",
  "BFD_RELOC_HI16_S_PLTOFF",
  "BFD_RELOC_8_PLTOFF",
  "BFD_RELOC_68K_GLOB_DAT",
  "BFD_RELOC_68K_JMP_SLOT",
  "BFD_RELOC_68K_RELATIVE",
  "BFD_RELOC_32_BASEREL",
  "BFD_RELOC_16_BASEREL",
  "BFD_RELOC_LO16_BASEREL",
  "BFD_RELOC_HI16_BASEREL",
  "BFD_RELOC_HI16_S_BASEREL",
  "BFD_RELOC_8_BASEREL",
  "BFD_RELOC_RVA",
  "BFD_RELOC_8_FFnn",
  "BFD_RELOC_32_PCREL_S2",
  "BFD_RELOC_16_PCREL_S2",
  "BFD_RELOC_23_PCREL_S2",
  "BFD_RELOC_HI22",
  "BFD_RELOC_LO10",
  "BFD_RELOC_GPREL16",
  "BFD_RELOC_GPREL32",
  "BFD_RELOC_I960_CALLJ",
  "BFD_RELOC_NONE",
  "BFD_RELOC_SPARC_WDISP22",
  "BFD_RELOC_SPARC22",
  "BFD_RELOC_SPARC13",
  "BFD_RELOC_SPARC_GOT10",
  "BFD_RELOC_SPARC_GOT13",
  "BFD_RELOC_SPARC_GOT22",
  "BFD_RELOC_SPARC_PC10",
  "BFD_RELOC_SPARC_PC22",
  "BFD_RELOC_SPARC_WPLT30",
  "BFD_RELOC_SPARC_COPY",
  "BFD_RELOC_SPARC_GLOB_DAT",
  "BFD_RELOC_SPARC_JMP_SLOT",
  "BFD_RELOC_SPARC_RELATIVE",
  "BFD_RELOC_SPARC_UA16",
  "BFD_RELOC_SPARC_UA32",
  "BFD_RELOC_SPARC_UA64",
  "BFD_RELOC_SPARC_BASE13",
  "BFD_RELOC_SPARC_BASE22",
  "BFD_RELOC_SPARC_10",
  "BFD_RELOC_SPARC_11",
  "BFD_RELOC_SPARC_OLO10",
  "BFD_RELOC_SPARC_HH22",
  "BFD_RELOC_SPARC_HM10",
  "BFD_RELOC_SPARC_LM22",
  "BFD_RELOC_SPARC_PC_HH22",
  "BFD_RELOC_SPARC_PC_HM10",
  "BFD_RELOC_SPARC_PC_LM22",
  "BFD_RELOC_SPARC_WDISP16",
  "BFD_RELOC_SPARC_WDISP19",
  "BFD_RELOC_SPARC_7",
  "BFD_RELOC_SPARC_6",
  "BFD_RELOC_SPARC_5",
  "BFD_RELOC_SPARC_PLT32",
  "BFD_RELOC_SPARC_PLT64",
  "BFD_RELOC_SPARC_HIX22",
  "BFD_RELOC_SPARC_LOX10",
  "BFD_RELOC_SPARC_H44",
  "BFD_RELOC_SPARC_M44",
  "BFD_RELOC_SPARC_L44",
  "BFD_RELOC_SPARC_REGISTER",
  "BFD_RELOC_SPARC_REV32",
  "BFD_RELOC_SPARC_TLS_GD_HI22",
  "BFD_RELOC_SPARC_TLS_GD_LO10",
  "BFD_RELOC_SPARC_TLS_GD_ADD",
  "BFD_RELOC_SPARC_TLS_GD_CALL",
  "BFD_RELOC_SPARC_TLS_LDM_HI22",
  "BFD_RELOC_SPARC_TLS_LDM_LO10",
  "BFD_RELOC_SPARC_TLS_LDM_ADD",
  "BFD_RELOC_SPARC_TLS_LDM_CALL",
  "BFD_RELOC_SPARC_TLS_LDO_HIX22",
  "BFD_RELOC_SPARC_TLS_LDO_LOX10",
  "BFD_RELOC_SPARC_TLS_LDO_ADD",
  "BFD_RELOC_SPARC_TLS_IE_HI22",
  "BFD_RELOC_SPARC_TLS_IE_LO10",
  "BFD_RELOC_SPARC_TLS_IE_LD",
  "BFD_RELOC_SPARC_TLS_IE_LDX",
  "BFD_RELOC_SPARC_TLS_IE_ADD",
  "BFD_RELOC_SPARC_TLS_LE_HIX22",
  "BFD_RELOC_SPARC_TLS_LE_LOX10",
  "BFD_RELOC_SPARC_TLS_DTPMOD32",
  "BFD_RELOC_SPARC_TLS_DTPMOD64",
  "BFD_RELOC_SPARC_TLS_DTPOFF32",
  "BFD_RELOC_SPARC_TLS_DTPOFF64",
  "BFD_RELOC_SPARC_TLS_TPOFF32",
  "BFD_RELOC_SPARC_TLS_TPOFF64",
  "BFD_RELOC_SPU_IMM7",
  "BFD_RELOC_SPU_IMM8",
  "BFD_RELOC_SPU_IMM10",
  "BFD_RELOC_SPU_IMM10W",
  "BFD_RELOC_SPU_IMM16",
  "BFD_RELOC_SPU_IMM16W",
  "BFD_RELOC_SPU_IMM18",
  "BFD_RELOC_SPU_PCREL9a",
  "BFD_RELOC_SPU_PCREL9b",
  "BFD_RELOC_SPU_PCREL16",
  "BFD_RELOC_SPU_LO16",
  "BFD_RELOC_SPU_HI16",
  "BFD_RELOC_SPU_PPU32",
  "BFD_RELOC_SPU_PPU64",
  "BFD_RELOC_ALPHA_GPDISP_HI16",
  "BFD_RELOC_ALPHA_GPDISP_LO16",
  "BFD_RELOC_ALPHA_GPDISP",
  "BFD_RELOC_ALPHA_LITERAL",
  "BFD_RELOC_ALPHA_ELF_LITERAL",
  "BFD_RELOC_ALPHA_LITUSE",
  "BFD_RELOC_ALPHA_HINT",
  "BFD_RELOC_ALPHA_LINKAGE",
  "BFD_RELOC_ALPHA_CODEADDR",
  "BFD_RELOC_ALPHA_GPREL_HI16",
  "BFD_RELOC_ALPHA_GPREL_LO16",
  "BFD_RELOC_ALPHA_BRSGP",
  "BFD_RELOC_ALPHA_TLSGD",
  "BFD_RELOC_ALPHA_TLSLDM",
  "BFD_RELOC_ALPHA_DTPMOD64",
  "BFD_RELOC_ALPHA_GOTDTPREL16",
  "BFD_RELOC_ALPHA_DTPREL64",
  "BFD_RELOC_ALPHA_DTPREL_HI16",
  "BFD_RELOC_ALPHA_DTPREL_LO16",
  "BFD_RELOC_ALPHA_DTPREL16",
  "BFD_RELOC_ALPHA_GOTTPREL16",
  "BFD_RELOC_ALPHA_TPREL64",
  "BFD_RELOC_ALPHA_TPREL_HI16",
  "BFD_RELOC_ALPHA_TPREL_LO16",
  "BFD_RELOC_ALPHA_TPREL16",
  "BFD_RELOC_MIPS_JMP",
  "BFD_RELOC_MIPS16_JMP",
  "BFD_RELOC_MIPS16_GPREL",
  "BFD_RELOC_HI16",
  "BFD_RELOC_HI16_S",
  "BFD_RELOC_LO16",
  "BFD_RELOC_HI16_PCREL",
  "BFD_RELOC_HI16_S_PCREL",
  "BFD_RELOC_LO16_PCREL",
  "BFD_RELOC_MIPS16_HI16",
  "BFD_RELOC_MIPS16_HI16_S",
  "BFD_RELOC_MIPS16_LO16",
  "BFD_RELOC_MIPS_LITERAL",
  "BFD_RELOC_MIPS_GOT16",
  "BFD_RELOC_MIPS_CALL16",
  "BFD_RELOC_MIPS_GOT_HI16",
  "BFD_RELOC_MIPS_GOT_LO16",
  "BFD_RELOC_MIPS_CALL_HI16",
  "BFD_RELOC_MIPS_CALL_LO16",
  "BFD_RELOC_MIPS_SUB",
  "BFD_RELOC_MIPS_GOT_PAGE",
  "BFD_RELOC_MIPS_GOT_OFST",
  "BFD_RELOC_MIPS_GOT_DISP",
  "BFD_RELOC_MIPS_SHIFT5",
  "BFD_RELOC_MIPS_SHIFT6",
  "BFD_RELOC_MIPS_INSERT_A",
  "BFD_RELOC_MIPS_INSERT_B",
  "BFD_RELOC_MIPS_DELETE",
  "BFD_RELOC_MIPS_HIGHEST",
  "BFD_RELOC_MIPS_HIGHER",
  "BFD_RELOC_MIPS_SCN_DISP",
  "BFD_RELOC_MIPS_REL16",
  "BFD_RELOC_MIPS_RELGOT",
  "BFD_RELOC_MIPS_JALR",
  "BFD_RELOC_MIPS_TLS_DTPMOD32",
  "BFD_RELOC_MIPS_TLS_DTPREL32",
  "BFD_RELOC_MIPS_TLS_DTPMOD64",
  "BFD_RELOC_MIPS_TLS_DTPREL64",
  "BFD_RELOC_MIPS_TLS_GD",
  "BFD_RELOC_MIPS_TLS_LDM",
  "BFD_RELOC_MIPS_TLS_DTPREL_HI16",
  "BFD_RELOC_MIPS_TLS_DTPREL_LO16",
  "BFD_RELOC_MIPS_TLS_GOTTPREL",
  "BFD_RELOC_MIPS_TLS_TPREL32",
  "BFD_RELOC_MIPS_TLS_TPREL64",
  "BFD_RELOC_MIPS_TLS_TPREL_HI16",
  "BFD_RELOC_MIPS_TLS_TPREL_LO16",

  "BFD_RELOC_MIPS_COPY",
  "BFD_RELOC_MIPS_JUMP_SLOT",

  "BFD_RELOC_FRV_LABEL16",
  "BFD_RELOC_FRV_LABEL24",
  "BFD_RELOC_FRV_LO16",
  "BFD_RELOC_FRV_HI16",
  "BFD_RELOC_FRV_GPREL12",
  "BFD_RELOC_FRV_GPRELU12",
  "BFD_RELOC_FRV_GPREL32",
  "BFD_RELOC_FRV_GPRELHI",
  "BFD_RELOC_FRV_GPRELLO",
  "BFD_RELOC_FRV_GOT12",
  "BFD_RELOC_FRV_GOTHI",
  "BFD_RELOC_FRV_GOTLO",
  "BFD_RELOC_FRV_FUNCDESC",
  "BFD_RELOC_FRV_FUNCDESC_GOT12",
  "BFD_RELOC_FRV_FUNCDESC_GOTHI",
  "BFD_RELOC_FRV_FUNCDESC_GOTLO",
  "BFD_RELOC_FRV_FUNCDESC_VALUE",
  "BFD_RELOC_FRV_FUNCDESC_GOTOFF12",
  "BFD_RELOC_FRV_FUNCDESC_GOTOFFHI",
  "BFD_RELOC_FRV_FUNCDESC_GOTOFFLO",
  "BFD_RELOC_FRV_GOTOFF12",
  "BFD_RELOC_FRV_GOTOFFHI",
  "BFD_RELOC_FRV_GOTOFFLO",
  "BFD_RELOC_FRV_GETTLSOFF",
  "BFD_RELOC_FRV_TLSDESC_VALUE",
  "BFD_RELOC_FRV_GOTTLSDESC12",
  "BFD_RELOC_FRV_GOTTLSDESCHI",
  "BFD_RELOC_FRV_GOTTLSDESCLO",
  "BFD_RELOC_FRV_TLSMOFF12",
  "BFD_RELOC_FRV_TLSMOFFHI",
  "BFD_RELOC_FRV_TLSMOFFLO",
  "BFD_RELOC_FRV_GOTTLSOFF12",
  "BFD_RELOC_FRV_GOTTLSOFFHI",
  "BFD_RELOC_FRV_GOTTLSOFFLO",
  "BFD_RELOC_FRV_TLSOFF",
  "BFD_RELOC_FRV_TLSDESC_RELAX",
  "BFD_RELOC_FRV_GETTLSOFF_RELAX",
  "BFD_RELOC_FRV_TLSOFF_RELAX",
  "BFD_RELOC_FRV_TLSMOFF",

  "BFD_RELOC_MN10300_GOTOFF24",
  "BFD_RELOC_MN10300_GOT32",
  "BFD_RELOC_MN10300_GOT24",
  "BFD_RELOC_MN10300_GOT16",
  "BFD_RELOC_MN10300_COPY",
  "BFD_RELOC_MN10300_GLOB_DAT",
  "BFD_RELOC_MN10300_JMP_SLOT",
  "BFD_RELOC_MN10300_RELATIVE",

  "BFD_RELOC_386_GOT32",
  "BFD_RELOC_386_PLT32",
  "BFD_RELOC_386_COPY",
  "BFD_RELOC_386_GLOB_DAT",
  "BFD_RELOC_386_JUMP_SLOT",
  "BFD_RELOC_386_RELATIVE",
  "BFD_RELOC_386_GOTOFF",
  "BFD_RELOC_386_GOTPC",
  "BFD_RELOC_386_TLS_TPOFF",
  "BFD_RELOC_386_TLS_IE",
  "BFD_RELOC_386_TLS_GOTIE",
  "BFD_RELOC_386_TLS_LE",
  "BFD_RELOC_386_TLS_GD",
  "BFD_RELOC_386_TLS_LDM",
  "BFD_RELOC_386_TLS_LDO_32",
  "BFD_RELOC_386_TLS_IE_32",
  "BFD_RELOC_386_TLS_LE_32",
  "BFD_RELOC_386_TLS_DTPMOD32",
  "BFD_RELOC_386_TLS_DTPOFF32",
  "BFD_RELOC_386_TLS_TPOFF32",
  "BFD_RELOC_386_TLS_GOTDESC",
  "BFD_RELOC_386_TLS_DESC_CALL",
  "BFD_RELOC_386_TLS_DESC",
  "BFD_RELOC_X86_64_GOT32",
  "BFD_RELOC_X86_64_PLT32",
  "BFD_RELOC_X86_64_COPY",
  "BFD_RELOC_X86_64_GLOB_DAT",
  "BFD_RELOC_X86_64_JUMP_SLOT",
  "BFD_RELOC_X86_64_RELATIVE",
  "BFD_RELOC_X86_64_GOTPCREL",
  "BFD_RELOC_X86_64_32S",
  "BFD_RELOC_X86_64_DTPMOD64",
  "BFD_RELOC_X86_64_DTPOFF64",
  "BFD_RELOC_X86_64_TPOFF64",
  "BFD_RELOC_X86_64_TLSGD",
  "BFD_RELOC_X86_64_TLSLD",
  "BFD_RELOC_X86_64_DTPOFF32",
  "BFD_RELOC_X86_64_GOTTPOFF",
  "BFD_RELOC_X86_64_TPOFF32",
  "BFD_RELOC_X86_64_GOTOFF64",
  "BFD_RELOC_X86_64_GOTPC32",
  "BFD_RELOC_X86_64_GOT64",
  "BFD_RELOC_X86_64_GOTPCREL64",
  "BFD_RELOC_X86_64_GOTPC64",
  "BFD_RELOC_X86_64_GOTPLT64",
  "BFD_RELOC_X86_64_PLTOFF64",
  "BFD_RELOC_X86_64_GOTPC32_TLSDESC",
  "BFD_RELOC_X86_64_TLSDESC_CALL",
  "BFD_RELOC_X86_64_TLSDESC",
  "BFD_RELOC_NS32K_IMM_8",
  "BFD_RELOC_NS32K_IMM_16",
  "BFD_RELOC_NS32K_IMM_32",
  "BFD_RELOC_NS32K_IMM_8_PCREL",
  "BFD_RELOC_NS32K_IMM_16_PCREL",
  "BFD_RELOC_NS32K_IMM_32_PCREL",
  "BFD_RELOC_NS32K_DISP_8",
  "BFD_RELOC_NS32K_DISP_16",
  "BFD_RELOC_NS32K_DISP_32",
  "BFD_RELOC_NS32K_DISP_8_PCREL",
  "BFD_RELOC_NS32K_DISP_16_PCREL",
  "BFD_RELOC_NS32K_DISP_32_PCREL",
  "BFD_RELOC_PDP11_DISP_8_PCREL",
  "BFD_RELOC_PDP11_DISP_6_PCREL",
  "BFD_RELOC_PJ_CODE_HI16",
  "BFD_RELOC_PJ_CODE_LO16",
  "BFD_RELOC_PJ_CODE_DIR16",
  "BFD_RELOC_PJ_CODE_DIR32",
  "BFD_RELOC_PJ_CODE_REL16",
  "BFD_RELOC_PJ_CODE_REL32",
  "BFD_RELOC_PPC_B26",
  "BFD_RELOC_PPC_BA26",
  "BFD_RELOC_PPC_TOC16",
  "BFD_RELOC_PPC_B16",
  "BFD_RELOC_PPC_B16_BRTAKEN",
  "BFD_RELOC_PPC_B16_BRNTAKEN",
  "BFD_RELOC_PPC_BA16",
  "BFD_RELOC_PPC_BA16_BRTAKEN",
  "BFD_RELOC_PPC_BA16_BRNTAKEN",
  "BFD_RELOC_PPC_COPY",
  "BFD_RELOC_PPC_GLOB_DAT",
  "BFD_RELOC_PPC_JMP_SLOT",
  "BFD_RELOC_PPC_RELATIVE",
  "BFD_RELOC_PPC_LOCAL24PC",
  "BFD_RELOC_PPC_EMB_NADDR32",
  "BFD_RELOC_PPC_EMB_NADDR16",
  "BFD_RELOC_PPC_EMB_NADDR16_LO",
  "BFD_RELOC_PPC_EMB_NADDR16_HI",
  "BFD_RELOC_PPC_EMB_NADDR16_HA",
  "BFD_RELOC_PPC_EMB_SDAI16",
  "BFD_RELOC_PPC_EMB_SDA2I16",
  "BFD_RELOC_PPC_EMB_SDA2REL",
  "BFD_RELOC_PPC_EMB_SDA21",
  "BFD_RELOC_PPC_EMB_MRKREF",
  "BFD_RELOC_PPC_EMB_RELSEC16",
  "BFD_RELOC_PPC_EMB_RELST_LO",
  "BFD_RELOC_PPC_EMB_RELST_HI",
  "BFD_RELOC_PPC_EMB_RELST_HA",
  "BFD_RELOC_PPC_EMB_BIT_FLD",
  "BFD_RELOC_PPC_EMB_RELSDA",
  "BFD_RELOC_PPC64_HIGHER",
  "BFD_RELOC_PPC64_HIGHER_S",
  "BFD_RELOC_PPC64_HIGHEST",
  "BFD_RELOC_PPC64_HIGHEST_S",
  "BFD_RELOC_PPC64_TOC16_LO",
  "BFD_RELOC_PPC64_TOC16_HI",
  "BFD_RELOC_PPC64_TOC16_HA",
  "BFD_RELOC_PPC64_TOC",
  "BFD_RELOC_PPC64_PLTGOT16",
  "BFD_RELOC_PPC64_PLTGOT16_LO",
  "BFD_RELOC_PPC64_PLTGOT16_HI",
  "BFD_RELOC_PPC64_PLTGOT16_HA",
  "BFD_RELOC_PPC64_ADDR16_DS",
  "BFD_RELOC_PPC64_ADDR16_LO_DS",
  "BFD_RELOC_PPC64_GOT16_DS",
  "BFD_RELOC_PPC64_GOT16_LO_DS",
  "BFD_RELOC_PPC64_PLT16_LO_DS",
  "BFD_RELOC_PPC64_SECTOFF_DS",
  "BFD_RELOC_PPC64_SECTOFF_LO_DS",
  "BFD_RELOC_PPC64_TOC16_DS",
  "BFD_RELOC_PPC64_TOC16_LO_DS",
  "BFD_RELOC_PPC64_PLTGOT16_DS",
  "BFD_RELOC_PPC64_PLTGOT16_LO_DS",
  "BFD_RELOC_PPC_TLS",
  "BFD_RELOC_PPC_TLSGD",
  "BFD_RELOC_PPC_TLSLD",
  "BFD_RELOC_PPC_DTPMOD",
  "BFD_RELOC_PPC_TPREL16",
  "BFD_RELOC_PPC_TPREL16_LO",
  "BFD_RELOC_PPC_TPREL16_HI",
  "BFD_RELOC_PPC_TPREL16_HA",
  "BFD_RELOC_PPC_TPREL",
  "BFD_RELOC_PPC_DTPREL16",
  "BFD_RELOC_PPC_DTPREL16_LO",
  "BFD_RELOC_PPC_DTPREL16_HI",
  "BFD_RELOC_PPC_DTPREL16_HA",
  "BFD_RELOC_PPC_DTPREL",
  "BFD_RELOC_PPC_GOT_TLSGD16",
  "BFD_RELOC_PPC_GOT_TLSGD16_LO",
  "BFD_RELOC_PPC_GOT_TLSGD16_HI",
  "BFD_RELOC_PPC_GOT_TLSGD16_HA",
  "BFD_RELOC_PPC_GOT_TLSLD16",
  "BFD_RELOC_PPC_GOT_TLSLD16_LO",
  "BFD_RELOC_PPC_GOT_TLSLD16_HI",
  "BFD_RELOC_PPC_GOT_TLSLD16_HA",
  "BFD_RELOC_PPC_GOT_TPREL16",
  "BFD_RELOC_PPC_GOT_TPREL16_LO",
  "BFD_RELOC_PPC_GOT_TPREL16_HI",
  "BFD_RELOC_PPC_GOT_TPREL16_HA",
  "BFD_RELOC_PPC_GOT_DTPREL16",
  "BFD_RELOC_PPC_GOT_DTPREL16_LO",
  "BFD_RELOC_PPC_GOT_DTPREL16_HI",
  "BFD_RELOC_PPC_GOT_DTPREL16_HA",
  "BFD_RELOC_PPC64_TPREL16_DS",
  "BFD_RELOC_PPC64_TPREL16_LO_DS",
  "BFD_RELOC_PPC64_TPREL16_HIGHER",
  "BFD_RELOC_PPC64_TPREL16_HIGHERA",
  "BFD_RELOC_PPC64_TPREL16_HIGHEST",
  "BFD_RELOC_PPC64_TPREL16_HIGHESTA",
  "BFD_RELOC_PPC64_DTPREL16_DS",
  "BFD_RELOC_PPC64_DTPREL16_LO_DS",
  "BFD_RELOC_PPC64_DTPREL16_HIGHER",
  "BFD_RELOC_PPC64_DTPREL16_HIGHERA",
  "BFD_RELOC_PPC64_DTPREL16_HIGHEST",
  "BFD_RELOC_PPC64_DTPREL16_HIGHESTA",
  "BFD_RELOC_I370_D12",
  "BFD_RELOC_CTOR",
  "BFD_RELOC_ARM_PCREL_BRANCH",
  "BFD_RELOC_ARM_PCREL_BLX",
  "BFD_RELOC_THUMB_PCREL_BLX",
  "BFD_RELOC_ARM_PCREL_CALL",
  "BFD_RELOC_ARM_PCREL_JUMP",
  "BFD_RELOC_THUMB_PCREL_BRANCH7",
  "BFD_RELOC_THUMB_PCREL_BRANCH9",
  "BFD_RELOC_THUMB_PCREL_BRANCH12",
  "BFD_RELOC_THUMB_PCREL_BRANCH20",
  "BFD_RELOC_THUMB_PCREL_BRANCH23",
  "BFD_RELOC_THUMB_PCREL_BRANCH25",
  "BFD_RELOC_ARM_OFFSET_IMM",
  "BFD_RELOC_ARM_THUMB_OFFSET",
  "BFD_RELOC_ARM_TARGET1",
  "BFD_RELOC_ARM_ROSEGREL32",
  "BFD_RELOC_ARM_SBREL32",
  "BFD_RELOC_ARM_TARGET2",
  "BFD_RELOC_ARM_PREL31",
  "BFD_RELOC_ARM_MOVW",
  "BFD_RELOC_ARM_MOVT",
  "BFD_RELOC_ARM_MOVW_PCREL",
  "BFD_RELOC_ARM_MOVT_PCREL",
  "BFD_RELOC_ARM_THUMB_MOVW",
  "BFD_RELOC_ARM_THUMB_MOVT",
  "BFD_RELOC_ARM_THUMB_MOVW_PCREL",
  "BFD_RELOC_ARM_THUMB_MOVT_PCREL",
  "BFD_RELOC_ARM_JUMP_SLOT",
  "BFD_RELOC_ARM_GLOB_DAT",
  "BFD_RELOC_ARM_GOT32",
  "BFD_RELOC_ARM_PLT32",
  "BFD_RELOC_ARM_RELATIVE",
  "BFD_RELOC_ARM_GOTOFF",
  "BFD_RELOC_ARM_GOTPC",
  "BFD_RELOC_ARM_TLS_GD32",
  "BFD_RELOC_ARM_TLS_LDO32",
  "BFD_RELOC_ARM_TLS_LDM32",
  "BFD_RELOC_ARM_TLS_DTPOFF32",
  "BFD_RELOC_ARM_TLS_DTPMOD32",
  "BFD_RELOC_ARM_TLS_TPOFF32",
  "BFD_RELOC_ARM_TLS_IE32",
  "BFD_RELOC_ARM_TLS_LE32",
  "BFD_RELOC_ARM_ALU_PC_G0_NC",
  "BFD_RELOC_ARM_ALU_PC_G0",
  "BFD_RELOC_ARM_ALU_PC_G1_NC",
  "BFD_RELOC_ARM_ALU_PC_G1",
  "BFD_RELOC_ARM_ALU_PC_G2",
  "BFD_RELOC_ARM_LDR_PC_G0",
  "BFD_RELOC_ARM_LDR_PC_G1",
  "BFD_RELOC_ARM_LDR_PC_G2",
  "BFD_RELOC_ARM_LDRS_PC_G0",
  "BFD_RELOC_ARM_LDRS_PC_G1",
  "BFD_RELOC_ARM_LDRS_PC_G2",
  "BFD_RELOC_ARM_LDC_PC_G0",
  "BFD_RELOC_ARM_LDC_PC_G1",
  "BFD_RELOC_ARM_LDC_PC_G2",
  "BFD_RELOC_ARM_ALU_SB_G0_NC",
  "BFD_RELOC_ARM_ALU_SB_G0",
  "BFD_RELOC_ARM_ALU_SB_G1_NC",
  "BFD_RELOC_ARM_ALU_SB_G1",
  "BFD_RELOC_ARM_ALU_SB_G2",
  "BFD_RELOC_ARM_LDR_SB_G0",
  "BFD_RELOC_ARM_LDR_SB_G1",
  "BFD_RELOC_ARM_LDR_SB_G2",
  "BFD_RELOC_ARM_LDRS_SB_G0",
  "BFD_RELOC_ARM_LDRS_SB_G1",
  "BFD_RELOC_ARM_LDRS_SB_G2",
  "BFD_RELOC_ARM_LDC_SB_G0",
  "BFD_RELOC_ARM_LDC_SB_G1",
  "BFD_RELOC_ARM_LDC_SB_G2",
  "BFD_RELOC_ARM_IMMEDIATE",
  "BFD_RELOC_ARM_ADRL_IMMEDIATE",
  "BFD_RELOC_ARM_T32_IMMEDIATE",
  "BFD_RELOC_ARM_T32_ADD_IMM",
  "BFD_RELOC_ARM_T32_IMM12",
  "BFD_RELOC_ARM_T32_ADD_PC12",
  "BFD_RELOC_ARM_SHIFT_IMM",
  "BFD_RELOC_ARM_SMC",
  "BFD_RELOC_ARM_SWI",
  "BFD_RELOC_ARM_MULTI",
  "BFD_RELOC_ARM_CP_OFF_IMM",
  "BFD_RELOC_ARM_CP_OFF_IMM_S2",
  "BFD_RELOC_ARM_T32_CP_OFF_IMM",
  "BFD_RELOC_ARM_T32_CP_OFF_IMM_S2",
  "BFD_RELOC_ARM_ADR_IMM",
  "BFD_RELOC_ARM_LDR_IMM",
  "BFD_RELOC_ARM_LITERAL",
  "BFD_RELOC_ARM_IN_POOL",
  "BFD_RELOC_ARM_OFFSET_IMM8",
  "BFD_RELOC_ARM_T32_OFFSET_U8",
  "BFD_RELOC_ARM_T32_OFFSET_IMM",
  "BFD_RELOC_ARM_HWLITERAL",
  "BFD_RELOC_ARM_THUMB_ADD",
  "BFD_RELOC_ARM_THUMB_IMM",
  "BFD_RELOC_ARM_THUMB_SHIFT",
  "BFD_RELOC_SH_PCDISP8BY2",
  "BFD_RELOC_SH_PCDISP12BY2",
  "BFD_RELOC_SH_IMM3",
  "BFD_RELOC_SH_IMM3U",
  "BFD_RELOC_SH_DISP12",
  "BFD_RELOC_SH_DISP12BY2",
  "BFD_RELOC_SH_DISP12BY4",
  "BFD_RELOC_SH_DISP12BY8",
  "BFD_RELOC_SH_DISP20",
  "BFD_RELOC_SH_DISP20BY8",
  "BFD_RELOC_SH_IMM4",
  "BFD_RELOC_SH_IMM4BY2",
  "BFD_RELOC_SH_IMM4BY4",
  "BFD_RELOC_SH_IMM8",
  "BFD_RELOC_SH_IMM8BY2",
  "BFD_RELOC_SH_IMM8BY4",
  "BFD_RELOC_SH_PCRELIMM8BY2",
  "BFD_RELOC_SH_PCRELIMM8BY4",
  "BFD_RELOC_SH_SWITCH16",
  "BFD_RELOC_SH_SWITCH32",
  "BFD_RELOC_SH_USES",
  "BFD_RELOC_SH_COUNT",
  "BFD_RELOC_SH_ALIGN",
  "BFD_RELOC_SH_CODE",
  "BFD_RELOC_SH_DATA",
  "BFD_RELOC_SH_LABEL",
  "BFD_RELOC_SH_LOOP_START",
  "BFD_RELOC_SH_LOOP_END",
  "BFD_RELOC_SH_COPY",
  "BFD_RELOC_SH_GLOB_DAT",
  "BFD_RELOC_SH_JMP_SLOT",
  "BFD_RELOC_SH_RELATIVE",
  "BFD_RELOC_SH_GOTPC",
  "BFD_RELOC_SH_GOT_LOW16",
  "BFD_RELOC_SH_GOT_MEDLOW16",
  "BFD_RELOC_SH_GOT_MEDHI16",
  "BFD_RELOC_SH_GOT_HI16",
  "BFD_RELOC_SH_GOTPLT_LOW16",
  "BFD_RELOC_SH_GOTPLT_MEDLOW16",
  "BFD_RELOC_SH_GOTPLT_MEDHI16",
  "BFD_RELOC_SH_GOTPLT_HI16",
  "BFD_RELOC_SH_PLT_LOW16",
  "BFD_RELOC_SH_PLT_MEDLOW16",
  "BFD_RELOC_SH_PLT_MEDHI16",
  "BFD_RELOC_SH_PLT_HI16",
  "BFD_RELOC_SH_GOTOFF_LOW16",
  "BFD_RELOC_SH_GOTOFF_MEDLOW16",
  "BFD_RELOC_SH_GOTOFF_MEDHI16",
  "BFD_RELOC_SH_GOTOFF_HI16",
  "BFD_RELOC_SH_GOTPC_LOW16",
  "BFD_RELOC_SH_GOTPC_MEDLOW16",
  "BFD_RELOC_SH_GOTPC_MEDHI16",
  "BFD_RELOC_SH_GOTPC_HI16",
  "BFD_RELOC_SH_COPY64",
  "BFD_RELOC_SH_GLOB_DAT64",
  "BFD_RELOC_SH_JMP_SLOT64",
  "BFD_RELOC_SH_RELATIVE64",
  "BFD_RELOC_SH_GOT10BY4",
  "BFD_RELOC_SH_GOT10BY8",
  "BFD_RELOC_SH_GOTPLT10BY4",
  "BFD_RELOC_SH_GOTPLT10BY8",
  "BFD_RELOC_SH_GOTPLT32",
  "BFD_RELOC_SH_SHMEDIA_CODE",
  "BFD_RELOC_SH_IMMU5",
  "BFD_RELOC_SH_IMMS6",
  "BFD_RELOC_SH_IMMS6BY32",
  "BFD_RELOC_SH_IMMU6",
  "BFD_RELOC_SH_IMMS10",
  "BFD_RELOC_SH_IMMS10BY2",
  "BFD_RELOC_SH_IMMS10BY4",
  "BFD_RELOC_SH_IMMS10BY8",
  "BFD_RELOC_SH_IMMS16",
  "BFD_RELOC_SH_IMMU16",
  "BFD_RELOC_SH_IMM_LOW16",
  "BFD_RELOC_SH_IMM_LOW16_PCREL",
  "BFD_RELOC_SH_IMM_MEDLOW16",
  "BFD_RELOC_SH_IMM_MEDLOW16_PCREL",
  "BFD_RELOC_SH_IMM_MEDHI16",
  "BFD_RELOC_SH_IMM_MEDHI16_PCREL",
  "BFD_RELOC_SH_IMM_HI16",
  "BFD_RELOC_SH_IMM_HI16_PCREL",
  "BFD_RELOC_SH_PT_16",
  "BFD_RELOC_SH_TLS_GD_32",
  "BFD_RELOC_SH_TLS_LD_32",
  "BFD_RELOC_SH_TLS_LDO_32",
  "BFD_RELOC_SH_TLS_IE_32",
  "BFD_RELOC_SH_TLS_LE_32",
  "BFD_RELOC_SH_TLS_DTPMOD32",
  "BFD_RELOC_SH_TLS_DTPOFF32",
  "BFD_RELOC_SH_TLS_TPOFF32",
  "BFD_RELOC_ARC_B22_PCREL",
  "BFD_RELOC_ARC_B26",
  "BFD_RELOC_BFIN_16_IMM",
  "BFD_RELOC_BFIN_16_HIGH",
  "BFD_RELOC_BFIN_4_PCREL",
  "BFD_RELOC_BFIN_5_PCREL",
  "BFD_RELOC_BFIN_16_LOW",
  "BFD_RELOC_BFIN_10_PCREL",
  "BFD_RELOC_BFIN_11_PCREL",
  "BFD_RELOC_BFIN_12_PCREL_JUMP",
  "BFD_RELOC_BFIN_12_PCREL_JUMP_S",
  "BFD_RELOC_BFIN_24_PCREL_CALL_X",
  "BFD_RELOC_BFIN_24_PCREL_JUMP_L",
  "BFD_RELOC_BFIN_GOT17M4",
  "BFD_RELOC_BFIN_GOTHI",
  "BFD_RELOC_BFIN_GOTLO",
  "BFD_RELOC_BFIN_FUNCDESC",
  "BFD_RELOC_BFIN_FUNCDESC_GOT17M4",
  "BFD_RELOC_BFIN_FUNCDESC_GOTHI",
  "BFD_RELOC_BFIN_FUNCDESC_GOTLO",
  "BFD_RELOC_BFIN_FUNCDESC_VALUE",
  "BFD_RELOC_BFIN_FUNCDESC_GOTOFF17M4",
  "BFD_RELOC_BFIN_FUNCDESC_GOTOFFHI",
  "BFD_RELOC_BFIN_FUNCDESC_GOTOFFLO",
  "BFD_RELOC_BFIN_GOTOFF17M4",
  "BFD_RELOC_BFIN_GOTOFFHI",
  "BFD_RELOC_BFIN_GOTOFFLO",
  "BFD_RELOC_BFIN_GOT",
  "BFD_RELOC_BFIN_PLTPC",
  "BFD_ARELOC_BFIN_PUSH",
  "BFD_ARELOC_BFIN_CONST",
  "BFD_ARELOC_BFIN_ADD",
  "BFD_ARELOC_BFIN_SUB",
  "BFD_ARELOC_BFIN_MULT",
  "BFD_ARELOC_BFIN_DIV",
  "BFD_ARELOC_BFIN_MOD",
  "BFD_ARELOC_BFIN_LSHIFT",
  "BFD_ARELOC_BFIN_RSHIFT",
  "BFD_ARELOC_BFIN_AND",
  "BFD_ARELOC_BFIN_OR",
  "BFD_ARELOC_BFIN_XOR",
  "BFD_ARELOC_BFIN_LAND",
  "BFD_ARELOC_BFIN_LOR",
  "BFD_ARELOC_BFIN_LEN",
  "BFD_ARELOC_BFIN_NEG",
  "BFD_ARELOC_BFIN_COMP",
  "BFD_ARELOC_BFIN_PAGE",
  "BFD_ARELOC_BFIN_HWPAGE",
  "BFD_ARELOC_BFIN_ADDR",
  "BFD_RELOC_D10V_10_PCREL_R",
  "BFD_RELOC_D10V_10_PCREL_L",
  "BFD_RELOC_D10V_18",
  "BFD_RELOC_D10V_18_PCREL",
  "BFD_RELOC_D30V_6",
  "BFD_RELOC_D30V_9_PCREL",
  "BFD_RELOC_D30V_9_PCREL_R",
  "BFD_RELOC_D30V_15",
  "BFD_RELOC_D30V_15_PCREL",
  "BFD_RELOC_D30V_15_PCREL_R",
  "BFD_RELOC_D30V_21",
  "BFD_RELOC_D30V_21_PCREL",
  "BFD_RELOC_D30V_21_PCREL_R",
  "BFD_RELOC_D30V_32",
  "BFD_RELOC_D30V_32_PCREL",
  "BFD_RELOC_DLX_HI16_S",
  "BFD_RELOC_DLX_LO16",
  "BFD_RELOC_DLX_JMP26",
  "BFD_RELOC_M32C_HI8",
  "BFD_RELOC_M32C_RL_JUMP",
  "BFD_RELOC_M32C_RL_1ADDR",
  "BFD_RELOC_M32C_RL_2ADDR",
  "BFD_RELOC_M32R_24",
  "BFD_RELOC_M32R_10_PCREL",
  "BFD_RELOC_M32R_18_PCREL",
  "BFD_RELOC_M32R_26_PCREL",
  "BFD_RELOC_M32R_HI16_ULO",
  "BFD_RELOC_M32R_HI16_SLO",
  "BFD_RELOC_M32R_LO16",
  "BFD_RELOC_M32R_SDA16",
  "BFD_RELOC_M32R_GOT24",
  "BFD_RELOC_M32R_26_PLTREL",
  "BFD_RELOC_M32R_COPY",
  "BFD_RELOC_M32R_GLOB_DAT",
  "BFD_RELOC_M32R_JMP_SLOT",
  "BFD_RELOC_M32R_RELATIVE",
  "BFD_RELOC_M32R_GOTOFF",
  "BFD_RELOC_M32R_GOTOFF_HI_ULO",
  "BFD_RELOC_M32R_GOTOFF_HI_SLO",
  "BFD_RELOC_M32R_GOTOFF_LO",
  "BFD_RELOC_M32R_GOTPC24",
  "BFD_RELOC_M32R_GOT16_HI_ULO",
  "BFD_RELOC_M32R_GOT16_HI_SLO",
  "BFD_RELOC_M32R_GOT16_LO",
  "BFD_RELOC_M32R_GOTPC_HI_ULO",
  "BFD_RELOC_M32R_GOTPC_HI_SLO",
  "BFD_RELOC_M32R_GOTPC_LO",
  "BFD_RELOC_V850_9_PCREL",
  "BFD_RELOC_V850_22_PCREL",
  "BFD_RELOC_V850_SDA_16_16_OFFSET",
  "BFD_RELOC_V850_SDA_15_16_OFFSET",
  "BFD_RELOC_V850_ZDA_16_16_OFFSET",
  "BFD_RELOC_V850_ZDA_15_16_OFFSET",
  "BFD_RELOC_V850_TDA_6_8_OFFSET",
  "BFD_RELOC_V850_TDA_7_8_OFFSET",
  "BFD_RELOC_V850_TDA_7_7_OFFSET",
  "BFD_RELOC_V850_TDA_16_16_OFFSET",
  "BFD_RELOC_V850_TDA_4_5_OFFSET",
  "BFD_RELOC_V850_TDA_4_4_OFFSET",
  "BFD_RELOC_V850_SDA_16_16_SPLIT_OFFSET",
  "BFD_RELOC_V850_ZDA_16_16_SPLIT_OFFSET",
  "BFD_RELOC_V850_CALLT_6_7_OFFSET",
  "BFD_RELOC_V850_CALLT_16_16_OFFSET",
  "BFD_RELOC_V850_LONGCALL",
  "BFD_RELOC_V850_LONGJUMP",
  "BFD_RELOC_V850_ALIGN",
  "BFD_RELOC_V850_LO16_SPLIT_OFFSET",
  "BFD_RELOC_MN10300_32_PCREL",
  "BFD_RELOC_MN10300_16_PCREL",
  "BFD_RELOC_TIC30_LDP",
  "BFD_RELOC_TIC54X_PARTLS7",
  "BFD_RELOC_TIC54X_PARTMS9",
  "BFD_RELOC_TIC54X_23",
  "BFD_RELOC_TIC54X_16_OF_23",
  "BFD_RELOC_TIC54X_MS7_OF_23",
  "BFD_RELOC_FR30_48",
  "BFD_RELOC_FR30_20",
  "BFD_RELOC_FR30_6_IN_4",
  "BFD_RELOC_FR30_8_IN_8",
  "BFD_RELOC_FR30_9_IN_8",
  "BFD_RELOC_FR30_10_IN_8",
  "BFD_RELOC_FR30_9_PCREL",
  "BFD_RELOC_FR30_12_PCREL",
  "BFD_RELOC_MCORE_PCREL_IMM8BY4",
  "BFD_RELOC_MCORE_PCREL_IMM11BY2",
  "BFD_RELOC_MCORE_PCREL_IMM4BY2",
  "BFD_RELOC_MCORE_PCREL_32",
  "BFD_RELOC_MCORE_PCREL_JSR_IMM11BY2",
  "BFD_RELOC_MCORE_RVA",
  "BFD_RELOC_MEP_8",
  "BFD_RELOC_MEP_16",
  "BFD_RELOC_MEP_32",
  "BFD_RELOC_MEP_PCREL8A2",
  "BFD_RELOC_MEP_PCREL12A2",
  "BFD_RELOC_MEP_PCREL17A2",
  "BFD_RELOC_MEP_PCREL24A2",
  "BFD_RELOC_MEP_PCABS24A2",
  "BFD_RELOC_MEP_LOW16",
  "BFD_RELOC_MEP_HI16U",
  "BFD_RELOC_MEP_HI16S",
  "BFD_RELOC_MEP_GPREL",
  "BFD_RELOC_MEP_TPREL",
  "BFD_RELOC_MEP_TPREL7",
  "BFD_RELOC_MEP_TPREL7A2",
  "BFD_RELOC_MEP_TPREL7A4",
  "BFD_RELOC_MEP_UIMM24",
  "BFD_RELOC_MEP_ADDR24A4",
  "BFD_RELOC_MEP_GNU_VTINHERIT",
  "BFD_RELOC_MEP_GNU_VTENTRY",

  "BFD_RELOC_MMIX_GETA",
  "BFD_RELOC_MMIX_GETA_1",
  "BFD_RELOC_MMIX_GETA_2",
  "BFD_RELOC_MMIX_GETA_3",
  "BFD_RELOC_MMIX_CBRANCH",
  "BFD_RELOC_MMIX_CBRANCH_J",
  "BFD_RELOC_MMIX_CBRANCH_1",
  "BFD_RELOC_MMIX_CBRANCH_2",
  "BFD_RELOC_MMIX_CBRANCH_3",
  "BFD_RELOC_MMIX_PUSHJ",
  "BFD_RELOC_MMIX_PUSHJ_1",
  "BFD_RELOC_MMIX_PUSHJ_2",
  "BFD_RELOC_MMIX_PUSHJ_3",
  "BFD_RELOC_MMIX_PUSHJ_STUBBABLE",
  "BFD_RELOC_MMIX_JMP",
  "BFD_RELOC_MMIX_JMP_1",
  "BFD_RELOC_MMIX_JMP_2",
  "BFD_RELOC_MMIX_JMP_3",
  "BFD_RELOC_MMIX_ADDR19",
  "BFD_RELOC_MMIX_ADDR27",
  "BFD_RELOC_MMIX_REG_OR_BYTE",
  "BFD_RELOC_MMIX_REG",
  "BFD_RELOC_MMIX_BASE_PLUS_OFFSET",
  "BFD_RELOC_MMIX_LOCAL",
  "BFD_RELOC_AVR_7_PCREL",
  "BFD_RELOC_AVR_13_PCREL",
  "BFD_RELOC_AVR_16_PM",
  "BFD_RELOC_AVR_LO8_LDI",
  "BFD_RELOC_AVR_HI8_LDI",
  "BFD_RELOC_AVR_HH8_LDI",
  "BFD_RELOC_AVR_MS8_LDI",
  "BFD_RELOC_AVR_LO8_LDI_NEG",
  "BFD_RELOC_AVR_HI8_LDI_NEG",
  "BFD_RELOC_AVR_HH8_LDI_NEG",
  "BFD_RELOC_AVR_MS8_LDI_NEG",
  "BFD_RELOC_AVR_LO8_LDI_PM",
  "BFD_RELOC_AVR_LO8_LDI_GS",
  "BFD_RELOC_AVR_HI8_LDI_PM",
  "BFD_RELOC_AVR_HI8_LDI_GS",
  "BFD_RELOC_AVR_HH8_LDI_PM",
  "BFD_RELOC_AVR_LO8_LDI_PM_NEG",
  "BFD_RELOC_AVR_HI8_LDI_PM_NEG",
  "BFD_RELOC_AVR_HH8_LDI_PM_NEG",
  "BFD_RELOC_AVR_CALL",
  "BFD_RELOC_AVR_LDI",
  "BFD_RELOC_AVR_6",
  "BFD_RELOC_AVR_6_ADIW",
  "BFD_RELOC_390_12",
  "BFD_RELOC_390_GOT12",
  "BFD_RELOC_390_PLT32",
  "BFD_RELOC_390_COPY",
  "BFD_RELOC_390_GLOB_DAT",
  "BFD_RELOC_390_JMP_SLOT",
  "BFD_RELOC_390_RELATIVE",
  "BFD_RELOC_390_GOTPC",
  "BFD_RELOC_390_GOT16",
  "BFD_RELOC_390_PC16DBL",
  "BFD_RELOC_390_PLT16DBL",
  "BFD_RELOC_390_PC32DBL",
  "BFD_RELOC_390_PLT32DBL",
  "BFD_RELOC_390_GOTPCDBL",
  "BFD_RELOC_390_GOT64",
  "BFD_RELOC_390_PLT64",
  "BFD_RELOC_390_GOTENT",
  "BFD_RELOC_390_GOTOFF64",
  "BFD_RELOC_390_GOTPLT12",
  "BFD_RELOC_390_GOTPLT16",
  "BFD_RELOC_390_GOTPLT32",
  "BFD_RELOC_390_GOTPLT64",
  "BFD_RELOC_390_GOTPLTENT",
  "BFD_RELOC_390_PLTOFF16",
  "BFD_RELOC_390_PLTOFF32",
  "BFD_RELOC_390_PLTOFF64",
  "BFD_RELOC_390_TLS_LOAD",
  "BFD_RELOC_390_TLS_GDCALL",
  "BFD_RELOC_390_TLS_LDCALL",
  "BFD_RELOC_390_TLS_GD32",
  "BFD_RELOC_390_TLS_GD64",
  "BFD_RELOC_390_TLS_GOTIE12",
  "BFD_RELOC_390_TLS_GOTIE32",
  "BFD_RELOC_390_TLS_GOTIE64",
  "BFD_RELOC_390_TLS_LDM32",
  "BFD_RELOC_390_TLS_LDM64",
  "BFD_RELOC_390_TLS_IE32",
  "BFD_RELOC_390_TLS_IE64",
  "BFD_RELOC_390_TLS_IEENT",
  "BFD_RELOC_390_TLS_LE32",
  "BFD_RELOC_390_TLS_LE64",
  "BFD_RELOC_390_TLS_LDO32",
  "BFD_RELOC_390_TLS_LDO64",
  "BFD_RELOC_390_TLS_DTPMOD",
  "BFD_RELOC_390_TLS_DTPOFF",
  "BFD_RELOC_390_TLS_TPOFF",
  "BFD_RELOC_390_20",
  "BFD_RELOC_390_GOT20",
  "BFD_RELOC_390_GOTPLT20",
  "BFD_RELOC_390_TLS_GOTIE20",
  "BFD_RELOC_SCORE_DUMMY1",
  "BFD_RELOC_SCORE_GPREL15",
  "BFD_RELOC_SCORE_DUMMY2",
  "BFD_RELOC_SCORE_JMP",
  "BFD_RELOC_SCORE_BRANCH",
  "BFD_RELOC_SCORE16_JMP",
  "BFD_RELOC_SCORE16_BRANCH",
  "BFD_RELOC_SCORE_GOT15",
  "BFD_RELOC_SCORE_GOT_LO16",
  "BFD_RELOC_SCORE_CALL15",
  "BFD_RELOC_SCORE_DUMMY_HI16",
  "BFD_RELOC_IP2K_FR9",
  "BFD_RELOC_IP2K_BANK",
  "BFD_RELOC_IP2K_ADDR16CJP",
  "BFD_RELOC_IP2K_PAGE3",
  "BFD_RELOC_IP2K_LO8DATA",
  "BFD_RELOC_IP2K_HI8DATA",
  "BFD_RELOC_IP2K_EX8DATA",
  "BFD_RELOC_IP2K_LO8INSN",
  "BFD_RELOC_IP2K_HI8INSN",
  "BFD_RELOC_IP2K_PC_SKIP",
  "BFD_RELOC_IP2K_TEXT",
  "BFD_RELOC_IP2K_FR_OFFSET",
  "BFD_RELOC_VPE4KMATH_DATA",
  "BFD_RELOC_VPE4KMATH_INSN",
  "BFD_RELOC_VTABLE_INHERIT",
  "BFD_RELOC_VTABLE_ENTRY",
  "BFD_RELOC_IA64_IMM14",
  "BFD_RELOC_IA64_IMM22",
  "BFD_RELOC_IA64_IMM64",
  "BFD_RELOC_IA64_DIR32MSB",
  "BFD_RELOC_IA64_DIR32LSB",
  "BFD_RELOC_IA64_DIR64MSB",
  "BFD_RELOC_IA64_DIR64LSB",
  "BFD_RELOC_IA64_GPREL22",
  "BFD_RELOC_IA64_GPREL64I",
  "BFD_RELOC_IA64_GPREL32MSB",
  "BFD_RELOC_IA64_GPREL32LSB",
  "BFD_RELOC_IA64_GPREL64MSB",
  "BFD_RELOC_IA64_GPREL64LSB",
  "BFD_RELOC_IA64_LTOFF22",
  "BFD_RELOC_IA64_LTOFF64I",
  "BFD_RELOC_IA64_PLTOFF22",
  "BFD_RELOC_IA64_PLTOFF64I",
  "BFD_RELOC_IA64_PLTOFF64MSB",
  "BFD_RELOC_IA64_PLTOFF64LSB",
  "BFD_RELOC_IA64_FPTR64I",
  "BFD_RELOC_IA64_FPTR32MSB",
  "BFD_RELOC_IA64_FPTR32LSB",
  "BFD_RELOC_IA64_FPTR64MSB",
  "BFD_RELOC_IA64_FPTR64LSB",
  "BFD_RELOC_IA64_PCREL21B",
  "BFD_RELOC_IA64_PCREL21BI",
  "BFD_RELOC_IA64_PCREL21M",
  "BFD_RELOC_IA64_PCREL21F",
  "BFD_RELOC_IA64_PCREL22",
  "BFD_RELOC_IA64_PCREL60B",
  "BFD_RELOC_IA64_PCREL64I",
  "BFD_RELOC_IA64_PCREL32MSB",
  "BFD_RELOC_IA64_PCREL32LSB",
  "BFD_RELOC_IA64_PCREL64MSB",
  "BFD_RELOC_IA64_PCREL64LSB",
  "BFD_RELOC_IA64_LTOFF_FPTR22",
  "BFD_RELOC_IA64_LTOFF_FPTR64I",
  "BFD_RELOC_IA64_LTOFF_FPTR32MSB",
  "BFD_RELOC_IA64_LTOFF_FPTR32LSB",
  "BFD_RELOC_IA64_LTOFF_FPTR64MSB",
  "BFD_RELOC_IA64_LTOFF_FPTR64LSB",
  "BFD_RELOC_IA64_SEGREL32MSB",
  "BFD_RELOC_IA64_SEGREL32LSB",
  "BFD_RELOC_IA64_SEGREL64MSB",
  "BFD_RELOC_IA64_SEGREL64LSB",
  "BFD_RELOC_IA64_SECREL32MSB",
  "BFD_RELOC_IA64_SECREL32LSB",
  "BFD_RELOC_IA64_SECREL64MSB",
  "BFD_RELOC_IA64_SECREL64LSB",
  "BFD_RELOC_IA64_REL32MSB",
  "BFD_RELOC_IA64_REL32LSB",
  "BFD_RELOC_IA64_REL64MSB",
  "BFD_RELOC_IA64_REL64LSB",
  "BFD_RELOC_IA64_LTV32MSB",
  "BFD_RELOC_IA64_LTV32LSB",
  "BFD_RELOC_IA64_LTV64MSB",
  "BFD_RELOC_IA64_LTV64LSB",
  "BFD_RELOC_IA64_IPLTMSB",
  "BFD_RELOC_IA64_IPLTLSB",
  "BFD_RELOC_IA64_COPY",
  "BFD_RELOC_IA64_LTOFF22X",
  "BFD_RELOC_IA64_LDXMOV",
  "BFD_RELOC_IA64_TPREL14",
  "BFD_RELOC_IA64_TPREL22",
  "BFD_RELOC_IA64_TPREL64I",
  "BFD_RELOC_IA64_TPREL64MSB",
  "BFD_RELOC_IA64_TPREL64LSB",
  "BFD_RELOC_IA64_LTOFF_TPREL22",
  "BFD_RELOC_IA64_DTPMOD64MSB",
  "BFD_RELOC_IA64_DTPMOD64LSB",
  "BFD_RELOC_IA64_LTOFF_DTPMOD22",
  "BFD_RELOC_IA64_DTPREL14",
  "BFD_RELOC_IA64_DTPREL22",
  "BFD_RELOC_IA64_DTPREL64I",
  "BFD_RELOC_IA64_DTPREL32MSB",
  "BFD_RELOC_IA64_DTPREL32LSB",
  "BFD_RELOC_IA64_DTPREL64MSB",
  "BFD_RELOC_IA64_DTPREL64LSB",
  "BFD_RELOC_IA64_LTOFF_DTPREL22",
  "BFD_RELOC_M68HC11_HI8",
  "BFD_RELOC_M68HC11_LO8",
  "BFD_RELOC_M68HC11_3B",
  "BFD_RELOC_M68HC11_RL_JUMP",
  "BFD_RELOC_M68HC11_RL_GROUP",
  "BFD_RELOC_M68HC11_LO16",
  "BFD_RELOC_M68HC11_PAGE",
  "BFD_RELOC_M68HC11_24",
  "BFD_RELOC_M68HC12_5B",
  "BFD_RELOC_16C_NUM08",
  "BFD_RELOC_16C_NUM08_C",
  "BFD_RELOC_16C_NUM16",
  "BFD_RELOC_16C_NUM16_C",
  "BFD_RELOC_16C_NUM32",
  "BFD_RELOC_16C_NUM32_C",
  "BFD_RELOC_16C_DISP04",
  "BFD_RELOC_16C_DISP04_C",
  "BFD_RELOC_16C_DISP08",
  "BFD_RELOC_16C_DISP08_C",
  "BFD_RELOC_16C_DISP16",
  "BFD_RELOC_16C_DISP16_C",
  "BFD_RELOC_16C_DISP24",
  "BFD_RELOC_16C_DISP24_C",
  "BFD_RELOC_16C_DISP24a",
  "BFD_RELOC_16C_DISP24a_C",
  "BFD_RELOC_16C_REG04",
  "BFD_RELOC_16C_REG04_C",
  "BFD_RELOC_16C_REG04a",
  "BFD_RELOC_16C_REG04a_C",
  "BFD_RELOC_16C_REG14",
  "BFD_RELOC_16C_REG14_C",
  "BFD_RELOC_16C_REG16",
  "BFD_RELOC_16C_REG16_C",
  "BFD_RELOC_16C_REG20",
  "BFD_RELOC_16C_REG20_C",
  "BFD_RELOC_16C_ABS20",
  "BFD_RELOC_16C_ABS20_C",
  "BFD_RELOC_16C_ABS24",
  "BFD_RELOC_16C_ABS24_C",
  "BFD_RELOC_16C_IMM04",
  "BFD_RELOC_16C_IMM04_C",
  "BFD_RELOC_16C_IMM16",
  "BFD_RELOC_16C_IMM16_C",
  "BFD_RELOC_16C_IMM20",
  "BFD_RELOC_16C_IMM20_C",
  "BFD_RELOC_16C_IMM24",
  "BFD_RELOC_16C_IMM24_C",
  "BFD_RELOC_16C_IMM32",
  "BFD_RELOC_16C_IMM32_C",
  "BFD_RELOC_CR16_NUM8",
  "BFD_RELOC_CR16_NUM16",
  "BFD_RELOC_CR16_NUM32",
  "BFD_RELOC_CR16_NUM32a",
  "BFD_RELOC_CR16_REGREL0",
  "BFD_RELOC_CR16_REGREL4",
  "BFD_RELOC_CR16_REGREL4a",
  "BFD_RELOC_CR16_REGREL14",
  "BFD_RELOC_CR16_REGREL14a",
  "BFD_RELOC_CR16_REGREL16",
  "BFD_RELOC_CR16_REGREL20",
  "BFD_RELOC_CR16_REGREL20a",
  "BFD_RELOC_CR16_ABS20",
  "BFD_RELOC_CR16_ABS24",
  "BFD_RELOC_CR16_IMM4",
  "BFD_RELOC_CR16_IMM8",
  "BFD_RELOC_CR16_IMM16",
  "BFD_RELOC_CR16_IMM20",
  "BFD_RELOC_CR16_IMM24",
  "BFD_RELOC_CR16_IMM32",
  "BFD_RELOC_CR16_IMM32a",
  "BFD_RELOC_CR16_DISP4",
  "BFD_RELOC_CR16_DISP8",
  "BFD_RELOC_CR16_DISP16",
  "BFD_RELOC_CR16_DISP20",
  "BFD_RELOC_CR16_DISP24",
  "BFD_RELOC_CR16_DISP24a",
  "BFD_RELOC_CRX_REL4",
  "BFD_RELOC_CRX_REL8",
  "BFD_RELOC_CRX_REL8_CMP",
  "BFD_RELOC_CRX_REL16",
  "BFD_RELOC_CRX_REL24",
  "BFD_RELOC_CRX_REL32",
  "BFD_RELOC_CRX_REGREL12",
  "BFD_RELOC_CRX_REGREL22",
  "BFD_RELOC_CRX_REGREL28",
  "BFD_RELOC_CRX_REGREL32",
  "BFD_RELOC_CRX_ABS16",
  "BFD_RELOC_CRX_ABS32",
  "BFD_RELOC_CRX_NUM8",
  "BFD_RELOC_CRX_NUM16",
  "BFD_RELOC_CRX_NUM32",
  "BFD_RELOC_CRX_IMM16",
  "BFD_RELOC_CRX_IMM32",
  "BFD_RELOC_CRX_SWITCH8",
  "BFD_RELOC_CRX_SWITCH16",
  "BFD_RELOC_CRX_SWITCH32",
  "BFD_RELOC_CRIS_BDISP8",
  "BFD_RELOC_CRIS_UNSIGNED_5",
  "BFD_RELOC_CRIS_SIGNED_6",
  "BFD_RELOC_CRIS_UNSIGNED_6",
  "BFD_RELOC_CRIS_SIGNED_8",
  "BFD_RELOC_CRIS_UNSIGNED_8",
  "BFD_RELOC_CRIS_SIGNED_16",
  "BFD_RELOC_CRIS_UNSIGNED_16",
  "BFD_RELOC_CRIS_LAPCQ_OFFSET",
  "BFD_RELOC_CRIS_UNSIGNED_4",
  "BFD_RELOC_CRIS_COPY",
  "BFD_RELOC_CRIS_GLOB_DAT",
  "BFD_RELOC_CRIS_JUMP_SLOT",
  "BFD_RELOC_CRIS_RELATIVE",
  "BFD_RELOC_CRIS_32_GOT",
  "BFD_RELOC_CRIS_16_GOT",
  "BFD_RELOC_CRIS_32_GOTPLT",
  "BFD_RELOC_CRIS_16_GOTPLT",
  "BFD_RELOC_CRIS_32_GOTREL",
  "BFD_RELOC_CRIS_32_PLT_GOTREL",
  "BFD_RELOC_CRIS_32_PLT_PCREL",
  "BFD_RELOC_860_COPY",
  "BFD_RELOC_860_GLOB_DAT",
  "BFD_RELOC_860_JUMP_SLOT",
  "BFD_RELOC_860_RELATIVE",
  "BFD_RELOC_860_PC26",
  "BFD_RELOC_860_PLT26",
  "BFD_RELOC_860_PC16",
  "BFD_RELOC_860_LOW0",
  "BFD_RELOC_860_SPLIT0",
  "BFD_RELOC_860_LOW1",
  "BFD_RELOC_860_SPLIT1",
  "BFD_RELOC_860_LOW2",
  "BFD_RELOC_860_SPLIT2",
  "BFD_RELOC_860_LOW3",
  "BFD_RELOC_860_LOGOT0",
  "BFD_RELOC_860_SPGOT0",
  "BFD_RELOC_860_LOGOT1",
  "BFD_RELOC_860_SPGOT1",
  "BFD_RELOC_860_LOGOTOFF0",
  "BFD_RELOC_860_SPGOTOFF0",
  "BFD_RELOC_860_LOGOTOFF1",
  "BFD_RELOC_860_SPGOTOFF1",
  "BFD_RELOC_860_LOGOTOFF2",
  "BFD_RELOC_860_LOGOTOFF3",
  "BFD_RELOC_860_LOPC",
  "BFD_RELOC_860_HIGHADJ",
  "BFD_RELOC_860_HAGOT",
  "BFD_RELOC_860_HAGOTOFF",
  "BFD_RELOC_860_HAPC",
  "BFD_RELOC_860_HIGH",
  "BFD_RELOC_860_HIGOT",
  "BFD_RELOC_860_HIGOTOFF",
  "BFD_RELOC_OPENRISC_ABS_26",
  "BFD_RELOC_OPENRISC_REL_26",
  "BFD_RELOC_H8_DIR16A8",
  "BFD_RELOC_H8_DIR16R8",
  "BFD_RELOC_H8_DIR24A8",
  "BFD_RELOC_H8_DIR24R8",
  "BFD_RELOC_H8_DIR32A16",
  "BFD_RELOC_XSTORMY16_REL_12",
  "BFD_RELOC_XSTORMY16_12",
  "BFD_RELOC_XSTORMY16_24",
  "BFD_RELOC_XSTORMY16_FPTR16",
  "BFD_RELOC_RELC",

  "BFD_RELOC_XC16X_PAG",
  "BFD_RELOC_XC16X_POF",
  "BFD_RELOC_XC16X_SEG",
  "BFD_RELOC_XC16X_SOF",
  "BFD_RELOC_VAX_GLOB_DAT",
  "BFD_RELOC_VAX_JMP_SLOT",
  "BFD_RELOC_VAX_RELATIVE",
  "BFD_RELOC_MT_PC16",
  "BFD_RELOC_MT_HI16",
  "BFD_RELOC_MT_LO16",
  "BFD_RELOC_MT_GNU_VTINHERIT",
  "BFD_RELOC_MT_GNU_VTENTRY",
  "BFD_RELOC_MT_PCINSN8",
  "BFD_RELOC_MSP430_10_PCREL",
  "BFD_RELOC_MSP430_16_PCREL",
  "BFD_RELOC_MSP430_16",
  "BFD_RELOC_MSP430_16_PCREL_BYTE",
  "BFD_RELOC_MSP430_16_BYTE",
  "BFD_RELOC_MSP430_2X_PCREL",
  "BFD_RELOC_MSP430_RL_PCREL",
  "BFD_RELOC_IQ2000_OFFSET_16",
  "BFD_RELOC_IQ2000_OFFSET_21",
  "BFD_RELOC_IQ2000_UHI16",
  "BFD_RELOC_XTENSA_RTLD",
  "BFD_RELOC_XTENSA_GLOB_DAT",
  "BFD_RELOC_XTENSA_JMP_SLOT",
  "BFD_RELOC_XTENSA_RELATIVE",
  "BFD_RELOC_XTENSA_PLT",
  "BFD_RELOC_XTENSA_DIFF8",
  "BFD_RELOC_XTENSA_DIFF16",
  "BFD_RELOC_XTENSA_DIFF32",
  "BFD_RELOC_XTENSA_SLOT0_OP",
  "BFD_RELOC_XTENSA_SLOT1_OP",
  "BFD_RELOC_XTENSA_SLOT2_OP",
  "BFD_RELOC_XTENSA_SLOT3_OP",
  "BFD_RELOC_XTENSA_SLOT4_OP",
  "BFD_RELOC_XTENSA_SLOT5_OP",
  "BFD_RELOC_XTENSA_SLOT6_OP",
  "BFD_RELOC_XTENSA_SLOT7_OP",
  "BFD_RELOC_XTENSA_SLOT8_OP",
  "BFD_RELOC_XTENSA_SLOT9_OP",
  "BFD_RELOC_XTENSA_SLOT10_OP",
  "BFD_RELOC_XTENSA_SLOT11_OP",
  "BFD_RELOC_XTENSA_SLOT12_OP",
  "BFD_RELOC_XTENSA_SLOT13_OP",
  "BFD_RELOC_XTENSA_SLOT14_OP",
  "BFD_RELOC_XTENSA_SLOT0_ALT",
  "BFD_RELOC_XTENSA_SLOT1_ALT",
  "BFD_RELOC_XTENSA_SLOT2_ALT",
  "BFD_RELOC_XTENSA_SLOT3_ALT",
  "BFD_RELOC_XTENSA_SLOT4_ALT",
  "BFD_RELOC_XTENSA_SLOT5_ALT",
  "BFD_RELOC_XTENSA_SLOT6_ALT",
  "BFD_RELOC_XTENSA_SLOT7_ALT",
  "BFD_RELOC_XTENSA_SLOT8_ALT",
  "BFD_RELOC_XTENSA_SLOT9_ALT",
  "BFD_RELOC_XTENSA_SLOT10_ALT",
  "BFD_RELOC_XTENSA_SLOT11_ALT",
  "BFD_RELOC_XTENSA_SLOT12_ALT",
  "BFD_RELOC_XTENSA_SLOT13_ALT",
  "BFD_RELOC_XTENSA_SLOT14_ALT",
  "BFD_RELOC_XTENSA_OP0",
  "BFD_RELOC_XTENSA_OP1",
  "BFD_RELOC_XTENSA_OP2",
  "BFD_RELOC_XTENSA_ASM_EXPAND",
  "BFD_RELOC_XTENSA_ASM_SIMPLIFY",
  "BFD_RELOC_Z80_DISP8",
  "BFD_RELOC_Z8K_DISP7",
  "BFD_RELOC_Z8K_CALLR",
  "BFD_RELOC_Z8K_IMM4L",
 "@@overflow: BFD_RELOC_UNUSED@@",
};
#endif

reloc_howto_type *bfd_default_reloc_type_lookup
   (bfd *abfd, bfd_reloc_code_real_type  code);

bfd_boolean bfd_generic_relax_section
   (bfd *abfd,
    asection *section,
    struct bfd_link_info *,
    bfd_boolean *);

bfd_boolean bfd_generic_gc_sections
   (bfd *, struct bfd_link_info *);

bfd_boolean bfd_generic_merge_sections
   (bfd *, struct bfd_link_info *);

bfd_byte *bfd_generic_get_relocated_section_contents
   (bfd *abfd,
    struct bfd_link_info *link_info,
    struct bfd_link_order *link_order,
    bfd_byte *data,
    bfd_boolean relocatable,
    asymbol **symbols);

/* Extracted from archures.c.  */
extern const bfd_arch_info_type bfd_default_arch_struct;
bfd_boolean bfd_default_set_arch_mach
   (bfd *abfd, enum bfd_architecture arch, unsigned long mach);

const bfd_arch_info_type *bfd_default_compatible
   (const bfd_arch_info_type *a, const bfd_arch_info_type *b);

bfd_boolean bfd_default_scan
   (const struct bfd_arch_info *info, const char *string);

/* Extracted from elf.c.  */
struct elf_internal_shdr *bfd_elf_find_section (bfd *abfd, char *name);


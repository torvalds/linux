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


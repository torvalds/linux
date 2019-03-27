/* ldlang.h - linker command language support
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This file is part of GLD, the Gnu Linker.

   GLD is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GLD is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GLD; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef LDLANG_H
#define LDLANG_H

#define DEFAULT_MEMORY_REGION   "*default*"

typedef enum
{
  lang_input_file_is_l_enum,
  lang_input_file_is_symbols_only_enum,
  lang_input_file_is_marker_enum,
  lang_input_file_is_fake_enum,
  lang_input_file_is_search_file_enum,
  lang_input_file_is_file_enum
} lang_input_file_enum_type;

struct _fill_type
{
  size_t size;
  unsigned char data[1];
};

typedef struct statement_list
{
  union lang_statement_union *head;
  union lang_statement_union **tail;
} lang_statement_list_type;

typedef struct memory_region_struct
{
  char *name;
  struct memory_region_struct *next;
  bfd_vma origin;
  bfd_size_type length;
  bfd_vma current;
  union lang_statement_union *last_os;
  flagword flags;
  flagword not_flags;
  bfd_boolean had_full_message;
} lang_memory_region_type;

typedef struct lang_statement_header_struct
{
  union lang_statement_union *next;
  enum statement_enum
  {
    lang_output_section_statement_enum,
    lang_assignment_statement_enum,
    lang_input_statement_enum,
    lang_address_statement_enum,
    lang_wild_statement_enum,
    lang_input_section_enum,
    lang_object_symbols_statement_enum,
    lang_fill_statement_enum,
    lang_data_statement_enum,
    lang_reloc_statement_enum,
    lang_target_statement_enum,
    lang_output_statement_enum,
    lang_padding_statement_enum,
    lang_group_statement_enum,

    lang_afile_asection_pair_statement_enum,
    lang_constructors_statement_enum
  } type;
} lang_statement_header_type;

typedef struct
{
  lang_statement_header_type header;
  union etree_union *exp;
} lang_assignment_statement_type;

typedef struct lang_target_statement_struct
{
  lang_statement_header_type header;
  const char *target;
} lang_target_statement_type;

typedef struct lang_output_statement_struct
{
  lang_statement_header_type header;
  const char *name;
} lang_output_statement_type;

/* Section types specified in a linker script.  */

enum section_type
{
  normal_section,
  overlay_section,
  noload_section,
  noalloc_section
};

/* This structure holds a list of program headers describing
   segments in which this section should be placed.  */

typedef struct lang_output_section_phdr_list
{
  struct lang_output_section_phdr_list *next;
  const char *name;
  bfd_boolean used;
} lang_output_section_phdr_list;

typedef struct lang_output_section_statement_struct
{
  lang_statement_header_type header;
  lang_statement_list_type children;
  struct lang_output_section_statement_struct *next;
  struct lang_output_section_statement_struct *prev;
  const char *name;
  asection *bfd_section;
  lang_memory_region_type *region;
  lang_memory_region_type *lma_region;
  fill_type *fill;
  union etree_union *addr_tree;
  union etree_union *load_base;

  /* If non-null, an expression to evaluate after setting the section's
     size.  The expression is evaluated inside REGION (above) with '.'
     set to the end of the section.  Used in the last overlay section
     to move '.' past all the overlaid sections.  */
  union etree_union *update_dot_tree;

  lang_output_section_phdr_list *phdrs;

  unsigned int block_value;
  int subsection_alignment;	/* Alignment of components.  */
  int section_alignment;	/* Alignment of start of section.  */
  int constraint;
  flagword flags;
  enum section_type sectype;
  unsigned int processed_vma : 1;
  unsigned int processed_lma : 1;
  unsigned int all_input_readonly : 1;
  /* If this section should be ignored.  */
  unsigned int ignored : 1; 
  /* If there is a symbol relative to this section.  */
  unsigned int section_relative_symbol : 1; 
} lang_output_section_statement_type;

typedef struct
{
  lang_statement_header_type header;
} lang_common_statement_type;

typedef struct
{
  lang_statement_header_type header;
} lang_object_symbols_statement_type;

typedef struct
{
  lang_statement_header_type header;
  fill_type *fill;
  int size;
  asection *output_section;
} lang_fill_statement_type;

typedef struct
{
  lang_statement_header_type header;
  unsigned int type;
  union etree_union *exp;
  bfd_vma value;
  asection *output_section;
  bfd_vma output_offset;
} lang_data_statement_type;

/* Generate a reloc in the output file.  */

typedef struct
{
  lang_statement_header_type header;

  /* Reloc to generate.  */
  bfd_reloc_code_real_type reloc;

  /* Reloc howto structure.  */
  reloc_howto_type *howto;

  /* Section to generate reloc against.
     Exactly one of section and name must be NULL.  */
  asection *section;

  /* Name of symbol to generate reloc against.
     Exactly one of section and name must be NULL.  */
  const char *name;

  /* Expression for addend.  */
  union etree_union *addend_exp;

  /* Resolved addend.  */
  bfd_vma addend_value;

  /* Output section where reloc should be performed.  */
  asection *output_section;

  /* Offset within output section.  */
  bfd_vma output_offset;
} lang_reloc_statement_type;

typedef struct lang_input_statement_struct
{
  lang_statement_header_type header;
  /* Name of this file.  */
  const char *filename;
  /* Name to use for the symbol giving address of text start.
     Usually the same as filename, but for a file spec'd with
     -l this is the -l switch itself rather than the filename.  */
  const char *local_sym_name;

  bfd *the_bfd;

  file_ptr passive_position;

  /* Symbol table of the file.  */
  asymbol **asymbols;
  unsigned int symbol_count;

  /* Point to the next file - whatever it is, wanders up and down
     archives */
  union lang_statement_union *next;

  /* Point to the next file, but skips archive contents.  */
  union lang_statement_union *next_real_file;

  const char *target;

  unsigned int closed : 1;
  unsigned int is_archive : 1;

  /* 1 means search a set of directories for this file.  */
  unsigned int search_dirs_flag : 1;

  /* 1 means this was found in a search directory marked as sysrooted,
     if search_dirs_flag is false, otherwise, that it should be
     searched in ld_sysroot before any other location, as long as it
     starts with a slash.  */
  unsigned int sysrooted : 1;

  /* 1 means this is base file of incremental load.
     Do not load this file's text or data.
     Also default text_start to after this file's bss.  */
  unsigned int just_syms_flag : 1;

  /* Whether to search for this entry as a dynamic archive.  */
  unsigned int dynamic : 1;

  /* Whether DT_NEEDED tags should be added for dynamic libraries in
     DT_NEEDED tags from this entry.  */
  unsigned int add_needed : 1;

  /* Whether this entry should cause a DT_NEEDED tag only when
     satisfying references from regular files, or always.  */
  unsigned int as_needed : 1;

  /* Whether to include the entire contents of an archive.  */
  unsigned int whole_archive : 1;

  unsigned int loaded : 1;

  unsigned int real : 1;
} lang_input_statement_type;

typedef struct
{
  lang_statement_header_type header;
  asection *section;
} lang_input_section_type;

typedef struct
{
  lang_statement_header_type header;
  asection *section;
  union lang_statement_union *file;
} lang_afile_asection_pair_statement_type;

typedef struct lang_wild_statement_struct lang_wild_statement_type;

typedef void (*callback_t) (lang_wild_statement_type *, struct wildcard_list *,
			    asection *, lang_input_statement_type *, void *);

typedef void (*walk_wild_section_handler_t) (lang_wild_statement_type *,
					     lang_input_statement_type *,
					     callback_t callback,
					     void *data);

typedef bfd_boolean (*lang_match_sec_type_func) (bfd *, const asection *,
						 bfd *, const asection *);

/* Binary search tree structure to efficiently sort sections by
   name.  */
typedef struct lang_section_bst
{
  asection *section;
  struct lang_section_bst *left;
  struct lang_section_bst *right;
} lang_section_bst_type;

struct lang_wild_statement_struct
{
  lang_statement_header_type header;
  const char *filename;
  bfd_boolean filenames_sorted;
  struct wildcard_list *section_list;
  bfd_boolean keep_sections;
  lang_statement_list_type children;

  walk_wild_section_handler_t walk_wild_section_handler;
  struct wildcard_list *handler_data[4];
  lang_section_bst_type *tree;
};

typedef struct lang_address_statement_struct
{
  lang_statement_header_type header;
  const char *section_name;
  union etree_union *address;
  const segment_type *segment;
} lang_address_statement_type;

typedef struct
{
  lang_statement_header_type header;
  bfd_vma output_offset;
  size_t size;
  asection *output_section;
  fill_type *fill;
} lang_padding_statement_type;

/* A group statement collects a set of libraries together.  The
   libraries are searched multiple times, until no new undefined
   symbols are found.  The effect is to search a group of libraries as
   though they were a single library.  */

typedef struct
{
  lang_statement_header_type header;
  lang_statement_list_type children;
} lang_group_statement_type;

typedef union lang_statement_union
{
  lang_statement_header_type header;
  lang_wild_statement_type wild_statement;
  lang_data_statement_type data_statement;
  lang_reloc_statement_type reloc_statement;
  lang_address_statement_type address_statement;
  lang_output_section_statement_type output_section_statement;
  lang_afile_asection_pair_statement_type afile_asection_pair_statement;
  lang_assignment_statement_type assignment_statement;
  lang_input_statement_type input_statement;
  lang_target_statement_type target_statement;
  lang_output_statement_type output_statement;
  lang_input_section_type input_section;
  lang_common_statement_type common_statement;
  lang_object_symbols_statement_type object_symbols_statement;
  lang_fill_statement_type fill_statement;
  lang_padding_statement_type padding_statement;
  lang_group_statement_type group_statement;
} lang_statement_union_type;

/* This structure holds information about a program header, from the
   PHDRS command in the linker script.  */

struct lang_phdr
{
  struct lang_phdr *next;
  const char *name;
  unsigned long type;
  bfd_boolean filehdr;
  bfd_boolean phdrs;
  etree_type *at;
  etree_type *flags;
};

extern struct lang_phdr *lang_phdr_list;

/* This structure is used to hold a list of sections which may not
   cross reference each other.  */

typedef struct lang_nocrossref
{
  struct lang_nocrossref *next;
  const char *name;
} lang_nocrossref_type;

/* The list of nocrossref lists.  */

struct lang_nocrossrefs
{
  struct lang_nocrossrefs *next;
  lang_nocrossref_type *list;
};

extern struct lang_nocrossrefs *nocrossref_list;

/* This structure is used to hold a list of input section names which
   will not match an output section in the linker script.  */

struct unique_sections
{
  struct unique_sections *next;
  const char *name;
};

/* This structure records symbols for which we need to keep track of
   definedness for use in the DEFINED () test.  */

struct lang_definedness_hash_entry
{
  struct bfd_hash_entry root;
  int iteration;
};

/* Used by place_orphan to keep track of orphan sections and statements.  */

struct orphan_save {
  const char *name;
  flagword flags;
  lang_output_section_statement_type *os;
  asection **section;
  lang_statement_union_type **stmt;
  lang_output_section_statement_type **os_tail;
};

extern lang_output_section_statement_type *abs_output_section;
extern lang_statement_list_type lang_output_section_statement;
extern bfd_boolean lang_has_input_file;
extern etree_type *base;
extern lang_statement_list_type *stat_ptr;
extern bfd_boolean delete_output_file_on_failure;

extern struct bfd_sym_chain entry_symbol;
extern const char *entry_section;
extern bfd_boolean entry_from_cmdline;
extern lang_statement_list_type file_chain;
extern lang_statement_list_type input_file_chain;

extern int lang_statement_iteration;

extern void lang_init
  (void);
extern void lang_finish
  (void);
extern lang_memory_region_type *lang_memory_region_lookup
  (const char *const, bfd_boolean);
extern lang_memory_region_type *lang_memory_region_default
  (asection *);
extern void lang_map
  (void);
extern void lang_set_flags
  (lang_memory_region_type *, const char *, int);
extern void lang_add_output
  (const char *, int from_script);
extern lang_output_section_statement_type *lang_enter_output_section_statement
  (const char *output_section_statement_name,
   etree_type *address_exp,
   enum section_type sectype,
   etree_type *align,
   etree_type *subalign,
   etree_type *, int);
extern void lang_final
  (void);
extern void lang_process
  (void);
extern void lang_section_start
  (const char *, union etree_union *, const segment_type *);
extern void lang_add_entry
  (const char *, bfd_boolean);
extern void lang_default_entry
  (const char *);
extern void lang_add_target
  (const char *);
extern void lang_add_wild
  (struct wildcard_spec *, struct wildcard_list *, bfd_boolean);
extern void lang_add_map
  (const char *);
extern void lang_add_fill
  (fill_type *);
extern lang_assignment_statement_type *lang_add_assignment
  (union etree_union *);
extern void lang_add_attribute
  (enum statement_enum);
extern void lang_startup
  (const char *);
extern void lang_float
  (bfd_boolean);
extern void lang_leave_output_section_statement
  (fill_type *, const char *, lang_output_section_phdr_list *,
   const char *);
extern void lang_abs_symbol_at_end_of
  (const char *, const char *);
extern void lang_abs_symbol_at_beginning_of
  (const char *, const char *);
extern void lang_statement_append
  (lang_statement_list_type *, lang_statement_union_type *,
   lang_statement_union_type **);
extern void lang_for_each_input_file
  (void (*dothis) (lang_input_statement_type *));
extern void lang_for_each_file
  (void (*dothis) (lang_input_statement_type *));
extern void lang_reset_memory_regions
  (void);
extern void lang_do_assignments
  (void);

#define LANG_FOR_EACH_INPUT_STATEMENT(statement)			\
  lang_input_statement_type *statement;					\
  for (statement = (lang_input_statement_type *) file_chain.head;	\
       statement != (lang_input_statement_type *) NULL;			\
       statement = (lang_input_statement_type *) statement->next)	\

extern void lang_process
  (void);
extern void ldlang_add_file
  (lang_input_statement_type *);
extern lang_output_section_statement_type *lang_output_section_find
  (const char * const);
extern lang_output_section_statement_type *lang_output_section_find_by_flags
  (const asection *, lang_output_section_statement_type **,
   lang_match_sec_type_func);
extern lang_output_section_statement_type *lang_insert_orphan
  (asection *, const char *, lang_output_section_statement_type *,
   struct orphan_save *, etree_type *, lang_statement_list_type *);
extern lang_input_statement_type *lang_add_input_file
  (const char *, lang_input_file_enum_type, const char *);
extern void lang_add_keepsyms_file
  (const char *);
extern lang_output_section_statement_type *
  lang_output_section_statement_lookup
  (const char *const);
extern void ldlang_add_undef
  (const char *const);
extern void lang_add_output_format
  (const char *, const char *, const char *, int);
extern void lang_list_init
  (lang_statement_list_type *);
extern void lang_add_data
  (int type, union etree_union *);
extern void lang_add_reloc
  (bfd_reloc_code_real_type, reloc_howto_type *, asection *, const char *,
   union etree_union *);
extern void lang_for_each_statement
  (void (*) (lang_statement_union_type *));
extern void *stat_alloc
  (size_t);
extern void strip_excluded_output_sections
  (void);
extern void dprint_statement
  (lang_statement_union_type *, int);
extern void lang_size_sections
  (bfd_boolean *, bfd_boolean);
extern void one_lang_size_sections_pass
  (bfd_boolean *, bfd_boolean);
extern void lang_enter_group
  (void);
extern void lang_leave_group
  (void);
extern void lang_add_section
  (lang_statement_list_type *, asection *,
   lang_output_section_statement_type *);
extern void lang_new_phdr
  (const char *, etree_type *, bfd_boolean, bfd_boolean, etree_type *,
   etree_type *);
extern void lang_add_nocrossref
  (lang_nocrossref_type *);
extern void lang_enter_overlay
  (etree_type *, etree_type *);
extern void lang_enter_overlay_section
  (const char *);
extern void lang_leave_overlay_section
  (fill_type *, lang_output_section_phdr_list *);
extern void lang_leave_overlay
  (etree_type *, int, fill_type *, const char *,
   lang_output_section_phdr_list *, const char *);

extern struct bfd_elf_version_tree *lang_elf_version_info;

extern struct bfd_elf_version_expr *lang_new_vers_pattern
  (struct bfd_elf_version_expr *, const char *, const char *, bfd_boolean);
extern struct bfd_elf_version_tree *lang_new_vers_node
  (struct bfd_elf_version_expr *, struct bfd_elf_version_expr *);
extern struct bfd_elf_version_deps *lang_add_vers_depend
  (struct bfd_elf_version_deps *, const char *);
extern void lang_register_vers_node
  (const char *, struct bfd_elf_version_tree *, struct bfd_elf_version_deps *);
extern void lang_append_dynamic_list (struct bfd_elf_version_expr *);
extern void lang_append_dynamic_list_cpp_typeinfo (void);
extern void lang_append_dynamic_list_cpp_new (void);
bfd_boolean unique_section_p
  (const asection *);
extern void lang_add_unique
  (const char *);
extern const char *lang_get_output_target
  (void);
extern void lang_track_definedness (const char *);
extern int lang_symbol_definition_iteration (const char *);
extern void lang_update_definedness
  (const char *, struct bfd_link_hash_entry *);

extern void add_excluded_libs (const char *);
extern bfd_boolean load_symbols
  (lang_input_statement_type *, lang_statement_list_type *);

extern bfd_boolean
ldlang_override_segment_assignment
  (struct bfd_link_info *, bfd *, asection *, asection *, bfd_boolean);

#endif

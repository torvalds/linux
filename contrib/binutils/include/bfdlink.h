/* bfdlink.h -- header file for BFD link routines
   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.
   Written by Steve Chamberlain and Ian Lance Taylor, Cygnus Support.

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

#ifndef BFDLINK_H
#define BFDLINK_H

/* Which symbols to strip during a link.  */
enum bfd_link_strip
{
  strip_none,		/* Don't strip any symbols.  */
  strip_debugger,	/* Strip debugging symbols.  */
  strip_some,		/* keep_hash is the list of symbols to keep.  */
  strip_all		/* Strip all symbols.  */
};

/* Which local symbols to discard during a link.  This is irrelevant
   if strip_all is used.  */
enum bfd_link_discard
{
  discard_sec_merge,	/* Discard local temporary symbols in SEC_MERGE
			   sections.  */
  discard_none,		/* Don't discard any locals.  */
  discard_l,		/* Discard local temporary symbols.  */
  discard_all		/* Discard all locals.  */
};

/* Describes the type of hash table entry structure being used.
   Different hash table structure have different fields and so
   support different linking features.  */
enum bfd_link_hash_table_type
  {
    bfd_link_generic_hash_table,
    bfd_link_elf_hash_table
  };

/* These are the possible types of an entry in the BFD link hash
   table.  */

enum bfd_link_hash_type
{
  bfd_link_hash_new,		/* Symbol is new.  */
  bfd_link_hash_undefined,	/* Symbol seen before, but undefined.  */
  bfd_link_hash_undefweak,	/* Symbol is weak and undefined.  */
  bfd_link_hash_defined,	/* Symbol is defined.  */
  bfd_link_hash_defweak,	/* Symbol is weak and defined.  */
  bfd_link_hash_common,		/* Symbol is common.  */
  bfd_link_hash_indirect,	/* Symbol is an indirect link.  */
  bfd_link_hash_warning		/* Like indirect, but warn if referenced.  */
};

enum bfd_link_common_skip_ar_aymbols
{
  bfd_link_common_skip_none,
  bfd_link_common_skip_text,
  bfd_link_common_skip_data,
  bfd_link_common_skip_all
};

/* The linking routines use a hash table which uses this structure for
   its elements.  */

struct bfd_link_hash_entry
{
  /* Base hash table entry structure.  */
  struct bfd_hash_entry root;

  /* Type of this entry.  */
  enum bfd_link_hash_type type;

  /* A union of information depending upon the type.  */
  union
    {
      /* Nothing is kept for bfd_hash_new.  */
      /* bfd_link_hash_undefined, bfd_link_hash_undefweak.  */
      struct
	{
	  /* Undefined and common symbols are kept in a linked list through
	     this field.  This field is present in all of the union element
	     so that we don't need to remove entries from the list when we
	     change their type.  Removing entries would either require the
	     list to be doubly linked, which would waste more memory, or
	     require a traversal.  When an undefined or common symbol is
	     created, it should be added to this list, the head of which is in
	     the link hash table itself.  As symbols are defined, they need
	     not be removed from the list; anything which reads the list must
	     doublecheck the symbol type.

	     Weak symbols are not kept on this list.

	     Defined and defweak symbols use this field as a reference marker.
	     If the field is not NULL, or this structure is the tail of the
	     undefined symbol list, the symbol has been referenced.  If the
	     symbol is undefined and becomes defined, this field will
	     automatically be non-NULL since the symbol will have been on the
	     undefined symbol list.  */
	  struct bfd_link_hash_entry *next;
	  bfd *abfd;		/* BFD symbol was found in.  */
	  bfd *weak;		/* BFD weak symbol was found in.  */
	} undef;
      /* bfd_link_hash_defined, bfd_link_hash_defweak.  */
      struct
	{
	  struct bfd_link_hash_entry *next;
	  asection *section;	/* Symbol section.  */
	  bfd_vma value;	/* Symbol value.  */
	} def;
      /* bfd_link_hash_indirect, bfd_link_hash_warning.  */
      struct
	{
	  struct bfd_link_hash_entry *next;
	  struct bfd_link_hash_entry *link;	/* Real symbol.  */
	  const char *warning;	/* Warning (bfd_link_hash_warning only).  */
	} i;
      /* bfd_link_hash_common.  */
      struct
	{
	  struct bfd_link_hash_entry *next;
	  /* The linker needs to know three things about common
	     symbols: the size, the alignment, and the section in
	     which the symbol should be placed.  We store the size
	     here, and we allocate a small structure to hold the
	     section and the alignment.  The alignment is stored as a
	     power of two.  We don't store all the information
	     directly because we don't want to increase the size of
	     the union; this structure is a major space user in the
	     linker.  */
	  struct bfd_link_hash_common_entry
	    {
	      unsigned int alignment_power;	/* Alignment.  */
	      asection *section;		/* Symbol section.  */
	    } *p;
	  bfd_size_type size;	/* Common symbol size.  */
	} c;
    } u;
};

/* This is the link hash table.  It is a derived class of
   bfd_hash_table.  */

struct bfd_link_hash_table
{
  /* The hash table itself.  */
  struct bfd_hash_table table;
  /* The back end which created this hash table.  This indicates the
     type of the entries in the hash table, which is sometimes
     important information when linking object files of different
     types together.  */
  const bfd_target *creator;
  /* A linked list of undefined and common symbols, linked through the
     next field in the bfd_link_hash_entry structure.  */
  struct bfd_link_hash_entry *undefs;
  /* Entries are added to the tail of the undefs list.  */
  struct bfd_link_hash_entry *undefs_tail;
  /* The type of the link hash table.  */
  enum bfd_link_hash_table_type type;
};

/* Look up an entry in a link hash table.  If FOLLOW is TRUE, this
   follows bfd_link_hash_indirect and bfd_link_hash_warning links to
   the real symbol.  */
extern struct bfd_link_hash_entry *bfd_link_hash_lookup
  (struct bfd_link_hash_table *, const char *, bfd_boolean create,
   bfd_boolean copy, bfd_boolean follow);

/* Look up an entry in the main linker hash table if the symbol might
   be wrapped.  This should only be used for references to an
   undefined symbol, not for definitions of a symbol.  */

extern struct bfd_link_hash_entry *bfd_wrapped_link_hash_lookup
  (bfd *, struct bfd_link_info *, const char *, bfd_boolean,
   bfd_boolean, bfd_boolean);

/* Traverse a link hash table.  */
extern void bfd_link_hash_traverse
  (struct bfd_link_hash_table *,
    bfd_boolean (*) (struct bfd_link_hash_entry *, void *),
    void *);

/* Add an entry to the undefs list.  */
extern void bfd_link_add_undef
  (struct bfd_link_hash_table *, struct bfd_link_hash_entry *);

/* Remove symbols from the undefs list that don't belong there.  */
extern void bfd_link_repair_undef_list
  (struct bfd_link_hash_table *table);

struct bfd_sym_chain
{
  struct bfd_sym_chain *next;
  const char *name;
};

/* How to handle unresolved symbols.
   There are four possibilities which are enumerated below:  */
enum report_method
{
  /* This is the initial value when then link_info structure is created.
     It allows the various stages of the linker to determine whether they
     allowed to set the value.  */
  RM_NOT_YET_SET = 0,
  RM_IGNORE,
  RM_GENERATE_WARNING,
  RM_GENERATE_ERROR
};

struct bfd_elf_dynamic_list;

/* This structure holds all the information needed to communicate
   between BFD and the linker when doing a link.  */

struct bfd_link_info
{
  /* TRUE if BFD should generate a relocatable object file.  */
  unsigned int relocatable: 1;

  /* TRUE if BFD should generate relocation information in the final
     executable.  */
  unsigned int emitrelocations: 1;

  /* TRUE if BFD should generate a "task linked" object file,
     similar to relocatable but also with globals converted to
     statics.  */
  unsigned int task_link: 1;

  /* TRUE if BFD should generate a shared object.  */
  unsigned int shared: 1;

  /* TRUE if BFD should pre-bind symbols in a shared object.  */
  unsigned int symbolic: 1;

  /* TRUE if BFD should export all symbols in the dynamic symbol table
     of an executable, rather than only those used.  */
  unsigned int export_dynamic: 1;

  /* TRUE if shared objects should be linked directly, not shared.  */
  unsigned int static_link: 1;

  /* TRUE if the output file should be in a traditional format.  This
     is equivalent to the setting of the BFD_TRADITIONAL_FORMAT flag
     on the output file, but may be checked when reading the input
     files.  */
  unsigned int traditional_format: 1;

  /* TRUE if we want to produced optimized output files.  This might
     need much more time and therefore must be explicitly selected.  */
  unsigned int optimize: 1;

  /* TRUE if ok to have multiple definition.  */
  unsigned int allow_multiple_definition: 1;

  /* TRUE if ok to have version with no definition.  */
  unsigned int allow_undefined_version: 1;

  /* TRUE if a default symbol version should be created and used for
     exported symbols.  */
  unsigned int create_default_symver: 1;

  /* TRUE if a default symbol version should be created and used for
     imported symbols.  */
  unsigned int default_imported_symver: 1;

  /* TRUE if symbols should be retained in memory, FALSE if they
     should be freed and reread.  */
  unsigned int keep_memory: 1;

  /* TRUE if every symbol should be reported back via the notice
     callback.  */
  unsigned int notice_all: 1;

  /* TRUE if executable should not contain copy relocs.
     Setting this true may result in a non-sharable text segment.  */
  unsigned int nocopyreloc: 1;

  /* TRUE if the new ELF dynamic tags are enabled. */
  unsigned int new_dtags: 1;

  /* TRUE if non-PLT relocs should be merged into one reloc section
     and sorted so that relocs against the same symbol come together.  */
  unsigned int combreloc: 1;

  /* TRUE if .eh_frame_hdr section and PT_GNU_EH_FRAME ELF segment
     should be created.  */
  unsigned int eh_frame_hdr: 1;

  /* TRUE if global symbols in discarded sections should be stripped.  */
  unsigned int strip_discarded: 1;

  /* TRUE if generating a position independent executable.  */
  unsigned int pie: 1;

  /* TRUE if generating an executable, position independent or not.  */
  unsigned int executable : 1;

  /* TRUE if PT_GNU_STACK segment should be created with PF_R|PF_W|PF_X
     flags.  */
  unsigned int execstack: 1;

  /* TRUE if PT_GNU_STACK segment should be created with PF_R|PF_W
     flags.  */
  unsigned int noexecstack: 1;

  /* TRUE if PT_GNU_RELRO segment should be created.  */
  unsigned int relro: 1;

  /* TRUE if we should warn when adding a DT_TEXTREL to a shared object.  */
  unsigned int warn_shared_textrel: 1;

  /* TRUE if unreferenced sections should be removed.  */
  unsigned int gc_sections: 1;

  /* TRUE if user shoudl be informed of removed unreferenced sections.  */
  unsigned int print_gc_sections: 1;

  /* TRUE if .hash section should be created.  */
  unsigned int emit_hash: 1;

  /* TRUE if .gnu.hash section should be created.  */
  unsigned int emit_gnu_hash: 1;

  /* If TRUE reduce memory overheads, at the expense of speed. This will
     cause map file generation to use an O(N^2) algorithm and disable
     caching ELF symbol buffer.  */
  unsigned int reduce_memory_overheads: 1;

  /* TRUE if all data symbols should be dynamic.  */
   unsigned int dynamic_data: 1;

  /* TRUE if some symbols have to be dynamic, controlled by
     --dynamic-list command line options.  */
  unsigned int dynamic: 1;

  /* What to do with unresolved symbols in an object file.
     When producing executables the default is GENERATE_ERROR.
     When producing shared libraries the default is IGNORE.  The
     assumption with shared libraries is that the reference will be
     resolved at load/execution time.  */
  enum report_method unresolved_syms_in_objects;

  /* What to do with unresolved symbols in a shared library.
     The same defaults apply.  */
  enum report_method unresolved_syms_in_shared_libs;

  /* Which symbols to strip.  */
  enum bfd_link_strip strip;

  /* Which local symbols to discard.  */
  enum bfd_link_discard discard;

  /* Criteria for skipping symbols when detemining
     whether to include an object from an archive. */
  enum bfd_link_common_skip_ar_aymbols common_skip_ar_aymbols;

  /* Char that may appear as the first char of a symbol, but should be
     skipped (like symbol_leading_char) when looking up symbols in
     wrap_hash.  Used by PowerPC Linux for 'dot' symbols.  */
  char wrap_char;

  /* Function callbacks.  */
  const struct bfd_link_callbacks *callbacks;

  /* Hash table handled by BFD.  */
  struct bfd_link_hash_table *hash;

  /* Hash table of symbols to keep.  This is NULL unless strip is
     strip_some.  */
  struct bfd_hash_table *keep_hash;

  /* Hash table of symbols to report back via the notice callback.  If
     this is NULL, and notice_all is FALSE, then no symbols are
     reported back.  */
  struct bfd_hash_table *notice_hash;

  /* Hash table of symbols which are being wrapped (the --wrap linker
     option).  If this is NULL, no symbols are being wrapped.  */
  struct bfd_hash_table *wrap_hash;

  /* The list of input BFD's involved in the link.  These are chained
     together via the link_next field.  */
  bfd *input_bfds;
  bfd **input_bfds_tail;

  /* If a symbol should be created for each input BFD, this is section
     where those symbols should be placed.  It must be a section in
     the output BFD.  It may be NULL, in which case no such symbols
     will be created.  This is to support CREATE_OBJECT_SYMBOLS in the
     linker command language.  */
  asection *create_object_symbols_section;

  /* List of global symbol names that are starting points for marking
     sections against garbage collection.  */
  struct bfd_sym_chain *gc_sym_list;

  /* If a base output file is wanted, then this points to it */
  void *base_file;

  /* The function to call when the executable or shared object is
     loaded.  */
  const char *init_function;

  /* The function to call when the executable or shared object is
     unloaded.  */
  const char *fini_function;

  /* Number of relaxation passes.  Usually only one relaxation pass
     is needed.  But a backend can have as many relaxation passes as
     necessary.  During bfd_relax_section call, it is set to the
     current pass, starting from 0.  */
  int relax_pass;

  /* Number of relaxation trips.  This number is incremented every
     time the relaxation pass is restarted due to a previous
     relaxation returning true in *AGAIN.  */
  int relax_trip;

  /* Non-zero if auto-import thunks for DATA items in pei386 DLLs
     should be generated/linked against.  Set to 1 if this feature
     is explicitly requested by the user, -1 if enabled by default.  */
  int pei386_auto_import;

  /* Non-zero if runtime relocs for DATA items with non-zero addends
     in pei386 DLLs should be generated.  Set to 1 if this feature
     is explicitly requested by the user, -1 if enabled by default.  */
  int pei386_runtime_pseudo_reloc;

  /* How many spare .dynamic DT_NULL entries should be added?  */
  unsigned int spare_dynamic_tags;

  /* May be used to set DT_FLAGS for ELF. */
  bfd_vma flags;

  /* May be used to set DT_FLAGS_1 for ELF. */
  bfd_vma flags_1;

  /* Start and end of RELRO region.  */
  bfd_vma relro_start, relro_end;

  /* List of symbols should be dynamic.  */
  struct bfd_elf_dynamic_list *dynamic_list;
};

/* This structures holds a set of callback functions.  These are called
   by the BFD linker routines.  Except for the info functions, the first
   argument to each callback function is the bfd_link_info structure
   being used and each function returns a boolean value.  If the
   function returns FALSE, then the BFD function which called it should
   return with a failure indication.  */

struct bfd_link_callbacks
{
  /* A function which is called when an object is added from an
     archive.  ABFD is the archive element being added.  NAME is the
     name of the symbol which caused the archive element to be pulled
     in.  */
  bfd_boolean (*add_archive_element)
    (struct bfd_link_info *, bfd *abfd, const char *name);
  /* A function which is called when a symbol is found with multiple
     definitions.  NAME is the symbol which is defined multiple times.
     OBFD is the old BFD, OSEC is the old section, OVAL is the old
     value, NBFD is the new BFD, NSEC is the new section, and NVAL is
     the new value.  OBFD may be NULL.  OSEC and NSEC may be
     bfd_com_section or bfd_ind_section.  */
  bfd_boolean (*multiple_definition)
    (struct bfd_link_info *, const char *name,
     bfd *obfd, asection *osec, bfd_vma oval,
     bfd *nbfd, asection *nsec, bfd_vma nval);
  /* A function which is called when a common symbol is defined
     multiple times.  NAME is the symbol appearing multiple times.
     OBFD is the BFD of the existing symbol; it may be NULL if this is
     not known.  OTYPE is the type of the existing symbol, which may
     be bfd_link_hash_defined, bfd_link_hash_defweak,
     bfd_link_hash_common, or bfd_link_hash_indirect.  If OTYPE is
     bfd_link_hash_common, OSIZE is the size of the existing symbol.
     NBFD is the BFD of the new symbol.  NTYPE is the type of the new
     symbol, one of bfd_link_hash_defined, bfd_link_hash_common, or
     bfd_link_hash_indirect.  If NTYPE is bfd_link_hash_common, NSIZE
     is the size of the new symbol.  */
  bfd_boolean (*multiple_common)
    (struct bfd_link_info *, const char *name,
     bfd *obfd, enum bfd_link_hash_type otype, bfd_vma osize,
     bfd *nbfd, enum bfd_link_hash_type ntype, bfd_vma nsize);
  /* A function which is called to add a symbol to a set.  ENTRY is
     the link hash table entry for the set itself (e.g.,
     __CTOR_LIST__).  RELOC is the relocation to use for an entry in
     the set when generating a relocatable file, and is also used to
     get the size of the entry when generating an executable file.
     ABFD, SEC and VALUE identify the value to add to the set.  */
  bfd_boolean (*add_to_set)
    (struct bfd_link_info *, struct bfd_link_hash_entry *entry,
     bfd_reloc_code_real_type reloc, bfd *abfd, asection *sec, bfd_vma value);
  /* A function which is called when the name of a g++ constructor or
     destructor is found.  This is only called by some object file
     formats.  CONSTRUCTOR is TRUE for a constructor, FALSE for a
     destructor.  This will use BFD_RELOC_CTOR when generating a
     relocatable file.  NAME is the name of the symbol found.  ABFD,
     SECTION and VALUE are the value of the symbol.  */
  bfd_boolean (*constructor)
    (struct bfd_link_info *, bfd_boolean constructor, const char *name,
     bfd *abfd, asection *sec, bfd_vma value);
  /* A function which is called to issue a linker warning.  For
     example, this is called when there is a reference to a warning
     symbol.  WARNING is the warning to be issued.  SYMBOL is the name
     of the symbol which triggered the warning; it may be NULL if
     there is none.  ABFD, SECTION and ADDRESS identify the location
     which trigerred the warning; either ABFD or SECTION or both may
     be NULL if the location is not known.  */
  bfd_boolean (*warning)
    (struct bfd_link_info *, const char *warning, const char *symbol,
     bfd *abfd, asection *section, bfd_vma address);
  /* A function which is called when a relocation is attempted against
     an undefined symbol.  NAME is the symbol which is undefined.
     ABFD, SECTION and ADDRESS identify the location from which the
     reference is made. FATAL indicates whether an undefined symbol is
     a fatal error or not. In some cases SECTION may be NULL.  */
  bfd_boolean (*undefined_symbol)
    (struct bfd_link_info *, const char *name, bfd *abfd,
     asection *section, bfd_vma address, bfd_boolean fatal);
  /* A function which is called when a reloc overflow occurs. ENTRY is
     the link hash table entry for the symbol the reloc is against.
     NAME is the name of the local symbol or section the reloc is
     against, RELOC_NAME is the name of the relocation, and ADDEND is
     any addend that is used.  ABFD, SECTION and ADDRESS identify the
     location at which the overflow occurs; if this is the result of a
     bfd_section_reloc_link_order or bfd_symbol_reloc_link_order, then
     ABFD will be NULL.  */
  bfd_boolean (*reloc_overflow)
    (struct bfd_link_info *, struct bfd_link_hash_entry *entry,
     const char *name, const char *reloc_name, bfd_vma addend,
     bfd *abfd, asection *section, bfd_vma address);
  /* A function which is called when a dangerous reloc is performed.
     MESSAGE is an appropriate message.
     ABFD, SECTION and ADDRESS identify the location at which the
     problem occurred; if this is the result of a
     bfd_section_reloc_link_order or bfd_symbol_reloc_link_order, then
     ABFD will be NULL.  */
  bfd_boolean (*reloc_dangerous)
    (struct bfd_link_info *, const char *message,
     bfd *abfd, asection *section, bfd_vma address);
  /* A function which is called when a reloc is found to be attached
     to a symbol which is not being written out.  NAME is the name of
     the symbol.  ABFD, SECTION and ADDRESS identify the location of
     the reloc; if this is the result of a
     bfd_section_reloc_link_order or bfd_symbol_reloc_link_order, then
     ABFD will be NULL.  */
  bfd_boolean (*unattached_reloc)
    (struct bfd_link_info *, const char *name,
     bfd *abfd, asection *section, bfd_vma address);
  /* A function which is called when a symbol in notice_hash is
     defined or referenced.  NAME is the symbol.  ABFD, SECTION and
     ADDRESS are the value of the symbol.  If SECTION is
     bfd_und_section, this is a reference.  */
  bfd_boolean (*notice)
    (struct bfd_link_info *, const char *name,
     bfd *abfd, asection *section, bfd_vma address);
  /* Error or warning link info message.  */
  void (*einfo)
    (const char *fmt, ...);
  /* General link info message.  */
  void (*info)
    (const char *fmt, ...);
  /* Message to be printed in linker map file.  */
  void (*minfo)
    (const char *fmt, ...);
  /* This callback provides a chance for users of the BFD library to
     override its decision about whether to place two adjacent sections
     into the same segment.  */
  bfd_boolean (*override_segment_assignment)
    (struct bfd_link_info *, bfd * abfd,
     asection * current_section, asection * previous_section,
     bfd_boolean new_segment);
};

/* The linker builds link_order structures which tell the code how to
   include input data in the output file.  */

/* These are the types of link_order structures.  */

enum bfd_link_order_type
{
  bfd_undefined_link_order,	/* Undefined.  */
  bfd_indirect_link_order,	/* Built from a section.  */
  bfd_data_link_order,		/* Set to explicit data.  */
  bfd_section_reloc_link_order,	/* Relocate against a section.  */
  bfd_symbol_reloc_link_order	/* Relocate against a symbol.  */
};

/* This is the link_order structure itself.  These form a chain
   attached to the output section whose contents they are describing.  */

struct bfd_link_order
{
  /* Next link_order in chain.  */
  struct bfd_link_order *next;
  /* Type of link_order.  */
  enum bfd_link_order_type type;
  /* Offset within output section.  */
  bfd_vma offset;
  /* Size within output section.  */
  bfd_size_type size;
  /* Type specific information.  */
  union
    {
      struct
	{
	  /* Section to include.  If this is used, then
	     section->output_section must be the section the
	     link_order is attached to, section->output_offset must
	     equal the link_order offset field, and section->size
	     must equal the link_order size field.  Maybe these
	     restrictions should be relaxed someday.  */
	  asection *section;
	} indirect;
      struct
	{
	  /* Size of contents, or zero when contents size == size
	     within output section.
	     A non-zero value allows filling of the output section
	     with an arbitrary repeated pattern.  */
	  unsigned int size;
	  /* Data to put into file.  */
	  bfd_byte *contents;
	} data;
      struct
	{
	  /* Description of reloc to generate.  Used for
	     bfd_section_reloc_link_order and
	     bfd_symbol_reloc_link_order.  */
	  struct bfd_link_order_reloc *p;
	} reloc;
    } u;
};

/* A linker order of type bfd_section_reloc_link_order or
   bfd_symbol_reloc_link_order means to create a reloc against a
   section or symbol, respectively.  This is used to implement -Ur to
   generate relocs for the constructor tables.  The
   bfd_link_order_reloc structure describes the reloc that BFD should
   create.  It is similar to a arelent, but I didn't use arelent
   because the linker does not know anything about most symbols, and
   any asymbol structure it creates will be partially meaningless.
   This information could logically be in the bfd_link_order struct,
   but I didn't want to waste the space since these types of relocs
   are relatively rare.  */

struct bfd_link_order_reloc
{
  /* Reloc type.  */
  bfd_reloc_code_real_type reloc;

  union
    {
      /* For type bfd_section_reloc_link_order, this is the section
	 the reloc should be against.  This must be a section in the
	 output BFD, not any of the input BFDs.  */
      asection *section;
      /* For type bfd_symbol_reloc_link_order, this is the name of the
	 symbol the reloc should be against.  */
      const char *name;
    } u;

  /* Addend to use.  The object file should contain zero.  The BFD
     backend is responsible for filling in the contents of the object
     file correctly.  For some object file formats (e.g., COFF) the
     addend must be stored into in the object file, and for some
     (e.g., SPARC a.out) it is kept in the reloc.  */
  bfd_vma addend;
};

/* Allocate a new link_order for a section.  */
extern struct bfd_link_order *bfd_new_link_order (bfd *, asection *);

/* These structures are used to describe version information for the
   ELF linker.  These structures could be manipulated entirely inside
   BFD, but it would be a pain.  Instead, the regular linker sets up
   these structures, and then passes them into BFD.  */

/* Glob pattern for a version.  */

struct bfd_elf_version_expr
{
  /* Next glob pattern for this version.  */
  struct bfd_elf_version_expr *next;
  /* Glob pattern.  */
  const char *pattern;
  /* NULL for a glob pattern, otherwise a straight symbol.  */
  const char *symbol;
  /* Defined by ".symver".  */
  unsigned int symver : 1;
  /* Defined by version script.  */
  unsigned int script : 1;
  /* Pattern type.  */
#define BFD_ELF_VERSION_C_TYPE		1
#define BFD_ELF_VERSION_CXX_TYPE	2
#define BFD_ELF_VERSION_JAVA_TYPE	4
  unsigned int mask : 3;
};

struct bfd_elf_version_expr_head
{
  /* List of all patterns, both wildcards and non-wildcards.  */
  struct bfd_elf_version_expr *list;
  /* Hash table for non-wildcards.  */
  void *htab;
  /* Remaining patterns.  */
  struct bfd_elf_version_expr *remaining;
  /* What kind of pattern types are present in list (bitmask).  */
  unsigned int mask;
};

/* Version dependencies.  */

struct bfd_elf_version_deps
{
  /* Next dependency for this version.  */
  struct bfd_elf_version_deps *next;
  /* The version which this version depends upon.  */
  struct bfd_elf_version_tree *version_needed;
};

/* A node in the version tree.  */

struct bfd_elf_version_tree
{
  /* Next version.  */
  struct bfd_elf_version_tree *next;
  /* Name of this version.  */
  const char *name;
  /* Version number.  */
  unsigned int vernum;
  /* Regular expressions for global symbols in this version.  */
  struct bfd_elf_version_expr_head globals;
  /* Regular expressions for local symbols in this version.  */
  struct bfd_elf_version_expr_head locals;
  /* List of versions which this version depends upon.  */
  struct bfd_elf_version_deps *deps;
  /* Index of the version name.  This is used within BFD.  */
  unsigned int name_indx;
  /* Whether this version tree was used.  This is used within BFD.  */
  int used;
  /* Matching hook.  */
  struct bfd_elf_version_expr *(*match)
    (struct bfd_elf_version_expr_head *head,
     struct bfd_elf_version_expr *prev, const char *sym);
};

struct bfd_elf_dynamic_list
{
  struct bfd_elf_version_expr_head head;
  struct bfd_elf_version_expr *(*match)
    (struct bfd_elf_version_expr_head *head,
     struct bfd_elf_version_expr *prev, const char *sym);
};

#endif

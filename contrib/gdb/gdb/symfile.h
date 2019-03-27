/* Definitions for reading symbol files into GDB.

   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#if !defined (SYMFILE_H)
#define SYMFILE_H

/* This file requires that you first include "bfd.h".  */

/* Opaque declarations.  */
struct section_table;
struct objfile;
struct obstack;
struct block;

/* Partial symbols are stored in the psymbol_cache and pointers to
   them are kept in a dynamically grown array that is obtained from
   malloc and grown as necessary via realloc.  Each objfile typically
   has two of these, one for global symbols and one for static
   symbols.  Although this adds a level of indirection for storing or
   accessing the partial symbols, it allows us to throw away duplicate
   psymbols and set all pointers to the single saved instance.  */

struct psymbol_allocation_list
{

  /* Pointer to beginning of dynamically allocated array of pointers
     to partial symbols.  The array is dynamically expanded as
     necessary to accommodate more pointers.  */

  struct partial_symbol **list;

  /* Pointer to next available slot in which to store a pointer to a
     partial symbol.  */

  struct partial_symbol **next;

  /* Number of allocated pointer slots in current dynamic array (not
     the number of bytes of storage).  The "next" pointer will always
     point somewhere between list[0] and list[size], and when at
     list[size] the array will be expanded on the next attempt to
     store a pointer.  */

  int size;
};

/* Define an array of addresses to accommodate non-contiguous dynamic
   loading of modules.  This is for use when entering commands, so we
   can keep track of the section names until we read the file and can
   map them to bfd sections.  This structure is also used by solib.c
   to communicate the section addresses in shared objects to
   symbol_file_add ().  */

struct section_addr_info
{
  /* The number of sections for which address information is
     available.  */
  size_t num_sections;
  /* Sections whose names are file format dependent. */
  struct other_sections
  {
    CORE_ADDR addr;
    char *name;
    int sectindex;
  } other[1];
};

/* Structure to keep track of symbol reading functions for various
   object file types.  */

struct sym_fns
{

  /* BFD flavour that we handle, or (as a special kludge, see
     xcoffread.c, (enum bfd_flavour)-1 for xcoff).  */

  enum bfd_flavour sym_flavour;

  /* Initializes anything that is global to the entire symbol table.
     It is called during symbol_file_add, when we begin debugging an
     entirely new program.  */

  void (*sym_new_init) (struct objfile *);

  /* Reads any initial information from a symbol file, and initializes
     the struct sym_fns SF in preparation for sym_read().  It is
     called every time we read a symbol file for any reason.  */

  void (*sym_init) (struct objfile *);

  /* sym_read (objfile, mainline) Reads a symbol file into a psymtab
     (or possibly a symtab).  OBJFILE is the objfile struct for the
     file we are reading.  MAINLINE is 1 if this is the main symbol
     table being read, and 0 if a secondary symbol file (e.g. shared
     library or dynamically loaded file) is being read.  */

  void (*sym_read) (struct objfile *, int);

  /* Called when we are finished with an objfile.  Should do all
     cleanup that is specific to the object file format for the
     particular objfile.  */

  void (*sym_finish) (struct objfile *);

  /* This function produces a file-dependent section_offsets
     structure, allocated in the objfile's storage, and based on the
     parameter.  The parameter is currently a CORE_ADDR (FIXME!) for
     backward compatibility with the higher levels of GDB.  It should
     probably be changed to a string, where NULL means the default,
     and others are parsed in a file dependent way.  */

  void (*sym_offsets) (struct objfile *, struct section_addr_info *);

  /* Finds the next struct sym_fns.  They are allocated and
     initialized in whatever module implements the functions pointed
     to; an initializer calls add_symtab_fns to add them to the global
     chain.  */

  struct sym_fns *next;

};

/* The default version of sym_fns.sym_offsets for readers that don't
   do anything special.  */

extern void default_symfile_offsets (struct objfile *objfile,
				     struct section_addr_info *);


extern void extend_psymbol_list (struct psymbol_allocation_list *,
				 struct objfile *);

/* Add any kind of symbol to a psymbol_allocation_list.  */

/* #include "demangle.h" */

extern const
struct partial_symbol *add_psymbol_to_list (char *, int, domain_enum,
					    enum address_class,
					    struct psymbol_allocation_list *,
					    long, CORE_ADDR,
					    enum language, struct objfile *);

extern void add_psymbol_with_dem_name_to_list (char *, int, char *, int,
					       domain_enum,
					       enum address_class,
					       struct psymbol_allocation_list
					       *, long, CORE_ADDR,
					       enum language,
					       struct objfile *);


extern void init_psymbol_list (struct objfile *, int);

extern void sort_pst_symbols (struct partial_symtab *);

extern struct symtab *allocate_symtab (char *, struct objfile *);

extern int free_named_symtabs (char *);

extern void fill_in_vptr_fieldno (struct type *);

extern void add_symtab_fns (struct sym_fns *);

extern void syms_from_objfile (struct objfile *,
			       struct section_addr_info *,
			       struct section_offsets *, int, int, int);

extern void new_symfile_objfile (struct objfile *, int, int);

extern struct objfile *symbol_file_add (char *, int,
					struct section_addr_info *, int, int);

/* Create a new section_addr_info, with room for NUM_SECTIONS.  */

extern struct section_addr_info *alloc_section_addr_info (size_t
							  num_sections);

/* Build (allocate and populate) a section_addr_info struct from an
   existing section table.  */

extern struct section_addr_info
  *build_section_addr_info_from_section_table (const struct section_table
					       *start,
					       const struct section_table
					       *end);

/* Free all memory allocated by
   build_section_addr_info_from_section_table.  */

extern void free_section_addr_info (struct section_addr_info *);


extern struct partial_symtab *start_psymtab_common (struct objfile *,
						    struct section_offsets *,
						    char *, CORE_ADDR,
						    struct partial_symbol **,
						    struct partial_symbol **);

/* Make a copy of the string at PTR with SIZE characters in the symbol
   obstack (and add a null character at the end in the copy).  Returns
   the address of the copy.  */

extern char *obsavestring (const char *, int, struct obstack *);

/* Concatenate strings S1, S2 and S3; return the new string.  Space is
   found in the OBSTACKP  */

extern char *obconcat (struct obstack *obstackp, const char *, const char *,
		       const char *);

			/*   Variables   */

/* If non-zero, shared library symbols will be added automatically
   when the inferior is created, new libraries are loaded, or when
   attaching to the inferior.  This is almost always what users will
   want to have happen; but for very large programs, the startup time
   will be excessive, and so if this is a problem, the user can clear
   this flag and then add the shared library symbols as needed.  Note
   that there is a potential for confusion, since if the shared
   library symbols are not loaded, commands like "info fun" will *not*
   report all the functions that are actually present.  */

extern int auto_solib_add;

/* For systems that support it, a threshold size in megabytes.  If
   automatically adding a new library's symbol table to those already
   known to the debugger would cause the total shared library symbol
   size to exceed this threshhold, then the shlib's symbols are not
   added.  The threshold is ignored if the user explicitly asks for a
   shlib to be added, such as when using the "sharedlibrary" command.  */

extern int auto_solib_limit;

/* From symfile.c */

extern struct partial_symtab *allocate_psymtab (char *, struct objfile *);

extern void discard_psymtab (struct partial_symtab *);

extern void find_lowest_section (bfd *, asection *, void *);

extern bfd *symfile_bfd_open (char *);

extern int get_section_index (struct objfile *, char *);

/* Utility functions for overlay sections: */
extern enum overlay_debugging_state
{
  ovly_off,
  ovly_on,
  ovly_auto
} overlay_debugging;
extern int overlay_cache_invalid;

/* Return the "mapped" overlay section containing the PC.  */
extern asection *find_pc_mapped_section (CORE_ADDR);

/* Return any overlay section containing the PC (even in its LMA
   region).  */
extern asection *find_pc_overlay (CORE_ADDR);

/* Return true if the section is an overlay.  */
extern int section_is_overlay (asection *);

/* Return true if the overlay section is currently "mapped".  */
extern int section_is_mapped (asection *);

/* Return true if pc belongs to section's VMA.  */
extern CORE_ADDR pc_in_mapped_range (CORE_ADDR, asection *);

/* Return true if pc belongs to section's LMA.  */
extern CORE_ADDR pc_in_unmapped_range (CORE_ADDR, asection *);

/* Map an address from a section's LMA to its VMA.  */
extern CORE_ADDR overlay_mapped_address (CORE_ADDR, asection *);

/* Map an address from a section's VMA to its LMA.  */
extern CORE_ADDR overlay_unmapped_address (CORE_ADDR, asection *);

/* Convert an address in an overlay section (force into VMA range).  */
extern CORE_ADDR symbol_overlayed_address (CORE_ADDR, asection *);

/* Load symbols from a file.  */
extern void symbol_file_add_main (char *args, int from_tty);

/* Clear GDB symbol tables.  */
extern void symbol_file_clear (int from_tty);

extern bfd_byte *symfile_relocate_debug_section (bfd *abfd, asection *sectp,
						 bfd_byte * buf);

/* From dwarfread.c */

extern void dwarf_build_psymtabs (struct objfile *, int, file_ptr,
				  unsigned int, file_ptr, unsigned int);

/* From dwarf2read.c */

extern int dwarf2_has_info (bfd *abfd);

extern void dwarf2_build_psymtabs (struct objfile *, int);
extern void dwarf2_build_frame_info (struct objfile *);

/* From mdebugread.c */

/* Hack to force structures to exist before use in parameter list.  */
struct ecoff_debug_hack
{
  struct ecoff_debug_swap *a;
  struct ecoff_debug_info *b;
};

extern void mdebug_build_psymtabs (struct objfile *,
				   const struct ecoff_debug_swap *,
				   struct ecoff_debug_info *);

extern void elfmdebug_build_psymtabs (struct objfile *,
				      const struct ecoff_debug_swap *,
				      asection *);

#endif /* !defined(SYMFILE_H) */

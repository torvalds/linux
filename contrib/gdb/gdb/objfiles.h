/* Definitions for symbol file management in GDB.

   Copyright 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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

#if !defined (OBJFILES_H)
#define OBJFILES_H

#include "gdb_obstack.h"	/* For obstack internals.  */
#include "symfile.h"		/* For struct psymbol_allocation_list */

struct bcache;
struct htab;
struct symtab;
struct objfile_data;

/* This structure maintains information on a per-objfile basis about the
   "entry point" of the objfile, and the scope within which the entry point
   exists.  It is possible that gdb will see more than one objfile that is
   executable, each with its own entry point.

   For example, for dynamically linked executables in SVR4, the dynamic linker
   code is contained within the shared C library, which is actually executable
   and is run by the kernel first when an exec is done of a user executable
   that is dynamically linked.  The dynamic linker within the shared C library
   then maps in the various program segments in the user executable and jumps
   to the user executable's recorded entry point, as if the call had been made
   directly by the kernel.

   The traditional gdb method of using this info is to use the
   recorded entry point to set the variables
   deprecated_entry_file_lowpc and deprecated_entry_file_highpc from
   the debugging information, where these values are the starting
   address (inclusive) and ending address (exclusive) of the
   instruction space in the executable which correspond to the
   "startup file", I.E. crt0.o in most cases.  This file is assumed to
   be a startup file and frames with pc's inside it are treated as
   nonexistent.  Setting these variables is necessary so that
   backtraces do not fly off the bottom of the stack.

   NOTE: cagney/2003-09-09: It turns out that this "traditional"
   method doesn't work.  Corinna writes: ``It turns out that the call
   to deprecated_inside_entry_file destroys a meaningful backtrace
   under some conditions.  E. g. the backtrace tests in the asm-source
   testcase are broken for some targets.  In this test the functions
   are all implemented as part of one file and the testcase is not
   necessarily linked with a start file (depending on the target).
   What happens is, that the first frame is printed normaly and
   following frames are treated as being inside the enttry file then.
   This way, only the #0 frame is printed in the backtrace output.''
   Ref "frame.c" "NOTE: vinschen/2003-04-01".

   Gdb also supports an alternate method to avoid running off the bottom
   of the stack.

   There are two frames that are "special", the frame for the function
   containing the process entry point, since it has no predecessor frame,
   and the frame for the function containing the user code entry point
   (the main() function), since all the predecessor frames are for the
   process startup code.  Since we have no guarantee that the linked
   in startup modules have any debugging information that gdb can use,
   we need to avoid following frame pointers back into frames that might
   have been built in the startup code, as we might get hopelessly 
   confused.  However, we almost always have debugging information
   available for main().

   These variables are used to save the range of PC values which are
   valid within the main() function and within the function containing
   the process entry point.  If we always consider the frame for
   main() as the outermost frame when debugging user code, and the
   frame for the process entry point function as the outermost frame
   when debugging startup code, then all we have to do is have
   DEPRECATED_FRAME_CHAIN_VALID return false whenever a frame's
   current PC is within the range specified by these variables.  In
   essence, we set "ceilings" in the frame chain beyond which we will
   not proceed when following the frame chain back up the stack.

   A nice side effect is that we can still debug startup code without
   running off the end of the frame chain, assuming that we have usable
   debugging information in the startup modules, and if we choose to not
   use the block at main, or can't find it for some reason, everything
   still works as before.  And if we have no startup code debugging
   information but we do have usable information for main(), backtraces
   from user code don't go wandering off into the startup code.  */

struct entry_info
  {

    /* The value we should use for this objects entry point.
       The illegal/unknown value needs to be something other than 0, ~0
       for instance, which is much less likely than 0. */

    CORE_ADDR entry_point;

#define INVALID_ENTRY_POINT (~0)	/* ~0 will not be in any file, we hope.  */

    /* Start (inclusive) and end (exclusive) of function containing the
       entry point. */

    CORE_ADDR entry_func_lowpc;
    CORE_ADDR entry_func_highpc;

    /* Start (inclusive) and end (exclusive) of object file containing the
       entry point. */

    CORE_ADDR deprecated_entry_file_lowpc;
    CORE_ADDR deprecated_entry_file_highpc;

    /* Start (inclusive) and end (exclusive) of the user code main() function. */

    CORE_ADDR main_func_lowpc;
    CORE_ADDR main_func_highpc;

/* Use these values when any of the above ranges is invalid.  */

/* We use these values because it guarantees that there is no number that is
   both >= LOWPC && < HIGHPC.  It is also highly unlikely that 3 is a valid
   module or function start address (as opposed to 0).  */

#define INVALID_ENTRY_LOWPC (3)
#define INVALID_ENTRY_HIGHPC (1)

  };

/* Sections in an objfile.

   It is strange that we have both this notion of "sections"
   and the one used by section_offsets.  Section as used
   here, (currently at least) means a BFD section, and the sections
   are set up from the BFD sections in allocate_objfile.

   The sections in section_offsets have their meaning determined by
   the symbol format, and they are set up by the sym_offsets function
   for that symbol file format.

   I'm not sure this could or should be changed, however.  */

struct obj_section
  {
    CORE_ADDR addr;		/* lowest address in section */
    CORE_ADDR endaddr;		/* 1+highest address in section */

    /* This field is being used for nefarious purposes by syms_from_objfile.
       It is said to be redundant with section_offsets; it's not really being
       used that way, however, it's some sort of hack I don't understand
       and am not going to try to eliminate (yet, anyway).  FIXME.

       It was documented as "offset between (end)addr and actual memory
       addresses", but that's not true; addr & endaddr are actual memory
       addresses.  */
    CORE_ADDR offset;

    struct bfd_section *the_bfd_section;	/* BFD section pointer */

    /* Objfile this section is part of.  */
    struct objfile *objfile;

    /* True if this "overlay section" is mapped into an "overlay region". */
    int ovly_mapped;
  };

/* An import entry contains information about a symbol that
   is used in this objfile but not defined in it, and so needs
   to be imported from some other objfile */
/* Currently we just store the name; no attributes. 1997-08-05 */
typedef char *ImportEntry;


/* An export entry contains information about a symbol that
   is defined in this objfile and available for use in other
   objfiles */
typedef struct
  {
    char *name;			/* name of exported symbol */
    int address;		/* offset subject to relocation */
    /* Currently no other attributes 1997-08-05 */
  }
ExportEntry;


/* The "objstats" structure provides a place for gdb to record some
   interesting information about its internal state at runtime, on a
   per objfile basis, such as information about the number of symbols
   read, size of string table (if any), etc. */

struct objstats
  {
    int n_minsyms;		/* Number of minimal symbols read */
    int n_psyms;		/* Number of partial symbols read */
    int n_syms;			/* Number of full symbols read */
    int n_stabs;		/* Number of ".stabs" read (if applicable) */
    int n_types;		/* Number of types */
    int sz_strtab;		/* Size of stringtable, (if applicable) */
  };

#define OBJSTAT(objfile, expr) (objfile -> stats.expr)
#define OBJSTATS struct objstats stats
extern void print_objfile_statistics (void);
extern void print_symbol_bcache_statistics (void);

/* Number of entries in the minimal symbol hash table.  */
#define MINIMAL_SYMBOL_HASH_SIZE 2039

/* Master structure for keeping track of each file from which
   gdb reads symbols.  There are several ways these get allocated: 1.
   The main symbol file, symfile_objfile, set by the symbol-file command,
   2.  Additional symbol files added by the add-symbol-file command,
   3.  Shared library objfiles, added by ADD_SOLIB,  4.  symbol files
   for modules that were loaded when GDB attached to a remote system
   (see remote-vx.c).  */

struct objfile
  {

    /* All struct objfile's are chained together by their next pointers.
       The global variable "object_files" points to the first link in this
       chain.

       FIXME:  There is a problem here if the objfile is reusable, and if
       multiple users are to be supported.  The problem is that the objfile
       list is linked through a member of the objfile struct itself, which
       is only valid for one gdb process.  The list implementation needs to
       be changed to something like:

       struct list {struct list *next; struct objfile *objfile};

       where the list structure is completely maintained separately within
       each gdb process. */

    struct objfile *next;

    /* The object file's name, tilde-expanded and absolute.
       Malloc'd; free it if you free this struct.  */

    char *name;

    /* Some flag bits for this objfile. */

    unsigned short flags;

    /* Each objfile points to a linked list of symtabs derived from this file,
       one symtab structure for each compilation unit (source file).  Each link
       in the symtab list contains a backpointer to this objfile. */

    struct symtab *symtabs;

    /* Each objfile points to a linked list of partial symtabs derived from
       this file, one partial symtab structure for each compilation unit
       (source file). */

    struct partial_symtab *psymtabs;

    /* List of freed partial symtabs, available for re-use */

    struct partial_symtab *free_psymtabs;

    /* The object file's BFD.  Can be null if the objfile contains only
       minimal symbols, e.g. the run time common symbols for SunOS4.  */

    bfd *obfd;

    /* The modification timestamp of the object file, as of the last time
       we read its symbols.  */

    long mtime;

    /* Obstack to hold objects that should be freed when we load a new symbol
       table from this object file. */

    struct obstack objfile_obstack; 

    /* A byte cache where we can stash arbitrary "chunks" of bytes that
       will not change. */

    struct bcache *psymbol_cache;	/* Byte cache for partial syms */
    struct bcache *macro_cache;          /* Byte cache for macros */

    /* Hash table for mapping symbol names to demangled names.  Each
       entry in the hash table is actually two consecutive strings,
       both null-terminated; the first one is a mangled or linkage
       name, and the second is the demangled name or just a zero byte
       if the name doesn't demangle.  */
    struct htab *demangled_names_hash;

    /* Vectors of all partial symbols read in from file.  The actual data
       is stored in the objfile_obstack. */

    struct psymbol_allocation_list global_psymbols;
    struct psymbol_allocation_list static_psymbols;

    /* Each file contains a pointer to an array of minimal symbols for all
       global symbols that are defined within the file.  The array is terminated
       by a "null symbol", one that has a NULL pointer for the name and a zero
       value for the address.  This makes it easy to walk through the array
       when passed a pointer to somewhere in the middle of it.  There is also
       a count of the number of symbols, which does not include the terminating
       null symbol.  The array itself, as well as all the data that it points
       to, should be allocated on the objfile_obstack for this file. */

    struct minimal_symbol *msymbols;
    int minimal_symbol_count;

    /* This is a hash table used to index the minimal symbols by name.  */

    struct minimal_symbol *msymbol_hash[MINIMAL_SYMBOL_HASH_SIZE];

    /* This hash table is used to index the minimal symbols by their
       demangled names.  */

    struct minimal_symbol *msymbol_demangled_hash[MINIMAL_SYMBOL_HASH_SIZE];

    /* For object file formats which don't specify fundamental types, gdb
       can create such types.  For now, it maintains a vector of pointers
       to these internally created fundamental types on a per objfile basis,
       however it really should ultimately keep them on a per-compilation-unit
       basis, to account for linkage-units that consist of a number of
       compilation units that may have different fundamental types, such as
       linking C modules with ADA modules, or linking C modules that are
       compiled with 32-bit ints with C modules that are compiled with 64-bit
       ints (not inherently evil with a smarter linker). */

    struct type **fundamental_types;

    /* The mmalloc() malloc-descriptor for this objfile if we are using
       the memory mapped malloc() package to manage storage for this objfile's
       data.  NULL if we are not. */

    void *md;

    /* The file descriptor that was used to obtain the mmalloc descriptor
       for this objfile.  If we call mmalloc_detach with the malloc descriptor
       we should then close this file descriptor. */

    int mmfd;

    /* Structure which keeps track of functions that manipulate objfile's
       of the same type as this objfile.  I.E. the function to read partial
       symbols for example.  Note that this structure is in statically
       allocated memory, and is shared by all objfiles that use the
       object module reader of this type. */

    struct sym_fns *sf;

    /* The per-objfile information about the entry point, the scope (file/func)
       containing the entry point, and the scope of the user's main() func. */

    struct entry_info ei;

    /* Information about stabs.  Will be filled in with a dbx_symfile_info
       struct by those readers that need it. */

    struct dbx_symfile_info *sym_stab_info;

    /* Hook for information for use by the symbol reader (currently used
       for information shared by sym_init and sym_read).  It is
       typically a pointer to malloc'd memory.  The symbol reader's finish
       function is responsible for freeing the memory thusly allocated.  */

    void *sym_private;

    /* Hook for target-architecture-specific information.  This must
       point to memory allocated on one of the obstacks in this objfile,
       so that it gets freed automatically when reading a new object
       file. */

    void *obj_private;

    /* Per objfile data-pointers required by other GDB modules.  */
    /* FIXME: kettenis/20030711: This mechanism could replace
       sym_stab_info, sym_private and obj_private entirely.  */

    void **data;
    unsigned num_data;

    /* Set of relocation offsets to apply to each section.
       Currently on the objfile_obstack (which makes no sense, but I'm
       not sure it's harming anything).

       These offsets indicate that all symbols (including partial and
       minimal symbols) which have been read have been relocated by this
       much.  Symbols which are yet to be read need to be relocated by
       it.  */

    struct section_offsets *section_offsets;
    int num_sections;

    /* Indexes in the section_offsets array. These are initialized by the
       *_symfile_offsets() family of functions (som_symfile_offsets,
       xcoff_symfile_offsets, default_symfile_offsets). In theory they
       should correspond to the section indexes used by bfd for the
       current objfile. The exception to this for the time being is the
       SOM version. */

    int sect_index_text;
    int sect_index_data;
    int sect_index_bss;
    int sect_index_rodata;

    /* These pointers are used to locate the section table, which
       among other things, is used to map pc addresses into sections.
       SECTIONS points to the first entry in the table, and
       SECTIONS_END points to the first location past the last entry
       in the table.  Currently the table is stored on the
       objfile_obstack (which makes no sense, but I'm not sure it's
       harming anything).  */

    struct obj_section
     *sections, *sections_end;

    /* Imported symbols */
    /* FIXME: ezannoni 2004-02-10: This is just SOM (HP) specific (see
       somread.c). It should not pollute generic objfiles.  */
    ImportEntry *import_list;
    int import_list_size;

    /* Exported symbols */
    /* FIXME: ezannoni 2004-02-10: This is just SOM (HP) specific (see
       somread.c). It should not pollute generic objfiles.  */
    ExportEntry *export_list;
    int export_list_size;

    /* Link to objfile that contains the debug symbols for this one.
       One is loaded if this file has an debug link to an existing
       debug file with the right checksum */
    struct objfile *separate_debug_objfile;

    /* If this is a separate debug object, this is used as a link to the
       actual executable objfile. */
    struct objfile *separate_debug_objfile_backlink;
    
    /* Place to stash various statistics about this objfile */
      OBJSTATS;

    /* A symtab that the C++ code uses to stash special symbols
       associated to namespaces.  */

    /* FIXME/carlton-2003-06-27: Delete this in a few years once
       "possible namespace symbols" go away.  */
    struct symtab *cp_namespace_symtab;
  };

/* Defines for the objfile flag word. */

/* When using mapped/remapped predigested gdb symbol information, we need
   a flag that indicates that we have previously done an initial symbol
   table read from this particular objfile.  We can't just look for the
   absence of any of the three symbol tables (msymbols, psymtab, symtab)
   because if the file has no symbols for example, none of these will
   exist. */

#define OBJF_SYMS	(1 << 1)	/* Have tried to read symbols */

/* When an object file has its functions reordered (currently Irix-5.2
   shared libraries exhibit this behaviour), we will need an expensive
   algorithm to locate a partial symtab or symtab via an address.
   To avoid this penalty for normal object files, we use this flag,
   whose setting is determined upon symbol table read in.  */

#define OBJF_REORDERED	(1 << 2)	/* Functions are reordered */

/* Distinguish between an objfile for a shared library and a "vanilla"
   objfile. (If not set, the objfile may still actually be a solib.
   This can happen if the user created the objfile by using the
   add-symbol-file command.  GDB doesn't in that situation actually
   check whether the file is a solib.  Rather, the target's
   implementation of the solib interface is responsible for setting
   this flag when noticing solibs used by an inferior.)  */

#define OBJF_SHARED     (1 << 3)	/* From a shared library */

/* User requested that this objfile be read in it's entirety. */

#define OBJF_READNOW	(1 << 4)	/* Immediate full read */

/* This objfile was created because the user explicitly caused it
   (e.g., used the add-symbol-file command).  This bit offers a way
   for run_command to remove old objfile entries which are no longer
   valid (i.e., are associated with an old inferior), but to preserve
   ones that the user explicitly loaded via the add-symbol-file
   command. */

#define OBJF_USERLOADED	(1 << 5)	/* User loaded */

/* The object file that the main symbol table was loaded from (e.g. the
   argument to the "symbol-file" or "file" command).  */

extern struct objfile *symfile_objfile;

/* The object file that contains the runtime common minimal symbols
   for SunOS4. Note that this objfile has no associated BFD.  */

extern struct objfile *rt_common_objfile;

/* When we need to allocate a new type, we need to know which objfile_obstack
   to allocate the type on, since there is one for each objfile.  The places
   where types are allocated are deeply buried in function call hierarchies
   which know nothing about objfiles, so rather than trying to pass a
   particular objfile down to them, we just do an end run around them and
   set current_objfile to be whatever objfile we expect to be using at the
   time types are being allocated.  For instance, when we start reading
   symbols for a particular objfile, we set current_objfile to point to that
   objfile, and when we are done, we set it back to NULL, to ensure that we
   never put a type someplace other than where we are expecting to put it.
   FIXME:  Maybe we should review the entire type handling system and
   see if there is a better way to avoid this problem. */

extern struct objfile *current_objfile;

/* All known objfiles are kept in a linked list.  This points to the
   root of this list. */

extern struct objfile *object_files;

/* Declarations for functions defined in objfiles.c */

extern struct objfile *allocate_objfile (bfd *, int);

extern void init_entry_point_info (struct objfile *);

extern CORE_ADDR entry_point_address (void);

extern int build_objfile_section_table (struct objfile *);

extern void terminate_minimal_symbol_table (struct objfile *objfile);

extern void put_objfile_before (struct objfile *, struct objfile *);

extern void objfile_to_front (struct objfile *);

extern void unlink_objfile (struct objfile *);

extern void free_objfile (struct objfile *);

extern struct cleanup *make_cleanup_free_objfile (struct objfile *);

extern void free_all_objfiles (void);

extern void objfile_relocate (struct objfile *, struct section_offsets *);

extern int have_partial_symbols (void);

extern int have_full_symbols (void);

/* This operation deletes all objfile entries that represent solibs that
   weren't explicitly loaded by the user, via e.g., the add-symbol-file
   command.
 */
extern void objfile_purge_solibs (void);

/* Functions for dealing with the minimal symbol table, really a misc
   address<->symbol mapping for things we don't have debug symbols for.  */

extern int have_minimal_symbols (void);

extern struct obj_section *find_pc_section (CORE_ADDR pc);

extern struct obj_section *find_pc_sect_section (CORE_ADDR pc,
						 asection * section);

extern int in_plt_section (CORE_ADDR, char *);

extern int is_in_import_list (char *, struct objfile *);

/* Keep a registry of per-objfile data-pointers required by other GDB
   modules.  */

extern const struct objfile_data *register_objfile_data (void);
extern void clear_objfile_data (struct objfile *objfile);
extern void set_objfile_data (struct objfile *objfile,
			      const struct objfile_data *data, void *value);
extern void *objfile_data (struct objfile *objfile,
			   const struct objfile_data *data);


/* Traverse all object files.  ALL_OBJFILES_SAFE works even if you delete
   the objfile during the traversal.  */

#define	ALL_OBJFILES(obj) \
  for ((obj) = object_files; (obj) != NULL; (obj) = (obj)->next)

#define	ALL_OBJFILES_SAFE(obj,nxt) \
  for ((obj) = object_files; 	   \
       (obj) != NULL? ((nxt)=(obj)->next,1) :0;	\
       (obj) = (nxt))

/* Traverse all symtabs in one objfile.  */

#define	ALL_OBJFILE_SYMTABS(objfile, s) \
    for ((s) = (objfile) -> symtabs; (s) != NULL; (s) = (s) -> next)

/* Traverse all psymtabs in one objfile.  */

#define	ALL_OBJFILE_PSYMTABS(objfile, p) \
    for ((p) = (objfile) -> psymtabs; (p) != NULL; (p) = (p) -> next)

/* Traverse all minimal symbols in one objfile.  */

#define	ALL_OBJFILE_MSYMBOLS(objfile, m) \
    for ((m) = (objfile) -> msymbols; DEPRECATED_SYMBOL_NAME(m) != NULL; (m)++)

/* Traverse all symtabs in all objfiles.  */

#define	ALL_SYMTABS(objfile, s) \
  ALL_OBJFILES (objfile)	 \
    ALL_OBJFILE_SYMTABS (objfile, s)

/* Traverse all psymtabs in all objfiles.  */

#define	ALL_PSYMTABS(objfile, p) \
  ALL_OBJFILES (objfile)	 \
    ALL_OBJFILE_PSYMTABS (objfile, p)

/* Traverse all minimal symbols in all objfiles.  */

#define	ALL_MSYMBOLS(objfile, m) \
  ALL_OBJFILES (objfile)	 \
    ALL_OBJFILE_MSYMBOLS (objfile, m)

#define ALL_OBJFILE_OSECTIONS(objfile, osect)	\
  for (osect = objfile->sections; osect < objfile->sections_end; osect++)

#define ALL_OBJSECTIONS(objfile, osect)		\
  ALL_OBJFILES (objfile)			\
    ALL_OBJFILE_OSECTIONS (objfile, osect)

#define SECT_OFF_DATA(objfile) \
     ((objfile->sect_index_data == -1) \
      ? (internal_error (__FILE__, __LINE__, "sect_index_data not initialized"), -1) \
      : objfile->sect_index_data)

#define SECT_OFF_RODATA(objfile) \
     ((objfile->sect_index_rodata == -1) \
      ? (internal_error (__FILE__, __LINE__, "sect_index_rodata not initialized"), -1) \
      : objfile->sect_index_rodata)

#define SECT_OFF_TEXT(objfile) \
     ((objfile->sect_index_text == -1) \
      ? (internal_error (__FILE__, __LINE__, "sect_index_text not initialized"), -1) \
      : objfile->sect_index_text)

/* Sometimes the .bss section is missing from the objfile, so we don't
   want to die here. Let the users of SECT_OFF_BSS deal with an
   uninitialized section index. */
#define SECT_OFF_BSS(objfile) (objfile)->sect_index_bss

#endif /* !defined (OBJFILES_H) */

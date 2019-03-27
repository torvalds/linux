/* Include file for stabs debugging format support functions.
   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1999, 2000, 2001, 2002, 2003 Free Software Foundation, Inc.

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

struct objfile;

/* Definitions, prototypes, etc for stabs debugging format support
   functions.

   Variables declared in this file can be defined by #define-ing
   the name EXTERN to null.  It is used to declare variables that
   are normally extern, but which get defined in a single module
   using this technique.  */

#ifndef EXTERN
#define	EXTERN extern
#endif

/* Hash table of global symbols whose values are not known yet.
   They are chained thru the SYMBOL_VALUE_CHAIN, since we don't
   have the correct data for that slot yet.

   The use of the LOC_BLOCK code in this chain is nonstandard--
   it refers to a FORTRAN common block rather than the usual meaning, and
   the such LOC_BLOCK symbols use their fields in nonstandard ways.  */

EXTERN struct symbol *global_sym_chain[HASHSIZE];

extern void common_block_start (char *, struct objfile *);
extern void common_block_end (struct objfile *);

/* Kludge for xcoffread.c */

struct pending_stabs
  {
    int count;
    int length;
    char *stab[1];
  };

EXTERN struct pending_stabs *global_stabs;

/* The type code that process_one_symbol saw on its previous invocation.
   Used to detect pairs of N_SO symbols. */

EXTERN int previous_stab_code;

/* Support for Sun changes to dbx symbol format */

/* For each identified header file, we have a table of types defined
   in that header file.

   header_files maps header file names to their type tables.
   It is a vector of n_header_files elements.
   Each element describes one header file.
   It contains a vector of types.

   Sometimes it can happen that the same header file produces
   different results when included in different places.
   This can result from conditionals or from different
   things done before including the file.
   When this happens, there are multiple entries for the file in this table,
   one entry for each distinct set of results.
   The entries are distinguished by the INSTANCE field.
   The INSTANCE field appears in the N_BINCL and N_EXCL symbol table and is
   used to match header-file references to their corresponding data.  */

struct header_file
  {

    /* Name of header file */

    char *name;

    /* Numeric code distinguishing instances of one header file that produced
       different results when included.  It comes from the N_BINCL or N_EXCL. */

    int instance;

    /* Pointer to vector of types */

    struct type **vector;

    /* Allocated length (# elts) of that vector */

    int length;

  };

/* The table of header_files of this OBJFILE. */
#define HEADER_FILES(OBJFILE) (DBX_SYMFILE_INFO (OBJFILE)->header_files)

/* The actual length of HEADER_FILES. */
#define N_HEADER_FILES(OBJFILE) (DBX_SYMFILE_INFO (OBJFILE)->n_header_files)

/* The allocated lengh of HEADER_FILES. */
#define N_ALLOCATED_HEADER_FILES(OBJFILE) \
  (DBX_SYMFILE_INFO (OBJFILE)->n_allocated_header_files)

/* Within each object file, various header files are assigned numbers.
   A type is defined or referred to with a pair of numbers
   (FILENUM,TYPENUM) where FILENUM is the number of the header file
   and TYPENUM is the number within that header file.
   TYPENUM is the index within the vector of types for that header file.

   FILENUM == 0 is special; it refers to the main source of the object file,
   and not to any header file.  FILENUM != 1 is interpreted by looking it up
   in the following table, which contains indices in header_files.  */

EXTERN int *this_object_header_files;

EXTERN int n_this_object_header_files;

EXTERN int n_allocated_this_object_header_files;

extern void cleanup_undefined_types (void);

extern long read_number (char **, int);

extern struct symbol *define_symbol (CORE_ADDR, char *, int, int,
				     struct objfile *);

extern void stabsread_init (void);

extern void stabsread_new_init (void);

extern void start_stabs (void);

extern void end_stabs (void);

extern void finish_global_stabs (struct objfile *objfile);

/* COFF files can have multiple .stab sections, if they are linked
   using --split-by-reloc.  This linked list is used to pass the
   information into the functions in dbxread.c.  */
struct stab_section_list
  {
    /* Next in list.  */
    struct stab_section_list *next;

    /* Stab section.  */
    asection *section;
  };

/* Functions exported by dbxread.c.  These are not in stabsread.c because
   they are only used by some stabs readers.  */

extern struct partial_symtab *end_psymtab (struct partial_symtab *pst,
					   char **include_list,
					   int num_includes,
					   int capping_symbol_offset,
					   CORE_ADDR capping_text,
					   struct partial_symtab
					   **dependency_list,
					   int number_dependencies,
					   int textlow_not_set);

extern void process_one_symbol (int, int, CORE_ADDR, char *,
				struct section_offsets *, struct objfile *);

extern void elfstab_build_psymtabs (struct objfile *objfile,
				    int mainline,
				    asection *stabsect,
				    file_ptr stabstroffset,
				    unsigned int stabstrsize);

extern void coffstab_build_psymtabs
  (struct objfile *objfile,
   int mainline,
   CORE_ADDR textaddr, unsigned int textsize,
   struct stab_section_list *stabs,
   file_ptr stabstroffset, unsigned int stabstrsize);

extern void stabsect_build_psymtabs
  (struct objfile *objfile,
   int mainline, char *stab_name, char *stabstr_name, char *text_name);

extern void elfstab_offset_sections (struct objfile *,
				     struct partial_symtab *);
extern int symbol_reference_defined (char **);

extern void ref_add (int, struct symbol *, char *, CORE_ADDR);

extern struct symbol *ref_search (int);

extern void free_header_files (void);

extern void init_header_files (void);

#undef EXTERN

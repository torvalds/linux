/* Build symbol tables in GDB's internal format.
   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1995, 1996,
   1997, 1998, 1999, 2000, 2002, 2003 Free Software Foundation, Inc.

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

#if !defined (BUILDSYM_H)
#define BUILDSYM_H 1

struct objfile;
struct symbol;

/* This module provides definitions used for creating and adding to
   the symbol table.  These routines are called from various symbol-
   file-reading routines.

   They originated in dbxread.c of gdb-4.2, and were split out to
   make xcoffread.c more maintainable by sharing code.

   Variables declared in this file can be defined by #define-ing the
   name EXTERN to null.  It is used to declare variables that are
   normally extern, but which get defined in a single module using
   this technique.  */

struct block;

#ifndef EXTERN
#define	EXTERN extern
#endif

#define HASHSIZE 127		/* Size of things hashed via
				   hashname() */

/* Name of source file whose symbol data we are now processing.  This
   comes from a symbol of type N_SO. */

EXTERN char *last_source_file;

/* Core address of start of text of current source file.  This too
   comes from the N_SO symbol. */

EXTERN CORE_ADDR last_source_start_addr;

/* The list of sub-source-files within the current individual
   compilation.  Each file gets its own symtab with its own linetable
   and associated info, but they all share one blockvector.  */

struct subfile
  {
    struct subfile *next;
    char *name;
    char *dirname;
    struct linetable *line_vector;
    int line_vector_length;
    enum language language;
    char *debugformat;
  };

EXTERN struct subfile *subfiles;

EXTERN struct subfile *current_subfile;

/* Global variable which, when set, indicates that we are processing a
   .o file compiled with gcc */

EXTERN unsigned char processing_gcc_compilation;

/* When set, we are processing a .o file compiled by sun acc.  This is
   misnamed; it refers to all stabs-in-elf implementations which use
   N_UNDF the way Sun does, including Solaris gcc.  Hopefully all
   stabs-in-elf implementations ever invented will choose to be
   compatible.  */

EXTERN unsigned char processing_acc_compilation;

/* Count symbols as they are processed, for error messages.  */

EXTERN unsigned int symnum;

/* Record the symbols defined for each context in a list.  We don't
   create a struct block for the context until we know how long to
   make it.  */

#define PENDINGSIZE 100

struct pending
  {
    struct pending *next;
    int nsyms;
    struct symbol *symbol[PENDINGSIZE];
  };

/* Here are the three lists that symbols are put on.  */

/* static at top level, and types */

EXTERN struct pending *file_symbols;

/* global functions and variables */

EXTERN struct pending *global_symbols;

/* everything local to lexical context */

EXTERN struct pending *local_symbols;

/* func params local to lexical  context */

EXTERN struct pending *param_symbols;

/* Stack representing unclosed lexical contexts (that will become
   blocks, eventually).  */

struct context_stack
  {
    /* Outer locals at the time we entered */

    struct pending *locals;

    /* Pending func params at the time we entered */

    struct pending *params;

    /* Pointer into blocklist as of entry */

    struct pending_block *old_blocks;

    /* Name of function, if any, defining context */

    struct symbol *name;

    /* PC where this context starts */

    CORE_ADDR start_addr;

    /* Temp slot for exception handling. */

    CORE_ADDR end_addr;

    /* For error-checking matching push/pop */

    int depth;

  };

EXTERN struct context_stack *context_stack;

/* Index of first unused entry in context stack.  */

EXTERN int context_stack_depth;

/* Currently allocated size of context stack.  */

EXTERN int context_stack_size;

/* Non-zero if the context stack is empty.  */
#define outermost_context_p() (context_stack_depth == 0)

/* Nonzero if within a function (so symbols should be local, if
   nothing says specifically).  */

EXTERN int within_function;

/* List of blocks already made (lexical contexts already closed).
   This is used at the end to make the blockvector.  */

struct pending_block
  {
    struct pending_block *next;
    struct block *block;
  };

/* Pointer to the head of a linked list of symbol blocks which have
   already been finalized (lexical contexts already closed) and which
   are just waiting to be built into a blockvector when finalizing the
   associated symtab. */

EXTERN struct pending_block *pending_blocks;


struct subfile_stack
  {
    struct subfile_stack *next;
    char *name;
  };

EXTERN struct subfile_stack *subfile_stack;

#define next_symbol_text(objfile) (*next_symbol_text_func)(objfile)

/* Function to invoke get the next symbol.  Return the symbol name. */

EXTERN char *(*next_symbol_text_func) (struct objfile *);

/* Vector of types defined so far, indexed by their type numbers.
   Used for both stabs and coff.  (In newer sun systems, dbx uses a
   pair of numbers in parens, as in "(SUBFILENUM,NUMWITHINSUBFILE)".
   Then these numbers must be translated through the type_translations
   hash table to get the index into the type vector.)  */

EXTERN struct type **type_vector;

/* Number of elements allocated for type_vector currently.  */

EXTERN int type_vector_length;

/* Initial size of type vector.  Is realloc'd larger if needed, and
   realloc'd down to the size actually used, when completed.  */

#define	INITIAL_TYPE_VECTOR_LENGTH	160

extern void add_free_pendings (struct pending *list);

extern void add_symbol_to_list (struct symbol *symbol,
				struct pending **listhead);

extern struct symbol *find_symbol_in_list (struct pending *list,
					   char *name, int length);

extern void finish_block (struct symbol *symbol,
			  struct pending **listhead,
			  struct pending_block *old_blocks,
			  CORE_ADDR start, CORE_ADDR end,
			  struct objfile *objfile);

extern void really_free_pendings (void *dummy);

extern void start_subfile (char *name, char *dirname);

extern void patch_subfile_names (struct subfile *subfile, char *name);

extern void push_subfile (void);

extern char *pop_subfile (void);

extern struct symtab *end_symtab (CORE_ADDR end_addr,
				  struct objfile *objfile, int section);

/* Defined in stabsread.c.  */

extern void scan_file_globals (struct objfile *objfile);

extern void buildsym_new_init (void);

extern void buildsym_init (void);

extern struct context_stack *push_context (int desc, CORE_ADDR valu);

extern struct context_stack *pop_context (void);

extern void record_line (struct subfile *subfile, int line, CORE_ADDR pc);

extern void start_symtab (char *name, char *dirname, CORE_ADDR start_addr);

extern int hashname (char *name);

extern void free_pending_blocks (void);

/* FIXME: Note that this is used only in buildsym.c and dstread.c,
   which should be fixed to not need direct access to
   record_pending_block. */

extern void record_pending_block (struct objfile *objfile,
				  struct block *block,
				  struct pending_block *opblock);

extern void record_debugformat (char *format);

extern void merge_symbol_lists (struct pending **srclist,
				struct pending **targetlist);

/* The macro table for the compilation unit whose symbols we're
   currently reading.  All the symtabs for this CU will point to this.  */
EXTERN struct macro_table *pending_macros;

#undef EXTERN

#endif /* defined (BUILDSYM_H) */

/* Read a symbol table in ECOFF format (Third-Eye).

   Copyright 1986, 1987, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004 Free Software
   Foundation, Inc.

   Original version contributed by Alessandro Forin (af@cs.cmu.edu) at
   CMU.  Major work by Per Bothner, John Gilmore and Ian Lance Taylor
   at Cygnus Support.

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

/* This module provides the function mdebug_build_psymtabs.  It reads
   ECOFF debugging information into partial symbol tables.  The
   debugging information is read from two structures.  A struct
   ecoff_debug_swap includes the sizes of each ECOFF structure and
   swapping routines; these are fixed for a particular target.  A
   struct ecoff_debug_info points to the debugging information for a
   particular object file.

   ECOFF symbol tables are mostly written in the byte order of the
   target machine.  However, one section of the table (the auxiliary
   symbol information) is written in the host byte order.  There is a
   bit in the other symbol info which describes which host byte order
   was used.  ECOFF thereby takes the trophy from Intel `b.out' for
   the most brain-dead adaptation of a file format to byte order.

   This module can read all four of the known byte-order combinations,
   on any type of host.  */

#include "defs.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "gdbcore.h"
#include "objfiles.h"
#include "gdb_obstack.h"
#include "buildsym.h"
#include "stabsread.h"
#include "complaints.h"
#include "demangle.h"
#include "gdb_assert.h"
#include "block.h"
#include "dictionary.h"

/* These are needed if the tm.h file does not contain the necessary
   mips specific definitions.  */

#ifndef MIPS_EFI_SYMBOL_NAME
#define MIPS_EFI_SYMBOL_NAME "__GDB_EFI_INFO__"
extern void ecoff_relocate_efi (struct symbol *, CORE_ADDR);
#include "coff/sym.h"
#include "coff/symconst.h"
typedef struct mips_extra_func_info
  {
    long numargs;
    PDR pdr;
  }
 *mips_extra_func_info_t;
#ifndef RA_REGNUM
#define RA_REGNUM 0
#endif
#endif

#ifdef USG
#include <sys/types.h>
#endif

#include "gdb_stat.h"
#include "gdb_string.h"

#include "bfd.h"

#include "coff/ecoff.h"		/* COFF-like aspects of ecoff files */

#include "libaout.h"		/* Private BFD a.out information.  */
#include "aout/aout64.h"
#include "aout/stab_gnu.h"	/* STABS information */

#include "expression.h"
#include "language.h"		/* For local_hex_string() */

extern void _initialize_mdebugread (void);

/* Provide a way to test if we have both ECOFF and ELF symbol tables.  
   We use this define in order to know whether we should override a 
   symbol's ECOFF section with its ELF section.  This is necessary in 
   case the symbol's ELF section could not be represented in ECOFF.  */
#define ECOFF_IN_ELF(bfd) (bfd_get_flavour (bfd) == bfd_target_elf_flavour \
			   && bfd_get_section_by_name (bfd, ".mdebug") != NULL)


/* We put a pointer to this structure in the read_symtab_private field
   of the psymtab.  */

struct symloc
  {
    /* Index of the FDR that this psymtab represents.  */
    int fdr_idx;
    /* The BFD that the psymtab was created from.  */
    bfd *cur_bfd;
    const struct ecoff_debug_swap *debug_swap;
    struct ecoff_debug_info *debug_info;
    struct mdebug_pending **pending_list;
    /* Pointer to external symbols for this file.  */
    EXTR *extern_tab;
    /* Size of extern_tab.  */
    int extern_count;
    enum language pst_language;
  };

#define PST_PRIVATE(p) ((struct symloc *)(p)->read_symtab_private)
#define FDR_IDX(p) (PST_PRIVATE(p)->fdr_idx)
#define CUR_BFD(p) (PST_PRIVATE(p)->cur_bfd)
#define DEBUG_SWAP(p) (PST_PRIVATE(p)->debug_swap)
#define DEBUG_INFO(p) (PST_PRIVATE(p)->debug_info)
#define PENDING_LIST(p) (PST_PRIVATE(p)->pending_list)

#define SC_IS_TEXT(sc) ((sc) == scText \
		   || (sc) == scRConst \
          	   || (sc) == scInit \
          	   || (sc) == scFini)
#define SC_IS_DATA(sc) ((sc) == scData \
		   || (sc) == scSData \
		   || (sc) == scRData \
		   || (sc) == scPData \
		   || (sc) == scXData)
#define SC_IS_COMMON(sc) ((sc) == scCommon || (sc) == scSCommon)
#define SC_IS_BSS(sc) ((sc) == scBss)
#define SC_IS_SBSS(sc) ((sc) == scSBss)
#define SC_IS_UNDEF(sc) ((sc) == scUndefined || (sc) == scSUndefined)

/* Various complaints about symbol reading that don't abort the process */
static void
index_complaint (const char *arg1)
{
  complaint (&symfile_complaints, "bad aux index at symbol %s", arg1);
}

static void
unknown_ext_complaint (const char *arg1)
{
  complaint (&symfile_complaints, "unknown external symbol %s", arg1);
}

static void
basic_type_complaint (int arg1, const char *arg2)
{
  complaint (&symfile_complaints, "cannot map ECOFF basic type 0x%x for %s",
	     arg1, arg2);
}

static void
bad_tag_guess_complaint (const char *arg1)
{
  complaint (&symfile_complaints, "guessed tag type of %s incorrectly", arg1);
}

static void
bad_rfd_entry_complaint (const char *arg1, int arg2, int arg3)
{
  complaint (&symfile_complaints, "bad rfd entry for %s: file %d, index %d",
	     arg1, arg2, arg3);
}

static void
unexpected_type_code_complaint (const char *arg1)
{
  complaint (&symfile_complaints, "unexpected type code for %s", arg1);
}

/* Macros and extra defs */

/* Puns: hard to find whether -g was used and how */

#define MIN_GLEVEL GLEVEL_0
#define compare_glevel(a,b)					\
	(((a) == GLEVEL_3) ? ((b) < GLEVEL_3) :			\
	 ((b) == GLEVEL_3) ? -1 : (int)((b) - (a)))

/* Things that really are local to this module */

/* Remember what we deduced to be the source language of this psymtab. */

static enum language psymtab_language = language_unknown;

/* Current BFD.  */

static bfd *cur_bfd;

/* How to parse debugging information for CUR_BFD.  */

static const struct ecoff_debug_swap *debug_swap;

/* Pointers to debugging information for CUR_BFD.  */

static struct ecoff_debug_info *debug_info;

/* Pointer to current file decriptor record, and its index */

static FDR *cur_fdr;
static int cur_fd;

/* Index of current symbol */

static int cur_sdx;

/* Note how much "debuggable" this image is.  We would like
   to see at least one FDR with full symbols */

static int max_gdbinfo;
static int max_glevel;

/* When examining .o files, report on undefined symbols */

static int n_undef_symbols, n_undef_labels, n_undef_vars, n_undef_procs;

/* Pseudo symbol to use when putting stabs into the symbol table.  */

static char stabs_symbol[] = STABS_SYMBOL;

/* Types corresponding to mdebug format bt* basic types.  */

static struct type *mdebug_type_void;
static struct type *mdebug_type_char;
static struct type *mdebug_type_short;
static struct type *mdebug_type_int_32;
#define mdebug_type_int mdebug_type_int_32
static struct type *mdebug_type_int_64;
static struct type *mdebug_type_long_32;
static struct type *mdebug_type_long_64;
static struct type *mdebug_type_long_long_64;
static struct type *mdebug_type_unsigned_char;
static struct type *mdebug_type_unsigned_short;
static struct type *mdebug_type_unsigned_int_32;
static struct type *mdebug_type_unsigned_int_64;
static struct type *mdebug_type_unsigned_long_32;
static struct type *mdebug_type_unsigned_long_64;
static struct type *mdebug_type_unsigned_long_long_64;
static struct type *mdebug_type_adr_32;
static struct type *mdebug_type_adr_64;
static struct type *mdebug_type_float;
static struct type *mdebug_type_double;
static struct type *mdebug_type_complex;
static struct type *mdebug_type_double_complex;
static struct type *mdebug_type_fixed_dec;
static struct type *mdebug_type_float_dec;
static struct type *mdebug_type_string;

/* Types for symbols from files compiled without debugging info.  */

static struct type *nodebug_func_symbol_type;
static struct type *nodebug_var_symbol_type;

/* Nonzero if we have seen ecoff debugging info for a file.  */

static int found_ecoff_debugging_info;

/* Forward declarations */

static int upgrade_type (int, struct type **, int, union aux_ext *,
			 int, char *);

static void parse_partial_symbols (struct objfile *);

static int has_opaque_xref (FDR *, SYMR *);

static int cross_ref (int, union aux_ext *, struct type **, enum type_code,
		      char **, int, char *);

static struct symbol *new_symbol (char *);

static struct type *new_type (char *);

enum block_type { FUNCTION_BLOCK, NON_FUNCTION_BLOCK };

static struct block *new_block (enum block_type);

static struct symtab *new_symtab (char *, int, struct objfile *);

static struct linetable *new_linetable (int);

static struct blockvector *new_bvect (int);

static struct type *parse_type (int, union aux_ext *, unsigned int, int *,
				int, char *);

static struct symbol *mylookup_symbol (char *, struct block *, domain_enum,
				       enum address_class);

static void sort_blocks (struct symtab *);

static struct partial_symtab *new_psymtab (char *, struct objfile *);

static void psymtab_to_symtab_1 (struct partial_symtab *, char *);

static void add_block (struct block *, struct symtab *);

static void add_symbol (struct symbol *, struct block *);

static int add_line (struct linetable *, int, CORE_ADDR, int);

static struct linetable *shrink_linetable (struct linetable *);

static void handle_psymbol_enumerators (struct objfile *, FDR *, int,
					CORE_ADDR);

static char *mdebug_next_symbol_text (struct objfile *);

/* Address bounds for the signal trampoline in inferior, if any */

CORE_ADDR sigtramp_address, sigtramp_end;

/* Allocate zeroed memory */

static void *
xzalloc (unsigned int size)
{
  void *p = xmalloc (size);

  memset (p, 0, size);
  return p;
}

/* Exported procedure: Builds a symtab from the PST partial one.
   Restores the environment in effect when PST was created, delegates
   most of the work to an ancillary procedure, and sorts
   and reorders the symtab list at the end */

static void
mdebug_psymtab_to_symtab (struct partial_symtab *pst)
{

  if (!pst)
    return;

  if (info_verbose)
    {
      printf_filtered ("Reading in symbols for %s...", pst->filename);
      gdb_flush (gdb_stdout);
    }

  next_symbol_text_func = mdebug_next_symbol_text;

  psymtab_to_symtab_1 (pst, pst->filename);

  /* Match with global symbols.  This only needs to be done once,
     after all of the symtabs and dependencies have been read in.   */
  scan_file_globals (pst->objfile);

  if (info_verbose)
    printf_filtered ("done.\n");
}

/* File-level interface functions */

/* Find a file descriptor given its index RF relative to a file CF */

static FDR *
get_rfd (int cf, int rf)
{
  FDR *fdrs;
  FDR *f;
  RFDT rfd;

  fdrs = debug_info->fdr;
  f = fdrs + cf;
  /* Object files do not have the RFD table, all refs are absolute */
  if (f->rfdBase == 0)
    return fdrs + rf;
  (*debug_swap->swap_rfd_in) (cur_bfd,
			      ((char *) debug_info->external_rfd
			       + ((f->rfdBase + rf)
				  * debug_swap->external_rfd_size)),
			      &rfd);
  return fdrs + rfd;
}

/* Return a safer print NAME for a file descriptor */

static char *
fdr_name (FDR *f)
{
  if (f->rss == -1)
    return "<stripped file>";
  if (f->rss == 0)
    return "<NFY>";
  return debug_info->ss + f->issBase + f->rss;
}


/* Read in and parse the symtab of the file OBJFILE.  Symbols from
   different sections are relocated via the SECTION_OFFSETS.  */

void
mdebug_build_psymtabs (struct objfile *objfile,
		       const struct ecoff_debug_swap *swap,
		       struct ecoff_debug_info *info)
{
  cur_bfd = objfile->obfd;
  debug_swap = swap;
  debug_info = info;

  stabsread_new_init ();
  buildsym_new_init ();
  free_header_files ();
  init_header_files ();
        
  /* Make sure all the FDR information is swapped in.  */
  if (info->fdr == (FDR *) NULL)
    {
      char *fdr_src;
      char *fdr_end;
      FDR *fdr_ptr;

      info->fdr = (FDR *) obstack_alloc (&objfile->objfile_obstack,
					 (info->symbolic_header.ifdMax
					  * sizeof (FDR)));
      fdr_src = info->external_fdr;
      fdr_end = (fdr_src
		 + info->symbolic_header.ifdMax * swap->external_fdr_size);
      fdr_ptr = info->fdr;
      for (; fdr_src < fdr_end; fdr_src += swap->external_fdr_size, fdr_ptr++)
	(*swap->swap_fdr_in) (objfile->obfd, fdr_src, fdr_ptr);
    }

  parse_partial_symbols (objfile);

#if 0
  /* Check to make sure file was compiled with -g.  If not, warn the
     user of this limitation.  */
  if (compare_glevel (max_glevel, GLEVEL_2) < 0)
    {
      if (max_gdbinfo == 0)
	printf_unfiltered ("\n%s not compiled with -g, debugging support is limited.\n",
			   objfile->name);
      printf_unfiltered ("You should compile with -g2 or -g3 for best debugging support.\n");
      gdb_flush (gdb_stdout);
    }
#endif
}

/* Local utilities */

/* Map of FDR indexes to partial symtabs */

struct pst_map
{
  struct partial_symtab *pst;	/* the psymtab proper */
  long n_globals;		/* exported globals (external symbols) */
  long globals_offset;		/* cumulative */
};


/* Utility stack, used to nest procedures and blocks properly.
   It is a doubly linked list, to avoid too many alloc/free.
   Since we might need it quite a few times it is NOT deallocated
   after use. */

static struct parse_stack
  {
    struct parse_stack *next, *prev;
    struct symtab *cur_st;	/* Current symtab. */
    struct block *cur_block;	/* Block in it. */

    /* What are we parsing.  stFile, or stBlock are for files and
       blocks.  stProc or stStaticProc means we have seen the start of a
       procedure, but not the start of the block within in.  When we see
       the start of that block, we change it to stNil, without pushing a
       new block, i.e. stNil means both a procedure and a block.  */

    int blocktype;

    struct type *cur_type;	/* Type we parse fields for. */
    int cur_field;		/* Field number in cur_type. */
    CORE_ADDR procadr;		/* Start addres of this procedure */
    int numargs;		/* Its argument count */
  }

 *top_stack;			/* Top stack ptr */


/* Enter a new lexical context */

static void
push_parse_stack (void)
{
  struct parse_stack *new;

  /* Reuse frames if possible */
  if (top_stack && top_stack->prev)
    new = top_stack->prev;
  else
    new = (struct parse_stack *) xzalloc (sizeof (struct parse_stack));
  /* Initialize new frame with previous content */
  if (top_stack)
    {
      struct parse_stack *prev = new->prev;

      *new = *top_stack;
      top_stack->prev = new;
      new->prev = prev;
      new->next = top_stack;
    }
  top_stack = new;
}

/* Exit a lexical context */

static void
pop_parse_stack (void)
{
  if (!top_stack)
    return;
  if (top_stack->next)
    top_stack = top_stack->next;
}


/* Cross-references might be to things we haven't looked at
   yet, e.g. type references.  To avoid too many type
   duplications we keep a quick fixup table, an array
   of lists of references indexed by file descriptor */

struct mdebug_pending
{
  struct mdebug_pending *next;	/* link */
  char *s;			/* the unswapped symbol */
  struct type *t;		/* its partial type descriptor */
};


/* The pending information is kept for an entire object file, and used
   to be in the sym_private field.  I took it out when I split
   mdebugread from mipsread, because this might not be the only type
   of symbols read from an object file.  Instead, we allocate the
   pending information table when we create the partial symbols, and
   we store a pointer to the single table in each psymtab.  */

static struct mdebug_pending **pending_list;

/* Check whether we already saw symbol SH in file FH */

static struct mdebug_pending *
is_pending_symbol (FDR *fh, char *sh)
{
  int f_idx = fh - debug_info->fdr;
  struct mdebug_pending *p;

  /* Linear search is ok, list is typically no more than 10 deep */
  for (p = pending_list[f_idx]; p; p = p->next)
    if (p->s == sh)
      break;
  return p;
}

/* Add a new symbol SH of type T */

static void
add_pending (FDR *fh, char *sh, struct type *t)
{
  int f_idx = fh - debug_info->fdr;
  struct mdebug_pending *p = is_pending_symbol (fh, sh);

  /* Make sure we do not make duplicates */
  if (!p)
    {
      p = ((struct mdebug_pending *)
	   obstack_alloc (&current_objfile->objfile_obstack,
			  sizeof (struct mdebug_pending)));
      p->s = sh;
      p->t = t;
      p->next = pending_list[f_idx];
      pending_list[f_idx] = p;
    }
}


/* Parsing Routines proper. */

/* Parse a single symbol. Mostly just make up a GDB symbol for it.
   For blocks, procedures and types we open a new lexical context.
   This is basically just a big switch on the symbol's type.  Argument
   AX is the base pointer of aux symbols for this file (fh->iauxBase).
   EXT_SH points to the unswapped symbol, which is needed for struct,
   union, etc., types; it is NULL for an EXTR.  BIGEND says whether
   aux symbols are big-endian or little-endian.  Return count of
   SYMR's handled (normally one).  */

static int
parse_symbol (SYMR *sh, union aux_ext *ax, char *ext_sh, int bigend,
	      struct section_offsets *section_offsets, struct objfile *objfile)
{
  const bfd_size_type external_sym_size = debug_swap->external_sym_size;
  void (*const swap_sym_in) (bfd *, void *, SYMR *) = debug_swap->swap_sym_in;
  char *name;
  struct symbol *s;
  struct block *b;
  struct mdebug_pending *pend;
  struct type *t;
  struct field *f;
  int count = 1;
  enum address_class class;
  TIR tir;
  long svalue = sh->value;
  int bitsize;

  if (ext_sh == (char *) NULL)
    name = debug_info->ssext + sh->iss;
  else
    name = debug_info->ss + cur_fdr->issBase + sh->iss;

  switch (sh->sc)
    {
    case scText:
    case scRConst:
      /* Do not relocate relative values.
         The value of a stEnd symbol is the displacement from the
         corresponding start symbol value.
         The value of a stBlock symbol is the displacement from the
         procedure address.  */
      if (sh->st != stEnd && sh->st != stBlock)
	sh->value += ANOFFSET (section_offsets, SECT_OFF_TEXT (objfile));
      break;
    case scData:
    case scSData:
    case scRData:
    case scPData:
    case scXData:
      sh->value += ANOFFSET (section_offsets, SECT_OFF_DATA (objfile));
      break;
    case scBss:
    case scSBss:
      sh->value += ANOFFSET (section_offsets, SECT_OFF_BSS (objfile));
      break;
    }

  switch (sh->st)
    {
    case stNil:
      break;

    case stGlobal:		/* external symbol, goes into global block */
      class = LOC_STATIC;
      b = BLOCKVECTOR_BLOCK (BLOCKVECTOR (top_stack->cur_st),
			     GLOBAL_BLOCK);
      s = new_symbol (name);
      SYMBOL_VALUE_ADDRESS (s) = (CORE_ADDR) sh->value;
      goto data;

    case stStatic:		/* static data, goes into current block. */
      class = LOC_STATIC;
      b = top_stack->cur_block;
      s = new_symbol (name);
      if (SC_IS_COMMON (sh->sc))
	{
	  /* It is a FORTRAN common block.  At least for SGI Fortran the
	     address is not in the symbol; we need to fix it later in
	     scan_file_globals.  */
	  int bucket = hashname (DEPRECATED_SYMBOL_NAME (s));
	  SYMBOL_VALUE_CHAIN (s) = global_sym_chain[bucket];
	  global_sym_chain[bucket] = s;
	}
      else
	SYMBOL_VALUE_ADDRESS (s) = (CORE_ADDR) sh->value;
      goto data;

    case stLocal:		/* local variable, goes into current block */
      if (sh->sc == scRegister)
	{
	  class = LOC_REGISTER;
	  svalue = ECOFF_REG_TO_REGNUM (svalue);
	}
      else
	class = LOC_LOCAL;
      b = top_stack->cur_block;
      s = new_symbol (name);
      SYMBOL_VALUE (s) = svalue;

    data:			/* Common code for symbols describing data */
      SYMBOL_DOMAIN (s) = VAR_DOMAIN;
      SYMBOL_CLASS (s) = class;
      add_symbol (s, b);

      /* Type could be missing if file is compiled without debugging info.  */
      if (SC_IS_UNDEF (sh->sc)
	  || sh->sc == scNil || sh->index == indexNil)
	SYMBOL_TYPE (s) = nodebug_var_symbol_type;
      else
	SYMBOL_TYPE (s) = parse_type (cur_fd, ax, sh->index, 0, bigend, name);
      /* Value of a data symbol is its memory address */
      break;

    case stParam:		/* arg to procedure, goes into current block */
      max_gdbinfo++;
      found_ecoff_debugging_info = 1;
      top_stack->numargs++;

      /* Special GNU C++ name.  */
      if (is_cplus_marker (name[0]) && name[1] == 't' && name[2] == 0)
	name = "this";		/* FIXME, not alloc'd in obstack */
      s = new_symbol (name);

      SYMBOL_DOMAIN (s) = VAR_DOMAIN;
      switch (sh->sc)
	{
	case scRegister:
	  /* Pass by value in register.  */
	  SYMBOL_CLASS (s) = LOC_REGPARM;
	  svalue = ECOFF_REG_TO_REGNUM (svalue);
	  break;
	case scVar:
	  /* Pass by reference on stack.  */
	  SYMBOL_CLASS (s) = LOC_REF_ARG;
	  break;
	case scVarRegister:
	  /* Pass by reference in register.  */
	  SYMBOL_CLASS (s) = LOC_REGPARM_ADDR;
	  svalue = ECOFF_REG_TO_REGNUM (svalue);
	  break;
	default:
	  /* Pass by value on stack.  */
	  SYMBOL_CLASS (s) = LOC_ARG;
	  break;
	}
      SYMBOL_VALUE (s) = svalue;
      SYMBOL_TYPE (s) = parse_type (cur_fd, ax, sh->index, 0, bigend, name);
      add_symbol (s, top_stack->cur_block);
      break;

    case stLabel:		/* label, goes into current block */
      s = new_symbol (name);
      SYMBOL_DOMAIN (s) = VAR_DOMAIN;	/* so that it can be used */
      SYMBOL_CLASS (s) = LOC_LABEL;	/* but not misused */
      SYMBOL_VALUE_ADDRESS (s) = (CORE_ADDR) sh->value;
      SYMBOL_TYPE (s) = mdebug_type_int;
      add_symbol (s, top_stack->cur_block);
      break;

    case stProc:		/* Procedure, usually goes into global block */
    case stStaticProc:		/* Static procedure, goes into current block */
      /* For stProc symbol records, we need to check the storage class
         as well, as only (stProc, scText) entries represent "real"
         procedures - See the Compaq document titled "Object File /
         Symbol Table Format Specification" for more information.
         If the storage class is not scText, we discard the whole block
         of symbol records for this stProc.  */
      if (sh->st == stProc && sh->sc != scText)
        {
          char *ext_tsym = ext_sh;
          int keep_counting = 1;
          SYMR tsym;

          while (keep_counting)
            {
              ext_tsym += external_sym_size;
              (*swap_sym_in) (cur_bfd, ext_tsym, &tsym);
              count++;
              switch (tsym.st)
                {
                  case stParam:
                    break;
                  case stEnd:
                    keep_counting = 0;
                    break;
                  default:
                    complaint (&symfile_complaints,
                               "unknown symbol type 0x%x", sh->st);
                    break;
                }
            }
          break;
        }
      s = new_symbol (name);
      SYMBOL_DOMAIN (s) = VAR_DOMAIN;
      SYMBOL_CLASS (s) = LOC_BLOCK;
      /* Type of the return value */
      if (SC_IS_UNDEF (sh->sc) || sh->sc == scNil)
	t = mdebug_type_int;
      else
	{
	  t = parse_type (cur_fd, ax, sh->index + 1, 0, bigend, name);
	  if (strcmp (name, "malloc") == 0
	      && TYPE_CODE (t) == TYPE_CODE_VOID)
	    {
	      /* I don't know why, but, at least under Alpha GNU/Linux,
	         when linking against a malloc without debugging
	         symbols, its read as a function returning void---this
	         is bad because it means we cannot call functions with
	         string arguments interactively; i.e., "call
	         printf("howdy\n")" would fail with the error message
	         "program has no memory available".  To avoid this, we
	         patch up the type and make it void*
	         instead. (davidm@azstarnet.com)
	       */
	      t = make_pointer_type (t, NULL);
	    }
	}
      b = top_stack->cur_block;
      if (sh->st == stProc)
	{
	  struct blockvector *bv = BLOCKVECTOR (top_stack->cur_st);
	  /* The next test should normally be true, but provides a
	     hook for nested functions (which we don't want to make
	     global). */
	  if (b == BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK))
	    b = BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK);
	  /* Irix 5 sometimes has duplicate names for the same
	     function.  We want to add such names up at the global
	     level, not as a nested function.  */
	  else if (sh->value == top_stack->procadr)
	    b = BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK);
	}
      add_symbol (s, b);

      /* Make a type for the procedure itself */
      SYMBOL_TYPE (s) = lookup_function_type (t);

      /* All functions in C++ have prototypes.  For C we don't have enough
         information in the debug info.  */
      if (SYMBOL_LANGUAGE (s) == language_cplus)
	TYPE_FLAGS (SYMBOL_TYPE (s)) |= TYPE_FLAG_PROTOTYPED;

      /* Create and enter a new lexical context */
      b = new_block (FUNCTION_BLOCK);
      SYMBOL_BLOCK_VALUE (s) = b;
      BLOCK_FUNCTION (b) = s;
      BLOCK_START (b) = BLOCK_END (b) = sh->value;
      BLOCK_SUPERBLOCK (b) = top_stack->cur_block;
      add_block (b, top_stack->cur_st);

      /* Not if we only have partial info */
      if (SC_IS_UNDEF (sh->sc) || sh->sc == scNil)
	break;

      push_parse_stack ();
      top_stack->cur_block = b;
      top_stack->blocktype = sh->st;
      top_stack->cur_type = SYMBOL_TYPE (s);
      top_stack->cur_field = -1;
      top_stack->procadr = sh->value;
      top_stack->numargs = 0;
      break;

      /* Beginning of code for structure, union, and enum definitions.
         They all share a common set of local variables, defined here.  */
      {
	enum type_code type_code;
	char *ext_tsym;
	int nfields;
	long max_value;
	struct field *f;

    case stStruct:		/* Start a block defining a struct type */
	type_code = TYPE_CODE_STRUCT;
	goto structured_common;

    case stUnion:		/* Start a block defining a union type */
	type_code = TYPE_CODE_UNION;
	goto structured_common;

    case stEnum:		/* Start a block defining an enum type */
	type_code = TYPE_CODE_ENUM;
	goto structured_common;

    case stBlock:		/* Either a lexical block, or some type */
	if (sh->sc != scInfo && !SC_IS_COMMON (sh->sc))
	  goto case_stBlock_code;	/* Lexical block */

	type_code = TYPE_CODE_UNDEF;	/* We have a type.  */

	/* Common code for handling struct, union, enum, and/or as-yet-
	   unknown-type blocks of info about structured data.  `type_code'
	   has been set to the proper TYPE_CODE, if we know it.  */
      structured_common:
	found_ecoff_debugging_info = 1;
	push_parse_stack ();
	top_stack->blocktype = stBlock;

	/* First count the number of fields and the highest value. */
	nfields = 0;
	max_value = 0;
	for (ext_tsym = ext_sh + external_sym_size;
	     ;
	     ext_tsym += external_sym_size)
	  {
	    SYMR tsym;

	    (*swap_sym_in) (cur_bfd, ext_tsym, &tsym);

	    switch (tsym.st)
	      {
	      case stEnd:
                /* C++ encodes class types as structures where there the
                   methods are encoded as stProc. The scope of stProc
                   symbols also ends with stEnd, thus creating a risk of
                   taking the wrong stEnd symbol record as the end of
                   the current struct, which would cause GDB to undercount
                   the real number of fields in this struct.  To make sure
                   we really reached the right stEnd symbol record, we
                   check the associated name, and match it against the
                   struct name.  Since method names are mangled while
                   the class name is not, there is no risk of having a
                   method whose name is identical to the class name
                   (in particular constructor method names are different
                   from the class name).  There is therefore no risk that
                   this check stops the count on the StEnd of a method.
		   
		   Also, assume that we're really at the end when tsym.iss
		   is 0 (issNull).  */
                if (tsym.iss == issNull
		    || strcmp (debug_info->ss + cur_fdr->issBase + tsym.iss,
                               name) == 0)
                  goto end_of_fields;
                break;

	      case stMember:
		if (nfields == 0 && type_code == TYPE_CODE_UNDEF)
		  {
		    /* If the type of the member is Nil (or Void),
		       without qualifiers, assume the tag is an
		       enumeration.
		       Alpha cc -migrate enums are recognized by a zero
		       index and a zero symbol value.
		       DU 4.0 cc enums are recognized by a member type of
		       btEnum without qualifiers and a zero symbol value.  */
		    if (tsym.index == indexNil
			|| (tsym.index == 0 && sh->value == 0))
		      type_code = TYPE_CODE_ENUM;
		    else
		      {
			(*debug_swap->swap_tir_in) (bigend,
						    &ax[tsym.index].a_ti,
						    &tir);
			if ((tir.bt == btNil || tir.bt == btVoid
			     || (tir.bt == btEnum && sh->value == 0))
			    && tir.tq0 == tqNil)
			  type_code = TYPE_CODE_ENUM;
		      }
		  }
		nfields++;
		if (tsym.value > max_value)
		  max_value = tsym.value;
		break;

	      case stBlock:
	      case stUnion:
	      case stEnum:
	      case stStruct:
		{
#if 0
		  /* This is a no-op; is it trying to tell us something
		     we should be checking?  */
		  if (tsym.sc == scVariant);	/*UNIMPLEMENTED */
#endif
		  if (tsym.index != 0)
		    {
		      /* This is something like a struct within a
		         struct.  Skip over the fields of the inner
		         struct.  The -1 is because the for loop will
		         increment ext_tsym.  */
		      ext_tsym = ((char *) debug_info->external_sym
				  + ((cur_fdr->isymBase + tsym.index - 1)
				     * external_sym_size));
		    }
		}
		break;

	      case stTypedef:
		/* mips cc puts out a typedef for struct x if it is not yet
		   defined when it encounters
		   struct y { struct x *xp; };
		   Just ignore it. */
		break;

	      case stIndirect:
		/* Irix5 cc puts out a stIndirect for struct x if it is not
		   yet defined when it encounters
		   struct y { struct x *xp; };
		   Just ignore it. */
		break;

	      default:
		complaint (&symfile_complaints,
			   "declaration block contains unhandled symbol type %d",
			   tsym.st);
	      }
	  }
      end_of_fields:;

	/* In an stBlock, there is no way to distinguish structs,
	   unions, and enums at this point.  This is a bug in the
	   original design (that has been fixed with the recent
	   addition of the stStruct, stUnion, and stEnum symbol
	   types.)  The way you can tell is if/when you see a variable
	   or field of that type.  In that case the variable's type
	   (in the AUX table) says if the type is struct, union, or
	   enum, and points back to the stBlock here.  So you can
	   patch the tag kind up later - but only if there actually is
	   a variable or field of that type.

	   So until we know for sure, we will guess at this point.
	   The heuristic is:
	   If the first member has index==indexNil or a void type,
	   assume we have an enumeration.
	   Otherwise, if there is more than one member, and all
	   the members have offset 0, assume we have a union.
	   Otherwise, assume we have a struct.

	   The heuristic could guess wrong in the case of of an
	   enumeration with no members or a union with one (or zero)
	   members, or when all except the last field of a struct have
	   width zero.  These are uncommon and/or illegal situations,
	   and in any case guessing wrong probably doesn't matter
	   much.

	   But if we later do find out we were wrong, we fixup the tag
	   kind.  Members of an enumeration must be handled
	   differently from struct/union fields, and that is harder to
	   patch up, but luckily we shouldn't need to.  (If there are
	   any enumeration members, we can tell for sure it's an enum
	   here.) */

	if (type_code == TYPE_CODE_UNDEF)
	  {
	    if (nfields > 1 && max_value == 0)
	      type_code = TYPE_CODE_UNION;
	    else
	      type_code = TYPE_CODE_STRUCT;
	  }

	/* Create a new type or use the pending type.  */
	pend = is_pending_symbol (cur_fdr, ext_sh);
	if (pend == (struct mdebug_pending *) NULL)
	  {
	    t = new_type (NULL);
	    add_pending (cur_fdr, ext_sh, t);
	  }
	else
	  t = pend->t;

	/* Do not set the tag name if it is a compiler generated tag name
	   (.Fxx or .xxfake or empty) for unnamed struct/union/enums.
	   Alpha cc puts out an sh->iss of zero for those.  */
	if (sh->iss == 0 || name[0] == '.' || name[0] == '\0')
	  TYPE_TAG_NAME (t) = NULL;
	else
	  TYPE_TAG_NAME (t) = obconcat (&current_objfile->objfile_obstack,
					"", "", name);

	TYPE_CODE (t) = type_code;
	TYPE_LENGTH (t) = sh->value;
	TYPE_NFIELDS (t) = nfields;
	TYPE_FIELDS (t) = f = ((struct field *)
			       TYPE_ALLOC (t,
					   nfields * sizeof (struct field)));

	if (type_code == TYPE_CODE_ENUM)
	  {
	    int unsigned_enum = 1;

	    /* This is a non-empty enum. */

	    /* DEC c89 has the number of enumerators in the sh.value field,
	       not the type length, so we have to compensate for that
	       incompatibility quirk.
	       This might do the wrong thing for an enum with one or two
	       enumerators and gcc -gcoff -fshort-enums, but these cases
	       are hopefully rare enough.
	       Alpha cc -migrate has a sh.value field of zero, we adjust
	       that too.  */
	    if (TYPE_LENGTH (t) == TYPE_NFIELDS (t)
		|| TYPE_LENGTH (t) == 0)
	      TYPE_LENGTH (t) = TARGET_INT_BIT / HOST_CHAR_BIT;
	    for (ext_tsym = ext_sh + external_sym_size;
		 ;
		 ext_tsym += external_sym_size)
	      {
		SYMR tsym;
		struct symbol *enum_sym;

		(*swap_sym_in) (cur_bfd, ext_tsym, &tsym);

		if (tsym.st != stMember)
		  break;

		FIELD_BITPOS (*f) = tsym.value;
		FIELD_TYPE (*f) = t;
		FIELD_NAME (*f) = debug_info->ss + cur_fdr->issBase + tsym.iss;
		FIELD_BITSIZE (*f) = 0;
		FIELD_STATIC_KIND (*f) = 0;

		enum_sym = ((struct symbol *)
			    obstack_alloc (&current_objfile->objfile_obstack,
					   sizeof (struct symbol)));
		memset (enum_sym, 0, sizeof (struct symbol));
		DEPRECATED_SYMBOL_NAME (enum_sym) =
		  obsavestring (f->name, strlen (f->name),
				&current_objfile->objfile_obstack);
		SYMBOL_CLASS (enum_sym) = LOC_CONST;
		SYMBOL_TYPE (enum_sym) = t;
		SYMBOL_DOMAIN (enum_sym) = VAR_DOMAIN;
		SYMBOL_VALUE (enum_sym) = tsym.value;
		if (SYMBOL_VALUE (enum_sym) < 0)
		  unsigned_enum = 0;
		add_symbol (enum_sym, top_stack->cur_block);

		/* Skip the stMembers that we've handled. */
		count++;
		f++;
	      }
	    if (unsigned_enum)
	      TYPE_FLAGS (t) |= TYPE_FLAG_UNSIGNED;
	  }
	/* make this the current type */
	top_stack->cur_type = t;
	top_stack->cur_field = 0;

	/* Do not create a symbol for alpha cc unnamed structs.  */
	if (sh->iss == 0)
	  break;

	/* gcc puts out an empty struct for an opaque struct definitions,
	   do not create a symbol for it either.  */
	if (TYPE_NFIELDS (t) == 0)
	  {
	    TYPE_FLAGS (t) |= TYPE_FLAG_STUB;
	    break;
	  }

	s = new_symbol (name);
	SYMBOL_DOMAIN (s) = STRUCT_DOMAIN;
	SYMBOL_CLASS (s) = LOC_TYPEDEF;
	SYMBOL_VALUE (s) = 0;
	SYMBOL_TYPE (s) = t;
	add_symbol (s, top_stack->cur_block);
	break;

	/* End of local variables shared by struct, union, enum, and
	   block (as yet unknown struct/union/enum) processing.  */
      }

    case_stBlock_code:
      found_ecoff_debugging_info = 1;
      /* beginnning of (code) block. Value of symbol
         is the displacement from procedure start */
      push_parse_stack ();

      /* Do not start a new block if this is the outermost block of a
         procedure.  This allows the LOC_BLOCK symbol to point to the
         block with the local variables, so funcname::var works.  */
      if (top_stack->blocktype == stProc
	  || top_stack->blocktype == stStaticProc)
	{
	  top_stack->blocktype = stNil;
	  break;
	}

      top_stack->blocktype = stBlock;
      b = new_block (NON_FUNCTION_BLOCK);
      BLOCK_START (b) = sh->value + top_stack->procadr;
      BLOCK_SUPERBLOCK (b) = top_stack->cur_block;
      top_stack->cur_block = b;
      add_block (b, top_stack->cur_st);
      break;

    case stEnd:		/* end (of anything) */
      if (sh->sc == scInfo || SC_IS_COMMON (sh->sc))
	{
	  /* Finished with type */
	  top_stack->cur_type = 0;
	}
      else if (sh->sc == scText &&
	       (top_stack->blocktype == stProc ||
		top_stack->blocktype == stStaticProc))
	{
	  /* Finished with procedure */
	  struct blockvector *bv = BLOCKVECTOR (top_stack->cur_st);
	  struct mips_extra_func_info *e;
	  struct block *b = top_stack->cur_block;
	  struct type *ftype = top_stack->cur_type;
	  int i;

	  BLOCK_END (top_stack->cur_block) += sh->value;	/* size */

	  /* Make up special symbol to contain procedure specific info */
	  s = new_symbol (MIPS_EFI_SYMBOL_NAME);
	  SYMBOL_DOMAIN (s) = LABEL_DOMAIN;
	  SYMBOL_CLASS (s) = LOC_CONST;
	  SYMBOL_TYPE (s) = mdebug_type_void;
	  e = ((struct mips_extra_func_info *)
	       obstack_alloc (&current_objfile->objfile_obstack,
			      sizeof (struct mips_extra_func_info)));
	  memset (e, 0, sizeof (struct mips_extra_func_info));
	  SYMBOL_VALUE (s) = (long) e;
	  e->numargs = top_stack->numargs;
	  e->pdr.framereg = -1;
	  add_symbol (s, top_stack->cur_block);

	  /* f77 emits proc-level with address bounds==[0,0],
	     So look for such child blocks, and patch them.  */
	  for (i = 0; i < BLOCKVECTOR_NBLOCKS (bv); i++)
	    {
	      struct block *b_bad = BLOCKVECTOR_BLOCK (bv, i);
	      if (BLOCK_SUPERBLOCK (b_bad) == b
		  && BLOCK_START (b_bad) == top_stack->procadr
		  && BLOCK_END (b_bad) == top_stack->procadr)
		{
		  BLOCK_START (b_bad) = BLOCK_START (b);
		  BLOCK_END (b_bad) = BLOCK_END (b);
		}
	    }

	  if (TYPE_NFIELDS (ftype) <= 0)
	    {
	      /* No parameter type information is recorded with the function's
	         type.  Set that from the type of the parameter symbols. */
	      int nparams = top_stack->numargs;
	      int iparams;
	      struct symbol *sym;

	      if (nparams > 0)
		{
		  struct dict_iterator iter;
		  TYPE_NFIELDS (ftype) = nparams;
		  TYPE_FIELDS (ftype) = (struct field *)
		    TYPE_ALLOC (ftype, nparams * sizeof (struct field));

		  iparams = 0;
		  ALL_BLOCK_SYMBOLS (b, iter, sym)
		    {
		      if (iparams == nparams)
			break;

		      switch (SYMBOL_CLASS (sym))
			{
			case LOC_ARG:
			case LOC_REF_ARG:
			case LOC_REGPARM:
			case LOC_REGPARM_ADDR:
			  TYPE_FIELD_TYPE (ftype, iparams) = SYMBOL_TYPE (sym);
			  TYPE_FIELD_ARTIFICIAL (ftype, iparams) = 0;
			  iparams++;
			  break;
			default:
			  break;
			}
		    }
		}
	    }
	}
      else if (sh->sc == scText && top_stack->blocktype == stBlock)
	{
	  /* End of (code) block. The value of the symbol is the
	     displacement from the procedure`s start address of the
	     end of this block. */
	  BLOCK_END (top_stack->cur_block) = sh->value + top_stack->procadr;
	}
      else if (sh->sc == scText && top_stack->blocktype == stNil)
	{
	  /* End of outermost block.  Pop parse stack and ignore.  The
	     following stEnd of stProc will take care of the block.  */
	  ;
	}
      else if (sh->sc == scText && top_stack->blocktype == stFile)
	{
	  /* End of file.  Pop parse stack and ignore.  Higher
	     level code deals with this.  */
	  ;
	}
      else
	complaint (&symfile_complaints,
		   "stEnd with storage class %d not handled", sh->sc);

      pop_parse_stack ();	/* restore previous lexical context */
      break;

    case stMember:		/* member of struct or union */
      f = &TYPE_FIELDS (top_stack->cur_type)[top_stack->cur_field++];
      FIELD_NAME (*f) = name;
      FIELD_BITPOS (*f) = sh->value;
      bitsize = 0;
      FIELD_TYPE (*f) = parse_type (cur_fd, ax, sh->index, &bitsize, bigend, name);
      FIELD_BITSIZE (*f) = bitsize;
      FIELD_STATIC_KIND (*f) = 0;
      break;

    case stIndirect:		/* forward declaration on Irix5 */
      /* Forward declarations from Irix5 cc are handled by cross_ref,
         skip them.  */
      break;

    case stTypedef:		/* type definition */
      found_ecoff_debugging_info = 1;

      /* Typedefs for forward declarations and opaque structs from alpha cc
         are handled by cross_ref, skip them.  */
      if (sh->iss == 0)
	break;

      /* Parse the type or use the pending type.  */
      pend = is_pending_symbol (cur_fdr, ext_sh);
      if (pend == (struct mdebug_pending *) NULL)
	{
	  t = parse_type (cur_fd, ax, sh->index, (int *) NULL, bigend, name);
	  add_pending (cur_fdr, ext_sh, t);
	}
      else
	t = pend->t;

      /* mips cc puts out a typedef with the name of the struct for forward
         declarations. These should not go into the symbol table and
         TYPE_NAME should not be set for them.
         They can't be distinguished from an intentional typedef to
         the same name however:
         x.h:
         struct x { int ix; int jx; };
         struct xx;
         x.c:
         typedef struct x x;
         struct xx {int ixx; int jxx; };
         generates a cross referencing stTypedef for x and xx.
         The user visible effect of this is that the type of a pointer
         to struct foo sometimes is given as `foo *' instead of `struct foo *'.
         The problem is fixed with alpha cc and Irix5 cc.  */

      /* However if the typedef cross references to an opaque aggregate, it
         is safe to omit it from the symbol table.  */

      if (has_opaque_xref (cur_fdr, sh))
	break;
      s = new_symbol (name);
      SYMBOL_DOMAIN (s) = VAR_DOMAIN;
      SYMBOL_CLASS (s) = LOC_TYPEDEF;
      SYMBOL_BLOCK_VALUE (s) = top_stack->cur_block;
      SYMBOL_TYPE (s) = t;
      add_symbol (s, top_stack->cur_block);

      /* Incomplete definitions of structs should not get a name.  */
      if (TYPE_NAME (SYMBOL_TYPE (s)) == NULL
	  && (TYPE_NFIELDS (SYMBOL_TYPE (s)) != 0
	      || (TYPE_CODE (SYMBOL_TYPE (s)) != TYPE_CODE_STRUCT
		  && TYPE_CODE (SYMBOL_TYPE (s)) != TYPE_CODE_UNION)))
	{
	  if (TYPE_CODE (SYMBOL_TYPE (s)) == TYPE_CODE_PTR
	      || TYPE_CODE (SYMBOL_TYPE (s)) == TYPE_CODE_FUNC)
	    {
	      /* If we are giving a name to a type such as "pointer to
	         foo" or "function returning foo", we better not set
	         the TYPE_NAME.  If the program contains "typedef char
	         *caddr_t;", we don't want all variables of type char
	         * to print as caddr_t.  This is not just a
	         consequence of GDB's type management; CC and GCC (at
	         least through version 2.4) both output variables of
	         either type char * or caddr_t with the type
	         refering to the stTypedef symbol for caddr_t.  If a future
	         compiler cleans this up it GDB is not ready for it
	         yet, but if it becomes ready we somehow need to
	         disable this check (without breaking the PCC/GCC2.4
	         case).

	         Sigh.

	         Fortunately, this check seems not to be necessary
	         for anything except pointers or functions.  */
	    }
	  else
	    TYPE_NAME (SYMBOL_TYPE (s)) = DEPRECATED_SYMBOL_NAME (s);
	}
      break;

    case stFile:		/* file name */
      push_parse_stack ();
      top_stack->blocktype = sh->st;
      break;

      /* I`ve never seen these for C */
    case stRegReloc:
      break;			/* register relocation */
    case stForward:
      break;			/* forwarding address */
    case stConstant:
      break;			/* constant */
    default:
      complaint (&symfile_complaints, "unknown symbol type 0x%x", sh->st);
      break;
    }

  return count;
}

/* Parse the type information provided in the raw AX entries for
   the symbol SH. Return the bitfield size in BS, in case.
   We must byte-swap the AX entries before we use them; BIGEND says whether
   they are big-endian or little-endian (from fh->fBigendian).  */

static struct type *
parse_type (int fd, union aux_ext *ax, unsigned int aux_index, int *bs,
	    int bigend, char *sym_name)
{
  /* Null entries in this map are treated specially */
  static struct type **map_bt[] =
  {
    &mdebug_type_void,		/* btNil */
    &mdebug_type_adr_32,	/* btAdr */
    &mdebug_type_char,		/* btChar */
    &mdebug_type_unsigned_char,	/* btUChar */
    &mdebug_type_short,		/* btShort */
    &mdebug_type_unsigned_short,	/* btUShort */
    &mdebug_type_int_32,	/* btInt */
    &mdebug_type_unsigned_int_32,	/* btUInt */
    &mdebug_type_long_32,	/* btLong */
    &mdebug_type_unsigned_long_32,	/* btULong */
    &mdebug_type_float,		/* btFloat */
    &mdebug_type_double,	/* btDouble */
    0,				/* btStruct */
    0,				/* btUnion */
    0,				/* btEnum */
    0,				/* btTypedef */
    0,				/* btRange */
    0,				/* btSet */
    &mdebug_type_complex,	/* btComplex */
    &mdebug_type_double_complex,	/* btDComplex */
    0,				/* btIndirect */
    &mdebug_type_fixed_dec,	/* btFixedDec */
    &mdebug_type_float_dec,	/* btFloatDec */
    &mdebug_type_string,	/* btString */
    0,				/* btBit */
    0,				/* btPicture */
    &mdebug_type_void,		/* btVoid */
    0,				/* DEC C++:  Pointer to member */
    0,				/* DEC C++:  Virtual function table */
    0,				/* DEC C++:  Class (Record) */
    &mdebug_type_long_64,	/* btLong64  */
    &mdebug_type_unsigned_long_64,	/* btULong64 */
    &mdebug_type_long_long_64,	/* btLongLong64  */
    &mdebug_type_unsigned_long_long_64,		/* btULongLong64 */
    &mdebug_type_adr_64,	/* btAdr64 */
    &mdebug_type_int_64,	/* btInt64  */
    &mdebug_type_unsigned_int_64,	/* btUInt64 */
  };

  TIR t[1];
  struct type *tp = 0;
  enum type_code type_code = TYPE_CODE_UNDEF;

  /* Handle undefined types, they have indexNil. */
  if (aux_index == indexNil)
    return mdebug_type_int;

  /* Handle corrupt aux indices.  */
  if (aux_index >= (debug_info->fdr + fd)->caux)
    {
      index_complaint (sym_name);
      return mdebug_type_int;
    }
  ax += aux_index;

  /* Use aux as a type information record, map its basic type.  */
  (*debug_swap->swap_tir_in) (bigend, &ax->a_ti, t);
  if (t->bt >= (sizeof (map_bt) / sizeof (*map_bt)))
    {
      basic_type_complaint (t->bt, sym_name);
      return mdebug_type_int;
    }
  if (map_bt[t->bt])
    {
      tp = *map_bt[t->bt];
    }
  else
    {
      tp = NULL;
      /* Cannot use builtin types -- build our own */
      switch (t->bt)
	{
	case btStruct:
	  type_code = TYPE_CODE_STRUCT;
	  break;
	case btUnion:
	  type_code = TYPE_CODE_UNION;
	  break;
	case btEnum:
	  type_code = TYPE_CODE_ENUM;
	  break;
	case btRange:
	  type_code = TYPE_CODE_RANGE;
	  break;
	case btSet:
	  type_code = TYPE_CODE_SET;
	  break;
	case btIndirect:
	  /* alpha cc -migrate uses this for typedefs. The true type will
	     be obtained by crossreferencing below.  */
	  type_code = TYPE_CODE_ERROR;
	  break;
	case btTypedef:
	  /* alpha cc uses this for typedefs. The true type will be
	     obtained by crossreferencing below.  */
	  type_code = TYPE_CODE_ERROR;
	  break;
	default:
	  basic_type_complaint (t->bt, sym_name);
	  return mdebug_type_int;
	}
    }

  /* Move on to next aux */
  ax++;

  if (t->fBitfield)
    {
      int width = AUX_GET_WIDTH (bigend, ax);
      /* Inhibit core dumps if TIR is corrupted.  */
      if (bs == (int *) NULL)
	{
	  /* Alpha cc -migrate encodes char and unsigned char types
	     as short and unsigned short types with a field width of 8.
	     Enum types also have a field width which we ignore for now.  */
	  if (t->bt == btShort && width == 8)
	    tp = mdebug_type_char;
	  else if (t->bt == btUShort && width == 8)
	    tp = mdebug_type_unsigned_char;
	  else if (t->bt == btEnum)
	    ;
	  else
	    complaint (&symfile_complaints, "can't handle TIR fBitfield for %s",
		       sym_name);
	}
      else
	*bs = width;
      ax++;
    }

  /* A btIndirect entry cross references to an aux entry containing
     the type.  */
  if (t->bt == btIndirect)
    {
      RNDXR rn[1];
      int rf;
      FDR *xref_fh;
      int xref_fd;

      (*debug_swap->swap_rndx_in) (bigend, &ax->a_rndx, rn);
      ax++;
      if (rn->rfd == 0xfff)
	{
	  rf = AUX_GET_ISYM (bigend, ax);
	  ax++;
	}
      else
	rf = rn->rfd;

      if (rf == -1)
	{
	  complaint (&symfile_complaints,
		     "unable to cross ref btIndirect for %s", sym_name);
	  return mdebug_type_int;
	}
      xref_fh = get_rfd (fd, rf);
      xref_fd = xref_fh - debug_info->fdr;
      tp = parse_type (xref_fd, debug_info->external_aux + xref_fh->iauxBase,
		    rn->index, (int *) NULL, xref_fh->fBigendian, sym_name);
    }

  /* All these types really point to some (common) MIPS type
     definition, and only the type-qualifiers fully identify
     them.  We'll make the same effort at sharing. */
  if (t->bt == btStruct ||
      t->bt == btUnion ||
      t->bt == btEnum ||

  /* btSet (I think) implies that the name is a tag name, not a typedef
     name.  This apparently is a MIPS extension for C sets.  */
      t->bt == btSet)
    {
      char *name;

      /* Try to cross reference this type, build new type on failure.  */
      ax += cross_ref (fd, ax, &tp, type_code, &name, bigend, sym_name);
      if (tp == (struct type *) NULL)
	tp = init_type (type_code, 0, 0, (char *) NULL, current_objfile);

      /* DEC c89 produces cross references to qualified aggregate types,
         dereference them.  */
      while (TYPE_CODE (tp) == TYPE_CODE_PTR
	     || TYPE_CODE (tp) == TYPE_CODE_ARRAY)
	tp = TYPE_TARGET_TYPE (tp);

      /* Make sure that TYPE_CODE(tp) has an expected type code.
         Any type may be returned from cross_ref if file indirect entries
         are corrupted.  */
      if (TYPE_CODE (tp) != TYPE_CODE_STRUCT
	  && TYPE_CODE (tp) != TYPE_CODE_UNION
	  && TYPE_CODE (tp) != TYPE_CODE_ENUM)
	{
	  unexpected_type_code_complaint (sym_name);
	}
      else
	{

	  /* Usually, TYPE_CODE(tp) is already type_code.  The main
	     exception is if we guessed wrong re struct/union/enum.
	     But for struct vs. union a wrong guess is harmless, so
	     don't complain().  */
	  if ((TYPE_CODE (tp) == TYPE_CODE_ENUM
	       && type_code != TYPE_CODE_ENUM)
	      || (TYPE_CODE (tp) != TYPE_CODE_ENUM
		  && type_code == TYPE_CODE_ENUM))
	    {
	      bad_tag_guess_complaint (sym_name);
	    }

	  if (TYPE_CODE (tp) != type_code)
	    {
	      TYPE_CODE (tp) = type_code;
	    }

	  /* Do not set the tag name if it is a compiler generated tag name
	     (.Fxx or .xxfake or empty) for unnamed struct/union/enums.  */
	  if (name[0] == '.' || name[0] == '\0')
	    TYPE_TAG_NAME (tp) = NULL;
	  else if (TYPE_TAG_NAME (tp) == NULL
		   || strcmp (TYPE_TAG_NAME (tp), name) != 0)
	    TYPE_TAG_NAME (tp) = obsavestring (name, strlen (name),
					    &current_objfile->objfile_obstack);
	}
    }

  /* All these types really point to some (common) MIPS type
     definition, and only the type-qualifiers fully identify
     them.  We'll make the same effort at sharing.
     FIXME: We are not doing any guessing on range types.  */
  if (t->bt == btRange)
    {
      char *name;

      /* Try to cross reference this type, build new type on failure.  */
      ax += cross_ref (fd, ax, &tp, type_code, &name, bigend, sym_name);
      if (tp == (struct type *) NULL)
	tp = init_type (type_code, 0, 0, (char *) NULL, current_objfile);

      /* Make sure that TYPE_CODE(tp) has an expected type code.
         Any type may be returned from cross_ref if file indirect entries
         are corrupted.  */
      if (TYPE_CODE (tp) != TYPE_CODE_RANGE)
	{
	  unexpected_type_code_complaint (sym_name);
	}
      else
	{
	  /* Usually, TYPE_CODE(tp) is already type_code.  The main
	     exception is if we guessed wrong re struct/union/enum. */
	  if (TYPE_CODE (tp) != type_code)
	    {
	      bad_tag_guess_complaint (sym_name);
	      TYPE_CODE (tp) = type_code;
	    }
	  if (TYPE_NAME (tp) == NULL
	      || strcmp (TYPE_NAME (tp), name) != 0)
	    TYPE_NAME (tp) = obsavestring (name, strlen (name),
					   &current_objfile->objfile_obstack);
	}
    }
  if (t->bt == btTypedef)
    {
      char *name;

      /* Try to cross reference this type, it should succeed.  */
      ax += cross_ref (fd, ax, &tp, type_code, &name, bigend, sym_name);
      if (tp == (struct type *) NULL)
	{
	  complaint (&symfile_complaints,
		     "unable to cross ref btTypedef for %s", sym_name);
	  tp = mdebug_type_int;
	}
    }

  /* Deal with range types */
  if (t->bt == btRange)
    {
      TYPE_NFIELDS (tp) = 2;
      TYPE_FIELDS (tp) = ((struct field *)
			  TYPE_ALLOC (tp, 2 * sizeof (struct field)));
      TYPE_FIELD_NAME (tp, 0) = obsavestring ("Low", strlen ("Low"),
					    &current_objfile->objfile_obstack);
      TYPE_FIELD_BITPOS (tp, 0) = AUX_GET_DNLOW (bigend, ax);
      ax++;
      TYPE_FIELD_NAME (tp, 1) = obsavestring ("High", strlen ("High"),
					    &current_objfile->objfile_obstack);
      TYPE_FIELD_BITPOS (tp, 1) = AUX_GET_DNHIGH (bigend, ax);
      ax++;
    }

  /* Parse all the type qualifiers now. If there are more
     than 6 the game will continue in the next aux */

  while (1)
    {
#define PARSE_TQ(tq) \
      if (t->tq != tqNil) \
	ax += upgrade_type(fd, &tp, t->tq, ax, bigend, sym_name); \
      else \
	break;

      PARSE_TQ (tq0);
      PARSE_TQ (tq1);
      PARSE_TQ (tq2);
      PARSE_TQ (tq3);
      PARSE_TQ (tq4);
      PARSE_TQ (tq5);
#undef	PARSE_TQ

      /* mips cc 2.x and gcc never put out continued aux entries.  */
      if (!t->continued)
	break;

      (*debug_swap->swap_tir_in) (bigend, &ax->a_ti, t);
      ax++;
    }

  /* Complain for illegal continuations due to corrupt aux entries.  */
  if (t->continued)
    complaint (&symfile_complaints, "illegal TIR continued for %s", sym_name);

  return tp;
}

/* Make up a complex type from a basic one.  Type is passed by
   reference in TPP and side-effected as necessary. The type
   qualifier TQ says how to handle the aux symbols at AX for
   the symbol SX we are currently analyzing.  BIGEND says whether
   aux symbols are big-endian or little-endian.
   Returns the number of aux symbols we parsed. */

static int
upgrade_type (int fd, struct type **tpp, int tq, union aux_ext *ax, int bigend,
	      char *sym_name)
{
  int off;
  struct type *t;

  /* Used in array processing */
  int rf, id;
  FDR *fh;
  struct type *range;
  struct type *indx;
  int lower, upper;
  RNDXR rndx;

  switch (tq)
    {
    case tqPtr:
      t = lookup_pointer_type (*tpp);
      *tpp = t;
      return 0;

    case tqProc:
      t = lookup_function_type (*tpp);
      *tpp = t;
      return 0;

    case tqArray:
      off = 0;

      /* Determine and record the domain type (type of index) */
      (*debug_swap->swap_rndx_in) (bigend, &ax->a_rndx, &rndx);
      id = rndx.index;
      rf = rndx.rfd;
      if (rf == 0xfff)
	{
	  ax++;
	  rf = AUX_GET_ISYM (bigend, ax);
	  off++;
	}
      fh = get_rfd (fd, rf);

      indx = parse_type (fh - debug_info->fdr,
			 debug_info->external_aux + fh->iauxBase,
			 id, (int *) NULL, bigend, sym_name);

      /* The bounds type should be an integer type, but might be anything
         else due to corrupt aux entries.  */
      if (TYPE_CODE (indx) != TYPE_CODE_INT)
	{
	  complaint (&symfile_complaints,
		     "illegal array index type for %s, assuming int", sym_name);
	  indx = mdebug_type_int;
	}

      /* Get the bounds, and create the array type.  */
      ax++;
      lower = AUX_GET_DNLOW (bigend, ax);
      ax++;
      upper = AUX_GET_DNHIGH (bigend, ax);
      ax++;
      rf = AUX_GET_WIDTH (bigend, ax);	/* bit size of array element */

      range = create_range_type ((struct type *) NULL, indx,
				 lower, upper);

      t = create_array_type ((struct type *) NULL, *tpp, range);

      /* We used to fill in the supplied array element bitsize
         here if the TYPE_LENGTH of the target type was zero.
         This happens for a `pointer to an array of anonymous structs',
         but in this case the array element bitsize is also zero,
         so nothing is gained.
         And we used to check the TYPE_LENGTH of the target type against
         the supplied array element bitsize.
         gcc causes a mismatch for `pointer to array of object',
         since the sdb directives it uses do not have a way of
         specifying the bitsize, but it does no harm (the
         TYPE_LENGTH should be correct) and we should be able to
         ignore the erroneous bitsize from the auxiliary entry safely.
         dbx seems to ignore it too.  */

      /* TYPE_FLAG_TARGET_STUB now takes care of the zero TYPE_LENGTH
         problem.  */
      if (TYPE_LENGTH (*tpp) == 0)
	{
	  TYPE_FLAGS (t) |= TYPE_FLAG_TARGET_STUB;
	}

      *tpp = t;
      return 4 + off;

    case tqVol:
      /* Volatile -- currently ignored */
      return 0;

    case tqConst:
      /* Const -- currently ignored */
      return 0;

    default:
      complaint (&symfile_complaints, "unknown type qualifier 0x%x", tq);
      return 0;
    }
}


/* Parse a procedure descriptor record PR.  Note that the procedure is
   parsed _after_ the local symbols, now we just insert the extra
   information we need into a MIPS_EFI_SYMBOL_NAME symbol that has
   already been placed in the procedure's main block.  Note also that
   images that have been partially stripped (ld -x) have been deprived
   of local symbols, and we have to cope with them here.  FIRST_OFF is
   the offset of the first procedure for this FDR; we adjust the
   address by this amount, but I don't know why.  SEARCH_SYMTAB is the symtab
   to look for the function which contains the MIPS_EFI_SYMBOL_NAME symbol
   in question, or NULL to use top_stack->cur_block.  */

static void parse_procedure (PDR *, struct symtab *, struct partial_symtab *);

static void
parse_procedure (PDR *pr, struct symtab *search_symtab,
		 struct partial_symtab *pst)
{
  struct symbol *s, *i;
  struct block *b;
  struct mips_extra_func_info *e;
  char *sh_name;

  /* Simple rule to find files linked "-x" */
  if (cur_fdr->rss == -1)
    {
      if (pr->isym == -1)
	{
	  /* Static procedure at address pr->adr.  Sigh. */
	  /* FIXME-32x64.  assuming pr->adr fits in long.  */
	  complaint (&symfile_complaints,
		     "can't handle PDR for static proc at 0x%lx",
		     (unsigned long) pr->adr);
	  return;
	}
      else
	{
	  /* external */
	  EXTR she;

	  (*debug_swap->swap_ext_in) (cur_bfd,
				      ((char *) debug_info->external_ext
				       + (pr->isym
					  * debug_swap->external_ext_size)),
				      &she);
	  sh_name = debug_info->ssext + she.asym.iss;
	}
    }
  else
    {
      /* Full symbols */
      SYMR sh;

      (*debug_swap->swap_sym_in) (cur_bfd,
				  ((char *) debug_info->external_sym
				   + ((cur_fdr->isymBase + pr->isym)
				      * debug_swap->external_sym_size)),
				  &sh);
      sh_name = debug_info->ss + cur_fdr->issBase + sh.iss;
    }

  if (search_symtab != NULL)
    {
#if 0
      /* This loses both in the case mentioned (want a static, find a global),
         but also if we are looking up a non-mangled name which happens to
         match the name of a mangled function.  */
      /* We have to save the cur_fdr across the call to lookup_symbol.
         If the pdr is for a static function and if a global function with
         the same name exists, lookup_symbol will eventually read in the symtab
         for the global function and clobber cur_fdr.  */
      FDR *save_cur_fdr = cur_fdr;
      s = lookup_symbol (sh_name, NULL, VAR_DOMAIN, 0, NULL);
      cur_fdr = save_cur_fdr;
#else
      s = mylookup_symbol
	(sh_name,
	 BLOCKVECTOR_BLOCK (BLOCKVECTOR (search_symtab), STATIC_BLOCK),
	 VAR_DOMAIN,
	 LOC_BLOCK);
#endif
    }
  else
    s = mylookup_symbol (sh_name, top_stack->cur_block,
			 VAR_DOMAIN, LOC_BLOCK);

  if (s != 0)
    {
      b = SYMBOL_BLOCK_VALUE (s);
    }
  else
    {
      complaint (&symfile_complaints, "PDR for %s, but no symbol", sh_name);
#if 1
      return;
#else
/* FIXME -- delete.  We can't do symbol allocation now; it's all done.  */
      s = new_symbol (sh_name);
      SYMBOL_DOMAIN (s) = VAR_DOMAIN;
      SYMBOL_CLASS (s) = LOC_BLOCK;
      /* Donno its type, hope int is ok */
      SYMBOL_TYPE (s) = lookup_function_type (mdebug_type_int);
      add_symbol (s, top_stack->cur_block);
      /* Wont have symbols for this one */
      b = new_block (2);
      SYMBOL_BLOCK_VALUE (s) = b;
      BLOCK_FUNCTION (b) = s;
      BLOCK_START (b) = pr->adr;
      /* BOUND used to be the end of procedure's text, but the
         argument is no longer passed in.  */
      BLOCK_END (b) = bound;
      BLOCK_SUPERBLOCK (b) = top_stack->cur_block;
      add_block (b, top_stack->cur_st);
#endif
    }

  i = mylookup_symbol (MIPS_EFI_SYMBOL_NAME, b, LABEL_DOMAIN, LOC_CONST);

  if (i)
    {
      e = (struct mips_extra_func_info *) SYMBOL_VALUE (i);
      e->pdr = *pr;
      e->pdr.isym = (long) s;

      /* GDB expects the absolute function start address for the
         procedure descriptor in e->pdr.adr.
         As the address in the procedure descriptor is usually relative,
         we would have to relocate e->pdr.adr with cur_fdr->adr and
         ANOFFSET (pst->section_offsets, SECT_OFF_TEXT (pst->objfile)).
         Unfortunately cur_fdr->adr and e->pdr.adr are both absolute
         in shared libraries on some systems, and on other systems
         e->pdr.adr is sometimes offset by a bogus value.
         To work around these problems, we replace e->pdr.adr with
         the start address of the function.  */
      e->pdr.adr = BLOCK_START (b);

      /* Correct incorrect setjmp procedure descriptor from the library
         to make backtrace through setjmp work.  */
      if (e->pdr.pcreg == 0
	  && strcmp (sh_name, "setjmp") == 0)
	{
	  complaint (&symfile_complaints, "fixing bad setjmp PDR from libc");
	  e->pdr.pcreg = RA_REGNUM;
	  e->pdr.regmask = 0x80000000;
	  e->pdr.regoffset = -4;
	}
    }

  /* It would be reasonable that functions that have been compiled
     without debugging info have a btNil type for their return value,
     and functions that are void and are compiled with debugging info
     have btVoid.
     gcc and DEC f77 put out btNil types for both cases, so btNil is mapped
     to TYPE_CODE_VOID in parse_type to get the `compiled with debugging info'
     case right.
     The glevel field in cur_fdr could be used to determine the presence
     of debugging info, but GCC doesn't always pass the -g switch settings
     to the assembler and GAS doesn't set the glevel field from the -g switch
     settings.
     To work around these problems, the return value type of a TYPE_CODE_VOID
     function is adjusted accordingly if no debugging info was found in the
     compilation unit.  */

  if (processing_gcc_compilation == 0
      && found_ecoff_debugging_info == 0
      && TYPE_CODE (TYPE_TARGET_TYPE (SYMBOL_TYPE (s))) == TYPE_CODE_VOID)
    SYMBOL_TYPE (s) = nodebug_func_symbol_type;
}

/* Relocate the extra function info pointed to by the symbol table.  */

void
ecoff_relocate_efi (struct symbol *sym, CORE_ADDR delta)
{
  struct mips_extra_func_info *e;

  e = (struct mips_extra_func_info *) SYMBOL_VALUE (sym);

  e->pdr.adr += delta;
}

/* Parse the external symbol ES. Just call parse_symbol() after
   making sure we know where the aux are for it.
   BIGEND says whether aux entries are big-endian or little-endian.

   This routine clobbers top_stack->cur_block and ->cur_st. */

static void parse_external (EXTR *, int, struct section_offsets *,
			    struct objfile *);

static void
parse_external (EXTR *es, int bigend, struct section_offsets *section_offsets,
		struct objfile *objfile)
{
  union aux_ext *ax;

  if (es->ifd != ifdNil)
    {
      cur_fd = es->ifd;
      cur_fdr = debug_info->fdr + cur_fd;
      ax = debug_info->external_aux + cur_fdr->iauxBase;
    }
  else
    {
      cur_fdr = debug_info->fdr;
      ax = 0;
    }

  /* Reading .o files */
  if (SC_IS_UNDEF (es->asym.sc) || es->asym.sc == scNil)
    {
      char *what;
      switch (es->asym.st)
	{
	case stNil:
	  /* These are generated for static symbols in .o files,
	     ignore them.  */
	  return;
	case stStaticProc:
	case stProc:
	  what = "procedure";
	  n_undef_procs++;
	  break;
	case stGlobal:
	  what = "variable";
	  n_undef_vars++;
	  break;
	case stLabel:
	  what = "label";
	  n_undef_labels++;
	  break;
	default:
	  what = "symbol";
	  break;
	}
      n_undef_symbols++;
      /* FIXME:  Turn this into a complaint? */
      if (info_verbose)
	printf_filtered ("Warning: %s `%s' is undefined (in %s)\n",
			 what, debug_info->ssext + es->asym.iss,
			 fdr_name (cur_fdr));
      return;
    }

  switch (es->asym.st)
    {
    case stProc:
    case stStaticProc:
      /* There is no need to parse the external procedure symbols.
         If they are from objects compiled without -g, their index will
         be indexNil, and the symbol definition from the minimal symbol
         is preferrable (yielding a function returning int instead of int).
         If the index points to a local procedure symbol, the local
         symbol already provides the correct type.
         Note that the index of the external procedure symbol points
         to the local procedure symbol in the local symbol table, and
         _not_ to the auxiliary symbol info.  */
      break;
    case stGlobal:
    case stLabel:
      /* Global common symbols are resolved by the runtime loader,
         ignore them.  */
      if (SC_IS_COMMON (es->asym.sc))
	break;

      /* Note that the case of a symbol with indexNil must be handled
         anyways by parse_symbol().  */
      parse_symbol (&es->asym, ax, (char *) NULL, bigend, section_offsets, objfile);
      break;
    default:
      break;
    }
}

/* Parse the line number info for file descriptor FH into
   GDB's linetable LT.  MIPS' encoding requires a little bit
   of magic to get things out.  Note also that MIPS' line
   numbers can go back and forth, apparently we can live
   with that and do not need to reorder our linetables */

static void parse_lines (FDR *, PDR *, struct linetable *, int,
			 struct partial_symtab *, CORE_ADDR);

static void
parse_lines (FDR *fh, PDR *pr, struct linetable *lt, int maxlines,
	     struct partial_symtab *pst, CORE_ADDR lowest_pdr_addr)
{
  unsigned char *base;
  int j, k;
  int delta, count, lineno = 0;

  if (fh->cbLine == 0)
    return;

  /* Scan by procedure descriptors */
  k = 0;
  for (j = 0; j < fh->cpd; j++, pr++)
    {
      CORE_ADDR l;
      CORE_ADDR adr;
      unsigned char *halt;

      /* No code for this one */
      if (pr->iline == ilineNil ||
	  pr->lnLow == -1 || pr->lnHigh == -1)
	continue;

      /* Determine start and end address of compressed line bytes for
         this procedure.  */
      base = debug_info->line + fh->cbLineOffset;
      if (j != (fh->cpd - 1))
	halt = base + pr[1].cbLineOffset;
      else
	halt = base + fh->cbLine;
      base += pr->cbLineOffset;

      adr = pst->textlow + pr->adr - lowest_pdr_addr;

      l = adr >> 2;		/* in words */
      for (lineno = pr->lnLow; base < halt;)
	{
	  count = *base & 0x0f;
	  delta = *base++ >> 4;
	  if (delta >= 8)
	    delta -= 16;
	  if (delta == -8)
	    {
	      delta = (base[0] << 8) | base[1];
	      if (delta >= 0x8000)
		delta -= 0x10000;
	      base += 2;
	    }
	  lineno += delta;	/* first delta is 0 */

	  /* Complain if the line table overflows. Could happen
	     with corrupt binaries.  */
	  if (lt->nitems >= maxlines)
	    {
	      complaint (&symfile_complaints,
			 "guessed size of linetable for %s incorrectly",
			 fdr_name (fh));
	      break;
	    }
	  k = add_line (lt, lineno, l, k);
	  l += count + 1;
	}
    }
}

static void
function_outside_compilation_unit_complaint (const char *arg1)
{
  complaint (&symfile_complaints,
	     "function `%s' appears to be defined outside of all compilation units",
	     arg1);
}

/* Master parsing procedure for first-pass reading of file symbols
   into a partial_symtab.  */

static void
parse_partial_symbols (struct objfile *objfile)
{
  const bfd_size_type external_sym_size = debug_swap->external_sym_size;
  const bfd_size_type external_rfd_size = debug_swap->external_rfd_size;
  const bfd_size_type external_ext_size = debug_swap->external_ext_size;
  void (*const swap_ext_in) (bfd *, void *, EXTR *) = debug_swap->swap_ext_in;
  void (*const swap_sym_in) (bfd *, void *, SYMR *) = debug_swap->swap_sym_in;
  void (*const swap_rfd_in) (bfd *, void *, RFDT *) = debug_swap->swap_rfd_in;
  int f_idx, s_idx;
  HDRR *hdr = &debug_info->symbolic_header;
  /* Running pointers */
  FDR *fh;
  char *ext_out;
  char *ext_out_end;
  EXTR *ext_block;
  EXTR *ext_in;
  EXTR *ext_in_end;
  SYMR sh;
  struct partial_symtab *pst;
  int textlow_not_set = 1;
  int past_first_source_file = 0;

  /* List of current psymtab's include files */
  char **psymtab_include_list;
  int includes_allocated;
  int includes_used;
  EXTR *extern_tab;
  struct pst_map *fdr_to_pst;
  /* Index within current psymtab dependency list */
  struct partial_symtab **dependency_list;
  int dependencies_used, dependencies_allocated;
  struct cleanup *old_chain;
  char *name;
  enum language prev_language;
  asection *text_sect;
  int relocatable = 0;

  /* Irix 5.2 shared libraries have a fh->adr field of zero, but
     the shared libraries are prelinked at a high memory address.
     We have to adjust the start address of the object file for this case,
     by setting it to the start address of the first procedure in the file.
     But we should do no adjustments if we are debugging a .o file, where
     the text section (and fh->adr) really starts at zero.  */
  text_sect = bfd_get_section_by_name (cur_bfd, ".text");
  if (text_sect != NULL
      && (bfd_get_section_flags (cur_bfd, text_sect) & SEC_RELOC))
    relocatable = 1;

  extern_tab = (EXTR *) obstack_alloc (&objfile->objfile_obstack,
				       sizeof (EXTR) * hdr->iextMax);

  includes_allocated = 30;
  includes_used = 0;
  psymtab_include_list = (char **) alloca (includes_allocated *
					   sizeof (char *));
  next_symbol_text_func = mdebug_next_symbol_text;

  dependencies_allocated = 30;
  dependencies_used = 0;
  dependency_list =
    (struct partial_symtab **) alloca (dependencies_allocated *
				       sizeof (struct partial_symtab *));

  last_source_file = NULL;

  /*
   * Big plan:
   *
   * Only parse the Local and External symbols, and the Relative FDR.
   * Fixup enough of the loader symtab to be able to use it.
   * Allocate space only for the file's portions we need to
   * look at. (XXX)
   */

  max_gdbinfo = 0;
  max_glevel = MIN_GLEVEL;

  /* Allocate the map FDR -> PST.
     Minor hack: -O3 images might claim some global data belongs
     to FDR -1. We`ll go along with that */
  fdr_to_pst = (struct pst_map *) xzalloc ((hdr->ifdMax + 1) * sizeof *fdr_to_pst);
  old_chain = make_cleanup (xfree, fdr_to_pst);
  fdr_to_pst++;
  {
    struct partial_symtab *pst = new_psymtab ("", objfile);
    fdr_to_pst[-1].pst = pst;
    FDR_IDX (pst) = -1;
  }

  /* Allocate the global pending list.  */
  pending_list =
    ((struct mdebug_pending **)
     obstack_alloc (&objfile->objfile_obstack,
		    hdr->ifdMax * sizeof (struct mdebug_pending *)));
  memset (pending_list, 0,
	  hdr->ifdMax * sizeof (struct mdebug_pending *));

  /* Pass 0 over external syms: swap them in.  */
  ext_block = (EXTR *) xmalloc (hdr->iextMax * sizeof (EXTR));
  make_cleanup (xfree, ext_block);

  ext_out = (char *) debug_info->external_ext;
  ext_out_end = ext_out + hdr->iextMax * external_ext_size;
  ext_in = ext_block;
  for (; ext_out < ext_out_end; ext_out += external_ext_size, ext_in++)
    (*swap_ext_in) (cur_bfd, ext_out, ext_in);

  /* Pass 1 over external syms: Presize and partition the list */
  ext_in = ext_block;
  ext_in_end = ext_in + hdr->iextMax;
  for (; ext_in < ext_in_end; ext_in++)
    {
      /* See calls to complain below.  */
      if (ext_in->ifd >= -1
	  && ext_in->ifd < hdr->ifdMax
	  && ext_in->asym.iss >= 0
	  && ext_in->asym.iss < hdr->issExtMax)
	fdr_to_pst[ext_in->ifd].n_globals++;
    }

  /* Pass 1.5 over files:  partition out global symbol space */
  s_idx = 0;
  for (f_idx = -1; f_idx < hdr->ifdMax; f_idx++)
    {
      fdr_to_pst[f_idx].globals_offset = s_idx;
      s_idx += fdr_to_pst[f_idx].n_globals;
      fdr_to_pst[f_idx].n_globals = 0;
    }

  /* ECOFF in ELF:

     For ECOFF in ELF, we skip the creation of the minimal symbols.
     The ECOFF symbols should be a subset of the Elf symbols, and the 
     section information of the elf symbols will be more accurate.
     FIXME!  What about Irix 5's native linker?

     By default, Elf sections which don't exist in ECOFF 
     get put in ECOFF's absolute section by the gnu linker.
     Since absolute sections don't get relocated, we 
     end up calculating an address different from that of 
     the symbol's minimal symbol (created earlier from the
     Elf symtab).  

     To fix this, either :
     1) don't create the duplicate symbol
     (assumes ECOFF symtab is a subset of the ELF symtab;
     assumes no side-effects result from ignoring ECOFF symbol)
     2) create it, only if lookup for existing symbol in ELF's minimal 
     symbols fails
     (inefficient; 
     assumes no side-effects result from ignoring ECOFF symbol)
     3) create it, but lookup ELF's minimal symbol and use it's section
     during relocation, then modify "uniqify" phase to merge and 
     eliminate the duplicate symbol
     (highly inefficient)

     I've implemented #1 here...
     Skip the creation of the minimal symbols based on the ECOFF 
     symbol table. */

  /* Pass 2 over external syms: fill in external symbols */
  ext_in = ext_block;
  ext_in_end = ext_in + hdr->iextMax;
  for (; ext_in < ext_in_end; ext_in++)
    {
      enum minimal_symbol_type ms_type = mst_text;
      CORE_ADDR svalue = ext_in->asym.value;

      /* The Irix 5 native tools seem to sometimes generate bogus
         external symbols.  */
      if (ext_in->ifd < -1 || ext_in->ifd >= hdr->ifdMax)
	{
	  complaint (&symfile_complaints,
		     "bad ifd for external symbol: %d (max %ld)", ext_in->ifd,
		     hdr->ifdMax);
	  continue;
	}
      if (ext_in->asym.iss < 0 || ext_in->asym.iss >= hdr->issExtMax)
	{
	  complaint (&symfile_complaints,
		     "bad iss for external symbol: %ld (max %ld)",
		     ext_in->asym.iss, hdr->issExtMax);
	  continue;
	}

      extern_tab[fdr_to_pst[ext_in->ifd].globals_offset
		 + fdr_to_pst[ext_in->ifd].n_globals++] = *ext_in;


      if (SC_IS_UNDEF (ext_in->asym.sc) || ext_in->asym.sc == scNil)
	continue;


      /* Pass 3 over files, over local syms: fill in static symbols */
      name = debug_info->ssext + ext_in->asym.iss;

      /* Process ECOFF Symbol Types and Storage Classes */
      switch (ext_in->asym.st)
	{
	case stProc:
	  /* Beginnning of Procedure */
	  svalue += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
	  break;
	case stStaticProc:
	  /* Load time only static procs */
	  ms_type = mst_file_text;
	  svalue += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
	  break;
	case stGlobal:
	  /* External symbol */
	  if (SC_IS_COMMON (ext_in->asym.sc))
	    {
	      /* The value of a common symbol is its size, not its address.
	         Ignore it.  */
	      continue;
	    }
	  else if (SC_IS_DATA (ext_in->asym.sc))
	    {
	      ms_type = mst_data;
	      svalue += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
	    }
	  else if (SC_IS_BSS (ext_in->asym.sc))
	    {
	      ms_type = mst_bss;
	      svalue += ANOFFSET (objfile->section_offsets, SECT_OFF_BSS (objfile));
	    }
          else if (SC_IS_SBSS (ext_in->asym.sc))
            {
              ms_type = mst_bss;
              svalue += ANOFFSET (objfile->section_offsets, 
                                  get_section_index (objfile, ".sbss"));
            }
	  else
	    ms_type = mst_abs;
	  break;
	case stLabel:
	  /* Label */

          /* On certain platforms, some extra label symbols can be
             generated by the linker. One possible usage for this kind
             of symbols is to represent the address of the begining of a
             given section. For instance, on Tru64 5.1, the address of
             the _ftext label is the start address of the .text section.

             The storage class of these symbols is usually directly
             related to the section to which the symbol refers. For
             instance, on Tru64 5.1, the storage class for the _fdata
             label is scData, refering to the .data section.

             It is actually possible that the section associated to the
             storage class of the label does not exist. On True64 5.1
             for instance, the libm.so shared library does not contain
             any .data section, although it contains a _fpdata label
             which storage class is scData... Since these symbols are
             usually useless for the debugger user anyway, we just
             discard these symbols.
           */
          
	  if (SC_IS_TEXT (ext_in->asym.sc))
	    {
              if (objfile->sect_index_text == -1)
                continue;
                
	      ms_type = mst_file_text;
	      svalue += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
	    }
	  else if (SC_IS_DATA (ext_in->asym.sc))
	    {
              if (objfile->sect_index_data == -1)
                continue;

	      ms_type = mst_file_data;
	      svalue += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
	    }
	  else if (SC_IS_BSS (ext_in->asym.sc))
	    {
              if (objfile->sect_index_bss == -1)
                continue;

	      ms_type = mst_file_bss;
	      svalue += ANOFFSET (objfile->section_offsets, SECT_OFF_BSS (objfile));
	    }
          else if (SC_IS_SBSS (ext_in->asym.sc))
            {
              const int sbss_sect_index = get_section_index (objfile, ".sbss");

              if (sbss_sect_index == -1)
                continue;

              ms_type = mst_file_bss;
              svalue += ANOFFSET (objfile->section_offsets, sbss_sect_index);
            }
	  else
	    ms_type = mst_abs;
	  break;
	case stLocal:
	case stNil:
	  /* The alpha has the section start addresses in stLocal symbols
	     whose name starts with a `.'. Skip those but complain for all
	     other stLocal symbols.
	     Irix6 puts the section start addresses in stNil symbols, skip
	     those too. */
	  if (name[0] == '.')
	    continue;
	  /* Fall through.  */
	default:
	  ms_type = mst_unknown;
	  unknown_ext_complaint (name);
	}
      if (!ECOFF_IN_ELF (cur_bfd))
	prim_record_minimal_symbol (name, svalue, ms_type, objfile);
    }

  /* Pass 3 over files, over local syms: fill in static symbols */
  for (f_idx = 0; f_idx < hdr->ifdMax; f_idx++)
    {
      struct partial_symtab *save_pst;
      EXTR *ext_ptr;
      CORE_ADDR textlow;

      cur_fdr = fh = debug_info->fdr + f_idx;

      if (fh->csym == 0)
	{
	  fdr_to_pst[f_idx].pst = NULL;
	  continue;
	}

      /* Determine the start address for this object file from the
         file header and relocate it, except for Irix 5.2 zero fh->adr.  */
      if (fh->cpd)
	{
	  textlow = fh->adr;
	  if (relocatable || textlow != 0)
	    textlow += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
	}
      else
	textlow = 0;
      pst = start_psymtab_common (objfile, objfile->section_offsets,
				  fdr_name (fh),
				  textlow,
				  objfile->global_psymbols.next,
				  objfile->static_psymbols.next);
      pst->read_symtab_private = ((char *)
				  obstack_alloc (&objfile->objfile_obstack,
						 sizeof (struct symloc)));
      memset (pst->read_symtab_private, 0, sizeof (struct symloc));

      save_pst = pst;
      FDR_IDX (pst) = f_idx;
      CUR_BFD (pst) = cur_bfd;
      DEBUG_SWAP (pst) = debug_swap;
      DEBUG_INFO (pst) = debug_info;
      PENDING_LIST (pst) = pending_list;

      /* The way to turn this into a symtab is to call... */
      pst->read_symtab = mdebug_psymtab_to_symtab;

      /* Set up language for the pst.
         The language from the FDR is used if it is unambigious (e.g. cfront
         with native cc and g++ will set the language to C).
         Otherwise we have to deduce the language from the filename.
         Native ecoff has every header file in a separate FDR, so
         deduce_language_from_filename will return language_unknown for
         a header file, which is not what we want.
         But the FDRs for the header files are after the FDR for the source
         file, so we can assign the language of the source file to the
         following header files. Then we save the language in the private
         pst data so that we can reuse it when building symtabs.  */
      prev_language = psymtab_language;

      switch (fh->lang)
	{
	case langCplusplusV2:
	  psymtab_language = language_cplus;
	  break;
	default:
	  psymtab_language = deduce_language_from_filename (fdr_name (fh));
	  break;
	}
      if (psymtab_language == language_unknown)
	psymtab_language = prev_language;
      PST_PRIVATE (pst)->pst_language = psymtab_language;

      pst->texthigh = pst->textlow;

      /* For stabs-in-ecoff files, the second symbol must be @stab.
         This symbol is emitted by mips-tfile to signal that the
         current object file uses encapsulated stabs instead of mips
         ecoff for local symbols.  (It is the second symbol because
         the first symbol is the stFile used to signal the start of a
         file). */
      processing_gcc_compilation = 0;
      if (fh->csym >= 2)
	{
	  (*swap_sym_in) (cur_bfd,
			  ((char *) debug_info->external_sym
			   + (fh->isymBase + 1) * external_sym_size),
			  &sh);
	  if (strcmp (debug_info->ss + fh->issBase + sh.iss,
		      stabs_symbol) == 0)
	    processing_gcc_compilation = 2;
	}

      if (processing_gcc_compilation != 0)
	{
	  for (cur_sdx = 2; cur_sdx < fh->csym; cur_sdx++)
	    {
	      int type_code;
	      char *namestring;

	      (*swap_sym_in) (cur_bfd,
			      (((char *) debug_info->external_sym)
			    + (fh->isymBase + cur_sdx) * external_sym_size),
			      &sh);
	      type_code = ECOFF_UNMARK_STAB (sh.index);
	      if (!ECOFF_IS_STAB (&sh))
		{
		  if (sh.st == stProc || sh.st == stStaticProc)
		    {
		      CORE_ADDR procaddr;
		      long isym;

		      sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
		      if (sh.st == stStaticProc)
			{
			  namestring = debug_info->ss + fh->issBase + sh.iss;
			  prim_record_minimal_symbol_and_info (namestring,
							       sh.value,
							       mst_file_text,
							       NULL,
							       SECT_OFF_TEXT (objfile),
							       NULL,
							       objfile);
			}
		      procaddr = sh.value;

		      isym = AUX_GET_ISYM (fh->fBigendian,
					   (debug_info->external_aux
					    + fh->iauxBase
					    + sh.index));
		      (*swap_sym_in) (cur_bfd,
				      ((char *) debug_info->external_sym
				       + ((fh->isymBase + isym - 1)
					  * external_sym_size)),
				      &sh);
		      if (sh.st == stEnd)
			{
			  CORE_ADDR high = procaddr + sh.value;

			  /* Kludge for Irix 5.2 zero fh->adr.  */
			  if (!relocatable
			  && (pst->textlow == 0 || procaddr < pst->textlow))
			    pst->textlow = procaddr;
			  if (high > pst->texthigh)
			    pst->texthigh = high;
			}
		    }
		  else if (sh.st == stStatic)
		    {
		      switch (sh.sc)
			{
			case scUndefined:
			case scSUndefined:
			case scNil:
			case scAbs:
			  break;

			case scData:
			case scSData:
			case scRData:
			case scPData:
			case scXData:
			  namestring = debug_info->ss + fh->issBase + sh.iss;
			  sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
			  prim_record_minimal_symbol_and_info (namestring,
							       sh.value,
							       mst_file_data,
							       NULL,
							       SECT_OFF_DATA (objfile),
							       NULL,
							       objfile);
			  break;

			default:
			  /* FIXME!  Shouldn't this use cases for bss, 
			     then have the default be abs? */
			  namestring = debug_info->ss + fh->issBase + sh.iss;
			  sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_BSS (objfile));
			  prim_record_minimal_symbol_and_info (namestring,
							       sh.value,
							       mst_file_bss,
							       NULL,
							       SECT_OFF_BSS (objfile),
							       NULL,
							       objfile);
			  break;
			}
		    }
		  continue;
		}
	      /* Handle stabs continuation */
	      {
		char *stabstring = debug_info->ss + fh->issBase + sh.iss;
		int len = strlen (stabstring);
		while (stabstring[len - 1] == '\\')
		  {
		    SYMR sh2;
		    char *stabstring1 = stabstring;
		    char *stabstring2;
		    int len2;

		    /* Ignore continuation char from 1st string */
		    len--;

		    /* Read next stabstring */
		    cur_sdx++;
		    (*swap_sym_in) (cur_bfd,
				    (((char *) debug_info->external_sym)
				     + (fh->isymBase + cur_sdx)
				     * external_sym_size),
				    &sh2);
		    stabstring2 = debug_info->ss + fh->issBase + sh2.iss;
		    len2 = strlen (stabstring2);

		    /* Concatinate stabstring2 with stabstring1 */
		    if (stabstring
		     && stabstring != debug_info->ss + fh->issBase + sh.iss)
		      stabstring = xrealloc (stabstring, len + len2 + 1);
		    else
		      {
			stabstring = xmalloc (len + len2 + 1);
			strcpy (stabstring, stabstring1);
		      }
		    strcpy (stabstring + len, stabstring2);
		    len += len2;
		  }

		switch (type_code)
		  {
		    char *p;
		    /*
		     * Standard, external, non-debugger, symbols
		     */

		  case N_TEXT | N_EXT:
		  case N_NBTEXT | N_EXT:
		    sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
		    goto record_it;

		  case N_DATA | N_EXT:
		  case N_NBDATA | N_EXT:
		    sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
		    goto record_it;

		  case N_BSS:
		  case N_BSS | N_EXT:
		  case N_NBBSS | N_EXT:
		  case N_SETV | N_EXT:		/* FIXME, is this in BSS? */
		    sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_BSS (objfile));
		    goto record_it;

		  case N_ABS | N_EXT:
		  record_it:
		  continue;

		  /* Standard, local, non-debugger, symbols */

		  case N_NBTEXT:

		    /* We need to be able to deal with both N_FN or N_TEXT,
		       because we have no way of knowing whether the sys-supplied ld
		       or GNU ld was used to make the executable.  Sequents throw
		       in another wrinkle -- they renumbered N_FN.  */

		  case N_FN:
		  case N_FN_SEQ:
		  case N_TEXT:
		    continue;

		  case N_DATA:
		    sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
		    goto record_it;

		  case N_UNDF | N_EXT:
		    continue;			/* Just undefined, not COMMON */

		  case N_UNDF:
		    continue;

		    /* Lots of symbol types we can just ignore.  */

		  case N_ABS:
		  case N_NBDATA:
		  case N_NBBSS:
		    continue;

		    /* Keep going . . . */

		    /*
		     * Special symbol types for GNU
		     */
		  case N_INDR:
		  case N_INDR | N_EXT:
		  case N_SETA:
		  case N_SETA | N_EXT:
		  case N_SETT:
		  case N_SETT | N_EXT:
		  case N_SETD:
		  case N_SETD | N_EXT:
		  case N_SETB:
		  case N_SETB | N_EXT:
		  case N_SETV:
		    continue;

		    /*
		     * Debugger symbols
		     */

		  case N_SO:
		    {
		      CORE_ADDR valu;
		      static int prev_so_symnum = -10;
		      static int first_so_symnum;
		      char *p;
		      int prev_textlow_not_set;

		      valu = sh.value + ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));

		      prev_textlow_not_set = textlow_not_set;

#ifdef SOFUN_ADDRESS_MAYBE_MISSING
		      /* A zero value is probably an indication for the SunPRO 3.0
			 compiler. end_psymtab explicitly tests for zero, so
			 don't relocate it.  */

		      if (sh.value == 0)
			{
			  textlow_not_set = 1;
			  valu = 0;
			}
		      else
			textlow_not_set = 0;
#else
		      textlow_not_set = 0;
#endif
		      past_first_source_file = 1;

		      if (prev_so_symnum != symnum - 1)
			{			/* Here if prev stab wasn't N_SO */
			  first_so_symnum = symnum;

			  if (pst)
			    {
			      pst = (struct partial_symtab *) 0;
			      includes_used = 0;
			      dependencies_used = 0;
			    }
			}

		      prev_so_symnum = symnum;

		      /* End the current partial symtab and start a new one */

		      /* SET_NAMESTRING ();*/
		      namestring = stabstring;

		      /* Null name means end of .o file.  Don't start a new one. */
		      if (*namestring == '\000')
			continue;

		      /* Some compilers (including gcc) emit a pair of initial N_SOs.
			 The first one is a directory name; the second the file name.
			 If pst exists, is empty, and has a filename ending in '/',
			 we assume the previous N_SO was a directory name. */

		      p = strrchr (namestring, '/');
		      if (p && *(p + 1) == '\000')
			continue;		/* Simply ignore directory name SOs */

		      /* Some other compilers (C++ ones in particular) emit useless
			 SOs for non-existant .c files.  We ignore all subsequent SOs that
			 immediately follow the first.  */

		      if (!pst)
			pst = save_pst;
		      continue;
		    }

		  case N_BINCL:
		    continue;

		  case N_SOL:
		    {
		      enum language tmp_language;
		      /* Mark down an include file in the current psymtab */

		      /* SET_NAMESTRING ();*/
		      namestring = stabstring;

		      tmp_language = deduce_language_from_filename (namestring);

		      /* Only change the psymtab's language if we've learned
			 something useful (eg. tmp_language is not language_unknown).
			 In addition, to match what start_subfile does, never change
			 from C++ to C.  */
		      if (tmp_language != language_unknown
			  && (tmp_language != language_c
			      || psymtab_language != language_cplus))
			psymtab_language = tmp_language;

		      /* In C++, one may expect the same filename to come round many
			 times, when code is coming alternately from the main file
			 and from inline functions in other files. So I check to see
			 if this is a file we've seen before -- either the main
			 source file, or a previously included file.

			 This seems to be a lot of time to be spending on N_SOL, but
			 things like "break c-exp.y:435" need to work (I
			 suppose the psymtab_include_list could be hashed or put
			 in a binary tree, if profiling shows this is a major hog).  */
		      if (pst && strcmp (namestring, pst->filename) == 0)
			continue;
		      {
			int i;
			for (i = 0; i < includes_used; i++)
			  if (strcmp (namestring,
				      psymtab_include_list[i]) == 0)
			    {
			      i = -1;
			      break;
			    }
			if (i == -1)
			  continue;
		      }

		      psymtab_include_list[includes_used++] = namestring;
		      if (includes_used >= includes_allocated)
			{
			  char **orig = psymtab_include_list;

			  psymtab_include_list = (char **)
			    alloca ((includes_allocated *= 2) *
				    sizeof (char *));
			  memcpy (psymtab_include_list, orig,
				  includes_used * sizeof (char *));
			}
		      continue;
		    }
		  case N_LSYM:			/* Typedef or automatic variable. */
		  case N_STSYM:		/* Data seg var -- static  */
		  case N_LCSYM:		/* BSS      "  */
		  case N_ROSYM:		/* Read-only data seg var -- static.  */
		  case N_NBSTS:		/* Gould nobase.  */
		  case N_NBLCS:		/* symbols.  */
		  case N_FUN:
		  case N_GSYM:			/* Global (extern) variable; can be
						   data or bss (sigh FIXME).  */

		    /* Following may probably be ignored; I'll leave them here
		       for now (until I do Pascal and Modula 2 extensions).  */

		  case N_PC:			/* I may or may not need this; I
						   suspect not.  */
		  case N_M2C:			/* I suspect that I can ignore this here. */
		  case N_SCOPE:		/* Same.   */

		    /*    SET_NAMESTRING ();*/
		    namestring = stabstring;
		    p = (char *) strchr (namestring, ':');
		    if (!p)
		      continue;			/* Not a debugging symbol.   */



		    /* Main processing section for debugging symbols which
		       the initial read through the symbol tables needs to worry
		       about.  If we reach this point, the symbol which we are
		       considering is definitely one we are interested in.
		       p must also contain the (valid) index into the namestring
		       which indicates the debugging type symbol.  */

		    switch (p[1])
		      {
		      case 'S':
			sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
#ifdef STATIC_TRANSFORM_NAME
			namestring = STATIC_TRANSFORM_NAME (namestring);
#endif
			add_psymbol_to_list (namestring, p - namestring,
					     VAR_DOMAIN, LOC_STATIC,
					     &objfile->static_psymbols,
					     0, sh.value,
					     psymtab_language, objfile);
			continue;
		      case 'G':
			sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
			/* The addresses in these entries are reported to be
			   wrong.  See the code that reads 'G's for symtabs. */
			add_psymbol_to_list (namestring, p - namestring,
					     VAR_DOMAIN, LOC_STATIC,
					     &objfile->global_psymbols,
					     0, sh.value,
					     psymtab_language, objfile);
			continue;

		      case 'T':
			/* When a 'T' entry is defining an anonymous enum, it
			   may have a name which is the empty string, or a
			   single space.  Since they're not really defining a
			   symbol, those shouldn't go in the partial symbol
			   table.  We do pick up the elements of such enums at
			   'check_enum:', below.  */
			if (p >= namestring + 2
			    || (p == namestring + 1
				&& namestring[0] != ' '))
			  {
			    add_psymbol_to_list (namestring, p - namestring,
						 STRUCT_DOMAIN, LOC_TYPEDEF,
						 &objfile->static_psymbols,
						 sh.value, 0,
						 psymtab_language, objfile);
			    if (p[2] == 't')
			      {
				/* Also a typedef with the same name.  */
				add_psymbol_to_list (namestring, p - namestring,
						     VAR_DOMAIN, LOC_TYPEDEF,
						     &objfile->static_psymbols,
						     sh.value, 0,
						     psymtab_language, objfile);
				p += 1;
			      }
			  }
			goto check_enum;
		      case 't':
			if (p != namestring)	/* a name is there, not just :T... */
			  {
			    add_psymbol_to_list (namestring, p - namestring,
						 VAR_DOMAIN, LOC_TYPEDEF,
						 &objfile->static_psymbols,
						 sh.value, 0,
						 psymtab_language, objfile);
			  }
		      check_enum:
			/* If this is an enumerated type, we need to
			   add all the enum constants to the partial symbol
			   table.  This does not cover enums without names, e.g.
			   "enum {a, b} c;" in C, but fortunately those are
			   rare.  There is no way for GDB to find those from the
			   enum type without spending too much time on it.  Thus
			   to solve this problem, the compiler needs to put out the
			   enum in a nameless type.  GCC2 does this.  */

			/* We are looking for something of the form
			   <name> ":" ("t" | "T") [<number> "="] "e"
			   {<constant> ":" <value> ","} ";".  */

			/* Skip over the colon and the 't' or 'T'.  */
			p += 2;
			/* This type may be given a number.  Also, numbers can come
			   in pairs like (0,26).  Skip over it.  */
			while ((*p >= '0' && *p <= '9')
			       || *p == '(' || *p == ',' || *p == ')'
			       || *p == '=')
			  p++;

			if (*p++ == 'e')
			  {
			    /* The aix4 compiler emits extra crud before the members.  */
			    if (*p == '-')
			      {
				/* Skip over the type (?).  */
				while (*p != ':')
				  p++;

				/* Skip over the colon.  */
				p++;
			      }

			    /* We have found an enumerated type.  */
			    /* According to comments in read_enum_type
			       a comma could end it instead of a semicolon.
			       I don't know where that happens.
			       Accept either.  */
			    while (*p && *p != ';' && *p != ',')
			      {
				char *q;

				/* Check for and handle cretinous dbx symbol name
				   continuation!  */
				if (*p == '\\' || (*p == '?' && p[1] == '\0'))
				  p = next_symbol_text (objfile);

				/* Point to the character after the name
				   of the enum constant.  */
				for (q = p; *q && *q != ':'; q++)
				  ;
				/* Note that the value doesn't matter for
				   enum constants in psymtabs, just in symtabs.  */
				add_psymbol_to_list (p, q - p,
						     VAR_DOMAIN, LOC_CONST,
						     &objfile->static_psymbols, 0,
						     0, psymtab_language, objfile);
				/* Point past the name.  */
				p = q;
				/* Skip over the value.  */
				while (*p && *p != ',')
				  p++;
				/* Advance past the comma.  */
				if (*p)
				  p++;
			      }
			  }
			continue;
		      case 'c':
			/* Constant, e.g. from "const" in Pascal.  */
			add_psymbol_to_list (namestring, p - namestring,
					     VAR_DOMAIN, LOC_CONST,
					     &objfile->static_psymbols, sh.value,
					     0, psymtab_language, objfile);
			continue;

		      case 'f':
			if (! pst)
			  {
			    int name_len = p - namestring;
			    char *name = xmalloc (name_len + 1);
			    memcpy (name, namestring, name_len);
			    name[name_len] = '\0';
			    function_outside_compilation_unit_complaint (name);
			    xfree (name);
			  }
			sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
			add_psymbol_to_list (namestring, p - namestring,
					     VAR_DOMAIN, LOC_BLOCK,
					     &objfile->static_psymbols,
					     0, sh.value,
					     psymtab_language, objfile);
			continue;

			/* Global functions were ignored here, but now they
			   are put into the global psymtab like one would expect.
			   They're also in the minimal symbol table.  */
		      case 'F':
			if (! pst)
			  {
			    int name_len = p - namestring;
			    char *name = xmalloc (name_len + 1);
			    memcpy (name, namestring, name_len);
			    name[name_len] = '\0';
			    function_outside_compilation_unit_complaint (name);
			    xfree (name);
			  }
			sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
			add_psymbol_to_list (namestring, p - namestring,
					     VAR_DOMAIN, LOC_BLOCK,
					     &objfile->global_psymbols,
					     0, sh.value,
					     psymtab_language, objfile);
			continue;

			/* Two things show up here (hopefully); static symbols of
			   local scope (static used inside braces) or extensions
			   of structure symbols.  We can ignore both.  */
		      case 'V':
		      case '(':
		      case '0':
		      case '1':
		      case '2':
		      case '3':
		      case '4':
		      case '5':
		      case '6':
		      case '7':
		      case '8':
		      case '9':
		      case '-':
		      case '#':		/* for symbol identification (used in live ranges) */
			continue;

		      case ':':
			/* It is a C++ nested symbol.  We don't need to record it
			   (I don't think); if we try to look up foo::bar::baz,
			   then symbols for the symtab containing foo should get
			   read in, I think.  */
			/* Someone says sun cc puts out symbols like
			   /foo/baz/maclib::/usr/local/bin/maclib,
			   which would get here with a symbol type of ':'.  */
			continue;

		      default:
			/* Unexpected symbol descriptor.  The second and subsequent stabs
			   of a continued stab can show up here.  The question is
			   whether they ever can mimic a normal stab--it would be
			   nice if not, since we certainly don't want to spend the
			   time searching to the end of every string looking for
			   a backslash.  */

			complaint (&symfile_complaints,
				   "unknown symbol descriptor `%c'", p[1]);

			/* Ignore it; perhaps it is an extension that we don't
			   know about.  */
			continue;
		      }

		  case N_EXCL:
		    continue;

		  case N_ENDM:
#ifdef SOFUN_ADDRESS_MAYBE_MISSING
		    /* Solaris 2 end of module, finish current partial
		       symbol table.  END_PSYMTAB will set
		       pst->texthigh to the proper value, which is
		       necessary if a module compiled without
		       debugging info follows this module.  */
		    if (pst)
		      {
			pst = (struct partial_symtab *) 0;
			includes_used = 0;
			dependencies_used = 0;
		      }
#endif
		    continue;

		  case N_RBRAC:
		    if (sh.value > save_pst->texthigh)
		      save_pst->texthigh = sh.value;
		    continue;
		  case N_EINCL:
		  case N_DSLINE:
		  case N_BSLINE:
		  case N_SSYM:			/* Claim: Structure or union element.
						   Hopefully, I can ignore this.  */
		  case N_ENTRY:		/* Alternate entry point; can ignore. */
		  case N_MAIN:			/* Can definitely ignore this.   */
		  case N_CATCH:		/* These are GNU C++ extensions */
		  case N_EHDECL:		/* that can safely be ignored here. */
		  case N_LENG:
		  case N_BCOMM:
		  case N_ECOMM:
		  case N_ECOML:
		  case N_FNAME:
		  case N_SLINE:
		  case N_RSYM:
		  case N_PSYM:
		  case N_LBRAC:
		  case N_NSYMS:		/* Ultrix 4.0: symbol count */
		  case N_DEFD:			/* GNU Modula-2 */
		  case N_ALIAS:		/* SunPro F77: alias name, ignore for now.  */

		  case N_OBJ:			/* useless types from Solaris */
		  case N_OPT:
		    /* These symbols aren't interesting; don't worry about them */

		    continue;

		  default:
		    /* If we haven't found it yet, ignore it.  It's probably some
		       new type we don't know about yet.  */
		    complaint (&symfile_complaints, "unknown symbol type %s",
			       local_hex_string (type_code)); /*CUR_SYMBOL_TYPE*/
		    continue;
		  }
		if (stabstring
		    && stabstring != debug_info->ss + fh->issBase + sh.iss)
		  xfree (stabstring);
	      }
	      /* end - Handle continuation */
	    }
	}
      else
	{
	  for (cur_sdx = 0; cur_sdx < fh->csym;)
	    {
	      char *name;
	      enum address_class class;

	      (*swap_sym_in) (cur_bfd,
			      ((char *) debug_info->external_sym
			       + ((fh->isymBase + cur_sdx)
				  * external_sym_size)),
			      &sh);

	      if (ECOFF_IS_STAB (&sh))
		{
		  cur_sdx++;
		  continue;
		}

	      /* Non absolute static symbols go into the minimal table.  */
	      if (SC_IS_UNDEF (sh.sc) || sh.sc == scNil
		  || (sh.index == indexNil
		      && (sh.st != stStatic || sh.sc == scAbs)))
		{
		  /* FIXME, premature? */
		  cur_sdx++;
		  continue;
		}

	      name = debug_info->ss + fh->issBase + sh.iss;

	      switch (sh.sc)
		{
		case scText:
		case scRConst:
		  /* The value of a stEnd symbol is the displacement from the
		     corresponding start symbol value, do not relocate it.  */
		  if (sh.st != stEnd)
		    sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
		  break;
		case scData:
		case scSData:
		case scRData:
		case scPData:
		case scXData:
		  sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
		  break;
		case scBss:
		case scSBss:
		  sh.value += ANOFFSET (objfile->section_offsets, SECT_OFF_BSS (objfile));
		  break;
		}

	      switch (sh.st)
		{
		  CORE_ADDR high;
		  CORE_ADDR procaddr;
		  int new_sdx;

		case stStaticProc:
		  prim_record_minimal_symbol_and_info (name, sh.value,
						       mst_file_text, NULL,
						       SECT_OFF_TEXT (objfile), NULL,
						       objfile);

		  /* FALLTHROUGH */

		case stProc:
		  /* Ignore all parameter symbol records.  */
		  if (sh.index >= hdr->iauxMax)
		    {
		      /* Should not happen, but does when cross-compiling
		         with the MIPS compiler.  FIXME -- pull later.  */
		      index_complaint (name);
		      new_sdx = cur_sdx + 1;	/* Don't skip at all */
		    }
		  else
		    new_sdx = AUX_GET_ISYM (fh->fBigendian,
					    (debug_info->external_aux
					     + fh->iauxBase
					     + sh.index));

		  if (new_sdx <= cur_sdx)
		    {
		      /* This should not happen either... FIXME.  */
		      complaint (&symfile_complaints,
				 "bad proc end in aux found from symbol %s",
				 name);
		      new_sdx = cur_sdx + 1;	/* Don't skip backward */
		    }

                  /* For stProc symbol records, we need to check the
                     storage class as well, as only (stProc, scText)
                     entries represent "real" procedures - See the
                     Compaq document titled "Object File / Symbol Table
                     Format Specification" for more information.  If the
                     storage class is not scText, we discard the whole
                     block of symbol records for this stProc.  */
                  if (sh.st == stProc && sh.sc != scText)
                    goto skip;

		  /* Usually there is a local and a global stProc symbol
		     for a function. This means that the function name
		     has already been entered into the mimimal symbol table
		     while processing the global symbols in pass 2 above.
		     One notable exception is the PROGRAM name from
		     f77 compiled executables, it is only put out as
		     local stProc symbol, and a global MAIN__ stProc symbol
		     points to it.  It doesn't matter though, as gdb is
		     still able to find the PROGRAM name via the partial
		     symbol table, and the MAIN__ symbol via the minimal
		     symbol table.  */
		  if (sh.st == stProc)
		    add_psymbol_to_list (name, strlen (name),
					 VAR_DOMAIN, LOC_BLOCK,
					 &objfile->global_psymbols,
				    0, sh.value, psymtab_language, objfile);
		  else
		    add_psymbol_to_list (name, strlen (name),
					 VAR_DOMAIN, LOC_BLOCK,
					 &objfile->static_psymbols,
				    0, sh.value, psymtab_language, objfile);

		  procaddr = sh.value;

		  cur_sdx = new_sdx;
		  (*swap_sym_in) (cur_bfd,
				  ((char *) debug_info->external_sym
				   + ((fh->isymBase + cur_sdx - 1)
				      * external_sym_size)),
				  &sh);
		  if (sh.st != stEnd)
		    continue;

		  /* Kludge for Irix 5.2 zero fh->adr.  */
		  if (!relocatable
		      && (pst->textlow == 0 || procaddr < pst->textlow))
		    pst->textlow = procaddr;

		  high = procaddr + sh.value;
		  if (high > pst->texthigh)
		    pst->texthigh = high;
		  continue;

		case stStatic:	/* Variable */
		  if (SC_IS_DATA (sh.sc))
		    prim_record_minimal_symbol_and_info (name, sh.value,
							 mst_file_data, NULL,
							 SECT_OFF_DATA (objfile),
							 NULL,
							 objfile);
		  else
		    prim_record_minimal_symbol_and_info (name, sh.value,
							 mst_file_bss, NULL,
							 SECT_OFF_BSS (objfile),
							 NULL,
							 objfile);
		  class = LOC_STATIC;
		  break;

		case stIndirect:	/* Irix5 forward declaration */
		  /* Skip forward declarations from Irix5 cc */
		  goto skip;

		case stTypedef:	/* Typedef */
		  /* Skip typedefs for forward declarations and opaque
		     structs from alpha and mips cc.  */
		  if (sh.iss == 0 || has_opaque_xref (fh, &sh))
		    goto skip;
		  class = LOC_TYPEDEF;
		  break;

		case stConstant:	/* Constant decl */
		  class = LOC_CONST;
		  break;

		case stUnion:
		case stStruct:
		case stEnum:
		case stBlock:	/* { }, str, un, enum */
		  /* Do not create a partial symbol for cc unnamed aggregates
		     and gcc empty aggregates. */
		  if ((sh.sc == scInfo
		       || SC_IS_COMMON (sh.sc))
		      && sh.iss != 0
		      && sh.index != cur_sdx + 2)
		    {
		      add_psymbol_to_list (name, strlen (name),
					   STRUCT_DOMAIN, LOC_TYPEDEF,
					   &objfile->static_psymbols,
					   0, (CORE_ADDR) 0,
					   psymtab_language, objfile);
		    }
		  handle_psymbol_enumerators (objfile, fh, sh.st, sh.value);

		  /* Skip over the block */
		  new_sdx = sh.index;
		  if (new_sdx <= cur_sdx)
		    {
		      /* This happens with the Ultrix kernel. */
		      complaint (&symfile_complaints,
				 "bad aux index at block symbol %s", name);
		      new_sdx = cur_sdx + 1;	/* Don't skip backward */
		    }
		  cur_sdx = new_sdx;
		  continue;

		case stFile:	/* File headers */
		case stLabel:	/* Labels */
		case stEnd:	/* Ends of files */
		  goto skip;

		case stLocal:	/* Local variables */
		  /* Normally these are skipped because we skip over
		     all blocks we see.  However, these can occur
		     as visible symbols in a .h file that contains code. */
		  goto skip;

		default:
		  /* Both complaints are valid:  one gives symbol name,
		     the other the offending symbol type.  */
		  complaint (&symfile_complaints, "unknown local symbol %s",
			     name);
		  complaint (&symfile_complaints, "with type %d", sh.st);
		  cur_sdx++;
		  continue;
		}
	      /* Use this gdb symbol */
	      add_psymbol_to_list (name, strlen (name),
				   VAR_DOMAIN, class,
				   &objfile->static_psymbols,
				   0, sh.value, psymtab_language, objfile);
	    skip:
	      cur_sdx++;	/* Go to next file symbol */
	    }

	  /* Now do enter the external symbols. */
	  ext_ptr = &extern_tab[fdr_to_pst[f_idx].globals_offset];
	  cur_sdx = fdr_to_pst[f_idx].n_globals;
	  PST_PRIVATE (save_pst)->extern_count = cur_sdx;
	  PST_PRIVATE (save_pst)->extern_tab = ext_ptr;
	  for (; --cur_sdx >= 0; ext_ptr++)
	    {
	      enum address_class class;
	      SYMR *psh;
	      char *name;
	      CORE_ADDR svalue;

	      if (ext_ptr->ifd != f_idx)
		internal_error (__FILE__, __LINE__, "failed internal consistency check");
	      psh = &ext_ptr->asym;

	      /* Do not add undefined symbols to the partial symbol table.  */
	      if (SC_IS_UNDEF (psh->sc) || psh->sc == scNil)
		continue;

	      svalue = psh->value;
	      switch (psh->sc)
		{
		case scText:
		case scRConst:
		  svalue += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
		  break;
		case scData:
		case scSData:
		case scRData:
		case scPData:
		case scXData:
		  svalue += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
		  break;
		case scBss:
		case scSBss:
		  svalue += ANOFFSET (objfile->section_offsets, SECT_OFF_BSS (objfile));
		  break;
		}

	      switch (psh->st)
		{
		case stNil:
		  /* These are generated for static symbols in .o files,
		     ignore them.  */
		  continue;
		case stProc:
		case stStaticProc:
		  /* External procedure symbols have been entered
		     into the minimal symbol table in pass 2 above.
		     Ignore them, as parse_external will ignore them too.  */
		  continue;
		case stLabel:
		  class = LOC_LABEL;
		  break;
		default:
		  unknown_ext_complaint (debug_info->ssext + psh->iss);
		  /* Fall through, pretend it's global.  */
		case stGlobal:
		  /* Global common symbols are resolved by the runtime loader,
		     ignore them.  */
		  if (SC_IS_COMMON (psh->sc))
		    continue;

		  class = LOC_STATIC;
		  break;
		}
	      name = debug_info->ssext + psh->iss;
	      add_psymbol_to_list (name, strlen (name),
				   VAR_DOMAIN, class,
				   &objfile->global_psymbols,
				   0, svalue,
				   psymtab_language, objfile);
	    }
	}

      /* Link pst to FDR. end_psymtab returns NULL if the psymtab was
         empty and put on the free list.  */
      fdr_to_pst[f_idx].pst = end_psymtab (save_pst,
					psymtab_include_list, includes_used,
					   -1, save_pst->texthigh,
		       dependency_list, dependencies_used, textlow_not_set);
      includes_used = 0;
      dependencies_used = 0;

      if (objfile->ei.entry_point >= save_pst->textlow &&
	  objfile->ei.entry_point < save_pst->texthigh)
	{
	  objfile->ei.deprecated_entry_file_lowpc = save_pst->textlow;
	  objfile->ei.deprecated_entry_file_highpc = save_pst->texthigh;
	}

      /* The objfile has its functions reordered if this partial symbol
         table overlaps any other partial symbol table.
         We cannot assume a reordered objfile if a partial symbol table
         is contained within another partial symbol table, as partial symbol
         tables for include files with executable code are contained
         within the partial symbol table for the including source file,
         and we do not want to flag the objfile reordered for these cases.

         This strategy works well for Irix-5.2 shared libraries, but we
         might have to use a more elaborate (and slower) algorithm for
         other cases.  */
      save_pst = fdr_to_pst[f_idx].pst;
      if (save_pst != NULL
	  && save_pst->textlow != 0
	  && !(objfile->flags & OBJF_REORDERED))
	{
	  ALL_OBJFILE_PSYMTABS (objfile, pst)
	  {
	    if (save_pst != pst
		&& save_pst->textlow >= pst->textlow
		&& save_pst->textlow < pst->texthigh
		&& save_pst->texthigh > pst->texthigh)
	      {
		objfile->flags |= OBJF_REORDERED;
		break;
	      }
	  }
	}
    }

  /* Now scan the FDRs for dependencies */
  for (f_idx = 0; f_idx < hdr->ifdMax; f_idx++)
    {
      fh = f_idx + debug_info->fdr;
      pst = fdr_to_pst[f_idx].pst;

      if (pst == (struct partial_symtab *) NULL)
	continue;

      /* This should catch stabs-in-ecoff. */
      if (fh->crfd <= 1)
	continue;

      /* Skip the first file indirect entry as it is a self dependency
         for source files or a reverse .h -> .c dependency for header files.  */
      pst->number_of_dependencies = 0;
      pst->dependencies =
	((struct partial_symtab **)
	 obstack_alloc (&objfile->objfile_obstack,
			((fh->crfd - 1)
			 * sizeof (struct partial_symtab *))));
      for (s_idx = 1; s_idx < fh->crfd; s_idx++)
	{
	  RFDT rh;

	  (*swap_rfd_in) (cur_bfd,
			  ((char *) debug_info->external_rfd
			   + (fh->rfdBase + s_idx) * external_rfd_size),
			  &rh);
	  if (rh < 0 || rh >= hdr->ifdMax)
	    {
	      complaint (&symfile_complaints, "bad file number %ld", rh);
	      continue;
	    }

	  /* Skip self dependencies of header files.  */
	  if (rh == f_idx)
	    continue;

	  /* Do not add to dependeny list if psymtab was empty.  */
	  if (fdr_to_pst[rh].pst == (struct partial_symtab *) NULL)
	    continue;
	  pst->dependencies[pst->number_of_dependencies++] = fdr_to_pst[rh].pst;
	}
    }

  /* Remove the dummy psymtab created for -O3 images above, if it is
     still empty, to enable the detection of stripped executables.  */
  if (objfile->psymtabs->next == NULL
      && objfile->psymtabs->number_of_dependencies == 0
      && objfile->psymtabs->n_global_syms == 0
      && objfile->psymtabs->n_static_syms == 0)
    objfile->psymtabs = NULL;
  do_cleanups (old_chain);
}

/* If the current psymbol has an enumerated type, we need to add
   all the the enum constants to the partial symbol table.  */

static void
handle_psymbol_enumerators (struct objfile *objfile, FDR *fh, int stype,
			    CORE_ADDR svalue)
{
  const bfd_size_type external_sym_size = debug_swap->external_sym_size;
  void (*const swap_sym_in) (bfd *, void *, SYMR *) = debug_swap->swap_sym_in;
  char *ext_sym = ((char *) debug_info->external_sym
		   + ((fh->isymBase + cur_sdx + 1) * external_sym_size));
  SYMR sh;
  TIR tir;

  switch (stype)
    {
    case stEnum:
      break;

    case stBlock:
      /* It is an enumerated type if the next symbol entry is a stMember
         and its auxiliary index is indexNil or its auxiliary entry
         is a plain btNil or btVoid.
         Alpha cc -migrate enums are recognized by a zero index and
         a zero symbol value.
         DU 4.0 cc enums are recognized by a member type of btEnum without
         qualifiers and a zero symbol value.  */
      (*swap_sym_in) (cur_bfd, ext_sym, &sh);
      if (sh.st != stMember)
	return;

      if (sh.index == indexNil
	  || (sh.index == 0 && svalue == 0))
	break;
      (*debug_swap->swap_tir_in) (fh->fBigendian,
				  &(debug_info->external_aux
				    + fh->iauxBase + sh.index)->a_ti,
				  &tir);
      if ((tir.bt != btNil
	   && tir.bt != btVoid
	   && (tir.bt != btEnum || svalue != 0))
	  || tir.tq0 != tqNil)
	return;
      break;

    default:
      return;
    }

  for (;;)
    {
      char *name;

      (*swap_sym_in) (cur_bfd, ext_sym, &sh);
      if (sh.st != stMember)
	break;
      name = debug_info->ss + cur_fdr->issBase + sh.iss;

      /* Note that the value doesn't matter for enum constants
         in psymtabs, just in symtabs.  */
      add_psymbol_to_list (name, strlen (name),
			   VAR_DOMAIN, LOC_CONST,
			   &objfile->static_psymbols, 0,
			   (CORE_ADDR) 0, psymtab_language, objfile);
      ext_sym += external_sym_size;
    }
}

/* Get the next symbol.  OBJFILE is unused. */

static char *
mdebug_next_symbol_text (struct objfile *objfile)
{
  SYMR sh;

  cur_sdx++;
  (*debug_swap->swap_sym_in) (cur_bfd,
			      ((char *) debug_info->external_sym
			       + ((cur_fdr->isymBase + cur_sdx)
				  * debug_swap->external_sym_size)),
			      &sh);
  return debug_info->ss + cur_fdr->issBase + sh.iss;
}

/* Ancillary function to psymtab_to_symtab().  Does all the work
   for turning the partial symtab PST into a symtab, recurring
   first on all dependent psymtabs.  The argument FILENAME is
   only passed so we can see in debug stack traces what file
   is being read.

   This function has a split personality, based on whether the
   symbol table contains ordinary ecoff symbols, or stabs-in-ecoff.
   The flow of control and even the memory allocation differs.  FIXME.  */

static void
psymtab_to_symtab_1 (struct partial_symtab *pst, char *filename)
{
  bfd_size_type external_sym_size;
  bfd_size_type external_pdr_size;
  void (*swap_sym_in) (bfd *, void *, SYMR *);
  void (*swap_pdr_in) (bfd *, void *, PDR *);
  int i;
  struct symtab *st = NULL;
  FDR *fh;
  struct linetable *lines;
  CORE_ADDR lowest_pdr_addr = 0;
  int last_symtab_ended = 0;

  if (pst->readin)
    return;
  pst->readin = 1;

  /* Read in all partial symbtabs on which this one is dependent.
     NOTE that we do have circular dependencies, sigh.  We solved
     that by setting pst->readin before this point.  */

  for (i = 0; i < pst->number_of_dependencies; i++)
    if (!pst->dependencies[i]->readin)
      {
	/* Inform about additional files to be read in.  */
	if (info_verbose)
	  {
	    fputs_filtered (" ", gdb_stdout);
	    wrap_here ("");
	    fputs_filtered ("and ", gdb_stdout);
	    wrap_here ("");
	    printf_filtered ("%s...",
			     pst->dependencies[i]->filename);
	    wrap_here ("");	/* Flush output */
	    gdb_flush (gdb_stdout);
	  }
	/* We only pass the filename for debug purposes */
	psymtab_to_symtab_1 (pst->dependencies[i],
			     pst->dependencies[i]->filename);
      }

  /* Do nothing if this is a dummy psymtab.  */

  if (pst->n_global_syms == 0 && pst->n_static_syms == 0
      && pst->textlow == 0 && pst->texthigh == 0)
    return;

  /* Now read the symbols for this symtab */

  cur_bfd = CUR_BFD (pst);
  debug_swap = DEBUG_SWAP (pst);
  debug_info = DEBUG_INFO (pst);
  pending_list = PENDING_LIST (pst);
  external_sym_size = debug_swap->external_sym_size;
  external_pdr_size = debug_swap->external_pdr_size;
  swap_sym_in = debug_swap->swap_sym_in;
  swap_pdr_in = debug_swap->swap_pdr_in;
  current_objfile = pst->objfile;
  cur_fd = FDR_IDX (pst);
  fh = ((cur_fd == -1)
	? (FDR *) NULL
	: debug_info->fdr + cur_fd);
  cur_fdr = fh;

  /* See comment in parse_partial_symbols about the @stabs sentinel. */
  processing_gcc_compilation = 0;
  if (fh != (FDR *) NULL && fh->csym >= 2)
    {
      SYMR sh;

      (*swap_sym_in) (cur_bfd,
		      ((char *) debug_info->external_sym
		       + (fh->isymBase + 1) * external_sym_size),
		      &sh);
      if (strcmp (debug_info->ss + fh->issBase + sh.iss,
		  stabs_symbol) == 0)
	{
	  /* We indicate that this is a GCC compilation so that certain
	     features will be enabled in stabsread/dbxread.  */
	  processing_gcc_compilation = 2;
	}
    }

  if (processing_gcc_compilation != 0)
    {

      /* This symbol table contains stabs-in-ecoff entries.  */

      /* Parse local symbols first */

      if (fh->csym <= 2)	/* FIXME, this blows psymtab->symtab ptr */
	{
	  current_objfile = NULL;
	  return;
	}
      for (cur_sdx = 2; cur_sdx < fh->csym; cur_sdx++)
	{
	  SYMR sh;
	  char *name;
	  CORE_ADDR valu;

	  (*swap_sym_in) (cur_bfd,
			  (((char *) debug_info->external_sym)
			   + (fh->isymBase + cur_sdx) * external_sym_size),
			  &sh);
	  name = debug_info->ss + fh->issBase + sh.iss;
	  valu = sh.value;
	  /* XXX This is a hack.  It will go away!  */
	  if (ECOFF_IS_STAB (&sh) || (name[0] == '#'))
	    {
	      int type_code = ECOFF_UNMARK_STAB (sh.index);

	      /* We should never get non N_STAB symbols here, but they
	         should be harmless, so keep process_one_symbol from
	         complaining about them.  */
	      if (type_code & N_STAB)
		{
		  /* If we found a trailing N_SO with no name, process
                     it here instead of in process_one_symbol, so we
                     can keep a handle to its symtab.  The symtab
                     would otherwise be ended twice, once in
                     process_one_symbol, and once after this loop. */
		  if (type_code == N_SO
		      && last_source_file
		      && previous_stab_code != (unsigned char) N_SO
		      && *name == '\000')
		    {
		      valu += ANOFFSET (pst->section_offsets,
					SECT_OFF_TEXT (pst->objfile));
		      previous_stab_code = N_SO;
		      st = end_symtab (valu, pst->objfile,
				       SECT_OFF_TEXT (pst->objfile));
		      end_stabs ();
		      last_symtab_ended = 1;
		    }
		  else
		    {
		      last_symtab_ended = 0;
		      process_one_symbol (type_code, 0, valu, name,
					  pst->section_offsets, pst->objfile);
		    }
		}
	      /* Similarly a hack.  */
	      else if (name[0] == '#')
		{
		  process_one_symbol (N_SLINE, 0, valu, name,
				      pst->section_offsets, pst->objfile);
		}
	      if (type_code == N_FUN)
		{
		  /* Make up special symbol to contain
		     procedure specific info */
		  struct mips_extra_func_info *e =
		  ((struct mips_extra_func_info *)
		   obstack_alloc (&current_objfile->objfile_obstack,
				  sizeof (struct mips_extra_func_info)));
		  struct symbol *s = new_symbol (MIPS_EFI_SYMBOL_NAME);

		  memset (e, 0, sizeof (struct mips_extra_func_info));
		  SYMBOL_DOMAIN (s) = LABEL_DOMAIN;
		  SYMBOL_CLASS (s) = LOC_CONST;
		  SYMBOL_TYPE (s) = mdebug_type_void;
		  SYMBOL_VALUE (s) = (long) e;
		  e->pdr.framereg = -1;
		  add_symbol_to_list (s, &local_symbols);
		}
	    }
	  else if (sh.st == stLabel)
	    {
	      if (sh.index == indexNil)
		{
		  /* This is what the gcc2_compiled and __gnu_compiled_*
		     show up as.  So don't complain.  */
		  ;
		}
	      else
		{
		  /* Handle encoded stab line number. */
		  valu += ANOFFSET (pst->section_offsets, SECT_OFF_TEXT (pst->objfile));
		  record_line (current_subfile, sh.index, valu);
		}
	    }
	  else if (sh.st == stProc || sh.st == stStaticProc
		   || sh.st == stStatic || sh.st == stEnd)
	    /* These are generated by gcc-2.x, do not complain */
	    ;
	  else
	    complaint (&symfile_complaints, "unknown stabs symbol %s", name);
	}

      if (! last_symtab_ended)
	{
	  st = end_symtab (pst->texthigh, pst->objfile, SECT_OFF_TEXT (pst->objfile));
	  end_stabs ();
	}

      /* There used to be a call to sort_blocks here, but this should not
         be necessary for stabs symtabs.  And as sort_blocks modifies the
         start address of the GLOBAL_BLOCK to the FIRST_LOCAL_BLOCK,
         it did the wrong thing if the first procedure in a file was
         generated via asm statements.  */

      /* Fill in procedure info next.  */
      if (fh->cpd > 0)
	{
	  PDR *pr_block;
	  struct cleanup *old_chain;
	  char *pdr_ptr;
	  char *pdr_end;
	  PDR *pdr_in;
	  PDR *pdr_in_end;

	  pr_block = (PDR *) xmalloc (fh->cpd * sizeof (PDR));
	  old_chain = make_cleanup (xfree, pr_block);

	  pdr_ptr = ((char *) debug_info->external_pdr
		     + fh->ipdFirst * external_pdr_size);
	  pdr_end = pdr_ptr + fh->cpd * external_pdr_size;
	  pdr_in = pr_block;
	  for (;
	       pdr_ptr < pdr_end;
	       pdr_ptr += external_pdr_size, pdr_in++)
	    {
	      (*swap_pdr_in) (cur_bfd, pdr_ptr, pdr_in);

	      /* Determine lowest PDR address, the PDRs are not always
	         sorted.  */
	      if (pdr_in == pr_block)
		lowest_pdr_addr = pdr_in->adr;
	      else if (pdr_in->adr < lowest_pdr_addr)
		lowest_pdr_addr = pdr_in->adr;
	    }

	  pdr_in = pr_block;
	  pdr_in_end = pdr_in + fh->cpd;
	  for (; pdr_in < pdr_in_end; pdr_in++)
	    parse_procedure (pdr_in, st, pst);

	  do_cleanups (old_chain);
	}
    }
  else
    {
      /* This symbol table contains ordinary ecoff entries.  */

      int f_max;
      int maxlines;
      EXTR *ext_ptr;

      if (fh == 0)
	{
	  maxlines = 0;
	  st = new_symtab ("unknown", 0, pst->objfile);
	}
      else
	{
	  maxlines = 2 * fh->cline;
	  st = new_symtab (pst->filename, maxlines, pst->objfile);

	  /* The proper language was already determined when building
	     the psymtab, use it.  */
	  st->language = PST_PRIVATE (pst)->pst_language;
	}

      psymtab_language = st->language;

      lines = LINETABLE (st);

      /* Get a new lexical context */

      push_parse_stack ();
      top_stack->cur_st = st;
      top_stack->cur_block = BLOCKVECTOR_BLOCK (BLOCKVECTOR (st),
						STATIC_BLOCK);
      BLOCK_START (top_stack->cur_block) = pst->textlow;
      BLOCK_END (top_stack->cur_block) = 0;
      top_stack->blocktype = stFile;
      top_stack->cur_type = 0;
      top_stack->procadr = 0;
      top_stack->numargs = 0;
      found_ecoff_debugging_info = 0;

      if (fh)
	{
	  char *sym_ptr;
	  char *sym_end;

	  /* Parse local symbols first */
	  sym_ptr = ((char *) debug_info->external_sym
		     + fh->isymBase * external_sym_size);
	  sym_end = sym_ptr + fh->csym * external_sym_size;
	  while (sym_ptr < sym_end)
	    {
	      SYMR sh;
	      int c;

	      (*swap_sym_in) (cur_bfd, sym_ptr, &sh);
	      c = parse_symbol (&sh,
				debug_info->external_aux + fh->iauxBase,
				sym_ptr, fh->fBigendian, pst->section_offsets, pst->objfile);
	      sym_ptr += c * external_sym_size;
	    }

	  /* Linenumbers.  At the end, check if we can save memory.
	     parse_lines has to look ahead an arbitrary number of PDR
	     structures, so we swap them all first.  */
	  if (fh->cpd > 0)
	    {
	      PDR *pr_block;
	      struct cleanup *old_chain;
	      char *pdr_ptr;
	      char *pdr_end;
	      PDR *pdr_in;
	      PDR *pdr_in_end;

	      pr_block = (PDR *) xmalloc (fh->cpd * sizeof (PDR));

	      old_chain = make_cleanup (xfree, pr_block);

	      pdr_ptr = ((char *) debug_info->external_pdr
			 + fh->ipdFirst * external_pdr_size);
	      pdr_end = pdr_ptr + fh->cpd * external_pdr_size;
	      pdr_in = pr_block;
	      for (;
		   pdr_ptr < pdr_end;
		   pdr_ptr += external_pdr_size, pdr_in++)
		{
		  (*swap_pdr_in) (cur_bfd, pdr_ptr, pdr_in);

		  /* Determine lowest PDR address, the PDRs are not always
		     sorted.  */
		  if (pdr_in == pr_block)
		    lowest_pdr_addr = pdr_in->adr;
		  else if (pdr_in->adr < lowest_pdr_addr)
		    lowest_pdr_addr = pdr_in->adr;
		}

	      parse_lines (fh, pr_block, lines, maxlines, pst, lowest_pdr_addr);
	      if (lines->nitems < fh->cline)
		lines = shrink_linetable (lines);

	      /* Fill in procedure info next.  */
	      pdr_in = pr_block;
	      pdr_in_end = pdr_in + fh->cpd;
	      for (; pdr_in < pdr_in_end; pdr_in++)
		parse_procedure (pdr_in, 0, pst);

	      do_cleanups (old_chain);
	    }
	}

      LINETABLE (st) = lines;

      /* .. and our share of externals.
         XXX use the global list to speed up things here. how?
         FIXME, Maybe quit once we have found the right number of ext's? */
      top_stack->cur_st = st;
      top_stack->cur_block = BLOCKVECTOR_BLOCK (BLOCKVECTOR (top_stack->cur_st),
						GLOBAL_BLOCK);
      top_stack->blocktype = stFile;

      ext_ptr = PST_PRIVATE (pst)->extern_tab;
      for (i = PST_PRIVATE (pst)->extern_count; --i >= 0; ext_ptr++)
	parse_external (ext_ptr, fh->fBigendian, pst->section_offsets, pst->objfile);

      /* If there are undefined symbols, tell the user.
         The alpha has an undefined symbol for every symbol that is
         from a shared library, so tell the user only if verbose is on.  */
      if (info_verbose && n_undef_symbols)
	{
	  printf_filtered ("File %s contains %d unresolved references:",
			   st->filename, n_undef_symbols);
	  printf_filtered ("\n\t%4d variables\n\t%4d procedures\n\t%4d labels\n",
			   n_undef_vars, n_undef_procs, n_undef_labels);
	  n_undef_symbols = n_undef_labels = n_undef_vars = n_undef_procs = 0;

	}
      pop_parse_stack ();

      st->primary = 1;

      sort_blocks (st);
    }

  /* Now link the psymtab and the symtab.  */
  pst->symtab = st;

  current_objfile = NULL;
}

/* Ancillary parsing procedures. */

/* Return 1 if the symbol pointed to by SH has a cross reference
   to an opaque aggregate type, else 0.  */

static int
has_opaque_xref (FDR *fh, SYMR *sh)
{
  TIR tir;
  union aux_ext *ax;
  RNDXR rn[1];
  unsigned int rf;

  if (sh->index == indexNil)
    return 0;

  ax = debug_info->external_aux + fh->iauxBase + sh->index;
  (*debug_swap->swap_tir_in) (fh->fBigendian, &ax->a_ti, &tir);
  if (tir.bt != btStruct && tir.bt != btUnion && tir.bt != btEnum)
    return 0;

  ax++;
  (*debug_swap->swap_rndx_in) (fh->fBigendian, &ax->a_rndx, rn);
  if (rn->rfd == 0xfff)
    rf = AUX_GET_ISYM (fh->fBigendian, ax + 1);
  else
    rf = rn->rfd;
  if (rf != -1)
    return 0;
  return 1;
}

/* Lookup the type at relative index RN.  Return it in TPP
   if found and in any event come up with its name PNAME.
   BIGEND says whether aux symbols are big-endian or not (from fh->fBigendian).
   Return value says how many aux symbols we ate. */

static int
cross_ref (int fd, union aux_ext *ax, struct type **tpp, enum type_code type_code,	/* Use to alloc new type if none is found. */
	   char **pname, int bigend, char *sym_name)
{
  RNDXR rn[1];
  unsigned int rf;
  int result = 1;
  FDR *fh;
  char *esh;
  SYMR sh;
  int xref_fd;
  struct mdebug_pending *pend;

  *tpp = (struct type *) NULL;

  (*debug_swap->swap_rndx_in) (bigend, &ax->a_rndx, rn);

  /* Escape index means 'the next one' */
  if (rn->rfd == 0xfff)
    {
      result++;
      rf = AUX_GET_ISYM (bigend, ax + 1);
    }
  else
    {
      rf = rn->rfd;
    }

  /* mips cc uses a rf of -1 for opaque struct definitions.
     Set TYPE_FLAG_STUB for these types so that check_typedef will
     resolve them if the struct gets defined in another compilation unit.  */
  if (rf == -1)
    {
      *pname = "<undefined>";
      *tpp = init_type (type_code, 0, TYPE_FLAG_STUB, (char *) NULL, current_objfile);
      return result;
    }

  /* mips cc uses an escaped rn->index of 0 for struct return types
     of procedures that were compiled without -g. These will always remain
     undefined.  */
  if (rn->rfd == 0xfff && rn->index == 0)
    {
      *pname = "<undefined>";
      return result;
    }

  /* Find the relative file descriptor and the symbol in it.  */
  fh = get_rfd (fd, rf);
  xref_fd = fh - debug_info->fdr;

  if (rn->index >= fh->csym)
    {
      /* File indirect entry is corrupt.  */
      *pname = "<illegal>";
      bad_rfd_entry_complaint (sym_name, xref_fd, rn->index);
      return result;
    }

  /* If we have processed this symbol then we left a forwarding
     pointer to the type in the pending list.  If not, we`ll put
     it in a list of pending types, to be processed later when
     the file will be.  In any event, we collect the name for the
     type here.  */

  esh = ((char *) debug_info->external_sym
	 + ((fh->isymBase + rn->index)
	    * debug_swap->external_sym_size));
  (*debug_swap->swap_sym_in) (cur_bfd, esh, &sh);

  /* Make sure that this type of cross reference can be handled.  */
  if ((sh.sc != scInfo
       || (sh.st != stBlock && sh.st != stTypedef && sh.st != stIndirect
	   && sh.st != stStruct && sh.st != stUnion
	   && sh.st != stEnum))
      && (sh.st != stBlock || !SC_IS_COMMON (sh.sc)))
    {
      /* File indirect entry is corrupt.  */
      *pname = "<illegal>";
      bad_rfd_entry_complaint (sym_name, xref_fd, rn->index);
      return result;
    }

  *pname = debug_info->ss + fh->issBase + sh.iss;

  pend = is_pending_symbol (fh, esh);
  if (pend)
    *tpp = pend->t;
  else
    {
      /* We have not yet seen this type.  */

      if ((sh.iss == 0 && sh.st == stTypedef) || sh.st == stIndirect)
	{
	  TIR tir;

	  /* alpha cc puts out a stTypedef with a sh.iss of zero for
	     two cases:
	     a) forward declarations of structs/unions/enums which are not
	     defined in this compilation unit.
	     For these the type will be void. This is a bad design decision
	     as cross referencing across compilation units is impossible
	     due to the missing name.
	     b) forward declarations of structs/unions/enums/typedefs which
	     are defined later in this file or in another file in the same
	     compilation unit. Irix5 cc uses a stIndirect symbol for this.
	     Simply cross reference those again to get the true type.
	     The forward references are not entered in the pending list and
	     in the symbol table.  */

	  (*debug_swap->swap_tir_in) (bigend,
				      &(debug_info->external_aux
					+ fh->iauxBase + sh.index)->a_ti,
				      &tir);
	  if (tir.tq0 != tqNil)
	    complaint (&symfile_complaints,
		       "illegal tq0 in forward typedef for %s", sym_name);
	  switch (tir.bt)
	    {
	    case btVoid:
	      *tpp = init_type (type_code, 0, 0, (char *) NULL,
				current_objfile);
	      *pname = "<undefined>";
	      break;

	    case btStruct:
	    case btUnion:
	    case btEnum:
	      cross_ref (xref_fd,
			 (debug_info->external_aux
			  + fh->iauxBase + sh.index + 1),
			 tpp, type_code, pname,
			 fh->fBigendian, sym_name);
	      break;

	    case btTypedef:
	      /* Follow a forward typedef. This might recursively
	         call cross_ref till we get a non typedef'ed type.
	         FIXME: This is not correct behaviour, but gdb currently
	         cannot handle typedefs without type copying. Type
	         copying is impossible as we might have mutual forward
	         references between two files and the copied type would not
	         get filled in when we later parse its definition.  */
	      *tpp = parse_type (xref_fd,
				 debug_info->external_aux + fh->iauxBase,
				 sh.index,
				 (int *) NULL,
				 fh->fBigendian,
				 debug_info->ss + fh->issBase + sh.iss);
	      add_pending (fh, esh, *tpp);
	      break;

	    default:
	      complaint (&symfile_complaints,
			 "illegal bt %d in forward typedef for %s", tir.bt,
			 sym_name);
	      *tpp = init_type (type_code, 0, 0, (char *) NULL,
				current_objfile);
	      break;
	    }
	  return result;
	}
      else if (sh.st == stTypedef)
	{
	  /* Parse the type for a normal typedef. This might recursively call
	     cross_ref till we get a non typedef'ed type.
	     FIXME: This is not correct behaviour, but gdb currently
	     cannot handle typedefs without type copying. But type copying is
	     impossible as we might have mutual forward references between
	     two files and the copied type would not get filled in when
	     we later parse its definition.   */
	  *tpp = parse_type (xref_fd,
			     debug_info->external_aux + fh->iauxBase,
			     sh.index,
			     (int *) NULL,
			     fh->fBigendian,
			     debug_info->ss + fh->issBase + sh.iss);
	}
      else
	{
	  /* Cross reference to a struct/union/enum which is defined
	     in another file in the same compilation unit but that file
	     has not been parsed yet.
	     Initialize the type only, it will be filled in when
	     it's definition is parsed.  */
	  *tpp = init_type (type_code, 0, 0, (char *) NULL, current_objfile);
	}
      add_pending (fh, esh, *tpp);
    }

  /* We used one auxent normally, two if we got a "next one" rf. */
  return result;
}


/* Quick&dirty lookup procedure, to avoid the MI ones that require
   keeping the symtab sorted */

static struct symbol *
mylookup_symbol (char *name, struct block *block,
		 domain_enum domain, enum address_class class)
{
  struct dict_iterator iter;
  int inc;
  struct symbol *sym;

  inc = name[0];
  ALL_BLOCK_SYMBOLS (block, iter, sym)
    {
      if (DEPRECATED_SYMBOL_NAME (sym)[0] == inc
	  && SYMBOL_DOMAIN (sym) == domain
	  && SYMBOL_CLASS (sym) == class
	  && strcmp (DEPRECATED_SYMBOL_NAME (sym), name) == 0)
	return sym;
    }

  block = BLOCK_SUPERBLOCK (block);
  if (block)
    return mylookup_symbol (name, block, domain, class);
  return 0;
}


/* Add a new symbol S to a block B.  */

static void
add_symbol (struct symbol *s, struct block *b)
{
  dict_add_symbol (BLOCK_DICT (b), s);
}

/* Add a new block B to a symtab S */

static void
add_block (struct block *b, struct symtab *s)
{
  struct blockvector *bv = BLOCKVECTOR (s);

  bv = (struct blockvector *) xrealloc ((void *) bv,
					(sizeof (struct blockvector)
					 + BLOCKVECTOR_NBLOCKS (bv)
					 * sizeof (bv->block)));
  if (bv != BLOCKVECTOR (s))
    BLOCKVECTOR (s) = bv;

  BLOCKVECTOR_BLOCK (bv, BLOCKVECTOR_NBLOCKS (bv)++) = b;
}

/* Add a new linenumber entry (LINENO,ADR) to a linevector LT.
   MIPS' linenumber encoding might need more than one byte
   to describe it, LAST is used to detect these continuation lines.

   Combining lines with the same line number seems like a bad idea.
   E.g: There could be a line number entry with the same line number after the
   prologue and GDB should not ignore it (this is a better way to find
   a prologue than mips_skip_prologue).
   But due to the compressed line table format there are line number entries
   for the same line which are needed to bridge the gap to the next
   line number entry. These entries have a bogus address info with them
   and we are unable to tell them from intended duplicate line number
   entries.
   This is another reason why -ggdb debugging format is preferable.  */

static int
add_line (struct linetable *lt, int lineno, CORE_ADDR adr, int last)
{
  /* DEC c89 sometimes produces zero linenos which confuse gdb.
     Change them to something sensible. */
  if (lineno == 0)
    lineno = 1;
  if (last == 0)
    last = -2;			/* make sure we record first line */

  if (last == lineno)		/* skip continuation lines */
    return lineno;

  lt->item[lt->nitems].line = lineno;
  lt->item[lt->nitems++].pc = adr << 2;
  return lineno;
}

/* Sorting and reordering procedures */

/* Blocks with a smaller low bound should come first */

static int
compare_blocks (const void *arg1, const void *arg2)
{
  LONGEST addr_diff;
  struct block **b1 = (struct block **) arg1;
  struct block **b2 = (struct block **) arg2;

  addr_diff = (BLOCK_START ((*b1))) - (BLOCK_START ((*b2)));
  if (addr_diff == 0)
    return (BLOCK_END ((*b2))) - (BLOCK_END ((*b1)));
  return addr_diff;
}

/* Sort the blocks of a symtab S.
   Reorder the blocks in the blockvector by code-address,
   as required by some MI search routines */

static void
sort_blocks (struct symtab *s)
{
  struct blockvector *bv = BLOCKVECTOR (s);

  if (BLOCKVECTOR_NBLOCKS (bv) <= 2)
    {
      /* Cosmetic */
      if (BLOCK_END (BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK)) == 0)
	BLOCK_START (BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK)) = 0;
      if (BLOCK_END (BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK)) == 0)
	BLOCK_START (BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK)) = 0;
      return;
    }
  /*
   * This is very unfortunate: normally all functions are compiled in
   * the order they are found, but if the file is compiled -O3 things
   * are very different.  It would be nice to find a reliable test
   * to detect -O3 images in advance.
   */
  if (BLOCKVECTOR_NBLOCKS (bv) > 3)
    qsort (&BLOCKVECTOR_BLOCK (bv, FIRST_LOCAL_BLOCK),
	   BLOCKVECTOR_NBLOCKS (bv) - FIRST_LOCAL_BLOCK,
	   sizeof (struct block *),
	   compare_blocks);

  {
    CORE_ADDR high = 0;
    int i, j = BLOCKVECTOR_NBLOCKS (bv);

    for (i = FIRST_LOCAL_BLOCK; i < j; i++)
      if (high < BLOCK_END (BLOCKVECTOR_BLOCK (bv, i)))
	high = BLOCK_END (BLOCKVECTOR_BLOCK (bv, i));
    BLOCK_END (BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK)) = high;
  }

  BLOCK_START (BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK)) =
    BLOCK_START (BLOCKVECTOR_BLOCK (bv, FIRST_LOCAL_BLOCK));

  BLOCK_START (BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK)) =
    BLOCK_START (BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK));
  BLOCK_END (BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK)) =
    BLOCK_END (BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK));
}


/* Constructor/restructor/destructor procedures */

/* Allocate a new symtab for NAME.  Needs an estimate of how many
   linenumbers MAXLINES we'll put in it */

static struct symtab *
new_symtab (char *name, int maxlines, struct objfile *objfile)
{
  struct symtab *s = allocate_symtab (name, objfile);

  LINETABLE (s) = new_linetable (maxlines);

  /* All symtabs must have at least two blocks */
  BLOCKVECTOR (s) = new_bvect (2);
  BLOCKVECTOR_BLOCK (BLOCKVECTOR (s), GLOBAL_BLOCK)
    = new_block (NON_FUNCTION_BLOCK);
  BLOCKVECTOR_BLOCK (BLOCKVECTOR (s), STATIC_BLOCK)
    = new_block (NON_FUNCTION_BLOCK);
  BLOCK_SUPERBLOCK (BLOCKVECTOR_BLOCK (BLOCKVECTOR (s), STATIC_BLOCK)) =
    BLOCKVECTOR_BLOCK (BLOCKVECTOR (s), GLOBAL_BLOCK);

  s->free_code = free_linetable;
  s->debugformat = obsavestring ("ECOFF", 5,
				 &objfile->objfile_obstack);
  return (s);
}

/* Allocate a new partial_symtab NAME */

static struct partial_symtab *
new_psymtab (char *name, struct objfile *objfile)
{
  struct partial_symtab *psymtab;

  psymtab = allocate_psymtab (name, objfile);
  psymtab->section_offsets = objfile->section_offsets;

  /* Keep a backpointer to the file's symbols */

  psymtab->read_symtab_private = ((char *)
				  obstack_alloc (&objfile->objfile_obstack,
						 sizeof (struct symloc)));
  memset (psymtab->read_symtab_private, 0, sizeof (struct symloc));
  CUR_BFD (psymtab) = cur_bfd;
  DEBUG_SWAP (psymtab) = debug_swap;
  DEBUG_INFO (psymtab) = debug_info;
  PENDING_LIST (psymtab) = pending_list;

  /* The way to turn this into a symtab is to call... */
  psymtab->read_symtab = mdebug_psymtab_to_symtab;
  return (psymtab);
}


/* Allocate a linetable array of the given SIZE.  Since the struct
   already includes one item, we subtract one when calculating the
   proper size to allocate.  */

static struct linetable *
new_linetable (int size)
{
  struct linetable *l;

  size = (size - 1) * sizeof (l->item) + sizeof (struct linetable);
  l = (struct linetable *) xmalloc (size);
  l->nitems = 0;
  return l;
}

/* Oops, too big. Shrink it.  This was important with the 2.4 linetables,
   I am not so sure about the 3.4 ones.

   Since the struct linetable already includes one item, we subtract one when
   calculating the proper size to allocate.  */

static struct linetable *
shrink_linetable (struct linetable *lt)
{

  return (struct linetable *) xrealloc ((void *) lt,
					(sizeof (struct linetable)
					 + ((lt->nitems - 1)
					    * sizeof (lt->item))));
}

/* Allocate and zero a new blockvector of NBLOCKS blocks. */

static struct blockvector *
new_bvect (int nblocks)
{
  struct blockvector *bv;
  int size;

  size = sizeof (struct blockvector) + nblocks * sizeof (struct block *);
  bv = (struct blockvector *) xzalloc (size);

  BLOCKVECTOR_NBLOCKS (bv) = nblocks;

  return bv;
}

/* Allocate and zero a new block, and set its BLOCK_DICT.  If function
   is non-zero, assume the block is associated to a function, and make
   sure that the symbols are stored linearly; otherwise, store them
   hashed.  */

static struct block *
new_block (enum block_type type)
{
  /* FIXME: carlton/2003-09-11: This should use allocate_block to
     allocate the block.  Which, in turn, suggests that the block
     should be allocated on an obstack.  */
  struct block *retval = xzalloc (sizeof (struct block));

  if (type == FUNCTION_BLOCK)
    BLOCK_DICT (retval) = dict_create_linear_expandable ();
  else
    BLOCK_DICT (retval) = dict_create_hashed_expandable ();

  return retval;
}

/* Create a new symbol with printname NAME */

static struct symbol *
new_symbol (char *name)
{
  struct symbol *s = ((struct symbol *)
		      obstack_alloc (&current_objfile->objfile_obstack,
				     sizeof (struct symbol)));

  memset (s, 0, sizeof (*s));
  SYMBOL_LANGUAGE (s) = psymtab_language;
  SYMBOL_SET_NAMES (s, name, strlen (name), current_objfile);
  return s;
}

/* Create a new type with printname NAME */

static struct type *
new_type (char *name)
{
  struct type *t;

  t = alloc_type (current_objfile);
  TYPE_NAME (t) = name;
  TYPE_CPLUS_SPECIFIC (t) = (struct cplus_struct_type *) &cplus_struct_default;
  return t;
}

/* Read ECOFF debugging information from a BFD section.  This is
   called from elfread.c.  It parses the section into a
   ecoff_debug_info struct, and then lets the rest of the file handle
   it as normal.  */

void
elfmdebug_build_psymtabs (struct objfile *objfile,
			  const struct ecoff_debug_swap *swap, asection *sec)
{
  bfd *abfd = objfile->obfd;
  struct ecoff_debug_info *info;
  struct cleanup *back_to;

  /* FIXME: It's not clear whether we should be getting minimal symbol
     information from .mdebug in an ELF file, or whether we will.
     Re-initialize the minimal symbol reader in case we do.  */

  init_minimal_symbol_collection ();
  back_to = make_cleanup_discard_minimal_symbols ();

  info = ((struct ecoff_debug_info *)
	  obstack_alloc (&objfile->objfile_obstack,
			 sizeof (struct ecoff_debug_info)));

  if (!(*swap->read_debug_info) (abfd, sec, info))
    error ("Error reading ECOFF debugging information: %s",
	   bfd_errmsg (bfd_get_error ()));

  mdebug_build_psymtabs (objfile, swap, info);

  install_minimal_symbols (objfile);
  do_cleanups (back_to);
}


/* Things used for calling functions in the inferior.
   These functions are exported to our companion
   mips-tdep.c file and are here because they play
   with the symbol-table explicitly. */

/* Sigtramp: make sure we have all the necessary information
   about the signal trampoline code. Since the official code
   from MIPS does not do so, we make up that information ourselves.
   If they fix the library (unlikely) this code will neutralize itself. */

/* FIXME: This function is called only by mips-tdep.c.  It needs to be
   here because it calls functions defined in this file, but perhaps
   this could be handled in a better way.  Only compile it in when
   tm-mips.h is included. */

#ifdef TM_MIPS_H

void
fixup_sigtramp (void)
{
  struct symbol *s;
  struct symtab *st;
  struct block *b, *b0 = NULL;

  sigtramp_address = -1;

  /* We have to handle the following cases here:
     a) The Mips library has a sigtramp label within sigvec.
     b) Irix has a _sigtramp which we want to use, but it also has sigvec.  */
  s = lookup_symbol ("sigvec", 0, VAR_DOMAIN, 0, NULL);
  if (s != 0)
    {
      b0 = SYMBOL_BLOCK_VALUE (s);
      s = lookup_symbol ("sigtramp", b0, VAR_DOMAIN, 0, NULL);
    }
  if (s == 0)
    {
      /* No sigvec or no sigtramp inside sigvec, try _sigtramp.  */
      s = lookup_symbol ("_sigtramp", 0, VAR_DOMAIN, 0, NULL);
    }

  /* But maybe this program uses its own version of sigvec */
  if (s == 0)
    return;

  /* Did we or MIPSco fix the library ? */
  if (SYMBOL_CLASS (s) == LOC_BLOCK)
    {
      sigtramp_address = BLOCK_START (SYMBOL_BLOCK_VALUE (s));
      sigtramp_end = BLOCK_END (SYMBOL_BLOCK_VALUE (s));
      return;
    }

  sigtramp_address = SYMBOL_VALUE (s);
  sigtramp_end = sigtramp_address + 0x88;	/* black magic */

  /* But what symtab does it live in ? */
  st = find_pc_symtab (SYMBOL_VALUE (s));

  /*
   * Ok, there goes the fix: turn it into a procedure, with all the
   * needed info.  Note we make it a nested procedure of sigvec,
   * which is the way the (assembly) code is actually written.
   */
  SYMBOL_DOMAIN (s) = VAR_DOMAIN;
  SYMBOL_CLASS (s) = LOC_BLOCK;
  SYMBOL_TYPE (s) = init_type (TYPE_CODE_FUNC, 4, 0, (char *) NULL,
			       st->objfile);
  TYPE_TARGET_TYPE (SYMBOL_TYPE (s)) = mdebug_type_void;

  /* Need a block to allocate MIPS_EFI_SYMBOL_NAME in */
  b = new_block (NON_FUNCTION_BLOCK);
  SYMBOL_BLOCK_VALUE (s) = b;
  BLOCK_START (b) = sigtramp_address;
  BLOCK_END (b) = sigtramp_end;
  BLOCK_FUNCTION (b) = s;
  BLOCK_SUPERBLOCK (b) = BLOCK_SUPERBLOCK (b0);
  add_block (b, st);
  sort_blocks (st);

  /* Make a MIPS_EFI_SYMBOL_NAME entry for it */
  {
    struct mips_extra_func_info *e =
    ((struct mips_extra_func_info *)
     xzalloc (sizeof (struct mips_extra_func_info)));

    e->numargs = 0;		/* the kernel thinks otherwise */
    e->pdr.frameoffset = 32;
    e->pdr.framereg = SP_REGNUM;
    /* Note that setting pcreg is no longer strictly necessary as
       mips_frame_saved_pc is now aware of signal handler frames.  */
    e->pdr.pcreg = PC_REGNUM;
    e->pdr.regmask = -2;
    /* Offset to saved r31, in the sigtramp case the saved registers
       are above the frame in the sigcontext.
       We have 4 alignment bytes, 12 bytes for onstack, mask and pc,
       32 * 4 bytes for the general registers, 12 bytes for mdhi, mdlo, ownedfp
       and 32 * 4 bytes for the floating point registers.  */
    e->pdr.regoffset = 4 + 12 + 31 * 4;
    e->pdr.fregmask = -1;
    /* Offset to saved f30 (first saved *double* register).  */
    e->pdr.fregoffset = 4 + 12 + 32 * 4 + 12 + 30 * 4;
    e->pdr.isym = (long) s;
    e->pdr.adr = sigtramp_address;

    current_objfile = st->objfile;	/* Keep new_symbol happy */
    s = new_symbol (MIPS_EFI_SYMBOL_NAME);
    SYMBOL_VALUE (s) = (long) e;
    SYMBOL_DOMAIN (s) = LABEL_DOMAIN;
    SYMBOL_CLASS (s) = LOC_CONST;
    SYMBOL_TYPE (s) = mdebug_type_void;
    current_objfile = NULL;
  }

  dict_add_symbol (BLOCK_DICT (b), s);
}

#endif /* TM_MIPS_H */

void
_initialize_mdebugread (void)
{
  mdebug_type_void =
    init_type (TYPE_CODE_VOID, 1,
	       0,
	       "void", (struct objfile *) NULL);
  mdebug_type_char =
    init_type (TYPE_CODE_INT, 1,
	       0,
	       "char", (struct objfile *) NULL);
  mdebug_type_unsigned_char =
    init_type (TYPE_CODE_INT, 1,
	       TYPE_FLAG_UNSIGNED,
	       "unsigned char", (struct objfile *) NULL);
  mdebug_type_short =
    init_type (TYPE_CODE_INT, 2,
	       0,
	       "short", (struct objfile *) NULL);
  mdebug_type_unsigned_short =
    init_type (TYPE_CODE_INT, 2,
	       TYPE_FLAG_UNSIGNED,
	       "unsigned short", (struct objfile *) NULL);
  mdebug_type_int_32 =
    init_type (TYPE_CODE_INT, 4,
	       0,
	       "int", (struct objfile *) NULL);
  mdebug_type_unsigned_int_32 =
    init_type (TYPE_CODE_INT, 4,
	       TYPE_FLAG_UNSIGNED,
	       "unsigned int", (struct objfile *) NULL);
  mdebug_type_int_64 =
    init_type (TYPE_CODE_INT, 8,
	       0,
	       "int", (struct objfile *) NULL);
  mdebug_type_unsigned_int_64 =
    init_type (TYPE_CODE_INT, 8,
	       TYPE_FLAG_UNSIGNED,
	       "unsigned int", (struct objfile *) NULL);
  mdebug_type_long_32 =
    init_type (TYPE_CODE_INT, 4,
	       0,
	       "long", (struct objfile *) NULL);
  mdebug_type_unsigned_long_32 =
    init_type (TYPE_CODE_INT, 4,
	       TYPE_FLAG_UNSIGNED,
	       "unsigned long", (struct objfile *) NULL);
  mdebug_type_long_64 =
    init_type (TYPE_CODE_INT, 8,
	       0,
	       "long", (struct objfile *) NULL);
  mdebug_type_unsigned_long_64 =
    init_type (TYPE_CODE_INT, 8,
	       TYPE_FLAG_UNSIGNED,
	       "unsigned long", (struct objfile *) NULL);
  mdebug_type_long_long_64 =
    init_type (TYPE_CODE_INT, 8,
	       0,
	       "long long", (struct objfile *) NULL);
  mdebug_type_unsigned_long_long_64 =
    init_type (TYPE_CODE_INT, 8,
	       TYPE_FLAG_UNSIGNED,
	       "unsigned long long", (struct objfile *) NULL);
  mdebug_type_adr_32 =
    init_type (TYPE_CODE_PTR, 4,
	       TYPE_FLAG_UNSIGNED,
	       "adr_32", (struct objfile *) NULL);
  TYPE_TARGET_TYPE (mdebug_type_adr_32) = mdebug_type_void;
  mdebug_type_adr_64 =
    init_type (TYPE_CODE_PTR, 8,
	       TYPE_FLAG_UNSIGNED,
	       "adr_64", (struct objfile *) NULL);
  TYPE_TARGET_TYPE (mdebug_type_adr_64) = mdebug_type_void;
  mdebug_type_float =
    init_type (TYPE_CODE_FLT, TARGET_FLOAT_BIT / TARGET_CHAR_BIT,
	       0,
	       "float", (struct objfile *) NULL);
  mdebug_type_double =
    init_type (TYPE_CODE_FLT, TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
	       0,
	       "double", (struct objfile *) NULL);
  mdebug_type_complex =
    init_type (TYPE_CODE_COMPLEX, 2 * TARGET_FLOAT_BIT / TARGET_CHAR_BIT,
	       0,
	       "complex", (struct objfile *) NULL);
  TYPE_TARGET_TYPE (mdebug_type_complex) = mdebug_type_float;
  mdebug_type_double_complex =
    init_type (TYPE_CODE_COMPLEX, 2 * TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
	       0,
	       "double complex", (struct objfile *) NULL);
  TYPE_TARGET_TYPE (mdebug_type_double_complex) = mdebug_type_double;

  /* Is a "string" the way btString means it the same as TYPE_CODE_STRING?
     FIXME.  */
  mdebug_type_string =
    init_type (TYPE_CODE_STRING,
	       TARGET_CHAR_BIT / TARGET_CHAR_BIT,
	       0, "string",
	       (struct objfile *) NULL);

  /* We use TYPE_CODE_INT to print these as integers.  Does this do any
     good?  Would we be better off with TYPE_CODE_ERROR?  Should
     TYPE_CODE_ERROR print things in hex if it knows the size?  */
  mdebug_type_fixed_dec =
    init_type (TYPE_CODE_INT,
	       TARGET_INT_BIT / TARGET_CHAR_BIT,
	       0, "fixed decimal",
	       (struct objfile *) NULL);

  mdebug_type_float_dec =
    init_type (TYPE_CODE_ERROR,
	       TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
	       0, "floating decimal",
	       (struct objfile *) NULL);

  nodebug_func_symbol_type = init_type (TYPE_CODE_FUNC, 1, 0,
					"<function, no debug info>", NULL);
  TYPE_TARGET_TYPE (nodebug_func_symbol_type) = mdebug_type_int;
  nodebug_var_symbol_type =
    init_type (TYPE_CODE_INT, TARGET_INT_BIT / HOST_CHAR_BIT, 0,
	       "<variable, no debug info>", NULL);
}

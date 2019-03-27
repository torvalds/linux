/* Read NLM (NetWare Loadable Module) format executable files for GDB.
   Copyright 1993, 1994, 1995, 1996, 1998, 1999, 2000
   Free Software Foundation, Inc.
   Written by Fred Fish at Cygnus Support (fnf@cygnus.com).

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

#include "defs.h"
#include "bfd.h"
#include "symtab.h"
#include "symfile.h"
#include "objfiles.h"
#include "buildsym.h"
#include "stabsread.h"
#include "block.h"

extern void _initialize_nlmread (void);

static void nlm_new_init (struct objfile *);

static void nlm_symfile_init (struct objfile *);

static void nlm_symfile_read (struct objfile *, int);

static void nlm_symfile_finish (struct objfile *);

static void nlm_symtab_read (bfd *, CORE_ADDR, struct objfile *);

/* Initialize anything that needs initializing when a completely new symbol
   file is specified (not just adding some symbols from another file, e.g. a
   shared library).

   We reinitialize buildsym, since gdb will be able to read stabs from an NLM
   file at some point in the near future.  */

static void
nlm_new_init (struct objfile *ignore)
{
  stabsread_new_init ();
  buildsym_new_init ();
}


/* NLM specific initialization routine for reading symbols.

   It is passed a pointer to a struct sym_fns which contains, among other
   things, the BFD for the file whose symbols are being read, and a slot for
   a pointer to "private data" which we can fill with goodies.

   For now at least, we have nothing in particular to do, so this function is
   just a stub. */

static void
nlm_symfile_init (struct objfile *ignore)
{
}

/*

   LOCAL FUNCTION

   nlm_symtab_read -- read the symbol table of an NLM file

   SYNOPSIS

   void nlm_symtab_read (bfd *abfd, CORE_ADDR addr,
   struct objfile *objfile)

   DESCRIPTION

   Given an open bfd, a base address to relocate symbols to, and a
   flag that specifies whether or not this bfd is for an executable
   or not (may be shared library for example), add all the global
   function and data symbols to the minimal symbol table.
 */

static void
nlm_symtab_read (bfd *abfd, CORE_ADDR addr, struct objfile *objfile)
{
  long storage_needed;
  asymbol *sym;
  asymbol **symbol_table;
  long number_of_symbols;
  long i;
  struct cleanup *back_to;
  CORE_ADDR symaddr;
  enum minimal_symbol_type ms_type;

  storage_needed = bfd_get_symtab_upper_bound (abfd);
  if (storage_needed < 0)
    error ("Can't read symbols from %s: %s", bfd_get_filename (abfd),
	   bfd_errmsg (bfd_get_error ()));
  if (storage_needed > 0)
    {
      symbol_table = (asymbol **) xmalloc (storage_needed);
      back_to = make_cleanup (xfree, symbol_table);
      number_of_symbols = bfd_canonicalize_symtab (abfd, symbol_table);
      if (number_of_symbols < 0)
	error ("Can't read symbols from %s: %s", bfd_get_filename (abfd),
	       bfd_errmsg (bfd_get_error ()));

      for (i = 0; i < number_of_symbols; i++)
	{
	  sym = symbol_table[i];
	  if ( /*sym -> flags & BSF_GLOBAL */ 1)
	    {
	      /* Bfd symbols are section relative. */
	      symaddr = sym->value + sym->section->vma;
	      /* Relocate all non-absolute symbols by base address.  */
	      if (sym->section != &bfd_abs_section)
		symaddr += addr;

	      /* For non-absolute symbols, use the type of the section
	         they are relative to, to intuit text/data.  BFD provides
	         no way of figuring this out for absolute symbols. */
	      if (sym->section->flags & SEC_CODE)
		ms_type = mst_text;
	      else if (sym->section->flags & SEC_DATA)
		ms_type = mst_data;
	      else
		ms_type = mst_unknown;

	      prim_record_minimal_symbol (sym->name, symaddr, ms_type,
					  objfile);
	    }
	}
      do_cleanups (back_to);
    }
}


/* Scan and build partial symbols for a symbol file.
   We have been initialized by a call to nlm_symfile_init, which 
   currently does nothing.

   SECTION_OFFSETS is a set of offsets to apply to relocate the symbols
   in each section.  We simplify it down to a single offset for all
   symbols.  FIXME.

   MAINLINE is true if we are reading the main symbol
   table (as opposed to a shared lib or dynamically loaded file).

   This function only does the minimum work necessary for letting the
   user "name" things symbolically; it does not read the entire symtab.
   Instead, it reads the external and static symbols and puts them in partial
   symbol tables.  When more extensive information is requested of a
   file, the corresponding partial symbol table is mutated into a full
   fledged symbol table by going back and reading the symbols
   for real.

   Note that NLM files have two sets of information that is potentially
   useful for building gdb's minimal symbol table.  The first is a list
   of the publically exported symbols, and is currently used to build
   bfd's canonical symbol table.  The second is an optional native debugging
   format which contains additional symbols (and possibly duplicates of
   the publically exported symbols).  The optional native debugging format
   is not currently used. */

static void
nlm_symfile_read (struct objfile *objfile, int mainline)
{
  bfd *abfd = objfile->obfd;
  struct cleanup *back_to;
  CORE_ADDR offset;
  struct symbol *mainsym;

  init_minimal_symbol_collection ();
  back_to = make_cleanup_discard_minimal_symbols ();

  /* FIXME, should take a section_offsets param, not just an offset.  */

  offset = ANOFFSET (objfile->section_offsets, 0);

  /* Process the NLM export records, which become the bfd's canonical symbol
     table. */

  nlm_symtab_read (abfd, offset, objfile);

  /* Install any minimal symbols that have been collected as the current
     minimal symbols for this objfile. */

  install_minimal_symbols (objfile);
  do_cleanups (back_to);

  stabsect_build_psymtabs (objfile, mainline, ".stab",
			   ".stabstr", ".text");

  mainsym = lookup_symbol (main_name (), NULL, VAR_DOMAIN, NULL, NULL);

  if (mainsym
      && SYMBOL_CLASS (mainsym) == LOC_BLOCK)
    {
      objfile->ei.main_func_lowpc = BLOCK_START (SYMBOL_BLOCK_VALUE (mainsym));
      objfile->ei.main_func_highpc = BLOCK_END (SYMBOL_BLOCK_VALUE (mainsym));
    }

  /* FIXME:  We could locate and read the optional native debugging format
     here and add the symbols to the minimal symbol table. */
}


/* Perform any local cleanups required when we are done with a particular
   objfile.  I.E, we are in the process of discarding all symbol information
   for an objfile, freeing up all memory held for it, and unlinking the
   objfile struct from the global list of known objfiles. */

static void
nlm_symfile_finish (struct objfile *objfile)
{
  if (objfile->sym_private != NULL)
    {
      xmfree (objfile->md, objfile->sym_private);
    }
}

/* Register that we are able to handle NLM file format. */

static struct sym_fns nlm_sym_fns =
{
  bfd_target_nlm_flavour,
  nlm_new_init,			/* sym_new_init: init anything gbl to entire symtab */
  nlm_symfile_init,		/* sym_init: read initial info, setup for sym_read() */
  nlm_symfile_read,		/* sym_read: read a symbol file into symtab */
  nlm_symfile_finish,		/* sym_finish: finished with file, cleanup */
  default_symfile_offsets,	/* sym_offsets:  Translate ext. to int. relocation */
  NULL				/* next: pointer to next struct sym_fns */
};

void
_initialize_nlmread (void)
{
  add_symtab_fns (&nlm_sym_fns);
}

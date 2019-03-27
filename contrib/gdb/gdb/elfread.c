/* Read ELF (Executable and Linking Format) object files for GDB.

   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

   Written by Fred Fish at Cygnus Support.

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
#include "gdb_string.h"
#include "elf-bfd.h"
#include "elf/mips.h"
#include "symtab.h"
#include "symfile.h"
#include "objfiles.h"
#include "buildsym.h"
#include "stabsread.h"
#include "gdb-stabs.h"
#include "complaints.h"
#include "demangle.h"

extern void _initialize_elfread (void);

/* The struct elfinfo is available only during ELF symbol table and
   psymtab reading.  It is destroyed at the completion of psymtab-reading.
   It's local to elf_symfile_read.  */

struct elfinfo
  {
    file_ptr dboffset;		/* Offset to dwarf debug section */
    unsigned int dbsize;	/* Size of dwarf debug section */
    file_ptr lnoffset;		/* Offset to dwarf line number section */
    unsigned int lnsize;	/* Size of dwarf line number section */
    asection *stabsect;		/* Section pointer for .stab section */
    asection *stabindexsect;	/* Section pointer for .stab.index section */
    asection *mdebugsect;	/* Section pointer for .mdebug section */
  };

static void free_elfinfo (void *);

/* We are called once per section from elf_symfile_read.  We
   need to examine each section we are passed, check to see
   if it is something we are interested in processing, and
   if so, stash away some access information for the section.

   For now we recognize the dwarf debug information sections and
   line number sections from matching their section names.  The
   ELF definition is no real help here since it has no direct
   knowledge of DWARF (by design, so any debugging format can be
   used).

   We also recognize the ".stab" sections used by the Sun compilers
   released with Solaris 2.

   FIXME: The section names should not be hardwired strings (what
   should they be?  I don't think most object file formats have enough
   section flags to specify what kind of debug section it is
   -kingdon).  */

static void
elf_locate_sections (bfd *ignore_abfd, asection *sectp, void *eip)
{
  struct elfinfo *ei;

  ei = (struct elfinfo *) eip;
  if (strcmp (sectp->name, ".debug") == 0)
    {
      ei->dboffset = sectp->filepos;
      ei->dbsize = bfd_get_section_size (sectp);
    }
  else if (strcmp (sectp->name, ".line") == 0)
    {
      ei->lnoffset = sectp->filepos;
      ei->lnsize = bfd_get_section_size (sectp);
    }
  else if (strcmp (sectp->name, ".stab") == 0)
    {
      ei->stabsect = sectp;
    }
  else if (strcmp (sectp->name, ".stab.index") == 0)
    {
      ei->stabindexsect = sectp;
    }
  else if (strcmp (sectp->name, ".mdebug") == 0)
    {
      ei->mdebugsect = sectp;
    }
}

static struct minimal_symbol *
record_minimal_symbol (char *name, CORE_ADDR address,
		       enum minimal_symbol_type ms_type,
		       asection *bfd_section, struct objfile *objfile)
{
  if (ms_type == mst_text || ms_type == mst_file_text)
    address = SMASH_TEXT_ADDRESS (address);

  return prim_record_minimal_symbol_and_info
    (name, address, ms_type, NULL, bfd_section->index, bfd_section, objfile);
}

/*

   LOCAL FUNCTION

   elf_symtab_read -- read the symbol table of an ELF file

   SYNOPSIS

   void elf_symtab_read (struct objfile *objfile, int dynamic,
			 long number_of_symbols, asymbol **symbol_table)

   DESCRIPTION

   Given an objfile, a symbol table, and a flag indicating whether the
   symbol table contains dynamic symbols, add all the global function
   and data symbols to the minimal symbol table.

   In stabs-in-ELF, as implemented by Sun, there are some local symbols
   defined in the ELF symbol table, which can be used to locate
   the beginnings of sections from each ".o" file that was linked to
   form the executable objfile.  We gather any such info and record it
   in data structures hung off the objfile's private data.

 */

static void
elf_symtab_read (struct objfile *objfile, int dynamic,
		 long number_of_symbols, asymbol **symbol_table)
{
  long storage_needed;
  asymbol *sym;
  long i;
  CORE_ADDR symaddr;
  CORE_ADDR offset;
  enum minimal_symbol_type ms_type;
  /* If sectinfo is nonNULL, it contains section info that should end up
     filed in the objfile.  */
  struct stab_section_info *sectinfo = NULL;
  /* If filesym is nonzero, it points to a file symbol, but we haven't
     seen any section info for it yet.  */
  asymbol *filesym = 0;
#ifdef SOFUN_ADDRESS_MAYBE_MISSING
  /* Name of filesym, as saved on the objfile_obstack.  */
  char *filesymname = obsavestring ("", 0, &objfile->objfile_obstack);
#endif
  struct dbx_symfile_info *dbx = objfile->sym_stab_info;
  int stripped = (bfd_get_symcount (objfile->obfd) == 0);

  if (1)
    {
      for (i = 0; i < number_of_symbols; i++)
	{
	  sym = symbol_table[i];
	  if (sym->name == NULL || *sym->name == '\0')
	    {
	      /* Skip names that don't exist (shouldn't happen), or names
	         that are null strings (may happen). */
	      continue;
	    }

          offset = ANOFFSET (objfile->section_offsets, sym->section->index);
	  if (dynamic
	      && sym->section == &bfd_und_section
	      && (sym->flags & BSF_FUNCTION))
	    {
	      struct minimal_symbol *msym;

	      /* Symbol is a reference to a function defined in
	         a shared library.
	         If its value is non zero then it is usually the address
	         of the corresponding entry in the procedure linkage table,
	         plus the desired section offset.
	         If its value is zero then the dynamic linker has to resolve
	         the symbol. We are unable to find any meaningful address
	         for this symbol in the executable file, so we skip it.  */
	      symaddr = sym->value;
	      if (symaddr == 0)
		continue;
	      symaddr += offset;
	      msym = record_minimal_symbol
		((char *) sym->name, symaddr,
		 mst_solib_trampoline, sym->section, objfile);
#ifdef SOFUN_ADDRESS_MAYBE_MISSING
	      if (msym != NULL)
		msym->filename = filesymname;
#endif
	      continue;
	    }

	  /* If it is a nonstripped executable, do not enter dynamic
	     symbols, as the dynamic symbol table is usually a subset
	     of the main symbol table.  */
	  if (dynamic && !stripped)
	    continue;
	  if (sym->flags & BSF_FILE)
	    {
	      /* STT_FILE debugging symbol that helps stabs-in-elf debugging.
	         Chain any old one onto the objfile; remember new sym.  */
	      if (sectinfo != NULL)
		{
		  sectinfo->next = dbx->stab_section_info;
		  dbx->stab_section_info = sectinfo;
		  sectinfo = NULL;
		}
	      filesym = sym;
#ifdef SOFUN_ADDRESS_MAYBE_MISSING
	      filesymname =
		obsavestring ((char *) filesym->name, strlen (filesym->name),
			      &objfile->objfile_obstack);
#endif
	    }
	  else if (sym->flags & (BSF_GLOBAL | BSF_LOCAL | BSF_WEAK))
	    {
	      struct minimal_symbol *msym;

	      /* Select global/local/weak symbols.  Note that bfd puts abs
	         symbols in their own section, so all symbols we are
	         interested in will have a section. */
	      /* Bfd symbols are section relative. */
	      symaddr = sym->value + sym->section->vma;
	      /* Relocate all non-absolute symbols by the section offset.  */
	      if (sym->section != &bfd_abs_section)
		{
		  symaddr += offset;
		}
	      /* For non-absolute symbols, use the type of the section
	         they are relative to, to intuit text/data.  Bfd provides
	         no way of figuring this out for absolute symbols. */
	      if (sym->section == &bfd_abs_section)
		{
		  /* This is a hack to get the minimal symbol type
		     right for Irix 5, which has absolute addresses
		     with special section indices for dynamic symbols. */
		  unsigned short shndx =
		  ((elf_symbol_type *) sym)->internal_elf_sym.st_shndx;

		  switch (shndx)
		    {
		    case SHN_MIPS_TEXT:
		      ms_type = mst_text;
		      break;
		    case SHN_MIPS_DATA:
		      ms_type = mst_data;
		      break;
		    case SHN_MIPS_ACOMMON:
		      ms_type = mst_bss;
		      break;
		    default:
		      ms_type = mst_abs;
		    }

		  /* If it is an Irix dynamic symbol, skip section name
		     symbols, relocate all others by section offset. */
		  if (ms_type != mst_abs)
		    {
		      if (sym->name[0] == '.')
			continue;
		      symaddr += offset;
		    }
		}
	      else if (sym->section->flags & SEC_CODE)
		{
		  if (sym->flags & BSF_GLOBAL)
		    {
		      ms_type = mst_text;
		    }
		  else if ((sym->name[0] == '.' && sym->name[1] == 'L')
			   || ((sym->flags & BSF_LOCAL)
			       && sym->name[0] == '$'
			       && sym->name[1] == 'L'))
		    /* Looks like a compiler-generated label.  Skip
		       it.  The assembler should be skipping these (to
		       keep executables small), but apparently with
		       gcc on the (deleted) delta m88k SVR4, it loses.
		       So to have us check too should be harmless (but
		       I encourage people to fix this in the assembler
		       instead of adding checks here).  */
		    continue;
		  else
		    {
		      ms_type = mst_file_text;
		    }
		}
	      else if (sym->section->flags & SEC_ALLOC)
		{
		  if (sym->flags & (BSF_GLOBAL | BSF_WEAK))
		    {
		      if (sym->section->flags & SEC_LOAD)
			{
			  ms_type = mst_data;
			}
		      else
			{
			  ms_type = mst_bss;
			}
		    }
		  else if (sym->flags & BSF_LOCAL)
		    {
		      /* Named Local variable in a Data section.
		         Check its name for stabs-in-elf.  */
		      int special_local_sect;
		      if (strcmp ("Bbss.bss", sym->name) == 0)
			special_local_sect = SECT_OFF_BSS (objfile);
		      else if (strcmp ("Ddata.data", sym->name) == 0)
			special_local_sect = SECT_OFF_DATA (objfile);
		      else if (strcmp ("Drodata.rodata", sym->name) == 0)
			special_local_sect = SECT_OFF_RODATA (objfile);
		      else
			special_local_sect = -1;
		      if (special_local_sect >= 0)
			{
			  /* Found a special local symbol.  Allocate a
			     sectinfo, if needed, and fill it in.  */
			  if (sectinfo == NULL)
			    {
			      int max_index;
			      size_t size;

			      max_index 
				= max (SECT_OFF_BSS (objfile),
				       max (SECT_OFF_DATA (objfile),
					    SECT_OFF_RODATA (objfile)));

                              /* max_index is the largest index we'll
                                 use into this array, so we must
                                 allocate max_index+1 elements for it.
                                 However, 'struct stab_section_info'
                                 already includes one element, so we
                                 need to allocate max_index aadditional
                                 elements.  */
			      size = (sizeof (struct stab_section_info) 
				      + (sizeof (CORE_ADDR)
					 * max_index));
			      sectinfo = (struct stab_section_info *)
				xmmalloc (objfile->md, size);
			      memset (sectinfo, 0, size);
			      sectinfo->num_sections = max_index;
			      if (filesym == NULL)
				{
				  complaint (&symfile_complaints,
					     "elf/stab section information %s without a preceding file symbol",
					     sym->name);
				}
			      else
				{
				  sectinfo->filename =
				    (char *) filesym->name;
				}
			    }
			  if (sectinfo->sections[special_local_sect] != 0)
			    complaint (&symfile_complaints,
				       "duplicated elf/stab section information for %s",
				       sectinfo->filename);
			  /* BFD symbols are section relative.  */
			  symaddr = sym->value + sym->section->vma;
			  /* Relocate non-absolute symbols by the
                             section offset.  */
			  if (sym->section != &bfd_abs_section)
			    symaddr += offset;
			  sectinfo->sections[special_local_sect] = symaddr;
			  /* The special local symbols don't go in the
			     minimal symbol table, so ignore this one.  */
			  continue;
			}
		      /* Not a special stabs-in-elf symbol, do regular
		         symbol processing.  */
		      if (sym->section->flags & SEC_LOAD)
			{
			  ms_type = mst_file_data;
			}
		      else
			{
			  ms_type = mst_file_bss;
			}
		    }
		  else
		    {
		      ms_type = mst_unknown;
		    }
		}
	      else
		{
		  /* FIXME:  Solaris2 shared libraries include lots of
		     odd "absolute" and "undefined" symbols, that play 
		     hob with actions like finding what function the PC
		     is in.  Ignore them if they aren't text, data, or bss.  */
		  /* ms_type = mst_unknown; */
		  continue;	/* Skip this symbol. */
		}
	      msym = record_minimal_symbol
		((char *) sym->name, symaddr,
		 ms_type, sym->section, objfile);
	      if (msym)
	      {
		/* Pass symbol size field in via BFD.  FIXME!!!  */
		unsigned long size = ((elf_symbol_type *) sym)->internal_elf_sym.st_size;
		MSYMBOL_SIZE(msym) = size;
	      }
#ifdef SOFUN_ADDRESS_MAYBE_MISSING
	      if (msym != NULL)
		msym->filename = filesymname;
#endif
	      ELF_MAKE_MSYMBOL_SPECIAL (sym, msym);
	    }
	}
    }
}

/* Scan and build partial symbols for a symbol file.
   We have been initialized by a call to elf_symfile_init, which 
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

   We look for sections with specific names, to tell us what debug
   format to look for:  FIXME!!!

   dwarf_build_psymtabs() builds psymtabs for DWARF symbols;
   elfstab_build_psymtabs() handles STABS symbols;
   mdebug_build_psymtabs() handles ECOFF debugging information.

   Note that ELF files have a "minimal" symbol table, which looks a lot
   like a COFF symbol table, but has only the minimal information necessary
   for linking.  We process this also, and use the information to
   build gdb's minimal symbol table.  This gives us some minimal debugging
   capability even for files compiled without -g.  */

static void
elf_symfile_read (struct objfile *objfile, int mainline)
{
  bfd *abfd = objfile->obfd;
  struct elfinfo ei;
  struct cleanup *back_to;
  CORE_ADDR offset;
  long symcount = 0, dynsymcount = 0, synthcount, storage_needed;
  asymbol **symbol_table = NULL, **dyn_symbol_table = NULL;
  asymbol *synthsyms;

  init_minimal_symbol_collection ();
  back_to = make_cleanup_discard_minimal_symbols ();

  memset ((char *) &ei, 0, sizeof (ei));

  /* Allocate struct to keep track of the symfile */
  objfile->sym_stab_info = (struct dbx_symfile_info *)
    xmmalloc (objfile->md, sizeof (struct dbx_symfile_info));
  memset ((char *) objfile->sym_stab_info, 0, sizeof (struct dbx_symfile_info));
  make_cleanup (free_elfinfo, (void *) objfile);

  /* Process the normal ELF symbol table first.  This may write some 
     chain of info into the dbx_symfile_info in objfile->sym_stab_info,
     which can later be used by elfstab_offset_sections.  */

  storage_needed = bfd_get_symtab_upper_bound (objfile->obfd);
  if (storage_needed < 0)
    error (_("Can't read symbols from %s: %s"), bfd_get_filename (objfile->obfd),
	   bfd_errmsg (bfd_get_error ()));

  if (storage_needed > 0)
    {
      symbol_table = (asymbol **) xmalloc (storage_needed);
      make_cleanup (xfree, symbol_table);
      symcount = bfd_canonicalize_symtab (objfile->obfd, symbol_table);

      if (symcount < 0)
	error (_("Can't read symbols from %s: %s"), bfd_get_filename (objfile->obfd),
	       bfd_errmsg (bfd_get_error ()));

      elf_symtab_read (objfile, 0, symcount, symbol_table);
    }

  /* Add the dynamic symbols.  */

  storage_needed = bfd_get_dynamic_symtab_upper_bound (objfile->obfd);

  if (storage_needed > 0)
    {
      dyn_symbol_table = (asymbol **) xmalloc (storage_needed);
      make_cleanup (xfree, dyn_symbol_table);
      dynsymcount = bfd_canonicalize_dynamic_symtab (objfile->obfd,
						     dyn_symbol_table);

      if (dynsymcount < 0)
	error (_("Can't read symbols from %s: %s"), bfd_get_filename (objfile->obfd),
	       bfd_errmsg (bfd_get_error ()));

      elf_symtab_read (objfile, 1, dynsymcount, dyn_symbol_table);
    }

  /* Add synthetic symbols - for instance, names for any PLT entries.  */

  synthcount = bfd_get_synthetic_symtab (abfd, symcount, symbol_table,
					 dynsymcount, dyn_symbol_table,
					 &synthsyms);
  if (synthcount > 0)
    {
      asymbol **synth_symbol_table;
      long i;

      make_cleanup (xfree, synthsyms);
      synth_symbol_table = xmalloc (sizeof (asymbol *) * synthcount);
      for (i = 0; i < synthcount; i++)
	{
	  synth_symbol_table[i] = synthsyms + i;
	  /* Synthetic symbols are not, strictly speaking, either local
	     or global.  But we can treat them as global symbols, since
	     they are effectively dynamic symbols.  */
	  synth_symbol_table[i]->flags |= BSF_GLOBAL;
	}
      make_cleanup (xfree, synth_symbol_table);
      elf_symtab_read (objfile, 0, synthcount, synth_symbol_table);
    }

  /* Install any minimal symbols that have been collected as the current
     minimal symbols for this objfile.  The debug readers below this point
     should not generate new minimal symbols; if they do it's their
     responsibility to install them.  "mdebug" appears to be the only one
     which will do this.  */

  install_minimal_symbols (objfile);
  do_cleanups (back_to);

  /* Now process debugging information, which is contained in
     special ELF sections. */

  /* If we are reinitializing, or if we have never loaded syms yet,
     set table to empty.  MAINLINE is cleared so that *_read_psymtab
     functions do not all also re-initialize the psymbol table. */
  if (mainline)
    {
      init_psymbol_list (objfile, 0);
      mainline = 0;
    }

  /* We first have to find them... */
  bfd_map_over_sections (abfd, elf_locate_sections, (void *) & ei);

  /* ELF debugging information is inserted into the psymtab in the
     order of least informative first - most informative last.  Since
     the psymtab table is searched `most recent insertion first' this
     increases the probability that more detailed debug information
     for a section is found.

     For instance, an object file might contain both .mdebug (XCOFF)
     and .debug_info (DWARF2) sections then .mdebug is inserted first
     (searched last) and DWARF2 is inserted last (searched first).  If
     we don't do this then the XCOFF info is found first - for code in
     an included file XCOFF info is useless. */

  if (ei.mdebugsect)
    {
      const struct ecoff_debug_swap *swap;

      /* .mdebug section, presumably holding ECOFF debugging
         information.  */
      swap = get_elf_backend_data (abfd)->elf_backend_ecoff_debug_swap;
      if (swap)
	elfmdebug_build_psymtabs (objfile, swap, ei.mdebugsect);
    }
  if (ei.stabsect)
    {
      asection *str_sect;

      /* Stab sections have an associated string table that looks like
         a separate section.  */
      str_sect = bfd_get_section_by_name (abfd, ".stabstr");

      /* FIXME should probably warn about a stab section without a stabstr.  */
      if (str_sect)
	elfstab_build_psymtabs (objfile,
				mainline,
				ei.stabsect,
				str_sect->filepos,
				bfd_section_size (abfd, str_sect));
    }
  if (dwarf2_has_info (abfd))
    {
      /* DWARF 2 sections */
      dwarf2_build_psymtabs (objfile, mainline);
    }
  else if (ei.dboffset && ei.lnoffset)
    {
      /* DWARF sections */
      dwarf_build_psymtabs (objfile,
			    mainline,
			    ei.dboffset, ei.dbsize,
			    ei.lnoffset, ei.lnsize);
    }

  /* FIXME: kettenis/20030504: This still needs to be integrated with
     dwarf2read.c in a better way.  */
  dwarf2_build_frame_info (objfile);
}

/* This cleans up the objfile's sym_stab_info pointer, and the chain of
   stab_section_info's, that might be dangling from it.  */

static void
free_elfinfo (void *objp)
{
  struct objfile *objfile = (struct objfile *) objp;
  struct dbx_symfile_info *dbxinfo = objfile->sym_stab_info;
  struct stab_section_info *ssi, *nssi;

  ssi = dbxinfo->stab_section_info;
  while (ssi)
    {
      nssi = ssi->next;
      xmfree (objfile->md, ssi);
      ssi = nssi;
    }

  dbxinfo->stab_section_info = 0;	/* Just say No mo info about this.  */
}


/* Initialize anything that needs initializing when a completely new symbol
   file is specified (not just adding some symbols from another file, e.g. a
   shared library).

   We reinitialize buildsym, since we may be reading stabs from an ELF file.  */

static void
elf_new_init (struct objfile *ignore)
{
  stabsread_new_init ();
  buildsym_new_init ();
}

/* Perform any local cleanups required when we are done with a particular
   objfile.  I.E, we are in the process of discarding all symbol information
   for an objfile, freeing up all memory held for it, and unlinking the
   objfile struct from the global list of known objfiles. */

static void
elf_symfile_finish (struct objfile *objfile)
{
  if (objfile->sym_stab_info != NULL)
    {
      xmfree (objfile->md, objfile->sym_stab_info);
    }
}

/* ELF specific initialization routine for reading symbols.

   It is passed a pointer to a struct sym_fns which contains, among other
   things, the BFD for the file whose symbols are being read, and a slot for
   a pointer to "private data" which we can fill with goodies.

   For now at least, we have nothing in particular to do, so this function is
   just a stub. */

static void
elf_symfile_init (struct objfile *objfile)
{
  /* ELF objects may be reordered, so set OBJF_REORDERED.  If we
     find this causes a significant slowdown in gdb then we could
     set it in the debug symbol readers only when necessary.  */
  objfile->flags |= OBJF_REORDERED;
}

/* When handling an ELF file that contains Sun STABS debug info,
   some of the debug info is relative to the particular chunk of the
   section that was generated in its individual .o file.  E.g.
   offsets to static variables are relative to the start of the data
   segment *for that module before linking*.  This information is
   painfully squirreled away in the ELF symbol table as local symbols
   with wierd names.  Go get 'em when needed.  */

void
elfstab_offset_sections (struct objfile *objfile, struct partial_symtab *pst)
{
  char *filename = pst->filename;
  struct dbx_symfile_info *dbx = objfile->sym_stab_info;
  struct stab_section_info *maybe = dbx->stab_section_info;
  struct stab_section_info *questionable = 0;
  int i;
  char *p;

  /* The ELF symbol info doesn't include path names, so strip the path
     (if any) from the psymtab filename.  */
  while (0 != (p = strchr (filename, '/')))
    filename = p + 1;

  /* FIXME:  This linear search could speed up significantly
     if it was chained in the right order to match how we search it,
     and if we unchained when we found a match. */
  for (; maybe; maybe = maybe->next)
    {
      if (filename[0] == maybe->filename[0]
	  && strcmp (filename, maybe->filename) == 0)
	{
	  /* We found a match.  But there might be several source files
	     (from different directories) with the same name.  */
	  if (0 == maybe->found)
	    break;
	  questionable = maybe;	/* Might use it later.  */
	}
    }

  if (maybe == 0 && questionable != 0)
    {
      complaint (&symfile_complaints,
		 "elf/stab section information questionable for %s", filename);
      maybe = questionable;
    }

  if (maybe)
    {
      /* Found it!  Allocate a new psymtab struct, and fill it in.  */
      maybe->found++;
      pst->section_offsets = (struct section_offsets *)
	obstack_alloc (&objfile->objfile_obstack, 
		       SIZEOF_N_SECTION_OFFSETS (objfile->num_sections));
      for (i = 0; i < maybe->num_sections; i++)
	(pst->section_offsets)->offsets[i] = maybe->sections[i];
      return;
    }

  /* We were unable to find any offsets for this file.  Complain.  */
  if (dbx->stab_section_info)	/* If there *is* any info, */
    complaint (&symfile_complaints,
	       "elf/stab section information missing for %s", filename);
}

/* Register that we are able to handle ELF object file formats.  */

static struct sym_fns elf_sym_fns =
{
  bfd_target_elf_flavour,
  elf_new_init,			/* sym_new_init: init anything gbl to entire symtab */
  elf_symfile_init,		/* sym_init: read initial info, setup for sym_read() */
  elf_symfile_read,		/* sym_read: read a symbol file into symtab */
  elf_symfile_finish,		/* sym_finish: finished with file, cleanup */
  default_symfile_offsets,	/* sym_offsets:  Translate ext. to int. relocation */
  NULL				/* next: pointer to next struct sym_fns */
};

void
_initialize_elfread (void)
{
  add_symtab_fns (&elf_sym_fns);
}

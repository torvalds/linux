/* Read a symbol table in MIPS' format (Third-Eye).
   Copyright 1986, 1987, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996,
   1998, 1999, 2000, 2001, 2003, 2004
   Free Software Foundation, Inc.
   Contributed by Alessandro Forin (af@cs.cmu.edu) at CMU.  Major work
   by Per Bothner, John Gilmore and Ian Lance Taylor at Cygnus Support.

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

/* Read symbols from an ECOFF file.  Most of the work is done in
   mdebugread.c.  */

#include "defs.h"
#include "gdb_string.h"
#include "bfd.h"
#include "symtab.h"
#include "objfiles.h"
#include "buildsym.h"
#include "stabsread.h"

#include "coff/sym.h"
#include "coff/internal.h"
#include "coff/ecoff.h"
#include "libcoff.h"		/* Private BFD COFF information.  */
#include "libecoff.h"		/* Private BFD ECOFF information.  */
#include "elf/common.h"
#include "elf/mips.h"

extern void _initialize_mipsread (void);

static void mipscoff_new_init (struct objfile *);

static void mipscoff_symfile_init (struct objfile *);

static void mipscoff_symfile_read (struct objfile *, int);

static void mipscoff_symfile_finish (struct objfile *);

static void
read_alphacoff_dynamic_symtab (struct section_offsets *,
			       struct objfile *objfile);

/* Initialize anything that needs initializing when a completely new
   symbol file is specified (not just adding some symbols from another
   file, e.g. a shared library).  */

extern CORE_ADDR sigtramp_address;

static void
mipscoff_new_init (struct objfile *ignore)
{
  sigtramp_address = 0;
  stabsread_new_init ();
  buildsym_new_init ();
}

/* Initialize to read a symbol file (nothing to do).  */

static void
mipscoff_symfile_init (struct objfile *objfile)
{
}

/* Read a symbol file from a file.  */

static void
mipscoff_symfile_read (struct objfile *objfile, int mainline)
{
  bfd *abfd = objfile->obfd;
  struct cleanup *back_to;

  init_minimal_symbol_collection ();
  back_to = make_cleanup_discard_minimal_symbols ();

  /* Now that the executable file is positioned at symbol table,
     process it and define symbols accordingly.  */

  if (!((*ecoff_backend (abfd)->debug_swap.read_debug_info)
	(abfd, (asection *) NULL, &ecoff_data (abfd)->debug_info)))
    error ("Error reading symbol table: %s", bfd_errmsg (bfd_get_error ()));

  mdebug_build_psymtabs (objfile, &ecoff_backend (abfd)->debug_swap,
			 &ecoff_data (abfd)->debug_info);

  /* Add alpha coff dynamic symbols.  */

  read_alphacoff_dynamic_symtab (objfile->section_offsets, objfile);

  /* Install any minimal symbols that have been collected as the current
     minimal symbols for this objfile. */

  install_minimal_symbols (objfile);

  /* If the entry_file bounds are still unknown after processing the
     partial symbols, then try to set them from the minimal symbols
     surrounding the entry_point.  */

  if (mainline
      && objfile->ei.entry_point != INVALID_ENTRY_POINT
      && objfile->ei.deprecated_entry_file_lowpc == INVALID_ENTRY_LOWPC)
    {
      struct minimal_symbol *m;

      m = lookup_minimal_symbol_by_pc (objfile->ei.entry_point);
      if (m && DEPRECATED_SYMBOL_NAME (m + 1))
	{
	  objfile->ei.deprecated_entry_file_lowpc = SYMBOL_VALUE_ADDRESS (m);
	  objfile->ei.deprecated_entry_file_highpc = SYMBOL_VALUE_ADDRESS (m + 1);
	}
    }

  do_cleanups (back_to);
}

/* Perform any local cleanups required when we are done with a
   particular objfile.  */

static void
mipscoff_symfile_finish (struct objfile *objfile)
{
}

/* Alpha OSF/1 encapsulates the dynamic symbols in ELF format in a
   standard coff section.  The ELF format for the symbols differs from
   the format defined in elf/external.h. It seems that a normal ELF 32 bit
   format is used, and the representation only changes because longs are
   64 bit on the alpha. In addition, the handling of text/data section
   indices for symbols is different from the ELF ABI.
   As the BFD linker currently does not support dynamic linking on the alpha,
   there seems to be no reason to pollute BFD with another mixture of object
   file formats for now.  */

/* Format of an alpha external ELF symbol.  */

typedef struct
{
  unsigned char st_name[4];	/* Symbol name, index in string tbl */
  unsigned char st_pad[4];	/* Pad to long word boundary */
  unsigned char st_value[8];	/* Value of the symbol */
  unsigned char st_size[4];	/* Associated symbol size */
  unsigned char st_info[1];	/* Type and binding attributes */
  unsigned char st_other[1];	/* No defined meaning, 0 */
  unsigned char st_shndx[2];	/* Associated section index */
}
Elfalpha_External_Sym;

/* Format of an alpha external ELF dynamic info structure.  */

typedef struct
  {
    unsigned char d_tag[4];	/* Tag */
    unsigned char d_pad[4];	/* Pad to long word boundary */
    union
      {
	unsigned char d_ptr[8];	/* Pointer value */
	unsigned char d_val[4];	/* Integer value */
      }
    d_un;
  }
Elfalpha_External_Dyn;

/* Struct to obtain the section pointers for alpha dynamic symbol info.  */

struct alphacoff_dynsecinfo
  {
    asection *sym_sect;		/* Section pointer for .dynsym section */
    asection *str_sect;		/* Section pointer for .dynstr section */
    asection *dyninfo_sect;	/* Section pointer for .dynamic section */
    asection *got_sect;		/* Section pointer for .got section */
  };

/* We are called once per section from read_alphacoff_dynamic_symtab.
   We need to examine each section we are passed, check to see
   if it is something we are interested in processing, and
   if so, stash away some access information for the section.  */

static void
alphacoff_locate_sections (bfd *ignore_abfd, asection *sectp, void *sip)
{
  struct alphacoff_dynsecinfo *si;

  si = (struct alphacoff_dynsecinfo *) sip;

  if (DEPRECATED_STREQ (sectp->name, ".dynsym"))
    {
      si->sym_sect = sectp;
    }
  else if (DEPRECATED_STREQ (sectp->name, ".dynstr"))
    {
      si->str_sect = sectp;
    }
  else if (DEPRECATED_STREQ (sectp->name, ".dynamic"))
    {
      si->dyninfo_sect = sectp;
    }
  else if (DEPRECATED_STREQ (sectp->name, ".got"))
    {
      si->got_sect = sectp;
    }
}

/* Scan an alpha dynamic symbol table for symbols of interest and
   add them to the minimal symbol table.  */

static void
read_alphacoff_dynamic_symtab (struct section_offsets *section_offsets,
			       struct objfile *objfile)
{
  bfd *abfd = objfile->obfd;
  struct alphacoff_dynsecinfo si;
  char *sym_secptr;
  char *str_secptr;
  char *dyninfo_secptr;
  char *got_secptr;
  bfd_size_type sym_secsize;
  bfd_size_type str_secsize;
  bfd_size_type dyninfo_secsize;
  bfd_size_type got_secsize;
  int sym_count;
  int i;
  int stripped;
  Elfalpha_External_Sym *x_symp;
  char *dyninfo_p;
  char *dyninfo_end;
  int got_entry_size = 8;
  int dt_mips_local_gotno = -1;
  int dt_mips_gotsym = -1;
  struct cleanup *cleanups;


  /* We currently only know how to handle alpha dynamic symbols.  */
  if (bfd_get_arch (abfd) != bfd_arch_alpha)
    return;

  /* Locate the dynamic symbols sections and read them in.  */
  memset ((char *) &si, 0, sizeof (si));
  bfd_map_over_sections (abfd, alphacoff_locate_sections, (void *) & si);
  if (si.sym_sect == NULL
      || si.str_sect == NULL
      || si.dyninfo_sect == NULL
      || si.got_sect == NULL)
    return;

  sym_secsize = bfd_get_section_size (si.sym_sect);
  str_secsize = bfd_get_section_size (si.str_sect);
  dyninfo_secsize = bfd_get_section_size (si.dyninfo_sect);
  got_secsize = bfd_get_section_size (si.got_sect);
  sym_secptr = xmalloc (sym_secsize);
  cleanups = make_cleanup (free, sym_secptr);
  str_secptr = xmalloc (str_secsize);
  make_cleanup (free, str_secptr);
  dyninfo_secptr = xmalloc (dyninfo_secsize);
  make_cleanup (free, dyninfo_secptr);
  got_secptr = xmalloc (got_secsize);
  make_cleanup (free, got_secptr);

  if (!bfd_get_section_contents (abfd, si.sym_sect, sym_secptr,
				 (file_ptr) 0, sym_secsize))
    return;
  if (!bfd_get_section_contents (abfd, si.str_sect, str_secptr,
				 (file_ptr) 0, str_secsize))
    return;
  if (!bfd_get_section_contents (abfd, si.dyninfo_sect, dyninfo_secptr,
				 (file_ptr) 0, dyninfo_secsize))
    return;
  if (!bfd_get_section_contents (abfd, si.got_sect, got_secptr,
				 (file_ptr) 0, got_secsize))
    return;

  /* Find the number of local GOT entries and the index for the
     the first dynamic symbol in the GOT. */
  for (dyninfo_p = dyninfo_secptr, dyninfo_end = dyninfo_p + dyninfo_secsize;
       dyninfo_p < dyninfo_end;
       dyninfo_p += sizeof (Elfalpha_External_Dyn))
    {
      Elfalpha_External_Dyn *x_dynp = (Elfalpha_External_Dyn *) dyninfo_p;
      long dyn_tag;

      dyn_tag = bfd_h_get_32 (abfd, (bfd_byte *) x_dynp->d_tag);
      if (dyn_tag == DT_NULL)
	break;
      else if (dyn_tag == DT_MIPS_LOCAL_GOTNO)
	{
	  if (dt_mips_local_gotno < 0)
	    dt_mips_local_gotno
	      = bfd_h_get_32 (abfd, (bfd_byte *) x_dynp->d_un.d_val);
	}
      else if (dyn_tag == DT_MIPS_GOTSYM)
	{
	  if (dt_mips_gotsym < 0)
	    dt_mips_gotsym
	      = bfd_h_get_32 (abfd, (bfd_byte *) x_dynp->d_un.d_val);
	}
    }
  if (dt_mips_local_gotno < 0 || dt_mips_gotsym < 0)
    return;

  /* Scan all dynamic symbols and enter them into the minimal symbol table
     if appropriate.  */
  sym_count = sym_secsize / sizeof (Elfalpha_External_Sym);
  stripped = (bfd_get_symcount (abfd) == 0);

  /* Skip first symbol, which is a null dummy.  */
  for (i = 1, x_symp = (Elfalpha_External_Sym *) sym_secptr + 1;
       i < sym_count;
       i++, x_symp++)
    {
      unsigned long strx;
      char *name;
      bfd_vma sym_value;
      unsigned char sym_info;
      unsigned int sym_shndx;
      int isglobal;
      enum minimal_symbol_type ms_type;

      strx = bfd_h_get_32 (abfd, (bfd_byte *) x_symp->st_name);
      if (strx >= str_secsize)
	continue;
      name = str_secptr + strx;
      if (*name == '\0' || *name == '.')
	continue;

      sym_value = bfd_h_get_64 (abfd, (bfd_byte *) x_symp->st_value);
      sym_info = bfd_h_get_8 (abfd, (bfd_byte *) x_symp->st_info);
      sym_shndx = bfd_h_get_16 (abfd, (bfd_byte *) x_symp->st_shndx);
      isglobal = (ELF_ST_BIND (sym_info) == STB_GLOBAL);

      if (sym_shndx == SHN_UNDEF)
	{
	  /* Handle undefined functions which are defined in a shared
	     library.  */
	  if (ELF_ST_TYPE (sym_info) != STT_FUNC
	      || ELF_ST_BIND (sym_info) != STB_GLOBAL)
	    continue;

	  ms_type = mst_solib_trampoline;

	  /* If sym_value is nonzero, it points to the shared library
	     trampoline entry, which is what we are looking for.

	     If sym_value is zero, then we have to get the GOT entry
	     for the symbol.
	     If the GOT entry is nonzero, it represents the quickstart
	     address of the function and we use that as the symbol value.

	     If the GOT entry is zero, the function address has to be resolved
	     by the runtime loader before the executable is started.
	     We are unable to find any meaningful address for these
	     functions in the executable file, so we skip them.  */
	  if (sym_value == 0)
	    {
	      int got_entry_offset =
	      (i - dt_mips_gotsym + dt_mips_local_gotno) * got_entry_size;

	      if (got_entry_offset < 0 || got_entry_offset >= got_secsize)
		continue;
	      sym_value =
		bfd_h_get_64 (abfd,
			      (bfd_byte *) (got_secptr + got_entry_offset));
	      if (sym_value == 0)
		continue;
	    }
	}
      else
	{
	  /* Symbols defined in the executable itself. We only care about
	     them if this is a stripped executable, otherwise they have
	     been retrieved from the normal symbol table already.  */
	  if (!stripped)
	    continue;

	  if (sym_shndx == SHN_MIPS_TEXT)
	    {
	      if (isglobal)
		ms_type = mst_text;
	      else
		ms_type = mst_file_text;
	      sym_value += ANOFFSET (section_offsets, SECT_OFF_TEXT (objfile));
	    }
	  else if (sym_shndx == SHN_MIPS_DATA)
	    {
	      if (isglobal)
		ms_type = mst_data;
	      else
		ms_type = mst_file_data;
	      sym_value += ANOFFSET (section_offsets, SECT_OFF_DATA (objfile));
	    }
	  else if (sym_shndx == SHN_MIPS_ACOMMON)
	    {
	      if (isglobal)
		ms_type = mst_bss;
	      else
		ms_type = mst_file_bss;
	      sym_value += ANOFFSET (section_offsets, SECT_OFF_BSS (objfile));
	    }
	  else if (sym_shndx == SHN_ABS)
	    {
	      ms_type = mst_abs;
	    }
	  else
	    {
	      continue;
	    }
	}

      prim_record_minimal_symbol (name, sym_value, ms_type, objfile);
    }

  do_cleanups (cleanups);
}

/* Initialization */

static struct sym_fns ecoff_sym_fns =
{
  bfd_target_ecoff_flavour,
  mipscoff_new_init,		/* sym_new_init: init anything gbl to entire symtab */
  mipscoff_symfile_init,	/* sym_init: read initial info, setup for sym_read() */
  mipscoff_symfile_read,	/* sym_read: read a symbol file into symtab */
  mipscoff_symfile_finish,	/* sym_finish: finished with file, cleanup */
  default_symfile_offsets,	/* sym_offsets: dummy FIXME til implem sym reloc */
  NULL				/* next: pointer to next struct sym_fns */
};

void
_initialize_mipsread (void)
{
  add_symtab_fns (&ecoff_sym_fns);
}

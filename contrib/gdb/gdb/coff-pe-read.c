/* Read the export table symbols from a portable executable and
   convert to internal format, for GDB. Used as a last resort if no
   debugging symbols recognized.

   Copyright 2003 Free Software Foundation, Inc.

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
   Boston, MA 02111-1307, USA.

   Contributed by Raoul M. Gough (RaoulGough@yahoo.co.uk). */

#include "coff-pe-read.h"

#include "bfd.h"

#include "defs.h"
#include "gdbtypes.h"

#include "symtab.h"
#include "symfile.h"
#include "objfiles.h"

/* Internal section information */

struct read_pe_section_data
{
  CORE_ADDR vma_offset;		/* Offset to loaded address of section. */
  unsigned long rva_start;	/* Start offset within the pe. */
  unsigned long rva_end;	/* End offset within the pe. */
  enum minimal_symbol_type ms_type;	/* Type to assign symbols in section. */
};

#define PE_SECTION_INDEX_TEXT     0
#define PE_SECTION_INDEX_DATA     1
#define PE_SECTION_INDEX_BSS      2
#define PE_SECTION_TABLE_SIZE     3
#define PE_SECTION_INDEX_INVALID -1

/* Get the index of the named section in our own array, which contains
   text, data and bss in that order. Return PE_SECTION_INDEX_INVALID
   if passed an unrecognised section name. */

static int
read_pe_section_index (const char *section_name)
{
  if (strcmp (section_name, ".text") == 0)
    {
      return PE_SECTION_INDEX_TEXT;
    }

  else if (strcmp (section_name, ".data") == 0)
    {
      return PE_SECTION_INDEX_DATA;
    }

  else if (strcmp (section_name, ".bss") == 0)
    {
      return PE_SECTION_INDEX_BSS;
    }

  else
    {
      return PE_SECTION_INDEX_INVALID;
    }
}

/* Record the virtual memory address of a section. */

static void
get_section_vmas (bfd *abfd, asection *sectp, void *context)
{
  struct read_pe_section_data *sections = context;
  int sectix = read_pe_section_index (sectp->name);

  if (sectix != PE_SECTION_INDEX_INVALID)
    {
      /* Data within the section start at rva_start in the pe and at
         bfd_get_section_vma() within memory. Store the offset. */

      sections[sectix].vma_offset
	= bfd_get_section_vma (abfd, sectp) - sections[sectix].rva_start;
    }
}

/* Create a minimal symbol entry for an exported symbol. */

static void
add_pe_exported_sym (char *sym_name,
		     unsigned long func_rva,
		     const struct read_pe_section_data *section_data,
		     const char *dll_name, struct objfile *objfile)
{
  /* Add the stored offset to get the loaded address of the symbol. */

  CORE_ADDR vma = func_rva + section_data->vma_offset;

  char *qualified_name = 0;
  int dll_name_len = strlen (dll_name);
  int count;

  /* Generate a (hopefully unique) qualified name using the first part
     of the dll name, e.g. KERNEL32!AddAtomA. This matches the style
     used by windbg from the "Microsoft Debugging Tools for Windows". */

  qualified_name = xmalloc (dll_name_len + strlen (sym_name) + 2);

  strncpy (qualified_name, dll_name, dll_name_len);
  qualified_name[dll_name_len] = '!';
  strcpy (qualified_name + dll_name_len + 1, sym_name);

  prim_record_minimal_symbol (qualified_name,
			      vma, section_data->ms_type, objfile);

  xfree (qualified_name);

  /* Enter the plain name as well, which might not be unique. */
  prim_record_minimal_symbol (sym_name, vma, section_data->ms_type, objfile);
}

/* Truncate a dll_name at the first dot character. */

static void
read_pe_truncate_name (char *dll_name)
{
  while (*dll_name)
    {
      if ((*dll_name) == '.')
	{
	  *dll_name = '\0';	/* truncates and causes loop exit. */
	}

      else
	{
	  ++dll_name;
	}
    }
}

/* Low-level support functions, direct from the ld module pe-dll.c. */
static unsigned int
pe_get16 (bfd *abfd, int where)
{
  unsigned char b[2];

  bfd_seek (abfd, (file_ptr) where, SEEK_SET);
  bfd_bread (b, (bfd_size_type) 2, abfd);
  return b[0] + (b[1] << 8);
}

static unsigned int
pe_get32 (bfd *abfd, int where)
{
  unsigned char b[4];

  bfd_seek (abfd, (file_ptr) where, SEEK_SET);
  bfd_bread (b, (bfd_size_type) 4, abfd);
  return b[0] + (b[1] << 8) + (b[2] << 16) + (b[3] << 24);
}

static unsigned int
pe_as32 (void *ptr)
{
  unsigned char *b = ptr;

  return b[0] + (b[1] << 8) + (b[2] << 16) + (b[3] << 24);
}

/* Read the (non-debug) export symbol table from a portable
   executable. Code originally lifted from the ld function
   pe_implied_import_dll in pe-dll.c. */

void
read_pe_exported_syms (struct objfile *objfile)
{
  bfd *dll = objfile->obfd;
  unsigned long pe_header_offset, opthdr_ofs, num_entries, i;
  unsigned long export_rva, export_size, nsections, secptr, expptr;
  unsigned long exp_funcbase;
  unsigned char *expdata, *erva;
  unsigned long name_rvas, ordinals, nexp, ordbase;
  char *dll_name;

  /* Array elements are for text, data and bss in that order
     Initialization with start_rva > end_rva guarantees that
     unused sections won't be matched. */
  struct read_pe_section_data section_data[PE_SECTION_TABLE_SIZE]
    = { {0, 1, 0, mst_text},
  {0, 1, 0, mst_data},
  {0, 1, 0, mst_bss}
  };

  struct cleanup *back_to = 0;

  char const *target = bfd_get_target (objfile->obfd);

  if ((strcmp (target, "pe-i386") != 0) && (strcmp (target, "pei-i386") != 0))
    {
      /* This is not an i386 format file. Abort now, because the code
         is untested on anything else. *FIXME* test on further
         architectures and loosen or remove this test. */
      return;
    }

  /* Get pe_header, optional header and numbers of export entries.  */
  pe_header_offset = pe_get32 (dll, 0x3c);
  opthdr_ofs = pe_header_offset + 4 + 20;
  num_entries = pe_get32 (dll, opthdr_ofs + 92);

  if (num_entries < 1)		/* No exports.  */
    {
      return;
    }

  export_rva = pe_get32 (dll, opthdr_ofs + 96);
  export_size = pe_get32 (dll, opthdr_ofs + 100);
  nsections = pe_get16 (dll, pe_header_offset + 4 + 2);
  secptr = (pe_header_offset + 4 + 20 +
	    pe_get16 (dll, pe_header_offset + 4 + 16));
  expptr = 0;

  /* Get the rva and size of the export section.  */
  for (i = 0; i < nsections; i++)
    {
      char sname[8];
      unsigned long secptr1 = secptr + 40 * i;
      unsigned long vaddr = pe_get32 (dll, secptr1 + 12);
      unsigned long vsize = pe_get32 (dll, secptr1 + 16);
      unsigned long fptr = pe_get32 (dll, secptr1 + 20);

      bfd_seek (dll, (file_ptr) secptr1, SEEK_SET);
      bfd_bread (sname, (bfd_size_type) 8, dll);

      if (vaddr <= export_rva && vaddr + vsize > export_rva)
	{
	  expptr = fptr + (export_rva - vaddr);
	  if (export_rva + export_size > vaddr + vsize)
	    export_size = vsize - (export_rva - vaddr);
	  break;
	}
    }

  if (export_size == 0)
    {
      /* Empty export table. */
      return;
    }

  /* Scan sections and store the base and size of the relevant sections. */
  for (i = 0; i < nsections; i++)
    {
      unsigned long secptr1 = secptr + 40 * i;
      unsigned long vsize = pe_get32 (dll, secptr1 + 8);
      unsigned long vaddr = pe_get32 (dll, secptr1 + 12);
      unsigned long flags = pe_get32 (dll, secptr1 + 36);
      char sec_name[9];
      int sectix;

      sec_name[8] = '\0';
      bfd_seek (dll, (file_ptr) secptr1 + 0, SEEK_SET);
      bfd_bread (sec_name, (bfd_size_type) 8, dll);

      sectix = read_pe_section_index (sec_name);

      if (sectix != PE_SECTION_INDEX_INVALID)
	{
	  section_data[sectix].rva_start = vaddr;
	  section_data[sectix].rva_end = vaddr + vsize;
	}
    }

  expdata = (unsigned char *) xmalloc (export_size);
  back_to = make_cleanup (xfree, expdata);

  bfd_seek (dll, (file_ptr) expptr, SEEK_SET);
  bfd_bread (expdata, (bfd_size_type) export_size, dll);
  erva = expdata - export_rva;

  nexp = pe_as32 (expdata + 24);
  name_rvas = pe_as32 (expdata + 32);
  ordinals = pe_as32 (expdata + 36);
  ordbase = pe_as32 (expdata + 16);
  exp_funcbase = pe_as32 (expdata + 28);

  /* Use internal dll name instead of full pathname. */
  dll_name = pe_as32 (expdata + 12) + erva;

  bfd_map_over_sections (dll, get_section_vmas, section_data);

  /* Adjust the vma_offsets in case this PE got relocated. This
     assumes that *all* sections share the same relocation offset
     as the text section. */
  for (i = 0; i < PE_SECTION_TABLE_SIZE; i++)
    {
      section_data[i].vma_offset
	+= ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
    }

  printf_filtered ("Minimal symbols from %s...", dll_name);
  wrap_here ("");

  /* Truncate name at first dot. Should maybe also convert to all
     lower case for convenience on Windows. */
  read_pe_truncate_name (dll_name);

  /* Iterate through the list of symbols.  */
  for (i = 0; i < nexp; i++)
    {
      /* Pointer to the names vector.  */
      unsigned long name_rva = pe_as32 (erva + name_rvas + i * 4);

      /* Pointer to the function address vector.  */
      unsigned long func_rva = pe_as32 (erva + exp_funcbase + i * 4);

      /* Find this symbol's section in our own array. */
      int sectix = 0;

      for (sectix = 0; sectix < PE_SECTION_TABLE_SIZE; ++sectix)
	{
	  if ((func_rva >= section_data[sectix].rva_start)
	      && (func_rva < section_data[sectix].rva_end))
	    {
	      add_pe_exported_sym (erva + name_rva,
				   func_rva,
				   section_data + sectix, dll_name, objfile);
	      break;
	    }
	}
    }

  /* discard expdata. */
  do_cleanups (back_to);
}

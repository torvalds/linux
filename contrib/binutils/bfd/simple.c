/* simple.c -- BFD simple client routines
   Copyright 2002, 2003, 2004, 2005, 2007
   Free Software Foundation, Inc.
   Contributed by MontaVista Software, Inc.

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

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "bfdlink.h"

static bfd_boolean
simple_dummy_warning (struct bfd_link_info *link_info ATTRIBUTE_UNUSED,
		      const char *warning ATTRIBUTE_UNUSED,
		      const char *symbol ATTRIBUTE_UNUSED,
		      bfd *abfd ATTRIBUTE_UNUSED,
		      asection *section ATTRIBUTE_UNUSED,
		      bfd_vma address ATTRIBUTE_UNUSED)
{
  return TRUE;
}

static bfd_boolean
simple_dummy_undefined_symbol (struct bfd_link_info *link_info ATTRIBUTE_UNUSED,
			       const char *name ATTRIBUTE_UNUSED,
			       bfd *abfd ATTRIBUTE_UNUSED,
			       asection *section ATTRIBUTE_UNUSED,
			       bfd_vma address ATTRIBUTE_UNUSED,
			       bfd_boolean fatal ATTRIBUTE_UNUSED)
{
  return TRUE;
}

static bfd_boolean
simple_dummy_reloc_overflow (struct bfd_link_info *link_info ATTRIBUTE_UNUSED,
			     struct bfd_link_hash_entry *entry ATTRIBUTE_UNUSED,
			     const char *name ATTRIBUTE_UNUSED,
			     const char *reloc_name ATTRIBUTE_UNUSED,
			     bfd_vma addend ATTRIBUTE_UNUSED,
			     bfd *abfd ATTRIBUTE_UNUSED,
			     asection *section ATTRIBUTE_UNUSED,
			     bfd_vma address ATTRIBUTE_UNUSED)
{
  return TRUE;
}

static bfd_boolean
simple_dummy_reloc_dangerous (struct bfd_link_info *link_info ATTRIBUTE_UNUSED,
			      const char *message ATTRIBUTE_UNUSED,
			      bfd *abfd ATTRIBUTE_UNUSED,
			      asection *section ATTRIBUTE_UNUSED,
			      bfd_vma address ATTRIBUTE_UNUSED)
{
  return TRUE;
}

static bfd_boolean
simple_dummy_unattached_reloc (struct bfd_link_info *link_info ATTRIBUTE_UNUSED,
			       const char *name ATTRIBUTE_UNUSED,
			       bfd *abfd ATTRIBUTE_UNUSED,
			       asection *section ATTRIBUTE_UNUSED,
			       bfd_vma address ATTRIBUTE_UNUSED)
{
  return TRUE;
}

static bfd_boolean
simple_dummy_multiple_definition (struct bfd_link_info *link_info ATTRIBUTE_UNUSED,
				  const char *name ATTRIBUTE_UNUSED,
				  bfd *obfd ATTRIBUTE_UNUSED,
				  asection *osec ATTRIBUTE_UNUSED,
				  bfd_vma oval ATTRIBUTE_UNUSED,
				  bfd *nbfd ATTRIBUTE_UNUSED,
				  asection *nsec ATTRIBUTE_UNUSED,
				  bfd_vma nval ATTRIBUTE_UNUSED)
{
  return TRUE;
}

static void
simple_dummy_einfo (const char *fmt ATTRIBUTE_UNUSED, ...)
{
}

struct saved_output_info
{
  bfd_vma offset;
  asection *section;
};

static void
simple_save_output_info (bfd *abfd ATTRIBUTE_UNUSED,
			 asection *section,
			 void *ptr)
{
  struct saved_output_info *output_info = ptr;
  output_info[section->index].offset = section->output_offset;
  output_info[section->index].section = section->output_section;
  if ((section->flags & SEC_DEBUGGING) != 0
      || section->output_section == NULL)
    {
      section->output_offset = 0;
      section->output_section = section;
    }
}

static void
simple_restore_output_info (bfd *abfd ATTRIBUTE_UNUSED,
			    asection *section,
			    void *ptr)
{
  struct saved_output_info *output_info = ptr;
  section->output_offset = output_info[section->index].offset;
  section->output_section = output_info[section->index].section;
}

/*
FUNCTION
	bfd_simple_relocate_secton

SYNOPSIS
	bfd_byte *bfd_simple_get_relocated_section_contents
	  (bfd *abfd, asection *sec, bfd_byte *outbuf, asymbol **symbol_table);

DESCRIPTION
	Returns the relocated contents of section @var{sec}.  The symbols in
	@var{symbol_table} will be used, or the symbols from @var{abfd} if
	@var{symbol_table} is NULL.  The output offsets for debug sections will
	be temporarily reset to 0.  The result will be stored at @var{outbuf}
	or allocated with @code{bfd_malloc} if @var{outbuf} is @code{NULL}.

	Returns @code{NULL} on a fatal error; ignores errors applying
	particular relocations.
*/

bfd_byte *
bfd_simple_get_relocated_section_contents (bfd *abfd,
					   asection *sec,
					   bfd_byte *outbuf,
					   asymbol **symbol_table)
{
  struct bfd_link_info link_info;
  struct bfd_link_order link_order;
  struct bfd_link_callbacks callbacks;
  bfd_byte *contents, *data;
  int storage_needed;
  void *saved_offsets;

  if (! (sec->flags & SEC_RELOC))
    {
      bfd_size_type amt = sec->rawsize > sec->size ? sec->rawsize : sec->size;
      bfd_size_type size = sec->rawsize ? sec->rawsize : sec->size;

      if (outbuf == NULL)
	contents = bfd_malloc (amt);
      else
	contents = outbuf;

      if (contents)
	bfd_get_section_contents (abfd, sec, contents, 0, size);

      return contents;
    }

  /* In order to use bfd_get_relocated_section_contents, we need
     to forge some data structures that it expects.  */

  /* Fill in the bare minimum number of fields for our purposes.  */
  memset (&link_info, 0, sizeof (link_info));
  link_info.input_bfds = abfd;
  link_info.input_bfds_tail = &abfd->link_next;

  link_info.hash = _bfd_generic_link_hash_table_create (abfd);
  link_info.callbacks = &callbacks;
  callbacks.warning = simple_dummy_warning;
  callbacks.undefined_symbol = simple_dummy_undefined_symbol;
  callbacks.reloc_overflow = simple_dummy_reloc_overflow;
  callbacks.reloc_dangerous = simple_dummy_reloc_dangerous;
  callbacks.unattached_reloc = simple_dummy_unattached_reloc;
  callbacks.multiple_definition = simple_dummy_multiple_definition;
  callbacks.einfo = simple_dummy_einfo;

  memset (&link_order, 0, sizeof (link_order));
  link_order.next = NULL;
  link_order.type = bfd_indirect_link_order;
  link_order.offset = 0;
  link_order.size = sec->size;
  link_order.u.indirect.section = sec;

  data = NULL;
  if (outbuf == NULL)
    {
      data = bfd_malloc (sec->size);
      if (data == NULL)
	return NULL;
      outbuf = data;
    }

  /* The sections in ABFD may already have output sections and offsets set.
     Because this function is primarily for debug sections, and GCC uses the
     knowledge that debug sections will generally have VMA 0 when emitting
     relocations between DWARF-2 sections (which are supposed to be
     section-relative offsets anyway), we need to reset the output offsets
     to zero.  We also need to arrange for section->output_section->vma plus
     section->output_offset to equal section->vma, which we do by setting
     section->output_section to point back to section.  Save the original
     output offset and output section to restore later.  */
  saved_offsets = malloc (sizeof (struct saved_output_info)
			  * abfd->section_count);
  if (saved_offsets == NULL)
    {
      if (data)
	free (data);
      return NULL;
    }
  bfd_map_over_sections (abfd, simple_save_output_info, saved_offsets);

  if (symbol_table == NULL)
    {
      _bfd_generic_link_add_symbols (abfd, &link_info);

      storage_needed = bfd_get_symtab_upper_bound (abfd);
      symbol_table = bfd_malloc (storage_needed);
      bfd_canonicalize_symtab (abfd, symbol_table);
    }
  else
    storage_needed = 0;

  contents = bfd_get_relocated_section_contents (abfd,
						 &link_info,
						 &link_order,
						 outbuf,
						 0,
						 symbol_table);
  if (contents == NULL && data != NULL)
    free (data);

  bfd_map_over_sections (abfd, simple_restore_output_info, saved_offsets);
  free (saved_offsets);

  _bfd_generic_link_hash_table_free (link_info.hash);
  return contents;
}

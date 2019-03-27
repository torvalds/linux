/* MIPS-specific support for 64-bit ELF
   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2006, 2007
   Free Software Foundation, Inc.
   Ian Lance Taylor, Cygnus Support
   Linker support added by Mark Mitchell, CodeSourcery, LLC.
   <mark@codesourcery.com>

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

/* This file supports the 64-bit (MIPS) ELF archives.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "aout/ar.h"

/* Irix 6 defines a 64bit archive map format, so that they can
   have archives more than 4 GB in size.  */

bfd_boolean bfd_elf64_archive_slurp_armap (bfd *);
bfd_boolean bfd_elf64_archive_write_armap
  (bfd *, unsigned int, struct orl *, unsigned int, int);

/* Read an Irix 6 armap.  */

bfd_boolean
bfd_elf64_archive_slurp_armap (bfd *abfd)
{
  struct artdata *ardata = bfd_ardata (abfd);
  char nextname[17];
  file_ptr arhdrpos;
  bfd_size_type i, parsed_size, nsymz, stringsize, carsym_size, ptrsize;
  struct areltdata *mapdata;
  bfd_byte int_buf[8];
  char *stringbase;
  bfd_byte *raw_armap = NULL;
  carsym *carsyms;
  bfd_size_type amt;

  ardata->symdefs = NULL;

  /* Get the name of the first element.  */
  arhdrpos = bfd_tell (abfd);
  i = bfd_bread (nextname, 16, abfd);
  if (i == 0)
    return TRUE;
  if (i != 16)
    return FALSE;

  if (bfd_seek (abfd, (file_ptr) - 16, SEEK_CUR) != 0)
    return FALSE;

  /* Archives with traditional armaps are still permitted.  */
  if (CONST_STRNEQ (nextname, "/               "))
    return bfd_slurp_armap (abfd);

  if (! CONST_STRNEQ (nextname, "/SYM64/         "))
    {
      bfd_has_map (abfd) = FALSE;
      return TRUE;
    }

  mapdata = (struct areltdata *) _bfd_read_ar_hdr (abfd);
  if (mapdata == NULL)
    return FALSE;
  parsed_size = mapdata->parsed_size;
  bfd_release (abfd, mapdata);

  if (bfd_bread (int_buf, 8, abfd) != 8)
    {
      if (bfd_get_error () != bfd_error_system_call)
	bfd_set_error (bfd_error_malformed_archive);
      return FALSE;
    }

  nsymz = bfd_getb64 (int_buf);
  stringsize = parsed_size - 8 * nsymz - 8;

  carsym_size = nsymz * sizeof (carsym);
  ptrsize = 8 * nsymz;

  amt = carsym_size + stringsize + 1;
  ardata->symdefs = bfd_zalloc (abfd, amt);
  if (ardata->symdefs == NULL)
    return FALSE;
  carsyms = ardata->symdefs;
  stringbase = ((char *) ardata->symdefs) + carsym_size;

  raw_armap = bfd_alloc (abfd, ptrsize);
  if (raw_armap == NULL)
    goto release_symdefs;

  if (bfd_bread (raw_armap, ptrsize, abfd) != ptrsize
      || bfd_bread (stringbase, stringsize, abfd) != stringsize)
    {
      if (bfd_get_error () != bfd_error_system_call)
	bfd_set_error (bfd_error_malformed_archive);
      goto release_raw_armap;
    }

  for (i = 0; i < nsymz; i++)
    {
      carsyms->file_offset = bfd_getb64 (raw_armap + i * 8);
      carsyms->name = stringbase;
      stringbase += strlen (stringbase) + 1;
      ++carsyms;
    }
  *stringbase = '\0';

  ardata->symdef_count = nsymz;
  ardata->first_file_filepos = bfd_tell (abfd);
  /* Pad to an even boundary if you have to.  */
  ardata->first_file_filepos += (ardata->first_file_filepos) % 2;

  bfd_has_map (abfd) = TRUE;
  bfd_release (abfd, raw_armap);

  return TRUE;

release_raw_armap:
  bfd_release (abfd, raw_armap);
release_symdefs:
  bfd_release (abfd, ardata->symdefs);
  return FALSE;
}

/* Write out an Irix 6 armap.  The Irix 6 tools are supposed to be
   able to handle ordinary ELF armaps, but at least on Irix 6.2 the
   linker crashes.  */

bfd_boolean
bfd_elf64_archive_write_armap (bfd *arch,
			       unsigned int elength,
			       struct orl *map,
			       unsigned int symbol_count,
			       int stridx)
{
  unsigned int ranlibsize = (symbol_count * 8) + 8;
  unsigned int stringsize = stridx;
  unsigned int mapsize = stringsize + ranlibsize;
  file_ptr archive_member_file_ptr;
  bfd *current = arch->archive_head;
  unsigned int count;
  struct ar_hdr hdr;
  int padding;
  bfd_byte buf[8];

  padding = BFD_ALIGN (mapsize, 8) - mapsize;
  mapsize += padding;

  /* work out where the first object file will go in the archive */
  archive_member_file_ptr = (mapsize
			     + elength
			     + sizeof (struct ar_hdr)
			     + SARMAG);

  memset (&hdr, ' ', sizeof (struct ar_hdr));
  memcpy (hdr.ar_name, "/SYM64/", strlen ("/SYM64/"));
  _bfd_ar_spacepad (hdr.ar_size, sizeof (hdr.ar_size), "%-10ld",
                    mapsize);
  _bfd_ar_spacepad (hdr.ar_date, sizeof (hdr.ar_date), "%ld",
                    time (NULL));
  /* This, at least, is what Intel coff sets the values to.: */
  _bfd_ar_spacepad (hdr.ar_uid, sizeof (hdr.ar_uid), "%ld", 0);
  _bfd_ar_spacepad (hdr.ar_gid, sizeof (hdr.ar_gid), "%ld", 0);
  _bfd_ar_spacepad (hdr.ar_mode, sizeof (hdr.ar_mode), "%-7lo", 0);
  memcpy (hdr.ar_fmag, ARFMAG, 2);

  /* Write the ar header for this item and the number of symbols */

  if (bfd_bwrite (&hdr, sizeof (struct ar_hdr), arch)
      != sizeof (struct ar_hdr))
    return FALSE;

  bfd_putb64 ((bfd_vma) symbol_count, buf);
  if (bfd_bwrite (buf, 8, arch) != 8)
    return FALSE;

  /* Two passes, first write the file offsets for each symbol -
     remembering that each offset is on a two byte boundary.  */

  /* Write out the file offset for the file associated with each
     symbol, and remember to keep the offsets padded out.  */

  current = arch->archive_head;
  count = 0;
  while (current != NULL && count < symbol_count)
    {
      /* For each symbol which is used defined in this object, write out
	 the object file's address in the archive */

      while (map[count].u.abfd == current)
	{
	  bfd_putb64 ((bfd_vma) archive_member_file_ptr, buf);
	  if (bfd_bwrite (buf, 8, arch) != 8)
	    return FALSE;
	  count++;
	}
      /* Add size of this archive entry */
      archive_member_file_ptr += (arelt_size (current)
				  + sizeof (struct ar_hdr));
      /* remember about the even alignment */
      archive_member_file_ptr += archive_member_file_ptr % 2;
      current = current->archive_next;
    }

  /* now write the strings themselves */
  for (count = 0; count < symbol_count; count++)
    {
      size_t len = strlen (*map[count].name) + 1;

      if (bfd_bwrite (*map[count].name, len, arch) != len)
	return FALSE;
    }

  /* The spec says that this should be padded to an 8 byte boundary.
     However, the Irix 6.2 tools do not appear to do this.  */
  while (padding != 0)
    {
      if (bfd_bwrite ("", 1, arch) != 1)
	return FALSE;
      --padding;
    }

  return TRUE;
}

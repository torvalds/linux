/* BFD back-end for HP/Intel IA-64 COFF files.
   Copyright 1999, 2000, 2001, 2002, 2007 Free Software Foundation, Inc.
   Contributed by David Mosberger <davidm@hpl.hp.com>

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
#include "coff/ia64.h"
#include "coff/internal.h"
#include "coff/pe.h"
#include "libcoff.h"

#define COFF_DEFAULT_SECTION_ALIGNMENT_POWER (2)
/* The page size is a guess based on ELF.  */

#define COFF_PAGE_SIZE 0x1000

static reloc_howto_type howto_table[] =
{
  EMPTY_HOWTO (0),
};

#define BADMAG(x) IA64BADMAG(x)
#define IA64 1			/* Customize coffcode.h */

#ifdef COFF_WITH_pep
# undef AOUTSZ
# define AOUTSZ		PEPAOUTSZ
# define PEAOUTHDR	PEPAOUTHDR
#endif

#define RTYPE2HOWTO(cache_ptr, dst) \
	    (cache_ptr)->howto = howto_table + (dst)->r_type;

#ifdef COFF_WITH_PE
/* Return TRUE if this relocation should
   appear in the output .reloc section.  */

static bfd_boolean in_reloc_p PARAMS ((bfd *, reloc_howto_type *));

static bfd_boolean
in_reloc_p(abfd, howto)
     bfd * abfd ATTRIBUTE_UNUSED;
     reloc_howto_type *howto ATTRIBUTE_UNUSED;
{
  return FALSE;			/* We don't do relocs for now...  */
}
#endif

#include "coffcode.h"

static const bfd_target *ia64coff_object_p PARAMS ((bfd *));

static const bfd_target *
ia64coff_object_p (abfd)
     bfd *abfd;
{
#ifdef COFF_IMAGE_WITH_PE
  {
    struct external_PEI_DOS_hdr dos_hdr;
    struct external_PEI_IMAGE_hdr image_hdr;
    file_ptr offset;

    if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0
	|| (bfd_bread (&dos_hdr, (bfd_size_type) sizeof (dos_hdr), abfd)
	    != sizeof (dos_hdr)))
      {
	if (bfd_get_error () != bfd_error_system_call)
	  bfd_set_error (bfd_error_wrong_format);
	return NULL;
      }

    /* There are really two magic numbers involved; the magic number
       that says this is a NT executable (PEI) and the magic number
       that determines the architecture.  The former is DOSMAGIC,
       stored in the e_magic field.  The latter is stored in the
       f_magic field.  If the NT magic number isn't valid, the
       architecture magic number could be mimicked by some other
       field (specifically, the number of relocs in section 3).  Since
       this routine can only be called correctly for a PEI file, check
       the e_magic number here, and, if it doesn't match, clobber the
       f_magic number so that we don't get a false match.  */
    if (H_GET_16 (abfd, dos_hdr.e_magic) != DOSMAGIC)
      {
	bfd_set_error (bfd_error_wrong_format);
	return NULL;
      }

    offset = H_GET_32 (abfd, dos_hdr.e_lfanew);
    if (bfd_seek (abfd, offset, SEEK_SET) != 0
	|| (bfd_bread (&image_hdr, (bfd_size_type) sizeof (image_hdr), abfd)
	    != sizeof (image_hdr)))
      {
	if (bfd_get_error () != bfd_error_system_call)
	  bfd_set_error (bfd_error_wrong_format);
	return NULL;
      }

    if (H_GET_32 (abfd, image_hdr.nt_signature)
	!= 0x4550)
      {
	bfd_set_error (bfd_error_wrong_format);
	return NULL;
      }

    /* Here is the hack.  coff_object_p wants to read filhsz bytes to
       pick up the COFF header for PE, see "struct external_PEI_filehdr"
       in include/coff/pe.h.  We adjust so that that will work. */
    if (bfd_seek (abfd, offset - sizeof (dos_hdr), SEEK_SET) != 0)
      {
	if (bfd_get_error () != bfd_error_system_call)
	  bfd_set_error (bfd_error_wrong_format);
	return NULL;
      }
  }
#endif

  return coff_object_p (abfd);
}

const bfd_target
#ifdef TARGET_SYM
  TARGET_SYM =
#else
  ia64coff_vec =
#endif
{
#ifdef TARGET_NAME
  TARGET_NAME,
#else
  "coff-ia64",			/* name */
#endif
  bfd_target_coff_flavour,
  BFD_ENDIAN_LITTLE,		/* data byte order is little */
  BFD_ENDIAN_LITTLE,		/* header byte order is little */

  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),

#ifndef COFF_WITH_PE
  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC /* section flags */
   | SEC_CODE | SEC_DATA),
#else
  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC /* section flags */
   | SEC_CODE | SEC_DATA
   | SEC_LINK_ONCE | SEC_LINK_DUPLICATES),
#endif

#ifdef TARGET_UNDERSCORE
  TARGET_UNDERSCORE,		/* leading underscore */
#else
  0,				/* leading underscore */
#endif
  '/',				/* ar_pad_char */
  15,				/* ar_max_namelen */

  bfd_getl64, bfd_getl_signed_64, bfd_putl64,
     bfd_getl32, bfd_getl_signed_32, bfd_putl32,
     bfd_getl16, bfd_getl_signed_16, bfd_putl16, /* data */
  bfd_getl64, bfd_getl_signed_64, bfd_putl64,
     bfd_getl32, bfd_getl_signed_32, bfd_putl32,
     bfd_getl16, bfd_getl_signed_16, bfd_putl16, /* hdrs */

/* Note that we allow an object file to be treated as a core file as well.  */
    {_bfd_dummy_target, ia64coff_object_p, /* bfd_check_format */
       bfd_generic_archive_p, ia64coff_object_p},
    {bfd_false, coff_mkobject, _bfd_generic_mkarchive, /* bfd_set_format */
       bfd_false},
    {bfd_false, coff_write_object_contents, /* bfd_write_contents */
       _bfd_write_archive_contents, bfd_false},

     BFD_JUMP_TABLE_GENERIC (coff),
     BFD_JUMP_TABLE_COPY (coff),
     BFD_JUMP_TABLE_CORE (_bfd_nocore),
     BFD_JUMP_TABLE_ARCHIVE (_bfd_archive_coff),
     BFD_JUMP_TABLE_SYMBOLS (coff),
     BFD_JUMP_TABLE_RELOCS (coff),
     BFD_JUMP_TABLE_WRITE (coff),
     BFD_JUMP_TABLE_LINK (coff),
     BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  NULL,

  COFF_SWAP_TABLE
};

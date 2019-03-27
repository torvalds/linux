/* BFD back-end definitions used by all NetBSD targets.
   Copyright 1990, 1991, 1992, 1994, 1995, 1996, 1997, 1998, 2000, 2002,
   2005, 2007 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301,
   USA.  */

/* Check for our machine type (part of magic number).  */
#ifndef MACHTYPE_OK
#define MACHTYPE_OK(m) ((m) == DEFAULT_MID || (m) == M_UNKNOWN)
#endif

/* This is the normal load address for executables.  */
#define TEXT_START_ADDR		TARGET_PAGE_SIZE

/* NetBSD ZMAGIC has its header in the text segment.  */
#define N_HEADER_IN_TEXT(x)	1

/* Determine if this is a shared library using the flags.  */
#define N_SHARED_LIB(x) 	(N_DYNAMIC (x))

/* We have 6 bits of flags and 10 bits of machine ID.  */
#define N_MACHTYPE(exec) \
	((enum machine_type) (((exec).a_info >> 16) & 0x03ff))
#define N_FLAGS(exec) \
	(((exec).a_info >> 26) & 0x3f)

#define N_SET_INFO(exec, magic, type, flags) \
	((exec).a_info = ((magic) & 0xffff) \
	 | (((int) (type) & 0x3ff) << 16) \
	 | (((flags) & 0x3f) << 24))
#define N_SET_MACHTYPE(exec, machtype) \
	((exec).a_info = \
         ((exec).a_info & 0xfb00ffff) | ((((int) (machtype)) & 0x3ff) << 16))
#define N_SET_FLAGS(exec, flags) \
	((exec).a_info = \
	 ((exec).a_info & 0x03ffffff) | ((flags & 0x03f) << 26))

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "libaout.h"

/* On NetBSD, the magic number is always in ntohl's "network" (big-endian)
   format.  */
#define SWAP_MAGIC(ext) bfd_getb32 (ext)

/* On NetBSD, the entry point may be taken to be the start of the text
   section.  */
#define MY_entry_is_text_address 1

#define MY_write_object_contents MY (write_object_contents)
static bfd_boolean MY (write_object_contents) (bfd *);

#define MY_text_includes_header 1

#include "aout-target.h"

/* Write an object file.
   Section contents have already been written.  We write the
   file header, symbols, and relocation.  */

static bfd_boolean
MY (write_object_contents) (bfd *abfd)
{
  struct external_exec exec_bytes;
  struct internal_exec *execp = exec_hdr (abfd);

  /* We must make certain that the magic number has been set.  This
     will normally have been done by set_section_contents, but only if
     there actually are some section contents.  */
  if (! abfd->output_has_begun)
    {
      bfd_size_type text_size;
      file_ptr text_end;

      NAME (aout, adjust_sizes_and_vmas) (abfd, & text_size, & text_end);
    }

  obj_reloc_entry_size (abfd) = RELOC_STD_SIZE;

  /* Magic number, maestro, please!  */
  switch (bfd_get_arch(abfd))
    {
    case DEFAULT_ARCH:
      N_SET_MACHTYPE(*execp, DEFAULT_MID);
      break;
    default:
      N_SET_MACHTYPE(*execp, M_UNKNOWN);
      break;
    }

  /* The NetBSD magic number is always big-endian */
#ifndef TARGET_IS_BIG_ENDIAN_P
  /* XXX aren't there any macro to change byteorder of a word independent of
     the host's or target's endianesses?  */
  execp->a_info
    = (execp->a_info & 0xff) << 24 | (execp->a_info & 0xff00) << 8
      | (execp->a_info & 0xff0000) >> 8 | (execp->a_info & 0xff000000) >> 24;
#endif

  WRITE_HEADERS (abfd, execp);

  return TRUE;
}

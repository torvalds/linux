/* BFD back-end definitions used by all FreeBSD targets.
   Copyright 1990, 1991, 1992, 1996, 1997, 2000, 2001, 2002, 2005, 2007
   Free Software Foundation, Inc.

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

/* $FreeBSD$ */

/* FreeBSD QMAGIC files have the header in the text. */
#define	N_HEADER_IN_TEXT(x)	1
#define MY_text_includes_header 1

#define TEXT_START_ADDR		(TARGET_PAGE_SIZE + 0x20)

/*
 * FreeBSD uses a weird mix of byte orderings for its a_info field.
 * Its assembler emits NetBSD style object files, with a big-endian
 * a_info.  Its linker seems to accept either byte ordering, but
 * emits a little-endian a_info.
 *
 * Here, we accept either byte ordering, but always produce
 * little-endian.
 *
 * FIXME - Probably we should always produce the _native_ byte
 * ordering.  I.e., it should be in the architecture-specific
 * file, not here.  But in reality, there is no chance
 * that FreeBSD will ever use a.out in a new port.
 */

#define N_MACHTYPE(exec) \
	((enum machine_type) \
	 ((freebsd_swap_magic(&(exec).a_info) >> 16) & 0x3ff))
#define N_FLAGS(exec) \
	((enum machine_type) \
	 ((freebsd_swap_magic(&(exec).a_info) >> 26) & 0x3f))

#define N_SET_INFO(exec, magic, type, flags) \
	((exec).a_info = ((magic) & 0xffff) \
	 | (((int)(type) & 0x3ff) << 16) \
	 | (((flags) & 0x3f) << 26))
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

#define SWAP_MAGIC(ext)			(freebsd_swap_magic(ext))

#define MY_bfd_final_link MY (bfd_final_link)
#define MY_write_object_contents MY (write_object_contents)
static bfd_boolean MY (bfd_final_link) (bfd *, struct bfd_link_info *);
static bfd_boolean MY (write_object_contents) (bfd *);
static long freebsd_swap_magic (void *);

#include "aout-target.h"

static bfd_boolean
MY (bfd_final_link) (bfd *abfd, struct bfd_link_info *info)
{
  obj_aout_subformat (abfd) = q_magic_format;
  return NAME(aout,final_link) (abfd, info, MY_final_link_callback);
}

/* Swap a magic number.  We accept either endian, whichever looks valid. */

static long
freebsd_swap_magic (void *ext)
{
  long linfo = bfd_getl32(ext);
  long binfo = bfd_getb32(ext);
  int lmagic = linfo & 0xffff;
  int bmagic = binfo & 0xffff;
  int lmagic_ok = lmagic == OMAGIC || lmagic == NMAGIC ||
    lmagic == ZMAGIC || lmagic == QMAGIC;
  int bmagic_ok = bmagic == OMAGIC || bmagic == NMAGIC ||
    bmagic == ZMAGIC || bmagic == QMAGIC;

  return bmagic_ok && !lmagic_ok ? binfo : linfo;
}

/* Write an object file.
   Section contents have already been written.  We write the
   file header, symbols, and relocation.  */

static bfd_boolean
MY (write_object_contents) (bfd *abfd)
{
  struct external_exec exec_bytes;
  struct internal_exec *execp = exec_hdr (abfd);

  obj_reloc_entry_size (abfd) = RELOC_STD_SIZE;

  /* Magic number, maestro, please!  */
  switch (bfd_get_arch(abfd))
    {
    case bfd_arch_m68k:
      if (strcmp (abfd->xvec->name, "a.out-m68k4k-netbsd") == 0)
	N_SET_MACHTYPE (*execp, M_68K4K_NETBSD);
      else
	N_SET_MACHTYPE (*execp, M_68K_NETBSD);
      break;
    case bfd_arch_sparc:
      N_SET_MACHTYPE (*execp, M_SPARC_NETBSD);
      break;
    case bfd_arch_i386:
      N_SET_MACHTYPE (*execp, M_386_NETBSD);
      break;
    case bfd_arch_ns32k:
      N_SET_MACHTYPE (*execp, M_532_NETBSD);
      break;
    default:
      N_SET_MACHTYPE (*execp, M_UNKNOWN);
      break;
    }

  WRITE_HEADERS(abfd, execp);

  return TRUE;
}

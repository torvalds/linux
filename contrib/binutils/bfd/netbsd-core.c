/* BFD back end for NetBSD style core files
   Copyright 1988, 1989, 1991, 1992, 1993, 1996, 1998, 1999, 2000, 2001,
   2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Written by Paul Kranenburg, EUR

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
#include "libaout.h"           /* BFD a.out internal data structures.  */

#include <sys/param.h>
#include <sys/dir.h>
#include <signal.h>
#include <sys/core.h>

/* The machine ID for OpenBSD/sparc64 and older versions of
   NetBSD/sparc64 overlaps with M_MIPS1.  */
#define M_SPARC64_OPENBSD	M_MIPS1

/* Offset of StackGhost cookie within `struct md_coredump' on
   OpenBSD/sparc.  */
#define SPARC_WCOOKIE_OFFSET	344

/* Offset of StackGhost cookie within `struct md_coredump' on
   OpenBSD/sparc64.  */
#define SPARC64_WCOOKIE_OFFSET	832

#define netbsd_core_file_matches_executable_p generic_core_file_matches_executable_p

struct netbsd_core_struct
{
  struct core core;
} *rawptr;

/* Handle NetBSD-style core dump file.  */

static const bfd_target *
netbsd_core_file_p (bfd *abfd)
{
  int val;
  unsigned i;
  file_ptr offset;
  asection *asect;
  struct core core;
  struct coreseg coreseg;
  bfd_size_type amt = sizeof core;

  val = bfd_bread (&core, amt, abfd);
  if (val != sizeof core)
    {
      /* Too small to be a core file.  */
      bfd_set_error (bfd_error_wrong_format);
      return 0;
    }

  if (CORE_GETMAGIC (core) != COREMAGIC)
    {
      bfd_set_error (bfd_error_wrong_format);
      return 0;
    }

  amt = sizeof (struct netbsd_core_struct);
  rawptr = (struct netbsd_core_struct *) bfd_zalloc (abfd, amt);
  if (rawptr == NULL)
    return 0;

  rawptr->core = core;
  abfd->tdata.netbsd_core_data = rawptr;

  offset = core.c_hdrsize;
  for (i = 0; i < core.c_nseg; i++)
    {
      const char *sname;
      flagword flags;

      if (bfd_seek (abfd, offset, SEEK_SET) != 0)
	goto punt;

      val = bfd_bread (&coreseg, sizeof coreseg, abfd);
      if (val != sizeof coreseg)
	{
	  bfd_set_error (bfd_error_file_truncated);
	  goto punt;
	}
      if (CORE_GETMAGIC (coreseg) != CORESEGMAGIC)
	{
	  bfd_set_error (bfd_error_wrong_format);
	  goto punt;
	}

      offset += core.c_seghdrsize;

      switch (CORE_GETFLAG (coreseg))
	{
	case CORE_CPU:
	  sname = ".reg";
	  flags = SEC_ALLOC + SEC_HAS_CONTENTS;
	  break;
	case CORE_DATA:
	  sname = ".data";
	  flags = SEC_ALLOC + SEC_LOAD + SEC_HAS_CONTENTS;
	  break;
	case CORE_STACK:
	  sname = ".stack";
	  flags = SEC_ALLOC + SEC_LOAD + SEC_HAS_CONTENTS;
	  break;
	default:
	  sname = ".unknown";
	  flags = SEC_ALLOC + SEC_HAS_CONTENTS;
	  break;
	}
      asect = bfd_make_section_anyway_with_flags (abfd, sname, flags);
      if (asect == NULL)
	goto punt;

      asect->size = coreseg.c_size;
      asect->vma = coreseg.c_addr;
      asect->filepos = offset;
      asect->alignment_power = 2;

      if (CORE_GETFLAG (coreseg) == CORE_CPU)
	{
	  bfd_size_type wcookie_offset;

	  switch (CORE_GETMID (core))
	    {
	    case M_SPARC_NETBSD:
	      wcookie_offset = SPARC_WCOOKIE_OFFSET;
	      break;
	    case M_SPARC64_OPENBSD:
	      wcookie_offset = SPARC64_WCOOKIE_OFFSET;
	      break;
	    default:
	      wcookie_offset = 0;
	      break;
	    }

	  if (wcookie_offset > 0 && coreseg.c_size > wcookie_offset)
	    {
	      /* Truncate the .reg section.  */
	      asect->size = wcookie_offset;

	      /* And create the .wcookie section.  */
	      flags = SEC_ALLOC + SEC_HAS_CONTENTS;
	      asect = bfd_make_section_anyway_with_flags (abfd, ".wcookie",
							  flags);
	      if (asect == NULL)
		goto punt;

	      asect->size = coreseg.c_size - wcookie_offset;
	      asect->vma = 0;
	      asect->filepos = offset + wcookie_offset;
	      asect->alignment_power = 2;
	    }
	}

      offset += coreseg.c_size;
    }

  /* Set architecture from machine ID.  */
  switch (CORE_GETMID (core))
    {
    case M_ALPHA_NETBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_alpha, 0);
      break;

    case M_ARM6_NETBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_arm, bfd_mach_arm_3);
      break;

    case M_X86_64_NETBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_i386, bfd_mach_x86_64);
      break;

    case M_386_NETBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_i386, bfd_mach_i386_i386);
      break;

    case M_68K_NETBSD:
    case M_68K4K_NETBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_m68k, 0);
      break;

    case M_88K_OPENBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_m88k, 0);
      break;

    case M_HPPA_OPENBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_hppa, bfd_mach_hppa11);
      break;

    case M_POWERPC_NETBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_powerpc, bfd_mach_ppc);
      break;

    case M_SPARC_NETBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_sparc, bfd_mach_sparc);
      break;

    case M_SPARC64_NETBSD:
    case M_SPARC64_OPENBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_sparc, bfd_mach_sparc_v9);
      break;

    case M_VAX_NETBSD:
    case M_VAX4K_NETBSD:
      bfd_default_set_arch_mach (abfd, bfd_arch_vax, 0);
      break;
    }

  /* OK, we believe you.  You're a core file (sure, sure).  */
  return abfd->xvec;

 punt:
  bfd_release (abfd, abfd->tdata.any);
  abfd->tdata.any = NULL;
  bfd_section_list_clear (abfd);
  return 0;
}

static char*
netbsd_core_file_failing_command (bfd *abfd)
{
  /*return core_command (abfd);*/
  return abfd->tdata.netbsd_core_data->core.c_name;
}

static int
netbsd_core_file_failing_signal (bfd *abfd)
{
  /*return core_signal (abfd);*/
  return abfd->tdata.netbsd_core_data->core.c_signo;
}

/* If somebody calls any byte-swapping routines, shoot them.  */

static void
swap_abort (void)
{
 /* This way doesn't require any declaration for ANSI to fuck up.  */
  abort ();
}

#define	NO_GET ((bfd_vma (*) (const void *)) swap_abort)
#define	NO_PUT ((void (*) (bfd_vma, void *)) swap_abort)
#define	NO_GETS ((bfd_signed_vma (*) (const void *)) swap_abort)
#define	NO_GET64 ((bfd_uint64_t (*) (const void *)) swap_abort)
#define	NO_PUT64 ((void (*) (bfd_uint64_t, void *)) swap_abort)
#define	NO_GETS64 ((bfd_int64_t (*) (const void *)) swap_abort)

const bfd_target netbsd_core_vec =
  {
    "netbsd-core",
    bfd_target_unknown_flavour,
    BFD_ENDIAN_UNKNOWN,		/* Target byte order.  */
    BFD_ENDIAN_UNKNOWN,		/* Target headers byte order.  */
    (HAS_RELOC | EXEC_P |	/* Object flags.  */
     HAS_LINENO | HAS_DEBUG |
     HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),
    (SEC_HAS_CONTENTS |		/* Section flags.  */
     SEC_ALLOC | SEC_LOAD | SEC_RELOC),
    0,				/* Symbol prefix.  */
    ' ',			/* ar_pad_char.  */
    16,				/* ar_max_namelen.  */
    NO_GET64, NO_GETS64, NO_PUT64,	/* 64 bit data.  */
    NO_GET, NO_GETS, NO_PUT,		/* 32 bit data.  */
    NO_GET, NO_GETS, NO_PUT,		/* 16 bit data.  */
    NO_GET64, NO_GETS64, NO_PUT64,	/* 64 bit hdrs.  */
    NO_GET, NO_GETS, NO_PUT,		/* 32 bit hdrs.  */
    NO_GET, NO_GETS, NO_PUT,		/* 16 bit hdrs.  */

    {					/* bfd_check_format.  */
      _bfd_dummy_target,		/* Unknown format.  */
      _bfd_dummy_target,		/* Object file.  */
      _bfd_dummy_target,		/* Archive.  */
      netbsd_core_file_p		/* A core file.  */
    },
    {					/* bfd_set_format.  */
      bfd_false, bfd_false,
      bfd_false, bfd_false
    },
    {					/* bfd_write_contents.  */
      bfd_false, bfd_false,
      bfd_false, bfd_false
    },

    BFD_JUMP_TABLE_GENERIC (_bfd_generic),
    BFD_JUMP_TABLE_COPY (_bfd_generic),
    BFD_JUMP_TABLE_CORE (netbsd),
    BFD_JUMP_TABLE_ARCHIVE (_bfd_noarchive),
    BFD_JUMP_TABLE_SYMBOLS (_bfd_nosymbols),
    BFD_JUMP_TABLE_RELOCS (_bfd_norelocs),
    BFD_JUMP_TABLE_WRITE (_bfd_generic),
    BFD_JUMP_TABLE_LINK (_bfd_nolink),
    BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

    NULL,

    (PTR) 0			        /* Backend_data.  */
  };

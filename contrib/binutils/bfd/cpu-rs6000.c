/* BFD back-end for rs6000 support
   Copyright 1990, 1991, 1993, 1995, 2000, 2002, 2003, 2007
   Free Software Foundation, Inc.
   Written by Mimi Phuong-Thao Vo of IBM
   and John Gilmore of Cygnus Support.

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

/* The RS/6000 architecture is compatible with the PowerPC common
   architecture.  */

static const bfd_arch_info_type *rs6000_compatible
  PARAMS ((const bfd_arch_info_type *, const bfd_arch_info_type *));

static const bfd_arch_info_type *
rs6000_compatible (a,b)
     const bfd_arch_info_type *a;
     const bfd_arch_info_type *b;
{
  BFD_ASSERT (a->arch == bfd_arch_rs6000);
  switch (b->arch)
    {
    default:
      return NULL;
    case bfd_arch_rs6000:
      return bfd_default_compatible (a, b);
    case bfd_arch_powerpc:
      if (a->mach == bfd_mach_rs6k)
	return b;
      return NULL;
    }
  /*NOTREACHED*/
}

static const bfd_arch_info_type arch_info_struct[] =
{
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_rs6000,
    bfd_mach_rs6k_rs1,
    "rs6000",
    "rs6000:rs1",
    3,
    FALSE, /* not the default */
    rs6000_compatible,
    bfd_default_scan,
    &arch_info_struct[1]
  },
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_rs6000,
    bfd_mach_rs6k_rsc,
    "rs6000",
    "rs6000:rsc",
    3,
    FALSE, /* not the default */
    rs6000_compatible,
    bfd_default_scan,
    &arch_info_struct[2]
  },
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_rs6000,
    bfd_mach_rs6k_rs2,
    "rs6000",
    "rs6000:rs2",
    3,
    FALSE, /* not the default */
    rs6000_compatible,
    bfd_default_scan,
    0
  }
};

const bfd_arch_info_type bfd_rs6000_arch =
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_rs6000,
    bfd_mach_rs6k,	/* POWER common architecture */
    "rs6000",
    "rs6000:6000",
    3,
    TRUE, /* the default */
    rs6000_compatible,
    bfd_default_scan,
    &arch_info_struct[0]
  };

/* BFD PowerPC CPU definition
   Copyright 1994, 1995, 1996, 2000, 2001, 2002, 2003, 2007
   Free Software Foundation, Inc.
   Contributed by Ian Lance Taylor, Cygnus Support.

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

/* The common PowerPC architecture is compatible with the RS/6000.  */

static const bfd_arch_info_type *powerpc_compatible
  PARAMS ((const bfd_arch_info_type *, const bfd_arch_info_type *));

static const bfd_arch_info_type *
powerpc_compatible (a,b)
     const bfd_arch_info_type *a;
     const bfd_arch_info_type *b;
{
  BFD_ASSERT (a->arch == bfd_arch_powerpc);
  switch (b->arch)
    {
    default:
      return NULL;
    case bfd_arch_powerpc:
      return bfd_default_compatible (a, b);
    case bfd_arch_rs6000:
      if (b->mach == bfd_mach_rs6k)
	return a;
      return NULL;
    }
  /*NOTREACHED*/
}

const bfd_arch_info_type bfd_powerpc_archs[] =
{
#if BFD_DEFAULT_TARGET_SIZE == 64
  /* Default arch must come first.  */
  {
    64,	/* 64 bits in a word */
    64,	/* 64 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc64,
    "powerpc",
    "powerpc:common64",
    3,
    TRUE, /* default for 64 bit target */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[1]
  },
  /* elf32-ppc:ppc_elf_object_p relies on the default 32 bit arch
     being immediately after the 64 bit default.  */
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc, /* for the POWER/PowerPC common architecture */
    "powerpc",
    "powerpc:common",
    3,
    FALSE,
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[2],
  },
#else
  /* Default arch must come first.  */
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc, /* for the POWER/PowerPC common architecture */
    "powerpc",
    "powerpc:common",
    3,
    TRUE, /* default for 32 bit target */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[1],
  },
  /* elf64-ppc:ppc64_elf_object_p relies on the default 64 bit arch
     being immediately after the 32 bit default.  */
  {
    64,	/* 64 bits in a word */
    64,	/* 64 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc64,
    "powerpc",
    "powerpc:common64",
    3,
    FALSE,
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[2]
  },
#endif
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_603,
    "powerpc",
    "powerpc:603",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[3]
  },
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_ec603e,
    "powerpc",
    "powerpc:EC603e",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[4]
  },
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_604,
    "powerpc",
    "powerpc:604",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[5]
  },
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_403,
    "powerpc",
    "powerpc:403",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[6]
  },
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_601,
    "powerpc",
    "powerpc:601",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[7]
  },
  {
    64,	/* 64 bits in a word */
    64,	/* 64 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_620,
    "powerpc",
    "powerpc:620",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[8]
  },
  {
    64,	/* 64 bits in a word */
    64,	/* 64 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_630,
    "powerpc",
    "powerpc:630",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[9]
  },
  {
    64,	/* 64 bits in a word */
    64,	/* 64 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_a35,
    "powerpc",
    "powerpc:a35",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[10]
  },
  {
    64,	/* 64 bits in a word */
    64,	/* 64 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_rs64ii,
    "powerpc",
    "powerpc:rs64ii",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[11]
  },
  {
    64,	/* 64 bits in a word */
    64,	/* 64 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_rs64iii,
    "powerpc",
    "powerpc:rs64iii",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[12]
  },
  {
    32,	/* 32 bits in a word */
    32,	/* 32 bits in an address */
    8,	/* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_7400,
    "powerpc",
    "powerpc:7400",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[13]
  },
  {
    32, /* 32 bits in a word */
    32, /* 32 bits in an address */
    8,  /* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_e500,
    "powerpc",
    "powerpc:e500",
    3,
    FALSE,
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[14]
  },
  {
    32,       /* 32 bits in a word */
    32,       /* 32 bits in an address */
    8,        /* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_860,
    "powerpc",
    "powerpc:MPC8XX",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    &bfd_powerpc_archs[15]
  },
  {
    32, /* 32 bits in a word */
    32, /* 32 bits in an address */
    8,  /* 8 bits in a byte */
    bfd_arch_powerpc,
    bfd_mach_ppc_750,
    "powerpc",
    "powerpc:750",
    3,
    FALSE, /* not the default */
    powerpc_compatible,
    bfd_default_scan,
    0
  }
};

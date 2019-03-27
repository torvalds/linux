/* bfd back-end for mips support
   Copyright 1990, 1991, 1993, 1994, 1995, 1996, 1997, 1998, 2000, 2001,
   2002, 2003, 2004, 2007 Free Software Foundation, Inc.
   Written by Steve Chamberlain of Cygnus Support.

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

static const bfd_arch_info_type *mips_compatible
  (const bfd_arch_info_type *, const bfd_arch_info_type *);

/* The default routine tests bits_per_word, which is wrong on mips as
   mips word size doesn't correlate with reloc size.  */

static const bfd_arch_info_type *
mips_compatible (const bfd_arch_info_type *a, const bfd_arch_info_type *b)
{
  if (a->arch != b->arch)
    return NULL;

  /* Machine compatibility is checked in
     _bfd_mips_elf_merge_private_bfd_data.  */

  return a;
}

#define N(BITS_WORD, BITS_ADDR, NUMBER, PRINT, DEFAULT, NEXT)		\
  {							\
    BITS_WORD, /*  bits in a word */			\
    BITS_ADDR, /* bits in an address */			\
    8,	/* 8 bits in a byte */				\
    bfd_arch_mips,					\
    NUMBER,						\
    "mips",						\
    PRINT,						\
    3,							\
    DEFAULT,						\
    mips_compatible,					\
    bfd_default_scan,					\
    NEXT,						\
  }

enum
{
  I_mips3000,
  I_mips3900,
  I_mips4000,
  I_mips4010,
  I_mips4100,
  I_mips4111,
  I_mips4120,
  I_mips4300,
  I_mips4400,
  I_mips4600,
  I_mips4650,
  I_mips5000,
  I_mips5400,
  I_mips5500,
  I_mips6000,
  I_mips7000,
  I_mips8000,
  I_mips9000,
  I_mips10000,
  I_mips12000,
  I_mips16,
  I_mips5,
  I_mipsisa32,
  I_mipsisa32r2,
  I_mipsisa64,
  I_mipsisa64r2,
  I_mipsocteon,
  I_sb1,
};

#define NN(index) (&arch_info_struct[(index) + 1])

static const bfd_arch_info_type arch_info_struct[] =
{
  N (32, 32, bfd_mach_mips3000, "mips:3000",      FALSE, NN(I_mips3000)),
  N (32, 32, bfd_mach_mips3900, "mips:3900",      FALSE, NN(I_mips3900)),
  N (64, 64, bfd_mach_mips4000, "mips:4000",      FALSE, NN(I_mips4000)),
  N (64, 64, bfd_mach_mips4010, "mips:4010",      FALSE, NN(I_mips4010)),
  N (64, 64, bfd_mach_mips4100, "mips:4100",      FALSE, NN(I_mips4100)),
  N (64, 64, bfd_mach_mips4111, "mips:4111",      FALSE, NN(I_mips4111)),
  N (64, 64, bfd_mach_mips4120, "mips:4120",      FALSE, NN(I_mips4120)),
  N (64, 64, bfd_mach_mips4300, "mips:4300",      FALSE, NN(I_mips4300)),
  N (64, 64, bfd_mach_mips4400, "mips:4400",      FALSE, NN(I_mips4400)),
  N (64, 64, bfd_mach_mips4600, "mips:4600",      FALSE, NN(I_mips4600)),
  N (64, 64, bfd_mach_mips4650, "mips:4650",      FALSE, NN(I_mips4650)),
  N (64, 64, bfd_mach_mips5000, "mips:5000",      FALSE, NN(I_mips5000)),
  N (64, 64, bfd_mach_mips5400, "mips:5400",      FALSE, NN(I_mips5400)),
  N (64, 64, bfd_mach_mips5500, "mips:5500",      FALSE, NN(I_mips5500)),
  N (32, 32, bfd_mach_mips6000, "mips:6000",      FALSE, NN(I_mips6000)),
  N (64, 64, bfd_mach_mips7000, "mips:7000",      FALSE, NN(I_mips7000)),
  N (64, 64, bfd_mach_mips8000, "mips:8000",      FALSE, NN(I_mips8000)),
  N (64, 64, bfd_mach_mips9000, "mips:9000",      FALSE, NN(I_mips9000)),
  N (64, 64, bfd_mach_mips10000,"mips:10000",     FALSE, NN(I_mips10000)),
  N (64, 64, bfd_mach_mips12000,"mips:12000",     FALSE, NN(I_mips12000)),
  N (64, 64, bfd_mach_mips16,   "mips:16",        FALSE, NN(I_mips16)),
  N (64, 64, bfd_mach_mips5,    "mips:mips5",     FALSE, NN(I_mips5)),
  N (32, 32, bfd_mach_mipsisa32,  "mips:isa32",   FALSE, NN(I_mipsisa32)),
  N (32, 32, bfd_mach_mipsisa32r2,"mips:isa32r2", FALSE, NN(I_mipsisa32r2)),
  N (64, 64, bfd_mach_mipsisa64,  "mips:isa64",   FALSE, NN(I_mipsisa64)),
  N (64, 64, bfd_mach_mipsisa64r2,"mips:isa64r2", FALSE, NN(I_mipsisa64r2)),
  N (64, 64, bfd_mach_mips_octeon,"mips:octeon",  FALSE, NN(I_mipsocteon)),
  N (64, 64, bfd_mach_mips_sb1, "mips:sb1",       FALSE, 0),
};

/* The default architecture is mips:3000, but with a machine number of
   zero.  This lets the linker distinguish between a default setting
   of mips, and an explicit setting of mips:3000.  */

const bfd_arch_info_type bfd_mips_arch =
N (32, 32, 0, "mips", TRUE, &arch_info_struct[0]);

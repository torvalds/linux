/* BFD support for the Intel 386 architecture.
   Copyright 1992, 1994, 1995, 1996, 1998, 2000, 2001, 2002, 2004, 2007
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
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"

const bfd_arch_info_type bfd_x86_64_arch_intel_syntax =
{
  64, /* 64 bits in a word */
  64, /* 64 bits in an address */
  8,  /* 8 bits in a byte */
  bfd_arch_i386,
  bfd_mach_x86_64_intel_syntax,
  "i386:intel",
  "i386:x86-64:intel",
  3,
  FALSE,
  bfd_default_compatible,
  bfd_default_scan,
  0
};

const bfd_arch_info_type bfd_i386_arch_intel_syntax =
{
  32,	/* 32 bits in a word */
  32,	/* 32 bits in an address */
  8,	/* 8 bits in a byte */
  bfd_arch_i386,
  bfd_mach_i386_i386_intel_syntax,
  "i386:intel",
  "i386:intel",
  3,
  TRUE,
  bfd_default_compatible,
  bfd_default_scan,
  &bfd_x86_64_arch_intel_syntax
};

const bfd_arch_info_type i8086_arch =
{
  32,	/* 32 bits in a word */
  32,	/* 32 bits in an address (well, not really) */
  8,	/* 8 bits in a byte */
  bfd_arch_i386,
  bfd_mach_i386_i8086,
  "i8086",
  "i8086",
  3,
  FALSE,
  bfd_default_compatible,
  bfd_default_scan,
  &bfd_i386_arch_intel_syntax
};

const bfd_arch_info_type bfd_x86_64_arch =
{
  64, /* 32 bits in a word */
  64, /* 32 bits in an address */
  8,  /* 8 bits in a byte */
  bfd_arch_i386,
  bfd_mach_x86_64,
  "i386",
  "i386:x86-64",
  3,
  FALSE,
  bfd_default_compatible,
  bfd_default_scan,
  &i8086_arch
};

const bfd_arch_info_type bfd_i386_arch =
{
  32,	/* 32 bits in a word */
  32,	/* 32 bits in an address */
  8,	/* 8 bits in a byte */
  bfd_arch_i386,
  bfd_mach_i386_i386,
  "i386",
  "i386",
  3,
  TRUE,
  bfd_default_compatible,
  bfd_default_scan,
  &bfd_x86_64_arch
};

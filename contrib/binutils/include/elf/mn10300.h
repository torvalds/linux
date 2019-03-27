/* MN10300 ELF support for BFD.
   Copyright 1998, 1999, 2000, 2003 Free Software Foundation, Inc.

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

/* This file holds definitions specific to the MN10300 ELF ABI. */

#ifndef _ELF_MN10300_H
#define _ELF_MN10300_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_mn10300_reloc_type)
  RELOC_NUMBER (R_MN10300_NONE, 0)
  RELOC_NUMBER (R_MN10300_32, 1)
  RELOC_NUMBER (R_MN10300_16, 2)
  RELOC_NUMBER (R_MN10300_8, 3)
  RELOC_NUMBER (R_MN10300_PCREL32, 4)
  RELOC_NUMBER (R_MN10300_PCREL16, 5)
  RELOC_NUMBER (R_MN10300_PCREL8, 6)
  RELOC_NUMBER (R_MN10300_GNU_VTINHERIT, 7)
  RELOC_NUMBER (R_MN10300_GNU_VTENTRY, 8)
  RELOC_NUMBER (R_MN10300_24, 9)
  RELOC_NUMBER (R_MN10300_GOTPC32, 10)
  RELOC_NUMBER (R_MN10300_GOTPC16, 11)
  RELOC_NUMBER (R_MN10300_GOTOFF32, 12)
  RELOC_NUMBER (R_MN10300_GOTOFF24, 13)
  RELOC_NUMBER (R_MN10300_GOTOFF16, 14)
  RELOC_NUMBER (R_MN10300_PLT32, 15)
  RELOC_NUMBER (R_MN10300_PLT16, 16)
  RELOC_NUMBER (R_MN10300_GOT32, 17)
  RELOC_NUMBER (R_MN10300_GOT24, 18)
  RELOC_NUMBER (R_MN10300_GOT16, 19)
  RELOC_NUMBER (R_MN10300_COPY, 20)
  RELOC_NUMBER (R_MN10300_GLOB_DAT, 21)
  RELOC_NUMBER (R_MN10300_JMP_SLOT, 22)
  RELOC_NUMBER (R_MN10300_RELATIVE, 23)
END_RELOC_NUMBERS (R_MN10300_MAX)

/* Machine variant if we know it.  This field was invented at Cygnus,
   but it is hoped that other vendors will adopt it.  If some standard
   is developed, this code should be changed to follow it. */

#define EF_MN10300_MACH		0x00FF0000

/* Cygnus is choosing values between 80 and 9F;
   00 - 7F should be left for a future standard;
   the rest are open. */

#define E_MN10300_MACH_MN10300	0x00810000
#define E_MN10300_MACH_AM33	0x00820000
#define E_MN10300_MACH_AM33_2   0x00830000
#endif /* _ELF_MN10300_H */

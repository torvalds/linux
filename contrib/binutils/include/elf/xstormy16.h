/* XSTORMY16 ELF support for BFD.
   Copyright (C) 2001, 2002 Free Software Foundation, Inc.

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
along with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef _ELF_XSTORMY16_H
#define _ELF_XSTORMY16_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_xstormy16_reloc_type)
  RELOC_NUMBER (R_XSTORMY16_NONE, 0)

  RELOC_NUMBER (R_XSTORMY16_32, 1)
  RELOC_NUMBER (R_XSTORMY16_16, 2)
  RELOC_NUMBER (R_XSTORMY16_8, 3)
  RELOC_NUMBER (R_XSTORMY16_PC32, 4)
  RELOC_NUMBER (R_XSTORMY16_PC16, 5)
  RELOC_NUMBER (R_XSTORMY16_PC8, 6)

  RELOC_NUMBER (R_XSTORMY16_REL_12, 7)
  RELOC_NUMBER (R_XSTORMY16_24, 8)
  RELOC_NUMBER (R_XSTORMY16_FPTR16, 9)

  RELOC_NUMBER (R_XSTORMY16_LO16, 10)
  RELOC_NUMBER (R_XSTORMY16_HI16, 11)
  RELOC_NUMBER (R_XSTORMY16_12, 12)

  RELOC_NUMBER (R_XSTORMY16_GNU_VTINHERIT, 128)
  RELOC_NUMBER (R_XSTORMY16_GNU_VTENTRY, 129)
END_RELOC_NUMBERS (R_XSTORMY16_max)

/* Define the data & instruction memory discriminator.  In a linked
   executable, an symbol should be deemed to point to an instruction
   if ((address & XSTORMY16_INSN_MASK) == XSTORMY16_INSN_VALUE), and similarly
   for the data space.  See also `ld/emulparams/elf32xstormy16.sh'.  */
#define XSTORMY16_DATA_MASK   0xffc00000
#define XSTORMY16_DATA_VALUE  0x00000000
#define XSTORMY16_INSN_MASK   0xffc00000
#define XSTORMY16_INSN_VALUE  0x00400000

#endif /* _ELF_XSTORMY16_H */

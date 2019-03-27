/* Motorola MCore support for BFD.
   Copyright 1995, 1999, 2000 Free Software Foundation, Inc.

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

/* This file holds definitions specific to the MCore ELF ABI. */
#ifndef _ELF_MORE_H
#define _ELF_MORE_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_mcore_reloc_type)
  RELOC_NUMBER (R_MCORE_NONE, 0)
  RELOC_NUMBER (R_MCORE_ADDR32, 1)
  RELOC_NUMBER (R_MCORE_PCRELIMM8BY4, 2)
  RELOC_NUMBER (R_MCORE_PCRELIMM11BY2, 3)
  RELOC_NUMBER (R_MCORE_PCRELIMM4BY2, 4)
  RELOC_NUMBER (R_MCORE_PCREL32, 5)
  RELOC_NUMBER (R_MCORE_PCRELJSR_IMM11BY2, 6)
  RELOC_NUMBER (R_MCORE_GNU_VTINHERIT, 7)
  RELOC_NUMBER (R_MCORE_GNU_VTENTRY, 8)
  RELOC_NUMBER (R_MCORE_RELATIVE, 9)
  RELOC_NUMBER (R_MCORE_COPY, 10)
  RELOC_NUMBER (R_MCORE_GLOB_DAT, 11)
  RELOC_NUMBER (R_MCORE_JUMP_SLOT, 12)
END_RELOC_NUMBERS (R_MCORE_max)

/* Section Attributes.  */
#define SHF_MCORE_NOREAD	0x80000000

#endif /* _ELF_MCORE_H */

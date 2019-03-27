/* CR16 ELF support for BFD.
   Copyright 2007 Free Software Foundation, Inc.
   Contributed by M R Swami Reddy.

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
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef _ELF_CR16_H
#define _ELF_CR16_H

#include "elf/reloc-macros.h"

/* Creating indices for reloc_map_index array.  */
START_RELOC_NUMBERS(elf_cr16_reloc_type)
  RELOC_NUMBER (R_CR16_NONE,           0)
  RELOC_NUMBER (R_CR16_NUM8,           1)
  RELOC_NUMBER (R_CR16_NUM16,          2)
  RELOC_NUMBER (R_CR16_NUM32,          3)
  RELOC_NUMBER (R_CR16_NUM32a,         4)
  RELOC_NUMBER (R_CR16_REGREL4,        5)
  RELOC_NUMBER (R_CR16_REGREL4a,       6)
  RELOC_NUMBER (R_CR16_REGREL14,       7)
  RELOC_NUMBER (R_CR16_REGREL14a,      8)
  RELOC_NUMBER (R_CR16_REGREL16,       9)
  RELOC_NUMBER (R_CR16_REGREL20,       10)
  RELOC_NUMBER (R_CR16_REGREL20a,      11)
  RELOC_NUMBER (R_CR16_ABS20,          12)
  RELOC_NUMBER (R_CR16_ABS24,          13)
  RELOC_NUMBER (R_CR16_IMM4,           14)
  RELOC_NUMBER (R_CR16_IMM8,           15)
  RELOC_NUMBER (R_CR16_IMM16,          16)
  RELOC_NUMBER (R_CR16_IMM20,          17)
  RELOC_NUMBER (R_CR16_IMM24,          18)
  RELOC_NUMBER (R_CR16_IMM32,          19)
  RELOC_NUMBER (R_CR16_IMM32a,         20)
  RELOC_NUMBER (R_CR16_DISP4,          21)
  RELOC_NUMBER (R_CR16_DISP8,          22)
  RELOC_NUMBER (R_CR16_DISP16,         23)
  RELOC_NUMBER (R_CR16_DISP24,         24)
  RELOC_NUMBER (R_CR16_DISP24a,        25)
END_RELOC_NUMBERS(R_CR16_MAX)
        
#endif /* _ELF_CR16_H */

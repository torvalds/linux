/* CRX ELF support for BFD.
   Copyright 2004 Free Software Foundation, Inc.
   Contributed by Tomer Levi, NSC, Israel.
   Originally written for GAS 2.12 by Tomer Levi, NSC, Israel.
   Updates, BFDizing, GNUifying and ELF support by Tomer Levi.

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

#ifndef _ELF_CRX_H
#define _ELF_CRX_H

#include "elf/reloc-macros.h"

/* Creating indices for reloc_map_index array.  */
START_RELOC_NUMBERS(elf_crx_reloc_type)
  RELOC_NUMBER (R_CRX_NONE,           0)
  RELOC_NUMBER (R_CRX_REL4,           1)
  RELOC_NUMBER (R_CRX_REL8,           2)
  RELOC_NUMBER (R_CRX_REL8_CMP,       3)
  RELOC_NUMBER (R_CRX_REL16,          4)
  RELOC_NUMBER (R_CRX_REL24,          5)
  RELOC_NUMBER (R_CRX_REL32,          6)
  RELOC_NUMBER (R_CRX_REGREL12,       7)
  RELOC_NUMBER (R_CRX_REGREL22,       8)
  RELOC_NUMBER (R_CRX_REGREL28,       9)
  RELOC_NUMBER (R_CRX_REGREL32,       10)
  RELOC_NUMBER (R_CRX_ABS16,          11)
  RELOC_NUMBER (R_CRX_ABS32,          12)
  RELOC_NUMBER (R_CRX_NUM8,	      13)
  RELOC_NUMBER (R_CRX_NUM16,          14)
  RELOC_NUMBER (R_CRX_NUM32,          15)
  RELOC_NUMBER (R_CRX_IMM16,	      16)
  RELOC_NUMBER (R_CRX_IMM32,	      17)
  RELOC_NUMBER (R_CRX_SWITCH8,	      18)
  RELOC_NUMBER (R_CRX_SWITCH16,	      19)
  RELOC_NUMBER (R_CRX_SWITCH32,	      20)
END_RELOC_NUMBERS(R_CRX_MAX)
	
#endif /* _ELF_CRX_H */

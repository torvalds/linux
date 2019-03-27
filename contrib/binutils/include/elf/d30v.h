/* d30v ELF support for BFD.
   Copyright 1998, 2000 Free Software Foundation, Inc.

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

#ifndef _ELF_D30V_H
#define _ELF_D30V_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_d30v_reloc_type)
  RELOC_NUMBER (R_D30V_NONE, 0)
  RELOC_NUMBER (R_D30V_6, 1)
  RELOC_NUMBER (R_D30V_9_PCREL, 2)
  RELOC_NUMBER (R_D30V_9_PCREL_R, 3)
  RELOC_NUMBER (R_D30V_15, 4)
  RELOC_NUMBER (R_D30V_15_PCREL, 5)
  RELOC_NUMBER (R_D30V_15_PCREL_R, 6)
  RELOC_NUMBER (R_D30V_21, 7)
  RELOC_NUMBER (R_D30V_21_PCREL, 8)
  RELOC_NUMBER (R_D30V_21_PCREL_R, 9)
  RELOC_NUMBER (R_D30V_32, 10)
  RELOC_NUMBER (R_D30V_32_PCREL, 11)
  RELOC_NUMBER (R_D30V_32_NORMAL, 12)
END_RELOC_NUMBERS (R_D30V_max)

#endif

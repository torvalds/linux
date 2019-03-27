/* MN10200 ELF support for BFD.
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
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* This file holds definitions specific to the MN10200 ELF ABI. */

#ifndef _ELF_MN10200_H
#define _ELF_MN10200_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_mn10200_reloc_type)
  RELOC_NUMBER (R_MN10200_NONE, 0)
  RELOC_NUMBER (R_MN10200_32, 1)
  RELOC_NUMBER (R_MN10200_16, 2)
  RELOC_NUMBER (R_MN10200_8, 3)
  RELOC_NUMBER (R_MN10200_24, 4)
  RELOC_NUMBER (R_MN10200_PCREL8, 5)
  RELOC_NUMBER (R_MN10200_PCREL16, 6)
  RELOC_NUMBER (R_MN10200_PCREL24, 7)
END_RELOC_NUMBERS (R_MN10200_max)

#endif /* _ELF_MN10200_H */

/* FR30 ELF support for BFD.
   Copyright 1998, 1999, 2000 Free Software Foundation, Inc.

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

#ifndef _ELF_FR30_H
#define _ELF_FR30_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_fr30_reloc_type)
  RELOC_NUMBER (R_FR30_NONE, 0)
  RELOC_NUMBER (R_FR30_8, 1)
  RELOC_NUMBER (R_FR30_20, 2)
  RELOC_NUMBER (R_FR30_32, 3)
  RELOC_NUMBER (R_FR30_48, 4)
  RELOC_NUMBER (R_FR30_6_IN_4, 5)
  RELOC_NUMBER (R_FR30_8_IN_8, 6)
  RELOC_NUMBER (R_FR30_9_IN_8, 7)
  RELOC_NUMBER (R_FR30_10_IN_8, 8)
  RELOC_NUMBER (R_FR30_9_PCREL, 9)
  RELOC_NUMBER (R_FR30_12_PCREL, 10)
  RELOC_NUMBER (R_FR30_GNU_VTINHERIT, 11)
  RELOC_NUMBER (R_FR30_GNU_VTENTRY, 12)
END_RELOC_NUMBERS (R_FR30_max)

#endif /* _ELF_FR30_H */

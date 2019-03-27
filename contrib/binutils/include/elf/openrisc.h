/* OpenRISC ELF support for BFD.
   Copyright 2001 Free Software Foundation, Inc.

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

#ifndef _ELF_OPENRISC_H
#define _ELF_OPENRISC_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_openrisc_reloc_type)
  RELOC_NUMBER (R_OPENRISC_NONE, 0)
  RELOC_NUMBER (R_OPENRISC_INSN_REL_26, 1)
  RELOC_NUMBER (R_OPENRISC_INSN_ABS_26, 2)
  RELOC_NUMBER (R_OPENRISC_LO_16_IN_INSN, 3)
  RELOC_NUMBER (R_OPENRISC_HI_16_IN_INSN, 4)
  RELOC_NUMBER (R_OPENRISC_8, 5)
  RELOC_NUMBER (R_OPENRISC_16, 6)
  RELOC_NUMBER (R_OPENRISC_32, 7)
  RELOC_NUMBER (R_OPENRISC_GNU_VTINHERIT, 8)
  RELOC_NUMBER (R_OPENRISC_GNU_VTENTRY, 9)
END_RELOC_NUMBERS (R_OPENRISC_max)

#endif /* _ELF_OPENRISC_H */

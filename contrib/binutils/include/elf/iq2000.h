/* IQ2000 ELF support for BFD.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.

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

#ifndef _ELF_IQ2000_H
#define _ELF_IQ2000_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_iq2000_reloc_type)
  RELOC_NUMBER (R_IQ2000_NONE, 0)
  RELOC_NUMBER (R_IQ2000_16, 1)
  RELOC_NUMBER (R_IQ2000_32, 2)
  RELOC_NUMBER (R_IQ2000_26, 3)
  RELOC_NUMBER (R_IQ2000_PC16, 4)
  RELOC_NUMBER (R_IQ2000_HI16, 5)
  RELOC_NUMBER (R_IQ2000_LO16, 6)
  RELOC_NUMBER (R_IQ2000_OFFSET_16, 7)
  RELOC_NUMBER (R_IQ2000_OFFSET_21, 8)
  RELOC_NUMBER (R_IQ2000_UHI16, 9)
  RELOC_NUMBER (R_IQ2000_32_DEBUG, 10)
  RELOC_NUMBER (R_IQ2000_GNU_VTINHERIT, 200)
  RELOC_NUMBER (R_IQ2000_GNU_VTENTRY, 201)
END_RELOC_NUMBERS(R_IQ2000_max)

#define EF_IQ2000_CPU_IQ2000	0x00000001      /* default */
#define EF_IQ2000_CPU_IQ10      0x00000002      /* IQ10 */
#define EF_IQ2000_CPU_MASK	0x00000003	/* specific cpu bits */
#define EF_IQ2000_ALL_FLAGS	(EF_IQ2000_CPU_MASK)

/* Define the data & instruction memory discriminator.  In a linked
   executable, an symbol should be deemed to point to an instruction
   if ((address & IQ2000_INSN_MASK) == IQ2000_INSN_VALUE), and similarly
   for the data space.  */

#define IQ2000_DATA_MASK   0x80000000
#define IQ2000_DATA_VALUE  0x00000000
#define IQ2000_INSN_MASK   0x80000000
#define IQ2000_INSN_VALUE  0x80000000


#endif /* _ELF_IQ2000_H */

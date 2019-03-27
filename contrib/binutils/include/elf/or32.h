/* OR1K ELF support for BFD. Derived from ppc.h.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Contributed by Ivan Guzvinec  <ivang@opencores.org>

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

#ifndef _ELF_OR1K_H
#define _ELF_OR1K_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_or32_reloc_type)
  RELOC_NUMBER (R_OR32_NONE, 0)
  RELOC_NUMBER (R_OR32_32, 1)
  RELOC_NUMBER (R_OR32_16, 2)
  RELOC_NUMBER (R_OR32_8, 3)
  RELOC_NUMBER (R_OR32_CONST, 4)
  RELOC_NUMBER (R_OR32_CONSTH, 5)
  RELOC_NUMBER (R_OR32_JUMPTARG, 6)
  RELOC_NUMBER (R_OR32_GNU_VTENTRY, 7)
  RELOC_NUMBER (R_OR32_GNU_VTINHERIT, 8)
END_RELOC_NUMBERS (R_OR32_max)

/* Four bit OR32 machine type field.  */
#define EF_OR32_MACH             0x0000000f

/* Various CPU types.  */
#define E_OR32_MACH_BASE         0x00000000
#define E_OR32_MACH_UNUSED1      0x00000001
#define E_OR32_MACH_UNUSED2      0x00000002
#define E_OR32_MACH_UNUSED4      0x00000003

/* Processor specific section headers, sh_type field */
#define SHT_ORDERED		SHT_HIPROC	/* Link editor is to sort the \
						   entries in this section \
						   based on the address \
						   specified in the associated \
						   symbol table entry.  */

/* Processor specific section flags, sh_flags field */
#define SHF_EXCLUDE		0x80000000	/* Link editor is to exclude \
						   this section from executable \
						   and shared objects that it \
						   builds when those objects \
						   are not to be furhter \
						   relocated.  */
#endif /* _ELF_OR1K_H */

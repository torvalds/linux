/* VAX ELF support for BFD.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Contributed by Matt Thomas <matt@3am-software.com>.

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

#ifndef _ELF_VAX_H
#define _ELF_VAX_H

#include "elf/reloc-macros.h"

/* Relocation types.  */
START_RELOC_NUMBERS (elf_vax_reloc_type)
  RELOC_NUMBER (R_VAX_NONE, 0)		/* No reloc */
  RELOC_NUMBER (R_VAX_32, 1)		/* Direct 32 bit  */
  RELOC_NUMBER (R_VAX_16, 2)		/* Direct 16 bit  */
  RELOC_NUMBER (R_VAX_8, 3)		/* Direct 8 bit  */
  RELOC_NUMBER (R_VAX_PC32, 4)		/* PC relative 32 bit */
  RELOC_NUMBER (R_VAX_PC16, 5)		/* PC relative 16 bit */
  RELOC_NUMBER (R_VAX_PC8, 6)		/* PC relative 8 bit */
  RELOC_NUMBER (R_VAX_GOT32, 7)		/* 32 bit PC relative GOT entry */
  RELOC_NUMBER (R_VAX_PLT32, 13)	/* 32 bit PC relative PLT address */
  RELOC_NUMBER (R_VAX_COPY, 19)		/* Copy symbol at runtime */
  RELOC_NUMBER (R_VAX_GLOB_DAT, 20)	/* Create GOT entry */
  RELOC_NUMBER (R_VAX_JMP_SLOT, 21)	/* Create PLT entry */
  RELOC_NUMBER (R_VAX_RELATIVE, 22)	/* Adjust by program base */
  /* These are GNU extensions to enable C++ vtable garbage collection.  */
  RELOC_NUMBER (R_VAX_GNU_VTINHERIT, 23)
  RELOC_NUMBER (R_VAX_GNU_VTENTRY, 24)
END_RELOC_NUMBERS (R_VAX_max)   

/* Processor specific flags for the ELF header e_flags field.  */
#define EF_VAX_NONPIC		0x0001	/* Object contains non-PIC code */
#define EF_VAX_DFLOAT		0x0100	/* Object contains D-Float insn.  */
#define EF_VAX_GFLOAT		0x0200	/* Object contains G-Float insn.  */

#endif

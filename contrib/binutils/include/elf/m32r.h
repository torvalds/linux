/* M32R ELF support for BFD.
   Copyright 1996, 1997, 1998, 1999, 2000, 2003, 2004 Free Software Foundation, Inc.

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

#ifndef _ELF_M32R_H
#define _ELF_M32R_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_m32r_reloc_type)
  RELOC_NUMBER (R_M32R_NONE, 0)
  /* REL relocations */
  RELOC_NUMBER (R_M32R_16, 1)		 /* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_32, 2)		 /* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_24, 3)		 /* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_10_PCREL, 4)	 /* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_18_PCREL, 5)	 /* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_26_PCREL, 6)	 /* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_HI16_ULO, 7)	 /* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_HI16_SLO, 8)	 /* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_LO16, 9)		 /* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_SDA16, 10)	 /* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_GNU_VTINHERIT, 11)/* For backwards compatibility. */
  RELOC_NUMBER (R_M32R_GNU_VTENTRY, 12)	 /* For backwards compatibility. */

  /* RELA relocations */
  RELOC_NUMBER (R_M32R_16_RELA, 33)
  RELOC_NUMBER (R_M32R_32_RELA, 34)
  RELOC_NUMBER (R_M32R_24_RELA, 35)
  RELOC_NUMBER (R_M32R_10_PCREL_RELA, 36)
  RELOC_NUMBER (R_M32R_18_PCREL_RELA, 37)
  RELOC_NUMBER (R_M32R_26_PCREL_RELA, 38)
  RELOC_NUMBER (R_M32R_HI16_ULO_RELA, 39)
  RELOC_NUMBER (R_M32R_HI16_SLO_RELA, 40)
  RELOC_NUMBER (R_M32R_LO16_RELA, 41)
  RELOC_NUMBER (R_M32R_SDA16_RELA, 42)
  RELOC_NUMBER (R_M32R_RELA_GNU_VTINHERIT, 43)
  RELOC_NUMBER (R_M32R_RELA_GNU_VTENTRY, 44)

  RELOC_NUMBER (R_M32R_REL32, 45)
                                                                                
  RELOC_NUMBER (R_M32R_GOT24, 48)
  RELOC_NUMBER (R_M32R_26_PLTREL, 49)
  RELOC_NUMBER (R_M32R_COPY, 50)
  RELOC_NUMBER (R_M32R_GLOB_DAT, 51)
  RELOC_NUMBER (R_M32R_JMP_SLOT, 52)
  RELOC_NUMBER (R_M32R_RELATIVE, 53)
  RELOC_NUMBER (R_M32R_GOTOFF, 54)
  RELOC_NUMBER (R_M32R_GOTPC24, 55)
  RELOC_NUMBER (R_M32R_GOT16_HI_ULO, 56)
  RELOC_NUMBER (R_M32R_GOT16_HI_SLO, 57)
  RELOC_NUMBER (R_M32R_GOT16_LO, 58)
  RELOC_NUMBER (R_M32R_GOTPC_HI_ULO, 59)
  RELOC_NUMBER (R_M32R_GOTPC_HI_SLO, 60)
  RELOC_NUMBER (R_M32R_GOTPC_LO, 61)
  RELOC_NUMBER (R_M32R_GOTOFF_HI_ULO, 62)
  RELOC_NUMBER (R_M32R_GOTOFF_HI_SLO, 63)
  RELOC_NUMBER (R_M32R_GOTOFF_LO, 64)

END_RELOC_NUMBERS (R_M32R_max)

/* Processor specific section indices.  These sections do not actually
   exist.  Symbols with a st_shndx field corresponding to one of these
   values have a special meaning.  */

/* Small common symbol.  */
#define SHN_M32R_SCOMMON	0xff00

/* Processor specific section flags.  */

/* This section contains sufficient relocs to be relaxed.
   When relaxing, even relocs of branch instructions the assembler could
   complete must be present because relaxing may cause the branch target to
   move.  */
#define SHF_M32R_CAN_RELAX	0x10000000

/* Processor specific flags for the ELF header e_flags field.  */

/* Two bit m32r architecture field.  */
#define EF_M32R_ARCH		0x30000000

/* m32r code.  */
#define E_M32R_ARCH		0x00000000
/* m32rx code.  */
#define E_M32RX_ARCH            0x10000000
/* m32r2 code.  */
#define E_M32R2_ARCH            0x20000000

/* 12 bit m32r new instructions field.  */
#define EF_M32R_INST            0x0FFF0000
/* Parallel instructions.  */
#define E_M32R_HAS_PARALLEL     0x00010000
/* Hidden instructions for m32rx:
   jc, jnc, macwhi-a, macwlo-a, mulwhi-a, mulwlo-a, sth+, shb+, sat, pcmpbz,
   sc, snc.  */
#define E_M32R_HAS_HIDDEN_INST  0x00020000
/* New bit instructions:
   clrpsw, setpsw, bset, bclr, btst.  */
#define E_M32R_HAS_BIT_INST     0x00040000
/* Floating point instructions.  */
#define E_M32R_HAS_FLOAT_INST   0x00080000

/* 4 bit m32r ignore to check field.  */
#define EF_M32R_IGNORE          0x0000000F

#endif

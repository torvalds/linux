/* ALPHA ELF support for BFD.
   Copyright 1996, 1998, 2000, 2001, 2002 Free Software Foundation, Inc.

   By Eric Youngdale, <eric@aib.com>.  No processor supplement available
   for this platform.

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

/* This file holds definitions specific to the ALPHA ELF ABI.  Note
   that most of this is not actually implemented by BFD.  */

#ifndef _ELF_ALPHA_H
#define _ELF_ALPHA_H

/* Processor specific flags for the ELF header e_flags field.  */

/* All addresses must be below 2GB.  */
#define EF_ALPHA_32BIT		0x00000001

/* All relocations needed for relaxation with code movement are present.  */
#define EF_ALPHA_CANRELAX	0x00000002

/* Processor specific section flags.  */

/* This section must be in the global data area.  */
#define SHF_ALPHA_GPREL		0x10000000

/* Section contains some sort of debugging information.  The exact
   format is unspecified.  It's probably ECOFF symbols.  */
#define SHT_ALPHA_DEBUG		0x70000001

/* Section contains register usage information.  */
#define SHT_ALPHA_REGINFO	0x70000002

/* A section of type SHT_MIPS_REGINFO contains the following
   structure.  */
typedef struct
{
  /* Mask of general purpose registers used.  */
  unsigned long ri_gprmask;
  /* Mask of co-processor registers used.  */
  unsigned long ri_cprmask[4];
  /* GP register value for this object file.  */
  long ri_gp_value;
} Elf64_RegInfo;

/* Special values for the st_other field in the symbol table.  */

#define STO_ALPHA_NOPV		0x80
#define STO_ALPHA_STD_GPLOAD	0x88

/* Special values for Elf64_Dyn tag.  */
#define DT_ALPHA_PLTRO		DT_LOPROC

#include "elf/reloc-macros.h"

/* Alpha relocs.  */
START_RELOC_NUMBERS (elf_alpha_reloc_type)
  RELOC_NUMBER (R_ALPHA_NONE, 0)	/* No reloc */
  RELOC_NUMBER (R_ALPHA_REFLONG, 1)	/* Direct 32 bit */
  RELOC_NUMBER (R_ALPHA_REFQUAD, 2)	/* Direct 64 bit */
  RELOC_NUMBER (R_ALPHA_GPREL32, 3)	/* GP relative 32 bit */
  RELOC_NUMBER (R_ALPHA_LITERAL, 4)	/* GP relative 16 bit w/optimization */
  RELOC_NUMBER (R_ALPHA_LITUSE, 5)	/* Optimization hint for LITERAL */
  RELOC_NUMBER (R_ALPHA_GPDISP, 6)	/* Add displacement to GP */
  RELOC_NUMBER (R_ALPHA_BRADDR, 7)	/* PC+4 relative 23 bit shifted */
  RELOC_NUMBER (R_ALPHA_HINT, 8)	/* PC+4 relative 16 bit shifted */
  RELOC_NUMBER (R_ALPHA_SREL16, 9)	/* PC relative 16 bit */
  RELOC_NUMBER (R_ALPHA_SREL32, 10)	/* PC relative 32 bit */
  RELOC_NUMBER (R_ALPHA_SREL64, 11)	/* PC relative 64 bit */

  /* Skip 12 - 16; deprecated ECOFF relocs.  */

  RELOC_NUMBER (R_ALPHA_GPRELHIGH, 17)	/* GP relative 32 bit, high 16 bits */
  RELOC_NUMBER (R_ALPHA_GPRELLOW, 18)	/* GP relative 32 bit, low 16 bits */
  RELOC_NUMBER (R_ALPHA_GPREL16, 19)	/* GP relative 16 bit */

  /* Skip 20 - 23; deprecated ECOFF relocs.  */

  /* These relocations are specific to shared libraries.  */
  RELOC_NUMBER (R_ALPHA_COPY, 24)	/* Copy symbol at runtime */
  RELOC_NUMBER (R_ALPHA_GLOB_DAT, 25)	/* Create GOT entry */
  RELOC_NUMBER (R_ALPHA_JMP_SLOT, 26)	/* Create PLT entry */
  RELOC_NUMBER (R_ALPHA_RELATIVE, 27)	/* Adjust by program base */

  /* Like BRADDR, but assert that the source and target object file
     share the same GP value, and adjust the target address for 
     STO_ALPHA_STD_GPLOAD.  */
  RELOC_NUMBER (R_ALPHA_BRSGP, 28)

  /* Thread-Local Storage.  */
  RELOC_NUMBER (R_ALPHA_TLSGD, 29)
  RELOC_NUMBER (R_ALPHA_TLSLDM, 30)
  RELOC_NUMBER (R_ALPHA_DTPMOD64, 31)
  RELOC_NUMBER (R_ALPHA_GOTDTPREL, 32)
  RELOC_NUMBER (R_ALPHA_DTPREL64, 33)
  RELOC_NUMBER (R_ALPHA_DTPRELHI, 34)
  RELOC_NUMBER (R_ALPHA_DTPRELLO, 35)
  RELOC_NUMBER (R_ALPHA_DTPREL16, 36)
  RELOC_NUMBER (R_ALPHA_GOTTPREL, 37)
  RELOC_NUMBER (R_ALPHA_TPREL64, 38)
  RELOC_NUMBER (R_ALPHA_TPRELHI, 39)
  RELOC_NUMBER (R_ALPHA_TPRELLO, 40)
  RELOC_NUMBER (R_ALPHA_TPREL16, 41)

END_RELOC_NUMBERS (R_ALPHA_max)

#define LITUSE_ALPHA_ADDR	0
#define LITUSE_ALPHA_BASE	1
#define LITUSE_ALPHA_BYTOFF	2
#define LITUSE_ALPHA_JSR	3
#define LITUSE_ALPHA_TLSGD	4
#define LITUSE_ALPHA_TLSLDM	5
#define LITUSE_ALPHA_JSRDIRECT	6

#endif /* _ELF_ALPHA_H */

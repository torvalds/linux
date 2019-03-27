/* 390 ELF support for BFD.
   Copyright 2000, 2001, 2003 Free Software Foundation, Inc.
   Contributed by Carl B. Pedersen and Martin Schwidefsky.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef _ELF_390_H
#define _ELF_390_H

/* Processor specific flags for the ELF header e_flags field.  */

/* Symbol types.  */

#define STACK_REG		15		/* Global Stack reg */
#define BACKL_REG		14		/* Global Backlink reg */
#define BASE_REG		13		/* Global Base reg */
#define GOT_REG 		12		/* Holds addr of GOT */

#include "elf/reloc-macros.h"

/* Relocation types.  */

START_RELOC_NUMBERS (elf_s390_reloc_type)
    RELOC_NUMBER (R_390_NONE, 0)	/* No reloc.  */
    RELOC_NUMBER (R_390_8, 1)		/* Direct 8 bit.  */
    RELOC_NUMBER (R_390_12, 2)		/* Direct 12 bit.  */
    RELOC_NUMBER (R_390_16, 3)		/* Direct 16 bit.  */
    RELOC_NUMBER (R_390_32, 4)		/* Direct 32 bit.  */
    RELOC_NUMBER (R_390_PC32, 5)	/* PC relative 32 bit.  */
    RELOC_NUMBER (R_390_GOT12, 6)	/* 12 bit GOT offset.  */
    RELOC_NUMBER (R_390_GOT32, 7)	/* 32 bit GOT offset.  */
    RELOC_NUMBER (R_390_PLT32, 8)	/* 32 bit PC relative PLT address.  */
    RELOC_NUMBER (R_390_COPY, 9)	/* Copy symbol at runtime.  */
    RELOC_NUMBER (R_390_GLOB_DAT, 10)	/* Create GOT entry.  */
    RELOC_NUMBER (R_390_JMP_SLOT, 11)	/* Create PLT entry.  */
    RELOC_NUMBER (R_390_RELATIVE, 12)	/* Adjust by program base.  */
    RELOC_NUMBER (R_390_GOTOFF32, 13)	/* 32 bit offset to GOT.  */
    RELOC_NUMBER (R_390_GOTPC, 14)	/* 32 bit PC relative offset to GOT.  */
    RELOC_NUMBER (R_390_GOT16, 15)	/* 16 bit GOT offset.  */
    RELOC_NUMBER (R_390_PC16, 16)	/* PC relative 16 bit.  */
    RELOC_NUMBER (R_390_PC16DBL, 17)	/* PC relative 16 bit shifted by 1.  */
    RELOC_NUMBER (R_390_PLT16DBL, 18)	/* 16 bit PC rel. PLT shifted by 1.  */
    RELOC_NUMBER (R_390_PC32DBL, 19)	/* PC relative 32 bit shifted by 1.  */
    RELOC_NUMBER (R_390_PLT32DBL, 20)	/* 32 bit PC rel. PLT shifted by 1.  */
    RELOC_NUMBER (R_390_GOTPCDBL, 21)	/* 32 bit PC rel. GOT shifted by 1.  */
    RELOC_NUMBER (R_390_64, 22)		/* Direct 64 bit.  */
    RELOC_NUMBER (R_390_PC64, 23)	/* PC relative 64 bit.  */
    RELOC_NUMBER (R_390_GOT64, 24)	/* 64 bit GOT offset.  */
    RELOC_NUMBER (R_390_PLT64, 25)	/* 64 bit PC relative PLT address.  */
    RELOC_NUMBER (R_390_GOTENT, 26)	/* 32 bit PC rel. to GOT entry >> 1. */
    RELOC_NUMBER (R_390_GOTOFF16, 27)   /* 16 bit offset to GOT. */
    RELOC_NUMBER (R_390_GOTOFF64, 28)   /* 64 bit offset to GOT. */
    RELOC_NUMBER (R_390_GOTPLT12, 29)   /* 12 bit offset to jump slot.  */
    RELOC_NUMBER (R_390_GOTPLT16, 30)   /* 16 bit offset to jump slot.  */
    RELOC_NUMBER (R_390_GOTPLT32, 31)   /* 32 bit offset to jump slot.  */
    RELOC_NUMBER (R_390_GOTPLT64, 32)   /* 64 bit offset to jump slot.  */
    RELOC_NUMBER (R_390_GOTPLTENT, 33)  /* 32 bit rel. offset to jump slot.  */
    RELOC_NUMBER (R_390_PLTOFF16, 34)   /* 16 bit offset from GOT to PLT. */
    RELOC_NUMBER (R_390_PLTOFF32, 35)   /* 32 bit offset from GOT to PLT. */
    RELOC_NUMBER (R_390_PLTOFF64, 36)   /* 16 bit offset from GOT to PLT. */
    RELOC_NUMBER (R_390_TLS_LOAD, 37)	/* Tag for load insn in TLS code. */
    RELOC_NUMBER (R_390_TLS_GDCALL, 38)	/* Tag for function call in general
					   dynamic TLS code.  */
    RELOC_NUMBER (R_390_TLS_LDCALL, 39)	/* Tag for function call in local
					   dynamic TLS code.  */
    RELOC_NUMBER (R_390_TLS_GD32, 40)	/* Direct 32 bit for general dynamic
					   thread local data.  */
    RELOC_NUMBER (R_390_TLS_GD64, 41)	/* Direct 64 bit for general dynamic
					   thread local data.  */
    RELOC_NUMBER (R_390_TLS_GOTIE12, 42)/* 12 bit GOT offset for static TLS
					   block offset.  */
    RELOC_NUMBER (R_390_TLS_GOTIE32, 43)/* 32 bit GOT offset for static TLS
					   block offset.  */
    RELOC_NUMBER (R_390_TLS_GOTIE64, 44)/* 64 bit GOT offset for static TLS
					   block offset.  */
    RELOC_NUMBER (R_390_TLS_LDM32, 45)	/* Direct 32 bit for local dynamic
					   thread local data in LD code.  */
    RELOC_NUMBER (R_390_TLS_LDM64, 46)	/* Direct 64 bit for local dynamic
					   thread local data in LD code.  */
    RELOC_NUMBER (R_390_TLS_IE32, 47)	/* 32 bit address of GOT entry for
					   negated static TLS block offset.  */
    RELOC_NUMBER (R_390_TLS_IE64, 48)	/* 64 bit address of GOT entry for
					   negated static TLS block offset.  */
    RELOC_NUMBER (R_390_TLS_IEENT, 49)	/* 32 bit rel. offset to GOT entry for
					   negated static TLS block offset.  */
    RELOC_NUMBER (R_390_TLS_LE32, 50)	/* 32 bit negated offset relative to
					   static TLS block.  */
    RELOC_NUMBER (R_390_TLS_LE64, 51)	/* 64 bit negated offset relative to
					   static TLS block.  */
    RELOC_NUMBER (R_390_TLS_LDO32, 52)	/* 32 bit offset relative to TLS
					   block.  */
    RELOC_NUMBER (R_390_TLS_LDO64, 53)	/* 64 bit offset relative to TLS
					   block.  */
    RELOC_NUMBER (R_390_TLS_DTPMOD, 54)	/* ID of module containing symbol.  */
    RELOC_NUMBER (R_390_TLS_DTPOFF, 55)	/* Offset in TLS block.  */
    RELOC_NUMBER (R_390_TLS_TPOFF, 56)	/* Negate offset in static TLS
					   block.  */
    RELOC_NUMBER (R_390_20, 57)		/* Direct 20 bit.  */
    RELOC_NUMBER (R_390_GOT20, 58)	/* 20 bit GOT offset.  */
    RELOC_NUMBER (R_390_GOTPLT20, 59)	/* 20 bit offset to jump slot.  */
    RELOC_NUMBER (R_390_TLS_GOTIE20, 60)/* 20 bit GOT offset for statis TLS
					   block offset.  */
    /* These are GNU extensions to enable C++ vtable garbage collection.  */
    RELOC_NUMBER (R_390_GNU_VTINHERIT, 250)
    RELOC_NUMBER (R_390_GNU_VTENTRY, 251)
END_RELOC_NUMBERS (R_390_max)

#endif /* _ELF_390_H */



/* ix86 ELF support for BFD.
   Copyright 1998, 1999, 2000, 2002, 2004, 2005, 2006
   Free Software Foundation, Inc.

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

#ifndef _ELF_I386_H
#define _ELF_I386_H

#include "elf/reloc-macros.h"

START_RELOC_NUMBERS (elf_i386_reloc_type)
     RELOC_NUMBER (R_386_NONE,      0)	/* No reloc */
     RELOC_NUMBER (R_386_32,        1)	/* Direct 32 bit  */
     RELOC_NUMBER (R_386_PC32,      2)	/* PC relative 32 bit */
     RELOC_NUMBER (R_386_GOT32,     3)	/* 32 bit GOT entry */
     RELOC_NUMBER (R_386_PLT32,	    4)	/* 32 bit PLT address */
     RELOC_NUMBER (R_386_COPY,	    5)	/* Copy symbol at runtime */
     RELOC_NUMBER (R_386_GLOB_DAT,  6)	/* Create GOT entry */
     RELOC_NUMBER (R_386_JUMP_SLOT, 7)	/* Create PLT entry */
     RELOC_NUMBER (R_386_RELATIVE,  8)	/* Adjust by program base */
     RELOC_NUMBER (R_386_GOTOFF,    9)	/* 32 bit offset to GOT */
     RELOC_NUMBER (R_386_GOTPC,    10)	/* 32 bit PC relative offset to GOT */
     RELOC_NUMBER (R_386_32PLT,    11)	/* Used by Sun */
     FAKE_RELOC   (FIRST_INVALID_RELOC, 12)
     FAKE_RELOC   (LAST_INVALID_RELOC,  13)
     RELOC_NUMBER (R_386_TLS_TPOFF,14)
     RELOC_NUMBER (R_386_TLS_IE,   15)
     RELOC_NUMBER (R_386_TLS_GOTIE,16)
     RELOC_NUMBER (R_386_TLS_LE,   17)
     RELOC_NUMBER (R_386_TLS_GD,   18)
     RELOC_NUMBER (R_386_TLS_LDM,  19)
     RELOC_NUMBER (R_386_16,       20)
     RELOC_NUMBER (R_386_PC16,     21)
     RELOC_NUMBER (R_386_8,	   22)
     RELOC_NUMBER (R_386_PC8,      23)
     RELOC_NUMBER (R_386_TLS_GD_32,    24)
     RELOC_NUMBER (R_386_TLS_GD_PUSH,  25)
     RELOC_NUMBER (R_386_TLS_GD_CALL,  26)
     RELOC_NUMBER (R_386_TLS_GD_POP,   27)
     RELOC_NUMBER (R_386_TLS_LDM_32,   28)
     RELOC_NUMBER (R_386_TLS_LDM_PUSH, 29)
     RELOC_NUMBER (R_386_TLS_LDM_CALL, 30)
     RELOC_NUMBER (R_386_TLS_LDM_POP,  31)
     RELOC_NUMBER (R_386_TLS_LDO_32,   32)
     RELOC_NUMBER (R_386_TLS_IE_32,    33)
     RELOC_NUMBER (R_386_TLS_LE_32,    34)
     RELOC_NUMBER (R_386_TLS_DTPMOD32, 35)
     RELOC_NUMBER (R_386_TLS_DTPOFF32, 36)
     RELOC_NUMBER (R_386_TLS_TPOFF32,  37)
/* 38 */
     RELOC_NUMBER (R_386_TLS_GOTDESC,  39)
     RELOC_NUMBER (R_386_TLS_DESC_CALL,40)
     RELOC_NUMBER (R_386_TLS_DESC,     41)

     /* Used by Intel.  */
     RELOC_NUMBER (R_386_USED_BY_INTEL_200, 200)

     /* These are GNU extensions to enable C++ vtable garbage collection.  */
     RELOC_NUMBER (R_386_GNU_VTINHERIT, 250)
     RELOC_NUMBER (R_386_GNU_VTENTRY, 251)
END_RELOC_NUMBERS (R_386_max)

#endif

/* SPU ELF support for BFD.

   Copyright 2006, 2007 Free Software Foundation, Inc.

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

#ifndef _ELF_SPU_H
#define _ELF_SPU_H

#include "elf/reloc-macros.h"

/* elf32-spu.c depends on these being consecutive. */
START_RELOC_NUMBERS (elf_spu_reloc_type)
     RELOC_NUMBER (R_SPU_NONE,		 0)
     RELOC_NUMBER (R_SPU_ADDR10,	 1)
     RELOC_NUMBER (R_SPU_ADDR16,	 2)
     RELOC_NUMBER (R_SPU_ADDR16_HI,	 3)
     RELOC_NUMBER (R_SPU_ADDR16_LO,	 4)
     RELOC_NUMBER (R_SPU_ADDR18,	 5)
     RELOC_NUMBER (R_SPU_ADDR32,	 6)
     RELOC_NUMBER (R_SPU_REL16,		 7)
     RELOC_NUMBER (R_SPU_ADDR7,		 8)
     RELOC_NUMBER (R_SPU_REL9,		 9)
     RELOC_NUMBER (R_SPU_REL9I,		10)
     RELOC_NUMBER (R_SPU_ADDR10I,	11)
     RELOC_NUMBER (R_SPU_ADDR16I,	12)
     RELOC_NUMBER (R_SPU_REL32,		13)
     RELOC_NUMBER (R_SPU_ADDR16X,	14)
     RELOC_NUMBER (R_SPU_PPU32,		15)
     RELOC_NUMBER (R_SPU_PPU64,		16)
END_RELOC_NUMBERS (R_SPU_max)

/* Program header extensions */

/* Mark a PT_LOAD segment as containing an overlay which should not
   initially be loaded.  */
#define PF_OVERLAY		(1 << 27)

/* SPU Dynamic Object Information.  */
#define PT_SPU_INFO             0x70000000

/* SPU plugin information */
#define SPU_PLUGIN_NAME         "SPUNAME"
#define SPU_PTNOTE_SPUNAME	".note.spu_name"

#endif /* _ELF_SPU_H */

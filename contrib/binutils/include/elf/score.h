/* Score ELF support for BFD.
   Copyright 2006 Free Software Foundation, Inc.
   Contributed by 
   Mei Ligang (ligang@sunnorth.com.cn)
   Pei-Lin Tsai (pltsai@sunplus.com)

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef _ELF_SCORE_H
#define _ELF_SCORE_H

#include "elf/reloc-macros.h"

#define SCORE_SIMULATOR_ACTIVE  1
#define OPC_PTMASK              0xc0000000      /* Parity-bit Mask.  */
#define OPC16_PTMASK		0x00008000
/* The parity-bit denotes.  */
#define OPC_32                  0xc0000000      /* Denotes 32b instruction, (default).  */
#define OPC_16                  0x00000000      /* Denotes 16b instruction.  */
#define OPC_PE                  0x8000          /* Denotes parallel-execution instructions.  */
#define GP_DISP_LABEL           "_gp_disp"

/* Processor specific flags for the ELF header e_flags field:  */

/* File contains position independent code.  */
#define EF_SCORE_PIC            0x80000000

/* Fix data dependency.  */
#define EF_SCORE_FIXDEP         0x40000000 

/* Defined and allocated common symbol.  Value is virtual address.  If
   relocated, alignment must be preserved.  */
#define SHN_SCORE_TEXT		0xff01
#define SHN_SCORE_DATA		0xff02
/* Small common symbol.  */
#define SHN_SCORE_SCOMMON	0xff03

/* Processor specific section flags.  */

/* This section must be in the global data area.  */
#define SHF_SCORE_GPREL		0x10000000

/* This section should be merged.  */
#define SHF_SCORE_MERGE		0x20000000

/* This section contains address data of size implied by section
   element size.  */
#define SHF_SCORE_ADDR		0x40000000

/* This section contains string data.  */
#define SHF_SCORE_STRING		0x80000000

/* This section may not be stripped.  */
#define SHF_SCORE_NOSTRIP	0x08000000

/* This section is local to threads.  */
#define SHF_SCORE_LOCAL		0x04000000

/* Linker should generate implicit weak names for this section.  */
#define SHF_SCORE_NAMES		0x02000000

/* Section contais text/data which may be replicated in other sections.
   Linker should retain only one copy.  */
#define SHF_SCORE_NODUPES	0x01000000

/* Processor specific dynamic array tags.  */

/* Base address of the segment.  */
#define DT_SCORE_BASE_ADDRESS	0x70000001
/* Number of local global offset table entries.  */
#define DT_SCORE_LOCAL_GOTNO	0x70000002
/* Number of entries in the .dynsym section.  */
#define DT_SCORE_SYMTABNO	0x70000003
/* Index of first dynamic symbol in global offset table.  */
#define DT_SCORE_GOTSYM		0x70000004
/* Index of first external dynamic symbol not referenced locally.  */
#define DT_SCORE_UNREFEXTNO	0x70000005
/* Number of page table entries in global offset table.  */
#define DT_SCORE_HIPAGENO	0x70000006


/* Processor specific section types.  */


/* Relocation types.  */
START_RELOC_NUMBERS (elf_score_reloc_type)
  RELOC_NUMBER (R_SCORE_NONE,           0)
  RELOC_NUMBER (R_SCORE_HI16,           1)   
  RELOC_NUMBER (R_SCORE_LO16,           2)   
  RELOC_NUMBER (R_SCORE_DUMMY1,         3)
  RELOC_NUMBER (R_SCORE_24,             4)   
  RELOC_NUMBER (R_SCORE_PC19,           5)  
  RELOC_NUMBER (R_SCORE16_11,           6)   
  RELOC_NUMBER (R_SCORE16_PC8,          7)  
  RELOC_NUMBER (R_SCORE_ABS32,          8)
  RELOC_NUMBER (R_SCORE_ABS16,          9)
  RELOC_NUMBER (R_SCORE_DUMMY2,         10)
  RELOC_NUMBER (R_SCORE_GP15,           11)
  RELOC_NUMBER (R_SCORE_GNU_VTINHERIT,  12)
  RELOC_NUMBER (R_SCORE_GNU_VTENTRY,    13)
  RELOC_NUMBER (R_SCORE_GOT15,          14)
  RELOC_NUMBER (R_SCORE_GOT_LO16,       15)
  RELOC_NUMBER (R_SCORE_CALL15,         16)
  RELOC_NUMBER (R_SCORE_GPREL32,        17)
  RELOC_NUMBER (R_SCORE_REL32,          18)
  RELOC_NUMBER (R_SCORE_DUMMY_HI16,     19)
END_RELOC_NUMBERS (R_SCORE_max)

#endif /* _ELF_SCORE_H */

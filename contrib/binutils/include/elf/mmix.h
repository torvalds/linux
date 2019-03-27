/* MMIX support for BFD.
   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.

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

/* This file holds definitions specific to the MMIX ELF ABI. */
#ifndef ELF_MMIX_H
#define ELF_MMIX_H

#include "elf/reloc-macros.h"

/* Relocations.  See the reloc table in bfd/elf64-mmix.c for details.  */
START_RELOC_NUMBERS (elf_mmix_reloc_type)
  RELOC_NUMBER (R_MMIX_NONE, 0)

  /* Standard absolute relocations.  */
  RELOC_NUMBER (R_MMIX_8, 1)
  RELOC_NUMBER (R_MMIX_16, 2)
  RELOC_NUMBER (R_MMIX_24, 3)
  RELOC_NUMBER (R_MMIX_32, 4)
  RELOC_NUMBER (R_MMIX_64, 5)

  /* Standard relative relocations.  */
  RELOC_NUMBER (R_MMIX_PC_8, 6)
  RELOC_NUMBER (R_MMIX_PC_16, 7)
  RELOC_NUMBER (R_MMIX_PC_24, 8)
  RELOC_NUMBER (R_MMIX_PC_32, 9)
  RELOC_NUMBER (R_MMIX_PC_64, 10)

  /* GNU extensions for C++ vtables.  */
  RELOC_NUMBER (R_MMIX_GNU_VTINHERIT, 11)
  RELOC_NUMBER (R_MMIX_GNU_VTENTRY, 12)

  /* A GETA instruction.  */
  RELOC_NUMBER (R_MMIX_GETA, 13)
  RELOC_NUMBER (R_MMIX_GETA_1, 14)
  RELOC_NUMBER (R_MMIX_GETA_2, 15)
  RELOC_NUMBER (R_MMIX_GETA_3, 16)

  /* A conditional branch instruction.  */
  RELOC_NUMBER (R_MMIX_CBRANCH, 17)
  RELOC_NUMBER (R_MMIX_CBRANCH_J, 18)
  RELOC_NUMBER (R_MMIX_CBRANCH_1, 19)
  RELOC_NUMBER (R_MMIX_CBRANCH_2, 20)
  RELOC_NUMBER (R_MMIX_CBRANCH_3, 21)

  /* A PUSHJ instruction.  */
  RELOC_NUMBER (R_MMIX_PUSHJ, 22)
  RELOC_NUMBER (R_MMIX_PUSHJ_1, 23)
  RELOC_NUMBER (R_MMIX_PUSHJ_2, 24)
  RELOC_NUMBER (R_MMIX_PUSHJ_3, 25)

  /* A JMP instruction.  */
  RELOC_NUMBER (R_MMIX_JMP, 26)
  RELOC_NUMBER (R_MMIX_JMP_1, 27)
  RELOC_NUMBER (R_MMIX_JMP_2, 28)
  RELOC_NUMBER (R_MMIX_JMP_3, 29)

  /* A relative address such as in a GETA or a branch.  */
  RELOC_NUMBER (R_MMIX_ADDR19, 30)

  /* A relative address such as in a JMP (only).  */
  RELOC_NUMBER (R_MMIX_ADDR27, 31)

  /* A general register or a number 0..255.  */
  RELOC_NUMBER (R_MMIX_REG_OR_BYTE, 32)

  /* A general register. */
  RELOC_NUMBER (R_MMIX_REG, 33)

  /* A global register and an offset, the global register (allocated at
     link time) contents plus the offset made equivalent to the relocation
     expression at link time.  The relocation must point at the Y field of
     an instruction.  */
  RELOC_NUMBER (R_MMIX_BASE_PLUS_OFFSET, 34)

  /* A LOCAL assertion.  */
  RELOC_NUMBER (R_MMIX_LOCAL, 35)

  /* A PUSHJ instruction, generating a stub if it does not reach.  */
  RELOC_NUMBER (R_MMIX_PUSHJ_STUBBABLE, 36)
END_RELOC_NUMBERS (R_MMIX_max)


/* Section Attributes.  */
/* A section containing necessary information for relaxation.  */
#define SHF_MMIX_CANRELAX	0x80000000

/* Symbol attributes.  */
/* A symbol with this section-index is a register.  */
#define SHN_REGISTER	SHN_LOPROC

/* This section holds contents for each initialized register, at VMA
   regno*8.  A symbol relative to this section will be transformed to an
   absolute symbol with the value corresponding to the register number at
   final link time.  A symbol with a value outside the inclusive range
   32*8 .. 254*8 is an error.  It is highly recommended to only use an
   upper bound of 253*8 or lower as specified in the (currently
   unspecified) ABI.  */
#define MMIX_REG_CONTENTS_SECTION_NAME ".MMIX.reg_contents"

/* At link time, a section by this name is created, expected to be
   included in MMIX_REG_CONTENTS_SECTION_NAME in the output.  */
#define MMIX_LD_ALLOCATED_REG_CONTENTS_SECTION_NAME \
 ".MMIX.reg_contents.linker_allocated"

/* This is a faked section holding symbols with SHN_REGISTER.  Don't
   confuse it with MMIX_REG_CONTENTS_SECTION_NAME; this one has no
   contents, just values.  It is an error for a value in this section to
   be outside the range 32..255 and it must never become an actual section
   in an object file.  */
#define MMIX_REG_SECTION_NAME "*REG*"

/* Appended with a number N=0..65535, this is a representation of the
   mmixal "BSPEC N" ... "ESPEC" directive pair; the contents go into an
   ELF section by name ".MMIX.spec_data.N".  */
#define MMIX_OTHER_SPEC_SECTION_PREFIX ".MMIX.spec_data."

/* A section SECNAME is noted to start at "__.MMIX.start.SECNAME" by the
   presence of this symbol.  Currently only implemented for ".text"
   through the symbol "__.MMIX.start..text".  */
#define MMIX_LOC_SECTION_START_SYMBOL_PREFIX "__.MMIX.start."

/* This symbol is always a function.  */
#define MMIX_START_SYMBOL_NAME "Main"


/* We smuggle in a few MMO specifics here.  We don't make a specific MMO
   file, since we can't reasonably support MMO without ELF; we have to
   include this file anyway.  */

#define MMO_TEXT_SECTION_NAME ".text"
#define MMO_DATA_SECTION_NAME ".data"

/* A definition for the flags we put in spec data in files.  A copy of our
   own of some flags to keep immune to BFD flag changes.  See section.c of
   2001-07-18 for flag documentation.  */
#define MMO_SEC_ALLOC      0x001
#define MMO_SEC_LOAD       0x002
#define MMO_SEC_RELOC      0x004
#define MMO_SEC_READONLY   0x010
#define MMO_SEC_CODE       0x020
#define MMO_SEC_DATA       0x040
#define MMO_SEC_NEVER_LOAD 0x400
#define MMO_SEC_IS_COMMON 0x8000
#define MMO_SEC_DEBUGGING 0x10000

#ifdef BFD_ARCH_SIZE
extern bfd_boolean _bfd_mmix_before_linker_allocation
  (bfd *, struct bfd_link_info *);
extern bfd_boolean _bfd_mmix_after_linker_allocation
  (bfd *, struct bfd_link_info *);
extern bfd_boolean _bfd_mmix_check_all_relocs
  (bfd *, struct bfd_link_info *);
#endif

#endif /* ELF_MMIX_H */

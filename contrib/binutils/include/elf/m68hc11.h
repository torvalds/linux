/* m68hc11 & m68hc12 ELF support for BFD.
   Copyright 1999, 2000, 2001, 2002 Free Software Foundation, Inc.

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

#ifndef _ELF_M68HC11_H
#define _ELF_M68HC11_H

#include "elf/reloc-macros.h"

/* Relocation types.  */
START_RELOC_NUMBERS (elf_m68hc11_reloc_type)
  RELOC_NUMBER (R_M68HC11_NONE, 0)
  RELOC_NUMBER (R_M68HC11_8, 1)
  RELOC_NUMBER (R_M68HC11_HI8, 2)
  RELOC_NUMBER (R_M68HC11_LO8, 3)
  RELOC_NUMBER (R_M68HC11_PCREL_8, 4)
  RELOC_NUMBER (R_M68HC11_16, 5)
  RELOC_NUMBER (R_M68HC11_32, 6)
  RELOC_NUMBER (R_M68HC11_3B, 7)
  RELOC_NUMBER (R_M68HC11_PCREL_16, 8)

     /* These are GNU extensions to enable C++ vtable garbage collection.  */
  RELOC_NUMBER (R_M68HC11_GNU_VTINHERIT, 9)
  RELOC_NUMBER (R_M68HC11_GNU_VTENTRY, 10)

  RELOC_NUMBER (R_M68HC11_24, 11)
  RELOC_NUMBER (R_M68HC11_LO16, 12)
  RELOC_NUMBER (R_M68HC11_PAGE, 13)

     /* GNU extension for linker relaxation.
        Mark beginning of a jump instruction (any form).  */
  RELOC_NUMBER (R_M68HC11_RL_JUMP, 20)

     /* Mark beginning of Gcc relaxation group instruction.  */
  RELOC_NUMBER (R_M68HC11_RL_GROUP, 21)
END_RELOC_NUMBERS (R_M68HC11_max)

/* Processor specific flags for the ELF header e_flags field.  */

/* ABI identification.  */
#define EF_M68HC11_ABI  0x00000000F

/* Integers are 32-bit long.  */
#define E_M68HC11_I32   0x000000001

/* Doubles are 64-bit long.  */
#define E_M68HC11_F64   0x000000002

/* Uses 68HC12 memory banks.  */
#define E_M68HC12_BANKS 0x000000004

#define EF_M68HC11_MACH_MASK 0xF0
#define EF_M68HC11_GENERIC   0x00 /* Generic 68HC12/backward compatibility.  */
#define EF_M68HC12_MACH      0x10 /* 68HC12 microcontroller.  */
#define EF_M68HCS12_MACH     0x20 /* 68HCS12 microcontroller.  */
#define EF_M68HC11_MACH(mach) ((mach) & EF_M68HC11_MACH_MASK)

/* True if we can merge machines.  A generic HC12 can work on any proc
   but once we have specific code, merge is not possible.  */
#define EF_M68HC11_CAN_MERGE_MACH(mach1, mach2) \
  ((EF_M68HC11_MACH (mach1) == EF_M68HC11_MACH (mach2)) \
   || (EF_M68HC11_MACH (mach1) == EF_M68HC11_GENERIC) \
   || (EF_M68HC11_MACH (mach2) == EF_M68HC11_GENERIC))

#define EF_M68HC11_MERGE_MACH(mach1, mach2) \
  (((EF_M68HC11_MACH (mach1) == EF_M68HC11_MACH (mach2)) \
    || (EF_M68HC11_MACH (mach1) == EF_M68HC11_GENERIC)) ? \
      EF_M68HC11_MACH (mach2) : EF_M68HC11_MACH (mach1))


/* Special values for the st_other field in the symbol table.  These
   are used for 68HC12 to identify far functions (must be called with
   'call' and returns with 'rtc').  */
#define STO_M68HC12_FAR 0x80

/* Identify interrupt handlers.  This is used by the debugger to
   correctly compute the stack frame.  */
#define STO_M68HC12_INTERRUPT 0x40
     
#endif

/* MSP430 ELF support for BFD.
   Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Dmitry Diky <diwil@mail.ru>

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

#ifndef _ELF_MSP430_H
#define _ELF_MSP430_H

#include "elf/reloc-macros.h"

/* Processor specific flags for the ELF header e_flags field.  */
#define EF_MSP430_MACH 		0xff

#define E_MSP430_MACH_MSP430x11  11
#define E_MSP430_MACH_MSP430x11x1  110
#define E_MSP430_MACH_MSP430x12  12
#define E_MSP430_MACH_MSP430x13  13
#define E_MSP430_MACH_MSP430x14  14
#define E_MSP430_MACH_MSP430x15  15
#define E_MSP430_MACH_MSP430x16  16
#define E_MSP430_MACH_MSP430x31  31
#define E_MSP430_MACH_MSP430x32  32
#define E_MSP430_MACH_MSP430x33  33
#define E_MSP430_MACH_MSP430x41  41
#define E_MSP430_MACH_MSP430x42  42
#define E_MSP430_MACH_MSP430x43  43
#define E_MSP430_MACH_MSP430x44  44

/* Relocations.  */
START_RELOC_NUMBERS (elf_msp430_reloc_type)
     RELOC_NUMBER (R_MSP430_NONE,		0)
     RELOC_NUMBER (R_MSP430_32,			1)
     RELOC_NUMBER (R_MSP430_10_PCREL,		2)
     RELOC_NUMBER (R_MSP430_16, 		3)
     RELOC_NUMBER (R_MSP430_16_PCREL, 		4)
     RELOC_NUMBER (R_MSP430_16_BYTE, 		5)
     RELOC_NUMBER (R_MSP430_16_PCREL_BYTE, 	6)
     RELOC_NUMBER (R_MSP430_2X_PCREL,		7)
     RELOC_NUMBER (R_MSP430_RL_PCREL,		8)

END_RELOC_NUMBERS (R_MSP430_max)

#endif /* _ELF_MSP430_H */

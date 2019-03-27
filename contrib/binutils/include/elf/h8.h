/* H8300/h8500 ELF support for BFD.
   Copyright 2001, 2003 Free Software Foundation, Inc.

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

#ifndef _ELF_H8_H
#define _ELF_H8_H

#include "elf/reloc-macros.h"

/* Relocations.  */
/* Relocations 59..63 are GNU extensions.  */
START_RELOC_NUMBERS (elf_h8_reloc_type)
  RELOC_NUMBER (R_H8_NONE, 0)
  RELOC_NUMBER (R_H8_DIR32, 1)
  RELOC_NUMBER (R_H8_DIR32_28, 2)
  RELOC_NUMBER (R_H8_DIR32_24, 3)
  RELOC_NUMBER (R_H8_DIR32_16, 4)
  RELOC_NUMBER (R_H8_DIR32U, 6)
  RELOC_NUMBER (R_H8_DIR32U_28, 7)
  RELOC_NUMBER (R_H8_DIR32U_24, 8)
  RELOC_NUMBER (R_H8_DIR32U_20, 9)
  RELOC_NUMBER (R_H8_DIR32U_16, 10)
  RELOC_NUMBER (R_H8_DIR24, 11)
  RELOC_NUMBER (R_H8_DIR24_20, 12)
  RELOC_NUMBER (R_H8_DIR24_16, 13)
  RELOC_NUMBER (R_H8_DIR24U, 14)
  RELOC_NUMBER (R_H8_DIR24U_20, 15)
  RELOC_NUMBER (R_H8_DIR24U_16, 16)
  RELOC_NUMBER (R_H8_DIR16, 17)
  RELOC_NUMBER (R_H8_DIR16U, 18)
  RELOC_NUMBER (R_H8_DIR16S_32, 19)
  RELOC_NUMBER (R_H8_DIR16S_28, 20)
  RELOC_NUMBER (R_H8_DIR16S_24, 21)
  RELOC_NUMBER (R_H8_DIR16S_20, 22)
  RELOC_NUMBER (R_H8_DIR16S, 23)
  RELOC_NUMBER (R_H8_DIR8, 24)
  RELOC_NUMBER (R_H8_DIR8U, 25)
  RELOC_NUMBER (R_H8_DIR8Z_32, 26)
  RELOC_NUMBER (R_H8_DIR8Z_28, 27)
  RELOC_NUMBER (R_H8_DIR8Z_24, 28)
  RELOC_NUMBER (R_H8_DIR8Z_20, 29)
  RELOC_NUMBER (R_H8_DIR8Z_16, 30)
  RELOC_NUMBER (R_H8_PCREL16, 31)
  RELOC_NUMBER (R_H8_PCREL8, 32)
  RELOC_NUMBER (R_H8_BPOS, 33)
  FAKE_RELOC (R_H8_FIRST_INVALID_DIR_RELOC, 34)
  FAKE_RELOC (R_H8_LAST_INVALID_DIR_RELOC, 58)
  RELOC_NUMBER (R_H8_DIR16A8, 59)
  RELOC_NUMBER (R_H8_DIR16R8, 60)
  RELOC_NUMBER (R_H8_DIR24A8, 61)
  RELOC_NUMBER (R_H8_DIR24R8, 62)
  RELOC_NUMBER (R_H8_DIR32A16, 63)
  RELOC_NUMBER (R_H8_ABS32, 65)
  RELOC_NUMBER (R_H8_ABS32A16, 127)
  RELOC_NUMBER (R_H8_SYM, 128)
  RELOC_NUMBER (R_H8_OPneg, 129)
  RELOC_NUMBER (R_H8_OPadd, 130)
  RELOC_NUMBER (R_H8_OPsub, 131)
  RELOC_NUMBER (R_H8_OPmul, 132)
  RELOC_NUMBER (R_H8_OPdiv, 133)
  RELOC_NUMBER (R_H8_OPshla, 134)
  RELOC_NUMBER (R_H8_OPshra, 135)
  RELOC_NUMBER (R_H8_OPsctsize, 136)
  RELOC_NUMBER (R_H8_OPhword, 137)
  RELOC_NUMBER (R_H8_OPlword, 138)
  RELOC_NUMBER (R_H8_OPhigh, 139)
  RELOC_NUMBER (R_H8_OPlow, 140)
  RELOC_NUMBER (R_H8_OPscttop, 141)
END_RELOC_NUMBERS (R_H8_max)

/* Machine variant if we know it.  This field was invented at Cygnus,
   but it is hoped that other vendors will adopt it.  If some standard
   is developed, this code should be changed to follow it. */

#define EF_H8_MACH		0x00FF0000

#define E_H8_MACH_H8300		0x00800000
#define E_H8_MACH_H8300H	0x00810000
#define E_H8_MACH_H8300S	0x00820000
#define E_H8_MACH_H8300HN	0x00830000
#define E_H8_MACH_H8300SN	0x00840000
#define E_H8_MACH_H8300SX	0x00850000
#define E_H8_MACH_H8300SXN	0x00860000

#endif

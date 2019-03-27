/* picoJava ELF support for BFD.
   Copyright 1999, 2000 Free Software Foundation, Inc.

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

#ifndef _ELF_PJ_H
#define _ELF_PJ_H

#include "elf/reloc-macros.h"

/* Relocations.  */

START_RELOC_NUMBERS (elf_pj_reloc_type)
  RELOC_NUMBER (R_PJ_NONE, 0)
  RELOC_NUMBER (R_PJ_DATA_DIR32, 1)
  RELOC_NUMBER (R_PJ_CODE_REL32, 2)
  RELOC_NUMBER (R_PJ_CODE_REL16, 3) 
  RELOC_NUMBER (R_PJ_CODE_DIR32, 6)
  RELOC_NUMBER (R_PJ_CODE_DIR16, 7)  
  RELOC_NUMBER (R_PJ_CODE_LO16, 13)
  RELOC_NUMBER (R_PJ_CODE_HI16, 14)
  RELOC_NUMBER (R_PJ_GNU_VTINHERIT, 15)
  RELOC_NUMBER (R_PJ_GNU_VTENTRY, 16)
END_RELOC_NUMBERS (R_PJ_max)

#define EF_PICOJAVA_ARCH     0x0000000f
#define EF_PICOJAVA_NEWCALLS 0x00000010
#define EF_PICOJAVA_GNUCALLS 0x00000020  /* The (currently) non standard GNU calling convention */

#endif

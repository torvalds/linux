/* DLX support for BFD.
   Copyright 2002 Free Software Foundation, Inc.

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

#ifndef _ELF_DLX_H
#define _ELF_DLX_H

#include "elf/reloc-macros.h"

#if 0
START_RELOC_NUMBERS (elf_dlx_reloc_type)
     RELOC_NUMBER (R_DLX_NONE,            0)
     RELOC_NUMBER (R_DLX_RELOC_16,        1)
     RELOC_NUMBER (R_DLX_RELOC_26,        2)
     RELOC_NUMBER (R_DLX_RELOC_32,        3)
     RELOC_NUMBER (R_DLX_GNU_VTINHERIT,   4)
     RELOC_NUMBER (R_DLX_GNU_VTENTRY,     5)
     RELOC_NUMBER (R_DLX_RELOC_16_HI,     6)
     RELOC_NUMBER (R_DLX_RELOC_16_LO,     7)
     RELOC_NUMBER (R_DLX_RELOC_16_PCREL,  8)
     RELOC_NUMBER (R_DLX_RELOC_26_PCREL,  9)
END_RELOC_NUMBERS (R_DLX_max)
#else
START_RELOC_NUMBERS (elf_dlx_reloc_type)
     RELOC_NUMBER (R_DLX_NONE,            0)
     RELOC_NUMBER (R_DLX_RELOC_8,         1)
     RELOC_NUMBER (R_DLX_RELOC_16,        2)
     RELOC_NUMBER (R_DLX_RELOC_32,        3)
     RELOC_NUMBER (R_DLX_GNU_VTINHERIT,   4)
     RELOC_NUMBER (R_DLX_GNU_VTENTRY,     5)
     RELOC_NUMBER (R_DLX_RELOC_16_HI,     6)
     RELOC_NUMBER (R_DLX_RELOC_16_LO,     7)
     RELOC_NUMBER (R_DLX_RELOC_16_PCREL,  8)
     RELOC_NUMBER (R_DLX_RELOC_26_PCREL,  9)
END_RELOC_NUMBERS (R_DLX_max)
#endif /* 0 */

#endif /* _ELF_DLX_H */

/* Intel 960 ELF support for BFD.
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

#ifndef _ELF_I960_H
#define _ELF_I960_H

#include "elf/reloc-macros.h"


START_RELOC_NUMBERS (elf_i960_reloc_type)
     RELOC_NUMBER (R_960_NONE,	    0)
     RELOC_NUMBER (R_960_12,	    1)
     RELOC_NUMBER (R_960_32,	    2)
     RELOC_NUMBER (R_960_IP24,	    3)
     RELOC_NUMBER (R_960_SUB,	    4)
     RELOC_NUMBER (R_960_OPTCALL,   5)
     RELOC_NUMBER (R_960_OPTCALLX,  6)
     RELOC_NUMBER (R_960_OPTCALLXA, 7)
END_RELOC_NUMBERS (R_960_max)

#endif /* _ELF_I960_H */

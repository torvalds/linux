/* i370 ELF support for BFD.
   Copyright 2000, 2002 Free Software Foundation, Inc.

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

/* This file holds definitions specific to the i370 ELF ABI.  Note
   that most of this is not actually implemented by BFD.  */

#ifndef _ELF_I370_H
#define _ELF_I370_H

#include "elf/reloc-macros.h"

/* Processor specific section headers, sh_type field */

#define SHT_ORDERED		SHT_HIPROC	/* Link editor is to sort the \
						   entries in this section \
						   based on the address \
						   specified in the associated \
						   symbol table entry.  */

#define	EF_I370_RELOCATABLE	0x00010000	/* i370 -mrelocatable flag */
#define	EF_I370_RELOCATABLE_LIB	0x00008000	/* i370 -mrelocatable-lib flag */
/* Processor specific section flags, sh_flags field */

#define SHF_EXCLUDE		0x80000000	/* Link editor is to exclude \
						   this section from executable \
						   and shared objects that it \
						   builds when those objects \
						   are not to be furhter \
						   relocated.  */

/* i370 relocations
   Note that there is really just one relocation that we currently
   support (and only one that we seem to need, at the moment), and
   that is the 31-bit address relocation.  Note that the 370/390
   only supports a 31-bit (2GB) address space.  */

START_RELOC_NUMBERS (i370_reloc_type)
  RELOC_NUMBER (R_I370_NONE,      0)
  RELOC_NUMBER (R_I370_ADDR31,    1)
  RELOC_NUMBER (R_I370_ADDR32,    2)
  RELOC_NUMBER (R_I370_ADDR16,    3) 
  RELOC_NUMBER (R_I370_REL31,     4)
  RELOC_NUMBER (R_I370_REL32,     5)  
  RELOC_NUMBER (R_I370_ADDR12,    6)
  RELOC_NUMBER (R_I370_REL12,     7)
  RELOC_NUMBER (R_I370_ADDR8,     8)
  RELOC_NUMBER (R_I370_REL8,      9)
  RELOC_NUMBER (R_I370_COPY,     10)
  RELOC_NUMBER (R_I370_RELATIVE, 11)
END_RELOC_NUMBERS (R_I370_max)

#endif /* _ELF_I370_H */

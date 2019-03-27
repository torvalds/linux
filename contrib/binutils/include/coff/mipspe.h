/* coff information for Windows CE with MIPS VR4111
   
   Copyright 2000 Free Software Foundation, Inc.

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

#define L_LNNO_SIZE 2
#define INCLUDE_COMDAT_FIELDS_IN_AUXENT
#include "coff/external.h"

#define MIPS_ARCH_MAGIC_WINCE	0x0166  /* Windows CE - little endian */
#define MIPS_PE_MAGIC		0x010b

#define MIPSBADMAG(x) ((x).f_magic != MIPS_ARCH_MAGIC_WINCE)

/* define some NT default values */
/*  #define NT_IMAGE_BASE        0x400000 moved to internal.h */
#define NT_SECTION_ALIGNMENT 0x1000
#define NT_FILE_ALIGNMENT    0x200
#define NT_DEF_RESERVE       0x100000
#define NT_DEF_COMMIT        0x1000

/********************** RELOCATION DIRECTIVES **********************/

/* The external reloc has an offset field, because some of the reloc
   types on the h8 don't have room in the instruction for the entire
   offset - eg the strange jump and high page addressing modes.  */

struct external_reloc
{
  char r_vaddr[4];
  char r_symndx[4];
  char r_type[2];
};

#define RELOC struct external_reloc
#define RELSZ 10

/* MIPS PE relocation types. */

#define	MIPS_R_ABSOLUTE	0 /* ignored */
#define	MIPS_R_REFHALF	1
#define	MIPS_R_REFWORD	2
#define	MIPS_R_JMPADDR	3
#define	MIPS_R_REFHI	4 /* PAIR follows */
#define	MIPS_R_REFLO	5
#define	MIPS_R_GPREL	6
#define	MIPS_R_LITERAL	7 /* same as GPREL */
#define	MIPS_R_SECTION	10
#define	MIPS_R_SECREL	11
#define	MIPS_R_SECRELLO	12
#define	MIPS_R_SECRELHI	13 /* PAIR follows */
#define	MIPS_R_RVA	34 /* 0x22 */
#define	MIPS_R_PAIR	37 /* 0x25 - symndx is really a signed 16-bit addend */

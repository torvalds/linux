/* Basic coff information for the PowerPC
   Based on coff/rs6000.h, coff/i386.h and others.
   
   Copyright 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
 
   Initial release: Kim Knuttila (krk@cygnus.com)  */
#define L_LNNO_SIZE 2
#define INCLUDE_COMDAT_FIELDS_IN_AUXENT
#include "coff/external.h"

/* Bits for f_flags:
 	F_RELFLG	relocation info stripped from file
 	F_EXEC		file is executable (no unresolved external references)
 	F_LNNO		line numbers stripped from file
 	F_LSYMS		local symbols stripped from file
 	F_AR32WR	file has byte ordering of an AR32WR machine (e.g. vax).  */

#define F_RELFLG	(0x0001)
#define F_EXEC		(0x0002)
#define F_LNNO		(0x0004)
#define F_LSYMS		(0x0008)

/* extra NT defines */
#define PPCMAGIC       0760         /* peeked on aa PowerPC Windows NT box */
#define DOSMAGIC       0x5a4d       /* from arm.h, i386.h */
#define NT_SIGNATURE   0x00004550   /* from arm.h, i386.h */

/* from winnt.h */
#define IMAGE_NT_OPTIONAL_HDR_MAGIC        0x10b

#define PPCBADMAG(x) ((x).f_magic != PPCMAGIC) 

/********************** RELOCATION DIRECTIVES **********************/

struct external_reloc
{
  char r_vaddr[4];
  char r_symndx[4];
  char r_type[2];
};

#define RELOC struct external_reloc
#define RELSZ 10


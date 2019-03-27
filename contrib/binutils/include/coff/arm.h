/* ARM COFF support for BFD.
   Copyright 1998, 1999, 2000, 2002, 2003 Free Software Foundation, Inc.

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

#define COFFARM 1

#define L_LNNO_SIZE 2
#define INCLUDE_COMDAT_FIELDS_IN_AUXENT
#include "coff/external.h"

/* Bits for f_flags:
 	F_RELFLG	relocation info stripped from file
 	F_EXEC		file is executable (no unresolved external references)
 	F_LNNO		line numbers stripped from file
 	F_LSYMS		local symbols stripped from file
        F_INTERWORK     file supports switching between ARM and Thumb instruction sets
        F_INTERWORK_SET the F_INTERWORK bit is valid
 	F_APCS_FLOAT	code passes float arguments in float registers
 	F_PIC		code is reentrant/position-independent
 	F_AR32WR	file has byte ordering of an AR32WR machine (e.g. vax)
 	F_APCS_26	file uses 26 bit ARM Procedure Calling Standard
 	F_APCS_SET	the F_APCS_26, F_APCS_FLOAT and F_PIC bits have been initialised
 	F_SOFT_FLOAT	code does not use floating point instructions.  */

#define F_RELFLG	(0x0001)
#define F_EXEC		(0x0002)
#define F_LNNO		(0x0004)
#define F_LSYMS		(0x0008)
#define F_INTERWORK	(0x0010)
#define F_INTERWORK_SET	(0x0020)
#define F_APCS_FLOAT	(0x0040)
#undef  F_AR16WR
#define F_PIC		(0x0080)
#define	F_AR32WR	(0x0100)
#define F_APCS_26	(0x0400)
#define F_APCS_SET	(0x0800)
#define F_SOFT_FLOAT	(0x2000)
#define F_VFP_FLOAT	(0x4000)

/* Bits stored in flags field of the internal_f structure */

#define F_INTERWORK	(0x0010)
#define F_APCS_FLOAT	(0x0040)
#define F_PIC		(0x0080)
#define F_APCS26	(0x1000)
#define F_ARM_ARCHITECTURE_MASK (0x4000+0x0800+0x0400)
#define F_ARM_2		(0x0400)
#define F_ARM_2a	(0x0800)
#define F_ARM_3		(0x0c00)
#define F_ARM_3M	(0x4000)
#define F_ARM_4		(0x4400)
#define F_ARM_4T	(0x4800)
#define F_ARM_5		(0x4c00)

/*
  ARMMAGIC ought to encoded the procesor type,
  but it is too late to change it now, instead
  the flags field of the internal_f structure
  is used as shown above.
 
  XXX - NC 5/6/97.  */

#define	ARMMAGIC	0xa00  /* I just made this up */

#define ARMBADMAG(x) (((x).f_magic != ARMMAGIC))

#define	ARMPEMAGIC	0x1c0
#define	THUMBPEMAGIC	0x1c2

#undef  ARMBADMAG
#define ARMBADMAG(x) (((x).f_magic != ARMMAGIC) && ((x).f_magic != ARMPEMAGIC) && ((x).f_magic != THUMBPEMAGIC))

#define OMAGIC          0404    /* object files, eg as output */
#define ZMAGIC          0413    /* demand load format, eg normal ld output */
#define STMAGIC		0401	/* target shlib */
#define SHMAGIC		0443	/* host   shlib */

/* define some NT default values */
/*  #define NT_IMAGE_BASE        0x400000 moved to internal.h */
#define NT_SECTION_ALIGNMENT 0x1000
#define NT_FILE_ALIGNMENT    0x200  
#define NT_DEF_RESERVE       0x100000
#define NT_DEF_COMMIT        0x1000

/* We use the .rdata section to hold read only data.  */
#define _LIT	".rdata"

/********************** RELOCATION DIRECTIVES **********************/
#ifdef ARM_WINCE
struct external_reloc
{
  char r_vaddr[4];
  char r_symndx[4];
  char r_type[2];
};

#define RELOC struct external_reloc
#define RELSZ 10

#else
struct external_reloc
{
  char r_vaddr[4];
  char r_symndx[4];
  char r_type[2];
  char r_offset[4];
};

#define RELOC struct external_reloc
#define RELSZ 14
#endif

#define ARM_NOTE_SECTION ".note"

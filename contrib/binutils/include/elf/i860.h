/* i860 ELF support for BFD.
   Copyright 2000 Free Software Foundation, Inc.

   Contributed by Jason Eckhardt <jle@cygnus.com>.

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

#ifndef _ELF_I860_H
#define _ELF_I860_H

/* Note: i860 ELF is defined to use only RELA relocations.  */

#include "elf/reloc-macros.h"

START_RELOC_NUMBERS (elf_i860_reloc_type)
     RELOC_NUMBER (R_860_NONE,      0x00)	/* No reloc */
     RELOC_NUMBER (R_860_32,        0x01)	/* S+A  */
     RELOC_NUMBER (R_860_COPY,	    0x02)	/* No calculation */
     RELOC_NUMBER (R_860_GLOB_DAT,  0x03)	/* S, Create GOT entry */
     RELOC_NUMBER (R_860_JUMP_SLOT, 0x04)	/* S+A, Create PLT entry */
     RELOC_NUMBER (R_860_RELATIVE,  0x05)	/* B+A, Adj by program base */
     RELOC_NUMBER (R_860_PC26,      0x30)	/* (S+A-P) >> 2 */ 
     RELOC_NUMBER (R_860_PLT26,	    0x31)	/* (L+A-P) >> 2 */
     RELOC_NUMBER (R_860_PC16,      0x32)	/* (S+A-P) >> 2 */
     RELOC_NUMBER (R_860_LOW0,      0x40)	/* S+A */
     RELOC_NUMBER (R_860_SPLIT0,    0x42)	/* S+A */       
     RELOC_NUMBER (R_860_LOW1,      0x44)	/* S+A */
     RELOC_NUMBER (R_860_SPLIT1,    0x46)	/* S+A */
     RELOC_NUMBER (R_860_LOW2,      0x48)	/* S+A */
     RELOC_NUMBER (R_860_SPLIT2,    0x4A)	/* S+A */
     RELOC_NUMBER (R_860_LOW3,      0x4C)	/* S+A */
     RELOC_NUMBER (R_860_LOGOT0,    0x50)	/* G */
     RELOC_NUMBER (R_860_SPGOT0,    0x52)	/* G */
     RELOC_NUMBER (R_860_LOGOT1,    0x54)	/* G */
     RELOC_NUMBER (R_860_SPGOT1,    0x56)	/* G */
     RELOC_NUMBER (R_860_LOGOTOFF0, 0x60)	/* O */
     RELOC_NUMBER (R_860_SPGOTOFF0, 0x62)	/* O */
     RELOC_NUMBER (R_860_LOGOTOFF1, 0x64)	/* O */
     RELOC_NUMBER (R_860_SPGOTOFF1, 0x66)	/* O */
     RELOC_NUMBER (R_860_LOGOTOFF2, 0x68)	/* O */
     RELOC_NUMBER (R_860_LOGOTOFF3, 0x6C)	/* O */
     RELOC_NUMBER (R_860_LOPC,      0x70)	/* (S+A-P) >> 2 */
     RELOC_NUMBER (R_860_HIGHADJ,   0x80)	/* hiadj(S+A) */
     RELOC_NUMBER (R_860_HAGOT,     0x90)	/* hiadj(G) */
     RELOC_NUMBER (R_860_HAGOTOFF,  0xA0)	/* hiadj(O) */
     RELOC_NUMBER (R_860_HAPC,      0xB0)	/* hiadj((S+A-P) >> 2) */
     RELOC_NUMBER (R_860_HIGH,      0xC0)	/* (S+A) >> 16 */
     RELOC_NUMBER (R_860_HIGOT,     0xD0)	/* G >> 16 */
     RELOC_NUMBER (R_860_HIGOTOFF,  0xE0)	/* O */
END_RELOC_NUMBERS (R_860_max)

#endif

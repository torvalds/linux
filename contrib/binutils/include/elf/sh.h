/* SH ELF support for BFD.
   Copyright 1998, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

#ifndef _ELF_SH_H
#define _ELF_SH_H

/* Processor specific flags for the ELF header e_flags field.  */

#define EF_SH_MACH_MASK	0x1f
#define EF_SH_UNKNOWN	   0 /* For backwards compatibility.  */
#define EF_SH1		   1
#define EF_SH2		   2
#define EF_SH3		   3
#define EF_SH_DSP	   4
#define EF_SH3_DSP	   5
#define EF_SH4AL_DSP	   6
#define EF_SH3E		   8
#define EF_SH4		   9
#define EF_SH2E            11
#define EF_SH4A		   12
#define EF_SH2A            13

#define EF_SH4_NOFPU	   16
#define EF_SH4A_NOFPU	   17
#define EF_SH4_NOMMU_NOFPU 18
#define EF_SH2A_NOFPU      19
#define EF_SH3_NOMMU       20

#define EF_SH2A_SH4_NOFPU  21
#define EF_SH2A_SH3_NOFPU  22
#define EF_SH2A_SH4        23
#define EF_SH2A_SH3E       24

/* This one can only mix in objects from other EF_SH5 objects.  */
#define EF_SH5		  10

/* Define the mapping from ELF to bfd mach numbers.
   bfd_mach_* are defined in bfd_in2.h (generated from
   archures.c).  */
#define EF_SH_BFD_TABLE \
/* EF_SH_UNKNOWN	*/ bfd_mach_sh3		, \
/* EF_SH1		*/ bfd_mach_sh		, \
/* EF_SH2		*/ bfd_mach_sh2		, \
/* EF_SH3		*/ bfd_mach_sh3		, \
/* EF_SH_DSP		*/ bfd_mach_sh_dsp	, \
/* EF_SH3_DSP		*/ bfd_mach_sh3_dsp	, \
/* EF_SHAL_DSP		*/ bfd_mach_sh4al_dsp	, \
/* 7			*/ 0, \
/* EF_SH3E		*/ bfd_mach_sh3e	, \
/* EF_SH4		*/ bfd_mach_sh4		, \
/* EF_SH5		*/ 0, \
/* EF_SH2E		*/ bfd_mach_sh2e	, \
/* EF_SH4A		*/ bfd_mach_sh4a	, \
/* EF_SH2A		*/ bfd_mach_sh2a        , \
/* 14, 15		*/ 0, 0, \
/* EF_SH4_NOFPU		*/ bfd_mach_sh4_nofpu	, \
/* EF_SH4A_NOFPU	*/ bfd_mach_sh4a_nofpu	, \
/* EF_SH4_NOMMU_NOFPU	*/ bfd_mach_sh4_nommu_nofpu, \
/* EF_SH2A_NOFPU	*/ bfd_mach_sh2a_nofpu  , \
/* EF_SH3_NOMMU		*/ bfd_mach_sh3_nommu   , \
/* EF_SH2A_SH4_NOFPU    */ bfd_mach_sh2a_nofpu_or_sh4_nommu_nofpu, \
/* EF_SH2A_SH3_NOFPU    */ bfd_mach_sh2a_nofpu_or_sh3_nommu, \
/* EF_SH2A_SH4          */ bfd_mach_sh2a_or_sh4 , \
/* EF_SH2A_SH3E         */ bfd_mach_sh2a_or_sh3e

/* Convert arch_sh* into EF_SH*.  */
int sh_find_elf_flags (unsigned int arch_set);

/* Convert bfd_mach_* into EF_SH*.  */
int sh_elf_get_flags_from_mach (unsigned long mach);

/* Flags for the st_other symbol field.
   Keep away from the STV_ visibility flags (bit 0..1).  */

/* A reference to this symbol should by default add 1.  */
#define STO_SH5_ISA32 (1 << 2)

/* Section contains only SHmedia code (no SHcompact code).  */
#define SHF_SH5_ISA32		0x40000000

/* Section contains both SHmedia and SHcompact code, and possibly also
   constants.  */
#define SHF_SH5_ISA32_MIXED	0x20000000

/* If applied to a .cranges section, marks that the section is sorted by
   increasing cr_addr values.  */
#define SHT_SH5_CR_SORTED 0x80000001

/* Symbol should be handled as DataLabel (attached to global SHN_UNDEF
   symbols).  */
#define STT_DATALABEL STT_LOPROC

#include "elf/reloc-macros.h"

/* Relocations.  */
/* Relocations 10-32 and 128-255 are GNU extensions.
   25..32 and 10 are used for relaxation.  */
START_RELOC_NUMBERS (elf_sh_reloc_type)
  RELOC_NUMBER (R_SH_NONE, 0)
  RELOC_NUMBER (R_SH_DIR32, 1)
  RELOC_NUMBER (R_SH_REL32, 2)
  RELOC_NUMBER (R_SH_DIR8WPN, 3)
  RELOC_NUMBER (R_SH_IND12W, 4)
  RELOC_NUMBER (R_SH_DIR8WPL, 5)
  RELOC_NUMBER (R_SH_DIR8WPZ, 6)
  RELOC_NUMBER (R_SH_DIR8BP, 7)
  RELOC_NUMBER (R_SH_DIR8W, 8)
  RELOC_NUMBER (R_SH_DIR8L, 9)

  RELOC_NUMBER (R_SH_LOOP_START, 10)
  RELOC_NUMBER (R_SH_LOOP_END, 11)

  FAKE_RELOC (R_SH_FIRST_INVALID_RELOC, 12)
  FAKE_RELOC (R_SH_LAST_INVALID_RELOC, 21)

  RELOC_NUMBER (R_SH_GNU_VTINHERIT, 22)
  RELOC_NUMBER (R_SH_GNU_VTENTRY, 23)
  RELOC_NUMBER (R_SH_SWITCH8, 24)
  RELOC_NUMBER (R_SH_SWITCH16, 25)
  RELOC_NUMBER (R_SH_SWITCH32, 26)
  RELOC_NUMBER (R_SH_USES, 27)
  RELOC_NUMBER (R_SH_COUNT, 28)
  RELOC_NUMBER (R_SH_ALIGN, 29)
  RELOC_NUMBER (R_SH_CODE, 30)
  RELOC_NUMBER (R_SH_DATA, 31)
  RELOC_NUMBER (R_SH_LABEL, 32)

  RELOC_NUMBER (R_SH_DIR16, 33)
  RELOC_NUMBER (R_SH_DIR8, 34)
  RELOC_NUMBER (R_SH_DIR8UL, 35)
  RELOC_NUMBER (R_SH_DIR8UW, 36)
  RELOC_NUMBER (R_SH_DIR8U, 37)
  RELOC_NUMBER (R_SH_DIR8SW, 38)
  RELOC_NUMBER (R_SH_DIR8S, 39)
  RELOC_NUMBER (R_SH_DIR4UL, 40)
  RELOC_NUMBER (R_SH_DIR4UW, 41)
  RELOC_NUMBER (R_SH_DIR4U, 42)
  RELOC_NUMBER (R_SH_PSHA, 43)
  RELOC_NUMBER (R_SH_PSHL, 44)
  RELOC_NUMBER (R_SH_DIR5U, 45)
  RELOC_NUMBER (R_SH_DIR6U, 46)
  RELOC_NUMBER (R_SH_DIR6S, 47)
  RELOC_NUMBER (R_SH_DIR10S, 48)
  RELOC_NUMBER (R_SH_DIR10SW, 49)
  RELOC_NUMBER (R_SH_DIR10SL, 50)
  RELOC_NUMBER (R_SH_DIR10SQ, 51)
  FAKE_RELOC (R_SH_FIRST_INVALID_RELOC_2, 52)
  FAKE_RELOC (R_SH_LAST_INVALID_RELOC_2, 52)
  RELOC_NUMBER (R_SH_DIR16S, 53)
  FAKE_RELOC (R_SH_FIRST_INVALID_RELOC_3, 54)
  FAKE_RELOC (R_SH_LAST_INVALID_RELOC_3, 143)
  RELOC_NUMBER (R_SH_TLS_GD_32, 144)
  RELOC_NUMBER (R_SH_TLS_LD_32, 145)
  RELOC_NUMBER (R_SH_TLS_LDO_32, 146)
  RELOC_NUMBER (R_SH_TLS_IE_32, 147)
  RELOC_NUMBER (R_SH_TLS_LE_32, 148)
  RELOC_NUMBER (R_SH_TLS_DTPMOD32, 149)
  RELOC_NUMBER (R_SH_TLS_DTPOFF32, 150)
  RELOC_NUMBER (R_SH_TLS_TPOFF32, 151)
  FAKE_RELOC (R_SH_FIRST_INVALID_RELOC_4, 152)
  FAKE_RELOC (R_SH_LAST_INVALID_RELOC_4, 159)
  RELOC_NUMBER (R_SH_GOT32, 160)
  RELOC_NUMBER (R_SH_PLT32, 161)
  RELOC_NUMBER (R_SH_COPY, 162)
  RELOC_NUMBER (R_SH_GLOB_DAT, 163)
  RELOC_NUMBER (R_SH_JMP_SLOT, 164)
  RELOC_NUMBER (R_SH_RELATIVE, 165)
  RELOC_NUMBER (R_SH_GOTOFF, 166)
  RELOC_NUMBER (R_SH_GOTPC, 167)
  RELOC_NUMBER (R_SH_GOTPLT32, 168)
  RELOC_NUMBER (R_SH_GOT_LOW16, 169)
  RELOC_NUMBER (R_SH_GOT_MEDLOW16, 170)
  RELOC_NUMBER (R_SH_GOT_MEDHI16, 171)
  RELOC_NUMBER (R_SH_GOT_HI16, 172)
  RELOC_NUMBER (R_SH_GOTPLT_LOW16, 173)
  RELOC_NUMBER (R_SH_GOTPLT_MEDLOW16, 174)
  RELOC_NUMBER (R_SH_GOTPLT_MEDHI16, 175)
  RELOC_NUMBER (R_SH_GOTPLT_HI16, 176)
  RELOC_NUMBER (R_SH_PLT_LOW16, 177)
  RELOC_NUMBER (R_SH_PLT_MEDLOW16, 178)
  RELOC_NUMBER (R_SH_PLT_MEDHI16, 179)
  RELOC_NUMBER (R_SH_PLT_HI16, 180)
  RELOC_NUMBER (R_SH_GOTOFF_LOW16, 181)
  RELOC_NUMBER (R_SH_GOTOFF_MEDLOW16, 182)
  RELOC_NUMBER (R_SH_GOTOFF_MEDHI16, 183)
  RELOC_NUMBER (R_SH_GOTOFF_HI16, 184)
  RELOC_NUMBER (R_SH_GOTPC_LOW16, 185)
  RELOC_NUMBER (R_SH_GOTPC_MEDLOW16, 186)
  RELOC_NUMBER (R_SH_GOTPC_MEDHI16, 187)
  RELOC_NUMBER (R_SH_GOTPC_HI16, 188)
  RELOC_NUMBER (R_SH_GOT10BY4, 189)
  RELOC_NUMBER (R_SH_GOTPLT10BY4, 190)
  RELOC_NUMBER (R_SH_GOT10BY8, 191)
  RELOC_NUMBER (R_SH_GOTPLT10BY8, 192)
  RELOC_NUMBER (R_SH_COPY64, 193)
  RELOC_NUMBER (R_SH_GLOB_DAT64, 194)
  RELOC_NUMBER (R_SH_JMP_SLOT64, 195)
  RELOC_NUMBER (R_SH_RELATIVE64, 196)
  FAKE_RELOC (R_SH_FIRST_INVALID_RELOC_5, 197)
  FAKE_RELOC (R_SH_LAST_INVALID_RELOC_5, 241)
  RELOC_NUMBER (R_SH_SHMEDIA_CODE, 242)
  RELOC_NUMBER (R_SH_PT_16, 243)
  RELOC_NUMBER (R_SH_IMMS16, 244)
  RELOC_NUMBER (R_SH_IMMU16, 245)
  RELOC_NUMBER (R_SH_IMM_LOW16, 246)
  RELOC_NUMBER (R_SH_IMM_LOW16_PCREL, 247)
  RELOC_NUMBER (R_SH_IMM_MEDLOW16, 248)
  RELOC_NUMBER (R_SH_IMM_MEDLOW16_PCREL, 249)
  RELOC_NUMBER (R_SH_IMM_MEDHI16, 250)
  RELOC_NUMBER (R_SH_IMM_MEDHI16_PCREL, 251)
  RELOC_NUMBER (R_SH_IMM_HI16, 252)
  RELOC_NUMBER (R_SH_IMM_HI16_PCREL, 253)
  RELOC_NUMBER (R_SH_64, 254)
  RELOC_NUMBER (R_SH_64_PCREL, 255)
END_RELOC_NUMBERS (R_SH_max)

#endif

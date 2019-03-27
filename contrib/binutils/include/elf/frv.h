/* FRV ELF support for BFD.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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
along with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef _ELF_FRV_H
#define _ELF_FRV_H

#include "elf/reloc-macros.h"

/* Relocations.  */
START_RELOC_NUMBERS (elf_frv_reloc_type)
  RELOC_NUMBER (R_FRV_NONE, 0)
  RELOC_NUMBER (R_FRV_32, 1)
  RELOC_NUMBER (R_FRV_LABEL16, 2)
  RELOC_NUMBER (R_FRV_LABEL24, 3)
  RELOC_NUMBER (R_FRV_LO16, 4)
  RELOC_NUMBER (R_FRV_HI16, 5)
  RELOC_NUMBER (R_FRV_GPREL12, 6)
  RELOC_NUMBER (R_FRV_GPRELU12, 7)
  RELOC_NUMBER (R_FRV_GPREL32, 8)
  RELOC_NUMBER (R_FRV_GPRELHI, 9)
  RELOC_NUMBER (R_FRV_GPRELLO, 10)
  RELOC_NUMBER (R_FRV_GOT12, 11)
  RELOC_NUMBER (R_FRV_GOTHI, 12)
  RELOC_NUMBER (R_FRV_GOTLO, 13)
  RELOC_NUMBER (R_FRV_FUNCDESC, 14)
  RELOC_NUMBER (R_FRV_FUNCDESC_GOT12, 15)
  RELOC_NUMBER (R_FRV_FUNCDESC_GOTHI, 16)
  RELOC_NUMBER (R_FRV_FUNCDESC_GOTLO, 17)
  RELOC_NUMBER (R_FRV_FUNCDESC_VALUE, 18)
  RELOC_NUMBER (R_FRV_FUNCDESC_GOTOFF12, 19)
  RELOC_NUMBER (R_FRV_FUNCDESC_GOTOFFHI, 20)
  RELOC_NUMBER (R_FRV_FUNCDESC_GOTOFFLO, 21)
  RELOC_NUMBER (R_FRV_GOTOFF12, 22)
  RELOC_NUMBER (R_FRV_GOTOFFHI, 23)
  RELOC_NUMBER (R_FRV_GOTOFFLO, 24)
  RELOC_NUMBER (R_FRV_GETTLSOFF, 25)
  RELOC_NUMBER (R_FRV_TLSDESC_VALUE, 26)
  RELOC_NUMBER (R_FRV_GOTTLSDESC12, 27)
  RELOC_NUMBER (R_FRV_GOTTLSDESCHI, 28)
  RELOC_NUMBER (R_FRV_GOTTLSDESCLO, 29)
  RELOC_NUMBER (R_FRV_TLSMOFF12, 30)
  RELOC_NUMBER (R_FRV_TLSMOFFHI, 31)
  RELOC_NUMBER (R_FRV_TLSMOFFLO, 32)
  RELOC_NUMBER (R_FRV_GOTTLSOFF12, 33)
  RELOC_NUMBER (R_FRV_GOTTLSOFFHI, 34)
  RELOC_NUMBER (R_FRV_GOTTLSOFFLO, 35)
  RELOC_NUMBER (R_FRV_TLSOFF, 36)
  RELOC_NUMBER (R_FRV_TLSDESC_RELAX, 37)
  RELOC_NUMBER (R_FRV_GETTLSOFF_RELAX, 38)
  RELOC_NUMBER (R_FRV_TLSOFF_RELAX, 39)
  RELOC_NUMBER (R_FRV_TLSMOFF, 40)
  RELOC_NUMBER (R_FRV_GNU_VTINHERIT, 200)
  RELOC_NUMBER (R_FRV_GNU_VTENTRY, 201)
END_RELOC_NUMBERS(R_FRV_max)

/* Processor specific flags for the ELF header e_flags field.  */
						/* gpr support */
#define EF_FRV_GPR_MASK		0x00000003	/* mask for # of gprs */
#define EF_FRV_GPR_32		0x00000001	/* -mgpr-32 */
#define EF_FRV_GPR_64		0x00000002	/* -mgpr-64 */

						/* fpr support */
#define EF_FRV_FPR_MASK		0x0000000c	/* mask for # of fprs */
#define EF_FRV_FPR_32		0x00000004	/* -mfpr-32 */
#define EF_FRV_FPR_64		0x00000008	/* -mfpr-64 */
#define EF_FRV_FPR_NONE		0x0000000c	/* -msoft-float */

						/* double word support */
#define EF_FRV_DWORD_MASK	0x00000030	/* mask for dword support */
#define EF_FRV_DWORD_YES	0x00000010	/* use double word insns */
#define EF_FRV_DWORD_NO		0x00000020	/* don't use double word insn*/

#define EF_FRV_DOUBLE		0x00000040	/* -mdouble */
#define EF_FRV_MEDIA		0x00000080	/* -mmedia */

#define EF_FRV_PIC		0x00000100	/* -fpic */
#define EF_FRV_NON_PIC_RELOCS	0x00000200	/* used non pic safe relocs */

#define EF_FRV_MULADD		0x00000400	/* -mmuladd */
#define EF_FRV_BIGPIC		0x00000800	/* -fPIC */
#define	EF_FRV_LIBPIC		0x00001000	/* -mlibrary-pic */
#define EF_FRV_G0		0x00002000	/* -G 0, no small data ptr */
#define EF_FRV_NOPACK		0x00004000	/* -mnopack */
#define EF_FRV_FDPIC		0x00008000      /* -mfdpic */

#define	EF_FRV_CPU_MASK		0xff000000	/* specific cpu bits */
#define EF_FRV_CPU_GENERIC	0x00000000	/* generic FRV */
#define EF_FRV_CPU_FR500	0x01000000	/* FRV500 */
#define EF_FRV_CPU_FR300	0x02000000	/* FRV300 */
#define EF_FRV_CPU_SIMPLE	0x03000000	/* SIMPLE */
#define EF_FRV_CPU_TOMCAT	0x04000000	/* Tomcat, FR500 prototype */
#define EF_FRV_CPU_FR400	0x05000000	/* FRV400 */
#define EF_FRV_CPU_FR550	0x06000000	/* FRV550 */
#define EF_FRV_CPU_FR405	0x07000000
#define EF_FRV_CPU_FR450	0x08000000

						/* Mask of PIC related bits */
#define	EF_FRV_PIC_FLAGS	(EF_FRV_PIC | EF_FRV_LIBPIC | EF_FRV_BIGPIC \
				 | EF_FRV_FDPIC)

						/* Mask of all flags */
#define EF_FRV_ALL_FLAGS	(EF_FRV_GPR_MASK | \
				 EF_FRV_FPR_MASK | \
				 EF_FRV_DWORD_MASK | \
				 EF_FRV_DOUBLE | \
				 EF_FRV_MEDIA | \
				 EF_FRV_PIC_FLAGS | \
				 EF_FRV_NON_PIC_RELOCS | \
				 EF_FRV_MULADD | \
				 EF_FRV_G0 | \
				 EF_FRV_NOPACK | \
				 EF_FRV_CPU_MASK)

#endif /* _ELF_FRV_H */

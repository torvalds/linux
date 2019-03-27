/* Blackfin ELF support for BFD.
   Copyright (C) 2005 Free Software Foundation, Inc.

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

#ifndef _ELF_BFIN_H
#define _ELF_BFIN_H
			     
#include "elf/reloc-macros.h"

START_RELOC_NUMBERS (elf_bfin_reloc_type)
  RELOC_NUMBER (R_unused0, 0x00) /* relocation type 0 is not defined*/
  RELOC_NUMBER (R_pcrel5m2, 0x01) /*LSETUP part a*/
  RELOC_NUMBER (R_unused1, 0x02) /* relocation type 2 is not defined*/
  RELOC_NUMBER (R_pcrel10, 0x03) /* type 3, 0x00) if cc jump <target>  */
  RELOC_NUMBER (R_pcrel12_jump, 0x04) /* type 4, 0x00) jump <target> */
  RELOC_NUMBER (R_rimm16, 0x05)    /* type 0x5, 0x00) rN = <target> */
  RELOC_NUMBER (R_luimm16, 0x06)  /* # 0x6, 0x00) preg.l=<target> Load imm 16 to lower half */
  RELOC_NUMBER (R_huimm16, 0x07)  /* # 0x7, 0x00) preg.h=<target> Load imm 16 to upper half*/
  RELOC_NUMBER (R_pcrel12_jump_s, 0x08) /* # 0x8 jump.s <target> */
  RELOC_NUMBER (R_pcrel24_jump_x, 0x09) /* # 0x9 jump.x <target> */
  RELOC_NUMBER (R_pcrel24, 0x0a)        /* # 0xa call <target> , 0x00) not expandable*/
  RELOC_NUMBER (R_unusedb, 0x0b)         /* # 0xb not generated */
  RELOC_NUMBER (R_unusedc, 0x0c)       /* # 0xc  not used */
  RELOC_NUMBER (R_pcrel24_jump_l, 0x0d) /*0xd jump.l <target>*/
  RELOC_NUMBER (R_pcrel24_call_x, 0x0e) /* 0xE, 0x00) call.x <target> if <target> is above 24 bit limit call through P1 */
  RELOC_NUMBER (R_var_eq_symb, 0x0f)    /* 0xf, 0x00) linker should treat it same as 0x12 */
  RELOC_NUMBER (R_byte_data, 0x10)      /* 0x10, 0x00) .byte var = symbol */
  RELOC_NUMBER (R_byte2_data, 0x11)     /* 0x11, 0x00) .byte2 var = symbol */
  RELOC_NUMBER (R_byte4_data, 0x12)     /* 0x12, 0x00) .byte4 var = symbol and .var var=symbol */
  RELOC_NUMBER (R_pcrel11, 0x13)        /* 0x13, 0x00) lsetup part b */
  RELOC_NUMBER (R_BFIN_GOT17M4, 0x14)
  RELOC_NUMBER (R_BFIN_GOTHI, 0x15)
  RELOC_NUMBER (R_BFIN_GOTLO, 0x16)
  RELOC_NUMBER (R_BFIN_FUNCDESC, 0x17)
  RELOC_NUMBER (R_BFIN_FUNCDESC_GOT17M4, 0x18)
  RELOC_NUMBER (R_BFIN_FUNCDESC_GOTHI, 0x19)
  RELOC_NUMBER (R_BFIN_FUNCDESC_GOTLO, 0x1a)
  RELOC_NUMBER (R_BFIN_FUNCDESC_VALUE, 0x1b)
  RELOC_NUMBER (R_BFIN_FUNCDESC_GOTOFF17M4, 0x1c)
  RELOC_NUMBER (R_BFIN_FUNCDESC_GOTOFFHI, 0x1d)
  RELOC_NUMBER (R_BFIN_FUNCDESC_GOTOFFLO, 0x1e)
  RELOC_NUMBER (R_BFIN_GOTOFF17M4, 0x1f)
  RELOC_NUMBER (R_BFIN_GOTOFFHI, 0x20)
  RELOC_NUMBER (R_BFIN_GOTOFFLO, 0x21)

  RELOC_NUMBER (R_push, 0xE0)
  RELOC_NUMBER (R_const, 0xE1)
  RELOC_NUMBER (R_add, 0xE2)
  RELOC_NUMBER (R_sub, 0xE3)
  RELOC_NUMBER (R_mult, 0xE4)
  RELOC_NUMBER (R_div, 0xE5)
  RELOC_NUMBER (R_mod, 0xE6)
  RELOC_NUMBER (R_lshift, 0xE7)
  RELOC_NUMBER (R_rshift, 0xE8)
  RELOC_NUMBER (R_and, 0xE9)
  RELOC_NUMBER (R_or, 0xEA)
  RELOC_NUMBER (R_xor, 0xEB)
  RELOC_NUMBER (R_land, 0xEC)
  RELOC_NUMBER (R_lor, 0xED)
  RELOC_NUMBER (R_len, 0xEE)
  RELOC_NUMBER (R_neg, 0xEF)
  RELOC_NUMBER (R_comp, 0xF0)
  RELOC_NUMBER (R_page, 0xF1)
  RELOC_NUMBER (R_hwpage, 0xF2)
  RELOC_NUMBER (R_addr, 0xF3)
  RELOC_NUMBER (R_pltpc, 0x40)          /* PLT gnu only relocation */
  RELOC_NUMBER (R_got, 0x41)            /* GOT gnu only relocation */
  RELOC_NUMBER (R_BFIN_GNU_VTINHERIT, 0x42) /* C++, gnu only */
  RELOC_NUMBER (R_BFIN_GNU_VTENTRY, 0x43) /* C++, gnu only */
END_RELOC_NUMBERS (R_max)

/* Processor specific flags for the ELF header e_flags field.  */
#define EF_BFIN_PIC		0x00000001	/* -fpic */
#define EF_BFIN_FDPIC		0x00000002      /* -mfdpic */

#define	EF_BFIN_PIC_FLAGS	(EF_BFIN_PIC | EF_BFIN_FDPIC)
#endif /* _ELF_BFIN_H */

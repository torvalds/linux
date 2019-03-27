/* spu.h -- Assembler for spu

   Copyright 2006, 2007 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef TC_SPU
#define TC_SPU 1

#include "opcode/spu.h"

#define TARGET_FORMAT "elf32-spu"
#define TARGET_ARCH bfd_arch_spu
#define TARGET_NAME "elf32-spu"

#define TARGET_BYTES_BIG_ENDIAN 1

struct tc_fix_info {
  unsigned short arg_format;
  unsigned short insn_tag;
};

/* fixS will have a member named tc_fix_data of this type.  */
#define TC_FIX_TYPE struct tc_fix_info
#define TC_INIT_FIX_DATA(FIXP) \
  do						\
    {						\
      (FIXP)->tc_fix_data.arg_format = 0;	\
      (FIXP)->tc_fix_data.insn_tag = 0;		\
    }						\
  while (0)

/* Don't reduce function symbols to section symbols, and don't adjust
   references to PPU symbols.  */
#define tc_fix_adjustable(FIXP) \
  (!(S_IS_FUNCTION ((FIXP)->fx_addsy)			\
     || (FIXP)->fx_r_type == BFD_RELOC_SPU_PPU32	\
     || (FIXP)->fx_r_type == BFD_RELOC_SPU_PPU64))

/* Keep relocs on calls.  Branches to function symbols are tail or
   sibling calls.  */
#define TC_FORCE_RELOCATION(FIXP) \
  ((FIXP)->tc_fix_data.insn_tag == M_BRSL		\
   || (FIXP)->tc_fix_data.insn_tag == M_BRASL		\
   || (((FIXP)->tc_fix_data.insn_tag == M_BR		\
	|| (FIXP)->tc_fix_data.insn_tag == M_BRA)	\
       && (FIXP)->fx_addsy != NULL			\
       && S_IS_FUNCTION ((FIXP)->fx_addsy))		\
   || (FIXP)->fx_r_type == BFD_RELOC_SPU_PPU32		\
   || (FIXP)->fx_r_type == BFD_RELOC_SPU_PPU64		\
   || generic_force_reloc (FIXP))

/* Values passed to md_apply_fix don't include symbol values.  */
#define MD_APPLY_SYM_VALUE(FIX) 0

/* The spu uses pseudo-ops with no leading period.  */
#define NO_PSEUDO_DOT 1

/* Don't warn on word overflow; it happens on %hi relocs.  */
#undef WARN_SIGNED_OVERFLOW_WORD

#define DIFF_EXPR_OK

#define WORKING_DOT_WORD

#define md_number_to_chars number_to_chars_bigendian

#define md_convert_frag(b,s,f)		{as_fatal (_("spu convert_frag\n"));}

/* We don't need to do anything special for undefined symbols.  */
#define md_undefined_symbol(s) 0

extern symbolS *section_symbol (asection *);
#define md_operand(e) \
  do {								\
    if (strncasecmp (input_line_pointer, "@ppu", 4) == 0)	\
      {								\
	e->X_op = O_symbol;					\
	if (abs_section_sym == NULL)				\
	  abs_section_sym = section_symbol (absolute_section);	\
	e->X_add_symbol = abs_section_sym;			\
	e->X_add_number = 0;					\
      }								\
  } while (0)

/* Fill in rs_align_code fragments.  */
extern void spu_handle_align PARAMS ((fragS *));
#define HANDLE_ALIGN(frag)  spu_handle_align (frag)

#define MAX_MEM_FOR_RS_ALIGN_CODE  (7 + 8)

#endif /* TC_SPU */

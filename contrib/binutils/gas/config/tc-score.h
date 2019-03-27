/* tc-score.h -- Score specific file for assembler
   Copyright 2006 Free Software Foundation, Inc.
   Contributed by: 
   Mei Ligang (ligang@sunnorth.com.cn)
   Pei-Lin Tsai (pltsai@sunplus.com)
 
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
   Software Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#ifndef TC_SCORE
#define TC_SCORE

#define TARGET_ARCH 	            bfd_arch_score
#define WORKING_DOT_WORD
#define DIFF_EXPR_OK
#define RELOC_EXPANSION_POSSIBLE
#define MAX_RELOC_EXPANSION         2
#define MAX_MEM_FOR_RS_ALIGN_CODE  (3 + 4)

#define md_undefined_symbol(name)  NULL

#define TARGET_FORMAT  (target_big_endian ? "elf32-bigscore" : "elf32-littlescore")

#define md_relax_frag(segment, fragp, stretch)  score_relax_frag (segment, fragp, stretch)
extern int score_relax_frag (asection *, struct frag *, long);

#define md_frag_check(fragp)  score_frag_check (fragp)
extern void score_frag_check (fragS *);

#define TC_VALIDATE_FIX(FIXP, SEGTYPE, SKIP)  score_validate_fix (FIXP)
extern void score_validate_fix (struct fix *);

#define TC_FORCE_RELOCATION(FIXP)  score_force_relocation (FIXP)
extern int score_force_relocation (struct fix *);

#define tc_fix_adjustable(fixp)  score_fix_adjustable (fixp)
extern bfd_boolean score_fix_adjustable (struct fix *);

#define elf_tc_final_processing  score_elf_final_processing
extern void score_elf_final_processing (void);

struct score_tc_frag_data
{
  unsigned int is_insn;
  struct fix *fixp;
};

#define TC_FRAG_TYPE struct score_tc_frag_data

#define TC_FRAG_INIT(FRAGP) \
  do \
    { \
      (FRAGP)->tc_frag_data.is_insn = (((FRAGP)->fr_type == rs_machine_dependent) ? 1 : 0); \
    } \
  while (0)

#ifdef OBJ_ELF
#define GLOBAL_OFFSET_TABLE_NAME "_GLOBAL_OFFSET_TABLE_"
#else
#define GLOBAL_OFFSET_TABLE_NAME "__GLOBAL_OFFSET_TABLE_"
#endif

enum score_pic_level
{
  NO_PIC,
  PIC
};

#endif /*TC_SCORE */

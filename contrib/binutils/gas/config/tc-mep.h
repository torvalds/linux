/* tc-mep.h -- Header file for tc-mep.c.
   Copyright (C) 2001, 2002, 2005 Free Software Foundation, Inc.

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
   along with GAS; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA. */

#define TC_MEP

/* Support computed relocations.  */
#define OBJ_COMPLEX_RELC

/* Support many operands per instruction.  */
#define GAS_CGEN_MAX_FIXUPS 10

#define LISTING_HEADER "MEP GAS "

/* The target BFD architecture.  */
#define TARGET_ARCH bfd_arch_mep

#define TARGET_FORMAT (target_big_endian ? "elf32-mep" : "elf32-mep-little")

/* This is the default.  */
#define TARGET_BYTES_BIG_ENDIAN 1

/* Permit temporary numeric labels. */
#define LOCAL_LABELS_FB 1

/* .-foo gets turned into PC relative relocs.  */
#define DIFF_EXPR_OK

/* We don't need to handle .word strangely.  */
#define WORKING_DOT_WORD

/* Values passed to md_apply_fix don't include the symbol value.  */
#define MD_APPLY_SYM_VALUE(FIX) 0

#define MD_APPLY_FIX
#define md_apply_fix mep_apply_fix
extern void mep_apply_fix (struct fix *, valueT *, segT);

/* Call md_pcrel_from_section(), not md_pcrel_from().  */
#define MD_PCREL_FROM_SECTION(FIXP, SEC) md_pcrel_from_section (FIXP, SEC)
extern long md_pcrel_from_section (struct fix *, segT);

#define tc_frob_file() mep_frob_file ()
extern void mep_frob_file (void);

#define tc_fix_adjustable(fixP) mep_fix_adjustable (fixP)
extern bfd_boolean mep_fix_adjustable (struct fix *);

/* After creating a fixup for an instruction operand, we need
   to check for HI16 relocs and queue them up for later sorting.  */
#define md_cgen_record_fixup_exp  mep_cgen_record_fixup_exp

/* When relaxing, we need to emit various relocs we otherwise wouldn't.  */
#define TC_FORCE_RELOCATION(fix) mep_force_relocation (fix)
extern int mep_force_relocation (struct fix *);

#define tc_gen_reloc gas_cgen_tc_gen_reloc

extern void gas_cgen_md_operand (expressionS *);
#define md_operand(x) gas_cgen_md_operand (x)

#define md_flush_pending_output() mep_flush_pending_output()
extern int mep_flush_pending_output(void);

extern const struct relax_type md_relax_table[];
#define TC_GENERIC_RELAX_TABLE md_relax_table

/* Account for inserting a jmp after the insn.  */
#define TC_CGEN_MAX_RELAX(insn, len) ((len) + 4)

extern void mep_prepare_relax_scan (fragS *, offsetT *, relax_substateT);
#define md_prepare_relax_scan(FRAGP, ADDR, AIM, STATE, TYPE) \
	mep_prepare_relax_scan (FRAGP, &AIM, STATE)

#define skip_whitespace(str) while (*(str) == ' ') ++(str)

/* Support for core/vliw mode switching.  */
#define CORE 0
#define VLIW 1
#define MAX_PARALLEL_INSNS 56 /* From email from Toshiba.  */
#define VTEXT_SECTION_NAME ".vtext"

/* Needed to process pending instructions when a label is encountered.  */
#define TC_START_LABEL(ch, ptr)    ((ch == ':') && mep_flush_pending_output ())

#define tc_unrecognized_line(c) mep_unrecognized_line (c)
extern int mep_unrecognized_line (int);
#define md_cleanup mep_cleanup
extern void mep_cleanup (void);

#define md_elf_section_letter		mep_elf_section_letter
extern int mep_elf_section_letter (int, char **);
#define md_elf_section_flags		mep_elf_section_flags
extern flagword mep_elf_section_flags  (flagword, int, int);

#define ELF_TC_SPECIAL_SECTIONS \
  { VTEXT_SECTION_NAME, SHT_PROGBITS, SHF_ALLOC|SHF_EXECINSTR|SHF_MEP_VLIW },

/* The values of the following enum are for use with parinsnum, which 
   is a variable in md_assemble that keeps track of whether or not the
   next instruction is expected to be the first or second instrucion in
   a parallelization group.  */
typedef enum exp_par_insn_{FIRST, SECOND} EXP_PAR_INSN;

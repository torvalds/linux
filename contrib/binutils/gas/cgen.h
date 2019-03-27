/* GAS cgen support.
   Copyright 1998, 1999, 2000, 2001, 2002, 2003
   Free Software Foundation, Inc.

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

#ifndef GAS_CGEN_H
#define GAS_CGEN_H

/* Opcode table handle.  */
extern CGEN_CPU_DESC gas_cgen_cpu_desc;

/* Maximum number of fixups in an insn.
   If you need to change this, allow target to override and do so there.  */
#ifndef GAS_CGEN_MAX_FIXUPS
#define GAS_CGEN_MAX_FIXUPS 3
#endif

/* Struct defining result of gas_cgen_finish_insn.  */
typedef struct {
  /* frag containing the insn */
  fragS * frag;
  /* Address of insn in frag.  */
  char * addr;
  /* Number of fixups this insn has.  */
  int num_fixups;
  /* Array of fixups.  */
  fixS * fixups[GAS_CGEN_MAX_FIXUPS];
} finished_insnS;

/* Callback for operand parsing.
   The result is an error message or NULL for success.
   The parsed value is stored in the bfd_vma *.  */
extern const char * gas_cgen_parse_operand
     (CGEN_CPU_DESC, enum cgen_parse_operand_type,
      const char **, int, int, enum cgen_parse_operand_result *,
      bfd_vma *);

/* Call this from md_assemble to initialize the assembler callback.  */
extern void gas_cgen_init_parse (void);

/* Routines and macros for saving fixup chains.  */
extern void gas_cgen_save_fixups (int);
extern void gas_cgen_restore_fixups (int);
extern void gas_cgen_swap_fixups (int);
extern void gas_cgen_initialize_saved_fixups_array (void);
#define MAX_SAVED_FIXUP_CHAINS 50

/* Add a register to the assembler's hash table.
   This makes lets GAS parse registers for us.
   ??? This isn't currently used, but it could be in the future.  */
extern void cgen_asm_record_register (char *, int);

/* After CGEN_SYM (assemble_insn) is done, this is called to
   output the insn and record any fixups.  */
extern void gas_cgen_finish_insn (const CGEN_INSN *,
				  CGEN_INSN_BYTES_PTR, unsigned int,
				  int, finished_insnS *);

/* Record a fixup.  */
extern fixS * gas_cgen_record_fixup (fragS *, int, const CGEN_INSN *,
				     int, const CGEN_OPERAND *, int,
				     symbolS *, offsetT);
extern fixS * gas_cgen_record_fixup_exp (fragS *, int, const CGEN_INSN *,
					 int, const CGEN_OPERAND *, int,
					 expressionS *);

/* md_apply_fix handler */
extern void gas_cgen_md_apply_fix (fixS *, valueT *, segT);

/* tc_gen_reloc handler */
extern arelent *gas_cgen_tc_gen_reloc (asection *, fixS *);

/* Target supplied routine to lookup a reloc.  */
extern bfd_reloc_code_real_type
md_cgen_lookup_reloc (const CGEN_INSN *, const CGEN_OPERAND *, fixS *);

/* Optional target supplied routine to record a fixup for an expression.  */
extern fixS *
md_cgen_record_fixup_exp (fragS *, int, const CGEN_INSN *, int,
			  const CGEN_OPERAND *, int, expressionS *);

extern void gas_cgen_md_operand (expressionS *);

/* Perform any cgen specific initialisation for gas.  */
extern void gas_cgen_begin (void);

#endif /* GAS_CGEN_H */

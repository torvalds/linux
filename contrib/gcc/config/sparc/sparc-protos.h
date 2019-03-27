/* Prototypes of target machine for SPARC.
   Copyright (C) 1999, 2000, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com).
   64-bit SPARC-V9 support by Michael Tiemann, Jim Wilson, and Doug Evans,
   at Cygnus Support.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifndef __SPARC_PROTOS_H__
#define __SPARC_PROTOS_H__

#ifdef TREE_CODE
extern struct rtx_def *function_value (tree, enum machine_mode, int);
extern void function_arg_advance (CUMULATIVE_ARGS *,
				  enum machine_mode, tree, int);
extern struct rtx_def *function_arg (const CUMULATIVE_ARGS *,
				     enum machine_mode, tree, int, int);
#ifdef RTX_CODE
extern void init_cumulative_args (CUMULATIVE_ARGS *, tree, rtx, tree);
extern void sparc_va_start (tree, rtx);
#endif
extern unsigned long sparc_type_code (tree);
#ifdef ARGS_SIZE_RTX
/* expr.h defines ARGS_SIZE_RTX and `enum direction' */
extern enum direction function_arg_padding (enum machine_mode, tree);
#endif /* ARGS_SIZE_RTX */
#endif /* TREE_CODE */

extern void order_regs_for_local_alloc (void);
extern HOST_WIDE_INT sparc_compute_frame_size (HOST_WIDE_INT, int);
extern void sparc_expand_prologue (void);
extern void sparc_expand_epilogue (void);
extern bool sparc_can_use_return_insn_p (void);
extern int check_pic (int);
extern int short_branch (int, int);
extern void sparc_profile_hook (int);
extern void sparc_override_options (void);
extern void sparc_output_scratch_registers (FILE *);

#ifdef RTX_CODE
extern enum machine_mode select_cc_mode (enum rtx_code, rtx, rtx);
/* Define the function that build the compare insn for scc and bcc.  */
extern rtx gen_compare_reg (enum rtx_code code);
extern void sparc_emit_float_lib_cmp (rtx, rtx, enum rtx_code);
extern void sparc_emit_floatunsdi (rtx [2], enum machine_mode);
extern void sparc_emit_fixunsdi (rtx [2], enum machine_mode);
extern void emit_tfmode_binop (enum rtx_code, rtx *);
extern void emit_tfmode_unop (enum rtx_code, rtx *);
extern void emit_tfmode_cvt (enum rtx_code, rtx *);
/* This function handles all v9 scc insns */
extern int gen_v9_scc (enum rtx_code, rtx *);
extern void sparc_initialize_trampoline (rtx, rtx, rtx);
extern void sparc64_initialize_trampoline (rtx, rtx, rtx);
extern bool legitimate_constant_p (rtx);
extern bool constant_address_p (rtx);
extern bool legitimate_pic_operand_p (rtx);
extern int legitimate_address_p (enum machine_mode, rtx, int);
extern rtx legitimize_pic_address (rtx, enum machine_mode, rtx);
extern rtx legitimize_tls_address (rtx);
extern rtx legitimize_address (rtx, rtx, enum machine_mode);
extern void sparc_defer_case_vector (rtx, rtx, int);
extern bool sparc_expand_move (enum machine_mode, rtx *);
extern void sparc_emit_set_const32 (rtx, rtx);
extern void sparc_emit_set_const64 (rtx, rtx);
extern void sparc_emit_set_symbolic_const64 (rtx, rtx, rtx);
extern int sparc_splitdi_legitimate (rtx, rtx);
extern int sparc_absnegfloat_split_legitimate (rtx, rtx);
extern const char *output_ubranch (rtx, int, rtx);
extern const char *output_cbranch (rtx, rtx, int, int, int, rtx);
extern const char *output_return (rtx);
extern const char *output_sibcall (rtx, rtx);
extern const char *output_v8plus_shift (rtx *, rtx, const char *);
extern const char *output_v9branch (rtx, rtx, int, int, int, int, rtx);
extern void emit_v9_brxx_insn (enum rtx_code, rtx, rtx);
extern void print_operand (FILE *, rtx, int);
extern int mems_ok_for_ldd_peep (rtx, rtx, rtx);
extern int arith_double_4096_operand (rtx, enum machine_mode);
extern int arith_4096_operand (rtx, enum machine_mode);
extern int zero_operand (rtx, enum machine_mode);
extern int fp_zero_operand (rtx, enum machine_mode);
extern int reg_or_0_operand (rtx, enum machine_mode);
extern int empty_delay_slot (rtx);
extern int eligible_for_return_delay (rtx);
extern int eligible_for_sibcall_delay (rtx);
extern int tls_call_delay (rtx);
extern int emit_move_sequence (rtx, enum machine_mode);
extern int fp_sethi_p (rtx);
extern int fp_mov_p (rtx);
extern int fp_high_losum_p (rtx);
extern bool sparc_tls_referenced_p (rtx);
extern int mem_min_alignment (rtx, int);
extern int pic_address_needs_scratch (rtx);
extern int reg_unused_after (rtx, rtx);
extern int register_ok_for_ldd (rtx);
extern int registers_ok_for_ldd_peep (rtx, rtx);
extern int v9_regcmp_p (enum rtx_code);
/* Function used for V8+ code generation.  Returns 1 if the high
   32 bits of REG are 0 before INSN.  */   
extern int sparc_check_64 (rtx, rtx);
extern rtx gen_df_reg (rtx, int);
extern int sparc_extra_constraint_check (rtx, int, int);
extern void sparc_expand_compare_and_swap_12 (rtx, rtx, rtx, rtx);
#endif /* RTX_CODE */

#endif /* __SPARC_PROTOS_H__ */

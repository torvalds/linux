/* Definitions of target machine for GNU compiler, for IBM RS/6000.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Richard Kenner (kenner@vlsi1.ultra.nyu.edu)

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#ifndef GCC_RS6000_PROTOS_H
#define GCC_RS6000_PROTOS_H

/* Declare functions in rs6000.c */

#ifdef RTX_CODE

#ifdef TREE_CODE
extern void init_cumulative_args (CUMULATIVE_ARGS *, tree, rtx, int, int, int);
extern void rs6000_va_start (tree, rtx);
#endif /* TREE_CODE */

extern bool easy_altivec_constant (rtx, enum machine_mode);
extern bool macho_lo_sum_memory_operand (rtx, enum machine_mode);
extern int num_insns_constant (rtx, enum machine_mode);
extern int num_insns_constant_wide (HOST_WIDE_INT);
extern int small_data_operand (rtx, enum machine_mode);
extern bool toc_relative_expr_p (rtx);
extern bool invalid_e500_subreg (rtx, enum machine_mode);
extern void validate_condition_mode (enum rtx_code, enum machine_mode);
extern bool legitimate_constant_pool_address_p (rtx);
extern bool legitimate_indirect_address_p (rtx, int);

extern rtx rs6000_got_register (rtx);
extern rtx find_addr_reg (rtx);
extern rtx gen_easy_altivec_constant (rtx);
extern const char *output_vec_const_move (rtx *);
extern void rs6000_expand_vector_init (rtx, rtx);
extern void rs6000_expand_vector_set (rtx, rtx, int);
extern void rs6000_expand_vector_extract (rtx, rtx, int);
extern void build_mask64_2_operands (rtx, rtx *);
extern int expand_block_clear (rtx[]);
extern int expand_block_move (rtx[]);
extern const char * rs6000_output_load_multiple (rtx[]);
extern int includes_lshift_p (rtx, rtx);
extern int includes_rshift_p (rtx, rtx);
extern int includes_rldic_lshift_p (rtx, rtx);
extern int includes_rldicr_lshift_p (rtx, rtx);
extern int insvdi_rshift_rlwimi_p (rtx, rtx, rtx);
extern int registers_ok_for_quad_peep (rtx, rtx);
extern int mems_ok_for_quad_peep (rtx, rtx);
extern bool gpr_or_gpr_p (rtx, rtx);
extern enum reg_class rs6000_secondary_reload_class (enum reg_class,
						     enum machine_mode, rtx);
extern int ccr_bit (rtx, int);
extern int extract_MB (rtx);
extern int extract_ME (rtx);
extern void rs6000_output_function_entry (FILE *, const char *);
extern void print_operand (FILE *, rtx, int);
extern void print_operand_address (FILE *, rtx);
extern enum rtx_code rs6000_reverse_condition (enum machine_mode,
					       enum rtx_code);
extern void rs6000_emit_sCOND (enum rtx_code, rtx);
extern void rs6000_emit_cbranch (enum rtx_code, rtx);
extern char * output_cbranch (rtx, const char *, int, rtx);
extern char * output_e500_flip_gt_bit (rtx, rtx);
extern rtx rs6000_emit_set_const (rtx, enum machine_mode, rtx, int);
extern int rs6000_emit_cmove (rtx, rtx, rtx, rtx);
extern int rs6000_emit_vector_cond_expr (rtx, rtx, rtx, rtx, rtx, rtx);
extern void rs6000_emit_minmax (rtx, enum rtx_code, rtx, rtx);
extern void rs6000_emit_sync (enum rtx_code, enum machine_mode,
			      rtx, rtx, rtx, rtx, bool);
extern void rs6000_split_atomic_op (enum rtx_code, rtx, rtx, rtx, rtx, rtx);
extern void rs6000_split_compare_and_swap (rtx, rtx, rtx, rtx, rtx);
extern void rs6000_expand_compare_and_swapqhi (rtx, rtx, rtx, rtx);
extern void rs6000_split_compare_and_swapqhi (rtx, rtx, rtx, rtx, rtx, rtx);
extern void rs6000_split_lock_test_and_set (rtx, rtx, rtx, rtx);
extern void rs6000_emit_swdivsf (rtx, rtx, rtx);
extern void rs6000_emit_swdivdf (rtx, rtx, rtx);
extern void output_toc (FILE *, rtx, int, enum machine_mode);
extern void rs6000_initialize_trampoline (rtx, rtx, rtx);
extern rtx rs6000_longcall_ref (rtx);
extern void rs6000_fatal_bad_address (rtx);
extern rtx create_TOC_reference (rtx);
extern void rs6000_split_multireg_move (rtx, rtx);
extern void rs6000_emit_move (rtx, rtx, enum machine_mode);
extern rtx rs6000_legitimize_address (rtx, rtx, enum machine_mode);
extern rtx rs6000_legitimize_reload_address (rtx, enum machine_mode,
					     int, int, int, int *);
extern int rs6000_legitimate_address (enum machine_mode, rtx, int);
extern bool rs6000_legitimate_offset_address_p (enum machine_mode, rtx, int);
extern bool rs6000_mode_dependent_address (rtx);
extern bool rs6000_offsettable_memref_p (rtx);
extern rtx rs6000_return_addr (int, rtx);
extern void rs6000_output_symbol_ref (FILE*, rtx);
extern HOST_WIDE_INT rs6000_initial_elimination_offset (int, int);

extern rtx rs6000_machopic_legitimize_pic_address (rtx, enum machine_mode,
						   rtx);
#endif /* RTX_CODE */

#ifdef TREE_CODE
extern unsigned int rs6000_special_round_type_align (tree, unsigned int,
						     unsigned int);
extern void function_arg_advance (CUMULATIVE_ARGS *, enum machine_mode,
				  tree, int, int);
extern int function_arg_boundary (enum machine_mode, tree);
extern rtx function_arg (CUMULATIVE_ARGS *, enum machine_mode, tree, int);
extern tree altivec_resolve_overloaded_builtin (tree, tree);
extern rtx rs6000_function_value (tree, tree);
extern rtx rs6000_libcall_value (enum machine_mode);
extern rtx rs6000_va_arg (tree, tree);
extern int function_ok_for_sibcall (tree);
extern void rs6000_elf_declare_function_name (FILE *, const char *, tree);
extern bool rs6000_elf_in_small_data_p (tree);
#ifdef ARGS_SIZE_RTX
/* expr.h defines ARGS_SIZE_RTX and `enum direction' */
extern enum direction function_arg_padding (enum machine_mode, tree);
#endif /* ARGS_SIZE_RTX */

#endif /* TREE_CODE */

extern void optimization_options (int, int);
extern void rs6000_override_options (const char *);
extern int direct_return (void);
extern int first_reg_to_save (void);
extern int first_fp_reg_to_save (void);
extern void output_ascii (FILE *, const char *, int);
extern void rs6000_gen_section_name (char **, const char *, const char *);
extern void output_function_profiler (FILE *, int);
extern void output_profile_hook  (int);
extern int rs6000_trampoline_size (void);
extern int get_TOC_alias_set (void);
extern void rs6000_emit_prologue (void);
extern void rs6000_emit_load_toc_table (int);
extern void rs6000_aix_emit_builtin_unwind_init (void);
extern unsigned int rs6000_dbx_register_number (unsigned int);
extern void rs6000_emit_epilogue (int);
extern void rs6000_emit_eh_reg_restore (rtx, rtx);
extern const char * output_isel (rtx *);
extern int rs6000_register_move_cost (enum machine_mode,
				      enum reg_class, enum reg_class);
extern int rs6000_memory_move_cost (enum machine_mode, enum reg_class, int);
extern bool rs6000_tls_referenced_p (rtx);
extern int rs6000_hard_regno_nregs (int, enum machine_mode);
extern void rs6000_conditional_register_usage (void);

/* Declare functions in rs6000-c.c */

extern void rs6000_pragma_longcall (struct cpp_reader *);
extern void rs6000_cpu_cpp_builtins (struct cpp_reader *);

#if TARGET_MACHO
char *output_call (rtx, rtx *, int, int);
#endif

extern bool rs6000_hard_regno_mode_ok_p[][FIRST_PSEUDO_REGISTER];
#endif  /* rs6000-protos.h */

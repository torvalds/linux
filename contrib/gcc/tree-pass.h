/* Definitions for describing one tree-ssa optimization pass.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>

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


#ifndef GCC_TREE_PASS_H
#define GCC_TREE_PASS_H 1

/* In tree-dump.c */

/* Different tree dump places.  When you add new tree dump places,
   extend the DUMP_FILES array in tree-dump.c.  */
enum tree_dump_index
{
  TDI_none,			/* No dump */
  TDI_cgraph,                   /* dump function call graph.  */
  TDI_tu,			/* dump the whole translation unit.  */
  TDI_class,			/* dump class hierarchy.  */
  TDI_original,			/* dump each function before optimizing it */
  TDI_generic,			/* dump each function after genericizing it */
  TDI_nested,			/* dump each function after unnesting it */
  TDI_inlined,			/* dump each function after inlining
				   within it.  */
  TDI_vcg,			/* create a VCG graph file for each
				   function's flowgraph.  */
  TDI_tree_all,                 /* enable all the GENERIC/GIMPLE dumps.  */
  TDI_rtl_all,                  /* enable all the RTL dumps.  */
  TDI_ipa_all,                  /* enable all the IPA dumps.  */

  TDI_end
};

/* Bit masks to control dumping. Not all values are applicable to
   all dumps. Add new ones at the end. When you define new
   values, extend the DUMP_OPTIONS array in tree-dump.c */
#define TDF_ADDRESS	(1 << 0)	/* dump node addresses */
#define TDF_SLIM	(1 << 1)	/* don't go wild following links */
#define TDF_RAW  	(1 << 2)	/* don't unparse the function */
#define TDF_DETAILS	(1 << 3)	/* show more detailed info about
					   each pass */
#define TDF_STATS	(1 << 4)	/* dump various statistics about
					   each pass */
#define TDF_BLOCKS	(1 << 5)	/* display basic block boundaries */
#define TDF_VOPS	(1 << 6)	/* display virtual operands */
#define TDF_LINENO	(1 << 7)	/* display statement line numbers */
#define TDF_UID		(1 << 8)	/* display decl UIDs */

#define TDF_TREE	(1 << 9)	/* is a tree dump */
#define TDF_RTL		(1 << 10)	/* is a RTL dump */
#define TDF_IPA		(1 << 11)	/* is an IPA dump */
#define TDF_STMTADDR	(1 << 12)	/* Address of stmt.  */

#define TDF_GRAPH	(1 << 13)	/* a graph dump is being emitted */

extern char *get_dump_file_name (enum tree_dump_index);
extern int dump_enabled_p (enum tree_dump_index);
extern int dump_initialized_p (enum tree_dump_index);
extern FILE *dump_begin (enum tree_dump_index, int *);
extern void dump_end (enum tree_dump_index, FILE *);
extern void dump_node (tree, int, FILE *);
extern int dump_switch_p (const char *);
extern const char *dump_flag_name (enum tree_dump_index);

/* Global variables used to communicate with passes.  */
extern FILE *dump_file;
extern int dump_flags;
extern const char *dump_file_name;

/* Return the dump_file_info for the given phase.  */
extern struct dump_file_info *get_dump_file_info (enum tree_dump_index);

/* Describe one pass.  */
struct tree_opt_pass
{
  /* Terse name of the pass used as a fragment of the dump file name.  */
  const char *name;

  /* If non-null, this pass and all sub-passes are executed only if
     the function returns true.  */
  bool (*gate) (void);

  /* This is the code to run.  If null, then there should be sub-passes
     otherwise this pass does nothing.  The return value contains
     TODOs to execute in addition to those in TODO_flags_finish.   */
  unsigned int (*execute) (void);

  /* A list of sub-passes to run, dependent on gate predicate.  */
  struct tree_opt_pass *sub;

  /* Next in the list of passes to run, independent of gate predicate.  */
  struct tree_opt_pass *next;

  /* Static pass number, used as a fragment of the dump file name.  */
  int static_pass_number;

  /* The timevar id associated with this pass.  */
  /* ??? Ideally would be dynamically assigned.  */
  unsigned int tv_id;

  /* Sets of properties input and output from this pass.  */
  unsigned int properties_required;
  unsigned int properties_provided;
  unsigned int properties_destroyed;

  /* Flags indicating common sets things to do before and after.  */
  unsigned int todo_flags_start;
  unsigned int todo_flags_finish;

  /* Letter for RTL dumps.  */
  char letter;
};

/* Define a tree dump switch.  */
struct dump_file_info
{
  const char *suffix;           /* suffix to give output file.  */
  const char *swtch;            /* command line switch */
  const char *glob;             /* command line glob  */
  int flags;                    /* user flags */
  int state;                    /* state of play */
  int num;                      /* dump file number */
  int letter;                   /* enabling letter for RTL dumps */
};

/* Pass properties.  */
#define PROP_gimple_any		(1 << 0)	/* entire gimple grammar */
#define PROP_gimple_lcf		(1 << 1)	/* lowered control flow */
#define PROP_gimple_leh		(1 << 2)	/* lowered eh */
#define PROP_cfg		(1 << 3)
#define PROP_referenced_vars	(1 << 4)
#define PROP_pta		(1 << 5)
#define PROP_ssa		(1 << 6)
#define PROP_no_crit_edges      (1 << 7)
#define PROP_rtl		(1 << 8)
#define PROP_alias		(1 << 9)
#define PROP_gimple_lomp	(1 << 10)	/* lowered OpenMP directives */
#define PROP_smt_usage          (1 << 11)       /* which SMT's are
						   used alone.  */

#define PROP_trees \
  (PROP_gimple_any | PROP_gimple_lcf | PROP_gimple_leh | PROP_gimple_lomp)

/* To-do flags.  */
#define TODO_dump_func			(1 << 0)
#define TODO_ggc_collect		(1 << 1)
#define TODO_verify_ssa			(1 << 2) 
#define TODO_verify_flow		(1 << 3)
#define TODO_verify_stmts		(1 << 4)
#define TODO_cleanup_cfg        	(1 << 5)
#define TODO_verify_loops		(1 << 6)
#define TODO_dump_cgraph		(1 << 7)

/* To-do flags for calls to update_ssa.  */

/* Update the SSA form inserting PHI nodes for newly exposed symbols
   and virtual names marked for updating.  When updating real names,
   only insert PHI nodes for a real name O_j in blocks reached by all
   the new and old definitions for O_j.  If the iterated dominance
   frontier for O_j is not pruned, we may end up inserting PHI nodes
   in blocks that have one or more edges with no incoming definition
   for O_j.  This would lead to uninitialized warnings for O_j's
   symbol.  */
#define TODO_update_ssa			(1 << 8)

/* Update the SSA form without inserting any new PHI nodes at all.
   This is used by passes that have either inserted all the PHI nodes
   themselves or passes that need only to patch use-def and def-def
   chains for virtuals (e.g., DCE).  */
#define TODO_update_ssa_no_phi		(1 << 9)

/* Insert PHI nodes everywhere they are needed.  No pruning of the
   IDF is done.  This is used by passes that need the PHI nodes for
   O_j even if it means that some arguments will come from the default
   definition of O_j's symbol (e.g., pass_linear_transform).
   
   WARNING: If you need to use this flag, chances are that your pass
   may be doing something wrong.  Inserting PHI nodes for an old name
   where not all edges carry a new replacement may lead to silent
   codegen errors or spurious uninitialized warnings.  */
#define TODO_update_ssa_full_phi	(1 << 10)

/* Passes that update the SSA form on their own may want to delegate
   the updating of virtual names to the generic updater.  Since FUD
   chains are easier to maintain, this simplifies the work they need
   to do.  NOTE: If this flag is used, any OLD->NEW mappings for real
   names are explicitly destroyed and only the symbols marked for
   renaming are processed.  */
#define TODO_update_ssa_only_virtuals	(1 << 11)

/* Some passes leave unused local variables that can be removed from
   cfun->unexpanded_var_list.  This reduces the size of dump files and
   the memory footprint for VAR_DECLs.  */
#define TODO_remove_unused_locals	(1 << 12)

/* Internally used for the first in a sequence of passes.  It is set
   for the passes that are handed to register_dump_files.  */
#define TODO_set_props			(1 << 13)

/* Set by passes that may make SMT's that were previously never used
   in statements, used.  */
#define TODO_update_smt_usage           (1 << 14)

#define TODO_update_ssa_any		\
    (TODO_update_ssa			\
     | TODO_update_ssa_no_phi		\
     | TODO_update_ssa_full_phi		\
     | TODO_update_ssa_only_virtuals)

#define TODO_verify_all \
  (TODO_verify_ssa | TODO_verify_flow | TODO_verify_stmts)

extern void tree_lowering_passes (tree decl);

extern struct tree_opt_pass pass_mudflap_1;
extern struct tree_opt_pass pass_mudflap_2;
extern struct tree_opt_pass pass_remove_useless_stmts;
extern struct tree_opt_pass pass_lower_cf;
extern struct tree_opt_pass pass_lower_eh;
extern struct tree_opt_pass pass_build_cfg;
extern struct tree_opt_pass pass_tree_profile;
extern struct tree_opt_pass pass_early_tree_profile;
extern struct tree_opt_pass pass_cleanup_cfg;
extern struct tree_opt_pass pass_referenced_vars;
extern struct tree_opt_pass pass_sra;
extern struct tree_opt_pass pass_tail_recursion;
extern struct tree_opt_pass pass_tail_calls;
extern struct tree_opt_pass pass_tree_loop;
extern struct tree_opt_pass pass_tree_loop_init;
extern struct tree_opt_pass pass_lim;
extern struct tree_opt_pass pass_tree_unswitch;
extern struct tree_opt_pass pass_iv_canon;
extern struct tree_opt_pass pass_scev_cprop;
extern struct tree_opt_pass pass_empty_loop;
extern struct tree_opt_pass pass_record_bounds;
extern struct tree_opt_pass pass_if_conversion;
extern struct tree_opt_pass pass_vectorize;
extern struct tree_opt_pass pass_complete_unroll;
extern struct tree_opt_pass pass_loop_prefetch;
extern struct tree_opt_pass pass_iv_optimize;
extern struct tree_opt_pass pass_tree_loop_done;
extern struct tree_opt_pass pass_ch;
extern struct tree_opt_pass pass_ccp;
extern struct tree_opt_pass pass_phi_only_cprop;
extern struct tree_opt_pass pass_build_ssa;
extern struct tree_opt_pass pass_del_ssa;
extern struct tree_opt_pass pass_dominator;
extern struct tree_opt_pass pass_dce;
extern struct tree_opt_pass pass_dce_loop;
extern struct tree_opt_pass pass_cd_dce;
extern struct tree_opt_pass pass_merge_phi;
extern struct tree_opt_pass pass_may_alias;
extern struct tree_opt_pass pass_split_crit_edges;
extern struct tree_opt_pass pass_pre;
extern struct tree_opt_pass pass_profile;
extern struct tree_opt_pass pass_lower_complex_O0;
extern struct tree_opt_pass pass_lower_complex;
extern struct tree_opt_pass pass_lower_vector;
extern struct tree_opt_pass pass_lower_vector_ssa;
extern struct tree_opt_pass pass_lower_omp;
extern struct tree_opt_pass pass_expand_omp;
extern struct tree_opt_pass pass_object_sizes;
extern struct tree_opt_pass pass_fold_builtins;
extern struct tree_opt_pass pass_stdarg;
extern struct tree_opt_pass pass_early_warn_uninitialized;
extern struct tree_opt_pass pass_late_warn_uninitialized;
extern struct tree_opt_pass pass_cse_reciprocals;
extern struct tree_opt_pass pass_warn_function_return;
extern struct tree_opt_pass pass_warn_function_noreturn;
extern struct tree_opt_pass pass_phiopt;
extern struct tree_opt_pass pass_forwprop;
extern struct tree_opt_pass pass_redundant_phi;
extern struct tree_opt_pass pass_dse;
extern struct tree_opt_pass pass_nrv;
extern struct tree_opt_pass pass_mark_used_blocks;
extern struct tree_opt_pass pass_rename_ssa_copies;
extern struct tree_opt_pass pass_expand;
extern struct tree_opt_pass pass_rest_of_compilation;
extern struct tree_opt_pass pass_sink_code;
extern struct tree_opt_pass pass_fre;
extern struct tree_opt_pass pass_linear_transform;
extern struct tree_opt_pass pass_copy_prop;
extern struct tree_opt_pass pass_store_ccp;
extern struct tree_opt_pass pass_store_copy_prop;
extern struct tree_opt_pass pass_vrp;
extern struct tree_opt_pass pass_create_structure_vars;
extern struct tree_opt_pass pass_uncprop;
extern struct tree_opt_pass pass_return_slot;
extern struct tree_opt_pass pass_reassoc;
extern struct tree_opt_pass pass_rebuild_cgraph_edges;
extern struct tree_opt_pass pass_reset_cc_flags;

/* IPA Passes */
extern struct tree_opt_pass pass_ipa_cp;
extern struct tree_opt_pass pass_ipa_inline;
extern struct tree_opt_pass pass_early_ipa_inline;
extern struct tree_opt_pass pass_ipa_reference;
extern struct tree_opt_pass pass_ipa_pure_const;
extern struct tree_opt_pass pass_ipa_type_escape;
extern struct tree_opt_pass pass_ipa_pta;
extern struct tree_opt_pass pass_early_local_passes;

extern struct tree_opt_pass pass_all_optimizations;
extern struct tree_opt_pass pass_cleanup_cfg_post_optimizing;
extern struct tree_opt_pass pass_free_cfg_annotations;
extern struct tree_opt_pass pass_free_datastructures;
extern struct tree_opt_pass pass_init_datastructures;
extern struct tree_opt_pass pass_fixup_cfg;

extern struct tree_opt_pass pass_init_function;
extern struct tree_opt_pass pass_jump;
extern struct tree_opt_pass pass_insn_locators_initialize;
extern struct tree_opt_pass pass_rtl_eh;
extern struct tree_opt_pass pass_initial_value_sets;
extern struct tree_opt_pass pass_unshare_all_rtl;
extern struct tree_opt_pass pass_instantiate_virtual_regs;
extern struct tree_opt_pass pass_jump2;
extern struct tree_opt_pass pass_cse;
extern struct tree_opt_pass pass_gcse;
extern struct tree_opt_pass pass_jump_bypass;
extern struct tree_opt_pass pass_profiling;
extern struct tree_opt_pass pass_rtl_ifcvt;
extern struct tree_opt_pass pass_tracer;

extern struct tree_opt_pass pass_loop2;
extern struct tree_opt_pass pass_rtl_loop_init;
extern struct tree_opt_pass pass_rtl_move_loop_invariants;
extern struct tree_opt_pass pass_rtl_unswitch;
extern struct tree_opt_pass pass_rtl_unroll_and_peel_loops;
extern struct tree_opt_pass pass_rtl_doloop;
extern struct tree_opt_pass pass_rtl_loop_done;

extern struct tree_opt_pass pass_web;
extern struct tree_opt_pass pass_cse2;
extern struct tree_opt_pass pass_life;
extern struct tree_opt_pass pass_combine;
extern struct tree_opt_pass pass_if_after_combine;
extern struct tree_opt_pass pass_partition_blocks;
extern struct tree_opt_pass pass_partition_blocks;
extern struct tree_opt_pass pass_regmove;
extern struct tree_opt_pass pass_split_all_insns;
extern struct tree_opt_pass pass_mode_switching;
extern struct tree_opt_pass pass_see;
extern struct tree_opt_pass pass_recompute_reg_usage;
extern struct tree_opt_pass pass_sms;
extern struct tree_opt_pass pass_sched;
extern struct tree_opt_pass pass_local_alloc;
extern struct tree_opt_pass pass_global_alloc;
extern struct tree_opt_pass pass_postreload;
extern struct tree_opt_pass pass_clean_state;
extern struct tree_opt_pass pass_branch_prob;
extern struct tree_opt_pass pass_value_profile_transformations;
extern struct tree_opt_pass pass_remove_death_notes;
extern struct tree_opt_pass pass_postreload_cse;
extern struct tree_opt_pass pass_gcse2;
extern struct tree_opt_pass pass_flow2;
extern struct tree_opt_pass pass_stack_adjustments;
extern struct tree_opt_pass pass_peephole2;
extern struct tree_opt_pass pass_if_after_reload;
extern struct tree_opt_pass pass_regrename;
extern struct tree_opt_pass pass_reorder_blocks;
extern struct tree_opt_pass pass_branch_target_load_optimize;
extern struct tree_opt_pass pass_leaf_regs;
extern struct tree_opt_pass pass_sched2;
extern struct tree_opt_pass pass_stack_regs;
extern struct tree_opt_pass pass_compute_alignments;
extern struct tree_opt_pass pass_duplicate_computed_gotos;
extern struct tree_opt_pass pass_variable_tracking;
extern struct tree_opt_pass pass_free_cfg;
extern struct tree_opt_pass pass_machine_reorg;
extern struct tree_opt_pass pass_purge_lineno_notes;
extern struct tree_opt_pass pass_cleanup_barriers;
extern struct tree_opt_pass pass_delay_slots;
extern struct tree_opt_pass pass_split_for_shorten_branches;
extern struct tree_opt_pass pass_split_before_regstack;
extern struct tree_opt_pass pass_convert_to_eh_region_ranges;
extern struct tree_opt_pass pass_shorten_branches;
extern struct tree_opt_pass pass_set_nothrow_function_flags;
extern struct tree_opt_pass pass_final;
extern struct tree_opt_pass pass_rtl_seqabstr;

/* The root of the compilation pass tree, once constructed.  */
extern struct tree_opt_pass *all_passes, *all_ipa_passes, *all_lowering_passes;

extern void execute_pass_list (struct tree_opt_pass *);
extern void execute_ipa_pass_list (struct tree_opt_pass *);

#endif /* GCC_TREE_PASS_H */

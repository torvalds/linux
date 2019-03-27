/* Default macros to initialize the lang_hooks data structure.
   Copyright 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Alexandre Oliva  <aoliva@redhat.com>

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

#ifndef GCC_LANG_HOOKS_DEF_H
#define GCC_LANG_HOOKS_DEF_H

#include "hooks.h"

struct diagnostic_context;

/* Provide a hook routine for alias sets that always returns 1.  This is
   used by languages that haven't deal with alias sets yet.  */
extern HOST_WIDE_INT hook_get_alias_set_0 (tree);

/* Note to creators of new hooks:

   The macros in this file should NOT be surrounded by a
   #ifdef...#endif pair, since this file declares the defaults.  Each
   front end overrides any hooks it wishes to, in the file containing
   its struct lang_hooks, AFTER including this file.  */

/* See langhooks.h for the definition and documentation of each hook.  */

extern void lhd_do_nothing (void);
extern void lhd_do_nothing_t (tree);
extern void lhd_do_nothing_i (int);
extern void lhd_do_nothing_f (struct function *);
extern bool lhd_post_options (const char **);
extern HOST_WIDE_INT lhd_get_alias_set (tree);
extern tree lhd_return_tree (tree);
extern tree lhd_return_null_tree_v (void);
extern tree lhd_return_null_tree (tree);
extern tree lhd_do_nothing_iii_return_null_tree (int, int, int);
extern int lhd_safe_from_p (rtx, tree);
extern tree lhd_staticp (tree);
extern void lhd_print_tree_nothing (FILE *, tree, int);
extern const char *lhd_decl_printable_name (tree, int);
extern const char *lhd_dwarf_name (tree, int);
extern int lhd_types_compatible_p (tree, tree);
extern rtx lhd_expand_expr (tree, rtx, enum machine_mode, int, rtx *);
extern int lhd_expand_decl (tree);
extern void lhd_print_error_function (struct diagnostic_context *,
				      const char *);
extern void lhd_set_decl_assembler_name (tree);
extern bool lhd_can_use_bit_fields_p (void);
extern bool lhd_warn_unused_global_decl (tree);
extern void lhd_incomplete_type_error (tree, tree);
extern tree lhd_type_promotes_to (tree);
extern void lhd_register_builtin_type (tree, const char *);
extern bool lhd_decl_ok_for_sibcall (tree);
extern const char *lhd_comdat_group (tree);
extern tree lhd_expr_size (tree);
extern size_t lhd_tree_size (enum tree_code);
extern HOST_WIDE_INT lhd_to_target_charset (HOST_WIDE_INT);
extern tree lhd_expr_to_decl (tree, bool *, bool *, bool *);

/* Declarations of default tree inlining hooks.  */
extern tree lhd_tree_inlining_walk_subtrees (tree *, int *, walk_tree_fn,
					     void *, struct pointer_set_t*);
extern int lhd_tree_inlining_cannot_inline_tree_fn (tree *);
extern int lhd_tree_inlining_disregard_inline_limits (tree);
extern tree lhd_tree_inlining_add_pending_fn_decls (void *, tree);
extern int lhd_tree_inlining_auto_var_in_fn_p (tree, tree);
extern int lhd_tree_inlining_anon_aggr_type_p (tree);
extern int lhd_tree_inlining_start_inlining (tree);
extern void lhd_tree_inlining_end_inlining (tree);
extern tree lhd_tree_inlining_convert_parm_for_inlining (tree, tree, tree, int);
extern void lhd_initialize_diagnostics (struct diagnostic_context *);
extern tree lhd_callgraph_analyze_expr (tree *, int *, tree);


/* Declarations for tree gimplification hooks.  */
extern int lhd_gimplify_expr (tree *, tree *, tree *);
extern enum omp_clause_default_kind lhd_omp_predetermined_sharing (tree);
extern tree lhd_omp_assignment (tree, tree, tree);
struct gimplify_omp_ctx;
extern void lhd_omp_firstprivatize_type_sizes (struct gimplify_omp_ctx *,
					       tree);

/* APPLE LOCAL begin 6353006  */
extern tree lhd_build_generic_block_struct_type (void);
/* APPLE LOCAL end 6353006  */

/* APPLE LOCAL radar 6386976  */
extern bool lhd_is_runtime_specific_type (tree);

#define LANG_HOOKS_NAME			"GNU unknown"
#define LANG_HOOKS_IDENTIFIER_SIZE	sizeof (struct lang_identifier)
#define LANG_HOOKS_INIT			hook_bool_void_false
#define LANG_HOOKS_FINISH		lhd_do_nothing
#define LANG_HOOKS_PARSE_FILE		lhd_do_nothing_i
#define LANG_HOOKS_CLEAR_BINDING_STACK	lhd_do_nothing
#define LANG_HOOKS_INIT_OPTIONS		hook_uint_uint_constcharptrptr_0
#define LANG_HOOKS_INITIALIZE_DIAGNOSTICS lhd_initialize_diagnostics
#define LANG_HOOKS_HANDLE_OPTION	hook_int_size_t_constcharptr_int_0
#define LANG_HOOKS_MISSING_ARGUMENT	hook_bool_constcharptr_size_t_false
#define LANG_HOOKS_POST_OPTIONS		lhd_post_options
#define LANG_HOOKS_GET_ALIAS_SET	lhd_get_alias_set
#define LANG_HOOKS_EXPAND_CONSTANT	lhd_return_tree
#define LANG_HOOKS_EXPAND_EXPR		lhd_expand_expr
#define LANG_HOOKS_EXPAND_DECL		lhd_expand_decl
#define LANG_HOOKS_SAFE_FROM_P		lhd_safe_from_p
#define LANG_HOOKS_FINISH_INCOMPLETE_DECL lhd_do_nothing_t
#define LANG_HOOKS_STATICP		lhd_staticp
#define LANG_HOOKS_DUP_LANG_SPECIFIC_DECL lhd_do_nothing_t
#define LANG_HOOKS_SET_DECL_ASSEMBLER_NAME lhd_set_decl_assembler_name
#define LANG_HOOKS_CAN_USE_BIT_FIELDS_P lhd_can_use_bit_fields_p
#define LANG_HOOKS_REDUCE_BIT_FIELD_OPERATIONS false
#define LANG_HOOKS_NO_BODY_BLOCKS	false
#define LANG_HOOKS_PRINT_STATISTICS	lhd_do_nothing
#define LANG_HOOKS_PRINT_XNODE		lhd_print_tree_nothing
#define LANG_HOOKS_PRINT_DECL		lhd_print_tree_nothing
#define LANG_HOOKS_PRINT_TYPE		lhd_print_tree_nothing
#define LANG_HOOKS_PRINT_IDENTIFIER	lhd_print_tree_nothing
#define LANG_HOOKS_PRINT_ERROR_FUNCTION lhd_print_error_function
#define LANG_HOOKS_DECL_PRINTABLE_NAME	lhd_decl_printable_name
#define LANG_HOOKS_DWARF_NAME		lhd_dwarf_name
#define LANG_HOOKS_GET_CALLEE_FNDECL	lhd_return_null_tree
#define LANG_HOOKS_EXPR_SIZE		lhd_expr_size
#define LANG_HOOKS_TREE_SIZE		lhd_tree_size
#define LANG_HOOKS_TYPES_COMPATIBLE_P	lhd_types_compatible_p
#define LANG_HOOKS_BUILTIN_FUNCTION	builtin_function
#define LANG_HOOKS_EXPR_TO_DECL		lhd_expr_to_decl
#define LANG_HOOKS_TO_TARGET_CHARSET	lhd_to_target_charset
#define LANG_HOOKS_INIT_TS		lhd_do_nothing

#define LANG_HOOKS_FUNCTION_INIT	lhd_do_nothing_f
#define LANG_HOOKS_FUNCTION_FINAL	lhd_do_nothing_f
#define LANG_HOOKS_FUNCTION_ENTER_NESTED lhd_do_nothing_f
#define LANG_HOOKS_FUNCTION_LEAVE_NESTED lhd_do_nothing_f
#define LANG_HOOKS_FUNCTION_MISSING_NORETURN_OK_P hook_bool_tree_true

/* APPLE LOCAL begin radar 6353006  */
#define LANG_HOOKS_BUILD_GENERIC_BLOCK_STRUCT_TYPE \
lhd_build_generic_block_struct_type
/* APPLE LOCAL end radar 6353006  */

/* Attribute hooks.  */
#define LANG_HOOKS_ATTRIBUTE_TABLE		NULL
#define LANG_HOOKS_COMMON_ATTRIBUTE_TABLE	NULL
#define LANG_HOOKS_FORMAT_ATTRIBUTE_TABLE	NULL

/* Tree inlining hooks.  */
#define LANG_HOOKS_TREE_INLINING_WALK_SUBTREES lhd_tree_inlining_walk_subtrees
#define LANG_HOOKS_TREE_INLINING_CANNOT_INLINE_TREE_FN \
  lhd_tree_inlining_cannot_inline_tree_fn
#define LANG_HOOKS_TREE_INLINING_DISREGARD_INLINE_LIMITS \
  lhd_tree_inlining_disregard_inline_limits
#define LANG_HOOKS_TREE_INLINING_ADD_PENDING_FN_DECLS \
  lhd_tree_inlining_add_pending_fn_decls
#define LANG_HOOKS_TREE_INLINING_AUTO_VAR_IN_FN_P \
  lhd_tree_inlining_auto_var_in_fn_p
#define LANG_HOOKS_TREE_INLINING_ANON_AGGR_TYPE_P \
  lhd_tree_inlining_anon_aggr_type_p
#define LANG_HOOKS_TREE_INLINING_VAR_MOD_TYPE_P \
  hook_bool_tree_tree_false
#define LANG_HOOKS_TREE_INLINING_START_INLINING \
  lhd_tree_inlining_start_inlining
#define LANG_HOOKS_TREE_INLINING_END_INLINING \
  lhd_tree_inlining_end_inlining
#define LANG_HOOKS_TREE_INLINING_CONVERT_PARM_FOR_INLINING \
  lhd_tree_inlining_convert_parm_for_inlining

#define LANG_HOOKS_TREE_INLINING_INITIALIZER { \
  LANG_HOOKS_TREE_INLINING_WALK_SUBTREES, \
  LANG_HOOKS_TREE_INLINING_CANNOT_INLINE_TREE_FN, \
  LANG_HOOKS_TREE_INLINING_DISREGARD_INLINE_LIMITS, \
  LANG_HOOKS_TREE_INLINING_ADD_PENDING_FN_DECLS, \
  LANG_HOOKS_TREE_INLINING_AUTO_VAR_IN_FN_P, \
  LANG_HOOKS_TREE_INLINING_ANON_AGGR_TYPE_P, \
  LANG_HOOKS_TREE_INLINING_VAR_MOD_TYPE_P, \
  LANG_HOOKS_TREE_INLINING_START_INLINING, \
  LANG_HOOKS_TREE_INLINING_END_INLINING, \
  LANG_HOOKS_TREE_INLINING_CONVERT_PARM_FOR_INLINING \
}

#define LANG_HOOKS_CALLGRAPH_ANALYZE_EXPR lhd_callgraph_analyze_expr
#define LANG_HOOKS_CALLGRAPH_EXPAND_FUNCTION NULL

#define LANG_HOOKS_CALLGRAPH_INITIALIZER { \
  LANG_HOOKS_CALLGRAPH_ANALYZE_EXPR, \
  LANG_HOOKS_CALLGRAPH_EXPAND_FUNCTION, \
}

#define LANG_HOOKS_FUNCTION_INITIALIZER {	\
  LANG_HOOKS_FUNCTION_INIT,			\
  LANG_HOOKS_FUNCTION_FINAL,			\
  LANG_HOOKS_FUNCTION_ENTER_NESTED,		\
  LANG_HOOKS_FUNCTION_LEAVE_NESTED,		\
  LANG_HOOKS_FUNCTION_MISSING_NORETURN_OK_P	\
}

/* Hooks for tree gimplification.  */
#define LANG_HOOKS_GIMPLIFY_EXPR lhd_gimplify_expr
#define LANG_HOOKS_FOLD_OBJ_TYPE_REF NULL

/* Tree dump hooks.  */
extern bool lhd_tree_dump_dump_tree (void *, tree);
extern int lhd_tree_dump_type_quals (tree);
extern tree lhd_make_node (enum tree_code);

#define LANG_HOOKS_TREE_DUMP_DUMP_TREE_FN lhd_tree_dump_dump_tree
#define LANG_HOOKS_TREE_DUMP_TYPE_QUALS_FN lhd_tree_dump_type_quals

#define LANG_HOOKS_TREE_DUMP_INITIALIZER { \
  LANG_HOOKS_TREE_DUMP_DUMP_TREE_FN, \
  LANG_HOOKS_TREE_DUMP_TYPE_QUALS_FN \
}

/* Types hooks.  There are no reasonable defaults for most of them,
   so we create a compile-time error instead.  */
#define LANG_HOOKS_MAKE_TYPE lhd_make_node
#define LANG_HOOKS_INCOMPLETE_TYPE_ERROR lhd_incomplete_type_error
#define LANG_HOOKS_GENERIC_TYPE_P	hook_bool_tree_false
#define LANG_HOOKS_TYPE_PROMOTES_TO lhd_type_promotes_to
#define LANG_HOOKS_REGISTER_BUILTIN_TYPE lhd_register_builtin_type
#define LANG_HOOKS_TYPE_MAX_SIZE	lhd_return_null_tree
#define LANG_HOOKS_OMP_FIRSTPRIVATIZE_TYPE_SIZES \
  lhd_omp_firstprivatize_type_sizes
/* APPLE LOCAL radar 6386976  */
#define LANG_HOOKS_IS_RUNTIME_SPECIFIC_TYPE lhd_is_runtime_specific_type
#define LANG_HOOKS_HASH_TYPES		true

#define LANG_HOOKS_FOR_TYPES_INITIALIZER { \
  LANG_HOOKS_MAKE_TYPE, \
  LANG_HOOKS_TYPE_FOR_MODE, \
  LANG_HOOKS_TYPE_FOR_SIZE, \
  LANG_HOOKS_UNSIGNED_TYPE, \
  LANG_HOOKS_SIGNED_TYPE, \
  LANG_HOOKS_SIGNED_OR_UNSIGNED_TYPE, \
  LANG_HOOKS_GENERIC_TYPE_P, \
  LANG_HOOKS_TYPE_PROMOTES_TO, \
  LANG_HOOKS_REGISTER_BUILTIN_TYPE, \
  LANG_HOOKS_INCOMPLETE_TYPE_ERROR, \
  LANG_HOOKS_TYPE_MAX_SIZE, \
  LANG_HOOKS_OMP_FIRSTPRIVATIZE_TYPE_SIZES, \
  /* APPLE LOCAL radar 6386976  */ \
  LANG_HOOKS_IS_RUNTIME_SPECIFIC_TYPE, \
  LANG_HOOKS_HASH_TYPES \
}

/* Declaration hooks.  */
#define LANG_HOOKS_GLOBAL_BINDINGS_P global_bindings_p
#define LANG_HOOKS_INSERT_BLOCK	insert_block
#define LANG_HOOKS_PUSHDECL	pushdecl
#define LANG_HOOKS_GETDECLS	getdecls
#define LANG_HOOKS_WARN_UNUSED_GLOBAL_DECL lhd_warn_unused_global_decl
#define LANG_HOOKS_WRITE_GLOBALS write_global_declarations
#define LANG_HOOKS_PREPARE_ASSEMBLE_VARIABLE NULL
#define LANG_HOOKS_DECL_OK_FOR_SIBCALL	lhd_decl_ok_for_sibcall
#define LANG_HOOKS_COMDAT_GROUP lhd_comdat_group
#define LANG_HOOKS_OMP_PRIVATIZE_BY_REFERENCE hook_bool_tree_false
#define LANG_HOOKS_OMP_PREDETERMINED_SHARING lhd_omp_predetermined_sharing
#define LANG_HOOKS_OMP_DISREGARD_VALUE_EXPR hook_bool_tree_bool_false
#define LANG_HOOKS_OMP_PRIVATE_DEBUG_CLAUSE hook_bool_tree_bool_false
#define LANG_HOOKS_OMP_CLAUSE_DEFAULT_CTOR hook_tree_tree_tree_null
#define LANG_HOOKS_OMP_CLAUSE_COPY_CTOR lhd_omp_assignment
#define LANG_HOOKS_OMP_CLAUSE_ASSIGN_OP lhd_omp_assignment
#define LANG_HOOKS_OMP_CLAUSE_DTOR hook_tree_tree_tree_null

#define LANG_HOOKS_DECLS { \
  LANG_HOOKS_GLOBAL_BINDINGS_P, \
  LANG_HOOKS_INSERT_BLOCK, \
  LANG_HOOKS_PUSHDECL, \
  LANG_HOOKS_GETDECLS, \
  LANG_HOOKS_WARN_UNUSED_GLOBAL_DECL, \
  LANG_HOOKS_WRITE_GLOBALS, \
  LANG_HOOKS_PREPARE_ASSEMBLE_VARIABLE, \
  LANG_HOOKS_DECL_OK_FOR_SIBCALL, \
  LANG_HOOKS_COMDAT_GROUP, \
  LANG_HOOKS_OMP_PRIVATIZE_BY_REFERENCE, \
  LANG_HOOKS_OMP_PREDETERMINED_SHARING, \
  LANG_HOOKS_OMP_DISREGARD_VALUE_EXPR, \
  LANG_HOOKS_OMP_PRIVATE_DEBUG_CLAUSE, \
  LANG_HOOKS_OMP_CLAUSE_DEFAULT_CTOR, \
  LANG_HOOKS_OMP_CLAUSE_COPY_CTOR, \
  LANG_HOOKS_OMP_CLAUSE_ASSIGN_OP, \
  LANG_HOOKS_OMP_CLAUSE_DTOR \
}

/* The whole thing.  The structure is defined in langhooks.h.  */
#define LANG_HOOKS_INITIALIZER { \
  LANG_HOOKS_NAME, \
  LANG_HOOKS_IDENTIFIER_SIZE, \
  LANG_HOOKS_TREE_SIZE, \
  LANG_HOOKS_INIT_OPTIONS, \
  LANG_HOOKS_INITIALIZE_DIAGNOSTICS, \
  LANG_HOOKS_HANDLE_OPTION, \
  LANG_HOOKS_MISSING_ARGUMENT, \
  LANG_HOOKS_POST_OPTIONS, \
  LANG_HOOKS_INIT, \
  LANG_HOOKS_FINISH, \
  LANG_HOOKS_PARSE_FILE, \
  LANG_HOOKS_CLEAR_BINDING_STACK, \
  LANG_HOOKS_GET_ALIAS_SET, \
  LANG_HOOKS_EXPAND_CONSTANT, \
  LANG_HOOKS_EXPAND_EXPR, \
  LANG_HOOKS_EXPAND_DECL, \
  LANG_HOOKS_SAFE_FROM_P, \
  LANG_HOOKS_FINISH_INCOMPLETE_DECL, \
  LANG_HOOKS_MARK_ADDRESSABLE, \
  LANG_HOOKS_STATICP, \
  LANG_HOOKS_DUP_LANG_SPECIFIC_DECL, \
  LANG_HOOKS_SET_DECL_ASSEMBLER_NAME, \
  LANG_HOOKS_CAN_USE_BIT_FIELDS_P, \
  LANG_HOOKS_REDUCE_BIT_FIELD_OPERATIONS, \
  LANG_HOOKS_NO_BODY_BLOCKS, \
  LANG_HOOKS_PRINT_STATISTICS, \
  LANG_HOOKS_PRINT_XNODE, \
  LANG_HOOKS_PRINT_DECL, \
  LANG_HOOKS_PRINT_TYPE, \
  LANG_HOOKS_PRINT_IDENTIFIER, \
  LANG_HOOKS_DECL_PRINTABLE_NAME, \
  LANG_HOOKS_DWARF_NAME, \
  LANG_HOOKS_TYPES_COMPATIBLE_P, \
  LANG_HOOKS_GET_CALLEE_FNDECL, \
  LANG_HOOKS_PRINT_ERROR_FUNCTION, \
  LANG_HOOKS_EXPR_SIZE, \
  LANG_HOOKS_TO_TARGET_CHARSET, \
  LANG_HOOKS_ATTRIBUTE_TABLE, \
  LANG_HOOKS_COMMON_ATTRIBUTE_TABLE, \
  LANG_HOOKS_FORMAT_ATTRIBUTE_TABLE, \
  LANG_HOOKS_FUNCTION_INITIALIZER, \
  LANG_HOOKS_TREE_INLINING_INITIALIZER, \
  LANG_HOOKS_CALLGRAPH_INITIALIZER, \
  LANG_HOOKS_TREE_DUMP_INITIALIZER, \
  LANG_HOOKS_DECLS, \
  LANG_HOOKS_FOR_TYPES_INITIALIZER, \
  LANG_HOOKS_GIMPLIFY_EXPR, \
  LANG_HOOKS_FOLD_OBJ_TYPE_REF, \
  LANG_HOOKS_BUILTIN_FUNCTION, \
  LANG_HOOKS_INIT_TS,          \
  LANG_HOOKS_EXPR_TO_DECL, \
/* APPLE LOCAL radar 6353006  */ \
  LANG_HOOKS_BUILD_GENERIC_BLOCK_STRUCT_TYPE, \
}

#endif /* GCC_LANG_HOOKS_DEF_H */

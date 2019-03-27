/* Default initializers for a generic GCC target.
   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

 In other words, you are welcome to use, share and improve this program.
 You are forbidden to forbid anyone else to use, share and improve
 what you give them.   Help stamp out software-hoarding!  */

/* See target.h for a description of what this file contains and how to
   use it.

   We want to have non-NULL default definitions of all hook functions,
   even if they do nothing.  */

/* Note that if one of these macros must be defined in an OS .h file
   rather than the .c file, then we need to wrap the default
   definition in a #ifndef, since files include tm.h before this one.  */

/* Assembler output.  */
#ifndef TARGET_ASM_OPEN_PAREN
#define TARGET_ASM_OPEN_PAREN "("
#endif
#ifndef TARGET_ASM_CLOSE_PAREN
#define TARGET_ASM_CLOSE_PAREN ")"
#endif

#define TARGET_ASM_BYTE_OP "\t.byte\t"

#define TARGET_ASM_ALIGNED_HI_OP "\t.short\t"
#define TARGET_ASM_ALIGNED_SI_OP "\t.long\t"
#define TARGET_ASM_ALIGNED_DI_OP NULL
#define TARGET_ASM_ALIGNED_TI_OP NULL

/* GAS and SYSV4 assemblers accept these.  */
#if defined (OBJECT_FORMAT_ELF)
#define TARGET_ASM_UNALIGNED_HI_OP "\t.2byte\t"
#define TARGET_ASM_UNALIGNED_SI_OP "\t.4byte\t"
#define TARGET_ASM_UNALIGNED_DI_OP "\t.8byte\t"
#define TARGET_ASM_UNALIGNED_TI_OP NULL
#else
#define TARGET_ASM_UNALIGNED_HI_OP NULL
#define TARGET_ASM_UNALIGNED_SI_OP NULL
#define TARGET_ASM_UNALIGNED_DI_OP NULL
#define TARGET_ASM_UNALIGNED_TI_OP NULL
#endif /* OBJECT_FORMAT_ELF */

#define TARGET_ASM_INTEGER default_assemble_integer

#ifndef TARGET_ASM_GLOBALIZE_LABEL
#define TARGET_ASM_GLOBALIZE_LABEL default_globalize_label
#endif

#ifndef TARGET_ASM_EMIT_UNWIND_LABEL
#define TARGET_ASM_EMIT_UNWIND_LABEL default_emit_unwind_label
#endif

#ifndef TARGET_ASM_EMIT_EXCEPT_TABLE_LABEL
#define TARGET_ASM_EMIT_EXCEPT_TABLE_LABEL default_emit_except_table_label
#endif

#ifndef TARGET_UNWIND_EMIT
#define TARGET_UNWIND_EMIT default_unwind_emit
#endif

#ifndef TARGET_ASM_INTERNAL_LABEL
#define TARGET_ASM_INTERNAL_LABEL default_internal_label
#endif

#ifndef TARGET_ARM_TTYPE
#define TARGET_ASM_TTYPE hook_bool_rtx_false
#endif

#ifndef TARGET_ASM_ASSEMBLE_VISIBILITY
#define TARGET_ASM_ASSEMBLE_VISIBILITY default_assemble_visibility
#endif

#define TARGET_ASM_FUNCTION_PROLOGUE default_function_pro_epilogue
#define TARGET_ASM_FUNCTION_EPILOGUE default_function_pro_epilogue
#define TARGET_ASM_FUNCTION_END_PROLOGUE no_asm_to_stream
#define TARGET_ASM_FUNCTION_BEGIN_EPILOGUE no_asm_to_stream

#ifndef TARGET_ASM_RELOC_RW_MASK
#define TARGET_ASM_RELOC_RW_MASK default_reloc_rw_mask
#endif

#ifndef TARGET_ASM_SELECT_SECTION
#define TARGET_ASM_SELECT_SECTION default_select_section
#endif

#ifndef TARGET_ASM_UNIQUE_SECTION
#define TARGET_ASM_UNIQUE_SECTION default_unique_section
#endif

#ifndef TARGET_ASM_FUNCTION_RODATA_SECTION
#define TARGET_ASM_FUNCTION_RODATA_SECTION default_function_rodata_section
#endif

#ifndef TARGET_ASM_SELECT_RTX_SECTION
#define TARGET_ASM_SELECT_RTX_SECTION default_select_rtx_section
#endif

#if !defined(TARGET_ASM_CONSTRUCTOR) && !defined(USE_COLLECT2)
# ifdef CTORS_SECTION_ASM_OP
#  define TARGET_ASM_CONSTRUCTOR default_ctor_section_asm_out_constructor
# else
#  ifdef TARGET_ASM_NAMED_SECTION
#   define TARGET_ASM_CONSTRUCTOR default_named_section_asm_out_constructor
#  else
#   define TARGET_ASM_CONSTRUCTOR default_stabs_asm_out_constructor
#  endif
# endif
#endif

#if !defined(TARGET_ASM_DESTRUCTOR) && !defined(USE_COLLECT2)
# ifdef DTORS_SECTION_ASM_OP
#  define TARGET_ASM_DESTRUCTOR default_dtor_section_asm_out_destructor
# else
#  ifdef TARGET_ASM_NAMED_SECTION
#   define TARGET_ASM_DESTRUCTOR default_named_section_asm_out_destructor
#  else
#   define TARGET_ASM_DESTRUCTOR default_stabs_asm_out_destructor
#  endif
# endif
#endif

#define TARGET_ASM_OUTPUT_MI_THUNK NULL
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK hook_bool_tree_hwi_hwi_tree_false

#if !defined(TARGET_HAVE_CTORS_DTORS)
# if defined(TARGET_ASM_CONSTRUCTOR) && defined(TARGET_ASM_DESTRUCTOR)
# define TARGET_HAVE_CTORS_DTORS true
# else
# define TARGET_HAVE_CTORS_DTORS false
# define TARGET_ASM_CONSTRUCTOR NULL
# define TARGET_ASM_DESTRUCTOR NULL
# endif
#endif

#ifndef TARGET_HAVE_SWITCHABLE_BSS_SECTIONS
#define TARGET_HAVE_SWITCHABLE_BSS_SECTIONS false
#endif

#ifndef TARGET_ASM_INIT_SECTIONS
#define TARGET_ASM_INIT_SECTIONS hook_void_void
#endif

#ifdef TARGET_ASM_NAMED_SECTION
#define TARGET_HAVE_NAMED_SECTIONS true
#else
#define TARGET_ASM_NAMED_SECTION default_no_named_section
#define TARGET_HAVE_NAMED_SECTIONS false
#endif

#ifndef TARGET_INVALID_WITHIN_DOLOOP
#define TARGET_INVALID_WITHIN_DOLOOP default_invalid_within_doloop
#endif

#ifndef TARGET_VALID_DLLIMPORT_ATTRIBUTE_P
#define TARGET_VALID_DLLIMPORT_ATTRIBUTE_P hook_bool_tree_true
#endif

#ifndef TARGET_HAVE_TLS
#define TARGET_HAVE_TLS false
#endif

#ifndef TARGET_HAVE_SRODATA_SECTION
#define TARGET_HAVE_SRODATA_SECTION false
#endif

#ifndef TARGET_TERMINATE_DW2_EH_FRAME_INFO
#ifdef EH_FRAME_SECTION_NAME
#define TARGET_TERMINATE_DW2_EH_FRAME_INFO false
#else
#define TARGET_TERMINATE_DW2_EH_FRAME_INFO true
#endif
#endif

#define TARGET_DWARF_REGISTER_SPAN hook_rtx_rtx_null

#ifndef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START default_file_start
#endif

#ifndef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END hook_void_void
#endif

#ifndef TARGET_EXTRA_LIVE_ON_ENTRY
#define TARGET_EXTRA_LIVE_ON_ENTRY hook_void_bitmap
#endif

#ifndef TARGET_ASM_FILE_START_APP_OFF
#define TARGET_ASM_FILE_START_APP_OFF false
#endif

#ifndef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE false
#endif

#ifndef TARGET_ASM_EXTERNAL_LIBCALL
#define TARGET_ASM_EXTERNAL_LIBCALL default_external_libcall
#endif

#ifndef TARGET_ASM_MARK_DECL_PRESERVED
#define TARGET_ASM_MARK_DECL_PRESERVED hook_void_constcharptr
#endif

#ifndef TARGET_ASM_OUTPUT_ANCHOR
#ifdef ASM_OUTPUT_DEF
#define TARGET_ASM_OUTPUT_ANCHOR default_asm_output_anchor
#else
#define TARGET_ASM_OUTPUT_ANCHOR NULL
#endif
#endif

#ifndef TARGET_ASM_OUTPUT_DWARF_DTPREL
#define TARGET_ASM_OUTPUT_DWARF_DTPREL NULL
#endif

#define TARGET_ASM_ALIGNED_INT_OP				\
		       {TARGET_ASM_ALIGNED_HI_OP,		\
			TARGET_ASM_ALIGNED_SI_OP,		\
			TARGET_ASM_ALIGNED_DI_OP,		\
			TARGET_ASM_ALIGNED_TI_OP}

#define TARGET_ASM_UNALIGNED_INT_OP				\
		       {TARGET_ASM_UNALIGNED_HI_OP,		\
			TARGET_ASM_UNALIGNED_SI_OP,		\
			TARGET_ASM_UNALIGNED_DI_OP,		\
			TARGET_ASM_UNALIGNED_TI_OP}

#define TARGET_ASM_OUT {TARGET_ASM_OPEN_PAREN,			\
			TARGET_ASM_CLOSE_PAREN,			\
			TARGET_ASM_BYTE_OP,			\
			TARGET_ASM_ALIGNED_INT_OP,		\
			TARGET_ASM_UNALIGNED_INT_OP,		\
			TARGET_ASM_INTEGER,			\
			TARGET_ASM_GLOBALIZE_LABEL,		\
                        TARGET_ASM_EMIT_UNWIND_LABEL,           \
			TARGET_ASM_EMIT_EXCEPT_TABLE_LABEL,	\
			TARGET_UNWIND_EMIT,			\
			TARGET_ASM_INTERNAL_LABEL,		\
			TARGET_ASM_TTYPE,			\
			TARGET_ASM_ASSEMBLE_VISIBILITY,		\
			TARGET_ASM_FUNCTION_PROLOGUE,		\
			TARGET_ASM_FUNCTION_END_PROLOGUE,	\
			TARGET_ASM_FUNCTION_BEGIN_EPILOGUE,	\
			TARGET_ASM_FUNCTION_EPILOGUE,		\
			TARGET_ASM_INIT_SECTIONS,		\
			TARGET_ASM_NAMED_SECTION,		\
			TARGET_ASM_RELOC_RW_MASK,		\
			TARGET_ASM_SELECT_SECTION,		\
			TARGET_ASM_SELECT_RTX_SECTION,		\
			TARGET_ASM_UNIQUE_SECTION,		\
			TARGET_ASM_FUNCTION_RODATA_SECTION,	\
			TARGET_ASM_CONSTRUCTOR,			\
			TARGET_ASM_DESTRUCTOR,                  \
                        TARGET_ASM_OUTPUT_MI_THUNK,             \
                        TARGET_ASM_CAN_OUTPUT_MI_THUNK,         \
                        TARGET_ASM_FILE_START,                  \
                        TARGET_ASM_FILE_END,			\
			TARGET_ASM_EXTERNAL_LIBCALL,            \
                        TARGET_ASM_MARK_DECL_PRESERVED,		\
			TARGET_ASM_OUTPUT_ANCHOR,		\
			TARGET_ASM_OUTPUT_DWARF_DTPREL}

/* Scheduler hooks.  All of these default to null pointers, which
   haifa-sched.c looks for and handles.  */
#define TARGET_SCHED_ADJUST_COST 0
#define TARGET_SCHED_ADJUST_PRIORITY 0
#define TARGET_SCHED_ISSUE_RATE 0
#define TARGET_SCHED_VARIABLE_ISSUE 0
#define TARGET_SCHED_INIT 0
#define TARGET_SCHED_FINISH 0
#define TARGET_SCHED_INIT_GLOBAL 0
#define TARGET_SCHED_FINISH_GLOBAL 0
#define TARGET_SCHED_REORDER 0
#define TARGET_SCHED_REORDER2 0
#define TARGET_SCHED_DEPENDENCIES_EVALUATION_HOOK 0
#define TARGET_SCHED_INIT_DFA_PRE_CYCLE_INSN 0
#define TARGET_SCHED_DFA_PRE_CYCLE_INSN 0
#define TARGET_SCHED_INIT_DFA_POST_CYCLE_INSN 0
#define TARGET_SCHED_DFA_POST_CYCLE_INSN 0
#define TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD 0
#define TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD_GUARD 0
#define TARGET_SCHED_DFA_NEW_CYCLE 0
#define TARGET_SCHED_IS_COSTLY_DEPENDENCE 0
#define TARGET_SCHED_ADJUST_COST_2 0
#define TARGET_SCHED_H_I_D_EXTENDED 0
#define TARGET_SCHED_SPECULATE_INSN 0
#define TARGET_SCHED_NEEDS_BLOCK_P 0
#define TARGET_SCHED_GEN_CHECK 0
#define TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD_GUARD_SPEC 0
#define TARGET_SCHED_SET_SCHED_FLAGS 0


#define TARGET_SCHED						\
  {TARGET_SCHED_ADJUST_COST,					\
   TARGET_SCHED_ADJUST_PRIORITY,				\
   TARGET_SCHED_ISSUE_RATE,					\
   TARGET_SCHED_VARIABLE_ISSUE,					\
   TARGET_SCHED_INIT,						\
   TARGET_SCHED_FINISH,						\
   TARGET_SCHED_INIT_GLOBAL,					\
   TARGET_SCHED_FINISH_GLOBAL,					\
   TARGET_SCHED_REORDER,					\
   TARGET_SCHED_REORDER2,					\
   TARGET_SCHED_DEPENDENCIES_EVALUATION_HOOK,			\
   TARGET_SCHED_INIT_DFA_PRE_CYCLE_INSN,			\
   TARGET_SCHED_DFA_PRE_CYCLE_INSN,				\
   TARGET_SCHED_INIT_DFA_POST_CYCLE_INSN,			\
   TARGET_SCHED_DFA_POST_CYCLE_INSN,				\
   TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD,		\
   TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD_GUARD,	\
   TARGET_SCHED_DFA_NEW_CYCLE,					\
   TARGET_SCHED_IS_COSTLY_DEPENDENCE,                           \
   TARGET_SCHED_ADJUST_COST_2,                                  \
   TARGET_SCHED_H_I_D_EXTENDED,					\
   TARGET_SCHED_SPECULATE_INSN,                                 \
   TARGET_SCHED_NEEDS_BLOCK_P,                                  \
   TARGET_SCHED_GEN_CHECK,                                      \
   TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD_GUARD_SPEC, \
   TARGET_SCHED_SET_SCHED_FLAGS}

#define TARGET_VECTORIZE_BUILTIN_MASK_FOR_LOAD 0
#define TARGET_VECTOR_ALIGNMENT_REACHABLE \
  default_builtin_vector_alignment_reachable

#define TARGET_VECTORIZE                                                \
  {TARGET_VECTORIZE_BUILTIN_MASK_FOR_LOAD,				\
   TARGET_VECTOR_ALIGNMENT_REACHABLE}

#define TARGET_DEFAULT_TARGET_FLAGS 0

#define TARGET_HANDLE_OPTION hook_bool_size_t_constcharptr_int_true

/* In except.c */
#define TARGET_EH_RETURN_FILTER_MODE  default_eh_return_filter_mode

/* In tree.c.  */
#define TARGET_MERGE_DECL_ATTRIBUTES merge_decl_attributes
#define TARGET_MERGE_TYPE_ATTRIBUTES merge_type_attributes
#define TARGET_ATTRIBUTE_TABLE NULL

/* In cse.c.  */
#define TARGET_ADDRESS_COST default_address_cost

/* In builtins.c.  */
#define TARGET_INIT_BUILTINS hook_void_void
#define TARGET_EXPAND_BUILTIN default_expand_builtin
#define TARGET_RESOLVE_OVERLOADED_BUILTIN NULL
#define TARGET_FOLD_BUILTIN hook_tree_tree_tree_bool_null

/* In varasm.c.  */
#ifndef TARGET_SECTION_TYPE_FLAGS
#define TARGET_SECTION_TYPE_FLAGS default_section_type_flags
#endif

#ifndef TARGET_STRIP_NAME_ENCODING
#define TARGET_STRIP_NAME_ENCODING default_strip_name_encoding
#endif

#ifndef TARGET_BINDS_LOCAL_P
#define TARGET_BINDS_LOCAL_P default_binds_local_p
#endif

#ifndef TARGET_SHIFT_TRUNCATION_MASK
#define TARGET_SHIFT_TRUNCATION_MASK default_shift_truncation_mask
#endif

#ifndef TARGET_MIN_DIVISIONS_FOR_RECIP_MUL
#define TARGET_MIN_DIVISIONS_FOR_RECIP_MUL default_min_divisions_for_recip_mul
#endif

#ifndef TARGET_MODE_REP_EXTENDED
#define TARGET_MODE_REP_EXTENDED default_mode_rep_extended
#endif

#ifndef TARGET_VALID_POINTER_MODE
#define TARGET_VALID_POINTER_MODE default_valid_pointer_mode
#endif

#ifndef TARGET_SCALAR_MODE_SUPPORTED_P
#define TARGET_SCALAR_MODE_SUPPORTED_P default_scalar_mode_supported_p
#endif

#ifndef TARGET_DECIMAL_FLOAT_SUPPORTED_P
#define TARGET_DECIMAL_FLOAT_SUPPORTED_P default_decimal_float_supported_p
#endif

#ifndef TARGET_VECTOR_MODE_SUPPORTED_P
#define TARGET_VECTOR_MODE_SUPPORTED_P hook_bool_mode_false
#endif

#ifndef TARGET_VECTOR_OPAQUE_P
#define TARGET_VECTOR_OPAQUE_P hook_bool_tree_false
#endif

/* In hooks.c.  */
#define TARGET_CANNOT_MODIFY_JUMPS_P hook_bool_void_false
#define TARGET_BRANCH_TARGET_REGISTER_CLASS hook_int_void_no_regs
#define TARGET_BRANCH_TARGET_REGISTER_CALLEE_SAVED hook_bool_bool_false
#define TARGET_CANNOT_FORCE_CONST_MEM hook_bool_rtx_false
#define TARGET_CANNOT_COPY_INSN_P NULL
#define TARGET_COMMUTATIVE_P hook_bool_rtx_commutative_p
#define TARGET_DELEGITIMIZE_ADDRESS hook_rtx_rtx_identity
#define TARGET_USE_BLOCKS_FOR_CONSTANT_P hook_bool_mode_rtx_false
#define TARGET_MIN_ANCHOR_OFFSET 0
#define TARGET_MAX_ANCHOR_OFFSET 0
#define TARGET_USE_ANCHORS_FOR_SYMBOL_P default_use_anchors_for_symbol_p
#define TARGET_FUNCTION_OK_FOR_SIBCALL hook_bool_tree_tree_false
#define TARGET_COMP_TYPE_ATTRIBUTES hook_int_tree_tree_1
#ifndef TARGET_SET_DEFAULT_TYPE_ATTRIBUTES
#define TARGET_SET_DEFAULT_TYPE_ATTRIBUTES hook_void_tree
#endif
#define TARGET_INSERT_ATTRIBUTES hook_void_tree_treeptr
#define TARGET_FUNCTION_ATTRIBUTE_INLINABLE_P hook_bool_tree_false
#define TARGET_MS_BITFIELD_LAYOUT_P hook_bool_tree_false
#define TARGET_ALIGN_ANON_BITFIELD hook_bool_void_false
#define TARGET_NARROW_VOLATILE_BITFIELD hook_bool_void_false
#define TARGET_RTX_COSTS hook_bool_rtx_int_int_intp_false
#define TARGET_MANGLE_FUNDAMENTAL_TYPE hook_constcharptr_tree_null
#define TARGET_ALLOCATE_INITIAL_VALUE NULL

#ifndef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS hook_void_void
#endif

#ifndef TARGET_IN_SMALL_DATA_P
#define TARGET_IN_SMALL_DATA_P hook_bool_tree_false
#endif

#ifndef TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO default_encode_section_info
#endif

#ifndef TARGET_INVALID_ARG_FOR_UNPROTOTYPED_FN
#define TARGET_INVALID_ARG_FOR_UNPROTOTYPED_FN hook_invalid_arg_for_unprototyped_fn
#endif

#define TARGET_INVALID_CONVERSION hook_constcharptr_tree_tree_null
#define TARGET_INVALID_UNARY_OP hook_constcharptr_int_tree_null
#define TARGET_INVALID_BINARY_OP hook_constcharptr_int_tree_tree_null

#define TARGET_FIXED_CONDITION_CODE_REGS hook_bool_uintp_uintp_false

#define TARGET_CC_MODES_COMPATIBLE default_cc_modes_compatible

#define TARGET_MACHINE_DEPENDENT_REORG 0

#define TARGET_BUILD_BUILTIN_VA_LIST std_build_builtin_va_list

#define TARGET_GET_PCH_VALIDITY default_get_pch_validity
#define TARGET_PCH_VALID_P default_pch_valid_p
#define TARGET_CHECK_PCH_TARGET_FLAGS NULL

#define TARGET_DEFAULT_SHORT_ENUMS hook_bool_void_false

#define TARGET_BUILTIN_SETJMP_FRAME_VALUE default_builtin_setjmp_frame_value

#define TARGET_MD_ASM_CLOBBERS hook_tree_tree_tree_tree_3rd_identity

#define TARGET_DWARF_CALLING_CONVENTION hook_int_tree_0

#define TARGET_DWARF_HANDLE_FRAME_UNSPEC 0

#define TARGET_STDARG_OPTIMIZE_HOOK 0

#define TARGET_STACK_PROTECT_GUARD  default_stack_protect_guard
#define TARGET_STACK_PROTECT_FAIL   default_external_stack_protect_fail

#define TARGET_ARM_EABI_UNWINDER false

#define TARGET_PROMOTE_FUNCTION_ARGS hook_bool_tree_false
#define TARGET_PROMOTE_FUNCTION_RETURN hook_bool_tree_false
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_false

#define TARGET_STRUCT_VALUE_RTX hook_rtx_tree_int_null
#define TARGET_RETURN_IN_MEMORY default_return_in_memory
#define TARGET_RETURN_IN_MSB hook_bool_tree_false

#define TARGET_EXPAND_BUILTIN_SAVEREGS default_expand_builtin_saveregs
#define TARGET_SETUP_INCOMING_VARARGS default_setup_incoming_varargs
#define TARGET_STRICT_ARGUMENT_NAMING hook_bool_CUMULATIVE_ARGS_false
#define TARGET_PRETEND_OUTGOING_VARARGS_NAMED \
  default_pretend_outgoing_varargs_named
#define TARGET_SPLIT_COMPLEX_ARG NULL

#define TARGET_GIMPLIFY_VA_ARG_EXPR std_gimplify_va_arg_expr
#define TARGET_PASS_BY_REFERENCE hook_bool_CUMULATIVE_ARGS_mode_tree_bool_false

#define TARGET_RELAXED_ORDERING false

#define TARGET_MUST_PASS_IN_STACK must_pass_in_stack_var_size_or_pad
#define TARGET_CALLEE_COPIES hook_bool_CUMULATIVE_ARGS_mode_tree_bool_false
#define TARGET_ARG_PARTIAL_BYTES hook_int_CUMULATIVE_ARGS_mode_tree_bool_0

#define TARGET_FUNCTION_VALUE default_function_value
#define TARGET_INTERNAL_ARG_POINTER default_internal_arg_pointer

#define TARGET_CALLS {						\
   TARGET_PROMOTE_FUNCTION_ARGS,				\
   TARGET_PROMOTE_FUNCTION_RETURN,				\
   TARGET_PROMOTE_PROTOTYPES,					\
   TARGET_STRUCT_VALUE_RTX,					\
   TARGET_RETURN_IN_MEMORY,					\
   TARGET_RETURN_IN_MSB,					\
   TARGET_PASS_BY_REFERENCE,					\
   TARGET_EXPAND_BUILTIN_SAVEREGS,				\
   TARGET_SETUP_INCOMING_VARARGS,				\
   TARGET_STRICT_ARGUMENT_NAMING,				\
   TARGET_PRETEND_OUTGOING_VARARGS_NAMED,			\
   TARGET_SPLIT_COMPLEX_ARG,					\
   TARGET_MUST_PASS_IN_STACK,					\
   TARGET_CALLEE_COPIES,					\
   TARGET_ARG_PARTIAL_BYTES,					\
   TARGET_INVALID_ARG_FOR_UNPROTOTYPED_FN,			\
   TARGET_FUNCTION_VALUE,					\
   TARGET_INTERNAL_ARG_POINTER					\
   }

#ifndef TARGET_UNWIND_TABLES_DEFAULT
#define TARGET_UNWIND_TABLES_DEFAULT false
#endif

#ifndef TARGET_HANDLE_PRAGMA_REDEFINE_EXTNAME
#define TARGET_HANDLE_PRAGMA_REDEFINE_EXTNAME 0
#endif

#ifndef TARGET_HANDLE_PRAGMA_EXTERN_PREFIX
#define TARGET_HANDLE_PRAGMA_EXTERN_PREFIX 0
#endif

#ifndef TARGET_SECONDARY_RELOAD
#define TARGET_SECONDARY_RELOAD default_secondary_reload
#endif


/* C++ specific.  */
#ifndef TARGET_CXX_GUARD_TYPE
#define TARGET_CXX_GUARD_TYPE default_cxx_guard_type
#endif

#ifndef TARGET_CXX_GUARD_MASK_BIT
#define TARGET_CXX_GUARD_MASK_BIT hook_bool_void_false
#endif

#ifndef TARGET_CXX_GET_COOKIE_SIZE
#define TARGET_CXX_GET_COOKIE_SIZE default_cxx_get_cookie_size
#endif

#ifndef TARGET_CXX_COOKIE_HAS_SIZE
#define TARGET_CXX_COOKIE_HAS_SIZE hook_bool_void_false
#endif

#ifndef TARGET_CXX_IMPORT_EXPORT_CLASS
#define TARGET_CXX_IMPORT_EXPORT_CLASS NULL
#endif

#ifndef TARGET_CXX_CDTOR_RETURNS_THIS
#define TARGET_CXX_CDTOR_RETURNS_THIS hook_bool_void_false
#endif

#ifndef TARGET_CXX_KEY_METHOD_MAY_BE_INLINE
#define TARGET_CXX_KEY_METHOD_MAY_BE_INLINE hook_bool_void_true
#endif

#ifndef TARGET_CXX_DETERMINE_CLASS_DATA_VISIBILITY
#define TARGET_CXX_DETERMINE_CLASS_DATA_VISIBILITY hook_void_tree
#endif

#ifndef TARGET_CXX_CLASS_DATA_ALWAYS_COMDAT
#define TARGET_CXX_CLASS_DATA_ALWAYS_COMDAT hook_bool_void_true
#endif

#ifndef TARGET_CXX_LIBRARY_RTTI_COMDAT
#define TARGET_CXX_LIBRARY_RTTI_COMDAT hook_bool_void_true
#endif

#ifndef TARGET_CXX_USE_AEABI_ATEXIT
#define TARGET_CXX_USE_AEABI_ATEXIT hook_bool_void_false
#endif

#ifndef TARGET_CXX_ADJUST_CLASS_AT_DEFINITION
#define TARGET_CXX_ADJUST_CLASS_AT_DEFINITION hook_void_tree
#endif

#define TARGET_CXX				\
  {						\
    TARGET_CXX_GUARD_TYPE,			\
    TARGET_CXX_GUARD_MASK_BIT,			\
    TARGET_CXX_GET_COOKIE_SIZE,			\
    TARGET_CXX_COOKIE_HAS_SIZE,			\
    TARGET_CXX_IMPORT_EXPORT_CLASS,		\
    TARGET_CXX_CDTOR_RETURNS_THIS,		\
    TARGET_CXX_KEY_METHOD_MAY_BE_INLINE,	\
    TARGET_CXX_DETERMINE_CLASS_DATA_VISIBILITY,	\
    TARGET_CXX_CLASS_DATA_ALWAYS_COMDAT,        \
    TARGET_CXX_LIBRARY_RTTI_COMDAT,	        \
    TARGET_CXX_USE_AEABI_ATEXIT,		\
    TARGET_CXX_ADJUST_CLASS_AT_DEFINITION	\
  }

/* The whole shebang.  */
#define TARGET_INITIALIZER			\
{						\
  TARGET_ASM_OUT,				\
  TARGET_SCHED,					\
  TARGET_VECTORIZE,				\
  TARGET_DEFAULT_TARGET_FLAGS,			\
  TARGET_HANDLE_OPTION,				\
  TARGET_EH_RETURN_FILTER_MODE,			\
  TARGET_MERGE_DECL_ATTRIBUTES,			\
  TARGET_MERGE_TYPE_ATTRIBUTES,			\
  TARGET_ATTRIBUTE_TABLE,			\
  TARGET_COMP_TYPE_ATTRIBUTES,			\
  TARGET_SET_DEFAULT_TYPE_ATTRIBUTES,		\
  TARGET_INSERT_ATTRIBUTES,			\
  TARGET_FUNCTION_ATTRIBUTE_INLINABLE_P,	\
  TARGET_MS_BITFIELD_LAYOUT_P,			\
  TARGET_DECIMAL_FLOAT_SUPPORTED_P,		\
  TARGET_ALIGN_ANON_BITFIELD,			\
  TARGET_NARROW_VOLATILE_BITFIELD,		\
  TARGET_INIT_BUILTINS,				\
  TARGET_EXPAND_BUILTIN,			\
  TARGET_RESOLVE_OVERLOADED_BUILTIN,		\
  TARGET_FOLD_BUILTIN,				\
  TARGET_MANGLE_FUNDAMENTAL_TYPE,		\
  TARGET_INIT_LIBFUNCS,				\
  TARGET_SECTION_TYPE_FLAGS,			\
  TARGET_CANNOT_MODIFY_JUMPS_P,			\
  TARGET_BRANCH_TARGET_REGISTER_CLASS,		\
  TARGET_BRANCH_TARGET_REGISTER_CALLEE_SAVED,	\
  TARGET_CANNOT_FORCE_CONST_MEM,		\
  TARGET_CANNOT_COPY_INSN_P,			\
  TARGET_COMMUTATIVE_P,				\
  TARGET_DELEGITIMIZE_ADDRESS,			\
  TARGET_USE_BLOCKS_FOR_CONSTANT_P,		\
  TARGET_MIN_ANCHOR_OFFSET,			\
  TARGET_MAX_ANCHOR_OFFSET,			\
  TARGET_USE_ANCHORS_FOR_SYMBOL_P,		\
  TARGET_FUNCTION_OK_FOR_SIBCALL,		\
  TARGET_IN_SMALL_DATA_P,			\
  TARGET_BINDS_LOCAL_P,				\
  TARGET_ENCODE_SECTION_INFO,			\
  TARGET_STRIP_NAME_ENCODING,			\
  TARGET_SHIFT_TRUNCATION_MASK,			\
  TARGET_MIN_DIVISIONS_FOR_RECIP_MUL,		\
  TARGET_MODE_REP_EXTENDED,			\
  TARGET_VALID_POINTER_MODE,                    \
  TARGET_SCALAR_MODE_SUPPORTED_P,		\
  TARGET_VECTOR_MODE_SUPPORTED_P,               \
  TARGET_VECTOR_OPAQUE_P,			\
  TARGET_RTX_COSTS,				\
  TARGET_ADDRESS_COST,				\
  TARGET_ALLOCATE_INITIAL_VALUE,		\
  TARGET_DWARF_REGISTER_SPAN,                   \
  TARGET_FIXED_CONDITION_CODE_REGS,		\
  TARGET_CC_MODES_COMPATIBLE,			\
  TARGET_MACHINE_DEPENDENT_REORG,		\
  TARGET_BUILD_BUILTIN_VA_LIST,			\
  TARGET_GIMPLIFY_VA_ARG_EXPR,			\
  TARGET_GET_PCH_VALIDITY,			\
  TARGET_PCH_VALID_P,				\
  TARGET_CHECK_PCH_TARGET_FLAGS,		\
  TARGET_DEFAULT_SHORT_ENUMS,			\
  TARGET_BUILTIN_SETJMP_FRAME_VALUE,		\
  TARGET_MD_ASM_CLOBBERS,			\
  TARGET_DWARF_CALLING_CONVENTION,              \
  TARGET_DWARF_HANDLE_FRAME_UNSPEC,		\
  TARGET_STDARG_OPTIMIZE_HOOK,			\
  TARGET_STACK_PROTECT_GUARD,			\
  TARGET_STACK_PROTECT_FAIL,			\
  TARGET_INVALID_WITHIN_DOLOOP,			\
  TARGET_VALID_DLLIMPORT_ATTRIBUTE_P,		\
  TARGET_CALLS,					\
  TARGET_INVALID_CONVERSION,			\
  TARGET_INVALID_UNARY_OP,			\
  TARGET_INVALID_BINARY_OP,			\
  TARGET_SECONDARY_RELOAD,			\
  TARGET_CXX,					\
  TARGET_EXTRA_LIVE_ON_ENTRY,                    \
  TARGET_UNWIND_TABLES_DEFAULT,			\
  TARGET_HAVE_NAMED_SECTIONS,			\
  TARGET_HAVE_SWITCHABLE_BSS_SECTIONS,		\
  TARGET_HAVE_CTORS_DTORS,			\
  TARGET_HAVE_TLS,				\
  TARGET_HAVE_SRODATA_SECTION,			\
  TARGET_TERMINATE_DW2_EH_FRAME_INFO,		\
  TARGET_ASM_FILE_START_APP_OFF,		\
  TARGET_ASM_FILE_START_FILE_DIRECTIVE,		\
  TARGET_HANDLE_PRAGMA_REDEFINE_EXTNAME,	\
  TARGET_HANDLE_PRAGMA_EXTERN_PREFIX,		\
  TARGET_RELAXED_ORDERING,			\
  TARGET_ARM_EABI_UNWINDER			\
}

#include "hooks.h"
#include "targhooks.h"

/* Subroutines for insn-output.c for SPARC.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com)
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "insn-codes.h"
#include "conditions.h"
#include "output.h"
#include "insn-attr.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "optabs.h"
#include "recog.h"
#include "toplev.h"
#include "ggc.h"
#include "tm_p.h"
#include "debug.h"
#include "target.h"
#include "target-def.h"
#include "cfglayout.h"
#include "tree-gimple.h"
#include "langhooks.h"

/* Processor costs */
static const
struct processor_costs cypress_costs = {
  COSTS_N_INSNS (2), /* int load */
  COSTS_N_INSNS (2), /* int signed load */
  COSTS_N_INSNS (2), /* int zeroed load */
  COSTS_N_INSNS (2), /* float load */
  COSTS_N_INSNS (5), /* fmov, fneg, fabs */
  COSTS_N_INSNS (5), /* fadd, fsub */
  COSTS_N_INSNS (1), /* fcmp */
  COSTS_N_INSNS (1), /* fmov, fmovr */
  COSTS_N_INSNS (7), /* fmul */
  COSTS_N_INSNS (37), /* fdivs */
  COSTS_N_INSNS (37), /* fdivd */
  COSTS_N_INSNS (63), /* fsqrts */
  COSTS_N_INSNS (63), /* fsqrtd */
  COSTS_N_INSNS (1), /* imul */
  COSTS_N_INSNS (1), /* imulX */
  0, /* imul bit factor */
  COSTS_N_INSNS (1), /* idiv */
  COSTS_N_INSNS (1), /* idivX */
  COSTS_N_INSNS (1), /* movcc/movr */
  0, /* shift penalty */
};

static const
struct processor_costs supersparc_costs = {
  COSTS_N_INSNS (1), /* int load */
  COSTS_N_INSNS (1), /* int signed load */
  COSTS_N_INSNS (1), /* int zeroed load */
  COSTS_N_INSNS (0), /* float load */
  COSTS_N_INSNS (3), /* fmov, fneg, fabs */
  COSTS_N_INSNS (3), /* fadd, fsub */
  COSTS_N_INSNS (3), /* fcmp */
  COSTS_N_INSNS (1), /* fmov, fmovr */
  COSTS_N_INSNS (3), /* fmul */
  COSTS_N_INSNS (6), /* fdivs */
  COSTS_N_INSNS (9), /* fdivd */
  COSTS_N_INSNS (12), /* fsqrts */
  COSTS_N_INSNS (12), /* fsqrtd */
  COSTS_N_INSNS (4), /* imul */
  COSTS_N_INSNS (4), /* imulX */
  0, /* imul bit factor */
  COSTS_N_INSNS (4), /* idiv */
  COSTS_N_INSNS (4), /* idivX */
  COSTS_N_INSNS (1), /* movcc/movr */
  1, /* shift penalty */
};

static const
struct processor_costs hypersparc_costs = {
  COSTS_N_INSNS (1), /* int load */
  COSTS_N_INSNS (1), /* int signed load */
  COSTS_N_INSNS (1), /* int zeroed load */
  COSTS_N_INSNS (1), /* float load */
  COSTS_N_INSNS (1), /* fmov, fneg, fabs */
  COSTS_N_INSNS (1), /* fadd, fsub */
  COSTS_N_INSNS (1), /* fcmp */
  COSTS_N_INSNS (1), /* fmov, fmovr */
  COSTS_N_INSNS (1), /* fmul */
  COSTS_N_INSNS (8), /* fdivs */
  COSTS_N_INSNS (12), /* fdivd */
  COSTS_N_INSNS (17), /* fsqrts */
  COSTS_N_INSNS (17), /* fsqrtd */
  COSTS_N_INSNS (17), /* imul */
  COSTS_N_INSNS (17), /* imulX */
  0, /* imul bit factor */
  COSTS_N_INSNS (17), /* idiv */
  COSTS_N_INSNS (17), /* idivX */
  COSTS_N_INSNS (1), /* movcc/movr */
  0, /* shift penalty */
};

static const
struct processor_costs sparclet_costs = {
  COSTS_N_INSNS (3), /* int load */
  COSTS_N_INSNS (3), /* int signed load */
  COSTS_N_INSNS (1), /* int zeroed load */
  COSTS_N_INSNS (1), /* float load */
  COSTS_N_INSNS (1), /* fmov, fneg, fabs */
  COSTS_N_INSNS (1), /* fadd, fsub */
  COSTS_N_INSNS (1), /* fcmp */
  COSTS_N_INSNS (1), /* fmov, fmovr */
  COSTS_N_INSNS (1), /* fmul */
  COSTS_N_INSNS (1), /* fdivs */
  COSTS_N_INSNS (1), /* fdivd */
  COSTS_N_INSNS (1), /* fsqrts */
  COSTS_N_INSNS (1), /* fsqrtd */
  COSTS_N_INSNS (5), /* imul */
  COSTS_N_INSNS (5), /* imulX */
  0, /* imul bit factor */
  COSTS_N_INSNS (5), /* idiv */
  COSTS_N_INSNS (5), /* idivX */
  COSTS_N_INSNS (1), /* movcc/movr */
  0, /* shift penalty */
};

static const
struct processor_costs ultrasparc_costs = {
  COSTS_N_INSNS (2), /* int load */
  COSTS_N_INSNS (3), /* int signed load */
  COSTS_N_INSNS (2), /* int zeroed load */
  COSTS_N_INSNS (2), /* float load */
  COSTS_N_INSNS (1), /* fmov, fneg, fabs */
  COSTS_N_INSNS (4), /* fadd, fsub */
  COSTS_N_INSNS (1), /* fcmp */
  COSTS_N_INSNS (2), /* fmov, fmovr */
  COSTS_N_INSNS (4), /* fmul */
  COSTS_N_INSNS (13), /* fdivs */
  COSTS_N_INSNS (23), /* fdivd */
  COSTS_N_INSNS (13), /* fsqrts */
  COSTS_N_INSNS (23), /* fsqrtd */
  COSTS_N_INSNS (4), /* imul */
  COSTS_N_INSNS (4), /* imulX */
  2, /* imul bit factor */
  COSTS_N_INSNS (37), /* idiv */
  COSTS_N_INSNS (68), /* idivX */
  COSTS_N_INSNS (2), /* movcc/movr */
  2, /* shift penalty */
};

static const
struct processor_costs ultrasparc3_costs = {
  COSTS_N_INSNS (2), /* int load */
  COSTS_N_INSNS (3), /* int signed load */
  COSTS_N_INSNS (3), /* int zeroed load */
  COSTS_N_INSNS (2), /* float load */
  COSTS_N_INSNS (3), /* fmov, fneg, fabs */
  COSTS_N_INSNS (4), /* fadd, fsub */
  COSTS_N_INSNS (5), /* fcmp */
  COSTS_N_INSNS (3), /* fmov, fmovr */
  COSTS_N_INSNS (4), /* fmul */
  COSTS_N_INSNS (17), /* fdivs */
  COSTS_N_INSNS (20), /* fdivd */
  COSTS_N_INSNS (20), /* fsqrts */
  COSTS_N_INSNS (29), /* fsqrtd */
  COSTS_N_INSNS (6), /* imul */
  COSTS_N_INSNS (6), /* imulX */
  0, /* imul bit factor */
  COSTS_N_INSNS (40), /* idiv */
  COSTS_N_INSNS (71), /* idivX */
  COSTS_N_INSNS (2), /* movcc/movr */
  0, /* shift penalty */
};

static const
struct processor_costs niagara_costs = {
  COSTS_N_INSNS (3), /* int load */
  COSTS_N_INSNS (3), /* int signed load */
  COSTS_N_INSNS (3), /* int zeroed load */
  COSTS_N_INSNS (9), /* float load */
  COSTS_N_INSNS (8), /* fmov, fneg, fabs */
  COSTS_N_INSNS (8), /* fadd, fsub */
  COSTS_N_INSNS (26), /* fcmp */
  COSTS_N_INSNS (8), /* fmov, fmovr */
  COSTS_N_INSNS (29), /* fmul */
  COSTS_N_INSNS (54), /* fdivs */
  COSTS_N_INSNS (83), /* fdivd */
  COSTS_N_INSNS (100), /* fsqrts - not implemented in hardware */
  COSTS_N_INSNS (100), /* fsqrtd - not implemented in hardware */
  COSTS_N_INSNS (11), /* imul */
  COSTS_N_INSNS (11), /* imulX */
  0, /* imul bit factor */
  COSTS_N_INSNS (72), /* idiv */
  COSTS_N_INSNS (72), /* idivX */
  COSTS_N_INSNS (1), /* movcc/movr */
  0, /* shift penalty */
};

const struct processor_costs *sparc_costs = &cypress_costs;

#ifdef HAVE_AS_RELAX_OPTION
/* If 'as' and 'ld' are relaxing tail call insns into branch always, use
   "or %o7,%g0,X; call Y; or X,%g0,%o7" always, so that it can be optimized.
   With sethi/jmp, neither 'as' nor 'ld' has an easy way how to find out if
   somebody does not branch between the sethi and jmp.  */
#define LEAF_SIBCALL_SLOT_RESERVED_P 1
#else
#define LEAF_SIBCALL_SLOT_RESERVED_P \
  ((TARGET_ARCH64 && !TARGET_CM_MEDLOW) || flag_pic)
#endif

/* Global variables for machine-dependent things.  */

/* Size of frame.  Need to know this to emit return insns from leaf procedures.
   ACTUAL_FSIZE is set by sparc_compute_frame_size() which is called during the
   reload pass.  This is important as the value is later used for scheduling
   (to see what can go in a delay slot).
   APPARENT_FSIZE is the size of the stack less the register save area and less
   the outgoing argument area.  It is used when saving call preserved regs.  */
static HOST_WIDE_INT apparent_fsize;
static HOST_WIDE_INT actual_fsize;

/* Number of live general or floating point registers needed to be
   saved (as 4-byte quantities).  */
static int num_gfregs;

/* The alias set for prologue/epilogue register save/restore.  */
static GTY(()) int sparc_sr_alias_set;

/* The alias set for the structure return value.  */
static GTY(()) int struct_value_alias_set;

/* Save the operands last given to a compare for use when we
   generate a scc or bcc insn.  */
rtx sparc_compare_op0, sparc_compare_op1, sparc_compare_emitted;

/* Vector to say how input registers are mapped to output registers.
   HARD_FRAME_POINTER_REGNUM cannot be remapped by this function to
   eliminate it.  You must use -fomit-frame-pointer to get that.  */
char leaf_reg_remap[] =
{ 0, 1, 2, 3, 4, 5, 6, 7,
  -1, -1, -1, -1, -1, -1, 14, -1,
  -1, -1, -1, -1, -1, -1, -1, -1,
  8, 9, 10, 11, 12, 13, -1, 15,

  32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47,
  48, 49, 50, 51, 52, 53, 54, 55,
  56, 57, 58, 59, 60, 61, 62, 63,
  64, 65, 66, 67, 68, 69, 70, 71,
  72, 73, 74, 75, 76, 77, 78, 79,
  80, 81, 82, 83, 84, 85, 86, 87,
  88, 89, 90, 91, 92, 93, 94, 95,
  96, 97, 98, 99, 100};

/* Vector, indexed by hard register number, which contains 1
   for a register that is allowable in a candidate for leaf
   function treatment.  */
char sparc_leaf_regs[] =
{ 1, 1, 1, 1, 1, 1, 1, 1,
  0, 0, 0, 0, 0, 0, 1, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1, 0, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1};

struct machine_function GTY(())
{
  /* Some local-dynamic TLS symbol name.  */
  const char *some_ld_name;

  /* True if the current function is leaf and uses only leaf regs,
     so that the SPARC leaf function optimization can be applied.
     Private version of current_function_uses_only_leaf_regs, see
     sparc_expand_prologue for the rationale.  */
  int leaf_function_p;

  /* True if the data calculated by sparc_expand_prologue are valid.  */
  bool prologue_data_valid_p;
};

#define sparc_leaf_function_p  cfun->machine->leaf_function_p
#define sparc_prologue_data_valid_p  cfun->machine->prologue_data_valid_p

/* Register we pretend to think the frame pointer is allocated to.
   Normally, this is %fp, but if we are in a leaf procedure, this
   is %sp+"something".  We record "something" separately as it may
   be too big for reg+constant addressing.  */
static rtx frame_base_reg;
static HOST_WIDE_INT frame_base_offset;

/* 1 if the next opcode is to be specially indented.  */
int sparc_indent_opcode = 0;

static bool sparc_handle_option (size_t, const char *, int);
static void sparc_init_modes (void);
static void scan_record_type (tree, int *, int *, int *);
static int function_arg_slotno (const CUMULATIVE_ARGS *, enum machine_mode,
				tree, int, int, int *, int *);

static int supersparc_adjust_cost (rtx, rtx, rtx, int);
static int hypersparc_adjust_cost (rtx, rtx, rtx, int);

static void sparc_output_addr_vec (rtx);
static void sparc_output_addr_diff_vec (rtx);
static void sparc_output_deferred_case_vectors (void);
static rtx sparc_builtin_saveregs (void);
static int epilogue_renumber (rtx *, int);
static bool sparc_assemble_integer (rtx, unsigned int, int);
static int set_extends (rtx);
static void emit_pic_helper (void);
static void load_pic_register (bool);
static int save_or_restore_regs (int, int, rtx, int, int);
static void emit_save_or_restore_regs (int);
static void sparc_asm_function_prologue (FILE *, HOST_WIDE_INT);
static void sparc_asm_function_epilogue (FILE *, HOST_WIDE_INT);
#ifdef OBJECT_FORMAT_ELF
static void sparc_elf_asm_named_section (const char *, unsigned int, tree);
#endif

static int sparc_adjust_cost (rtx, rtx, rtx, int);
static int sparc_issue_rate (void);
static void sparc_sched_init (FILE *, int, int);
static int sparc_use_sched_lookahead (void);

static void emit_soft_tfmode_libcall (const char *, int, rtx *);
static void emit_soft_tfmode_binop (enum rtx_code, rtx *);
static void emit_soft_tfmode_unop (enum rtx_code, rtx *);
static void emit_soft_tfmode_cvt (enum rtx_code, rtx *);
static void emit_hard_tfmode_operation (enum rtx_code, rtx *);

static bool sparc_function_ok_for_sibcall (tree, tree);
static void sparc_init_libfuncs (void);
static void sparc_init_builtins (void);
static void sparc_vis_init_builtins (void);
static rtx sparc_expand_builtin (tree, rtx, rtx, enum machine_mode, int);
static tree sparc_fold_builtin (tree, tree, bool);
static int sparc_vis_mul8x16 (int, int);
static tree sparc_handle_vis_mul8x16 (int, tree, tree, tree);
static void sparc_output_mi_thunk (FILE *, tree, HOST_WIDE_INT,
				   HOST_WIDE_INT, tree);
static bool sparc_can_output_mi_thunk (tree, HOST_WIDE_INT,
				       HOST_WIDE_INT, tree);
static struct machine_function * sparc_init_machine_status (void);
static bool sparc_cannot_force_const_mem (rtx);
static rtx sparc_tls_get_addr (void);
static rtx sparc_tls_got (void);
static const char *get_some_local_dynamic_name (void);
static int get_some_local_dynamic_name_1 (rtx *, void *);
static bool sparc_rtx_costs (rtx, int, int, int *);
static bool sparc_promote_prototypes (tree);
static rtx sparc_struct_value_rtx (tree, int);
static bool sparc_return_in_memory (tree, tree);
static bool sparc_strict_argument_naming (CUMULATIVE_ARGS *);
static tree sparc_gimplify_va_arg (tree, tree, tree *, tree *);
static bool sparc_vector_mode_supported_p (enum machine_mode);
static bool sparc_pass_by_reference (CUMULATIVE_ARGS *,
				     enum machine_mode, tree, bool);
static int sparc_arg_partial_bytes (CUMULATIVE_ARGS *,
				    enum machine_mode, tree, bool);
static void sparc_dwarf_handle_frame_unspec (const char *, rtx, int);
static void sparc_output_dwarf_dtprel (FILE *, int, rtx) ATTRIBUTE_UNUSED;
static void sparc_file_end (void);
#ifdef TARGET_ALTERNATE_LONG_DOUBLE_MANGLING
static const char *sparc_mangle_fundamental_type (tree);
#endif
#ifdef SUBTARGET_ATTRIBUTE_TABLE
const struct attribute_spec sparc_attribute_table[];
#endif

/* Option handling.  */

/* Parsed value.  */
enum cmodel sparc_cmodel;

char sparc_hard_reg_printed[8];

struct sparc_cpu_select sparc_select[] =
{
  /* switch	name,		tune	arch */
  { (char *)0,	"default",	1,	1 },
  { (char *)0,	"-mcpu=",	1,	1 },
  { (char *)0,	"-mtune=",	1,	0 },
  { 0, 0, 0, 0 }
};

/* CPU type.  This is set from TARGET_CPU_DEFAULT and -m{cpu,tune}=xxx.  */
enum processor_type sparc_cpu;

/* Whetheran FPU option was specified.  */
static bool fpu_option_set = false;

/* Initialize the GCC target structure.  */

/* The sparc default is to use .half rather than .short for aligned
   HI objects.  Use .word instead of .long on non-ELF systems.  */
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.half\t"
#ifndef OBJECT_FORMAT_ELF
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.word\t"
#endif

#undef TARGET_ASM_UNALIGNED_HI_OP
#define TARGET_ASM_UNALIGNED_HI_OP "\t.uahalf\t"
#undef TARGET_ASM_UNALIGNED_SI_OP
#define TARGET_ASM_UNALIGNED_SI_OP "\t.uaword\t"
#undef TARGET_ASM_UNALIGNED_DI_OP
#define TARGET_ASM_UNALIGNED_DI_OP "\t.uaxword\t"

/* The target hook has to handle DI-mode values.  */
#undef TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER sparc_assemble_integer

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE sparc_asm_function_prologue
#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE sparc_asm_function_epilogue

#undef TARGET_SCHED_ADJUST_COST
#define TARGET_SCHED_ADJUST_COST sparc_adjust_cost
#undef TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE sparc_issue_rate
#undef TARGET_SCHED_INIT
#define TARGET_SCHED_INIT sparc_sched_init
#undef TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD
#define TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD sparc_use_sched_lookahead

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL sparc_function_ok_for_sibcall

#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS sparc_init_libfuncs
#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS sparc_init_builtins

#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN sparc_expand_builtin
#undef TARGET_FOLD_BUILTIN
#define TARGET_FOLD_BUILTIN sparc_fold_builtin

#if TARGET_TLS
#undef TARGET_HAVE_TLS
#define TARGET_HAVE_TLS true
#endif

#undef TARGET_CANNOT_FORCE_CONST_MEM
#define TARGET_CANNOT_FORCE_CONST_MEM sparc_cannot_force_const_mem

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK sparc_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK sparc_can_output_mi_thunk

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS sparc_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST hook_int_rtx_0

/* This is only needed for TARGET_ARCH64, but since PROMOTE_FUNCTION_MODE is a
   no-op for TARGET_ARCH32 this is ok.  Otherwise we'd need to add a runtime
   test for this value.  */
#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS hook_bool_tree_true

/* This is only needed for TARGET_ARCH64, but since PROMOTE_FUNCTION_MODE is a
   no-op for TARGET_ARCH32 this is ok.  Otherwise we'd need to add a runtime
   test for this value.  */
#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN hook_bool_tree_true

#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES sparc_promote_prototypes

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX sparc_struct_value_rtx
#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY sparc_return_in_memory
#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK must_pass_in_stack_var_size
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE sparc_pass_by_reference
#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES sparc_arg_partial_bytes

#undef TARGET_EXPAND_BUILTIN_SAVEREGS
#define TARGET_EXPAND_BUILTIN_SAVEREGS sparc_builtin_saveregs
#undef TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING sparc_strict_argument_naming

#undef TARGET_GIMPLIFY_VA_ARG_EXPR
#define TARGET_GIMPLIFY_VA_ARG_EXPR sparc_gimplify_va_arg

#undef TARGET_VECTOR_MODE_SUPPORTED_P
#define TARGET_VECTOR_MODE_SUPPORTED_P sparc_vector_mode_supported_p

#undef TARGET_DWARF_HANDLE_FRAME_UNSPEC
#define TARGET_DWARF_HANDLE_FRAME_UNSPEC sparc_dwarf_handle_frame_unspec

#ifdef SUBTARGET_INSERT_ATTRIBUTES
#undef TARGET_INSERT_ATTRIBUTES
#define TARGET_INSERT_ATTRIBUTES SUBTARGET_INSERT_ATTRIBUTES
#endif

#ifdef SUBTARGET_ATTRIBUTE_TABLE
#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE sparc_attribute_table
#endif

#undef TARGET_RELAXED_ORDERING
#define TARGET_RELAXED_ORDERING SPARC_RELAXED_ORDERING

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS TARGET_DEFAULT
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION sparc_handle_option

#if TARGET_GNU_TLS
#undef TARGET_ASM_OUTPUT_DWARF_DTPREL
#define TARGET_ASM_OUTPUT_DWARF_DTPREL sparc_output_dwarf_dtprel
#endif

#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END sparc_file_end

#ifdef TARGET_ALTERNATE_LONG_DOUBLE_MANGLING
#undef TARGET_MANGLE_FUNDAMENTAL_TYPE
#define TARGET_MANGLE_FUNDAMENTAL_TYPE sparc_mangle_fundamental_type
#endif

struct gcc_target targetm = TARGET_INITIALIZER;

/* Implement TARGET_HANDLE_OPTION.  */

static bool
sparc_handle_option (size_t code, const char *arg, int value ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_mfpu:
    case OPT_mhard_float:
    case OPT_msoft_float:
      fpu_option_set = true;
      break;

    case OPT_mcpu_:
      sparc_select[1].string = arg;
      break;

    case OPT_mtune_:
      sparc_select[2].string = arg;
      break;
    }

  return true;
}

/* Validate and override various options, and do some machine dependent
   initialization.  */

void
sparc_override_options (void)
{
  static struct code_model {
    const char *const name;
    const int value;
  } const cmodels[] = {
    { "32", CM_32 },
    { "medlow", CM_MEDLOW },
    { "medmid", CM_MEDMID },
    { "medany", CM_MEDANY },
    { "embmedany", CM_EMBMEDANY },
    { 0, 0 }
  };
  const struct code_model *cmodel;
  /* Map TARGET_CPU_DEFAULT to value for -m{arch,tune}=.  */
  static struct cpu_default {
    const int cpu;
    const char *const name;
  } const cpu_default[] = {
    /* There must be one entry here for each TARGET_CPU value.  */
    { TARGET_CPU_sparc, "cypress" },
    { TARGET_CPU_sparclet, "tsc701" },
    { TARGET_CPU_sparclite, "f930" },
    { TARGET_CPU_v8, "v8" },
    { TARGET_CPU_hypersparc, "hypersparc" },
    { TARGET_CPU_sparclite86x, "sparclite86x" },
    { TARGET_CPU_supersparc, "supersparc" },
    { TARGET_CPU_v9, "v9" },
    { TARGET_CPU_ultrasparc, "ultrasparc" },
    { TARGET_CPU_ultrasparc3, "ultrasparc3" },
    { TARGET_CPU_niagara, "niagara" },
    { 0, 0 }
  };
  const struct cpu_default *def;
  /* Table of values for -m{cpu,tune}=.  */
  static struct cpu_table {
    const char *const name;
    const enum processor_type processor;
    const int disable;
    const int enable;
  } const cpu_table[] = {
    { "v7",         PROCESSOR_V7, MASK_ISA, 0 },
    { "cypress",    PROCESSOR_CYPRESS, MASK_ISA, 0 },
    { "v8",         PROCESSOR_V8, MASK_ISA, MASK_V8 },
    /* TI TMS390Z55 supersparc */
    { "supersparc", PROCESSOR_SUPERSPARC, MASK_ISA, MASK_V8 },
    { "sparclite",  PROCESSOR_SPARCLITE, MASK_ISA, MASK_SPARCLITE },
    /* The Fujitsu MB86930 is the original sparclite chip, with no fpu.
       The Fujitsu MB86934 is the recent sparclite chip, with an fpu.  */
    { "f930",       PROCESSOR_F930, MASK_ISA|MASK_FPU, MASK_SPARCLITE },
    { "f934",       PROCESSOR_F934, MASK_ISA, MASK_SPARCLITE|MASK_FPU },
    { "hypersparc", PROCESSOR_HYPERSPARC, MASK_ISA, MASK_V8|MASK_FPU },
    { "sparclite86x",  PROCESSOR_SPARCLITE86X, MASK_ISA|MASK_FPU,
      MASK_SPARCLITE },
    { "sparclet",   PROCESSOR_SPARCLET, MASK_ISA, MASK_SPARCLET },
    /* TEMIC sparclet */
    { "tsc701",     PROCESSOR_TSC701, MASK_ISA, MASK_SPARCLET },
    { "v9",         PROCESSOR_V9, MASK_ISA, MASK_V9 },
    /* TI ultrasparc I, II, IIi */
    { "ultrasparc", PROCESSOR_ULTRASPARC, MASK_ISA, MASK_V9
    /* Although insns using %y are deprecated, it is a clear win on current
       ultrasparcs.  */
    						    |MASK_DEPRECATED_V8_INSNS},
    /* TI ultrasparc III */
    /* ??? Check if %y issue still holds true in ultra3.  */
    { "ultrasparc3", PROCESSOR_ULTRASPARC3, MASK_ISA, MASK_V9|MASK_DEPRECATED_V8_INSNS},
    /* UltraSPARC T1 */
    { "niagara", PROCESSOR_NIAGARA, MASK_ISA, MASK_V9|MASK_DEPRECATED_V8_INSNS},
    { 0, 0, 0, 0 }
  };
  const struct cpu_table *cpu;
  const struct sparc_cpu_select *sel;
  int fpu;
  
#ifndef SPARC_BI_ARCH
  /* Check for unsupported architecture size.  */
  if (! TARGET_64BIT != DEFAULT_ARCH32_P)
    error ("%s is not supported by this configuration",
	   DEFAULT_ARCH32_P ? "-m64" : "-m32");
#endif

  /* We force all 64bit archs to use 128 bit long double */
  if (TARGET_64BIT && ! TARGET_LONG_DOUBLE_128)
    {
      error ("-mlong-double-64 not allowed with -m64");
      target_flags |= MASK_LONG_DOUBLE_128;
    }

  /* Code model selection.  */
  sparc_cmodel = SPARC_DEFAULT_CMODEL;
  
#ifdef SPARC_BI_ARCH
  if (TARGET_ARCH32)
    sparc_cmodel = CM_32;
#endif

  if (sparc_cmodel_string != NULL)
    {
      if (TARGET_ARCH64)
	{
	  for (cmodel = &cmodels[0]; cmodel->name; cmodel++)
	    if (strcmp (sparc_cmodel_string, cmodel->name) == 0)
	      break;
	  if (cmodel->name == NULL)
	    error ("bad value (%s) for -mcmodel= switch", sparc_cmodel_string);
	  else
	    sparc_cmodel = cmodel->value;
	}
      else
	error ("-mcmodel= is not supported on 32 bit systems");
    }

  fpu = target_flags & MASK_FPU; /* save current -mfpu status */

  /* Set the default CPU.  */
  for (def = &cpu_default[0]; def->name; ++def)
    if (def->cpu == TARGET_CPU_DEFAULT)
      break;
  gcc_assert (def->name);
  sparc_select[0].string = def->name;

  for (sel = &sparc_select[0]; sel->name; ++sel)
    {
      if (sel->string)
	{
	  for (cpu = &cpu_table[0]; cpu->name; ++cpu)
	    if (! strcmp (sel->string, cpu->name))
	      {
		if (sel->set_tune_p)
		  sparc_cpu = cpu->processor;

		if (sel->set_arch_p)
		  {
		    target_flags &= ~cpu->disable;
		    target_flags |= cpu->enable;
		  }
		break;
	      }

	  if (! cpu->name)
	    error ("bad value (%s) for %s switch", sel->string, sel->name);
	}
    }

  /* If -mfpu or -mno-fpu was explicitly used, don't override with
     the processor default.  */
  if (fpu_option_set)
    target_flags = (target_flags & ~MASK_FPU) | fpu;

  /* Don't allow -mvis if FPU is disabled.  */
  if (! TARGET_FPU)
    target_flags &= ~MASK_VIS;

  /* -mvis assumes UltraSPARC+, so we are sure v9 instructions
     are available.
     -m64 also implies v9.  */
  if (TARGET_VIS || TARGET_ARCH64)
    {
      target_flags |= MASK_V9;
      target_flags &= ~(MASK_V8 | MASK_SPARCLET | MASK_SPARCLITE);
    }

  /* Use the deprecated v8 insns for sparc64 in 32 bit mode.  */
  if (TARGET_V9 && TARGET_ARCH32)
    target_flags |= MASK_DEPRECATED_V8_INSNS;

  /* V8PLUS requires V9, makes no sense in 64 bit mode.  */
  if (! TARGET_V9 || TARGET_ARCH64)
    target_flags &= ~MASK_V8PLUS;

  /* Don't use stack biasing in 32 bit mode.  */
  if (TARGET_ARCH32)
    target_flags &= ~MASK_STACK_BIAS;
    
  /* Supply a default value for align_functions.  */
  if (align_functions == 0
      && (sparc_cpu == PROCESSOR_ULTRASPARC
	  || sparc_cpu == PROCESSOR_ULTRASPARC3
	  || sparc_cpu == PROCESSOR_NIAGARA))
    align_functions = 32;

  /* Validate PCC_STRUCT_RETURN.  */
  if (flag_pcc_struct_return == DEFAULT_PCC_STRUCT_RETURN)
    flag_pcc_struct_return = (TARGET_ARCH64 ? 0 : 1);

  /* Only use .uaxword when compiling for a 64-bit target.  */
  if (!TARGET_ARCH64)
    targetm.asm_out.unaligned_op.di = NULL;

  /* Do various machine dependent initializations.  */
  sparc_init_modes ();

  /* Acquire unique alias sets for our private stuff.  */
  sparc_sr_alias_set = new_alias_set ();
  struct_value_alias_set = new_alias_set ();

  /* Set up function hooks.  */
  init_machine_status = sparc_init_machine_status;

  switch (sparc_cpu)
    {
    case PROCESSOR_V7:
    case PROCESSOR_CYPRESS:
      sparc_costs = &cypress_costs;
      break;
    case PROCESSOR_V8:
    case PROCESSOR_SPARCLITE:
    case PROCESSOR_SUPERSPARC:
      sparc_costs = &supersparc_costs;
      break;
    case PROCESSOR_F930:
    case PROCESSOR_F934:
    case PROCESSOR_HYPERSPARC:
    case PROCESSOR_SPARCLITE86X:
      sparc_costs = &hypersparc_costs;
      break;
    case PROCESSOR_SPARCLET:
    case PROCESSOR_TSC701:
      sparc_costs = &sparclet_costs;
      break;
    case PROCESSOR_V9:
    case PROCESSOR_ULTRASPARC:
      sparc_costs = &ultrasparc_costs;
      break;
    case PROCESSOR_ULTRASPARC3:
      sparc_costs = &ultrasparc3_costs;
      break;
    case PROCESSOR_NIAGARA:
      sparc_costs = &niagara_costs;
      break;
    };

#ifdef TARGET_DEFAULT_LONG_DOUBLE_128
  if (!(target_flags_explicit & MASK_LONG_DOUBLE_128))
    target_flags |= MASK_LONG_DOUBLE_128;
#endif
}

#ifdef SUBTARGET_ATTRIBUTE_TABLE
/* Table of valid machine attributes.  */
const struct attribute_spec sparc_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  SUBTARGET_ATTRIBUTE_TABLE,
  { NULL,        0, 0, false, false, false, NULL }
};
#endif

/* Miscellaneous utilities.  */

/* Nonzero if CODE, a comparison, is suitable for use in v9 conditional move
   or branch on register contents instructions.  */

int
v9_regcmp_p (enum rtx_code code)
{
  return (code == EQ || code == NE || code == GE || code == LT
	  || code == LE || code == GT);
}

/* Nonzero if OP is a floating point constant which can
   be loaded into an integer register using a single
   sethi instruction.  */

int
fp_sethi_p (rtx op)
{
  if (GET_CODE (op) == CONST_DOUBLE)
    {
      REAL_VALUE_TYPE r;
      long i;

      REAL_VALUE_FROM_CONST_DOUBLE (r, op);
      REAL_VALUE_TO_TARGET_SINGLE (r, i);
      return !SPARC_SIMM13_P (i) && SPARC_SETHI_P (i);
    }

  return 0;
}

/* Nonzero if OP is a floating point constant which can
   be loaded into an integer register using a single
   mov instruction.  */

int
fp_mov_p (rtx op)
{
  if (GET_CODE (op) == CONST_DOUBLE)
    {
      REAL_VALUE_TYPE r;
      long i;

      REAL_VALUE_FROM_CONST_DOUBLE (r, op);
      REAL_VALUE_TO_TARGET_SINGLE (r, i);
      return SPARC_SIMM13_P (i);
    }

  return 0;
}

/* Nonzero if OP is a floating point constant which can
   be loaded into an integer register using a high/losum
   instruction sequence.  */

int
fp_high_losum_p (rtx op)
{
  /* The constraints calling this should only be in
     SFmode move insns, so any constant which cannot
     be moved using a single insn will do.  */
  if (GET_CODE (op) == CONST_DOUBLE)
    {
      REAL_VALUE_TYPE r;
      long i;

      REAL_VALUE_FROM_CONST_DOUBLE (r, op);
      REAL_VALUE_TO_TARGET_SINGLE (r, i);
      return !SPARC_SIMM13_P (i) && !SPARC_SETHI_P (i);
    }

  return 0;
}

/* Expand a move instruction.  Return true if all work is done.  */

bool
sparc_expand_move (enum machine_mode mode, rtx *operands)
{
  /* Handle sets of MEM first.  */
  if (GET_CODE (operands[0]) == MEM)
    {
      /* 0 is a register (or a pair of registers) on SPARC.  */
      if (register_or_zero_operand (operands[1], mode))
	return false;

      if (!reload_in_progress)
	{
	  operands[0] = validize_mem (operands[0]);
	  operands[1] = force_reg (mode, operands[1]);
	}
    }

  /* Fixup TLS cases.  */
  if (TARGET_HAVE_TLS
      && CONSTANT_P (operands[1])
      && GET_CODE (operands[1]) != HIGH
      && sparc_tls_referenced_p (operands [1]))
    {
      rtx sym = operands[1];
      rtx addend = NULL;

      if (GET_CODE (sym) == CONST && GET_CODE (XEXP (sym, 0)) == PLUS)
	{
	  addend = XEXP (XEXP (sym, 0), 1);
	  sym = XEXP (XEXP (sym, 0), 0);
	}

      gcc_assert (SPARC_SYMBOL_REF_TLS_P (sym));

      sym = legitimize_tls_address (sym);
      if (addend)
	{
	  sym = gen_rtx_PLUS (mode, sym, addend);
	  sym = force_operand (sym, operands[0]);
	}
      operands[1] = sym;
    }
 
  /* Fixup PIC cases.  */
  if (flag_pic && CONSTANT_P (operands[1]))
    {
      if (pic_address_needs_scratch (operands[1]))
	operands[1] = legitimize_pic_address (operands[1], mode, 0);

      if (GET_CODE (operands[1]) == LABEL_REF && mode == SImode)
	{
	  emit_insn (gen_movsi_pic_label_ref (operands[0], operands[1]));
	  return true;
	}

      if (GET_CODE (operands[1]) == LABEL_REF && mode == DImode)
	{
	  gcc_assert (TARGET_ARCH64);
	  emit_insn (gen_movdi_pic_label_ref (operands[0], operands[1]));
	  return true;
	}

      if (symbolic_operand (operands[1], mode))
	{
	  operands[1] = legitimize_pic_address (operands[1],
						mode,
						(reload_in_progress ?
						 operands[0] :
						 NULL_RTX));
	  return false;
	}
    }

  /* If we are trying to toss an integer constant into FP registers,
     or loading a FP or vector constant, force it into memory.  */
  if (CONSTANT_P (operands[1])
      && REG_P (operands[0])
      && (SPARC_FP_REG_P (REGNO (operands[0]))
	  || SCALAR_FLOAT_MODE_P (mode)
	  || VECTOR_MODE_P (mode)))
    {
      /* emit_group_store will send such bogosity to us when it is
         not storing directly into memory.  So fix this up to avoid
         crashes in output_constant_pool.  */
      if (operands [1] == const0_rtx)
	operands[1] = CONST0_RTX (mode);

      /* We can clear FP registers if TARGET_VIS, and always other regs.  */
      if ((TARGET_VIS || REGNO (operands[0]) < SPARC_FIRST_FP_REG)
	  && const_zero_operand (operands[1], mode))
	return false;

      if (REGNO (operands[0]) < SPARC_FIRST_FP_REG
	  /* We are able to build any SF constant in integer registers
	     with at most 2 instructions.  */
	  && (mode == SFmode
	      /* And any DF constant in integer registers.  */
	      || (mode == DFmode
		  && (reload_completed || reload_in_progress))))
	return false;

      operands[1] = force_const_mem (mode, operands[1]);
      if (!reload_in_progress)
	operands[1] = validize_mem (operands[1]);
      return false;
    }

  /* Accept non-constants and valid constants unmodified.  */
  if (!CONSTANT_P (operands[1])
      || GET_CODE (operands[1]) == HIGH
      || input_operand (operands[1], mode))
    return false;

  switch (mode)
    {
    case QImode:
      /* All QImode constants require only one insn, so proceed.  */
      break;

    case HImode:
    case SImode:
      sparc_emit_set_const32 (operands[0], operands[1]);
      return true;

    case DImode:
      /* input_operand should have filtered out 32-bit mode.  */
      sparc_emit_set_const64 (operands[0], operands[1]);
      return true;
    
    default:
      gcc_unreachable ();
    }

  return false;
}

/* Load OP1, a 32-bit constant, into OP0, a register.
   We know it can't be done in one insn when we get
   here, the move expander guarantees this.  */

void
sparc_emit_set_const32 (rtx op0, rtx op1)
{
  enum machine_mode mode = GET_MODE (op0);
  rtx temp;

  if (reload_in_progress || reload_completed)
    temp = op0;
  else
    temp = gen_reg_rtx (mode);

  if (GET_CODE (op1) == CONST_INT)
    {
      gcc_assert (!small_int_operand (op1, mode)
		  && !const_high_operand (op1, mode));

      /* Emit them as real moves instead of a HIGH/LO_SUM,
	 this way CSE can see everything and reuse intermediate
	 values if it wants.  */
      emit_insn (gen_rtx_SET (VOIDmode, temp,
			      GEN_INT (INTVAL (op1)
			        & ~(HOST_WIDE_INT)0x3ff)));

      emit_insn (gen_rtx_SET (VOIDmode,
			      op0,
			      gen_rtx_IOR (mode, temp,
					   GEN_INT (INTVAL (op1) & 0x3ff))));
    }
  else
    {
      /* A symbol, emit in the traditional way.  */
      emit_insn (gen_rtx_SET (VOIDmode, temp,
			      gen_rtx_HIGH (mode, op1)));
      emit_insn (gen_rtx_SET (VOIDmode,
			      op0, gen_rtx_LO_SUM (mode, temp, op1)));
    }
}

/* Load OP1, a symbolic 64-bit constant, into OP0, a DImode register.
   If TEMP is nonzero, we are forbidden to use any other scratch
   registers.  Otherwise, we are allowed to generate them as needed.

   Note that TEMP may have TImode if the code model is TARGET_CM_MEDANY
   or TARGET_CM_EMBMEDANY (see the reload_indi and reload_outdi patterns).  */

void
sparc_emit_set_symbolic_const64 (rtx op0, rtx op1, rtx temp)
{
  rtx temp1, temp2, temp3, temp4, temp5;
  rtx ti_temp = 0;

  if (temp && GET_MODE (temp) == TImode)
    {
      ti_temp = temp;
      temp = gen_rtx_REG (DImode, REGNO (temp));
    }

  /* SPARC-V9 code-model support.  */
  switch (sparc_cmodel)
    {
    case CM_MEDLOW:
      /* The range spanned by all instructions in the object is less
	 than 2^31 bytes (2GB) and the distance from any instruction
	 to the location of the label _GLOBAL_OFFSET_TABLE_ is less
	 than 2^31 bytes (2GB).

	 The executable must be in the low 4TB of the virtual address
	 space.

	 sethi	%hi(symbol), %temp1
	 or	%temp1, %lo(symbol), %reg  */
      if (temp)
	temp1 = temp;  /* op0 is allowed.  */
      else
	temp1 = gen_reg_rtx (DImode);

      emit_insn (gen_rtx_SET (VOIDmode, temp1, gen_rtx_HIGH (DImode, op1)));
      emit_insn (gen_rtx_SET (VOIDmode, op0, gen_rtx_LO_SUM (DImode, temp1, op1)));
      break;

    case CM_MEDMID:
      /* The range spanned by all instructions in the object is less
	 than 2^31 bytes (2GB) and the distance from any instruction
	 to the location of the label _GLOBAL_OFFSET_TABLE_ is less
	 than 2^31 bytes (2GB).

	 The executable must be in the low 16TB of the virtual address
	 space.

	 sethi	%h44(symbol), %temp1
	 or	%temp1, %m44(symbol), %temp2
	 sllx	%temp2, 12, %temp3
	 or	%temp3, %l44(symbol), %reg  */
      if (temp)
	{
	  temp1 = op0;
	  temp2 = op0;
	  temp3 = temp;  /* op0 is allowed.  */
	}
      else
	{
	  temp1 = gen_reg_rtx (DImode);
	  temp2 = gen_reg_rtx (DImode);
	  temp3 = gen_reg_rtx (DImode);
	}

      emit_insn (gen_seth44 (temp1, op1));
      emit_insn (gen_setm44 (temp2, temp1, op1));
      emit_insn (gen_rtx_SET (VOIDmode, temp3,
			      gen_rtx_ASHIFT (DImode, temp2, GEN_INT (12))));
      emit_insn (gen_setl44 (op0, temp3, op1));
      break;

    case CM_MEDANY:
      /* The range spanned by all instructions in the object is less
	 than 2^31 bytes (2GB) and the distance from any instruction
	 to the location of the label _GLOBAL_OFFSET_TABLE_ is less
	 than 2^31 bytes (2GB).

	 The executable can be placed anywhere in the virtual address
	 space.

	 sethi	%hh(symbol), %temp1
	 sethi	%lm(symbol), %temp2
	 or	%temp1, %hm(symbol), %temp3
	 sllx	%temp3, 32, %temp4
	 or	%temp4, %temp2, %temp5
	 or	%temp5, %lo(symbol), %reg  */
      if (temp)
	{
	  /* It is possible that one of the registers we got for operands[2]
	     might coincide with that of operands[0] (which is why we made
	     it TImode).  Pick the other one to use as our scratch.  */
	  if (rtx_equal_p (temp, op0))
	    {
	      gcc_assert (ti_temp);
	      temp = gen_rtx_REG (DImode, REGNO (temp) + 1);
	    }
	  temp1 = op0;
	  temp2 = temp;  /* op0 is _not_ allowed, see above.  */
	  temp3 = op0;
	  temp4 = op0;
	  temp5 = op0;
	}
      else
	{
	  temp1 = gen_reg_rtx (DImode);
	  temp2 = gen_reg_rtx (DImode);
	  temp3 = gen_reg_rtx (DImode);
	  temp4 = gen_reg_rtx (DImode);
	  temp5 = gen_reg_rtx (DImode);
	}

      emit_insn (gen_sethh (temp1, op1));
      emit_insn (gen_setlm (temp2, op1));
      emit_insn (gen_sethm (temp3, temp1, op1));
      emit_insn (gen_rtx_SET (VOIDmode, temp4,
			      gen_rtx_ASHIFT (DImode, temp3, GEN_INT (32))));
      emit_insn (gen_rtx_SET (VOIDmode, temp5,
			      gen_rtx_PLUS (DImode, temp4, temp2)));
      emit_insn (gen_setlo (op0, temp5, op1));
      break;

    case CM_EMBMEDANY:
      /* Old old old backwards compatibility kruft here.
	 Essentially it is MEDLOW with a fixed 64-bit
	 virtual base added to all data segment addresses.
	 Text-segment stuff is computed like MEDANY, we can't
	 reuse the code above because the relocation knobs
	 look different.

	 Data segment:	sethi	%hi(symbol), %temp1
			add	%temp1, EMBMEDANY_BASE_REG, %temp2
			or	%temp2, %lo(symbol), %reg  */
      if (data_segment_operand (op1, GET_MODE (op1)))
	{
	  if (temp)
	    {
	      temp1 = temp;  /* op0 is allowed.  */
	      temp2 = op0;
	    }
	  else
	    {
	      temp1 = gen_reg_rtx (DImode);
	      temp2 = gen_reg_rtx (DImode);
	    }

	  emit_insn (gen_embmedany_sethi (temp1, op1));
	  emit_insn (gen_embmedany_brsum (temp2, temp1));
	  emit_insn (gen_embmedany_losum (op0, temp2, op1));
	}

      /* Text segment:	sethi	%uhi(symbol), %temp1
			sethi	%hi(symbol), %temp2
			or	%temp1, %ulo(symbol), %temp3
			sllx	%temp3, 32, %temp4
			or	%temp4, %temp2, %temp5
			or	%temp5, %lo(symbol), %reg  */
      else
	{
	  if (temp)
	    {
	      /* It is possible that one of the registers we got for operands[2]
		 might coincide with that of operands[0] (which is why we made
		 it TImode).  Pick the other one to use as our scratch.  */
	      if (rtx_equal_p (temp, op0))
		{
		  gcc_assert (ti_temp);
		  temp = gen_rtx_REG (DImode, REGNO (temp) + 1);
		}
	      temp1 = op0;
	      temp2 = temp;  /* op0 is _not_ allowed, see above.  */
	      temp3 = op0;
	      temp4 = op0;
	      temp5 = op0;
	    }
	  else
	    {
	      temp1 = gen_reg_rtx (DImode);
	      temp2 = gen_reg_rtx (DImode);
	      temp3 = gen_reg_rtx (DImode);
	      temp4 = gen_reg_rtx (DImode);
	      temp5 = gen_reg_rtx (DImode);
	    }

	  emit_insn (gen_embmedany_textuhi (temp1, op1));
	  emit_insn (gen_embmedany_texthi  (temp2, op1));
	  emit_insn (gen_embmedany_textulo (temp3, temp1, op1));
	  emit_insn (gen_rtx_SET (VOIDmode, temp4,
				  gen_rtx_ASHIFT (DImode, temp3, GEN_INT (32))));
	  emit_insn (gen_rtx_SET (VOIDmode, temp5,
				  gen_rtx_PLUS (DImode, temp4, temp2)));
	  emit_insn (gen_embmedany_textlo  (op0, temp5, op1));
	}
      break;

    default:
      gcc_unreachable ();
    }
}

#if HOST_BITS_PER_WIDE_INT == 32
void
sparc_emit_set_const64 (rtx op0 ATTRIBUTE_UNUSED, rtx op1 ATTRIBUTE_UNUSED)
{
  gcc_unreachable ();
}
#else
/* These avoid problems when cross compiling.  If we do not
   go through all this hair then the optimizer will see
   invalid REG_EQUAL notes or in some cases none at all.  */
static rtx gen_safe_HIGH64 (rtx, HOST_WIDE_INT);
static rtx gen_safe_SET64 (rtx, HOST_WIDE_INT);
static rtx gen_safe_OR64 (rtx, HOST_WIDE_INT);
static rtx gen_safe_XOR64 (rtx, HOST_WIDE_INT);

/* The optimizer is not to assume anything about exactly
   which bits are set for a HIGH, they are unspecified.
   Unfortunately this leads to many missed optimizations
   during CSE.  We mask out the non-HIGH bits, and matches
   a plain movdi, to alleviate this problem.  */
static rtx
gen_safe_HIGH64 (rtx dest, HOST_WIDE_INT val)
{
  return gen_rtx_SET (VOIDmode, dest, GEN_INT (val & ~(HOST_WIDE_INT)0x3ff));
}

static rtx
gen_safe_SET64 (rtx dest, HOST_WIDE_INT val)
{
  return gen_rtx_SET (VOIDmode, dest, GEN_INT (val));
}

static rtx
gen_safe_OR64 (rtx src, HOST_WIDE_INT val)
{
  return gen_rtx_IOR (DImode, src, GEN_INT (val));
}

static rtx
gen_safe_XOR64 (rtx src, HOST_WIDE_INT val)
{
  return gen_rtx_XOR (DImode, src, GEN_INT (val));
}

/* Worker routines for 64-bit constant formation on arch64.
   One of the key things to be doing in these emissions is
   to create as many temp REGs as possible.  This makes it
   possible for half-built constants to be used later when
   such values are similar to something required later on.
   Without doing this, the optimizer cannot see such
   opportunities.  */

static void sparc_emit_set_const64_quick1 (rtx, rtx,
					   unsigned HOST_WIDE_INT, int);

static void
sparc_emit_set_const64_quick1 (rtx op0, rtx temp,
			       unsigned HOST_WIDE_INT low_bits, int is_neg)
{
  unsigned HOST_WIDE_INT high_bits;

  if (is_neg)
    high_bits = (~low_bits) & 0xffffffff;
  else
    high_bits = low_bits;

  emit_insn (gen_safe_HIGH64 (temp, high_bits));
  if (!is_neg)
    {
      emit_insn (gen_rtx_SET (VOIDmode, op0,
			      gen_safe_OR64 (temp, (high_bits & 0x3ff))));
    }
  else
    {
      /* If we are XOR'ing with -1, then we should emit a one's complement
	 instead.  This way the combiner will notice logical operations
	 such as ANDN later on and substitute.  */
      if ((low_bits & 0x3ff) == 0x3ff)
	{
	  emit_insn (gen_rtx_SET (VOIDmode, op0,
				  gen_rtx_NOT (DImode, temp)));
	}
      else
	{
	  emit_insn (gen_rtx_SET (VOIDmode, op0,
				  gen_safe_XOR64 (temp,
						  (-(HOST_WIDE_INT)0x400
						   | (low_bits & 0x3ff)))));
	}
    }
}

static void sparc_emit_set_const64_quick2 (rtx, rtx, unsigned HOST_WIDE_INT,
					   unsigned HOST_WIDE_INT, int);

static void
sparc_emit_set_const64_quick2 (rtx op0, rtx temp,
			       unsigned HOST_WIDE_INT high_bits,
			       unsigned HOST_WIDE_INT low_immediate,
			       int shift_count)
{
  rtx temp2 = op0;

  if ((high_bits & 0xfffffc00) != 0)
    {
      emit_insn (gen_safe_HIGH64 (temp, high_bits));
      if ((high_bits & ~0xfffffc00) != 0)
	emit_insn (gen_rtx_SET (VOIDmode, op0,
				gen_safe_OR64 (temp, (high_bits & 0x3ff))));
      else
	temp2 = temp;
    }
  else
    {
      emit_insn (gen_safe_SET64 (temp, high_bits));
      temp2 = temp;
    }

  /* Now shift it up into place.  */
  emit_insn (gen_rtx_SET (VOIDmode, op0,
			  gen_rtx_ASHIFT (DImode, temp2,
					  GEN_INT (shift_count))));

  /* If there is a low immediate part piece, finish up by
     putting that in as well.  */
  if (low_immediate != 0)
    emit_insn (gen_rtx_SET (VOIDmode, op0,
			    gen_safe_OR64 (op0, low_immediate)));
}

static void sparc_emit_set_const64_longway (rtx, rtx, unsigned HOST_WIDE_INT,
					    unsigned HOST_WIDE_INT);

/* Full 64-bit constant decomposition.  Even though this is the
   'worst' case, we still optimize a few things away.  */
static void
sparc_emit_set_const64_longway (rtx op0, rtx temp,
				unsigned HOST_WIDE_INT high_bits,
				unsigned HOST_WIDE_INT low_bits)
{
  rtx sub_temp;

  if (reload_in_progress || reload_completed)
    sub_temp = op0;
  else
    sub_temp = gen_reg_rtx (DImode);

  if ((high_bits & 0xfffffc00) != 0)
    {
      emit_insn (gen_safe_HIGH64 (temp, high_bits));
      if ((high_bits & ~0xfffffc00) != 0)
	emit_insn (gen_rtx_SET (VOIDmode,
				sub_temp,
				gen_safe_OR64 (temp, (high_bits & 0x3ff))));
      else
	sub_temp = temp;
    }
  else
    {
      emit_insn (gen_safe_SET64 (temp, high_bits));
      sub_temp = temp;
    }

  if (!reload_in_progress && !reload_completed)
    {
      rtx temp2 = gen_reg_rtx (DImode);
      rtx temp3 = gen_reg_rtx (DImode);
      rtx temp4 = gen_reg_rtx (DImode);

      emit_insn (gen_rtx_SET (VOIDmode, temp4,
			      gen_rtx_ASHIFT (DImode, sub_temp,
					      GEN_INT (32))));

      emit_insn (gen_safe_HIGH64 (temp2, low_bits));
      if ((low_bits & ~0xfffffc00) != 0)
	{
	  emit_insn (gen_rtx_SET (VOIDmode, temp3,
				  gen_safe_OR64 (temp2, (low_bits & 0x3ff))));
	  emit_insn (gen_rtx_SET (VOIDmode, op0,
				  gen_rtx_PLUS (DImode, temp4, temp3)));
	}
      else
	{
	  emit_insn (gen_rtx_SET (VOIDmode, op0,
				  gen_rtx_PLUS (DImode, temp4, temp2)));
	}
    }
  else
    {
      rtx low1 = GEN_INT ((low_bits >> (32 - 12))          & 0xfff);
      rtx low2 = GEN_INT ((low_bits >> (32 - 12 - 12))     & 0xfff);
      rtx low3 = GEN_INT ((low_bits >> (32 - 12 - 12 - 8)) & 0x0ff);
      int to_shift = 12;

      /* We are in the middle of reload, so this is really
	 painful.  However we do still make an attempt to
	 avoid emitting truly stupid code.  */
      if (low1 != const0_rtx)
	{
	  emit_insn (gen_rtx_SET (VOIDmode, op0,
				  gen_rtx_ASHIFT (DImode, sub_temp,
						  GEN_INT (to_shift))));
	  emit_insn (gen_rtx_SET (VOIDmode, op0,
				  gen_rtx_IOR (DImode, op0, low1)));
	  sub_temp = op0;
	  to_shift = 12;
	}
      else
	{
	  to_shift += 12;
	}
      if (low2 != const0_rtx)
	{
	  emit_insn (gen_rtx_SET (VOIDmode, op0,
				  gen_rtx_ASHIFT (DImode, sub_temp,
						  GEN_INT (to_shift))));
	  emit_insn (gen_rtx_SET (VOIDmode, op0,
				  gen_rtx_IOR (DImode, op0, low2)));
	  sub_temp = op0;
	  to_shift = 8;
	}
      else
	{
	  to_shift += 8;
	}
      emit_insn (gen_rtx_SET (VOIDmode, op0,
			      gen_rtx_ASHIFT (DImode, sub_temp,
					      GEN_INT (to_shift))));
      if (low3 != const0_rtx)
	emit_insn (gen_rtx_SET (VOIDmode, op0,
				gen_rtx_IOR (DImode, op0, low3)));
      /* phew...  */
    }
}

/* Analyze a 64-bit constant for certain properties.  */
static void analyze_64bit_constant (unsigned HOST_WIDE_INT,
				    unsigned HOST_WIDE_INT,
				    int *, int *, int *);

static void
analyze_64bit_constant (unsigned HOST_WIDE_INT high_bits,
			unsigned HOST_WIDE_INT low_bits,
			int *hbsp, int *lbsp, int *abbasp)
{
  int lowest_bit_set, highest_bit_set, all_bits_between_are_set;
  int i;

  lowest_bit_set = highest_bit_set = -1;
  i = 0;
  do
    {
      if ((lowest_bit_set == -1)
	  && ((low_bits >> i) & 1))
	lowest_bit_set = i;
      if ((highest_bit_set == -1)
	  && ((high_bits >> (32 - i - 1)) & 1))
	highest_bit_set = (64 - i - 1);
    }
  while (++i < 32
	 && ((highest_bit_set == -1)
	     || (lowest_bit_set == -1)));
  if (i == 32)
    {
      i = 0;
      do
	{
	  if ((lowest_bit_set == -1)
	      && ((high_bits >> i) & 1))
	    lowest_bit_set = i + 32;
	  if ((highest_bit_set == -1)
	      && ((low_bits >> (32 - i - 1)) & 1))
	    highest_bit_set = 32 - i - 1;
	}
      while (++i < 32
	     && ((highest_bit_set == -1)
		 || (lowest_bit_set == -1)));
    }
  /* If there are no bits set this should have gone out
     as one instruction!  */
  gcc_assert (lowest_bit_set != -1 && highest_bit_set != -1);
  all_bits_between_are_set = 1;
  for (i = lowest_bit_set; i <= highest_bit_set; i++)
    {
      if (i < 32)
	{
	  if ((low_bits & (1 << i)) != 0)
	    continue;
	}
      else
	{
	  if ((high_bits & (1 << (i - 32))) != 0)
	    continue;
	}
      all_bits_between_are_set = 0;
      break;
    }
  *hbsp = highest_bit_set;
  *lbsp = lowest_bit_set;
  *abbasp = all_bits_between_are_set;
}

static int const64_is_2insns (unsigned HOST_WIDE_INT, unsigned HOST_WIDE_INT);

static int
const64_is_2insns (unsigned HOST_WIDE_INT high_bits,
		   unsigned HOST_WIDE_INT low_bits)
{
  int highest_bit_set, lowest_bit_set, all_bits_between_are_set;

  if (high_bits == 0
      || high_bits == 0xffffffff)
    return 1;

  analyze_64bit_constant (high_bits, low_bits,
			  &highest_bit_set, &lowest_bit_set,
			  &all_bits_between_are_set);

  if ((highest_bit_set == 63
       || lowest_bit_set == 0)
      && all_bits_between_are_set != 0)
    return 1;

  if ((highest_bit_set - lowest_bit_set) < 21)
    return 1;

  return 0;
}

static unsigned HOST_WIDE_INT create_simple_focus_bits (unsigned HOST_WIDE_INT,
							unsigned HOST_WIDE_INT,
							int, int);

static unsigned HOST_WIDE_INT
create_simple_focus_bits (unsigned HOST_WIDE_INT high_bits,
			  unsigned HOST_WIDE_INT low_bits,
			  int lowest_bit_set, int shift)
{
  HOST_WIDE_INT hi, lo;

  if (lowest_bit_set < 32)
    {
      lo = (low_bits >> lowest_bit_set) << shift;
      hi = ((high_bits << (32 - lowest_bit_set)) << shift);
    }
  else
    {
      lo = 0;
      hi = ((high_bits >> (lowest_bit_set - 32)) << shift);
    }
  gcc_assert (! (hi & lo));
  return (hi | lo);
}

/* Here we are sure to be arch64 and this is an integer constant
   being loaded into a register.  Emit the most efficient
   insn sequence possible.  Detection of all the 1-insn cases
   has been done already.  */
void
sparc_emit_set_const64 (rtx op0, rtx op1)
{
  unsigned HOST_WIDE_INT high_bits, low_bits;
  int lowest_bit_set, highest_bit_set;
  int all_bits_between_are_set;
  rtx temp = 0;

  /* Sanity check that we know what we are working with.  */
  gcc_assert (TARGET_ARCH64
	      && (GET_CODE (op0) == SUBREG
		  || (REG_P (op0) && ! SPARC_FP_REG_P (REGNO (op0)))));

  if (reload_in_progress || reload_completed)
    temp = op0;

  if (GET_CODE (op1) != CONST_INT)
    {
      sparc_emit_set_symbolic_const64 (op0, op1, temp);
      return;
    }

  if (! temp)
    temp = gen_reg_rtx (DImode);

  high_bits = ((INTVAL (op1) >> 32) & 0xffffffff);
  low_bits = (INTVAL (op1) & 0xffffffff);

  /* low_bits	bits 0  --> 31
     high_bits	bits 32 --> 63  */

  analyze_64bit_constant (high_bits, low_bits,
			  &highest_bit_set, &lowest_bit_set,
			  &all_bits_between_are_set);

  /* First try for a 2-insn sequence.  */

  /* These situations are preferred because the optimizer can
   * do more things with them:
   * 1) mov	-1, %reg
   *    sllx	%reg, shift, %reg
   * 2) mov	-1, %reg
   *    srlx	%reg, shift, %reg
   * 3) mov	some_small_const, %reg
   *    sllx	%reg, shift, %reg
   */
  if (((highest_bit_set == 63
	|| lowest_bit_set == 0)
       && all_bits_between_are_set != 0)
      || ((highest_bit_set - lowest_bit_set) < 12))
    {
      HOST_WIDE_INT the_const = -1;
      int shift = lowest_bit_set;

      if ((highest_bit_set != 63
	   && lowest_bit_set != 0)
	  || all_bits_between_are_set == 0)
	{
	  the_const =
	    create_simple_focus_bits (high_bits, low_bits,
				      lowest_bit_set, 0);
	}
      else if (lowest_bit_set == 0)
	shift = -(63 - highest_bit_set);

      gcc_assert (SPARC_SIMM13_P (the_const));
      gcc_assert (shift != 0);

      emit_insn (gen_safe_SET64 (temp, the_const));
      if (shift > 0)
	emit_insn (gen_rtx_SET (VOIDmode,
				op0,
				gen_rtx_ASHIFT (DImode,
						temp,
						GEN_INT (shift))));
      else if (shift < 0)
	emit_insn (gen_rtx_SET (VOIDmode,
				op0,
				gen_rtx_LSHIFTRT (DImode,
						  temp,
						  GEN_INT (-shift))));
      return;
    }

  /* Now a range of 22 or less bits set somewhere.
   * 1) sethi	%hi(focus_bits), %reg
   *    sllx	%reg, shift, %reg
   * 2) sethi	%hi(focus_bits), %reg
   *    srlx	%reg, shift, %reg
   */
  if ((highest_bit_set - lowest_bit_set) < 21)
    {
      unsigned HOST_WIDE_INT focus_bits =
	create_simple_focus_bits (high_bits, low_bits,
				  lowest_bit_set, 10);

      gcc_assert (SPARC_SETHI_P (focus_bits));
      gcc_assert (lowest_bit_set != 10);

      emit_insn (gen_safe_HIGH64 (temp, focus_bits));

      /* If lowest_bit_set == 10 then a sethi alone could have done it.  */
      if (lowest_bit_set < 10)
	emit_insn (gen_rtx_SET (VOIDmode,
				op0,
				gen_rtx_LSHIFTRT (DImode, temp,
						  GEN_INT (10 - lowest_bit_set))));
      else if (lowest_bit_set > 10)
	emit_insn (gen_rtx_SET (VOIDmode,
				op0,
				gen_rtx_ASHIFT (DImode, temp,
						GEN_INT (lowest_bit_set - 10))));
      return;
    }

  /* 1) sethi	%hi(low_bits), %reg
   *    or	%reg, %lo(low_bits), %reg
   * 2) sethi	%hi(~low_bits), %reg
   *	xor	%reg, %lo(-0x400 | (low_bits & 0x3ff)), %reg
   */
  if (high_bits == 0
      || high_bits == 0xffffffff)
    {
      sparc_emit_set_const64_quick1 (op0, temp, low_bits,
				     (high_bits == 0xffffffff));
      return;
    }

  /* Now, try 3-insn sequences.  */

  /* 1) sethi	%hi(high_bits), %reg
   *    or	%reg, %lo(high_bits), %reg
   *    sllx	%reg, 32, %reg
   */
  if (low_bits == 0)
    {
      sparc_emit_set_const64_quick2 (op0, temp, high_bits, 0, 32);
      return;
    }

  /* We may be able to do something quick
     when the constant is negated, so try that.  */
  if (const64_is_2insns ((~high_bits) & 0xffffffff,
			 (~low_bits) & 0xfffffc00))
    {
      /* NOTE: The trailing bits get XOR'd so we need the
	 non-negated bits, not the negated ones.  */
      unsigned HOST_WIDE_INT trailing_bits = low_bits & 0x3ff;

      if ((((~high_bits) & 0xffffffff) == 0
	   && ((~low_bits) & 0x80000000) == 0)
	  || (((~high_bits) & 0xffffffff) == 0xffffffff
	      && ((~low_bits) & 0x80000000) != 0))
	{
	  unsigned HOST_WIDE_INT fast_int = (~low_bits & 0xffffffff);

	  if ((SPARC_SETHI_P (fast_int)
	       && (~high_bits & 0xffffffff) == 0)
	      || SPARC_SIMM13_P (fast_int))
	    emit_insn (gen_safe_SET64 (temp, fast_int));
	  else
	    sparc_emit_set_const64 (temp, GEN_INT (fast_int));
	}
      else
	{
	  rtx negated_const;
	  negated_const = GEN_INT (((~low_bits) & 0xfffffc00) |
				   (((HOST_WIDE_INT)((~high_bits) & 0xffffffff))<<32));
	  sparc_emit_set_const64 (temp, negated_const);
	}

      /* If we are XOR'ing with -1, then we should emit a one's complement
	 instead.  This way the combiner will notice logical operations
	 such as ANDN later on and substitute.  */
      if (trailing_bits == 0x3ff)
	{
	  emit_insn (gen_rtx_SET (VOIDmode, op0,
				  gen_rtx_NOT (DImode, temp)));
	}
      else
	{
	  emit_insn (gen_rtx_SET (VOIDmode,
				  op0,
				  gen_safe_XOR64 (temp,
						  (-0x400 | trailing_bits))));
	}
      return;
    }

  /* 1) sethi	%hi(xxx), %reg
   *    or	%reg, %lo(xxx), %reg
   *	sllx	%reg, yyy, %reg
   *
   * ??? This is just a generalized version of the low_bits==0
   * thing above, FIXME...
   */
  if ((highest_bit_set - lowest_bit_set) < 32)
    {
      unsigned HOST_WIDE_INT focus_bits =
	create_simple_focus_bits (high_bits, low_bits,
				  lowest_bit_set, 0);

      /* We can't get here in this state.  */
      gcc_assert (highest_bit_set >= 32 && lowest_bit_set < 32);

      /* So what we know is that the set bits straddle the
	 middle of the 64-bit word.  */
      sparc_emit_set_const64_quick2 (op0, temp,
				     focus_bits, 0,
				     lowest_bit_set);
      return;
    }

  /* 1) sethi	%hi(high_bits), %reg
   *    or	%reg, %lo(high_bits), %reg
   *    sllx	%reg, 32, %reg
   *	or	%reg, low_bits, %reg
   */
  if (SPARC_SIMM13_P(low_bits)
      && ((int)low_bits > 0))
    {
      sparc_emit_set_const64_quick2 (op0, temp, high_bits, low_bits, 32);
      return;
    }

  /* The easiest way when all else fails, is full decomposition.  */
#if 0
  printf ("sparc_emit_set_const64: Hard constant [%08lx%08lx] neg[%08lx%08lx]\n",
	  high_bits, low_bits, ~high_bits, ~low_bits);
#endif
  sparc_emit_set_const64_longway (op0, temp, high_bits, low_bits);
}
#endif /* HOST_BITS_PER_WIDE_INT == 32 */

/* Given a comparison code (EQ, NE, etc.) and the first operand of a COMPARE,
   return the mode to be used for the comparison.  For floating-point,
   CCFP[E]mode is used.  CC_NOOVmode should be used when the first operand
   is a PLUS, MINUS, NEG, or ASHIFT.  CCmode should be used when no special
   processing is needed.  */

enum machine_mode
select_cc_mode (enum rtx_code op, rtx x, rtx y ATTRIBUTE_UNUSED)
{
  if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
    {
      switch (op)
	{
	case EQ:
	case NE:
	case UNORDERED:
	case ORDERED:
	case UNLT:
	case UNLE:
	case UNGT:
	case UNGE:
	case UNEQ:
	case LTGT:
	  return CCFPmode;

	case LT:
	case LE:
	case GT:
	case GE:
	  return CCFPEmode;

	default:
	  gcc_unreachable ();
	}
    }
  else if (GET_CODE (x) == PLUS || GET_CODE (x) == MINUS
	   || GET_CODE (x) == NEG || GET_CODE (x) == ASHIFT)
    {
      if (TARGET_ARCH64 && GET_MODE (x) == DImode)
	return CCX_NOOVmode;
      else
	return CC_NOOVmode;
    }
  else
    {
      if (TARGET_ARCH64 && GET_MODE (x) == DImode)
	return CCXmode;
      else
	return CCmode;
    }
}

/* X and Y are two things to compare using CODE.  Emit the compare insn and
   return the rtx for the cc reg in the proper mode.  */

rtx
gen_compare_reg (enum rtx_code code)
{
  rtx x = sparc_compare_op0;
  rtx y = sparc_compare_op1;
  enum machine_mode mode = SELECT_CC_MODE (code, x, y);
  rtx cc_reg;

  if (sparc_compare_emitted != NULL_RTX)
    {
      cc_reg = sparc_compare_emitted;
      sparc_compare_emitted = NULL_RTX;
      return cc_reg;
    }

  /* ??? We don't have movcc patterns so we cannot generate pseudo regs for the
     fcc regs (cse can't tell they're really call clobbered regs and will
     remove a duplicate comparison even if there is an intervening function
     call - it will then try to reload the cc reg via an int reg which is why
     we need the movcc patterns).  It is possible to provide the movcc
     patterns by using the ldxfsr/stxfsr v9 insns.  I tried it: you need two
     registers (say %g1,%g5) and it takes about 6 insns.  A better fix would be
     to tell cse that CCFPE mode registers (even pseudos) are call
     clobbered.  */

  /* ??? This is an experiment.  Rather than making changes to cse which may
     or may not be easy/clean, we do our own cse.  This is possible because
     we will generate hard registers.  Cse knows they're call clobbered (it
     doesn't know the same thing about pseudos). If we guess wrong, no big
     deal, but if we win, great!  */

  if (TARGET_V9 && GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
#if 1 /* experiment */
    {
      int reg;
      /* We cycle through the registers to ensure they're all exercised.  */
      static int next_fcc_reg = 0;
      /* Previous x,y for each fcc reg.  */
      static rtx prev_args[4][2];

      /* Scan prev_args for x,y.  */
      for (reg = 0; reg < 4; reg++)
	if (prev_args[reg][0] == x && prev_args[reg][1] == y)
	  break;
      if (reg == 4)
	{
	  reg = next_fcc_reg;
	  prev_args[reg][0] = x;
	  prev_args[reg][1] = y;
	  next_fcc_reg = (next_fcc_reg + 1) & 3;
	}
      cc_reg = gen_rtx_REG (mode, reg + SPARC_FIRST_V9_FCC_REG);
    }
#else
    cc_reg = gen_reg_rtx (mode);
#endif /* ! experiment */
  else if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
    cc_reg = gen_rtx_REG (mode, SPARC_FCC_REG);
  else
    cc_reg = gen_rtx_REG (mode, SPARC_ICC_REG);

  emit_insn (gen_rtx_SET (VOIDmode, cc_reg,
			  gen_rtx_COMPARE (mode, x, y)));

  return cc_reg;
}

/* This function is used for v9 only.
   CODE is the code for an Scc's comparison.
   OPERANDS[0] is the target of the Scc insn.
   OPERANDS[1] is the value we compare against const0_rtx (which hasn't
   been generated yet).

   This function is needed to turn

	   (set (reg:SI 110)
	       (gt (reg:CCX 100 %icc)
	           (const_int 0)))
   into
	   (set (reg:SI 110)
	       (gt:DI (reg:CCX 100 %icc)
	           (const_int 0)))

   IE: The instruction recognizer needs to see the mode of the comparison to
   find the right instruction. We could use "gt:DI" right in the
   define_expand, but leaving it out allows us to handle DI, SI, etc.

   We refer to the global sparc compare operands sparc_compare_op0 and
   sparc_compare_op1.  */

int
gen_v9_scc (enum rtx_code compare_code, register rtx *operands)
{
  if (! TARGET_ARCH64
      && (GET_MODE (sparc_compare_op0) == DImode
	  || GET_MODE (operands[0]) == DImode))
    return 0;

  /* Try to use the movrCC insns.  */
  if (TARGET_ARCH64
      && GET_MODE_CLASS (GET_MODE (sparc_compare_op0)) == MODE_INT
      && sparc_compare_op1 == const0_rtx
      && v9_regcmp_p (compare_code))
    {
      rtx op0 = sparc_compare_op0;
      rtx temp;

      /* Special case for op0 != 0.  This can be done with one instruction if
	 operands[0] == sparc_compare_op0.  */

      if (compare_code == NE
	  && GET_MODE (operands[0]) == DImode
	  && rtx_equal_p (op0, operands[0]))
	{
	  emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			      gen_rtx_IF_THEN_ELSE (DImode,
				       gen_rtx_fmt_ee (compare_code, DImode,
						       op0, const0_rtx),
				       const1_rtx,
				       operands[0])));
	  return 1;
	}

      if (reg_overlap_mentioned_p (operands[0], op0))
	{
	  /* Handle the case where operands[0] == sparc_compare_op0.
	     We "early clobber" the result.  */
	  op0 = gen_reg_rtx (GET_MODE (sparc_compare_op0));
	  emit_move_insn (op0, sparc_compare_op0);
	}

      emit_insn (gen_rtx_SET (VOIDmode, operands[0], const0_rtx));
      if (GET_MODE (op0) != DImode)
	{
	  temp = gen_reg_rtx (DImode);
	  convert_move (temp, op0, 0);
	}
      else
	temp = op0;
      emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			  gen_rtx_IF_THEN_ELSE (GET_MODE (operands[0]),
				   gen_rtx_fmt_ee (compare_code, DImode,
						   temp, const0_rtx),
				   const1_rtx,
				   operands[0])));
      return 1;
    }
  else
    {
      operands[1] = gen_compare_reg (compare_code);

      switch (GET_MODE (operands[1]))
	{
	  case CCmode :
	  case CCXmode :
	  case CCFPEmode :
	  case CCFPmode :
	    break;
	  default :
	    gcc_unreachable ();
	}
      emit_insn (gen_rtx_SET (VOIDmode, operands[0], const0_rtx));
      emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			  gen_rtx_IF_THEN_ELSE (GET_MODE (operands[0]),
				   gen_rtx_fmt_ee (compare_code,
						   GET_MODE (operands[1]),
						   operands[1], const0_rtx),
				    const1_rtx, operands[0])));
      return 1;
    }
}

/* Emit a conditional jump insn for the v9 architecture using comparison code
   CODE and jump target LABEL.
   This function exists to take advantage of the v9 brxx insns.  */

void
emit_v9_brxx_insn (enum rtx_code code, rtx op0, rtx label)
{
  gcc_assert (sparc_compare_emitted == NULL_RTX);
  emit_jump_insn (gen_rtx_SET (VOIDmode,
			   pc_rtx,
			   gen_rtx_IF_THEN_ELSE (VOIDmode,
				    gen_rtx_fmt_ee (code, GET_MODE (op0),
						    op0, const0_rtx),
				    gen_rtx_LABEL_REF (VOIDmode, label),
				    pc_rtx)));
}

/* Generate a DFmode part of a hard TFmode register.
   REG is the TFmode hard register, LOW is 1 for the
   low 64bit of the register and 0 otherwise.
 */
rtx
gen_df_reg (rtx reg, int low)
{
  int regno = REGNO (reg);

  if ((WORDS_BIG_ENDIAN == 0) ^ (low != 0))
    regno += (TARGET_ARCH64 && regno < 32) ? 1 : 2;
  return gen_rtx_REG (DFmode, regno);
}

/* Generate a call to FUNC with OPERANDS.  Operand 0 is the return value.
   Unlike normal calls, TFmode operands are passed by reference.  It is
   assumed that no more than 3 operands are required.  */

static void
emit_soft_tfmode_libcall (const char *func_name, int nargs, rtx *operands)
{
  rtx ret_slot = NULL, arg[3], func_sym;
  int i;

  /* We only expect to be called for conversions, unary, and binary ops.  */
  gcc_assert (nargs == 2 || nargs == 3);

  for (i = 0; i < nargs; ++i)
    {
      rtx this_arg = operands[i];
      rtx this_slot;

      /* TFmode arguments and return values are passed by reference.  */
      if (GET_MODE (this_arg) == TFmode)
	{
	  int force_stack_temp;

	  force_stack_temp = 0;
	  if (TARGET_BUGGY_QP_LIB && i == 0)
	    force_stack_temp = 1;

	  if (GET_CODE (this_arg) == MEM
	      && ! force_stack_temp)
	    this_arg = XEXP (this_arg, 0);
	  else if (CONSTANT_P (this_arg)
		   && ! force_stack_temp)
	    {
	      this_slot = force_const_mem (TFmode, this_arg);
	      this_arg = XEXP (this_slot, 0);
	    }
	  else
	    {
	      this_slot = assign_stack_temp (TFmode, GET_MODE_SIZE (TFmode), 0);

	      /* Operand 0 is the return value.  We'll copy it out later.  */
	      if (i > 0)
		emit_move_insn (this_slot, this_arg);
	      else
		ret_slot = this_slot;

	      this_arg = XEXP (this_slot, 0);
	    }
	}

      arg[i] = this_arg;
    }

  func_sym = gen_rtx_SYMBOL_REF (Pmode, func_name);

  if (GET_MODE (operands[0]) == TFmode)
    {
      if (nargs == 2)
	emit_library_call (func_sym, LCT_NORMAL, VOIDmode, 2,
			   arg[0], GET_MODE (arg[0]),
			   arg[1], GET_MODE (arg[1]));
      else
	emit_library_call (func_sym, LCT_NORMAL, VOIDmode, 3,
			   arg[0], GET_MODE (arg[0]),
			   arg[1], GET_MODE (arg[1]),
			   arg[2], GET_MODE (arg[2]));

      if (ret_slot)
	emit_move_insn (operands[0], ret_slot);
    }
  else
    {
      rtx ret;

      gcc_assert (nargs == 2);

      ret = emit_library_call_value (func_sym, operands[0], LCT_NORMAL,
				     GET_MODE (operands[0]), 1,
				     arg[1], GET_MODE (arg[1]));

      if (ret != operands[0])
	emit_move_insn (operands[0], ret);
    }
}

/* Expand soft-float TFmode calls to sparc abi routines.  */

static void
emit_soft_tfmode_binop (enum rtx_code code, rtx *operands)
{
  const char *func;

  switch (code)
    {
    case PLUS:
      func = "_Qp_add";
      break;
    case MINUS:
      func = "_Qp_sub";
      break;
    case MULT:
      func = "_Qp_mul";
      break;
    case DIV:
      func = "_Qp_div";
      break;
    default:
      gcc_unreachable ();
    }

  emit_soft_tfmode_libcall (func, 3, operands);
}

static void
emit_soft_tfmode_unop (enum rtx_code code, rtx *operands)
{
  const char *func;

  gcc_assert (code == SQRT);
  func = "_Qp_sqrt";

  emit_soft_tfmode_libcall (func, 2, operands);
}

static void
emit_soft_tfmode_cvt (enum rtx_code code, rtx *operands)
{
  const char *func;

  switch (code)
    {
    case FLOAT_EXTEND:
      switch (GET_MODE (operands[1]))
	{
	case SFmode:
	  func = "_Qp_stoq";
	  break;
	case DFmode:
	  func = "_Qp_dtoq";
	  break;
	default:
	  gcc_unreachable ();
	}
      break;

    case FLOAT_TRUNCATE:
      switch (GET_MODE (operands[0]))
	{
	case SFmode:
	  func = "_Qp_qtos";
	  break;
	case DFmode:
	  func = "_Qp_qtod";
	  break;
	default:
	  gcc_unreachable ();
	}
      break;

    case FLOAT:
      switch (GET_MODE (operands[1]))
	{
	case SImode:
	  func = "_Qp_itoq";
	  break;
	case DImode:
	  func = "_Qp_xtoq";
	  break;
	default:
	  gcc_unreachable ();
	}
      break;

    case UNSIGNED_FLOAT:
      switch (GET_MODE (operands[1]))
	{
	case SImode:
	  func = "_Qp_uitoq";
	  break;
	case DImode:
	  func = "_Qp_uxtoq";
	  break;
	default:
	  gcc_unreachable ();
	}
      break;

    case FIX:
      switch (GET_MODE (operands[0]))
	{
	case SImode:
	  func = "_Qp_qtoi";
	  break;
	case DImode:
	  func = "_Qp_qtox";
	  break;
	default:
	  gcc_unreachable ();
	}
      break;

    case UNSIGNED_FIX:
      switch (GET_MODE (operands[0]))
	{
	case SImode:
	  func = "_Qp_qtoui";
	  break;
	case DImode:
	  func = "_Qp_qtoux";
	  break;
	default:
	  gcc_unreachable ();
	}
      break;

    default:
      gcc_unreachable ();
    }

  emit_soft_tfmode_libcall (func, 2, operands);
}

/* Expand a hard-float tfmode operation.  All arguments must be in
   registers.  */

static void
emit_hard_tfmode_operation (enum rtx_code code, rtx *operands)
{
  rtx op, dest;

  if (GET_RTX_CLASS (code) == RTX_UNARY)
    {
      operands[1] = force_reg (GET_MODE (operands[1]), operands[1]);
      op = gen_rtx_fmt_e (code, GET_MODE (operands[0]), operands[1]);
    }
  else
    {
      operands[1] = force_reg (GET_MODE (operands[1]), operands[1]);
      operands[2] = force_reg (GET_MODE (operands[2]), operands[2]);
      op = gen_rtx_fmt_ee (code, GET_MODE (operands[0]),
			   operands[1], operands[2]);
    }

  if (register_operand (operands[0], VOIDmode))
    dest = operands[0];
  else
    dest = gen_reg_rtx (GET_MODE (operands[0]));

  emit_insn (gen_rtx_SET (VOIDmode, dest, op));

  if (dest != operands[0])
    emit_move_insn (operands[0], dest);
}

void
emit_tfmode_binop (enum rtx_code code, rtx *operands)
{
  if (TARGET_HARD_QUAD)
    emit_hard_tfmode_operation (code, operands);
  else
    emit_soft_tfmode_binop (code, operands);
}

void
emit_tfmode_unop (enum rtx_code code, rtx *operands)
{
  if (TARGET_HARD_QUAD)
    emit_hard_tfmode_operation (code, operands);
  else
    emit_soft_tfmode_unop (code, operands);
}

void
emit_tfmode_cvt (enum rtx_code code, rtx *operands)
{
  if (TARGET_HARD_QUAD)
    emit_hard_tfmode_operation (code, operands);
  else
    emit_soft_tfmode_cvt (code, operands);
}

/* Return nonzero if a branch/jump/call instruction will be emitting
   nop into its delay slot.  */

int
empty_delay_slot (rtx insn)
{
  rtx seq;

  /* If no previous instruction (should not happen), return true.  */
  if (PREV_INSN (insn) == NULL)
    return 1;

  seq = NEXT_INSN (PREV_INSN (insn));
  if (GET_CODE (PATTERN (seq)) == SEQUENCE)
    return 0;

  return 1;
}

/* Return nonzero if TRIAL can go into the call delay slot.  */

int
tls_call_delay (rtx trial)
{
  rtx pat;

  /* Binutils allows
       call __tls_get_addr, %tgd_call (foo)
        add %l7, %o0, %o0, %tgd_add (foo)
     while Sun as/ld does not.  */
  if (TARGET_GNU_TLS || !TARGET_TLS)
    return 1;

  pat = PATTERN (trial);

  /* We must reject tgd_add{32|64}, i.e.
       (set (reg) (plus (reg) (unspec [(reg) (symbol_ref)] UNSPEC_TLSGD)))
     and tldm_add{32|64}, i.e.
       (set (reg) (plus (reg) (unspec [(reg) (symbol_ref)] UNSPEC_TLSLDM)))
     for Sun as/ld.  */
  if (GET_CODE (pat) == SET
      && GET_CODE (SET_SRC (pat)) == PLUS)
    {
      rtx unspec = XEXP (SET_SRC (pat), 1);

      if (GET_CODE (unspec) == UNSPEC
	  && (XINT (unspec, 1) == UNSPEC_TLSGD
	      || XINT (unspec, 1) == UNSPEC_TLSLDM))
	return 0;
    }

  return 1;
}

/* Return nonzero if TRIAL, an insn, can be combined with a 'restore'
   instruction.  RETURN_P is true if the v9 variant 'return' is to be
   considered in the test too.

   TRIAL must be a SET whose destination is a REG appropriate for the
   'restore' instruction or, if RETURN_P is true, for the 'return'
   instruction.  */

static int
eligible_for_restore_insn (rtx trial, bool return_p)
{
  rtx pat = PATTERN (trial);
  rtx src = SET_SRC (pat);

  /* The 'restore src,%g0,dest' pattern for word mode and below.  */
  if (GET_MODE_CLASS (GET_MODE (src)) != MODE_FLOAT
      && arith_operand (src, GET_MODE (src)))
    {
      if (TARGET_ARCH64)
        return GET_MODE_SIZE (GET_MODE (src)) <= GET_MODE_SIZE (DImode);
      else
        return GET_MODE_SIZE (GET_MODE (src)) <= GET_MODE_SIZE (SImode);
    }

  /* The 'restore src,%g0,dest' pattern for double-word mode.  */
  else if (GET_MODE_CLASS (GET_MODE (src)) != MODE_FLOAT
	   && arith_double_operand (src, GET_MODE (src)))
    return GET_MODE_SIZE (GET_MODE (src)) <= GET_MODE_SIZE (DImode);

  /* The 'restore src,%g0,dest' pattern for float if no FPU.  */
  else if (! TARGET_FPU && register_operand (src, SFmode))
    return 1;

  /* The 'restore src,%g0,dest' pattern for double if no FPU.  */
  else if (! TARGET_FPU && TARGET_ARCH64 && register_operand (src, DFmode))
    return 1;

  /* If we have the 'return' instruction, anything that does not use
     local or output registers and can go into a delay slot wins.  */
  else if (return_p && TARGET_V9 && ! epilogue_renumber (&pat, 1)
	   && (get_attr_in_uncond_branch_delay (trial)
	       == IN_UNCOND_BRANCH_DELAY_TRUE))
    return 1;

  /* The 'restore src1,src2,dest' pattern for SImode.  */
  else if (GET_CODE (src) == PLUS
	   && register_operand (XEXP (src, 0), SImode)
	   && arith_operand (XEXP (src, 1), SImode))
    return 1;

  /* The 'restore src1,src2,dest' pattern for DImode.  */
  else if (GET_CODE (src) == PLUS
	   && register_operand (XEXP (src, 0), DImode)
	   && arith_double_operand (XEXP (src, 1), DImode))
    return 1;

  /* The 'restore src1,%lo(src2),dest' pattern.  */
  else if (GET_CODE (src) == LO_SUM
	   && ! TARGET_CM_MEDMID
	   && ((register_operand (XEXP (src, 0), SImode)
	        && immediate_operand (XEXP (src, 1), SImode))
	       || (TARGET_ARCH64
		   && register_operand (XEXP (src, 0), DImode)
		   && immediate_operand (XEXP (src, 1), DImode))))
    return 1;

  /* The 'restore src,src,dest' pattern.  */
  else if (GET_CODE (src) == ASHIFT
	   && (register_operand (XEXP (src, 0), SImode)
	       || register_operand (XEXP (src, 0), DImode))
	   && XEXP (src, 1) == const1_rtx)
    return 1;

  return 0;
}

/* Return nonzero if TRIAL can go into the function return's
   delay slot.  */

int
eligible_for_return_delay (rtx trial)
{
  rtx pat;

  if (GET_CODE (trial) != INSN || GET_CODE (PATTERN (trial)) != SET)
    return 0;

  if (get_attr_length (trial) != 1)
    return 0;

  /* If there are any call-saved registers, we should scan TRIAL if it
     does not reference them.  For now just make it easy.  */
  if (num_gfregs)
    return 0;

  /* If the function uses __builtin_eh_return, the eh_return machinery
     occupies the delay slot.  */
  if (current_function_calls_eh_return)
    return 0;

  /* In the case of a true leaf function, anything can go into the slot.  */
  if (sparc_leaf_function_p)
    return get_attr_in_uncond_branch_delay (trial)
	   == IN_UNCOND_BRANCH_DELAY_TRUE;

  pat = PATTERN (trial);

  /* Otherwise, only operations which can be done in tandem with
     a `restore' or `return' insn can go into the delay slot.  */
  if (GET_CODE (SET_DEST (pat)) != REG
      || (REGNO (SET_DEST (pat)) >= 8 && REGNO (SET_DEST (pat)) < 24))
    return 0;

  /* If this instruction sets up floating point register and we have a return
     instruction, it can probably go in.  But restore will not work
     with FP_REGS.  */
  if (REGNO (SET_DEST (pat)) >= 32)
    return (TARGET_V9
	    && ! epilogue_renumber (&pat, 1)
	    && (get_attr_in_uncond_branch_delay (trial)
		== IN_UNCOND_BRANCH_DELAY_TRUE));

  return eligible_for_restore_insn (trial, true);
}

/* Return nonzero if TRIAL can go into the sibling call's
   delay slot.  */

int
eligible_for_sibcall_delay (rtx trial)
{
  rtx pat;

  if (GET_CODE (trial) != INSN || GET_CODE (PATTERN (trial)) != SET)
    return 0;

  if (get_attr_length (trial) != 1)
    return 0;

  pat = PATTERN (trial);

  if (sparc_leaf_function_p)
    {
      /* If the tail call is done using the call instruction,
	 we have to restore %o7 in the delay slot.  */
      if (LEAF_SIBCALL_SLOT_RESERVED_P)
	return 0;

      /* %g1 is used to build the function address */
      if (reg_mentioned_p (gen_rtx_REG (Pmode, 1), pat))
	return 0;

      return 1;
    }

  /* Otherwise, only operations which can be done in tandem with
     a `restore' insn can go into the delay slot.  */
  if (GET_CODE (SET_DEST (pat)) != REG
      || (REGNO (SET_DEST (pat)) >= 8 && REGNO (SET_DEST (pat)) < 24)
      || REGNO (SET_DEST (pat)) >= 32)
    return 0;

  /* If it mentions %o7, it can't go in, because sibcall will clobber it
     in most cases.  */
  if (reg_mentioned_p (gen_rtx_REG (Pmode, 15), pat))
    return 0;

  return eligible_for_restore_insn (trial, false);
}

int
short_branch (int uid1, int uid2)
{
  int delta = INSN_ADDRESSES (uid1) - INSN_ADDRESSES (uid2);

  /* Leave a few words of "slop".  */
  if (delta >= -1023 && delta <= 1022)
    return 1;

  return 0;
}

/* Return nonzero if REG is not used after INSN.
   We assume REG is a reload reg, and therefore does
   not live past labels or calls or jumps.  */
int
reg_unused_after (rtx reg, rtx insn)
{
  enum rtx_code code, prev_code = UNKNOWN;

  while ((insn = NEXT_INSN (insn)))
    {
      if (prev_code == CALL_INSN && call_used_regs[REGNO (reg)])
	return 1;

      code = GET_CODE (insn);
      if (GET_CODE (insn) == CODE_LABEL)
	return 1;

      if (INSN_P (insn))
	{
	  rtx set = single_set (insn);
	  int in_src = set && reg_overlap_mentioned_p (reg, SET_SRC (set));
	  if (set && in_src)
	    return 0;
	  if (set && reg_overlap_mentioned_p (reg, SET_DEST (set)))
	    return 1;
	  if (set == 0 && reg_overlap_mentioned_p (reg, PATTERN (insn)))
	    return 0;
	}
      prev_code = code;
    }
  return 1;
}

/* Determine if it's legal to put X into the constant pool.  This
   is not possible if X contains the address of a symbol that is
   not constant (TLS) or not known at final link time (PIC).  */

static bool
sparc_cannot_force_const_mem (rtx x)
{
  switch (GET_CODE (x))
    {
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
      /* Accept all non-symbolic constants.  */
      return false;

    case LABEL_REF:
      /* Labels are OK iff we are non-PIC.  */
      return flag_pic != 0;

    case SYMBOL_REF:
      /* 'Naked' TLS symbol references are never OK,
	 non-TLS symbols are OK iff we are non-PIC.  */
      if (SYMBOL_REF_TLS_MODEL (x))
	return true;
      else
	return flag_pic != 0;

    case CONST:
      return sparc_cannot_force_const_mem (XEXP (x, 0));
    case PLUS:
    case MINUS:
      return sparc_cannot_force_const_mem (XEXP (x, 0))
         || sparc_cannot_force_const_mem (XEXP (x, 1));
    case UNSPEC:
      return true;
    default:
      gcc_unreachable ();
    }
}

/* PIC support.  */
static GTY(()) char pic_helper_symbol_name[256];
static GTY(()) rtx pic_helper_symbol;
static GTY(()) bool pic_helper_emitted_p = false;
static GTY(()) rtx global_offset_table;

/* Ensure that we are not using patterns that are not OK with PIC.  */

int
check_pic (int i)
{
  switch (flag_pic)
    {
    case 1:
      gcc_assert (GET_CODE (recog_data.operand[i]) != SYMBOL_REF
	  	  && (GET_CODE (recog_data.operand[i]) != CONST
	          || (GET_CODE (XEXP (recog_data.operand[i], 0)) == MINUS
		      && (XEXP (XEXP (recog_data.operand[i], 0), 0)
			  == global_offset_table)
		      && (GET_CODE (XEXP (XEXP (recog_data.operand[i], 0), 1))
			  == CONST))));
    case 2:
    default:
      return 1;
    }
}

/* Return true if X is an address which needs a temporary register when 
   reloaded while generating PIC code.  */

int
pic_address_needs_scratch (rtx x)
{
  /* An address which is a symbolic plus a non SMALL_INT needs a temp reg.  */
  if (GET_CODE (x) == CONST && GET_CODE (XEXP (x, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (x, 0), 0)) == SYMBOL_REF
      && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
      && ! SMALL_INT (XEXP (XEXP (x, 0), 1)))
    return 1;

  return 0;
}

/* Determine if a given RTX is a valid constant.  We already know this
   satisfies CONSTANT_P.  */

bool
legitimate_constant_p (rtx x)
{
  rtx inner;

  switch (GET_CODE (x))
    {
    case SYMBOL_REF:
      /* TLS symbols are not constant.  */
      if (SYMBOL_REF_TLS_MODEL (x))
	return false;
      break;

    case CONST:
      inner = XEXP (x, 0);

      /* Offsets of TLS symbols are never valid.
	 Discourage CSE from creating them.  */
      if (GET_CODE (inner) == PLUS
	  && SPARC_SYMBOL_REF_TLS_P (XEXP (inner, 0)))
	return false;
      break;

    case CONST_DOUBLE:
      if (GET_MODE (x) == VOIDmode)
        return true;

      /* Floating point constants are generally not ok.
	 The only exception is 0.0 in VIS.  */
      if (TARGET_VIS
	  && SCALAR_FLOAT_MODE_P (GET_MODE (x))
	  && const_zero_operand (x, GET_MODE (x)))
	return true;

      return false;

    case CONST_VECTOR:
      /* Vector constants are generally not ok.
	 The only exception is 0 in VIS.  */
      if (TARGET_VIS
	  && const_zero_operand (x, GET_MODE (x)))
	return true;

      return false;

    default:
      break;
    }

  return true;
}

/* Determine if a given RTX is a valid constant address.  */

bool
constant_address_p (rtx x)
{
  switch (GET_CODE (x))
    {
    case LABEL_REF:
    case CONST_INT:
    case HIGH:
      return true;

    case CONST:
      if (flag_pic && pic_address_needs_scratch (x))
	return false;
      return legitimate_constant_p (x);

    case SYMBOL_REF:
      return !flag_pic && legitimate_constant_p (x);

    default:
      return false;
    }
}

/* Nonzero if the constant value X is a legitimate general operand
   when generating PIC code.  It is given that flag_pic is on and
   that X satisfies CONSTANT_P or is a CONST_DOUBLE.  */

bool
legitimate_pic_operand_p (rtx x)
{
  if (pic_address_needs_scratch (x))
    return false;
  if (SPARC_SYMBOL_REF_TLS_P (x)
      || (GET_CODE (x) == CONST
	  && GET_CODE (XEXP (x, 0)) == PLUS
	  && SPARC_SYMBOL_REF_TLS_P (XEXP (XEXP (x, 0), 0))))
    return false;
  return true;
}

/* Return nonzero if ADDR is a valid memory address.
   STRICT specifies whether strict register checking applies.  */
   
int
legitimate_address_p (enum machine_mode mode, rtx addr, int strict)
{
  rtx rs1 = NULL, rs2 = NULL, imm1 = NULL;

  if (REG_P (addr) || GET_CODE (addr) == SUBREG)
    rs1 = addr;
  else if (GET_CODE (addr) == PLUS)
    {
      rs1 = XEXP (addr, 0);
      rs2 = XEXP (addr, 1);

      /* Canonicalize.  REG comes first, if there are no regs,
	 LO_SUM comes first.  */
      if (!REG_P (rs1)
	  && GET_CODE (rs1) != SUBREG
	  && (REG_P (rs2)
	      || GET_CODE (rs2) == SUBREG
	      || (GET_CODE (rs2) == LO_SUM && GET_CODE (rs1) != LO_SUM)))
	{
	  rs1 = XEXP (addr, 1);
	  rs2 = XEXP (addr, 0);
	}

      if ((flag_pic == 1
	   && rs1 == pic_offset_table_rtx
	   && !REG_P (rs2)
	   && GET_CODE (rs2) != SUBREG
	   && GET_CODE (rs2) != LO_SUM
	   && GET_CODE (rs2) != MEM
	   && ! SPARC_SYMBOL_REF_TLS_P (rs2)
	   && (! symbolic_operand (rs2, VOIDmode) || mode == Pmode)
	   && (GET_CODE (rs2) != CONST_INT || SMALL_INT (rs2)))
	  || ((REG_P (rs1)
	       || GET_CODE (rs1) == SUBREG)
	      && RTX_OK_FOR_OFFSET_P (rs2)))
	{
	  imm1 = rs2;
	  rs2 = NULL;
	}
      else if ((REG_P (rs1) || GET_CODE (rs1) == SUBREG)
	       && (REG_P (rs2) || GET_CODE (rs2) == SUBREG))
	{
	  /* We prohibit REG + REG for TFmode when there are no quad move insns
	     and we consequently need to split.  We do this because REG+REG
	     is not an offsettable address.  If we get the situation in reload
	     where source and destination of a movtf pattern are both MEMs with
	     REG+REG address, then only one of them gets converted to an
	     offsettable address.  */
	  if (mode == TFmode
	      && ! (TARGET_FPU && TARGET_ARCH64 && TARGET_HARD_QUAD))
	    return 0;

	  /* We prohibit REG + REG on ARCH32 if not optimizing for
	     DFmode/DImode because then mem_min_alignment is likely to be zero
	     after reload and the  forced split would lack a matching splitter
	     pattern.  */
	  if (TARGET_ARCH32 && !optimize
	      && (mode == DFmode || mode == DImode))
	    return 0;
	}
      else if (USE_AS_OFFSETABLE_LO10
	       && GET_CODE (rs1) == LO_SUM
	       && TARGET_ARCH64
	       && ! TARGET_CM_MEDMID
	       && RTX_OK_FOR_OLO10_P (rs2))
	{
	  rs2 = NULL;
	  imm1 = XEXP (rs1, 1);
	  rs1 = XEXP (rs1, 0);
	  if (! CONSTANT_P (imm1) || SPARC_SYMBOL_REF_TLS_P (rs1))
	    return 0;
	}
    }
  else if (GET_CODE (addr) == LO_SUM)
    {
      rs1 = XEXP (addr, 0);
      imm1 = XEXP (addr, 1);

      if (! CONSTANT_P (imm1) || SPARC_SYMBOL_REF_TLS_P (rs1))
	return 0;

      /* We can't allow TFmode in 32-bit mode, because an offset greater
	 than the alignment (8) may cause the LO_SUM to overflow.  */
      if (mode == TFmode && TARGET_ARCH32)
	return 0;
    }
  else if (GET_CODE (addr) == CONST_INT && SMALL_INT (addr))
    return 1;
  else
    return 0;

  if (GET_CODE (rs1) == SUBREG)
    rs1 = SUBREG_REG (rs1);
  if (!REG_P (rs1))
    return 0;

  if (rs2)
    {
      if (GET_CODE (rs2) == SUBREG)
	rs2 = SUBREG_REG (rs2);
      if (!REG_P (rs2))
	return 0;
    }

  if (strict)
    {
      if (!REGNO_OK_FOR_BASE_P (REGNO (rs1))
	  || (rs2 && !REGNO_OK_FOR_BASE_P (REGNO (rs2))))
	return 0;
    }
  else
    {
      if ((REGNO (rs1) >= 32
	   && REGNO (rs1) != FRAME_POINTER_REGNUM
	   && REGNO (rs1) < FIRST_PSEUDO_REGISTER)
	  || (rs2
	      && (REGNO (rs2) >= 32
		  && REGNO (rs2) != FRAME_POINTER_REGNUM
		  && REGNO (rs2) < FIRST_PSEUDO_REGISTER)))
	return 0;
    }
  return 1;
}

/* Construct the SYMBOL_REF for the tls_get_offset function.  */

static GTY(()) rtx sparc_tls_symbol;

static rtx
sparc_tls_get_addr (void)
{
  if (!sparc_tls_symbol)
    sparc_tls_symbol = gen_rtx_SYMBOL_REF (Pmode, "__tls_get_addr");

  return sparc_tls_symbol;
}

static rtx
sparc_tls_got (void)
{
  rtx temp;
  if (flag_pic)
    {
      current_function_uses_pic_offset_table = 1;
      return pic_offset_table_rtx;
    }

  if (!global_offset_table)
    global_offset_table = gen_rtx_SYMBOL_REF (Pmode, "_GLOBAL_OFFSET_TABLE_");
  temp = gen_reg_rtx (Pmode);
  emit_move_insn (temp, global_offset_table);
  return temp;
}

/* Return 1 if *X is a thread-local symbol.  */

static int
sparc_tls_symbol_ref_1 (rtx *x, void *data ATTRIBUTE_UNUSED)
{
  return SPARC_SYMBOL_REF_TLS_P (*x);
}

/* Return 1 if X contains a thread-local symbol.  */

bool
sparc_tls_referenced_p (rtx x)
{
  if (!TARGET_HAVE_TLS)
    return false;

  return for_each_rtx (&x, &sparc_tls_symbol_ref_1, 0);
}

/* ADDR contains a thread-local SYMBOL_REF.  Generate code to compute
   this (thread-local) address.  */

rtx
legitimize_tls_address (rtx addr)
{
  rtx temp1, temp2, temp3, ret, o0, got, insn;

  gcc_assert (! no_new_pseudos);

  if (GET_CODE (addr) == SYMBOL_REF)
    switch (SYMBOL_REF_TLS_MODEL (addr))
      {
      case TLS_MODEL_GLOBAL_DYNAMIC:
	start_sequence ();
	temp1 = gen_reg_rtx (SImode);
	temp2 = gen_reg_rtx (SImode);
	ret = gen_reg_rtx (Pmode);
	o0 = gen_rtx_REG (Pmode, 8);
	got = sparc_tls_got ();
	emit_insn (gen_tgd_hi22 (temp1, addr));
	emit_insn (gen_tgd_lo10 (temp2, temp1, addr));
	if (TARGET_ARCH32)
	  {
	    emit_insn (gen_tgd_add32 (o0, got, temp2, addr));
	    insn = emit_call_insn (gen_tgd_call32 (o0, sparc_tls_get_addr (),
						   addr, const1_rtx));
	  }
	else
	  {
	    emit_insn (gen_tgd_add64 (o0, got, temp2, addr));
	    insn = emit_call_insn (gen_tgd_call64 (o0, sparc_tls_get_addr (),
						   addr, const1_rtx));
	  }
        CALL_INSN_FUNCTION_USAGE (insn)
	  = gen_rtx_EXPR_LIST (VOIDmode, gen_rtx_USE (VOIDmode, o0),
			       CALL_INSN_FUNCTION_USAGE (insn));
	insn = get_insns ();
	end_sequence ();
	emit_libcall_block (insn, ret, o0, addr);
	break;

      case TLS_MODEL_LOCAL_DYNAMIC:
	start_sequence ();
	temp1 = gen_reg_rtx (SImode);
	temp2 = gen_reg_rtx (SImode);
	temp3 = gen_reg_rtx (Pmode);
	ret = gen_reg_rtx (Pmode);
	o0 = gen_rtx_REG (Pmode, 8);
	got = sparc_tls_got ();
	emit_insn (gen_tldm_hi22 (temp1));
	emit_insn (gen_tldm_lo10 (temp2, temp1));
	if (TARGET_ARCH32)
	  {
	    emit_insn (gen_tldm_add32 (o0, got, temp2));
	    insn = emit_call_insn (gen_tldm_call32 (o0, sparc_tls_get_addr (),
						    const1_rtx));
	  }
	else
	  {
	    emit_insn (gen_tldm_add64 (o0, got, temp2));
	    insn = emit_call_insn (gen_tldm_call64 (o0, sparc_tls_get_addr (),
						    const1_rtx));
	  }
        CALL_INSN_FUNCTION_USAGE (insn)
	  = gen_rtx_EXPR_LIST (VOIDmode, gen_rtx_USE (VOIDmode, o0),
			       CALL_INSN_FUNCTION_USAGE (insn));
	insn = get_insns ();
	end_sequence ();
	emit_libcall_block (insn, temp3, o0,
			    gen_rtx_UNSPEC (Pmode, gen_rtvec (1, const0_rtx),
					    UNSPEC_TLSLD_BASE));
	temp1 = gen_reg_rtx (SImode);
	temp2 = gen_reg_rtx (SImode);
	emit_insn (gen_tldo_hix22 (temp1, addr));
	emit_insn (gen_tldo_lox10 (temp2, temp1, addr));
	if (TARGET_ARCH32)
	  emit_insn (gen_tldo_add32 (ret, temp3, temp2, addr));
	else
	  emit_insn (gen_tldo_add64 (ret, temp3, temp2, addr));
	break;

      case TLS_MODEL_INITIAL_EXEC:
	temp1 = gen_reg_rtx (SImode);
	temp2 = gen_reg_rtx (SImode);
	temp3 = gen_reg_rtx (Pmode);
	got = sparc_tls_got ();
	emit_insn (gen_tie_hi22 (temp1, addr));
	emit_insn (gen_tie_lo10 (temp2, temp1, addr));
	if (TARGET_ARCH32)
	  emit_insn (gen_tie_ld32 (temp3, got, temp2, addr));
	else
	  emit_insn (gen_tie_ld64 (temp3, got, temp2, addr));
        if (TARGET_SUN_TLS)
	  {
	    ret = gen_reg_rtx (Pmode);
	    if (TARGET_ARCH32)
	      emit_insn (gen_tie_add32 (ret, gen_rtx_REG (Pmode, 7),
					temp3, addr));
	    else
	      emit_insn (gen_tie_add64 (ret, gen_rtx_REG (Pmode, 7),
					temp3, addr));
	  }
	else
	  ret = gen_rtx_PLUS (Pmode, gen_rtx_REG (Pmode, 7), temp3);
	break;

      case TLS_MODEL_LOCAL_EXEC:
	temp1 = gen_reg_rtx (Pmode);
	temp2 = gen_reg_rtx (Pmode);
	if (TARGET_ARCH32)
	  {
	    emit_insn (gen_tle_hix22_sp32 (temp1, addr));
	    emit_insn (gen_tle_lox10_sp32 (temp2, temp1, addr));
	  }
	else
	  {
	    emit_insn (gen_tle_hix22_sp64 (temp1, addr));
	    emit_insn (gen_tle_lox10_sp64 (temp2, temp1, addr));
	  }
	ret = gen_rtx_PLUS (Pmode, gen_rtx_REG (Pmode, 7), temp2);
	break;

      default:
	gcc_unreachable ();
      }

  else
    gcc_unreachable ();  /* for now ... */

  return ret;
}


/* Legitimize PIC addresses.  If the address is already position-independent,
   we return ORIG.  Newly generated position-independent addresses go into a
   reg.  This is REG if nonzero, otherwise we allocate register(s) as
   necessary.  */

rtx
legitimize_pic_address (rtx orig, enum machine_mode mode ATTRIBUTE_UNUSED,
			rtx reg)
{
  if (GET_CODE (orig) == SYMBOL_REF)
    {
      rtx pic_ref, address;
      rtx insn;

      if (reg == 0)
	{
	  gcc_assert (! reload_in_progress && ! reload_completed);
	  reg = gen_reg_rtx (Pmode);
	}

      if (flag_pic == 2)
	{
	  /* If not during reload, allocate another temp reg here for loading
	     in the address, so that these instructions can be optimized
	     properly.  */
	  rtx temp_reg = ((reload_in_progress || reload_completed)
			  ? reg : gen_reg_rtx (Pmode));

	  /* Must put the SYMBOL_REF inside an UNSPEC here so that cse
	     won't get confused into thinking that these two instructions
	     are loading in the true address of the symbol.  If in the
	     future a PIC rtx exists, that should be used instead.  */
	  if (TARGET_ARCH64)
	    {
	      emit_insn (gen_movdi_high_pic (temp_reg, orig));
	      emit_insn (gen_movdi_lo_sum_pic (temp_reg, temp_reg, orig));
	    }
	  else
	    {
	      emit_insn (gen_movsi_high_pic (temp_reg, orig));
	      emit_insn (gen_movsi_lo_sum_pic (temp_reg, temp_reg, orig));
	    }
	  address = temp_reg;
	}
      else
	address = orig;

      pic_ref = gen_const_mem (Pmode,
			       gen_rtx_PLUS (Pmode,
					     pic_offset_table_rtx, address));
      current_function_uses_pic_offset_table = 1;
      insn = emit_move_insn (reg, pic_ref);
      /* Put a REG_EQUAL note on this insn, so that it can be optimized
	 by loop.  */
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_EQUAL, orig,
				  REG_NOTES (insn));
      return reg;
    }
  else if (GET_CODE (orig) == CONST)
    {
      rtx base, offset;

      if (GET_CODE (XEXP (orig, 0)) == PLUS
	  && XEXP (XEXP (orig, 0), 0) == pic_offset_table_rtx)
	return orig;

      if (reg == 0)
	{
	  gcc_assert (! reload_in_progress && ! reload_completed);
	  reg = gen_reg_rtx (Pmode);
	}

      gcc_assert (GET_CODE (XEXP (orig, 0)) == PLUS);
      base = legitimize_pic_address (XEXP (XEXP (orig, 0), 0), Pmode, reg);
      offset = legitimize_pic_address (XEXP (XEXP (orig, 0), 1), Pmode,
			 	       base == reg ? 0 : reg);

      if (GET_CODE (offset) == CONST_INT)
	{
	  if (SMALL_INT (offset))
	    return plus_constant (base, INTVAL (offset));
	  else if (! reload_in_progress && ! reload_completed)
	    offset = force_reg (Pmode, offset);
	  else
	    /* If we reach here, then something is seriously wrong.  */
	    gcc_unreachable ();
	}
      return gen_rtx_PLUS (Pmode, base, offset);
    }
  else if (GET_CODE (orig) == LABEL_REF)
    /* ??? Why do we do this?  */
    /* Now movsi_pic_label_ref uses it, but we ought to be checking that
       the register is live instead, in case it is eliminated.  */
    current_function_uses_pic_offset_table = 1;

  return orig;
}

/* Try machine-dependent ways of modifying an illegitimate address X
   to be legitimate.  If we find one, return the new, valid address.

   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.

   MODE is the mode of the operand pointed to by X.  */

rtx
legitimize_address (rtx x, rtx oldx ATTRIBUTE_UNUSED, enum machine_mode mode)
{
  rtx orig_x = x;

  if (GET_CODE (x) == PLUS && GET_CODE (XEXP (x, 0)) == MULT)
    x = gen_rtx_PLUS (Pmode, XEXP (x, 1),
		      force_operand (XEXP (x, 0), NULL_RTX));
  if (GET_CODE (x) == PLUS && GET_CODE (XEXP (x, 1)) == MULT)
    x = gen_rtx_PLUS (Pmode, XEXP (x, 0),
		      force_operand (XEXP (x, 1), NULL_RTX));
  if (GET_CODE (x) == PLUS && GET_CODE (XEXP (x, 0)) == PLUS)
    x = gen_rtx_PLUS (Pmode, force_operand (XEXP (x, 0), NULL_RTX),
		      XEXP (x, 1));
  if (GET_CODE (x) == PLUS && GET_CODE (XEXP (x, 1)) == PLUS)
    x = gen_rtx_PLUS (Pmode, XEXP (x, 0),
		      force_operand (XEXP (x, 1), NULL_RTX));

  if (x != orig_x && legitimate_address_p (mode, x, FALSE))
    return x;

  if (SPARC_SYMBOL_REF_TLS_P (x))
    x = legitimize_tls_address (x);
  else if (flag_pic)
    x = legitimize_pic_address (x, mode, 0);
  else if (GET_CODE (x) == PLUS && CONSTANT_ADDRESS_P (XEXP (x, 1)))
    x = gen_rtx_PLUS (Pmode, XEXP (x, 0),
		      copy_to_mode_reg (Pmode, XEXP (x, 1)));
  else if (GET_CODE (x) == PLUS && CONSTANT_ADDRESS_P (XEXP (x, 0)))
    x = gen_rtx_PLUS (Pmode, XEXP (x, 1),
		      copy_to_mode_reg (Pmode, XEXP (x, 0)));
  else if (GET_CODE (x) == SYMBOL_REF
	   || GET_CODE (x) == CONST
           || GET_CODE (x) == LABEL_REF)
    x = copy_to_suggested_reg (x, NULL_RTX, Pmode);
  return x;
}

/* Emit the special PIC helper function.  */

static void
emit_pic_helper (void)
{
  const char *pic_name = reg_names[REGNO (pic_offset_table_rtx)];
  int align;

  switch_to_section (text_section);

  align = floor_log2 (FUNCTION_BOUNDARY / BITS_PER_UNIT);
  if (align > 0)
    ASM_OUTPUT_ALIGN (asm_out_file, align);
  ASM_OUTPUT_LABEL (asm_out_file, pic_helper_symbol_name);
  if (flag_delayed_branch)
    fprintf (asm_out_file, "\tjmp\t%%o7+8\n\t add\t%%o7, %s, %s\n",
	    pic_name, pic_name);
  else
    fprintf (asm_out_file, "\tadd\t%%o7, %s, %s\n\tjmp\t%%o7+8\n\t nop\n",
	    pic_name, pic_name);

  pic_helper_emitted_p = true;
}

/* Emit code to load the PIC register.  */

static void
load_pic_register (bool delay_pic_helper)
{
  int orig_flag_pic = flag_pic;

  /* If we haven't initialized the special PIC symbols, do so now.  */
  if (!pic_helper_symbol_name[0])
    {
      ASM_GENERATE_INTERNAL_LABEL (pic_helper_symbol_name, "LADDPC", 0);
      pic_helper_symbol = gen_rtx_SYMBOL_REF (Pmode, pic_helper_symbol_name);
      global_offset_table = gen_rtx_SYMBOL_REF (Pmode, "_GLOBAL_OFFSET_TABLE_");
    }

  /* If we haven't emitted the special PIC helper function, do so now unless
     we are requested to delay it.  */
  if (!delay_pic_helper && !pic_helper_emitted_p)
    emit_pic_helper ();

  flag_pic = 0;
  if (TARGET_ARCH64)
    emit_insn (gen_load_pcrel_symdi (pic_offset_table_rtx, global_offset_table,
				     pic_helper_symbol));
  else
    emit_insn (gen_load_pcrel_symsi (pic_offset_table_rtx, global_offset_table,
				     pic_helper_symbol));
  flag_pic = orig_flag_pic;

  /* Need to emit this whether or not we obey regdecls,
     since setjmp/longjmp can cause life info to screw up.
     ??? In the case where we don't obey regdecls, this is not sufficient
     since we may not fall out the bottom.  */
  emit_insn (gen_rtx_USE (VOIDmode, pic_offset_table_rtx));
}

/* Return 1 if RTX is a MEM which is known to be aligned to at
   least a DESIRED byte boundary.  */

int
mem_min_alignment (rtx mem, int desired)
{
  rtx addr, base, offset;

  /* If it's not a MEM we can't accept it.  */
  if (GET_CODE (mem) != MEM)
    return 0;

  /* Obviously...  */
  if (!TARGET_UNALIGNED_DOUBLES
      && MEM_ALIGN (mem) / BITS_PER_UNIT >= (unsigned)desired)
    return 1;

  /* ??? The rest of the function predates MEM_ALIGN so
     there is probably a bit of redundancy.  */
  addr = XEXP (mem, 0);
  base = offset = NULL_RTX;
  if (GET_CODE (addr) == PLUS)
    {
      if (GET_CODE (XEXP (addr, 0)) == REG)
	{
	  base = XEXP (addr, 0);

	  /* What we are saying here is that if the base
	     REG is aligned properly, the compiler will make
	     sure any REG based index upon it will be so
	     as well.  */
	  if (GET_CODE (XEXP (addr, 1)) == CONST_INT)
	    offset = XEXP (addr, 1);
	  else
	    offset = const0_rtx;
	}
    }
  else if (GET_CODE (addr) == REG)
    {
      base = addr;
      offset = const0_rtx;
    }

  if (base != NULL_RTX)
    {
      int regno = REGNO (base);

      if (regno != HARD_FRAME_POINTER_REGNUM && regno != STACK_POINTER_REGNUM)
	{
	  /* Check if the compiler has recorded some information
	     about the alignment of the base REG.  If reload has
	     completed, we already matched with proper alignments.
	     If not running global_alloc, reload might give us
	     unaligned pointer to local stack though.  */
	  if (((cfun != 0
		&& REGNO_POINTER_ALIGN (regno) >= desired * BITS_PER_UNIT)
	       || (optimize && reload_completed))
	      && (INTVAL (offset) & (desired - 1)) == 0)
	    return 1;
	}
      else
	{
	  if (((INTVAL (offset) - SPARC_STACK_BIAS) & (desired - 1)) == 0)
	    return 1;
	}
    }
  else if (! TARGET_UNALIGNED_DOUBLES
	   || CONSTANT_P (addr)
	   || GET_CODE (addr) == LO_SUM)
    {
      /* Anything else we know is properly aligned unless TARGET_UNALIGNED_DOUBLES
	 is true, in which case we can only assume that an access is aligned if
	 it is to a constant address, or the address involves a LO_SUM.  */
      return 1;
    }
  
  /* An obviously unaligned address.  */
  return 0;
}


/* Vectors to keep interesting information about registers where it can easily
   be got.  We used to use the actual mode value as the bit number, but there
   are more than 32 modes now.  Instead we use two tables: one indexed by
   hard register number, and one indexed by mode.  */

/* The purpose of sparc_mode_class is to shrink the range of modes so that
   they all fit (as bit numbers) in a 32 bit word (again).  Each real mode is
   mapped into one sparc_mode_class mode.  */

enum sparc_mode_class {
  S_MODE, D_MODE, T_MODE, O_MODE,
  SF_MODE, DF_MODE, TF_MODE, OF_MODE,
  CC_MODE, CCFP_MODE
};

/* Modes for single-word and smaller quantities.  */
#define S_MODES ((1 << (int) S_MODE) | (1 << (int) SF_MODE))

/* Modes for double-word and smaller quantities.  */
#define D_MODES (S_MODES | (1 << (int) D_MODE) | (1 << DF_MODE))

/* Modes for quad-word and smaller quantities.  */
#define T_MODES (D_MODES | (1 << (int) T_MODE) | (1 << (int) TF_MODE))

/* Modes for 8-word and smaller quantities.  */
#define O_MODES (T_MODES | (1 << (int) O_MODE) | (1 << (int) OF_MODE))

/* Modes for single-float quantities.  We must allow any single word or
   smaller quantity.  This is because the fix/float conversion instructions
   take integer inputs/outputs from the float registers.  */
#define SF_MODES (S_MODES)

/* Modes for double-float and smaller quantities.  */
#define DF_MODES (S_MODES | D_MODES)

/* Modes for double-float only quantities.  */
#define DF_MODES_NO_S ((1 << (int) D_MODE) | (1 << (int) DF_MODE))

/* Modes for quad-float only quantities.  */
#define TF_ONLY_MODES (1 << (int) TF_MODE)

/* Modes for quad-float and smaller quantities.  */
#define TF_MODES (DF_MODES | TF_ONLY_MODES)

/* Modes for quad-float and double-float quantities.  */
#define TF_MODES_NO_S (DF_MODES_NO_S | TF_ONLY_MODES)

/* Modes for quad-float pair only quantities.  */
#define OF_ONLY_MODES (1 << (int) OF_MODE)

/* Modes for quad-float pairs and smaller quantities.  */
#define OF_MODES (TF_MODES | OF_ONLY_MODES)

#define OF_MODES_NO_S (TF_MODES_NO_S | OF_ONLY_MODES)

/* Modes for condition codes.  */
#define CC_MODES (1 << (int) CC_MODE)
#define CCFP_MODES (1 << (int) CCFP_MODE)

/* Value is 1 if register/mode pair is acceptable on sparc.
   The funny mixture of D and T modes is because integer operations
   do not specially operate on tetra quantities, so non-quad-aligned
   registers can hold quadword quantities (except %o4 and %i4 because
   they cross fixed registers).  */

/* This points to either the 32 bit or the 64 bit version.  */
const int *hard_regno_mode_classes;

static const int hard_32bit_mode_classes[] = {
  S_MODES, S_MODES, T_MODES, S_MODES, T_MODES, S_MODES, D_MODES, S_MODES,
  T_MODES, S_MODES, T_MODES, S_MODES, D_MODES, S_MODES, D_MODES, S_MODES,
  T_MODES, S_MODES, T_MODES, S_MODES, T_MODES, S_MODES, D_MODES, S_MODES,
  T_MODES, S_MODES, T_MODES, S_MODES, D_MODES, S_MODES, D_MODES, S_MODES,

  OF_MODES, SF_MODES, DF_MODES, SF_MODES, OF_MODES, SF_MODES, DF_MODES, SF_MODES,
  OF_MODES, SF_MODES, DF_MODES, SF_MODES, OF_MODES, SF_MODES, DF_MODES, SF_MODES,
  OF_MODES, SF_MODES, DF_MODES, SF_MODES, OF_MODES, SF_MODES, DF_MODES, SF_MODES,
  OF_MODES, SF_MODES, DF_MODES, SF_MODES, TF_MODES, SF_MODES, DF_MODES, SF_MODES,

  /* FP regs f32 to f63.  Only the even numbered registers actually exist,
     and none can hold SFmode/SImode values.  */
  OF_MODES_NO_S, 0, DF_MODES_NO_S, 0, OF_MODES_NO_S, 0, DF_MODES_NO_S, 0,
  OF_MODES_NO_S, 0, DF_MODES_NO_S, 0, OF_MODES_NO_S, 0, DF_MODES_NO_S, 0,
  OF_MODES_NO_S, 0, DF_MODES_NO_S, 0, OF_MODES_NO_S, 0, DF_MODES_NO_S, 0,
  OF_MODES_NO_S, 0, DF_MODES_NO_S, 0, TF_MODES_NO_S, 0, DF_MODES_NO_S, 0,

  /* %fcc[0123] */
  CCFP_MODES, CCFP_MODES, CCFP_MODES, CCFP_MODES,

  /* %icc */
  CC_MODES
};

static const int hard_64bit_mode_classes[] = {
  D_MODES, D_MODES, T_MODES, D_MODES, T_MODES, D_MODES, T_MODES, D_MODES,
  O_MODES, D_MODES, T_MODES, D_MODES, T_MODES, D_MODES, T_MODES, D_MODES,
  T_MODES, D_MODES, T_MODES, D_MODES, T_MODES, D_MODES, T_MODES, D_MODES,
  O_MODES, D_MODES, T_MODES, D_MODES, T_MODES, D_MODES, T_MODES, D_MODES,

  OF_MODES, SF_MODES, DF_MODES, SF_MODES, OF_MODES, SF_MODES, DF_MODES, SF_MODES,
  OF_MODES, SF_MODES, DF_MODES, SF_MODES, OF_MODES, SF_MODES, DF_MODES, SF_MODES,
  OF_MODES, SF_MODES, DF_MODES, SF_MODES, OF_MODES, SF_MODES, DF_MODES, SF_MODES,
  OF_MODES, SF_MODES, DF_MODES, SF_MODES, TF_MODES, SF_MODES, DF_MODES, SF_MODES,

  /* FP regs f32 to f63.  Only the even numbered registers actually exist,
     and none can hold SFmode/SImode values.  */
  OF_MODES_NO_S, 0, DF_MODES_NO_S, 0, OF_MODES_NO_S, 0, DF_MODES_NO_S, 0,
  OF_MODES_NO_S, 0, DF_MODES_NO_S, 0, OF_MODES_NO_S, 0, DF_MODES_NO_S, 0,
  OF_MODES_NO_S, 0, DF_MODES_NO_S, 0, OF_MODES_NO_S, 0, DF_MODES_NO_S, 0,
  OF_MODES_NO_S, 0, DF_MODES_NO_S, 0, TF_MODES_NO_S, 0, DF_MODES_NO_S, 0,

  /* %fcc[0123] */
  CCFP_MODES, CCFP_MODES, CCFP_MODES, CCFP_MODES,

  /* %icc */
  CC_MODES
};

int sparc_mode_class [NUM_MACHINE_MODES];

enum reg_class sparc_regno_reg_class[FIRST_PSEUDO_REGISTER];

static void
sparc_init_modes (void)
{
  int i;

  for (i = 0; i < NUM_MACHINE_MODES; i++)
    {
      switch (GET_MODE_CLASS (i))
	{
	case MODE_INT:
	case MODE_PARTIAL_INT:
	case MODE_COMPLEX_INT:
	  if (GET_MODE_SIZE (i) <= 4)
	    sparc_mode_class[i] = 1 << (int) S_MODE;
	  else if (GET_MODE_SIZE (i) == 8)
	    sparc_mode_class[i] = 1 << (int) D_MODE;
	  else if (GET_MODE_SIZE (i) == 16)
	    sparc_mode_class[i] = 1 << (int) T_MODE;
	  else if (GET_MODE_SIZE (i) == 32)
	    sparc_mode_class[i] = 1 << (int) O_MODE;
	  else 
	    sparc_mode_class[i] = 0;
	  break;
	case MODE_VECTOR_INT:
	  if (GET_MODE_SIZE (i) <= 4)
	    sparc_mode_class[i] = 1 << (int)SF_MODE;
	  else if (GET_MODE_SIZE (i) == 8)
	    sparc_mode_class[i] = 1 << (int)DF_MODE;
	  break;
	case MODE_FLOAT:
	case MODE_COMPLEX_FLOAT:
	  if (GET_MODE_SIZE (i) <= 4)
	    sparc_mode_class[i] = 1 << (int) SF_MODE;
	  else if (GET_MODE_SIZE (i) == 8)
	    sparc_mode_class[i] = 1 << (int) DF_MODE;
	  else if (GET_MODE_SIZE (i) == 16)
	    sparc_mode_class[i] = 1 << (int) TF_MODE;
	  else if (GET_MODE_SIZE (i) == 32)
	    sparc_mode_class[i] = 1 << (int) OF_MODE;
	  else 
	    sparc_mode_class[i] = 0;
	  break;
	case MODE_CC:
	  if (i == (int) CCFPmode || i == (int) CCFPEmode)
	    sparc_mode_class[i] = 1 << (int) CCFP_MODE;
	  else
	    sparc_mode_class[i] = 1 << (int) CC_MODE;
	  break;
	default:
	  sparc_mode_class[i] = 0;
	  break;
	}
    }

  if (TARGET_ARCH64)
    hard_regno_mode_classes = hard_64bit_mode_classes;
  else
    hard_regno_mode_classes = hard_32bit_mode_classes;

  /* Initialize the array used by REGNO_REG_CLASS.  */
  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
      if (i < 16 && TARGET_V8PLUS)
	sparc_regno_reg_class[i] = I64_REGS;
      else if (i < 32 || i == FRAME_POINTER_REGNUM)
	sparc_regno_reg_class[i] = GENERAL_REGS;
      else if (i < 64)
	sparc_regno_reg_class[i] = FP_REGS;
      else if (i < 96)
	sparc_regno_reg_class[i] = EXTRA_FP_REGS;
      else if (i < 100)
	sparc_regno_reg_class[i] = FPCC_REGS;
      else
	sparc_regno_reg_class[i] = NO_REGS;
    }
}

/* Compute the frame size required by the function.  This function is called
   during the reload pass and also by sparc_expand_prologue.  */

HOST_WIDE_INT
sparc_compute_frame_size (HOST_WIDE_INT size, int leaf_function_p)
{
  int outgoing_args_size = (current_function_outgoing_args_size
			    + REG_PARM_STACK_SPACE (current_function_decl));
  int n_regs = 0;  /* N_REGS is the number of 4-byte regs saved thus far.  */
  int i;

  if (TARGET_ARCH64)
    {
      for (i = 0; i < 8; i++)
	if (regs_ever_live[i] && ! call_used_regs[i])
	  n_regs += 2;
    }
  else
    {
      for (i = 0; i < 8; i += 2)
	if ((regs_ever_live[i] && ! call_used_regs[i])
	    || (regs_ever_live[i+1] && ! call_used_regs[i+1]))
	  n_regs += 2;
    }

  for (i = 32; i < (TARGET_V9 ? 96 : 64); i += 2)
    if ((regs_ever_live[i] && ! call_used_regs[i])
	|| (regs_ever_live[i+1] && ! call_used_regs[i+1]))
      n_regs += 2;

  /* Set up values for use in prologue and epilogue.  */
  num_gfregs = n_regs;

  if (leaf_function_p
      && n_regs == 0
      && size == 0
      && current_function_outgoing_args_size == 0)
    actual_fsize = apparent_fsize = 0;
  else
    {
      /* We subtract STARTING_FRAME_OFFSET, remember it's negative.  */
      apparent_fsize = (size - STARTING_FRAME_OFFSET + 7) & -8;
      apparent_fsize += n_regs * 4;
      actual_fsize = apparent_fsize + ((outgoing_args_size + 7) & -8);
    }

  /* Make sure nothing can clobber our register windows.
     If a SAVE must be done, or there is a stack-local variable,
     the register window area must be allocated.  */
  if (! leaf_function_p || size > 0)
    actual_fsize += FIRST_PARM_OFFSET (current_function_decl);

  return SPARC_STACK_ALIGN (actual_fsize);
}

/* Output any necessary .register pseudo-ops.  */

void
sparc_output_scratch_registers (FILE *file ATTRIBUTE_UNUSED)
{
#ifdef HAVE_AS_REGISTER_PSEUDO_OP
  int i;

  if (TARGET_ARCH32)
    return;

  /* Check if %g[2367] were used without
     .register being printed for them already.  */
  for (i = 2; i < 8; i++)
    {
      if (regs_ever_live [i]
	  && ! sparc_hard_reg_printed [i])
	{
	  sparc_hard_reg_printed [i] = 1;
	  /* %g7 is used as TLS base register, use #ignore
	     for it instead of #scratch.  */
	  fprintf (file, "\t.register\t%%g%d, #%s\n", i,
		   i == 7 ? "ignore" : "scratch");
	}
      if (i == 3) i = 5;
    }
#endif
}

/* Save/restore call-saved registers from LOW to HIGH at BASE+OFFSET
   as needed.  LOW should be double-word aligned for 32-bit registers.
   Return the new OFFSET.  */

#define SORR_SAVE    0
#define SORR_RESTORE 1

static int
save_or_restore_regs (int low, int high, rtx base, int offset, int action)
{
  rtx mem, insn;
  int i;

  if (TARGET_ARCH64 && high <= 32)
    {
      for (i = low; i < high; i++)
	{
	  if (regs_ever_live[i] && ! call_used_regs[i])
	    {
	      mem = gen_rtx_MEM (DImode, plus_constant (base, offset));
	      set_mem_alias_set (mem, sparc_sr_alias_set);
	      if (action == SORR_SAVE)
		{
		  insn = emit_move_insn (mem, gen_rtx_REG (DImode, i));
		  RTX_FRAME_RELATED_P (insn) = 1;
		}
	      else  /* action == SORR_RESTORE */
		emit_move_insn (gen_rtx_REG (DImode, i), mem);
	      offset += 8;
	    }
	}
    }
  else
    {
      for (i = low; i < high; i += 2)
	{
	  bool reg0 = regs_ever_live[i] && ! call_used_regs[i];
	  bool reg1 = regs_ever_live[i+1] && ! call_used_regs[i+1];
	  enum machine_mode mode;
	  int regno;

	  if (reg0 && reg1)
	    {
	      mode = i < 32 ? DImode : DFmode;
	      regno = i;
	    }
	  else if (reg0)
	    {
	      mode = i < 32 ? SImode : SFmode;
	      regno = i;
	    }
	  else if (reg1)
	    {
	      mode = i < 32 ? SImode : SFmode;
	      regno = i + 1;
	      offset += 4;
	    }
	  else
	    continue;

	  mem = gen_rtx_MEM (mode, plus_constant (base, offset));
	  set_mem_alias_set (mem, sparc_sr_alias_set);
	  if (action == SORR_SAVE)
	    {
	      insn = emit_move_insn (mem, gen_rtx_REG (mode, regno));
	      RTX_FRAME_RELATED_P (insn) = 1;
	    }
	  else  /* action == SORR_RESTORE */
	    emit_move_insn (gen_rtx_REG (mode, regno), mem);

	  /* Always preserve double-word alignment.  */
	  offset = (offset + 7) & -8;
	}
    }

  return offset;
}

/* Emit code to save call-saved registers.  */

static void
emit_save_or_restore_regs (int action)
{
  HOST_WIDE_INT offset;
  rtx base;

  offset = frame_base_offset - apparent_fsize;

  if (offset < -4096 || offset + num_gfregs * 4 > 4095)
    {
      /* ??? This might be optimized a little as %g1 might already have a
	 value close enough that a single add insn will do.  */
      /* ??? Although, all of this is probably only a temporary fix
	 because if %g1 can hold a function result, then
	 sparc_expand_epilogue will lose (the result will be
	 clobbered).  */
      base = gen_rtx_REG (Pmode, 1);
      emit_move_insn (base, GEN_INT (offset));
      emit_insn (gen_rtx_SET (VOIDmode,
			      base,
			      gen_rtx_PLUS (Pmode, frame_base_reg, base)));
      offset = 0;
    }
  else
    base = frame_base_reg;

  offset = save_or_restore_regs (0, 8, base, offset, action);
  save_or_restore_regs (32, TARGET_V9 ? 96 : 64, base, offset, action);
}

/* Generate a save_register_window insn.  */

static rtx
gen_save_register_window (rtx increment)
{
  if (TARGET_ARCH64)
    return gen_save_register_windowdi (increment);
  else
    return gen_save_register_windowsi (increment);
}

/* Generate an increment for the stack pointer.  */

static rtx
gen_stack_pointer_inc (rtx increment)
{
  return gen_rtx_SET (VOIDmode,
		      stack_pointer_rtx,
		      gen_rtx_PLUS (Pmode,
				    stack_pointer_rtx,
				    increment));
}

/* Generate a decrement for the stack pointer.  */

static rtx
gen_stack_pointer_dec (rtx decrement)
{
  return gen_rtx_SET (VOIDmode,
		      stack_pointer_rtx,
		      gen_rtx_MINUS (Pmode,
				     stack_pointer_rtx,
				     decrement));
}

/* Expand the function prologue.  The prologue is responsible for reserving
   storage for the frame, saving the call-saved registers and loading the
   PIC register if needed.  */

void
sparc_expand_prologue (void)
{
  rtx insn;
  int i;

  /* Compute a snapshot of current_function_uses_only_leaf_regs.  Relying
     on the final value of the flag means deferring the prologue/epilogue
     expansion until just before the second scheduling pass, which is too
     late to emit multiple epilogues or return insns.

     Of course we are making the assumption that the value of the flag
     will not change between now and its final value.  Of the three parts
     of the formula, only the last one can reasonably vary.  Let's take a
     closer look, after assuming that the first two ones are set to true
     (otherwise the last value is effectively silenced).

     If only_leaf_regs_used returns false, the global predicate will also
     be false so the actual frame size calculated below will be positive.
     As a consequence, the save_register_window insn will be emitted in
     the instruction stream; now this insn explicitly references %fp
     which is not a leaf register so only_leaf_regs_used will always
     return false subsequently.

     If only_leaf_regs_used returns true, we hope that the subsequent
     optimization passes won't cause non-leaf registers to pop up.  For
     example, the regrename pass has special provisions to not rename to
     non-leaf registers in a leaf function.  */
  sparc_leaf_function_p
    = optimize > 0 && leaf_function_p () && only_leaf_regs_used ();

  /* Need to use actual_fsize, since we are also allocating
     space for our callee (and our own register save area).  */
  actual_fsize
    = sparc_compute_frame_size (get_frame_size(), sparc_leaf_function_p);

  /* Advertise that the data calculated just above are now valid.  */
  sparc_prologue_data_valid_p = true;

  if (sparc_leaf_function_p)
    {
      frame_base_reg = stack_pointer_rtx;
      frame_base_offset = actual_fsize + SPARC_STACK_BIAS;
    }
  else
    {
      frame_base_reg = hard_frame_pointer_rtx;
      frame_base_offset = SPARC_STACK_BIAS;
    }

  if (actual_fsize == 0)
    /* do nothing.  */ ;
  else if (sparc_leaf_function_p)
    {
      if (actual_fsize <= 4096)
	insn = emit_insn (gen_stack_pointer_inc (GEN_INT (-actual_fsize)));
      else if (actual_fsize <= 8192)
	{
	  insn = emit_insn (gen_stack_pointer_inc (GEN_INT (-4096)));
	  /* %sp is still the CFA register.  */
	  RTX_FRAME_RELATED_P (insn) = 1;
	  insn
	    = emit_insn (gen_stack_pointer_inc (GEN_INT (4096-actual_fsize)));
	}
      else
	{
	  rtx reg = gen_rtx_REG (Pmode, 1);
	  emit_move_insn (reg, GEN_INT (-actual_fsize));
	  insn = emit_insn (gen_stack_pointer_inc (reg));
	  REG_NOTES (insn) =
	    gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
			       gen_stack_pointer_inc (GEN_INT (-actual_fsize)),
			       REG_NOTES (insn));
	}

      RTX_FRAME_RELATED_P (insn) = 1;
    }
  else
    {
      if (actual_fsize <= 4096)
	insn = emit_insn (gen_save_register_window (GEN_INT (-actual_fsize)));
      else if (actual_fsize <= 8192)
	{
	  insn = emit_insn (gen_save_register_window (GEN_INT (-4096)));
	  /* %sp is not the CFA register anymore.  */
	  emit_insn (gen_stack_pointer_inc (GEN_INT (4096-actual_fsize)));
	}
      else
	{
	  rtx reg = gen_rtx_REG (Pmode, 1);
	  emit_move_insn (reg, GEN_INT (-actual_fsize));
	  insn = emit_insn (gen_save_register_window (reg));
	}

      RTX_FRAME_RELATED_P (insn) = 1;
      for (i=0; i < XVECLEN (PATTERN (insn), 0); i++)
        RTX_FRAME_RELATED_P (XVECEXP (PATTERN (insn), 0, i)) = 1;
    }

  if (num_gfregs)
    emit_save_or_restore_regs (SORR_SAVE);

  /* Load the PIC register if needed.  */
  if (flag_pic && current_function_uses_pic_offset_table)
    load_pic_register (false);
}
 
/* This function generates the assembly code for function entry, which boils
   down to emitting the necessary .register directives.  */

static void
sparc_asm_function_prologue (FILE *file, HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  /* Check that the assumption we made in sparc_expand_prologue is valid.  */
  gcc_assert (sparc_leaf_function_p == current_function_uses_only_leaf_regs);

  sparc_output_scratch_registers (file);
}

/* Expand the function epilogue, either normal or part of a sibcall.
   We emit all the instructions except the return or the call.  */

void
sparc_expand_epilogue (void)
{
  if (num_gfregs)
    emit_save_or_restore_regs (SORR_RESTORE);

  if (actual_fsize == 0)
    /* do nothing.  */ ;
  else if (sparc_leaf_function_p)
    {
      if (actual_fsize <= 4096)
	emit_insn (gen_stack_pointer_dec (GEN_INT (- actual_fsize)));
      else if (actual_fsize <= 8192)
	{
	  emit_insn (gen_stack_pointer_dec (GEN_INT (-4096)));
	  emit_insn (gen_stack_pointer_dec (GEN_INT (4096 - actual_fsize)));
	}
      else
	{
	  rtx reg = gen_rtx_REG (Pmode, 1);
	  emit_move_insn (reg, GEN_INT (-actual_fsize));
	  emit_insn (gen_stack_pointer_dec (reg));
	}
    }
}

/* Return true if it is appropriate to emit `return' instructions in the
   body of a function.  */

bool
sparc_can_use_return_insn_p (void)
{
  return sparc_prologue_data_valid_p
	 && (actual_fsize == 0 || !sparc_leaf_function_p);
}
  
/* This function generates the assembly code for function exit.  */
  
static void
sparc_asm_function_epilogue (FILE *file, HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  /* If code does not drop into the epilogue, we have to still output
     a dummy nop for the sake of sane backtraces.  Otherwise, if the
     last two instructions of a function were "call foo; dslot;" this
     can make the return PC of foo (i.e. address of call instruction
     plus 8) point to the first instruction in the next function.  */

  rtx insn, last_real_insn;

  insn = get_last_insn ();

  last_real_insn = prev_real_insn (insn);
  if (last_real_insn
      && GET_CODE (last_real_insn) == INSN
      && GET_CODE (PATTERN (last_real_insn)) == SEQUENCE)
    last_real_insn = XVECEXP (PATTERN (last_real_insn), 0, 0);

  if (last_real_insn && GET_CODE (last_real_insn) == CALL_INSN)
    fputs("\tnop\n", file);

  sparc_output_deferred_case_vectors ();
}
  
/* Output a 'restore' instruction.  */
 
static void
output_restore (rtx pat)
{
  rtx operands[3];

  if (! pat)
    {
      fputs ("\t restore\n", asm_out_file);
      return;
    }

  gcc_assert (GET_CODE (pat) == SET);

  operands[0] = SET_DEST (pat);
  pat = SET_SRC (pat);

  switch (GET_CODE (pat))
    {
      case PLUS:
	operands[1] = XEXP (pat, 0);
	operands[2] = XEXP (pat, 1);
	output_asm_insn (" restore %r1, %2, %Y0", operands);
	break;
      case LO_SUM:
	operands[1] = XEXP (pat, 0);
	operands[2] = XEXP (pat, 1);
	output_asm_insn (" restore %r1, %%lo(%a2), %Y0", operands);
	break;
      case ASHIFT:
	operands[1] = XEXP (pat, 0);
	gcc_assert (XEXP (pat, 1) == const1_rtx);
	output_asm_insn (" restore %r1, %r1, %Y0", operands);
	break;
      default:
	operands[1] = pat;
	output_asm_insn (" restore %%g0, %1, %Y0", operands);
	break;
    }
}
  
/* Output a return.  */

const char *
output_return (rtx insn)
{
  if (sparc_leaf_function_p)
    {
      /* This is a leaf function so we don't have to bother restoring the
	 register window, which frees us from dealing with the convoluted
	 semantics of restore/return.  We simply output the jump to the
	 return address and the insn in the delay slot (if any).  */

      gcc_assert (! current_function_calls_eh_return);

      return "jmp\t%%o7+%)%#";
    }
  else
    {
      /* This is a regular function so we have to restore the register window.
	 We may have a pending insn for the delay slot, which will be either
	 combined with the 'restore' instruction or put in the delay slot of
	 the 'return' instruction.  */

      if (current_function_calls_eh_return)
	{
	  /* If the function uses __builtin_eh_return, the eh_return
	     machinery occupies the delay slot.  */
	  gcc_assert (! final_sequence);

	  if (! flag_delayed_branch)
	    fputs ("\tadd\t%fp, %g1, %fp\n", asm_out_file);

	  if (TARGET_V9)
	    fputs ("\treturn\t%i7+8\n", asm_out_file);
	  else
	    fputs ("\trestore\n\tjmp\t%o7+8\n", asm_out_file);

	  if (flag_delayed_branch)
	    fputs ("\t add\t%sp, %g1, %sp\n", asm_out_file);
	  else
	    fputs ("\t nop\n", asm_out_file);
	}
      else if (final_sequence)
	{
	  rtx delay, pat;

	  delay = NEXT_INSN (insn);
	  gcc_assert (delay);

	  pat = PATTERN (delay);

	  if (TARGET_V9 && ! epilogue_renumber (&pat, 1))
	    {
	      epilogue_renumber (&pat, 0);
	      return "return\t%%i7+%)%#";
	    }
	  else
	    {
	      output_asm_insn ("jmp\t%%i7+%)", NULL);
	      output_restore (pat);
	      PATTERN (delay) = gen_blockage ();
	      INSN_CODE (delay) = -1;
	    }
	}
      else
        {
	  /* The delay slot is empty.  */
	  if (TARGET_V9)
	    return "return\t%%i7+%)\n\t nop";
	  else if (flag_delayed_branch)
	    return "jmp\t%%i7+%)\n\t restore";
	  else
	    return "restore\n\tjmp\t%%o7+%)\n\t nop";
	}
    }

  return "";
}

/* Output a sibling call.  */

const char *
output_sibcall (rtx insn, rtx call_operand)
{
  rtx operands[1];

  gcc_assert (flag_delayed_branch);

  operands[0] = call_operand;

  if (sparc_leaf_function_p)
    {
      /* This is a leaf function so we don't have to bother restoring the
	 register window.  We simply output the jump to the function and
	 the insn in the delay slot (if any).  */

      gcc_assert (!(LEAF_SIBCALL_SLOT_RESERVED_P && final_sequence));

      if (final_sequence)
	output_asm_insn ("sethi\t%%hi(%a0), %%g1\n\tjmp\t%%g1 + %%lo(%a0)%#",
			 operands);
      else
	/* Use or with rs2 %%g0 instead of mov, so that as/ld can optimize
	   it into branch if possible.  */
	output_asm_insn ("or\t%%o7, %%g0, %%g1\n\tcall\t%a0, 0\n\t or\t%%g1, %%g0, %%o7",
			 operands);
    }
  else
    {
      /* This is a regular function so we have to restore the register window.
	 We may have a pending insn for the delay slot, which will be combined
	 with the 'restore' instruction.  */

      output_asm_insn ("call\t%a0, 0", operands);

      if (final_sequence)
	{
	  rtx delay = NEXT_INSN (insn);
	  gcc_assert (delay);

	  output_restore (PATTERN (delay));

	  PATTERN (delay) = gen_blockage ();
	  INSN_CODE (delay) = -1;
	}
      else
	output_restore (NULL_RTX);
    }

  return "";
}

/* Functions for handling argument passing.

   For 32-bit, the first 6 args are normally in registers and the rest are
   pushed.  Any arg that starts within the first 6 words is at least
   partially passed in a register unless its data type forbids.

   For 64-bit, the argument registers are laid out as an array of 16 elements
   and arguments are added sequentially.  The first 6 int args and up to the
   first 16 fp args (depending on size) are passed in regs.

   Slot    Stack   Integral   Float   Float in structure   Double   Long Double
   ----    -----   --------   -----   ------------------   ------   -----------
    15   [SP+248]              %f31       %f30,%f31         %d30
    14   [SP+240]              %f29       %f28,%f29         %d28       %q28
    13   [SP+232]              %f27       %f26,%f27         %d26
    12   [SP+224]              %f25       %f24,%f25         %d24       %q24
    11   [SP+216]              %f23       %f22,%f23         %d22
    10   [SP+208]              %f21       %f20,%f21         %d20       %q20
     9   [SP+200]              %f19       %f18,%f19         %d18
     8   [SP+192]              %f17       %f16,%f17         %d16       %q16
     7   [SP+184]              %f15       %f14,%f15         %d14
     6   [SP+176]              %f13       %f12,%f13         %d12       %q12
     5   [SP+168]     %o5      %f11       %f10,%f11         %d10
     4   [SP+160]     %o4       %f9        %f8,%f9           %d8        %q8
     3   [SP+152]     %o3       %f7        %f6,%f7           %d6
     2   [SP+144]     %o2       %f5        %f4,%f5           %d4        %q4
     1   [SP+136]     %o1       %f3        %f2,%f3           %d2
     0   [SP+128]     %o0       %f1        %f0,%f1           %d0        %q0

   Here SP = %sp if -mno-stack-bias or %sp+stack_bias otherwise.

   Integral arguments are always passed as 64-bit quantities appropriately
   extended.

   Passing of floating point values is handled as follows.
   If a prototype is in scope:
     If the value is in a named argument (i.e. not a stdarg function or a
     value not part of the `...') then the value is passed in the appropriate
     fp reg.
     If the value is part of the `...' and is passed in one of the first 6
     slots then the value is passed in the appropriate int reg.
     If the value is part of the `...' and is not passed in one of the first 6
     slots then the value is passed in memory.
   If a prototype is not in scope:
     If the value is one of the first 6 arguments the value is passed in the
     appropriate integer reg and the appropriate fp reg.
     If the value is not one of the first 6 arguments the value is passed in
     the appropriate fp reg and in memory.


   Summary of the calling conventions implemented by GCC on SPARC:

   32-bit ABI:
                                size      argument     return value

      small integer              <4       int. reg.      int. reg.
      word                        4       int. reg.      int. reg.
      double word                 8       int. reg.      int. reg.

      _Complex small integer     <8       int. reg.      int. reg.
      _Complex word               8       int. reg.      int. reg.
      _Complex double word       16        memory        int. reg.

      vector integer            <=8       int. reg.       FP reg.
      vector integer             >8        memory         memory

      float                       4       int. reg.       FP reg.
      double                      8       int. reg.       FP reg.
      long double                16        memory         memory

      _Complex float              8        memory         FP reg.
      _Complex double            16        memory         FP reg.
      _Complex long double       32        memory         FP reg.

      vector float              any        memory         memory

      aggregate                 any        memory         memory



    64-bit ABI:
                                size      argument     return value

      small integer              <8       int. reg.      int. reg.
      word                        8       int. reg.      int. reg.
      double word                16       int. reg.      int. reg.

      _Complex small integer    <16       int. reg.      int. reg.
      _Complex word              16       int. reg.      int. reg.
      _Complex double word       32        memory        int. reg.

      vector integer           <=16        FP reg.        FP reg.
      vector integer       16<s<=32        memory         FP reg.
      vector integer            >32        memory         memory

      float                       4        FP reg.        FP reg.
      double                      8        FP reg.        FP reg.
      long double                16        FP reg.        FP reg.

      _Complex float              8        FP reg.        FP reg.
      _Complex double            16        FP reg.        FP reg.
      _Complex long double       32        memory         FP reg.

      vector float             <=16        FP reg.        FP reg.
      vector float         16<s<=32        memory         FP reg.
      vector float              >32        memory         memory

      aggregate                <=16         reg.           reg.
      aggregate            16<s<=32        memory          reg.
      aggregate                 >32        memory         memory



Note #1: complex floating-point types follow the extended SPARC ABIs as
implemented by the Sun compiler.

Note #2: integral vector types follow the scalar floating-point types
conventions to match what is implemented by the Sun VIS SDK.

Note #3: floating-point vector types follow the aggregate types 
conventions.  */


/* Maximum number of int regs for args.  */
#define SPARC_INT_ARG_MAX 6
/* Maximum number of fp regs for args.  */
#define SPARC_FP_ARG_MAX 16

#define ROUND_ADVANCE(SIZE) (((SIZE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* Handle the INIT_CUMULATIVE_ARGS macro.
   Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.  */

void
init_cumulative_args (struct sparc_args *cum, tree fntype,
		      rtx libname ATTRIBUTE_UNUSED,
		      tree fndecl ATTRIBUTE_UNUSED)
{
  cum->words = 0;
  cum->prototype_p = fntype && TYPE_ARG_TYPES (fntype);
  cum->libcall_p = fntype == 0;
}

/* Handle the TARGET_PROMOTE_PROTOTYPES target hook.
   When a prototype says `char' or `short', really pass an `int'.  */

static bool
sparc_promote_prototypes (tree fntype ATTRIBUTE_UNUSED)
{
  return TARGET_ARCH32 ? true : false;
}

/* Handle the TARGET_STRICT_ARGUMENT_NAMING target hook.  */

static bool
sparc_strict_argument_naming (CUMULATIVE_ARGS *ca ATTRIBUTE_UNUSED)
{
  return TARGET_ARCH64 ? true : false;
}

/* Scan the record type TYPE and return the following predicates:
    - INTREGS_P: the record contains at least one field or sub-field
      that is eligible for promotion in integer registers.
    - FP_REGS_P: the record contains at least one field or sub-field
      that is eligible for promotion in floating-point registers.
    - PACKED_P: the record contains at least one field that is packed.

   Sub-fields are not taken into account for the PACKED_P predicate.  */

static void
scan_record_type (tree type, int *intregs_p, int *fpregs_p, int *packed_p)
{
  tree field;

  for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
    {
      if (TREE_CODE (field) == FIELD_DECL)
	{
	  if (TREE_CODE (TREE_TYPE (field)) == RECORD_TYPE)
	    scan_record_type (TREE_TYPE (field), intregs_p, fpregs_p, 0);
	  else if ((FLOAT_TYPE_P (TREE_TYPE (field))
		   || TREE_CODE (TREE_TYPE (field)) == VECTOR_TYPE)
		  && TARGET_FPU)
	    *fpregs_p = 1;
	  else
	    *intregs_p = 1;

	  if (packed_p && DECL_PACKED (field))
	    *packed_p = 1;
	}
    }
}

/* Compute the slot number to pass an argument in.
   Return the slot number or -1 if passing on the stack.

   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).
   INCOMING_P is zero for FUNCTION_ARG, nonzero for FUNCTION_INCOMING_ARG.
   *PREGNO records the register number to use if scalar type.
   *PPADDING records the amount of padding needed in words.  */

static int
function_arg_slotno (const struct sparc_args *cum, enum machine_mode mode,
		     tree type, int named, int incoming_p,
		     int *pregno, int *ppadding)
{
  int regbase = (incoming_p
		 ? SPARC_INCOMING_INT_ARG_FIRST
		 : SPARC_OUTGOING_INT_ARG_FIRST);
  int slotno = cum->words;
  enum mode_class mclass;
  int regno;

  *ppadding = 0;

  if (type && TREE_ADDRESSABLE (type))
    return -1;

  if (TARGET_ARCH32
      && mode == BLKmode
      && type
      && TYPE_ALIGN (type) % PARM_BOUNDARY != 0)
    return -1;

  /* For SPARC64, objects requiring 16-byte alignment get it.  */
  if (TARGET_ARCH64
      && (type ? TYPE_ALIGN (type) : GET_MODE_ALIGNMENT (mode)) >= 128
      && (slotno & 1) != 0)
    slotno++, *ppadding = 1;

  mclass = GET_MODE_CLASS (mode);
  if (type && TREE_CODE (type) == VECTOR_TYPE)
    {
      /* Vector types deserve special treatment because they are
	 polymorphic wrt their mode, depending upon whether VIS
	 instructions are enabled.  */
      if (TREE_CODE (TREE_TYPE (type)) == REAL_TYPE)
	{
	  /* The SPARC port defines no floating-point vector modes.  */
	  gcc_assert (mode == BLKmode);
	}
      else
	{
	  /* Integral vector types should either have a vector
	     mode or an integral mode, because we are guaranteed
	     by pass_by_reference that their size is not greater
	     than 16 bytes and TImode is 16-byte wide.  */
	  gcc_assert (mode != BLKmode);

	  /* Vector integers are handled like floats according to
	     the Sun VIS SDK.  */
	  mclass = MODE_FLOAT;
	}
    }

  switch (mclass)
    {
    case MODE_FLOAT:
    case MODE_COMPLEX_FLOAT:
      if (TARGET_ARCH64 && TARGET_FPU && named)
	{
	  if (slotno >= SPARC_FP_ARG_MAX)
	    return -1;
	  regno = SPARC_FP_ARG_FIRST + slotno * 2;
	  /* Arguments filling only one single FP register are
	     right-justified in the outer double FP register.  */
	  if (GET_MODE_SIZE (mode) <= 4)
	    regno++;
	  break;
	}
      /* fallthrough */

    case MODE_INT:
    case MODE_COMPLEX_INT:
      if (slotno >= SPARC_INT_ARG_MAX)
	return -1;
      regno = regbase + slotno;
      break;

    case MODE_RANDOM:
      if (mode == VOIDmode)
	/* MODE is VOIDmode when generating the actual call.  */
	return -1;

      gcc_assert (mode == BLKmode);

      if (TARGET_ARCH32
	  || !type
	  || (TREE_CODE (type) != VECTOR_TYPE
	      && TREE_CODE (type) != RECORD_TYPE))
	{
	  if (slotno >= SPARC_INT_ARG_MAX)
	    return -1;
	  regno = regbase + slotno;
	}
      else  /* TARGET_ARCH64 && type */
	{
	  int intregs_p = 0, fpregs_p = 0, packed_p = 0;

	  /* First see what kinds of registers we would need.  */
	  if (TREE_CODE (type) == VECTOR_TYPE)
	    fpregs_p = 1;
	  else
	    scan_record_type (type, &intregs_p, &fpregs_p, &packed_p);

	  /* The ABI obviously doesn't specify how packed structures
	     are passed.  These are defined to be passed in int regs
	     if possible, otherwise memory.  */
	  if (packed_p || !named)
	    fpregs_p = 0, intregs_p = 1;

	  /* If all arg slots are filled, then must pass on stack.  */
	  if (fpregs_p && slotno >= SPARC_FP_ARG_MAX)
	    return -1;

	  /* If there are only int args and all int arg slots are filled,
	     then must pass on stack.  */
	  if (!fpregs_p && intregs_p && slotno >= SPARC_INT_ARG_MAX)
	    return -1;

	  /* Note that even if all int arg slots are filled, fp members may
	     still be passed in regs if such regs are available.
	     *PREGNO isn't set because there may be more than one, it's up
	     to the caller to compute them.  */
	  return slotno;
	}
      break;

    default :
      gcc_unreachable ();
    }

  *pregno = regno;
  return slotno;
}

/* Handle recursive register counting for structure field layout.  */

struct function_arg_record_value_parms
{
  rtx ret;		/* return expression being built.  */
  int slotno;		/* slot number of the argument.  */
  int named;		/* whether the argument is named.  */
  int regbase;		/* regno of the base register.  */
  int stack;		/* 1 if part of the argument is on the stack.  */
  int intoffset;	/* offset of the first pending integer field.  */
  unsigned int nregs;	/* number of words passed in registers.  */
};

static void function_arg_record_value_3
 (HOST_WIDE_INT, struct function_arg_record_value_parms *);
static void function_arg_record_value_2
 (tree, HOST_WIDE_INT, struct function_arg_record_value_parms *, bool);
static void function_arg_record_value_1
 (tree, HOST_WIDE_INT, struct function_arg_record_value_parms *, bool);
static rtx function_arg_record_value (tree, enum machine_mode, int, int, int);
static rtx function_arg_union_value (int, enum machine_mode, int, int);

/* A subroutine of function_arg_record_value.  Traverse the structure
   recursively and determine how many registers will be required.  */

static void
function_arg_record_value_1 (tree type, HOST_WIDE_INT startbitpos,
			     struct function_arg_record_value_parms *parms,
			     bool packed_p)
{
  tree field;

  /* We need to compute how many registers are needed so we can
     allocate the PARALLEL but before we can do that we need to know
     whether there are any packed fields.  The ABI obviously doesn't
     specify how structures are passed in this case, so they are
     defined to be passed in int regs if possible, otherwise memory,
     regardless of whether there are fp values present.  */

  if (! packed_p)
    for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
      {
	if (TREE_CODE (field) == FIELD_DECL && DECL_PACKED (field))
	  {
	    packed_p = true;
	    break;
	  }
      }

  /* Compute how many registers we need.  */
  for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
    {
      if (TREE_CODE (field) == FIELD_DECL)
	{
	  HOST_WIDE_INT bitpos = startbitpos;

	  if (DECL_SIZE (field) != 0)
	    {
	      if (integer_zerop (DECL_SIZE (field)))
		continue;

	      if (host_integerp (bit_position (field), 1))
		bitpos += int_bit_position (field);
	    }

	  /* ??? FIXME: else assume zero offset.  */

	  if (TREE_CODE (TREE_TYPE (field)) == RECORD_TYPE)
	    function_arg_record_value_1 (TREE_TYPE (field),
	    				 bitpos,
					 parms,
					 packed_p);
	  else if ((FLOAT_TYPE_P (TREE_TYPE (field))
		    || TREE_CODE (TREE_TYPE (field)) == VECTOR_TYPE)
		   && TARGET_FPU
		   && parms->named
		   && ! packed_p)
	    {
	      if (parms->intoffset != -1)
		{
		  unsigned int startbit, endbit;
		  int intslots, this_slotno;

		  startbit = parms->intoffset & -BITS_PER_WORD;
		  endbit   = (bitpos + BITS_PER_WORD - 1) & -BITS_PER_WORD;

		  intslots = (endbit - startbit) / BITS_PER_WORD;
		  this_slotno = parms->slotno + parms->intoffset
		    / BITS_PER_WORD;

		  if (intslots > 0 && intslots > SPARC_INT_ARG_MAX - this_slotno)
		    {
		      intslots = MAX (0, SPARC_INT_ARG_MAX - this_slotno);
		      /* We need to pass this field on the stack.  */
		      parms->stack = 1;
		    }

		  parms->nregs += intslots;
		  parms->intoffset = -1;
		}

	      /* There's no need to check this_slotno < SPARC_FP_ARG MAX.
		 If it wasn't true we wouldn't be here.  */
	      if (TREE_CODE (TREE_TYPE (field)) == VECTOR_TYPE
		  && DECL_MODE (field) == BLKmode)
		parms->nregs += TYPE_VECTOR_SUBPARTS (TREE_TYPE (field));
	      else if (TREE_CODE (TREE_TYPE (field)) == COMPLEX_TYPE)
		parms->nregs += 2;
	      else
		parms->nregs += 1;
	    }
	  else
	    {
	      if (parms->intoffset == -1)
		parms->intoffset = bitpos;
	    }
	}
    }
}

/* A subroutine of function_arg_record_value.  Assign the bits of the
   structure between parms->intoffset and bitpos to integer registers.  */

static void 
function_arg_record_value_3 (HOST_WIDE_INT bitpos,
			     struct function_arg_record_value_parms *parms)
{
  enum machine_mode mode;
  unsigned int regno;
  unsigned int startbit, endbit;
  int this_slotno, intslots, intoffset;
  rtx reg;

  if (parms->intoffset == -1)
    return;

  intoffset = parms->intoffset;
  parms->intoffset = -1;

  startbit = intoffset & -BITS_PER_WORD;
  endbit = (bitpos + BITS_PER_WORD - 1) & -BITS_PER_WORD;
  intslots = (endbit - startbit) / BITS_PER_WORD;
  this_slotno = parms->slotno + intoffset / BITS_PER_WORD;

  intslots = MIN (intslots, SPARC_INT_ARG_MAX - this_slotno);
  if (intslots <= 0)
    return;

  /* If this is the trailing part of a word, only load that much into
     the register.  Otherwise load the whole register.  Note that in
     the latter case we may pick up unwanted bits.  It's not a problem
     at the moment but may wish to revisit.  */

  if (intoffset % BITS_PER_WORD != 0)
    mode = smallest_mode_for_size (BITS_PER_WORD - intoffset % BITS_PER_WORD,
			  	   MODE_INT);
  else
    mode = word_mode;

  intoffset /= BITS_PER_UNIT;
  do
    {
      regno = parms->regbase + this_slotno;
      reg = gen_rtx_REG (mode, regno);
      XVECEXP (parms->ret, 0, parms->stack + parms->nregs)
	= gen_rtx_EXPR_LIST (VOIDmode, reg, GEN_INT (intoffset));

      this_slotno += 1;
      intoffset = (intoffset | (UNITS_PER_WORD-1)) + 1;
      mode = word_mode;
      parms->nregs += 1;
      intslots -= 1;
    }
  while (intslots > 0);
}

/* A subroutine of function_arg_record_value.  Traverse the structure
   recursively and assign bits to floating point registers.  Track which
   bits in between need integer registers; invoke function_arg_record_value_3
   to make that happen.  */

static void
function_arg_record_value_2 (tree type, HOST_WIDE_INT startbitpos,
			     struct function_arg_record_value_parms *parms,
			     bool packed_p)
{
  tree field;

  if (! packed_p)
    for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
      {
	if (TREE_CODE (field) == FIELD_DECL && DECL_PACKED (field))
	  {
	    packed_p = true;
	    break;
	  }
      }

  for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
    {
      if (TREE_CODE (field) == FIELD_DECL)
	{
	  HOST_WIDE_INT bitpos = startbitpos;

	  if (DECL_SIZE (field) != 0)
	    {
	      if (integer_zerop (DECL_SIZE (field)))
		continue;

	      if (host_integerp (bit_position (field), 1))
		bitpos += int_bit_position (field);
	    }

	  /* ??? FIXME: else assume zero offset.  */

	  if (TREE_CODE (TREE_TYPE (field)) == RECORD_TYPE)
	    function_arg_record_value_2 (TREE_TYPE (field),
	    				 bitpos,
					 parms,
					 packed_p);
	  else if ((FLOAT_TYPE_P (TREE_TYPE (field))
		    || TREE_CODE (TREE_TYPE (field)) == VECTOR_TYPE)
		   && TARGET_FPU
		   && parms->named
		   && ! packed_p)
	    {
	      int this_slotno = parms->slotno + bitpos / BITS_PER_WORD;
	      int regno, nregs, pos;
	      enum machine_mode mode = DECL_MODE (field);
	      rtx reg;

	      function_arg_record_value_3 (bitpos, parms);

	      if (TREE_CODE (TREE_TYPE (field)) == VECTOR_TYPE
		  && mode == BLKmode)
	        {
		  mode = TYPE_MODE (TREE_TYPE (TREE_TYPE (field)));
		  nregs = TYPE_VECTOR_SUBPARTS (TREE_TYPE (field));
		}
	      else if (TREE_CODE (TREE_TYPE (field)) == COMPLEX_TYPE)
	        {
		  mode = TYPE_MODE (TREE_TYPE (TREE_TYPE (field)));
		  nregs = 2;
		}
	      else
	        nregs = 1;

	      regno = SPARC_FP_ARG_FIRST + this_slotno * 2;
	      if (GET_MODE_SIZE (mode) <= 4 && (bitpos & 32) != 0)
		regno++;
	      reg = gen_rtx_REG (mode, regno);
	      pos = bitpos / BITS_PER_UNIT;
	      XVECEXP (parms->ret, 0, parms->stack + parms->nregs)
		= gen_rtx_EXPR_LIST (VOIDmode, reg, GEN_INT (pos));
	      parms->nregs += 1;
	      while (--nregs > 0)
		{
		  regno += GET_MODE_SIZE (mode) / 4;
	  	  reg = gen_rtx_REG (mode, regno);
		  pos += GET_MODE_SIZE (mode);
		  XVECEXP (parms->ret, 0, parms->stack + parms->nregs)
		    = gen_rtx_EXPR_LIST (VOIDmode, reg, GEN_INT (pos));
		  parms->nregs += 1;
		}
	    }
	  else
	    {
	      if (parms->intoffset == -1)
		parms->intoffset = bitpos;
	    }
	}
    }
}

/* Used by function_arg and function_value to implement the complex
   conventions of the 64-bit ABI for passing and returning structures.
   Return an expression valid as a return value for the two macros
   FUNCTION_ARG and FUNCTION_VALUE.

   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   MODE is the argument's machine mode.
   SLOTNO is the index number of the argument's slot in the parameter array.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).
   REGBASE is the regno of the base register for the parameter array.  */
   
static rtx
function_arg_record_value (tree type, enum machine_mode mode,
			   int slotno, int named, int regbase)
{
  HOST_WIDE_INT typesize = int_size_in_bytes (type);
  struct function_arg_record_value_parms parms;
  unsigned int nregs;

  parms.ret = NULL_RTX;
  parms.slotno = slotno;
  parms.named = named;
  parms.regbase = regbase;
  parms.stack = 0;

  /* Compute how many registers we need.  */
  parms.nregs = 0;
  parms.intoffset = 0;
  function_arg_record_value_1 (type, 0, &parms, false);

  /* Take into account pending integer fields.  */
  if (parms.intoffset != -1)
    {
      unsigned int startbit, endbit;
      int intslots, this_slotno;

      startbit = parms.intoffset & -BITS_PER_WORD;
      endbit = (typesize*BITS_PER_UNIT + BITS_PER_WORD - 1) & -BITS_PER_WORD;
      intslots = (endbit - startbit) / BITS_PER_WORD;
      this_slotno = slotno + parms.intoffset / BITS_PER_WORD;

      if (intslots > 0 && intslots > SPARC_INT_ARG_MAX - this_slotno)
        {
	  intslots = MAX (0, SPARC_INT_ARG_MAX - this_slotno);
	  /* We need to pass this field on the stack.  */
	  parms.stack = 1;
        }

      parms.nregs += intslots;
    }
  nregs = parms.nregs;

  /* Allocate the vector and handle some annoying special cases.  */
  if (nregs == 0)
    {
      /* ??? Empty structure has no value?  Duh?  */
      if (typesize <= 0)
	{
	  /* Though there's nothing really to store, return a word register
	     anyway so the rest of gcc doesn't go nuts.  Returning a PARALLEL
	     leads to breakage due to the fact that there are zero bytes to
	     load.  */
	  return gen_rtx_REG (mode, regbase);
	}
      else
	{
	  /* ??? C++ has structures with no fields, and yet a size.  Give up
	     for now and pass everything back in integer registers.  */
	  nregs = (typesize + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
	}
      if (nregs + slotno > SPARC_INT_ARG_MAX)
	nregs = SPARC_INT_ARG_MAX - slotno;
    }
  gcc_assert (nregs != 0);

  parms.ret = gen_rtx_PARALLEL (mode, rtvec_alloc (parms.stack + nregs));

  /* If at least one field must be passed on the stack, generate
     (parallel [(expr_list (nil) ...) ...]) so that all fields will
     also be passed on the stack.  We can't do much better because the
     semantics of TARGET_ARG_PARTIAL_BYTES doesn't handle the case
     of structures for which the fields passed exclusively in registers
     are not at the beginning of the structure.  */
  if (parms.stack)
    XVECEXP (parms.ret, 0, 0)
      = gen_rtx_EXPR_LIST (VOIDmode, NULL_RTX, const0_rtx);

  /* Fill in the entries.  */
  parms.nregs = 0;
  parms.intoffset = 0;
  function_arg_record_value_2 (type, 0, &parms, false);
  function_arg_record_value_3 (typesize * BITS_PER_UNIT, &parms);

  gcc_assert (parms.nregs == nregs);

  return parms.ret;
}

/* Used by function_arg and function_value to implement the conventions
   of the 64-bit ABI for passing and returning unions.
   Return an expression valid as a return value for the two macros
   FUNCTION_ARG and FUNCTION_VALUE.

   SIZE is the size in bytes of the union.
   MODE is the argument's machine mode.
   REGNO is the hard register the union will be passed in.  */

static rtx
function_arg_union_value (int size, enum machine_mode mode, int slotno,
			  int regno)
{
  int nwords = ROUND_ADVANCE (size), i;
  rtx regs;

  /* See comment in previous function for empty structures.  */
  if (nwords == 0)
    return gen_rtx_REG (mode, regno);

  if (slotno == SPARC_INT_ARG_MAX - 1)
    nwords = 1;

  regs = gen_rtx_PARALLEL (mode, rtvec_alloc (nwords));

  for (i = 0; i < nwords; i++)
    {
      /* Unions are passed left-justified.  */
      XVECEXP (regs, 0, i)
	= gen_rtx_EXPR_LIST (VOIDmode,
			     gen_rtx_REG (word_mode, regno),
			     GEN_INT (UNITS_PER_WORD * i));
      regno++;
    }

  return regs;
}

/* Used by function_arg and function_value to implement the conventions
   for passing and returning large (BLKmode) vectors.
   Return an expression valid as a return value for the two macros
   FUNCTION_ARG and FUNCTION_VALUE.

   SIZE is the size in bytes of the vector.
   BASE_MODE is the argument's base machine mode.
   REGNO is the FP hard register the vector will be passed in.  */

static rtx
function_arg_vector_value (int size, enum machine_mode base_mode, int regno)
{
  unsigned short base_mode_size = GET_MODE_SIZE (base_mode);
  int nregs = size / base_mode_size, i;
  rtx regs;

  regs = gen_rtx_PARALLEL (BLKmode, rtvec_alloc (nregs));

  for (i = 0; i < nregs; i++)
    {
      XVECEXP (regs, 0, i)
	= gen_rtx_EXPR_LIST (VOIDmode,
			     gen_rtx_REG (base_mode, regno),
			     GEN_INT (base_mode_size * i));
      regno += base_mode_size / 4;
    }

  return regs;
}

/* Handle the FUNCTION_ARG macro.
   Determine where to put an argument to a function.
   Value is zero to push the argument on the stack,
   or a hard register in which to store the argument.

   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).
   INCOMING_P is zero for FUNCTION_ARG, nonzero for FUNCTION_INCOMING_ARG.  */

rtx
function_arg (const struct sparc_args *cum, enum machine_mode mode,
	      tree type, int named, int incoming_p)
{
  int regbase = (incoming_p
		 ? SPARC_INCOMING_INT_ARG_FIRST
		 : SPARC_OUTGOING_INT_ARG_FIRST);
  int slotno, regno, padding;
  enum mode_class mclass = GET_MODE_CLASS (mode);

  slotno = function_arg_slotno (cum, mode, type, named, incoming_p,
				&regno, &padding);
  if (slotno == -1)
    return 0;

  /* Vector types deserve special treatment because they are polymorphic wrt
     their mode, depending upon whether VIS instructions are enabled.  */
  if (type && TREE_CODE (type) == VECTOR_TYPE)
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      gcc_assert ((TARGET_ARCH32 && size <= 8)
		  || (TARGET_ARCH64 && size <= 16));

      if (mode == BLKmode)
	return function_arg_vector_value (size,
					  TYPE_MODE (TREE_TYPE (type)),
					  SPARC_FP_ARG_FIRST + 2*slotno);
      else
	mclass = MODE_FLOAT;
    }

  if (TARGET_ARCH32)
    return gen_rtx_REG (mode, regno);

  /* Structures up to 16 bytes in size are passed in arg slots on the stack
     and are promoted to registers if possible.  */
  if (type && TREE_CODE (type) == RECORD_TYPE)
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      gcc_assert (size <= 16);

      return function_arg_record_value (type, mode, slotno, named, regbase);
    }

  /* Unions up to 16 bytes in size are passed in integer registers.  */
  else if (type && TREE_CODE (type) == UNION_TYPE)
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      gcc_assert (size <= 16);

      return function_arg_union_value (size, mode, slotno, regno);
    }

  /* v9 fp args in reg slots beyond the int reg slots get passed in regs
     but also have the slot allocated for them.
     If no prototype is in scope fp values in register slots get passed
     in two places, either fp regs and int regs or fp regs and memory.  */
  else if ((mclass == MODE_FLOAT || mclass == MODE_COMPLEX_FLOAT)
	   && SPARC_FP_REG_P (regno))
    {
      rtx reg = gen_rtx_REG (mode, regno);
      if (cum->prototype_p || cum->libcall_p)
	{
	  /* "* 2" because fp reg numbers are recorded in 4 byte
	     quantities.  */
#if 0
	  /* ??? This will cause the value to be passed in the fp reg and
	     in the stack.  When a prototype exists we want to pass the
	     value in the reg but reserve space on the stack.  That's an
	     optimization, and is deferred [for a bit].  */
	  if ((regno - SPARC_FP_ARG_FIRST) >= SPARC_INT_ARG_MAX * 2)
	    return gen_rtx_PARALLEL (mode,
			    gen_rtvec (2,
				       gen_rtx_EXPR_LIST (VOIDmode,
						NULL_RTX, const0_rtx),
				       gen_rtx_EXPR_LIST (VOIDmode,
						reg, const0_rtx)));
	  else
#else
	  /* ??? It seems that passing back a register even when past
	     the area declared by REG_PARM_STACK_SPACE will allocate
	     space appropriately, and will not copy the data onto the
	     stack, exactly as we desire.

	     This is due to locate_and_pad_parm being called in
	     expand_call whenever reg_parm_stack_space > 0, which
	     while beneficial to our example here, would seem to be
	     in error from what had been intended.  Ho hum...  -- r~ */
#endif
	    return reg;
	}
      else
	{
	  rtx v0, v1;

	  if ((regno - SPARC_FP_ARG_FIRST) < SPARC_INT_ARG_MAX * 2)
	    {
	      int intreg;

	      /* On incoming, we don't need to know that the value
		 is passed in %f0 and %i0, and it confuses other parts
		 causing needless spillage even on the simplest cases.  */
	      if (incoming_p)
		return reg;

	      intreg = (SPARC_OUTGOING_INT_ARG_FIRST
			+ (regno - SPARC_FP_ARG_FIRST) / 2);

	      v0 = gen_rtx_EXPR_LIST (VOIDmode, reg, const0_rtx);
	      v1 = gen_rtx_EXPR_LIST (VOIDmode, gen_rtx_REG (mode, intreg),
				      const0_rtx);
	      return gen_rtx_PARALLEL (mode, gen_rtvec (2, v0, v1));
	    }
	  else
	    {
	      v0 = gen_rtx_EXPR_LIST (VOIDmode, NULL_RTX, const0_rtx);
	      v1 = gen_rtx_EXPR_LIST (VOIDmode, reg, const0_rtx);
	      return gen_rtx_PARALLEL (mode, gen_rtvec (2, v0, v1));
	    }
	}
    }

  /* All other aggregate types are passed in an integer register in a mode
     corresponding to the size of the type.  */
  else if (type && AGGREGATE_TYPE_P (type))
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      gcc_assert (size <= 16);

      mode = mode_for_size (size * BITS_PER_UNIT, MODE_INT, 0);
    }

  return gen_rtx_REG (mode, regno);
}

/* For an arg passed partly in registers and partly in memory,
   this is the number of bytes of registers used.
   For args passed entirely in registers or entirely in memory, zero.

   Any arg that starts in the first 6 regs but won't entirely fit in them
   needs partial registers on v8.  On v9, structures with integer
   values in arg slots 5,6 will be passed in %o5 and SP+176, and complex fp
   values that begin in the last fp reg [where "last fp reg" varies with the
   mode] will be split between that reg and memory.  */

static int
sparc_arg_partial_bytes (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			 tree type, bool named)
{
  int slotno, regno, padding;

  /* We pass 0 for incoming_p here, it doesn't matter.  */
  slotno = function_arg_slotno (cum, mode, type, named, 0, &regno, &padding);

  if (slotno == -1)
    return 0;

  if (TARGET_ARCH32)
    {
      if ((slotno + (mode == BLKmode
		     ? ROUND_ADVANCE (int_size_in_bytes (type))
		     : ROUND_ADVANCE (GET_MODE_SIZE (mode))))
	  > SPARC_INT_ARG_MAX)
	return (SPARC_INT_ARG_MAX - slotno) * UNITS_PER_WORD;
    }
  else
    {
      /* We are guaranteed by pass_by_reference that the size of the
	 argument is not greater than 16 bytes, so we only need to return
	 one word if the argument is partially passed in registers.  */

      if (type && AGGREGATE_TYPE_P (type))
	{
	  int size = int_size_in_bytes (type);

	  if (size > UNITS_PER_WORD
	      && slotno == SPARC_INT_ARG_MAX - 1)
	    return UNITS_PER_WORD;
	}
      else if (GET_MODE_CLASS (mode) == MODE_COMPLEX_INT
	       || (GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT
		   && ! (TARGET_FPU && named)))
	{
	  /* The complex types are passed as packed types.  */
	  if (GET_MODE_SIZE (mode) > UNITS_PER_WORD
	      && slotno == SPARC_INT_ARG_MAX - 1)
	    return UNITS_PER_WORD;
	}
      else if (GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT)
	{
	  if ((slotno + GET_MODE_SIZE (mode) / UNITS_PER_WORD)
	      > SPARC_FP_ARG_MAX)
	    return UNITS_PER_WORD;
	}
    }

  return 0;
}

/* Handle the TARGET_PASS_BY_REFERENCE target hook.
   Specify whether to pass the argument by reference.  */

static bool
sparc_pass_by_reference (CUMULATIVE_ARGS *cum ATTRIBUTE_UNUSED,
			 enum machine_mode mode, tree type,
			 bool named ATTRIBUTE_UNUSED)
{
  if (TARGET_ARCH32)
    /* Original SPARC 32-bit ABI says that structures and unions,
       and quad-precision floats are passed by reference.  For Pascal,
       also pass arrays by reference.  All other base types are passed
       in registers.

       Extended ABI (as implemented by the Sun compiler) says that all
       complex floats are passed by reference.  Pass complex integers
       in registers up to 8 bytes.  More generally, enforce the 2-word
       cap for passing arguments in registers.

       Vector ABI (as implemented by the Sun VIS SDK) says that vector
       integers are passed like floats of the same size, that is in
       registers up to 8 bytes.  Pass all vector floats by reference
       like structure and unions.  */
    return ((type && (AGGREGATE_TYPE_P (type) || VECTOR_FLOAT_TYPE_P (type)))
	    || mode == SCmode
	    /* Catch CDImode, TFmode, DCmode and TCmode.  */
	    || GET_MODE_SIZE (mode) > 8
	    || (type
		&& TREE_CODE (type) == VECTOR_TYPE
		&& (unsigned HOST_WIDE_INT) int_size_in_bytes (type) > 8));
  else
    /* Original SPARC 64-bit ABI says that structures and unions
       smaller than 16 bytes are passed in registers, as well as
       all other base types.
       
       Extended ABI (as implemented by the Sun compiler) says that
       complex floats are passed in registers up to 16 bytes.  Pass
       all complex integers in registers up to 16 bytes.  More generally,
       enforce the 2-word cap for passing arguments in registers.

       Vector ABI (as implemented by the Sun VIS SDK) says that vector
       integers are passed like floats of the same size, that is in
       registers (up to 16 bytes).  Pass all vector floats like structure
       and unions.  */
    return ((type
	     && (AGGREGATE_TYPE_P (type) || TREE_CODE (type) == VECTOR_TYPE)
	     && (unsigned HOST_WIDE_INT) int_size_in_bytes (type) > 16)
	    /* Catch CTImode and TCmode.  */
	    || GET_MODE_SIZE (mode) > 16);
}

/* Handle the FUNCTION_ARG_ADVANCE macro.
   Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   TYPE is null for libcalls where that information may not be available.  */

void
function_arg_advance (struct sparc_args *cum, enum machine_mode mode,
		      tree type, int named)
{
  int slotno, regno, padding;

  /* We pass 0 for incoming_p here, it doesn't matter.  */
  slotno = function_arg_slotno (cum, mode, type, named, 0, &regno, &padding);

  /* If register required leading padding, add it.  */
  if (slotno != -1)
    cum->words += padding;

  if (TARGET_ARCH32)
    {
      cum->words += (mode != BLKmode
		     ? ROUND_ADVANCE (GET_MODE_SIZE (mode))
		     : ROUND_ADVANCE (int_size_in_bytes (type)));
    }
  else
    {
      if (type && AGGREGATE_TYPE_P (type))
	{
	  int size = int_size_in_bytes (type);

	  if (size <= 8)
	    ++cum->words;
	  else if (size <= 16)
	    cum->words += 2;
	  else /* passed by reference */
	    ++cum->words;
	}
      else
	{
	  cum->words += (mode != BLKmode
			 ? ROUND_ADVANCE (GET_MODE_SIZE (mode))
			 : ROUND_ADVANCE (int_size_in_bytes (type)));
	}
    }
}

/* Handle the FUNCTION_ARG_PADDING macro.
   For the 64 bit ABI structs are always stored left shifted in their
   argument slot.  */

enum direction
function_arg_padding (enum machine_mode mode, tree type)
{
  if (TARGET_ARCH64 && type != 0 && AGGREGATE_TYPE_P (type))
    return upward;

  /* Fall back to the default.  */
  return DEFAULT_FUNCTION_ARG_PADDING (mode, type);
}

/* Handle the TARGET_RETURN_IN_MEMORY target hook.
   Specify whether to return the return value in memory.  */

static bool
sparc_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  if (TARGET_ARCH32)
    /* Original SPARC 32-bit ABI says that structures and unions,
       and quad-precision floats are returned in memory.  All other
       base types are returned in registers.

       Extended ABI (as implemented by the Sun compiler) says that
       all complex floats are returned in registers (8 FP registers
       at most for '_Complex long double').  Return all complex integers
       in registers (4 at most for '_Complex long long').

       Vector ABI (as implemented by the Sun VIS SDK) says that vector
       integers are returned like floats of the same size, that is in
       registers up to 8 bytes and in memory otherwise.  Return all
       vector floats in memory like structure and unions; note that
       they always have BLKmode like the latter.  */
    return (TYPE_MODE (type) == BLKmode
	    || TYPE_MODE (type) == TFmode
	    || (TREE_CODE (type) == VECTOR_TYPE
		&& (unsigned HOST_WIDE_INT) int_size_in_bytes (type) > 8));
  else
    /* Original SPARC 64-bit ABI says that structures and unions
       smaller than 32 bytes are returned in registers, as well as
       all other base types.
       
       Extended ABI (as implemented by the Sun compiler) says that all
       complex floats are returned in registers (8 FP registers at most
       for '_Complex long double').  Return all complex integers in
       registers (4 at most for '_Complex TItype').

       Vector ABI (as implemented by the Sun VIS SDK) says that vector
       integers are returned like floats of the same size, that is in
       registers.  Return all vector floats like structure and unions;
       note that they always have BLKmode like the latter.  */
    return ((TYPE_MODE (type) == BLKmode
	     && (unsigned HOST_WIDE_INT) int_size_in_bytes (type) > 32));
}

/* Handle the TARGET_STRUCT_VALUE target hook.
   Return where to find the structure return value address.  */

static rtx
sparc_struct_value_rtx (tree fndecl, int incoming)
{
  if (TARGET_ARCH64)
    return 0;
  else
    {
      rtx mem;

      if (incoming)
	mem = gen_rtx_MEM (Pmode, plus_constant (frame_pointer_rtx,
						 STRUCT_VALUE_OFFSET));
      else
	mem = gen_rtx_MEM (Pmode, plus_constant (stack_pointer_rtx,
						 STRUCT_VALUE_OFFSET));

      /* Only follow the SPARC ABI for fixed-size structure returns. 
         Variable size structure returns are handled per the normal 
         procedures in GCC. This is enabled by -mstd-struct-return */
      if (incoming == 2 
	  && sparc_std_struct_return
	  && TYPE_SIZE_UNIT (TREE_TYPE (fndecl))
	  && TREE_CODE (TYPE_SIZE_UNIT (TREE_TYPE (fndecl))) == INTEGER_CST)
	{
	  /* We must check and adjust the return address, as it is
	     optional as to whether the return object is really
	     provided.  */
	  rtx ret_rtx = gen_rtx_REG (Pmode, 31);
	  rtx scratch = gen_reg_rtx (SImode);
	  rtx endlab = gen_label_rtx (); 

	  /* Calculate the return object size */
	  tree size = TYPE_SIZE_UNIT (TREE_TYPE (fndecl));
	  rtx size_rtx = GEN_INT (TREE_INT_CST_LOW (size) & 0xfff);
	  /* Construct a temporary return value */
	  rtx temp_val = assign_stack_local (Pmode, TREE_INT_CST_LOW (size), 0);

	  /* Implement SPARC 32-bit psABI callee returns struck checking
	     requirements: 
	    
	      Fetch the instruction where we will return to and see if
	     it's an unimp instruction (the most significant 10 bits
	     will be zero).  */
	  emit_move_insn (scratch, gen_rtx_MEM (SImode,
						plus_constant (ret_rtx, 8)));
	  /* Assume the size is valid and pre-adjust */
	  emit_insn (gen_add3_insn (ret_rtx, ret_rtx, GEN_INT (4)));
	  emit_cmp_and_jump_insns (scratch, size_rtx, EQ, const0_rtx, SImode, 0, endlab);
	  emit_insn (gen_sub3_insn (ret_rtx, ret_rtx, GEN_INT (4)));
	  /* Assign stack temp: 
	     Write the address of the memory pointed to by temp_val into
	     the memory pointed to by mem */
	  emit_move_insn (mem, XEXP (temp_val, 0));
	  emit_label (endlab);
	}

      set_mem_alias_set (mem, struct_value_alias_set);
      return mem;
    }
}

/* Handle FUNCTION_VALUE, FUNCTION_OUTGOING_VALUE, and LIBCALL_VALUE macros.
   For v9, function return values are subject to the same rules as arguments,
   except that up to 32 bytes may be returned in registers.  */

rtx
function_value (tree type, enum machine_mode mode, int incoming_p)
{
  /* Beware that the two values are swapped here wrt function_arg.  */
  int regbase = (incoming_p
		 ? SPARC_OUTGOING_INT_ARG_FIRST
		 : SPARC_INCOMING_INT_ARG_FIRST);
  enum mode_class mclass = GET_MODE_CLASS (mode);
  int regno;

  /* Vector types deserve special treatment because they are polymorphic wrt
     their mode, depending upon whether VIS instructions are enabled.  */
  if (type && TREE_CODE (type) == VECTOR_TYPE)
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      gcc_assert ((TARGET_ARCH32 && size <= 8)
		  || (TARGET_ARCH64 && size <= 32));

      if (mode == BLKmode)
	return function_arg_vector_value (size,
					  TYPE_MODE (TREE_TYPE (type)),
					  SPARC_FP_ARG_FIRST);
      else
	mclass = MODE_FLOAT;
    }

  if (TARGET_ARCH64 && type)
    {
      /* Structures up to 32 bytes in size are returned in registers.  */
      if (TREE_CODE (type) == RECORD_TYPE)
	{
	  HOST_WIDE_INT size = int_size_in_bytes (type);
	  gcc_assert (size <= 32);

	  return function_arg_record_value (type, mode, 0, 1, regbase);
	}

      /* Unions up to 32 bytes in size are returned in integer registers.  */
      else if (TREE_CODE (type) == UNION_TYPE)
	{
	  HOST_WIDE_INT size = int_size_in_bytes (type);
	  gcc_assert (size <= 32);

	  return function_arg_union_value (size, mode, 0, regbase);
	}

      /* Objects that require it are returned in FP registers.  */
      else if (mclass == MODE_FLOAT || mclass == MODE_COMPLEX_FLOAT)
	;

      /* All other aggregate types are returned in an integer register in a
	 mode corresponding to the size of the type.  */
      else if (AGGREGATE_TYPE_P (type))
	{
	  /* All other aggregate types are passed in an integer register
	     in a mode corresponding to the size of the type.  */
	  HOST_WIDE_INT size = int_size_in_bytes (type);
	  gcc_assert (size <= 32);

	  mode = mode_for_size (size * BITS_PER_UNIT, MODE_INT, 0);

	  /* ??? We probably should have made the same ABI change in
	     3.4.0 as the one we made for unions.   The latter was
	     required by the SCD though, while the former is not
	     specified, so we favored compatibility and efficiency.

	     Now we're stuck for aggregates larger than 16 bytes,
	     because OImode vanished in the meantime.  Let's not
	     try to be unduly clever, and simply follow the ABI
	     for unions in that case.  */
	  if (mode == BLKmode)
	    return function_arg_union_value (size, mode, 0, regbase);
	  else
	    mclass = MODE_INT;
	}

      /* This must match PROMOTE_FUNCTION_MODE.  */
      else if (mclass == MODE_INT && GET_MODE_SIZE (mode) < UNITS_PER_WORD)
	mode = word_mode;
    }

  if ((mclass == MODE_FLOAT || mclass == MODE_COMPLEX_FLOAT) && TARGET_FPU)
    regno = SPARC_FP_ARG_FIRST;
  else
    regno = regbase;

  return gen_rtx_REG (mode, regno);
}

/* Do what is necessary for `va_start'.  We look at the current function
   to determine if stdarg or varargs is used and return the address of
   the first unnamed parameter.  */

static rtx
sparc_builtin_saveregs (void)
{
  int first_reg = current_function_args_info.words;
  rtx address;
  int regno;

  for (regno = first_reg; regno < SPARC_INT_ARG_MAX; regno++)
    emit_move_insn (gen_rtx_MEM (word_mode,
				 gen_rtx_PLUS (Pmode,
					       frame_pointer_rtx,
					       GEN_INT (FIRST_PARM_OFFSET (0)
							+ (UNITS_PER_WORD
							   * regno)))),
		    gen_rtx_REG (word_mode,
				 SPARC_INCOMING_INT_ARG_FIRST + regno));

  address = gen_rtx_PLUS (Pmode,
			  frame_pointer_rtx,
			  GEN_INT (FIRST_PARM_OFFSET (0)
				   + UNITS_PER_WORD * first_reg));

  return address;
}

/* Implement `va_start' for stdarg.  */

void
sparc_va_start (tree valist, rtx nextarg)
{
  nextarg = expand_builtin_saveregs ();
  std_expand_builtin_va_start (valist, nextarg);
}

/* Implement `va_arg' for stdarg.  */

static tree
sparc_gimplify_va_arg (tree valist, tree type, tree *pre_p, tree *post_p)
{
  HOST_WIDE_INT size, rsize, align;
  tree addr, incr;
  bool indirect;
  tree ptrtype = build_pointer_type (type);

  if (pass_by_reference (NULL, TYPE_MODE (type), type, false))
    {
      indirect = true;
      size = rsize = UNITS_PER_WORD;
      align = 0;
    }
  else
    {
      indirect = false;
      size = int_size_in_bytes (type);
      rsize = (size + UNITS_PER_WORD - 1) & -UNITS_PER_WORD;
      align = 0;
    
      if (TARGET_ARCH64)
	{
	  /* For SPARC64, objects requiring 16-byte alignment get it.  */
	  if (TYPE_ALIGN (type) >= 2 * (unsigned) BITS_PER_WORD)
	    align = 2 * UNITS_PER_WORD;

	  /* SPARC-V9 ABI states that structures up to 16 bytes in size
	     are left-justified in their slots.  */
	  if (AGGREGATE_TYPE_P (type))
	    {
	      if (size == 0)
		size = rsize = UNITS_PER_WORD;
	      else
		size = rsize;
	    }
	}
    }

  incr = valist;
  if (align)
    {
      incr = fold (build2 (PLUS_EXPR, ptr_type_node, incr,
			   ssize_int (align - 1)));
      incr = fold (build2 (BIT_AND_EXPR, ptr_type_node, incr,
			   ssize_int (-align)));
    }

  gimplify_expr (&incr, pre_p, post_p, is_gimple_val, fb_rvalue);
  addr = incr;

  if (BYTES_BIG_ENDIAN && size < rsize)
    addr = fold (build2 (PLUS_EXPR, ptr_type_node, incr,
			 ssize_int (rsize - size)));

  if (indirect)
    {
      addr = fold_convert (build_pointer_type (ptrtype), addr);
      addr = build_va_arg_indirect_ref (addr);
    }
  /* If the address isn't aligned properly for the type,
     we may need to copy to a temporary.  
     FIXME: This is inefficient.  Usually we can do this
     in registers.  */
  else if (align == 0
	   && TYPE_ALIGN (type) > BITS_PER_WORD)
    {
      tree tmp = create_tmp_var (type, "va_arg_tmp");
      tree dest_addr = build_fold_addr_expr (tmp);

      tree copy = build_function_call_expr
	(implicit_built_in_decls[BUILT_IN_MEMCPY],
	 tree_cons (NULL_TREE, dest_addr,
		    tree_cons (NULL_TREE, addr,
			       tree_cons (NULL_TREE, size_int (rsize),
					  NULL_TREE))));

      gimplify_and_add (copy, pre_p);
      addr = dest_addr;
    }
  else
    addr = fold_convert (ptrtype, addr);

  incr = fold (build2 (PLUS_EXPR, ptr_type_node, incr, ssize_int (rsize)));
  incr = build2 (MODIFY_EXPR, ptr_type_node, valist, incr);
  gimplify_and_add (incr, post_p);

  return build_va_arg_indirect_ref (addr);
}

/* Implement the TARGET_VECTOR_MODE_SUPPORTED_P target hook.
   Specify whether the vector mode is supported by the hardware.  */

static bool
sparc_vector_mode_supported_p (enum machine_mode mode)
{
  return TARGET_VIS && VECTOR_MODE_P (mode) ? true : false;
}

/* Return the string to output an unconditional branch to LABEL, which is
   the operand number of the label.

   DEST is the destination insn (i.e. the label), INSN is the source.  */

const char *
output_ubranch (rtx dest, int label, rtx insn)
{
  static char string[64];
  bool v9_form = false;
  char *p;

  if (TARGET_V9 && INSN_ADDRESSES_SET_P ())
    {
      int delta = (INSN_ADDRESSES (INSN_UID (dest))
		   - INSN_ADDRESSES (INSN_UID (insn)));
      /* Leave some instructions for "slop".  */
      if (delta >= -260000 && delta < 260000)
	v9_form = true;
    }

  if (v9_form)
    strcpy (string, "ba%*,pt\t%%xcc, ");
  else
    strcpy (string, "b%*\t");

  p = strchr (string, '\0');
  *p++ = '%';
  *p++ = 'l';
  *p++ = '0' + label;
  *p++ = '%';
  *p++ = '(';
  *p = '\0';

  return string;
}

/* Return the string to output a conditional branch to LABEL, which is
   the operand number of the label.  OP is the conditional expression.
   XEXP (OP, 0) is assumed to be a condition code register (integer or
   floating point) and its mode specifies what kind of comparison we made.

   DEST is the destination insn (i.e. the label), INSN is the source.

   REVERSED is nonzero if we should reverse the sense of the comparison.

   ANNUL is nonzero if we should generate an annulling branch.  */

const char *
output_cbranch (rtx op, rtx dest, int label, int reversed, int annul,
		rtx insn)
{
  static char string[64];
  enum rtx_code code = GET_CODE (op);
  rtx cc_reg = XEXP (op, 0);
  enum machine_mode mode = GET_MODE (cc_reg);
  const char *labelno, *branch;
  int spaces = 8, far;
  char *p;

  /* v9 branches are limited to +-1MB.  If it is too far away,
     change

     bne,pt %xcc, .LC30

     to

     be,pn %xcc, .+12
      nop
     ba .LC30

     and

     fbne,a,pn %fcc2, .LC29

     to

     fbe,pt %fcc2, .+16
      nop
     ba .LC29  */

  far = TARGET_V9 && (get_attr_length (insn) >= 3);
  if (reversed ^ far)
    {
      /* Reversal of FP compares takes care -- an ordered compare
	 becomes an unordered compare and vice versa.  */
      if (mode == CCFPmode || mode == CCFPEmode)
	code = reverse_condition_maybe_unordered (code);
      else
	code = reverse_condition (code);
    }

  /* Start by writing the branch condition.  */
  if (mode == CCFPmode || mode == CCFPEmode)
    {
      switch (code)
	{
	case NE:
	  branch = "fbne";
	  break;
	case EQ:
	  branch = "fbe";
	  break;
	case GE:
	  branch = "fbge";
	  break;
	case GT:
	  branch = "fbg";
	  break;
	case LE:
	  branch = "fble";
	  break;
	case LT:
	  branch = "fbl";
	  break;
	case UNORDERED:
	  branch = "fbu";
	  break;
	case ORDERED:
	  branch = "fbo";
	  break;
	case UNGT:
	  branch = "fbug";
	  break;
	case UNLT:
	  branch = "fbul";
	  break;
	case UNEQ:
	  branch = "fbue";
	  break;
	case UNGE:
	  branch = "fbuge";
	  break;
	case UNLE:
	  branch = "fbule";
	  break;
	case LTGT:
	  branch = "fblg";
	  break;

	default:
	  gcc_unreachable ();
	}

      /* ??? !v9: FP branches cannot be preceded by another floating point
	 insn.  Because there is currently no concept of pre-delay slots,
	 we can fix this only by always emitting a nop before a floating
	 point branch.  */

      string[0] = '\0';
      if (! TARGET_V9)
	strcpy (string, "nop\n\t");
      strcat (string, branch);
    }
  else
    {
      switch (code)
	{
	case NE:
	  branch = "bne";
	  break;
	case EQ:
	  branch = "be";
	  break;
	case GE:
	  if (mode == CC_NOOVmode || mode == CCX_NOOVmode)
	    branch = "bpos";
	  else
	    branch = "bge";
	  break;
	case GT:
	  branch = "bg";
	  break;
	case LE:
	  branch = "ble";
	  break;
	case LT:
	  if (mode == CC_NOOVmode || mode == CCX_NOOVmode)
	    branch = "bneg";
	  else
	    branch = "bl";
	  break;
	case GEU:
	  branch = "bgeu";
	  break;
	case GTU:
	  branch = "bgu";
	  break;
	case LEU:
	  branch = "bleu";
	  break;
	case LTU:
	  branch = "blu";
	  break;

	default:
	  gcc_unreachable ();
	}
      strcpy (string, branch);
    }
  spaces -= strlen (branch);
  p = strchr (string, '\0');

  /* Now add the annulling, the label, and a possible noop.  */
  if (annul && ! far)
    {
      strcpy (p, ",a");
      p += 2;
      spaces -= 2;
    }

  if (TARGET_V9)
    {
      rtx note;
      int v8 = 0;

      if (! far && insn && INSN_ADDRESSES_SET_P ())
	{
	  int delta = (INSN_ADDRESSES (INSN_UID (dest))
		       - INSN_ADDRESSES (INSN_UID (insn)));
	  /* Leave some instructions for "slop".  */
	  if (delta < -260000 || delta >= 260000)
	    v8 = 1;
	}

      if (mode == CCFPmode || mode == CCFPEmode)
	{
	  static char v9_fcc_labelno[] = "%%fccX, ";
	  /* Set the char indicating the number of the fcc reg to use.  */
	  v9_fcc_labelno[5] = REGNO (cc_reg) - SPARC_FIRST_V9_FCC_REG + '0';
	  labelno = v9_fcc_labelno;
	  if (v8)
	    {
	      gcc_assert (REGNO (cc_reg) == SPARC_FCC_REG);
	      labelno = "";
	    }
	}
      else if (mode == CCXmode || mode == CCX_NOOVmode)
	{
	  labelno = "%%xcc, ";
	  gcc_assert (! v8);
	}
      else
	{
	  labelno = "%%icc, ";
	  if (v8)
	    labelno = "";
	}

      if (*labelno && insn && (note = find_reg_note (insn, REG_BR_PROB, NULL_RTX)))
	{
	  strcpy (p,
		  ((INTVAL (XEXP (note, 0)) >= REG_BR_PROB_BASE / 2) ^ far)
		  ? ",pt" : ",pn");
	  p += 3;
	  spaces -= 3;
	}
    }
  else
    labelno = "";

  if (spaces > 0)
    *p++ = '\t';
  else
    *p++ = ' ';
  strcpy (p, labelno);
  p = strchr (p, '\0');
  if (far)
    {
      strcpy (p, ".+12\n\t nop\n\tb\t");
      /* Skip the next insn if requested or
	 if we know that it will be a nop.  */
      if (annul || ! final_sequence)
        p[3] = '6';
      p += 14;
    }
  *p++ = '%';
  *p++ = 'l';
  *p++ = label + '0';
  *p++ = '%';
  *p++ = '#';
  *p = '\0';

  return string;
}

/* Emit a library call comparison between floating point X and Y.
   COMPARISON is the rtl operator to compare with (EQ, NE, GT, etc.).
   TARGET_ARCH64 uses _Qp_* functions, which use pointers to TFmode
   values as arguments instead of the TFmode registers themselves,
   that's why we cannot call emit_float_lib_cmp.  */
void
sparc_emit_float_lib_cmp (rtx x, rtx y, enum rtx_code comparison)
{
  const char *qpfunc;
  rtx slot0, slot1, result, tem, tem2;
  enum machine_mode mode;

  switch (comparison)
    {
    case EQ:
      qpfunc = (TARGET_ARCH64) ? "_Qp_feq" : "_Q_feq";
      break;

    case NE:
      qpfunc = (TARGET_ARCH64) ? "_Qp_fne" : "_Q_fne";
      break;

    case GT:
      qpfunc = (TARGET_ARCH64) ? "_Qp_fgt" : "_Q_fgt";
      break;

    case GE:
      qpfunc = (TARGET_ARCH64) ? "_Qp_fge" : "_Q_fge";
      break;

    case LT:
      qpfunc = (TARGET_ARCH64) ? "_Qp_flt" : "_Q_flt";
      break;

    case LE:
      qpfunc = (TARGET_ARCH64) ? "_Qp_fle" : "_Q_fle";
      break;

    case ORDERED:
    case UNORDERED:
    case UNGT:
    case UNLT:
    case UNEQ:
    case UNGE:
    case UNLE:
    case LTGT:
      qpfunc = (TARGET_ARCH64) ? "_Qp_cmp" : "_Q_cmp";
      break;

    default:
      gcc_unreachable ();
    }

  if (TARGET_ARCH64)
    {
      if (GET_CODE (x) != MEM)
	{
	  slot0 = assign_stack_temp (TFmode, GET_MODE_SIZE(TFmode), 0);
	  emit_move_insn (slot0, x);
	}
      else
	slot0 = x;

      if (GET_CODE (y) != MEM)
	{
	  slot1 = assign_stack_temp (TFmode, GET_MODE_SIZE(TFmode), 0);
	  emit_move_insn (slot1, y);
	}
      else
	slot1 = y;

      emit_library_call (gen_rtx_SYMBOL_REF (Pmode, qpfunc), LCT_NORMAL,
			 DImode, 2,
			 XEXP (slot0, 0), Pmode,
			 XEXP (slot1, 0), Pmode);

      mode = DImode;
    }
  else
    {
      emit_library_call (gen_rtx_SYMBOL_REF (Pmode, qpfunc), LCT_NORMAL,
			 SImode, 2,
			 x, TFmode, y, TFmode);

      mode = SImode;
    }


  /* Immediately move the result of the libcall into a pseudo
     register so reload doesn't clobber the value if it needs
     the return register for a spill reg.  */
  result = gen_reg_rtx (mode);
  emit_move_insn (result, hard_libcall_value (mode));

  switch (comparison)
    {
    default:
      emit_cmp_insn (result, const0_rtx, NE, NULL_RTX, mode, 0);
      break;
    case ORDERED:
    case UNORDERED:
      emit_cmp_insn (result, GEN_INT(3), comparison == UNORDERED ? EQ : NE,
		     NULL_RTX, mode, 0);
      break;
    case UNGT:
    case UNGE:
      emit_cmp_insn (result, const1_rtx,
		     comparison == UNGT ? GT : NE, NULL_RTX, mode, 0);
      break;
    case UNLE:
      emit_cmp_insn (result, const2_rtx, NE, NULL_RTX, mode, 0);
      break;
    case UNLT:
      tem = gen_reg_rtx (mode);
      if (TARGET_ARCH32)
	emit_insn (gen_andsi3 (tem, result, const1_rtx));
      else
	emit_insn (gen_anddi3 (tem, result, const1_rtx));
      emit_cmp_insn (tem, const0_rtx, NE, NULL_RTX, mode, 0);
      break;
    case UNEQ:
    case LTGT:
      tem = gen_reg_rtx (mode);
      if (TARGET_ARCH32)
	emit_insn (gen_addsi3 (tem, result, const1_rtx));
      else
	emit_insn (gen_adddi3 (tem, result, const1_rtx));
      tem2 = gen_reg_rtx (mode);
      if (TARGET_ARCH32)
	emit_insn (gen_andsi3 (tem2, tem, const2_rtx));
      else
	emit_insn (gen_anddi3 (tem2, tem, const2_rtx));
      emit_cmp_insn (tem2, const0_rtx, comparison == UNEQ ? EQ : NE,
		     NULL_RTX, mode, 0);
      break;
    }
}

/* Generate an unsigned DImode to FP conversion.  This is the same code
   optabs would emit if we didn't have TFmode patterns.  */

void
sparc_emit_floatunsdi (rtx *operands, enum machine_mode mode)
{
  rtx neglab, donelab, i0, i1, f0, in, out;

  out = operands[0];
  in = force_reg (DImode, operands[1]);
  neglab = gen_label_rtx ();
  donelab = gen_label_rtx ();
  i0 = gen_reg_rtx (DImode);
  i1 = gen_reg_rtx (DImode);
  f0 = gen_reg_rtx (mode);

  emit_cmp_and_jump_insns (in, const0_rtx, LT, const0_rtx, DImode, 0, neglab);

  emit_insn (gen_rtx_SET (VOIDmode, out, gen_rtx_FLOAT (mode, in)));
  emit_jump_insn (gen_jump (donelab));
  emit_barrier ();

  emit_label (neglab);

  emit_insn (gen_lshrdi3 (i0, in, const1_rtx));
  emit_insn (gen_anddi3 (i1, in, const1_rtx));
  emit_insn (gen_iordi3 (i0, i0, i1));
  emit_insn (gen_rtx_SET (VOIDmode, f0, gen_rtx_FLOAT (mode, i0)));
  emit_insn (gen_rtx_SET (VOIDmode, out, gen_rtx_PLUS (mode, f0, f0)));

  emit_label (donelab);
}

/* Generate an FP to unsigned DImode conversion.  This is the same code
   optabs would emit if we didn't have TFmode patterns.  */

void
sparc_emit_fixunsdi (rtx *operands, enum machine_mode mode)
{
  rtx neglab, donelab, i0, i1, f0, in, out, limit;

  out = operands[0];
  in = force_reg (mode, operands[1]);
  neglab = gen_label_rtx ();
  donelab = gen_label_rtx ();
  i0 = gen_reg_rtx (DImode);
  i1 = gen_reg_rtx (DImode);
  limit = gen_reg_rtx (mode);
  f0 = gen_reg_rtx (mode);

  emit_move_insn (limit,
		  CONST_DOUBLE_FROM_REAL_VALUE (
		    REAL_VALUE_ATOF ("9223372036854775808.0", mode), mode));
  emit_cmp_and_jump_insns (in, limit, GE, NULL_RTX, mode, 0, neglab);

  emit_insn (gen_rtx_SET (VOIDmode,
			  out,
			  gen_rtx_FIX (DImode, gen_rtx_FIX (mode, in))));
  emit_jump_insn (gen_jump (donelab));
  emit_barrier ();

  emit_label (neglab);

  emit_insn (gen_rtx_SET (VOIDmode, f0, gen_rtx_MINUS (mode, in, limit)));
  emit_insn (gen_rtx_SET (VOIDmode,
			  i0,
			  gen_rtx_FIX (DImode, gen_rtx_FIX (mode, f0))));
  emit_insn (gen_movdi (i1, const1_rtx));
  emit_insn (gen_ashldi3 (i1, i1, GEN_INT (63)));
  emit_insn (gen_xordi3 (out, i0, i1));

  emit_label (donelab);
}

/* Return the string to output a conditional branch to LABEL, testing
   register REG.  LABEL is the operand number of the label; REG is the
   operand number of the reg.  OP is the conditional expression.  The mode
   of REG says what kind of comparison we made.

   DEST is the destination insn (i.e. the label), INSN is the source.

   REVERSED is nonzero if we should reverse the sense of the comparison.

   ANNUL is nonzero if we should generate an annulling branch.  */

const char *
output_v9branch (rtx op, rtx dest, int reg, int label, int reversed,
		 int annul, rtx insn)
{
  static char string[64];
  enum rtx_code code = GET_CODE (op);
  enum machine_mode mode = GET_MODE (XEXP (op, 0));
  rtx note;
  int far;
  char *p;

  /* branch on register are limited to +-128KB.  If it is too far away,
     change
     
     brnz,pt %g1, .LC30
     
     to
     
     brz,pn %g1, .+12
      nop
     ba,pt %xcc, .LC30
     
     and
     
     brgez,a,pn %o1, .LC29
     
     to
     
     brlz,pt %o1, .+16
      nop
     ba,pt %xcc, .LC29  */

  far = get_attr_length (insn) >= 3;

  /* If not floating-point or if EQ or NE, we can just reverse the code.  */
  if (reversed ^ far)
    code = reverse_condition (code);

  /* Only 64 bit versions of these instructions exist.  */
  gcc_assert (mode == DImode);

  /* Start by writing the branch condition.  */

  switch (code)
    {
    case NE:
      strcpy (string, "brnz");
      break;

    case EQ:
      strcpy (string, "brz");
      break;

    case GE:
      strcpy (string, "brgez");
      break;

    case LT:
      strcpy (string, "brlz");
      break;

    case LE:
      strcpy (string, "brlez");
      break;

    case GT:
      strcpy (string, "brgz");
      break;

    default:
      gcc_unreachable ();
    }

  p = strchr (string, '\0');

  /* Now add the annulling, reg, label, and nop.  */
  if (annul && ! far)
    {
      strcpy (p, ",a");
      p += 2;
    }

  if (insn && (note = find_reg_note (insn, REG_BR_PROB, NULL_RTX)))
    {
      strcpy (p,
	      ((INTVAL (XEXP (note, 0)) >= REG_BR_PROB_BASE / 2) ^ far)
	      ? ",pt" : ",pn");
      p += 3;
    }

  *p = p < string + 8 ? '\t' : ' ';
  p++;
  *p++ = '%';
  *p++ = '0' + reg;
  *p++ = ',';
  *p++ = ' ';
  if (far)
    {
      int veryfar = 1, delta;

      if (INSN_ADDRESSES_SET_P ())
	{
	  delta = (INSN_ADDRESSES (INSN_UID (dest))
		   - INSN_ADDRESSES (INSN_UID (insn)));
	  /* Leave some instructions for "slop".  */
	  if (delta >= -260000 && delta < 260000)
	    veryfar = 0;
	}

      strcpy (p, ".+12\n\t nop\n\t");
      /* Skip the next insn if requested or
	 if we know that it will be a nop.  */
      if (annul || ! final_sequence)
        p[3] = '6';
      p += 12;
      if (veryfar)
	{
	  strcpy (p, "b\t");
	  p += 2;
	}
      else
	{
	  strcpy (p, "ba,pt\t%%xcc, ");
	  p += 13;
	}
    }
  *p++ = '%';
  *p++ = 'l';
  *p++ = '0' + label;
  *p++ = '%';
  *p++ = '#';
  *p = '\0';

  return string;
}

/* Return 1, if any of the registers of the instruction are %l[0-7] or %o[0-7].
   Such instructions cannot be used in the delay slot of return insn on v9.
   If TEST is 0, also rename all %i[0-7] registers to their %o[0-7] counterparts.
 */

static int
epilogue_renumber (register rtx *where, int test)
{
  register const char *fmt;
  register int i;
  register enum rtx_code code;

  if (*where == 0)
    return 0;

  code = GET_CODE (*where);

  switch (code)
    {
    case REG:
      if (REGNO (*where) >= 8 && REGNO (*where) < 24)      /* oX or lX */
	return 1;
      if (! test && REGNO (*where) >= 24 && REGNO (*where) < 32)
	*where = gen_rtx_REG (GET_MODE (*where), OUTGOING_REGNO (REGNO(*where)));
    case SCRATCH:
    case CC0:
    case PC:
    case CONST_INT:
    case CONST_DOUBLE:
      return 0;

      /* Do not replace the frame pointer with the stack pointer because
	 it can cause the delayed instruction to load below the stack.
	 This occurs when instructions like:

	 (set (reg/i:SI 24 %i0)
	     (mem/f:SI (plus:SI (reg/f:SI 30 %fp)
                       (const_int -20 [0xffffffec])) 0))

	 are in the return delayed slot.  */
    case PLUS:
      if (GET_CODE (XEXP (*where, 0)) == REG
	  && REGNO (XEXP (*where, 0)) == HARD_FRAME_POINTER_REGNUM
	  && (GET_CODE (XEXP (*where, 1)) != CONST_INT
	      || INTVAL (XEXP (*where, 1)) < SPARC_STACK_BIAS))
	return 1;
      break;

    case MEM:
      if (SPARC_STACK_BIAS
	  && GET_CODE (XEXP (*where, 0)) == REG
	  && REGNO (XEXP (*where, 0)) == HARD_FRAME_POINTER_REGNUM)
	return 1;
      break;

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'E')
	{
	  register int j;
	  for (j = XVECLEN (*where, i) - 1; j >= 0; j--)
	    if (epilogue_renumber (&(XVECEXP (*where, i, j)), test))
	      return 1;
	}
      else if (fmt[i] == 'e'
	       && epilogue_renumber (&(XEXP (*where, i)), test))
	return 1;
    }
  return 0;
}

/* Leaf functions and non-leaf functions have different needs.  */

static const int
reg_leaf_alloc_order[] = REG_LEAF_ALLOC_ORDER;

static const int
reg_nonleaf_alloc_order[] = REG_ALLOC_ORDER;

static const int *const reg_alloc_orders[] = {
  reg_leaf_alloc_order,
  reg_nonleaf_alloc_order};

void
order_regs_for_local_alloc (void)
{
  static int last_order_nonleaf = 1;

  if (regs_ever_live[15] != last_order_nonleaf)
    {
      last_order_nonleaf = !last_order_nonleaf;
      memcpy ((char *) reg_alloc_order,
	      (const char *) reg_alloc_orders[last_order_nonleaf],
	      FIRST_PSEUDO_REGISTER * sizeof (int));
    }
}

/* Return 1 if REG and MEM are legitimate enough to allow the various
   mem<-->reg splits to be run.  */

int
sparc_splitdi_legitimate (rtx reg, rtx mem)
{
  /* Punt if we are here by mistake.  */
  gcc_assert (reload_completed);

  /* We must have an offsettable memory reference.  */
  if (! offsettable_memref_p (mem))
    return 0;

  /* If we have legitimate args for ldd/std, we do not want
     the split to happen.  */
  if ((REGNO (reg) % 2) == 0
      && mem_min_alignment (mem, 8))
    return 0;

  /* Success.  */
  return 1;
}

/* Return 1 if x and y are some kind of REG and they refer to
   different hard registers.  This test is guaranteed to be
   run after reload.  */

int
sparc_absnegfloat_split_legitimate (rtx x, rtx y)
{
  if (GET_CODE (x) != REG)
    return 0;
  if (GET_CODE (y) != REG)
    return 0;
  if (REGNO (x) == REGNO (y))
    return 0;
  return 1;
}

/* Return 1 if REGNO (reg1) is even and REGNO (reg1) == REGNO (reg2) - 1.
   This makes them candidates for using ldd and std insns. 

   Note reg1 and reg2 *must* be hard registers.  */

int
registers_ok_for_ldd_peep (rtx reg1, rtx reg2)
{
  /* We might have been passed a SUBREG.  */
  if (GET_CODE (reg1) != REG || GET_CODE (reg2) != REG) 
    return 0;

  if (REGNO (reg1) % 2 != 0)
    return 0;

  /* Integer ldd is deprecated in SPARC V9 */ 
  if (TARGET_V9 && REGNO (reg1) < 32)                  
    return 0;                             

  return (REGNO (reg1) == REGNO (reg2) - 1);
}

/* Return 1 if the addresses in mem1 and mem2 are suitable for use in
   an ldd or std insn.
   
   This can only happen when addr1 and addr2, the addresses in mem1
   and mem2, are consecutive memory locations (addr1 + 4 == addr2).
   addr1 must also be aligned on a 64-bit boundary.

   Also iff dependent_reg_rtx is not null it should not be used to
   compute the address for mem1, i.e. we cannot optimize a sequence
   like:
   	ld [%o0], %o0
	ld [%o0 + 4], %o1
   to
   	ldd [%o0], %o0
   nor:
	ld [%g3 + 4], %g3
	ld [%g3], %g2
   to
        ldd [%g3], %g2

   But, note that the transformation from:
	ld [%g2 + 4], %g3
        ld [%g2], %g2
   to
	ldd [%g2], %g2
   is perfectly fine.  Thus, the peephole2 patterns always pass us
   the destination register of the first load, never the second one.

   For stores we don't have a similar problem, so dependent_reg_rtx is
   NULL_RTX.  */

int
mems_ok_for_ldd_peep (rtx mem1, rtx mem2, rtx dependent_reg_rtx)
{
  rtx addr1, addr2;
  unsigned int reg1;
  HOST_WIDE_INT offset1;

  /* The mems cannot be volatile.  */
  if (MEM_VOLATILE_P (mem1) || MEM_VOLATILE_P (mem2))
    return 0;

  /* MEM1 should be aligned on a 64-bit boundary.  */
  if (MEM_ALIGN (mem1) < 64)
    return 0;
  
  addr1 = XEXP (mem1, 0);
  addr2 = XEXP (mem2, 0);
  
  /* Extract a register number and offset (if used) from the first addr.  */
  if (GET_CODE (addr1) == PLUS)
    {
      /* If not a REG, return zero.  */
      if (GET_CODE (XEXP (addr1, 0)) != REG)
	return 0;
      else
	{
          reg1 = REGNO (XEXP (addr1, 0));
	  /* The offset must be constant!  */
	  if (GET_CODE (XEXP (addr1, 1)) != CONST_INT)
            return 0;
          offset1 = INTVAL (XEXP (addr1, 1));
	}
    }
  else if (GET_CODE (addr1) != REG)
    return 0;
  else
    {
      reg1 = REGNO (addr1);
      /* This was a simple (mem (reg)) expression.  Offset is 0.  */
      offset1 = 0;
    }

  /* Make sure the second address is a (mem (plus (reg) (const_int).  */
  if (GET_CODE (addr2) != PLUS)
    return 0;

  if (GET_CODE (XEXP (addr2, 0)) != REG
      || GET_CODE (XEXP (addr2, 1)) != CONST_INT)
    return 0;

  if (reg1 != REGNO (XEXP (addr2, 0)))
    return 0;

  if (dependent_reg_rtx != NULL_RTX && reg1 == REGNO (dependent_reg_rtx))
    return 0;
  
  /* The first offset must be evenly divisible by 8 to ensure the 
     address is 64 bit aligned.  */
  if (offset1 % 8 != 0)
    return 0;

  /* The offset for the second addr must be 4 more than the first addr.  */
  if (INTVAL (XEXP (addr2, 1)) != offset1 + 4)
    return 0;

  /* All the tests passed.  addr1 and addr2 are valid for ldd and std
     instructions.  */
  return 1;
}

/* Return 1 if reg is a pseudo, or is the first register in 
   a hard register pair.  This makes it a candidate for use in
   ldd and std insns.  */

int
register_ok_for_ldd (rtx reg)
{
  /* We might have been passed a SUBREG.  */
  if (GET_CODE (reg) != REG) 
    return 0;

  if (REGNO (reg) < FIRST_PSEUDO_REGISTER)
    return (REGNO (reg) % 2 == 0);
  else 
    return 1;
}

/* Print operand X (an rtx) in assembler syntax to file FILE.
   CODE is a letter or dot (`z' in `%z0') or 0 if no letter was specified.
   For `%' followed by punctuation, CODE is the punctuation and X is null.  */

void
print_operand (FILE *file, rtx x, int code)
{
  switch (code)
    {
    case '#':
      /* Output an insn in a delay slot.  */
      if (final_sequence)
        sparc_indent_opcode = 1;
      else
	fputs ("\n\t nop", file);
      return;
    case '*':
      /* Output an annul flag if there's nothing for the delay slot and we
	 are optimizing.  This is always used with '(' below.
         Sun OS 4.1.1 dbx can't handle an annulled unconditional branch;
	 this is a dbx bug.  So, we only do this when optimizing.
         On UltraSPARC, a branch in a delay slot causes a pipeline flush.
	 Always emit a nop in case the next instruction is a branch.  */
      if (! final_sequence && (optimize && (int)sparc_cpu < PROCESSOR_V9))
	fputs (",a", file);
      return;
    case '(':
      /* Output a 'nop' if there's nothing for the delay slot and we are
	 not optimizing.  This is always used with '*' above.  */
      if (! final_sequence && ! (optimize && (int)sparc_cpu < PROCESSOR_V9))
	fputs ("\n\t nop", file);
      else if (final_sequence)
        sparc_indent_opcode = 1;
      return;
    case ')':
      /* Output the right displacement from the saved PC on function return.
	 The caller may have placed an "unimp" insn immediately after the call
	 so we have to account for it.  This insn is used in the 32-bit ABI
	 when calling a function that returns a non zero-sized structure. The
	 64-bit ABI doesn't have it.  Be careful to have this test be the same
	 as that used on the call. The exception here is that when 
	 sparc_std_struct_return is enabled, the psABI is followed exactly
	 and the adjustment is made by the code in sparc_struct_value_rtx. 
	 The call emitted is the same when sparc_std_struct_return is 
	 present. */
     if (! TARGET_ARCH64
	 && current_function_returns_struct
	 && ! sparc_std_struct_return
	 && (TREE_CODE (DECL_SIZE (DECL_RESULT (current_function_decl)))
	     == INTEGER_CST)
	 && ! integer_zerop (DECL_SIZE (DECL_RESULT (current_function_decl))))
	fputs ("12", file);
      else
        fputc ('8', file);
      return;
    case '_':
      /* Output the Embedded Medium/Anywhere code model base register.  */
      fputs (EMBMEDANY_BASE_REG, file);
      return;
    case '&':
      /* Print some local dynamic TLS name.  */
      assemble_name (file, get_some_local_dynamic_name ());
      return;

    case 'Y':
      /* Adjust the operand to take into account a RESTORE operation.  */
      if (GET_CODE (x) == CONST_INT)
	break;
      else if (GET_CODE (x) != REG)
	output_operand_lossage ("invalid %%Y operand");
      else if (REGNO (x) < 8)
	fputs (reg_names[REGNO (x)], file);
      else if (REGNO (x) >= 24 && REGNO (x) < 32)
	fputs (reg_names[REGNO (x)-16], file);
      else
	output_operand_lossage ("invalid %%Y operand");
      return;
    case 'L':
      /* Print out the low order register name of a register pair.  */
      if (WORDS_BIG_ENDIAN)
	fputs (reg_names[REGNO (x)+1], file);
      else
	fputs (reg_names[REGNO (x)], file);
      return;
    case 'H':
      /* Print out the high order register name of a register pair.  */
      if (WORDS_BIG_ENDIAN)
	fputs (reg_names[REGNO (x)], file);
      else
	fputs (reg_names[REGNO (x)+1], file);
      return;
    case 'R':
      /* Print out the second register name of a register pair or quad.
	 I.e., R (%o0) => %o1.  */
      fputs (reg_names[REGNO (x)+1], file);
      return;
    case 'S':
      /* Print out the third register name of a register quad.
	 I.e., S (%o0) => %o2.  */
      fputs (reg_names[REGNO (x)+2], file);
      return;
    case 'T':
      /* Print out the fourth register name of a register quad.
	 I.e., T (%o0) => %o3.  */
      fputs (reg_names[REGNO (x)+3], file);
      return;
    case 'x':
      /* Print a condition code register.  */
      if (REGNO (x) == SPARC_ICC_REG)
	{
	  /* We don't handle CC[X]_NOOVmode because they're not supposed
	     to occur here.  */
	  if (GET_MODE (x) == CCmode)
	    fputs ("%icc", file);
	  else if (GET_MODE (x) == CCXmode)
	    fputs ("%xcc", file);
	  else
	    gcc_unreachable ();
	}
      else
	/* %fccN register */
	fputs (reg_names[REGNO (x)], file);
      return;
    case 'm':
      /* Print the operand's address only.  */
      output_address (XEXP (x, 0));
      return;
    case 'r':
      /* In this case we need a register.  Use %g0 if the
	 operand is const0_rtx.  */
      if (x == const0_rtx
	  || (GET_MODE (x) != VOIDmode && x == CONST0_RTX (GET_MODE (x))))
	{
	  fputs ("%g0", file);
	  return;
	}
      else
	break;

    case 'A':
      switch (GET_CODE (x))
	{
	case IOR: fputs ("or", file); break;
	case AND: fputs ("and", file); break;
	case XOR: fputs ("xor", file); break;
	default: output_operand_lossage ("invalid %%A operand");
	}
      return;

    case 'B':
      switch (GET_CODE (x))
	{
	case IOR: fputs ("orn", file); break;
	case AND: fputs ("andn", file); break;
	case XOR: fputs ("xnor", file); break;
	default: output_operand_lossage ("invalid %%B operand");
	}
      return;

      /* These are used by the conditional move instructions.  */
    case 'c' :
    case 'C':
      {
	enum rtx_code rc = GET_CODE (x);
	
	if (code == 'c')
	  {
	    enum machine_mode mode = GET_MODE (XEXP (x, 0));
	    if (mode == CCFPmode || mode == CCFPEmode)
	      rc = reverse_condition_maybe_unordered (GET_CODE (x));
	    else
	      rc = reverse_condition (GET_CODE (x));
	  }
	switch (rc)
	  {
	  case NE: fputs ("ne", file); break;
	  case EQ: fputs ("e", file); break;
	  case GE: fputs ("ge", file); break;
	  case GT: fputs ("g", file); break;
	  case LE: fputs ("le", file); break;
	  case LT: fputs ("l", file); break;
	  case GEU: fputs ("geu", file); break;
	  case GTU: fputs ("gu", file); break;
	  case LEU: fputs ("leu", file); break;
	  case LTU: fputs ("lu", file); break;
	  case LTGT: fputs ("lg", file); break;
	  case UNORDERED: fputs ("u", file); break;
	  case ORDERED: fputs ("o", file); break;
	  case UNLT: fputs ("ul", file); break;
	  case UNLE: fputs ("ule", file); break;
	  case UNGT: fputs ("ug", file); break;
	  case UNGE: fputs ("uge", file); break;
	  case UNEQ: fputs ("ue", file); break;
	  default: output_operand_lossage (code == 'c'
					   ? "invalid %%c operand"
					   : "invalid %%C operand");
	  }
	return;
      }

      /* These are used by the movr instruction pattern.  */
    case 'd':
    case 'D':
      {
	enum rtx_code rc = (code == 'd'
			    ? reverse_condition (GET_CODE (x))
			    : GET_CODE (x));
	switch (rc)
	  {
	  case NE: fputs ("ne", file); break;
	  case EQ: fputs ("e", file); break;
	  case GE: fputs ("gez", file); break;
	  case LT: fputs ("lz", file); break;
	  case LE: fputs ("lez", file); break;
	  case GT: fputs ("gz", file); break;
	  default: output_operand_lossage (code == 'd'
					   ? "invalid %%d operand"
					   : "invalid %%D operand");
	  }
	return;
      }

    case 'b':
      {
	/* Print a sign-extended character.  */
	int i = trunc_int_for_mode (INTVAL (x), QImode);
	fprintf (file, "%d", i);
	return;
      }

    case 'f':
      /* Operand must be a MEM; write its address.  */
      if (GET_CODE (x) != MEM)
	output_operand_lossage ("invalid %%f operand");
      output_address (XEXP (x, 0));
      return;

    case 's':
      {
	/* Print a sign-extended 32-bit value.  */
	HOST_WIDE_INT i;
	if (GET_CODE(x) == CONST_INT)
	  i = INTVAL (x);
	else if (GET_CODE(x) == CONST_DOUBLE)
	  i = CONST_DOUBLE_LOW (x);
	else
	  {
	    output_operand_lossage ("invalid %%s operand");
	    return;
	  }
	i = trunc_int_for_mode (i, SImode);
	fprintf (file, HOST_WIDE_INT_PRINT_DEC, i);
	return;
      }

    case 0:
      /* Do nothing special.  */
      break;

    default:
      /* Undocumented flag.  */
      output_operand_lossage ("invalid operand output code");
    }

  if (GET_CODE (x) == REG)
    fputs (reg_names[REGNO (x)], file);
  else if (GET_CODE (x) == MEM)
    {
      fputc ('[', file);
	/* Poor Sun assembler doesn't understand absolute addressing.  */
      if (CONSTANT_P (XEXP (x, 0)))
	fputs ("%g0+", file);
      output_address (XEXP (x, 0));
      fputc (']', file);
    }
  else if (GET_CODE (x) == HIGH)
    {
      fputs ("%hi(", file);
      output_addr_const (file, XEXP (x, 0));
      fputc (')', file);
    }
  else if (GET_CODE (x) == LO_SUM)
    {
      print_operand (file, XEXP (x, 0), 0);
      if (TARGET_CM_MEDMID)
	fputs ("+%l44(", file);
      else
	fputs ("+%lo(", file);
      output_addr_const (file, XEXP (x, 1));
      fputc (')', file);
    }
  else if (GET_CODE (x) == CONST_DOUBLE
	   && (GET_MODE (x) == VOIDmode
	       || GET_MODE_CLASS (GET_MODE (x)) == MODE_INT))
    {
      if (CONST_DOUBLE_HIGH (x) == 0)
	fprintf (file, "%u", (unsigned int) CONST_DOUBLE_LOW (x));
      else if (CONST_DOUBLE_HIGH (x) == -1
	       && CONST_DOUBLE_LOW (x) < 0)
	fprintf (file, "%d", (int) CONST_DOUBLE_LOW (x));
      else
	output_operand_lossage ("long long constant not a valid immediate operand");
    }
  else if (GET_CODE (x) == CONST_DOUBLE)
    output_operand_lossage ("floating point constant not a valid immediate operand");
  else { output_addr_const (file, x); }
}

/* Target hook for assembling integer objects.  The sparc version has
   special handling for aligned DI-mode objects.  */

static bool
sparc_assemble_integer (rtx x, unsigned int size, int aligned_p)
{
  /* ??? We only output .xword's for symbols and only then in environments
     where the assembler can handle them.  */
  if (aligned_p && size == 8
      && (GET_CODE (x) != CONST_INT && GET_CODE (x) != CONST_DOUBLE))
    {
      if (TARGET_V9)
	{
	  assemble_integer_with_op ("\t.xword\t", x);
	  return true;
	}
      else
	{
	  assemble_aligned_integer (4, const0_rtx);
	  assemble_aligned_integer (4, x);
	  return true;
	}
    }
  return default_assemble_integer (x, size, aligned_p);
}

/* Return the value of a code used in the .proc pseudo-op that says
   what kind of result this function returns.  For non-C types, we pick
   the closest C type.  */

#ifndef SHORT_TYPE_SIZE
#define SHORT_TYPE_SIZE (BITS_PER_UNIT * 2)
#endif

#ifndef INT_TYPE_SIZE
#define INT_TYPE_SIZE BITS_PER_WORD
#endif

#ifndef LONG_TYPE_SIZE
#define LONG_TYPE_SIZE BITS_PER_WORD
#endif

#ifndef LONG_LONG_TYPE_SIZE
#define LONG_LONG_TYPE_SIZE (BITS_PER_WORD * 2)
#endif

#ifndef FLOAT_TYPE_SIZE
#define FLOAT_TYPE_SIZE BITS_PER_WORD
#endif

#ifndef DOUBLE_TYPE_SIZE
#define DOUBLE_TYPE_SIZE (BITS_PER_WORD * 2)
#endif

#ifndef LONG_DOUBLE_TYPE_SIZE
#define LONG_DOUBLE_TYPE_SIZE (BITS_PER_WORD * 2)
#endif

unsigned long
sparc_type_code (register tree type)
{
  register unsigned long qualifiers = 0;
  register unsigned shift;

  /* Only the first 30 bits of the qualifier are valid.  We must refrain from
     setting more, since some assemblers will give an error for this.  Also,
     we must be careful to avoid shifts of 32 bits or more to avoid getting
     unpredictable results.  */

  for (shift = 6; shift < 30; shift += 2, type = TREE_TYPE (type))
    {
      switch (TREE_CODE (type))
	{
	case ERROR_MARK:
	  return qualifiers;
  
	case ARRAY_TYPE:
	  qualifiers |= (3 << shift);
	  break;

	case FUNCTION_TYPE:
	case METHOD_TYPE:
	  qualifiers |= (2 << shift);
	  break;

	case POINTER_TYPE:
	case REFERENCE_TYPE:
	case OFFSET_TYPE:
	  qualifiers |= (1 << shift);
	  break;

	case RECORD_TYPE:
	  return (qualifiers | 8);

	case UNION_TYPE:
	case QUAL_UNION_TYPE:
	  return (qualifiers | 9);

	case ENUMERAL_TYPE:
	  return (qualifiers | 10);

	case VOID_TYPE:
	  return (qualifiers | 16);

	case INTEGER_TYPE:
	  /* If this is a range type, consider it to be the underlying
	     type.  */
	  if (TREE_TYPE (type) != 0)
	    break;

	  /* Carefully distinguish all the standard types of C,
	     without messing up if the language is not C.  We do this by
	     testing TYPE_PRECISION and TYPE_UNSIGNED.  The old code used to
	     look at both the names and the above fields, but that's redundant.
	     Any type whose size is between two C types will be considered
	     to be the wider of the two types.  Also, we do not have a
	     special code to use for "long long", so anything wider than
	     long is treated the same.  Note that we can't distinguish
	     between "int" and "long" in this code if they are the same
	     size, but that's fine, since neither can the assembler.  */

	  if (TYPE_PRECISION (type) <= CHAR_TYPE_SIZE)
	    return (qualifiers | (TYPE_UNSIGNED (type) ? 12 : 2));
  
	  else if (TYPE_PRECISION (type) <= SHORT_TYPE_SIZE)
	    return (qualifiers | (TYPE_UNSIGNED (type) ? 13 : 3));
  
	  else if (TYPE_PRECISION (type) <= INT_TYPE_SIZE)
	    return (qualifiers | (TYPE_UNSIGNED (type) ? 14 : 4));
  
	  else
	    return (qualifiers | (TYPE_UNSIGNED (type) ? 15 : 5));
  
	case REAL_TYPE:
	  /* If this is a range type, consider it to be the underlying
	     type.  */
	  if (TREE_TYPE (type) != 0)
	    break;

	  /* Carefully distinguish all the standard types of C,
	     without messing up if the language is not C.  */

	  if (TYPE_PRECISION (type) == FLOAT_TYPE_SIZE)
	    return (qualifiers | 6);

	  else 
	    return (qualifiers | 7);
  
	case COMPLEX_TYPE:	/* GNU Fortran COMPLEX type.  */
	  /* ??? We need to distinguish between double and float complex types,
	     but I don't know how yet because I can't reach this code from
	     existing front-ends.  */
	  return (qualifiers | 7);	/* Who knows? */

	case VECTOR_TYPE:
	case BOOLEAN_TYPE:	/* Boolean truth value type.  */
	case LANG_TYPE:		/* ? */
	  return qualifiers;
  
	default:
	  gcc_unreachable ();		/* Not a type! */
        }
    }

  return qualifiers;
}

/* Nested function support.  */

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.

   This takes 16 insns: 2 shifts & 2 ands (to split up addresses), 4 sethi
   (to load in opcodes), 4 iors (to merge address and opcodes), and 4 writes
   (to store insns).  This is a bit excessive.  Perhaps a different
   mechanism would be better here.

   Emit enough FLUSH insns to synchronize the data and instruction caches.  */

void
sparc_initialize_trampoline (rtx tramp, rtx fnaddr, rtx cxt)
{
  /* SPARC 32-bit trampoline:

 	sethi	%hi(fn), %g1
 	sethi	%hi(static), %g2
 	jmp	%g1+%lo(fn)
 	or	%g2, %lo(static), %g2

    SETHI i,r  = 00rr rrr1 00ii iiii iiii iiii iiii iiii
    JMPL r+i,d = 10dd ddd1 1100 0rrr rr1i iiii iiii iiii
   */

  emit_move_insn
    (gen_rtx_MEM (SImode, plus_constant (tramp, 0)),
     expand_binop (SImode, ior_optab,
		   expand_shift (RSHIFT_EXPR, SImode, fnaddr,
				 size_int (10), 0, 1),
		   GEN_INT (trunc_int_for_mode (0x03000000, SImode)),
		   NULL_RTX, 1, OPTAB_DIRECT));

  emit_move_insn
    (gen_rtx_MEM (SImode, plus_constant (tramp, 4)),
     expand_binop (SImode, ior_optab,
		   expand_shift (RSHIFT_EXPR, SImode, cxt,
				 size_int (10), 0, 1),
		   GEN_INT (trunc_int_for_mode (0x05000000, SImode)),
		   NULL_RTX, 1, OPTAB_DIRECT));

  emit_move_insn
    (gen_rtx_MEM (SImode, plus_constant (tramp, 8)),
     expand_binop (SImode, ior_optab,
		   expand_and (SImode, fnaddr, GEN_INT (0x3ff), NULL_RTX),
		   GEN_INT (trunc_int_for_mode (0x81c06000, SImode)),
		   NULL_RTX, 1, OPTAB_DIRECT));

  emit_move_insn
    (gen_rtx_MEM (SImode, plus_constant (tramp, 12)),
     expand_binop (SImode, ior_optab,
		   expand_and (SImode, cxt, GEN_INT (0x3ff), NULL_RTX),
		   GEN_INT (trunc_int_for_mode (0x8410a000, SImode)),
		   NULL_RTX, 1, OPTAB_DIRECT));

  /* On UltraSPARC a flush flushes an entire cache line.  The trampoline is
     aligned on a 16 byte boundary so one flush clears it all.  */
  emit_insn (gen_flush (validize_mem (gen_rtx_MEM (SImode, tramp))));
  if (sparc_cpu != PROCESSOR_ULTRASPARC
      && sparc_cpu != PROCESSOR_ULTRASPARC3
      && sparc_cpu != PROCESSOR_NIAGARA)
    emit_insn (gen_flush (validize_mem (gen_rtx_MEM (SImode,
						     plus_constant (tramp, 8)))));

  /* Call __enable_execute_stack after writing onto the stack to make sure
     the stack address is accessible.  */
#ifdef ENABLE_EXECUTE_STACK
  emit_library_call (gen_rtx_SYMBOL_REF (Pmode, "__enable_execute_stack"),
                     LCT_NORMAL, VOIDmode, 1, tramp, Pmode);
#endif

}

/* The 64-bit version is simpler because it makes more sense to load the
   values as "immediate" data out of the trampoline.  It's also easier since
   we can read the PC without clobbering a register.  */

void
sparc64_initialize_trampoline (rtx tramp, rtx fnaddr, rtx cxt)
{
  /* SPARC 64-bit trampoline:

	rd	%pc, %g1
	ldx	[%g1+24], %g5
	jmp	%g5
	ldx	[%g1+16], %g5
	+16 bytes data
   */

  emit_move_insn (gen_rtx_MEM (SImode, tramp),
		  GEN_INT (trunc_int_for_mode (0x83414000, SImode)));
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (tramp, 4)),
		  GEN_INT (trunc_int_for_mode (0xca586018, SImode)));
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (tramp, 8)),
		  GEN_INT (trunc_int_for_mode (0x81c14000, SImode)));
  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (tramp, 12)),
		  GEN_INT (trunc_int_for_mode (0xca586010, SImode)));
  emit_move_insn (gen_rtx_MEM (DImode, plus_constant (tramp, 16)), cxt);
  emit_move_insn (gen_rtx_MEM (DImode, plus_constant (tramp, 24)), fnaddr);
  emit_insn (gen_flushdi (validize_mem (gen_rtx_MEM (DImode, tramp))));

  if (sparc_cpu != PROCESSOR_ULTRASPARC
      && sparc_cpu != PROCESSOR_ULTRASPARC3
      && sparc_cpu != PROCESSOR_NIAGARA)
    emit_insn (gen_flushdi (validize_mem (gen_rtx_MEM (DImode, plus_constant (tramp, 8)))));

  /* Call __enable_execute_stack after writing onto the stack to make sure
     the stack address is accessible.  */
#ifdef ENABLE_EXECUTE_STACK
  emit_library_call (gen_rtx_SYMBOL_REF (Pmode, "__enable_execute_stack"),
                     LCT_NORMAL, VOIDmode, 1, tramp, Pmode);
#endif
}

/* Adjust the cost of a scheduling dependency.  Return the new cost of
   a dependency LINK or INSN on DEP_INSN.  COST is the current cost.  */

static int
supersparc_adjust_cost (rtx insn, rtx link, rtx dep_insn, int cost)
{
  enum attr_type insn_type;

  if (! recog_memoized (insn))
    return 0;

  insn_type = get_attr_type (insn);

  if (REG_NOTE_KIND (link) == 0)
    {
      /* Data dependency; DEP_INSN writes a register that INSN reads some
	 cycles later.  */

      /* if a load, then the dependence must be on the memory address;
	 add an extra "cycle".  Note that the cost could be two cycles
	 if the reg was written late in an instruction group; we ca not tell
	 here.  */
      if (insn_type == TYPE_LOAD || insn_type == TYPE_FPLOAD)
	return cost + 3;

      /* Get the delay only if the address of the store is the dependence.  */
      if (insn_type == TYPE_STORE || insn_type == TYPE_FPSTORE)
	{
	  rtx pat = PATTERN(insn);
	  rtx dep_pat = PATTERN (dep_insn);

	  if (GET_CODE (pat) != SET || GET_CODE (dep_pat) != SET)
	    return cost;  /* This should not happen!  */

	  /* The dependency between the two instructions was on the data that
	     is being stored.  Assume that this implies that the address of the
	     store is not dependent.  */
	  if (rtx_equal_p (SET_DEST (dep_pat), SET_SRC (pat)))
	    return cost;

	  return cost + 3;  /* An approximation.  */
	}

      /* A shift instruction cannot receive its data from an instruction
	 in the same cycle; add a one cycle penalty.  */
      if (insn_type == TYPE_SHIFT)
	return cost + 3;   /* Split before cascade into shift.  */
    }
  else
    {
      /* Anti- or output- dependency; DEP_INSN reads/writes a register that
	 INSN writes some cycles later.  */

      /* These are only significant for the fpu unit; writing a fp reg before
         the fpu has finished with it stalls the processor.  */

      /* Reusing an integer register causes no problems.  */
      if (insn_type == TYPE_IALU || insn_type == TYPE_SHIFT)
	return 0;
    }
	
  return cost;
}

static int
hypersparc_adjust_cost (rtx insn, rtx link, rtx dep_insn, int cost)
{
  enum attr_type insn_type, dep_type;
  rtx pat = PATTERN(insn);
  rtx dep_pat = PATTERN (dep_insn);

  if (recog_memoized (insn) < 0 || recog_memoized (dep_insn) < 0)
    return cost;

  insn_type = get_attr_type (insn);
  dep_type = get_attr_type (dep_insn);

  switch (REG_NOTE_KIND (link))
    {
    case 0:
      /* Data dependency; DEP_INSN writes a register that INSN reads some
	 cycles later.  */

      switch (insn_type)
	{
	case TYPE_STORE:
	case TYPE_FPSTORE:
	  /* Get the delay iff the address of the store is the dependence.  */
	  if (GET_CODE (pat) != SET || GET_CODE (dep_pat) != SET)
	    return cost;

	  if (rtx_equal_p (SET_DEST (dep_pat), SET_SRC (pat)))
	    return cost;
	  return cost + 3;

	case TYPE_LOAD:
	case TYPE_SLOAD:
	case TYPE_FPLOAD:
	  /* If a load, then the dependence must be on the memory address.  If
	     the addresses aren't equal, then it might be a false dependency */
	  if (dep_type == TYPE_STORE || dep_type == TYPE_FPSTORE)
	    {
	      if (GET_CODE (pat) != SET || GET_CODE (dep_pat) != SET
		  || GET_CODE (SET_DEST (dep_pat)) != MEM        
		  || GET_CODE (SET_SRC (pat)) != MEM
		  || ! rtx_equal_p (XEXP (SET_DEST (dep_pat), 0),
				    XEXP (SET_SRC (pat), 0)))
		return cost + 2;

	      return cost + 8;        
	    }
	  break;

	case TYPE_BRANCH:
	  /* Compare to branch latency is 0.  There is no benefit from
	     separating compare and branch.  */
	  if (dep_type == TYPE_COMPARE)
	    return 0;
	  /* Floating point compare to branch latency is less than
	     compare to conditional move.  */
	  if (dep_type == TYPE_FPCMP)
	    return cost - 1;
	  break;
	default:
	  break;
	}
	break;

    case REG_DEP_ANTI:
      /* Anti-dependencies only penalize the fpu unit.  */
      if (insn_type == TYPE_IALU || insn_type == TYPE_SHIFT)
        return 0;
      break;

    default:
      break;
    }    

  return cost;
}

static int
sparc_adjust_cost(rtx insn, rtx link, rtx dep, int cost)
{
  switch (sparc_cpu)
    {
    case PROCESSOR_SUPERSPARC:
      cost = supersparc_adjust_cost (insn, link, dep, cost);
      break;
    case PROCESSOR_HYPERSPARC:
    case PROCESSOR_SPARCLITE86X:
      cost = hypersparc_adjust_cost (insn, link, dep, cost);
      break;
    default:
      break;
    }
  return cost;
}

static void
sparc_sched_init (FILE *dump ATTRIBUTE_UNUSED,
		  int sched_verbose ATTRIBUTE_UNUSED,
		  int max_ready ATTRIBUTE_UNUSED)
{
}
  
static int
sparc_use_sched_lookahead (void)
{
  if (sparc_cpu == PROCESSOR_NIAGARA)
    return 0;
  if (sparc_cpu == PROCESSOR_ULTRASPARC
      || sparc_cpu == PROCESSOR_ULTRASPARC3)
    return 4;
  if ((1 << sparc_cpu) &
      ((1 << PROCESSOR_SUPERSPARC) | (1 << PROCESSOR_HYPERSPARC) |
       (1 << PROCESSOR_SPARCLITE86X)))
    return 3;
  return 0;
}

static int
sparc_issue_rate (void)
{
  switch (sparc_cpu)
    {
    case PROCESSOR_NIAGARA:
    default:
      return 1;
    case PROCESSOR_V9:
      /* Assume V9 processors are capable of at least dual-issue.  */
      return 2;
    case PROCESSOR_SUPERSPARC:
      return 3;
    case PROCESSOR_HYPERSPARC:
    case PROCESSOR_SPARCLITE86X:
      return 2;
    case PROCESSOR_ULTRASPARC:
    case PROCESSOR_ULTRASPARC3:
      return 4;
    }
}

static int
set_extends (rtx insn)
{
  register rtx pat = PATTERN (insn);

  switch (GET_CODE (SET_SRC (pat)))
    {
      /* Load and some shift instructions zero extend.  */
    case MEM:
    case ZERO_EXTEND:
      /* sethi clears the high bits */
    case HIGH:
      /* LO_SUM is used with sethi.  sethi cleared the high
	 bits and the values used with lo_sum are positive */
    case LO_SUM:
      /* Store flag stores 0 or 1 */
    case LT: case LTU:
    case GT: case GTU:
    case LE: case LEU:
    case GE: case GEU:
    case EQ:
    case NE:
      return 1;
    case AND:
      {
	rtx op0 = XEXP (SET_SRC (pat), 0);
	rtx op1 = XEXP (SET_SRC (pat), 1);
	if (GET_CODE (op1) == CONST_INT)
	  return INTVAL (op1) >= 0;
	if (GET_CODE (op0) != REG)
	  return 0;
	if (sparc_check_64 (op0, insn) == 1)
	  return 1;
	return (GET_CODE (op1) == REG && sparc_check_64 (op1, insn) == 1);
      }
    case IOR:
    case XOR:
      {
	rtx op0 = XEXP (SET_SRC (pat), 0);
	rtx op1 = XEXP (SET_SRC (pat), 1);
	if (GET_CODE (op0) != REG || sparc_check_64 (op0, insn) <= 0)
	  return 0;
	if (GET_CODE (op1) == CONST_INT)
	  return INTVAL (op1) >= 0;
	return (GET_CODE (op1) == REG && sparc_check_64 (op1, insn) == 1);
      }
    case LSHIFTRT:
      return GET_MODE (SET_SRC (pat)) == SImode;
      /* Positive integers leave the high bits zero.  */
    case CONST_DOUBLE:
      return ! (CONST_DOUBLE_LOW (SET_SRC (pat)) & 0x80000000);
    case CONST_INT:
      return ! (INTVAL (SET_SRC (pat)) & 0x80000000);
    case ASHIFTRT:
    case SIGN_EXTEND:
      return - (GET_MODE (SET_SRC (pat)) == SImode);
    case REG:
      return sparc_check_64 (SET_SRC (pat), insn);
    default:
      return 0;
    }
}

/* We _ought_ to have only one kind per function, but...  */
static GTY(()) rtx sparc_addr_diff_list;
static GTY(()) rtx sparc_addr_list;

void
sparc_defer_case_vector (rtx lab, rtx vec, int diff)
{
  vec = gen_rtx_EXPR_LIST (VOIDmode, lab, vec);
  if (diff)
    sparc_addr_diff_list
      = gen_rtx_EXPR_LIST (VOIDmode, vec, sparc_addr_diff_list);
  else
    sparc_addr_list = gen_rtx_EXPR_LIST (VOIDmode, vec, sparc_addr_list);
}

static void 
sparc_output_addr_vec (rtx vec)
{
  rtx lab = XEXP (vec, 0), body = XEXP (vec, 1);
  int idx, vlen = XVECLEN (body, 0);

#ifdef ASM_OUTPUT_ADDR_VEC_START  
  ASM_OUTPUT_ADDR_VEC_START (asm_out_file);
#endif

#ifdef ASM_OUTPUT_CASE_LABEL
  ASM_OUTPUT_CASE_LABEL (asm_out_file, "L", CODE_LABEL_NUMBER (lab),
			 NEXT_INSN (lab));
#else
  (*targetm.asm_out.internal_label) (asm_out_file, "L", CODE_LABEL_NUMBER (lab));
#endif

  for (idx = 0; idx < vlen; idx++)
    {
      ASM_OUTPUT_ADDR_VEC_ELT
	(asm_out_file, CODE_LABEL_NUMBER (XEXP (XVECEXP (body, 0, idx), 0)));
    }
    
#ifdef ASM_OUTPUT_ADDR_VEC_END
  ASM_OUTPUT_ADDR_VEC_END (asm_out_file);
#endif
}

static void 
sparc_output_addr_diff_vec (rtx vec)
{
  rtx lab = XEXP (vec, 0), body = XEXP (vec, 1);
  rtx base = XEXP (XEXP (body, 0), 0);
  int idx, vlen = XVECLEN (body, 1);

#ifdef ASM_OUTPUT_ADDR_VEC_START  
  ASM_OUTPUT_ADDR_VEC_START (asm_out_file);
#endif

#ifdef ASM_OUTPUT_CASE_LABEL
  ASM_OUTPUT_CASE_LABEL (asm_out_file, "L", CODE_LABEL_NUMBER (lab),
			 NEXT_INSN (lab));
#else
  (*targetm.asm_out.internal_label) (asm_out_file, "L", CODE_LABEL_NUMBER (lab));
#endif

  for (idx = 0; idx < vlen; idx++)
    {
      ASM_OUTPUT_ADDR_DIFF_ELT
        (asm_out_file,
         body,
         CODE_LABEL_NUMBER (XEXP (XVECEXP (body, 1, idx), 0)),
         CODE_LABEL_NUMBER (base));
    }
    
#ifdef ASM_OUTPUT_ADDR_VEC_END
  ASM_OUTPUT_ADDR_VEC_END (asm_out_file);
#endif
}

static void
sparc_output_deferred_case_vectors (void)
{
  rtx t;
  int align;

  if (sparc_addr_list == NULL_RTX
      && sparc_addr_diff_list == NULL_RTX)
    return;

  /* Align to cache line in the function's code section.  */
  switch_to_section (current_function_section ());

  align = floor_log2 (FUNCTION_BOUNDARY / BITS_PER_UNIT);
  if (align > 0)
    ASM_OUTPUT_ALIGN (asm_out_file, align);
  
  for (t = sparc_addr_list; t ; t = XEXP (t, 1))
    sparc_output_addr_vec (XEXP (t, 0));
  for (t = sparc_addr_diff_list; t ; t = XEXP (t, 1))
    sparc_output_addr_diff_vec (XEXP (t, 0));

  sparc_addr_list = sparc_addr_diff_list = NULL_RTX;
}

/* Return 0 if the high 32 bits of X (the low word of X, if DImode) are
   unknown.  Return 1 if the high bits are zero, -1 if the register is
   sign extended.  */
int
sparc_check_64 (rtx x, rtx insn)
{
  /* If a register is set only once it is safe to ignore insns this
     code does not know how to handle.  The loop will either recognize
     the single set and return the correct value or fail to recognize
     it and return 0.  */
  int set_once = 0;
  rtx y = x;

  gcc_assert (GET_CODE (x) == REG);

  if (GET_MODE (x) == DImode)
    y = gen_rtx_REG (SImode, REGNO (x) + WORDS_BIG_ENDIAN);

  if (flag_expensive_optimizations
      && REG_N_SETS (REGNO (y)) == 1)
    set_once = 1;

  if (insn == 0)
    {
      if (set_once)
	insn = get_last_insn_anywhere ();
      else
	return 0;
    }

  while ((insn = PREV_INSN (insn)))
    {
      switch (GET_CODE (insn))
	{
	case JUMP_INSN:
	case NOTE:
	  break;
	case CODE_LABEL:
	case CALL_INSN:
	default:
	  if (! set_once)
	    return 0;
	  break;
	case INSN:
	  {
	    rtx pat = PATTERN (insn);
	    if (GET_CODE (pat) != SET)
	      return 0;
	    if (rtx_equal_p (x, SET_DEST (pat)))
	      return set_extends (insn);
	    if (y && rtx_equal_p (y, SET_DEST (pat)))
	      return set_extends (insn);
	    if (reg_overlap_mentioned_p (SET_DEST (pat), y))
	      return 0;
	  }
	}
    }
  return 0;
}

/* Returns assembly code to perform a DImode shift using
   a 64-bit global or out register on SPARC-V8+.  */
const char *
output_v8plus_shift (rtx *operands, rtx insn, const char *opcode)
{
  static char asm_code[60];

  /* The scratch register is only required when the destination
     register is not a 64-bit global or out register.  */
  if (which_alternative != 2)
    operands[3] = operands[0];

  /* We can only shift by constants <= 63. */
  if (GET_CODE (operands[2]) == CONST_INT)
    operands[2] = GEN_INT (INTVAL (operands[2]) & 0x3f);

  if (GET_CODE (operands[1]) == CONST_INT)
    {
      output_asm_insn ("mov\t%1, %3", operands);
    }
  else
    {
      output_asm_insn ("sllx\t%H1, 32, %3", operands);
      if (sparc_check_64 (operands[1], insn) <= 0)
	output_asm_insn ("srl\t%L1, 0, %L1", operands);
      output_asm_insn ("or\t%L1, %3, %3", operands);
    }

  strcpy(asm_code, opcode);

  if (which_alternative != 2)
    return strcat (asm_code, "\t%0, %2, %L0\n\tsrlx\t%L0, 32, %H0");
  else
    return strcat (asm_code, "\t%3, %2, %3\n\tsrlx\t%3, 32, %H0\n\tmov\t%3, %L0");
}

/* Output rtl to increment the profiler label LABELNO
   for profiling a function entry.  */

void
sparc_profile_hook (int labelno)
{
  char buf[32];
  rtx lab, fun;

  ASM_GENERATE_INTERNAL_LABEL (buf, "LP", labelno);
  lab = gen_rtx_SYMBOL_REF (Pmode, ggc_strdup (buf));
  fun = gen_rtx_SYMBOL_REF (Pmode, MCOUNT_FUNCTION);

  emit_library_call (fun, LCT_NORMAL, VOIDmode, 1, lab, Pmode);
}

#ifdef OBJECT_FORMAT_ELF
static void
sparc_elf_asm_named_section (const char *name, unsigned int flags,
			     tree decl)
{
  if (flags & SECTION_MERGE)
    {
      /* entsize cannot be expressed in this section attributes
	 encoding style.  */
      default_elf_asm_named_section (name, flags, decl);
      return;
    }

  fprintf (asm_out_file, "\t.section\t\"%s\"", name);

  if (!(flags & SECTION_DEBUG))
    fputs (",#alloc", asm_out_file);
  if (flags & SECTION_WRITE)
    fputs (",#write", asm_out_file);
  if (flags & SECTION_TLS)
    fputs (",#tls", asm_out_file);
  if (flags & SECTION_CODE)
    fputs (",#execinstr", asm_out_file);

  /* ??? Handle SECTION_BSS.  */

  fputc ('\n', asm_out_file);
}
#endif /* OBJECT_FORMAT_ELF */

/* We do not allow indirect calls to be optimized into sibling calls.

   We cannot use sibling calls when delayed branches are disabled
   because they will likely require the call delay slot to be filled.

   Also, on SPARC 32-bit we cannot emit a sibling call when the
   current function returns a structure.  This is because the "unimp
   after call" convention would cause the callee to return to the
   wrong place.  The generic code already disallows cases where the
   function being called returns a structure.

   It may seem strange how this last case could occur.  Usually there
   is code after the call which jumps to epilogue code which dumps the
   return value into the struct return area.  That ought to invalidate
   the sibling call right?  Well, in the C++ case we can end up passing
   the pointer to the struct return area to a constructor (which returns
   void) and then nothing else happens.  Such a sibling call would look
   valid without the added check here.  */
static bool
sparc_function_ok_for_sibcall (tree decl, tree exp ATTRIBUTE_UNUSED)
{
  return (decl
	  && flag_delayed_branch
	  && (TARGET_ARCH64 || ! current_function_returns_struct));
}

/* libfunc renaming.  */
#include "config/gofast.h"

static void
sparc_init_libfuncs (void)
{
  if (TARGET_ARCH32)
    {
      /* Use the subroutines that Sun's library provides for integer
	 multiply and divide.  The `*' prevents an underscore from
	 being prepended by the compiler. .umul is a little faster
	 than .mul.  */
      set_optab_libfunc (smul_optab, SImode, "*.umul");
      set_optab_libfunc (sdiv_optab, SImode, "*.div");
      set_optab_libfunc (udiv_optab, SImode, "*.udiv");
      set_optab_libfunc (smod_optab, SImode, "*.rem");
      set_optab_libfunc (umod_optab, SImode, "*.urem");

      /* TFmode arithmetic.  These names are part of the SPARC 32bit ABI.  */
      set_optab_libfunc (add_optab, TFmode, "_Q_add");
      set_optab_libfunc (sub_optab, TFmode, "_Q_sub");
      set_optab_libfunc (neg_optab, TFmode, "_Q_neg");
      set_optab_libfunc (smul_optab, TFmode, "_Q_mul");
      set_optab_libfunc (sdiv_optab, TFmode, "_Q_div");

      /* We can define the TFmode sqrt optab only if TARGET_FPU.  This
	 is because with soft-float, the SFmode and DFmode sqrt
	 instructions will be absent, and the compiler will notice and
	 try to use the TFmode sqrt instruction for calls to the
	 builtin function sqrt, but this fails.  */
      if (TARGET_FPU)
	set_optab_libfunc (sqrt_optab, TFmode, "_Q_sqrt");

      set_optab_libfunc (eq_optab, TFmode, "_Q_feq");
      set_optab_libfunc (ne_optab, TFmode, "_Q_fne");
      set_optab_libfunc (gt_optab, TFmode, "_Q_fgt");
      set_optab_libfunc (ge_optab, TFmode, "_Q_fge");
      set_optab_libfunc (lt_optab, TFmode, "_Q_flt");
      set_optab_libfunc (le_optab, TFmode, "_Q_fle");

      set_conv_libfunc (sext_optab,   TFmode, SFmode, "_Q_stoq");
      set_conv_libfunc (sext_optab,   TFmode, DFmode, "_Q_dtoq");
      set_conv_libfunc (trunc_optab,  SFmode, TFmode, "_Q_qtos");
      set_conv_libfunc (trunc_optab,  DFmode, TFmode, "_Q_qtod");

      set_conv_libfunc (sfix_optab,   SImode, TFmode, "_Q_qtoi");
      set_conv_libfunc (ufix_optab,   SImode, TFmode, "_Q_qtou");
      set_conv_libfunc (sfloat_optab, TFmode, SImode, "_Q_itoq");
      set_conv_libfunc (ufloat_optab, TFmode, SImode, "_Q_utoq");

      if (DITF_CONVERSION_LIBFUNCS)
	{
	  set_conv_libfunc (sfix_optab,   DImode, TFmode, "_Q_qtoll");
	  set_conv_libfunc (ufix_optab,   DImode, TFmode, "_Q_qtoull");
	  set_conv_libfunc (sfloat_optab, TFmode, DImode, "_Q_lltoq");
	  set_conv_libfunc (ufloat_optab, TFmode, DImode, "_Q_ulltoq");
	}

      if (SUN_CONVERSION_LIBFUNCS)
	{
	  set_conv_libfunc (sfix_optab, DImode, SFmode, "__ftoll");
	  set_conv_libfunc (ufix_optab, DImode, SFmode, "__ftoull");
	  set_conv_libfunc (sfix_optab, DImode, DFmode, "__dtoll");
	  set_conv_libfunc (ufix_optab, DImode, DFmode, "__dtoull");
	}
    }
  if (TARGET_ARCH64)
    {
      /* In the SPARC 64bit ABI, SImode multiply and divide functions
	 do not exist in the library.  Make sure the compiler does not
	 emit calls to them by accident.  (It should always use the
         hardware instructions.)  */
      set_optab_libfunc (smul_optab, SImode, 0);
      set_optab_libfunc (sdiv_optab, SImode, 0);
      set_optab_libfunc (udiv_optab, SImode, 0);
      set_optab_libfunc (smod_optab, SImode, 0);
      set_optab_libfunc (umod_optab, SImode, 0);

      if (SUN_INTEGER_MULTIPLY_64)
	{
	  set_optab_libfunc (smul_optab, DImode, "__mul64");
	  set_optab_libfunc (sdiv_optab, DImode, "__div64");
	  set_optab_libfunc (udiv_optab, DImode, "__udiv64");
	  set_optab_libfunc (smod_optab, DImode, "__rem64");
	  set_optab_libfunc (umod_optab, DImode, "__urem64");
	}

      if (SUN_CONVERSION_LIBFUNCS)
	{
	  set_conv_libfunc (sfix_optab, DImode, SFmode, "__ftol");
	  set_conv_libfunc (ufix_optab, DImode, SFmode, "__ftoul");
	  set_conv_libfunc (sfix_optab, DImode, DFmode, "__dtol");
	  set_conv_libfunc (ufix_optab, DImode, DFmode, "__dtoul");
	}
    }

  gofast_maybe_init_libfuncs ();
}

#define def_builtin(NAME, CODE, TYPE) \
  lang_hooks.builtin_function((NAME), (TYPE), (CODE), BUILT_IN_MD, NULL, \
                              NULL_TREE)

/* Implement the TARGET_INIT_BUILTINS target hook.
   Create builtin functions for special SPARC instructions.  */

static void
sparc_init_builtins (void)
{
  if (TARGET_VIS)
    sparc_vis_init_builtins ();
}

/* Create builtin functions for VIS 1.0 instructions.  */

static void
sparc_vis_init_builtins (void)
{
  tree v4qi = build_vector_type (unsigned_intQI_type_node, 4);
  tree v8qi = build_vector_type (unsigned_intQI_type_node, 8);
  tree v4hi = build_vector_type (intHI_type_node, 4);
  tree v2hi = build_vector_type (intHI_type_node, 2);
  tree v2si = build_vector_type (intSI_type_node, 2);

  tree v4qi_ftype_v4hi = build_function_type_list (v4qi, v4hi, 0);
  tree v8qi_ftype_v2si_v8qi = build_function_type_list (v8qi, v2si, v8qi, 0);
  tree v2hi_ftype_v2si = build_function_type_list (v2hi, v2si, 0);
  tree v4hi_ftype_v4qi = build_function_type_list (v4hi, v4qi, 0);
  tree v8qi_ftype_v4qi_v4qi = build_function_type_list (v8qi, v4qi, v4qi, 0);
  tree v4hi_ftype_v4qi_v4hi = build_function_type_list (v4hi, v4qi, v4hi, 0);
  tree v4hi_ftype_v4qi_v2hi = build_function_type_list (v4hi, v4qi, v2hi, 0);
  tree v2si_ftype_v4qi_v2hi = build_function_type_list (v2si, v4qi, v2hi, 0);
  tree v4hi_ftype_v8qi_v4hi = build_function_type_list (v4hi, v8qi, v4hi, 0);
  tree v4hi_ftype_v4hi_v4hi = build_function_type_list (v4hi, v4hi, v4hi, 0);
  tree v2si_ftype_v2si_v2si = build_function_type_list (v2si, v2si, v2si, 0);
  tree v8qi_ftype_v8qi_v8qi = build_function_type_list (v8qi, v8qi, v8qi, 0);
  tree di_ftype_v8qi_v8qi_di = build_function_type_list (intDI_type_node,
							 v8qi, v8qi,
							 intDI_type_node, 0);
  tree di_ftype_di_di = build_function_type_list (intDI_type_node,
						  intDI_type_node,
						  intDI_type_node, 0);
  tree ptr_ftype_ptr_si = build_function_type_list (ptr_type_node,
		        			    ptr_type_node,
					            intSI_type_node, 0);
  tree ptr_ftype_ptr_di = build_function_type_list (ptr_type_node,
		        			    ptr_type_node,
					            intDI_type_node, 0);

  /* Packing and expanding vectors.  */
  def_builtin ("__builtin_vis_fpack16", CODE_FOR_fpack16_vis, v4qi_ftype_v4hi);
  def_builtin ("__builtin_vis_fpack32", CODE_FOR_fpack32_vis,
	       v8qi_ftype_v2si_v8qi);
  def_builtin ("__builtin_vis_fpackfix", CODE_FOR_fpackfix_vis,
	       v2hi_ftype_v2si);
  def_builtin ("__builtin_vis_fexpand", CODE_FOR_fexpand_vis, v4hi_ftype_v4qi);
  def_builtin ("__builtin_vis_fpmerge", CODE_FOR_fpmerge_vis,
	       v8qi_ftype_v4qi_v4qi);

  /* Multiplications.  */
  def_builtin ("__builtin_vis_fmul8x16", CODE_FOR_fmul8x16_vis,
	       v4hi_ftype_v4qi_v4hi);
  def_builtin ("__builtin_vis_fmul8x16au", CODE_FOR_fmul8x16au_vis,
	       v4hi_ftype_v4qi_v2hi);
  def_builtin ("__builtin_vis_fmul8x16al", CODE_FOR_fmul8x16al_vis,
	       v4hi_ftype_v4qi_v2hi);
  def_builtin ("__builtin_vis_fmul8sux16", CODE_FOR_fmul8sux16_vis,
	       v4hi_ftype_v8qi_v4hi);
  def_builtin ("__builtin_vis_fmul8ulx16", CODE_FOR_fmul8ulx16_vis,
	       v4hi_ftype_v8qi_v4hi);
  def_builtin ("__builtin_vis_fmuld8sux16", CODE_FOR_fmuld8sux16_vis,
	       v2si_ftype_v4qi_v2hi);
  def_builtin ("__builtin_vis_fmuld8ulx16", CODE_FOR_fmuld8ulx16_vis,
	       v2si_ftype_v4qi_v2hi);

  /* Data aligning.  */
  def_builtin ("__builtin_vis_faligndatav4hi", CODE_FOR_faligndatav4hi_vis,
	       v4hi_ftype_v4hi_v4hi);
  def_builtin ("__builtin_vis_faligndatav8qi", CODE_FOR_faligndatav8qi_vis,
	       v8qi_ftype_v8qi_v8qi);
  def_builtin ("__builtin_vis_faligndatav2si", CODE_FOR_faligndatav2si_vis,
	       v2si_ftype_v2si_v2si);
  def_builtin ("__builtin_vis_faligndatadi", CODE_FOR_faligndatadi_vis,
               di_ftype_di_di);
  if (TARGET_ARCH64)
    def_builtin ("__builtin_vis_alignaddr", CODE_FOR_alignaddrdi_vis,
	         ptr_ftype_ptr_di);
  else
    def_builtin ("__builtin_vis_alignaddr", CODE_FOR_alignaddrsi_vis,
	         ptr_ftype_ptr_si);

  /* Pixel distance.  */
  def_builtin ("__builtin_vis_pdist", CODE_FOR_pdist_vis,
	       di_ftype_v8qi_v8qi_di);
}

/* Handle TARGET_EXPAND_BUILTIN target hook.
   Expand builtin functions for sparc intrinsics.  */

static rtx
sparc_expand_builtin (tree exp, rtx target,
		      rtx subtarget ATTRIBUTE_UNUSED,
		      enum machine_mode tmode ATTRIBUTE_UNUSED,
		      int ignore ATTRIBUTE_UNUSED)
{
  tree arglist;
  tree fndecl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
  unsigned int icode = DECL_FUNCTION_CODE (fndecl);
  rtx pat, op[4];
  enum machine_mode mode[4];
  int arg_count = 0;

  mode[0] = insn_data[icode].operand[0].mode;
  if (!target
      || GET_MODE (target) != mode[0]
      || ! (*insn_data[icode].operand[0].predicate) (target, mode[0]))
    op[0] = gen_reg_rtx (mode[0]);
  else
    op[0] = target;

  for (arglist = TREE_OPERAND (exp, 1); arglist;
       arglist = TREE_CHAIN (arglist))
    {
      tree arg = TREE_VALUE (arglist);

      arg_count++;
      mode[arg_count] = insn_data[icode].operand[arg_count].mode;
      op[arg_count] = expand_normal (arg);

      if (! (*insn_data[icode].operand[arg_count].predicate) (op[arg_count],
							      mode[arg_count]))
	op[arg_count] = copy_to_mode_reg (mode[arg_count], op[arg_count]);
    }

  switch (arg_count)
    {
    case 1:
      pat = GEN_FCN (icode) (op[0], op[1]);
      break;
    case 2:
      pat = GEN_FCN (icode) (op[0], op[1], op[2]);
      break;
    case 3:
      pat = GEN_FCN (icode) (op[0], op[1], op[2], op[3]);
      break;
    default:
      gcc_unreachable ();
    }

  if (!pat)
    return NULL_RTX;

  emit_insn (pat);

  return op[0];
}

static int
sparc_vis_mul8x16 (int e8, int e16)
{
  return (e8 * e16 + 128) / 256;
}

/* Multiply the vector elements in ELTS0 to the elements in ELTS1 as specified
   by FNCODE.  All of the elements in ELTS0 and ELTS1 lists must be integer
   constants.  A tree list with the results of the multiplications is returned,
   and each element in the list is of INNER_TYPE.  */

static tree
sparc_handle_vis_mul8x16 (int fncode, tree inner_type, tree elts0, tree elts1)
{
  tree n_elts = NULL_TREE;
  int scale;

  switch (fncode)
    {
    case CODE_FOR_fmul8x16_vis:
      for (; elts0 && elts1;
	   elts0 = TREE_CHAIN (elts0), elts1 = TREE_CHAIN (elts1))
	{
	  int val
	    = sparc_vis_mul8x16 (TREE_INT_CST_LOW (TREE_VALUE (elts0)),
				 TREE_INT_CST_LOW (TREE_VALUE (elts1)));
	  n_elts = tree_cons (NULL_TREE,
			      build_int_cst (inner_type, val),
			      n_elts);
	}
      break;

    case CODE_FOR_fmul8x16au_vis:
      scale = TREE_INT_CST_LOW (TREE_VALUE (elts1));

      for (; elts0; elts0 = TREE_CHAIN (elts0))
	{
	  int val
	    = sparc_vis_mul8x16 (TREE_INT_CST_LOW (TREE_VALUE (elts0)),
				 scale);
	  n_elts = tree_cons (NULL_TREE,
			      build_int_cst (inner_type, val),
			      n_elts);
	}
      break;

    case CODE_FOR_fmul8x16al_vis:
      scale = TREE_INT_CST_LOW (TREE_VALUE (TREE_CHAIN (elts1)));

      for (; elts0; elts0 = TREE_CHAIN (elts0))
	{
	  int val
	    = sparc_vis_mul8x16 (TREE_INT_CST_LOW (TREE_VALUE (elts0)),
				 scale);
	  n_elts = tree_cons (NULL_TREE,
			      build_int_cst (inner_type, val),
			      n_elts);
	}
      break;

    default:
      gcc_unreachable ();
    }

  return nreverse (n_elts);

}
/* Handle TARGET_FOLD_BUILTIN target hook.
   Fold builtin functions for SPARC intrinsics.  If IGNORE is true the
   result of the function call is ignored.  NULL_TREE is returned if the
   function could not be folded.  */

static tree
sparc_fold_builtin (tree fndecl, tree arglist, bool ignore)
{
  tree arg0, arg1, arg2;
  tree rtype = TREE_TYPE (TREE_TYPE (fndecl));

  if (ignore
      && DECL_FUNCTION_CODE (fndecl) != CODE_FOR_alignaddrsi_vis
      && DECL_FUNCTION_CODE (fndecl) != CODE_FOR_alignaddrdi_vis)
    return fold_convert (rtype, integer_zero_node);

  switch (DECL_FUNCTION_CODE (fndecl))
    {
    case CODE_FOR_fexpand_vis:
      arg0 = TREE_VALUE (arglist);
      STRIP_NOPS (arg0);

      if (TREE_CODE (arg0) == VECTOR_CST)
	{
	  tree inner_type = TREE_TYPE (rtype);
	  tree elts = TREE_VECTOR_CST_ELTS (arg0);
	  tree n_elts = NULL_TREE;

	  for (; elts; elts = TREE_CHAIN (elts))
	    {
	      unsigned int val = TREE_INT_CST_LOW (TREE_VALUE (elts)) << 4;
	      n_elts = tree_cons (NULL_TREE,
				  build_int_cst (inner_type, val),
				  n_elts);
	    }
	  return build_vector (rtype, nreverse (n_elts));
	}
      break;

    case CODE_FOR_fmul8x16_vis:
    case CODE_FOR_fmul8x16au_vis:
    case CODE_FOR_fmul8x16al_vis:
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      STRIP_NOPS (arg0);
      STRIP_NOPS (arg1);

      if (TREE_CODE (arg0) == VECTOR_CST && TREE_CODE (arg1) == VECTOR_CST)
	{
	  tree inner_type = TREE_TYPE (rtype);
	  tree elts0 = TREE_VECTOR_CST_ELTS (arg0);
	  tree elts1 = TREE_VECTOR_CST_ELTS (arg1);
	  tree n_elts = sparc_handle_vis_mul8x16 (DECL_FUNCTION_CODE (fndecl),
						  inner_type, elts0, elts1);

	  return build_vector (rtype, n_elts);
	}
      break;

    case CODE_FOR_fpmerge_vis:
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      STRIP_NOPS (arg0);
      STRIP_NOPS (arg1);

      if (TREE_CODE (arg0) == VECTOR_CST && TREE_CODE (arg1) == VECTOR_CST)
	{
	  tree elts0 = TREE_VECTOR_CST_ELTS (arg0);
	  tree elts1 = TREE_VECTOR_CST_ELTS (arg1);
	  tree n_elts = NULL_TREE;

	  for (; elts0 && elts1;
	       elts0 = TREE_CHAIN (elts0), elts1 = TREE_CHAIN (elts1))
	    {
	      n_elts = tree_cons (NULL_TREE, TREE_VALUE (elts0), n_elts);
	      n_elts = tree_cons (NULL_TREE, TREE_VALUE (elts1), n_elts);
	    }

	  return build_vector (rtype, nreverse (n_elts));
	}
      break;

    case CODE_FOR_pdist_vis:
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      arg2 = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      STRIP_NOPS (arg0);
      STRIP_NOPS (arg1);
      STRIP_NOPS (arg2);

      if (TREE_CODE (arg0) == VECTOR_CST
	  && TREE_CODE (arg1) == VECTOR_CST
	  && TREE_CODE (arg2) == INTEGER_CST)
	{
	  int overflow = 0;
	  unsigned HOST_WIDE_INT low = TREE_INT_CST_LOW (arg2);
	  HOST_WIDE_INT high = TREE_INT_CST_HIGH (arg2);
	  tree elts0 = TREE_VECTOR_CST_ELTS (arg0);
	  tree elts1 = TREE_VECTOR_CST_ELTS (arg1);

	  for (; elts0 && elts1;
	       elts0 = TREE_CHAIN (elts0), elts1 = TREE_CHAIN (elts1))
	    {
	      unsigned HOST_WIDE_INT
		low0 = TREE_INT_CST_LOW (TREE_VALUE (elts0)),
		low1 = TREE_INT_CST_LOW (TREE_VALUE (elts1));
	      HOST_WIDE_INT high0 = TREE_INT_CST_HIGH (TREE_VALUE (elts0));
	      HOST_WIDE_INT high1 = TREE_INT_CST_HIGH (TREE_VALUE (elts1));

	      unsigned HOST_WIDE_INT l;
	      HOST_WIDE_INT h;

	      overflow |= neg_double (low1, high1, &l, &h);
	      overflow |= add_double (low0, high0, l, h, &l, &h);
	      if (h < 0)
		overflow |= neg_double (l, h, &l, &h);

	      overflow |= add_double (low, high, l, h, &low, &high);
	    }

	  gcc_assert (overflow == 0);

	  return build_int_cst_wide (rtype, low, high);
	}

    default:
      break;
    }

  return NULL_TREE;
}

int
sparc_extra_constraint_check (rtx op, int c, int strict)
{
  int reload_ok_mem;

  if (TARGET_ARCH64
      && (c == 'T' || c == 'U'))
    return 0;

  switch (c)
    {
    case 'Q':
      return fp_sethi_p (op);

    case 'R':
      return fp_mov_p (op);

    case 'S':
      return fp_high_losum_p (op);

    case 'U':
      if (! strict
	  || (GET_CODE (op) == REG
	      && (REGNO (op) < FIRST_PSEUDO_REGISTER
		  || reg_renumber[REGNO (op)] >= 0)))
	return register_ok_for_ldd (op);

      return 0;

    case 'W':
    case 'T':
      break;

    case 'Y':
      return const_zero_operand (op, GET_MODE (op));

    default:
      return 0;
    }

  /* Our memory extra constraints have to emulate the
     behavior of 'm' and 'o' in order for reload to work
     correctly.  */
  if (GET_CODE (op) == MEM)
    {
      reload_ok_mem = 0;
      if ((TARGET_ARCH64 || mem_min_alignment (op, 8))
	  && (! strict
	      || strict_memory_address_p (Pmode, XEXP (op, 0))))
	reload_ok_mem = 1;
    }
  else
    {
      reload_ok_mem = (reload_in_progress
		       && GET_CODE (op) == REG
		       && REGNO (op) >= FIRST_PSEUDO_REGISTER
		       && reg_renumber [REGNO (op)] < 0);
    }

  return reload_ok_mem;
}

/* ??? This duplicates information provided to the compiler by the
   ??? scheduler description.  Some day, teach genautomata to output
   ??? the latencies and then CSE will just use that.  */

static bool
sparc_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  enum machine_mode mode = GET_MODE (x);
  bool float_mode_p = FLOAT_MODE_P (mode);

  switch (code)
    {
    case CONST_INT:
      if (INTVAL (x) < 0x1000 && INTVAL (x) >= -0x1000)
	{
	  *total = 0;
	  return true;
	}
      /* FALLTHRU */

    case HIGH:
      *total = 2;
      return true;

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      *total = 4;
      return true;

    case CONST_DOUBLE:
      if (GET_MODE (x) == VOIDmode
	  && ((CONST_DOUBLE_HIGH (x) == 0
	       && CONST_DOUBLE_LOW (x) < 0x1000)
	      || (CONST_DOUBLE_HIGH (x) == -1
		  && CONST_DOUBLE_LOW (x) < 0
		  && CONST_DOUBLE_LOW (x) >= -0x1000)))
	*total = 0;
      else
	*total = 8;
      return true;

    case MEM:
      /* If outer-code was a sign or zero extension, a cost
	 of COSTS_N_INSNS (1) was already added in.  This is
	 why we are subtracting it back out.  */
      if (outer_code == ZERO_EXTEND)
	{
	  *total = sparc_costs->int_zload - COSTS_N_INSNS (1);
	}
      else if (outer_code == SIGN_EXTEND)
	{
	  *total = sparc_costs->int_sload - COSTS_N_INSNS (1);
	}
      else if (float_mode_p)
	{
	  *total = sparc_costs->float_load;
	}
      else
	{
	  *total = sparc_costs->int_load;
	}

      return true;

    case PLUS:
    case MINUS:
      if (float_mode_p)
	*total = sparc_costs->float_plusminus;
      else
	*total = COSTS_N_INSNS (1);
      return false;

    case MULT:
      if (float_mode_p)
	*total = sparc_costs->float_mul;
      else if (! TARGET_HARD_MUL)
	*total = COSTS_N_INSNS (25);
      else
	{
	  int bit_cost;

	  bit_cost = 0;
	  if (sparc_costs->int_mul_bit_factor)
	    {
	      int nbits;

	      if (GET_CODE (XEXP (x, 1)) == CONST_INT)
		{
		  unsigned HOST_WIDE_INT value = INTVAL (XEXP (x, 1));
		  for (nbits = 0; value != 0; value &= value - 1)
		    nbits++;
		}
	      else if (GET_CODE (XEXP (x, 1)) == CONST_DOUBLE
		       && GET_MODE (XEXP (x, 1)) == VOIDmode)
		{
		  rtx x1 = XEXP (x, 1);
		  unsigned HOST_WIDE_INT value1 = CONST_DOUBLE_LOW (x1);
		  unsigned HOST_WIDE_INT value2 = CONST_DOUBLE_HIGH (x1);

		  for (nbits = 0; value1 != 0; value1 &= value1 - 1)
		    nbits++;
		  for (; value2 != 0; value2 &= value2 - 1)
		    nbits++;
		}
	      else
		nbits = 7;

	      if (nbits < 3)
		nbits = 3;
	      bit_cost = (nbits - 3) / sparc_costs->int_mul_bit_factor;
	      bit_cost = COSTS_N_INSNS (bit_cost);
	    }

	  if (mode == DImode)
	    *total = sparc_costs->int_mulX + bit_cost;
	  else
	    *total = sparc_costs->int_mul + bit_cost;
	}
      return false;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      *total = COSTS_N_INSNS (1) + sparc_costs->shift_penalty;
      return false;

    case DIV:
    case UDIV:
    case MOD:
    case UMOD:
      if (float_mode_p)
	{
	  if (mode == DFmode)
	    *total = sparc_costs->float_div_df;
	  else
	    *total = sparc_costs->float_div_sf;
	}
      else
	{
	  if (mode == DImode)
	    *total = sparc_costs->int_divX;
	  else
	    *total = sparc_costs->int_div;
	}
      return false;

    case NEG:
      if (! float_mode_p)
	{
	  *total = COSTS_N_INSNS (1);
	  return false;
	}
      /* FALLTHRU */

    case ABS:
    case FLOAT:
    case UNSIGNED_FLOAT:
    case FIX:
    case UNSIGNED_FIX:
    case FLOAT_EXTEND:
    case FLOAT_TRUNCATE:
      *total = sparc_costs->float_move;
      return false;

    case SQRT:
      if (mode == DFmode)
	*total = sparc_costs->float_sqrt_df;
      else
	*total = sparc_costs->float_sqrt_sf;
      return false;

    case COMPARE:
      if (float_mode_p)
	*total = sparc_costs->float_cmp;
      else
	*total = COSTS_N_INSNS (1);
      return false;

    case IF_THEN_ELSE:
      if (float_mode_p)
	*total = sparc_costs->float_cmove;
      else
	*total = sparc_costs->int_cmove;
      return false;

    case IOR:
      /* Handle the NAND vector patterns.  */
      if (sparc_vector_mode_supported_p (GET_MODE (x))
	  && GET_CODE (XEXP (x, 0)) == NOT
	  && GET_CODE (XEXP (x, 1)) == NOT)
	{
	  *total = COSTS_N_INSNS (1);
	  return true;
	}
      else
        return false;

    default:
      return false;
    }
}

/* Emit the sequence of insns SEQ while preserving the registers REG and REG2.
   This is achieved by means of a manual dynamic stack space allocation in
   the current frame.  We make the assumption that SEQ doesn't contain any
   function calls, with the possible exception of calls to the PIC helper.  */

static void
emit_and_preserve (rtx seq, rtx reg, rtx reg2)
{
  /* We must preserve the lowest 16 words for the register save area.  */
  HOST_WIDE_INT offset = 16*UNITS_PER_WORD;
  /* We really need only 2 words of fresh stack space.  */
  HOST_WIDE_INT size = SPARC_STACK_ALIGN (offset + 2*UNITS_PER_WORD);

  rtx slot
    = gen_rtx_MEM (word_mode, plus_constant (stack_pointer_rtx,
					     SPARC_STACK_BIAS + offset));

  emit_insn (gen_stack_pointer_dec (GEN_INT (size)));
  emit_insn (gen_rtx_SET (VOIDmode, slot, reg));
  if (reg2)
    emit_insn (gen_rtx_SET (VOIDmode,
			    adjust_address (slot, word_mode, UNITS_PER_WORD),
			    reg2));
  emit_insn (seq);
  if (reg2)
    emit_insn (gen_rtx_SET (VOIDmode,
			    reg2,
			    adjust_address (slot, word_mode, UNITS_PER_WORD)));
  emit_insn (gen_rtx_SET (VOIDmode, reg, slot));
  emit_insn (gen_stack_pointer_inc (GEN_INT (size)));
}

/* Output the assembler code for a thunk function.  THUNK_DECL is the
   declaration for the thunk function itself, FUNCTION is the decl for
   the target function.  DELTA is an immediate constant offset to be
   added to THIS.  If VCALL_OFFSET is nonzero, the word at address
   (*THIS + VCALL_OFFSET) should be additionally added to THIS.  */

static void
sparc_output_mi_thunk (FILE *file, tree thunk_fndecl ATTRIBUTE_UNUSED,
		       HOST_WIDE_INT delta, HOST_WIDE_INT vcall_offset,
		       tree function)
{
  rtx this, insn, funexp;
  unsigned int int_arg_first;

  reload_completed = 1;
  epilogue_completed = 1;
  no_new_pseudos = 1;
  reset_block_changes ();

  emit_note (NOTE_INSN_PROLOGUE_END);

  if (flag_delayed_branch)
    {
      /* We will emit a regular sibcall below, so we need to instruct
	 output_sibcall that we are in a leaf function.  */
      sparc_leaf_function_p = current_function_uses_only_leaf_regs = 1;

      /* This will cause final.c to invoke leaf_renumber_regs so we
	 must behave as if we were in a not-yet-leafified function.  */
      int_arg_first = SPARC_INCOMING_INT_ARG_FIRST;
    }
  else
    {
      /* We will emit the sibcall manually below, so we will need to
	 manually spill non-leaf registers.  */
      sparc_leaf_function_p = current_function_uses_only_leaf_regs = 0;

      /* We really are in a leaf function.  */
      int_arg_first = SPARC_OUTGOING_INT_ARG_FIRST;
    }

  /* Find the "this" pointer.  Normally in %o0, but in ARCH64 if the function
     returns a structure, the structure return pointer is there instead.  */
  if (TARGET_ARCH64 && aggregate_value_p (TREE_TYPE (TREE_TYPE (function)), function))
    this = gen_rtx_REG (Pmode, int_arg_first + 1);
  else
    this = gen_rtx_REG (Pmode, int_arg_first);

  /* Add DELTA.  When possible use a plain add, otherwise load it into
     a register first.  */
  if (delta)
    {
      rtx delta_rtx = GEN_INT (delta);

      if (! SPARC_SIMM13_P (delta))
	{
	  rtx scratch = gen_rtx_REG (Pmode, 1);
	  emit_move_insn (scratch, delta_rtx);
	  delta_rtx = scratch;
	}

      /* THIS += DELTA.  */
      emit_insn (gen_add2_insn (this, delta_rtx));
    }

  /* Add the word at address (*THIS + VCALL_OFFSET).  */
  if (vcall_offset)
    {
      rtx vcall_offset_rtx = GEN_INT (vcall_offset);
      rtx scratch = gen_rtx_REG (Pmode, 1);

      gcc_assert (vcall_offset < 0);

      /* SCRATCH = *THIS.  */
      emit_move_insn (scratch, gen_rtx_MEM (Pmode, this));

      /* Prepare for adding VCALL_OFFSET.  The difficulty is that we
	 may not have any available scratch register at this point.  */
      if (SPARC_SIMM13_P (vcall_offset))
	;
      /* This is the case if ARCH64 (unless -ffixed-g5 is passed).  */
      else if (! fixed_regs[5]
	       /* The below sequence is made up of at least 2 insns,
		  while the default method may need only one.  */
	       && vcall_offset < -8192)
	{
	  rtx scratch2 = gen_rtx_REG (Pmode, 5);
	  emit_move_insn (scratch2, vcall_offset_rtx);
	  vcall_offset_rtx = scratch2;
	}
      else
	{
	  rtx increment = GEN_INT (-4096);

	  /* VCALL_OFFSET is a negative number whose typical range can be
	     estimated as -32768..0 in 32-bit mode.  In almost all cases
	     it is therefore cheaper to emit multiple add insns than
	     spilling and loading the constant into a register (at least
	     6 insns).  */
	  while (! SPARC_SIMM13_P (vcall_offset))
	    {
	      emit_insn (gen_add2_insn (scratch, increment));
	      vcall_offset += 4096;
	    }
	  vcall_offset_rtx = GEN_INT (vcall_offset); /* cannot be 0 */
	}

      /* SCRATCH = *(*THIS + VCALL_OFFSET).  */
      emit_move_insn (scratch, gen_rtx_MEM (Pmode,
					    gen_rtx_PLUS (Pmode,
							  scratch,
							  vcall_offset_rtx)));

      /* THIS += *(*THIS + VCALL_OFFSET).  */
      emit_insn (gen_add2_insn (this, scratch));
    }

  /* Generate a tail call to the target function.  */
  if (! TREE_USED (function))
    {
      assemble_external (function);
      TREE_USED (function) = 1;
    }
  funexp = XEXP (DECL_RTL (function), 0);

  if (flag_delayed_branch)
    {
      funexp = gen_rtx_MEM (FUNCTION_MODE, funexp);
      insn = emit_call_insn (gen_sibcall (funexp));
      SIBLING_CALL_P (insn) = 1;
    }
  else
    {
      /* The hoops we have to jump through in order to generate a sibcall
	 without using delay slots...  */
      rtx spill_reg, spill_reg2, seq, scratch = gen_rtx_REG (Pmode, 1);

      if (flag_pic)
        {
	  spill_reg = gen_rtx_REG (word_mode, 15);  /* %o7 */
	  spill_reg2 = gen_rtx_REG (word_mode, PIC_OFFSET_TABLE_REGNUM);
	  start_sequence ();
	  /* Delay emitting the PIC helper function because it needs to
	     change the section and we are emitting assembly code.  */
	  load_pic_register (true);  /* clobbers %o7 */
	  scratch = legitimize_pic_address (funexp, Pmode, scratch);
	  seq = get_insns ();
	  end_sequence ();
	  emit_and_preserve (seq, spill_reg, spill_reg2);
	}
      else if (TARGET_ARCH32)
	{
	  emit_insn (gen_rtx_SET (VOIDmode,
				  scratch,
				  gen_rtx_HIGH (SImode, funexp)));
	  emit_insn (gen_rtx_SET (VOIDmode,
				  scratch,
				  gen_rtx_LO_SUM (SImode, scratch, funexp)));
	}
      else  /* TARGET_ARCH64 */
        {
	  switch (sparc_cmodel)
	    {
	    case CM_MEDLOW:
	    case CM_MEDMID:
	      /* The destination can serve as a temporary.  */
	      sparc_emit_set_symbolic_const64 (scratch, funexp, scratch);
	      break;

	    case CM_MEDANY:
	    case CM_EMBMEDANY:
	      /* The destination cannot serve as a temporary.  */
	      spill_reg = gen_rtx_REG (DImode, 15);  /* %o7 */
	      start_sequence ();
	      sparc_emit_set_symbolic_const64 (scratch, funexp, spill_reg);
	      seq = get_insns ();
	      end_sequence ();
	      emit_and_preserve (seq, spill_reg, 0);
	      break;

	    default:
	      gcc_unreachable ();
	    }
	}

      emit_jump_insn (gen_indirect_jump (scratch));
    }

  emit_barrier ();

  /* Run just enough of rest_of_compilation to get the insns emitted.
     There's not really enough bulk here to make other passes such as
     instruction scheduling worth while.  Note that use_thunk calls
     assemble_start_function and assemble_end_function.  */
  insn = get_insns ();
  insn_locators_initialize ();
  shorten_branches (insn);
  final_start_function (insn, file, 1);
  final (insn, file, 1);
  final_end_function ();

  reload_completed = 0;
  epilogue_completed = 0;
  no_new_pseudos = 0;
}

/* Return true if sparc_output_mi_thunk would be able to output the
   assembler code for the thunk function specified by the arguments
   it is passed, and false otherwise.  */
static bool
sparc_can_output_mi_thunk (tree thunk_fndecl ATTRIBUTE_UNUSED,
			   HOST_WIDE_INT delta ATTRIBUTE_UNUSED,
			   HOST_WIDE_INT vcall_offset,
			   tree function ATTRIBUTE_UNUSED)
{
  /* Bound the loop used in the default method above.  */
  return (vcall_offset >= -32768 || ! fixed_regs[5]);
}

/* How to allocate a 'struct machine_function'.  */

static struct machine_function *
sparc_init_machine_status (void)
{
  return ggc_alloc_cleared (sizeof (struct machine_function));
}

/* Locate some local-dynamic symbol still in use by this function
   so that we can print its name in local-dynamic base patterns.  */

static const char *
get_some_local_dynamic_name (void)
{
  rtx insn;

  if (cfun->machine->some_ld_name)
    return cfun->machine->some_ld_name;

  for (insn = get_insns (); insn ; insn = NEXT_INSN (insn))
    if (INSN_P (insn)
	&& for_each_rtx (&PATTERN (insn), get_some_local_dynamic_name_1, 0))
      return cfun->machine->some_ld_name;

  gcc_unreachable ();
}

static int
get_some_local_dynamic_name_1 (rtx *px, void *data ATTRIBUTE_UNUSED)
{
  rtx x = *px;

  if (x
      && GET_CODE (x) == SYMBOL_REF
      && SYMBOL_REF_TLS_MODEL (x) == TLS_MODEL_LOCAL_DYNAMIC)
    {
      cfun->machine->some_ld_name = XSTR (x, 0);
      return 1;
    }

  return 0;
}

/* Handle the TARGET_DWARF_HANDLE_FRAME_UNSPEC hook.
   This is called from dwarf2out.c to emit call frame instructions
   for frame-related insns containing UNSPECs and UNSPEC_VOLATILEs. */
static void
sparc_dwarf_handle_frame_unspec (const char *label,
				 rtx pattern ATTRIBUTE_UNUSED,
				 int index ATTRIBUTE_UNUSED)
{
  gcc_assert (index == UNSPECV_SAVEW);
  dwarf2out_window_save (label);
}

/* This is called from dwarf2out.c via TARGET_ASM_OUTPUT_DWARF_DTPREL.
   We need to emit DTP-relative relocations.  */

static void
sparc_output_dwarf_dtprel (FILE *file, int size, rtx x)
{
  switch (size)
    {
    case 4:
      fputs ("\t.word\t%r_tls_dtpoff32(", file);
      break;
    case 8:
      fputs ("\t.xword\t%r_tls_dtpoff64(", file);
      break;
    default:
      gcc_unreachable ();
    }
  output_addr_const (file, x);
  fputs (")", file);
}

/* Do whatever processing is required at the end of a file.  */

static void
sparc_file_end (void)
{
  /* If we haven't emitted the special PIC helper function, do so now.  */
  if (pic_helper_symbol_name[0] && !pic_helper_emitted_p)
    emit_pic_helper ();

  if (NEED_INDICATE_EXEC_STACK)
    file_end_indicate_exec_stack ();
}

#ifdef TARGET_ALTERNATE_LONG_DOUBLE_MANGLING
/* Implement TARGET_MANGLE_FUNDAMENTAL_TYPE.  */

static const char *
sparc_mangle_fundamental_type (tree type)
{
  if (!TARGET_64BIT
      && TYPE_MAIN_VARIANT (type) == long_double_type_node
      && TARGET_LONG_DOUBLE_128)
    return "g";

  /* For all other types, use normal C++ mangling.  */
  return NULL;
}
#endif

/* Expand code to perform a 8 or 16-bit compare and swap by doing 32-bit
   compare and swap on the word containing the byte or half-word.  */

void
sparc_expand_compare_and_swap_12 (rtx result, rtx mem, rtx oldval, rtx newval)
{
  rtx addr1 = force_reg (Pmode, XEXP (mem, 0));
  rtx addr = gen_reg_rtx (Pmode);
  rtx off = gen_reg_rtx (SImode);
  rtx oldv = gen_reg_rtx (SImode);
  rtx newv = gen_reg_rtx (SImode);
  rtx oldvalue = gen_reg_rtx (SImode);
  rtx newvalue = gen_reg_rtx (SImode);
  rtx res = gen_reg_rtx (SImode);
  rtx resv = gen_reg_rtx (SImode);
  rtx memsi, val, mask, end_label, loop_label, cc;

  emit_insn (gen_rtx_SET (VOIDmode, addr,
			  gen_rtx_AND (Pmode, addr1, GEN_INT (-4))));

  if (Pmode != SImode)
    addr1 = gen_lowpart (SImode, addr1);
  emit_insn (gen_rtx_SET (VOIDmode, off,
			  gen_rtx_AND (SImode, addr1, GEN_INT (3))));

  memsi = gen_rtx_MEM (SImode, addr);
  set_mem_alias_set (memsi, ALIAS_SET_MEMORY_BARRIER);
  MEM_VOLATILE_P (memsi) = MEM_VOLATILE_P (mem);

  val = force_reg (SImode, memsi);

  emit_insn (gen_rtx_SET (VOIDmode, off,
			  gen_rtx_XOR (SImode, off,
				       GEN_INT (GET_MODE (mem) == QImode
						? 3 : 2))));

  emit_insn (gen_rtx_SET (VOIDmode, off,
			  gen_rtx_ASHIFT (SImode, off, GEN_INT (3))));

  if (GET_MODE (mem) == QImode)
    mask = force_reg (SImode, GEN_INT (0xff));
  else
    mask = force_reg (SImode, GEN_INT (0xffff));

  emit_insn (gen_rtx_SET (VOIDmode, mask,
			  gen_rtx_ASHIFT (SImode, mask, off)));

  emit_insn (gen_rtx_SET (VOIDmode, val,
			  gen_rtx_AND (SImode, gen_rtx_NOT (SImode, mask),
				       val)));

  oldval = gen_lowpart (SImode, oldval);
  emit_insn (gen_rtx_SET (VOIDmode, oldv,
			  gen_rtx_ASHIFT (SImode, oldval, off)));

  newval = gen_lowpart_common (SImode, newval);
  emit_insn (gen_rtx_SET (VOIDmode, newv,
			  gen_rtx_ASHIFT (SImode, newval, off)));

  emit_insn (gen_rtx_SET (VOIDmode, oldv,
			  gen_rtx_AND (SImode, oldv, mask)));

  emit_insn (gen_rtx_SET (VOIDmode, newv,
			  gen_rtx_AND (SImode, newv, mask)));

  end_label = gen_label_rtx ();
  loop_label = gen_label_rtx ();
  emit_label (loop_label);

  emit_insn (gen_rtx_SET (VOIDmode, oldvalue,
			  gen_rtx_IOR (SImode, oldv, val)));

  emit_insn (gen_rtx_SET (VOIDmode, newvalue,
			  gen_rtx_IOR (SImode, newv, val)));

  emit_insn (gen_sync_compare_and_swapsi (res, memsi, oldvalue, newvalue));

  emit_cmp_and_jump_insns (res, oldvalue, EQ, NULL, SImode, 0, end_label);

  emit_insn (gen_rtx_SET (VOIDmode, resv,
			  gen_rtx_AND (SImode, gen_rtx_NOT (SImode, mask),
				       res)));

  sparc_compare_op0 = resv;
  sparc_compare_op1 = val;
  cc = gen_compare_reg (NE);

  emit_insn (gen_rtx_SET (VOIDmode, val, resv));

  sparc_compare_emitted = cc;
  emit_jump_insn (gen_bne (loop_label));

  emit_label (end_label);

  emit_insn (gen_rtx_SET (VOIDmode, res,
			  gen_rtx_AND (SImode, res, mask)));

  emit_insn (gen_rtx_SET (VOIDmode, res,
			  gen_rtx_LSHIFTRT (SImode, res, off)));

  emit_move_insn (result, gen_lowpart (GET_MODE (result), res));
}

#include "gt-sparc.h"

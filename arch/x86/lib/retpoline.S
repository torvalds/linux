/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/stringify.h>
#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/asm-offsets.h>
#include <asm/export.h>
#include <asm/nospec-branch.h>
#include <asm/unwind_hints.h>
#include <asm/percpu.h>
#include <asm/frame.h>
#include <asm/nops.h>

	.section .text.__x86.indirect_thunk


.macro POLINE reg
	ANNOTATE_INTRA_FUNCTION_CALL
	call    .Ldo_rop_\@
	int3
.Ldo_rop_\@:
	mov     %\reg, (%_ASM_SP)
	UNWIND_HINT_FUNC
.endm

.macro RETPOLINE reg
	POLINE \reg
	RET
.endm

.macro THUNK reg

	.align RETPOLINE_THUNK_SIZE
SYM_INNER_LABEL(__x86_indirect_thunk_\reg, SYM_L_GLOBAL)
	UNWIND_HINT_UNDEFINED
	ANNOTATE_NOENDBR

	ALTERNATIVE_2 __stringify(RETPOLINE \reg), \
		      __stringify(lfence; ANNOTATE_RETPOLINE_SAFE; jmp *%\reg; int3), X86_FEATURE_RETPOLINE_LFENCE, \
		      __stringify(ANNOTATE_RETPOLINE_SAFE; jmp *%\reg), ALT_NOT(X86_FEATURE_RETPOLINE)

.endm

/*
 * Despite being an assembler file we can't just use .irp here
 * because __KSYM_DEPS__ only uses the C preprocessor and would
 * only see one instance of "__x86_indirect_thunk_\reg" rather
 * than one per register with the correct names. So we do it
 * the simple and nasty way...
 *
 * Worse, you can only have a single EXPORT_SYMBOL per line,
 * and CPP can't insert newlines, so we have to repeat everything
 * at least twice.
 */

#define __EXPORT_THUNK(sym)	_ASM_NOKPROBE(sym); EXPORT_SYMBOL(sym)

	.align RETPOLINE_THUNK_SIZE
SYM_CODE_START(__x86_indirect_thunk_array)

#define GEN(reg) THUNK reg
#include <asm/GEN-for-each-reg.h>
#undef GEN

	.align RETPOLINE_THUNK_SIZE
SYM_CODE_END(__x86_indirect_thunk_array)

#define GEN(reg) __EXPORT_THUNK(__x86_indirect_thunk_ ## reg)
#include <asm/GEN-for-each-reg.h>
#undef GEN

#ifdef CONFIG_CALL_DEPTH_TRACKING
.macro CALL_THUNK reg
	.align RETPOLINE_THUNK_SIZE

SYM_INNER_LABEL(__x86_indirect_call_thunk_\reg, SYM_L_GLOBAL)
	UNWIND_HINT_UNDEFINED
	ANNOTATE_NOENDBR

	CALL_DEPTH_ACCOUNT
	POLINE \reg
	ANNOTATE_UNRET_SAFE
	ret
	int3
.endm

	.align RETPOLINE_THUNK_SIZE
SYM_CODE_START(__x86_indirect_call_thunk_array)

#define GEN(reg) CALL_THUNK reg
#include <asm/GEN-for-each-reg.h>
#undef GEN

	.align RETPOLINE_THUNK_SIZE
SYM_CODE_END(__x86_indirect_call_thunk_array)

#define GEN(reg) __EXPORT_THUNK(__x86_indirect_call_thunk_ ## reg)
#include <asm/GEN-for-each-reg.h>
#undef GEN

.macro JUMP_THUNK reg
	.align RETPOLINE_THUNK_SIZE

SYM_INNER_LABEL(__x86_indirect_jump_thunk_\reg, SYM_L_GLOBAL)
	UNWIND_HINT_UNDEFINED
	ANNOTATE_NOENDBR
	POLINE \reg
	ANNOTATE_UNRET_SAFE
	ret
	int3
.endm

	.align RETPOLINE_THUNK_SIZE
SYM_CODE_START(__x86_indirect_jump_thunk_array)

#define GEN(reg) JUMP_THUNK reg
#include <asm/GEN-for-each-reg.h>
#undef GEN

	.align RETPOLINE_THUNK_SIZE
SYM_CODE_END(__x86_indirect_jump_thunk_array)

#define GEN(reg) __EXPORT_THUNK(__x86_indirect_jump_thunk_ ## reg)
#include <asm/GEN-for-each-reg.h>
#undef GEN
#endif
/*
 * This function name is magical and is used by -mfunction-return=thunk-extern
 * for the compiler to generate JMPs to it.
 */
#ifdef CONFIG_RETHUNK

/*
 * srso_untrain_ret_alias() and srso_safe_ret_alias() are placed at
 * special addresses:
 *
 * - srso_untrain_ret_alias() is 2M aligned
 * - srso_safe_ret_alias() is also in the same 2M page but bits 2, 8, 14
 * and 20 in its virtual address are set (while those bits in the
 * srso_untrain_ret_alias() function are cleared).
 *
 * This guarantees that those two addresses will alias in the branch
 * target buffer of Zen3/4 generations, leading to any potential
 * poisoned entries at that BTB slot to get evicted.
 *
 * As a result, srso_safe_ret_alias() becomes a safe return.
 */
#ifdef CONFIG_CPU_SRSO
	.section .text.__x86.rethunk_untrain

SYM_START(srso_untrain_ret_alias, SYM_L_GLOBAL, SYM_A_NONE)
	ANNOTATE_NOENDBR
	ASM_NOP2
	lfence
	jmp __x86_return_thunk
SYM_FUNC_END(srso_untrain_ret_alias)
__EXPORT_THUNK(srso_untrain_ret_alias)

	.section .text.__x86.rethunk_safe
#endif

/* Needs a definition for the __x86_return_thunk alternative below. */
SYM_START(srso_safe_ret_alias, SYM_L_GLOBAL, SYM_A_NONE)
#ifdef CONFIG_CPU_SRSO
	add $8, %_ASM_SP
	UNWIND_HINT_FUNC
#endif
	ANNOTATE_UNRET_SAFE
	ret
	int3
SYM_FUNC_END(srso_safe_ret_alias)

	.section .text.__x86.return_thunk

/*
 * Safety details here pertain to the AMD Zen{1,2} microarchitecture:
 * 1) The RET at __x86_return_thunk must be on a 64 byte boundary, for
 *    alignment within the BTB.
 * 2) The instruction at zen_untrain_ret must contain, and not
 *    end with, the 0xc3 byte of the RET.
 * 3) STIBP must be enabled, or SMT disabled, to prevent the sibling thread
 *    from re-poisioning the BTB prediction.
 */
	.align 64
	.skip 64 - (__ret - zen_untrain_ret), 0xcc
SYM_START(zen_untrain_ret, SYM_L_GLOBAL, SYM_A_NONE)
	ANNOTATE_NOENDBR
	/*
	 * As executed from zen_untrain_ret, this is:
	 *
	 *   TEST $0xcc, %bl
	 *   LFENCE
	 *   JMP __x86_return_thunk
	 *
	 * Executing the TEST instruction has a side effect of evicting any BTB
	 * prediction (potentially attacker controlled) attached to the RET, as
	 * __x86_return_thunk + 1 isn't an instruction boundary at the moment.
	 */
	.byte	0xf6

	/*
	 * As executed from __x86_return_thunk, this is a plain RET.
	 *
	 * As part of the TEST above, RET is the ModRM byte, and INT3 the imm8.
	 *
	 * We subsequently jump backwards and architecturally execute the RET.
	 * This creates a correct BTB prediction (type=ret), but in the
	 * meantime we suffer Straight Line Speculation (because the type was
	 * no branch) which is halted by the INT3.
	 *
	 * With SMT enabled and STIBP active, a sibling thread cannot poison
	 * RET's prediction to a type of its choice, but can evict the
	 * prediction due to competitive sharing. If the prediction is
	 * evicted, __x86_return_thunk will suffer Straight Line Speculation
	 * which will be contained safely by the INT3.
	 */
SYM_INNER_LABEL(__ret, SYM_L_GLOBAL)
	ret
	int3
SYM_CODE_END(__ret)

	/*
	 * Ensure the TEST decoding / BTB invalidation is complete.
	 */
	lfence

	/*
	 * Jump back and execute the RET in the middle of the TEST instruction.
	 * INT3 is for SLS protection.
	 */
	jmp __ret
	int3
SYM_FUNC_END(zen_untrain_ret)
__EXPORT_THUNK(zen_untrain_ret)

/*
 * SRSO untraining sequence for Zen1/2, similar to zen_untrain_ret()
 * above. On kernel entry, srso_untrain_ret() is executed which is a
 *
 * movabs $0xccccccc308c48348,%rax
 *
 * and when the return thunk executes the inner label srso_safe_ret()
 * later, it is a stack manipulation and a RET which is mispredicted and
 * thus a "safe" one to use.
 */
	.align 64
	.skip 64 - (srso_safe_ret - srso_untrain_ret), 0xcc
SYM_START(srso_untrain_ret, SYM_L_GLOBAL, SYM_A_NONE)
	ANNOTATE_NOENDBR
	.byte 0x48, 0xb8

SYM_INNER_LABEL(srso_safe_ret, SYM_L_GLOBAL)
	add $8, %_ASM_SP
	ret
	int3
	int3
	int3
	lfence
	call srso_safe_ret
	int3
SYM_CODE_END(srso_safe_ret)
SYM_FUNC_END(srso_untrain_ret)
__EXPORT_THUNK(srso_untrain_ret)

SYM_FUNC_START(__x86_return_thunk)
	ALTERNATIVE_2 "jmp __ret", "call srso_safe_ret", X86_FEATURE_SRSO, \
			"call srso_safe_ret_alias", X86_FEATURE_SRSO_ALIAS
	int3
SYM_CODE_END(__x86_return_thunk)
EXPORT_SYMBOL(__x86_return_thunk)

#endif /* CONFIG_RETHUNK */

#ifdef CONFIG_CALL_DEPTH_TRACKING

	.align 64
SYM_FUNC_START(__x86_return_skl)
	ANNOTATE_NOENDBR
	/*
	 * Keep the hotpath in a 16byte I-fetch for the non-debug
	 * case.
	 */
	CALL_THUNKS_DEBUG_INC_RETS
	shlq	$5, PER_CPU_VAR(pcpu_hot + X86_call_depth)
	jz	1f
	ANNOTATE_UNRET_SAFE
	ret
	int3
1:
	CALL_THUNKS_DEBUG_INC_STUFFS
	.rept	16
	ANNOTATE_INTRA_FUNCTION_CALL
	call	2f
	int3
2:
	.endr
	add	$(8*16), %rsp

	CREDIT_CALL_DEPTH

	ANNOTATE_UNRET_SAFE
	ret
	int3
SYM_FUNC_END(__x86_return_skl)

#endif /* CONFIG_CALL_DEPTH_TRACKING */

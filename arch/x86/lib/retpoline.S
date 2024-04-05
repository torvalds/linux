/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/export.h>
#include <linux/stringify.h>
#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/asm-offsets.h>
#include <asm/nospec-branch.h>
#include <asm/unwind_hints.h>
#include <asm/percpu.h>
#include <asm/frame.h>
#include <asm/nops.h>

	.section .text..__x86.indirect_thunk


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

#ifdef CONFIG_MITIGATION_CALL_DEPTH_TRACKING
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

#ifdef CONFIG_MITIGATION_RETHUNK

/*
 * Be careful here: that label cannot really be removed because in
 * some configurations and toolchains, the JMP __x86_return_thunk the
 * compiler issues is either a short one or the compiler doesn't use
 * relocations for same-section JMPs and that breaks the returns
 * detection logic in apply_returns() and in objtool.
 */
	.section .text..__x86.return_thunk

#ifdef CONFIG_MITIGATION_SRSO

/*
 * srso_alias_untrain_ret() and srso_alias_safe_ret() are placed at
 * special addresses:
 *
 * - srso_alias_untrain_ret() is 2M aligned
 * - srso_alias_safe_ret() is also in the same 2M page but bits 2, 8, 14
 * and 20 in its virtual address are set (while those bits in the
 * srso_alias_untrain_ret() function are cleared).
 *
 * This guarantees that those two addresses will alias in the branch
 * target buffer of Zen3/4 generations, leading to any potential
 * poisoned entries at that BTB slot to get evicted.
 *
 * As a result, srso_alias_safe_ret() becomes a safe return.
 */
	.pushsection .text..__x86.rethunk_untrain
SYM_CODE_START_NOALIGN(srso_alias_untrain_ret)
	UNWIND_HINT_FUNC
	ANNOTATE_NOENDBR
	ASM_NOP2
	lfence
	jmp srso_alias_return_thunk
SYM_FUNC_END(srso_alias_untrain_ret)
__EXPORT_THUNK(srso_alias_untrain_ret)
	.popsection

	.pushsection .text..__x86.rethunk_safe
SYM_CODE_START_NOALIGN(srso_alias_safe_ret)
	lea 8(%_ASM_SP), %_ASM_SP
	UNWIND_HINT_FUNC
	ANNOTATE_UNRET_SAFE
	ret
	int3
SYM_FUNC_END(srso_alias_safe_ret)

SYM_CODE_START_NOALIGN(srso_alias_return_thunk)
	UNWIND_HINT_FUNC
	ANNOTATE_NOENDBR
	call srso_alias_safe_ret
	ud2
SYM_CODE_END(srso_alias_return_thunk)
	.popsection

/*
 * SRSO untraining sequence for Zen1/2, similar to retbleed_untrain_ret()
 * above. On kernel entry, srso_untrain_ret() is executed which is a
 *
 * movabs $0xccccc30824648d48,%rax
 *
 * and when the return thunk executes the inner label srso_safe_ret()
 * later, it is a stack manipulation and a RET which is mispredicted and
 * thus a "safe" one to use.
 */
	.align 64
	.skip 64 - (srso_safe_ret - srso_untrain_ret), 0xcc
SYM_CODE_START_LOCAL_NOALIGN(srso_untrain_ret)
	ANNOTATE_NOENDBR
	.byte 0x48, 0xb8

/*
 * This forces the function return instruction to speculate into a trap
 * (UD2 in srso_return_thunk() below).  This RET will then mispredict
 * and execution will continue at the return site read from the top of
 * the stack.
 */
SYM_INNER_LABEL(srso_safe_ret, SYM_L_GLOBAL)
	lea 8(%_ASM_SP), %_ASM_SP
	ret
	int3
	int3
	/* end of movabs */
	lfence
	call srso_safe_ret
	ud2
SYM_CODE_END(srso_safe_ret)
SYM_FUNC_END(srso_untrain_ret)

SYM_CODE_START(srso_return_thunk)
	UNWIND_HINT_FUNC
	ANNOTATE_NOENDBR
	call srso_safe_ret
	ud2
SYM_CODE_END(srso_return_thunk)

#define JMP_SRSO_UNTRAIN_RET "jmp srso_untrain_ret"
#else /* !CONFIG_MITIGATION_SRSO */
/* Dummy for the alternative in CALL_UNTRAIN_RET. */
SYM_CODE_START(srso_alias_untrain_ret)
	ANNOTATE_UNRET_SAFE
	ret
	int3
SYM_FUNC_END(srso_alias_untrain_ret)
__EXPORT_THUNK(srso_alias_untrain_ret)
#define JMP_SRSO_UNTRAIN_RET "ud2"
#endif /* CONFIG_MITIGATION_SRSO */

#ifdef CONFIG_MITIGATION_UNRET_ENTRY

/*
 * Some generic notes on the untraining sequences:
 *
 * They are interchangeable when it comes to flushing potentially wrong
 * RET predictions from the BTB.
 *
 * The SRSO Zen1/2 (MOVABS) untraining sequence is longer than the
 * Retbleed sequence because the return sequence done there
 * (srso_safe_ret()) is longer and the return sequence must fully nest
 * (end before) the untraining sequence. Therefore, the untraining
 * sequence must fully overlap the return sequence.
 *
 * Regarding alignment - the instructions which need to be untrained,
 * must all start at a cacheline boundary for Zen1/2 generations. That
 * is, instruction sequences starting at srso_safe_ret() and
 * the respective instruction sequences at retbleed_return_thunk()
 * must start at a cacheline boundary.
 */

/*
 * Safety details here pertain to the AMD Zen{1,2} microarchitecture:
 * 1) The RET at retbleed_return_thunk must be on a 64 byte boundary, for
 *    alignment within the BTB.
 * 2) The instruction at retbleed_untrain_ret must contain, and not
 *    end with, the 0xc3 byte of the RET.
 * 3) STIBP must be enabled, or SMT disabled, to prevent the sibling thread
 *    from re-poisioning the BTB prediction.
 */
	.align 64
	.skip 64 - (retbleed_return_thunk - retbleed_untrain_ret), 0xcc
SYM_CODE_START_LOCAL_NOALIGN(retbleed_untrain_ret)
	ANNOTATE_NOENDBR
	/*
	 * As executed from retbleed_untrain_ret, this is:
	 *
	 *   TEST $0xcc, %bl
	 *   LFENCE
	 *   JMP retbleed_return_thunk
	 *
	 * Executing the TEST instruction has a side effect of evicting any BTB
	 * prediction (potentially attacker controlled) attached to the RET, as
	 * retbleed_return_thunk + 1 isn't an instruction boundary at the moment.
	 */
	.byte	0xf6

	/*
	 * As executed from retbleed_return_thunk, this is a plain RET.
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
	 * evicted, retbleed_return_thunk will suffer Straight Line Speculation
	 * which will be contained safely by the INT3.
	 */
SYM_INNER_LABEL(retbleed_return_thunk, SYM_L_GLOBAL)
	ret
	int3
SYM_CODE_END(retbleed_return_thunk)

	/*
	 * Ensure the TEST decoding / BTB invalidation is complete.
	 */
	lfence

	/*
	 * Jump back and execute the RET in the middle of the TEST instruction.
	 * INT3 is for SLS protection.
	 */
	jmp retbleed_return_thunk
	int3
SYM_FUNC_END(retbleed_untrain_ret)

#define JMP_RETBLEED_UNTRAIN_RET "jmp retbleed_untrain_ret"
#else /* !CONFIG_MITIGATION_UNRET_ENTRY */
#define JMP_RETBLEED_UNTRAIN_RET "ud2"
#endif /* CONFIG_MITIGATION_UNRET_ENTRY */

#if defined(CONFIG_MITIGATION_UNRET_ENTRY) || defined(CONFIG_MITIGATION_SRSO)

SYM_FUNC_START(entry_untrain_ret)
	ALTERNATIVE JMP_RETBLEED_UNTRAIN_RET, JMP_SRSO_UNTRAIN_RET, X86_FEATURE_SRSO
SYM_FUNC_END(entry_untrain_ret)
__EXPORT_THUNK(entry_untrain_ret)

#endif /* CONFIG_MITIGATION_UNRET_ENTRY || CONFIG_MITIGATION_SRSO */

#ifdef CONFIG_MITIGATION_CALL_DEPTH_TRACKING

	.align 64
SYM_FUNC_START(call_depth_return_thunk)
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
SYM_FUNC_END(call_depth_return_thunk)

#endif /* CONFIG_MITIGATION_CALL_DEPTH_TRACKING */

/*
 * This function name is magical and is used by -mfunction-return=thunk-extern
 * for the compiler to generate JMPs to it.
 *
 * This code is only used during kernel boot or module init.  All
 * 'JMP __x86_return_thunk' sites are changed to something else by
 * apply_returns().
 *
 * The ALTERNATIVE below adds a really loud warning to catch the case
 * where the insufficient default return thunk ends up getting used for
 * whatever reason like miscompilation or failure of
 * objtool/alternatives/etc to patch all the return sites.
 */
SYM_CODE_START(__x86_return_thunk)
	UNWIND_HINT_FUNC
	ANNOTATE_NOENDBR
	ALTERNATIVE __stringify(ANNOTATE_UNRET_SAFE; ret), \
		   "jmp warn_thunk_thunk", X86_FEATURE_ALWAYS
	int3
SYM_CODE_END(__x86_return_thunk)
EXPORT_SYMBOL(__x86_return_thunk)

#endif /* CONFIG_MITIGATION_RETHUNK */

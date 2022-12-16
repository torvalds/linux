/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_X86_NOSPEC_BRANCH_H_
#define _ASM_X86_NOSPEC_BRANCH_H_

#include <linux/static_key.h>
#include <linux/objtool.h>
#include <linux/linkage.h>

#include <asm/alternative.h>
#include <asm/cpufeatures.h>
#include <asm/msr-index.h>
#include <asm/unwind_hints.h>
#include <asm/percpu.h>
#include <asm/current.h>

/*
 * Call depth tracking for Intel SKL CPUs to address the RSB underflow
 * issue in software.
 *
 * The tracking does not use a counter. It uses uses arithmetic shift
 * right on call entry and logical shift left on return.
 *
 * The depth tracking variable is initialized to 0x8000.... when the call
 * depth is zero. The arithmetic shift right sign extends the MSB and
 * saturates after the 12th call. The shift count is 5 for both directions
 * so the tracking covers 12 nested calls.
 *
 *  Call
 *  0: 0x8000000000000000	0x0000000000000000
 *  1: 0xfc00000000000000	0xf000000000000000
 * ...
 * 11: 0xfffffffffffffff8	0xfffffffffffffc00
 * 12: 0xffffffffffffffff	0xffffffffffffffe0
 *
 * After a return buffer fill the depth is credited 12 calls before the
 * next stuffing has to take place.
 *
 * There is a inaccuracy for situations like this:
 *
 *  10 calls
 *   5 returns
 *   3 calls
 *   4 returns
 *   3 calls
 *   ....
 *
 * The shift count might cause this to be off by one in either direction,
 * but there is still a cushion vs. the RSB depth. The algorithm does not
 * claim to be perfect and it can be speculated around by the CPU, but it
 * is considered that it obfuscates the problem enough to make exploitation
 * extremly difficult.
 */
#define RET_DEPTH_SHIFT			5
#define RSB_RET_STUFF_LOOPS		16
#define RET_DEPTH_INIT			0x8000000000000000ULL
#define RET_DEPTH_INIT_FROM_CALL	0xfc00000000000000ULL
#define RET_DEPTH_CREDIT		0xffffffffffffffffULL

#ifdef CONFIG_CALL_THUNKS_DEBUG
# define CALL_THUNKS_DEBUG_INC_CALLS				\
	incq	%gs:__x86_call_count;
# define CALL_THUNKS_DEBUG_INC_RETS				\
	incq	%gs:__x86_ret_count;
# define CALL_THUNKS_DEBUG_INC_STUFFS				\
	incq	%gs:__x86_stuffs_count;
# define CALL_THUNKS_DEBUG_INC_CTXSW				\
	incq	%gs:__x86_ctxsw_count;
#else
# define CALL_THUNKS_DEBUG_INC_CALLS
# define CALL_THUNKS_DEBUG_INC_RETS
# define CALL_THUNKS_DEBUG_INC_STUFFS
# define CALL_THUNKS_DEBUG_INC_CTXSW
#endif

#if defined(CONFIG_CALL_DEPTH_TRACKING) && !defined(COMPILE_OFFSETS)

#include <asm/asm-offsets.h>

#define CREDIT_CALL_DEPTH					\
	movq	$-1, PER_CPU_VAR(pcpu_hot + X86_call_depth);

#define ASM_CREDIT_CALL_DEPTH					\
	movq	$-1, PER_CPU_VAR(pcpu_hot + X86_call_depth);

#define RESET_CALL_DEPTH					\
	mov	$0x80, %rax;					\
	shl	$56, %rax;					\
	movq	%rax, PER_CPU_VAR(pcpu_hot + X86_call_depth);

#define RESET_CALL_DEPTH_FROM_CALL				\
	mov	$0xfc, %rax;					\
	shl	$56, %rax;					\
	movq	%rax, PER_CPU_VAR(pcpu_hot + X86_call_depth);	\
	CALL_THUNKS_DEBUG_INC_CALLS

#define INCREMENT_CALL_DEPTH					\
	sarq	$5, %gs:pcpu_hot + X86_call_depth;		\
	CALL_THUNKS_DEBUG_INC_CALLS

#define ASM_INCREMENT_CALL_DEPTH				\
	sarq	$5, PER_CPU_VAR(pcpu_hot + X86_call_depth);	\
	CALL_THUNKS_DEBUG_INC_CALLS

#else
#define CREDIT_CALL_DEPTH
#define ASM_CREDIT_CALL_DEPTH
#define RESET_CALL_DEPTH
#define INCREMENT_CALL_DEPTH
#define ASM_INCREMENT_CALL_DEPTH
#define RESET_CALL_DEPTH_FROM_CALL
#endif

/*
 * Fill the CPU return stack buffer.
 *
 * Each entry in the RSB, if used for a speculative 'ret', contains an
 * infinite 'pause; lfence; jmp' loop to capture speculative execution.
 *
 * This is required in various cases for retpoline and IBRS-based
 * mitigations for the Spectre variant 2 vulnerability. Sometimes to
 * eliminate potentially bogus entries from the RSB, and sometimes
 * purely to ensure that it doesn't get empty, which on some CPUs would
 * allow predictions from other (unwanted!) sources to be used.
 *
 * We define a CPP macro such that it can be used from both .S files and
 * inline assembly. It's possible to do a .macro and then include that
 * from C via asm(".include <asm/nospec-branch.h>") but let's not go there.
 */

#define RETPOLINE_THUNK_SIZE	32
#define RSB_CLEAR_LOOPS		32	/* To forcibly overwrite all entries */

/*
 * Common helper for __FILL_RETURN_BUFFER and __FILL_ONE_RETURN.
 */
#define __FILL_RETURN_SLOT			\
	ANNOTATE_INTRA_FUNCTION_CALL;		\
	call	772f;				\
	int3;					\
772:

/*
 * Stuff the entire RSB.
 *
 * Google experimented with loop-unrolling and this turned out to be
 * the optimal version - two calls, each with their own speculation
 * trap should their return address end up getting used, in a loop.
 */
#ifdef CONFIG_X86_64
#define __FILL_RETURN_BUFFER(reg, nr)			\
	mov	$(nr/2), reg;				\
771:							\
	__FILL_RETURN_SLOT				\
	__FILL_RETURN_SLOT				\
	add	$(BITS_PER_LONG/8) * 2, %_ASM_SP;	\
	dec	reg;					\
	jnz	771b;					\
	/* barrier for jnz misprediction */		\
	lfence;						\
	ASM_CREDIT_CALL_DEPTH				\
	CALL_THUNKS_DEBUG_INC_CTXSW
#else
/*
 * i386 doesn't unconditionally have LFENCE, as such it can't
 * do a loop.
 */
#define __FILL_RETURN_BUFFER(reg, nr)			\
	.rept nr;					\
	__FILL_RETURN_SLOT;				\
	.endr;						\
	add	$(BITS_PER_LONG/8) * nr, %_ASM_SP;
#endif

/*
 * Stuff a single RSB slot.
 *
 * To mitigate Post-Barrier RSB speculation, one CALL instruction must be
 * forced to retire before letting a RET instruction execute.
 *
 * On PBRSB-vulnerable CPUs, it is not safe for a RET to be executed
 * before this point.
 */
#define __FILL_ONE_RETURN				\
	__FILL_RETURN_SLOT				\
	add	$(BITS_PER_LONG/8), %_ASM_SP;		\
	lfence;

#ifdef __ASSEMBLY__

/*
 * This should be used immediately before an indirect jump/call. It tells
 * objtool the subsequent indirect jump/call is vouched safe for retpoline
 * builds.
 */
.macro ANNOTATE_RETPOLINE_SAFE
	.Lannotate_\@:
	.pushsection .discard.retpoline_safe
	_ASM_PTR .Lannotate_\@
	.popsection
.endm

/*
 * (ab)use RETPOLINE_SAFE on RET to annotate away 'bare' RET instructions
 * vs RETBleed validation.
 */
#define ANNOTATE_UNRET_SAFE ANNOTATE_RETPOLINE_SAFE

/*
 * Abuse ANNOTATE_RETPOLINE_SAFE on a NOP to indicate UNRET_END, should
 * eventually turn into it's own annotation.
 */
.macro ANNOTATE_UNRET_END
#ifdef CONFIG_DEBUG_ENTRY
	ANNOTATE_RETPOLINE_SAFE
	nop
#endif
.endm

/*
 * Equivalent to -mindirect-branch-cs-prefix; emit the 5 byte jmp/call
 * to the retpoline thunk with a CS prefix when the register requires
 * a RAX prefix byte to encode. Also see apply_retpolines().
 */
.macro __CS_PREFIX reg:req
	.irp rs,r8,r9,r10,r11,r12,r13,r14,r15
	.ifc \reg,\rs
	.byte 0x2e
	.endif
	.endr
.endm

/*
 * JMP_NOSPEC and CALL_NOSPEC macros can be used instead of a simple
 * indirect jmp/call which may be susceptible to the Spectre variant 2
 * attack.
 */
.macro JMP_NOSPEC reg:req
#ifdef CONFIG_RETPOLINE
	__CS_PREFIX \reg
	jmp	__x86_indirect_thunk_\reg
#else
	jmp	*%\reg
	int3
#endif
.endm

.macro CALL_NOSPEC reg:req
#ifdef CONFIG_RETPOLINE
	__CS_PREFIX \reg
	call	__x86_indirect_thunk_\reg
#else
	call	*%\reg
#endif
.endm

 /*
  * A simpler FILL_RETURN_BUFFER macro. Don't make people use the CPP
  * monstrosity above, manually.
  */
.macro FILL_RETURN_BUFFER reg:req nr:req ftr:req ftr2=ALT_NOT(X86_FEATURE_ALWAYS)
	ALTERNATIVE_2 "jmp .Lskip_rsb_\@", \
		__stringify(__FILL_RETURN_BUFFER(\reg,\nr)), \ftr, \
		__stringify(__FILL_ONE_RETURN), \ftr2

.Lskip_rsb_\@:
.endm

#ifdef CONFIG_CPU_UNRET_ENTRY
#define CALL_ZEN_UNTRAIN_RET	"call zen_untrain_ret"
#else
#define CALL_ZEN_UNTRAIN_RET	""
#endif

/*
 * Mitigate RETBleed for AMD/Hygon Zen uarch. Requires KERNEL CR3 because the
 * return thunk isn't mapped into the userspace tables (then again, AMD
 * typically has NO_MELTDOWN).
 *
 * While zen_untrain_ret() doesn't clobber anything but requires stack,
 * entry_ibpb() will clobber AX, CX, DX.
 *
 * As such, this must be placed after every *SWITCH_TO_KERNEL_CR3 at a point
 * where we have a stack but before any RET instruction.
 */
.macro UNTRAIN_RET
#if defined(CONFIG_CPU_UNRET_ENTRY) || defined(CONFIG_CPU_IBPB_ENTRY) || \
	defined(CONFIG_CALL_DEPTH_TRACKING)
	ANNOTATE_UNRET_END
	ALTERNATIVE_3 "",						\
		      CALL_ZEN_UNTRAIN_RET, X86_FEATURE_UNRET,		\
		      "call entry_ibpb", X86_FEATURE_ENTRY_IBPB,	\
		      __stringify(RESET_CALL_DEPTH), X86_FEATURE_CALL_DEPTH
#endif
.endm

.macro UNTRAIN_RET_FROM_CALL
#if defined(CONFIG_CPU_UNRET_ENTRY) || defined(CONFIG_CPU_IBPB_ENTRY) || \
	defined(CONFIG_CALL_DEPTH_TRACKING)
	ANNOTATE_UNRET_END
	ALTERNATIVE_3 "",						\
		      CALL_ZEN_UNTRAIN_RET, X86_FEATURE_UNRET,		\
		      "call entry_ibpb", X86_FEATURE_ENTRY_IBPB,	\
		      __stringify(RESET_CALL_DEPTH_FROM_CALL), X86_FEATURE_CALL_DEPTH
#endif
.endm


.macro CALL_DEPTH_ACCOUNT
#ifdef CONFIG_CALL_DEPTH_TRACKING
	ALTERNATIVE "",							\
		    __stringify(ASM_INCREMENT_CALL_DEPTH), X86_FEATURE_CALL_DEPTH
#endif
.endm

#else /* __ASSEMBLY__ */

#define ANNOTATE_RETPOLINE_SAFE					\
	"999:\n\t"						\
	".pushsection .discard.retpoline_safe\n\t"		\
	_ASM_PTR " 999b\n\t"					\
	".popsection\n\t"

typedef u8 retpoline_thunk_t[RETPOLINE_THUNK_SIZE];
extern retpoline_thunk_t __x86_indirect_thunk_array[];
extern retpoline_thunk_t __x86_indirect_call_thunk_array[];
extern retpoline_thunk_t __x86_indirect_jump_thunk_array[];

extern void __x86_return_thunk(void);
extern void zen_untrain_ret(void);
extern void entry_ibpb(void);

#ifdef CONFIG_CALL_THUNKS
extern void (*x86_return_thunk)(void);
#else
#define x86_return_thunk	(&__x86_return_thunk)
#endif

#ifdef CONFIG_CALL_DEPTH_TRACKING
extern void __x86_return_skl(void);

static inline void x86_set_skl_return_thunk(void)
{
	x86_return_thunk = &__x86_return_skl;
}

#define CALL_DEPTH_ACCOUNT					\
	ALTERNATIVE("",						\
		    __stringify(INCREMENT_CALL_DEPTH),		\
		    X86_FEATURE_CALL_DEPTH)

#ifdef CONFIG_CALL_THUNKS_DEBUG
DECLARE_PER_CPU(u64, __x86_call_count);
DECLARE_PER_CPU(u64, __x86_ret_count);
DECLARE_PER_CPU(u64, __x86_stuffs_count);
DECLARE_PER_CPU(u64, __x86_ctxsw_count);
#endif
#else
static inline void x86_set_skl_return_thunk(void) {}

#define CALL_DEPTH_ACCOUNT ""

#endif

#ifdef CONFIG_RETPOLINE

#define GEN(reg) \
	extern retpoline_thunk_t __x86_indirect_thunk_ ## reg;
#include <asm/GEN-for-each-reg.h>
#undef GEN

#define GEN(reg)						\
	extern retpoline_thunk_t __x86_indirect_call_thunk_ ## reg;
#include <asm/GEN-for-each-reg.h>
#undef GEN

#define GEN(reg)						\
	extern retpoline_thunk_t __x86_indirect_jump_thunk_ ## reg;
#include <asm/GEN-for-each-reg.h>
#undef GEN

#ifdef CONFIG_X86_64

/*
 * Inline asm uses the %V modifier which is only in newer GCC
 * which is ensured when CONFIG_RETPOLINE is defined.
 */
# define CALL_NOSPEC						\
	ALTERNATIVE_2(						\
	ANNOTATE_RETPOLINE_SAFE					\
	"call *%[thunk_target]\n",				\
	"call __x86_indirect_thunk_%V[thunk_target]\n",		\
	X86_FEATURE_RETPOLINE,					\
	"lfence;\n"						\
	ANNOTATE_RETPOLINE_SAFE					\
	"call *%[thunk_target]\n",				\
	X86_FEATURE_RETPOLINE_LFENCE)

# define THUNK_TARGET(addr) [thunk_target] "r" (addr)

#else /* CONFIG_X86_32 */
/*
 * For i386 we use the original ret-equivalent retpoline, because
 * otherwise we'll run out of registers. We don't care about CET
 * here, anyway.
 */
# define CALL_NOSPEC						\
	ALTERNATIVE_2(						\
	ANNOTATE_RETPOLINE_SAFE					\
	"call *%[thunk_target]\n",				\
	"       jmp    904f;\n"					\
	"       .align 16\n"					\
	"901:	call   903f;\n"					\
	"902:	pause;\n"					\
	"    	lfence;\n"					\
	"       jmp    902b;\n"					\
	"       .align 16\n"					\
	"903:	lea    4(%%esp), %%esp;\n"			\
	"       pushl  %[thunk_target];\n"			\
	"       ret;\n"						\
	"       .align 16\n"					\
	"904:	call   901b;\n",				\
	X86_FEATURE_RETPOLINE,					\
	"lfence;\n"						\
	ANNOTATE_RETPOLINE_SAFE					\
	"call *%[thunk_target]\n",				\
	X86_FEATURE_RETPOLINE_LFENCE)

# define THUNK_TARGET(addr) [thunk_target] "rm" (addr)
#endif
#else /* No retpoline for C / inline asm */
# define CALL_NOSPEC "call *%[thunk_target]\n"
# define THUNK_TARGET(addr) [thunk_target] "rm" (addr)
#endif

/* The Spectre V2 mitigation variants */
enum spectre_v2_mitigation {
	SPECTRE_V2_NONE,
	SPECTRE_V2_RETPOLINE,
	SPECTRE_V2_LFENCE,
	SPECTRE_V2_EIBRS,
	SPECTRE_V2_EIBRS_RETPOLINE,
	SPECTRE_V2_EIBRS_LFENCE,
	SPECTRE_V2_IBRS,
};

/* The indirect branch speculation control variants */
enum spectre_v2_user_mitigation {
	SPECTRE_V2_USER_NONE,
	SPECTRE_V2_USER_STRICT,
	SPECTRE_V2_USER_STRICT_PREFERRED,
	SPECTRE_V2_USER_PRCTL,
	SPECTRE_V2_USER_SECCOMP,
};

/* The Speculative Store Bypass disable variants */
enum ssb_mitigation {
	SPEC_STORE_BYPASS_NONE,
	SPEC_STORE_BYPASS_DISABLE,
	SPEC_STORE_BYPASS_PRCTL,
	SPEC_STORE_BYPASS_SECCOMP,
};

extern char __indirect_thunk_start[];
extern char __indirect_thunk_end[];

static __always_inline
void alternative_msr_write(unsigned int msr, u64 val, unsigned int feature)
{
	asm volatile(ALTERNATIVE("", "wrmsr", %c[feature])
		: : "c" (msr),
		    "a" ((u32)val),
		    "d" ((u32)(val >> 32)),
		    [feature] "i" (feature)
		: "memory");
}

static inline void indirect_branch_prediction_barrier(void)
{
	u64 val = PRED_CMD_IBPB;

	alternative_msr_write(MSR_IA32_PRED_CMD, val, X86_FEATURE_USE_IBPB);
}

/* The Intel SPEC CTRL MSR base value cache */
extern u64 x86_spec_ctrl_base;
DECLARE_PER_CPU(u64, x86_spec_ctrl_current);
extern void update_spec_ctrl_cond(u64 val);
extern u64 spec_ctrl_current(void);

/*
 * With retpoline, we must use IBRS to restrict branch prediction
 * before calling into firmware.
 *
 * (Implemented as CPP macros due to header hell.)
 */
#define firmware_restrict_branch_speculation_start()			\
do {									\
	preempt_disable();						\
	alternative_msr_write(MSR_IA32_SPEC_CTRL,			\
			      spec_ctrl_current() | SPEC_CTRL_IBRS,	\
			      X86_FEATURE_USE_IBRS_FW);			\
	alternative_msr_write(MSR_IA32_PRED_CMD, PRED_CMD_IBPB,		\
			      X86_FEATURE_USE_IBPB_FW);			\
} while (0)

#define firmware_restrict_branch_speculation_end()			\
do {									\
	alternative_msr_write(MSR_IA32_SPEC_CTRL,			\
			      spec_ctrl_current(),			\
			      X86_FEATURE_USE_IBRS_FW);			\
	preempt_enable();						\
} while (0)

DECLARE_STATIC_KEY_FALSE(switch_to_cond_stibp);
DECLARE_STATIC_KEY_FALSE(switch_mm_cond_ibpb);
DECLARE_STATIC_KEY_FALSE(switch_mm_always_ibpb);

DECLARE_STATIC_KEY_FALSE(mds_user_clear);
DECLARE_STATIC_KEY_FALSE(mds_idle_clear);

DECLARE_STATIC_KEY_FALSE(switch_mm_cond_l1d_flush);

DECLARE_STATIC_KEY_FALSE(mmio_stale_data_clear);

#include <asm/segment.h>

/**
 * mds_clear_cpu_buffers - Mitigation for MDS and TAA vulnerability
 *
 * This uses the otherwise unused and obsolete VERW instruction in
 * combination with microcode which triggers a CPU buffer flush when the
 * instruction is executed.
 */
static __always_inline void mds_clear_cpu_buffers(void)
{
	static const u16 ds = __KERNEL_DS;

	/*
	 * Has to be the memory-operand variant because only that
	 * guarantees the CPU buffer flush functionality according to
	 * documentation. The register-operand variant does not.
	 * Works with any segment selector, but a valid writable
	 * data segment is the fastest variant.
	 *
	 * "cc" clobber is required because VERW modifies ZF.
	 */
	asm volatile("verw %[ds]" : : [ds] "m" (ds) : "cc");
}

/**
 * mds_user_clear_cpu_buffers - Mitigation for MDS and TAA vulnerability
 *
 * Clear CPU buffers if the corresponding static key is enabled
 */
static __always_inline void mds_user_clear_cpu_buffers(void)
{
	if (static_branch_likely(&mds_user_clear))
		mds_clear_cpu_buffers();
}

/**
 * mds_idle_clear_cpu_buffers - Mitigation for MDS vulnerability
 *
 * Clear CPU buffers if the corresponding static key is enabled
 */
static inline void mds_idle_clear_cpu_buffers(void)
{
	if (static_branch_likely(&mds_idle_clear))
		mds_clear_cpu_buffers();
}

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_NOSPEC_BRANCH_H_ */

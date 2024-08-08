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

#define RETPOLINE_THUNK_SIZE	32

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
	lfence;
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
#if (defined(CONFIG_CPU_UNRET_ENTRY) || defined(CONFIG_CPU_SRSO))
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
#define CALL_UNTRAIN_RET	"call entry_untrain_ret"
#else
#define CALL_UNTRAIN_RET	""
#endif

/*
 * Mitigate RETBleed for AMD/Hygon Zen uarch. Requires KERNEL CR3 because the
 * return thunk isn't mapped into the userspace tables (then again, AMD
 * typically has NO_MELTDOWN).
 *
 * While retbleed_untrain_ret() doesn't clobber anything but requires stack,
 * entry_ibpb() will clobber AX, CX, DX.
 *
 * As such, this must be placed after every *SWITCH_TO_KERNEL_CR3 at a point
 * where we have a stack but before any RET instruction.
 */
.macro UNTRAIN_RET
#if defined(CONFIG_CPU_UNRET_ENTRY) || defined(CONFIG_CPU_IBPB_ENTRY) || \
	defined(CONFIG_CPU_SRSO)
	ANNOTATE_UNRET_END
	ALTERNATIVE_2 "",						\
		      CALL_UNTRAIN_RET, X86_FEATURE_UNRET,		\
		      "call entry_ibpb", X86_FEATURE_ENTRY_IBPB
#endif
.endm

/*
 * Macro to execute VERW instruction that mitigate transient data sampling
 * attacks such as MDS. On affected systems a microcode update overloaded VERW
 * instruction to also clear the CPU buffers. VERW clobbers CFLAGS.ZF.
 *
 * Note: Only the memory operand variant of VERW clears the CPU buffers.
 */
.macro CLEAR_CPU_BUFFERS
	ALTERNATIVE "jmp .Lskip_verw_\@", "", X86_FEATURE_CLEAR_CPU_BUF
	verw _ASM_RIP(mds_verw_sel)
.Lskip_verw_\@:
.endm

#else /* __ASSEMBLY__ */

#define ANNOTATE_RETPOLINE_SAFE					\
	"999:\n\t"						\
	".pushsection .discard.retpoline_safe\n\t"		\
	_ASM_PTR " 999b\n\t"					\
	".popsection\n\t"

typedef u8 retpoline_thunk_t[RETPOLINE_THUNK_SIZE];
extern retpoline_thunk_t __x86_indirect_thunk_array[];

#ifdef CONFIG_RETHUNK
extern void __x86_return_thunk(void);
#else
static inline void __x86_return_thunk(void) {}
#endif

extern void retbleed_return_thunk(void);
extern void srso_return_thunk(void);
extern void srso_alias_return_thunk(void);

extern void retbleed_untrain_ret(void);
extern void srso_untrain_ret(void);
extern void srso_alias_untrain_ret(void);

extern void entry_untrain_ret(void);
extern void entry_ibpb(void);

extern void (*x86_return_thunk)(void);

#ifdef CONFIG_RETPOLINE

#define GEN(reg) \
	extern retpoline_thunk_t __x86_indirect_thunk_ ## reg;
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

extern u64 x86_pred_cmd;

static inline void indirect_branch_prediction_barrier(void)
{
	alternative_msr_write(MSR_IA32_PRED_CMD, x86_pred_cmd, X86_FEATURE_USE_IBPB);
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

DECLARE_STATIC_KEY_FALSE(mds_idle_clear);

DECLARE_STATIC_KEY_FALSE(switch_mm_cond_l1d_flush);

DECLARE_STATIC_KEY_FALSE(mmio_stale_data_clear);

extern u16 mds_verw_sel;

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

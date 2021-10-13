/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __X86_KERNEL_FPU_XSTATE_H
#define __X86_KERNEL_FPU_XSTATE_H

#include <asm/cpufeature.h>
#include <asm/fpu/xstate.h>

static inline void xstate_init_xcomp_bv(struct xregs_state *xsave, u64 mask)
{
	/*
	 * XRSTORS requires these bits set in xcomp_bv, or it will
	 * trigger #GP:
	 */
	if (cpu_feature_enabled(X86_FEATURE_XSAVES))
		xsave->header.xcomp_bv = mask | XCOMP_BV_COMPACTED_FORMAT;
}

extern void __copy_xstate_to_uabi_buf(struct membuf to, struct fpstate *fpstate,
				      u32 pkru_val, enum xstate_copy_mode copy_mode);

extern void fpu__init_cpu_xstate(void);
extern void fpu__init_system_xstate(void);

extern void *get_xsave_addr(struct xregs_state *xsave, int xfeature_nr);

/* XSAVE/XRSTOR wrapper functions */

#ifdef CONFIG_X86_64
#define REX_PREFIX	"0x48, "
#else
#define REX_PREFIX
#endif

/* These macros all use (%edi)/(%rdi) as the single memory argument. */
#define XSAVE		".byte " REX_PREFIX "0x0f,0xae,0x27"
#define XSAVEOPT	".byte " REX_PREFIX "0x0f,0xae,0x37"
#define XSAVES		".byte " REX_PREFIX "0x0f,0xc7,0x2f"
#define XRSTOR		".byte " REX_PREFIX "0x0f,0xae,0x2f"
#define XRSTORS		".byte " REX_PREFIX "0x0f,0xc7,0x1f"

/*
 * After this @err contains 0 on success or the trap number when the
 * operation raises an exception.
 */
#define XSTATE_OP(op, st, lmask, hmask, err)				\
	asm volatile("1:" op "\n\t"					\
		     "xor %[err], %[err]\n"				\
		     "2:\n\t"						\
		     _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_FAULT_MCE_SAFE)	\
		     : [err] "=a" (err)					\
		     : "D" (st), "m" (*st), "a" (lmask), "d" (hmask)	\
		     : "memory")

/*
 * If XSAVES is enabled, it replaces XSAVEOPT because it supports a compact
 * format and supervisor states in addition to modified optimization in
 * XSAVEOPT.
 *
 * Otherwise, if XSAVEOPT is enabled, XSAVEOPT replaces XSAVE because XSAVEOPT
 * supports modified optimization which is not supported by XSAVE.
 *
 * We use XSAVE as a fallback.
 *
 * The 661 label is defined in the ALTERNATIVE* macros as the address of the
 * original instruction which gets replaced. We need to use it here as the
 * address of the instruction where we might get an exception at.
 */
#define XSTATE_XSAVE(st, lmask, hmask, err)				\
	asm volatile(ALTERNATIVE_2(XSAVE,				\
				   XSAVEOPT, X86_FEATURE_XSAVEOPT,	\
				   XSAVES,   X86_FEATURE_XSAVES)	\
		     "\n"						\
		     "xor %[err], %[err]\n"				\
		     "3:\n"						\
		     ".pushsection .fixup,\"ax\"\n"			\
		     "4: movl $-2, %[err]\n"				\
		     "jmp 3b\n"						\
		     ".popsection\n"					\
		     _ASM_EXTABLE(661b, 4b)				\
		     : [err] "=r" (err)					\
		     : "D" (st), "m" (*st), "a" (lmask), "d" (hmask)	\
		     : "memory")

/*
 * Use XRSTORS to restore context if it is enabled. XRSTORS supports compact
 * XSAVE area format.
 */
#define XSTATE_XRESTORE(st, lmask, hmask)				\
	asm volatile(ALTERNATIVE(XRSTOR,				\
				 XRSTORS, X86_FEATURE_XSAVES)		\
		     "\n"						\
		     "3:\n"						\
		     _ASM_EXTABLE_TYPE(661b, 3b, EX_TYPE_FPU_RESTORE)	\
		     :							\
		     : "D" (st), "m" (*st), "a" (lmask), "d" (hmask)	\
		     : "memory")

/*
 * Save processor xstate to xsave area.
 *
 * Uses either XSAVE or XSAVEOPT or XSAVES depending on the CPU features
 * and command line options. The choice is permanent until the next reboot.
 */
static inline void os_xsave(struct fpstate *fpstate)
{
	u64 mask = fpstate->xfeatures;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	WARN_ON_FPU(!alternatives_patched);

	XSTATE_XSAVE(&fpstate->regs.xsave, lmask, hmask, err);

	/* We should never fault when copying to a kernel buffer: */
	WARN_ON_FPU(err);
}

/*
 * Restore processor xstate from xsave area.
 *
 * Uses XRSTORS when XSAVES is used, XRSTOR otherwise.
 */
static inline void os_xrstor(struct xregs_state *xstate, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;

	XSTATE_XRESTORE(xstate, lmask, hmask);
}

/*
 * Save xstate to user space xsave area.
 *
 * We don't use modified optimization because xrstor/xrstors might track
 * a different application.
 *
 * We don't use compacted format xsave area for backward compatibility for
 * old applications which don't understand the compacted format of the
 * xsave area.
 *
 * The caller has to zero buf::header before calling this because XSAVE*
 * does not touch the reserved fields in the header.
 */
static inline int xsave_to_user_sigframe(struct xregs_state __user *buf)
{
	/*
	 * Include the features which are not xsaved/rstored by the kernel
	 * internally, e.g. PKRU. That's user space ABI and also required
	 * to allow the signal handler to modify PKRU.
	 */
	u64 mask = current->thread.fpu.fpstate->user_xfeatures;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	stac();
	XSTATE_OP(XSAVE, buf, lmask, hmask, err);
	clac();

	return err;
}

/*
 * Restore xstate from user space xsave area.
 */
static inline int xrstor_from_user_sigframe(struct xregs_state __user *buf, u64 mask)
{
	struct xregs_state *xstate = ((__force struct xregs_state *)buf);
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	stac();
	XSTATE_OP(XRSTOR, xstate, lmask, hmask, err);
	clac();

	return err;
}

/*
 * Restore xstate from kernel space xsave area, return an error code instead of
 * an exception.
 */
static inline int os_xrstor_safe(struct xregs_state *xstate, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	if (cpu_feature_enabled(X86_FEATURE_XSAVES))
		XSTATE_OP(XRSTORS, xstate, lmask, hmask, err);
	else
		XSTATE_OP(XRSTOR, xstate, lmask, hmask, err);

	return err;
}


#endif

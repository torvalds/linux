/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/uaccess.h>
#include <linux/types.h>

#include <asm/processor.h>
#include <asm/fpu/api.h>
#include <asm/user.h>

/* Bit 63 of XCR0 is reserved for future expansion */
#define XFEATURE_MASK_EXTEND	(~(XFEATURE_MASK_FPSSE | (1ULL << 63)))

#define XSTATE_CPUID		0x0000000d

#define FXSAVE_SIZE	512

#define XSAVE_HDR_SIZE	    64
#define XSAVE_HDR_OFFSET    FXSAVE_SIZE

#define XSAVE_YMM_SIZE	    256
#define XSAVE_YMM_OFFSET    (XSAVE_HDR_SIZE + XSAVE_HDR_OFFSET)

#define XSAVE_ALIGNMENT     64

/* All currently supported user features */
#define XFEATURE_MASK_USER_SUPPORTED (XFEATURE_MASK_FP | \
				      XFEATURE_MASK_SSE | \
				      XFEATURE_MASK_YMM | \
				      XFEATURE_MASK_OPMASK | \
				      XFEATURE_MASK_ZMM_Hi256 | \
				      XFEATURE_MASK_Hi16_ZMM	 | \
				      XFEATURE_MASK_PKRU | \
				      XFEATURE_MASK_BNDREGS | \
				      XFEATURE_MASK_BNDCSR)

/*
 * Features which are restored when returning to user space.
 * PKRU is not restored on return to user space because PKRU
 * is switched eagerly in switch_to() and flush_thread()
 */
#define XFEATURE_MASK_USER_RESTORE	\
	(XFEATURE_MASK_USER_SUPPORTED & ~XFEATURE_MASK_PKRU)

/* All currently supported supervisor features */
#define XFEATURE_MASK_SUPERVISOR_SUPPORTED (XFEATURE_MASK_PASID)

/*
 * A supervisor state component may not always contain valuable information,
 * and its size may be huge. Saving/restoring such supervisor state components
 * at each context switch can cause high CPU and space overhead, which should
 * be avoided. Such supervisor state components should only be saved/restored
 * on demand. The on-demand supervisor features are set in this mask.
 *
 * Unlike the existing supported supervisor features, an independent supervisor
 * feature does not allocate a buffer in task->fpu, and the corresponding
 * supervisor state component cannot be saved/restored at each context switch.
 *
 * To support an independent supervisor feature, a developer should follow the
 * dos and don'ts as below:
 * - Do dynamically allocate a buffer for the supervisor state component.
 * - Do manually invoke the XSAVES/XRSTORS instruction to save/restore the
 *   state component to/from the buffer.
 * - Don't set the bit corresponding to the independent supervisor feature in
 *   IA32_XSS at run time, since it has been set at boot time.
 */
#define XFEATURE_MASK_INDEPENDENT (XFEATURE_MASK_LBR)

/*
 * Unsupported supervisor features. When a supervisor feature in this mask is
 * supported in the future, move it to the supported supervisor feature mask.
 */
#define XFEATURE_MASK_SUPERVISOR_UNSUPPORTED (XFEATURE_MASK_PT)

/* All supervisor states including supported and unsupported states. */
#define XFEATURE_MASK_SUPERVISOR_ALL (XFEATURE_MASK_SUPERVISOR_SUPPORTED | \
				      XFEATURE_MASK_INDEPENDENT | \
				      XFEATURE_MASK_SUPERVISOR_UNSUPPORTED)

extern u64 xfeatures_mask_all;

static inline u64 xfeatures_mask_supervisor(void)
{
	return xfeatures_mask_all & XFEATURE_MASK_SUPERVISOR_SUPPORTED;
}

/*
 * The xfeatures which are enabled in XCR0 and expected to be in ptrace
 * buffers and signal frames.
 */
static inline u64 xfeatures_mask_uabi(void)
{
	return xfeatures_mask_all & XFEATURE_MASK_USER_SUPPORTED;
}

/*
 * The xfeatures which are restored by the kernel when returning to user
 * mode. This is not necessarily the same as xfeatures_mask_uabi() as the
 * kernel does not manage all XCR0 enabled features via xsave/xrstor as
 * some of them have to be switched eagerly on context switch and exec().
 */
static inline u64 xfeatures_mask_restore_user(void)
{
	return xfeatures_mask_all & XFEATURE_MASK_USER_RESTORE;
}

/*
 * Like xfeatures_mask_restore_user() but additionally restors the
 * supported supervisor states.
 */
static inline u64 xfeatures_mask_fpstate(void)
{
	return xfeatures_mask_all & \
		(XFEATURE_MASK_USER_RESTORE | XFEATURE_MASK_SUPERVISOR_SUPPORTED);
}

static inline u64 xfeatures_mask_independent(void)
{
	if (!boot_cpu_has(X86_FEATURE_ARCH_LBR))
		return XFEATURE_MASK_INDEPENDENT & ~XFEATURE_MASK_LBR;

	return XFEATURE_MASK_INDEPENDENT;
}

extern u64 xstate_fx_sw_bytes[USER_XSTATE_FX_SW_WORDS];

extern void __init update_regset_xstate_info(unsigned int size,
					     u64 xstate_mask);

int xfeature_size(int xfeature_nr);

void xsaves(struct xregs_state *xsave, u64 mask);
void xrstors(struct xregs_state *xsave, u64 mask);

#endif

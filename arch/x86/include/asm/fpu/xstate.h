/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/uaccess.h>
#include <linux/types.h>

#include <asm/processor.h>
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

/* All currently supported supervisor features */
#define XFEATURE_MASK_SUPERVISOR_SUPPORTED (XFEATURE_MASK_PASID)

/*
 * A supervisor state component may not always contain valuable information,
 * and its size may be huge. Saving/restoring such supervisor state components
 * at each context switch can cause high CPU and space overhead, which should
 * be avoided. Such supervisor state components should only be saved/restored
 * on demand. The on-demand dynamic supervisor features are set in this mask.
 *
 * Unlike the existing supported supervisor features, a dynamic supervisor
 * feature does not allocate a buffer in task->fpu, and the corresponding
 * supervisor state component cannot be saved/restored at each context switch.
 *
 * To support a dynamic supervisor feature, a developer should follow the
 * dos and don'ts as below:
 * - Do dynamically allocate a buffer for the supervisor state component.
 * - Do manually invoke the XSAVES/XRSTORS instruction to save/restore the
 *   state component to/from the buffer.
 * - Don't set the bit corresponding to the dynamic supervisor feature in
 *   IA32_XSS at run time, since it has been set at boot time.
 */
#define XFEATURE_MASK_DYNAMIC (XFEATURE_MASK_LBR)

/*
 * Unsupported supervisor features. When a supervisor feature in this mask is
 * supported in the future, move it to the supported supervisor feature mask.
 */
#define XFEATURE_MASK_SUPERVISOR_UNSUPPORTED (XFEATURE_MASK_PT)

/* All supervisor states including supported and unsupported states. */
#define XFEATURE_MASK_SUPERVISOR_ALL (XFEATURE_MASK_SUPERVISOR_SUPPORTED | \
				      XFEATURE_MASK_DYNAMIC | \
				      XFEATURE_MASK_SUPERVISOR_UNSUPPORTED)

#ifdef CONFIG_X86_64
#define REX_PREFIX	"0x48, "
#else
#define REX_PREFIX
#endif

extern u64 xfeatures_mask_all;

static inline u64 xfeatures_mask_supervisor(void)
{
	return xfeatures_mask_all & XFEATURE_MASK_SUPERVISOR_SUPPORTED;
}

static inline u64 xfeatures_mask_user(void)
{
	return xfeatures_mask_all & XFEATURE_MASK_USER_SUPPORTED;
}

static inline u64 xfeatures_mask_dynamic(void)
{
	if (!boot_cpu_has(X86_FEATURE_ARCH_LBR))
		return XFEATURE_MASK_DYNAMIC & ~XFEATURE_MASK_LBR;

	return XFEATURE_MASK_DYNAMIC;
}

extern u64 xstate_fx_sw_bytes[USER_XSTATE_FX_SW_WORDS];

extern void __init update_regset_xstate_info(unsigned int size,
					     u64 xstate_mask);

void *get_xsave_addr(struct xregs_state *xsave, int xfeature_nr);
const void *get_xsave_field_ptr(int xfeature_nr);
int using_compacted_format(void);
int xfeature_size(int xfeature_nr);
struct membuf;
void copy_xstate_to_kernel(struct membuf to, struct xregs_state *xsave);
int copy_kernel_to_xstate(struct xregs_state *xsave, const void *kbuf);
int copy_user_to_xstate(struct xregs_state *xsave, const void __user *ubuf);
void copy_supervisor_to_kernel(struct xregs_state *xsave);
void copy_dynamic_supervisor_to_kernel(struct xregs_state *xstate, u64 mask);
void copy_kernel_to_dynamic_supervisor(struct xregs_state *xstate, u64 mask);


/* Validate an xstate header supplied by userspace (ptrace or sigreturn) */
int validate_user_xstate_header(const struct xstate_header *hdr);

#endif

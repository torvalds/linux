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
#define XFEATURE_MASK_SUPERVISOR_SUPPORTED (0)

/*
 * Unsupported supervisor features. When a supervisor feature in this mask is
 * supported in the future, move it to the supported supervisor feature mask.
 */
#define XFEATURE_MASK_SUPERVISOR_UNSUPPORTED (XFEATURE_MASK_PT)

/* All supervisor states including supported and unsupported states. */
#define XFEATURE_MASK_SUPERVISOR_ALL (XFEATURE_MASK_SUPERVISOR_SUPPORTED | \
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

extern u64 xstate_fx_sw_bytes[USER_XSTATE_FX_SW_WORDS];

extern void __init update_regset_xstate_info(unsigned int size,
					     u64 xstate_mask);

void *get_xsave_addr(struct xregs_state *xsave, int xfeature_nr);
const void *get_xsave_field_ptr(int xfeature_nr);
int using_compacted_format(void);
int copy_xstate_to_kernel(void *kbuf, struct xregs_state *xsave, unsigned int offset, unsigned int size);
int copy_xstate_to_user(void __user *ubuf, struct xregs_state *xsave, unsigned int offset, unsigned int size);
int copy_kernel_to_xstate(struct xregs_state *xsave, const void *kbuf);
int copy_user_to_xstate(struct xregs_state *xsave, const void __user *ubuf);
void copy_supervisor_to_kernel(struct xregs_state *xsave);

/* Validate an xstate header supplied by userspace (ptrace or sigreturn) */
int validate_user_xstate_header(const struct xstate_header *hdr);

#endif

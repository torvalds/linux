#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/types.h>
#include <asm/processor.h>
#include <linux/uaccess.h>

/* Bit 63 of XCR0 is reserved for future expansion */
#define XFEATURE_MASK_EXTEND	(~(XFEATURE_MASK_FPSSE | (1ULL << 63)))

#define XSTATE_CPUID		0x0000000d

#define FXSAVE_SIZE	512

#define XSAVE_HDR_SIZE	    64
#define XSAVE_HDR_OFFSET    FXSAVE_SIZE

#define XSAVE_YMM_SIZE	    256
#define XSAVE_YMM_OFFSET    (XSAVE_HDR_SIZE + XSAVE_HDR_OFFSET)

/* Supervisor features */
#define XFEATURE_MASK_SUPERVISOR (XFEATURE_MASK_PT)

/* Supported features which support lazy state saving */
#define XFEATURE_MASK_LAZY	(XFEATURE_MASK_FP | \
				 XFEATURE_MASK_SSE | \
				 XFEATURE_MASK_YMM | \
				 XFEATURE_MASK_OPMASK | \
				 XFEATURE_MASK_ZMM_Hi256 | \
				 XFEATURE_MASK_Hi16_ZMM	 | \
				 XFEATURE_MASK_PKRU)

/* Supported features which require eager state saving */
#define XFEATURE_MASK_EAGER	(XFEATURE_MASK_BNDREGS | XFEATURE_MASK_BNDCSR)

/* All currently supported features */
#define XCNTXT_MASK	(XFEATURE_MASK_LAZY | XFEATURE_MASK_EAGER)

#ifdef CONFIG_X86_64
#define REX_PREFIX	"0x48, "
#else
#define REX_PREFIX
#endif

extern u64 xfeatures_mask;
extern u64 xstate_fx_sw_bytes[USER_XSTATE_FX_SW_WORDS];

extern void update_regset_xstate_info(unsigned int size, u64 xstate_mask);

void fpu__xstate_clear_all_cpu_caps(void);
void *get_xsave_addr(struct xregs_state *xsave, int xstate);
const void *get_xsave_field_ptr(int xstate_field);
int using_compacted_format(void);
int copyout_from_xsaves(unsigned int pos, unsigned int count, void *kbuf,
			void __user *ubuf, struct xregs_state *xsave);
int copyin_to_xsaves(const void *kbuf, const void __user *ubuf,
		     struct xregs_state *xsave);
#endif

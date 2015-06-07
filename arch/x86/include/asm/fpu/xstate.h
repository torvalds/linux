#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/types.h>
#include <asm/processor.h>
#include <linux/uaccess.h>

/* Bit 63 of XCR0 is reserved for future expansion */
#define XSTATE_EXTEND_MASK	(~(XSTATE_FPSSE | (1ULL << 63)))

#define XSTATE_CPUID		0x0000000d

#define FXSAVE_SIZE	512

#define XSAVE_HDR_SIZE	    64
#define XSAVE_HDR_OFFSET    FXSAVE_SIZE

#define XSAVE_YMM_SIZE	    256
#define XSAVE_YMM_OFFSET    (XSAVE_HDR_SIZE + XSAVE_HDR_OFFSET)

/* Supported features which support lazy state saving */
#define XSTATE_LAZY	(XSTATE_FP | XSTATE_SSE | XSTATE_YMM		      \
			| XSTATE_OPMASK | XSTATE_ZMM_Hi256 | XSTATE_Hi16_ZMM)

/* Supported features which require eager state saving */
#define XSTATE_EAGER	(XSTATE_BNDREGS | XSTATE_BNDCSR)

/* All currently supported features */
#define XCNTXT_MASK	(XSTATE_LAZY | XSTATE_EAGER)

#ifdef CONFIG_X86_64
#define REX_PREFIX	"0x48, "
#else
#define REX_PREFIX
#endif

extern unsigned int xstate_size;
extern u64 xfeatures_mask;
extern u64 xstate_fx_sw_bytes[USER_XSTATE_FX_SW_WORDS];

extern void update_regset_xstate_info(unsigned int size, u64 xstate_mask);

void *get_xsave_addr(struct xregs_state *xsave, int xstate);
const void *get_xsave_field_ptr(int xstate_field);

#endif

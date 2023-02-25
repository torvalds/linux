/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SHARED_TDX_H
#define _ASM_X86_SHARED_TDX_H

#include <linux/bits.h>
#include <linux/types.h>

#define TDX_HYPERCALL_STANDARD  0

#define TDX_HCALL_HAS_OUTPUT	BIT(0)

#define TDX_CPUID_LEAF_ID	0x21
#define TDX_IDENT		"IntelTDX    "

#ifndef __ASSEMBLY__

/*
 * Used in __tdx_hypercall() to pass down and get back registers' values of
 * the TDCALL instruction when requesting services from the VMM.
 *
 * This is a software only structure and not part of the TDX module/VMM ABI.
 */
struct tdx_hypercall_args {
	u64 r8;
	u64 r9;
	u64 r10;
	u64 r11;
	u64 r12;
	u64 r13;
	u64 r14;
	u64 r15;
	u64 rdi;
	u64 rsi;
	u64 rbx;
	u64 rdx;
};

/* Used to request services from the VMM */
u64 __tdx_hypercall(struct tdx_hypercall_args *args, unsigned long flags);

/* Called from __tdx_hypercall() for unrecoverable failure */
void __tdx_hypercall_failed(void);

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_SHARED_TDX_H */

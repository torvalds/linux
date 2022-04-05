// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021-2022 Intel Corporation */

#undef pr_fmt
#define pr_fmt(fmt)     "tdx: " fmt

#include <linux/cpufeature.h>
#include <asm/tdx.h>

/*
 * Wrapper for standard use of __tdx_hypercall with no output aside from
 * return code.
 */
static inline u64 _tdx_hypercall(u64 fn, u64 r12, u64 r13, u64 r14, u64 r15)
{
	struct tdx_hypercall_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = fn,
		.r12 = r12,
		.r13 = r13,
		.r14 = r14,
		.r15 = r15,
	};

	return __tdx_hypercall(&args, 0);
}

/* Called from __tdx_hypercall() for unrecoverable failure */
void __tdx_hypercall_failed(void)
{
	panic("TDVMCALL failed. TDX module bug?");
}

void __init tdx_early_init(void)
{
	u32 eax, sig[3];

	cpuid_count(TDX_CPUID_LEAF_ID, 0, &eax, &sig[0], &sig[2],  &sig[1]);

	if (memcmp(TDX_IDENT, sig, sizeof(sig)))
		return;

	setup_force_cpu_cap(X86_FEATURE_TDX_GUEST);

	pr_info("Guest detected\n");
}

// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2024 Google LLC
// Author: Ard Biesheuvel <ardb@google.com>

#include <linux/types.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_CPU_TLB_V4WT
void v4_flush_user_tlb_range(unsigned long, unsigned long, struct vm_area_struct *);
void v4_flush_kern_tlb_range(unsigned long, unsigned long);

struct cpu_tlb_fns v4_tlb_fns __initconst = {
	.flush_user_range	= v4_flush_user_tlb_range,
	.flush_kern_range	= v4_flush_kern_tlb_range,
	.tlb_flags		= v4_tlb_flags,
};
#endif

#ifdef CONFIG_CPU_TLB_V4WB
void v4wb_flush_user_tlb_range(unsigned long, unsigned long, struct vm_area_struct *);
void v4wb_flush_kern_tlb_range(unsigned long, unsigned long);

struct cpu_tlb_fns v4wb_tlb_fns __initconst = {
	.flush_user_range	= v4wb_flush_user_tlb_range,
	.flush_kern_range	= v4wb_flush_kern_tlb_range,
	.tlb_flags		= v4wb_tlb_flags,
};
#endif

#if defined(CONFIG_CPU_TLB_V4WBI) || defined(CONFIG_CPU_TLB_FEROCEON)
void v4wbi_flush_user_tlb_range(unsigned long, unsigned long, struct vm_area_struct *);
void v4wbi_flush_kern_tlb_range(unsigned long, unsigned long);

struct cpu_tlb_fns v4wbi_tlb_fns __initconst = {
	.flush_user_range	= v4wbi_flush_user_tlb_range,
	.flush_kern_range	= v4wbi_flush_kern_tlb_range,
	.tlb_flags		= v4wbi_tlb_flags,
};
#endif

#ifdef CONFIG_CPU_TLB_V6
void v6wbi_flush_user_tlb_range(unsigned long, unsigned long, struct vm_area_struct *);
void v6wbi_flush_kern_tlb_range(unsigned long, unsigned long);

struct cpu_tlb_fns v6wbi_tlb_fns __initconst = {
	.flush_user_range	= v6wbi_flush_user_tlb_range,
	.flush_kern_range	= v6wbi_flush_kern_tlb_range,
	.tlb_flags		= v6wbi_tlb_flags,
};
#endif

#ifdef CONFIG_CPU_TLB_V7
void v7wbi_flush_user_tlb_range(unsigned long, unsigned long, struct vm_area_struct *);
void v7wbi_flush_kern_tlb_range(unsigned long, unsigned long);

struct cpu_tlb_fns v7wbi_tlb_fns __initconst = {
	.flush_user_range	= v7wbi_flush_user_tlb_range,
	.flush_kern_range	= v7wbi_flush_kern_tlb_range,
	.tlb_flags		= IS_ENABLED(CONFIG_SMP) ? v7wbi_tlb_flags_smp
							 : v7wbi_tlb_flags_up,
};

#ifdef CONFIG_SMP_ON_UP
/* This will be run-time patched so the offset better be right */
static_assert(offsetof(struct cpu_tlb_fns, tlb_flags) == 8);

asm("	.pushsection	\".alt.smp.init\", \"a\"		\n" \
    "	.align		2					\n" \
    "	.long		v7wbi_tlb_fns + 8 - .			\n" \
    "	.long "  	__stringify(v7wbi_tlb_flags_up) "	\n" \
    "	.popsection						\n");
#endif
#endif

#ifdef CONFIG_CPU_TLB_FA
void fa_flush_user_tlb_range(unsigned long, unsigned long, struct vm_area_struct *);
void fa_flush_kern_tlb_range(unsigned long, unsigned long);

struct cpu_tlb_fns fa_tlb_fns __initconst = {
	.flush_user_range	= fa_flush_user_tlb_range,
	.flush_kern_range	= fa_flush_kern_tlb_range,
	.tlb_flags		= fa_tlb_flags,
};
#endif

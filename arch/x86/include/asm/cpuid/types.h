/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPUID_TYPES_H
#define _ASM_X86_CPUID_TYPES_H

#include <linux/build_bug.h>
#include <linux/types.h>

/*
 * Types for raw CPUID access:
 */

struct cpuid_regs {
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
};

enum cpuid_regs_idx {
	CPUID_EAX = 0,
	CPUID_EBX,
	CPUID_ECX,
	CPUID_EDX,
};

#define CPUID_LEAF_MWAIT	0x05
#define CPUID_LEAF_DCA		0x09
#define CPUID_LEAF_XSTATE	0x0d
#define CPUID_LEAF_TSC		0x15
#define CPUID_LEAF_FREQ		0x16
#define CPUID_LEAF_TILE		0x1d

/*
 * Types for CPUID(0x2) parsing
 * Check <asm/cpuid/leaf_0x2_api.h>
 */

struct leaf_0x2_reg {
		u32		: 31,
			invalid	: 1;
};

union leaf_0x2_regs {
	struct leaf_0x2_reg	reg[4];
	u32			regv[4];
	u8			desc[16];
};

/*
 * Leaf 0x2 1-byte descriptors' cache types
 * To be used for their mappings at cache_table[]
 */
enum _cache_table_type {
	CACHE_L1_INST,
	CACHE_L1_DATA,
	CACHE_L2,
	CACHE_L3,
} __packed;
#ifndef __CHECKER__
static_assert(sizeof(enum _cache_table_type) == 1);
#endif

/*
 * Leaf 0x2 1-byte descriptors' TLB types
 * To be used for their mappings at intel_tlb_table[]
 *
 * Start at 1 since type 0 is reserved for HW byte descriptors which are
 * not recognized by the kernel; i.e., those without an explicit mapping.
 */
enum _tlb_table_type {
	TLB_INST_4K		= 1,
	TLB_INST_4M,
	TLB_INST_2M_4M,
	TLB_INST_ALL,

	TLB_DATA_4K,
	TLB_DATA_4M,
	TLB_DATA_2M_4M,
	TLB_DATA_4K_4M,
	TLB_DATA_1G,
	TLB_DATA_1G_2M_4M,

	TLB_DATA0_4K,
	TLB_DATA0_4M,
	TLB_DATA0_2M_4M,

	STLB_4K,
	STLB_4K_2M,
} __packed;
#ifndef __CHECKER__
static_assert(sizeof(enum _tlb_table_type) == 1);
#endif

#endif /* _ASM_X86_CPUID_TYPES_H */

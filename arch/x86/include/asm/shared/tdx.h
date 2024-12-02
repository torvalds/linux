/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SHARED_TDX_H
#define _ASM_X86_SHARED_TDX_H

#include <linux/bits.h>
#include <linux/types.h>

#define TDX_HYPERCALL_STANDARD  0

#define TDX_CPUID_LEAF_ID	0x21
#define TDX_IDENT		"IntelTDX    "

/* TDX module Call Leaf IDs */
#define TDG_VP_VMCALL			0
#define TDG_VP_INFO			1
#define TDG_VP_VEINFO_GET		3
#define TDG_MR_REPORT			4
#define TDG_MEM_PAGE_ACCEPT		6
#define TDG_VM_RD			7
#define TDG_VM_WR			8

/* TDX attributes */
#define TDX_ATTR_DEBUG_BIT		0
#define TDX_ATTR_DEBUG			BIT_ULL(TDX_ATTR_DEBUG_BIT)
#define TDX_ATTR_HGS_PLUS_PROF_BIT	4
#define TDX_ATTR_HGS_PLUS_PROF		BIT_ULL(TDX_ATTR_HGS_PLUS_PROF_BIT)
#define TDX_ATTR_PERF_PROF_BIT		5
#define TDX_ATTR_PERF_PROF		BIT_ULL(TDX_ATTR_PERF_PROF_BIT)
#define TDX_ATTR_PMT_PROF_BIT		6
#define TDX_ATTR_PMT_PROF		BIT_ULL(TDX_ATTR_PMT_PROF_BIT)
#define TDX_ATTR_ICSSD_BIT		16
#define TDX_ATTR_ICSSD			BIT_ULL(TDX_ATTR_ICSSD_BIT)
#define TDX_ATTR_LASS_BIT		27
#define TDX_ATTR_LASS			BIT_ULL(TDX_ATTR_LASS_BIT)
#define TDX_ATTR_SEPT_VE_DISABLE_BIT	28
#define TDX_ATTR_SEPT_VE_DISABLE	BIT_ULL(TDX_ATTR_SEPT_VE_DISABLE_BIT)
#define TDX_ATTR_MIGRTABLE_BIT		29
#define TDX_ATTR_MIGRTABLE		BIT_ULL(TDX_ATTR_MIGRTABLE_BIT)
#define TDX_ATTR_PKS_BIT		30
#define TDX_ATTR_PKS			BIT_ULL(TDX_ATTR_PKS_BIT)
#define TDX_ATTR_KL_BIT			31
#define TDX_ATTR_KL			BIT_ULL(TDX_ATTR_KL_BIT)
#define TDX_ATTR_TPA_BIT		62
#define TDX_ATTR_TPA			BIT_ULL(TDX_ATTR_TPA_BIT)
#define TDX_ATTR_PERFMON_BIT		63
#define TDX_ATTR_PERFMON		BIT_ULL(TDX_ATTR_PERFMON_BIT)

/* TDX TD-Scope Metadata. To be used by TDG.VM.WR and TDG.VM.RD */
#define TDCS_CONFIG_FLAGS		0x1110000300000016
#define TDCS_TD_CTLS			0x1110000300000017
#define TDCS_NOTIFY_ENABLES		0x9100000000000010
#define TDCS_TOPOLOGY_ENUM_CONFIGURED	0x9100000000000019

/* TDCS_CONFIG_FLAGS bits */
#define TDCS_CONFIG_FLEXIBLE_PENDING_VE	BIT_ULL(1)

/* TDCS_TD_CTLS bits */
#define TD_CTLS_PENDING_VE_DISABLE_BIT	0
#define TD_CTLS_PENDING_VE_DISABLE	BIT_ULL(TD_CTLS_PENDING_VE_DISABLE_BIT)
#define TD_CTLS_ENUM_TOPOLOGY_BIT	1
#define TD_CTLS_ENUM_TOPOLOGY		BIT_ULL(TD_CTLS_ENUM_TOPOLOGY_BIT)
#define TD_CTLS_VIRT_CPUID2_BIT		2
#define TD_CTLS_VIRT_CPUID2		BIT_ULL(TD_CTLS_VIRT_CPUID2_BIT)
#define TD_CTLS_REDUCE_VE_BIT		3
#define TD_CTLS_REDUCE_VE		BIT_ULL(TD_CTLS_REDUCE_VE_BIT)
#define TD_CTLS_LOCK_BIT		63
#define TD_CTLS_LOCK			BIT_ULL(TD_CTLS_LOCK_BIT)

/* TDX hypercall Leaf IDs */
#define TDVMCALL_MAP_GPA		0x10001
#define TDVMCALL_GET_QUOTE		0x10002
#define TDVMCALL_REPORT_FATAL_ERROR	0x10003

#define TDVMCALL_STATUS_RETRY		1

/*
 * Bitmasks of exposed registers (with VMM).
 */
#define TDX_RDX		BIT(2)
#define TDX_RBX		BIT(3)
#define TDX_RSI		BIT(6)
#define TDX_RDI		BIT(7)
#define TDX_R8		BIT(8)
#define TDX_R9		BIT(9)
#define TDX_R10		BIT(10)
#define TDX_R11		BIT(11)
#define TDX_R12		BIT(12)
#define TDX_R13		BIT(13)
#define TDX_R14		BIT(14)
#define TDX_R15		BIT(15)

/*
 * These registers are clobbered to hold arguments for each
 * TDVMCALL. They are safe to expose to the VMM.
 * Each bit in this mask represents a register ID. Bit field
 * details can be found in TDX GHCI specification, section
 * titled "TDCALL [TDG.VP.VMCALL] leaf".
 */
#define TDVMCALL_EXPOSE_REGS_MASK	\
	(TDX_RDX | TDX_RBX | TDX_RSI | TDX_RDI | TDX_R8  | TDX_R9  | \
	 TDX_R10 | TDX_R11 | TDX_R12 | TDX_R13 | TDX_R14 | TDX_R15)

/* TDX supported page sizes from the TDX module ABI. */
#define TDX_PS_4K	0
#define TDX_PS_2M	1
#define TDX_PS_1G	2
#define TDX_PS_NR	(TDX_PS_1G + 1)

#ifndef __ASSEMBLY__

#include <linux/compiler_attributes.h>

/*
 * Used in __tdcall*() to gather the input/output registers' values of the
 * TDCALL instruction when requesting services from the TDX module. This is a
 * software only structure and not part of the TDX module/VMM ABI
 */
struct tdx_module_args {
	/* callee-clobbered */
	u64 rcx;
	u64 rdx;
	u64 r8;
	u64 r9;
	/* extra callee-clobbered */
	u64 r10;
	u64 r11;
	/* callee-saved + rdi/rsi */
	u64 r12;
	u64 r13;
	u64 r14;
	u64 r15;
	u64 rbx;
	u64 rdi;
	u64 rsi;
};

/* Used to communicate with the TDX module */
u64 __tdcall(u64 fn, struct tdx_module_args *args);
u64 __tdcall_ret(u64 fn, struct tdx_module_args *args);
u64 __tdcall_saved_ret(u64 fn, struct tdx_module_args *args);

/* Used to request services from the VMM */
u64 __tdx_hypercall(struct tdx_module_args *args);

/*
 * Wrapper for standard use of __tdx_hypercall with no output aside from
 * return code.
 */
static inline u64 _tdx_hypercall(u64 fn, u64 r12, u64 r13, u64 r14, u64 r15)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = fn,
		.r12 = r12,
		.r13 = r13,
		.r14 = r14,
		.r15 = r15,
	};

	return __tdx_hypercall(&args);
}


/* Called from __tdx_hypercall() for unrecoverable failure */
void __noreturn __tdx_hypercall_failed(void);

bool tdx_accept_memory(phys_addr_t start, phys_addr_t end);

/*
 * The TDG.VP.VMCALL-Instruction-execution sub-functions are defined
 * independently from but are currently matched 1:1 with VMX EXIT_REASONs.
 * Reusing the KVM EXIT_REASON macros makes it easier to connect the host and
 * guest sides of these calls.
 */
static __always_inline u64 hcall_func(u64 exit_reason)
{
        return exit_reason;
}

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_SHARED_TDX_H */

/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_KVM_PARA_H
#define _UAPI_ASM_X86_KVM_PARA_H

#include <linux/types.h>

/* This CPUID returns the signature 'KVMKVMKVM' in ebx, ecx, and edx.  It
 * should be used to determine that a VM is running under KVM.
 */
#define KVM_CPUID_SIGNATURE	0x40000000
#define KVM_SIGNATURE "KVMKVMKVM\0\0\0"

/* This CPUID returns two feature bitmaps in eax, edx. Before enabling
 * a particular paravirtualization, the appropriate feature bit should
 * be checked in eax. The performance hint feature bit should be checked
 * in edx.
 */
#define KVM_CPUID_FEATURES	0x40000001
#define KVM_FEATURE_CLOCKSOURCE		0
#define KVM_FEATURE_NOP_IO_DELAY	1
#define KVM_FEATURE_MMU_OP		2
/* This indicates that the new set of kvmclock msrs
 * are available. The use of 0x11 and 0x12 is deprecated
 */
#define KVM_FEATURE_CLOCKSOURCE2        3
#define KVM_FEATURE_ASYNC_PF		4
#define KVM_FEATURE_STEAL_TIME		5
#define KVM_FEATURE_PV_EOI		6
#define KVM_FEATURE_PV_UNHALT		7
#define KVM_FEATURE_PV_TLB_FLUSH	9
#define KVM_FEATURE_ASYNC_PF_VMEXIT	10
#define KVM_FEATURE_PV_SEND_IPI	11
#define KVM_FEATURE_POLL_CONTROL	12
#define KVM_FEATURE_PV_SCHED_YIELD	13
#define KVM_FEATURE_ASYNC_PF_INT	14
#define KVM_FEATURE_MSI_EXT_DEST_ID	15
#define KVM_FEATURE_HC_MAP_GPA_RANGE	16
#define KVM_FEATURE_MIGRATION_CONTROL	17

#define KVM_HINTS_REALTIME      0

/* The last 8 bits are used to indicate how to interpret the flags field
 * in pvclock structure. If no bits are set, all flags are ignored.
 */
#define KVM_FEATURE_CLOCKSOURCE_STABLE_BIT	24

#define MSR_KVM_WALL_CLOCK  0x11
#define MSR_KVM_SYSTEM_TIME 0x12

#define KVM_MSR_ENABLED 1
/* Custom MSRs falls in the range 0x4b564d00-0x4b564dff */
#define MSR_KVM_WALL_CLOCK_NEW  0x4b564d00
#define MSR_KVM_SYSTEM_TIME_NEW 0x4b564d01
#define MSR_KVM_ASYNC_PF_EN 0x4b564d02
#define MSR_KVM_STEAL_TIME  0x4b564d03
#define MSR_KVM_PV_EOI_EN      0x4b564d04
#define MSR_KVM_POLL_CONTROL	0x4b564d05
#define MSR_KVM_ASYNC_PF_INT	0x4b564d06
#define MSR_KVM_ASYNC_PF_ACK	0x4b564d07
#define MSR_KVM_MIGRATION_CONTROL	0x4b564d08

struct kvm_steal_time {
	__u64 steal;
	__u32 version;
	__u32 flags;
	__u8  preempted;
	__u8  u8_pad[3];
	__u32 pad[11];
};

#define KVM_VCPU_PREEMPTED          (1 << 0)
#define KVM_VCPU_FLUSH_TLB          (1 << 1)

#define KVM_CLOCK_PAIRING_WALLCLOCK 0
struct kvm_clock_pairing {
	__s64 sec;
	__s64 nsec;
	__u64 tsc;
	__u32 flags;
	__u32 pad[9];
};

#define KVM_STEAL_ALIGNMENT_BITS 5
#define KVM_STEAL_VALID_BITS ((-1ULL << (KVM_STEAL_ALIGNMENT_BITS + 1)))
#define KVM_STEAL_RESERVED_MASK (((1 << KVM_STEAL_ALIGNMENT_BITS) - 1 ) << 1)

#define KVM_MAX_MMU_OP_BATCH           32

#define KVM_ASYNC_PF_ENABLED			(1 << 0)
#define KVM_ASYNC_PF_SEND_ALWAYS		(1 << 1)
#define KVM_ASYNC_PF_DELIVERY_AS_PF_VMEXIT	(1 << 2)
#define KVM_ASYNC_PF_DELIVERY_AS_INT		(1 << 3)

/* MSR_KVM_ASYNC_PF_INT */
#define KVM_ASYNC_PF_VEC_MASK			__GENMASK(7, 0)

/* MSR_KVM_MIGRATION_CONTROL */
#define KVM_MIGRATION_READY		(1 << 0)

/* KVM_HC_MAP_GPA_RANGE */
#define KVM_MAP_GPA_RANGE_PAGE_SZ_4K	0
#define KVM_MAP_GPA_RANGE_PAGE_SZ_2M	(1 << 0)
#define KVM_MAP_GPA_RANGE_PAGE_SZ_1G	(1 << 1)
#define KVM_MAP_GPA_RANGE_ENC_STAT(n)	(n << 4)
#define KVM_MAP_GPA_RANGE_ENCRYPTED	KVM_MAP_GPA_RANGE_ENC_STAT(1)
#define KVM_MAP_GPA_RANGE_DECRYPTED	KVM_MAP_GPA_RANGE_ENC_STAT(0)

/* Operations for KVM_HC_MMU_OP */
#define KVM_MMU_OP_WRITE_PTE            1
#define KVM_MMU_OP_FLUSH_TLB	        2
#define KVM_MMU_OP_RELEASE_PT	        3

/* Payload for KVM_HC_MMU_OP */
struct kvm_mmu_op_header {
	__u32 op;
	__u32 pad;
};

struct kvm_mmu_op_write_pte {
	struct kvm_mmu_op_header header;
	__u64 pte_phys;
	__u64 pte_val;
};

struct kvm_mmu_op_flush_tlb {
	struct kvm_mmu_op_header header;
};

struct kvm_mmu_op_release_pt {
	struct kvm_mmu_op_header header;
	__u64 pt_phys;
};

#define KVM_PV_REASON_PAGE_NOT_PRESENT 1
#define KVM_PV_REASON_PAGE_READY 2

struct kvm_vcpu_pv_apf_data {
	/* Used for 'page not present' events delivered via #PF */
	__u32 flags;

	/* Used for 'page ready' events delivered via interrupt notification */
	__u32 token;

	__u8 pad[56];
	__u32 enabled;
};

#define KVM_PV_EOI_BIT 0
#define KVM_PV_EOI_MASK (0x1 << KVM_PV_EOI_BIT)
#define KVM_PV_EOI_ENABLED KVM_PV_EOI_MASK
#define KVM_PV_EOI_DISABLED 0x0

#endif /* _UAPI_ASM_X86_KVM_PARA_H */

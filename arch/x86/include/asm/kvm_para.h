#ifndef _ASM_X86_KVM_PARA_H
#define _ASM_X86_KVM_PARA_H

#include <linux/types.h>
#include <asm/hyperv.h>

/* This CPUID returns the signature 'KVMKVMKVM' in ebx, ecx, and edx.  It
 * should be used to determine that a VM is running under KVM.
 */
#define KVM_CPUID_SIGNATURE	0x40000000

/* This CPUID returns a feature bitmap in eax.  Before enabling a particular
 * paravirtualization, the appropriate feature bit should be checked.
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

struct kvm_steal_time {
	__u64 steal;
	__u32 version;
	__u32 flags;
	__u32 pad[12];
};

#define KVM_STEAL_ALIGNMENT_BITS 5
#define KVM_STEAL_VALID_BITS ((-1ULL << (KVM_STEAL_ALIGNMENT_BITS + 1)))
#define KVM_STEAL_RESERVED_MASK (((1 << KVM_STEAL_ALIGNMENT_BITS) - 1 ) << 1)

#define KVM_MAX_MMU_OP_BATCH           32

#define KVM_ASYNC_PF_ENABLED			(1 << 0)
#define KVM_ASYNC_PF_SEND_ALWAYS		(1 << 1)

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
	__u32 reason;
	__u8 pad[60];
	__u32 enabled;
};

#define KVM_PV_EOI_BIT 0
#define KVM_PV_EOI_MASK (0x1 << KVM_PV_EOI_BIT)
#define KVM_PV_EOI_ENABLED KVM_PV_EOI_MASK
#define KVM_PV_EOI_DISABLED 0x0

#ifdef __KERNEL__
#include <asm/processor.h>

extern void kvmclock_init(void);
extern int kvm_register_clock(char *txt);

#ifdef CONFIG_KVM_GUEST
bool kvm_check_and_clear_guest_paused(void);
#else
static inline bool kvm_check_and_clear_guest_paused(void)
{
	return false;
}
#endif /* CONFIG_KVM_GUEST */

/* This instruction is vmcall.  On non-VT architectures, it will generate a
 * trap that we will then rewrite to the appropriate instruction.
 */
#define KVM_HYPERCALL ".byte 0x0f,0x01,0xc1"

/* For KVM hypercalls, a three-byte sequence of either the vmcall or the vmmcall
 * instruction.  The hypervisor may replace it with something else but only the
 * instructions are guaranteed to be supported.
 *
 * Up to four arguments may be passed in rbx, rcx, rdx, and rsi respectively.
 * The hypercall number should be placed in rax and the return value will be
 * placed in rax.  No other registers will be clobbered unless explicited
 * noted by the particular hypercall.
 */

static inline long kvm_hypercall0(unsigned int nr)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr)
		     : "memory");
	return ret;
}

static inline long kvm_hypercall1(unsigned int nr, unsigned long p1)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr), "b"(p1)
		     : "memory");
	return ret;
}

static inline long kvm_hypercall2(unsigned int nr, unsigned long p1,
				  unsigned long p2)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2)
		     : "memory");
	return ret;
}

static inline long kvm_hypercall3(unsigned int nr, unsigned long p1,
				  unsigned long p2, unsigned long p3)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2), "d"(p3)
		     : "memory");
	return ret;
}

static inline long kvm_hypercall4(unsigned int nr, unsigned long p1,
				  unsigned long p2, unsigned long p3,
				  unsigned long p4)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2), "d"(p3), "S"(p4)
		     : "memory");
	return ret;
}

static inline int kvm_para_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	char signature[13];

	if (boot_cpu_data.cpuid_level < 0)
		return 0;	/* So we don't blow up on old processors */

	if (cpu_has_hypervisor) {
		cpuid(KVM_CPUID_SIGNATURE, &eax, &ebx, &ecx, &edx);
		memcpy(signature + 0, &ebx, 4);
		memcpy(signature + 4, &ecx, 4);
		memcpy(signature + 8, &edx, 4);
		signature[12] = 0;

		if (strcmp(signature, "KVMKVMKVM") == 0)
			return 1;
	}

	return 0;
}

static inline unsigned int kvm_arch_para_features(void)
{
	return cpuid_eax(KVM_CPUID_FEATURES);
}

#ifdef CONFIG_KVM_GUEST
void __init kvm_guest_init(void);
void kvm_async_pf_task_wait(u32 token);
void kvm_async_pf_task_wake(u32 token);
u32 kvm_read_and_reset_pf_reason(void);
extern void kvm_disable_steal_time(void);
#else
#define kvm_guest_init() do { } while (0)
#define kvm_async_pf_task_wait(T) do {} while(0)
#define kvm_async_pf_task_wake(T) do {} while(0)
static inline u32 kvm_read_and_reset_pf_reason(void)
{
	return 0;
}

static inline void kvm_disable_steal_time(void)
{
	return;
}
#endif

#endif /* __KERNEL__ */

#endif /* _ASM_X86_KVM_PARA_H */

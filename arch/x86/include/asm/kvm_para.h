#ifndef _ASM_X86_KVM_PARA_H
#define _ASM_X86_KVM_PARA_H

#include <asm/processor.h>
#include <uapi/asm/kvm_para.h>

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
 * placed in rax.  No other registers will be clobbered unless explicitly
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

static inline uint32_t kvm_cpuid_base(void)
{
	if (boot_cpu_data.cpuid_level < 0)
		return 0;	/* So we don't blow up on old processors */

	if (cpu_has_hypervisor)
		return hypervisor_cpuid_base("KVMKVMKVM\0\0\0", 0);

	return 0;
}

static inline bool kvm_para_available(void)
{
	return kvm_cpuid_base() != 0;
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

#endif /* _ASM_X86_KVM_PARA_H */

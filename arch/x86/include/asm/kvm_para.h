/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_KVM_PARA_H
#define _ASM_X86_KVM_PARA_H

#include <asm/processor.h>
#include <asm/alternative.h>
#include <linux/interrupt.h>
#include <uapi/asm/kvm_para.h>

#include <asm/tdx.h>

#ifdef CONFIG_KVM_GUEST
bool kvm_check_and_clear_guest_paused(void);
#else
static inline bool kvm_check_and_clear_guest_paused(void)
{
	return false;
}
#endif /* CONFIG_KVM_GUEST */

#define KVM_HYPERCALL \
        ALTERNATIVE("vmcall", "vmmcall", X86_FEATURE_VMMCALL)

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

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return tdx_kvm_hypercall(nr, 0, 0, 0, 0);

	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr)
		     : "memory");
	return ret;
}

static inline long kvm_hypercall1(unsigned int nr, unsigned long p1)
{
	long ret;

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return tdx_kvm_hypercall(nr, p1, 0, 0, 0);

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

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return tdx_kvm_hypercall(nr, p1, p2, 0, 0);

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

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return tdx_kvm_hypercall(nr, p1, p2, p3, 0);

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

	if (cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return tdx_kvm_hypercall(nr, p1, p2, p3, p4);

	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2), "d"(p3), "S"(p4)
		     : "memory");
	return ret;
}

static inline long kvm_sev_hypercall3(unsigned int nr, unsigned long p1,
				      unsigned long p2, unsigned long p3)
{
	long ret;

	asm volatile("vmmcall"
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2), "d"(p3)
		     : "memory");
	return ret;
}

#ifdef CONFIG_KVM_GUEST
void kvmclock_init(void);
void kvmclock_disable(void);
bool kvm_para_available(void);
unsigned int kvm_arch_para_features(void);
unsigned int kvm_arch_para_hints(void);
void kvm_async_pf_task_wait_schedule(u32 token);
void kvm_async_pf_task_wake(u32 token);
u32 kvm_read_and_reset_apf_flags(void);
bool __kvm_handle_async_pf(struct pt_regs *regs, u32 token);

DECLARE_STATIC_KEY_FALSE(kvm_async_pf_enabled);

static __always_inline bool kvm_handle_async_pf(struct pt_regs *regs, u32 token)
{
	if (static_branch_unlikely(&kvm_async_pf_enabled))
		return __kvm_handle_async_pf(regs, token);
	else
		return false;
}

#ifdef CONFIG_PARAVIRT_SPINLOCKS
void __init kvm_spinlock_init(void);
#else /* !CONFIG_PARAVIRT_SPINLOCKS */
static inline void kvm_spinlock_init(void)
{
}
#endif /* CONFIG_PARAVIRT_SPINLOCKS */

#else /* CONFIG_KVM_GUEST */
#define kvm_async_pf_task_wait_schedule(T) do {} while(0)
#define kvm_async_pf_task_wake(T) do {} while(0)

static inline bool kvm_para_available(void)
{
	return false;
}

static inline unsigned int kvm_arch_para_features(void)
{
	return 0;
}

static inline unsigned int kvm_arch_para_hints(void)
{
	return 0;
}

static inline u32 kvm_read_and_reset_apf_flags(void)
{
	return 0;
}

static __always_inline bool kvm_handle_async_pf(struct pt_regs *regs, u32 token)
{
	return false;
}
#endif

#endif /* _ASM_X86_KVM_PARA_H */

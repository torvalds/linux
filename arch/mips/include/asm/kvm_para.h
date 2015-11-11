#ifndef _ASM_MIPS_KVM_PARA_H
#define _ASM_MIPS_KVM_PARA_H

#include <uapi/asm/kvm_para.h>

#define KVM_HYPERCALL ".word 0x42000028"

/*
 * Hypercalls for KVM.
 *
 * Hypercall number is passed in v0.
 * Return value will be placed in v0.
 * Up to 3 arguments are passed in a0, a1, and a2.
 */
static inline unsigned long kvm_hypercall0(unsigned long num)
{
	register unsigned long n asm("v0");
	register unsigned long r asm("v0");

	n = num;
	__asm__ __volatile__(
		KVM_HYPERCALL
		: "=r" (r) : "r" (n) : "memory"
		);

	return r;
}

static inline unsigned long kvm_hypercall1(unsigned long num,
					unsigned long arg0)
{
	register unsigned long n asm("v0");
	register unsigned long r asm("v0");
	register unsigned long a0 asm("a0");

	n = num;
	a0 = arg0;
	__asm__ __volatile__(
		KVM_HYPERCALL
		: "=r" (r) : "r" (n), "r" (a0) : "memory"
		);

	return r;
}

static inline unsigned long kvm_hypercall2(unsigned long num,
					unsigned long arg0, unsigned long arg1)
{
	register unsigned long n asm("v0");
	register unsigned long r asm("v0");
	register unsigned long a0 asm("a0");
	register unsigned long a1 asm("a1");

	n = num;
	a0 = arg0;
	a1 = arg1;
	__asm__ __volatile__(
		KVM_HYPERCALL
		: "=r" (r) : "r" (n), "r" (a0), "r" (a1) : "memory"
		);

	return r;
}

static inline unsigned long kvm_hypercall3(unsigned long num,
	unsigned long arg0, unsigned long arg1, unsigned long arg2)
{
	register unsigned long n asm("v0");
	register unsigned long r asm("v0");
	register unsigned long a0 asm("a0");
	register unsigned long a1 asm("a1");
	register unsigned long a2 asm("a2");

	n = num;
	a0 = arg0;
	a1 = arg1;
	a2 = arg2;
	__asm__ __volatile__(
		KVM_HYPERCALL
		: "=r" (r) : "r" (n), "r" (a0), "r" (a1), "r" (a2) : "memory"
		);

	return r;
}

static inline bool kvm_check_and_clear_guest_paused(void)
{
	return false;
}

static inline unsigned int kvm_arch_para_features(void)
{
	return 0;
}

#ifdef CONFIG_MIPS_PARAVIRT
static inline bool kvm_para_available(void)
{
	return true;
}
#else
static inline bool kvm_para_available(void)
{
	return false;
}
#endif


#endif /* _ASM_MIPS_KVM_PARA_H */

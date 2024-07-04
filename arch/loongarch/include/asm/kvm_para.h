/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LOONGARCH_KVM_PARA_H
#define _ASM_LOONGARCH_KVM_PARA_H

/*
 * Hypercall code field
 */
#define HYPERVISOR_KVM			1
#define HYPERVISOR_VENDOR_SHIFT		8
#define HYPERCALL_ENCODE(vendor, code)	((vendor << HYPERVISOR_VENDOR_SHIFT) + code)

#define KVM_HCALL_CODE_SERVICE		0
#define KVM_HCALL_CODE_SWDBG		1

#define KVM_HCALL_SERVICE		HYPERCALL_ENCODE(HYPERVISOR_KVM, KVM_HCALL_CODE_SERVICE)
#define  KVM_HCALL_FUNC_IPI		1

#define KVM_HCALL_SWDBG			HYPERCALL_ENCODE(HYPERVISOR_KVM, KVM_HCALL_CODE_SWDBG)

/*
 * LoongArch hypercall return code
 */
#define KVM_HCALL_SUCCESS		0
#define KVM_HCALL_INVALID_CODE		-1UL
#define KVM_HCALL_INVALID_PARAMETER	-2UL

/*
 * Hypercall interface for KVM hypervisor
 *
 * a0: function identifier
 * a1-a6: args
 * Return value will be placed in a0.
 * Up to 6 arguments are passed in a1, a2, a3, a4, a5, a6.
 */
static __always_inline long kvm_hypercall0(u64 fid)
{
	register long ret asm("a0");
	register unsigned long fun asm("a0") = fid;

	__asm__ __volatile__(
		"hvcl "__stringify(KVM_HCALL_SERVICE)
		: "=r" (ret)
		: "r" (fun)
		: "memory"
		);

	return ret;
}

static __always_inline long kvm_hypercall1(u64 fid, unsigned long arg0)
{
	register long ret asm("a0");
	register unsigned long fun asm("a0") = fid;
	register unsigned long a1  asm("a1") = arg0;

	__asm__ __volatile__(
		"hvcl "__stringify(KVM_HCALL_SERVICE)
		: "=r" (ret)
		: "r" (fun), "r" (a1)
		: "memory"
		);

	return ret;
}

static __always_inline long kvm_hypercall2(u64 fid,
		unsigned long arg0, unsigned long arg1)
{
	register long ret asm("a0");
	register unsigned long fun asm("a0") = fid;
	register unsigned long a1  asm("a1") = arg0;
	register unsigned long a2  asm("a2") = arg1;

	__asm__ __volatile__(
		"hvcl "__stringify(KVM_HCALL_SERVICE)
		: "=r" (ret)
		: "r" (fun), "r" (a1), "r" (a2)
		: "memory"
		);

	return ret;
}

static __always_inline long kvm_hypercall3(u64 fid,
	unsigned long arg0, unsigned long arg1, unsigned long arg2)
{
	register long ret asm("a0");
	register unsigned long fun asm("a0") = fid;
	register unsigned long a1  asm("a1") = arg0;
	register unsigned long a2  asm("a2") = arg1;
	register unsigned long a3  asm("a3") = arg2;

	__asm__ __volatile__(
		"hvcl "__stringify(KVM_HCALL_SERVICE)
		: "=r" (ret)
		: "r" (fun), "r" (a1), "r" (a2), "r" (a3)
		: "memory"
		);

	return ret;
}

static __always_inline long kvm_hypercall4(u64 fid,
		unsigned long arg0, unsigned long arg1,
		unsigned long arg2, unsigned long arg3)
{
	register long ret asm("a0");
	register unsigned long fun asm("a0") = fid;
	register unsigned long a1  asm("a1") = arg0;
	register unsigned long a2  asm("a2") = arg1;
	register unsigned long a3  asm("a3") = arg2;
	register unsigned long a4  asm("a4") = arg3;

	__asm__ __volatile__(
		"hvcl "__stringify(KVM_HCALL_SERVICE)
		: "=r" (ret)
		: "r"(fun), "r" (a1), "r" (a2), "r" (a3), "r" (a4)
		: "memory"
		);

	return ret;
}

static __always_inline long kvm_hypercall5(u64 fid,
		unsigned long arg0, unsigned long arg1,
		unsigned long arg2, unsigned long arg3, unsigned long arg4)
{
	register long ret asm("a0");
	register unsigned long fun asm("a0") = fid;
	register unsigned long a1  asm("a1") = arg0;
	register unsigned long a2  asm("a2") = arg1;
	register unsigned long a3  asm("a3") = arg2;
	register unsigned long a4  asm("a4") = arg3;
	register unsigned long a5  asm("a5") = arg4;

	__asm__ __volatile__(
		"hvcl "__stringify(KVM_HCALL_SERVICE)
		: "=r" (ret)
		: "r"(fun), "r" (a1), "r" (a2), "r" (a3), "r" (a4), "r" (a5)
		: "memory"
		);

	return ret;
}

static inline unsigned int kvm_arch_para_features(void)
{
	return 0;
}

static inline unsigned int kvm_arch_para_hints(void)
{
	return 0;
}

static inline bool kvm_check_and_clear_guest_paused(void)
{
	return false;
}

#endif /* _ASM_LOONGARCH_KVM_PARA_H */

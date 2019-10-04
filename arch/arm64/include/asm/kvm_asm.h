/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ARM_KVM_ASM_H__
#define __ARM_KVM_ASM_H__

#include <asm/hyp_image.h>
#include <asm/virt.h>

#define ARM_EXIT_WITH_SERROR_BIT  31
#define ARM_EXCEPTION_CODE(x)	  ((x) & ~(1U << ARM_EXIT_WITH_SERROR_BIT))
#define ARM_EXCEPTION_IS_TRAP(x)  (ARM_EXCEPTION_CODE((x)) == ARM_EXCEPTION_TRAP)
#define ARM_SERROR_PENDING(x)	  !!((x) & (1U << ARM_EXIT_WITH_SERROR_BIT))

#define ARM_EXCEPTION_IRQ	  0
#define ARM_EXCEPTION_EL1_SERROR  1
#define ARM_EXCEPTION_TRAP	  2
#define ARM_EXCEPTION_IL	  3
/* The hyp-stub will return this for any kvm_call_hyp() call */
#define ARM_EXCEPTION_HYP_GONE	  HVC_STUB_ERR

#define kvm_arm_exception_type					\
	{ARM_EXCEPTION_IRQ,		"IRQ"		},	\
	{ARM_EXCEPTION_EL1_SERROR, 	"SERROR"	},	\
	{ARM_EXCEPTION_TRAP, 		"TRAP"		},	\
	{ARM_EXCEPTION_HYP_GONE,	"HYP_GONE"	}

/*
 * Size of the HYP vectors preamble. kvm_patch_vector_branch() generates code
 * that jumps over this.
 */
#define KVM_VECTOR_PREAMBLE	(2 * AARCH64_INSN_SIZE)

#define __SMCCC_WORKAROUND_1_SMC_SZ 36

#define KVM_HOST_SMCCC_ID(id)						\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_64,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   (id))

#define KVM_HOST_SMCCC_FUNC(name) KVM_HOST_SMCCC_ID(__KVM_HOST_SMCCC_FUNC_##name)

#define __KVM_HOST_SMCCC_FUNC___kvm_hyp_init			0
#define __KVM_HOST_SMCCC_FUNC___kvm_vcpu_run			1
#define __KVM_HOST_SMCCC_FUNC___kvm_flush_vm_context		2
#define __KVM_HOST_SMCCC_FUNC___kvm_tlb_flush_vmid_ipa		3
#define __KVM_HOST_SMCCC_FUNC___kvm_tlb_flush_vmid		4
#define __KVM_HOST_SMCCC_FUNC___kvm_tlb_flush_local_vmid	5
#define __KVM_HOST_SMCCC_FUNC___kvm_timer_set_cntvoff		6
#define __KVM_HOST_SMCCC_FUNC___kvm_enable_ssbs			7
#define __KVM_HOST_SMCCC_FUNC___vgic_v3_get_ich_vtr_el2		8
#define __KVM_HOST_SMCCC_FUNC___vgic_v3_read_vmcr		9
#define __KVM_HOST_SMCCC_FUNC___vgic_v3_write_vmcr		10
#define __KVM_HOST_SMCCC_FUNC___vgic_v3_init_lrs		11
#define __KVM_HOST_SMCCC_FUNC___kvm_get_mdcr_el2		12
#define __KVM_HOST_SMCCC_FUNC___vgic_v3_save_aprs		13
#define __KVM_HOST_SMCCC_FUNC___vgic_v3_restore_aprs		14

#ifndef __ASSEMBLY__

#include <linux/mm.h>

#define DECLARE_KVM_VHE_SYM(sym)	extern char sym[]
#define DECLARE_KVM_NVHE_SYM(sym)	extern char kvm_nvhe_sym(sym)[]

/*
 * Define a pair of symbols sharing the same name but one defined in
 * VHE and the other in nVHE hyp implementations.
 */
#define DECLARE_KVM_HYP_SYM(sym)		\
	DECLARE_KVM_VHE_SYM(sym);		\
	DECLARE_KVM_NVHE_SYM(sym)

#define DECLARE_KVM_VHE_PER_CPU(type, sym)	\
	DECLARE_PER_CPU(type, sym)
#define DECLARE_KVM_NVHE_PER_CPU(type, sym)	\
	DECLARE_PER_CPU(type, kvm_nvhe_sym(sym))

#define DECLARE_KVM_HYP_PER_CPU(type, sym)	\
	DECLARE_KVM_VHE_PER_CPU(type, sym);	\
	DECLARE_KVM_NVHE_PER_CPU(type, sym)

/*
 * Compute pointer to a symbol defined in nVHE percpu region.
 * Returns NULL if percpu memory has not been allocated yet.
 */
#define this_cpu_ptr_nvhe_sym(sym)	per_cpu_ptr_nvhe_sym(sym, smp_processor_id())
#define per_cpu_ptr_nvhe_sym(sym, cpu)						\
	({									\
		unsigned long base, off;					\
		base = kvm_arm_hyp_percpu_base[cpu];				\
		off = (unsigned long)&CHOOSE_NVHE_SYM(sym) -			\
		      (unsigned long)&CHOOSE_NVHE_SYM(__per_cpu_start);		\
		base ? (typeof(CHOOSE_NVHE_SYM(sym))*)(base + off) : NULL;	\
	})

#if defined(__KVM_NVHE_HYPERVISOR__)

#define CHOOSE_NVHE_SYM(sym)	sym
#define CHOOSE_HYP_SYM(sym)	CHOOSE_NVHE_SYM(sym)

/* The nVHE hypervisor shouldn't even try to access VHE symbols */
extern void *__nvhe_undefined_symbol;
#define CHOOSE_VHE_SYM(sym)		__nvhe_undefined_symbol
#define this_cpu_ptr_hyp_sym(sym)	(&__nvhe_undefined_symbol)
#define per_cpu_ptr_hyp_sym(sym, cpu)	(&__nvhe_undefined_symbol)

#elif defined(__KVM_VHE_HYPERVISOR__)

#define CHOOSE_VHE_SYM(sym)	sym
#define CHOOSE_HYP_SYM(sym)	CHOOSE_VHE_SYM(sym)

/* The VHE hypervisor shouldn't even try to access nVHE symbols */
extern void *__vhe_undefined_symbol;
#define CHOOSE_NVHE_SYM(sym)		__vhe_undefined_symbol
#define this_cpu_ptr_hyp_sym(sym)	(&__vhe_undefined_symbol)
#define per_cpu_ptr_hyp_sym(sym, cpu)	(&__vhe_undefined_symbol)

#else

/*
 * BIG FAT WARNINGS:
 *
 * - Don't be tempted to change the following is_kernel_in_hyp_mode()
 *   to has_vhe(). has_vhe() is implemented as a *final* capability,
 *   while this is used early at boot time, when the capabilities are
 *   not final yet....
 *
 * - Don't let the nVHE hypervisor have access to this, as it will
 *   pick the *wrong* symbol (yes, it runs at EL2...).
 */
#define CHOOSE_HYP_SYM(sym)		(is_kernel_in_hyp_mode()	\
					   ? CHOOSE_VHE_SYM(sym)	\
					   : CHOOSE_NVHE_SYM(sym))

#define this_cpu_ptr_hyp_sym(sym)	(is_kernel_in_hyp_mode()	\
					   ? this_cpu_ptr(&sym)		\
					   : this_cpu_ptr_nvhe_sym(sym))

#define per_cpu_ptr_hyp_sym(sym, cpu)	(is_kernel_in_hyp_mode()	\
					   ? per_cpu_ptr(&sym, cpu)	\
					   : per_cpu_ptr_nvhe_sym(sym, cpu))

#define CHOOSE_VHE_SYM(sym)	sym
#define CHOOSE_NVHE_SYM(sym)	kvm_nvhe_sym(sym)

#endif

/* Translate a kernel address @ptr into its equivalent linear mapping */
#define kvm_ksym_ref(ptr)						\
	({								\
		void *val = (ptr);					\
		if (!is_kernel_in_hyp_mode())				\
			val = lm_alias((ptr));				\
		val;							\
	 })
#define kvm_ksym_ref_nvhe(sym)	kvm_ksym_ref(__va_function(kvm_nvhe_sym(sym)))

struct kvm;
struct kvm_vcpu;
struct kvm_s2_mmu;

DECLARE_KVM_NVHE_SYM(__kvm_hyp_init);
DECLARE_KVM_NVHE_SYM(__kvm_hyp_host_vector);
DECLARE_KVM_HYP_SYM(__kvm_hyp_vector);
#define __kvm_hyp_init		CHOOSE_NVHE_SYM(__kvm_hyp_init)
#define __kvm_hyp_host_vector	CHOOSE_NVHE_SYM(__kvm_hyp_host_vector)
#define __kvm_hyp_vector	CHOOSE_HYP_SYM(__kvm_hyp_vector)

extern unsigned long kvm_arm_hyp_percpu_base[NR_CPUS];
DECLARE_KVM_NVHE_SYM(__per_cpu_start);
DECLARE_KVM_NVHE_SYM(__per_cpu_end);

extern atomic_t arm64_el2_vector_last_slot;
DECLARE_KVM_HYP_SYM(__bp_harden_hyp_vecs);
#define __bp_harden_hyp_vecs	CHOOSE_HYP_SYM(__bp_harden_hyp_vecs)

extern void __kvm_flush_vm_context(void);
extern void __kvm_tlb_flush_vmid_ipa(struct kvm_s2_mmu *mmu, phys_addr_t ipa,
				     int level);
extern void __kvm_tlb_flush_vmid(struct kvm_s2_mmu *mmu);
extern void __kvm_tlb_flush_local_vmid(struct kvm_s2_mmu *mmu);

extern void __kvm_timer_set_cntvoff(u64 cntvoff);

extern int __kvm_vcpu_run(struct kvm_vcpu *vcpu);

extern void __kvm_enable_ssbs(void);

extern u64 __vgic_v3_get_ich_vtr_el2(void);
extern u64 __vgic_v3_read_vmcr(void);
extern void __vgic_v3_write_vmcr(u32 vmcr);
extern void __vgic_v3_init_lrs(void);

extern u32 __kvm_get_mdcr_el2(void);

extern char __smccc_workaround_1_smc[__SMCCC_WORKAROUND_1_SMC_SZ];

/*
 * Obtain the PC-relative address of a kernel symbol
 * s: symbol
 *
 * The goal of this macro is to return a symbol's address based on a
 * PC-relative computation, as opposed to a loading the VA from a
 * constant pool or something similar. This works well for HYP, as an
 * absolute VA is guaranteed to be wrong. Only use this if trying to
 * obtain the address of a symbol (i.e. not something you obtained by
 * following a pointer).
 */
#define hyp_symbol_addr(s)						\
	({								\
		typeof(s) *addr;					\
		asm("adrp	%0, %1\n"				\
		    "add	%0, %0, :lo12:%1\n"			\
		    : "=r" (addr) : "S" (&s));				\
		addr;							\
	})

#define __KVM_EXTABLE(from, to)						\
	"	.pushsection	__kvm_ex_table, \"a\"\n"		\
	"	.align		3\n"					\
	"	.long		(" #from " - .), (" #to " - .)\n"	\
	"	.popsection\n"


#define __kvm_at(at_op, addr)						\
( { 									\
	int __kvm_at_err = 0;						\
	u64 spsr, elr;							\
	asm volatile(							\
	"	mrs	%1, spsr_el2\n"					\
	"	mrs	%2, elr_el2\n"					\
	"1:	at	"at_op", %3\n"					\
	"	isb\n"							\
	"	b	9f\n"						\
	"2:	msr	spsr_el2, %1\n"					\
	"	msr	elr_el2, %2\n"					\
	"	mov	%w0, %4\n"					\
	"9:\n"								\
	__KVM_EXTABLE(1b, 2b)						\
	: "+r" (__kvm_at_err), "=&r" (spsr), "=&r" (elr)		\
	: "r" (addr), "i" (-EFAULT));					\
	__kvm_at_err;							\
} )


#else /* __ASSEMBLY__ */

.macro get_host_ctxt reg, tmp
	adr_this_cpu \reg, kvm_host_data, \tmp
	add	\reg, \reg, #HOST_DATA_CONTEXT
.endm

.macro get_vcpu_ptr vcpu, ctxt
	get_host_ctxt \ctxt, \vcpu
	ldr	\vcpu, [\ctxt, #HOST_CONTEXT_VCPU]
.endm

.macro get_loaded_vcpu vcpu, ctxt
	adr_this_cpu \ctxt, kvm_hyp_ctxt, \vcpu
	ldr	\vcpu, [\ctxt, #HOST_CONTEXT_VCPU]
.endm

.macro set_loaded_vcpu vcpu, ctxt, tmp
	adr_this_cpu \ctxt, kvm_hyp_ctxt, \tmp
	str	\vcpu, [\ctxt, #HOST_CONTEXT_VCPU]
.endm

/*
 * KVM extable for unexpected exceptions.
 * In the same format _asm_extable, but output to a different section so that
 * it can be mapped to EL2. The KVM version is not sorted. The caller must
 * ensure:
 * x18 has the hypervisor value to allow any Shadow-Call-Stack instrumented
 * code to write to it, and that SPSR_EL2 and ELR_EL2 are restored by the fixup.
 */
.macro	_kvm_extable, from, to
	.pushsection	__kvm_ex_table, "a"
	.align		3
	.long		(\from - .), (\to - .)
	.popsection
.endm

#define CPU_XREG_OFFSET(x)	(CPU_USER_PT_REGS + 8*x)
#define CPU_LR_OFFSET		CPU_XREG_OFFSET(30)
#define CPU_SP_EL0_OFFSET	(CPU_LR_OFFSET + 8)

/*
 * We treat x18 as callee-saved as the host may use it as a platform
 * register (e.g. for shadow call stack).
 */
.macro save_callee_saved_regs ctxt
	str	x18,      [\ctxt, #CPU_XREG_OFFSET(18)]
	stp	x19, x20, [\ctxt, #CPU_XREG_OFFSET(19)]
	stp	x21, x22, [\ctxt, #CPU_XREG_OFFSET(21)]
	stp	x23, x24, [\ctxt, #CPU_XREG_OFFSET(23)]
	stp	x25, x26, [\ctxt, #CPU_XREG_OFFSET(25)]
	stp	x27, x28, [\ctxt, #CPU_XREG_OFFSET(27)]
	stp	x29, lr,  [\ctxt, #CPU_XREG_OFFSET(29)]
.endm

.macro restore_callee_saved_regs ctxt
	// We require \ctxt is not x18-x28
	ldr	x18,      [\ctxt, #CPU_XREG_OFFSET(18)]
	ldp	x19, x20, [\ctxt, #CPU_XREG_OFFSET(19)]
	ldp	x21, x22, [\ctxt, #CPU_XREG_OFFSET(21)]
	ldp	x23, x24, [\ctxt, #CPU_XREG_OFFSET(23)]
	ldp	x25, x26, [\ctxt, #CPU_XREG_OFFSET(25)]
	ldp	x27, x28, [\ctxt, #CPU_XREG_OFFSET(27)]
	ldp	x29, lr,  [\ctxt, #CPU_XREG_OFFSET(29)]
.endm

.macro save_sp_el0 ctxt, tmp
	mrs	\tmp,	sp_el0
	str	\tmp,	[\ctxt, #CPU_SP_EL0_OFFSET]
.endm

.macro restore_sp_el0 ctxt, tmp
	ldr	\tmp,	  [\ctxt, #CPU_SP_EL0_OFFSET]
	msr	sp_el0, \tmp
.endm

#endif

#endif /* __ARM_KVM_ASM_H__ */

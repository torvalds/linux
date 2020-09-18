/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ARM_KVM_ASM_H__
#define __ARM_KVM_ASM_H__

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

#ifndef __ASSEMBLY__

#include <linux/mm.h>

/*
 * Translate name of a symbol defined in nVHE hyp to the name seen
 * by kernel proper. All nVHE symbols are prefixed by the build system
 * to avoid clashes with the VHE variants.
 */
#define kvm_nvhe_sym(sym)	__kvm_nvhe_##sym

#define DECLARE_KVM_VHE_SYM(sym)	extern char sym[]
#define DECLARE_KVM_NVHE_SYM(sym)	extern char kvm_nvhe_sym(sym)[]

/*
 * Define a pair of symbols sharing the same name but one defined in
 * VHE and the other in nVHE hyp implementations.
 */
#define DECLARE_KVM_HYP_SYM(sym)		\
	DECLARE_KVM_VHE_SYM(sym);		\
	DECLARE_KVM_NVHE_SYM(sym)

#define CHOOSE_VHE_SYM(sym)	sym
#define CHOOSE_NVHE_SYM(sym)	kvm_nvhe_sym(sym)

#ifndef __KVM_NVHE_HYPERVISOR__
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
#define CHOOSE_HYP_SYM(sym)	(is_kernel_in_hyp_mode() ? CHOOSE_VHE_SYM(sym) \
					   : CHOOSE_NVHE_SYM(sym))
#else
/* The nVHE hypervisor shouldn't even try to access anything */
extern void *__nvhe_undefined_symbol;
#define CHOOSE_HYP_SYM(sym)	__nvhe_undefined_symbol
#endif

/* Translate a kernel address @ptr into its equivalent linear mapping */
#define kvm_ksym_ref(ptr)						\
	({								\
		void *val = (ptr);					\
		if (!is_kernel_in_hyp_mode())				\
			val = lm_alias((ptr));				\
		val;							\
	 })
#define kvm_ksym_ref_nvhe(sym)	kvm_ksym_ref(kvm_nvhe_sym(sym))

struct kvm;
struct kvm_vcpu;
struct kvm_s2_mmu;

DECLARE_KVM_NVHE_SYM(__kvm_hyp_init);
DECLARE_KVM_HYP_SYM(__kvm_hyp_vector);
#define __kvm_hyp_init		CHOOSE_NVHE_SYM(__kvm_hyp_init)
#define __kvm_hyp_vector	CHOOSE_HYP_SYM(__kvm_hyp_vector)

#ifdef CONFIG_RANDOMIZE_BASE
extern atomic_t arm64_el2_vector_last_slot;
DECLARE_KVM_HYP_SYM(__bp_harden_hyp_vecs);
#define __bp_harden_hyp_vecs	CHOOSE_HYP_SYM(__bp_harden_hyp_vecs)
#endif

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

/*
 * Home-grown __this_cpu_{ptr,read} variants that always work at HYP,
 * provided that sym is really a *symbol* and not a pointer obtained from
 * a data structure. As for SHIFT_PERCPU_PTR(), the creative casting keeps
 * sparse quiet.
 */
#define __hyp_this_cpu_ptr(sym)						\
	({								\
		void *__ptr;						\
		__verify_pcpu_ptr(&sym);				\
		__ptr = hyp_symbol_addr(sym);				\
		__ptr += read_sysreg(tpidr_el2);			\
		(typeof(sym) __kernel __force *)__ptr;			\
	 })

#define __hyp_this_cpu_read(sym)					\
	({								\
		*__hyp_this_cpu_ptr(sym);				\
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

.macro hyp_adr_this_cpu reg, sym, tmp
	adr_l	\reg, \sym
	mrs	\tmp, tpidr_el2
	add	\reg, \reg, \tmp
.endm

.macro hyp_ldr_this_cpu reg, sym, tmp
	adr_l	\reg, \sym
	mrs	\tmp, tpidr_el2
	ldr	\reg,  [\reg, \tmp]
.endm

.macro get_host_ctxt reg, tmp
	hyp_adr_this_cpu \reg, kvm_host_data, \tmp
	add	\reg, \reg, #HOST_DATA_CONTEXT
.endm

.macro get_vcpu_ptr vcpu, ctxt
	get_host_ctxt \ctxt, \vcpu
	ldr	\vcpu, [\ctxt, #HOST_CONTEXT_VCPU]
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

#endif

#endif /* __ARM_KVM_ASM_H__ */

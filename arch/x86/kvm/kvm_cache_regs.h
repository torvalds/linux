/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_KVM_CACHE_REGS_H
#define ASM_KVM_CACHE_REGS_H

#include <linux/kvm_host.h>

#define KVM_POSSIBLE_CR0_GUEST_BITS	(X86_CR0_TS | X86_CR0_WP)
#define KVM_POSSIBLE_CR4_GUEST_BITS				  \
	(X86_CR4_PVI | X86_CR4_DE | X86_CR4_PCE | X86_CR4_OSFXSR  \
	 | X86_CR4_OSXMMEXCPT | X86_CR4_PGE | X86_CR4_TSD | X86_CR4_FSGSBASE)

#define X86_CR0_PDPTR_BITS    (X86_CR0_CD | X86_CR0_NW | X86_CR0_PG)
#define X86_CR4_TLBFLUSH_BITS (X86_CR4_PGE | X86_CR4_PCIDE | X86_CR4_PAE | X86_CR4_SMEP)
#define X86_CR4_PDPTR_BITS    (X86_CR4_PGE | X86_CR4_PSE | X86_CR4_PAE | X86_CR4_SMEP)

static_assert(!(KVM_POSSIBLE_CR0_GUEST_BITS & X86_CR0_PDPTR_BITS));

#define BUILD_KVM_GPR_ACCESSORS(lname, uname)				      \
static __always_inline unsigned long kvm_##lname##_read(struct kvm_vcpu *vcpu)\
{									      \
	return vcpu->arch.regs[VCPU_REGS_##uname];			      \
}									      \
static __always_inline void kvm_##lname##_write(struct kvm_vcpu *vcpu,	      \
						unsigned long val)	      \
{									      \
	vcpu->arch.regs[VCPU_REGS_##uname] = val;			      \
}
BUILD_KVM_GPR_ACCESSORS(rax, RAX)
BUILD_KVM_GPR_ACCESSORS(rbx, RBX)
BUILD_KVM_GPR_ACCESSORS(rcx, RCX)
BUILD_KVM_GPR_ACCESSORS(rdx, RDX)
BUILD_KVM_GPR_ACCESSORS(rbp, RBP)
BUILD_KVM_GPR_ACCESSORS(rsi, RSI)
BUILD_KVM_GPR_ACCESSORS(rdi, RDI)
#ifdef CONFIG_X86_64
BUILD_KVM_GPR_ACCESSORS(r8,  R8)
BUILD_KVM_GPR_ACCESSORS(r9,  R9)
BUILD_KVM_GPR_ACCESSORS(r10, R10)
BUILD_KVM_GPR_ACCESSORS(r11, R11)
BUILD_KVM_GPR_ACCESSORS(r12, R12)
BUILD_KVM_GPR_ACCESSORS(r13, R13)
BUILD_KVM_GPR_ACCESSORS(r14, R14)
BUILD_KVM_GPR_ACCESSORS(r15, R15)
#endif

/*
 * avail  dirty
 * 0	  0	  register in VMCS/VMCB
 * 0	  1	  *INVALID*
 * 1	  0	  register in vcpu->arch
 * 1	  1	  register in vcpu->arch, needs to be stored back
 */
static inline bool kvm_register_is_available(struct kvm_vcpu *vcpu,
					     enum kvm_reg reg)
{
	return test_bit(reg, (unsigned long *)&vcpu->arch.regs_avail);
}

static inline bool kvm_register_is_dirty(struct kvm_vcpu *vcpu,
					 enum kvm_reg reg)
{
	return test_bit(reg, (unsigned long *)&vcpu->arch.regs_dirty);
}

static inline void kvm_register_mark_available(struct kvm_vcpu *vcpu,
					       enum kvm_reg reg)
{
	__set_bit(reg, (unsigned long *)&vcpu->arch.regs_avail);
}

static inline void kvm_register_mark_dirty(struct kvm_vcpu *vcpu,
					   enum kvm_reg reg)
{
	__set_bit(reg, (unsigned long *)&vcpu->arch.regs_avail);
	__set_bit(reg, (unsigned long *)&vcpu->arch.regs_dirty);
}

/*
 * kvm_register_test_and_mark_available() is a special snowflake that uses an
 * arch bitop directly to avoid the explicit instrumentation that comes with
 * the generic bitops.  This allows code that cannot be instrumented (noinstr
 * functions), e.g. the low level VM-Enter/VM-Exit paths, to cache registers.
 */
static __always_inline bool kvm_register_test_and_mark_available(struct kvm_vcpu *vcpu,
								 enum kvm_reg reg)
{
	return arch___test_and_set_bit(reg, (unsigned long *)&vcpu->arch.regs_avail);
}

/*
 * The "raw" register helpers are only for cases where the full 64 bits of a
 * register are read/written irrespective of current vCPU mode.  In other words,
 * odds are good you shouldn't be using the raw variants.
 */
static inline unsigned long kvm_register_read_raw(struct kvm_vcpu *vcpu, int reg)
{
	if (WARN_ON_ONCE((unsigned int)reg >= NR_VCPU_REGS))
		return 0;

	if (!kvm_register_is_available(vcpu, reg))
		kvm_x86_call(cache_reg)(vcpu, reg);

	return vcpu->arch.regs[reg];
}

static inline void kvm_register_write_raw(struct kvm_vcpu *vcpu, int reg,
					  unsigned long val)
{
	if (WARN_ON_ONCE((unsigned int)reg >= NR_VCPU_REGS))
		return;

	vcpu->arch.regs[reg] = val;
	kvm_register_mark_dirty(vcpu, reg);
}

static inline unsigned long kvm_rip_read(struct kvm_vcpu *vcpu)
{
	return kvm_register_read_raw(vcpu, VCPU_REGS_RIP);
}

static inline void kvm_rip_write(struct kvm_vcpu *vcpu, unsigned long val)
{
	kvm_register_write_raw(vcpu, VCPU_REGS_RIP, val);
}

static inline unsigned long kvm_rsp_read(struct kvm_vcpu *vcpu)
{
	return kvm_register_read_raw(vcpu, VCPU_REGS_RSP);
}

static inline void kvm_rsp_write(struct kvm_vcpu *vcpu, unsigned long val)
{
	kvm_register_write_raw(vcpu, VCPU_REGS_RSP, val);
}

static inline u64 kvm_pdptr_read(struct kvm_vcpu *vcpu, int index)
{
	might_sleep();  /* on svm */

	if (!kvm_register_is_available(vcpu, VCPU_EXREG_PDPTR))
		kvm_x86_call(cache_reg)(vcpu, VCPU_EXREG_PDPTR);

	return vcpu->arch.walk_mmu->pdptrs[index];
}

static inline void kvm_pdptr_write(struct kvm_vcpu *vcpu, int index, u64 value)
{
	vcpu->arch.walk_mmu->pdptrs[index] = value;
}

static inline ulong kvm_read_cr0_bits(struct kvm_vcpu *vcpu, ulong mask)
{
	ulong tmask = mask & KVM_POSSIBLE_CR0_GUEST_BITS;
	if ((tmask & vcpu->arch.cr0_guest_owned_bits) &&
	    !kvm_register_is_available(vcpu, VCPU_EXREG_CR0))
		kvm_x86_call(cache_reg)(vcpu, VCPU_EXREG_CR0);
	return vcpu->arch.cr0 & mask;
}

static __always_inline bool kvm_is_cr0_bit_set(struct kvm_vcpu *vcpu,
					       unsigned long cr0_bit)
{
	BUILD_BUG_ON(!is_power_of_2(cr0_bit));

	return !!kvm_read_cr0_bits(vcpu, cr0_bit);
}

static inline ulong kvm_read_cr0(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr0_bits(vcpu, ~0UL);
}

static inline ulong kvm_read_cr4_bits(struct kvm_vcpu *vcpu, ulong mask)
{
	ulong tmask = mask & KVM_POSSIBLE_CR4_GUEST_BITS;
	if ((tmask & vcpu->arch.cr4_guest_owned_bits) &&
	    !kvm_register_is_available(vcpu, VCPU_EXREG_CR4))
		kvm_x86_call(cache_reg)(vcpu, VCPU_EXREG_CR4);
	return vcpu->arch.cr4 & mask;
}

static __always_inline bool kvm_is_cr4_bit_set(struct kvm_vcpu *vcpu,
					       unsigned long cr4_bit)
{
	BUILD_BUG_ON(!is_power_of_2(cr4_bit));

	return !!kvm_read_cr4_bits(vcpu, cr4_bit);
}

static inline ulong kvm_read_cr3(struct kvm_vcpu *vcpu)
{
	if (!kvm_register_is_available(vcpu, VCPU_EXREG_CR3))
		kvm_x86_call(cache_reg)(vcpu, VCPU_EXREG_CR3);
	return vcpu->arch.cr3;
}

static inline ulong kvm_read_cr4(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr4_bits(vcpu, ~0UL);
}

static inline u64 kvm_read_edx_eax(struct kvm_vcpu *vcpu)
{
	return (kvm_rax_read(vcpu) & -1u)
		| ((u64)(kvm_rdx_read(vcpu) & -1u) << 32);
}

static inline void enter_guest_mode(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hflags |= HF_GUEST_MASK;
	vcpu->stat.guest_mode = 1;
}

static inline void leave_guest_mode(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hflags &= ~HF_GUEST_MASK;

	if (vcpu->arch.load_eoi_exitmap_pending) {
		vcpu->arch.load_eoi_exitmap_pending = false;
		kvm_make_request(KVM_REQ_LOAD_EOI_EXITMAP, vcpu);
	}

	vcpu->stat.guest_mode = 0;
}

static inline bool is_guest_mode(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hflags & HF_GUEST_MASK;
}

#endif

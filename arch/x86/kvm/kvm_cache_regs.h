#ifndef ASM_KVM_CACHE_REGS_H
#define ASM_KVM_CACHE_REGS_H

#define KVM_POSSIBLE_CR0_GUEST_BITS X86_CR0_TS
#define KVM_POSSIBLE_CR4_GUEST_BITS				  \
	(X86_CR4_PVI | X86_CR4_DE | X86_CR4_PCE | X86_CR4_OSFXSR  \
	 | X86_CR4_OSXMMEXCPT | X86_CR4_PGE)

static inline unsigned long kvm_register_read(struct kvm_vcpu *vcpu,
					      enum kvm_reg reg)
{
	if (!test_bit(reg, (unsigned long *)&vcpu->arch.regs_avail))
		kvm_x86_ops->cache_reg(vcpu, reg);

	return vcpu->arch.regs[reg];
}

static inline void kvm_register_write(struct kvm_vcpu *vcpu,
				      enum kvm_reg reg,
				      unsigned long val)
{
	vcpu->arch.regs[reg] = val;
	__set_bit(reg, (unsigned long *)&vcpu->arch.regs_dirty);
	__set_bit(reg, (unsigned long *)&vcpu->arch.regs_avail);
}

static inline unsigned long kvm_rip_read(struct kvm_vcpu *vcpu)
{
	return kvm_register_read(vcpu, VCPU_REGS_RIP);
}

static inline void kvm_rip_write(struct kvm_vcpu *vcpu, unsigned long val)
{
	kvm_register_write(vcpu, VCPU_REGS_RIP, val);
}

static inline u64 kvm_pdptr_read(struct kvm_vcpu *vcpu, int index)
{
	might_sleep();  /* on svm */

	if (!test_bit(VCPU_EXREG_PDPTR,
		      (unsigned long *)&vcpu->arch.regs_avail))
		kvm_x86_ops->cache_reg(vcpu, VCPU_EXREG_PDPTR);

	return vcpu->arch.walk_mmu->pdptrs[index];
}

static inline ulong kvm_read_cr0_bits(struct kvm_vcpu *vcpu, ulong mask)
{
	ulong tmask = mask & KVM_POSSIBLE_CR0_GUEST_BITS;
	if (tmask & vcpu->arch.cr0_guest_owned_bits)
		kvm_x86_ops->decache_cr0_guest_bits(vcpu);
	return vcpu->arch.cr0 & mask;
}

static inline ulong kvm_read_cr0(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr0_bits(vcpu, ~0UL);
}

static inline ulong kvm_read_cr4_bits(struct kvm_vcpu *vcpu, ulong mask)
{
	ulong tmask = mask & KVM_POSSIBLE_CR4_GUEST_BITS;
	if (tmask & vcpu->arch.cr4_guest_owned_bits)
		kvm_x86_ops->decache_cr4_guest_bits(vcpu);
	return vcpu->arch.cr4 & mask;
}

static inline ulong kvm_read_cr3(struct kvm_vcpu *vcpu)
{
	if (!test_bit(VCPU_EXREG_CR3, (ulong *)&vcpu->arch.regs_avail))
		kvm_x86_ops->decache_cr3(vcpu);
	return vcpu->arch.cr3;
}

static inline ulong kvm_read_cr4(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr4_bits(vcpu, ~0UL);
}

static inline u64 kvm_read_edx_eax(struct kvm_vcpu *vcpu)
{
	return (kvm_register_read(vcpu, VCPU_REGS_RAX) & -1u)
		| ((u64)(kvm_register_read(vcpu, VCPU_REGS_RDX) & -1u) << 32);
}

static inline void enter_guest_mode(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hflags |= HF_GUEST_MASK;
}

static inline void leave_guest_mode(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hflags &= ~HF_GUEST_MASK;
}

static inline bool is_guest_mode(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hflags & HF_GUEST_MASK;
}

#endif

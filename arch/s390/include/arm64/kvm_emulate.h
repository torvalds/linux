/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __S390_ARM64_KVM_EMULATE_H__
#define __S390_ARM64_KVM_EMULATE_H__

#include <asm/fault.h>
#include <linux/kvm_host.h>

#include <arm64/kvm_nested.h>
#include <arm64/ptrace.h>
#include <arm64/kvm_arm.h>
#include <arm64/sysreg.h>

static __always_inline unsigned long *vcpu_pc(const struct kvm_vcpu *vcpu)
{
	return (unsigned long *)&vcpu->arch.sae_block.pc;
}

static __always_inline unsigned long *vcpu_cpsr(const struct kvm_vcpu *vcpu)
{
	return (unsigned long *)&vcpu->arch.sae_block.pstate;
}

static __always_inline unsigned long *vcpu_sp_el0(const struct kvm_vcpu *vcpu)
{
	return (unsigned long *)&vcpu->arch.sae_block.sp_el0;
}

static __always_inline u64 *vcpu_sp_el1(struct kvm_vcpu *vcpu)
{
	return &vcpu->arch.sae_block.sp_el1;
}

static __always_inline __vector128 *vcpu_vreg(struct kvm_vcpu *vcpu, int off)
{
	return &vcpu->arch.ctxt.vregs[off];
}

static __always_inline u32 *vcpu_fpsr(struct kvm_vcpu *vcpu)
{
	return &vcpu->arch.sae_block.fpsr;
}

static __always_inline u32 *vcpu_fpcr(struct kvm_vcpu *vcpu)
{
	return &vcpu->arch.sae_block.fpcr;
}

static __always_inline bool vcpu_mode_is_32bit(const struct kvm_vcpu *vcpu)
{
	return false;
}

static __always_inline u64 kvm_vcpu_get_esr(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.sae_block.hai.esr_el2;
}

static inline unsigned long *vcpu_hcr(struct kvm_vcpu *vcpu)
{
	return (unsigned long *)&vcpu->arch.hcr_el2;
}

static __always_inline unsigned long kvm_vcpu_get_hfar(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.sae_block.hai.far_el2;
}

static __always_inline phys_addr_t kvm_vcpu_get_fault_ipa(const struct kvm_vcpu *vcpu)
{
	return gfn_to_gpa(vcpu->arch.sae_block.hai.teid.addr);
}

static inline u16 kvm_vcpu_fault_pic(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.sae_block.hai.pic & PGM_INT_CODE_MASK;
}

static __always_inline
bool kvm_vcpu_trap_is_permission_fault(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_fault_pic(vcpu) == PGM_PROTECTION;
}

static __always_inline bool kvm_condition_valid(const struct kvm_vcpu *vcpu)
{
	return true;
}

static __always_inline bool vcpu_el1_is_32bit(struct kvm_vcpu *vcpu)
{
	return false;
}

static inline bool kvm_vcpu_is_be(struct kvm_vcpu *vcpu)
{
	return false;
}

static inline int kvm_vcpu_abt_gltl(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.sae_block.hai.gltl;
}

static inline bool vcpu_mode_priv(const struct kvm_vcpu *vcpu)
{
	u32 mode = *vcpu_cpsr(vcpu) & PSR_MODE_MASK;

	return mode != PSR_MODE_EL0t;
}

static inline void kvm_skip_instr(struct kvm_vcpu *vcpu)
{
	*vcpu_pc(vcpu) += 4;
	*vcpu_cpsr(vcpu) &= ~PSR_BTYPE_MASK;

	/* advance the singlestep state machine */
	*vcpu_cpsr(vcpu) &= ~SPSR_ELx_SS;
}

static inline void kvm_reset_fpsimd(struct kvm_vcpu *vcpu)
{
	memset(vcpu->arch.ctxt.vregs, 0, sizeof(vcpu->arch.ctxt.vregs));
	vcpu->arch.sae_block.fpsr = 0;
	vcpu->arch.sae_block.fpcr = 0;
}

#include <arm64/kvm_emulate-gen.h>

#endif /* __S390_ARM64_KVM_EMULATE_H__ */

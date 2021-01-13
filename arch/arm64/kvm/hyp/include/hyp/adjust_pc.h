// SPDX-License-Identifier: GPL-2.0-only
/*
 * Guest PC manipulation helpers
 *
 * Copyright (C) 2012,2013 - ARM Ltd
 * Copyright (C) 2020 - Google LLC
 * Author: Marc Zyngier <maz@kernel.org>
 */

#ifndef __ARM64_KVM_HYP_ADJUST_PC_H__
#define __ARM64_KVM_HYP_ADJUST_PC_H__

#include <asm/kvm_emulate.h>
#include <asm/kvm_host.h>

void kvm_inject_exception(struct kvm_vcpu *vcpu);

static inline void kvm_skip_instr(struct kvm_vcpu *vcpu)
{
	if (vcpu_mode_is_32bit(vcpu)) {
		kvm_skip_instr32(vcpu);
	} else {
		*vcpu_pc(vcpu) += 4;
		*vcpu_cpsr(vcpu) &= ~PSR_BTYPE_MASK;
	}

	/* advance the singlestep state machine */
	*vcpu_cpsr(vcpu) &= ~DBG_SPSR_SS;
}

/*
 * Skip an instruction which has been emulated at hyp while most guest sysregs
 * are live.
 */
static inline void __kvm_skip_instr(struct kvm_vcpu *vcpu)
{
	*vcpu_pc(vcpu) = read_sysreg_el2(SYS_ELR);
	vcpu_gp_regs(vcpu)->pstate = read_sysreg_el2(SYS_SPSR);

	kvm_skip_instr(vcpu);

	write_sysreg_el2(vcpu_gp_regs(vcpu)->pstate, SYS_SPSR);
	write_sysreg_el2(*vcpu_pc(vcpu), SYS_ELR);
}

/*
 * Adjust the guest PC on entry, depending on flags provided by EL1
 * for the purpose of emulation (MMIO, sysreg) or exception injection.
 */
static inline void __adjust_pc(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.flags & KVM_ARM64_PENDING_EXCEPTION) {
		kvm_inject_exception(vcpu);
		vcpu->arch.flags &= ~(KVM_ARM64_PENDING_EXCEPTION |
				      KVM_ARM64_EXCEPT_MASK);
	} else 	if (vcpu->arch.flags & KVM_ARM64_INCREMENT_PC) {
		kvm_skip_instr(vcpu);
		vcpu->arch.flags &= ~KVM_ARM64_INCREMENT_PC;
	}
}

/*
 * Skip an instruction while host sysregs are live.
 * Assumes host is always 64-bit.
 */
static inline void kvm_skip_host_instr(void)
{
	write_sysreg_el2(read_sysreg_el2(SYS_ELR) + 4, SYS_ELR);
}

#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_KVM_SMM_H
#define ASM_KVM_SMM_H

#define GET_SMSTATE(type, buf, offset)		\
	(*(type *)((buf) + (offset) - 0x7e00))

#define PUT_SMSTATE(type, buf, offset, val)                      \
	*(type *)((buf) + (offset) - 0x7e00) = val

#ifdef CONFIG_KVM_SMM
static inline int kvm_inject_smi(struct kvm_vcpu *vcpu)
{
	kvm_make_request(KVM_REQ_SMI, vcpu);
	return 0;
}

static inline bool is_smm(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hflags & HF_SMM_MASK;
}

void kvm_smm_changed(struct kvm_vcpu *vcpu, bool in_smm);
void enter_smm(struct kvm_vcpu *vcpu);
int emulator_leave_smm(struct x86_emulate_ctxt *ctxt);
void process_smi(struct kvm_vcpu *vcpu);
#else
static inline int kvm_inject_smi(struct kvm_vcpu *vcpu) { return -ENOTTY; }
static inline bool is_smm(struct kvm_vcpu *vcpu) { return false; }

/*
 * emulator_leave_smm is used as a function pointer, so the
 * stub is defined in x86.c.
 */
#endif

#endif

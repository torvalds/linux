// SPDX-License-Identifier: GPL-2.0

#include <linux/kvm_host.h>
#include <linux/fpu.h>

#include <arm64/kvm_emulate.h>
#include <arm64/kvm_nested.h>

bool kvm_arm_vcpu_is_finalized(struct kvm_vcpu *vcpu)
{
	return true;
}

static inline void vcpu_reset_hcr(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hcr_el2 = HCR_EL2_E2H | HCR_EL2_RW | HCR_EL2_AMO |
			     HCR_EL2_IMO | HCR_EL2_FMO | HCR_EL2_PTW;
	/* traps */
	vcpu->arch.hcr_el2 |= HCR_EL2_TSC | HCR_EL2_TID1 | HCR_EL2_TID2 |
			      HCR_EL2_TID3 | HCR_EL2_TID4 | HCR_EL2_TID5 |
			      HCR_EL2_TIDCP | HCR_EL2_TLOR;
}

void kvm_reset_vcpu(struct kvm_vcpu *vcpu)
{
	struct vcpu_reset_state reset_state;
	bool loaded;

	scoped_guard(spinlock, &vcpu->arch.mp_state_lock) {
		reset_state = vcpu->arch.reset_state;
		vcpu->arch.reset_state.reset = false;
	}

	/*
	 * Disable preemption around the vcpu reset as we might otherwise race with
	 * preempt notifiers which call stiasrm/lasrm from put/load
	 */
	preempt_disable();

	/* The reset must run with an unloaded save area */
	loaded = vcpu_is_loaded(vcpu);
	if (loaded)
		vcpu_put(vcpu);

	kvm_reset_vcpu_core(vcpu);

	/* Reset special registers */
	vcpu_reset_hcr(vcpu);

	if (reset_state.reset) {
		*vcpu_pc(vcpu) = reset_state.pc;
		vcpu_clear_flag(vcpu, PENDING_EXCEPTION);
		vcpu_clear_flag(vcpu, EXCEPT_MASK);
		vcpu_clear_flag(vcpu, INCREMENT_PC);
		vcpu_set_reg(vcpu, 0, reset_state.r0);
	}

	/* Load new vx-regs into HW if they are currently loaded */
	if (current->thread.kfpu_flags)
		load_vx_regs(vcpu->arch.ctxt.vregs);

	if (loaded)
		vcpu_load(vcpu);

	preempt_enable();
}

int kvm_arm_vcpu_finalize(struct kvm_vcpu *vcpu, int feature)
{
	return -EINVAL;
}

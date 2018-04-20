// SPDX-License-Identifier: GPL-2.0
/*
 * arch/arm64/kvm/fpsimd.c: Guest/host FPSIMD context coordination helpers
 *
 * Copyright 2018 Arm Limited
 * Author: Dave Martin <Dave.Martin@arm.com>
 */
#include <linux/bottom_half.h>
#include <linux/sched.h>
#include <linux/thread_info.h>
#include <linux/kvm_host.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_host.h>
#include <asm/kvm_mmu.h>

/*
 * Called on entry to KVM_RUN unless this vcpu previously ran at least
 * once and the most recent prior KVM_RUN for this vcpu was called from
 * the same task as current (highly likely).
 *
 * This is guaranteed to execute before kvm_arch_vcpu_load_fp(vcpu),
 * such that on entering hyp the relevant parts of current are already
 * mapped.
 */
int kvm_arch_vcpu_run_map_fp(struct kvm_vcpu *vcpu)
{
	int ret;

	struct thread_info *ti = &current->thread_info;
	struct user_fpsimd_state *fpsimd = &current->thread.uw.fpsimd_state;

	/*
	 * Make sure the host task thread flags and fpsimd state are
	 * visible to hyp:
	 */
	ret = create_hyp_mappings(ti, ti + 1, PAGE_HYP);
	if (ret)
		goto error;

	ret = create_hyp_mappings(fpsimd, fpsimd + 1, PAGE_HYP);
	if (ret)
		goto error;

	vcpu->arch.host_thread_info = kern_hyp_va(ti);
	vcpu->arch.host_fpsimd_state = kern_hyp_va(fpsimd);
error:
	return ret;
}

/*
 * Prepare vcpu for saving the host's FPSIMD state and loading the guest's.
 * The actual loading is done by the FPSIMD access trap taken to hyp.
 *
 * Here, we just set the correct metadata to indicate that the FPSIMD
 * state in the cpu regs (if any) belongs to current on the host.
 *
 * TIF_SVE is backed up here, since it may get clobbered with guest state.
 * This flag is restored by kvm_arch_vcpu_put_fp(vcpu).
 */
void kvm_arch_vcpu_load_fp(struct kvm_vcpu *vcpu)
{
	BUG_ON(!current->mm);

	vcpu->arch.flags &= ~(KVM_ARM64_FP_ENABLED | KVM_ARM64_HOST_SVE_IN_USE);
	vcpu->arch.flags |= KVM_ARM64_FP_HOST;
	if (test_thread_flag(TIF_SVE))
		vcpu->arch.flags |= KVM_ARM64_HOST_SVE_IN_USE;
}

/*
 * If the guest FPSIMD state was loaded, update the host's context
 * tracking data mark the CPU FPSIMD regs as dirty and belonging to vcpu
 * so that they will be written back if the kernel clobbers them due to
 * kernel-mode NEON before re-entry into the guest.
 */
void kvm_arch_vcpu_ctxsync_fp(struct kvm_vcpu *vcpu)
{
	WARN_ON_ONCE(!irqs_disabled());

	if (vcpu->arch.flags & KVM_ARM64_FP_ENABLED) {
		fpsimd_bind_state_to_cpu(&vcpu->arch.ctxt.gp_regs.fp_regs);
		clear_thread_flag(TIF_FOREIGN_FPSTATE);
		clear_thread_flag(TIF_SVE);
	}
}

/*
 * Write back the vcpu FPSIMD regs if they are dirty, and invalidate the
 * cpu FPSIMD regs so that they can't be spuriously reused if this vcpu
 * disappears and another task or vcpu appears that recycles the same
 * struct fpsimd_state.
 */
void kvm_arch_vcpu_put_fp(struct kvm_vcpu *vcpu)
{
	local_bh_disable();

	update_thread_flag(TIF_SVE,
			   vcpu->arch.flags & KVM_ARM64_HOST_SVE_IN_USE);

	if (vcpu->arch.flags & KVM_ARM64_FP_ENABLED) {
		/* Clean guest FP state to memory and invalidate cpu view */
		fpsimd_save();
		fpsimd_flush_cpu_state();
	} else if (!test_thread_flag(TIF_FOREIGN_FPSTATE)) {
		/* Ensure user trap controls are correctly restored */
		fpsimd_bind_task_to_cpu();
	}

	local_bh_enable();
}

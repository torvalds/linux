// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Ventana Micro Systems Inc.
 */

#include <linux/kvm_host.h>
#include <linux/wordpart.h>

#include <asm/kvm_vcpu_sbi.h>
#include <asm/sbi.h>

static int kvm_sbi_ext_susp_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				    struct kvm_vcpu_sbi_return *retdata)
{
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	struct kvm_cpu_context *reset_cntx;
	unsigned long funcid = cp->a6;
	unsigned long hva, i;
	struct kvm_vcpu *tmp;

	switch (funcid) {
	case SBI_EXT_SUSP_SYSTEM_SUSPEND:
		if (lower_32_bits(cp->a0) != SBI_SUSP_SLEEP_TYPE_SUSPEND_TO_RAM) {
			retdata->err_val = SBI_ERR_INVALID_PARAM;
			return 0;
		}

		if (!(cp->sstatus & SR_SPP)) {
			retdata->err_val = SBI_ERR_FAILURE;
			return 0;
		}

		hva = kvm_vcpu_gfn_to_hva_prot(vcpu, cp->a1 >> PAGE_SHIFT, NULL);
		if (kvm_is_error_hva(hva)) {
			retdata->err_val = SBI_ERR_INVALID_ADDRESS;
			return 0;
		}

		kvm_for_each_vcpu(i, tmp, vcpu->kvm) {
			if (tmp == vcpu)
				continue;
			if (!kvm_riscv_vcpu_stopped(tmp)) {
				retdata->err_val = SBI_ERR_DENIED;
				return 0;
			}
		}

		spin_lock(&vcpu->arch.reset_cntx_lock);
		reset_cntx = &vcpu->arch.guest_reset_context;
		reset_cntx->sepc = cp->a1;
		reset_cntx->a0 = vcpu->vcpu_id;
		reset_cntx->a1 = cp->a2;
		spin_unlock(&vcpu->arch.reset_cntx_lock);

		kvm_make_request(KVM_REQ_VCPU_RESET, vcpu);

		/* userspace provides the suspend implementation */
		kvm_riscv_vcpu_sbi_forward(vcpu, run);
		retdata->uexit = true;
		break;
	default:
		retdata->err_val = SBI_ERR_NOT_SUPPORTED;
		break;
	}

	return 0;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_susp = {
	.extid_start = SBI_EXT_SUSP,
	.extid_end = SBI_EXT_SUSP,
	.default_disabled = true,
	.handler = kvm_sbi_ext_susp_handler,
};

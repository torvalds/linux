// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Ventana Micro Systems Inc.
 */

#include <linux/kvm_host.h>

#include <asm/kvm_vcpu_sbi.h>
#include <asm/sbi.h>

void kvm_riscv_vcpu_record_steal_time(struct kvm_vcpu *vcpu)
{
}

static int kvm_sbi_sta_steal_time_set_shmem(struct kvm_vcpu *vcpu)
{
	return SBI_ERR_FAILURE;
}

static int kvm_sbi_ext_sta_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				   struct kvm_vcpu_sbi_return *retdata)
{
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	unsigned long funcid = cp->a6;
	int ret;

	switch (funcid) {
	case SBI_EXT_STA_STEAL_TIME_SET_SHMEM:
		ret = kvm_sbi_sta_steal_time_set_shmem(vcpu);
		break;
	default:
		ret = SBI_ERR_NOT_SUPPORTED;
		break;
	}

	retdata->err_val = ret;

	return 0;
}

static unsigned long kvm_sbi_ext_sta_probe(struct kvm_vcpu *vcpu)
{
	return 0;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_sta = {
	.extid_start = SBI_EXT_STA,
	.extid_end = SBI_EXT_STA,
	.handler = kvm_sbi_ext_sta_handler,
	.probe = kvm_sbi_ext_sta_probe,
};

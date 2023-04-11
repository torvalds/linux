// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Rivos Inc
 *
 * Authors:
 *     Atish Patra <atishp@rivosinc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <asm/csr.h>
#include <asm/sbi.h>
#include <asm/kvm_vcpu_sbi.h>

static int kvm_sbi_ext_pmu_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				   struct kvm_vcpu_sbi_return *retdata)
{
	int ret = 0;
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);
	unsigned long funcid = cp->a6;
	u64 temp;

	if (!kvpmu->init_done) {
		retdata->err_val = SBI_ERR_NOT_SUPPORTED;
		return 0;
	}

	switch (funcid) {
	case SBI_EXT_PMU_NUM_COUNTERS:
		ret = kvm_riscv_vcpu_pmu_num_ctrs(vcpu, retdata);
		break;
	case SBI_EXT_PMU_COUNTER_GET_INFO:
		ret = kvm_riscv_vcpu_pmu_ctr_info(vcpu, cp->a0, retdata);
		break;
	case SBI_EXT_PMU_COUNTER_CFG_MATCH:
#if defined(CONFIG_32BIT)
		temp = ((uint64_t)cp->a5 << 32) | cp->a4;
#else
		temp = cp->a4;
#endif
		/*
		 * This can fail if perf core framework fails to create an event.
		 * Forward the error to userspace because it's an error which
		 * happened within the host kernel. The other option would be
		 * to convert to an SBI error and forward to the guest.
		 */
		ret = kvm_riscv_vcpu_pmu_ctr_cfg_match(vcpu, cp->a0, cp->a1,
						       cp->a2, cp->a3, temp, retdata);
		break;
	case SBI_EXT_PMU_COUNTER_START:
#if defined(CONFIG_32BIT)
		temp = ((uint64_t)cp->a4 << 32) | cp->a3;
#else
		temp = cp->a3;
#endif
		ret = kvm_riscv_vcpu_pmu_ctr_start(vcpu, cp->a0, cp->a1, cp->a2,
						   temp, retdata);
		break;
	case SBI_EXT_PMU_COUNTER_STOP:
		ret = kvm_riscv_vcpu_pmu_ctr_stop(vcpu, cp->a0, cp->a1, cp->a2, retdata);
		break;
	case SBI_EXT_PMU_COUNTER_FW_READ:
		ret = kvm_riscv_vcpu_pmu_ctr_read(vcpu, cp->a0, retdata);
		break;
	default:
		retdata->err_val = SBI_ERR_NOT_SUPPORTED;
	}

	return ret;
}

static unsigned long kvm_sbi_ext_pmu_probe(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);

	return kvpmu->init_done;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_pmu = {
	.extid_start = SBI_EXT_PMU,
	.extid_end = SBI_EXT_PMU,
	.handler = kvm_sbi_ext_pmu_handler,
	.probe = kvm_sbi_ext_pmu_probe,
};

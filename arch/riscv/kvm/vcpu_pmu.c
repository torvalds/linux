// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Rivos Inc
 *
 * Authors:
 *     Atish Patra <atishp@rivosinc.com>
 */

#define pr_fmt(fmt)	"riscv-kvm-pmu: " fmt
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <linux/perf/riscv_pmu.h>
#include <asm/csr.h>
#include <asm/kvm_vcpu_sbi.h>
#include <asm/kvm_vcpu_pmu.h>

#define kvm_pmu_num_counters(pmu) ((pmu)->num_hw_ctrs + (pmu)->num_fw_ctrs)

static int pmu_ctr_read(struct kvm_vcpu *vcpu, unsigned long cidx,
			unsigned long *out_val)
{
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;
	u64 enabled, running;

	pmc = &kvpmu->pmc[cidx];
	if (!pmc->perf_event)
		return -EINVAL;

	pmc->counter_val += perf_event_read_value(pmc->perf_event, &enabled, &running);
	*out_val = pmc->counter_val;

	return 0;
}

int kvm_riscv_vcpu_pmu_read_hpm(struct kvm_vcpu *vcpu, unsigned int csr_num,
				unsigned long *val, unsigned long new_val,
				unsigned long wr_mask)
{
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);
	int cidx, ret = KVM_INSN_CONTINUE_NEXT_SEPC;

	if (!kvpmu || !kvpmu->init_done) {
		/*
		 * In absence of sscofpmf in the platform, the guest OS may use
		 * the legacy PMU driver to read cycle/instret. In that case,
		 * just return 0 to avoid any illegal trap. However, any other
		 * hpmcounter access should result in illegal trap as they must
		 * be access through SBI PMU only.
		 */
		if (csr_num == CSR_CYCLE || csr_num == CSR_INSTRET) {
			*val = 0;
			return ret;
		} else {
			return KVM_INSN_ILLEGAL_TRAP;
		}
	}

	/* The counter CSR are read only. Thus, any write should result in illegal traps */
	if (wr_mask)
		return KVM_INSN_ILLEGAL_TRAP;

	cidx = csr_num - CSR_CYCLE;

	if (pmu_ctr_read(vcpu, cidx, val) < 0)
		return KVM_INSN_ILLEGAL_TRAP;

	return ret;
}

int kvm_riscv_vcpu_pmu_num_ctrs(struct kvm_vcpu *vcpu,
				struct kvm_vcpu_sbi_return *retdata)
{
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);

	retdata->out_val = kvm_pmu_num_counters(kvpmu);

	return 0;
}

int kvm_riscv_vcpu_pmu_ctr_info(struct kvm_vcpu *vcpu, unsigned long cidx,
				struct kvm_vcpu_sbi_return *retdata)
{
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);

	if (cidx > RISCV_KVM_MAX_COUNTERS || cidx == 1) {
		retdata->err_val = SBI_ERR_INVALID_PARAM;
		return 0;
	}

	retdata->out_val = kvpmu->pmc[cidx].cinfo.value;

	return 0;
}

int kvm_riscv_vcpu_pmu_ctr_start(struct kvm_vcpu *vcpu, unsigned long ctr_base,
				 unsigned long ctr_mask, unsigned long flags, u64 ival,
				 struct kvm_vcpu_sbi_return *retdata)
{
	/* TODO */
	return 0;
}

int kvm_riscv_vcpu_pmu_ctr_stop(struct kvm_vcpu *vcpu, unsigned long ctr_base,
				unsigned long ctr_mask, unsigned long flags,
				struct kvm_vcpu_sbi_return *retdata)
{
	/* TODO */
	return 0;
}

int kvm_riscv_vcpu_pmu_ctr_cfg_match(struct kvm_vcpu *vcpu, unsigned long ctr_base,
				     unsigned long ctr_mask, unsigned long flags,
				     unsigned long eidx, u64 evtdata,
				     struct kvm_vcpu_sbi_return *retdata)
{
	/* TODO */
	return 0;
}

int kvm_riscv_vcpu_pmu_ctr_read(struct kvm_vcpu *vcpu, unsigned long cidx,
				struct kvm_vcpu_sbi_return *retdata)
{
	int ret;

	ret = pmu_ctr_read(vcpu, cidx, &retdata->out_val);
	if (ret == -EINVAL)
		retdata->err_val = SBI_ERR_INVALID_PARAM;

	return 0;
}

void kvm_riscv_vcpu_pmu_init(struct kvm_vcpu *vcpu)
{
	int i = 0, ret, num_hw_ctrs = 0, hpm_width = 0;
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;

	/*
	 * PMU functionality should be only available to guests if privilege mode
	 * filtering is available in the host. Otherwise, guest will always count
	 * events while the execution is in hypervisor mode.
	 */
	if (!riscv_isa_extension_available(NULL, SSCOFPMF))
		return;

	ret = riscv_pmu_get_hpm_info(&hpm_width, &num_hw_ctrs);
	if (ret < 0 || !hpm_width || !num_hw_ctrs)
		return;

	/*
	 * Increase the number of hardware counters to offset the time counter.
	 */
	kvpmu->num_hw_ctrs = num_hw_ctrs + 1;
	kvpmu->num_fw_ctrs = SBI_PMU_FW_MAX;

	if (kvpmu->num_hw_ctrs > RISCV_KVM_MAX_HW_CTRS) {
		pr_warn_once("Limiting the hardware counters to 32 as specified by the ISA");
		kvpmu->num_hw_ctrs = RISCV_KVM_MAX_HW_CTRS;
	}

	/*
	 * There is no correlation between the logical hardware counter and virtual counters.
	 * However, we need to encode a hpmcounter CSR in the counter info field so that
	 * KVM can trap n emulate the read. This works well in the migration use case as
	 * KVM doesn't care if the actual hpmcounter is available in the hardware or not.
	 */
	for (i = 0; i < kvm_pmu_num_counters(kvpmu); i++) {
		/* TIME CSR shouldn't be read from perf interface */
		if (i == 1)
			continue;
		pmc = &kvpmu->pmc[i];
		pmc->idx = i;
		if (i < kvpmu->num_hw_ctrs) {
			pmc->cinfo.type = SBI_PMU_CTR_TYPE_HW;
			if (i < 3)
				/* CY, IR counters */
				pmc->cinfo.width = 63;
			else
				pmc->cinfo.width = hpm_width;
			/*
			 * The CSR number doesn't have any relation with the logical
			 * hardware counters. The CSR numbers are encoded sequentially
			 * to avoid maintaining a map between the virtual counter
			 * and CSR number.
			 */
			pmc->cinfo.csr = CSR_CYCLE + i;
		} else {
			pmc->cinfo.type = SBI_PMU_CTR_TYPE_FW;
			pmc->cinfo.width = BITS_PER_LONG - 1;
		}
	}

	kvpmu->init_done = true;
}

void kvm_riscv_vcpu_pmu_deinit(struct kvm_vcpu *vcpu)
{
	/* TODO */
}

void kvm_riscv_vcpu_pmu_reset(struct kvm_vcpu *vcpu)
{
	kvm_riscv_vcpu_pmu_deinit(vcpu);
}

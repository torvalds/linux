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
#include <linux/bitops.h>

#define kvm_pmu_num_counters(pmu) ((pmu)->num_hw_ctrs + (pmu)->num_fw_ctrs)
#define get_event_type(x) (((x) & SBI_PMU_EVENT_IDX_TYPE_MASK) >> 16)
#define get_event_code(x) ((x) & SBI_PMU_EVENT_IDX_CODE_MASK)

static enum perf_hw_id hw_event_perf_map[SBI_PMU_HW_GENERAL_MAX] = {
	[SBI_PMU_HW_CPU_CYCLES] = PERF_COUNT_HW_CPU_CYCLES,
	[SBI_PMU_HW_INSTRUCTIONS] = PERF_COUNT_HW_INSTRUCTIONS,
	[SBI_PMU_HW_CACHE_REFERENCES] = PERF_COUNT_HW_CACHE_REFERENCES,
	[SBI_PMU_HW_CACHE_MISSES] = PERF_COUNT_HW_CACHE_MISSES,
	[SBI_PMU_HW_BRANCH_INSTRUCTIONS] = PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
	[SBI_PMU_HW_BRANCH_MISSES] = PERF_COUNT_HW_BRANCH_MISSES,
	[SBI_PMU_HW_BUS_CYCLES] = PERF_COUNT_HW_BUS_CYCLES,
	[SBI_PMU_HW_STALLED_CYCLES_FRONTEND] = PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,
	[SBI_PMU_HW_STALLED_CYCLES_BACKEND] = PERF_COUNT_HW_STALLED_CYCLES_BACKEND,
	[SBI_PMU_HW_REF_CPU_CYCLES] = PERF_COUNT_HW_REF_CPU_CYCLES,
};

static u64 kvm_pmu_get_sample_period(struct kvm_pmc *pmc)
{
	u64 counter_val_mask = GENMASK(pmc->cinfo.width, 0);
	u64 sample_period;

	if (!pmc->counter_val)
		sample_period = counter_val_mask;
	else
		sample_period = (-pmc->counter_val) & counter_val_mask;

	return sample_period;
}

static u32 kvm_pmu_get_perf_event_type(unsigned long eidx)
{
	enum sbi_pmu_event_type etype = get_event_type(eidx);
	u32 type = PERF_TYPE_MAX;

	switch (etype) {
	case SBI_PMU_EVENT_TYPE_HW:
		type = PERF_TYPE_HARDWARE;
		break;
	case SBI_PMU_EVENT_TYPE_CACHE:
		type = PERF_TYPE_HW_CACHE;
		break;
	case SBI_PMU_EVENT_TYPE_RAW:
	case SBI_PMU_EVENT_TYPE_FW:
		type = PERF_TYPE_RAW;
		break;
	default:
		break;
	}

	return type;
}

static bool kvm_pmu_is_fw_event(unsigned long eidx)
{
	return get_event_type(eidx) == SBI_PMU_EVENT_TYPE_FW;
}

static void kvm_pmu_release_perf_event(struct kvm_pmc *pmc)
{
	if (pmc->perf_event) {
		perf_event_disable(pmc->perf_event);
		perf_event_release_kernel(pmc->perf_event);
		pmc->perf_event = NULL;
	}
}

static u64 kvm_pmu_get_perf_event_hw_config(u32 sbi_event_code)
{
	return hw_event_perf_map[sbi_event_code];
}

static u64 kvm_pmu_get_perf_event_cache_config(u32 sbi_event_code)
{
	u64 config = U64_MAX;
	unsigned int cache_type, cache_op, cache_result;

	/* All the cache event masks lie within 0xFF. No separate masking is necessary */
	cache_type = (sbi_event_code & SBI_PMU_EVENT_CACHE_ID_CODE_MASK) >>
		      SBI_PMU_EVENT_CACHE_ID_SHIFT;
	cache_op = (sbi_event_code & SBI_PMU_EVENT_CACHE_OP_ID_CODE_MASK) >>
		    SBI_PMU_EVENT_CACHE_OP_SHIFT;
	cache_result = sbi_event_code & SBI_PMU_EVENT_CACHE_RESULT_ID_CODE_MASK;

	if (cache_type >= PERF_COUNT_HW_CACHE_MAX ||
	    cache_op >= PERF_COUNT_HW_CACHE_OP_MAX ||
	    cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return config;

	config = cache_type | (cache_op << 8) | (cache_result << 16);

	return config;
}

static u64 kvm_pmu_get_perf_event_config(unsigned long eidx, uint64_t evt_data)
{
	enum sbi_pmu_event_type etype = get_event_type(eidx);
	u32 ecode = get_event_code(eidx);
	u64 config = U64_MAX;

	switch (etype) {
	case SBI_PMU_EVENT_TYPE_HW:
		if (ecode < SBI_PMU_HW_GENERAL_MAX)
			config = kvm_pmu_get_perf_event_hw_config(ecode);
		break;
	case SBI_PMU_EVENT_TYPE_CACHE:
		config = kvm_pmu_get_perf_event_cache_config(ecode);
		break;
	case SBI_PMU_EVENT_TYPE_RAW:
		config = evt_data & RISCV_PMU_RAW_EVENT_MASK;
		break;
	case SBI_PMU_EVENT_TYPE_FW:
		if (ecode < SBI_PMU_FW_MAX)
			config = (1ULL << 63) | ecode;
		break;
	default:
		break;
	}

	return config;
}

static int kvm_pmu_get_fixed_pmc_index(unsigned long eidx)
{
	u32 etype = kvm_pmu_get_perf_event_type(eidx);
	u32 ecode = get_event_code(eidx);

	if (etype != SBI_PMU_EVENT_TYPE_HW)
		return -EINVAL;

	if (ecode == SBI_PMU_HW_CPU_CYCLES)
		return 0;
	else if (ecode == SBI_PMU_HW_INSTRUCTIONS)
		return 2;
	else
		return -EINVAL;
}

static int kvm_pmu_get_programmable_pmc_index(struct kvm_pmu *kvpmu, unsigned long eidx,
					      unsigned long cbase, unsigned long cmask)
{
	int ctr_idx = -1;
	int i, pmc_idx;
	int min, max;

	if (kvm_pmu_is_fw_event(eidx)) {
		/* Firmware counters are mapped 1:1 starting from num_hw_ctrs for simplicity */
		min = kvpmu->num_hw_ctrs;
		max = min + kvpmu->num_fw_ctrs;
	} else {
		/* First 3 counters are reserved for fixed counters */
		min = 3;
		max = kvpmu->num_hw_ctrs;
	}

	for_each_set_bit(i, &cmask, BITS_PER_LONG) {
		pmc_idx = i + cbase;
		if ((pmc_idx >= min && pmc_idx < max) &&
		    !test_bit(pmc_idx, kvpmu->pmc_in_use)) {
			ctr_idx = pmc_idx;
			break;
		}
	}

	return ctr_idx;
}

static int pmu_get_pmc_index(struct kvm_pmu *pmu, unsigned long eidx,
			     unsigned long cbase, unsigned long cmask)
{
	int ret;

	/* Fixed counters need to be have fixed mapping as they have different width */
	ret = kvm_pmu_get_fixed_pmc_index(eidx);
	if (ret >= 0)
		return ret;

	return kvm_pmu_get_programmable_pmc_index(pmu, eidx, cbase, cmask);
}

static int pmu_ctr_read(struct kvm_vcpu *vcpu, unsigned long cidx,
			unsigned long *out_val)
{
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;
	u64 enabled, running;
	int fevent_code;

	pmc = &kvpmu->pmc[cidx];

	if (pmc->cinfo.type == SBI_PMU_CTR_TYPE_FW) {
		fevent_code = get_event_code(pmc->event_idx);
		pmc->counter_val = kvpmu->fw_event[fevent_code].value;
	} else if (pmc->perf_event) {
		pmc->counter_val += perf_event_read_value(pmc->perf_event, &enabled, &running);
	} else {
		return -EINVAL;
	}
	*out_val = pmc->counter_val;

	return 0;
}

static int kvm_pmu_validate_counter_mask(struct kvm_pmu *kvpmu, unsigned long ctr_base,
					 unsigned long ctr_mask)
{
	/* Make sure the we have a valid counter mask requested from the caller */
	if (!ctr_mask || (ctr_base + __fls(ctr_mask) >= kvm_pmu_num_counters(kvpmu)))
		return -EINVAL;

	return 0;
}

static int kvm_pmu_create_perf_event(struct kvm_pmc *pmc, struct perf_event_attr *attr,
				     unsigned long flags, unsigned long eidx, unsigned long evtdata)
{
	struct perf_event *event;

	kvm_pmu_release_perf_event(pmc);
	attr->config = kvm_pmu_get_perf_event_config(eidx, evtdata);
	if (flags & SBI_PMU_CFG_FLAG_CLEAR_VALUE) {
		//TODO: Do we really want to clear the value in hardware counter
		pmc->counter_val = 0;
	}

	/*
	 * Set the default sample_period for now. The guest specified value
	 * will be updated in the start call.
	 */
	attr->sample_period = kvm_pmu_get_sample_period(pmc);

	event = perf_event_create_kernel_counter(attr, -1, current, NULL, pmc);
	if (IS_ERR(event)) {
		pr_err("kvm pmu event creation failed for eidx %lx: %ld\n", eidx, PTR_ERR(event));
		return PTR_ERR(event);
	}

	pmc->perf_event = event;
	if (flags & SBI_PMU_CFG_FLAG_AUTO_START)
		perf_event_enable(pmc->perf_event);

	return 0;
}

int kvm_riscv_vcpu_pmu_incr_fw(struct kvm_vcpu *vcpu, unsigned long fid)
{
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);
	struct kvm_fw_event *fevent;

	if (!kvpmu || fid >= SBI_PMU_FW_MAX)
		return -EINVAL;

	fevent = &kvpmu->fw_event[fid];
	if (fevent->started)
		fevent->value++;

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
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);
	int i, pmc_index, sbiret = 0;
	struct kvm_pmc *pmc;
	int fevent_code;

	if (kvm_pmu_validate_counter_mask(kvpmu, ctr_base, ctr_mask) < 0) {
		sbiret = SBI_ERR_INVALID_PARAM;
		goto out;
	}

	/* Start the counters that have been configured and requested by the guest */
	for_each_set_bit(i, &ctr_mask, RISCV_MAX_COUNTERS) {
		pmc_index = i + ctr_base;
		if (!test_bit(pmc_index, kvpmu->pmc_in_use))
			continue;
		pmc = &kvpmu->pmc[pmc_index];
		if (flags & SBI_PMU_START_FLAG_SET_INIT_VALUE)
			pmc->counter_val = ival;
		if (pmc->cinfo.type == SBI_PMU_CTR_TYPE_FW) {
			fevent_code = get_event_code(pmc->event_idx);
			if (fevent_code >= SBI_PMU_FW_MAX) {
				sbiret = SBI_ERR_INVALID_PARAM;
				goto out;
			}

			/* Check if the counter was already started for some reason */
			if (kvpmu->fw_event[fevent_code].started) {
				sbiret = SBI_ERR_ALREADY_STARTED;
				continue;
			}

			kvpmu->fw_event[fevent_code].started = true;
			kvpmu->fw_event[fevent_code].value = pmc->counter_val;
		} else if (pmc->perf_event) {
			if (unlikely(pmc->started)) {
				sbiret = SBI_ERR_ALREADY_STARTED;
				continue;
			}
			perf_event_period(pmc->perf_event, kvm_pmu_get_sample_period(pmc));
			perf_event_enable(pmc->perf_event);
			pmc->started = true;
		} else {
			sbiret = SBI_ERR_INVALID_PARAM;
		}
	}

out:
	retdata->err_val = sbiret;

	return 0;
}

int kvm_riscv_vcpu_pmu_ctr_stop(struct kvm_vcpu *vcpu, unsigned long ctr_base,
				unsigned long ctr_mask, unsigned long flags,
				struct kvm_vcpu_sbi_return *retdata)
{
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);
	int i, pmc_index, sbiret = 0;
	struct kvm_pmc *pmc;
	int fevent_code;

	if (kvm_pmu_validate_counter_mask(kvpmu, ctr_base, ctr_mask) < 0) {
		sbiret = SBI_ERR_INVALID_PARAM;
		goto out;
	}

	/* Stop the counters that have been configured and requested by the guest */
	for_each_set_bit(i, &ctr_mask, RISCV_MAX_COUNTERS) {
		pmc_index = i + ctr_base;
		if (!test_bit(pmc_index, kvpmu->pmc_in_use))
			continue;
		pmc = &kvpmu->pmc[pmc_index];
		if (pmc->cinfo.type == SBI_PMU_CTR_TYPE_FW) {
			fevent_code = get_event_code(pmc->event_idx);
			if (fevent_code >= SBI_PMU_FW_MAX) {
				sbiret = SBI_ERR_INVALID_PARAM;
				goto out;
			}

			if (!kvpmu->fw_event[fevent_code].started)
				sbiret = SBI_ERR_ALREADY_STOPPED;

			kvpmu->fw_event[fevent_code].started = false;
		} else if (pmc->perf_event) {
			if (pmc->started) {
				/* Stop counting the counter */
				perf_event_disable(pmc->perf_event);
				pmc->started = false;
			} else {
				sbiret = SBI_ERR_ALREADY_STOPPED;
			}

			if (flags & SBI_PMU_STOP_FLAG_RESET)
				/* Release the counter if this is a reset request */
				kvm_pmu_release_perf_event(pmc);
		} else {
			sbiret = SBI_ERR_INVALID_PARAM;
		}
		if (flags & SBI_PMU_STOP_FLAG_RESET) {
			pmc->event_idx = SBI_PMU_EVENT_IDX_INVALID;
			clear_bit(pmc_index, kvpmu->pmc_in_use);
		}
	}

out:
	retdata->err_val = sbiret;

	return 0;
}

int kvm_riscv_vcpu_pmu_ctr_cfg_match(struct kvm_vcpu *vcpu, unsigned long ctr_base,
				     unsigned long ctr_mask, unsigned long flags,
				     unsigned long eidx, u64 evtdata,
				     struct kvm_vcpu_sbi_return *retdata)
{
	int ctr_idx, ret, sbiret = 0;
	bool is_fevent;
	unsigned long event_code;
	u32 etype = kvm_pmu_get_perf_event_type(eidx);
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc = NULL;
	struct perf_event_attr attr = {
		.type = etype,
		.size = sizeof(struct perf_event_attr),
		.pinned = true,
		/*
		 * It should never reach here if the platform doesn't support the sscofpmf
		 * extension as mode filtering won't work without it.
		 */
		.exclude_host = true,
		.exclude_hv = true,
		.exclude_user = !!(flags & SBI_PMU_CFG_FLAG_SET_UINH),
		.exclude_kernel = !!(flags & SBI_PMU_CFG_FLAG_SET_SINH),
		.config1 = RISCV_PMU_CONFIG1_GUEST_EVENTS,
	};

	if (kvm_pmu_validate_counter_mask(kvpmu, ctr_base, ctr_mask) < 0) {
		sbiret = SBI_ERR_INVALID_PARAM;
		goto out;
	}

	event_code = get_event_code(eidx);
	is_fevent = kvm_pmu_is_fw_event(eidx);
	if (is_fevent && event_code >= SBI_PMU_FW_MAX) {
		sbiret = SBI_ERR_NOT_SUPPORTED;
		goto out;
	}

	/*
	 * SKIP_MATCH flag indicates the caller is aware of the assigned counter
	 * for this event. Just do a sanity check if it already marked used.
	 */
	if (flags & SBI_PMU_CFG_FLAG_SKIP_MATCH) {
		if (!test_bit(ctr_base + __ffs(ctr_mask), kvpmu->pmc_in_use)) {
			sbiret = SBI_ERR_FAILURE;
			goto out;
		}
		ctr_idx = ctr_base + __ffs(ctr_mask);
	} else  {
		ctr_idx = pmu_get_pmc_index(kvpmu, eidx, ctr_base, ctr_mask);
		if (ctr_idx < 0) {
			sbiret = SBI_ERR_NOT_SUPPORTED;
			goto out;
		}
	}

	pmc = &kvpmu->pmc[ctr_idx];
	pmc->idx = ctr_idx;

	if (is_fevent) {
		if (flags & SBI_PMU_CFG_FLAG_AUTO_START)
			kvpmu->fw_event[event_code].started = true;
	} else {
		ret = kvm_pmu_create_perf_event(pmc, &attr, flags, eidx, evtdata);
		if (ret)
			return ret;
	}

	set_bit(ctr_idx, kvpmu->pmc_in_use);
	pmc->event_idx = eidx;
	retdata->out_val = ctr_idx;
out:
	retdata->err_val = sbiret;

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
	memset(&kvpmu->fw_event, 0, SBI_PMU_FW_MAX * sizeof(struct kvm_fw_event));

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
		pmc->event_idx = SBI_PMU_EVENT_IDX_INVALID;
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
	struct kvm_pmu *kvpmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;
	int i;

	if (!kvpmu)
		return;

	for_each_set_bit(i, kvpmu->pmc_in_use, RISCV_MAX_COUNTERS) {
		pmc = &kvpmu->pmc[i];
		pmc->counter_val = 0;
		kvm_pmu_release_perf_event(pmc);
		pmc->event_idx = SBI_PMU_EVENT_IDX_INVALID;
	}
	bitmap_zero(kvpmu->pmc_in_use, RISCV_MAX_COUNTERS);
	memset(&kvpmu->fw_event, 0, SBI_PMU_FW_MAX * sizeof(struct kvm_fw_event));
}

void kvm_riscv_vcpu_pmu_reset(struct kvm_vcpu *vcpu)
{
	kvm_riscv_vcpu_pmu_deinit(vcpu);
}

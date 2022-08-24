/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_PMU_H
#define __KVM_X86_PMU_H

#include <linux/nospec.h>

#define vcpu_to_pmu(vcpu) (&(vcpu)->arch.pmu)
#define pmu_to_vcpu(pmu)  (container_of((pmu), struct kvm_vcpu, arch.pmu))
#define pmc_to_pmu(pmc)   (&(pmc)->vcpu->arch.pmu)

#define MSR_IA32_MISC_ENABLE_PMU_RO_MASK (MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL |	\
					  MSR_IA32_MISC_ENABLE_BTS_UNAVAIL)

/* retrieve the 4 bits for EN and PMI out of IA32_FIXED_CTR_CTRL */
#define fixed_ctrl_field(ctrl_reg, idx) (((ctrl_reg) >> ((idx)*4)) & 0xf)

#define VMWARE_BACKDOOR_PMC_HOST_TSC		0x10000
#define VMWARE_BACKDOOR_PMC_REAL_TIME		0x10001
#define VMWARE_BACKDOOR_PMC_APPARENT_TIME	0x10002

struct kvm_event_hw_type_mapping {
	u8 eventsel;
	u8 unit_mask;
	unsigned event_type;
};

struct kvm_pmu_ops {
	bool (*hw_event_available)(struct kvm_pmc *pmc);
	bool (*pmc_is_enabled)(struct kvm_pmc *pmc);
	struct kvm_pmc *(*pmc_idx_to_pmc)(struct kvm_pmu *pmu, int pmc_idx);
	struct kvm_pmc *(*rdpmc_ecx_to_pmc)(struct kvm_vcpu *vcpu,
		unsigned int idx, u64 *mask);
	struct kvm_pmc *(*msr_idx_to_pmc)(struct kvm_vcpu *vcpu, u32 msr);
	bool (*is_valid_rdpmc_ecx)(struct kvm_vcpu *vcpu, unsigned int idx);
	bool (*is_valid_msr)(struct kvm_vcpu *vcpu, u32 msr);
	int (*get_msr)(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
	int (*set_msr)(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
	void (*refresh)(struct kvm_vcpu *vcpu);
	void (*init)(struct kvm_vcpu *vcpu);
	void (*reset)(struct kvm_vcpu *vcpu);
	void (*deliver_pmi)(struct kvm_vcpu *vcpu);
	void (*cleanup)(struct kvm_vcpu *vcpu);
};

void kvm_pmu_ops_update(const struct kvm_pmu_ops *pmu_ops);

static inline u64 pmc_bitmask(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	return pmu->counter_bitmask[pmc->type];
}

static inline u64 pmc_read_counter(struct kvm_pmc *pmc)
{
	u64 counter, enabled, running;

	counter = pmc->counter;
	if (pmc->perf_event && !pmc->is_paused)
		counter += perf_event_read_value(pmc->perf_event,
						 &enabled, &running);
	/* FIXME: Scaling needed? */
	return counter & pmc_bitmask(pmc);
}

static inline void pmc_release_perf_event(struct kvm_pmc *pmc)
{
	if (pmc->perf_event) {
		perf_event_release_kernel(pmc->perf_event);
		pmc->perf_event = NULL;
		pmc->current_config = 0;
		pmc_to_pmu(pmc)->event_count--;
	}
}

static inline void pmc_stop_counter(struct kvm_pmc *pmc)
{
	if (pmc->perf_event) {
		pmc->counter = pmc_read_counter(pmc);
		pmc_release_perf_event(pmc);
	}
}

static inline bool pmc_is_gp(struct kvm_pmc *pmc)
{
	return pmc->type == KVM_PMC_GP;
}

static inline bool pmc_is_fixed(struct kvm_pmc *pmc)
{
	return pmc->type == KVM_PMC_FIXED;
}

static inline bool kvm_valid_perf_global_ctrl(struct kvm_pmu *pmu,
						 u64 data)
{
	return !(pmu->global_ctrl_mask & data);
}

/* returns general purpose PMC with the specified MSR. Note that it can be
 * used for both PERFCTRn and EVNTSELn; that is why it accepts base as a
 * parameter to tell them apart.
 */
static inline struct kvm_pmc *get_gp_pmc(struct kvm_pmu *pmu, u32 msr,
					 u32 base)
{
	if (msr >= base && msr < base + pmu->nr_arch_gp_counters) {
		u32 index = array_index_nospec(msr - base,
					       pmu->nr_arch_gp_counters);

		return &pmu->gp_counters[index];
	}

	return NULL;
}

/* returns fixed PMC with the specified MSR */
static inline struct kvm_pmc *get_fixed_pmc(struct kvm_pmu *pmu, u32 msr)
{
	int base = MSR_CORE_PERF_FIXED_CTR0;

	if (msr >= base && msr < base + pmu->nr_arch_fixed_counters) {
		u32 index = array_index_nospec(msr - base,
					       pmu->nr_arch_fixed_counters);

		return &pmu->fixed_counters[index];
	}

	return NULL;
}

static inline u64 get_sample_period(struct kvm_pmc *pmc, u64 counter_value)
{
	u64 sample_period = (-counter_value) & pmc_bitmask(pmc);

	if (!sample_period)
		sample_period = pmc_bitmask(pmc) + 1;
	return sample_period;
}

static inline void pmc_update_sample_period(struct kvm_pmc *pmc)
{
	if (!pmc->perf_event || pmc->is_paused)
		return;

	perf_event_period(pmc->perf_event,
			  get_sample_period(pmc, pmc->counter));
}

static inline bool pmc_speculative_in_use(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	if (pmc_is_fixed(pmc))
		return fixed_ctrl_field(pmu->fixed_ctr_ctrl,
					pmc->idx - INTEL_PMC_IDX_FIXED) & 0x3;

	return pmc->eventsel & ARCH_PERFMON_EVENTSEL_ENABLE;
}

extern struct x86_pmu_capability kvm_pmu_cap;

static inline void kvm_init_pmu_capability(void)
{
	bool is_intel = boot_cpu_data.x86_vendor == X86_VENDOR_INTEL;

	perf_get_x86_pmu_capability(&kvm_pmu_cap);

	 /*
	  * For Intel, only support guest architectural pmu
	  * on a host with architectural pmu.
	  */
	if ((is_intel && !kvm_pmu_cap.version) || !kvm_pmu_cap.num_counters_gp)
		enable_pmu = false;

	if (!enable_pmu) {
		memset(&kvm_pmu_cap, 0, sizeof(kvm_pmu_cap));
		return;
	}

	kvm_pmu_cap.version = min(kvm_pmu_cap.version, 2);
	kvm_pmu_cap.num_counters_fixed = min(kvm_pmu_cap.num_counters_fixed,
					     KVM_PMC_MAX_FIXED);
}

void reprogram_counter(struct kvm_pmc *pmc);

void kvm_pmu_deliver_pmi(struct kvm_vcpu *vcpu);
void kvm_pmu_handle_event(struct kvm_vcpu *vcpu);
int kvm_pmu_rdpmc(struct kvm_vcpu *vcpu, unsigned pmc, u64 *data);
bool kvm_pmu_is_valid_rdpmc_ecx(struct kvm_vcpu *vcpu, unsigned int idx);
bool kvm_pmu_is_valid_msr(struct kvm_vcpu *vcpu, u32 msr);
int kvm_pmu_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
int kvm_pmu_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
void kvm_pmu_refresh(struct kvm_vcpu *vcpu);
void kvm_pmu_reset(struct kvm_vcpu *vcpu);
void kvm_pmu_init(struct kvm_vcpu *vcpu);
void kvm_pmu_cleanup(struct kvm_vcpu *vcpu);
void kvm_pmu_destroy(struct kvm_vcpu *vcpu);
int kvm_vm_ioctl_set_pmu_event_filter(struct kvm *kvm, void __user *argp);
void kvm_pmu_trigger_event(struct kvm_vcpu *vcpu, u64 perf_hw_id);

bool is_vmware_backdoor_pmc(u32 pmc_idx);

extern struct kvm_pmu_ops intel_pmu_ops;
extern struct kvm_pmu_ops amd_pmu_ops;
#endif /* __KVM_X86_PMU_H */

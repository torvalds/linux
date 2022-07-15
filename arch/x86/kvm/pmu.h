/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_PMU_H
#define __KVM_X86_PMU_H

#include <linux/nospec.h>

#define vcpu_to_pmu(vcpu) (&(vcpu)->arch.pmu)
#define pmu_to_vcpu(pmu)  (container_of((pmu), struct kvm_vcpu, arch.pmu))
#define pmc_to_pmu(pmc)   (&(pmc)->vcpu->arch.pmu)

/* retrieve the 4 bits for EN and PMI out of IA32_FIXED_CTR_CTRL */
#define fixed_ctrl_field(ctrl_reg, idx) (((ctrl_reg) >> ((idx)*4)) & 0xf)

#define VMWARE_BACKDOOR_PMC_HOST_TSC		0x10000
#define VMWARE_BACKDOOR_PMC_REAL_TIME		0x10001
#define VMWARE_BACKDOOR_PMC_APPARENT_TIME	0x10002

#define MAX_FIXED_COUNTERS	3

struct kvm_event_hw_type_mapping {
	u8 eventsel;
	u8 unit_mask;
	unsigned event_type;
};

struct kvm_pmu_ops {
	unsigned int (*pmc_perf_hw_id)(struct kvm_pmc *pmc);
	unsigned (*find_fixed_event)(int idx);
	bool (*pmc_is_enabled)(struct kvm_pmc *pmc);
	struct kvm_pmc *(*pmc_idx_to_pmc)(struct kvm_pmu *pmu, int pmc_idx);
	struct kvm_pmc *(*rdpmc_ecx_to_pmc)(struct kvm_vcpu *vcpu,
		unsigned int idx, u64 *mask);
	struct kvm_pmc *(*msr_idx_to_pmc)(struct kvm_vcpu *vcpu, u32 msr);
	int (*is_valid_rdpmc_ecx)(struct kvm_vcpu *vcpu, unsigned int idx);
	bool (*is_valid_msr)(struct kvm_vcpu *vcpu, u32 msr);
	int (*get_msr)(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
	int (*set_msr)(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
	void (*refresh)(struct kvm_vcpu *vcpu);
	void (*init)(struct kvm_vcpu *vcpu);
	void (*reset)(struct kvm_vcpu *vcpu);
};

static inline u64 pmc_bitmask(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	return pmu->counter_bitmask[pmc->type];
}

static inline u64 pmc_read_counter(struct kvm_pmc *pmc)
{
	u64 counter, enabled, running;

	counter = pmc->counter;
	if (pmc->perf_event)
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

static inline bool pmc_is_enabled(struct kvm_pmc *pmc)
{
	return kvm_x86_ops.pmu_ops->pmc_is_enabled(pmc);
}

static inline bool kvm_valid_perf_global_ctrl(struct kvm_pmu *pmu,
						 u64 data)
{
	return !(pmu->global_ctrl_mask & data);
}

/* returns general purpose PMC with the specified MSR. Note that it can be
 * used for both PERFCTRn and EVNTSELn; that is why it accepts base as a
 * paramenter to tell them apart.
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

void reprogram_gp_counter(struct kvm_pmc *pmc, u64 eventsel);
void reprogram_fixed_counter(struct kvm_pmc *pmc, u8 ctrl, int fixed_idx);
void reprogram_counter(struct kvm_pmu *pmu, int pmc_idx);

void kvm_pmu_deliver_pmi(struct kvm_vcpu *vcpu);
void kvm_pmu_handle_event(struct kvm_vcpu *vcpu);
int kvm_pmu_rdpmc(struct kvm_vcpu *vcpu, unsigned pmc, u64 *data);
int kvm_pmu_is_valid_rdpmc_ecx(struct kvm_vcpu *vcpu, unsigned int idx);
bool kvm_pmu_is_valid_msr(struct kvm_vcpu *vcpu, u32 msr);
int kvm_pmu_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
int kvm_pmu_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
void kvm_pmu_refresh(struct kvm_vcpu *vcpu);
void kvm_pmu_reset(struct kvm_vcpu *vcpu);
void kvm_pmu_init(struct kvm_vcpu *vcpu);
void kvm_pmu_cleanup(struct kvm_vcpu *vcpu);
void kvm_pmu_destroy(struct kvm_vcpu *vcpu);
int kvm_vm_ioctl_set_pmu_event_filter(struct kvm *kvm, void __user *argp);

bool is_vmware_backdoor_pmc(u32 pmc_idx);

extern struct kvm_pmu_ops intel_pmu_ops;
extern struct kvm_pmu_ops amd_pmu_ops;
#endif /* __KVM_X86_PMU_H */

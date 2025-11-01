/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_PMU_H
#define __KVM_X86_PMU_H

#include <linux/nospec.h>

#include <asm/kvm_host.h>

#define vcpu_to_pmu(vcpu) (&(vcpu)->arch.pmu)
#define pmu_to_vcpu(pmu)  (container_of((pmu), struct kvm_vcpu, arch.pmu))
#define pmc_to_pmu(pmc)   (&(pmc)->vcpu->arch.pmu)

#define MSR_IA32_MISC_ENABLE_PMU_RO_MASK (MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL |	\
					  MSR_IA32_MISC_ENABLE_BTS_UNAVAIL)

/* retrieve a fixed counter bits out of IA32_FIXED_CTR_CTRL */
#define fixed_ctrl_field(ctrl_reg, idx) \
	(((ctrl_reg) >> ((idx) * INTEL_FIXED_BITS_STRIDE)) & INTEL_FIXED_BITS_MASK)

#define VMWARE_BACKDOOR_PMC_HOST_TSC		0x10000
#define VMWARE_BACKDOOR_PMC_REAL_TIME		0x10001
#define VMWARE_BACKDOOR_PMC_APPARENT_TIME	0x10002

#define KVM_FIXED_PMC_BASE_IDX INTEL_PMC_IDX_FIXED

struct kvm_pmu_ops {
	struct kvm_pmc *(*rdpmc_ecx_to_pmc)(struct kvm_vcpu *vcpu,
		unsigned int idx, u64 *mask);
	struct kvm_pmc *(*msr_idx_to_pmc)(struct kvm_vcpu *vcpu, u32 msr);
	int (*check_rdpmc_early)(struct kvm_vcpu *vcpu, unsigned int idx);
	bool (*is_valid_msr)(struct kvm_vcpu *vcpu, u32 msr);
	int (*get_msr)(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
	int (*set_msr)(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
	void (*refresh)(struct kvm_vcpu *vcpu);
	void (*init)(struct kvm_vcpu *vcpu);
	void (*reset)(struct kvm_vcpu *vcpu);
	void (*deliver_pmi)(struct kvm_vcpu *vcpu);
	void (*cleanup)(struct kvm_vcpu *vcpu);

	const u64 EVENTSEL_EVENT;
	const int MAX_NR_GP_COUNTERS;
	const int MIN_NR_GP_COUNTERS;
};

void kvm_pmu_ops_update(const struct kvm_pmu_ops *pmu_ops);

static inline bool kvm_pmu_has_perf_global_ctrl(struct kvm_pmu *pmu)
{
	/*
	 * Architecturally, Intel's SDM states that IA32_PERF_GLOBAL_CTRL is
	 * supported if "CPUID.0AH: EAX[7:0] > 0", i.e. if the PMU version is
	 * greater than zero.  However, KVM only exposes and emulates the MSR
	 * to/for the guest if the guest PMU supports at least "Architectural
	 * Performance Monitoring Version 2".
	 *
	 * AMD's version of PERF_GLOBAL_CTRL conveniently shows up with v2.
	 */
	return pmu->version > 1;
}

/*
 * KVM tracks all counters in 64-bit bitmaps, with general purpose counters
 * mapped to bits 31:0 and fixed counters mapped to 63:32, e.g. fixed counter 0
 * is tracked internally via index 32.  On Intel, (AMD doesn't support fixed
 * counters), this mirrors how fixed counters are mapped to PERF_GLOBAL_CTRL
 * and similar MSRs, i.e. tracking fixed counters at base index 32 reduces the
 * amounter of boilerplate needed to iterate over PMCs *and* simplifies common
 * enabling/disable/reset operations.
 *
 * WARNING!  This helper is only for lookups that are initiated by KVM, it is
 * NOT safe for guest lookups, e.g. will do the wrong thing if passed a raw
 * ECX value from RDPMC (fixed counters are accessed by setting bit 30 in ECX
 * for RDPMC, not by adding 32 to the fixed counter index).
 */
static inline struct kvm_pmc *kvm_pmc_idx_to_pmc(struct kvm_pmu *pmu, int idx)
{
	if (idx < pmu->nr_arch_gp_counters)
		return &pmu->gp_counters[idx];

	idx -= KVM_FIXED_PMC_BASE_IDX;
	if (idx >= 0 && idx < pmu->nr_arch_fixed_counters)
		return &pmu->fixed_counters[idx];

	return NULL;
}

#define kvm_for_each_pmc(pmu, pmc, i, bitmap)			\
	for_each_set_bit(i, bitmap, X86_PMC_IDX_MAX)		\
		if (!(pmc = kvm_pmc_idx_to_pmc(pmu, i)))	\
			continue;				\
		else						\

static inline u64 pmc_bitmask(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	return pmu->counter_bitmask[pmc->type];
}

static inline u64 pmc_read_counter(struct kvm_pmc *pmc)
{
	u64 counter, enabled, running;

	counter = pmc->counter + pmc->emulated_counter;

	if (pmc->perf_event && !pmc->is_paused)
		counter += perf_event_read_value(pmc->perf_event,
						 &enabled, &running);
	/* FIXME: Scaling needed? */
	return counter & pmc_bitmask(pmc);
}

void pmc_write_counter(struct kvm_pmc *pmc, u64 val);

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
	return !(pmu->global_ctrl_rsvd & data);
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

static inline bool pmc_is_locally_enabled(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	if (pmc_is_fixed(pmc))
		return fixed_ctrl_field(pmu->fixed_ctr_ctrl,
					pmc->idx - KVM_FIXED_PMC_BASE_IDX) &
					(INTEL_FIXED_0_KERNEL | INTEL_FIXED_0_USER);

	return pmc->eventsel & ARCH_PERFMON_EVENTSEL_ENABLE;
}

extern struct x86_pmu_capability kvm_pmu_cap;

void kvm_init_pmu_capability(const struct kvm_pmu_ops *pmu_ops);

void kvm_pmu_recalc_pmc_emulation(struct kvm_pmu *pmu, struct kvm_pmc *pmc);

static inline void kvm_pmu_request_counter_reprogram(struct kvm_pmc *pmc)
{
	kvm_pmu_recalc_pmc_emulation(pmc_to_pmu(pmc), pmc);

	set_bit(pmc->idx, pmc_to_pmu(pmc)->reprogram_pmi);
	kvm_make_request(KVM_REQ_PMU, pmc->vcpu);
}

static inline void reprogram_counters(struct kvm_pmu *pmu, u64 diff)
{
	int bit;

	if (!diff)
		return;

	for_each_set_bit(bit, (unsigned long *)&diff, X86_PMC_IDX_MAX)
		set_bit(bit, pmu->reprogram_pmi);
	kvm_make_request(KVM_REQ_PMU, pmu_to_vcpu(pmu));
}

/*
 * Check if a PMC is enabled by comparing it against global_ctrl bits.
 *
 * If the vPMU doesn't have global_ctrl MSR, all vPMCs are enabled.
 */
static inline bool pmc_is_globally_enabled(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	if (!kvm_pmu_has_perf_global_ctrl(pmu))
		return true;

	return test_bit(pmc->idx, (unsigned long *)&pmu->global_ctrl);
}

void kvm_pmu_deliver_pmi(struct kvm_vcpu *vcpu);
void kvm_pmu_handle_event(struct kvm_vcpu *vcpu);
int kvm_pmu_rdpmc(struct kvm_vcpu *vcpu, unsigned pmc, u64 *data);
int kvm_pmu_check_rdpmc_early(struct kvm_vcpu *vcpu, unsigned int idx);
bool kvm_pmu_is_valid_msr(struct kvm_vcpu *vcpu, u32 msr);
int kvm_pmu_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
int kvm_pmu_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
void kvm_pmu_refresh(struct kvm_vcpu *vcpu);
void kvm_pmu_init(struct kvm_vcpu *vcpu);
void kvm_pmu_cleanup(struct kvm_vcpu *vcpu);
void kvm_pmu_destroy(struct kvm_vcpu *vcpu);
int kvm_vm_ioctl_set_pmu_event_filter(struct kvm *kvm, void __user *argp);
void kvm_pmu_instruction_retired(struct kvm_vcpu *vcpu);
void kvm_pmu_branch_retired(struct kvm_vcpu *vcpu);

bool is_vmware_backdoor_pmc(u32 pmc_idx);

extern struct kvm_pmu_ops intel_pmu_ops;
extern struct kvm_pmu_ops amd_pmu_ops;
#endif /* __KVM_X86_PMU_H */

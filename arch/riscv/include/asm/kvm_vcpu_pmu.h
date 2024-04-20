/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Rivos Inc
 *
 * Authors:
 *     Atish Patra <atishp@rivosinc.com>
 */

#ifndef __KVM_VCPU_RISCV_PMU_H
#define __KVM_VCPU_RISCV_PMU_H

#include <linux/perf/riscv_pmu.h>
#include <asm/sbi.h>

#ifdef CONFIG_RISCV_PMU_SBI
#define RISCV_KVM_MAX_FW_CTRS	32
#define RISCV_KVM_MAX_HW_CTRS	32
#define RISCV_KVM_MAX_COUNTERS	(RISCV_KVM_MAX_HW_CTRS + RISCV_KVM_MAX_FW_CTRS)
static_assert(RISCV_KVM_MAX_COUNTERS <= 64);

struct kvm_fw_event {
	/* Current value of the event */
	unsigned long value;

	/* Event monitoring status */
	bool started;
};

/* Per virtual pmu counter data */
struct kvm_pmc {
	u8 idx;
	struct perf_event *perf_event;
	u64 counter_val;
	union sbi_pmu_ctr_info cinfo;
	/* Event monitoring status */
	bool started;
	/* Monitoring event ID */
	unsigned long event_idx;
};

/* PMU data structure per vcpu */
struct kvm_pmu {
	struct kvm_pmc pmc[RISCV_KVM_MAX_COUNTERS];
	struct kvm_fw_event fw_event[RISCV_KVM_MAX_FW_CTRS];
	/* Number of the virtual firmware counters available */
	int num_fw_ctrs;
	/* Number of the virtual hardware counters available */
	int num_hw_ctrs;
	/* A flag to indicate that pmu initialization is done */
	bool init_done;
	/* Bit map of all the virtual counter used */
	DECLARE_BITMAP(pmc_in_use, RISCV_KVM_MAX_COUNTERS);
	/* The address of the counter snapshot area (guest physical address) */
	gpa_t snapshot_addr;
	/* The actual data of the snapshot */
	struct riscv_pmu_snapshot_data *sdata;
};

#define vcpu_to_pmu(vcpu) (&(vcpu)->arch.pmu_context)
#define pmu_to_vcpu(pmu)  (container_of((pmu), struct kvm_vcpu, arch.pmu_context))

#if defined(CONFIG_32BIT)
#define KVM_RISCV_VCPU_HPMCOUNTER_CSR_FUNCS \
{.base = CSR_CYCLEH,	.count = 31,	.func = kvm_riscv_vcpu_pmu_read_hpm }, \
{.base = CSR_CYCLE,	.count = 31,	.func = kvm_riscv_vcpu_pmu_read_hpm },
#else
#define KVM_RISCV_VCPU_HPMCOUNTER_CSR_FUNCS \
{.base = CSR_CYCLE,	.count = 31,	.func = kvm_riscv_vcpu_pmu_read_hpm },
#endif

int kvm_riscv_vcpu_pmu_incr_fw(struct kvm_vcpu *vcpu, unsigned long fid);
int kvm_riscv_vcpu_pmu_read_hpm(struct kvm_vcpu *vcpu, unsigned int csr_num,
				unsigned long *val, unsigned long new_val,
				unsigned long wr_mask);

int kvm_riscv_vcpu_pmu_num_ctrs(struct kvm_vcpu *vcpu, struct kvm_vcpu_sbi_return *retdata);
int kvm_riscv_vcpu_pmu_ctr_info(struct kvm_vcpu *vcpu, unsigned long cidx,
				struct kvm_vcpu_sbi_return *retdata);
int kvm_riscv_vcpu_pmu_ctr_start(struct kvm_vcpu *vcpu, unsigned long ctr_base,
				 unsigned long ctr_mask, unsigned long flags, u64 ival,
				 struct kvm_vcpu_sbi_return *retdata);
int kvm_riscv_vcpu_pmu_ctr_stop(struct kvm_vcpu *vcpu, unsigned long ctr_base,
				unsigned long ctr_mask, unsigned long flags,
				struct kvm_vcpu_sbi_return *retdata);
int kvm_riscv_vcpu_pmu_ctr_cfg_match(struct kvm_vcpu *vcpu, unsigned long ctr_base,
				     unsigned long ctr_mask, unsigned long flags,
				     unsigned long eidx, u64 evtdata,
				     struct kvm_vcpu_sbi_return *retdata);
int kvm_riscv_vcpu_pmu_ctr_read(struct kvm_vcpu *vcpu, unsigned long cidx,
				struct kvm_vcpu_sbi_return *retdata);
void kvm_riscv_vcpu_pmu_init(struct kvm_vcpu *vcpu);
int kvm_riscv_vcpu_pmu_snapshot_set_shmem(struct kvm_vcpu *vcpu, unsigned long saddr_low,
				      unsigned long saddr_high, unsigned long flags,
				      struct kvm_vcpu_sbi_return *retdata);
void kvm_riscv_vcpu_pmu_deinit(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_pmu_reset(struct kvm_vcpu *vcpu);

#else
struct kvm_pmu {
};

#define KVM_RISCV_VCPU_HPMCOUNTER_CSR_FUNCS \
{.base = 0,	.count = 0,	.func = NULL },

static inline void kvm_riscv_vcpu_pmu_init(struct kvm_vcpu *vcpu) {}
static inline int kvm_riscv_vcpu_pmu_incr_fw(struct kvm_vcpu *vcpu, unsigned long fid)
{
	return 0;
}

static inline void kvm_riscv_vcpu_pmu_deinit(struct kvm_vcpu *vcpu) {}
static inline void kvm_riscv_vcpu_pmu_reset(struct kvm_vcpu *vcpu) {}
#endif /* CONFIG_RISCV_PMU_SBI */
#endif /* !__KVM_VCPU_RISCV_PMU_H */

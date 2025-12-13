/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Atish Patra <atish.patra@wdc.com>
 */

#ifndef __RISCV_KVM_VCPU_SBI_H__
#define __RISCV_KVM_VCPU_SBI_H__

#define KVM_SBI_IMPID 3

#define KVM_SBI_VERSION_MAJOR 3
#define KVM_SBI_VERSION_MINOR 0

enum kvm_riscv_sbi_ext_status {
	KVM_RISCV_SBI_EXT_STATUS_UNINITIALIZED,
	KVM_RISCV_SBI_EXT_STATUS_UNAVAILABLE,
	KVM_RISCV_SBI_EXT_STATUS_ENABLED,
	KVM_RISCV_SBI_EXT_STATUS_DISABLED,
};

struct kvm_vcpu_sbi_context {
	int return_handled;
	enum kvm_riscv_sbi_ext_status ext_status[KVM_RISCV_SBI_EXT_MAX];
};

struct kvm_vcpu_sbi_return {
	unsigned long out_val;
	unsigned long err_val;
	struct kvm_cpu_trap *utrap;
	bool uexit;
};

struct kvm_vcpu_sbi_extension {
	unsigned long extid_start;
	unsigned long extid_end;

	bool default_disabled;

	/**
	 * SBI extension handler. It can be defined for a given extension or group of
	 * extension. But it should always return linux error codes rather than SBI
	 * specific error codes.
	 */
	int (*handler)(struct kvm_vcpu *vcpu, struct kvm_run *run,
		       struct kvm_vcpu_sbi_return *retdata);

	/* Extension specific probe function */
	unsigned long (*probe)(struct kvm_vcpu *vcpu);

	/*
	 * Init/deinit function called once during VCPU init/destroy. These
	 * might be use if the SBI extensions need to allocate or do specific
	 * init time only configuration.
	 */
	int (*init)(struct kvm_vcpu *vcpu);
	void (*deinit)(struct kvm_vcpu *vcpu);

	void (*reset)(struct kvm_vcpu *vcpu);

	unsigned long state_reg_subtype;
	unsigned long (*get_state_reg_count)(struct kvm_vcpu *vcpu);
	int (*get_state_reg_id)(struct kvm_vcpu *vcpu, int index, u64 *reg_id);
	int (*get_state_reg)(struct kvm_vcpu *vcpu, unsigned long reg_num,
			     unsigned long reg_size, void *reg_val);
	int (*set_state_reg)(struct kvm_vcpu *vcpu, unsigned long reg_num,
			     unsigned long reg_size, const void *reg_val);
};

void kvm_riscv_vcpu_sbi_forward(struct kvm_vcpu *vcpu, struct kvm_run *run);
void kvm_riscv_vcpu_sbi_system_reset(struct kvm_vcpu *vcpu,
				     struct kvm_run *run,
				     u32 type, u64 flags);
void kvm_riscv_vcpu_sbi_request_reset(struct kvm_vcpu *vcpu,
				      unsigned long pc, unsigned long a1);
void kvm_riscv_vcpu_sbi_load_reset_state(struct kvm_vcpu *vcpu);
int kvm_riscv_vcpu_sbi_return(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_riscv_vcpu_reg_indices_sbi_ext(struct kvm_vcpu *vcpu, u64 __user *uindices);
int kvm_riscv_vcpu_set_reg_sbi_ext(struct kvm_vcpu *vcpu,
				   const struct kvm_one_reg *reg);
int kvm_riscv_vcpu_get_reg_sbi_ext(struct kvm_vcpu *vcpu,
				   const struct kvm_one_reg *reg);
int kvm_riscv_vcpu_reg_indices_sbi(struct kvm_vcpu *vcpu, u64 __user *uindices);
int kvm_riscv_vcpu_set_reg_sbi(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
int kvm_riscv_vcpu_get_reg_sbi(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
const struct kvm_vcpu_sbi_extension *kvm_vcpu_sbi_find_ext(
				struct kvm_vcpu *vcpu, unsigned long extid);
int kvm_riscv_vcpu_sbi_ecall(struct kvm_vcpu *vcpu, struct kvm_run *run);
void kvm_riscv_vcpu_sbi_init(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_sbi_deinit(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_sbi_reset(struct kvm_vcpu *vcpu);

#ifdef CONFIG_RISCV_SBI_V01
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_v01;
#endif
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_base;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_time;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_ipi;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_rfence;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_srst;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_hsm;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_dbcn;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_susp;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_sta;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_fwft;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_experimental;
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_vendor;

#ifdef CONFIG_RISCV_PMU_SBI
extern const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_pmu;
#endif
#endif /* __RISCV_KVM_VCPU_SBI_H__ */

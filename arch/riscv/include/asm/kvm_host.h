/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#ifndef __RISCV_KVM_HOST_H__
#define __RISCV_KVM_HOST_H__

#include <linux/types.h>
#include <linux/kvm.h>
#include <linux/kvm_types.h>

#ifdef CONFIG_64BIT
#define KVM_MAX_VCPUS			(1U << 16)
#else
#define KVM_MAX_VCPUS			(1U << 9)
#endif

#define KVM_HALT_POLL_NS_DEFAULT	500000

#define KVM_VCPU_MAX_FEATURES		0

#define KVM_REQ_SLEEP \
	KVM_ARCH_REQ_FLAGS(0, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_VCPU_RESET		KVM_ARCH_REQ(1)

struct kvm_vm_stat {
	struct kvm_vm_stat_generic generic;
};

struct kvm_vcpu_stat {
	struct kvm_vcpu_stat_generic generic;
	u64 ecall_exit_stat;
	u64 wfi_exit_stat;
	u64 mmio_exit_user;
	u64 mmio_exit_kernel;
	u64 exits;
};

struct kvm_arch_memory_slot {
};

struct kvm_arch {
	/* stage2 page table */
	pgd_t *pgd;
	phys_addr_t pgd_phys;
};

struct kvm_cpu_trap {
	unsigned long sepc;
	unsigned long scause;
	unsigned long stval;
	unsigned long htval;
	unsigned long htinst;
};

struct kvm_vcpu_arch {
	/* Don't run the VCPU (blocked) */
	bool pause;

	/* SRCU lock index for in-kernel run loop */
	int srcu_idx;
};

static inline void kvm_arch_hardware_unsetup(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}
static inline void kvm_arch_vcpu_block_finish(struct kvm_vcpu *vcpu) {}

void kvm_riscv_stage2_flush_cache(struct kvm_vcpu *vcpu);
int kvm_riscv_stage2_alloc_pgd(struct kvm *kvm);
void kvm_riscv_stage2_free_pgd(struct kvm *kvm);
void kvm_riscv_stage2_update_hgatp(struct kvm_vcpu *vcpu);

int kvm_riscv_vcpu_mmio_return(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_riscv_vcpu_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
			struct kvm_cpu_trap *trap);

static inline void __kvm_riscv_switch_to(struct kvm_vcpu_arch *vcpu_arch) {}

#endif /* __RISCV_KVM_HOST_H__ */

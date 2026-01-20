/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 */

#ifndef __KVM_VCPU_RISCV_CONFIG_H
#define __KVM_VCPU_RISCV_CONFIG_H

#include <linux/types.h>

struct kvm_vcpu;

struct kvm_vcpu_config {
	u64 henvcfg;
	u64 hstateen0;
	unsigned long hedeleg;
	unsigned long hideleg;
};

void kvm_riscv_vcpu_config_init(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_config_guest_debug(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_config_ran_once(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_config_load(struct kvm_vcpu *vcpu);

#endif

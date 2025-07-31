/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Ventana Micro Systems Inc.
 */

#ifndef __RISCV_KVM_VMID_H_
#define __RISCV_KVM_VMID_H_

#include <linux/kvm_types.h>

struct kvm_vmid {
	/*
	 * Writes to vmid_version and vmid happen with vmid_lock held
	 * whereas reads happen without any lock held.
	 */
	unsigned long vmid_version;
	unsigned long vmid;
};

void __init kvm_riscv_gstage_vmid_detect(void);
unsigned long kvm_riscv_gstage_vmid_bits(void);
int kvm_riscv_gstage_vmid_init(struct kvm *kvm);
bool kvm_riscv_gstage_vmid_ver_changed(struct kvm_vmid *vmid);
void kvm_riscv_gstage_vmid_update(struct kvm_vcpu *vcpu);
void kvm_riscv_gstage_vmid_sanitize(struct kvm_vcpu *vcpu);

#endif

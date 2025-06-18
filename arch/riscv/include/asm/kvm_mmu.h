/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Ventana Micro Systems Inc.
 */

#ifndef __RISCV_KVM_MMU_H_
#define __RISCV_KVM_MMU_H_

#include <linux/kvm_types.h>

struct kvm_gstage_mapping {
	gpa_t addr;
	pte_t pte;
	u32 level;
};

int kvm_riscv_gstage_ioremap(struct kvm *kvm, gpa_t gpa,
			     phys_addr_t hpa, unsigned long size,
			     bool writable, bool in_atomic);
void kvm_riscv_gstage_iounmap(struct kvm *kvm, gpa_t gpa,
			      unsigned long size);
int kvm_riscv_gstage_map(struct kvm_vcpu *vcpu,
			 struct kvm_memory_slot *memslot,
			 gpa_t gpa, unsigned long hva, bool is_write,
			 struct kvm_gstage_mapping *out_map);
int kvm_riscv_gstage_alloc_pgd(struct kvm *kvm);
void kvm_riscv_gstage_free_pgd(struct kvm *kvm);
void kvm_riscv_gstage_update_hgatp(struct kvm_vcpu *vcpu);
void kvm_riscv_gstage_mode_detect(void);
unsigned long kvm_riscv_gstage_mode(void);
int kvm_riscv_gstage_gpa_bits(void);

#endif

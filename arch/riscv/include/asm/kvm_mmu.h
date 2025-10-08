/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Ventana Micro Systems Inc.
 */

#ifndef __RISCV_KVM_MMU_H_
#define __RISCV_KVM_MMU_H_

#include <asm/kvm_gstage.h>

int kvm_riscv_mmu_ioremap(struct kvm *kvm, gpa_t gpa, phys_addr_t hpa,
			  unsigned long size, bool writable, bool in_atomic);
void kvm_riscv_mmu_iounmap(struct kvm *kvm, gpa_t gpa, unsigned long size);
int kvm_riscv_mmu_map(struct kvm_vcpu *vcpu, struct kvm_memory_slot *memslot,
		      gpa_t gpa, unsigned long hva, bool is_write,
		      struct kvm_gstage_mapping *out_map);
int kvm_riscv_mmu_alloc_pgd(struct kvm *kvm);
void kvm_riscv_mmu_free_pgd(struct kvm *kvm);
void kvm_riscv_mmu_update_hgatp(struct kvm_vcpu *vcpu);

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Ventana Micro Systems Inc.
 */

#ifndef __RISCV_KVM_TLB_H_
#define __RISCV_KVM_TLB_H_

#include <linux/kvm_types.h>

enum kvm_riscv_hfence_type {
	KVM_RISCV_HFENCE_UNKNOWN = 0,
	KVM_RISCV_HFENCE_GVMA_VMID_GPA,
	KVM_RISCV_HFENCE_GVMA_VMID_ALL,
	KVM_RISCV_HFENCE_VVMA_ASID_GVA,
	KVM_RISCV_HFENCE_VVMA_ASID_ALL,
	KVM_RISCV_HFENCE_VVMA_GVA,
	KVM_RISCV_HFENCE_VVMA_ALL
};

struct kvm_riscv_hfence {
	enum kvm_riscv_hfence_type type;
	unsigned long asid;
	unsigned long vmid;
	unsigned long order;
	gpa_t addr;
	gpa_t size;
};

#define KVM_RISCV_VCPU_MAX_HFENCE	64

#define KVM_RISCV_GSTAGE_TLB_MIN_ORDER		12

void kvm_riscv_local_hfence_gvma_vmid_gpa(unsigned long vmid,
					  gpa_t gpa, gpa_t gpsz,
					  unsigned long order);
void kvm_riscv_local_hfence_gvma_vmid_all(unsigned long vmid);
void kvm_riscv_local_hfence_gvma_gpa(gpa_t gpa, gpa_t gpsz,
				     unsigned long order);
void kvm_riscv_local_hfence_gvma_all(void);
void kvm_riscv_local_hfence_vvma_asid_gva(unsigned long vmid,
					  unsigned long asid,
					  unsigned long gva,
					  unsigned long gvsz,
					  unsigned long order);
void kvm_riscv_local_hfence_vvma_asid_all(unsigned long vmid,
					  unsigned long asid);
void kvm_riscv_local_hfence_vvma_gva(unsigned long vmid,
				     unsigned long gva, unsigned long gvsz,
				     unsigned long order);
void kvm_riscv_local_hfence_vvma_all(unsigned long vmid);

void kvm_riscv_tlb_flush_process(struct kvm_vcpu *vcpu);

void kvm_riscv_fence_i_process(struct kvm_vcpu *vcpu);
void kvm_riscv_hfence_vvma_all_process(struct kvm_vcpu *vcpu);
void kvm_riscv_hfence_process(struct kvm_vcpu *vcpu);

void kvm_riscv_fence_i(struct kvm *kvm,
		       unsigned long hbase, unsigned long hmask);
void kvm_riscv_hfence_gvma_vmid_gpa(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask,
				    gpa_t gpa, gpa_t gpsz,
				    unsigned long order, unsigned long vmid);
void kvm_riscv_hfence_gvma_vmid_all(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask,
				    unsigned long vmid);
void kvm_riscv_hfence_vvma_asid_gva(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask,
				    unsigned long gva, unsigned long gvsz,
				    unsigned long order, unsigned long asid,
				    unsigned long vmid);
void kvm_riscv_hfence_vvma_asid_all(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask,
				    unsigned long asid, unsigned long vmid);
void kvm_riscv_hfence_vvma_gva(struct kvm *kvm,
			       unsigned long hbase, unsigned long hmask,
			       unsigned long gva, unsigned long gvsz,
			       unsigned long order, unsigned long vmid);
void kvm_riscv_hfence_vvma_all(struct kvm *kvm,
			       unsigned long hbase, unsigned long hmask,
			       unsigned long vmid);

#endif

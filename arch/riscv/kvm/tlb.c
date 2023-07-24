// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#include <linux/bitmap.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/kvm_host.h>
#include <asm/cacheflush.h>
#include <asm/csr.h>
#include <asm/hwcap.h>
#include <asm/insn-def.h>

#define has_svinval()	riscv_has_extension_unlikely(RISCV_ISA_EXT_SVINVAL)

void kvm_riscv_local_hfence_gvma_vmid_gpa(unsigned long vmid,
					  gpa_t gpa, gpa_t gpsz,
					  unsigned long order)
{
	gpa_t pos;

	if (PTRS_PER_PTE < (gpsz >> order)) {
		kvm_riscv_local_hfence_gvma_vmid_all(vmid);
		return;
	}

	if (has_svinval()) {
		asm volatile (SFENCE_W_INVAL() ::: "memory");
		for (pos = gpa; pos < (gpa + gpsz); pos += BIT(order))
			asm volatile (HINVAL_GVMA(%0, %1)
			: : "r" (pos >> 2), "r" (vmid) : "memory");
		asm volatile (SFENCE_INVAL_IR() ::: "memory");
	} else {
		for (pos = gpa; pos < (gpa + gpsz); pos += BIT(order))
			asm volatile (HFENCE_GVMA(%0, %1)
			: : "r" (pos >> 2), "r" (vmid) : "memory");
	}
}

void kvm_riscv_local_hfence_gvma_vmid_all(unsigned long vmid)
{
	asm volatile(HFENCE_GVMA(zero, %0) : : "r" (vmid) : "memory");
}

void kvm_riscv_local_hfence_gvma_gpa(gpa_t gpa, gpa_t gpsz,
				     unsigned long order)
{
	gpa_t pos;

	if (PTRS_PER_PTE < (gpsz >> order)) {
		kvm_riscv_local_hfence_gvma_all();
		return;
	}

	if (has_svinval()) {
		asm volatile (SFENCE_W_INVAL() ::: "memory");
		for (pos = gpa; pos < (gpa + gpsz); pos += BIT(order))
			asm volatile(HINVAL_GVMA(%0, zero)
			: : "r" (pos >> 2) : "memory");
		asm volatile (SFENCE_INVAL_IR() ::: "memory");
	} else {
		for (pos = gpa; pos < (gpa + gpsz); pos += BIT(order))
			asm volatile(HFENCE_GVMA(%0, zero)
			: : "r" (pos >> 2) : "memory");
	}
}

void kvm_riscv_local_hfence_gvma_all(void)
{
	asm volatile(HFENCE_GVMA(zero, zero) : : : "memory");
}

void kvm_riscv_local_hfence_vvma_asid_gva(unsigned long vmid,
					  unsigned long asid,
					  unsigned long gva,
					  unsigned long gvsz,
					  unsigned long order)
{
	unsigned long pos, hgatp;

	if (PTRS_PER_PTE < (gvsz >> order)) {
		kvm_riscv_local_hfence_vvma_asid_all(vmid, asid);
		return;
	}

	hgatp = csr_swap(CSR_HGATP, vmid << HGATP_VMID_SHIFT);

	if (has_svinval()) {
		asm volatile (SFENCE_W_INVAL() ::: "memory");
		for (pos = gva; pos < (gva + gvsz); pos += BIT(order))
			asm volatile(HINVAL_VVMA(%0, %1)
			: : "r" (pos), "r" (asid) : "memory");
		asm volatile (SFENCE_INVAL_IR() ::: "memory");
	} else {
		for (pos = gva; pos < (gva + gvsz); pos += BIT(order))
			asm volatile(HFENCE_VVMA(%0, %1)
			: : "r" (pos), "r" (asid) : "memory");
	}

	csr_write(CSR_HGATP, hgatp);
}

void kvm_riscv_local_hfence_vvma_asid_all(unsigned long vmid,
					  unsigned long asid)
{
	unsigned long hgatp;

	hgatp = csr_swap(CSR_HGATP, vmid << HGATP_VMID_SHIFT);

	asm volatile(HFENCE_VVMA(zero, %0) : : "r" (asid) : "memory");

	csr_write(CSR_HGATP, hgatp);
}

void kvm_riscv_local_hfence_vvma_gva(unsigned long vmid,
				     unsigned long gva, unsigned long gvsz,
				     unsigned long order)
{
	unsigned long pos, hgatp;

	if (PTRS_PER_PTE < (gvsz >> order)) {
		kvm_riscv_local_hfence_vvma_all(vmid);
		return;
	}

	hgatp = csr_swap(CSR_HGATP, vmid << HGATP_VMID_SHIFT);

	if (has_svinval()) {
		asm volatile (SFENCE_W_INVAL() ::: "memory");
		for (pos = gva; pos < (gva + gvsz); pos += BIT(order))
			asm volatile(HINVAL_VVMA(%0, zero)
			: : "r" (pos) : "memory");
		asm volatile (SFENCE_INVAL_IR() ::: "memory");
	} else {
		for (pos = gva; pos < (gva + gvsz); pos += BIT(order))
			asm volatile(HFENCE_VVMA(%0, zero)
			: : "r" (pos) : "memory");
	}

	csr_write(CSR_HGATP, hgatp);
}

void kvm_riscv_local_hfence_vvma_all(unsigned long vmid)
{
	unsigned long hgatp;

	hgatp = csr_swap(CSR_HGATP, vmid << HGATP_VMID_SHIFT);

	asm volatile(HFENCE_VVMA(zero, zero) : : : "memory");

	csr_write(CSR_HGATP, hgatp);
}

void kvm_riscv_local_tlb_sanitize(struct kvm_vcpu *vcpu)
{
	unsigned long vmid;

	if (!kvm_riscv_gstage_vmid_bits() ||
	    vcpu->arch.last_exit_cpu == vcpu->cpu)
		return;

	/*
	 * On RISC-V platforms with hardware VMID support, we share same
	 * VMID for all VCPUs of a particular Guest/VM. This means we might
	 * have stale G-stage TLB entries on the current Host CPU due to
	 * some other VCPU of the same Guest which ran previously on the
	 * current Host CPU.
	 *
	 * To cleanup stale TLB entries, we simply flush all G-stage TLB
	 * entries by VMID whenever underlying Host CPU changes for a VCPU.
	 */

	vmid = READ_ONCE(vcpu->kvm->arch.vmid.vmid);
	kvm_riscv_local_hfence_gvma_vmid_all(vmid);
}

void kvm_riscv_fence_i_process(struct kvm_vcpu *vcpu)
{
	kvm_riscv_vcpu_pmu_incr_fw(vcpu, SBI_PMU_FW_FENCE_I_RCVD);
	local_flush_icache_all();
}

void kvm_riscv_hfence_gvma_vmid_all_process(struct kvm_vcpu *vcpu)
{
	struct kvm_vmid *vmid;

	vmid = &vcpu->kvm->arch.vmid;
	kvm_riscv_local_hfence_gvma_vmid_all(READ_ONCE(vmid->vmid));
}

void kvm_riscv_hfence_vvma_all_process(struct kvm_vcpu *vcpu)
{
	struct kvm_vmid *vmid;

	vmid = &vcpu->kvm->arch.vmid;
	kvm_riscv_local_hfence_vvma_all(READ_ONCE(vmid->vmid));
}

static bool vcpu_hfence_dequeue(struct kvm_vcpu *vcpu,
				struct kvm_riscv_hfence *out_data)
{
	bool ret = false;
	struct kvm_vcpu_arch *varch = &vcpu->arch;

	spin_lock(&varch->hfence_lock);

	if (varch->hfence_queue[varch->hfence_head].type) {
		memcpy(out_data, &varch->hfence_queue[varch->hfence_head],
		       sizeof(*out_data));
		varch->hfence_queue[varch->hfence_head].type = 0;

		varch->hfence_head++;
		if (varch->hfence_head == KVM_RISCV_VCPU_MAX_HFENCE)
			varch->hfence_head = 0;

		ret = true;
	}

	spin_unlock(&varch->hfence_lock);

	return ret;
}

static bool vcpu_hfence_enqueue(struct kvm_vcpu *vcpu,
				const struct kvm_riscv_hfence *data)
{
	bool ret = false;
	struct kvm_vcpu_arch *varch = &vcpu->arch;

	spin_lock(&varch->hfence_lock);

	if (!varch->hfence_queue[varch->hfence_tail].type) {
		memcpy(&varch->hfence_queue[varch->hfence_tail],
		       data, sizeof(*data));

		varch->hfence_tail++;
		if (varch->hfence_tail == KVM_RISCV_VCPU_MAX_HFENCE)
			varch->hfence_tail = 0;

		ret = true;
	}

	spin_unlock(&varch->hfence_lock);

	return ret;
}

void kvm_riscv_hfence_process(struct kvm_vcpu *vcpu)
{
	struct kvm_riscv_hfence d = { 0 };
	struct kvm_vmid *v = &vcpu->kvm->arch.vmid;

	while (vcpu_hfence_dequeue(vcpu, &d)) {
		switch (d.type) {
		case KVM_RISCV_HFENCE_UNKNOWN:
			break;
		case KVM_RISCV_HFENCE_GVMA_VMID_GPA:
			kvm_riscv_local_hfence_gvma_vmid_gpa(
						READ_ONCE(v->vmid),
						d.addr, d.size, d.order);
			break;
		case KVM_RISCV_HFENCE_VVMA_ASID_GVA:
			kvm_riscv_vcpu_pmu_incr_fw(vcpu, SBI_PMU_FW_HFENCE_VVMA_ASID_RCVD);
			kvm_riscv_local_hfence_vvma_asid_gva(
						READ_ONCE(v->vmid), d.asid,
						d.addr, d.size, d.order);
			break;
		case KVM_RISCV_HFENCE_VVMA_ASID_ALL:
			kvm_riscv_vcpu_pmu_incr_fw(vcpu, SBI_PMU_FW_HFENCE_VVMA_ASID_RCVD);
			kvm_riscv_local_hfence_vvma_asid_all(
						READ_ONCE(v->vmid), d.asid);
			break;
		case KVM_RISCV_HFENCE_VVMA_GVA:
			kvm_riscv_vcpu_pmu_incr_fw(vcpu, SBI_PMU_FW_HFENCE_VVMA_RCVD);
			kvm_riscv_local_hfence_vvma_gva(
						READ_ONCE(v->vmid),
						d.addr, d.size, d.order);
			break;
		default:
			break;
		}
	}
}

static void make_xfence_request(struct kvm *kvm,
				unsigned long hbase, unsigned long hmask,
				unsigned int req, unsigned int fallback_req,
				const struct kvm_riscv_hfence *data)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;
	unsigned int actual_req = req;
	DECLARE_BITMAP(vcpu_mask, KVM_MAX_VCPUS);

	bitmap_zero(vcpu_mask, KVM_MAX_VCPUS);
	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (hbase != -1UL) {
			if (vcpu->vcpu_id < hbase)
				continue;
			if (!(hmask & (1UL << (vcpu->vcpu_id - hbase))))
				continue;
		}

		bitmap_set(vcpu_mask, i, 1);

		if (!data || !data->type)
			continue;

		/*
		 * Enqueue hfence data to VCPU hfence queue. If we don't
		 * have space in the VCPU hfence queue then fallback to
		 * a more conservative hfence request.
		 */
		if (!vcpu_hfence_enqueue(vcpu, data))
			actual_req = fallback_req;
	}

	kvm_make_vcpus_request_mask(kvm, actual_req, vcpu_mask);
}

void kvm_riscv_fence_i(struct kvm *kvm,
		       unsigned long hbase, unsigned long hmask)
{
	make_xfence_request(kvm, hbase, hmask, KVM_REQ_FENCE_I,
			    KVM_REQ_FENCE_I, NULL);
}

void kvm_riscv_hfence_gvma_vmid_gpa(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask,
				    gpa_t gpa, gpa_t gpsz,
				    unsigned long order)
{
	struct kvm_riscv_hfence data;

	data.type = KVM_RISCV_HFENCE_GVMA_VMID_GPA;
	data.asid = 0;
	data.addr = gpa;
	data.size = gpsz;
	data.order = order;
	make_xfence_request(kvm, hbase, hmask, KVM_REQ_HFENCE,
			    KVM_REQ_HFENCE_GVMA_VMID_ALL, &data);
}

void kvm_riscv_hfence_gvma_vmid_all(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask)
{
	make_xfence_request(kvm, hbase, hmask, KVM_REQ_HFENCE_GVMA_VMID_ALL,
			    KVM_REQ_HFENCE_GVMA_VMID_ALL, NULL);
}

void kvm_riscv_hfence_vvma_asid_gva(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask,
				    unsigned long gva, unsigned long gvsz,
				    unsigned long order, unsigned long asid)
{
	struct kvm_riscv_hfence data;

	data.type = KVM_RISCV_HFENCE_VVMA_ASID_GVA;
	data.asid = asid;
	data.addr = gva;
	data.size = gvsz;
	data.order = order;
	make_xfence_request(kvm, hbase, hmask, KVM_REQ_HFENCE,
			    KVM_REQ_HFENCE_VVMA_ALL, &data);
}

void kvm_riscv_hfence_vvma_asid_all(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask,
				    unsigned long asid)
{
	struct kvm_riscv_hfence data;

	data.type = KVM_RISCV_HFENCE_VVMA_ASID_ALL;
	data.asid = asid;
	data.addr = data.size = data.order = 0;
	make_xfence_request(kvm, hbase, hmask, KVM_REQ_HFENCE,
			    KVM_REQ_HFENCE_VVMA_ALL, &data);
}

void kvm_riscv_hfence_vvma_gva(struct kvm *kvm,
			       unsigned long hbase, unsigned long hmask,
			       unsigned long gva, unsigned long gvsz,
			       unsigned long order)
{
	struct kvm_riscv_hfence data;

	data.type = KVM_RISCV_HFENCE_VVMA_GVA;
	data.asid = 0;
	data.addr = gva;
	data.size = gvsz;
	data.order = order;
	make_xfence_request(kvm, hbase, hmask, KVM_REQ_HFENCE,
			    KVM_REQ_HFENCE_VVMA_ALL, &data);
}

void kvm_riscv_hfence_vvma_all(struct kvm *kvm,
			       unsigned long hbase, unsigned long hmask)
{
	make_xfence_request(kvm, hbase, hmask, KVM_REQ_HFENCE_VVMA_ALL,
			    KVM_REQ_HFENCE_VVMA_ALL, NULL);
}

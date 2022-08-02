// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/kvm_host.h>
#include <asm/csr.h>

static unsigned long vmid_version = 1;
static unsigned long vmid_next;
static unsigned long vmid_bits;
static DEFINE_SPINLOCK(vmid_lock);

void kvm_riscv_gstage_vmid_detect(void)
{
	unsigned long old;

	/* Figure-out number of VMID bits in HW */
	old = csr_read(CSR_HGATP);
	csr_write(CSR_HGATP, old | HGATP_VMID_MASK);
	vmid_bits = csr_read(CSR_HGATP);
	vmid_bits = (vmid_bits & HGATP_VMID_MASK) >> HGATP_VMID_SHIFT;
	vmid_bits = fls_long(vmid_bits);
	csr_write(CSR_HGATP, old);

	/* We polluted local TLB so flush all guest TLB */
	kvm_riscv_local_hfence_gvma_all();

	/* We don't use VMID bits if they are not sufficient */
	if ((1UL << vmid_bits) < num_possible_cpus())
		vmid_bits = 0;
}

unsigned long kvm_riscv_gstage_vmid_bits(void)
{
	return vmid_bits;
}

int kvm_riscv_gstage_vmid_init(struct kvm *kvm)
{
	/* Mark the initial VMID and VMID version invalid */
	kvm->arch.vmid.vmid_version = 0;
	kvm->arch.vmid.vmid = 0;

	return 0;
}

bool kvm_riscv_gstage_vmid_ver_changed(struct kvm_vmid *vmid)
{
	if (!vmid_bits)
		return false;

	return unlikely(READ_ONCE(vmid->vmid_version) !=
			READ_ONCE(vmid_version));
}

static void __local_hfence_gvma_all(void *info)
{
	kvm_riscv_local_hfence_gvma_all();
}

void kvm_riscv_gstage_vmid_update(struct kvm_vcpu *vcpu)
{
	unsigned long i;
	struct kvm_vcpu *v;
	struct kvm_vmid *vmid = &vcpu->kvm->arch.vmid;

	if (!kvm_riscv_gstage_vmid_ver_changed(vmid))
		return;

	spin_lock(&vmid_lock);

	/*
	 * We need to re-check the vmid_version here to ensure that if
	 * another vcpu already allocated a valid vmid for this vm.
	 */
	if (!kvm_riscv_gstage_vmid_ver_changed(vmid)) {
		spin_unlock(&vmid_lock);
		return;
	}

	/* First user of a new VMID version? */
	if (unlikely(vmid_next == 0)) {
		WRITE_ONCE(vmid_version, READ_ONCE(vmid_version) + 1);
		vmid_next = 1;

		/*
		 * We ran out of VMIDs so we increment vmid_version and
		 * start assigning VMIDs from 1.
		 *
		 * This also means existing VMIDs assignment to all Guest
		 * instances is invalid and we have force VMID re-assignement
		 * for all Guest instances. The Guest instances that were not
		 * running will automatically pick-up new VMIDs because will
		 * call kvm_riscv_gstage_vmid_update() whenever they enter
		 * in-kernel run loop. For Guest instances that are already
		 * running, we force VM exits on all host CPUs using IPI and
		 * flush all Guest TLBs.
		 */
		on_each_cpu_mask(cpu_online_mask, __local_hfence_gvma_all,
				 NULL, 1);
	}

	vmid->vmid = vmid_next;
	vmid_next++;
	vmid_next &= (1 << vmid_bits) - 1;

	WRITE_ONCE(vmid->vmid_version, READ_ONCE(vmid_version));

	spin_unlock(&vmid_lock);

	/* Request G-stage page table update for all VCPUs */
	kvm_for_each_vcpu(i, v, vcpu->kvm)
		kvm_make_request(KVM_REQ_UPDATE_HGATP, v);
}

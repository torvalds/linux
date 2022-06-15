// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * AMD SVM support
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 */

#define pr_fmt(fmt) "SVM: " fmt

#include <linux/kvm_types.h>
#include <linux/hashtable.h>
#include <linux/amd-iommu.h>
#include <linux/kvm_host.h>

#include <asm/irq_remapping.h>

#include "trace.h"
#include "lapic.h"
#include "x86.h"
#include "irq.h"
#include "svm.h"

/* AVIC GATAG is encoded using VM and VCPU IDs */
#define AVIC_VCPU_ID_BITS		8
#define AVIC_VCPU_ID_MASK		((1 << AVIC_VCPU_ID_BITS) - 1)

#define AVIC_VM_ID_BITS			24
#define AVIC_VM_ID_NR			(1 << AVIC_VM_ID_BITS)
#define AVIC_VM_ID_MASK			((1 << AVIC_VM_ID_BITS) - 1)

#define AVIC_GATAG(x, y)		(((x & AVIC_VM_ID_MASK) << AVIC_VCPU_ID_BITS) | \
						(y & AVIC_VCPU_ID_MASK))
#define AVIC_GATAG_TO_VMID(x)		((x >> AVIC_VCPU_ID_BITS) & AVIC_VM_ID_MASK)
#define AVIC_GATAG_TO_VCPUID(x)		(x & AVIC_VCPU_ID_MASK)

/* Note:
 * This hash table is used to map VM_ID to a struct kvm_svm,
 * when handling AMD IOMMU GALOG notification to schedule in
 * a particular vCPU.
 */
#define SVM_VM_DATA_HASH_BITS	8
static DEFINE_HASHTABLE(svm_vm_data_hash, SVM_VM_DATA_HASH_BITS);
static u32 next_vm_id = 0;
static bool next_vm_id_wrapped = 0;
static DEFINE_SPINLOCK(svm_vm_data_hash_lock);

/*
 * This is a wrapper of struct amd_iommu_ir_data.
 */
struct amd_svm_iommu_ir {
	struct list_head node;	/* Used by SVM for per-vcpu ir_list */
	void *data;		/* Storing pointer to struct amd_ir_data */
};


/* Note:
 * This function is called from IOMMU driver to notify
 * SVM to schedule in a particular vCPU of a particular VM.
 */
int avic_ga_log_notifier(u32 ga_tag)
{
	unsigned long flags;
	struct kvm_svm *kvm_svm;
	struct kvm_vcpu *vcpu = NULL;
	u32 vm_id = AVIC_GATAG_TO_VMID(ga_tag);
	u32 vcpu_id = AVIC_GATAG_TO_VCPUID(ga_tag);

	pr_debug("SVM: %s: vm_id=%#x, vcpu_id=%#x\n", __func__, vm_id, vcpu_id);
	trace_kvm_avic_ga_log(vm_id, vcpu_id);

	spin_lock_irqsave(&svm_vm_data_hash_lock, flags);
	hash_for_each_possible(svm_vm_data_hash, kvm_svm, hnode, vm_id) {
		if (kvm_svm->avic_vm_id != vm_id)
			continue;
		vcpu = kvm_get_vcpu_by_id(&kvm_svm->kvm, vcpu_id);
		break;
	}
	spin_unlock_irqrestore(&svm_vm_data_hash_lock, flags);

	/* Note:
	 * At this point, the IOMMU should have already set the pending
	 * bit in the vAPIC backing page. So, we just need to schedule
	 * in the vcpu.
	 */
	if (vcpu)
		kvm_vcpu_wake_up(vcpu);

	return 0;
}

void avic_vm_destroy(struct kvm *kvm)
{
	unsigned long flags;
	struct kvm_svm *kvm_svm = to_kvm_svm(kvm);

	if (!enable_apicv)
		return;

	if (kvm_svm->avic_logical_id_table_page)
		__free_page(kvm_svm->avic_logical_id_table_page);
	if (kvm_svm->avic_physical_id_table_page)
		__free_page(kvm_svm->avic_physical_id_table_page);

	spin_lock_irqsave(&svm_vm_data_hash_lock, flags);
	hash_del(&kvm_svm->hnode);
	spin_unlock_irqrestore(&svm_vm_data_hash_lock, flags);
}

int avic_vm_init(struct kvm *kvm)
{
	unsigned long flags;
	int err = -ENOMEM;
	struct kvm_svm *kvm_svm = to_kvm_svm(kvm);
	struct kvm_svm *k2;
	struct page *p_page;
	struct page *l_page;
	u32 vm_id;

	if (!enable_apicv)
		return 0;

	/* Allocating physical APIC ID table (4KB) */
	p_page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!p_page)
		goto free_avic;

	kvm_svm->avic_physical_id_table_page = p_page;

	/* Allocating logical APIC ID table (4KB) */
	l_page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!l_page)
		goto free_avic;

	kvm_svm->avic_logical_id_table_page = l_page;

	spin_lock_irqsave(&svm_vm_data_hash_lock, flags);
 again:
	vm_id = next_vm_id = (next_vm_id + 1) & AVIC_VM_ID_MASK;
	if (vm_id == 0) { /* id is 1-based, zero is not okay */
		next_vm_id_wrapped = 1;
		goto again;
	}
	/* Is it still in use? Only possible if wrapped at least once */
	if (next_vm_id_wrapped) {
		hash_for_each_possible(svm_vm_data_hash, k2, hnode, vm_id) {
			if (k2->avic_vm_id == vm_id)
				goto again;
		}
	}
	kvm_svm->avic_vm_id = vm_id;
	hash_add(svm_vm_data_hash, &kvm_svm->hnode, kvm_svm->avic_vm_id);
	spin_unlock_irqrestore(&svm_vm_data_hash_lock, flags);

	return 0;

free_avic:
	avic_vm_destroy(kvm);
	return err;
}

void avic_init_vmcb(struct vcpu_svm *svm, struct vmcb *vmcb)
{
	struct kvm_svm *kvm_svm = to_kvm_svm(svm->vcpu.kvm);
	phys_addr_t bpa = __sme_set(page_to_phys(svm->avic_backing_page));
	phys_addr_t lpa = __sme_set(page_to_phys(kvm_svm->avic_logical_id_table_page));
	phys_addr_t ppa = __sme_set(page_to_phys(kvm_svm->avic_physical_id_table_page));

	vmcb->control.avic_backing_page = bpa & AVIC_HPA_MASK;
	vmcb->control.avic_logical_id = lpa & AVIC_HPA_MASK;
	vmcb->control.avic_physical_id = ppa & AVIC_HPA_MASK;
	vmcb->control.avic_physical_id |= AVIC_MAX_PHYSICAL_ID_COUNT;
	vmcb->control.avic_vapic_bar = APIC_DEFAULT_PHYS_BASE & VMCB_AVIC_APIC_BAR_MASK;

	if (kvm_apicv_activated(svm->vcpu.kvm))
		vmcb->control.int_ctl |= AVIC_ENABLE_MASK;
	else
		vmcb->control.int_ctl &= ~AVIC_ENABLE_MASK;
}

static u64 *avic_get_physical_id_entry(struct kvm_vcpu *vcpu,
				       unsigned int index)
{
	u64 *avic_physical_id_table;
	struct kvm_svm *kvm_svm = to_kvm_svm(vcpu->kvm);

	if (index >= AVIC_MAX_PHYSICAL_ID_COUNT)
		return NULL;

	avic_physical_id_table = page_address(kvm_svm->avic_physical_id_table_page);

	return &avic_physical_id_table[index];
}

/*
 * Note:
 * AVIC hardware walks the nested page table to check permissions,
 * but does not use the SPA address specified in the leaf page
 * table entry since it uses  address in the AVIC_BACKING_PAGE pointer
 * field of the VMCB. Therefore, we set up the
 * APIC_ACCESS_PAGE_PRIVATE_MEMSLOT (4KB) here.
 */
static int avic_alloc_access_page(struct kvm *kvm)
{
	void __user *ret;
	int r = 0;

	mutex_lock(&kvm->slots_lock);

	if (kvm->arch.apic_access_memslot_enabled)
		goto out;

	ret = __x86_set_memory_region(kvm,
				      APIC_ACCESS_PAGE_PRIVATE_MEMSLOT,
				      APIC_DEFAULT_PHYS_BASE,
				      PAGE_SIZE);
	if (IS_ERR(ret)) {
		r = PTR_ERR(ret);
		goto out;
	}

	kvm->arch.apic_access_memslot_enabled = true;
out:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

static int avic_init_backing_page(struct kvm_vcpu *vcpu)
{
	u64 *entry, new_entry;
	int id = vcpu->vcpu_id;
	struct vcpu_svm *svm = to_svm(vcpu);

	if (id >= AVIC_MAX_PHYSICAL_ID_COUNT)
		return -EINVAL;

	if (!vcpu->arch.apic->regs)
		return -EINVAL;

	if (kvm_apicv_activated(vcpu->kvm)) {
		int ret;

		ret = avic_alloc_access_page(vcpu->kvm);
		if (ret)
			return ret;
	}

	svm->avic_backing_page = virt_to_page(vcpu->arch.apic->regs);

	/* Setting AVIC backing page address in the phy APIC ID table */
	entry = avic_get_physical_id_entry(vcpu, id);
	if (!entry)
		return -EINVAL;

	new_entry = __sme_set((page_to_phys(svm->avic_backing_page) &
			      AVIC_PHYSICAL_ID_ENTRY_BACKING_PAGE_MASK) |
			      AVIC_PHYSICAL_ID_ENTRY_VALID_MASK);
	WRITE_ONCE(*entry, new_entry);

	svm->avic_physical_id_cache = entry;

	return 0;
}

void avic_ring_doorbell(struct kvm_vcpu *vcpu)
{
	/*
	 * Note, the vCPU could get migrated to a different pCPU at any point,
	 * which could result in signalling the wrong/previous pCPU.  But if
	 * that happens the vCPU is guaranteed to do a VMRUN (after being
	 * migrated) and thus will process pending interrupts, i.e. a doorbell
	 * is not needed (and the spurious one is harmless).
	 */
	int cpu = READ_ONCE(vcpu->cpu);

	if (cpu != get_cpu())
		wrmsrl(MSR_AMD64_SVM_AVIC_DOORBELL, kvm_cpu_get_apicid(cpu));
	put_cpu();
}

/*
 * A fast-path version of avic_kick_target_vcpus(), which attempts to match
 * destination APIC ID to vCPU without looping through all vCPUs.
 */
static int avic_kick_target_vcpus_fast(struct kvm *kvm, struct kvm_lapic *source,
				       u32 icrl, u32 icrh, u32 index)
{
	u32 dest, apic_id;
	struct kvm_vcpu *vcpu;
	int dest_mode = icrl & APIC_DEST_MASK;
	int shorthand = icrl & APIC_SHORT_MASK;
	struct kvm_svm *kvm_svm = to_kvm_svm(kvm);
	u32 *avic_logical_id_table = page_address(kvm_svm->avic_logical_id_table_page);

	if (shorthand != APIC_DEST_NOSHORT)
		return -EINVAL;

	/*
	 * The AVIC incomplete IPI #vmexit info provides index into
	 * the physical APIC ID table, which can be used to derive
	 * guest physical APIC ID.
	 */
	if (dest_mode == APIC_DEST_PHYSICAL) {
		apic_id = index;
	} else {
		if (!apic_x2apic_mode(source)) {
			/* For xAPIC logical mode, the index is for logical APIC table. */
			apic_id = avic_logical_id_table[index] & 0x1ff;
		} else {
			return -EINVAL;
		}
	}

	/*
	 * Assuming vcpu ID is the same as physical apic ID,
	 * and use it to retrieve the target vCPU.
	 */
	vcpu = kvm_get_vcpu_by_id(kvm, apic_id);
	if (!vcpu)
		return -EINVAL;

	if (apic_x2apic_mode(vcpu->arch.apic))
		dest = icrh;
	else
		dest = GET_APIC_DEST_FIELD(icrh);

	/*
	 * Try matching the destination APIC ID with the vCPU.
	 */
	if (kvm_apic_match_dest(vcpu, source, shorthand, dest, dest_mode)) {
		vcpu->arch.apic->irr_pending = true;
		svm_complete_interrupt_delivery(vcpu,
						icrl & APIC_MODE_MASK,
						icrl & APIC_INT_LEVELTRIG,
						icrl & APIC_VECTOR_MASK);
		return 0;
	}

	return -EINVAL;
}

static void avic_kick_target_vcpus(struct kvm *kvm, struct kvm_lapic *source,
				   u32 icrl, u32 icrh, u32 index)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;

	if (!avic_kick_target_vcpus_fast(kvm, source, icrl, icrh, index))
		return;

	trace_kvm_avic_kick_vcpu_slowpath(icrh, icrl, index);

	/*
	 * Wake any target vCPUs that are blocking, i.e. waiting for a wake
	 * event.  There's no need to signal doorbells, as hardware has handled
	 * vCPUs that were in guest at the time of the IPI, and vCPUs that have
	 * since entered the guest will have processed pending IRQs at VMRUN.
	 */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (kvm_apic_match_dest(vcpu, source, icrl & APIC_SHORT_MASK,
					GET_APIC_DEST_FIELD(icrh),
					icrl & APIC_DEST_MASK)) {
			vcpu->arch.apic->irr_pending = true;
			svm_complete_interrupt_delivery(vcpu,
							icrl & APIC_MODE_MASK,
							icrl & APIC_INT_LEVELTRIG,
							icrl & APIC_VECTOR_MASK);
		}
	}
}

int avic_incomplete_ipi_interception(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 icrh = svm->vmcb->control.exit_info_1 >> 32;
	u32 icrl = svm->vmcb->control.exit_info_1;
	u32 id = svm->vmcb->control.exit_info_2 >> 32;
	u32 index = svm->vmcb->control.exit_info_2 & 0x1FF;
	struct kvm_lapic *apic = vcpu->arch.apic;

	trace_kvm_avic_incomplete_ipi(vcpu->vcpu_id, icrh, icrl, id, index);

	switch (id) {
	case AVIC_IPI_FAILURE_INVALID_INT_TYPE:
		/*
		 * Emulate IPIs that are not handled by AVIC hardware, which
		 * only virtualizes Fixed, Edge-Triggered INTRs.  The exit is
		 * a trap, e.g. ICR holds the correct value and RIP has been
		 * advanced, KVM is responsible only for emulating the IPI.
		 * Sadly, hardware may sometimes leave the BUSY flag set, in
		 * which case KVM needs to emulate the ICR write as well in
		 * order to clear the BUSY flag.
		 */
		if (icrl & APIC_ICR_BUSY)
			kvm_apic_write_nodecode(vcpu, APIC_ICR);
		else
			kvm_apic_send_ipi(apic, icrl, icrh);
		break;
	case AVIC_IPI_FAILURE_TARGET_NOT_RUNNING:
		/*
		 * At this point, we expect that the AVIC HW has already
		 * set the appropriate IRR bits on the valid target
		 * vcpus. So, we just need to kick the appropriate vcpu.
		 */
		avic_kick_target_vcpus(vcpu->kvm, apic, icrl, icrh, index);
		break;
	case AVIC_IPI_FAILURE_INVALID_TARGET:
		break;
	case AVIC_IPI_FAILURE_INVALID_BACKING_PAGE:
		WARN_ONCE(1, "Invalid backing page\n");
		break;
	default:
		pr_err("Unknown IPI interception\n");
	}

	return 1;
}

unsigned long avic_vcpu_get_apicv_inhibit_reasons(struct kvm_vcpu *vcpu)
{
	if (is_guest_mode(vcpu))
		return APICV_INHIBIT_REASON_NESTED;
	return 0;
}

static u32 *avic_get_logical_id_entry(struct kvm_vcpu *vcpu, u32 ldr, bool flat)
{
	struct kvm_svm *kvm_svm = to_kvm_svm(vcpu->kvm);
	int index;
	u32 *logical_apic_id_table;
	int dlid = GET_APIC_LOGICAL_ID(ldr);

	if (!dlid)
		return NULL;

	if (flat) { /* flat */
		index = ffs(dlid) - 1;
		if (index > 7)
			return NULL;
	} else { /* cluster */
		int cluster = (dlid & 0xf0) >> 4;
		int apic = ffs(dlid & 0x0f) - 1;

		if ((apic < 0) || (apic > 7) ||
		    (cluster >= 0xf))
			return NULL;
		index = (cluster << 2) + apic;
	}

	logical_apic_id_table = (u32 *) page_address(kvm_svm->avic_logical_id_table_page);

	return &logical_apic_id_table[index];
}

static int avic_ldr_write(struct kvm_vcpu *vcpu, u8 g_physical_id, u32 ldr)
{
	bool flat;
	u32 *entry, new_entry;

	flat = kvm_lapic_get_reg(vcpu->arch.apic, APIC_DFR) == APIC_DFR_FLAT;
	entry = avic_get_logical_id_entry(vcpu, ldr, flat);
	if (!entry)
		return -EINVAL;

	new_entry = READ_ONCE(*entry);
	new_entry &= ~AVIC_LOGICAL_ID_ENTRY_GUEST_PHYSICAL_ID_MASK;
	new_entry |= (g_physical_id & AVIC_LOGICAL_ID_ENTRY_GUEST_PHYSICAL_ID_MASK);
	new_entry |= AVIC_LOGICAL_ID_ENTRY_VALID_MASK;
	WRITE_ONCE(*entry, new_entry);

	return 0;
}

static void avic_invalidate_logical_id_entry(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	bool flat = svm->dfr_reg == APIC_DFR_FLAT;
	u32 *entry = avic_get_logical_id_entry(vcpu, svm->ldr_reg, flat);

	if (entry)
		clear_bit(AVIC_LOGICAL_ID_ENTRY_VALID_BIT, (unsigned long *)entry);
}

static int avic_handle_ldr_update(struct kvm_vcpu *vcpu)
{
	int ret = 0;
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 ldr = kvm_lapic_get_reg(vcpu->arch.apic, APIC_LDR);
	u32 id = kvm_xapic_id(vcpu->arch.apic);

	if (ldr == svm->ldr_reg)
		return 0;

	avic_invalidate_logical_id_entry(vcpu);

	if (ldr)
		ret = avic_ldr_write(vcpu, id, ldr);

	if (!ret)
		svm->ldr_reg = ldr;

	return ret;
}

static int avic_handle_apic_id_update(struct kvm_vcpu *vcpu)
{
	u64 *old, *new;
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 id = kvm_xapic_id(vcpu->arch.apic);

	if (vcpu->vcpu_id == id)
		return 0;

	old = avic_get_physical_id_entry(vcpu, vcpu->vcpu_id);
	new = avic_get_physical_id_entry(vcpu, id);
	if (!new || !old)
		return 1;

	/* We need to move physical_id_entry to new offset */
	*new = *old;
	*old = 0ULL;
	to_svm(vcpu)->avic_physical_id_cache = new;

	/*
	 * Also update the guest physical APIC ID in the logical
	 * APIC ID table entry if already setup the LDR.
	 */
	if (svm->ldr_reg)
		avic_handle_ldr_update(vcpu);

	return 0;
}

static void avic_handle_dfr_update(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 dfr = kvm_lapic_get_reg(vcpu->arch.apic, APIC_DFR);

	if (svm->dfr_reg == dfr)
		return;

	avic_invalidate_logical_id_entry(vcpu);
	svm->dfr_reg = dfr;
}

static int avic_unaccel_trap_write(struct kvm_vcpu *vcpu)
{
	u32 offset = to_svm(vcpu)->vmcb->control.exit_info_1 &
				AVIC_UNACCEL_ACCESS_OFFSET_MASK;

	switch (offset) {
	case APIC_ID:
		if (avic_handle_apic_id_update(vcpu))
			return 0;
		break;
	case APIC_LDR:
		if (avic_handle_ldr_update(vcpu))
			return 0;
		break;
	case APIC_DFR:
		avic_handle_dfr_update(vcpu);
		break;
	default:
		break;
	}

	kvm_apic_write_nodecode(vcpu, offset);
	return 1;
}

static bool is_avic_unaccelerated_access_trap(u32 offset)
{
	bool ret = false;

	switch (offset) {
	case APIC_ID:
	case APIC_EOI:
	case APIC_RRR:
	case APIC_LDR:
	case APIC_DFR:
	case APIC_SPIV:
	case APIC_ESR:
	case APIC_ICR:
	case APIC_LVTT:
	case APIC_LVTTHMR:
	case APIC_LVTPC:
	case APIC_LVT0:
	case APIC_LVT1:
	case APIC_LVTERR:
	case APIC_TMICT:
	case APIC_TDCR:
		ret = true;
		break;
	default:
		break;
	}
	return ret;
}

int avic_unaccelerated_access_interception(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int ret = 0;
	u32 offset = svm->vmcb->control.exit_info_1 &
		     AVIC_UNACCEL_ACCESS_OFFSET_MASK;
	u32 vector = svm->vmcb->control.exit_info_2 &
		     AVIC_UNACCEL_ACCESS_VECTOR_MASK;
	bool write = (svm->vmcb->control.exit_info_1 >> 32) &
		     AVIC_UNACCEL_ACCESS_WRITE_MASK;
	bool trap = is_avic_unaccelerated_access_trap(offset);

	trace_kvm_avic_unaccelerated_access(vcpu->vcpu_id, offset,
					    trap, write, vector);
	if (trap) {
		/* Handling Trap */
		WARN_ONCE(!write, "svm: Handling trap read.\n");
		ret = avic_unaccel_trap_write(vcpu);
	} else {
		/* Handling Fault */
		ret = kvm_emulate_instruction(vcpu, 0);
	}

	return ret;
}

int avic_init_vcpu(struct vcpu_svm *svm)
{
	int ret;
	struct kvm_vcpu *vcpu = &svm->vcpu;

	if (!enable_apicv || !irqchip_in_kernel(vcpu->kvm))
		return 0;

	ret = avic_init_backing_page(vcpu);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&svm->ir_list);
	spin_lock_init(&svm->ir_list_lock);
	svm->dfr_reg = APIC_DFR_FLAT;

	return ret;
}

void avic_apicv_post_state_restore(struct kvm_vcpu *vcpu)
{
	if (avic_handle_apic_id_update(vcpu) != 0)
		return;
	avic_handle_dfr_update(vcpu);
	avic_handle_ldr_update(vcpu);
}

static int avic_set_pi_irte_mode(struct kvm_vcpu *vcpu, bool activate)
{
	int ret = 0;
	unsigned long flags;
	struct amd_svm_iommu_ir *ir;
	struct vcpu_svm *svm = to_svm(vcpu);

	if (!kvm_arch_has_assigned_device(vcpu->kvm))
		return 0;

	/*
	 * Here, we go through the per-vcpu ir_list to update all existing
	 * interrupt remapping table entry targeting this vcpu.
	 */
	spin_lock_irqsave(&svm->ir_list_lock, flags);

	if (list_empty(&svm->ir_list))
		goto out;

	list_for_each_entry(ir, &svm->ir_list, node) {
		if (activate)
			ret = amd_iommu_activate_guest_mode(ir->data);
		else
			ret = amd_iommu_deactivate_guest_mode(ir->data);
		if (ret)
			break;
	}
out:
	spin_unlock_irqrestore(&svm->ir_list_lock, flags);
	return ret;
}

static void svm_ir_list_del(struct vcpu_svm *svm, struct amd_iommu_pi_data *pi)
{
	unsigned long flags;
	struct amd_svm_iommu_ir *cur;

	spin_lock_irqsave(&svm->ir_list_lock, flags);
	list_for_each_entry(cur, &svm->ir_list, node) {
		if (cur->data != pi->ir_data)
			continue;
		list_del(&cur->node);
		kfree(cur);
		break;
	}
	spin_unlock_irqrestore(&svm->ir_list_lock, flags);
}

static int svm_ir_list_add(struct vcpu_svm *svm, struct amd_iommu_pi_data *pi)
{
	int ret = 0;
	unsigned long flags;
	struct amd_svm_iommu_ir *ir;

	/**
	 * In some cases, the existing irte is updated and re-set,
	 * so we need to check here if it's already been * added
	 * to the ir_list.
	 */
	if (pi->ir_data && (pi->prev_ga_tag != 0)) {
		struct kvm *kvm = svm->vcpu.kvm;
		u32 vcpu_id = AVIC_GATAG_TO_VCPUID(pi->prev_ga_tag);
		struct kvm_vcpu *prev_vcpu = kvm_get_vcpu_by_id(kvm, vcpu_id);
		struct vcpu_svm *prev_svm;

		if (!prev_vcpu) {
			ret = -EINVAL;
			goto out;
		}

		prev_svm = to_svm(prev_vcpu);
		svm_ir_list_del(prev_svm, pi);
	}

	/**
	 * Allocating new amd_iommu_pi_data, which will get
	 * add to the per-vcpu ir_list.
	 */
	ir = kzalloc(sizeof(struct amd_svm_iommu_ir), GFP_KERNEL_ACCOUNT);
	if (!ir) {
		ret = -ENOMEM;
		goto out;
	}
	ir->data = pi->ir_data;

	spin_lock_irqsave(&svm->ir_list_lock, flags);
	list_add(&ir->node, &svm->ir_list);
	spin_unlock_irqrestore(&svm->ir_list_lock, flags);
out:
	return ret;
}

/*
 * Note:
 * The HW cannot support posting multicast/broadcast
 * interrupts to a vCPU. So, we still use legacy interrupt
 * remapping for these kind of interrupts.
 *
 * For lowest-priority interrupts, we only support
 * those with single CPU as the destination, e.g. user
 * configures the interrupts via /proc/irq or uses
 * irqbalance to make the interrupts single-CPU.
 */
static int
get_pi_vcpu_info(struct kvm *kvm, struct kvm_kernel_irq_routing_entry *e,
		 struct vcpu_data *vcpu_info, struct vcpu_svm **svm)
{
	struct kvm_lapic_irq irq;
	struct kvm_vcpu *vcpu = NULL;

	kvm_set_msi_irq(kvm, e, &irq);

	if (!kvm_intr_is_single_vcpu(kvm, &irq, &vcpu) ||
	    !kvm_irq_is_postable(&irq)) {
		pr_debug("SVM: %s: use legacy intr remap mode for irq %u\n",
			 __func__, irq.vector);
		return -1;
	}

	pr_debug("SVM: %s: use GA mode for irq %u\n", __func__,
		 irq.vector);
	*svm = to_svm(vcpu);
	vcpu_info->pi_desc_addr = __sme_set(page_to_phys((*svm)->avic_backing_page));
	vcpu_info->vector = irq.vector;

	return 0;
}

/*
 * avic_pi_update_irte - set IRTE for Posted-Interrupts
 *
 * @kvm: kvm
 * @host_irq: host irq of the interrupt
 * @guest_irq: gsi of the interrupt
 * @set: set or unset PI
 * returns 0 on success, < 0 on failure
 */
int avic_pi_update_irte(struct kvm *kvm, unsigned int host_irq,
			uint32_t guest_irq, bool set)
{
	struct kvm_kernel_irq_routing_entry *e;
	struct kvm_irq_routing_table *irq_rt;
	int idx, ret = 0;

	if (!kvm_arch_has_assigned_device(kvm) ||
	    !irq_remapping_cap(IRQ_POSTING_CAP))
		return 0;

	pr_debug("SVM: %s: host_irq=%#x, guest_irq=%#x, set=%#x\n",
		 __func__, host_irq, guest_irq, set);

	idx = srcu_read_lock(&kvm->irq_srcu);
	irq_rt = srcu_dereference(kvm->irq_routing, &kvm->irq_srcu);

	if (guest_irq >= irq_rt->nr_rt_entries ||
		hlist_empty(&irq_rt->map[guest_irq])) {
		pr_warn_once("no route for guest_irq %u/%u (broken user space?)\n",
			     guest_irq, irq_rt->nr_rt_entries);
		goto out;
	}

	hlist_for_each_entry(e, &irq_rt->map[guest_irq], link) {
		struct vcpu_data vcpu_info;
		struct vcpu_svm *svm = NULL;

		if (e->type != KVM_IRQ_ROUTING_MSI)
			continue;

		/**
		 * Here, we setup with legacy mode in the following cases:
		 * 1. When cannot target interrupt to a specific vcpu.
		 * 2. Unsetting posted interrupt.
		 * 3. APIC virtualization is disabled for the vcpu.
		 * 4. IRQ has incompatible delivery mode (SMI, INIT, etc)
		 */
		if (!get_pi_vcpu_info(kvm, e, &vcpu_info, &svm) && set &&
		    kvm_vcpu_apicv_active(&svm->vcpu)) {
			struct amd_iommu_pi_data pi;

			/* Try to enable guest_mode in IRTE */
			pi.base = __sme_set(page_to_phys(svm->avic_backing_page) &
					    AVIC_HPA_MASK);
			pi.ga_tag = AVIC_GATAG(to_kvm_svm(kvm)->avic_vm_id,
						     svm->vcpu.vcpu_id);
			pi.is_guest_mode = true;
			pi.vcpu_data = &vcpu_info;
			ret = irq_set_vcpu_affinity(host_irq, &pi);

			/**
			 * Here, we successfully setting up vcpu affinity in
			 * IOMMU guest mode. Now, we need to store the posted
			 * interrupt information in a per-vcpu ir_list so that
			 * we can reference to them directly when we update vcpu
			 * scheduling information in IOMMU irte.
			 */
			if (!ret && pi.is_guest_mode)
				svm_ir_list_add(svm, &pi);
		} else {
			/* Use legacy mode in IRTE */
			struct amd_iommu_pi_data pi;

			/**
			 * Here, pi is used to:
			 * - Tell IOMMU to use legacy mode for this interrupt.
			 * - Retrieve ga_tag of prior interrupt remapping data.
			 */
			pi.prev_ga_tag = 0;
			pi.is_guest_mode = false;
			ret = irq_set_vcpu_affinity(host_irq, &pi);

			/**
			 * Check if the posted interrupt was previously
			 * setup with the guest_mode by checking if the ga_tag
			 * was cached. If so, we need to clean up the per-vcpu
			 * ir_list.
			 */
			if (!ret && pi.prev_ga_tag) {
				int id = AVIC_GATAG_TO_VCPUID(pi.prev_ga_tag);
				struct kvm_vcpu *vcpu;

				vcpu = kvm_get_vcpu_by_id(kvm, id);
				if (vcpu)
					svm_ir_list_del(to_svm(vcpu), &pi);
			}
		}

		if (!ret && svm) {
			trace_kvm_pi_irte_update(host_irq, svm->vcpu.vcpu_id,
						 e->gsi, vcpu_info.vector,
						 vcpu_info.pi_desc_addr, set);
		}

		if (ret < 0) {
			pr_err("%s: failed to update PI IRTE\n", __func__);
			goto out;
		}
	}

	ret = 0;
out:
	srcu_read_unlock(&kvm->irq_srcu, idx);
	return ret;
}

bool avic_check_apicv_inhibit_reasons(enum kvm_apicv_inhibit reason)
{
	ulong supported = BIT(APICV_INHIBIT_REASON_DISABLE) |
			  BIT(APICV_INHIBIT_REASON_ABSENT) |
			  BIT(APICV_INHIBIT_REASON_HYPERV) |
			  BIT(APICV_INHIBIT_REASON_NESTED) |
			  BIT(APICV_INHIBIT_REASON_IRQWIN) |
			  BIT(APICV_INHIBIT_REASON_PIT_REINJ) |
			  BIT(APICV_INHIBIT_REASON_X2APIC) |
			  BIT(APICV_INHIBIT_REASON_BLOCKIRQ) |
			  BIT(APICV_INHIBIT_REASON_SEV);

	return supported & BIT(reason);
}


static inline int
avic_update_iommu_vcpu_affinity(struct kvm_vcpu *vcpu, int cpu, bool r)
{
	int ret = 0;
	unsigned long flags;
	struct amd_svm_iommu_ir *ir;
	struct vcpu_svm *svm = to_svm(vcpu);

	if (!kvm_arch_has_assigned_device(vcpu->kvm))
		return 0;

	/*
	 * Here, we go through the per-vcpu ir_list to update all existing
	 * interrupt remapping table entry targeting this vcpu.
	 */
	spin_lock_irqsave(&svm->ir_list_lock, flags);

	if (list_empty(&svm->ir_list))
		goto out;

	list_for_each_entry(ir, &svm->ir_list, node) {
		ret = amd_iommu_update_ga(cpu, r, ir->data);
		if (ret)
			break;
	}
out:
	spin_unlock_irqrestore(&svm->ir_list_lock, flags);
	return ret;
}

void __avic_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	u64 entry;
	int h_physical_id = kvm_cpu_get_apicid(cpu);
	struct vcpu_svm *svm = to_svm(vcpu);

	lockdep_assert_preemption_disabled();

	if (WARN_ON(h_physical_id & ~AVIC_PHYSICAL_ID_ENTRY_HOST_PHYSICAL_ID_MASK))
		return;

	/*
	 * No need to update anything if the vCPU is blocking, i.e. if the vCPU
	 * is being scheduled in after being preempted.  The CPU entries in the
	 * Physical APIC table and IRTE are consumed iff IsRun{ning} is '1'.
	 * If the vCPU was migrated, its new CPU value will be stuffed when the
	 * vCPU unblocks.
	 */
	if (kvm_vcpu_is_blocking(vcpu))
		return;

	entry = READ_ONCE(*(svm->avic_physical_id_cache));
	WARN_ON(entry & AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK);

	entry &= ~AVIC_PHYSICAL_ID_ENTRY_HOST_PHYSICAL_ID_MASK;
	entry |= (h_physical_id & AVIC_PHYSICAL_ID_ENTRY_HOST_PHYSICAL_ID_MASK);
	entry |= AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK;

	WRITE_ONCE(*(svm->avic_physical_id_cache), entry);
	avic_update_iommu_vcpu_affinity(vcpu, h_physical_id, true);
}

void __avic_vcpu_put(struct kvm_vcpu *vcpu)
{
	u64 entry;
	struct vcpu_svm *svm = to_svm(vcpu);

	lockdep_assert_preemption_disabled();

	entry = READ_ONCE(*(svm->avic_physical_id_cache));

	/* Nothing to do if IsRunning == '0' due to vCPU blocking. */
	if (!(entry & AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK))
		return;

	avic_update_iommu_vcpu_affinity(vcpu, -1, 0);

	entry &= ~AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK;
	WRITE_ONCE(*(svm->avic_physical_id_cache), entry);
}

static void avic_vcpu_load(struct kvm_vcpu *vcpu)
{
	int cpu = get_cpu();

	WARN_ON(cpu != vcpu->cpu);

	__avic_vcpu_load(vcpu, cpu);

	put_cpu();
}

static void avic_vcpu_put(struct kvm_vcpu *vcpu)
{
	preempt_disable();

	__avic_vcpu_put(vcpu);

	preempt_enable();
}

void avic_refresh_apicv_exec_ctrl(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb01.ptr;
	bool activated = kvm_vcpu_apicv_active(vcpu);

	if (!enable_apicv)
		return;

	if (activated) {
		/**
		 * During AVIC temporary deactivation, guest could update
		 * APIC ID, DFR and LDR registers, which would not be trapped
		 * by avic_unaccelerated_access_interception(). In this case,
		 * we need to check and update the AVIC logical APIC ID table
		 * accordingly before re-activating.
		 */
		avic_apicv_post_state_restore(vcpu);
		vmcb->control.int_ctl |= AVIC_ENABLE_MASK;
	} else {
		vmcb->control.int_ctl &= ~AVIC_ENABLE_MASK;
	}
	vmcb_mark_dirty(vmcb, VMCB_AVIC);

	if (activated)
		avic_vcpu_load(vcpu);
	else
		avic_vcpu_put(vcpu);

	avic_set_pi_irte_mode(vcpu, activated);
}

void avic_vcpu_blocking(struct kvm_vcpu *vcpu)
{
	if (!kvm_vcpu_apicv_active(vcpu))
		return;

       /*
        * Unload the AVIC when the vCPU is about to block, _before_
        * the vCPU actually blocks.
        *
        * Any IRQs that arrive before IsRunning=0 will not cause an
        * incomplete IPI vmexit on the source, therefore vIRR will also
        * be checked by kvm_vcpu_check_block() before blocking.  The
        * memory barrier implicit in set_current_state orders writing
        * IsRunning=0 before reading the vIRR.  The processor needs a
        * matching memory barrier on interrupt delivery between writing
        * IRR and reading IsRunning; the lack of this barrier might be
        * the cause of errata #1235).
        */
	avic_vcpu_put(vcpu);
}

void avic_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
	if (!kvm_vcpu_apicv_active(vcpu))
		return;

	avic_vcpu_load(vcpu);
}

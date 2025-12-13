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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kvm_types.h>
#include <linux/hashtable.h>
#include <linux/amd-iommu.h>
#include <linux/kvm_host.h>
#include <linux/kvm_irqfd.h>

#include <asm/irq_remapping.h>
#include <asm/msr.h>

#include "trace.h"
#include "lapic.h"
#include "x86.h"
#include "irq.h"
#include "svm.h"

/*
 * Encode the arbitrary VM ID and the vCPU's _index_ into the GATag so that
 * KVM can retrieve the correct vCPU from a GALog entry if an interrupt can't
 * be delivered, e.g. because the vCPU isn't running.  Use the vCPU's index
 * instead of its ID (a.k.a. its default APIC ID), as KVM is guaranteed a fast
 * lookup on the index, where as vCPUs whose index doesn't match their ID need
 * to walk the entire xarray of vCPUs in the worst case scenario.
 *
 * For the vCPU index, use however many bits are currently allowed for the max
 * guest physical APIC ID (limited by the size of the physical ID table), and
 * use whatever bits remain to assign arbitrary AVIC IDs to VMs.  Note, the
 * size of the GATag is defined by hardware (32 bits), but is an opaque value
 * as far as hardware is concerned.
 */
#define AVIC_VCPU_IDX_MASK		AVIC_PHYSICAL_MAX_INDEX_MASK

#define AVIC_VM_ID_SHIFT		HWEIGHT32(AVIC_PHYSICAL_MAX_INDEX_MASK)
#define AVIC_VM_ID_MASK			(GENMASK(31, AVIC_VM_ID_SHIFT) >> AVIC_VM_ID_SHIFT)

#define AVIC_GATAG_TO_VMID(x)		((x >> AVIC_VM_ID_SHIFT) & AVIC_VM_ID_MASK)
#define AVIC_GATAG_TO_VCPUIDX(x)	(x & AVIC_VCPU_IDX_MASK)

#define __AVIC_GATAG(vm_id, vcpu_idx)	((((vm_id) & AVIC_VM_ID_MASK) << AVIC_VM_ID_SHIFT) | \
					 ((vcpu_idx) & AVIC_VCPU_IDX_MASK))
#define AVIC_GATAG(vm_id, vcpu_idx)					\
({									\
	u32 ga_tag = __AVIC_GATAG(vm_id, vcpu_idx);			\
									\
	WARN_ON_ONCE(AVIC_GATAG_TO_VCPUIDX(ga_tag) != (vcpu_idx));	\
	WARN_ON_ONCE(AVIC_GATAG_TO_VMID(ga_tag) != (vm_id));		\
	ga_tag;								\
})

static_assert(__AVIC_GATAG(AVIC_VM_ID_MASK, AVIC_VCPU_IDX_MASK) == -1u);

#define AVIC_AUTO_MODE -1

static int avic_param_set(const char *val, const struct kernel_param *kp)
{
	if (val && sysfs_streq(val, "auto")) {
		*(int *)kp->arg = AVIC_AUTO_MODE;
		return 0;
	}

	return param_set_bint(val, kp);
}

static const struct kernel_param_ops avic_ops = {
	.flags = KERNEL_PARAM_OPS_FL_NOARG,
	.set = avic_param_set,
	.get = param_get_bool,
};

/*
 * Enable / disable AVIC.  In "auto" mode (default behavior), AVIC is enabled
 * for Zen4+ CPUs with x2AVIC (and all other criteria for enablement are met).
 */
static int avic = AVIC_AUTO_MODE;
module_param_cb(avic, &avic_ops, &avic, 0444);
__MODULE_PARM_TYPE(avic, "bool");

module_param(enable_ipiv, bool, 0444);

static bool force_avic;
module_param_unsafe(force_avic, bool, 0444);

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
static bool x2avic_enabled;


static void avic_set_x2apic_msr_interception(struct vcpu_svm *svm,
					     bool intercept)
{
	static const u32 x2avic_passthrough_msrs[] = {
		X2APIC_MSR(APIC_ID),
		X2APIC_MSR(APIC_LVR),
		X2APIC_MSR(APIC_TASKPRI),
		X2APIC_MSR(APIC_ARBPRI),
		X2APIC_MSR(APIC_PROCPRI),
		X2APIC_MSR(APIC_EOI),
		X2APIC_MSR(APIC_RRR),
		X2APIC_MSR(APIC_LDR),
		X2APIC_MSR(APIC_DFR),
		X2APIC_MSR(APIC_SPIV),
		X2APIC_MSR(APIC_ISR),
		X2APIC_MSR(APIC_TMR),
		X2APIC_MSR(APIC_IRR),
		X2APIC_MSR(APIC_ESR),
		X2APIC_MSR(APIC_ICR),
		X2APIC_MSR(APIC_ICR2),

		/*
		 * Note!  Always intercept LVTT, as TSC-deadline timer mode
		 * isn't virtualized by hardware, and the CPU will generate a
		 * #GP instead of a #VMEXIT.
		 */
		X2APIC_MSR(APIC_LVTTHMR),
		X2APIC_MSR(APIC_LVTPC),
		X2APIC_MSR(APIC_LVT0),
		X2APIC_MSR(APIC_LVT1),
		X2APIC_MSR(APIC_LVTERR),
		X2APIC_MSR(APIC_TMICT),
		X2APIC_MSR(APIC_TMCCT),
		X2APIC_MSR(APIC_TDCR),
	};
	int i;

	if (intercept == svm->x2avic_msrs_intercepted)
		return;

	if (!x2avic_enabled)
		return;

	for (i = 0; i < ARRAY_SIZE(x2avic_passthrough_msrs); i++)
		svm_set_intercept_for_msr(&svm->vcpu, x2avic_passthrough_msrs[i],
					  MSR_TYPE_RW, intercept);

	svm->x2avic_msrs_intercepted = intercept;
}

static void avic_activate_vmcb(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = svm->vmcb01.ptr;

	vmcb->control.int_ctl &= ~(AVIC_ENABLE_MASK | X2APIC_MODE_MASK);
	vmcb->control.avic_physical_id &= ~AVIC_PHYSICAL_MAX_INDEX_MASK;

	vmcb->control.int_ctl |= AVIC_ENABLE_MASK;

	/*
	 * Note: KVM supports hybrid-AVIC mode, where KVM emulates x2APIC MSR
	 * accesses, while interrupt injection to a running vCPU can be
	 * achieved using AVIC doorbell.  KVM disables the APIC access page
	 * (deletes the memslot) if any vCPU has x2APIC enabled, thus enabling
	 * AVIC in hybrid mode activates only the doorbell mechanism.
	 */
	if (x2avic_enabled && apic_x2apic_mode(svm->vcpu.arch.apic)) {
		vmcb->control.int_ctl |= X2APIC_MODE_MASK;
		vmcb->control.avic_physical_id |= X2AVIC_MAX_PHYSICAL_ID;
		/* Disabling MSR intercept for x2APIC registers */
		avic_set_x2apic_msr_interception(svm, false);
	} else {
		/*
		 * Flush the TLB, the guest may have inserted a non-APIC
		 * mapping into the TLB while AVIC was disabled.
		 */
		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, &svm->vcpu);

		/* For xAVIC and hybrid-xAVIC modes */
		vmcb->control.avic_physical_id |= AVIC_MAX_PHYSICAL_ID;
		/* Enabling MSR intercept for x2APIC registers */
		avic_set_x2apic_msr_interception(svm, true);
	}
}

static void avic_deactivate_vmcb(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = svm->vmcb01.ptr;

	vmcb->control.int_ctl &= ~(AVIC_ENABLE_MASK | X2APIC_MODE_MASK);
	vmcb->control.avic_physical_id &= ~AVIC_PHYSICAL_MAX_INDEX_MASK;

	/*
	 * If running nested and the guest uses its own MSR bitmap, there
	 * is no need to update L0's msr bitmap
	 */
	if (is_guest_mode(&svm->vcpu) &&
	    vmcb12_is_intercept(&svm->nested.ctl, INTERCEPT_MSR_PROT))
		return;

	/* Enabling MSR intercept for x2APIC registers */
	avic_set_x2apic_msr_interception(svm, true);
}

/* Note:
 * This function is called from IOMMU driver to notify
 * SVM to schedule in a particular vCPU of a particular VM.
 */
static int avic_ga_log_notifier(u32 ga_tag)
{
	unsigned long flags;
	struct kvm_svm *kvm_svm;
	struct kvm_vcpu *vcpu = NULL;
	u32 vm_id = AVIC_GATAG_TO_VMID(ga_tag);
	u32 vcpu_idx = AVIC_GATAG_TO_VCPUIDX(ga_tag);

	pr_debug("SVM: %s: vm_id=%#x, vcpu_idx=%#x\n", __func__, vm_id, vcpu_idx);
	trace_kvm_avic_ga_log(vm_id, vcpu_idx);

	spin_lock_irqsave(&svm_vm_data_hash_lock, flags);
	hash_for_each_possible(svm_vm_data_hash, kvm_svm, hnode, vm_id) {
		if (kvm_svm->avic_vm_id != vm_id)
			continue;
		vcpu = kvm_get_vcpu(&kvm_svm->kvm, vcpu_idx);
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

	free_page((unsigned long)kvm_svm->avic_logical_id_table);
	free_page((unsigned long)kvm_svm->avic_physical_id_table);

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
	u32 vm_id;

	if (!enable_apicv)
		return 0;

	kvm_svm->avic_physical_id_table = (void *)get_zeroed_page(GFP_KERNEL_ACCOUNT);
	if (!kvm_svm->avic_physical_id_table)
		goto free_avic;

	kvm_svm->avic_logical_id_table = (void *)get_zeroed_page(GFP_KERNEL_ACCOUNT);
	if (!kvm_svm->avic_logical_id_table)
		goto free_avic;

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

static phys_addr_t avic_get_backing_page_address(struct vcpu_svm *svm)
{
	return __sme_set(__pa(svm->vcpu.arch.apic->regs));
}

void avic_init_vmcb(struct vcpu_svm *svm, struct vmcb *vmcb)
{
	struct kvm_svm *kvm_svm = to_kvm_svm(svm->vcpu.kvm);

	vmcb->control.avic_backing_page = avic_get_backing_page_address(svm);
	vmcb->control.avic_logical_id = __sme_set(__pa(kvm_svm->avic_logical_id_table));
	vmcb->control.avic_physical_id = __sme_set(__pa(kvm_svm->avic_physical_id_table));
	vmcb->control.avic_vapic_bar = APIC_DEFAULT_PHYS_BASE;

	if (kvm_apicv_activated(svm->vcpu.kvm))
		avic_activate_vmcb(svm);
	else
		avic_deactivate_vmcb(svm);
}

static int avic_init_backing_page(struct kvm_vcpu *vcpu)
{
	struct kvm_svm *kvm_svm = to_kvm_svm(vcpu->kvm);
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 id = vcpu->vcpu_id;
	u64 new_entry;

	/*
	 * Inhibit AVIC if the vCPU ID is bigger than what is supported by AVIC
	 * hardware.  Immediately clear apicv_active, i.e. don't wait until the
	 * KVM_REQ_APICV_UPDATE request is processed on the first KVM_RUN, as
	 * avic_vcpu_load() expects to be called if and only if the vCPU has
	 * fully initialized AVIC.
	 */
	if ((!x2avic_enabled && id > AVIC_MAX_PHYSICAL_ID) ||
	    (id > X2AVIC_MAX_PHYSICAL_ID)) {
		kvm_set_apicv_inhibit(vcpu->kvm, APICV_INHIBIT_REASON_PHYSICAL_ID_TOO_BIG);
		vcpu->arch.apic->apicv_active = false;
		return 0;
	}

	BUILD_BUG_ON((AVIC_MAX_PHYSICAL_ID + 1) * sizeof(new_entry) > PAGE_SIZE ||
		     (X2AVIC_MAX_PHYSICAL_ID + 1) * sizeof(new_entry) > PAGE_SIZE);

	if (WARN_ON_ONCE(!vcpu->arch.apic->regs))
		return -EINVAL;

	if (kvm_apicv_activated(vcpu->kvm)) {
		int ret;

		/*
		 * Note, AVIC hardware walks the nested page table to check
		 * permissions, but does not use the SPA address specified in
		 * the leaf SPTE since it uses address in the AVIC_BACKING_PAGE
		 * pointer field of the VMCB.
		 */
		ret = kvm_alloc_apic_access_page(vcpu->kvm);
		if (ret)
			return ret;
	}

	/* Note, fls64() returns the bit position, +1. */
	BUILD_BUG_ON(__PHYSICAL_MASK_SHIFT >
		     fls64(AVIC_PHYSICAL_ID_ENTRY_BACKING_PAGE_MASK));

	/* Setting AVIC backing page address in the phy APIC ID table */
	new_entry = avic_get_backing_page_address(svm) |
		    AVIC_PHYSICAL_ID_ENTRY_VALID_MASK;
	svm->avic_physical_id_entry = new_entry;

	/*
	 * Initialize the real table, as vCPUs must have a valid entry in order
	 * for broadcast IPIs to function correctly (broadcast IPIs ignore
	 * invalid entries, i.e. aren't guaranteed to generate a VM-Exit).
	 */
	WRITE_ONCE(kvm_svm->avic_physical_id_table[id], new_entry);

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

	if (cpu != get_cpu()) {
		wrmsrq(MSR_AMD64_SVM_AVIC_DOORBELL, kvm_cpu_get_apicid(cpu));
		trace_kvm_avic_doorbell(vcpu->vcpu_id, kvm_cpu_get_apicid(cpu));
	}
	put_cpu();
}


static void avic_kick_vcpu(struct kvm_vcpu *vcpu, u32 icrl)
{
	vcpu->arch.apic->irr_pending = true;
	svm_complete_interrupt_delivery(vcpu,
					icrl & APIC_MODE_MASK,
					icrl & APIC_INT_LEVELTRIG,
					icrl & APIC_VECTOR_MASK);
}

static void avic_kick_vcpu_by_physical_id(struct kvm *kvm, u32 physical_id,
					  u32 icrl)
{
	/*
	 * KVM inhibits AVIC if any vCPU ID diverges from the vCPUs APIC ID,
	 * i.e. APIC ID == vCPU ID.
	 */
	struct kvm_vcpu *target_vcpu = kvm_get_vcpu_by_id(kvm, physical_id);

	/* Once again, nothing to do if the target vCPU doesn't exist. */
	if (unlikely(!target_vcpu))
		return;

	avic_kick_vcpu(target_vcpu, icrl);
}

static void avic_kick_vcpu_by_logical_id(struct kvm *kvm, u32 *avic_logical_id_table,
					 u32 logid_index, u32 icrl)
{
	u32 physical_id;

	if (avic_logical_id_table) {
		u32 logid_entry = avic_logical_id_table[logid_index];

		/* Nothing to do if the logical destination is invalid. */
		if (unlikely(!(logid_entry & AVIC_LOGICAL_ID_ENTRY_VALID_MASK)))
			return;

		physical_id = logid_entry &
			      AVIC_LOGICAL_ID_ENTRY_GUEST_PHYSICAL_ID_MASK;
	} else {
		/*
		 * For x2APIC, the logical APIC ID is a read-only value that is
		 * derived from the x2APIC ID, thus the x2APIC ID can be found
		 * by reversing the calculation (stored in logid_index).  Note,
		 * bits 31:20 of the x2APIC ID aren't propagated to the logical
		 * ID, but KVM limits the x2APIC ID limited to KVM_MAX_VCPU_IDS.
		 */
		physical_id = logid_index;
	}

	avic_kick_vcpu_by_physical_id(kvm, physical_id, icrl);
}

/*
 * A fast-path version of avic_kick_target_vcpus(), which attempts to match
 * destination APIC ID to vCPU without looping through all vCPUs.
 */
static int avic_kick_target_vcpus_fast(struct kvm *kvm, struct kvm_lapic *source,
				       u32 icrl, u32 icrh, u32 index)
{
	int dest_mode = icrl & APIC_DEST_MASK;
	int shorthand = icrl & APIC_SHORT_MASK;
	struct kvm_svm *kvm_svm = to_kvm_svm(kvm);
	u32 dest;

	if (shorthand != APIC_DEST_NOSHORT)
		return -EINVAL;

	if (apic_x2apic_mode(source))
		dest = icrh;
	else
		dest = GET_XAPIC_DEST_FIELD(icrh);

	if (dest_mode == APIC_DEST_PHYSICAL) {
		/* broadcast destination, use slow path */
		if (apic_x2apic_mode(source) && dest == X2APIC_BROADCAST)
			return -EINVAL;
		if (!apic_x2apic_mode(source) && dest == APIC_BROADCAST)
			return -EINVAL;

		if (WARN_ON_ONCE(dest != index))
			return -EINVAL;

		avic_kick_vcpu_by_physical_id(kvm, dest, icrl);
	} else {
		u32 *avic_logical_id_table;
		unsigned long bitmap, i;
		u32 cluster;

		if (apic_x2apic_mode(source)) {
			/* 16 bit dest mask, 16 bit cluster id */
			bitmap = dest & 0xFFFF;
			cluster = (dest >> 16) << 4;
		} else if (kvm_lapic_get_reg(source, APIC_DFR) == APIC_DFR_FLAT) {
			/* 8 bit dest mask*/
			bitmap = dest;
			cluster = 0;
		} else {
			/* 4 bit desk mask, 4 bit cluster id */
			bitmap = dest & 0xF;
			cluster = (dest >> 4) << 2;
		}

		/* Nothing to do if there are no destinations in the cluster. */
		if (unlikely(!bitmap))
			return 0;

		if (apic_x2apic_mode(source))
			avic_logical_id_table = NULL;
		else
			avic_logical_id_table = kvm_svm->avic_logical_id_table;

		/*
		 * AVIC is inhibited if vCPUs aren't mapped 1:1 with logical
		 * IDs, thus each bit in the destination is guaranteed to map
		 * to at most one vCPU.
		 */
		for_each_set_bit(i, &bitmap, 16)
			avic_kick_vcpu_by_logical_id(kvm, avic_logical_id_table,
						     cluster + i, icrl);
	}

	return 0;
}

static void avic_kick_target_vcpus(struct kvm *kvm, struct kvm_lapic *source,
				   u32 icrl, u32 icrh, u32 index)
{
	u32 dest = apic_x2apic_mode(source) ? icrh : GET_XAPIC_DEST_FIELD(icrh);
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
					dest, icrl & APIC_DEST_MASK))
			avic_kick_vcpu(vcpu, icrl);
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
	case AVIC_IPI_FAILURE_INVALID_TARGET:
	case AVIC_IPI_FAILURE_INVALID_INT_TYPE:
		/*
		 * Emulate IPIs that are not handled by AVIC hardware, which
		 * only virtualizes Fixed, Edge-Triggered INTRs, and falls over
		 * if _any_ targets are invalid, e.g. if the logical mode mask
		 * is a superset of running vCPUs.
		 *
		 * The exit is a trap, e.g. ICR holds the correct value and RIP
		 * has been advanced, KVM is responsible only for emulating the
		 * IPI.  Sadly, hardware may sometimes leave the BUSY flag set,
		 * in which case KVM needs to emulate the ICR write as well in
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
	case AVIC_IPI_FAILURE_INVALID_BACKING_PAGE:
		WARN_ONCE(1, "Invalid backing page\n");
		break;
	case AVIC_IPI_FAILURE_INVALID_IPI_VECTOR:
		/* Invalid IPI with vector < 16 */
		break;
	default:
		vcpu_unimpl(vcpu, "Unknown avic incomplete IPI interception\n");
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
	u32 cluster, index;

	ldr = GET_APIC_LOGICAL_ID(ldr);

	if (flat) {
		cluster = 0;
	} else {
		cluster = (ldr >> 4);
		if (cluster >= 0xf)
			return NULL;
		ldr &= 0xf;
	}
	if (!ldr || !is_power_of_2(ldr))
		return NULL;

	index = __ffs(ldr);
	if (WARN_ON_ONCE(index > 7))
		return NULL;
	index += (cluster << 2);

	return &kvm_svm->avic_logical_id_table[index];
}

static void avic_ldr_write(struct kvm_vcpu *vcpu, u8 g_physical_id, u32 ldr)
{
	bool flat;
	u32 *entry, new_entry;

	flat = kvm_lapic_get_reg(vcpu->arch.apic, APIC_DFR) == APIC_DFR_FLAT;
	entry = avic_get_logical_id_entry(vcpu, ldr, flat);
	if (!entry)
		return;

	new_entry = READ_ONCE(*entry);
	new_entry &= ~AVIC_LOGICAL_ID_ENTRY_GUEST_PHYSICAL_ID_MASK;
	new_entry |= (g_physical_id & AVIC_LOGICAL_ID_ENTRY_GUEST_PHYSICAL_ID_MASK);
	new_entry |= AVIC_LOGICAL_ID_ENTRY_VALID_MASK;
	WRITE_ONCE(*entry, new_entry);
}

static void avic_invalidate_logical_id_entry(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	bool flat = svm->dfr_reg == APIC_DFR_FLAT;
	u32 *entry;

	/* Note: x2AVIC does not use logical APIC ID table */
	if (apic_x2apic_mode(vcpu->arch.apic))
		return;

	entry = avic_get_logical_id_entry(vcpu, svm->ldr_reg, flat);
	if (entry)
		clear_bit(AVIC_LOGICAL_ID_ENTRY_VALID_BIT, (unsigned long *)entry);
}

static void avic_handle_ldr_update(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 ldr = kvm_lapic_get_reg(vcpu->arch.apic, APIC_LDR);
	u32 id = kvm_xapic_id(vcpu->arch.apic);

	/* AVIC does not support LDR update for x2APIC */
	if (apic_x2apic_mode(vcpu->arch.apic))
		return;

	if (ldr == svm->ldr_reg)
		return;

	avic_invalidate_logical_id_entry(vcpu);

	svm->ldr_reg = ldr;
	avic_ldr_write(vcpu, id, ldr);
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
	case APIC_LDR:
		avic_handle_ldr_update(vcpu);
		break;
	case APIC_DFR:
		avic_handle_dfr_update(vcpu);
		break;
	case APIC_RRR:
		/* Ignore writes to Read Remote Data, it's read-only. */
		return 1;
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

	INIT_LIST_HEAD(&svm->ir_list);
	raw_spin_lock_init(&svm->ir_list_lock);

	if (!enable_apicv || !irqchip_in_kernel(vcpu->kvm))
		return 0;

	ret = avic_init_backing_page(vcpu);
	if (ret)
		return ret;

	svm->dfr_reg = APIC_DFR_FLAT;

	return ret;
}

void avic_apicv_post_state_restore(struct kvm_vcpu *vcpu)
{
	avic_handle_dfr_update(vcpu);
	avic_handle_ldr_update(vcpu);
}

static void svm_ir_list_del(struct kvm_kernel_irqfd *irqfd)
{
	struct kvm_vcpu *vcpu = irqfd->irq_bypass_vcpu;
	unsigned long flags;

	if (!vcpu)
		return;

	raw_spin_lock_irqsave(&to_svm(vcpu)->ir_list_lock, flags);
	list_del(&irqfd->vcpu_list);
	raw_spin_unlock_irqrestore(&to_svm(vcpu)->ir_list_lock, flags);
}

int avic_pi_update_irte(struct kvm_kernel_irqfd *irqfd, struct kvm *kvm,
			unsigned int host_irq, uint32_t guest_irq,
			struct kvm_vcpu *vcpu, u32 vector)
{
	/*
	 * If the IRQ was affined to a different vCPU, remove the IRTE metadata
	 * from the *previous* vCPU's list.
	 */
	svm_ir_list_del(irqfd);

	if (vcpu) {
		/*
		 * Try to enable guest_mode in IRTE, unless AVIC is inhibited,
		 * in which case configure the IRTE for legacy mode, but track
		 * the IRTE metadata so that it can be converted to guest mode
		 * if AVIC is enabled/uninhibited in the future.
		 */
		struct amd_iommu_pi_data pi_data = {
			.ga_tag = AVIC_GATAG(to_kvm_svm(kvm)->avic_vm_id,
					     vcpu->vcpu_idx),
			.is_guest_mode = kvm_vcpu_apicv_active(vcpu),
			.vapic_addr = avic_get_backing_page_address(to_svm(vcpu)),
			.vector = vector,
		};
		struct vcpu_svm *svm = to_svm(vcpu);
		u64 entry;
		int ret;

		/*
		 * Prevent the vCPU from being scheduled out or migrated until
		 * the IRTE is updated and its metadata has been added to the
		 * list of IRQs being posted to the vCPU, to ensure the IRTE
		 * isn't programmed with stale pCPU/IsRunning information.
		 */
		guard(raw_spinlock_irqsave)(&svm->ir_list_lock);

		/*
		 * Update the target pCPU for IOMMU doorbells if the vCPU is
		 * running.  If the vCPU is NOT running, i.e. is blocking or
		 * scheduled out, KVM will update the pCPU info when the vCPU
		 * is awakened and/or scheduled in.  See also avic_vcpu_load().
		 */
		entry = svm->avic_physical_id_entry;
		if (entry & AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK) {
			pi_data.cpu = entry & AVIC_PHYSICAL_ID_ENTRY_HOST_PHYSICAL_ID_MASK;
		} else {
			pi_data.cpu = -1;
			pi_data.ga_log_intr = entry & AVIC_PHYSICAL_ID_ENTRY_GA_LOG_INTR;
		}

		ret = irq_set_vcpu_affinity(host_irq, &pi_data);
		if (ret)
			return ret;

		/*
		 * Revert to legacy mode if the IOMMU didn't provide metadata
		 * for the IRTE, which KVM needs to keep the IRTE up-to-date,
		 * e.g. if the vCPU is migrated or AVIC is disabled.
		 */
		if (WARN_ON_ONCE(!pi_data.ir_data)) {
			irq_set_vcpu_affinity(host_irq, NULL);
			return -EIO;
		}

		irqfd->irq_bypass_data = pi_data.ir_data;
		list_add(&irqfd->vcpu_list, &svm->ir_list);
		return 0;
	}
	return irq_set_vcpu_affinity(host_irq, NULL);
}

enum avic_vcpu_action {
	/*
	 * There is no need to differentiate between activate and deactivate,
	 * as KVM only refreshes AVIC state when the vCPU is scheduled in and
	 * isn't blocking, i.e. the pCPU must always be (in)valid when AVIC is
	 * being (de)activated.
	 */
	AVIC_TOGGLE_ON_OFF	= BIT(0),
	AVIC_ACTIVATE		= AVIC_TOGGLE_ON_OFF,
	AVIC_DEACTIVATE		= AVIC_TOGGLE_ON_OFF,

	/*
	 * No unique action is required to deal with a vCPU that stops/starts
	 * running.  A vCPU that starts running by definition stops blocking as
	 * well, and a vCPU that stops running can't have been blocking, i.e.
	 * doesn't need to toggle GALogIntr.
	 */
	AVIC_START_RUNNING	= 0,
	AVIC_STOP_RUNNING	= 0,

	/*
	 * When a vCPU starts blocking, KVM needs to set the GALogIntr flag
	 * int all associated IRTEs so that KVM can wake the vCPU if an IRQ is
	 * sent to the vCPU.
	 */
	AVIC_START_BLOCKING	= BIT(1),
};

static void avic_update_iommu_vcpu_affinity(struct kvm_vcpu *vcpu, int cpu,
					    enum avic_vcpu_action action)
{
	bool ga_log_intr = (action & AVIC_START_BLOCKING);
	struct vcpu_svm *svm = to_svm(vcpu);
	struct kvm_kernel_irqfd *irqfd;

	lockdep_assert_held(&svm->ir_list_lock);

	/*
	 * Here, we go through the per-vcpu ir_list to update all existing
	 * interrupt remapping table entry targeting this vcpu.
	 */
	if (list_empty(&svm->ir_list))
		return;

	list_for_each_entry(irqfd, &svm->ir_list, vcpu_list) {
		void *data = irqfd->irq_bypass_data;

		if (!(action & AVIC_TOGGLE_ON_OFF))
			WARN_ON_ONCE(amd_iommu_update_ga(data, cpu, ga_log_intr));
		else if (cpu >= 0)
			WARN_ON_ONCE(amd_iommu_activate_guest_mode(data, cpu, ga_log_intr));
		else
			WARN_ON_ONCE(amd_iommu_deactivate_guest_mode(data));
	}
}

static void __avic_vcpu_load(struct kvm_vcpu *vcpu, int cpu,
			     enum avic_vcpu_action action)
{
	struct kvm_svm *kvm_svm = to_kvm_svm(vcpu->kvm);
	int h_physical_id = kvm_cpu_get_apicid(cpu);
	struct vcpu_svm *svm = to_svm(vcpu);
	unsigned long flags;
	u64 entry;

	lockdep_assert_preemption_disabled();

	if (WARN_ON(h_physical_id & ~AVIC_PHYSICAL_ID_ENTRY_HOST_PHYSICAL_ID_MASK))
		return;

	if (WARN_ON_ONCE(vcpu->vcpu_id * sizeof(entry) >= PAGE_SIZE))
		return;

	/*
	 * Grab the per-vCPU interrupt remapping lock even if the VM doesn't
	 * _currently_ have assigned devices, as that can change.  Holding
	 * ir_list_lock ensures that either svm_ir_list_add() will consume
	 * up-to-date entry information, or that this task will wait until
	 * svm_ir_list_add() completes to set the new target pCPU.
	 */
	raw_spin_lock_irqsave(&svm->ir_list_lock, flags);

	entry = svm->avic_physical_id_entry;
	WARN_ON_ONCE(entry & AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK);

	entry &= ~(AVIC_PHYSICAL_ID_ENTRY_HOST_PHYSICAL_ID_MASK |
		   AVIC_PHYSICAL_ID_ENTRY_GA_LOG_INTR);
	entry |= (h_physical_id & AVIC_PHYSICAL_ID_ENTRY_HOST_PHYSICAL_ID_MASK);
	entry |= AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK;

	svm->avic_physical_id_entry = entry;

	/*
	 * If IPI virtualization is disabled, clear IsRunning when updating the
	 * actual Physical ID table, so that the CPU never sees IsRunning=1.
	 * Keep the APIC ID up-to-date in the entry to minimize the chances of
	 * things going sideways if hardware peeks at the ID.
	 */
	if (!enable_ipiv)
		entry &= ~AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK;

	WRITE_ONCE(kvm_svm->avic_physical_id_table[vcpu->vcpu_id], entry);

	avic_update_iommu_vcpu_affinity(vcpu, h_physical_id, action);

	raw_spin_unlock_irqrestore(&svm->ir_list_lock, flags);
}

void avic_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	/*
	 * No need to update anything if the vCPU is blocking, i.e. if the vCPU
	 * is being scheduled in after being preempted.  The CPU entries in the
	 * Physical APIC table and IRTE are consumed iff IsRun{ning} is '1'.
	 * If the vCPU was migrated, its new CPU value will be stuffed when the
	 * vCPU unblocks.
	 */
	if (kvm_vcpu_is_blocking(vcpu))
		return;

	__avic_vcpu_load(vcpu, cpu, AVIC_START_RUNNING);
}

static void __avic_vcpu_put(struct kvm_vcpu *vcpu, enum avic_vcpu_action action)
{
	struct kvm_svm *kvm_svm = to_kvm_svm(vcpu->kvm);
	struct vcpu_svm *svm = to_svm(vcpu);
	unsigned long flags;
	u64 entry = svm->avic_physical_id_entry;

	lockdep_assert_preemption_disabled();

	if (WARN_ON_ONCE(vcpu->vcpu_id * sizeof(entry) >= PAGE_SIZE))
		return;

	/*
	 * Take and hold the per-vCPU interrupt remapping lock while updating
	 * the Physical ID entry even though the lock doesn't protect against
	 * multiple writers (see above).  Holding ir_list_lock ensures that
	 * either svm_ir_list_add() will consume up-to-date entry information,
	 * or that this task will wait until svm_ir_list_add() completes to
	 * mark the vCPU as not running.
	 */
	raw_spin_lock_irqsave(&svm->ir_list_lock, flags);

	avic_update_iommu_vcpu_affinity(vcpu, -1, action);

	WARN_ON_ONCE(entry & AVIC_PHYSICAL_ID_ENTRY_GA_LOG_INTR);

	/*
	 * Keep the previous APIC ID in the entry so that a rogue doorbell from
	 * hardware is at least restricted to a CPU associated with the vCPU.
	 */
	entry &= ~AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK;

	if (enable_ipiv)
		WRITE_ONCE(kvm_svm->avic_physical_id_table[vcpu->vcpu_id], entry);

	/*
	 * Note!  Don't set AVIC_PHYSICAL_ID_ENTRY_GA_LOG_INTR in the table as
	 * it's a synthetic flag that usurps an unused should-be-zero bit.
	 */
	if (action & AVIC_START_BLOCKING)
		entry |= AVIC_PHYSICAL_ID_ENTRY_GA_LOG_INTR;

	svm->avic_physical_id_entry = entry;

	raw_spin_unlock_irqrestore(&svm->ir_list_lock, flags);
}

void avic_vcpu_put(struct kvm_vcpu *vcpu)
{
	/*
	 * Note, reading the Physical ID entry outside of ir_list_lock is safe
	 * as only the pCPU that has loaded (or is loading) the vCPU is allowed
	 * to modify the entry, and preemption is disabled.  I.e. the vCPU
	 * can't be scheduled out and thus avic_vcpu_{put,load}() can't run
	 * recursively.
	 */
	u64 entry = to_svm(vcpu)->avic_physical_id_entry;

	/*
	 * Nothing to do if IsRunning == '0' due to vCPU blocking, i.e. if the
	 * vCPU is preempted while its in the process of blocking.  WARN if the
	 * vCPU wasn't running and isn't blocking, KVM shouldn't attempt to put
	 * the AVIC if it wasn't previously loaded.
	 */
	if (!(entry & AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK)) {
		if (WARN_ON_ONCE(!kvm_vcpu_is_blocking(vcpu)))
			return;

		/*
		 * The vCPU was preempted while blocking, ensure its IRTEs are
		 * configured to generate GA Log Interrupts.
		 */
		if (!(WARN_ON_ONCE(!(entry & AVIC_PHYSICAL_ID_ENTRY_GA_LOG_INTR))))
			return;
	}

	__avic_vcpu_put(vcpu, kvm_vcpu_is_blocking(vcpu) ? AVIC_START_BLOCKING :
							   AVIC_STOP_RUNNING);
}

void avic_refresh_virtual_apic_mode(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb01.ptr;

	if (!lapic_in_kernel(vcpu) || !enable_apicv)
		return;

	if (kvm_vcpu_apicv_active(vcpu)) {
		/**
		 * During AVIC temporary deactivation, guest could update
		 * APIC ID, DFR and LDR registers, which would not be trapped
		 * by avic_unaccelerated_access_interception(). In this case,
		 * we need to check and update the AVIC logical APIC ID table
		 * accordingly before re-activating.
		 */
		avic_apicv_post_state_restore(vcpu);
		avic_activate_vmcb(svm);
	} else {
		avic_deactivate_vmcb(svm);
	}
	vmcb_mark_dirty(vmcb, VMCB_AVIC);
}

void avic_refresh_apicv_exec_ctrl(struct kvm_vcpu *vcpu)
{
	if (!enable_apicv)
		return;

	/* APICv should only be toggled on/off while the vCPU is running. */
	WARN_ON_ONCE(kvm_vcpu_is_blocking(vcpu));

	avic_refresh_virtual_apic_mode(vcpu);

	if (kvm_vcpu_apicv_active(vcpu))
		__avic_vcpu_load(vcpu, vcpu->cpu, AVIC_ACTIVATE);
	else
		__avic_vcpu_put(vcpu, AVIC_DEACTIVATE);
}

void avic_vcpu_blocking(struct kvm_vcpu *vcpu)
{
	if (!kvm_vcpu_apicv_active(vcpu))
		return;

	/*
	 * Unload the AVIC when the vCPU is about to block, _before_ the vCPU
	 * actually blocks.
	 *
	 * Note, any IRQs that arrive before IsRunning=0 will not cause an
	 * incomplete IPI vmexit on the source; kvm_vcpu_check_block() handles
	 * this by checking vIRR one last time before blocking.  The memory
	 * barrier implicit in set_current_state orders writing IsRunning=0
	 * before reading the vIRR.  The processor needs a matching memory
	 * barrier on interrupt delivery between writing IRR and reading
	 * IsRunning; the lack of this barrier might be the cause of errata #1235).
	 *
	 * Clear IsRunning=0 even if guest IRQs are disabled, i.e. even if KVM
	 * doesn't need to detect events for scheduling purposes.  The doorbell
	 * used to signal running vCPUs cannot be blocked, i.e. will perturb the
	 * CPU and cause noisy neighbor problems if the VM is sending interrupts
	 * to the vCPU while it's scheduled out.
	 */
	__avic_vcpu_put(vcpu, AVIC_START_BLOCKING);
}

void avic_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
	if (!kvm_vcpu_apicv_active(vcpu))
		return;

	avic_vcpu_load(vcpu, vcpu->cpu);
}

static bool __init avic_want_avic_enabled(void)
{
	/*
	 * In "auto" mode, enable AVIC by default for Zen4+ if x2AVIC is
	 * supported (to avoid enabling partial support by default, and because
	 * x2AVIC should be supported by all Zen4+ CPUs).  Explicitly check for
	 * family 0x19 and later (Zen5+), as the kernel's synthetic ZenX flags
	 * aren't inclusive of previous generations, i.e. the kernel will set
	 * at most one ZenX feature flag.
	 */
	if (avic == AVIC_AUTO_MODE)
		avic = boot_cpu_has(X86_FEATURE_X2AVIC) &&
		       (boot_cpu_data.x86 > 0x19 || cpu_feature_enabled(X86_FEATURE_ZEN4));

	if (!avic || !npt_enabled)
		return false;

	/* AVIC is a prerequisite for x2AVIC. */
	if (!boot_cpu_has(X86_FEATURE_AVIC) && !force_avic) {
		if (boot_cpu_has(X86_FEATURE_X2AVIC))
			pr_warn(FW_BUG "Cannot enable x2AVIC, AVIC is unsupported\n");
		return false;
	}

	if (cc_platform_has(CC_ATTR_HOST_SEV_SNP) &&
	    !boot_cpu_has(X86_FEATURE_HV_INUSE_WR_ALLOWED)) {
		pr_warn("AVIC disabled: missing HvInUseWrAllowed on SNP-enabled system\n");
		return false;
	}

	/*
	 * Print a scary message if AVIC is force enabled to make it abundantly
	 * clear that ignoring CPUID could have repercussions.  See Revision
	 * Guide for specific AMD processor for more details.
	 */
	if (!boot_cpu_has(X86_FEATURE_AVIC))
		pr_warn("AVIC unsupported in CPUID but force enabled, your system might crash and burn\n");

	return true;
}

/*
 * Note:
 * - The module param avic enable both xAPIC and x2APIC mode.
 * - Hypervisor can support both xAVIC and x2AVIC in the same guest.
 * - The mode can be switched at run-time.
 */
bool __init avic_hardware_setup(void)
{
	avic = avic_want_avic_enabled();
	if (!avic)
		return false;

	pr_info("AVIC enabled\n");

	/* AVIC is a prerequisite for x2AVIC. */
	x2avic_enabled = boot_cpu_has(X86_FEATURE_X2AVIC);
	if (x2avic_enabled)
		pr_info("x2AVIC enabled\n");
	else
		svm_x86_ops.allow_apicv_in_x2apic_without_x2apic_virtualization = true;

	/*
	 * Disable IPI virtualization for AMD Family 17h CPUs (Zen1 and Zen2)
	 * due to erratum 1235, which results in missed VM-Exits on the sender
	 * and thus missed wake events for blocking vCPUs due to the CPU
	 * failing to see a software update to clear IsRunning.
	 */
	enable_ipiv = enable_ipiv && boot_cpu_data.x86 != 0x17;

	amd_iommu_register_ga_log_notifier(&avic_ga_log_notifier);

	return true;
}

void avic_hardware_unsetup(void)
{
	if (avic)
		amd_iommu_register_ga_log_notifier(NULL);
}

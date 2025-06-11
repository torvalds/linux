// SPDX-License-Identifier: GPL-2.0-only
/*
 * irq_comm.c: Common API for in kernel interrupt controller
 * Copyright (c) 2007, Intel Corporation.
 *
 * Authors:
 *   Yaozu (Eddie) Dong <Eddie.dong@intel.com>
 *
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kvm_host.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/rculist.h>

#include "hyperv.h"
#include "ioapic.h"
#include "irq.h"
#include "lapic.h"
#include "trace.h"
#include "x86.h"
#include "xen.h"

int kvm_irq_delivery_to_apic(struct kvm *kvm, struct kvm_lapic *src,
		struct kvm_lapic_irq *irq, struct dest_map *dest_map)
{
	int r = -1;
	struct kvm_vcpu *vcpu, *lowest = NULL;
	unsigned long i, dest_vcpu_bitmap[BITS_TO_LONGS(KVM_MAX_VCPUS)];
	unsigned int dest_vcpus = 0;

	if (kvm_irq_delivery_to_apic_fast(kvm, src, irq, &r, dest_map))
		return r;

	if (irq->dest_mode == APIC_DEST_PHYSICAL &&
	    irq->dest_id == 0xff && kvm_lowest_prio_delivery(irq)) {
		pr_info("apic: phys broadcast and lowest prio\n");
		irq->delivery_mode = APIC_DM_FIXED;
	}

	memset(dest_vcpu_bitmap, 0, sizeof(dest_vcpu_bitmap));

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!kvm_apic_present(vcpu))
			continue;

		if (!kvm_apic_match_dest(vcpu, src, irq->shorthand,
					irq->dest_id, irq->dest_mode))
			continue;

		if (!kvm_lowest_prio_delivery(irq)) {
			if (r < 0)
				r = 0;
			r += kvm_apic_set_irq(vcpu, irq, dest_map);
		} else if (kvm_apic_sw_enabled(vcpu->arch.apic)) {
			if (!kvm_vector_hashing_enabled()) {
				if (!lowest)
					lowest = vcpu;
				else if (kvm_apic_compare_prio(vcpu, lowest) < 0)
					lowest = vcpu;
			} else {
				__set_bit(i, dest_vcpu_bitmap);
				dest_vcpus++;
			}
		}
	}

	if (dest_vcpus != 0) {
		int idx = kvm_vector_to_index(irq->vector, dest_vcpus,
					dest_vcpu_bitmap, KVM_MAX_VCPUS);

		lowest = kvm_get_vcpu(kvm, idx);
	}

	if (lowest)
		r = kvm_apic_set_irq(lowest, irq, dest_map);

	return r;
}

void kvm_set_msi_irq(struct kvm *kvm, struct kvm_kernel_irq_routing_entry *e,
		     struct kvm_lapic_irq *irq)
{
	struct msi_msg msg = { .address_lo = e->msi.address_lo,
			       .address_hi = e->msi.address_hi,
			       .data = e->msi.data };

	trace_kvm_msi_set_irq(msg.address_lo | (kvm->arch.x2apic_format ?
			      (u64)msg.address_hi << 32 : 0), msg.data);

	irq->dest_id = x86_msi_msg_get_destid(&msg, kvm->arch.x2apic_format);
	irq->vector = msg.arch_data.vector;
	irq->dest_mode = kvm_lapic_irq_dest_mode(msg.arch_addr_lo.dest_mode_logical);
	irq->trig_mode = msg.arch_data.is_level;
	irq->delivery_mode = msg.arch_data.delivery_mode << 8;
	irq->msi_redir_hint = msg.arch_addr_lo.redirect_hint;
	irq->level = 1;
	irq->shorthand = APIC_DEST_NOSHORT;
}
EXPORT_SYMBOL_GPL(kvm_set_msi_irq);

static inline bool kvm_msi_route_invalid(struct kvm *kvm,
		struct kvm_kernel_irq_routing_entry *e)
{
	return kvm->arch.x2apic_format && (e->msi.address_hi & 0xff);
}

int kvm_set_msi(struct kvm_kernel_irq_routing_entry *e,
		struct kvm *kvm, int irq_source_id, int level, bool line_status)
{
	struct kvm_lapic_irq irq;

	if (kvm_msi_route_invalid(kvm, e))
		return -EINVAL;

	if (!level)
		return -1;

	kvm_set_msi_irq(kvm, e, &irq);

	return kvm_irq_delivery_to_apic(kvm, NULL, &irq, NULL);
}

int kvm_arch_set_irq_inatomic(struct kvm_kernel_irq_routing_entry *e,
			      struct kvm *kvm, int irq_source_id, int level,
			      bool line_status)
{
	struct kvm_lapic_irq irq;
	int r;

	switch (e->type) {
#ifdef CONFIG_KVM_HYPERV
	case KVM_IRQ_ROUTING_HV_SINT:
		return kvm_hv_synic_set_irq(e, kvm, irq_source_id, level,
					    line_status);
#endif

	case KVM_IRQ_ROUTING_MSI:
		if (kvm_msi_route_invalid(kvm, e))
			return -EINVAL;

		kvm_set_msi_irq(kvm, e, &irq);

		if (kvm_irq_delivery_to_apic_fast(kvm, NULL, &irq, &r, NULL))
			return r;
		break;

#ifdef CONFIG_KVM_XEN
	case KVM_IRQ_ROUTING_XEN_EVTCHN:
		if (!level)
			return -1;

		return kvm_xen_set_evtchn_fast(&e->xen_evtchn, kvm);
#endif
	default:
		break;
	}

	return -EWOULDBLOCK;
}

bool kvm_arch_can_set_irq_routing(struct kvm *kvm)
{
	return irqchip_in_kernel(kvm);
}

int kvm_set_routing_entry(struct kvm *kvm,
			  struct kvm_kernel_irq_routing_entry *e,
			  const struct kvm_irq_routing_entry *ue)
{
	/* We can't check irqchip_in_kernel() here as some callers are
	 * currently initializing the irqchip. Other callers should therefore
	 * check kvm_arch_can_set_irq_routing() before calling this function.
	 */
	switch (ue->type) {
#ifdef CONFIG_KVM_IOAPIC
	case KVM_IRQ_ROUTING_IRQCHIP:
		if (irqchip_split(kvm))
			return -EINVAL;
		e->irqchip.pin = ue->u.irqchip.pin;
		switch (ue->u.irqchip.irqchip) {
		case KVM_IRQCHIP_PIC_SLAVE:
			e->irqchip.pin += PIC_NUM_PINS / 2;
			fallthrough;
		case KVM_IRQCHIP_PIC_MASTER:
			if (ue->u.irqchip.pin >= PIC_NUM_PINS / 2)
				return -EINVAL;
			e->set = kvm_pic_set_irq;
			break;
		case KVM_IRQCHIP_IOAPIC:
			if (ue->u.irqchip.pin >= KVM_IOAPIC_NUM_PINS)
				return -EINVAL;
			e->set = kvm_ioapic_set_irq;
			break;
		default:
			return -EINVAL;
		}
		e->irqchip.irqchip = ue->u.irqchip.irqchip;
		break;
#endif
	case KVM_IRQ_ROUTING_MSI:
		e->set = kvm_set_msi;
		e->msi.address_lo = ue->u.msi.address_lo;
		e->msi.address_hi = ue->u.msi.address_hi;
		e->msi.data = ue->u.msi.data;

		if (kvm_msi_route_invalid(kvm, e))
			return -EINVAL;
		break;
#ifdef CONFIG_KVM_HYPERV
	case KVM_IRQ_ROUTING_HV_SINT:
		e->set = kvm_hv_synic_set_irq;
		e->hv_sint.vcpu = ue->u.hv_sint.vcpu;
		e->hv_sint.sint = ue->u.hv_sint.sint;
		break;
#endif
#ifdef CONFIG_KVM_XEN
	case KVM_IRQ_ROUTING_XEN_EVTCHN:
		return kvm_xen_setup_evtchn(kvm, e, ue);
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

bool kvm_intr_is_single_vcpu(struct kvm *kvm, struct kvm_lapic_irq *irq,
			     struct kvm_vcpu **dest_vcpu)
{
	int r = 0;
	unsigned long i;
	struct kvm_vcpu *vcpu;

	if (kvm_intr_is_single_vcpu_fast(kvm, irq, dest_vcpu))
		return true;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!kvm_apic_present(vcpu))
			continue;

		if (!kvm_apic_match_dest(vcpu, NULL, irq->shorthand,
					irq->dest_id, irq->dest_mode))
			continue;

		if (++r == 2)
			return false;

		*dest_vcpu = vcpu;
	}

	return r == 1;
}
EXPORT_SYMBOL_GPL(kvm_intr_is_single_vcpu);

void kvm_scan_ioapic_irq(struct kvm_vcpu *vcpu, u32 dest_id, u16 dest_mode,
			 u8 vector, unsigned long *ioapic_handled_vectors)
{
	/*
	 * Intercept EOI if the vCPU is the target of the new IRQ routing, or
	 * the vCPU has a pending IRQ from the old routing, i.e. if the vCPU
	 * may receive a level-triggered IRQ in the future, or already received
	 * level-triggered IRQ.  The EOI needs to be intercepted and forwarded
	 * to I/O APIC emulation so that the IRQ can be de-asserted.
	 */
	if (kvm_apic_match_dest(vcpu, NULL, APIC_DEST_NOSHORT, dest_id, dest_mode)) {
		__set_bit(vector, ioapic_handled_vectors);
	} else if (kvm_apic_pending_eoi(vcpu, vector)) {
		__set_bit(vector, ioapic_handled_vectors);

		/*
		 * Track the highest pending EOI for which the vCPU is NOT the
		 * target in the new routing.  Only the EOI for the IRQ that is
		 * in-flight (for the old routing) needs to be intercepted, any
		 * future IRQs that arrive on this vCPU will be coincidental to
		 * the level-triggered routing and don't need to be intercepted.
		 */
		if ((int)vector > vcpu->arch.highest_stale_pending_ioapic_eoi)
			vcpu->arch.highest_stale_pending_ioapic_eoi = vector;
	}
}

void kvm_scan_ioapic_routes(struct kvm_vcpu *vcpu,
			    ulong *ioapic_handled_vectors)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_kernel_irq_routing_entry *entry;
	struct kvm_irq_routing_table *table;
	u32 i, nr_ioapic_pins;
	int idx;

	idx = srcu_read_lock(&kvm->irq_srcu);
	table = srcu_dereference(kvm->irq_routing, &kvm->irq_srcu);
	nr_ioapic_pins = min_t(u32, table->nr_rt_entries,
			       kvm->arch.nr_reserved_ioapic_pins);
	for (i = 0; i < nr_ioapic_pins; ++i) {
		hlist_for_each_entry(entry, &table->map[i], link) {
			struct kvm_lapic_irq irq;

			if (entry->type != KVM_IRQ_ROUTING_MSI)
				continue;

			kvm_set_msi_irq(vcpu->kvm, entry, &irq);

			if (!irq.trig_mode)
				continue;

			kvm_scan_ioapic_irq(vcpu, irq.dest_id, irq.dest_mode,
					    irq.vector, ioapic_handled_vectors);
		}
	}
	srcu_read_unlock(&kvm->irq_srcu, idx);
}

void kvm_arch_irq_routing_update(struct kvm *kvm)
{
#ifdef CONFIG_KVM_HYPERV
	kvm_hv_irq_routing_update(kvm);
#endif

	if (irqchip_split(kvm))
		kvm_make_scan_ioapic_request(kvm);
}

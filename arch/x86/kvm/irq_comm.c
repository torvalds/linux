/*
 * irq_comm.c: Common API for in kernel interrupt controller
 * Copyright (c) 2007, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 * Authors:
 *   Yaozu (Eddie) Dong <Eddie.dong@intel.com>
 *
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 */

#include <linux/kvm_host.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <trace/events/kvm.h>

#include <asm/msidef.h>

#include "irq.h"

#include "ioapic.h"

#include "lapic.h"

#include "hyperv.h"
#include "x86.h"

static int kvm_set_pic_irq(struct kvm_kernel_irq_routing_entry *e,
			   struct kvm *kvm, int irq_source_id, int level,
			   bool line_status)
{
	struct kvm_pic *pic = pic_irqchip(kvm);
	return kvm_pic_set_irq(pic, e->irqchip.pin, irq_source_id, level);
}

static int kvm_set_ioapic_irq(struct kvm_kernel_irq_routing_entry *e,
			      struct kvm *kvm, int irq_source_id, int level,
			      bool line_status)
{
	struct kvm_ioapic *ioapic = kvm->arch.vioapic;
	return kvm_ioapic_set_irq(ioapic, e->irqchip.pin, irq_source_id, level,
				line_status);
}

int kvm_irq_delivery_to_apic(struct kvm *kvm, struct kvm_lapic *src,
		struct kvm_lapic_irq *irq, struct dest_map *dest_map)
{
	int i, r = -1;
	struct kvm_vcpu *vcpu, *lowest = NULL;
	unsigned long dest_vcpu_bitmap[BITS_TO_LONGS(KVM_MAX_VCPUS)];
	unsigned int dest_vcpus = 0;

	if (irq->dest_mode == 0 && irq->dest_id == 0xff &&
			kvm_lowest_prio_delivery(irq)) {
		printk(KERN_INFO "kvm: apic: phys broadcast and lowest prio\n");
		irq->delivery_mode = APIC_DM_FIXED;
	}

	if (kvm_irq_delivery_to_apic_fast(kvm, src, irq, &r, dest_map))
		return r;

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
		} else if (kvm_lapic_enabled(vcpu)) {
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

void kvm_set_msi_irq(struct kvm_kernel_irq_routing_entry *e,
		     struct kvm_lapic_irq *irq)
{
	trace_kvm_msi_set_irq(e->msi.address_lo, e->msi.data);

	irq->dest_id = (e->msi.address_lo &
			MSI_ADDR_DEST_ID_MASK) >> MSI_ADDR_DEST_ID_SHIFT;
	irq->vector = (e->msi.data &
			MSI_DATA_VECTOR_MASK) >> MSI_DATA_VECTOR_SHIFT;
	irq->dest_mode = (1 << MSI_ADDR_DEST_MODE_SHIFT) & e->msi.address_lo;
	irq->trig_mode = (1 << MSI_DATA_TRIGGER_SHIFT) & e->msi.data;
	irq->delivery_mode = e->msi.data & 0x700;
	irq->msi_redir_hint = ((e->msi.address_lo
		& MSI_ADDR_REDIRECTION_LOWPRI) > 0);
	irq->level = 1;
	irq->shorthand = 0;
}
EXPORT_SYMBOL_GPL(kvm_set_msi_irq);

int kvm_set_msi(struct kvm_kernel_irq_routing_entry *e,
		struct kvm *kvm, int irq_source_id, int level, bool line_status)
{
	struct kvm_lapic_irq irq;

	if (!level)
		return -1;

	kvm_set_msi_irq(e, &irq);

	return kvm_irq_delivery_to_apic(kvm, NULL, &irq, NULL);
}


int kvm_arch_set_irq_inatomic(struct kvm_kernel_irq_routing_entry *e,
			      struct kvm *kvm, int irq_source_id, int level,
			      bool line_status)
{
	struct kvm_lapic_irq irq;
	int r;

	if (unlikely(e->type != KVM_IRQ_ROUTING_MSI))
		return -EWOULDBLOCK;

	kvm_set_msi_irq(e, &irq);

	if (kvm_irq_delivery_to_apic_fast(kvm, NULL, &irq, &r, NULL))
		return r;
	else
		return -EWOULDBLOCK;
}

int kvm_request_irq_source_id(struct kvm *kvm)
{
	unsigned long *bitmap = &kvm->arch.irq_sources_bitmap;
	int irq_source_id;

	mutex_lock(&kvm->irq_lock);
	irq_source_id = find_first_zero_bit(bitmap, BITS_PER_LONG);

	if (irq_source_id >= BITS_PER_LONG) {
		printk(KERN_WARNING "kvm: exhaust allocatable IRQ sources!\n");
		irq_source_id = -EFAULT;
		goto unlock;
	}

	ASSERT(irq_source_id != KVM_USERSPACE_IRQ_SOURCE_ID);
	ASSERT(irq_source_id != KVM_IRQFD_RESAMPLE_IRQ_SOURCE_ID);
	set_bit(irq_source_id, bitmap);
unlock:
	mutex_unlock(&kvm->irq_lock);

	return irq_source_id;
}

void kvm_free_irq_source_id(struct kvm *kvm, int irq_source_id)
{
	ASSERT(irq_source_id != KVM_USERSPACE_IRQ_SOURCE_ID);
	ASSERT(irq_source_id != KVM_IRQFD_RESAMPLE_IRQ_SOURCE_ID);

	mutex_lock(&kvm->irq_lock);
	if (irq_source_id < 0 ||
	    irq_source_id >= BITS_PER_LONG) {
		printk(KERN_ERR "kvm: IRQ source ID out of range!\n");
		goto unlock;
	}
	clear_bit(irq_source_id, &kvm->arch.irq_sources_bitmap);
	if (!ioapic_in_kernel(kvm))
		goto unlock;

	kvm_ioapic_clear_all(kvm->arch.vioapic, irq_source_id);
	kvm_pic_clear_all(pic_irqchip(kvm), irq_source_id);
unlock:
	mutex_unlock(&kvm->irq_lock);
}

void kvm_register_irq_mask_notifier(struct kvm *kvm, int irq,
				    struct kvm_irq_mask_notifier *kimn)
{
	mutex_lock(&kvm->irq_lock);
	kimn->irq = irq;
	hlist_add_head_rcu(&kimn->link, &kvm->arch.mask_notifier_list);
	mutex_unlock(&kvm->irq_lock);
}

void kvm_unregister_irq_mask_notifier(struct kvm *kvm, int irq,
				      struct kvm_irq_mask_notifier *kimn)
{
	mutex_lock(&kvm->irq_lock);
	hlist_del_rcu(&kimn->link);
	mutex_unlock(&kvm->irq_lock);
	synchronize_srcu(&kvm->irq_srcu);
}

void kvm_fire_mask_notifiers(struct kvm *kvm, unsigned irqchip, unsigned pin,
			     bool mask)
{
	struct kvm_irq_mask_notifier *kimn;
	int idx, gsi;

	idx = srcu_read_lock(&kvm->irq_srcu);
	gsi = kvm_irq_map_chip_pin(kvm, irqchip, pin);
	if (gsi != -1)
		hlist_for_each_entry_rcu(kimn, &kvm->arch.mask_notifier_list, link)
			if (kimn->irq == gsi)
				kimn->func(kimn, mask);
	srcu_read_unlock(&kvm->irq_srcu, idx);
}

static int kvm_hv_set_sint(struct kvm_kernel_irq_routing_entry *e,
		    struct kvm *kvm, int irq_source_id, int level,
		    bool line_status)
{
	if (!level)
		return -1;

	return kvm_hv_synic_set_irq(kvm, e->hv_sint.vcpu, e->hv_sint.sint);
}

int kvm_set_routing_entry(struct kvm_kernel_irq_routing_entry *e,
			  const struct kvm_irq_routing_entry *ue)
{
	int r = -EINVAL;
	int delta;
	unsigned max_pin;

	switch (ue->type) {
	case KVM_IRQ_ROUTING_IRQCHIP:
		delta = 0;
		switch (ue->u.irqchip.irqchip) {
		case KVM_IRQCHIP_PIC_MASTER:
			e->set = kvm_set_pic_irq;
			max_pin = PIC_NUM_PINS;
			break;
		case KVM_IRQCHIP_PIC_SLAVE:
			e->set = kvm_set_pic_irq;
			max_pin = PIC_NUM_PINS;
			delta = 8;
			break;
		case KVM_IRQCHIP_IOAPIC:
			max_pin = KVM_IOAPIC_NUM_PINS;
			e->set = kvm_set_ioapic_irq;
			break;
		default:
			goto out;
		}
		e->irqchip.irqchip = ue->u.irqchip.irqchip;
		e->irqchip.pin = ue->u.irqchip.pin + delta;
		if (e->irqchip.pin >= max_pin)
			goto out;
		break;
	case KVM_IRQ_ROUTING_MSI:
		e->set = kvm_set_msi;
		e->msi.address_lo = ue->u.msi.address_lo;
		e->msi.address_hi = ue->u.msi.address_hi;
		e->msi.data = ue->u.msi.data;
		break;
	case KVM_IRQ_ROUTING_HV_SINT:
		e->set = kvm_hv_set_sint;
		e->hv_sint.vcpu = ue->u.hv_sint.vcpu;
		e->hv_sint.sint = ue->u.hv_sint.sint;
		break;
	default:
		goto out;
	}

	r = 0;
out:
	return r;
}

bool kvm_intr_is_single_vcpu(struct kvm *kvm, struct kvm_lapic_irq *irq,
			     struct kvm_vcpu **dest_vcpu)
{
	int i, r = 0;
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

#define IOAPIC_ROUTING_ENTRY(irq) \
	{ .gsi = irq, .type = KVM_IRQ_ROUTING_IRQCHIP,	\
	  .u.irqchip = { .irqchip = KVM_IRQCHIP_IOAPIC, .pin = (irq) } }
#define ROUTING_ENTRY1(irq) IOAPIC_ROUTING_ENTRY(irq)

#define PIC_ROUTING_ENTRY(irq) \
	{ .gsi = irq, .type = KVM_IRQ_ROUTING_IRQCHIP,	\
	  .u.irqchip = { .irqchip = SELECT_PIC(irq), .pin = (irq) % 8 } }
#define ROUTING_ENTRY2(irq) \
	IOAPIC_ROUTING_ENTRY(irq), PIC_ROUTING_ENTRY(irq)

static const struct kvm_irq_routing_entry default_routing[] = {
	ROUTING_ENTRY2(0), ROUTING_ENTRY2(1),
	ROUTING_ENTRY2(2), ROUTING_ENTRY2(3),
	ROUTING_ENTRY2(4), ROUTING_ENTRY2(5),
	ROUTING_ENTRY2(6), ROUTING_ENTRY2(7),
	ROUTING_ENTRY2(8), ROUTING_ENTRY2(9),
	ROUTING_ENTRY2(10), ROUTING_ENTRY2(11),
	ROUTING_ENTRY2(12), ROUTING_ENTRY2(13),
	ROUTING_ENTRY2(14), ROUTING_ENTRY2(15),
	ROUTING_ENTRY1(16), ROUTING_ENTRY1(17),
	ROUTING_ENTRY1(18), ROUTING_ENTRY1(19),
	ROUTING_ENTRY1(20), ROUTING_ENTRY1(21),
	ROUTING_ENTRY1(22), ROUTING_ENTRY1(23),
};

int kvm_setup_default_irq_routing(struct kvm *kvm)
{
	return kvm_set_irq_routing(kvm, default_routing,
				   ARRAY_SIZE(default_routing), 0);
}

static const struct kvm_irq_routing_entry empty_routing[] = {};

int kvm_setup_empty_irq_routing(struct kvm *kvm)
{
	return kvm_set_irq_routing(kvm, empty_routing, 0, 0);
}

void kvm_arch_post_irq_routing_update(struct kvm *kvm)
{
	if (ioapic_in_kernel(kvm) || !irqchip_in_kernel(kvm))
		return;
	kvm_make_scan_ioapic_request(kvm);
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
			u32 dest_id, dest_mode;
			bool level;

			if (entry->type != KVM_IRQ_ROUTING_MSI)
				continue;
			dest_id = (entry->msi.address_lo >> 12) & 0xff;
			dest_mode = (entry->msi.address_lo >> 2) & 0x1;
			level = entry->msi.data & MSI_DATA_TRIGGER_LEVEL;
			if (level && kvm_apic_match_dest(vcpu, NULL, 0,
						dest_id, dest_mode)) {
				u32 vector = entry->msi.data & 0xff;

				__set_bit(vector,
					  ioapic_handled_vectors);
			}
		}
	}
	srcu_read_unlock(&kvm->irq_srcu, idx);
}

int kvm_arch_set_irq(struct kvm_kernel_irq_routing_entry *irq, struct kvm *kvm,
		     int irq_source_id, int level, bool line_status)
{
	switch (irq->type) {
	case KVM_IRQ_ROUTING_HV_SINT:
		return kvm_hv_set_sint(irq, kvm, irq_source_id, level,
				       line_status);
	default:
		return -EWOULDBLOCK;
	}
}

void kvm_arch_irq_routing_update(struct kvm *kvm)
{
	kvm_hv_irq_routing_update(kvm);
}

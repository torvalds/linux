// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 *  Copyright (C) 2001  MandrakeSoft S.A.
 *  Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 *    MandrakeSoft S.A.
 *    43, rue d'Aboukir
 *    75002 Paris - France
 *    http://www.linux-mandrake.com/
 *    http://www.mandrakesoft.com/
 *
 *  Yunhong Jiang <yunhong.jiang@intel.com>
 *  Yaozu (Eddie) Dong <eddie.dong@intel.com>
 *  Based on Xen 3.1 code.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/smp.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/nospec.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/current.h>

#include "ioapic.h"
#include "lapic.h"
#include "irq.h"
#include "trace.h"

static int ioapic_service(struct kvm_ioapic *vioapic, int irq,
		bool line_status);

static void kvm_ioapic_update_eoi_one(struct kvm_vcpu *vcpu,
				      struct kvm_ioapic *ioapic,
				      int trigger_mode,
				      int pin);

static unsigned long ioapic_read_indirect(struct kvm_ioapic *ioapic)
{
	unsigned long result = 0;

	switch (ioapic->ioregsel) {
	case IOAPIC_REG_VERSION:
		result = ((((IOAPIC_NUM_PINS - 1) & 0xff) << 16)
			  | (IOAPIC_VERSION_ID & 0xff));
		break;

	case IOAPIC_REG_APIC_ID:
	case IOAPIC_REG_ARB_ID:
		result = ((ioapic->id & 0xf) << 24);
		break;

	default:
		{
			u32 redir_index = (ioapic->ioregsel - 0x10) >> 1;
			u64 redir_content = ~0ULL;

			if (redir_index < IOAPIC_NUM_PINS) {
				u32 index = array_index_nospec(
					redir_index, IOAPIC_NUM_PINS);

				redir_content = ioapic->redirtbl[index].bits;
			}

			result = (ioapic->ioregsel & 0x1) ?
			    (redir_content >> 32) & 0xffffffff :
			    redir_content & 0xffffffff;
			break;
		}
	}

	return result;
}

static void rtc_irq_eoi_tracking_reset(struct kvm_ioapic *ioapic)
{
	ioapic->rtc_status.pending_eoi = 0;
	bitmap_zero(ioapic->rtc_status.dest_map.map, KVM_MAX_VCPU_IDS);
}

static void kvm_rtc_eoi_tracking_restore_all(struct kvm_ioapic *ioapic);

static void rtc_status_pending_eoi_check_valid(struct kvm_ioapic *ioapic)
{
	if (WARN_ON(ioapic->rtc_status.pending_eoi < 0))
		kvm_rtc_eoi_tracking_restore_all(ioapic);
}

static void __rtc_irq_eoi_tracking_restore_one(struct kvm_vcpu *vcpu)
{
	bool new_val, old_val;
	struct kvm_ioapic *ioapic = vcpu->kvm->arch.vioapic;
	struct dest_map *dest_map = &ioapic->rtc_status.dest_map;
	union kvm_ioapic_redirect_entry *e;

	e = &ioapic->redirtbl[RTC_GSI];
	if (!kvm_apic_match_dest(vcpu, NULL, APIC_DEST_NOSHORT,
				 e->fields.dest_id,
				 kvm_lapic_irq_dest_mode(!!e->fields.dest_mode)))
		return;

	new_val = kvm_apic_pending_eoi(vcpu, e->fields.vector);
	old_val = test_bit(vcpu->vcpu_id, dest_map->map);

	if (new_val == old_val)
		return;

	if (new_val) {
		__set_bit(vcpu->vcpu_id, dest_map->map);
		dest_map->vectors[vcpu->vcpu_id] = e->fields.vector;
		ioapic->rtc_status.pending_eoi++;
	} else {
		__clear_bit(vcpu->vcpu_id, dest_map->map);
		ioapic->rtc_status.pending_eoi--;
		rtc_status_pending_eoi_check_valid(ioapic);
	}
}

void kvm_rtc_eoi_tracking_restore_one(struct kvm_vcpu *vcpu)
{
	struct kvm_ioapic *ioapic = vcpu->kvm->arch.vioapic;

	spin_lock(&ioapic->lock);
	__rtc_irq_eoi_tracking_restore_one(vcpu);
	spin_unlock(&ioapic->lock);
}

static void kvm_rtc_eoi_tracking_restore_all(struct kvm_ioapic *ioapic)
{
	struct kvm_vcpu *vcpu;
	unsigned long i;

	if (RTC_GSI >= IOAPIC_NUM_PINS)
		return;

	rtc_irq_eoi_tracking_reset(ioapic);
	kvm_for_each_vcpu(i, vcpu, ioapic->kvm)
	    __rtc_irq_eoi_tracking_restore_one(vcpu);
}

static void rtc_irq_eoi(struct kvm_ioapic *ioapic, struct kvm_vcpu *vcpu,
			int vector)
{
	struct dest_map *dest_map = &ioapic->rtc_status.dest_map;

	/* RTC special handling */
	if (test_bit(vcpu->vcpu_id, dest_map->map) &&
	    (vector == dest_map->vectors[vcpu->vcpu_id]) &&
	    (test_and_clear_bit(vcpu->vcpu_id,
				ioapic->rtc_status.dest_map.map))) {
		--ioapic->rtc_status.pending_eoi;
		rtc_status_pending_eoi_check_valid(ioapic);
	}
}

static bool rtc_irq_check_coalesced(struct kvm_ioapic *ioapic)
{
	if (ioapic->rtc_status.pending_eoi > 0)
		return true; /* coalesced */

	return false;
}

static void ioapic_lazy_update_eoi(struct kvm_ioapic *ioapic, int irq)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;
	union kvm_ioapic_redirect_entry *entry = &ioapic->redirtbl[irq];

	kvm_for_each_vcpu(i, vcpu, ioapic->kvm) {
		if (!kvm_apic_match_dest(vcpu, NULL, APIC_DEST_NOSHORT,
					 entry->fields.dest_id,
					 entry->fields.dest_mode) ||
		    kvm_apic_pending_eoi(vcpu, entry->fields.vector))
			continue;

		/*
		 * If no longer has pending EOI in LAPICs, update
		 * EOI for this vector.
		 */
		rtc_irq_eoi(ioapic, vcpu, entry->fields.vector);
		break;
	}
}

static int ioapic_set_irq(struct kvm_ioapic *ioapic, unsigned int irq,
		int irq_level, bool line_status)
{
	union kvm_ioapic_redirect_entry entry;
	u32 mask = 1 << irq;
	u32 old_irr;
	int edge, ret;

	entry = ioapic->redirtbl[irq];
	edge = (entry.fields.trig_mode == IOAPIC_EDGE_TRIG);

	if (!irq_level) {
		ioapic->irr &= ~mask;
		ret = 1;
		goto out;
	}

	/*
	 * AMD SVM AVIC accelerate EOI write iff the interrupt is edge
	 * triggered, in which case the in-kernel IOAPIC will not be able
	 * to receive the EOI.  In this case, we do a lazy update of the
	 * pending EOI when trying to set IOAPIC irq.
	 */
	if (edge && kvm_apicv_activated(ioapic->kvm))
		ioapic_lazy_update_eoi(ioapic, irq);

	/*
	 * Return 0 for coalesced interrupts; for edge-triggered interrupts,
	 * this only happens if a previous edge has not been delivered due
	 * to masking.  For level interrupts, the remote_irr field tells
	 * us if the interrupt is waiting for an EOI.
	 *
	 * RTC is special: it is edge-triggered, but userspace likes to know
	 * if it has been already ack-ed via EOI because coalesced RTC
	 * interrupts lead to time drift in Windows guests.  So we track
	 * EOI manually for the RTC interrupt.
	 */
	if (irq == RTC_GSI && line_status &&
		rtc_irq_check_coalesced(ioapic)) {
		ret = 0;
		goto out;
	}

	old_irr = ioapic->irr;
	ioapic->irr |= mask;
	if (edge) {
		ioapic->irr_delivered &= ~mask;
		if (old_irr == ioapic->irr) {
			ret = 0;
			goto out;
		}
	}

	ret = ioapic_service(ioapic, irq, line_status);

out:
	trace_kvm_ioapic_set_irq(entry.bits, irq, ret == 0);
	return ret;
}

static void kvm_ioapic_inject_all(struct kvm_ioapic *ioapic, unsigned long irr)
{
	u32 idx;

	rtc_irq_eoi_tracking_reset(ioapic);
	for_each_set_bit(idx, &irr, IOAPIC_NUM_PINS)
		ioapic_set_irq(ioapic, idx, 1, true);

	kvm_rtc_eoi_tracking_restore_all(ioapic);
}


void kvm_ioapic_scan_entry(struct kvm_vcpu *vcpu, ulong *ioapic_handled_vectors)
{
	struct kvm_ioapic *ioapic = vcpu->kvm->arch.vioapic;
	struct dest_map *dest_map = &ioapic->rtc_status.dest_map;
	union kvm_ioapic_redirect_entry *e;
	int index;

	spin_lock(&ioapic->lock);

	/* Make sure we see any missing RTC EOI */
	if (test_bit(vcpu->vcpu_id, dest_map->map))
		__set_bit(dest_map->vectors[vcpu->vcpu_id],
			  ioapic_handled_vectors);

	for (index = 0; index < IOAPIC_NUM_PINS; index++) {
		e = &ioapic->redirtbl[index];
		if (e->fields.trig_mode == IOAPIC_LEVEL_TRIG ||
		    kvm_irq_has_notifier(ioapic->kvm, KVM_IRQCHIP_IOAPIC, index) ||
		    index == RTC_GSI) {
			u16 dm = kvm_lapic_irq_dest_mode(!!e->fields.dest_mode);

			kvm_scan_ioapic_irq(vcpu, e->fields.dest_id, dm,
					    e->fields.vector, ioapic_handled_vectors);
		}
	}
	spin_unlock(&ioapic->lock);
}

void kvm_arch_post_irq_ack_notifier_list_update(struct kvm *kvm)
{
	if (!ioapic_in_kernel(kvm))
		return;
	kvm_make_scan_ioapic_request(kvm);
}

void kvm_register_irq_mask_notifier(struct kvm *kvm, int irq,
				    struct kvm_irq_mask_notifier *kimn)
{
	struct kvm_ioapic *ioapic = kvm->arch.vioapic;

	mutex_lock(&kvm->irq_lock);
	kimn->irq = irq;
	hlist_add_head_rcu(&kimn->link, &ioapic->mask_notifier_list);
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
	struct kvm_ioapic *ioapic = kvm->arch.vioapic;
	struct kvm_irq_mask_notifier *kimn;
	int idx, gsi;

	idx = srcu_read_lock(&kvm->irq_srcu);
	gsi = kvm_irq_map_chip_pin(kvm, irqchip, pin);
	if (gsi != -1)
		hlist_for_each_entry_rcu(kimn, &ioapic->mask_notifier_list, link)
			if (kimn->irq == gsi)
				kimn->func(kimn, mask);
	srcu_read_unlock(&kvm->irq_srcu, idx);
}

static void ioapic_write_indirect(struct kvm_ioapic *ioapic, u32 val)
{
	unsigned index;
	bool mask_before, mask_after;
	union kvm_ioapic_redirect_entry *e;
	int old_remote_irr, old_delivery_status, old_dest_id, old_dest_mode;
	DECLARE_BITMAP(vcpu_bitmap, KVM_MAX_VCPUS);

	switch (ioapic->ioregsel) {
	case IOAPIC_REG_VERSION:
		/* Writes are ignored. */
		break;

	case IOAPIC_REG_APIC_ID:
		ioapic->id = (val >> 24) & 0xf;
		break;

	case IOAPIC_REG_ARB_ID:
		break;

	default:
		index = (ioapic->ioregsel - 0x10) >> 1;

		if (index >= IOAPIC_NUM_PINS)
			return;
		index = array_index_nospec(index, IOAPIC_NUM_PINS);
		e = &ioapic->redirtbl[index];
		mask_before = e->fields.mask;
		/* Preserve read-only fields */
		old_remote_irr = e->fields.remote_irr;
		old_delivery_status = e->fields.delivery_status;
		old_dest_id = e->fields.dest_id;
		old_dest_mode = e->fields.dest_mode;
		if (ioapic->ioregsel & 1) {
			e->bits &= 0xffffffff;
			e->bits |= (u64) val << 32;
		} else {
			e->bits &= ~0xffffffffULL;
			e->bits |= (u32) val;
		}
		e->fields.remote_irr = old_remote_irr;
		e->fields.delivery_status = old_delivery_status;

		/*
		 * Some OSes (Linux, Xen) assume that Remote IRR bit will
		 * be cleared by IOAPIC hardware when the entry is configured
		 * as edge-triggered. This behavior is used to simulate an
		 * explicit EOI on IOAPICs that don't have the EOI register.
		 */
		if (e->fields.trig_mode == IOAPIC_EDGE_TRIG)
			e->fields.remote_irr = 0;

		mask_after = e->fields.mask;
		if (mask_before != mask_after)
			kvm_fire_mask_notifiers(ioapic->kvm, KVM_IRQCHIP_IOAPIC, index, mask_after);
		if (e->fields.trig_mode == IOAPIC_LEVEL_TRIG &&
		    ioapic->irr & (1 << index) && !e->fields.mask && !e->fields.remote_irr) {
			/*
			 * Pending status in irr may be outdated: the IRQ line may have
			 * already been deasserted by a device while the IRQ was masked.
			 * This occurs, for instance, if the interrupt is handled in a
			 * Linux guest as a oneshot interrupt (IRQF_ONESHOT). In this
			 * case the guest acknowledges the interrupt to the device in
			 * its threaded irq handler, i.e. after the EOI but before
			 * unmasking, so at the time of unmasking the IRQ line is
			 * already down but our pending irr bit is still set. In such
			 * cases, injecting this pending interrupt to the guest is
			 * buggy: the guest will receive an extra unwanted interrupt.
			 *
			 * So we need to check here if the IRQ is actually still pending.
			 * As we are generally not able to probe the IRQ line status
			 * directly, we do it through irqfd resampler. Namely, we clear
			 * the pending status and notify the resampler that this interrupt
			 * is done, without actually injecting it into the guest. If the
			 * IRQ line is actually already deasserted, we are done. If it is
			 * still asserted, a new interrupt will be shortly triggered
			 * through irqfd and injected into the guest.
			 *
			 * If, however, it's not possible to resample (no irqfd resampler
			 * registered for this irq), then unconditionally inject this
			 * pending interrupt into the guest, so the guest will not miss
			 * an interrupt, although may get an extra unwanted interrupt.
			 */
			if (kvm_notify_irqfd_resampler(ioapic->kvm, KVM_IRQCHIP_IOAPIC, index))
				ioapic->irr &= ~(1 << index);
			else
				ioapic_service(ioapic, index, false);
		}
		if (e->fields.delivery_mode == APIC_DM_FIXED) {
			struct kvm_lapic_irq irq;

			irq.vector = e->fields.vector;
			irq.delivery_mode = e->fields.delivery_mode << 8;
			irq.dest_mode =
			    kvm_lapic_irq_dest_mode(!!e->fields.dest_mode);
			irq.level = false;
			irq.trig_mode = e->fields.trig_mode;
			irq.shorthand = APIC_DEST_NOSHORT;
			irq.dest_id = e->fields.dest_id;
			irq.msi_redir_hint = false;
			bitmap_zero(vcpu_bitmap, KVM_MAX_VCPUS);
			kvm_bitmap_or_dest_vcpus(ioapic->kvm, &irq,
						 vcpu_bitmap);
			if (old_dest_mode != e->fields.dest_mode ||
			    old_dest_id != e->fields.dest_id) {
				/*
				 * Update vcpu_bitmap with vcpus specified in
				 * the previous request as well. This is done to
				 * keep ioapic_handled_vectors synchronized.
				 */
				irq.dest_id = old_dest_id;
				irq.dest_mode =
				    kvm_lapic_irq_dest_mode(
					!!e->fields.dest_mode);
				kvm_bitmap_or_dest_vcpus(ioapic->kvm, &irq,
							 vcpu_bitmap);
			}
			kvm_make_scan_ioapic_request_mask(ioapic->kvm,
							  vcpu_bitmap);
		} else {
			kvm_make_scan_ioapic_request(ioapic->kvm);
		}
		break;
	}
}

static int ioapic_service(struct kvm_ioapic *ioapic, int irq, bool line_status)
{
	union kvm_ioapic_redirect_entry *entry = &ioapic->redirtbl[irq];
	struct kvm_lapic_irq irqe;
	int ret;

	if (entry->fields.mask ||
	    (entry->fields.trig_mode == IOAPIC_LEVEL_TRIG &&
	    entry->fields.remote_irr))
		return -1;

	irqe.dest_id = entry->fields.dest_id;
	irqe.vector = entry->fields.vector;
	irqe.dest_mode = kvm_lapic_irq_dest_mode(!!entry->fields.dest_mode);
	irqe.trig_mode = entry->fields.trig_mode;
	irqe.delivery_mode = entry->fields.delivery_mode << 8;
	irqe.level = 1;
	irqe.shorthand = APIC_DEST_NOSHORT;
	irqe.msi_redir_hint = false;

	if (irqe.trig_mode == IOAPIC_EDGE_TRIG)
		ioapic->irr_delivered |= 1 << irq;

	if (irq == RTC_GSI && line_status) {
		/*
		 * pending_eoi cannot ever become negative (see
		 * rtc_status_pending_eoi_check_valid) and the caller
		 * ensures that it is only called if it is >= zero, namely
		 * if rtc_irq_check_coalesced returns false).
		 */
		BUG_ON(ioapic->rtc_status.pending_eoi != 0);
		ret = kvm_irq_delivery_to_apic(ioapic->kvm, NULL, &irqe,
					       &ioapic->rtc_status.dest_map);
		ioapic->rtc_status.pending_eoi = (ret < 0 ? 0 : ret);
	} else
		ret = kvm_irq_delivery_to_apic(ioapic->kvm, NULL, &irqe, NULL);

	if (ret && irqe.trig_mode == IOAPIC_LEVEL_TRIG)
		entry->fields.remote_irr = 1;

	return ret;
}

int kvm_ioapic_set_irq(struct kvm_kernel_irq_routing_entry *e, struct kvm *kvm,
		       int irq_source_id, int level, bool line_status)
{
	struct kvm_ioapic *ioapic = kvm->arch.vioapic;
	int irq = e->irqchip.pin;
	int ret, irq_level;

	BUG_ON(irq < 0 || irq >= IOAPIC_NUM_PINS);

	spin_lock(&ioapic->lock);
	irq_level = __kvm_irq_line_state(&ioapic->irq_states[irq],
					 irq_source_id, level);
	ret = ioapic_set_irq(ioapic, irq, irq_level, line_status);

	spin_unlock(&ioapic->lock);

	return ret;
}

static void kvm_ioapic_eoi_inject_work(struct work_struct *work)
{
	int i;
	struct kvm_ioapic *ioapic = container_of(work, struct kvm_ioapic,
						 eoi_inject.work);
	spin_lock(&ioapic->lock);
	for (i = 0; i < IOAPIC_NUM_PINS; i++) {
		union kvm_ioapic_redirect_entry *ent = &ioapic->redirtbl[i];

		if (ent->fields.trig_mode != IOAPIC_LEVEL_TRIG)
			continue;

		if (ioapic->irr & (1 << i) && !ent->fields.remote_irr)
			ioapic_service(ioapic, i, false);
	}
	spin_unlock(&ioapic->lock);
}

#define IOAPIC_SUCCESSIVE_IRQ_MAX_COUNT 10000
static void kvm_ioapic_update_eoi_one(struct kvm_vcpu *vcpu,
				      struct kvm_ioapic *ioapic,
				      int trigger_mode,
				      int pin)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	union kvm_ioapic_redirect_entry *ent = &ioapic->redirtbl[pin];

	/*
	 * We are dropping lock while calling ack notifiers because ack
	 * notifier callbacks for assigned devices call into IOAPIC
	 * recursively. Since remote_irr is cleared only after call
	 * to notifiers if the same vector will be delivered while lock
	 * is dropped it will be put into irr and will be delivered
	 * after ack notifier returns.
	 */
	spin_unlock(&ioapic->lock);
	kvm_notify_acked_irq(ioapic->kvm, KVM_IRQCHIP_IOAPIC, pin);
	spin_lock(&ioapic->lock);

	if (trigger_mode != IOAPIC_LEVEL_TRIG ||
	    kvm_lapic_get_reg(apic, APIC_SPIV) & APIC_SPIV_DIRECTED_EOI)
		return;

	ASSERT(ent->fields.trig_mode == IOAPIC_LEVEL_TRIG);
	ent->fields.remote_irr = 0;
	if (!ent->fields.mask && (ioapic->irr & (1 << pin))) {
		++ioapic->irq_eoi[pin];
		if (ioapic->irq_eoi[pin] == IOAPIC_SUCCESSIVE_IRQ_MAX_COUNT) {
			/*
			 * Real hardware does not deliver the interrupt
			 * immediately during eoi broadcast, and this
			 * lets a buggy guest make slow progress
			 * even if it does not correctly handle a
			 * level-triggered interrupt.  Emulate this
			 * behavior if we detect an interrupt storm.
			 */
			schedule_delayed_work(&ioapic->eoi_inject, HZ / 100);
			ioapic->irq_eoi[pin] = 0;
			trace_kvm_ioapic_delayed_eoi_inj(ent->bits);
		} else {
			ioapic_service(ioapic, pin, false);
		}
	} else {
		ioapic->irq_eoi[pin] = 0;
	}
}

void kvm_ioapic_update_eoi(struct kvm_vcpu *vcpu, int vector, int trigger_mode)
{
	int i;
	struct kvm_ioapic *ioapic = vcpu->kvm->arch.vioapic;

	spin_lock(&ioapic->lock);
	rtc_irq_eoi(ioapic, vcpu, vector);
	for (i = 0; i < IOAPIC_NUM_PINS; i++) {
		union kvm_ioapic_redirect_entry *ent = &ioapic->redirtbl[i];

		if (ent->fields.vector != vector)
			continue;
		kvm_ioapic_update_eoi_one(vcpu, ioapic, trigger_mode, i);
	}
	spin_unlock(&ioapic->lock);
}

static inline struct kvm_ioapic *to_ioapic(struct kvm_io_device *dev)
{
	return container_of(dev, struct kvm_ioapic, dev);
}

static inline int ioapic_in_range(struct kvm_ioapic *ioapic, gpa_t addr)
{
	return ((addr >= ioapic->base_address &&
		 (addr < ioapic->base_address + IOAPIC_MEM_LENGTH)));
}

static int ioapic_mmio_read(struct kvm_vcpu *vcpu, struct kvm_io_device *this,
				gpa_t addr, int len, void *val)
{
	struct kvm_ioapic *ioapic = to_ioapic(this);
	u32 result;
	if (!ioapic_in_range(ioapic, addr))
		return -EOPNOTSUPP;

	ASSERT(!(addr & 0xf));	/* check alignment */

	addr &= 0xff;
	spin_lock(&ioapic->lock);
	switch (addr) {
	case IOAPIC_REG_SELECT:
		result = ioapic->ioregsel;
		break;

	case IOAPIC_REG_WINDOW:
		result = ioapic_read_indirect(ioapic);
		break;

	default:
		result = 0;
		break;
	}
	spin_unlock(&ioapic->lock);

	switch (len) {
	case 8:
		*(u64 *) val = result;
		break;
	case 1:
	case 2:
	case 4:
		memcpy(val, (char *)&result, len);
		break;
	default:
		printk(KERN_WARNING "ioapic: wrong length %d\n", len);
	}
	return 0;
}

static int ioapic_mmio_write(struct kvm_vcpu *vcpu, struct kvm_io_device *this,
				 gpa_t addr, int len, const void *val)
{
	struct kvm_ioapic *ioapic = to_ioapic(this);
	u32 data;
	if (!ioapic_in_range(ioapic, addr))
		return -EOPNOTSUPP;

	ASSERT(!(addr & 0xf));	/* check alignment */

	switch (len) {
	case 8:
	case 4:
		data = *(u32 *) val;
		break;
	case 2:
		data = *(u16 *) val;
		break;
	case 1:
		data = *(u8  *) val;
		break;
	default:
		printk(KERN_WARNING "ioapic: Unsupported size %d\n", len);
		return 0;
	}

	addr &= 0xff;
	spin_lock(&ioapic->lock);
	switch (addr) {
	case IOAPIC_REG_SELECT:
		ioapic->ioregsel = data & 0xFF; /* 8-bit register */
		break;

	case IOAPIC_REG_WINDOW:
		ioapic_write_indirect(ioapic, data);
		break;

	default:
		break;
	}
	spin_unlock(&ioapic->lock);
	return 0;
}

static void kvm_ioapic_reset(struct kvm_ioapic *ioapic)
{
	int i;

	cancel_delayed_work_sync(&ioapic->eoi_inject);
	for (i = 0; i < IOAPIC_NUM_PINS; i++)
		ioapic->redirtbl[i].fields.mask = 1;
	ioapic->base_address = IOAPIC_DEFAULT_BASE_ADDRESS;
	ioapic->ioregsel = 0;
	ioapic->irr = 0;
	ioapic->irr_delivered = 0;
	ioapic->id = 0;
	memset(ioapic->irq_eoi, 0x00, sizeof(ioapic->irq_eoi));
	rtc_irq_eoi_tracking_reset(ioapic);
}

static const struct kvm_io_device_ops ioapic_mmio_ops = {
	.read     = ioapic_mmio_read,
	.write    = ioapic_mmio_write,
};

int kvm_ioapic_init(struct kvm *kvm)
{
	struct kvm_ioapic *ioapic;
	int ret;

	ioapic = kzalloc(sizeof(struct kvm_ioapic), GFP_KERNEL_ACCOUNT);
	if (!ioapic)
		return -ENOMEM;
	spin_lock_init(&ioapic->lock);
	INIT_DELAYED_WORK(&ioapic->eoi_inject, kvm_ioapic_eoi_inject_work);
	INIT_HLIST_HEAD(&ioapic->mask_notifier_list);
	kvm->arch.vioapic = ioapic;
	kvm_ioapic_reset(ioapic);
	kvm_iodevice_init(&ioapic->dev, &ioapic_mmio_ops);
	ioapic->kvm = kvm;
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, ioapic->base_address,
				      IOAPIC_MEM_LENGTH, &ioapic->dev);
	mutex_unlock(&kvm->slots_lock);
	if (ret < 0) {
		kvm->arch.vioapic = NULL;
		kfree(ioapic);
	}

	return ret;
}

void kvm_ioapic_destroy(struct kvm *kvm)
{
	struct kvm_ioapic *ioapic = kvm->arch.vioapic;

	if (!ioapic)
		return;

	cancel_delayed_work_sync(&ioapic->eoi_inject);
	mutex_lock(&kvm->slots_lock);
	kvm_io_bus_unregister_dev(kvm, KVM_MMIO_BUS, &ioapic->dev);
	mutex_unlock(&kvm->slots_lock);
	kvm->arch.vioapic = NULL;
	kfree(ioapic);
}

void kvm_get_ioapic(struct kvm *kvm, struct kvm_ioapic_state *state)
{
	struct kvm_ioapic *ioapic = kvm->arch.vioapic;

	spin_lock(&ioapic->lock);
	memcpy(state, ioapic, sizeof(struct kvm_ioapic_state));
	state->irr &= ~ioapic->irr_delivered;
	spin_unlock(&ioapic->lock);
}

void kvm_set_ioapic(struct kvm *kvm, struct kvm_ioapic_state *state)
{
	struct kvm_ioapic *ioapic = kvm->arch.vioapic;

	spin_lock(&ioapic->lock);
	memcpy(ioapic, state, sizeof(struct kvm_ioapic_state));
	ioapic->irr = 0;
	ioapic->irr_delivered = 0;
	kvm_make_scan_ioapic_request(kvm);
	kvm_ioapic_inject_all(ioapic, state->irr);
	spin_unlock(&ioapic->lock);
}

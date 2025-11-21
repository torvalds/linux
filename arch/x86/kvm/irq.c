// SPDX-License-Identifier: GPL-2.0-only
/*
 * irq.c: API for in kernel interrupt controller
 * Copyright (c) 2007, Intel Corporation.
 * Copyright 2009 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Yaozu (Eddie) Dong <Eddie.dong@intel.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/kvm_host.h>
#include <linux/kvm_irqfd.h>

#include "hyperv.h"
#include "ioapic.h"
#include "irq.h"
#include "trace.h"
#include "x86.h"
#include "xen.h"

/*
 * check if there are pending timer events
 * to be processed.
 */
int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	int r = 0;

	if (lapic_in_kernel(vcpu))
		r = apic_has_pending_timer(vcpu);
	if (kvm_xen_timer_enabled(vcpu))
		r += kvm_xen_has_pending_timer(vcpu);

	return r;
}

/*
 * check if there is a pending userspace external interrupt
 */
static int pending_userspace_extint(struct kvm_vcpu *v)
{
	return v->arch.pending_external_vector != -1;
}

static int get_userspace_extint(struct kvm_vcpu *vcpu)
{
	int vector = vcpu->arch.pending_external_vector;

	vcpu->arch.pending_external_vector = -1;
	return vector;
}

/*
 * check if there is pending interrupt from
 * non-APIC source without intack.
 */
int kvm_cpu_has_extint(struct kvm_vcpu *v)
{
	/*
	 * FIXME: interrupt.injected represents an interrupt whose
	 * side-effects have already been applied (e.g. bit from IRR
	 * already moved to ISR). Therefore, it is incorrect to rely
	 * on interrupt.injected to know if there is a pending
	 * interrupt in the user-mode LAPIC.
	 * This leads to nVMX/nSVM not be able to distinguish
	 * if it should exit from L2 to L1 on EXTERNAL_INTERRUPT on
	 * pending interrupt or should re-inject an injected
	 * interrupt.
	 */
	if (!lapic_in_kernel(v))
		return v->arch.interrupt.injected;

	if (kvm_xen_has_interrupt(v))
		return 1;

	if (!kvm_apic_accept_pic_intr(v))
		return 0;

#ifdef CONFIG_KVM_IOAPIC
	if (pic_in_kernel(v->kvm))
		return v->kvm->arch.vpic->output;
#endif

	WARN_ON_ONCE(!irqchip_split(v->kvm));
	return pending_userspace_extint(v);
}

/*
 * check if there is injectable interrupt:
 * when virtual interrupt delivery enabled,
 * interrupt from apic will handled by hardware,
 * we don't need to check it here.
 */
int kvm_cpu_has_injectable_intr(struct kvm_vcpu *v)
{
	if (kvm_cpu_has_extint(v))
		return 1;

	if (!is_guest_mode(v) && kvm_vcpu_apicv_active(v))
		return 0;

	return kvm_apic_has_interrupt(v) != -1; /* LAPIC */
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_cpu_has_injectable_intr);

/*
 * check if there is pending interrupt without
 * intack.
 */
int kvm_cpu_has_interrupt(struct kvm_vcpu *v)
{
	if (kvm_cpu_has_extint(v))
		return 1;

	if (lapic_in_kernel(v) && v->arch.apic->guest_apic_protected)
		return kvm_x86_call(protected_apic_has_interrupt)(v);

	return kvm_apic_has_interrupt(v) != -1;	/* LAPIC */
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_cpu_has_interrupt);

/*
 * Read pending interrupt(from non-APIC source)
 * vector and intack.
 */
int kvm_cpu_get_extint(struct kvm_vcpu *v)
{
	if (!kvm_cpu_has_extint(v)) {
		WARN_ON(!lapic_in_kernel(v));
		return -1;
	}

	if (!lapic_in_kernel(v))
		return v->arch.interrupt.nr;

#ifdef CONFIG_KVM_XEN
	if (kvm_xen_has_interrupt(v))
		return v->kvm->arch.xen.upcall_vector;
#endif

#ifdef CONFIG_KVM_IOAPIC
	if (pic_in_kernel(v->kvm))
		return kvm_pic_read_irq(v->kvm); /* PIC */
#endif

	WARN_ON_ONCE(!irqchip_split(v->kvm));
	return get_userspace_extint(v);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_cpu_get_extint);

/*
 * Read pending interrupt vector and intack.
 */
int kvm_cpu_get_interrupt(struct kvm_vcpu *v)
{
	int vector = kvm_cpu_get_extint(v);
	if (vector != -1)
		return vector;			/* PIC */

	vector = kvm_apic_has_interrupt(v);	/* APIC */
	if (vector != -1)
		kvm_apic_ack_interrupt(v, vector);

	return vector;
}

void kvm_inject_pending_timer_irqs(struct kvm_vcpu *vcpu)
{
	if (lapic_in_kernel(vcpu))
		kvm_inject_apic_timer_irqs(vcpu);
	if (kvm_xen_timer_enabled(vcpu))
		kvm_xen_inject_timer_irqs(vcpu);
}

void __kvm_migrate_timers(struct kvm_vcpu *vcpu)
{
	__kvm_migrate_apic_timer(vcpu);
#ifdef CONFIG_KVM_IOAPIC
	__kvm_migrate_pit_timer(vcpu);
#endif
	kvm_x86_call(migrate_timers)(vcpu);
}

bool kvm_arch_irqfd_allowed(struct kvm *kvm, struct kvm_irqfd *args)
{
	bool resample = args->flags & KVM_IRQFD_FLAG_RESAMPLE;

	return resample ? irqchip_full(kvm) : irqchip_in_kernel(kvm);
}

bool kvm_arch_irqchip_in_kernel(struct kvm *kvm)
{
	return irqchip_in_kernel(kvm);
}

static void kvm_msi_to_lapic_irq(struct kvm *kvm,
				 struct kvm_kernel_irq_routing_entry *e,
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

	kvm_msi_to_lapic_irq(kvm, e, &irq);

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

		kvm_msi_to_lapic_irq(kvm, e, &irq);

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

int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irq_event,
			bool line_status)
{
	if (!irqchip_in_kernel(kvm))
		return -ENXIO;

	irq_event->status = kvm_set_irq(kvm, KVM_USERSPACE_IRQ_SOURCE_ID,
					irq_event->irq, irq_event->level,
					line_status);
	return 0;
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

			kvm_msi_to_lapic_irq(vcpu->kvm, entry, &irq);

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

static int kvm_pi_update_irte(struct kvm_kernel_irqfd *irqfd,
			      struct kvm_kernel_irq_routing_entry *entry)
{
	unsigned int host_irq = irqfd->producer->irq;
	struct kvm *kvm = irqfd->kvm;
	struct kvm_vcpu *vcpu = NULL;
	struct kvm_lapic_irq irq;
	int r;

	if (WARN_ON_ONCE(!irqchip_in_kernel(kvm) || !kvm_arch_has_irq_bypass()))
		return -EINVAL;

	if (entry && entry->type == KVM_IRQ_ROUTING_MSI) {
		kvm_msi_to_lapic_irq(kvm, entry, &irq);

		/*
		 * Force remapped mode if hardware doesn't support posting the
		 * virtual interrupt to a vCPU.  Only IRQs are postable (NMIs,
		 * SMIs, etc. are not), and neither AMD nor Intel IOMMUs support
		 * posting multicast/broadcast IRQs.  If the interrupt can't be
		 * posted, the device MSI needs to be routed to the host so that
		 * the guest's desired interrupt can be synthesized by KVM.
		 *
		 * This means that KVM can only post lowest-priority interrupts
		 * if they have a single CPU as the destination, e.g. only if
		 * the guest has affined the interrupt to a single vCPU.
		 */
		if (!kvm_intr_is_single_vcpu(kvm, &irq, &vcpu) ||
		    !kvm_irq_is_postable(&irq))
			vcpu = NULL;
	}

	if (!irqfd->irq_bypass_vcpu && !vcpu)
		return 0;

	r = kvm_x86_call(pi_update_irte)(irqfd, irqfd->kvm, host_irq, irqfd->gsi,
					 vcpu, irq.vector);
	if (r) {
		WARN_ON_ONCE(irqfd->irq_bypass_vcpu && !vcpu);
		irqfd->irq_bypass_vcpu = NULL;
		return r;
	}

	irqfd->irq_bypass_vcpu = vcpu;

	trace_kvm_pi_irte_update(host_irq, vcpu, irqfd->gsi, irq.vector, !!vcpu);
	return 0;
}

int kvm_arch_irq_bypass_add_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);
	struct kvm *kvm = irqfd->kvm;
	int ret = 0;

	spin_lock_irq(&kvm->irqfds.lock);
	irqfd->producer = prod;

	if (!kvm->arch.nr_possible_bypass_irqs++)
		kvm_x86_call(pi_start_bypass)(kvm);

	if (irqfd->irq_entry.type == KVM_IRQ_ROUTING_MSI) {
		ret = kvm_pi_update_irte(irqfd, &irqfd->irq_entry);
		if (ret)
			kvm->arch.nr_possible_bypass_irqs--;
	}
	spin_unlock_irq(&kvm->irqfds.lock);

	return ret;
}

void kvm_arch_irq_bypass_del_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);
	struct kvm *kvm = irqfd->kvm;
	int ret;

	WARN_ON(irqfd->producer != prod);

	/*
	 * If the producer of an IRQ that is currently being posted to a vCPU
	 * is unregistered, change the associated IRTE back to remapped mode as
	 * the IRQ has been released (or repurposed) by the device driver, i.e.
	 * KVM must relinquish control of the IRTE.
	 */
	spin_lock_irq(&kvm->irqfds.lock);

	if (irqfd->irq_entry.type == KVM_IRQ_ROUTING_MSI) {
		ret = kvm_pi_update_irte(irqfd, NULL);
		if (ret)
			pr_info("irq bypass consumer (eventfd %p) unregistration fails: %d\n",
				irqfd->consumer.eventfd, ret);
	}
	irqfd->producer = NULL;

	kvm->arch.nr_possible_bypass_irqs--;

	spin_unlock_irq(&kvm->irqfds.lock);
}

void kvm_arch_update_irqfd_routing(struct kvm_kernel_irqfd *irqfd,
				   struct kvm_kernel_irq_routing_entry *old,
				   struct kvm_kernel_irq_routing_entry *new)
{
	if (new->type != KVM_IRQ_ROUTING_MSI &&
	    old->type != KVM_IRQ_ROUTING_MSI)
		return;

	if (old->type == KVM_IRQ_ROUTING_MSI &&
	    new->type == KVM_IRQ_ROUTING_MSI &&
	    !memcmp(&old->msi, &new->msi, sizeof(new->msi)))
		return;

	kvm_pi_update_irte(irqfd, new);
}

#ifdef CONFIG_KVM_IOAPIC
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

int kvm_setup_default_ioapic_and_pic_routing(struct kvm *kvm)
{
	return kvm_set_irq_routing(kvm, default_routing,
				   ARRAY_SIZE(default_routing), 0);
}

int kvm_vm_ioctl_get_irqchip(struct kvm *kvm, struct kvm_irqchip *chip)
{
	struct kvm_pic *pic = kvm->arch.vpic;
	int r;

	r = 0;
	switch (chip->chip_id) {
	case KVM_IRQCHIP_PIC_MASTER:
		memcpy(&chip->chip.pic, &pic->pics[0],
			sizeof(struct kvm_pic_state));
		break;
	case KVM_IRQCHIP_PIC_SLAVE:
		memcpy(&chip->chip.pic, &pic->pics[1],
			sizeof(struct kvm_pic_state));
		break;
	case KVM_IRQCHIP_IOAPIC:
		kvm_get_ioapic(kvm, &chip->chip.ioapic);
		break;
	default:
		r = -EINVAL;
		break;
	}
	return r;
}

int kvm_vm_ioctl_set_irqchip(struct kvm *kvm, struct kvm_irqchip *chip)
{
	struct kvm_pic *pic = kvm->arch.vpic;
	int r;

	r = 0;
	switch (chip->chip_id) {
	case KVM_IRQCHIP_PIC_MASTER:
		spin_lock(&pic->lock);
		memcpy(&pic->pics[0], &chip->chip.pic,
			sizeof(struct kvm_pic_state));
		spin_unlock(&pic->lock);
		break;
	case KVM_IRQCHIP_PIC_SLAVE:
		spin_lock(&pic->lock);
		memcpy(&pic->pics[1], &chip->chip.pic,
			sizeof(struct kvm_pic_state));
		spin_unlock(&pic->lock);
		break;
	case KVM_IRQCHIP_IOAPIC:
		kvm_set_ioapic(kvm, &chip->chip.ioapic);
		break;
	default:
		r = -EINVAL;
		break;
	}
	kvm_pic_update_irq(pic);
	return r;
}
#endif

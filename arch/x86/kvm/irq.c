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

#include "ioapic.h"
#include "irq.h"
#include "i8254.h"
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

	if (pic_in_kernel(v->kvm))
		return v->kvm->arch.vpic->output;

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
EXPORT_SYMBOL_GPL(kvm_cpu_has_injectable_intr);

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
EXPORT_SYMBOL_GPL(kvm_cpu_has_interrupt);

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

	if (pic_in_kernel(v->kvm))
		return kvm_pic_read_irq(v->kvm); /* PIC */

	WARN_ON_ONCE(!irqchip_split(v->kvm));
	return get_userspace_extint(v);
}
EXPORT_SYMBOL_GPL(kvm_cpu_get_extint);

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
	__kvm_migrate_pit_timer(vcpu);
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

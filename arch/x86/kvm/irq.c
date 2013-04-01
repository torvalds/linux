/*
 * irq.c: API for in kernel interrupt controller
 * Copyright (c) 2007, Intel Corporation.
 * Copyright 2009 Red Hat, Inc. and/or its affiliates.
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
 */

#include <linux/module.h>
#include <linux/kvm_host.h>

#include "irq.h"
#include "i8254.h"
#include "x86.h"

/*
 * check if there are pending timer events
 * to be processed.
 */
int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return apic_has_pending_timer(vcpu);
}
EXPORT_SYMBOL(kvm_cpu_has_pending_timer);

/*
 * check if there is pending interrupt from
 * non-APIC source without intack.
 */
static int kvm_cpu_has_extint(struct kvm_vcpu *v)
{
	if (kvm_apic_accept_pic_intr(v))
		return pic_irqchip(v->kvm)->output;	/* PIC */
	else
		return 0;
}

/*
 * check if there is injectable interrupt:
 * when virtual interrupt delivery enabled,
 * interrupt from apic will handled by hardware,
 * we don't need to check it here.
 */
int kvm_cpu_has_injectable_intr(struct kvm_vcpu *v)
{
	if (!irqchip_in_kernel(v->kvm))
		return v->arch.interrupt.pending;

	if (kvm_cpu_has_extint(v))
		return 1;

	if (kvm_apic_vid_enabled(v->kvm))
		return 0;

	return kvm_apic_has_interrupt(v) != -1; /* LAPIC */
}

/*
 * check if there is pending interrupt without
 * intack.
 */
int kvm_cpu_has_interrupt(struct kvm_vcpu *v)
{
	if (!irqchip_in_kernel(v->kvm))
		return v->arch.interrupt.pending;

	if (kvm_cpu_has_extint(v))
		return 1;

	return kvm_apic_has_interrupt(v) != -1;	/* LAPIC */
}
EXPORT_SYMBOL_GPL(kvm_cpu_has_interrupt);

/*
 * Read pending interrupt(from non-APIC source)
 * vector and intack.
 */
static int kvm_cpu_get_extint(struct kvm_vcpu *v)
{
	if (kvm_cpu_has_extint(v))
		return kvm_pic_read_irq(v->kvm); /* PIC */
	return -1;
}

/*
 * Read pending interrupt vector and intack.
 */
int kvm_cpu_get_interrupt(struct kvm_vcpu *v)
{
	int vector;

	if (!irqchip_in_kernel(v->kvm))
		return v->arch.interrupt.nr;

	vector = kvm_cpu_get_extint(v);

	if (kvm_apic_vid_enabled(v->kvm) || vector != -1)
		return vector;			/* PIC */

	return kvm_get_apic_interrupt(v);	/* APIC */
}

void kvm_inject_pending_timer_irqs(struct kvm_vcpu *vcpu)
{
	kvm_inject_apic_timer_irqs(vcpu);
	/* TODO: PIT, RTC etc. */
}
EXPORT_SYMBOL_GPL(kvm_inject_pending_timer_irqs);

void __kvm_migrate_timers(struct kvm_vcpu *vcpu)
{
	__kvm_migrate_apic_timer(vcpu);
	__kvm_migrate_pit_timer(vcpu);
}

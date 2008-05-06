/*
 * irq.c: API for in kernel interrupt controller
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
 */

#include <linux/module.h>
#include <linux/kvm_host.h>

#include "irq.h"
#include "i8254.h"

/*
 * check if there are pending timer events
 * to be processed.
 */
int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	int ret;

	ret = pit_has_pending_timer(vcpu);
	ret |= apic_has_pending_timer(vcpu);

	return ret;
}
EXPORT_SYMBOL(kvm_cpu_has_pending_timer);

/*
 * check if there is pending interrupt without
 * intack.
 */
int kvm_cpu_has_interrupt(struct kvm_vcpu *v)
{
	struct kvm_pic *s;

	if (kvm_apic_has_interrupt(v) == -1) {	/* LAPIC */
		if (kvm_apic_accept_pic_intr(v)) {
			s = pic_irqchip(v->kvm);	/* PIC */
			return s->output;
		} else
			return 0;
	}
	return 1;
}
EXPORT_SYMBOL_GPL(kvm_cpu_has_interrupt);

/*
 * Read pending interrupt vector and intack.
 */
int kvm_cpu_get_interrupt(struct kvm_vcpu *v)
{
	struct kvm_pic *s;
	int vector;

	vector = kvm_get_apic_interrupt(v);	/* APIC */
	if (vector == -1) {
		if (kvm_apic_accept_pic_intr(v)) {
			s = pic_irqchip(v->kvm);
			s->output = 0;		/* PIC */
			vector = kvm_pic_read_irq(s);
		}
	}
	return vector;
}
EXPORT_SYMBOL_GPL(kvm_cpu_get_interrupt);

void kvm_inject_pending_timer_irqs(struct kvm_vcpu *vcpu)
{
	kvm_inject_apic_timer_irqs(vcpu);
	kvm_inject_pit_timer_irqs(vcpu);
	/* TODO: PIT, RTC etc. */
}
EXPORT_SYMBOL_GPL(kvm_inject_pending_timer_irqs);

void kvm_timer_intr_post(struct kvm_vcpu *vcpu, int vec)
{
	kvm_apic_timer_intr_post(vcpu, vec);
	kvm_pit_timer_intr_post(vcpu, vec);
	/* TODO: PIT, RTC etc. */
}
EXPORT_SYMBOL_GPL(kvm_timer_intr_post);

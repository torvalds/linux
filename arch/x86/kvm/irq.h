/*
 * irq.h: in kernel interrupt controller related definitions
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

#ifndef __IRQ_H
#define __IRQ_H

#include <linux/mm_types.h>
#include <linux/hrtimer.h>
#include <linux/kvm_host.h>
#include <linux/spinlock.h>

#include <kvm/iodev.h>
#include "ioapic.h"
#include "lapic.h"

#define PIC_NUM_PINS 16
#define SELECT_PIC(irq) \
	((irq) < 8 ? KVM_IRQCHIP_PIC_MASTER : KVM_IRQCHIP_PIC_SLAVE)

struct kvm;
struct kvm_vcpu;

struct kvm_kpic_state {
	u8 last_irr;	/* edge detection */
	u8 irr;		/* interrupt request register */
	u8 imr;		/* interrupt mask register */
	u8 isr;		/* interrupt service register */
	u8 priority_add;	/* highest irq priority */
	u8 irq_base;
	u8 read_reg_select;
	u8 poll;
	u8 special_mask;
	u8 init_state;
	u8 auto_eoi;
	u8 rotate_on_auto_eoi;
	u8 special_fully_nested_mode;
	u8 init4;		/* true if 4 byte init */
	u8 elcr;		/* PIIX edge/trigger selection */
	u8 elcr_mask;
	u8 isr_ack;	/* interrupt ack detection */
	struct kvm_pic *pics_state;
};

struct kvm_pic {
	spinlock_t lock;
	bool wakeup_needed;
	unsigned pending_acks;
	struct kvm *kvm;
	struct kvm_kpic_state pics[2]; /* 0 is master pic, 1 is slave pic */
	int output;		/* intr from master PIC */
	struct kvm_io_device dev_master;
	struct kvm_io_device dev_slave;
	struct kvm_io_device dev_eclr;
	void (*ack_notifier)(void *opaque, int irq);
	unsigned long irq_states[PIC_NUM_PINS];
};

struct kvm_pic *kvm_create_pic(struct kvm *kvm);
void kvm_destroy_pic(struct kvm_pic *vpic);
int kvm_pic_read_irq(struct kvm *kvm);
void kvm_pic_update_irq(struct kvm_pic *s);

static inline struct kvm_pic *pic_irqchip(struct kvm *kvm)
{
	return kvm->arch.vpic;
}

static inline int pic_in_kernel(struct kvm *kvm)
{
	int ret;

	ret = (pic_irqchip(kvm) != NULL);
	return ret;
}

static inline int irqchip_split(struct kvm *kvm)
{
	return kvm->arch.irqchip_split;
}

static inline int irqchip_in_kernel(struct kvm *kvm)
{
	struct kvm_pic *vpic = pic_irqchip(kvm);
	bool ret;

	ret = (vpic != NULL);
	ret |= irqchip_split(kvm);

	/* Read vpic before kvm->irq_routing.  */
	smp_rmb();
	return ret;
}

static inline int lapic_in_kernel(struct kvm_vcpu *vcpu)
{
	/* Same as irqchip_in_kernel(vcpu->kvm), but with less
	 * pointer chasing and no unnecessary memory barriers.
	 */
	return vcpu->arch.apic != NULL;
}

void kvm_pic_reset(struct kvm_kpic_state *s);

void kvm_inject_pending_timer_irqs(struct kvm_vcpu *vcpu);
void kvm_inject_apic_timer_irqs(struct kvm_vcpu *vcpu);
void kvm_apic_nmi_wd_deliver(struct kvm_vcpu *vcpu);
void __kvm_migrate_apic_timer(struct kvm_vcpu *vcpu);
void __kvm_migrate_pit_timer(struct kvm_vcpu *vcpu);
void __kvm_migrate_timers(struct kvm_vcpu *vcpu);

int apic_has_pending_timer(struct kvm_vcpu *vcpu);

#endif

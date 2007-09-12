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

#include "kvm.h"

typedef void irq_request_func(void *opaque, int level);

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
	struct kvm_pic *pics_state;
};

struct kvm_pic {
	struct kvm_kpic_state pics[2]; /* 0 is master pic, 1 is slave pic */
	irq_request_func *irq_request;
	void *irq_request_opaque;
	int output;		/* intr from master PIC */
	struct kvm_io_device dev;
};

struct kvm_pic *kvm_create_pic(struct kvm *kvm);
void kvm_pic_set_irq(void *opaque, int irq, int level);
int kvm_pic_read_irq(struct kvm_pic *s);
int kvm_cpu_get_interrupt(struct kvm_vcpu *v);
int kvm_cpu_has_interrupt(struct kvm_vcpu *v);

struct kvm_lapic {
	unsigned long base_address;
	struct kvm_io_device dev;
	struct {
		atomic_t pending;
		s64 period;	/* unit: ns */
		u32 divide_count;
		ktime_t last_update;
		struct hrtimer dev;
	} timer;
	struct kvm_vcpu *vcpu;
	struct page *regs_page;
	void *regs;
};

#ifdef DEBUG
#define ASSERT(x)  							\
do {									\
	if (!(x)) {							\
		printk(KERN_EMERG "assertion failed %s: %d: %s\n",	\
		       __FILE__, __LINE__, #x);				\
		BUG();							\
	}								\
} while (0)
#else
#define ASSERT(x) do { } while (0)
#endif

void kvm_vcpu_kick(struct kvm_vcpu *vcpu);
int kvm_apic_has_interrupt(struct kvm_vcpu *vcpu);
int kvm_get_apic_interrupt(struct kvm_vcpu *vcpu);
int kvm_create_lapic(struct kvm_vcpu *vcpu);
void kvm_free_apic(struct kvm_lapic *apic);
u64 kvm_lapic_get_cr8(struct kvm_vcpu *vcpu);
void kvm_lapic_set_tpr(struct kvm_vcpu *vcpu, unsigned long cr8);
void kvm_lapic_set_base(struct kvm_vcpu *vcpu, u64 value);
u64 kvm_get_apic_base(struct kvm_vcpu *vcpu);
void kvm_set_apic_base(struct kvm_vcpu *vcpu, u64 data);
void kvm_ioapic_update_eoi(struct kvm *kvm, int vector);

#endif

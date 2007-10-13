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
void kvm_pic_update_irq(struct kvm_pic *s);

#define IOAPIC_NUM_PINS  KVM_IOAPIC_NUM_PINS
#define IOAPIC_VERSION_ID 0x11	/* IOAPIC version */
#define IOAPIC_EDGE_TRIG  0
#define IOAPIC_LEVEL_TRIG 1

#define IOAPIC_DEFAULT_BASE_ADDRESS  0xfec00000
#define IOAPIC_MEM_LENGTH            0x100

/* Direct registers. */
#define IOAPIC_REG_SELECT  0x00
#define IOAPIC_REG_WINDOW  0x10
#define IOAPIC_REG_EOI     0x40	/* IA64 IOSAPIC only */

/* Indirect registers. */
#define IOAPIC_REG_APIC_ID 0x00	/* x86 IOAPIC only */
#define IOAPIC_REG_VERSION 0x01
#define IOAPIC_REG_ARB_ID  0x02	/* x86 IOAPIC only */

struct kvm_ioapic {
	u64 base_address;
	u32 ioregsel;
	u32 id;
	u32 irr;
	u32 pad;
	union ioapic_redir_entry {
		u64 bits;
		struct {
			u8 vector;
			u8 delivery_mode:3;
			u8 dest_mode:1;
			u8 delivery_status:1;
			u8 polarity:1;
			u8 remote_irr:1;
			u8 trig_mode:1;
			u8 mask:1;
			u8 reserve:7;
			u8 reserved[4];
			u8 dest_id;
		} fields;
	} redirtbl[IOAPIC_NUM_PINS];
	struct kvm_io_device dev;
	struct kvm *kvm;
};

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
int kvm_apic_accept_pic_intr(struct kvm_vcpu *vcpu);
int kvm_get_apic_interrupt(struct kvm_vcpu *vcpu);
int kvm_create_lapic(struct kvm_vcpu *vcpu);
void kvm_lapic_reset(struct kvm_vcpu *vcpu);
void kvm_free_apic(struct kvm_lapic *apic);
u64 kvm_lapic_get_cr8(struct kvm_vcpu *vcpu);
void kvm_lapic_set_tpr(struct kvm_vcpu *vcpu, unsigned long cr8);
void kvm_lapic_set_base(struct kvm_vcpu *vcpu, u64 value);
struct kvm_lapic *kvm_apic_round_robin(struct kvm *kvm, u8 vector,
				       unsigned long bitmap);
u64 kvm_get_apic_base(struct kvm_vcpu *vcpu);
void kvm_set_apic_base(struct kvm_vcpu *vcpu, u64 data);
int kvm_apic_match_physical_addr(struct kvm_lapic *apic, u16 dest);
void kvm_ioapic_update_eoi(struct kvm *kvm, int vector);
int kvm_apic_match_logical_addr(struct kvm_lapic *apic, u8 mda);
int kvm_apic_set_irq(struct kvm_lapic *apic, u8 vec, u8 trig);
void kvm_apic_post_state_restore(struct kvm_vcpu *vcpu);
int kvm_ioapic_init(struct kvm *kvm);
void kvm_ioapic_set_irq(struct kvm_ioapic *ioapic, int irq, int level);
int kvm_lapic_enabled(struct kvm_vcpu *vcpu);
int kvm_lapic_find_highest_irr(struct kvm_vcpu *vcpu);
void kvm_apic_timer_intr_post(struct kvm_vcpu *vcpu, int vec);
void kvm_timer_intr_post(struct kvm_vcpu *vcpu, int vec);
void kvm_inject_pending_timer_irqs(struct kvm_vcpu *vcpu);
void kvm_inject_apic_timer_irqs(struct kvm_vcpu *vcpu);
void kvm_migrate_apic_timer(struct kvm_vcpu *vcpu);

#endif

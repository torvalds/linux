#ifndef __KVM_X86_LAPIC_H
#define __KVM_X86_LAPIC_H

#include "iodev.h"

#include <linux/kvm_host.h>

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
	gpa_t vapic_addr;
	struct page *vapic_page;
};
int kvm_create_lapic(struct kvm_vcpu *vcpu);
void kvm_free_lapic(struct kvm_vcpu *vcpu);

int kvm_apic_has_interrupt(struct kvm_vcpu *vcpu);
int kvm_apic_accept_pic_intr(struct kvm_vcpu *vcpu);
int kvm_get_apic_interrupt(struct kvm_vcpu *vcpu);
void kvm_lapic_reset(struct kvm_vcpu *vcpu);
u64 kvm_lapic_get_cr8(struct kvm_vcpu *vcpu);
void kvm_lapic_set_tpr(struct kvm_vcpu *vcpu, unsigned long cr8);
void kvm_lapic_set_base(struct kvm_vcpu *vcpu, u64 value);
u64 kvm_lapic_get_base(struct kvm_vcpu *vcpu);

int kvm_apic_match_physical_addr(struct kvm_lapic *apic, u16 dest);
int kvm_apic_match_logical_addr(struct kvm_lapic *apic, u8 mda);
int kvm_apic_set_irq(struct kvm_vcpu *vcpu, u8 vec, u8 trig);

u64 kvm_get_apic_base(struct kvm_vcpu *vcpu);
void kvm_set_apic_base(struct kvm_vcpu *vcpu, u64 data);
void kvm_apic_post_state_restore(struct kvm_vcpu *vcpu);
int kvm_lapic_enabled(struct kvm_vcpu *vcpu);
int kvm_lapic_find_highest_irr(struct kvm_vcpu *vcpu);
void kvm_apic_timer_intr_post(struct kvm_vcpu *vcpu, int vec);

void kvm_lapic_set_vapic_addr(struct kvm_vcpu *vcpu, gpa_t vapic_addr);
void kvm_lapic_sync_from_vapic(struct kvm_vcpu *vcpu);
void kvm_lapic_sync_to_vapic(struct kvm_vcpu *vcpu);

#endif

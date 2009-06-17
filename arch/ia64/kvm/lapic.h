#ifndef __KVM_IA64_LAPIC_H
#define __KVM_IA64_LAPIC_H

#include <linux/kvm_host.h>

/*
 * vlsapic
 */
struct kvm_lapic{
	struct kvm_vcpu *vcpu;
	uint64_t insvc[4];
	uint64_t vhpi;
	uint8_t xtp;
	uint8_t pal_init_pending;
	uint8_t pad[2];
};

int kvm_create_lapic(struct kvm_vcpu *vcpu);
void kvm_free_lapic(struct kvm_vcpu *vcpu);

int kvm_apic_match_physical_addr(struct kvm_lapic *apic, u16 dest);
int kvm_apic_match_logical_addr(struct kvm_lapic *apic, u8 mda);
int kvm_apic_match_dest(struct kvm_vcpu *vcpu, struct kvm_lapic *source,
		int short_hand, int dest, int dest_mode);
int kvm_apic_compare_prio(struct kvm_vcpu *vcpu1, struct kvm_vcpu *vcpu2);
int kvm_apic_set_irq(struct kvm_vcpu *vcpu, struct kvm_lapic_irq *irq);
#define kvm_apic_present(x) (true)

#endif

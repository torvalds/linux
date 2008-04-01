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
int kvm_apic_set_irq(struct kvm_vcpu *vcpu, u8 vec, u8 trig);

#endif

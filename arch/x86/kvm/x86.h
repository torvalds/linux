#ifndef ARCH_X86_KVM_X86_H
#define ARCH_X86_KVM_X86_H

#include <linux/kvm_host.h>

static inline void kvm_clear_exception_queue(struct kvm_vcpu *vcpu)
{
	vcpu->arch.exception.pending = false;
}

static inline void kvm_queue_interrupt(struct kvm_vcpu *vcpu, u8 vector)
{
	vcpu->arch.interrupt.pending = true;
	vcpu->arch.interrupt.nr = vector;
}

static inline void kvm_clear_interrupt_queue(struct kvm_vcpu *vcpu)
{
	vcpu->arch.interrupt.pending = false;
}

static inline u8 kvm_pop_irq(struct kvm_vcpu *vcpu)
{
	int word_index = __ffs(vcpu->arch.irq_summary);
	int bit_index = __ffs(vcpu->arch.irq_pending[word_index]);
	int irq = word_index * BITS_PER_LONG + bit_index;

	clear_bit(bit_index, &vcpu->arch.irq_pending[word_index]);
	if (!vcpu->arch.irq_pending[word_index])
		clear_bit(word_index, &vcpu->arch.irq_summary);
	return irq;
}

static inline bool kvm_event_needs_reinjection(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.exception.pending || vcpu->arch.interrupt.pending ||
		vcpu->arch.nmi_injected;
}
#endif

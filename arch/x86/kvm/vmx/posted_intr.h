/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_POSTED_INTR_H
#define __KVM_X86_VMX_POSTED_INTR_H

#define POSTED_INTR_ON  0
#define POSTED_INTR_SN  1

/* Posted-Interrupt Descriptor */
struct pi_desc {
	u32 pir[8];     /* Posted interrupt requested */
	union {
		struct {
				/* bit 256 - Outstanding Notification */
			u16	on	: 1,
				/* bit 257 - Suppress Notification */
				sn	: 1,
				/* bit 271:258 - Reserved */
				rsvd_1	: 14;
				/* bit 279:272 - Notification Vector */
			u8	nv;
				/* bit 287:280 - Reserved */
			u8	rsvd_2;
				/* bit 319:288 - Notification Destination */
			u32	ndst;
		};
		u64 control;
	};
	u32 rsvd[6];
} __aligned(64);

static inline bool pi_test_and_set_on(struct pi_desc *pi_desc)
{
	return test_and_set_bit(POSTED_INTR_ON,
			(unsigned long *)&pi_desc->control);
}

static inline bool pi_test_and_clear_on(struct pi_desc *pi_desc)
{
	return test_and_clear_bit(POSTED_INTR_ON,
			(unsigned long *)&pi_desc->control);
}

static inline int pi_test_and_set_pir(int vector, struct pi_desc *pi_desc)
{
	return test_and_set_bit(vector, (unsigned long *)pi_desc->pir);
}

static inline bool pi_is_pir_empty(struct pi_desc *pi_desc)
{
	return bitmap_empty((unsigned long *)pi_desc->pir, NR_VECTORS);
}

static inline void pi_set_sn(struct pi_desc *pi_desc)
{
	set_bit(POSTED_INTR_SN,
		(unsigned long *)&pi_desc->control);
}

static inline void pi_set_on(struct pi_desc *pi_desc)
{
	set_bit(POSTED_INTR_ON,
		(unsigned long *)&pi_desc->control);
}

static inline void pi_clear_on(struct pi_desc *pi_desc)
{
	clear_bit(POSTED_INTR_ON,
		(unsigned long *)&pi_desc->control);
}

static inline void pi_clear_sn(struct pi_desc *pi_desc)
{
	clear_bit(POSTED_INTR_SN,
		(unsigned long *)&pi_desc->control);
}

static inline int pi_test_on(struct pi_desc *pi_desc)
{
	return test_bit(POSTED_INTR_ON,
			(unsigned long *)&pi_desc->control);
}

static inline int pi_test_sn(struct pi_desc *pi_desc)
{
	return test_bit(POSTED_INTR_SN,
			(unsigned long *)&pi_desc->control);
}

void vmx_vcpu_pi_load(struct kvm_vcpu *vcpu, int cpu);
void vmx_vcpu_pi_put(struct kvm_vcpu *vcpu);
int pi_pre_block(struct kvm_vcpu *vcpu);
void pi_post_block(struct kvm_vcpu *vcpu);
void pi_wakeup_handler(void);
void __init pi_init(int cpu);
bool pi_has_pending_interrupt(struct kvm_vcpu *vcpu);
int pi_update_irte(struct kvm *kvm, unsigned int host_irq, uint32_t guest_irq,
		   bool set);

#endif /* __KVM_X86_VMX_POSTED_INTR_H */
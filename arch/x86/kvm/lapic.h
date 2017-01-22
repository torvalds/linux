#ifndef __KVM_X86_LAPIC_H
#define __KVM_X86_LAPIC_H

#include <kvm/iodev.h>

#include <linux/kvm_host.h>

#define KVM_APIC_INIT		0
#define KVM_APIC_SIPI		1

struct kvm_timer {
	struct hrtimer timer;
	s64 period; 				/* unit: ns */
	u32 timer_mode;
	u32 timer_mode_mask;
	u64 tscdeadline;
	u64 expired_tscdeadline;
	atomic_t pending;			/* accumulated triggered timers */
};

struct kvm_lapic {
	unsigned long base_address;
	struct kvm_io_device dev;
	struct kvm_timer lapic_timer;
	u32 divide_count;
	struct kvm_vcpu *vcpu;
	bool sw_enabled;
	bool irr_pending;
	bool lvt0_in_nmi_mode;
	/* Number of bits set in ISR. */
	s16 isr_count;
	/* The highest vector set in ISR; if -1 - invalid, must scan ISR. */
	int highest_isr_cache;
	/**
	 * APIC register page.  The layout matches the register layout seen by
	 * the guest 1:1, because it is accessed by the vmx microcode.
	 * Note: Only one register, the TPR, is used by the microcode.
	 */
	void *regs;
	gpa_t vapic_addr;
	struct gfn_to_hva_cache vapic_cache;
	unsigned long pending_events;
	unsigned int sipi_vector;
};
int kvm_create_lapic(struct kvm_vcpu *vcpu);
void kvm_free_lapic(struct kvm_vcpu *vcpu);

int kvm_apic_has_interrupt(struct kvm_vcpu *vcpu);
int kvm_apic_accept_pic_intr(struct kvm_vcpu *vcpu);
int kvm_get_apic_interrupt(struct kvm_vcpu *vcpu);
void kvm_apic_accept_events(struct kvm_vcpu *vcpu);
void kvm_lapic_reset(struct kvm_vcpu *vcpu, bool init_event);
u64 kvm_lapic_get_cr8(struct kvm_vcpu *vcpu);
void kvm_lapic_set_tpr(struct kvm_vcpu *vcpu, unsigned long cr8);
void kvm_lapic_set_eoi(struct kvm_vcpu *vcpu);
void kvm_lapic_set_base(struct kvm_vcpu *vcpu, u64 value);
u64 kvm_lapic_get_base(struct kvm_vcpu *vcpu);
void kvm_apic_set_version(struct kvm_vcpu *vcpu);

void __kvm_apic_update_irr(u32 *pir, void *regs);
void kvm_apic_update_irr(struct kvm_vcpu *vcpu, u32 *pir);
int kvm_apic_set_irq(struct kvm_vcpu *vcpu, struct kvm_lapic_irq *irq,
		unsigned long *dest_map);
int kvm_apic_local_deliver(struct kvm_lapic *apic, int lvt_type);

bool kvm_irq_delivery_to_apic_fast(struct kvm *kvm, struct kvm_lapic *src,
		struct kvm_lapic_irq *irq, int *r, unsigned long *dest_map);

u64 kvm_get_apic_base(struct kvm_vcpu *vcpu);
int kvm_set_apic_base(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
void kvm_apic_post_state_restore(struct kvm_vcpu *vcpu,
		struct kvm_lapic_state *s);
int kvm_lapic_find_highest_irr(struct kvm_vcpu *vcpu);

u64 kvm_get_lapic_tscdeadline_msr(struct kvm_vcpu *vcpu);
void kvm_set_lapic_tscdeadline_msr(struct kvm_vcpu *vcpu, u64 data);

void kvm_apic_write_nodecode(struct kvm_vcpu *vcpu, u32 offset);
void kvm_apic_set_eoi_accelerated(struct kvm_vcpu *vcpu, int vector);

int kvm_lapic_set_vapic_addr(struct kvm_vcpu *vcpu, gpa_t vapic_addr);
void kvm_lapic_sync_from_vapic(struct kvm_vcpu *vcpu);
void kvm_lapic_sync_to_vapic(struct kvm_vcpu *vcpu);

int kvm_x2apic_msr_write(struct kvm_vcpu *vcpu, u32 msr, u64 data);
int kvm_x2apic_msr_read(struct kvm_vcpu *vcpu, u32 msr, u64 *data);

int kvm_hv_vapic_msr_write(struct kvm_vcpu *vcpu, u32 msr, u64 data);
int kvm_hv_vapic_msr_read(struct kvm_vcpu *vcpu, u32 msr, u64 *data);

static inline bool kvm_hv_vapic_assist_page_enabled(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hyperv.hv_vapic & HV_X64_MSR_APIC_ASSIST_PAGE_ENABLE;
}

int kvm_lapic_enable_pv_eoi(struct kvm_vcpu *vcpu, u64 data);
void kvm_lapic_init(void);
void kvm_lapic_exit(void);

static inline u32 kvm_apic_get_reg(struct kvm_lapic *apic, int reg_off)
{
	        return *((u32 *) (apic->regs + reg_off));
}

extern struct static_key kvm_no_apic_vcpu;

static inline bool kvm_vcpu_has_lapic(struct kvm_vcpu *vcpu)
{
	if (static_key_false(&kvm_no_apic_vcpu))
		return vcpu->arch.apic;
	return true;
}

extern struct static_key_deferred apic_hw_disabled;

static inline int kvm_apic_hw_enabled(struct kvm_lapic *apic)
{
	if (static_key_false(&apic_hw_disabled.key))
		return apic->vcpu->arch.apic_base & MSR_IA32_APICBASE_ENABLE;
	return MSR_IA32_APICBASE_ENABLE;
}

extern struct static_key_deferred apic_sw_disabled;

static inline bool kvm_apic_sw_enabled(struct kvm_lapic *apic)
{
	if (static_key_false(&apic_sw_disabled.key))
		return apic->sw_enabled;
	return true;
}

static inline bool kvm_apic_present(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_has_lapic(vcpu) && kvm_apic_hw_enabled(vcpu->arch.apic);
}

static inline int kvm_lapic_enabled(struct kvm_vcpu *vcpu)
{
	return kvm_apic_present(vcpu) && kvm_apic_sw_enabled(vcpu->arch.apic);
}

static inline int apic_x2apic_mode(struct kvm_lapic *apic)
{
	return apic->vcpu->arch.apic_base & X2APIC_ENABLE;
}

static inline bool kvm_vcpu_apic_vid_enabled(struct kvm_vcpu *vcpu)
{
	return kvm_x86_ops->cpu_uses_apicv(vcpu);
}

static inline bool kvm_apic_has_events(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_has_lapic(vcpu) && vcpu->arch.apic->pending_events;
}

static inline bool kvm_lowest_prio_delivery(struct kvm_lapic_irq *irq)
{
	return (irq->delivery_mode == APIC_DM_LOWEST ||
			irq->msi_redir_hint);
}

static inline int kvm_lapic_latched_init(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_has_lapic(vcpu) && test_bit(KVM_APIC_INIT, &vcpu->arch.apic->pending_events);
}

bool kvm_apic_pending_eoi(struct kvm_vcpu *vcpu, int vector);

void wait_lapic_expire(struct kvm_vcpu *vcpu);

bool kvm_intr_is_single_vcpu_fast(struct kvm *kvm, struct kvm_lapic_irq *irq,
			struct kvm_vcpu **dest_vcpu);
#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_LAPIC_H
#define __KVM_X86_LAPIC_H

#include <kvm/iodev.h>

#include <linux/kvm_host.h>

#include "hyperv.h"

#define KVM_APIC_INIT		0
#define KVM_APIC_SIPI		1
#define KVM_APIC_LVT_NUM	6

#define APIC_SHORT_MASK			0xc0000
#define APIC_DEST_NOSHORT		0x0
#define APIC_DEST_MASK			0x800

#define APIC_BUS_CYCLE_NS       1
#define APIC_BUS_FREQUENCY      (1000000000ULL / APIC_BUS_CYCLE_NS)

#define APIC_BROADCAST			0xFF
#define X2APIC_BROADCAST		0xFFFFFFFFul

enum lapic_mode {
	LAPIC_MODE_DISABLED = 0,
	LAPIC_MODE_INVALID = X2APIC_ENABLE,
	LAPIC_MODE_XAPIC = MSR_IA32_APICBASE_ENABLE,
	LAPIC_MODE_X2APIC = MSR_IA32_APICBASE_ENABLE | X2APIC_ENABLE,
};

struct kvm_timer {
	struct hrtimer timer;
	s64 period; 				/* unit: ns */
	ktime_t target_expiration;
	u32 timer_mode;
	u32 timer_mode_mask;
	u64 tscdeadline;
	u64 expired_tscdeadline;
	u32 timer_advance_ns;
	s64 advance_expire_delta;
	atomic_t pending;			/* accumulated triggered timers */
	bool hv_timer_in_use;
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

struct dest_map;

int kvm_create_lapic(struct kvm_vcpu *vcpu, int timer_advance_ns);
void kvm_free_lapic(struct kvm_vcpu *vcpu);

int kvm_apic_has_interrupt(struct kvm_vcpu *vcpu);
int kvm_apic_accept_pic_intr(struct kvm_vcpu *vcpu);
int kvm_get_apic_interrupt(struct kvm_vcpu *vcpu);
int kvm_apic_accept_events(struct kvm_vcpu *vcpu);
void kvm_lapic_reset(struct kvm_vcpu *vcpu, bool init_event);
u64 kvm_lapic_get_cr8(struct kvm_vcpu *vcpu);
void kvm_lapic_set_tpr(struct kvm_vcpu *vcpu, unsigned long cr8);
void kvm_lapic_set_eoi(struct kvm_vcpu *vcpu);
void kvm_lapic_set_base(struct kvm_vcpu *vcpu, u64 value);
u64 kvm_lapic_get_base(struct kvm_vcpu *vcpu);
void kvm_recalculate_apic_map(struct kvm *kvm);
void kvm_apic_set_version(struct kvm_vcpu *vcpu);
bool kvm_apic_match_dest(struct kvm_vcpu *vcpu, struct kvm_lapic *source,
			   int shorthand, unsigned int dest, int dest_mode);
int kvm_apic_compare_prio(struct kvm_vcpu *vcpu1, struct kvm_vcpu *vcpu2);
void kvm_apic_clear_irr(struct kvm_vcpu *vcpu, int vec);
bool __kvm_apic_update_irr(u32 *pir, void *regs, int *max_irr);
bool kvm_apic_update_irr(struct kvm_vcpu *vcpu, u32 *pir, int *max_irr);
void kvm_apic_update_ppr(struct kvm_vcpu *vcpu);
int kvm_apic_set_irq(struct kvm_vcpu *vcpu, struct kvm_lapic_irq *irq,
		     struct dest_map *dest_map);
int kvm_apic_local_deliver(struct kvm_lapic *apic, int lvt_type);
void kvm_apic_update_apicv(struct kvm_vcpu *vcpu);

bool kvm_irq_delivery_to_apic_fast(struct kvm *kvm, struct kvm_lapic *src,
		struct kvm_lapic_irq *irq, int *r, struct dest_map *dest_map);
void kvm_apic_send_ipi(struct kvm_lapic *apic, u32 icr_low, u32 icr_high);

u64 kvm_get_apic_base(struct kvm_vcpu *vcpu);
int kvm_set_apic_base(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
int kvm_apic_get_state(struct kvm_vcpu *vcpu, struct kvm_lapic_state *s);
int kvm_apic_set_state(struct kvm_vcpu *vcpu, struct kvm_lapic_state *s);
enum lapic_mode kvm_get_apic_mode(struct kvm_vcpu *vcpu);
int kvm_lapic_find_highest_irr(struct kvm_vcpu *vcpu);

u64 kvm_get_lapic_tscdeadline_msr(struct kvm_vcpu *vcpu);
void kvm_set_lapic_tscdeadline_msr(struct kvm_vcpu *vcpu, u64 data);

void kvm_apic_write_nodecode(struct kvm_vcpu *vcpu, u32 offset);
void kvm_apic_set_eoi_accelerated(struct kvm_vcpu *vcpu, int vector);

int kvm_lapic_set_vapic_addr(struct kvm_vcpu *vcpu, gpa_t vapic_addr);
void kvm_lapic_sync_from_vapic(struct kvm_vcpu *vcpu);
void kvm_lapic_sync_to_vapic(struct kvm_vcpu *vcpu);

int kvm_x2apic_icr_write(struct kvm_lapic *apic, u64 data);
int kvm_x2apic_msr_write(struct kvm_vcpu *vcpu, u32 msr, u64 data);
int kvm_x2apic_msr_read(struct kvm_vcpu *vcpu, u32 msr, u64 *data);

int kvm_hv_vapic_msr_write(struct kvm_vcpu *vcpu, u32 msr, u64 data);
int kvm_hv_vapic_msr_read(struct kvm_vcpu *vcpu, u32 msr, u64 *data);

int kvm_lapic_set_pv_eoi(struct kvm_vcpu *vcpu, u64 data, unsigned long len);
void kvm_lapic_exit(void);

#define VEC_POS(v) ((v) & (32 - 1))
#define REG_POS(v) (((v) >> 5) << 4)

static inline void kvm_lapic_clear_vector(int vec, void *bitmap)
{
	clear_bit(VEC_POS(vec), (bitmap) + REG_POS(vec));
}

static inline void kvm_lapic_set_vector(int vec, void *bitmap)
{
	set_bit(VEC_POS(vec), (bitmap) + REG_POS(vec));
}

static inline void kvm_lapic_set_irr(int vec, struct kvm_lapic *apic)
{
	kvm_lapic_set_vector(vec, apic->regs + APIC_IRR);
	/*
	 * irr_pending must be true if any interrupt is pending; set it after
	 * APIC_IRR to avoid race with apic_clear_irr
	 */
	apic->irr_pending = true;
}

static inline u32 __kvm_lapic_get_reg(char *regs, int reg_off)
{
	return *((u32 *) (regs + reg_off));
}

static inline u32 kvm_lapic_get_reg(struct kvm_lapic *apic, int reg_off)
{
	return __kvm_lapic_get_reg(apic->regs, reg_off);
}

DECLARE_STATIC_KEY_FALSE(kvm_has_noapic_vcpu);

static inline bool lapic_in_kernel(struct kvm_vcpu *vcpu)
{
	if (static_branch_unlikely(&kvm_has_noapic_vcpu))
		return vcpu->arch.apic;
	return true;
}

extern struct static_key_false_deferred apic_hw_disabled;

static inline int kvm_apic_hw_enabled(struct kvm_lapic *apic)
{
	if (static_branch_unlikely(&apic_hw_disabled.key))
		return apic->vcpu->arch.apic_base & MSR_IA32_APICBASE_ENABLE;
	return MSR_IA32_APICBASE_ENABLE;
}

extern struct static_key_false_deferred apic_sw_disabled;

static inline bool kvm_apic_sw_enabled(struct kvm_lapic *apic)
{
	if (static_branch_unlikely(&apic_sw_disabled.key))
		return apic->sw_enabled;
	return true;
}

static inline bool kvm_apic_present(struct kvm_vcpu *vcpu)
{
	return lapic_in_kernel(vcpu) && kvm_apic_hw_enabled(vcpu->arch.apic);
}

static inline int kvm_lapic_enabled(struct kvm_vcpu *vcpu)
{
	return kvm_apic_present(vcpu) && kvm_apic_sw_enabled(vcpu->arch.apic);
}

static inline int apic_x2apic_mode(struct kvm_lapic *apic)
{
	return apic->vcpu->arch.apic_base & X2APIC_ENABLE;
}

static inline bool kvm_vcpu_apicv_active(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.apic && vcpu->arch.apicv_active;
}

static inline bool kvm_apic_has_events(struct kvm_vcpu *vcpu)
{
	return lapic_in_kernel(vcpu) && vcpu->arch.apic->pending_events;
}

static inline bool kvm_lowest_prio_delivery(struct kvm_lapic_irq *irq)
{
	return (irq->delivery_mode == APIC_DM_LOWEST ||
			irq->msi_redir_hint);
}

static inline int kvm_lapic_latched_init(struct kvm_vcpu *vcpu)
{
	return lapic_in_kernel(vcpu) && test_bit(KVM_APIC_INIT, &vcpu->arch.apic->pending_events);
}

bool kvm_apic_pending_eoi(struct kvm_vcpu *vcpu, int vector);

void kvm_wait_lapic_expire(struct kvm_vcpu *vcpu);

void kvm_bitmap_or_dest_vcpus(struct kvm *kvm, struct kvm_lapic_irq *irq,
			      unsigned long *vcpu_bitmap);

bool kvm_intr_is_single_vcpu_fast(struct kvm *kvm, struct kvm_lapic_irq *irq,
			struct kvm_vcpu **dest_vcpu);
int kvm_vector_to_index(u32 vector, u32 dest_vcpus,
			const unsigned long *bitmap, u32 bitmap_size);
void kvm_lapic_switch_to_sw_timer(struct kvm_vcpu *vcpu);
void kvm_lapic_switch_to_hv_timer(struct kvm_vcpu *vcpu);
void kvm_lapic_expired_hv_timer(struct kvm_vcpu *vcpu);
bool kvm_lapic_hv_timer_in_use(struct kvm_vcpu *vcpu);
void kvm_lapic_restart_hv_timer(struct kvm_vcpu *vcpu);
bool kvm_can_use_hv_timer(struct kvm_vcpu *vcpu);

static inline enum lapic_mode kvm_apic_mode(u64 apic_base)
{
	return apic_base & (MSR_IA32_APICBASE_ENABLE | X2APIC_ENABLE);
}

static inline u8 kvm_xapic_id(struct kvm_lapic *apic)
{
	return kvm_lapic_get_reg(apic, APIC_ID) >> 24;
}

#endif

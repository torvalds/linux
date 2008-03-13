#ifndef __I8254_H
#define __I8254_H

#include "iodev.h"

struct kvm_kpit_timer {
	struct hrtimer timer;
	int irq;
	s64 period; /* unit: ns */
	s64 scheduled;
	ktime_t last_update;
	atomic_t pending;
};

struct kvm_kpit_channel_state {
	u32 count; /* can be 65536 */
	u16 latched_count;
	u8 count_latched;
	u8 status_latched;
	u8 status;
	u8 read_state;
	u8 write_state;
	u8 write_latch;
	u8 rw_mode;
	u8 mode;
	u8 bcd; /* not supported */
	u8 gate; /* timer start */
	ktime_t count_load_time;
};

struct kvm_kpit_state {
	struct kvm_kpit_channel_state channels[3];
	struct kvm_kpit_timer pit_timer;
	u32    speaker_data_on;
	struct mutex lock;
	struct kvm_pit *pit;
	bool inject_pending; /* if inject pending interrupts */
	unsigned long last_injected_time;
};

struct kvm_pit {
	unsigned long base_addresss;
	struct kvm_io_device dev;
	struct kvm_io_device speaker_dev;
	struct kvm *kvm;
	struct kvm_kpit_state pit_state;
};

#define KVM_PIT_BASE_ADDRESS	    0x40
#define KVM_SPEAKER_BASE_ADDRESS    0x61
#define KVM_PIT_MEM_LENGTH	    4
#define KVM_PIT_FREQ		    1193181
#define KVM_MAX_PIT_INTR_INTERVAL   HZ / 100
#define KVM_PIT_CHANNEL_MASK	    0x3

void kvm_inject_pit_timer_irqs(struct kvm_vcpu *vcpu);
void kvm_pit_timer_intr_post(struct kvm_vcpu *vcpu, int vec);
void kvm_pit_load_count(struct kvm *kvm, int channel, u32 val);
struct kvm_pit *kvm_create_pit(struct kvm *kvm);
void kvm_free_pit(struct kvm *kvm);
void kvm_pit_reset(struct kvm_pit *pit);

#endif

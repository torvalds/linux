/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __I8254_H
#define __I8254_H

#include <linux/kthread.h>

#include <kvm/iodev.h>

#include <uapi/asm/kvm.h>

#include "ioapic.h"

#ifdef CONFIG_KVM_IOAPIC
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
	/* All members before "struct mutex lock" are protected by the lock. */
	struct kvm_kpit_channel_state channels[3];
	u32 flags;
	bool is_periodic;
	s64 period; 				/* unit: ns */
	struct hrtimer timer;

	struct mutex lock;
	atomic_t reinject;
	atomic_t pending; /* accumulated triggered timers */
	atomic_t irq_ack;
	struct kvm_irq_ack_notifier irq_ack_notifier;
};

struct kvm_pit {
	struct kvm_io_device dev;
	struct kvm_io_device speaker_dev;
	struct kvm *kvm;
	struct kvm_kpit_state pit_state;
	struct kvm_irq_mask_notifier mask_notifier;
	struct kthread_worker *worker;
	struct kthread_work expired;
};

#define KVM_PIT_BASE_ADDRESS	    0x40
#define KVM_SPEAKER_BASE_ADDRESS    0x61
#define KVM_PIT_MEM_LENGTH	    4
#define KVM_PIT_FREQ		    1193181
#define KVM_MAX_PIT_INTR_INTERVAL   HZ / 100
#define KVM_PIT_CHANNEL_MASK	    0x3

int kvm_vm_ioctl_get_pit(struct kvm *kvm, struct kvm_pit_state *ps);
int kvm_vm_ioctl_set_pit(struct kvm *kvm, struct kvm_pit_state *ps);
int kvm_vm_ioctl_get_pit2(struct kvm *kvm, struct kvm_pit_state2 *ps);
int kvm_vm_ioctl_set_pit2(struct kvm *kvm, struct kvm_pit_state2 *ps);
int kvm_vm_ioctl_reinject(struct kvm *kvm, struct kvm_reinject_control *control);

struct kvm_pit *kvm_create_pit(struct kvm *kvm, u32 flags);
void kvm_free_pit(struct kvm *kvm);
#endif /* CONFIG_KVM_IOAPIC */

#endif

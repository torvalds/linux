// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright © 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * KVM Xen emulation
 */

#ifndef __ARCH_X86_KVM_XEN_H__
#define __ARCH_X86_KVM_XEN_H__

#ifdef CONFIG_KVM_XEN
#include <linux/jump_label_ratelimit.h>

extern struct static_key_false_deferred kvm_xen_enabled;

int __kvm_xen_has_interrupt(struct kvm_vcpu *vcpu);
void kvm_xen_inject_pending_events(struct kvm_vcpu *vcpu);
int kvm_xen_vcpu_set_attr(struct kvm_vcpu *vcpu, struct kvm_xen_vcpu_attr *data);
int kvm_xen_vcpu_get_attr(struct kvm_vcpu *vcpu, struct kvm_xen_vcpu_attr *data);
int kvm_xen_hvm_set_attr(struct kvm *kvm, struct kvm_xen_hvm_attr *data);
int kvm_xen_hvm_get_attr(struct kvm *kvm, struct kvm_xen_hvm_attr *data);
int kvm_xen_hvm_evtchn_send(struct kvm *kvm, struct kvm_irq_routing_xen_evtchn *evt);
int kvm_xen_write_hypercall_page(struct kvm_vcpu *vcpu, u64 data);
int kvm_xen_hvm_config(struct kvm *kvm, struct kvm_xen_hvm_config *xhc);
void kvm_xen_init_vm(struct kvm *kvm);
void kvm_xen_destroy_vm(struct kvm *kvm);
void kvm_xen_init_vcpu(struct kvm_vcpu *vcpu);
void kvm_xen_destroy_vcpu(struct kvm_vcpu *vcpu);
int kvm_xen_set_evtchn_fast(struct kvm_xen_evtchn *xe,
			    struct kvm *kvm);
int kvm_xen_setup_evtchn(struct kvm *kvm,
			 struct kvm_kernel_irq_routing_entry *e,
			 const struct kvm_irq_routing_entry *ue);

static inline bool kvm_xen_msr_enabled(struct kvm *kvm)
{
	return static_branch_unlikely(&kvm_xen_enabled.key) &&
		kvm->arch.xen_hvm_config.msr;
}

static inline bool kvm_xen_hypercall_enabled(struct kvm *kvm)
{
	return static_branch_unlikely(&kvm_xen_enabled.key) &&
		(kvm->arch.xen_hvm_config.flags &
		 KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL);
}

static inline int kvm_xen_has_interrupt(struct kvm_vcpu *vcpu)
{
	if (static_branch_unlikely(&kvm_xen_enabled.key) &&
	    vcpu->arch.xen.vcpu_info_cache.active &&
	    vcpu->kvm->arch.xen.upcall_vector)
		return __kvm_xen_has_interrupt(vcpu);

	return 0;
}

static inline bool kvm_xen_has_pending_events(struct kvm_vcpu *vcpu)
{
	return static_branch_unlikely(&kvm_xen_enabled.key) &&
		vcpu->arch.xen.evtchn_pending_sel;
}

static inline bool kvm_xen_timer_enabled(struct kvm_vcpu *vcpu)
{
	return !!vcpu->arch.xen.timer_virq;
}

static inline int kvm_xen_has_pending_timer(struct kvm_vcpu *vcpu)
{
	if (kvm_xen_hypercall_enabled(vcpu->kvm) && kvm_xen_timer_enabled(vcpu))
		return atomic_read(&vcpu->arch.xen.timer_pending);

	return 0;
}

void kvm_xen_inject_timer_irqs(struct kvm_vcpu *vcpu);
#else
static inline int kvm_xen_write_hypercall_page(struct kvm_vcpu *vcpu, u64 data)
{
	return 1;
}

static inline void kvm_xen_init_vm(struct kvm *kvm)
{
}

static inline void kvm_xen_destroy_vm(struct kvm *kvm)
{
}

static inline void kvm_xen_init_vcpu(struct kvm_vcpu *vcpu)
{
}

static inline void kvm_xen_destroy_vcpu(struct kvm_vcpu *vcpu)
{
}

static inline bool kvm_xen_msr_enabled(struct kvm *kvm)
{
	return false;
}

static inline bool kvm_xen_hypercall_enabled(struct kvm *kvm)
{
	return false;
}

static inline int kvm_xen_has_interrupt(struct kvm_vcpu *vcpu)
{
	return 0;
}

static inline void kvm_xen_inject_pending_events(struct kvm_vcpu *vcpu)
{
}

static inline bool kvm_xen_has_pending_events(struct kvm_vcpu *vcpu)
{
	return false;
}

static inline int kvm_xen_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return 0;
}

static inline void kvm_xen_inject_timer_irqs(struct kvm_vcpu *vcpu)
{
}

static inline bool kvm_xen_timer_enabled(struct kvm_vcpu *vcpu)
{
	return false;
}
#endif

int kvm_xen_hypercall(struct kvm_vcpu *vcpu);

#include <asm/pvclock-abi.h>
#include <asm/xen/interface.h>
#include <xen/interface/vcpu.h>

void kvm_xen_update_runstate_guest(struct kvm_vcpu *vcpu, int state);

static inline void kvm_xen_runstate_set_running(struct kvm_vcpu *vcpu)
{
	kvm_xen_update_runstate_guest(vcpu, RUNSTATE_running);
}

static inline void kvm_xen_runstate_set_preempted(struct kvm_vcpu *vcpu)
{
	/*
	 * If the vCPU wasn't preempted but took a normal exit for
	 * some reason (hypercalls, I/O, etc.), that is accounted as
	 * still RUNSTATE_running, as the VMM is still operating on
	 * behalf of the vCPU. Only if the VMM does actually block
	 * does it need to enter RUNSTATE_blocked.
	 */
	if (WARN_ON_ONCE(!vcpu->preempted))
		return;

	kvm_xen_update_runstate_guest(vcpu, RUNSTATE_runnable);
}

/* 32-bit compatibility definitions, also used natively in 32-bit build */
struct compat_arch_vcpu_info {
	unsigned int cr2;
	unsigned int pad[5];
};

struct compat_vcpu_info {
	uint8_t evtchn_upcall_pending;
	uint8_t evtchn_upcall_mask;
	uint16_t pad;
	uint32_t evtchn_pending_sel;
	struct compat_arch_vcpu_info arch;
	struct pvclock_vcpu_time_info time;
}; /* 64 bytes (x86) */

struct compat_arch_shared_info {
	unsigned int max_pfn;
	unsigned int pfn_to_mfn_frame_list_list;
	unsigned int nmi_reason;
	unsigned int p2m_cr3;
	unsigned int p2m_vaddr;
	unsigned int p2m_generation;
	uint32_t wc_sec_hi;
};

struct compat_shared_info {
	struct compat_vcpu_info vcpu_info[MAX_VIRT_CPUS];
	uint32_t evtchn_pending[32];
	uint32_t evtchn_mask[32];
	struct pvclock_wall_clock wc;
	struct compat_arch_shared_info arch;
};

#define COMPAT_EVTCHN_2L_NR_CHANNELS (8 *				\
				      sizeof_field(struct compat_shared_info, \
						   evtchn_pending))
struct compat_vcpu_runstate_info {
    int state;
    uint64_t state_entry_time;
    uint64_t time[4];
} __attribute__((packed));

#endif /* __ARCH_X86_KVM_XEN_H__ */

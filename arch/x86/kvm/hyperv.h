/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KVM Microsoft Hyper-V emulation
 *
 * derived from arch/x86/kvm/x86.c
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright (C) 2008 Qumranet, Inc.
 * Copyright IBM Corporation, 2008
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 * Copyright (C) 2015 Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Amit Shah    <amit.shah@qumranet.com>
 *   Ben-Ami Yassour <benami@il.ibm.com>
 *   Andrey Smetanin <asmetanin@virtuozzo.com>
 */

#ifndef __ARCH_X86_KVM_HYPERV_H__
#define __ARCH_X86_KVM_HYPERV_H__

#include <linux/kvm_host.h>
#include "x86.h"

#ifdef CONFIG_KVM_HYPERV

/* "Hv#1" signature */
#define HYPERV_CPUID_SIGNATURE_EAX 0x31237648

/*
 * The #defines related to the synthetic debugger are required by KDNet, but
 * they are not documented in the Hyper-V TLFS because the synthetic debugger
 * functionality has been deprecated and is subject to removal in future
 * versions of Windows.
 */
#define HYPERV_CPUID_SYNDBG_VENDOR_AND_MAX_FUNCTIONS	0x40000080
#define HYPERV_CPUID_SYNDBG_INTERFACE			0x40000081
#define HYPERV_CPUID_SYNDBG_PLATFORM_CAPABILITIES	0x40000082

/*
 * Hyper-V synthetic debugger platform capabilities
 * These are HYPERV_CPUID_SYNDBG_PLATFORM_CAPABILITIES.EAX bits.
 */
#define HV_X64_SYNDBG_CAP_ALLOW_KERNEL_DEBUGGING	BIT(1)

/* Hyper-V Synthetic debug options MSR */
#define HV_X64_MSR_SYNDBG_CONTROL		0x400000F1
#define HV_X64_MSR_SYNDBG_STATUS		0x400000F2
#define HV_X64_MSR_SYNDBG_SEND_BUFFER		0x400000F3
#define HV_X64_MSR_SYNDBG_RECV_BUFFER		0x400000F4
#define HV_X64_MSR_SYNDBG_PENDING_BUFFER	0x400000F5
#define HV_X64_MSR_SYNDBG_OPTIONS		0x400000FF

/* Hyper-V HV_X64_MSR_SYNDBG_OPTIONS bits */
#define HV_X64_SYNDBG_OPTION_USE_HCALLS		BIT(2)

static inline struct kvm_hv *to_kvm_hv(struct kvm *kvm)
{
	return &kvm->arch.hyperv;
}

static inline struct kvm_vcpu_hv *to_hv_vcpu(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hyperv;
}

static inline struct kvm_vcpu_hv_synic *to_hv_synic(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);

	return &hv_vcpu->synic;
}

static inline struct kvm_vcpu *hv_synic_to_vcpu(struct kvm_vcpu_hv_synic *synic)
{
	struct kvm_vcpu_hv *hv_vcpu = container_of(synic, struct kvm_vcpu_hv, synic);

	return hv_vcpu->vcpu;
}

static inline struct kvm_hv_syndbg *to_hv_syndbg(struct kvm_vcpu *vcpu)
{
	return &vcpu->kvm->arch.hyperv.hv_syndbg;
}

static inline u32 kvm_hv_get_vpindex(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);

	return hv_vcpu ? hv_vcpu->vp_index : vcpu->vcpu_idx;
}

int kvm_hv_set_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 data, bool host);
int kvm_hv_get_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata, bool host);

static inline bool kvm_hv_hypercall_enabled(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hyperv_enabled && to_kvm_hv(vcpu->kvm)->hv_guest_os_id;
}

int kvm_hv_hypercall(struct kvm_vcpu *vcpu);

void kvm_hv_irq_routing_update(struct kvm *kvm);
int kvm_hv_synic_set_irq(struct kvm *kvm, u32 vcpu_id, u32 sint);
void kvm_hv_synic_send_eoi(struct kvm_vcpu *vcpu, int vector);
int kvm_hv_activate_synic(struct kvm_vcpu *vcpu, bool dont_zero_synic_pages);

static inline bool kvm_hv_synic_has_vector(struct kvm_vcpu *vcpu, int vector)
{
	return to_hv_vcpu(vcpu) && test_bit(vector, to_hv_synic(vcpu)->vec_bitmap);
}

static inline bool kvm_hv_synic_auto_eoi_set(struct kvm_vcpu *vcpu, int vector)
{
	return to_hv_vcpu(vcpu) &&
	       test_bit(vector, to_hv_synic(vcpu)->auto_eoi_bitmap);
}

void kvm_hv_vcpu_uninit(struct kvm_vcpu *vcpu);

bool kvm_hv_assist_page_enabled(struct kvm_vcpu *vcpu);
int kvm_hv_get_assist_page(struct kvm_vcpu *vcpu);

static inline struct kvm_vcpu_hv_stimer *to_hv_stimer(struct kvm_vcpu *vcpu,
						      int timer_index)
{
	return &to_hv_vcpu(vcpu)->stimer[timer_index];
}

static inline struct kvm_vcpu *hv_stimer_to_vcpu(struct kvm_vcpu_hv_stimer *stimer)
{
	struct kvm_vcpu_hv *hv_vcpu;

	hv_vcpu = container_of(stimer - stimer->index, struct kvm_vcpu_hv,
			       stimer[0]);
	return hv_vcpu->vcpu;
}

static inline bool kvm_hv_has_stimer_pending(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);

	if (!hv_vcpu)
		return false;

	return !bitmap_empty(hv_vcpu->stimer_pending_bitmap,
			     HV_SYNIC_STIMER_COUNT);
}

/*
 * With HV_ACCESS_TSC_INVARIANT feature, invariant TSC (CPUID.80000007H:EDX[8])
 * is only observed after HV_X64_MSR_TSC_INVARIANT_CONTROL was written to.
 */
static inline bool kvm_hv_invtsc_suppressed(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);

	/*
	 * If Hyper-V's invariant TSC control is not exposed to the guest,
	 * the invariant TSC CPUID flag is not suppressed, Windows guests were
	 * observed to be able to handle it correctly. Going forward, VMMs are
	 * encouraged to enable Hyper-V's invariant TSC control when invariant
	 * TSC CPUID flag is set to make KVM's behavior match genuine Hyper-V.
	 */
	if (!hv_vcpu ||
	    !(hv_vcpu->cpuid_cache.features_eax & HV_ACCESS_TSC_INVARIANT))
		return false;

	/*
	 * If Hyper-V's invariant TSC control is exposed to the guest, KVM is
	 * responsible for suppressing the invariant TSC CPUID flag if the
	 * Hyper-V control is not enabled.
	 */
	return !(to_kvm_hv(vcpu->kvm)->hv_invtsc_control & HV_EXPOSE_INVARIANT_TSC);
}

void kvm_hv_process_stimers(struct kvm_vcpu *vcpu);

void kvm_hv_setup_tsc_page(struct kvm *kvm,
			   struct pvclock_vcpu_time_info *hv_clock);
void kvm_hv_request_tsc_page_update(struct kvm *kvm);

void kvm_hv_xsaves_xsavec_maybe_warn(struct kvm_vcpu *vcpu);

void kvm_hv_init_vm(struct kvm *kvm);
void kvm_hv_destroy_vm(struct kvm *kvm);
int kvm_hv_vcpu_init(struct kvm_vcpu *vcpu);
void kvm_hv_set_cpuid(struct kvm_vcpu *vcpu, bool hyperv_enabled);
int kvm_hv_set_enforce_cpuid(struct kvm_vcpu *vcpu, bool enforce);
int kvm_vm_ioctl_hv_eventfd(struct kvm *kvm, struct kvm_hyperv_eventfd *args);
int kvm_get_hv_cpuid(struct kvm_vcpu *vcpu, struct kvm_cpuid2 *cpuid,
		     struct kvm_cpuid_entry2 __user *entries);

static inline struct kvm_vcpu_hv_tlb_flush_fifo *kvm_hv_get_tlb_flush_fifo(struct kvm_vcpu *vcpu,
									   bool is_guest_mode)
{
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);
	int i = is_guest_mode ? HV_L2_TLB_FLUSH_FIFO :
				HV_L1_TLB_FLUSH_FIFO;

	return &hv_vcpu->tlb_flush_fifo[i];
}

static inline void kvm_hv_vcpu_purge_flush_tlb(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_hv_tlb_flush_fifo *tlb_flush_fifo;

	if (!to_hv_vcpu(vcpu) || !kvm_check_request(KVM_REQ_HV_TLB_FLUSH, vcpu))
		return;

	tlb_flush_fifo = kvm_hv_get_tlb_flush_fifo(vcpu, is_guest_mode(vcpu));

	kfifo_reset_out(&tlb_flush_fifo->entries);
}

static inline bool guest_hv_cpuid_has_l2_tlb_flush(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);

	return hv_vcpu &&
		(hv_vcpu->cpuid_cache.nested_eax & HV_X64_NESTED_DIRECT_FLUSH);
}

static inline bool kvm_hv_is_tlb_flush_hcall(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);
	u16 code;

	if (!hv_vcpu)
		return false;

	code = is_64_bit_hypercall(vcpu) ? kvm_rcx_read(vcpu) :
					   kvm_rax_read(vcpu);

	return (code == HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE ||
		code == HVCALL_FLUSH_VIRTUAL_ADDRESS_LIST ||
		code == HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE_EX ||
		code == HVCALL_FLUSH_VIRTUAL_ADDRESS_LIST_EX);
}

static inline int kvm_hv_verify_vp_assist(struct kvm_vcpu *vcpu)
{
	if (!to_hv_vcpu(vcpu))
		return 0;

	if (!kvm_hv_assist_page_enabled(vcpu))
		return 0;

	return kvm_hv_get_assist_page(vcpu);
}

static inline void kvm_hv_nested_transtion_tlb_flush(struct kvm_vcpu *vcpu,
						     bool tdp_enabled)
{
	/*
	 * KVM_REQ_HV_TLB_FLUSH flushes entries from either L1's VP_ID or
	 * L2's VP_ID upon request from the guest. Make sure we check for
	 * pending entries in the right FIFO upon L1/L2 transition as these
	 * requests are put by other vCPUs asynchronously.
	 */
	if (to_hv_vcpu(vcpu) && tdp_enabled)
		kvm_make_request(KVM_REQ_HV_TLB_FLUSH, vcpu);
}

int kvm_hv_vcpu_flush_tlb(struct kvm_vcpu *vcpu);
#else /* CONFIG_KVM_HYPERV */
static inline void kvm_hv_setup_tsc_page(struct kvm *kvm,
					 struct pvclock_vcpu_time_info *hv_clock) {}
static inline void kvm_hv_request_tsc_page_update(struct kvm *kvm) {}
static inline void kvm_hv_xsaves_xsavec_maybe_warn(struct kvm_vcpu *vcpu) {}
static inline void kvm_hv_init_vm(struct kvm *kvm) {}
static inline void kvm_hv_destroy_vm(struct kvm *kvm) {}
static inline int kvm_hv_vcpu_init(struct kvm_vcpu *vcpu)
{
	return 0;
}
static inline void kvm_hv_vcpu_uninit(struct kvm_vcpu *vcpu) {}
static inline bool kvm_hv_hypercall_enabled(struct kvm_vcpu *vcpu)
{
	return false;
}
static inline int kvm_hv_hypercall(struct kvm_vcpu *vcpu)
{
	return HV_STATUS_ACCESS_DENIED;
}
static inline void kvm_hv_vcpu_purge_flush_tlb(struct kvm_vcpu *vcpu) {}
static inline bool kvm_hv_synic_has_vector(struct kvm_vcpu *vcpu, int vector)
{
	return false;
}
static inline bool kvm_hv_synic_auto_eoi_set(struct kvm_vcpu *vcpu, int vector)
{
	return false;
}
static inline void kvm_hv_synic_send_eoi(struct kvm_vcpu *vcpu, int vector) {}
static inline bool kvm_hv_invtsc_suppressed(struct kvm_vcpu *vcpu)
{
	return false;
}
static inline void kvm_hv_set_cpuid(struct kvm_vcpu *vcpu, bool hyperv_enabled) {}
static inline bool kvm_hv_has_stimer_pending(struct kvm_vcpu *vcpu)
{
	return false;
}
static inline bool kvm_hv_is_tlb_flush_hcall(struct kvm_vcpu *vcpu)
{
	return false;
}
static inline bool guest_hv_cpuid_has_l2_tlb_flush(struct kvm_vcpu *vcpu)
{
	return false;
}
static inline int kvm_hv_verify_vp_assist(struct kvm_vcpu *vcpu)
{
	return 0;
}
static inline u32 kvm_hv_get_vpindex(struct kvm_vcpu *vcpu)
{
	return vcpu->vcpu_idx;
}
static inline void kvm_hv_nested_transtion_tlb_flush(struct kvm_vcpu *vcpu, bool tdp_enabled) {}
#endif /* CONFIG_KVM_HYPERV */

#endif /* __ARCH_X86_KVM_HYPERV_H__ */

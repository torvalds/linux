// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * derived from drivers/kvm/kvm_main.c
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright (C) 2008 Qumranet, Inc.
 * Copyright IBM Corporation, 2008
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Amit Shah    <amit.shah@qumranet.com>
 *   Ben-Ami Yassour <benami@il.ibm.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kvm_host.h>
#include "irq.h"
#include "ioapic.h"
#include "mmu.h"
#include "i8254.h"
#include "tss.h"
#include "kvm_cache_regs.h"
#include "kvm_emulate.h"
#include "mmu/page_track.h"
#include "x86.h"
#include "cpuid.h"
#include "pmu.h"
#include "hyperv.h"
#include "lapic.h"
#include "xen.h"
#include "smm.h"

#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/kvm.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <linux/moduleparam.h>
#include <linux/mman.h>
#include <linux/highmem.h>
#include <linux/iommu.h>
#include <linux/cpufreq.h>
#include <linux/user-return-notifier.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include <linux/uaccess.h>
#include <linux/hash.h>
#include <linux/pci.h>
#include <linux/timekeeper_internal.h>
#include <linux/pvclock_gtod.h>
#include <linux/kvm_irqfd.h>
#include <linux/irqbypass.h>
#include <linux/sched/stat.h>
#include <linux/sched/isolation.h>
#include <linux/mem_encrypt.h>
#include <linux/entry-kvm.h>
#include <linux/suspend.h>
#include <linux/smp.h>

#include <trace/events/ipi.h>
#include <trace/events/kvm.h>

#include <asm/debugreg.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include <asm/mce.h>
#include <asm/pkru.h>
#include <linux/kernel_stat.h>
#include <asm/fpu/api.h>
#include <asm/fpu/xcr.h>
#include <asm/fpu/xstate.h>
#include <asm/pvclock.h>
#include <asm/div64.h>
#include <asm/irq_remapping.h>
#include <asm/mshyperv.h>
#include <asm/hypervisor.h>
#include <asm/tlbflush.h>
#include <asm/intel_pt.h>
#include <asm/emulate_prefix.h>
#include <asm/sgx.h>
#include <clocksource/hyperv_timer.h>

#define CREATE_TRACE_POINTS
#include "trace.h"

#define MAX_IO_MSRS 256
#define KVM_MAX_MCE_BANKS 32

/*
 * Note, kvm_caps fields should *never* have default values, all fields must be
 * recomputed from scratch during vendor module load, e.g. to account for a
 * vendor module being reloaded with different module parameters.
 */
struct kvm_caps kvm_caps __read_mostly;
EXPORT_SYMBOL_GPL(kvm_caps);

struct kvm_host_values kvm_host __read_mostly;
EXPORT_SYMBOL_GPL(kvm_host);

#define  ERR_PTR_USR(e)  ((void __user *)ERR_PTR(e))

#define emul_to_vcpu(ctxt) \
	((struct kvm_vcpu *)(ctxt)->vcpu)

/* EFER defaults:
 * - enable syscall per default because its emulated by KVM
 * - enable LME and LMA per default on 64 bit KVM
 */
#ifdef CONFIG_X86_64
static
u64 __read_mostly efer_reserved_bits = ~((u64)(EFER_SCE | EFER_LME | EFER_LMA));
#else
static u64 __read_mostly efer_reserved_bits = ~((u64)EFER_SCE);
#endif

static u64 __read_mostly cr4_reserved_bits = CR4_RESERVED_BITS;

#define KVM_EXIT_HYPERCALL_VALID_MASK (1 << KVM_HC_MAP_GPA_RANGE)

#define KVM_CAP_PMU_VALID_MASK KVM_PMU_CAP_DISABLE

#define KVM_X2APIC_API_VALID_FLAGS (KVM_X2APIC_API_USE_32BIT_IDS | \
                                    KVM_X2APIC_API_DISABLE_BROADCAST_QUIRK)

static void update_cr8_intercept(struct kvm_vcpu *vcpu);
static void process_nmi(struct kvm_vcpu *vcpu);
static void __kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags);
static void store_regs(struct kvm_vcpu *vcpu);
static int sync_regs(struct kvm_vcpu *vcpu);
static int kvm_vcpu_do_singlestep(struct kvm_vcpu *vcpu);

static int __set_sregs2(struct kvm_vcpu *vcpu, struct kvm_sregs2 *sregs2);
static void __get_sregs2(struct kvm_vcpu *vcpu, struct kvm_sregs2 *sregs2);

static DEFINE_MUTEX(vendor_module_lock);
struct kvm_x86_ops kvm_x86_ops __read_mostly;

#define KVM_X86_OP(func)					     \
	DEFINE_STATIC_CALL_NULL(kvm_x86_##func,			     \
				*(((struct kvm_x86_ops *)0)->func));
#define KVM_X86_OP_OPTIONAL KVM_X86_OP
#define KVM_X86_OP_OPTIONAL_RET0 KVM_X86_OP
#include <asm/kvm-x86-ops.h>
EXPORT_STATIC_CALL_GPL(kvm_x86_get_cs_db_l_bits);
EXPORT_STATIC_CALL_GPL(kvm_x86_cache_reg);

static bool __read_mostly ignore_msrs = 0;
module_param(ignore_msrs, bool, 0644);

bool __read_mostly report_ignored_msrs = true;
module_param(report_ignored_msrs, bool, 0644);
EXPORT_SYMBOL_GPL(report_ignored_msrs);

unsigned int min_timer_period_us = 200;
module_param(min_timer_period_us, uint, 0644);

static bool __read_mostly kvmclock_periodic_sync = true;
module_param(kvmclock_periodic_sync, bool, 0444);

/* tsc tolerance in parts per million - default to 1/2 of the NTP threshold */
static u32 __read_mostly tsc_tolerance_ppm = 250;
module_param(tsc_tolerance_ppm, uint, 0644);

static bool __read_mostly vector_hashing = true;
module_param(vector_hashing, bool, 0444);

bool __read_mostly enable_vmware_backdoor = false;
module_param(enable_vmware_backdoor, bool, 0444);
EXPORT_SYMBOL_GPL(enable_vmware_backdoor);

/*
 * Flags to manipulate forced emulation behavior (any non-zero value will
 * enable forced emulation).
 */
#define KVM_FEP_CLEAR_RFLAGS_RF	BIT(1)
static int __read_mostly force_emulation_prefix;
module_param(force_emulation_prefix, int, 0644);

int __read_mostly pi_inject_timer = -1;
module_param(pi_inject_timer, bint, 0644);

/* Enable/disable PMU virtualization */
bool __read_mostly enable_pmu = true;
EXPORT_SYMBOL_GPL(enable_pmu);
module_param(enable_pmu, bool, 0444);

bool __read_mostly eager_page_split = true;
module_param(eager_page_split, bool, 0644);

/* Enable/disable SMT_RSB bug mitigation */
static bool __read_mostly mitigate_smt_rsb;
module_param(mitigate_smt_rsb, bool, 0444);

/*
 * Restoring the host value for MSRs that are only consumed when running in
 * usermode, e.g. SYSCALL MSRs and TSC_AUX, can be deferred until the CPU
 * returns to userspace, i.e. the kernel can run with the guest's value.
 */
#define KVM_MAX_NR_USER_RETURN_MSRS 16

struct kvm_user_return_msrs {
	struct user_return_notifier urn;
	bool registered;
	struct kvm_user_return_msr_values {
		u64 host;
		u64 curr;
	} values[KVM_MAX_NR_USER_RETURN_MSRS];
};

u32 __read_mostly kvm_nr_uret_msrs;
EXPORT_SYMBOL_GPL(kvm_nr_uret_msrs);
static u32 __read_mostly kvm_uret_msrs_list[KVM_MAX_NR_USER_RETURN_MSRS];
static struct kvm_user_return_msrs __percpu *user_return_msrs;

#define KVM_SUPPORTED_XCR0     (XFEATURE_MASK_FP | XFEATURE_MASK_SSE \
				| XFEATURE_MASK_YMM | XFEATURE_MASK_BNDREGS \
				| XFEATURE_MASK_BNDCSR | XFEATURE_MASK_AVX512 \
				| XFEATURE_MASK_PKRU | XFEATURE_MASK_XTILE)

bool __read_mostly allow_smaller_maxphyaddr = 0;
EXPORT_SYMBOL_GPL(allow_smaller_maxphyaddr);

bool __read_mostly enable_apicv = true;
EXPORT_SYMBOL_GPL(enable_apicv);

const struct _kvm_stats_desc kvm_vm_stats_desc[] = {
	KVM_GENERIC_VM_STATS(),
	STATS_DESC_COUNTER(VM, mmu_shadow_zapped),
	STATS_DESC_COUNTER(VM, mmu_pte_write),
	STATS_DESC_COUNTER(VM, mmu_pde_zapped),
	STATS_DESC_COUNTER(VM, mmu_flooded),
	STATS_DESC_COUNTER(VM, mmu_recycled),
	STATS_DESC_COUNTER(VM, mmu_cache_miss),
	STATS_DESC_ICOUNTER(VM, mmu_unsync),
	STATS_DESC_ICOUNTER(VM, pages_4k),
	STATS_DESC_ICOUNTER(VM, pages_2m),
	STATS_DESC_ICOUNTER(VM, pages_1g),
	STATS_DESC_ICOUNTER(VM, nx_lpage_splits),
	STATS_DESC_PCOUNTER(VM, max_mmu_rmap_size),
	STATS_DESC_PCOUNTER(VM, max_mmu_page_hash_collisions)
};

const struct kvm_stats_header kvm_vm_stats_header = {
	.name_size = KVM_STATS_NAME_SIZE,
	.num_desc = ARRAY_SIZE(kvm_vm_stats_desc),
	.id_offset = sizeof(struct kvm_stats_header),
	.desc_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE,
	.data_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE +
		       sizeof(kvm_vm_stats_desc),
};

const struct _kvm_stats_desc kvm_vcpu_stats_desc[] = {
	KVM_GENERIC_VCPU_STATS(),
	STATS_DESC_COUNTER(VCPU, pf_taken),
	STATS_DESC_COUNTER(VCPU, pf_fixed),
	STATS_DESC_COUNTER(VCPU, pf_emulate),
	STATS_DESC_COUNTER(VCPU, pf_spurious),
	STATS_DESC_COUNTER(VCPU, pf_fast),
	STATS_DESC_COUNTER(VCPU, pf_mmio_spte_created),
	STATS_DESC_COUNTER(VCPU, pf_guest),
	STATS_DESC_COUNTER(VCPU, tlb_flush),
	STATS_DESC_COUNTER(VCPU, invlpg),
	STATS_DESC_COUNTER(VCPU, exits),
	STATS_DESC_COUNTER(VCPU, io_exits),
	STATS_DESC_COUNTER(VCPU, mmio_exits),
	STATS_DESC_COUNTER(VCPU, signal_exits),
	STATS_DESC_COUNTER(VCPU, irq_window_exits),
	STATS_DESC_COUNTER(VCPU, nmi_window_exits),
	STATS_DESC_COUNTER(VCPU, l1d_flush),
	STATS_DESC_COUNTER(VCPU, halt_exits),
	STATS_DESC_COUNTER(VCPU, request_irq_exits),
	STATS_DESC_COUNTER(VCPU, irq_exits),
	STATS_DESC_COUNTER(VCPU, host_state_reload),
	STATS_DESC_COUNTER(VCPU, fpu_reload),
	STATS_DESC_COUNTER(VCPU, insn_emulation),
	STATS_DESC_COUNTER(VCPU, insn_emulation_fail),
	STATS_DESC_COUNTER(VCPU, hypercalls),
	STATS_DESC_COUNTER(VCPU, irq_injections),
	STATS_DESC_COUNTER(VCPU, nmi_injections),
	STATS_DESC_COUNTER(VCPU, req_event),
	STATS_DESC_COUNTER(VCPU, nested_run),
	STATS_DESC_COUNTER(VCPU, directed_yield_attempted),
	STATS_DESC_COUNTER(VCPU, directed_yield_successful),
	STATS_DESC_COUNTER(VCPU, preemption_reported),
	STATS_DESC_COUNTER(VCPU, preemption_other),
	STATS_DESC_IBOOLEAN(VCPU, guest_mode),
	STATS_DESC_COUNTER(VCPU, notify_window_exits),
};

const struct kvm_stats_header kvm_vcpu_stats_header = {
	.name_size = KVM_STATS_NAME_SIZE,
	.num_desc = ARRAY_SIZE(kvm_vcpu_stats_desc),
	.id_offset = sizeof(struct kvm_stats_header),
	.desc_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE,
	.data_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE +
		       sizeof(kvm_vcpu_stats_desc),
};

static struct kmem_cache *x86_emulator_cache;

/*
 * The three MSR lists(msrs_to_save, emulated_msrs, msr_based_features) track
 * the set of MSRs that KVM exposes to userspace through KVM_GET_MSRS,
 * KVM_SET_MSRS, and KVM_GET_MSR_INDEX_LIST.  msrs_to_save holds MSRs that
 * require host support, i.e. should be probed via RDMSR.  emulated_msrs holds
 * MSRs that KVM emulates without strictly requiring host support.
 * msr_based_features holds MSRs that enumerate features, i.e. are effectively
 * CPUID leafs.  Note, msr_based_features isn't mutually exclusive with
 * msrs_to_save and emulated_msrs.
 */

static const u32 msrs_to_save_base[] = {
	MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
	MSR_STAR,
#ifdef CONFIG_X86_64
	MSR_CSTAR, MSR_KERNEL_GS_BASE, MSR_SYSCALL_MASK, MSR_LSTAR,
#endif
	MSR_IA32_TSC, MSR_IA32_CR_PAT, MSR_VM_HSAVE_PA,
	MSR_IA32_FEAT_CTL, MSR_IA32_BNDCFGS, MSR_TSC_AUX,
	MSR_IA32_SPEC_CTRL, MSR_IA32_TSX_CTRL,
	MSR_IA32_RTIT_CTL, MSR_IA32_RTIT_STATUS, MSR_IA32_RTIT_CR3_MATCH,
	MSR_IA32_RTIT_OUTPUT_BASE, MSR_IA32_RTIT_OUTPUT_MASK,
	MSR_IA32_RTIT_ADDR0_A, MSR_IA32_RTIT_ADDR0_B,
	MSR_IA32_RTIT_ADDR1_A, MSR_IA32_RTIT_ADDR1_B,
	MSR_IA32_RTIT_ADDR2_A, MSR_IA32_RTIT_ADDR2_B,
	MSR_IA32_RTIT_ADDR3_A, MSR_IA32_RTIT_ADDR3_B,
	MSR_IA32_UMWAIT_CONTROL,

	MSR_IA32_XFD, MSR_IA32_XFD_ERR,
};

static const u32 msrs_to_save_pmu[] = {
	MSR_ARCH_PERFMON_FIXED_CTR0, MSR_ARCH_PERFMON_FIXED_CTR1,
	MSR_ARCH_PERFMON_FIXED_CTR0 + 2,
	MSR_CORE_PERF_FIXED_CTR_CTRL, MSR_CORE_PERF_GLOBAL_STATUS,
	MSR_CORE_PERF_GLOBAL_CTRL,
	MSR_IA32_PEBS_ENABLE, MSR_IA32_DS_AREA, MSR_PEBS_DATA_CFG,

	/* This part of MSRs should match KVM_MAX_NR_INTEL_GP_COUNTERS. */
	MSR_ARCH_PERFMON_PERFCTR0, MSR_ARCH_PERFMON_PERFCTR1,
	MSR_ARCH_PERFMON_PERFCTR0 + 2, MSR_ARCH_PERFMON_PERFCTR0 + 3,
	MSR_ARCH_PERFMON_PERFCTR0 + 4, MSR_ARCH_PERFMON_PERFCTR0 + 5,
	MSR_ARCH_PERFMON_PERFCTR0 + 6, MSR_ARCH_PERFMON_PERFCTR0 + 7,
	MSR_ARCH_PERFMON_EVENTSEL0, MSR_ARCH_PERFMON_EVENTSEL1,
	MSR_ARCH_PERFMON_EVENTSEL0 + 2, MSR_ARCH_PERFMON_EVENTSEL0 + 3,
	MSR_ARCH_PERFMON_EVENTSEL0 + 4, MSR_ARCH_PERFMON_EVENTSEL0 + 5,
	MSR_ARCH_PERFMON_EVENTSEL0 + 6, MSR_ARCH_PERFMON_EVENTSEL0 + 7,

	MSR_K7_EVNTSEL0, MSR_K7_EVNTSEL1, MSR_K7_EVNTSEL2, MSR_K7_EVNTSEL3,
	MSR_K7_PERFCTR0, MSR_K7_PERFCTR1, MSR_K7_PERFCTR2, MSR_K7_PERFCTR3,

	/* This part of MSRs should match KVM_MAX_NR_AMD_GP_COUNTERS. */
	MSR_F15H_PERF_CTL0, MSR_F15H_PERF_CTL1, MSR_F15H_PERF_CTL2,
	MSR_F15H_PERF_CTL3, MSR_F15H_PERF_CTL4, MSR_F15H_PERF_CTL5,
	MSR_F15H_PERF_CTR0, MSR_F15H_PERF_CTR1, MSR_F15H_PERF_CTR2,
	MSR_F15H_PERF_CTR3, MSR_F15H_PERF_CTR4, MSR_F15H_PERF_CTR5,

	MSR_AMD64_PERF_CNTR_GLOBAL_CTL,
	MSR_AMD64_PERF_CNTR_GLOBAL_STATUS,
	MSR_AMD64_PERF_CNTR_GLOBAL_STATUS_CLR,
};

static u32 msrs_to_save[ARRAY_SIZE(msrs_to_save_base) +
			ARRAY_SIZE(msrs_to_save_pmu)];
static unsigned num_msrs_to_save;

static const u32 emulated_msrs_all[] = {
	MSR_KVM_SYSTEM_TIME, MSR_KVM_WALL_CLOCK,
	MSR_KVM_SYSTEM_TIME_NEW, MSR_KVM_WALL_CLOCK_NEW,

#ifdef CONFIG_KVM_HYPERV
	HV_X64_MSR_GUEST_OS_ID, HV_X64_MSR_HYPERCALL,
	HV_X64_MSR_TIME_REF_COUNT, HV_X64_MSR_REFERENCE_TSC,
	HV_X64_MSR_TSC_FREQUENCY, HV_X64_MSR_APIC_FREQUENCY,
	HV_X64_MSR_CRASH_P0, HV_X64_MSR_CRASH_P1, HV_X64_MSR_CRASH_P2,
	HV_X64_MSR_CRASH_P3, HV_X64_MSR_CRASH_P4, HV_X64_MSR_CRASH_CTL,
	HV_X64_MSR_RESET,
	HV_X64_MSR_VP_INDEX,
	HV_X64_MSR_VP_RUNTIME,
	HV_X64_MSR_SCONTROL,
	HV_X64_MSR_STIMER0_CONFIG,
	HV_X64_MSR_VP_ASSIST_PAGE,
	HV_X64_MSR_REENLIGHTENMENT_CONTROL, HV_X64_MSR_TSC_EMULATION_CONTROL,
	HV_X64_MSR_TSC_EMULATION_STATUS, HV_X64_MSR_TSC_INVARIANT_CONTROL,
	HV_X64_MSR_SYNDBG_OPTIONS,
	HV_X64_MSR_SYNDBG_CONTROL, HV_X64_MSR_SYNDBG_STATUS,
	HV_X64_MSR_SYNDBG_SEND_BUFFER, HV_X64_MSR_SYNDBG_RECV_BUFFER,
	HV_X64_MSR_SYNDBG_PENDING_BUFFER,
#endif

	MSR_KVM_ASYNC_PF_EN, MSR_KVM_STEAL_TIME,
	MSR_KVM_PV_EOI_EN, MSR_KVM_ASYNC_PF_INT, MSR_KVM_ASYNC_PF_ACK,

	MSR_IA32_TSC_ADJUST,
	MSR_IA32_TSC_DEADLINE,
	MSR_IA32_ARCH_CAPABILITIES,
	MSR_IA32_PERF_CAPABILITIES,
	MSR_IA32_MISC_ENABLE,
	MSR_IA32_MCG_STATUS,
	MSR_IA32_MCG_CTL,
	MSR_IA32_MCG_EXT_CTL,
	MSR_IA32_SMBASE,
	MSR_SMI_COUNT,
	MSR_PLATFORM_INFO,
	MSR_MISC_FEATURES_ENABLES,
	MSR_AMD64_VIRT_SPEC_CTRL,
	MSR_AMD64_TSC_RATIO,
	MSR_IA32_POWER_CTL,
	MSR_IA32_UCODE_REV,

	/*
	 * KVM always supports the "true" VMX control MSRs, even if the host
	 * does not.  The VMX MSRs as a whole are considered "emulated" as KVM
	 * doesn't strictly require them to exist in the host (ignoring that
	 * KVM would refuse to load in the first place if the core set of MSRs
	 * aren't supported).
	 */
	MSR_IA32_VMX_BASIC,
	MSR_IA32_VMX_TRUE_PINBASED_CTLS,
	MSR_IA32_VMX_TRUE_PROCBASED_CTLS,
	MSR_IA32_VMX_TRUE_EXIT_CTLS,
	MSR_IA32_VMX_TRUE_ENTRY_CTLS,
	MSR_IA32_VMX_MISC,
	MSR_IA32_VMX_CR0_FIXED0,
	MSR_IA32_VMX_CR4_FIXED0,
	MSR_IA32_VMX_VMCS_ENUM,
	MSR_IA32_VMX_PROCBASED_CTLS2,
	MSR_IA32_VMX_EPT_VPID_CAP,
	MSR_IA32_VMX_VMFUNC,

	MSR_K7_HWCR,
	MSR_KVM_POLL_CONTROL,
};

static u32 emulated_msrs[ARRAY_SIZE(emulated_msrs_all)];
static unsigned num_emulated_msrs;

/*
 * List of MSRs that control the existence of MSR-based features, i.e. MSRs
 * that are effectively CPUID leafs.  VMX MSRs are also included in the set of
 * feature MSRs, but are handled separately to allow expedited lookups.
 */
static const u32 msr_based_features_all_except_vmx[] = {
	MSR_AMD64_DE_CFG,
	MSR_IA32_UCODE_REV,
	MSR_IA32_ARCH_CAPABILITIES,
	MSR_IA32_PERF_CAPABILITIES,
	MSR_PLATFORM_INFO,
};

static u32 msr_based_features[ARRAY_SIZE(msr_based_features_all_except_vmx) +
			      (KVM_LAST_EMULATED_VMX_MSR - KVM_FIRST_EMULATED_VMX_MSR + 1)];
static unsigned int num_msr_based_features;

/*
 * All feature MSRs except uCode revID, which tracks the currently loaded uCode
 * patch, are immutable once the vCPU model is defined.
 */
static bool kvm_is_immutable_feature_msr(u32 msr)
{
	int i;

	if (msr >= KVM_FIRST_EMULATED_VMX_MSR && msr <= KVM_LAST_EMULATED_VMX_MSR)
		return true;

	for (i = 0; i < ARRAY_SIZE(msr_based_features_all_except_vmx); i++) {
		if (msr == msr_based_features_all_except_vmx[i])
			return msr != MSR_IA32_UCODE_REV;
	}

	return false;
}

static bool kvm_is_advertised_msr(u32 msr_index)
{
	unsigned int i;

	for (i = 0; i < num_msrs_to_save; i++) {
		if (msrs_to_save[i] == msr_index)
			return true;
	}

	for (i = 0; i < num_emulated_msrs; i++) {
		if (emulated_msrs[i] == msr_index)
			return true;
	}

	return false;
}

typedef int (*msr_access_t)(struct kvm_vcpu *vcpu, u32 index, u64 *data,
			    bool host_initiated);

static __always_inline int kvm_do_msr_access(struct kvm_vcpu *vcpu, u32 msr,
					     u64 *data, bool host_initiated,
					     enum kvm_msr_access rw,
					     msr_access_t msr_access_fn)
{
	const char *op = rw == MSR_TYPE_W ? "wrmsr" : "rdmsr";
	int ret;

	BUILD_BUG_ON(rw != MSR_TYPE_R && rw != MSR_TYPE_W);

	/*
	 * Zero the data on read failures to avoid leaking stack data to the
	 * guest and/or userspace, e.g. if the failure is ignored below.
	 */
	ret = msr_access_fn(vcpu, msr, data, host_initiated);
	if (ret && rw == MSR_TYPE_R)
		*data = 0;

	if (ret != KVM_MSR_RET_UNSUPPORTED)
		return ret;

	/*
	 * Userspace is allowed to read MSRs, and write '0' to MSRs, that KVM
	 * advertises to userspace, even if an MSR isn't fully supported.
	 * Simply check that @data is '0', which covers both the write '0' case
	 * and all reads (in which case @data is zeroed on failure; see above).
	 */
	if (host_initiated && !*data && kvm_is_advertised_msr(msr))
		return 0;

	if (!ignore_msrs) {
		kvm_debug_ratelimited("unhandled %s: 0x%x data 0x%llx\n",
				      op, msr, *data);
		return ret;
	}

	if (report_ignored_msrs)
		kvm_pr_unimpl("ignored %s: 0x%x data 0x%llx\n", op, msr, *data);

	return 0;
}

static struct kmem_cache *kvm_alloc_emulator_cache(void)
{
	unsigned int useroffset = offsetof(struct x86_emulate_ctxt, src);
	unsigned int size = sizeof(struct x86_emulate_ctxt);

	return kmem_cache_create_usercopy("x86_emulator", size,
					  __alignof__(struct x86_emulate_ctxt),
					  SLAB_ACCOUNT, useroffset,
					  size - useroffset, NULL);
}

static int emulator_fix_hypercall(struct x86_emulate_ctxt *ctxt);

static inline void kvm_async_pf_hash_reset(struct kvm_vcpu *vcpu)
{
	int i;
	for (i = 0; i < ASYNC_PF_PER_VCPU; i++)
		vcpu->arch.apf.gfns[i] = ~0;
}

static void kvm_on_user_return(struct user_return_notifier *urn)
{
	unsigned slot;
	struct kvm_user_return_msrs *msrs
		= container_of(urn, struct kvm_user_return_msrs, urn);
	struct kvm_user_return_msr_values *values;
	unsigned long flags;

	/*
	 * Disabling irqs at this point since the following code could be
	 * interrupted and executed through kvm_arch_disable_virtualization_cpu()
	 */
	local_irq_save(flags);
	if (msrs->registered) {
		msrs->registered = false;
		user_return_notifier_unregister(urn);
	}
	local_irq_restore(flags);
	for (slot = 0; slot < kvm_nr_uret_msrs; ++slot) {
		values = &msrs->values[slot];
		if (values->host != values->curr) {
			wrmsrl(kvm_uret_msrs_list[slot], values->host);
			values->curr = values->host;
		}
	}
}

static int kvm_probe_user_return_msr(u32 msr)
{
	u64 val;
	int ret;

	preempt_disable();
	ret = rdmsrl_safe(msr, &val);
	if (ret)
		goto out;
	ret = wrmsrl_safe(msr, val);
out:
	preempt_enable();
	return ret;
}

int kvm_add_user_return_msr(u32 msr)
{
	BUG_ON(kvm_nr_uret_msrs >= KVM_MAX_NR_USER_RETURN_MSRS);

	if (kvm_probe_user_return_msr(msr))
		return -1;

	kvm_uret_msrs_list[kvm_nr_uret_msrs] = msr;
	return kvm_nr_uret_msrs++;
}
EXPORT_SYMBOL_GPL(kvm_add_user_return_msr);

int kvm_find_user_return_msr(u32 msr)
{
	int i;

	for (i = 0; i < kvm_nr_uret_msrs; ++i) {
		if (kvm_uret_msrs_list[i] == msr)
			return i;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(kvm_find_user_return_msr);

static void kvm_user_return_msr_cpu_online(void)
{
	struct kvm_user_return_msrs *msrs = this_cpu_ptr(user_return_msrs);
	u64 value;
	int i;

	for (i = 0; i < kvm_nr_uret_msrs; ++i) {
		rdmsrl_safe(kvm_uret_msrs_list[i], &value);
		msrs->values[i].host = value;
		msrs->values[i].curr = value;
	}
}

int kvm_set_user_return_msr(unsigned slot, u64 value, u64 mask)
{
	struct kvm_user_return_msrs *msrs = this_cpu_ptr(user_return_msrs);
	int err;

	value = (value & mask) | (msrs->values[slot].host & ~mask);
	if (value == msrs->values[slot].curr)
		return 0;
	err = wrmsrl_safe(kvm_uret_msrs_list[slot], value);
	if (err)
		return 1;

	msrs->values[slot].curr = value;
	if (!msrs->registered) {
		msrs->urn.on_user_return = kvm_on_user_return;
		user_return_notifier_register(&msrs->urn);
		msrs->registered = true;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_user_return_msr);

static void drop_user_return_notifiers(void)
{
	struct kvm_user_return_msrs *msrs = this_cpu_ptr(user_return_msrs);

	if (msrs->registered)
		kvm_on_user_return(&msrs->urn);
}

/*
 * Handle a fault on a hardware virtualization (VMX or SVM) instruction.
 *
 * Hardware virtualization extension instructions may fault if a reboot turns
 * off virtualization while processes are running.  Usually after catching the
 * fault we just panic; during reboot instead the instruction is ignored.
 */
noinstr void kvm_spurious_fault(void)
{
	/* Fault while not rebooting.  We want the trace. */
	BUG_ON(!kvm_rebooting);
}
EXPORT_SYMBOL_GPL(kvm_spurious_fault);

#define EXCPT_BENIGN		0
#define EXCPT_CONTRIBUTORY	1
#define EXCPT_PF		2

static int exception_class(int vector)
{
	switch (vector) {
	case PF_VECTOR:
		return EXCPT_PF;
	case DE_VECTOR:
	case TS_VECTOR:
	case NP_VECTOR:
	case SS_VECTOR:
	case GP_VECTOR:
		return EXCPT_CONTRIBUTORY;
	default:
		break;
	}
	return EXCPT_BENIGN;
}

#define EXCPT_FAULT		0
#define EXCPT_TRAP		1
#define EXCPT_ABORT		2
#define EXCPT_INTERRUPT		3
#define EXCPT_DB		4

static int exception_type(int vector)
{
	unsigned int mask;

	if (WARN_ON(vector > 31 || vector == NMI_VECTOR))
		return EXCPT_INTERRUPT;

	mask = 1 << vector;

	/*
	 * #DBs can be trap-like or fault-like, the caller must check other CPU
	 * state, e.g. DR6, to determine whether a #DB is a trap or fault.
	 */
	if (mask & (1 << DB_VECTOR))
		return EXCPT_DB;

	if (mask & ((1 << BP_VECTOR) | (1 << OF_VECTOR)))
		return EXCPT_TRAP;

	if (mask & ((1 << DF_VECTOR) | (1 << MC_VECTOR)))
		return EXCPT_ABORT;

	/* Reserved exceptions will result in fault */
	return EXCPT_FAULT;
}

void kvm_deliver_exception_payload(struct kvm_vcpu *vcpu,
				   struct kvm_queued_exception *ex)
{
	if (!ex->has_payload)
		return;

	switch (ex->vector) {
	case DB_VECTOR:
		/*
		 * "Certain debug exceptions may clear bit 0-3.  The
		 * remaining contents of the DR6 register are never
		 * cleared by the processor".
		 */
		vcpu->arch.dr6 &= ~DR_TRAP_BITS;
		/*
		 * In order to reflect the #DB exception payload in guest
		 * dr6, three components need to be considered: active low
		 * bit, FIXED_1 bits and active high bits (e.g. DR6_BD,
		 * DR6_BS and DR6_BT)
		 * DR6_ACTIVE_LOW contains the FIXED_1 and active low bits.
		 * In the target guest dr6:
		 * FIXED_1 bits should always be set.
		 * Active low bits should be cleared if 1-setting in payload.
		 * Active high bits should be set if 1-setting in payload.
		 *
		 * Note, the payload is compatible with the pending debug
		 * exceptions/exit qualification under VMX, that active_low bits
		 * are active high in payload.
		 * So they need to be flipped for DR6.
		 */
		vcpu->arch.dr6 |= DR6_ACTIVE_LOW;
		vcpu->arch.dr6 |= ex->payload;
		vcpu->arch.dr6 ^= ex->payload & DR6_ACTIVE_LOW;

		/*
		 * The #DB payload is defined as compatible with the 'pending
		 * debug exceptions' field under VMX, not DR6. While bit 12 is
		 * defined in the 'pending debug exceptions' field (enabled
		 * breakpoint), it is reserved and must be zero in DR6.
		 */
		vcpu->arch.dr6 &= ~BIT(12);
		break;
	case PF_VECTOR:
		vcpu->arch.cr2 = ex->payload;
		break;
	}

	ex->has_payload = false;
	ex->payload = 0;
}
EXPORT_SYMBOL_GPL(kvm_deliver_exception_payload);

static void kvm_queue_exception_vmexit(struct kvm_vcpu *vcpu, unsigned int vector,
				       bool has_error_code, u32 error_code,
				       bool has_payload, unsigned long payload)
{
	struct kvm_queued_exception *ex = &vcpu->arch.exception_vmexit;

	ex->vector = vector;
	ex->injected = false;
	ex->pending = true;
	ex->has_error_code = has_error_code;
	ex->error_code = error_code;
	ex->has_payload = has_payload;
	ex->payload = payload;
}

static void kvm_multiple_exception(struct kvm_vcpu *vcpu,
		unsigned nr, bool has_error, u32 error_code,
	        bool has_payload, unsigned long payload, bool reinject)
{
	u32 prev_nr;
	int class1, class2;

	kvm_make_request(KVM_REQ_EVENT, vcpu);

	/*
	 * If the exception is destined for L2 and isn't being reinjected,
	 * morph it to a VM-Exit if L1 wants to intercept the exception.  A
	 * previously injected exception is not checked because it was checked
	 * when it was original queued, and re-checking is incorrect if _L1_
	 * injected the exception, in which case it's exempt from interception.
	 */
	if (!reinject && is_guest_mode(vcpu) &&
	    kvm_x86_ops.nested_ops->is_exception_vmexit(vcpu, nr, error_code)) {
		kvm_queue_exception_vmexit(vcpu, nr, has_error, error_code,
					   has_payload, payload);
		return;
	}

	if (!vcpu->arch.exception.pending && !vcpu->arch.exception.injected) {
	queue:
		if (reinject) {
			/*
			 * On VM-Entry, an exception can be pending if and only
			 * if event injection was blocked by nested_run_pending.
			 * In that case, however, vcpu_enter_guest() requests an
			 * immediate exit, and the guest shouldn't proceed far
			 * enough to need reinjection.
			 */
			WARN_ON_ONCE(kvm_is_exception_pending(vcpu));
			vcpu->arch.exception.injected = true;
			if (WARN_ON_ONCE(has_payload)) {
				/*
				 * A reinjected event has already
				 * delivered its payload.
				 */
				has_payload = false;
				payload = 0;
			}
		} else {
			vcpu->arch.exception.pending = true;
			vcpu->arch.exception.injected = false;
		}
		vcpu->arch.exception.has_error_code = has_error;
		vcpu->arch.exception.vector = nr;
		vcpu->arch.exception.error_code = error_code;
		vcpu->arch.exception.has_payload = has_payload;
		vcpu->arch.exception.payload = payload;
		if (!is_guest_mode(vcpu))
			kvm_deliver_exception_payload(vcpu,
						      &vcpu->arch.exception);
		return;
	}

	/* to check exception */
	prev_nr = vcpu->arch.exception.vector;
	if (prev_nr == DF_VECTOR) {
		/* triple fault -> shutdown */
		kvm_make_request(KVM_REQ_TRIPLE_FAULT, vcpu);
		return;
	}
	class1 = exception_class(prev_nr);
	class2 = exception_class(nr);
	if ((class1 == EXCPT_CONTRIBUTORY && class2 == EXCPT_CONTRIBUTORY) ||
	    (class1 == EXCPT_PF && class2 != EXCPT_BENIGN)) {
		/*
		 * Synthesize #DF.  Clear the previously injected or pending
		 * exception so as not to incorrectly trigger shutdown.
		 */
		vcpu->arch.exception.injected = false;
		vcpu->arch.exception.pending = false;

		kvm_queue_exception_e(vcpu, DF_VECTOR, 0);
	} else {
		/* replace previous exception with a new one in a hope
		   that instruction re-execution will regenerate lost
		   exception */
		goto queue;
	}
}

void kvm_queue_exception(struct kvm_vcpu *vcpu, unsigned nr)
{
	kvm_multiple_exception(vcpu, nr, false, 0, false, 0, false);
}
EXPORT_SYMBOL_GPL(kvm_queue_exception);

void kvm_requeue_exception(struct kvm_vcpu *vcpu, unsigned nr)
{
	kvm_multiple_exception(vcpu, nr, false, 0, false, 0, true);
}
EXPORT_SYMBOL_GPL(kvm_requeue_exception);

void kvm_queue_exception_p(struct kvm_vcpu *vcpu, unsigned nr,
			   unsigned long payload)
{
	kvm_multiple_exception(vcpu, nr, false, 0, true, payload, false);
}
EXPORT_SYMBOL_GPL(kvm_queue_exception_p);

static void kvm_queue_exception_e_p(struct kvm_vcpu *vcpu, unsigned nr,
				    u32 error_code, unsigned long payload)
{
	kvm_multiple_exception(vcpu, nr, true, error_code,
			       true, payload, false);
}

int kvm_complete_insn_gp(struct kvm_vcpu *vcpu, int err)
{
	if (err)
		kvm_inject_gp(vcpu, 0);
	else
		return kvm_skip_emulated_instruction(vcpu);

	return 1;
}
EXPORT_SYMBOL_GPL(kvm_complete_insn_gp);

static int complete_emulated_insn_gp(struct kvm_vcpu *vcpu, int err)
{
	if (err) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	return kvm_emulate_instruction(vcpu, EMULTYPE_NO_DECODE | EMULTYPE_SKIP |
				       EMULTYPE_COMPLETE_USER_EXIT);
}

void kvm_inject_page_fault(struct kvm_vcpu *vcpu, struct x86_exception *fault)
{
	++vcpu->stat.pf_guest;

	/*
	 * Async #PF in L2 is always forwarded to L1 as a VM-Exit regardless of
	 * whether or not L1 wants to intercept "regular" #PF.
	 */
	if (is_guest_mode(vcpu) && fault->async_page_fault)
		kvm_queue_exception_vmexit(vcpu, PF_VECTOR,
					   true, fault->error_code,
					   true, fault->address);
	else
		kvm_queue_exception_e_p(vcpu, PF_VECTOR, fault->error_code,
					fault->address);
}

void kvm_inject_emulated_page_fault(struct kvm_vcpu *vcpu,
				    struct x86_exception *fault)
{
	struct kvm_mmu *fault_mmu;
	WARN_ON_ONCE(fault->vector != PF_VECTOR);

	fault_mmu = fault->nested_page_fault ? vcpu->arch.mmu :
					       vcpu->arch.walk_mmu;

	/*
	 * Invalidate the TLB entry for the faulting address, if it exists,
	 * else the access will fault indefinitely (and to emulate hardware).
	 */
	if ((fault->error_code & PFERR_PRESENT_MASK) &&
	    !(fault->error_code & PFERR_RSVD_MASK))
		kvm_mmu_invalidate_addr(vcpu, fault_mmu, fault->address,
					KVM_MMU_ROOT_CURRENT);

	fault_mmu->inject_page_fault(vcpu, fault);
}
EXPORT_SYMBOL_GPL(kvm_inject_emulated_page_fault);

void kvm_inject_nmi(struct kvm_vcpu *vcpu)
{
	atomic_inc(&vcpu->arch.nmi_queued);
	kvm_make_request(KVM_REQ_NMI, vcpu);
}

void kvm_queue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code)
{
	kvm_multiple_exception(vcpu, nr, true, error_code, false, 0, false);
}
EXPORT_SYMBOL_GPL(kvm_queue_exception_e);

void kvm_requeue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code)
{
	kvm_multiple_exception(vcpu, nr, true, error_code, false, 0, true);
}
EXPORT_SYMBOL_GPL(kvm_requeue_exception_e);

/*
 * Checks if cpl <= required_cpl; if true, return true.  Otherwise queue
 * a #GP and return false.
 */
bool kvm_require_cpl(struct kvm_vcpu *vcpu, int required_cpl)
{
	if (kvm_x86_call(get_cpl)(vcpu) <= required_cpl)
		return true;
	kvm_queue_exception_e(vcpu, GP_VECTOR, 0);
	return false;
}

bool kvm_require_dr(struct kvm_vcpu *vcpu, int dr)
{
	if ((dr != 4 && dr != 5) || !kvm_is_cr4_bit_set(vcpu, X86_CR4_DE))
		return true;

	kvm_queue_exception(vcpu, UD_VECTOR);
	return false;
}
EXPORT_SYMBOL_GPL(kvm_require_dr);

static inline u64 pdptr_rsvd_bits(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.reserved_gpa_bits | rsvd_bits(5, 8) | rsvd_bits(1, 2);
}

/*
 * Load the pae pdptrs.  Return 1 if they are all valid, 0 otherwise.
 */
int load_pdptrs(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;
	gfn_t pdpt_gfn = cr3 >> PAGE_SHIFT;
	gpa_t real_gpa;
	int i;
	int ret;
	u64 pdpte[ARRAY_SIZE(mmu->pdptrs)];

	/*
	 * If the MMU is nested, CR3 holds an L2 GPA and needs to be translated
	 * to an L1 GPA.
	 */
	real_gpa = kvm_translate_gpa(vcpu, mmu, gfn_to_gpa(pdpt_gfn),
				     PFERR_USER_MASK | PFERR_WRITE_MASK, NULL);
	if (real_gpa == INVALID_GPA)
		return 0;

	/* Note the offset, PDPTRs are 32 byte aligned when using PAE paging. */
	ret = kvm_vcpu_read_guest_page(vcpu, gpa_to_gfn(real_gpa), pdpte,
				       cr3 & GENMASK(11, 5), sizeof(pdpte));
	if (ret < 0)
		return 0;

	for (i = 0; i < ARRAY_SIZE(pdpte); ++i) {
		if ((pdpte[i] & PT_PRESENT_MASK) &&
		    (pdpte[i] & pdptr_rsvd_bits(vcpu))) {
			return 0;
		}
	}

	/*
	 * Marking VCPU_EXREG_PDPTR dirty doesn't work for !tdp_enabled.
	 * Shadow page roots need to be reconstructed instead.
	 */
	if (!tdp_enabled && memcmp(mmu->pdptrs, pdpte, sizeof(mmu->pdptrs)))
		kvm_mmu_free_roots(vcpu->kvm, mmu, KVM_MMU_ROOT_CURRENT);

	memcpy(mmu->pdptrs, pdpte, sizeof(mmu->pdptrs));
	kvm_register_mark_dirty(vcpu, VCPU_EXREG_PDPTR);
	kvm_make_request(KVM_REQ_LOAD_MMU_PGD, vcpu);
	vcpu->arch.pdptrs_from_userspace = false;

	return 1;
}
EXPORT_SYMBOL_GPL(load_pdptrs);

static bool kvm_is_valid_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
#ifdef CONFIG_X86_64
	if (cr0 & 0xffffffff00000000UL)
		return false;
#endif

	if ((cr0 & X86_CR0_NW) && !(cr0 & X86_CR0_CD))
		return false;

	if ((cr0 & X86_CR0_PG) && !(cr0 & X86_CR0_PE))
		return false;

	return kvm_x86_call(is_valid_cr0)(vcpu, cr0);
}

void kvm_post_set_cr0(struct kvm_vcpu *vcpu, unsigned long old_cr0, unsigned long cr0)
{
	/*
	 * CR0.WP is incorporated into the MMU role, but only for non-nested,
	 * indirect shadow MMUs.  If paging is disabled, no updates are needed
	 * as there are no permission bits to emulate.  If TDP is enabled, the
	 * MMU's metadata needs to be updated, e.g. so that emulating guest
	 * translations does the right thing, but there's no need to unload the
	 * root as CR0.WP doesn't affect SPTEs.
	 */
	if ((cr0 ^ old_cr0) == X86_CR0_WP) {
		if (!(cr0 & X86_CR0_PG))
			return;

		if (tdp_enabled) {
			kvm_init_mmu(vcpu);
			return;
		}
	}

	if ((cr0 ^ old_cr0) & X86_CR0_PG) {
		kvm_clear_async_pf_completion_queue(vcpu);
		kvm_async_pf_hash_reset(vcpu);

		/*
		 * Clearing CR0.PG is defined to flush the TLB from the guest's
		 * perspective.
		 */
		if (!(cr0 & X86_CR0_PG))
			kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);
	}

	if ((cr0 ^ old_cr0) & KVM_MMU_CR0_ROLE_BITS)
		kvm_mmu_reset_context(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_post_set_cr0);

int kvm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	unsigned long old_cr0 = kvm_read_cr0(vcpu);

	if (!kvm_is_valid_cr0(vcpu, cr0))
		return 1;

	cr0 |= X86_CR0_ET;

	/* Write to CR0 reserved bits are ignored, even on Intel. */
	cr0 &= ~CR0_RESERVED_BITS;

#ifdef CONFIG_X86_64
	if ((vcpu->arch.efer & EFER_LME) && !is_paging(vcpu) &&
	    (cr0 & X86_CR0_PG)) {
		int cs_db, cs_l;

		if (!is_pae(vcpu))
			return 1;
		kvm_x86_call(get_cs_db_l_bits)(vcpu, &cs_db, &cs_l);
		if (cs_l)
			return 1;
	}
#endif
	if (!(vcpu->arch.efer & EFER_LME) && (cr0 & X86_CR0_PG) &&
	    is_pae(vcpu) && ((cr0 ^ old_cr0) & X86_CR0_PDPTR_BITS) &&
	    !load_pdptrs(vcpu, kvm_read_cr3(vcpu)))
		return 1;

	if (!(cr0 & X86_CR0_PG) &&
	    (is_64_bit_mode(vcpu) || kvm_is_cr4_bit_set(vcpu, X86_CR4_PCIDE)))
		return 1;

	kvm_x86_call(set_cr0)(vcpu, cr0);

	kvm_post_set_cr0(vcpu, old_cr0, cr0);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_cr0);

void kvm_lmsw(struct kvm_vcpu *vcpu, unsigned long msw)
{
	(void)kvm_set_cr0(vcpu, kvm_read_cr0_bits(vcpu, ~0x0eul) | (msw & 0x0f));
}
EXPORT_SYMBOL_GPL(kvm_lmsw);

void kvm_load_guest_xsave_state(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.guest_state_protected)
		return;

	if (kvm_is_cr4_bit_set(vcpu, X86_CR4_OSXSAVE)) {

		if (vcpu->arch.xcr0 != kvm_host.xcr0)
			xsetbv(XCR_XFEATURE_ENABLED_MASK, vcpu->arch.xcr0);

		if (guest_can_use(vcpu, X86_FEATURE_XSAVES) &&
		    vcpu->arch.ia32_xss != kvm_host.xss)
			wrmsrl(MSR_IA32_XSS, vcpu->arch.ia32_xss);
	}

	if (cpu_feature_enabled(X86_FEATURE_PKU) &&
	    vcpu->arch.pkru != vcpu->arch.host_pkru &&
	    ((vcpu->arch.xcr0 & XFEATURE_MASK_PKRU) ||
	     kvm_is_cr4_bit_set(vcpu, X86_CR4_PKE)))
		write_pkru(vcpu->arch.pkru);
}
EXPORT_SYMBOL_GPL(kvm_load_guest_xsave_state);

void kvm_load_host_xsave_state(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.guest_state_protected)
		return;

	if (cpu_feature_enabled(X86_FEATURE_PKU) &&
	    ((vcpu->arch.xcr0 & XFEATURE_MASK_PKRU) ||
	     kvm_is_cr4_bit_set(vcpu, X86_CR4_PKE))) {
		vcpu->arch.pkru = rdpkru();
		if (vcpu->arch.pkru != vcpu->arch.host_pkru)
			write_pkru(vcpu->arch.host_pkru);
	}

	if (kvm_is_cr4_bit_set(vcpu, X86_CR4_OSXSAVE)) {

		if (vcpu->arch.xcr0 != kvm_host.xcr0)
			xsetbv(XCR_XFEATURE_ENABLED_MASK, kvm_host.xcr0);

		if (guest_can_use(vcpu, X86_FEATURE_XSAVES) &&
		    vcpu->arch.ia32_xss != kvm_host.xss)
			wrmsrl(MSR_IA32_XSS, kvm_host.xss);
	}

}
EXPORT_SYMBOL_GPL(kvm_load_host_xsave_state);

#ifdef CONFIG_X86_64
static inline u64 kvm_guest_supported_xfd(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.guest_supported_xcr0 & XFEATURE_MASK_USER_DYNAMIC;
}
#endif

static int __kvm_set_xcr(struct kvm_vcpu *vcpu, u32 index, u64 xcr)
{
	u64 xcr0 = xcr;
	u64 old_xcr0 = vcpu->arch.xcr0;
	u64 valid_bits;

	/* Only support XCR_XFEATURE_ENABLED_MASK(xcr0) now  */
	if (index != XCR_XFEATURE_ENABLED_MASK)
		return 1;
	if (!(xcr0 & XFEATURE_MASK_FP))
		return 1;
	if ((xcr0 & XFEATURE_MASK_YMM) && !(xcr0 & XFEATURE_MASK_SSE))
		return 1;

	/*
	 * Do not allow the guest to set bits that we do not support
	 * saving.  However, xcr0 bit 0 is always set, even if the
	 * emulated CPU does not support XSAVE (see kvm_vcpu_reset()).
	 */
	valid_bits = vcpu->arch.guest_supported_xcr0 | XFEATURE_MASK_FP;
	if (xcr0 & ~valid_bits)
		return 1;

	if ((!(xcr0 & XFEATURE_MASK_BNDREGS)) !=
	    (!(xcr0 & XFEATURE_MASK_BNDCSR)))
		return 1;

	if (xcr0 & XFEATURE_MASK_AVX512) {
		if (!(xcr0 & XFEATURE_MASK_YMM))
			return 1;
		if ((xcr0 & XFEATURE_MASK_AVX512) != XFEATURE_MASK_AVX512)
			return 1;
	}

	if ((xcr0 & XFEATURE_MASK_XTILE) &&
	    ((xcr0 & XFEATURE_MASK_XTILE) != XFEATURE_MASK_XTILE))
		return 1;

	vcpu->arch.xcr0 = xcr0;

	if ((xcr0 ^ old_xcr0) & XFEATURE_MASK_EXTEND)
		kvm_update_cpuid_runtime(vcpu);
	return 0;
}

int kvm_emulate_xsetbv(struct kvm_vcpu *vcpu)
{
	/* Note, #UD due to CR4.OSXSAVE=0 has priority over the intercept. */
	if (kvm_x86_call(get_cpl)(vcpu) != 0 ||
	    __kvm_set_xcr(vcpu, kvm_rcx_read(vcpu), kvm_read_edx_eax(vcpu))) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	return kvm_skip_emulated_instruction(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_emulate_xsetbv);

bool __kvm_is_valid_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	if (cr4 & cr4_reserved_bits)
		return false;

	if (cr4 & vcpu->arch.cr4_guest_rsvd_bits)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(__kvm_is_valid_cr4);

static bool kvm_is_valid_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	return __kvm_is_valid_cr4(vcpu, cr4) &&
	       kvm_x86_call(is_valid_cr4)(vcpu, cr4);
}

void kvm_post_set_cr4(struct kvm_vcpu *vcpu, unsigned long old_cr4, unsigned long cr4)
{
	if ((cr4 ^ old_cr4) & KVM_MMU_CR4_ROLE_BITS)
		kvm_mmu_reset_context(vcpu);

	/*
	 * If CR4.PCIDE is changed 0 -> 1, there is no need to flush the TLB
	 * according to the SDM; however, stale prev_roots could be reused
	 * incorrectly in the future after a MOV to CR3 with NOFLUSH=1, so we
	 * free them all.  This is *not* a superset of KVM_REQ_TLB_FLUSH_GUEST
	 * or KVM_REQ_TLB_FLUSH_CURRENT, because the hardware TLB is not flushed,
	 * so fall through.
	 */
	if (!tdp_enabled &&
	    (cr4 & X86_CR4_PCIDE) && !(old_cr4 & X86_CR4_PCIDE))
		kvm_mmu_unload(vcpu);

	/*
	 * The TLB has to be flushed for all PCIDs if any of the following
	 * (architecturally required) changes happen:
	 * - CR4.PCIDE is changed from 1 to 0
	 * - CR4.PGE is toggled
	 *
	 * This is a superset of KVM_REQ_TLB_FLUSH_CURRENT.
	 */
	if (((cr4 ^ old_cr4) & X86_CR4_PGE) ||
	    (!(cr4 & X86_CR4_PCIDE) && (old_cr4 & X86_CR4_PCIDE)))
		kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);

	/*
	 * The TLB has to be flushed for the current PCID if any of the
	 * following (architecturally required) changes happen:
	 * - CR4.SMEP is changed from 0 to 1
	 * - CR4.PAE is toggled
	 */
	else if (((cr4 ^ old_cr4) & X86_CR4_PAE) ||
		 ((cr4 & X86_CR4_SMEP) && !(old_cr4 & X86_CR4_SMEP)))
		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);

}
EXPORT_SYMBOL_GPL(kvm_post_set_cr4);

int kvm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	unsigned long old_cr4 = kvm_read_cr4(vcpu);

	if (!kvm_is_valid_cr4(vcpu, cr4))
		return 1;

	if (is_long_mode(vcpu)) {
		if (!(cr4 & X86_CR4_PAE))
			return 1;
		if ((cr4 ^ old_cr4) & X86_CR4_LA57)
			return 1;
	} else if (is_paging(vcpu) && (cr4 & X86_CR4_PAE)
		   && ((cr4 ^ old_cr4) & X86_CR4_PDPTR_BITS)
		   && !load_pdptrs(vcpu, kvm_read_cr3(vcpu)))
		return 1;

	if ((cr4 & X86_CR4_PCIDE) && !(old_cr4 & X86_CR4_PCIDE)) {
		/* PCID can not be enabled when cr3[11:0]!=000H or EFER.LMA=0 */
		if ((kvm_read_cr3(vcpu) & X86_CR3_PCID_MASK) || !is_long_mode(vcpu))
			return 1;
	}

	kvm_x86_call(set_cr4)(vcpu, cr4);

	kvm_post_set_cr4(vcpu, old_cr4, cr4);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_cr4);

static void kvm_invalidate_pcid(struct kvm_vcpu *vcpu, unsigned long pcid)
{
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	unsigned long roots_to_free = 0;
	int i;

	/*
	 * MOV CR3 and INVPCID are usually not intercepted when using TDP, but
	 * this is reachable when running EPT=1 and unrestricted_guest=0,  and
	 * also via the emulator.  KVM's TDP page tables are not in the scope of
	 * the invalidation, but the guest's TLB entries need to be flushed as
	 * the CPU may have cached entries in its TLB for the target PCID.
	 */
	if (unlikely(tdp_enabled)) {
		kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);
		return;
	}

	/*
	 * If neither the current CR3 nor any of the prev_roots use the given
	 * PCID, then nothing needs to be done here because a resync will
	 * happen anyway before switching to any other CR3.
	 */
	if (kvm_get_active_pcid(vcpu) == pcid) {
		kvm_make_request(KVM_REQ_MMU_SYNC, vcpu);
		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
	}

	/*
	 * If PCID is disabled, there is no need to free prev_roots even if the
	 * PCIDs for them are also 0, because MOV to CR3 always flushes the TLB
	 * with PCIDE=0.
	 */
	if (!kvm_is_cr4_bit_set(vcpu, X86_CR4_PCIDE))
		return;

	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
		if (kvm_get_pcid(vcpu, mmu->prev_roots[i].pgd) == pcid)
			roots_to_free |= KVM_MMU_ROOT_PREVIOUS(i);

	kvm_mmu_free_roots(vcpu->kvm, mmu, roots_to_free);
}

int kvm_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	bool skip_tlb_flush = false;
	unsigned long pcid = 0;
#ifdef CONFIG_X86_64
	if (kvm_is_cr4_bit_set(vcpu, X86_CR4_PCIDE)) {
		skip_tlb_flush = cr3 & X86_CR3_PCID_NOFLUSH;
		cr3 &= ~X86_CR3_PCID_NOFLUSH;
		pcid = cr3 & X86_CR3_PCID_MASK;
	}
#endif

	/* PDPTRs are always reloaded for PAE paging. */
	if (cr3 == kvm_read_cr3(vcpu) && !is_pae_paging(vcpu))
		goto handle_tlb_flush;

	/*
	 * Do not condition the GPA check on long mode, this helper is used to
	 * stuff CR3, e.g. for RSM emulation, and there is no guarantee that
	 * the current vCPU mode is accurate.
	 */
	if (!kvm_vcpu_is_legal_cr3(vcpu, cr3))
		return 1;

	if (is_pae_paging(vcpu) && !load_pdptrs(vcpu, cr3))
		return 1;

	if (cr3 != kvm_read_cr3(vcpu))
		kvm_mmu_new_pgd(vcpu, cr3);

	vcpu->arch.cr3 = cr3;
	kvm_register_mark_dirty(vcpu, VCPU_EXREG_CR3);
	/* Do not call post_set_cr3, we do not get here for confidential guests.  */

handle_tlb_flush:
	/*
	 * A load of CR3 that flushes the TLB flushes only the current PCID,
	 * even if PCID is disabled, in which case PCID=0 is flushed.  It's a
	 * moot point in the end because _disabling_ PCID will flush all PCIDs,
	 * and it's impossible to use a non-zero PCID when PCID is disabled,
	 * i.e. only PCID=0 can be relevant.
	 */
	if (!skip_tlb_flush)
		kvm_invalidate_pcid(vcpu, pcid);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_cr3);

int kvm_set_cr8(struct kvm_vcpu *vcpu, unsigned long cr8)
{
	if (cr8 & CR8_RESERVED_BITS)
		return 1;
	if (lapic_in_kernel(vcpu))
		kvm_lapic_set_tpr(vcpu, cr8);
	else
		vcpu->arch.cr8 = cr8;
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_cr8);

unsigned long kvm_get_cr8(struct kvm_vcpu *vcpu)
{
	if (lapic_in_kernel(vcpu))
		return kvm_lapic_get_cr8(vcpu);
	else
		return vcpu->arch.cr8;
}
EXPORT_SYMBOL_GPL(kvm_get_cr8);

static void kvm_update_dr0123(struct kvm_vcpu *vcpu)
{
	int i;

	if (!(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)) {
		for (i = 0; i < KVM_NR_DB_REGS; i++)
			vcpu->arch.eff_db[i] = vcpu->arch.db[i];
	}
}

void kvm_update_dr7(struct kvm_vcpu *vcpu)
{
	unsigned long dr7;

	if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)
		dr7 = vcpu->arch.guest_debug_dr7;
	else
		dr7 = vcpu->arch.dr7;
	kvm_x86_call(set_dr7)(vcpu, dr7);
	vcpu->arch.switch_db_regs &= ~KVM_DEBUGREG_BP_ENABLED;
	if (dr7 & DR7_BP_EN_MASK)
		vcpu->arch.switch_db_regs |= KVM_DEBUGREG_BP_ENABLED;
}
EXPORT_SYMBOL_GPL(kvm_update_dr7);

static u64 kvm_dr6_fixed(struct kvm_vcpu *vcpu)
{
	u64 fixed = DR6_FIXED_1;

	if (!guest_cpuid_has(vcpu, X86_FEATURE_RTM))
		fixed |= DR6_RTM;

	if (!guest_cpuid_has(vcpu, X86_FEATURE_BUS_LOCK_DETECT))
		fixed |= DR6_BUS_LOCK;
	return fixed;
}

int kvm_set_dr(struct kvm_vcpu *vcpu, int dr, unsigned long val)
{
	size_t size = ARRAY_SIZE(vcpu->arch.db);

	switch (dr) {
	case 0 ... 3:
		vcpu->arch.db[array_index_nospec(dr, size)] = val;
		if (!(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP))
			vcpu->arch.eff_db[dr] = val;
		break;
	case 4:
	case 6:
		if (!kvm_dr6_valid(val))
			return 1; /* #GP */
		vcpu->arch.dr6 = (val & DR6_VOLATILE) | kvm_dr6_fixed(vcpu);
		break;
	case 5:
	default: /* 7 */
		if (!kvm_dr7_valid(val))
			return 1; /* #GP */
		vcpu->arch.dr7 = (val & DR7_VOLATILE) | DR7_FIXED_1;
		kvm_update_dr7(vcpu);
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_dr);

unsigned long kvm_get_dr(struct kvm_vcpu *vcpu, int dr)
{
	size_t size = ARRAY_SIZE(vcpu->arch.db);

	switch (dr) {
	case 0 ... 3:
		return vcpu->arch.db[array_index_nospec(dr, size)];
	case 4:
	case 6:
		return vcpu->arch.dr6;
	case 5:
	default: /* 7 */
		return vcpu->arch.dr7;
	}
}
EXPORT_SYMBOL_GPL(kvm_get_dr);

int kvm_emulate_rdpmc(struct kvm_vcpu *vcpu)
{
	u32 ecx = kvm_rcx_read(vcpu);
	u64 data;

	if (kvm_pmu_rdpmc(vcpu, ecx, &data)) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	kvm_rax_write(vcpu, (u32)data);
	kvm_rdx_write(vcpu, data >> 32);
	return kvm_skip_emulated_instruction(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_emulate_rdpmc);

/*
 * Some IA32_ARCH_CAPABILITIES bits have dependencies on MSRs that KVM
 * does not yet virtualize. These include:
 *   10 - MISC_PACKAGE_CTRLS
 *   11 - ENERGY_FILTERING_CTL
 *   12 - DOITM
 *   18 - FB_CLEAR_CTRL
 *   21 - XAPIC_DISABLE_STATUS
 *   23 - OVERCLOCKING_STATUS
 */

#define KVM_SUPPORTED_ARCH_CAP \
	(ARCH_CAP_RDCL_NO | ARCH_CAP_IBRS_ALL | ARCH_CAP_RSBA | \
	 ARCH_CAP_SKIP_VMENTRY_L1DFLUSH | ARCH_CAP_SSB_NO | ARCH_CAP_MDS_NO | \
	 ARCH_CAP_PSCHANGE_MC_NO | ARCH_CAP_TSX_CTRL_MSR | ARCH_CAP_TAA_NO | \
	 ARCH_CAP_SBDR_SSDP_NO | ARCH_CAP_FBSDP_NO | ARCH_CAP_PSDP_NO | \
	 ARCH_CAP_FB_CLEAR | ARCH_CAP_RRSBA | ARCH_CAP_PBRSB_NO | ARCH_CAP_GDS_NO | \
	 ARCH_CAP_RFDS_NO | ARCH_CAP_RFDS_CLEAR | ARCH_CAP_BHI_NO)

static u64 kvm_get_arch_capabilities(void)
{
	u64 data = kvm_host.arch_capabilities & KVM_SUPPORTED_ARCH_CAP;

	/*
	 * If nx_huge_pages is enabled, KVM's shadow paging will ensure that
	 * the nested hypervisor runs with NX huge pages.  If it is not,
	 * L1 is anyway vulnerable to ITLB_MULTIHIT exploits from other
	 * L1 guests, so it need not worry about its own (L2) guests.
	 */
	data |= ARCH_CAP_PSCHANGE_MC_NO;

	/*
	 * If we're doing cache flushes (either "always" or "cond")
	 * we will do one whenever the guest does a vmlaunch/vmresume.
	 * If an outer hypervisor is doing the cache flush for us
	 * (ARCH_CAP_SKIP_VMENTRY_L1DFLUSH), we can safely pass that
	 * capability to the guest too, and if EPT is disabled we're not
	 * vulnerable.  Overall, only VMENTER_L1D_FLUSH_NEVER will
	 * require a nested hypervisor to do a flush of its own.
	 */
	if (l1tf_vmx_mitigation != VMENTER_L1D_FLUSH_NEVER)
		data |= ARCH_CAP_SKIP_VMENTRY_L1DFLUSH;

	if (!boot_cpu_has_bug(X86_BUG_CPU_MELTDOWN))
		data |= ARCH_CAP_RDCL_NO;
	if (!boot_cpu_has_bug(X86_BUG_SPEC_STORE_BYPASS))
		data |= ARCH_CAP_SSB_NO;
	if (!boot_cpu_has_bug(X86_BUG_MDS))
		data |= ARCH_CAP_MDS_NO;
	if (!boot_cpu_has_bug(X86_BUG_RFDS))
		data |= ARCH_CAP_RFDS_NO;

	if (!boot_cpu_has(X86_FEATURE_RTM)) {
		/*
		 * If RTM=0 because the kernel has disabled TSX, the host might
		 * have TAA_NO or TSX_CTRL.  Clear TAA_NO (the guest sees RTM=0
		 * and therefore knows that there cannot be TAA) but keep
		 * TSX_CTRL: some buggy userspaces leave it set on tsx=on hosts,
		 * and we want to allow migrating those guests to tsx=off hosts.
		 */
		data &= ~ARCH_CAP_TAA_NO;
	} else if (!boot_cpu_has_bug(X86_BUG_TAA)) {
		data |= ARCH_CAP_TAA_NO;
	} else {
		/*
		 * Nothing to do here; we emulate TSX_CTRL if present on the
		 * host so the guest can choose between disabling TSX or
		 * using VERW to clear CPU buffers.
		 */
	}

	if (!boot_cpu_has_bug(X86_BUG_GDS) || gds_ucode_mitigated())
		data |= ARCH_CAP_GDS_NO;

	return data;
}

static int kvm_get_feature_msr(struct kvm_vcpu *vcpu, u32 index, u64 *data,
			       bool host_initiated)
{
	WARN_ON_ONCE(!host_initiated);

	switch (index) {
	case MSR_IA32_ARCH_CAPABILITIES:
		*data = kvm_get_arch_capabilities();
		break;
	case MSR_IA32_PERF_CAPABILITIES:
		*data = kvm_caps.supported_perf_cap;
		break;
	case MSR_PLATFORM_INFO:
		*data = MSR_PLATFORM_INFO_CPUID_FAULT;
		break;
	case MSR_IA32_UCODE_REV:
		rdmsrl_safe(index, data);
		break;
	default:
		return kvm_x86_call(get_feature_msr)(index, data);
	}
	return 0;
}

static int do_get_feature_msr(struct kvm_vcpu *vcpu, unsigned index, u64 *data)
{
	return kvm_do_msr_access(vcpu, index, data, true, MSR_TYPE_R,
				 kvm_get_feature_msr);
}

static bool __kvm_valid_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	if (efer & EFER_AUTOIBRS && !guest_cpuid_has(vcpu, X86_FEATURE_AUTOIBRS))
		return false;

	if (efer & EFER_FFXSR && !guest_cpuid_has(vcpu, X86_FEATURE_FXSR_OPT))
		return false;

	if (efer & EFER_SVME && !guest_cpuid_has(vcpu, X86_FEATURE_SVM))
		return false;

	if (efer & (EFER_LME | EFER_LMA) &&
	    !guest_cpuid_has(vcpu, X86_FEATURE_LM))
		return false;

	if (efer & EFER_NX && !guest_cpuid_has(vcpu, X86_FEATURE_NX))
		return false;

	return true;

}
bool kvm_valid_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	if (efer & efer_reserved_bits)
		return false;

	return __kvm_valid_efer(vcpu, efer);
}
EXPORT_SYMBOL_GPL(kvm_valid_efer);

static int set_efer(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	u64 old_efer = vcpu->arch.efer;
	u64 efer = msr_info->data;
	int r;

	if (efer & efer_reserved_bits)
		return 1;

	if (!msr_info->host_initiated) {
		if (!__kvm_valid_efer(vcpu, efer))
			return 1;

		if (is_paging(vcpu) &&
		    (vcpu->arch.efer & EFER_LME) != (efer & EFER_LME))
			return 1;
	}

	efer &= ~EFER_LMA;
	efer |= vcpu->arch.efer & EFER_LMA;

	r = kvm_x86_call(set_efer)(vcpu, efer);
	if (r) {
		WARN_ON(r > 0);
		return r;
	}

	if ((efer ^ old_efer) & KVM_MMU_EFER_ROLE_BITS)
		kvm_mmu_reset_context(vcpu);

	if (!static_cpu_has(X86_FEATURE_XSAVES) &&
	    (efer & EFER_SVME))
		kvm_hv_xsaves_xsavec_maybe_warn(vcpu);

	return 0;
}

void kvm_enable_efer_bits(u64 mask)
{
       efer_reserved_bits &= ~mask;
}
EXPORT_SYMBOL_GPL(kvm_enable_efer_bits);

bool kvm_msr_allowed(struct kvm_vcpu *vcpu, u32 index, u32 type)
{
	struct kvm_x86_msr_filter *msr_filter;
	struct msr_bitmap_range *ranges;
	struct kvm *kvm = vcpu->kvm;
	bool allowed;
	int idx;
	u32 i;

	/* x2APIC MSRs do not support filtering. */
	if (index >= 0x800 && index <= 0x8ff)
		return true;

	idx = srcu_read_lock(&kvm->srcu);

	msr_filter = srcu_dereference(kvm->arch.msr_filter, &kvm->srcu);
	if (!msr_filter) {
		allowed = true;
		goto out;
	}

	allowed = msr_filter->default_allow;
	ranges = msr_filter->ranges;

	for (i = 0; i < msr_filter->count; i++) {
		u32 start = ranges[i].base;
		u32 end = start + ranges[i].nmsrs;
		u32 flags = ranges[i].flags;
		unsigned long *bitmap = ranges[i].bitmap;

		if ((index >= start) && (index < end) && (flags & type)) {
			allowed = test_bit(index - start, bitmap);
			break;
		}
	}

out:
	srcu_read_unlock(&kvm->srcu, idx);

	return allowed;
}
EXPORT_SYMBOL_GPL(kvm_msr_allowed);

/*
 * Write @data into the MSR specified by @index.  Select MSR specific fault
 * checks are bypassed if @host_initiated is %true.
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
static int __kvm_set_msr(struct kvm_vcpu *vcpu, u32 index, u64 data,
			 bool host_initiated)
{
	struct msr_data msr;

	switch (index) {
	case MSR_FS_BASE:
	case MSR_GS_BASE:
	case MSR_KERNEL_GS_BASE:
	case MSR_CSTAR:
	case MSR_LSTAR:
		if (is_noncanonical_msr_address(data, vcpu))
			return 1;
		break;
	case MSR_IA32_SYSENTER_EIP:
	case MSR_IA32_SYSENTER_ESP:
		/*
		 * IA32_SYSENTER_ESP and IA32_SYSENTER_EIP cause #GP if
		 * non-canonical address is written on Intel but not on
		 * AMD (which ignores the top 32-bits, because it does
		 * not implement 64-bit SYSENTER).
		 *
		 * 64-bit code should hence be able to write a non-canonical
		 * value on AMD.  Making the address canonical ensures that
		 * vmentry does not fail on Intel after writing a non-canonical
		 * value, and that something deterministic happens if the guest
		 * invokes 64-bit SYSENTER.
		 */
		data = __canonical_address(data, max_host_virt_addr_bits());
		break;
	case MSR_TSC_AUX:
		if (!kvm_is_supported_user_return_msr(MSR_TSC_AUX))
			return 1;

		if (!host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_RDTSCP) &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_RDPID))
			return 1;

		/*
		 * Per Intel's SDM, bits 63:32 are reserved, but AMD's APM has
		 * incomplete and conflicting architectural behavior.  Current
		 * AMD CPUs completely ignore bits 63:32, i.e. they aren't
		 * reserved and always read as zeros.  Enforce Intel's reserved
		 * bits check if the guest CPU is Intel compatible, otherwise
		 * clear the bits.  This ensures cross-vendor migration will
		 * provide consistent behavior for the guest.
		 */
		if (guest_cpuid_is_intel_compatible(vcpu) && (data >> 32) != 0)
			return 1;

		data = (u32)data;
		break;
	}

	msr.data = data;
	msr.index = index;
	msr.host_initiated = host_initiated;

	return kvm_x86_call(set_msr)(vcpu, &msr);
}

static int _kvm_set_msr(struct kvm_vcpu *vcpu, u32 index, u64 *data,
			bool host_initiated)
{
	return __kvm_set_msr(vcpu, index, *data, host_initiated);
}

static int kvm_set_msr_ignored_check(struct kvm_vcpu *vcpu,
				     u32 index, u64 data, bool host_initiated)
{
	return kvm_do_msr_access(vcpu, index, &data, host_initiated, MSR_TYPE_W,
				 _kvm_set_msr);
}

/*
 * Read the MSR specified by @index into @data.  Select MSR specific fault
 * checks are bypassed if @host_initiated is %true.
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
int __kvm_get_msr(struct kvm_vcpu *vcpu, u32 index, u64 *data,
		  bool host_initiated)
{
	struct msr_data msr;
	int ret;

	switch (index) {
	case MSR_TSC_AUX:
		if (!kvm_is_supported_user_return_msr(MSR_TSC_AUX))
			return 1;

		if (!host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_RDTSCP) &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_RDPID))
			return 1;
		break;
	}

	msr.index = index;
	msr.host_initiated = host_initiated;

	ret = kvm_x86_call(get_msr)(vcpu, &msr);
	if (!ret)
		*data = msr.data;
	return ret;
}

static int kvm_get_msr_ignored_check(struct kvm_vcpu *vcpu,
				     u32 index, u64 *data, bool host_initiated)
{
	return kvm_do_msr_access(vcpu, index, data, host_initiated, MSR_TYPE_R,
				 __kvm_get_msr);
}

int kvm_get_msr_with_filter(struct kvm_vcpu *vcpu, u32 index, u64 *data)
{
	if (!kvm_msr_allowed(vcpu, index, KVM_MSR_FILTER_READ))
		return KVM_MSR_RET_FILTERED;
	return kvm_get_msr_ignored_check(vcpu, index, data, false);
}
EXPORT_SYMBOL_GPL(kvm_get_msr_with_filter);

int kvm_set_msr_with_filter(struct kvm_vcpu *vcpu, u32 index, u64 data)
{
	if (!kvm_msr_allowed(vcpu, index, KVM_MSR_FILTER_WRITE))
		return KVM_MSR_RET_FILTERED;
	return kvm_set_msr_ignored_check(vcpu, index, data, false);
}
EXPORT_SYMBOL_GPL(kvm_set_msr_with_filter);

int kvm_get_msr(struct kvm_vcpu *vcpu, u32 index, u64 *data)
{
	return kvm_get_msr_ignored_check(vcpu, index, data, false);
}
EXPORT_SYMBOL_GPL(kvm_get_msr);

int kvm_set_msr(struct kvm_vcpu *vcpu, u32 index, u64 data)
{
	return kvm_set_msr_ignored_check(vcpu, index, data, false);
}
EXPORT_SYMBOL_GPL(kvm_set_msr);

static void complete_userspace_rdmsr(struct kvm_vcpu *vcpu)
{
	if (!vcpu->run->msr.error) {
		kvm_rax_write(vcpu, (u32)vcpu->run->msr.data);
		kvm_rdx_write(vcpu, vcpu->run->msr.data >> 32);
	}
}

static int complete_emulated_msr_access(struct kvm_vcpu *vcpu)
{
	return complete_emulated_insn_gp(vcpu, vcpu->run->msr.error);
}

static int complete_emulated_rdmsr(struct kvm_vcpu *vcpu)
{
	complete_userspace_rdmsr(vcpu);
	return complete_emulated_msr_access(vcpu);
}

static int complete_fast_msr_access(struct kvm_vcpu *vcpu)
{
	return kvm_x86_call(complete_emulated_msr)(vcpu, vcpu->run->msr.error);
}

static int complete_fast_rdmsr(struct kvm_vcpu *vcpu)
{
	complete_userspace_rdmsr(vcpu);
	return complete_fast_msr_access(vcpu);
}

static u64 kvm_msr_reason(int r)
{
	switch (r) {
	case KVM_MSR_RET_UNSUPPORTED:
		return KVM_MSR_EXIT_REASON_UNKNOWN;
	case KVM_MSR_RET_FILTERED:
		return KVM_MSR_EXIT_REASON_FILTER;
	default:
		return KVM_MSR_EXIT_REASON_INVAL;
	}
}

static int kvm_msr_user_space(struct kvm_vcpu *vcpu, u32 index,
			      u32 exit_reason, u64 data,
			      int (*completion)(struct kvm_vcpu *vcpu),
			      int r)
{
	u64 msr_reason = kvm_msr_reason(r);

	/* Check if the user wanted to know about this MSR fault */
	if (!(vcpu->kvm->arch.user_space_msr_mask & msr_reason))
		return 0;

	vcpu->run->exit_reason = exit_reason;
	vcpu->run->msr.error = 0;
	memset(vcpu->run->msr.pad, 0, sizeof(vcpu->run->msr.pad));
	vcpu->run->msr.reason = msr_reason;
	vcpu->run->msr.index = index;
	vcpu->run->msr.data = data;
	vcpu->arch.complete_userspace_io = completion;

	return 1;
}

int kvm_emulate_rdmsr(struct kvm_vcpu *vcpu)
{
	u32 ecx = kvm_rcx_read(vcpu);
	u64 data;
	int r;

	r = kvm_get_msr_with_filter(vcpu, ecx, &data);

	if (!r) {
		trace_kvm_msr_read(ecx, data);

		kvm_rax_write(vcpu, data & -1u);
		kvm_rdx_write(vcpu, (data >> 32) & -1u);
	} else {
		/* MSR read failed? See if we should ask user space */
		if (kvm_msr_user_space(vcpu, ecx, KVM_EXIT_X86_RDMSR, 0,
				       complete_fast_rdmsr, r))
			return 0;
		trace_kvm_msr_read_ex(ecx);
	}

	return kvm_x86_call(complete_emulated_msr)(vcpu, r);
}
EXPORT_SYMBOL_GPL(kvm_emulate_rdmsr);

int kvm_emulate_wrmsr(struct kvm_vcpu *vcpu)
{
	u32 ecx = kvm_rcx_read(vcpu);
	u64 data = kvm_read_edx_eax(vcpu);
	int r;

	r = kvm_set_msr_with_filter(vcpu, ecx, data);

	if (!r) {
		trace_kvm_msr_write(ecx, data);
	} else {
		/* MSR write failed? See if we should ask user space */
		if (kvm_msr_user_space(vcpu, ecx, KVM_EXIT_X86_WRMSR, data,
				       complete_fast_msr_access, r))
			return 0;
		/* Signal all other negative errors to userspace */
		if (r < 0)
			return r;
		trace_kvm_msr_write_ex(ecx, data);
	}

	return kvm_x86_call(complete_emulated_msr)(vcpu, r);
}
EXPORT_SYMBOL_GPL(kvm_emulate_wrmsr);

int kvm_emulate_as_nop(struct kvm_vcpu *vcpu)
{
	return kvm_skip_emulated_instruction(vcpu);
}

int kvm_emulate_invd(struct kvm_vcpu *vcpu)
{
	/* Treat an INVD instruction as a NOP and just skip it. */
	return kvm_emulate_as_nop(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_emulate_invd);

int kvm_handle_invalid_op(struct kvm_vcpu *vcpu)
{
	kvm_queue_exception(vcpu, UD_VECTOR);
	return 1;
}
EXPORT_SYMBOL_GPL(kvm_handle_invalid_op);


static int kvm_emulate_monitor_mwait(struct kvm_vcpu *vcpu, const char *insn)
{
	if (!kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_MWAIT_NEVER_UD_FAULTS) &&
	    !guest_cpuid_has(vcpu, X86_FEATURE_MWAIT))
		return kvm_handle_invalid_op(vcpu);

	pr_warn_once("%s instruction emulated as NOP!\n", insn);
	return kvm_emulate_as_nop(vcpu);
}
int kvm_emulate_mwait(struct kvm_vcpu *vcpu)
{
	return kvm_emulate_monitor_mwait(vcpu, "MWAIT");
}
EXPORT_SYMBOL_GPL(kvm_emulate_mwait);

int kvm_emulate_monitor(struct kvm_vcpu *vcpu)
{
	return kvm_emulate_monitor_mwait(vcpu, "MONITOR");
}
EXPORT_SYMBOL_GPL(kvm_emulate_monitor);

static inline bool kvm_vcpu_exit_request(struct kvm_vcpu *vcpu)
{
	xfer_to_guest_mode_prepare();

	return READ_ONCE(vcpu->mode) == EXITING_GUEST_MODE ||
	       kvm_request_pending(vcpu) || xfer_to_guest_mode_work_pending();
}

/*
 * The fast path for frequent and performance sensitive wrmsr emulation,
 * i.e. the sending of IPI, sending IPI early in the VM-Exit flow reduces
 * the latency of virtual IPI by avoiding the expensive bits of transitioning
 * from guest to host, e.g. reacquiring KVM's SRCU lock. In contrast to the
 * other cases which must be called after interrupts are enabled on the host.
 */
static int handle_fastpath_set_x2apic_icr_irqoff(struct kvm_vcpu *vcpu, u64 data)
{
	if (!lapic_in_kernel(vcpu) || !apic_x2apic_mode(vcpu->arch.apic))
		return 1;

	if (((data & APIC_SHORT_MASK) == APIC_DEST_NOSHORT) &&
	    ((data & APIC_DEST_MASK) == APIC_DEST_PHYSICAL) &&
	    ((data & APIC_MODE_MASK) == APIC_DM_FIXED) &&
	    ((u32)(data >> 32) != X2APIC_BROADCAST))
		return kvm_x2apic_icr_write(vcpu->arch.apic, data);

	return 1;
}

static int handle_fastpath_set_tscdeadline(struct kvm_vcpu *vcpu, u64 data)
{
	if (!kvm_can_use_hv_timer(vcpu))
		return 1;

	kvm_set_lapic_tscdeadline_msr(vcpu, data);
	return 0;
}

fastpath_t handle_fastpath_set_msr_irqoff(struct kvm_vcpu *vcpu)
{
	u32 msr = kvm_rcx_read(vcpu);
	u64 data;
	fastpath_t ret;
	bool handled;

	kvm_vcpu_srcu_read_lock(vcpu);

	switch (msr) {
	case APIC_BASE_MSR + (APIC_ICR >> 4):
		data = kvm_read_edx_eax(vcpu);
		handled = !handle_fastpath_set_x2apic_icr_irqoff(vcpu, data);
		break;
	case MSR_IA32_TSC_DEADLINE:
		data = kvm_read_edx_eax(vcpu);
		handled = !handle_fastpath_set_tscdeadline(vcpu, data);
		break;
	default:
		handled = false;
		break;
	}

	if (handled) {
		if (!kvm_skip_emulated_instruction(vcpu))
			ret = EXIT_FASTPATH_EXIT_USERSPACE;
		else
			ret = EXIT_FASTPATH_REENTER_GUEST;
		trace_kvm_msr_write(msr, data);
	} else {
		ret = EXIT_FASTPATH_NONE;
	}

	kvm_vcpu_srcu_read_unlock(vcpu);

	return ret;
}
EXPORT_SYMBOL_GPL(handle_fastpath_set_msr_irqoff);

/*
 * Adapt set_msr() to msr_io()'s calling convention
 */
static int do_get_msr(struct kvm_vcpu *vcpu, unsigned index, u64 *data)
{
	return kvm_get_msr_ignored_check(vcpu, index, data, true);
}

static int do_set_msr(struct kvm_vcpu *vcpu, unsigned index, u64 *data)
{
	u64 val;

	/*
	 * Disallow writes to immutable feature MSRs after KVM_RUN.  KVM does
	 * not support modifying the guest vCPU model on the fly, e.g. changing
	 * the nVMX capabilities while L2 is running is nonsensical.  Allow
	 * writes of the same value, e.g. to allow userspace to blindly stuff
	 * all MSRs when emulating RESET.
	 */
	if (kvm_vcpu_has_run(vcpu) && kvm_is_immutable_feature_msr(index) &&
	    (do_get_msr(vcpu, index, &val) || *data != val))
		return -EINVAL;

	return kvm_set_msr_ignored_check(vcpu, index, *data, true);
}

#ifdef CONFIG_X86_64
struct pvclock_clock {
	int vclock_mode;
	u64 cycle_last;
	u64 mask;
	u32 mult;
	u32 shift;
	u64 base_cycles;
	u64 offset;
};

struct pvclock_gtod_data {
	seqcount_t	seq;

	struct pvclock_clock clock; /* extract of a clocksource struct */
	struct pvclock_clock raw_clock; /* extract of a clocksource struct */

	ktime_t		offs_boot;
	u64		wall_time_sec;
};

static struct pvclock_gtod_data pvclock_gtod_data;

static void update_pvclock_gtod(struct timekeeper *tk)
{
	struct pvclock_gtod_data *vdata = &pvclock_gtod_data;

	write_seqcount_begin(&vdata->seq);

	/* copy pvclock gtod data */
	vdata->clock.vclock_mode	= tk->tkr_mono.clock->vdso_clock_mode;
	vdata->clock.cycle_last		= tk->tkr_mono.cycle_last;
	vdata->clock.mask		= tk->tkr_mono.mask;
	vdata->clock.mult		= tk->tkr_mono.mult;
	vdata->clock.shift		= tk->tkr_mono.shift;
	vdata->clock.base_cycles	= tk->tkr_mono.xtime_nsec;
	vdata->clock.offset		= tk->tkr_mono.base;

	vdata->raw_clock.vclock_mode	= tk->tkr_raw.clock->vdso_clock_mode;
	vdata->raw_clock.cycle_last	= tk->tkr_raw.cycle_last;
	vdata->raw_clock.mask		= tk->tkr_raw.mask;
	vdata->raw_clock.mult		= tk->tkr_raw.mult;
	vdata->raw_clock.shift		= tk->tkr_raw.shift;
	vdata->raw_clock.base_cycles	= tk->tkr_raw.xtime_nsec;
	vdata->raw_clock.offset		= tk->tkr_raw.base;

	vdata->wall_time_sec            = tk->xtime_sec;

	vdata->offs_boot		= tk->offs_boot;

	write_seqcount_end(&vdata->seq);
}

static s64 get_kvmclock_base_ns(void)
{
	/* Count up from boot time, but with the frequency of the raw clock.  */
	return ktime_to_ns(ktime_add(ktime_get_raw(), pvclock_gtod_data.offs_boot));
}
#else
static s64 get_kvmclock_base_ns(void)
{
	/* Master clock not used, so we can just use CLOCK_BOOTTIME.  */
	return ktime_get_boottime_ns();
}
#endif

static void kvm_write_wall_clock(struct kvm *kvm, gpa_t wall_clock, int sec_hi_ofs)
{
	int version;
	int r;
	struct pvclock_wall_clock wc;
	u32 wc_sec_hi;
	u64 wall_nsec;

	if (!wall_clock)
		return;

	r = kvm_read_guest(kvm, wall_clock, &version, sizeof(version));
	if (r)
		return;

	if (version & 1)
		++version;  /* first time write, random junk */

	++version;

	if (kvm_write_guest(kvm, wall_clock, &version, sizeof(version)))
		return;

	wall_nsec = kvm_get_wall_clock_epoch(kvm);

	wc.nsec = do_div(wall_nsec, NSEC_PER_SEC);
	wc.sec = (u32)wall_nsec; /* overflow in 2106 guest time */
	wc.version = version;

	kvm_write_guest(kvm, wall_clock, &wc, sizeof(wc));

	if (sec_hi_ofs) {
		wc_sec_hi = wall_nsec >> 32;
		kvm_write_guest(kvm, wall_clock + sec_hi_ofs,
				&wc_sec_hi, sizeof(wc_sec_hi));
	}

	version++;
	kvm_write_guest(kvm, wall_clock, &version, sizeof(version));
}

static void kvm_write_system_time(struct kvm_vcpu *vcpu, gpa_t system_time,
				  bool old_msr, bool host_initiated)
{
	struct kvm_arch *ka = &vcpu->kvm->arch;

	if (vcpu->vcpu_id == 0 && !host_initiated) {
		if (ka->boot_vcpu_runs_old_kvmclock != old_msr)
			kvm_make_request(KVM_REQ_MASTERCLOCK_UPDATE, vcpu);

		ka->boot_vcpu_runs_old_kvmclock = old_msr;
	}

	vcpu->arch.time = system_time;
	kvm_make_request(KVM_REQ_GLOBAL_CLOCK_UPDATE, vcpu);

	/* we verify if the enable bit is set... */
	if (system_time & 1)
		kvm_gpc_activate(&vcpu->arch.pv_time, system_time & ~1ULL,
				 sizeof(struct pvclock_vcpu_time_info));
	else
		kvm_gpc_deactivate(&vcpu->arch.pv_time);

	return;
}

static uint32_t div_frac(uint32_t dividend, uint32_t divisor)
{
	do_shl32_div32(dividend, divisor);
	return dividend;
}

static void kvm_get_time_scale(uint64_t scaled_hz, uint64_t base_hz,
			       s8 *pshift, u32 *pmultiplier)
{
	uint64_t scaled64;
	int32_t  shift = 0;
	uint64_t tps64;
	uint32_t tps32;

	tps64 = base_hz;
	scaled64 = scaled_hz;
	while (tps64 > scaled64*2 || tps64 & 0xffffffff00000000ULL) {
		tps64 >>= 1;
		shift--;
	}

	tps32 = (uint32_t)tps64;
	while (tps32 <= scaled64 || scaled64 & 0xffffffff00000000ULL) {
		if (scaled64 & 0xffffffff00000000ULL || tps32 & 0x80000000)
			scaled64 >>= 1;
		else
			tps32 <<= 1;
		shift++;
	}

	*pshift = shift;
	*pmultiplier = div_frac(scaled64, tps32);
}

#ifdef CONFIG_X86_64
static atomic_t kvm_guest_has_master_clock = ATOMIC_INIT(0);
#endif

static DEFINE_PER_CPU(unsigned long, cpu_tsc_khz);
static unsigned long max_tsc_khz;

static u32 adjust_tsc_khz(u32 khz, s32 ppm)
{
	u64 v = (u64)khz * (1000000 + ppm);
	do_div(v, 1000000);
	return v;
}

static void kvm_vcpu_write_tsc_multiplier(struct kvm_vcpu *vcpu, u64 l1_multiplier);

static int set_tsc_khz(struct kvm_vcpu *vcpu, u32 user_tsc_khz, bool scale)
{
	u64 ratio;

	/* Guest TSC same frequency as host TSC? */
	if (!scale) {
		kvm_vcpu_write_tsc_multiplier(vcpu, kvm_caps.default_tsc_scaling_ratio);
		return 0;
	}

	/* TSC scaling supported? */
	if (!kvm_caps.has_tsc_control) {
		if (user_tsc_khz > tsc_khz) {
			vcpu->arch.tsc_catchup = 1;
			vcpu->arch.tsc_always_catchup = 1;
			return 0;
		} else {
			pr_warn_ratelimited("user requested TSC rate below hardware speed\n");
			return -1;
		}
	}

	/* TSC scaling required  - calculate ratio */
	ratio = mul_u64_u32_div(1ULL << kvm_caps.tsc_scaling_ratio_frac_bits,
				user_tsc_khz, tsc_khz);

	if (ratio == 0 || ratio >= kvm_caps.max_tsc_scaling_ratio) {
		pr_warn_ratelimited("Invalid TSC scaling ratio - virtual-tsc-khz=%u\n",
			            user_tsc_khz);
		return -1;
	}

	kvm_vcpu_write_tsc_multiplier(vcpu, ratio);
	return 0;
}

static int kvm_set_tsc_khz(struct kvm_vcpu *vcpu, u32 user_tsc_khz)
{
	u32 thresh_lo, thresh_hi;
	int use_scaling = 0;

	/* tsc_khz can be zero if TSC calibration fails */
	if (user_tsc_khz == 0) {
		/* set tsc_scaling_ratio to a safe value */
		kvm_vcpu_write_tsc_multiplier(vcpu, kvm_caps.default_tsc_scaling_ratio);
		return -1;
	}

	/* Compute a scale to convert nanoseconds in TSC cycles */
	kvm_get_time_scale(user_tsc_khz * 1000LL, NSEC_PER_SEC,
			   &vcpu->arch.virtual_tsc_shift,
			   &vcpu->arch.virtual_tsc_mult);
	vcpu->arch.virtual_tsc_khz = user_tsc_khz;

	/*
	 * Compute the variation in TSC rate which is acceptable
	 * within the range of tolerance and decide if the
	 * rate being applied is within that bounds of the hardware
	 * rate.  If so, no scaling or compensation need be done.
	 */
	thresh_lo = adjust_tsc_khz(tsc_khz, -tsc_tolerance_ppm);
	thresh_hi = adjust_tsc_khz(tsc_khz, tsc_tolerance_ppm);
	if (user_tsc_khz < thresh_lo || user_tsc_khz > thresh_hi) {
		pr_debug("requested TSC rate %u falls outside tolerance [%u,%u]\n",
			 user_tsc_khz, thresh_lo, thresh_hi);
		use_scaling = 1;
	}
	return set_tsc_khz(vcpu, user_tsc_khz, use_scaling);
}

static u64 compute_guest_tsc(struct kvm_vcpu *vcpu, s64 kernel_ns)
{
	u64 tsc = pvclock_scale_delta(kernel_ns-vcpu->arch.this_tsc_nsec,
				      vcpu->arch.virtual_tsc_mult,
				      vcpu->arch.virtual_tsc_shift);
	tsc += vcpu->arch.this_tsc_write;
	return tsc;
}

#ifdef CONFIG_X86_64
static inline bool gtod_is_based_on_tsc(int mode)
{
	return mode == VDSO_CLOCKMODE_TSC || mode == VDSO_CLOCKMODE_HVCLOCK;
}
#endif

static void kvm_track_tsc_matching(struct kvm_vcpu *vcpu, bool new_generation)
{
#ifdef CONFIG_X86_64
	struct kvm_arch *ka = &vcpu->kvm->arch;
	struct pvclock_gtod_data *gtod = &pvclock_gtod_data;

	/*
	 * To use the masterclock, the host clocksource must be based on TSC
	 * and all vCPUs must have matching TSCs.  Note, the count for matching
	 * vCPUs doesn't include the reference vCPU, hence "+1".
	 */
	bool use_master_clock = (ka->nr_vcpus_matched_tsc + 1 ==
				 atomic_read(&vcpu->kvm->online_vcpus)) &&
				gtod_is_based_on_tsc(gtod->clock.vclock_mode);

	/*
	 * Request a masterclock update if the masterclock needs to be toggled
	 * on/off, or when starting a new generation and the masterclock is
	 * enabled (compute_guest_tsc() requires the masterclock snapshot to be
	 * taken _after_ the new generation is created).
	 */
	if ((ka->use_master_clock && new_generation) ||
	    (ka->use_master_clock != use_master_clock))
		kvm_make_request(KVM_REQ_MASTERCLOCK_UPDATE, vcpu);

	trace_kvm_track_tsc(vcpu->vcpu_id, ka->nr_vcpus_matched_tsc,
			    atomic_read(&vcpu->kvm->online_vcpus),
		            ka->use_master_clock, gtod->clock.vclock_mode);
#endif
}

/*
 * Multiply tsc by a fixed point number represented by ratio.
 *
 * The most significant 64-N bits (mult) of ratio represent the
 * integral part of the fixed point number; the remaining N bits
 * (frac) represent the fractional part, ie. ratio represents a fixed
 * point number (mult + frac * 2^(-N)).
 *
 * N equals to kvm_caps.tsc_scaling_ratio_frac_bits.
 */
static inline u64 __scale_tsc(u64 ratio, u64 tsc)
{
	return mul_u64_u64_shr(tsc, ratio, kvm_caps.tsc_scaling_ratio_frac_bits);
}

u64 kvm_scale_tsc(u64 tsc, u64 ratio)
{
	u64 _tsc = tsc;

	if (ratio != kvm_caps.default_tsc_scaling_ratio)
		_tsc = __scale_tsc(ratio, tsc);

	return _tsc;
}

static u64 kvm_compute_l1_tsc_offset(struct kvm_vcpu *vcpu, u64 target_tsc)
{
	u64 tsc;

	tsc = kvm_scale_tsc(rdtsc(), vcpu->arch.l1_tsc_scaling_ratio);

	return target_tsc - tsc;
}

u64 kvm_read_l1_tsc(struct kvm_vcpu *vcpu, u64 host_tsc)
{
	return vcpu->arch.l1_tsc_offset +
		kvm_scale_tsc(host_tsc, vcpu->arch.l1_tsc_scaling_ratio);
}
EXPORT_SYMBOL_GPL(kvm_read_l1_tsc);

u64 kvm_calc_nested_tsc_offset(u64 l1_offset, u64 l2_offset, u64 l2_multiplier)
{
	u64 nested_offset;

	if (l2_multiplier == kvm_caps.default_tsc_scaling_ratio)
		nested_offset = l1_offset;
	else
		nested_offset = mul_s64_u64_shr((s64) l1_offset, l2_multiplier,
						kvm_caps.tsc_scaling_ratio_frac_bits);

	nested_offset += l2_offset;
	return nested_offset;
}
EXPORT_SYMBOL_GPL(kvm_calc_nested_tsc_offset);

u64 kvm_calc_nested_tsc_multiplier(u64 l1_multiplier, u64 l2_multiplier)
{
	if (l2_multiplier != kvm_caps.default_tsc_scaling_ratio)
		return mul_u64_u64_shr(l1_multiplier, l2_multiplier,
				       kvm_caps.tsc_scaling_ratio_frac_bits);

	return l1_multiplier;
}
EXPORT_SYMBOL_GPL(kvm_calc_nested_tsc_multiplier);

static void kvm_vcpu_write_tsc_offset(struct kvm_vcpu *vcpu, u64 l1_offset)
{
	trace_kvm_write_tsc_offset(vcpu->vcpu_id,
				   vcpu->arch.l1_tsc_offset,
				   l1_offset);

	vcpu->arch.l1_tsc_offset = l1_offset;

	/*
	 * If we are here because L1 chose not to trap WRMSR to TSC then
	 * according to the spec this should set L1's TSC (as opposed to
	 * setting L1's offset for L2).
	 */
	if (is_guest_mode(vcpu))
		vcpu->arch.tsc_offset = kvm_calc_nested_tsc_offset(
			l1_offset,
			kvm_x86_call(get_l2_tsc_offset)(vcpu),
			kvm_x86_call(get_l2_tsc_multiplier)(vcpu));
	else
		vcpu->arch.tsc_offset = l1_offset;

	kvm_x86_call(write_tsc_offset)(vcpu);
}

static void kvm_vcpu_write_tsc_multiplier(struct kvm_vcpu *vcpu, u64 l1_multiplier)
{
	vcpu->arch.l1_tsc_scaling_ratio = l1_multiplier;

	/* Userspace is changing the multiplier while L2 is active */
	if (is_guest_mode(vcpu))
		vcpu->arch.tsc_scaling_ratio = kvm_calc_nested_tsc_multiplier(
			l1_multiplier,
			kvm_x86_call(get_l2_tsc_multiplier)(vcpu));
	else
		vcpu->arch.tsc_scaling_ratio = l1_multiplier;

	if (kvm_caps.has_tsc_control)
		kvm_x86_call(write_tsc_multiplier)(vcpu);
}

static inline bool kvm_check_tsc_unstable(void)
{
#ifdef CONFIG_X86_64
	/*
	 * TSC is marked unstable when we're running on Hyper-V,
	 * 'TSC page' clocksource is good.
	 */
	if (pvclock_gtod_data.clock.vclock_mode == VDSO_CLOCKMODE_HVCLOCK)
		return false;
#endif
	return check_tsc_unstable();
}

/*
 * Infers attempts to synchronize the guest's tsc from host writes. Sets the
 * offset for the vcpu and tracks the TSC matching generation that the vcpu
 * participates in.
 */
static void __kvm_synchronize_tsc(struct kvm_vcpu *vcpu, u64 offset, u64 tsc,
				  u64 ns, bool matched)
{
	struct kvm *kvm = vcpu->kvm;

	lockdep_assert_held(&kvm->arch.tsc_write_lock);

	/*
	 * We also track th most recent recorded KHZ, write and time to
	 * allow the matching interval to be extended at each write.
	 */
	kvm->arch.last_tsc_nsec = ns;
	kvm->arch.last_tsc_write = tsc;
	kvm->arch.last_tsc_khz = vcpu->arch.virtual_tsc_khz;
	kvm->arch.last_tsc_offset = offset;

	vcpu->arch.last_guest_tsc = tsc;

	kvm_vcpu_write_tsc_offset(vcpu, offset);

	if (!matched) {
		/*
		 * We split periods of matched TSC writes into generations.
		 * For each generation, we track the original measured
		 * nanosecond time, offset, and write, so if TSCs are in
		 * sync, we can match exact offset, and if not, we can match
		 * exact software computation in compute_guest_tsc()
		 *
		 * These values are tracked in kvm->arch.cur_xxx variables.
		 */
		kvm->arch.cur_tsc_generation++;
		kvm->arch.cur_tsc_nsec = ns;
		kvm->arch.cur_tsc_write = tsc;
		kvm->arch.cur_tsc_offset = offset;
		kvm->arch.nr_vcpus_matched_tsc = 0;
	} else if (vcpu->arch.this_tsc_generation != kvm->arch.cur_tsc_generation) {
		kvm->arch.nr_vcpus_matched_tsc++;
	}

	/* Keep track of which generation this VCPU has synchronized to */
	vcpu->arch.this_tsc_generation = kvm->arch.cur_tsc_generation;
	vcpu->arch.this_tsc_nsec = kvm->arch.cur_tsc_nsec;
	vcpu->arch.this_tsc_write = kvm->arch.cur_tsc_write;

	kvm_track_tsc_matching(vcpu, !matched);
}

static void kvm_synchronize_tsc(struct kvm_vcpu *vcpu, u64 *user_value)
{
	u64 data = user_value ? *user_value : 0;
	struct kvm *kvm = vcpu->kvm;
	u64 offset, ns, elapsed;
	unsigned long flags;
	bool matched = false;
	bool synchronizing = false;

	raw_spin_lock_irqsave(&kvm->arch.tsc_write_lock, flags);
	offset = kvm_compute_l1_tsc_offset(vcpu, data);
	ns = get_kvmclock_base_ns();
	elapsed = ns - kvm->arch.last_tsc_nsec;

	if (vcpu->arch.virtual_tsc_khz) {
		if (data == 0) {
			/*
			 * Force synchronization when creating a vCPU, or when
			 * userspace explicitly writes a zero value.
			 */
			synchronizing = true;
		} else if (kvm->arch.user_set_tsc) {
			u64 tsc_exp = kvm->arch.last_tsc_write +
						nsec_to_cycles(vcpu, elapsed);
			u64 tsc_hz = vcpu->arch.virtual_tsc_khz * 1000LL;
			/*
			 * Here lies UAPI baggage: when a user-initiated TSC write has
			 * a small delta (1 second) of virtual cycle time against the
			 * previously set vCPU, we assume that they were intended to be
			 * in sync and the delta was only due to the racy nature of the
			 * legacy API.
			 *
			 * This trick falls down when restoring a guest which genuinely
			 * has been running for less time than the 1 second of imprecision
			 * which we allow for in the legacy API. In this case, the first
			 * value written by userspace (on any vCPU) should not be subject
			 * to this 'correction' to make it sync up with values that only
			 * come from the kernel's default vCPU creation. Make the 1-second
			 * slop hack only trigger if the user_set_tsc flag is already set.
			 */
			synchronizing = data < tsc_exp + tsc_hz &&
					data + tsc_hz > tsc_exp;
		}
	}

	if (user_value)
		kvm->arch.user_set_tsc = true;

	/*
	 * For a reliable TSC, we can match TSC offsets, and for an unstable
	 * TSC, we add elapsed time in this computation.  We could let the
	 * compensation code attempt to catch up if we fall behind, but
	 * it's better to try to match offsets from the beginning.
         */
	if (synchronizing &&
	    vcpu->arch.virtual_tsc_khz == kvm->arch.last_tsc_khz) {
		if (!kvm_check_tsc_unstable()) {
			offset = kvm->arch.cur_tsc_offset;
		} else {
			u64 delta = nsec_to_cycles(vcpu, elapsed);
			data += delta;
			offset = kvm_compute_l1_tsc_offset(vcpu, data);
		}
		matched = true;
	}

	__kvm_synchronize_tsc(vcpu, offset, data, ns, matched);
	raw_spin_unlock_irqrestore(&kvm->arch.tsc_write_lock, flags);
}

static inline void adjust_tsc_offset_guest(struct kvm_vcpu *vcpu,
					   s64 adjustment)
{
	u64 tsc_offset = vcpu->arch.l1_tsc_offset;
	kvm_vcpu_write_tsc_offset(vcpu, tsc_offset + adjustment);
}

static inline void adjust_tsc_offset_host(struct kvm_vcpu *vcpu, s64 adjustment)
{
	if (vcpu->arch.l1_tsc_scaling_ratio != kvm_caps.default_tsc_scaling_ratio)
		WARN_ON(adjustment < 0);
	adjustment = kvm_scale_tsc((u64) adjustment,
				   vcpu->arch.l1_tsc_scaling_ratio);
	adjust_tsc_offset_guest(vcpu, adjustment);
}

#ifdef CONFIG_X86_64

static u64 read_tsc(void)
{
	u64 ret = (u64)rdtsc_ordered();
	u64 last = pvclock_gtod_data.clock.cycle_last;

	if (likely(ret >= last))
		return ret;

	/*
	 * GCC likes to generate cmov here, but this branch is extremely
	 * predictable (it's just a function of time and the likely is
	 * very likely) and there's a data dependence, so force GCC
	 * to generate a branch instead.  I don't barrier() because
	 * we don't actually need a barrier, and if this function
	 * ever gets inlined it will generate worse code.
	 */
	asm volatile ("");
	return last;
}

static inline u64 vgettsc(struct pvclock_clock *clock, u64 *tsc_timestamp,
			  int *mode)
{
	u64 tsc_pg_val;
	long v;

	switch (clock->vclock_mode) {
	case VDSO_CLOCKMODE_HVCLOCK:
		if (hv_read_tsc_page_tsc(hv_get_tsc_page(),
					 tsc_timestamp, &tsc_pg_val)) {
			/* TSC page valid */
			*mode = VDSO_CLOCKMODE_HVCLOCK;
			v = (tsc_pg_val - clock->cycle_last) &
				clock->mask;
		} else {
			/* TSC page invalid */
			*mode = VDSO_CLOCKMODE_NONE;
		}
		break;
	case VDSO_CLOCKMODE_TSC:
		*mode = VDSO_CLOCKMODE_TSC;
		*tsc_timestamp = read_tsc();
		v = (*tsc_timestamp - clock->cycle_last) &
			clock->mask;
		break;
	default:
		*mode = VDSO_CLOCKMODE_NONE;
	}

	if (*mode == VDSO_CLOCKMODE_NONE)
		*tsc_timestamp = v = 0;

	return v * clock->mult;
}

/*
 * As with get_kvmclock_base_ns(), this counts from boot time, at the
 * frequency of CLOCK_MONOTONIC_RAW (hence adding gtos->offs_boot).
 */
static int do_kvmclock_base(s64 *t, u64 *tsc_timestamp)
{
	struct pvclock_gtod_data *gtod = &pvclock_gtod_data;
	unsigned long seq;
	int mode;
	u64 ns;

	do {
		seq = read_seqcount_begin(&gtod->seq);
		ns = gtod->raw_clock.base_cycles;
		ns += vgettsc(&gtod->raw_clock, tsc_timestamp, &mode);
		ns >>= gtod->raw_clock.shift;
		ns += ktime_to_ns(ktime_add(gtod->raw_clock.offset, gtod->offs_boot));
	} while (unlikely(read_seqcount_retry(&gtod->seq, seq)));
	*t = ns;

	return mode;
}

/*
 * This calculates CLOCK_MONOTONIC at the time of the TSC snapshot, with
 * no boot time offset.
 */
static int do_monotonic(s64 *t, u64 *tsc_timestamp)
{
	struct pvclock_gtod_data *gtod = &pvclock_gtod_data;
	unsigned long seq;
	int mode;
	u64 ns;

	do {
		seq = read_seqcount_begin(&gtod->seq);
		ns = gtod->clock.base_cycles;
		ns += vgettsc(&gtod->clock, tsc_timestamp, &mode);
		ns >>= gtod->clock.shift;
		ns += ktime_to_ns(gtod->clock.offset);
	} while (unlikely(read_seqcount_retry(&gtod->seq, seq)));
	*t = ns;

	return mode;
}

static int do_realtime(struct timespec64 *ts, u64 *tsc_timestamp)
{
	struct pvclock_gtod_data *gtod = &pvclock_gtod_data;
	unsigned long seq;
	int mode;
	u64 ns;

	do {
		seq = read_seqcount_begin(&gtod->seq);
		ts->tv_sec = gtod->wall_time_sec;
		ns = gtod->clock.base_cycles;
		ns += vgettsc(&gtod->clock, tsc_timestamp, &mode);
		ns >>= gtod->clock.shift;
	} while (unlikely(read_seqcount_retry(&gtod->seq, seq)));

	ts->tv_sec += __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return mode;
}

/*
 * Calculates the kvmclock_base_ns (CLOCK_MONOTONIC_RAW + boot time) and
 * reports the TSC value from which it do so. Returns true if host is
 * using TSC based clocksource.
 */
static bool kvm_get_time_and_clockread(s64 *kernel_ns, u64 *tsc_timestamp)
{
	/* checked again under seqlock below */
	if (!gtod_is_based_on_tsc(pvclock_gtod_data.clock.vclock_mode))
		return false;

	return gtod_is_based_on_tsc(do_kvmclock_base(kernel_ns,
						     tsc_timestamp));
}

/*
 * Calculates CLOCK_MONOTONIC and reports the TSC value from which it did
 * so. Returns true if host is using TSC based clocksource.
 */
bool kvm_get_monotonic_and_clockread(s64 *kernel_ns, u64 *tsc_timestamp)
{
	/* checked again under seqlock below */
	if (!gtod_is_based_on_tsc(pvclock_gtod_data.clock.vclock_mode))
		return false;

	return gtod_is_based_on_tsc(do_monotonic(kernel_ns,
						 tsc_timestamp));
}

/*
 * Calculates CLOCK_REALTIME and reports the TSC value from which it did
 * so. Returns true if host is using TSC based clocksource.
 *
 * DO NOT USE this for anything related to migration. You want CLOCK_TAI
 * for that.
 */
static bool kvm_get_walltime_and_clockread(struct timespec64 *ts,
					   u64 *tsc_timestamp)
{
	/* checked again under seqlock below */
	if (!gtod_is_based_on_tsc(pvclock_gtod_data.clock.vclock_mode))
		return false;

	return gtod_is_based_on_tsc(do_realtime(ts, tsc_timestamp));
}
#endif

/*
 *
 * Assuming a stable TSC across physical CPUS, and a stable TSC
 * across virtual CPUs, the following condition is possible.
 * Each numbered line represents an event visible to both
 * CPUs at the next numbered event.
 *
 * "timespecX" represents host monotonic time. "tscX" represents
 * RDTSC value.
 *
 * 		VCPU0 on CPU0		|	VCPU1 on CPU1
 *
 * 1.  read timespec0,tsc0
 * 2.					| timespec1 = timespec0 + N
 * 					| tsc1 = tsc0 + M
 * 3. transition to guest		| transition to guest
 * 4. ret0 = timespec0 + (rdtsc - tsc0) |
 * 5.				        | ret1 = timespec1 + (rdtsc - tsc1)
 * 				        | ret1 = timespec0 + N + (rdtsc - (tsc0 + M))
 *
 * Since ret0 update is visible to VCPU1 at time 5, to obey monotonicity:
 *
 * 	- ret0 < ret1
 *	- timespec0 + (rdtsc - tsc0) < timespec0 + N + (rdtsc - (tsc0 + M))
 *		...
 *	- 0 < N - M => M < N
 *
 * That is, when timespec0 != timespec1, M < N. Unfortunately that is not
 * always the case (the difference between two distinct xtime instances
 * might be smaller then the difference between corresponding TSC reads,
 * when updating guest vcpus pvclock areas).
 *
 * To avoid that problem, do not allow visibility of distinct
 * system_timestamp/tsc_timestamp values simultaneously: use a master
 * copy of host monotonic time values. Update that master copy
 * in lockstep.
 *
 * Rely on synchronization of host TSCs and guest TSCs for monotonicity.
 *
 */

static void pvclock_update_vm_gtod_copy(struct kvm *kvm)
{
#ifdef CONFIG_X86_64
	struct kvm_arch *ka = &kvm->arch;
	int vclock_mode;
	bool host_tsc_clocksource, vcpus_matched;

	lockdep_assert_held(&kvm->arch.tsc_write_lock);
	vcpus_matched = (ka->nr_vcpus_matched_tsc + 1 ==
			atomic_read(&kvm->online_vcpus));

	/*
	 * If the host uses TSC clock, then passthrough TSC as stable
	 * to the guest.
	 */
	host_tsc_clocksource = kvm_get_time_and_clockread(
					&ka->master_kernel_ns,
					&ka->master_cycle_now);

	ka->use_master_clock = host_tsc_clocksource && vcpus_matched
				&& !ka->backwards_tsc_observed
				&& !ka->boot_vcpu_runs_old_kvmclock;

	if (ka->use_master_clock)
		atomic_set(&kvm_guest_has_master_clock, 1);

	vclock_mode = pvclock_gtod_data.clock.vclock_mode;
	trace_kvm_update_master_clock(ka->use_master_clock, vclock_mode,
					vcpus_matched);
#endif
}

static void kvm_make_mclock_inprogress_request(struct kvm *kvm)
{
	kvm_make_all_cpus_request(kvm, KVM_REQ_MCLOCK_INPROGRESS);
}

static void __kvm_start_pvclock_update(struct kvm *kvm)
{
	raw_spin_lock_irq(&kvm->arch.tsc_write_lock);
	write_seqcount_begin(&kvm->arch.pvclock_sc);
}

static void kvm_start_pvclock_update(struct kvm *kvm)
{
	kvm_make_mclock_inprogress_request(kvm);

	/* no guest entries from this point */
	__kvm_start_pvclock_update(kvm);
}

static void kvm_end_pvclock_update(struct kvm *kvm)
{
	struct kvm_arch *ka = &kvm->arch;
	struct kvm_vcpu *vcpu;
	unsigned long i;

	write_seqcount_end(&ka->pvclock_sc);
	raw_spin_unlock_irq(&ka->tsc_write_lock);
	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);

	/* guest entries allowed */
	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_clear_request(KVM_REQ_MCLOCK_INPROGRESS, vcpu);
}

static void kvm_update_masterclock(struct kvm *kvm)
{
	kvm_hv_request_tsc_page_update(kvm);
	kvm_start_pvclock_update(kvm);
	pvclock_update_vm_gtod_copy(kvm);
	kvm_end_pvclock_update(kvm);
}

/*
 * Use the kernel's tsc_khz directly if the TSC is constant, otherwise use KVM's
 * per-CPU value (which may be zero if a CPU is going offline).  Note, tsc_khz
 * can change during boot even if the TSC is constant, as it's possible for KVM
 * to be loaded before TSC calibration completes.  Ideally, KVM would get a
 * notification when calibration completes, but practically speaking calibration
 * will complete before userspace is alive enough to create VMs.
 */
static unsigned long get_cpu_tsc_khz(void)
{
	if (static_cpu_has(X86_FEATURE_CONSTANT_TSC))
		return tsc_khz;
	else
		return __this_cpu_read(cpu_tsc_khz);
}

/* Called within read_seqcount_begin/retry for kvm->pvclock_sc.  */
static void __get_kvmclock(struct kvm *kvm, struct kvm_clock_data *data)
{
	struct kvm_arch *ka = &kvm->arch;
	struct pvclock_vcpu_time_info hv_clock;

	/* both __this_cpu_read() and rdtsc() should be on the same cpu */
	get_cpu();

	data->flags = 0;
	if (ka->use_master_clock &&
	    (static_cpu_has(X86_FEATURE_CONSTANT_TSC) || __this_cpu_read(cpu_tsc_khz))) {
#ifdef CONFIG_X86_64
		struct timespec64 ts;

		if (kvm_get_walltime_and_clockread(&ts, &data->host_tsc)) {
			data->realtime = ts.tv_nsec + NSEC_PER_SEC * ts.tv_sec;
			data->flags |= KVM_CLOCK_REALTIME | KVM_CLOCK_HOST_TSC;
		} else
#endif
		data->host_tsc = rdtsc();

		data->flags |= KVM_CLOCK_TSC_STABLE;
		hv_clock.tsc_timestamp = ka->master_cycle_now;
		hv_clock.system_time = ka->master_kernel_ns + ka->kvmclock_offset;
		kvm_get_time_scale(NSEC_PER_SEC, get_cpu_tsc_khz() * 1000LL,
				   &hv_clock.tsc_shift,
				   &hv_clock.tsc_to_system_mul);
		data->clock = __pvclock_read_cycles(&hv_clock, data->host_tsc);
	} else {
		data->clock = get_kvmclock_base_ns() + ka->kvmclock_offset;
	}

	put_cpu();
}

static void get_kvmclock(struct kvm *kvm, struct kvm_clock_data *data)
{
	struct kvm_arch *ka = &kvm->arch;
	unsigned seq;

	do {
		seq = read_seqcount_begin(&ka->pvclock_sc);
		__get_kvmclock(kvm, data);
	} while (read_seqcount_retry(&ka->pvclock_sc, seq));
}

u64 get_kvmclock_ns(struct kvm *kvm)
{
	struct kvm_clock_data data;

	get_kvmclock(kvm, &data);
	return data.clock;
}

static void kvm_setup_guest_pvclock(struct kvm_vcpu *v,
				    struct gfn_to_pfn_cache *gpc,
				    unsigned int offset,
				    bool force_tsc_unstable)
{
	struct kvm_vcpu_arch *vcpu = &v->arch;
	struct pvclock_vcpu_time_info *guest_hv_clock;
	unsigned long flags;

	read_lock_irqsave(&gpc->lock, flags);
	while (!kvm_gpc_check(gpc, offset + sizeof(*guest_hv_clock))) {
		read_unlock_irqrestore(&gpc->lock, flags);

		if (kvm_gpc_refresh(gpc, offset + sizeof(*guest_hv_clock)))
			return;

		read_lock_irqsave(&gpc->lock, flags);
	}

	guest_hv_clock = (void *)(gpc->khva + offset);

	/*
	 * This VCPU is paused, but it's legal for a guest to read another
	 * VCPU's kvmclock, so we really have to follow the specification where
	 * it says that version is odd if data is being modified, and even after
	 * it is consistent.
	 */

	guest_hv_clock->version = vcpu->hv_clock.version = (guest_hv_clock->version + 1) | 1;
	smp_wmb();

	/* retain PVCLOCK_GUEST_STOPPED if set in guest copy */
	vcpu->hv_clock.flags |= (guest_hv_clock->flags & PVCLOCK_GUEST_STOPPED);

	if (vcpu->pvclock_set_guest_stopped_request) {
		vcpu->hv_clock.flags |= PVCLOCK_GUEST_STOPPED;
		vcpu->pvclock_set_guest_stopped_request = false;
	}

	memcpy(guest_hv_clock, &vcpu->hv_clock, sizeof(*guest_hv_clock));

	if (force_tsc_unstable)
		guest_hv_clock->flags &= ~PVCLOCK_TSC_STABLE_BIT;

	smp_wmb();

	guest_hv_clock->version = ++vcpu->hv_clock.version;

	kvm_gpc_mark_dirty_in_slot(gpc);
	read_unlock_irqrestore(&gpc->lock, flags);

	trace_kvm_pvclock_update(v->vcpu_id, &vcpu->hv_clock);
}

static int kvm_guest_time_update(struct kvm_vcpu *v)
{
	unsigned long flags, tgt_tsc_khz;
	unsigned seq;
	struct kvm_vcpu_arch *vcpu = &v->arch;
	struct kvm_arch *ka = &v->kvm->arch;
	s64 kernel_ns;
	u64 tsc_timestamp, host_tsc;
	u8 pvclock_flags;
	bool use_master_clock;
#ifdef CONFIG_KVM_XEN
	/*
	 * For Xen guests we may need to override PVCLOCK_TSC_STABLE_BIT as unless
	 * explicitly told to use TSC as its clocksource Xen will not set this bit.
	 * This default behaviour led to bugs in some guest kernels which cause
	 * problems if they observe PVCLOCK_TSC_STABLE_BIT in the pvclock flags.
	 */
	bool xen_pvclock_tsc_unstable =
		ka->xen_hvm_config.flags & KVM_XEN_HVM_CONFIG_PVCLOCK_TSC_UNSTABLE;
#endif

	kernel_ns = 0;
	host_tsc = 0;

	/*
	 * If the host uses TSC clock, then passthrough TSC as stable
	 * to the guest.
	 */
	do {
		seq = read_seqcount_begin(&ka->pvclock_sc);
		use_master_clock = ka->use_master_clock;
		if (use_master_clock) {
			host_tsc = ka->master_cycle_now;
			kernel_ns = ka->master_kernel_ns;
		}
	} while (read_seqcount_retry(&ka->pvclock_sc, seq));

	/* Keep irq disabled to prevent changes to the clock */
	local_irq_save(flags);
	tgt_tsc_khz = get_cpu_tsc_khz();
	if (unlikely(tgt_tsc_khz == 0)) {
		local_irq_restore(flags);
		kvm_make_request(KVM_REQ_CLOCK_UPDATE, v);
		return 1;
	}
	if (!use_master_clock) {
		host_tsc = rdtsc();
		kernel_ns = get_kvmclock_base_ns();
	}

	tsc_timestamp = kvm_read_l1_tsc(v, host_tsc);

	/*
	 * We may have to catch up the TSC to match elapsed wall clock
	 * time for two reasons, even if kvmclock is used.
	 *   1) CPU could have been running below the maximum TSC rate
	 *   2) Broken TSC compensation resets the base at each VCPU
	 *      entry to avoid unknown leaps of TSC even when running
	 *      again on the same CPU.  This may cause apparent elapsed
	 *      time to disappear, and the guest to stand still or run
	 *	very slowly.
	 */
	if (vcpu->tsc_catchup) {
		u64 tsc = compute_guest_tsc(v, kernel_ns);
		if (tsc > tsc_timestamp) {
			adjust_tsc_offset_guest(v, tsc - tsc_timestamp);
			tsc_timestamp = tsc;
		}
	}

	local_irq_restore(flags);

	/* With all the info we got, fill in the values */

	if (kvm_caps.has_tsc_control)
		tgt_tsc_khz = kvm_scale_tsc(tgt_tsc_khz,
					    v->arch.l1_tsc_scaling_ratio);

	if (unlikely(vcpu->hw_tsc_khz != tgt_tsc_khz)) {
		kvm_get_time_scale(NSEC_PER_SEC, tgt_tsc_khz * 1000LL,
				   &vcpu->hv_clock.tsc_shift,
				   &vcpu->hv_clock.tsc_to_system_mul);
		vcpu->hw_tsc_khz = tgt_tsc_khz;
		kvm_xen_update_tsc_info(v);
	}

	vcpu->hv_clock.tsc_timestamp = tsc_timestamp;
	vcpu->hv_clock.system_time = kernel_ns + v->kvm->arch.kvmclock_offset;
	vcpu->last_guest_tsc = tsc_timestamp;

	/* If the host uses TSC clocksource, then it is stable */
	pvclock_flags = 0;
	if (use_master_clock)
		pvclock_flags |= PVCLOCK_TSC_STABLE_BIT;

	vcpu->hv_clock.flags = pvclock_flags;

	if (vcpu->pv_time.active)
		kvm_setup_guest_pvclock(v, &vcpu->pv_time, 0, false);
#ifdef CONFIG_KVM_XEN
	if (vcpu->xen.vcpu_info_cache.active)
		kvm_setup_guest_pvclock(v, &vcpu->xen.vcpu_info_cache,
					offsetof(struct compat_vcpu_info, time),
					xen_pvclock_tsc_unstable);
	if (vcpu->xen.vcpu_time_info_cache.active)
		kvm_setup_guest_pvclock(v, &vcpu->xen.vcpu_time_info_cache, 0,
					xen_pvclock_tsc_unstable);
#endif
	kvm_hv_setup_tsc_page(v->kvm, &vcpu->hv_clock);
	return 0;
}

/*
 * The pvclock_wall_clock ABI tells the guest the wall clock time at
 * which it started (i.e. its epoch, when its kvmclock was zero).
 *
 * In fact those clocks are subtly different; wall clock frequency is
 * adjusted by NTP and has leap seconds, while the kvmclock is a
 * simple function of the TSC without any such adjustment.
 *
 * Perhaps the ABI should have exposed CLOCK_TAI and a ratio between
 * that and kvmclock, but even that would be subject to change over
 * time.
 *
 * Attempt to calculate the epoch at a given moment using the *same*
 * TSC reading via kvm_get_walltime_and_clockread() to obtain both
 * wallclock and kvmclock times, and subtracting one from the other.
 *
 * Fall back to using their values at slightly different moments by
 * calling ktime_get_real_ns() and get_kvmclock_ns() separately.
 */
uint64_t kvm_get_wall_clock_epoch(struct kvm *kvm)
{
#ifdef CONFIG_X86_64
	struct pvclock_vcpu_time_info hv_clock;
	struct kvm_arch *ka = &kvm->arch;
	unsigned long seq, local_tsc_khz;
	struct timespec64 ts;
	uint64_t host_tsc;

	do {
		seq = read_seqcount_begin(&ka->pvclock_sc);

		local_tsc_khz = 0;
		if (!ka->use_master_clock)
			break;

		/*
		 * The TSC read and the call to get_cpu_tsc_khz() must happen
		 * on the same CPU.
		 */
		get_cpu();

		local_tsc_khz = get_cpu_tsc_khz();

		if (local_tsc_khz &&
		    !kvm_get_walltime_and_clockread(&ts, &host_tsc))
			local_tsc_khz = 0; /* Fall back to old method */

		put_cpu();

		/*
		 * These values must be snapshotted within the seqcount loop.
		 * After that, it's just mathematics which can happen on any
		 * CPU at any time.
		 */
		hv_clock.tsc_timestamp = ka->master_cycle_now;
		hv_clock.system_time = ka->master_kernel_ns + ka->kvmclock_offset;

	} while (read_seqcount_retry(&ka->pvclock_sc, seq));

	/*
	 * If the conditions were right, and obtaining the wallclock+TSC was
	 * successful, calculate the KVM clock at the corresponding time and
	 * subtract one from the other to get the guest's epoch in nanoseconds
	 * since 1970-01-01.
	 */
	if (local_tsc_khz) {
		kvm_get_time_scale(NSEC_PER_SEC, local_tsc_khz * NSEC_PER_USEC,
				   &hv_clock.tsc_shift,
				   &hv_clock.tsc_to_system_mul);
		return ts.tv_nsec + NSEC_PER_SEC * ts.tv_sec -
			__pvclock_read_cycles(&hv_clock, host_tsc);
	}
#endif
	return ktime_get_real_ns() - get_kvmclock_ns(kvm);
}

/*
 * kvmclock updates which are isolated to a given vcpu, such as
 * vcpu->cpu migration, should not allow system_timestamp from
 * the rest of the vcpus to remain static. Otherwise ntp frequency
 * correction applies to one vcpu's system_timestamp but not
 * the others.
 *
 * So in those cases, request a kvmclock update for all vcpus.
 * We need to rate-limit these requests though, as they can
 * considerably slow guests that have a large number of vcpus.
 * The time for a remote vcpu to update its kvmclock is bound
 * by the delay we use to rate-limit the updates.
 */

#define KVMCLOCK_UPDATE_DELAY msecs_to_jiffies(100)

static void kvmclock_update_fn(struct work_struct *work)
{
	unsigned long i;
	struct delayed_work *dwork = to_delayed_work(work);
	struct kvm_arch *ka = container_of(dwork, struct kvm_arch,
					   kvmclock_update_work);
	struct kvm *kvm = container_of(ka, struct kvm, arch);
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
		kvm_vcpu_kick(vcpu);
	}
}

static void kvm_gen_kvmclock_update(struct kvm_vcpu *v)
{
	struct kvm *kvm = v->kvm;

	kvm_make_request(KVM_REQ_CLOCK_UPDATE, v);
	schedule_delayed_work(&kvm->arch.kvmclock_update_work,
					KVMCLOCK_UPDATE_DELAY);
}

#define KVMCLOCK_SYNC_PERIOD (300 * HZ)

static void kvmclock_sync_fn(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct kvm_arch *ka = container_of(dwork, struct kvm_arch,
					   kvmclock_sync_work);
	struct kvm *kvm = container_of(ka, struct kvm, arch);

	schedule_delayed_work(&kvm->arch.kvmclock_update_work, 0);
	schedule_delayed_work(&kvm->arch.kvmclock_sync_work,
					KVMCLOCK_SYNC_PERIOD);
}

/* These helpers are safe iff @msr is known to be an MCx bank MSR. */
static bool is_mci_control_msr(u32 msr)
{
	return (msr & 3) == 0;
}
static bool is_mci_status_msr(u32 msr)
{
	return (msr & 3) == 1;
}

/*
 * On AMD, HWCR[McStatusWrEn] controls whether setting MCi_STATUS results in #GP.
 */
static bool can_set_mci_status(struct kvm_vcpu *vcpu)
{
	/* McStatusWrEn enabled? */
	if (guest_cpuid_is_amd_compatible(vcpu))
		return !!(vcpu->arch.msr_hwcr & BIT_ULL(18));

	return false;
}

static int set_msr_mce(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	u64 mcg_cap = vcpu->arch.mcg_cap;
	unsigned bank_num = mcg_cap & 0xff;
	u32 msr = msr_info->index;
	u64 data = msr_info->data;
	u32 offset, last_msr;

	switch (msr) {
	case MSR_IA32_MCG_STATUS:
		vcpu->arch.mcg_status = data;
		break;
	case MSR_IA32_MCG_CTL:
		if (!(mcg_cap & MCG_CTL_P) &&
		    (data || !msr_info->host_initiated))
			return 1;
		if (data != 0 && data != ~(u64)0)
			return 1;
		vcpu->arch.mcg_ctl = data;
		break;
	case MSR_IA32_MC0_CTL2 ... MSR_IA32_MCx_CTL2(KVM_MAX_MCE_BANKS) - 1:
		last_msr = MSR_IA32_MCx_CTL2(bank_num) - 1;
		if (msr > last_msr)
			return 1;

		if (!(mcg_cap & MCG_CMCI_P) && (data || !msr_info->host_initiated))
			return 1;
		/* An attempt to write a 1 to a reserved bit raises #GP */
		if (data & ~(MCI_CTL2_CMCI_EN | MCI_CTL2_CMCI_THRESHOLD_MASK))
			return 1;
		offset = array_index_nospec(msr - MSR_IA32_MC0_CTL2,
					    last_msr + 1 - MSR_IA32_MC0_CTL2);
		vcpu->arch.mci_ctl2_banks[offset] = data;
		break;
	case MSR_IA32_MC0_CTL ... MSR_IA32_MCx_CTL(KVM_MAX_MCE_BANKS) - 1:
		last_msr = MSR_IA32_MCx_CTL(bank_num) - 1;
		if (msr > last_msr)
			return 1;

		/*
		 * Only 0 or all 1s can be written to IA32_MCi_CTL, all other
		 * values are architecturally undefined.  But, some Linux
		 * kernels clear bit 10 in bank 4 to workaround a BIOS/GART TLB
		 * issue on AMD K8s, allow bit 10 to be clear when setting all
		 * other bits in order to avoid an uncaught #GP in the guest.
		 *
		 * UNIXWARE clears bit 0 of MC1_CTL to ignore correctable,
		 * single-bit ECC data errors.
		 */
		if (is_mci_control_msr(msr) &&
		    data != 0 && (data | (1 << 10) | 1) != ~(u64)0)
			return 1;

		/*
		 * All CPUs allow writing 0 to MCi_STATUS MSRs to clear the MSR.
		 * AMD-based CPUs allow non-zero values, but if and only if
		 * HWCR[McStatusWrEn] is set.
		 */
		if (!msr_info->host_initiated && is_mci_status_msr(msr) &&
		    data != 0 && !can_set_mci_status(vcpu))
			return 1;

		offset = array_index_nospec(msr - MSR_IA32_MC0_CTL,
					    last_msr + 1 - MSR_IA32_MC0_CTL);
		vcpu->arch.mce_banks[offset] = data;
		break;
	default:
		return 1;
	}
	return 0;
}

static inline bool kvm_pv_async_pf_enabled(struct kvm_vcpu *vcpu)
{
	u64 mask = KVM_ASYNC_PF_ENABLED | KVM_ASYNC_PF_DELIVERY_AS_INT;

	return (vcpu->arch.apf.msr_en_val & mask) == mask;
}

static int kvm_pv_enable_async_pf(struct kvm_vcpu *vcpu, u64 data)
{
	gpa_t gpa = data & ~0x3f;

	/* Bits 4:5 are reserved, Should be zero */
	if (data & 0x30)
		return 1;

	if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF_VMEXIT) &&
	    (data & KVM_ASYNC_PF_DELIVERY_AS_PF_VMEXIT))
		return 1;

	if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF_INT) &&
	    (data & KVM_ASYNC_PF_DELIVERY_AS_INT))
		return 1;

	if (!lapic_in_kernel(vcpu))
		return data ? 1 : 0;

	vcpu->arch.apf.msr_en_val = data;

	if (!kvm_pv_async_pf_enabled(vcpu)) {
		kvm_clear_async_pf_completion_queue(vcpu);
		kvm_async_pf_hash_reset(vcpu);
		return 0;
	}

	if (kvm_gfn_to_hva_cache_init(vcpu->kvm, &vcpu->arch.apf.data, gpa,
					sizeof(u64)))
		return 1;

	vcpu->arch.apf.send_user_only = !(data & KVM_ASYNC_PF_SEND_ALWAYS);
	vcpu->arch.apf.delivery_as_pf_vmexit = data & KVM_ASYNC_PF_DELIVERY_AS_PF_VMEXIT;

	kvm_async_pf_wakeup_all(vcpu);

	return 0;
}

static int kvm_pv_enable_async_pf_int(struct kvm_vcpu *vcpu, u64 data)
{
	/* Bits 8-63 are reserved */
	if (data >> 8)
		return 1;

	if (!lapic_in_kernel(vcpu))
		return 1;

	vcpu->arch.apf.msr_int_val = data;

	vcpu->arch.apf.vec = data & KVM_ASYNC_PF_VEC_MASK;

	return 0;
}

static void kvmclock_reset(struct kvm_vcpu *vcpu)
{
	kvm_gpc_deactivate(&vcpu->arch.pv_time);
	vcpu->arch.time = 0;
}

static void kvm_vcpu_flush_tlb_all(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.tlb_flush;
	kvm_x86_call(flush_tlb_all)(vcpu);

	/* Flushing all ASIDs flushes the current ASID... */
	kvm_clear_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
}

static void kvm_vcpu_flush_tlb_guest(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.tlb_flush;

	if (!tdp_enabled) {
		/*
		 * A TLB flush on behalf of the guest is equivalent to
		 * INVPCID(all), toggling CR4.PGE, etc., which requires
		 * a forced sync of the shadow page tables.  Ensure all the
		 * roots are synced and the guest TLB in hardware is clean.
		 */
		kvm_mmu_sync_roots(vcpu);
		kvm_mmu_sync_prev_roots(vcpu);
	}

	kvm_x86_call(flush_tlb_guest)(vcpu);

	/*
	 * Flushing all "guest" TLB is always a superset of Hyper-V's fine
	 * grained flushing.
	 */
	kvm_hv_vcpu_purge_flush_tlb(vcpu);
}


static inline void kvm_vcpu_flush_tlb_current(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.tlb_flush;
	kvm_x86_call(flush_tlb_current)(vcpu);
}

/*
 * Service "local" TLB flush requests, which are specific to the current MMU
 * context.  In addition to the generic event handling in vcpu_enter_guest(),
 * TLB flushes that are targeted at an MMU context also need to be serviced
 * prior before nested VM-Enter/VM-Exit.
 */
void kvm_service_local_tlb_flush_requests(struct kvm_vcpu *vcpu)
{
	if (kvm_check_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu))
		kvm_vcpu_flush_tlb_current(vcpu);

	if (kvm_check_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu))
		kvm_vcpu_flush_tlb_guest(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_service_local_tlb_flush_requests);

static void record_steal_time(struct kvm_vcpu *vcpu)
{
	struct gfn_to_hva_cache *ghc = &vcpu->arch.st.cache;
	struct kvm_steal_time __user *st;
	struct kvm_memslots *slots;
	gpa_t gpa = vcpu->arch.st.msr_val & KVM_STEAL_VALID_BITS;
	u64 steal;
	u32 version;

	if (kvm_xen_msr_enabled(vcpu->kvm)) {
		kvm_xen_runstate_set_running(vcpu);
		return;
	}

	if (!(vcpu->arch.st.msr_val & KVM_MSR_ENABLED))
		return;

	if (WARN_ON_ONCE(current->mm != vcpu->kvm->mm))
		return;

	slots = kvm_memslots(vcpu->kvm);

	if (unlikely(slots->generation != ghc->generation ||
		     gpa != ghc->gpa ||
		     kvm_is_error_hva(ghc->hva) || !ghc->memslot)) {
		/* We rely on the fact that it fits in a single page. */
		BUILD_BUG_ON((sizeof(*st) - 1) & KVM_STEAL_VALID_BITS);

		if (kvm_gfn_to_hva_cache_init(vcpu->kvm, ghc, gpa, sizeof(*st)) ||
		    kvm_is_error_hva(ghc->hva) || !ghc->memslot)
			return;
	}

	st = (struct kvm_steal_time __user *)ghc->hva;
	/*
	 * Doing a TLB flush here, on the guest's behalf, can avoid
	 * expensive IPIs.
	 */
	if (guest_pv_has(vcpu, KVM_FEATURE_PV_TLB_FLUSH)) {
		u8 st_preempted = 0;
		int err = -EFAULT;

		if (!user_access_begin(st, sizeof(*st)))
			return;

		asm volatile("1: xchgb %0, %2\n"
			     "xor %1, %1\n"
			     "2:\n"
			     _ASM_EXTABLE_UA(1b, 2b)
			     : "+q" (st_preempted),
			       "+&r" (err),
			       "+m" (st->preempted));
		if (err)
			goto out;

		user_access_end();

		vcpu->arch.st.preempted = 0;

		trace_kvm_pv_tlb_flush(vcpu->vcpu_id,
				       st_preempted & KVM_VCPU_FLUSH_TLB);
		if (st_preempted & KVM_VCPU_FLUSH_TLB)
			kvm_vcpu_flush_tlb_guest(vcpu);

		if (!user_access_begin(st, sizeof(*st)))
			goto dirty;
	} else {
		if (!user_access_begin(st, sizeof(*st)))
			return;

		unsafe_put_user(0, &st->preempted, out);
		vcpu->arch.st.preempted = 0;
	}

	unsafe_get_user(version, &st->version, out);
	if (version & 1)
		version += 1;  /* first time write, random junk */

	version += 1;
	unsafe_put_user(version, &st->version, out);

	smp_wmb();

	unsafe_get_user(steal, &st->steal, out);
	steal += current->sched_info.run_delay -
		vcpu->arch.st.last_steal;
	vcpu->arch.st.last_steal = current->sched_info.run_delay;
	unsafe_put_user(steal, &st->steal, out);

	version += 1;
	unsafe_put_user(version, &st->version, out);

 out:
	user_access_end();
 dirty:
	mark_page_dirty_in_slot(vcpu->kvm, ghc->memslot, gpa_to_gfn(ghc->gpa));
}

int kvm_set_msr_common(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	u32 msr = msr_info->index;
	u64 data = msr_info->data;

	if (msr && msr == vcpu->kvm->arch.xen_hvm_config.msr)
		return kvm_xen_write_hypercall_page(vcpu, data);

	switch (msr) {
	case MSR_AMD64_NB_CFG:
	case MSR_IA32_UCODE_WRITE:
	case MSR_VM_HSAVE_PA:
	case MSR_AMD64_PATCH_LOADER:
	case MSR_AMD64_BU_CFG2:
	case MSR_AMD64_DC_CFG:
	case MSR_AMD64_TW_CFG:
	case MSR_F15H_EX_CFG:
		break;

	case MSR_IA32_UCODE_REV:
		if (msr_info->host_initiated)
			vcpu->arch.microcode_version = data;
		break;
	case MSR_IA32_ARCH_CAPABILITIES:
		if (!msr_info->host_initiated ||
		    !guest_cpuid_has(vcpu, X86_FEATURE_ARCH_CAPABILITIES))
			return KVM_MSR_RET_UNSUPPORTED;
		vcpu->arch.arch_capabilities = data;
		break;
	case MSR_IA32_PERF_CAPABILITIES:
		if (!msr_info->host_initiated ||
		    !guest_cpuid_has(vcpu, X86_FEATURE_PDCM))
			return KVM_MSR_RET_UNSUPPORTED;

		if (data & ~kvm_caps.supported_perf_cap)
			return 1;

		/*
		 * Note, this is not just a performance optimization!  KVM
		 * disallows changing feature MSRs after the vCPU has run; PMU
		 * refresh will bug the VM if called after the vCPU has run.
		 */
		if (vcpu->arch.perf_capabilities == data)
			break;

		vcpu->arch.perf_capabilities = data;
		kvm_pmu_refresh(vcpu);
		break;
	case MSR_IA32_PRED_CMD: {
		u64 reserved_bits = ~(PRED_CMD_IBPB | PRED_CMD_SBPB);

		if (!msr_info->host_initiated) {
			if ((!guest_has_pred_cmd_msr(vcpu)))
				return 1;

			if (!guest_cpuid_has(vcpu, X86_FEATURE_SPEC_CTRL) &&
			    !guest_cpuid_has(vcpu, X86_FEATURE_AMD_IBPB))
				reserved_bits |= PRED_CMD_IBPB;

			if (!guest_cpuid_has(vcpu, X86_FEATURE_SBPB))
				reserved_bits |= PRED_CMD_SBPB;
		}

		if (!boot_cpu_has(X86_FEATURE_IBPB))
			reserved_bits |= PRED_CMD_IBPB;

		if (!boot_cpu_has(X86_FEATURE_SBPB))
			reserved_bits |= PRED_CMD_SBPB;

		if (data & reserved_bits)
			return 1;

		if (!data)
			break;

		wrmsrl(MSR_IA32_PRED_CMD, data);
		break;
	}
	case MSR_IA32_FLUSH_CMD:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_FLUSH_L1D))
			return 1;

		if (!boot_cpu_has(X86_FEATURE_FLUSH_L1D) || (data & ~L1D_FLUSH))
			return 1;
		if (!data)
			break;

		wrmsrl(MSR_IA32_FLUSH_CMD, L1D_FLUSH);
		break;
	case MSR_EFER:
		return set_efer(vcpu, msr_info);
	case MSR_K7_HWCR:
		data &= ~(u64)0x40;	/* ignore flush filter disable */
		data &= ~(u64)0x100;	/* ignore ignne emulation enable */
		data &= ~(u64)0x8;	/* ignore TLB cache disable */

		/*
		 * Allow McStatusWrEn and TscFreqSel. (Linux guests from v3.2
		 * through at least v6.6 whine if TscFreqSel is clear,
		 * depending on F/M/S.
		 */
		if (data & ~(BIT_ULL(18) | BIT_ULL(24))) {
			kvm_pr_unimpl_wrmsr(vcpu, msr, data);
			return 1;
		}
		vcpu->arch.msr_hwcr = data;
		break;
	case MSR_FAM10H_MMIO_CONF_BASE:
		if (data != 0) {
			kvm_pr_unimpl_wrmsr(vcpu, msr, data);
			return 1;
		}
		break;
	case MSR_IA32_CR_PAT:
		if (!kvm_pat_valid(data))
			return 1;

		vcpu->arch.pat = data;
		break;
	case MTRRphysBase_MSR(0) ... MSR_MTRRfix4K_F8000:
	case MSR_MTRRdefType:
		return kvm_mtrr_set_msr(vcpu, msr, data);
	case MSR_IA32_APICBASE:
		return kvm_apic_set_base(vcpu, data, msr_info->host_initiated);
	case APIC_BASE_MSR ... APIC_BASE_MSR + 0xff:
		return kvm_x2apic_msr_write(vcpu, msr, data);
	case MSR_IA32_TSC_DEADLINE:
		kvm_set_lapic_tscdeadline_msr(vcpu, data);
		break;
	case MSR_IA32_TSC_ADJUST:
		if (guest_cpuid_has(vcpu, X86_FEATURE_TSC_ADJUST)) {
			if (!msr_info->host_initiated) {
				s64 adj = data - vcpu->arch.ia32_tsc_adjust_msr;
				adjust_tsc_offset_guest(vcpu, adj);
				/* Before back to guest, tsc_timestamp must be adjusted
				 * as well, otherwise guest's percpu pvclock time could jump.
				 */
				kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
			}
			vcpu->arch.ia32_tsc_adjust_msr = data;
		}
		break;
	case MSR_IA32_MISC_ENABLE: {
		u64 old_val = vcpu->arch.ia32_misc_enable_msr;

		if (!msr_info->host_initiated) {
			/* RO bits */
			if ((old_val ^ data) & MSR_IA32_MISC_ENABLE_PMU_RO_MASK)
				return 1;

			/* R bits, i.e. writes are ignored, but don't fault. */
			data = data & ~MSR_IA32_MISC_ENABLE_EMON;
			data |= old_val & MSR_IA32_MISC_ENABLE_EMON;
		}

		if (!kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_MISC_ENABLE_NO_MWAIT) &&
		    ((old_val ^ data)  & MSR_IA32_MISC_ENABLE_MWAIT)) {
			if (!guest_cpuid_has(vcpu, X86_FEATURE_XMM3))
				return 1;
			vcpu->arch.ia32_misc_enable_msr = data;
			kvm_update_cpuid_runtime(vcpu);
		} else {
			vcpu->arch.ia32_misc_enable_msr = data;
		}
		break;
	}
	case MSR_IA32_SMBASE:
		if (!IS_ENABLED(CONFIG_KVM_SMM) || !msr_info->host_initiated)
			return 1;
		vcpu->arch.smbase = data;
		break;
	case MSR_IA32_POWER_CTL:
		vcpu->arch.msr_ia32_power_ctl = data;
		break;
	case MSR_IA32_TSC:
		if (msr_info->host_initiated) {
			kvm_synchronize_tsc(vcpu, &data);
		} else {
			u64 adj = kvm_compute_l1_tsc_offset(vcpu, data) - vcpu->arch.l1_tsc_offset;
			adjust_tsc_offset_guest(vcpu, adj);
			vcpu->arch.ia32_tsc_adjust_msr += adj;
		}
		break;
	case MSR_IA32_XSS:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_XSAVES))
			return 1;
		/*
		 * KVM supports exposing PT to the guest, but does not support
		 * IA32_XSS[bit 8]. Guests have to use RDMSR/WRMSR rather than
		 * XSAVES/XRSTORS to save/restore PT MSRs.
		 */
		if (data & ~kvm_caps.supported_xss)
			return 1;
		vcpu->arch.ia32_xss = data;
		kvm_update_cpuid_runtime(vcpu);
		break;
	case MSR_SMI_COUNT:
		if (!msr_info->host_initiated)
			return 1;
		vcpu->arch.smi_count = data;
		break;
	case MSR_KVM_WALL_CLOCK_NEW:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE2))
			return 1;

		vcpu->kvm->arch.wall_clock = data;
		kvm_write_wall_clock(vcpu->kvm, data, 0);
		break;
	case MSR_KVM_WALL_CLOCK:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE))
			return 1;

		vcpu->kvm->arch.wall_clock = data;
		kvm_write_wall_clock(vcpu->kvm, data, 0);
		break;
	case MSR_KVM_SYSTEM_TIME_NEW:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE2))
			return 1;

		kvm_write_system_time(vcpu, data, false, msr_info->host_initiated);
		break;
	case MSR_KVM_SYSTEM_TIME:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE))
			return 1;

		kvm_write_system_time(vcpu, data, true,  msr_info->host_initiated);
		break;
	case MSR_KVM_ASYNC_PF_EN:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF))
			return 1;

		if (kvm_pv_enable_async_pf(vcpu, data))
			return 1;
		break;
	case MSR_KVM_ASYNC_PF_INT:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF_INT))
			return 1;

		if (kvm_pv_enable_async_pf_int(vcpu, data))
			return 1;
		break;
	case MSR_KVM_ASYNC_PF_ACK:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF_INT))
			return 1;
		if (data & 0x1) {
			vcpu->arch.apf.pageready_pending = false;
			kvm_check_async_pf_completion(vcpu);
		}
		break;
	case MSR_KVM_STEAL_TIME:
		if (!guest_pv_has(vcpu, KVM_FEATURE_STEAL_TIME))
			return 1;

		if (unlikely(!sched_info_on()))
			return 1;

		if (data & KVM_STEAL_RESERVED_MASK)
			return 1;

		vcpu->arch.st.msr_val = data;

		if (!(data & KVM_MSR_ENABLED))
			break;

		kvm_make_request(KVM_REQ_STEAL_UPDATE, vcpu);

		break;
	case MSR_KVM_PV_EOI_EN:
		if (!guest_pv_has(vcpu, KVM_FEATURE_PV_EOI))
			return 1;

		if (kvm_lapic_set_pv_eoi(vcpu, data, sizeof(u8)))
			return 1;
		break;

	case MSR_KVM_POLL_CONTROL:
		if (!guest_pv_has(vcpu, KVM_FEATURE_POLL_CONTROL))
			return 1;

		/* only enable bit supported */
		if (data & (-1ULL << 1))
			return 1;

		vcpu->arch.msr_kvm_poll_control = data;
		break;

	case MSR_IA32_MCG_CTL:
	case MSR_IA32_MCG_STATUS:
	case MSR_IA32_MC0_CTL ... MSR_IA32_MCx_CTL(KVM_MAX_MCE_BANKS) - 1:
	case MSR_IA32_MC0_CTL2 ... MSR_IA32_MCx_CTL2(KVM_MAX_MCE_BANKS) - 1:
		return set_msr_mce(vcpu, msr_info);

	case MSR_K7_PERFCTR0 ... MSR_K7_PERFCTR3:
	case MSR_P6_PERFCTR0 ... MSR_P6_PERFCTR1:
	case MSR_K7_EVNTSEL0 ... MSR_K7_EVNTSEL3:
	case MSR_P6_EVNTSEL0 ... MSR_P6_EVNTSEL1:
		if (kvm_pmu_is_valid_msr(vcpu, msr))
			return kvm_pmu_set_msr(vcpu, msr_info);

		if (data)
			kvm_pr_unimpl_wrmsr(vcpu, msr, data);
		break;
	case MSR_K7_CLK_CTL:
		/*
		 * Ignore all writes to this no longer documented MSR.
		 * Writes are only relevant for old K7 processors,
		 * all pre-dating SVM, but a recommended workaround from
		 * AMD for these chips. It is possible to specify the
		 * affected processor models on the command line, hence
		 * the need to ignore the workaround.
		 */
		break;
#ifdef CONFIG_KVM_HYPERV
	case HV_X64_MSR_GUEST_OS_ID ... HV_X64_MSR_SINT15:
	case HV_X64_MSR_SYNDBG_CONTROL ... HV_X64_MSR_SYNDBG_PENDING_BUFFER:
	case HV_X64_MSR_SYNDBG_OPTIONS:
	case HV_X64_MSR_CRASH_P0 ... HV_X64_MSR_CRASH_P4:
	case HV_X64_MSR_CRASH_CTL:
	case HV_X64_MSR_STIMER0_CONFIG ... HV_X64_MSR_STIMER3_COUNT:
	case HV_X64_MSR_REENLIGHTENMENT_CONTROL:
	case HV_X64_MSR_TSC_EMULATION_CONTROL:
	case HV_X64_MSR_TSC_EMULATION_STATUS:
	case HV_X64_MSR_TSC_INVARIANT_CONTROL:
		return kvm_hv_set_msr_common(vcpu, msr, data,
					     msr_info->host_initiated);
#endif
	case MSR_IA32_BBL_CR_CTL3:
		/* Drop writes to this legacy MSR -- see rdmsr
		 * counterpart for further detail.
		 */
		kvm_pr_unimpl_wrmsr(vcpu, msr, data);
		break;
	case MSR_AMD64_OSVW_ID_LENGTH:
		if (!guest_cpuid_has(vcpu, X86_FEATURE_OSVW))
			return 1;
		vcpu->arch.osvw.length = data;
		break;
	case MSR_AMD64_OSVW_STATUS:
		if (!guest_cpuid_has(vcpu, X86_FEATURE_OSVW))
			return 1;
		vcpu->arch.osvw.status = data;
		break;
	case MSR_PLATFORM_INFO:
		if (!msr_info->host_initiated)
			return 1;
		vcpu->arch.msr_platform_info = data;
		break;
	case MSR_MISC_FEATURES_ENABLES:
		if (data & ~MSR_MISC_FEATURES_ENABLES_CPUID_FAULT ||
		    (data & MSR_MISC_FEATURES_ENABLES_CPUID_FAULT &&
		     !supports_cpuid_fault(vcpu)))
			return 1;
		vcpu->arch.msr_misc_features_enables = data;
		break;
#ifdef CONFIG_X86_64
	case MSR_IA32_XFD:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_XFD))
			return 1;

		if (data & ~kvm_guest_supported_xfd(vcpu))
			return 1;

		fpu_update_guest_xfd(&vcpu->arch.guest_fpu, data);
		break;
	case MSR_IA32_XFD_ERR:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_XFD))
			return 1;

		if (data & ~kvm_guest_supported_xfd(vcpu))
			return 1;

		vcpu->arch.guest_fpu.xfd_err = data;
		break;
#endif
	default:
		if (kvm_pmu_is_valid_msr(vcpu, msr))
			return kvm_pmu_set_msr(vcpu, msr_info);

		return KVM_MSR_RET_UNSUPPORTED;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_msr_common);

static int get_msr_mce(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata, bool host)
{
	u64 data;
	u64 mcg_cap = vcpu->arch.mcg_cap;
	unsigned bank_num = mcg_cap & 0xff;
	u32 offset, last_msr;

	switch (msr) {
	case MSR_IA32_P5_MC_ADDR:
	case MSR_IA32_P5_MC_TYPE:
		data = 0;
		break;
	case MSR_IA32_MCG_CAP:
		data = vcpu->arch.mcg_cap;
		break;
	case MSR_IA32_MCG_CTL:
		if (!(mcg_cap & MCG_CTL_P) && !host)
			return 1;
		data = vcpu->arch.mcg_ctl;
		break;
	case MSR_IA32_MCG_STATUS:
		data = vcpu->arch.mcg_status;
		break;
	case MSR_IA32_MC0_CTL2 ... MSR_IA32_MCx_CTL2(KVM_MAX_MCE_BANKS) - 1:
		last_msr = MSR_IA32_MCx_CTL2(bank_num) - 1;
		if (msr > last_msr)
			return 1;

		if (!(mcg_cap & MCG_CMCI_P) && !host)
			return 1;
		offset = array_index_nospec(msr - MSR_IA32_MC0_CTL2,
					    last_msr + 1 - MSR_IA32_MC0_CTL2);
		data = vcpu->arch.mci_ctl2_banks[offset];
		break;
	case MSR_IA32_MC0_CTL ... MSR_IA32_MCx_CTL(KVM_MAX_MCE_BANKS) - 1:
		last_msr = MSR_IA32_MCx_CTL(bank_num) - 1;
		if (msr > last_msr)
			return 1;

		offset = array_index_nospec(msr - MSR_IA32_MC0_CTL,
					    last_msr + 1 - MSR_IA32_MC0_CTL);
		data = vcpu->arch.mce_banks[offset];
		break;
	default:
		return 1;
	}
	*pdata = data;
	return 0;
}

int kvm_get_msr_common(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	switch (msr_info->index) {
	case MSR_IA32_PLATFORM_ID:
	case MSR_IA32_EBL_CR_POWERON:
	case MSR_IA32_LASTBRANCHFROMIP:
	case MSR_IA32_LASTBRANCHTOIP:
	case MSR_IA32_LASTINTFROMIP:
	case MSR_IA32_LASTINTTOIP:
	case MSR_AMD64_SYSCFG:
	case MSR_K8_TSEG_ADDR:
	case MSR_K8_TSEG_MASK:
	case MSR_VM_HSAVE_PA:
	case MSR_K8_INT_PENDING_MSG:
	case MSR_AMD64_NB_CFG:
	case MSR_FAM10H_MMIO_CONF_BASE:
	case MSR_AMD64_BU_CFG2:
	case MSR_IA32_PERF_CTL:
	case MSR_AMD64_DC_CFG:
	case MSR_AMD64_TW_CFG:
	case MSR_F15H_EX_CFG:
	/*
	 * Intel Sandy Bridge CPUs must support the RAPL (running average power
	 * limit) MSRs. Just return 0, as we do not want to expose the host
	 * data here. Do not conditionalize this on CPUID, as KVM does not do
	 * so for existing CPU-specific MSRs.
	 */
	case MSR_RAPL_POWER_UNIT:
	case MSR_PP0_ENERGY_STATUS:	/* Power plane 0 (core) */
	case MSR_PP1_ENERGY_STATUS:	/* Power plane 1 (graphics uncore) */
	case MSR_PKG_ENERGY_STATUS:	/* Total package */
	case MSR_DRAM_ENERGY_STATUS:	/* DRAM controller */
		msr_info->data = 0;
		break;
	case MSR_K7_EVNTSEL0 ... MSR_K7_EVNTSEL3:
	case MSR_K7_PERFCTR0 ... MSR_K7_PERFCTR3:
	case MSR_P6_PERFCTR0 ... MSR_P6_PERFCTR1:
	case MSR_P6_EVNTSEL0 ... MSR_P6_EVNTSEL1:
		if (kvm_pmu_is_valid_msr(vcpu, msr_info->index))
			return kvm_pmu_get_msr(vcpu, msr_info);
		msr_info->data = 0;
		break;
	case MSR_IA32_UCODE_REV:
		msr_info->data = vcpu->arch.microcode_version;
		break;
	case MSR_IA32_ARCH_CAPABILITIES:
		if (!guest_cpuid_has(vcpu, X86_FEATURE_ARCH_CAPABILITIES))
			return KVM_MSR_RET_UNSUPPORTED;
		msr_info->data = vcpu->arch.arch_capabilities;
		break;
	case MSR_IA32_PERF_CAPABILITIES:
		if (!guest_cpuid_has(vcpu, X86_FEATURE_PDCM))
			return KVM_MSR_RET_UNSUPPORTED;
		msr_info->data = vcpu->arch.perf_capabilities;
		break;
	case MSR_IA32_POWER_CTL:
		msr_info->data = vcpu->arch.msr_ia32_power_ctl;
		break;
	case MSR_IA32_TSC: {
		/*
		 * Intel SDM states that MSR_IA32_TSC read adds the TSC offset
		 * even when not intercepted. AMD manual doesn't explicitly
		 * state this but appears to behave the same.
		 *
		 * On userspace reads and writes, however, we unconditionally
		 * return L1's TSC value to ensure backwards-compatible
		 * behavior for migration.
		 */
		u64 offset, ratio;

		if (msr_info->host_initiated) {
			offset = vcpu->arch.l1_tsc_offset;
			ratio = vcpu->arch.l1_tsc_scaling_ratio;
		} else {
			offset = vcpu->arch.tsc_offset;
			ratio = vcpu->arch.tsc_scaling_ratio;
		}

		msr_info->data = kvm_scale_tsc(rdtsc(), ratio) + offset;
		break;
	}
	case MSR_IA32_CR_PAT:
		msr_info->data = vcpu->arch.pat;
		break;
	case MSR_MTRRcap:
	case MTRRphysBase_MSR(0) ... MSR_MTRRfix4K_F8000:
	case MSR_MTRRdefType:
		return kvm_mtrr_get_msr(vcpu, msr_info->index, &msr_info->data);
	case 0xcd: /* fsb frequency */
		msr_info->data = 3;
		break;
		/*
		 * MSR_EBC_FREQUENCY_ID
		 * Conservative value valid for even the basic CPU models.
		 * Models 0,1: 000 in bits 23:21 indicating a bus speed of
		 * 100MHz, model 2 000 in bits 18:16 indicating 100MHz,
		 * and 266MHz for model 3, or 4. Set Core Clock
		 * Frequency to System Bus Frequency Ratio to 1 (bits
		 * 31:24) even though these are only valid for CPU
		 * models > 2, however guests may end up dividing or
		 * multiplying by zero otherwise.
		 */
	case MSR_EBC_FREQUENCY_ID:
		msr_info->data = 1 << 24;
		break;
	case MSR_IA32_APICBASE:
		msr_info->data = vcpu->arch.apic_base;
		break;
	case APIC_BASE_MSR ... APIC_BASE_MSR + 0xff:
		return kvm_x2apic_msr_read(vcpu, msr_info->index, &msr_info->data);
	case MSR_IA32_TSC_DEADLINE:
		msr_info->data = kvm_get_lapic_tscdeadline_msr(vcpu);
		break;
	case MSR_IA32_TSC_ADJUST:
		msr_info->data = (u64)vcpu->arch.ia32_tsc_adjust_msr;
		break;
	case MSR_IA32_MISC_ENABLE:
		msr_info->data = vcpu->arch.ia32_misc_enable_msr;
		break;
	case MSR_IA32_SMBASE:
		if (!IS_ENABLED(CONFIG_KVM_SMM) || !msr_info->host_initiated)
			return 1;
		msr_info->data = vcpu->arch.smbase;
		break;
	case MSR_SMI_COUNT:
		msr_info->data = vcpu->arch.smi_count;
		break;
	case MSR_IA32_PERF_STATUS:
		/* TSC increment by tick */
		msr_info->data = 1000ULL;
		/* CPU multiplier */
		msr_info->data |= (((uint64_t)4ULL) << 40);
		break;
	case MSR_EFER:
		msr_info->data = vcpu->arch.efer;
		break;
	case MSR_KVM_WALL_CLOCK:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE))
			return 1;

		msr_info->data = vcpu->kvm->arch.wall_clock;
		break;
	case MSR_KVM_WALL_CLOCK_NEW:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE2))
			return 1;

		msr_info->data = vcpu->kvm->arch.wall_clock;
		break;
	case MSR_KVM_SYSTEM_TIME:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE))
			return 1;

		msr_info->data = vcpu->arch.time;
		break;
	case MSR_KVM_SYSTEM_TIME_NEW:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE2))
			return 1;

		msr_info->data = vcpu->arch.time;
		break;
	case MSR_KVM_ASYNC_PF_EN:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF))
			return 1;

		msr_info->data = vcpu->arch.apf.msr_en_val;
		break;
	case MSR_KVM_ASYNC_PF_INT:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF_INT))
			return 1;

		msr_info->data = vcpu->arch.apf.msr_int_val;
		break;
	case MSR_KVM_ASYNC_PF_ACK:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF_INT))
			return 1;

		msr_info->data = 0;
		break;
	case MSR_KVM_STEAL_TIME:
		if (!guest_pv_has(vcpu, KVM_FEATURE_STEAL_TIME))
			return 1;

		msr_info->data = vcpu->arch.st.msr_val;
		break;
	case MSR_KVM_PV_EOI_EN:
		if (!guest_pv_has(vcpu, KVM_FEATURE_PV_EOI))
			return 1;

		msr_info->data = vcpu->arch.pv_eoi.msr_val;
		break;
	case MSR_KVM_POLL_CONTROL:
		if (!guest_pv_has(vcpu, KVM_FEATURE_POLL_CONTROL))
			return 1;

		msr_info->data = vcpu->arch.msr_kvm_poll_control;
		break;
	case MSR_IA32_P5_MC_ADDR:
	case MSR_IA32_P5_MC_TYPE:
	case MSR_IA32_MCG_CAP:
	case MSR_IA32_MCG_CTL:
	case MSR_IA32_MCG_STATUS:
	case MSR_IA32_MC0_CTL ... MSR_IA32_MCx_CTL(KVM_MAX_MCE_BANKS) - 1:
	case MSR_IA32_MC0_CTL2 ... MSR_IA32_MCx_CTL2(KVM_MAX_MCE_BANKS) - 1:
		return get_msr_mce(vcpu, msr_info->index, &msr_info->data,
				   msr_info->host_initiated);
	case MSR_IA32_XSS:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_XSAVES))
			return 1;
		msr_info->data = vcpu->arch.ia32_xss;
		break;
	case MSR_K7_CLK_CTL:
		/*
		 * Provide expected ramp-up count for K7. All other
		 * are set to zero, indicating minimum divisors for
		 * every field.
		 *
		 * This prevents guest kernels on AMD host with CPU
		 * type 6, model 8 and higher from exploding due to
		 * the rdmsr failing.
		 */
		msr_info->data = 0x20000000;
		break;
#ifdef CONFIG_KVM_HYPERV
	case HV_X64_MSR_GUEST_OS_ID ... HV_X64_MSR_SINT15:
	case HV_X64_MSR_SYNDBG_CONTROL ... HV_X64_MSR_SYNDBG_PENDING_BUFFER:
	case HV_X64_MSR_SYNDBG_OPTIONS:
	case HV_X64_MSR_CRASH_P0 ... HV_X64_MSR_CRASH_P4:
	case HV_X64_MSR_CRASH_CTL:
	case HV_X64_MSR_STIMER0_CONFIG ... HV_X64_MSR_STIMER3_COUNT:
	case HV_X64_MSR_REENLIGHTENMENT_CONTROL:
	case HV_X64_MSR_TSC_EMULATION_CONTROL:
	case HV_X64_MSR_TSC_EMULATION_STATUS:
	case HV_X64_MSR_TSC_INVARIANT_CONTROL:
		return kvm_hv_get_msr_common(vcpu,
					     msr_info->index, &msr_info->data,
					     msr_info->host_initiated);
#endif
	case MSR_IA32_BBL_CR_CTL3:
		/* This legacy MSR exists but isn't fully documented in current
		 * silicon.  It is however accessed by winxp in very narrow
		 * scenarios where it sets bit #19, itself documented as
		 * a "reserved" bit.  Best effort attempt to source coherent
		 * read data here should the balance of the register be
		 * interpreted by the guest:
		 *
		 * L2 cache control register 3: 64GB range, 256KB size,
		 * enabled, latency 0x1, configured
		 */
		msr_info->data = 0xbe702111;
		break;
	case MSR_AMD64_OSVW_ID_LENGTH:
		if (!guest_cpuid_has(vcpu, X86_FEATURE_OSVW))
			return 1;
		msr_info->data = vcpu->arch.osvw.length;
		break;
	case MSR_AMD64_OSVW_STATUS:
		if (!guest_cpuid_has(vcpu, X86_FEATURE_OSVW))
			return 1;
		msr_info->data = vcpu->arch.osvw.status;
		break;
	case MSR_PLATFORM_INFO:
		if (!msr_info->host_initiated &&
		    !vcpu->kvm->arch.guest_can_read_msr_platform_info)
			return 1;
		msr_info->data = vcpu->arch.msr_platform_info;
		break;
	case MSR_MISC_FEATURES_ENABLES:
		msr_info->data = vcpu->arch.msr_misc_features_enables;
		break;
	case MSR_K7_HWCR:
		msr_info->data = vcpu->arch.msr_hwcr;
		break;
#ifdef CONFIG_X86_64
	case MSR_IA32_XFD:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_XFD))
			return 1;

		msr_info->data = vcpu->arch.guest_fpu.fpstate->xfd;
		break;
	case MSR_IA32_XFD_ERR:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_XFD))
			return 1;

		msr_info->data = vcpu->arch.guest_fpu.xfd_err;
		break;
#endif
	default:
		if (kvm_pmu_is_valid_msr(vcpu, msr_info->index))
			return kvm_pmu_get_msr(vcpu, msr_info);

		return KVM_MSR_RET_UNSUPPORTED;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_get_msr_common);

/*
 * Read or write a bunch of msrs. All parameters are kernel addresses.
 *
 * @return number of msrs set successfully.
 */
static int __msr_io(struct kvm_vcpu *vcpu, struct kvm_msrs *msrs,
		    struct kvm_msr_entry *entries,
		    int (*do_msr)(struct kvm_vcpu *vcpu,
				  unsigned index, u64 *data))
{
	int i;

	for (i = 0; i < msrs->nmsrs; ++i)
		if (do_msr(vcpu, entries[i].index, &entries[i].data))
			break;

	return i;
}

/*
 * Read or write a bunch of msrs. Parameters are user addresses.
 *
 * @return number of msrs set successfully.
 */
static int msr_io(struct kvm_vcpu *vcpu, struct kvm_msrs __user *user_msrs,
		  int (*do_msr)(struct kvm_vcpu *vcpu,
				unsigned index, u64 *data),
		  int writeback)
{
	struct kvm_msrs msrs;
	struct kvm_msr_entry *entries;
	unsigned size;
	int r;

	r = -EFAULT;
	if (copy_from_user(&msrs, user_msrs, sizeof(msrs)))
		goto out;

	r = -E2BIG;
	if (msrs.nmsrs >= MAX_IO_MSRS)
		goto out;

	size = sizeof(struct kvm_msr_entry) * msrs.nmsrs;
	entries = memdup_user(user_msrs->entries, size);
	if (IS_ERR(entries)) {
		r = PTR_ERR(entries);
		goto out;
	}

	r = __msr_io(vcpu, &msrs, entries, do_msr);

	if (writeback && copy_to_user(user_msrs->entries, entries, size))
		r = -EFAULT;

	kfree(entries);
out:
	return r;
}

static inline bool kvm_can_mwait_in_guest(void)
{
	return boot_cpu_has(X86_FEATURE_MWAIT) &&
		!boot_cpu_has_bug(X86_BUG_MONITOR) &&
		boot_cpu_has(X86_FEATURE_ARAT);
}

#ifdef CONFIG_KVM_HYPERV
static int kvm_ioctl_get_supported_hv_cpuid(struct kvm_vcpu *vcpu,
					    struct kvm_cpuid2 __user *cpuid_arg)
{
	struct kvm_cpuid2 cpuid;
	int r;

	r = -EFAULT;
	if (copy_from_user(&cpuid, cpuid_arg, sizeof(cpuid)))
		return r;

	r = kvm_get_hv_cpuid(vcpu, &cpuid, cpuid_arg->entries);
	if (r)
		return r;

	r = -EFAULT;
	if (copy_to_user(cpuid_arg, &cpuid, sizeof(cpuid)))
		return r;

	return 0;
}
#endif

static bool kvm_is_vm_type_supported(unsigned long type)
{
	return type < 32 && (kvm_caps.supported_vm_types & BIT(type));
}

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r = 0;

	switch (ext) {
	case KVM_CAP_IRQCHIP:
	case KVM_CAP_HLT:
	case KVM_CAP_MMU_SHADOW_CACHE_CONTROL:
	case KVM_CAP_SET_TSS_ADDR:
	case KVM_CAP_EXT_CPUID:
	case KVM_CAP_EXT_EMUL_CPUID:
	case KVM_CAP_CLOCKSOURCE:
	case KVM_CAP_PIT:
	case KVM_CAP_NOP_IO_DELAY:
	case KVM_CAP_MP_STATE:
	case KVM_CAP_SYNC_MMU:
	case KVM_CAP_USER_NMI:
	case KVM_CAP_REINJECT_CONTROL:
	case KVM_CAP_IRQ_INJECT_STATUS:
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_IOEVENTFD_NO_LENGTH:
	case KVM_CAP_PIT2:
	case KVM_CAP_PIT_STATE2:
	case KVM_CAP_SET_IDENTITY_MAP_ADDR:
	case KVM_CAP_VCPU_EVENTS:
#ifdef CONFIG_KVM_HYPERV
	case KVM_CAP_HYPERV:
	case KVM_CAP_HYPERV_VAPIC:
	case KVM_CAP_HYPERV_SPIN:
	case KVM_CAP_HYPERV_TIME:
	case KVM_CAP_HYPERV_SYNIC:
	case KVM_CAP_HYPERV_SYNIC2:
	case KVM_CAP_HYPERV_VP_INDEX:
	case KVM_CAP_HYPERV_EVENTFD:
	case KVM_CAP_HYPERV_TLBFLUSH:
	case KVM_CAP_HYPERV_SEND_IPI:
	case KVM_CAP_HYPERV_CPUID:
	case KVM_CAP_HYPERV_ENFORCE_CPUID:
	case KVM_CAP_SYS_HYPERV_CPUID:
#endif
	case KVM_CAP_PCI_SEGMENT:
	case KVM_CAP_DEBUGREGS:
	case KVM_CAP_X86_ROBUST_SINGLESTEP:
	case KVM_CAP_XSAVE:
	case KVM_CAP_ASYNC_PF:
	case KVM_CAP_ASYNC_PF_INT:
	case KVM_CAP_GET_TSC_KHZ:
	case KVM_CAP_KVMCLOCK_CTRL:
	case KVM_CAP_IOAPIC_POLARITY_IGNORED:
	case KVM_CAP_TSC_DEADLINE_TIMER:
	case KVM_CAP_DISABLE_QUIRKS:
	case KVM_CAP_SET_BOOT_CPU_ID:
 	case KVM_CAP_SPLIT_IRQCHIP:
	case KVM_CAP_IMMEDIATE_EXIT:
	case KVM_CAP_PMU_EVENT_FILTER:
	case KVM_CAP_PMU_EVENT_MASKED_EVENTS:
	case KVM_CAP_GET_MSR_FEATURES:
	case KVM_CAP_MSR_PLATFORM_INFO:
	case KVM_CAP_EXCEPTION_PAYLOAD:
	case KVM_CAP_X86_TRIPLE_FAULT_EVENT:
	case KVM_CAP_SET_GUEST_DEBUG:
	case KVM_CAP_LAST_CPU:
	case KVM_CAP_X86_USER_SPACE_MSR:
	case KVM_CAP_X86_MSR_FILTER:
	case KVM_CAP_ENFORCE_PV_FEATURE_CPUID:
#ifdef CONFIG_X86_SGX_KVM
	case KVM_CAP_SGX_ATTRIBUTE:
#endif
	case KVM_CAP_VM_COPY_ENC_CONTEXT_FROM:
	case KVM_CAP_VM_MOVE_ENC_CONTEXT_FROM:
	case KVM_CAP_SREGS2:
	case KVM_CAP_EXIT_ON_EMULATION_FAILURE:
	case KVM_CAP_VCPU_ATTRIBUTES:
	case KVM_CAP_SYS_ATTRIBUTES:
	case KVM_CAP_VAPIC:
	case KVM_CAP_ENABLE_CAP:
	case KVM_CAP_VM_DISABLE_NX_HUGE_PAGES:
	case KVM_CAP_IRQFD_RESAMPLE:
	case KVM_CAP_MEMORY_FAULT_INFO:
	case KVM_CAP_X86_GUEST_MODE:
		r = 1;
		break;
	case KVM_CAP_PRE_FAULT_MEMORY:
		r = tdp_enabled;
		break;
	case KVM_CAP_X86_APIC_BUS_CYCLES_NS:
		r = APIC_BUS_CYCLE_NS_DEFAULT;
		break;
	case KVM_CAP_EXIT_HYPERCALL:
		r = KVM_EXIT_HYPERCALL_VALID_MASK;
		break;
	case KVM_CAP_SET_GUEST_DEBUG2:
		return KVM_GUESTDBG_VALID_MASK;
#ifdef CONFIG_KVM_XEN
	case KVM_CAP_XEN_HVM:
		r = KVM_XEN_HVM_CONFIG_HYPERCALL_MSR |
		    KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL |
		    KVM_XEN_HVM_CONFIG_SHARED_INFO |
		    KVM_XEN_HVM_CONFIG_EVTCHN_2LEVEL |
		    KVM_XEN_HVM_CONFIG_EVTCHN_SEND |
		    KVM_XEN_HVM_CONFIG_PVCLOCK_TSC_UNSTABLE |
		    KVM_XEN_HVM_CONFIG_SHARED_INFO_HVA;
		if (sched_info_on())
			r |= KVM_XEN_HVM_CONFIG_RUNSTATE |
			     KVM_XEN_HVM_CONFIG_RUNSTATE_UPDATE_FLAG;
		break;
#endif
	case KVM_CAP_SYNC_REGS:
		r = KVM_SYNC_X86_VALID_FIELDS;
		break;
	case KVM_CAP_ADJUST_CLOCK:
		r = KVM_CLOCK_VALID_FLAGS;
		break;
	case KVM_CAP_X86_DISABLE_EXITS:
		r = KVM_X86_DISABLE_EXITS_PAUSE;

		if (!mitigate_smt_rsb) {
			r |= KVM_X86_DISABLE_EXITS_HLT |
			     KVM_X86_DISABLE_EXITS_CSTATE;

			if (kvm_can_mwait_in_guest())
				r |= KVM_X86_DISABLE_EXITS_MWAIT;
		}
		break;
	case KVM_CAP_X86_SMM:
		if (!IS_ENABLED(CONFIG_KVM_SMM))
			break;

		/* SMBASE is usually relocated above 1M on modern chipsets,
		 * and SMM handlers might indeed rely on 4G segment limits,
		 * so do not report SMM to be available if real mode is
		 * emulated via vm86 mode.  Still, do not go to great lengths
		 * to avoid userspace's usage of the feature, because it is a
		 * fringe case that is not enabled except via specific settings
		 * of the module parameters.
		 */
		r = kvm_x86_call(has_emulated_msr)(kvm, MSR_IA32_SMBASE);
		break;
	case KVM_CAP_NR_VCPUS:
		r = min_t(unsigned int, num_online_cpus(), KVM_MAX_VCPUS);
		break;
	case KVM_CAP_MAX_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
	case KVM_CAP_MAX_VCPU_ID:
		r = KVM_MAX_VCPU_IDS;
		break;
	case KVM_CAP_PV_MMU:	/* obsolete */
		r = 0;
		break;
	case KVM_CAP_MCE:
		r = KVM_MAX_MCE_BANKS;
		break;
	case KVM_CAP_XCRS:
		r = boot_cpu_has(X86_FEATURE_XSAVE);
		break;
	case KVM_CAP_TSC_CONTROL:
	case KVM_CAP_VM_TSC_CONTROL:
		r = kvm_caps.has_tsc_control;
		break;
	case KVM_CAP_X2APIC_API:
		r = KVM_X2APIC_API_VALID_FLAGS;
		break;
	case KVM_CAP_NESTED_STATE:
		r = kvm_x86_ops.nested_ops->get_state ?
			kvm_x86_ops.nested_ops->get_state(NULL, NULL, 0) : 0;
		break;
#ifdef CONFIG_KVM_HYPERV
	case KVM_CAP_HYPERV_DIRECT_TLBFLUSH:
		r = kvm_x86_ops.enable_l2_tlb_flush != NULL;
		break;
	case KVM_CAP_HYPERV_ENLIGHTENED_VMCS:
		r = kvm_x86_ops.nested_ops->enable_evmcs != NULL;
		break;
#endif
	case KVM_CAP_SMALLER_MAXPHYADDR:
		r = (int) allow_smaller_maxphyaddr;
		break;
	case KVM_CAP_STEAL_TIME:
		r = sched_info_on();
		break;
	case KVM_CAP_X86_BUS_LOCK_EXIT:
		if (kvm_caps.has_bus_lock_exit)
			r = KVM_BUS_LOCK_DETECTION_OFF |
			    KVM_BUS_LOCK_DETECTION_EXIT;
		else
			r = 0;
		break;
	case KVM_CAP_XSAVE2: {
		r = xstate_required_size(kvm_get_filtered_xcr0(), false);
		if (r < sizeof(struct kvm_xsave))
			r = sizeof(struct kvm_xsave);
		break;
	}
	case KVM_CAP_PMU_CAPABILITY:
		r = enable_pmu ? KVM_CAP_PMU_VALID_MASK : 0;
		break;
	case KVM_CAP_DISABLE_QUIRKS2:
		r = KVM_X86_VALID_QUIRKS;
		break;
	case KVM_CAP_X86_NOTIFY_VMEXIT:
		r = kvm_caps.has_notify_vmexit;
		break;
	case KVM_CAP_VM_TYPES:
		r = kvm_caps.supported_vm_types;
		break;
	case KVM_CAP_READONLY_MEM:
		r = kvm ? kvm_arch_has_readonly_mem(kvm) : 1;
		break;
	default:
		break;
	}
	return r;
}

static int __kvm_x86_dev_get_attr(struct kvm_device_attr *attr, u64 *val)
{
	if (attr->group) {
		if (kvm_x86_ops.dev_get_attr)
			return kvm_x86_call(dev_get_attr)(attr->group, attr->attr, val);
		return -ENXIO;
	}

	switch (attr->attr) {
	case KVM_X86_XCOMP_GUEST_SUPP:
		*val = kvm_caps.supported_xcr0;
		return 0;
	default:
		return -ENXIO;
	}
}

static int kvm_x86_dev_get_attr(struct kvm_device_attr *attr)
{
	u64 __user *uaddr = u64_to_user_ptr(attr->addr);
	int r;
	u64 val;

	r = __kvm_x86_dev_get_attr(attr, &val);
	if (r < 0)
		return r;

	if (put_user(val, uaddr))
		return -EFAULT;

	return 0;
}

static int kvm_x86_dev_has_attr(struct kvm_device_attr *attr)
{
	u64 val;

	return __kvm_x86_dev_get_attr(attr, &val);
}

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long r;

	switch (ioctl) {
	case KVM_GET_MSR_INDEX_LIST: {
		struct kvm_msr_list __user *user_msr_list = argp;
		struct kvm_msr_list msr_list;
		unsigned n;

		r = -EFAULT;
		if (copy_from_user(&msr_list, user_msr_list, sizeof(msr_list)))
			goto out;
		n = msr_list.nmsrs;
		msr_list.nmsrs = num_msrs_to_save + num_emulated_msrs;
		if (copy_to_user(user_msr_list, &msr_list, sizeof(msr_list)))
			goto out;
		r = -E2BIG;
		if (n < msr_list.nmsrs)
			goto out;
		r = -EFAULT;
		if (copy_to_user(user_msr_list->indices, &msrs_to_save,
				 num_msrs_to_save * sizeof(u32)))
			goto out;
		if (copy_to_user(user_msr_list->indices + num_msrs_to_save,
				 &emulated_msrs,
				 num_emulated_msrs * sizeof(u32)))
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_SUPPORTED_CPUID:
	case KVM_GET_EMULATED_CPUID: {
		struct kvm_cpuid2 __user *cpuid_arg = argp;
		struct kvm_cpuid2 cpuid;

		r = -EFAULT;
		if (copy_from_user(&cpuid, cpuid_arg, sizeof(cpuid)))
			goto out;

		r = kvm_dev_ioctl_get_cpuid(&cpuid, cpuid_arg->entries,
					    ioctl);
		if (r)
			goto out;

		r = -EFAULT;
		if (copy_to_user(cpuid_arg, &cpuid, sizeof(cpuid)))
			goto out;
		r = 0;
		break;
	}
	case KVM_X86_GET_MCE_CAP_SUPPORTED:
		r = -EFAULT;
		if (copy_to_user(argp, &kvm_caps.supported_mce_cap,
				 sizeof(kvm_caps.supported_mce_cap)))
			goto out;
		r = 0;
		break;
	case KVM_GET_MSR_FEATURE_INDEX_LIST: {
		struct kvm_msr_list __user *user_msr_list = argp;
		struct kvm_msr_list msr_list;
		unsigned int n;

		r = -EFAULT;
		if (copy_from_user(&msr_list, user_msr_list, sizeof(msr_list)))
			goto out;
		n = msr_list.nmsrs;
		msr_list.nmsrs = num_msr_based_features;
		if (copy_to_user(user_msr_list, &msr_list, sizeof(msr_list)))
			goto out;
		r = -E2BIG;
		if (n < msr_list.nmsrs)
			goto out;
		r = -EFAULT;
		if (copy_to_user(user_msr_list->indices, &msr_based_features,
				 num_msr_based_features * sizeof(u32)))
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_MSRS:
		r = msr_io(NULL, argp, do_get_feature_msr, 1);
		break;
#ifdef CONFIG_KVM_HYPERV
	case KVM_GET_SUPPORTED_HV_CPUID:
		r = kvm_ioctl_get_supported_hv_cpuid(NULL, argp);
		break;
#endif
	case KVM_GET_DEVICE_ATTR: {
		struct kvm_device_attr attr;
		r = -EFAULT;
		if (copy_from_user(&attr, (void __user *)arg, sizeof(attr)))
			break;
		r = kvm_x86_dev_get_attr(&attr);
		break;
	}
	case KVM_HAS_DEVICE_ATTR: {
		struct kvm_device_attr attr;
		r = -EFAULT;
		if (copy_from_user(&attr, (void __user *)arg, sizeof(attr)))
			break;
		r = kvm_x86_dev_has_attr(&attr);
		break;
	}
	default:
		r = -EINVAL;
		break;
	}
out:
	return r;
}

static void wbinvd_ipi(void *garbage)
{
	wbinvd();
}

static bool need_emulate_wbinvd(struct kvm_vcpu *vcpu)
{
	return kvm_arch_has_noncoherent_dma(vcpu->kvm);
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);

	vcpu->arch.l1tf_flush_l1d = true;

	if (vcpu->scheduled_out && pmu->version && pmu->event_count) {
		pmu->need_cleanup = true;
		kvm_make_request(KVM_REQ_PMU, vcpu);
	}

	/* Address WBINVD may be executed by guest */
	if (need_emulate_wbinvd(vcpu)) {
		if (kvm_x86_call(has_wbinvd_exit)())
			cpumask_set_cpu(cpu, vcpu->arch.wbinvd_dirty_mask);
		else if (vcpu->cpu != -1 && vcpu->cpu != cpu)
			smp_call_function_single(vcpu->cpu,
					wbinvd_ipi, NULL, 1);
	}

	kvm_x86_call(vcpu_load)(vcpu, cpu);

	/* Save host pkru register if supported */
	vcpu->arch.host_pkru = read_pkru();

	/* Apply any externally detected TSC adjustments (due to suspend) */
	if (unlikely(vcpu->arch.tsc_offset_adjustment)) {
		adjust_tsc_offset_host(vcpu, vcpu->arch.tsc_offset_adjustment);
		vcpu->arch.tsc_offset_adjustment = 0;
		kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
	}

	if (unlikely(vcpu->cpu != cpu) || kvm_check_tsc_unstable()) {
		s64 tsc_delta = !vcpu->arch.last_host_tsc ? 0 :
				rdtsc() - vcpu->arch.last_host_tsc;
		if (tsc_delta < 0)
			mark_tsc_unstable("KVM discovered backwards TSC");

		if (kvm_check_tsc_unstable()) {
			u64 offset = kvm_compute_l1_tsc_offset(vcpu,
						vcpu->arch.last_guest_tsc);
			kvm_vcpu_write_tsc_offset(vcpu, offset);
			vcpu->arch.tsc_catchup = 1;
		}

		if (kvm_lapic_hv_timer_in_use(vcpu))
			kvm_lapic_restart_hv_timer(vcpu);

		/*
		 * On a host with synchronized TSC, there is no need to update
		 * kvmclock on vcpu->cpu migration
		 */
		if (!vcpu->kvm->arch.use_master_clock || vcpu->cpu == -1)
			kvm_make_request(KVM_REQ_GLOBAL_CLOCK_UPDATE, vcpu);
		if (vcpu->cpu != cpu)
			kvm_make_request(KVM_REQ_MIGRATE_TIMER, vcpu);
		vcpu->cpu = cpu;
	}

	kvm_make_request(KVM_REQ_STEAL_UPDATE, vcpu);
}

static void kvm_steal_time_set_preempted(struct kvm_vcpu *vcpu)
{
	struct gfn_to_hva_cache *ghc = &vcpu->arch.st.cache;
	struct kvm_steal_time __user *st;
	struct kvm_memslots *slots;
	static const u8 preempted = KVM_VCPU_PREEMPTED;
	gpa_t gpa = vcpu->arch.st.msr_val & KVM_STEAL_VALID_BITS;

	/*
	 * The vCPU can be marked preempted if and only if the VM-Exit was on
	 * an instruction boundary and will not trigger guest emulation of any
	 * kind (see vcpu_run).  Vendor specific code controls (conservatively)
	 * when this is true, for example allowing the vCPU to be marked
	 * preempted if and only if the VM-Exit was due to a host interrupt.
	 */
	if (!vcpu->arch.at_instruction_boundary) {
		vcpu->stat.preemption_other++;
		return;
	}

	vcpu->stat.preemption_reported++;
	if (!(vcpu->arch.st.msr_val & KVM_MSR_ENABLED))
		return;

	if (vcpu->arch.st.preempted)
		return;

	/* This happens on process exit */
	if (unlikely(current->mm != vcpu->kvm->mm))
		return;

	slots = kvm_memslots(vcpu->kvm);

	if (unlikely(slots->generation != ghc->generation ||
		     gpa != ghc->gpa ||
		     kvm_is_error_hva(ghc->hva) || !ghc->memslot))
		return;

	st = (struct kvm_steal_time __user *)ghc->hva;
	BUILD_BUG_ON(sizeof(st->preempted) != sizeof(preempted));

	if (!copy_to_user_nofault(&st->preempted, &preempted, sizeof(preempted)))
		vcpu->arch.st.preempted = KVM_VCPU_PREEMPTED;

	mark_page_dirty_in_slot(vcpu->kvm, ghc->memslot, gpa_to_gfn(ghc->gpa));
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	int idx;

	if (vcpu->preempted) {
		/*
		 * Assume protected guests are in-kernel.  Inefficient yielding
		 * due to false positives is preferable to never yielding due
		 * to false negatives.
		 */
		vcpu->arch.preempted_in_kernel = vcpu->arch.guest_state_protected ||
						 !kvm_x86_call(get_cpl_no_cache)(vcpu);

		/*
		 * Take the srcu lock as memslots will be accessed to check the gfn
		 * cache generation against the memslots generation.
		 */
		idx = srcu_read_lock(&vcpu->kvm->srcu);
		if (kvm_xen_msr_enabled(vcpu->kvm))
			kvm_xen_runstate_set_preempted(vcpu);
		else
			kvm_steal_time_set_preempted(vcpu);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);
	}

	kvm_x86_call(vcpu_put)(vcpu);
	vcpu->arch.last_host_tsc = rdtsc();
}

static int kvm_vcpu_ioctl_get_lapic(struct kvm_vcpu *vcpu,
				    struct kvm_lapic_state *s)
{
	kvm_x86_call(sync_pir_to_irr)(vcpu);

	return kvm_apic_get_state(vcpu, s);
}

static int kvm_vcpu_ioctl_set_lapic(struct kvm_vcpu *vcpu,
				    struct kvm_lapic_state *s)
{
	int r;

	r = kvm_apic_set_state(vcpu, s);
	if (r)
		return r;
	update_cr8_intercept(vcpu);

	return 0;
}

static int kvm_cpu_accept_dm_intr(struct kvm_vcpu *vcpu)
{
	/*
	 * We can accept userspace's request for interrupt injection
	 * as long as we have a place to store the interrupt number.
	 * The actual injection will happen when the CPU is able to
	 * deliver the interrupt.
	 */
	if (kvm_cpu_has_extint(vcpu))
		return false;

	/* Acknowledging ExtINT does not happen if LINT0 is masked.  */
	return (!lapic_in_kernel(vcpu) ||
		kvm_apic_accept_pic_intr(vcpu));
}

static int kvm_vcpu_ready_for_interrupt_injection(struct kvm_vcpu *vcpu)
{
	/*
	 * Do not cause an interrupt window exit if an exception
	 * is pending or an event needs reinjection; userspace
	 * might want to inject the interrupt manually using KVM_SET_REGS
	 * or KVM_SET_SREGS.  For that to work, we must be at an
	 * instruction boundary and with no events half-injected.
	 */
	return (kvm_arch_interrupt_allowed(vcpu) &&
		kvm_cpu_accept_dm_intr(vcpu) &&
		!kvm_event_needs_reinjection(vcpu) &&
		!kvm_is_exception_pending(vcpu));
}

static int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu,
				    struct kvm_interrupt *irq)
{
	if (irq->irq >= KVM_NR_INTERRUPTS)
		return -EINVAL;

	if (!irqchip_in_kernel(vcpu->kvm)) {
		kvm_queue_interrupt(vcpu, irq->irq, false);
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		return 0;
	}

	/*
	 * With in-kernel LAPIC, we only use this to inject EXTINT, so
	 * fail for in-kernel 8259.
	 */
	if (pic_in_kernel(vcpu->kvm))
		return -ENXIO;

	if (vcpu->arch.pending_external_vector != -1)
		return -EEXIST;

	vcpu->arch.pending_external_vector = irq->irq;
	kvm_make_request(KVM_REQ_EVENT, vcpu);
	return 0;
}

static int kvm_vcpu_ioctl_nmi(struct kvm_vcpu *vcpu)
{
	kvm_inject_nmi(vcpu);

	return 0;
}

static int vcpu_ioctl_tpr_access_reporting(struct kvm_vcpu *vcpu,
					   struct kvm_tpr_access_ctl *tac)
{
	if (tac->flags)
		return -EINVAL;
	vcpu->arch.tpr_access_reporting = !!tac->enabled;
	return 0;
}

static int kvm_vcpu_ioctl_x86_setup_mce(struct kvm_vcpu *vcpu,
					u64 mcg_cap)
{
	int r;
	unsigned bank_num = mcg_cap & 0xff, bank;

	r = -EINVAL;
	if (!bank_num || bank_num > KVM_MAX_MCE_BANKS)
		goto out;
	if (mcg_cap & ~(kvm_caps.supported_mce_cap | 0xff | 0xff0000))
		goto out;
	r = 0;
	vcpu->arch.mcg_cap = mcg_cap;
	/* Init IA32_MCG_CTL to all 1s */
	if (mcg_cap & MCG_CTL_P)
		vcpu->arch.mcg_ctl = ~(u64)0;
	/* Init IA32_MCi_CTL to all 1s, IA32_MCi_CTL2 to all 0s */
	for (bank = 0; bank < bank_num; bank++) {
		vcpu->arch.mce_banks[bank*4] = ~(u64)0;
		if (mcg_cap & MCG_CMCI_P)
			vcpu->arch.mci_ctl2_banks[bank] = 0;
	}

	kvm_apic_after_set_mcg_cap(vcpu);

	kvm_x86_call(setup_mce)(vcpu);
out:
	return r;
}

/*
 * Validate this is an UCNA (uncorrectable no action) error by checking the
 * MCG_STATUS and MCi_STATUS registers:
 * - none of the bits for Machine Check Exceptions are set
 * - both the VAL (valid) and UC (uncorrectable) bits are set
 * MCI_STATUS_PCC - Processor Context Corrupted
 * MCI_STATUS_S - Signaled as a Machine Check Exception
 * MCI_STATUS_AR - Software recoverable Action Required
 */
static bool is_ucna(struct kvm_x86_mce *mce)
{
	return	!mce->mcg_status &&
		!(mce->status & (MCI_STATUS_PCC | MCI_STATUS_S | MCI_STATUS_AR)) &&
		(mce->status & MCI_STATUS_VAL) &&
		(mce->status & MCI_STATUS_UC);
}

static int kvm_vcpu_x86_set_ucna(struct kvm_vcpu *vcpu, struct kvm_x86_mce *mce, u64* banks)
{
	u64 mcg_cap = vcpu->arch.mcg_cap;

	banks[1] = mce->status;
	banks[2] = mce->addr;
	banks[3] = mce->misc;
	vcpu->arch.mcg_status = mce->mcg_status;

	if (!(mcg_cap & MCG_CMCI_P) ||
	    !(vcpu->arch.mci_ctl2_banks[mce->bank] & MCI_CTL2_CMCI_EN))
		return 0;

	if (lapic_in_kernel(vcpu))
		kvm_apic_local_deliver(vcpu->arch.apic, APIC_LVTCMCI);

	return 0;
}

static int kvm_vcpu_ioctl_x86_set_mce(struct kvm_vcpu *vcpu,
				      struct kvm_x86_mce *mce)
{
	u64 mcg_cap = vcpu->arch.mcg_cap;
	unsigned bank_num = mcg_cap & 0xff;
	u64 *banks = vcpu->arch.mce_banks;

	if (mce->bank >= bank_num || !(mce->status & MCI_STATUS_VAL))
		return -EINVAL;

	banks += array_index_nospec(4 * mce->bank, 4 * bank_num);

	if (is_ucna(mce))
		return kvm_vcpu_x86_set_ucna(vcpu, mce, banks);

	/*
	 * if IA32_MCG_CTL is not all 1s, the uncorrected error
	 * reporting is disabled
	 */
	if ((mce->status & MCI_STATUS_UC) && (mcg_cap & MCG_CTL_P) &&
	    vcpu->arch.mcg_ctl != ~(u64)0)
		return 0;
	/*
	 * if IA32_MCi_CTL is not all 1s, the uncorrected error
	 * reporting is disabled for the bank
	 */
	if ((mce->status & MCI_STATUS_UC) && banks[0] != ~(u64)0)
		return 0;
	if (mce->status & MCI_STATUS_UC) {
		if ((vcpu->arch.mcg_status & MCG_STATUS_MCIP) ||
		    !kvm_is_cr4_bit_set(vcpu, X86_CR4_MCE)) {
			kvm_make_request(KVM_REQ_TRIPLE_FAULT, vcpu);
			return 0;
		}
		if (banks[1] & MCI_STATUS_VAL)
			mce->status |= MCI_STATUS_OVER;
		banks[2] = mce->addr;
		banks[3] = mce->misc;
		vcpu->arch.mcg_status = mce->mcg_status;
		banks[1] = mce->status;
		kvm_queue_exception(vcpu, MC_VECTOR);
	} else if (!(banks[1] & MCI_STATUS_VAL)
		   || !(banks[1] & MCI_STATUS_UC)) {
		if (banks[1] & MCI_STATUS_VAL)
			mce->status |= MCI_STATUS_OVER;
		banks[2] = mce->addr;
		banks[3] = mce->misc;
		banks[1] = mce->status;
	} else
		banks[1] |= MCI_STATUS_OVER;
	return 0;
}

static void kvm_vcpu_ioctl_x86_get_vcpu_events(struct kvm_vcpu *vcpu,
					       struct kvm_vcpu_events *events)
{
	struct kvm_queued_exception *ex;

	process_nmi(vcpu);

#ifdef CONFIG_KVM_SMM
	if (kvm_check_request(KVM_REQ_SMI, vcpu))
		process_smi(vcpu);
#endif

	/*
	 * KVM's ABI only allows for one exception to be migrated.  Luckily,
	 * the only time there can be two queued exceptions is if there's a
	 * non-exiting _injected_ exception, and a pending exiting exception.
	 * In that case, ignore the VM-Exiting exception as it's an extension
	 * of the injected exception.
	 */
	if (vcpu->arch.exception_vmexit.pending &&
	    !vcpu->arch.exception.pending &&
	    !vcpu->arch.exception.injected)
		ex = &vcpu->arch.exception_vmexit;
	else
		ex = &vcpu->arch.exception;

	/*
	 * In guest mode, payload delivery should be deferred if the exception
	 * will be intercepted by L1, e.g. KVM should not modifying CR2 if L1
	 * intercepts #PF, ditto for DR6 and #DBs.  If the per-VM capability,
	 * KVM_CAP_EXCEPTION_PAYLOAD, is not set, userspace may or may not
	 * propagate the payload and so it cannot be safely deferred.  Deliver
	 * the payload if the capability hasn't been requested.
	 */
	if (!vcpu->kvm->arch.exception_payload_enabled &&
	    ex->pending && ex->has_payload)
		kvm_deliver_exception_payload(vcpu, ex);

	memset(events, 0, sizeof(*events));

	/*
	 * The API doesn't provide the instruction length for software
	 * exceptions, so don't report them. As long as the guest RIP
	 * isn't advanced, we should expect to encounter the exception
	 * again.
	 */
	if (!kvm_exception_is_soft(ex->vector)) {
		events->exception.injected = ex->injected;
		events->exception.pending = ex->pending;
		/*
		 * For ABI compatibility, deliberately conflate
		 * pending and injected exceptions when
		 * KVM_CAP_EXCEPTION_PAYLOAD isn't enabled.
		 */
		if (!vcpu->kvm->arch.exception_payload_enabled)
			events->exception.injected |= ex->pending;
	}
	events->exception.nr = ex->vector;
	events->exception.has_error_code = ex->has_error_code;
	events->exception.error_code = ex->error_code;
	events->exception_has_payload = ex->has_payload;
	events->exception_payload = ex->payload;

	events->interrupt.injected =
		vcpu->arch.interrupt.injected && !vcpu->arch.interrupt.soft;
	events->interrupt.nr = vcpu->arch.interrupt.nr;
	events->interrupt.shadow = kvm_x86_call(get_interrupt_shadow)(vcpu);

	events->nmi.injected = vcpu->arch.nmi_injected;
	events->nmi.pending = kvm_get_nr_pending_nmis(vcpu);
	events->nmi.masked = kvm_x86_call(get_nmi_mask)(vcpu);

	/* events->sipi_vector is never valid when reporting to user space */

#ifdef CONFIG_KVM_SMM
	events->smi.smm = is_smm(vcpu);
	events->smi.pending = vcpu->arch.smi_pending;
	events->smi.smm_inside_nmi =
		!!(vcpu->arch.hflags & HF_SMM_INSIDE_NMI_MASK);
#endif
	events->smi.latched_init = kvm_lapic_latched_init(vcpu);

	events->flags = (KVM_VCPUEVENT_VALID_NMI_PENDING
			 | KVM_VCPUEVENT_VALID_SHADOW
			 | KVM_VCPUEVENT_VALID_SMM);
	if (vcpu->kvm->arch.exception_payload_enabled)
		events->flags |= KVM_VCPUEVENT_VALID_PAYLOAD;
	if (vcpu->kvm->arch.triple_fault_event) {
		events->triple_fault.pending = kvm_test_request(KVM_REQ_TRIPLE_FAULT, vcpu);
		events->flags |= KVM_VCPUEVENT_VALID_TRIPLE_FAULT;
	}
}

static int kvm_vcpu_ioctl_x86_set_vcpu_events(struct kvm_vcpu *vcpu,
					      struct kvm_vcpu_events *events)
{
	if (events->flags & ~(KVM_VCPUEVENT_VALID_NMI_PENDING
			      | KVM_VCPUEVENT_VALID_SIPI_VECTOR
			      | KVM_VCPUEVENT_VALID_SHADOW
			      | KVM_VCPUEVENT_VALID_SMM
			      | KVM_VCPUEVENT_VALID_PAYLOAD
			      | KVM_VCPUEVENT_VALID_TRIPLE_FAULT))
		return -EINVAL;

	if (events->flags & KVM_VCPUEVENT_VALID_PAYLOAD) {
		if (!vcpu->kvm->arch.exception_payload_enabled)
			return -EINVAL;
		if (events->exception.pending)
			events->exception.injected = 0;
		else
			events->exception_has_payload = 0;
	} else {
		events->exception.pending = 0;
		events->exception_has_payload = 0;
	}

	if ((events->exception.injected || events->exception.pending) &&
	    (events->exception.nr > 31 || events->exception.nr == NMI_VECTOR))
		return -EINVAL;

	/* INITs are latched while in SMM */
	if (events->flags & KVM_VCPUEVENT_VALID_SMM &&
	    (events->smi.smm || events->smi.pending) &&
	    vcpu->arch.mp_state == KVM_MP_STATE_INIT_RECEIVED)
		return -EINVAL;

	process_nmi(vcpu);

	/*
	 * Flag that userspace is stuffing an exception, the next KVM_RUN will
	 * morph the exception to a VM-Exit if appropriate.  Do this only for
	 * pending exceptions, already-injected exceptions are not subject to
	 * intercpetion.  Note, userspace that conflates pending and injected
	 * is hosed, and will incorrectly convert an injected exception into a
	 * pending exception, which in turn may cause a spurious VM-Exit.
	 */
	vcpu->arch.exception_from_userspace = events->exception.pending;

	vcpu->arch.exception_vmexit.pending = false;

	vcpu->arch.exception.injected = events->exception.injected;
	vcpu->arch.exception.pending = events->exception.pending;
	vcpu->arch.exception.vector = events->exception.nr;
	vcpu->arch.exception.has_error_code = events->exception.has_error_code;
	vcpu->arch.exception.error_code = events->exception.error_code;
	vcpu->arch.exception.has_payload = events->exception_has_payload;
	vcpu->arch.exception.payload = events->exception_payload;

	vcpu->arch.interrupt.injected = events->interrupt.injected;
	vcpu->arch.interrupt.nr = events->interrupt.nr;
	vcpu->arch.interrupt.soft = events->interrupt.soft;
	if (events->flags & KVM_VCPUEVENT_VALID_SHADOW)
		kvm_x86_call(set_interrupt_shadow)(vcpu,
						   events->interrupt.shadow);

	vcpu->arch.nmi_injected = events->nmi.injected;
	if (events->flags & KVM_VCPUEVENT_VALID_NMI_PENDING) {
		vcpu->arch.nmi_pending = 0;
		atomic_set(&vcpu->arch.nmi_queued, events->nmi.pending);
		if (events->nmi.pending)
			kvm_make_request(KVM_REQ_NMI, vcpu);
	}
	kvm_x86_call(set_nmi_mask)(vcpu, events->nmi.masked);

	if (events->flags & KVM_VCPUEVENT_VALID_SIPI_VECTOR &&
	    lapic_in_kernel(vcpu))
		vcpu->arch.apic->sipi_vector = events->sipi_vector;

	if (events->flags & KVM_VCPUEVENT_VALID_SMM) {
#ifdef CONFIG_KVM_SMM
		if (!!(vcpu->arch.hflags & HF_SMM_MASK) != events->smi.smm) {
			kvm_leave_nested(vcpu);
			kvm_smm_changed(vcpu, events->smi.smm);
		}

		vcpu->arch.smi_pending = events->smi.pending;

		if (events->smi.smm) {
			if (events->smi.smm_inside_nmi)
				vcpu->arch.hflags |= HF_SMM_INSIDE_NMI_MASK;
			else
				vcpu->arch.hflags &= ~HF_SMM_INSIDE_NMI_MASK;
		}

#else
		if (events->smi.smm || events->smi.pending ||
		    events->smi.smm_inside_nmi)
			return -EINVAL;
#endif

		if (lapic_in_kernel(vcpu)) {
			if (events->smi.latched_init)
				set_bit(KVM_APIC_INIT, &vcpu->arch.apic->pending_events);
			else
				clear_bit(KVM_APIC_INIT, &vcpu->arch.apic->pending_events);
		}
	}

	if (events->flags & KVM_VCPUEVENT_VALID_TRIPLE_FAULT) {
		if (!vcpu->kvm->arch.triple_fault_event)
			return -EINVAL;
		if (events->triple_fault.pending)
			kvm_make_request(KVM_REQ_TRIPLE_FAULT, vcpu);
		else
			kvm_clear_request(KVM_REQ_TRIPLE_FAULT, vcpu);
	}

	kvm_make_request(KVM_REQ_EVENT, vcpu);

	return 0;
}

static int kvm_vcpu_ioctl_x86_get_debugregs(struct kvm_vcpu *vcpu,
					    struct kvm_debugregs *dbgregs)
{
	unsigned int i;

	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	memset(dbgregs, 0, sizeof(*dbgregs));

	BUILD_BUG_ON(ARRAY_SIZE(vcpu->arch.db) != ARRAY_SIZE(dbgregs->db));
	for (i = 0; i < ARRAY_SIZE(vcpu->arch.db); i++)
		dbgregs->db[i] = vcpu->arch.db[i];

	dbgregs->dr6 = vcpu->arch.dr6;
	dbgregs->dr7 = vcpu->arch.dr7;
	return 0;
}

static int kvm_vcpu_ioctl_x86_set_debugregs(struct kvm_vcpu *vcpu,
					    struct kvm_debugregs *dbgregs)
{
	unsigned int i;

	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	if (dbgregs->flags)
		return -EINVAL;

	if (!kvm_dr6_valid(dbgregs->dr6))
		return -EINVAL;
	if (!kvm_dr7_valid(dbgregs->dr7))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(vcpu->arch.db); i++)
		vcpu->arch.db[i] = dbgregs->db[i];

	kvm_update_dr0123(vcpu);
	vcpu->arch.dr6 = dbgregs->dr6;
	vcpu->arch.dr7 = dbgregs->dr7;
	kvm_update_dr7(vcpu);

	return 0;
}


static int kvm_vcpu_ioctl_x86_get_xsave2(struct kvm_vcpu *vcpu,
					 u8 *state, unsigned int size)
{
	/*
	 * Only copy state for features that are enabled for the guest.  The
	 * state itself isn't problematic, but setting bits in the header for
	 * features that are supported in *this* host but not exposed to the
	 * guest can result in KVM_SET_XSAVE failing when live migrating to a
	 * compatible host without the features that are NOT exposed to the
	 * guest.
	 *
	 * FP+SSE can always be saved/restored via KVM_{G,S}ET_XSAVE, even if
	 * XSAVE/XCRO are not exposed to the guest, and even if XSAVE isn't
	 * supported by the host.
	 */
	u64 supported_xcr0 = vcpu->arch.guest_supported_xcr0 |
			     XFEATURE_MASK_FPSSE;

	if (fpstate_is_confidential(&vcpu->arch.guest_fpu))
		return vcpu->kvm->arch.has_protected_state ? -EINVAL : 0;

	fpu_copy_guest_fpstate_to_uabi(&vcpu->arch.guest_fpu, state, size,
				       supported_xcr0, vcpu->arch.pkru);
	return 0;
}

static int kvm_vcpu_ioctl_x86_get_xsave(struct kvm_vcpu *vcpu,
					struct kvm_xsave *guest_xsave)
{
	return kvm_vcpu_ioctl_x86_get_xsave2(vcpu, (void *)guest_xsave->region,
					     sizeof(guest_xsave->region));
}

static int kvm_vcpu_ioctl_x86_set_xsave(struct kvm_vcpu *vcpu,
					struct kvm_xsave *guest_xsave)
{
	if (fpstate_is_confidential(&vcpu->arch.guest_fpu))
		return vcpu->kvm->arch.has_protected_state ? -EINVAL : 0;

	return fpu_copy_uabi_to_guest_fpstate(&vcpu->arch.guest_fpu,
					      guest_xsave->region,
					      kvm_caps.supported_xcr0,
					      &vcpu->arch.pkru);
}

static int kvm_vcpu_ioctl_x86_get_xcrs(struct kvm_vcpu *vcpu,
				       struct kvm_xcrs *guest_xcrs)
{
	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	if (!boot_cpu_has(X86_FEATURE_XSAVE)) {
		guest_xcrs->nr_xcrs = 0;
		return 0;
	}

	guest_xcrs->nr_xcrs = 1;
	guest_xcrs->flags = 0;
	guest_xcrs->xcrs[0].xcr = XCR_XFEATURE_ENABLED_MASK;
	guest_xcrs->xcrs[0].value = vcpu->arch.xcr0;
	return 0;
}

static int kvm_vcpu_ioctl_x86_set_xcrs(struct kvm_vcpu *vcpu,
				       struct kvm_xcrs *guest_xcrs)
{
	int i, r = 0;

	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	if (!boot_cpu_has(X86_FEATURE_XSAVE))
		return -EINVAL;

	if (guest_xcrs->nr_xcrs > KVM_MAX_XCRS || guest_xcrs->flags)
		return -EINVAL;

	for (i = 0; i < guest_xcrs->nr_xcrs; i++)
		/* Only support XCR0 currently */
		if (guest_xcrs->xcrs[i].xcr == XCR_XFEATURE_ENABLED_MASK) {
			r = __kvm_set_xcr(vcpu, XCR_XFEATURE_ENABLED_MASK,
				guest_xcrs->xcrs[i].value);
			break;
		}
	if (r)
		r = -EINVAL;
	return r;
}

/*
 * kvm_set_guest_paused() indicates to the guest kernel that it has been
 * stopped by the hypervisor.  This function will be called from the host only.
 * EINVAL is returned when the host attempts to set the flag for a guest that
 * does not support pv clocks.
 */
static int kvm_set_guest_paused(struct kvm_vcpu *vcpu)
{
	if (!vcpu->arch.pv_time.active)
		return -EINVAL;
	vcpu->arch.pvclock_set_guest_stopped_request = true;
	kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
	return 0;
}

static int kvm_arch_tsc_has_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	int r;

	switch (attr->attr) {
	case KVM_VCPU_TSC_OFFSET:
		r = 0;
		break;
	default:
		r = -ENXIO;
	}

	return r;
}

static int kvm_arch_tsc_get_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	u64 __user *uaddr = u64_to_user_ptr(attr->addr);
	int r;

	switch (attr->attr) {
	case KVM_VCPU_TSC_OFFSET:
		r = -EFAULT;
		if (put_user(vcpu->arch.l1_tsc_offset, uaddr))
			break;
		r = 0;
		break;
	default:
		r = -ENXIO;
	}

	return r;
}

static int kvm_arch_tsc_set_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	u64 __user *uaddr = u64_to_user_ptr(attr->addr);
	struct kvm *kvm = vcpu->kvm;
	int r;

	switch (attr->attr) {
	case KVM_VCPU_TSC_OFFSET: {
		u64 offset, tsc, ns;
		unsigned long flags;
		bool matched;

		r = -EFAULT;
		if (get_user(offset, uaddr))
			break;

		raw_spin_lock_irqsave(&kvm->arch.tsc_write_lock, flags);

		matched = (vcpu->arch.virtual_tsc_khz &&
			   kvm->arch.last_tsc_khz == vcpu->arch.virtual_tsc_khz &&
			   kvm->arch.last_tsc_offset == offset);

		tsc = kvm_scale_tsc(rdtsc(), vcpu->arch.l1_tsc_scaling_ratio) + offset;
		ns = get_kvmclock_base_ns();

		kvm->arch.user_set_tsc = true;
		__kvm_synchronize_tsc(vcpu, offset, tsc, ns, matched);
		raw_spin_unlock_irqrestore(&kvm->arch.tsc_write_lock, flags);

		r = 0;
		break;
	}
	default:
		r = -ENXIO;
	}

	return r;
}

static int kvm_vcpu_ioctl_device_attr(struct kvm_vcpu *vcpu,
				      unsigned int ioctl,
				      void __user *argp)
{
	struct kvm_device_attr attr;
	int r;

	if (copy_from_user(&attr, argp, sizeof(attr)))
		return -EFAULT;

	if (attr.group != KVM_VCPU_TSC_CTRL)
		return -ENXIO;

	switch (ioctl) {
	case KVM_HAS_DEVICE_ATTR:
		r = kvm_arch_tsc_has_attr(vcpu, &attr);
		break;
	case KVM_GET_DEVICE_ATTR:
		r = kvm_arch_tsc_get_attr(vcpu, &attr);
		break;
	case KVM_SET_DEVICE_ATTR:
		r = kvm_arch_tsc_set_attr(vcpu, &attr);
		break;
	}

	return r;
}

static int kvm_vcpu_ioctl_enable_cap(struct kvm_vcpu *vcpu,
				     struct kvm_enable_cap *cap)
{
	if (cap->flags)
		return -EINVAL;

	switch (cap->cap) {
#ifdef CONFIG_KVM_HYPERV
	case KVM_CAP_HYPERV_SYNIC2:
		if (cap->args[0])
			return -EINVAL;
		fallthrough;

	case KVM_CAP_HYPERV_SYNIC:
		if (!irqchip_in_kernel(vcpu->kvm))
			return -EINVAL;
		return kvm_hv_activate_synic(vcpu, cap->cap ==
					     KVM_CAP_HYPERV_SYNIC2);
	case KVM_CAP_HYPERV_ENLIGHTENED_VMCS:
		{
			int r;
			uint16_t vmcs_version;
			void __user *user_ptr;

			if (!kvm_x86_ops.nested_ops->enable_evmcs)
				return -ENOTTY;
			r = kvm_x86_ops.nested_ops->enable_evmcs(vcpu, &vmcs_version);
			if (!r) {
				user_ptr = (void __user *)(uintptr_t)cap->args[0];
				if (copy_to_user(user_ptr, &vmcs_version,
						 sizeof(vmcs_version)))
					r = -EFAULT;
			}
			return r;
		}
	case KVM_CAP_HYPERV_DIRECT_TLBFLUSH:
		if (!kvm_x86_ops.enable_l2_tlb_flush)
			return -ENOTTY;

		return kvm_x86_call(enable_l2_tlb_flush)(vcpu);

	case KVM_CAP_HYPERV_ENFORCE_CPUID:
		return kvm_hv_set_enforce_cpuid(vcpu, cap->args[0]);
#endif

	case KVM_CAP_ENFORCE_PV_FEATURE_CPUID:
		vcpu->arch.pv_cpuid.enforce = cap->args[0];
		if (vcpu->arch.pv_cpuid.enforce)
			kvm_update_pv_runtime(vcpu);

		return 0;
	default:
		return -EINVAL;
	}
}

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;
	union {
		struct kvm_sregs2 *sregs2;
		struct kvm_lapic_state *lapic;
		struct kvm_xsave *xsave;
		struct kvm_xcrs *xcrs;
		void *buffer;
	} u;

	vcpu_load(vcpu);

	u.buffer = NULL;
	switch (ioctl) {
	case KVM_GET_LAPIC: {
		r = -EINVAL;
		if (!lapic_in_kernel(vcpu))
			goto out;
		u.lapic = kzalloc(sizeof(struct kvm_lapic_state), GFP_KERNEL);

		r = -ENOMEM;
		if (!u.lapic)
			goto out;
		r = kvm_vcpu_ioctl_get_lapic(vcpu, u.lapic);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, u.lapic, sizeof(struct kvm_lapic_state)))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_LAPIC: {
		r = -EINVAL;
		if (!lapic_in_kernel(vcpu))
			goto out;
		u.lapic = memdup_user(argp, sizeof(*u.lapic));
		if (IS_ERR(u.lapic)) {
			r = PTR_ERR(u.lapic);
			goto out_nofree;
		}

		r = kvm_vcpu_ioctl_set_lapic(vcpu, u.lapic);
		break;
	}
	case KVM_INTERRUPT: {
		struct kvm_interrupt irq;

		r = -EFAULT;
		if (copy_from_user(&irq, argp, sizeof(irq)))
			goto out;
		r = kvm_vcpu_ioctl_interrupt(vcpu, &irq);
		break;
	}
	case KVM_NMI: {
		r = kvm_vcpu_ioctl_nmi(vcpu);
		break;
	}
	case KVM_SMI: {
		r = kvm_inject_smi(vcpu);
		break;
	}
	case KVM_SET_CPUID: {
		struct kvm_cpuid __user *cpuid_arg = argp;
		struct kvm_cpuid cpuid;

		r = -EFAULT;
		if (copy_from_user(&cpuid, cpuid_arg, sizeof(cpuid)))
			goto out;
		r = kvm_vcpu_ioctl_set_cpuid(vcpu, &cpuid, cpuid_arg->entries);
		break;
	}
	case KVM_SET_CPUID2: {
		struct kvm_cpuid2 __user *cpuid_arg = argp;
		struct kvm_cpuid2 cpuid;

		r = -EFAULT;
		if (copy_from_user(&cpuid, cpuid_arg, sizeof(cpuid)))
			goto out;
		r = kvm_vcpu_ioctl_set_cpuid2(vcpu, &cpuid,
					      cpuid_arg->entries);
		break;
	}
	case KVM_GET_CPUID2: {
		struct kvm_cpuid2 __user *cpuid_arg = argp;
		struct kvm_cpuid2 cpuid;

		r = -EFAULT;
		if (copy_from_user(&cpuid, cpuid_arg, sizeof(cpuid)))
			goto out;
		r = kvm_vcpu_ioctl_get_cpuid2(vcpu, &cpuid,
					      cpuid_arg->entries);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(cpuid_arg, &cpuid, sizeof(cpuid)))
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_MSRS: {
		int idx = srcu_read_lock(&vcpu->kvm->srcu);
		r = msr_io(vcpu, argp, do_get_msr, 1);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);
		break;
	}
	case KVM_SET_MSRS: {
		int idx = srcu_read_lock(&vcpu->kvm->srcu);
		r = msr_io(vcpu, argp, do_set_msr, 0);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);
		break;
	}
	case KVM_TPR_ACCESS_REPORTING: {
		struct kvm_tpr_access_ctl tac;

		r = -EFAULT;
		if (copy_from_user(&tac, argp, sizeof(tac)))
			goto out;
		r = vcpu_ioctl_tpr_access_reporting(vcpu, &tac);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &tac, sizeof(tac)))
			goto out;
		r = 0;
		break;
	};
	case KVM_SET_VAPIC_ADDR: {
		struct kvm_vapic_addr va;
		int idx;

		r = -EINVAL;
		if (!lapic_in_kernel(vcpu))
			goto out;
		r = -EFAULT;
		if (copy_from_user(&va, argp, sizeof(va)))
			goto out;
		idx = srcu_read_lock(&vcpu->kvm->srcu);
		r = kvm_lapic_set_vapic_addr(vcpu, va.vapic_addr);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);
		break;
	}
	case KVM_X86_SETUP_MCE: {
		u64 mcg_cap;

		r = -EFAULT;
		if (copy_from_user(&mcg_cap, argp, sizeof(mcg_cap)))
			goto out;
		r = kvm_vcpu_ioctl_x86_setup_mce(vcpu, mcg_cap);
		break;
	}
	case KVM_X86_SET_MCE: {
		struct kvm_x86_mce mce;

		r = -EFAULT;
		if (copy_from_user(&mce, argp, sizeof(mce)))
			goto out;
		r = kvm_vcpu_ioctl_x86_set_mce(vcpu, &mce);
		break;
	}
	case KVM_GET_VCPU_EVENTS: {
		struct kvm_vcpu_events events;

		kvm_vcpu_ioctl_x86_get_vcpu_events(vcpu, &events);

		r = -EFAULT;
		if (copy_to_user(argp, &events, sizeof(struct kvm_vcpu_events)))
			break;
		r = 0;
		break;
	}
	case KVM_SET_VCPU_EVENTS: {
		struct kvm_vcpu_events events;

		r = -EFAULT;
		if (copy_from_user(&events, argp, sizeof(struct kvm_vcpu_events)))
			break;

		kvm_vcpu_srcu_read_lock(vcpu);
		r = kvm_vcpu_ioctl_x86_set_vcpu_events(vcpu, &events);
		kvm_vcpu_srcu_read_unlock(vcpu);
		break;
	}
	case KVM_GET_DEBUGREGS: {
		struct kvm_debugregs dbgregs;

		r = kvm_vcpu_ioctl_x86_get_debugregs(vcpu, &dbgregs);
		if (r < 0)
			break;

		r = -EFAULT;
		if (copy_to_user(argp, &dbgregs,
				 sizeof(struct kvm_debugregs)))
			break;
		r = 0;
		break;
	}
	case KVM_SET_DEBUGREGS: {
		struct kvm_debugregs dbgregs;

		r = -EFAULT;
		if (copy_from_user(&dbgregs, argp,
				   sizeof(struct kvm_debugregs)))
			break;

		r = kvm_vcpu_ioctl_x86_set_debugregs(vcpu, &dbgregs);
		break;
	}
	case KVM_GET_XSAVE: {
		r = -EINVAL;
		if (vcpu->arch.guest_fpu.uabi_size > sizeof(struct kvm_xsave))
			break;

		u.xsave = kzalloc(sizeof(struct kvm_xsave), GFP_KERNEL);
		r = -ENOMEM;
		if (!u.xsave)
			break;

		r = kvm_vcpu_ioctl_x86_get_xsave(vcpu, u.xsave);
		if (r < 0)
			break;

		r = -EFAULT;
		if (copy_to_user(argp, u.xsave, sizeof(struct kvm_xsave)))
			break;
		r = 0;
		break;
	}
	case KVM_SET_XSAVE: {
		int size = vcpu->arch.guest_fpu.uabi_size;

		u.xsave = memdup_user(argp, size);
		if (IS_ERR(u.xsave)) {
			r = PTR_ERR(u.xsave);
			goto out_nofree;
		}

		r = kvm_vcpu_ioctl_x86_set_xsave(vcpu, u.xsave);
		break;
	}

	case KVM_GET_XSAVE2: {
		int size = vcpu->arch.guest_fpu.uabi_size;

		u.xsave = kzalloc(size, GFP_KERNEL);
		r = -ENOMEM;
		if (!u.xsave)
			break;

		r = kvm_vcpu_ioctl_x86_get_xsave2(vcpu, u.buffer, size);
		if (r < 0)
			break;

		r = -EFAULT;
		if (copy_to_user(argp, u.xsave, size))
			break;

		r = 0;
		break;
	}

	case KVM_GET_XCRS: {
		u.xcrs = kzalloc(sizeof(struct kvm_xcrs), GFP_KERNEL);
		r = -ENOMEM;
		if (!u.xcrs)
			break;

		r = kvm_vcpu_ioctl_x86_get_xcrs(vcpu, u.xcrs);
		if (r < 0)
			break;

		r = -EFAULT;
		if (copy_to_user(argp, u.xcrs,
				 sizeof(struct kvm_xcrs)))
			break;
		r = 0;
		break;
	}
	case KVM_SET_XCRS: {
		u.xcrs = memdup_user(argp, sizeof(*u.xcrs));
		if (IS_ERR(u.xcrs)) {
			r = PTR_ERR(u.xcrs);
			goto out_nofree;
		}

		r = kvm_vcpu_ioctl_x86_set_xcrs(vcpu, u.xcrs);
		break;
	}
	case KVM_SET_TSC_KHZ: {
		u32 user_tsc_khz;

		r = -EINVAL;
		user_tsc_khz = (u32)arg;

		if (kvm_caps.has_tsc_control &&
		    user_tsc_khz >= kvm_caps.max_guest_tsc_khz)
			goto out;

		if (user_tsc_khz == 0)
			user_tsc_khz = tsc_khz;

		if (!kvm_set_tsc_khz(vcpu, user_tsc_khz))
			r = 0;

		goto out;
	}
	case KVM_GET_TSC_KHZ: {
		r = vcpu->arch.virtual_tsc_khz;
		goto out;
	}
	case KVM_KVMCLOCK_CTRL: {
		r = kvm_set_guest_paused(vcpu);
		goto out;
	}
	case KVM_ENABLE_CAP: {
		struct kvm_enable_cap cap;

		r = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			goto out;
		r = kvm_vcpu_ioctl_enable_cap(vcpu, &cap);
		break;
	}
	case KVM_GET_NESTED_STATE: {
		struct kvm_nested_state __user *user_kvm_nested_state = argp;
		u32 user_data_size;

		r = -EINVAL;
		if (!kvm_x86_ops.nested_ops->get_state)
			break;

		BUILD_BUG_ON(sizeof(user_data_size) != sizeof(user_kvm_nested_state->size));
		r = -EFAULT;
		if (get_user(user_data_size, &user_kvm_nested_state->size))
			break;

		r = kvm_x86_ops.nested_ops->get_state(vcpu, user_kvm_nested_state,
						     user_data_size);
		if (r < 0)
			break;

		if (r > user_data_size) {
			if (put_user(r, &user_kvm_nested_state->size))
				r = -EFAULT;
			else
				r = -E2BIG;
			break;
		}

		r = 0;
		break;
	}
	case KVM_SET_NESTED_STATE: {
		struct kvm_nested_state __user *user_kvm_nested_state = argp;
		struct kvm_nested_state kvm_state;
		int idx;

		r = -EINVAL;
		if (!kvm_x86_ops.nested_ops->set_state)
			break;

		r = -EFAULT;
		if (copy_from_user(&kvm_state, user_kvm_nested_state, sizeof(kvm_state)))
			break;

		r = -EINVAL;
		if (kvm_state.size < sizeof(kvm_state))
			break;

		if (kvm_state.flags &
		    ~(KVM_STATE_NESTED_RUN_PENDING | KVM_STATE_NESTED_GUEST_MODE
		      | KVM_STATE_NESTED_EVMCS | KVM_STATE_NESTED_MTF_PENDING
		      | KVM_STATE_NESTED_GIF_SET))
			break;

		/* nested_run_pending implies guest_mode.  */
		if ((kvm_state.flags & KVM_STATE_NESTED_RUN_PENDING)
		    && !(kvm_state.flags & KVM_STATE_NESTED_GUEST_MODE))
			break;

		idx = srcu_read_lock(&vcpu->kvm->srcu);
		r = kvm_x86_ops.nested_ops->set_state(vcpu, user_kvm_nested_state, &kvm_state);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);
		break;
	}
#ifdef CONFIG_KVM_HYPERV
	case KVM_GET_SUPPORTED_HV_CPUID:
		r = kvm_ioctl_get_supported_hv_cpuid(vcpu, argp);
		break;
#endif
#ifdef CONFIG_KVM_XEN
	case KVM_XEN_VCPU_GET_ATTR: {
		struct kvm_xen_vcpu_attr xva;

		r = -EFAULT;
		if (copy_from_user(&xva, argp, sizeof(xva)))
			goto out;
		r = kvm_xen_vcpu_get_attr(vcpu, &xva);
		if (!r && copy_to_user(argp, &xva, sizeof(xva)))
			r = -EFAULT;
		break;
	}
	case KVM_XEN_VCPU_SET_ATTR: {
		struct kvm_xen_vcpu_attr xva;

		r = -EFAULT;
		if (copy_from_user(&xva, argp, sizeof(xva)))
			goto out;
		r = kvm_xen_vcpu_set_attr(vcpu, &xva);
		break;
	}
#endif
	case KVM_GET_SREGS2: {
		r = -EINVAL;
		if (vcpu->kvm->arch.has_protected_state &&
		    vcpu->arch.guest_state_protected)
			goto out;

		u.sregs2 = kzalloc(sizeof(struct kvm_sregs2), GFP_KERNEL);
		r = -ENOMEM;
		if (!u.sregs2)
			goto out;
		__get_sregs2(vcpu, u.sregs2);
		r = -EFAULT;
		if (copy_to_user(argp, u.sregs2, sizeof(struct kvm_sregs2)))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_SREGS2: {
		r = -EINVAL;
		if (vcpu->kvm->arch.has_protected_state &&
		    vcpu->arch.guest_state_protected)
			goto out;

		u.sregs2 = memdup_user(argp, sizeof(struct kvm_sregs2));
		if (IS_ERR(u.sregs2)) {
			r = PTR_ERR(u.sregs2);
			u.sregs2 = NULL;
			goto out;
		}
		r = __set_sregs2(vcpu, u.sregs2);
		break;
	}
	case KVM_HAS_DEVICE_ATTR:
	case KVM_GET_DEVICE_ATTR:
	case KVM_SET_DEVICE_ATTR:
		r = kvm_vcpu_ioctl_device_attr(vcpu, ioctl, argp);
		break;
	default:
		r = -EINVAL;
	}
out:
	kfree(u.buffer);
out_nofree:
	vcpu_put(vcpu);
	return r;
}

vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

static int kvm_vm_ioctl_set_tss_addr(struct kvm *kvm, unsigned long addr)
{
	int ret;

	if (addr > (unsigned int)(-3 * PAGE_SIZE))
		return -EINVAL;
	ret = kvm_x86_call(set_tss_addr)(kvm, addr);
	return ret;
}

static int kvm_vm_ioctl_set_identity_map_addr(struct kvm *kvm,
					      u64 ident_addr)
{
	return kvm_x86_call(set_identity_map_addr)(kvm, ident_addr);
}

static int kvm_vm_ioctl_set_nr_mmu_pages(struct kvm *kvm,
					 unsigned long kvm_nr_mmu_pages)
{
	if (kvm_nr_mmu_pages < KVM_MIN_ALLOC_MMU_PAGES)
		return -EINVAL;

	mutex_lock(&kvm->slots_lock);

	kvm_mmu_change_mmu_pages(kvm, kvm_nr_mmu_pages);
	kvm->arch.n_requested_mmu_pages = kvm_nr_mmu_pages;

	mutex_unlock(&kvm->slots_lock);
	return 0;
}

static int kvm_vm_ioctl_get_irqchip(struct kvm *kvm, struct kvm_irqchip *chip)
{
	struct kvm_pic *pic = kvm->arch.vpic;
	int r;

	r = 0;
	switch (chip->chip_id) {
	case KVM_IRQCHIP_PIC_MASTER:
		memcpy(&chip->chip.pic, &pic->pics[0],
			sizeof(struct kvm_pic_state));
		break;
	case KVM_IRQCHIP_PIC_SLAVE:
		memcpy(&chip->chip.pic, &pic->pics[1],
			sizeof(struct kvm_pic_state));
		break;
	case KVM_IRQCHIP_IOAPIC:
		kvm_get_ioapic(kvm, &chip->chip.ioapic);
		break;
	default:
		r = -EINVAL;
		break;
	}
	return r;
}

static int kvm_vm_ioctl_set_irqchip(struct kvm *kvm, struct kvm_irqchip *chip)
{
	struct kvm_pic *pic = kvm->arch.vpic;
	int r;

	r = 0;
	switch (chip->chip_id) {
	case KVM_IRQCHIP_PIC_MASTER:
		spin_lock(&pic->lock);
		memcpy(&pic->pics[0], &chip->chip.pic,
			sizeof(struct kvm_pic_state));
		spin_unlock(&pic->lock);
		break;
	case KVM_IRQCHIP_PIC_SLAVE:
		spin_lock(&pic->lock);
		memcpy(&pic->pics[1], &chip->chip.pic,
			sizeof(struct kvm_pic_state));
		spin_unlock(&pic->lock);
		break;
	case KVM_IRQCHIP_IOAPIC:
		kvm_set_ioapic(kvm, &chip->chip.ioapic);
		break;
	default:
		r = -EINVAL;
		break;
	}
	kvm_pic_update_irq(pic);
	return r;
}

static int kvm_vm_ioctl_get_pit(struct kvm *kvm, struct kvm_pit_state *ps)
{
	struct kvm_kpit_state *kps = &kvm->arch.vpit->pit_state;

	BUILD_BUG_ON(sizeof(*ps) != sizeof(kps->channels));

	mutex_lock(&kps->lock);
	memcpy(ps, &kps->channels, sizeof(*ps));
	mutex_unlock(&kps->lock);
	return 0;
}

static int kvm_vm_ioctl_set_pit(struct kvm *kvm, struct kvm_pit_state *ps)
{
	int i;
	struct kvm_pit *pit = kvm->arch.vpit;

	mutex_lock(&pit->pit_state.lock);
	memcpy(&pit->pit_state.channels, ps, sizeof(*ps));
	for (i = 0; i < 3; i++)
		kvm_pit_load_count(pit, i, ps->channels[i].count, 0);
	mutex_unlock(&pit->pit_state.lock);
	return 0;
}

static int kvm_vm_ioctl_get_pit2(struct kvm *kvm, struct kvm_pit_state2 *ps)
{
	mutex_lock(&kvm->arch.vpit->pit_state.lock);
	memcpy(ps->channels, &kvm->arch.vpit->pit_state.channels,
		sizeof(ps->channels));
	ps->flags = kvm->arch.vpit->pit_state.flags;
	mutex_unlock(&kvm->arch.vpit->pit_state.lock);
	memset(&ps->reserved, 0, sizeof(ps->reserved));
	return 0;
}

static int kvm_vm_ioctl_set_pit2(struct kvm *kvm, struct kvm_pit_state2 *ps)
{
	int start = 0;
	int i;
	u32 prev_legacy, cur_legacy;
	struct kvm_pit *pit = kvm->arch.vpit;

	mutex_lock(&pit->pit_state.lock);
	prev_legacy = pit->pit_state.flags & KVM_PIT_FLAGS_HPET_LEGACY;
	cur_legacy = ps->flags & KVM_PIT_FLAGS_HPET_LEGACY;
	if (!prev_legacy && cur_legacy)
		start = 1;
	memcpy(&pit->pit_state.channels, &ps->channels,
	       sizeof(pit->pit_state.channels));
	pit->pit_state.flags = ps->flags;
	for (i = 0; i < 3; i++)
		kvm_pit_load_count(pit, i, pit->pit_state.channels[i].count,
				   start && i == 0);
	mutex_unlock(&pit->pit_state.lock);
	return 0;
}

static int kvm_vm_ioctl_reinject(struct kvm *kvm,
				 struct kvm_reinject_control *control)
{
	struct kvm_pit *pit = kvm->arch.vpit;

	/* pit->pit_state.lock was overloaded to prevent userspace from getting
	 * an inconsistent state after running multiple KVM_REINJECT_CONTROL
	 * ioctls in parallel.  Use a separate lock if that ioctl isn't rare.
	 */
	mutex_lock(&pit->pit_state.lock);
	kvm_pit_set_reinject(pit, control->pit_reinject);
	mutex_unlock(&pit->pit_state.lock);

	return 0;
}

void kvm_arch_sync_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot)
{

	/*
	 * Flush all CPUs' dirty log buffers to the  dirty_bitmap.  Called
	 * before reporting dirty_bitmap to userspace.  KVM flushes the buffers
	 * on all VM-Exits, thus we only need to kick running vCPUs to force a
	 * VM-Exit.
	 */
	struct kvm_vcpu *vcpu;
	unsigned long i;

	if (!kvm_x86_ops.cpu_dirty_log_size)
		return;

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_vcpu_kick(vcpu);
}

int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irq_event,
			bool line_status)
{
	if (!irqchip_in_kernel(kvm))
		return -ENXIO;

	irq_event->status = kvm_set_irq(kvm, KVM_USERSPACE_IRQ_SOURCE_ID,
					irq_event->irq, irq_event->level,
					line_status);
	return 0;
}

int kvm_vm_ioctl_enable_cap(struct kvm *kvm,
			    struct kvm_enable_cap *cap)
{
	int r;

	if (cap->flags)
		return -EINVAL;

	switch (cap->cap) {
	case KVM_CAP_DISABLE_QUIRKS2:
		r = -EINVAL;
		if (cap->args[0] & ~KVM_X86_VALID_QUIRKS)
			break;
		fallthrough;
	case KVM_CAP_DISABLE_QUIRKS:
		kvm->arch.disabled_quirks = cap->args[0];
		r = 0;
		break;
	case KVM_CAP_SPLIT_IRQCHIP: {
		mutex_lock(&kvm->lock);
		r = -EINVAL;
		if (cap->args[0] > MAX_NR_RESERVED_IOAPIC_PINS)
			goto split_irqchip_unlock;
		r = -EEXIST;
		if (irqchip_in_kernel(kvm))
			goto split_irqchip_unlock;
		if (kvm->created_vcpus)
			goto split_irqchip_unlock;
		/* Pairs with irqchip_in_kernel. */
		smp_wmb();
		kvm->arch.irqchip_mode = KVM_IRQCHIP_SPLIT;
		kvm->arch.nr_reserved_ioapic_pins = cap->args[0];
		kvm_clear_apicv_inhibit(kvm, APICV_INHIBIT_REASON_ABSENT);
		r = 0;
split_irqchip_unlock:
		mutex_unlock(&kvm->lock);
		break;
	}
	case KVM_CAP_X2APIC_API:
		r = -EINVAL;
		if (cap->args[0] & ~KVM_X2APIC_API_VALID_FLAGS)
			break;

		if (cap->args[0] & KVM_X2APIC_API_USE_32BIT_IDS)
			kvm->arch.x2apic_format = true;
		if (cap->args[0] & KVM_X2APIC_API_DISABLE_BROADCAST_QUIRK)
			kvm->arch.x2apic_broadcast_quirk_disabled = true;

		r = 0;
		break;
	case KVM_CAP_X86_DISABLE_EXITS:
		r = -EINVAL;
		if (cap->args[0] & ~KVM_X86_DISABLE_VALID_EXITS)
			break;

		if (cap->args[0] & KVM_X86_DISABLE_EXITS_PAUSE)
			kvm->arch.pause_in_guest = true;

#define SMT_RSB_MSG "This processor is affected by the Cross-Thread Return Predictions vulnerability. " \
		    "KVM_CAP_X86_DISABLE_EXITS should only be used with SMT disabled or trusted guests."

		if (!mitigate_smt_rsb) {
			if (boot_cpu_has_bug(X86_BUG_SMT_RSB) && cpu_smt_possible() &&
			    (cap->args[0] & ~KVM_X86_DISABLE_EXITS_PAUSE))
				pr_warn_once(SMT_RSB_MSG);

			if ((cap->args[0] & KVM_X86_DISABLE_EXITS_MWAIT) &&
			    kvm_can_mwait_in_guest())
				kvm->arch.mwait_in_guest = true;
			if (cap->args[0] & KVM_X86_DISABLE_EXITS_HLT)
				kvm->arch.hlt_in_guest = true;
			if (cap->args[0] & KVM_X86_DISABLE_EXITS_CSTATE)
				kvm->arch.cstate_in_guest = true;
		}

		r = 0;
		break;
	case KVM_CAP_MSR_PLATFORM_INFO:
		kvm->arch.guest_can_read_msr_platform_info = cap->args[0];
		r = 0;
		break;
	case KVM_CAP_EXCEPTION_PAYLOAD:
		kvm->arch.exception_payload_enabled = cap->args[0];
		r = 0;
		break;
	case KVM_CAP_X86_TRIPLE_FAULT_EVENT:
		kvm->arch.triple_fault_event = cap->args[0];
		r = 0;
		break;
	case KVM_CAP_X86_USER_SPACE_MSR:
		r = -EINVAL;
		if (cap->args[0] & ~KVM_MSR_EXIT_REASON_VALID_MASK)
			break;
		kvm->arch.user_space_msr_mask = cap->args[0];
		r = 0;
		break;
	case KVM_CAP_X86_BUS_LOCK_EXIT:
		r = -EINVAL;
		if (cap->args[0] & ~KVM_BUS_LOCK_DETECTION_VALID_MODE)
			break;

		if ((cap->args[0] & KVM_BUS_LOCK_DETECTION_OFF) &&
		    (cap->args[0] & KVM_BUS_LOCK_DETECTION_EXIT))
			break;

		if (kvm_caps.has_bus_lock_exit &&
		    cap->args[0] & KVM_BUS_LOCK_DETECTION_EXIT)
			kvm->arch.bus_lock_detection_enabled = true;
		r = 0;
		break;
#ifdef CONFIG_X86_SGX_KVM
	case KVM_CAP_SGX_ATTRIBUTE: {
		unsigned long allowed_attributes = 0;

		r = sgx_set_attribute(&allowed_attributes, cap->args[0]);
		if (r)
			break;

		/* KVM only supports the PROVISIONKEY privileged attribute. */
		if ((allowed_attributes & SGX_ATTR_PROVISIONKEY) &&
		    !(allowed_attributes & ~SGX_ATTR_PROVISIONKEY))
			kvm->arch.sgx_provisioning_allowed = true;
		else
			r = -EINVAL;
		break;
	}
#endif
	case KVM_CAP_VM_COPY_ENC_CONTEXT_FROM:
		r = -EINVAL;
		if (!kvm_x86_ops.vm_copy_enc_context_from)
			break;

		r = kvm_x86_call(vm_copy_enc_context_from)(kvm, cap->args[0]);
		break;
	case KVM_CAP_VM_MOVE_ENC_CONTEXT_FROM:
		r = -EINVAL;
		if (!kvm_x86_ops.vm_move_enc_context_from)
			break;

		r = kvm_x86_call(vm_move_enc_context_from)(kvm, cap->args[0]);
		break;
	case KVM_CAP_EXIT_HYPERCALL:
		if (cap->args[0] & ~KVM_EXIT_HYPERCALL_VALID_MASK) {
			r = -EINVAL;
			break;
		}
		kvm->arch.hypercall_exit_enabled = cap->args[0];
		r = 0;
		break;
	case KVM_CAP_EXIT_ON_EMULATION_FAILURE:
		r = -EINVAL;
		if (cap->args[0] & ~1)
			break;
		kvm->arch.exit_on_emulation_error = cap->args[0];
		r = 0;
		break;
	case KVM_CAP_PMU_CAPABILITY:
		r = -EINVAL;
		if (!enable_pmu || (cap->args[0] & ~KVM_CAP_PMU_VALID_MASK))
			break;

		mutex_lock(&kvm->lock);
		if (!kvm->created_vcpus) {
			kvm->arch.enable_pmu = !(cap->args[0] & KVM_PMU_CAP_DISABLE);
			r = 0;
		}
		mutex_unlock(&kvm->lock);
		break;
	case KVM_CAP_MAX_VCPU_ID:
		r = -EINVAL;
		if (cap->args[0] > KVM_MAX_VCPU_IDS)
			break;

		mutex_lock(&kvm->lock);
		if (kvm->arch.bsp_vcpu_id > cap->args[0]) {
			;
		} else if (kvm->arch.max_vcpu_ids == cap->args[0]) {
			r = 0;
		} else if (!kvm->arch.max_vcpu_ids) {
			kvm->arch.max_vcpu_ids = cap->args[0];
			r = 0;
		}
		mutex_unlock(&kvm->lock);
		break;
	case KVM_CAP_X86_NOTIFY_VMEXIT:
		r = -EINVAL;
		if ((u32)cap->args[0] & ~KVM_X86_NOTIFY_VMEXIT_VALID_BITS)
			break;
		if (!kvm_caps.has_notify_vmexit)
			break;
		if (!((u32)cap->args[0] & KVM_X86_NOTIFY_VMEXIT_ENABLED))
			break;
		mutex_lock(&kvm->lock);
		if (!kvm->created_vcpus) {
			kvm->arch.notify_window = cap->args[0] >> 32;
			kvm->arch.notify_vmexit_flags = (u32)cap->args[0];
			r = 0;
		}
		mutex_unlock(&kvm->lock);
		break;
	case KVM_CAP_VM_DISABLE_NX_HUGE_PAGES:
		r = -EINVAL;

		/*
		 * Since the risk of disabling NX hugepages is a guest crashing
		 * the system, ensure the userspace process has permission to
		 * reboot the system.
		 *
		 * Note that unlike the reboot() syscall, the process must have
		 * this capability in the root namespace because exposing
		 * /dev/kvm into a container does not limit the scope of the
		 * iTLB multihit bug to that container. In other words,
		 * this must use capable(), not ns_capable().
		 */
		if (!capable(CAP_SYS_BOOT)) {
			r = -EPERM;
			break;
		}

		if (cap->args[0])
			break;

		mutex_lock(&kvm->lock);
		if (!kvm->created_vcpus) {
			kvm->arch.disable_nx_huge_pages = true;
			r = 0;
		}
		mutex_unlock(&kvm->lock);
		break;
	case KVM_CAP_X86_APIC_BUS_CYCLES_NS: {
		u64 bus_cycle_ns = cap->args[0];
		u64 unused;

		/*
		 * Guard against overflow in tmict_to_ns(). 128 is the highest
		 * divide value that can be programmed in APIC_TDCR.
		 */
		r = -EINVAL;
		if (!bus_cycle_ns ||
		    check_mul_overflow((u64)U32_MAX * 128, bus_cycle_ns, &unused))
			break;

		r = 0;
		mutex_lock(&kvm->lock);
		if (!irqchip_in_kernel(kvm))
			r = -ENXIO;
		else if (kvm->created_vcpus)
			r = -EINVAL;
		else
			kvm->arch.apic_bus_cycle_ns = bus_cycle_ns;
		mutex_unlock(&kvm->lock);
		break;
	}
	default:
		r = -EINVAL;
		break;
	}
	return r;
}

static struct kvm_x86_msr_filter *kvm_alloc_msr_filter(bool default_allow)
{
	struct kvm_x86_msr_filter *msr_filter;

	msr_filter = kzalloc(sizeof(*msr_filter), GFP_KERNEL_ACCOUNT);
	if (!msr_filter)
		return NULL;

	msr_filter->default_allow = default_allow;
	return msr_filter;
}

static void kvm_free_msr_filter(struct kvm_x86_msr_filter *msr_filter)
{
	u32 i;

	if (!msr_filter)
		return;

	for (i = 0; i < msr_filter->count; i++)
		kfree(msr_filter->ranges[i].bitmap);

	kfree(msr_filter);
}

static int kvm_add_msr_filter(struct kvm_x86_msr_filter *msr_filter,
			      struct kvm_msr_filter_range *user_range)
{
	unsigned long *bitmap;
	size_t bitmap_size;

	if (!user_range->nmsrs)
		return 0;

	if (user_range->flags & ~KVM_MSR_FILTER_RANGE_VALID_MASK)
		return -EINVAL;

	if (!user_range->flags)
		return -EINVAL;

	bitmap_size = BITS_TO_LONGS(user_range->nmsrs) * sizeof(long);
	if (!bitmap_size || bitmap_size > KVM_MSR_FILTER_MAX_BITMAP_SIZE)
		return -EINVAL;

	bitmap = memdup_user((__user u8*)user_range->bitmap, bitmap_size);
	if (IS_ERR(bitmap))
		return PTR_ERR(bitmap);

	msr_filter->ranges[msr_filter->count] = (struct msr_bitmap_range) {
		.flags = user_range->flags,
		.base = user_range->base,
		.nmsrs = user_range->nmsrs,
		.bitmap = bitmap,
	};

	msr_filter->count++;
	return 0;
}

static int kvm_vm_ioctl_set_msr_filter(struct kvm *kvm,
				       struct kvm_msr_filter *filter)
{
	struct kvm_x86_msr_filter *new_filter, *old_filter;
	bool default_allow;
	bool empty = true;
	int r;
	u32 i;

	if (filter->flags & ~KVM_MSR_FILTER_VALID_MASK)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(filter->ranges); i++)
		empty &= !filter->ranges[i].nmsrs;

	default_allow = !(filter->flags & KVM_MSR_FILTER_DEFAULT_DENY);
	if (empty && !default_allow)
		return -EINVAL;

	new_filter = kvm_alloc_msr_filter(default_allow);
	if (!new_filter)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(filter->ranges); i++) {
		r = kvm_add_msr_filter(new_filter, &filter->ranges[i]);
		if (r) {
			kvm_free_msr_filter(new_filter);
			return r;
		}
	}

	mutex_lock(&kvm->lock);
	old_filter = rcu_replace_pointer(kvm->arch.msr_filter, new_filter,
					 mutex_is_locked(&kvm->lock));
	mutex_unlock(&kvm->lock);
	synchronize_srcu(&kvm->srcu);

	kvm_free_msr_filter(old_filter);

	kvm_make_all_cpus_request(kvm, KVM_REQ_MSR_FILTER_CHANGED);

	return 0;
}

#ifdef CONFIG_KVM_COMPAT
/* for KVM_X86_SET_MSR_FILTER */
struct kvm_msr_filter_range_compat {
	__u32 flags;
	__u32 nmsrs;
	__u32 base;
	__u32 bitmap;
};

struct kvm_msr_filter_compat {
	__u32 flags;
	struct kvm_msr_filter_range_compat ranges[KVM_MSR_FILTER_MAX_RANGES];
};

#define KVM_X86_SET_MSR_FILTER_COMPAT _IOW(KVMIO, 0xc6, struct kvm_msr_filter_compat)

long kvm_arch_vm_compat_ioctl(struct file *filp, unsigned int ioctl,
			      unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct kvm *kvm = filp->private_data;
	long r = -ENOTTY;

	switch (ioctl) {
	case KVM_X86_SET_MSR_FILTER_COMPAT: {
		struct kvm_msr_filter __user *user_msr_filter = argp;
		struct kvm_msr_filter_compat filter_compat;
		struct kvm_msr_filter filter;
		int i;

		if (copy_from_user(&filter_compat, user_msr_filter,
				   sizeof(filter_compat)))
			return -EFAULT;

		filter.flags = filter_compat.flags;
		for (i = 0; i < ARRAY_SIZE(filter.ranges); i++) {
			struct kvm_msr_filter_range_compat *cr;

			cr = &filter_compat.ranges[i];
			filter.ranges[i] = (struct kvm_msr_filter_range) {
				.flags = cr->flags,
				.nmsrs = cr->nmsrs,
				.base = cr->base,
				.bitmap = (__u8 *)(ulong)cr->bitmap,
			};
		}

		r = kvm_vm_ioctl_set_msr_filter(kvm, &filter);
		break;
	}
	}

	return r;
}
#endif

#ifdef CONFIG_HAVE_KVM_PM_NOTIFIER
static int kvm_arch_suspend_notifier(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu;
	unsigned long i;
	int ret = 0;

	mutex_lock(&kvm->lock);
	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!vcpu->arch.pv_time.active)
			continue;

		ret = kvm_set_guest_paused(vcpu);
		if (ret) {
			kvm_err("Failed to pause guest VCPU%d: %d\n",
				vcpu->vcpu_id, ret);
			break;
		}
	}
	mutex_unlock(&kvm->lock);

	return ret ? NOTIFY_BAD : NOTIFY_DONE;
}

int kvm_arch_pm_notifier(struct kvm *kvm, unsigned long state)
{
	switch (state) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		return kvm_arch_suspend_notifier(kvm);
	}

	return NOTIFY_DONE;
}
#endif /* CONFIG_HAVE_KVM_PM_NOTIFIER */

static int kvm_vm_ioctl_get_clock(struct kvm *kvm, void __user *argp)
{
	struct kvm_clock_data data = { 0 };

	get_kvmclock(kvm, &data);
	if (copy_to_user(argp, &data, sizeof(data)))
		return -EFAULT;

	return 0;
}

static int kvm_vm_ioctl_set_clock(struct kvm *kvm, void __user *argp)
{
	struct kvm_arch *ka = &kvm->arch;
	struct kvm_clock_data data;
	u64 now_raw_ns;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;

	/*
	 * Only KVM_CLOCK_REALTIME is used, but allow passing the
	 * result of KVM_GET_CLOCK back to KVM_SET_CLOCK.
	 */
	if (data.flags & ~KVM_CLOCK_VALID_FLAGS)
		return -EINVAL;

	kvm_hv_request_tsc_page_update(kvm);
	kvm_start_pvclock_update(kvm);
	pvclock_update_vm_gtod_copy(kvm);

	/*
	 * This pairs with kvm_guest_time_update(): when masterclock is
	 * in use, we use master_kernel_ns + kvmclock_offset to set
	 * unsigned 'system_time' so if we use get_kvmclock_ns() (which
	 * is slightly ahead) here we risk going negative on unsigned
	 * 'system_time' when 'data.clock' is very small.
	 */
	if (data.flags & KVM_CLOCK_REALTIME) {
		u64 now_real_ns = ktime_get_real_ns();

		/*
		 * Avoid stepping the kvmclock backwards.
		 */
		if (now_real_ns > data.realtime)
			data.clock += now_real_ns - data.realtime;
	}

	if (ka->use_master_clock)
		now_raw_ns = ka->master_kernel_ns;
	else
		now_raw_ns = get_kvmclock_base_ns();
	ka->kvmclock_offset = data.clock - now_raw_ns;
	kvm_end_pvclock_update(kvm);
	return 0;
}

int kvm_arch_vm_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r = -ENOTTY;
	/*
	 * This union makes it completely explicit to gcc-3.x
	 * that these two variables' stack usage should be
	 * combined, not added together.
	 */
	union {
		struct kvm_pit_state ps;
		struct kvm_pit_state2 ps2;
		struct kvm_pit_config pit_config;
	} u;

	switch (ioctl) {
	case KVM_SET_TSS_ADDR:
		r = kvm_vm_ioctl_set_tss_addr(kvm, arg);
		break;
	case KVM_SET_IDENTITY_MAP_ADDR: {
		u64 ident_addr;

		mutex_lock(&kvm->lock);
		r = -EINVAL;
		if (kvm->created_vcpus)
			goto set_identity_unlock;
		r = -EFAULT;
		if (copy_from_user(&ident_addr, argp, sizeof(ident_addr)))
			goto set_identity_unlock;
		r = kvm_vm_ioctl_set_identity_map_addr(kvm, ident_addr);
set_identity_unlock:
		mutex_unlock(&kvm->lock);
		break;
	}
	case KVM_SET_NR_MMU_PAGES:
		r = kvm_vm_ioctl_set_nr_mmu_pages(kvm, arg);
		break;
	case KVM_CREATE_IRQCHIP: {
		mutex_lock(&kvm->lock);

		r = -EEXIST;
		if (irqchip_in_kernel(kvm))
			goto create_irqchip_unlock;

		r = -EINVAL;
		if (kvm->created_vcpus)
			goto create_irqchip_unlock;

		r = kvm_pic_init(kvm);
		if (r)
			goto create_irqchip_unlock;

		r = kvm_ioapic_init(kvm);
		if (r) {
			kvm_pic_destroy(kvm);
			goto create_irqchip_unlock;
		}

		r = kvm_setup_default_irq_routing(kvm);
		if (r) {
			kvm_ioapic_destroy(kvm);
			kvm_pic_destroy(kvm);
			goto create_irqchip_unlock;
		}
		/* Write kvm->irq_routing before enabling irqchip_in_kernel. */
		smp_wmb();
		kvm->arch.irqchip_mode = KVM_IRQCHIP_KERNEL;
		kvm_clear_apicv_inhibit(kvm, APICV_INHIBIT_REASON_ABSENT);
	create_irqchip_unlock:
		mutex_unlock(&kvm->lock);
		break;
	}
	case KVM_CREATE_PIT:
		u.pit_config.flags = KVM_PIT_SPEAKER_DUMMY;
		goto create_pit;
	case KVM_CREATE_PIT2:
		r = -EFAULT;
		if (copy_from_user(&u.pit_config, argp,
				   sizeof(struct kvm_pit_config)))
			goto out;
	create_pit:
		mutex_lock(&kvm->lock);
		r = -EEXIST;
		if (kvm->arch.vpit)
			goto create_pit_unlock;
		r = -ENOENT;
		if (!pic_in_kernel(kvm))
			goto create_pit_unlock;
		r = -ENOMEM;
		kvm->arch.vpit = kvm_create_pit(kvm, u.pit_config.flags);
		if (kvm->arch.vpit)
			r = 0;
	create_pit_unlock:
		mutex_unlock(&kvm->lock);
		break;
	case KVM_GET_IRQCHIP: {
		/* 0: PIC master, 1: PIC slave, 2: IOAPIC */
		struct kvm_irqchip *chip;

		chip = memdup_user(argp, sizeof(*chip));
		if (IS_ERR(chip)) {
			r = PTR_ERR(chip);
			goto out;
		}

		r = -ENXIO;
		if (!irqchip_kernel(kvm))
			goto get_irqchip_out;
		r = kvm_vm_ioctl_get_irqchip(kvm, chip);
		if (r)
			goto get_irqchip_out;
		r = -EFAULT;
		if (copy_to_user(argp, chip, sizeof(*chip)))
			goto get_irqchip_out;
		r = 0;
	get_irqchip_out:
		kfree(chip);
		break;
	}
	case KVM_SET_IRQCHIP: {
		/* 0: PIC master, 1: PIC slave, 2: IOAPIC */
		struct kvm_irqchip *chip;

		chip = memdup_user(argp, sizeof(*chip));
		if (IS_ERR(chip)) {
			r = PTR_ERR(chip);
			goto out;
		}

		r = -ENXIO;
		if (!irqchip_kernel(kvm))
			goto set_irqchip_out;
		r = kvm_vm_ioctl_set_irqchip(kvm, chip);
	set_irqchip_out:
		kfree(chip);
		break;
	}
	case KVM_GET_PIT: {
		r = -EFAULT;
		if (copy_from_user(&u.ps, argp, sizeof(struct kvm_pit_state)))
			goto out;
		r = -ENXIO;
		if (!kvm->arch.vpit)
			goto out;
		r = kvm_vm_ioctl_get_pit(kvm, &u.ps);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &u.ps, sizeof(struct kvm_pit_state)))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_PIT: {
		r = -EFAULT;
		if (copy_from_user(&u.ps, argp, sizeof(u.ps)))
			goto out;
		mutex_lock(&kvm->lock);
		r = -ENXIO;
		if (!kvm->arch.vpit)
			goto set_pit_out;
		r = kvm_vm_ioctl_set_pit(kvm, &u.ps);
set_pit_out:
		mutex_unlock(&kvm->lock);
		break;
	}
	case KVM_GET_PIT2: {
		r = -ENXIO;
		if (!kvm->arch.vpit)
			goto out;
		r = kvm_vm_ioctl_get_pit2(kvm, &u.ps2);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &u.ps2, sizeof(u.ps2)))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_PIT2: {
		r = -EFAULT;
		if (copy_from_user(&u.ps2, argp, sizeof(u.ps2)))
			goto out;
		mutex_lock(&kvm->lock);
		r = -ENXIO;
		if (!kvm->arch.vpit)
			goto set_pit2_out;
		r = kvm_vm_ioctl_set_pit2(kvm, &u.ps2);
set_pit2_out:
		mutex_unlock(&kvm->lock);
		break;
	}
	case KVM_REINJECT_CONTROL: {
		struct kvm_reinject_control control;
		r =  -EFAULT;
		if (copy_from_user(&control, argp, sizeof(control)))
			goto out;
		r = -ENXIO;
		if (!kvm->arch.vpit)
			goto out;
		r = kvm_vm_ioctl_reinject(kvm, &control);
		break;
	}
	case KVM_SET_BOOT_CPU_ID:
		r = 0;
		mutex_lock(&kvm->lock);
		if (kvm->created_vcpus)
			r = -EBUSY;
		else if (arg > KVM_MAX_VCPU_IDS ||
			 (kvm->arch.max_vcpu_ids && arg > kvm->arch.max_vcpu_ids))
			r = -EINVAL;
		else
			kvm->arch.bsp_vcpu_id = arg;
		mutex_unlock(&kvm->lock);
		break;
#ifdef CONFIG_KVM_XEN
	case KVM_XEN_HVM_CONFIG: {
		struct kvm_xen_hvm_config xhc;
		r = -EFAULT;
		if (copy_from_user(&xhc, argp, sizeof(xhc)))
			goto out;
		r = kvm_xen_hvm_config(kvm, &xhc);
		break;
	}
	case KVM_XEN_HVM_GET_ATTR: {
		struct kvm_xen_hvm_attr xha;

		r = -EFAULT;
		if (copy_from_user(&xha, argp, sizeof(xha)))
			goto out;
		r = kvm_xen_hvm_get_attr(kvm, &xha);
		if (!r && copy_to_user(argp, &xha, sizeof(xha)))
			r = -EFAULT;
		break;
	}
	case KVM_XEN_HVM_SET_ATTR: {
		struct kvm_xen_hvm_attr xha;

		r = -EFAULT;
		if (copy_from_user(&xha, argp, sizeof(xha)))
			goto out;
		r = kvm_xen_hvm_set_attr(kvm, &xha);
		break;
	}
	case KVM_XEN_HVM_EVTCHN_SEND: {
		struct kvm_irq_routing_xen_evtchn uxe;

		r = -EFAULT;
		if (copy_from_user(&uxe, argp, sizeof(uxe)))
			goto out;
		r = kvm_xen_hvm_evtchn_send(kvm, &uxe);
		break;
	}
#endif
	case KVM_SET_CLOCK:
		r = kvm_vm_ioctl_set_clock(kvm, argp);
		break;
	case KVM_GET_CLOCK:
		r = kvm_vm_ioctl_get_clock(kvm, argp);
		break;
	case KVM_SET_TSC_KHZ: {
		u32 user_tsc_khz;

		r = -EINVAL;
		user_tsc_khz = (u32)arg;

		if (kvm_caps.has_tsc_control &&
		    user_tsc_khz >= kvm_caps.max_guest_tsc_khz)
			goto out;

		if (user_tsc_khz == 0)
			user_tsc_khz = tsc_khz;

		WRITE_ONCE(kvm->arch.default_tsc_khz, user_tsc_khz);
		r = 0;

		goto out;
	}
	case KVM_GET_TSC_KHZ: {
		r = READ_ONCE(kvm->arch.default_tsc_khz);
		goto out;
	}
	case KVM_MEMORY_ENCRYPT_OP: {
		r = -ENOTTY;
		if (!kvm_x86_ops.mem_enc_ioctl)
			goto out;

		r = kvm_x86_call(mem_enc_ioctl)(kvm, argp);
		break;
	}
	case KVM_MEMORY_ENCRYPT_REG_REGION: {
		struct kvm_enc_region region;

		r = -EFAULT;
		if (copy_from_user(&region, argp, sizeof(region)))
			goto out;

		r = -ENOTTY;
		if (!kvm_x86_ops.mem_enc_register_region)
			goto out;

		r = kvm_x86_call(mem_enc_register_region)(kvm, &region);
		break;
	}
	case KVM_MEMORY_ENCRYPT_UNREG_REGION: {
		struct kvm_enc_region region;

		r = -EFAULT;
		if (copy_from_user(&region, argp, sizeof(region)))
			goto out;

		r = -ENOTTY;
		if (!kvm_x86_ops.mem_enc_unregister_region)
			goto out;

		r = kvm_x86_call(mem_enc_unregister_region)(kvm, &region);
		break;
	}
#ifdef CONFIG_KVM_HYPERV
	case KVM_HYPERV_EVENTFD: {
		struct kvm_hyperv_eventfd hvevfd;

		r = -EFAULT;
		if (copy_from_user(&hvevfd, argp, sizeof(hvevfd)))
			goto out;
		r = kvm_vm_ioctl_hv_eventfd(kvm, &hvevfd);
		break;
	}
#endif
	case KVM_SET_PMU_EVENT_FILTER:
		r = kvm_vm_ioctl_set_pmu_event_filter(kvm, argp);
		break;
	case KVM_X86_SET_MSR_FILTER: {
		struct kvm_msr_filter __user *user_msr_filter = argp;
		struct kvm_msr_filter filter;

		if (copy_from_user(&filter, user_msr_filter, sizeof(filter)))
			return -EFAULT;

		r = kvm_vm_ioctl_set_msr_filter(kvm, &filter);
		break;
	}
	default:
		r = -ENOTTY;
	}
out:
	return r;
}

static void kvm_probe_feature_msr(u32 msr_index)
{
	u64 data;

	if (kvm_get_feature_msr(NULL, msr_index, &data, true))
		return;

	msr_based_features[num_msr_based_features++] = msr_index;
}

static void kvm_probe_msr_to_save(u32 msr_index)
{
	u32 dummy[2];

	if (rdmsr_safe(msr_index, &dummy[0], &dummy[1]))
		return;

	/*
	 * Even MSRs that are valid in the host may not be exposed to guests in
	 * some cases.
	 */
	switch (msr_index) {
	case MSR_IA32_BNDCFGS:
		if (!kvm_mpx_supported())
			return;
		break;
	case MSR_TSC_AUX:
		if (!kvm_cpu_cap_has(X86_FEATURE_RDTSCP) &&
		    !kvm_cpu_cap_has(X86_FEATURE_RDPID))
			return;
		break;
	case MSR_IA32_UMWAIT_CONTROL:
		if (!kvm_cpu_cap_has(X86_FEATURE_WAITPKG))
			return;
		break;
	case MSR_IA32_RTIT_CTL:
	case MSR_IA32_RTIT_STATUS:
		if (!kvm_cpu_cap_has(X86_FEATURE_INTEL_PT))
			return;
		break;
	case MSR_IA32_RTIT_CR3_MATCH:
		if (!kvm_cpu_cap_has(X86_FEATURE_INTEL_PT) ||
		    !intel_pt_validate_hw_cap(PT_CAP_cr3_filtering))
			return;
		break;
	case MSR_IA32_RTIT_OUTPUT_BASE:
	case MSR_IA32_RTIT_OUTPUT_MASK:
		if (!kvm_cpu_cap_has(X86_FEATURE_INTEL_PT) ||
		    (!intel_pt_validate_hw_cap(PT_CAP_topa_output) &&
		     !intel_pt_validate_hw_cap(PT_CAP_single_range_output)))
			return;
		break;
	case MSR_IA32_RTIT_ADDR0_A ... MSR_IA32_RTIT_ADDR3_B:
		if (!kvm_cpu_cap_has(X86_FEATURE_INTEL_PT) ||
		    (msr_index - MSR_IA32_RTIT_ADDR0_A >=
		     intel_pt_validate_hw_cap(PT_CAP_num_address_ranges) * 2))
			return;
		break;
	case MSR_ARCH_PERFMON_PERFCTR0 ...
	     MSR_ARCH_PERFMON_PERFCTR0 + KVM_MAX_NR_GP_COUNTERS - 1:
		if (msr_index - MSR_ARCH_PERFMON_PERFCTR0 >=
		    kvm_pmu_cap.num_counters_gp)
			return;
		break;
	case MSR_ARCH_PERFMON_EVENTSEL0 ...
	     MSR_ARCH_PERFMON_EVENTSEL0 + KVM_MAX_NR_GP_COUNTERS - 1:
		if (msr_index - MSR_ARCH_PERFMON_EVENTSEL0 >=
		    kvm_pmu_cap.num_counters_gp)
			return;
		break;
	case MSR_ARCH_PERFMON_FIXED_CTR0 ...
	     MSR_ARCH_PERFMON_FIXED_CTR0 + KVM_MAX_NR_FIXED_COUNTERS - 1:
		if (msr_index - MSR_ARCH_PERFMON_FIXED_CTR0 >=
		    kvm_pmu_cap.num_counters_fixed)
			return;
		break;
	case MSR_AMD64_PERF_CNTR_GLOBAL_CTL:
	case MSR_AMD64_PERF_CNTR_GLOBAL_STATUS:
	case MSR_AMD64_PERF_CNTR_GLOBAL_STATUS_CLR:
		if (!kvm_cpu_cap_has(X86_FEATURE_PERFMON_V2))
			return;
		break;
	case MSR_IA32_XFD:
	case MSR_IA32_XFD_ERR:
		if (!kvm_cpu_cap_has(X86_FEATURE_XFD))
			return;
		break;
	case MSR_IA32_TSX_CTRL:
		if (!(kvm_get_arch_capabilities() & ARCH_CAP_TSX_CTRL_MSR))
			return;
		break;
	default:
		break;
	}

	msrs_to_save[num_msrs_to_save++] = msr_index;
}

static void kvm_init_msr_lists(void)
{
	unsigned i;

	BUILD_BUG_ON_MSG(KVM_MAX_NR_FIXED_COUNTERS != 3,
			 "Please update the fixed PMCs in msrs_to_save_pmu[]");

	num_msrs_to_save = 0;
	num_emulated_msrs = 0;
	num_msr_based_features = 0;

	for (i = 0; i < ARRAY_SIZE(msrs_to_save_base); i++)
		kvm_probe_msr_to_save(msrs_to_save_base[i]);

	if (enable_pmu) {
		for (i = 0; i < ARRAY_SIZE(msrs_to_save_pmu); i++)
			kvm_probe_msr_to_save(msrs_to_save_pmu[i]);
	}

	for (i = 0; i < ARRAY_SIZE(emulated_msrs_all); i++) {
		if (!kvm_x86_call(has_emulated_msr)(NULL,
						    emulated_msrs_all[i]))
			continue;

		emulated_msrs[num_emulated_msrs++] = emulated_msrs_all[i];
	}

	for (i = KVM_FIRST_EMULATED_VMX_MSR; i <= KVM_LAST_EMULATED_VMX_MSR; i++)
		kvm_probe_feature_msr(i);

	for (i = 0; i < ARRAY_SIZE(msr_based_features_all_except_vmx); i++)
		kvm_probe_feature_msr(msr_based_features_all_except_vmx[i]);
}

static int vcpu_mmio_write(struct kvm_vcpu *vcpu, gpa_t addr, int len,
			   const void *v)
{
	int handled = 0;
	int n;

	do {
		n = min(len, 8);
		if (!(lapic_in_kernel(vcpu) &&
		      !kvm_iodevice_write(vcpu, &vcpu->arch.apic->dev, addr, n, v))
		    && kvm_io_bus_write(vcpu, KVM_MMIO_BUS, addr, n, v))
			break;
		handled += n;
		addr += n;
		len -= n;
		v += n;
	} while (len);

	return handled;
}

static int vcpu_mmio_read(struct kvm_vcpu *vcpu, gpa_t addr, int len, void *v)
{
	int handled = 0;
	int n;

	do {
		n = min(len, 8);
		if (!(lapic_in_kernel(vcpu) &&
		      !kvm_iodevice_read(vcpu, &vcpu->arch.apic->dev,
					 addr, n, v))
		    && kvm_io_bus_read(vcpu, KVM_MMIO_BUS, addr, n, v))
			break;
		trace_kvm_mmio(KVM_TRACE_MMIO_READ, n, addr, v);
		handled += n;
		addr += n;
		len -= n;
		v += n;
	} while (len);

	return handled;
}

void kvm_set_segment(struct kvm_vcpu *vcpu,
		     struct kvm_segment *var, int seg)
{
	kvm_x86_call(set_segment)(vcpu, var, seg);
}

void kvm_get_segment(struct kvm_vcpu *vcpu,
		     struct kvm_segment *var, int seg)
{
	kvm_x86_call(get_segment)(vcpu, var, seg);
}

gpa_t translate_nested_gpa(struct kvm_vcpu *vcpu, gpa_t gpa, u64 access,
			   struct x86_exception *exception)
{
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	gpa_t t_gpa;

	BUG_ON(!mmu_is_nested(vcpu));

	/* NPT walks are always user-walks */
	access |= PFERR_USER_MASK;
	t_gpa  = mmu->gva_to_gpa(vcpu, mmu, gpa, access, exception);

	return t_gpa;
}

gpa_t kvm_mmu_gva_to_gpa_read(struct kvm_vcpu *vcpu, gva_t gva,
			      struct x86_exception *exception)
{
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;

	u64 access = (kvm_x86_call(get_cpl)(vcpu) == 3) ? PFERR_USER_MASK : 0;
	return mmu->gva_to_gpa(vcpu, mmu, gva, access, exception);
}
EXPORT_SYMBOL_GPL(kvm_mmu_gva_to_gpa_read);

gpa_t kvm_mmu_gva_to_gpa_write(struct kvm_vcpu *vcpu, gva_t gva,
			       struct x86_exception *exception)
{
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;

	u64 access = (kvm_x86_call(get_cpl)(vcpu) == 3) ? PFERR_USER_MASK : 0;
	access |= PFERR_WRITE_MASK;
	return mmu->gva_to_gpa(vcpu, mmu, gva, access, exception);
}
EXPORT_SYMBOL_GPL(kvm_mmu_gva_to_gpa_write);

/* uses this to access any guest's mapped memory without checking CPL */
gpa_t kvm_mmu_gva_to_gpa_system(struct kvm_vcpu *vcpu, gva_t gva,
				struct x86_exception *exception)
{
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;

	return mmu->gva_to_gpa(vcpu, mmu, gva, 0, exception);
}

static int kvm_read_guest_virt_helper(gva_t addr, void *val, unsigned int bytes,
				      struct kvm_vcpu *vcpu, u64 access,
				      struct x86_exception *exception)
{
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;
	void *data = val;
	int r = X86EMUL_CONTINUE;

	while (bytes) {
		gpa_t gpa = mmu->gva_to_gpa(vcpu, mmu, addr, access, exception);
		unsigned offset = addr & (PAGE_SIZE-1);
		unsigned toread = min(bytes, (unsigned)PAGE_SIZE - offset);
		int ret;

		if (gpa == INVALID_GPA)
			return X86EMUL_PROPAGATE_FAULT;
		ret = kvm_vcpu_read_guest_page(vcpu, gpa >> PAGE_SHIFT, data,
					       offset, toread);
		if (ret < 0) {
			r = X86EMUL_IO_NEEDED;
			goto out;
		}

		bytes -= toread;
		data += toread;
		addr += toread;
	}
out:
	return r;
}

/* used for instruction fetching */
static int kvm_fetch_guest_virt(struct x86_emulate_ctxt *ctxt,
				gva_t addr, void *val, unsigned int bytes,
				struct x86_exception *exception)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;
	u64 access = (kvm_x86_call(get_cpl)(vcpu) == 3) ? PFERR_USER_MASK : 0;
	unsigned offset;
	int ret;

	/* Inline kvm_read_guest_virt_helper for speed.  */
	gpa_t gpa = mmu->gva_to_gpa(vcpu, mmu, addr, access|PFERR_FETCH_MASK,
				    exception);
	if (unlikely(gpa == INVALID_GPA))
		return X86EMUL_PROPAGATE_FAULT;

	offset = addr & (PAGE_SIZE-1);
	if (WARN_ON(offset + bytes > PAGE_SIZE))
		bytes = (unsigned)PAGE_SIZE - offset;
	ret = kvm_vcpu_read_guest_page(vcpu, gpa >> PAGE_SHIFT, val,
				       offset, bytes);
	if (unlikely(ret < 0))
		return X86EMUL_IO_NEEDED;

	return X86EMUL_CONTINUE;
}

int kvm_read_guest_virt(struct kvm_vcpu *vcpu,
			       gva_t addr, void *val, unsigned int bytes,
			       struct x86_exception *exception)
{
	u64 access = (kvm_x86_call(get_cpl)(vcpu) == 3) ? PFERR_USER_MASK : 0;

	/*
	 * FIXME: this should call handle_emulation_failure if X86EMUL_IO_NEEDED
	 * is returned, but our callers are not ready for that and they blindly
	 * call kvm_inject_page_fault.  Ensure that they at least do not leak
	 * uninitialized kernel stack memory into cr2 and error code.
	 */
	memset(exception, 0, sizeof(*exception));
	return kvm_read_guest_virt_helper(addr, val, bytes, vcpu, access,
					  exception);
}
EXPORT_SYMBOL_GPL(kvm_read_guest_virt);

static int emulator_read_std(struct x86_emulate_ctxt *ctxt,
			     gva_t addr, void *val, unsigned int bytes,
			     struct x86_exception *exception, bool system)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	u64 access = 0;

	if (system)
		access |= PFERR_IMPLICIT_ACCESS;
	else if (kvm_x86_call(get_cpl)(vcpu) == 3)
		access |= PFERR_USER_MASK;

	return kvm_read_guest_virt_helper(addr, val, bytes, vcpu, access, exception);
}

static int kvm_write_guest_virt_helper(gva_t addr, void *val, unsigned int bytes,
				      struct kvm_vcpu *vcpu, u64 access,
				      struct x86_exception *exception)
{
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;
	void *data = val;
	int r = X86EMUL_CONTINUE;

	while (bytes) {
		gpa_t gpa = mmu->gva_to_gpa(vcpu, mmu, addr, access, exception);
		unsigned offset = addr & (PAGE_SIZE-1);
		unsigned towrite = min(bytes, (unsigned)PAGE_SIZE - offset);
		int ret;

		if (gpa == INVALID_GPA)
			return X86EMUL_PROPAGATE_FAULT;
		ret = kvm_vcpu_write_guest(vcpu, gpa, data, towrite);
		if (ret < 0) {
			r = X86EMUL_IO_NEEDED;
			goto out;
		}

		bytes -= towrite;
		data += towrite;
		addr += towrite;
	}
out:
	return r;
}

static int emulator_write_std(struct x86_emulate_ctxt *ctxt, gva_t addr, void *val,
			      unsigned int bytes, struct x86_exception *exception,
			      bool system)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	u64 access = PFERR_WRITE_MASK;

	if (system)
		access |= PFERR_IMPLICIT_ACCESS;
	else if (kvm_x86_call(get_cpl)(vcpu) == 3)
		access |= PFERR_USER_MASK;

	return kvm_write_guest_virt_helper(addr, val, bytes, vcpu,
					   access, exception);
}

int kvm_write_guest_virt_system(struct kvm_vcpu *vcpu, gva_t addr, void *val,
				unsigned int bytes, struct x86_exception *exception)
{
	/* kvm_write_guest_virt_system can pull in tons of pages. */
	vcpu->arch.l1tf_flush_l1d = true;

	return kvm_write_guest_virt_helper(addr, val, bytes, vcpu,
					   PFERR_WRITE_MASK, exception);
}
EXPORT_SYMBOL_GPL(kvm_write_guest_virt_system);

static int kvm_check_emulate_insn(struct kvm_vcpu *vcpu, int emul_type,
				  void *insn, int insn_len)
{
	return kvm_x86_call(check_emulate_instruction)(vcpu, emul_type,
						       insn, insn_len);
}

int handle_ud(struct kvm_vcpu *vcpu)
{
	static const char kvm_emulate_prefix[] = { __KVM_EMULATE_PREFIX };
	int fep_flags = READ_ONCE(force_emulation_prefix);
	int emul_type = EMULTYPE_TRAP_UD;
	char sig[5]; /* ud2; .ascii "kvm" */
	struct x86_exception e;
	int r;

	r = kvm_check_emulate_insn(vcpu, emul_type, NULL, 0);
	if (r != X86EMUL_CONTINUE)
		return 1;

	if (fep_flags &&
	    kvm_read_guest_virt(vcpu, kvm_get_linear_rip(vcpu),
				sig, sizeof(sig), &e) == 0 &&
	    memcmp(sig, kvm_emulate_prefix, sizeof(sig)) == 0) {
		if (fep_flags & KVM_FEP_CLEAR_RFLAGS_RF)
			kvm_set_rflags(vcpu, kvm_get_rflags(vcpu) & ~X86_EFLAGS_RF);
		kvm_rip_write(vcpu, kvm_rip_read(vcpu) + sizeof(sig));
		emul_type = EMULTYPE_TRAP_UD_FORCED;
	}

	return kvm_emulate_instruction(vcpu, emul_type);
}
EXPORT_SYMBOL_GPL(handle_ud);

static int vcpu_is_mmio_gpa(struct kvm_vcpu *vcpu, unsigned long gva,
			    gpa_t gpa, bool write)
{
	/* For APIC access vmexit */
	if ((gpa & PAGE_MASK) == APIC_DEFAULT_PHYS_BASE)
		return 1;

	if (vcpu_match_mmio_gpa(vcpu, gpa)) {
		trace_vcpu_match_mmio(gva, gpa, write, true);
		return 1;
	}

	return 0;
}

static int vcpu_mmio_gva_to_gpa(struct kvm_vcpu *vcpu, unsigned long gva,
				gpa_t *gpa, struct x86_exception *exception,
				bool write)
{
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;
	u64 access = ((kvm_x86_call(get_cpl)(vcpu) == 3) ? PFERR_USER_MASK : 0)
		     | (write ? PFERR_WRITE_MASK : 0);

	/*
	 * currently PKRU is only applied to ept enabled guest so
	 * there is no pkey in EPT page table for L1 guest or EPT
	 * shadow page table for L2 guest.
	 */
	if (vcpu_match_mmio_gva(vcpu, gva) && (!is_paging(vcpu) ||
	    !permission_fault(vcpu, vcpu->arch.walk_mmu,
			      vcpu->arch.mmio_access, 0, access))) {
		*gpa = vcpu->arch.mmio_gfn << PAGE_SHIFT |
					(gva & (PAGE_SIZE - 1));
		trace_vcpu_match_mmio(gva, *gpa, write, false);
		return 1;
	}

	*gpa = mmu->gva_to_gpa(vcpu, mmu, gva, access, exception);

	if (*gpa == INVALID_GPA)
		return -1;

	return vcpu_is_mmio_gpa(vcpu, gva, *gpa, write);
}

int emulator_write_phys(struct kvm_vcpu *vcpu, gpa_t gpa,
			const void *val, int bytes)
{
	int ret;

	ret = kvm_vcpu_write_guest(vcpu, gpa, val, bytes);
	if (ret < 0)
		return 0;
	kvm_page_track_write(vcpu, gpa, val, bytes);
	return 1;
}

struct read_write_emulator_ops {
	int (*read_write_prepare)(struct kvm_vcpu *vcpu, void *val,
				  int bytes);
	int (*read_write_emulate)(struct kvm_vcpu *vcpu, gpa_t gpa,
				  void *val, int bytes);
	int (*read_write_mmio)(struct kvm_vcpu *vcpu, gpa_t gpa,
			       int bytes, void *val);
	int (*read_write_exit_mmio)(struct kvm_vcpu *vcpu, gpa_t gpa,
				    void *val, int bytes);
	bool write;
};

static int read_prepare(struct kvm_vcpu *vcpu, void *val, int bytes)
{
	if (vcpu->mmio_read_completed) {
		trace_kvm_mmio(KVM_TRACE_MMIO_READ, bytes,
			       vcpu->mmio_fragments[0].gpa, val);
		vcpu->mmio_read_completed = 0;
		return 1;
	}

	return 0;
}

static int read_emulate(struct kvm_vcpu *vcpu, gpa_t gpa,
			void *val, int bytes)
{
	return !kvm_vcpu_read_guest(vcpu, gpa, val, bytes);
}

static int write_emulate(struct kvm_vcpu *vcpu, gpa_t gpa,
			 void *val, int bytes)
{
	return emulator_write_phys(vcpu, gpa, val, bytes);
}

static int write_mmio(struct kvm_vcpu *vcpu, gpa_t gpa, int bytes, void *val)
{
	trace_kvm_mmio(KVM_TRACE_MMIO_WRITE, bytes, gpa, val);
	return vcpu_mmio_write(vcpu, gpa, bytes, val);
}

static int read_exit_mmio(struct kvm_vcpu *vcpu, gpa_t gpa,
			  void *val, int bytes)
{
	trace_kvm_mmio(KVM_TRACE_MMIO_READ_UNSATISFIED, bytes, gpa, NULL);
	return X86EMUL_IO_NEEDED;
}

static int write_exit_mmio(struct kvm_vcpu *vcpu, gpa_t gpa,
			   void *val, int bytes)
{
	struct kvm_mmio_fragment *frag = &vcpu->mmio_fragments[0];

	memcpy(vcpu->run->mmio.data, frag->data, min(8u, frag->len));
	return X86EMUL_CONTINUE;
}

static const struct read_write_emulator_ops read_emultor = {
	.read_write_prepare = read_prepare,
	.read_write_emulate = read_emulate,
	.read_write_mmio = vcpu_mmio_read,
	.read_write_exit_mmio = read_exit_mmio,
};

static const struct read_write_emulator_ops write_emultor = {
	.read_write_emulate = write_emulate,
	.read_write_mmio = write_mmio,
	.read_write_exit_mmio = write_exit_mmio,
	.write = true,
};

static int emulator_read_write_onepage(unsigned long addr, void *val,
				       unsigned int bytes,
				       struct x86_exception *exception,
				       struct kvm_vcpu *vcpu,
				       const struct read_write_emulator_ops *ops)
{
	gpa_t gpa;
	int handled, ret;
	bool write = ops->write;
	struct kvm_mmio_fragment *frag;
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;

	/*
	 * If the exit was due to a NPF we may already have a GPA.
	 * If the GPA is present, use it to avoid the GVA to GPA table walk.
	 * Note, this cannot be used on string operations since string
	 * operation using rep will only have the initial GPA from the NPF
	 * occurred.
	 */
	if (ctxt->gpa_available && emulator_can_use_gpa(ctxt) &&
	    (addr & ~PAGE_MASK) == (ctxt->gpa_val & ~PAGE_MASK)) {
		gpa = ctxt->gpa_val;
		ret = vcpu_is_mmio_gpa(vcpu, addr, gpa, write);
	} else {
		ret = vcpu_mmio_gva_to_gpa(vcpu, addr, &gpa, exception, write);
		if (ret < 0)
			return X86EMUL_PROPAGATE_FAULT;
	}

	if (!ret && ops->read_write_emulate(vcpu, gpa, val, bytes))
		return X86EMUL_CONTINUE;

	/*
	 * Is this MMIO handled locally?
	 */
	handled = ops->read_write_mmio(vcpu, gpa, bytes, val);
	if (handled == bytes)
		return X86EMUL_CONTINUE;

	gpa += handled;
	bytes -= handled;
	val += handled;

	WARN_ON(vcpu->mmio_nr_fragments >= KVM_MAX_MMIO_FRAGMENTS);
	frag = &vcpu->mmio_fragments[vcpu->mmio_nr_fragments++];
	frag->gpa = gpa;
	frag->data = val;
	frag->len = bytes;
	return X86EMUL_CONTINUE;
}

static int emulator_read_write(struct x86_emulate_ctxt *ctxt,
			unsigned long addr,
			void *val, unsigned int bytes,
			struct x86_exception *exception,
			const struct read_write_emulator_ops *ops)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	gpa_t gpa;
	int rc;

	if (ops->read_write_prepare &&
		  ops->read_write_prepare(vcpu, val, bytes))
		return X86EMUL_CONTINUE;

	vcpu->mmio_nr_fragments = 0;

	/* Crossing a page boundary? */
	if (((addr + bytes - 1) ^ addr) & PAGE_MASK) {
		int now;

		now = -addr & ~PAGE_MASK;
		rc = emulator_read_write_onepage(addr, val, now, exception,
						 vcpu, ops);

		if (rc != X86EMUL_CONTINUE)
			return rc;
		addr += now;
		if (ctxt->mode != X86EMUL_MODE_PROT64)
			addr = (u32)addr;
		val += now;
		bytes -= now;
	}

	rc = emulator_read_write_onepage(addr, val, bytes, exception,
					 vcpu, ops);
	if (rc != X86EMUL_CONTINUE)
		return rc;

	if (!vcpu->mmio_nr_fragments)
		return rc;

	gpa = vcpu->mmio_fragments[0].gpa;

	vcpu->mmio_needed = 1;
	vcpu->mmio_cur_fragment = 0;

	vcpu->run->mmio.len = min(8u, vcpu->mmio_fragments[0].len);
	vcpu->run->mmio.is_write = vcpu->mmio_is_write = ops->write;
	vcpu->run->exit_reason = KVM_EXIT_MMIO;
	vcpu->run->mmio.phys_addr = gpa;

	return ops->read_write_exit_mmio(vcpu, gpa, val, bytes);
}

static int emulator_read_emulated(struct x86_emulate_ctxt *ctxt,
				  unsigned long addr,
				  void *val,
				  unsigned int bytes,
				  struct x86_exception *exception)
{
	return emulator_read_write(ctxt, addr, val, bytes,
				   exception, &read_emultor);
}

static int emulator_write_emulated(struct x86_emulate_ctxt *ctxt,
			    unsigned long addr,
			    const void *val,
			    unsigned int bytes,
			    struct x86_exception *exception)
{
	return emulator_read_write(ctxt, addr, (void *)val, bytes,
				   exception, &write_emultor);
}

#define emulator_try_cmpxchg_user(t, ptr, old, new) \
	(__try_cmpxchg_user((t __user *)(ptr), (t *)(old), *(t *)(new), efault ## t))

static int emulator_cmpxchg_emulated(struct x86_emulate_ctxt *ctxt,
				     unsigned long addr,
				     const void *old,
				     const void *new,
				     unsigned int bytes,
				     struct x86_exception *exception)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	u64 page_line_mask;
	unsigned long hva;
	gpa_t gpa;
	int r;

	/* guests cmpxchg8b have to be emulated atomically */
	if (bytes > 8 || (bytes & (bytes - 1)))
		goto emul_write;

	gpa = kvm_mmu_gva_to_gpa_write(vcpu, addr, NULL);

	if (gpa == INVALID_GPA ||
	    (gpa & PAGE_MASK) == APIC_DEFAULT_PHYS_BASE)
		goto emul_write;

	/*
	 * Emulate the atomic as a straight write to avoid #AC if SLD is
	 * enabled in the host and the access splits a cache line.
	 */
	if (boot_cpu_has(X86_FEATURE_SPLIT_LOCK_DETECT))
		page_line_mask = ~(cache_line_size() - 1);
	else
		page_line_mask = PAGE_MASK;

	if (((gpa + bytes - 1) & page_line_mask) != (gpa & page_line_mask))
		goto emul_write;

	hva = kvm_vcpu_gfn_to_hva(vcpu, gpa_to_gfn(gpa));
	if (kvm_is_error_hva(hva))
		goto emul_write;

	hva += offset_in_page(gpa);

	switch (bytes) {
	case 1:
		r = emulator_try_cmpxchg_user(u8, hva, old, new);
		break;
	case 2:
		r = emulator_try_cmpxchg_user(u16, hva, old, new);
		break;
	case 4:
		r = emulator_try_cmpxchg_user(u32, hva, old, new);
		break;
	case 8:
		r = emulator_try_cmpxchg_user(u64, hva, old, new);
		break;
	default:
		BUG();
	}

	if (r < 0)
		return X86EMUL_UNHANDLEABLE;

	/*
	 * Mark the page dirty _before_ checking whether or not the CMPXCHG was
	 * successful, as the old value is written back on failure.  Note, for
	 * live migration, this is unnecessarily conservative as CMPXCHG writes
	 * back the original value and the access is atomic, but KVM's ABI is
	 * that all writes are dirty logged, regardless of the value written.
	 */
	kvm_vcpu_mark_page_dirty(vcpu, gpa_to_gfn(gpa));

	if (r)
		return X86EMUL_CMPXCHG_FAILED;

	kvm_page_track_write(vcpu, gpa, new, bytes);

	return X86EMUL_CONTINUE;

emul_write:
	pr_warn_once("emulating exchange as write\n");

	return emulator_write_emulated(ctxt, addr, new, bytes, exception);
}

static int emulator_pio_in_out(struct kvm_vcpu *vcpu, int size,
			       unsigned short port, void *data,
			       unsigned int count, bool in)
{
	unsigned i;
	int r;

	WARN_ON_ONCE(vcpu->arch.pio.count);
	for (i = 0; i < count; i++) {
		if (in)
			r = kvm_io_bus_read(vcpu, KVM_PIO_BUS, port, size, data);
		else
			r = kvm_io_bus_write(vcpu, KVM_PIO_BUS, port, size, data);

		if (r) {
			if (i == 0)
				goto userspace_io;

			/*
			 * Userspace must have unregistered the device while PIO
			 * was running.  Drop writes / read as 0.
			 */
			if (in)
				memset(data, 0, size * (count - i));
			break;
		}

		data += size;
	}
	return 1;

userspace_io:
	vcpu->arch.pio.port = port;
	vcpu->arch.pio.in = in;
	vcpu->arch.pio.count = count;
	vcpu->arch.pio.size = size;

	if (in)
		memset(vcpu->arch.pio_data, 0, size * count);
	else
		memcpy(vcpu->arch.pio_data, data, size * count);

	vcpu->run->exit_reason = KVM_EXIT_IO;
	vcpu->run->io.direction = in ? KVM_EXIT_IO_IN : KVM_EXIT_IO_OUT;
	vcpu->run->io.size = size;
	vcpu->run->io.data_offset = KVM_PIO_PAGE_OFFSET * PAGE_SIZE;
	vcpu->run->io.count = count;
	vcpu->run->io.port = port;
	return 0;
}

static int emulator_pio_in(struct kvm_vcpu *vcpu, int size,
      			   unsigned short port, void *val, unsigned int count)
{
	int r = emulator_pio_in_out(vcpu, size, port, val, count, true);
	if (r)
		trace_kvm_pio(KVM_PIO_IN, port, size, count, val);

	return r;
}

static void complete_emulator_pio_in(struct kvm_vcpu *vcpu, void *val)
{
	int size = vcpu->arch.pio.size;
	unsigned int count = vcpu->arch.pio.count;
	memcpy(val, vcpu->arch.pio_data, size * count);
	trace_kvm_pio(KVM_PIO_IN, vcpu->arch.pio.port, size, count, vcpu->arch.pio_data);
	vcpu->arch.pio.count = 0;
}

static int emulator_pio_in_emulated(struct x86_emulate_ctxt *ctxt,
				    int size, unsigned short port, void *val,
				    unsigned int count)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	if (vcpu->arch.pio.count) {
		/*
		 * Complete a previous iteration that required userspace I/O.
		 * Note, @count isn't guaranteed to match pio.count as userspace
		 * can modify ECX before rerunning the vCPU.  Ignore any such
		 * shenanigans as KVM doesn't support modifying the rep count,
		 * and the emulator ensures @count doesn't overflow the buffer.
		 */
		complete_emulator_pio_in(vcpu, val);
		return 1;
	}

	return emulator_pio_in(vcpu, size, port, val, count);
}

static int emulator_pio_out(struct kvm_vcpu *vcpu, int size,
			    unsigned short port, const void *val,
			    unsigned int count)
{
	trace_kvm_pio(KVM_PIO_OUT, port, size, count, val);
	return emulator_pio_in_out(vcpu, size, port, (void *)val, count, false);
}

static int emulator_pio_out_emulated(struct x86_emulate_ctxt *ctxt,
				     int size, unsigned short port,
				     const void *val, unsigned int count)
{
	return emulator_pio_out(emul_to_vcpu(ctxt), size, port, val, count);
}

static unsigned long get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	return kvm_x86_call(get_segment_base)(vcpu, seg);
}

static void emulator_invlpg(struct x86_emulate_ctxt *ctxt, ulong address)
{
	kvm_mmu_invlpg(emul_to_vcpu(ctxt), address);
}

static int kvm_emulate_wbinvd_noskip(struct kvm_vcpu *vcpu)
{
	if (!need_emulate_wbinvd(vcpu))
		return X86EMUL_CONTINUE;

	if (kvm_x86_call(has_wbinvd_exit)()) {
		int cpu = get_cpu();

		cpumask_set_cpu(cpu, vcpu->arch.wbinvd_dirty_mask);
		on_each_cpu_mask(vcpu->arch.wbinvd_dirty_mask,
				wbinvd_ipi, NULL, 1);
		put_cpu();
		cpumask_clear(vcpu->arch.wbinvd_dirty_mask);
	} else
		wbinvd();
	return X86EMUL_CONTINUE;
}

int kvm_emulate_wbinvd(struct kvm_vcpu *vcpu)
{
	kvm_emulate_wbinvd_noskip(vcpu);
	return kvm_skip_emulated_instruction(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_emulate_wbinvd);



static void emulator_wbinvd(struct x86_emulate_ctxt *ctxt)
{
	kvm_emulate_wbinvd_noskip(emul_to_vcpu(ctxt));
}

static unsigned long emulator_get_dr(struct x86_emulate_ctxt *ctxt, int dr)
{
	return kvm_get_dr(emul_to_vcpu(ctxt), dr);
}

static int emulator_set_dr(struct x86_emulate_ctxt *ctxt, int dr,
			   unsigned long value)
{

	return kvm_set_dr(emul_to_vcpu(ctxt), dr, value);
}

static u64 mk_cr_64(u64 curr_cr, u32 new_val)
{
	return (curr_cr & ~((1ULL << 32) - 1)) | new_val;
}

static unsigned long emulator_get_cr(struct x86_emulate_ctxt *ctxt, int cr)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	unsigned long value;

	switch (cr) {
	case 0:
		value = kvm_read_cr0(vcpu);
		break;
	case 2:
		value = vcpu->arch.cr2;
		break;
	case 3:
		value = kvm_read_cr3(vcpu);
		break;
	case 4:
		value = kvm_read_cr4(vcpu);
		break;
	case 8:
		value = kvm_get_cr8(vcpu);
		break;
	default:
		kvm_err("%s: unexpected cr %u\n", __func__, cr);
		return 0;
	}

	return value;
}

static int emulator_set_cr(struct x86_emulate_ctxt *ctxt, int cr, ulong val)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	int res = 0;

	switch (cr) {
	case 0:
		res = kvm_set_cr0(vcpu, mk_cr_64(kvm_read_cr0(vcpu), val));
		break;
	case 2:
		vcpu->arch.cr2 = val;
		break;
	case 3:
		res = kvm_set_cr3(vcpu, val);
		break;
	case 4:
		res = kvm_set_cr4(vcpu, mk_cr_64(kvm_read_cr4(vcpu), val));
		break;
	case 8:
		res = kvm_set_cr8(vcpu, val);
		break;
	default:
		kvm_err("%s: unexpected cr %u\n", __func__, cr);
		res = -1;
	}

	return res;
}

static int emulator_get_cpl(struct x86_emulate_ctxt *ctxt)
{
	return kvm_x86_call(get_cpl)(emul_to_vcpu(ctxt));
}

static void emulator_get_gdt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_call(get_gdt)(emul_to_vcpu(ctxt), dt);
}

static void emulator_get_idt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_call(get_idt)(emul_to_vcpu(ctxt), dt);
}

static void emulator_set_gdt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_call(set_gdt)(emul_to_vcpu(ctxt), dt);
}

static void emulator_set_idt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_call(set_idt)(emul_to_vcpu(ctxt), dt);
}

static unsigned long emulator_get_cached_segment_base(
	struct x86_emulate_ctxt *ctxt, int seg)
{
	return get_segment_base(emul_to_vcpu(ctxt), seg);
}

static bool emulator_get_segment(struct x86_emulate_ctxt *ctxt, u16 *selector,
				 struct desc_struct *desc, u32 *base3,
				 int seg)
{
	struct kvm_segment var;

	kvm_get_segment(emul_to_vcpu(ctxt), &var, seg);
	*selector = var.selector;

	if (var.unusable) {
		memset(desc, 0, sizeof(*desc));
		if (base3)
			*base3 = 0;
		return false;
	}

	if (var.g)
		var.limit >>= 12;
	set_desc_limit(desc, var.limit);
	set_desc_base(desc, (unsigned long)var.base);
#ifdef CONFIG_X86_64
	if (base3)
		*base3 = var.base >> 32;
#endif
	desc->type = var.type;
	desc->s = var.s;
	desc->dpl = var.dpl;
	desc->p = var.present;
	desc->avl = var.avl;
	desc->l = var.l;
	desc->d = var.db;
	desc->g = var.g;

	return true;
}

static void emulator_set_segment(struct x86_emulate_ctxt *ctxt, u16 selector,
				 struct desc_struct *desc, u32 base3,
				 int seg)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	struct kvm_segment var;

	var.selector = selector;
	var.base = get_desc_base(desc);
#ifdef CONFIG_X86_64
	var.base |= ((u64)base3) << 32;
#endif
	var.limit = get_desc_limit(desc);
	if (desc->g)
		var.limit = (var.limit << 12) | 0xfff;
	var.type = desc->type;
	var.dpl = desc->dpl;
	var.db = desc->d;
	var.s = desc->s;
	var.l = desc->l;
	var.g = desc->g;
	var.avl = desc->avl;
	var.present = desc->p;
	var.unusable = !var.present;
	var.padding = 0;

	kvm_set_segment(vcpu, &var, seg);
	return;
}

static int emulator_get_msr_with_filter(struct x86_emulate_ctxt *ctxt,
					u32 msr_index, u64 *pdata)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	int r;

	r = kvm_get_msr_with_filter(vcpu, msr_index, pdata);
	if (r < 0)
		return X86EMUL_UNHANDLEABLE;

	if (r) {
		if (kvm_msr_user_space(vcpu, msr_index, KVM_EXIT_X86_RDMSR, 0,
				       complete_emulated_rdmsr, r))
			return X86EMUL_IO_NEEDED;

		trace_kvm_msr_read_ex(msr_index);
		return X86EMUL_PROPAGATE_FAULT;
	}

	trace_kvm_msr_read(msr_index, *pdata);
	return X86EMUL_CONTINUE;
}

static int emulator_set_msr_with_filter(struct x86_emulate_ctxt *ctxt,
					u32 msr_index, u64 data)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	int r;

	r = kvm_set_msr_with_filter(vcpu, msr_index, data);
	if (r < 0)
		return X86EMUL_UNHANDLEABLE;

	if (r) {
		if (kvm_msr_user_space(vcpu, msr_index, KVM_EXIT_X86_WRMSR, data,
				       complete_emulated_msr_access, r))
			return X86EMUL_IO_NEEDED;

		trace_kvm_msr_write_ex(msr_index, data);
		return X86EMUL_PROPAGATE_FAULT;
	}

	trace_kvm_msr_write(msr_index, data);
	return X86EMUL_CONTINUE;
}

static int emulator_get_msr(struct x86_emulate_ctxt *ctxt,
			    u32 msr_index, u64 *pdata)
{
	return kvm_get_msr(emul_to_vcpu(ctxt), msr_index, pdata);
}

static int emulator_check_rdpmc_early(struct x86_emulate_ctxt *ctxt, u32 pmc)
{
	return kvm_pmu_check_rdpmc_early(emul_to_vcpu(ctxt), pmc);
}

static int emulator_read_pmc(struct x86_emulate_ctxt *ctxt,
			     u32 pmc, u64 *pdata)
{
	return kvm_pmu_rdpmc(emul_to_vcpu(ctxt), pmc, pdata);
}

static void emulator_halt(struct x86_emulate_ctxt *ctxt)
{
	emul_to_vcpu(ctxt)->arch.halt_request = 1;
}

static int emulator_intercept(struct x86_emulate_ctxt *ctxt,
			      struct x86_instruction_info *info,
			      enum x86_intercept_stage stage)
{
	return kvm_x86_call(check_intercept)(emul_to_vcpu(ctxt), info, stage,
					     &ctxt->exception);
}

static bool emulator_get_cpuid(struct x86_emulate_ctxt *ctxt,
			      u32 *eax, u32 *ebx, u32 *ecx, u32 *edx,
			      bool exact_only)
{
	return kvm_cpuid(emul_to_vcpu(ctxt), eax, ebx, ecx, edx, exact_only);
}

static bool emulator_guest_has_movbe(struct x86_emulate_ctxt *ctxt)
{
	return guest_cpuid_has(emul_to_vcpu(ctxt), X86_FEATURE_MOVBE);
}

static bool emulator_guest_has_fxsr(struct x86_emulate_ctxt *ctxt)
{
	return guest_cpuid_has(emul_to_vcpu(ctxt), X86_FEATURE_FXSR);
}

static bool emulator_guest_has_rdpid(struct x86_emulate_ctxt *ctxt)
{
	return guest_cpuid_has(emul_to_vcpu(ctxt), X86_FEATURE_RDPID);
}

static bool emulator_guest_cpuid_is_intel_compatible(struct x86_emulate_ctxt *ctxt)
{
	return guest_cpuid_is_intel_compatible(emul_to_vcpu(ctxt));
}

static ulong emulator_read_gpr(struct x86_emulate_ctxt *ctxt, unsigned reg)
{
	return kvm_register_read_raw(emul_to_vcpu(ctxt), reg);
}

static void emulator_write_gpr(struct x86_emulate_ctxt *ctxt, unsigned reg, ulong val)
{
	kvm_register_write_raw(emul_to_vcpu(ctxt), reg, val);
}

static void emulator_set_nmi_mask(struct x86_emulate_ctxt *ctxt, bool masked)
{
	kvm_x86_call(set_nmi_mask)(emul_to_vcpu(ctxt), masked);
}

static bool emulator_is_smm(struct x86_emulate_ctxt *ctxt)
{
	return is_smm(emul_to_vcpu(ctxt));
}

static bool emulator_is_guest_mode(struct x86_emulate_ctxt *ctxt)
{
	return is_guest_mode(emul_to_vcpu(ctxt));
}

#ifndef CONFIG_KVM_SMM
static int emulator_leave_smm(struct x86_emulate_ctxt *ctxt)
{
	WARN_ON_ONCE(1);
	return X86EMUL_UNHANDLEABLE;
}
#endif

static void emulator_triple_fault(struct x86_emulate_ctxt *ctxt)
{
	kvm_make_request(KVM_REQ_TRIPLE_FAULT, emul_to_vcpu(ctxt));
}

static int emulator_set_xcr(struct x86_emulate_ctxt *ctxt, u32 index, u64 xcr)
{
	return __kvm_set_xcr(emul_to_vcpu(ctxt), index, xcr);
}

static void emulator_vm_bugged(struct x86_emulate_ctxt *ctxt)
{
	struct kvm *kvm = emul_to_vcpu(ctxt)->kvm;

	if (!kvm->vm_bugged)
		kvm_vm_bugged(kvm);
}

static gva_t emulator_get_untagged_addr(struct x86_emulate_ctxt *ctxt,
					gva_t addr, unsigned int flags)
{
	if (!kvm_x86_ops.get_untagged_addr)
		return addr;

	return kvm_x86_call(get_untagged_addr)(emul_to_vcpu(ctxt),
					       addr, flags);
}

static bool emulator_is_canonical_addr(struct x86_emulate_ctxt *ctxt,
				       gva_t addr, unsigned int flags)
{
	return !is_noncanonical_address(addr, emul_to_vcpu(ctxt), flags);
}

static const struct x86_emulate_ops emulate_ops = {
	.vm_bugged           = emulator_vm_bugged,
	.read_gpr            = emulator_read_gpr,
	.write_gpr           = emulator_write_gpr,
	.read_std            = emulator_read_std,
	.write_std           = emulator_write_std,
	.fetch               = kvm_fetch_guest_virt,
	.read_emulated       = emulator_read_emulated,
	.write_emulated      = emulator_write_emulated,
	.cmpxchg_emulated    = emulator_cmpxchg_emulated,
	.invlpg              = emulator_invlpg,
	.pio_in_emulated     = emulator_pio_in_emulated,
	.pio_out_emulated    = emulator_pio_out_emulated,
	.get_segment         = emulator_get_segment,
	.set_segment         = emulator_set_segment,
	.get_cached_segment_base = emulator_get_cached_segment_base,
	.get_gdt             = emulator_get_gdt,
	.get_idt	     = emulator_get_idt,
	.set_gdt             = emulator_set_gdt,
	.set_idt	     = emulator_set_idt,
	.get_cr              = emulator_get_cr,
	.set_cr              = emulator_set_cr,
	.cpl                 = emulator_get_cpl,
	.get_dr              = emulator_get_dr,
	.set_dr              = emulator_set_dr,
	.set_msr_with_filter = emulator_set_msr_with_filter,
	.get_msr_with_filter = emulator_get_msr_with_filter,
	.get_msr             = emulator_get_msr,
	.check_rdpmc_early   = emulator_check_rdpmc_early,
	.read_pmc            = emulator_read_pmc,
	.halt                = emulator_halt,
	.wbinvd              = emulator_wbinvd,
	.fix_hypercall       = emulator_fix_hypercall,
	.intercept           = emulator_intercept,
	.get_cpuid           = emulator_get_cpuid,
	.guest_has_movbe     = emulator_guest_has_movbe,
	.guest_has_fxsr      = emulator_guest_has_fxsr,
	.guest_has_rdpid     = emulator_guest_has_rdpid,
	.guest_cpuid_is_intel_compatible = emulator_guest_cpuid_is_intel_compatible,
	.set_nmi_mask        = emulator_set_nmi_mask,
	.is_smm              = emulator_is_smm,
	.is_guest_mode       = emulator_is_guest_mode,
	.leave_smm           = emulator_leave_smm,
	.triple_fault        = emulator_triple_fault,
	.set_xcr             = emulator_set_xcr,
	.get_untagged_addr   = emulator_get_untagged_addr,
	.is_canonical_addr   = emulator_is_canonical_addr,
};

static void toggle_interruptibility(struct kvm_vcpu *vcpu, u32 mask)
{
	u32 int_shadow = kvm_x86_call(get_interrupt_shadow)(vcpu);
	/*
	 * an sti; sti; sequence only disable interrupts for the first
	 * instruction. So, if the last instruction, be it emulated or
	 * not, left the system with the INT_STI flag enabled, it
	 * means that the last instruction is an sti. We should not
	 * leave the flag on in this case. The same goes for mov ss
	 */
	if (int_shadow & mask)
		mask = 0;
	if (unlikely(int_shadow || mask)) {
		kvm_x86_call(set_interrupt_shadow)(vcpu, mask);
		if (!mask)
			kvm_make_request(KVM_REQ_EVENT, vcpu);
	}
}

static void inject_emulated_exception(struct kvm_vcpu *vcpu)
{
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;

	if (ctxt->exception.vector == PF_VECTOR)
		kvm_inject_emulated_page_fault(vcpu, &ctxt->exception);
	else if (ctxt->exception.error_code_valid)
		kvm_queue_exception_e(vcpu, ctxt->exception.vector,
				      ctxt->exception.error_code);
	else
		kvm_queue_exception(vcpu, ctxt->exception.vector);
}

static struct x86_emulate_ctxt *alloc_emulate_ctxt(struct kvm_vcpu *vcpu)
{
	struct x86_emulate_ctxt *ctxt;

	ctxt = kmem_cache_zalloc(x86_emulator_cache, GFP_KERNEL_ACCOUNT);
	if (!ctxt) {
		pr_err("failed to allocate vcpu's emulator\n");
		return NULL;
	}

	ctxt->vcpu = vcpu;
	ctxt->ops = &emulate_ops;
	vcpu->arch.emulate_ctxt = ctxt;

	return ctxt;
}

static void init_emulate_ctxt(struct kvm_vcpu *vcpu)
{
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;
	int cs_db, cs_l;

	kvm_x86_call(get_cs_db_l_bits)(vcpu, &cs_db, &cs_l);

	ctxt->gpa_available = false;
	ctxt->eflags = kvm_get_rflags(vcpu);
	ctxt->tf = (ctxt->eflags & X86_EFLAGS_TF) != 0;

	ctxt->eip = kvm_rip_read(vcpu);
	ctxt->mode = (!is_protmode(vcpu))		? X86EMUL_MODE_REAL :
		     (ctxt->eflags & X86_EFLAGS_VM)	? X86EMUL_MODE_VM86 :
		     (cs_l && is_long_mode(vcpu))	? X86EMUL_MODE_PROT64 :
		     cs_db				? X86EMUL_MODE_PROT32 :
							  X86EMUL_MODE_PROT16;
	ctxt->interruptibility = 0;
	ctxt->have_exception = false;
	ctxt->exception.vector = -1;
	ctxt->perm_ok = false;

	init_decode_cache(ctxt);
	vcpu->arch.emulate_regs_need_sync_from_vcpu = false;
}

void kvm_inject_realmode_interrupt(struct kvm_vcpu *vcpu, int irq, int inc_eip)
{
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;
	int ret;

	init_emulate_ctxt(vcpu);

	ctxt->op_bytes = 2;
	ctxt->ad_bytes = 2;
	ctxt->_eip = ctxt->eip + inc_eip;
	ret = emulate_int_real(ctxt, irq);

	if (ret != X86EMUL_CONTINUE) {
		kvm_make_request(KVM_REQ_TRIPLE_FAULT, vcpu);
	} else {
		ctxt->eip = ctxt->_eip;
		kvm_rip_write(vcpu, ctxt->eip);
		kvm_set_rflags(vcpu, ctxt->eflags);
	}
}
EXPORT_SYMBOL_GPL(kvm_inject_realmode_interrupt);

static void prepare_emulation_failure_exit(struct kvm_vcpu *vcpu, u64 *data,
					   u8 ndata, u8 *insn_bytes, u8 insn_size)
{
	struct kvm_run *run = vcpu->run;
	u64 info[5];
	u8 info_start;

	/*
	 * Zero the whole array used to retrieve the exit info, as casting to
	 * u32 for select entries will leave some chunks uninitialized.
	 */
	memset(&info, 0, sizeof(info));

	kvm_x86_call(get_exit_info)(vcpu, (u32 *)&info[0], &info[1], &info[2],
				    (u32 *)&info[3], (u32 *)&info[4]);

	run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
	run->emulation_failure.suberror = KVM_INTERNAL_ERROR_EMULATION;

	/*
	 * There's currently space for 13 entries, but 5 are used for the exit
	 * reason and info.  Restrict to 4 to reduce the maintenance burden
	 * when expanding kvm_run.emulation_failure in the future.
	 */
	if (WARN_ON_ONCE(ndata > 4))
		ndata = 4;

	/* Always include the flags as a 'data' entry. */
	info_start = 1;
	run->emulation_failure.flags = 0;

	if (insn_size) {
		BUILD_BUG_ON((sizeof(run->emulation_failure.insn_size) +
			      sizeof(run->emulation_failure.insn_bytes) != 16));
		info_start += 2;
		run->emulation_failure.flags |=
			KVM_INTERNAL_ERROR_EMULATION_FLAG_INSTRUCTION_BYTES;
		run->emulation_failure.insn_size = insn_size;
		memset(run->emulation_failure.insn_bytes, 0x90,
		       sizeof(run->emulation_failure.insn_bytes));
		memcpy(run->emulation_failure.insn_bytes, insn_bytes, insn_size);
	}

	memcpy(&run->internal.data[info_start], info, sizeof(info));
	memcpy(&run->internal.data[info_start + ARRAY_SIZE(info)], data,
	       ndata * sizeof(data[0]));

	run->emulation_failure.ndata = info_start + ARRAY_SIZE(info) + ndata;
}

static void prepare_emulation_ctxt_failure_exit(struct kvm_vcpu *vcpu)
{
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;

	prepare_emulation_failure_exit(vcpu, NULL, 0, ctxt->fetch.data,
				       ctxt->fetch.end - ctxt->fetch.data);
}

void __kvm_prepare_emulation_failure_exit(struct kvm_vcpu *vcpu, u64 *data,
					  u8 ndata)
{
	prepare_emulation_failure_exit(vcpu, data, ndata, NULL, 0);
}
EXPORT_SYMBOL_GPL(__kvm_prepare_emulation_failure_exit);

void kvm_prepare_emulation_failure_exit(struct kvm_vcpu *vcpu)
{
	__kvm_prepare_emulation_failure_exit(vcpu, NULL, 0);
}
EXPORT_SYMBOL_GPL(kvm_prepare_emulation_failure_exit);

static int handle_emulation_failure(struct kvm_vcpu *vcpu, int emulation_type)
{
	struct kvm *kvm = vcpu->kvm;

	++vcpu->stat.insn_emulation_fail;
	trace_kvm_emulate_insn_failed(vcpu);

	if (emulation_type & EMULTYPE_VMWARE_GP) {
		kvm_queue_exception_e(vcpu, GP_VECTOR, 0);
		return 1;
	}

	if (kvm->arch.exit_on_emulation_error ||
	    (emulation_type & EMULTYPE_SKIP)) {
		prepare_emulation_ctxt_failure_exit(vcpu);
		return 0;
	}

	kvm_queue_exception(vcpu, UD_VECTOR);

	if (!is_guest_mode(vcpu) && kvm_x86_call(get_cpl)(vcpu) == 0) {
		prepare_emulation_ctxt_failure_exit(vcpu);
		return 0;
	}

	return 1;
}

static bool kvm_unprotect_and_retry_on_failure(struct kvm_vcpu *vcpu,
					       gpa_t cr2_or_gpa,
					       int emulation_type)
{
	if (!(emulation_type & EMULTYPE_ALLOW_RETRY_PF))
		return false;

	/*
	 * If the failed instruction faulted on an access to page tables that
	 * are used to translate any part of the instruction, KVM can't resolve
	 * the issue by unprotecting the gfn, as zapping the shadow page will
	 * result in the instruction taking a !PRESENT page fault and thus put
	 * the vCPU into an infinite loop of page faults.  E.g. KVM will create
	 * a SPTE and write-protect the gfn to resolve the !PRESENT fault, and
	 * then zap the SPTE to unprotect the gfn, and then do it all over
	 * again.  Report the error to userspace.
	 */
	if (emulation_type & EMULTYPE_WRITE_PF_TO_SP)
		return false;

	/*
	 * If emulation may have been triggered by a write to a shadowed page
	 * table, unprotect the gfn (zap any relevant SPTEs) and re-enter the
	 * guest to let the CPU re-execute the instruction in the hope that the
	 * CPU can cleanly execute the instruction that KVM failed to emulate.
	 */
	__kvm_mmu_unprotect_gfn_and_retry(vcpu, cr2_or_gpa, true);

	/*
	 * Retry even if _this_ vCPU didn't unprotect the gfn, as it's possible
	 * all SPTEs were already zapped by a different task.  The alternative
	 * is to report the error to userspace and likely terminate the guest,
	 * and the last_retry_{eip,addr} checks will prevent retrying the page
	 * fault indefinitely, i.e. there's nothing to lose by retrying.
	 */
	return true;
}

static int complete_emulated_mmio(struct kvm_vcpu *vcpu);
static int complete_emulated_pio(struct kvm_vcpu *vcpu);

static int kvm_vcpu_check_hw_bp(unsigned long addr, u32 type, u32 dr7,
				unsigned long *db)
{
	u32 dr6 = 0;
	int i;
	u32 enable, rwlen;

	enable = dr7;
	rwlen = dr7 >> 16;
	for (i = 0; i < 4; i++, enable >>= 2, rwlen >>= 4)
		if ((enable & 3) && (rwlen & 15) == type && db[i] == addr)
			dr6 |= (1 << i);
	return dr6;
}

static int kvm_vcpu_do_singlestep(struct kvm_vcpu *vcpu)
{
	struct kvm_run *kvm_run = vcpu->run;

	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP) {
		kvm_run->debug.arch.dr6 = DR6_BS | DR6_ACTIVE_LOW;
		kvm_run->debug.arch.pc = kvm_get_linear_rip(vcpu);
		kvm_run->debug.arch.exception = DB_VECTOR;
		kvm_run->exit_reason = KVM_EXIT_DEBUG;
		return 0;
	}
	kvm_queue_exception_p(vcpu, DB_VECTOR, DR6_BS);
	return 1;
}

int kvm_skip_emulated_instruction(struct kvm_vcpu *vcpu)
{
	unsigned long rflags = kvm_x86_call(get_rflags)(vcpu);
	int r;

	r = kvm_x86_call(skip_emulated_instruction)(vcpu);
	if (unlikely(!r))
		return 0;

	kvm_pmu_trigger_event(vcpu, kvm_pmu_eventsel.INSTRUCTIONS_RETIRED);

	/*
	 * rflags is the old, "raw" value of the flags.  The new value has
	 * not been saved yet.
	 *
	 * This is correct even for TF set by the guest, because "the
	 * processor will not generate this exception after the instruction
	 * that sets the TF flag".
	 */
	if (unlikely(rflags & X86_EFLAGS_TF))
		r = kvm_vcpu_do_singlestep(vcpu);
	return r;
}
EXPORT_SYMBOL_GPL(kvm_skip_emulated_instruction);

static bool kvm_is_code_breakpoint_inhibited(struct kvm_vcpu *vcpu)
{
	if (kvm_get_rflags(vcpu) & X86_EFLAGS_RF)
		return true;

	/*
	 * Intel compatible CPUs inhibit code #DBs when MOV/POP SS blocking is
	 * active, but AMD compatible CPUs do not.
	 */
	if (!guest_cpuid_is_intel_compatible(vcpu))
		return false;

	return kvm_x86_call(get_interrupt_shadow)(vcpu) & KVM_X86_SHADOW_INT_MOV_SS;
}

static bool kvm_vcpu_check_code_breakpoint(struct kvm_vcpu *vcpu,
					   int emulation_type, int *r)
{
	WARN_ON_ONCE(emulation_type & EMULTYPE_NO_DECODE);

	/*
	 * Do not check for code breakpoints if hardware has already done the
	 * checks, as inferred from the emulation type.  On NO_DECODE and SKIP,
	 * the instruction has passed all exception checks, and all intercepted
	 * exceptions that trigger emulation have lower priority than code
	 * breakpoints, i.e. the fact that the intercepted exception occurred
	 * means any code breakpoints have already been serviced.
	 *
	 * Note, KVM needs to check for code #DBs on EMULTYPE_TRAP_UD_FORCED as
	 * hardware has checked the RIP of the magic prefix, but not the RIP of
	 * the instruction being emulated.  The intent of forced emulation is
	 * to behave as if KVM intercepted the instruction without an exception
	 * and without a prefix.
	 */
	if (emulation_type & (EMULTYPE_NO_DECODE | EMULTYPE_SKIP |
			      EMULTYPE_TRAP_UD | EMULTYPE_VMWARE_GP | EMULTYPE_PF))
		return false;

	if (unlikely(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP) &&
	    (vcpu->arch.guest_debug_dr7 & DR7_BP_EN_MASK)) {
		struct kvm_run *kvm_run = vcpu->run;
		unsigned long eip = kvm_get_linear_rip(vcpu);
		u32 dr6 = kvm_vcpu_check_hw_bp(eip, 0,
					   vcpu->arch.guest_debug_dr7,
					   vcpu->arch.eff_db);

		if (dr6 != 0) {
			kvm_run->debug.arch.dr6 = dr6 | DR6_ACTIVE_LOW;
			kvm_run->debug.arch.pc = eip;
			kvm_run->debug.arch.exception = DB_VECTOR;
			kvm_run->exit_reason = KVM_EXIT_DEBUG;
			*r = 0;
			return true;
		}
	}

	if (unlikely(vcpu->arch.dr7 & DR7_BP_EN_MASK) &&
	    !kvm_is_code_breakpoint_inhibited(vcpu)) {
		unsigned long eip = kvm_get_linear_rip(vcpu);
		u32 dr6 = kvm_vcpu_check_hw_bp(eip, 0,
					   vcpu->arch.dr7,
					   vcpu->arch.db);

		if (dr6 != 0) {
			kvm_queue_exception_p(vcpu, DB_VECTOR, dr6);
			*r = 1;
			return true;
		}
	}

	return false;
}

static bool is_vmware_backdoor_opcode(struct x86_emulate_ctxt *ctxt)
{
	switch (ctxt->opcode_len) {
	case 1:
		switch (ctxt->b) {
		case 0xe4:	/* IN */
		case 0xe5:
		case 0xec:
		case 0xed:
		case 0xe6:	/* OUT */
		case 0xe7:
		case 0xee:
		case 0xef:
		case 0x6c:	/* INS */
		case 0x6d:
		case 0x6e:	/* OUTS */
		case 0x6f:
			return true;
		}
		break;
	case 2:
		switch (ctxt->b) {
		case 0x33:	/* RDPMC */
			return true;
		}
		break;
	}

	return false;
}

/*
 * Decode an instruction for emulation.  The caller is responsible for handling
 * code breakpoints.  Note, manually detecting code breakpoints is unnecessary
 * (and wrong) when emulating on an intercepted fault-like exception[*], as
 * code breakpoints have higher priority and thus have already been done by
 * hardware.
 *
 * [*] Except #MC, which is higher priority, but KVM should never emulate in
 *     response to a machine check.
 */
int x86_decode_emulated_instruction(struct kvm_vcpu *vcpu, int emulation_type,
				    void *insn, int insn_len)
{
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;
	int r;

	init_emulate_ctxt(vcpu);

	r = x86_decode_insn(ctxt, insn, insn_len, emulation_type);

	trace_kvm_emulate_insn_start(vcpu);
	++vcpu->stat.insn_emulation;

	return r;
}
EXPORT_SYMBOL_GPL(x86_decode_emulated_instruction);

int x86_emulate_instruction(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
			    int emulation_type, void *insn, int insn_len)
{
	int r;
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;
	bool writeback = true;

	if ((emulation_type & EMULTYPE_ALLOW_RETRY_PF) &&
	    (WARN_ON_ONCE(is_guest_mode(vcpu)) ||
	     WARN_ON_ONCE(!(emulation_type & EMULTYPE_PF))))
		emulation_type &= ~EMULTYPE_ALLOW_RETRY_PF;

	r = kvm_check_emulate_insn(vcpu, emulation_type, insn, insn_len);
	if (r != X86EMUL_CONTINUE) {
		if (r == X86EMUL_RETRY_INSTR || r == X86EMUL_PROPAGATE_FAULT)
			return 1;

		WARN_ON_ONCE(r != X86EMUL_UNHANDLEABLE);
		return handle_emulation_failure(vcpu, emulation_type);
	}

	vcpu->arch.l1tf_flush_l1d = true;

	if (!(emulation_type & EMULTYPE_NO_DECODE)) {
		kvm_clear_exception_queue(vcpu);

		/*
		 * Return immediately if RIP hits a code breakpoint, such #DBs
		 * are fault-like and are higher priority than any faults on
		 * the code fetch itself.
		 */
		if (kvm_vcpu_check_code_breakpoint(vcpu, emulation_type, &r))
			return r;

		r = x86_decode_emulated_instruction(vcpu, emulation_type,
						    insn, insn_len);
		if (r != EMULATION_OK)  {
			if ((emulation_type & EMULTYPE_TRAP_UD) ||
			    (emulation_type & EMULTYPE_TRAP_UD_FORCED)) {
				kvm_queue_exception(vcpu, UD_VECTOR);
				return 1;
			}
			if (kvm_unprotect_and_retry_on_failure(vcpu, cr2_or_gpa,
							       emulation_type))
				return 1;

			if (ctxt->have_exception &&
			    !(emulation_type & EMULTYPE_SKIP)) {
				/*
				 * #UD should result in just EMULATION_FAILED, and trap-like
				 * exception should not be encountered during decode.
				 */
				WARN_ON_ONCE(ctxt->exception.vector == UD_VECTOR ||
					     exception_type(ctxt->exception.vector) == EXCPT_TRAP);
				inject_emulated_exception(vcpu);
				return 1;
			}
			return handle_emulation_failure(vcpu, emulation_type);
		}
	}

	if ((emulation_type & EMULTYPE_VMWARE_GP) &&
	    !is_vmware_backdoor_opcode(ctxt)) {
		kvm_queue_exception_e(vcpu, GP_VECTOR, 0);
		return 1;
	}

	/*
	 * EMULTYPE_SKIP without EMULTYPE_COMPLETE_USER_EXIT is intended for
	 * use *only* by vendor callbacks for kvm_skip_emulated_instruction().
	 * The caller is responsible for updating interruptibility state and
	 * injecting single-step #DBs.
	 */
	if (emulation_type & EMULTYPE_SKIP) {
		if (ctxt->mode != X86EMUL_MODE_PROT64)
			ctxt->eip = (u32)ctxt->_eip;
		else
			ctxt->eip = ctxt->_eip;

		if (emulation_type & EMULTYPE_COMPLETE_USER_EXIT) {
			r = 1;
			goto writeback;
		}

		kvm_rip_write(vcpu, ctxt->eip);
		if (ctxt->eflags & X86_EFLAGS_RF)
			kvm_set_rflags(vcpu, ctxt->eflags & ~X86_EFLAGS_RF);
		return 1;
	}

	/*
	 * If emulation was caused by a write-protection #PF on a non-page_table
	 * writing instruction, try to unprotect the gfn, i.e. zap shadow pages,
	 * and retry the instruction, as the vCPU is likely no longer using the
	 * gfn as a page table.
	 */
	if ((emulation_type & EMULTYPE_ALLOW_RETRY_PF) &&
	    !x86_page_table_writing_insn(ctxt) &&
	    kvm_mmu_unprotect_gfn_and_retry(vcpu, cr2_or_gpa))
		return 1;

	/* this is needed for vmware backdoor interface to work since it
	   changes registers values  during IO operation */
	if (vcpu->arch.emulate_regs_need_sync_from_vcpu) {
		vcpu->arch.emulate_regs_need_sync_from_vcpu = false;
		emulator_invalidate_register_cache(ctxt);
	}

restart:
	if (emulation_type & EMULTYPE_PF) {
		/* Save the faulting GPA (cr2) in the address field */
		ctxt->exception.address = cr2_or_gpa;

		/* With shadow page tables, cr2 contains a GVA or nGPA. */
		if (vcpu->arch.mmu->root_role.direct) {
			ctxt->gpa_available = true;
			ctxt->gpa_val = cr2_or_gpa;
		}
	} else {
		/* Sanitize the address out of an abundance of paranoia. */
		ctxt->exception.address = 0;
	}

	r = x86_emulate_insn(ctxt);

	if (r == EMULATION_INTERCEPTED)
		return 1;

	if (r == EMULATION_FAILED) {
		if (kvm_unprotect_and_retry_on_failure(vcpu, cr2_or_gpa,
						       emulation_type))
			return 1;

		return handle_emulation_failure(vcpu, emulation_type);
	}

	if (ctxt->have_exception) {
		WARN_ON_ONCE(vcpu->mmio_needed && !vcpu->mmio_is_write);
		vcpu->mmio_needed = false;
		r = 1;
		inject_emulated_exception(vcpu);
	} else if (vcpu->arch.pio.count) {
		if (!vcpu->arch.pio.in) {
			/* FIXME: return into emulator if single-stepping.  */
			vcpu->arch.pio.count = 0;
		} else {
			writeback = false;
			vcpu->arch.complete_userspace_io = complete_emulated_pio;
		}
		r = 0;
	} else if (vcpu->mmio_needed) {
		++vcpu->stat.mmio_exits;

		if (!vcpu->mmio_is_write)
			writeback = false;
		r = 0;
		vcpu->arch.complete_userspace_io = complete_emulated_mmio;
	} else if (vcpu->arch.complete_userspace_io) {
		writeback = false;
		r = 0;
	} else if (r == EMULATION_RESTART)
		goto restart;
	else
		r = 1;

writeback:
	if (writeback) {
		unsigned long rflags = kvm_x86_call(get_rflags)(vcpu);
		toggle_interruptibility(vcpu, ctxt->interruptibility);
		vcpu->arch.emulate_regs_need_sync_to_vcpu = false;

		/*
		 * Note, EXCPT_DB is assumed to be fault-like as the emulator
		 * only supports code breakpoints and general detect #DB, both
		 * of which are fault-like.
		 */
		if (!ctxt->have_exception ||
		    exception_type(ctxt->exception.vector) == EXCPT_TRAP) {
			kvm_pmu_trigger_event(vcpu, kvm_pmu_eventsel.INSTRUCTIONS_RETIRED);
			if (ctxt->is_branch)
				kvm_pmu_trigger_event(vcpu, kvm_pmu_eventsel.BRANCH_INSTRUCTIONS_RETIRED);
			kvm_rip_write(vcpu, ctxt->eip);
			if (r && (ctxt->tf || (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)))
				r = kvm_vcpu_do_singlestep(vcpu);
			kvm_x86_call(update_emulated_instruction)(vcpu);
			__kvm_set_rflags(vcpu, ctxt->eflags);
		}

		/*
		 * For STI, interrupts are shadowed; so KVM_REQ_EVENT will
		 * do nothing, and it will be requested again as soon as
		 * the shadow expires.  But we still need to check here,
		 * because POPF has no interrupt shadow.
		 */
		if (unlikely((ctxt->eflags & ~rflags) & X86_EFLAGS_IF))
			kvm_make_request(KVM_REQ_EVENT, vcpu);
	} else
		vcpu->arch.emulate_regs_need_sync_to_vcpu = true;

	return r;
}

int kvm_emulate_instruction(struct kvm_vcpu *vcpu, int emulation_type)
{
	return x86_emulate_instruction(vcpu, 0, emulation_type, NULL, 0);
}
EXPORT_SYMBOL_GPL(kvm_emulate_instruction);

int kvm_emulate_instruction_from_buffer(struct kvm_vcpu *vcpu,
					void *insn, int insn_len)
{
	return x86_emulate_instruction(vcpu, 0, 0, insn, insn_len);
}
EXPORT_SYMBOL_GPL(kvm_emulate_instruction_from_buffer);

static int complete_fast_pio_out_port_0x7e(struct kvm_vcpu *vcpu)
{
	vcpu->arch.pio.count = 0;
	return 1;
}

static int complete_fast_pio_out(struct kvm_vcpu *vcpu)
{
	vcpu->arch.pio.count = 0;

	if (unlikely(!kvm_is_linear_rip(vcpu, vcpu->arch.pio.linear_rip)))
		return 1;

	return kvm_skip_emulated_instruction(vcpu);
}

static int kvm_fast_pio_out(struct kvm_vcpu *vcpu, int size,
			    unsigned short port)
{
	unsigned long val = kvm_rax_read(vcpu);
	int ret = emulator_pio_out(vcpu, size, port, &val, 1);

	if (ret)
		return ret;

	/*
	 * Workaround userspace that relies on old KVM behavior of %rip being
	 * incremented prior to exiting to userspace to handle "OUT 0x7e".
	 */
	if (port == 0x7e &&
	    kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_OUT_7E_INC_RIP)) {
		vcpu->arch.complete_userspace_io =
			complete_fast_pio_out_port_0x7e;
		kvm_skip_emulated_instruction(vcpu);
	} else {
		vcpu->arch.pio.linear_rip = kvm_get_linear_rip(vcpu);
		vcpu->arch.complete_userspace_io = complete_fast_pio_out;
	}
	return 0;
}

static int complete_fast_pio_in(struct kvm_vcpu *vcpu)
{
	unsigned long val;

	/* We should only ever be called with arch.pio.count equal to 1 */
	BUG_ON(vcpu->arch.pio.count != 1);

	if (unlikely(!kvm_is_linear_rip(vcpu, vcpu->arch.pio.linear_rip))) {
		vcpu->arch.pio.count = 0;
		return 1;
	}

	/* For size less than 4 we merge, else we zero extend */
	val = (vcpu->arch.pio.size < 4) ? kvm_rax_read(vcpu) : 0;

	complete_emulator_pio_in(vcpu, &val);
	kvm_rax_write(vcpu, val);

	return kvm_skip_emulated_instruction(vcpu);
}

static int kvm_fast_pio_in(struct kvm_vcpu *vcpu, int size,
			   unsigned short port)
{
	unsigned long val;
	int ret;

	/* For size less than 4 we merge, else we zero extend */
	val = (size < 4) ? kvm_rax_read(vcpu) : 0;

	ret = emulator_pio_in(vcpu, size, port, &val, 1);
	if (ret) {
		kvm_rax_write(vcpu, val);
		return ret;
	}

	vcpu->arch.pio.linear_rip = kvm_get_linear_rip(vcpu);
	vcpu->arch.complete_userspace_io = complete_fast_pio_in;

	return 0;
}

int kvm_fast_pio(struct kvm_vcpu *vcpu, int size, unsigned short port, int in)
{
	int ret;

	if (in)
		ret = kvm_fast_pio_in(vcpu, size, port);
	else
		ret = kvm_fast_pio_out(vcpu, size, port);
	return ret && kvm_skip_emulated_instruction(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_fast_pio);

static int kvmclock_cpu_down_prep(unsigned int cpu)
{
	__this_cpu_write(cpu_tsc_khz, 0);
	return 0;
}

static void tsc_khz_changed(void *data)
{
	struct cpufreq_freqs *freq = data;
	unsigned long khz;

	WARN_ON_ONCE(boot_cpu_has(X86_FEATURE_CONSTANT_TSC));

	if (data)
		khz = freq->new;
	else
		khz = cpufreq_quick_get(raw_smp_processor_id());
	if (!khz)
		khz = tsc_khz;
	__this_cpu_write(cpu_tsc_khz, khz);
}

#ifdef CONFIG_X86_64
static void kvm_hyperv_tsc_notifier(void)
{
	struct kvm *kvm;
	int cpu;

	mutex_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list)
		kvm_make_mclock_inprogress_request(kvm);

	/* no guest entries from this point */
	hyperv_stop_tsc_emulation();

	/* TSC frequency always matches when on Hyper-V */
	if (!boot_cpu_has(X86_FEATURE_CONSTANT_TSC)) {
		for_each_present_cpu(cpu)
			per_cpu(cpu_tsc_khz, cpu) = tsc_khz;
	}
	kvm_caps.max_guest_tsc_khz = tsc_khz;

	list_for_each_entry(kvm, &vm_list, vm_list) {
		__kvm_start_pvclock_update(kvm);
		pvclock_update_vm_gtod_copy(kvm);
		kvm_end_pvclock_update(kvm);
	}

	mutex_unlock(&kvm_lock);
}
#endif

static void __kvmclock_cpufreq_notifier(struct cpufreq_freqs *freq, int cpu)
{
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
	int send_ipi = 0;
	unsigned long i;

	/*
	 * We allow guests to temporarily run on slowing clocks,
	 * provided we notify them after, or to run on accelerating
	 * clocks, provided we notify them before.  Thus time never
	 * goes backwards.
	 *
	 * However, we have a problem.  We can't atomically update
	 * the frequency of a given CPU from this function; it is
	 * merely a notifier, which can be called from any CPU.
	 * Changing the TSC frequency at arbitrary points in time
	 * requires a recomputation of local variables related to
	 * the TSC for each VCPU.  We must flag these local variables
	 * to be updated and be sure the update takes place with the
	 * new frequency before any guests proceed.
	 *
	 * Unfortunately, the combination of hotplug CPU and frequency
	 * change creates an intractable locking scenario; the order
	 * of when these callouts happen is undefined with respect to
	 * CPU hotplug, and they can race with each other.  As such,
	 * merely setting per_cpu(cpu_tsc_khz) = X during a hotadd is
	 * undefined; you can actually have a CPU frequency change take
	 * place in between the computation of X and the setting of the
	 * variable.  To protect against this problem, all updates of
	 * the per_cpu tsc_khz variable are done in an interrupt
	 * protected IPI, and all callers wishing to update the value
	 * must wait for a synchronous IPI to complete (which is trivial
	 * if the caller is on the CPU already).  This establishes the
	 * necessary total order on variable updates.
	 *
	 * Note that because a guest time update may take place
	 * anytime after the setting of the VCPU's request bit, the
	 * correct TSC value must be set before the request.  However,
	 * to ensure the update actually makes it to any guest which
	 * starts running in hardware virtualization between the set
	 * and the acquisition of the spinlock, we must also ping the
	 * CPU after setting the request bit.
	 *
	 */

	smp_call_function_single(cpu, tsc_khz_changed, freq, 1);

	mutex_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list) {
		kvm_for_each_vcpu(i, vcpu, kvm) {
			if (vcpu->cpu != cpu)
				continue;
			kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
			if (vcpu->cpu != raw_smp_processor_id())
				send_ipi = 1;
		}
	}
	mutex_unlock(&kvm_lock);

	if (freq->old < freq->new && send_ipi) {
		/*
		 * We upscale the frequency.  Must make the guest
		 * doesn't see old kvmclock values while running with
		 * the new frequency, otherwise we risk the guest sees
		 * time go backwards.
		 *
		 * In case we update the frequency for another cpu
		 * (which might be in guest context) send an interrupt
		 * to kick the cpu out of guest context.  Next time
		 * guest context is entered kvmclock will be updated,
		 * so the guest will not see stale values.
		 */
		smp_call_function_single(cpu, tsc_khz_changed, freq, 1);
	}
}

static int kvmclock_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				     void *data)
{
	struct cpufreq_freqs *freq = data;
	int cpu;

	if (val == CPUFREQ_PRECHANGE && freq->old > freq->new)
		return 0;
	if (val == CPUFREQ_POSTCHANGE && freq->old < freq->new)
		return 0;

	for_each_cpu(cpu, freq->policy->cpus)
		__kvmclock_cpufreq_notifier(freq, cpu);

	return 0;
}

static struct notifier_block kvmclock_cpufreq_notifier_block = {
	.notifier_call  = kvmclock_cpufreq_notifier
};

static int kvmclock_cpu_online(unsigned int cpu)
{
	tsc_khz_changed(NULL);
	return 0;
}

static void kvm_timer_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_CONSTANT_TSC)) {
		max_tsc_khz = tsc_khz;

		if (IS_ENABLED(CONFIG_CPU_FREQ)) {
			struct cpufreq_policy *policy;
			int cpu;

			cpu = get_cpu();
			policy = cpufreq_cpu_get(cpu);
			if (policy) {
				if (policy->cpuinfo.max_freq)
					max_tsc_khz = policy->cpuinfo.max_freq;
				cpufreq_cpu_put(policy);
			}
			put_cpu();
		}
		cpufreq_register_notifier(&kvmclock_cpufreq_notifier_block,
					  CPUFREQ_TRANSITION_NOTIFIER);

		cpuhp_setup_state(CPUHP_AP_X86_KVM_CLK_ONLINE, "x86/kvm/clk:online",
				  kvmclock_cpu_online, kvmclock_cpu_down_prep);
	}
}

#ifdef CONFIG_X86_64
static void pvclock_gtod_update_fn(struct work_struct *work)
{
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
	unsigned long i;

	mutex_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list)
		kvm_for_each_vcpu(i, vcpu, kvm)
			kvm_make_request(KVM_REQ_MASTERCLOCK_UPDATE, vcpu);
	atomic_set(&kvm_guest_has_master_clock, 0);
	mutex_unlock(&kvm_lock);
}

static DECLARE_WORK(pvclock_gtod_work, pvclock_gtod_update_fn);

/*
 * Indirection to move queue_work() out of the tk_core.seq write held
 * region to prevent possible deadlocks against time accessors which
 * are invoked with work related locks held.
 */
static void pvclock_irq_work_fn(struct irq_work *w)
{
	queue_work(system_long_wq, &pvclock_gtod_work);
}

static DEFINE_IRQ_WORK(pvclock_irq_work, pvclock_irq_work_fn);

/*
 * Notification about pvclock gtod data update.
 */
static int pvclock_gtod_notify(struct notifier_block *nb, unsigned long unused,
			       void *priv)
{
	struct pvclock_gtod_data *gtod = &pvclock_gtod_data;
	struct timekeeper *tk = priv;

	update_pvclock_gtod(tk);

	/*
	 * Disable master clock if host does not trust, or does not use,
	 * TSC based clocksource. Delegate queue_work() to irq_work as
	 * this is invoked with tk_core.seq write held.
	 */
	if (!gtod_is_based_on_tsc(gtod->clock.vclock_mode) &&
	    atomic_read(&kvm_guest_has_master_clock) != 0)
		irq_work_queue(&pvclock_irq_work);
	return 0;
}

static struct notifier_block pvclock_gtod_notifier = {
	.notifier_call = pvclock_gtod_notify,
};
#endif

static inline void kvm_ops_update(struct kvm_x86_init_ops *ops)
{
	memcpy(&kvm_x86_ops, ops->runtime_ops, sizeof(kvm_x86_ops));

#define __KVM_X86_OP(func) \
	static_call_update(kvm_x86_##func, kvm_x86_ops.func);
#define KVM_X86_OP(func) \
	WARN_ON(!kvm_x86_ops.func); __KVM_X86_OP(func)
#define KVM_X86_OP_OPTIONAL __KVM_X86_OP
#define KVM_X86_OP_OPTIONAL_RET0(func) \
	static_call_update(kvm_x86_##func, (void *)kvm_x86_ops.func ? : \
					   (void *)__static_call_return0);
#include <asm/kvm-x86-ops.h>
#undef __KVM_X86_OP

	kvm_pmu_ops_update(ops->pmu_ops);
}

static int kvm_x86_check_processor_compatibility(void)
{
	int cpu = smp_processor_id();
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	/*
	 * Compatibility checks are done when loading KVM and when enabling
	 * hardware, e.g. during CPU hotplug, to ensure all online CPUs are
	 * compatible, i.e. KVM should never perform a compatibility check on
	 * an offline CPU.
	 */
	WARN_ON(!cpu_online(cpu));

	if (__cr4_reserved_bits(cpu_has, c) !=
	    __cr4_reserved_bits(cpu_has, &boot_cpu_data))
		return -EIO;

	return kvm_x86_call(check_processor_compatibility)();
}

static void kvm_x86_check_cpu_compat(void *ret)
{
	*(int *)ret = kvm_x86_check_processor_compatibility();
}

int kvm_x86_vendor_init(struct kvm_x86_init_ops *ops)
{
	u64 host_pat;
	int r, cpu;

	guard(mutex)(&vendor_module_lock);

	if (kvm_x86_ops.enable_virtualization_cpu) {
		pr_err("already loaded vendor module '%s'\n", kvm_x86_ops.name);
		return -EEXIST;
	}

	/*
	 * KVM explicitly assumes that the guest has an FPU and
	 * FXSAVE/FXRSTOR. For example, the KVM_GET_FPU explicitly casts the
	 * vCPU's FPU state as a fxregs_state struct.
	 */
	if (!boot_cpu_has(X86_FEATURE_FPU) || !boot_cpu_has(X86_FEATURE_FXSR)) {
		pr_err("inadequate fpu\n");
		return -EOPNOTSUPP;
	}

	if (IS_ENABLED(CONFIG_PREEMPT_RT) && !boot_cpu_has(X86_FEATURE_CONSTANT_TSC)) {
		pr_err("RT requires X86_FEATURE_CONSTANT_TSC\n");
		return -EOPNOTSUPP;
	}

	/*
	 * KVM assumes that PAT entry '0' encodes WB memtype and simply zeroes
	 * the PAT bits in SPTEs.  Bail if PAT[0] is programmed to something
	 * other than WB.  Note, EPT doesn't utilize the PAT, but don't bother
	 * with an exception.  PAT[0] is set to WB on RESET and also by the
	 * kernel, i.e. failure indicates a kernel bug or broken firmware.
	 */
	if (rdmsrl_safe(MSR_IA32_CR_PAT, &host_pat) ||
	    (host_pat & GENMASK(2, 0)) != 6) {
		pr_err("host PAT[0] is not WB\n");
		return -EIO;
	}

	memset(&kvm_caps, 0, sizeof(kvm_caps));

	x86_emulator_cache = kvm_alloc_emulator_cache();
	if (!x86_emulator_cache) {
		pr_err("failed to allocate cache for x86 emulator\n");
		return -ENOMEM;
	}

	user_return_msrs = alloc_percpu(struct kvm_user_return_msrs);
	if (!user_return_msrs) {
		pr_err("failed to allocate percpu kvm_user_return_msrs\n");
		r = -ENOMEM;
		goto out_free_x86_emulator_cache;
	}
	kvm_nr_uret_msrs = 0;

	r = kvm_mmu_vendor_module_init();
	if (r)
		goto out_free_percpu;

	kvm_caps.supported_vm_types = BIT(KVM_X86_DEFAULT_VM);
	kvm_caps.supported_mce_cap = MCG_CTL_P | MCG_SER_P;

	if (boot_cpu_has(X86_FEATURE_XSAVE)) {
		kvm_host.xcr0 = xgetbv(XCR_XFEATURE_ENABLED_MASK);
		kvm_caps.supported_xcr0 = kvm_host.xcr0 & KVM_SUPPORTED_XCR0;
	}

	rdmsrl_safe(MSR_EFER, &kvm_host.efer);

	if (boot_cpu_has(X86_FEATURE_XSAVES))
		rdmsrl(MSR_IA32_XSS, kvm_host.xss);

	kvm_init_pmu_capability(ops->pmu_ops);

	if (boot_cpu_has(X86_FEATURE_ARCH_CAPABILITIES))
		rdmsrl(MSR_IA32_ARCH_CAPABILITIES, kvm_host.arch_capabilities);

	r = ops->hardware_setup();
	if (r != 0)
		goto out_mmu_exit;

	kvm_ops_update(ops);

	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu, kvm_x86_check_cpu_compat, &r, 1);
		if (r < 0)
			goto out_unwind_ops;
	}

	/*
	 * Point of no return!  DO NOT add error paths below this point unless
	 * absolutely necessary, as most operations from this point forward
	 * require unwinding.
	 */
	kvm_timer_init();

	if (pi_inject_timer == -1)
		pi_inject_timer = housekeeping_enabled(HK_TYPE_TIMER);
#ifdef CONFIG_X86_64
	pvclock_gtod_register_notifier(&pvclock_gtod_notifier);

	if (hypervisor_is_type(X86_HYPER_MS_HYPERV))
		set_hv_tscchange_cb(kvm_hyperv_tsc_notifier);
#endif

	kvm_register_perf_callbacks(ops->handle_intel_pt_intr);

	if (IS_ENABLED(CONFIG_KVM_SW_PROTECTED_VM) && tdp_mmu_enabled)
		kvm_caps.supported_vm_types |= BIT(KVM_X86_SW_PROTECTED_VM);

	if (!kvm_cpu_cap_has(X86_FEATURE_XSAVES))
		kvm_caps.supported_xss = 0;

#define __kvm_cpu_cap_has(UNUSED_, f) kvm_cpu_cap_has(f)
	cr4_reserved_bits = __cr4_reserved_bits(__kvm_cpu_cap_has, UNUSED_);
#undef __kvm_cpu_cap_has

	if (kvm_caps.has_tsc_control) {
		/*
		 * Make sure the user can only configure tsc_khz values that
		 * fit into a signed integer.
		 * A min value is not calculated because it will always
		 * be 1 on all machines.
		 */
		u64 max = min(0x7fffffffULL,
			      __scale_tsc(kvm_caps.max_tsc_scaling_ratio, tsc_khz));
		kvm_caps.max_guest_tsc_khz = max;
	}
	kvm_caps.default_tsc_scaling_ratio = 1ULL << kvm_caps.tsc_scaling_ratio_frac_bits;
	kvm_init_msr_lists();
	return 0;

out_unwind_ops:
	kvm_x86_ops.enable_virtualization_cpu = NULL;
	kvm_x86_call(hardware_unsetup)();
out_mmu_exit:
	kvm_mmu_vendor_module_exit();
out_free_percpu:
	free_percpu(user_return_msrs);
out_free_x86_emulator_cache:
	kmem_cache_destroy(x86_emulator_cache);
	return r;
}
EXPORT_SYMBOL_GPL(kvm_x86_vendor_init);

void kvm_x86_vendor_exit(void)
{
	kvm_unregister_perf_callbacks();

#ifdef CONFIG_X86_64
	if (hypervisor_is_type(X86_HYPER_MS_HYPERV))
		clear_hv_tscchange_cb();
#endif
	kvm_lapic_exit();

	if (!boot_cpu_has(X86_FEATURE_CONSTANT_TSC)) {
		cpufreq_unregister_notifier(&kvmclock_cpufreq_notifier_block,
					    CPUFREQ_TRANSITION_NOTIFIER);
		cpuhp_remove_state_nocalls(CPUHP_AP_X86_KVM_CLK_ONLINE);
	}
#ifdef CONFIG_X86_64
	pvclock_gtod_unregister_notifier(&pvclock_gtod_notifier);
	irq_work_sync(&pvclock_irq_work);
	cancel_work_sync(&pvclock_gtod_work);
#endif
	kvm_x86_call(hardware_unsetup)();
	kvm_mmu_vendor_module_exit();
	free_percpu(user_return_msrs);
	kmem_cache_destroy(x86_emulator_cache);
#ifdef CONFIG_KVM_XEN
	static_key_deferred_flush(&kvm_xen_enabled);
	WARN_ON(static_branch_unlikely(&kvm_xen_enabled.key));
#endif
	mutex_lock(&vendor_module_lock);
	kvm_x86_ops.enable_virtualization_cpu = NULL;
	mutex_unlock(&vendor_module_lock);
}
EXPORT_SYMBOL_GPL(kvm_x86_vendor_exit);

#ifdef CONFIG_X86_64
static int kvm_pv_clock_pairing(struct kvm_vcpu *vcpu, gpa_t paddr,
			        unsigned long clock_type)
{
	struct kvm_clock_pairing clock_pairing;
	struct timespec64 ts;
	u64 cycle;
	int ret;

	if (clock_type != KVM_CLOCK_PAIRING_WALLCLOCK)
		return -KVM_EOPNOTSUPP;

	/*
	 * When tsc is in permanent catchup mode guests won't be able to use
	 * pvclock_read_retry loop to get consistent view of pvclock
	 */
	if (vcpu->arch.tsc_always_catchup)
		return -KVM_EOPNOTSUPP;

	if (!kvm_get_walltime_and_clockread(&ts, &cycle))
		return -KVM_EOPNOTSUPP;

	clock_pairing.sec = ts.tv_sec;
	clock_pairing.nsec = ts.tv_nsec;
	clock_pairing.tsc = kvm_read_l1_tsc(vcpu, cycle);
	clock_pairing.flags = 0;
	memset(&clock_pairing.pad, 0, sizeof(clock_pairing.pad));

	ret = 0;
	if (kvm_write_guest(vcpu->kvm, paddr, &clock_pairing,
			    sizeof(struct kvm_clock_pairing)))
		ret = -KVM_EFAULT;

	return ret;
}
#endif

/*
 * kvm_pv_kick_cpu_op:  Kick a vcpu.
 *
 * @apicid - apicid of vcpu to be kicked.
 */
static void kvm_pv_kick_cpu_op(struct kvm *kvm, int apicid)
{
	/*
	 * All other fields are unused for APIC_DM_REMRD, but may be consumed by
	 * common code, e.g. for tracing. Defer initialization to the compiler.
	 */
	struct kvm_lapic_irq lapic_irq = {
		.delivery_mode = APIC_DM_REMRD,
		.dest_mode = APIC_DEST_PHYSICAL,
		.shorthand = APIC_DEST_NOSHORT,
		.dest_id = apicid,
	};

	kvm_irq_delivery_to_apic(kvm, NULL, &lapic_irq, NULL);
}

bool kvm_apicv_activated(struct kvm *kvm)
{
	return (READ_ONCE(kvm->arch.apicv_inhibit_reasons) == 0);
}
EXPORT_SYMBOL_GPL(kvm_apicv_activated);

bool kvm_vcpu_apicv_activated(struct kvm_vcpu *vcpu)
{
	ulong vm_reasons = READ_ONCE(vcpu->kvm->arch.apicv_inhibit_reasons);
	ulong vcpu_reasons =
			kvm_x86_call(vcpu_get_apicv_inhibit_reasons)(vcpu);

	return (vm_reasons | vcpu_reasons) == 0;
}
EXPORT_SYMBOL_GPL(kvm_vcpu_apicv_activated);

static void set_or_clear_apicv_inhibit(unsigned long *inhibits,
				       enum kvm_apicv_inhibit reason, bool set)
{
	const struct trace_print_flags apicv_inhibits[] = { APICV_INHIBIT_REASONS };

	BUILD_BUG_ON(ARRAY_SIZE(apicv_inhibits) != NR_APICV_INHIBIT_REASONS);

	if (set)
		__set_bit(reason, inhibits);
	else
		__clear_bit(reason, inhibits);

	trace_kvm_apicv_inhibit_changed(reason, set, *inhibits);
}

static void kvm_apicv_init(struct kvm *kvm)
{
	enum kvm_apicv_inhibit reason = enable_apicv ? APICV_INHIBIT_REASON_ABSENT :
						       APICV_INHIBIT_REASON_DISABLED;

	set_or_clear_apicv_inhibit(&kvm->arch.apicv_inhibit_reasons, reason, true);

	init_rwsem(&kvm->arch.apicv_update_lock);
}

static void kvm_sched_yield(struct kvm_vcpu *vcpu, unsigned long dest_id)
{
	struct kvm_vcpu *target = NULL;
	struct kvm_apic_map *map;

	vcpu->stat.directed_yield_attempted++;

	if (single_task_running())
		goto no_yield;

	rcu_read_lock();
	map = rcu_dereference(vcpu->kvm->arch.apic_map);

	if (likely(map) && dest_id <= map->max_apic_id && map->phys_map[dest_id])
		target = map->phys_map[dest_id]->vcpu;

	rcu_read_unlock();

	if (!target || !READ_ONCE(target->ready))
		goto no_yield;

	/* Ignore requests to yield to self */
	if (vcpu == target)
		goto no_yield;

	if (kvm_vcpu_yield_to(target) <= 0)
		goto no_yield;

	vcpu->stat.directed_yield_successful++;

no_yield:
	return;
}

static int complete_hypercall_exit(struct kvm_vcpu *vcpu)
{
	u64 ret = vcpu->run->hypercall.ret;

	if (!is_64_bit_mode(vcpu))
		ret = (u32)ret;
	kvm_rax_write(vcpu, ret);
	++vcpu->stat.hypercalls;
	return kvm_skip_emulated_instruction(vcpu);
}

unsigned long __kvm_emulate_hypercall(struct kvm_vcpu *vcpu, unsigned long nr,
				      unsigned long a0, unsigned long a1,
				      unsigned long a2, unsigned long a3,
				      int op_64_bit, int cpl)
{
	unsigned long ret;

	trace_kvm_hypercall(nr, a0, a1, a2, a3);

	if (!op_64_bit) {
		nr &= 0xFFFFFFFF;
		a0 &= 0xFFFFFFFF;
		a1 &= 0xFFFFFFFF;
		a2 &= 0xFFFFFFFF;
		a3 &= 0xFFFFFFFF;
	}

	if (cpl) {
		ret = -KVM_EPERM;
		goto out;
	}

	ret = -KVM_ENOSYS;

	switch (nr) {
	case KVM_HC_VAPIC_POLL_IRQ:
		ret = 0;
		break;
	case KVM_HC_KICK_CPU:
		if (!guest_pv_has(vcpu, KVM_FEATURE_PV_UNHALT))
			break;

		kvm_pv_kick_cpu_op(vcpu->kvm, a1);
		kvm_sched_yield(vcpu, a1);
		ret = 0;
		break;
#ifdef CONFIG_X86_64
	case KVM_HC_CLOCK_PAIRING:
		ret = kvm_pv_clock_pairing(vcpu, a0, a1);
		break;
#endif
	case KVM_HC_SEND_IPI:
		if (!guest_pv_has(vcpu, KVM_FEATURE_PV_SEND_IPI))
			break;

		ret = kvm_pv_send_ipi(vcpu->kvm, a0, a1, a2, a3, op_64_bit);
		break;
	case KVM_HC_SCHED_YIELD:
		if (!guest_pv_has(vcpu, KVM_FEATURE_PV_SCHED_YIELD))
			break;

		kvm_sched_yield(vcpu, a0);
		ret = 0;
		break;
	case KVM_HC_MAP_GPA_RANGE: {
		u64 gpa = a0, npages = a1, attrs = a2;

		ret = -KVM_ENOSYS;
		if (!(vcpu->kvm->arch.hypercall_exit_enabled & (1 << KVM_HC_MAP_GPA_RANGE)))
			break;

		if (!PAGE_ALIGNED(gpa) || !npages ||
		    gpa_to_gfn(gpa) + npages <= gpa_to_gfn(gpa)) {
			ret = -KVM_EINVAL;
			break;
		}

		vcpu->run->exit_reason        = KVM_EXIT_HYPERCALL;
		vcpu->run->hypercall.nr       = KVM_HC_MAP_GPA_RANGE;
		vcpu->run->hypercall.args[0]  = gpa;
		vcpu->run->hypercall.args[1]  = npages;
		vcpu->run->hypercall.args[2]  = attrs;
		vcpu->run->hypercall.flags    = 0;
		if (op_64_bit)
			vcpu->run->hypercall.flags |= KVM_EXIT_HYPERCALL_LONG_MODE;

		WARN_ON_ONCE(vcpu->run->hypercall.flags & KVM_EXIT_HYPERCALL_MBZ);
		vcpu->arch.complete_userspace_io = complete_hypercall_exit;
		/* stat is incremented on completion. */
		return 0;
	}
	default:
		ret = -KVM_ENOSYS;
		break;
	}

out:
	++vcpu->stat.hypercalls;
	return ret;
}
EXPORT_SYMBOL_GPL(__kvm_emulate_hypercall);

int kvm_emulate_hypercall(struct kvm_vcpu *vcpu)
{
	unsigned long nr, a0, a1, a2, a3, ret;
	int op_64_bit;
	int cpl;

	if (kvm_xen_hypercall_enabled(vcpu->kvm))
		return kvm_xen_hypercall(vcpu);

	if (kvm_hv_hypercall_enabled(vcpu))
		return kvm_hv_hypercall(vcpu);

	nr = kvm_rax_read(vcpu);
	a0 = kvm_rbx_read(vcpu);
	a1 = kvm_rcx_read(vcpu);
	a2 = kvm_rdx_read(vcpu);
	a3 = kvm_rsi_read(vcpu);
	op_64_bit = is_64_bit_hypercall(vcpu);
	cpl = kvm_x86_call(get_cpl)(vcpu);

	ret = __kvm_emulate_hypercall(vcpu, nr, a0, a1, a2, a3, op_64_bit, cpl);
	if (nr == KVM_HC_MAP_GPA_RANGE && !ret)
		/* MAP_GPA tosses the request to the user space. */
		return 0;

	if (!op_64_bit)
		ret = (u32)ret;
	kvm_rax_write(vcpu, ret);

	return kvm_skip_emulated_instruction(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_emulate_hypercall);

static int emulator_fix_hypercall(struct x86_emulate_ctxt *ctxt)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	char instruction[3];
	unsigned long rip = kvm_rip_read(vcpu);

	/*
	 * If the quirk is disabled, synthesize a #UD and let the guest pick up
	 * the pieces.
	 */
	if (!kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_FIX_HYPERCALL_INSN)) {
		ctxt->exception.error_code_valid = false;
		ctxt->exception.vector = UD_VECTOR;
		ctxt->have_exception = true;
		return X86EMUL_PROPAGATE_FAULT;
	}

	kvm_x86_call(patch_hypercall)(vcpu, instruction);

	return emulator_write_emulated(ctxt, rip, instruction, 3,
		&ctxt->exception);
}

static int dm_request_for_irq_injection(struct kvm_vcpu *vcpu)
{
	return vcpu->run->request_interrupt_window &&
		likely(!pic_in_kernel(vcpu->kvm));
}

/* Called within kvm->srcu read side.  */
static void post_kvm_run_save(struct kvm_vcpu *vcpu)
{
	struct kvm_run *kvm_run = vcpu->run;

	kvm_run->if_flag = kvm_x86_call(get_if_flag)(vcpu);
	kvm_run->cr8 = kvm_get_cr8(vcpu);
	kvm_run->apic_base = vcpu->arch.apic_base;

	kvm_run->ready_for_interrupt_injection =
		pic_in_kernel(vcpu->kvm) ||
		kvm_vcpu_ready_for_interrupt_injection(vcpu);

	if (is_smm(vcpu))
		kvm_run->flags |= KVM_RUN_X86_SMM;
	if (is_guest_mode(vcpu))
		kvm_run->flags |= KVM_RUN_X86_GUEST_MODE;
}

static void update_cr8_intercept(struct kvm_vcpu *vcpu)
{
	int max_irr, tpr;

	if (!kvm_x86_ops.update_cr8_intercept)
		return;

	if (!lapic_in_kernel(vcpu))
		return;

	if (vcpu->arch.apic->apicv_active)
		return;

	if (!vcpu->arch.apic->vapic_addr)
		max_irr = kvm_lapic_find_highest_irr(vcpu);
	else
		max_irr = -1;

	if (max_irr != -1)
		max_irr >>= 4;

	tpr = kvm_lapic_get_cr8(vcpu);

	kvm_x86_call(update_cr8_intercept)(vcpu, tpr, max_irr);
}


int kvm_check_nested_events(struct kvm_vcpu *vcpu)
{
	if (kvm_test_request(KVM_REQ_TRIPLE_FAULT, vcpu)) {
		kvm_x86_ops.nested_ops->triple_fault(vcpu);
		return 1;
	}

	return kvm_x86_ops.nested_ops->check_events(vcpu);
}

static void kvm_inject_exception(struct kvm_vcpu *vcpu)
{
	/*
	 * Suppress the error code if the vCPU is in Real Mode, as Real Mode
	 * exceptions don't report error codes.  The presence of an error code
	 * is carried with the exception and only stripped when the exception
	 * is injected as intercepted #PF VM-Exits for AMD's Paged Real Mode do
	 * report an error code despite the CPU being in Real Mode.
	 */
	vcpu->arch.exception.has_error_code &= is_protmode(vcpu);

	trace_kvm_inj_exception(vcpu->arch.exception.vector,
				vcpu->arch.exception.has_error_code,
				vcpu->arch.exception.error_code,
				vcpu->arch.exception.injected);

	kvm_x86_call(inject_exception)(vcpu);
}

/*
 * Check for any event (interrupt or exception) that is ready to be injected,
 * and if there is at least one event, inject the event with the highest
 * priority.  This handles both "pending" events, i.e. events that have never
 * been injected into the guest, and "injected" events, i.e. events that were
 * injected as part of a previous VM-Enter, but weren't successfully delivered
 * and need to be re-injected.
 *
 * Note, this is not guaranteed to be invoked on a guest instruction boundary,
 * i.e. doesn't guarantee that there's an event window in the guest.  KVM must
 * be able to inject exceptions in the "middle" of an instruction, and so must
 * also be able to re-inject NMIs and IRQs in the middle of an instruction.
 * I.e. for exceptions and re-injected events, NOT invoking this on instruction
 * boundaries is necessary and correct.
 *
 * For simplicity, KVM uses a single path to inject all events (except events
 * that are injected directly from L1 to L2) and doesn't explicitly track
 * instruction boundaries for asynchronous events.  However, because VM-Exits
 * that can occur during instruction execution typically result in KVM skipping
 * the instruction or injecting an exception, e.g. instruction and exception
 * intercepts, and because pending exceptions have higher priority than pending
 * interrupts, KVM still honors instruction boundaries in most scenarios.
 *
 * But, if a VM-Exit occurs during instruction execution, and KVM does NOT skip
 * the instruction or inject an exception, then KVM can incorrecty inject a new
 * asynchronous event if the event became pending after the CPU fetched the
 * instruction (in the guest).  E.g. if a page fault (#PF, #NPF, EPT violation)
 * occurs and is resolved by KVM, a coincident NMI, SMI, IRQ, etc... can be
 * injected on the restarted instruction instead of being deferred until the
 * instruction completes.
 *
 * In practice, this virtualization hole is unlikely to be observed by the
 * guest, and even less likely to cause functional problems.  To detect the
 * hole, the guest would have to trigger an event on a side effect of an early
 * phase of instruction execution, e.g. on the instruction fetch from memory.
 * And for it to be a functional problem, the guest would need to depend on the
 * ordering between that side effect, the instruction completing, _and_ the
 * delivery of the asynchronous event.
 */
static int kvm_check_and_inject_events(struct kvm_vcpu *vcpu,
				       bool *req_immediate_exit)
{
	bool can_inject;
	int r;

	/*
	 * Process nested events first, as nested VM-Exit supersedes event
	 * re-injection.  If there's an event queued for re-injection, it will
	 * be saved into the appropriate vmc{b,s}12 fields on nested VM-Exit.
	 */
	if (is_guest_mode(vcpu))
		r = kvm_check_nested_events(vcpu);
	else
		r = 0;

	/*
	 * Re-inject exceptions and events *especially* if immediate entry+exit
	 * to/from L2 is needed, as any event that has already been injected
	 * into L2 needs to complete its lifecycle before injecting a new event.
	 *
	 * Don't re-inject an NMI or interrupt if there is a pending exception.
	 * This collision arises if an exception occurred while vectoring the
	 * injected event, KVM intercepted said exception, and KVM ultimately
	 * determined the fault belongs to the guest and queues the exception
	 * for injection back into the guest.
	 *
	 * "Injected" interrupts can also collide with pending exceptions if
	 * userspace ignores the "ready for injection" flag and blindly queues
	 * an interrupt.  In that case, prioritizing the exception is correct,
	 * as the exception "occurred" before the exit to userspace.  Trap-like
	 * exceptions, e.g. most #DBs, have higher priority than interrupts.
	 * And while fault-like exceptions, e.g. #GP and #PF, are the lowest
	 * priority, they're only generated (pended) during instruction
	 * execution, and interrupts are recognized at instruction boundaries.
	 * Thus a pending fault-like exception means the fault occurred on the
	 * *previous* instruction and must be serviced prior to recognizing any
	 * new events in order to fully complete the previous instruction.
	 */
	if (vcpu->arch.exception.injected)
		kvm_inject_exception(vcpu);
	else if (kvm_is_exception_pending(vcpu))
		; /* see above */
	else if (vcpu->arch.nmi_injected)
		kvm_x86_call(inject_nmi)(vcpu);
	else if (vcpu->arch.interrupt.injected)
		kvm_x86_call(inject_irq)(vcpu, true);

	/*
	 * Exceptions that morph to VM-Exits are handled above, and pending
	 * exceptions on top of injected exceptions that do not VM-Exit should
	 * either morph to #DF or, sadly, override the injected exception.
	 */
	WARN_ON_ONCE(vcpu->arch.exception.injected &&
		     vcpu->arch.exception.pending);

	/*
	 * Bail if immediate entry+exit to/from the guest is needed to complete
	 * nested VM-Enter or event re-injection so that a different pending
	 * event can be serviced (or if KVM needs to exit to userspace).
	 *
	 * Otherwise, continue processing events even if VM-Exit occurred.  The
	 * VM-Exit will have cleared exceptions that were meant for L2, but
	 * there may now be events that can be injected into L1.
	 */
	if (r < 0)
		goto out;

	/*
	 * A pending exception VM-Exit should either result in nested VM-Exit
	 * or force an immediate re-entry and exit to/from L2, and exception
	 * VM-Exits cannot be injected (flag should _never_ be set).
	 */
	WARN_ON_ONCE(vcpu->arch.exception_vmexit.injected ||
		     vcpu->arch.exception_vmexit.pending);

	/*
	 * New events, other than exceptions, cannot be injected if KVM needs
	 * to re-inject a previous event.  See above comments on re-injecting
	 * for why pending exceptions get priority.
	 */
	can_inject = !kvm_event_needs_reinjection(vcpu);

	if (vcpu->arch.exception.pending) {
		/*
		 * Fault-class exceptions, except #DBs, set RF=1 in the RFLAGS
		 * value pushed on the stack.  Trap-like exception and all #DBs
		 * leave RF as-is (KVM follows Intel's behavior in this regard;
		 * AMD states that code breakpoint #DBs excplitly clear RF=0).
		 *
		 * Note, most versions of Intel's SDM and AMD's APM incorrectly
		 * describe the behavior of General Detect #DBs, which are
		 * fault-like.  They do _not_ set RF, a la code breakpoints.
		 */
		if (exception_type(vcpu->arch.exception.vector) == EXCPT_FAULT)
			__kvm_set_rflags(vcpu, kvm_get_rflags(vcpu) |
					     X86_EFLAGS_RF);

		if (vcpu->arch.exception.vector == DB_VECTOR) {
			kvm_deliver_exception_payload(vcpu, &vcpu->arch.exception);
			if (vcpu->arch.dr7 & DR7_GD) {
				vcpu->arch.dr7 &= ~DR7_GD;
				kvm_update_dr7(vcpu);
			}
		}

		kvm_inject_exception(vcpu);

		vcpu->arch.exception.pending = false;
		vcpu->arch.exception.injected = true;

		can_inject = false;
	}

	/* Don't inject interrupts if the user asked to avoid doing so */
	if (vcpu->guest_debug & KVM_GUESTDBG_BLOCKIRQ)
		return 0;

	/*
	 * Finally, inject interrupt events.  If an event cannot be injected
	 * due to architectural conditions (e.g. IF=0) a window-open exit
	 * will re-request KVM_REQ_EVENT.  Sometimes however an event is pending
	 * and can architecturally be injected, but we cannot do it right now:
	 * an interrupt could have arrived just now and we have to inject it
	 * as a vmexit, or there could already an event in the queue, which is
	 * indicated by can_inject.  In that case we request an immediate exit
	 * in order to make progress and get back here for another iteration.
	 * The kvm_x86_ops hooks communicate this by returning -EBUSY.
	 */
#ifdef CONFIG_KVM_SMM
	if (vcpu->arch.smi_pending) {
		r = can_inject ? kvm_x86_call(smi_allowed)(vcpu, true) :
				 -EBUSY;
		if (r < 0)
			goto out;
		if (r) {
			vcpu->arch.smi_pending = false;
			++vcpu->arch.smi_count;
			enter_smm(vcpu);
			can_inject = false;
		} else
			kvm_x86_call(enable_smi_window)(vcpu);
	}
#endif

	if (vcpu->arch.nmi_pending) {
		r = can_inject ? kvm_x86_call(nmi_allowed)(vcpu, true) :
				 -EBUSY;
		if (r < 0)
			goto out;
		if (r) {
			--vcpu->arch.nmi_pending;
			vcpu->arch.nmi_injected = true;
			kvm_x86_call(inject_nmi)(vcpu);
			can_inject = false;
			WARN_ON(kvm_x86_call(nmi_allowed)(vcpu, true) < 0);
		}
		if (vcpu->arch.nmi_pending)
			kvm_x86_call(enable_nmi_window)(vcpu);
	}

	if (kvm_cpu_has_injectable_intr(vcpu)) {
		r = can_inject ? kvm_x86_call(interrupt_allowed)(vcpu, true) :
				 -EBUSY;
		if (r < 0)
			goto out;
		if (r) {
			int irq = kvm_cpu_get_interrupt(vcpu);

			if (!WARN_ON_ONCE(irq == -1)) {
				kvm_queue_interrupt(vcpu, irq, false);
				kvm_x86_call(inject_irq)(vcpu, false);
				WARN_ON(kvm_x86_call(interrupt_allowed)(vcpu, true) < 0);
			}
		}
		if (kvm_cpu_has_injectable_intr(vcpu))
			kvm_x86_call(enable_irq_window)(vcpu);
	}

	if (is_guest_mode(vcpu) &&
	    kvm_x86_ops.nested_ops->has_events &&
	    kvm_x86_ops.nested_ops->has_events(vcpu, true))
		*req_immediate_exit = true;

	/*
	 * KVM must never queue a new exception while injecting an event; KVM
	 * is done emulating and should only propagate the to-be-injected event
	 * to the VMCS/VMCB.  Queueing a new exception can put the vCPU into an
	 * infinite loop as KVM will bail from VM-Enter to inject the pending
	 * exception and start the cycle all over.
	 *
	 * Exempt triple faults as they have special handling and won't put the
	 * vCPU into an infinite loop.  Triple fault can be queued when running
	 * VMX without unrestricted guest, as that requires KVM to emulate Real
	 * Mode events (see kvm_inject_realmode_interrupt()).
	 */
	WARN_ON_ONCE(vcpu->arch.exception.pending ||
		     vcpu->arch.exception_vmexit.pending);
	return 0;

out:
	if (r == -EBUSY) {
		*req_immediate_exit = true;
		r = 0;
	}
	return r;
}

static void process_nmi(struct kvm_vcpu *vcpu)
{
	unsigned int limit;

	/*
	 * x86 is limited to one NMI pending, but because KVM can't react to
	 * incoming NMIs as quickly as bare metal, e.g. if the vCPU is
	 * scheduled out, KVM needs to play nice with two queued NMIs showing
	 * up at the same time.  To handle this scenario, allow two NMIs to be
	 * (temporarily) pending so long as NMIs are not blocked and KVM is not
	 * waiting for a previous NMI injection to complete (which effectively
	 * blocks NMIs).  KVM will immediately inject one of the two NMIs, and
	 * will request an NMI window to handle the second NMI.
	 */
	if (kvm_x86_call(get_nmi_mask)(vcpu) || vcpu->arch.nmi_injected)
		limit = 1;
	else
		limit = 2;

	/*
	 * Adjust the limit to account for pending virtual NMIs, which aren't
	 * tracked in vcpu->arch.nmi_pending.
	 */
	if (kvm_x86_call(is_vnmi_pending)(vcpu))
		limit--;

	vcpu->arch.nmi_pending += atomic_xchg(&vcpu->arch.nmi_queued, 0);
	vcpu->arch.nmi_pending = min(vcpu->arch.nmi_pending, limit);

	if (vcpu->arch.nmi_pending &&
	    (kvm_x86_call(set_vnmi_pending)(vcpu)))
		vcpu->arch.nmi_pending--;

	if (vcpu->arch.nmi_pending)
		kvm_make_request(KVM_REQ_EVENT, vcpu);
}

/* Return total number of NMIs pending injection to the VM */
int kvm_get_nr_pending_nmis(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.nmi_pending +
	       kvm_x86_call(is_vnmi_pending)(vcpu);
}

void kvm_make_scan_ioapic_request_mask(struct kvm *kvm,
				       unsigned long *vcpu_bitmap)
{
	kvm_make_vcpus_request_mask(kvm, KVM_REQ_SCAN_IOAPIC, vcpu_bitmap);
}

void kvm_make_scan_ioapic_request(struct kvm *kvm)
{
	kvm_make_all_cpus_request(kvm, KVM_REQ_SCAN_IOAPIC);
}

void __kvm_vcpu_update_apicv(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	bool activate;

	if (!lapic_in_kernel(vcpu))
		return;

	down_read(&vcpu->kvm->arch.apicv_update_lock);
	preempt_disable();

	/* Do not activate APICV when APIC is disabled */
	activate = kvm_vcpu_apicv_activated(vcpu) &&
		   (kvm_get_apic_mode(vcpu) != LAPIC_MODE_DISABLED);

	if (apic->apicv_active == activate)
		goto out;

	apic->apicv_active = activate;
	kvm_apic_update_apicv(vcpu);
	kvm_x86_call(refresh_apicv_exec_ctrl)(vcpu);

	/*
	 * When APICv gets disabled, we may still have injected interrupts
	 * pending. At the same time, KVM_REQ_EVENT may not be set as APICv was
	 * still active when the interrupt got accepted. Make sure
	 * kvm_check_and_inject_events() is called to check for that.
	 */
	if (!apic->apicv_active)
		kvm_make_request(KVM_REQ_EVENT, vcpu);

out:
	preempt_enable();
	up_read(&vcpu->kvm->arch.apicv_update_lock);
}
EXPORT_SYMBOL_GPL(__kvm_vcpu_update_apicv);

static void kvm_vcpu_update_apicv(struct kvm_vcpu *vcpu)
{
	if (!lapic_in_kernel(vcpu))
		return;

	/*
	 * Due to sharing page tables across vCPUs, the xAPIC memslot must be
	 * deleted if any vCPU has xAPIC virtualization and x2APIC enabled, but
	 * and hardware doesn't support x2APIC virtualization.  E.g. some AMD
	 * CPUs support AVIC but not x2APIC.  KVM still allows enabling AVIC in
	 * this case so that KVM can use the AVIC doorbell to inject interrupts
	 * to running vCPUs, but KVM must not create SPTEs for the APIC base as
	 * the vCPU would incorrectly be able to access the vAPIC page via MMIO
	 * despite being in x2APIC mode.  For simplicity, inhibiting the APIC
	 * access page is sticky.
	 */
	if (apic_x2apic_mode(vcpu->arch.apic) &&
	    kvm_x86_ops.allow_apicv_in_x2apic_without_x2apic_virtualization)
		kvm_inhibit_apic_access_page(vcpu);

	__kvm_vcpu_update_apicv(vcpu);
}

void __kvm_set_or_clear_apicv_inhibit(struct kvm *kvm,
				      enum kvm_apicv_inhibit reason, bool set)
{
	unsigned long old, new;

	lockdep_assert_held_write(&kvm->arch.apicv_update_lock);

	if (!(kvm_x86_ops.required_apicv_inhibits & BIT(reason)))
		return;

	old = new = kvm->arch.apicv_inhibit_reasons;

	set_or_clear_apicv_inhibit(&new, reason, set);

	if (!!old != !!new) {
		/*
		 * Kick all vCPUs before setting apicv_inhibit_reasons to avoid
		 * false positives in the sanity check WARN in vcpu_enter_guest().
		 * This task will wait for all vCPUs to ack the kick IRQ before
		 * updating apicv_inhibit_reasons, and all other vCPUs will
		 * block on acquiring apicv_update_lock so that vCPUs can't
		 * redo vcpu_enter_guest() without seeing the new inhibit state.
		 *
		 * Note, holding apicv_update_lock and taking it in the read
		 * side (handling the request) also prevents other vCPUs from
		 * servicing the request with a stale apicv_inhibit_reasons.
		 */
		kvm_make_all_cpus_request(kvm, KVM_REQ_APICV_UPDATE);
		kvm->arch.apicv_inhibit_reasons = new;
		if (new) {
			unsigned long gfn = gpa_to_gfn(APIC_DEFAULT_PHYS_BASE);
			int idx = srcu_read_lock(&kvm->srcu);

			kvm_zap_gfn_range(kvm, gfn, gfn+1);
			srcu_read_unlock(&kvm->srcu, idx);
		}
	} else {
		kvm->arch.apicv_inhibit_reasons = new;
	}
}

void kvm_set_or_clear_apicv_inhibit(struct kvm *kvm,
				    enum kvm_apicv_inhibit reason, bool set)
{
	if (!enable_apicv)
		return;

	down_write(&kvm->arch.apicv_update_lock);
	__kvm_set_or_clear_apicv_inhibit(kvm, reason, set);
	up_write(&kvm->arch.apicv_update_lock);
}
EXPORT_SYMBOL_GPL(kvm_set_or_clear_apicv_inhibit);

static void vcpu_scan_ioapic(struct kvm_vcpu *vcpu)
{
	if (!kvm_apic_present(vcpu))
		return;

	bitmap_zero(vcpu->arch.ioapic_handled_vectors, 256);

	kvm_x86_call(sync_pir_to_irr)(vcpu);

	if (irqchip_split(vcpu->kvm))
		kvm_scan_ioapic_routes(vcpu, vcpu->arch.ioapic_handled_vectors);
	else if (ioapic_in_kernel(vcpu->kvm))
		kvm_ioapic_scan_entry(vcpu, vcpu->arch.ioapic_handled_vectors);

	if (is_guest_mode(vcpu))
		vcpu->arch.load_eoi_exitmap_pending = true;
	else
		kvm_make_request(KVM_REQ_LOAD_EOI_EXITMAP, vcpu);
}

static void vcpu_load_eoi_exitmap(struct kvm_vcpu *vcpu)
{
	if (!kvm_apic_hw_enabled(vcpu->arch.apic))
		return;

#ifdef CONFIG_KVM_HYPERV
	if (to_hv_vcpu(vcpu)) {
		u64 eoi_exit_bitmap[4];

		bitmap_or((ulong *)eoi_exit_bitmap,
			  vcpu->arch.ioapic_handled_vectors,
			  to_hv_synic(vcpu)->vec_bitmap, 256);
		kvm_x86_call(load_eoi_exitmap)(vcpu, eoi_exit_bitmap);
		return;
	}
#endif
	kvm_x86_call(load_eoi_exitmap)(
		vcpu, (u64 *)vcpu->arch.ioapic_handled_vectors);
}

void kvm_arch_guest_memory_reclaimed(struct kvm *kvm)
{
	kvm_x86_call(guest_memory_reclaimed)(kvm);
}

static void kvm_vcpu_reload_apic_access_page(struct kvm_vcpu *vcpu)
{
	if (!lapic_in_kernel(vcpu))
		return;

	kvm_x86_call(set_apic_access_page_addr)(vcpu);
}

/*
 * Called within kvm->srcu read side.
 * Returns 1 to let vcpu_run() continue the guest execution loop without
 * exiting to the userspace.  Otherwise, the value will be returned to the
 * userspace.
 */
static int vcpu_enter_guest(struct kvm_vcpu *vcpu)
{
	int r;
	bool req_int_win =
		dm_request_for_irq_injection(vcpu) &&
		kvm_cpu_accept_dm_intr(vcpu);
	fastpath_t exit_fastpath;

	bool req_immediate_exit = false;

	if (kvm_request_pending(vcpu)) {
		if (kvm_check_request(KVM_REQ_VM_DEAD, vcpu)) {
			r = -EIO;
			goto out;
		}

		if (kvm_dirty_ring_check_request(vcpu)) {
			r = 0;
			goto out;
		}

		if (kvm_check_request(KVM_REQ_GET_NESTED_STATE_PAGES, vcpu)) {
			if (unlikely(!kvm_x86_ops.nested_ops->get_nested_state_pages(vcpu))) {
				r = 0;
				goto out;
			}
		}
		if (kvm_check_request(KVM_REQ_MMU_FREE_OBSOLETE_ROOTS, vcpu))
			kvm_mmu_free_obsolete_roots(vcpu);
		if (kvm_check_request(KVM_REQ_MIGRATE_TIMER, vcpu))
			__kvm_migrate_timers(vcpu);
		if (kvm_check_request(KVM_REQ_MASTERCLOCK_UPDATE, vcpu))
			kvm_update_masterclock(vcpu->kvm);
		if (kvm_check_request(KVM_REQ_GLOBAL_CLOCK_UPDATE, vcpu))
			kvm_gen_kvmclock_update(vcpu);
		if (kvm_check_request(KVM_REQ_CLOCK_UPDATE, vcpu)) {
			r = kvm_guest_time_update(vcpu);
			if (unlikely(r))
				goto out;
		}
		if (kvm_check_request(KVM_REQ_MMU_SYNC, vcpu))
			kvm_mmu_sync_roots(vcpu);
		if (kvm_check_request(KVM_REQ_LOAD_MMU_PGD, vcpu))
			kvm_mmu_load_pgd(vcpu);

		/*
		 * Note, the order matters here, as flushing "all" TLB entries
		 * also flushes the "current" TLB entries, i.e. servicing the
		 * flush "all" will clear any request to flush "current".
		 */
		if (kvm_check_request(KVM_REQ_TLB_FLUSH, vcpu))
			kvm_vcpu_flush_tlb_all(vcpu);

		kvm_service_local_tlb_flush_requests(vcpu);

		/*
		 * Fall back to a "full" guest flush if Hyper-V's precise
		 * flushing fails.  Note, Hyper-V's flushing is per-vCPU, but
		 * the flushes are considered "remote" and not "local" because
		 * the requests can be initiated from other vCPUs.
		 */
#ifdef CONFIG_KVM_HYPERV
		if (kvm_check_request(KVM_REQ_HV_TLB_FLUSH, vcpu) &&
		    kvm_hv_vcpu_flush_tlb(vcpu))
			kvm_vcpu_flush_tlb_guest(vcpu);
#endif

		if (kvm_check_request(KVM_REQ_REPORT_TPR_ACCESS, vcpu)) {
			vcpu->run->exit_reason = KVM_EXIT_TPR_ACCESS;
			r = 0;
			goto out;
		}
		if (kvm_test_request(KVM_REQ_TRIPLE_FAULT, vcpu)) {
			if (is_guest_mode(vcpu))
				kvm_x86_ops.nested_ops->triple_fault(vcpu);

			if (kvm_check_request(KVM_REQ_TRIPLE_FAULT, vcpu)) {
				vcpu->run->exit_reason = KVM_EXIT_SHUTDOWN;
				vcpu->mmio_needed = 0;
				r = 0;
				goto out;
			}
		}
		if (kvm_check_request(KVM_REQ_APF_HALT, vcpu)) {
			/* Page is swapped out. Do synthetic halt */
			vcpu->arch.apf.halted = true;
			r = 1;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_STEAL_UPDATE, vcpu))
			record_steal_time(vcpu);
		if (kvm_check_request(KVM_REQ_PMU, vcpu))
			kvm_pmu_handle_event(vcpu);
		if (kvm_check_request(KVM_REQ_PMI, vcpu))
			kvm_pmu_deliver_pmi(vcpu);
#ifdef CONFIG_KVM_SMM
		if (kvm_check_request(KVM_REQ_SMI, vcpu))
			process_smi(vcpu);
#endif
		if (kvm_check_request(KVM_REQ_NMI, vcpu))
			process_nmi(vcpu);
		if (kvm_check_request(KVM_REQ_IOAPIC_EOI_EXIT, vcpu)) {
			BUG_ON(vcpu->arch.pending_ioapic_eoi > 255);
			if (test_bit(vcpu->arch.pending_ioapic_eoi,
				     vcpu->arch.ioapic_handled_vectors)) {
				vcpu->run->exit_reason = KVM_EXIT_IOAPIC_EOI;
				vcpu->run->eoi.vector =
						vcpu->arch.pending_ioapic_eoi;
				r = 0;
				goto out;
			}
		}
		if (kvm_check_request(KVM_REQ_SCAN_IOAPIC, vcpu))
			vcpu_scan_ioapic(vcpu);
		if (kvm_check_request(KVM_REQ_LOAD_EOI_EXITMAP, vcpu))
			vcpu_load_eoi_exitmap(vcpu);
		if (kvm_check_request(KVM_REQ_APIC_PAGE_RELOAD, vcpu))
			kvm_vcpu_reload_apic_access_page(vcpu);
#ifdef CONFIG_KVM_HYPERV
		if (kvm_check_request(KVM_REQ_HV_CRASH, vcpu)) {
			vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
			vcpu->run->system_event.type = KVM_SYSTEM_EVENT_CRASH;
			vcpu->run->system_event.ndata = 0;
			r = 0;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_HV_RESET, vcpu)) {
			vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
			vcpu->run->system_event.type = KVM_SYSTEM_EVENT_RESET;
			vcpu->run->system_event.ndata = 0;
			r = 0;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_HV_EXIT, vcpu)) {
			struct kvm_vcpu_hv *hv_vcpu = to_hv_vcpu(vcpu);

			vcpu->run->exit_reason = KVM_EXIT_HYPERV;
			vcpu->run->hyperv = hv_vcpu->exit;
			r = 0;
			goto out;
		}

		/*
		 * KVM_REQ_HV_STIMER has to be processed after
		 * KVM_REQ_CLOCK_UPDATE, because Hyper-V SynIC timers
		 * depend on the guest clock being up-to-date
		 */
		if (kvm_check_request(KVM_REQ_HV_STIMER, vcpu))
			kvm_hv_process_stimers(vcpu);
#endif
		if (kvm_check_request(KVM_REQ_APICV_UPDATE, vcpu))
			kvm_vcpu_update_apicv(vcpu);
		if (kvm_check_request(KVM_REQ_APF_READY, vcpu))
			kvm_check_async_pf_completion(vcpu);
		if (kvm_check_request(KVM_REQ_MSR_FILTER_CHANGED, vcpu))
			kvm_x86_call(msr_filter_changed)(vcpu);

		if (kvm_check_request(KVM_REQ_UPDATE_CPU_DIRTY_LOGGING, vcpu))
			kvm_x86_call(update_cpu_dirty_logging)(vcpu);

		if (kvm_check_request(KVM_REQ_UPDATE_PROTECTED_GUEST_STATE, vcpu)) {
			kvm_vcpu_reset(vcpu, true);
			if (vcpu->arch.mp_state != KVM_MP_STATE_RUNNABLE) {
				r = 1;
				goto out;
			}
		}
	}

	if (kvm_check_request(KVM_REQ_EVENT, vcpu) || req_int_win ||
	    kvm_xen_has_interrupt(vcpu)) {
		++vcpu->stat.req_event;
		r = kvm_apic_accept_events(vcpu);
		if (r < 0) {
			r = 0;
			goto out;
		}
		if (vcpu->arch.mp_state == KVM_MP_STATE_INIT_RECEIVED) {
			r = 1;
			goto out;
		}

		r = kvm_check_and_inject_events(vcpu, &req_immediate_exit);
		if (r < 0) {
			r = 0;
			goto out;
		}
		if (req_int_win)
			kvm_x86_call(enable_irq_window)(vcpu);

		if (kvm_lapic_enabled(vcpu)) {
			update_cr8_intercept(vcpu);
			kvm_lapic_sync_to_vapic(vcpu);
		}
	}

	r = kvm_mmu_reload(vcpu);
	if (unlikely(r)) {
		goto cancel_injection;
	}

	preempt_disable();

	kvm_x86_call(prepare_switch_to_guest)(vcpu);

	/*
	 * Disable IRQs before setting IN_GUEST_MODE.  Posted interrupt
	 * IPI are then delayed after guest entry, which ensures that they
	 * result in virtual interrupt delivery.
	 */
	local_irq_disable();

	/* Store vcpu->apicv_active before vcpu->mode.  */
	smp_store_release(&vcpu->mode, IN_GUEST_MODE);

	kvm_vcpu_srcu_read_unlock(vcpu);

	/*
	 * 1) We should set ->mode before checking ->requests.  Please see
	 * the comment in kvm_vcpu_exiting_guest_mode().
	 *
	 * 2) For APICv, we should set ->mode before checking PID.ON. This
	 * pairs with the memory barrier implicit in pi_test_and_set_on
	 * (see vmx_deliver_posted_interrupt).
	 *
	 * 3) This also orders the write to mode from any reads to the page
	 * tables done while the VCPU is running.  Please see the comment
	 * in kvm_flush_remote_tlbs.
	 */
	smp_mb__after_srcu_read_unlock();

	/*
	 * Process pending posted interrupts to handle the case where the
	 * notification IRQ arrived in the host, or was never sent (because the
	 * target vCPU wasn't running).  Do this regardless of the vCPU's APICv
	 * status, KVM doesn't update assigned devices when APICv is inhibited,
	 * i.e. they can post interrupts even if APICv is temporarily disabled.
	 */
	if (kvm_lapic_enabled(vcpu))
		kvm_x86_call(sync_pir_to_irr)(vcpu);

	if (kvm_vcpu_exit_request(vcpu)) {
		vcpu->mode = OUTSIDE_GUEST_MODE;
		smp_wmb();
		local_irq_enable();
		preempt_enable();
		kvm_vcpu_srcu_read_lock(vcpu);
		r = 1;
		goto cancel_injection;
	}

	if (req_immediate_exit)
		kvm_make_request(KVM_REQ_EVENT, vcpu);

	fpregs_assert_state_consistent();
	if (test_thread_flag(TIF_NEED_FPU_LOAD))
		switch_fpu_return();

	if (vcpu->arch.guest_fpu.xfd_err)
		wrmsrl(MSR_IA32_XFD_ERR, vcpu->arch.guest_fpu.xfd_err);

	if (unlikely(vcpu->arch.switch_db_regs)) {
		set_debugreg(0, 7);
		set_debugreg(vcpu->arch.eff_db[0], 0);
		set_debugreg(vcpu->arch.eff_db[1], 1);
		set_debugreg(vcpu->arch.eff_db[2], 2);
		set_debugreg(vcpu->arch.eff_db[3], 3);
	} else if (unlikely(hw_breakpoint_active())) {
		set_debugreg(0, 7);
	}

	guest_timing_enter_irqoff();

	for (;;) {
		/*
		 * Assert that vCPU vs. VM APICv state is consistent.  An APICv
		 * update must kick and wait for all vCPUs before toggling the
		 * per-VM state, and responding vCPUs must wait for the update
		 * to complete before servicing KVM_REQ_APICV_UPDATE.
		 */
		WARN_ON_ONCE((kvm_vcpu_apicv_activated(vcpu) != kvm_vcpu_apicv_active(vcpu)) &&
			     (kvm_get_apic_mode(vcpu) != LAPIC_MODE_DISABLED));

		exit_fastpath = kvm_x86_call(vcpu_run)(vcpu,
						       req_immediate_exit);
		if (likely(exit_fastpath != EXIT_FASTPATH_REENTER_GUEST))
			break;

		if (kvm_lapic_enabled(vcpu))
			kvm_x86_call(sync_pir_to_irr)(vcpu);

		if (unlikely(kvm_vcpu_exit_request(vcpu))) {
			exit_fastpath = EXIT_FASTPATH_EXIT_HANDLED;
			break;
		}

		/* Note, VM-Exits that go down the "slow" path are accounted below. */
		++vcpu->stat.exits;
	}

	/*
	 * Do this here before restoring debug registers on the host.  And
	 * since we do this before handling the vmexit, a DR access vmexit
	 * can (a) read the correct value of the debug registers, (b) set
	 * KVM_DEBUGREG_WONT_EXIT again.
	 */
	if (unlikely(vcpu->arch.switch_db_regs & KVM_DEBUGREG_WONT_EXIT)) {
		WARN_ON(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP);
		kvm_x86_call(sync_dirty_debug_regs)(vcpu);
		kvm_update_dr0123(vcpu);
		kvm_update_dr7(vcpu);
	}

	/*
	 * If the guest has used debug registers, at least dr7
	 * will be disabled while returning to the host.
	 * If we don't have active breakpoints in the host, we don't
	 * care about the messed up debug address registers. But if
	 * we have some of them active, restore the old state.
	 */
	if (hw_breakpoint_active())
		hw_breakpoint_restore();

	vcpu->arch.last_vmentry_cpu = vcpu->cpu;
	vcpu->arch.last_guest_tsc = kvm_read_l1_tsc(vcpu, rdtsc());

	vcpu->mode = OUTSIDE_GUEST_MODE;
	smp_wmb();

	/*
	 * Sync xfd before calling handle_exit_irqoff() which may
	 * rely on the fact that guest_fpu::xfd is up-to-date (e.g.
	 * in #NM irqoff handler).
	 */
	if (vcpu->arch.xfd_no_write_intercept)
		fpu_sync_guest_vmexit_xfd_state();

	kvm_x86_call(handle_exit_irqoff)(vcpu);

	if (vcpu->arch.guest_fpu.xfd_err)
		wrmsrl(MSR_IA32_XFD_ERR, 0);

	/*
	 * Consume any pending interrupts, including the possible source of
	 * VM-Exit on SVM and any ticks that occur between VM-Exit and now.
	 * An instruction is required after local_irq_enable() to fully unblock
	 * interrupts on processors that implement an interrupt shadow, the
	 * stat.exits increment will do nicely.
	 */
	kvm_before_interrupt(vcpu, KVM_HANDLING_IRQ);
	local_irq_enable();
	++vcpu->stat.exits;
	local_irq_disable();
	kvm_after_interrupt(vcpu);

	/*
	 * Wait until after servicing IRQs to account guest time so that any
	 * ticks that occurred while running the guest are properly accounted
	 * to the guest.  Waiting until IRQs are enabled degrades the accuracy
	 * of accounting via context tracking, but the loss of accuracy is
	 * acceptable for all known use cases.
	 */
	guest_timing_exit_irqoff();

	local_irq_enable();
	preempt_enable();

	kvm_vcpu_srcu_read_lock(vcpu);

	/*
	 * Call this to ensure WC buffers in guest are evicted after each VM
	 * Exit, so that the evicted WC writes can be snooped across all cpus
	 */
	smp_mb__after_srcu_read_lock();

	/*
	 * Profile KVM exit RIPs:
	 */
	if (unlikely(prof_on == KVM_PROFILING)) {
		unsigned long rip = kvm_rip_read(vcpu);
		profile_hit(KVM_PROFILING, (void *)rip);
	}

	if (unlikely(vcpu->arch.tsc_always_catchup))
		kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);

	if (vcpu->arch.apic_attention)
		kvm_lapic_sync_from_vapic(vcpu);

	if (unlikely(exit_fastpath == EXIT_FASTPATH_EXIT_USERSPACE))
		return 0;

	r = kvm_x86_call(handle_exit)(vcpu, exit_fastpath);
	return r;

cancel_injection:
	if (req_immediate_exit)
		kvm_make_request(KVM_REQ_EVENT, vcpu);
	kvm_x86_call(cancel_injection)(vcpu);
	if (unlikely(vcpu->arch.apic_attention))
		kvm_lapic_sync_from_vapic(vcpu);
out:
	return r;
}

static bool kvm_vcpu_running(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.mp_state == KVM_MP_STATE_RUNNABLE &&
		!vcpu->arch.apf.halted);
}

static bool kvm_vcpu_has_events(struct kvm_vcpu *vcpu)
{
	if (!list_empty_careful(&vcpu->async_pf.done))
		return true;

	if (kvm_apic_has_pending_init_or_sipi(vcpu) &&
	    kvm_apic_init_sipi_allowed(vcpu))
		return true;

	if (vcpu->arch.pv.pv_unhalted)
		return true;

	if (kvm_is_exception_pending(vcpu))
		return true;

	if (kvm_test_request(KVM_REQ_NMI, vcpu) ||
	    (vcpu->arch.nmi_pending &&
	     kvm_x86_call(nmi_allowed)(vcpu, false)))
		return true;

#ifdef CONFIG_KVM_SMM
	if (kvm_test_request(KVM_REQ_SMI, vcpu) ||
	    (vcpu->arch.smi_pending &&
	     kvm_x86_call(smi_allowed)(vcpu, false)))
		return true;
#endif

	if (kvm_test_request(KVM_REQ_PMI, vcpu))
		return true;

	if (kvm_test_request(KVM_REQ_UPDATE_PROTECTED_GUEST_STATE, vcpu))
		return true;

	if (kvm_arch_interrupt_allowed(vcpu) && kvm_cpu_has_interrupt(vcpu))
		return true;

	if (kvm_hv_has_stimer_pending(vcpu))
		return true;

	if (is_guest_mode(vcpu) &&
	    kvm_x86_ops.nested_ops->has_events &&
	    kvm_x86_ops.nested_ops->has_events(vcpu, false))
		return true;

	if (kvm_xen_has_pending_events(vcpu))
		return true;

	return false;
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_running(vcpu) || kvm_vcpu_has_events(vcpu);
}

/* Called within kvm->srcu read side.  */
static inline int vcpu_block(struct kvm_vcpu *vcpu)
{
	bool hv_timer;

	if (!kvm_arch_vcpu_runnable(vcpu)) {
		/*
		 * Switch to the software timer before halt-polling/blocking as
		 * the guest's timer may be a break event for the vCPU, and the
		 * hypervisor timer runs only when the CPU is in guest mode.
		 * Switch before halt-polling so that KVM recognizes an expired
		 * timer before blocking.
		 */
		hv_timer = kvm_lapic_hv_timer_in_use(vcpu);
		if (hv_timer)
			kvm_lapic_switch_to_sw_timer(vcpu);

		kvm_vcpu_srcu_read_unlock(vcpu);
		if (vcpu->arch.mp_state == KVM_MP_STATE_HALTED)
			kvm_vcpu_halt(vcpu);
		else
			kvm_vcpu_block(vcpu);
		kvm_vcpu_srcu_read_lock(vcpu);

		if (hv_timer)
			kvm_lapic_switch_to_hv_timer(vcpu);

		/*
		 * If the vCPU is not runnable, a signal or another host event
		 * of some kind is pending; service it without changing the
		 * vCPU's activity state.
		 */
		if (!kvm_arch_vcpu_runnable(vcpu))
			return 1;
	}

	/*
	 * Evaluate nested events before exiting the halted state.  This allows
	 * the halt state to be recorded properly in the VMCS12's activity
	 * state field (AMD does not have a similar field and a VM-Exit always
	 * causes a spurious wakeup from HLT).
	 */
	if (is_guest_mode(vcpu)) {
		int r = kvm_check_nested_events(vcpu);

		WARN_ON_ONCE(r == -EBUSY);
		if (r < 0)
			return 0;
	}

	if (kvm_apic_accept_events(vcpu) < 0)
		return 0;
	switch(vcpu->arch.mp_state) {
	case KVM_MP_STATE_HALTED:
	case KVM_MP_STATE_AP_RESET_HOLD:
		vcpu->arch.pv.pv_unhalted = false;
		vcpu->arch.mp_state =
			KVM_MP_STATE_RUNNABLE;
		fallthrough;
	case KVM_MP_STATE_RUNNABLE:
		vcpu->arch.apf.halted = false;
		break;
	case KVM_MP_STATE_INIT_RECEIVED:
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
	return 1;
}

/* Called within kvm->srcu read side.  */
static int vcpu_run(struct kvm_vcpu *vcpu)
{
	int r;

	vcpu->run->exit_reason = KVM_EXIT_UNKNOWN;

	for (;;) {
		/*
		 * If another guest vCPU requests a PV TLB flush in the middle
		 * of instruction emulation, the rest of the emulation could
		 * use a stale page translation. Assume that any code after
		 * this point can start executing an instruction.
		 */
		vcpu->arch.at_instruction_boundary = false;
		if (kvm_vcpu_running(vcpu)) {
			r = vcpu_enter_guest(vcpu);
		} else {
			r = vcpu_block(vcpu);
		}

		if (r <= 0)
			break;

		kvm_clear_request(KVM_REQ_UNBLOCK, vcpu);
		if (kvm_xen_has_pending_events(vcpu))
			kvm_xen_inject_pending_events(vcpu);

		if (kvm_cpu_has_pending_timer(vcpu))
			kvm_inject_pending_timer_irqs(vcpu);

		if (dm_request_for_irq_injection(vcpu) &&
			kvm_vcpu_ready_for_interrupt_injection(vcpu)) {
			r = 0;
			vcpu->run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
			++vcpu->stat.request_irq_exits;
			break;
		}

		if (__xfer_to_guest_mode_work_pending()) {
			kvm_vcpu_srcu_read_unlock(vcpu);
			r = xfer_to_guest_mode_handle_work(vcpu);
			kvm_vcpu_srcu_read_lock(vcpu);
			if (r)
				return r;
		}
	}

	return r;
}

static int __kvm_emulate_halt(struct kvm_vcpu *vcpu, int state, int reason)
{
	/*
	 * The vCPU has halted, e.g. executed HLT.  Update the run state if the
	 * local APIC is in-kernel, the run loop will detect the non-runnable
	 * state and halt the vCPU.  Exit to userspace if the local APIC is
	 * managed by userspace, in which case userspace is responsible for
	 * handling wake events.
	 */
	++vcpu->stat.halt_exits;
	if (lapic_in_kernel(vcpu)) {
		if (kvm_vcpu_has_events(vcpu))
			vcpu->arch.pv.pv_unhalted = false;
		else
			vcpu->arch.mp_state = state;
		return 1;
	} else {
		vcpu->run->exit_reason = reason;
		return 0;
	}
}

int kvm_emulate_halt_noskip(struct kvm_vcpu *vcpu)
{
	return __kvm_emulate_halt(vcpu, KVM_MP_STATE_HALTED, KVM_EXIT_HLT);
}
EXPORT_SYMBOL_GPL(kvm_emulate_halt_noskip);

int kvm_emulate_halt(struct kvm_vcpu *vcpu)
{
	int ret = kvm_skip_emulated_instruction(vcpu);
	/*
	 * TODO: we might be squashing a GUESTDBG_SINGLESTEP-triggered
	 * KVM_EXIT_DEBUG here.
	 */
	return kvm_emulate_halt_noskip(vcpu) && ret;
}
EXPORT_SYMBOL_GPL(kvm_emulate_halt);

fastpath_t handle_fastpath_hlt(struct kvm_vcpu *vcpu)
{
	int ret;

	kvm_vcpu_srcu_read_lock(vcpu);
	ret = kvm_emulate_halt(vcpu);
	kvm_vcpu_srcu_read_unlock(vcpu);

	if (!ret)
		return EXIT_FASTPATH_EXIT_USERSPACE;

	if (kvm_vcpu_running(vcpu))
		return EXIT_FASTPATH_REENTER_GUEST;

	return EXIT_FASTPATH_EXIT_HANDLED;
}
EXPORT_SYMBOL_GPL(handle_fastpath_hlt);

int kvm_emulate_ap_reset_hold(struct kvm_vcpu *vcpu)
{
	int ret = kvm_skip_emulated_instruction(vcpu);

	return __kvm_emulate_halt(vcpu, KVM_MP_STATE_AP_RESET_HOLD,
					KVM_EXIT_AP_RESET_HOLD) && ret;
}
EXPORT_SYMBOL_GPL(kvm_emulate_ap_reset_hold);

bool kvm_arch_dy_has_pending_interrupt(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_apicv_active(vcpu) &&
	       kvm_x86_call(dy_apicv_has_pending_interrupt)(vcpu);
}

bool kvm_arch_vcpu_preempted_in_kernel(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.preempted_in_kernel;
}

bool kvm_arch_dy_runnable(struct kvm_vcpu *vcpu)
{
	if (READ_ONCE(vcpu->arch.pv.pv_unhalted))
		return true;

	if (kvm_test_request(KVM_REQ_NMI, vcpu) ||
#ifdef CONFIG_KVM_SMM
		kvm_test_request(KVM_REQ_SMI, vcpu) ||
#endif
		 kvm_test_request(KVM_REQ_EVENT, vcpu))
		return true;

	return kvm_arch_dy_has_pending_interrupt(vcpu);
}

static inline int complete_emulated_io(struct kvm_vcpu *vcpu)
{
	return kvm_emulate_instruction(vcpu, EMULTYPE_NO_DECODE);
}

static int complete_emulated_pio(struct kvm_vcpu *vcpu)
{
	BUG_ON(!vcpu->arch.pio.count);

	return complete_emulated_io(vcpu);
}

/*
 * Implements the following, as a state machine:
 *
 * read:
 *   for each fragment
 *     for each mmio piece in the fragment
 *       write gpa, len
 *       exit
 *       copy data
 *   execute insn
 *
 * write:
 *   for each fragment
 *     for each mmio piece in the fragment
 *       write gpa, len
 *       copy data
 *       exit
 */
static int complete_emulated_mmio(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct kvm_mmio_fragment *frag;
	unsigned len;

	BUG_ON(!vcpu->mmio_needed);

	/* Complete previous fragment */
	frag = &vcpu->mmio_fragments[vcpu->mmio_cur_fragment];
	len = min(8u, frag->len);
	if (!vcpu->mmio_is_write)
		memcpy(frag->data, run->mmio.data, len);

	if (frag->len <= 8) {
		/* Switch to the next fragment. */
		frag++;
		vcpu->mmio_cur_fragment++;
	} else {
		/* Go forward to the next mmio piece. */
		frag->data += len;
		frag->gpa += len;
		frag->len -= len;
	}

	if (vcpu->mmio_cur_fragment >= vcpu->mmio_nr_fragments) {
		vcpu->mmio_needed = 0;

		/* FIXME: return into emulator if single-stepping.  */
		if (vcpu->mmio_is_write)
			return 1;
		vcpu->mmio_read_completed = 1;
		return complete_emulated_io(vcpu);
	}

	run->exit_reason = KVM_EXIT_MMIO;
	run->mmio.phys_addr = frag->gpa;
	if (vcpu->mmio_is_write)
		memcpy(run->mmio.data, frag->data, min(8u, frag->len));
	run->mmio.len = min(8u, frag->len);
	run->mmio.is_write = vcpu->mmio_is_write;
	vcpu->arch.complete_userspace_io = complete_emulated_mmio;
	return 0;
}

/* Swap (qemu) user FPU context for the guest FPU context. */
static void kvm_load_guest_fpu(struct kvm_vcpu *vcpu)
{
	/* Exclude PKRU, it's restored separately immediately after VM-Exit. */
	fpu_swap_kvm_fpstate(&vcpu->arch.guest_fpu, true);
	trace_kvm_fpu(1);
}

/* When vcpu_run ends, restore user space FPU context. */
static void kvm_put_guest_fpu(struct kvm_vcpu *vcpu)
{
	fpu_swap_kvm_fpstate(&vcpu->arch.guest_fpu, false);
	++vcpu->stat.fpu_reload;
	trace_kvm_fpu(0);
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	struct kvm_queued_exception *ex = &vcpu->arch.exception;
	struct kvm_run *kvm_run = vcpu->run;
	int r;

	vcpu_load(vcpu);
	kvm_sigset_activate(vcpu);
	kvm_run->flags = 0;
	kvm_load_guest_fpu(vcpu);

	kvm_vcpu_srcu_read_lock(vcpu);
	if (unlikely(vcpu->arch.mp_state == KVM_MP_STATE_UNINITIALIZED)) {
		if (!vcpu->wants_to_run) {
			r = -EINTR;
			goto out;
		}

		/*
		 * Don't bother switching APIC timer emulation from the
		 * hypervisor timer to the software timer, the only way for the
		 * APIC timer to be active is if userspace stuffed vCPU state,
		 * i.e. put the vCPU into a nonsensical state.  Only an INIT
		 * will transition the vCPU out of UNINITIALIZED (without more
		 * state stuffing from userspace), which will reset the local
		 * APIC and thus cancel the timer or drop the IRQ (if the timer
		 * already expired).
		 */
		kvm_vcpu_srcu_read_unlock(vcpu);
		kvm_vcpu_block(vcpu);
		kvm_vcpu_srcu_read_lock(vcpu);

		if (kvm_apic_accept_events(vcpu) < 0) {
			r = 0;
			goto out;
		}
		r = -EAGAIN;
		if (signal_pending(current)) {
			r = -EINTR;
			kvm_run->exit_reason = KVM_EXIT_INTR;
			++vcpu->stat.signal_exits;
		}
		goto out;
	}

	if ((kvm_run->kvm_valid_regs & ~KVM_SYNC_X86_VALID_FIELDS) ||
	    (kvm_run->kvm_dirty_regs & ~KVM_SYNC_X86_VALID_FIELDS)) {
		r = -EINVAL;
		goto out;
	}

	if (kvm_run->kvm_dirty_regs) {
		r = sync_regs(vcpu);
		if (r != 0)
			goto out;
	}

	/* re-sync apic's tpr */
	if (!lapic_in_kernel(vcpu)) {
		if (kvm_set_cr8(vcpu, kvm_run->cr8) != 0) {
			r = -EINVAL;
			goto out;
		}
	}

	/*
	 * If userspace set a pending exception and L2 is active, convert it to
	 * a pending VM-Exit if L1 wants to intercept the exception.
	 */
	if (vcpu->arch.exception_from_userspace && is_guest_mode(vcpu) &&
	    kvm_x86_ops.nested_ops->is_exception_vmexit(vcpu, ex->vector,
							ex->error_code)) {
		kvm_queue_exception_vmexit(vcpu, ex->vector,
					   ex->has_error_code, ex->error_code,
					   ex->has_payload, ex->payload);
		ex->injected = false;
		ex->pending = false;
	}
	vcpu->arch.exception_from_userspace = false;

	if (unlikely(vcpu->arch.complete_userspace_io)) {
		int (*cui)(struct kvm_vcpu *) = vcpu->arch.complete_userspace_io;
		vcpu->arch.complete_userspace_io = NULL;
		r = cui(vcpu);
		if (r <= 0)
			goto out;
	} else {
		WARN_ON_ONCE(vcpu->arch.pio.count);
		WARN_ON_ONCE(vcpu->mmio_needed);
	}

	if (!vcpu->wants_to_run) {
		r = -EINTR;
		goto out;
	}

	r = kvm_x86_call(vcpu_pre_run)(vcpu);
	if (r <= 0)
		goto out;

	r = vcpu_run(vcpu);

out:
	kvm_put_guest_fpu(vcpu);
	if (kvm_run->kvm_valid_regs)
		store_regs(vcpu);
	post_kvm_run_save(vcpu);
	kvm_vcpu_srcu_read_unlock(vcpu);

	kvm_sigset_deactivate(vcpu);
	vcpu_put(vcpu);
	return r;
}

static void __get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	if (vcpu->arch.emulate_regs_need_sync_to_vcpu) {
		/*
		 * We are here if userspace calls get_regs() in the middle of
		 * instruction emulation. Registers state needs to be copied
		 * back from emulation context to vcpu. Userspace shouldn't do
		 * that usually, but some bad designed PV devices (vmware
		 * backdoor interface) need this to work
		 */
		emulator_writeback_register_cache(vcpu->arch.emulate_ctxt);
		vcpu->arch.emulate_regs_need_sync_to_vcpu = false;
	}
	regs->rax = kvm_rax_read(vcpu);
	regs->rbx = kvm_rbx_read(vcpu);
	regs->rcx = kvm_rcx_read(vcpu);
	regs->rdx = kvm_rdx_read(vcpu);
	regs->rsi = kvm_rsi_read(vcpu);
	regs->rdi = kvm_rdi_read(vcpu);
	regs->rsp = kvm_rsp_read(vcpu);
	regs->rbp = kvm_rbp_read(vcpu);
#ifdef CONFIG_X86_64
	regs->r8 = kvm_r8_read(vcpu);
	regs->r9 = kvm_r9_read(vcpu);
	regs->r10 = kvm_r10_read(vcpu);
	regs->r11 = kvm_r11_read(vcpu);
	regs->r12 = kvm_r12_read(vcpu);
	regs->r13 = kvm_r13_read(vcpu);
	regs->r14 = kvm_r14_read(vcpu);
	regs->r15 = kvm_r15_read(vcpu);
#endif

	regs->rip = kvm_rip_read(vcpu);
	regs->rflags = kvm_get_rflags(vcpu);
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	vcpu_load(vcpu);
	__get_regs(vcpu, regs);
	vcpu_put(vcpu);
	return 0;
}

static void __set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	vcpu->arch.emulate_regs_need_sync_from_vcpu = true;
	vcpu->arch.emulate_regs_need_sync_to_vcpu = false;

	kvm_rax_write(vcpu, regs->rax);
	kvm_rbx_write(vcpu, regs->rbx);
	kvm_rcx_write(vcpu, regs->rcx);
	kvm_rdx_write(vcpu, regs->rdx);
	kvm_rsi_write(vcpu, regs->rsi);
	kvm_rdi_write(vcpu, regs->rdi);
	kvm_rsp_write(vcpu, regs->rsp);
	kvm_rbp_write(vcpu, regs->rbp);
#ifdef CONFIG_X86_64
	kvm_r8_write(vcpu, regs->r8);
	kvm_r9_write(vcpu, regs->r9);
	kvm_r10_write(vcpu, regs->r10);
	kvm_r11_write(vcpu, regs->r11);
	kvm_r12_write(vcpu, regs->r12);
	kvm_r13_write(vcpu, regs->r13);
	kvm_r14_write(vcpu, regs->r14);
	kvm_r15_write(vcpu, regs->r15);
#endif

	kvm_rip_write(vcpu, regs->rip);
	kvm_set_rflags(vcpu, regs->rflags | X86_EFLAGS_FIXED);

	vcpu->arch.exception.pending = false;
	vcpu->arch.exception_vmexit.pending = false;

	kvm_make_request(KVM_REQ_EVENT, vcpu);
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	vcpu_load(vcpu);
	__set_regs(vcpu, regs);
	vcpu_put(vcpu);
	return 0;
}

static void __get_sregs_common(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	struct desc_ptr dt;

	if (vcpu->arch.guest_state_protected)
		goto skip_protected_regs;

	kvm_get_segment(vcpu, &sregs->cs, VCPU_SREG_CS);
	kvm_get_segment(vcpu, &sregs->ds, VCPU_SREG_DS);
	kvm_get_segment(vcpu, &sregs->es, VCPU_SREG_ES);
	kvm_get_segment(vcpu, &sregs->fs, VCPU_SREG_FS);
	kvm_get_segment(vcpu, &sregs->gs, VCPU_SREG_GS);
	kvm_get_segment(vcpu, &sregs->ss, VCPU_SREG_SS);

	kvm_get_segment(vcpu, &sregs->tr, VCPU_SREG_TR);
	kvm_get_segment(vcpu, &sregs->ldt, VCPU_SREG_LDTR);

	kvm_x86_call(get_idt)(vcpu, &dt);
	sregs->idt.limit = dt.size;
	sregs->idt.base = dt.address;
	kvm_x86_call(get_gdt)(vcpu, &dt);
	sregs->gdt.limit = dt.size;
	sregs->gdt.base = dt.address;

	sregs->cr2 = vcpu->arch.cr2;
	sregs->cr3 = kvm_read_cr3(vcpu);

skip_protected_regs:
	sregs->cr0 = kvm_read_cr0(vcpu);
	sregs->cr4 = kvm_read_cr4(vcpu);
	sregs->cr8 = kvm_get_cr8(vcpu);
	sregs->efer = vcpu->arch.efer;
	sregs->apic_base = vcpu->arch.apic_base;
}

static void __get_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	__get_sregs_common(vcpu, sregs);

	if (vcpu->arch.guest_state_protected)
		return;

	if (vcpu->arch.interrupt.injected && !vcpu->arch.interrupt.soft)
		set_bit(vcpu->arch.interrupt.nr,
			(unsigned long *)sregs->interrupt_bitmap);
}

static void __get_sregs2(struct kvm_vcpu *vcpu, struct kvm_sregs2 *sregs2)
{
	int i;

	__get_sregs_common(vcpu, (struct kvm_sregs *)sregs2);

	if (vcpu->arch.guest_state_protected)
		return;

	if (is_pae_paging(vcpu)) {
		for (i = 0 ; i < 4 ; i++)
			sregs2->pdptrs[i] = kvm_pdptr_read(vcpu, i);
		sregs2->flags |= KVM_SREGS2_FLAGS_PDPTRS_VALID;
	}
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	vcpu_load(vcpu);
	__get_sregs(vcpu, sregs);
	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	int r;

	vcpu_load(vcpu);
	if (kvm_mpx_supported())
		kvm_load_guest_fpu(vcpu);

	r = kvm_apic_accept_events(vcpu);
	if (r < 0)
		goto out;
	r = 0;

	if ((vcpu->arch.mp_state == KVM_MP_STATE_HALTED ||
	     vcpu->arch.mp_state == KVM_MP_STATE_AP_RESET_HOLD) &&
	    vcpu->arch.pv.pv_unhalted)
		mp_state->mp_state = KVM_MP_STATE_RUNNABLE;
	else
		mp_state->mp_state = vcpu->arch.mp_state;

out:
	if (kvm_mpx_supported())
		kvm_put_guest_fpu(vcpu);
	vcpu_put(vcpu);
	return r;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	int ret = -EINVAL;

	vcpu_load(vcpu);

	switch (mp_state->mp_state) {
	case KVM_MP_STATE_UNINITIALIZED:
	case KVM_MP_STATE_HALTED:
	case KVM_MP_STATE_AP_RESET_HOLD:
	case KVM_MP_STATE_INIT_RECEIVED:
	case KVM_MP_STATE_SIPI_RECEIVED:
		if (!lapic_in_kernel(vcpu))
			goto out;
		break;

	case KVM_MP_STATE_RUNNABLE:
		break;

	default:
		goto out;
	}

	/*
	 * Pending INITs are reported using KVM_SET_VCPU_EVENTS, disallow
	 * forcing the guest into INIT/SIPI if those events are supposed to be
	 * blocked.  KVM prioritizes SMI over INIT, so reject INIT/SIPI state
	 * if an SMI is pending as well.
	 */
	if ((!kvm_apic_init_sipi_allowed(vcpu) || vcpu->arch.smi_pending) &&
	    (mp_state->mp_state == KVM_MP_STATE_SIPI_RECEIVED ||
	     mp_state->mp_state == KVM_MP_STATE_INIT_RECEIVED))
		goto out;

	if (mp_state->mp_state == KVM_MP_STATE_SIPI_RECEIVED) {
		vcpu->arch.mp_state = KVM_MP_STATE_INIT_RECEIVED;
		set_bit(KVM_APIC_SIPI, &vcpu->arch.apic->pending_events);
	} else
		vcpu->arch.mp_state = mp_state->mp_state;
	kvm_make_request(KVM_REQ_EVENT, vcpu);

	ret = 0;
out:
	vcpu_put(vcpu);
	return ret;
}

int kvm_task_switch(struct kvm_vcpu *vcpu, u16 tss_selector, int idt_index,
		    int reason, bool has_error_code, u32 error_code)
{
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;
	int ret;

	init_emulate_ctxt(vcpu);

	ret = emulator_task_switch(ctxt, tss_selector, idt_index, reason,
				   has_error_code, error_code);

	/*
	 * Report an error userspace if MMIO is needed, as KVM doesn't support
	 * MMIO during a task switch (or any other complex operation).
	 */
	if (ret || vcpu->mmio_needed) {
		vcpu->mmio_needed = false;
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror = KVM_INTERNAL_ERROR_EMULATION;
		vcpu->run->internal.ndata = 0;
		return 0;
	}

	kvm_rip_write(vcpu, ctxt->eip);
	kvm_set_rflags(vcpu, ctxt->eflags);
	return 1;
}
EXPORT_SYMBOL_GPL(kvm_task_switch);

static bool kvm_is_valid_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	if ((sregs->efer & EFER_LME) && (sregs->cr0 & X86_CR0_PG)) {
		/*
		 * When EFER.LME and CR0.PG are set, the processor is in
		 * 64-bit mode (though maybe in a 32-bit code segment).
		 * CR4.PAE and EFER.LMA must be set.
		 */
		if (!(sregs->cr4 & X86_CR4_PAE) || !(sregs->efer & EFER_LMA))
			return false;
		if (!kvm_vcpu_is_legal_cr3(vcpu, sregs->cr3))
			return false;
	} else {
		/*
		 * Not in 64-bit mode: EFER.LMA is clear and the code
		 * segment cannot be 64-bit.
		 */
		if (sregs->efer & EFER_LMA || sregs->cs.l)
			return false;
	}

	return kvm_is_valid_cr4(vcpu, sregs->cr4) &&
	       kvm_is_valid_cr0(vcpu, sregs->cr0);
}

static int __set_sregs_common(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs,
		int *mmu_reset_needed, bool update_pdptrs)
{
	int idx;
	struct desc_ptr dt;

	if (!kvm_is_valid_sregs(vcpu, sregs))
		return -EINVAL;

	if (kvm_apic_set_base(vcpu, sregs->apic_base, true))
		return -EINVAL;

	if (vcpu->arch.guest_state_protected)
		return 0;

	dt.size = sregs->idt.limit;
	dt.address = sregs->idt.base;
	kvm_x86_call(set_idt)(vcpu, &dt);
	dt.size = sregs->gdt.limit;
	dt.address = sregs->gdt.base;
	kvm_x86_call(set_gdt)(vcpu, &dt);

	vcpu->arch.cr2 = sregs->cr2;
	*mmu_reset_needed |= kvm_read_cr3(vcpu) != sregs->cr3;
	vcpu->arch.cr3 = sregs->cr3;
	kvm_register_mark_dirty(vcpu, VCPU_EXREG_CR3);
	kvm_x86_call(post_set_cr3)(vcpu, sregs->cr3);

	kvm_set_cr8(vcpu, sregs->cr8);

	*mmu_reset_needed |= vcpu->arch.efer != sregs->efer;
	kvm_x86_call(set_efer)(vcpu, sregs->efer);

	*mmu_reset_needed |= kvm_read_cr0(vcpu) != sregs->cr0;
	kvm_x86_call(set_cr0)(vcpu, sregs->cr0);

	*mmu_reset_needed |= kvm_read_cr4(vcpu) != sregs->cr4;
	kvm_x86_call(set_cr4)(vcpu, sregs->cr4);

	if (update_pdptrs) {
		idx = srcu_read_lock(&vcpu->kvm->srcu);
		if (is_pae_paging(vcpu)) {
			load_pdptrs(vcpu, kvm_read_cr3(vcpu));
			*mmu_reset_needed = 1;
		}
		srcu_read_unlock(&vcpu->kvm->srcu, idx);
	}

	kvm_set_segment(vcpu, &sregs->cs, VCPU_SREG_CS);
	kvm_set_segment(vcpu, &sregs->ds, VCPU_SREG_DS);
	kvm_set_segment(vcpu, &sregs->es, VCPU_SREG_ES);
	kvm_set_segment(vcpu, &sregs->fs, VCPU_SREG_FS);
	kvm_set_segment(vcpu, &sregs->gs, VCPU_SREG_GS);
	kvm_set_segment(vcpu, &sregs->ss, VCPU_SREG_SS);

	kvm_set_segment(vcpu, &sregs->tr, VCPU_SREG_TR);
	kvm_set_segment(vcpu, &sregs->ldt, VCPU_SREG_LDTR);

	update_cr8_intercept(vcpu);

	/* Older userspace won't unhalt the vcpu on reset. */
	if (kvm_vcpu_is_bsp(vcpu) && kvm_rip_read(vcpu) == 0xfff0 &&
	    sregs->cs.selector == 0xf000 && sregs->cs.base == 0xffff0000 &&
	    !is_protmode(vcpu))
		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;

	return 0;
}

static int __set_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	int pending_vec, max_bits;
	int mmu_reset_needed = 0;
	int ret = __set_sregs_common(vcpu, sregs, &mmu_reset_needed, true);

	if (ret)
		return ret;

	if (mmu_reset_needed) {
		kvm_mmu_reset_context(vcpu);
		kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);
	}

	max_bits = KVM_NR_INTERRUPTS;
	pending_vec = find_first_bit(
		(const unsigned long *)sregs->interrupt_bitmap, max_bits);

	if (pending_vec < max_bits) {
		kvm_queue_interrupt(vcpu, pending_vec, false);
		pr_debug("Set back pending irq %d\n", pending_vec);
		kvm_make_request(KVM_REQ_EVENT, vcpu);
	}
	return 0;
}

static int __set_sregs2(struct kvm_vcpu *vcpu, struct kvm_sregs2 *sregs2)
{
	int mmu_reset_needed = 0;
	bool valid_pdptrs = sregs2->flags & KVM_SREGS2_FLAGS_PDPTRS_VALID;
	bool pae = (sregs2->cr0 & X86_CR0_PG) && (sregs2->cr4 & X86_CR4_PAE) &&
		!(sregs2->efer & EFER_LMA);
	int i, ret;

	if (sregs2->flags & ~KVM_SREGS2_FLAGS_PDPTRS_VALID)
		return -EINVAL;

	if (valid_pdptrs && (!pae || vcpu->arch.guest_state_protected))
		return -EINVAL;

	ret = __set_sregs_common(vcpu, (struct kvm_sregs *)sregs2,
				 &mmu_reset_needed, !valid_pdptrs);
	if (ret)
		return ret;

	if (valid_pdptrs) {
		for (i = 0; i < 4 ; i++)
			kvm_pdptr_write(vcpu, i, sregs2->pdptrs[i]);

		kvm_register_mark_dirty(vcpu, VCPU_EXREG_PDPTR);
		mmu_reset_needed = 1;
		vcpu->arch.pdptrs_from_userspace = true;
	}
	if (mmu_reset_needed) {
		kvm_mmu_reset_context(vcpu);
		kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);
	}
	return 0;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	int ret;

	if (vcpu->kvm->arch.has_protected_state &&
	    vcpu->arch.guest_state_protected)
		return -EINVAL;

	vcpu_load(vcpu);
	ret = __set_sregs(vcpu, sregs);
	vcpu_put(vcpu);
	return ret;
}

static void kvm_arch_vcpu_guestdbg_update_apicv_inhibit(struct kvm *kvm)
{
	bool set = false;
	struct kvm_vcpu *vcpu;
	unsigned long i;

	if (!enable_apicv)
		return;

	down_write(&kvm->arch.apicv_update_lock);

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (vcpu->guest_debug & KVM_GUESTDBG_BLOCKIRQ) {
			set = true;
			break;
		}
	}
	__kvm_set_or_clear_apicv_inhibit(kvm, APICV_INHIBIT_REASON_BLOCKIRQ, set);
	up_write(&kvm->arch.apicv_update_lock);
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	unsigned long rflags;
	int i, r;

	if (vcpu->arch.guest_state_protected)
		return -EINVAL;

	vcpu_load(vcpu);

	if (dbg->control & (KVM_GUESTDBG_INJECT_DB | KVM_GUESTDBG_INJECT_BP)) {
		r = -EBUSY;
		if (kvm_is_exception_pending(vcpu))
			goto out;
		if (dbg->control & KVM_GUESTDBG_INJECT_DB)
			kvm_queue_exception(vcpu, DB_VECTOR);
		else
			kvm_queue_exception(vcpu, BP_VECTOR);
	}

	/*
	 * Read rflags as long as potentially injected trace flags are still
	 * filtered out.
	 */
	rflags = kvm_get_rflags(vcpu);

	vcpu->guest_debug = dbg->control;
	if (!(vcpu->guest_debug & KVM_GUESTDBG_ENABLE))
		vcpu->guest_debug = 0;

	if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP) {
		for (i = 0; i < KVM_NR_DB_REGS; ++i)
			vcpu->arch.eff_db[i] = dbg->arch.debugreg[i];
		vcpu->arch.guest_debug_dr7 = dbg->arch.debugreg[7];
	} else {
		for (i = 0; i < KVM_NR_DB_REGS; i++)
			vcpu->arch.eff_db[i] = vcpu->arch.db[i];
	}
	kvm_update_dr7(vcpu);

	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)
		vcpu->arch.singlestep_rip = kvm_get_linear_rip(vcpu);

	/*
	 * Trigger an rflags update that will inject or remove the trace
	 * flags.
	 */
	kvm_set_rflags(vcpu, rflags);

	kvm_x86_call(update_exception_bitmap)(vcpu);

	kvm_arch_vcpu_guestdbg_update_apicv_inhibit(vcpu->kvm);

	r = 0;

out:
	vcpu_put(vcpu);
	return r;
}

/*
 * Translate a guest virtual address to a guest physical address.
 */
int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				    struct kvm_translation *tr)
{
	unsigned long vaddr = tr->linear_address;
	gpa_t gpa;
	int idx;

	vcpu_load(vcpu);

	idx = srcu_read_lock(&vcpu->kvm->srcu);
	gpa = kvm_mmu_gva_to_gpa_system(vcpu, vaddr, NULL);
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	tr->physical_address = gpa;
	tr->valid = gpa != INVALID_GPA;
	tr->writeable = 1;
	tr->usermode = 0;

	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	struct fxregs_state *fxsave;

	if (fpstate_is_confidential(&vcpu->arch.guest_fpu))
		return vcpu->kvm->arch.has_protected_state ? -EINVAL : 0;

	vcpu_load(vcpu);

	fxsave = &vcpu->arch.guest_fpu.fpstate->regs.fxsave;
	memcpy(fpu->fpr, fxsave->st_space, 128);
	fpu->fcw = fxsave->cwd;
	fpu->fsw = fxsave->swd;
	fpu->ftwx = fxsave->twd;
	fpu->last_opcode = fxsave->fop;
	fpu->last_ip = fxsave->rip;
	fpu->last_dp = fxsave->rdp;
	memcpy(fpu->xmm, fxsave->xmm_space, sizeof(fxsave->xmm_space));

	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	struct fxregs_state *fxsave;

	if (fpstate_is_confidential(&vcpu->arch.guest_fpu))
		return vcpu->kvm->arch.has_protected_state ? -EINVAL : 0;

	vcpu_load(vcpu);

	fxsave = &vcpu->arch.guest_fpu.fpstate->regs.fxsave;

	memcpy(fxsave->st_space, fpu->fpr, 128);
	fxsave->cwd = fpu->fcw;
	fxsave->swd = fpu->fsw;
	fxsave->twd = fpu->ftwx;
	fxsave->fop = fpu->last_opcode;
	fxsave->rip = fpu->last_ip;
	fxsave->rdp = fpu->last_dp;
	memcpy(fxsave->xmm_space, fpu->xmm, sizeof(fxsave->xmm_space));

	vcpu_put(vcpu);
	return 0;
}

static void store_regs(struct kvm_vcpu *vcpu)
{
	BUILD_BUG_ON(sizeof(struct kvm_sync_regs) > SYNC_REGS_SIZE_BYTES);

	if (vcpu->run->kvm_valid_regs & KVM_SYNC_X86_REGS)
		__get_regs(vcpu, &vcpu->run->s.regs.regs);

	if (vcpu->run->kvm_valid_regs & KVM_SYNC_X86_SREGS)
		__get_sregs(vcpu, &vcpu->run->s.regs.sregs);

	if (vcpu->run->kvm_valid_regs & KVM_SYNC_X86_EVENTS)
		kvm_vcpu_ioctl_x86_get_vcpu_events(
				vcpu, &vcpu->run->s.regs.events);
}

static int sync_regs(struct kvm_vcpu *vcpu)
{
	if (vcpu->run->kvm_dirty_regs & KVM_SYNC_X86_REGS) {
		__set_regs(vcpu, &vcpu->run->s.regs.regs);
		vcpu->run->kvm_dirty_regs &= ~KVM_SYNC_X86_REGS;
	}

	if (vcpu->run->kvm_dirty_regs & KVM_SYNC_X86_SREGS) {
		struct kvm_sregs sregs = vcpu->run->s.regs.sregs;

		if (__set_sregs(vcpu, &sregs))
			return -EINVAL;

		vcpu->run->kvm_dirty_regs &= ~KVM_SYNC_X86_SREGS;
	}

	if (vcpu->run->kvm_dirty_regs & KVM_SYNC_X86_EVENTS) {
		struct kvm_vcpu_events events = vcpu->run->s.regs.events;

		if (kvm_vcpu_ioctl_x86_set_vcpu_events(vcpu, &events))
			return -EINVAL;

		vcpu->run->kvm_dirty_regs &= ~KVM_SYNC_X86_EVENTS;
	}

	return 0;
}

int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	if (kvm_check_tsc_unstable() && kvm->created_vcpus)
		pr_warn_once("SMP vm created on host with unstable TSC; "
			     "guest TSC will not be reliable\n");

	if (!kvm->arch.max_vcpu_ids)
		kvm->arch.max_vcpu_ids = KVM_MAX_VCPU_IDS;

	if (id >= kvm->arch.max_vcpu_ids)
		return -EINVAL;

	return kvm_x86_call(vcpu_precreate)(kvm);
}

int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	struct page *page;
	int r;

	vcpu->arch.last_vmentry_cpu = -1;
	vcpu->arch.regs_avail = ~0;
	vcpu->arch.regs_dirty = ~0;

	kvm_gpc_init(&vcpu->arch.pv_time, vcpu->kvm);

	if (!irqchip_in_kernel(vcpu->kvm) || kvm_vcpu_is_reset_bsp(vcpu))
		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
	else
		vcpu->arch.mp_state = KVM_MP_STATE_UNINITIALIZED;

	r = kvm_mmu_create(vcpu);
	if (r < 0)
		return r;

	r = kvm_create_lapic(vcpu);
	if (r < 0)
		goto fail_mmu_destroy;

	r = -ENOMEM;

	page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!page)
		goto fail_free_lapic;
	vcpu->arch.pio_data = page_address(page);

	vcpu->arch.mce_banks = kcalloc(KVM_MAX_MCE_BANKS * 4, sizeof(u64),
				       GFP_KERNEL_ACCOUNT);
	vcpu->arch.mci_ctl2_banks = kcalloc(KVM_MAX_MCE_BANKS, sizeof(u64),
					    GFP_KERNEL_ACCOUNT);
	if (!vcpu->arch.mce_banks || !vcpu->arch.mci_ctl2_banks)
		goto fail_free_mce_banks;
	vcpu->arch.mcg_cap = KVM_MAX_MCE_BANKS;

	if (!zalloc_cpumask_var(&vcpu->arch.wbinvd_dirty_mask,
				GFP_KERNEL_ACCOUNT))
		goto fail_free_mce_banks;

	if (!alloc_emulate_ctxt(vcpu))
		goto free_wbinvd_dirty_mask;

	if (!fpu_alloc_guest_fpstate(&vcpu->arch.guest_fpu)) {
		pr_err("failed to allocate vcpu's fpu\n");
		goto free_emulate_ctxt;
	}

	vcpu->arch.maxphyaddr = cpuid_query_maxphyaddr(vcpu);
	vcpu->arch.reserved_gpa_bits = kvm_vcpu_reserved_gpa_bits_raw(vcpu);

	kvm_async_pf_hash_reset(vcpu);

	if (kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_STUFF_FEATURE_MSRS)) {
		vcpu->arch.arch_capabilities = kvm_get_arch_capabilities();
		vcpu->arch.msr_platform_info = MSR_PLATFORM_INFO_CPUID_FAULT;
		vcpu->arch.perf_capabilities = kvm_caps.supported_perf_cap;
	}
	kvm_pmu_init(vcpu);

	vcpu->arch.pending_external_vector = -1;
	vcpu->arch.preempted_in_kernel = false;

#if IS_ENABLED(CONFIG_HYPERV)
	vcpu->arch.hv_root_tdp = INVALID_PAGE;
#endif

	r = kvm_x86_call(vcpu_create)(vcpu);
	if (r)
		goto free_guest_fpu;

	kvm_xen_init_vcpu(vcpu);
	vcpu_load(vcpu);
	kvm_set_tsc_khz(vcpu, vcpu->kvm->arch.default_tsc_khz);
	kvm_vcpu_reset(vcpu, false);
	kvm_init_mmu(vcpu);
	vcpu_put(vcpu);
	return 0;

free_guest_fpu:
	fpu_free_guest_fpstate(&vcpu->arch.guest_fpu);
free_emulate_ctxt:
	kmem_cache_free(x86_emulator_cache, vcpu->arch.emulate_ctxt);
free_wbinvd_dirty_mask:
	free_cpumask_var(vcpu->arch.wbinvd_dirty_mask);
fail_free_mce_banks:
	kfree(vcpu->arch.mce_banks);
	kfree(vcpu->arch.mci_ctl2_banks);
	free_page((unsigned long)vcpu->arch.pio_data);
fail_free_lapic:
	kvm_free_lapic(vcpu);
fail_mmu_destroy:
	kvm_mmu_destroy(vcpu);
	return r;
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	if (mutex_lock_killable(&vcpu->mutex))
		return;
	vcpu_load(vcpu);
	kvm_synchronize_tsc(vcpu, NULL);
	vcpu_put(vcpu);

	/* poll control enabled by default */
	vcpu->arch.msr_kvm_poll_control = 1;

	mutex_unlock(&vcpu->mutex);

	if (kvmclock_periodic_sync && vcpu->vcpu_idx == 0)
		schedule_delayed_work(&kvm->arch.kvmclock_sync_work,
						KVMCLOCK_SYNC_PERIOD);
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	int idx;

	kvmclock_reset(vcpu);

	kvm_x86_call(vcpu_free)(vcpu);

	kmem_cache_free(x86_emulator_cache, vcpu->arch.emulate_ctxt);
	free_cpumask_var(vcpu->arch.wbinvd_dirty_mask);
	fpu_free_guest_fpstate(&vcpu->arch.guest_fpu);

	kvm_xen_destroy_vcpu(vcpu);
	kvm_hv_vcpu_uninit(vcpu);
	kvm_pmu_destroy(vcpu);
	kfree(vcpu->arch.mce_banks);
	kfree(vcpu->arch.mci_ctl2_banks);
	kvm_free_lapic(vcpu);
	idx = srcu_read_lock(&vcpu->kvm->srcu);
	kvm_mmu_destroy(vcpu);
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	free_page((unsigned long)vcpu->arch.pio_data);
	kvfree(vcpu->arch.cpuid_entries);
}

void kvm_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event)
{
	struct kvm_cpuid_entry2 *cpuid_0x1;
	unsigned long old_cr0 = kvm_read_cr0(vcpu);
	unsigned long new_cr0;

	/*
	 * Several of the "set" flows, e.g. ->set_cr0(), read other registers
	 * to handle side effects.  RESET emulation hits those flows and relies
	 * on emulated/virtualized registers, including those that are loaded
	 * into hardware, to be zeroed at vCPU creation.  Use CRs as a sentinel
	 * to detect improper or missing initialization.
	 */
	WARN_ON_ONCE(!init_event &&
		     (old_cr0 || kvm_read_cr3(vcpu) || kvm_read_cr4(vcpu)));

	/*
	 * SVM doesn't unconditionally VM-Exit on INIT and SHUTDOWN, thus it's
	 * possible to INIT the vCPU while L2 is active.  Force the vCPU back
	 * into L1 as EFER.SVME is cleared on INIT (along with all other EFER
	 * bits), i.e. virtualization is disabled.
	 */
	if (is_guest_mode(vcpu))
		kvm_leave_nested(vcpu);

	kvm_lapic_reset(vcpu, init_event);

	WARN_ON_ONCE(is_guest_mode(vcpu) || is_smm(vcpu));
	vcpu->arch.hflags = 0;

	vcpu->arch.smi_pending = 0;
	vcpu->arch.smi_count = 0;
	atomic_set(&vcpu->arch.nmi_queued, 0);
	vcpu->arch.nmi_pending = 0;
	vcpu->arch.nmi_injected = false;
	kvm_clear_interrupt_queue(vcpu);
	kvm_clear_exception_queue(vcpu);

	memset(vcpu->arch.db, 0, sizeof(vcpu->arch.db));
	kvm_update_dr0123(vcpu);
	vcpu->arch.dr6 = DR6_ACTIVE_LOW;
	vcpu->arch.dr7 = DR7_FIXED_1;
	kvm_update_dr7(vcpu);

	vcpu->arch.cr2 = 0;

	kvm_make_request(KVM_REQ_EVENT, vcpu);
	vcpu->arch.apf.msr_en_val = 0;
	vcpu->arch.apf.msr_int_val = 0;
	vcpu->arch.st.msr_val = 0;

	kvmclock_reset(vcpu);

	kvm_clear_async_pf_completion_queue(vcpu);
	kvm_async_pf_hash_reset(vcpu);
	vcpu->arch.apf.halted = false;

	if (vcpu->arch.guest_fpu.fpstate && kvm_mpx_supported()) {
		struct fpstate *fpstate = vcpu->arch.guest_fpu.fpstate;

		/*
		 * All paths that lead to INIT are required to load the guest's
		 * FPU state (because most paths are buried in KVM_RUN).
		 */
		if (init_event)
			kvm_put_guest_fpu(vcpu);

		fpstate_clear_xstate_component(fpstate, XFEATURE_BNDREGS);
		fpstate_clear_xstate_component(fpstate, XFEATURE_BNDCSR);

		if (init_event)
			kvm_load_guest_fpu(vcpu);
	}

	if (!init_event) {
		vcpu->arch.smbase = 0x30000;

		vcpu->arch.pat = MSR_IA32_CR_PAT_DEFAULT;

		vcpu->arch.msr_misc_features_enables = 0;
		vcpu->arch.ia32_misc_enable_msr = MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL |
						  MSR_IA32_MISC_ENABLE_BTS_UNAVAIL;

		__kvm_set_xcr(vcpu, 0, XFEATURE_MASK_FP);
		__kvm_set_msr(vcpu, MSR_IA32_XSS, 0, true);
	}

	/* All GPRs except RDX (handled below) are zeroed on RESET/INIT. */
	memset(vcpu->arch.regs, 0, sizeof(vcpu->arch.regs));
	kvm_register_mark_dirty(vcpu, VCPU_REGS_RSP);

	/*
	 * Fall back to KVM's default Family/Model/Stepping of 0x600 (P6/Athlon)
	 * if no CPUID match is found.  Note, it's impossible to get a match at
	 * RESET since KVM emulates RESET before exposing the vCPU to userspace,
	 * i.e. it's impossible for kvm_find_cpuid_entry() to find a valid entry
	 * on RESET.  But, go through the motions in case that's ever remedied.
	 */
	cpuid_0x1 = kvm_find_cpuid_entry(vcpu, 1);
	kvm_rdx_write(vcpu, cpuid_0x1 ? cpuid_0x1->eax : 0x600);

	kvm_x86_call(vcpu_reset)(vcpu, init_event);

	kvm_set_rflags(vcpu, X86_EFLAGS_FIXED);
	kvm_rip_write(vcpu, 0xfff0);

	vcpu->arch.cr3 = 0;
	kvm_register_mark_dirty(vcpu, VCPU_EXREG_CR3);

	/*
	 * CR0.CD/NW are set on RESET, preserved on INIT.  Note, some versions
	 * of Intel's SDM list CD/NW as being set on INIT, but they contradict
	 * (or qualify) that with a footnote stating that CD/NW are preserved.
	 */
	new_cr0 = X86_CR0_ET;
	if (init_event)
		new_cr0 |= (old_cr0 & (X86_CR0_NW | X86_CR0_CD));
	else
		new_cr0 |= X86_CR0_NW | X86_CR0_CD;

	kvm_x86_call(set_cr0)(vcpu, new_cr0);
	kvm_x86_call(set_cr4)(vcpu, 0);
	kvm_x86_call(set_efer)(vcpu, 0);
	kvm_x86_call(update_exception_bitmap)(vcpu);

	/*
	 * On the standard CR0/CR4/EFER modification paths, there are several
	 * complex conditions determining whether the MMU has to be reset and/or
	 * which PCIDs have to be flushed.  However, CR0.WP and the paging-related
	 * bits in CR4 and EFER are irrelevant if CR0.PG was '0'; and a reset+flush
	 * is needed anyway if CR0.PG was '1' (which can only happen for INIT, as
	 * CR0 will be '0' prior to RESET).  So we only need to check CR0.PG here.
	 */
	if (old_cr0 & X86_CR0_PG) {
		kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);
		kvm_mmu_reset_context(vcpu);
	}

	/*
	 * Intel's SDM states that all TLB entries are flushed on INIT.  AMD's
	 * APM states the TLBs are untouched by INIT, but it also states that
	 * the TLBs are flushed on "External initialization of the processor."
	 * Flush the guest TLB regardless of vendor, there is no meaningful
	 * benefit in relying on the guest to flush the TLB immediately after
	 * INIT.  A spurious TLB flush is benign and likely negligible from a
	 * performance perspective.
	 */
	if (init_event)
		kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_reset);

void kvm_vcpu_deliver_sipi_vector(struct kvm_vcpu *vcpu, u8 vector)
{
	struct kvm_segment cs;

	kvm_get_segment(vcpu, &cs, VCPU_SREG_CS);
	cs.selector = vector << 8;
	cs.base = vector << 12;
	kvm_set_segment(vcpu, &cs, VCPU_SREG_CS);
	kvm_rip_write(vcpu, 0);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_deliver_sipi_vector);

void kvm_arch_enable_virtualization(void)
{
	cpu_emergency_register_virt_callback(kvm_x86_ops.emergency_disable_virtualization_cpu);
}

void kvm_arch_disable_virtualization(void)
{
	cpu_emergency_unregister_virt_callback(kvm_x86_ops.emergency_disable_virtualization_cpu);
}

int kvm_arch_enable_virtualization_cpu(void)
{
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
	unsigned long i;
	int ret;
	u64 local_tsc;
	u64 max_tsc = 0;
	bool stable, backwards_tsc = false;

	kvm_user_return_msr_cpu_online();

	ret = kvm_x86_check_processor_compatibility();
	if (ret)
		return ret;

	ret = kvm_x86_call(enable_virtualization_cpu)();
	if (ret != 0)
		return ret;

	local_tsc = rdtsc();
	stable = !kvm_check_tsc_unstable();
	list_for_each_entry(kvm, &vm_list, vm_list) {
		kvm_for_each_vcpu(i, vcpu, kvm) {
			if (!stable && vcpu->cpu == smp_processor_id())
				kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
			if (stable && vcpu->arch.last_host_tsc > local_tsc) {
				backwards_tsc = true;
				if (vcpu->arch.last_host_tsc > max_tsc)
					max_tsc = vcpu->arch.last_host_tsc;
			}
		}
	}

	/*
	 * Sometimes, even reliable TSCs go backwards.  This happens on
	 * platforms that reset TSC during suspend or hibernate actions, but
	 * maintain synchronization.  We must compensate.  Fortunately, we can
	 * detect that condition here, which happens early in CPU bringup,
	 * before any KVM threads can be running.  Unfortunately, we can't
	 * bring the TSCs fully up to date with real time, as we aren't yet far
	 * enough into CPU bringup that we know how much real time has actually
	 * elapsed; our helper function, ktime_get_boottime_ns() will be using boot
	 * variables that haven't been updated yet.
	 *
	 * So we simply find the maximum observed TSC above, then record the
	 * adjustment to TSC in each VCPU.  When the VCPU later gets loaded,
	 * the adjustment will be applied.  Note that we accumulate
	 * adjustments, in case multiple suspend cycles happen before some VCPU
	 * gets a chance to run again.  In the event that no KVM threads get a
	 * chance to run, we will miss the entire elapsed period, as we'll have
	 * reset last_host_tsc, so VCPUs will not have the TSC adjusted and may
	 * loose cycle time.  This isn't too big a deal, since the loss will be
	 * uniform across all VCPUs (not to mention the scenario is extremely
	 * unlikely). It is possible that a second hibernate recovery happens
	 * much faster than a first, causing the observed TSC here to be
	 * smaller; this would require additional padding adjustment, which is
	 * why we set last_host_tsc to the local tsc observed here.
	 *
	 * N.B. - this code below runs only on platforms with reliable TSC,
	 * as that is the only way backwards_tsc is set above.  Also note
	 * that this runs for ALL vcpus, which is not a bug; all VCPUs should
	 * have the same delta_cyc adjustment applied if backwards_tsc
	 * is detected.  Note further, this adjustment is only done once,
	 * as we reset last_host_tsc on all VCPUs to stop this from being
	 * called multiple times (one for each physical CPU bringup).
	 *
	 * Platforms with unreliable TSCs don't have to deal with this, they
	 * will be compensated by the logic in vcpu_load, which sets the TSC to
	 * catchup mode.  This will catchup all VCPUs to real time, but cannot
	 * guarantee that they stay in perfect synchronization.
	 */
	if (backwards_tsc) {
		u64 delta_cyc = max_tsc - local_tsc;
		list_for_each_entry(kvm, &vm_list, vm_list) {
			kvm->arch.backwards_tsc_observed = true;
			kvm_for_each_vcpu(i, vcpu, kvm) {
				vcpu->arch.tsc_offset_adjustment += delta_cyc;
				vcpu->arch.last_host_tsc = local_tsc;
				kvm_make_request(KVM_REQ_MASTERCLOCK_UPDATE, vcpu);
			}

			/*
			 * We have to disable TSC offset matching.. if you were
			 * booting a VM while issuing an S4 host suspend....
			 * you may have some problem.  Solving this issue is
			 * left as an exercise to the reader.
			 */
			kvm->arch.last_tsc_nsec = 0;
			kvm->arch.last_tsc_write = 0;
		}

	}
	return 0;
}

void kvm_arch_disable_virtualization_cpu(void)
{
	kvm_x86_call(disable_virtualization_cpu)();
	drop_user_return_notifiers();
}

bool kvm_vcpu_is_reset_bsp(struct kvm_vcpu *vcpu)
{
	return vcpu->kvm->arch.bsp_vcpu_id == vcpu->vcpu_id;
}

bool kvm_vcpu_is_bsp(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.apic_base & MSR_IA32_APICBASE_BSP) != 0;
}

void kvm_arch_free_vm(struct kvm *kvm)
{
#if IS_ENABLED(CONFIG_HYPERV)
	kfree(kvm->arch.hv_pa_pg);
#endif
	__kvm_arch_free_vm(kvm);
}


int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	int ret;
	unsigned long flags;

	if (!kvm_is_vm_type_supported(type))
		return -EINVAL;

	kvm->arch.vm_type = type;
	kvm->arch.has_private_mem =
		(type == KVM_X86_SW_PROTECTED_VM);
	/* Decided by the vendor code for other VM types.  */
	kvm->arch.pre_fault_allowed =
		type == KVM_X86_DEFAULT_VM || type == KVM_X86_SW_PROTECTED_VM;

	ret = kvm_page_track_init(kvm);
	if (ret)
		goto out;

	kvm_mmu_init_vm(kvm);

	ret = kvm_x86_call(vm_init)(kvm);
	if (ret)
		goto out_uninit_mmu;

	INIT_HLIST_HEAD(&kvm->arch.mask_notifier_list);
	atomic_set(&kvm->arch.noncoherent_dma_count, 0);

	/* Reserve bit 0 of irq_sources_bitmap for userspace irq source */
	set_bit(KVM_USERSPACE_IRQ_SOURCE_ID, &kvm->arch.irq_sources_bitmap);
	/* Reserve bit 1 of irq_sources_bitmap for irqfd-resampler */
	set_bit(KVM_IRQFD_RESAMPLE_IRQ_SOURCE_ID,
		&kvm->arch.irq_sources_bitmap);

	raw_spin_lock_init(&kvm->arch.tsc_write_lock);
	mutex_init(&kvm->arch.apic_map_lock);
	seqcount_raw_spinlock_init(&kvm->arch.pvclock_sc, &kvm->arch.tsc_write_lock);
	kvm->arch.kvmclock_offset = -get_kvmclock_base_ns();

	raw_spin_lock_irqsave(&kvm->arch.tsc_write_lock, flags);
	pvclock_update_vm_gtod_copy(kvm);
	raw_spin_unlock_irqrestore(&kvm->arch.tsc_write_lock, flags);

	kvm->arch.default_tsc_khz = max_tsc_khz ? : tsc_khz;
	kvm->arch.apic_bus_cycle_ns = APIC_BUS_CYCLE_NS_DEFAULT;
	kvm->arch.guest_can_read_msr_platform_info = true;
	kvm->arch.enable_pmu = enable_pmu;

#if IS_ENABLED(CONFIG_HYPERV)
	spin_lock_init(&kvm->arch.hv_root_tdp_lock);
	kvm->arch.hv_root_tdp = INVALID_PAGE;
#endif

	INIT_DELAYED_WORK(&kvm->arch.kvmclock_update_work, kvmclock_update_fn);
	INIT_DELAYED_WORK(&kvm->arch.kvmclock_sync_work, kvmclock_sync_fn);

	kvm_apicv_init(kvm);
	kvm_hv_init_vm(kvm);
	kvm_xen_init_vm(kvm);

	return 0;

out_uninit_mmu:
	kvm_mmu_uninit_vm(kvm);
	kvm_page_track_cleanup(kvm);
out:
	return ret;
}

int kvm_arch_post_init_vm(struct kvm *kvm)
{
	return kvm_mmu_post_init_vm(kvm);
}

static void kvm_unload_vcpu_mmu(struct kvm_vcpu *vcpu)
{
	vcpu_load(vcpu);
	kvm_mmu_unload(vcpu);
	vcpu_put(vcpu);
}

static void kvm_unload_vcpu_mmus(struct kvm *kvm)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		kvm_clear_async_pf_completion_queue(vcpu);
		kvm_unload_vcpu_mmu(vcpu);
	}
}

void kvm_arch_sync_events(struct kvm *kvm)
{
	cancel_delayed_work_sync(&kvm->arch.kvmclock_sync_work);
	cancel_delayed_work_sync(&kvm->arch.kvmclock_update_work);
	kvm_free_pit(kvm);
}

/**
 * __x86_set_memory_region: Setup KVM internal memory slot
 *
 * @kvm: the kvm pointer to the VM.
 * @id: the slot ID to setup.
 * @gpa: the GPA to install the slot (unused when @size == 0).
 * @size: the size of the slot. Set to zero to uninstall a slot.
 *
 * This function helps to setup a KVM internal memory slot.  Specify
 * @size > 0 to install a new slot, while @size == 0 to uninstall a
 * slot.  The return code can be one of the following:
 *
 *   HVA:           on success (uninstall will return a bogus HVA)
 *   -errno:        on error
 *
 * The caller should always use IS_ERR() to check the return value
 * before use.  Note, the KVM internal memory slots are guaranteed to
 * remain valid and unchanged until the VM is destroyed, i.e., the
 * GPA->HVA translation will not change.  However, the HVA is a user
 * address, i.e. its accessibility is not guaranteed, and must be
 * accessed via __copy_{to,from}_user().
 */
void __user * __x86_set_memory_region(struct kvm *kvm, int id, gpa_t gpa,
				      u32 size)
{
	int i, r;
	unsigned long hva, old_npages;
	struct kvm_memslots *slots = kvm_memslots(kvm);
	struct kvm_memory_slot *slot;

	/* Called with kvm->slots_lock held.  */
	if (WARN_ON(id >= KVM_MEM_SLOTS_NUM))
		return ERR_PTR_USR(-EINVAL);

	slot = id_to_memslot(slots, id);
	if (size) {
		if (slot && slot->npages)
			return ERR_PTR_USR(-EEXIST);

		/*
		 * MAP_SHARED to prevent internal slot pages from being moved
		 * by fork()/COW.
		 */
		hva = vm_mmap(NULL, 0, size, PROT_READ | PROT_WRITE,
			      MAP_SHARED | MAP_ANONYMOUS, 0);
		if (IS_ERR_VALUE(hva))
			return (void __user *)hva;
	} else {
		if (!slot || !slot->npages)
			return NULL;

		old_npages = slot->npages;
		hva = slot->userspace_addr;
	}

	for (i = 0; i < kvm_arch_nr_memslot_as_ids(kvm); i++) {
		struct kvm_userspace_memory_region2 m;

		m.slot = id | (i << 16);
		m.flags = 0;
		m.guest_phys_addr = gpa;
		m.userspace_addr = hva;
		m.memory_size = size;
		r = __kvm_set_memory_region(kvm, &m);
		if (r < 0)
			return ERR_PTR_USR(r);
	}

	if (!size)
		vm_munmap(hva, old_npages * PAGE_SIZE);

	return (void __user *)hva;
}
EXPORT_SYMBOL_GPL(__x86_set_memory_region);

void kvm_arch_pre_destroy_vm(struct kvm *kvm)
{
	kvm_mmu_pre_destroy_vm(kvm);
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	if (current->mm == kvm->mm) {
		/*
		 * Free memory regions allocated on behalf of userspace,
		 * unless the memory map has changed due to process exit
		 * or fd copying.
		 */
		mutex_lock(&kvm->slots_lock);
		__x86_set_memory_region(kvm, APIC_ACCESS_PAGE_PRIVATE_MEMSLOT,
					0, 0);
		__x86_set_memory_region(kvm, IDENTITY_PAGETABLE_PRIVATE_MEMSLOT,
					0, 0);
		__x86_set_memory_region(kvm, TSS_PRIVATE_MEMSLOT, 0, 0);
		mutex_unlock(&kvm->slots_lock);
	}
	kvm_unload_vcpu_mmus(kvm);
	kvm_x86_call(vm_destroy)(kvm);
	kvm_free_msr_filter(srcu_dereference_check(kvm->arch.msr_filter, &kvm->srcu, 1));
	kvm_pic_destroy(kvm);
	kvm_ioapic_destroy(kvm);
	kvm_destroy_vcpus(kvm);
	kvfree(rcu_dereference_check(kvm->arch.apic_map, 1));
	kfree(srcu_dereference_check(kvm->arch.pmu_event_filter, &kvm->srcu, 1));
	kvm_mmu_uninit_vm(kvm);
	kvm_page_track_cleanup(kvm);
	kvm_xen_destroy_vm(kvm);
	kvm_hv_destroy_vm(kvm);
}

static void memslot_rmap_free(struct kvm_memory_slot *slot)
{
	int i;

	for (i = 0; i < KVM_NR_PAGE_SIZES; ++i) {
		vfree(slot->arch.rmap[i]);
		slot->arch.rmap[i] = NULL;
	}
}

void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	int i;

	memslot_rmap_free(slot);

	for (i = 1; i < KVM_NR_PAGE_SIZES; ++i) {
		vfree(slot->arch.lpage_info[i - 1]);
		slot->arch.lpage_info[i - 1] = NULL;
	}

	kvm_page_track_free_memslot(slot);
}

int memslot_rmap_alloc(struct kvm_memory_slot *slot, unsigned long npages)
{
	const int sz = sizeof(*slot->arch.rmap[0]);
	int i;

	for (i = 0; i < KVM_NR_PAGE_SIZES; ++i) {
		int level = i + 1;
		int lpages = __kvm_mmu_slot_lpages(slot, npages, level);

		if (slot->arch.rmap[i])
			continue;

		slot->arch.rmap[i] = __vcalloc(lpages, sz, GFP_KERNEL_ACCOUNT);
		if (!slot->arch.rmap[i]) {
			memslot_rmap_free(slot);
			return -ENOMEM;
		}
	}

	return 0;
}

static int kvm_alloc_memslot_metadata(struct kvm *kvm,
				      struct kvm_memory_slot *slot)
{
	unsigned long npages = slot->npages;
	int i, r;

	/*
	 * Clear out the previous array pointers for the KVM_MR_MOVE case.  The
	 * old arrays will be freed by __kvm_set_memory_region() if installing
	 * the new memslot is successful.
	 */
	memset(&slot->arch, 0, sizeof(slot->arch));

	if (kvm_memslots_have_rmaps(kvm)) {
		r = memslot_rmap_alloc(slot, npages);
		if (r)
			return r;
	}

	for (i = 1; i < KVM_NR_PAGE_SIZES; ++i) {
		struct kvm_lpage_info *linfo;
		unsigned long ugfn;
		int lpages;
		int level = i + 1;

		lpages = __kvm_mmu_slot_lpages(slot, npages, level);

		linfo = __vcalloc(lpages, sizeof(*linfo), GFP_KERNEL_ACCOUNT);
		if (!linfo)
			goto out_free;

		slot->arch.lpage_info[i - 1] = linfo;

		if (slot->base_gfn & (KVM_PAGES_PER_HPAGE(level) - 1))
			linfo[0].disallow_lpage = 1;
		if ((slot->base_gfn + npages) & (KVM_PAGES_PER_HPAGE(level) - 1))
			linfo[lpages - 1].disallow_lpage = 1;
		ugfn = slot->userspace_addr >> PAGE_SHIFT;
		/*
		 * If the gfn and userspace address are not aligned wrt each
		 * other, disable large page support for this slot.
		 */
		if ((slot->base_gfn ^ ugfn) & (KVM_PAGES_PER_HPAGE(level) - 1)) {
			unsigned long j;

			for (j = 0; j < lpages; ++j)
				linfo[j].disallow_lpage = 1;
		}
	}

#ifdef CONFIG_KVM_GENERIC_MEMORY_ATTRIBUTES
	kvm_mmu_init_memslot_memory_attributes(kvm, slot);
#endif

	if (kvm_page_track_create_memslot(kvm, slot, npages))
		goto out_free;

	return 0;

out_free:
	memslot_rmap_free(slot);

	for (i = 1; i < KVM_NR_PAGE_SIZES; ++i) {
		vfree(slot->arch.lpage_info[i - 1]);
		slot->arch.lpage_info[i - 1] = NULL;
	}
	return -ENOMEM;
}

void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen)
{
	struct kvm_vcpu *vcpu;
	unsigned long i;

	/*
	 * memslots->generation has been incremented.
	 * mmio generation may have reached its maximum value.
	 */
	kvm_mmu_invalidate_mmio_sptes(kvm, gen);

	/* Force re-initialization of steal_time cache */
	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_vcpu_kick(vcpu);
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				   const struct kvm_memory_slot *old,
				   struct kvm_memory_slot *new,
				   enum kvm_mr_change change)
{
	/*
	 * KVM doesn't support moving memslots when there are external page
	 * trackers attached to the VM, i.e. if KVMGT is in use.
	 */
	if (change == KVM_MR_MOVE && kvm_page_track_has_external_user(kvm))
		return -EINVAL;

	if (change == KVM_MR_CREATE || change == KVM_MR_MOVE) {
		if ((new->base_gfn + new->npages - 1) > kvm_mmu_max_gfn())
			return -EINVAL;

		return kvm_alloc_memslot_metadata(kvm, new);
	}

	if (change == KVM_MR_FLAGS_ONLY)
		memcpy(&new->arch, &old->arch, sizeof(old->arch));
	else if (WARN_ON_ONCE(change != KVM_MR_DELETE))
		return -EIO;

	return 0;
}


static void kvm_mmu_update_cpu_dirty_logging(struct kvm *kvm, bool enable)
{
	int nr_slots;

	if (!kvm_x86_ops.cpu_dirty_log_size)
		return;

	nr_slots = atomic_read(&kvm->nr_memslots_dirty_logging);
	if ((enable && nr_slots == 1) || !nr_slots)
		kvm_make_all_cpus_request(kvm, KVM_REQ_UPDATE_CPU_DIRTY_LOGGING);
}

static void kvm_mmu_slot_apply_flags(struct kvm *kvm,
				     struct kvm_memory_slot *old,
				     const struct kvm_memory_slot *new,
				     enum kvm_mr_change change)
{
	u32 old_flags = old ? old->flags : 0;
	u32 new_flags = new ? new->flags : 0;
	bool log_dirty_pages = new_flags & KVM_MEM_LOG_DIRTY_PAGES;

	/*
	 * Update CPU dirty logging if dirty logging is being toggled.  This
	 * applies to all operations.
	 */
	if ((old_flags ^ new_flags) & KVM_MEM_LOG_DIRTY_PAGES)
		kvm_mmu_update_cpu_dirty_logging(kvm, log_dirty_pages);

	/*
	 * Nothing more to do for RO slots (which can't be dirtied and can't be
	 * made writable) or CREATE/MOVE/DELETE of a slot.
	 *
	 * For a memslot with dirty logging disabled:
	 * CREATE:      No dirty mappings will already exist.
	 * MOVE/DELETE: The old mappings will already have been cleaned up by
	 *		kvm_arch_flush_shadow_memslot()
	 *
	 * For a memslot with dirty logging enabled:
	 * CREATE:      No shadow pages exist, thus nothing to write-protect
	 *		and no dirty bits to clear.
	 * MOVE/DELETE: The old mappings will already have been cleaned up by
	 *		kvm_arch_flush_shadow_memslot().
	 */
	if ((change != KVM_MR_FLAGS_ONLY) || (new_flags & KVM_MEM_READONLY))
		return;

	/*
	 * READONLY and non-flags changes were filtered out above, and the only
	 * other flag is LOG_DIRTY_PAGES, i.e. something is wrong if dirty
	 * logging isn't being toggled on or off.
	 */
	if (WARN_ON_ONCE(!((old_flags ^ new_flags) & KVM_MEM_LOG_DIRTY_PAGES)))
		return;

	if (!log_dirty_pages) {
		/*
		 * Recover huge page mappings in the slot now that dirty logging
		 * is disabled, i.e. now that KVM does not have to track guest
		 * writes at 4KiB granularity.
		 *
		 * Dirty logging might be disabled by userspace if an ongoing VM
		 * live migration is cancelled and the VM must continue running
		 * on the source.
		 */
		kvm_mmu_recover_huge_pages(kvm, new);
	} else {
		/*
		 * Initially-all-set does not require write protecting any page,
		 * because they're all assumed to be dirty.
		 */
		if (kvm_dirty_log_manual_protect_and_init_set(kvm))
			return;

		if (READ_ONCE(eager_page_split))
			kvm_mmu_slot_try_split_huge_pages(kvm, new, PG_LEVEL_4K);

		if (kvm_x86_ops.cpu_dirty_log_size) {
			kvm_mmu_slot_leaf_clear_dirty(kvm, new);
			kvm_mmu_slot_remove_write_access(kvm, new, PG_LEVEL_2M);
		} else {
			kvm_mmu_slot_remove_write_access(kvm, new, PG_LEVEL_4K);
		}

		/*
		 * Unconditionally flush the TLBs after enabling dirty logging.
		 * A flush is almost always going to be necessary (see below),
		 * and unconditionally flushing allows the helpers to omit
		 * the subtly complex checks when removing write access.
		 *
		 * Do the flush outside of mmu_lock to reduce the amount of
		 * time mmu_lock is held.  Flushing after dropping mmu_lock is
		 * safe as KVM only needs to guarantee the slot is fully
		 * write-protected before returning to userspace, i.e. before
		 * userspace can consume the dirty status.
		 *
		 * Flushing outside of mmu_lock requires KVM to be careful when
		 * making decisions based on writable status of an SPTE, e.g. a
		 * !writable SPTE doesn't guarantee a CPU can't perform writes.
		 *
		 * Specifically, KVM also write-protects guest page tables to
		 * monitor changes when using shadow paging, and must guarantee
		 * no CPUs can write to those page before mmu_lock is dropped.
		 * Because CPUs may have stale TLB entries at this point, a
		 * !writable SPTE doesn't guarantee CPUs can't perform writes.
		 *
		 * KVM also allows making SPTES writable outside of mmu_lock,
		 * e.g. to allow dirty logging without taking mmu_lock.
		 *
		 * To handle these scenarios, KVM uses a separate software-only
		 * bit (MMU-writable) to track if a SPTE is !writable due to
		 * a guest page table being write-protected (KVM clears the
		 * MMU-writable flag when write-protecting for shadow paging).
		 *
		 * The use of MMU-writable is also the primary motivation for
		 * the unconditional flush.  Because KVM must guarantee that a
		 * CPU doesn't contain stale, writable TLB entries for a
		 * !MMU-writable SPTE, KVM must flush if it encounters any
		 * MMU-writable SPTE regardless of whether the actual hardware
		 * writable bit was set.  I.e. KVM is almost guaranteed to need
		 * to flush, while unconditionally flushing allows the "remove
		 * write access" helpers to ignore MMU-writable entirely.
		 *
		 * See is_writable_pte() for more details (the case involving
		 * access-tracked SPTEs is particularly relevant).
		 */
		kvm_flush_remote_tlbs_memslot(kvm, new);
	}
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	if (change == KVM_MR_DELETE)
		kvm_page_track_delete_slot(kvm, old);

	if (!kvm->arch.n_requested_mmu_pages &&
	    (change == KVM_MR_CREATE || change == KVM_MR_DELETE)) {
		unsigned long nr_mmu_pages;

		nr_mmu_pages = kvm->nr_memslot_pages / KVM_MEMSLOT_PAGES_TO_MMU_PAGES_RATIO;
		nr_mmu_pages = max(nr_mmu_pages, KVM_MIN_ALLOC_MMU_PAGES);
		kvm_mmu_change_mmu_pages(kvm, nr_mmu_pages);
	}

	kvm_mmu_slot_apply_flags(kvm, old, new, change);

	/* Free the arrays associated with the old memslot. */
	if (change == KVM_MR_MOVE)
		kvm_arch_free_memslot(kvm, old);
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	WARN_ON_ONCE(!kvm_arch_pmi_in_guest(vcpu));

	if (vcpu->arch.guest_state_protected)
		return true;

	return kvm_x86_call(get_cpl)(vcpu) == 0;
}

unsigned long kvm_arch_vcpu_get_ip(struct kvm_vcpu *vcpu)
{
	WARN_ON_ONCE(!kvm_arch_pmi_in_guest(vcpu));

	if (vcpu->arch.guest_state_protected)
		return 0;

	return kvm_rip_read(vcpu);
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_exiting_guest_mode(vcpu) == IN_GUEST_MODE;
}

int kvm_arch_interrupt_allowed(struct kvm_vcpu *vcpu)
{
	return kvm_x86_call(interrupt_allowed)(vcpu, false);
}

unsigned long kvm_get_linear_rip(struct kvm_vcpu *vcpu)
{
	/* Can't read the RIP when guest state is protected, just return 0 */
	if (vcpu->arch.guest_state_protected)
		return 0;

	if (is_64_bit_mode(vcpu))
		return kvm_rip_read(vcpu);
	return (u32)(get_segment_base(vcpu, VCPU_SREG_CS) +
		     kvm_rip_read(vcpu));
}
EXPORT_SYMBOL_GPL(kvm_get_linear_rip);

bool kvm_is_linear_rip(struct kvm_vcpu *vcpu, unsigned long linear_rip)
{
	return kvm_get_linear_rip(vcpu) == linear_rip;
}
EXPORT_SYMBOL_GPL(kvm_is_linear_rip);

unsigned long kvm_get_rflags(struct kvm_vcpu *vcpu)
{
	unsigned long rflags;

	rflags = kvm_x86_call(get_rflags)(vcpu);
	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)
		rflags &= ~X86_EFLAGS_TF;
	return rflags;
}
EXPORT_SYMBOL_GPL(kvm_get_rflags);

static void __kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP &&
	    kvm_is_linear_rip(vcpu, vcpu->arch.singlestep_rip))
		rflags |= X86_EFLAGS_TF;
	kvm_x86_call(set_rflags)(vcpu, rflags);
}

void kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	__kvm_set_rflags(vcpu, rflags);
	kvm_make_request(KVM_REQ_EVENT, vcpu);
}
EXPORT_SYMBOL_GPL(kvm_set_rflags);

static inline u32 kvm_async_pf_hash_fn(gfn_t gfn)
{
	BUILD_BUG_ON(!is_power_of_2(ASYNC_PF_PER_VCPU));

	return hash_32(gfn & 0xffffffff, order_base_2(ASYNC_PF_PER_VCPU));
}

static inline u32 kvm_async_pf_next_probe(u32 key)
{
	return (key + 1) & (ASYNC_PF_PER_VCPU - 1);
}

static void kvm_add_async_pf_gfn(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	u32 key = kvm_async_pf_hash_fn(gfn);

	while (vcpu->arch.apf.gfns[key] != ~0)
		key = kvm_async_pf_next_probe(key);

	vcpu->arch.apf.gfns[key] = gfn;
}

static u32 kvm_async_pf_gfn_slot(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	int i;
	u32 key = kvm_async_pf_hash_fn(gfn);

	for (i = 0; i < ASYNC_PF_PER_VCPU &&
		     (vcpu->arch.apf.gfns[key] != gfn &&
		      vcpu->arch.apf.gfns[key] != ~0); i++)
		key = kvm_async_pf_next_probe(key);

	return key;
}

bool kvm_find_async_pf_gfn(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	return vcpu->arch.apf.gfns[kvm_async_pf_gfn_slot(vcpu, gfn)] == gfn;
}

static void kvm_del_async_pf_gfn(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	u32 i, j, k;

	i = j = kvm_async_pf_gfn_slot(vcpu, gfn);

	if (WARN_ON_ONCE(vcpu->arch.apf.gfns[i] != gfn))
		return;

	while (true) {
		vcpu->arch.apf.gfns[i] = ~0;
		do {
			j = kvm_async_pf_next_probe(j);
			if (vcpu->arch.apf.gfns[j] == ~0)
				return;
			k = kvm_async_pf_hash_fn(vcpu->arch.apf.gfns[j]);
			/*
			 * k lies cyclically in ]i,j]
			 * |    i.k.j |
			 * |....j i.k.| or  |.k..j i...|
			 */
		} while ((i <= j) ? (i < k && k <= j) : (i < k || k <= j));
		vcpu->arch.apf.gfns[i] = vcpu->arch.apf.gfns[j];
		i = j;
	}
}

static inline int apf_put_user_notpresent(struct kvm_vcpu *vcpu)
{
	u32 reason = KVM_PV_REASON_PAGE_NOT_PRESENT;

	return kvm_write_guest_cached(vcpu->kvm, &vcpu->arch.apf.data, &reason,
				      sizeof(reason));
}

static inline int apf_put_user_ready(struct kvm_vcpu *vcpu, u32 token)
{
	unsigned int offset = offsetof(struct kvm_vcpu_pv_apf_data, token);

	return kvm_write_guest_offset_cached(vcpu->kvm, &vcpu->arch.apf.data,
					     &token, offset, sizeof(token));
}

static inline bool apf_pageready_slot_free(struct kvm_vcpu *vcpu)
{
	unsigned int offset = offsetof(struct kvm_vcpu_pv_apf_data, token);
	u32 val;

	if (kvm_read_guest_offset_cached(vcpu->kvm, &vcpu->arch.apf.data,
					 &val, offset, sizeof(val)))
		return false;

	return !val;
}

static bool kvm_can_deliver_async_pf(struct kvm_vcpu *vcpu)
{

	if (!kvm_pv_async_pf_enabled(vcpu))
		return false;

	if (vcpu->arch.apf.send_user_only &&
	    kvm_x86_call(get_cpl)(vcpu) == 0)
		return false;

	if (is_guest_mode(vcpu)) {
		/*
		 * L1 needs to opt into the special #PF vmexits that are
		 * used to deliver async page faults.
		 */
		return vcpu->arch.apf.delivery_as_pf_vmexit;
	} else {
		/*
		 * Play it safe in case the guest temporarily disables paging.
		 * The real mode IDT in particular is unlikely to have a #PF
		 * exception setup.
		 */
		return is_paging(vcpu);
	}
}

bool kvm_can_do_async_pf(struct kvm_vcpu *vcpu)
{
	if (unlikely(!lapic_in_kernel(vcpu) ||
		     kvm_event_needs_reinjection(vcpu) ||
		     kvm_is_exception_pending(vcpu)))
		return false;

	if (kvm_hlt_in_guest(vcpu->kvm) && !kvm_can_deliver_async_pf(vcpu))
		return false;

	/*
	 * If interrupts are off we cannot even use an artificial
	 * halt state.
	 */
	return kvm_arch_interrupt_allowed(vcpu);
}

bool kvm_arch_async_page_not_present(struct kvm_vcpu *vcpu,
				     struct kvm_async_pf *work)
{
	struct x86_exception fault;

	trace_kvm_async_pf_not_present(work->arch.token, work->cr2_or_gpa);
	kvm_add_async_pf_gfn(vcpu, work->arch.gfn);

	if (kvm_can_deliver_async_pf(vcpu) &&
	    !apf_put_user_notpresent(vcpu)) {
		fault.vector = PF_VECTOR;
		fault.error_code_valid = true;
		fault.error_code = 0;
		fault.nested_page_fault = false;
		fault.address = work->arch.token;
		fault.async_page_fault = true;
		kvm_inject_page_fault(vcpu, &fault);
		return true;
	} else {
		/*
		 * It is not possible to deliver a paravirtualized asynchronous
		 * page fault, but putting the guest in an artificial halt state
		 * can be beneficial nevertheless: if an interrupt arrives, we
		 * can deliver it timely and perhaps the guest will schedule
		 * another process.  When the instruction that triggered a page
		 * fault is retried, hopefully the page will be ready in the host.
		 */
		kvm_make_request(KVM_REQ_APF_HALT, vcpu);
		return false;
	}
}

void kvm_arch_async_page_present(struct kvm_vcpu *vcpu,
				 struct kvm_async_pf *work)
{
	struct kvm_lapic_irq irq = {
		.delivery_mode = APIC_DM_FIXED,
		.vector = vcpu->arch.apf.vec
	};

	if (work->wakeup_all)
		work->arch.token = ~0; /* broadcast wakeup */
	else
		kvm_del_async_pf_gfn(vcpu, work->arch.gfn);
	trace_kvm_async_pf_ready(work->arch.token, work->cr2_or_gpa);

	if ((work->wakeup_all || work->notpresent_injected) &&
	    kvm_pv_async_pf_enabled(vcpu) &&
	    !apf_put_user_ready(vcpu, work->arch.token)) {
		vcpu->arch.apf.pageready_pending = true;
		kvm_apic_set_irq(vcpu, &irq, NULL);
	}

	vcpu->arch.apf.halted = false;
	vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
}

void kvm_arch_async_page_present_queued(struct kvm_vcpu *vcpu)
{
	kvm_make_request(KVM_REQ_APF_READY, vcpu);
	if (!vcpu->arch.apf.pageready_pending)
		kvm_vcpu_kick(vcpu);
}

bool kvm_arch_can_dequeue_async_page_present(struct kvm_vcpu *vcpu)
{
	if (!kvm_pv_async_pf_enabled(vcpu))
		return true;
	else
		return kvm_lapic_enabled(vcpu) && apf_pageready_slot_free(vcpu);
}

void kvm_arch_start_assignment(struct kvm *kvm)
{
	if (atomic_inc_return(&kvm->arch.assigned_device_count) == 1)
		kvm_x86_call(pi_start_assignment)(kvm);
}
EXPORT_SYMBOL_GPL(kvm_arch_start_assignment);

void kvm_arch_end_assignment(struct kvm *kvm)
{
	atomic_dec(&kvm->arch.assigned_device_count);
}
EXPORT_SYMBOL_GPL(kvm_arch_end_assignment);

bool noinstr kvm_arch_has_assigned_device(struct kvm *kvm)
{
	return raw_atomic_read(&kvm->arch.assigned_device_count);
}
EXPORT_SYMBOL_GPL(kvm_arch_has_assigned_device);

static void kvm_noncoherent_dma_assignment_start_or_stop(struct kvm *kvm)
{
	/*
	 * Non-coherent DMA assignment and de-assignment may affect whether or
	 * not KVM honors guest PAT, and thus may cause changes in EPT SPTEs
	 * due to toggling the "ignore PAT" bit.  Zap all SPTEs when the first
	 * (or last) non-coherent device is (un)registered to so that new SPTEs
	 * with the correct "ignore guest PAT" setting are created.
	 */
	if (kvm_mmu_may_ignore_guest_pat())
		kvm_zap_gfn_range(kvm, gpa_to_gfn(0), gpa_to_gfn(~0ULL));
}

void kvm_arch_register_noncoherent_dma(struct kvm *kvm)
{
	if (atomic_inc_return(&kvm->arch.noncoherent_dma_count) == 1)
		kvm_noncoherent_dma_assignment_start_or_stop(kvm);
}
EXPORT_SYMBOL_GPL(kvm_arch_register_noncoherent_dma);

void kvm_arch_unregister_noncoherent_dma(struct kvm *kvm)
{
	if (!atomic_dec_return(&kvm->arch.noncoherent_dma_count))
		kvm_noncoherent_dma_assignment_start_or_stop(kvm);
}
EXPORT_SYMBOL_GPL(kvm_arch_unregister_noncoherent_dma);

bool kvm_arch_has_noncoherent_dma(struct kvm *kvm)
{
	return atomic_read(&kvm->arch.noncoherent_dma_count);
}
EXPORT_SYMBOL_GPL(kvm_arch_has_noncoherent_dma);

bool kvm_arch_has_irq_bypass(void)
{
	return enable_apicv && irq_remapping_cap(IRQ_POSTING_CAP);
}

int kvm_arch_irq_bypass_add_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);
	int ret;

	irqfd->producer = prod;
	kvm_arch_start_assignment(irqfd->kvm);
	ret = kvm_x86_call(pi_update_irte)(irqfd->kvm,
					   prod->irq, irqfd->gsi, 1);
	if (ret)
		kvm_arch_end_assignment(irqfd->kvm);

	return ret;
}

void kvm_arch_irq_bypass_del_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	int ret;
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	WARN_ON(irqfd->producer != prod);
	irqfd->producer = NULL;

	/*
	 * When producer of consumer is unregistered, we change back to
	 * remapped mode, so we can re-use the current implementation
	 * when the irq is masked/disabled or the consumer side (KVM
	 * int this case doesn't want to receive the interrupts.
	*/
	ret = kvm_x86_call(pi_update_irte)(irqfd->kvm,
					   prod->irq, irqfd->gsi, 0);
	if (ret)
		printk(KERN_INFO "irq bypass consumer (token %p) unregistration"
		       " fails: %d\n", irqfd->consumer.token, ret);

	kvm_arch_end_assignment(irqfd->kvm);
}

int kvm_arch_update_irqfd_routing(struct kvm *kvm, unsigned int host_irq,
				   uint32_t guest_irq, bool set)
{
	return kvm_x86_call(pi_update_irte)(kvm, host_irq, guest_irq, set);
}

bool kvm_arch_irqfd_route_changed(struct kvm_kernel_irq_routing_entry *old,
				  struct kvm_kernel_irq_routing_entry *new)
{
	if (new->type != KVM_IRQ_ROUTING_MSI)
		return true;

	return !!memcmp(&old->msi, &new->msi, sizeof(new->msi));
}

bool kvm_vector_hashing_enabled(void)
{
	return vector_hashing;
}

bool kvm_arch_no_poll(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.msr_kvm_poll_control & 1) == 0;
}
EXPORT_SYMBOL_GPL(kvm_arch_no_poll);

#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_PREPARE
int kvm_arch_gmem_prepare(struct kvm *kvm, gfn_t gfn, kvm_pfn_t pfn, int max_order)
{
	return kvm_x86_call(gmem_prepare)(kvm, pfn, gfn, max_order);
}
#endif

#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_INVALIDATE
void kvm_arch_gmem_invalidate(kvm_pfn_t start, kvm_pfn_t end)
{
	kvm_x86_call(gmem_invalidate)(start, end);
}
#endif

int kvm_spec_ctrl_test_value(u64 value)
{
	/*
	 * test that setting IA32_SPEC_CTRL to given value
	 * is allowed by the host processor
	 */

	u64 saved_value;
	unsigned long flags;
	int ret = 0;

	local_irq_save(flags);

	if (rdmsrl_safe(MSR_IA32_SPEC_CTRL, &saved_value))
		ret = 1;
	else if (wrmsrl_safe(MSR_IA32_SPEC_CTRL, value))
		ret = 1;
	else
		wrmsrl(MSR_IA32_SPEC_CTRL, saved_value);

	local_irq_restore(flags);

	return ret;
}
EXPORT_SYMBOL_GPL(kvm_spec_ctrl_test_value);

void kvm_fixup_and_inject_pf_error(struct kvm_vcpu *vcpu, gva_t gva, u16 error_code)
{
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;
	struct x86_exception fault;
	u64 access = error_code &
		(PFERR_WRITE_MASK | PFERR_FETCH_MASK | PFERR_USER_MASK);

	if (!(error_code & PFERR_PRESENT_MASK) ||
	    mmu->gva_to_gpa(vcpu, mmu, gva, access, &fault) != INVALID_GPA) {
		/*
		 * If vcpu->arch.walk_mmu->gva_to_gpa succeeded, the page
		 * tables probably do not match the TLB.  Just proceed
		 * with the error code that the processor gave.
		 */
		fault.vector = PF_VECTOR;
		fault.error_code_valid = true;
		fault.error_code = error_code;
		fault.nested_page_fault = false;
		fault.address = gva;
		fault.async_page_fault = false;
	}
	vcpu->arch.walk_mmu->inject_page_fault(vcpu, &fault);
}
EXPORT_SYMBOL_GPL(kvm_fixup_and_inject_pf_error);

/*
 * Handles kvm_read/write_guest_virt*() result and either injects #PF or returns
 * KVM_EXIT_INTERNAL_ERROR for cases not currently handled by KVM. Return value
 * indicates whether exit to userspace is needed.
 */
int kvm_handle_memory_failure(struct kvm_vcpu *vcpu, int r,
			      struct x86_exception *e)
{
	if (r == X86EMUL_PROPAGATE_FAULT) {
		if (KVM_BUG_ON(!e, vcpu->kvm))
			return -EIO;

		kvm_inject_emulated_page_fault(vcpu, e);
		return 1;
	}

	/*
	 * In case kvm_read/write_guest_virt*() failed with X86EMUL_IO_NEEDED
	 * while handling a VMX instruction KVM could've handled the request
	 * correctly by exiting to userspace and performing I/O but there
	 * doesn't seem to be a real use-case behind such requests, just return
	 * KVM_EXIT_INTERNAL_ERROR for now.
	 */
	kvm_prepare_emulation_failure_exit(vcpu);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_handle_memory_failure);

int kvm_handle_invpcid(struct kvm_vcpu *vcpu, unsigned long type, gva_t gva)
{
	bool pcid_enabled;
	struct x86_exception e;
	struct {
		u64 pcid;
		u64 gla;
	} operand;
	int r;

	r = kvm_read_guest_virt(vcpu, gva, &operand, sizeof(operand), &e);
	if (r != X86EMUL_CONTINUE)
		return kvm_handle_memory_failure(vcpu, r, &e);

	if (operand.pcid >> 12 != 0) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	pcid_enabled = kvm_is_cr4_bit_set(vcpu, X86_CR4_PCIDE);

	switch (type) {
	case INVPCID_TYPE_INDIV_ADDR:
		/*
		 * LAM doesn't apply to addresses that are inputs to TLB
		 * invalidation.
		 */
		if ((!pcid_enabled && (operand.pcid != 0)) ||
		    is_noncanonical_invlpg_address(operand.gla, vcpu)) {
			kvm_inject_gp(vcpu, 0);
			return 1;
		}
		kvm_mmu_invpcid_gva(vcpu, operand.gla, operand.pcid);
		return kvm_skip_emulated_instruction(vcpu);

	case INVPCID_TYPE_SINGLE_CTXT:
		if (!pcid_enabled && (operand.pcid != 0)) {
			kvm_inject_gp(vcpu, 0);
			return 1;
		}

		kvm_invalidate_pcid(vcpu, operand.pcid);
		return kvm_skip_emulated_instruction(vcpu);

	case INVPCID_TYPE_ALL_NON_GLOBAL:
		/*
		 * Currently, KVM doesn't mark global entries in the shadow
		 * page tables, so a non-global flush just degenerates to a
		 * global flush. If needed, we could optimize this later by
		 * keeping track of global entries in shadow page tables.
		 */

		fallthrough;
	case INVPCID_TYPE_ALL_INCL_GLOBAL:
		kvm_make_request(KVM_REQ_TLB_FLUSH_GUEST, vcpu);
		return kvm_skip_emulated_instruction(vcpu);

	default:
		kvm_inject_gp(vcpu, 0);
		return 1;
	}
}
EXPORT_SYMBOL_GPL(kvm_handle_invpcid);

static int complete_sev_es_emulated_mmio(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct kvm_mmio_fragment *frag;
	unsigned int len;

	BUG_ON(!vcpu->mmio_needed);

	/* Complete previous fragment */
	frag = &vcpu->mmio_fragments[vcpu->mmio_cur_fragment];
	len = min(8u, frag->len);
	if (!vcpu->mmio_is_write)
		memcpy(frag->data, run->mmio.data, len);

	if (frag->len <= 8) {
		/* Switch to the next fragment. */
		frag++;
		vcpu->mmio_cur_fragment++;
	} else {
		/* Go forward to the next mmio piece. */
		frag->data += len;
		frag->gpa += len;
		frag->len -= len;
	}

	if (vcpu->mmio_cur_fragment >= vcpu->mmio_nr_fragments) {
		vcpu->mmio_needed = 0;

		// VMG change, at this point, we're always done
		// RIP has already been advanced
		return 1;
	}

	// More MMIO is needed
	run->mmio.phys_addr = frag->gpa;
	run->mmio.len = min(8u, frag->len);
	run->mmio.is_write = vcpu->mmio_is_write;
	if (run->mmio.is_write)
		memcpy(run->mmio.data, frag->data, min(8u, frag->len));
	run->exit_reason = KVM_EXIT_MMIO;

	vcpu->arch.complete_userspace_io = complete_sev_es_emulated_mmio;

	return 0;
}

int kvm_sev_es_mmio_write(struct kvm_vcpu *vcpu, gpa_t gpa, unsigned int bytes,
			  void *data)
{
	int handled;
	struct kvm_mmio_fragment *frag;

	if (!data)
		return -EINVAL;

	handled = write_emultor.read_write_mmio(vcpu, gpa, bytes, data);
	if (handled == bytes)
		return 1;

	bytes -= handled;
	gpa += handled;
	data += handled;

	/*TODO: Check if need to increment number of frags */
	frag = vcpu->mmio_fragments;
	vcpu->mmio_nr_fragments = 1;
	frag->len = bytes;
	frag->gpa = gpa;
	frag->data = data;

	vcpu->mmio_needed = 1;
	vcpu->mmio_cur_fragment = 0;

	vcpu->run->mmio.phys_addr = gpa;
	vcpu->run->mmio.len = min(8u, frag->len);
	vcpu->run->mmio.is_write = 1;
	memcpy(vcpu->run->mmio.data, frag->data, min(8u, frag->len));
	vcpu->run->exit_reason = KVM_EXIT_MMIO;

	vcpu->arch.complete_userspace_io = complete_sev_es_emulated_mmio;

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_sev_es_mmio_write);

int kvm_sev_es_mmio_read(struct kvm_vcpu *vcpu, gpa_t gpa, unsigned int bytes,
			 void *data)
{
	int handled;
	struct kvm_mmio_fragment *frag;

	if (!data)
		return -EINVAL;

	handled = read_emultor.read_write_mmio(vcpu, gpa, bytes, data);
	if (handled == bytes)
		return 1;

	bytes -= handled;
	gpa += handled;
	data += handled;

	/*TODO: Check if need to increment number of frags */
	frag = vcpu->mmio_fragments;
	vcpu->mmio_nr_fragments = 1;
	frag->len = bytes;
	frag->gpa = gpa;
	frag->data = data;

	vcpu->mmio_needed = 1;
	vcpu->mmio_cur_fragment = 0;

	vcpu->run->mmio.phys_addr = gpa;
	vcpu->run->mmio.len = min(8u, frag->len);
	vcpu->run->mmio.is_write = 0;
	vcpu->run->exit_reason = KVM_EXIT_MMIO;

	vcpu->arch.complete_userspace_io = complete_sev_es_emulated_mmio;

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_sev_es_mmio_read);

static void advance_sev_es_emulated_pio(struct kvm_vcpu *vcpu, unsigned count, int size)
{
	vcpu->arch.sev_pio_count -= count;
	vcpu->arch.sev_pio_data += count * size;
}

static int kvm_sev_es_outs(struct kvm_vcpu *vcpu, unsigned int size,
			   unsigned int port);

static int complete_sev_es_emulated_outs(struct kvm_vcpu *vcpu)
{
	int size = vcpu->arch.pio.size;
	int port = vcpu->arch.pio.port;

	vcpu->arch.pio.count = 0;
	if (vcpu->arch.sev_pio_count)
		return kvm_sev_es_outs(vcpu, size, port);
	return 1;
}

static int kvm_sev_es_outs(struct kvm_vcpu *vcpu, unsigned int size,
			   unsigned int port)
{
	for (;;) {
		unsigned int count =
			min_t(unsigned int, PAGE_SIZE / size, vcpu->arch.sev_pio_count);
		int ret = emulator_pio_out(vcpu, size, port, vcpu->arch.sev_pio_data, count);

		/* memcpy done already by emulator_pio_out.  */
		advance_sev_es_emulated_pio(vcpu, count, size);
		if (!ret)
			break;

		/* Emulation done by the kernel.  */
		if (!vcpu->arch.sev_pio_count)
			return 1;
	}

	vcpu->arch.complete_userspace_io = complete_sev_es_emulated_outs;
	return 0;
}

static int kvm_sev_es_ins(struct kvm_vcpu *vcpu, unsigned int size,
			  unsigned int port);

static int complete_sev_es_emulated_ins(struct kvm_vcpu *vcpu)
{
	unsigned count = vcpu->arch.pio.count;
	int size = vcpu->arch.pio.size;
	int port = vcpu->arch.pio.port;

	complete_emulator_pio_in(vcpu, vcpu->arch.sev_pio_data);
	advance_sev_es_emulated_pio(vcpu, count, size);
	if (vcpu->arch.sev_pio_count)
		return kvm_sev_es_ins(vcpu, size, port);
	return 1;
}

static int kvm_sev_es_ins(struct kvm_vcpu *vcpu, unsigned int size,
			  unsigned int port)
{
	for (;;) {
		unsigned int count =
			min_t(unsigned int, PAGE_SIZE / size, vcpu->arch.sev_pio_count);
		if (!emulator_pio_in(vcpu, size, port, vcpu->arch.sev_pio_data, count))
			break;

		/* Emulation done by the kernel.  */
		advance_sev_es_emulated_pio(vcpu, count, size);
		if (!vcpu->arch.sev_pio_count)
			return 1;
	}

	vcpu->arch.complete_userspace_io = complete_sev_es_emulated_ins;
	return 0;
}

int kvm_sev_es_string_io(struct kvm_vcpu *vcpu, unsigned int size,
			 unsigned int port, void *data,  unsigned int count,
			 int in)
{
	vcpu->arch.sev_pio_data = data;
	vcpu->arch.sev_pio_count = count;
	return in ? kvm_sev_es_ins(vcpu, size, port)
		  : kvm_sev_es_outs(vcpu, size, port);
}
EXPORT_SYMBOL_GPL(kvm_sev_es_string_io);

EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_entry);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_exit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_fast_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_inj_virq);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_page_fault);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_msr);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_cr);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_vmenter);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_vmexit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_vmexit_inject);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_intr_vmexit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_vmenter_failed);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_invlpga);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_skinit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_intercepts);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_write_tsc_offset);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_ple_window_update);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_pml_full);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_pi_irte_update);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_avic_unaccelerated_access);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_avic_incomplete_ipi);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_avic_ga_log);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_avic_kick_vcpu_slowpath);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_avic_doorbell);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_apicv_accept_irq);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_vmgexit_enter);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_vmgexit_exit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_vmgexit_msr_protocol_enter);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_vmgexit_msr_protocol_exit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_rmp_fault);

static int __init kvm_x86_init(void)
{
	kvm_mmu_x86_module_init();
	mitigate_smt_rsb &= boot_cpu_has_bug(X86_BUG_SMT_RSB) && cpu_smt_possible();
	return 0;
}
module_init(kvm_x86_init);

static void __exit kvm_x86_exit(void)
{
	WARN_ON_ONCE(static_branch_unlikely(&kvm_has_noapic_vcpu));
}
module_exit(kvm_x86_exit);

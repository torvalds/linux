// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kvm_host.h>
#include <asm/intel_pt.h>
#include <asm/vmx.h>

#include "hyperv.h"
#include "lapic.h"
#include "msrs.h"
#include "pmu.h"
#include "trace.h"
#include "vmx/vmx.h"
#include "xen.h"
#include "x86.h"

bool __read_mostly ignore_msrs = 0;
module_param(ignore_msrs, bool, 0644);

bool __read_mostly report_ignored_msrs = true;
module_param(report_ignored_msrs, bool, 0644);
EXPORT_SYMBOL_FOR_KVM_INTERNAL(report_ignored_msrs);

#define MAX_IO_MSRS 256

struct msr_bitmap_range {
	u32 flags;
	u32 nmsrs;
	u32 base;
	unsigned long *bitmap;
};

struct kvm_x86_msr_filter {
	u8 count;
	bool default_allow:1;
	struct msr_bitmap_range ranges[16];
};

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
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_nr_uret_msrs);
static u32 __read_mostly kvm_uret_msrs_list[KVM_MAX_NR_USER_RETURN_MSRS];
static DEFINE_PER_CPU(struct kvm_user_return_msrs, user_return_msrs);

void kvm_destroy_user_return_msrs(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		WARN_ON_ONCE(per_cpu(user_return_msrs, cpu).registered);

	kvm_nr_uret_msrs = 0;
}

static void kvm_on_user_return(struct user_return_notifier *urn)
{
	unsigned slot;
	struct kvm_user_return_msrs *msrs
		= container_of(urn, struct kvm_user_return_msrs, urn);
	struct kvm_user_return_msr_values *values;

	msrs->registered = false;
	user_return_notifier_unregister(urn);

	for (slot = 0; slot < kvm_nr_uret_msrs; ++slot) {
		values = &msrs->values[slot];
		if (values->host != values->curr) {
			wrmsrq(kvm_uret_msrs_list[slot], values->host);
			values->curr = values->host;
		}
	}
}

static int kvm_probe_user_return_msr(u32 msr)
{
	u64 val;
	int ret;

	preempt_disable();
	ret = rdmsrq_safe(msr, &val);
	if (ret)
		goto out;
	ret = wrmsrq_safe(msr, val);
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
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_add_user_return_msr);

int kvm_find_user_return_msr(u32 msr)
{
	int i;

	for (i = 0; i < kvm_nr_uret_msrs; ++i) {
		if (kvm_uret_msrs_list[i] == msr)
			return i;
	}
	return -1;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_find_user_return_msr);

void kvm_user_return_msr_cpu_online(void)
{
	struct kvm_user_return_msrs *msrs = this_cpu_ptr(&user_return_msrs);
	u64 value;
	int i;

	for (i = 0; i < kvm_nr_uret_msrs; ++i) {
		rdmsrq_safe(kvm_uret_msrs_list[i], &value);
		msrs->values[i].host = value;
		msrs->values[i].curr = value;
	}
}

static void kvm_user_return_register_notifier(struct kvm_user_return_msrs *msrs)
{
	if (!msrs->registered) {
		msrs->urn.on_user_return = kvm_on_user_return;
		user_return_notifier_register(&msrs->urn);
		msrs->registered = true;
	}
}

int kvm_set_user_return_msr(unsigned slot, u64 value, u64 mask)
{
	struct kvm_user_return_msrs *msrs = this_cpu_ptr(&user_return_msrs);
	int err;

	value = (value & mask) | (msrs->values[slot].host & ~mask);
	if (value == msrs->values[slot].curr)
		return 0;
	err = wrmsrq_safe(kvm_uret_msrs_list[slot], value);
	if (err)
		return 1;

	msrs->values[slot].curr = value;
	kvm_user_return_register_notifier(msrs);
	return 0;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_set_user_return_msr);

u64 kvm_get_user_return_msr(unsigned int slot)
{
	return this_cpu_ptr(&user_return_msrs)->values[slot].curr;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_get_user_return_msr);

void drop_user_return_notifiers(void)
{
	struct kvm_user_return_msrs *msrs = this_cpu_ptr(&user_return_msrs);

	if (msrs->registered)
		kvm_on_user_return(&msrs->urn);
}

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

	MSR_IA32_XFD, MSR_IA32_XFD_ERR, MSR_IA32_XSS,

	MSR_IA32_U_CET, MSR_IA32_S_CET,
	MSR_IA32_PL0_SSP, MSR_IA32_PL1_SSP, MSR_IA32_PL2_SSP,
	MSR_IA32_PL3_SSP, MSR_IA32_INT_SSP_TAB,
	MSR_IA32_DEBUGCTLMSR,
	MSR_IA32_LASTBRANCHFROMIP, MSR_IA32_LASTBRANCHTOIP,
	MSR_IA32_LASTINTFROMIP, MSR_IA32_LASTINTTOIP,
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
	MSR_AMD64_PERF_CNTR_GLOBAL_STATUS_SET,
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

int kvm_get_msr_index_list(struct kvm_msr_list __user *user_msr_list)
{
	struct kvm_msr_list msr_list;
	unsigned int n;

	if (copy_from_user(&msr_list, user_msr_list, sizeof(msr_list)))
		return -EFAULT;

	n = msr_list.nmsrs;
	msr_list.nmsrs = num_msrs_to_save + num_emulated_msrs;
	if (copy_to_user(user_msr_list, &msr_list, sizeof(msr_list)))
		return -EFAULT;

	if (n < msr_list.nmsrs)
		return -E2BIG;

	if (copy_to_user(user_msr_list->indices, &msrs_to_save,
			 num_msrs_to_save * sizeof(u32)))
		return -EFAULT;

	if (copy_to_user(user_msr_list->indices + num_msrs_to_save,
			 &emulated_msrs, num_emulated_msrs * sizeof(u32)))
		return -EFAULT;

	return 0;
}

int kvm_get_feature_msr_index_list(struct kvm_msr_list __user *user_msr_list)
{
	struct kvm_msr_list msr_list;
	unsigned int n;

	if (copy_from_user(&msr_list, user_msr_list, sizeof(msr_list)))
		return -EFAULT;

	n = msr_list.nmsrs;
	msr_list.nmsrs = num_msr_based_features;
	if (copy_to_user(user_msr_list, &msr_list, sizeof(msr_list)))
		return -EFAULT;

	if (n < msr_list.nmsrs)
		return -E2BIG;

	if (copy_to_user(user_msr_list->indices, &msr_based_features,
			 num_msr_based_features * sizeof(u32)))
		return -EFAULT;

	return 0;
}

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
	 ARCH_CAP_RFDS_NO | ARCH_CAP_RFDS_CLEAR | ARCH_CAP_BHI_NO | ARCH_CAP_ITS_NO)

u64 kvm_get_arch_capabilities(void)
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
	if (!boot_cpu_has_bug(X86_BUG_ITS))
		data |= ARCH_CAP_ITS_NO;

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
		rdmsrq_safe(index, data);
		break;
	default:
		return kvm_x86_call(get_feature_msr)(index, data);
	}
	return 0;
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

static int do_get_feature_msr(struct kvm_vcpu *vcpu, unsigned index, u64 *data)
{
	return kvm_do_msr_access(vcpu, index, data, true, MSR_TYPE_R,
				 kvm_get_feature_msr);
}

static bool __kvm_valid_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	if (efer & EFER_AUTOIBRS && !guest_cpu_cap_has(vcpu, X86_FEATURE_AUTOIBRS))
		return false;

	if (efer & EFER_FFXSR && !guest_cpu_cap_has(vcpu, X86_FEATURE_FXSR_OPT))
		return false;

	if (efer & EFER_SVME && !guest_cpu_cap_has(vcpu, X86_FEATURE_SVM))
		return false;

	if (efer & (EFER_LME | EFER_LMA) &&
	    !guest_cpu_cap_has(vcpu, X86_FEATURE_LM))
		return false;

	if (efer & EFER_NX && !guest_cpu_cap_has(vcpu, X86_FEATURE_NX))
		return false;

	return true;

}
bool kvm_valid_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	if (efer & ~kvm_caps.supported_efer_bits)
		return false;

	return __kvm_valid_efer(vcpu, efer);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_valid_efer);

static int set_efer(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	u64 old_efer = vcpu->arch.efer;
	u64 efer = msr_info->data;
	int r;

	if (efer & ~kvm_caps.supported_efer_bits)
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

	if (!cpu_feature_enabled(X86_FEATURE_XSAVES) &&
	    (efer & EFER_SVME))
		kvm_hv_xsaves_xsavec_maybe_warn(vcpu);

	return 0;
}

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
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_msr_allowed);

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
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_RDTSCP) &&
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_RDPID))
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
	case MSR_IA32_U_CET:
	case MSR_IA32_S_CET:
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK) &&
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_IBT))
			return KVM_MSR_RET_UNSUPPORTED;
		if (!kvm_is_valid_u_s_cet(vcpu, data))
			return 1;
		break;
	case MSR_KVM_INTERNAL_GUEST_SSP:
		if (!host_initiated)
			return 1;
		fallthrough;
		/*
		 * Note that the MSR emulation here is flawed when a vCPU
		 * doesn't support the Intel 64 architecture. The expected
		 * architectural behavior in this case is that the upper 32
		 * bits do not exist and should always read '0'. However,
		 * because the actual hardware on which the virtual CPU is
		 * running does support Intel 64, XRSTORS/XSAVES in the
		 * guest could observe behavior that violates the
		 * architecture. Intercepting XRSTORS/XSAVES for this
		 * special case isn't deemed worthwhile.
		 */
	case MSR_IA32_PL0_SSP ... MSR_IA32_INT_SSP_TAB:
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK))
			return KVM_MSR_RET_UNSUPPORTED;
		/*
		 * MSR_IA32_INT_SSP_TAB is not present on processors that do
		 * not support Intel 64 architecture.
		 */
		if (index == MSR_IA32_INT_SSP_TAB && !guest_cpu_cap_has(vcpu, X86_FEATURE_LM))
			return KVM_MSR_RET_UNSUPPORTED;
		if (is_noncanonical_msr_address(data, vcpu))
			return 1;
		/* All SSP MSRs except MSR_IA32_INT_SSP_TAB must be 4-byte aligned */
		if (index != MSR_IA32_INT_SSP_TAB && !IS_ALIGNED(data, 4))
			return 1;
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
static int __kvm_get_msr(struct kvm_vcpu *vcpu, u32 index, u64 *data,
			 bool host_initiated)
{
	struct msr_data msr;
	int ret;

	switch (index) {
	case MSR_TSC_AUX:
		if (!kvm_is_supported_user_return_msr(MSR_TSC_AUX))
			return 1;

		if (!host_initiated &&
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_RDTSCP) &&
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_RDPID))
			return 1;
		break;
	case MSR_IA32_U_CET:
	case MSR_IA32_S_CET:
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK) &&
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_IBT))
			return KVM_MSR_RET_UNSUPPORTED;
		break;
	case MSR_KVM_INTERNAL_GUEST_SSP:
		if (!host_initiated)
			return 1;
		fallthrough;
	case MSR_IA32_PL0_SSP ... MSR_IA32_INT_SSP_TAB:
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK))
			return KVM_MSR_RET_UNSUPPORTED;
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

int kvm_msr_write(struct kvm_vcpu *vcpu, u32 index, u64 data)
{
	return __kvm_set_msr(vcpu, index, data, true);
}

int kvm_msr_read(struct kvm_vcpu *vcpu, u32 index, u64 *data)
{
	return __kvm_get_msr(vcpu, index, data, true);
}

int __kvm_emulate_msr_read(struct kvm_vcpu *vcpu, u32 index, u64 *data)
{
	return kvm_get_msr_ignored_check(vcpu, index, data, false);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(__kvm_emulate_msr_read);

int __kvm_emulate_msr_write(struct kvm_vcpu *vcpu, u32 index, u64 data)
{
	return kvm_set_msr_ignored_check(vcpu, index, data, false);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(__kvm_emulate_msr_write);

int kvm_emulate_msr_read(struct kvm_vcpu *vcpu, u32 index, u64 *data)
{
	if (!kvm_msr_allowed(vcpu, index, KVM_MSR_FILTER_READ))
		return KVM_MSR_RET_FILTERED;

	return __kvm_emulate_msr_read(vcpu, index, data);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_emulate_msr_read);

int kvm_emulate_msr_write(struct kvm_vcpu *vcpu, u32 index, u64 data)
{
	if (!kvm_msr_allowed(vcpu, index, KVM_MSR_FILTER_WRITE))
		return KVM_MSR_RET_FILTERED;

	return __kvm_emulate_msr_write(vcpu, index, data);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_emulate_msr_write);

static fastpath_t __handle_fastpath_wrmsr(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	if (!kvm_pmu_is_fastpath_emulation_allowed(vcpu))
		return EXIT_FASTPATH_NONE;

	switch (msr) {
	case APIC_BASE_MSR + (APIC_ICR >> 4):
		if (!lapic_in_kernel(vcpu) || !apic_x2apic_mode(vcpu->arch.apic) ||
		    kvm_x2apic_icr_write_fast(vcpu->arch.apic, data))
			return EXIT_FASTPATH_NONE;
		break;
	case MSR_IA32_TSC_DEADLINE:
		kvm_set_lapic_tscdeadline_msr(vcpu, data);
		break;
	default:
		return EXIT_FASTPATH_NONE;
	}

	trace_kvm_msr_write(msr, data);

	if (!kvm_skip_emulated_instruction(vcpu))
		return EXIT_FASTPATH_EXIT_USERSPACE;

	return EXIT_FASTPATH_REENTER_GUEST;
}

fastpath_t handle_fastpath_wrmsr(struct kvm_vcpu *vcpu)
{
	return __handle_fastpath_wrmsr(vcpu, kvm_ecx_read(vcpu),
				       kvm_read_edx_eax(vcpu));
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(handle_fastpath_wrmsr);

fastpath_t handle_fastpath_wrmsr_imm(struct kvm_vcpu *vcpu, u32 msr, int reg)
{
	return __handle_fastpath_wrmsr(vcpu, msr, kvm_register_read(vcpu, reg));
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(handle_fastpath_wrmsr_imm);

static void complete_userspace_rdmsr(struct kvm_vcpu *vcpu)
{
	if (!vcpu->run->msr.error) {
		kvm_eax_write(vcpu, vcpu->run->msr.data);
		kvm_edx_write(vcpu, vcpu->run->msr.data >> 32);
	}
}

static int complete_emulated_insn_gp(struct kvm_vcpu *vcpu, int err)
{
	if (err) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	return kvm_emulate_instruction(vcpu, EMULTYPE_NO_DECODE | EMULTYPE_SKIP |
				       EMULTYPE_COMPLETE_USER_EXIT);
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

static int complete_fast_rdmsr_imm(struct kvm_vcpu *vcpu)
{
	if (!vcpu->run->msr.error)
		kvm_register_write(vcpu, vcpu->arch.cui_rdmsr_imm_reg,
				   vcpu->run->msr.data);

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

static int __kvm_emulate_rdmsr(struct kvm_vcpu *vcpu, u32 msr, int reg,
			       int (*complete_rdmsr)(struct kvm_vcpu *))
{
	u64 data;
	int r;

	r = kvm_emulate_msr_read(vcpu, msr, &data);

	if (!r) {
		trace_kvm_msr_read(msr, data);

		if (reg < 0) {
			kvm_eax_write(vcpu, data);
			kvm_edx_write(vcpu, data >> 32);
		} else {
			kvm_register_write(vcpu, reg, data);
		}
	} else {
		/* MSR read failed? See if we should ask user space */
		if (kvm_msr_user_space(vcpu, msr, KVM_EXIT_X86_RDMSR, 0,
				       complete_rdmsr, r))
			return 0;
		trace_kvm_msr_read_ex(msr);
	}

	return kvm_x86_call(complete_emulated_msr)(vcpu, r);
}

int kvm_emulate_rdmsr(struct kvm_vcpu *vcpu)
{
	return __kvm_emulate_rdmsr(vcpu, kvm_ecx_read(vcpu), -1,
				   complete_fast_rdmsr);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_emulate_rdmsr);

int kvm_emulate_rdmsr_imm(struct kvm_vcpu *vcpu, u32 msr, int reg)
{
	vcpu->arch.cui_rdmsr_imm_reg = reg;

	return __kvm_emulate_rdmsr(vcpu, msr, reg, complete_fast_rdmsr_imm);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_emulate_rdmsr_imm);

static int __kvm_emulate_wrmsr(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	int r;

	r = kvm_emulate_msr_write(vcpu, msr, data);
	if (!r) {
		trace_kvm_msr_write(msr, data);
	} else {
		/* MSR write failed? See if we should ask user space */
		if (kvm_msr_user_space(vcpu, msr, KVM_EXIT_X86_WRMSR, data,
				       complete_fast_msr_access, r))
			return 0;
		/* Signal all other negative errors to userspace */
		if (r < 0)
			return r;
		trace_kvm_msr_write_ex(msr, data);
	}

	return kvm_x86_call(complete_emulated_msr)(vcpu, r);
}

int kvm_emulate_wrmsr(struct kvm_vcpu *vcpu)
{
	return __kvm_emulate_wrmsr(vcpu, kvm_ecx_read(vcpu),
				   kvm_read_edx_eax(vcpu));
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_emulate_wrmsr);

int kvm_emulate_wrmsr_imm(struct kvm_vcpu *vcpu, u32 msr, int reg)
{
	return __kvm_emulate_wrmsr(vcpu, msr, kvm_register_read(vcpu, reg));
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_emulate_wrmsr_imm);

int kvm_emulator_get_msr_with_filter(struct kvm_vcpu *vcpu, u32 msr_index,
				     u64 *pdata)
{
	int r;

	r = kvm_emulate_msr_read(vcpu, msr_index, pdata);
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

int kvm_emulator_set_msr_with_filter(struct kvm_vcpu *vcpu, u32 msr_index,
				     u64 data)
{
	int r;

	r = kvm_emulate_msr_write(vcpu, msr_index, data);
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

int kvm_emulator_get_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 *pdata)
{
	/*
	 * Treat emulator accesses to the current shadow stack pointer as host-
	 * initiated, as they aren't true MSR accesses (SSP is a "just a reg"),
	 * and this API is used only for implicit accesses, i.e. not RDMSR, and
	 * so the index is fully KVM-controlled.
	 */
	if (unlikely(msr_index == MSR_KVM_INTERNAL_GUEST_SSP))
		return kvm_msr_read(vcpu, msr_index, pdata);

	return __kvm_emulate_msr_read(vcpu, msr_index, pdata);
}

/*
 * Returns true if the MSR in question is managed via XSTATE, i.e. is context
 * switched with the rest of guest FPU state.
 *
 * Note, S_CET is _not_ saved/restored via XSAVES/XRSTORS.
 */
static bool is_xstate_managed_msr(struct kvm_vcpu *vcpu, u32 msr)
{
	if (!vcpu)
		return false;

	switch (msr) {
	case MSR_IA32_U_CET:
		return guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK) ||
		       guest_cpu_cap_has(vcpu, X86_FEATURE_IBT);
	case MSR_IA32_PL0_SSP ... MSR_IA32_PL3_SSP:
		return guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK);
	default:
		return false;
	}
}

/*
 * Lock (and if necessary, re-load) the guest FPU, i.e. XSTATE, and access an
 * MSR that is managed via XSTATE.  Note, the caller is responsible for doing
 * the initial FPU load, this helper only ensures that guest state is resident
 * in hardware (the kernel can load its FPU state in IRQ context).
 *
 * Note, loading guest values for U_CET and PL[0-3]_SSP while executing in the
 * kernel is safe, as U_CET is specific to userspace, and PL[0-3]_SSP are only
 * consumed when transitioning to lower privilege levels, i.e. are effectively
 * only consumed by userspace as well.
 */
static __always_inline void kvm_access_xstate_msr(struct kvm_vcpu *vcpu,
						  struct msr_data *msr_info,
						  int access)
{
	BUILD_BUG_ON(access != MSR_TYPE_R && access != MSR_TYPE_W);

	KVM_BUG_ON(!is_xstate_managed_msr(vcpu, msr_info->index), vcpu->kvm);
	KVM_BUG_ON(!vcpu->arch.guest_fpu.fpstate->in_use, vcpu->kvm);

	kvm_fpu_get();
	if (access == MSR_TYPE_R)
		rdmsrq(msr_info->index, msr_info->data);
	else
		wrmsrq(msr_info->index, msr_info->data);
	kvm_fpu_put();
}

static void kvm_set_xstate_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	kvm_access_xstate_msr(vcpu, msr_info, MSR_TYPE_W);
}

static void kvm_get_xstate_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	kvm_access_xstate_msr(vcpu, msr_info, MSR_TYPE_R);
}

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

	if (__kvm_pv_async_pf_enabled(data) &&
	    kvm_gfn_to_hva_cache_init(vcpu->kvm, &vcpu->arch.apf.data, gpa,
				      sizeof(u64)))
		return 1;

	vcpu->arch.apf.msr_en_val = data;

	if (__kvm_pv_async_pf_enabled(data)) {
		kvm_async_pf_wakeup_all(vcpu);
	} else {
		kvm_clear_async_pf_completion_queue(vcpu);
		kvm_async_pf_hash_reset(vcpu);
	}
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

#ifdef CONFIG_X86_64
static inline u64 kvm_guest_supported_xfd(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.guest_supported_xcr0 & XFEATURE_MASK_USER_DYNAMIC;
}
#endif

int kvm_set_msr_common(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	u32 msr = msr_info->index;
	u64 data = msr_info->data;

	/*
	 * Do not allow host-initiated writes to trigger the Xen hypercall
	 * page setup; it could incur locking paths which are not expected
	 * if userspace sets the MSR in an unusual location.
	 */
	if (kvm_xen_is_hypercall_page_msr(vcpu->kvm, msr) &&
	    !msr_info->host_initiated)
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
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_ARCH_CAPABILITIES))
			return KVM_MSR_RET_UNSUPPORTED;
		vcpu->arch.arch_capabilities = data;
		break;
	case MSR_IA32_PERF_CAPABILITIES:
		if (!msr_info->host_initiated ||
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_PDCM))
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
		kvm_make_request(KVM_REQ_RECALC_INTERCEPTS, vcpu);
		break;
	case MSR_IA32_PRED_CMD: {
		u64 reserved_bits = ~(PRED_CMD_IBPB | PRED_CMD_SBPB);

		if (!msr_info->host_initiated) {
			if ((!guest_has_pred_cmd_msr(vcpu)))
				return 1;

			if (!guest_cpu_cap_has(vcpu, X86_FEATURE_SPEC_CTRL) &&
			    !guest_cpu_cap_has(vcpu, X86_FEATURE_AMD_IBPB))
				reserved_bits |= PRED_CMD_IBPB;

			if (!guest_cpu_cap_has(vcpu, X86_FEATURE_SBPB))
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

		wrmsrq(MSR_IA32_PRED_CMD, data);
		break;
	}
	case MSR_IA32_FLUSH_CMD:
		if (!msr_info->host_initiated &&
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_FLUSH_L1D))
			return 1;

		if (!boot_cpu_has(X86_FEATURE_FLUSH_L1D) || (data & ~L1D_FLUSH))
			return 1;
		if (!data)
			break;

		wrmsrq(MSR_IA32_FLUSH_CMD, L1D_FLUSH);
		break;
	case MSR_EFER:
		return set_efer(vcpu, msr_info);
	case MSR_K7_HWCR: {
		/*
		 * Allow McStatusWrEn and TscFreqSel. (Linux guests from v3.2
		 * through at least v6.6 whine if TscFreqSel is clear,
		 * depending on F/M/S.
		 */
		u64 valid = BIT_ULL(18) | BIT_ULL(24);

		data &= ~(u64)0x40;	/* ignore flush filter disable */
		data &= ~(u64)0x100;	/* ignore ignne emulation enable */
		data &= ~(u64)0x8;	/* ignore TLB cache disable */

		if (guest_cpu_cap_has(vcpu, X86_FEATURE_GP_ON_USER_CPUID))
			valid |= MSR_K7_HWCR_CPUID_USER_DIS;

		if (data & ~valid) {
			kvm_pr_unimpl_wrmsr(vcpu, msr, data);
			return 1;
		}
		vcpu->arch.msr_hwcr = data;
		break;
	}
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
		if (guest_cpu_cap_has(vcpu, X86_FEATURE_TSC_ADJUST)) {
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
			if (!guest_cpu_cap_has(vcpu, X86_FEATURE_XMM3))
				return 1;
			vcpu->arch.ia32_misc_enable_msr = data;
			vcpu->arch.cpuid_dynamic_bits_dirty = true;
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
		} else if (!vcpu->arch.guest_tsc_protected) {
			u64 adj = kvm_compute_l1_tsc_offset(vcpu, data) - vcpu->arch.l1_tsc_offset;
			adjust_tsc_offset_guest(vcpu, adj);
			vcpu->arch.ia32_tsc_adjust_msr += adj;
		}
		break;
	case MSR_IA32_XSS:
		if (!guest_cpuid_has(vcpu, X86_FEATURE_XSAVES))
			return KVM_MSR_RET_UNSUPPORTED;

		if (data & ~vcpu->arch.guest_supported_xss)
			return 1;
		if (vcpu->arch.ia32_xss == data)
			break;
		vcpu->arch.ia32_xss = data;
		vcpu->arch.cpuid_dynamic_bits_dirty = true;
		break;
	case MSR_SMI_COUNT:
		if (!msr_info->host_initiated)
			return 1;
		vcpu->arch.smi_count = data;
		break;
	case MSR_KVM_WALL_CLOCK_NEW:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE2))
			return KVM_MSR_RET_UNSUPPORTED;

		vcpu->kvm->arch.wall_clock = data;
		kvm_write_wall_clock(vcpu->kvm, data, 0);
		break;
	case MSR_KVM_WALL_CLOCK:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE))
			return KVM_MSR_RET_UNSUPPORTED;

		vcpu->kvm->arch.wall_clock = data;
		kvm_write_wall_clock(vcpu->kvm, data, 0);
		break;
	case MSR_KVM_SYSTEM_TIME_NEW:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE2))
			return KVM_MSR_RET_UNSUPPORTED;

		kvm_write_system_time(vcpu, data, false, msr_info->host_initiated);
		break;
	case MSR_KVM_SYSTEM_TIME:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE))
			return KVM_MSR_RET_UNSUPPORTED;

		kvm_write_system_time(vcpu, data, true,  msr_info->host_initiated);
		break;
	case MSR_KVM_ASYNC_PF_EN:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF))
			return KVM_MSR_RET_UNSUPPORTED;

		if (kvm_pv_enable_async_pf(vcpu, data))
			return 1;
		break;
	case MSR_KVM_ASYNC_PF_INT:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF_INT))
			return KVM_MSR_RET_UNSUPPORTED;

		if (kvm_pv_enable_async_pf_int(vcpu, data))
			return 1;
		break;
	case MSR_KVM_ASYNC_PF_ACK:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF_INT))
			return KVM_MSR_RET_UNSUPPORTED;
		if (data & 0x1) {
			/*
			 * Pairs with the smp_mb__after_atomic() in
			 * kvm_arch_async_page_present_queued().
			 */
			smp_store_mb(vcpu->arch.apf.pageready_pending, false);

			kvm_check_async_pf_completion(vcpu);
		}
		break;
	case MSR_KVM_STEAL_TIME:
		if (!guest_pv_has(vcpu, KVM_FEATURE_STEAL_TIME))
			return KVM_MSR_RET_UNSUPPORTED;

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
			return KVM_MSR_RET_UNSUPPORTED;

		if (kvm_lapic_set_pv_eoi(vcpu, data, sizeof(u8)))
			return 1;
		break;

	case MSR_KVM_POLL_CONTROL:
		if (!guest_pv_has(vcpu, KVM_FEATURE_POLL_CONTROL))
			return KVM_MSR_RET_UNSUPPORTED;

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
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_OSVW))
			return 1;
		vcpu->arch.osvw.length = data;
		break;
	case MSR_AMD64_OSVW_STATUS:
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_OSVW))
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
		     !(vcpu->arch.msr_platform_info & MSR_PLATFORM_INFO_CPUID_FAULT)))
			return 1;
		vcpu->arch.msr_misc_features_enables = data;
		break;
#ifdef CONFIG_X86_64
	case MSR_IA32_XFD:
		if (!msr_info->host_initiated &&
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_XFD))
			return 1;

		if (data & ~kvm_guest_supported_xfd(vcpu))
			return 1;

		fpu_update_guest_xfd(&vcpu->arch.guest_fpu, data);
		break;
	case MSR_IA32_XFD_ERR:
		if (!msr_info->host_initiated &&
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_XFD))
			return 1;

		if (data & ~kvm_guest_supported_xfd(vcpu))
			return 1;

		vcpu->arch.guest_fpu.xfd_err = data;
		break;
#endif
	case MSR_IA32_U_CET:
	case MSR_IA32_PL0_SSP ... MSR_IA32_PL3_SSP:
		kvm_set_xstate_msr(vcpu, msr_info);
		break;
	default:
		if (kvm_pmu_is_valid_msr(vcpu, msr))
			return kvm_pmu_set_msr(vcpu, msr_info);

		return KVM_MSR_RET_UNSUPPORTED;
	}
	return 0;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_set_msr_common);

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
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_ARCH_CAPABILITIES))
			return KVM_MSR_RET_UNSUPPORTED;
		msr_info->data = vcpu->arch.arch_capabilities;
		break;
	case MSR_IA32_PERF_CAPABILITIES:
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_PDCM))
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
			return KVM_MSR_RET_UNSUPPORTED;

		msr_info->data = vcpu->kvm->arch.wall_clock;
		break;
	case MSR_KVM_WALL_CLOCK_NEW:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE2))
			return KVM_MSR_RET_UNSUPPORTED;

		msr_info->data = vcpu->kvm->arch.wall_clock;
		break;
	case MSR_KVM_SYSTEM_TIME:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE))
			return KVM_MSR_RET_UNSUPPORTED;

		msr_info->data = vcpu->arch.time;
		break;
	case MSR_KVM_SYSTEM_TIME_NEW:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE2))
			return KVM_MSR_RET_UNSUPPORTED;

		msr_info->data = vcpu->arch.time;
		break;
	case MSR_KVM_ASYNC_PF_EN:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF))
			return KVM_MSR_RET_UNSUPPORTED;

		msr_info->data = vcpu->arch.apf.msr_en_val;
		break;
	case MSR_KVM_ASYNC_PF_INT:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF_INT))
			return KVM_MSR_RET_UNSUPPORTED;

		msr_info->data = vcpu->arch.apf.msr_int_val;
		break;
	case MSR_KVM_ASYNC_PF_ACK:
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF_INT))
			return KVM_MSR_RET_UNSUPPORTED;

		msr_info->data = 0;
		break;
	case MSR_KVM_STEAL_TIME:
		if (!guest_pv_has(vcpu, KVM_FEATURE_STEAL_TIME))
			return KVM_MSR_RET_UNSUPPORTED;

		msr_info->data = vcpu->arch.st.msr_val;
		break;
	case MSR_KVM_PV_EOI_EN:
		if (!guest_pv_has(vcpu, KVM_FEATURE_PV_EOI))
			return KVM_MSR_RET_UNSUPPORTED;

		msr_info->data = vcpu->arch.pv_eoi.msr_val;
		break;
	case MSR_KVM_POLL_CONTROL:
		if (!guest_pv_has(vcpu, KVM_FEATURE_POLL_CONTROL))
			return KVM_MSR_RET_UNSUPPORTED;

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
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_OSVW))
			return 1;
		msr_info->data = vcpu->arch.osvw.length;
		break;
	case MSR_AMD64_OSVW_STATUS:
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_OSVW))
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
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_XFD))
			return 1;

		msr_info->data = vcpu->arch.guest_fpu.fpstate->xfd;
		break;
	case MSR_IA32_XFD_ERR:
		if (!msr_info->host_initiated &&
		    !guest_cpu_cap_has(vcpu, X86_FEATURE_XFD))
			return 1;

		msr_info->data = vcpu->arch.guest_fpu.xfd_err;
		break;
#endif
	case MSR_IA32_U_CET:
	case MSR_IA32_PL0_SSP ... MSR_IA32_PL3_SSP:
		kvm_get_xstate_msr(vcpu, msr_info);
		break;
	default:
		if (kvm_pmu_is_valid_msr(vcpu, msr_info->index))
			return kvm_pmu_get_msr(vcpu, msr_info);

		return KVM_MSR_RET_UNSUPPORTED;
	}
	return 0;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_get_msr_common);

static int do_get_msr(struct kvm_vcpu *vcpu, unsigned index, u64 *data)
{
	return kvm_get_msr_ignored_check(vcpu, index, data, true);
}

static int do_set_msr(struct kvm_vcpu *vcpu, unsigned index, u64 *data)
{
	u64 val;

	/*
	 * Reject writes to immutable feature MSRs if the vCPU model is frozen,
	 * as KVM doesn't support modifying the guest vCPU model on the fly,
	 * e.g. changing the VMX capabilities MSRs while L2 is active is
	 * nonsensical.  Allow writes of the same value, e.g. so that userspace
	 * can blindly stuff all MSRs when emulating RESET.
	 */
	if (!kvm_can_set_cpuid_and_feature_msrs(vcpu) &&
	    kvm_is_immutable_feature_msr(index) &&
	    (do_get_msr(vcpu, index, &val) || *data != val))
		return -EINVAL;

	return kvm_set_msr_ignored_check(vcpu, index, *data, true);
}

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
	bool fpu_loaded = false;
	int i;

	for (i = 0; i < msrs->nmsrs; ++i) {
		/*
		 * If userspace is accessing one or more XSTATE-managed MSRs,
		 * temporarily load the guest's FPU state so that the guest's
		 * MSR value(s) is resident in hardware and thus can be accessed
		 * via RDMSR/WRMSR.
		 */
		if (!fpu_loaded && is_xstate_managed_msr(vcpu, entries[i].index)) {
			kvm_load_guest_fpu(vcpu);
			fpu_loaded = true;
		}
		if (do_msr(vcpu, entries[i].index, &entries[i].data))
			break;
	}
	if (fpu_loaded)
		kvm_put_guest_fpu(vcpu);

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

int kvm_get_feature_msrs(struct kvm_msrs __user *user_msrs)
{
	return msr_io(NULL, user_msrs, do_get_feature_msr, 1);
}

int kvm_get_msrs(struct kvm_vcpu *vcpu, struct kvm_msrs __user *user_msrs)
{
	guard(srcu)(&vcpu->kvm->srcu);

	return msr_io(vcpu, user_msrs, do_get_msr, 1);
}

int kvm_set_msrs(struct kvm_vcpu *vcpu, struct kvm_msrs __user *user_msrs)
{
	guard(srcu)(&vcpu->kvm->srcu);

	return msr_io(vcpu, user_msrs, do_set_msr, 0);
}

static int kvm_get_one_msr(struct kvm_vcpu *vcpu, u32 msr, u64 __user *user_val)
{
	u64 val;

	if (do_get_msr(vcpu, msr, &val))
		return -EINVAL;

	if (put_user(val, user_val))
		return -EFAULT;

	return 0;
}

static int kvm_set_one_msr(struct kvm_vcpu *vcpu, u32 msr, u64 __user *user_val)
{
	u64 val;

	if (get_user(val, user_val))
		return -EFAULT;

	if (do_set_msr(vcpu, msr, &val))
		return -EINVAL;

	return 0;
}

struct kvm_x86_reg_id {
	__u32 index;
	__u8  type;
	__u8  rsvd1;
	__u8  rsvd2:4;
	__u8  size:4;
	__u8  x86;
};

static int kvm_translate_kvm_reg(struct kvm_vcpu *vcpu,
				 struct kvm_x86_reg_id *reg)
{
	switch (reg->index) {
	case KVM_REG_GUEST_SSP:
		/*
		 * FIXME: If host-initiated accesses are ever exempted from
		 * ignore_msrs (in kvm_do_msr_access()), drop this manual check
		 * and rely on KVM's standard checks to reject accesses to regs
		 * that don't exist.
		 */
		if (!guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK))
			return -EINVAL;

		reg->type = KVM_X86_REG_TYPE_MSR;
		reg->index = MSR_KVM_INTERNAL_GUEST_SSP;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int kvm_get_set_one_reg(struct kvm_vcpu *vcpu, unsigned int ioctl,
			void __user *argp)
{
	struct kvm_one_reg one_reg;
	struct kvm_x86_reg_id *reg;
	u64 __user *user_val;
	bool load_fpu;
	int r;

	if (copy_from_user(&one_reg, argp, sizeof(one_reg)))
		return -EFAULT;

	if ((one_reg.id & KVM_REG_ARCH_MASK) != KVM_REG_X86)
		return -EINVAL;

	reg = (struct kvm_x86_reg_id *)&one_reg.id;
	if (reg->rsvd1 || reg->rsvd2)
		return -EINVAL;

	if (reg->type == KVM_X86_REG_TYPE_KVM) {
		r = kvm_translate_kvm_reg(vcpu, reg);
		if (r)
			return r;
	}

	if (reg->type != KVM_X86_REG_TYPE_MSR)
		return -EINVAL;

	if ((one_reg.id & KVM_REG_SIZE_MASK) != KVM_REG_SIZE_U64)
		return -EINVAL;

	guard(srcu)(&vcpu->kvm->srcu);

	load_fpu = is_xstate_managed_msr(vcpu, reg->index);
	if (load_fpu)
		kvm_load_guest_fpu(vcpu);

	user_val = u64_to_user_ptr(one_reg.addr);
	if (ioctl == KVM_GET_ONE_REG)
		r = kvm_get_one_msr(vcpu, reg->index, user_val);
	else
		r = kvm_set_one_msr(vcpu, reg->index, user_val);

	if (load_fpu)
		kvm_put_guest_fpu(vcpu);
	return r;
}

int kvm_get_reg_list(struct kvm_vcpu *vcpu,
		     struct kvm_reg_list __user *user_list)
{
	u64 nr_regs = guest_cpu_cap_has(vcpu, X86_FEATURE_SHSTK) ? 1 : 0;
	u64 user_nr_regs;

	if (get_user(user_nr_regs, &user_list->n))
		return -EFAULT;

	if (put_user(nr_regs, &user_list->n))
		return -EFAULT;

	if (user_nr_regs < nr_regs)
		return -E2BIG;

	if (nr_regs &&
	    put_user(KVM_X86_REG_KVM(KVM_REG_GUEST_SSP), &user_list->reg[0]))
		return -EFAULT;

	return 0;
}

static struct kvm_x86_msr_filter *kvm_alloc_msr_filter(bool default_allow)
{
	struct kvm_x86_msr_filter *msr_filter;

	msr_filter = kzalloc_obj(*msr_filter, GFP_KERNEL_ACCOUNT);
	if (!msr_filter)
		return NULL;

	msr_filter->default_allow = default_allow;
	return msr_filter;
}

void kvm_free_msr_filter(struct kvm_x86_msr_filter *msr_filter)
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

int kvm_vm_ioctl_set_msr_filter(struct kvm *kvm, struct kvm_msr_filter *filter)
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

	/*
	 * Recalc MSR intercepts as userspace may want to intercept accesses to
	 * MSRs that KVM would otherwise pass through to the guest.
	 */
	kvm_make_all_cpus_request(kvm, KVM_REQ_RECALC_INTERCEPTS);

	return 0;
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
	u64 dummy;

	if (rdmsrq_safe(msr_index, &dummy))
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
	case MSR_AMD64_PERF_CNTR_GLOBAL_STATUS_SET:
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
	case MSR_IA32_XSS:
		if (!kvm_caps.supported_xss)
			return;
		break;
	case MSR_IA32_U_CET:
	case MSR_IA32_S_CET:
		if (!kvm_cpu_cap_has(X86_FEATURE_SHSTK) &&
		    !kvm_cpu_cap_has(X86_FEATURE_IBT))
			return;
		break;
	case MSR_IA32_INT_SSP_TAB:
		if (!kvm_cpu_cap_has(X86_FEATURE_LM))
			return;
		fallthrough;
	case MSR_IA32_PL0_SSP ... MSR_IA32_PL3_SSP:
		if (!kvm_cpu_cap_has(X86_FEATURE_SHSTK))
			return;
		break;
	default:
		break;
	}

	msrs_to_save[num_msrs_to_save++] = msr_index;
}

void kvm_init_msr_lists(void)
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

	if (rdmsrq_safe(MSR_IA32_SPEC_CTRL, &saved_value))
		ret = 1;
	else if (wrmsrq_safe(MSR_IA32_SPEC_CTRL, value))
		ret = 1;
	else
		wrmsrq(MSR_IA32_SPEC_CTRL, saved_value);

	local_irq_restore(flags);

	return ret;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_spec_ctrl_test_value);

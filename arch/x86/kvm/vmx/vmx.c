// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 */

#include <linux/frame.h>
#include <linux/highmem.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mod_devicetable.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/smt.h>
#include <linux/slab.h>
#include <linux/tboot.h>
#include <linux/trace_events.h>

#include <asm/apic.h>
#include <asm/asm.h>
#include <asm/cpu.h>
#include <asm/debugreg.h>
#include <asm/desc.h>
#include <asm/fpu/internal.h>
#include <asm/io.h>
#include <asm/irq_remapping.h>
#include <asm/kexec.h>
#include <asm/perf_event.h>
#include <asm/mce.h>
#include <asm/mmu_context.h>
#include <asm/mshyperv.h>
#include <asm/spec-ctrl.h>
#include <asm/virtext.h>
#include <asm/vmx.h>

#include "capabilities.h"
#include "cpuid.h"
#include "evmcs.h"
#include "irq.h"
#include "kvm_cache_regs.h"
#include "lapic.h"
#include "mmu.h"
#include "nested.h"
#include "ops.h"
#include "pmu.h"
#include "trace.h"
#include "vmcs.h"
#include "vmcs12.h"
#include "vmx.h"
#include "x86.h"

MODULE_AUTHOR("Qumranet");
MODULE_LICENSE("GPL");

static const struct x86_cpu_id vmx_cpu_id[] = {
	X86_FEATURE_MATCH(X86_FEATURE_VMX),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, vmx_cpu_id);

bool __read_mostly enable_vpid = 1;
module_param_named(vpid, enable_vpid, bool, 0444);

static bool __read_mostly enable_vnmi = 1;
module_param_named(vnmi, enable_vnmi, bool, S_IRUGO);

bool __read_mostly flexpriority_enabled = 1;
module_param_named(flexpriority, flexpriority_enabled, bool, S_IRUGO);

bool __read_mostly enable_ept = 1;
module_param_named(ept, enable_ept, bool, S_IRUGO);

bool __read_mostly enable_unrestricted_guest = 1;
module_param_named(unrestricted_guest,
			enable_unrestricted_guest, bool, S_IRUGO);

bool __read_mostly enable_ept_ad_bits = 1;
module_param_named(eptad, enable_ept_ad_bits, bool, S_IRUGO);

static bool __read_mostly emulate_invalid_guest_state = true;
module_param(emulate_invalid_guest_state, bool, S_IRUGO);

static bool __read_mostly fasteoi = 1;
module_param(fasteoi, bool, S_IRUGO);

bool __read_mostly enable_apicv = 1;
module_param(enable_apicv, bool, S_IRUGO);

/*
 * If nested=1, nested virtualization is supported, i.e., guests may use
 * VMX and be a hypervisor for its own guests. If nested=0, guests may not
 * use VMX instructions.
 */
static bool __read_mostly nested = 1;
module_param(nested, bool, S_IRUGO);

bool __read_mostly enable_pml = 1;
module_param_named(pml, enable_pml, bool, S_IRUGO);

static bool __read_mostly dump_invalid_vmcs = 0;
module_param(dump_invalid_vmcs, bool, 0644);

#define MSR_BITMAP_MODE_X2APIC		1
#define MSR_BITMAP_MODE_X2APIC_APICV	2

#define KVM_VMX_TSC_MULTIPLIER_MAX     0xffffffffffffffffULL

/* Guest_tsc -> host_tsc conversion requires 64-bit division.  */
static int __read_mostly cpu_preemption_timer_multi;
static bool __read_mostly enable_preemption_timer = 1;
#ifdef CONFIG_X86_64
module_param_named(preemption_timer, enable_preemption_timer, bool, S_IRUGO);
#endif

#define KVM_VM_CR0_ALWAYS_OFF (X86_CR0_NW | X86_CR0_CD)
#define KVM_VM_CR0_ALWAYS_ON_UNRESTRICTED_GUEST X86_CR0_NE
#define KVM_VM_CR0_ALWAYS_ON				\
	(KVM_VM_CR0_ALWAYS_ON_UNRESTRICTED_GUEST | 	\
	 X86_CR0_WP | X86_CR0_PG | X86_CR0_PE)
#define KVM_CR4_GUEST_OWNED_BITS				      \
	(X86_CR4_PVI | X86_CR4_DE | X86_CR4_PCE | X86_CR4_OSFXSR      \
	 | X86_CR4_OSXMMEXCPT | X86_CR4_LA57 | X86_CR4_TSD)

#define KVM_VM_CR4_ALWAYS_ON_UNRESTRICTED_GUEST X86_CR4_VMXE
#define KVM_PMODE_VM_CR4_ALWAYS_ON (X86_CR4_PAE | X86_CR4_VMXE)
#define KVM_RMODE_VM_CR4_ALWAYS_ON (X86_CR4_VME | X86_CR4_PAE | X86_CR4_VMXE)

#define RMODE_GUEST_OWNED_EFLAGS_BITS (~(X86_EFLAGS_IOPL | X86_EFLAGS_VM))

#define MSR_IA32_RTIT_STATUS_MASK (~(RTIT_STATUS_FILTEREN | \
	RTIT_STATUS_CONTEXTEN | RTIT_STATUS_TRIGGEREN | \
	RTIT_STATUS_ERROR | RTIT_STATUS_STOPPED | \
	RTIT_STATUS_BYTECNT))

#define MSR_IA32_RTIT_OUTPUT_BASE_MASK \
	(~((1UL << cpuid_query_maxphyaddr(vcpu)) - 1) | 0x7f)

/*
 * These 2 parameters are used to config the controls for Pause-Loop Exiting:
 * ple_gap:    upper bound on the amount of time between two successive
 *             executions of PAUSE in a loop. Also indicate if ple enabled.
 *             According to test, this time is usually smaller than 128 cycles.
 * ple_window: upper bound on the amount of time a guest is allowed to execute
 *             in a PAUSE loop. Tests indicate that most spinlocks are held for
 *             less than 2^12 cycles
 * Time is measured based on a counter that runs at the same rate as the TSC,
 * refer SDM volume 3b section 21.6.13 & 22.1.3.
 */
static unsigned int ple_gap = KVM_DEFAULT_PLE_GAP;
module_param(ple_gap, uint, 0444);

static unsigned int ple_window = KVM_VMX_DEFAULT_PLE_WINDOW;
module_param(ple_window, uint, 0444);

/* Default doubles per-vcpu window every exit. */
static unsigned int ple_window_grow = KVM_DEFAULT_PLE_WINDOW_GROW;
module_param(ple_window_grow, uint, 0444);

/* Default resets per-vcpu window every exit to ple_window. */
static unsigned int ple_window_shrink = KVM_DEFAULT_PLE_WINDOW_SHRINK;
module_param(ple_window_shrink, uint, 0444);

/* Default is to compute the maximum so we can never overflow. */
static unsigned int ple_window_max        = KVM_VMX_DEFAULT_PLE_WINDOW_MAX;
module_param(ple_window_max, uint, 0444);

/* Default is SYSTEM mode, 1 for host-guest mode */
int __read_mostly pt_mode = PT_MODE_SYSTEM;
module_param(pt_mode, int, S_IRUGO);

static DEFINE_STATIC_KEY_FALSE(vmx_l1d_should_flush);
static DEFINE_STATIC_KEY_FALSE(vmx_l1d_flush_cond);
static DEFINE_MUTEX(vmx_l1d_flush_mutex);

/* Storage for pre module init parameter parsing */
static enum vmx_l1d_flush_state __read_mostly vmentry_l1d_flush_param = VMENTER_L1D_FLUSH_AUTO;

static const struct {
	const char *option;
	bool for_parse;
} vmentry_l1d_param[] = {
	[VMENTER_L1D_FLUSH_AUTO]	 = {"auto", true},
	[VMENTER_L1D_FLUSH_NEVER]	 = {"never", true},
	[VMENTER_L1D_FLUSH_COND]	 = {"cond", true},
	[VMENTER_L1D_FLUSH_ALWAYS]	 = {"always", true},
	[VMENTER_L1D_FLUSH_EPT_DISABLED] = {"EPT disabled", false},
	[VMENTER_L1D_FLUSH_NOT_REQUIRED] = {"not required", false},
};

#define L1D_CACHE_ORDER 4
static void *vmx_l1d_flush_pages;

static int vmx_setup_l1d_flush(enum vmx_l1d_flush_state l1tf)
{
	struct page *page;
	unsigned int i;

	if (!boot_cpu_has_bug(X86_BUG_L1TF)) {
		l1tf_vmx_mitigation = VMENTER_L1D_FLUSH_NOT_REQUIRED;
		return 0;
	}

	if (!enable_ept) {
		l1tf_vmx_mitigation = VMENTER_L1D_FLUSH_EPT_DISABLED;
		return 0;
	}

	if (boot_cpu_has(X86_FEATURE_ARCH_CAPABILITIES)) {
		u64 msr;

		rdmsrl(MSR_IA32_ARCH_CAPABILITIES, msr);
		if (msr & ARCH_CAP_SKIP_VMENTRY_L1DFLUSH) {
			l1tf_vmx_mitigation = VMENTER_L1D_FLUSH_NOT_REQUIRED;
			return 0;
		}
	}

	/* If set to auto use the default l1tf mitigation method */
	if (l1tf == VMENTER_L1D_FLUSH_AUTO) {
		switch (l1tf_mitigation) {
		case L1TF_MITIGATION_OFF:
			l1tf = VMENTER_L1D_FLUSH_NEVER;
			break;
		case L1TF_MITIGATION_FLUSH_NOWARN:
		case L1TF_MITIGATION_FLUSH:
		case L1TF_MITIGATION_FLUSH_NOSMT:
			l1tf = VMENTER_L1D_FLUSH_COND;
			break;
		case L1TF_MITIGATION_FULL:
		case L1TF_MITIGATION_FULL_FORCE:
			l1tf = VMENTER_L1D_FLUSH_ALWAYS;
			break;
		}
	} else if (l1tf_mitigation == L1TF_MITIGATION_FULL_FORCE) {
		l1tf = VMENTER_L1D_FLUSH_ALWAYS;
	}

	if (l1tf != VMENTER_L1D_FLUSH_NEVER && !vmx_l1d_flush_pages &&
	    !boot_cpu_has(X86_FEATURE_FLUSH_L1D)) {
		/*
		 * This allocation for vmx_l1d_flush_pages is not tied to a VM
		 * lifetime and so should not be charged to a memcg.
		 */
		page = alloc_pages(GFP_KERNEL, L1D_CACHE_ORDER);
		if (!page)
			return -ENOMEM;
		vmx_l1d_flush_pages = page_address(page);

		/*
		 * Initialize each page with a different pattern in
		 * order to protect against KSM in the nested
		 * virtualization case.
		 */
		for (i = 0; i < 1u << L1D_CACHE_ORDER; ++i) {
			memset(vmx_l1d_flush_pages + i * PAGE_SIZE, i + 1,
			       PAGE_SIZE);
		}
	}

	l1tf_vmx_mitigation = l1tf;

	if (l1tf != VMENTER_L1D_FLUSH_NEVER)
		static_branch_enable(&vmx_l1d_should_flush);
	else
		static_branch_disable(&vmx_l1d_should_flush);

	if (l1tf == VMENTER_L1D_FLUSH_COND)
		static_branch_enable(&vmx_l1d_flush_cond);
	else
		static_branch_disable(&vmx_l1d_flush_cond);
	return 0;
}

static int vmentry_l1d_flush_parse(const char *s)
{
	unsigned int i;

	if (s) {
		for (i = 0; i < ARRAY_SIZE(vmentry_l1d_param); i++) {
			if (vmentry_l1d_param[i].for_parse &&
			    sysfs_streq(s, vmentry_l1d_param[i].option))
				return i;
		}
	}
	return -EINVAL;
}

static int vmentry_l1d_flush_set(const char *s, const struct kernel_param *kp)
{
	int l1tf, ret;

	l1tf = vmentry_l1d_flush_parse(s);
	if (l1tf < 0)
		return l1tf;

	if (!boot_cpu_has(X86_BUG_L1TF))
		return 0;

	/*
	 * Has vmx_init() run already? If not then this is the pre init
	 * parameter parsing. In that case just store the value and let
	 * vmx_init() do the proper setup after enable_ept has been
	 * established.
	 */
	if (l1tf_vmx_mitigation == VMENTER_L1D_FLUSH_AUTO) {
		vmentry_l1d_flush_param = l1tf;
		return 0;
	}

	mutex_lock(&vmx_l1d_flush_mutex);
	ret = vmx_setup_l1d_flush(l1tf);
	mutex_unlock(&vmx_l1d_flush_mutex);
	return ret;
}

static int vmentry_l1d_flush_get(char *s, const struct kernel_param *kp)
{
	if (WARN_ON_ONCE(l1tf_vmx_mitigation >= ARRAY_SIZE(vmentry_l1d_param)))
		return sprintf(s, "???\n");

	return sprintf(s, "%s\n", vmentry_l1d_param[l1tf_vmx_mitigation].option);
}

static const struct kernel_param_ops vmentry_l1d_flush_ops = {
	.set = vmentry_l1d_flush_set,
	.get = vmentry_l1d_flush_get,
};
module_param_cb(vmentry_l1d_flush, &vmentry_l1d_flush_ops, NULL, 0644);

static bool guest_state_valid(struct kvm_vcpu *vcpu);
static u32 vmx_segment_access_rights(struct kvm_segment *var);
static __always_inline void vmx_disable_intercept_for_msr(unsigned long *msr_bitmap,
							  u32 msr, int type);

void vmx_vmexit(void);

#define vmx_insn_failed(fmt...)		\
do {					\
	WARN_ONCE(1, fmt);		\
	pr_warn_ratelimited(fmt);	\
} while (0)

asmlinkage void vmread_error(unsigned long field, bool fault)
{
	if (fault)
		kvm_spurious_fault();
	else
		vmx_insn_failed("kvm: vmread failed: field=%lx\n", field);
}

noinline void vmwrite_error(unsigned long field, unsigned long value)
{
	vmx_insn_failed("kvm: vmwrite failed: field=%lx val=%lx err=%d\n",
			field, value, vmcs_read32(VM_INSTRUCTION_ERROR));
}

noinline void vmclear_error(struct vmcs *vmcs, u64 phys_addr)
{
	vmx_insn_failed("kvm: vmclear failed: %p/%llx\n", vmcs, phys_addr);
}

noinline void vmptrld_error(struct vmcs *vmcs, u64 phys_addr)
{
	vmx_insn_failed("kvm: vmptrld failed: %p/%llx\n", vmcs, phys_addr);
}

noinline void invvpid_error(unsigned long ext, u16 vpid, gva_t gva)
{
	vmx_insn_failed("kvm: invvpid failed: ext=0x%lx vpid=%u gva=0x%lx\n",
			ext, vpid, gva);
}

noinline void invept_error(unsigned long ext, u64 eptp, gpa_t gpa)
{
	vmx_insn_failed("kvm: invept failed: ext=0x%lx eptp=%llx gpa=0x%llx\n",
			ext, eptp, gpa);
}

static DEFINE_PER_CPU(struct vmcs *, vmxarea);
DEFINE_PER_CPU(struct vmcs *, current_vmcs);
/*
 * We maintain a per-CPU linked-list of VMCS loaded on that CPU. This is needed
 * when a CPU is brought down, and we need to VMCLEAR all VMCSs loaded on it.
 */
static DEFINE_PER_CPU(struct list_head, loaded_vmcss_on_cpu);

/*
 * We maintian a per-CPU linked-list of vCPU, so in wakeup_handler() we
 * can find which vCPU should be waken up.
 */
static DEFINE_PER_CPU(struct list_head, blocked_vcpu_on_cpu);
static DEFINE_PER_CPU(spinlock_t, blocked_vcpu_on_cpu_lock);

static DECLARE_BITMAP(vmx_vpid_bitmap, VMX_NR_VPIDS);
static DEFINE_SPINLOCK(vmx_vpid_lock);

struct vmcs_config vmcs_config;
struct vmx_capability vmx_capability;

#define VMX_SEGMENT_FIELD(seg)					\
	[VCPU_SREG_##seg] = {                                   \
		.selector = GUEST_##seg##_SELECTOR,		\
		.base = GUEST_##seg##_BASE,		   	\
		.limit = GUEST_##seg##_LIMIT,		   	\
		.ar_bytes = GUEST_##seg##_AR_BYTES,	   	\
	}

static const struct kvm_vmx_segment_field {
	unsigned selector;
	unsigned base;
	unsigned limit;
	unsigned ar_bytes;
} kvm_vmx_segment_fields[] = {
	VMX_SEGMENT_FIELD(CS),
	VMX_SEGMENT_FIELD(DS),
	VMX_SEGMENT_FIELD(ES),
	VMX_SEGMENT_FIELD(FS),
	VMX_SEGMENT_FIELD(GS),
	VMX_SEGMENT_FIELD(SS),
	VMX_SEGMENT_FIELD(TR),
	VMX_SEGMENT_FIELD(LDTR),
};

u64 host_efer;
static unsigned long host_idt_base;

/*
 * Though SYSCALL is only supported in 64-bit mode on Intel CPUs, kvm
 * will emulate SYSCALL in legacy mode if the vendor string in guest
 * CPUID.0:{EBX,ECX,EDX} is "AuthenticAMD" or "AMDisbetter!" To
 * support this emulation, IA32_STAR must always be included in
 * vmx_msr_index[], even in i386 builds.
 */
const u32 vmx_msr_index[] = {
#ifdef CONFIG_X86_64
	MSR_SYSCALL_MASK, MSR_LSTAR, MSR_CSTAR,
#endif
	MSR_EFER, MSR_TSC_AUX, MSR_STAR,
	MSR_IA32_TSX_CTRL,
};

#if IS_ENABLED(CONFIG_HYPERV)
static bool __read_mostly enlightened_vmcs = true;
module_param(enlightened_vmcs, bool, 0444);

/* check_ept_pointer() should be under protection of ept_pointer_lock. */
static void check_ept_pointer_match(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu;
	u64 tmp_eptp = INVALID_PAGE;
	int i;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!VALID_PAGE(tmp_eptp)) {
			tmp_eptp = to_vmx(vcpu)->ept_pointer;
		} else if (tmp_eptp != to_vmx(vcpu)->ept_pointer) {
			to_kvm_vmx(kvm)->ept_pointers_match
				= EPT_POINTERS_MISMATCH;
			return;
		}
	}

	to_kvm_vmx(kvm)->ept_pointers_match = EPT_POINTERS_MATCH;
}

static int kvm_fill_hv_flush_list_func(struct hv_guest_mapping_flush_list *flush,
		void *data)
{
	struct kvm_tlb_range *range = data;

	return hyperv_fill_flush_guest_mapping_list(flush, range->start_gfn,
			range->pages);
}

static inline int __hv_remote_flush_tlb_with_range(struct kvm *kvm,
		struct kvm_vcpu *vcpu, struct kvm_tlb_range *range)
{
	u64 ept_pointer = to_vmx(vcpu)->ept_pointer;

	/*
	 * FLUSH_GUEST_PHYSICAL_ADDRESS_SPACE hypercall needs address
	 * of the base of EPT PML4 table, strip off EPT configuration
	 * information.
	 */
	if (range)
		return hyperv_flush_guest_mapping_range(ept_pointer & PAGE_MASK,
				kvm_fill_hv_flush_list_func, (void *)range);
	else
		return hyperv_flush_guest_mapping(ept_pointer & PAGE_MASK);
}

static int hv_remote_flush_tlb_with_range(struct kvm *kvm,
		struct kvm_tlb_range *range)
{
	struct kvm_vcpu *vcpu;
	int ret = 0, i;

	spin_lock(&to_kvm_vmx(kvm)->ept_pointer_lock);

	if (to_kvm_vmx(kvm)->ept_pointers_match == EPT_POINTERS_CHECK)
		check_ept_pointer_match(kvm);

	if (to_kvm_vmx(kvm)->ept_pointers_match != EPT_POINTERS_MATCH) {
		kvm_for_each_vcpu(i, vcpu, kvm) {
			/* If ept_pointer is invalid pointer, bypass flush request. */
			if (VALID_PAGE(to_vmx(vcpu)->ept_pointer))
				ret |= __hv_remote_flush_tlb_with_range(
					kvm, vcpu, range);
		}
	} else {
		ret = __hv_remote_flush_tlb_with_range(kvm,
				kvm_get_vcpu(kvm, 0), range);
	}

	spin_unlock(&to_kvm_vmx(kvm)->ept_pointer_lock);
	return ret;
}
static int hv_remote_flush_tlb(struct kvm *kvm)
{
	return hv_remote_flush_tlb_with_range(kvm, NULL);
}

static int hv_enable_direct_tlbflush(struct kvm_vcpu *vcpu)
{
	struct hv_enlightened_vmcs *evmcs;
	struct hv_partition_assist_pg **p_hv_pa_pg =
			&vcpu->kvm->arch.hyperv.hv_pa_pg;
	/*
	 * Synthetic VM-Exit is not enabled in current code and so All
	 * evmcs in singe VM shares same assist page.
	 */
	if (!*p_hv_pa_pg)
		*p_hv_pa_pg = kzalloc(PAGE_SIZE, GFP_KERNEL);

	if (!*p_hv_pa_pg)
		return -ENOMEM;

	evmcs = (struct hv_enlightened_vmcs *)to_vmx(vcpu)->loaded_vmcs->vmcs;

	evmcs->partition_assist_page =
		__pa(*p_hv_pa_pg);
	evmcs->hv_vm_id = (unsigned long)vcpu->kvm;
	evmcs->hv_enlightenments_control.nested_flush_hypercall = 1;

	return 0;
}

#endif /* IS_ENABLED(CONFIG_HYPERV) */

/*
 * Comment's format: document - errata name - stepping - processor name.
 * Refer from
 * https://www.virtualbox.org/svn/vbox/trunk/src/VBox/VMM/VMMR0/HMR0.cpp
 */
static u32 vmx_preemption_cpu_tfms[] = {
/* 323344.pdf - BA86   - D0 - Xeon 7500 Series */
0x000206E6,
/* 323056.pdf - AAX65  - C2 - Xeon L3406 */
/* 322814.pdf - AAT59  - C2 - i7-600, i5-500, i5-400 and i3-300 Mobile */
/* 322911.pdf - AAU65  - C2 - i5-600, i3-500 Desktop and Pentium G6950 */
0x00020652,
/* 322911.pdf - AAU65  - K0 - i5-600, i3-500 Desktop and Pentium G6950 */
0x00020655,
/* 322373.pdf - AAO95  - B1 - Xeon 3400 Series */
/* 322166.pdf - AAN92  - B1 - i7-800 and i5-700 Desktop */
/*
 * 320767.pdf - AAP86  - B1 -
 * i7-900 Mobile Extreme, i7-800 and i7-700 Mobile
 */
0x000106E5,
/* 321333.pdf - AAM126 - C0 - Xeon 3500 */
0x000106A0,
/* 321333.pdf - AAM126 - C1 - Xeon 3500 */
0x000106A1,
/* 320836.pdf - AAJ124 - C0 - i7-900 Desktop Extreme and i7-900 Desktop */
0x000106A4,
 /* 321333.pdf - AAM126 - D0 - Xeon 3500 */
 /* 321324.pdf - AAK139 - D0 - Xeon 5500 */
 /* 320836.pdf - AAJ124 - D0 - i7-900 Extreme and i7-900 Desktop */
0x000106A5,
 /* Xeon E3-1220 V2 */
0x000306A8,
};

static inline bool cpu_has_broken_vmx_preemption_timer(void)
{
	u32 eax = cpuid_eax(0x00000001), i;

	/* Clear the reserved bits */
	eax &= ~(0x3U << 14 | 0xfU << 28);
	for (i = 0; i < ARRAY_SIZE(vmx_preemption_cpu_tfms); i++)
		if (eax == vmx_preemption_cpu_tfms[i])
			return true;

	return false;
}

static inline bool cpu_need_virtualize_apic_accesses(struct kvm_vcpu *vcpu)
{
	return flexpriority_enabled && lapic_in_kernel(vcpu);
}

static inline bool report_flexpriority(void)
{
	return flexpriority_enabled;
}

static inline int __find_msr_index(struct vcpu_vmx *vmx, u32 msr)
{
	int i;

	for (i = 0; i < vmx->nmsrs; ++i)
		if (vmx_msr_index[vmx->guest_msrs[i].index] == msr)
			return i;
	return -1;
}

struct shared_msr_entry *find_msr_entry(struct vcpu_vmx *vmx, u32 msr)
{
	int i;

	i = __find_msr_index(vmx, msr);
	if (i >= 0)
		return &vmx->guest_msrs[i];
	return NULL;
}

static int vmx_set_guest_msr(struct vcpu_vmx *vmx, struct shared_msr_entry *msr, u64 data)
{
	int ret = 0;

	u64 old_msr_data = msr->data;
	msr->data = data;
	if (msr - vmx->guest_msrs < vmx->save_nmsrs) {
		preempt_disable();
		ret = kvm_set_shared_msr(msr->index, msr->data,
					 msr->mask);
		preempt_enable();
		if (ret)
			msr->data = old_msr_data;
	}
	return ret;
}

void loaded_vmcs_init(struct loaded_vmcs *loaded_vmcs)
{
	vmcs_clear(loaded_vmcs->vmcs);
	if (loaded_vmcs->shadow_vmcs && loaded_vmcs->launched)
		vmcs_clear(loaded_vmcs->shadow_vmcs);
	loaded_vmcs->cpu = -1;
	loaded_vmcs->launched = 0;
}

#ifdef CONFIG_KEXEC_CORE
/*
 * This bitmap is used to indicate whether the vmclear
 * operation is enabled on all cpus. All disabled by
 * default.
 */
static cpumask_t crash_vmclear_enabled_bitmap = CPU_MASK_NONE;

static inline void crash_enable_local_vmclear(int cpu)
{
	cpumask_set_cpu(cpu, &crash_vmclear_enabled_bitmap);
}

static inline void crash_disable_local_vmclear(int cpu)
{
	cpumask_clear_cpu(cpu, &crash_vmclear_enabled_bitmap);
}

static inline int crash_local_vmclear_enabled(int cpu)
{
	return cpumask_test_cpu(cpu, &crash_vmclear_enabled_bitmap);
}

static void crash_vmclear_local_loaded_vmcss(void)
{
	int cpu = raw_smp_processor_id();
	struct loaded_vmcs *v;

	if (!crash_local_vmclear_enabled(cpu))
		return;

	list_for_each_entry(v, &per_cpu(loaded_vmcss_on_cpu, cpu),
			    loaded_vmcss_on_cpu_link)
		vmcs_clear(v->vmcs);
}
#else
static inline void crash_enable_local_vmclear(int cpu) { }
static inline void crash_disable_local_vmclear(int cpu) { }
#endif /* CONFIG_KEXEC_CORE */

static void __loaded_vmcs_clear(void *arg)
{
	struct loaded_vmcs *loaded_vmcs = arg;
	int cpu = raw_smp_processor_id();

	if (loaded_vmcs->cpu != cpu)
		return; /* vcpu migration can race with cpu offline */
	if (per_cpu(current_vmcs, cpu) == loaded_vmcs->vmcs)
		per_cpu(current_vmcs, cpu) = NULL;
	crash_disable_local_vmclear(cpu);
	list_del(&loaded_vmcs->loaded_vmcss_on_cpu_link);

	/*
	 * we should ensure updating loaded_vmcs->loaded_vmcss_on_cpu_link
	 * is before setting loaded_vmcs->vcpu to -1 which is done in
	 * loaded_vmcs_init. Otherwise, other cpu can see vcpu = -1 fist
	 * then adds the vmcs into percpu list before it is deleted.
	 */
	smp_wmb();

	loaded_vmcs_init(loaded_vmcs);
	crash_enable_local_vmclear(cpu);
}

void loaded_vmcs_clear(struct loaded_vmcs *loaded_vmcs)
{
	int cpu = loaded_vmcs->cpu;

	if (cpu != -1)
		smp_call_function_single(cpu,
			 __loaded_vmcs_clear, loaded_vmcs, 1);
}

static bool vmx_segment_cache_test_set(struct vcpu_vmx *vmx, unsigned seg,
				       unsigned field)
{
	bool ret;
	u32 mask = 1 << (seg * SEG_FIELD_NR + field);

	if (!kvm_register_is_available(&vmx->vcpu, VCPU_EXREG_SEGMENTS)) {
		kvm_register_mark_available(&vmx->vcpu, VCPU_EXREG_SEGMENTS);
		vmx->segment_cache.bitmask = 0;
	}
	ret = vmx->segment_cache.bitmask & mask;
	vmx->segment_cache.bitmask |= mask;
	return ret;
}

static u16 vmx_read_guest_seg_selector(struct vcpu_vmx *vmx, unsigned seg)
{
	u16 *p = &vmx->segment_cache.seg[seg].selector;

	if (!vmx_segment_cache_test_set(vmx, seg, SEG_FIELD_SEL))
		*p = vmcs_read16(kvm_vmx_segment_fields[seg].selector);
	return *p;
}

static ulong vmx_read_guest_seg_base(struct vcpu_vmx *vmx, unsigned seg)
{
	ulong *p = &vmx->segment_cache.seg[seg].base;

	if (!vmx_segment_cache_test_set(vmx, seg, SEG_FIELD_BASE))
		*p = vmcs_readl(kvm_vmx_segment_fields[seg].base);
	return *p;
}

static u32 vmx_read_guest_seg_limit(struct vcpu_vmx *vmx, unsigned seg)
{
	u32 *p = &vmx->segment_cache.seg[seg].limit;

	if (!vmx_segment_cache_test_set(vmx, seg, SEG_FIELD_LIMIT))
		*p = vmcs_read32(kvm_vmx_segment_fields[seg].limit);
	return *p;
}

static u32 vmx_read_guest_seg_ar(struct vcpu_vmx *vmx, unsigned seg)
{
	u32 *p = &vmx->segment_cache.seg[seg].ar;

	if (!vmx_segment_cache_test_set(vmx, seg, SEG_FIELD_AR))
		*p = vmcs_read32(kvm_vmx_segment_fields[seg].ar_bytes);
	return *p;
}

void update_exception_bitmap(struct kvm_vcpu *vcpu)
{
	u32 eb;

	eb = (1u << PF_VECTOR) | (1u << UD_VECTOR) | (1u << MC_VECTOR) |
	     (1u << DB_VECTOR) | (1u << AC_VECTOR);
	/*
	 * Guest access to VMware backdoor ports could legitimately
	 * trigger #GP because of TSS I/O permission bitmap.
	 * We intercept those #GP and allow access to them anyway
	 * as VMware does.
	 */
	if (enable_vmware_backdoor)
		eb |= (1u << GP_VECTOR);
	if ((vcpu->guest_debug &
	     (KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP)) ==
	    (KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP))
		eb |= 1u << BP_VECTOR;
	if (to_vmx(vcpu)->rmode.vm86_active)
		eb = ~0;
	if (enable_ept)
		eb &= ~(1u << PF_VECTOR); /* bypass_guest_pf = 0 */

	/* When we are running a nested L2 guest and L1 specified for it a
	 * certain exception bitmap, we must trap the same exceptions and pass
	 * them to L1. When running L2, we will only handle the exceptions
	 * specified above if L1 did not want them.
	 */
	if (is_guest_mode(vcpu))
		eb |= get_vmcs12(vcpu)->exception_bitmap;

	vmcs_write32(EXCEPTION_BITMAP, eb);
}

/*
 * Check if MSR is intercepted for currently loaded MSR bitmap.
 */
static bool msr_write_intercepted(struct kvm_vcpu *vcpu, u32 msr)
{
	unsigned long *msr_bitmap;
	int f = sizeof(unsigned long);

	if (!cpu_has_vmx_msr_bitmap())
		return true;

	msr_bitmap = to_vmx(vcpu)->loaded_vmcs->msr_bitmap;

	if (msr <= 0x1fff) {
		return !!test_bit(msr, msr_bitmap + 0x800 / f);
	} else if ((msr >= 0xc0000000) && (msr <= 0xc0001fff)) {
		msr &= 0x1fff;
		return !!test_bit(msr, msr_bitmap + 0xc00 / f);
	}

	return true;
}

static void clear_atomic_switch_msr_special(struct vcpu_vmx *vmx,
		unsigned long entry, unsigned long exit)
{
	vm_entry_controls_clearbit(vmx, entry);
	vm_exit_controls_clearbit(vmx, exit);
}

int vmx_find_msr_index(struct vmx_msrs *m, u32 msr)
{
	unsigned int i;

	for (i = 0; i < m->nr; ++i) {
		if (m->val[i].index == msr)
			return i;
	}
	return -ENOENT;
}

static void clear_atomic_switch_msr(struct vcpu_vmx *vmx, unsigned msr)
{
	int i;
	struct msr_autoload *m = &vmx->msr_autoload;

	switch (msr) {
	case MSR_EFER:
		if (cpu_has_load_ia32_efer()) {
			clear_atomic_switch_msr_special(vmx,
					VM_ENTRY_LOAD_IA32_EFER,
					VM_EXIT_LOAD_IA32_EFER);
			return;
		}
		break;
	case MSR_CORE_PERF_GLOBAL_CTRL:
		if (cpu_has_load_perf_global_ctrl()) {
			clear_atomic_switch_msr_special(vmx,
					VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL,
					VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL);
			return;
		}
		break;
	}
	i = vmx_find_msr_index(&m->guest, msr);
	if (i < 0)
		goto skip_guest;
	--m->guest.nr;
	m->guest.val[i] = m->guest.val[m->guest.nr];
	vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, m->guest.nr);

skip_guest:
	i = vmx_find_msr_index(&m->host, msr);
	if (i < 0)
		return;

	--m->host.nr;
	m->host.val[i] = m->host.val[m->host.nr];
	vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, m->host.nr);
}

static void add_atomic_switch_msr_special(struct vcpu_vmx *vmx,
		unsigned long entry, unsigned long exit,
		unsigned long guest_val_vmcs, unsigned long host_val_vmcs,
		u64 guest_val, u64 host_val)
{
	vmcs_write64(guest_val_vmcs, guest_val);
	if (host_val_vmcs != HOST_IA32_EFER)
		vmcs_write64(host_val_vmcs, host_val);
	vm_entry_controls_setbit(vmx, entry);
	vm_exit_controls_setbit(vmx, exit);
}

static void add_atomic_switch_msr(struct vcpu_vmx *vmx, unsigned msr,
				  u64 guest_val, u64 host_val, bool entry_only)
{
	int i, j = 0;
	struct msr_autoload *m = &vmx->msr_autoload;

	switch (msr) {
	case MSR_EFER:
		if (cpu_has_load_ia32_efer()) {
			add_atomic_switch_msr_special(vmx,
					VM_ENTRY_LOAD_IA32_EFER,
					VM_EXIT_LOAD_IA32_EFER,
					GUEST_IA32_EFER,
					HOST_IA32_EFER,
					guest_val, host_val);
			return;
		}
		break;
	case MSR_CORE_PERF_GLOBAL_CTRL:
		if (cpu_has_load_perf_global_ctrl()) {
			add_atomic_switch_msr_special(vmx,
					VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL,
					VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL,
					GUEST_IA32_PERF_GLOBAL_CTRL,
					HOST_IA32_PERF_GLOBAL_CTRL,
					guest_val, host_val);
			return;
		}
		break;
	case MSR_IA32_PEBS_ENABLE:
		/* PEBS needs a quiescent period after being disabled (to write
		 * a record).  Disabling PEBS through VMX MSR swapping doesn't
		 * provide that period, so a CPU could write host's record into
		 * guest's memory.
		 */
		wrmsrl(MSR_IA32_PEBS_ENABLE, 0);
	}

	i = vmx_find_msr_index(&m->guest, msr);
	if (!entry_only)
		j = vmx_find_msr_index(&m->host, msr);

	if ((i < 0 && m->guest.nr == NR_LOADSTORE_MSRS) ||
		(j < 0 &&  m->host.nr == NR_LOADSTORE_MSRS)) {
		printk_once(KERN_WARNING "Not enough msr switch entries. "
				"Can't add msr %x\n", msr);
		return;
	}
	if (i < 0) {
		i = m->guest.nr++;
		vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, m->guest.nr);
	}
	m->guest.val[i].index = msr;
	m->guest.val[i].value = guest_val;

	if (entry_only)
		return;

	if (j < 0) {
		j = m->host.nr++;
		vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, m->host.nr);
	}
	m->host.val[j].index = msr;
	m->host.val[j].value = host_val;
}

static bool update_transition_efer(struct vcpu_vmx *vmx, int efer_offset)
{
	u64 guest_efer = vmx->vcpu.arch.efer;
	u64 ignore_bits = 0;

	/* Shadow paging assumes NX to be available.  */
	if (!enable_ept)
		guest_efer |= EFER_NX;

	/*
	 * LMA and LME handled by hardware; SCE meaningless outside long mode.
	 */
	ignore_bits |= EFER_SCE;
#ifdef CONFIG_X86_64
	ignore_bits |= EFER_LMA | EFER_LME;
	/* SCE is meaningful only in long mode on Intel */
	if (guest_efer & EFER_LMA)
		ignore_bits &= ~(u64)EFER_SCE;
#endif

	/*
	 * On EPT, we can't emulate NX, so we must switch EFER atomically.
	 * On CPUs that support "load IA32_EFER", always switch EFER
	 * atomically, since it's faster than switching it manually.
	 */
	if (cpu_has_load_ia32_efer() ||
	    (enable_ept && ((vmx->vcpu.arch.efer ^ host_efer) & EFER_NX))) {
		if (!(guest_efer & EFER_LMA))
			guest_efer &= ~EFER_LME;
		if (guest_efer != host_efer)
			add_atomic_switch_msr(vmx, MSR_EFER,
					      guest_efer, host_efer, false);
		else
			clear_atomic_switch_msr(vmx, MSR_EFER);
		return false;
	} else {
		clear_atomic_switch_msr(vmx, MSR_EFER);

		guest_efer &= ~ignore_bits;
		guest_efer |= host_efer & ignore_bits;

		vmx->guest_msrs[efer_offset].data = guest_efer;
		vmx->guest_msrs[efer_offset].mask = ~ignore_bits;

		return true;
	}
}

#ifdef CONFIG_X86_32
/*
 * On 32-bit kernels, VM exits still load the FS and GS bases from the
 * VMCS rather than the segment table.  KVM uses this helper to figure
 * out the current bases to poke them into the VMCS before entry.
 */
static unsigned long segment_base(u16 selector)
{
	struct desc_struct *table;
	unsigned long v;

	if (!(selector & ~SEGMENT_RPL_MASK))
		return 0;

	table = get_current_gdt_ro();

	if ((selector & SEGMENT_TI_MASK) == SEGMENT_LDT) {
		u16 ldt_selector = kvm_read_ldt();

		if (!(ldt_selector & ~SEGMENT_RPL_MASK))
			return 0;

		table = (struct desc_struct *)segment_base(ldt_selector);
	}
	v = get_desc_base(&table[selector >> 3]);
	return v;
}
#endif

static inline bool pt_can_write_msr(struct vcpu_vmx *vmx)
{
	return (pt_mode == PT_MODE_HOST_GUEST) &&
	       !(vmx->pt_desc.guest.ctl & RTIT_CTL_TRACEEN);
}

static inline void pt_load_msr(struct pt_ctx *ctx, u32 addr_range)
{
	u32 i;

	wrmsrl(MSR_IA32_RTIT_STATUS, ctx->status);
	wrmsrl(MSR_IA32_RTIT_OUTPUT_BASE, ctx->output_base);
	wrmsrl(MSR_IA32_RTIT_OUTPUT_MASK, ctx->output_mask);
	wrmsrl(MSR_IA32_RTIT_CR3_MATCH, ctx->cr3_match);
	for (i = 0; i < addr_range; i++) {
		wrmsrl(MSR_IA32_RTIT_ADDR0_A + i * 2, ctx->addr_a[i]);
		wrmsrl(MSR_IA32_RTIT_ADDR0_B + i * 2, ctx->addr_b[i]);
	}
}

static inline void pt_save_msr(struct pt_ctx *ctx, u32 addr_range)
{
	u32 i;

	rdmsrl(MSR_IA32_RTIT_STATUS, ctx->status);
	rdmsrl(MSR_IA32_RTIT_OUTPUT_BASE, ctx->output_base);
	rdmsrl(MSR_IA32_RTIT_OUTPUT_MASK, ctx->output_mask);
	rdmsrl(MSR_IA32_RTIT_CR3_MATCH, ctx->cr3_match);
	for (i = 0; i < addr_range; i++) {
		rdmsrl(MSR_IA32_RTIT_ADDR0_A + i * 2, ctx->addr_a[i]);
		rdmsrl(MSR_IA32_RTIT_ADDR0_B + i * 2, ctx->addr_b[i]);
	}
}

static void pt_guest_enter(struct vcpu_vmx *vmx)
{
	if (pt_mode == PT_MODE_SYSTEM)
		return;

	/*
	 * GUEST_IA32_RTIT_CTL is already set in the VMCS.
	 * Save host state before VM entry.
	 */
	rdmsrl(MSR_IA32_RTIT_CTL, vmx->pt_desc.host.ctl);
	if (vmx->pt_desc.guest.ctl & RTIT_CTL_TRACEEN) {
		wrmsrl(MSR_IA32_RTIT_CTL, 0);
		pt_save_msr(&vmx->pt_desc.host, vmx->pt_desc.addr_range);
		pt_load_msr(&vmx->pt_desc.guest, vmx->pt_desc.addr_range);
	}
}

static void pt_guest_exit(struct vcpu_vmx *vmx)
{
	if (pt_mode == PT_MODE_SYSTEM)
		return;

	if (vmx->pt_desc.guest.ctl & RTIT_CTL_TRACEEN) {
		pt_save_msr(&vmx->pt_desc.guest, vmx->pt_desc.addr_range);
		pt_load_msr(&vmx->pt_desc.host, vmx->pt_desc.addr_range);
	}

	/* Reload host state (IA32_RTIT_CTL will be cleared on VM exit). */
	wrmsrl(MSR_IA32_RTIT_CTL, vmx->pt_desc.host.ctl);
}

void vmx_set_host_fs_gs(struct vmcs_host_state *host, u16 fs_sel, u16 gs_sel,
			unsigned long fs_base, unsigned long gs_base)
{
	if (unlikely(fs_sel != host->fs_sel)) {
		if (!(fs_sel & 7))
			vmcs_write16(HOST_FS_SELECTOR, fs_sel);
		else
			vmcs_write16(HOST_FS_SELECTOR, 0);
		host->fs_sel = fs_sel;
	}
	if (unlikely(gs_sel != host->gs_sel)) {
		if (!(gs_sel & 7))
			vmcs_write16(HOST_GS_SELECTOR, gs_sel);
		else
			vmcs_write16(HOST_GS_SELECTOR, 0);
		host->gs_sel = gs_sel;
	}
	if (unlikely(fs_base != host->fs_base)) {
		vmcs_writel(HOST_FS_BASE, fs_base);
		host->fs_base = fs_base;
	}
	if (unlikely(gs_base != host->gs_base)) {
		vmcs_writel(HOST_GS_BASE, gs_base);
		host->gs_base = gs_base;
	}
}

void vmx_prepare_switch_to_guest(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct vmcs_host_state *host_state;
#ifdef CONFIG_X86_64
	int cpu = raw_smp_processor_id();
#endif
	unsigned long fs_base, gs_base;
	u16 fs_sel, gs_sel;
	int i;

	vmx->req_immediate_exit = false;

	/*
	 * Note that guest MSRs to be saved/restored can also be changed
	 * when guest state is loaded. This happens when guest transitions
	 * to/from long-mode by setting MSR_EFER.LMA.
	 */
	if (!vmx->guest_msrs_ready) {
		vmx->guest_msrs_ready = true;
		for (i = 0; i < vmx->save_nmsrs; ++i)
			kvm_set_shared_msr(vmx->guest_msrs[i].index,
					   vmx->guest_msrs[i].data,
					   vmx->guest_msrs[i].mask);

	}

    	if (vmx->nested.need_vmcs12_to_shadow_sync)
		nested_sync_vmcs12_to_shadow(vcpu);

	if (vmx->guest_state_loaded)
		return;

	host_state = &vmx->loaded_vmcs->host_state;

	/*
	 * Set host fs and gs selectors.  Unfortunately, 22.2.3 does not
	 * allow segment selectors with cpl > 0 or ti == 1.
	 */
	host_state->ldt_sel = kvm_read_ldt();

#ifdef CONFIG_X86_64
	savesegment(ds, host_state->ds_sel);
	savesegment(es, host_state->es_sel);

	gs_base = cpu_kernelmode_gs_base(cpu);
	if (likely(is_64bit_mm(current->mm))) {
		save_fsgs_for_kvm();
		fs_sel = current->thread.fsindex;
		gs_sel = current->thread.gsindex;
		fs_base = current->thread.fsbase;
		vmx->msr_host_kernel_gs_base = current->thread.gsbase;
	} else {
		savesegment(fs, fs_sel);
		savesegment(gs, gs_sel);
		fs_base = read_msr(MSR_FS_BASE);
		vmx->msr_host_kernel_gs_base = read_msr(MSR_KERNEL_GS_BASE);
	}

	wrmsrl(MSR_KERNEL_GS_BASE, vmx->msr_guest_kernel_gs_base);
#else
	savesegment(fs, fs_sel);
	savesegment(gs, gs_sel);
	fs_base = segment_base(fs_sel);
	gs_base = segment_base(gs_sel);
#endif

	vmx_set_host_fs_gs(host_state, fs_sel, gs_sel, fs_base, gs_base);
	vmx->guest_state_loaded = true;
}

static void vmx_prepare_switch_to_host(struct vcpu_vmx *vmx)
{
	struct vmcs_host_state *host_state;

	if (!vmx->guest_state_loaded)
		return;

	host_state = &vmx->loaded_vmcs->host_state;

	++vmx->vcpu.stat.host_state_reload;

#ifdef CONFIG_X86_64
	rdmsrl(MSR_KERNEL_GS_BASE, vmx->msr_guest_kernel_gs_base);
#endif
	if (host_state->ldt_sel || (host_state->gs_sel & 7)) {
		kvm_load_ldt(host_state->ldt_sel);
#ifdef CONFIG_X86_64
		load_gs_index(host_state->gs_sel);
#else
		loadsegment(gs, host_state->gs_sel);
#endif
	}
	if (host_state->fs_sel & 7)
		loadsegment(fs, host_state->fs_sel);
#ifdef CONFIG_X86_64
	if (unlikely(host_state->ds_sel | host_state->es_sel)) {
		loadsegment(ds, host_state->ds_sel);
		loadsegment(es, host_state->es_sel);
	}
#endif
	invalidate_tss_limit();
#ifdef CONFIG_X86_64
	wrmsrl(MSR_KERNEL_GS_BASE, vmx->msr_host_kernel_gs_base);
#endif
	load_fixmap_gdt(raw_smp_processor_id());
	vmx->guest_state_loaded = false;
	vmx->guest_msrs_ready = false;
}

#ifdef CONFIG_X86_64
static u64 vmx_read_guest_kernel_gs_base(struct vcpu_vmx *vmx)
{
	preempt_disable();
	if (vmx->guest_state_loaded)
		rdmsrl(MSR_KERNEL_GS_BASE, vmx->msr_guest_kernel_gs_base);
	preempt_enable();
	return vmx->msr_guest_kernel_gs_base;
}

static void vmx_write_guest_kernel_gs_base(struct vcpu_vmx *vmx, u64 data)
{
	preempt_disable();
	if (vmx->guest_state_loaded)
		wrmsrl(MSR_KERNEL_GS_BASE, data);
	preempt_enable();
	vmx->msr_guest_kernel_gs_base = data;
}
#endif

static void vmx_vcpu_pi_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);
	struct pi_desc old, new;
	unsigned int dest;

	/*
	 * In case of hot-plug or hot-unplug, we may have to undo
	 * vmx_vcpu_pi_put even if there is no assigned device.  And we
	 * always keep PI.NDST up to date for simplicity: it makes the
	 * code easier, and CPU migration is not a fast path.
	 */
	if (!pi_test_sn(pi_desc) && vcpu->cpu == cpu)
		return;

	/*
	 * If the 'nv' field is POSTED_INTR_WAKEUP_VECTOR, do not change
	 * PI.NDST: pi_post_block is the one expected to change PID.NDST and the
	 * wakeup handler expects the vCPU to be on the blocked_vcpu_list that
	 * matches PI.NDST. Otherwise, a vcpu may not be able to be woken up
	 * correctly.
	 */
	if (pi_desc->nv == POSTED_INTR_WAKEUP_VECTOR || vcpu->cpu == cpu) {
		pi_clear_sn(pi_desc);
		goto after_clear_sn;
	}

	/* The full case.  */
	do {
		old.control = new.control = pi_desc->control;

		dest = cpu_physical_id(cpu);

		if (x2apic_enabled())
			new.ndst = dest;
		else
			new.ndst = (dest << 8) & 0xFF00;

		new.sn = 0;
	} while (cmpxchg64(&pi_desc->control, old.control,
			   new.control) != old.control);

after_clear_sn:

	/*
	 * Clear SN before reading the bitmap.  The VT-d firmware
	 * writes the bitmap and reads SN atomically (5.2.3 in the
	 * spec), so it doesn't really have a memory barrier that
	 * pairs with this, but we cannot do that and we need one.
	 */
	smp_mb__after_atomic();

	if (!pi_is_pir_empty(pi_desc))
		pi_set_on(pi_desc);
}

void vmx_vcpu_load_vmcs(struct kvm_vcpu *vcpu, int cpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	bool already_loaded = vmx->loaded_vmcs->cpu == cpu;

	if (!already_loaded) {
		loaded_vmcs_clear(vmx->loaded_vmcs);
		local_irq_disable();
		crash_disable_local_vmclear(cpu);

		/*
		 * Read loaded_vmcs->cpu should be before fetching
		 * loaded_vmcs->loaded_vmcss_on_cpu_link.
		 * See the comments in __loaded_vmcs_clear().
		 */
		smp_rmb();

		list_add(&vmx->loaded_vmcs->loaded_vmcss_on_cpu_link,
			 &per_cpu(loaded_vmcss_on_cpu, cpu));
		crash_enable_local_vmclear(cpu);
		local_irq_enable();
	}

	if (per_cpu(current_vmcs, cpu) != vmx->loaded_vmcs->vmcs) {
		per_cpu(current_vmcs, cpu) = vmx->loaded_vmcs->vmcs;
		vmcs_load(vmx->loaded_vmcs->vmcs);
		indirect_branch_prediction_barrier();
	}

	if (!already_loaded) {
		void *gdt = get_current_gdt_ro();
		unsigned long sysenter_esp;

		kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);

		/*
		 * Linux uses per-cpu TSS and GDT, so set these when switching
		 * processors.  See 22.2.4.
		 */
		vmcs_writel(HOST_TR_BASE,
			    (unsigned long)&get_cpu_entry_area(cpu)->tss.x86_tss);
		vmcs_writel(HOST_GDTR_BASE, (unsigned long)gdt);   /* 22.2.4 */

		rdmsrl(MSR_IA32_SYSENTER_ESP, sysenter_esp);
		vmcs_writel(HOST_IA32_SYSENTER_ESP, sysenter_esp); /* 22.2.3 */

		vmx->loaded_vmcs->cpu = cpu;
	}

	/* Setup TSC multiplier */
	if (kvm_has_tsc_control &&
	    vmx->current_tsc_ratio != vcpu->arch.tsc_scaling_ratio)
		decache_tsc_multiplier(vmx);
}

/*
 * Switches to specified vcpu, until a matching vcpu_put(), but assumes
 * vcpu mutex is already taken.
 */
void vmx_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	vmx_vcpu_load_vmcs(vcpu, cpu);

	vmx_vcpu_pi_load(vcpu, cpu);

	vmx->host_pkru = read_pkru();
	vmx->host_debugctlmsr = get_debugctlmsr();
}

static void vmx_vcpu_pi_put(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);

	if (!kvm_arch_has_assigned_device(vcpu->kvm) ||
		!irq_remapping_cap(IRQ_POSTING_CAP)  ||
		!kvm_vcpu_apicv_active(vcpu))
		return;

	/* Set SN when the vCPU is preempted */
	if (vcpu->preempted)
		pi_set_sn(pi_desc);
}

static void vmx_vcpu_put(struct kvm_vcpu *vcpu)
{
	vmx_vcpu_pi_put(vcpu);

	vmx_prepare_switch_to_host(to_vmx(vcpu));
}

static bool emulation_required(struct kvm_vcpu *vcpu)
{
	return emulate_invalid_guest_state && !guest_state_valid(vcpu);
}

unsigned long vmx_get_rflags(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long rflags, save_rflags;

	if (!kvm_register_is_available(vcpu, VCPU_EXREG_RFLAGS)) {
		kvm_register_mark_available(vcpu, VCPU_EXREG_RFLAGS);
		rflags = vmcs_readl(GUEST_RFLAGS);
		if (vmx->rmode.vm86_active) {
			rflags &= RMODE_GUEST_OWNED_EFLAGS_BITS;
			save_rflags = vmx->rmode.save_rflags;
			rflags |= save_rflags & ~RMODE_GUEST_OWNED_EFLAGS_BITS;
		}
		vmx->rflags = rflags;
	}
	return vmx->rflags;
}

void vmx_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long old_rflags;

	if (enable_unrestricted_guest) {
		kvm_register_mark_available(vcpu, VCPU_EXREG_RFLAGS);
		vmx->rflags = rflags;
		vmcs_writel(GUEST_RFLAGS, rflags);
		return;
	}

	old_rflags = vmx_get_rflags(vcpu);
	vmx->rflags = rflags;
	if (vmx->rmode.vm86_active) {
		vmx->rmode.save_rflags = rflags;
		rflags |= X86_EFLAGS_IOPL | X86_EFLAGS_VM;
	}
	vmcs_writel(GUEST_RFLAGS, rflags);

	if ((old_rflags ^ vmx->rflags) & X86_EFLAGS_VM)
		vmx->emulation_required = emulation_required(vcpu);
}

u32 vmx_get_interrupt_shadow(struct kvm_vcpu *vcpu)
{
	u32 interruptibility = vmcs_read32(GUEST_INTERRUPTIBILITY_INFO);
	int ret = 0;

	if (interruptibility & GUEST_INTR_STATE_STI)
		ret |= KVM_X86_SHADOW_INT_STI;
	if (interruptibility & GUEST_INTR_STATE_MOV_SS)
		ret |= KVM_X86_SHADOW_INT_MOV_SS;

	return ret;
}

void vmx_set_interrupt_shadow(struct kvm_vcpu *vcpu, int mask)
{
	u32 interruptibility_old = vmcs_read32(GUEST_INTERRUPTIBILITY_INFO);
	u32 interruptibility = interruptibility_old;

	interruptibility &= ~(GUEST_INTR_STATE_STI | GUEST_INTR_STATE_MOV_SS);

	if (mask & KVM_X86_SHADOW_INT_MOV_SS)
		interruptibility |= GUEST_INTR_STATE_MOV_SS;
	else if (mask & KVM_X86_SHADOW_INT_STI)
		interruptibility |= GUEST_INTR_STATE_STI;

	if ((interruptibility != interruptibility_old))
		vmcs_write32(GUEST_INTERRUPTIBILITY_INFO, interruptibility);
}

static int vmx_rtit_ctl_check(struct kvm_vcpu *vcpu, u64 data)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long value;

	/*
	 * Any MSR write that attempts to change bits marked reserved will
	 * case a #GP fault.
	 */
	if (data & vmx->pt_desc.ctl_bitmask)
		return 1;

	/*
	 * Any attempt to modify IA32_RTIT_CTL while TraceEn is set will
	 * result in a #GP unless the same write also clears TraceEn.
	 */
	if ((vmx->pt_desc.guest.ctl & RTIT_CTL_TRACEEN) &&
		((vmx->pt_desc.guest.ctl ^ data) & ~RTIT_CTL_TRACEEN))
		return 1;

	/*
	 * WRMSR to IA32_RTIT_CTL that sets TraceEn but clears this bit
	 * and FabricEn would cause #GP, if
	 * CPUID.(EAX=14H, ECX=0):ECX.SNGLRGNOUT[bit 2] = 0
	 */
	if ((data & RTIT_CTL_TRACEEN) && !(data & RTIT_CTL_TOPA) &&
		!(data & RTIT_CTL_FABRIC_EN) &&
		!intel_pt_validate_cap(vmx->pt_desc.caps,
					PT_CAP_single_range_output))
		return 1;

	/*
	 * MTCFreq, CycThresh and PSBFreq encodings check, any MSR write that
	 * utilize encodings marked reserved will casue a #GP fault.
	 */
	value = intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_mtc_periods);
	if (intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_mtc) &&
			!test_bit((data & RTIT_CTL_MTC_RANGE) >>
			RTIT_CTL_MTC_RANGE_OFFSET, &value))
		return 1;
	value = intel_pt_validate_cap(vmx->pt_desc.caps,
						PT_CAP_cycle_thresholds);
	if (intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_psb_cyc) &&
			!test_bit((data & RTIT_CTL_CYC_THRESH) >>
			RTIT_CTL_CYC_THRESH_OFFSET, &value))
		return 1;
	value = intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_psb_periods);
	if (intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_psb_cyc) &&
			!test_bit((data & RTIT_CTL_PSB_FREQ) >>
			RTIT_CTL_PSB_FREQ_OFFSET, &value))
		return 1;

	/*
	 * If ADDRx_CFG is reserved or the encodings is >2 will
	 * cause a #GP fault.
	 */
	value = (data & RTIT_CTL_ADDR0) >> RTIT_CTL_ADDR0_OFFSET;
	if ((value && (vmx->pt_desc.addr_range < 1)) || (value > 2))
		return 1;
	value = (data & RTIT_CTL_ADDR1) >> RTIT_CTL_ADDR1_OFFSET;
	if ((value && (vmx->pt_desc.addr_range < 2)) || (value > 2))
		return 1;
	value = (data & RTIT_CTL_ADDR2) >> RTIT_CTL_ADDR2_OFFSET;
	if ((value && (vmx->pt_desc.addr_range < 3)) || (value > 2))
		return 1;
	value = (data & RTIT_CTL_ADDR3) >> RTIT_CTL_ADDR3_OFFSET;
	if ((value && (vmx->pt_desc.addr_range < 4)) || (value > 2))
		return 1;

	return 0;
}

static int skip_emulated_instruction(struct kvm_vcpu *vcpu)
{
	unsigned long rip;

	/*
	 * Using VMCS.VM_EXIT_INSTRUCTION_LEN on EPT misconfig depends on
	 * undefined behavior: Intel's SDM doesn't mandate the VMCS field be
	 * set when EPT misconfig occurs.  In practice, real hardware updates
	 * VM_EXIT_INSTRUCTION_LEN on EPT misconfig, but other hypervisors
	 * (namely Hyper-V) don't set it due to it being undefined behavior,
	 * i.e. we end up advancing IP with some random value.
	 */
	if (!static_cpu_has(X86_FEATURE_HYPERVISOR) ||
	    to_vmx(vcpu)->exit_reason != EXIT_REASON_EPT_MISCONFIG) {
		rip = kvm_rip_read(vcpu);
		rip += vmcs_read32(VM_EXIT_INSTRUCTION_LEN);
		kvm_rip_write(vcpu, rip);
	} else {
		if (!kvm_emulate_instruction(vcpu, EMULTYPE_SKIP))
			return 0;
	}

	/* skipping an emulated instruction also counts */
	vmx_set_interrupt_shadow(vcpu, 0);

	return 1;
}


/*
 * Recognizes a pending MTF VM-exit and records the nested state for later
 * delivery.
 */
static void vmx_update_emulated_instruction(struct kvm_vcpu *vcpu)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (!is_guest_mode(vcpu))
		return;

	/*
	 * Per the SDM, MTF takes priority over debug-trap exceptions besides
	 * T-bit traps. As instruction emulation is completed (i.e. at the
	 * instruction boundary), any #DB exception pending delivery must be a
	 * debug-trap. Record the pending MTF state to be delivered in
	 * vmx_check_nested_events().
	 */
	if (nested_cpu_has_mtf(vmcs12) &&
	    (!vcpu->arch.exception.pending ||
	     vcpu->arch.exception.nr == DB_VECTOR))
		vmx->nested.mtf_pending = true;
	else
		vmx->nested.mtf_pending = false;
}

static int vmx_skip_emulated_instruction(struct kvm_vcpu *vcpu)
{
	vmx_update_emulated_instruction(vcpu);
	return skip_emulated_instruction(vcpu);
}

static void vmx_clear_hlt(struct kvm_vcpu *vcpu)
{
	/*
	 * Ensure that we clear the HLT state in the VMCS.  We don't need to
	 * explicitly skip the instruction because if the HLT state is set,
	 * then the instruction is already executing and RIP has already been
	 * advanced.
	 */
	if (kvm_hlt_in_guest(vcpu->kvm) &&
			vmcs_read32(GUEST_ACTIVITY_STATE) == GUEST_ACTIVITY_HLT)
		vmcs_write32(GUEST_ACTIVITY_STATE, GUEST_ACTIVITY_ACTIVE);
}

static void vmx_queue_exception(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned nr = vcpu->arch.exception.nr;
	bool has_error_code = vcpu->arch.exception.has_error_code;
	u32 error_code = vcpu->arch.exception.error_code;
	u32 intr_info = nr | INTR_INFO_VALID_MASK;

	kvm_deliver_exception_payload(vcpu);

	if (has_error_code) {
		vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, error_code);
		intr_info |= INTR_INFO_DELIVER_CODE_MASK;
	}

	if (vmx->rmode.vm86_active) {
		int inc_eip = 0;
		if (kvm_exception_is_soft(nr))
			inc_eip = vcpu->arch.event_exit_inst_len;
		kvm_inject_realmode_interrupt(vcpu, nr, inc_eip);
		return;
	}

	WARN_ON_ONCE(vmx->emulation_required);

	if (kvm_exception_is_soft(nr)) {
		vmcs_write32(VM_ENTRY_INSTRUCTION_LEN,
			     vmx->vcpu.arch.event_exit_inst_len);
		intr_info |= INTR_TYPE_SOFT_EXCEPTION;
	} else
		intr_info |= INTR_TYPE_HARD_EXCEPTION;

	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, intr_info);

	vmx_clear_hlt(vcpu);
}

static bool vmx_rdtscp_supported(void)
{
	return cpu_has_vmx_rdtscp();
}

static bool vmx_invpcid_supported(void)
{
	return cpu_has_vmx_invpcid();
}

/*
 * Swap MSR entry in host/guest MSR entry array.
 */
static void move_msr_up(struct vcpu_vmx *vmx, int from, int to)
{
	struct shared_msr_entry tmp;

	tmp = vmx->guest_msrs[to];
	vmx->guest_msrs[to] = vmx->guest_msrs[from];
	vmx->guest_msrs[from] = tmp;
}

/*
 * Set up the vmcs to automatically save and restore system
 * msrs.  Don't touch the 64-bit msrs if the guest is in legacy
 * mode, as fiddling with msrs is very expensive.
 */
static void setup_msrs(struct vcpu_vmx *vmx)
{
	int save_nmsrs, index;

	save_nmsrs = 0;
#ifdef CONFIG_X86_64
	/*
	 * The SYSCALL MSRs are only needed on long mode guests, and only
	 * when EFER.SCE is set.
	 */
	if (is_long_mode(&vmx->vcpu) && (vmx->vcpu.arch.efer & EFER_SCE)) {
		index = __find_msr_index(vmx, MSR_STAR);
		if (index >= 0)
			move_msr_up(vmx, index, save_nmsrs++);
		index = __find_msr_index(vmx, MSR_LSTAR);
		if (index >= 0)
			move_msr_up(vmx, index, save_nmsrs++);
		index = __find_msr_index(vmx, MSR_SYSCALL_MASK);
		if (index >= 0)
			move_msr_up(vmx, index, save_nmsrs++);
	}
#endif
	index = __find_msr_index(vmx, MSR_EFER);
	if (index >= 0 && update_transition_efer(vmx, index))
		move_msr_up(vmx, index, save_nmsrs++);
	index = __find_msr_index(vmx, MSR_TSC_AUX);
	if (index >= 0 && guest_cpuid_has(&vmx->vcpu, X86_FEATURE_RDTSCP))
		move_msr_up(vmx, index, save_nmsrs++);
	index = __find_msr_index(vmx, MSR_IA32_TSX_CTRL);
	if (index >= 0)
		move_msr_up(vmx, index, save_nmsrs++);

	vmx->save_nmsrs = save_nmsrs;
	vmx->guest_msrs_ready = false;

	if (cpu_has_vmx_msr_bitmap())
		vmx_update_msr_bitmap(&vmx->vcpu);
}

static u64 vmx_read_l1_tsc_offset(struct kvm_vcpu *vcpu)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);

	if (is_guest_mode(vcpu) &&
	    (vmcs12->cpu_based_vm_exec_control & CPU_BASED_USE_TSC_OFFSETTING))
		return vcpu->arch.tsc_offset - vmcs12->tsc_offset;

	return vcpu->arch.tsc_offset;
}

static u64 vmx_write_l1_tsc_offset(struct kvm_vcpu *vcpu, u64 offset)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	u64 g_tsc_offset = 0;

	/*
	 * We're here if L1 chose not to trap WRMSR to TSC. According
	 * to the spec, this should set L1's TSC; The offset that L1
	 * set for L2 remains unchanged, and still needs to be added
	 * to the newly set TSC to get L2's TSC.
	 */
	if (is_guest_mode(vcpu) &&
	    (vmcs12->cpu_based_vm_exec_control & CPU_BASED_USE_TSC_OFFSETTING))
		g_tsc_offset = vmcs12->tsc_offset;

	trace_kvm_write_tsc_offset(vcpu->vcpu_id,
				   vcpu->arch.tsc_offset - g_tsc_offset,
				   offset);
	vmcs_write64(TSC_OFFSET, offset + g_tsc_offset);
	return offset + g_tsc_offset;
}

/*
 * nested_vmx_allowed() checks whether a guest should be allowed to use VMX
 * instructions and MSRs (i.e., nested VMX). Nested VMX is disabled for
 * all guests if the "nested" module option is off, and can also be disabled
 * for a single guest by disabling its VMX cpuid bit.
 */
bool nested_vmx_allowed(struct kvm_vcpu *vcpu)
{
	return nested && guest_cpuid_has(vcpu, X86_FEATURE_VMX);
}

static inline bool vmx_feature_control_msr_valid(struct kvm_vcpu *vcpu,
						 uint64_t val)
{
	uint64_t valid_bits = to_vmx(vcpu)->msr_ia32_feature_control_valid_bits;

	return !(val & ~valid_bits);
}

static int vmx_get_msr_feature(struct kvm_msr_entry *msr)
{
	switch (msr->index) {
	case MSR_IA32_VMX_BASIC ... MSR_IA32_VMX_VMFUNC:
		if (!nested)
			return 1;
		return vmx_get_vmx_msr(&vmcs_config.nested, msr->index, &msr->data);
	default:
		return 1;
	}
}

/*
 * Reads an msr value (of 'msr_index') into 'pdata'.
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
static int vmx_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct shared_msr_entry *msr;
	u32 index;

	switch (msr_info->index) {
#ifdef CONFIG_X86_64
	case MSR_FS_BASE:
		msr_info->data = vmcs_readl(GUEST_FS_BASE);
		break;
	case MSR_GS_BASE:
		msr_info->data = vmcs_readl(GUEST_GS_BASE);
		break;
	case MSR_KERNEL_GS_BASE:
		msr_info->data = vmx_read_guest_kernel_gs_base(vmx);
		break;
#endif
	case MSR_EFER:
		return kvm_get_msr_common(vcpu, msr_info);
	case MSR_IA32_TSX_CTRL:
		if (!msr_info->host_initiated &&
		    !(vcpu->arch.arch_capabilities & ARCH_CAP_TSX_CTRL_MSR))
			return 1;
		goto find_shared_msr;
	case MSR_IA32_UMWAIT_CONTROL:
		if (!msr_info->host_initiated && !vmx_has_waitpkg(vmx))
			return 1;

		msr_info->data = vmx->msr_ia32_umwait_control;
		break;
	case MSR_IA32_SPEC_CTRL:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_SPEC_CTRL))
			return 1;

		msr_info->data = to_vmx(vcpu)->spec_ctrl;
		break;
	case MSR_IA32_SYSENTER_CS:
		msr_info->data = vmcs_read32(GUEST_SYSENTER_CS);
		break;
	case MSR_IA32_SYSENTER_EIP:
		msr_info->data = vmcs_readl(GUEST_SYSENTER_EIP);
		break;
	case MSR_IA32_SYSENTER_ESP:
		msr_info->data = vmcs_readl(GUEST_SYSENTER_ESP);
		break;
	case MSR_IA32_BNDCFGS:
		if (!kvm_mpx_supported() ||
		    (!msr_info->host_initiated &&
		     !guest_cpuid_has(vcpu, X86_FEATURE_MPX)))
			return 1;
		msr_info->data = vmcs_read64(GUEST_BNDCFGS);
		break;
	case MSR_IA32_MCG_EXT_CTL:
		if (!msr_info->host_initiated &&
		    !(vmx->msr_ia32_feature_control &
		      FEAT_CTL_LMCE_ENABLED))
			return 1;
		msr_info->data = vcpu->arch.mcg_ext_ctl;
		break;
	case MSR_IA32_FEAT_CTL:
		msr_info->data = vmx->msr_ia32_feature_control;
		break;
	case MSR_IA32_VMX_BASIC ... MSR_IA32_VMX_VMFUNC:
		if (!nested_vmx_allowed(vcpu))
			return 1;
		if (vmx_get_vmx_msr(&vmx->nested.msrs, msr_info->index,
				    &msr_info->data))
			return 1;
		/*
		 * Enlightened VMCS v1 doesn't have certain fields, but buggy
		 * Hyper-V versions are still trying to use corresponding
		 * features when they are exposed. Filter out the essential
		 * minimum.
		 */
		if (!msr_info->host_initiated &&
		    vmx->nested.enlightened_vmcs_enabled)
			nested_evmcs_filter_control_msr(msr_info->index,
							&msr_info->data);
		break;
	case MSR_IA32_RTIT_CTL:
		if (pt_mode != PT_MODE_HOST_GUEST)
			return 1;
		msr_info->data = vmx->pt_desc.guest.ctl;
		break;
	case MSR_IA32_RTIT_STATUS:
		if (pt_mode != PT_MODE_HOST_GUEST)
			return 1;
		msr_info->data = vmx->pt_desc.guest.status;
		break;
	case MSR_IA32_RTIT_CR3_MATCH:
		if ((pt_mode != PT_MODE_HOST_GUEST) ||
			!intel_pt_validate_cap(vmx->pt_desc.caps,
						PT_CAP_cr3_filtering))
			return 1;
		msr_info->data = vmx->pt_desc.guest.cr3_match;
		break;
	case MSR_IA32_RTIT_OUTPUT_BASE:
		if ((pt_mode != PT_MODE_HOST_GUEST) ||
			(!intel_pt_validate_cap(vmx->pt_desc.caps,
					PT_CAP_topa_output) &&
			 !intel_pt_validate_cap(vmx->pt_desc.caps,
					PT_CAP_single_range_output)))
			return 1;
		msr_info->data = vmx->pt_desc.guest.output_base;
		break;
	case MSR_IA32_RTIT_OUTPUT_MASK:
		if ((pt_mode != PT_MODE_HOST_GUEST) ||
			(!intel_pt_validate_cap(vmx->pt_desc.caps,
					PT_CAP_topa_output) &&
			 !intel_pt_validate_cap(vmx->pt_desc.caps,
					PT_CAP_single_range_output)))
			return 1;
		msr_info->data = vmx->pt_desc.guest.output_mask;
		break;
	case MSR_IA32_RTIT_ADDR0_A ... MSR_IA32_RTIT_ADDR3_B:
		index = msr_info->index - MSR_IA32_RTIT_ADDR0_A;
		if ((pt_mode != PT_MODE_HOST_GUEST) ||
			(index >= 2 * intel_pt_validate_cap(vmx->pt_desc.caps,
					PT_CAP_num_address_ranges)))
			return 1;
		if (index % 2)
			msr_info->data = vmx->pt_desc.guest.addr_b[index / 2];
		else
			msr_info->data = vmx->pt_desc.guest.addr_a[index / 2];
		break;
	case MSR_TSC_AUX:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_RDTSCP))
			return 1;
		goto find_shared_msr;
	default:
	find_shared_msr:
		msr = find_msr_entry(vmx, msr_info->index);
		if (msr) {
			msr_info->data = msr->data;
			break;
		}
		return kvm_get_msr_common(vcpu, msr_info);
	}

	return 0;
}

/*
 * Writes msr value into the appropriate "register".
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
static int vmx_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct shared_msr_entry *msr;
	int ret = 0;
	u32 msr_index = msr_info->index;
	u64 data = msr_info->data;
	u32 index;

	switch (msr_index) {
	case MSR_EFER:
		ret = kvm_set_msr_common(vcpu, msr_info);
		break;
#ifdef CONFIG_X86_64
	case MSR_FS_BASE:
		vmx_segment_cache_clear(vmx);
		vmcs_writel(GUEST_FS_BASE, data);
		break;
	case MSR_GS_BASE:
		vmx_segment_cache_clear(vmx);
		vmcs_writel(GUEST_GS_BASE, data);
		break;
	case MSR_KERNEL_GS_BASE:
		vmx_write_guest_kernel_gs_base(vmx, data);
		break;
#endif
	case MSR_IA32_SYSENTER_CS:
		if (is_guest_mode(vcpu))
			get_vmcs12(vcpu)->guest_sysenter_cs = data;
		vmcs_write32(GUEST_SYSENTER_CS, data);
		break;
	case MSR_IA32_SYSENTER_EIP:
		if (is_guest_mode(vcpu))
			get_vmcs12(vcpu)->guest_sysenter_eip = data;
		vmcs_writel(GUEST_SYSENTER_EIP, data);
		break;
	case MSR_IA32_SYSENTER_ESP:
		if (is_guest_mode(vcpu))
			get_vmcs12(vcpu)->guest_sysenter_esp = data;
		vmcs_writel(GUEST_SYSENTER_ESP, data);
		break;
	case MSR_IA32_DEBUGCTLMSR:
		if (is_guest_mode(vcpu) && get_vmcs12(vcpu)->vm_exit_controls &
						VM_EXIT_SAVE_DEBUG_CONTROLS)
			get_vmcs12(vcpu)->guest_ia32_debugctl = data;

		ret = kvm_set_msr_common(vcpu, msr_info);
		break;

	case MSR_IA32_BNDCFGS:
		if (!kvm_mpx_supported() ||
		    (!msr_info->host_initiated &&
		     !guest_cpuid_has(vcpu, X86_FEATURE_MPX)))
			return 1;
		if (is_noncanonical_address(data & PAGE_MASK, vcpu) ||
		    (data & MSR_IA32_BNDCFGS_RSVD))
			return 1;
		vmcs_write64(GUEST_BNDCFGS, data);
		break;
	case MSR_IA32_UMWAIT_CONTROL:
		if (!msr_info->host_initiated && !vmx_has_waitpkg(vmx))
			return 1;

		/* The reserved bit 1 and non-32 bit [63:32] should be zero */
		if (data & (BIT_ULL(1) | GENMASK_ULL(63, 32)))
			return 1;

		vmx->msr_ia32_umwait_control = data;
		break;
	case MSR_IA32_SPEC_CTRL:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_SPEC_CTRL))
			return 1;

		if (data & ~kvm_spec_ctrl_valid_bits(vcpu))
			return 1;

		vmx->spec_ctrl = data;
		if (!data)
			break;

		/*
		 * For non-nested:
		 * When it's written (to non-zero) for the first time, pass
		 * it through.
		 *
		 * For nested:
		 * The handling of the MSR bitmap for L2 guests is done in
		 * nested_vmx_prepare_msr_bitmap. We should not touch the
		 * vmcs02.msr_bitmap here since it gets completely overwritten
		 * in the merging. We update the vmcs01 here for L1 as well
		 * since it will end up touching the MSR anyway now.
		 */
		vmx_disable_intercept_for_msr(vmx->vmcs01.msr_bitmap,
					      MSR_IA32_SPEC_CTRL,
					      MSR_TYPE_RW);
		break;
	case MSR_IA32_TSX_CTRL:
		if (!msr_info->host_initiated &&
		    !(vcpu->arch.arch_capabilities & ARCH_CAP_TSX_CTRL_MSR))
			return 1;
		if (data & ~(TSX_CTRL_RTM_DISABLE | TSX_CTRL_CPUID_CLEAR))
			return 1;
		goto find_shared_msr;
	case MSR_IA32_PRED_CMD:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_SPEC_CTRL))
			return 1;

		if (data & ~PRED_CMD_IBPB)
			return 1;
		if (!boot_cpu_has(X86_FEATURE_SPEC_CTRL))
			return 1;
		if (!data)
			break;

		wrmsrl(MSR_IA32_PRED_CMD, PRED_CMD_IBPB);

		/*
		 * For non-nested:
		 * When it's written (to non-zero) for the first time, pass
		 * it through.
		 *
		 * For nested:
		 * The handling of the MSR bitmap for L2 guests is done in
		 * nested_vmx_prepare_msr_bitmap. We should not touch the
		 * vmcs02.msr_bitmap here since it gets completely overwritten
		 * in the merging.
		 */
		vmx_disable_intercept_for_msr(vmx->vmcs01.msr_bitmap, MSR_IA32_PRED_CMD,
					      MSR_TYPE_W);
		break;
	case MSR_IA32_CR_PAT:
		if (!kvm_pat_valid(data))
			return 1;

		if (is_guest_mode(vcpu) &&
		    get_vmcs12(vcpu)->vm_exit_controls & VM_EXIT_SAVE_IA32_PAT)
			get_vmcs12(vcpu)->guest_ia32_pat = data;

		if (vmcs_config.vmentry_ctrl & VM_ENTRY_LOAD_IA32_PAT) {
			vmcs_write64(GUEST_IA32_PAT, data);
			vcpu->arch.pat = data;
			break;
		}
		ret = kvm_set_msr_common(vcpu, msr_info);
		break;
	case MSR_IA32_TSC_ADJUST:
		ret = kvm_set_msr_common(vcpu, msr_info);
		break;
	case MSR_IA32_MCG_EXT_CTL:
		if ((!msr_info->host_initiated &&
		     !(to_vmx(vcpu)->msr_ia32_feature_control &
		       FEAT_CTL_LMCE_ENABLED)) ||
		    (data & ~MCG_EXT_CTL_LMCE_EN))
			return 1;
		vcpu->arch.mcg_ext_ctl = data;
		break;
	case MSR_IA32_FEAT_CTL:
		if (!vmx_feature_control_msr_valid(vcpu, data) ||
		    (to_vmx(vcpu)->msr_ia32_feature_control &
		     FEAT_CTL_LOCKED && !msr_info->host_initiated))
			return 1;
		vmx->msr_ia32_feature_control = data;
		if (msr_info->host_initiated && data == 0)
			vmx_leave_nested(vcpu);
		break;
	case MSR_IA32_VMX_BASIC ... MSR_IA32_VMX_VMFUNC:
		if (!msr_info->host_initiated)
			return 1; /* they are read-only */
		if (!nested_vmx_allowed(vcpu))
			return 1;
		return vmx_set_vmx_msr(vcpu, msr_index, data);
	case MSR_IA32_RTIT_CTL:
		if ((pt_mode != PT_MODE_HOST_GUEST) ||
			vmx_rtit_ctl_check(vcpu, data) ||
			vmx->nested.vmxon)
			return 1;
		vmcs_write64(GUEST_IA32_RTIT_CTL, data);
		vmx->pt_desc.guest.ctl = data;
		pt_update_intercept_for_msr(vmx);
		break;
	case MSR_IA32_RTIT_STATUS:
		if (!pt_can_write_msr(vmx))
			return 1;
		if (data & MSR_IA32_RTIT_STATUS_MASK)
			return 1;
		vmx->pt_desc.guest.status = data;
		break;
	case MSR_IA32_RTIT_CR3_MATCH:
		if (!pt_can_write_msr(vmx))
			return 1;
		if (!intel_pt_validate_cap(vmx->pt_desc.caps,
					   PT_CAP_cr3_filtering))
			return 1;
		vmx->pt_desc.guest.cr3_match = data;
		break;
	case MSR_IA32_RTIT_OUTPUT_BASE:
		if (!pt_can_write_msr(vmx))
			return 1;
		if (!intel_pt_validate_cap(vmx->pt_desc.caps,
					   PT_CAP_topa_output) &&
		    !intel_pt_validate_cap(vmx->pt_desc.caps,
					   PT_CAP_single_range_output))
			return 1;
		if (data & MSR_IA32_RTIT_OUTPUT_BASE_MASK)
			return 1;
		vmx->pt_desc.guest.output_base = data;
		break;
	case MSR_IA32_RTIT_OUTPUT_MASK:
		if (!pt_can_write_msr(vmx))
			return 1;
		if (!intel_pt_validate_cap(vmx->pt_desc.caps,
					   PT_CAP_topa_output) &&
		    !intel_pt_validate_cap(vmx->pt_desc.caps,
					   PT_CAP_single_range_output))
			return 1;
		vmx->pt_desc.guest.output_mask = data;
		break;
	case MSR_IA32_RTIT_ADDR0_A ... MSR_IA32_RTIT_ADDR3_B:
		if (!pt_can_write_msr(vmx))
			return 1;
		index = msr_info->index - MSR_IA32_RTIT_ADDR0_A;
		if (index >= 2 * intel_pt_validate_cap(vmx->pt_desc.caps,
						       PT_CAP_num_address_ranges))
			return 1;
		if (is_noncanonical_address(data, vcpu))
			return 1;
		if (index % 2)
			vmx->pt_desc.guest.addr_b[index / 2] = data;
		else
			vmx->pt_desc.guest.addr_a[index / 2] = data;
		break;
	case MSR_TSC_AUX:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_RDTSCP))
			return 1;
		/* Check reserved bit, higher 32 bits should be zero */
		if ((data >> 32) != 0)
			return 1;
		goto find_shared_msr;

	default:
	find_shared_msr:
		msr = find_msr_entry(vmx, msr_index);
		if (msr)
			ret = vmx_set_guest_msr(vmx, msr, data);
		else
			ret = kvm_set_msr_common(vcpu, msr_info);
	}

	return ret;
}

static void vmx_cache_reg(struct kvm_vcpu *vcpu, enum kvm_reg reg)
{
	kvm_register_mark_available(vcpu, reg);

	switch (reg) {
	case VCPU_REGS_RSP:
		vcpu->arch.regs[VCPU_REGS_RSP] = vmcs_readl(GUEST_RSP);
		break;
	case VCPU_REGS_RIP:
		vcpu->arch.regs[VCPU_REGS_RIP] = vmcs_readl(GUEST_RIP);
		break;
	case VCPU_EXREG_PDPTR:
		if (enable_ept)
			ept_save_pdptrs(vcpu);
		break;
	case VCPU_EXREG_CR3:
		if (enable_unrestricted_guest || (enable_ept && is_paging(vcpu)))
			vcpu->arch.cr3 = vmcs_readl(GUEST_CR3);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

static __init int cpu_has_kvm_support(void)
{
	return cpu_has_vmx();
}

static __init int vmx_disabled_by_bios(void)
{
	return !boot_cpu_has(X86_FEATURE_MSR_IA32_FEAT_CTL) ||
	       !boot_cpu_has(X86_FEATURE_VMX);
}

static void kvm_cpu_vmxon(u64 addr)
{
	cr4_set_bits(X86_CR4_VMXE);
	intel_pt_handle_vmx(1);

	asm volatile ("vmxon %0" : : "m"(addr));
}

static int hardware_enable(void)
{
	int cpu = raw_smp_processor_id();
	u64 phys_addr = __pa(per_cpu(vmxarea, cpu));

	if (cr4_read_shadow() & X86_CR4_VMXE)
		return -EBUSY;

	/*
	 * This can happen if we hot-added a CPU but failed to allocate
	 * VP assist page for it.
	 */
	if (static_branch_unlikely(&enable_evmcs) &&
	    !hv_get_vp_assist_page(cpu))
		return -EFAULT;

	INIT_LIST_HEAD(&per_cpu(loaded_vmcss_on_cpu, cpu));
	INIT_LIST_HEAD(&per_cpu(blocked_vcpu_on_cpu, cpu));
	spin_lock_init(&per_cpu(blocked_vcpu_on_cpu_lock, cpu));

	/*
	 * Now we can enable the vmclear operation in kdump
	 * since the loaded_vmcss_on_cpu list on this cpu
	 * has been initialized.
	 *
	 * Though the cpu is not in VMX operation now, there
	 * is no problem to enable the vmclear operation
	 * for the loaded_vmcss_on_cpu list is empty!
	 */
	crash_enable_local_vmclear(cpu);

	kvm_cpu_vmxon(phys_addr);
	if (enable_ept)
		ept_sync_global();

	return 0;
}

static void vmclear_local_loaded_vmcss(void)
{
	int cpu = raw_smp_processor_id();
	struct loaded_vmcs *v, *n;

	list_for_each_entry_safe(v, n, &per_cpu(loaded_vmcss_on_cpu, cpu),
				 loaded_vmcss_on_cpu_link)
		__loaded_vmcs_clear(v);
}


/* Just like cpu_vmxoff(), but with the __kvm_handle_fault_on_reboot()
 * tricks.
 */
static void kvm_cpu_vmxoff(void)
{
	asm volatile (__ex("vmxoff"));

	intel_pt_handle_vmx(0);
	cr4_clear_bits(X86_CR4_VMXE);
}

static void hardware_disable(void)
{
	vmclear_local_loaded_vmcss();
	kvm_cpu_vmxoff();
}

static __init int adjust_vmx_controls(u32 ctl_min, u32 ctl_opt,
				      u32 msr, u32 *result)
{
	u32 vmx_msr_low, vmx_msr_high;
	u32 ctl = ctl_min | ctl_opt;

	rdmsr(msr, vmx_msr_low, vmx_msr_high);

	ctl &= vmx_msr_high; /* bit == 0 in high word ==> must be zero */
	ctl |= vmx_msr_low;  /* bit == 1 in low word  ==> must be one  */

	/* Ensure minimum (required) set of control bits are supported. */
	if (ctl_min & ~ctl)
		return -EIO;

	*result = ctl;
	return 0;
}

static __init int setup_vmcs_config(struct vmcs_config *vmcs_conf,
				    struct vmx_capability *vmx_cap)
{
	u32 vmx_msr_low, vmx_msr_high;
	u32 min, opt, min2, opt2;
	u32 _pin_based_exec_control = 0;
	u32 _cpu_based_exec_control = 0;
	u32 _cpu_based_2nd_exec_control = 0;
	u32 _vmexit_control = 0;
	u32 _vmentry_control = 0;

	memset(vmcs_conf, 0, sizeof(*vmcs_conf));
	min = CPU_BASED_HLT_EXITING |
#ifdef CONFIG_X86_64
	      CPU_BASED_CR8_LOAD_EXITING |
	      CPU_BASED_CR8_STORE_EXITING |
#endif
	      CPU_BASED_CR3_LOAD_EXITING |
	      CPU_BASED_CR3_STORE_EXITING |
	      CPU_BASED_UNCOND_IO_EXITING |
	      CPU_BASED_MOV_DR_EXITING |
	      CPU_BASED_USE_TSC_OFFSETTING |
	      CPU_BASED_MWAIT_EXITING |
	      CPU_BASED_MONITOR_EXITING |
	      CPU_BASED_INVLPG_EXITING |
	      CPU_BASED_RDPMC_EXITING;

	opt = CPU_BASED_TPR_SHADOW |
	      CPU_BASED_USE_MSR_BITMAPS |
	      CPU_BASED_ACTIVATE_SECONDARY_CONTROLS;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_PROCBASED_CTLS,
				&_cpu_based_exec_control) < 0)
		return -EIO;
#ifdef CONFIG_X86_64
	if ((_cpu_based_exec_control & CPU_BASED_TPR_SHADOW))
		_cpu_based_exec_control &= ~CPU_BASED_CR8_LOAD_EXITING &
					   ~CPU_BASED_CR8_STORE_EXITING;
#endif
	if (_cpu_based_exec_control & CPU_BASED_ACTIVATE_SECONDARY_CONTROLS) {
		min2 = 0;
		opt2 = SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
			SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
			SECONDARY_EXEC_WBINVD_EXITING |
			SECONDARY_EXEC_ENABLE_VPID |
			SECONDARY_EXEC_ENABLE_EPT |
			SECONDARY_EXEC_UNRESTRICTED_GUEST |
			SECONDARY_EXEC_PAUSE_LOOP_EXITING |
			SECONDARY_EXEC_DESC |
			SECONDARY_EXEC_RDTSCP |
			SECONDARY_EXEC_ENABLE_INVPCID |
			SECONDARY_EXEC_APIC_REGISTER_VIRT |
			SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
			SECONDARY_EXEC_SHADOW_VMCS |
			SECONDARY_EXEC_XSAVES |
			SECONDARY_EXEC_RDSEED_EXITING |
			SECONDARY_EXEC_RDRAND_EXITING |
			SECONDARY_EXEC_ENABLE_PML |
			SECONDARY_EXEC_TSC_SCALING |
			SECONDARY_EXEC_ENABLE_USR_WAIT_PAUSE |
			SECONDARY_EXEC_PT_USE_GPA |
			SECONDARY_EXEC_PT_CONCEAL_VMX |
			SECONDARY_EXEC_ENABLE_VMFUNC |
			SECONDARY_EXEC_ENCLS_EXITING;
		if (adjust_vmx_controls(min2, opt2,
					MSR_IA32_VMX_PROCBASED_CTLS2,
					&_cpu_based_2nd_exec_control) < 0)
			return -EIO;
	}
#ifndef CONFIG_X86_64
	if (!(_cpu_based_2nd_exec_control &
				SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES))
		_cpu_based_exec_control &= ~CPU_BASED_TPR_SHADOW;
#endif

	if (!(_cpu_based_exec_control & CPU_BASED_TPR_SHADOW))
		_cpu_based_2nd_exec_control &= ~(
				SECONDARY_EXEC_APIC_REGISTER_VIRT |
				SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
				SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY);

	rdmsr_safe(MSR_IA32_VMX_EPT_VPID_CAP,
		&vmx_cap->ept, &vmx_cap->vpid);

	if (_cpu_based_2nd_exec_control & SECONDARY_EXEC_ENABLE_EPT) {
		/* CR3 accesses and invlpg don't need to cause VM Exits when EPT
		   enabled */
		_cpu_based_exec_control &= ~(CPU_BASED_CR3_LOAD_EXITING |
					     CPU_BASED_CR3_STORE_EXITING |
					     CPU_BASED_INVLPG_EXITING);
	} else if (vmx_cap->ept) {
		vmx_cap->ept = 0;
		pr_warn_once("EPT CAP should not exist if not support "
				"1-setting enable EPT VM-execution control\n");
	}
	if (!(_cpu_based_2nd_exec_control & SECONDARY_EXEC_ENABLE_VPID) &&
		vmx_cap->vpid) {
		vmx_cap->vpid = 0;
		pr_warn_once("VPID CAP should not exist if not support "
				"1-setting enable VPID VM-execution control\n");
	}

	min = VM_EXIT_SAVE_DEBUG_CONTROLS | VM_EXIT_ACK_INTR_ON_EXIT;
#ifdef CONFIG_X86_64
	min |= VM_EXIT_HOST_ADDR_SPACE_SIZE;
#endif
	opt = VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
	      VM_EXIT_LOAD_IA32_PAT |
	      VM_EXIT_LOAD_IA32_EFER |
	      VM_EXIT_CLEAR_BNDCFGS |
	      VM_EXIT_PT_CONCEAL_PIP |
	      VM_EXIT_CLEAR_IA32_RTIT_CTL;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_EXIT_CTLS,
				&_vmexit_control) < 0)
		return -EIO;

	min = PIN_BASED_EXT_INTR_MASK | PIN_BASED_NMI_EXITING;
	opt = PIN_BASED_VIRTUAL_NMIS | PIN_BASED_POSTED_INTR |
		 PIN_BASED_VMX_PREEMPTION_TIMER;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_PINBASED_CTLS,
				&_pin_based_exec_control) < 0)
		return -EIO;

	if (cpu_has_broken_vmx_preemption_timer())
		_pin_based_exec_control &= ~PIN_BASED_VMX_PREEMPTION_TIMER;
	if (!(_cpu_based_2nd_exec_control &
		SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY))
		_pin_based_exec_control &= ~PIN_BASED_POSTED_INTR;

	min = VM_ENTRY_LOAD_DEBUG_CONTROLS;
	opt = VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL |
	      VM_ENTRY_LOAD_IA32_PAT |
	      VM_ENTRY_LOAD_IA32_EFER |
	      VM_ENTRY_LOAD_BNDCFGS |
	      VM_ENTRY_PT_CONCEAL_PIP |
	      VM_ENTRY_LOAD_IA32_RTIT_CTL;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_ENTRY_CTLS,
				&_vmentry_control) < 0)
		return -EIO;

	/*
	 * Some cpus support VM_{ENTRY,EXIT}_IA32_PERF_GLOBAL_CTRL but they
	 * can't be used due to an errata where VM Exit may incorrectly clear
	 * IA32_PERF_GLOBAL_CTRL[34:32].  Workaround the errata by using the
	 * MSR load mechanism to switch IA32_PERF_GLOBAL_CTRL.
	 */
	if (boot_cpu_data.x86 == 0x6) {
		switch (boot_cpu_data.x86_model) {
		case 26: /* AAK155 */
		case 30: /* AAP115 */
		case 37: /* AAT100 */
		case 44: /* BC86,AAY89,BD102 */
		case 46: /* BA97 */
			_vmentry_control &= ~VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL;
			_vmexit_control &= ~VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL;
			pr_warn_once("kvm: VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL "
					"does not work properly. Using workaround\n");
			break;
		default:
			break;
		}
	}


	rdmsr(MSR_IA32_VMX_BASIC, vmx_msr_low, vmx_msr_high);

	/* IA-32 SDM Vol 3B: VMCS size is never greater than 4kB. */
	if ((vmx_msr_high & 0x1fff) > PAGE_SIZE)
		return -EIO;

#ifdef CONFIG_X86_64
	/* IA-32 SDM Vol 3B: 64-bit CPUs always have VMX_BASIC_MSR[48]==0. */
	if (vmx_msr_high & (1u<<16))
		return -EIO;
#endif

	/* Require Write-Back (WB) memory type for VMCS accesses. */
	if (((vmx_msr_high >> 18) & 15) != 6)
		return -EIO;

	vmcs_conf->size = vmx_msr_high & 0x1fff;
	vmcs_conf->order = get_order(vmcs_conf->size);
	vmcs_conf->basic_cap = vmx_msr_high & ~0x1fff;

	vmcs_conf->revision_id = vmx_msr_low;

	vmcs_conf->pin_based_exec_ctrl = _pin_based_exec_control;
	vmcs_conf->cpu_based_exec_ctrl = _cpu_based_exec_control;
	vmcs_conf->cpu_based_2nd_exec_ctrl = _cpu_based_2nd_exec_control;
	vmcs_conf->vmexit_ctrl         = _vmexit_control;
	vmcs_conf->vmentry_ctrl        = _vmentry_control;

	if (static_branch_unlikely(&enable_evmcs))
		evmcs_sanitize_exec_ctrls(vmcs_conf);

	return 0;
}

struct vmcs *alloc_vmcs_cpu(bool shadow, int cpu, gfp_t flags)
{
	int node = cpu_to_node(cpu);
	struct page *pages;
	struct vmcs *vmcs;

	pages = __alloc_pages_node(node, flags, vmcs_config.order);
	if (!pages)
		return NULL;
	vmcs = page_address(pages);
	memset(vmcs, 0, vmcs_config.size);

	/* KVM supports Enlightened VMCS v1 only */
	if (static_branch_unlikely(&enable_evmcs))
		vmcs->hdr.revision_id = KVM_EVMCS_VERSION;
	else
		vmcs->hdr.revision_id = vmcs_config.revision_id;

	if (shadow)
		vmcs->hdr.shadow_vmcs = 1;
	return vmcs;
}

void free_vmcs(struct vmcs *vmcs)
{
	free_pages((unsigned long)vmcs, vmcs_config.order);
}

/*
 * Free a VMCS, but before that VMCLEAR it on the CPU where it was last loaded
 */
void free_loaded_vmcs(struct loaded_vmcs *loaded_vmcs)
{
	if (!loaded_vmcs->vmcs)
		return;
	loaded_vmcs_clear(loaded_vmcs);
	free_vmcs(loaded_vmcs->vmcs);
	loaded_vmcs->vmcs = NULL;
	if (loaded_vmcs->msr_bitmap)
		free_page((unsigned long)loaded_vmcs->msr_bitmap);
	WARN_ON(loaded_vmcs->shadow_vmcs != NULL);
}

int alloc_loaded_vmcs(struct loaded_vmcs *loaded_vmcs)
{
	loaded_vmcs->vmcs = alloc_vmcs(false);
	if (!loaded_vmcs->vmcs)
		return -ENOMEM;

	loaded_vmcs->shadow_vmcs = NULL;
	loaded_vmcs->hv_timer_soft_disabled = false;
	loaded_vmcs_init(loaded_vmcs);

	if (cpu_has_vmx_msr_bitmap()) {
		loaded_vmcs->msr_bitmap = (unsigned long *)
				__get_free_page(GFP_KERNEL_ACCOUNT);
		if (!loaded_vmcs->msr_bitmap)
			goto out_vmcs;
		memset(loaded_vmcs->msr_bitmap, 0xff, PAGE_SIZE);

		if (IS_ENABLED(CONFIG_HYPERV) &&
		    static_branch_unlikely(&enable_evmcs) &&
		    (ms_hyperv.nested_features & HV_X64_NESTED_MSR_BITMAP)) {
			struct hv_enlightened_vmcs *evmcs =
				(struct hv_enlightened_vmcs *)loaded_vmcs->vmcs;

			evmcs->hv_enlightenments_control.msr_bitmap = 1;
		}
	}

	memset(&loaded_vmcs->host_state, 0, sizeof(struct vmcs_host_state));
	memset(&loaded_vmcs->controls_shadow, 0,
		sizeof(struct vmcs_controls_shadow));

	return 0;

out_vmcs:
	free_loaded_vmcs(loaded_vmcs);
	return -ENOMEM;
}

static void free_kvm_area(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		free_vmcs(per_cpu(vmxarea, cpu));
		per_cpu(vmxarea, cpu) = NULL;
	}
}

static __init int alloc_kvm_area(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct vmcs *vmcs;

		vmcs = alloc_vmcs_cpu(false, cpu, GFP_KERNEL);
		if (!vmcs) {
			free_kvm_area();
			return -ENOMEM;
		}

		/*
		 * When eVMCS is enabled, alloc_vmcs_cpu() sets
		 * vmcs->revision_id to KVM_EVMCS_VERSION instead of
		 * revision_id reported by MSR_IA32_VMX_BASIC.
		 *
		 * However, even though not explicitly documented by
		 * TLFS, VMXArea passed as VMXON argument should
		 * still be marked with revision_id reported by
		 * physical CPU.
		 */
		if (static_branch_unlikely(&enable_evmcs))
			vmcs->hdr.revision_id = vmcs_config.revision_id;

		per_cpu(vmxarea, cpu) = vmcs;
	}
	return 0;
}

static void fix_pmode_seg(struct kvm_vcpu *vcpu, int seg,
		struct kvm_segment *save)
{
	if (!emulate_invalid_guest_state) {
		/*
		 * CS and SS RPL should be equal during guest entry according
		 * to VMX spec, but in reality it is not always so. Since vcpu
		 * is in the middle of the transition from real mode to
		 * protected mode it is safe to assume that RPL 0 is a good
		 * default value.
		 */
		if (seg == VCPU_SREG_CS || seg == VCPU_SREG_SS)
			save->selector &= ~SEGMENT_RPL_MASK;
		save->dpl = save->selector & SEGMENT_RPL_MASK;
		save->s = 1;
	}
	vmx_set_segment(vcpu, save, seg);
}

static void enter_pmode(struct kvm_vcpu *vcpu)
{
	unsigned long flags;
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	/*
	 * Update real mode segment cache. It may be not up-to-date if sement
	 * register was written while vcpu was in a guest mode.
	 */
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_ES], VCPU_SREG_ES);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_DS], VCPU_SREG_DS);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_FS], VCPU_SREG_FS);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_GS], VCPU_SREG_GS);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_SS], VCPU_SREG_SS);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_CS], VCPU_SREG_CS);

	vmx->rmode.vm86_active = 0;

	vmx_set_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_TR], VCPU_SREG_TR);

	flags = vmcs_readl(GUEST_RFLAGS);
	flags &= RMODE_GUEST_OWNED_EFLAGS_BITS;
	flags |= vmx->rmode.save_rflags & ~RMODE_GUEST_OWNED_EFLAGS_BITS;
	vmcs_writel(GUEST_RFLAGS, flags);

	vmcs_writel(GUEST_CR4, (vmcs_readl(GUEST_CR4) & ~X86_CR4_VME) |
			(vmcs_readl(CR4_READ_SHADOW) & X86_CR4_VME));

	update_exception_bitmap(vcpu);

	fix_pmode_seg(vcpu, VCPU_SREG_CS, &vmx->rmode.segs[VCPU_SREG_CS]);
	fix_pmode_seg(vcpu, VCPU_SREG_SS, &vmx->rmode.segs[VCPU_SREG_SS]);
	fix_pmode_seg(vcpu, VCPU_SREG_ES, &vmx->rmode.segs[VCPU_SREG_ES]);
	fix_pmode_seg(vcpu, VCPU_SREG_DS, &vmx->rmode.segs[VCPU_SREG_DS]);
	fix_pmode_seg(vcpu, VCPU_SREG_FS, &vmx->rmode.segs[VCPU_SREG_FS]);
	fix_pmode_seg(vcpu, VCPU_SREG_GS, &vmx->rmode.segs[VCPU_SREG_GS]);
}

static void fix_rmode_seg(int seg, struct kvm_segment *save)
{
	const struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];
	struct kvm_segment var = *save;

	var.dpl = 0x3;
	if (seg == VCPU_SREG_CS)
		var.type = 0x3;

	if (!emulate_invalid_guest_state) {
		var.selector = var.base >> 4;
		var.base = var.base & 0xffff0;
		var.limit = 0xffff;
		var.g = 0;
		var.db = 0;
		var.present = 1;
		var.s = 1;
		var.l = 0;
		var.unusable = 0;
		var.type = 0x3;
		var.avl = 0;
		if (save->base & 0xf)
			printk_once(KERN_WARNING "kvm: segment base is not "
					"paragraph aligned when entering "
					"protected mode (seg=%d)", seg);
	}

	vmcs_write16(sf->selector, var.selector);
	vmcs_writel(sf->base, var.base);
	vmcs_write32(sf->limit, var.limit);
	vmcs_write32(sf->ar_bytes, vmx_segment_access_rights(&var));
}

static void enter_rmode(struct kvm_vcpu *vcpu)
{
	unsigned long flags;
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct kvm_vmx *kvm_vmx = to_kvm_vmx(vcpu->kvm);

	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_TR], VCPU_SREG_TR);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_ES], VCPU_SREG_ES);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_DS], VCPU_SREG_DS);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_FS], VCPU_SREG_FS);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_GS], VCPU_SREG_GS);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_SS], VCPU_SREG_SS);
	vmx_get_segment(vcpu, &vmx->rmode.segs[VCPU_SREG_CS], VCPU_SREG_CS);

	vmx->rmode.vm86_active = 1;

	/*
	 * Very old userspace does not call KVM_SET_TSS_ADDR before entering
	 * vcpu. Warn the user that an update is overdue.
	 */
	if (!kvm_vmx->tss_addr)
		printk_once(KERN_WARNING "kvm: KVM_SET_TSS_ADDR need to be "
			     "called before entering vcpu\n");

	vmx_segment_cache_clear(vmx);

	vmcs_writel(GUEST_TR_BASE, kvm_vmx->tss_addr);
	vmcs_write32(GUEST_TR_LIMIT, RMODE_TSS_SIZE - 1);
	vmcs_write32(GUEST_TR_AR_BYTES, 0x008b);

	flags = vmcs_readl(GUEST_RFLAGS);
	vmx->rmode.save_rflags = flags;

	flags |= X86_EFLAGS_IOPL | X86_EFLAGS_VM;

	vmcs_writel(GUEST_RFLAGS, flags);
	vmcs_writel(GUEST_CR4, vmcs_readl(GUEST_CR4) | X86_CR4_VME);
	update_exception_bitmap(vcpu);

	fix_rmode_seg(VCPU_SREG_SS, &vmx->rmode.segs[VCPU_SREG_SS]);
	fix_rmode_seg(VCPU_SREG_CS, &vmx->rmode.segs[VCPU_SREG_CS]);
	fix_rmode_seg(VCPU_SREG_ES, &vmx->rmode.segs[VCPU_SREG_ES]);
	fix_rmode_seg(VCPU_SREG_DS, &vmx->rmode.segs[VCPU_SREG_DS]);
	fix_rmode_seg(VCPU_SREG_GS, &vmx->rmode.segs[VCPU_SREG_GS]);
	fix_rmode_seg(VCPU_SREG_FS, &vmx->rmode.segs[VCPU_SREG_FS]);

	kvm_mmu_reset_context(vcpu);
}

void vmx_set_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct shared_msr_entry *msr = find_msr_entry(vmx, MSR_EFER);

	if (!msr)
		return;

	vcpu->arch.efer = efer;
	if (efer & EFER_LMA) {
		vm_entry_controls_setbit(to_vmx(vcpu), VM_ENTRY_IA32E_MODE);
		msr->data = efer;
	} else {
		vm_entry_controls_clearbit(to_vmx(vcpu), VM_ENTRY_IA32E_MODE);

		msr->data = efer & ~EFER_LME;
	}
	setup_msrs(vmx);
}

#ifdef CONFIG_X86_64

static void enter_lmode(struct kvm_vcpu *vcpu)
{
	u32 guest_tr_ar;

	vmx_segment_cache_clear(to_vmx(vcpu));

	guest_tr_ar = vmcs_read32(GUEST_TR_AR_BYTES);
	if ((guest_tr_ar & VMX_AR_TYPE_MASK) != VMX_AR_TYPE_BUSY_64_TSS) {
		pr_debug_ratelimited("%s: tss fixup for long mode. \n",
				     __func__);
		vmcs_write32(GUEST_TR_AR_BYTES,
			     (guest_tr_ar & ~VMX_AR_TYPE_MASK)
			     | VMX_AR_TYPE_BUSY_64_TSS);
	}
	vmx_set_efer(vcpu, vcpu->arch.efer | EFER_LMA);
}

static void exit_lmode(struct kvm_vcpu *vcpu)
{
	vm_entry_controls_clearbit(to_vmx(vcpu), VM_ENTRY_IA32E_MODE);
	vmx_set_efer(vcpu, vcpu->arch.efer & ~EFER_LMA);
}

#endif

static void vmx_flush_tlb_gva(struct kvm_vcpu *vcpu, gva_t addr)
{
	int vpid = to_vmx(vcpu)->vpid;

	if (!vpid_sync_vcpu_addr(vpid, addr))
		vpid_sync_context(vpid);

	/*
	 * If VPIDs are not supported or enabled, then the above is a no-op.
	 * But we don't really need a TLB flush in that case anyway, because
	 * each VM entry/exit includes an implicit flush when VPID is 0.
	 */
}

static void vmx_decache_cr0_guest_bits(struct kvm_vcpu *vcpu)
{
	ulong cr0_guest_owned_bits = vcpu->arch.cr0_guest_owned_bits;

	vcpu->arch.cr0 &= ~cr0_guest_owned_bits;
	vcpu->arch.cr0 |= vmcs_readl(GUEST_CR0) & cr0_guest_owned_bits;
}

static void vmx_decache_cr4_guest_bits(struct kvm_vcpu *vcpu)
{
	ulong cr4_guest_owned_bits = vcpu->arch.cr4_guest_owned_bits;

	vcpu->arch.cr4 &= ~cr4_guest_owned_bits;
	vcpu->arch.cr4 |= vmcs_readl(GUEST_CR4) & cr4_guest_owned_bits;
}

static void ept_load_pdptrs(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;

	if (!kvm_register_is_dirty(vcpu, VCPU_EXREG_PDPTR))
		return;

	if (is_pae_paging(vcpu)) {
		vmcs_write64(GUEST_PDPTR0, mmu->pdptrs[0]);
		vmcs_write64(GUEST_PDPTR1, mmu->pdptrs[1]);
		vmcs_write64(GUEST_PDPTR2, mmu->pdptrs[2]);
		vmcs_write64(GUEST_PDPTR3, mmu->pdptrs[3]);
	}
}

void ept_save_pdptrs(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *mmu = vcpu->arch.walk_mmu;

	if (is_pae_paging(vcpu)) {
		mmu->pdptrs[0] = vmcs_read64(GUEST_PDPTR0);
		mmu->pdptrs[1] = vmcs_read64(GUEST_PDPTR1);
		mmu->pdptrs[2] = vmcs_read64(GUEST_PDPTR2);
		mmu->pdptrs[3] = vmcs_read64(GUEST_PDPTR3);
	}

	kvm_register_mark_dirty(vcpu, VCPU_EXREG_PDPTR);
}

static void ept_update_paging_mode_cr0(unsigned long *hw_cr0,
					unsigned long cr0,
					struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (!kvm_register_is_available(vcpu, VCPU_EXREG_CR3))
		vmx_cache_reg(vcpu, VCPU_EXREG_CR3);
	if (!(cr0 & X86_CR0_PG)) {
		/* From paging/starting to nonpaging */
		exec_controls_setbit(vmx, CPU_BASED_CR3_LOAD_EXITING |
					  CPU_BASED_CR3_STORE_EXITING);
		vcpu->arch.cr0 = cr0;
		vmx_set_cr4(vcpu, kvm_read_cr4(vcpu));
	} else if (!is_paging(vcpu)) {
		/* From nonpaging to paging */
		exec_controls_clearbit(vmx, CPU_BASED_CR3_LOAD_EXITING |
					    CPU_BASED_CR3_STORE_EXITING);
		vcpu->arch.cr0 = cr0;
		vmx_set_cr4(vcpu, kvm_read_cr4(vcpu));
	}

	if (!(cr0 & X86_CR0_WP))
		*hw_cr0 &= ~X86_CR0_WP;
}

void vmx_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long hw_cr0;

	hw_cr0 = (cr0 & ~KVM_VM_CR0_ALWAYS_OFF);
	if (enable_unrestricted_guest)
		hw_cr0 |= KVM_VM_CR0_ALWAYS_ON_UNRESTRICTED_GUEST;
	else {
		hw_cr0 |= KVM_VM_CR0_ALWAYS_ON;

		if (vmx->rmode.vm86_active && (cr0 & X86_CR0_PE))
			enter_pmode(vcpu);

		if (!vmx->rmode.vm86_active && !(cr0 & X86_CR0_PE))
			enter_rmode(vcpu);
	}

#ifdef CONFIG_X86_64
	if (vcpu->arch.efer & EFER_LME) {
		if (!is_paging(vcpu) && (cr0 & X86_CR0_PG))
			enter_lmode(vcpu);
		if (is_paging(vcpu) && !(cr0 & X86_CR0_PG))
			exit_lmode(vcpu);
	}
#endif

	if (enable_ept && !enable_unrestricted_guest)
		ept_update_paging_mode_cr0(&hw_cr0, cr0, vcpu);

	vmcs_writel(CR0_READ_SHADOW, cr0);
	vmcs_writel(GUEST_CR0, hw_cr0);
	vcpu->arch.cr0 = cr0;

	/* depends on vcpu->arch.cr0 to be set to a new value */
	vmx->emulation_required = emulation_required(vcpu);
}

static int get_ept_level(struct kvm_vcpu *vcpu)
{
	/* Nested EPT currently only supports 4-level walks. */
	if (is_guest_mode(vcpu) && nested_cpu_has_ept(get_vmcs12(vcpu)))
		return 4;
	if (cpu_has_vmx_ept_5levels() && (cpuid_maxphyaddr(vcpu) > 48))
		return 5;
	return 4;
}

u64 construct_eptp(struct kvm_vcpu *vcpu, unsigned long root_hpa)
{
	u64 eptp = VMX_EPTP_MT_WB;

	eptp |= (get_ept_level(vcpu) == 5) ? VMX_EPTP_PWL_5 : VMX_EPTP_PWL_4;

	if (enable_ept_ad_bits &&
	    (!is_guest_mode(vcpu) || nested_ept_ad_enabled(vcpu)))
		eptp |= VMX_EPTP_AD_ENABLE_BIT;
	eptp |= (root_hpa & PAGE_MASK);

	return eptp;
}

void vmx_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	struct kvm *kvm = vcpu->kvm;
	bool update_guest_cr3 = true;
	unsigned long guest_cr3;
	u64 eptp;

	guest_cr3 = cr3;
	if (enable_ept) {
		eptp = construct_eptp(vcpu, cr3);
		vmcs_write64(EPT_POINTER, eptp);

		if (kvm_x86_ops->tlb_remote_flush) {
			spin_lock(&to_kvm_vmx(kvm)->ept_pointer_lock);
			to_vmx(vcpu)->ept_pointer = eptp;
			to_kvm_vmx(kvm)->ept_pointers_match
				= EPT_POINTERS_CHECK;
			spin_unlock(&to_kvm_vmx(kvm)->ept_pointer_lock);
		}

		/* Loading vmcs02.GUEST_CR3 is handled by nested VM-Enter. */
		if (is_guest_mode(vcpu))
			update_guest_cr3 = false;
		else if (!enable_unrestricted_guest && !is_paging(vcpu))
			guest_cr3 = to_kvm_vmx(kvm)->ept_identity_map_addr;
		else if (test_bit(VCPU_EXREG_CR3, (ulong *)&vcpu->arch.regs_avail))
			guest_cr3 = vcpu->arch.cr3;
		else /* vmcs01.GUEST_CR3 is already up-to-date. */
			update_guest_cr3 = false;
		ept_load_pdptrs(vcpu);
	}

	if (update_guest_cr3)
		vmcs_writel(GUEST_CR3, guest_cr3);
}

int vmx_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	/*
	 * Pass through host's Machine Check Enable value to hw_cr4, which
	 * is in force while we are in guest mode.  Do not let guests control
	 * this bit, even if host CR4.MCE == 0.
	 */
	unsigned long hw_cr4;

	hw_cr4 = (cr4_read_shadow() & X86_CR4_MCE) | (cr4 & ~X86_CR4_MCE);
	if (enable_unrestricted_guest)
		hw_cr4 |= KVM_VM_CR4_ALWAYS_ON_UNRESTRICTED_GUEST;
	else if (vmx->rmode.vm86_active)
		hw_cr4 |= KVM_RMODE_VM_CR4_ALWAYS_ON;
	else
		hw_cr4 |= KVM_PMODE_VM_CR4_ALWAYS_ON;

	if (!boot_cpu_has(X86_FEATURE_UMIP) && vmx_umip_emulated()) {
		if (cr4 & X86_CR4_UMIP) {
			secondary_exec_controls_setbit(vmx, SECONDARY_EXEC_DESC);
			hw_cr4 &= ~X86_CR4_UMIP;
		} else if (!is_guest_mode(vcpu) ||
			!nested_cpu_has2(get_vmcs12(vcpu), SECONDARY_EXEC_DESC)) {
			secondary_exec_controls_clearbit(vmx, SECONDARY_EXEC_DESC);
		}
	}

	if (cr4 & X86_CR4_VMXE) {
		/*
		 * To use VMXON (and later other VMX instructions), a guest
		 * must first be able to turn on cr4.VMXE (see handle_vmon()).
		 * So basically the check on whether to allow nested VMX
		 * is here.  We operate under the default treatment of SMM,
		 * so VMX cannot be enabled under SMM.
		 */
		if (!nested_vmx_allowed(vcpu) || is_smm(vcpu))
			return 1;
	}

	if (vmx->nested.vmxon && !nested_cr4_valid(vcpu, cr4))
		return 1;

	vcpu->arch.cr4 = cr4;

	if (!enable_unrestricted_guest) {
		if (enable_ept) {
			if (!is_paging(vcpu)) {
				hw_cr4 &= ~X86_CR4_PAE;
				hw_cr4 |= X86_CR4_PSE;
			} else if (!(cr4 & X86_CR4_PAE)) {
				hw_cr4 &= ~X86_CR4_PAE;
			}
		}

		/*
		 * SMEP/SMAP/PKU is disabled if CPU is in non-paging mode in
		 * hardware.  To emulate this behavior, SMEP/SMAP/PKU needs
		 * to be manually disabled when guest switches to non-paging
		 * mode.
		 *
		 * If !enable_unrestricted_guest, the CPU is always running
		 * with CR0.PG=1 and CR4 needs to be modified.
		 * If enable_unrestricted_guest, the CPU automatically
		 * disables SMEP/SMAP/PKU when the guest sets CR0.PG=0.
		 */
		if (!is_paging(vcpu))
			hw_cr4 &= ~(X86_CR4_SMEP | X86_CR4_SMAP | X86_CR4_PKE);
	}

	vmcs_writel(CR4_READ_SHADOW, cr4);
	vmcs_writel(GUEST_CR4, hw_cr4);
	return 0;
}

void vmx_get_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u32 ar;

	if (vmx->rmode.vm86_active && seg != VCPU_SREG_LDTR) {
		*var = vmx->rmode.segs[seg];
		if (seg == VCPU_SREG_TR
		    || var->selector == vmx_read_guest_seg_selector(vmx, seg))
			return;
		var->base = vmx_read_guest_seg_base(vmx, seg);
		var->selector = vmx_read_guest_seg_selector(vmx, seg);
		return;
	}
	var->base = vmx_read_guest_seg_base(vmx, seg);
	var->limit = vmx_read_guest_seg_limit(vmx, seg);
	var->selector = vmx_read_guest_seg_selector(vmx, seg);
	ar = vmx_read_guest_seg_ar(vmx, seg);
	var->unusable = (ar >> 16) & 1;
	var->type = ar & 15;
	var->s = (ar >> 4) & 1;
	var->dpl = (ar >> 5) & 3;
	/*
	 * Some userspaces do not preserve unusable property. Since usable
	 * segment has to be present according to VMX spec we can use present
	 * property to amend userspace bug by making unusable segment always
	 * nonpresent. vmx_segment_access_rights() already marks nonpresent
	 * segment as unusable.
	 */
	var->present = !var->unusable;
	var->avl = (ar >> 12) & 1;
	var->l = (ar >> 13) & 1;
	var->db = (ar >> 14) & 1;
	var->g = (ar >> 15) & 1;
}

static u64 vmx_get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	struct kvm_segment s;

	if (to_vmx(vcpu)->rmode.vm86_active) {
		vmx_get_segment(vcpu, &s, seg);
		return s.base;
	}
	return vmx_read_guest_seg_base(to_vmx(vcpu), seg);
}

int vmx_get_cpl(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (unlikely(vmx->rmode.vm86_active))
		return 0;
	else {
		int ar = vmx_read_guest_seg_ar(vmx, VCPU_SREG_SS);
		return VMX_AR_DPL(ar);
	}
}

static u32 vmx_segment_access_rights(struct kvm_segment *var)
{
	u32 ar;

	if (var->unusable || !var->present)
		ar = 1 << 16;
	else {
		ar = var->type & 15;
		ar |= (var->s & 1) << 4;
		ar |= (var->dpl & 3) << 5;
		ar |= (var->present & 1) << 7;
		ar |= (var->avl & 1) << 12;
		ar |= (var->l & 1) << 13;
		ar |= (var->db & 1) << 14;
		ar |= (var->g & 1) << 15;
	}

	return ar;
}

void vmx_set_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	const struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];

	vmx_segment_cache_clear(vmx);

	if (vmx->rmode.vm86_active && seg != VCPU_SREG_LDTR) {
		vmx->rmode.segs[seg] = *var;
		if (seg == VCPU_SREG_TR)
			vmcs_write16(sf->selector, var->selector);
		else if (var->s)
			fix_rmode_seg(seg, &vmx->rmode.segs[seg]);
		goto out;
	}

	vmcs_writel(sf->base, var->base);
	vmcs_write32(sf->limit, var->limit);
	vmcs_write16(sf->selector, var->selector);

	/*
	 *   Fix the "Accessed" bit in AR field of segment registers for older
	 * qemu binaries.
	 *   IA32 arch specifies that at the time of processor reset the
	 * "Accessed" bit in the AR field of segment registers is 1. And qemu
	 * is setting it to 0 in the userland code. This causes invalid guest
	 * state vmexit when "unrestricted guest" mode is turned on.
	 *    Fix for this setup issue in cpu_reset is being pushed in the qemu
	 * tree. Newer qemu binaries with that qemu fix would not need this
	 * kvm hack.
	 */
	if (enable_unrestricted_guest && (seg != VCPU_SREG_LDTR))
		var->type |= 0x1; /* Accessed */

	vmcs_write32(sf->ar_bytes, vmx_segment_access_rights(var));

out:
	vmx->emulation_required = emulation_required(vcpu);
}

static void vmx_get_cs_db_l_bits(struct kvm_vcpu *vcpu, int *db, int *l)
{
	u32 ar = vmx_read_guest_seg_ar(to_vmx(vcpu), VCPU_SREG_CS);

	*db = (ar >> 14) & 1;
	*l = (ar >> 13) & 1;
}

static void vmx_get_idt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	dt->size = vmcs_read32(GUEST_IDTR_LIMIT);
	dt->address = vmcs_readl(GUEST_IDTR_BASE);
}

static void vmx_set_idt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	vmcs_write32(GUEST_IDTR_LIMIT, dt->size);
	vmcs_writel(GUEST_IDTR_BASE, dt->address);
}

static void vmx_get_gdt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	dt->size = vmcs_read32(GUEST_GDTR_LIMIT);
	dt->address = vmcs_readl(GUEST_GDTR_BASE);
}

static void vmx_set_gdt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	vmcs_write32(GUEST_GDTR_LIMIT, dt->size);
	vmcs_writel(GUEST_GDTR_BASE, dt->address);
}

static bool rmode_segment_valid(struct kvm_vcpu *vcpu, int seg)
{
	struct kvm_segment var;
	u32 ar;

	vmx_get_segment(vcpu, &var, seg);
	var.dpl = 0x3;
	if (seg == VCPU_SREG_CS)
		var.type = 0x3;
	ar = vmx_segment_access_rights(&var);

	if (var.base != (var.selector << 4))
		return false;
	if (var.limit != 0xffff)
		return false;
	if (ar != 0xf3)
		return false;

	return true;
}

static bool code_segment_valid(struct kvm_vcpu *vcpu)
{
	struct kvm_segment cs;
	unsigned int cs_rpl;

	vmx_get_segment(vcpu, &cs, VCPU_SREG_CS);
	cs_rpl = cs.selector & SEGMENT_RPL_MASK;

	if (cs.unusable)
		return false;
	if (~cs.type & (VMX_AR_TYPE_CODE_MASK|VMX_AR_TYPE_ACCESSES_MASK))
		return false;
	if (!cs.s)
		return false;
	if (cs.type & VMX_AR_TYPE_WRITEABLE_MASK) {
		if (cs.dpl > cs_rpl)
			return false;
	} else {
		if (cs.dpl != cs_rpl)
			return false;
	}
	if (!cs.present)
		return false;

	/* TODO: Add Reserved field check, this'll require a new member in the kvm_segment_field structure */
	return true;
}

static bool stack_segment_valid(struct kvm_vcpu *vcpu)
{
	struct kvm_segment ss;
	unsigned int ss_rpl;

	vmx_get_segment(vcpu, &ss, VCPU_SREG_SS);
	ss_rpl = ss.selector & SEGMENT_RPL_MASK;

	if (ss.unusable)
		return true;
	if (ss.type != 3 && ss.type != 7)
		return false;
	if (!ss.s)
		return false;
	if (ss.dpl != ss_rpl) /* DPL != RPL */
		return false;
	if (!ss.present)
		return false;

	return true;
}

static bool data_segment_valid(struct kvm_vcpu *vcpu, int seg)
{
	struct kvm_segment var;
	unsigned int rpl;

	vmx_get_segment(vcpu, &var, seg);
	rpl = var.selector & SEGMENT_RPL_MASK;

	if (var.unusable)
		return true;
	if (!var.s)
		return false;
	if (!var.present)
		return false;
	if (~var.type & (VMX_AR_TYPE_CODE_MASK|VMX_AR_TYPE_WRITEABLE_MASK)) {
		if (var.dpl < rpl) /* DPL < RPL */
			return false;
	}

	/* TODO: Add other members to kvm_segment_field to allow checking for other access
	 * rights flags
	 */
	return true;
}

static bool tr_valid(struct kvm_vcpu *vcpu)
{
	struct kvm_segment tr;

	vmx_get_segment(vcpu, &tr, VCPU_SREG_TR);

	if (tr.unusable)
		return false;
	if (tr.selector & SEGMENT_TI_MASK)	/* TI = 1 */
		return false;
	if (tr.type != 3 && tr.type != 11) /* TODO: Check if guest is in IA32e mode */
		return false;
	if (!tr.present)
		return false;

	return true;
}

static bool ldtr_valid(struct kvm_vcpu *vcpu)
{
	struct kvm_segment ldtr;

	vmx_get_segment(vcpu, &ldtr, VCPU_SREG_LDTR);

	if (ldtr.unusable)
		return true;
	if (ldtr.selector & SEGMENT_TI_MASK)	/* TI = 1 */
		return false;
	if (ldtr.type != 2)
		return false;
	if (!ldtr.present)
		return false;

	return true;
}

static bool cs_ss_rpl_check(struct kvm_vcpu *vcpu)
{
	struct kvm_segment cs, ss;

	vmx_get_segment(vcpu, &cs, VCPU_SREG_CS);
	vmx_get_segment(vcpu, &ss, VCPU_SREG_SS);

	return ((cs.selector & SEGMENT_RPL_MASK) ==
		 (ss.selector & SEGMENT_RPL_MASK));
}

/*
 * Check if guest state is valid. Returns true if valid, false if
 * not.
 * We assume that registers are always usable
 */
static bool guest_state_valid(struct kvm_vcpu *vcpu)
{
	if (enable_unrestricted_guest)
		return true;

	/* real mode guest state checks */
	if (!is_protmode(vcpu) || (vmx_get_rflags(vcpu) & X86_EFLAGS_VM)) {
		if (!rmode_segment_valid(vcpu, VCPU_SREG_CS))
			return false;
		if (!rmode_segment_valid(vcpu, VCPU_SREG_SS))
			return false;
		if (!rmode_segment_valid(vcpu, VCPU_SREG_DS))
			return false;
		if (!rmode_segment_valid(vcpu, VCPU_SREG_ES))
			return false;
		if (!rmode_segment_valid(vcpu, VCPU_SREG_FS))
			return false;
		if (!rmode_segment_valid(vcpu, VCPU_SREG_GS))
			return false;
	} else {
	/* protected mode guest state checks */
		if (!cs_ss_rpl_check(vcpu))
			return false;
		if (!code_segment_valid(vcpu))
			return false;
		if (!stack_segment_valid(vcpu))
			return false;
		if (!data_segment_valid(vcpu, VCPU_SREG_DS))
			return false;
		if (!data_segment_valid(vcpu, VCPU_SREG_ES))
			return false;
		if (!data_segment_valid(vcpu, VCPU_SREG_FS))
			return false;
		if (!data_segment_valid(vcpu, VCPU_SREG_GS))
			return false;
		if (!tr_valid(vcpu))
			return false;
		if (!ldtr_valid(vcpu))
			return false;
	}
	/* TODO:
	 * - Add checks on RIP
	 * - Add checks on RFLAGS
	 */

	return true;
}

static int init_rmode_tss(struct kvm *kvm)
{
	gfn_t fn;
	u16 data = 0;
	int idx, r;

	idx = srcu_read_lock(&kvm->srcu);
	fn = to_kvm_vmx(kvm)->tss_addr >> PAGE_SHIFT;
	r = kvm_clear_guest_page(kvm, fn, 0, PAGE_SIZE);
	if (r < 0)
		goto out;
	data = TSS_BASE_SIZE + TSS_REDIRECTION_SIZE;
	r = kvm_write_guest_page(kvm, fn++, &data,
			TSS_IOPB_BASE_OFFSET, sizeof(u16));
	if (r < 0)
		goto out;
	r = kvm_clear_guest_page(kvm, fn++, 0, PAGE_SIZE);
	if (r < 0)
		goto out;
	r = kvm_clear_guest_page(kvm, fn, 0, PAGE_SIZE);
	if (r < 0)
		goto out;
	data = ~0;
	r = kvm_write_guest_page(kvm, fn, &data,
				 RMODE_TSS_SIZE - 2 * PAGE_SIZE - 1,
				 sizeof(u8));
out:
	srcu_read_unlock(&kvm->srcu, idx);
	return r;
}

static int init_rmode_identity_map(struct kvm *kvm)
{
	struct kvm_vmx *kvm_vmx = to_kvm_vmx(kvm);
	int i, r = 0;
	kvm_pfn_t identity_map_pfn;
	u32 tmp;

	/* Protect kvm_vmx->ept_identity_pagetable_done. */
	mutex_lock(&kvm->slots_lock);

	if (likely(kvm_vmx->ept_identity_pagetable_done))
		goto out;

	if (!kvm_vmx->ept_identity_map_addr)
		kvm_vmx->ept_identity_map_addr = VMX_EPT_IDENTITY_PAGETABLE_ADDR;
	identity_map_pfn = kvm_vmx->ept_identity_map_addr >> PAGE_SHIFT;

	r = __x86_set_memory_region(kvm, IDENTITY_PAGETABLE_PRIVATE_MEMSLOT,
				    kvm_vmx->ept_identity_map_addr, PAGE_SIZE);
	if (r < 0)
		goto out;

	r = kvm_clear_guest_page(kvm, identity_map_pfn, 0, PAGE_SIZE);
	if (r < 0)
		goto out;
	/* Set up identity-mapping pagetable for EPT in real mode */
	for (i = 0; i < PT32_ENT_PER_PAGE; i++) {
		tmp = (i << 22) + (_PAGE_PRESENT | _PAGE_RW | _PAGE_USER |
			_PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_PSE);
		r = kvm_write_guest_page(kvm, identity_map_pfn,
				&tmp, i * sizeof(tmp), sizeof(tmp));
		if (r < 0)
			goto out;
	}
	kvm_vmx->ept_identity_pagetable_done = true;

out:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

static void seg_setup(int seg)
{
	const struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];
	unsigned int ar;

	vmcs_write16(sf->selector, 0);
	vmcs_writel(sf->base, 0);
	vmcs_write32(sf->limit, 0xffff);
	ar = 0x93;
	if (seg == VCPU_SREG_CS)
		ar |= 0x08; /* code segment */

	vmcs_write32(sf->ar_bytes, ar);
}

static int alloc_apic_access_page(struct kvm *kvm)
{
	struct page *page;
	int r = 0;

	mutex_lock(&kvm->slots_lock);
	if (kvm->arch.apic_access_page_done)
		goto out;
	r = __x86_set_memory_region(kvm, APIC_ACCESS_PAGE_PRIVATE_MEMSLOT,
				    APIC_DEFAULT_PHYS_BASE, PAGE_SIZE);
	if (r)
		goto out;

	page = gfn_to_page(kvm, APIC_DEFAULT_PHYS_BASE >> PAGE_SHIFT);
	if (is_error_page(page)) {
		r = -EFAULT;
		goto out;
	}

	/*
	 * Do not pin the page in memory, so that memory hot-unplug
	 * is able to migrate it.
	 */
	put_page(page);
	kvm->arch.apic_access_page_done = true;
out:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

int allocate_vpid(void)
{
	int vpid;

	if (!enable_vpid)
		return 0;
	spin_lock(&vmx_vpid_lock);
	vpid = find_first_zero_bit(vmx_vpid_bitmap, VMX_NR_VPIDS);
	if (vpid < VMX_NR_VPIDS)
		__set_bit(vpid, vmx_vpid_bitmap);
	else
		vpid = 0;
	spin_unlock(&vmx_vpid_lock);
	return vpid;
}

void free_vpid(int vpid)
{
	if (!enable_vpid || vpid == 0)
		return;
	spin_lock(&vmx_vpid_lock);
	__clear_bit(vpid, vmx_vpid_bitmap);
	spin_unlock(&vmx_vpid_lock);
}

static __always_inline void vmx_disable_intercept_for_msr(unsigned long *msr_bitmap,
							  u32 msr, int type)
{
	int f = sizeof(unsigned long);

	if (!cpu_has_vmx_msr_bitmap())
		return;

	if (static_branch_unlikely(&enable_evmcs))
		evmcs_touch_msr_bitmap();

	/*
	 * See Intel PRM Vol. 3, 20.6.9 (MSR-Bitmap Address). Early manuals
	 * have the write-low and read-high bitmap offsets the wrong way round.
	 * We can control MSRs 0x00000000-0x00001fff and 0xc0000000-0xc0001fff.
	 */
	if (msr <= 0x1fff) {
		if (type & MSR_TYPE_R)
			/* read-low */
			__clear_bit(msr, msr_bitmap + 0x000 / f);

		if (type & MSR_TYPE_W)
			/* write-low */
			__clear_bit(msr, msr_bitmap + 0x800 / f);

	} else if ((msr >= 0xc0000000) && (msr <= 0xc0001fff)) {
		msr &= 0x1fff;
		if (type & MSR_TYPE_R)
			/* read-high */
			__clear_bit(msr, msr_bitmap + 0x400 / f);

		if (type & MSR_TYPE_W)
			/* write-high */
			__clear_bit(msr, msr_bitmap + 0xc00 / f);

	}
}

static __always_inline void vmx_enable_intercept_for_msr(unsigned long *msr_bitmap,
							 u32 msr, int type)
{
	int f = sizeof(unsigned long);

	if (!cpu_has_vmx_msr_bitmap())
		return;

	if (static_branch_unlikely(&enable_evmcs))
		evmcs_touch_msr_bitmap();

	/*
	 * See Intel PRM Vol. 3, 20.6.9 (MSR-Bitmap Address). Early manuals
	 * have the write-low and read-high bitmap offsets the wrong way round.
	 * We can control MSRs 0x00000000-0x00001fff and 0xc0000000-0xc0001fff.
	 */
	if (msr <= 0x1fff) {
		if (type & MSR_TYPE_R)
			/* read-low */
			__set_bit(msr, msr_bitmap + 0x000 / f);

		if (type & MSR_TYPE_W)
			/* write-low */
			__set_bit(msr, msr_bitmap + 0x800 / f);

	} else if ((msr >= 0xc0000000) && (msr <= 0xc0001fff)) {
		msr &= 0x1fff;
		if (type & MSR_TYPE_R)
			/* read-high */
			__set_bit(msr, msr_bitmap + 0x400 / f);

		if (type & MSR_TYPE_W)
			/* write-high */
			__set_bit(msr, msr_bitmap + 0xc00 / f);

	}
}

static __always_inline void vmx_set_intercept_for_msr(unsigned long *msr_bitmap,
			     			      u32 msr, int type, bool value)
{
	if (value)
		vmx_enable_intercept_for_msr(msr_bitmap, msr, type);
	else
		vmx_disable_intercept_for_msr(msr_bitmap, msr, type);
}

static u8 vmx_msr_bitmap_mode(struct kvm_vcpu *vcpu)
{
	u8 mode = 0;

	if (cpu_has_secondary_exec_ctrls() &&
	    (secondary_exec_controls_get(to_vmx(vcpu)) &
	     SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE)) {
		mode |= MSR_BITMAP_MODE_X2APIC;
		if (enable_apicv && kvm_vcpu_apicv_active(vcpu))
			mode |= MSR_BITMAP_MODE_X2APIC_APICV;
	}

	return mode;
}

static void vmx_update_msr_bitmap_x2apic(unsigned long *msr_bitmap,
					 u8 mode)
{
	int msr;

	for (msr = 0x800; msr <= 0x8ff; msr += BITS_PER_LONG) {
		unsigned word = msr / BITS_PER_LONG;
		msr_bitmap[word] = (mode & MSR_BITMAP_MODE_X2APIC_APICV) ? 0 : ~0;
		msr_bitmap[word + (0x800 / sizeof(long))] = ~0;
	}

	if (mode & MSR_BITMAP_MODE_X2APIC) {
		/*
		 * TPR reads and writes can be virtualized even if virtual interrupt
		 * delivery is not in use.
		 */
		vmx_disable_intercept_for_msr(msr_bitmap, X2APIC_MSR(APIC_TASKPRI), MSR_TYPE_RW);
		if (mode & MSR_BITMAP_MODE_X2APIC_APICV) {
			vmx_enable_intercept_for_msr(msr_bitmap, X2APIC_MSR(APIC_TMCCT), MSR_TYPE_R);
			vmx_disable_intercept_for_msr(msr_bitmap, X2APIC_MSR(APIC_EOI), MSR_TYPE_W);
			vmx_disable_intercept_for_msr(msr_bitmap, X2APIC_MSR(APIC_SELF_IPI), MSR_TYPE_W);
		}
	}
}

void vmx_update_msr_bitmap(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long *msr_bitmap = vmx->vmcs01.msr_bitmap;
	u8 mode = vmx_msr_bitmap_mode(vcpu);
	u8 changed = mode ^ vmx->msr_bitmap_mode;

	if (!changed)
		return;

	if (changed & (MSR_BITMAP_MODE_X2APIC | MSR_BITMAP_MODE_X2APIC_APICV))
		vmx_update_msr_bitmap_x2apic(msr_bitmap, mode);

	vmx->msr_bitmap_mode = mode;
}

void pt_update_intercept_for_msr(struct vcpu_vmx *vmx)
{
	unsigned long *msr_bitmap = vmx->vmcs01.msr_bitmap;
	bool flag = !(vmx->pt_desc.guest.ctl & RTIT_CTL_TRACEEN);
	u32 i;

	vmx_set_intercept_for_msr(msr_bitmap, MSR_IA32_RTIT_STATUS,
							MSR_TYPE_RW, flag);
	vmx_set_intercept_for_msr(msr_bitmap, MSR_IA32_RTIT_OUTPUT_BASE,
							MSR_TYPE_RW, flag);
	vmx_set_intercept_for_msr(msr_bitmap, MSR_IA32_RTIT_OUTPUT_MASK,
							MSR_TYPE_RW, flag);
	vmx_set_intercept_for_msr(msr_bitmap, MSR_IA32_RTIT_CR3_MATCH,
							MSR_TYPE_RW, flag);
	for (i = 0; i < vmx->pt_desc.addr_range; i++) {
		vmx_set_intercept_for_msr(msr_bitmap,
			MSR_IA32_RTIT_ADDR0_A + i * 2, MSR_TYPE_RW, flag);
		vmx_set_intercept_for_msr(msr_bitmap,
			MSR_IA32_RTIT_ADDR0_B + i * 2, MSR_TYPE_RW, flag);
	}
}

static bool vmx_guest_apic_has_interrupt(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	void *vapic_page;
	u32 vppr;
	int rvi;

	if (WARN_ON_ONCE(!is_guest_mode(vcpu)) ||
		!nested_cpu_has_vid(get_vmcs12(vcpu)) ||
		WARN_ON_ONCE(!vmx->nested.virtual_apic_map.gfn))
		return false;

	rvi = vmx_get_rvi();

	vapic_page = vmx->nested.virtual_apic_map.hva;
	vppr = *((u32 *)(vapic_page + APIC_PROCPRI));

	return ((rvi & 0xf0) > (vppr & 0xf0));
}

static inline bool kvm_vcpu_trigger_posted_interrupt(struct kvm_vcpu *vcpu,
						     bool nested)
{
#ifdef CONFIG_SMP
	int pi_vec = nested ? POSTED_INTR_NESTED_VECTOR : POSTED_INTR_VECTOR;

	if (vcpu->mode == IN_GUEST_MODE) {
		/*
		 * The vector of interrupt to be delivered to vcpu had
		 * been set in PIR before this function.
		 *
		 * Following cases will be reached in this block, and
		 * we always send a notification event in all cases as
		 * explained below.
		 *
		 * Case 1: vcpu keeps in non-root mode. Sending a
		 * notification event posts the interrupt to vcpu.
		 *
		 * Case 2: vcpu exits to root mode and is still
		 * runnable. PIR will be synced to vIRR before the
		 * next vcpu entry. Sending a notification event in
		 * this case has no effect, as vcpu is not in root
		 * mode.
		 *
		 * Case 3: vcpu exits to root mode and is blocked.
		 * vcpu_block() has already synced PIR to vIRR and
		 * never blocks vcpu if vIRR is not cleared. Therefore,
		 * a blocked vcpu here does not wait for any requested
		 * interrupts in PIR, and sending a notification event
		 * which has no effect is safe here.
		 */

		apic->send_IPI_mask(get_cpu_mask(vcpu->cpu), pi_vec);
		return true;
	}
#endif
	return false;
}

static int vmx_deliver_nested_posted_interrupt(struct kvm_vcpu *vcpu,
						int vector)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (is_guest_mode(vcpu) &&
	    vector == vmx->nested.posted_intr_nv) {
		/*
		 * If a posted intr is not recognized by hardware,
		 * we will accomplish it in the next vmentry.
		 */
		vmx->nested.pi_pending = true;
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		/* the PIR and ON have been set by L1. */
		if (!kvm_vcpu_trigger_posted_interrupt(vcpu, true))
			kvm_vcpu_kick(vcpu);
		return 0;
	}
	return -1;
}
/*
 * Send interrupt to vcpu via posted interrupt way.
 * 1. If target vcpu is running(non-root mode), send posted interrupt
 * notification to vcpu and hardware will sync PIR to vIRR atomically.
 * 2. If target vcpu isn't running(root mode), kick it to pick up the
 * interrupt from PIR in next vmentry.
 */
static int vmx_deliver_posted_interrupt(struct kvm_vcpu *vcpu, int vector)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	int r;

	r = vmx_deliver_nested_posted_interrupt(vcpu, vector);
	if (!r)
		return 0;

	if (!vcpu->arch.apicv_active)
		return -1;

	if (pi_test_and_set_pir(vector, &vmx->pi_desc))
		return 0;

	/* If a previous notification has sent the IPI, nothing to do.  */
	if (pi_test_and_set_on(&vmx->pi_desc))
		return 0;

	if (!kvm_vcpu_trigger_posted_interrupt(vcpu, false))
		kvm_vcpu_kick(vcpu);

	return 0;
}

/*
 * Set up the vmcs's constant host-state fields, i.e., host-state fields that
 * will not change in the lifetime of the guest.
 * Note that host-state that does change is set elsewhere. E.g., host-state
 * that is set differently for each CPU is set in vmx_vcpu_load(), not here.
 */
void vmx_set_constant_host_state(struct vcpu_vmx *vmx)
{
	u32 low32, high32;
	unsigned long tmpl;
	unsigned long cr0, cr3, cr4;

	cr0 = read_cr0();
	WARN_ON(cr0 & X86_CR0_TS);
	vmcs_writel(HOST_CR0, cr0);  /* 22.2.3 */

	/*
	 * Save the most likely value for this task's CR3 in the VMCS.
	 * We can't use __get_current_cr3_fast() because we're not atomic.
	 */
	cr3 = __read_cr3();
	vmcs_writel(HOST_CR3, cr3);		/* 22.2.3  FIXME: shadow tables */
	vmx->loaded_vmcs->host_state.cr3 = cr3;

	/* Save the most likely value for this task's CR4 in the VMCS. */
	cr4 = cr4_read_shadow();
	vmcs_writel(HOST_CR4, cr4);			/* 22.2.3, 22.2.5 */
	vmx->loaded_vmcs->host_state.cr4 = cr4;

	vmcs_write16(HOST_CS_SELECTOR, __KERNEL_CS);  /* 22.2.4 */
#ifdef CONFIG_X86_64
	/*
	 * Load null selectors, so we can avoid reloading them in
	 * vmx_prepare_switch_to_host(), in case userspace uses
	 * the null selectors too (the expected case).
	 */
	vmcs_write16(HOST_DS_SELECTOR, 0);
	vmcs_write16(HOST_ES_SELECTOR, 0);
#else
	vmcs_write16(HOST_DS_SELECTOR, __KERNEL_DS);  /* 22.2.4 */
	vmcs_write16(HOST_ES_SELECTOR, __KERNEL_DS);  /* 22.2.4 */
#endif
	vmcs_write16(HOST_SS_SELECTOR, __KERNEL_DS);  /* 22.2.4 */
	vmcs_write16(HOST_TR_SELECTOR, GDT_ENTRY_TSS*8);  /* 22.2.4 */

	vmcs_writel(HOST_IDTR_BASE, host_idt_base);   /* 22.2.4 */

	vmcs_writel(HOST_RIP, (unsigned long)vmx_vmexit); /* 22.2.5 */

	rdmsr(MSR_IA32_SYSENTER_CS, low32, high32);
	vmcs_write32(HOST_IA32_SYSENTER_CS, low32);
	rdmsrl(MSR_IA32_SYSENTER_EIP, tmpl);
	vmcs_writel(HOST_IA32_SYSENTER_EIP, tmpl);   /* 22.2.3 */

	if (vmcs_config.vmexit_ctrl & VM_EXIT_LOAD_IA32_PAT) {
		rdmsr(MSR_IA32_CR_PAT, low32, high32);
		vmcs_write64(HOST_IA32_PAT, low32 | ((u64) high32 << 32));
	}

	if (cpu_has_load_ia32_efer())
		vmcs_write64(HOST_IA32_EFER, host_efer);
}

void set_cr4_guest_host_mask(struct vcpu_vmx *vmx)
{
	vmx->vcpu.arch.cr4_guest_owned_bits = KVM_CR4_GUEST_OWNED_BITS;
	if (enable_ept)
		vmx->vcpu.arch.cr4_guest_owned_bits |= X86_CR4_PGE;
	if (is_guest_mode(&vmx->vcpu))
		vmx->vcpu.arch.cr4_guest_owned_bits &=
			~get_vmcs12(&vmx->vcpu)->cr4_guest_host_mask;
	vmcs_writel(CR4_GUEST_HOST_MASK, ~vmx->vcpu.arch.cr4_guest_owned_bits);
}

u32 vmx_pin_based_exec_ctrl(struct vcpu_vmx *vmx)
{
	u32 pin_based_exec_ctrl = vmcs_config.pin_based_exec_ctrl;

	if (!kvm_vcpu_apicv_active(&vmx->vcpu))
		pin_based_exec_ctrl &= ~PIN_BASED_POSTED_INTR;

	if (!enable_vnmi)
		pin_based_exec_ctrl &= ~PIN_BASED_VIRTUAL_NMIS;

	if (!enable_preemption_timer)
		pin_based_exec_ctrl &= ~PIN_BASED_VMX_PREEMPTION_TIMER;

	return pin_based_exec_ctrl;
}

static void vmx_refresh_apicv_exec_ctrl(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	pin_controls_set(vmx, vmx_pin_based_exec_ctrl(vmx));
	if (cpu_has_secondary_exec_ctrls()) {
		if (kvm_vcpu_apicv_active(vcpu))
			secondary_exec_controls_setbit(vmx,
				      SECONDARY_EXEC_APIC_REGISTER_VIRT |
				      SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY);
		else
			secondary_exec_controls_clearbit(vmx,
					SECONDARY_EXEC_APIC_REGISTER_VIRT |
					SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY);
	}

	if (cpu_has_vmx_msr_bitmap())
		vmx_update_msr_bitmap(vcpu);
}

u32 vmx_exec_control(struct vcpu_vmx *vmx)
{
	u32 exec_control = vmcs_config.cpu_based_exec_ctrl;

	if (vmx->vcpu.arch.switch_db_regs & KVM_DEBUGREG_WONT_EXIT)
		exec_control &= ~CPU_BASED_MOV_DR_EXITING;

	if (!cpu_need_tpr_shadow(&vmx->vcpu)) {
		exec_control &= ~CPU_BASED_TPR_SHADOW;
#ifdef CONFIG_X86_64
		exec_control |= CPU_BASED_CR8_STORE_EXITING |
				CPU_BASED_CR8_LOAD_EXITING;
#endif
	}
	if (!enable_ept)
		exec_control |= CPU_BASED_CR3_STORE_EXITING |
				CPU_BASED_CR3_LOAD_EXITING  |
				CPU_BASED_INVLPG_EXITING;
	if (kvm_mwait_in_guest(vmx->vcpu.kvm))
		exec_control &= ~(CPU_BASED_MWAIT_EXITING |
				CPU_BASED_MONITOR_EXITING);
	if (kvm_hlt_in_guest(vmx->vcpu.kvm))
		exec_control &= ~CPU_BASED_HLT_EXITING;
	return exec_control;
}


static void vmx_compute_secondary_exec_control(struct vcpu_vmx *vmx)
{
	struct kvm_vcpu *vcpu = &vmx->vcpu;

	u32 exec_control = vmcs_config.cpu_based_2nd_exec_ctrl;

	if (pt_mode == PT_MODE_SYSTEM)
		exec_control &= ~(SECONDARY_EXEC_PT_USE_GPA | SECONDARY_EXEC_PT_CONCEAL_VMX);
	if (!cpu_need_virtualize_apic_accesses(vcpu))
		exec_control &= ~SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES;
	if (vmx->vpid == 0)
		exec_control &= ~SECONDARY_EXEC_ENABLE_VPID;
	if (!enable_ept) {
		exec_control &= ~SECONDARY_EXEC_ENABLE_EPT;
		enable_unrestricted_guest = 0;
	}
	if (!enable_unrestricted_guest)
		exec_control &= ~SECONDARY_EXEC_UNRESTRICTED_GUEST;
	if (kvm_pause_in_guest(vmx->vcpu.kvm))
		exec_control &= ~SECONDARY_EXEC_PAUSE_LOOP_EXITING;
	if (!kvm_vcpu_apicv_active(vcpu))
		exec_control &= ~(SECONDARY_EXEC_APIC_REGISTER_VIRT |
				  SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY);
	exec_control &= ~SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE;

	/* SECONDARY_EXEC_DESC is enabled/disabled on writes to CR4.UMIP,
	 * in vmx_set_cr4.  */
	exec_control &= ~SECONDARY_EXEC_DESC;

	/* SECONDARY_EXEC_SHADOW_VMCS is enabled when L1 executes VMPTRLD
	   (handle_vmptrld).
	   We can NOT enable shadow_vmcs here because we don't have yet
	   a current VMCS12
	*/
	exec_control &= ~SECONDARY_EXEC_SHADOW_VMCS;

	if (!enable_pml)
		exec_control &= ~SECONDARY_EXEC_ENABLE_PML;

	if (vmx_xsaves_supported()) {
		/* Exposing XSAVES only when XSAVE is exposed */
		bool xsaves_enabled =
			boot_cpu_has(X86_FEATURE_XSAVE) &&
			guest_cpuid_has(vcpu, X86_FEATURE_XSAVE) &&
			guest_cpuid_has(vcpu, X86_FEATURE_XSAVES);

		vcpu->arch.xsaves_enabled = xsaves_enabled;

		if (!xsaves_enabled)
			exec_control &= ~SECONDARY_EXEC_XSAVES;

		if (nested) {
			if (xsaves_enabled)
				vmx->nested.msrs.secondary_ctls_high |=
					SECONDARY_EXEC_XSAVES;
			else
				vmx->nested.msrs.secondary_ctls_high &=
					~SECONDARY_EXEC_XSAVES;
		}
	}

	if (vmx_rdtscp_supported()) {
		bool rdtscp_enabled = guest_cpuid_has(vcpu, X86_FEATURE_RDTSCP);
		if (!rdtscp_enabled)
			exec_control &= ~SECONDARY_EXEC_RDTSCP;

		if (nested) {
			if (rdtscp_enabled)
				vmx->nested.msrs.secondary_ctls_high |=
					SECONDARY_EXEC_RDTSCP;
			else
				vmx->nested.msrs.secondary_ctls_high &=
					~SECONDARY_EXEC_RDTSCP;
		}
	}

	if (vmx_invpcid_supported()) {
		/* Exposing INVPCID only when PCID is exposed */
		bool invpcid_enabled =
			guest_cpuid_has(vcpu, X86_FEATURE_INVPCID) &&
			guest_cpuid_has(vcpu, X86_FEATURE_PCID);

		if (!invpcid_enabled) {
			exec_control &= ~SECONDARY_EXEC_ENABLE_INVPCID;
			guest_cpuid_clear(vcpu, X86_FEATURE_INVPCID);
		}

		if (nested) {
			if (invpcid_enabled)
				vmx->nested.msrs.secondary_ctls_high |=
					SECONDARY_EXEC_ENABLE_INVPCID;
			else
				vmx->nested.msrs.secondary_ctls_high &=
					~SECONDARY_EXEC_ENABLE_INVPCID;
		}
	}

	if (vmx_rdrand_supported()) {
		bool rdrand_enabled = guest_cpuid_has(vcpu, X86_FEATURE_RDRAND);
		if (rdrand_enabled)
			exec_control &= ~SECONDARY_EXEC_RDRAND_EXITING;

		if (nested) {
			if (rdrand_enabled)
				vmx->nested.msrs.secondary_ctls_high |=
					SECONDARY_EXEC_RDRAND_EXITING;
			else
				vmx->nested.msrs.secondary_ctls_high &=
					~SECONDARY_EXEC_RDRAND_EXITING;
		}
	}

	if (vmx_rdseed_supported()) {
		bool rdseed_enabled = guest_cpuid_has(vcpu, X86_FEATURE_RDSEED);
		if (rdseed_enabled)
			exec_control &= ~SECONDARY_EXEC_RDSEED_EXITING;

		if (nested) {
			if (rdseed_enabled)
				vmx->nested.msrs.secondary_ctls_high |=
					SECONDARY_EXEC_RDSEED_EXITING;
			else
				vmx->nested.msrs.secondary_ctls_high &=
					~SECONDARY_EXEC_RDSEED_EXITING;
		}
	}

	if (vmx_waitpkg_supported()) {
		bool waitpkg_enabled =
			guest_cpuid_has(vcpu, X86_FEATURE_WAITPKG);

		if (!waitpkg_enabled)
			exec_control &= ~SECONDARY_EXEC_ENABLE_USR_WAIT_PAUSE;

		if (nested) {
			if (waitpkg_enabled)
				vmx->nested.msrs.secondary_ctls_high |=
					SECONDARY_EXEC_ENABLE_USR_WAIT_PAUSE;
			else
				vmx->nested.msrs.secondary_ctls_high &=
					~SECONDARY_EXEC_ENABLE_USR_WAIT_PAUSE;
		}
	}

	vmx->secondary_exec_control = exec_control;
}

static void ept_set_mmio_spte_mask(void)
{
	/*
	 * EPT Misconfigurations can be generated if the value of bits 2:0
	 * of an EPT paging-structure entry is 110b (write/execute).
	 */
	kvm_mmu_set_mmio_spte_mask(VMX_EPT_RWX_MASK,
				   VMX_EPT_MISCONFIG_WX_VALUE, 0);
}

#define VMX_XSS_EXIT_BITMAP 0

/*
 * Noting that the initialization of Guest-state Area of VMCS is in
 * vmx_vcpu_reset().
 */
static void init_vmcs(struct vcpu_vmx *vmx)
{
	if (nested)
		nested_vmx_set_vmcs_shadowing_bitmap();

	if (cpu_has_vmx_msr_bitmap())
		vmcs_write64(MSR_BITMAP, __pa(vmx->vmcs01.msr_bitmap));

	vmcs_write64(VMCS_LINK_POINTER, -1ull); /* 22.3.1.5 */

	/* Control */
	pin_controls_set(vmx, vmx_pin_based_exec_ctrl(vmx));

	exec_controls_set(vmx, vmx_exec_control(vmx));

	if (cpu_has_secondary_exec_ctrls()) {
		vmx_compute_secondary_exec_control(vmx);
		secondary_exec_controls_set(vmx, vmx->secondary_exec_control);
	}

	if (kvm_vcpu_apicv_active(&vmx->vcpu)) {
		vmcs_write64(EOI_EXIT_BITMAP0, 0);
		vmcs_write64(EOI_EXIT_BITMAP1, 0);
		vmcs_write64(EOI_EXIT_BITMAP2, 0);
		vmcs_write64(EOI_EXIT_BITMAP3, 0);

		vmcs_write16(GUEST_INTR_STATUS, 0);

		vmcs_write16(POSTED_INTR_NV, POSTED_INTR_VECTOR);
		vmcs_write64(POSTED_INTR_DESC_ADDR, __pa((&vmx->pi_desc)));
	}

	if (!kvm_pause_in_guest(vmx->vcpu.kvm)) {
		vmcs_write32(PLE_GAP, ple_gap);
		vmx->ple_window = ple_window;
		vmx->ple_window_dirty = true;
	}

	vmcs_write32(PAGE_FAULT_ERROR_CODE_MASK, 0);
	vmcs_write32(PAGE_FAULT_ERROR_CODE_MATCH, 0);
	vmcs_write32(CR3_TARGET_COUNT, 0);           /* 22.2.1 */

	vmcs_write16(HOST_FS_SELECTOR, 0);            /* 22.2.4 */
	vmcs_write16(HOST_GS_SELECTOR, 0);            /* 22.2.4 */
	vmx_set_constant_host_state(vmx);
	vmcs_writel(HOST_FS_BASE, 0); /* 22.2.4 */
	vmcs_writel(HOST_GS_BASE, 0); /* 22.2.4 */

	if (cpu_has_vmx_vmfunc())
		vmcs_write64(VM_FUNCTION_CONTROL, 0);

	vmcs_write32(VM_EXIT_MSR_STORE_COUNT, 0);
	vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, 0);
	vmcs_write64(VM_EXIT_MSR_LOAD_ADDR, __pa(vmx->msr_autoload.host.val));
	vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, 0);
	vmcs_write64(VM_ENTRY_MSR_LOAD_ADDR, __pa(vmx->msr_autoload.guest.val));

	if (vmcs_config.vmentry_ctrl & VM_ENTRY_LOAD_IA32_PAT)
		vmcs_write64(GUEST_IA32_PAT, vmx->vcpu.arch.pat);

	vm_exit_controls_set(vmx, vmx_vmexit_ctrl());

	/* 22.2.1, 20.8.1 */
	vm_entry_controls_set(vmx, vmx_vmentry_ctrl());

	vmx->vcpu.arch.cr0_guest_owned_bits = X86_CR0_TS;
	vmcs_writel(CR0_GUEST_HOST_MASK, ~X86_CR0_TS);

	set_cr4_guest_host_mask(vmx);

	if (vmx->vpid != 0)
		vmcs_write16(VIRTUAL_PROCESSOR_ID, vmx->vpid);

	if (vmx_xsaves_supported())
		vmcs_write64(XSS_EXIT_BITMAP, VMX_XSS_EXIT_BITMAP);

	if (enable_pml) {
		vmcs_write64(PML_ADDRESS, page_to_phys(vmx->pml_pg));
		vmcs_write16(GUEST_PML_INDEX, PML_ENTITY_NUM - 1);
	}

	if (cpu_has_vmx_encls_vmexit())
		vmcs_write64(ENCLS_EXITING_BITMAP, -1ull);

	if (pt_mode == PT_MODE_HOST_GUEST) {
		memset(&vmx->pt_desc, 0, sizeof(vmx->pt_desc));
		/* Bit[6~0] are forced to 1, writes are ignored. */
		vmx->pt_desc.guest.output_mask = 0x7F;
		vmcs_write64(GUEST_IA32_RTIT_CTL, 0);
	}
}

static void vmx_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct msr_data apic_base_msr;
	u64 cr0;

	vmx->rmode.vm86_active = 0;
	vmx->spec_ctrl = 0;

	vmx->msr_ia32_umwait_control = 0;

	vmx->vcpu.arch.regs[VCPU_REGS_RDX] = get_rdx_init_val();
	vmx->hv_deadline_tsc = -1;
	kvm_set_cr8(vcpu, 0);

	if (!init_event) {
		apic_base_msr.data = APIC_DEFAULT_PHYS_BASE |
				     MSR_IA32_APICBASE_ENABLE;
		if (kvm_vcpu_is_reset_bsp(vcpu))
			apic_base_msr.data |= MSR_IA32_APICBASE_BSP;
		apic_base_msr.host_initiated = true;
		kvm_set_apic_base(vcpu, &apic_base_msr);
	}

	vmx_segment_cache_clear(vmx);

	seg_setup(VCPU_SREG_CS);
	vmcs_write16(GUEST_CS_SELECTOR, 0xf000);
	vmcs_writel(GUEST_CS_BASE, 0xffff0000ul);

	seg_setup(VCPU_SREG_DS);
	seg_setup(VCPU_SREG_ES);
	seg_setup(VCPU_SREG_FS);
	seg_setup(VCPU_SREG_GS);
	seg_setup(VCPU_SREG_SS);

	vmcs_write16(GUEST_TR_SELECTOR, 0);
	vmcs_writel(GUEST_TR_BASE, 0);
	vmcs_write32(GUEST_TR_LIMIT, 0xffff);
	vmcs_write32(GUEST_TR_AR_BYTES, 0x008b);

	vmcs_write16(GUEST_LDTR_SELECTOR, 0);
	vmcs_writel(GUEST_LDTR_BASE, 0);
	vmcs_write32(GUEST_LDTR_LIMIT, 0xffff);
	vmcs_write32(GUEST_LDTR_AR_BYTES, 0x00082);

	if (!init_event) {
		vmcs_write32(GUEST_SYSENTER_CS, 0);
		vmcs_writel(GUEST_SYSENTER_ESP, 0);
		vmcs_writel(GUEST_SYSENTER_EIP, 0);
		vmcs_write64(GUEST_IA32_DEBUGCTL, 0);
	}

	kvm_set_rflags(vcpu, X86_EFLAGS_FIXED);
	kvm_rip_write(vcpu, 0xfff0);

	vmcs_writel(GUEST_GDTR_BASE, 0);
	vmcs_write32(GUEST_GDTR_LIMIT, 0xffff);

	vmcs_writel(GUEST_IDTR_BASE, 0);
	vmcs_write32(GUEST_IDTR_LIMIT, 0xffff);

	vmcs_write32(GUEST_ACTIVITY_STATE, GUEST_ACTIVITY_ACTIVE);
	vmcs_write32(GUEST_INTERRUPTIBILITY_INFO, 0);
	vmcs_writel(GUEST_PENDING_DBG_EXCEPTIONS, 0);
	if (kvm_mpx_supported())
		vmcs_write64(GUEST_BNDCFGS, 0);

	setup_msrs(vmx);

	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, 0);  /* 22.2.1 */

	if (cpu_has_vmx_tpr_shadow() && !init_event) {
		vmcs_write64(VIRTUAL_APIC_PAGE_ADDR, 0);
		if (cpu_need_tpr_shadow(vcpu))
			vmcs_write64(VIRTUAL_APIC_PAGE_ADDR,
				     __pa(vcpu->arch.apic->regs));
		vmcs_write32(TPR_THRESHOLD, 0);
	}

	kvm_make_request(KVM_REQ_APIC_PAGE_RELOAD, vcpu);

	cr0 = X86_CR0_NW | X86_CR0_CD | X86_CR0_ET;
	vmx->vcpu.arch.cr0 = cr0;
	vmx_set_cr0(vcpu, cr0); /* enter rmode */
	vmx_set_cr4(vcpu, 0);
	vmx_set_efer(vcpu, 0);

	update_exception_bitmap(vcpu);

	vpid_sync_context(vmx->vpid);
	if (init_event)
		vmx_clear_hlt(vcpu);
}

static void enable_irq_window(struct kvm_vcpu *vcpu)
{
	exec_controls_setbit(to_vmx(vcpu), CPU_BASED_INTR_WINDOW_EXITING);
}

static void enable_nmi_window(struct kvm_vcpu *vcpu)
{
	if (!enable_vnmi ||
	    vmcs_read32(GUEST_INTERRUPTIBILITY_INFO) & GUEST_INTR_STATE_STI) {
		enable_irq_window(vcpu);
		return;
	}

	exec_controls_setbit(to_vmx(vcpu), CPU_BASED_NMI_WINDOW_EXITING);
}

static void vmx_inject_irq(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	uint32_t intr;
	int irq = vcpu->arch.interrupt.nr;

	trace_kvm_inj_virq(irq);

	++vcpu->stat.irq_injections;
	if (vmx->rmode.vm86_active) {
		int inc_eip = 0;
		if (vcpu->arch.interrupt.soft)
			inc_eip = vcpu->arch.event_exit_inst_len;
		kvm_inject_realmode_interrupt(vcpu, irq, inc_eip);
		return;
	}
	intr = irq | INTR_INFO_VALID_MASK;
	if (vcpu->arch.interrupt.soft) {
		intr |= INTR_TYPE_SOFT_INTR;
		vmcs_write32(VM_ENTRY_INSTRUCTION_LEN,
			     vmx->vcpu.arch.event_exit_inst_len);
	} else
		intr |= INTR_TYPE_EXT_INTR;
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, intr);

	vmx_clear_hlt(vcpu);
}

static void vmx_inject_nmi(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (!enable_vnmi) {
		/*
		 * Tracking the NMI-blocked state in software is built upon
		 * finding the next open IRQ window. This, in turn, depends on
		 * well-behaving guests: They have to keep IRQs disabled at
		 * least as long as the NMI handler runs. Otherwise we may
		 * cause NMI nesting, maybe breaking the guest. But as this is
		 * highly unlikely, we can live with the residual risk.
		 */
		vmx->loaded_vmcs->soft_vnmi_blocked = 1;
		vmx->loaded_vmcs->vnmi_blocked_time = 0;
	}

	++vcpu->stat.nmi_injections;
	vmx->loaded_vmcs->nmi_known_unmasked = false;

	if (vmx->rmode.vm86_active) {
		kvm_inject_realmode_interrupt(vcpu, NMI_VECTOR, 0);
		return;
	}

	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
			INTR_TYPE_NMI_INTR | INTR_INFO_VALID_MASK | NMI_VECTOR);

	vmx_clear_hlt(vcpu);
}

bool vmx_get_nmi_mask(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	bool masked;

	if (!enable_vnmi)
		return vmx->loaded_vmcs->soft_vnmi_blocked;
	if (vmx->loaded_vmcs->nmi_known_unmasked)
		return false;
	masked = vmcs_read32(GUEST_INTERRUPTIBILITY_INFO) & GUEST_INTR_STATE_NMI;
	vmx->loaded_vmcs->nmi_known_unmasked = !masked;
	return masked;
}

void vmx_set_nmi_mask(struct kvm_vcpu *vcpu, bool masked)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (!enable_vnmi) {
		if (vmx->loaded_vmcs->soft_vnmi_blocked != masked) {
			vmx->loaded_vmcs->soft_vnmi_blocked = masked;
			vmx->loaded_vmcs->vnmi_blocked_time = 0;
		}
	} else {
		vmx->loaded_vmcs->nmi_known_unmasked = !masked;
		if (masked)
			vmcs_set_bits(GUEST_INTERRUPTIBILITY_INFO,
				      GUEST_INTR_STATE_NMI);
		else
			vmcs_clear_bits(GUEST_INTERRUPTIBILITY_INFO,
					GUEST_INTR_STATE_NMI);
	}
}

static int vmx_nmi_allowed(struct kvm_vcpu *vcpu)
{
	if (to_vmx(vcpu)->nested.nested_run_pending)
		return 0;

	if (!enable_vnmi &&
	    to_vmx(vcpu)->loaded_vmcs->soft_vnmi_blocked)
		return 0;

	return	!(vmcs_read32(GUEST_INTERRUPTIBILITY_INFO) &
		  (GUEST_INTR_STATE_MOV_SS | GUEST_INTR_STATE_STI
		   | GUEST_INTR_STATE_NMI));
}

static int vmx_interrupt_allowed(struct kvm_vcpu *vcpu)
{
	return (!to_vmx(vcpu)->nested.nested_run_pending &&
		vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_IF) &&
		!(vmcs_read32(GUEST_INTERRUPTIBILITY_INFO) &
			(GUEST_INTR_STATE_STI | GUEST_INTR_STATE_MOV_SS));
}

static int vmx_set_tss_addr(struct kvm *kvm, unsigned int addr)
{
	int ret;

	if (enable_unrestricted_guest)
		return 0;

	mutex_lock(&kvm->slots_lock);
	ret = __x86_set_memory_region(kvm, TSS_PRIVATE_MEMSLOT, addr,
				      PAGE_SIZE * 3);
	mutex_unlock(&kvm->slots_lock);

	if (ret)
		return ret;
	to_kvm_vmx(kvm)->tss_addr = addr;
	return init_rmode_tss(kvm);
}

static int vmx_set_identity_map_addr(struct kvm *kvm, u64 ident_addr)
{
	to_kvm_vmx(kvm)->ept_identity_map_addr = ident_addr;
	return 0;
}

static bool rmode_exception(struct kvm_vcpu *vcpu, int vec)
{
	switch (vec) {
	case BP_VECTOR:
		/*
		 * Update instruction length as we may reinject the exception
		 * from user space while in guest debugging mode.
		 */
		to_vmx(vcpu)->vcpu.arch.event_exit_inst_len =
			vmcs_read32(VM_EXIT_INSTRUCTION_LEN);
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_SW_BP)
			return false;
		/* fall through */
	case DB_VECTOR:
		if (vcpu->guest_debug &
			(KVM_GUESTDBG_SINGLESTEP | KVM_GUESTDBG_USE_HW_BP))
			return false;
		/* fall through */
	case DE_VECTOR:
	case OF_VECTOR:
	case BR_VECTOR:
	case UD_VECTOR:
	case DF_VECTOR:
	case SS_VECTOR:
	case GP_VECTOR:
	case MF_VECTOR:
		return true;
	break;
	}
	return false;
}

static int handle_rmode_exception(struct kvm_vcpu *vcpu,
				  int vec, u32 err_code)
{
	/*
	 * Instruction with address size override prefix opcode 0x67
	 * Cause the #SS fault with 0 error code in VM86 mode.
	 */
	if (((vec == GP_VECTOR) || (vec == SS_VECTOR)) && err_code == 0) {
		if (kvm_emulate_instruction(vcpu, 0)) {
			if (vcpu->arch.halt_request) {
				vcpu->arch.halt_request = 0;
				return kvm_vcpu_halt(vcpu);
			}
			return 1;
		}
		return 0;
	}

	/*
	 * Forward all other exceptions that are valid in real mode.
	 * FIXME: Breaks guest debugging in real mode, needs to be fixed with
	 *        the required debugging infrastructure rework.
	 */
	kvm_queue_exception(vcpu, vec);
	return 1;
}

/*
 * Trigger machine check on the host. We assume all the MSRs are already set up
 * by the CPU and that we still run on the same CPU as the MCE occurred on.
 * We pass a fake environment to the machine check handler because we want
 * the guest to be always treated like user space, no matter what context
 * it used internally.
 */
static void kvm_machine_check(void)
{
#if defined(CONFIG_X86_MCE) && defined(CONFIG_X86_64)
	struct pt_regs regs = {
		.cs = 3, /* Fake ring 3 no matter what the guest ran on */
		.flags = X86_EFLAGS_IF,
	};

	do_machine_check(&regs, 0);
#endif
}

static int handle_machine_check(struct kvm_vcpu *vcpu)
{
	/* handled by vmx_vcpu_run() */
	return 1;
}

static int handle_exception_nmi(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct kvm_run *kvm_run = vcpu->run;
	u32 intr_info, ex_no, error_code;
	unsigned long cr2, rip, dr6;
	u32 vect_info;

	vect_info = vmx->idt_vectoring_info;
	intr_info = vmx->exit_intr_info;

	if (is_machine_check(intr_info) || is_nmi(intr_info))
		return 1; /* handled by handle_exception_nmi_irqoff() */

	if (is_invalid_opcode(intr_info))
		return handle_ud(vcpu);

	error_code = 0;
	if (intr_info & INTR_INFO_DELIVER_CODE_MASK)
		error_code = vmcs_read32(VM_EXIT_INTR_ERROR_CODE);

	if (!vmx->rmode.vm86_active && is_gp_fault(intr_info)) {
		WARN_ON_ONCE(!enable_vmware_backdoor);

		/*
		 * VMware backdoor emulation on #GP interception only handles
		 * IN{S}, OUT{S}, and RDPMC, none of which generate a non-zero
		 * error code on #GP.
		 */
		if (error_code) {
			kvm_queue_exception_e(vcpu, GP_VECTOR, error_code);
			return 1;
		}
		return kvm_emulate_instruction(vcpu, EMULTYPE_VMWARE_GP);
	}

	/*
	 * The #PF with PFEC.RSVD = 1 indicates the guest is accessing
	 * MMIO, it is better to report an internal error.
	 * See the comments in vmx_handle_exit.
	 */
	if ((vect_info & VECTORING_INFO_VALID_MASK) &&
	    !(is_page_fault(intr_info) && !(error_code & PFERR_RSVD_MASK))) {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror = KVM_INTERNAL_ERROR_SIMUL_EX;
		vcpu->run->internal.ndata = 3;
		vcpu->run->internal.data[0] = vect_info;
		vcpu->run->internal.data[1] = intr_info;
		vcpu->run->internal.data[2] = error_code;
		return 0;
	}

	if (is_page_fault(intr_info)) {
		cr2 = vmcs_readl(EXIT_QUALIFICATION);
		/* EPT won't cause page fault directly */
		WARN_ON_ONCE(!vcpu->arch.apf.host_apf_reason && enable_ept);
		return kvm_handle_page_fault(vcpu, error_code, cr2, NULL, 0);
	}

	ex_no = intr_info & INTR_INFO_VECTOR_MASK;

	if (vmx->rmode.vm86_active && rmode_exception(vcpu, ex_no))
		return handle_rmode_exception(vcpu, ex_no, error_code);

	switch (ex_no) {
	case AC_VECTOR:
		kvm_queue_exception_e(vcpu, AC_VECTOR, error_code);
		return 1;
	case DB_VECTOR:
		dr6 = vmcs_readl(EXIT_QUALIFICATION);
		if (!(vcpu->guest_debug &
		      (KVM_GUESTDBG_SINGLESTEP | KVM_GUESTDBG_USE_HW_BP))) {
			vcpu->arch.dr6 &= ~DR_TRAP_BITS;
			vcpu->arch.dr6 |= dr6 | DR6_RTM;
			if (is_icebp(intr_info))
				WARN_ON(!skip_emulated_instruction(vcpu));

			kvm_queue_exception(vcpu, DB_VECTOR);
			return 1;
		}
		kvm_run->debug.arch.dr6 = dr6 | DR6_FIXED_1;
		kvm_run->debug.arch.dr7 = vmcs_readl(GUEST_DR7);
		/* fall through */
	case BP_VECTOR:
		/*
		 * Update instruction length as we may reinject #BP from
		 * user space while in guest debugging mode. Reading it for
		 * #DB as well causes no harm, it is not used in that case.
		 */
		vmx->vcpu.arch.event_exit_inst_len =
			vmcs_read32(VM_EXIT_INSTRUCTION_LEN);
		kvm_run->exit_reason = KVM_EXIT_DEBUG;
		rip = kvm_rip_read(vcpu);
		kvm_run->debug.arch.pc = vmcs_readl(GUEST_CS_BASE) + rip;
		kvm_run->debug.arch.exception = ex_no;
		break;
	default:
		kvm_run->exit_reason = KVM_EXIT_EXCEPTION;
		kvm_run->ex.exception = ex_no;
		kvm_run->ex.error_code = error_code;
		break;
	}
	return 0;
}

static __always_inline int handle_external_interrupt(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.irq_exits;
	return 1;
}

static int handle_triple_fault(struct kvm_vcpu *vcpu)
{
	vcpu->run->exit_reason = KVM_EXIT_SHUTDOWN;
	vcpu->mmio_needed = 0;
	return 0;
}

static int handle_io(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qualification;
	int size, in, string;
	unsigned port;

	exit_qualification = vmcs_readl(EXIT_QUALIFICATION);
	string = (exit_qualification & 16) != 0;

	++vcpu->stat.io_exits;

	if (string)
		return kvm_emulate_instruction(vcpu, 0);

	port = exit_qualification >> 16;
	size = (exit_qualification & 7) + 1;
	in = (exit_qualification & 8) != 0;

	return kvm_fast_pio(vcpu, size, port, in);
}

static void
vmx_patch_hypercall(struct kvm_vcpu *vcpu, unsigned char *hypercall)
{
	/*
	 * Patch in the VMCALL instruction:
	 */
	hypercall[0] = 0x0f;
	hypercall[1] = 0x01;
	hypercall[2] = 0xc1;
}

/* called to set cr0 as appropriate for a mov-to-cr0 exit. */
static int handle_set_cr0(struct kvm_vcpu *vcpu, unsigned long val)
{
	if (is_guest_mode(vcpu)) {
		struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
		unsigned long orig_val = val;

		/*
		 * We get here when L2 changed cr0 in a way that did not change
		 * any of L1's shadowed bits (see nested_vmx_exit_handled_cr),
		 * but did change L0 shadowed bits. So we first calculate the
		 * effective cr0 value that L1 would like to write into the
		 * hardware. It consists of the L2-owned bits from the new
		 * value combined with the L1-owned bits from L1's guest_cr0.
		 */
		val = (val & ~vmcs12->cr0_guest_host_mask) |
			(vmcs12->guest_cr0 & vmcs12->cr0_guest_host_mask);

		if (!nested_guest_cr0_valid(vcpu, val))
			return 1;

		if (kvm_set_cr0(vcpu, val))
			return 1;
		vmcs_writel(CR0_READ_SHADOW, orig_val);
		return 0;
	} else {
		if (to_vmx(vcpu)->nested.vmxon &&
		    !nested_host_cr0_valid(vcpu, val))
			return 1;

		return kvm_set_cr0(vcpu, val);
	}
}

static int handle_set_cr4(struct kvm_vcpu *vcpu, unsigned long val)
{
	if (is_guest_mode(vcpu)) {
		struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
		unsigned long orig_val = val;

		/* analogously to handle_set_cr0 */
		val = (val & ~vmcs12->cr4_guest_host_mask) |
			(vmcs12->guest_cr4 & vmcs12->cr4_guest_host_mask);
		if (kvm_set_cr4(vcpu, val))
			return 1;
		vmcs_writel(CR4_READ_SHADOW, orig_val);
		return 0;
	} else
		return kvm_set_cr4(vcpu, val);
}

static int handle_desc(struct kvm_vcpu *vcpu)
{
	WARN_ON(!(vcpu->arch.cr4 & X86_CR4_UMIP));
	return kvm_emulate_instruction(vcpu, 0);
}

static int handle_cr(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qualification, val;
	int cr;
	int reg;
	int err;
	int ret;

	exit_qualification = vmcs_readl(EXIT_QUALIFICATION);
	cr = exit_qualification & 15;
	reg = (exit_qualification >> 8) & 15;
	switch ((exit_qualification >> 4) & 3) {
	case 0: /* mov to cr */
		val = kvm_register_readl(vcpu, reg);
		trace_kvm_cr_write(cr, val);
		switch (cr) {
		case 0:
			err = handle_set_cr0(vcpu, val);
			return kvm_complete_insn_gp(vcpu, err);
		case 3:
			WARN_ON_ONCE(enable_unrestricted_guest);
			err = kvm_set_cr3(vcpu, val);
			return kvm_complete_insn_gp(vcpu, err);
		case 4:
			err = handle_set_cr4(vcpu, val);
			return kvm_complete_insn_gp(vcpu, err);
		case 8: {
				u8 cr8_prev = kvm_get_cr8(vcpu);
				u8 cr8 = (u8)val;
				err = kvm_set_cr8(vcpu, cr8);
				ret = kvm_complete_insn_gp(vcpu, err);
				if (lapic_in_kernel(vcpu))
					return ret;
				if (cr8_prev <= cr8)
					return ret;
				/*
				 * TODO: we might be squashing a
				 * KVM_GUESTDBG_SINGLESTEP-triggered
				 * KVM_EXIT_DEBUG here.
				 */
				vcpu->run->exit_reason = KVM_EXIT_SET_TPR;
				return 0;
			}
		}
		break;
	case 2: /* clts */
		WARN_ONCE(1, "Guest should always own CR0.TS");
		vmx_set_cr0(vcpu, kvm_read_cr0_bits(vcpu, ~X86_CR0_TS));
		trace_kvm_cr_write(0, kvm_read_cr0(vcpu));
		return kvm_skip_emulated_instruction(vcpu);
	case 1: /*mov from cr*/
		switch (cr) {
		case 3:
			WARN_ON_ONCE(enable_unrestricted_guest);
			val = kvm_read_cr3(vcpu);
			kvm_register_write(vcpu, reg, val);
			trace_kvm_cr_read(cr, val);
			return kvm_skip_emulated_instruction(vcpu);
		case 8:
			val = kvm_get_cr8(vcpu);
			kvm_register_write(vcpu, reg, val);
			trace_kvm_cr_read(cr, val);
			return kvm_skip_emulated_instruction(vcpu);
		}
		break;
	case 3: /* lmsw */
		val = (exit_qualification >> LMSW_SOURCE_DATA_SHIFT) & 0x0f;
		trace_kvm_cr_write(0, (kvm_read_cr0(vcpu) & ~0xful) | val);
		kvm_lmsw(vcpu, val);

		return kvm_skip_emulated_instruction(vcpu);
	default:
		break;
	}
	vcpu->run->exit_reason = 0;
	vcpu_unimpl(vcpu, "unhandled control register: op %d cr %d\n",
	       (int)(exit_qualification >> 4) & 3, cr);
	return 0;
}

static int handle_dr(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qualification;
	int dr, dr7, reg;

	exit_qualification = vmcs_readl(EXIT_QUALIFICATION);
	dr = exit_qualification & DEBUG_REG_ACCESS_NUM;

	/* First, if DR does not exist, trigger UD */
	if (!kvm_require_dr(vcpu, dr))
		return 1;

	/* Do not handle if the CPL > 0, will trigger GP on re-entry */
	if (!kvm_require_cpl(vcpu, 0))
		return 1;
	dr7 = vmcs_readl(GUEST_DR7);
	if (dr7 & DR7_GD) {
		/*
		 * As the vm-exit takes precedence over the debug trap, we
		 * need to emulate the latter, either for the host or the
		 * guest debugging itself.
		 */
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP) {
			vcpu->run->debug.arch.dr6 = vcpu->arch.dr6;
			vcpu->run->debug.arch.dr7 = dr7;
			vcpu->run->debug.arch.pc = kvm_get_linear_rip(vcpu);
			vcpu->run->debug.arch.exception = DB_VECTOR;
			vcpu->run->exit_reason = KVM_EXIT_DEBUG;
			return 0;
		} else {
			vcpu->arch.dr6 &= ~DR_TRAP_BITS;
			vcpu->arch.dr6 |= DR6_BD | DR6_RTM;
			kvm_queue_exception(vcpu, DB_VECTOR);
			return 1;
		}
	}

	if (vcpu->guest_debug == 0) {
		exec_controls_clearbit(to_vmx(vcpu), CPU_BASED_MOV_DR_EXITING);

		/*
		 * No more DR vmexits; force a reload of the debug registers
		 * and reenter on this instruction.  The next vmexit will
		 * retrieve the full state of the debug registers.
		 */
		vcpu->arch.switch_db_regs |= KVM_DEBUGREG_WONT_EXIT;
		return 1;
	}

	reg = DEBUG_REG_ACCESS_REG(exit_qualification);
	if (exit_qualification & TYPE_MOV_FROM_DR) {
		unsigned long val;

		if (kvm_get_dr(vcpu, dr, &val))
			return 1;
		kvm_register_write(vcpu, reg, val);
	} else
		if (kvm_set_dr(vcpu, dr, kvm_register_readl(vcpu, reg)))
			return 1;

	return kvm_skip_emulated_instruction(vcpu);
}

static u64 vmx_get_dr6(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.dr6;
}

static void vmx_set_dr6(struct kvm_vcpu *vcpu, unsigned long val)
{
}

static void vmx_sync_dirty_debug_regs(struct kvm_vcpu *vcpu)
{
	get_debugreg(vcpu->arch.db[0], 0);
	get_debugreg(vcpu->arch.db[1], 1);
	get_debugreg(vcpu->arch.db[2], 2);
	get_debugreg(vcpu->arch.db[3], 3);
	get_debugreg(vcpu->arch.dr6, 6);
	vcpu->arch.dr7 = vmcs_readl(GUEST_DR7);

	vcpu->arch.switch_db_regs &= ~KVM_DEBUGREG_WONT_EXIT;
	exec_controls_setbit(to_vmx(vcpu), CPU_BASED_MOV_DR_EXITING);
}

static void vmx_set_dr7(struct kvm_vcpu *vcpu, unsigned long val)
{
	vmcs_writel(GUEST_DR7, val);
}

static int handle_tpr_below_threshold(struct kvm_vcpu *vcpu)
{
	kvm_apic_update_ppr(vcpu);
	return 1;
}

static int handle_interrupt_window(struct kvm_vcpu *vcpu)
{
	exec_controls_clearbit(to_vmx(vcpu), CPU_BASED_INTR_WINDOW_EXITING);

	kvm_make_request(KVM_REQ_EVENT, vcpu);

	++vcpu->stat.irq_window_exits;
	return 1;
}

static int handle_vmcall(struct kvm_vcpu *vcpu)
{
	return kvm_emulate_hypercall(vcpu);
}

static int handle_invd(struct kvm_vcpu *vcpu)
{
	return kvm_emulate_instruction(vcpu, 0);
}

static int handle_invlpg(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qualification = vmcs_readl(EXIT_QUALIFICATION);

	kvm_mmu_invlpg(vcpu, exit_qualification);
	return kvm_skip_emulated_instruction(vcpu);
}

static int handle_rdpmc(struct kvm_vcpu *vcpu)
{
	int err;

	err = kvm_rdpmc(vcpu);
	return kvm_complete_insn_gp(vcpu, err);
}

static int handle_wbinvd(struct kvm_vcpu *vcpu)
{
	return kvm_emulate_wbinvd(vcpu);
}

static int handle_xsetbv(struct kvm_vcpu *vcpu)
{
	u64 new_bv = kvm_read_edx_eax(vcpu);
	u32 index = kvm_rcx_read(vcpu);

	if (kvm_set_xcr(vcpu, index, new_bv) == 0)
		return kvm_skip_emulated_instruction(vcpu);
	return 1;
}

static int handle_apic_access(struct kvm_vcpu *vcpu)
{
	if (likely(fasteoi)) {
		unsigned long exit_qualification = vmcs_readl(EXIT_QUALIFICATION);
		int access_type, offset;

		access_type = exit_qualification & APIC_ACCESS_TYPE;
		offset = exit_qualification & APIC_ACCESS_OFFSET;
		/*
		 * Sane guest uses MOV to write EOI, with written value
		 * not cared. So make a short-circuit here by avoiding
		 * heavy instruction emulation.
		 */
		if ((access_type == TYPE_LINEAR_APIC_INST_WRITE) &&
		    (offset == APIC_EOI)) {
			kvm_lapic_set_eoi(vcpu);
			return kvm_skip_emulated_instruction(vcpu);
		}
	}
	return kvm_emulate_instruction(vcpu, 0);
}

static int handle_apic_eoi_induced(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qualification = vmcs_readl(EXIT_QUALIFICATION);
	int vector = exit_qualification & 0xff;

	/* EOI-induced VM exit is trap-like and thus no need to adjust IP */
	kvm_apic_set_eoi_accelerated(vcpu, vector);
	return 1;
}

static int handle_apic_write(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qualification = vmcs_readl(EXIT_QUALIFICATION);
	u32 offset = exit_qualification & 0xfff;

	/* APIC-write VM exit is trap-like and thus no need to adjust IP */
	kvm_apic_write_nodecode(vcpu, offset);
	return 1;
}

static int handle_task_switch(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long exit_qualification;
	bool has_error_code = false;
	u32 error_code = 0;
	u16 tss_selector;
	int reason, type, idt_v, idt_index;

	idt_v = (vmx->idt_vectoring_info & VECTORING_INFO_VALID_MASK);
	idt_index = (vmx->idt_vectoring_info & VECTORING_INFO_VECTOR_MASK);
	type = (vmx->idt_vectoring_info & VECTORING_INFO_TYPE_MASK);

	exit_qualification = vmcs_readl(EXIT_QUALIFICATION);

	reason = (u32)exit_qualification >> 30;
	if (reason == TASK_SWITCH_GATE && idt_v) {
		switch (type) {
		case INTR_TYPE_NMI_INTR:
			vcpu->arch.nmi_injected = false;
			vmx_set_nmi_mask(vcpu, true);
			break;
		case INTR_TYPE_EXT_INTR:
		case INTR_TYPE_SOFT_INTR:
			kvm_clear_interrupt_queue(vcpu);
			break;
		case INTR_TYPE_HARD_EXCEPTION:
			if (vmx->idt_vectoring_info &
			    VECTORING_INFO_DELIVER_CODE_MASK) {
				has_error_code = true;
				error_code =
					vmcs_read32(IDT_VECTORING_ERROR_CODE);
			}
			/* fall through */
		case INTR_TYPE_SOFT_EXCEPTION:
			kvm_clear_exception_queue(vcpu);
			break;
		default:
			break;
		}
	}
	tss_selector = exit_qualification;

	if (!idt_v || (type != INTR_TYPE_HARD_EXCEPTION &&
		       type != INTR_TYPE_EXT_INTR &&
		       type != INTR_TYPE_NMI_INTR))
		WARN_ON(!skip_emulated_instruction(vcpu));

	/*
	 * TODO: What about debug traps on tss switch?
	 *       Are we supposed to inject them and update dr6?
	 */
	return kvm_task_switch(vcpu, tss_selector,
			       type == INTR_TYPE_SOFT_INTR ? idt_index : -1,
			       reason, has_error_code, error_code);
}

static int handle_ept_violation(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qualification;
	gpa_t gpa;
	u64 error_code;

	exit_qualification = vmcs_readl(EXIT_QUALIFICATION);

	/*
	 * EPT violation happened while executing iret from NMI,
	 * "blocked by NMI" bit has to be set before next VM entry.
	 * There are errata that may cause this bit to not be set:
	 * AAK134, BY25.
	 */
	if (!(to_vmx(vcpu)->idt_vectoring_info & VECTORING_INFO_VALID_MASK) &&
			enable_vnmi &&
			(exit_qualification & INTR_INFO_UNBLOCK_NMI))
		vmcs_set_bits(GUEST_INTERRUPTIBILITY_INFO, GUEST_INTR_STATE_NMI);

	gpa = vmcs_read64(GUEST_PHYSICAL_ADDRESS);
	trace_kvm_page_fault(gpa, exit_qualification);

	/* Is it a read fault? */
	error_code = (exit_qualification & EPT_VIOLATION_ACC_READ)
		     ? PFERR_USER_MASK : 0;
	/* Is it a write fault? */
	error_code |= (exit_qualification & EPT_VIOLATION_ACC_WRITE)
		      ? PFERR_WRITE_MASK : 0;
	/* Is it a fetch fault? */
	error_code |= (exit_qualification & EPT_VIOLATION_ACC_INSTR)
		      ? PFERR_FETCH_MASK : 0;
	/* ept page table entry is present? */
	error_code |= (exit_qualification &
		       (EPT_VIOLATION_READABLE | EPT_VIOLATION_WRITABLE |
			EPT_VIOLATION_EXECUTABLE))
		      ? PFERR_PRESENT_MASK : 0;

	error_code |= (exit_qualification & 0x100) != 0 ?
	       PFERR_GUEST_FINAL_MASK : PFERR_GUEST_PAGE_MASK;

	vcpu->arch.exit_qualification = exit_qualification;
	return kvm_mmu_page_fault(vcpu, gpa, error_code, NULL, 0);
}

static int handle_ept_misconfig(struct kvm_vcpu *vcpu)
{
	gpa_t gpa;

	/*
	 * A nested guest cannot optimize MMIO vmexits, because we have an
	 * nGPA here instead of the required GPA.
	 */
	gpa = vmcs_read64(GUEST_PHYSICAL_ADDRESS);
	if (!is_guest_mode(vcpu) &&
	    !kvm_io_bus_write(vcpu, KVM_FAST_MMIO_BUS, gpa, 0, NULL)) {
		trace_kvm_fast_mmio(gpa);
		return kvm_skip_emulated_instruction(vcpu);
	}

	return kvm_mmu_page_fault(vcpu, gpa, PFERR_RSVD_MASK, NULL, 0);
}

static int handle_nmi_window(struct kvm_vcpu *vcpu)
{
	WARN_ON_ONCE(!enable_vnmi);
	exec_controls_clearbit(to_vmx(vcpu), CPU_BASED_NMI_WINDOW_EXITING);
	++vcpu->stat.nmi_window_exits;
	kvm_make_request(KVM_REQ_EVENT, vcpu);

	return 1;
}

static int handle_invalid_guest_state(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	bool intr_window_requested;
	unsigned count = 130;

	/*
	 * We should never reach the point where we are emulating L2
	 * due to invalid guest state as that means we incorrectly
	 * allowed a nested VMEntry with an invalid vmcs12.
	 */
	WARN_ON_ONCE(vmx->emulation_required && vmx->nested.nested_run_pending);

	intr_window_requested = exec_controls_get(vmx) &
				CPU_BASED_INTR_WINDOW_EXITING;

	while (vmx->emulation_required && count-- != 0) {
		if (intr_window_requested && vmx_interrupt_allowed(vcpu))
			return handle_interrupt_window(&vmx->vcpu);

		if (kvm_test_request(KVM_REQ_EVENT, vcpu))
			return 1;

		if (!kvm_emulate_instruction(vcpu, 0))
			return 0;

		if (vmx->emulation_required && !vmx->rmode.vm86_active &&
		    vcpu->arch.exception.pending) {
			vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			vcpu->run->internal.suberror =
						KVM_INTERNAL_ERROR_EMULATION;
			vcpu->run->internal.ndata = 0;
			return 0;
		}

		if (vcpu->arch.halt_request) {
			vcpu->arch.halt_request = 0;
			return kvm_vcpu_halt(vcpu);
		}

		/*
		 * Note, return 1 and not 0, vcpu_run() is responsible for
		 * morphing the pending signal into the proper return code.
		 */
		if (signal_pending(current))
			return 1;

		if (need_resched())
			schedule();
	}

	return 1;
}

static void grow_ple_window(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned int old = vmx->ple_window;

	vmx->ple_window = __grow_ple_window(old, ple_window,
					    ple_window_grow,
					    ple_window_max);

	if (vmx->ple_window != old) {
		vmx->ple_window_dirty = true;
		trace_kvm_ple_window_update(vcpu->vcpu_id,
					    vmx->ple_window, old);
	}
}

static void shrink_ple_window(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned int old = vmx->ple_window;

	vmx->ple_window = __shrink_ple_window(old, ple_window,
					      ple_window_shrink,
					      ple_window);

	if (vmx->ple_window != old) {
		vmx->ple_window_dirty = true;
		trace_kvm_ple_window_update(vcpu->vcpu_id,
					    vmx->ple_window, old);
	}
}

/*
 * Handler for POSTED_INTERRUPT_WAKEUP_VECTOR.
 */
static void wakeup_handler(void)
{
	struct kvm_vcpu *vcpu;
	int cpu = smp_processor_id();

	spin_lock(&per_cpu(blocked_vcpu_on_cpu_lock, cpu));
	list_for_each_entry(vcpu, &per_cpu(blocked_vcpu_on_cpu, cpu),
			blocked_vcpu_list) {
		struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);

		if (pi_test_on(pi_desc) == 1)
			kvm_vcpu_kick(vcpu);
	}
	spin_unlock(&per_cpu(blocked_vcpu_on_cpu_lock, cpu));
}

static void vmx_enable_tdp(void)
{
	kvm_mmu_set_mask_ptes(VMX_EPT_READABLE_MASK,
		enable_ept_ad_bits ? VMX_EPT_ACCESS_BIT : 0ull,
		enable_ept_ad_bits ? VMX_EPT_DIRTY_BIT : 0ull,
		0ull, VMX_EPT_EXECUTABLE_MASK,
		cpu_has_vmx_ept_execute_only() ? 0ull : VMX_EPT_READABLE_MASK,
		VMX_EPT_RWX_MASK, 0ull);

	ept_set_mmio_spte_mask();
	kvm_enable_tdp();
}

/*
 * Indicate a busy-waiting vcpu in spinlock. We do not enable the PAUSE
 * exiting, so only get here on cpu with PAUSE-Loop-Exiting.
 */
static int handle_pause(struct kvm_vcpu *vcpu)
{
	if (!kvm_pause_in_guest(vcpu->kvm))
		grow_ple_window(vcpu);

	/*
	 * Intel sdm vol3 ch-25.1.3 says: The "PAUSE-loop exiting"
	 * VM-execution control is ignored if CPL > 0. OTOH, KVM
	 * never set PAUSE_EXITING and just set PLE if supported,
	 * so the vcpu must be CPL=0 if it gets a PAUSE exit.
	 */
	kvm_vcpu_on_spin(vcpu, true);
	return kvm_skip_emulated_instruction(vcpu);
}

static int handle_nop(struct kvm_vcpu *vcpu)
{
	return kvm_skip_emulated_instruction(vcpu);
}

static int handle_mwait(struct kvm_vcpu *vcpu)
{
	printk_once(KERN_WARNING "kvm: MWAIT instruction emulated as NOP!\n");
	return handle_nop(vcpu);
}

static int handle_invalid_op(struct kvm_vcpu *vcpu)
{
	kvm_queue_exception(vcpu, UD_VECTOR);
	return 1;
}

static int handle_monitor_trap(struct kvm_vcpu *vcpu)
{
	return 1;
}

static int handle_monitor(struct kvm_vcpu *vcpu)
{
	printk_once(KERN_WARNING "kvm: MONITOR instruction emulated as NOP!\n");
	return handle_nop(vcpu);
}

static int handle_invpcid(struct kvm_vcpu *vcpu)
{
	u32 vmx_instruction_info;
	unsigned long type;
	bool pcid_enabled;
	gva_t gva;
	struct x86_exception e;
	unsigned i;
	unsigned long roots_to_free = 0;
	struct {
		u64 pcid;
		u64 gla;
	} operand;

	if (!guest_cpuid_has(vcpu, X86_FEATURE_INVPCID)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	}

	vmx_instruction_info = vmcs_read32(VMX_INSTRUCTION_INFO);
	type = kvm_register_readl(vcpu, (vmx_instruction_info >> 28) & 0xf);

	if (type > 3) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	/* According to the Intel instruction reference, the memory operand
	 * is read even if it isn't needed (e.g., for type==all)
	 */
	if (get_vmx_mem_address(vcpu, vmcs_readl(EXIT_QUALIFICATION),
				vmx_instruction_info, false,
				sizeof(operand), &gva))
		return 1;

	if (kvm_read_guest_virt(vcpu, gva, &operand, sizeof(operand), &e)) {
		kvm_inject_page_fault(vcpu, &e);
		return 1;
	}

	if (operand.pcid >> 12 != 0) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	pcid_enabled = kvm_read_cr4_bits(vcpu, X86_CR4_PCIDE);

	switch (type) {
	case INVPCID_TYPE_INDIV_ADDR:
		if ((!pcid_enabled && (operand.pcid != 0)) ||
		    is_noncanonical_address(operand.gla, vcpu)) {
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

		if (kvm_get_active_pcid(vcpu) == operand.pcid) {
			kvm_mmu_sync_roots(vcpu);
			kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
		}

		for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
			if (kvm_get_pcid(vcpu, vcpu->arch.mmu->prev_roots[i].cr3)
			    == operand.pcid)
				roots_to_free |= KVM_MMU_ROOT_PREVIOUS(i);

		kvm_mmu_free_roots(vcpu, vcpu->arch.mmu, roots_to_free);
		/*
		 * If neither the current cr3 nor any of the prev_roots use the
		 * given PCID, then nothing needs to be done here because a
		 * resync will happen anyway before switching to any other CR3.
		 */

		return kvm_skip_emulated_instruction(vcpu);

	case INVPCID_TYPE_ALL_NON_GLOBAL:
		/*
		 * Currently, KVM doesn't mark global entries in the shadow
		 * page tables, so a non-global flush just degenerates to a
		 * global flush. If needed, we could optimize this later by
		 * keeping track of global entries in shadow page tables.
		 */

		/* fall-through */
	case INVPCID_TYPE_ALL_INCL_GLOBAL:
		kvm_mmu_unload(vcpu);
		return kvm_skip_emulated_instruction(vcpu);

	default:
		BUG(); /* We have already checked above that type <= 3 */
	}
}

static int handle_pml_full(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qualification;

	trace_kvm_pml_full(vcpu->vcpu_id);

	exit_qualification = vmcs_readl(EXIT_QUALIFICATION);

	/*
	 * PML buffer FULL happened while executing iret from NMI,
	 * "blocked by NMI" bit has to be set before next VM entry.
	 */
	if (!(to_vmx(vcpu)->idt_vectoring_info & VECTORING_INFO_VALID_MASK) &&
			enable_vnmi &&
			(exit_qualification & INTR_INFO_UNBLOCK_NMI))
		vmcs_set_bits(GUEST_INTERRUPTIBILITY_INFO,
				GUEST_INTR_STATE_NMI);

	/*
	 * PML buffer already flushed at beginning of VMEXIT. Nothing to do
	 * here.., and there's no userspace involvement needed for PML.
	 */
	return 1;
}

static int handle_preemption_timer(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (!vmx->req_immediate_exit &&
	    !unlikely(vmx->loaded_vmcs->hv_timer_soft_disabled))
		kvm_lapic_expired_hv_timer(vcpu);

	return 1;
}

/*
 * When nested=0, all VMX instruction VM Exits filter here.  The handlers
 * are overwritten by nested_vmx_setup() when nested=1.
 */
static int handle_vmx_instruction(struct kvm_vcpu *vcpu)
{
	kvm_queue_exception(vcpu, UD_VECTOR);
	return 1;
}

static int handle_encls(struct kvm_vcpu *vcpu)
{
	/*
	 * SGX virtualization is not yet supported.  There is no software
	 * enable bit for SGX, so we have to trap ENCLS and inject a #UD
	 * to prevent the guest from executing ENCLS.
	 */
	kvm_queue_exception(vcpu, UD_VECTOR);
	return 1;
}

/*
 * The exit handlers return 1 if the exit was handled fully and guest execution
 * may resume.  Otherwise they set the kvm_run parameter to indicate what needs
 * to be done to userspace and return 0.
 */
static int (*kvm_vmx_exit_handlers[])(struct kvm_vcpu *vcpu) = {
	[EXIT_REASON_EXCEPTION_NMI]           = handle_exception_nmi,
	[EXIT_REASON_EXTERNAL_INTERRUPT]      = handle_external_interrupt,
	[EXIT_REASON_TRIPLE_FAULT]            = handle_triple_fault,
	[EXIT_REASON_NMI_WINDOW]	      = handle_nmi_window,
	[EXIT_REASON_IO_INSTRUCTION]          = handle_io,
	[EXIT_REASON_CR_ACCESS]               = handle_cr,
	[EXIT_REASON_DR_ACCESS]               = handle_dr,
	[EXIT_REASON_CPUID]                   = kvm_emulate_cpuid,
	[EXIT_REASON_MSR_READ]                = kvm_emulate_rdmsr,
	[EXIT_REASON_MSR_WRITE]               = kvm_emulate_wrmsr,
	[EXIT_REASON_INTERRUPT_WINDOW]        = handle_interrupt_window,
	[EXIT_REASON_HLT]                     = kvm_emulate_halt,
	[EXIT_REASON_INVD]		      = handle_invd,
	[EXIT_REASON_INVLPG]		      = handle_invlpg,
	[EXIT_REASON_RDPMC]                   = handle_rdpmc,
	[EXIT_REASON_VMCALL]                  = handle_vmcall,
	[EXIT_REASON_VMCLEAR]		      = handle_vmx_instruction,
	[EXIT_REASON_VMLAUNCH]		      = handle_vmx_instruction,
	[EXIT_REASON_VMPTRLD]		      = handle_vmx_instruction,
	[EXIT_REASON_VMPTRST]		      = handle_vmx_instruction,
	[EXIT_REASON_VMREAD]		      = handle_vmx_instruction,
	[EXIT_REASON_VMRESUME]		      = handle_vmx_instruction,
	[EXIT_REASON_VMWRITE]		      = handle_vmx_instruction,
	[EXIT_REASON_VMOFF]		      = handle_vmx_instruction,
	[EXIT_REASON_VMON]		      = handle_vmx_instruction,
	[EXIT_REASON_TPR_BELOW_THRESHOLD]     = handle_tpr_below_threshold,
	[EXIT_REASON_APIC_ACCESS]             = handle_apic_access,
	[EXIT_REASON_APIC_WRITE]              = handle_apic_write,
	[EXIT_REASON_EOI_INDUCED]             = handle_apic_eoi_induced,
	[EXIT_REASON_WBINVD]                  = handle_wbinvd,
	[EXIT_REASON_XSETBV]                  = handle_xsetbv,
	[EXIT_REASON_TASK_SWITCH]             = handle_task_switch,
	[EXIT_REASON_MCE_DURING_VMENTRY]      = handle_machine_check,
	[EXIT_REASON_GDTR_IDTR]		      = handle_desc,
	[EXIT_REASON_LDTR_TR]		      = handle_desc,
	[EXIT_REASON_EPT_VIOLATION]	      = handle_ept_violation,
	[EXIT_REASON_EPT_MISCONFIG]           = handle_ept_misconfig,
	[EXIT_REASON_PAUSE_INSTRUCTION]       = handle_pause,
	[EXIT_REASON_MWAIT_INSTRUCTION]	      = handle_mwait,
	[EXIT_REASON_MONITOR_TRAP_FLAG]       = handle_monitor_trap,
	[EXIT_REASON_MONITOR_INSTRUCTION]     = handle_monitor,
	[EXIT_REASON_INVEPT]                  = handle_vmx_instruction,
	[EXIT_REASON_INVVPID]                 = handle_vmx_instruction,
	[EXIT_REASON_RDRAND]                  = handle_invalid_op,
	[EXIT_REASON_RDSEED]                  = handle_invalid_op,
	[EXIT_REASON_PML_FULL]		      = handle_pml_full,
	[EXIT_REASON_INVPCID]                 = handle_invpcid,
	[EXIT_REASON_VMFUNC]		      = handle_vmx_instruction,
	[EXIT_REASON_PREEMPTION_TIMER]	      = handle_preemption_timer,
	[EXIT_REASON_ENCLS]		      = handle_encls,
};

static const int kvm_vmx_max_exit_handlers =
	ARRAY_SIZE(kvm_vmx_exit_handlers);

static void vmx_get_exit_info(struct kvm_vcpu *vcpu, u64 *info1, u64 *info2)
{
	*info1 = vmcs_readl(EXIT_QUALIFICATION);
	*info2 = vmcs_read32(VM_EXIT_INTR_INFO);
}

static void vmx_destroy_pml_buffer(struct vcpu_vmx *vmx)
{
	if (vmx->pml_pg) {
		__free_page(vmx->pml_pg);
		vmx->pml_pg = NULL;
	}
}

static void vmx_flush_pml_buffer(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u64 *pml_buf;
	u16 pml_idx;

	pml_idx = vmcs_read16(GUEST_PML_INDEX);

	/* Do nothing if PML buffer is empty */
	if (pml_idx == (PML_ENTITY_NUM - 1))
		return;

	/* PML index always points to next available PML buffer entity */
	if (pml_idx >= PML_ENTITY_NUM)
		pml_idx = 0;
	else
		pml_idx++;

	pml_buf = page_address(vmx->pml_pg);
	for (; pml_idx < PML_ENTITY_NUM; pml_idx++) {
		u64 gpa;

		gpa = pml_buf[pml_idx];
		WARN_ON(gpa & (PAGE_SIZE - 1));
		kvm_vcpu_mark_page_dirty(vcpu, gpa >> PAGE_SHIFT);
	}

	/* reset PML index */
	vmcs_write16(GUEST_PML_INDEX, PML_ENTITY_NUM - 1);
}

/*
 * Flush all vcpus' PML buffer and update logged GPAs to dirty_bitmap.
 * Called before reporting dirty_bitmap to userspace.
 */
static void kvm_flush_pml_buffers(struct kvm *kvm)
{
	int i;
	struct kvm_vcpu *vcpu;
	/*
	 * We only need to kick vcpu out of guest mode here, as PML buffer
	 * is flushed at beginning of all VMEXITs, and it's obvious that only
	 * vcpus running in guest are possible to have unflushed GPAs in PML
	 * buffer.
	 */
	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_vcpu_kick(vcpu);
}

static void vmx_dump_sel(char *name, uint32_t sel)
{
	pr_err("%s sel=0x%04x, attr=0x%05x, limit=0x%08x, base=0x%016lx\n",
	       name, vmcs_read16(sel),
	       vmcs_read32(sel + GUEST_ES_AR_BYTES - GUEST_ES_SELECTOR),
	       vmcs_read32(sel + GUEST_ES_LIMIT - GUEST_ES_SELECTOR),
	       vmcs_readl(sel + GUEST_ES_BASE - GUEST_ES_SELECTOR));
}

static void vmx_dump_dtsel(char *name, uint32_t limit)
{
	pr_err("%s                           limit=0x%08x, base=0x%016lx\n",
	       name, vmcs_read32(limit),
	       vmcs_readl(limit + GUEST_GDTR_BASE - GUEST_GDTR_LIMIT));
}

void dump_vmcs(void)
{
	u32 vmentry_ctl, vmexit_ctl;
	u32 cpu_based_exec_ctrl, pin_based_exec_ctrl, secondary_exec_control;
	unsigned long cr4;
	u64 efer;
	int i, n;

	if (!dump_invalid_vmcs) {
		pr_warn_ratelimited("set kvm_intel.dump_invalid_vmcs=1 to dump internal KVM state.\n");
		return;
	}

	vmentry_ctl = vmcs_read32(VM_ENTRY_CONTROLS);
	vmexit_ctl = vmcs_read32(VM_EXIT_CONTROLS);
	cpu_based_exec_ctrl = vmcs_read32(CPU_BASED_VM_EXEC_CONTROL);
	pin_based_exec_ctrl = vmcs_read32(PIN_BASED_VM_EXEC_CONTROL);
	cr4 = vmcs_readl(GUEST_CR4);
	efer = vmcs_read64(GUEST_IA32_EFER);
	secondary_exec_control = 0;
	if (cpu_has_secondary_exec_ctrls())
		secondary_exec_control = vmcs_read32(SECONDARY_VM_EXEC_CONTROL);

	pr_err("*** Guest State ***\n");
	pr_err("CR0: actual=0x%016lx, shadow=0x%016lx, gh_mask=%016lx\n",
	       vmcs_readl(GUEST_CR0), vmcs_readl(CR0_READ_SHADOW),
	       vmcs_readl(CR0_GUEST_HOST_MASK));
	pr_err("CR4: actual=0x%016lx, shadow=0x%016lx, gh_mask=%016lx\n",
	       cr4, vmcs_readl(CR4_READ_SHADOW), vmcs_readl(CR4_GUEST_HOST_MASK));
	pr_err("CR3 = 0x%016lx\n", vmcs_readl(GUEST_CR3));
	if ((secondary_exec_control & SECONDARY_EXEC_ENABLE_EPT) &&
	    (cr4 & X86_CR4_PAE) && !(efer & EFER_LMA))
	{
		pr_err("PDPTR0 = 0x%016llx  PDPTR1 = 0x%016llx\n",
		       vmcs_read64(GUEST_PDPTR0), vmcs_read64(GUEST_PDPTR1));
		pr_err("PDPTR2 = 0x%016llx  PDPTR3 = 0x%016llx\n",
		       vmcs_read64(GUEST_PDPTR2), vmcs_read64(GUEST_PDPTR3));
	}
	pr_err("RSP = 0x%016lx  RIP = 0x%016lx\n",
	       vmcs_readl(GUEST_RSP), vmcs_readl(GUEST_RIP));
	pr_err("RFLAGS=0x%08lx         DR7 = 0x%016lx\n",
	       vmcs_readl(GUEST_RFLAGS), vmcs_readl(GUEST_DR7));
	pr_err("Sysenter RSP=%016lx CS:RIP=%04x:%016lx\n",
	       vmcs_readl(GUEST_SYSENTER_ESP),
	       vmcs_read32(GUEST_SYSENTER_CS), vmcs_readl(GUEST_SYSENTER_EIP));
	vmx_dump_sel("CS:  ", GUEST_CS_SELECTOR);
	vmx_dump_sel("DS:  ", GUEST_DS_SELECTOR);
	vmx_dump_sel("SS:  ", GUEST_SS_SELECTOR);
	vmx_dump_sel("ES:  ", GUEST_ES_SELECTOR);
	vmx_dump_sel("FS:  ", GUEST_FS_SELECTOR);
	vmx_dump_sel("GS:  ", GUEST_GS_SELECTOR);
	vmx_dump_dtsel("GDTR:", GUEST_GDTR_LIMIT);
	vmx_dump_sel("LDTR:", GUEST_LDTR_SELECTOR);
	vmx_dump_dtsel("IDTR:", GUEST_IDTR_LIMIT);
	vmx_dump_sel("TR:  ", GUEST_TR_SELECTOR);
	if ((vmexit_ctl & (VM_EXIT_SAVE_IA32_PAT | VM_EXIT_SAVE_IA32_EFER)) ||
	    (vmentry_ctl & (VM_ENTRY_LOAD_IA32_PAT | VM_ENTRY_LOAD_IA32_EFER)))
		pr_err("EFER =     0x%016llx  PAT = 0x%016llx\n",
		       efer, vmcs_read64(GUEST_IA32_PAT));
	pr_err("DebugCtl = 0x%016llx  DebugExceptions = 0x%016lx\n",
	       vmcs_read64(GUEST_IA32_DEBUGCTL),
	       vmcs_readl(GUEST_PENDING_DBG_EXCEPTIONS));
	if (cpu_has_load_perf_global_ctrl() &&
	    vmentry_ctl & VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL)
		pr_err("PerfGlobCtl = 0x%016llx\n",
		       vmcs_read64(GUEST_IA32_PERF_GLOBAL_CTRL));
	if (vmentry_ctl & VM_ENTRY_LOAD_BNDCFGS)
		pr_err("BndCfgS = 0x%016llx\n", vmcs_read64(GUEST_BNDCFGS));
	pr_err("Interruptibility = %08x  ActivityState = %08x\n",
	       vmcs_read32(GUEST_INTERRUPTIBILITY_INFO),
	       vmcs_read32(GUEST_ACTIVITY_STATE));
	if (secondary_exec_control & SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY)
		pr_err("InterruptStatus = %04x\n",
		       vmcs_read16(GUEST_INTR_STATUS));

	pr_err("*** Host State ***\n");
	pr_err("RIP = 0x%016lx  RSP = 0x%016lx\n",
	       vmcs_readl(HOST_RIP), vmcs_readl(HOST_RSP));
	pr_err("CS=%04x SS=%04x DS=%04x ES=%04x FS=%04x GS=%04x TR=%04x\n",
	       vmcs_read16(HOST_CS_SELECTOR), vmcs_read16(HOST_SS_SELECTOR),
	       vmcs_read16(HOST_DS_SELECTOR), vmcs_read16(HOST_ES_SELECTOR),
	       vmcs_read16(HOST_FS_SELECTOR), vmcs_read16(HOST_GS_SELECTOR),
	       vmcs_read16(HOST_TR_SELECTOR));
	pr_err("FSBase=%016lx GSBase=%016lx TRBase=%016lx\n",
	       vmcs_readl(HOST_FS_BASE), vmcs_readl(HOST_GS_BASE),
	       vmcs_readl(HOST_TR_BASE));
	pr_err("GDTBase=%016lx IDTBase=%016lx\n",
	       vmcs_readl(HOST_GDTR_BASE), vmcs_readl(HOST_IDTR_BASE));
	pr_err("CR0=%016lx CR3=%016lx CR4=%016lx\n",
	       vmcs_readl(HOST_CR0), vmcs_readl(HOST_CR3),
	       vmcs_readl(HOST_CR4));
	pr_err("Sysenter RSP=%016lx CS:RIP=%04x:%016lx\n",
	       vmcs_readl(HOST_IA32_SYSENTER_ESP),
	       vmcs_read32(HOST_IA32_SYSENTER_CS),
	       vmcs_readl(HOST_IA32_SYSENTER_EIP));
	if (vmexit_ctl & (VM_EXIT_LOAD_IA32_PAT | VM_EXIT_LOAD_IA32_EFER))
		pr_err("EFER = 0x%016llx  PAT = 0x%016llx\n",
		       vmcs_read64(HOST_IA32_EFER),
		       vmcs_read64(HOST_IA32_PAT));
	if (cpu_has_load_perf_global_ctrl() &&
	    vmexit_ctl & VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL)
		pr_err("PerfGlobCtl = 0x%016llx\n",
		       vmcs_read64(HOST_IA32_PERF_GLOBAL_CTRL));

	pr_err("*** Control State ***\n");
	pr_err("PinBased=%08x CPUBased=%08x SecondaryExec=%08x\n",
	       pin_based_exec_ctrl, cpu_based_exec_ctrl, secondary_exec_control);
	pr_err("EntryControls=%08x ExitControls=%08x\n", vmentry_ctl, vmexit_ctl);
	pr_err("ExceptionBitmap=%08x PFECmask=%08x PFECmatch=%08x\n",
	       vmcs_read32(EXCEPTION_BITMAP),
	       vmcs_read32(PAGE_FAULT_ERROR_CODE_MASK),
	       vmcs_read32(PAGE_FAULT_ERROR_CODE_MATCH));
	pr_err("VMEntry: intr_info=%08x errcode=%08x ilen=%08x\n",
	       vmcs_read32(VM_ENTRY_INTR_INFO_FIELD),
	       vmcs_read32(VM_ENTRY_EXCEPTION_ERROR_CODE),
	       vmcs_read32(VM_ENTRY_INSTRUCTION_LEN));
	pr_err("VMExit: intr_info=%08x errcode=%08x ilen=%08x\n",
	       vmcs_read32(VM_EXIT_INTR_INFO),
	       vmcs_read32(VM_EXIT_INTR_ERROR_CODE),
	       vmcs_read32(VM_EXIT_INSTRUCTION_LEN));
	pr_err("        reason=%08x qualification=%016lx\n",
	       vmcs_read32(VM_EXIT_REASON), vmcs_readl(EXIT_QUALIFICATION));
	pr_err("IDTVectoring: info=%08x errcode=%08x\n",
	       vmcs_read32(IDT_VECTORING_INFO_FIELD),
	       vmcs_read32(IDT_VECTORING_ERROR_CODE));
	pr_err("TSC Offset = 0x%016llx\n", vmcs_read64(TSC_OFFSET));
	if (secondary_exec_control & SECONDARY_EXEC_TSC_SCALING)
		pr_err("TSC Multiplier = 0x%016llx\n",
		       vmcs_read64(TSC_MULTIPLIER));
	if (cpu_based_exec_ctrl & CPU_BASED_TPR_SHADOW) {
		if (secondary_exec_control & SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY) {
			u16 status = vmcs_read16(GUEST_INTR_STATUS);
			pr_err("SVI|RVI = %02x|%02x ", status >> 8, status & 0xff);
		}
		pr_cont("TPR Threshold = 0x%02x\n", vmcs_read32(TPR_THRESHOLD));
		if (secondary_exec_control & SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES)
			pr_err("APIC-access addr = 0x%016llx ", vmcs_read64(APIC_ACCESS_ADDR));
		pr_cont("virt-APIC addr = 0x%016llx\n", vmcs_read64(VIRTUAL_APIC_PAGE_ADDR));
	}
	if (pin_based_exec_ctrl & PIN_BASED_POSTED_INTR)
		pr_err("PostedIntrVec = 0x%02x\n", vmcs_read16(POSTED_INTR_NV));
	if ((secondary_exec_control & SECONDARY_EXEC_ENABLE_EPT))
		pr_err("EPT pointer = 0x%016llx\n", vmcs_read64(EPT_POINTER));
	n = vmcs_read32(CR3_TARGET_COUNT);
	for (i = 0; i + 1 < n; i += 4)
		pr_err("CR3 target%u=%016lx target%u=%016lx\n",
		       i, vmcs_readl(CR3_TARGET_VALUE0 + i * 2),
		       i + 1, vmcs_readl(CR3_TARGET_VALUE0 + i * 2 + 2));
	if (i < n)
		pr_err("CR3 target%u=%016lx\n",
		       i, vmcs_readl(CR3_TARGET_VALUE0 + i * 2));
	if (secondary_exec_control & SECONDARY_EXEC_PAUSE_LOOP_EXITING)
		pr_err("PLE Gap=%08x Window=%08x\n",
		       vmcs_read32(PLE_GAP), vmcs_read32(PLE_WINDOW));
	if (secondary_exec_control & SECONDARY_EXEC_ENABLE_VPID)
		pr_err("Virtual processor ID = 0x%04x\n",
		       vmcs_read16(VIRTUAL_PROCESSOR_ID));
}

/*
 * The guest has exited.  See if we can fix it or if we need userspace
 * assistance.
 */
static int vmx_handle_exit(struct kvm_vcpu *vcpu,
	enum exit_fastpath_completion exit_fastpath)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u32 exit_reason = vmx->exit_reason;
	u32 vectoring_info = vmx->idt_vectoring_info;

	trace_kvm_exit(exit_reason, vcpu, KVM_ISA_VMX);

	/*
	 * Flush logged GPAs PML buffer, this will make dirty_bitmap more
	 * updated. Another good is, in kvm_vm_ioctl_get_dirty_log, before
	 * querying dirty_bitmap, we only need to kick all vcpus out of guest
	 * mode as if vcpus is in root mode, the PML buffer must has been
	 * flushed already.
	 */
	if (enable_pml)
		vmx_flush_pml_buffer(vcpu);

	/* If guest state is invalid, start emulating */
	if (vmx->emulation_required)
		return handle_invalid_guest_state(vcpu);

	if (is_guest_mode(vcpu) && nested_vmx_exit_reflected(vcpu, exit_reason))
		return nested_vmx_reflect_vmexit(vcpu, exit_reason);

	if (exit_reason & VMX_EXIT_REASONS_FAILED_VMENTRY) {
		dump_vmcs();
		vcpu->run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		vcpu->run->fail_entry.hardware_entry_failure_reason
			= exit_reason;
		return 0;
	}

	if (unlikely(vmx->fail)) {
		dump_vmcs();
		vcpu->run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		vcpu->run->fail_entry.hardware_entry_failure_reason
			= vmcs_read32(VM_INSTRUCTION_ERROR);
		return 0;
	}

	/*
	 * Note:
	 * Do not try to fix EXIT_REASON_EPT_MISCONFIG if it caused by
	 * delivery event since it indicates guest is accessing MMIO.
	 * The vm-exit can be triggered again after return to guest that
	 * will cause infinite loop.
	 */
	if ((vectoring_info & VECTORING_INFO_VALID_MASK) &&
			(exit_reason != EXIT_REASON_EXCEPTION_NMI &&
			exit_reason != EXIT_REASON_EPT_VIOLATION &&
			exit_reason != EXIT_REASON_PML_FULL &&
			exit_reason != EXIT_REASON_TASK_SWITCH)) {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror = KVM_INTERNAL_ERROR_DELIVERY_EV;
		vcpu->run->internal.ndata = 3;
		vcpu->run->internal.data[0] = vectoring_info;
		vcpu->run->internal.data[1] = exit_reason;
		vcpu->run->internal.data[2] = vcpu->arch.exit_qualification;
		if (exit_reason == EXIT_REASON_EPT_MISCONFIG) {
			vcpu->run->internal.ndata++;
			vcpu->run->internal.data[3] =
				vmcs_read64(GUEST_PHYSICAL_ADDRESS);
		}
		return 0;
	}

	if (unlikely(!enable_vnmi &&
		     vmx->loaded_vmcs->soft_vnmi_blocked)) {
		if (vmx_interrupt_allowed(vcpu)) {
			vmx->loaded_vmcs->soft_vnmi_blocked = 0;
		} else if (vmx->loaded_vmcs->vnmi_blocked_time > 1000000000LL &&
			   vcpu->arch.nmi_pending) {
			/*
			 * This CPU don't support us in finding the end of an
			 * NMI-blocked window if the guest runs with IRQs
			 * disabled. So we pull the trigger after 1 s of
			 * futile waiting, but inform the user about this.
			 */
			printk(KERN_WARNING "%s: Breaking out of NMI-blocked "
			       "state on VCPU %d after 1 s timeout\n",
			       __func__, vcpu->vcpu_id);
			vmx->loaded_vmcs->soft_vnmi_blocked = 0;
		}
	}

	if (exit_fastpath == EXIT_FASTPATH_SKIP_EMUL_INS) {
		kvm_skip_emulated_instruction(vcpu);
		return 1;
	}

	if (exit_reason >= kvm_vmx_max_exit_handlers)
		goto unexpected_vmexit;
#ifdef CONFIG_RETPOLINE
	if (exit_reason == EXIT_REASON_MSR_WRITE)
		return kvm_emulate_wrmsr(vcpu);
	else if (exit_reason == EXIT_REASON_PREEMPTION_TIMER)
		return handle_preemption_timer(vcpu);
	else if (exit_reason == EXIT_REASON_INTERRUPT_WINDOW)
		return handle_interrupt_window(vcpu);
	else if (exit_reason == EXIT_REASON_EXTERNAL_INTERRUPT)
		return handle_external_interrupt(vcpu);
	else if (exit_reason == EXIT_REASON_HLT)
		return kvm_emulate_halt(vcpu);
	else if (exit_reason == EXIT_REASON_EPT_MISCONFIG)
		return handle_ept_misconfig(vcpu);
#endif

	exit_reason = array_index_nospec(exit_reason,
					 kvm_vmx_max_exit_handlers);
	if (!kvm_vmx_exit_handlers[exit_reason])
		goto unexpected_vmexit;

	return kvm_vmx_exit_handlers[exit_reason](vcpu);

unexpected_vmexit:
	vcpu_unimpl(vcpu, "vmx: unexpected exit reason 0x%x\n", exit_reason);
	dump_vmcs();
	vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
	vcpu->run->internal.suberror =
			KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON;
	vcpu->run->internal.ndata = 1;
	vcpu->run->internal.data[0] = exit_reason;
	return 0;
}

/*
 * Software based L1D cache flush which is used when microcode providing
 * the cache control MSR is not loaded.
 *
 * The L1D cache is 32 KiB on Nehalem and later microarchitectures, but to
 * flush it is required to read in 64 KiB because the replacement algorithm
 * is not exactly LRU. This could be sized at runtime via topology
 * information but as all relevant affected CPUs have 32KiB L1D cache size
 * there is no point in doing so.
 */
static void vmx_l1d_flush(struct kvm_vcpu *vcpu)
{
	int size = PAGE_SIZE << L1D_CACHE_ORDER;

	/*
	 * This code is only executed when the the flush mode is 'cond' or
	 * 'always'
	 */
	if (static_branch_likely(&vmx_l1d_flush_cond)) {
		bool flush_l1d;

		/*
		 * Clear the per-vcpu flush bit, it gets set again
		 * either from vcpu_run() or from one of the unsafe
		 * VMEXIT handlers.
		 */
		flush_l1d = vcpu->arch.l1tf_flush_l1d;
		vcpu->arch.l1tf_flush_l1d = false;

		/*
		 * Clear the per-cpu flush bit, it gets set again from
		 * the interrupt handlers.
		 */
		flush_l1d |= kvm_get_cpu_l1tf_flush_l1d();
		kvm_clear_cpu_l1tf_flush_l1d();

		if (!flush_l1d)
			return;
	}

	vcpu->stat.l1d_flush++;

	if (static_cpu_has(X86_FEATURE_FLUSH_L1D)) {
		wrmsrl(MSR_IA32_FLUSH_CMD, L1D_FLUSH);
		return;
	}

	asm volatile(
		/* First ensure the pages are in the TLB */
		"xorl	%%eax, %%eax\n"
		".Lpopulate_tlb:\n\t"
		"movzbl	(%[flush_pages], %%" _ASM_AX "), %%ecx\n\t"
		"addl	$4096, %%eax\n\t"
		"cmpl	%%eax, %[size]\n\t"
		"jne	.Lpopulate_tlb\n\t"
		"xorl	%%eax, %%eax\n\t"
		"cpuid\n\t"
		/* Now fill the cache */
		"xorl	%%eax, %%eax\n"
		".Lfill_cache:\n"
		"movzbl	(%[flush_pages], %%" _ASM_AX "), %%ecx\n\t"
		"addl	$64, %%eax\n\t"
		"cmpl	%%eax, %[size]\n\t"
		"jne	.Lfill_cache\n\t"
		"lfence\n"
		:: [flush_pages] "r" (vmx_l1d_flush_pages),
		    [size] "r" (size)
		: "eax", "ebx", "ecx", "edx");
}

static void update_cr8_intercept(struct kvm_vcpu *vcpu, int tpr, int irr)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	int tpr_threshold;

	if (is_guest_mode(vcpu) &&
		nested_cpu_has(vmcs12, CPU_BASED_TPR_SHADOW))
		return;

	tpr_threshold = (irr == -1 || tpr < irr) ? 0 : irr;
	if (is_guest_mode(vcpu))
		to_vmx(vcpu)->nested.l1_tpr_threshold = tpr_threshold;
	else
		vmcs_write32(TPR_THRESHOLD, tpr_threshold);
}

void vmx_set_virtual_apic_mode(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u32 sec_exec_control;

	if (!lapic_in_kernel(vcpu))
		return;

	if (!flexpriority_enabled &&
	    !cpu_has_vmx_virtualize_x2apic_mode())
		return;

	/* Postpone execution until vmcs01 is the current VMCS. */
	if (is_guest_mode(vcpu)) {
		vmx->nested.change_vmcs01_virtual_apic_mode = true;
		return;
	}

	sec_exec_control = secondary_exec_controls_get(vmx);
	sec_exec_control &= ~(SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
			      SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE);

	switch (kvm_get_apic_mode(vcpu)) {
	case LAPIC_MODE_INVALID:
		WARN_ONCE(true, "Invalid local APIC state");
	case LAPIC_MODE_DISABLED:
		break;
	case LAPIC_MODE_XAPIC:
		if (flexpriority_enabled) {
			sec_exec_control |=
				SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES;
			vmx_flush_tlb(vcpu, true);
		}
		break;
	case LAPIC_MODE_X2APIC:
		if (cpu_has_vmx_virtualize_x2apic_mode())
			sec_exec_control |=
				SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE;
		break;
	}
	secondary_exec_controls_set(vmx, sec_exec_control);

	vmx_update_msr_bitmap(vcpu);
}

static void vmx_set_apic_access_page_addr(struct kvm_vcpu *vcpu, hpa_t hpa)
{
	if (!is_guest_mode(vcpu)) {
		vmcs_write64(APIC_ACCESS_ADDR, hpa);
		vmx_flush_tlb(vcpu, true);
	}
}

static void vmx_hwapic_isr_update(struct kvm_vcpu *vcpu, int max_isr)
{
	u16 status;
	u8 old;

	if (max_isr == -1)
		max_isr = 0;

	status = vmcs_read16(GUEST_INTR_STATUS);
	old = status >> 8;
	if (max_isr != old) {
		status &= 0xff;
		status |= max_isr << 8;
		vmcs_write16(GUEST_INTR_STATUS, status);
	}
}

static void vmx_set_rvi(int vector)
{
	u16 status;
	u8 old;

	if (vector == -1)
		vector = 0;

	status = vmcs_read16(GUEST_INTR_STATUS);
	old = (u8)status & 0xff;
	if ((u8)vector != old) {
		status &= ~0xff;
		status |= (u8)vector;
		vmcs_write16(GUEST_INTR_STATUS, status);
	}
}

static void vmx_hwapic_irr_update(struct kvm_vcpu *vcpu, int max_irr)
{
	/*
	 * When running L2, updating RVI is only relevant when
	 * vmcs12 virtual-interrupt-delivery enabled.
	 * However, it can be enabled only when L1 also
	 * intercepts external-interrupts and in that case
	 * we should not update vmcs02 RVI but instead intercept
	 * interrupt. Therefore, do nothing when running L2.
	 */
	if (!is_guest_mode(vcpu))
		vmx_set_rvi(max_irr);
}

static int vmx_sync_pir_to_irr(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	int max_irr;
	bool max_irr_updated;

	WARN_ON(!vcpu->arch.apicv_active);
	if (pi_test_on(&vmx->pi_desc)) {
		pi_clear_on(&vmx->pi_desc);
		/*
		 * IOMMU can write to PID.ON, so the barrier matters even on UP.
		 * But on x86 this is just a compiler barrier anyway.
		 */
		smp_mb__after_atomic();
		max_irr_updated =
			kvm_apic_update_irr(vcpu, vmx->pi_desc.pir, &max_irr);

		/*
		 * If we are running L2 and L1 has a new pending interrupt
		 * which can be injected, we should re-evaluate
		 * what should be done with this new L1 interrupt.
		 * If L1 intercepts external-interrupts, we should
		 * exit from L2 to L1. Otherwise, interrupt should be
		 * delivered directly to L2.
		 */
		if (is_guest_mode(vcpu) && max_irr_updated) {
			if (nested_exit_on_intr(vcpu))
				kvm_vcpu_exiting_guest_mode(vcpu);
			else
				kvm_make_request(KVM_REQ_EVENT, vcpu);
		}
	} else {
		max_irr = kvm_lapic_find_highest_irr(vcpu);
	}
	vmx_hwapic_irr_update(vcpu, max_irr);
	return max_irr;
}

static bool vmx_dy_apicv_has_pending_interrupt(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);

	return pi_test_on(pi_desc) ||
		(pi_test_sn(pi_desc) && !pi_is_pir_empty(pi_desc));
}

static void vmx_load_eoi_exitmap(struct kvm_vcpu *vcpu, u64 *eoi_exit_bitmap)
{
	if (!kvm_vcpu_apicv_active(vcpu))
		return;

	vmcs_write64(EOI_EXIT_BITMAP0, eoi_exit_bitmap[0]);
	vmcs_write64(EOI_EXIT_BITMAP1, eoi_exit_bitmap[1]);
	vmcs_write64(EOI_EXIT_BITMAP2, eoi_exit_bitmap[2]);
	vmcs_write64(EOI_EXIT_BITMAP3, eoi_exit_bitmap[3]);
}

static void vmx_apicv_post_state_restore(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	pi_clear_on(&vmx->pi_desc);
	memset(vmx->pi_desc.pir, 0, sizeof(vmx->pi_desc.pir));
}

static void handle_exception_nmi_irqoff(struct vcpu_vmx *vmx)
{
	vmx->exit_intr_info = vmcs_read32(VM_EXIT_INTR_INFO);

	/* if exit due to PF check for async PF */
	if (is_page_fault(vmx->exit_intr_info))
		vmx->vcpu.arch.apf.host_apf_reason = kvm_read_and_reset_pf_reason();

	/* Handle machine checks before interrupts are enabled */
	if (is_machine_check(vmx->exit_intr_info))
		kvm_machine_check();

	/* We need to handle NMIs before interrupts are enabled */
	if (is_nmi(vmx->exit_intr_info)) {
		kvm_before_interrupt(&vmx->vcpu);
		asm("int $2");
		kvm_after_interrupt(&vmx->vcpu);
	}
}

static void handle_external_interrupt_irqoff(struct kvm_vcpu *vcpu)
{
	unsigned int vector;
	unsigned long entry;
#ifdef CONFIG_X86_64
	unsigned long tmp;
#endif
	gate_desc *desc;
	u32 intr_info;

	intr_info = vmcs_read32(VM_EXIT_INTR_INFO);
	if (WARN_ONCE(!is_external_intr(intr_info),
	    "KVM: unexpected VM-Exit interrupt info: 0x%x", intr_info))
		return;

	vector = intr_info & INTR_INFO_VECTOR_MASK;
	desc = (gate_desc *)host_idt_base + vector;
	entry = gate_offset(desc);

	kvm_before_interrupt(vcpu);

	asm volatile(
#ifdef CONFIG_X86_64
		"mov %%" _ASM_SP ", %[sp]\n\t"
		"and $0xfffffffffffffff0, %%" _ASM_SP "\n\t"
		"push $%c[ss]\n\t"
		"push %[sp]\n\t"
#endif
		"pushf\n\t"
		__ASM_SIZE(push) " $%c[cs]\n\t"
		CALL_NOSPEC
		:
#ifdef CONFIG_X86_64
		[sp]"=&r"(tmp),
#endif
		ASM_CALL_CONSTRAINT
		:
		THUNK_TARGET(entry),
		[ss]"i"(__KERNEL_DS),
		[cs]"i"(__KERNEL_CS)
	);

	kvm_after_interrupt(vcpu);
}
STACK_FRAME_NON_STANDARD(handle_external_interrupt_irqoff);

static void vmx_handle_exit_irqoff(struct kvm_vcpu *vcpu,
	enum exit_fastpath_completion *exit_fastpath)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (vmx->exit_reason == EXIT_REASON_EXTERNAL_INTERRUPT)
		handle_external_interrupt_irqoff(vcpu);
	else if (vmx->exit_reason == EXIT_REASON_EXCEPTION_NMI)
		handle_exception_nmi_irqoff(vmx);
	else if (!is_guest_mode(vcpu) &&
		vmx->exit_reason == EXIT_REASON_MSR_WRITE)
		*exit_fastpath = handle_fastpath_set_msr_irqoff(vcpu);
}

static bool vmx_has_emulated_msr(int index)
{
	switch (index) {
	case MSR_IA32_SMBASE:
		/*
		 * We cannot do SMM unless we can run the guest in big
		 * real mode.
		 */
		return enable_unrestricted_guest || emulate_invalid_guest_state;
	case MSR_IA32_VMX_BASIC ... MSR_IA32_VMX_VMFUNC:
		return nested;
	case MSR_AMD64_VIRT_SPEC_CTRL:
		/* This is AMD only.  */
		return false;
	default:
		return true;
	}
}

static bool vmx_pt_supported(void)
{
	return pt_mode == PT_MODE_HOST_GUEST;
}

static void vmx_recover_nmi_blocking(struct vcpu_vmx *vmx)
{
	u32 exit_intr_info;
	bool unblock_nmi;
	u8 vector;
	bool idtv_info_valid;

	idtv_info_valid = vmx->idt_vectoring_info & VECTORING_INFO_VALID_MASK;

	if (enable_vnmi) {
		if (vmx->loaded_vmcs->nmi_known_unmasked)
			return;
		/*
		 * Can't use vmx->exit_intr_info since we're not sure what
		 * the exit reason is.
		 */
		exit_intr_info = vmcs_read32(VM_EXIT_INTR_INFO);
		unblock_nmi = (exit_intr_info & INTR_INFO_UNBLOCK_NMI) != 0;
		vector = exit_intr_info & INTR_INFO_VECTOR_MASK;
		/*
		 * SDM 3: 27.7.1.2 (September 2008)
		 * Re-set bit "block by NMI" before VM entry if vmexit caused by
		 * a guest IRET fault.
		 * SDM 3: 23.2.2 (September 2008)
		 * Bit 12 is undefined in any of the following cases:
		 *  If the VM exit sets the valid bit in the IDT-vectoring
		 *   information field.
		 *  If the VM exit is due to a double fault.
		 */
		if ((exit_intr_info & INTR_INFO_VALID_MASK) && unblock_nmi &&
		    vector != DF_VECTOR && !idtv_info_valid)
			vmcs_set_bits(GUEST_INTERRUPTIBILITY_INFO,
				      GUEST_INTR_STATE_NMI);
		else
			vmx->loaded_vmcs->nmi_known_unmasked =
				!(vmcs_read32(GUEST_INTERRUPTIBILITY_INFO)
				  & GUEST_INTR_STATE_NMI);
	} else if (unlikely(vmx->loaded_vmcs->soft_vnmi_blocked))
		vmx->loaded_vmcs->vnmi_blocked_time +=
			ktime_to_ns(ktime_sub(ktime_get(),
					      vmx->loaded_vmcs->entry_time));
}

static void __vmx_complete_interrupts(struct kvm_vcpu *vcpu,
				      u32 idt_vectoring_info,
				      int instr_len_field,
				      int error_code_field)
{
	u8 vector;
	int type;
	bool idtv_info_valid;

	idtv_info_valid = idt_vectoring_info & VECTORING_INFO_VALID_MASK;

	vcpu->arch.nmi_injected = false;
	kvm_clear_exception_queue(vcpu);
	kvm_clear_interrupt_queue(vcpu);

	if (!idtv_info_valid)
		return;

	kvm_make_request(KVM_REQ_EVENT, vcpu);

	vector = idt_vectoring_info & VECTORING_INFO_VECTOR_MASK;
	type = idt_vectoring_info & VECTORING_INFO_TYPE_MASK;

	switch (type) {
	case INTR_TYPE_NMI_INTR:
		vcpu->arch.nmi_injected = true;
		/*
		 * SDM 3: 27.7.1.2 (September 2008)
		 * Clear bit "block by NMI" before VM entry if a NMI
		 * delivery faulted.
		 */
		vmx_set_nmi_mask(vcpu, false);
		break;
	case INTR_TYPE_SOFT_EXCEPTION:
		vcpu->arch.event_exit_inst_len = vmcs_read32(instr_len_field);
		/* fall through */
	case INTR_TYPE_HARD_EXCEPTION:
		if (idt_vectoring_info & VECTORING_INFO_DELIVER_CODE_MASK) {
			u32 err = vmcs_read32(error_code_field);
			kvm_requeue_exception_e(vcpu, vector, err);
		} else
			kvm_requeue_exception(vcpu, vector);
		break;
	case INTR_TYPE_SOFT_INTR:
		vcpu->arch.event_exit_inst_len = vmcs_read32(instr_len_field);
		/* fall through */
	case INTR_TYPE_EXT_INTR:
		kvm_queue_interrupt(vcpu, vector, type == INTR_TYPE_SOFT_INTR);
		break;
	default:
		break;
	}
}

static void vmx_complete_interrupts(struct vcpu_vmx *vmx)
{
	__vmx_complete_interrupts(&vmx->vcpu, vmx->idt_vectoring_info,
				  VM_EXIT_INSTRUCTION_LEN,
				  IDT_VECTORING_ERROR_CODE);
}

static void vmx_cancel_injection(struct kvm_vcpu *vcpu)
{
	__vmx_complete_interrupts(vcpu,
				  vmcs_read32(VM_ENTRY_INTR_INFO_FIELD),
				  VM_ENTRY_INSTRUCTION_LEN,
				  VM_ENTRY_EXCEPTION_ERROR_CODE);

	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, 0);
}

static void atomic_switch_perf_msrs(struct vcpu_vmx *vmx)
{
	int i, nr_msrs;
	struct perf_guest_switch_msr *msrs;

	msrs = perf_guest_get_msrs(&nr_msrs);

	if (!msrs)
		return;

	for (i = 0; i < nr_msrs; i++)
		if (msrs[i].host == msrs[i].guest)
			clear_atomic_switch_msr(vmx, msrs[i].msr);
		else
			add_atomic_switch_msr(vmx, msrs[i].msr, msrs[i].guest,
					msrs[i].host, false);
}

static void atomic_switch_umwait_control_msr(struct vcpu_vmx *vmx)
{
	u32 host_umwait_control;

	if (!vmx_has_waitpkg(vmx))
		return;

	host_umwait_control = get_umwait_control_msr();

	if (vmx->msr_ia32_umwait_control != host_umwait_control)
		add_atomic_switch_msr(vmx, MSR_IA32_UMWAIT_CONTROL,
			vmx->msr_ia32_umwait_control,
			host_umwait_control, false);
	else
		clear_atomic_switch_msr(vmx, MSR_IA32_UMWAIT_CONTROL);
}

static void vmx_update_hv_timer(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u64 tscl;
	u32 delta_tsc;

	if (vmx->req_immediate_exit) {
		vmcs_write32(VMX_PREEMPTION_TIMER_VALUE, 0);
		vmx->loaded_vmcs->hv_timer_soft_disabled = false;
	} else if (vmx->hv_deadline_tsc != -1) {
		tscl = rdtsc();
		if (vmx->hv_deadline_tsc > tscl)
			/* set_hv_timer ensures the delta fits in 32-bits */
			delta_tsc = (u32)((vmx->hv_deadline_tsc - tscl) >>
				cpu_preemption_timer_multi);
		else
			delta_tsc = 0;

		vmcs_write32(VMX_PREEMPTION_TIMER_VALUE, delta_tsc);
		vmx->loaded_vmcs->hv_timer_soft_disabled = false;
	} else if (!vmx->loaded_vmcs->hv_timer_soft_disabled) {
		vmcs_write32(VMX_PREEMPTION_TIMER_VALUE, -1);
		vmx->loaded_vmcs->hv_timer_soft_disabled = true;
	}
}

void vmx_update_host_rsp(struct vcpu_vmx *vmx, unsigned long host_rsp)
{
	if (unlikely(host_rsp != vmx->loaded_vmcs->host_state.rsp)) {
		vmx->loaded_vmcs->host_state.rsp = host_rsp;
		vmcs_writel(HOST_RSP, host_rsp);
	}
}

bool __vmx_vcpu_run(struct vcpu_vmx *vmx, unsigned long *regs, bool launched);

static void vmx_vcpu_run(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long cr3, cr4;

	/* Record the guest's net vcpu time for enforced NMI injections. */
	if (unlikely(!enable_vnmi &&
		     vmx->loaded_vmcs->soft_vnmi_blocked))
		vmx->loaded_vmcs->entry_time = ktime_get();

	/* Don't enter VMX if guest state is invalid, let the exit handler
	   start emulation until we arrive back to a valid state */
	if (vmx->emulation_required)
		return;

	if (vmx->ple_window_dirty) {
		vmx->ple_window_dirty = false;
		vmcs_write32(PLE_WINDOW, vmx->ple_window);
	}

	/*
	 * We did this in prepare_switch_to_guest, because it needs to
	 * be within srcu_read_lock.
	 */
	WARN_ON_ONCE(vmx->nested.need_vmcs12_to_shadow_sync);

	if (kvm_register_is_dirty(vcpu, VCPU_REGS_RSP))
		vmcs_writel(GUEST_RSP, vcpu->arch.regs[VCPU_REGS_RSP]);
	if (kvm_register_is_dirty(vcpu, VCPU_REGS_RIP))
		vmcs_writel(GUEST_RIP, vcpu->arch.regs[VCPU_REGS_RIP]);

	cr3 = __get_current_cr3_fast();
	if (unlikely(cr3 != vmx->loaded_vmcs->host_state.cr3)) {
		vmcs_writel(HOST_CR3, cr3);
		vmx->loaded_vmcs->host_state.cr3 = cr3;
	}

	cr4 = cr4_read_shadow();
	if (unlikely(cr4 != vmx->loaded_vmcs->host_state.cr4)) {
		vmcs_writel(HOST_CR4, cr4);
		vmx->loaded_vmcs->host_state.cr4 = cr4;
	}

	/* When single-stepping over STI and MOV SS, we must clear the
	 * corresponding interruptibility bits in the guest state. Otherwise
	 * vmentry fails as it then expects bit 14 (BS) in pending debug
	 * exceptions being set, but that's not correct for the guest debugging
	 * case. */
	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)
		vmx_set_interrupt_shadow(vcpu, 0);

	kvm_load_guest_xsave_state(vcpu);

	if (static_cpu_has(X86_FEATURE_PKU) &&
	    kvm_read_cr4_bits(vcpu, X86_CR4_PKE) &&
	    vcpu->arch.pkru != vmx->host_pkru)
		__write_pkru(vcpu->arch.pkru);

	pt_guest_enter(vmx);

	atomic_switch_perf_msrs(vmx);
	atomic_switch_umwait_control_msr(vmx);

	if (enable_preemption_timer)
		vmx_update_hv_timer(vcpu);

	if (lapic_in_kernel(vcpu) &&
		vcpu->arch.apic->lapic_timer.timer_advance_ns)
		kvm_wait_lapic_expire(vcpu);

	/*
	 * If this vCPU has touched SPEC_CTRL, restore the guest's value if
	 * it's non-zero. Since vmentry is serialising on affected CPUs, there
	 * is no need to worry about the conditional branch over the wrmsr
	 * being speculatively taken.
	 */
	x86_spec_ctrl_set_guest(vmx->spec_ctrl, 0);

	/* L1D Flush includes CPU buffer clear to mitigate MDS */
	if (static_branch_unlikely(&vmx_l1d_should_flush))
		vmx_l1d_flush(vcpu);
	else if (static_branch_unlikely(&mds_user_clear))
		mds_clear_cpu_buffers();

	if (vcpu->arch.cr2 != read_cr2())
		write_cr2(vcpu->arch.cr2);

	vmx->fail = __vmx_vcpu_run(vmx, (unsigned long *)&vcpu->arch.regs,
				   vmx->loaded_vmcs->launched);

	vcpu->arch.cr2 = read_cr2();

	/*
	 * We do not use IBRS in the kernel. If this vCPU has used the
	 * SPEC_CTRL MSR it may have left it on; save the value and
	 * turn it off. This is much more efficient than blindly adding
	 * it to the atomic save/restore list. Especially as the former
	 * (Saving guest MSRs on vmexit) doesn't even exist in KVM.
	 *
	 * For non-nested case:
	 * If the L01 MSR bitmap does not intercept the MSR, then we need to
	 * save it.
	 *
	 * For nested case:
	 * If the L02 MSR bitmap does not intercept the MSR, then we need to
	 * save it.
	 */
	if (unlikely(!msr_write_intercepted(vcpu, MSR_IA32_SPEC_CTRL)))
		vmx->spec_ctrl = native_read_msr(MSR_IA32_SPEC_CTRL);

	x86_spec_ctrl_restore_host(vmx->spec_ctrl, 0);

	/* All fields are clean at this point */
	if (static_branch_unlikely(&enable_evmcs))
		current_evmcs->hv_clean_fields |=
			HV_VMX_ENLIGHTENED_CLEAN_FIELD_ALL;

	if (static_branch_unlikely(&enable_evmcs))
		current_evmcs->hv_vp_id = vcpu->arch.hyperv.vp_index;

	/* MSR_IA32_DEBUGCTLMSR is zeroed on vmexit. Restore it if needed */
	if (vmx->host_debugctlmsr)
		update_debugctlmsr(vmx->host_debugctlmsr);

#ifndef CONFIG_X86_64
	/*
	 * The sysexit path does not restore ds/es, so we must set them to
	 * a reasonable value ourselves.
	 *
	 * We can't defer this to vmx_prepare_switch_to_host() since that
	 * function may be executed in interrupt context, which saves and
	 * restore segments around it, nullifying its effect.
	 */
	loadsegment(ds, __USER_DS);
	loadsegment(es, __USER_DS);
#endif

	vcpu->arch.regs_avail = ~((1 << VCPU_REGS_RIP) | (1 << VCPU_REGS_RSP)
				  | (1 << VCPU_EXREG_RFLAGS)
				  | (1 << VCPU_EXREG_PDPTR)
				  | (1 << VCPU_EXREG_SEGMENTS)
				  | (1 << VCPU_EXREG_CR3));
	vcpu->arch.regs_dirty = 0;

	pt_guest_exit(vmx);

	/*
	 * eager fpu is enabled if PKEY is supported and CR4 is switched
	 * back on host, so it is safe to read guest PKRU from current
	 * XSAVE.
	 */
	if (static_cpu_has(X86_FEATURE_PKU) &&
	    kvm_read_cr4_bits(vcpu, X86_CR4_PKE)) {
		vcpu->arch.pkru = rdpkru();
		if (vcpu->arch.pkru != vmx->host_pkru)
			__write_pkru(vmx->host_pkru);
	}

	kvm_load_host_xsave_state(vcpu);

	vmx->nested.nested_run_pending = 0;
	vmx->idt_vectoring_info = 0;

	vmx->exit_reason = vmx->fail ? 0xdead : vmcs_read32(VM_EXIT_REASON);
	if ((u16)vmx->exit_reason == EXIT_REASON_MCE_DURING_VMENTRY)
		kvm_machine_check();

	if (vmx->fail || (vmx->exit_reason & VMX_EXIT_REASONS_FAILED_VMENTRY))
		return;

	vmx->loaded_vmcs->launched = 1;
	vmx->idt_vectoring_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);

	vmx_recover_nmi_blocking(vmx);
	vmx_complete_interrupts(vmx);
}

static struct kvm *vmx_vm_alloc(void)
{
	struct kvm_vmx *kvm_vmx = __vmalloc(sizeof(struct kvm_vmx),
					    GFP_KERNEL_ACCOUNT | __GFP_ZERO,
					    PAGE_KERNEL);
	return &kvm_vmx->kvm;
}

static void vmx_vm_free(struct kvm *kvm)
{
	kfree(kvm->arch.hyperv.hv_pa_pg);
	vfree(to_kvm_vmx(kvm));
}

static void vmx_free_vcpu(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (enable_pml)
		vmx_destroy_pml_buffer(vmx);
	free_vpid(vmx->vpid);
	nested_vmx_free_vcpu(vcpu);
	free_loaded_vmcs(vmx->loaded_vmcs);
}

static int vmx_create_vcpu(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx;
	unsigned long *msr_bitmap;
	int i, cpu, err;

	BUILD_BUG_ON(offsetof(struct vcpu_vmx, vcpu) != 0);
	vmx = to_vmx(vcpu);

	err = -ENOMEM;

	vmx->vpid = allocate_vpid();

	/*
	 * If PML is turned on, failure on enabling PML just results in failure
	 * of creating the vcpu, therefore we can simplify PML logic (by
	 * avoiding dealing with cases, such as enabling PML partially on vcpus
	 * for the guest), etc.
	 */
	if (enable_pml) {
		vmx->pml_pg = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
		if (!vmx->pml_pg)
			goto free_vpid;
	}

	BUILD_BUG_ON(ARRAY_SIZE(vmx_msr_index) != NR_SHARED_MSRS);

	for (i = 0; i < ARRAY_SIZE(vmx_msr_index); ++i) {
		u32 index = vmx_msr_index[i];
		u32 data_low, data_high;
		int j = vmx->nmsrs;

		if (rdmsr_safe(index, &data_low, &data_high) < 0)
			continue;
		if (wrmsr_safe(index, data_low, data_high) < 0)
			continue;

		vmx->guest_msrs[j].index = i;
		vmx->guest_msrs[j].data = 0;
		switch (index) {
		case MSR_IA32_TSX_CTRL:
			/*
			 * No need to pass TSX_CTRL_CPUID_CLEAR through, so
			 * let's avoid changing CPUID bits under the host
			 * kernel's feet.
			 */
			vmx->guest_msrs[j].mask = ~(u64)TSX_CTRL_CPUID_CLEAR;
			break;
		default:
			vmx->guest_msrs[j].mask = -1ull;
			break;
		}
		++vmx->nmsrs;
	}

	err = alloc_loaded_vmcs(&vmx->vmcs01);
	if (err < 0)
		goto free_pml;

	msr_bitmap = vmx->vmcs01.msr_bitmap;
	vmx_disable_intercept_for_msr(msr_bitmap, MSR_IA32_TSC, MSR_TYPE_R);
	vmx_disable_intercept_for_msr(msr_bitmap, MSR_FS_BASE, MSR_TYPE_RW);
	vmx_disable_intercept_for_msr(msr_bitmap, MSR_GS_BASE, MSR_TYPE_RW);
	vmx_disable_intercept_for_msr(msr_bitmap, MSR_KERNEL_GS_BASE, MSR_TYPE_RW);
	vmx_disable_intercept_for_msr(msr_bitmap, MSR_IA32_SYSENTER_CS, MSR_TYPE_RW);
	vmx_disable_intercept_for_msr(msr_bitmap, MSR_IA32_SYSENTER_ESP, MSR_TYPE_RW);
	vmx_disable_intercept_for_msr(msr_bitmap, MSR_IA32_SYSENTER_EIP, MSR_TYPE_RW);
	if (kvm_cstate_in_guest(vcpu->kvm)) {
		vmx_disable_intercept_for_msr(msr_bitmap, MSR_CORE_C1_RES, MSR_TYPE_R);
		vmx_disable_intercept_for_msr(msr_bitmap, MSR_CORE_C3_RESIDENCY, MSR_TYPE_R);
		vmx_disable_intercept_for_msr(msr_bitmap, MSR_CORE_C6_RESIDENCY, MSR_TYPE_R);
		vmx_disable_intercept_for_msr(msr_bitmap, MSR_CORE_C7_RESIDENCY, MSR_TYPE_R);
	}
	vmx->msr_bitmap_mode = 0;

	vmx->loaded_vmcs = &vmx->vmcs01;
	cpu = get_cpu();
	vmx_vcpu_load(vcpu, cpu);
	vcpu->cpu = cpu;
	init_vmcs(vmx);
	vmx_vcpu_put(vcpu);
	put_cpu();
	if (cpu_need_virtualize_apic_accesses(vcpu)) {
		err = alloc_apic_access_page(vcpu->kvm);
		if (err)
			goto free_vmcs;
	}

	if (enable_ept && !enable_unrestricted_guest) {
		err = init_rmode_identity_map(vcpu->kvm);
		if (err)
			goto free_vmcs;
	}

	if (nested)
		nested_vmx_setup_ctls_msrs(&vmx->nested.msrs,
					   vmx_capability.ept);
	else
		memset(&vmx->nested.msrs, 0, sizeof(vmx->nested.msrs));

	vmx->nested.posted_intr_nv = -1;
	vmx->nested.current_vmptr = -1ull;

	vcpu->arch.microcode_version = 0x100000000ULL;
	vmx->msr_ia32_feature_control_valid_bits = FEAT_CTL_LOCKED;

	/*
	 * Enforce invariant: pi_desc.nv is always either POSTED_INTR_VECTOR
	 * or POSTED_INTR_WAKEUP_VECTOR.
	 */
	vmx->pi_desc.nv = POSTED_INTR_VECTOR;
	vmx->pi_desc.sn = 1;

	vmx->ept_pointer = INVALID_PAGE;

	return 0;

free_vmcs:
	free_loaded_vmcs(vmx->loaded_vmcs);
free_pml:
	vmx_destroy_pml_buffer(vmx);
free_vpid:
	free_vpid(vmx->vpid);
	return err;
}

#define L1TF_MSG_SMT "L1TF CPU bug present and SMT on, data leak possible. See CVE-2018-3646 and https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/l1tf.html for details.\n"
#define L1TF_MSG_L1D "L1TF CPU bug present and virtualization mitigation disabled, data leak possible. See CVE-2018-3646 and https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/l1tf.html for details.\n"

static int vmx_vm_init(struct kvm *kvm)
{
	spin_lock_init(&to_kvm_vmx(kvm)->ept_pointer_lock);

	if (!ple_gap)
		kvm->arch.pause_in_guest = true;

	if (boot_cpu_has(X86_BUG_L1TF) && enable_ept) {
		switch (l1tf_mitigation) {
		case L1TF_MITIGATION_OFF:
		case L1TF_MITIGATION_FLUSH_NOWARN:
			/* 'I explicitly don't care' is set */
			break;
		case L1TF_MITIGATION_FLUSH:
		case L1TF_MITIGATION_FLUSH_NOSMT:
		case L1TF_MITIGATION_FULL:
			/*
			 * Warn upon starting the first VM in a potentially
			 * insecure environment.
			 */
			if (sched_smt_active())
				pr_warn_once(L1TF_MSG_SMT);
			if (l1tf_vmx_mitigation == VMENTER_L1D_FLUSH_NEVER)
				pr_warn_once(L1TF_MSG_L1D);
			break;
		case L1TF_MITIGATION_FULL_FORCE:
			/* Flush is enforced */
			break;
		}
	}
	kvm_apicv_init(kvm, enable_apicv);
	return 0;
}

static int __init vmx_check_processor_compat(void)
{
	struct vmcs_config vmcs_conf;
	struct vmx_capability vmx_cap;

	if (!this_cpu_has(X86_FEATURE_MSR_IA32_FEAT_CTL) ||
	    !this_cpu_has(X86_FEATURE_VMX)) {
		pr_err("kvm: VMX is disabled on CPU %d\n", smp_processor_id());
		return -EIO;
	}

	if (setup_vmcs_config(&vmcs_conf, &vmx_cap) < 0)
		return -EIO;
	if (nested)
		nested_vmx_setup_ctls_msrs(&vmcs_conf.nested, vmx_cap.ept);
	if (memcmp(&vmcs_config, &vmcs_conf, sizeof(struct vmcs_config)) != 0) {
		printk(KERN_ERR "kvm: CPU %d feature inconsistency!\n",
				smp_processor_id());
		return -EIO;
	}
	return 0;
}

static u64 vmx_get_mt_mask(struct kvm_vcpu *vcpu, gfn_t gfn, bool is_mmio)
{
	u8 cache;
	u64 ipat = 0;

	/* For VT-d and EPT combination
	 * 1. MMIO: always map as UC
	 * 2. EPT with VT-d:
	 *   a. VT-d without snooping control feature: can't guarantee the
	 *	result, try to trust guest.
	 *   b. VT-d with snooping control feature: snooping control feature of
	 *	VT-d engine can guarantee the cache correctness. Just set it
	 *	to WB to keep consistent with host. So the same as item 3.
	 * 3. EPT without VT-d: always map as WB and set IPAT=1 to keep
	 *    consistent with host MTRR
	 */
	if (is_mmio) {
		cache = MTRR_TYPE_UNCACHABLE;
		goto exit;
	}

	if (!kvm_arch_has_noncoherent_dma(vcpu->kvm)) {
		ipat = VMX_EPT_IPAT_BIT;
		cache = MTRR_TYPE_WRBACK;
		goto exit;
	}

	if (kvm_read_cr0(vcpu) & X86_CR0_CD) {
		ipat = VMX_EPT_IPAT_BIT;
		if (kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_CD_NW_CLEARED))
			cache = MTRR_TYPE_WRBACK;
		else
			cache = MTRR_TYPE_UNCACHABLE;
		goto exit;
	}

	cache = kvm_mtrr_get_guest_memory_type(vcpu, gfn);

exit:
	return (cache << VMX_EPT_MT_EPTE_SHIFT) | ipat;
}

static int vmx_get_lpage_level(void)
{
	if (enable_ept && !cpu_has_vmx_ept_1g_page())
		return PT_DIRECTORY_LEVEL;
	else
		/* For shadow and EPT supported 1GB page */
		return PT_PDPE_LEVEL;
}

static void vmcs_set_secondary_exec_control(struct vcpu_vmx *vmx)
{
	/*
	 * These bits in the secondary execution controls field
	 * are dynamic, the others are mostly based on the hypervisor
	 * architecture and the guest's CPUID.  Do not touch the
	 * dynamic bits.
	 */
	u32 mask =
		SECONDARY_EXEC_SHADOW_VMCS |
		SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
		SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
		SECONDARY_EXEC_DESC;

	u32 new_ctl = vmx->secondary_exec_control;
	u32 cur_ctl = secondary_exec_controls_get(vmx);

	secondary_exec_controls_set(vmx, (new_ctl & ~mask) | (cur_ctl & mask));
}

/*
 * Generate MSR_IA32_VMX_CR{0,4}_FIXED1 according to CPUID. Only set bits
 * (indicating "allowed-1") if they are supported in the guest's CPUID.
 */
static void nested_vmx_cr_fixed1_bits_update(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct kvm_cpuid_entry2 *entry;

	vmx->nested.msrs.cr0_fixed1 = 0xffffffff;
	vmx->nested.msrs.cr4_fixed1 = X86_CR4_PCE;

#define cr4_fixed1_update(_cr4_mask, _reg, _cpuid_mask) do {		\
	if (entry && (entry->_reg & (_cpuid_mask)))			\
		vmx->nested.msrs.cr4_fixed1 |= (_cr4_mask);	\
} while (0)

	entry = kvm_find_cpuid_entry(vcpu, 0x1, 0);
	cr4_fixed1_update(X86_CR4_VME,        edx, feature_bit(VME));
	cr4_fixed1_update(X86_CR4_PVI,        edx, feature_bit(VME));
	cr4_fixed1_update(X86_CR4_TSD,        edx, feature_bit(TSC));
	cr4_fixed1_update(X86_CR4_DE,         edx, feature_bit(DE));
	cr4_fixed1_update(X86_CR4_PSE,        edx, feature_bit(PSE));
	cr4_fixed1_update(X86_CR4_PAE,        edx, feature_bit(PAE));
	cr4_fixed1_update(X86_CR4_MCE,        edx, feature_bit(MCE));
	cr4_fixed1_update(X86_CR4_PGE,        edx, feature_bit(PGE));
	cr4_fixed1_update(X86_CR4_OSFXSR,     edx, feature_bit(FXSR));
	cr4_fixed1_update(X86_CR4_OSXMMEXCPT, edx, feature_bit(XMM));
	cr4_fixed1_update(X86_CR4_VMXE,       ecx, feature_bit(VMX));
	cr4_fixed1_update(X86_CR4_SMXE,       ecx, feature_bit(SMX));
	cr4_fixed1_update(X86_CR4_PCIDE,      ecx, feature_bit(PCID));
	cr4_fixed1_update(X86_CR4_OSXSAVE,    ecx, feature_bit(XSAVE));

	entry = kvm_find_cpuid_entry(vcpu, 0x7, 0);
	cr4_fixed1_update(X86_CR4_FSGSBASE,   ebx, feature_bit(FSGSBASE));
	cr4_fixed1_update(X86_CR4_SMEP,       ebx, feature_bit(SMEP));
	cr4_fixed1_update(X86_CR4_SMAP,       ebx, feature_bit(SMAP));
	cr4_fixed1_update(X86_CR4_PKE,        ecx, feature_bit(PKU));
	cr4_fixed1_update(X86_CR4_UMIP,       ecx, feature_bit(UMIP));
	cr4_fixed1_update(X86_CR4_LA57,       ecx, feature_bit(LA57));

#undef cr4_fixed1_update
}

static void nested_vmx_entry_exit_ctls_update(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (kvm_mpx_supported()) {
		bool mpx_enabled = guest_cpuid_has(vcpu, X86_FEATURE_MPX);

		if (mpx_enabled) {
			vmx->nested.msrs.entry_ctls_high |= VM_ENTRY_LOAD_BNDCFGS;
			vmx->nested.msrs.exit_ctls_high |= VM_EXIT_CLEAR_BNDCFGS;
		} else {
			vmx->nested.msrs.entry_ctls_high &= ~VM_ENTRY_LOAD_BNDCFGS;
			vmx->nested.msrs.exit_ctls_high &= ~VM_EXIT_CLEAR_BNDCFGS;
		}
	}
}

static void update_intel_pt_cfg(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct kvm_cpuid_entry2 *best = NULL;
	int i;

	for (i = 0; i < PT_CPUID_LEAVES; i++) {
		best = kvm_find_cpuid_entry(vcpu, 0x14, i);
		if (!best)
			return;
		vmx->pt_desc.caps[CPUID_EAX + i*PT_CPUID_REGS_NUM] = best->eax;
		vmx->pt_desc.caps[CPUID_EBX + i*PT_CPUID_REGS_NUM] = best->ebx;
		vmx->pt_desc.caps[CPUID_ECX + i*PT_CPUID_REGS_NUM] = best->ecx;
		vmx->pt_desc.caps[CPUID_EDX + i*PT_CPUID_REGS_NUM] = best->edx;
	}

	/* Get the number of configurable Address Ranges for filtering */
	vmx->pt_desc.addr_range = intel_pt_validate_cap(vmx->pt_desc.caps,
						PT_CAP_num_address_ranges);

	/* Initialize and clear the no dependency bits */
	vmx->pt_desc.ctl_bitmask = ~(RTIT_CTL_TRACEEN | RTIT_CTL_OS |
			RTIT_CTL_USR | RTIT_CTL_TSC_EN | RTIT_CTL_DISRETC);

	/*
	 * If CPUID.(EAX=14H,ECX=0):EBX[0]=1 CR3Filter can be set otherwise
	 * will inject an #GP
	 */
	if (intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_cr3_filtering))
		vmx->pt_desc.ctl_bitmask &= ~RTIT_CTL_CR3EN;

	/*
	 * If CPUID.(EAX=14H,ECX=0):EBX[1]=1 CYCEn, CycThresh and
	 * PSBFreq can be set
	 */
	if (intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_psb_cyc))
		vmx->pt_desc.ctl_bitmask &= ~(RTIT_CTL_CYCLEACC |
				RTIT_CTL_CYC_THRESH | RTIT_CTL_PSB_FREQ);

	/*
	 * If CPUID.(EAX=14H,ECX=0):EBX[3]=1 MTCEn BranchEn and
	 * MTCFreq can be set
	 */
	if (intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_mtc))
		vmx->pt_desc.ctl_bitmask &= ~(RTIT_CTL_MTC_EN |
				RTIT_CTL_BRANCH_EN | RTIT_CTL_MTC_RANGE);

	/* If CPUID.(EAX=14H,ECX=0):EBX[4]=1 FUPonPTW and PTWEn can be set */
	if (intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_ptwrite))
		vmx->pt_desc.ctl_bitmask &= ~(RTIT_CTL_FUP_ON_PTW |
							RTIT_CTL_PTW_EN);

	/* If CPUID.(EAX=14H,ECX=0):EBX[5]=1 PwrEvEn can be set */
	if (intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_power_event_trace))
		vmx->pt_desc.ctl_bitmask &= ~RTIT_CTL_PWR_EVT_EN;

	/* If CPUID.(EAX=14H,ECX=0):ECX[0]=1 ToPA can be set */
	if (intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_topa_output))
		vmx->pt_desc.ctl_bitmask &= ~RTIT_CTL_TOPA;

	/* If CPUID.(EAX=14H,ECX=0):ECX[3]=1 FabircEn can be set */
	if (intel_pt_validate_cap(vmx->pt_desc.caps, PT_CAP_output_subsys))
		vmx->pt_desc.ctl_bitmask &= ~RTIT_CTL_FABRIC_EN;

	/* unmask address range configure area */
	for (i = 0; i < vmx->pt_desc.addr_range; i++)
		vmx->pt_desc.ctl_bitmask &= ~(0xfULL << (32 + i * 4));
}

static void vmx_cpuid_update(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	/* xsaves_enabled is recomputed in vmx_compute_secondary_exec_control(). */
	vcpu->arch.xsaves_enabled = false;

	if (cpu_has_secondary_exec_ctrls()) {
		vmx_compute_secondary_exec_control(vmx);
		vmcs_set_secondary_exec_control(vmx);
	}

	if (nested_vmx_allowed(vcpu))
		to_vmx(vcpu)->msr_ia32_feature_control_valid_bits |=
			FEAT_CTL_VMX_ENABLED_INSIDE_SMX |
			FEAT_CTL_VMX_ENABLED_OUTSIDE_SMX;
	else
		to_vmx(vcpu)->msr_ia32_feature_control_valid_bits &=
			~(FEAT_CTL_VMX_ENABLED_INSIDE_SMX |
			  FEAT_CTL_VMX_ENABLED_OUTSIDE_SMX);

	if (nested_vmx_allowed(vcpu)) {
		nested_vmx_cr_fixed1_bits_update(vcpu);
		nested_vmx_entry_exit_ctls_update(vcpu);
	}

	if (boot_cpu_has(X86_FEATURE_INTEL_PT) &&
			guest_cpuid_has(vcpu, X86_FEATURE_INTEL_PT))
		update_intel_pt_cfg(vcpu);

	if (boot_cpu_has(X86_FEATURE_RTM)) {
		struct shared_msr_entry *msr;
		msr = find_msr_entry(vmx, MSR_IA32_TSX_CTRL);
		if (msr) {
			bool enabled = guest_cpuid_has(vcpu, X86_FEATURE_RTM);
			vmx_set_guest_msr(vmx, msr, enabled ? 0 : TSX_CTRL_RTM_DISABLE);
		}
	}
}

static void vmx_set_supported_cpuid(u32 func, struct kvm_cpuid_entry2 *entry)
{
	if (func == 1 && nested)
		entry->ecx |= feature_bit(VMX);
}

static void vmx_request_immediate_exit(struct kvm_vcpu *vcpu)
{
	to_vmx(vcpu)->req_immediate_exit = true;
}

static int vmx_check_intercept_io(struct kvm_vcpu *vcpu,
				  struct x86_instruction_info *info)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	unsigned short port;
	bool intercept;
	int size;

	if (info->intercept == x86_intercept_in ||
	    info->intercept == x86_intercept_ins) {
		port = info->src_val;
		size = info->dst_bytes;
	} else {
		port = info->dst_val;
		size = info->src_bytes;
	}

	/*
	 * If the 'use IO bitmaps' VM-execution control is 0, IO instruction
	 * VM-exits depend on the 'unconditional IO exiting' VM-execution
	 * control.
	 *
	 * Otherwise, IO instruction VM-exits are controlled by the IO bitmaps.
	 */
	if (!nested_cpu_has(vmcs12, CPU_BASED_USE_IO_BITMAPS))
		intercept = nested_cpu_has(vmcs12,
					   CPU_BASED_UNCOND_IO_EXITING);
	else
		intercept = nested_vmx_check_io_bitmaps(vcpu, port, size);

	return intercept ? X86EMUL_UNHANDLEABLE : X86EMUL_CONTINUE;
}

static int vmx_check_intercept(struct kvm_vcpu *vcpu,
			       struct x86_instruction_info *info,
			       enum x86_intercept_stage stage)
{
	struct vmcs12 *vmcs12 = get_vmcs12(vcpu);
	struct x86_emulate_ctxt *ctxt = &vcpu->arch.emulate_ctxt;

	switch (info->intercept) {
	/*
	 * RDPID causes #UD if disabled through secondary execution controls.
	 * Because it is marked as EmulateOnUD, we need to intercept it here.
	 */
	case x86_intercept_rdtscp:
		if (!nested_cpu_has2(vmcs12, SECONDARY_EXEC_RDTSCP)) {
			ctxt->exception.vector = UD_VECTOR;
			ctxt->exception.error_code_valid = false;
			return X86EMUL_PROPAGATE_FAULT;
		}
		break;

	case x86_intercept_in:
	case x86_intercept_ins:
	case x86_intercept_out:
	case x86_intercept_outs:
		return vmx_check_intercept_io(vcpu, info);

	/* TODO: check more intercepts... */
	default:
		break;
	}

	return X86EMUL_UNHANDLEABLE;
}

#ifdef CONFIG_X86_64
/* (a << shift) / divisor, return 1 if overflow otherwise 0 */
static inline int u64_shl_div_u64(u64 a, unsigned int shift,
				  u64 divisor, u64 *result)
{
	u64 low = a << shift, high = a >> (64 - shift);

	/* To avoid the overflow on divq */
	if (high >= divisor)
		return 1;

	/* Low hold the result, high hold rem which is discarded */
	asm("divq %2\n\t" : "=a" (low), "=d" (high) :
	    "rm" (divisor), "0" (low), "1" (high));
	*result = low;

	return 0;
}

static int vmx_set_hv_timer(struct kvm_vcpu *vcpu, u64 guest_deadline_tsc,
			    bool *expired)
{
	struct vcpu_vmx *vmx;
	u64 tscl, guest_tscl, delta_tsc, lapic_timer_advance_cycles;
	struct kvm_timer *ktimer = &vcpu->arch.apic->lapic_timer;

	if (kvm_mwait_in_guest(vcpu->kvm) ||
		kvm_can_post_timer_interrupt(vcpu))
		return -EOPNOTSUPP;

	vmx = to_vmx(vcpu);
	tscl = rdtsc();
	guest_tscl = kvm_read_l1_tsc(vcpu, tscl);
	delta_tsc = max(guest_deadline_tsc, guest_tscl) - guest_tscl;
	lapic_timer_advance_cycles = nsec_to_cycles(vcpu,
						    ktimer->timer_advance_ns);

	if (delta_tsc > lapic_timer_advance_cycles)
		delta_tsc -= lapic_timer_advance_cycles;
	else
		delta_tsc = 0;

	/* Convert to host delta tsc if tsc scaling is enabled */
	if (vcpu->arch.tsc_scaling_ratio != kvm_default_tsc_scaling_ratio &&
	    delta_tsc && u64_shl_div_u64(delta_tsc,
				kvm_tsc_scaling_ratio_frac_bits,
				vcpu->arch.tsc_scaling_ratio, &delta_tsc))
		return -ERANGE;

	/*
	 * If the delta tsc can't fit in the 32 bit after the multi shift,
	 * we can't use the preemption timer.
	 * It's possible that it fits on later vmentries, but checking
	 * on every vmentry is costly so we just use an hrtimer.
	 */
	if (delta_tsc >> (cpu_preemption_timer_multi + 32))
		return -ERANGE;

	vmx->hv_deadline_tsc = tscl + delta_tsc;
	*expired = !delta_tsc;
	return 0;
}

static void vmx_cancel_hv_timer(struct kvm_vcpu *vcpu)
{
	to_vmx(vcpu)->hv_deadline_tsc = -1;
}
#endif

static void vmx_sched_in(struct kvm_vcpu *vcpu, int cpu)
{
	if (!kvm_pause_in_guest(vcpu->kvm))
		shrink_ple_window(vcpu);
}

static void vmx_slot_enable_log_dirty(struct kvm *kvm,
				     struct kvm_memory_slot *slot)
{
	kvm_mmu_slot_leaf_clear_dirty(kvm, slot);
	kvm_mmu_slot_largepage_remove_write_access(kvm, slot);
}

static void vmx_slot_disable_log_dirty(struct kvm *kvm,
				       struct kvm_memory_slot *slot)
{
	kvm_mmu_slot_set_dirty(kvm, slot);
}

static void vmx_flush_log_dirty(struct kvm *kvm)
{
	kvm_flush_pml_buffers(kvm);
}

static int vmx_write_pml_buffer(struct kvm_vcpu *vcpu)
{
	struct vmcs12 *vmcs12;
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	gpa_t gpa, dst;

	if (is_guest_mode(vcpu)) {
		WARN_ON_ONCE(vmx->nested.pml_full);

		/*
		 * Check if PML is enabled for the nested guest.
		 * Whether eptp bit 6 is set is already checked
		 * as part of A/D emulation.
		 */
		vmcs12 = get_vmcs12(vcpu);
		if (!nested_cpu_has_pml(vmcs12))
			return 0;

		if (vmcs12->guest_pml_index >= PML_ENTITY_NUM) {
			vmx->nested.pml_full = true;
			return 1;
		}

		gpa = vmcs_read64(GUEST_PHYSICAL_ADDRESS) & ~0xFFFull;
		dst = vmcs12->pml_address + sizeof(u64) * vmcs12->guest_pml_index;

		if (kvm_write_guest_page(vcpu->kvm, gpa_to_gfn(dst), &gpa,
					 offset_in_page(dst), sizeof(gpa)))
			return 0;

		vmcs12->guest_pml_index--;
	}

	return 0;
}

static void vmx_enable_log_dirty_pt_masked(struct kvm *kvm,
					   struct kvm_memory_slot *memslot,
					   gfn_t offset, unsigned long mask)
{
	kvm_mmu_clear_dirty_pt_masked(kvm, memslot, offset, mask);
}

static void __pi_post_block(struct kvm_vcpu *vcpu)
{
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);
	struct pi_desc old, new;
	unsigned int dest;

	do {
		old.control = new.control = pi_desc->control;
		WARN(old.nv != POSTED_INTR_WAKEUP_VECTOR,
		     "Wakeup handler not enabled while the VCPU is blocked\n");

		dest = cpu_physical_id(vcpu->cpu);

		if (x2apic_enabled())
			new.ndst = dest;
		else
			new.ndst = (dest << 8) & 0xFF00;

		/* set 'NV' to 'notification vector' */
		new.nv = POSTED_INTR_VECTOR;
	} while (cmpxchg64(&pi_desc->control, old.control,
			   new.control) != old.control);

	if (!WARN_ON_ONCE(vcpu->pre_pcpu == -1)) {
		spin_lock(&per_cpu(blocked_vcpu_on_cpu_lock, vcpu->pre_pcpu));
		list_del(&vcpu->blocked_vcpu_list);
		spin_unlock(&per_cpu(blocked_vcpu_on_cpu_lock, vcpu->pre_pcpu));
		vcpu->pre_pcpu = -1;
	}
}

/*
 * This routine does the following things for vCPU which is going
 * to be blocked if VT-d PI is enabled.
 * - Store the vCPU to the wakeup list, so when interrupts happen
 *   we can find the right vCPU to wake up.
 * - Change the Posted-interrupt descriptor as below:
 *      'NDST' <-- vcpu->pre_pcpu
 *      'NV' <-- POSTED_INTR_WAKEUP_VECTOR
 * - If 'ON' is set during this process, which means at least one
 *   interrupt is posted for this vCPU, we cannot block it, in
 *   this case, return 1, otherwise, return 0.
 *
 */
static int pi_pre_block(struct kvm_vcpu *vcpu)
{
	unsigned int dest;
	struct pi_desc old, new;
	struct pi_desc *pi_desc = vcpu_to_pi_desc(vcpu);

	if (!kvm_arch_has_assigned_device(vcpu->kvm) ||
		!irq_remapping_cap(IRQ_POSTING_CAP)  ||
		!kvm_vcpu_apicv_active(vcpu))
		return 0;

	WARN_ON(irqs_disabled());
	local_irq_disable();
	if (!WARN_ON_ONCE(vcpu->pre_pcpu != -1)) {
		vcpu->pre_pcpu = vcpu->cpu;
		spin_lock(&per_cpu(blocked_vcpu_on_cpu_lock, vcpu->pre_pcpu));
		list_add_tail(&vcpu->blocked_vcpu_list,
			      &per_cpu(blocked_vcpu_on_cpu,
				       vcpu->pre_pcpu));
		spin_unlock(&per_cpu(blocked_vcpu_on_cpu_lock, vcpu->pre_pcpu));
	}

	do {
		old.control = new.control = pi_desc->control;

		WARN((pi_desc->sn == 1),
		     "Warning: SN field of posted-interrupts "
		     "is set before blocking\n");

		/*
		 * Since vCPU can be preempted during this process,
		 * vcpu->cpu could be different with pre_pcpu, we
		 * need to set pre_pcpu as the destination of wakeup
		 * notification event, then we can find the right vCPU
		 * to wakeup in wakeup handler if interrupts happen
		 * when the vCPU is in blocked state.
		 */
		dest = cpu_physical_id(vcpu->pre_pcpu);

		if (x2apic_enabled())
			new.ndst = dest;
		else
			new.ndst = (dest << 8) & 0xFF00;

		/* set 'NV' to 'wakeup vector' */
		new.nv = POSTED_INTR_WAKEUP_VECTOR;
	} while (cmpxchg64(&pi_desc->control, old.control,
			   new.control) != old.control);

	/* We should not block the vCPU if an interrupt is posted for it.  */
	if (pi_test_on(pi_desc) == 1)
		__pi_post_block(vcpu);

	local_irq_enable();
	return (vcpu->pre_pcpu == -1);
}

static int vmx_pre_block(struct kvm_vcpu *vcpu)
{
	if (pi_pre_block(vcpu))
		return 1;

	if (kvm_lapic_hv_timer_in_use(vcpu))
		kvm_lapic_switch_to_sw_timer(vcpu);

	return 0;
}

static void pi_post_block(struct kvm_vcpu *vcpu)
{
	if (vcpu->pre_pcpu == -1)
		return;

	WARN_ON(irqs_disabled());
	local_irq_disable();
	__pi_post_block(vcpu);
	local_irq_enable();
}

static void vmx_post_block(struct kvm_vcpu *vcpu)
{
	if (kvm_x86_ops->set_hv_timer)
		kvm_lapic_switch_to_hv_timer(vcpu);

	pi_post_block(vcpu);
}

/*
 * vmx_update_pi_irte - set IRTE for Posted-Interrupts
 *
 * @kvm: kvm
 * @host_irq: host irq of the interrupt
 * @guest_irq: gsi of the interrupt
 * @set: set or unset PI
 * returns 0 on success, < 0 on failure
 */
static int vmx_update_pi_irte(struct kvm *kvm, unsigned int host_irq,
			      uint32_t guest_irq, bool set)
{
	struct kvm_kernel_irq_routing_entry *e;
	struct kvm_irq_routing_table *irq_rt;
	struct kvm_lapic_irq irq;
	struct kvm_vcpu *vcpu;
	struct vcpu_data vcpu_info;
	int idx, ret = 0;

	if (!kvm_arch_has_assigned_device(kvm) ||
		!irq_remapping_cap(IRQ_POSTING_CAP) ||
		!kvm_vcpu_apicv_active(kvm->vcpus[0]))
		return 0;

	idx = srcu_read_lock(&kvm->irq_srcu);
	irq_rt = srcu_dereference(kvm->irq_routing, &kvm->irq_srcu);
	if (guest_irq >= irq_rt->nr_rt_entries ||
	    hlist_empty(&irq_rt->map[guest_irq])) {
		pr_warn_once("no route for guest_irq %u/%u (broken user space?)\n",
			     guest_irq, irq_rt->nr_rt_entries);
		goto out;
	}

	hlist_for_each_entry(e, &irq_rt->map[guest_irq], link) {
		if (e->type != KVM_IRQ_ROUTING_MSI)
			continue;
		/*
		 * VT-d PI cannot support posting multicast/broadcast
		 * interrupts to a vCPU, we still use interrupt remapping
		 * for these kind of interrupts.
		 *
		 * For lowest-priority interrupts, we only support
		 * those with single CPU as the destination, e.g. user
		 * configures the interrupts via /proc/irq or uses
		 * irqbalance to make the interrupts single-CPU.
		 *
		 * We will support full lowest-priority interrupt later.
		 *
		 * In addition, we can only inject generic interrupts using
		 * the PI mechanism, refuse to route others through it.
		 */

		kvm_set_msi_irq(kvm, e, &irq);
		if (!kvm_intr_is_single_vcpu(kvm, &irq, &vcpu) ||
		    !kvm_irq_is_postable(&irq)) {
			/*
			 * Make sure the IRTE is in remapped mode if
			 * we don't handle it in posted mode.
			 */
			ret = irq_set_vcpu_affinity(host_irq, NULL);
			if (ret < 0) {
				printk(KERN_INFO
				   "failed to back to remapped mode, irq: %u\n",
				   host_irq);
				goto out;
			}

			continue;
		}

		vcpu_info.pi_desc_addr = __pa(vcpu_to_pi_desc(vcpu));
		vcpu_info.vector = irq.vector;

		trace_kvm_pi_irte_update(host_irq, vcpu->vcpu_id, e->gsi,
				vcpu_info.vector, vcpu_info.pi_desc_addr, set);

		if (set)
			ret = irq_set_vcpu_affinity(host_irq, &vcpu_info);
		else
			ret = irq_set_vcpu_affinity(host_irq, NULL);

		if (ret < 0) {
			printk(KERN_INFO "%s: failed to update PI IRTE\n",
					__func__);
			goto out;
		}
	}

	ret = 0;
out:
	srcu_read_unlock(&kvm->irq_srcu, idx);
	return ret;
}

static void vmx_setup_mce(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.mcg_cap & MCG_LMCE_P)
		to_vmx(vcpu)->msr_ia32_feature_control_valid_bits |=
			FEAT_CTL_LMCE_ENABLED;
	else
		to_vmx(vcpu)->msr_ia32_feature_control_valid_bits &=
			~FEAT_CTL_LMCE_ENABLED;
}

static int vmx_smi_allowed(struct kvm_vcpu *vcpu)
{
	/* we need a nested vmexit to enter SMM, postpone if run is pending */
	if (to_vmx(vcpu)->nested.nested_run_pending)
		return 0;
	return 1;
}

static int vmx_pre_enter_smm(struct kvm_vcpu *vcpu, char *smstate)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	vmx->nested.smm.guest_mode = is_guest_mode(vcpu);
	if (vmx->nested.smm.guest_mode)
		nested_vmx_vmexit(vcpu, -1, 0, 0);

	vmx->nested.smm.vmxon = vmx->nested.vmxon;
	vmx->nested.vmxon = false;
	vmx_clear_hlt(vcpu);
	return 0;
}

static int vmx_pre_leave_smm(struct kvm_vcpu *vcpu, const char *smstate)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	int ret;

	if (vmx->nested.smm.vmxon) {
		vmx->nested.vmxon = true;
		vmx->nested.smm.vmxon = false;
	}

	if (vmx->nested.smm.guest_mode) {
		ret = nested_vmx_enter_non_root_mode(vcpu, false);
		if (ret)
			return ret;

		vmx->nested.smm.guest_mode = false;
	}
	return 0;
}

static int enable_smi_window(struct kvm_vcpu *vcpu)
{
	return 0;
}

static bool vmx_need_emulation_on_page_fault(struct kvm_vcpu *vcpu)
{
	return false;
}

static bool vmx_apic_init_signal_blocked(struct kvm_vcpu *vcpu)
{
	return to_vmx(vcpu)->nested.vmxon;
}

static __init int hardware_setup(void)
{
	unsigned long host_bndcfgs;
	struct desc_ptr dt;
	int r, i;

	rdmsrl_safe(MSR_EFER, &host_efer);

	store_idt(&dt);
	host_idt_base = dt.address;

	for (i = 0; i < ARRAY_SIZE(vmx_msr_index); ++i)
		kvm_define_shared_msr(i, vmx_msr_index[i]);

	if (setup_vmcs_config(&vmcs_config, &vmx_capability) < 0)
		return -EIO;

	if (boot_cpu_has(X86_FEATURE_NX))
		kvm_enable_efer_bits(EFER_NX);

	if (boot_cpu_has(X86_FEATURE_MPX)) {
		rdmsrl(MSR_IA32_BNDCFGS, host_bndcfgs);
		WARN_ONCE(host_bndcfgs, "KVM: BNDCFGS in host will be lost");
	}

	if (!cpu_has_vmx_vpid() || !cpu_has_vmx_invvpid() ||
	    !(cpu_has_vmx_invvpid_single() || cpu_has_vmx_invvpid_global()))
		enable_vpid = 0;

	if (!cpu_has_vmx_ept() ||
	    !cpu_has_vmx_ept_4levels() ||
	    !cpu_has_vmx_ept_mt_wb() ||
	    !cpu_has_vmx_invept_global())
		enable_ept = 0;

	if (!cpu_has_vmx_ept_ad_bits() || !enable_ept)
		enable_ept_ad_bits = 0;

	if (!cpu_has_vmx_unrestricted_guest() || !enable_ept)
		enable_unrestricted_guest = 0;

	if (!cpu_has_vmx_flexpriority())
		flexpriority_enabled = 0;

	if (!cpu_has_virtual_nmis())
		enable_vnmi = 0;

	/*
	 * set_apic_access_page_addr() is used to reload apic access
	 * page upon invalidation.  No need to do anything if not
	 * using the APIC_ACCESS_ADDR VMCS field.
	 */
	if (!flexpriority_enabled)
		kvm_x86_ops->set_apic_access_page_addr = NULL;

	if (!cpu_has_vmx_tpr_shadow())
		kvm_x86_ops->update_cr8_intercept = NULL;

	if (enable_ept && !cpu_has_vmx_ept_2m_page())
		kvm_disable_largepages();

#if IS_ENABLED(CONFIG_HYPERV)
	if (ms_hyperv.nested_features & HV_X64_NESTED_GUEST_MAPPING_FLUSH
	    && enable_ept) {
		kvm_x86_ops->tlb_remote_flush = hv_remote_flush_tlb;
		kvm_x86_ops->tlb_remote_flush_with_range =
				hv_remote_flush_tlb_with_range;
	}
#endif

	if (!cpu_has_vmx_ple()) {
		ple_gap = 0;
		ple_window = 0;
		ple_window_grow = 0;
		ple_window_max = 0;
		ple_window_shrink = 0;
	}

	if (!cpu_has_vmx_apicv()) {
		enable_apicv = 0;
		kvm_x86_ops->sync_pir_to_irr = NULL;
	}

	if (cpu_has_vmx_tsc_scaling()) {
		kvm_has_tsc_control = true;
		kvm_max_tsc_scaling_ratio = KVM_VMX_TSC_MULTIPLIER_MAX;
		kvm_tsc_scaling_ratio_frac_bits = 48;
	}

	set_bit(0, vmx_vpid_bitmap); /* 0 is reserved for host */

	if (enable_ept)
		vmx_enable_tdp();
	else
		kvm_disable_tdp();

	/*
	 * Only enable PML when hardware supports PML feature, and both EPT
	 * and EPT A/D bit features are enabled -- PML depends on them to work.
	 */
	if (!enable_ept || !enable_ept_ad_bits || !cpu_has_vmx_pml())
		enable_pml = 0;

	if (!enable_pml) {
		kvm_x86_ops->slot_enable_log_dirty = NULL;
		kvm_x86_ops->slot_disable_log_dirty = NULL;
		kvm_x86_ops->flush_log_dirty = NULL;
		kvm_x86_ops->enable_log_dirty_pt_masked = NULL;
	}

	if (!cpu_has_vmx_preemption_timer())
		enable_preemption_timer = false;

	if (enable_preemption_timer) {
		u64 use_timer_freq = 5000ULL * 1000 * 1000;
		u64 vmx_msr;

		rdmsrl(MSR_IA32_VMX_MISC, vmx_msr);
		cpu_preemption_timer_multi =
			vmx_msr & VMX_MISC_PREEMPTION_TIMER_RATE_MASK;

		if (tsc_khz)
			use_timer_freq = (u64)tsc_khz * 1000;
		use_timer_freq >>= cpu_preemption_timer_multi;

		/*
		 * KVM "disables" the preemption timer by setting it to its max
		 * value.  Don't use the timer if it might cause spurious exits
		 * at a rate faster than 0.1 Hz (of uninterrupted guest time).
		 */
		if (use_timer_freq > 0xffffffffu / 10)
			enable_preemption_timer = false;
	}

	if (!enable_preemption_timer) {
		kvm_x86_ops->set_hv_timer = NULL;
		kvm_x86_ops->cancel_hv_timer = NULL;
		kvm_x86_ops->request_immediate_exit = __kvm_request_immediate_exit;
	}

	kvm_set_posted_intr_wakeup_handler(wakeup_handler);

	kvm_mce_cap_supported |= MCG_LMCE_P;

	if (pt_mode != PT_MODE_SYSTEM && pt_mode != PT_MODE_HOST_GUEST)
		return -EINVAL;
	if (!enable_ept || !cpu_has_vmx_intel_pt())
		pt_mode = PT_MODE_SYSTEM;

	if (nested) {
		nested_vmx_setup_ctls_msrs(&vmcs_config.nested,
					   vmx_capability.ept);

		r = nested_vmx_hardware_setup(kvm_vmx_exit_handlers);
		if (r)
			return r;
	}

	r = alloc_kvm_area();
	if (r)
		nested_vmx_hardware_unsetup();
	return r;
}

static __exit void hardware_unsetup(void)
{
	if (nested)
		nested_vmx_hardware_unsetup();

	free_kvm_area();
}

static bool vmx_check_apicv_inhibit_reasons(ulong bit)
{
	ulong supported = BIT(APICV_INHIBIT_REASON_DISABLE) |
			  BIT(APICV_INHIBIT_REASON_HYPERV);

	return supported & BIT(bit);
}

static struct kvm_x86_ops vmx_x86_ops __ro_after_init = {
	.cpu_has_kvm_support = cpu_has_kvm_support,
	.disabled_by_bios = vmx_disabled_by_bios,
	.hardware_setup = hardware_setup,
	.hardware_unsetup = hardware_unsetup,
	.check_processor_compatibility = vmx_check_processor_compat,
	.hardware_enable = hardware_enable,
	.hardware_disable = hardware_disable,
	.cpu_has_accelerated_tpr = report_flexpriority,
	.has_emulated_msr = vmx_has_emulated_msr,

	.vm_init = vmx_vm_init,
	.vm_alloc = vmx_vm_alloc,
	.vm_free = vmx_vm_free,

	.vcpu_create = vmx_create_vcpu,
	.vcpu_free = vmx_free_vcpu,
	.vcpu_reset = vmx_vcpu_reset,

	.prepare_guest_switch = vmx_prepare_switch_to_guest,
	.vcpu_load = vmx_vcpu_load,
	.vcpu_put = vmx_vcpu_put,

	.update_bp_intercept = update_exception_bitmap,
	.get_msr_feature = vmx_get_msr_feature,
	.get_msr = vmx_get_msr,
	.set_msr = vmx_set_msr,
	.get_segment_base = vmx_get_segment_base,
	.get_segment = vmx_get_segment,
	.set_segment = vmx_set_segment,
	.get_cpl = vmx_get_cpl,
	.get_cs_db_l_bits = vmx_get_cs_db_l_bits,
	.decache_cr0_guest_bits = vmx_decache_cr0_guest_bits,
	.decache_cr4_guest_bits = vmx_decache_cr4_guest_bits,
	.set_cr0 = vmx_set_cr0,
	.set_cr3 = vmx_set_cr3,
	.set_cr4 = vmx_set_cr4,
	.set_efer = vmx_set_efer,
	.get_idt = vmx_get_idt,
	.set_idt = vmx_set_idt,
	.get_gdt = vmx_get_gdt,
	.set_gdt = vmx_set_gdt,
	.get_dr6 = vmx_get_dr6,
	.set_dr6 = vmx_set_dr6,
	.set_dr7 = vmx_set_dr7,
	.sync_dirty_debug_regs = vmx_sync_dirty_debug_regs,
	.cache_reg = vmx_cache_reg,
	.get_rflags = vmx_get_rflags,
	.set_rflags = vmx_set_rflags,

	.tlb_flush = vmx_flush_tlb,
	.tlb_flush_gva = vmx_flush_tlb_gva,

	.run = vmx_vcpu_run,
	.handle_exit = vmx_handle_exit,
	.skip_emulated_instruction = vmx_skip_emulated_instruction,
	.update_emulated_instruction = vmx_update_emulated_instruction,
	.set_interrupt_shadow = vmx_set_interrupt_shadow,
	.get_interrupt_shadow = vmx_get_interrupt_shadow,
	.patch_hypercall = vmx_patch_hypercall,
	.set_irq = vmx_inject_irq,
	.set_nmi = vmx_inject_nmi,
	.queue_exception = vmx_queue_exception,
	.cancel_injection = vmx_cancel_injection,
	.interrupt_allowed = vmx_interrupt_allowed,
	.nmi_allowed = vmx_nmi_allowed,
	.get_nmi_mask = vmx_get_nmi_mask,
	.set_nmi_mask = vmx_set_nmi_mask,
	.enable_nmi_window = enable_nmi_window,
	.enable_irq_window = enable_irq_window,
	.update_cr8_intercept = update_cr8_intercept,
	.set_virtual_apic_mode = vmx_set_virtual_apic_mode,
	.set_apic_access_page_addr = vmx_set_apic_access_page_addr,
	.refresh_apicv_exec_ctrl = vmx_refresh_apicv_exec_ctrl,
	.load_eoi_exitmap = vmx_load_eoi_exitmap,
	.apicv_post_state_restore = vmx_apicv_post_state_restore,
	.check_apicv_inhibit_reasons = vmx_check_apicv_inhibit_reasons,
	.hwapic_irr_update = vmx_hwapic_irr_update,
	.hwapic_isr_update = vmx_hwapic_isr_update,
	.guest_apic_has_interrupt = vmx_guest_apic_has_interrupt,
	.sync_pir_to_irr = vmx_sync_pir_to_irr,
	.deliver_posted_interrupt = vmx_deliver_posted_interrupt,
	.dy_apicv_has_pending_interrupt = vmx_dy_apicv_has_pending_interrupt,

	.set_tss_addr = vmx_set_tss_addr,
	.set_identity_map_addr = vmx_set_identity_map_addr,
	.get_tdp_level = get_ept_level,
	.get_mt_mask = vmx_get_mt_mask,

	.get_exit_info = vmx_get_exit_info,

	.get_lpage_level = vmx_get_lpage_level,

	.cpuid_update = vmx_cpuid_update,

	.rdtscp_supported = vmx_rdtscp_supported,
	.invpcid_supported = vmx_invpcid_supported,

	.set_supported_cpuid = vmx_set_supported_cpuid,

	.has_wbinvd_exit = cpu_has_vmx_wbinvd_exit,

	.read_l1_tsc_offset = vmx_read_l1_tsc_offset,
	.write_l1_tsc_offset = vmx_write_l1_tsc_offset,

	.set_tdp_cr3 = vmx_set_cr3,

	.check_intercept = vmx_check_intercept,
	.handle_exit_irqoff = vmx_handle_exit_irqoff,
	.mpx_supported = vmx_mpx_supported,
	.xsaves_supported = vmx_xsaves_supported,
	.umip_emulated = vmx_umip_emulated,
	.pt_supported = vmx_pt_supported,
	.pku_supported = vmx_pku_supported,

	.request_immediate_exit = vmx_request_immediate_exit,

	.sched_in = vmx_sched_in,

	.slot_enable_log_dirty = vmx_slot_enable_log_dirty,
	.slot_disable_log_dirty = vmx_slot_disable_log_dirty,
	.flush_log_dirty = vmx_flush_log_dirty,
	.enable_log_dirty_pt_masked = vmx_enable_log_dirty_pt_masked,
	.write_log_dirty = vmx_write_pml_buffer,

	.pre_block = vmx_pre_block,
	.post_block = vmx_post_block,

	.pmu_ops = &intel_pmu_ops,

	.update_pi_irte = vmx_update_pi_irte,

#ifdef CONFIG_X86_64
	.set_hv_timer = vmx_set_hv_timer,
	.cancel_hv_timer = vmx_cancel_hv_timer,
#endif

	.setup_mce = vmx_setup_mce,

	.smi_allowed = vmx_smi_allowed,
	.pre_enter_smm = vmx_pre_enter_smm,
	.pre_leave_smm = vmx_pre_leave_smm,
	.enable_smi_window = enable_smi_window,

	.check_nested_events = NULL,
	.get_nested_state = NULL,
	.set_nested_state = NULL,
	.get_vmcs12_pages = NULL,
	.nested_enable_evmcs = NULL,
	.nested_get_evmcs_version = NULL,
	.need_emulation_on_page_fault = vmx_need_emulation_on_page_fault,
	.apic_init_signal_blocked = vmx_apic_init_signal_blocked,
};

static void vmx_cleanup_l1d_flush(void)
{
	if (vmx_l1d_flush_pages) {
		free_pages((unsigned long)vmx_l1d_flush_pages, L1D_CACHE_ORDER);
		vmx_l1d_flush_pages = NULL;
	}
	/* Restore state so sysfs ignores VMX */
	l1tf_vmx_mitigation = VMENTER_L1D_FLUSH_AUTO;
}

static void vmx_exit(void)
{
#ifdef CONFIG_KEXEC_CORE
	RCU_INIT_POINTER(crash_vmclear_loaded_vmcss, NULL);
	synchronize_rcu();
#endif

	kvm_exit();

#if IS_ENABLED(CONFIG_HYPERV)
	if (static_branch_unlikely(&enable_evmcs)) {
		int cpu;
		struct hv_vp_assist_page *vp_ap;
		/*
		 * Reset everything to support using non-enlightened VMCS
		 * access later (e.g. when we reload the module with
		 * enlightened_vmcs=0)
		 */
		for_each_online_cpu(cpu) {
			vp_ap =	hv_get_vp_assist_page(cpu);

			if (!vp_ap)
				continue;

			vp_ap->nested_control.features.directhypercall = 0;
			vp_ap->current_nested_vmcs = 0;
			vp_ap->enlighten_vmentry = 0;
		}

		static_branch_disable(&enable_evmcs);
	}
#endif
	vmx_cleanup_l1d_flush();
}
module_exit(vmx_exit);

static int __init vmx_init(void)
{
	int r;

#if IS_ENABLED(CONFIG_HYPERV)
	/*
	 * Enlightened VMCS usage should be recommended and the host needs
	 * to support eVMCS v1 or above. We can also disable eVMCS support
	 * with module parameter.
	 */
	if (enlightened_vmcs &&
	    ms_hyperv.hints & HV_X64_ENLIGHTENED_VMCS_RECOMMENDED &&
	    (ms_hyperv.nested_features & HV_X64_ENLIGHTENED_VMCS_VERSION) >=
	    KVM_EVMCS_VERSION) {
		int cpu;

		/* Check that we have assist pages on all online CPUs */
		for_each_online_cpu(cpu) {
			if (!hv_get_vp_assist_page(cpu)) {
				enlightened_vmcs = false;
				break;
			}
		}

		if (enlightened_vmcs) {
			pr_info("KVM: vmx: using Hyper-V Enlightened VMCS\n");
			static_branch_enable(&enable_evmcs);
		}

		if (ms_hyperv.nested_features & HV_X64_NESTED_DIRECT_FLUSH)
			vmx_x86_ops.enable_direct_tlbflush
				= hv_enable_direct_tlbflush;

	} else {
		enlightened_vmcs = false;
	}
#endif

	r = kvm_init(&vmx_x86_ops, sizeof(struct vcpu_vmx),
		     __alignof__(struct vcpu_vmx), THIS_MODULE);
	if (r)
		return r;

	/*
	 * Must be called after kvm_init() so enable_ept is properly set
	 * up. Hand the parameter mitigation value in which was stored in
	 * the pre module init parser. If no parameter was given, it will
	 * contain 'auto' which will be turned into the default 'cond'
	 * mitigation mode.
	 */
	r = vmx_setup_l1d_flush(vmentry_l1d_flush_param);
	if (r) {
		vmx_exit();
		return r;
	}

#ifdef CONFIG_KEXEC_CORE
	rcu_assign_pointer(crash_vmclear_loaded_vmcss,
			   crash_vmclear_local_loaded_vmcss);
#endif
	vmx_check_vmcs12_offsets();

	return 0;
}
module_init(vmx_init);

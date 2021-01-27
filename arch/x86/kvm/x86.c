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

#include <linux/kvm_host.h>
#include "irq.h"
#include "ioapic.h"
#include "mmu.h"
#include "i8254.h"
#include "tss.h"
#include "kvm_cache_regs.h"
#include "kvm_emulate.h"
#include "x86.h"
#include "cpuid.h"
#include "pmu.h"
#include "hyperv.h"
#include "lapic.h"

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
#include <linux/intel-iommu.h>
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

#include <trace/events/kvm.h>

#include <asm/debugreg.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include <asm/mce.h>
#include <linux/kernel_stat.h>
#include <asm/fpu/internal.h> /* Ugh! */
#include <asm/pvclock.h>
#include <asm/div64.h>
#include <asm/irq_remapping.h>
#include <asm/mshyperv.h>
#include <asm/hypervisor.h>
#include <asm/tlbflush.h>
#include <asm/intel_pt.h>
#include <asm/emulate_prefix.h>
#include <clocksource/hyperv_timer.h>

#define CREATE_TRACE_POINTS
#include "trace.h"

#define MAX_IO_MSRS 256
#define KVM_MAX_MCE_BANKS 32
u64 __read_mostly kvm_mce_cap_supported = MCG_CTL_P | MCG_SER_P;
EXPORT_SYMBOL_GPL(kvm_mce_cap_supported);

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

#define KVM_X2APIC_API_VALID_FLAGS (KVM_X2APIC_API_USE_32BIT_IDS | \
                                    KVM_X2APIC_API_DISABLE_BROADCAST_QUIRK)

static void update_cr8_intercept(struct kvm_vcpu *vcpu);
static void process_nmi(struct kvm_vcpu *vcpu);
static void process_smi(struct kvm_vcpu *vcpu);
static void enter_smm(struct kvm_vcpu *vcpu);
static void __kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags);
static void store_regs(struct kvm_vcpu *vcpu);
static int sync_regs(struct kvm_vcpu *vcpu);

struct kvm_x86_ops kvm_x86_ops __read_mostly;
EXPORT_SYMBOL_GPL(kvm_x86_ops);

static bool __read_mostly ignore_msrs = 0;
module_param(ignore_msrs, bool, S_IRUGO | S_IWUSR);

static bool __read_mostly report_ignored_msrs = true;
module_param(report_ignored_msrs, bool, S_IRUGO | S_IWUSR);

unsigned int min_timer_period_us = 200;
module_param(min_timer_period_us, uint, S_IRUGO | S_IWUSR);

static bool __read_mostly kvmclock_periodic_sync = true;
module_param(kvmclock_periodic_sync, bool, S_IRUGO);

bool __read_mostly kvm_has_tsc_control;
EXPORT_SYMBOL_GPL(kvm_has_tsc_control);
u32  __read_mostly kvm_max_guest_tsc_khz;
EXPORT_SYMBOL_GPL(kvm_max_guest_tsc_khz);
u8   __read_mostly kvm_tsc_scaling_ratio_frac_bits;
EXPORT_SYMBOL_GPL(kvm_tsc_scaling_ratio_frac_bits);
u64  __read_mostly kvm_max_tsc_scaling_ratio;
EXPORT_SYMBOL_GPL(kvm_max_tsc_scaling_ratio);
u64 __read_mostly kvm_default_tsc_scaling_ratio;
EXPORT_SYMBOL_GPL(kvm_default_tsc_scaling_ratio);

/* tsc tolerance in parts per million - default to 1/2 of the NTP threshold */
static u32 __read_mostly tsc_tolerance_ppm = 250;
module_param(tsc_tolerance_ppm, uint, S_IRUGO | S_IWUSR);

/*
 * lapic timer advance (tscdeadline mode only) in nanoseconds.  '-1' enables
 * adaptive tuning starting from default advancment of 1000ns.  '0' disables
 * advancement entirely.  Any other value is used as-is and disables adaptive
 * tuning, i.e. allows priveleged userspace to set an exact advancement time.
 */
static int __read_mostly lapic_timer_advance_ns = -1;
module_param(lapic_timer_advance_ns, int, S_IRUGO | S_IWUSR);

static bool __read_mostly vector_hashing = true;
module_param(vector_hashing, bool, S_IRUGO);

bool __read_mostly enable_vmware_backdoor = false;
module_param(enable_vmware_backdoor, bool, S_IRUGO);
EXPORT_SYMBOL_GPL(enable_vmware_backdoor);

static bool __read_mostly force_emulation_prefix = false;
module_param(force_emulation_prefix, bool, S_IRUGO);

int __read_mostly pi_inject_timer = -1;
module_param(pi_inject_timer, bint, S_IRUGO | S_IWUSR);

/*
 * Restoring the host value for MSRs that are only consumed when running in
 * usermode, e.g. SYSCALL MSRs and TSC_AUX, can be deferred until the CPU
 * returns to userspace, i.e. the kernel can run with the guest's value.
 */
#define KVM_MAX_NR_USER_RETURN_MSRS 16

struct kvm_user_return_msrs_global {
	int nr;
	u32 msrs[KVM_MAX_NR_USER_RETURN_MSRS];
};

struct kvm_user_return_msrs {
	struct user_return_notifier urn;
	bool registered;
	struct kvm_user_return_msr_values {
		u64 host;
		u64 curr;
	} values[KVM_MAX_NR_USER_RETURN_MSRS];
};

static struct kvm_user_return_msrs_global __read_mostly user_return_msrs_global;
static struct kvm_user_return_msrs __percpu *user_return_msrs;

#define KVM_SUPPORTED_XCR0     (XFEATURE_MASK_FP | XFEATURE_MASK_SSE \
				| XFEATURE_MASK_YMM | XFEATURE_MASK_BNDREGS \
				| XFEATURE_MASK_BNDCSR | XFEATURE_MASK_AVX512 \
				| XFEATURE_MASK_PKRU)

u64 __read_mostly host_efer;
EXPORT_SYMBOL_GPL(host_efer);

bool __read_mostly allow_smaller_maxphyaddr = 0;
EXPORT_SYMBOL_GPL(allow_smaller_maxphyaddr);

u64 __read_mostly host_xss;
EXPORT_SYMBOL_GPL(host_xss);
u64 __read_mostly supported_xss;
EXPORT_SYMBOL_GPL(supported_xss);

struct kvm_stats_debugfs_item debugfs_entries[] = {
	VCPU_STAT("pf_fixed", pf_fixed),
	VCPU_STAT("pf_guest", pf_guest),
	VCPU_STAT("tlb_flush", tlb_flush),
	VCPU_STAT("invlpg", invlpg),
	VCPU_STAT("exits", exits),
	VCPU_STAT("io_exits", io_exits),
	VCPU_STAT("mmio_exits", mmio_exits),
	VCPU_STAT("signal_exits", signal_exits),
	VCPU_STAT("irq_window", irq_window_exits),
	VCPU_STAT("nmi_window", nmi_window_exits),
	VCPU_STAT("halt_exits", halt_exits),
	VCPU_STAT("halt_successful_poll", halt_successful_poll),
	VCPU_STAT("halt_attempted_poll", halt_attempted_poll),
	VCPU_STAT("halt_poll_invalid", halt_poll_invalid),
	VCPU_STAT("halt_wakeup", halt_wakeup),
	VCPU_STAT("hypercalls", hypercalls),
	VCPU_STAT("request_irq", request_irq_exits),
	VCPU_STAT("irq_exits", irq_exits),
	VCPU_STAT("host_state_reload", host_state_reload),
	VCPU_STAT("fpu_reload", fpu_reload),
	VCPU_STAT("insn_emulation", insn_emulation),
	VCPU_STAT("insn_emulation_fail", insn_emulation_fail),
	VCPU_STAT("irq_injections", irq_injections),
	VCPU_STAT("nmi_injections", nmi_injections),
	VCPU_STAT("req_event", req_event),
	VCPU_STAT("l1d_flush", l1d_flush),
	VCPU_STAT("halt_poll_success_ns", halt_poll_success_ns),
	VCPU_STAT("halt_poll_fail_ns", halt_poll_fail_ns),
	VM_STAT("mmu_shadow_zapped", mmu_shadow_zapped),
	VM_STAT("mmu_pte_write", mmu_pte_write),
	VM_STAT("mmu_pte_updated", mmu_pte_updated),
	VM_STAT("mmu_pde_zapped", mmu_pde_zapped),
	VM_STAT("mmu_flooded", mmu_flooded),
	VM_STAT("mmu_recycled", mmu_recycled),
	VM_STAT("mmu_cache_miss", mmu_cache_miss),
	VM_STAT("mmu_unsync", mmu_unsync),
	VM_STAT("remote_tlb_flush", remote_tlb_flush),
	VM_STAT("largepages", lpages, .mode = 0444),
	VM_STAT("nx_largepages_splitted", nx_lpage_splits, .mode = 0444),
	VM_STAT("max_mmu_page_hash_collisions", max_mmu_page_hash_collisions),
	{ NULL }
};

u64 __read_mostly host_xcr0;
u64 __read_mostly supported_xcr0;
EXPORT_SYMBOL_GPL(supported_xcr0);

static struct kmem_cache *x86_fpu_cache;

static struct kmem_cache *x86_emulator_cache;

/*
 * When called, it means the previous get/set msr reached an invalid msr.
 * Return true if we want to ignore/silent this failed msr access.
 */
static bool kvm_msr_ignored_check(struct kvm_vcpu *vcpu, u32 msr,
				  u64 data, bool write)
{
	const char *op = write ? "wrmsr" : "rdmsr";

	if (ignore_msrs) {
		if (report_ignored_msrs)
			kvm_pr_unimpl("ignored %s: 0x%x data 0x%llx\n",
				      op, msr, data);
		/* Mask the error */
		return true;
	} else {
		kvm_debug_ratelimited("unhandled %s: 0x%x data 0x%llx\n",
				      op, msr, data);
		return false;
	}
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
	 * interrupted and executed through kvm_arch_hardware_disable()
	 */
	local_irq_save(flags);
	if (msrs->registered) {
		msrs->registered = false;
		user_return_notifier_unregister(urn);
	}
	local_irq_restore(flags);
	for (slot = 0; slot < user_return_msrs_global.nr; ++slot) {
		values = &msrs->values[slot];
		if (values->host != values->curr) {
			wrmsrl(user_return_msrs_global.msrs[slot], values->host);
			values->curr = values->host;
		}
	}
}

void kvm_define_user_return_msr(unsigned slot, u32 msr)
{
	BUG_ON(slot >= KVM_MAX_NR_USER_RETURN_MSRS);
	user_return_msrs_global.msrs[slot] = msr;
	if (slot >= user_return_msrs_global.nr)
		user_return_msrs_global.nr = slot + 1;
}
EXPORT_SYMBOL_GPL(kvm_define_user_return_msr);

static void kvm_user_return_msr_cpu_online(void)
{
	unsigned int cpu = smp_processor_id();
	struct kvm_user_return_msrs *msrs = per_cpu_ptr(user_return_msrs, cpu);
	u64 value;
	int i;

	for (i = 0; i < user_return_msrs_global.nr; ++i) {
		rdmsrl_safe(user_return_msrs_global.msrs[i], &value);
		msrs->values[i].host = value;
		msrs->values[i].curr = value;
	}
}

int kvm_set_user_return_msr(unsigned slot, u64 value, u64 mask)
{
	unsigned int cpu = smp_processor_id();
	struct kvm_user_return_msrs *msrs = per_cpu_ptr(user_return_msrs, cpu);
	int err;

	value = (value & mask) | (msrs->values[slot].host & ~mask);
	if (value == msrs->values[slot].curr)
		return 0;
	err = wrmsrl_safe(user_return_msrs_global.msrs[slot], value);
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
	unsigned int cpu = smp_processor_id();
	struct kvm_user_return_msrs *msrs = per_cpu_ptr(user_return_msrs, cpu);

	if (msrs->registered)
		kvm_on_user_return(&msrs->urn);
}

u64 kvm_get_apic_base(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.apic_base;
}
EXPORT_SYMBOL_GPL(kvm_get_apic_base);

enum lapic_mode kvm_get_apic_mode(struct kvm_vcpu *vcpu)
{
	return kvm_apic_mode(kvm_get_apic_base(vcpu));
}
EXPORT_SYMBOL_GPL(kvm_get_apic_mode);

int kvm_set_apic_base(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	enum lapic_mode old_mode = kvm_get_apic_mode(vcpu);
	enum lapic_mode new_mode = kvm_apic_mode(msr_info->data);
	u64 reserved_bits = ((~0ULL) << cpuid_maxphyaddr(vcpu)) | 0x2ff |
		(guest_cpuid_has(vcpu, X86_FEATURE_X2APIC) ? 0 : X2APIC_ENABLE);

	if ((msr_info->data & reserved_bits) != 0 || new_mode == LAPIC_MODE_INVALID)
		return 1;
	if (!msr_info->host_initiated) {
		if (old_mode == LAPIC_MODE_X2APIC && new_mode == LAPIC_MODE_XAPIC)
			return 1;
		if (old_mode == LAPIC_MODE_DISABLED && new_mode == LAPIC_MODE_X2APIC)
			return 1;
	}

	kvm_lapic_set_base(vcpu, msr_info->data);
	kvm_recalculate_apic_map(vcpu->kvm);
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_apic_base);

asmlinkage __visible noinstr void kvm_spurious_fault(void)
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

static int exception_type(int vector)
{
	unsigned int mask;

	if (WARN_ON(vector > 31 || vector == NMI_VECTOR))
		return EXCPT_INTERRUPT;

	mask = 1 << vector;

	/* #DB is trap, as instruction watchpoints are handled elsewhere */
	if (mask & ((1 << DB_VECTOR) | (1 << BP_VECTOR) | (1 << OF_VECTOR)))
		return EXCPT_TRAP;

	if (mask & ((1 << DF_VECTOR) | (1 << MC_VECTOR)))
		return EXCPT_ABORT;

	/* Reserved exceptions will result in fault */
	return EXCPT_FAULT;
}

void kvm_deliver_exception_payload(struct kvm_vcpu *vcpu)
{
	unsigned nr = vcpu->arch.exception.nr;
	bool has_payload = vcpu->arch.exception.has_payload;
	unsigned long payload = vcpu->arch.exception.payload;

	if (!has_payload)
		return;

	switch (nr) {
	case DB_VECTOR:
		/*
		 * "Certain debug exceptions may clear bit 0-3.  The
		 * remaining contents of the DR6 register are never
		 * cleared by the processor".
		 */
		vcpu->arch.dr6 &= ~DR_TRAP_BITS;
		/*
		 * DR6.RTM is set by all #DB exceptions that don't clear it.
		 */
		vcpu->arch.dr6 |= DR6_RTM;
		vcpu->arch.dr6 |= payload;
		/*
		 * Bit 16 should be set in the payload whenever the #DB
		 * exception should clear DR6.RTM. This makes the payload
		 * compatible with the pending debug exceptions under VMX.
		 * Though not currently documented in the SDM, this also
		 * makes the payload compatible with the exit qualification
		 * for #DB exceptions under VMX.
		 */
		vcpu->arch.dr6 ^= payload & DR6_RTM;

		/*
		 * The #DB payload is defined as compatible with the 'pending
		 * debug exceptions' field under VMX, not DR6. While bit 12 is
		 * defined in the 'pending debug exceptions' field (enabled
		 * breakpoint), it is reserved and must be zero in DR6.
		 */
		vcpu->arch.dr6 &= ~BIT(12);
		break;
	case PF_VECTOR:
		vcpu->arch.cr2 = payload;
		break;
	}

	vcpu->arch.exception.has_payload = false;
	vcpu->arch.exception.payload = 0;
}
EXPORT_SYMBOL_GPL(kvm_deliver_exception_payload);

static void kvm_multiple_exception(struct kvm_vcpu *vcpu,
		unsigned nr, bool has_error, u32 error_code,
	        bool has_payload, unsigned long payload, bool reinject)
{
	u32 prev_nr;
	int class1, class2;

	kvm_make_request(KVM_REQ_EVENT, vcpu);

	if (!vcpu->arch.exception.pending && !vcpu->arch.exception.injected) {
	queue:
		if (has_error && !is_protmode(vcpu))
			has_error = false;
		if (reinject) {
			/*
			 * On vmentry, vcpu->arch.exception.pending is only
			 * true if an event injection was blocked by
			 * nested_run_pending.  In that case, however,
			 * vcpu_enter_guest requests an immediate exit,
			 * and the guest shouldn't proceed far enough to
			 * need reinjection.
			 */
			WARN_ON_ONCE(vcpu->arch.exception.pending);
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
		vcpu->arch.exception.nr = nr;
		vcpu->arch.exception.error_code = error_code;
		vcpu->arch.exception.has_payload = has_payload;
		vcpu->arch.exception.payload = payload;
		if (!is_guest_mode(vcpu))
			kvm_deliver_exception_payload(vcpu);
		return;
	}

	/* to check exception */
	prev_nr = vcpu->arch.exception.nr;
	if (prev_nr == DF_VECTOR) {
		/* triple fault -> shutdown */
		kvm_make_request(KVM_REQ_TRIPLE_FAULT, vcpu);
		return;
	}
	class1 = exception_class(prev_nr);
	class2 = exception_class(nr);
	if ((class1 == EXCPT_CONTRIBUTORY && class2 == EXCPT_CONTRIBUTORY)
		|| (class1 == EXCPT_PF && class2 != EXCPT_BENIGN)) {
		/*
		 * Generate double fault per SDM Table 5-5.  Set
		 * exception.pending = true so that the double fault
		 * can trigger a nested vmexit.
		 */
		vcpu->arch.exception.pending = true;
		vcpu->arch.exception.injected = false;
		vcpu->arch.exception.has_error_code = true;
		vcpu->arch.exception.nr = DF_VECTOR;
		vcpu->arch.exception.error_code = 0;
		vcpu->arch.exception.has_payload = false;
		vcpu->arch.exception.payload = 0;
	} else
		/* replace previous exception with a new one in a hope
		   that instruction re-execution will regenerate lost
		   exception */
		goto queue;
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

void kvm_inject_page_fault(struct kvm_vcpu *vcpu, struct x86_exception *fault)
{
	++vcpu->stat.pf_guest;
	vcpu->arch.exception.nested_apf =
		is_guest_mode(vcpu) && fault->async_page_fault;
	if (vcpu->arch.exception.nested_apf) {
		vcpu->arch.apf.nested_apf_token = fault->address;
		kvm_queue_exception_e(vcpu, PF_VECTOR, fault->error_code);
	} else {
		kvm_queue_exception_e_p(vcpu, PF_VECTOR, fault->error_code,
					fault->address);
	}
}
EXPORT_SYMBOL_GPL(kvm_inject_page_fault);

bool kvm_inject_emulated_page_fault(struct kvm_vcpu *vcpu,
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
		kvm_mmu_invalidate_gva(vcpu, fault_mmu, fault->address,
				       fault_mmu->root_hpa);

	fault_mmu->inject_page_fault(vcpu, fault);
	return fault->nested_page_fault;
}
EXPORT_SYMBOL_GPL(kvm_inject_emulated_page_fault);

void kvm_inject_nmi(struct kvm_vcpu *vcpu)
{
	atomic_inc(&vcpu->arch.nmi_queued);
	kvm_make_request(KVM_REQ_NMI, vcpu);
}
EXPORT_SYMBOL_GPL(kvm_inject_nmi);

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
	if (kvm_x86_ops.get_cpl(vcpu) <= required_cpl)
		return true;
	kvm_queue_exception_e(vcpu, GP_VECTOR, 0);
	return false;
}
EXPORT_SYMBOL_GPL(kvm_require_cpl);

bool kvm_require_dr(struct kvm_vcpu *vcpu, int dr)
{
	if ((dr != 4 && dr != 5) || !kvm_read_cr4_bits(vcpu, X86_CR4_DE))
		return true;

	kvm_queue_exception(vcpu, UD_VECTOR);
	return false;
}
EXPORT_SYMBOL_GPL(kvm_require_dr);

/*
 * This function will be used to read from the physical memory of the currently
 * running guest. The difference to kvm_vcpu_read_guest_page is that this function
 * can read from guest physical or from the guest's guest physical memory.
 */
int kvm_read_guest_page_mmu(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			    gfn_t ngfn, void *data, int offset, int len,
			    u32 access)
{
	struct x86_exception exception;
	gfn_t real_gfn;
	gpa_t ngpa;

	ngpa     = gfn_to_gpa(ngfn);
	real_gfn = mmu->translate_gpa(vcpu, ngpa, access, &exception);
	if (real_gfn == UNMAPPED_GVA)
		return -EFAULT;

	real_gfn = gpa_to_gfn(real_gfn);

	return kvm_vcpu_read_guest_page(vcpu, real_gfn, data, offset, len);
}
EXPORT_SYMBOL_GPL(kvm_read_guest_page_mmu);

static int kvm_read_nested_guest_page(struct kvm_vcpu *vcpu, gfn_t gfn,
			       void *data, int offset, int len, u32 access)
{
	return kvm_read_guest_page_mmu(vcpu, vcpu->arch.walk_mmu, gfn,
				       data, offset, len, access);
}

static inline u64 pdptr_rsvd_bits(struct kvm_vcpu *vcpu)
{
	return rsvd_bits(cpuid_maxphyaddr(vcpu), 63) | rsvd_bits(5, 8) |
	       rsvd_bits(1, 2);
}

/*
 * Load the pae pdptrs.  Return 1 if they are all valid, 0 otherwise.
 */
int load_pdptrs(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu, unsigned long cr3)
{
	gfn_t pdpt_gfn = cr3 >> PAGE_SHIFT;
	unsigned offset = ((cr3 & (PAGE_SIZE-1)) >> 5) << 2;
	int i;
	int ret;
	u64 pdpte[ARRAY_SIZE(mmu->pdptrs)];

	ret = kvm_read_guest_page_mmu(vcpu, mmu, pdpt_gfn, pdpte,
				      offset * sizeof(u64), sizeof(pdpte),
				      PFERR_USER_MASK|PFERR_WRITE_MASK);
	if (ret < 0) {
		ret = 0;
		goto out;
	}
	for (i = 0; i < ARRAY_SIZE(pdpte); ++i) {
		if ((pdpte[i] & PT_PRESENT_MASK) &&
		    (pdpte[i] & pdptr_rsvd_bits(vcpu))) {
			ret = 0;
			goto out;
		}
	}
	ret = 1;

	memcpy(mmu->pdptrs, pdpte, sizeof(mmu->pdptrs));
	kvm_register_mark_dirty(vcpu, VCPU_EXREG_PDPTR);

out:

	return ret;
}
EXPORT_SYMBOL_GPL(load_pdptrs);

bool pdptrs_changed(struct kvm_vcpu *vcpu)
{
	u64 pdpte[ARRAY_SIZE(vcpu->arch.walk_mmu->pdptrs)];
	int offset;
	gfn_t gfn;
	int r;

	if (!is_pae_paging(vcpu))
		return false;

	if (!kvm_register_is_available(vcpu, VCPU_EXREG_PDPTR))
		return true;

	gfn = (kvm_read_cr3(vcpu) & 0xffffffe0ul) >> PAGE_SHIFT;
	offset = (kvm_read_cr3(vcpu) & 0xffffffe0ul) & (PAGE_SIZE - 1);
	r = kvm_read_nested_guest_page(vcpu, gfn, pdpte, offset, sizeof(pdpte),
				       PFERR_USER_MASK | PFERR_WRITE_MASK);
	if (r < 0)
		return true;

	return memcmp(pdpte, vcpu->arch.walk_mmu->pdptrs, sizeof(pdpte)) != 0;
}
EXPORT_SYMBOL_GPL(pdptrs_changed);

void kvm_post_set_cr0(struct kvm_vcpu *vcpu, unsigned long old_cr0, unsigned long cr0)
{
	unsigned long update_bits = X86_CR0_PG | X86_CR0_WP;

	if ((cr0 ^ old_cr0) & X86_CR0_PG) {
		kvm_clear_async_pf_completion_queue(vcpu);
		kvm_async_pf_hash_reset(vcpu);
	}

	if ((cr0 ^ old_cr0) & update_bits)
		kvm_mmu_reset_context(vcpu);

	if (((cr0 ^ old_cr0) & X86_CR0_CD) &&
	    kvm_arch_has_noncoherent_dma(vcpu->kvm) &&
	    !kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_CD_NW_CLEARED))
		kvm_zap_gfn_range(vcpu->kvm, 0, ~0ULL);
}
EXPORT_SYMBOL_GPL(kvm_post_set_cr0);

int kvm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	unsigned long old_cr0 = kvm_read_cr0(vcpu);
	unsigned long pdptr_bits = X86_CR0_CD | X86_CR0_NW | X86_CR0_PG;

	cr0 |= X86_CR0_ET;

#ifdef CONFIG_X86_64
	if (cr0 & 0xffffffff00000000UL)
		return 1;
#endif

	cr0 &= ~CR0_RESERVED_BITS;

	if ((cr0 & X86_CR0_NW) && !(cr0 & X86_CR0_CD))
		return 1;

	if ((cr0 & X86_CR0_PG) && !(cr0 & X86_CR0_PE))
		return 1;

#ifdef CONFIG_X86_64
	if ((vcpu->arch.efer & EFER_LME) && !is_paging(vcpu) &&
	    (cr0 & X86_CR0_PG)) {
		int cs_db, cs_l;

		if (!is_pae(vcpu))
			return 1;
		kvm_x86_ops.get_cs_db_l_bits(vcpu, &cs_db, &cs_l);
		if (cs_l)
			return 1;
	}
#endif
	if (!(vcpu->arch.efer & EFER_LME) && (cr0 & X86_CR0_PG) &&
	    is_pae(vcpu) && ((cr0 ^ old_cr0) & pdptr_bits) &&
	    !load_pdptrs(vcpu, vcpu->arch.walk_mmu, kvm_read_cr3(vcpu)))
		return 1;

	if (!(cr0 & X86_CR0_PG) && kvm_read_cr4_bits(vcpu, X86_CR4_PCIDE))
		return 1;

	kvm_x86_ops.set_cr0(vcpu, cr0);

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

	if (kvm_read_cr4_bits(vcpu, X86_CR4_OSXSAVE)) {

		if (vcpu->arch.xcr0 != host_xcr0)
			xsetbv(XCR_XFEATURE_ENABLED_MASK, vcpu->arch.xcr0);

		if (vcpu->arch.xsaves_enabled &&
		    vcpu->arch.ia32_xss != host_xss)
			wrmsrl(MSR_IA32_XSS, vcpu->arch.ia32_xss);
	}

	if (static_cpu_has(X86_FEATURE_PKU) &&
	    (kvm_read_cr4_bits(vcpu, X86_CR4_PKE) ||
	     (vcpu->arch.xcr0 & XFEATURE_MASK_PKRU)) &&
	    vcpu->arch.pkru != vcpu->arch.host_pkru)
		__write_pkru(vcpu->arch.pkru);
}
EXPORT_SYMBOL_GPL(kvm_load_guest_xsave_state);

void kvm_load_host_xsave_state(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.guest_state_protected)
		return;

	if (static_cpu_has(X86_FEATURE_PKU) &&
	    (kvm_read_cr4_bits(vcpu, X86_CR4_PKE) ||
	     (vcpu->arch.xcr0 & XFEATURE_MASK_PKRU))) {
		vcpu->arch.pkru = rdpkru();
		if (vcpu->arch.pkru != vcpu->arch.host_pkru)
			__write_pkru(vcpu->arch.host_pkru);
	}

	if (kvm_read_cr4_bits(vcpu, X86_CR4_OSXSAVE)) {

		if (vcpu->arch.xcr0 != host_xcr0)
			xsetbv(XCR_XFEATURE_ENABLED_MASK, host_xcr0);

		if (vcpu->arch.xsaves_enabled &&
		    vcpu->arch.ia32_xss != host_xss)
			wrmsrl(MSR_IA32_XSS, host_xss);
	}

}
EXPORT_SYMBOL_GPL(kvm_load_host_xsave_state);

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
	 * emulated CPU does not support XSAVE (see fx_init).
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
	vcpu->arch.xcr0 = xcr0;

	if ((xcr0 ^ old_xcr0) & XFEATURE_MASK_EXTEND)
		kvm_update_cpuid_runtime(vcpu);
	return 0;
}

int kvm_set_xcr(struct kvm_vcpu *vcpu, u32 index, u64 xcr)
{
	if (kvm_x86_ops.get_cpl(vcpu) != 0 ||
	    __kvm_set_xcr(vcpu, index, xcr)) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_xcr);

bool kvm_is_valid_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	if (cr4 & cr4_reserved_bits)
		return false;

	if (cr4 & vcpu->arch.cr4_guest_rsvd_bits)
		return false;

	return kvm_x86_ops.is_valid_cr4(vcpu, cr4);
}
EXPORT_SYMBOL_GPL(kvm_is_valid_cr4);

void kvm_post_set_cr4(struct kvm_vcpu *vcpu, unsigned long old_cr4, unsigned long cr4)
{
	unsigned long mmu_role_bits = X86_CR4_PGE | X86_CR4_PSE | X86_CR4_PAE |
				      X86_CR4_SMEP | X86_CR4_SMAP | X86_CR4_PKE;

	if (((cr4 ^ old_cr4) & mmu_role_bits) ||
	    (!(cr4 & X86_CR4_PCIDE) && (old_cr4 & X86_CR4_PCIDE)))
		kvm_mmu_reset_context(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_post_set_cr4);

int kvm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	unsigned long old_cr4 = kvm_read_cr4(vcpu);
	unsigned long pdptr_bits = X86_CR4_PGE | X86_CR4_PSE | X86_CR4_PAE |
				   X86_CR4_SMEP;

	if (!kvm_is_valid_cr4(vcpu, cr4))
		return 1;

	if (is_long_mode(vcpu)) {
		if (!(cr4 & X86_CR4_PAE))
			return 1;
		if ((cr4 ^ old_cr4) & X86_CR4_LA57)
			return 1;
	} else if (is_paging(vcpu) && (cr4 & X86_CR4_PAE)
		   && ((cr4 ^ old_cr4) & pdptr_bits)
		   && !load_pdptrs(vcpu, vcpu->arch.walk_mmu,
				   kvm_read_cr3(vcpu)))
		return 1;

	if ((cr4 & X86_CR4_PCIDE) && !(old_cr4 & X86_CR4_PCIDE)) {
		if (!guest_cpuid_has(vcpu, X86_FEATURE_PCID))
			return 1;

		/* PCID can not be enabled when cr3[11:0]!=000H or EFER.LMA=0 */
		if ((kvm_read_cr3(vcpu) & X86_CR3_PCID_MASK) || !is_long_mode(vcpu))
			return 1;
	}

	kvm_x86_ops.set_cr4(vcpu, cr4);

	kvm_post_set_cr4(vcpu, old_cr4, cr4);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_cr4);

int kvm_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	bool skip_tlb_flush = false;
#ifdef CONFIG_X86_64
	bool pcid_enabled = kvm_read_cr4_bits(vcpu, X86_CR4_PCIDE);

	if (pcid_enabled) {
		skip_tlb_flush = cr3 & X86_CR3_PCID_NOFLUSH;
		cr3 &= ~X86_CR3_PCID_NOFLUSH;
	}
#endif

	if (cr3 == kvm_read_cr3(vcpu) && !pdptrs_changed(vcpu)) {
		if (!skip_tlb_flush) {
			kvm_mmu_sync_roots(vcpu);
			kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
		}
		return 0;
	}

	if (is_long_mode(vcpu) &&
	    (cr3 & vcpu->arch.cr3_lm_rsvd_bits))
		return 1;
	else if (is_pae_paging(vcpu) &&
		 !load_pdptrs(vcpu, vcpu->arch.walk_mmu, cr3))
		return 1;

	kvm_mmu_new_pgd(vcpu, cr3, skip_tlb_flush, skip_tlb_flush);
	vcpu->arch.cr3 = cr3;
	kvm_register_mark_available(vcpu, VCPU_EXREG_CR3);

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
		vcpu->arch.switch_db_regs |= KVM_DEBUGREG_RELOAD;
	}
}

void kvm_update_dr7(struct kvm_vcpu *vcpu)
{
	unsigned long dr7;

	if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)
		dr7 = vcpu->arch.guest_debug_dr7;
	else
		dr7 = vcpu->arch.dr7;
	kvm_x86_ops.set_dr7(vcpu, dr7);
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
	return fixed;
}

static int __kvm_set_dr(struct kvm_vcpu *vcpu, int dr, unsigned long val)
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
			return -1; /* #GP */
		vcpu->arch.dr6 = (val & DR6_VOLATILE) | kvm_dr6_fixed(vcpu);
		break;
	case 5:
	default: /* 7 */
		if (!kvm_dr7_valid(val))
			return -1; /* #GP */
		vcpu->arch.dr7 = (val & DR7_VOLATILE) | DR7_FIXED_1;
		kvm_update_dr7(vcpu);
		break;
	}

	return 0;
}

int kvm_set_dr(struct kvm_vcpu *vcpu, int dr, unsigned long val)
{
	if (__kvm_set_dr(vcpu, dr, val)) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_dr);

int kvm_get_dr(struct kvm_vcpu *vcpu, int dr, unsigned long *val)
{
	size_t size = ARRAY_SIZE(vcpu->arch.db);

	switch (dr) {
	case 0 ... 3:
		*val = vcpu->arch.db[array_index_nospec(dr, size)];
		break;
	case 4:
	case 6:
		*val = vcpu->arch.dr6;
		break;
	case 5:
	default: /* 7 */
		*val = vcpu->arch.dr7;
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_get_dr);

bool kvm_rdpmc(struct kvm_vcpu *vcpu)
{
	u32 ecx = kvm_rcx_read(vcpu);
	u64 data;
	int err;

	err = kvm_pmu_rdpmc(vcpu, ecx, &data);
	if (err)
		return err;
	kvm_rax_write(vcpu, (u32)data);
	kvm_rdx_write(vcpu, data >> 32);
	return err;
}
EXPORT_SYMBOL_GPL(kvm_rdpmc);

/*
 * List of msr numbers which we expose to userspace through KVM_GET_MSRS
 * and KVM_SET_MSRS, and KVM_GET_MSR_INDEX_LIST.
 *
 * The three MSR lists(msrs_to_save, emulated_msrs, msr_based_features)
 * extract the supported MSRs from the related const lists.
 * msrs_to_save is selected from the msrs_to_save_all to reflect the
 * capabilities of the host cpu. This capabilities test skips MSRs that are
 * kvm-specific. Those are put in emulated_msrs_all; filtering of emulated_msrs
 * may depend on host virtualization features rather than host cpu features.
 */

static const u32 msrs_to_save_all[] = {
	MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
	MSR_STAR,
#ifdef CONFIG_X86_64
	MSR_CSTAR, MSR_KERNEL_GS_BASE, MSR_SYSCALL_MASK, MSR_LSTAR,
#endif
	MSR_IA32_TSC, MSR_IA32_CR_PAT, MSR_VM_HSAVE_PA,
	MSR_IA32_FEAT_CTL, MSR_IA32_BNDCFGS, MSR_TSC_AUX,
	MSR_IA32_SPEC_CTRL,
	MSR_IA32_RTIT_CTL, MSR_IA32_RTIT_STATUS, MSR_IA32_RTIT_CR3_MATCH,
	MSR_IA32_RTIT_OUTPUT_BASE, MSR_IA32_RTIT_OUTPUT_MASK,
	MSR_IA32_RTIT_ADDR0_A, MSR_IA32_RTIT_ADDR0_B,
	MSR_IA32_RTIT_ADDR1_A, MSR_IA32_RTIT_ADDR1_B,
	MSR_IA32_RTIT_ADDR2_A, MSR_IA32_RTIT_ADDR2_B,
	MSR_IA32_RTIT_ADDR3_A, MSR_IA32_RTIT_ADDR3_B,
	MSR_IA32_UMWAIT_CONTROL,

	MSR_ARCH_PERFMON_FIXED_CTR0, MSR_ARCH_PERFMON_FIXED_CTR1,
	MSR_ARCH_PERFMON_FIXED_CTR0 + 2, MSR_ARCH_PERFMON_FIXED_CTR0 + 3,
	MSR_CORE_PERF_FIXED_CTR_CTRL, MSR_CORE_PERF_GLOBAL_STATUS,
	MSR_CORE_PERF_GLOBAL_CTRL, MSR_CORE_PERF_GLOBAL_OVF_CTRL,
	MSR_ARCH_PERFMON_PERFCTR0, MSR_ARCH_PERFMON_PERFCTR1,
	MSR_ARCH_PERFMON_PERFCTR0 + 2, MSR_ARCH_PERFMON_PERFCTR0 + 3,
	MSR_ARCH_PERFMON_PERFCTR0 + 4, MSR_ARCH_PERFMON_PERFCTR0 + 5,
	MSR_ARCH_PERFMON_PERFCTR0 + 6, MSR_ARCH_PERFMON_PERFCTR0 + 7,
	MSR_ARCH_PERFMON_PERFCTR0 + 8, MSR_ARCH_PERFMON_PERFCTR0 + 9,
	MSR_ARCH_PERFMON_PERFCTR0 + 10, MSR_ARCH_PERFMON_PERFCTR0 + 11,
	MSR_ARCH_PERFMON_PERFCTR0 + 12, MSR_ARCH_PERFMON_PERFCTR0 + 13,
	MSR_ARCH_PERFMON_PERFCTR0 + 14, MSR_ARCH_PERFMON_PERFCTR0 + 15,
	MSR_ARCH_PERFMON_PERFCTR0 + 16, MSR_ARCH_PERFMON_PERFCTR0 + 17,
	MSR_ARCH_PERFMON_EVENTSEL0, MSR_ARCH_PERFMON_EVENTSEL1,
	MSR_ARCH_PERFMON_EVENTSEL0 + 2, MSR_ARCH_PERFMON_EVENTSEL0 + 3,
	MSR_ARCH_PERFMON_EVENTSEL0 + 4, MSR_ARCH_PERFMON_EVENTSEL0 + 5,
	MSR_ARCH_PERFMON_EVENTSEL0 + 6, MSR_ARCH_PERFMON_EVENTSEL0 + 7,
	MSR_ARCH_PERFMON_EVENTSEL0 + 8, MSR_ARCH_PERFMON_EVENTSEL0 + 9,
	MSR_ARCH_PERFMON_EVENTSEL0 + 10, MSR_ARCH_PERFMON_EVENTSEL0 + 11,
	MSR_ARCH_PERFMON_EVENTSEL0 + 12, MSR_ARCH_PERFMON_EVENTSEL0 + 13,
	MSR_ARCH_PERFMON_EVENTSEL0 + 14, MSR_ARCH_PERFMON_EVENTSEL0 + 15,
	MSR_ARCH_PERFMON_EVENTSEL0 + 16, MSR_ARCH_PERFMON_EVENTSEL0 + 17,
};

static u32 msrs_to_save[ARRAY_SIZE(msrs_to_save_all)];
static unsigned num_msrs_to_save;

static const u32 emulated_msrs_all[] = {
	MSR_KVM_SYSTEM_TIME, MSR_KVM_WALL_CLOCK,
	MSR_KVM_SYSTEM_TIME_NEW, MSR_KVM_WALL_CLOCK_NEW,
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
	HV_X64_MSR_TSC_EMULATION_STATUS,
	HV_X64_MSR_SYNDBG_OPTIONS,
	HV_X64_MSR_SYNDBG_CONTROL, HV_X64_MSR_SYNDBG_STATUS,
	HV_X64_MSR_SYNDBG_SEND_BUFFER, HV_X64_MSR_SYNDBG_RECV_BUFFER,
	HV_X64_MSR_SYNDBG_PENDING_BUFFER,

	MSR_KVM_ASYNC_PF_EN, MSR_KVM_STEAL_TIME,
	MSR_KVM_PV_EOI_EN, MSR_KVM_ASYNC_PF_INT, MSR_KVM_ASYNC_PF_ACK,

	MSR_IA32_TSC_ADJUST,
	MSR_IA32_TSCDEADLINE,
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
	MSR_IA32_POWER_CTL,
	MSR_IA32_UCODE_REV,

	/*
	 * The following list leaves out MSRs whose values are determined
	 * by arch/x86/kvm/vmx/nested.c based on CPUID or other MSRs.
	 * We always support the "true" VMX control MSRs, even if the host
	 * processor does not, so I am putting these registers here rather
	 * than in msrs_to_save_all.
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
 * List of msr numbers which are used to expose MSR-based features that
 * can be used by a hypervisor to validate requested CPU features.
 */
static const u32 msr_based_features_all[] = {
	MSR_IA32_VMX_BASIC,
	MSR_IA32_VMX_TRUE_PINBASED_CTLS,
	MSR_IA32_VMX_PINBASED_CTLS,
	MSR_IA32_VMX_TRUE_PROCBASED_CTLS,
	MSR_IA32_VMX_PROCBASED_CTLS,
	MSR_IA32_VMX_TRUE_EXIT_CTLS,
	MSR_IA32_VMX_EXIT_CTLS,
	MSR_IA32_VMX_TRUE_ENTRY_CTLS,
	MSR_IA32_VMX_ENTRY_CTLS,
	MSR_IA32_VMX_MISC,
	MSR_IA32_VMX_CR0_FIXED0,
	MSR_IA32_VMX_CR0_FIXED1,
	MSR_IA32_VMX_CR4_FIXED0,
	MSR_IA32_VMX_CR4_FIXED1,
	MSR_IA32_VMX_VMCS_ENUM,
	MSR_IA32_VMX_PROCBASED_CTLS2,
	MSR_IA32_VMX_EPT_VPID_CAP,
	MSR_IA32_VMX_VMFUNC,

	MSR_F10H_DECFG,
	MSR_IA32_UCODE_REV,
	MSR_IA32_ARCH_CAPABILITIES,
	MSR_IA32_PERF_CAPABILITIES,
};

static u32 msr_based_features[ARRAY_SIZE(msr_based_features_all)];
static unsigned int num_msr_based_features;

static u64 kvm_get_arch_capabilities(void)
{
	u64 data = 0;

	if (boot_cpu_has(X86_FEATURE_ARCH_CAPABILITIES))
		rdmsrl(MSR_IA32_ARCH_CAPABILITIES, data);

	/*
	 * If nx_huge_pages is enabled, KVM's shadow paging will ensure that
	 * the nested hypervisor runs with NX huge pages.  If it is not,
	 * L1 is anyway vulnerable to ITLB_MULTIHIT explots from other
	 * L1 guests, so it need not worry about its own (L2) guests.
	 */
	data |= ARCH_CAP_PSCHANGE_MC_NO;

	/*
	 * If we're doing cache flushes (either "always" or "cond")
	 * we will do one whenever the guest does a vmlaunch/vmresume.
	 * If an outer hypervisor is doing the cache flush for us
	 * (VMENTER_L1D_FLUSH_NESTED_VM), we can safely pass that
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

	/*
	 * On TAA affected systems:
	 *      - nothing to do if TSX is disabled on the host.
	 *      - we emulate TSX_CTRL if present on the host.
	 *	  This lets the guest use VERW to clear CPU buffers.
	 */
	if (!boot_cpu_has(X86_FEATURE_RTM))
		data &= ~(ARCH_CAP_TAA_NO | ARCH_CAP_TSX_CTRL_MSR);
	else if (!boot_cpu_has_bug(X86_BUG_TAA))
		data |= ARCH_CAP_TAA_NO;

	return data;
}

static int kvm_get_msr_feature(struct kvm_msr_entry *msr)
{
	switch (msr->index) {
	case MSR_IA32_ARCH_CAPABILITIES:
		msr->data = kvm_get_arch_capabilities();
		break;
	case MSR_IA32_UCODE_REV:
		rdmsrl_safe(msr->index, &msr->data);
		break;
	default:
		return kvm_x86_ops.get_msr_feature(msr);
	}
	return 0;
}

static int do_get_msr_feature(struct kvm_vcpu *vcpu, unsigned index, u64 *data)
{
	struct kvm_msr_entry msr;
	int r;

	msr.index = index;
	r = kvm_get_msr_feature(&msr);

	if (r == KVM_MSR_RET_INVALID) {
		/* Unconditionally clear the output for simplicity */
		*data = 0;
		if (kvm_msr_ignored_check(vcpu, index, 0, false))
			r = 0;
	}

	if (r)
		return r;

	*data = msr.data;

	return 0;
}

static bool __kvm_valid_efer(struct kvm_vcpu *vcpu, u64 efer)
{
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

	r = kvm_x86_ops.set_efer(vcpu, efer);
	if (r) {
		WARN_ON(r > 0);
		return r;
	}

	/* Update reserved bits */
	if ((efer ^ old_efer) & EFER_NX)
		kvm_mmu_reset_context(vcpu);

	return 0;
}

void kvm_enable_efer_bits(u64 mask)
{
       efer_reserved_bits &= ~mask;
}
EXPORT_SYMBOL_GPL(kvm_enable_efer_bits);

bool kvm_msr_allowed(struct kvm_vcpu *vcpu, u32 index, u32 type)
{
	struct kvm *kvm = vcpu->kvm;
	struct msr_bitmap_range *ranges = kvm->arch.msr_filter.ranges;
	u32 count = kvm->arch.msr_filter.count;
	u32 i;
	bool r = kvm->arch.msr_filter.default_allow;
	int idx;

	/* MSR filtering not set up or x2APIC enabled, allow everything */
	if (!count || (index >= 0x800 && index <= 0x8ff))
		return true;

	/* Prevent collision with set_msr_filter */
	idx = srcu_read_lock(&kvm->srcu);

	for (i = 0; i < count; i++) {
		u32 start = ranges[i].base;
		u32 end = start + ranges[i].nmsrs;
		u32 flags = ranges[i].flags;
		unsigned long *bitmap = ranges[i].bitmap;

		if ((index >= start) && (index < end) && (flags & type)) {
			r = !!test_bit(index - start, bitmap);
			break;
		}
	}

	srcu_read_unlock(&kvm->srcu, idx);

	return r;
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

	if (!host_initiated && !kvm_msr_allowed(vcpu, index, KVM_MSR_FILTER_WRITE))
		return KVM_MSR_RET_FILTERED;

	switch (index) {
	case MSR_FS_BASE:
	case MSR_GS_BASE:
	case MSR_KERNEL_GS_BASE:
	case MSR_CSTAR:
	case MSR_LSTAR:
		if (is_noncanonical_address(data, vcpu))
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
		data = get_canonical(data, vcpu_virt_addr_bits(vcpu));
	}

	msr.data = data;
	msr.index = index;
	msr.host_initiated = host_initiated;

	return kvm_x86_ops.set_msr(vcpu, &msr);
}

static int kvm_set_msr_ignored_check(struct kvm_vcpu *vcpu,
				     u32 index, u64 data, bool host_initiated)
{
	int ret = __kvm_set_msr(vcpu, index, data, host_initiated);

	if (ret == KVM_MSR_RET_INVALID)
		if (kvm_msr_ignored_check(vcpu, index, data, true))
			ret = 0;

	return ret;
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

	if (!host_initiated && !kvm_msr_allowed(vcpu, index, KVM_MSR_FILTER_READ))
		return KVM_MSR_RET_FILTERED;

	msr.index = index;
	msr.host_initiated = host_initiated;

	ret = kvm_x86_ops.get_msr(vcpu, &msr);
	if (!ret)
		*data = msr.data;
	return ret;
}

static int kvm_get_msr_ignored_check(struct kvm_vcpu *vcpu,
				     u32 index, u64 *data, bool host_initiated)
{
	int ret = __kvm_get_msr(vcpu, index, data, host_initiated);

	if (ret == KVM_MSR_RET_INVALID) {
		/* Unconditionally clear *data for simplicity */
		*data = 0;
		if (kvm_msr_ignored_check(vcpu, index, 0, false))
			ret = 0;
	}

	return ret;
}

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

static int complete_emulated_rdmsr(struct kvm_vcpu *vcpu)
{
	int err = vcpu->run->msr.error;
	if (!err) {
		kvm_rax_write(vcpu, (u32)vcpu->run->msr.data);
		kvm_rdx_write(vcpu, vcpu->run->msr.data >> 32);
	}

	return kvm_x86_ops.complete_emulated_msr(vcpu, err);
}

static int complete_emulated_wrmsr(struct kvm_vcpu *vcpu)
{
	return kvm_x86_ops.complete_emulated_msr(vcpu, vcpu->run->msr.error);
}

static u64 kvm_msr_reason(int r)
{
	switch (r) {
	case KVM_MSR_RET_INVALID:
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

static int kvm_get_msr_user_space(struct kvm_vcpu *vcpu, u32 index, int r)
{
	return kvm_msr_user_space(vcpu, index, KVM_EXIT_X86_RDMSR, 0,
				   complete_emulated_rdmsr, r);
}

static int kvm_set_msr_user_space(struct kvm_vcpu *vcpu, u32 index, u64 data, int r)
{
	return kvm_msr_user_space(vcpu, index, KVM_EXIT_X86_WRMSR, data,
				   complete_emulated_wrmsr, r);
}

int kvm_emulate_rdmsr(struct kvm_vcpu *vcpu)
{
	u32 ecx = kvm_rcx_read(vcpu);
	u64 data;
	int r;

	r = kvm_get_msr(vcpu, ecx, &data);

	/* MSR read failed? See if we should ask user space */
	if (r && kvm_get_msr_user_space(vcpu, ecx, r)) {
		/* Bounce to user space */
		return 0;
	}

	if (!r) {
		trace_kvm_msr_read(ecx, data);

		kvm_rax_write(vcpu, data & -1u);
		kvm_rdx_write(vcpu, (data >> 32) & -1u);
	} else {
		trace_kvm_msr_read_ex(ecx);
	}

	return kvm_x86_ops.complete_emulated_msr(vcpu, r);
}
EXPORT_SYMBOL_GPL(kvm_emulate_rdmsr);

int kvm_emulate_wrmsr(struct kvm_vcpu *vcpu)
{
	u32 ecx = kvm_rcx_read(vcpu);
	u64 data = kvm_read_edx_eax(vcpu);
	int r;

	r = kvm_set_msr(vcpu, ecx, data);

	/* MSR write failed? See if we should ask user space */
	if (r && kvm_set_msr_user_space(vcpu, ecx, data, r))
		/* Bounce to user space */
		return 0;

	/* Signal all other negative errors to userspace */
	if (r < 0)
		return r;

	if (!r)
		trace_kvm_msr_write(ecx, data);
	else
		trace_kvm_msr_write_ex(ecx, data);

	return kvm_x86_ops.complete_emulated_msr(vcpu, r);
}
EXPORT_SYMBOL_GPL(kvm_emulate_wrmsr);

bool kvm_vcpu_exit_request(struct kvm_vcpu *vcpu)
{
	return vcpu->mode == EXITING_GUEST_MODE || kvm_request_pending(vcpu) ||
		xfer_to_guest_mode_work_pending();
}
EXPORT_SYMBOL_GPL(kvm_vcpu_exit_request);

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
		((u32)(data >> 32) != X2APIC_BROADCAST)) {

		data &= ~(1 << 12);
		kvm_apic_send_ipi(vcpu->arch.apic, (u32)data, (u32)(data >> 32));
		kvm_lapic_set_reg(vcpu->arch.apic, APIC_ICR2, (u32)(data >> 32));
		kvm_lapic_set_reg(vcpu->arch.apic, APIC_ICR, (u32)data);
		trace_kvm_apic_write(APIC_ICR, (u32)data);
		return 0;
	}

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
	fastpath_t ret = EXIT_FASTPATH_NONE;

	switch (msr) {
	case APIC_BASE_MSR + (APIC_ICR >> 4):
		data = kvm_read_edx_eax(vcpu);
		if (!handle_fastpath_set_x2apic_icr_irqoff(vcpu, data)) {
			kvm_skip_emulated_instruction(vcpu);
			ret = EXIT_FASTPATH_EXIT_HANDLED;
		}
		break;
	case MSR_IA32_TSCDEADLINE:
		data = kvm_read_edx_eax(vcpu);
		if (!handle_fastpath_set_tscdeadline(vcpu, data)) {
			kvm_skip_emulated_instruction(vcpu);
			ret = EXIT_FASTPATH_REENTER_GUEST;
		}
		break;
	default:
		break;
	}

	if (ret != EXIT_FASTPATH_NONE)
		trace_kvm_msr_write(msr, data);

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

static void kvm_write_wall_clock(struct kvm *kvm, gpa_t wall_clock)
{
	int version;
	int r;
	struct pvclock_wall_clock wc;
	u64 wall_nsec;

	kvm->arch.wall_clock = wall_clock;

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

	/*
	 * The guest calculates current wall clock time by adding
	 * system time (updated by kvm_guest_time_update below) to the
	 * wall clock specified here.  We do the reverse here.
	 */
	wall_nsec = ktime_get_real_ns() - get_kvmclock_ns(kvm);

	wc.nsec = do_div(wall_nsec, 1000000000);
	wc.sec = (u32)wall_nsec; /* overflow in 2106 guest time */
	wc.version = version;

	kvm_write_guest(kvm, wall_clock, &wc, sizeof(wc));

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
	vcpu->arch.pv_time_enabled = false;
	if (!(system_time & 1))
		return;

	if (!kvm_gfn_to_hva_cache_init(vcpu->kvm,
				       &vcpu->arch.pv_time, system_time & ~1ULL,
				       sizeof(struct pvclock_vcpu_time_info)))
		vcpu->arch.pv_time_enabled = true;

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

static int set_tsc_khz(struct kvm_vcpu *vcpu, u32 user_tsc_khz, bool scale)
{
	u64 ratio;

	/* Guest TSC same frequency as host TSC? */
	if (!scale) {
		vcpu->arch.tsc_scaling_ratio = kvm_default_tsc_scaling_ratio;
		return 0;
	}

	/* TSC scaling supported? */
	if (!kvm_has_tsc_control) {
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
	ratio = mul_u64_u32_div(1ULL << kvm_tsc_scaling_ratio_frac_bits,
				user_tsc_khz, tsc_khz);

	if (ratio == 0 || ratio >= kvm_max_tsc_scaling_ratio) {
		pr_warn_ratelimited("Invalid TSC scaling ratio - virtual-tsc-khz=%u\n",
			            user_tsc_khz);
		return -1;
	}

	vcpu->arch.tsc_scaling_ratio = ratio;
	return 0;
}

static int kvm_set_tsc_khz(struct kvm_vcpu *vcpu, u32 user_tsc_khz)
{
	u32 thresh_lo, thresh_hi;
	int use_scaling = 0;

	/* tsc_khz can be zero if TSC calibration fails */
	if (user_tsc_khz == 0) {
		/* set tsc_scaling_ratio to a safe value */
		vcpu->arch.tsc_scaling_ratio = kvm_default_tsc_scaling_ratio;
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
		pr_debug("kvm: requested TSC rate %u falls outside tolerance [%u,%u]\n", user_tsc_khz, thresh_lo, thresh_hi);
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

static inline int gtod_is_based_on_tsc(int mode)
{
	return mode == VDSO_CLOCKMODE_TSC || mode == VDSO_CLOCKMODE_HVCLOCK;
}

static void kvm_track_tsc_matching(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_X86_64
	bool vcpus_matched;
	struct kvm_arch *ka = &vcpu->kvm->arch;
	struct pvclock_gtod_data *gtod = &pvclock_gtod_data;

	vcpus_matched = (ka->nr_vcpus_matched_tsc + 1 ==
			 atomic_read(&vcpu->kvm->online_vcpus));

	/*
	 * Once the masterclock is enabled, always perform request in
	 * order to update it.
	 *
	 * In order to enable masterclock, the host clocksource must be TSC
	 * and the vcpus need to have matched TSCs.  When that happens,
	 * perform request to enable masterclock.
	 */
	if (ka->use_master_clock ||
	    (gtod_is_based_on_tsc(gtod->clock.vclock_mode) && vcpus_matched))
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
 * N equals to kvm_tsc_scaling_ratio_frac_bits.
 */
static inline u64 __scale_tsc(u64 ratio, u64 tsc)
{
	return mul_u64_u64_shr(tsc, ratio, kvm_tsc_scaling_ratio_frac_bits);
}

u64 kvm_scale_tsc(struct kvm_vcpu *vcpu, u64 tsc)
{
	u64 _tsc = tsc;
	u64 ratio = vcpu->arch.tsc_scaling_ratio;

	if (ratio != kvm_default_tsc_scaling_ratio)
		_tsc = __scale_tsc(ratio, tsc);

	return _tsc;
}
EXPORT_SYMBOL_GPL(kvm_scale_tsc);

static u64 kvm_compute_tsc_offset(struct kvm_vcpu *vcpu, u64 target_tsc)
{
	u64 tsc;

	tsc = kvm_scale_tsc(vcpu, rdtsc());

	return target_tsc - tsc;
}

u64 kvm_read_l1_tsc(struct kvm_vcpu *vcpu, u64 host_tsc)
{
	return vcpu->arch.l1_tsc_offset + kvm_scale_tsc(vcpu, host_tsc);
}
EXPORT_SYMBOL_GPL(kvm_read_l1_tsc);

static void kvm_vcpu_write_tsc_offset(struct kvm_vcpu *vcpu, u64 offset)
{
	vcpu->arch.l1_tsc_offset = offset;
	vcpu->arch.tsc_offset = kvm_x86_ops.write_l1_tsc_offset(vcpu, offset);
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

static void kvm_synchronize_tsc(struct kvm_vcpu *vcpu, u64 data)
{
	struct kvm *kvm = vcpu->kvm;
	u64 offset, ns, elapsed;
	unsigned long flags;
	bool matched;
	bool already_matched;
	bool synchronizing = false;

	raw_spin_lock_irqsave(&kvm->arch.tsc_write_lock, flags);
	offset = kvm_compute_tsc_offset(vcpu, data);
	ns = get_kvmclock_base_ns();
	elapsed = ns - kvm->arch.last_tsc_nsec;

	if (vcpu->arch.virtual_tsc_khz) {
		if (data == 0) {
			/*
			 * detection of vcpu initialization -- need to sync
			 * with other vCPUs. This particularly helps to keep
			 * kvm_clock stable after CPU hotplug
			 */
			synchronizing = true;
		} else {
			u64 tsc_exp = kvm->arch.last_tsc_write +
						nsec_to_cycles(vcpu, elapsed);
			u64 tsc_hz = vcpu->arch.virtual_tsc_khz * 1000LL;
			/*
			 * Special case: TSC write with a small delta (1 second)
			 * of virtual cycle time against real time is
			 * interpreted as an attempt to synchronize the CPU.
			 */
			synchronizing = data < tsc_exp + tsc_hz &&
					data + tsc_hz > tsc_exp;
		}
	}

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
			offset = kvm_compute_tsc_offset(vcpu, data);
		}
		matched = true;
		already_matched = (vcpu->arch.this_tsc_generation == kvm->arch.cur_tsc_generation);
	} else {
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
		kvm->arch.cur_tsc_write = data;
		kvm->arch.cur_tsc_offset = offset;
		matched = false;
	}

	/*
	 * We also track th most recent recorded KHZ, write and time to
	 * allow the matching interval to be extended at each write.
	 */
	kvm->arch.last_tsc_nsec = ns;
	kvm->arch.last_tsc_write = data;
	kvm->arch.last_tsc_khz = vcpu->arch.virtual_tsc_khz;

	vcpu->arch.last_guest_tsc = data;

	/* Keep track of which generation this VCPU has synchronized to */
	vcpu->arch.this_tsc_generation = kvm->arch.cur_tsc_generation;
	vcpu->arch.this_tsc_nsec = kvm->arch.cur_tsc_nsec;
	vcpu->arch.this_tsc_write = kvm->arch.cur_tsc_write;

	kvm_vcpu_write_tsc_offset(vcpu, offset);
	raw_spin_unlock_irqrestore(&kvm->arch.tsc_write_lock, flags);

	spin_lock(&kvm->arch.pvclock_gtod_sync_lock);
	if (!matched) {
		kvm->arch.nr_vcpus_matched_tsc = 0;
	} else if (!already_matched) {
		kvm->arch.nr_vcpus_matched_tsc++;
	}

	kvm_track_tsc_matching(vcpu);
	spin_unlock(&kvm->arch.pvclock_gtod_sync_lock);
}

static inline void adjust_tsc_offset_guest(struct kvm_vcpu *vcpu,
					   s64 adjustment)
{
	u64 tsc_offset = vcpu->arch.l1_tsc_offset;
	kvm_vcpu_write_tsc_offset(vcpu, tsc_offset + adjustment);
}

static inline void adjust_tsc_offset_host(struct kvm_vcpu *vcpu, s64 adjustment)
{
	if (vcpu->arch.tsc_scaling_ratio != kvm_default_tsc_scaling_ratio)
		WARN_ON(adjustment < 0);
	adjustment = kvm_scale_tsc(vcpu, (u64) adjustment);
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
	long v;
	u64 tsc_pg_val;

	switch (clock->vclock_mode) {
	case VDSO_CLOCKMODE_HVCLOCK:
		tsc_pg_val = hv_read_tsc_page_tsc(hv_get_tsc_page(),
						  tsc_timestamp);
		if (tsc_pg_val != U64_MAX) {
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

static int do_monotonic_raw(s64 *t, u64 *tsc_timestamp)
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

/* returns true if host is using TSC based clocksource */
static bool kvm_get_time_and_clockread(s64 *kernel_ns, u64 *tsc_timestamp)
{
	/* checked again under seqlock below */
	if (!gtod_is_based_on_tsc(pvclock_gtod_data.clock.vclock_mode))
		return false;

	return gtod_is_based_on_tsc(do_monotonic_raw(kernel_ns,
						      tsc_timestamp));
}

/* returns true if host is using TSC based clocksource */
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

void kvm_make_mclock_inprogress_request(struct kvm *kvm)
{
	kvm_make_all_cpus_request(kvm, KVM_REQ_MCLOCK_INPROGRESS);
}

static void kvm_gen_update_masterclock(struct kvm *kvm)
{
#ifdef CONFIG_X86_64
	int i;
	struct kvm_vcpu *vcpu;
	struct kvm_arch *ka = &kvm->arch;

	spin_lock(&ka->pvclock_gtod_sync_lock);
	kvm_make_mclock_inprogress_request(kvm);
	/* no guest entries from this point */
	pvclock_update_vm_gtod_copy(kvm);

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);

	/* guest entries allowed */
	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_clear_request(KVM_REQ_MCLOCK_INPROGRESS, vcpu);

	spin_unlock(&ka->pvclock_gtod_sync_lock);
#endif
}

u64 get_kvmclock_ns(struct kvm *kvm)
{
	struct kvm_arch *ka = &kvm->arch;
	struct pvclock_vcpu_time_info hv_clock;
	u64 ret;

	spin_lock(&ka->pvclock_gtod_sync_lock);
	if (!ka->use_master_clock) {
		spin_unlock(&ka->pvclock_gtod_sync_lock);
		return get_kvmclock_base_ns() + ka->kvmclock_offset;
	}

	hv_clock.tsc_timestamp = ka->master_cycle_now;
	hv_clock.system_time = ka->master_kernel_ns + ka->kvmclock_offset;
	spin_unlock(&ka->pvclock_gtod_sync_lock);

	/* both __this_cpu_read() and rdtsc() should be on the same cpu */
	get_cpu();

	if (__this_cpu_read(cpu_tsc_khz)) {
		kvm_get_time_scale(NSEC_PER_SEC, __this_cpu_read(cpu_tsc_khz) * 1000LL,
				   &hv_clock.tsc_shift,
				   &hv_clock.tsc_to_system_mul);
		ret = __pvclock_read_cycles(&hv_clock, rdtsc());
	} else
		ret = get_kvmclock_base_ns() + ka->kvmclock_offset;

	put_cpu();

	return ret;
}

static void kvm_setup_pvclock_page(struct kvm_vcpu *v)
{
	struct kvm_vcpu_arch *vcpu = &v->arch;
	struct pvclock_vcpu_time_info guest_hv_clock;

	if (unlikely(kvm_read_guest_cached(v->kvm, &vcpu->pv_time,
		&guest_hv_clock, sizeof(guest_hv_clock))))
		return;

	/* This VCPU is paused, but it's legal for a guest to read another
	 * VCPU's kvmclock, so we really have to follow the specification where
	 * it says that version is odd if data is being modified, and even after
	 * it is consistent.
	 *
	 * Version field updates must be kept separate.  This is because
	 * kvm_write_guest_cached might use a "rep movs" instruction, and
	 * writes within a string instruction are weakly ordered.  So there
	 * are three writes overall.
	 *
	 * As a small optimization, only write the version field in the first
	 * and third write.  The vcpu->pv_time cache is still valid, because the
	 * version field is the first in the struct.
	 */
	BUILD_BUG_ON(offsetof(struct pvclock_vcpu_time_info, version) != 0);

	if (guest_hv_clock.version & 1)
		++guest_hv_clock.version;  /* first time write, random junk */

	vcpu->hv_clock.version = guest_hv_clock.version + 1;
	kvm_write_guest_cached(v->kvm, &vcpu->pv_time,
				&vcpu->hv_clock,
				sizeof(vcpu->hv_clock.version));

	smp_wmb();

	/* retain PVCLOCK_GUEST_STOPPED if set in guest copy */
	vcpu->hv_clock.flags |= (guest_hv_clock.flags & PVCLOCK_GUEST_STOPPED);

	if (vcpu->pvclock_set_guest_stopped_request) {
		vcpu->hv_clock.flags |= PVCLOCK_GUEST_STOPPED;
		vcpu->pvclock_set_guest_stopped_request = false;
	}

	trace_kvm_pvclock_update(v->vcpu_id, &vcpu->hv_clock);

	kvm_write_guest_cached(v->kvm, &vcpu->pv_time,
				&vcpu->hv_clock,
				sizeof(vcpu->hv_clock));

	smp_wmb();

	vcpu->hv_clock.version++;
	kvm_write_guest_cached(v->kvm, &vcpu->pv_time,
				&vcpu->hv_clock,
				sizeof(vcpu->hv_clock.version));
}

static int kvm_guest_time_update(struct kvm_vcpu *v)
{
	unsigned long flags, tgt_tsc_khz;
	struct kvm_vcpu_arch *vcpu = &v->arch;
	struct kvm_arch *ka = &v->kvm->arch;
	s64 kernel_ns;
	u64 tsc_timestamp, host_tsc;
	u8 pvclock_flags;
	bool use_master_clock;

	kernel_ns = 0;
	host_tsc = 0;

	/*
	 * If the host uses TSC clock, then passthrough TSC as stable
	 * to the guest.
	 */
	spin_lock(&ka->pvclock_gtod_sync_lock);
	use_master_clock = ka->use_master_clock;
	if (use_master_clock) {
		host_tsc = ka->master_cycle_now;
		kernel_ns = ka->master_kernel_ns;
	}
	spin_unlock(&ka->pvclock_gtod_sync_lock);

	/* Keep irq disabled to prevent changes to the clock */
	local_irq_save(flags);
	tgt_tsc_khz = __this_cpu_read(cpu_tsc_khz);
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

	if (kvm_has_tsc_control)
		tgt_tsc_khz = kvm_scale_tsc(v, tgt_tsc_khz);

	if (unlikely(vcpu->hw_tsc_khz != tgt_tsc_khz)) {
		kvm_get_time_scale(NSEC_PER_SEC, tgt_tsc_khz * 1000LL,
				   &vcpu->hv_clock.tsc_shift,
				   &vcpu->hv_clock.tsc_to_system_mul);
		vcpu->hw_tsc_khz = tgt_tsc_khz;
	}

	vcpu->hv_clock.tsc_timestamp = tsc_timestamp;
	vcpu->hv_clock.system_time = kernel_ns + v->kvm->arch.kvmclock_offset;
	vcpu->last_guest_tsc = tsc_timestamp;

	/* If the host uses TSC clocksource, then it is stable */
	pvclock_flags = 0;
	if (use_master_clock)
		pvclock_flags |= PVCLOCK_TSC_STABLE_BIT;

	vcpu->hv_clock.flags = pvclock_flags;

	if (vcpu->pv_time_enabled)
		kvm_setup_pvclock_page(v);
	if (v == kvm_get_vcpu(v->kvm, 0))
		kvm_hv_setup_tsc_page(v->kvm, &vcpu->hv_clock);
	return 0;
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
	int i;
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

	if (!kvmclock_periodic_sync)
		return;

	schedule_delayed_work(&kvm->arch.kvmclock_update_work, 0);
	schedule_delayed_work(&kvm->arch.kvmclock_sync_work,
					KVMCLOCK_SYNC_PERIOD);
}

/*
 * On AMD, HWCR[McStatusWrEn] controls whether setting MCi_STATUS results in #GP.
 */
static bool can_set_mci_status(struct kvm_vcpu *vcpu)
{
	/* McStatusWrEn enabled? */
	if (guest_cpuid_is_amd_or_hygon(vcpu))
		return !!(vcpu->arch.msr_hwcr & BIT_ULL(18));

	return false;
}

static int set_msr_mce(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	u64 mcg_cap = vcpu->arch.mcg_cap;
	unsigned bank_num = mcg_cap & 0xff;
	u32 msr = msr_info->index;
	u64 data = msr_info->data;

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
	default:
		if (msr >= MSR_IA32_MC0_CTL &&
		    msr < MSR_IA32_MCx_CTL(bank_num)) {
			u32 offset = array_index_nospec(
				msr - MSR_IA32_MC0_CTL,
				MSR_IA32_MCx_CTL(bank_num) - MSR_IA32_MC0_CTL);

			/* only 0 or all 1s can be written to IA32_MCi_CTL
			 * some Linux kernels though clear bit 10 in bank 4 to
			 * workaround a BIOS/GART TBL issue on AMD K8s, ignore
			 * this to avoid an uncatched #GP in the guest
			 */
			if ((offset & 0x3) == 0 &&
			    data != 0 && (data | (1 << 10)) != ~(u64)0)
				return -1;

			/* MCi_STATUS */
			if (!msr_info->host_initiated &&
			    (offset & 0x3) == 1 && data != 0) {
				if (!can_set_mci_status(vcpu))
					return -1;
			}

			vcpu->arch.mce_banks[offset] = data;
			break;
		}
		return 1;
	}
	return 0;
}

static int xen_hvm_config(struct kvm_vcpu *vcpu, u64 data)
{
	struct kvm *kvm = vcpu->kvm;
	int lm = is_long_mode(vcpu);
	u8 *blob_addr = lm ? (u8 *)(long)kvm->arch.xen_hvm_config.blob_addr_64
		: (u8 *)(long)kvm->arch.xen_hvm_config.blob_addr_32;
	u8 blob_size = lm ? kvm->arch.xen_hvm_config.blob_size_64
		: kvm->arch.xen_hvm_config.blob_size_32;
	u32 page_num = data & ~PAGE_MASK;
	u64 page_addr = data & PAGE_MASK;
	u8 *page;

	if (page_num >= blob_size)
		return 1;

	page = memdup_user(blob_addr + (page_num * PAGE_SIZE), PAGE_SIZE);
	if (IS_ERR(page))
		return PTR_ERR(page);

	if (kvm_vcpu_write_guest(vcpu, page_addr, page, PAGE_SIZE)) {
		kfree(page);
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
	vcpu->arch.pv_time_enabled = false;
	vcpu->arch.time = 0;
}

static void kvm_vcpu_flush_tlb_all(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.tlb_flush;
	kvm_x86_ops.tlb_flush_all(vcpu);
}

static void kvm_vcpu_flush_tlb_guest(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.tlb_flush;
	kvm_x86_ops.tlb_flush_guest(vcpu);
}

static void record_steal_time(struct kvm_vcpu *vcpu)
{
	struct kvm_host_map map;
	struct kvm_steal_time *st;

	if (!(vcpu->arch.st.msr_val & KVM_MSR_ENABLED))
		return;

	/* -EAGAIN is returned in atomic context so we can just return. */
	if (kvm_map_gfn(vcpu, vcpu->arch.st.msr_val >> PAGE_SHIFT,
			&map, &vcpu->arch.st.cache, false))
		return;

	st = map.hva +
		offset_in_page(vcpu->arch.st.msr_val & KVM_STEAL_VALID_BITS);

	/*
	 * Doing a TLB flush here, on the guest's behalf, can avoid
	 * expensive IPIs.
	 */
	if (guest_pv_has(vcpu, KVM_FEATURE_PV_TLB_FLUSH)) {
		trace_kvm_pv_tlb_flush(vcpu->vcpu_id,
				       st->preempted & KVM_VCPU_FLUSH_TLB);
		if (xchg(&st->preempted, 0) & KVM_VCPU_FLUSH_TLB)
			kvm_vcpu_flush_tlb_guest(vcpu);
	}

	vcpu->arch.st.preempted = 0;

	if (st->version & 1)
		st->version += 1;  /* first time write, random junk */

	st->version += 1;

	smp_wmb();

	st->steal += current->sched_info.run_delay -
		vcpu->arch.st.last_steal;
	vcpu->arch.st.last_steal = current->sched_info.run_delay;

	smp_wmb();

	st->version += 1;

	kvm_unmap_gfn(vcpu, &map, &vcpu->arch.st.cache, true, false);
}

int kvm_set_msr_common(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	bool pr = false;
	u32 msr = msr_info->index;
	u64 data = msr_info->data;

	switch (msr) {
	case MSR_AMD64_NB_CFG:
	case MSR_IA32_UCODE_WRITE:
	case MSR_VM_HSAVE_PA:
	case MSR_AMD64_PATCH_LOADER:
	case MSR_AMD64_BU_CFG2:
	case MSR_AMD64_DC_CFG:
	case MSR_F15H_EX_CFG:
		break;

	case MSR_IA32_UCODE_REV:
		if (msr_info->host_initiated)
			vcpu->arch.microcode_version = data;
		break;
	case MSR_IA32_ARCH_CAPABILITIES:
		if (!msr_info->host_initiated)
			return 1;
		vcpu->arch.arch_capabilities = data;
		break;
	case MSR_IA32_PERF_CAPABILITIES: {
		struct kvm_msr_entry msr_ent = {.index = msr, .data = 0};

		if (!msr_info->host_initiated)
			return 1;
		if (guest_cpuid_has(vcpu, X86_FEATURE_PDCM) && kvm_get_msr_feature(&msr_ent))
			return 1;
		if (data & ~msr_ent.data)
			return 1;

		vcpu->arch.perf_capabilities = data;

		return 0;
		}
	case MSR_EFER:
		return set_efer(vcpu, msr_info);
	case MSR_K7_HWCR:
		data &= ~(u64)0x40;	/* ignore flush filter disable */
		data &= ~(u64)0x100;	/* ignore ignne emulation enable */
		data &= ~(u64)0x8;	/* ignore TLB cache disable */

		/* Handle McStatusWrEn */
		if (data == BIT_ULL(18)) {
			vcpu->arch.msr_hwcr = data;
		} else if (data != 0) {
			vcpu_unimpl(vcpu, "unimplemented HWCR wrmsr: 0x%llx\n",
				    data);
			return 1;
		}
		break;
	case MSR_FAM10H_MMIO_CONF_BASE:
		if (data != 0) {
			vcpu_unimpl(vcpu, "unimplemented MMIO_CONF_BASE wrmsr: "
				    "0x%llx\n", data);
			return 1;
		}
		break;
	case MSR_IA32_DEBUGCTLMSR:
		if (!data) {
			/* We support the non-activated case already */
			break;
		} else if (data & ~(DEBUGCTLMSR_LBR | DEBUGCTLMSR_BTF)) {
			/* Values other than LBR and BTF are vendor-specific,
			   thus reserved and should throw a #GP */
			return 1;
		} else if (report_ignored_msrs)
			vcpu_unimpl(vcpu, "%s: MSR_IA32_DEBUGCTLMSR 0x%llx, nop\n",
				    __func__, data);
		break;
	case 0x200 ... 0x2ff:
		return kvm_mtrr_set_msr(vcpu, msr, data);
	case MSR_IA32_APICBASE:
		return kvm_set_apic_base(vcpu, msr_info);
	case APIC_BASE_MSR ... APIC_BASE_MSR + 0xff:
		return kvm_x2apic_msr_write(vcpu, msr, data);
	case MSR_IA32_TSCDEADLINE:
		kvm_set_lapic_tscdeadline_msr(vcpu, data);
		break;
	case MSR_IA32_TSC_ADJUST:
		if (guest_cpuid_has(vcpu, X86_FEATURE_TSC_ADJUST)) {
			if (!msr_info->host_initiated) {
				s64 adj = data - vcpu->arch.ia32_tsc_adjust_msr;
				adjust_tsc_offset_guest(vcpu, adj);
			}
			vcpu->arch.ia32_tsc_adjust_msr = data;
		}
		break;
	case MSR_IA32_MISC_ENABLE:
		if (!kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_MISC_ENABLE_NO_MWAIT) &&
		    ((vcpu->arch.ia32_misc_enable_msr ^ data) & MSR_IA32_MISC_ENABLE_MWAIT)) {
			if (!guest_cpuid_has(vcpu, X86_FEATURE_XMM3))
				return 1;
			vcpu->arch.ia32_misc_enable_msr = data;
			kvm_update_cpuid_runtime(vcpu);
		} else {
			vcpu->arch.ia32_misc_enable_msr = data;
		}
		break;
	case MSR_IA32_SMBASE:
		if (!msr_info->host_initiated)
			return 1;
		vcpu->arch.smbase = data;
		break;
	case MSR_IA32_POWER_CTL:
		vcpu->arch.msr_ia32_power_ctl = data;
		break;
	case MSR_IA32_TSC:
		if (msr_info->host_initiated) {
			kvm_synchronize_tsc(vcpu, data);
		} else {
			u64 adj = kvm_compute_tsc_offset(vcpu, data) - vcpu->arch.l1_tsc_offset;
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
		if (data & ~supported_xss)
			return 1;
		vcpu->arch.ia32_xss = data;
		break;
	case MSR_SMI_COUNT:
		if (!msr_info->host_initiated)
			return 1;
		vcpu->arch.smi_count = data;
		break;
	case MSR_KVM_WALL_CLOCK_NEW:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE2))
			return 1;

		kvm_write_wall_clock(vcpu->kvm, data);
		break;
	case MSR_KVM_WALL_CLOCK:
		if (!guest_pv_has(vcpu, KVM_FEATURE_CLOCKSOURCE))
			return 1;

		kvm_write_wall_clock(vcpu->kvm, data);
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
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF))
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

		if (kvm_lapic_enable_pv_eoi(vcpu, data, sizeof(u8)))
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
		return set_msr_mce(vcpu, msr_info);

	case MSR_K7_PERFCTR0 ... MSR_K7_PERFCTR3:
	case MSR_P6_PERFCTR0 ... MSR_P6_PERFCTR1:
		pr = true;
		fallthrough;
	case MSR_K7_EVNTSEL0 ... MSR_K7_EVNTSEL3:
	case MSR_P6_EVNTSEL0 ... MSR_P6_EVNTSEL1:
		if (kvm_pmu_is_valid_msr(vcpu, msr))
			return kvm_pmu_set_msr(vcpu, msr_info);

		if (pr || data != 0)
			vcpu_unimpl(vcpu, "disabled perfctr wrmsr: "
				    "0x%x data 0x%llx\n", msr, data);
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
	case HV_X64_MSR_GUEST_OS_ID ... HV_X64_MSR_SINT15:
	case HV_X64_MSR_SYNDBG_CONTROL ... HV_X64_MSR_SYNDBG_PENDING_BUFFER:
	case HV_X64_MSR_SYNDBG_OPTIONS:
	case HV_X64_MSR_CRASH_P0 ... HV_X64_MSR_CRASH_P4:
	case HV_X64_MSR_CRASH_CTL:
	case HV_X64_MSR_STIMER0_CONFIG ... HV_X64_MSR_STIMER3_COUNT:
	case HV_X64_MSR_REENLIGHTENMENT_CONTROL:
	case HV_X64_MSR_TSC_EMULATION_CONTROL:
	case HV_X64_MSR_TSC_EMULATION_STATUS:
		return kvm_hv_set_msr_common(vcpu, msr, data,
					     msr_info->host_initiated);
	case MSR_IA32_BBL_CR_CTL3:
		/* Drop writes to this legacy MSR -- see rdmsr
		 * counterpart for further detail.
		 */
		if (report_ignored_msrs)
			vcpu_unimpl(vcpu, "ignored wrmsr: 0x%x data 0x%llx\n",
				msr, data);
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
		if (!msr_info->host_initiated ||
		    (!(data & MSR_PLATFORM_INFO_CPUID_FAULT) &&
		     cpuid_fault_enabled(vcpu)))
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
	default:
		if (msr && (msr == vcpu->kvm->arch.xen_hvm_config.msr))
			return xen_hvm_config(vcpu, data);
		if (kvm_pmu_is_valid_msr(vcpu, msr))
			return kvm_pmu_set_msr(vcpu, msr_info);
		return KVM_MSR_RET_INVALID;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_msr_common);

static int get_msr_mce(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata, bool host)
{
	u64 data;
	u64 mcg_cap = vcpu->arch.mcg_cap;
	unsigned bank_num = mcg_cap & 0xff;

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
	default:
		if (msr >= MSR_IA32_MC0_CTL &&
		    msr < MSR_IA32_MCx_CTL(bank_num)) {
			u32 offset = array_index_nospec(
				msr - MSR_IA32_MC0_CTL,
				MSR_IA32_MCx_CTL(bank_num) - MSR_IA32_MC0_CTL);

			data = vcpu->arch.mce_banks[offset];
			break;
		}
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
	case MSR_IA32_DEBUGCTLMSR:
	case MSR_IA32_LASTBRANCHFROMIP:
	case MSR_IA32_LASTBRANCHTOIP:
	case MSR_IA32_LASTINTFROMIP:
	case MSR_IA32_LASTINTTOIP:
	case MSR_K8_SYSCFG:
	case MSR_K8_TSEG_ADDR:
	case MSR_K8_TSEG_MASK:
	case MSR_VM_HSAVE_PA:
	case MSR_K8_INT_PENDING_MSG:
	case MSR_AMD64_NB_CFG:
	case MSR_FAM10H_MMIO_CONF_BASE:
	case MSR_AMD64_BU_CFG2:
	case MSR_IA32_PERF_CTL:
	case MSR_AMD64_DC_CFG:
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
	case MSR_F15H_PERF_CTL0 ... MSR_F15H_PERF_CTR5:
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
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_ARCH_CAPABILITIES))
			return 1;
		msr_info->data = vcpu->arch.arch_capabilities;
		break;
	case MSR_IA32_PERF_CAPABILITIES:
		if (!msr_info->host_initiated &&
		    !guest_cpuid_has(vcpu, X86_FEATURE_PDCM))
			return 1;
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
		u64 tsc_offset = msr_info->host_initiated ? vcpu->arch.l1_tsc_offset :
							    vcpu->arch.tsc_offset;

		msr_info->data = kvm_scale_tsc(vcpu, rdtsc()) + tsc_offset;
		break;
	}
	case MSR_MTRRcap:
	case 0x200 ... 0x2ff:
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
		msr_info->data = kvm_get_apic_base(vcpu);
		break;
	case APIC_BASE_MSR ... APIC_BASE_MSR + 0xff:
		return kvm_x2apic_msr_read(vcpu, msr_info->index, &msr_info->data);
	case MSR_IA32_TSCDEADLINE:
		msr_info->data = kvm_get_lapic_tscdeadline_msr(vcpu);
		break;
	case MSR_IA32_TSC_ADJUST:
		msr_info->data = (u64)vcpu->arch.ia32_tsc_adjust_msr;
		break;
	case MSR_IA32_MISC_ENABLE:
		msr_info->data = vcpu->arch.ia32_misc_enable_msr;
		break;
	case MSR_IA32_SMBASE:
		if (!msr_info->host_initiated)
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
		if (!guest_pv_has(vcpu, KVM_FEATURE_ASYNC_PF))
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
	case HV_X64_MSR_GUEST_OS_ID ... HV_X64_MSR_SINT15:
	case HV_X64_MSR_SYNDBG_CONTROL ... HV_X64_MSR_SYNDBG_PENDING_BUFFER:
	case HV_X64_MSR_SYNDBG_OPTIONS:
	case HV_X64_MSR_CRASH_P0 ... HV_X64_MSR_CRASH_P4:
	case HV_X64_MSR_CRASH_CTL:
	case HV_X64_MSR_STIMER0_CONFIG ... HV_X64_MSR_STIMER3_COUNT:
	case HV_X64_MSR_REENLIGHTENMENT_CONTROL:
	case HV_X64_MSR_TSC_EMULATION_CONTROL:
	case HV_X64_MSR_TSC_EMULATION_STATUS:
		return kvm_hv_get_msr_common(vcpu,
					     msr_info->index, &msr_info->data,
					     msr_info->host_initiated);
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
	default:
		if (kvm_pmu_is_valid_msr(vcpu, msr_info->index))
			return kvm_pmu_get_msr(vcpu, msr_info);
		return KVM_MSR_RET_INVALID;
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
	int r, n;
	unsigned size;

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

	r = n = __msr_io(vcpu, &msrs, entries, do_msr);
	if (r < 0)
		goto out_free;

	r = -EFAULT;
	if (writeback && copy_to_user(user_msrs->entries, entries, size))
		goto out_free;

	r = n;

out_free:
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
	case KVM_CAP_XEN_HVM:
	case KVM_CAP_VCPU_EVENTS:
	case KVM_CAP_HYPERV:
	case KVM_CAP_HYPERV_VAPIC:
	case KVM_CAP_HYPERV_SPIN:
	case KVM_CAP_HYPERV_SYNIC:
	case KVM_CAP_HYPERV_SYNIC2:
	case KVM_CAP_HYPERV_VP_INDEX:
	case KVM_CAP_HYPERV_EVENTFD:
	case KVM_CAP_HYPERV_TLBFLUSH:
	case KVM_CAP_HYPERV_SEND_IPI:
	case KVM_CAP_HYPERV_CPUID:
	case KVM_CAP_SYS_HYPERV_CPUID:
	case KVM_CAP_PCI_SEGMENT:
	case KVM_CAP_DEBUGREGS:
	case KVM_CAP_X86_ROBUST_SINGLESTEP:
	case KVM_CAP_XSAVE:
	case KVM_CAP_ASYNC_PF:
	case KVM_CAP_ASYNC_PF_INT:
	case KVM_CAP_GET_TSC_KHZ:
	case KVM_CAP_KVMCLOCK_CTRL:
	case KVM_CAP_READONLY_MEM:
	case KVM_CAP_HYPERV_TIME:
	case KVM_CAP_IOAPIC_POLARITY_IGNORED:
	case KVM_CAP_TSC_DEADLINE_TIMER:
	case KVM_CAP_DISABLE_QUIRKS:
	case KVM_CAP_SET_BOOT_CPU_ID:
 	case KVM_CAP_SPLIT_IRQCHIP:
	case KVM_CAP_IMMEDIATE_EXIT:
	case KVM_CAP_PMU_EVENT_FILTER:
	case KVM_CAP_GET_MSR_FEATURES:
	case KVM_CAP_MSR_PLATFORM_INFO:
	case KVM_CAP_EXCEPTION_PAYLOAD:
	case KVM_CAP_SET_GUEST_DEBUG:
	case KVM_CAP_LAST_CPU:
	case KVM_CAP_X86_USER_SPACE_MSR:
	case KVM_CAP_X86_MSR_FILTER:
	case KVM_CAP_ENFORCE_PV_FEATURE_CPUID:
		r = 1;
		break;
	case KVM_CAP_SYNC_REGS:
		r = KVM_SYNC_X86_VALID_FIELDS;
		break;
	case KVM_CAP_ADJUST_CLOCK:
		r = KVM_CLOCK_TSC_STABLE;
		break;
	case KVM_CAP_X86_DISABLE_EXITS:
		r |=  KVM_X86_DISABLE_EXITS_HLT | KVM_X86_DISABLE_EXITS_PAUSE |
		      KVM_X86_DISABLE_EXITS_CSTATE;
		if(kvm_can_mwait_in_guest())
			r |= KVM_X86_DISABLE_EXITS_MWAIT;
		break;
	case KVM_CAP_X86_SMM:
		/* SMBASE is usually relocated above 1M on modern chipsets,
		 * and SMM handlers might indeed rely on 4G segment limits,
		 * so do not report SMM to be available if real mode is
		 * emulated via vm86 mode.  Still, do not go to great lengths
		 * to avoid userspace's usage of the feature, because it is a
		 * fringe case that is not enabled except via specific settings
		 * of the module parameters.
		 */
		r = kvm_x86_ops.has_emulated_msr(kvm, MSR_IA32_SMBASE);
		break;
	case KVM_CAP_VAPIC:
		r = !kvm_x86_ops.cpu_has_accelerated_tpr();
		break;
	case KVM_CAP_NR_VCPUS:
		r = KVM_SOFT_MAX_VCPUS;
		break;
	case KVM_CAP_MAX_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
	case KVM_CAP_MAX_VCPU_ID:
		r = KVM_MAX_VCPU_ID;
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
		r = kvm_has_tsc_control;
		break;
	case KVM_CAP_X2APIC_API:
		r = KVM_X2APIC_API_VALID_FLAGS;
		break;
	case KVM_CAP_NESTED_STATE:
		r = kvm_x86_ops.nested_ops->get_state ?
			kvm_x86_ops.nested_ops->get_state(NULL, NULL, 0) : 0;
		break;
	case KVM_CAP_HYPERV_DIRECT_TLBFLUSH:
		r = kvm_x86_ops.enable_direct_tlbflush != NULL;
		break;
	case KVM_CAP_HYPERV_ENLIGHTENED_VMCS:
		r = kvm_x86_ops.nested_ops->enable_evmcs != NULL;
		break;
	case KVM_CAP_SMALLER_MAXPHYADDR:
		r = (int) allow_smaller_maxphyaddr;
		break;
	case KVM_CAP_STEAL_TIME:
		r = sched_info_on();
		break;
	default:
		break;
	}
	return r;

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
		if (copy_to_user(argp, &kvm_mce_cap_supported,
				 sizeof(kvm_mce_cap_supported)))
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
		r = msr_io(NULL, argp, do_get_msr_feature, 1);
		break;
	case KVM_GET_SUPPORTED_HV_CPUID:
		r = kvm_ioctl_get_supported_hv_cpuid(NULL, argp);
		break;
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
	/* Address WBINVD may be executed by guest */
	if (need_emulate_wbinvd(vcpu)) {
		if (kvm_x86_ops.has_wbinvd_exit())
			cpumask_set_cpu(cpu, vcpu->arch.wbinvd_dirty_mask);
		else if (vcpu->cpu != -1 && vcpu->cpu != cpu)
			smp_call_function_single(vcpu->cpu,
					wbinvd_ipi, NULL, 1);
	}

	kvm_x86_ops.vcpu_load(vcpu, cpu);

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
			u64 offset = kvm_compute_tsc_offset(vcpu,
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
	struct kvm_host_map map;
	struct kvm_steal_time *st;

	if (!(vcpu->arch.st.msr_val & KVM_MSR_ENABLED))
		return;

	if (vcpu->arch.st.preempted)
		return;

	if (kvm_map_gfn(vcpu, vcpu->arch.st.msr_val >> PAGE_SHIFT, &map,
			&vcpu->arch.st.cache, true))
		return;

	st = map.hva +
		offset_in_page(vcpu->arch.st.msr_val & KVM_STEAL_VALID_BITS);

	st->preempted = vcpu->arch.st.preempted = KVM_VCPU_PREEMPTED;

	kvm_unmap_gfn(vcpu, &map, &vcpu->arch.st.cache, true, true);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	int idx;

	if (vcpu->preempted && !vcpu->arch.guest_state_protected)
		vcpu->arch.preempted_in_kernel = !kvm_x86_ops.get_cpl(vcpu);

	/*
	 * Disable page faults because we're in atomic context here.
	 * kvm_write_guest_offset_cached() would call might_fault()
	 * that relies on pagefault_disable() to tell if there's a
	 * bug. NOTE: the write to guest memory may not go through if
	 * during postcopy live migration or if there's heavy guest
	 * paging.
	 */
	pagefault_disable();
	/*
	 * kvm_memslots() will be called by
	 * kvm_write_guest_offset_cached() so take the srcu lock.
	 */
	idx = srcu_read_lock(&vcpu->kvm->srcu);
	kvm_steal_time_set_preempted(vcpu);
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	pagefault_enable();
	kvm_x86_ops.vcpu_put(vcpu);
	vcpu->arch.last_host_tsc = rdtsc();
	/*
	 * If userspace has set any breakpoints or watchpoints, dr6 is restored
	 * on every vmexit, but if not, we might have a stale dr6 from the
	 * guest. do_debug expects dr6 to be cleared after it runs, do the same.
	 */
	set_debugreg(0, 6);
}

static int kvm_vcpu_ioctl_get_lapic(struct kvm_vcpu *vcpu,
				    struct kvm_lapic_state *s)
{
	if (vcpu->arch.apicv_active)
		kvm_x86_ops.sync_pir_to_irr(vcpu);

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
	return kvm_arch_interrupt_allowed(vcpu) &&
		kvm_cpu_accept_dm_intr(vcpu);
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

static int kvm_vcpu_ioctl_smi(struct kvm_vcpu *vcpu)
{
	kvm_make_request(KVM_REQ_SMI, vcpu);

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
	if (mcg_cap & ~(kvm_mce_cap_supported | 0xff | 0xff0000))
		goto out;
	r = 0;
	vcpu->arch.mcg_cap = mcg_cap;
	/* Init IA32_MCG_CTL to all 1s */
	if (mcg_cap & MCG_CTL_P)
		vcpu->arch.mcg_ctl = ~(u64)0;
	/* Init IA32_MCi_CTL to all 1s */
	for (bank = 0; bank < bank_num; bank++)
		vcpu->arch.mce_banks[bank*4] = ~(u64)0;

	kvm_x86_ops.setup_mce(vcpu);
out:
	return r;
}

static int kvm_vcpu_ioctl_x86_set_mce(struct kvm_vcpu *vcpu,
				      struct kvm_x86_mce *mce)
{
	u64 mcg_cap = vcpu->arch.mcg_cap;
	unsigned bank_num = mcg_cap & 0xff;
	u64 *banks = vcpu->arch.mce_banks;

	if (mce->bank >= bank_num || !(mce->status & MCI_STATUS_VAL))
		return -EINVAL;
	/*
	 * if IA32_MCG_CTL is not all 1s, the uncorrected error
	 * reporting is disabled
	 */
	if ((mce->status & MCI_STATUS_UC) && (mcg_cap & MCG_CTL_P) &&
	    vcpu->arch.mcg_ctl != ~(u64)0)
		return 0;
	banks += 4 * mce->bank;
	/*
	 * if IA32_MCi_CTL is not all 1s, the uncorrected error
	 * reporting is disabled for the bank
	 */
	if ((mce->status & MCI_STATUS_UC) && banks[0] != ~(u64)0)
		return 0;
	if (mce->status & MCI_STATUS_UC) {
		if ((vcpu->arch.mcg_status & MCG_STATUS_MCIP) ||
		    !kvm_read_cr4_bits(vcpu, X86_CR4_MCE)) {
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
	process_nmi(vcpu);

	if (kvm_check_request(KVM_REQ_SMI, vcpu))
		process_smi(vcpu);

	/*
	 * In guest mode, payload delivery should be deferred,
	 * so that the L1 hypervisor can intercept #PF before
	 * CR2 is modified (or intercept #DB before DR6 is
	 * modified under nVMX). Unless the per-VM capability,
	 * KVM_CAP_EXCEPTION_PAYLOAD, is set, we may not defer the delivery of
	 * an exception payload and handle after a KVM_GET_VCPU_EVENTS. Since we
	 * opportunistically defer the exception payload, deliver it if the
	 * capability hasn't been requested before processing a
	 * KVM_GET_VCPU_EVENTS.
	 */
	if (!vcpu->kvm->arch.exception_payload_enabled &&
	    vcpu->arch.exception.pending && vcpu->arch.exception.has_payload)
		kvm_deliver_exception_payload(vcpu);

	/*
	 * The API doesn't provide the instruction length for software
	 * exceptions, so don't report them. As long as the guest RIP
	 * isn't advanced, we should expect to encounter the exception
	 * again.
	 */
	if (kvm_exception_is_soft(vcpu->arch.exception.nr)) {
		events->exception.injected = 0;
		events->exception.pending = 0;
	} else {
		events->exception.injected = vcpu->arch.exception.injected;
		events->exception.pending = vcpu->arch.exception.pending;
		/*
		 * For ABI compatibility, deliberately conflate
		 * pending and injected exceptions when
		 * KVM_CAP_EXCEPTION_PAYLOAD isn't enabled.
		 */
		if (!vcpu->kvm->arch.exception_payload_enabled)
			events->exception.injected |=
				vcpu->arch.exception.pending;
	}
	events->exception.nr = vcpu->arch.exception.nr;
	events->exception.has_error_code = vcpu->arch.exception.has_error_code;
	events->exception.error_code = vcpu->arch.exception.error_code;
	events->exception_has_payload = vcpu->arch.exception.has_payload;
	events->exception_payload = vcpu->arch.exception.payload;

	events->interrupt.injected =
		vcpu->arch.interrupt.injected && !vcpu->arch.interrupt.soft;
	events->interrupt.nr = vcpu->arch.interrupt.nr;
	events->interrupt.soft = 0;
	events->interrupt.shadow = kvm_x86_ops.get_interrupt_shadow(vcpu);

	events->nmi.injected = vcpu->arch.nmi_injected;
	events->nmi.pending = vcpu->arch.nmi_pending != 0;
	events->nmi.masked = kvm_x86_ops.get_nmi_mask(vcpu);
	events->nmi.pad = 0;

	events->sipi_vector = 0; /* never valid when reporting to user space */

	events->smi.smm = is_smm(vcpu);
	events->smi.pending = vcpu->arch.smi_pending;
	events->smi.smm_inside_nmi =
		!!(vcpu->arch.hflags & HF_SMM_INSIDE_NMI_MASK);
	events->smi.latched_init = kvm_lapic_latched_init(vcpu);

	events->flags = (KVM_VCPUEVENT_VALID_NMI_PENDING
			 | KVM_VCPUEVENT_VALID_SHADOW
			 | KVM_VCPUEVENT_VALID_SMM);
	if (vcpu->kvm->arch.exception_payload_enabled)
		events->flags |= KVM_VCPUEVENT_VALID_PAYLOAD;

	memset(&events->reserved, 0, sizeof(events->reserved));
}

static void kvm_smm_changed(struct kvm_vcpu *vcpu);

static int kvm_vcpu_ioctl_x86_set_vcpu_events(struct kvm_vcpu *vcpu,
					      struct kvm_vcpu_events *events)
{
	if (events->flags & ~(KVM_VCPUEVENT_VALID_NMI_PENDING
			      | KVM_VCPUEVENT_VALID_SIPI_VECTOR
			      | KVM_VCPUEVENT_VALID_SHADOW
			      | KVM_VCPUEVENT_VALID_SMM
			      | KVM_VCPUEVENT_VALID_PAYLOAD))
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
	vcpu->arch.exception.injected = events->exception.injected;
	vcpu->arch.exception.pending = events->exception.pending;
	vcpu->arch.exception.nr = events->exception.nr;
	vcpu->arch.exception.has_error_code = events->exception.has_error_code;
	vcpu->arch.exception.error_code = events->exception.error_code;
	vcpu->arch.exception.has_payload = events->exception_has_payload;
	vcpu->arch.exception.payload = events->exception_payload;

	vcpu->arch.interrupt.injected = events->interrupt.injected;
	vcpu->arch.interrupt.nr = events->interrupt.nr;
	vcpu->arch.interrupt.soft = events->interrupt.soft;
	if (events->flags & KVM_VCPUEVENT_VALID_SHADOW)
		kvm_x86_ops.set_interrupt_shadow(vcpu,
						  events->interrupt.shadow);

	vcpu->arch.nmi_injected = events->nmi.injected;
	if (events->flags & KVM_VCPUEVENT_VALID_NMI_PENDING)
		vcpu->arch.nmi_pending = events->nmi.pending;
	kvm_x86_ops.set_nmi_mask(vcpu, events->nmi.masked);

	if (events->flags & KVM_VCPUEVENT_VALID_SIPI_VECTOR &&
	    lapic_in_kernel(vcpu))
		vcpu->arch.apic->sipi_vector = events->sipi_vector;

	if (events->flags & KVM_VCPUEVENT_VALID_SMM) {
		if (!!(vcpu->arch.hflags & HF_SMM_MASK) != events->smi.smm) {
			if (events->smi.smm)
				vcpu->arch.hflags |= HF_SMM_MASK;
			else
				vcpu->arch.hflags &= ~HF_SMM_MASK;
			kvm_smm_changed(vcpu);
		}

		vcpu->arch.smi_pending = events->smi.pending;

		if (events->smi.smm) {
			if (events->smi.smm_inside_nmi)
				vcpu->arch.hflags |= HF_SMM_INSIDE_NMI_MASK;
			else
				vcpu->arch.hflags &= ~HF_SMM_INSIDE_NMI_MASK;
		}

		if (lapic_in_kernel(vcpu)) {
			if (events->smi.latched_init)
				set_bit(KVM_APIC_INIT, &vcpu->arch.apic->pending_events);
			else
				clear_bit(KVM_APIC_INIT, &vcpu->arch.apic->pending_events);
		}
	}

	kvm_make_request(KVM_REQ_EVENT, vcpu);

	return 0;
}

static void kvm_vcpu_ioctl_x86_get_debugregs(struct kvm_vcpu *vcpu,
					     struct kvm_debugregs *dbgregs)
{
	unsigned long val;

	memcpy(dbgregs->db, vcpu->arch.db, sizeof(vcpu->arch.db));
	kvm_get_dr(vcpu, 6, &val);
	dbgregs->dr6 = val;
	dbgregs->dr7 = vcpu->arch.dr7;
	dbgregs->flags = 0;
	memset(&dbgregs->reserved, 0, sizeof(dbgregs->reserved));
}

static int kvm_vcpu_ioctl_x86_set_debugregs(struct kvm_vcpu *vcpu,
					    struct kvm_debugregs *dbgregs)
{
	if (dbgregs->flags)
		return -EINVAL;

	if (dbgregs->dr6 & ~0xffffffffull)
		return -EINVAL;
	if (dbgregs->dr7 & ~0xffffffffull)
		return -EINVAL;

	memcpy(vcpu->arch.db, dbgregs->db, sizeof(vcpu->arch.db));
	kvm_update_dr0123(vcpu);
	vcpu->arch.dr6 = dbgregs->dr6;
	vcpu->arch.dr7 = dbgregs->dr7;
	kvm_update_dr7(vcpu);

	return 0;
}

#define XSTATE_COMPACTION_ENABLED (1ULL << 63)

static void fill_xsave(u8 *dest, struct kvm_vcpu *vcpu)
{
	struct xregs_state *xsave = &vcpu->arch.guest_fpu->state.xsave;
	u64 xstate_bv = xsave->header.xfeatures;
	u64 valid;

	/*
	 * Copy legacy XSAVE area, to avoid complications with CPUID
	 * leaves 0 and 1 in the loop below.
	 */
	memcpy(dest, xsave, XSAVE_HDR_OFFSET);

	/* Set XSTATE_BV */
	xstate_bv &= vcpu->arch.guest_supported_xcr0 | XFEATURE_MASK_FPSSE;
	*(u64 *)(dest + XSAVE_HDR_OFFSET) = xstate_bv;

	/*
	 * Copy each region from the possibly compacted offset to the
	 * non-compacted offset.
	 */
	valid = xstate_bv & ~XFEATURE_MASK_FPSSE;
	while (valid) {
		u64 xfeature_mask = valid & -valid;
		int xfeature_nr = fls64(xfeature_mask) - 1;
		void *src = get_xsave_addr(xsave, xfeature_nr);

		if (src) {
			u32 size, offset, ecx, edx;
			cpuid_count(XSTATE_CPUID, xfeature_nr,
				    &size, &offset, &ecx, &edx);
			if (xfeature_nr == XFEATURE_PKRU)
				memcpy(dest + offset, &vcpu->arch.pkru,
				       sizeof(vcpu->arch.pkru));
			else
				memcpy(dest + offset, src, size);

		}

		valid -= xfeature_mask;
	}
}

static void load_xsave(struct kvm_vcpu *vcpu, u8 *src)
{
	struct xregs_state *xsave = &vcpu->arch.guest_fpu->state.xsave;
	u64 xstate_bv = *(u64 *)(src + XSAVE_HDR_OFFSET);
	u64 valid;

	/*
	 * Copy legacy XSAVE area, to avoid complications with CPUID
	 * leaves 0 and 1 in the loop below.
	 */
	memcpy(xsave, src, XSAVE_HDR_OFFSET);

	/* Set XSTATE_BV and possibly XCOMP_BV.  */
	xsave->header.xfeatures = xstate_bv;
	if (boot_cpu_has(X86_FEATURE_XSAVES))
		xsave->header.xcomp_bv = host_xcr0 | XSTATE_COMPACTION_ENABLED;

	/*
	 * Copy each region from the non-compacted offset to the
	 * possibly compacted offset.
	 */
	valid = xstate_bv & ~XFEATURE_MASK_FPSSE;
	while (valid) {
		u64 xfeature_mask = valid & -valid;
		int xfeature_nr = fls64(xfeature_mask) - 1;
		void *dest = get_xsave_addr(xsave, xfeature_nr);

		if (dest) {
			u32 size, offset, ecx, edx;
			cpuid_count(XSTATE_CPUID, xfeature_nr,
				    &size, &offset, &ecx, &edx);
			if (xfeature_nr == XFEATURE_PKRU)
				memcpy(&vcpu->arch.pkru, src + offset,
				       sizeof(vcpu->arch.pkru));
			else
				memcpy(dest, src + offset, size);
		}

		valid -= xfeature_mask;
	}
}

static void kvm_vcpu_ioctl_x86_get_xsave(struct kvm_vcpu *vcpu,
					 struct kvm_xsave *guest_xsave)
{
	if (!vcpu->arch.guest_fpu)
		return;

	if (boot_cpu_has(X86_FEATURE_XSAVE)) {
		memset(guest_xsave, 0, sizeof(struct kvm_xsave));
		fill_xsave((u8 *) guest_xsave->region, vcpu);
	} else {
		memcpy(guest_xsave->region,
			&vcpu->arch.guest_fpu->state.fxsave,
			sizeof(struct fxregs_state));
		*(u64 *)&guest_xsave->region[XSAVE_HDR_OFFSET / sizeof(u32)] =
			XFEATURE_MASK_FPSSE;
	}
}

#define XSAVE_MXCSR_OFFSET 24

static int kvm_vcpu_ioctl_x86_set_xsave(struct kvm_vcpu *vcpu,
					struct kvm_xsave *guest_xsave)
{
	u64 xstate_bv;
	u32 mxcsr;

	if (!vcpu->arch.guest_fpu)
		return 0;

	xstate_bv = *(u64 *)&guest_xsave->region[XSAVE_HDR_OFFSET / sizeof(u32)];
	mxcsr = *(u32 *)&guest_xsave->region[XSAVE_MXCSR_OFFSET / sizeof(u32)];

	if (boot_cpu_has(X86_FEATURE_XSAVE)) {
		/*
		 * Here we allow setting states that are not present in
		 * CPUID leaf 0xD, index 0, EDX:EAX.  This is for compatibility
		 * with old userspace.
		 */
		if (xstate_bv & ~supported_xcr0 || mxcsr & ~mxcsr_feature_mask)
			return -EINVAL;
		load_xsave(vcpu, (u8 *)guest_xsave->region);
	} else {
		if (xstate_bv & ~XFEATURE_MASK_FPSSE ||
			mxcsr & ~mxcsr_feature_mask)
			return -EINVAL;
		memcpy(&vcpu->arch.guest_fpu->state.fxsave,
			guest_xsave->region, sizeof(struct fxregs_state));
	}
	return 0;
}

static void kvm_vcpu_ioctl_x86_get_xcrs(struct kvm_vcpu *vcpu,
					struct kvm_xcrs *guest_xcrs)
{
	if (!boot_cpu_has(X86_FEATURE_XSAVE)) {
		guest_xcrs->nr_xcrs = 0;
		return;
	}

	guest_xcrs->nr_xcrs = 1;
	guest_xcrs->flags = 0;
	guest_xcrs->xcrs[0].xcr = XCR_XFEATURE_ENABLED_MASK;
	guest_xcrs->xcrs[0].value = vcpu->arch.xcr0;
}

static int kvm_vcpu_ioctl_x86_set_xcrs(struct kvm_vcpu *vcpu,
				       struct kvm_xcrs *guest_xcrs)
{
	int i, r = 0;

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
	if (!vcpu->arch.pv_time_enabled)
		return -EINVAL;
	vcpu->arch.pvclock_set_guest_stopped_request = true;
	kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
	return 0;
}

static int kvm_vcpu_ioctl_enable_cap(struct kvm_vcpu *vcpu,
				     struct kvm_enable_cap *cap)
{
	int r;
	uint16_t vmcs_version;
	void __user *user_ptr;

	if (cap->flags)
		return -EINVAL;

	switch (cap->cap) {
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
	case KVM_CAP_HYPERV_DIRECT_TLBFLUSH:
		if (!kvm_x86_ops.enable_direct_tlbflush)
			return -ENOTTY;

		return kvm_x86_ops.enable_direct_tlbflush(vcpu);

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
		u.lapic = kzalloc(sizeof(struct kvm_lapic_state),
				GFP_KERNEL_ACCOUNT);

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
		r = kvm_vcpu_ioctl_smi(vcpu);
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

		r = kvm_vcpu_ioctl_x86_set_vcpu_events(vcpu, &events);
		break;
	}
	case KVM_GET_DEBUGREGS: {
		struct kvm_debugregs dbgregs;

		kvm_vcpu_ioctl_x86_get_debugregs(vcpu, &dbgregs);

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
		u.xsave = kzalloc(sizeof(struct kvm_xsave), GFP_KERNEL_ACCOUNT);
		r = -ENOMEM;
		if (!u.xsave)
			break;

		kvm_vcpu_ioctl_x86_get_xsave(vcpu, u.xsave);

		r = -EFAULT;
		if (copy_to_user(argp, u.xsave, sizeof(struct kvm_xsave)))
			break;
		r = 0;
		break;
	}
	case KVM_SET_XSAVE: {
		u.xsave = memdup_user(argp, sizeof(*u.xsave));
		if (IS_ERR(u.xsave)) {
			r = PTR_ERR(u.xsave);
			goto out_nofree;
		}

		r = kvm_vcpu_ioctl_x86_set_xsave(vcpu, u.xsave);
		break;
	}
	case KVM_GET_XCRS: {
		u.xcrs = kzalloc(sizeof(struct kvm_xcrs), GFP_KERNEL_ACCOUNT);
		r = -ENOMEM;
		if (!u.xcrs)
			break;

		kvm_vcpu_ioctl_x86_get_xcrs(vcpu, u.xcrs);

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

		if (kvm_has_tsc_control &&
		    user_tsc_khz >= kvm_max_guest_tsc_khz)
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
	case KVM_GET_SUPPORTED_HV_CPUID:
		r = kvm_ioctl_get_supported_hv_cpuid(vcpu, argp);
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
	ret = kvm_x86_ops.set_tss_addr(kvm, addr);
	return ret;
}

static int kvm_vm_ioctl_set_identity_map_addr(struct kvm *kvm,
					      u64 ident_addr)
{
	return kvm_x86_ops.set_identity_map_addr(kvm, ident_addr);
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

static unsigned long kvm_vm_ioctl_get_nr_mmu_pages(struct kvm *kvm)
{
	return kvm->arch.n_max_mmu_pages;
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
	 * Flush potentially hardware-cached dirty pages to dirty_bitmap.
	 */
	if (kvm_x86_ops.flush_log_dirty)
		kvm_x86_ops.flush_log_dirty(kvm);
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
		r = kvm_setup_empty_irq_routing(kvm);
		if (r)
			goto split_irqchip_unlock;
		/* Pairs with irqchip_in_kernel. */
		smp_wmb();
		kvm->arch.irqchip_mode = KVM_IRQCHIP_SPLIT;
		kvm->arch.nr_reserved_ioapic_pins = cap->args[0];
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

		if ((cap->args[0] & KVM_X86_DISABLE_EXITS_MWAIT) &&
			kvm_can_mwait_in_guest())
			kvm->arch.mwait_in_guest = true;
		if (cap->args[0] & KVM_X86_DISABLE_EXITS_HLT)
			kvm->arch.hlt_in_guest = true;
		if (cap->args[0] & KVM_X86_DISABLE_EXITS_PAUSE)
			kvm->arch.pause_in_guest = true;
		if (cap->args[0] & KVM_X86_DISABLE_EXITS_CSTATE)
			kvm->arch.cstate_in_guest = true;
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
	case KVM_CAP_X86_USER_SPACE_MSR:
		kvm->arch.user_space_msr_mask = cap->args[0];
		r = 0;
		break;
	default:
		r = -EINVAL;
		break;
	}
	return r;
}

static void kvm_clear_msr_filter(struct kvm *kvm)
{
	u32 i;
	u32 count = kvm->arch.msr_filter.count;
	struct msr_bitmap_range ranges[16];

	mutex_lock(&kvm->lock);
	kvm->arch.msr_filter.count = 0;
	memcpy(ranges, kvm->arch.msr_filter.ranges, count * sizeof(ranges[0]));
	mutex_unlock(&kvm->lock);
	synchronize_srcu(&kvm->srcu);

	for (i = 0; i < count; i++)
		kfree(ranges[i].bitmap);
}

static int kvm_add_msr_filter(struct kvm *kvm, struct kvm_msr_filter_range *user_range)
{
	struct msr_bitmap_range *ranges = kvm->arch.msr_filter.ranges;
	struct msr_bitmap_range range;
	unsigned long *bitmap = NULL;
	size_t bitmap_size;
	int r;

	if (!user_range->nmsrs)
		return 0;

	bitmap_size = BITS_TO_LONGS(user_range->nmsrs) * sizeof(long);
	if (!bitmap_size || bitmap_size > KVM_MSR_FILTER_MAX_BITMAP_SIZE)
		return -EINVAL;

	bitmap = memdup_user((__user u8*)user_range->bitmap, bitmap_size);
	if (IS_ERR(bitmap))
		return PTR_ERR(bitmap);

	range = (struct msr_bitmap_range) {
		.flags = user_range->flags,
		.base = user_range->base,
		.nmsrs = user_range->nmsrs,
		.bitmap = bitmap,
	};

	if (range.flags & ~(KVM_MSR_FILTER_READ | KVM_MSR_FILTER_WRITE)) {
		r = -EINVAL;
		goto err;
	}

	if (!range.flags) {
		r = -EINVAL;
		goto err;
	}

	/* Everything ok, add this range identifier to our global pool */
	ranges[kvm->arch.msr_filter.count] = range;
	/* Make sure we filled the array before we tell anyone to walk it */
	smp_wmb();
	kvm->arch.msr_filter.count++;

	return 0;
err:
	kfree(bitmap);
	return r;
}

static int kvm_vm_ioctl_set_msr_filter(struct kvm *kvm, void __user *argp)
{
	struct kvm_msr_filter __user *user_msr_filter = argp;
	struct kvm_msr_filter filter;
	bool default_allow;
	int r = 0;
	bool empty = true;
	u32 i;

	if (copy_from_user(&filter, user_msr_filter, sizeof(filter)))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(filter.ranges); i++)
		empty &= !filter.ranges[i].nmsrs;

	default_allow = !(filter.flags & KVM_MSR_FILTER_DEFAULT_DENY);
	if (empty && !default_allow)
		return -EINVAL;

	kvm_clear_msr_filter(kvm);

	kvm->arch.msr_filter.default_allow = default_allow;

	/*
	 * Protect from concurrent calls to this function that could trigger
	 * a TOCTOU violation on kvm->arch.msr_filter.count.
	 */
	mutex_lock(&kvm->lock);
	for (i = 0; i < ARRAY_SIZE(filter.ranges); i++) {
		r = kvm_add_msr_filter(kvm, &filter.ranges[i]);
		if (r)
			break;
	}

	kvm_make_all_cpus_request(kvm, KVM_REQ_MSR_FILTER_CHANGED);
	mutex_unlock(&kvm->lock);

	return r;
}

long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg)
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
	case KVM_GET_NR_MMU_PAGES:
		r = kvm_vm_ioctl_get_nr_mmu_pages(kvm);
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
		else
			kvm->arch.bsp_vcpu_id = arg;
		mutex_unlock(&kvm->lock);
		break;
	case KVM_XEN_HVM_CONFIG: {
		struct kvm_xen_hvm_config xhc;
		r = -EFAULT;
		if (copy_from_user(&xhc, argp, sizeof(xhc)))
			goto out;
		r = -EINVAL;
		if (xhc.flags)
			goto out;
		memcpy(&kvm->arch.xen_hvm_config, &xhc, sizeof(xhc));
		r = 0;
		break;
	}
	case KVM_SET_CLOCK: {
		struct kvm_clock_data user_ns;
		u64 now_ns;

		r = -EFAULT;
		if (copy_from_user(&user_ns, argp, sizeof(user_ns)))
			goto out;

		r = -EINVAL;
		if (user_ns.flags)
			goto out;

		r = 0;
		/*
		 * TODO: userspace has to take care of races with VCPU_RUN, so
		 * kvm_gen_update_masterclock() can be cut down to locked
		 * pvclock_update_vm_gtod_copy().
		 */
		kvm_gen_update_masterclock(kvm);
		now_ns = get_kvmclock_ns(kvm);
		kvm->arch.kvmclock_offset += user_ns.clock - now_ns;
		kvm_make_all_cpus_request(kvm, KVM_REQ_CLOCK_UPDATE);
		break;
	}
	case KVM_GET_CLOCK: {
		struct kvm_clock_data user_ns;
		u64 now_ns;

		now_ns = get_kvmclock_ns(kvm);
		user_ns.clock = now_ns;
		user_ns.flags = kvm->arch.use_master_clock ? KVM_CLOCK_TSC_STABLE : 0;
		memset(&user_ns.pad, 0, sizeof(user_ns.pad));

		r = -EFAULT;
		if (copy_to_user(argp, &user_ns, sizeof(user_ns)))
			goto out;
		r = 0;
		break;
	}
	case KVM_MEMORY_ENCRYPT_OP: {
		r = -ENOTTY;
		if (kvm_x86_ops.mem_enc_op)
			r = kvm_x86_ops.mem_enc_op(kvm, argp);
		break;
	}
	case KVM_MEMORY_ENCRYPT_REG_REGION: {
		struct kvm_enc_region region;

		r = -EFAULT;
		if (copy_from_user(&region, argp, sizeof(region)))
			goto out;

		r = -ENOTTY;
		if (kvm_x86_ops.mem_enc_reg_region)
			r = kvm_x86_ops.mem_enc_reg_region(kvm, &region);
		break;
	}
	case KVM_MEMORY_ENCRYPT_UNREG_REGION: {
		struct kvm_enc_region region;

		r = -EFAULT;
		if (copy_from_user(&region, argp, sizeof(region)))
			goto out;

		r = -ENOTTY;
		if (kvm_x86_ops.mem_enc_unreg_region)
			r = kvm_x86_ops.mem_enc_unreg_region(kvm, &region);
		break;
	}
	case KVM_HYPERV_EVENTFD: {
		struct kvm_hyperv_eventfd hvevfd;

		r = -EFAULT;
		if (copy_from_user(&hvevfd, argp, sizeof(hvevfd)))
			goto out;
		r = kvm_vm_ioctl_hv_eventfd(kvm, &hvevfd);
		break;
	}
	case KVM_SET_PMU_EVENT_FILTER:
		r = kvm_vm_ioctl_set_pmu_event_filter(kvm, argp);
		break;
	case KVM_X86_SET_MSR_FILTER:
		r = kvm_vm_ioctl_set_msr_filter(kvm, argp);
		break;
	default:
		r = -ENOTTY;
	}
out:
	return r;
}

static void kvm_init_msr_list(void)
{
	struct x86_pmu_capability x86_pmu;
	u32 dummy[2];
	unsigned i;

	BUILD_BUG_ON_MSG(INTEL_PMC_MAX_FIXED != 4,
			 "Please update the fixed PMCs in msrs_to_saved_all[]");

	perf_get_x86_pmu_capability(&x86_pmu);

	num_msrs_to_save = 0;
	num_emulated_msrs = 0;
	num_msr_based_features = 0;

	for (i = 0; i < ARRAY_SIZE(msrs_to_save_all); i++) {
		if (rdmsr_safe(msrs_to_save_all[i], &dummy[0], &dummy[1]) < 0)
			continue;

		/*
		 * Even MSRs that are valid in the host may not be exposed
		 * to the guests in some cases.
		 */
		switch (msrs_to_save_all[i]) {
		case MSR_IA32_BNDCFGS:
			if (!kvm_mpx_supported())
				continue;
			break;
		case MSR_TSC_AUX:
			if (!kvm_cpu_cap_has(X86_FEATURE_RDTSCP))
				continue;
			break;
		case MSR_IA32_UMWAIT_CONTROL:
			if (!kvm_cpu_cap_has(X86_FEATURE_WAITPKG))
				continue;
			break;
		case MSR_IA32_RTIT_CTL:
		case MSR_IA32_RTIT_STATUS:
			if (!kvm_cpu_cap_has(X86_FEATURE_INTEL_PT))
				continue;
			break;
		case MSR_IA32_RTIT_CR3_MATCH:
			if (!kvm_cpu_cap_has(X86_FEATURE_INTEL_PT) ||
			    !intel_pt_validate_hw_cap(PT_CAP_cr3_filtering))
				continue;
			break;
		case MSR_IA32_RTIT_OUTPUT_BASE:
		case MSR_IA32_RTIT_OUTPUT_MASK:
			if (!kvm_cpu_cap_has(X86_FEATURE_INTEL_PT) ||
				(!intel_pt_validate_hw_cap(PT_CAP_topa_output) &&
				 !intel_pt_validate_hw_cap(PT_CAP_single_range_output)))
				continue;
			break;
		case MSR_IA32_RTIT_ADDR0_A ... MSR_IA32_RTIT_ADDR3_B:
			if (!kvm_cpu_cap_has(X86_FEATURE_INTEL_PT) ||
				msrs_to_save_all[i] - MSR_IA32_RTIT_ADDR0_A >=
				intel_pt_validate_hw_cap(PT_CAP_num_address_ranges) * 2)
				continue;
			break;
		case MSR_ARCH_PERFMON_PERFCTR0 ... MSR_ARCH_PERFMON_PERFCTR0 + 17:
			if (msrs_to_save_all[i] - MSR_ARCH_PERFMON_PERFCTR0 >=
			    min(INTEL_PMC_MAX_GENERIC, x86_pmu.num_counters_gp))
				continue;
			break;
		case MSR_ARCH_PERFMON_EVENTSEL0 ... MSR_ARCH_PERFMON_EVENTSEL0 + 17:
			if (msrs_to_save_all[i] - MSR_ARCH_PERFMON_EVENTSEL0 >=
			    min(INTEL_PMC_MAX_GENERIC, x86_pmu.num_counters_gp))
				continue;
			break;
		default:
			break;
		}

		msrs_to_save[num_msrs_to_save++] = msrs_to_save_all[i];
	}

	for (i = 0; i < ARRAY_SIZE(emulated_msrs_all); i++) {
		if (!kvm_x86_ops.has_emulated_msr(NULL, emulated_msrs_all[i]))
			continue;

		emulated_msrs[num_emulated_msrs++] = emulated_msrs_all[i];
	}

	for (i = 0; i < ARRAY_SIZE(msr_based_features_all); i++) {
		struct kvm_msr_entry msr;

		msr.index = msr_based_features_all[i];
		if (kvm_get_msr_feature(&msr))
			continue;

		msr_based_features[num_msr_based_features++] = msr_based_features_all[i];
	}
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

static void kvm_set_segment(struct kvm_vcpu *vcpu,
			struct kvm_segment *var, int seg)
{
	kvm_x86_ops.set_segment(vcpu, var, seg);
}

void kvm_get_segment(struct kvm_vcpu *vcpu,
		     struct kvm_segment *var, int seg)
{
	kvm_x86_ops.get_segment(vcpu, var, seg);
}

gpa_t translate_nested_gpa(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access,
			   struct x86_exception *exception)
{
	gpa_t t_gpa;

	BUG_ON(!mmu_is_nested(vcpu));

	/* NPT walks are always user-walks */
	access |= PFERR_USER_MASK;
	t_gpa  = vcpu->arch.mmu->gva_to_gpa(vcpu, gpa, access, exception);

	return t_gpa;
}

gpa_t kvm_mmu_gva_to_gpa_read(struct kvm_vcpu *vcpu, gva_t gva,
			      struct x86_exception *exception)
{
	u32 access = (kvm_x86_ops.get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0;
	return vcpu->arch.walk_mmu->gva_to_gpa(vcpu, gva, access, exception);
}

 gpa_t kvm_mmu_gva_to_gpa_fetch(struct kvm_vcpu *vcpu, gva_t gva,
				struct x86_exception *exception)
{
	u32 access = (kvm_x86_ops.get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0;
	access |= PFERR_FETCH_MASK;
	return vcpu->arch.walk_mmu->gva_to_gpa(vcpu, gva, access, exception);
}

gpa_t kvm_mmu_gva_to_gpa_write(struct kvm_vcpu *vcpu, gva_t gva,
			       struct x86_exception *exception)
{
	u32 access = (kvm_x86_ops.get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0;
	access |= PFERR_WRITE_MASK;
	return vcpu->arch.walk_mmu->gva_to_gpa(vcpu, gva, access, exception);
}

/* uses this to access any guest's mapped memory without checking CPL */
gpa_t kvm_mmu_gva_to_gpa_system(struct kvm_vcpu *vcpu, gva_t gva,
				struct x86_exception *exception)
{
	return vcpu->arch.walk_mmu->gva_to_gpa(vcpu, gva, 0, exception);
}

static int kvm_read_guest_virt_helper(gva_t addr, void *val, unsigned int bytes,
				      struct kvm_vcpu *vcpu, u32 access,
				      struct x86_exception *exception)
{
	void *data = val;
	int r = X86EMUL_CONTINUE;

	while (bytes) {
		gpa_t gpa = vcpu->arch.walk_mmu->gva_to_gpa(vcpu, addr, access,
							    exception);
		unsigned offset = addr & (PAGE_SIZE-1);
		unsigned toread = min(bytes, (unsigned)PAGE_SIZE - offset);
		int ret;

		if (gpa == UNMAPPED_GVA)
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
	u32 access = (kvm_x86_ops.get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0;
	unsigned offset;
	int ret;

	/* Inline kvm_read_guest_virt_helper for speed.  */
	gpa_t gpa = vcpu->arch.walk_mmu->gva_to_gpa(vcpu, addr, access|PFERR_FETCH_MASK,
						    exception);
	if (unlikely(gpa == UNMAPPED_GVA))
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
	u32 access = (kvm_x86_ops.get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0;

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
	u32 access = 0;

	if (!system && kvm_x86_ops.get_cpl(vcpu) == 3)
		access |= PFERR_USER_MASK;

	return kvm_read_guest_virt_helper(addr, val, bytes, vcpu, access, exception);
}

static int kvm_read_guest_phys_system(struct x86_emulate_ctxt *ctxt,
		unsigned long addr, void *val, unsigned int bytes)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	int r = kvm_vcpu_read_guest(vcpu, addr, val, bytes);

	return r < 0 ? X86EMUL_IO_NEEDED : X86EMUL_CONTINUE;
}

static int kvm_write_guest_virt_helper(gva_t addr, void *val, unsigned int bytes,
				      struct kvm_vcpu *vcpu, u32 access,
				      struct x86_exception *exception)
{
	void *data = val;
	int r = X86EMUL_CONTINUE;

	while (bytes) {
		gpa_t gpa =  vcpu->arch.walk_mmu->gva_to_gpa(vcpu, addr,
							     access,
							     exception);
		unsigned offset = addr & (PAGE_SIZE-1);
		unsigned towrite = min(bytes, (unsigned)PAGE_SIZE - offset);
		int ret;

		if (gpa == UNMAPPED_GVA)
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
	u32 access = PFERR_WRITE_MASK;

	if (!system && kvm_x86_ops.get_cpl(vcpu) == 3)
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

int handle_ud(struct kvm_vcpu *vcpu)
{
	static const char kvm_emulate_prefix[] = { __KVM_EMULATE_PREFIX };
	int emul_type = EMULTYPE_TRAP_UD;
	char sig[5]; /* ud2; .ascii "kvm" */
	struct x86_exception e;

	if (unlikely(!kvm_x86_ops.can_emulate_instruction(vcpu, NULL, 0)))
		return 1;

	if (force_emulation_prefix &&
	    kvm_read_guest_virt(vcpu, kvm_get_linear_rip(vcpu),
				sig, sizeof(sig), &e) == 0 &&
	    memcmp(sig, kvm_emulate_prefix, sizeof(sig)) == 0) {
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
	u32 access = ((kvm_x86_ops.get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0)
		| (write ? PFERR_WRITE_MASK : 0);

	/*
	 * currently PKRU is only applied to ept enabled guest so
	 * there is no pkey in EPT page table for L1 guest or EPT
	 * shadow page table for L2 guest.
	 */
	if (vcpu_match_mmio_gva(vcpu, gva)
	    && !permission_fault(vcpu, vcpu->arch.walk_mmu,
				 vcpu->arch.mmio_access, 0, access)) {
		*gpa = vcpu->arch.mmio_gfn << PAGE_SHIFT |
					(gva & (PAGE_SIZE - 1));
		trace_vcpu_match_mmio(gva, *gpa, write, false);
		return 1;
	}

	*gpa = vcpu->arch.walk_mmu->gva_to_gpa(vcpu, gva, access, exception);

	if (*gpa == UNMAPPED_GVA)
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

#define CMPXCHG_TYPE(t, ptr, old, new) \
	(cmpxchg((t *)(ptr), *(t *)(old), *(t *)(new)) == *(t *)(old))

#ifdef CONFIG_X86_64
#  define CMPXCHG64(ptr, old, new) CMPXCHG_TYPE(u64, ptr, old, new)
#else
#  define CMPXCHG64(ptr, old, new) \
	(cmpxchg64((u64 *)(ptr), *(u64 *)(old), *(u64 *)(new)) == *(u64 *)(old))
#endif

static int emulator_cmpxchg_emulated(struct x86_emulate_ctxt *ctxt,
				     unsigned long addr,
				     const void *old,
				     const void *new,
				     unsigned int bytes,
				     struct x86_exception *exception)
{
	struct kvm_host_map map;
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	u64 page_line_mask;
	gpa_t gpa;
	char *kaddr;
	bool exchanged;

	/* guests cmpxchg8b have to be emulated atomically */
	if (bytes > 8 || (bytes & (bytes - 1)))
		goto emul_write;

	gpa = kvm_mmu_gva_to_gpa_write(vcpu, addr, NULL);

	if (gpa == UNMAPPED_GVA ||
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

	if (kvm_vcpu_map(vcpu, gpa_to_gfn(gpa), &map))
		goto emul_write;

	kaddr = map.hva + offset_in_page(gpa);

	switch (bytes) {
	case 1:
		exchanged = CMPXCHG_TYPE(u8, kaddr, old, new);
		break;
	case 2:
		exchanged = CMPXCHG_TYPE(u16, kaddr, old, new);
		break;
	case 4:
		exchanged = CMPXCHG_TYPE(u32, kaddr, old, new);
		break;
	case 8:
		exchanged = CMPXCHG64(kaddr, old, new);
		break;
	default:
		BUG();
	}

	kvm_vcpu_unmap(vcpu, &map, true);

	if (!exchanged)
		return X86EMUL_CMPXCHG_FAILED;

	kvm_page_track_write(vcpu, gpa, new, bytes);

	return X86EMUL_CONTINUE;

emul_write:
	printk_once(KERN_WARNING "kvm: emulating exchange as write\n");

	return emulator_write_emulated(ctxt, addr, new, bytes, exception);
}

static int kernel_pio(struct kvm_vcpu *vcpu, void *pd)
{
	int r = 0, i;

	for (i = 0; i < vcpu->arch.pio.count; i++) {
		if (vcpu->arch.pio.in)
			r = kvm_io_bus_read(vcpu, KVM_PIO_BUS, vcpu->arch.pio.port,
					    vcpu->arch.pio.size, pd);
		else
			r = kvm_io_bus_write(vcpu, KVM_PIO_BUS,
					     vcpu->arch.pio.port, vcpu->arch.pio.size,
					     pd);
		if (r)
			break;
		pd += vcpu->arch.pio.size;
	}
	return r;
}

static int emulator_pio_in_out(struct kvm_vcpu *vcpu, int size,
			       unsigned short port, void *val,
			       unsigned int count, bool in)
{
	vcpu->arch.pio.port = port;
	vcpu->arch.pio.in = in;
	vcpu->arch.pio.count  = count;
	vcpu->arch.pio.size = size;

	if (!kernel_pio(vcpu, vcpu->arch.pio_data)) {
		vcpu->arch.pio.count = 0;
		return 1;
	}

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
	int ret;

	if (vcpu->arch.pio.count)
		goto data_avail;

	memset(vcpu->arch.pio_data, 0, size * count);

	ret = emulator_pio_in_out(vcpu, size, port, val, count, true);
	if (ret) {
data_avail:
		memcpy(val, vcpu->arch.pio_data, size * count);
		trace_kvm_pio(KVM_PIO_IN, port, size, count, vcpu->arch.pio_data);
		vcpu->arch.pio.count = 0;
		return 1;
	}

	return 0;
}

static int emulator_pio_in_emulated(struct x86_emulate_ctxt *ctxt,
				    int size, unsigned short port, void *val,
				    unsigned int count)
{
	return emulator_pio_in(emul_to_vcpu(ctxt), size, port, val, count);

}

static int emulator_pio_out(struct kvm_vcpu *vcpu, int size,
			    unsigned short port, const void *val,
			    unsigned int count)
{
	memcpy(vcpu->arch.pio_data, val, size * count);
	trace_kvm_pio(KVM_PIO_OUT, port, size, count, vcpu->arch.pio_data);
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
	return kvm_x86_ops.get_segment_base(vcpu, seg);
}

static void emulator_invlpg(struct x86_emulate_ctxt *ctxt, ulong address)
{
	kvm_mmu_invlpg(emul_to_vcpu(ctxt), address);
}

static int kvm_emulate_wbinvd_noskip(struct kvm_vcpu *vcpu)
{
	if (!need_emulate_wbinvd(vcpu))
		return X86EMUL_CONTINUE;

	if (kvm_x86_ops.has_wbinvd_exit()) {
		int cpu = get_cpu();

		cpumask_set_cpu(cpu, vcpu->arch.wbinvd_dirty_mask);
		smp_call_function_many(vcpu->arch.wbinvd_dirty_mask,
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

static int emulator_get_dr(struct x86_emulate_ctxt *ctxt, int dr,
			   unsigned long *dest)
{
	return kvm_get_dr(emul_to_vcpu(ctxt), dr, dest);
}

static int emulator_set_dr(struct x86_emulate_ctxt *ctxt, int dr,
			   unsigned long value)
{

	return __kvm_set_dr(emul_to_vcpu(ctxt), dr, value);
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
	return kvm_x86_ops.get_cpl(emul_to_vcpu(ctxt));
}

static void emulator_get_gdt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_ops.get_gdt(emul_to_vcpu(ctxt), dt);
}

static void emulator_get_idt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_ops.get_idt(emul_to_vcpu(ctxt), dt);
}

static void emulator_set_gdt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_ops.set_gdt(emul_to_vcpu(ctxt), dt);
}

static void emulator_set_idt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_ops.set_idt(emul_to_vcpu(ctxt), dt);
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

static int emulator_get_msr(struct x86_emulate_ctxt *ctxt,
			    u32 msr_index, u64 *pdata)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	int r;

	r = kvm_get_msr(vcpu, msr_index, pdata);

	if (r && kvm_get_msr_user_space(vcpu, msr_index, r)) {
		/* Bounce to user space */
		return X86EMUL_IO_NEEDED;
	}

	return r;
}

static int emulator_set_msr(struct x86_emulate_ctxt *ctxt,
			    u32 msr_index, u64 data)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	int r;

	r = kvm_set_msr(vcpu, msr_index, data);

	if (r && kvm_set_msr_user_space(vcpu, msr_index, data, r)) {
		/* Bounce to user space */
		return X86EMUL_IO_NEEDED;
	}

	return r;
}

static u64 emulator_get_smbase(struct x86_emulate_ctxt *ctxt)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);

	return vcpu->arch.smbase;
}

static void emulator_set_smbase(struct x86_emulate_ctxt *ctxt, u64 smbase)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);

	vcpu->arch.smbase = smbase;
}

static int emulator_check_pmc(struct x86_emulate_ctxt *ctxt,
			      u32 pmc)
{
	return kvm_pmu_is_valid_rdpmc_ecx(emul_to_vcpu(ctxt), pmc);
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
	return kvm_x86_ops.check_intercept(emul_to_vcpu(ctxt), info, stage,
					    &ctxt->exception);
}

static bool emulator_get_cpuid(struct x86_emulate_ctxt *ctxt,
			      u32 *eax, u32 *ebx, u32 *ecx, u32 *edx,
			      bool exact_only)
{
	return kvm_cpuid(emul_to_vcpu(ctxt), eax, ebx, ecx, edx, exact_only);
}

static bool emulator_guest_has_long_mode(struct x86_emulate_ctxt *ctxt)
{
	return guest_cpuid_has(emul_to_vcpu(ctxt), X86_FEATURE_LM);
}

static bool emulator_guest_has_movbe(struct x86_emulate_ctxt *ctxt)
{
	return guest_cpuid_has(emul_to_vcpu(ctxt), X86_FEATURE_MOVBE);
}

static bool emulator_guest_has_fxsr(struct x86_emulate_ctxt *ctxt)
{
	return guest_cpuid_has(emul_to_vcpu(ctxt), X86_FEATURE_FXSR);
}

static ulong emulator_read_gpr(struct x86_emulate_ctxt *ctxt, unsigned reg)
{
	return kvm_register_read(emul_to_vcpu(ctxt), reg);
}

static void emulator_write_gpr(struct x86_emulate_ctxt *ctxt, unsigned reg, ulong val)
{
	kvm_register_write(emul_to_vcpu(ctxt), reg, val);
}

static void emulator_set_nmi_mask(struct x86_emulate_ctxt *ctxt, bool masked)
{
	kvm_x86_ops.set_nmi_mask(emul_to_vcpu(ctxt), masked);
}

static unsigned emulator_get_hflags(struct x86_emulate_ctxt *ctxt)
{
	return emul_to_vcpu(ctxt)->arch.hflags;
}

static void emulator_set_hflags(struct x86_emulate_ctxt *ctxt, unsigned emul_flags)
{
	emul_to_vcpu(ctxt)->arch.hflags = emul_flags;
}

static int emulator_pre_leave_smm(struct x86_emulate_ctxt *ctxt,
				  const char *smstate)
{
	return kvm_x86_ops.pre_leave_smm(emul_to_vcpu(ctxt), smstate);
}

static void emulator_post_leave_smm(struct x86_emulate_ctxt *ctxt)
{
	kvm_smm_changed(emul_to_vcpu(ctxt));
}

static int emulator_set_xcr(struct x86_emulate_ctxt *ctxt, u32 index, u64 xcr)
{
	return __kvm_set_xcr(emul_to_vcpu(ctxt), index, xcr);
}

static const struct x86_emulate_ops emulate_ops = {
	.read_gpr            = emulator_read_gpr,
	.write_gpr           = emulator_write_gpr,
	.read_std            = emulator_read_std,
	.write_std           = emulator_write_std,
	.read_phys           = kvm_read_guest_phys_system,
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
	.get_smbase          = emulator_get_smbase,
	.set_smbase          = emulator_set_smbase,
	.set_msr             = emulator_set_msr,
	.get_msr             = emulator_get_msr,
	.check_pmc	     = emulator_check_pmc,
	.read_pmc            = emulator_read_pmc,
	.halt                = emulator_halt,
	.wbinvd              = emulator_wbinvd,
	.fix_hypercall       = emulator_fix_hypercall,
	.intercept           = emulator_intercept,
	.get_cpuid           = emulator_get_cpuid,
	.guest_has_long_mode = emulator_guest_has_long_mode,
	.guest_has_movbe     = emulator_guest_has_movbe,
	.guest_has_fxsr      = emulator_guest_has_fxsr,
	.set_nmi_mask        = emulator_set_nmi_mask,
	.get_hflags          = emulator_get_hflags,
	.set_hflags          = emulator_set_hflags,
	.pre_leave_smm       = emulator_pre_leave_smm,
	.post_leave_smm      = emulator_post_leave_smm,
	.set_xcr             = emulator_set_xcr,
};

static void toggle_interruptibility(struct kvm_vcpu *vcpu, u32 mask)
{
	u32 int_shadow = kvm_x86_ops.get_interrupt_shadow(vcpu);
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
		kvm_x86_ops.set_interrupt_shadow(vcpu, mask);
		if (!mask)
			kvm_make_request(KVM_REQ_EVENT, vcpu);
	}
}

static bool inject_emulated_exception(struct kvm_vcpu *vcpu)
{
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;
	if (ctxt->exception.vector == PF_VECTOR)
		return kvm_inject_emulated_page_fault(vcpu, &ctxt->exception);

	if (ctxt->exception.error_code_valid)
		kvm_queue_exception_e(vcpu, ctxt->exception.vector,
				      ctxt->exception.error_code);
	else
		kvm_queue_exception(vcpu, ctxt->exception.vector);
	return false;
}

static struct x86_emulate_ctxt *alloc_emulate_ctxt(struct kvm_vcpu *vcpu)
{
	struct x86_emulate_ctxt *ctxt;

	ctxt = kmem_cache_zalloc(x86_emulator_cache, GFP_KERNEL_ACCOUNT);
	if (!ctxt) {
		pr_err("kvm: failed to allocate vcpu's emulator\n");
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

	kvm_x86_ops.get_cs_db_l_bits(vcpu, &cs_db, &cs_l);

	ctxt->gpa_available = false;
	ctxt->eflags = kvm_get_rflags(vcpu);
	ctxt->tf = (ctxt->eflags & X86_EFLAGS_TF) != 0;

	ctxt->eip = kvm_rip_read(vcpu);
	ctxt->mode = (!is_protmode(vcpu))		? X86EMUL_MODE_REAL :
		     (ctxt->eflags & X86_EFLAGS_VM)	? X86EMUL_MODE_VM86 :
		     (cs_l && is_long_mode(vcpu))	? X86EMUL_MODE_PROT64 :
		     cs_db				? X86EMUL_MODE_PROT32 :
							  X86EMUL_MODE_PROT16;
	BUILD_BUG_ON(HF_GUEST_MASK != X86EMUL_GUEST_MASK);
	BUILD_BUG_ON(HF_SMM_MASK != X86EMUL_SMM_MASK);
	BUILD_BUG_ON(HF_SMM_INSIDE_NMI_MASK != X86EMUL_SMM_INSIDE_NMI_MASK);

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

static int handle_emulation_failure(struct kvm_vcpu *vcpu, int emulation_type)
{
	++vcpu->stat.insn_emulation_fail;
	trace_kvm_emulate_insn_failed(vcpu);

	if (emulation_type & EMULTYPE_VMWARE_GP) {
		kvm_queue_exception_e(vcpu, GP_VECTOR, 0);
		return 1;
	}

	if (emulation_type & EMULTYPE_SKIP) {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror = KVM_INTERNAL_ERROR_EMULATION;
		vcpu->run->internal.ndata = 0;
		return 0;
	}

	kvm_queue_exception(vcpu, UD_VECTOR);

	if (!is_guest_mode(vcpu) && kvm_x86_ops.get_cpl(vcpu) == 0) {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror = KVM_INTERNAL_ERROR_EMULATION;
		vcpu->run->internal.ndata = 0;
		return 0;
	}

	return 1;
}

static bool reexecute_instruction(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
				  bool write_fault_to_shadow_pgtable,
				  int emulation_type)
{
	gpa_t gpa = cr2_or_gpa;
	kvm_pfn_t pfn;

	if (!(emulation_type & EMULTYPE_ALLOW_RETRY_PF))
		return false;

	if (WARN_ON_ONCE(is_guest_mode(vcpu)) ||
	    WARN_ON_ONCE(!(emulation_type & EMULTYPE_PF)))
		return false;

	if (!vcpu->arch.mmu->direct_map) {
		/*
		 * Write permission should be allowed since only
		 * write access need to be emulated.
		 */
		gpa = kvm_mmu_gva_to_gpa_write(vcpu, cr2_or_gpa, NULL);

		/*
		 * If the mapping is invalid in guest, let cpu retry
		 * it to generate fault.
		 */
		if (gpa == UNMAPPED_GVA)
			return true;
	}

	/*
	 * Do not retry the unhandleable instruction if it faults on the
	 * readonly host memory, otherwise it will goto a infinite loop:
	 * retry instruction -> write #PF -> emulation fail -> retry
	 * instruction -> ...
	 */
	pfn = gfn_to_pfn(vcpu->kvm, gpa_to_gfn(gpa));

	/*
	 * If the instruction failed on the error pfn, it can not be fixed,
	 * report the error to userspace.
	 */
	if (is_error_noslot_pfn(pfn))
		return false;

	kvm_release_pfn_clean(pfn);

	/* The instructions are well-emulated on direct mmu. */
	if (vcpu->arch.mmu->direct_map) {
		unsigned int indirect_shadow_pages;

		spin_lock(&vcpu->kvm->mmu_lock);
		indirect_shadow_pages = vcpu->kvm->arch.indirect_shadow_pages;
		spin_unlock(&vcpu->kvm->mmu_lock);

		if (indirect_shadow_pages)
			kvm_mmu_unprotect_page(vcpu->kvm, gpa_to_gfn(gpa));

		return true;
	}

	/*
	 * if emulation was due to access to shadowed page table
	 * and it failed try to unshadow page and re-enter the
	 * guest to let CPU execute the instruction.
	 */
	kvm_mmu_unprotect_page(vcpu->kvm, gpa_to_gfn(gpa));

	/*
	 * If the access faults on its page table, it can not
	 * be fixed by unprotecting shadow page and it should
	 * be reported to userspace.
	 */
	return !write_fault_to_shadow_pgtable;
}

static bool retry_instruction(struct x86_emulate_ctxt *ctxt,
			      gpa_t cr2_or_gpa,  int emulation_type)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	unsigned long last_retry_eip, last_retry_addr, gpa = cr2_or_gpa;

	last_retry_eip = vcpu->arch.last_retry_eip;
	last_retry_addr = vcpu->arch.last_retry_addr;

	/*
	 * If the emulation is caused by #PF and it is non-page_table
	 * writing instruction, it means the VM-EXIT is caused by shadow
	 * page protected, we can zap the shadow page and retry this
	 * instruction directly.
	 *
	 * Note: if the guest uses a non-page-table modifying instruction
	 * on the PDE that points to the instruction, then we will unmap
	 * the instruction and go to an infinite loop. So, we cache the
	 * last retried eip and the last fault address, if we meet the eip
	 * and the address again, we can break out of the potential infinite
	 * loop.
	 */
	vcpu->arch.last_retry_eip = vcpu->arch.last_retry_addr = 0;

	if (!(emulation_type & EMULTYPE_ALLOW_RETRY_PF))
		return false;

	if (WARN_ON_ONCE(is_guest_mode(vcpu)) ||
	    WARN_ON_ONCE(!(emulation_type & EMULTYPE_PF)))
		return false;

	if (x86_page_table_writing_insn(ctxt))
		return false;

	if (ctxt->eip == last_retry_eip && last_retry_addr == cr2_or_gpa)
		return false;

	vcpu->arch.last_retry_eip = ctxt->eip;
	vcpu->arch.last_retry_addr = cr2_or_gpa;

	if (!vcpu->arch.mmu->direct_map)
		gpa = kvm_mmu_gva_to_gpa_write(vcpu, cr2_or_gpa, NULL);

	kvm_mmu_unprotect_page(vcpu->kvm, gpa_to_gfn(gpa));

	return true;
}

static int complete_emulated_mmio(struct kvm_vcpu *vcpu);
static int complete_emulated_pio(struct kvm_vcpu *vcpu);

static void kvm_smm_changed(struct kvm_vcpu *vcpu)
{
	if (!(vcpu->arch.hflags & HF_SMM_MASK)) {
		/* This is a good place to trace that we are exiting SMM.  */
		trace_kvm_enter_smm(vcpu->vcpu_id, vcpu->arch.smbase, false);

		/* Process a latched INIT or SMI, if any.  */
		kvm_make_request(KVM_REQ_EVENT, vcpu);
	}

	kvm_mmu_reset_context(vcpu);
}

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
		kvm_run->debug.arch.dr6 = DR6_BS | DR6_FIXED_1 | DR6_RTM;
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
	unsigned long rflags = kvm_x86_ops.get_rflags(vcpu);
	int r;

	r = kvm_x86_ops.skip_emulated_instruction(vcpu);
	if (unlikely(!r))
		return 0;

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

static bool kvm_vcpu_check_breakpoint(struct kvm_vcpu *vcpu, int *r)
{
	if (unlikely(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP) &&
	    (vcpu->arch.guest_debug_dr7 & DR7_BP_EN_MASK)) {
		struct kvm_run *kvm_run = vcpu->run;
		unsigned long eip = kvm_get_linear_rip(vcpu);
		u32 dr6 = kvm_vcpu_check_hw_bp(eip, 0,
					   vcpu->arch.guest_debug_dr7,
					   vcpu->arch.eff_db);

		if (dr6 != 0) {
			kvm_run->debug.arch.dr6 = dr6 | DR6_FIXED_1 | DR6_RTM;
			kvm_run->debug.arch.pc = eip;
			kvm_run->debug.arch.exception = DB_VECTOR;
			kvm_run->exit_reason = KVM_EXIT_DEBUG;
			*r = 0;
			return true;
		}
	}

	if (unlikely(vcpu->arch.dr7 & DR7_BP_EN_MASK) &&
	    !(kvm_get_rflags(vcpu) & X86_EFLAGS_RF)) {
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

int x86_emulate_instruction(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
			    int emulation_type, void *insn, int insn_len)
{
	int r;
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;
	bool writeback = true;
	bool write_fault_to_spt;

	if (unlikely(!kvm_x86_ops.can_emulate_instruction(vcpu, insn, insn_len)))
		return 1;

	vcpu->arch.l1tf_flush_l1d = true;

	/*
	 * Clear write_fault_to_shadow_pgtable here to ensure it is
	 * never reused.
	 */
	write_fault_to_spt = vcpu->arch.write_fault_to_shadow_pgtable;
	vcpu->arch.write_fault_to_shadow_pgtable = false;
	kvm_clear_exception_queue(vcpu);

	if (!(emulation_type & EMULTYPE_NO_DECODE)) {
		init_emulate_ctxt(vcpu);

		/*
		 * We will reenter on the same instruction since
		 * we do not set complete_userspace_io.  This does not
		 * handle watchpoints yet, those would be handled in
		 * the emulate_ops.
		 */
		if (!(emulation_type & EMULTYPE_SKIP) &&
		    kvm_vcpu_check_breakpoint(vcpu, &r))
			return r;

		ctxt->interruptibility = 0;
		ctxt->have_exception = false;
		ctxt->exception.vector = -1;
		ctxt->perm_ok = false;

		ctxt->ud = emulation_type & EMULTYPE_TRAP_UD;

		r = x86_decode_insn(ctxt, insn, insn_len);

		trace_kvm_emulate_insn_start(vcpu);
		++vcpu->stat.insn_emulation;
		if (r != EMULATION_OK)  {
			if ((emulation_type & EMULTYPE_TRAP_UD) ||
			    (emulation_type & EMULTYPE_TRAP_UD_FORCED)) {
				kvm_queue_exception(vcpu, UD_VECTOR);
				return 1;
			}
			if (reexecute_instruction(vcpu, cr2_or_gpa,
						  write_fault_to_spt,
						  emulation_type))
				return 1;
			if (ctxt->have_exception) {
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
	 * Note, EMULTYPE_SKIP is intended for use *only* by vendor callbacks
	 * for kvm_skip_emulated_instruction().  The caller is responsible for
	 * updating interruptibility state and injecting single-step #DBs.
	 */
	if (emulation_type & EMULTYPE_SKIP) {
		kvm_rip_write(vcpu, ctxt->_eip);
		if (ctxt->eflags & X86_EFLAGS_RF)
			kvm_set_rflags(vcpu, ctxt->eflags & ~X86_EFLAGS_RF);
		return 1;
	}

	if (retry_instruction(ctxt, cr2_or_gpa, emulation_type))
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
		if (vcpu->arch.mmu->direct_map) {
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
		if (reexecute_instruction(vcpu, cr2_or_gpa, write_fault_to_spt,
					emulation_type))
			return 1;

		return handle_emulation_failure(vcpu, emulation_type);
	}

	if (ctxt->have_exception) {
		r = 1;
		if (inject_emulated_exception(vcpu))
			return r;
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
	} else if (r == EMULATION_RESTART)
		goto restart;
	else
		r = 1;

	if (writeback) {
		unsigned long rflags = kvm_x86_ops.get_rflags(vcpu);
		toggle_interruptibility(vcpu, ctxt->interruptibility);
		vcpu->arch.emulate_regs_need_sync_to_vcpu = false;
		if (!ctxt->have_exception ||
		    exception_type(ctxt->exception.vector) == EXCPT_TRAP) {
			kvm_rip_write(vcpu, ctxt->eip);
			if (r && (ctxt->tf || (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)))
				r = kvm_vcpu_do_singlestep(vcpu);
			if (kvm_x86_ops.update_emulated_instruction)
				kvm_x86_ops.update_emulated_instruction(vcpu);
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

	/*
	 * Since vcpu->arch.pio.count == 1 let emulator_pio_in perform
	 * the copy and tracing
	 */
	emulator_pio_in(vcpu, vcpu->arch.pio.size, vcpu->arch.pio.port, &val, 1);
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
	unsigned long khz = 0;

	if (data)
		khz = freq->new;
	else if (!boot_cpu_has(X86_FEATURE_CONSTANT_TSC))
		khz = cpufreq_quick_get(raw_smp_processor_id());
	if (!khz)
		khz = tsc_khz;
	__this_cpu_write(cpu_tsc_khz, khz);
}

#ifdef CONFIG_X86_64
static void kvm_hyperv_tsc_notifier(void)
{
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
	int cpu;

	mutex_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list)
		kvm_make_mclock_inprogress_request(kvm);

	hyperv_stop_tsc_emulation();

	/* TSC frequency always matches when on Hyper-V */
	for_each_present_cpu(cpu)
		per_cpu(cpu_tsc_khz, cpu) = tsc_khz;
	kvm_max_guest_tsc_khz = tsc_khz;

	list_for_each_entry(kvm, &vm_list, vm_list) {
		struct kvm_arch *ka = &kvm->arch;

		spin_lock(&ka->pvclock_gtod_sync_lock);

		pvclock_update_vm_gtod_copy(kvm);

		kvm_for_each_vcpu(cpu, vcpu, kvm)
			kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);

		kvm_for_each_vcpu(cpu, vcpu, kvm)
			kvm_clear_request(KVM_REQ_MCLOCK_INPROGRESS, vcpu);

		spin_unlock(&ka->pvclock_gtod_sync_lock);
	}
	mutex_unlock(&kvm_lock);
}
#endif

static void __kvmclock_cpufreq_notifier(struct cpufreq_freqs *freq, int cpu)
{
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
	int i, send_ipi = 0;

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
	max_tsc_khz = tsc_khz;

	if (!boot_cpu_has(X86_FEATURE_CONSTANT_TSC)) {
#ifdef CONFIG_CPU_FREQ
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
#endif
		cpufreq_register_notifier(&kvmclock_cpufreq_notifier_block,
					  CPUFREQ_TRANSITION_NOTIFIER);
	}

	cpuhp_setup_state(CPUHP_AP_X86_KVM_CLK_ONLINE, "x86/kvm/clk:online",
			  kvmclock_cpu_online, kvmclock_cpu_down_prep);
}

DEFINE_PER_CPU(struct kvm_vcpu *, current_vcpu);
EXPORT_PER_CPU_SYMBOL_GPL(current_vcpu);

int kvm_is_in_guest(void)
{
	return __this_cpu_read(current_vcpu) != NULL;
}

static int kvm_is_user_mode(void)
{
	int user_mode = 3;

	if (__this_cpu_read(current_vcpu))
		user_mode = kvm_x86_ops.get_cpl(__this_cpu_read(current_vcpu));

	return user_mode != 0;
}

static unsigned long kvm_get_guest_ip(void)
{
	unsigned long ip = 0;

	if (__this_cpu_read(current_vcpu))
		ip = kvm_rip_read(__this_cpu_read(current_vcpu));

	return ip;
}

static void kvm_handle_intel_pt_intr(void)
{
	struct kvm_vcpu *vcpu = __this_cpu_read(current_vcpu);

	kvm_make_request(KVM_REQ_PMI, vcpu);
	__set_bit(MSR_CORE_PERF_GLOBAL_OVF_CTRL_TRACE_TOPA_PMI_BIT,
			(unsigned long *)&vcpu->arch.pmu.global_status);
}

static struct perf_guest_info_callbacks kvm_guest_cbs = {
	.is_in_guest		= kvm_is_in_guest,
	.is_user_mode		= kvm_is_user_mode,
	.get_guest_ip		= kvm_get_guest_ip,
	.handle_intel_pt_intr	= kvm_handle_intel_pt_intr,
};

#ifdef CONFIG_X86_64
static void pvclock_gtod_update_fn(struct work_struct *work)
{
	struct kvm *kvm;

	struct kvm_vcpu *vcpu;
	int i;

	mutex_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list)
		kvm_for_each_vcpu(i, vcpu, kvm)
			kvm_make_request(KVM_REQ_MASTERCLOCK_UPDATE, vcpu);
	atomic_set(&kvm_guest_has_master_clock, 0);
	mutex_unlock(&kvm_lock);
}

static DECLARE_WORK(pvclock_gtod_work, pvclock_gtod_update_fn);

/*
 * Notification about pvclock gtod data update.
 */
static int pvclock_gtod_notify(struct notifier_block *nb, unsigned long unused,
			       void *priv)
{
	struct pvclock_gtod_data *gtod = &pvclock_gtod_data;
	struct timekeeper *tk = priv;

	update_pvclock_gtod(tk);

	/* disable master clock if host does not trust, or does not
	 * use, TSC based clocksource.
	 */
	if (!gtod_is_based_on_tsc(gtod->clock.vclock_mode) &&
	    atomic_read(&kvm_guest_has_master_clock) != 0)
		queue_work(system_long_wq, &pvclock_gtod_work);

	return 0;
}

static struct notifier_block pvclock_gtod_notifier = {
	.notifier_call = pvclock_gtod_notify,
};
#endif

int kvm_arch_init(void *opaque)
{
	struct kvm_x86_init_ops *ops = opaque;
	int r;

	if (kvm_x86_ops.hardware_enable) {
		printk(KERN_ERR "kvm: already loaded the other module\n");
		r = -EEXIST;
		goto out;
	}

	if (!ops->cpu_has_kvm_support()) {
		pr_err_ratelimited("kvm: no hardware support\n");
		r = -EOPNOTSUPP;
		goto out;
	}
	if (ops->disabled_by_bios()) {
		pr_err_ratelimited("kvm: disabled by bios\n");
		r = -EOPNOTSUPP;
		goto out;
	}

	/*
	 * KVM explicitly assumes that the guest has an FPU and
	 * FXSAVE/FXRSTOR. For example, the KVM_GET_FPU explicitly casts the
	 * vCPU's FPU state as a fxregs_state struct.
	 */
	if (!boot_cpu_has(X86_FEATURE_FPU) || !boot_cpu_has(X86_FEATURE_FXSR)) {
		printk(KERN_ERR "kvm: inadequate fpu\n");
		r = -EOPNOTSUPP;
		goto out;
	}

	r = -ENOMEM;
	x86_fpu_cache = kmem_cache_create("x86_fpu", sizeof(struct fpu),
					  __alignof__(struct fpu), SLAB_ACCOUNT,
					  NULL);
	if (!x86_fpu_cache) {
		printk(KERN_ERR "kvm: failed to allocate cache for x86 fpu\n");
		goto out;
	}

	x86_emulator_cache = kvm_alloc_emulator_cache();
	if (!x86_emulator_cache) {
		pr_err("kvm: failed to allocate cache for x86 emulator\n");
		goto out_free_x86_fpu_cache;
	}

	user_return_msrs = alloc_percpu(struct kvm_user_return_msrs);
	if (!user_return_msrs) {
		printk(KERN_ERR "kvm: failed to allocate percpu kvm_user_return_msrs\n");
		goto out_free_x86_emulator_cache;
	}

	r = kvm_mmu_module_init();
	if (r)
		goto out_free_percpu;

	kvm_mmu_set_mask_ptes(PT_USER_MASK, PT_ACCESSED_MASK,
			PT_DIRTY_MASK, PT64_NX_MASK, 0,
			PT_PRESENT_MASK, 0, sme_me_mask);
	kvm_timer_init();

	perf_register_guest_info_callbacks(&kvm_guest_cbs);

	if (boot_cpu_has(X86_FEATURE_XSAVE)) {
		host_xcr0 = xgetbv(XCR_XFEATURE_ENABLED_MASK);
		supported_xcr0 = host_xcr0 & KVM_SUPPORTED_XCR0;
	}

	kvm_lapic_init();
	if (pi_inject_timer == -1)
		pi_inject_timer = housekeeping_enabled(HK_FLAG_TIMER);
#ifdef CONFIG_X86_64
	pvclock_gtod_register_notifier(&pvclock_gtod_notifier);

	if (hypervisor_is_type(X86_HYPER_MS_HYPERV))
		set_hv_tscchange_cb(kvm_hyperv_tsc_notifier);
#endif

	return 0;

out_free_percpu:
	free_percpu(user_return_msrs);
out_free_x86_emulator_cache:
	kmem_cache_destroy(x86_emulator_cache);
out_free_x86_fpu_cache:
	kmem_cache_destroy(x86_fpu_cache);
out:
	return r;
}

void kvm_arch_exit(void)
{
#ifdef CONFIG_X86_64
	if (hypervisor_is_type(X86_HYPER_MS_HYPERV))
		clear_hv_tscchange_cb();
#endif
	kvm_lapic_exit();
	perf_unregister_guest_info_callbacks(&kvm_guest_cbs);

	if (!boot_cpu_has(X86_FEATURE_CONSTANT_TSC))
		cpufreq_unregister_notifier(&kvmclock_cpufreq_notifier_block,
					    CPUFREQ_TRANSITION_NOTIFIER);
	cpuhp_remove_state_nocalls(CPUHP_AP_X86_KVM_CLK_ONLINE);
#ifdef CONFIG_X86_64
	pvclock_gtod_unregister_notifier(&pvclock_gtod_notifier);
#endif
	kvm_x86_ops.hardware_enable = NULL;
	kvm_mmu_module_exit();
	free_percpu(user_return_msrs);
	kmem_cache_destroy(x86_fpu_cache);
}

static int __kvm_vcpu_halt(struct kvm_vcpu *vcpu, int state, int reason)
{
	++vcpu->stat.halt_exits;
	if (lapic_in_kernel(vcpu)) {
		vcpu->arch.mp_state = state;
		return 1;
	} else {
		vcpu->run->exit_reason = reason;
		return 0;
	}
}

int kvm_vcpu_halt(struct kvm_vcpu *vcpu)
{
	return __kvm_vcpu_halt(vcpu, KVM_MP_STATE_HALTED, KVM_EXIT_HLT);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_halt);

int kvm_emulate_halt(struct kvm_vcpu *vcpu)
{
	int ret = kvm_skip_emulated_instruction(vcpu);
	/*
	 * TODO: we might be squashing a GUESTDBG_SINGLESTEP-triggered
	 * KVM_EXIT_DEBUG here.
	 */
	return kvm_vcpu_halt(vcpu) && ret;
}
EXPORT_SYMBOL_GPL(kvm_emulate_halt);

int kvm_emulate_ap_reset_hold(struct kvm_vcpu *vcpu)
{
	int ret = kvm_skip_emulated_instruction(vcpu);

	return __kvm_vcpu_halt(vcpu, KVM_MP_STATE_AP_RESET_HOLD, KVM_EXIT_AP_RESET_HOLD) && ret;
}
EXPORT_SYMBOL_GPL(kvm_emulate_ap_reset_hold);

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

	if (kvm_get_walltime_and_clockread(&ts, &cycle) == false)
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
static void kvm_pv_kick_cpu_op(struct kvm *kvm, unsigned long flags, int apicid)
{
	struct kvm_lapic_irq lapic_irq;

	lapic_irq.shorthand = APIC_DEST_NOSHORT;
	lapic_irq.dest_mode = APIC_DEST_PHYSICAL;
	lapic_irq.level = 0;
	lapic_irq.dest_id = apicid;
	lapic_irq.msi_redir_hint = false;

	lapic_irq.delivery_mode = APIC_DM_REMRD;
	kvm_irq_delivery_to_apic(kvm, NULL, &lapic_irq, NULL);
}

bool kvm_apicv_activated(struct kvm *kvm)
{
	return (READ_ONCE(kvm->arch.apicv_inhibit_reasons) == 0);
}
EXPORT_SYMBOL_GPL(kvm_apicv_activated);

void kvm_apicv_init(struct kvm *kvm, bool enable)
{
	if (enable)
		clear_bit(APICV_INHIBIT_REASON_DISABLE,
			  &kvm->arch.apicv_inhibit_reasons);
	else
		set_bit(APICV_INHIBIT_REASON_DISABLE,
			&kvm->arch.apicv_inhibit_reasons);
}
EXPORT_SYMBOL_GPL(kvm_apicv_init);

static void kvm_sched_yield(struct kvm *kvm, unsigned long dest_id)
{
	struct kvm_vcpu *target = NULL;
	struct kvm_apic_map *map;

	rcu_read_lock();
	map = rcu_dereference(kvm->arch.apic_map);

	if (likely(map) && dest_id <= map->max_apic_id && map->phys_map[dest_id])
		target = map->phys_map[dest_id]->vcpu;

	rcu_read_unlock();

	if (target && READ_ONCE(target->ready))
		kvm_vcpu_yield_to(target);
}

int kvm_emulate_hypercall(struct kvm_vcpu *vcpu)
{
	unsigned long nr, a0, a1, a2, a3, ret;
	int op_64_bit;

	if (kvm_hv_hypercall_enabled(vcpu->kvm))
		return kvm_hv_hypercall(vcpu);

	nr = kvm_rax_read(vcpu);
	a0 = kvm_rbx_read(vcpu);
	a1 = kvm_rcx_read(vcpu);
	a2 = kvm_rdx_read(vcpu);
	a3 = kvm_rsi_read(vcpu);

	trace_kvm_hypercall(nr, a0, a1, a2, a3);

	op_64_bit = is_64_bit_mode(vcpu);
	if (!op_64_bit) {
		nr &= 0xFFFFFFFF;
		a0 &= 0xFFFFFFFF;
		a1 &= 0xFFFFFFFF;
		a2 &= 0xFFFFFFFF;
		a3 &= 0xFFFFFFFF;
	}

	if (kvm_x86_ops.get_cpl(vcpu) != 0) {
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

		kvm_pv_kick_cpu_op(vcpu->kvm, a0, a1);
		kvm_sched_yield(vcpu->kvm, a1);
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

		kvm_sched_yield(vcpu->kvm, a0);
		ret = 0;
		break;
	default:
		ret = -KVM_ENOSYS;
		break;
	}
out:
	if (!op_64_bit)
		ret = (u32)ret;
	kvm_rax_write(vcpu, ret);

	++vcpu->stat.hypercalls;
	return kvm_skip_emulated_instruction(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_emulate_hypercall);

static int emulator_fix_hypercall(struct x86_emulate_ctxt *ctxt)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	char instruction[3];
	unsigned long rip = kvm_rip_read(vcpu);

	kvm_x86_ops.patch_hypercall(vcpu, instruction);

	return emulator_write_emulated(ctxt, rip, instruction, 3,
		&ctxt->exception);
}

static int dm_request_for_irq_injection(struct kvm_vcpu *vcpu)
{
	return vcpu->run->request_interrupt_window &&
		likely(!pic_in_kernel(vcpu->kvm));
}

static void post_kvm_run_save(struct kvm_vcpu *vcpu)
{
	struct kvm_run *kvm_run = vcpu->run;

	/*
	 * if_flag is obsolete and useless, so do not bother
	 * setting it for SEV-ES guests.  Userspace can just
	 * use kvm_run->ready_for_interrupt_injection.
	 */
	kvm_run->if_flag = !vcpu->arch.guest_state_protected
		&& (kvm_get_rflags(vcpu) & X86_EFLAGS_IF) != 0;

	kvm_run->flags = is_smm(vcpu) ? KVM_RUN_X86_SMM : 0;
	kvm_run->cr8 = kvm_get_cr8(vcpu);
	kvm_run->apic_base = kvm_get_apic_base(vcpu);
	kvm_run->ready_for_interrupt_injection =
		pic_in_kernel(vcpu->kvm) ||
		kvm_vcpu_ready_for_interrupt_injection(vcpu);
}

static void update_cr8_intercept(struct kvm_vcpu *vcpu)
{
	int max_irr, tpr;

	if (!kvm_x86_ops.update_cr8_intercept)
		return;

	if (!lapic_in_kernel(vcpu))
		return;

	if (vcpu->arch.apicv_active)
		return;

	if (!vcpu->arch.apic->vapic_addr)
		max_irr = kvm_lapic_find_highest_irr(vcpu);
	else
		max_irr = -1;

	if (max_irr != -1)
		max_irr >>= 4;

	tpr = kvm_lapic_get_cr8(vcpu);

	kvm_x86_ops.update_cr8_intercept(vcpu, tpr, max_irr);
}

static void inject_pending_event(struct kvm_vcpu *vcpu, bool *req_immediate_exit)
{
	int r;
	bool can_inject = true;

	/* try to reinject previous events if any */

	if (vcpu->arch.exception.injected) {
		kvm_x86_ops.queue_exception(vcpu);
		can_inject = false;
	}
	/*
	 * Do not inject an NMI or interrupt if there is a pending
	 * exception.  Exceptions and interrupts are recognized at
	 * instruction boundaries, i.e. the start of an instruction.
	 * Trap-like exceptions, e.g. #DB, have higher priority than
	 * NMIs and interrupts, i.e. traps are recognized before an
	 * NMI/interrupt that's pending on the same instruction.
	 * Fault-like exceptions, e.g. #GP and #PF, are the lowest
	 * priority, but are only generated (pended) during instruction
	 * execution, i.e. a pending fault-like exception means the
	 * fault occurred on the *previous* instruction and must be
	 * serviced prior to recognizing any new events in order to
	 * fully complete the previous instruction.
	 */
	else if (!vcpu->arch.exception.pending) {
		if (vcpu->arch.nmi_injected) {
			kvm_x86_ops.set_nmi(vcpu);
			can_inject = false;
		} else if (vcpu->arch.interrupt.injected) {
			kvm_x86_ops.set_irq(vcpu);
			can_inject = false;
		}
	}

	WARN_ON_ONCE(vcpu->arch.exception.injected &&
		     vcpu->arch.exception.pending);

	/*
	 * Call check_nested_events() even if we reinjected a previous event
	 * in order for caller to determine if it should require immediate-exit
	 * from L2 to L1 due to pending L1 events which require exit
	 * from L2 to L1.
	 */
	if (is_guest_mode(vcpu)) {
		r = kvm_x86_ops.nested_ops->check_events(vcpu);
		if (r < 0)
			goto busy;
	}

	/* try to inject new event if pending */
	if (vcpu->arch.exception.pending) {
		trace_kvm_inj_exception(vcpu->arch.exception.nr,
					vcpu->arch.exception.has_error_code,
					vcpu->arch.exception.error_code);

		vcpu->arch.exception.pending = false;
		vcpu->arch.exception.injected = true;

		if (exception_type(vcpu->arch.exception.nr) == EXCPT_FAULT)
			__kvm_set_rflags(vcpu, kvm_get_rflags(vcpu) |
					     X86_EFLAGS_RF);

		if (vcpu->arch.exception.nr == DB_VECTOR) {
			kvm_deliver_exception_payload(vcpu);
			if (vcpu->arch.dr7 & DR7_GD) {
				vcpu->arch.dr7 &= ~DR7_GD;
				kvm_update_dr7(vcpu);
			}
		}

		kvm_x86_ops.queue_exception(vcpu);
		can_inject = false;
	}

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
	if (vcpu->arch.smi_pending) {
		r = can_inject ? kvm_x86_ops.smi_allowed(vcpu, true) : -EBUSY;
		if (r < 0)
			goto busy;
		if (r) {
			vcpu->arch.smi_pending = false;
			++vcpu->arch.smi_count;
			enter_smm(vcpu);
			can_inject = false;
		} else
			kvm_x86_ops.enable_smi_window(vcpu);
	}

	if (vcpu->arch.nmi_pending) {
		r = can_inject ? kvm_x86_ops.nmi_allowed(vcpu, true) : -EBUSY;
		if (r < 0)
			goto busy;
		if (r) {
			--vcpu->arch.nmi_pending;
			vcpu->arch.nmi_injected = true;
			kvm_x86_ops.set_nmi(vcpu);
			can_inject = false;
			WARN_ON(kvm_x86_ops.nmi_allowed(vcpu, true) < 0);
		}
		if (vcpu->arch.nmi_pending)
			kvm_x86_ops.enable_nmi_window(vcpu);
	}

	if (kvm_cpu_has_injectable_intr(vcpu)) {
		r = can_inject ? kvm_x86_ops.interrupt_allowed(vcpu, true) : -EBUSY;
		if (r < 0)
			goto busy;
		if (r) {
			kvm_queue_interrupt(vcpu, kvm_cpu_get_interrupt(vcpu), false);
			kvm_x86_ops.set_irq(vcpu);
			WARN_ON(kvm_x86_ops.interrupt_allowed(vcpu, true) < 0);
		}
		if (kvm_cpu_has_injectable_intr(vcpu))
			kvm_x86_ops.enable_irq_window(vcpu);
	}

	if (is_guest_mode(vcpu) &&
	    kvm_x86_ops.nested_ops->hv_timer_pending &&
	    kvm_x86_ops.nested_ops->hv_timer_pending(vcpu))
		*req_immediate_exit = true;

	WARN_ON(vcpu->arch.exception.pending);
	return;

busy:
	*req_immediate_exit = true;
	return;
}

static void process_nmi(struct kvm_vcpu *vcpu)
{
	unsigned limit = 2;

	/*
	 * x86 is limited to one NMI running, and one NMI pending after it.
	 * If an NMI is already in progress, limit further NMIs to just one.
	 * Otherwise, allow two (and we'll inject the first one immediately).
	 */
	if (kvm_x86_ops.get_nmi_mask(vcpu) || vcpu->arch.nmi_injected)
		limit = 1;

	vcpu->arch.nmi_pending += atomic_xchg(&vcpu->arch.nmi_queued, 0);
	vcpu->arch.nmi_pending = min(vcpu->arch.nmi_pending, limit);
	kvm_make_request(KVM_REQ_EVENT, vcpu);
}

static u32 enter_smm_get_segment_flags(struct kvm_segment *seg)
{
	u32 flags = 0;
	flags |= seg->g       << 23;
	flags |= seg->db      << 22;
	flags |= seg->l       << 21;
	flags |= seg->avl     << 20;
	flags |= seg->present << 15;
	flags |= seg->dpl     << 13;
	flags |= seg->s       << 12;
	flags |= seg->type    << 8;
	return flags;
}

static void enter_smm_save_seg_32(struct kvm_vcpu *vcpu, char *buf, int n)
{
	struct kvm_segment seg;
	int offset;

	kvm_get_segment(vcpu, &seg, n);
	put_smstate(u32, buf, 0x7fa8 + n * 4, seg.selector);

	if (n < 3)
		offset = 0x7f84 + n * 12;
	else
		offset = 0x7f2c + (n - 3) * 12;

	put_smstate(u32, buf, offset + 8, seg.base);
	put_smstate(u32, buf, offset + 4, seg.limit);
	put_smstate(u32, buf, offset, enter_smm_get_segment_flags(&seg));
}

#ifdef CONFIG_X86_64
static void enter_smm_save_seg_64(struct kvm_vcpu *vcpu, char *buf, int n)
{
	struct kvm_segment seg;
	int offset;
	u16 flags;

	kvm_get_segment(vcpu, &seg, n);
	offset = 0x7e00 + n * 16;

	flags = enter_smm_get_segment_flags(&seg) >> 8;
	put_smstate(u16, buf, offset, seg.selector);
	put_smstate(u16, buf, offset + 2, flags);
	put_smstate(u32, buf, offset + 4, seg.limit);
	put_smstate(u64, buf, offset + 8, seg.base);
}
#endif

static void enter_smm_save_state_32(struct kvm_vcpu *vcpu, char *buf)
{
	struct desc_ptr dt;
	struct kvm_segment seg;
	unsigned long val;
	int i;

	put_smstate(u32, buf, 0x7ffc, kvm_read_cr0(vcpu));
	put_smstate(u32, buf, 0x7ff8, kvm_read_cr3(vcpu));
	put_smstate(u32, buf, 0x7ff4, kvm_get_rflags(vcpu));
	put_smstate(u32, buf, 0x7ff0, kvm_rip_read(vcpu));

	for (i = 0; i < 8; i++)
		put_smstate(u32, buf, 0x7fd0 + i * 4, kvm_register_read(vcpu, i));

	kvm_get_dr(vcpu, 6, &val);
	put_smstate(u32, buf, 0x7fcc, (u32)val);
	kvm_get_dr(vcpu, 7, &val);
	put_smstate(u32, buf, 0x7fc8, (u32)val);

	kvm_get_segment(vcpu, &seg, VCPU_SREG_TR);
	put_smstate(u32, buf, 0x7fc4, seg.selector);
	put_smstate(u32, buf, 0x7f64, seg.base);
	put_smstate(u32, buf, 0x7f60, seg.limit);
	put_smstate(u32, buf, 0x7f5c, enter_smm_get_segment_flags(&seg));

	kvm_get_segment(vcpu, &seg, VCPU_SREG_LDTR);
	put_smstate(u32, buf, 0x7fc0, seg.selector);
	put_smstate(u32, buf, 0x7f80, seg.base);
	put_smstate(u32, buf, 0x7f7c, seg.limit);
	put_smstate(u32, buf, 0x7f78, enter_smm_get_segment_flags(&seg));

	kvm_x86_ops.get_gdt(vcpu, &dt);
	put_smstate(u32, buf, 0x7f74, dt.address);
	put_smstate(u32, buf, 0x7f70, dt.size);

	kvm_x86_ops.get_idt(vcpu, &dt);
	put_smstate(u32, buf, 0x7f58, dt.address);
	put_smstate(u32, buf, 0x7f54, dt.size);

	for (i = 0; i < 6; i++)
		enter_smm_save_seg_32(vcpu, buf, i);

	put_smstate(u32, buf, 0x7f14, kvm_read_cr4(vcpu));

	/* revision id */
	put_smstate(u32, buf, 0x7efc, 0x00020000);
	put_smstate(u32, buf, 0x7ef8, vcpu->arch.smbase);
}

#ifdef CONFIG_X86_64
static void enter_smm_save_state_64(struct kvm_vcpu *vcpu, char *buf)
{
	struct desc_ptr dt;
	struct kvm_segment seg;
	unsigned long val;
	int i;

	for (i = 0; i < 16; i++)
		put_smstate(u64, buf, 0x7ff8 - i * 8, kvm_register_read(vcpu, i));

	put_smstate(u64, buf, 0x7f78, kvm_rip_read(vcpu));
	put_smstate(u32, buf, 0x7f70, kvm_get_rflags(vcpu));

	kvm_get_dr(vcpu, 6, &val);
	put_smstate(u64, buf, 0x7f68, val);
	kvm_get_dr(vcpu, 7, &val);
	put_smstate(u64, buf, 0x7f60, val);

	put_smstate(u64, buf, 0x7f58, kvm_read_cr0(vcpu));
	put_smstate(u64, buf, 0x7f50, kvm_read_cr3(vcpu));
	put_smstate(u64, buf, 0x7f48, kvm_read_cr4(vcpu));

	put_smstate(u32, buf, 0x7f00, vcpu->arch.smbase);

	/* revision id */
	put_smstate(u32, buf, 0x7efc, 0x00020064);

	put_smstate(u64, buf, 0x7ed0, vcpu->arch.efer);

	kvm_get_segment(vcpu, &seg, VCPU_SREG_TR);
	put_smstate(u16, buf, 0x7e90, seg.selector);
	put_smstate(u16, buf, 0x7e92, enter_smm_get_segment_flags(&seg) >> 8);
	put_smstate(u32, buf, 0x7e94, seg.limit);
	put_smstate(u64, buf, 0x7e98, seg.base);

	kvm_x86_ops.get_idt(vcpu, &dt);
	put_smstate(u32, buf, 0x7e84, dt.size);
	put_smstate(u64, buf, 0x7e88, dt.address);

	kvm_get_segment(vcpu, &seg, VCPU_SREG_LDTR);
	put_smstate(u16, buf, 0x7e70, seg.selector);
	put_smstate(u16, buf, 0x7e72, enter_smm_get_segment_flags(&seg) >> 8);
	put_smstate(u32, buf, 0x7e74, seg.limit);
	put_smstate(u64, buf, 0x7e78, seg.base);

	kvm_x86_ops.get_gdt(vcpu, &dt);
	put_smstate(u32, buf, 0x7e64, dt.size);
	put_smstate(u64, buf, 0x7e68, dt.address);

	for (i = 0; i < 6; i++)
		enter_smm_save_seg_64(vcpu, buf, i);
}
#endif

static void enter_smm(struct kvm_vcpu *vcpu)
{
	struct kvm_segment cs, ds;
	struct desc_ptr dt;
	char buf[512];
	u32 cr0;

	trace_kvm_enter_smm(vcpu->vcpu_id, vcpu->arch.smbase, true);
	memset(buf, 0, 512);
#ifdef CONFIG_X86_64
	if (guest_cpuid_has(vcpu, X86_FEATURE_LM))
		enter_smm_save_state_64(vcpu, buf);
	else
#endif
		enter_smm_save_state_32(vcpu, buf);

	/*
	 * Give pre_enter_smm() a chance to make ISA-specific changes to the
	 * vCPU state (e.g. leave guest mode) after we've saved the state into
	 * the SMM state-save area.
	 */
	kvm_x86_ops.pre_enter_smm(vcpu, buf);

	vcpu->arch.hflags |= HF_SMM_MASK;
	kvm_vcpu_write_guest(vcpu, vcpu->arch.smbase + 0xfe00, buf, sizeof(buf));

	if (kvm_x86_ops.get_nmi_mask(vcpu))
		vcpu->arch.hflags |= HF_SMM_INSIDE_NMI_MASK;
	else
		kvm_x86_ops.set_nmi_mask(vcpu, true);

	kvm_set_rflags(vcpu, X86_EFLAGS_FIXED);
	kvm_rip_write(vcpu, 0x8000);

	cr0 = vcpu->arch.cr0 & ~(X86_CR0_PE | X86_CR0_EM | X86_CR0_TS | X86_CR0_PG);
	kvm_x86_ops.set_cr0(vcpu, cr0);
	vcpu->arch.cr0 = cr0;

	kvm_x86_ops.set_cr4(vcpu, 0);

	/* Undocumented: IDT limit is set to zero on entry to SMM.  */
	dt.address = dt.size = 0;
	kvm_x86_ops.set_idt(vcpu, &dt);

	__kvm_set_dr(vcpu, 7, DR7_FIXED_1);

	cs.selector = (vcpu->arch.smbase >> 4) & 0xffff;
	cs.base = vcpu->arch.smbase;

	ds.selector = 0;
	ds.base = 0;

	cs.limit    = ds.limit = 0xffffffff;
	cs.type     = ds.type = 0x3;
	cs.dpl      = ds.dpl = 0;
	cs.db       = ds.db = 0;
	cs.s        = ds.s = 1;
	cs.l        = ds.l = 0;
	cs.g        = ds.g = 1;
	cs.avl      = ds.avl = 0;
	cs.present  = ds.present = 1;
	cs.unusable = ds.unusable = 0;
	cs.padding  = ds.padding = 0;

	kvm_set_segment(vcpu, &cs, VCPU_SREG_CS);
	kvm_set_segment(vcpu, &ds, VCPU_SREG_DS);
	kvm_set_segment(vcpu, &ds, VCPU_SREG_ES);
	kvm_set_segment(vcpu, &ds, VCPU_SREG_FS);
	kvm_set_segment(vcpu, &ds, VCPU_SREG_GS);
	kvm_set_segment(vcpu, &ds, VCPU_SREG_SS);

#ifdef CONFIG_X86_64
	if (guest_cpuid_has(vcpu, X86_FEATURE_LM))
		kvm_x86_ops.set_efer(vcpu, 0);
#endif

	kvm_update_cpuid_runtime(vcpu);
	kvm_mmu_reset_context(vcpu);
}

static void process_smi(struct kvm_vcpu *vcpu)
{
	vcpu->arch.smi_pending = true;
	kvm_make_request(KVM_REQ_EVENT, vcpu);
}

void kvm_make_scan_ioapic_request_mask(struct kvm *kvm,
				       unsigned long *vcpu_bitmap)
{
	cpumask_var_t cpus;

	zalloc_cpumask_var(&cpus, GFP_ATOMIC);

	kvm_make_vcpus_request_mask(kvm, KVM_REQ_SCAN_IOAPIC,
				    NULL, vcpu_bitmap, cpus);

	free_cpumask_var(cpus);
}

void kvm_make_scan_ioapic_request(struct kvm *kvm)
{
	kvm_make_all_cpus_request(kvm, KVM_REQ_SCAN_IOAPIC);
}

void kvm_vcpu_update_apicv(struct kvm_vcpu *vcpu)
{
	if (!lapic_in_kernel(vcpu))
		return;

	vcpu->arch.apicv_active = kvm_apicv_activated(vcpu->kvm);
	kvm_apic_update_apicv(vcpu);
	kvm_x86_ops.refresh_apicv_exec_ctrl(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_update_apicv);

/*
 * NOTE: Do not hold any lock prior to calling this.
 *
 * In particular, kvm_request_apicv_update() expects kvm->srcu not to be
 * locked, because it calls __x86_set_memory_region() which does
 * synchronize_srcu(&kvm->srcu).
 */
void kvm_request_apicv_update(struct kvm *kvm, bool activate, ulong bit)
{
	struct kvm_vcpu *except;
	unsigned long old, new, expected;

	if (!kvm_x86_ops.check_apicv_inhibit_reasons ||
	    !kvm_x86_ops.check_apicv_inhibit_reasons(bit))
		return;

	old = READ_ONCE(kvm->arch.apicv_inhibit_reasons);
	do {
		expected = new = old;
		if (activate)
			__clear_bit(bit, &new);
		else
			__set_bit(bit, &new);
		if (new == old)
			break;
		old = cmpxchg(&kvm->arch.apicv_inhibit_reasons, expected, new);
	} while (old != expected);

	if (!!old == !!new)
		return;

	trace_kvm_apicv_update_request(activate, bit);
	if (kvm_x86_ops.pre_update_apicv_exec_ctrl)
		kvm_x86_ops.pre_update_apicv_exec_ctrl(kvm, activate);

	/*
	 * Sending request to update APICV for all other vcpus,
	 * while update the calling vcpu immediately instead of
	 * waiting for another #VMEXIT to handle the request.
	 */
	except = kvm_get_running_vcpu();
	kvm_make_all_cpus_request_except(kvm, KVM_REQ_APICV_UPDATE,
					 except);
	if (except)
		kvm_vcpu_update_apicv(except);
}
EXPORT_SYMBOL_GPL(kvm_request_apicv_update);

static void vcpu_scan_ioapic(struct kvm_vcpu *vcpu)
{
	if (!kvm_apic_present(vcpu))
		return;

	bitmap_zero(vcpu->arch.ioapic_handled_vectors, 256);

	if (irqchip_split(vcpu->kvm))
		kvm_scan_ioapic_routes(vcpu, vcpu->arch.ioapic_handled_vectors);
	else {
		if (vcpu->arch.apicv_active)
			kvm_x86_ops.sync_pir_to_irr(vcpu);
		if (ioapic_in_kernel(vcpu->kvm))
			kvm_ioapic_scan_entry(vcpu, vcpu->arch.ioapic_handled_vectors);
	}

	if (is_guest_mode(vcpu))
		vcpu->arch.load_eoi_exitmap_pending = true;
	else
		kvm_make_request(KVM_REQ_LOAD_EOI_EXITMAP, vcpu);
}

static void vcpu_load_eoi_exitmap(struct kvm_vcpu *vcpu)
{
	u64 eoi_exit_bitmap[4];

	if (!kvm_apic_hw_enabled(vcpu->arch.apic))
		return;

	bitmap_or((ulong *)eoi_exit_bitmap, vcpu->arch.ioapic_handled_vectors,
		  vcpu_to_synic(vcpu)->vec_bitmap, 256);
	kvm_x86_ops.load_eoi_exitmap(vcpu, eoi_exit_bitmap);
}

void kvm_arch_mmu_notifier_invalidate_range(struct kvm *kvm,
					    unsigned long start, unsigned long end)
{
	unsigned long apic_address;

	/*
	 * The physical address of apic access page is stored in the VMCS.
	 * Update it when it becomes invalid.
	 */
	apic_address = gfn_to_hva(kvm, APIC_DEFAULT_PHYS_BASE >> PAGE_SHIFT);
	if (start <= apic_address && apic_address < end)
		kvm_make_all_cpus_request(kvm, KVM_REQ_APIC_PAGE_RELOAD);
}

void kvm_vcpu_reload_apic_access_page(struct kvm_vcpu *vcpu)
{
	if (!lapic_in_kernel(vcpu))
		return;

	if (!kvm_x86_ops.set_apic_access_page_addr)
		return;

	kvm_x86_ops.set_apic_access_page_addr(vcpu);
}

void __kvm_request_immediate_exit(struct kvm_vcpu *vcpu)
{
	smp_send_reschedule(vcpu->cpu);
}
EXPORT_SYMBOL_GPL(__kvm_request_immediate_exit);

/*
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

	/* Forbid vmenter if vcpu dirty ring is soft-full */
	if (unlikely(vcpu->kvm->dirty_ring_size &&
		     kvm_dirty_ring_soft_full(&vcpu->dirty_ring))) {
		vcpu->run->exit_reason = KVM_EXIT_DIRTY_RING_FULL;
		trace_kvm_dirty_ring_exit(vcpu);
		r = 0;
		goto out;
	}

	if (kvm_request_pending(vcpu)) {
		if (kvm_check_request(KVM_REQ_GET_NESTED_STATE_PAGES, vcpu)) {
			if (unlikely(!kvm_x86_ops.nested_ops->get_nested_state_pages(vcpu))) {
				r = 0;
				goto out;
			}
		}
		if (kvm_check_request(KVM_REQ_MMU_RELOAD, vcpu))
			kvm_mmu_unload(vcpu);
		if (kvm_check_request(KVM_REQ_MIGRATE_TIMER, vcpu))
			__kvm_migrate_timers(vcpu);
		if (kvm_check_request(KVM_REQ_MASTERCLOCK_UPDATE, vcpu))
			kvm_gen_update_masterclock(vcpu->kvm);
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
		if (kvm_check_request(KVM_REQ_TLB_FLUSH, vcpu)) {
			kvm_vcpu_flush_tlb_all(vcpu);

			/* Flushing all ASIDs flushes the current ASID... */
			kvm_clear_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
		}
		if (kvm_check_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu))
			kvm_vcpu_flush_tlb_current(vcpu);
		if (kvm_check_request(KVM_REQ_HV_TLB_FLUSH, vcpu))
			kvm_vcpu_flush_tlb_guest(vcpu);

		if (kvm_check_request(KVM_REQ_REPORT_TPR_ACCESS, vcpu)) {
			vcpu->run->exit_reason = KVM_EXIT_TPR_ACCESS;
			r = 0;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_TRIPLE_FAULT, vcpu)) {
			vcpu->run->exit_reason = KVM_EXIT_SHUTDOWN;
			vcpu->mmio_needed = 0;
			r = 0;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_APF_HALT, vcpu)) {
			/* Page is swapped out. Do synthetic halt */
			vcpu->arch.apf.halted = true;
			r = 1;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_STEAL_UPDATE, vcpu))
			record_steal_time(vcpu);
		if (kvm_check_request(KVM_REQ_SMI, vcpu))
			process_smi(vcpu);
		if (kvm_check_request(KVM_REQ_NMI, vcpu))
			process_nmi(vcpu);
		if (kvm_check_request(KVM_REQ_PMU, vcpu))
			kvm_pmu_handle_event(vcpu);
		if (kvm_check_request(KVM_REQ_PMI, vcpu))
			kvm_pmu_deliver_pmi(vcpu);
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
		if (kvm_check_request(KVM_REQ_HV_CRASH, vcpu)) {
			vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
			vcpu->run->system_event.type = KVM_SYSTEM_EVENT_CRASH;
			r = 0;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_HV_RESET, vcpu)) {
			vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
			vcpu->run->system_event.type = KVM_SYSTEM_EVENT_RESET;
			r = 0;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_HV_EXIT, vcpu)) {
			vcpu->run->exit_reason = KVM_EXIT_HYPERV;
			vcpu->run->hyperv = vcpu->arch.hyperv.exit;
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
		if (kvm_check_request(KVM_REQ_APICV_UPDATE, vcpu))
			kvm_vcpu_update_apicv(vcpu);
		if (kvm_check_request(KVM_REQ_APF_READY, vcpu))
			kvm_check_async_pf_completion(vcpu);
		if (kvm_check_request(KVM_REQ_MSR_FILTER_CHANGED, vcpu))
			kvm_x86_ops.msr_filter_changed(vcpu);
	}

	if (kvm_check_request(KVM_REQ_EVENT, vcpu) || req_int_win) {
		++vcpu->stat.req_event;
		kvm_apic_accept_events(vcpu);
		if (vcpu->arch.mp_state == KVM_MP_STATE_INIT_RECEIVED) {
			r = 1;
			goto out;
		}

		inject_pending_event(vcpu, &req_immediate_exit);
		if (req_int_win)
			kvm_x86_ops.enable_irq_window(vcpu);

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

	kvm_x86_ops.prepare_guest_switch(vcpu);

	/*
	 * Disable IRQs before setting IN_GUEST_MODE.  Posted interrupt
	 * IPI are then delayed after guest entry, which ensures that they
	 * result in virtual interrupt delivery.
	 */
	local_irq_disable();
	vcpu->mode = IN_GUEST_MODE;

	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);

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
	 * This handles the case where a posted interrupt was
	 * notified with kvm_vcpu_kick.
	 */
	if (kvm_lapic_enabled(vcpu) && vcpu->arch.apicv_active)
		kvm_x86_ops.sync_pir_to_irr(vcpu);

	if (kvm_vcpu_exit_request(vcpu)) {
		vcpu->mode = OUTSIDE_GUEST_MODE;
		smp_wmb();
		local_irq_enable();
		preempt_enable();
		vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
		r = 1;
		goto cancel_injection;
	}

	if (req_immediate_exit) {
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		kvm_x86_ops.request_immediate_exit(vcpu);
	}

	fpregs_assert_state_consistent();
	if (test_thread_flag(TIF_NEED_FPU_LOAD))
		switch_fpu_return();

	if (unlikely(vcpu->arch.switch_db_regs)) {
		set_debugreg(0, 7);
		set_debugreg(vcpu->arch.eff_db[0], 0);
		set_debugreg(vcpu->arch.eff_db[1], 1);
		set_debugreg(vcpu->arch.eff_db[2], 2);
		set_debugreg(vcpu->arch.eff_db[3], 3);
		set_debugreg(vcpu->arch.dr6, 6);
		vcpu->arch.switch_db_regs &= ~KVM_DEBUGREG_RELOAD;
	}

	exit_fastpath = kvm_x86_ops.run(vcpu);

	/*
	 * Do this here before restoring debug registers on the host.  And
	 * since we do this before handling the vmexit, a DR access vmexit
	 * can (a) read the correct value of the debug registers, (b) set
	 * KVM_DEBUGREG_WONT_EXIT again.
	 */
	if (unlikely(vcpu->arch.switch_db_regs & KVM_DEBUGREG_WONT_EXIT)) {
		WARN_ON(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP);
		kvm_x86_ops.sync_dirty_debug_regs(vcpu);
		kvm_update_dr0123(vcpu);
		kvm_update_dr7(vcpu);
		vcpu->arch.switch_db_regs &= ~KVM_DEBUGREG_RELOAD;
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

	kvm_x86_ops.handle_exit_irqoff(vcpu);

	/*
	 * Consume any pending interrupts, including the possible source of
	 * VM-Exit on SVM and any ticks that occur between VM-Exit and now.
	 * An instruction is required after local_irq_enable() to fully unblock
	 * interrupts on processors that implement an interrupt shadow, the
	 * stat.exits increment will do nicely.
	 */
	kvm_before_interrupt(vcpu);
	local_irq_enable();
	++vcpu->stat.exits;
	local_irq_disable();
	kvm_after_interrupt(vcpu);

	if (lapic_in_kernel(vcpu)) {
		s64 delta = vcpu->arch.apic->lapic_timer.advance_expire_delta;
		if (delta != S64_MIN) {
			trace_kvm_wait_lapic_expire(vcpu->vcpu_id, delta);
			vcpu->arch.apic->lapic_timer.advance_expire_delta = S64_MIN;
		}
	}

	local_irq_enable();
	preempt_enable();

	vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);

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

	r = kvm_x86_ops.handle_exit(vcpu, exit_fastpath);
	return r;

cancel_injection:
	if (req_immediate_exit)
		kvm_make_request(KVM_REQ_EVENT, vcpu);
	kvm_x86_ops.cancel_injection(vcpu);
	if (unlikely(vcpu->arch.apic_attention))
		kvm_lapic_sync_from_vapic(vcpu);
out:
	return r;
}

static inline int vcpu_block(struct kvm *kvm, struct kvm_vcpu *vcpu)
{
	if (!kvm_arch_vcpu_runnable(vcpu) &&
	    (!kvm_x86_ops.pre_block || kvm_x86_ops.pre_block(vcpu) == 0)) {
		srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);
		kvm_vcpu_block(vcpu);
		vcpu->srcu_idx = srcu_read_lock(&kvm->srcu);

		if (kvm_x86_ops.post_block)
			kvm_x86_ops.post_block(vcpu);

		if (!kvm_check_request(KVM_REQ_UNHALT, vcpu))
			return 1;
	}

	kvm_apic_accept_events(vcpu);
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
		return -EINTR;
	}
	return 1;
}

static inline bool kvm_vcpu_running(struct kvm_vcpu *vcpu)
{
	if (is_guest_mode(vcpu))
		kvm_x86_ops.nested_ops->check_events(vcpu);

	return (vcpu->arch.mp_state == KVM_MP_STATE_RUNNABLE &&
		!vcpu->arch.apf.halted);
}

static int vcpu_run(struct kvm_vcpu *vcpu)
{
	int r;
	struct kvm *kvm = vcpu->kvm;

	vcpu->srcu_idx = srcu_read_lock(&kvm->srcu);
	vcpu->arch.l1tf_flush_l1d = true;

	for (;;) {
		if (kvm_vcpu_running(vcpu)) {
			r = vcpu_enter_guest(vcpu);
		} else {
			r = vcpu_block(kvm, vcpu);
		}

		if (r <= 0)
			break;

		kvm_clear_request(KVM_REQ_PENDING_TIMER, vcpu);
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
			srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);
			r = xfer_to_guest_mode_handle_work(vcpu);
			if (r)
				return r;
			vcpu->srcu_idx = srcu_read_lock(&kvm->srcu);
		}
	}

	srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);

	return r;
}

static inline int complete_emulated_io(struct kvm_vcpu *vcpu)
{
	int r;

	vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
	r = kvm_emulate_instruction(vcpu, EMULTYPE_NO_DECODE);
	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
	return r;
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

static void kvm_save_current_fpu(struct fpu *fpu)
{
	/*
	 * If the target FPU state is not resident in the CPU registers, just
	 * memcpy() from current, else save CPU state directly to the target.
	 */
	if (test_thread_flag(TIF_NEED_FPU_LOAD))
		memcpy(&fpu->state, &current->thread.fpu.state,
		       fpu_kernel_xstate_size);
	else
		copy_fpregs_to_fpstate(fpu);
}

/* Swap (qemu) user FPU context for the guest FPU context. */
static void kvm_load_guest_fpu(struct kvm_vcpu *vcpu)
{
	fpregs_lock();

	kvm_save_current_fpu(vcpu->arch.user_fpu);

	/*
	 * Guests with protected state can't have it set by the hypervisor,
	 * so skip trying to set it.
	 */
	if (vcpu->arch.guest_fpu)
		/* PKRU is separately restored in kvm_x86_ops.run. */
		__copy_kernel_to_fpregs(&vcpu->arch.guest_fpu->state,
					~XFEATURE_MASK_PKRU);

	fpregs_mark_activate();
	fpregs_unlock();

	trace_kvm_fpu(1);
}

/* When vcpu_run ends, restore user space FPU context. */
static void kvm_put_guest_fpu(struct kvm_vcpu *vcpu)
{
	fpregs_lock();

	/*
	 * Guests with protected state can't have it read by the hypervisor,
	 * so skip trying to save it.
	 */
	if (vcpu->arch.guest_fpu)
		kvm_save_current_fpu(vcpu->arch.guest_fpu);

	copy_kernel_to_fpregs(&vcpu->arch.user_fpu->state);

	fpregs_mark_activate();
	fpregs_unlock();

	++vcpu->stat.fpu_reload;
	trace_kvm_fpu(0);
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	struct kvm_run *kvm_run = vcpu->run;
	int r;

	vcpu_load(vcpu);
	kvm_sigset_activate(vcpu);
	kvm_load_guest_fpu(vcpu);

	if (unlikely(vcpu->arch.mp_state == KVM_MP_STATE_UNINITIALIZED)) {
		if (kvm_run->immediate_exit) {
			r = -EINTR;
			goto out;
		}
		kvm_vcpu_block(vcpu);
		kvm_apic_accept_events(vcpu);
		kvm_clear_request(KVM_REQ_UNHALT, vcpu);
		r = -EAGAIN;
		if (signal_pending(current)) {
			r = -EINTR;
			kvm_run->exit_reason = KVM_EXIT_INTR;
			++vcpu->stat.signal_exits;
		}
		goto out;
	}

	if (kvm_run->kvm_valid_regs & ~KVM_SYNC_X86_VALID_FIELDS) {
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

	if (unlikely(vcpu->arch.complete_userspace_io)) {
		int (*cui)(struct kvm_vcpu *) = vcpu->arch.complete_userspace_io;
		vcpu->arch.complete_userspace_io = NULL;
		r = cui(vcpu);
		if (r <= 0)
			goto out;
	} else
		WARN_ON(vcpu->arch.pio.count || vcpu->mmio_needed);

	if (kvm_run->immediate_exit)
		r = -EINTR;
	else
		r = vcpu_run(vcpu);

out:
	kvm_put_guest_fpu(vcpu);
	if (kvm_run->kvm_valid_regs)
		store_regs(vcpu);
	post_kvm_run_save(vcpu);
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

	kvm_make_request(KVM_REQ_EVENT, vcpu);
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	vcpu_load(vcpu);
	__set_regs(vcpu, regs);
	vcpu_put(vcpu);
	return 0;
}

void kvm_get_cs_db_l_bits(struct kvm_vcpu *vcpu, int *db, int *l)
{
	struct kvm_segment cs;

	kvm_get_segment(vcpu, &cs, VCPU_SREG_CS);
	*db = cs.db;
	*l = cs.l;
}
EXPORT_SYMBOL_GPL(kvm_get_cs_db_l_bits);

static void __get_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
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

	kvm_x86_ops.get_idt(vcpu, &dt);
	sregs->idt.limit = dt.size;
	sregs->idt.base = dt.address;
	kvm_x86_ops.get_gdt(vcpu, &dt);
	sregs->gdt.limit = dt.size;
	sregs->gdt.base = dt.address;

	sregs->cr2 = vcpu->arch.cr2;
	sregs->cr3 = kvm_read_cr3(vcpu);

skip_protected_regs:
	sregs->cr0 = kvm_read_cr0(vcpu);
	sregs->cr4 = kvm_read_cr4(vcpu);
	sregs->cr8 = kvm_get_cr8(vcpu);
	sregs->efer = vcpu->arch.efer;
	sregs->apic_base = kvm_get_apic_base(vcpu);

	memset(sregs->interrupt_bitmap, 0, sizeof(sregs->interrupt_bitmap));

	if (vcpu->arch.interrupt.injected && !vcpu->arch.interrupt.soft)
		set_bit(vcpu->arch.interrupt.nr,
			(unsigned long *)sregs->interrupt_bitmap);
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	vcpu_load(vcpu);
	__get_sregs(vcpu, sregs);
	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	vcpu_load(vcpu);
	if (kvm_mpx_supported())
		kvm_load_guest_fpu(vcpu);

	kvm_apic_accept_events(vcpu);
	if ((vcpu->arch.mp_state == KVM_MP_STATE_HALTED ||
	     vcpu->arch.mp_state == KVM_MP_STATE_AP_RESET_HOLD) &&
	    vcpu->arch.pv.pv_unhalted)
		mp_state->mp_state = KVM_MP_STATE_RUNNABLE;
	else
		mp_state->mp_state = vcpu->arch.mp_state;

	if (kvm_mpx_supported())
		kvm_put_guest_fpu(vcpu);
	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	int ret = -EINVAL;

	vcpu_load(vcpu);

	if (!lapic_in_kernel(vcpu) &&
	    mp_state->mp_state != KVM_MP_STATE_RUNNABLE)
		goto out;

	/*
	 * KVM_MP_STATE_INIT_RECEIVED means the processor is in
	 * INIT state; latched init should be reported using
	 * KVM_SET_VCPU_EVENTS, so reject it here.
	 */
	if ((kvm_vcpu_latch_init(vcpu) || vcpu->arch.smi_pending) &&
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
	if (ret) {
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
	} else {
		/*
		 * Not in 64-bit mode: EFER.LMA is clear and the code
		 * segment cannot be 64-bit.
		 */
		if (sregs->efer & EFER_LMA || sregs->cs.l)
			return false;
	}

	return kvm_is_valid_cr4(vcpu, sregs->cr4);
}

static int __set_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	struct msr_data apic_base_msr;
	int mmu_reset_needed = 0;
	int pending_vec, max_bits, idx;
	struct desc_ptr dt;
	int ret = -EINVAL;

	if (!kvm_is_valid_sregs(vcpu, sregs))
		goto out;

	apic_base_msr.data = sregs->apic_base;
	apic_base_msr.host_initiated = true;
	if (kvm_set_apic_base(vcpu, &apic_base_msr))
		goto out;

	if (vcpu->arch.guest_state_protected)
		goto skip_protected_regs;

	dt.size = sregs->idt.limit;
	dt.address = sregs->idt.base;
	kvm_x86_ops.set_idt(vcpu, &dt);
	dt.size = sregs->gdt.limit;
	dt.address = sregs->gdt.base;
	kvm_x86_ops.set_gdt(vcpu, &dt);

	vcpu->arch.cr2 = sregs->cr2;
	mmu_reset_needed |= kvm_read_cr3(vcpu) != sregs->cr3;
	vcpu->arch.cr3 = sregs->cr3;
	kvm_register_mark_available(vcpu, VCPU_EXREG_CR3);

	kvm_set_cr8(vcpu, sregs->cr8);

	mmu_reset_needed |= vcpu->arch.efer != sregs->efer;
	kvm_x86_ops.set_efer(vcpu, sregs->efer);

	mmu_reset_needed |= kvm_read_cr0(vcpu) != sregs->cr0;
	kvm_x86_ops.set_cr0(vcpu, sregs->cr0);
	vcpu->arch.cr0 = sregs->cr0;

	mmu_reset_needed |= kvm_read_cr4(vcpu) != sregs->cr4;
	kvm_x86_ops.set_cr4(vcpu, sregs->cr4);

	idx = srcu_read_lock(&vcpu->kvm->srcu);
	if (is_pae_paging(vcpu)) {
		load_pdptrs(vcpu, vcpu->arch.walk_mmu, kvm_read_cr3(vcpu));
		mmu_reset_needed = 1;
	}
	srcu_read_unlock(&vcpu->kvm->srcu, idx);

	if (mmu_reset_needed)
		kvm_mmu_reset_context(vcpu);

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

skip_protected_regs:
	max_bits = KVM_NR_INTERRUPTS;
	pending_vec = find_first_bit(
		(const unsigned long *)sregs->interrupt_bitmap, max_bits);
	if (pending_vec < max_bits) {
		kvm_queue_interrupt(vcpu, pending_vec, false);
		pr_debug("Set back pending irq %d\n", pending_vec);
	}

	kvm_make_request(KVM_REQ_EVENT, vcpu);

	ret = 0;
out:
	return ret;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	int ret;

	vcpu_load(vcpu);
	ret = __set_sregs(vcpu, sregs);
	vcpu_put(vcpu);
	return ret;
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
		if (vcpu->arch.exception.pending)
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
		vcpu->arch.singlestep_rip = kvm_rip_read(vcpu) +
			get_segment_base(vcpu, VCPU_SREG_CS);

	/*
	 * Trigger an rflags update that will inject or remove the trace
	 * flags.
	 */
	kvm_set_rflags(vcpu, rflags);

	kvm_x86_ops.update_exception_bitmap(vcpu);

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
	tr->valid = gpa != UNMAPPED_GVA;
	tr->writeable = 1;
	tr->usermode = 0;

	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	struct fxregs_state *fxsave;

	if (!vcpu->arch.guest_fpu)
		return 0;

	vcpu_load(vcpu);

	fxsave = &vcpu->arch.guest_fpu->state.fxsave;
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

	if (!vcpu->arch.guest_fpu)
		return 0;

	vcpu_load(vcpu);

	fxsave = &vcpu->arch.guest_fpu->state.fxsave;

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
	if (vcpu->run->kvm_dirty_regs & ~KVM_SYNC_X86_VALID_FIELDS)
		return -EINVAL;

	if (vcpu->run->kvm_dirty_regs & KVM_SYNC_X86_REGS) {
		__set_regs(vcpu, &vcpu->run->s.regs.regs);
		vcpu->run->kvm_dirty_regs &= ~KVM_SYNC_X86_REGS;
	}
	if (vcpu->run->kvm_dirty_regs & KVM_SYNC_X86_SREGS) {
		if (__set_sregs(vcpu, &vcpu->run->s.regs.sregs))
			return -EINVAL;
		vcpu->run->kvm_dirty_regs &= ~KVM_SYNC_X86_SREGS;
	}
	if (vcpu->run->kvm_dirty_regs & KVM_SYNC_X86_EVENTS) {
		if (kvm_vcpu_ioctl_x86_set_vcpu_events(
				vcpu, &vcpu->run->s.regs.events))
			return -EINVAL;
		vcpu->run->kvm_dirty_regs &= ~KVM_SYNC_X86_EVENTS;
	}

	return 0;
}

static void fx_init(struct kvm_vcpu *vcpu)
{
	if (!vcpu->arch.guest_fpu)
		return;

	fpstate_init(&vcpu->arch.guest_fpu->state);
	if (boot_cpu_has(X86_FEATURE_XSAVES))
		vcpu->arch.guest_fpu->state.xsave.header.xcomp_bv =
			host_xcr0 | XSTATE_COMPACTION_ENABLED;

	/*
	 * Ensure guest xcr0 is valid for loading
	 */
	vcpu->arch.xcr0 = XFEATURE_MASK_FP;

	vcpu->arch.cr0 |= X86_CR0_ET;
}

void kvm_free_guest_fpu(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.guest_fpu) {
		kmem_cache_free(x86_fpu_cache, vcpu->arch.guest_fpu);
		vcpu->arch.guest_fpu = NULL;
	}
}
EXPORT_SYMBOL_GPL(kvm_free_guest_fpu);

int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	if (kvm_check_tsc_unstable() && atomic_read(&kvm->online_vcpus) != 0)
		pr_warn_once("kvm: SMP vm created on host with unstable TSC; "
			     "guest TSC will not be reliable\n");

	return 0;
}

int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	struct page *page;
	int r;

	if (!irqchip_in_kernel(vcpu->kvm) || kvm_vcpu_is_reset_bsp(vcpu))
		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
	else
		vcpu->arch.mp_state = KVM_MP_STATE_UNINITIALIZED;

	kvm_set_tsc_khz(vcpu, max_tsc_khz);

	r = kvm_mmu_create(vcpu);
	if (r < 0)
		return r;

	if (irqchip_in_kernel(vcpu->kvm)) {
		r = kvm_create_lapic(vcpu, lapic_timer_advance_ns);
		if (r < 0)
			goto fail_mmu_destroy;
		if (kvm_apicv_activated(vcpu->kvm))
			vcpu->arch.apicv_active = true;
	} else
		static_key_slow_inc(&kvm_no_apic_vcpu);

	r = -ENOMEM;

	page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!page)
		goto fail_free_lapic;
	vcpu->arch.pio_data = page_address(page);

	vcpu->arch.mce_banks = kzalloc(KVM_MAX_MCE_BANKS * sizeof(u64) * 4,
				       GFP_KERNEL_ACCOUNT);
	if (!vcpu->arch.mce_banks)
		goto fail_free_pio_data;
	vcpu->arch.mcg_cap = KVM_MAX_MCE_BANKS;

	if (!zalloc_cpumask_var(&vcpu->arch.wbinvd_dirty_mask,
				GFP_KERNEL_ACCOUNT))
		goto fail_free_mce_banks;

	if (!alloc_emulate_ctxt(vcpu))
		goto free_wbinvd_dirty_mask;

	vcpu->arch.user_fpu = kmem_cache_zalloc(x86_fpu_cache,
						GFP_KERNEL_ACCOUNT);
	if (!vcpu->arch.user_fpu) {
		pr_err("kvm: failed to allocate userspace's fpu\n");
		goto free_emulate_ctxt;
	}

	vcpu->arch.guest_fpu = kmem_cache_zalloc(x86_fpu_cache,
						 GFP_KERNEL_ACCOUNT);
	if (!vcpu->arch.guest_fpu) {
		pr_err("kvm: failed to allocate vcpu's fpu\n");
		goto free_user_fpu;
	}
	fx_init(vcpu);

	vcpu->arch.maxphyaddr = cpuid_query_maxphyaddr(vcpu);

	vcpu->arch.pat = MSR_IA32_CR_PAT_DEFAULT;

	kvm_async_pf_hash_reset(vcpu);
	kvm_pmu_init(vcpu);

	vcpu->arch.pending_external_vector = -1;
	vcpu->arch.preempted_in_kernel = false;

	kvm_hv_vcpu_init(vcpu);

	r = kvm_x86_ops.vcpu_create(vcpu);
	if (r)
		goto free_guest_fpu;

	vcpu->arch.arch_capabilities = kvm_get_arch_capabilities();
	vcpu->arch.msr_platform_info = MSR_PLATFORM_INFO_CPUID_FAULT;
	kvm_vcpu_mtrr_init(vcpu);
	vcpu_load(vcpu);
	kvm_vcpu_reset(vcpu, false);
	kvm_init_mmu(vcpu, false);
	vcpu_put(vcpu);
	return 0;

free_guest_fpu:
	kvm_free_guest_fpu(vcpu);
free_user_fpu:
	kmem_cache_free(x86_fpu_cache, vcpu->arch.user_fpu);
free_emulate_ctxt:
	kmem_cache_free(x86_emulator_cache, vcpu->arch.emulate_ctxt);
free_wbinvd_dirty_mask:
	free_cpumask_var(vcpu->arch.wbinvd_dirty_mask);
fail_free_mce_banks:
	kfree(vcpu->arch.mce_banks);
fail_free_pio_data:
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

	kvm_hv_vcpu_postcreate(vcpu);

	if (mutex_lock_killable(&vcpu->mutex))
		return;
	vcpu_load(vcpu);
	kvm_synchronize_tsc(vcpu, 0);
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
	struct gfn_to_pfn_cache *cache = &vcpu->arch.st.cache;
	int idx;

	kvm_release_pfn(cache->pfn, cache->dirty, cache);

	kvmclock_reset(vcpu);

	kvm_x86_ops.vcpu_free(vcpu);

	kmem_cache_free(x86_emulator_cache, vcpu->arch.emulate_ctxt);
	free_cpumask_var(vcpu->arch.wbinvd_dirty_mask);
	kmem_cache_free(x86_fpu_cache, vcpu->arch.user_fpu);
	kvm_free_guest_fpu(vcpu);

	kvm_hv_vcpu_uninit(vcpu);
	kvm_pmu_destroy(vcpu);
	kfree(vcpu->arch.mce_banks);
	kvm_free_lapic(vcpu);
	idx = srcu_read_lock(&vcpu->kvm->srcu);
	kvm_mmu_destroy(vcpu);
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	free_page((unsigned long)vcpu->arch.pio_data);
	kvfree(vcpu->arch.cpuid_entries);
	if (!lapic_in_kernel(vcpu))
		static_key_slow_dec(&kvm_no_apic_vcpu);
}

void kvm_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event)
{
	kvm_lapic_reset(vcpu, init_event);

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
	vcpu->arch.dr6 = DR6_INIT;
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

	if (vcpu->arch.guest_fpu && kvm_mpx_supported()) {
		void *mpx_state_buffer;

		/*
		 * To avoid have the INIT path from kvm_apic_has_events() that be
		 * called with loaded FPU and does not let userspace fix the state.
		 */
		if (init_event)
			kvm_put_guest_fpu(vcpu);
		mpx_state_buffer = get_xsave_addr(&vcpu->arch.guest_fpu->state.xsave,
					XFEATURE_BNDREGS);
		if (mpx_state_buffer)
			memset(mpx_state_buffer, 0, sizeof(struct mpx_bndreg_state));
		mpx_state_buffer = get_xsave_addr(&vcpu->arch.guest_fpu->state.xsave,
					XFEATURE_BNDCSR);
		if (mpx_state_buffer)
			memset(mpx_state_buffer, 0, sizeof(struct mpx_bndcsr));
		if (init_event)
			kvm_load_guest_fpu(vcpu);
	}

	if (!init_event) {
		kvm_pmu_reset(vcpu);
		vcpu->arch.smbase = 0x30000;

		vcpu->arch.msr_misc_features_enables = 0;

		vcpu->arch.xcr0 = XFEATURE_MASK_FP;
	}

	memset(vcpu->arch.regs, 0, sizeof(vcpu->arch.regs));
	vcpu->arch.regs_avail = ~0;
	vcpu->arch.regs_dirty = ~0;

	vcpu->arch.ia32_xss = 0;

	kvm_x86_ops.vcpu_reset(vcpu, init_event);
}

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

int kvm_arch_hardware_enable(void)
{
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
	int i;
	int ret;
	u64 local_tsc;
	u64 max_tsc = 0;
	bool stable, backwards_tsc = false;

	kvm_user_return_msr_cpu_online();
	ret = kvm_x86_ops.hardware_enable();
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

void kvm_arch_hardware_disable(void)
{
	kvm_x86_ops.hardware_disable();
	drop_user_return_notifiers();
}

int kvm_arch_hardware_setup(void *opaque)
{
	struct kvm_x86_init_ops *ops = opaque;
	int r;

	rdmsrl_safe(MSR_EFER, &host_efer);

	if (boot_cpu_has(X86_FEATURE_XSAVES))
		rdmsrl(MSR_IA32_XSS, host_xss);

	r = ops->hardware_setup();
	if (r != 0)
		return r;

	memcpy(&kvm_x86_ops, ops->runtime_ops, sizeof(kvm_x86_ops));

	if (!kvm_cpu_cap_has(X86_FEATURE_XSAVES))
		supported_xss = 0;

#define __kvm_cpu_cap_has(UNUSED_, f) kvm_cpu_cap_has(f)
	cr4_reserved_bits = __cr4_reserved_bits(__kvm_cpu_cap_has, UNUSED_);
#undef __kvm_cpu_cap_has

	if (kvm_has_tsc_control) {
		/*
		 * Make sure the user can only configure tsc_khz values that
		 * fit into a signed integer.
		 * A min value is not calculated because it will always
		 * be 1 on all machines.
		 */
		u64 max = min(0x7fffffffULL,
			      __scale_tsc(kvm_max_tsc_scaling_ratio, tsc_khz));
		kvm_max_guest_tsc_khz = max;

		kvm_default_tsc_scaling_ratio = 1ULL << kvm_tsc_scaling_ratio_frac_bits;
	}

	kvm_init_msr_list();
	return 0;
}

void kvm_arch_hardware_unsetup(void)
{
	kvm_x86_ops.hardware_unsetup();
}

int kvm_arch_check_processor_compat(void *opaque)
{
	struct cpuinfo_x86 *c = &cpu_data(smp_processor_id());
	struct kvm_x86_init_ops *ops = opaque;

	WARN_ON(!irqs_disabled());

	if (__cr4_reserved_bits(cpu_has, c) !=
	    __cr4_reserved_bits(cpu_has, &boot_cpu_data))
		return -EIO;

	return ops->check_processor_compatibility();
}

bool kvm_vcpu_is_reset_bsp(struct kvm_vcpu *vcpu)
{
	return vcpu->kvm->arch.bsp_vcpu_id == vcpu->vcpu_id;
}
EXPORT_SYMBOL_GPL(kvm_vcpu_is_reset_bsp);

bool kvm_vcpu_is_bsp(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.apic_base & MSR_IA32_APICBASE_BSP) != 0;
}

struct static_key kvm_no_apic_vcpu __read_mostly;
EXPORT_SYMBOL_GPL(kvm_no_apic_vcpu);

void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);

	vcpu->arch.l1tf_flush_l1d = true;
	if (pmu->version && unlikely(pmu->event_count)) {
		pmu->need_cleanup = true;
		kvm_make_request(KVM_REQ_PMU, vcpu);
	}
	kvm_x86_ops.sched_in(vcpu, cpu);
}

void kvm_arch_free_vm(struct kvm *kvm)
{
	kfree(kvm->arch.hyperv.hv_pa_pg);
	vfree(kvm);
}


int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	if (type)
		return -EINVAL;

	INIT_HLIST_HEAD(&kvm->arch.mask_notifier_list);
	INIT_LIST_HEAD(&kvm->arch.active_mmu_pages);
	INIT_LIST_HEAD(&kvm->arch.zapped_obsolete_pages);
	INIT_LIST_HEAD(&kvm->arch.lpage_disallowed_mmu_pages);
	INIT_LIST_HEAD(&kvm->arch.assigned_dev_head);
	atomic_set(&kvm->arch.noncoherent_dma_count, 0);

	/* Reserve bit 0 of irq_sources_bitmap for userspace irq source */
	set_bit(KVM_USERSPACE_IRQ_SOURCE_ID, &kvm->arch.irq_sources_bitmap);
	/* Reserve bit 1 of irq_sources_bitmap for irqfd-resampler */
	set_bit(KVM_IRQFD_RESAMPLE_IRQ_SOURCE_ID,
		&kvm->arch.irq_sources_bitmap);

	raw_spin_lock_init(&kvm->arch.tsc_write_lock);
	mutex_init(&kvm->arch.apic_map_lock);
	spin_lock_init(&kvm->arch.pvclock_gtod_sync_lock);

	kvm->arch.kvmclock_offset = -get_kvmclock_base_ns();
	pvclock_update_vm_gtod_copy(kvm);

	kvm->arch.guest_can_read_msr_platform_info = true;

	INIT_DELAYED_WORK(&kvm->arch.kvmclock_update_work, kvmclock_update_fn);
	INIT_DELAYED_WORK(&kvm->arch.kvmclock_sync_work, kvmclock_sync_fn);

	kvm_hv_init_vm(kvm);
	kvm_page_track_init(kvm);
	kvm_mmu_init_vm(kvm);

	return kvm_x86_ops.vm_init(kvm);
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

static void kvm_free_vcpus(struct kvm *kvm)
{
	unsigned int i;
	struct kvm_vcpu *vcpu;

	/*
	 * Unpin any mmu pages first.
	 */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		kvm_clear_async_pf_completion_queue(vcpu);
		kvm_unload_vcpu_mmu(vcpu);
	}
	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_vcpu_destroy(vcpu);

	mutex_lock(&kvm->lock);
	for (i = 0; i < atomic_read(&kvm->online_vcpus); i++)
		kvm->vcpus[i] = NULL;

	atomic_set(&kvm->online_vcpus, 0);
	mutex_unlock(&kvm->lock);
}

void kvm_arch_sync_events(struct kvm *kvm)
{
	cancel_delayed_work_sync(&kvm->arch.kvmclock_sync_work);
	cancel_delayed_work_sync(&kvm->arch.kvmclock_update_work);
	kvm_free_pit(kvm);
}

#define  ERR_PTR_USR(e)  ((void __user *)ERR_PTR(e))

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
		if (IS_ERR((void *)hva))
			return (void __user *)hva;
	} else {
		if (!slot || !slot->npages)
			return 0;

		old_npages = slot->npages;
		hva = 0;
	}

	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++) {
		struct kvm_userspace_memory_region m;

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
	u32 i;

	if (current->mm == kvm->mm) {
		/*
		 * Free memory regions allocated on behalf of userspace,
		 * unless the the memory map has changed due to process exit
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
	if (kvm_x86_ops.vm_destroy)
		kvm_x86_ops.vm_destroy(kvm);
	for (i = 0; i < kvm->arch.msr_filter.count; i++)
		kfree(kvm->arch.msr_filter.ranges[i].bitmap);
	kvm_pic_destroy(kvm);
	kvm_ioapic_destroy(kvm);
	kvm_free_vcpus(kvm);
	kvfree(rcu_dereference_check(kvm->arch.apic_map, 1));
	kfree(srcu_dereference_check(kvm->arch.pmu_event_filter, &kvm->srcu, 1));
	kvm_mmu_uninit_vm(kvm);
	kvm_page_track_cleanup(kvm);
	kvm_hv_destroy_vm(kvm);
}

void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	int i;

	for (i = 0; i < KVM_NR_PAGE_SIZES; ++i) {
		kvfree(slot->arch.rmap[i]);
		slot->arch.rmap[i] = NULL;

		if (i == 0)
			continue;

		kvfree(slot->arch.lpage_info[i - 1]);
		slot->arch.lpage_info[i - 1] = NULL;
	}

	kvm_page_track_free_memslot(slot);
}

static int kvm_alloc_memslot_metadata(struct kvm_memory_slot *slot,
				      unsigned long npages)
{
	int i;

	/*
	 * Clear out the previous array pointers for the KVM_MR_MOVE case.  The
	 * old arrays will be freed by __kvm_set_memory_region() if installing
	 * the new memslot is successful.
	 */
	memset(&slot->arch, 0, sizeof(slot->arch));

	for (i = 0; i < KVM_NR_PAGE_SIZES; ++i) {
		struct kvm_lpage_info *linfo;
		unsigned long ugfn;
		int lpages;
		int level = i + 1;

		lpages = gfn_to_index(slot->base_gfn + npages - 1,
				      slot->base_gfn, level) + 1;

		slot->arch.rmap[i] =
			kvcalloc(lpages, sizeof(*slot->arch.rmap[i]),
				 GFP_KERNEL_ACCOUNT);
		if (!slot->arch.rmap[i])
			goto out_free;
		if (i == 0)
			continue;

		linfo = kvcalloc(lpages, sizeof(*linfo), GFP_KERNEL_ACCOUNT);
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

	if (kvm_page_track_create_memslot(slot, npages))
		goto out_free;

	return 0;

out_free:
	for (i = 0; i < KVM_NR_PAGE_SIZES; ++i) {
		kvfree(slot->arch.rmap[i]);
		slot->arch.rmap[i] = NULL;
		if (i == 0)
			continue;

		kvfree(slot->arch.lpage_info[i - 1]);
		slot->arch.lpage_info[i - 1] = NULL;
	}
	return -ENOMEM;
}

void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen)
{
	struct kvm_vcpu *vcpu;
	int i;

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
				struct kvm_memory_slot *memslot,
				const struct kvm_userspace_memory_region *mem,
				enum kvm_mr_change change)
{
	if (change == KVM_MR_CREATE || change == KVM_MR_MOVE)
		return kvm_alloc_memslot_metadata(memslot,
						  mem->memory_size >> PAGE_SHIFT);
	return 0;
}

static void kvm_mmu_slot_apply_flags(struct kvm *kvm,
				     struct kvm_memory_slot *old,
				     struct kvm_memory_slot *new,
				     enum kvm_mr_change change)
{
	/*
	 * Nothing to do for RO slots or CREATE/MOVE/DELETE of a slot.
	 * See comments below.
	 */
	if ((change != KVM_MR_FLAGS_ONLY) || (new->flags & KVM_MEM_READONLY))
		return;

	/*
	 * Dirty logging tracks sptes in 4k granularity, meaning that large
	 * sptes have to be split.  If live migration is successful, the guest
	 * in the source machine will be destroyed and large sptes will be
	 * created in the destination. However, if the guest continues to run
	 * in the source machine (for example if live migration fails), small
	 * sptes will remain around and cause bad performance.
	 *
	 * Scan sptes if dirty logging has been stopped, dropping those
	 * which can be collapsed into a single large-page spte.  Later
	 * page faults will create the large-page sptes.
	 *
	 * There is no need to do this in any of the following cases:
	 * CREATE:      No dirty mappings will already exist.
	 * MOVE/DELETE: The old mappings will already have been cleaned up by
	 *		kvm_arch_flush_shadow_memslot()
	 */
	if ((old->flags & KVM_MEM_LOG_DIRTY_PAGES) &&
	    !(new->flags & KVM_MEM_LOG_DIRTY_PAGES))
		kvm_mmu_zap_collapsible_sptes(kvm, new);

	/*
	 * Enable or disable dirty logging for the slot.
	 *
	 * For KVM_MR_DELETE and KVM_MR_MOVE, the shadow pages of the old
	 * slot have been zapped so no dirty logging updates are needed for
	 * the old slot.
	 * For KVM_MR_CREATE and KVM_MR_MOVE, once the new slot is visible
	 * any mappings that might be created in it will consume the
	 * properties of the new slot and do not need to be updated here.
	 *
	 * When PML is enabled, the kvm_x86_ops dirty logging hooks are
	 * called to enable/disable dirty logging.
	 *
	 * When disabling dirty logging with PML enabled, the D-bit is set
	 * for sptes in the slot in order to prevent unnecessary GPA
	 * logging in the PML buffer (and potential PML buffer full VMEXIT).
	 * This guarantees leaving PML enabled for the guest's lifetime
	 * won't have any additional overhead from PML when the guest is
	 * running with dirty logging disabled.
	 *
	 * When enabling dirty logging, large sptes are write-protected
	 * so they can be split on first write.  New large sptes cannot
	 * be created for this slot until the end of the logging.
	 * See the comments in fast_page_fault().
	 * For small sptes, nothing is done if the dirty log is in the
	 * initial-all-set state.  Otherwise, depending on whether pml
	 * is enabled the D-bit or the W-bit will be cleared.
	 */
	if (new->flags & KVM_MEM_LOG_DIRTY_PAGES) {
		if (kvm_x86_ops.slot_enable_log_dirty) {
			kvm_x86_ops.slot_enable_log_dirty(kvm, new);
		} else {
			int level =
				kvm_dirty_log_manual_protect_and_init_set(kvm) ?
				PG_LEVEL_2M : PG_LEVEL_4K;

			/*
			 * If we're with initial-all-set, we don't need
			 * to write protect any small page because
			 * they're reported as dirty already.  However
			 * we still need to write-protect huge pages
			 * so that the page split can happen lazily on
			 * the first write to the huge page.
			 */
			kvm_mmu_slot_remove_write_access(kvm, new, level);
		}
	} else {
		if (kvm_x86_ops.slot_disable_log_dirty)
			kvm_x86_ops.slot_disable_log_dirty(kvm, new);
	}
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	if (!kvm->arch.n_requested_mmu_pages)
		kvm_mmu_change_mmu_pages(kvm,
				kvm_mmu_calculate_default_mmu_pages(kvm));

	/*
	 * FIXME: const-ify all uses of struct kvm_memory_slot.
	 */
	kvm_mmu_slot_apply_flags(kvm, old, (struct kvm_memory_slot *) new, change);

	/* Free the arrays associated with the old memslot. */
	if (change == KVM_MR_MOVE)
		kvm_arch_free_memslot(kvm, old);
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	kvm_mmu_zap_all(kvm);
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
	kvm_page_track_flush_slot(kvm, slot);
}

static inline bool kvm_guest_apic_has_interrupt(struct kvm_vcpu *vcpu)
{
	return (is_guest_mode(vcpu) &&
			kvm_x86_ops.guest_apic_has_interrupt &&
			kvm_x86_ops.guest_apic_has_interrupt(vcpu));
}

static inline bool kvm_vcpu_has_events(struct kvm_vcpu *vcpu)
{
	if (!list_empty_careful(&vcpu->async_pf.done))
		return true;

	if (kvm_apic_has_events(vcpu))
		return true;

	if (vcpu->arch.pv.pv_unhalted)
		return true;

	if (vcpu->arch.exception.pending)
		return true;

	if (kvm_test_request(KVM_REQ_NMI, vcpu) ||
	    (vcpu->arch.nmi_pending &&
	     kvm_x86_ops.nmi_allowed(vcpu, false)))
		return true;

	if (kvm_test_request(KVM_REQ_SMI, vcpu) ||
	    (vcpu->arch.smi_pending &&
	     kvm_x86_ops.smi_allowed(vcpu, false)))
		return true;

	if (kvm_arch_interrupt_allowed(vcpu) &&
	    (kvm_cpu_has_interrupt(vcpu) ||
	    kvm_guest_apic_has_interrupt(vcpu)))
		return true;

	if (kvm_hv_has_stimer_pending(vcpu))
		return true;

	if (is_guest_mode(vcpu) &&
	    kvm_x86_ops.nested_ops->hv_timer_pending &&
	    kvm_x86_ops.nested_ops->hv_timer_pending(vcpu))
		return true;

	return false;
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_running(vcpu) || kvm_vcpu_has_events(vcpu);
}

bool kvm_arch_dy_runnable(struct kvm_vcpu *vcpu)
{
	if (READ_ONCE(vcpu->arch.pv.pv_unhalted))
		return true;

	if (kvm_test_request(KVM_REQ_NMI, vcpu) ||
		kvm_test_request(KVM_REQ_SMI, vcpu) ||
		 kvm_test_request(KVM_REQ_EVENT, vcpu))
		return true;

	if (vcpu->arch.apicv_active && kvm_x86_ops.dy_apicv_has_pending_interrupt(vcpu))
		return true;

	return false;
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.preempted_in_kernel;
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_exiting_guest_mode(vcpu) == IN_GUEST_MODE;
}

int kvm_arch_interrupt_allowed(struct kvm_vcpu *vcpu)
{
	return kvm_x86_ops.interrupt_allowed(vcpu, false);
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

	rflags = kvm_x86_ops.get_rflags(vcpu);
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
	kvm_x86_ops.set_rflags(vcpu, rflags);
}

void kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	__kvm_set_rflags(vcpu, rflags);
	kvm_make_request(KVM_REQ_EVENT, vcpu);
}
EXPORT_SYMBOL_GPL(kvm_set_rflags);

void kvm_arch_async_page_ready(struct kvm_vcpu *vcpu, struct kvm_async_pf *work)
{
	int r;

	if ((vcpu->arch.mmu->direct_map != work->arch.direct_map) ||
	      work->wakeup_all)
		return;

	r = kvm_mmu_reload(vcpu);
	if (unlikely(r))
		return;

	if (!vcpu->arch.mmu->direct_map &&
	      work->arch.cr3 != vcpu->arch.mmu->get_guest_pgd(vcpu))
		return;

	kvm_mmu_do_page_fault(vcpu, work->cr2_or_gpa, 0, true);
}

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
	if (!vcpu->arch.apf.delivery_as_pf_vmexit && is_guest_mode(vcpu))
		return false;

	if (!kvm_pv_async_pf_enabled(vcpu) ||
	    (vcpu->arch.apf.send_user_only && kvm_x86_ops.get_cpl(vcpu) == 0))
		return false;

	return true;
}

bool kvm_can_do_async_pf(struct kvm_vcpu *vcpu)
{
	if (unlikely(!lapic_in_kernel(vcpu) ||
		     kvm_event_needs_reinjection(vcpu) ||
		     vcpu->arch.exception.pending))
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
		return apf_pageready_slot_free(vcpu);
}

void kvm_arch_start_assignment(struct kvm *kvm)
{
	atomic_inc(&kvm->arch.assigned_device_count);
}
EXPORT_SYMBOL_GPL(kvm_arch_start_assignment);

void kvm_arch_end_assignment(struct kvm *kvm)
{
	atomic_dec(&kvm->arch.assigned_device_count);
}
EXPORT_SYMBOL_GPL(kvm_arch_end_assignment);

bool kvm_arch_has_assigned_device(struct kvm *kvm)
{
	return atomic_read(&kvm->arch.assigned_device_count);
}
EXPORT_SYMBOL_GPL(kvm_arch_has_assigned_device);

void kvm_arch_register_noncoherent_dma(struct kvm *kvm)
{
	atomic_inc(&kvm->arch.noncoherent_dma_count);
}
EXPORT_SYMBOL_GPL(kvm_arch_register_noncoherent_dma);

void kvm_arch_unregister_noncoherent_dma(struct kvm *kvm)
{
	atomic_dec(&kvm->arch.noncoherent_dma_count);
}
EXPORT_SYMBOL_GPL(kvm_arch_unregister_noncoherent_dma);

bool kvm_arch_has_noncoherent_dma(struct kvm *kvm)
{
	return atomic_read(&kvm->arch.noncoherent_dma_count);
}
EXPORT_SYMBOL_GPL(kvm_arch_has_noncoherent_dma);

bool kvm_arch_has_irq_bypass(void)
{
	return true;
}

int kvm_arch_irq_bypass_add_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);
	int ret;

	irqfd->producer = prod;
	kvm_arch_start_assignment(irqfd->kvm);
	ret = kvm_x86_ops.update_pi_irte(irqfd->kvm,
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
	ret = kvm_x86_ops.update_pi_irte(irqfd->kvm, prod->irq, irqfd->gsi, 0);
	if (ret)
		printk(KERN_INFO "irq bypass consumer (token %p) unregistration"
		       " fails: %d\n", irqfd->consumer.token, ret);

	kvm_arch_end_assignment(irqfd->kvm);
}

int kvm_arch_update_irqfd_routing(struct kvm *kvm, unsigned int host_irq,
				   uint32_t guest_irq, bool set)
{
	return kvm_x86_ops.update_pi_irte(kvm, host_irq, guest_irq, set);
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
	struct x86_exception fault;
	u32 access = error_code &
		(PFERR_WRITE_MASK | PFERR_FETCH_MASK | PFERR_USER_MASK);

	if (!(error_code & PFERR_PRESENT_MASK) ||
	    vcpu->arch.walk_mmu->gva_to_gpa(vcpu, gva, access, &fault) != UNMAPPED_GVA) {
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
	vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
	vcpu->run->internal.suberror = KVM_INTERNAL_ERROR_EMULATION;
	vcpu->run->internal.ndata = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_handle_memory_failure);

int kvm_handle_invpcid(struct kvm_vcpu *vcpu, unsigned long type, gva_t gva)
{
	bool pcid_enabled;
	struct x86_exception e;
	unsigned i;
	unsigned long roots_to_free = 0;
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
			kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
		}

		for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
			if (kvm_get_pcid(vcpu, vcpu->arch.mmu->prev_roots[i].pgd)
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

		fallthrough;
	case INVPCID_TYPE_ALL_INCL_GLOBAL:
		kvm_mmu_unload(vcpu);
		return kvm_skip_emulated_instruction(vcpu);

	default:
		BUG(); /* We have already checked above that type <= 3 */
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

static int complete_sev_es_emulated_ins(struct kvm_vcpu *vcpu)
{
	memcpy(vcpu->arch.guest_ins_data, vcpu->arch.pio_data,
	       vcpu->arch.pio.count * vcpu->arch.pio.size);
	vcpu->arch.pio.count = 0;

	return 1;
}

static int kvm_sev_es_outs(struct kvm_vcpu *vcpu, unsigned int size,
			   unsigned int port, void *data,  unsigned int count)
{
	int ret;

	ret = emulator_pio_out_emulated(vcpu->arch.emulate_ctxt, size, port,
					data, count);
	if (ret)
		return ret;

	vcpu->arch.pio.count = 0;

	return 0;
}

static int kvm_sev_es_ins(struct kvm_vcpu *vcpu, unsigned int size,
			  unsigned int port, void *data, unsigned int count)
{
	int ret;

	ret = emulator_pio_in_emulated(vcpu->arch.emulate_ctxt, size, port,
				       data, count);
	if (ret) {
		vcpu->arch.pio.count = 0;
	} else {
		vcpu->arch.guest_ins_data = data;
		vcpu->arch.complete_userspace_io = complete_sev_es_emulated_ins;
	}

	return 0;
}

int kvm_sev_es_string_io(struct kvm_vcpu *vcpu, unsigned int size,
			 unsigned int port, void *data,  unsigned int count,
			 int in)
{
	return in ? kvm_sev_es_ins(vcpu, size, port, data, count)
		  : kvm_sev_es_outs(vcpu, size, port, data, count);
}
EXPORT_SYMBOL_GPL(kvm_sev_es_string_io);

EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_entry);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_exit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_fast_mmio);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_inj_virq);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_page_fault);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_msr);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_cr);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_vmrun);
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
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_apicv_update_request);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_vmgexit_enter);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_vmgexit_exit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_vmgexit_msr_protocol_enter);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_vmgexit_msr_protocol_exit);

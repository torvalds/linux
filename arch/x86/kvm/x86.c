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
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <linux/kvm_host.h>
#include "irq.h"
#include "mmu.h"
#include "i8254.h"
#include "tss.h"
#include "kvm_cache_regs.h"
#include "x86.h"
#include "cpuid.h"

#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/kvm.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
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
#include <trace/events/kvm.h>

#define CREATE_TRACE_POINTS
#include "trace.h"

#include <asm/debugreg.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include <asm/mtrr.h>
#include <asm/mce.h>
#include <asm/i387.h>
#include <asm/fpu-internal.h> /* Ugh! */
#include <asm/xcr.h>
#include <asm/pvclock.h>
#include <asm/div64.h>

#define MAX_IO_MSRS 256
#define KVM_MAX_MCE_BANKS 32
#define KVM_MCE_CAP_SUPPORTED (MCG_CTL_P | MCG_SER_P)

#define emul_to_vcpu(ctxt) \
	container_of(ctxt, struct kvm_vcpu, arch.emulate_ctxt)

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

#define VM_STAT(x) offsetof(struct kvm, stat.x), KVM_STAT_VM
#define VCPU_STAT(x) offsetof(struct kvm_vcpu, stat.x), KVM_STAT_VCPU

static void update_cr8_intercept(struct kvm_vcpu *vcpu);
static void process_nmi(struct kvm_vcpu *vcpu);

struct kvm_x86_ops *kvm_x86_ops;
EXPORT_SYMBOL_GPL(kvm_x86_ops);

static bool ignore_msrs = 0;
module_param(ignore_msrs, bool, S_IRUGO | S_IWUSR);

bool kvm_has_tsc_control;
EXPORT_SYMBOL_GPL(kvm_has_tsc_control);
u32  kvm_max_guest_tsc_khz;
EXPORT_SYMBOL_GPL(kvm_max_guest_tsc_khz);

/* tsc tolerance in parts per million - default to 1/2 of the NTP threshold */
static u32 tsc_tolerance_ppm = 250;
module_param(tsc_tolerance_ppm, uint, S_IRUGO | S_IWUSR);

#define KVM_NR_SHARED_MSRS 16

struct kvm_shared_msrs_global {
	int nr;
	u32 msrs[KVM_NR_SHARED_MSRS];
};

struct kvm_shared_msrs {
	struct user_return_notifier urn;
	bool registered;
	struct kvm_shared_msr_values {
		u64 host;
		u64 curr;
	} values[KVM_NR_SHARED_MSRS];
};

static struct kvm_shared_msrs_global __read_mostly shared_msrs_global;
static DEFINE_PER_CPU(struct kvm_shared_msrs, shared_msrs);

struct kvm_stats_debugfs_item debugfs_entries[] = {
	{ "pf_fixed", VCPU_STAT(pf_fixed) },
	{ "pf_guest", VCPU_STAT(pf_guest) },
	{ "tlb_flush", VCPU_STAT(tlb_flush) },
	{ "invlpg", VCPU_STAT(invlpg) },
	{ "exits", VCPU_STAT(exits) },
	{ "io_exits", VCPU_STAT(io_exits) },
	{ "mmio_exits", VCPU_STAT(mmio_exits) },
	{ "signal_exits", VCPU_STAT(signal_exits) },
	{ "irq_window", VCPU_STAT(irq_window_exits) },
	{ "nmi_window", VCPU_STAT(nmi_window_exits) },
	{ "halt_exits", VCPU_STAT(halt_exits) },
	{ "halt_wakeup", VCPU_STAT(halt_wakeup) },
	{ "hypercalls", VCPU_STAT(hypercalls) },
	{ "request_irq", VCPU_STAT(request_irq_exits) },
	{ "irq_exits", VCPU_STAT(irq_exits) },
	{ "host_state_reload", VCPU_STAT(host_state_reload) },
	{ "efer_reload", VCPU_STAT(efer_reload) },
	{ "fpu_reload", VCPU_STAT(fpu_reload) },
	{ "insn_emulation", VCPU_STAT(insn_emulation) },
	{ "insn_emulation_fail", VCPU_STAT(insn_emulation_fail) },
	{ "irq_injections", VCPU_STAT(irq_injections) },
	{ "nmi_injections", VCPU_STAT(nmi_injections) },
	{ "mmu_shadow_zapped", VM_STAT(mmu_shadow_zapped) },
	{ "mmu_pte_write", VM_STAT(mmu_pte_write) },
	{ "mmu_pte_updated", VM_STAT(mmu_pte_updated) },
	{ "mmu_pde_zapped", VM_STAT(mmu_pde_zapped) },
	{ "mmu_flooded", VM_STAT(mmu_flooded) },
	{ "mmu_recycled", VM_STAT(mmu_recycled) },
	{ "mmu_cache_miss", VM_STAT(mmu_cache_miss) },
	{ "mmu_unsync", VM_STAT(mmu_unsync) },
	{ "remote_tlb_flush", VM_STAT(remote_tlb_flush) },
	{ "largepages", VM_STAT(lpages) },
	{ NULL }
};

u64 __read_mostly host_xcr0;

int emulator_fix_hypercall(struct x86_emulate_ctxt *ctxt);

static inline void kvm_async_pf_hash_reset(struct kvm_vcpu *vcpu)
{
	int i;
	for (i = 0; i < roundup_pow_of_two(ASYNC_PF_PER_VCPU); i++)
		vcpu->arch.apf.gfns[i] = ~0;
}

static void kvm_on_user_return(struct user_return_notifier *urn)
{
	unsigned slot;
	struct kvm_shared_msrs *locals
		= container_of(urn, struct kvm_shared_msrs, urn);
	struct kvm_shared_msr_values *values;

	for (slot = 0; slot < shared_msrs_global.nr; ++slot) {
		values = &locals->values[slot];
		if (values->host != values->curr) {
			wrmsrl(shared_msrs_global.msrs[slot], values->host);
			values->curr = values->host;
		}
	}
	locals->registered = false;
	user_return_notifier_unregister(urn);
}

static void shared_msr_update(unsigned slot, u32 msr)
{
	struct kvm_shared_msrs *smsr;
	u64 value;

	smsr = &__get_cpu_var(shared_msrs);
	/* only read, and nobody should modify it at this time,
	 * so don't need lock */
	if (slot >= shared_msrs_global.nr) {
		printk(KERN_ERR "kvm: invalid MSR slot!");
		return;
	}
	rdmsrl_safe(msr, &value);
	smsr->values[slot].host = value;
	smsr->values[slot].curr = value;
}

void kvm_define_shared_msr(unsigned slot, u32 msr)
{
	if (slot >= shared_msrs_global.nr)
		shared_msrs_global.nr = slot + 1;
	shared_msrs_global.msrs[slot] = msr;
	/* we need ensured the shared_msr_global have been updated */
	smp_wmb();
}
EXPORT_SYMBOL_GPL(kvm_define_shared_msr);

static void kvm_shared_msr_cpu_online(void)
{
	unsigned i;

	for (i = 0; i < shared_msrs_global.nr; ++i)
		shared_msr_update(i, shared_msrs_global.msrs[i]);
}

void kvm_set_shared_msr(unsigned slot, u64 value, u64 mask)
{
	struct kvm_shared_msrs *smsr = &__get_cpu_var(shared_msrs);

	if (((value ^ smsr->values[slot].curr) & mask) == 0)
		return;
	smsr->values[slot].curr = value;
	wrmsrl(shared_msrs_global.msrs[slot], value);
	if (!smsr->registered) {
		smsr->urn.on_user_return = kvm_on_user_return;
		user_return_notifier_register(&smsr->urn);
		smsr->registered = true;
	}
}
EXPORT_SYMBOL_GPL(kvm_set_shared_msr);

static void drop_user_return_notifiers(void *ignore)
{
	struct kvm_shared_msrs *smsr = &__get_cpu_var(shared_msrs);

	if (smsr->registered)
		kvm_on_user_return(&smsr->urn);
}

u64 kvm_get_apic_base(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.apic_base;
}
EXPORT_SYMBOL_GPL(kvm_get_apic_base);

void kvm_set_apic_base(struct kvm_vcpu *vcpu, u64 data)
{
	/* TODO: reserve bits check */
	kvm_lapic_set_base(vcpu, data);
}
EXPORT_SYMBOL_GPL(kvm_set_apic_base);

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

static void kvm_multiple_exception(struct kvm_vcpu *vcpu,
		unsigned nr, bool has_error, u32 error_code,
		bool reinject)
{
	u32 prev_nr;
	int class1, class2;

	kvm_make_request(KVM_REQ_EVENT, vcpu);

	if (!vcpu->arch.exception.pending) {
	queue:
		vcpu->arch.exception.pending = true;
		vcpu->arch.exception.has_error_code = has_error;
		vcpu->arch.exception.nr = nr;
		vcpu->arch.exception.error_code = error_code;
		vcpu->arch.exception.reinject = reinject;
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
		/* generate double fault per SDM Table 5-5 */
		vcpu->arch.exception.pending = true;
		vcpu->arch.exception.has_error_code = true;
		vcpu->arch.exception.nr = DF_VECTOR;
		vcpu->arch.exception.error_code = 0;
	} else
		/* replace previous exception with a new one in a hope
		   that instruction re-execution will regenerate lost
		   exception */
		goto queue;
}

void kvm_queue_exception(struct kvm_vcpu *vcpu, unsigned nr)
{
	kvm_multiple_exception(vcpu, nr, false, 0, false);
}
EXPORT_SYMBOL_GPL(kvm_queue_exception);

void kvm_requeue_exception(struct kvm_vcpu *vcpu, unsigned nr)
{
	kvm_multiple_exception(vcpu, nr, false, 0, true);
}
EXPORT_SYMBOL_GPL(kvm_requeue_exception);

void kvm_complete_insn_gp(struct kvm_vcpu *vcpu, int err)
{
	if (err)
		kvm_inject_gp(vcpu, 0);
	else
		kvm_x86_ops->skip_emulated_instruction(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_complete_insn_gp);

void kvm_inject_page_fault(struct kvm_vcpu *vcpu, struct x86_exception *fault)
{
	++vcpu->stat.pf_guest;
	vcpu->arch.cr2 = fault->address;
	kvm_queue_exception_e(vcpu, PF_VECTOR, fault->error_code);
}
EXPORT_SYMBOL_GPL(kvm_inject_page_fault);

void kvm_propagate_fault(struct kvm_vcpu *vcpu, struct x86_exception *fault)
{
	if (mmu_is_nested(vcpu) && !fault->nested_page_fault)
		vcpu->arch.nested_mmu.inject_page_fault(vcpu, fault);
	else
		vcpu->arch.mmu.inject_page_fault(vcpu, fault);
}

void kvm_inject_nmi(struct kvm_vcpu *vcpu)
{
	atomic_inc(&vcpu->arch.nmi_queued);
	kvm_make_request(KVM_REQ_NMI, vcpu);
}
EXPORT_SYMBOL_GPL(kvm_inject_nmi);

void kvm_queue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code)
{
	kvm_multiple_exception(vcpu, nr, true, error_code, false);
}
EXPORT_SYMBOL_GPL(kvm_queue_exception_e);

void kvm_requeue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code)
{
	kvm_multiple_exception(vcpu, nr, true, error_code, true);
}
EXPORT_SYMBOL_GPL(kvm_requeue_exception_e);

/*
 * Checks if cpl <= required_cpl; if true, return true.  Otherwise queue
 * a #GP and return false.
 */
bool kvm_require_cpl(struct kvm_vcpu *vcpu, int required_cpl)
{
	if (kvm_x86_ops->get_cpl(vcpu) <= required_cpl)
		return true;
	kvm_queue_exception_e(vcpu, GP_VECTOR, 0);
	return false;
}
EXPORT_SYMBOL_GPL(kvm_require_cpl);

/*
 * This function will be used to read from the physical memory of the currently
 * running guest. The difference to kvm_read_guest_page is that this function
 * can read from guest physical or from the guest's guest physical memory.
 */
int kvm_read_guest_page_mmu(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			    gfn_t ngfn, void *data, int offset, int len,
			    u32 access)
{
	gfn_t real_gfn;
	gpa_t ngpa;

	ngpa     = gfn_to_gpa(ngfn);
	real_gfn = mmu->translate_gpa(vcpu, ngpa, access);
	if (real_gfn == UNMAPPED_GVA)
		return -EFAULT;

	real_gfn = gpa_to_gfn(real_gfn);

	return kvm_read_guest_page(vcpu->kvm, real_gfn, data, offset, len);
}
EXPORT_SYMBOL_GPL(kvm_read_guest_page_mmu);

int kvm_read_nested_guest_page(struct kvm_vcpu *vcpu, gfn_t gfn,
			       void *data, int offset, int len, u32 access)
{
	return kvm_read_guest_page_mmu(vcpu, vcpu->arch.walk_mmu, gfn,
				       data, offset, len, access);
}

/*
 * Load the pae pdptrs.  Return true is they are all valid.
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
		if (is_present_gpte(pdpte[i]) &&
		    (pdpte[i] & vcpu->arch.mmu.rsvd_bits_mask[0][2])) {
			ret = 0;
			goto out;
		}
	}
	ret = 1;

	memcpy(mmu->pdptrs, pdpte, sizeof(mmu->pdptrs));
	__set_bit(VCPU_EXREG_PDPTR,
		  (unsigned long *)&vcpu->arch.regs_avail);
	__set_bit(VCPU_EXREG_PDPTR,
		  (unsigned long *)&vcpu->arch.regs_dirty);
out:

	return ret;
}
EXPORT_SYMBOL_GPL(load_pdptrs);

static bool pdptrs_changed(struct kvm_vcpu *vcpu)
{
	u64 pdpte[ARRAY_SIZE(vcpu->arch.walk_mmu->pdptrs)];
	bool changed = true;
	int offset;
	gfn_t gfn;
	int r;

	if (is_long_mode(vcpu) || !is_pae(vcpu))
		return false;

	if (!test_bit(VCPU_EXREG_PDPTR,
		      (unsigned long *)&vcpu->arch.regs_avail))
		return true;

	gfn = (kvm_read_cr3(vcpu) & ~31u) >> PAGE_SHIFT;
	offset = (kvm_read_cr3(vcpu) & ~31u) & (PAGE_SIZE - 1);
	r = kvm_read_nested_guest_page(vcpu, gfn, pdpte, offset, sizeof(pdpte),
				       PFERR_USER_MASK | PFERR_WRITE_MASK);
	if (r < 0)
		goto out;
	changed = memcmp(pdpte, vcpu->arch.walk_mmu->pdptrs, sizeof(pdpte)) != 0;
out:

	return changed;
}

int kvm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	unsigned long old_cr0 = kvm_read_cr0(vcpu);
	unsigned long update_bits = X86_CR0_PG | X86_CR0_WP |
				    X86_CR0_CD | X86_CR0_NW;

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

	if (!is_paging(vcpu) && (cr0 & X86_CR0_PG)) {
#ifdef CONFIG_X86_64
		if ((vcpu->arch.efer & EFER_LME)) {
			int cs_db, cs_l;

			if (!is_pae(vcpu))
				return 1;
			kvm_x86_ops->get_cs_db_l_bits(vcpu, &cs_db, &cs_l);
			if (cs_l)
				return 1;
		} else
#endif
		if (is_pae(vcpu) && !load_pdptrs(vcpu, vcpu->arch.walk_mmu,
						 kvm_read_cr3(vcpu)))
			return 1;
	}

	if (!(cr0 & X86_CR0_PG) && kvm_read_cr4_bits(vcpu, X86_CR4_PCIDE))
		return 1;

	kvm_x86_ops->set_cr0(vcpu, cr0);

	if ((cr0 ^ old_cr0) & X86_CR0_PG) {
		kvm_clear_async_pf_completion_queue(vcpu);
		kvm_async_pf_hash_reset(vcpu);
	}

	if ((cr0 ^ old_cr0) & update_bits)
		kvm_mmu_reset_context(vcpu);
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_cr0);

void kvm_lmsw(struct kvm_vcpu *vcpu, unsigned long msw)
{
	(void)kvm_set_cr0(vcpu, kvm_read_cr0_bits(vcpu, ~0x0eul) | (msw & 0x0f));
}
EXPORT_SYMBOL_GPL(kvm_lmsw);

int __kvm_set_xcr(struct kvm_vcpu *vcpu, u32 index, u64 xcr)
{
	u64 xcr0;

	/* Only support XCR_XFEATURE_ENABLED_MASK(xcr0) now  */
	if (index != XCR_XFEATURE_ENABLED_MASK)
		return 1;
	xcr0 = xcr;
	if (kvm_x86_ops->get_cpl(vcpu) != 0)
		return 1;
	if (!(xcr0 & XSTATE_FP))
		return 1;
	if ((xcr0 & XSTATE_YMM) && !(xcr0 & XSTATE_SSE))
		return 1;
	if (xcr0 & ~host_xcr0)
		return 1;
	vcpu->arch.xcr0 = xcr0;
	vcpu->guest_xcr0_loaded = 0;
	return 0;
}

int kvm_set_xcr(struct kvm_vcpu *vcpu, u32 index, u64 xcr)
{
	if (__kvm_set_xcr(vcpu, index, xcr)) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_xcr);

int kvm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	unsigned long old_cr4 = kvm_read_cr4(vcpu);
	unsigned long pdptr_bits = X86_CR4_PGE | X86_CR4_PSE |
				   X86_CR4_PAE | X86_CR4_SMEP;
	if (cr4 & CR4_RESERVED_BITS)
		return 1;

	if (!guest_cpuid_has_xsave(vcpu) && (cr4 & X86_CR4_OSXSAVE))
		return 1;

	if (!guest_cpuid_has_smep(vcpu) && (cr4 & X86_CR4_SMEP))
		return 1;

	if (!guest_cpuid_has_fsgsbase(vcpu) && (cr4 & X86_CR4_RDWRGSFS))
		return 1;

	if (is_long_mode(vcpu)) {
		if (!(cr4 & X86_CR4_PAE))
			return 1;
	} else if (is_paging(vcpu) && (cr4 & X86_CR4_PAE)
		   && ((cr4 ^ old_cr4) & pdptr_bits)
		   && !load_pdptrs(vcpu, vcpu->arch.walk_mmu,
				   kvm_read_cr3(vcpu)))
		return 1;

	if ((cr4 & X86_CR4_PCIDE) && !(old_cr4 & X86_CR4_PCIDE)) {
		if (!guest_cpuid_has_pcid(vcpu))
			return 1;

		/* PCID can not be enabled when cr3[11:0]!=000H or EFER.LMA=0 */
		if ((kvm_read_cr3(vcpu) & X86_CR3_PCID_MASK) || !is_long_mode(vcpu))
			return 1;
	}

	if (kvm_x86_ops->set_cr4(vcpu, cr4))
		return 1;

	if (((cr4 ^ old_cr4) & pdptr_bits) ||
	    (!(cr4 & X86_CR4_PCIDE) && (old_cr4 & X86_CR4_PCIDE)))
		kvm_mmu_reset_context(vcpu);

	if ((cr4 ^ old_cr4) & X86_CR4_OSXSAVE)
		kvm_update_cpuid(vcpu);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_cr4);

int kvm_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	if (cr3 == kvm_read_cr3(vcpu) && !pdptrs_changed(vcpu)) {
		kvm_mmu_sync_roots(vcpu);
		kvm_mmu_flush_tlb(vcpu);
		return 0;
	}

	if (is_long_mode(vcpu)) {
		if (kvm_read_cr4(vcpu) & X86_CR4_PCIDE) {
			if (cr3 & CR3_PCID_ENABLED_RESERVED_BITS)
				return 1;
		} else
			if (cr3 & CR3_L_MODE_RESERVED_BITS)
				return 1;
	} else {
		if (is_pae(vcpu)) {
			if (cr3 & CR3_PAE_RESERVED_BITS)
				return 1;
			if (is_paging(vcpu) &&
			    !load_pdptrs(vcpu, vcpu->arch.walk_mmu, cr3))
				return 1;
		}
		/*
		 * We don't check reserved bits in nonpae mode, because
		 * this isn't enforced, and VMware depends on this.
		 */
	}

	/*
	 * Does the new cr3 value map to physical memory? (Note, we
	 * catch an invalid cr3 even in real-mode, because it would
	 * cause trouble later on when we turn on paging anyway.)
	 *
	 * A real CPU would silently accept an invalid cr3 and would
	 * attempt to use it - with largely undefined (and often hard
	 * to debug) behavior on the guest side.
	 */
	if (unlikely(!gfn_to_memslot(vcpu->kvm, cr3 >> PAGE_SHIFT)))
		return 1;
	vcpu->arch.cr3 = cr3;
	__set_bit(VCPU_EXREG_CR3, (ulong *)&vcpu->arch.regs_avail);
	vcpu->arch.mmu.new_cr3(vcpu);
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_cr3);

int kvm_set_cr8(struct kvm_vcpu *vcpu, unsigned long cr8)
{
	if (cr8 & CR8_RESERVED_BITS)
		return 1;
	if (irqchip_in_kernel(vcpu->kvm))
		kvm_lapic_set_tpr(vcpu, cr8);
	else
		vcpu->arch.cr8 = cr8;
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_cr8);

unsigned long kvm_get_cr8(struct kvm_vcpu *vcpu)
{
	if (irqchip_in_kernel(vcpu->kvm))
		return kvm_lapic_get_cr8(vcpu);
	else
		return vcpu->arch.cr8;
}
EXPORT_SYMBOL_GPL(kvm_get_cr8);

static void kvm_update_dr7(struct kvm_vcpu *vcpu)
{
	unsigned long dr7;

	if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)
		dr7 = vcpu->arch.guest_debug_dr7;
	else
		dr7 = vcpu->arch.dr7;
	kvm_x86_ops->set_dr7(vcpu, dr7);
	vcpu->arch.switch_db_regs = (dr7 & DR7_BP_EN_MASK);
}

static int __kvm_set_dr(struct kvm_vcpu *vcpu, int dr, unsigned long val)
{
	switch (dr) {
	case 0 ... 3:
		vcpu->arch.db[dr] = val;
		if (!(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP))
			vcpu->arch.eff_db[dr] = val;
		break;
	case 4:
		if (kvm_read_cr4_bits(vcpu, X86_CR4_DE))
			return 1; /* #UD */
		/* fall through */
	case 6:
		if (val & 0xffffffff00000000ULL)
			return -1; /* #GP */
		vcpu->arch.dr6 = (val & DR6_VOLATILE) | DR6_FIXED_1;
		break;
	case 5:
		if (kvm_read_cr4_bits(vcpu, X86_CR4_DE))
			return 1; /* #UD */
		/* fall through */
	default: /* 7 */
		if (val & 0xffffffff00000000ULL)
			return -1; /* #GP */
		vcpu->arch.dr7 = (val & DR7_VOLATILE) | DR7_FIXED_1;
		kvm_update_dr7(vcpu);
		break;
	}

	return 0;
}

int kvm_set_dr(struct kvm_vcpu *vcpu, int dr, unsigned long val)
{
	int res;

	res = __kvm_set_dr(vcpu, dr, val);
	if (res > 0)
		kvm_queue_exception(vcpu, UD_VECTOR);
	else if (res < 0)
		kvm_inject_gp(vcpu, 0);

	return res;
}
EXPORT_SYMBOL_GPL(kvm_set_dr);

static int _kvm_get_dr(struct kvm_vcpu *vcpu, int dr, unsigned long *val)
{
	switch (dr) {
	case 0 ... 3:
		*val = vcpu->arch.db[dr];
		break;
	case 4:
		if (kvm_read_cr4_bits(vcpu, X86_CR4_DE))
			return 1;
		/* fall through */
	case 6:
		*val = vcpu->arch.dr6;
		break;
	case 5:
		if (kvm_read_cr4_bits(vcpu, X86_CR4_DE))
			return 1;
		/* fall through */
	default: /* 7 */
		*val = vcpu->arch.dr7;
		break;
	}

	return 0;
}

int kvm_get_dr(struct kvm_vcpu *vcpu, int dr, unsigned long *val)
{
	if (_kvm_get_dr(vcpu, dr, val)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_get_dr);

bool kvm_rdpmc(struct kvm_vcpu *vcpu)
{
	u32 ecx = kvm_register_read(vcpu, VCPU_REGS_RCX);
	u64 data;
	int err;

	err = kvm_pmu_read_pmc(vcpu, ecx, &data);
	if (err)
		return err;
	kvm_register_write(vcpu, VCPU_REGS_RAX, (u32)data);
	kvm_register_write(vcpu, VCPU_REGS_RDX, data >> 32);
	return err;
}
EXPORT_SYMBOL_GPL(kvm_rdpmc);

/*
 * List of msr numbers which we expose to userspace through KVM_GET_MSRS
 * and KVM_SET_MSRS, and KVM_GET_MSR_INDEX_LIST.
 *
 * This list is modified at module load time to reflect the
 * capabilities of the host cpu. This capabilities test skips MSRs that are
 * kvm-specific. Those are put in the beginning of the list.
 */

#define KVM_SAVE_MSRS_BEGIN	10
static u32 msrs_to_save[] = {
	MSR_KVM_SYSTEM_TIME, MSR_KVM_WALL_CLOCK,
	MSR_KVM_SYSTEM_TIME_NEW, MSR_KVM_WALL_CLOCK_NEW,
	HV_X64_MSR_GUEST_OS_ID, HV_X64_MSR_HYPERCALL,
	HV_X64_MSR_APIC_ASSIST_PAGE, MSR_KVM_ASYNC_PF_EN, MSR_KVM_STEAL_TIME,
	MSR_KVM_PV_EOI_EN,
	MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
	MSR_STAR,
#ifdef CONFIG_X86_64
	MSR_CSTAR, MSR_KERNEL_GS_BASE, MSR_SYSCALL_MASK, MSR_LSTAR,
#endif
	MSR_IA32_TSC, MSR_IA32_CR_PAT, MSR_VM_HSAVE_PA
};

static unsigned num_msrs_to_save;

static const u32 emulated_msrs[] = {
	MSR_IA32_TSCDEADLINE,
	MSR_IA32_MISC_ENABLE,
	MSR_IA32_MCG_STATUS,
	MSR_IA32_MCG_CTL,
};

static int set_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	u64 old_efer = vcpu->arch.efer;

	if (efer & efer_reserved_bits)
		return 1;

	if (is_paging(vcpu)
	    && (vcpu->arch.efer & EFER_LME) != (efer & EFER_LME))
		return 1;

	if (efer & EFER_FFXSR) {
		struct kvm_cpuid_entry2 *feat;

		feat = kvm_find_cpuid_entry(vcpu, 0x80000001, 0);
		if (!feat || !(feat->edx & bit(X86_FEATURE_FXSR_OPT)))
			return 1;
	}

	if (efer & EFER_SVME) {
		struct kvm_cpuid_entry2 *feat;

		feat = kvm_find_cpuid_entry(vcpu, 0x80000001, 0);
		if (!feat || !(feat->ecx & bit(X86_FEATURE_SVM)))
			return 1;
	}

	efer &= ~EFER_LMA;
	efer |= vcpu->arch.efer & EFER_LMA;

	kvm_x86_ops->set_efer(vcpu, efer);

	vcpu->arch.mmu.base_role.nxe = (efer & EFER_NX) && !tdp_enabled;

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


/*
 * Writes msr value into into the appropriate "register".
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
int kvm_set_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 data)
{
	return kvm_x86_ops->set_msr(vcpu, msr_index, data);
}

/*
 * Adapt set_msr() to msr_io()'s calling convention
 */
static int do_set_msr(struct kvm_vcpu *vcpu, unsigned index, u64 *data)
{
	return kvm_set_msr(vcpu, index, *data);
}

static void kvm_write_wall_clock(struct kvm *kvm, gpa_t wall_clock)
{
	int version;
	int r;
	struct pvclock_wall_clock wc;
	struct timespec boot;

	if (!wall_clock)
		return;

	r = kvm_read_guest(kvm, wall_clock, &version, sizeof(version));
	if (r)
		return;

	if (version & 1)
		++version;  /* first time write, random junk */

	++version;

	kvm_write_guest(kvm, wall_clock, &version, sizeof(version));

	/*
	 * The guest calculates current wall clock time by adding
	 * system time (updated by kvm_guest_time_update below) to the
	 * wall clock specified here.  guest system time equals host
	 * system time for us, thus we must fill in host boot time here.
	 */
	getboottime(&boot);

	if (kvm->arch.kvmclock_offset) {
		struct timespec ts = ns_to_timespec(kvm->arch.kvmclock_offset);
		boot = timespec_sub(boot, ts);
	}
	wc.sec = boot.tv_sec;
	wc.nsec = boot.tv_nsec;
	wc.version = version;

	kvm_write_guest(kvm, wall_clock, &wc, sizeof(wc));

	version++;
	kvm_write_guest(kvm, wall_clock, &version, sizeof(version));
}

static uint32_t div_frac(uint32_t dividend, uint32_t divisor)
{
	uint32_t quotient, remainder;

	/* Don't try to replace with do_div(), this one calculates
	 * "(dividend << 32) / divisor" */
	__asm__ ( "divl %4"
		  : "=a" (quotient), "=d" (remainder)
		  : "0" (0), "1" (dividend), "r" (divisor) );
	return quotient;
}

static void kvm_get_time_scale(uint32_t scaled_khz, uint32_t base_khz,
			       s8 *pshift, u32 *pmultiplier)
{
	uint64_t scaled64;
	int32_t  shift = 0;
	uint64_t tps64;
	uint32_t tps32;

	tps64 = base_khz * 1000LL;
	scaled64 = scaled_khz * 1000LL;
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

	pr_debug("%s: base_khz %u => %u, shift %d, mul %u\n",
		 __func__, base_khz, scaled_khz, shift, *pmultiplier);
}

static inline u64 get_kernel_ns(void)
{
	struct timespec ts;

	WARN_ON(preemptible());
	ktime_get_ts(&ts);
	monotonic_to_bootbased(&ts);
	return timespec_to_ns(&ts);
}

static DEFINE_PER_CPU(unsigned long, cpu_tsc_khz);
unsigned long max_tsc_khz;

static inline u64 nsec_to_cycles(struct kvm_vcpu *vcpu, u64 nsec)
{
	return pvclock_scale_delta(nsec, vcpu->arch.virtual_tsc_mult,
				   vcpu->arch.virtual_tsc_shift);
}

static u32 adjust_tsc_khz(u32 khz, s32 ppm)
{
	u64 v = (u64)khz * (1000000 + ppm);
	do_div(v, 1000000);
	return v;
}

static void kvm_set_tsc_khz(struct kvm_vcpu *vcpu, u32 this_tsc_khz)
{
	u32 thresh_lo, thresh_hi;
	int use_scaling = 0;

	/* Compute a scale to convert nanoseconds in TSC cycles */
	kvm_get_time_scale(this_tsc_khz, NSEC_PER_SEC / 1000,
			   &vcpu->arch.virtual_tsc_shift,
			   &vcpu->arch.virtual_tsc_mult);
	vcpu->arch.virtual_tsc_khz = this_tsc_khz;

	/*
	 * Compute the variation in TSC rate which is acceptable
	 * within the range of tolerance and decide if the
	 * rate being applied is within that bounds of the hardware
	 * rate.  If so, no scaling or compensation need be done.
	 */
	thresh_lo = adjust_tsc_khz(tsc_khz, -tsc_tolerance_ppm);
	thresh_hi = adjust_tsc_khz(tsc_khz, tsc_tolerance_ppm);
	if (this_tsc_khz < thresh_lo || this_tsc_khz > thresh_hi) {
		pr_debug("kvm: requested TSC rate %u falls outside tolerance [%u,%u]\n", this_tsc_khz, thresh_lo, thresh_hi);
		use_scaling = 1;
	}
	kvm_x86_ops->set_tsc_khz(vcpu, this_tsc_khz, use_scaling);
}

static u64 compute_guest_tsc(struct kvm_vcpu *vcpu, s64 kernel_ns)
{
	u64 tsc = pvclock_scale_delta(kernel_ns-vcpu->arch.this_tsc_nsec,
				      vcpu->arch.virtual_tsc_mult,
				      vcpu->arch.virtual_tsc_shift);
	tsc += vcpu->arch.this_tsc_write;
	return tsc;
}

void kvm_write_tsc(struct kvm_vcpu *vcpu, u64 data)
{
	struct kvm *kvm = vcpu->kvm;
	u64 offset, ns, elapsed;
	unsigned long flags;
	s64 usdiff;

	raw_spin_lock_irqsave(&kvm->arch.tsc_write_lock, flags);
	offset = kvm_x86_ops->compute_tsc_offset(vcpu, data);
	ns = get_kernel_ns();
	elapsed = ns - kvm->arch.last_tsc_nsec;

	/* n.b - signed multiplication and division required */
	usdiff = data - kvm->arch.last_tsc_write;
#ifdef CONFIG_X86_64
	usdiff = (usdiff * 1000) / vcpu->arch.virtual_tsc_khz;
#else
	/* do_div() only does unsigned */
	asm("idivl %2; xor %%edx, %%edx"
	    : "=A"(usdiff)
	    : "A"(usdiff * 1000), "rm"(vcpu->arch.virtual_tsc_khz));
#endif
	do_div(elapsed, 1000);
	usdiff -= elapsed;
	if (usdiff < 0)
		usdiff = -usdiff;

	/*
	 * Special case: TSC write with a small delta (1 second) of virtual
	 * cycle time against real time is interpreted as an attempt to
	 * synchronize the CPU.
         *
	 * For a reliable TSC, we can match TSC offsets, and for an unstable
	 * TSC, we add elapsed time in this computation.  We could let the
	 * compensation code attempt to catch up if we fall behind, but
	 * it's better to try to match offsets from the beginning.
         */
	if (usdiff < USEC_PER_SEC &&
	    vcpu->arch.virtual_tsc_khz == kvm->arch.last_tsc_khz) {
		if (!check_tsc_unstable()) {
			offset = kvm->arch.cur_tsc_offset;
			pr_debug("kvm: matched tsc offset for %llu\n", data);
		} else {
			u64 delta = nsec_to_cycles(vcpu, elapsed);
			data += delta;
			offset = kvm_x86_ops->compute_tsc_offset(vcpu, data);
			pr_debug("kvm: adjusted tsc offset by %llu\n", delta);
		}
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
		pr_debug("kvm: new tsc generation %u, clock %llu\n",
			 kvm->arch.cur_tsc_generation, data);
	}

	/*
	 * We also track th most recent recorded KHZ, write and time to
	 * allow the matching interval to be extended at each write.
	 */
	kvm->arch.last_tsc_nsec = ns;
	kvm->arch.last_tsc_write = data;
	kvm->arch.last_tsc_khz = vcpu->arch.virtual_tsc_khz;

	/* Reset of TSC must disable overshoot protection below */
	vcpu->arch.hv_clock.tsc_timestamp = 0;
	vcpu->arch.last_guest_tsc = data;

	/* Keep track of which generation this VCPU has synchronized to */
	vcpu->arch.this_tsc_generation = kvm->arch.cur_tsc_generation;
	vcpu->arch.this_tsc_nsec = kvm->arch.cur_tsc_nsec;
	vcpu->arch.this_tsc_write = kvm->arch.cur_tsc_write;

	kvm_x86_ops->write_tsc_offset(vcpu, offset);
	raw_spin_unlock_irqrestore(&kvm->arch.tsc_write_lock, flags);
}

EXPORT_SYMBOL_GPL(kvm_write_tsc);

static int kvm_guest_time_update(struct kvm_vcpu *v)
{
	unsigned long flags;
	struct kvm_vcpu_arch *vcpu = &v->arch;
	void *shared_kaddr;
	unsigned long this_tsc_khz;
	s64 kernel_ns, max_kernel_ns;
	u64 tsc_timestamp;
	u8 pvclock_flags;

	/* Keep irq disabled to prevent changes to the clock */
	local_irq_save(flags);
	tsc_timestamp = kvm_x86_ops->read_l1_tsc(v);
	kernel_ns = get_kernel_ns();
	this_tsc_khz = __get_cpu_var(cpu_tsc_khz);
	if (unlikely(this_tsc_khz == 0)) {
		local_irq_restore(flags);
		kvm_make_request(KVM_REQ_CLOCK_UPDATE, v);
		return 1;
	}

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

	if (!vcpu->time_page)
		return 0;

	/*
	 * Time as measured by the TSC may go backwards when resetting the base
	 * tsc_timestamp.  The reason for this is that the TSC resolution is
	 * higher than the resolution of the other clock scales.  Thus, many
	 * possible measurments of the TSC correspond to one measurement of any
	 * other clock, and so a spread of values is possible.  This is not a
	 * problem for the computation of the nanosecond clock; with TSC rates
	 * around 1GHZ, there can only be a few cycles which correspond to one
	 * nanosecond value, and any path through this code will inevitably
	 * take longer than that.  However, with the kernel_ns value itself,
	 * the precision may be much lower, down to HZ granularity.  If the
	 * first sampling of TSC against kernel_ns ends in the low part of the
	 * range, and the second in the high end of the range, we can get:
	 *
	 * (TSC - offset_low) * S + kns_old > (TSC - offset_high) * S + kns_new
	 *
	 * As the sampling errors potentially range in the thousands of cycles,
	 * it is possible such a time value has already been observed by the
	 * guest.  To protect against this, we must compute the system time as
	 * observed by the guest and ensure the new system time is greater.
	 */
	max_kernel_ns = 0;
	if (vcpu->hv_clock.tsc_timestamp) {
		max_kernel_ns = vcpu->last_guest_tsc -
				vcpu->hv_clock.tsc_timestamp;
		max_kernel_ns = pvclock_scale_delta(max_kernel_ns,
				    vcpu->hv_clock.tsc_to_system_mul,
				    vcpu->hv_clock.tsc_shift);
		max_kernel_ns += vcpu->last_kernel_ns;
	}

	if (unlikely(vcpu->hw_tsc_khz != this_tsc_khz)) {
		kvm_get_time_scale(NSEC_PER_SEC / 1000, this_tsc_khz,
				   &vcpu->hv_clock.tsc_shift,
				   &vcpu->hv_clock.tsc_to_system_mul);
		vcpu->hw_tsc_khz = this_tsc_khz;
	}

	if (max_kernel_ns > kernel_ns)
		kernel_ns = max_kernel_ns;

	/* With all the info we got, fill in the values */
	vcpu->hv_clock.tsc_timestamp = tsc_timestamp;
	vcpu->hv_clock.system_time = kernel_ns + v->kvm->arch.kvmclock_offset;
	vcpu->last_kernel_ns = kernel_ns;
	vcpu->last_guest_tsc = tsc_timestamp;

	pvclock_flags = 0;
	if (vcpu->pvclock_set_guest_stopped_request) {
		pvclock_flags |= PVCLOCK_GUEST_STOPPED;
		vcpu->pvclock_set_guest_stopped_request = false;
	}

	vcpu->hv_clock.flags = pvclock_flags;

	/*
	 * The interface expects us to write an even number signaling that the
	 * update is finished. Since the guest won't see the intermediate
	 * state, we just increase by 2 at the end.
	 */
	vcpu->hv_clock.version += 2;

	shared_kaddr = kmap_atomic(vcpu->time_page);

	memcpy(shared_kaddr + vcpu->time_offset, &vcpu->hv_clock,
	       sizeof(vcpu->hv_clock));

	kunmap_atomic(shared_kaddr);

	mark_page_dirty(v->kvm, vcpu->time >> PAGE_SHIFT);
	return 0;
}

static bool msr_mtrr_valid(unsigned msr)
{
	switch (msr) {
	case 0x200 ... 0x200 + 2 * KVM_NR_VAR_MTRR - 1:
	case MSR_MTRRfix64K_00000:
	case MSR_MTRRfix16K_80000:
	case MSR_MTRRfix16K_A0000:
	case MSR_MTRRfix4K_C0000:
	case MSR_MTRRfix4K_C8000:
	case MSR_MTRRfix4K_D0000:
	case MSR_MTRRfix4K_D8000:
	case MSR_MTRRfix4K_E0000:
	case MSR_MTRRfix4K_E8000:
	case MSR_MTRRfix4K_F0000:
	case MSR_MTRRfix4K_F8000:
	case MSR_MTRRdefType:
	case MSR_IA32_CR_PAT:
		return true;
	case 0x2f8:
		return true;
	}
	return false;
}

static bool valid_pat_type(unsigned t)
{
	return t < 8 && (1 << t) & 0xf3; /* 0, 1, 4, 5, 6, 7 */
}

static bool valid_mtrr_type(unsigned t)
{
	return t < 8 && (1 << t) & 0x73; /* 0, 1, 4, 5, 6 */
}

static bool mtrr_valid(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	int i;

	if (!msr_mtrr_valid(msr))
		return false;

	if (msr == MSR_IA32_CR_PAT) {
		for (i = 0; i < 8; i++)
			if (!valid_pat_type((data >> (i * 8)) & 0xff))
				return false;
		return true;
	} else if (msr == MSR_MTRRdefType) {
		if (data & ~0xcff)
			return false;
		return valid_mtrr_type(data & 0xff);
	} else if (msr >= MSR_MTRRfix64K_00000 && msr <= MSR_MTRRfix4K_F8000) {
		for (i = 0; i < 8 ; i++)
			if (!valid_mtrr_type((data >> (i * 8)) & 0xff))
				return false;
		return true;
	}

	/* variable MTRRs */
	return valid_mtrr_type(data & 0xff);
}

static int set_msr_mtrr(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	u64 *p = (u64 *)&vcpu->arch.mtrr_state.fixed_ranges;

	if (!mtrr_valid(vcpu, msr, data))
		return 1;

	if (msr == MSR_MTRRdefType) {
		vcpu->arch.mtrr_state.def_type = data;
		vcpu->arch.mtrr_state.enabled = (data & 0xc00) >> 10;
	} else if (msr == MSR_MTRRfix64K_00000)
		p[0] = data;
	else if (msr == MSR_MTRRfix16K_80000 || msr == MSR_MTRRfix16K_A0000)
		p[1 + msr - MSR_MTRRfix16K_80000] = data;
	else if (msr >= MSR_MTRRfix4K_C0000 && msr <= MSR_MTRRfix4K_F8000)
		p[3 + msr - MSR_MTRRfix4K_C0000] = data;
	else if (msr == MSR_IA32_CR_PAT)
		vcpu->arch.pat = data;
	else {	/* Variable MTRRs */
		int idx, is_mtrr_mask;
		u64 *pt;

		idx = (msr - 0x200) / 2;
		is_mtrr_mask = msr - 0x200 - 2 * idx;
		if (!is_mtrr_mask)
			pt =
			  (u64 *)&vcpu->arch.mtrr_state.var_ranges[idx].base_lo;
		else
			pt =
			  (u64 *)&vcpu->arch.mtrr_state.var_ranges[idx].mask_lo;
		*pt = data;
	}

	kvm_mmu_reset_context(vcpu);
	return 0;
}

static int set_msr_mce(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	u64 mcg_cap = vcpu->arch.mcg_cap;
	unsigned bank_num = mcg_cap & 0xff;

	switch (msr) {
	case MSR_IA32_MCG_STATUS:
		vcpu->arch.mcg_status = data;
		break;
	case MSR_IA32_MCG_CTL:
		if (!(mcg_cap & MCG_CTL_P))
			return 1;
		if (data != 0 && data != ~(u64)0)
			return -1;
		vcpu->arch.mcg_ctl = data;
		break;
	default:
		if (msr >= MSR_IA32_MC0_CTL &&
		    msr < MSR_IA32_MC0_CTL + 4 * bank_num) {
			u32 offset = msr - MSR_IA32_MC0_CTL;
			/* only 0 or all 1s can be written to IA32_MCi_CTL
			 * some Linux kernels though clear bit 10 in bank 4 to
			 * workaround a BIOS/GART TBL issue on AMD K8s, ignore
			 * this to avoid an uncatched #GP in the guest
			 */
			if ((offset & 0x3) == 0 &&
			    data != 0 && (data | (1 << 10)) != ~(u64)0)
				return -1;
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
	int r;

	r = -E2BIG;
	if (page_num >= blob_size)
		goto out;
	r = -ENOMEM;
	page = memdup_user(blob_addr + (page_num * PAGE_SIZE), PAGE_SIZE);
	if (IS_ERR(page)) {
		r = PTR_ERR(page);
		goto out;
	}
	if (kvm_write_guest(kvm, page_addr, page, PAGE_SIZE))
		goto out_free;
	r = 0;
out_free:
	kfree(page);
out:
	return r;
}

static bool kvm_hv_hypercall_enabled(struct kvm *kvm)
{
	return kvm->arch.hv_hypercall & HV_X64_MSR_HYPERCALL_ENABLE;
}

static bool kvm_hv_msr_partition_wide(u32 msr)
{
	bool r = false;
	switch (msr) {
	case HV_X64_MSR_GUEST_OS_ID:
	case HV_X64_MSR_HYPERCALL:
		r = true;
		break;
	}

	return r;
}

static int set_msr_hyperv_pw(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	struct kvm *kvm = vcpu->kvm;

	switch (msr) {
	case HV_X64_MSR_GUEST_OS_ID:
		kvm->arch.hv_guest_os_id = data;
		/* setting guest os id to zero disables hypercall page */
		if (!kvm->arch.hv_guest_os_id)
			kvm->arch.hv_hypercall &= ~HV_X64_MSR_HYPERCALL_ENABLE;
		break;
	case HV_X64_MSR_HYPERCALL: {
		u64 gfn;
		unsigned long addr;
		u8 instructions[4];

		/* if guest os id is not set hypercall should remain disabled */
		if (!kvm->arch.hv_guest_os_id)
			break;
		if (!(data & HV_X64_MSR_HYPERCALL_ENABLE)) {
			kvm->arch.hv_hypercall = data;
			break;
		}
		gfn = data >> HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_SHIFT;
		addr = gfn_to_hva(kvm, gfn);
		if (kvm_is_error_hva(addr))
			return 1;
		kvm_x86_ops->patch_hypercall(vcpu, instructions);
		((unsigned char *)instructions)[3] = 0xc3; /* ret */
		if (__copy_to_user((void __user *)addr, instructions, 4))
			return 1;
		kvm->arch.hv_hypercall = data;
		break;
	}
	default:
		vcpu_unimpl(vcpu, "HYPER-V unimplemented wrmsr: 0x%x "
			    "data 0x%llx\n", msr, data);
		return 1;
	}
	return 0;
}

static int set_msr_hyperv(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	switch (msr) {
	case HV_X64_MSR_APIC_ASSIST_PAGE: {
		unsigned long addr;

		if (!(data & HV_X64_MSR_APIC_ASSIST_PAGE_ENABLE)) {
			vcpu->arch.hv_vapic = data;
			break;
		}
		addr = gfn_to_hva(vcpu->kvm, data >>
				  HV_X64_MSR_APIC_ASSIST_PAGE_ADDRESS_SHIFT);
		if (kvm_is_error_hva(addr))
			return 1;
		if (__clear_user((void __user *)addr, PAGE_SIZE))
			return 1;
		vcpu->arch.hv_vapic = data;
		break;
	}
	case HV_X64_MSR_EOI:
		return kvm_hv_vapic_msr_write(vcpu, APIC_EOI, data);
	case HV_X64_MSR_ICR:
		return kvm_hv_vapic_msr_write(vcpu, APIC_ICR, data);
	case HV_X64_MSR_TPR:
		return kvm_hv_vapic_msr_write(vcpu, APIC_TASKPRI, data);
	default:
		vcpu_unimpl(vcpu, "HYPER-V unimplemented wrmsr: 0x%x "
			    "data 0x%llx\n", msr, data);
		return 1;
	}

	return 0;
}

static int kvm_pv_enable_async_pf(struct kvm_vcpu *vcpu, u64 data)
{
	gpa_t gpa = data & ~0x3f;

	/* Bits 2:5 are reserved, Should be zero */
	if (data & 0x3c)
		return 1;

	vcpu->arch.apf.msr_val = data;

	if (!(data & KVM_ASYNC_PF_ENABLED)) {
		kvm_clear_async_pf_completion_queue(vcpu);
		kvm_async_pf_hash_reset(vcpu);
		return 0;
	}

	if (kvm_gfn_to_hva_cache_init(vcpu->kvm, &vcpu->arch.apf.data, gpa))
		return 1;

	vcpu->arch.apf.send_user_only = !(data & KVM_ASYNC_PF_SEND_ALWAYS);
	kvm_async_pf_wakeup_all(vcpu);
	return 0;
}

static void kvmclock_reset(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.time_page) {
		kvm_release_page_dirty(vcpu->arch.time_page);
		vcpu->arch.time_page = NULL;
	}
}

static void accumulate_steal_time(struct kvm_vcpu *vcpu)
{
	u64 delta;

	if (!(vcpu->arch.st.msr_val & KVM_MSR_ENABLED))
		return;

	delta = current->sched_info.run_delay - vcpu->arch.st.last_steal;
	vcpu->arch.st.last_steal = current->sched_info.run_delay;
	vcpu->arch.st.accum_steal = delta;
}

static void record_steal_time(struct kvm_vcpu *vcpu)
{
	if (!(vcpu->arch.st.msr_val & KVM_MSR_ENABLED))
		return;

	if (unlikely(kvm_read_guest_cached(vcpu->kvm, &vcpu->arch.st.stime,
		&vcpu->arch.st.steal, sizeof(struct kvm_steal_time))))
		return;

	vcpu->arch.st.steal.steal += vcpu->arch.st.accum_steal;
	vcpu->arch.st.steal.version += 2;
	vcpu->arch.st.accum_steal = 0;

	kvm_write_guest_cached(vcpu->kvm, &vcpu->arch.st.stime,
		&vcpu->arch.st.steal, sizeof(struct kvm_steal_time));
}

int kvm_set_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	bool pr = false;

	switch (msr) {
	case MSR_EFER:
		return set_efer(vcpu, data);
	case MSR_K7_HWCR:
		data &= ~(u64)0x40;	/* ignore flush filter disable */
		data &= ~(u64)0x100;	/* ignore ignne emulation enable */
		data &= ~(u64)0x8;	/* ignore TLB cache disable */
		if (data != 0) {
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
	case MSR_AMD64_NB_CFG:
		break;
	case MSR_IA32_DEBUGCTLMSR:
		if (!data) {
			/* We support the non-activated case already */
			break;
		} else if (data & ~(DEBUGCTLMSR_LBR | DEBUGCTLMSR_BTF)) {
			/* Values other than LBR and BTF are vendor-specific,
			   thus reserved and should throw a #GP */
			return 1;
		}
		vcpu_unimpl(vcpu, "%s: MSR_IA32_DEBUGCTLMSR 0x%llx, nop\n",
			    __func__, data);
		break;
	case MSR_IA32_UCODE_REV:
	case MSR_IA32_UCODE_WRITE:
	case MSR_VM_HSAVE_PA:
	case MSR_AMD64_PATCH_LOADER:
		break;
	case 0x200 ... 0x2ff:
		return set_msr_mtrr(vcpu, msr, data);
	case MSR_IA32_APICBASE:
		kvm_set_apic_base(vcpu, data);
		break;
	case APIC_BASE_MSR ... APIC_BASE_MSR + 0x3ff:
		return kvm_x2apic_msr_write(vcpu, msr, data);
	case MSR_IA32_TSCDEADLINE:
		kvm_set_lapic_tscdeadline_msr(vcpu, data);
		break;
	case MSR_IA32_MISC_ENABLE:
		vcpu->arch.ia32_misc_enable_msr = data;
		break;
	case MSR_KVM_WALL_CLOCK_NEW:
	case MSR_KVM_WALL_CLOCK:
		vcpu->kvm->arch.wall_clock = data;
		kvm_write_wall_clock(vcpu->kvm, data);
		break;
	case MSR_KVM_SYSTEM_TIME_NEW:
	case MSR_KVM_SYSTEM_TIME: {
		kvmclock_reset(vcpu);

		vcpu->arch.time = data;
		kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);

		/* we verify if the enable bit is set... */
		if (!(data & 1))
			break;

		/* ...but clean it before doing the actual write */
		vcpu->arch.time_offset = data & ~(PAGE_MASK | 1);

		vcpu->arch.time_page =
				gfn_to_page(vcpu->kvm, data >> PAGE_SHIFT);

		if (is_error_page(vcpu->arch.time_page))
			vcpu->arch.time_page = NULL;

		break;
	}
	case MSR_KVM_ASYNC_PF_EN:
		if (kvm_pv_enable_async_pf(vcpu, data))
			return 1;
		break;
	case MSR_KVM_STEAL_TIME:

		if (unlikely(!sched_info_on()))
			return 1;

		if (data & KVM_STEAL_RESERVED_MASK)
			return 1;

		if (kvm_gfn_to_hva_cache_init(vcpu->kvm, &vcpu->arch.st.stime,
							data & KVM_STEAL_VALID_BITS))
			return 1;

		vcpu->arch.st.msr_val = data;

		if (!(data & KVM_MSR_ENABLED))
			break;

		vcpu->arch.st.last_steal = current->sched_info.run_delay;

		preempt_disable();
		accumulate_steal_time(vcpu);
		preempt_enable();

		kvm_make_request(KVM_REQ_STEAL_UPDATE, vcpu);

		break;
	case MSR_KVM_PV_EOI_EN:
		if (kvm_lapic_enable_pv_eoi(vcpu, data))
			return 1;
		break;

	case MSR_IA32_MCG_CTL:
	case MSR_IA32_MCG_STATUS:
	case MSR_IA32_MC0_CTL ... MSR_IA32_MC0_CTL + 4 * KVM_MAX_MCE_BANKS - 1:
		return set_msr_mce(vcpu, msr, data);

	/* Performance counters are not protected by a CPUID bit,
	 * so we should check all of them in the generic path for the sake of
	 * cross vendor migration.
	 * Writing a zero into the event select MSRs disables them,
	 * which we perfectly emulate ;-). Any other value should be at least
	 * reported, some guests depend on them.
	 */
	case MSR_K7_EVNTSEL0:
	case MSR_K7_EVNTSEL1:
	case MSR_K7_EVNTSEL2:
	case MSR_K7_EVNTSEL3:
		if (data != 0)
			vcpu_unimpl(vcpu, "unimplemented perfctr wrmsr: "
				    "0x%x data 0x%llx\n", msr, data);
		break;
	/* at least RHEL 4 unconditionally writes to the perfctr registers,
	 * so we ignore writes to make it happy.
	 */
	case MSR_K7_PERFCTR0:
	case MSR_K7_PERFCTR1:
	case MSR_K7_PERFCTR2:
	case MSR_K7_PERFCTR3:
		vcpu_unimpl(vcpu, "unimplemented perfctr wrmsr: "
			    "0x%x data 0x%llx\n", msr, data);
		break;
	case MSR_P6_PERFCTR0:
	case MSR_P6_PERFCTR1:
		pr = true;
	case MSR_P6_EVNTSEL0:
	case MSR_P6_EVNTSEL1:
		if (kvm_pmu_msr(vcpu, msr))
			return kvm_pmu_set_msr(vcpu, msr, data);

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
		if (kvm_hv_msr_partition_wide(msr)) {
			int r;
			mutex_lock(&vcpu->kvm->lock);
			r = set_msr_hyperv_pw(vcpu, msr, data);
			mutex_unlock(&vcpu->kvm->lock);
			return r;
		} else
			return set_msr_hyperv(vcpu, msr, data);
		break;
	case MSR_IA32_BBL_CR_CTL3:
		/* Drop writes to this legacy MSR -- see rdmsr
		 * counterpart for further detail.
		 */
		vcpu_unimpl(vcpu, "ignored wrmsr: 0x%x data %llx\n", msr, data);
		break;
	case MSR_AMD64_OSVW_ID_LENGTH:
		if (!guest_cpuid_has_osvw(vcpu))
			return 1;
		vcpu->arch.osvw.length = data;
		break;
	case MSR_AMD64_OSVW_STATUS:
		if (!guest_cpuid_has_osvw(vcpu))
			return 1;
		vcpu->arch.osvw.status = data;
		break;
	default:
		if (msr && (msr == vcpu->kvm->arch.xen_hvm_config.msr))
			return xen_hvm_config(vcpu, data);
		if (kvm_pmu_msr(vcpu, msr))
			return kvm_pmu_set_msr(vcpu, msr, data);
		if (!ignore_msrs) {
			vcpu_unimpl(vcpu, "unhandled wrmsr: 0x%x data %llx\n",
				    msr, data);
			return 1;
		} else {
			vcpu_unimpl(vcpu, "ignored wrmsr: 0x%x data %llx\n",
				    msr, data);
			break;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_msr_common);


/*
 * Reads an msr value (of 'msr_index') into 'pdata'.
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
int kvm_get_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 *pdata)
{
	return kvm_x86_ops->get_msr(vcpu, msr_index, pdata);
}

static int get_msr_mtrr(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	u64 *p = (u64 *)&vcpu->arch.mtrr_state.fixed_ranges;

	if (!msr_mtrr_valid(msr))
		return 1;

	if (msr == MSR_MTRRdefType)
		*pdata = vcpu->arch.mtrr_state.def_type +
			 (vcpu->arch.mtrr_state.enabled << 10);
	else if (msr == MSR_MTRRfix64K_00000)
		*pdata = p[0];
	else if (msr == MSR_MTRRfix16K_80000 || msr == MSR_MTRRfix16K_A0000)
		*pdata = p[1 + msr - MSR_MTRRfix16K_80000];
	else if (msr >= MSR_MTRRfix4K_C0000 && msr <= MSR_MTRRfix4K_F8000)
		*pdata = p[3 + msr - MSR_MTRRfix4K_C0000];
	else if (msr == MSR_IA32_CR_PAT)
		*pdata = vcpu->arch.pat;
	else {	/* Variable MTRRs */
		int idx, is_mtrr_mask;
		u64 *pt;

		idx = (msr - 0x200) / 2;
		is_mtrr_mask = msr - 0x200 - 2 * idx;
		if (!is_mtrr_mask)
			pt =
			  (u64 *)&vcpu->arch.mtrr_state.var_ranges[idx].base_lo;
		else
			pt =
			  (u64 *)&vcpu->arch.mtrr_state.var_ranges[idx].mask_lo;
		*pdata = *pt;
	}

	return 0;
}

static int get_msr_mce(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
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
		if (!(mcg_cap & MCG_CTL_P))
			return 1;
		data = vcpu->arch.mcg_ctl;
		break;
	case MSR_IA32_MCG_STATUS:
		data = vcpu->arch.mcg_status;
		break;
	default:
		if (msr >= MSR_IA32_MC0_CTL &&
		    msr < MSR_IA32_MC0_CTL + 4 * bank_num) {
			u32 offset = msr - MSR_IA32_MC0_CTL;
			data = vcpu->arch.mce_banks[offset];
			break;
		}
		return 1;
	}
	*pdata = data;
	return 0;
}

static int get_msr_hyperv_pw(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	u64 data = 0;
	struct kvm *kvm = vcpu->kvm;

	switch (msr) {
	case HV_X64_MSR_GUEST_OS_ID:
		data = kvm->arch.hv_guest_os_id;
		break;
	case HV_X64_MSR_HYPERCALL:
		data = kvm->arch.hv_hypercall;
		break;
	default:
		vcpu_unimpl(vcpu, "Hyper-V unhandled rdmsr: 0x%x\n", msr);
		return 1;
	}

	*pdata = data;
	return 0;
}

static int get_msr_hyperv(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	u64 data = 0;

	switch (msr) {
	case HV_X64_MSR_VP_INDEX: {
		int r;
		struct kvm_vcpu *v;
		kvm_for_each_vcpu(r, v, vcpu->kvm)
			if (v == vcpu)
				data = r;
		break;
	}
	case HV_X64_MSR_EOI:
		return kvm_hv_vapic_msr_read(vcpu, APIC_EOI, pdata);
	case HV_X64_MSR_ICR:
		return kvm_hv_vapic_msr_read(vcpu, APIC_ICR, pdata);
	case HV_X64_MSR_TPR:
		return kvm_hv_vapic_msr_read(vcpu, APIC_TASKPRI, pdata);
	case HV_X64_MSR_APIC_ASSIST_PAGE:
		data = vcpu->arch.hv_vapic;
		break;
	default:
		vcpu_unimpl(vcpu, "Hyper-V unhandled rdmsr: 0x%x\n", msr);
		return 1;
	}
	*pdata = data;
	return 0;
}

int kvm_get_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	u64 data;

	switch (msr) {
	case MSR_IA32_PLATFORM_ID:
	case MSR_IA32_EBL_CR_POWERON:
	case MSR_IA32_DEBUGCTLMSR:
	case MSR_IA32_LASTBRANCHFROMIP:
	case MSR_IA32_LASTBRANCHTOIP:
	case MSR_IA32_LASTINTFROMIP:
	case MSR_IA32_LASTINTTOIP:
	case MSR_K8_SYSCFG:
	case MSR_K7_HWCR:
	case MSR_VM_HSAVE_PA:
	case MSR_K7_EVNTSEL0:
	case MSR_K7_PERFCTR0:
	case MSR_K8_INT_PENDING_MSG:
	case MSR_AMD64_NB_CFG:
	case MSR_FAM10H_MMIO_CONF_BASE:
		data = 0;
		break;
	case MSR_P6_PERFCTR0:
	case MSR_P6_PERFCTR1:
	case MSR_P6_EVNTSEL0:
	case MSR_P6_EVNTSEL1:
		if (kvm_pmu_msr(vcpu, msr))
			return kvm_pmu_get_msr(vcpu, msr, pdata);
		data = 0;
		break;
	case MSR_IA32_UCODE_REV:
		data = 0x100000000ULL;
		break;
	case MSR_MTRRcap:
		data = 0x500 | KVM_NR_VAR_MTRR;
		break;
	case 0x200 ... 0x2ff:
		return get_msr_mtrr(vcpu, msr, pdata);
	case 0xcd: /* fsb frequency */
		data = 3;
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
		data = 1 << 24;
		break;
	case MSR_IA32_APICBASE:
		data = kvm_get_apic_base(vcpu);
		break;
	case APIC_BASE_MSR ... APIC_BASE_MSR + 0x3ff:
		return kvm_x2apic_msr_read(vcpu, msr, pdata);
		break;
	case MSR_IA32_TSCDEADLINE:
		data = kvm_get_lapic_tscdeadline_msr(vcpu);
		break;
	case MSR_IA32_MISC_ENABLE:
		data = vcpu->arch.ia32_misc_enable_msr;
		break;
	case MSR_IA32_PERF_STATUS:
		/* TSC increment by tick */
		data = 1000ULL;
		/* CPU multiplier */
		data |= (((uint64_t)4ULL) << 40);
		break;
	case MSR_EFER:
		data = vcpu->arch.efer;
		break;
	case MSR_KVM_WALL_CLOCK:
	case MSR_KVM_WALL_CLOCK_NEW:
		data = vcpu->kvm->arch.wall_clock;
		break;
	case MSR_KVM_SYSTEM_TIME:
	case MSR_KVM_SYSTEM_TIME_NEW:
		data = vcpu->arch.time;
		break;
	case MSR_KVM_ASYNC_PF_EN:
		data = vcpu->arch.apf.msr_val;
		break;
	case MSR_KVM_STEAL_TIME:
		data = vcpu->arch.st.msr_val;
		break;
	case MSR_KVM_PV_EOI_EN:
		data = vcpu->arch.pv_eoi.msr_val;
		break;
	case MSR_IA32_P5_MC_ADDR:
	case MSR_IA32_P5_MC_TYPE:
	case MSR_IA32_MCG_CAP:
	case MSR_IA32_MCG_CTL:
	case MSR_IA32_MCG_STATUS:
	case MSR_IA32_MC0_CTL ... MSR_IA32_MC0_CTL + 4 * KVM_MAX_MCE_BANKS - 1:
		return get_msr_mce(vcpu, msr, pdata);
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
		data = 0x20000000;
		break;
	case HV_X64_MSR_GUEST_OS_ID ... HV_X64_MSR_SINT15:
		if (kvm_hv_msr_partition_wide(msr)) {
			int r;
			mutex_lock(&vcpu->kvm->lock);
			r = get_msr_hyperv_pw(vcpu, msr, pdata);
			mutex_unlock(&vcpu->kvm->lock);
			return r;
		} else
			return get_msr_hyperv(vcpu, msr, pdata);
		break;
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
		data = 0xbe702111;
		break;
	case MSR_AMD64_OSVW_ID_LENGTH:
		if (!guest_cpuid_has_osvw(vcpu))
			return 1;
		data = vcpu->arch.osvw.length;
		break;
	case MSR_AMD64_OSVW_STATUS:
		if (!guest_cpuid_has_osvw(vcpu))
			return 1;
		data = vcpu->arch.osvw.status;
		break;
	default:
		if (kvm_pmu_msr(vcpu, msr))
			return kvm_pmu_get_msr(vcpu, msr, pdata);
		if (!ignore_msrs) {
			vcpu_unimpl(vcpu, "unhandled rdmsr: 0x%x\n", msr);
			return 1;
		} else {
			vcpu_unimpl(vcpu, "ignored rdmsr: 0x%x\n", msr);
			data = 0;
		}
		break;
	}
	*pdata = data;
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
	int i, idx;

	idx = srcu_read_lock(&vcpu->kvm->srcu);
	for (i = 0; i < msrs->nmsrs; ++i)
		if (do_msr(vcpu, entries[i].index, &entries[i].data))
			break;
	srcu_read_unlock(&vcpu->kvm->srcu, idx);

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
	if (copy_from_user(&msrs, user_msrs, sizeof msrs))
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

int kvm_dev_ioctl_check_extension(long ext)
{
	int r;

	switch (ext) {
	case KVM_CAP_IRQCHIP:
	case KVM_CAP_HLT:
	case KVM_CAP_MMU_SHADOW_CACHE_CONTROL:
	case KVM_CAP_SET_TSS_ADDR:
	case KVM_CAP_EXT_CPUID:
	case KVM_CAP_CLOCKSOURCE:
	case KVM_CAP_PIT:
	case KVM_CAP_NOP_IO_DELAY:
	case KVM_CAP_MP_STATE:
	case KVM_CAP_SYNC_MMU:
	case KVM_CAP_USER_NMI:
	case KVM_CAP_REINJECT_CONTROL:
	case KVM_CAP_IRQ_INJECT_STATUS:
	case KVM_CAP_ASSIGN_DEV_IRQ:
	case KVM_CAP_IRQFD:
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_PIT2:
	case KVM_CAP_PIT_STATE2:
	case KVM_CAP_SET_IDENTITY_MAP_ADDR:
	case KVM_CAP_XEN_HVM:
	case KVM_CAP_ADJUST_CLOCK:
	case KVM_CAP_VCPU_EVENTS:
	case KVM_CAP_HYPERV:
	case KVM_CAP_HYPERV_VAPIC:
	case KVM_CAP_HYPERV_SPIN:
	case KVM_CAP_PCI_SEGMENT:
	case KVM_CAP_DEBUGREGS:
	case KVM_CAP_X86_ROBUST_SINGLESTEP:
	case KVM_CAP_XSAVE:
	case KVM_CAP_ASYNC_PF:
	case KVM_CAP_GET_TSC_KHZ:
	case KVM_CAP_PCI_2_3:
	case KVM_CAP_KVMCLOCK_CTRL:
	case KVM_CAP_READONLY_MEM:
	case KVM_CAP_IRQFD_RESAMPLE:
		r = 1;
		break;
	case KVM_CAP_COALESCED_MMIO:
		r = KVM_COALESCED_MMIO_PAGE_OFFSET;
		break;
	case KVM_CAP_VAPIC:
		r = !kvm_x86_ops->cpu_has_accelerated_tpr();
		break;
	case KVM_CAP_NR_VCPUS:
		r = KVM_SOFT_MAX_VCPUS;
		break;
	case KVM_CAP_MAX_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
	case KVM_CAP_NR_MEMSLOTS:
		r = KVM_MEMORY_SLOTS;
		break;
	case KVM_CAP_PV_MMU:	/* obsolete */
		r = 0;
		break;
	case KVM_CAP_IOMMU:
		r = iommu_present(&pci_bus_type);
		break;
	case KVM_CAP_MCE:
		r = KVM_MAX_MCE_BANKS;
		break;
	case KVM_CAP_XCRS:
		r = cpu_has_xsave;
		break;
	case KVM_CAP_TSC_CONTROL:
		r = kvm_has_tsc_control;
		break;
	case KVM_CAP_TSC_DEADLINE_TIMER:
		r = boot_cpu_has(X86_FEATURE_TSC_DEADLINE_TIMER);
		break;
	default:
		r = 0;
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
		if (copy_from_user(&msr_list, user_msr_list, sizeof msr_list))
			goto out;
		n = msr_list.nmsrs;
		msr_list.nmsrs = num_msrs_to_save + ARRAY_SIZE(emulated_msrs);
		if (copy_to_user(user_msr_list, &msr_list, sizeof msr_list))
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
				 ARRAY_SIZE(emulated_msrs) * sizeof(u32)))
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_SUPPORTED_CPUID: {
		struct kvm_cpuid2 __user *cpuid_arg = argp;
		struct kvm_cpuid2 cpuid;

		r = -EFAULT;
		if (copy_from_user(&cpuid, cpuid_arg, sizeof cpuid))
			goto out;
		r = kvm_dev_ioctl_get_supported_cpuid(&cpuid,
						      cpuid_arg->entries);
		if (r)
			goto out;

		r = -EFAULT;
		if (copy_to_user(cpuid_arg, &cpuid, sizeof cpuid))
			goto out;
		r = 0;
		break;
	}
	case KVM_X86_GET_MCE_CAP_SUPPORTED: {
		u64 mce_cap;

		mce_cap = KVM_MCE_CAP_SUPPORTED;
		r = -EFAULT;
		if (copy_to_user(argp, &mce_cap, sizeof mce_cap))
			goto out;
		r = 0;
		break;
	}
	default:
		r = -EINVAL;
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
	return vcpu->kvm->arch.iommu_domain &&
		!(vcpu->kvm->arch.iommu_flags & KVM_IOMMU_CACHE_COHERENCY);
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	/* Address WBINVD may be executed by guest */
	if (need_emulate_wbinvd(vcpu)) {
		if (kvm_x86_ops->has_wbinvd_exit())
			cpumask_set_cpu(cpu, vcpu->arch.wbinvd_dirty_mask);
		else if (vcpu->cpu != -1 && vcpu->cpu != cpu)
			smp_call_function_single(vcpu->cpu,
					wbinvd_ipi, NULL, 1);
	}

	kvm_x86_ops->vcpu_load(vcpu, cpu);

	/* Apply any externally detected TSC adjustments (due to suspend) */
	if (unlikely(vcpu->arch.tsc_offset_adjustment)) {
		adjust_tsc_offset_host(vcpu, vcpu->arch.tsc_offset_adjustment);
		vcpu->arch.tsc_offset_adjustment = 0;
		set_bit(KVM_REQ_CLOCK_UPDATE, &vcpu->requests);
	}

	if (unlikely(vcpu->cpu != cpu) || check_tsc_unstable()) {
		s64 tsc_delta = !vcpu->arch.last_host_tsc ? 0 :
				native_read_tsc() - vcpu->arch.last_host_tsc;
		if (tsc_delta < 0)
			mark_tsc_unstable("KVM discovered backwards TSC");
		if (check_tsc_unstable()) {
			u64 offset = kvm_x86_ops->compute_tsc_offset(vcpu,
						vcpu->arch.last_guest_tsc);
			kvm_x86_ops->write_tsc_offset(vcpu, offset);
			vcpu->arch.tsc_catchup = 1;
		}
		kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
		if (vcpu->cpu != cpu)
			kvm_migrate_timers(vcpu);
		vcpu->cpu = cpu;
	}

	accumulate_steal_time(vcpu);
	kvm_make_request(KVM_REQ_STEAL_UPDATE, vcpu);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	kvm_x86_ops->vcpu_put(vcpu);
	kvm_put_guest_fpu(vcpu);
	vcpu->arch.last_host_tsc = native_read_tsc();
}

static int kvm_vcpu_ioctl_get_lapic(struct kvm_vcpu *vcpu,
				    struct kvm_lapic_state *s)
{
	memcpy(s->regs, vcpu->arch.apic->regs, sizeof *s);

	return 0;
}

static int kvm_vcpu_ioctl_set_lapic(struct kvm_vcpu *vcpu,
				    struct kvm_lapic_state *s)
{
	kvm_apic_post_state_restore(vcpu, s);
	update_cr8_intercept(vcpu);

	return 0;
}

static int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu,
				    struct kvm_interrupt *irq)
{
	if (irq->irq < 0 || irq->irq >= KVM_NR_INTERRUPTS)
		return -EINVAL;
	if (irqchip_in_kernel(vcpu->kvm))
		return -ENXIO;

	kvm_queue_interrupt(vcpu, irq->irq, false);
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
	if (!bank_num || bank_num >= KVM_MAX_MCE_BANKS)
		goto out;
	if (mcg_cap & ~(KVM_MCE_CAP_SUPPORTED | 0xff | 0xff0000))
		goto out;
	r = 0;
	vcpu->arch.mcg_cap = mcg_cap;
	/* Init IA32_MCG_CTL to all 1s */
	if (mcg_cap & MCG_CTL_P)
		vcpu->arch.mcg_ctl = ~(u64)0;
	/* Init IA32_MCi_CTL to all 1s */
	for (bank = 0; bank < bank_num; bank++)
		vcpu->arch.mce_banks[bank*4] = ~(u64)0;
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
	events->exception.injected =
		vcpu->arch.exception.pending &&
		!kvm_exception_is_soft(vcpu->arch.exception.nr);
	events->exception.nr = vcpu->arch.exception.nr;
	events->exception.has_error_code = vcpu->arch.exception.has_error_code;
	events->exception.pad = 0;
	events->exception.error_code = vcpu->arch.exception.error_code;

	events->interrupt.injected =
		vcpu->arch.interrupt.pending && !vcpu->arch.interrupt.soft;
	events->interrupt.nr = vcpu->arch.interrupt.nr;
	events->interrupt.soft = 0;
	events->interrupt.shadow =
		kvm_x86_ops->get_interrupt_shadow(vcpu,
			KVM_X86_SHADOW_INT_MOV_SS | KVM_X86_SHADOW_INT_STI);

	events->nmi.injected = vcpu->arch.nmi_injected;
	events->nmi.pending = vcpu->arch.nmi_pending != 0;
	events->nmi.masked = kvm_x86_ops->get_nmi_mask(vcpu);
	events->nmi.pad = 0;

	events->sipi_vector = vcpu->arch.sipi_vector;

	events->flags = (KVM_VCPUEVENT_VALID_NMI_PENDING
			 | KVM_VCPUEVENT_VALID_SIPI_VECTOR
			 | KVM_VCPUEVENT_VALID_SHADOW);
	memset(&events->reserved, 0, sizeof(events->reserved));
}

static int kvm_vcpu_ioctl_x86_set_vcpu_events(struct kvm_vcpu *vcpu,
					      struct kvm_vcpu_events *events)
{
	if (events->flags & ~(KVM_VCPUEVENT_VALID_NMI_PENDING
			      | KVM_VCPUEVENT_VALID_SIPI_VECTOR
			      | KVM_VCPUEVENT_VALID_SHADOW))
		return -EINVAL;

	process_nmi(vcpu);
	vcpu->arch.exception.pending = events->exception.injected;
	vcpu->arch.exception.nr = events->exception.nr;
	vcpu->arch.exception.has_error_code = events->exception.has_error_code;
	vcpu->arch.exception.error_code = events->exception.error_code;

	vcpu->arch.interrupt.pending = events->interrupt.injected;
	vcpu->arch.interrupt.nr = events->interrupt.nr;
	vcpu->arch.interrupt.soft = events->interrupt.soft;
	if (events->flags & KVM_VCPUEVENT_VALID_SHADOW)
		kvm_x86_ops->set_interrupt_shadow(vcpu,
						  events->interrupt.shadow);

	vcpu->arch.nmi_injected = events->nmi.injected;
	if (events->flags & KVM_VCPUEVENT_VALID_NMI_PENDING)
		vcpu->arch.nmi_pending = events->nmi.pending;
	kvm_x86_ops->set_nmi_mask(vcpu, events->nmi.masked);

	if (events->flags & KVM_VCPUEVENT_VALID_SIPI_VECTOR)
		vcpu->arch.sipi_vector = events->sipi_vector;

	kvm_make_request(KVM_REQ_EVENT, vcpu);

	return 0;
}

static void kvm_vcpu_ioctl_x86_get_debugregs(struct kvm_vcpu *vcpu,
					     struct kvm_debugregs *dbgregs)
{
	memcpy(dbgregs->db, vcpu->arch.db, sizeof(vcpu->arch.db));
	dbgregs->dr6 = vcpu->arch.dr6;
	dbgregs->dr7 = vcpu->arch.dr7;
	dbgregs->flags = 0;
	memset(&dbgregs->reserved, 0, sizeof(dbgregs->reserved));
}

static int kvm_vcpu_ioctl_x86_set_debugregs(struct kvm_vcpu *vcpu,
					    struct kvm_debugregs *dbgregs)
{
	if (dbgregs->flags)
		return -EINVAL;

	memcpy(vcpu->arch.db, dbgregs->db, sizeof(vcpu->arch.db));
	vcpu->arch.dr6 = dbgregs->dr6;
	vcpu->arch.dr7 = dbgregs->dr7;

	return 0;
}

static void kvm_vcpu_ioctl_x86_get_xsave(struct kvm_vcpu *vcpu,
					 struct kvm_xsave *guest_xsave)
{
	if (cpu_has_xsave)
		memcpy(guest_xsave->region,
			&vcpu->arch.guest_fpu.state->xsave,
			xstate_size);
	else {
		memcpy(guest_xsave->region,
			&vcpu->arch.guest_fpu.state->fxsave,
			sizeof(struct i387_fxsave_struct));
		*(u64 *)&guest_xsave->region[XSAVE_HDR_OFFSET / sizeof(u32)] =
			XSTATE_FPSSE;
	}
}

static int kvm_vcpu_ioctl_x86_set_xsave(struct kvm_vcpu *vcpu,
					struct kvm_xsave *guest_xsave)
{
	u64 xstate_bv =
		*(u64 *)&guest_xsave->region[XSAVE_HDR_OFFSET / sizeof(u32)];

	if (cpu_has_xsave)
		memcpy(&vcpu->arch.guest_fpu.state->xsave,
			guest_xsave->region, xstate_size);
	else {
		if (xstate_bv & ~XSTATE_FPSSE)
			return -EINVAL;
		memcpy(&vcpu->arch.guest_fpu.state->fxsave,
			guest_xsave->region, sizeof(struct i387_fxsave_struct));
	}
	return 0;
}

static void kvm_vcpu_ioctl_x86_get_xcrs(struct kvm_vcpu *vcpu,
					struct kvm_xcrs *guest_xcrs)
{
	if (!cpu_has_xsave) {
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

	if (!cpu_has_xsave)
		return -EINVAL;

	if (guest_xcrs->nr_xcrs > KVM_MAX_XCRS || guest_xcrs->flags)
		return -EINVAL;

	for (i = 0; i < guest_xcrs->nr_xcrs; i++)
		/* Only support XCR0 currently */
		if (guest_xcrs->xcrs[0].xcr == XCR_XFEATURE_ENABLED_MASK) {
			r = __kvm_set_xcr(vcpu, XCR_XFEATURE_ENABLED_MASK,
				guest_xcrs->xcrs[0].value);
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
	if (!vcpu->arch.time_page)
		return -EINVAL;
	vcpu->arch.pvclock_set_guest_stopped_request = true;
	kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
	return 0;
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

	u.buffer = NULL;
	switch (ioctl) {
	case KVM_GET_LAPIC: {
		r = -EINVAL;
		if (!vcpu->arch.apic)
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
		if (!vcpu->arch.apic)
			goto out;
		u.lapic = memdup_user(argp, sizeof(*u.lapic));
		if (IS_ERR(u.lapic)) {
			r = PTR_ERR(u.lapic);
			goto out;
		}

		r = kvm_vcpu_ioctl_set_lapic(vcpu, u.lapic);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_INTERRUPT: {
		struct kvm_interrupt irq;

		r = -EFAULT;
		if (copy_from_user(&irq, argp, sizeof irq))
			goto out;
		r = kvm_vcpu_ioctl_interrupt(vcpu, &irq);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_NMI: {
		r = kvm_vcpu_ioctl_nmi(vcpu);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_CPUID: {
		struct kvm_cpuid __user *cpuid_arg = argp;
		struct kvm_cpuid cpuid;

		r = -EFAULT;
		if (copy_from_user(&cpuid, cpuid_arg, sizeof cpuid))
			goto out;
		r = kvm_vcpu_ioctl_set_cpuid(vcpu, &cpuid, cpuid_arg->entries);
		if (r)
			goto out;
		break;
	}
	case KVM_SET_CPUID2: {
		struct kvm_cpuid2 __user *cpuid_arg = argp;
		struct kvm_cpuid2 cpuid;

		r = -EFAULT;
		if (copy_from_user(&cpuid, cpuid_arg, sizeof cpuid))
			goto out;
		r = kvm_vcpu_ioctl_set_cpuid2(vcpu, &cpuid,
					      cpuid_arg->entries);
		if (r)
			goto out;
		break;
	}
	case KVM_GET_CPUID2: {
		struct kvm_cpuid2 __user *cpuid_arg = argp;
		struct kvm_cpuid2 cpuid;

		r = -EFAULT;
		if (copy_from_user(&cpuid, cpuid_arg, sizeof cpuid))
			goto out;
		r = kvm_vcpu_ioctl_get_cpuid2(vcpu, &cpuid,
					      cpuid_arg->entries);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(cpuid_arg, &cpuid, sizeof cpuid))
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_MSRS:
		r = msr_io(vcpu, argp, kvm_get_msr, 1);
		break;
	case KVM_SET_MSRS:
		r = msr_io(vcpu, argp, do_set_msr, 0);
		break;
	case KVM_TPR_ACCESS_REPORTING: {
		struct kvm_tpr_access_ctl tac;

		r = -EFAULT;
		if (copy_from_user(&tac, argp, sizeof tac))
			goto out;
		r = vcpu_ioctl_tpr_access_reporting(vcpu, &tac);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &tac, sizeof tac))
			goto out;
		r = 0;
		break;
	};
	case KVM_SET_VAPIC_ADDR: {
		struct kvm_vapic_addr va;

		r = -EINVAL;
		if (!irqchip_in_kernel(vcpu->kvm))
			goto out;
		r = -EFAULT;
		if (copy_from_user(&va, argp, sizeof va))
			goto out;
		r = 0;
		kvm_lapic_set_vapic_addr(vcpu, va.vapic_addr);
		break;
	}
	case KVM_X86_SETUP_MCE: {
		u64 mcg_cap;

		r = -EFAULT;
		if (copy_from_user(&mcg_cap, argp, sizeof mcg_cap))
			goto out;
		r = kvm_vcpu_ioctl_x86_setup_mce(vcpu, mcg_cap);
		break;
	}
	case KVM_X86_SET_MCE: {
		struct kvm_x86_mce mce;

		r = -EFAULT;
		if (copy_from_user(&mce, argp, sizeof mce))
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
		u.xsave = kzalloc(sizeof(struct kvm_xsave), GFP_KERNEL);
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
			goto out;
		}

		r = kvm_vcpu_ioctl_x86_set_xsave(vcpu, u.xsave);
		break;
	}
	case KVM_GET_XCRS: {
		u.xcrs = kzalloc(sizeof(struct kvm_xcrs), GFP_KERNEL);
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
			goto out;
		}

		r = kvm_vcpu_ioctl_x86_set_xcrs(vcpu, u.xcrs);
		break;
	}
	case KVM_SET_TSC_KHZ: {
		u32 user_tsc_khz;

		r = -EINVAL;
		user_tsc_khz = (u32)arg;

		if (user_tsc_khz >= kvm_max_guest_tsc_khz)
			goto out;

		if (user_tsc_khz == 0)
			user_tsc_khz = tsc_khz;

		kvm_set_tsc_khz(vcpu, user_tsc_khz);

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
	default:
		r = -EINVAL;
	}
out:
	kfree(u.buffer);
	return r;
}

int kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

static int kvm_vm_ioctl_set_tss_addr(struct kvm *kvm, unsigned long addr)
{
	int ret;

	if (addr > (unsigned int)(-3 * PAGE_SIZE))
		return -1;
	ret = kvm_x86_ops->set_tss_addr(kvm, addr);
	return ret;
}

static int kvm_vm_ioctl_set_identity_map_addr(struct kvm *kvm,
					      u64 ident_addr)
{
	kvm->arch.ept_identity_map_addr = ident_addr;
	return 0;
}

static int kvm_vm_ioctl_set_nr_mmu_pages(struct kvm *kvm,
					  u32 kvm_nr_mmu_pages)
{
	if (kvm_nr_mmu_pages < KVM_MIN_ALLOC_MMU_PAGES)
		return -EINVAL;

	mutex_lock(&kvm->slots_lock);
	spin_lock(&kvm->mmu_lock);

	kvm_mmu_change_mmu_pages(kvm, kvm_nr_mmu_pages);
	kvm->arch.n_requested_mmu_pages = kvm_nr_mmu_pages;

	spin_unlock(&kvm->mmu_lock);
	mutex_unlock(&kvm->slots_lock);
	return 0;
}

static int kvm_vm_ioctl_get_nr_mmu_pages(struct kvm *kvm)
{
	return kvm->arch.n_max_mmu_pages;
}

static int kvm_vm_ioctl_get_irqchip(struct kvm *kvm, struct kvm_irqchip *chip)
{
	int r;

	r = 0;
	switch (chip->chip_id) {
	case KVM_IRQCHIP_PIC_MASTER:
		memcpy(&chip->chip.pic,
			&pic_irqchip(kvm)->pics[0],
			sizeof(struct kvm_pic_state));
		break;
	case KVM_IRQCHIP_PIC_SLAVE:
		memcpy(&chip->chip.pic,
			&pic_irqchip(kvm)->pics[1],
			sizeof(struct kvm_pic_state));
		break;
	case KVM_IRQCHIP_IOAPIC:
		r = kvm_get_ioapic(kvm, &chip->chip.ioapic);
		break;
	default:
		r = -EINVAL;
		break;
	}
	return r;
}

static int kvm_vm_ioctl_set_irqchip(struct kvm *kvm, struct kvm_irqchip *chip)
{
	int r;

	r = 0;
	switch (chip->chip_id) {
	case KVM_IRQCHIP_PIC_MASTER:
		spin_lock(&pic_irqchip(kvm)->lock);
		memcpy(&pic_irqchip(kvm)->pics[0],
			&chip->chip.pic,
			sizeof(struct kvm_pic_state));
		spin_unlock(&pic_irqchip(kvm)->lock);
		break;
	case KVM_IRQCHIP_PIC_SLAVE:
		spin_lock(&pic_irqchip(kvm)->lock);
		memcpy(&pic_irqchip(kvm)->pics[1],
			&chip->chip.pic,
			sizeof(struct kvm_pic_state));
		spin_unlock(&pic_irqchip(kvm)->lock);
		break;
	case KVM_IRQCHIP_IOAPIC:
		r = kvm_set_ioapic(kvm, &chip->chip.ioapic);
		break;
	default:
		r = -EINVAL;
		break;
	}
	kvm_pic_update_irq(pic_irqchip(kvm));
	return r;
}

static int kvm_vm_ioctl_get_pit(struct kvm *kvm, struct kvm_pit_state *ps)
{
	int r = 0;

	mutex_lock(&kvm->arch.vpit->pit_state.lock);
	memcpy(ps, &kvm->arch.vpit->pit_state, sizeof(struct kvm_pit_state));
	mutex_unlock(&kvm->arch.vpit->pit_state.lock);
	return r;
}

static int kvm_vm_ioctl_set_pit(struct kvm *kvm, struct kvm_pit_state *ps)
{
	int r = 0;

	mutex_lock(&kvm->arch.vpit->pit_state.lock);
	memcpy(&kvm->arch.vpit->pit_state, ps, sizeof(struct kvm_pit_state));
	kvm_pit_load_count(kvm, 0, ps->channels[0].count, 0);
	mutex_unlock(&kvm->arch.vpit->pit_state.lock);
	return r;
}

static int kvm_vm_ioctl_get_pit2(struct kvm *kvm, struct kvm_pit_state2 *ps)
{
	int r = 0;

	mutex_lock(&kvm->arch.vpit->pit_state.lock);
	memcpy(ps->channels, &kvm->arch.vpit->pit_state.channels,
		sizeof(ps->channels));
	ps->flags = kvm->arch.vpit->pit_state.flags;
	mutex_unlock(&kvm->arch.vpit->pit_state.lock);
	memset(&ps->reserved, 0, sizeof(ps->reserved));
	return r;
}

static int kvm_vm_ioctl_set_pit2(struct kvm *kvm, struct kvm_pit_state2 *ps)
{
	int r = 0, start = 0;
	u32 prev_legacy, cur_legacy;
	mutex_lock(&kvm->arch.vpit->pit_state.lock);
	prev_legacy = kvm->arch.vpit->pit_state.flags & KVM_PIT_FLAGS_HPET_LEGACY;
	cur_legacy = ps->flags & KVM_PIT_FLAGS_HPET_LEGACY;
	if (!prev_legacy && cur_legacy)
		start = 1;
	memcpy(&kvm->arch.vpit->pit_state.channels, &ps->channels,
	       sizeof(kvm->arch.vpit->pit_state.channels));
	kvm->arch.vpit->pit_state.flags = ps->flags;
	kvm_pit_load_count(kvm, 0, kvm->arch.vpit->pit_state.channels[0].count, start);
	mutex_unlock(&kvm->arch.vpit->pit_state.lock);
	return r;
}

static int kvm_vm_ioctl_reinject(struct kvm *kvm,
				 struct kvm_reinject_control *control)
{
	if (!kvm->arch.vpit)
		return -ENXIO;
	mutex_lock(&kvm->arch.vpit->pit_state.lock);
	kvm->arch.vpit->pit_state.reinject = control->pit_reinject;
	mutex_unlock(&kvm->arch.vpit->pit_state.lock);
	return 0;
}

/**
 * kvm_vm_ioctl_get_dirty_log - get and clear the log of dirty pages in a slot
 * @kvm: kvm instance
 * @log: slot id and address to which we copy the log
 *
 * We need to keep it in mind that VCPU threads can write to the bitmap
 * concurrently.  So, to avoid losing data, we keep the following order for
 * each bit:
 *
 *   1. Take a snapshot of the bit and clear it if needed.
 *   2. Write protect the corresponding page.
 *   3. Flush TLB's if needed.
 *   4. Copy the snapshot to the userspace.
 *
 * Between 2 and 3, the guest may write to the page using the remaining TLB
 * entry.  This is not a problem because the page will be reported dirty at
 * step 4 using the snapshot taken before and step 3 ensures that successive
 * writes will be logged for the next call.
 */
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log)
{
	int r;
	struct kvm_memory_slot *memslot;
	unsigned long n, i;
	unsigned long *dirty_bitmap;
	unsigned long *dirty_bitmap_buffer;
	bool is_dirty = false;

	mutex_lock(&kvm->slots_lock);

	r = -EINVAL;
	if (log->slot >= KVM_MEMORY_SLOTS)
		goto out;

	memslot = id_to_memslot(kvm->memslots, log->slot);

	dirty_bitmap = memslot->dirty_bitmap;
	r = -ENOENT;
	if (!dirty_bitmap)
		goto out;

	n = kvm_dirty_bitmap_bytes(memslot);

	dirty_bitmap_buffer = dirty_bitmap + n / sizeof(long);
	memset(dirty_bitmap_buffer, 0, n);

	spin_lock(&kvm->mmu_lock);

	for (i = 0; i < n / sizeof(long); i++) {
		unsigned long mask;
		gfn_t offset;

		if (!dirty_bitmap[i])
			continue;

		is_dirty = true;

		mask = xchg(&dirty_bitmap[i], 0);
		dirty_bitmap_buffer[i] = mask;

		offset = i * BITS_PER_LONG;
		kvm_mmu_write_protect_pt_masked(kvm, memslot, offset, mask);
	}
	if (is_dirty)
		kvm_flush_remote_tlbs(kvm);

	spin_unlock(&kvm->mmu_lock);

	r = -EFAULT;
	if (copy_to_user(log->dirty_bitmap, dirty_bitmap_buffer, n))
		goto out;

	r = 0;
out:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irq_event)
{
	if (!irqchip_in_kernel(kvm))
		return -ENXIO;

	irq_event->status = kvm_set_irq(kvm, KVM_USERSPACE_IRQ_SOURCE_ID,
					irq_event->irq, irq_event->level);
	return 0;
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
		if (r < 0)
			goto out;
		break;
	case KVM_SET_IDENTITY_MAP_ADDR: {
		u64 ident_addr;

		r = -EFAULT;
		if (copy_from_user(&ident_addr, argp, sizeof ident_addr))
			goto out;
		r = kvm_vm_ioctl_set_identity_map_addr(kvm, ident_addr);
		if (r < 0)
			goto out;
		break;
	}
	case KVM_SET_NR_MMU_PAGES:
		r = kvm_vm_ioctl_set_nr_mmu_pages(kvm, arg);
		if (r)
			goto out;
		break;
	case KVM_GET_NR_MMU_PAGES:
		r = kvm_vm_ioctl_get_nr_mmu_pages(kvm);
		break;
	case KVM_CREATE_IRQCHIP: {
		struct kvm_pic *vpic;

		mutex_lock(&kvm->lock);
		r = -EEXIST;
		if (kvm->arch.vpic)
			goto create_irqchip_unlock;
		r = -EINVAL;
		if (atomic_read(&kvm->online_vcpus))
			goto create_irqchip_unlock;
		r = -ENOMEM;
		vpic = kvm_create_pic(kvm);
		if (vpic) {
			r = kvm_ioapic_init(kvm);
			if (r) {
				mutex_lock(&kvm->slots_lock);
				kvm_io_bus_unregister_dev(kvm, KVM_PIO_BUS,
							  &vpic->dev_master);
				kvm_io_bus_unregister_dev(kvm, KVM_PIO_BUS,
							  &vpic->dev_slave);
				kvm_io_bus_unregister_dev(kvm, KVM_PIO_BUS,
							  &vpic->dev_eclr);
				mutex_unlock(&kvm->slots_lock);
				kfree(vpic);
				goto create_irqchip_unlock;
			}
		} else
			goto create_irqchip_unlock;
		smp_wmb();
		kvm->arch.vpic = vpic;
		smp_wmb();
		r = kvm_setup_default_irq_routing(kvm);
		if (r) {
			mutex_lock(&kvm->slots_lock);
			mutex_lock(&kvm->irq_lock);
			kvm_ioapic_destroy(kvm);
			kvm_destroy_pic(kvm);
			mutex_unlock(&kvm->irq_lock);
			mutex_unlock(&kvm->slots_lock);
		}
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
		mutex_lock(&kvm->slots_lock);
		r = -EEXIST;
		if (kvm->arch.vpit)
			goto create_pit_unlock;
		r = -ENOMEM;
		kvm->arch.vpit = kvm_create_pit(kvm, u.pit_config.flags);
		if (kvm->arch.vpit)
			r = 0;
	create_pit_unlock:
		mutex_unlock(&kvm->slots_lock);
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
		if (!irqchip_in_kernel(kvm))
			goto get_irqchip_out;
		r = kvm_vm_ioctl_get_irqchip(kvm, chip);
		if (r)
			goto get_irqchip_out;
		r = -EFAULT;
		if (copy_to_user(argp, chip, sizeof *chip))
			goto get_irqchip_out;
		r = 0;
	get_irqchip_out:
		kfree(chip);
		if (r)
			goto out;
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
		if (!irqchip_in_kernel(kvm))
			goto set_irqchip_out;
		r = kvm_vm_ioctl_set_irqchip(kvm, chip);
		if (r)
			goto set_irqchip_out;
		r = 0;
	set_irqchip_out:
		kfree(chip);
		if (r)
			goto out;
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
		if (copy_from_user(&u.ps, argp, sizeof u.ps))
			goto out;
		r = -ENXIO;
		if (!kvm->arch.vpit)
			goto out;
		r = kvm_vm_ioctl_set_pit(kvm, &u.ps);
		if (r)
			goto out;
		r = 0;
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
		r = -ENXIO;
		if (!kvm->arch.vpit)
			goto out;
		r = kvm_vm_ioctl_set_pit2(kvm, &u.ps2);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_REINJECT_CONTROL: {
		struct kvm_reinject_control control;
		r =  -EFAULT;
		if (copy_from_user(&control, argp, sizeof(control)))
			goto out;
		r = kvm_vm_ioctl_reinject(kvm, &control);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_XEN_HVM_CONFIG: {
		r = -EFAULT;
		if (copy_from_user(&kvm->arch.xen_hvm_config, argp,
				   sizeof(struct kvm_xen_hvm_config)))
			goto out;
		r = -EINVAL;
		if (kvm->arch.xen_hvm_config.flags)
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_CLOCK: {
		struct kvm_clock_data user_ns;
		u64 now_ns;
		s64 delta;

		r = -EFAULT;
		if (copy_from_user(&user_ns, argp, sizeof(user_ns)))
			goto out;

		r = -EINVAL;
		if (user_ns.flags)
			goto out;

		r = 0;
		local_irq_disable();
		now_ns = get_kernel_ns();
		delta = user_ns.clock - now_ns;
		local_irq_enable();
		kvm->arch.kvmclock_offset = delta;
		break;
	}
	case KVM_GET_CLOCK: {
		struct kvm_clock_data user_ns;
		u64 now_ns;

		local_irq_disable();
		now_ns = get_kernel_ns();
		user_ns.clock = kvm->arch.kvmclock_offset + now_ns;
		local_irq_enable();
		user_ns.flags = 0;
		memset(&user_ns.pad, 0, sizeof(user_ns.pad));

		r = -EFAULT;
		if (copy_to_user(argp, &user_ns, sizeof(user_ns)))
			goto out;
		r = 0;
		break;
	}

	default:
		;
	}
out:
	return r;
}

static void kvm_init_msr_list(void)
{
	u32 dummy[2];
	unsigned i, j;

	/* skip the first msrs in the list. KVM-specific */
	for (i = j = KVM_SAVE_MSRS_BEGIN; i < ARRAY_SIZE(msrs_to_save); i++) {
		if (rdmsr_safe(msrs_to_save[i], &dummy[0], &dummy[1]) < 0)
			continue;
		if (j < i)
			msrs_to_save[j] = msrs_to_save[i];
		j++;
	}
	num_msrs_to_save = j;
}

static int vcpu_mmio_write(struct kvm_vcpu *vcpu, gpa_t addr, int len,
			   const void *v)
{
	int handled = 0;
	int n;

	do {
		n = min(len, 8);
		if (!(vcpu->arch.apic &&
		      !kvm_iodevice_write(&vcpu->arch.apic->dev, addr, n, v))
		    && kvm_io_bus_write(vcpu->kvm, KVM_MMIO_BUS, addr, n, v))
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
		if (!(vcpu->arch.apic &&
		      !kvm_iodevice_read(&vcpu->arch.apic->dev, addr, n, v))
		    && kvm_io_bus_read(vcpu->kvm, KVM_MMIO_BUS, addr, n, v))
			break;
		trace_kvm_mmio(KVM_TRACE_MMIO_READ, n, addr, *(u64 *)v);
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
	kvm_x86_ops->set_segment(vcpu, var, seg);
}

void kvm_get_segment(struct kvm_vcpu *vcpu,
		     struct kvm_segment *var, int seg)
{
	kvm_x86_ops->get_segment(vcpu, var, seg);
}

gpa_t translate_nested_gpa(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access)
{
	gpa_t t_gpa;
	struct x86_exception exception;

	BUG_ON(!mmu_is_nested(vcpu));

	/* NPT walks are always user-walks */
	access |= PFERR_USER_MASK;
	t_gpa  = vcpu->arch.mmu.gva_to_gpa(vcpu, gpa, access, &exception);

	return t_gpa;
}

gpa_t kvm_mmu_gva_to_gpa_read(struct kvm_vcpu *vcpu, gva_t gva,
			      struct x86_exception *exception)
{
	u32 access = (kvm_x86_ops->get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0;
	return vcpu->arch.walk_mmu->gva_to_gpa(vcpu, gva, access, exception);
}

 gpa_t kvm_mmu_gva_to_gpa_fetch(struct kvm_vcpu *vcpu, gva_t gva,
				struct x86_exception *exception)
{
	u32 access = (kvm_x86_ops->get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0;
	access |= PFERR_FETCH_MASK;
	return vcpu->arch.walk_mmu->gva_to_gpa(vcpu, gva, access, exception);
}

gpa_t kvm_mmu_gva_to_gpa_write(struct kvm_vcpu *vcpu, gva_t gva,
			       struct x86_exception *exception)
{
	u32 access = (kvm_x86_ops->get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0;
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
		ret = kvm_read_guest(vcpu->kvm, gpa, data, toread);
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
	u32 access = (kvm_x86_ops->get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0;

	return kvm_read_guest_virt_helper(addr, val, bytes, vcpu,
					  access | PFERR_FETCH_MASK,
					  exception);
}

int kvm_read_guest_virt(struct x86_emulate_ctxt *ctxt,
			       gva_t addr, void *val, unsigned int bytes,
			       struct x86_exception *exception)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	u32 access = (kvm_x86_ops->get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0;

	return kvm_read_guest_virt_helper(addr, val, bytes, vcpu, access,
					  exception);
}
EXPORT_SYMBOL_GPL(kvm_read_guest_virt);

static int kvm_read_guest_virt_system(struct x86_emulate_ctxt *ctxt,
				      gva_t addr, void *val, unsigned int bytes,
				      struct x86_exception *exception)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	return kvm_read_guest_virt_helper(addr, val, bytes, vcpu, 0, exception);
}

int kvm_write_guest_virt_system(struct x86_emulate_ctxt *ctxt,
				       gva_t addr, void *val,
				       unsigned int bytes,
				       struct x86_exception *exception)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	void *data = val;
	int r = X86EMUL_CONTINUE;

	while (bytes) {
		gpa_t gpa =  vcpu->arch.walk_mmu->gva_to_gpa(vcpu, addr,
							     PFERR_WRITE_MASK,
							     exception);
		unsigned offset = addr & (PAGE_SIZE-1);
		unsigned towrite = min(bytes, (unsigned)PAGE_SIZE - offset);
		int ret;

		if (gpa == UNMAPPED_GVA)
			return X86EMUL_PROPAGATE_FAULT;
		ret = kvm_write_guest(vcpu->kvm, gpa, data, towrite);
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
EXPORT_SYMBOL_GPL(kvm_write_guest_virt_system);

static int vcpu_mmio_gva_to_gpa(struct kvm_vcpu *vcpu, unsigned long gva,
				gpa_t *gpa, struct x86_exception *exception,
				bool write)
{
	u32 access = ((kvm_x86_ops->get_cpl(vcpu) == 3) ? PFERR_USER_MASK : 0)
		| (write ? PFERR_WRITE_MASK : 0);

	if (vcpu_match_mmio_gva(vcpu, gva)
	    && !permission_fault(vcpu->arch.walk_mmu, vcpu->arch.access, access)) {
		*gpa = vcpu->arch.mmio_gfn << PAGE_SHIFT |
					(gva & (PAGE_SIZE - 1));
		trace_vcpu_match_mmio(gva, *gpa, write, false);
		return 1;
	}

	*gpa = vcpu->arch.walk_mmu->gva_to_gpa(vcpu, gva, access, exception);

	if (*gpa == UNMAPPED_GVA)
		return -1;

	/* For APIC access vmexit */
	if ((*gpa & PAGE_MASK) == APIC_DEFAULT_PHYS_BASE)
		return 1;

	if (vcpu_match_mmio_gpa(vcpu, *gpa)) {
		trace_vcpu_match_mmio(gva, *gpa, write, true);
		return 1;
	}

	return 0;
}

int emulator_write_phys(struct kvm_vcpu *vcpu, gpa_t gpa,
			const void *val, int bytes)
{
	int ret;

	ret = kvm_write_guest(vcpu->kvm, gpa, val, bytes);
	if (ret < 0)
		return 0;
	kvm_mmu_pte_write(vcpu, gpa, val, bytes);
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
			       vcpu->mmio_fragments[0].gpa, *(u64 *)val);
		vcpu->mmio_read_completed = 0;
		return 1;
	}

	return 0;
}

static int read_emulate(struct kvm_vcpu *vcpu, gpa_t gpa,
			void *val, int bytes)
{
	return !kvm_read_guest(vcpu->kvm, gpa, val, bytes);
}

static int write_emulate(struct kvm_vcpu *vcpu, gpa_t gpa,
			 void *val, int bytes)
{
	return emulator_write_phys(vcpu, gpa, val, bytes);
}

static int write_mmio(struct kvm_vcpu *vcpu, gpa_t gpa, int bytes, void *val)
{
	trace_kvm_mmio(KVM_TRACE_MMIO_WRITE, bytes, gpa, *(u64 *)val);
	return vcpu_mmio_write(vcpu, gpa, bytes, val);
}

static int read_exit_mmio(struct kvm_vcpu *vcpu, gpa_t gpa,
			  void *val, int bytes)
{
	trace_kvm_mmio(KVM_TRACE_MMIO_READ_UNSATISFIED, bytes, gpa, 0);
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

	ret = vcpu_mmio_gva_to_gpa(vcpu, addr, &gpa, exception, write);

	if (ret < 0)
		return X86EMUL_PROPAGATE_FAULT;

	/* For APIC access vmexit */
	if (ret)
		goto mmio;

	if (ops->read_write_emulate(vcpu, gpa, val, bytes))
		return X86EMUL_CONTINUE;

mmio:
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

int emulator_read_write(struct x86_emulate_ctxt *ctxt, unsigned long addr,
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

int emulator_write_emulated(struct x86_emulate_ctxt *ctxt,
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
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	gpa_t gpa;
	struct page *page;
	char *kaddr;
	bool exchanged;

	/* guests cmpxchg8b have to be emulated atomically */
	if (bytes > 8 || (bytes & (bytes - 1)))
		goto emul_write;

	gpa = kvm_mmu_gva_to_gpa_write(vcpu, addr, NULL);

	if (gpa == UNMAPPED_GVA ||
	    (gpa & PAGE_MASK) == APIC_DEFAULT_PHYS_BASE)
		goto emul_write;

	if (((gpa + bytes - 1) & PAGE_MASK) != (gpa & PAGE_MASK))
		goto emul_write;

	page = gfn_to_page(vcpu->kvm, gpa >> PAGE_SHIFT);
	if (is_error_page(page))
		goto emul_write;

	kaddr = kmap_atomic(page);
	kaddr += offset_in_page(gpa);
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
	kunmap_atomic(kaddr);
	kvm_release_page_dirty(page);

	if (!exchanged)
		return X86EMUL_CMPXCHG_FAILED;

	kvm_mmu_pte_write(vcpu, gpa, new, bytes);

	return X86EMUL_CONTINUE;

emul_write:
	printk_once(KERN_WARNING "kvm: emulating exchange as write\n");

	return emulator_write_emulated(ctxt, addr, new, bytes, exception);
}

static int kernel_pio(struct kvm_vcpu *vcpu, void *pd)
{
	/* TODO: String I/O for in kernel device */
	int r;

	if (vcpu->arch.pio.in)
		r = kvm_io_bus_read(vcpu->kvm, KVM_PIO_BUS, vcpu->arch.pio.port,
				    vcpu->arch.pio.size, pd);
	else
		r = kvm_io_bus_write(vcpu->kvm, KVM_PIO_BUS,
				     vcpu->arch.pio.port, vcpu->arch.pio.size,
				     pd);
	return r;
}

static int emulator_pio_in_out(struct kvm_vcpu *vcpu, int size,
			       unsigned short port, void *val,
			       unsigned int count, bool in)
{
	trace_kvm_pio(!in, port, size, count);

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

static int emulator_pio_in_emulated(struct x86_emulate_ctxt *ctxt,
				    int size, unsigned short port, void *val,
				    unsigned int count)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	int ret;

	if (vcpu->arch.pio.count)
		goto data_avail;

	ret = emulator_pio_in_out(vcpu, size, port, val, count, true);
	if (ret) {
data_avail:
		memcpy(val, vcpu->arch.pio_data, size * count);
		vcpu->arch.pio.count = 0;
		return 1;
	}

	return 0;
}

static int emulator_pio_out_emulated(struct x86_emulate_ctxt *ctxt,
				     int size, unsigned short port,
				     const void *val, unsigned int count)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);

	memcpy(vcpu->arch.pio_data, val, size * count);
	return emulator_pio_in_out(vcpu, size, port, (void *)val, count, false);
}

static unsigned long get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	return kvm_x86_ops->get_segment_base(vcpu, seg);
}

static void emulator_invlpg(struct x86_emulate_ctxt *ctxt, ulong address)
{
	kvm_mmu_invlpg(emul_to_vcpu(ctxt), address);
}

int kvm_emulate_wbinvd(struct kvm_vcpu *vcpu)
{
	if (!need_emulate_wbinvd(vcpu))
		return X86EMUL_CONTINUE;

	if (kvm_x86_ops->has_wbinvd_exit()) {
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
EXPORT_SYMBOL_GPL(kvm_emulate_wbinvd);

static void emulator_wbinvd(struct x86_emulate_ctxt *ctxt)
{
	kvm_emulate_wbinvd(emul_to_vcpu(ctxt));
}

int emulator_get_dr(struct x86_emulate_ctxt *ctxt, int dr, unsigned long *dest)
{
	return _kvm_get_dr(emul_to_vcpu(ctxt), dr, dest);
}

int emulator_set_dr(struct x86_emulate_ctxt *ctxt, int dr, unsigned long value)
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

static void emulator_set_rflags(struct x86_emulate_ctxt *ctxt, ulong val)
{
	kvm_set_rflags(emul_to_vcpu(ctxt), val);
}

static int emulator_get_cpl(struct x86_emulate_ctxt *ctxt)
{
	return kvm_x86_ops->get_cpl(emul_to_vcpu(ctxt));
}

static void emulator_get_gdt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_ops->get_gdt(emul_to_vcpu(ctxt), dt);
}

static void emulator_get_idt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_ops->get_idt(emul_to_vcpu(ctxt), dt);
}

static void emulator_set_gdt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_ops->set_gdt(emul_to_vcpu(ctxt), dt);
}

static void emulator_set_idt(struct x86_emulate_ctxt *ctxt, struct desc_ptr *dt)
{
	kvm_x86_ops->set_idt(emul_to_vcpu(ctxt), dt);
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

	if (var.unusable)
		return false;

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
	var.present = desc->p;
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
	return kvm_get_msr(emul_to_vcpu(ctxt), msr_index, pdata);
}

static int emulator_set_msr(struct x86_emulate_ctxt *ctxt,
			    u32 msr_index, u64 data)
{
	return kvm_set_msr(emul_to_vcpu(ctxt), msr_index, data);
}

static int emulator_read_pmc(struct x86_emulate_ctxt *ctxt,
			     u32 pmc, u64 *pdata)
{
	return kvm_pmu_read_pmc(emul_to_vcpu(ctxt), pmc, pdata);
}

static void emulator_halt(struct x86_emulate_ctxt *ctxt)
{
	emul_to_vcpu(ctxt)->arch.halt_request = 1;
}

static void emulator_get_fpu(struct x86_emulate_ctxt *ctxt)
{
	preempt_disable();
	kvm_load_guest_fpu(emul_to_vcpu(ctxt));
	/*
	 * CR0.TS may reference the host fpu state, not the guest fpu state,
	 * so it may be clear at this point.
	 */
	clts();
}

static void emulator_put_fpu(struct x86_emulate_ctxt *ctxt)
{
	preempt_enable();
}

static int emulator_intercept(struct x86_emulate_ctxt *ctxt,
			      struct x86_instruction_info *info,
			      enum x86_intercept_stage stage)
{
	return kvm_x86_ops->check_intercept(emul_to_vcpu(ctxt), info, stage);
}

static void emulator_get_cpuid(struct x86_emulate_ctxt *ctxt,
			       u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
	kvm_cpuid(emul_to_vcpu(ctxt), eax, ebx, ecx, edx);
}

static ulong emulator_read_gpr(struct x86_emulate_ctxt *ctxt, unsigned reg)
{
	return kvm_register_read(emul_to_vcpu(ctxt), reg);
}

static void emulator_write_gpr(struct x86_emulate_ctxt *ctxt, unsigned reg, ulong val)
{
	kvm_register_write(emul_to_vcpu(ctxt), reg, val);
}

static const struct x86_emulate_ops emulate_ops = {
	.read_gpr            = emulator_read_gpr,
	.write_gpr           = emulator_write_gpr,
	.read_std            = kvm_read_guest_virt_system,
	.write_std           = kvm_write_guest_virt_system,
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
	.set_rflags          = emulator_set_rflags,
	.cpl                 = emulator_get_cpl,
	.get_dr              = emulator_get_dr,
	.set_dr              = emulator_set_dr,
	.set_msr             = emulator_set_msr,
	.get_msr             = emulator_get_msr,
	.read_pmc            = emulator_read_pmc,
	.halt                = emulator_halt,
	.wbinvd              = emulator_wbinvd,
	.fix_hypercall       = emulator_fix_hypercall,
	.get_fpu             = emulator_get_fpu,
	.put_fpu             = emulator_put_fpu,
	.intercept           = emulator_intercept,
	.get_cpuid           = emulator_get_cpuid,
};

static void toggle_interruptibility(struct kvm_vcpu *vcpu, u32 mask)
{
	u32 int_shadow = kvm_x86_ops->get_interrupt_shadow(vcpu, mask);
	/*
	 * an sti; sti; sequence only disable interrupts for the first
	 * instruction. So, if the last instruction, be it emulated or
	 * not, left the system with the INT_STI flag enabled, it
	 * means that the last instruction is an sti. We should not
	 * leave the flag on in this case. The same goes for mov ss
	 */
	if (!(int_shadow & mask))
		kvm_x86_ops->set_interrupt_shadow(vcpu, mask);
}

static void inject_emulated_exception(struct kvm_vcpu *vcpu)
{
	struct x86_emulate_ctxt *ctxt = &vcpu->arch.emulate_ctxt;
	if (ctxt->exception.vector == PF_VECTOR)
		kvm_propagate_fault(vcpu, &ctxt->exception);
	else if (ctxt->exception.error_code_valid)
		kvm_queue_exception_e(vcpu, ctxt->exception.vector,
				      ctxt->exception.error_code);
	else
		kvm_queue_exception(vcpu, ctxt->exception.vector);
}

static void init_decode_cache(struct x86_emulate_ctxt *ctxt)
{
	memset(&ctxt->twobyte, 0,
	       (void *)&ctxt->_regs - (void *)&ctxt->twobyte);

	ctxt->fetch.start = 0;
	ctxt->fetch.end = 0;
	ctxt->io_read.pos = 0;
	ctxt->io_read.end = 0;
	ctxt->mem_read.pos = 0;
	ctxt->mem_read.end = 0;
}

static void init_emulate_ctxt(struct kvm_vcpu *vcpu)
{
	struct x86_emulate_ctxt *ctxt = &vcpu->arch.emulate_ctxt;
	int cs_db, cs_l;

	kvm_x86_ops->get_cs_db_l_bits(vcpu, &cs_db, &cs_l);

	ctxt->eflags = kvm_get_rflags(vcpu);
	ctxt->eip = kvm_rip_read(vcpu);
	ctxt->mode = (!is_protmode(vcpu))		? X86EMUL_MODE_REAL :
		     (ctxt->eflags & X86_EFLAGS_VM)	? X86EMUL_MODE_VM86 :
		     cs_l				? X86EMUL_MODE_PROT64 :
		     cs_db				? X86EMUL_MODE_PROT32 :
							  X86EMUL_MODE_PROT16;
	ctxt->guest_mode = is_guest_mode(vcpu);

	init_decode_cache(ctxt);
	vcpu->arch.emulate_regs_need_sync_from_vcpu = false;
}

int kvm_inject_realmode_interrupt(struct kvm_vcpu *vcpu, int irq, int inc_eip)
{
	struct x86_emulate_ctxt *ctxt = &vcpu->arch.emulate_ctxt;
	int ret;

	init_emulate_ctxt(vcpu);

	ctxt->op_bytes = 2;
	ctxt->ad_bytes = 2;
	ctxt->_eip = ctxt->eip + inc_eip;
	ret = emulate_int_real(ctxt, irq);

	if (ret != X86EMUL_CONTINUE)
		return EMULATE_FAIL;

	ctxt->eip = ctxt->_eip;
	kvm_rip_write(vcpu, ctxt->eip);
	kvm_set_rflags(vcpu, ctxt->eflags);

	if (irq == NMI_VECTOR)
		vcpu->arch.nmi_pending = 0;
	else
		vcpu->arch.interrupt.pending = false;

	return EMULATE_DONE;
}
EXPORT_SYMBOL_GPL(kvm_inject_realmode_interrupt);

static int handle_emulation_failure(struct kvm_vcpu *vcpu)
{
	int r = EMULATE_DONE;

	++vcpu->stat.insn_emulation_fail;
	trace_kvm_emulate_insn_failed(vcpu);
	if (!is_guest_mode(vcpu)) {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror = KVM_INTERNAL_ERROR_EMULATION;
		vcpu->run->internal.ndata = 0;
		r = EMULATE_FAIL;
	}
	kvm_queue_exception(vcpu, UD_VECTOR);

	return r;
}

static bool reexecute_instruction(struct kvm_vcpu *vcpu, gva_t gva)
{
	gpa_t gpa;
	pfn_t pfn;

	if (tdp_enabled)
		return false;

	/*
	 * if emulation was due to access to shadowed page table
	 * and it failed try to unshadow page and re-enter the
	 * guest to let CPU execute the instruction.
	 */
	if (kvm_mmu_unprotect_page_virt(vcpu, gva))
		return true;

	gpa = kvm_mmu_gva_to_gpa_system(vcpu, gva, NULL);

	if (gpa == UNMAPPED_GVA)
		return true; /* let cpu generate fault */

	/*
	 * Do not retry the unhandleable instruction if it faults on the
	 * readonly host memory, otherwise it will goto a infinite loop:
	 * retry instruction -> write #PF -> emulation fail -> retry
	 * instruction -> ...
	 */
	pfn = gfn_to_pfn(vcpu->kvm, gpa_to_gfn(gpa));
	if (!is_error_pfn(pfn)) {
		kvm_release_pfn_clean(pfn);
		return true;
	}

	return false;
}

static bool retry_instruction(struct x86_emulate_ctxt *ctxt,
			      unsigned long cr2,  int emulation_type)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	unsigned long last_retry_eip, last_retry_addr, gpa = cr2;

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

	if (!(emulation_type & EMULTYPE_RETRY))
		return false;

	if (x86_page_table_writing_insn(ctxt))
		return false;

	if (ctxt->eip == last_retry_eip && last_retry_addr == cr2)
		return false;

	vcpu->arch.last_retry_eip = ctxt->eip;
	vcpu->arch.last_retry_addr = cr2;

	if (!vcpu->arch.mmu.direct_map)
		gpa = kvm_mmu_gva_to_gpa_write(vcpu, cr2, NULL);

	kvm_mmu_unprotect_page(vcpu->kvm, gpa >> PAGE_SHIFT);

	return true;
}

static int complete_emulated_mmio(struct kvm_vcpu *vcpu);
static int complete_emulated_pio(struct kvm_vcpu *vcpu);

int x86_emulate_instruction(struct kvm_vcpu *vcpu,
			    unsigned long cr2,
			    int emulation_type,
			    void *insn,
			    int insn_len)
{
	int r;
	struct x86_emulate_ctxt *ctxt = &vcpu->arch.emulate_ctxt;
	bool writeback = true;

	kvm_clear_exception_queue(vcpu);

	if (!(emulation_type & EMULTYPE_NO_DECODE)) {
		init_emulate_ctxt(vcpu);
		ctxt->interruptibility = 0;
		ctxt->have_exception = false;
		ctxt->perm_ok = false;

		ctxt->only_vendor_specific_insn
			= emulation_type & EMULTYPE_TRAP_UD;

		r = x86_decode_insn(ctxt, insn, insn_len);

		trace_kvm_emulate_insn_start(vcpu);
		++vcpu->stat.insn_emulation;
		if (r != EMULATION_OK)  {
			if (emulation_type & EMULTYPE_TRAP_UD)
				return EMULATE_FAIL;
			if (reexecute_instruction(vcpu, cr2))
				return EMULATE_DONE;
			if (emulation_type & EMULTYPE_SKIP)
				return EMULATE_FAIL;
			return handle_emulation_failure(vcpu);
		}
	}

	if (emulation_type & EMULTYPE_SKIP) {
		kvm_rip_write(vcpu, ctxt->_eip);
		return EMULATE_DONE;
	}

	if (retry_instruction(ctxt, cr2, emulation_type))
		return EMULATE_DONE;

	/* this is needed for vmware backdoor interface to work since it
	   changes registers values  during IO operation */
	if (vcpu->arch.emulate_regs_need_sync_from_vcpu) {
		vcpu->arch.emulate_regs_need_sync_from_vcpu = false;
		emulator_invalidate_register_cache(ctxt);
	}

restart:
	r = x86_emulate_insn(ctxt);

	if (r == EMULATION_INTERCEPTED)
		return EMULATE_DONE;

	if (r == EMULATION_FAILED) {
		if (reexecute_instruction(vcpu, cr2))
			return EMULATE_DONE;

		return handle_emulation_failure(vcpu);
	}

	if (ctxt->have_exception) {
		inject_emulated_exception(vcpu);
		r = EMULATE_DONE;
	} else if (vcpu->arch.pio.count) {
		if (!vcpu->arch.pio.in)
			vcpu->arch.pio.count = 0;
		else {
			writeback = false;
			vcpu->arch.complete_userspace_io = complete_emulated_pio;
		}
		r = EMULATE_DO_MMIO;
	} else if (vcpu->mmio_needed) {
		if (!vcpu->mmio_is_write)
			writeback = false;
		r = EMULATE_DO_MMIO;
		vcpu->arch.complete_userspace_io = complete_emulated_mmio;
	} else if (r == EMULATION_RESTART)
		goto restart;
	else
		r = EMULATE_DONE;

	if (writeback) {
		toggle_interruptibility(vcpu, ctxt->interruptibility);
		kvm_set_rflags(vcpu, ctxt->eflags);
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		vcpu->arch.emulate_regs_need_sync_to_vcpu = false;
		kvm_rip_write(vcpu, ctxt->eip);
	} else
		vcpu->arch.emulate_regs_need_sync_to_vcpu = true;

	return r;
}
EXPORT_SYMBOL_GPL(x86_emulate_instruction);

int kvm_fast_pio_out(struct kvm_vcpu *vcpu, int size, unsigned short port)
{
	unsigned long val = kvm_register_read(vcpu, VCPU_REGS_RAX);
	int ret = emulator_pio_out_emulated(&vcpu->arch.emulate_ctxt,
					    size, port, &val, 1);
	/* do not return to emulator after return from userspace */
	vcpu->arch.pio.count = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(kvm_fast_pio_out);

static void tsc_bad(void *info)
{
	__this_cpu_write(cpu_tsc_khz, 0);
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

static int kvmclock_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				     void *data)
{
	struct cpufreq_freqs *freq = data;
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

	if (val == CPUFREQ_PRECHANGE && freq->old > freq->new)
		return 0;
	if (val == CPUFREQ_POSTCHANGE && freq->old < freq->new)
		return 0;

	smp_call_function_single(freq->cpu, tsc_khz_changed, freq, 1);

	raw_spin_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list) {
		kvm_for_each_vcpu(i, vcpu, kvm) {
			if (vcpu->cpu != freq->cpu)
				continue;
			kvm_make_request(KVM_REQ_CLOCK_UPDATE, vcpu);
			if (vcpu->cpu != smp_processor_id())
				send_ipi = 1;
		}
	}
	raw_spin_unlock(&kvm_lock);

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
		smp_call_function_single(freq->cpu, tsc_khz_changed, freq, 1);
	}
	return 0;
}

static struct notifier_block kvmclock_cpufreq_notifier_block = {
	.notifier_call  = kvmclock_cpufreq_notifier
};

static int kvmclock_cpu_notifier(struct notifier_block *nfb,
					unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
		case CPU_ONLINE:
		case CPU_DOWN_FAILED:
			smp_call_function_single(cpu, tsc_khz_changed, NULL, 1);
			break;
		case CPU_DOWN_PREPARE:
			smp_call_function_single(cpu, tsc_bad, NULL, 1);
			break;
	}
	return NOTIFY_OK;
}

static struct notifier_block kvmclock_cpu_notifier_block = {
	.notifier_call  = kvmclock_cpu_notifier,
	.priority = -INT_MAX
};

static void kvm_timer_init(void)
{
	int cpu;

	max_tsc_khz = tsc_khz;
	register_hotcpu_notifier(&kvmclock_cpu_notifier_block);
	if (!boot_cpu_has(X86_FEATURE_CONSTANT_TSC)) {
#ifdef CONFIG_CPU_FREQ
		struct cpufreq_policy policy;
		memset(&policy, 0, sizeof(policy));
		cpu = get_cpu();
		cpufreq_get_policy(&policy, cpu);
		if (policy.cpuinfo.max_freq)
			max_tsc_khz = policy.cpuinfo.max_freq;
		put_cpu();
#endif
		cpufreq_register_notifier(&kvmclock_cpufreq_notifier_block,
					  CPUFREQ_TRANSITION_NOTIFIER);
	}
	pr_debug("kvm: max_tsc_khz = %ld\n", max_tsc_khz);
	for_each_online_cpu(cpu)
		smp_call_function_single(cpu, tsc_khz_changed, NULL, 1);
}

static DEFINE_PER_CPU(struct kvm_vcpu *, current_vcpu);

int kvm_is_in_guest(void)
{
	return __this_cpu_read(current_vcpu) != NULL;
}

static int kvm_is_user_mode(void)
{
	int user_mode = 3;

	if (__this_cpu_read(current_vcpu))
		user_mode = kvm_x86_ops->get_cpl(__this_cpu_read(current_vcpu));

	return user_mode != 0;
}

static unsigned long kvm_get_guest_ip(void)
{
	unsigned long ip = 0;

	if (__this_cpu_read(current_vcpu))
		ip = kvm_rip_read(__this_cpu_read(current_vcpu));

	return ip;
}

static struct perf_guest_info_callbacks kvm_guest_cbs = {
	.is_in_guest		= kvm_is_in_guest,
	.is_user_mode		= kvm_is_user_mode,
	.get_guest_ip		= kvm_get_guest_ip,
};

void kvm_before_handle_nmi(struct kvm_vcpu *vcpu)
{
	__this_cpu_write(current_vcpu, vcpu);
}
EXPORT_SYMBOL_GPL(kvm_before_handle_nmi);

void kvm_after_handle_nmi(struct kvm_vcpu *vcpu)
{
	__this_cpu_write(current_vcpu, NULL);
}
EXPORT_SYMBOL_GPL(kvm_after_handle_nmi);

static void kvm_set_mmio_spte_mask(void)
{
	u64 mask;
	int maxphyaddr = boot_cpu_data.x86_phys_bits;

	/*
	 * Set the reserved bits and the present bit of an paging-structure
	 * entry to generate page fault with PFER.RSV = 1.
	 */
	mask = ((1ull << (62 - maxphyaddr + 1)) - 1) << maxphyaddr;
	mask |= 1ull;

#ifdef CONFIG_X86_64
	/*
	 * If reserved bit is not supported, clear the present bit to disable
	 * mmio page fault.
	 */
	if (maxphyaddr == 52)
		mask &= ~1ull;
#endif

	kvm_mmu_set_mmio_spte_mask(mask);
}

int kvm_arch_init(void *opaque)
{
	int r;
	struct kvm_x86_ops *ops = (struct kvm_x86_ops *)opaque;

	if (kvm_x86_ops) {
		printk(KERN_ERR "kvm: already loaded the other module\n");
		r = -EEXIST;
		goto out;
	}

	if (!ops->cpu_has_kvm_support()) {
		printk(KERN_ERR "kvm: no hardware support\n");
		r = -EOPNOTSUPP;
		goto out;
	}
	if (ops->disabled_by_bios()) {
		printk(KERN_ERR "kvm: disabled by bios\n");
		r = -EOPNOTSUPP;
		goto out;
	}

	r = kvm_mmu_module_init();
	if (r)
		goto out;

	kvm_set_mmio_spte_mask();
	kvm_init_msr_list();

	kvm_x86_ops = ops;
	kvm_mmu_set_mask_ptes(PT_USER_MASK, PT_ACCESSED_MASK,
			PT_DIRTY_MASK, PT64_NX_MASK, 0);

	kvm_timer_init();

	perf_register_guest_info_callbacks(&kvm_guest_cbs);

	if (cpu_has_xsave)
		host_xcr0 = xgetbv(XCR_XFEATURE_ENABLED_MASK);

	kvm_lapic_init();
	return 0;

out:
	return r;
}

void kvm_arch_exit(void)
{
	perf_unregister_guest_info_callbacks(&kvm_guest_cbs);

	if (!boot_cpu_has(X86_FEATURE_CONSTANT_TSC))
		cpufreq_unregister_notifier(&kvmclock_cpufreq_notifier_block,
					    CPUFREQ_TRANSITION_NOTIFIER);
	unregister_hotcpu_notifier(&kvmclock_cpu_notifier_block);
	kvm_x86_ops = NULL;
	kvm_mmu_module_exit();
}

int kvm_emulate_halt(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.halt_exits;
	if (irqchip_in_kernel(vcpu->kvm)) {
		vcpu->arch.mp_state = KVM_MP_STATE_HALTED;
		return 1;
	} else {
		vcpu->run->exit_reason = KVM_EXIT_HLT;
		return 0;
	}
}
EXPORT_SYMBOL_GPL(kvm_emulate_halt);

int kvm_hv_hypercall(struct kvm_vcpu *vcpu)
{
	u64 param, ingpa, outgpa, ret;
	uint16_t code, rep_idx, rep_cnt, res = HV_STATUS_SUCCESS, rep_done = 0;
	bool fast, longmode;
	int cs_db, cs_l;

	/*
	 * hypercall generates UD from non zero cpl and real mode
	 * per HYPER-V spec
	 */
	if (kvm_x86_ops->get_cpl(vcpu) != 0 || !is_protmode(vcpu)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 0;
	}

	kvm_x86_ops->get_cs_db_l_bits(vcpu, &cs_db, &cs_l);
	longmode = is_long_mode(vcpu) && cs_l == 1;

	if (!longmode) {
		param = ((u64)kvm_register_read(vcpu, VCPU_REGS_RDX) << 32) |
			(kvm_register_read(vcpu, VCPU_REGS_RAX) & 0xffffffff);
		ingpa = ((u64)kvm_register_read(vcpu, VCPU_REGS_RBX) << 32) |
			(kvm_register_read(vcpu, VCPU_REGS_RCX) & 0xffffffff);
		outgpa = ((u64)kvm_register_read(vcpu, VCPU_REGS_RDI) << 32) |
			(kvm_register_read(vcpu, VCPU_REGS_RSI) & 0xffffffff);
	}
#ifdef CONFIG_X86_64
	else {
		param = kvm_register_read(vcpu, VCPU_REGS_RCX);
		ingpa = kvm_register_read(vcpu, VCPU_REGS_RDX);
		outgpa = kvm_register_read(vcpu, VCPU_REGS_R8);
	}
#endif

	code = param & 0xffff;
	fast = (param >> 16) & 0x1;
	rep_cnt = (param >> 32) & 0xfff;
	rep_idx = (param >> 48) & 0xfff;

	trace_kvm_hv_hypercall(code, fast, rep_cnt, rep_idx, ingpa, outgpa);

	switch (code) {
	case HV_X64_HV_NOTIFY_LONG_SPIN_WAIT:
		kvm_vcpu_on_spin(vcpu);
		break;
	default:
		res = HV_STATUS_INVALID_HYPERCALL_CODE;
		break;
	}

	ret = res | (((u64)rep_done & 0xfff) << 32);
	if (longmode) {
		kvm_register_write(vcpu, VCPU_REGS_RAX, ret);
	} else {
		kvm_register_write(vcpu, VCPU_REGS_RDX, ret >> 32);
		kvm_register_write(vcpu, VCPU_REGS_RAX, ret & 0xffffffff);
	}

	return 1;
}

int kvm_emulate_hypercall(struct kvm_vcpu *vcpu)
{
	unsigned long nr, a0, a1, a2, a3, ret;
	int r = 1;

	if (kvm_hv_hypercall_enabled(vcpu->kvm))
		return kvm_hv_hypercall(vcpu);

	nr = kvm_register_read(vcpu, VCPU_REGS_RAX);
	a0 = kvm_register_read(vcpu, VCPU_REGS_RBX);
	a1 = kvm_register_read(vcpu, VCPU_REGS_RCX);
	a2 = kvm_register_read(vcpu, VCPU_REGS_RDX);
	a3 = kvm_register_read(vcpu, VCPU_REGS_RSI);

	trace_kvm_hypercall(nr, a0, a1, a2, a3);

	if (!is_long_mode(vcpu)) {
		nr &= 0xFFFFFFFF;
		a0 &= 0xFFFFFFFF;
		a1 &= 0xFFFFFFFF;
		a2 &= 0xFFFFFFFF;
		a3 &= 0xFFFFFFFF;
	}

	if (kvm_x86_ops->get_cpl(vcpu) != 0) {
		ret = -KVM_EPERM;
		goto out;
	}

	switch (nr) {
	case KVM_HC_VAPIC_POLL_IRQ:
		ret = 0;
		break;
	default:
		ret = -KVM_ENOSYS;
		break;
	}
out:
	kvm_register_write(vcpu, VCPU_REGS_RAX, ret);
	++vcpu->stat.hypercalls;
	return r;
}
EXPORT_SYMBOL_GPL(kvm_emulate_hypercall);

int emulator_fix_hypercall(struct x86_emulate_ctxt *ctxt)
{
	struct kvm_vcpu *vcpu = emul_to_vcpu(ctxt);
	char instruction[3];
	unsigned long rip = kvm_rip_read(vcpu);

	/*
	 * Blow out the MMU to ensure that no other VCPU has an active mapping
	 * to ensure that the updated hypercall appears atomically across all
	 * VCPUs.
	 */
	kvm_mmu_zap_all(vcpu->kvm);

	kvm_x86_ops->patch_hypercall(vcpu, instruction);

	return emulator_write_emulated(ctxt, rip, instruction, 3, NULL);
}

/*
 * Check if userspace requested an interrupt window, and that the
 * interrupt window is open.
 *
 * No need to exit to userspace if we already have an interrupt queued.
 */
static int dm_request_for_irq_injection(struct kvm_vcpu *vcpu)
{
	return (!irqchip_in_kernel(vcpu->kvm) && !kvm_cpu_has_interrupt(vcpu) &&
		vcpu->run->request_interrupt_window &&
		kvm_arch_interrupt_allowed(vcpu));
}

static void post_kvm_run_save(struct kvm_vcpu *vcpu)
{
	struct kvm_run *kvm_run = vcpu->run;

	kvm_run->if_flag = (kvm_get_rflags(vcpu) & X86_EFLAGS_IF) != 0;
	kvm_run->cr8 = kvm_get_cr8(vcpu);
	kvm_run->apic_base = kvm_get_apic_base(vcpu);
	if (irqchip_in_kernel(vcpu->kvm))
		kvm_run->ready_for_interrupt_injection = 1;
	else
		kvm_run->ready_for_interrupt_injection =
			kvm_arch_interrupt_allowed(vcpu) &&
			!kvm_cpu_has_interrupt(vcpu) &&
			!kvm_event_needs_reinjection(vcpu);
}

static int vapic_enter(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	struct page *page;

	if (!apic || !apic->vapic_addr)
		return 0;

	page = gfn_to_page(vcpu->kvm, apic->vapic_addr >> PAGE_SHIFT);
	if (is_error_page(page))
		return -EFAULT;

	vcpu->arch.apic->vapic_page = page;
	return 0;
}

static void vapic_exit(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	int idx;

	if (!apic || !apic->vapic_addr)
		return;

	idx = srcu_read_lock(&vcpu->kvm->srcu);
	kvm_release_page_dirty(apic->vapic_page);
	mark_page_dirty(vcpu->kvm, apic->vapic_addr >> PAGE_SHIFT);
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
}

static void update_cr8_intercept(struct kvm_vcpu *vcpu)
{
	int max_irr, tpr;

	if (!kvm_x86_ops->update_cr8_intercept)
		return;

	if (!vcpu->arch.apic)
		return;

	if (!vcpu->arch.apic->vapic_addr)
		max_irr = kvm_lapic_find_highest_irr(vcpu);
	else
		max_irr = -1;

	if (max_irr != -1)
		max_irr >>= 4;

	tpr = kvm_lapic_get_cr8(vcpu);

	kvm_x86_ops->update_cr8_intercept(vcpu, tpr, max_irr);
}

static void inject_pending_event(struct kvm_vcpu *vcpu)
{
	/* try to reinject previous events if any */
	if (vcpu->arch.exception.pending) {
		trace_kvm_inj_exception(vcpu->arch.exception.nr,
					vcpu->arch.exception.has_error_code,
					vcpu->arch.exception.error_code);
		kvm_x86_ops->queue_exception(vcpu, vcpu->arch.exception.nr,
					  vcpu->arch.exception.has_error_code,
					  vcpu->arch.exception.error_code,
					  vcpu->arch.exception.reinject);
		return;
	}

	if (vcpu->arch.nmi_injected) {
		kvm_x86_ops->set_nmi(vcpu);
		return;
	}

	if (vcpu->arch.interrupt.pending) {
		kvm_x86_ops->set_irq(vcpu);
		return;
	}

	/* try to inject new event if pending */
	if (vcpu->arch.nmi_pending) {
		if (kvm_x86_ops->nmi_allowed(vcpu)) {
			--vcpu->arch.nmi_pending;
			vcpu->arch.nmi_injected = true;
			kvm_x86_ops->set_nmi(vcpu);
		}
	} else if (kvm_cpu_has_interrupt(vcpu)) {
		if (kvm_x86_ops->interrupt_allowed(vcpu)) {
			kvm_queue_interrupt(vcpu, kvm_cpu_get_interrupt(vcpu),
					    false);
			kvm_x86_ops->set_irq(vcpu);
		}
	}
}

static void kvm_load_guest_xcr0(struct kvm_vcpu *vcpu)
{
	if (kvm_read_cr4_bits(vcpu, X86_CR4_OSXSAVE) &&
			!vcpu->guest_xcr0_loaded) {
		/* kvm_set_xcr() also depends on this */
		xsetbv(XCR_XFEATURE_ENABLED_MASK, vcpu->arch.xcr0);
		vcpu->guest_xcr0_loaded = 1;
	}
}

static void kvm_put_guest_xcr0(struct kvm_vcpu *vcpu)
{
	if (vcpu->guest_xcr0_loaded) {
		if (vcpu->arch.xcr0 != host_xcr0)
			xsetbv(XCR_XFEATURE_ENABLED_MASK, host_xcr0);
		vcpu->guest_xcr0_loaded = 0;
	}
}

static void process_nmi(struct kvm_vcpu *vcpu)
{
	unsigned limit = 2;

	/*
	 * x86 is limited to one NMI running, and one NMI pending after it.
	 * If an NMI is already in progress, limit further NMIs to just one.
	 * Otherwise, allow two (and we'll inject the first one immediately).
	 */
	if (kvm_x86_ops->get_nmi_mask(vcpu) || vcpu->arch.nmi_injected)
		limit = 1;

	vcpu->arch.nmi_pending += atomic_xchg(&vcpu->arch.nmi_queued, 0);
	vcpu->arch.nmi_pending = min(vcpu->arch.nmi_pending, limit);
	kvm_make_request(KVM_REQ_EVENT, vcpu);
}

static int vcpu_enter_guest(struct kvm_vcpu *vcpu)
{
	int r;
	bool req_int_win = !irqchip_in_kernel(vcpu->kvm) &&
		vcpu->run->request_interrupt_window;
	bool req_immediate_exit = 0;

	if (vcpu->requests) {
		if (kvm_check_request(KVM_REQ_MMU_RELOAD, vcpu))
			kvm_mmu_unload(vcpu);
		if (kvm_check_request(KVM_REQ_MIGRATE_TIMER, vcpu))
			__kvm_migrate_timers(vcpu);
		if (kvm_check_request(KVM_REQ_CLOCK_UPDATE, vcpu)) {
			r = kvm_guest_time_update(vcpu);
			if (unlikely(r))
				goto out;
		}
		if (kvm_check_request(KVM_REQ_MMU_SYNC, vcpu))
			kvm_mmu_sync_roots(vcpu);
		if (kvm_check_request(KVM_REQ_TLB_FLUSH, vcpu))
			kvm_x86_ops->tlb_flush(vcpu);
		if (kvm_check_request(KVM_REQ_REPORT_TPR_ACCESS, vcpu)) {
			vcpu->run->exit_reason = KVM_EXIT_TPR_ACCESS;
			r = 0;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_TRIPLE_FAULT, vcpu)) {
			vcpu->run->exit_reason = KVM_EXIT_SHUTDOWN;
			r = 0;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_DEACTIVATE_FPU, vcpu)) {
			vcpu->fpu_active = 0;
			kvm_x86_ops->fpu_deactivate(vcpu);
		}
		if (kvm_check_request(KVM_REQ_APF_HALT, vcpu)) {
			/* Page is swapped out. Do synthetic halt */
			vcpu->arch.apf.halted = true;
			r = 1;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_STEAL_UPDATE, vcpu))
			record_steal_time(vcpu);
		if (kvm_check_request(KVM_REQ_NMI, vcpu))
			process_nmi(vcpu);
		req_immediate_exit =
			kvm_check_request(KVM_REQ_IMMEDIATE_EXIT, vcpu);
		if (kvm_check_request(KVM_REQ_PMU, vcpu))
			kvm_handle_pmu_event(vcpu);
		if (kvm_check_request(KVM_REQ_PMI, vcpu))
			kvm_deliver_pmi(vcpu);
	}

	if (kvm_check_request(KVM_REQ_EVENT, vcpu) || req_int_win) {
		inject_pending_event(vcpu);

		/* enable NMI/IRQ window open exits if needed */
		if (vcpu->arch.nmi_pending)
			kvm_x86_ops->enable_nmi_window(vcpu);
		else if (kvm_cpu_has_interrupt(vcpu) || req_int_win)
			kvm_x86_ops->enable_irq_window(vcpu);

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

	kvm_x86_ops->prepare_guest_switch(vcpu);
	if (vcpu->fpu_active)
		kvm_load_guest_fpu(vcpu);
	kvm_load_guest_xcr0(vcpu);

	vcpu->mode = IN_GUEST_MODE;

	/* We should set ->mode before check ->requests,
	 * see the comment in make_all_cpus_request.
	 */
	smp_mb();

	local_irq_disable();

	if (vcpu->mode == EXITING_GUEST_MODE || vcpu->requests
	    || need_resched() || signal_pending(current)) {
		vcpu->mode = OUTSIDE_GUEST_MODE;
		smp_wmb();
		local_irq_enable();
		preempt_enable();
		r = 1;
		goto cancel_injection;
	}

	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);

	if (req_immediate_exit)
		smp_send_reschedule(vcpu->cpu);

	kvm_guest_enter();

	if (unlikely(vcpu->arch.switch_db_regs)) {
		set_debugreg(0, 7);
		set_debugreg(vcpu->arch.eff_db[0], 0);
		set_debugreg(vcpu->arch.eff_db[1], 1);
		set_debugreg(vcpu->arch.eff_db[2], 2);
		set_debugreg(vcpu->arch.eff_db[3], 3);
	}

	trace_kvm_entry(vcpu->vcpu_id);
	kvm_x86_ops->run(vcpu);

	/*
	 * If the guest has used debug registers, at least dr7
	 * will be disabled while returning to the host.
	 * If we don't have active breakpoints in the host, we don't
	 * care about the messed up debug address registers. But if
	 * we have some of them active, restore the old state.
	 */
	if (hw_breakpoint_active())
		hw_breakpoint_restore();

	vcpu->arch.last_guest_tsc = kvm_x86_ops->read_l1_tsc(vcpu);

	vcpu->mode = OUTSIDE_GUEST_MODE;
	smp_wmb();
	local_irq_enable();

	++vcpu->stat.exits;

	/*
	 * We must have an instruction between local_irq_enable() and
	 * kvm_guest_exit(), so the timer interrupt isn't delayed by
	 * the interrupt shadow.  The stat.exits increment will do nicely.
	 * But we need to prevent reordering, hence this barrier():
	 */
	barrier();

	kvm_guest_exit();

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

	r = kvm_x86_ops->handle_exit(vcpu);
	return r;

cancel_injection:
	kvm_x86_ops->cancel_injection(vcpu);
	if (unlikely(vcpu->arch.apic_attention))
		kvm_lapic_sync_from_vapic(vcpu);
out:
	return r;
}


static int __vcpu_run(struct kvm_vcpu *vcpu)
{
	int r;
	struct kvm *kvm = vcpu->kvm;

	if (unlikely(vcpu->arch.mp_state == KVM_MP_STATE_SIPI_RECEIVED)) {
		pr_debug("vcpu %d received sipi with vector # %x\n",
			 vcpu->vcpu_id, vcpu->arch.sipi_vector);
		kvm_lapic_reset(vcpu);
		r = kvm_arch_vcpu_reset(vcpu);
		if (r)
			return r;
		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
	}

	vcpu->srcu_idx = srcu_read_lock(&kvm->srcu);
	r = vapic_enter(vcpu);
	if (r) {
		srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);
		return r;
	}

	r = 1;
	while (r > 0) {
		if (vcpu->arch.mp_state == KVM_MP_STATE_RUNNABLE &&
		    !vcpu->arch.apf.halted)
			r = vcpu_enter_guest(vcpu);
		else {
			srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);
			kvm_vcpu_block(vcpu);
			vcpu->srcu_idx = srcu_read_lock(&kvm->srcu);
			if (kvm_check_request(KVM_REQ_UNHALT, vcpu))
			{
				switch(vcpu->arch.mp_state) {
				case KVM_MP_STATE_HALTED:
					vcpu->arch.mp_state =
						KVM_MP_STATE_RUNNABLE;
				case KVM_MP_STATE_RUNNABLE:
					vcpu->arch.apf.halted = false;
					break;
				case KVM_MP_STATE_SIPI_RECEIVED:
				default:
					r = -EINTR;
					break;
				}
			}
		}

		if (r <= 0)
			break;

		clear_bit(KVM_REQ_PENDING_TIMER, &vcpu->requests);
		if (kvm_cpu_has_pending_timer(vcpu))
			kvm_inject_pending_timer_irqs(vcpu);

		if (dm_request_for_irq_injection(vcpu)) {
			r = -EINTR;
			vcpu->run->exit_reason = KVM_EXIT_INTR;
			++vcpu->stat.request_irq_exits;
		}

		kvm_check_async_pf_completion(vcpu);

		if (signal_pending(current)) {
			r = -EINTR;
			vcpu->run->exit_reason = KVM_EXIT_INTR;
			++vcpu->stat.signal_exits;
		}
		if (need_resched()) {
			srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);
			kvm_resched(vcpu);
			vcpu->srcu_idx = srcu_read_lock(&kvm->srcu);
		}
	}

	srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);

	vapic_exit(vcpu);

	return r;
}

static inline int complete_emulated_io(struct kvm_vcpu *vcpu)
{
	int r;
	vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
	r = emulate_instruction(vcpu, EMULTYPE_NO_DECODE);
	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
	if (r != EMULATE_DONE)
		return 0;
	return 1;
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

	if (vcpu->mmio_cur_fragment == vcpu->mmio_nr_fragments) {
		vcpu->mmio_needed = 0;
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


int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	int r;
	sigset_t sigsaved;

	if (!tsk_used_math(current) && init_fpu(current))
		return -ENOMEM;

	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &vcpu->sigset, &sigsaved);

	if (unlikely(vcpu->arch.mp_state == KVM_MP_STATE_UNINITIALIZED)) {
		kvm_vcpu_block(vcpu);
		clear_bit(KVM_REQ_UNHALT, &vcpu->requests);
		r = -EAGAIN;
		goto out;
	}

	/* re-sync apic's tpr */
	if (!irqchip_in_kernel(vcpu->kvm)) {
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

	r = __vcpu_run(vcpu);

out:
	post_kvm_run_save(vcpu);
	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	return r;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	if (vcpu->arch.emulate_regs_need_sync_to_vcpu) {
		/*
		 * We are here if userspace calls get_regs() in the middle of
		 * instruction emulation. Registers state needs to be copied
		 * back from emulation context to vcpu. Userspace shouldn't do
		 * that usually, but some bad designed PV devices (vmware
		 * backdoor interface) need this to work
		 */
		emulator_writeback_register_cache(&vcpu->arch.emulate_ctxt);
		vcpu->arch.emulate_regs_need_sync_to_vcpu = false;
	}
	regs->rax = kvm_register_read(vcpu, VCPU_REGS_RAX);
	regs->rbx = kvm_register_read(vcpu, VCPU_REGS_RBX);
	regs->rcx = kvm_register_read(vcpu, VCPU_REGS_RCX);
	regs->rdx = kvm_register_read(vcpu, VCPU_REGS_RDX);
	regs->rsi = kvm_register_read(vcpu, VCPU_REGS_RSI);
	regs->rdi = kvm_register_read(vcpu, VCPU_REGS_RDI);
	regs->rsp = kvm_register_read(vcpu, VCPU_REGS_RSP);
	regs->rbp = kvm_register_read(vcpu, VCPU_REGS_RBP);
#ifdef CONFIG_X86_64
	regs->r8 = kvm_register_read(vcpu, VCPU_REGS_R8);
	regs->r9 = kvm_register_read(vcpu, VCPU_REGS_R9);
	regs->r10 = kvm_register_read(vcpu, VCPU_REGS_R10);
	regs->r11 = kvm_register_read(vcpu, VCPU_REGS_R11);
	regs->r12 = kvm_register_read(vcpu, VCPU_REGS_R12);
	regs->r13 = kvm_register_read(vcpu, VCPU_REGS_R13);
	regs->r14 = kvm_register_read(vcpu, VCPU_REGS_R14);
	regs->r15 = kvm_register_read(vcpu, VCPU_REGS_R15);
#endif

	regs->rip = kvm_rip_read(vcpu);
	regs->rflags = kvm_get_rflags(vcpu);

	return 0;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	vcpu->arch.emulate_regs_need_sync_from_vcpu = true;
	vcpu->arch.emulate_regs_need_sync_to_vcpu = false;

	kvm_register_write(vcpu, VCPU_REGS_RAX, regs->rax);
	kvm_register_write(vcpu, VCPU_REGS_RBX, regs->rbx);
	kvm_register_write(vcpu, VCPU_REGS_RCX, regs->rcx);
	kvm_register_write(vcpu, VCPU_REGS_RDX, regs->rdx);
	kvm_register_write(vcpu, VCPU_REGS_RSI, regs->rsi);
	kvm_register_write(vcpu, VCPU_REGS_RDI, regs->rdi);
	kvm_register_write(vcpu, VCPU_REGS_RSP, regs->rsp);
	kvm_register_write(vcpu, VCPU_REGS_RBP, regs->rbp);
#ifdef CONFIG_X86_64
	kvm_register_write(vcpu, VCPU_REGS_R8, regs->r8);
	kvm_register_write(vcpu, VCPU_REGS_R9, regs->r9);
	kvm_register_write(vcpu, VCPU_REGS_R10, regs->r10);
	kvm_register_write(vcpu, VCPU_REGS_R11, regs->r11);
	kvm_register_write(vcpu, VCPU_REGS_R12, regs->r12);
	kvm_register_write(vcpu, VCPU_REGS_R13, regs->r13);
	kvm_register_write(vcpu, VCPU_REGS_R14, regs->r14);
	kvm_register_write(vcpu, VCPU_REGS_R15, regs->r15);
#endif

	kvm_rip_write(vcpu, regs->rip);
	kvm_set_rflags(vcpu, regs->rflags);

	vcpu->arch.exception.pending = false;

	kvm_make_request(KVM_REQ_EVENT, vcpu);

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

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	struct desc_ptr dt;

	kvm_get_segment(vcpu, &sregs->cs, VCPU_SREG_CS);
	kvm_get_segment(vcpu, &sregs->ds, VCPU_SREG_DS);
	kvm_get_segment(vcpu, &sregs->es, VCPU_SREG_ES);
	kvm_get_segment(vcpu, &sregs->fs, VCPU_SREG_FS);
	kvm_get_segment(vcpu, &sregs->gs, VCPU_SREG_GS);
	kvm_get_segment(vcpu, &sregs->ss, VCPU_SREG_SS);

	kvm_get_segment(vcpu, &sregs->tr, VCPU_SREG_TR);
	kvm_get_segment(vcpu, &sregs->ldt, VCPU_SREG_LDTR);

	kvm_x86_ops->get_idt(vcpu, &dt);
	sregs->idt.limit = dt.size;
	sregs->idt.base = dt.address;
	kvm_x86_ops->get_gdt(vcpu, &dt);
	sregs->gdt.limit = dt.size;
	sregs->gdt.base = dt.address;

	sregs->cr0 = kvm_read_cr0(vcpu);
	sregs->cr2 = vcpu->arch.cr2;
	sregs->cr3 = kvm_read_cr3(vcpu);
	sregs->cr4 = kvm_read_cr4(vcpu);
	sregs->cr8 = kvm_get_cr8(vcpu);
	sregs->efer = vcpu->arch.efer;
	sregs->apic_base = kvm_get_apic_base(vcpu);

	memset(sregs->interrupt_bitmap, 0, sizeof sregs->interrupt_bitmap);

	if (vcpu->arch.interrupt.pending && !vcpu->arch.interrupt.soft)
		set_bit(vcpu->arch.interrupt.nr,
			(unsigned long *)sregs->interrupt_bitmap);

	return 0;
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	mp_state->mp_state = vcpu->arch.mp_state;
	return 0;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	vcpu->arch.mp_state = mp_state->mp_state;
	kvm_make_request(KVM_REQ_EVENT, vcpu);
	return 0;
}

int kvm_task_switch(struct kvm_vcpu *vcpu, u16 tss_selector, int idt_index,
		    int reason, bool has_error_code, u32 error_code)
{
	struct x86_emulate_ctxt *ctxt = &vcpu->arch.emulate_ctxt;
	int ret;

	init_emulate_ctxt(vcpu);

	ret = emulator_task_switch(ctxt, tss_selector, idt_index, reason,
				   has_error_code, error_code);

	if (ret)
		return EMULATE_FAIL;

	kvm_rip_write(vcpu, ctxt->eip);
	kvm_set_rflags(vcpu, ctxt->eflags);
	kvm_make_request(KVM_REQ_EVENT, vcpu);
	return EMULATE_DONE;
}
EXPORT_SYMBOL_GPL(kvm_task_switch);

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	int mmu_reset_needed = 0;
	int pending_vec, max_bits, idx;
	struct desc_ptr dt;

	if (!guest_cpuid_has_xsave(vcpu) && (sregs->cr4 & X86_CR4_OSXSAVE))
		return -EINVAL;

	dt.size = sregs->idt.limit;
	dt.address = sregs->idt.base;
	kvm_x86_ops->set_idt(vcpu, &dt);
	dt.size = sregs->gdt.limit;
	dt.address = sregs->gdt.base;
	kvm_x86_ops->set_gdt(vcpu, &dt);

	vcpu->arch.cr2 = sregs->cr2;
	mmu_reset_needed |= kvm_read_cr3(vcpu) != sregs->cr3;
	vcpu->arch.cr3 = sregs->cr3;
	__set_bit(VCPU_EXREG_CR3, (ulong *)&vcpu->arch.regs_avail);

	kvm_set_cr8(vcpu, sregs->cr8);

	mmu_reset_needed |= vcpu->arch.efer != sregs->efer;
	kvm_x86_ops->set_efer(vcpu, sregs->efer);
	kvm_set_apic_base(vcpu, sregs->apic_base);

	mmu_reset_needed |= kvm_read_cr0(vcpu) != sregs->cr0;
	kvm_x86_ops->set_cr0(vcpu, sregs->cr0);
	vcpu->arch.cr0 = sregs->cr0;

	mmu_reset_needed |= kvm_read_cr4(vcpu) != sregs->cr4;
	kvm_x86_ops->set_cr4(vcpu, sregs->cr4);
	if (sregs->cr4 & X86_CR4_OSXSAVE)
		kvm_update_cpuid(vcpu);

	idx = srcu_read_lock(&vcpu->kvm->srcu);
	if (!is_long_mode(vcpu) && is_pae(vcpu)) {
		load_pdptrs(vcpu, vcpu->arch.walk_mmu, kvm_read_cr3(vcpu));
		mmu_reset_needed = 1;
	}
	srcu_read_unlock(&vcpu->kvm->srcu, idx);

	if (mmu_reset_needed)
		kvm_mmu_reset_context(vcpu);

	max_bits = KVM_NR_INTERRUPTS;
	pending_vec = find_first_bit(
		(const unsigned long *)sregs->interrupt_bitmap, max_bits);
	if (pending_vec < max_bits) {
		kvm_queue_interrupt(vcpu, pending_vec, false);
		pr_debug("Set back pending irq %d\n", pending_vec);
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

	kvm_make_request(KVM_REQ_EVENT, vcpu);

	return 0;
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	unsigned long rflags;
	int i, r;

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

	kvm_x86_ops->update_db_bp_intercept(vcpu);

	r = 0;

out:

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

	idx = srcu_read_lock(&vcpu->kvm->srcu);
	gpa = kvm_mmu_gva_to_gpa_system(vcpu, vaddr, NULL);
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	tr->physical_address = gpa;
	tr->valid = gpa != UNMAPPED_GVA;
	tr->writeable = 1;
	tr->usermode = 0;

	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	struct i387_fxsave_struct *fxsave =
			&vcpu->arch.guest_fpu.state->fxsave;

	memcpy(fpu->fpr, fxsave->st_space, 128);
	fpu->fcw = fxsave->cwd;
	fpu->fsw = fxsave->swd;
	fpu->ftwx = fxsave->twd;
	fpu->last_opcode = fxsave->fop;
	fpu->last_ip = fxsave->rip;
	fpu->last_dp = fxsave->rdp;
	memcpy(fpu->xmm, fxsave->xmm_space, sizeof fxsave->xmm_space);

	return 0;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	struct i387_fxsave_struct *fxsave =
			&vcpu->arch.guest_fpu.state->fxsave;

	memcpy(fxsave->st_space, fpu->fpr, 128);
	fxsave->cwd = fpu->fcw;
	fxsave->swd = fpu->fsw;
	fxsave->twd = fpu->ftwx;
	fxsave->fop = fpu->last_opcode;
	fxsave->rip = fpu->last_ip;
	fxsave->rdp = fpu->last_dp;
	memcpy(fxsave->xmm_space, fpu->xmm, sizeof fxsave->xmm_space);

	return 0;
}

int fx_init(struct kvm_vcpu *vcpu)
{
	int err;

	err = fpu_alloc(&vcpu->arch.guest_fpu);
	if (err)
		return err;

	fpu_finit(&vcpu->arch.guest_fpu);

	/*
	 * Ensure guest xcr0 is valid for loading
	 */
	vcpu->arch.xcr0 = XSTATE_FP;

	vcpu->arch.cr0 |= X86_CR0_ET;

	return 0;
}
EXPORT_SYMBOL_GPL(fx_init);

static void fx_free(struct kvm_vcpu *vcpu)
{
	fpu_free(&vcpu->arch.guest_fpu);
}

void kvm_load_guest_fpu(struct kvm_vcpu *vcpu)
{
	if (vcpu->guest_fpu_loaded)
		return;

	/*
	 * Restore all possible states in the guest,
	 * and assume host would use all available bits.
	 * Guest xcr0 would be loaded later.
	 */
	kvm_put_guest_xcr0(vcpu);
	vcpu->guest_fpu_loaded = 1;
	__kernel_fpu_begin();
	fpu_restore_checking(&vcpu->arch.guest_fpu);
	trace_kvm_fpu(1);
}

void kvm_put_guest_fpu(struct kvm_vcpu *vcpu)
{
	kvm_put_guest_xcr0(vcpu);

	if (!vcpu->guest_fpu_loaded)
		return;

	vcpu->guest_fpu_loaded = 0;
	fpu_save_init(&vcpu->arch.guest_fpu);
	__kernel_fpu_end();
	++vcpu->stat.fpu_reload;
	kvm_make_request(KVM_REQ_DEACTIVATE_FPU, vcpu);
	trace_kvm_fpu(0);
}

void kvm_arch_vcpu_free(struct kvm_vcpu *vcpu)
{
	kvmclock_reset(vcpu);

	free_cpumask_var(vcpu->arch.wbinvd_dirty_mask);
	fx_free(vcpu);
	kvm_x86_ops->vcpu_free(vcpu);
}

struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm,
						unsigned int id)
{
	if (check_tsc_unstable() && atomic_read(&kvm->online_vcpus) != 0)
		printk_once(KERN_WARNING
		"kvm: SMP vm created on host with unstable TSC; "
		"guest TSC will not be reliable\n");
	return kvm_x86_ops->vcpu_create(kvm, id);
}

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	int r;

	vcpu->arch.mtrr_state.have_fixed = 1;
	r = vcpu_load(vcpu);
	if (r)
		return r;
	r = kvm_arch_vcpu_reset(vcpu);
	if (r == 0)
		r = kvm_mmu_setup(vcpu);
	vcpu_put(vcpu);

	return r;
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	int r;
	vcpu->arch.apf.msr_val = 0;

	r = vcpu_load(vcpu);
	BUG_ON(r);
	kvm_mmu_unload(vcpu);
	vcpu_put(vcpu);

	fx_free(vcpu);
	kvm_x86_ops->vcpu_free(vcpu);
}

int kvm_arch_vcpu_reset(struct kvm_vcpu *vcpu)
{
	atomic_set(&vcpu->arch.nmi_queued, 0);
	vcpu->arch.nmi_pending = 0;
	vcpu->arch.nmi_injected = false;

	memset(vcpu->arch.db, 0, sizeof(vcpu->arch.db));
	vcpu->arch.dr6 = DR6_FIXED_1;
	vcpu->arch.dr7 = DR7_FIXED_1;
	kvm_update_dr7(vcpu);

	kvm_make_request(KVM_REQ_EVENT, vcpu);
	vcpu->arch.apf.msr_val = 0;
	vcpu->arch.st.msr_val = 0;

	kvmclock_reset(vcpu);

	kvm_clear_async_pf_completion_queue(vcpu);
	kvm_async_pf_hash_reset(vcpu);
	vcpu->arch.apf.halted = false;

	kvm_pmu_reset(vcpu);

	return kvm_x86_ops->vcpu_reset(vcpu);
}

int kvm_arch_hardware_enable(void *garbage)
{
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
	int i;
	int ret;
	u64 local_tsc;
	u64 max_tsc = 0;
	bool stable, backwards_tsc = false;

	kvm_shared_msr_cpu_online();
	ret = kvm_x86_ops->hardware_enable(garbage);
	if (ret != 0)
		return ret;

	local_tsc = native_read_tsc();
	stable = !check_tsc_unstable();
	list_for_each_entry(kvm, &vm_list, vm_list) {
		kvm_for_each_vcpu(i, vcpu, kvm) {
			if (!stable && vcpu->cpu == smp_processor_id())
				set_bit(KVM_REQ_CLOCK_UPDATE, &vcpu->requests);
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
	 * elapsed; our helper function, get_kernel_ns() will be using boot
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
			kvm_for_each_vcpu(i, vcpu, kvm) {
				vcpu->arch.tsc_offset_adjustment += delta_cyc;
				vcpu->arch.last_host_tsc = local_tsc;
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

void kvm_arch_hardware_disable(void *garbage)
{
	kvm_x86_ops->hardware_disable(garbage);
	drop_user_return_notifiers(garbage);
}

int kvm_arch_hardware_setup(void)
{
	return kvm_x86_ops->hardware_setup();
}

void kvm_arch_hardware_unsetup(void)
{
	kvm_x86_ops->hardware_unsetup();
}

void kvm_arch_check_processor_compat(void *rtn)
{
	kvm_x86_ops->check_processor_compatibility(rtn);
}

bool kvm_vcpu_compatible(struct kvm_vcpu *vcpu)
{
	return irqchip_in_kernel(vcpu->kvm) == (vcpu->arch.apic != NULL);
}

struct static_key kvm_no_apic_vcpu __read_mostly;

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
	struct page *page;
	struct kvm *kvm;
	int r;

	BUG_ON(vcpu->kvm == NULL);
	kvm = vcpu->kvm;

	vcpu->arch.emulate_ctxt.ops = &emulate_ops;
	if (!irqchip_in_kernel(kvm) || kvm_vcpu_is_bsp(vcpu))
		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
	else
		vcpu->arch.mp_state = KVM_MP_STATE_UNINITIALIZED;

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page) {
		r = -ENOMEM;
		goto fail;
	}
	vcpu->arch.pio_data = page_address(page);

	kvm_set_tsc_khz(vcpu, max_tsc_khz);

	r = kvm_mmu_create(vcpu);
	if (r < 0)
		goto fail_free_pio_data;

	if (irqchip_in_kernel(kvm)) {
		r = kvm_create_lapic(vcpu);
		if (r < 0)
			goto fail_mmu_destroy;
	} else
		static_key_slow_inc(&kvm_no_apic_vcpu);

	vcpu->arch.mce_banks = kzalloc(KVM_MAX_MCE_BANKS * sizeof(u64) * 4,
				       GFP_KERNEL);
	if (!vcpu->arch.mce_banks) {
		r = -ENOMEM;
		goto fail_free_lapic;
	}
	vcpu->arch.mcg_cap = KVM_MAX_MCE_BANKS;

	if (!zalloc_cpumask_var(&vcpu->arch.wbinvd_dirty_mask, GFP_KERNEL))
		goto fail_free_mce_banks;

	kvm_async_pf_hash_reset(vcpu);
	kvm_pmu_init(vcpu);

	return 0;
fail_free_mce_banks:
	kfree(vcpu->arch.mce_banks);
fail_free_lapic:
	kvm_free_lapic(vcpu);
fail_mmu_destroy:
	kvm_mmu_destroy(vcpu);
fail_free_pio_data:
	free_page((unsigned long)vcpu->arch.pio_data);
fail:
	return r;
}

void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu)
{
	int idx;

	kvm_pmu_destroy(vcpu);
	kfree(vcpu->arch.mce_banks);
	kvm_free_lapic(vcpu);
	idx = srcu_read_lock(&vcpu->kvm->srcu);
	kvm_mmu_destroy(vcpu);
	srcu_read_unlock(&vcpu->kvm->srcu, idx);
	free_page((unsigned long)vcpu->arch.pio_data);
	if (!irqchip_in_kernel(vcpu->kvm))
		static_key_slow_dec(&kvm_no_apic_vcpu);
}

int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	if (type)
		return -EINVAL;

	INIT_LIST_HEAD(&kvm->arch.active_mmu_pages);
	INIT_LIST_HEAD(&kvm->arch.assigned_dev_head);

	/* Reserve bit 0 of irq_sources_bitmap for userspace irq source */
	set_bit(KVM_USERSPACE_IRQ_SOURCE_ID, &kvm->arch.irq_sources_bitmap);
	/* Reserve bit 1 of irq_sources_bitmap for irqfd-resampler */
	set_bit(KVM_IRQFD_RESAMPLE_IRQ_SOURCE_ID,
		&kvm->arch.irq_sources_bitmap);

	raw_spin_lock_init(&kvm->arch.tsc_write_lock);
	mutex_init(&kvm->arch.apic_map_lock);

	return 0;
}

static void kvm_unload_vcpu_mmu(struct kvm_vcpu *vcpu)
{
	int r;
	r = vcpu_load(vcpu);
	BUG_ON(r);
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
		kvm_arch_vcpu_free(vcpu);

	mutex_lock(&kvm->lock);
	for (i = 0; i < atomic_read(&kvm->online_vcpus); i++)
		kvm->vcpus[i] = NULL;

	atomic_set(&kvm->online_vcpus, 0);
	mutex_unlock(&kvm->lock);
}

void kvm_arch_sync_events(struct kvm *kvm)
{
	kvm_free_all_assigned_devices(kvm);
	kvm_free_pit(kvm);
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	kvm_iommu_unmap_guest(kvm);
	kfree(kvm->arch.vpic);
	kfree(kvm->arch.vioapic);
	kvm_free_vcpus(kvm);
	if (kvm->arch.apic_access_page)
		put_page(kvm->arch.apic_access_page);
	if (kvm->arch.ept_identity_pagetable)
		put_page(kvm->arch.ept_identity_pagetable);
	kfree(rcu_dereference_check(kvm->arch.apic_map, 1));
}

void kvm_arch_free_memslot(struct kvm_memory_slot *free,
			   struct kvm_memory_slot *dont)
{
	int i;

	for (i = 0; i < KVM_NR_PAGE_SIZES; ++i) {
		if (!dont || free->arch.rmap[i] != dont->arch.rmap[i]) {
			kvm_kvfree(free->arch.rmap[i]);
			free->arch.rmap[i] = NULL;
		}
		if (i == 0)
			continue;

		if (!dont || free->arch.lpage_info[i - 1] !=
			     dont->arch.lpage_info[i - 1]) {
			kvm_kvfree(free->arch.lpage_info[i - 1]);
			free->arch.lpage_info[i - 1] = NULL;
		}
	}
}

int kvm_arch_create_memslot(struct kvm_memory_slot *slot, unsigned long npages)
{
	int i;

	for (i = 0; i < KVM_NR_PAGE_SIZES; ++i) {
		unsigned long ugfn;
		int lpages;
		int level = i + 1;

		lpages = gfn_to_index(slot->base_gfn + npages - 1,
				      slot->base_gfn, level) + 1;

		slot->arch.rmap[i] =
			kvm_kvzalloc(lpages * sizeof(*slot->arch.rmap[i]));
		if (!slot->arch.rmap[i])
			goto out_free;
		if (i == 0)
			continue;

		slot->arch.lpage_info[i - 1] = kvm_kvzalloc(lpages *
					sizeof(*slot->arch.lpage_info[i - 1]));
		if (!slot->arch.lpage_info[i - 1])
			goto out_free;

		if (slot->base_gfn & (KVM_PAGES_PER_HPAGE(level) - 1))
			slot->arch.lpage_info[i - 1][0].write_count = 1;
		if ((slot->base_gfn + npages) & (KVM_PAGES_PER_HPAGE(level) - 1))
			slot->arch.lpage_info[i - 1][lpages - 1].write_count = 1;
		ugfn = slot->userspace_addr >> PAGE_SHIFT;
		/*
		 * If the gfn and userspace address are not aligned wrt each
		 * other, or if explicitly asked to, disable large page
		 * support for this slot
		 */
		if ((slot->base_gfn ^ ugfn) & (KVM_PAGES_PER_HPAGE(level) - 1) ||
		    !kvm_largepages_enabled()) {
			unsigned long j;

			for (j = 0; j < lpages; ++j)
				slot->arch.lpage_info[i - 1][j].write_count = 1;
		}
	}

	return 0;

out_free:
	for (i = 0; i < KVM_NR_PAGE_SIZES; ++i) {
		kvm_kvfree(slot->arch.rmap[i]);
		slot->arch.rmap[i] = NULL;
		if (i == 0)
			continue;

		kvm_kvfree(slot->arch.lpage_info[i - 1]);
		slot->arch.lpage_info[i - 1] = NULL;
	}
	return -ENOMEM;
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *memslot,
				struct kvm_memory_slot old,
				struct kvm_userspace_memory_region *mem,
				int user_alloc)
{
	int npages = memslot->npages;
	int map_flags = MAP_PRIVATE | MAP_ANONYMOUS;

	/* Prevent internal slot pages from being moved by fork()/COW. */
	if (memslot->id >= KVM_MEMORY_SLOTS)
		map_flags = MAP_SHARED | MAP_ANONYMOUS;

	/*To keep backward compatibility with older userspace,
	 *x86 needs to handle !user_alloc case.
	 */
	if (!user_alloc) {
		if (npages && !old.npages) {
			unsigned long userspace_addr;

			userspace_addr = vm_mmap(NULL, 0,
						 npages * PAGE_SIZE,
						 PROT_READ | PROT_WRITE,
						 map_flags,
						 0);

			if (IS_ERR((void *)userspace_addr))
				return PTR_ERR((void *)userspace_addr);

			memslot->userspace_addr = userspace_addr;
		}
	}


	return 0;
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem,
				struct kvm_memory_slot old,
				int user_alloc)
{

	int nr_mmu_pages = 0, npages = mem->memory_size >> PAGE_SHIFT;

	if (!user_alloc && !old.user_alloc && old.npages && !npages) {
		int ret;

		ret = vm_munmap(old.userspace_addr,
				old.npages * PAGE_SIZE);
		if (ret < 0)
			printk(KERN_WARNING
			       "kvm_vm_ioctl_set_memory_region: "
			       "failed to munmap memory\n");
	}

	if (!kvm->arch.n_requested_mmu_pages)
		nr_mmu_pages = kvm_mmu_calculate_mmu_pages(kvm);

	spin_lock(&kvm->mmu_lock);
	if (nr_mmu_pages)
		kvm_mmu_change_mmu_pages(kvm, nr_mmu_pages);
	kvm_mmu_slot_remove_write_access(kvm, mem->slot);
	spin_unlock(&kvm->mmu_lock);
	/*
	 * If memory slot is created, or moved, we need to clear all
	 * mmio sptes.
	 */
	if (npages && old.base_gfn != mem->guest_phys_addr >> PAGE_SHIFT) {
		kvm_mmu_zap_all(kvm);
		kvm_reload_remote_mmus(kvm);
	}
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	kvm_mmu_zap_all(kvm);
	kvm_reload_remote_mmus(kvm);
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
	kvm_arch_flush_shadow_all(kvm);
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.mp_state == KVM_MP_STATE_RUNNABLE &&
		!vcpu->arch.apf.halted)
		|| !list_empty_careful(&vcpu->async_pf.done)
		|| vcpu->arch.mp_state == KVM_MP_STATE_SIPI_RECEIVED
		|| atomic_read(&vcpu->arch.nmi_queued) ||
		(kvm_arch_interrupt_allowed(vcpu) &&
		 kvm_cpu_has_interrupt(vcpu));
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_exiting_guest_mode(vcpu) == IN_GUEST_MODE;
}

int kvm_arch_interrupt_allowed(struct kvm_vcpu *vcpu)
{
	return kvm_x86_ops->interrupt_allowed(vcpu);
}

bool kvm_is_linear_rip(struct kvm_vcpu *vcpu, unsigned long linear_rip)
{
	unsigned long current_rip = kvm_rip_read(vcpu) +
		get_segment_base(vcpu, VCPU_SREG_CS);

	return current_rip == linear_rip;
}
EXPORT_SYMBOL_GPL(kvm_is_linear_rip);

unsigned long kvm_get_rflags(struct kvm_vcpu *vcpu)
{
	unsigned long rflags;

	rflags = kvm_x86_ops->get_rflags(vcpu);
	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)
		rflags &= ~X86_EFLAGS_TF;
	return rflags;
}
EXPORT_SYMBOL_GPL(kvm_get_rflags);

void kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP &&
	    kvm_is_linear_rip(vcpu, vcpu->arch.singlestep_rip))
		rflags |= X86_EFLAGS_TF;
	kvm_x86_ops->set_rflags(vcpu, rflags);
	kvm_make_request(KVM_REQ_EVENT, vcpu);
}
EXPORT_SYMBOL_GPL(kvm_set_rflags);

void kvm_arch_async_page_ready(struct kvm_vcpu *vcpu, struct kvm_async_pf *work)
{
	int r;

	if ((vcpu->arch.mmu.direct_map != work->arch.direct_map) ||
	      is_error_page(work->page))
		return;

	r = kvm_mmu_reload(vcpu);
	if (unlikely(r))
		return;

	if (!vcpu->arch.mmu.direct_map &&
	      work->arch.cr3 != vcpu->arch.mmu.get_cr3(vcpu))
		return;

	vcpu->arch.mmu.page_fault(vcpu, work->gva, 0, true);
}

static inline u32 kvm_async_pf_hash_fn(gfn_t gfn)
{
	return hash_32(gfn & 0xffffffff, order_base_2(ASYNC_PF_PER_VCPU));
}

static inline u32 kvm_async_pf_next_probe(u32 key)
{
	return (key + 1) & (roundup_pow_of_two(ASYNC_PF_PER_VCPU) - 1);
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

	for (i = 0; i < roundup_pow_of_two(ASYNC_PF_PER_VCPU) &&
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

static int apf_put_user(struct kvm_vcpu *vcpu, u32 val)
{

	return kvm_write_guest_cached(vcpu->kvm, &vcpu->arch.apf.data, &val,
				      sizeof(val));
}

void kvm_arch_async_page_not_present(struct kvm_vcpu *vcpu,
				     struct kvm_async_pf *work)
{
	struct x86_exception fault;

	trace_kvm_async_pf_not_present(work->arch.token, work->gva);
	kvm_add_async_pf_gfn(vcpu, work->arch.gfn);

	if (!(vcpu->arch.apf.msr_val & KVM_ASYNC_PF_ENABLED) ||
	    (vcpu->arch.apf.send_user_only &&
	     kvm_x86_ops->get_cpl(vcpu) == 0))
		kvm_make_request(KVM_REQ_APF_HALT, vcpu);
	else if (!apf_put_user(vcpu, KVM_PV_REASON_PAGE_NOT_PRESENT)) {
		fault.vector = PF_VECTOR;
		fault.error_code_valid = true;
		fault.error_code = 0;
		fault.nested_page_fault = false;
		fault.address = work->arch.token;
		kvm_inject_page_fault(vcpu, &fault);
	}
}

void kvm_arch_async_page_present(struct kvm_vcpu *vcpu,
				 struct kvm_async_pf *work)
{
	struct x86_exception fault;

	trace_kvm_async_pf_ready(work->arch.token, work->gva);
	if (is_error_page(work->page))
		work->arch.token = ~0; /* broadcast wakeup */
	else
		kvm_del_async_pf_gfn(vcpu, work->arch.gfn);

	if ((vcpu->arch.apf.msr_val & KVM_ASYNC_PF_ENABLED) &&
	    !apf_put_user(vcpu, KVM_PV_REASON_PAGE_READY)) {
		fault.vector = PF_VECTOR;
		fault.error_code_valid = true;
		fault.error_code = 0;
		fault.nested_page_fault = false;
		fault.address = work->arch.token;
		kvm_inject_page_fault(vcpu, &fault);
	}
	vcpu->arch.apf.halted = false;
	vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
}

bool kvm_arch_can_inject_async_page_present(struct kvm_vcpu *vcpu)
{
	if (!(vcpu->arch.apf.msr_val & KVM_ASYNC_PF_ENABLED))
		return true;
	else
		return !kvm_event_needs_reinjection(vcpu) &&
			kvm_x86_ops->interrupt_allowed(vcpu);
}

EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_exit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_inj_virq);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_page_fault);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_msr);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_cr);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_vmrun);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_vmexit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_vmexit_inject);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_intr_vmexit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_invlpga);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_skinit);
EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_nested_intercepts);

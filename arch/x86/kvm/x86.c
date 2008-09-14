/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * derived from drivers/kvm/kvm_main.c
 *
 * Copyright (C) 2006 Qumranet, Inc.
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
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

#include <linux/clocksource.h>
#include <linux/kvm.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/mman.h>
#include <linux/highmem.h>

#include <asm/uaccess.h>
#include <asm/msr.h>
#include <asm/desc.h>

#define MAX_IO_MSRS 256
#define CR0_RESERVED_BITS						\
	(~(unsigned long)(X86_CR0_PE | X86_CR0_MP | X86_CR0_EM | X86_CR0_TS \
			  | X86_CR0_ET | X86_CR0_NE | X86_CR0_WP | X86_CR0_AM \
			  | X86_CR0_NW | X86_CR0_CD | X86_CR0_PG))
#define CR4_RESERVED_BITS						\
	(~(unsigned long)(X86_CR4_VME | X86_CR4_PVI | X86_CR4_TSD | X86_CR4_DE\
			  | X86_CR4_PSE | X86_CR4_PAE | X86_CR4_MCE	\
			  | X86_CR4_PGE | X86_CR4_PCE | X86_CR4_OSFXSR	\
			  | X86_CR4_OSXMMEXCPT | X86_CR4_VMXE))

#define CR8_RESERVED_BITS (~(unsigned long)X86_CR8_TPR)
/* EFER defaults:
 * - enable syscall per default because its emulated by KVM
 * - enable LME and LMA per default on 64 bit KVM
 */
#ifdef CONFIG_X86_64
static u64 __read_mostly efer_reserved_bits = 0xfffffffffffffafeULL;
#else
static u64 __read_mostly efer_reserved_bits = 0xfffffffffffffffeULL;
#endif

#define VM_STAT(x) offsetof(struct kvm, stat.x), KVM_STAT_VM
#define VCPU_STAT(x) offsetof(struct kvm_vcpu, stat.x), KVM_STAT_VCPU

static int kvm_dev_ioctl_get_supported_cpuid(struct kvm_cpuid2 *cpuid,
				    struct kvm_cpuid_entry2 __user *entries);

struct kvm_x86_ops *kvm_x86_ops;

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
	{ "mmu_shadow_zapped", VM_STAT(mmu_shadow_zapped) },
	{ "mmu_pte_write", VM_STAT(mmu_pte_write) },
	{ "mmu_pte_updated", VM_STAT(mmu_pte_updated) },
	{ "mmu_pde_zapped", VM_STAT(mmu_pde_zapped) },
	{ "mmu_flooded", VM_STAT(mmu_flooded) },
	{ "mmu_recycled", VM_STAT(mmu_recycled) },
	{ "mmu_cache_miss", VM_STAT(mmu_cache_miss) },
	{ "remote_tlb_flush", VM_STAT(remote_tlb_flush) },
	{ "largepages", VM_STAT(lpages) },
	{ NULL }
};


unsigned long segment_base(u16 selector)
{
	struct descriptor_table gdt;
	struct desc_struct *d;
	unsigned long table_base;
	unsigned long v;

	if (selector == 0)
		return 0;

	asm("sgdt %0" : "=m"(gdt));
	table_base = gdt.base;

	if (selector & 4) {           /* from ldt */
		u16 ldt_selector;

		asm("sldt %0" : "=g"(ldt_selector));
		table_base = segment_base(ldt_selector);
	}
	d = (struct desc_struct *)(table_base + (selector & ~7));
	v = d->base0 | ((unsigned long)d->base1 << 16) |
		((unsigned long)d->base2 << 24);
#ifdef CONFIG_X86_64
	if (d->s == 0 && (d->type == 2 || d->type == 9 || d->type == 11))
		v |= ((unsigned long)((struct ldttss_desc64 *)d)->base3) << 32;
#endif
	return v;
}
EXPORT_SYMBOL_GPL(segment_base);

u64 kvm_get_apic_base(struct kvm_vcpu *vcpu)
{
	if (irqchip_in_kernel(vcpu->kvm))
		return vcpu->arch.apic_base;
	else
		return vcpu->arch.apic_base;
}
EXPORT_SYMBOL_GPL(kvm_get_apic_base);

void kvm_set_apic_base(struct kvm_vcpu *vcpu, u64 data)
{
	/* TODO: reserve bits check */
	if (irqchip_in_kernel(vcpu->kvm))
		kvm_lapic_set_base(vcpu, data);
	else
		vcpu->arch.apic_base = data;
}
EXPORT_SYMBOL_GPL(kvm_set_apic_base);

void kvm_queue_exception(struct kvm_vcpu *vcpu, unsigned nr)
{
	WARN_ON(vcpu->arch.exception.pending);
	vcpu->arch.exception.pending = true;
	vcpu->arch.exception.has_error_code = false;
	vcpu->arch.exception.nr = nr;
}
EXPORT_SYMBOL_GPL(kvm_queue_exception);

void kvm_inject_page_fault(struct kvm_vcpu *vcpu, unsigned long addr,
			   u32 error_code)
{
	++vcpu->stat.pf_guest;
	if (vcpu->arch.exception.pending) {
		if (vcpu->arch.exception.nr == PF_VECTOR) {
			printk(KERN_DEBUG "kvm: inject_page_fault:"
					" double fault 0x%lx\n", addr);
			vcpu->arch.exception.nr = DF_VECTOR;
			vcpu->arch.exception.error_code = 0;
		} else if (vcpu->arch.exception.nr == DF_VECTOR) {
			/* triple fault -> shutdown */
			set_bit(KVM_REQ_TRIPLE_FAULT, &vcpu->requests);
		}
		return;
	}
	vcpu->arch.cr2 = addr;
	kvm_queue_exception_e(vcpu, PF_VECTOR, error_code);
}

void kvm_inject_nmi(struct kvm_vcpu *vcpu)
{
	vcpu->arch.nmi_pending = 1;
}
EXPORT_SYMBOL_GPL(kvm_inject_nmi);

void kvm_queue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code)
{
	WARN_ON(vcpu->arch.exception.pending);
	vcpu->arch.exception.pending = true;
	vcpu->arch.exception.has_error_code = true;
	vcpu->arch.exception.nr = nr;
	vcpu->arch.exception.error_code = error_code;
}
EXPORT_SYMBOL_GPL(kvm_queue_exception_e);

static void __queue_exception(struct kvm_vcpu *vcpu)
{
	kvm_x86_ops->queue_exception(vcpu, vcpu->arch.exception.nr,
				     vcpu->arch.exception.has_error_code,
				     vcpu->arch.exception.error_code);
}

/*
 * Load the pae pdptrs.  Return true is they are all valid.
 */
int load_pdptrs(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	gfn_t pdpt_gfn = cr3 >> PAGE_SHIFT;
	unsigned offset = ((cr3 & (PAGE_SIZE-1)) >> 5) << 2;
	int i;
	int ret;
	u64 pdpte[ARRAY_SIZE(vcpu->arch.pdptrs)];

	ret = kvm_read_guest_page(vcpu->kvm, pdpt_gfn, pdpte,
				  offset * sizeof(u64), sizeof(pdpte));
	if (ret < 0) {
		ret = 0;
		goto out;
	}
	for (i = 0; i < ARRAY_SIZE(pdpte); ++i) {
		if ((pdpte[i] & 1) && (pdpte[i] & 0xfffffff0000001e6ull)) {
			ret = 0;
			goto out;
		}
	}
	ret = 1;

	memcpy(vcpu->arch.pdptrs, pdpte, sizeof(vcpu->arch.pdptrs));
out:

	return ret;
}
EXPORT_SYMBOL_GPL(load_pdptrs);

static bool pdptrs_changed(struct kvm_vcpu *vcpu)
{
	u64 pdpte[ARRAY_SIZE(vcpu->arch.pdptrs)];
	bool changed = true;
	int r;

	if (is_long_mode(vcpu) || !is_pae(vcpu))
		return false;

	r = kvm_read_guest(vcpu->kvm, vcpu->arch.cr3 & ~31u, pdpte, sizeof(pdpte));
	if (r < 0)
		goto out;
	changed = memcmp(pdpte, vcpu->arch.pdptrs, sizeof(pdpte)) != 0;
out:

	return changed;
}

void kvm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	if (cr0 & CR0_RESERVED_BITS) {
		printk(KERN_DEBUG "set_cr0: 0x%lx #GP, reserved bits 0x%lx\n",
		       cr0, vcpu->arch.cr0);
		kvm_inject_gp(vcpu, 0);
		return;
	}

	if ((cr0 & X86_CR0_NW) && !(cr0 & X86_CR0_CD)) {
		printk(KERN_DEBUG "set_cr0: #GP, CD == 0 && NW == 1\n");
		kvm_inject_gp(vcpu, 0);
		return;
	}

	if ((cr0 & X86_CR0_PG) && !(cr0 & X86_CR0_PE)) {
		printk(KERN_DEBUG "set_cr0: #GP, set PG flag "
		       "and a clear PE flag\n");
		kvm_inject_gp(vcpu, 0);
		return;
	}

	if (!is_paging(vcpu) && (cr0 & X86_CR0_PG)) {
#ifdef CONFIG_X86_64
		if ((vcpu->arch.shadow_efer & EFER_LME)) {
			int cs_db, cs_l;

			if (!is_pae(vcpu)) {
				printk(KERN_DEBUG "set_cr0: #GP, start paging "
				       "in long mode while PAE is disabled\n");
				kvm_inject_gp(vcpu, 0);
				return;
			}
			kvm_x86_ops->get_cs_db_l_bits(vcpu, &cs_db, &cs_l);
			if (cs_l) {
				printk(KERN_DEBUG "set_cr0: #GP, start paging "
				       "in long mode while CS.L == 1\n");
				kvm_inject_gp(vcpu, 0);
				return;

			}
		} else
#endif
		if (is_pae(vcpu) && !load_pdptrs(vcpu, vcpu->arch.cr3)) {
			printk(KERN_DEBUG "set_cr0: #GP, pdptrs "
			       "reserved bits\n");
			kvm_inject_gp(vcpu, 0);
			return;
		}

	}

	kvm_x86_ops->set_cr0(vcpu, cr0);
	vcpu->arch.cr0 = cr0;

	kvm_mmu_reset_context(vcpu);
	return;
}
EXPORT_SYMBOL_GPL(kvm_set_cr0);

void kvm_lmsw(struct kvm_vcpu *vcpu, unsigned long msw)
{
	kvm_set_cr0(vcpu, (vcpu->arch.cr0 & ~0x0ful) | (msw & 0x0f));
	KVMTRACE_1D(LMSW, vcpu,
		    (u32)((vcpu->arch.cr0 & ~0x0ful) | (msw & 0x0f)),
		    handler);
}
EXPORT_SYMBOL_GPL(kvm_lmsw);

void kvm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	if (cr4 & CR4_RESERVED_BITS) {
		printk(KERN_DEBUG "set_cr4: #GP, reserved bits\n");
		kvm_inject_gp(vcpu, 0);
		return;
	}

	if (is_long_mode(vcpu)) {
		if (!(cr4 & X86_CR4_PAE)) {
			printk(KERN_DEBUG "set_cr4: #GP, clearing PAE while "
			       "in long mode\n");
			kvm_inject_gp(vcpu, 0);
			return;
		}
	} else if (is_paging(vcpu) && !is_pae(vcpu) && (cr4 & X86_CR4_PAE)
		   && !load_pdptrs(vcpu, vcpu->arch.cr3)) {
		printk(KERN_DEBUG "set_cr4: #GP, pdptrs reserved bits\n");
		kvm_inject_gp(vcpu, 0);
		return;
	}

	if (cr4 & X86_CR4_VMXE) {
		printk(KERN_DEBUG "set_cr4: #GP, setting VMXE\n");
		kvm_inject_gp(vcpu, 0);
		return;
	}
	kvm_x86_ops->set_cr4(vcpu, cr4);
	vcpu->arch.cr4 = cr4;
	kvm_mmu_reset_context(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_set_cr4);

void kvm_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	if (cr3 == vcpu->arch.cr3 && !pdptrs_changed(vcpu)) {
		kvm_mmu_flush_tlb(vcpu);
		return;
	}

	if (is_long_mode(vcpu)) {
		if (cr3 & CR3_L_MODE_RESERVED_BITS) {
			printk(KERN_DEBUG "set_cr3: #GP, reserved bits\n");
			kvm_inject_gp(vcpu, 0);
			return;
		}
	} else {
		if (is_pae(vcpu)) {
			if (cr3 & CR3_PAE_RESERVED_BITS) {
				printk(KERN_DEBUG
				       "set_cr3: #GP, reserved bits\n");
				kvm_inject_gp(vcpu, 0);
				return;
			}
			if (is_paging(vcpu) && !load_pdptrs(vcpu, cr3)) {
				printk(KERN_DEBUG "set_cr3: #GP, pdptrs "
				       "reserved bits\n");
				kvm_inject_gp(vcpu, 0);
				return;
			}
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
		kvm_inject_gp(vcpu, 0);
	else {
		vcpu->arch.cr3 = cr3;
		vcpu->arch.mmu.new_cr3(vcpu);
	}
}
EXPORT_SYMBOL_GPL(kvm_set_cr3);

void kvm_set_cr8(struct kvm_vcpu *vcpu, unsigned long cr8)
{
	if (cr8 & CR8_RESERVED_BITS) {
		printk(KERN_DEBUG "set_cr8: #GP, reserved bits 0x%lx\n", cr8);
		kvm_inject_gp(vcpu, 0);
		return;
	}
	if (irqchip_in_kernel(vcpu->kvm))
		kvm_lapic_set_tpr(vcpu, cr8);
	else
		vcpu->arch.cr8 = cr8;
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

/*
 * List of msr numbers which we expose to userspace through KVM_GET_MSRS
 * and KVM_SET_MSRS, and KVM_GET_MSR_INDEX_LIST.
 *
 * This list is modified at module load time to reflect the
 * capabilities of the host cpu.
 */
static u32 msrs_to_save[] = {
	MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
	MSR_K6_STAR,
#ifdef CONFIG_X86_64
	MSR_CSTAR, MSR_KERNEL_GS_BASE, MSR_SYSCALL_MASK, MSR_LSTAR,
#endif
	MSR_IA32_TIME_STAMP_COUNTER, MSR_KVM_SYSTEM_TIME, MSR_KVM_WALL_CLOCK,
	MSR_IA32_PERF_STATUS,
};

static unsigned num_msrs_to_save;

static u32 emulated_msrs[] = {
	MSR_IA32_MISC_ENABLE,
};

static void set_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	if (efer & efer_reserved_bits) {
		printk(KERN_DEBUG "set_efer: 0x%llx #GP, reserved bits\n",
		       efer);
		kvm_inject_gp(vcpu, 0);
		return;
	}

	if (is_paging(vcpu)
	    && (vcpu->arch.shadow_efer & EFER_LME) != (efer & EFER_LME)) {
		printk(KERN_DEBUG "set_efer: #GP, change LME while paging\n");
		kvm_inject_gp(vcpu, 0);
		return;
	}

	kvm_x86_ops->set_efer(vcpu, efer);

	efer &= ~EFER_LMA;
	efer |= vcpu->arch.shadow_efer & EFER_LMA;

	vcpu->arch.shadow_efer = efer;
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
	static int version;
	struct pvclock_wall_clock wc;
	struct timespec now, sys, boot;

	if (!wall_clock)
		return;

	version++;

	kvm_write_guest(kvm, wall_clock, &version, sizeof(version));

	/*
	 * The guest calculates current wall clock time by adding
	 * system time (updated by kvm_write_guest_time below) to the
	 * wall clock specified here.  guest system time equals host
	 * system time for us, thus we must fill in host boot time here.
	 */
	now = current_kernel_time();
	ktime_get_ts(&sys);
	boot = ns_to_timespec(timespec_to_ns(&now) - timespec_to_ns(&sys));

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

static void kvm_set_time_scale(uint32_t tsc_khz, struct pvclock_vcpu_time_info *hv_clock)
{
	uint64_t nsecs = 1000000000LL;
	int32_t  shift = 0;
	uint64_t tps64;
	uint32_t tps32;

	tps64 = tsc_khz * 1000LL;
	while (tps64 > nsecs*2) {
		tps64 >>= 1;
		shift--;
	}

	tps32 = (uint32_t)tps64;
	while (tps32 <= (uint32_t)nsecs) {
		tps32 <<= 1;
		shift++;
	}

	hv_clock->tsc_shift = shift;
	hv_clock->tsc_to_system_mul = div_frac(nsecs, tps32);

	pr_debug("%s: tsc_khz %u, tsc_shift %d, tsc_mul %u\n",
		 __FUNCTION__, tsc_khz, hv_clock->tsc_shift,
		 hv_clock->tsc_to_system_mul);
}

static void kvm_write_guest_time(struct kvm_vcpu *v)
{
	struct timespec ts;
	unsigned long flags;
	struct kvm_vcpu_arch *vcpu = &v->arch;
	void *shared_kaddr;

	if ((!vcpu->time_page))
		return;

	if (unlikely(vcpu->hv_clock_tsc_khz != tsc_khz)) {
		kvm_set_time_scale(tsc_khz, &vcpu->hv_clock);
		vcpu->hv_clock_tsc_khz = tsc_khz;
	}

	/* Keep irq disabled to prevent changes to the clock */
	local_irq_save(flags);
	kvm_get_msr(v, MSR_IA32_TIME_STAMP_COUNTER,
			  &vcpu->hv_clock.tsc_timestamp);
	ktime_get_ts(&ts);
	local_irq_restore(flags);

	/* With all the info we got, fill in the values */

	vcpu->hv_clock.system_time = ts.tv_nsec +
				     (NSEC_PER_SEC * (u64)ts.tv_sec);
	/*
	 * The interface expects us to write an even number signaling that the
	 * update is finished. Since the guest won't see the intermediate
	 * state, we just increase by 2 at the end.
	 */
	vcpu->hv_clock.version += 2;

	shared_kaddr = kmap_atomic(vcpu->time_page, KM_USER0);

	memcpy(shared_kaddr + vcpu->time_offset, &vcpu->hv_clock,
	       sizeof(vcpu->hv_clock));

	kunmap_atomic(shared_kaddr, KM_USER0);

	mark_page_dirty(v->kvm, vcpu->time >> PAGE_SHIFT);
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

static int set_msr_mtrr(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	if (!msr_mtrr_valid(msr))
		return 1;

	vcpu->arch.mtrr[msr - 0x200] = data;
	return 0;
}

int kvm_set_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	switch (msr) {
	case MSR_EFER:
		set_efer(vcpu, data);
		break;
	case MSR_IA32_MC0_STATUS:
		pr_unimpl(vcpu, "%s: MSR_IA32_MC0_STATUS 0x%llx, nop\n",
		       __func__, data);
		break;
	case MSR_IA32_MCG_STATUS:
		pr_unimpl(vcpu, "%s: MSR_IA32_MCG_STATUS 0x%llx, nop\n",
			__func__, data);
		break;
	case MSR_IA32_MCG_CTL:
		pr_unimpl(vcpu, "%s: MSR_IA32_MCG_CTL 0x%llx, nop\n",
			__func__, data);
		break;
	case MSR_IA32_UCODE_REV:
	case MSR_IA32_UCODE_WRITE:
		break;
	case 0x200 ... 0x2ff:
		return set_msr_mtrr(vcpu, msr, data);
	case MSR_IA32_APICBASE:
		kvm_set_apic_base(vcpu, data);
		break;
	case MSR_IA32_MISC_ENABLE:
		vcpu->arch.ia32_misc_enable_msr = data;
		break;
	case MSR_KVM_WALL_CLOCK:
		vcpu->kvm->arch.wall_clock = data;
		kvm_write_wall_clock(vcpu->kvm, data);
		break;
	case MSR_KVM_SYSTEM_TIME: {
		if (vcpu->arch.time_page) {
			kvm_release_page_dirty(vcpu->arch.time_page);
			vcpu->arch.time_page = NULL;
		}

		vcpu->arch.time = data;

		/* we verify if the enable bit is set... */
		if (!(data & 1))
			break;

		/* ...but clean it before doing the actual write */
		vcpu->arch.time_offset = data & ~(PAGE_MASK | 1);

		down_read(&current->mm->mmap_sem);
		vcpu->arch.time_page =
				gfn_to_page(vcpu->kvm, data >> PAGE_SHIFT);
		up_read(&current->mm->mmap_sem);

		if (is_error_page(vcpu->arch.time_page)) {
			kvm_release_page_clean(vcpu->arch.time_page);
			vcpu->arch.time_page = NULL;
		}

		kvm_write_guest_time(vcpu);
		break;
	}
	default:
		pr_unimpl(vcpu, "unhandled wrmsr: 0x%x data %llx\n", msr, data);
		return 1;
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
	if (!msr_mtrr_valid(msr))
		return 1;

	*pdata = vcpu->arch.mtrr[msr - 0x200];
	return 0;
}

int kvm_get_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	u64 data;

	switch (msr) {
	case 0xc0010010: /* SYSCFG */
	case 0xc0010015: /* HWCR */
	case MSR_IA32_PLATFORM_ID:
	case MSR_IA32_P5_MC_ADDR:
	case MSR_IA32_P5_MC_TYPE:
	case MSR_IA32_MC0_CTL:
	case MSR_IA32_MCG_STATUS:
	case MSR_IA32_MCG_CAP:
	case MSR_IA32_MCG_CTL:
	case MSR_IA32_MC0_MISC:
	case MSR_IA32_MC0_MISC+4:
	case MSR_IA32_MC0_MISC+8:
	case MSR_IA32_MC0_MISC+12:
	case MSR_IA32_MC0_MISC+16:
	case MSR_IA32_UCODE_REV:
	case MSR_IA32_EBL_CR_POWERON:
		data = 0;
		break;
	case MSR_MTRRcap:
		data = 0x500 | KVM_NR_VAR_MTRR;
		break;
	case 0x200 ... 0x2ff:
		return get_msr_mtrr(vcpu, msr, pdata);
	case 0xcd: /* fsb frequency */
		data = 3;
		break;
	case MSR_IA32_APICBASE:
		data = kvm_get_apic_base(vcpu);
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
		data = vcpu->arch.shadow_efer;
		break;
	case MSR_KVM_WALL_CLOCK:
		data = vcpu->kvm->arch.wall_clock;
		break;
	case MSR_KVM_SYSTEM_TIME:
		data = vcpu->arch.time;
		break;
	default:
		pr_unimpl(vcpu, "unhandled rdmsr: 0x%x\n", msr);
		return 1;
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
	int i;

	vcpu_load(vcpu);

	down_read(&vcpu->kvm->slots_lock);
	for (i = 0; i < msrs->nmsrs; ++i)
		if (do_msr(vcpu, entries[i].index, &entries[i].data))
			break;
	up_read(&vcpu->kvm->slots_lock);

	vcpu_put(vcpu);

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

	r = -ENOMEM;
	size = sizeof(struct kvm_msr_entry) * msrs.nmsrs;
	entries = vmalloc(size);
	if (!entries)
		goto out;

	r = -EFAULT;
	if (copy_from_user(entries, user_msrs->entries, size))
		goto out_free;

	r = n = __msr_io(vcpu, &msrs, entries, do_msr);
	if (r < 0)
		goto out_free;

	r = -EFAULT;
	if (writeback && copy_to_user(user_msrs->entries, entries, size))
		goto out_free;

	r = n;

out_free:
	vfree(entries);
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
	case KVM_CAP_USER_MEMORY:
	case KVM_CAP_SET_TSS_ADDR:
	case KVM_CAP_EXT_CPUID:
	case KVM_CAP_CLOCKSOURCE:
	case KVM_CAP_PIT:
	case KVM_CAP_NOP_IO_DELAY:
	case KVM_CAP_MP_STATE:
	case KVM_CAP_SYNC_MMU:
		r = 1;
		break;
	case KVM_CAP_COALESCED_MMIO:
		r = KVM_COALESCED_MMIO_PAGE_OFFSET;
		break;
	case KVM_CAP_VAPIC:
		r = !kvm_x86_ops->cpu_has_accelerated_tpr();
		break;
	case KVM_CAP_NR_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
	case KVM_CAP_NR_MEMSLOTS:
		r = KVM_MEMORY_SLOTS;
		break;
	case KVM_CAP_PV_MMU:
		r = !tdp_enabled;
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
		if (n < num_msrs_to_save)
			goto out;
		r = -EFAULT;
		if (copy_to_user(user_msr_list->indices, &msrs_to_save,
				 num_msrs_to_save * sizeof(u32)))
			goto out;
		if (copy_to_user(user_msr_list->indices
				 + num_msrs_to_save * sizeof(u32),
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
	default:
		r = -EINVAL;
	}
out:
	return r;
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	kvm_x86_ops->vcpu_load(vcpu, cpu);
	kvm_write_guest_time(vcpu);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	kvm_x86_ops->vcpu_put(vcpu);
	kvm_put_guest_fpu(vcpu);
}

static int is_efer_nx(void)
{
	u64 efer;

	rdmsrl(MSR_EFER, efer);
	return efer & EFER_NX;
}

static void cpuid_fix_nx_cap(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_cpuid_entry2 *e, *entry;

	entry = NULL;
	for (i = 0; i < vcpu->arch.cpuid_nent; ++i) {
		e = &vcpu->arch.cpuid_entries[i];
		if (e->function == 0x80000001) {
			entry = e;
			break;
		}
	}
	if (entry && (entry->edx & (1 << 20)) && !is_efer_nx()) {
		entry->edx &= ~(1 << 20);
		printk(KERN_INFO "kvm: guest NX capability removed\n");
	}
}

/* when an old userspace process fills a new kernel module */
static int kvm_vcpu_ioctl_set_cpuid(struct kvm_vcpu *vcpu,
				    struct kvm_cpuid *cpuid,
				    struct kvm_cpuid_entry __user *entries)
{
	int r, i;
	struct kvm_cpuid_entry *cpuid_entries;

	r = -E2BIG;
	if (cpuid->nent > KVM_MAX_CPUID_ENTRIES)
		goto out;
	r = -ENOMEM;
	cpuid_entries = vmalloc(sizeof(struct kvm_cpuid_entry) * cpuid->nent);
	if (!cpuid_entries)
		goto out;
	r = -EFAULT;
	if (copy_from_user(cpuid_entries, entries,
			   cpuid->nent * sizeof(struct kvm_cpuid_entry)))
		goto out_free;
	for (i = 0; i < cpuid->nent; i++) {
		vcpu->arch.cpuid_entries[i].function = cpuid_entries[i].function;
		vcpu->arch.cpuid_entries[i].eax = cpuid_entries[i].eax;
		vcpu->arch.cpuid_entries[i].ebx = cpuid_entries[i].ebx;
		vcpu->arch.cpuid_entries[i].ecx = cpuid_entries[i].ecx;
		vcpu->arch.cpuid_entries[i].edx = cpuid_entries[i].edx;
		vcpu->arch.cpuid_entries[i].index = 0;
		vcpu->arch.cpuid_entries[i].flags = 0;
		vcpu->arch.cpuid_entries[i].padding[0] = 0;
		vcpu->arch.cpuid_entries[i].padding[1] = 0;
		vcpu->arch.cpuid_entries[i].padding[2] = 0;
	}
	vcpu->arch.cpuid_nent = cpuid->nent;
	cpuid_fix_nx_cap(vcpu);
	r = 0;

out_free:
	vfree(cpuid_entries);
out:
	return r;
}

static int kvm_vcpu_ioctl_set_cpuid2(struct kvm_vcpu *vcpu,
				    struct kvm_cpuid2 *cpuid,
				    struct kvm_cpuid_entry2 __user *entries)
{
	int r;

	r = -E2BIG;
	if (cpuid->nent > KVM_MAX_CPUID_ENTRIES)
		goto out;
	r = -EFAULT;
	if (copy_from_user(&vcpu->arch.cpuid_entries, entries,
			   cpuid->nent * sizeof(struct kvm_cpuid_entry2)))
		goto out;
	vcpu->arch.cpuid_nent = cpuid->nent;
	return 0;

out:
	return r;
}

static int kvm_vcpu_ioctl_get_cpuid2(struct kvm_vcpu *vcpu,
				    struct kvm_cpuid2 *cpuid,
				    struct kvm_cpuid_entry2 __user *entries)
{
	int r;

	r = -E2BIG;
	if (cpuid->nent < vcpu->arch.cpuid_nent)
		goto out;
	r = -EFAULT;
	if (copy_to_user(entries, &vcpu->arch.cpuid_entries,
			   vcpu->arch.cpuid_nent * sizeof(struct kvm_cpuid_entry2)))
		goto out;
	return 0;

out:
	cpuid->nent = vcpu->arch.cpuid_nent;
	return r;
}

static inline u32 bit(int bitno)
{
	return 1 << (bitno & 31);
}

static void do_cpuid_1_ent(struct kvm_cpuid_entry2 *entry, u32 function,
			  u32 index)
{
	entry->function = function;
	entry->index = index;
	cpuid_count(entry->function, entry->index,
		&entry->eax, &entry->ebx, &entry->ecx, &entry->edx);
	entry->flags = 0;
}

static void do_cpuid_ent(struct kvm_cpuid_entry2 *entry, u32 function,
			 u32 index, int *nent, int maxnent)
{
	const u32 kvm_supported_word0_x86_features = bit(X86_FEATURE_FPU) |
		bit(X86_FEATURE_VME) | bit(X86_FEATURE_DE) |
		bit(X86_FEATURE_PSE) | bit(X86_FEATURE_TSC) |
		bit(X86_FEATURE_MSR) | bit(X86_FEATURE_PAE) |
		bit(X86_FEATURE_CX8) | bit(X86_FEATURE_APIC) |
		bit(X86_FEATURE_SEP) | bit(X86_FEATURE_PGE) |
		bit(X86_FEATURE_CMOV) | bit(X86_FEATURE_PSE36) |
		bit(X86_FEATURE_CLFLSH) | bit(X86_FEATURE_MMX) |
		bit(X86_FEATURE_FXSR) | bit(X86_FEATURE_XMM) |
		bit(X86_FEATURE_XMM2) | bit(X86_FEATURE_SELFSNOOP);
	const u32 kvm_supported_word1_x86_features = bit(X86_FEATURE_FPU) |
		bit(X86_FEATURE_VME) | bit(X86_FEATURE_DE) |
		bit(X86_FEATURE_PSE) | bit(X86_FEATURE_TSC) |
		bit(X86_FEATURE_MSR) | bit(X86_FEATURE_PAE) |
		bit(X86_FEATURE_CX8) | bit(X86_FEATURE_APIC) |
		bit(X86_FEATURE_PGE) |
		bit(X86_FEATURE_CMOV) | bit(X86_FEATURE_PSE36) |
		bit(X86_FEATURE_MMX) | bit(X86_FEATURE_FXSR) |
		bit(X86_FEATURE_SYSCALL) |
		(bit(X86_FEATURE_NX) && is_efer_nx()) |
#ifdef CONFIG_X86_64
		bit(X86_FEATURE_LM) |
#endif
		bit(X86_FEATURE_MMXEXT) |
		bit(X86_FEATURE_3DNOWEXT) |
		bit(X86_FEATURE_3DNOW);
	const u32 kvm_supported_word3_x86_features =
		bit(X86_FEATURE_XMM3) | bit(X86_FEATURE_CX16);
	const u32 kvm_supported_word6_x86_features =
		bit(X86_FEATURE_LAHF_LM) | bit(X86_FEATURE_CMP_LEGACY);

	/* all func 2 cpuid_count() should be called on the same cpu */
	get_cpu();
	do_cpuid_1_ent(entry, function, index);
	++*nent;

	switch (function) {
	case 0:
		entry->eax = min(entry->eax, (u32)0xb);
		break;
	case 1:
		entry->edx &= kvm_supported_word0_x86_features;
		entry->ecx &= kvm_supported_word3_x86_features;
		break;
	/* function 2 entries are STATEFUL. That is, repeated cpuid commands
	 * may return different values. This forces us to get_cpu() before
	 * issuing the first command, and also to emulate this annoying behavior
	 * in kvm_emulate_cpuid() using KVM_CPUID_FLAG_STATE_READ_NEXT */
	case 2: {
		int t, times = entry->eax & 0xff;

		entry->flags |= KVM_CPUID_FLAG_STATEFUL_FUNC;
		for (t = 1; t < times && *nent < maxnent; ++t) {
			do_cpuid_1_ent(&entry[t], function, 0);
			entry[t].flags |= KVM_CPUID_FLAG_STATEFUL_FUNC;
			++*nent;
		}
		break;
	}
	/* function 4 and 0xb have additional index. */
	case 4: {
		int i, cache_type;

		entry->flags |= KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
		/* read more entries until cache_type is zero */
		for (i = 1; *nent < maxnent; ++i) {
			cache_type = entry[i - 1].eax & 0x1f;
			if (!cache_type)
				break;
			do_cpuid_1_ent(&entry[i], function, i);
			entry[i].flags |=
			       KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
			++*nent;
		}
		break;
	}
	case 0xb: {
		int i, level_type;

		entry->flags |= KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
		/* read more entries until level_type is zero */
		for (i = 1; *nent < maxnent; ++i) {
			level_type = entry[i - 1].ecx & 0xff;
			if (!level_type)
				break;
			do_cpuid_1_ent(&entry[i], function, i);
			entry[i].flags |=
			       KVM_CPUID_FLAG_SIGNIFCANT_INDEX;
			++*nent;
		}
		break;
	}
	case 0x80000000:
		entry->eax = min(entry->eax, 0x8000001a);
		break;
	case 0x80000001:
		entry->edx &= kvm_supported_word1_x86_features;
		entry->ecx &= kvm_supported_word6_x86_features;
		break;
	}
	put_cpu();
}

static int kvm_dev_ioctl_get_supported_cpuid(struct kvm_cpuid2 *cpuid,
				    struct kvm_cpuid_entry2 __user *entries)
{
	struct kvm_cpuid_entry2 *cpuid_entries;
	int limit, nent = 0, r = -E2BIG;
	u32 func;

	if (cpuid->nent < 1)
		goto out;
	r = -ENOMEM;
	cpuid_entries = vmalloc(sizeof(struct kvm_cpuid_entry2) * cpuid->nent);
	if (!cpuid_entries)
		goto out;

	do_cpuid_ent(&cpuid_entries[0], 0, 0, &nent, cpuid->nent);
	limit = cpuid_entries[0].eax;
	for (func = 1; func <= limit && nent < cpuid->nent; ++func)
		do_cpuid_ent(&cpuid_entries[nent], func, 0,
				&nent, cpuid->nent);
	r = -E2BIG;
	if (nent >= cpuid->nent)
		goto out_free;

	do_cpuid_ent(&cpuid_entries[nent], 0x80000000, 0, &nent, cpuid->nent);
	limit = cpuid_entries[nent - 1].eax;
	for (func = 0x80000001; func <= limit && nent < cpuid->nent; ++func)
		do_cpuid_ent(&cpuid_entries[nent], func, 0,
			       &nent, cpuid->nent);
	r = -EFAULT;
	if (copy_to_user(entries, cpuid_entries,
			nent * sizeof(struct kvm_cpuid_entry2)))
		goto out_free;
	cpuid->nent = nent;
	r = 0;

out_free:
	vfree(cpuid_entries);
out:
	return r;
}

static int kvm_vcpu_ioctl_get_lapic(struct kvm_vcpu *vcpu,
				    struct kvm_lapic_state *s)
{
	vcpu_load(vcpu);
	memcpy(s->regs, vcpu->arch.apic->regs, sizeof *s);
	vcpu_put(vcpu);

	return 0;
}

static int kvm_vcpu_ioctl_set_lapic(struct kvm_vcpu *vcpu,
				    struct kvm_lapic_state *s)
{
	vcpu_load(vcpu);
	memcpy(vcpu->arch.apic->regs, s->regs, sizeof *s);
	kvm_apic_post_state_restore(vcpu);
	vcpu_put(vcpu);

	return 0;
}

static int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu,
				    struct kvm_interrupt *irq)
{
	if (irq->irq < 0 || irq->irq >= 256)
		return -EINVAL;
	if (irqchip_in_kernel(vcpu->kvm))
		return -ENXIO;
	vcpu_load(vcpu);

	set_bit(irq->irq, vcpu->arch.irq_pending);
	set_bit(irq->irq / BITS_PER_LONG, &vcpu->arch.irq_summary);

	vcpu_put(vcpu);

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

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;

	switch (ioctl) {
	case KVM_GET_LAPIC: {
		struct kvm_lapic_state lapic;

		memset(&lapic, 0, sizeof lapic);
		r = kvm_vcpu_ioctl_get_lapic(vcpu, &lapic);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &lapic, sizeof lapic))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_LAPIC: {
		struct kvm_lapic_state lapic;

		r = -EFAULT;
		if (copy_from_user(&lapic, argp, sizeof lapic))
			goto out;
		r = kvm_vcpu_ioctl_set_lapic(vcpu, &lapic);;
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
	default:
		r = -EINVAL;
	}
out:
	return r;
}

static int kvm_vm_ioctl_set_tss_addr(struct kvm *kvm, unsigned long addr)
{
	int ret;

	if (addr > (unsigned int)(-3 * PAGE_SIZE))
		return -1;
	ret = kvm_x86_ops->set_tss_addr(kvm, addr);
	return ret;
}

static int kvm_vm_ioctl_set_nr_mmu_pages(struct kvm *kvm,
					  u32 kvm_nr_mmu_pages)
{
	if (kvm_nr_mmu_pages < KVM_MIN_ALLOC_MMU_PAGES)
		return -EINVAL;

	down_write(&kvm->slots_lock);

	kvm_mmu_change_mmu_pages(kvm, kvm_nr_mmu_pages);
	kvm->arch.n_requested_mmu_pages = kvm_nr_mmu_pages;

	up_write(&kvm->slots_lock);
	return 0;
}

static int kvm_vm_ioctl_get_nr_mmu_pages(struct kvm *kvm)
{
	return kvm->arch.n_alloc_mmu_pages;
}

gfn_t unalias_gfn(struct kvm *kvm, gfn_t gfn)
{
	int i;
	struct kvm_mem_alias *alias;

	for (i = 0; i < kvm->arch.naliases; ++i) {
		alias = &kvm->arch.aliases[i];
		if (gfn >= alias->base_gfn
		    && gfn < alias->base_gfn + alias->npages)
			return alias->target_gfn + gfn - alias->base_gfn;
	}
	return gfn;
}

/*
 * Set a new alias region.  Aliases map a portion of physical memory into
 * another portion.  This is useful for memory windows, for example the PC
 * VGA region.
 */
static int kvm_vm_ioctl_set_memory_alias(struct kvm *kvm,
					 struct kvm_memory_alias *alias)
{
	int r, n;
	struct kvm_mem_alias *p;

	r = -EINVAL;
	/* General sanity checks */
	if (alias->memory_size & (PAGE_SIZE - 1))
		goto out;
	if (alias->guest_phys_addr & (PAGE_SIZE - 1))
		goto out;
	if (alias->slot >= KVM_ALIAS_SLOTS)
		goto out;
	if (alias->guest_phys_addr + alias->memory_size
	    < alias->guest_phys_addr)
		goto out;
	if (alias->target_phys_addr + alias->memory_size
	    < alias->target_phys_addr)
		goto out;

	down_write(&kvm->slots_lock);
	spin_lock(&kvm->mmu_lock);

	p = &kvm->arch.aliases[alias->slot];
	p->base_gfn = alias->guest_phys_addr >> PAGE_SHIFT;
	p->npages = alias->memory_size >> PAGE_SHIFT;
	p->target_gfn = alias->target_phys_addr >> PAGE_SHIFT;

	for (n = KVM_ALIAS_SLOTS; n > 0; --n)
		if (kvm->arch.aliases[n - 1].npages)
			break;
	kvm->arch.naliases = n;

	spin_unlock(&kvm->mmu_lock);
	kvm_mmu_zap_all(kvm);

	up_write(&kvm->slots_lock);

	return 0;

out:
	return r;
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
		memcpy(&chip->chip.ioapic,
			ioapic_irqchip(kvm),
			sizeof(struct kvm_ioapic_state));
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
		memcpy(&pic_irqchip(kvm)->pics[0],
			&chip->chip.pic,
			sizeof(struct kvm_pic_state));
		break;
	case KVM_IRQCHIP_PIC_SLAVE:
		memcpy(&pic_irqchip(kvm)->pics[1],
			&chip->chip.pic,
			sizeof(struct kvm_pic_state));
		break;
	case KVM_IRQCHIP_IOAPIC:
		memcpy(ioapic_irqchip(kvm),
			&chip->chip.ioapic,
			sizeof(struct kvm_ioapic_state));
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

	memcpy(ps, &kvm->arch.vpit->pit_state, sizeof(struct kvm_pit_state));
	return r;
}

static int kvm_vm_ioctl_set_pit(struct kvm *kvm, struct kvm_pit_state *ps)
{
	int r = 0;

	memcpy(&kvm->arch.vpit->pit_state, ps, sizeof(struct kvm_pit_state));
	kvm_pit_load_count(kvm, 0, ps->channels[0].count);
	return r;
}

/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
				      struct kvm_dirty_log *log)
{
	int r;
	int n;
	struct kvm_memory_slot *memslot;
	int is_dirty = 0;

	down_write(&kvm->slots_lock);

	r = kvm_get_dirty_log(kvm, log, &is_dirty);
	if (r)
		goto out;

	/* If nothing is dirty, don't bother messing with page tables. */
	if (is_dirty) {
		kvm_mmu_slot_remove_write_access(kvm, log->slot);
		kvm_flush_remote_tlbs(kvm);
		memslot = &kvm->memslots[log->slot];
		n = ALIGN(memslot->npages, BITS_PER_LONG) / 8;
		memset(memslot->dirty_bitmap, 0, n);
	}
	r = 0;
out:
	up_write(&kvm->slots_lock);
	return r;
}

long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r = -EINVAL;

	switch (ioctl) {
	case KVM_SET_TSS_ADDR:
		r = kvm_vm_ioctl_set_tss_addr(kvm, arg);
		if (r < 0)
			goto out;
		break;
	case KVM_SET_MEMORY_REGION: {
		struct kvm_memory_region kvm_mem;
		struct kvm_userspace_memory_region kvm_userspace_mem;

		r = -EFAULT;
		if (copy_from_user(&kvm_mem, argp, sizeof kvm_mem))
			goto out;
		kvm_userspace_mem.slot = kvm_mem.slot;
		kvm_userspace_mem.flags = kvm_mem.flags;
		kvm_userspace_mem.guest_phys_addr = kvm_mem.guest_phys_addr;
		kvm_userspace_mem.memory_size = kvm_mem.memory_size;
		r = kvm_vm_ioctl_set_memory_region(kvm, &kvm_userspace_mem, 0);
		if (r)
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
	case KVM_SET_MEMORY_ALIAS: {
		struct kvm_memory_alias alias;

		r = -EFAULT;
		if (copy_from_user(&alias, argp, sizeof alias))
			goto out;
		r = kvm_vm_ioctl_set_memory_alias(kvm, &alias);
		if (r)
			goto out;
		break;
	}
	case KVM_CREATE_IRQCHIP:
		r = -ENOMEM;
		kvm->arch.vpic = kvm_create_pic(kvm);
		if (kvm->arch.vpic) {
			r = kvm_ioapic_init(kvm);
			if (r) {
				kfree(kvm->arch.vpic);
				kvm->arch.vpic = NULL;
				goto out;
			}
		} else
			goto out;
		break;
	case KVM_CREATE_PIT:
		r = -ENOMEM;
		kvm->arch.vpit = kvm_create_pit(kvm);
		if (kvm->arch.vpit)
			r = 0;
		break;
	case KVM_IRQ_LINE: {
		struct kvm_irq_level irq_event;

		r = -EFAULT;
		if (copy_from_user(&irq_event, argp, sizeof irq_event))
			goto out;
		if (irqchip_in_kernel(kvm)) {
			mutex_lock(&kvm->lock);
			if (irq_event.irq < 16)
				kvm_pic_set_irq(pic_irqchip(kvm),
					irq_event.irq,
					irq_event.level);
			kvm_ioapic_set_irq(kvm->arch.vioapic,
					irq_event.irq,
					irq_event.level);
			mutex_unlock(&kvm->lock);
			r = 0;
		}
		break;
	}
	case KVM_GET_IRQCHIP: {
		/* 0: PIC master, 1: PIC slave, 2: IOAPIC */
		struct kvm_irqchip chip;

		r = -EFAULT;
		if (copy_from_user(&chip, argp, sizeof chip))
			goto out;
		r = -ENXIO;
		if (!irqchip_in_kernel(kvm))
			goto out;
		r = kvm_vm_ioctl_get_irqchip(kvm, &chip);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &chip, sizeof chip))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_IRQCHIP: {
		/* 0: PIC master, 1: PIC slave, 2: IOAPIC */
		struct kvm_irqchip chip;

		r = -EFAULT;
		if (copy_from_user(&chip, argp, sizeof chip))
			goto out;
		r = -ENXIO;
		if (!irqchip_in_kernel(kvm))
			goto out;
		r = kvm_vm_ioctl_set_irqchip(kvm, &chip);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_PIT: {
		struct kvm_pit_state ps;
		r = -EFAULT;
		if (copy_from_user(&ps, argp, sizeof ps))
			goto out;
		r = -ENXIO;
		if (!kvm->arch.vpit)
			goto out;
		r = kvm_vm_ioctl_get_pit(kvm, &ps);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &ps, sizeof ps))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_PIT: {
		struct kvm_pit_state ps;
		r = -EFAULT;
		if (copy_from_user(&ps, argp, sizeof ps))
			goto out;
		r = -ENXIO;
		if (!kvm->arch.vpit)
			goto out;
		r = kvm_vm_ioctl_set_pit(kvm, &ps);
		if (r)
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

	for (i = j = 0; i < ARRAY_SIZE(msrs_to_save); i++) {
		if (rdmsr_safe(msrs_to_save[i], &dummy[0], &dummy[1]) < 0)
			continue;
		if (j < i)
			msrs_to_save[j] = msrs_to_save[i];
		j++;
	}
	num_msrs_to_save = j;
}

/*
 * Only apic need an MMIO device hook, so shortcut now..
 */
static struct kvm_io_device *vcpu_find_pervcpu_dev(struct kvm_vcpu *vcpu,
						gpa_t addr, int len,
						int is_write)
{
	struct kvm_io_device *dev;

	if (vcpu->arch.apic) {
		dev = &vcpu->arch.apic->dev;
		if (dev->in_range(dev, addr, len, is_write))
			return dev;
	}
	return NULL;
}


static struct kvm_io_device *vcpu_find_mmio_dev(struct kvm_vcpu *vcpu,
						gpa_t addr, int len,
						int is_write)
{
	struct kvm_io_device *dev;

	dev = vcpu_find_pervcpu_dev(vcpu, addr, len, is_write);
	if (dev == NULL)
		dev = kvm_io_bus_find_dev(&vcpu->kvm->mmio_bus, addr, len,
					  is_write);
	return dev;
}

int emulator_read_std(unsigned long addr,
			     void *val,
			     unsigned int bytes,
			     struct kvm_vcpu *vcpu)
{
	void *data = val;
	int r = X86EMUL_CONTINUE;

	while (bytes) {
		gpa_t gpa = vcpu->arch.mmu.gva_to_gpa(vcpu, addr);
		unsigned offset = addr & (PAGE_SIZE-1);
		unsigned tocopy = min(bytes, (unsigned)PAGE_SIZE - offset);
		int ret;

		if (gpa == UNMAPPED_GVA) {
			r = X86EMUL_PROPAGATE_FAULT;
			goto out;
		}
		ret = kvm_read_guest(vcpu->kvm, gpa, data, tocopy);
		if (ret < 0) {
			r = X86EMUL_UNHANDLEABLE;
			goto out;
		}

		bytes -= tocopy;
		data += tocopy;
		addr += tocopy;
	}
out:
	return r;
}
EXPORT_SYMBOL_GPL(emulator_read_std);

static int emulator_read_emulated(unsigned long addr,
				  void *val,
				  unsigned int bytes,
				  struct kvm_vcpu *vcpu)
{
	struct kvm_io_device *mmio_dev;
	gpa_t                 gpa;

	if (vcpu->mmio_read_completed) {
		memcpy(val, vcpu->mmio_data, bytes);
		vcpu->mmio_read_completed = 0;
		return X86EMUL_CONTINUE;
	}

	gpa = vcpu->arch.mmu.gva_to_gpa(vcpu, addr);

	/* For APIC access vmexit */
	if ((gpa & PAGE_MASK) == APIC_DEFAULT_PHYS_BASE)
		goto mmio;

	if (emulator_read_std(addr, val, bytes, vcpu)
			== X86EMUL_CONTINUE)
		return X86EMUL_CONTINUE;
	if (gpa == UNMAPPED_GVA)
		return X86EMUL_PROPAGATE_FAULT;

mmio:
	/*
	 * Is this MMIO handled locally?
	 */
	mutex_lock(&vcpu->kvm->lock);
	mmio_dev = vcpu_find_mmio_dev(vcpu, gpa, bytes, 0);
	if (mmio_dev) {
		kvm_iodevice_read(mmio_dev, gpa, bytes, val);
		mutex_unlock(&vcpu->kvm->lock);
		return X86EMUL_CONTINUE;
	}
	mutex_unlock(&vcpu->kvm->lock);

	vcpu->mmio_needed = 1;
	vcpu->mmio_phys_addr = gpa;
	vcpu->mmio_size = bytes;
	vcpu->mmio_is_write = 0;

	return X86EMUL_UNHANDLEABLE;
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

static int emulator_write_emulated_onepage(unsigned long addr,
					   const void *val,
					   unsigned int bytes,
					   struct kvm_vcpu *vcpu)
{
	struct kvm_io_device *mmio_dev;
	gpa_t                 gpa;

	gpa = vcpu->arch.mmu.gva_to_gpa(vcpu, addr);

	if (gpa == UNMAPPED_GVA) {
		kvm_inject_page_fault(vcpu, addr, 2);
		return X86EMUL_PROPAGATE_FAULT;
	}

	/* For APIC access vmexit */
	if ((gpa & PAGE_MASK) == APIC_DEFAULT_PHYS_BASE)
		goto mmio;

	if (emulator_write_phys(vcpu, gpa, val, bytes))
		return X86EMUL_CONTINUE;

mmio:
	/*
	 * Is this MMIO handled locally?
	 */
	mutex_lock(&vcpu->kvm->lock);
	mmio_dev = vcpu_find_mmio_dev(vcpu, gpa, bytes, 1);
	if (mmio_dev) {
		kvm_iodevice_write(mmio_dev, gpa, bytes, val);
		mutex_unlock(&vcpu->kvm->lock);
		return X86EMUL_CONTINUE;
	}
	mutex_unlock(&vcpu->kvm->lock);

	vcpu->mmio_needed = 1;
	vcpu->mmio_phys_addr = gpa;
	vcpu->mmio_size = bytes;
	vcpu->mmio_is_write = 1;
	memcpy(vcpu->mmio_data, val, bytes);

	return X86EMUL_CONTINUE;
}

int emulator_write_emulated(unsigned long addr,
				   const void *val,
				   unsigned int bytes,
				   struct kvm_vcpu *vcpu)
{
	/* Crossing a page boundary? */
	if (((addr + bytes - 1) ^ addr) & PAGE_MASK) {
		int rc, now;

		now = -addr & ~PAGE_MASK;
		rc = emulator_write_emulated_onepage(addr, val, now, vcpu);
		if (rc != X86EMUL_CONTINUE)
			return rc;
		addr += now;
		val += now;
		bytes -= now;
	}
	return emulator_write_emulated_onepage(addr, val, bytes, vcpu);
}
EXPORT_SYMBOL_GPL(emulator_write_emulated);

static int emulator_cmpxchg_emulated(unsigned long addr,
				     const void *old,
				     const void *new,
				     unsigned int bytes,
				     struct kvm_vcpu *vcpu)
{
	static int reported;

	if (!reported) {
		reported = 1;
		printk(KERN_WARNING "kvm: emulating exchange as write\n");
	}
#ifndef CONFIG_X86_64
	/* guests cmpxchg8b have to be emulated atomically */
	if (bytes == 8) {
		gpa_t gpa;
		struct page *page;
		char *kaddr;
		u64 val;

		gpa = vcpu->arch.mmu.gva_to_gpa(vcpu, addr);

		if (gpa == UNMAPPED_GVA ||
		   (gpa & PAGE_MASK) == APIC_DEFAULT_PHYS_BASE)
			goto emul_write;

		if (((gpa + bytes - 1) & PAGE_MASK) != (gpa & PAGE_MASK))
			goto emul_write;

		val = *(u64 *)new;

		down_read(&current->mm->mmap_sem);
		page = gfn_to_page(vcpu->kvm, gpa >> PAGE_SHIFT);
		up_read(&current->mm->mmap_sem);

		kaddr = kmap_atomic(page, KM_USER0);
		set_64bit((u64 *)(kaddr + offset_in_page(gpa)), val);
		kunmap_atomic(kaddr, KM_USER0);
		kvm_release_page_dirty(page);
	}
emul_write:
#endif

	return emulator_write_emulated(addr, new, bytes, vcpu);
}

static unsigned long get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	return kvm_x86_ops->get_segment_base(vcpu, seg);
}

int emulate_invlpg(struct kvm_vcpu *vcpu, gva_t address)
{
	return X86EMUL_CONTINUE;
}

int emulate_clts(struct kvm_vcpu *vcpu)
{
	KVMTRACE_0D(CLTS, vcpu, handler);
	kvm_x86_ops->set_cr0(vcpu, vcpu->arch.cr0 & ~X86_CR0_TS);
	return X86EMUL_CONTINUE;
}

int emulator_get_dr(struct x86_emulate_ctxt *ctxt, int dr, unsigned long *dest)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;

	switch (dr) {
	case 0 ... 3:
		*dest = kvm_x86_ops->get_dr(vcpu, dr);
		return X86EMUL_CONTINUE;
	default:
		pr_unimpl(vcpu, "%s: unexpected dr %u\n", __func__, dr);
		return X86EMUL_UNHANDLEABLE;
	}
}

int emulator_set_dr(struct x86_emulate_ctxt *ctxt, int dr, unsigned long value)
{
	unsigned long mask = (ctxt->mode == X86EMUL_MODE_PROT64) ? ~0ULL : ~0U;
	int exception;

	kvm_x86_ops->set_dr(ctxt->vcpu, dr, value & mask, &exception);
	if (exception) {
		/* FIXME: better handling */
		return X86EMUL_UNHANDLEABLE;
	}
	return X86EMUL_CONTINUE;
}

void kvm_report_emulation_failure(struct kvm_vcpu *vcpu, const char *context)
{
	u8 opcodes[4];
	unsigned long rip = vcpu->arch.rip;
	unsigned long rip_linear;

	if (!printk_ratelimit())
		return;

	rip_linear = rip + get_segment_base(vcpu, VCPU_SREG_CS);

	emulator_read_std(rip_linear, (void *)opcodes, 4, vcpu);

	printk(KERN_ERR "emulation failed (%s) rip %lx %02x %02x %02x %02x\n",
	       context, rip, opcodes[0], opcodes[1], opcodes[2], opcodes[3]);
}
EXPORT_SYMBOL_GPL(kvm_report_emulation_failure);

static struct x86_emulate_ops emulate_ops = {
	.read_std            = emulator_read_std,
	.read_emulated       = emulator_read_emulated,
	.write_emulated      = emulator_write_emulated,
	.cmpxchg_emulated    = emulator_cmpxchg_emulated,
};

int emulate_instruction(struct kvm_vcpu *vcpu,
			struct kvm_run *run,
			unsigned long cr2,
			u16 error_code,
			int emulation_type)
{
	int r;
	struct decode_cache *c;

	vcpu->arch.mmio_fault_cr2 = cr2;
	kvm_x86_ops->cache_regs(vcpu);

	vcpu->mmio_is_write = 0;
	vcpu->arch.pio.string = 0;

	if (!(emulation_type & EMULTYPE_NO_DECODE)) {
		int cs_db, cs_l;
		kvm_x86_ops->get_cs_db_l_bits(vcpu, &cs_db, &cs_l);

		vcpu->arch.emulate_ctxt.vcpu = vcpu;
		vcpu->arch.emulate_ctxt.eflags = kvm_x86_ops->get_rflags(vcpu);
		vcpu->arch.emulate_ctxt.mode =
			(vcpu->arch.emulate_ctxt.eflags & X86_EFLAGS_VM)
			? X86EMUL_MODE_REAL : cs_l
			? X86EMUL_MODE_PROT64 :	cs_db
			? X86EMUL_MODE_PROT32 : X86EMUL_MODE_PROT16;

		r = x86_decode_insn(&vcpu->arch.emulate_ctxt, &emulate_ops);

		/* Reject the instructions other than VMCALL/VMMCALL when
		 * try to emulate invalid opcode */
		c = &vcpu->arch.emulate_ctxt.decode;
		if ((emulation_type & EMULTYPE_TRAP_UD) &&
		    (!(c->twobyte && c->b == 0x01 &&
		      (c->modrm_reg == 0 || c->modrm_reg == 3) &&
		       c->modrm_mod == 3 && c->modrm_rm == 1)))
			return EMULATE_FAIL;

		++vcpu->stat.insn_emulation;
		if (r)  {
			++vcpu->stat.insn_emulation_fail;
			if (kvm_mmu_unprotect_page_virt(vcpu, cr2))
				return EMULATE_DONE;
			return EMULATE_FAIL;
		}
	}

	r = x86_emulate_insn(&vcpu->arch.emulate_ctxt, &emulate_ops);

	if (vcpu->arch.pio.string)
		return EMULATE_DO_MMIO;

	if ((r || vcpu->mmio_is_write) && run) {
		run->exit_reason = KVM_EXIT_MMIO;
		run->mmio.phys_addr = vcpu->mmio_phys_addr;
		memcpy(run->mmio.data, vcpu->mmio_data, 8);
		run->mmio.len = vcpu->mmio_size;
		run->mmio.is_write = vcpu->mmio_is_write;
	}

	if (r) {
		if (kvm_mmu_unprotect_page_virt(vcpu, cr2))
			return EMULATE_DONE;
		if (!vcpu->mmio_needed) {
			kvm_report_emulation_failure(vcpu, "mmio");
			return EMULATE_FAIL;
		}
		return EMULATE_DO_MMIO;
	}

	kvm_x86_ops->decache_regs(vcpu);
	kvm_x86_ops->set_rflags(vcpu, vcpu->arch.emulate_ctxt.eflags);

	if (vcpu->mmio_is_write) {
		vcpu->mmio_needed = 0;
		return EMULATE_DO_MMIO;
	}

	return EMULATE_DONE;
}
EXPORT_SYMBOL_GPL(emulate_instruction);

static void free_pio_guest_pages(struct kvm_vcpu *vcpu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vcpu->arch.pio.guest_pages); ++i)
		if (vcpu->arch.pio.guest_pages[i]) {
			kvm_release_page_dirty(vcpu->arch.pio.guest_pages[i]);
			vcpu->arch.pio.guest_pages[i] = NULL;
		}
}

static int pio_copy_data(struct kvm_vcpu *vcpu)
{
	void *p = vcpu->arch.pio_data;
	void *q;
	unsigned bytes;
	int nr_pages = vcpu->arch.pio.guest_pages[1] ? 2 : 1;

	q = vmap(vcpu->arch.pio.guest_pages, nr_pages, VM_READ|VM_WRITE,
		 PAGE_KERNEL);
	if (!q) {
		free_pio_guest_pages(vcpu);
		return -ENOMEM;
	}
	q += vcpu->arch.pio.guest_page_offset;
	bytes = vcpu->arch.pio.size * vcpu->arch.pio.cur_count;
	if (vcpu->arch.pio.in)
		memcpy(q, p, bytes);
	else
		memcpy(p, q, bytes);
	q -= vcpu->arch.pio.guest_page_offset;
	vunmap(q);
	free_pio_guest_pages(vcpu);
	return 0;
}

int complete_pio(struct kvm_vcpu *vcpu)
{
	struct kvm_pio_request *io = &vcpu->arch.pio;
	long delta;
	int r;

	kvm_x86_ops->cache_regs(vcpu);

	if (!io->string) {
		if (io->in)
			memcpy(&vcpu->arch.regs[VCPU_REGS_RAX], vcpu->arch.pio_data,
			       io->size);
	} else {
		if (io->in) {
			r = pio_copy_data(vcpu);
			if (r) {
				kvm_x86_ops->cache_regs(vcpu);
				return r;
			}
		}

		delta = 1;
		if (io->rep) {
			delta *= io->cur_count;
			/*
			 * The size of the register should really depend on
			 * current address size.
			 */
			vcpu->arch.regs[VCPU_REGS_RCX] -= delta;
		}
		if (io->down)
			delta = -delta;
		delta *= io->size;
		if (io->in)
			vcpu->arch.regs[VCPU_REGS_RDI] += delta;
		else
			vcpu->arch.regs[VCPU_REGS_RSI] += delta;
	}

	kvm_x86_ops->decache_regs(vcpu);

	io->count -= io->cur_count;
	io->cur_count = 0;

	return 0;
}

static void kernel_pio(struct kvm_io_device *pio_dev,
		       struct kvm_vcpu *vcpu,
		       void *pd)
{
	/* TODO: String I/O for in kernel device */

	mutex_lock(&vcpu->kvm->lock);
	if (vcpu->arch.pio.in)
		kvm_iodevice_read(pio_dev, vcpu->arch.pio.port,
				  vcpu->arch.pio.size,
				  pd);
	else
		kvm_iodevice_write(pio_dev, vcpu->arch.pio.port,
				   vcpu->arch.pio.size,
				   pd);
	mutex_unlock(&vcpu->kvm->lock);
}

static void pio_string_write(struct kvm_io_device *pio_dev,
			     struct kvm_vcpu *vcpu)
{
	struct kvm_pio_request *io = &vcpu->arch.pio;
	void *pd = vcpu->arch.pio_data;
	int i;

	mutex_lock(&vcpu->kvm->lock);
	for (i = 0; i < io->cur_count; i++) {
		kvm_iodevice_write(pio_dev, io->port,
				   io->size,
				   pd);
		pd += io->size;
	}
	mutex_unlock(&vcpu->kvm->lock);
}

static struct kvm_io_device *vcpu_find_pio_dev(struct kvm_vcpu *vcpu,
					       gpa_t addr, int len,
					       int is_write)
{
	return kvm_io_bus_find_dev(&vcpu->kvm->pio_bus, addr, len, is_write);
}

int kvm_emulate_pio(struct kvm_vcpu *vcpu, struct kvm_run *run, int in,
		  int size, unsigned port)
{
	struct kvm_io_device *pio_dev;

	vcpu->run->exit_reason = KVM_EXIT_IO;
	vcpu->run->io.direction = in ? KVM_EXIT_IO_IN : KVM_EXIT_IO_OUT;
	vcpu->run->io.size = vcpu->arch.pio.size = size;
	vcpu->run->io.data_offset = KVM_PIO_PAGE_OFFSET * PAGE_SIZE;
	vcpu->run->io.count = vcpu->arch.pio.count = vcpu->arch.pio.cur_count = 1;
	vcpu->run->io.port = vcpu->arch.pio.port = port;
	vcpu->arch.pio.in = in;
	vcpu->arch.pio.string = 0;
	vcpu->arch.pio.down = 0;
	vcpu->arch.pio.guest_page_offset = 0;
	vcpu->arch.pio.rep = 0;

	if (vcpu->run->io.direction == KVM_EXIT_IO_IN)
		KVMTRACE_2D(IO_READ, vcpu, vcpu->run->io.port, (u32)size,
			    handler);
	else
		KVMTRACE_2D(IO_WRITE, vcpu, vcpu->run->io.port, (u32)size,
			    handler);

	kvm_x86_ops->cache_regs(vcpu);
	memcpy(vcpu->arch.pio_data, &vcpu->arch.regs[VCPU_REGS_RAX], 4);

	kvm_x86_ops->skip_emulated_instruction(vcpu);

	pio_dev = vcpu_find_pio_dev(vcpu, port, size, !in);
	if (pio_dev) {
		kernel_pio(pio_dev, vcpu, vcpu->arch.pio_data);
		complete_pio(vcpu);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_emulate_pio);

int kvm_emulate_pio_string(struct kvm_vcpu *vcpu, struct kvm_run *run, int in,
		  int size, unsigned long count, int down,
		  gva_t address, int rep, unsigned port)
{
	unsigned now, in_page;
	int i, ret = 0;
	int nr_pages = 1;
	struct page *page;
	struct kvm_io_device *pio_dev;

	vcpu->run->exit_reason = KVM_EXIT_IO;
	vcpu->run->io.direction = in ? KVM_EXIT_IO_IN : KVM_EXIT_IO_OUT;
	vcpu->run->io.size = vcpu->arch.pio.size = size;
	vcpu->run->io.data_offset = KVM_PIO_PAGE_OFFSET * PAGE_SIZE;
	vcpu->run->io.count = vcpu->arch.pio.count = vcpu->arch.pio.cur_count = count;
	vcpu->run->io.port = vcpu->arch.pio.port = port;
	vcpu->arch.pio.in = in;
	vcpu->arch.pio.string = 1;
	vcpu->arch.pio.down = down;
	vcpu->arch.pio.guest_page_offset = offset_in_page(address);
	vcpu->arch.pio.rep = rep;

	if (vcpu->run->io.direction == KVM_EXIT_IO_IN)
		KVMTRACE_2D(IO_READ, vcpu, vcpu->run->io.port, (u32)size,
			    handler);
	else
		KVMTRACE_2D(IO_WRITE, vcpu, vcpu->run->io.port, (u32)size,
			    handler);

	if (!count) {
		kvm_x86_ops->skip_emulated_instruction(vcpu);
		return 1;
	}

	if (!down)
		in_page = PAGE_SIZE - offset_in_page(address);
	else
		in_page = offset_in_page(address) + size;
	now = min(count, (unsigned long)in_page / size);
	if (!now) {
		/*
		 * String I/O straddles page boundary.  Pin two guest pages
		 * so that we satisfy atomicity constraints.  Do just one
		 * transaction to avoid complexity.
		 */
		nr_pages = 2;
		now = 1;
	}
	if (down) {
		/*
		 * String I/O in reverse.  Yuck.  Kill the guest, fix later.
		 */
		pr_unimpl(vcpu, "guest string pio down\n");
		kvm_inject_gp(vcpu, 0);
		return 1;
	}
	vcpu->run->io.count = now;
	vcpu->arch.pio.cur_count = now;

	if (vcpu->arch.pio.cur_count == vcpu->arch.pio.count)
		kvm_x86_ops->skip_emulated_instruction(vcpu);

	for (i = 0; i < nr_pages; ++i) {
		page = gva_to_page(vcpu, address + i * PAGE_SIZE);
		vcpu->arch.pio.guest_pages[i] = page;
		if (!page) {
			kvm_inject_gp(vcpu, 0);
			free_pio_guest_pages(vcpu);
			return 1;
		}
	}

	pio_dev = vcpu_find_pio_dev(vcpu, port,
				    vcpu->arch.pio.cur_count,
				    !vcpu->arch.pio.in);
	if (!vcpu->arch.pio.in) {
		/* string PIO write */
		ret = pio_copy_data(vcpu);
		if (ret >= 0 && pio_dev) {
			pio_string_write(pio_dev, vcpu);
			complete_pio(vcpu);
			if (vcpu->arch.pio.count == 0)
				ret = 1;
		}
	} else if (pio_dev)
		pr_unimpl(vcpu, "no string pio read support yet, "
		       "port %x size %d count %ld\n",
			port, size, count);

	return ret;
}
EXPORT_SYMBOL_GPL(kvm_emulate_pio_string);

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

	kvm_init_msr_list();

	kvm_x86_ops = ops;
	kvm_mmu_set_nonpresent_ptes(0ull, 0ull);
	kvm_mmu_set_base_ptes(PT_PRESENT_MASK);
	kvm_mmu_set_mask_ptes(PT_USER_MASK, PT_ACCESSED_MASK,
			PT_DIRTY_MASK, PT64_NX_MASK, 0);
	return 0;

out:
	return r;
}

void kvm_arch_exit(void)
{
	kvm_x86_ops = NULL;
	kvm_mmu_module_exit();
}

int kvm_emulate_halt(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.halt_exits;
	KVMTRACE_0D(HLT, vcpu, handler);
	if (irqchip_in_kernel(vcpu->kvm)) {
		vcpu->arch.mp_state = KVM_MP_STATE_HALTED;
		up_read(&vcpu->kvm->slots_lock);
		kvm_vcpu_block(vcpu);
		down_read(&vcpu->kvm->slots_lock);
		if (vcpu->arch.mp_state != KVM_MP_STATE_RUNNABLE)
			return -EINTR;
		return 1;
	} else {
		vcpu->run->exit_reason = KVM_EXIT_HLT;
		return 0;
	}
}
EXPORT_SYMBOL_GPL(kvm_emulate_halt);

static inline gpa_t hc_gpa(struct kvm_vcpu *vcpu, unsigned long a0,
			   unsigned long a1)
{
	if (is_long_mode(vcpu))
		return a0;
	else
		return a0 | ((gpa_t)a1 << 32);
}

int kvm_emulate_hypercall(struct kvm_vcpu *vcpu)
{
	unsigned long nr, a0, a1, a2, a3, ret;
	int r = 1;

	kvm_x86_ops->cache_regs(vcpu);

	nr = vcpu->arch.regs[VCPU_REGS_RAX];
	a0 = vcpu->arch.regs[VCPU_REGS_RBX];
	a1 = vcpu->arch.regs[VCPU_REGS_RCX];
	a2 = vcpu->arch.regs[VCPU_REGS_RDX];
	a3 = vcpu->arch.regs[VCPU_REGS_RSI];

	KVMTRACE_1D(VMMCALL, vcpu, (u32)nr, handler);

	if (!is_long_mode(vcpu)) {
		nr &= 0xFFFFFFFF;
		a0 &= 0xFFFFFFFF;
		a1 &= 0xFFFFFFFF;
		a2 &= 0xFFFFFFFF;
		a3 &= 0xFFFFFFFF;
	}

	switch (nr) {
	case KVM_HC_VAPIC_POLL_IRQ:
		ret = 0;
		break;
	case KVM_HC_MMU_OP:
		r = kvm_pv_mmu_op(vcpu, a0, hc_gpa(vcpu, a1, a2), &ret);
		break;
	default:
		ret = -KVM_ENOSYS;
		break;
	}
	vcpu->arch.regs[VCPU_REGS_RAX] = ret;
	kvm_x86_ops->decache_regs(vcpu);
	++vcpu->stat.hypercalls;
	return r;
}
EXPORT_SYMBOL_GPL(kvm_emulate_hypercall);

int kvm_fix_hypercall(struct kvm_vcpu *vcpu)
{
	char instruction[3];
	int ret = 0;


	/*
	 * Blow out the MMU to ensure that no other VCPU has an active mapping
	 * to ensure that the updated hypercall appears atomically across all
	 * VCPUs.
	 */
	kvm_mmu_zap_all(vcpu->kvm);

	kvm_x86_ops->cache_regs(vcpu);
	kvm_x86_ops->patch_hypercall(vcpu, instruction);
	if (emulator_write_emulated(vcpu->arch.rip, instruction, 3, vcpu)
	    != X86EMUL_CONTINUE)
		ret = -EFAULT;

	return ret;
}

static u64 mk_cr_64(u64 curr_cr, u32 new_val)
{
	return (curr_cr & ~((1ULL << 32) - 1)) | new_val;
}

void realmode_lgdt(struct kvm_vcpu *vcpu, u16 limit, unsigned long base)
{
	struct descriptor_table dt = { limit, base };

	kvm_x86_ops->set_gdt(vcpu, &dt);
}

void realmode_lidt(struct kvm_vcpu *vcpu, u16 limit, unsigned long base)
{
	struct descriptor_table dt = { limit, base };

	kvm_x86_ops->set_idt(vcpu, &dt);
}

void realmode_lmsw(struct kvm_vcpu *vcpu, unsigned long msw,
		   unsigned long *rflags)
{
	kvm_lmsw(vcpu, msw);
	*rflags = kvm_x86_ops->get_rflags(vcpu);
}

unsigned long realmode_get_cr(struct kvm_vcpu *vcpu, int cr)
{
	unsigned long value;

	kvm_x86_ops->decache_cr4_guest_bits(vcpu);
	switch (cr) {
	case 0:
		value = vcpu->arch.cr0;
		break;
	case 2:
		value = vcpu->arch.cr2;
		break;
	case 3:
		value = vcpu->arch.cr3;
		break;
	case 4:
		value = vcpu->arch.cr4;
		break;
	case 8:
		value = kvm_get_cr8(vcpu);
		break;
	default:
		vcpu_printf(vcpu, "%s: unexpected cr %u\n", __func__, cr);
		return 0;
	}
	KVMTRACE_3D(CR_READ, vcpu, (u32)cr, (u32)value,
		    (u32)((u64)value >> 32), handler);

	return value;
}

void realmode_set_cr(struct kvm_vcpu *vcpu, int cr, unsigned long val,
		     unsigned long *rflags)
{
	KVMTRACE_3D(CR_WRITE, vcpu, (u32)cr, (u32)val,
		    (u32)((u64)val >> 32), handler);

	switch (cr) {
	case 0:
		kvm_set_cr0(vcpu, mk_cr_64(vcpu->arch.cr0, val));
		*rflags = kvm_x86_ops->get_rflags(vcpu);
		break;
	case 2:
		vcpu->arch.cr2 = val;
		break;
	case 3:
		kvm_set_cr3(vcpu, val);
		break;
	case 4:
		kvm_set_cr4(vcpu, mk_cr_64(vcpu->arch.cr4, val));
		break;
	case 8:
		kvm_set_cr8(vcpu, val & 0xfUL);
		break;
	default:
		vcpu_printf(vcpu, "%s: unexpected cr %u\n", __func__, cr);
	}
}

static int move_to_next_stateful_cpuid_entry(struct kvm_vcpu *vcpu, int i)
{
	struct kvm_cpuid_entry2 *e = &vcpu->arch.cpuid_entries[i];
	int j, nent = vcpu->arch.cpuid_nent;

	e->flags &= ~KVM_CPUID_FLAG_STATE_READ_NEXT;
	/* when no next entry is found, the current entry[i] is reselected */
	for (j = i + 1; j == i; j = (j + 1) % nent) {
		struct kvm_cpuid_entry2 *ej = &vcpu->arch.cpuid_entries[j];
		if (ej->function == e->function) {
			ej->flags |= KVM_CPUID_FLAG_STATE_READ_NEXT;
			return j;
		}
	}
	return 0; /* silence gcc, even though control never reaches here */
}

/* find an entry with matching function, matching index (if needed), and that
 * should be read next (if it's stateful) */
static int is_matching_cpuid_entry(struct kvm_cpuid_entry2 *e,
	u32 function, u32 index)
{
	if (e->function != function)
		return 0;
	if ((e->flags & KVM_CPUID_FLAG_SIGNIFCANT_INDEX) && e->index != index)
		return 0;
	if ((e->flags & KVM_CPUID_FLAG_STATEFUL_FUNC) &&
		!(e->flags & KVM_CPUID_FLAG_STATE_READ_NEXT))
		return 0;
	return 1;
}

void kvm_emulate_cpuid(struct kvm_vcpu *vcpu)
{
	int i;
	u32 function, index;
	struct kvm_cpuid_entry2 *e, *best;

	kvm_x86_ops->cache_regs(vcpu);
	function = vcpu->arch.regs[VCPU_REGS_RAX];
	index = vcpu->arch.regs[VCPU_REGS_RCX];
	vcpu->arch.regs[VCPU_REGS_RAX] = 0;
	vcpu->arch.regs[VCPU_REGS_RBX] = 0;
	vcpu->arch.regs[VCPU_REGS_RCX] = 0;
	vcpu->arch.regs[VCPU_REGS_RDX] = 0;
	best = NULL;
	for (i = 0; i < vcpu->arch.cpuid_nent; ++i) {
		e = &vcpu->arch.cpuid_entries[i];
		if (is_matching_cpuid_entry(e, function, index)) {
			if (e->flags & KVM_CPUID_FLAG_STATEFUL_FUNC)
				move_to_next_stateful_cpuid_entry(vcpu, i);
			best = e;
			break;
		}
		/*
		 * Both basic or both extended?
		 */
		if (((e->function ^ function) & 0x80000000) == 0)
			if (!best || e->function > best->function)
				best = e;
	}
	if (best) {
		vcpu->arch.regs[VCPU_REGS_RAX] = best->eax;
		vcpu->arch.regs[VCPU_REGS_RBX] = best->ebx;
		vcpu->arch.regs[VCPU_REGS_RCX] = best->ecx;
		vcpu->arch.regs[VCPU_REGS_RDX] = best->edx;
	}
	kvm_x86_ops->decache_regs(vcpu);
	kvm_x86_ops->skip_emulated_instruction(vcpu);
	KVMTRACE_5D(CPUID, vcpu, function,
		    (u32)vcpu->arch.regs[VCPU_REGS_RAX],
		    (u32)vcpu->arch.regs[VCPU_REGS_RBX],
		    (u32)vcpu->arch.regs[VCPU_REGS_RCX],
		    (u32)vcpu->arch.regs[VCPU_REGS_RDX], handler);
}
EXPORT_SYMBOL_GPL(kvm_emulate_cpuid);

/*
 * Check if userspace requested an interrupt window, and that the
 * interrupt window is open.
 *
 * No need to exit to userspace if we already have an interrupt queued.
 */
static int dm_request_for_irq_injection(struct kvm_vcpu *vcpu,
					  struct kvm_run *kvm_run)
{
	return (!vcpu->arch.irq_summary &&
		kvm_run->request_interrupt_window &&
		vcpu->arch.interrupt_window_open &&
		(kvm_x86_ops->get_rflags(vcpu) & X86_EFLAGS_IF));
}

static void post_kvm_run_save(struct kvm_vcpu *vcpu,
			      struct kvm_run *kvm_run)
{
	kvm_run->if_flag = (kvm_x86_ops->get_rflags(vcpu) & X86_EFLAGS_IF) != 0;
	kvm_run->cr8 = kvm_get_cr8(vcpu);
	kvm_run->apic_base = kvm_get_apic_base(vcpu);
	if (irqchip_in_kernel(vcpu->kvm))
		kvm_run->ready_for_interrupt_injection = 1;
	else
		kvm_run->ready_for_interrupt_injection =
					(vcpu->arch.interrupt_window_open &&
					 vcpu->arch.irq_summary == 0);
}

static void vapic_enter(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	struct page *page;

	if (!apic || !apic->vapic_addr)
		return;

	down_read(&current->mm->mmap_sem);
	page = gfn_to_page(vcpu->kvm, apic->vapic_addr >> PAGE_SHIFT);
	up_read(&current->mm->mmap_sem);

	vcpu->arch.apic->vapic_page = page;
}

static void vapic_exit(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (!apic || !apic->vapic_addr)
		return;

	down_read(&vcpu->kvm->slots_lock);
	kvm_release_page_dirty(apic->vapic_page);
	mark_page_dirty(vcpu->kvm, apic->vapic_addr >> PAGE_SHIFT);
	up_read(&vcpu->kvm->slots_lock);
}

static int __vcpu_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	int r;

	if (unlikely(vcpu->arch.mp_state == KVM_MP_STATE_SIPI_RECEIVED)) {
		pr_debug("vcpu %d received sipi with vector # %x\n",
		       vcpu->vcpu_id, vcpu->arch.sipi_vector);
		kvm_lapic_reset(vcpu);
		r = kvm_x86_ops->vcpu_reset(vcpu);
		if (r)
			return r;
		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
	}

	down_read(&vcpu->kvm->slots_lock);
	vapic_enter(vcpu);

preempted:
	if (vcpu->guest_debug.enabled)
		kvm_x86_ops->guest_debug_pre(vcpu);

again:
	if (vcpu->requests)
		if (test_and_clear_bit(KVM_REQ_MMU_RELOAD, &vcpu->requests))
			kvm_mmu_unload(vcpu);

	r = kvm_mmu_reload(vcpu);
	if (unlikely(r))
		goto out;

	if (vcpu->requests) {
		if (test_and_clear_bit(KVM_REQ_MIGRATE_TIMER, &vcpu->requests))
			__kvm_migrate_timers(vcpu);
		if (test_and_clear_bit(KVM_REQ_TLB_FLUSH, &vcpu->requests))
			kvm_x86_ops->tlb_flush(vcpu);
		if (test_and_clear_bit(KVM_REQ_REPORT_TPR_ACCESS,
				       &vcpu->requests)) {
			kvm_run->exit_reason = KVM_EXIT_TPR_ACCESS;
			r = 0;
			goto out;
		}
		if (test_and_clear_bit(KVM_REQ_TRIPLE_FAULT, &vcpu->requests)) {
			kvm_run->exit_reason = KVM_EXIT_SHUTDOWN;
			r = 0;
			goto out;
		}
	}

	clear_bit(KVM_REQ_PENDING_TIMER, &vcpu->requests);
	kvm_inject_pending_timer_irqs(vcpu);

	preempt_disable();

	kvm_x86_ops->prepare_guest_switch(vcpu);
	kvm_load_guest_fpu(vcpu);

	local_irq_disable();

	if (vcpu->requests || need_resched()) {
		local_irq_enable();
		preempt_enable();
		r = 1;
		goto out;
	}

	if (signal_pending(current)) {
		local_irq_enable();
		preempt_enable();
		r = -EINTR;
		kvm_run->exit_reason = KVM_EXIT_INTR;
		++vcpu->stat.signal_exits;
		goto out;
	}

	vcpu->guest_mode = 1;
	/*
	 * Make sure that guest_mode assignment won't happen after
	 * testing the pending IRQ vector bitmap.
	 */
	smp_wmb();

	if (vcpu->arch.exception.pending)
		__queue_exception(vcpu);
	else if (irqchip_in_kernel(vcpu->kvm))
		kvm_x86_ops->inject_pending_irq(vcpu);
	else
		kvm_x86_ops->inject_pending_vectors(vcpu, kvm_run);

	kvm_lapic_sync_to_vapic(vcpu);

	up_read(&vcpu->kvm->slots_lock);

	kvm_guest_enter();


	KVMTRACE_0D(VMENTRY, vcpu, entryexit);
	kvm_x86_ops->run(vcpu, kvm_run);

	vcpu->guest_mode = 0;
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

	down_read(&vcpu->kvm->slots_lock);

	/*
	 * Profile KVM exit RIPs:
	 */
	if (unlikely(prof_on == KVM_PROFILING)) {
		kvm_x86_ops->cache_regs(vcpu);
		profile_hit(KVM_PROFILING, (void *)vcpu->arch.rip);
	}

	if (vcpu->arch.exception.pending && kvm_x86_ops->exception_injected(vcpu))
		vcpu->arch.exception.pending = false;

	kvm_lapic_sync_from_vapic(vcpu);

	r = kvm_x86_ops->handle_exit(kvm_run, vcpu);

	if (r > 0) {
		if (dm_request_for_irq_injection(vcpu, kvm_run)) {
			r = -EINTR;
			kvm_run->exit_reason = KVM_EXIT_INTR;
			++vcpu->stat.request_irq_exits;
			goto out;
		}
		if (!need_resched())
			goto again;
	}

out:
	up_read(&vcpu->kvm->slots_lock);
	if (r > 0) {
		kvm_resched(vcpu);
		down_read(&vcpu->kvm->slots_lock);
		goto preempted;
	}

	post_kvm_run_save(vcpu, kvm_run);

	vapic_exit(vcpu);

	return r;
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	int r;
	sigset_t sigsaved;

	vcpu_load(vcpu);

	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &vcpu->sigset, &sigsaved);

	if (unlikely(vcpu->arch.mp_state == KVM_MP_STATE_UNINITIALIZED)) {
		kvm_vcpu_block(vcpu);
		r = -EAGAIN;
		goto out;
	}

	/* re-sync apic's tpr */
	if (!irqchip_in_kernel(vcpu->kvm))
		kvm_set_cr8(vcpu, kvm_run->cr8);

	if (vcpu->arch.pio.cur_count) {
		r = complete_pio(vcpu);
		if (r)
			goto out;
	}
#if CONFIG_HAS_IOMEM
	if (vcpu->mmio_needed) {
		memcpy(vcpu->mmio_data, kvm_run->mmio.data, 8);
		vcpu->mmio_read_completed = 1;
		vcpu->mmio_needed = 0;

		down_read(&vcpu->kvm->slots_lock);
		r = emulate_instruction(vcpu, kvm_run,
					vcpu->arch.mmio_fault_cr2, 0,
					EMULTYPE_NO_DECODE);
		up_read(&vcpu->kvm->slots_lock);
		if (r == EMULATE_DO_MMIO) {
			/*
			 * Read-modify-write.  Back to userspace.
			 */
			r = 0;
			goto out;
		}
	}
#endif
	if (kvm_run->exit_reason == KVM_EXIT_HYPERCALL) {
		kvm_x86_ops->cache_regs(vcpu);
		vcpu->arch.regs[VCPU_REGS_RAX] = kvm_run->hypercall.ret;
		kvm_x86_ops->decache_regs(vcpu);
	}

	r = __vcpu_run(vcpu, kvm_run);

out:
	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	vcpu_put(vcpu);
	return r;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	vcpu_load(vcpu);

	kvm_x86_ops->cache_regs(vcpu);

	regs->rax = vcpu->arch.regs[VCPU_REGS_RAX];
	regs->rbx = vcpu->arch.regs[VCPU_REGS_RBX];
	regs->rcx = vcpu->arch.regs[VCPU_REGS_RCX];
	regs->rdx = vcpu->arch.regs[VCPU_REGS_RDX];
	regs->rsi = vcpu->arch.regs[VCPU_REGS_RSI];
	regs->rdi = vcpu->arch.regs[VCPU_REGS_RDI];
	regs->rsp = vcpu->arch.regs[VCPU_REGS_RSP];
	regs->rbp = vcpu->arch.regs[VCPU_REGS_RBP];
#ifdef CONFIG_X86_64
	regs->r8 = vcpu->arch.regs[VCPU_REGS_R8];
	regs->r9 = vcpu->arch.regs[VCPU_REGS_R9];
	regs->r10 = vcpu->arch.regs[VCPU_REGS_R10];
	regs->r11 = vcpu->arch.regs[VCPU_REGS_R11];
	regs->r12 = vcpu->arch.regs[VCPU_REGS_R12];
	regs->r13 = vcpu->arch.regs[VCPU_REGS_R13];
	regs->r14 = vcpu->arch.regs[VCPU_REGS_R14];
	regs->r15 = vcpu->arch.regs[VCPU_REGS_R15];
#endif

	regs->rip = vcpu->arch.rip;
	regs->rflags = kvm_x86_ops->get_rflags(vcpu);

	/*
	 * Don't leak debug flags in case they were set for guest debugging
	 */
	if (vcpu->guest_debug.enabled && vcpu->guest_debug.singlestep)
		regs->rflags &= ~(X86_EFLAGS_TF | X86_EFLAGS_RF);

	vcpu_put(vcpu);

	return 0;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	vcpu_load(vcpu);

	vcpu->arch.regs[VCPU_REGS_RAX] = regs->rax;
	vcpu->arch.regs[VCPU_REGS_RBX] = regs->rbx;
	vcpu->arch.regs[VCPU_REGS_RCX] = regs->rcx;
	vcpu->arch.regs[VCPU_REGS_RDX] = regs->rdx;
	vcpu->arch.regs[VCPU_REGS_RSI] = regs->rsi;
	vcpu->arch.regs[VCPU_REGS_RDI] = regs->rdi;
	vcpu->arch.regs[VCPU_REGS_RSP] = regs->rsp;
	vcpu->arch.regs[VCPU_REGS_RBP] = regs->rbp;
#ifdef CONFIG_X86_64
	vcpu->arch.regs[VCPU_REGS_R8] = regs->r8;
	vcpu->arch.regs[VCPU_REGS_R9] = regs->r9;
	vcpu->arch.regs[VCPU_REGS_R10] = regs->r10;
	vcpu->arch.regs[VCPU_REGS_R11] = regs->r11;
	vcpu->arch.regs[VCPU_REGS_R12] = regs->r12;
	vcpu->arch.regs[VCPU_REGS_R13] = regs->r13;
	vcpu->arch.regs[VCPU_REGS_R14] = regs->r14;
	vcpu->arch.regs[VCPU_REGS_R15] = regs->r15;
#endif

	vcpu->arch.rip = regs->rip;
	kvm_x86_ops->set_rflags(vcpu, regs->rflags);

	kvm_x86_ops->decache_regs(vcpu);

	vcpu->arch.exception.pending = false;

	vcpu_put(vcpu);

	return 0;
}

void kvm_get_segment(struct kvm_vcpu *vcpu,
		     struct kvm_segment *var, int seg)
{
	kvm_x86_ops->get_segment(vcpu, var, seg);
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
	struct descriptor_table dt;
	int pending_vec;

	vcpu_load(vcpu);

	kvm_get_segment(vcpu, &sregs->cs, VCPU_SREG_CS);
	kvm_get_segment(vcpu, &sregs->ds, VCPU_SREG_DS);
	kvm_get_segment(vcpu, &sregs->es, VCPU_SREG_ES);
	kvm_get_segment(vcpu, &sregs->fs, VCPU_SREG_FS);
	kvm_get_segment(vcpu, &sregs->gs, VCPU_SREG_GS);
	kvm_get_segment(vcpu, &sregs->ss, VCPU_SREG_SS);

	kvm_get_segment(vcpu, &sregs->tr, VCPU_SREG_TR);
	kvm_get_segment(vcpu, &sregs->ldt, VCPU_SREG_LDTR);

	kvm_x86_ops->get_idt(vcpu, &dt);
	sregs->idt.limit = dt.limit;
	sregs->idt.base = dt.base;
	kvm_x86_ops->get_gdt(vcpu, &dt);
	sregs->gdt.limit = dt.limit;
	sregs->gdt.base = dt.base;

	kvm_x86_ops->decache_cr4_guest_bits(vcpu);
	sregs->cr0 = vcpu->arch.cr0;
	sregs->cr2 = vcpu->arch.cr2;
	sregs->cr3 = vcpu->arch.cr3;
	sregs->cr4 = vcpu->arch.cr4;
	sregs->cr8 = kvm_get_cr8(vcpu);
	sregs->efer = vcpu->arch.shadow_efer;
	sregs->apic_base = kvm_get_apic_base(vcpu);

	if (irqchip_in_kernel(vcpu->kvm)) {
		memset(sregs->interrupt_bitmap, 0,
		       sizeof sregs->interrupt_bitmap);
		pending_vec = kvm_x86_ops->get_irq(vcpu);
		if (pending_vec >= 0)
			set_bit(pending_vec,
				(unsigned long *)sregs->interrupt_bitmap);
	} else
		memcpy(sregs->interrupt_bitmap, vcpu->arch.irq_pending,
		       sizeof sregs->interrupt_bitmap);

	vcpu_put(vcpu);

	return 0;
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	vcpu_load(vcpu);
	mp_state->mp_state = vcpu->arch.mp_state;
	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	vcpu_load(vcpu);
	vcpu->arch.mp_state = mp_state->mp_state;
	vcpu_put(vcpu);
	return 0;
}

static void kvm_set_segment(struct kvm_vcpu *vcpu,
			struct kvm_segment *var, int seg)
{
	kvm_x86_ops->set_segment(vcpu, var, seg);
}

static void seg_desct_to_kvm_desct(struct desc_struct *seg_desc, u16 selector,
				   struct kvm_segment *kvm_desct)
{
	kvm_desct->base = seg_desc->base0;
	kvm_desct->base |= seg_desc->base1 << 16;
	kvm_desct->base |= seg_desc->base2 << 24;
	kvm_desct->limit = seg_desc->limit0;
	kvm_desct->limit |= seg_desc->limit << 16;
	if (seg_desc->g) {
		kvm_desct->limit <<= 12;
		kvm_desct->limit |= 0xfff;
	}
	kvm_desct->selector = selector;
	kvm_desct->type = seg_desc->type;
	kvm_desct->present = seg_desc->p;
	kvm_desct->dpl = seg_desc->dpl;
	kvm_desct->db = seg_desc->d;
	kvm_desct->s = seg_desc->s;
	kvm_desct->l = seg_desc->l;
	kvm_desct->g = seg_desc->g;
	kvm_desct->avl = seg_desc->avl;
	if (!selector)
		kvm_desct->unusable = 1;
	else
		kvm_desct->unusable = 0;
	kvm_desct->padding = 0;
}

static void get_segment_descritptor_dtable(struct kvm_vcpu *vcpu,
					   u16 selector,
					   struct descriptor_table *dtable)
{
	if (selector & 1 << 2) {
		struct kvm_segment kvm_seg;

		kvm_get_segment(vcpu, &kvm_seg, VCPU_SREG_LDTR);

		if (kvm_seg.unusable)
			dtable->limit = 0;
		else
			dtable->limit = kvm_seg.limit;
		dtable->base = kvm_seg.base;
	}
	else
		kvm_x86_ops->get_gdt(vcpu, dtable);
}

/* allowed just for 8 bytes segments */
static int load_guest_segment_descriptor(struct kvm_vcpu *vcpu, u16 selector,
					 struct desc_struct *seg_desc)
{
	gpa_t gpa;
	struct descriptor_table dtable;
	u16 index = selector >> 3;

	get_segment_descritptor_dtable(vcpu, selector, &dtable);

	if (dtable.limit < index * 8 + 7) {
		kvm_queue_exception_e(vcpu, GP_VECTOR, selector & 0xfffc);
		return 1;
	}
	gpa = vcpu->arch.mmu.gva_to_gpa(vcpu, dtable.base);
	gpa += index * 8;
	return kvm_read_guest(vcpu->kvm, gpa, seg_desc, 8);
}

/* allowed just for 8 bytes segments */
static int save_guest_segment_descriptor(struct kvm_vcpu *vcpu, u16 selector,
					 struct desc_struct *seg_desc)
{
	gpa_t gpa;
	struct descriptor_table dtable;
	u16 index = selector >> 3;

	get_segment_descritptor_dtable(vcpu, selector, &dtable);

	if (dtable.limit < index * 8 + 7)
		return 1;
	gpa = vcpu->arch.mmu.gva_to_gpa(vcpu, dtable.base);
	gpa += index * 8;
	return kvm_write_guest(vcpu->kvm, gpa, seg_desc, 8);
}

static u32 get_tss_base_addr(struct kvm_vcpu *vcpu,
			     struct desc_struct *seg_desc)
{
	u32 base_addr;

	base_addr = seg_desc->base0;
	base_addr |= (seg_desc->base1 << 16);
	base_addr |= (seg_desc->base2 << 24);

	return vcpu->arch.mmu.gva_to_gpa(vcpu, base_addr);
}

static u16 get_segment_selector(struct kvm_vcpu *vcpu, int seg)
{
	struct kvm_segment kvm_seg;

	kvm_get_segment(vcpu, &kvm_seg, seg);
	return kvm_seg.selector;
}

static int load_segment_descriptor_to_kvm_desct(struct kvm_vcpu *vcpu,
						u16 selector,
						struct kvm_segment *kvm_seg)
{
	struct desc_struct seg_desc;

	if (load_guest_segment_descriptor(vcpu, selector, &seg_desc))
		return 1;
	seg_desct_to_kvm_desct(&seg_desc, selector, kvm_seg);
	return 0;
}

int kvm_load_segment_descriptor(struct kvm_vcpu *vcpu, u16 selector,
				int type_bits, int seg)
{
	struct kvm_segment kvm_seg;

	if (load_segment_descriptor_to_kvm_desct(vcpu, selector, &kvm_seg))
		return 1;
	kvm_seg.type |= type_bits;

	if (seg != VCPU_SREG_SS && seg != VCPU_SREG_CS &&
	    seg != VCPU_SREG_LDTR)
		if (!kvm_seg.s)
			kvm_seg.unusable = 1;

	kvm_set_segment(vcpu, &kvm_seg, seg);
	return 0;
}

static void save_state_to_tss32(struct kvm_vcpu *vcpu,
				struct tss_segment_32 *tss)
{
	tss->cr3 = vcpu->arch.cr3;
	tss->eip = vcpu->arch.rip;
	tss->eflags = kvm_x86_ops->get_rflags(vcpu);
	tss->eax = vcpu->arch.regs[VCPU_REGS_RAX];
	tss->ecx = vcpu->arch.regs[VCPU_REGS_RCX];
	tss->edx = vcpu->arch.regs[VCPU_REGS_RDX];
	tss->ebx = vcpu->arch.regs[VCPU_REGS_RBX];
	tss->esp = vcpu->arch.regs[VCPU_REGS_RSP];
	tss->ebp = vcpu->arch.regs[VCPU_REGS_RBP];
	tss->esi = vcpu->arch.regs[VCPU_REGS_RSI];
	tss->edi = vcpu->arch.regs[VCPU_REGS_RDI];

	tss->es = get_segment_selector(vcpu, VCPU_SREG_ES);
	tss->cs = get_segment_selector(vcpu, VCPU_SREG_CS);
	tss->ss = get_segment_selector(vcpu, VCPU_SREG_SS);
	tss->ds = get_segment_selector(vcpu, VCPU_SREG_DS);
	tss->fs = get_segment_selector(vcpu, VCPU_SREG_FS);
	tss->gs = get_segment_selector(vcpu, VCPU_SREG_GS);
	tss->ldt_selector = get_segment_selector(vcpu, VCPU_SREG_LDTR);
	tss->prev_task_link = get_segment_selector(vcpu, VCPU_SREG_TR);
}

static int load_state_from_tss32(struct kvm_vcpu *vcpu,
				  struct tss_segment_32 *tss)
{
	kvm_set_cr3(vcpu, tss->cr3);

	vcpu->arch.rip = tss->eip;
	kvm_x86_ops->set_rflags(vcpu, tss->eflags | 2);

	vcpu->arch.regs[VCPU_REGS_RAX] = tss->eax;
	vcpu->arch.regs[VCPU_REGS_RCX] = tss->ecx;
	vcpu->arch.regs[VCPU_REGS_RDX] = tss->edx;
	vcpu->arch.regs[VCPU_REGS_RBX] = tss->ebx;
	vcpu->arch.regs[VCPU_REGS_RSP] = tss->esp;
	vcpu->arch.regs[VCPU_REGS_RBP] = tss->ebp;
	vcpu->arch.regs[VCPU_REGS_RSI] = tss->esi;
	vcpu->arch.regs[VCPU_REGS_RDI] = tss->edi;

	if (kvm_load_segment_descriptor(vcpu, tss->ldt_selector, 0, VCPU_SREG_LDTR))
		return 1;

	if (kvm_load_segment_descriptor(vcpu, tss->es, 1, VCPU_SREG_ES))
		return 1;

	if (kvm_load_segment_descriptor(vcpu, tss->cs, 9, VCPU_SREG_CS))
		return 1;

	if (kvm_load_segment_descriptor(vcpu, tss->ss, 1, VCPU_SREG_SS))
		return 1;

	if (kvm_load_segment_descriptor(vcpu, tss->ds, 1, VCPU_SREG_DS))
		return 1;

	if (kvm_load_segment_descriptor(vcpu, tss->fs, 1, VCPU_SREG_FS))
		return 1;

	if (kvm_load_segment_descriptor(vcpu, tss->gs, 1, VCPU_SREG_GS))
		return 1;
	return 0;
}

static void save_state_to_tss16(struct kvm_vcpu *vcpu,
				struct tss_segment_16 *tss)
{
	tss->ip = vcpu->arch.rip;
	tss->flag = kvm_x86_ops->get_rflags(vcpu);
	tss->ax = vcpu->arch.regs[VCPU_REGS_RAX];
	tss->cx = vcpu->arch.regs[VCPU_REGS_RCX];
	tss->dx = vcpu->arch.regs[VCPU_REGS_RDX];
	tss->bx = vcpu->arch.regs[VCPU_REGS_RBX];
	tss->sp = vcpu->arch.regs[VCPU_REGS_RSP];
	tss->bp = vcpu->arch.regs[VCPU_REGS_RBP];
	tss->si = vcpu->arch.regs[VCPU_REGS_RSI];
	tss->di = vcpu->arch.regs[VCPU_REGS_RDI];

	tss->es = get_segment_selector(vcpu, VCPU_SREG_ES);
	tss->cs = get_segment_selector(vcpu, VCPU_SREG_CS);
	tss->ss = get_segment_selector(vcpu, VCPU_SREG_SS);
	tss->ds = get_segment_selector(vcpu, VCPU_SREG_DS);
	tss->ldt = get_segment_selector(vcpu, VCPU_SREG_LDTR);
	tss->prev_task_link = get_segment_selector(vcpu, VCPU_SREG_TR);
}

static int load_state_from_tss16(struct kvm_vcpu *vcpu,
				 struct tss_segment_16 *tss)
{
	vcpu->arch.rip = tss->ip;
	kvm_x86_ops->set_rflags(vcpu, tss->flag | 2);
	vcpu->arch.regs[VCPU_REGS_RAX] = tss->ax;
	vcpu->arch.regs[VCPU_REGS_RCX] = tss->cx;
	vcpu->arch.regs[VCPU_REGS_RDX] = tss->dx;
	vcpu->arch.regs[VCPU_REGS_RBX] = tss->bx;
	vcpu->arch.regs[VCPU_REGS_RSP] = tss->sp;
	vcpu->arch.regs[VCPU_REGS_RBP] = tss->bp;
	vcpu->arch.regs[VCPU_REGS_RSI] = tss->si;
	vcpu->arch.regs[VCPU_REGS_RDI] = tss->di;

	if (kvm_load_segment_descriptor(vcpu, tss->ldt, 0, VCPU_SREG_LDTR))
		return 1;

	if (kvm_load_segment_descriptor(vcpu, tss->es, 1, VCPU_SREG_ES))
		return 1;

	if (kvm_load_segment_descriptor(vcpu, tss->cs, 9, VCPU_SREG_CS))
		return 1;

	if (kvm_load_segment_descriptor(vcpu, tss->ss, 1, VCPU_SREG_SS))
		return 1;

	if (kvm_load_segment_descriptor(vcpu, tss->ds, 1, VCPU_SREG_DS))
		return 1;
	return 0;
}

static int kvm_task_switch_16(struct kvm_vcpu *vcpu, u16 tss_selector,
		       u32 old_tss_base,
		       struct desc_struct *nseg_desc)
{
	struct tss_segment_16 tss_segment_16;
	int ret = 0;

	if (kvm_read_guest(vcpu->kvm, old_tss_base, &tss_segment_16,
			   sizeof tss_segment_16))
		goto out;

	save_state_to_tss16(vcpu, &tss_segment_16);

	if (kvm_write_guest(vcpu->kvm, old_tss_base, &tss_segment_16,
			    sizeof tss_segment_16))
		goto out;

	if (kvm_read_guest(vcpu->kvm, get_tss_base_addr(vcpu, nseg_desc),
			   &tss_segment_16, sizeof tss_segment_16))
		goto out;

	if (load_state_from_tss16(vcpu, &tss_segment_16))
		goto out;

	ret = 1;
out:
	return ret;
}

static int kvm_task_switch_32(struct kvm_vcpu *vcpu, u16 tss_selector,
		       u32 old_tss_base,
		       struct desc_struct *nseg_desc)
{
	struct tss_segment_32 tss_segment_32;
	int ret = 0;

	if (kvm_read_guest(vcpu->kvm, old_tss_base, &tss_segment_32,
			   sizeof tss_segment_32))
		goto out;

	save_state_to_tss32(vcpu, &tss_segment_32);

	if (kvm_write_guest(vcpu->kvm, old_tss_base, &tss_segment_32,
			    sizeof tss_segment_32))
		goto out;

	if (kvm_read_guest(vcpu->kvm, get_tss_base_addr(vcpu, nseg_desc),
			   &tss_segment_32, sizeof tss_segment_32))
		goto out;

	if (load_state_from_tss32(vcpu, &tss_segment_32))
		goto out;

	ret = 1;
out:
	return ret;
}

int kvm_task_switch(struct kvm_vcpu *vcpu, u16 tss_selector, int reason)
{
	struct kvm_segment tr_seg;
	struct desc_struct cseg_desc;
	struct desc_struct nseg_desc;
	int ret = 0;
	u32 old_tss_base = get_segment_base(vcpu, VCPU_SREG_TR);
	u16 old_tss_sel = get_segment_selector(vcpu, VCPU_SREG_TR);

	old_tss_base = vcpu->arch.mmu.gva_to_gpa(vcpu, old_tss_base);

	/* FIXME: Handle errors. Failure to read either TSS or their
	 * descriptors should generate a pagefault.
	 */
	if (load_guest_segment_descriptor(vcpu, tss_selector, &nseg_desc))
		goto out;

	if (load_guest_segment_descriptor(vcpu, old_tss_sel, &cseg_desc))
		goto out;

	if (reason != TASK_SWITCH_IRET) {
		int cpl;

		cpl = kvm_x86_ops->get_cpl(vcpu);
		if ((tss_selector & 3) > nseg_desc.dpl || cpl > nseg_desc.dpl) {
			kvm_queue_exception_e(vcpu, GP_VECTOR, 0);
			return 1;
		}
	}

	if (!nseg_desc.p || (nseg_desc.limit0 | nseg_desc.limit << 16) < 0x67) {
		kvm_queue_exception_e(vcpu, TS_VECTOR, tss_selector & 0xfffc);
		return 1;
	}

	if (reason == TASK_SWITCH_IRET || reason == TASK_SWITCH_JMP) {
		cseg_desc.type &= ~(1 << 1); //clear the B flag
		save_guest_segment_descriptor(vcpu, old_tss_sel, &cseg_desc);
	}

	if (reason == TASK_SWITCH_IRET) {
		u32 eflags = kvm_x86_ops->get_rflags(vcpu);
		kvm_x86_ops->set_rflags(vcpu, eflags & ~X86_EFLAGS_NT);
	}

	kvm_x86_ops->skip_emulated_instruction(vcpu);
	kvm_x86_ops->cache_regs(vcpu);

	if (nseg_desc.type & 8)
		ret = kvm_task_switch_32(vcpu, tss_selector, old_tss_base,
					 &nseg_desc);
	else
		ret = kvm_task_switch_16(vcpu, tss_selector, old_tss_base,
					 &nseg_desc);

	if (reason == TASK_SWITCH_CALL || reason == TASK_SWITCH_GATE) {
		u32 eflags = kvm_x86_ops->get_rflags(vcpu);
		kvm_x86_ops->set_rflags(vcpu, eflags | X86_EFLAGS_NT);
	}

	if (reason != TASK_SWITCH_IRET) {
		nseg_desc.type |= (1 << 1);
		save_guest_segment_descriptor(vcpu, tss_selector,
					      &nseg_desc);
	}

	kvm_x86_ops->set_cr0(vcpu, vcpu->arch.cr0 | X86_CR0_TS);
	seg_desct_to_kvm_desct(&nseg_desc, tss_selector, &tr_seg);
	tr_seg.type = 11;
	kvm_set_segment(vcpu, &tr_seg, VCPU_SREG_TR);
out:
	kvm_x86_ops->decache_regs(vcpu);
	return ret;
}
EXPORT_SYMBOL_GPL(kvm_task_switch);

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	int mmu_reset_needed = 0;
	int i, pending_vec, max_bits;
	struct descriptor_table dt;

	vcpu_load(vcpu);

	dt.limit = sregs->idt.limit;
	dt.base = sregs->idt.base;
	kvm_x86_ops->set_idt(vcpu, &dt);
	dt.limit = sregs->gdt.limit;
	dt.base = sregs->gdt.base;
	kvm_x86_ops->set_gdt(vcpu, &dt);

	vcpu->arch.cr2 = sregs->cr2;
	mmu_reset_needed |= vcpu->arch.cr3 != sregs->cr3;
	vcpu->arch.cr3 = sregs->cr3;

	kvm_set_cr8(vcpu, sregs->cr8);

	mmu_reset_needed |= vcpu->arch.shadow_efer != sregs->efer;
	kvm_x86_ops->set_efer(vcpu, sregs->efer);
	kvm_set_apic_base(vcpu, sregs->apic_base);

	kvm_x86_ops->decache_cr4_guest_bits(vcpu);

	mmu_reset_needed |= vcpu->arch.cr0 != sregs->cr0;
	kvm_x86_ops->set_cr0(vcpu, sregs->cr0);
	vcpu->arch.cr0 = sregs->cr0;

	mmu_reset_needed |= vcpu->arch.cr4 != sregs->cr4;
	kvm_x86_ops->set_cr4(vcpu, sregs->cr4);
	if (!is_long_mode(vcpu) && is_pae(vcpu))
		load_pdptrs(vcpu, vcpu->arch.cr3);

	if (mmu_reset_needed)
		kvm_mmu_reset_context(vcpu);

	if (!irqchip_in_kernel(vcpu->kvm)) {
		memcpy(vcpu->arch.irq_pending, sregs->interrupt_bitmap,
		       sizeof vcpu->arch.irq_pending);
		vcpu->arch.irq_summary = 0;
		for (i = 0; i < ARRAY_SIZE(vcpu->arch.irq_pending); ++i)
			if (vcpu->arch.irq_pending[i])
				__set_bit(i, &vcpu->arch.irq_summary);
	} else {
		max_bits = (sizeof sregs->interrupt_bitmap) << 3;
		pending_vec = find_first_bit(
			(const unsigned long *)sregs->interrupt_bitmap,
			max_bits);
		/* Only pending external irq is handled here */
		if (pending_vec < max_bits) {
			kvm_x86_ops->set_irq(vcpu, pending_vec);
			pr_debug("Set back pending irq %d\n",
				 pending_vec);
		}
	}

	kvm_set_segment(vcpu, &sregs->cs, VCPU_SREG_CS);
	kvm_set_segment(vcpu, &sregs->ds, VCPU_SREG_DS);
	kvm_set_segment(vcpu, &sregs->es, VCPU_SREG_ES);
	kvm_set_segment(vcpu, &sregs->fs, VCPU_SREG_FS);
	kvm_set_segment(vcpu, &sregs->gs, VCPU_SREG_GS);
	kvm_set_segment(vcpu, &sregs->ss, VCPU_SREG_SS);

	kvm_set_segment(vcpu, &sregs->tr, VCPU_SREG_TR);
	kvm_set_segment(vcpu, &sregs->ldt, VCPU_SREG_LDTR);

	vcpu_put(vcpu);

	return 0;
}

int kvm_arch_vcpu_ioctl_debug_guest(struct kvm_vcpu *vcpu,
				    struct kvm_debug_guest *dbg)
{
	int r;

	vcpu_load(vcpu);

	r = kvm_x86_ops->set_guest_debug(vcpu, dbg);

	vcpu_put(vcpu);

	return r;
}

/*
 * fxsave fpu state.  Taken from x86_64/processor.h.  To be killed when
 * we have asm/x86/processor.h
 */
struct fxsave {
	u16	cwd;
	u16	swd;
	u16	twd;
	u16	fop;
	u64	rip;
	u64	rdp;
	u32	mxcsr;
	u32	mxcsr_mask;
	u32	st_space[32];	/* 8*16 bytes for each FP-reg = 128 bytes */
#ifdef CONFIG_X86_64
	u32	xmm_space[64];	/* 16*16 bytes for each XMM-reg = 256 bytes */
#else
	u32	xmm_space[32];	/* 8*16 bytes for each XMM-reg = 128 bytes */
#endif
};

/*
 * Translate a guest virtual address to a guest physical address.
 */
int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				    struct kvm_translation *tr)
{
	unsigned long vaddr = tr->linear_address;
	gpa_t gpa;

	vcpu_load(vcpu);
	down_read(&vcpu->kvm->slots_lock);
	gpa = vcpu->arch.mmu.gva_to_gpa(vcpu, vaddr);
	up_read(&vcpu->kvm->slots_lock);
	tr->physical_address = gpa;
	tr->valid = gpa != UNMAPPED_GVA;
	tr->writeable = 1;
	tr->usermode = 0;
	vcpu_put(vcpu);

	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	struct fxsave *fxsave = (struct fxsave *)&vcpu->arch.guest_fx_image;

	vcpu_load(vcpu);

	memcpy(fpu->fpr, fxsave->st_space, 128);
	fpu->fcw = fxsave->cwd;
	fpu->fsw = fxsave->swd;
	fpu->ftwx = fxsave->twd;
	fpu->last_opcode = fxsave->fop;
	fpu->last_ip = fxsave->rip;
	fpu->last_dp = fxsave->rdp;
	memcpy(fpu->xmm, fxsave->xmm_space, sizeof fxsave->xmm_space);

	vcpu_put(vcpu);

	return 0;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	struct fxsave *fxsave = (struct fxsave *)&vcpu->arch.guest_fx_image;

	vcpu_load(vcpu);

	memcpy(fxsave->st_space, fpu->fpr, 128);
	fxsave->cwd = fpu->fcw;
	fxsave->swd = fpu->fsw;
	fxsave->twd = fpu->ftwx;
	fxsave->fop = fpu->last_opcode;
	fxsave->rip = fpu->last_ip;
	fxsave->rdp = fpu->last_dp;
	memcpy(fxsave->xmm_space, fpu->xmm, sizeof fxsave->xmm_space);

	vcpu_put(vcpu);

	return 0;
}

void fx_init(struct kvm_vcpu *vcpu)
{
	unsigned after_mxcsr_mask;

	/*
	 * Touch the fpu the first time in non atomic context as if
	 * this is the first fpu instruction the exception handler
	 * will fire before the instruction returns and it'll have to
	 * allocate ram with GFP_KERNEL.
	 */
	if (!used_math())
		kvm_fx_save(&vcpu->arch.host_fx_image);

	/* Initialize guest FPU by resetting ours and saving into guest's */
	preempt_disable();
	kvm_fx_save(&vcpu->arch.host_fx_image);
	kvm_fx_finit();
	kvm_fx_save(&vcpu->arch.guest_fx_image);
	kvm_fx_restore(&vcpu->arch.host_fx_image);
	preempt_enable();

	vcpu->arch.cr0 |= X86_CR0_ET;
	after_mxcsr_mask = offsetof(struct i387_fxsave_struct, st_space);
	vcpu->arch.guest_fx_image.mxcsr = 0x1f80;
	memset((void *)&vcpu->arch.guest_fx_image + after_mxcsr_mask,
	       0, sizeof(struct i387_fxsave_struct) - after_mxcsr_mask);
}
EXPORT_SYMBOL_GPL(fx_init);

void kvm_load_guest_fpu(struct kvm_vcpu *vcpu)
{
	if (!vcpu->fpu_active || vcpu->guest_fpu_loaded)
		return;

	vcpu->guest_fpu_loaded = 1;
	kvm_fx_save(&vcpu->arch.host_fx_image);
	kvm_fx_restore(&vcpu->arch.guest_fx_image);
}
EXPORT_SYMBOL_GPL(kvm_load_guest_fpu);

void kvm_put_guest_fpu(struct kvm_vcpu *vcpu)
{
	if (!vcpu->guest_fpu_loaded)
		return;

	vcpu->guest_fpu_loaded = 0;
	kvm_fx_save(&vcpu->arch.guest_fx_image);
	kvm_fx_restore(&vcpu->arch.host_fx_image);
	++vcpu->stat.fpu_reload;
}
EXPORT_SYMBOL_GPL(kvm_put_guest_fpu);

void kvm_arch_vcpu_free(struct kvm_vcpu *vcpu)
{
	kvm_x86_ops->vcpu_free(vcpu);
}

struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm,
						unsigned int id)
{
	return kvm_x86_ops->vcpu_create(kvm, id);
}

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	int r;

	/* We do fxsave: this must be aligned. */
	BUG_ON((unsigned long)&vcpu->arch.host_fx_image & 0xF);

	vcpu_load(vcpu);
	r = kvm_arch_vcpu_reset(vcpu);
	if (r == 0)
		r = kvm_mmu_setup(vcpu);
	vcpu_put(vcpu);
	if (r < 0)
		goto free_vcpu;

	return 0;
free_vcpu:
	kvm_x86_ops->vcpu_free(vcpu);
	return r;
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	vcpu_load(vcpu);
	kvm_mmu_unload(vcpu);
	vcpu_put(vcpu);

	kvm_x86_ops->vcpu_free(vcpu);
}

int kvm_arch_vcpu_reset(struct kvm_vcpu *vcpu)
{
	return kvm_x86_ops->vcpu_reset(vcpu);
}

void kvm_arch_hardware_enable(void *garbage)
{
	kvm_x86_ops->hardware_enable(garbage);
}

void kvm_arch_hardware_disable(void *garbage)
{
	kvm_x86_ops->hardware_disable(garbage);
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

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
	struct page *page;
	struct kvm *kvm;
	int r;

	BUG_ON(vcpu->kvm == NULL);
	kvm = vcpu->kvm;

	vcpu->arch.mmu.root_hpa = INVALID_PAGE;
	if (!irqchip_in_kernel(kvm) || vcpu->vcpu_id == 0)
		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
	else
		vcpu->arch.mp_state = KVM_MP_STATE_UNINITIALIZED;

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page) {
		r = -ENOMEM;
		goto fail;
	}
	vcpu->arch.pio_data = page_address(page);

	r = kvm_mmu_create(vcpu);
	if (r < 0)
		goto fail_free_pio_data;

	if (irqchip_in_kernel(kvm)) {
		r = kvm_create_lapic(vcpu);
		if (r < 0)
			goto fail_mmu_destroy;
	}

	return 0;

fail_mmu_destroy:
	kvm_mmu_destroy(vcpu);
fail_free_pio_data:
	free_page((unsigned long)vcpu->arch.pio_data);
fail:
	return r;
}

void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu)
{
	kvm_free_lapic(vcpu);
	down_read(&vcpu->kvm->slots_lock);
	kvm_mmu_destroy(vcpu);
	up_read(&vcpu->kvm->slots_lock);
	free_page((unsigned long)vcpu->arch.pio_data);
}

struct  kvm *kvm_arch_create_vm(void)
{
	struct kvm *kvm = kzalloc(sizeof(struct kvm), GFP_KERNEL);

	if (!kvm)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&kvm->arch.active_mmu_pages);

	return kvm;
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

	/*
	 * Unpin any mmu pages first.
	 */
	for (i = 0; i < KVM_MAX_VCPUS; ++i)
		if (kvm->vcpus[i])
			kvm_unload_vcpu_mmu(kvm->vcpus[i]);
	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		if (kvm->vcpus[i]) {
			kvm_arch_vcpu_free(kvm->vcpus[i]);
			kvm->vcpus[i] = NULL;
		}
	}

}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	kvm_free_pit(kvm);
	kfree(kvm->arch.vpic);
	kfree(kvm->arch.vioapic);
	kvm_free_vcpus(kvm);
	kvm_free_physmem(kvm);
	if (kvm->arch.apic_access_page)
		put_page(kvm->arch.apic_access_page);
	if (kvm->arch.ept_identity_pagetable)
		put_page(kvm->arch.ept_identity_pagetable);
	kfree(kvm);
}

int kvm_arch_set_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem,
				struct kvm_memory_slot old,
				int user_alloc)
{
	int npages = mem->memory_size >> PAGE_SHIFT;
	struct kvm_memory_slot *memslot = &kvm->memslots[mem->slot];

	/*To keep backward compatibility with older userspace,
	 *x86 needs to hanlde !user_alloc case.
	 */
	if (!user_alloc) {
		if (npages && !old.rmap) {
			unsigned long userspace_addr;

			down_write(&current->mm->mmap_sem);
			userspace_addr = do_mmap(NULL, 0,
						 npages * PAGE_SIZE,
						 PROT_READ | PROT_WRITE,
						 MAP_SHARED | MAP_ANONYMOUS,
						 0);
			up_write(&current->mm->mmap_sem);

			if (IS_ERR((void *)userspace_addr))
				return PTR_ERR((void *)userspace_addr);

			/* set userspace_addr atomically for kvm_hva_to_rmapp */
			spin_lock(&kvm->mmu_lock);
			memslot->userspace_addr = userspace_addr;
			spin_unlock(&kvm->mmu_lock);
		} else {
			if (!old.user_alloc && old.rmap) {
				int ret;

				down_write(&current->mm->mmap_sem);
				ret = do_munmap(current->mm, old.userspace_addr,
						old.npages * PAGE_SIZE);
				up_write(&current->mm->mmap_sem);
				if (ret < 0)
					printk(KERN_WARNING
				       "kvm_vm_ioctl_set_memory_region: "
				       "failed to munmap memory\n");
			}
		}
	}

	if (!kvm->arch.n_requested_mmu_pages) {
		unsigned int nr_mmu_pages = kvm_mmu_calculate_mmu_pages(kvm);
		kvm_mmu_change_mmu_pages(kvm, nr_mmu_pages);
	}

	kvm_mmu_slot_remove_write_access(kvm, mem->slot);
	kvm_flush_remote_tlbs(kvm);

	return 0;
}

void kvm_arch_flush_shadow(struct kvm *kvm)
{
	kvm_mmu_zap_all(kvm);
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.mp_state == KVM_MP_STATE_RUNNABLE
	       || vcpu->arch.mp_state == KVM_MP_STATE_SIPI_RECEIVED;
}

static void vcpu_kick_intr(void *info)
{
#ifdef DEBUG
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *)info;
	printk(KERN_DEBUG "vcpu_kick_intr %p \n", vcpu);
#endif
}

void kvm_vcpu_kick(struct kvm_vcpu *vcpu)
{
	int ipi_pcpu = vcpu->cpu;
	int cpu = get_cpu();

	if (waitqueue_active(&vcpu->wq)) {
		wake_up_interruptible(&vcpu->wq);
		++vcpu->stat.halt_wakeup;
	}
	/*
	 * We may be called synchronously with irqs disabled in guest mode,
	 * So need not to call smp_call_function_single() in that case.
	 */
	if (vcpu->guest_mode && vcpu->cpu != cpu)
		smp_call_function_single(ipi_pcpu, vcpu_kick_intr, vcpu, 0);
	put_cpu();
}

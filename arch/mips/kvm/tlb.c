/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS TLB handling, this file is part of the Linux host kernel so that
 * TLB handlers run from KSEG0
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/kvm_host.h>
#include <linux/srcu.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/tlbdebug.h>

#undef CONFIG_MIPS_MT
#include <asm/r4kcache.h>
#define CONFIG_MIPS_MT

#define KVM_GUEST_PC_TLB    0
#define KVM_GUEST_SP_TLB    1

static u32 kvm_mips_get_kernel_asid(struct kvm_vcpu *vcpu)
{
	struct mm_struct *kern_mm = &vcpu->arch.guest_kernel_mm;
	int cpu = smp_processor_id();

	return cpu_asid(cpu, kern_mm);
}

static u32 kvm_mips_get_user_asid(struct kvm_vcpu *vcpu)
{
	struct mm_struct *user_mm = &vcpu->arch.guest_user_mm;
	int cpu = smp_processor_id();

	return cpu_asid(cpu, user_mm);
}

/* Structure defining an tlb entry data set. */

void kvm_mips_dump_host_tlbs(void)
{
	unsigned long flags;

	local_irq_save(flags);

	kvm_info("HOST TLBs:\n");
	dump_tlb_regs();
	pr_info("\n");
	dump_tlb_all();

	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(kvm_mips_dump_host_tlbs);

void kvm_mips_dump_guest_tlbs(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_mips_tlb tlb;
	int i;

	kvm_info("Guest TLBs:\n");
	kvm_info("Guest EntryHi: %#lx\n", kvm_read_c0_guest_entryhi(cop0));

	for (i = 0; i < KVM_MIPS_GUEST_TLB_SIZE; i++) {
		tlb = vcpu->arch.guest_tlb[i];
		kvm_info("TLB%c%3d Hi 0x%08lx ",
			 (tlb.tlb_lo[0] | tlb.tlb_lo[1]) & ENTRYLO_V
							? ' ' : '*',
			 i, tlb.tlb_hi);
		kvm_info("Lo0=0x%09llx %c%c attr %lx ",
			 (u64) mips3_tlbpfn_to_paddr(tlb.tlb_lo[0]),
			 (tlb.tlb_lo[0] & ENTRYLO_D) ? 'D' : ' ',
			 (tlb.tlb_lo[0] & ENTRYLO_G) ? 'G' : ' ',
			 (tlb.tlb_lo[0] & ENTRYLO_C) >> ENTRYLO_C_SHIFT);
		kvm_info("Lo1=0x%09llx %c%c attr %lx sz=%lx\n",
			 (u64) mips3_tlbpfn_to_paddr(tlb.tlb_lo[1]),
			 (tlb.tlb_lo[1] & ENTRYLO_D) ? 'D' : ' ',
			 (tlb.tlb_lo[1] & ENTRYLO_G) ? 'G' : ' ',
			 (tlb.tlb_lo[1] & ENTRYLO_C) >> ENTRYLO_C_SHIFT,
			 tlb.tlb_mask);
	}
}
EXPORT_SYMBOL_GPL(kvm_mips_dump_guest_tlbs);

int kvm_mips_guest_tlb_lookup(struct kvm_vcpu *vcpu, unsigned long entryhi)
{
	int i;
	int index = -1;
	struct kvm_mips_tlb *tlb = vcpu->arch.guest_tlb;

	for (i = 0; i < KVM_MIPS_GUEST_TLB_SIZE; i++) {
		if (TLB_HI_VPN2_HIT(tlb[i], entryhi) &&
		    TLB_HI_ASID_HIT(tlb[i], entryhi)) {
			index = i;
			break;
		}
	}

	kvm_debug("%s: entryhi: %#lx, index: %d lo0: %#lx, lo1: %#lx\n",
		  __func__, entryhi, index, tlb[i].tlb_lo[0], tlb[i].tlb_lo[1]);

	return index;
}
EXPORT_SYMBOL_GPL(kvm_mips_guest_tlb_lookup);

static int _kvm_mips_host_tlb_inv(unsigned long entryhi)
{
	int idx;

	write_c0_entryhi(entryhi);
	mtc0_tlbw_hazard();

	tlb_probe();
	tlb_probe_hazard();
	idx = read_c0_index();

	if (idx >= current_cpu_data.tlbsize)
		BUG();

	if (idx >= 0) {
		write_c0_entryhi(UNIQUE_ENTRYHI(idx));
		write_c0_entrylo0(0);
		write_c0_entrylo1(0);
		mtc0_tlbw_hazard();

		tlb_write_indexed();
		tlbw_use_hazard();
	}

	return idx;
}

int kvm_mips_host_tlb_inv(struct kvm_vcpu *vcpu, unsigned long va,
			  bool user, bool kernel)
{
	int idx_user, idx_kernel;
	unsigned long flags, old_entryhi;

	local_irq_save(flags);

	old_entryhi = read_c0_entryhi();

	if (user)
		idx_user = _kvm_mips_host_tlb_inv((va & VPN2_MASK) |
						  kvm_mips_get_user_asid(vcpu));
	if (kernel)
		idx_kernel = _kvm_mips_host_tlb_inv((va & VPN2_MASK) |
						kvm_mips_get_kernel_asid(vcpu));

	write_c0_entryhi(old_entryhi);
	mtc0_tlbw_hazard();

	local_irq_restore(flags);

	if (user && idx_user >= 0)
		kvm_debug("%s: Invalidated guest user entryhi %#lx @ idx %d\n",
			  __func__, (va & VPN2_MASK) |
				    kvm_mips_get_user_asid(vcpu), idx_user);
	if (kernel && idx_kernel >= 0)
		kvm_debug("%s: Invalidated guest kernel entryhi %#lx @ idx %d\n",
			  __func__, (va & VPN2_MASK) |
				    kvm_mips_get_kernel_asid(vcpu), idx_kernel);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_mips_host_tlb_inv);

/**
 * kvm_mips_suspend_mm() - Suspend the active mm.
 * @cpu		The CPU we're running on.
 *
 * Suspend the active_mm, ready for a switch to a KVM guest virtual address
 * space. This is left active for the duration of guest context, including time
 * with interrupts enabled, so we need to be careful not to confuse e.g. cache
 * management IPIs.
 *
 * kvm_mips_resume_mm() should be called before context switching to a different
 * process so we don't need to worry about reference counting.
 *
 * This needs to be in static kernel code to avoid exporting init_mm.
 */
void kvm_mips_suspend_mm(int cpu)
{
	cpumask_clear_cpu(cpu, mm_cpumask(current->active_mm));
	current->active_mm = &init_mm;
}
EXPORT_SYMBOL_GPL(kvm_mips_suspend_mm);

/**
 * kvm_mips_resume_mm() - Resume the current process mm.
 * @cpu		The CPU we're running on.
 *
 * Resume the mm of the current process, after a switch back from a KVM guest
 * virtual address space (see kvm_mips_suspend_mm()).
 */
void kvm_mips_resume_mm(int cpu)
{
	cpumask_set_cpu(cpu, mm_cpumask(current->mm));
	current->active_mm = current->mm;
}
EXPORT_SYMBOL_GPL(kvm_mips_resume_mm);

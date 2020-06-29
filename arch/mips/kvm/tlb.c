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
#include <asm/mipsregs.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/tlbdebug.h>

#undef CONFIG_MIPS_MT
#include <asm/r4kcache.h>
#define CONFIG_MIPS_MT

#define KVM_GUEST_PC_TLB    0
#define KVM_GUEST_SP_TLB    1

#ifdef CONFIG_KVM_MIPS_VZ
unsigned long GUESTID_MASK;
EXPORT_SYMBOL_GPL(GUESTID_MASK);
unsigned long GUESTID_FIRST_VERSION;
EXPORT_SYMBOL_GPL(GUESTID_FIRST_VERSION);
unsigned long GUESTID_VERSION_MASK;
EXPORT_SYMBOL_GPL(GUESTID_VERSION_MASK);

static u32 kvm_mips_get_root_asid(struct kvm_vcpu *vcpu)
{
	struct mm_struct *gpa_mm = &vcpu->kvm->arch.gpa_mm;

	if (cpu_has_guestid)
		return 0;
	else
		return cpu_asid(smp_processor_id(), gpa_mm);
}
#endif

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
	/*
	 * Initialize idx_user and idx_kernel to workaround bogus
	 * maybe-initialized warning when using GCC 6.
	 */
	int idx_user = 0, idx_kernel = 0;
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

	/*
	 * We don't want to get reserved instruction exceptions for missing tlb
	 * entries.
	 */
	if (cpu_has_vtag_icache)
		flush_icache_all();

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

#ifdef CONFIG_KVM_MIPS_VZ

/* GuestID management */

/**
 * clear_root_gid() - Set GuestCtl1.RID for normal root operation.
 */
static inline void clear_root_gid(void)
{
	if (cpu_has_guestid) {
		clear_c0_guestctl1(MIPS_GCTL1_RID);
		mtc0_tlbw_hazard();
	}
}

/**
 * set_root_gid_to_guest_gid() - Set GuestCtl1.RID to match GuestCtl1.ID.
 *
 * Sets the root GuestID to match the current guest GuestID, for TLB operation
 * on the GPA->RPA mappings in the root TLB.
 *
 * The caller must be sure to disable HTW while the root GID is set, and
 * possibly longer if TLB registers are modified.
 */
static inline void set_root_gid_to_guest_gid(void)
{
	unsigned int guestctl1;

	if (cpu_has_guestid) {
		back_to_back_c0_hazard();
		guestctl1 = read_c0_guestctl1();
		guestctl1 = (guestctl1 & ~MIPS_GCTL1_RID) |
			((guestctl1 & MIPS_GCTL1_ID) >> MIPS_GCTL1_ID_SHIFT)
						     << MIPS_GCTL1_RID_SHIFT;
		write_c0_guestctl1(guestctl1);
		mtc0_tlbw_hazard();
	}
}

int kvm_vz_host_tlb_inv(struct kvm_vcpu *vcpu, unsigned long va)
{
	int idx;
	unsigned long flags, old_entryhi;

	local_irq_save(flags);
	htw_stop();

	/* Set root GuestID for root probe and write of guest TLB entry */
	set_root_gid_to_guest_gid();

	old_entryhi = read_c0_entryhi();

	idx = _kvm_mips_host_tlb_inv((va & VPN2_MASK) |
				     kvm_mips_get_root_asid(vcpu));

	write_c0_entryhi(old_entryhi);
	clear_root_gid();
	mtc0_tlbw_hazard();

	htw_start();
	local_irq_restore(flags);

	/*
	 * We don't want to get reserved instruction exceptions for missing tlb
	 * entries.
	 */
	if (cpu_has_vtag_icache)
		flush_icache_all();

	if (idx > 0)
		kvm_debug("%s: Invalidated root entryhi %#lx @ idx %d\n",
			  __func__, (va & VPN2_MASK) |
				    kvm_mips_get_root_asid(vcpu), idx);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_vz_host_tlb_inv);

/**
 * kvm_vz_guest_tlb_lookup() - Lookup a guest VZ TLB mapping.
 * @vcpu:	KVM VCPU pointer.
 * @gpa:	Guest virtual address in a TLB mapped guest segment.
 * @gpa:	Ponter to output guest physical address it maps to.
 *
 * Converts a guest virtual address in a guest TLB mapped segment to a guest
 * physical address, by probing the guest TLB.
 *
 * Returns:	0 if guest TLB mapping exists for @gva. *@gpa will have been
 *		written.
 *		-EFAULT if no guest TLB mapping exists for @gva. *@gpa may not
 *		have been written.
 */
int kvm_vz_guest_tlb_lookup(struct kvm_vcpu *vcpu, unsigned long gva,
			    unsigned long *gpa)
{
	unsigned long o_entryhi, o_entrylo[2], o_pagemask;
	unsigned int o_index;
	unsigned long entrylo[2], pagemask, pagemaskbit, pa;
	unsigned long flags;
	int index;

	/* Probe the guest TLB for a mapping */
	local_irq_save(flags);
	/* Set root GuestID for root probe of guest TLB entry */
	htw_stop();
	set_root_gid_to_guest_gid();

	o_entryhi = read_gc0_entryhi();
	o_index = read_gc0_index();

	write_gc0_entryhi((o_entryhi & 0x3ff) | (gva & ~0xfffl));
	mtc0_tlbw_hazard();
	guest_tlb_probe();
	tlb_probe_hazard();

	index = read_gc0_index();
	if (index < 0) {
		/* No match, fail */
		write_gc0_entryhi(o_entryhi);
		write_gc0_index(o_index);

		clear_root_gid();
		htw_start();
		local_irq_restore(flags);
		return -EFAULT;
	}

	/* Match! read the TLB entry */
	o_entrylo[0] = read_gc0_entrylo0();
	o_entrylo[1] = read_gc0_entrylo1();
	o_pagemask = read_gc0_pagemask();

	mtc0_tlbr_hazard();
	guest_tlb_read();
	tlb_read_hazard();

	entrylo[0] = read_gc0_entrylo0();
	entrylo[1] = read_gc0_entrylo1();
	pagemask = ~read_gc0_pagemask() & ~0x1fffl;

	write_gc0_entryhi(o_entryhi);
	write_gc0_index(o_index);
	write_gc0_entrylo0(o_entrylo[0]);
	write_gc0_entrylo1(o_entrylo[1]);
	write_gc0_pagemask(o_pagemask);

	clear_root_gid();
	htw_start();
	local_irq_restore(flags);

	/* Select one of the EntryLo values and interpret the GPA */
	pagemaskbit = (pagemask ^ (pagemask & (pagemask - 1))) >> 1;
	pa = entrylo[!!(gva & pagemaskbit)];

	/*
	 * TLB entry may have become invalid since TLB probe if physical FTLB
	 * entries are shared between threads (e.g. I6400).
	 */
	if (!(pa & ENTRYLO_V))
		return -EFAULT;

	/*
	 * Note, this doesn't take guest MIPS32 XPA into account, where PFN is
	 * split with XI/RI in the middle.
	 */
	pa = (pa << 6) & ~0xfffl;
	pa |= gva & ~(pagemask | pagemaskbit);

	*gpa = pa;
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_vz_guest_tlb_lookup);

/**
 * kvm_vz_local_flush_roottlb_all_guests() - Flush all root TLB entries for
 * guests.
 *
 * Invalidate all entries in root tlb which are GPA mappings.
 */
void kvm_vz_local_flush_roottlb_all_guests(void)
{
	unsigned long flags;
	unsigned long old_entryhi, old_pagemask, old_guestctl1;
	int entry;

	if (WARN_ON(!cpu_has_guestid))
		return;

	local_irq_save(flags);
	htw_stop();

	/* TLBR may clobber EntryHi.ASID, PageMask, and GuestCtl1.RID */
	old_entryhi = read_c0_entryhi();
	old_pagemask = read_c0_pagemask();
	old_guestctl1 = read_c0_guestctl1();

	/*
	 * Invalidate guest entries in root TLB while leaving root entries
	 * intact when possible.
	 */
	for (entry = 0; entry < current_cpu_data.tlbsize; entry++) {
		write_c0_index(entry);
		mtc0_tlbw_hazard();
		tlb_read();
		tlb_read_hazard();

		/* Don't invalidate non-guest (RVA) mappings in the root TLB */
		if (!(read_c0_guestctl1() & MIPS_GCTL1_RID))
			continue;

		/* Make sure all entries differ. */
		write_c0_entryhi(UNIQUE_ENTRYHI(entry));
		write_c0_entrylo0(0);
		write_c0_entrylo1(0);
		write_c0_guestctl1(0);
		mtc0_tlbw_hazard();
		tlb_write_indexed();
	}

	write_c0_entryhi(old_entryhi);
	write_c0_pagemask(old_pagemask);
	write_c0_guestctl1(old_guestctl1);
	tlbw_use_hazard();

	htw_start();
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(kvm_vz_local_flush_roottlb_all_guests);

/**
 * kvm_vz_local_flush_guesttlb_all() - Flush all guest TLB entries.
 *
 * Invalidate all entries in guest tlb irrespective of guestid.
 */
void kvm_vz_local_flush_guesttlb_all(void)
{
	unsigned long flags;
	unsigned long old_index;
	unsigned long old_entryhi;
	unsigned long old_entrylo[2];
	unsigned long old_pagemask;
	int entry;
	u64 cvmmemctl2 = 0;

	local_irq_save(flags);

	/* Preserve all clobbered guest registers */
	old_index = read_gc0_index();
	old_entryhi = read_gc0_entryhi();
	old_entrylo[0] = read_gc0_entrylo0();
	old_entrylo[1] = read_gc0_entrylo1();
	old_pagemask = read_gc0_pagemask();

	switch (current_cpu_type()) {
	case CPU_CAVIUM_OCTEON3:
		/* Inhibit machine check due to multiple matching TLB entries */
		cvmmemctl2 = read_c0_cvmmemctl2();
		cvmmemctl2 |= CVMMEMCTL2_INHIBITTS;
		write_c0_cvmmemctl2(cvmmemctl2);
		break;
	}

	/* Invalidate guest entries in guest TLB */
	write_gc0_entrylo0(0);
	write_gc0_entrylo1(0);
	write_gc0_pagemask(0);
	for (entry = 0; entry < current_cpu_data.guest.tlbsize; entry++) {
		/* Make sure all entries differ. */
		write_gc0_index(entry);
		write_gc0_entryhi(UNIQUE_GUEST_ENTRYHI(entry));
		mtc0_tlbw_hazard();
		guest_tlb_write_indexed();
	}

	if (cvmmemctl2) {
		cvmmemctl2 &= ~CVMMEMCTL2_INHIBITTS;
		write_c0_cvmmemctl2(cvmmemctl2);
	}

	write_gc0_index(old_index);
	write_gc0_entryhi(old_entryhi);
	write_gc0_entrylo0(old_entrylo[0]);
	write_gc0_entrylo1(old_entrylo[1]);
	write_gc0_pagemask(old_pagemask);
	tlbw_use_hazard();

	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(kvm_vz_local_flush_guesttlb_all);

/**
 * kvm_vz_save_guesttlb() - Save a range of guest TLB entries.
 * @buf:	Buffer to write TLB entries into.
 * @index:	Start index.
 * @count:	Number of entries to save.
 *
 * Save a range of guest TLB entries. The caller must ensure interrupts are
 * disabled.
 */
void kvm_vz_save_guesttlb(struct kvm_mips_tlb *buf, unsigned int index,
			  unsigned int count)
{
	unsigned int end = index + count;
	unsigned long old_entryhi, old_entrylo0, old_entrylo1, old_pagemask;
	unsigned int guestctl1 = 0;
	int old_index, i;

	/* Save registers we're about to clobber */
	old_index = read_gc0_index();
	old_entryhi = read_gc0_entryhi();
	old_entrylo0 = read_gc0_entrylo0();
	old_entrylo1 = read_gc0_entrylo1();
	old_pagemask = read_gc0_pagemask();

	/* Set root GuestID for root probe */
	htw_stop();
	set_root_gid_to_guest_gid();
	if (cpu_has_guestid)
		guestctl1 = read_c0_guestctl1();

	/* Read each entry from guest TLB */
	for (i = index; i < end; ++i, ++buf) {
		write_gc0_index(i);

		mtc0_tlbr_hazard();
		guest_tlb_read();
		tlb_read_hazard();

		if (cpu_has_guestid &&
		    (read_c0_guestctl1() ^ guestctl1) & MIPS_GCTL1_RID) {
			/* Entry invalid or belongs to another guest */
			buf->tlb_hi = UNIQUE_GUEST_ENTRYHI(i);
			buf->tlb_lo[0] = 0;
			buf->tlb_lo[1] = 0;
			buf->tlb_mask = 0;
		} else {
			/* Entry belongs to the right guest */
			buf->tlb_hi = read_gc0_entryhi();
			buf->tlb_lo[0] = read_gc0_entrylo0();
			buf->tlb_lo[1] = read_gc0_entrylo1();
			buf->tlb_mask = read_gc0_pagemask();
		}
	}

	/* Clear root GuestID again */
	clear_root_gid();
	htw_start();

	/* Restore clobbered registers */
	write_gc0_index(old_index);
	write_gc0_entryhi(old_entryhi);
	write_gc0_entrylo0(old_entrylo0);
	write_gc0_entrylo1(old_entrylo1);
	write_gc0_pagemask(old_pagemask);

	tlbw_use_hazard();
}
EXPORT_SYMBOL_GPL(kvm_vz_save_guesttlb);

/**
 * kvm_vz_load_guesttlb() - Save a range of guest TLB entries.
 * @buf:	Buffer to read TLB entries from.
 * @index:	Start index.
 * @count:	Number of entries to load.
 *
 * Load a range of guest TLB entries. The caller must ensure interrupts are
 * disabled.
 */
void kvm_vz_load_guesttlb(const struct kvm_mips_tlb *buf, unsigned int index,
			  unsigned int count)
{
	unsigned int end = index + count;
	unsigned long old_entryhi, old_entrylo0, old_entrylo1, old_pagemask;
	int old_index, i;

	/* Save registers we're about to clobber */
	old_index = read_gc0_index();
	old_entryhi = read_gc0_entryhi();
	old_entrylo0 = read_gc0_entrylo0();
	old_entrylo1 = read_gc0_entrylo1();
	old_pagemask = read_gc0_pagemask();

	/* Set root GuestID for root probe */
	htw_stop();
	set_root_gid_to_guest_gid();

	/* Write each entry to guest TLB */
	for (i = index; i < end; ++i, ++buf) {
		write_gc0_index(i);
		write_gc0_entryhi(buf->tlb_hi);
		write_gc0_entrylo0(buf->tlb_lo[0]);
		write_gc0_entrylo1(buf->tlb_lo[1]);
		write_gc0_pagemask(buf->tlb_mask);

		mtc0_tlbw_hazard();
		guest_tlb_write_indexed();
	}

	/* Clear root GuestID again */
	clear_root_gid();
	htw_start();

	/* Restore clobbered registers */
	write_gc0_index(old_index);
	write_gc0_entryhi(old_entryhi);
	write_gc0_entrylo0(old_entrylo0);
	write_gc0_entrylo1(old_entrylo1);
	write_gc0_pagemask(old_pagemask);

	tlbw_use_hazard();
}
EXPORT_SYMBOL_GPL(kvm_vz_load_guesttlb);

#ifdef CONFIG_CPU_LOONGSON64
void kvm_loongson_clear_guest_vtlb(void)
{
	int idx = read_gc0_index();

	/* Set root GuestID for root probe and write of guest TLB entry */
	set_root_gid_to_guest_gid();

	write_gc0_index(0);
	guest_tlbinvf();
	write_gc0_index(idx);

	clear_root_gid();
	set_c0_diag(LOONGSON_DIAG_ITLB | LOONGSON_DIAG_DTLB);
}
EXPORT_SYMBOL_GPL(kvm_loongson_clear_guest_vtlb);

void kvm_loongson_clear_guest_ftlb(void)
{
	int i;
	int idx = read_gc0_index();

	/* Set root GuestID for root probe and write of guest TLB entry */
	set_root_gid_to_guest_gid();

	for (i = current_cpu_data.tlbsizevtlb;
	     i < (current_cpu_data.tlbsizevtlb +
		     current_cpu_data.tlbsizeftlbsets);
	     i++) {
		write_gc0_index(i);
		guest_tlbinvf();
	}
	write_gc0_index(idx);

	clear_root_gid();
	set_c0_diag(LOONGSON_DIAG_ITLB | LOONGSON_DIAG_DTLB);
}
EXPORT_SYMBOL_GPL(kvm_loongson_clear_guest_ftlb);
#endif

#endif

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

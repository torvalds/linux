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

#undef CONFIG_MIPS_MT
#include <asm/r4kcache.h>
#define CONFIG_MIPS_MT

#define KVM_GUEST_PC_TLB    0
#define KVM_GUEST_SP_TLB    1

atomic_t kvm_mips_instance;
EXPORT_SYMBOL_GPL(kvm_mips_instance);

/* These function pointers are initialized once the KVM module is loaded */
kvm_pfn_t (*kvm_mips_gfn_to_pfn)(struct kvm *kvm, gfn_t gfn);
EXPORT_SYMBOL_GPL(kvm_mips_gfn_to_pfn);

void (*kvm_mips_release_pfn_clean)(kvm_pfn_t pfn);
EXPORT_SYMBOL_GPL(kvm_mips_release_pfn_clean);

bool (*kvm_mips_is_error_pfn)(kvm_pfn_t pfn);
EXPORT_SYMBOL_GPL(kvm_mips_is_error_pfn);

static u32 kvm_mips_get_kernel_asid(struct kvm_vcpu *vcpu)
{
	int cpu = smp_processor_id();

	return vcpu->arch.guest_kernel_asid[cpu] &
			cpu_asid_mask(&cpu_data[cpu]);
}

static u32 kvm_mips_get_user_asid(struct kvm_vcpu *vcpu)
{
	int cpu = smp_processor_id();

	return vcpu->arch.guest_user_asid[cpu] &
			cpu_asid_mask(&cpu_data[cpu]);
}

inline u32 kvm_mips_get_commpage_asid(struct kvm_vcpu *vcpu)
{
	return vcpu->kvm->arch.commpage_tlb;
}

/* Structure defining an tlb entry data set. */

void kvm_mips_dump_host_tlbs(void)
{
	unsigned long old_entryhi;
	unsigned long old_pagemask;
	struct kvm_mips_tlb tlb;
	unsigned long flags;
	int i;

	local_irq_save(flags);

	old_entryhi = read_c0_entryhi();
	old_pagemask = read_c0_pagemask();

	kvm_info("HOST TLBs:\n");
	kvm_info("ASID: %#lx\n", read_c0_entryhi() &
		 cpu_asid_mask(&current_cpu_data));

	for (i = 0; i < current_cpu_data.tlbsize; i++) {
		write_c0_index(i);
		mtc0_tlbw_hazard();

		tlb_read();
		tlbw_use_hazard();

		tlb.tlb_hi = read_c0_entryhi();
		tlb.tlb_lo0 = read_c0_entrylo0();
		tlb.tlb_lo1 = read_c0_entrylo1();
		tlb.tlb_mask = read_c0_pagemask();

		kvm_info("TLB%c%3d Hi 0x%08lx ",
			 (tlb.tlb_lo0 | tlb.tlb_lo1) & MIPS3_PG_V ? ' ' : '*',
			 i, tlb.tlb_hi);
		kvm_info("Lo0=0x%09llx %c%c attr %lx ",
			 (u64) mips3_tlbpfn_to_paddr(tlb.tlb_lo0),
			 (tlb.tlb_lo0 & MIPS3_PG_D) ? 'D' : ' ',
			 (tlb.tlb_lo0 & MIPS3_PG_G) ? 'G' : ' ',
			 (tlb.tlb_lo0 >> 3) & 7);
		kvm_info("Lo1=0x%09llx %c%c attr %lx sz=%lx\n",
			 (u64) mips3_tlbpfn_to_paddr(tlb.tlb_lo1),
			 (tlb.tlb_lo1 & MIPS3_PG_D) ? 'D' : ' ',
			 (tlb.tlb_lo1 & MIPS3_PG_G) ? 'G' : ' ',
			 (tlb.tlb_lo1 >> 3) & 7, tlb.tlb_mask);
	}
	write_c0_entryhi(old_entryhi);
	write_c0_pagemask(old_pagemask);
	mtc0_tlbw_hazard();
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
			 (tlb.tlb_lo0 | tlb.tlb_lo1) & MIPS3_PG_V ? ' ' : '*',
			 i, tlb.tlb_hi);
		kvm_info("Lo0=0x%09llx %c%c attr %lx ",
			 (u64) mips3_tlbpfn_to_paddr(tlb.tlb_lo0),
			 (tlb.tlb_lo0 & MIPS3_PG_D) ? 'D' : ' ',
			 (tlb.tlb_lo0 & MIPS3_PG_G) ? 'G' : ' ',
			 (tlb.tlb_lo0 >> 3) & 7);
		kvm_info("Lo1=0x%09llx %c%c attr %lx sz=%lx\n",
			 (u64) mips3_tlbpfn_to_paddr(tlb.tlb_lo1),
			 (tlb.tlb_lo1 & MIPS3_PG_D) ? 'D' : ' ',
			 (tlb.tlb_lo1 & MIPS3_PG_G) ? 'G' : ' ',
			 (tlb.tlb_lo1 >> 3) & 7, tlb.tlb_mask);
	}
}
EXPORT_SYMBOL_GPL(kvm_mips_dump_guest_tlbs);

/* XXXKYMA: Must be called with interrupts disabled */
/* set flush_dcache_mask == 0 if no dcache flush required */
int kvm_mips_host_tlb_write(struct kvm_vcpu *vcpu, unsigned long entryhi,
			    unsigned long entrylo0, unsigned long entrylo1,
			    int flush_dcache_mask)
{
	unsigned long flags;
	unsigned long old_entryhi;
	int idx;

	local_irq_save(flags);

	old_entryhi = read_c0_entryhi();
	write_c0_entryhi(entryhi);
	mtc0_tlbw_hazard();

	tlb_probe();
	tlb_probe_hazard();
	idx = read_c0_index();

	if (idx > current_cpu_data.tlbsize) {
		kvm_err("%s: Invalid Index: %d\n", __func__, idx);
		kvm_mips_dump_host_tlbs();
		local_irq_restore(flags);
		return -1;
	}

	write_c0_entrylo0(entrylo0);
	write_c0_entrylo1(entrylo1);
	mtc0_tlbw_hazard();

	if (idx < 0)
		tlb_write_random();
	else
		tlb_write_indexed();
	tlbw_use_hazard();

	kvm_debug("@ %#lx idx: %2d [entryhi(R): %#lx] entrylo0(R): 0x%08lx, entrylo1(R): 0x%08lx\n",
		  vcpu->arch.pc, idx, read_c0_entryhi(),
		  read_c0_entrylo0(), read_c0_entrylo1());

	/* Flush D-cache */
	if (flush_dcache_mask) {
		if (entrylo0 & MIPS3_PG_V) {
			++vcpu->stat.flush_dcache_exits;
			flush_data_cache_page((entryhi & VPN2_MASK) &
					      ~flush_dcache_mask);
		}
		if (entrylo1 & MIPS3_PG_V) {
			++vcpu->stat.flush_dcache_exits;
			flush_data_cache_page(((entryhi & VPN2_MASK) &
					       ~flush_dcache_mask) |
					      (0x1 << PAGE_SHIFT));
		}
	}

	/* Restore old ASID */
	write_c0_entryhi(old_entryhi);
	mtc0_tlbw_hazard();
	tlbw_use_hazard();
	local_irq_restore(flags);
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_mips_host_tlb_write);

int kvm_mips_handle_commpage_tlb_fault(unsigned long badvaddr,
	struct kvm_vcpu *vcpu)
{
	kvm_pfn_t pfn0, pfn1;
	unsigned long flags, old_entryhi = 0, vaddr = 0;
	unsigned long entrylo0 = 0, entrylo1 = 0;

	pfn0 = CPHYSADDR(vcpu->arch.kseg0_commpage) >> PAGE_SHIFT;
	pfn1 = 0;
	entrylo0 = mips3_paddr_to_tlbpfn(pfn0 << PAGE_SHIFT) | (0x3 << 3) |
		   (1 << 2) | (0x1 << 1);
	entrylo1 = 0;

	local_irq_save(flags);

	old_entryhi = read_c0_entryhi();
	vaddr = badvaddr & (PAGE_MASK << 1);
	write_c0_entryhi(vaddr | kvm_mips_get_kernel_asid(vcpu));
	mtc0_tlbw_hazard();
	write_c0_entrylo0(entrylo0);
	mtc0_tlbw_hazard();
	write_c0_entrylo1(entrylo1);
	mtc0_tlbw_hazard();
	write_c0_index(kvm_mips_get_commpage_asid(vcpu));
	mtc0_tlbw_hazard();
	tlb_write_indexed();
	mtc0_tlbw_hazard();
	tlbw_use_hazard();

	kvm_debug("@ %#lx idx: %2d [entryhi(R): %#lx] entrylo0 (R): 0x%08lx, entrylo1(R): 0x%08lx\n",
		  vcpu->arch.pc, read_c0_index(), read_c0_entryhi(),
		  read_c0_entrylo0(), read_c0_entrylo1());

	/* Restore old ASID */
	write_c0_entryhi(old_entryhi);
	mtc0_tlbw_hazard();
	tlbw_use_hazard();
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_mips_handle_commpage_tlb_fault);

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
		  __func__, entryhi, index, tlb[i].tlb_lo0, tlb[i].tlb_lo1);

	return index;
}
EXPORT_SYMBOL_GPL(kvm_mips_guest_tlb_lookup);

int kvm_mips_host_tlb_lookup(struct kvm_vcpu *vcpu, unsigned long vaddr)
{
	unsigned long old_entryhi, flags;
	int idx;

	local_irq_save(flags);

	old_entryhi = read_c0_entryhi();

	if (KVM_GUEST_KERNEL_MODE(vcpu))
		write_c0_entryhi((vaddr & VPN2_MASK) |
				 kvm_mips_get_kernel_asid(vcpu));
	else {
		write_c0_entryhi((vaddr & VPN2_MASK) |
				 kvm_mips_get_user_asid(vcpu));
	}

	mtc0_tlbw_hazard();

	tlb_probe();
	tlb_probe_hazard();
	idx = read_c0_index();

	/* Restore old ASID */
	write_c0_entryhi(old_entryhi);
	mtc0_tlbw_hazard();
	tlbw_use_hazard();

	local_irq_restore(flags);

	kvm_debug("Host TLB lookup, %#lx, idx: %2d\n", vaddr, idx);

	return idx;
}
EXPORT_SYMBOL_GPL(kvm_mips_host_tlb_lookup);

int kvm_mips_host_tlb_inv(struct kvm_vcpu *vcpu, unsigned long va)
{
	int idx;
	unsigned long flags, old_entryhi;

	local_irq_save(flags);

	old_entryhi = read_c0_entryhi();

	write_c0_entryhi((va & VPN2_MASK) | kvm_mips_get_user_asid(vcpu));
	mtc0_tlbw_hazard();

	tlb_probe();
	tlb_probe_hazard();
	idx = read_c0_index();

	if (idx >= current_cpu_data.tlbsize)
		BUG();

	if (idx > 0) {
		write_c0_entryhi(UNIQUE_ENTRYHI(idx));
		mtc0_tlbw_hazard();

		write_c0_entrylo0(0);
		mtc0_tlbw_hazard();

		write_c0_entrylo1(0);
		mtc0_tlbw_hazard();

		tlb_write_indexed();
		mtc0_tlbw_hazard();
	}

	write_c0_entryhi(old_entryhi);
	mtc0_tlbw_hazard();
	tlbw_use_hazard();

	local_irq_restore(flags);

	if (idx > 0)
		kvm_debug("%s: Invalidated entryhi %#lx @ idx %d\n", __func__,
			  (va & VPN2_MASK) | kvm_mips_get_user_asid(vcpu), idx);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_mips_host_tlb_inv);

void kvm_mips_flush_host_tlb(int skip_kseg0)
{
	unsigned long flags;
	unsigned long old_entryhi, entryhi;
	unsigned long old_pagemask;
	int entry = 0;
	int maxentry = current_cpu_data.tlbsize;

	local_irq_save(flags);

	old_entryhi = read_c0_entryhi();
	old_pagemask = read_c0_pagemask();

	/* Blast 'em all away. */
	for (entry = 0; entry < maxentry; entry++) {
		write_c0_index(entry);
		mtc0_tlbw_hazard();

		if (skip_kseg0) {
			tlb_read();
			tlbw_use_hazard();

			entryhi = read_c0_entryhi();

			/* Don't blow away guest kernel entries */
			if (KVM_GUEST_KSEGX(entryhi) == KVM_GUEST_KSEG0)
				continue;
		}

		/* Make sure all entries differ. */
		write_c0_entryhi(UNIQUE_ENTRYHI(entry));
		mtc0_tlbw_hazard();
		write_c0_entrylo0(0);
		mtc0_tlbw_hazard();
		write_c0_entrylo1(0);
		mtc0_tlbw_hazard();

		tlb_write_indexed();
		mtc0_tlbw_hazard();
	}

	tlbw_use_hazard();

	write_c0_entryhi(old_entryhi);
	write_c0_pagemask(old_pagemask);
	mtc0_tlbw_hazard();
	tlbw_use_hazard();

	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(kvm_mips_flush_host_tlb);

void kvm_local_flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry = 0;

	local_irq_save(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = read_c0_entryhi();
	write_c0_entrylo0(0);
	write_c0_entrylo1(0);

	/* Blast 'em all away. */
	while (entry < current_cpu_data.tlbsize) {
		/* Make sure all entries differ. */
		write_c0_entryhi(UNIQUE_ENTRYHI(entry));
		write_c0_index(entry);
		mtc0_tlbw_hazard();
		tlb_write_indexed();
		entry++;
	}
	tlbw_use_hazard();
	write_c0_entryhi(old_ctx);
	mtc0_tlbw_hazard();

	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(kvm_local_flush_tlb_all);

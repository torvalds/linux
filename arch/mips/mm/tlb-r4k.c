/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1997, 1998, 1999, 2000 Ralf Baechle ralf@gnu.org
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2002 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/cpu_pm.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/memblock.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/export.h>
#include <linux/sort.h>

#include <asm/cpu.h>
#include <asm/cpu-type.h>
#include <asm/bootinfo.h>
#include <asm/hazards.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>
#include <asm/tlbdebug.h>
#include <asm/tlbex.h>
#include <asm/tlbmisc.h>
#include <asm/setup.h>

/*
 * LOONGSON-2 has a 4 entry itlb which is a subset of jtlb, LOONGSON-3 has
 * a 4 entry itlb and a 4 entry dtlb which are subsets of jtlb. Unfortunately,
 * itlb/dtlb are not totally transparent to software.
 */
static inline void flush_micro_tlb(void)
{
	switch (current_cpu_type()) {
	case CPU_LOONGSON2EF:
		write_c0_diag(LOONGSON_DIAG_ITLB);
		break;
	case CPU_LOONGSON64:
		write_c0_diag(LOONGSON_DIAG_ITLB | LOONGSON_DIAG_DTLB);
		break;
	default:
		break;
	}
}

static inline void flush_micro_tlb_vm(struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_EXEC)
		flush_micro_tlb();
}

void local_flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry, ftlbhighset;

	local_irq_save(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = read_c0_entryhi();
	htw_stop();
	write_c0_entrylo0(0);
	write_c0_entrylo1(0);

	entry = num_wired_entries();

	/*
	 * Blast 'em all away.
	 * If there are any wired entries, fall back to iterating
	 */
	if (cpu_has_tlbinv && !entry) {
		if (current_cpu_data.tlbsizevtlb) {
			write_c0_index(0);
			mtc0_tlbw_hazard();
			tlbinvf();  /* invalidate VTLB */
		}
		ftlbhighset = current_cpu_data.tlbsizevtlb +
			current_cpu_data.tlbsizeftlbsets;
		for (entry = current_cpu_data.tlbsizevtlb;
		     entry < ftlbhighset;
		     entry++) {
			write_c0_index(entry);
			mtc0_tlbw_hazard();
			tlbinvf();  /* invalidate one FTLB set */
		}
	} else {
		while (entry < current_cpu_data.tlbsize) {
			/* Make sure all entries differ. */
			write_c0_entryhi(UNIQUE_ENTRYHI(entry));
			write_c0_index(entry);
			mtc0_tlbw_hazard();
			tlb_write_indexed();
			entry++;
		}
	}
	tlbw_use_hazard();
	write_c0_entryhi(old_ctx);
	htw_start();
	flush_micro_tlb();
	local_irq_restore(flags);
}
EXPORT_SYMBOL(local_flush_tlb_all);

void local_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
	unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	int cpu = smp_processor_id();

	if (cpu_context(cpu, mm) != 0) {
		unsigned long size, flags;

		local_irq_save(flags);
		start = round_down(start, PAGE_SIZE << 1);
		end = round_up(end, PAGE_SIZE << 1);
		size = (end - start) >> (PAGE_SHIFT + 1);
		if (size <= (current_cpu_data.tlbsizeftlbsets ?
			     current_cpu_data.tlbsize / 8 :
			     current_cpu_data.tlbsize / 2)) {
			unsigned long old_entryhi, old_mmid;
			int newpid = cpu_asid(cpu, mm);

			old_entryhi = read_c0_entryhi();
			if (cpu_has_mmid) {
				old_mmid = read_c0_memorymapid();
				write_c0_memorymapid(newpid);
			}

			htw_stop();
			while (start < end) {
				int idx;

				if (cpu_has_mmid)
					write_c0_entryhi(start);
				else
					write_c0_entryhi(start | newpid);
				start += (PAGE_SIZE << 1);
				mtc0_tlbw_hazard();
				tlb_probe();
				tlb_probe_hazard();
				idx = read_c0_index();
				write_c0_entrylo0(0);
				write_c0_entrylo1(0);
				if (idx < 0)
					continue;
				/* Make sure all entries differ. */
				write_c0_entryhi(UNIQUE_ENTRYHI(idx));
				mtc0_tlbw_hazard();
				tlb_write_indexed();
			}
			tlbw_use_hazard();
			write_c0_entryhi(old_entryhi);
			if (cpu_has_mmid)
				write_c0_memorymapid(old_mmid);
			htw_start();
		} else {
			drop_mmu_context(mm);
		}
		flush_micro_tlb();
		local_irq_restore(flags);
	}
}

void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long size, flags;

	local_irq_save(flags);
	size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	size = (size + 1) >> 1;
	if (size <= (current_cpu_data.tlbsizeftlbsets ?
		     current_cpu_data.tlbsize / 8 :
		     current_cpu_data.tlbsize / 2)) {
		int pid = read_c0_entryhi();

		start &= (PAGE_MASK << 1);
		end += ((PAGE_SIZE << 1) - 1);
		end &= (PAGE_MASK << 1);
		htw_stop();

		while (start < end) {
			int idx;

			write_c0_entryhi(start);
			start += (PAGE_SIZE << 1);
			mtc0_tlbw_hazard();
			tlb_probe();
			tlb_probe_hazard();
			idx = read_c0_index();
			write_c0_entrylo0(0);
			write_c0_entrylo1(0);
			if (idx < 0)
				continue;
			/* Make sure all entries differ. */
			write_c0_entryhi(UNIQUE_ENTRYHI(idx));
			mtc0_tlbw_hazard();
			tlb_write_indexed();
		}
		tlbw_use_hazard();
		write_c0_entryhi(pid);
		htw_start();
	} else {
		local_flush_tlb_all();
	}
	flush_micro_tlb();
	local_irq_restore(flags);
}

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int cpu = smp_processor_id();

	if (cpu_context(cpu, vma->vm_mm) != 0) {
		unsigned long old_mmid;
		unsigned long flags, old_entryhi;
		int idx;

		page &= (PAGE_MASK << 1);
		local_irq_save(flags);
		old_entryhi = read_c0_entryhi();
		htw_stop();
		if (cpu_has_mmid) {
			old_mmid = read_c0_memorymapid();
			write_c0_entryhi(page);
			write_c0_memorymapid(cpu_asid(cpu, vma->vm_mm));
		} else {
			write_c0_entryhi(page | cpu_asid(cpu, vma->vm_mm));
		}
		mtc0_tlbw_hazard();
		tlb_probe();
		tlb_probe_hazard();
		idx = read_c0_index();
		write_c0_entrylo0(0);
		write_c0_entrylo1(0);
		if (idx < 0)
			goto finish;
		/* Make sure all entries differ. */
		write_c0_entryhi(UNIQUE_ENTRYHI(idx));
		mtc0_tlbw_hazard();
		tlb_write_indexed();
		tlbw_use_hazard();

	finish:
		write_c0_entryhi(old_entryhi);
		if (cpu_has_mmid)
			write_c0_memorymapid(old_mmid);
		htw_start();
		flush_micro_tlb_vm(vma);
		local_irq_restore(flags);
	}
}

/*
 * This one is only used for pages with the global bit set so we don't care
 * much about the ASID.
 */
void local_flush_tlb_one(unsigned long page)
{
	unsigned long flags;
	int oldpid, idx;

	local_irq_save(flags);
	oldpid = read_c0_entryhi();
	htw_stop();
	page &= (PAGE_MASK << 1);
	write_c0_entryhi(page);
	mtc0_tlbw_hazard();
	tlb_probe();
	tlb_probe_hazard();
	idx = read_c0_index();
	write_c0_entrylo0(0);
	write_c0_entrylo1(0);
	if (idx >= 0) {
		/* Make sure all entries differ. */
		write_c0_entryhi(UNIQUE_ENTRYHI(idx));
		mtc0_tlbw_hazard();
		tlb_write_indexed();
		tlbw_use_hazard();
	}
	write_c0_entryhi(oldpid);
	htw_start();
	flush_micro_tlb();
	local_irq_restore(flags);
}

/*
 * We will need multiple versions of update_mmu_cache(), one that just
 * updates the TLB with the new pte(s), and another which also checks
 * for the R4k "end of page" hardware bug and does the needy.
 */
void __update_tlb(struct vm_area_struct * vma, unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep, *ptemap = NULL;
	int idx, pid;

	/*
	 * Handle debugger faulting in for debuggee.
	 */
	if (current->active_mm != vma->vm_mm)
		return;

	local_irq_save(flags);

	htw_stop();
	address &= (PAGE_MASK << 1);
	if (cpu_has_mmid) {
		write_c0_entryhi(address);
	} else {
		pid = read_c0_entryhi() & cpu_asid_mask(&current_cpu_data);
		write_c0_entryhi(address | pid);
	}
	pgdp = pgd_offset(vma->vm_mm, address);
	mtc0_tlbw_hazard();
	tlb_probe();
	tlb_probe_hazard();
	p4dp = p4d_offset(pgdp, address);
	pudp = pud_offset(p4dp, address);
	pmdp = pmd_offset(pudp, address);
	idx = read_c0_index();
#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	/* this could be a huge page  */
	if (pmd_leaf(*pmdp)) {
		unsigned long lo;
		write_c0_pagemask(PM_HUGE_MASK);
		ptep = (pte_t *)pmdp;
		lo = pte_to_entrylo(pte_val(*ptep));
		write_c0_entrylo0(lo);
		write_c0_entrylo1(lo + (HPAGE_SIZE >> 7));

		mtc0_tlbw_hazard();
		if (idx < 0)
			tlb_write_random();
		else
			tlb_write_indexed();
		tlbw_use_hazard();
		write_c0_pagemask(PM_DEFAULT_MASK);
	} else
#endif
	{
		ptemap = ptep = pte_offset_map(pmdp, address);
		/*
		 * update_mmu_cache() is called between pte_offset_map_lock()
		 * and pte_unmap_unlock(), so we can assume that ptep is not
		 * NULL here: and what should be done below if it were NULL?
		 */

#if defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32)
#ifdef CONFIG_XPA
		write_c0_entrylo0(pte_to_entrylo(ptep->pte_high));
		if (cpu_has_xpa)
			writex_c0_entrylo0(ptep->pte_low & _PFNX_MASK);
		ptep++;
		write_c0_entrylo1(pte_to_entrylo(ptep->pte_high));
		if (cpu_has_xpa)
			writex_c0_entrylo1(ptep->pte_low & _PFNX_MASK);
#else
		write_c0_entrylo0(ptep->pte_high);
		ptep++;
		write_c0_entrylo1(ptep->pte_high);
#endif
#else
		write_c0_entrylo0(pte_to_entrylo(pte_val(*ptep++)));
		write_c0_entrylo1(pte_to_entrylo(pte_val(*ptep)));
#endif
		mtc0_tlbw_hazard();
		if (idx < 0)
			tlb_write_random();
		else
			tlb_write_indexed();
	}
	tlbw_use_hazard();
	htw_start();
	flush_micro_tlb_vm(vma);

	if (ptemap)
		pte_unmap(ptemap);
	local_irq_restore(flags);
}

void add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
		     unsigned long entryhi, unsigned long pagemask)
{
#ifdef CONFIG_XPA
	panic("Broken for XPA kernels");
#else
	unsigned int old_mmid;
	unsigned long flags;
	unsigned long wired;
	unsigned long old_pagemask;
	unsigned long old_ctx;

	local_irq_save(flags);
	if (cpu_has_mmid) {
		old_mmid = read_c0_memorymapid();
		write_c0_memorymapid(MMID_KERNEL_WIRED);
	}
	/* Save old context and create impossible VPN2 value */
	old_ctx = read_c0_entryhi();
	htw_stop();
	old_pagemask = read_c0_pagemask();
	wired = num_wired_entries();
	write_c0_wired(wired + 1);
	write_c0_index(wired);
	tlbw_use_hazard();	/* What is the hazard here? */
	write_c0_pagemask(pagemask);
	write_c0_entryhi(entryhi);
	write_c0_entrylo0(entrylo0);
	write_c0_entrylo1(entrylo1);
	mtc0_tlbw_hazard();
	tlb_write_indexed();
	tlbw_use_hazard();

	write_c0_entryhi(old_ctx);
	if (cpu_has_mmid)
		write_c0_memorymapid(old_mmid);
	tlbw_use_hazard();	/* What is the hazard here? */
	htw_start();
	write_c0_pagemask(old_pagemask);
	local_flush_tlb_all();
	local_irq_restore(flags);
#endif
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

int has_transparent_hugepage(void)
{
	static unsigned int mask = -1;

	if (mask == -1) {	/* first call comes during __init */
		unsigned long flags;

		local_irq_save(flags);
		write_c0_pagemask(PM_HUGE_MASK);
		back_to_back_c0_hazard();
		mask = read_c0_pagemask();
		write_c0_pagemask(PM_DEFAULT_MASK);
		local_irq_restore(flags);
	}
	return mask == PM_HUGE_MASK;
}
EXPORT_SYMBOL(has_transparent_hugepage);

#endif /* CONFIG_TRANSPARENT_HUGEPAGE  */

/*
 * Used for loading TLB entries before trap_init() has started, when we
 * don't actually want to add a wired entry which remains throughout the
 * lifetime of the system
 */

int temp_tlb_entry;

#ifndef CONFIG_64BIT
__init int add_temporary_entry(unsigned long entrylo0, unsigned long entrylo1,
			       unsigned long entryhi, unsigned long pagemask)
{
	int ret = 0;
	unsigned long flags;
	unsigned long wired;
	unsigned long old_pagemask;
	unsigned long old_ctx;

	local_irq_save(flags);
	/* Save old context and create impossible VPN2 value */
	htw_stop();
	old_ctx = read_c0_entryhi();
	old_pagemask = read_c0_pagemask();
	wired = num_wired_entries();
	if (--temp_tlb_entry < wired) {
		printk(KERN_WARNING
		       "No TLB space left for add_temporary_entry\n");
		ret = -ENOSPC;
		goto out;
	}

	write_c0_index(temp_tlb_entry);
	write_c0_pagemask(pagemask);
	write_c0_entryhi(entryhi);
	write_c0_entrylo0(entrylo0);
	write_c0_entrylo1(entrylo1);
	mtc0_tlbw_hazard();
	tlb_write_indexed();
	tlbw_use_hazard();

	write_c0_entryhi(old_ctx);
	write_c0_pagemask(old_pagemask);
	htw_start();
out:
	local_irq_restore(flags);
	return ret;
}
#endif

static int ntlb;
static int __init set_ntlb(char *str)
{
	get_option(&str, &ntlb);
	return 1;
}

__setup("ntlb=", set_ntlb);


/* The start bit position of VPN2 and Mask in EntryHi/PageMask registers.  */
#define VPN2_SHIFT 13

/* Read full EntryHi even with CONFIG_32BIT.  */
static inline unsigned long long read_c0_entryhi_native(void)
{
	return cpu_has_64bits ? read_c0_entryhi_64() : read_c0_entryhi();
}

/* Write full EntryHi even with CONFIG_32BIT.  */
static inline void write_c0_entryhi_native(unsigned long long v)
{
	if (cpu_has_64bits)
		write_c0_entryhi_64(v);
	else
		write_c0_entryhi(v);
}

/* TLB entry state for uniquification.  */
struct tlbent {
	unsigned long long wired:1;
	unsigned long long global:1;
	unsigned long long asid:10;
	unsigned long long vpn:51;
	unsigned long long pagesz:5;
	unsigned long long index:14;
};

/*
 * Comparison function for TLB entry sorting.  Place wired entries first,
 * then global entries, then order by the increasing VPN/ASID and the
 * decreasing page size.  This lets us avoid clashes with wired entries
 * easily and get entries for larger pages out of the way first.
 *
 * We could group bits so as to reduce the number of comparisons, but this
 * is seldom executed and not performance-critical, so prefer legibility.
 */
static int r4k_entry_cmp(const void *a, const void *b)
{
	struct tlbent ea = *(struct tlbent *)a, eb = *(struct tlbent *)b;

	if (ea.wired > eb.wired)
		return -1;
	else if (ea.wired < eb.wired)
		return 1;
	else if (ea.global > eb.global)
		return -1;
	else if (ea.global < eb.global)
		return 1;
	else if (ea.vpn < eb.vpn)
		return -1;
	else if (ea.vpn > eb.vpn)
		return 1;
	else if (ea.asid < eb.asid)
		return -1;
	else if (ea.asid > eb.asid)
		return 1;
	else if (ea.pagesz > eb.pagesz)
		return -1;
	else if (ea.pagesz < eb.pagesz)
		return 1;
	else
		return 0;
}

/*
 * Fetch all the TLB entries.  Mask individual VPN values retrieved with
 * the corresponding page mask and ignoring any 1KiB extension as we'll
 * be using 4KiB pages for uniquification.
 */
static void __ref r4k_tlb_uniquify_read(struct tlbent *tlb_vpns, int tlbsize)
{
	int start = num_wired_entries();
	unsigned long long vpn_mask;
	bool global;
	int i;

	vpn_mask = GENMASK(current_cpu_data.vmbits - 1, VPN2_SHIFT);
	vpn_mask |= cpu_has_64bits ? 3ULL << 62 : 1 << 31;

	for (i = 0; i < tlbsize; i++) {
		unsigned long long entryhi, vpn, mask, asid;
		unsigned int pagesz;

		write_c0_index(i);
		mtc0_tlbr_hazard();
		tlb_read();
		tlb_read_hazard();

		global = !!(read_c0_entrylo0() & ENTRYLO_G);
		entryhi = read_c0_entryhi_native();
		mask = read_c0_pagemask();

		asid = entryhi & cpu_asid_mask(&current_cpu_data);
		vpn = (entryhi & vpn_mask & ~mask) >> VPN2_SHIFT;
		pagesz = ilog2((mask >> VPN2_SHIFT) + 1);

		tlb_vpns[i].global = global;
		tlb_vpns[i].asid = global ? 0 : asid;
		tlb_vpns[i].vpn = vpn;
		tlb_vpns[i].pagesz = pagesz;
		tlb_vpns[i].wired = i < start;
		tlb_vpns[i].index = i;
	}
}

/*
 * Write unique values to all but the wired TLB entries each, using
 * the 4KiB page size.  This size might not be supported with R6, but
 * EHINV is mandatory for R6, so we won't ever be called in that case.
 *
 * A sorted table is supplied with any wired entries at the beginning,
 * followed by any global entries, and then finally regular entries.
 * We start at the VPN and ASID values of zero and only assign user
 * addresses, therefore guaranteeing no clash with addresses produced
 * by UNIQUE_ENTRYHI.  We avoid any VPN values used by wired or global
 * entries, by increasing the VPN value beyond the span of such entry.
 *
 * When a VPN/ASID clash is found with a regular entry we increment the
 * ASID instead until no VPN/ASID clash has been found or the ASID space
 * has been exhausted, in which case we increase the VPN value beyond
 * the span of the largest clashing entry.
 *
 * We do not need to be concerned about FTLB or MMID configurations as
 * those are required to implement the EHINV feature.
 */
static void __ref r4k_tlb_uniquify_write(struct tlbent *tlb_vpns, int tlbsize)
{
	unsigned long long asid, vpn, vpn_size, pagesz;
	int widx, gidx, idx, sidx, lidx, i;

	vpn_size = 1ULL << (current_cpu_data.vmbits - VPN2_SHIFT);
	pagesz = ilog2((PM_4K >> VPN2_SHIFT) + 1);

	write_c0_pagemask(PM_4K);
	write_c0_entrylo0(0);
	write_c0_entrylo1(0);

	asid = 0;
	vpn = 0;
	widx = 0;
	gidx = 0;
	for (sidx = 0; sidx < tlbsize && tlb_vpns[sidx].wired; sidx++)
		;
	for (lidx = sidx; lidx < tlbsize && tlb_vpns[lidx].global; lidx++)
		;
	idx = gidx = sidx + 1;
	for (i = sidx; i < tlbsize; i++) {
		unsigned long long entryhi, vpn_pagesz = 0;

		while (1) {
			if (WARN_ON(vpn >= vpn_size)) {
				dump_tlb_all();
				/* Pray local_flush_tlb_all() will cope.  */
				return;
			}

			/* VPN must be below the next wired entry.  */
			if (widx < sidx && vpn >= tlb_vpns[widx].vpn) {
				vpn = max(vpn,
					  (tlb_vpns[widx].vpn +
					   (1ULL << tlb_vpns[widx].pagesz)));
				asid = 0;
				widx++;
				continue;
			}
			/* VPN must be below the next global entry.  */
			if (gidx < lidx && vpn >= tlb_vpns[gidx].vpn) {
				vpn = max(vpn,
					  (tlb_vpns[gidx].vpn +
					   (1ULL << tlb_vpns[gidx].pagesz)));
				asid = 0;
				gidx++;
				continue;
			}
			/* Try to find a free ASID so as to conserve VPNs.  */
			if (idx < tlbsize && vpn == tlb_vpns[idx].vpn &&
			    asid == tlb_vpns[idx].asid) {
				unsigned long long idx_pagesz;

				idx_pagesz = tlb_vpns[idx].pagesz;
				vpn_pagesz = max(vpn_pagesz, idx_pagesz);
				do
					idx++;
				while (idx < tlbsize &&
				       vpn == tlb_vpns[idx].vpn &&
				       asid == tlb_vpns[idx].asid);
				asid++;
				if (asid > cpu_asid_mask(&current_cpu_data)) {
					vpn += vpn_pagesz;
					asid = 0;
					vpn_pagesz = 0;
				}
				continue;
			}
			/* VPN mustn't be above the next regular entry.  */
			if (idx < tlbsize && vpn > tlb_vpns[idx].vpn) {
				vpn = max(vpn,
					  (tlb_vpns[idx].vpn +
					   (1ULL << tlb_vpns[idx].pagesz)));
				asid = 0;
				idx++;
				continue;
			}
			break;
		}

		entryhi = (vpn << VPN2_SHIFT) | asid;
		write_c0_entryhi_native(entryhi);
		write_c0_index(tlb_vpns[i].index);
		mtc0_tlbw_hazard();
		tlb_write_indexed();

		tlb_vpns[i].asid = asid;
		tlb_vpns[i].vpn = vpn;
		tlb_vpns[i].pagesz = pagesz;

		asid++;
		if (asid > cpu_asid_mask(&current_cpu_data)) {
			vpn += 1ULL << pagesz;
			asid = 0;
		}
	}
}

/*
 * Initialise all TLB entries with unique values that do not clash with
 * what we have been handed over and what we'll be using ourselves.
 */
static void __ref r4k_tlb_uniquify(void)
{
	int tlbsize = current_cpu_data.tlbsize;
	bool use_slab = slab_is_available();
	phys_addr_t tlb_vpn_size;
	struct tlbent *tlb_vpns;

	tlb_vpn_size = tlbsize * sizeof(*tlb_vpns);
	tlb_vpns = (use_slab ?
		    kmalloc(tlb_vpn_size, GFP_ATOMIC) :
		    memblock_alloc_raw(tlb_vpn_size, sizeof(*tlb_vpns)));
	if (WARN_ON(!tlb_vpns))
		return; /* Pray local_flush_tlb_all() is good enough. */

	htw_stop();

	r4k_tlb_uniquify_read(tlb_vpns, tlbsize);

	sort(tlb_vpns, tlbsize, sizeof(*tlb_vpns), r4k_entry_cmp, NULL);

	r4k_tlb_uniquify_write(tlb_vpns, tlbsize);

	write_c0_pagemask(PM_DEFAULT_MASK);

	tlbw_use_hazard();
	htw_start();
	flush_micro_tlb();
	if (use_slab)
		kfree(tlb_vpns);
	else
		memblock_free(tlb_vpns, tlb_vpn_size);
}

/*
 * Configure TLB (for init or after a CPU has been powered off).
 */
static void r4k_tlb_configure(void)
{
	/*
	 * You should never change this register:
	 *   - On R4600 1.7 the tlbp never hits for pages smaller than
	 *     the value in the c0_pagemask register.
	 *   - The entire mm handling assumes the c0_pagemask register to
	 *     be set to fixed-size pages.
	 */
	write_c0_pagemask(PM_DEFAULT_MASK);
	back_to_back_c0_hazard();
	if (read_c0_pagemask() != PM_DEFAULT_MASK)
		panic("MMU doesn't support PAGE_SIZE=0x%lx", PAGE_SIZE);

	write_c0_wired(0);
	if (current_cpu_type() == CPU_R10000 ||
	    current_cpu_type() == CPU_R12000 ||
	    current_cpu_type() == CPU_R14000 ||
	    current_cpu_type() == CPU_R16000)
		write_c0_framemask(0);

	if (cpu_has_rixi) {
		/*
		 * Enable the no read, no exec bits, and enable large physical
		 * address.
		 */
#ifdef CONFIG_64BIT
		set_c0_pagegrain(PG_RIE | PG_XIE | PG_ELPA);
#else
		set_c0_pagegrain(PG_RIE | PG_XIE);
#endif
	}

	temp_tlb_entry = current_cpu_data.tlbsize - 1;

	/* From this point on the ARC firmware is dead.	 */
	if (!cpu_has_tlbinv)
		r4k_tlb_uniquify();
	local_flush_tlb_all();

	/* Did I tell you that ARC SUCKS?  */
}

void tlb_init(void)
{
	r4k_tlb_configure();

	if (ntlb) {
		if (ntlb > 1 && ntlb <= current_cpu_data.tlbsize) {
			int wired = current_cpu_data.tlbsize - ntlb;
			write_c0_wired(wired);
			write_c0_index(wired-1);
			printk("Restricting TLB to %d entries\n", ntlb);
		} else
			printk("Ignoring invalid argument ntlb=%d\n", ntlb);
	}

	build_tlb_refill_handler();
}

static int r4k_tlb_pm_notifier(struct notifier_block *self, unsigned long cmd,
			       void *v)
{
	switch (cmd) {
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		r4k_tlb_configure();
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block r4k_tlb_pm_notifier_block = {
	.notifier_call = r4k_tlb_pm_notifier,
};

static int __init r4k_tlb_init_pm(void)
{
	return cpu_pm_register_notifier(&r4k_tlb_pm_notifier_block);
}
arch_initcall(r4k_tlb_init_pm);

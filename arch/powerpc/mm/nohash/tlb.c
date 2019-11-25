// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains the routines for TLB flushing.
 * On machines where the MMU does not use a hash table to store virtual to
 * physical translations (ie, SW loaded TLBs or Book3E compilant processors,
 * this does -not- include 603 however which shares the implementation with
 * hash based processors)
 *
 *  -- BenH
 *
 * Copyright 2008,2009 Ben Herrenschmidt <benh@kernel.crashing.org>
 *                     IBM Corp.
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/hugetlb.h>

#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/code-patching.h>
#include <asm/cputhreads.h>
#include <asm/hugetlb.h>
#include <asm/paca.h>

#include <mm/mmu_decl.h>

/*
 * This struct lists the sw-supported page sizes.  The hardawre MMU may support
 * other sizes not listed here.   The .ind field is only used on MMUs that have
 * indirect page table entries.
 */
#if defined(CONFIG_PPC_BOOK3E_MMU) || defined(CONFIG_PPC_8xx)
#ifdef CONFIG_PPC_FSL_BOOK3E
struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT] = {
	[MMU_PAGE_4K] = {
		.shift	= 12,
		.enc	= BOOK3E_PAGESZ_4K,
	},
	[MMU_PAGE_2M] = {
		.shift	= 21,
		.enc	= BOOK3E_PAGESZ_2M,
	},
	[MMU_PAGE_4M] = {
		.shift	= 22,
		.enc	= BOOK3E_PAGESZ_4M,
	},
	[MMU_PAGE_16M] = {
		.shift	= 24,
		.enc	= BOOK3E_PAGESZ_16M,
	},
	[MMU_PAGE_64M] = {
		.shift	= 26,
		.enc	= BOOK3E_PAGESZ_64M,
	},
	[MMU_PAGE_256M] = {
		.shift	= 28,
		.enc	= BOOK3E_PAGESZ_256M,
	},
	[MMU_PAGE_1G] = {
		.shift	= 30,
		.enc	= BOOK3E_PAGESZ_1GB,
	},
};
#elif defined(CONFIG_PPC_8xx)
struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT] = {
	/* we only manage 4k and 16k pages as normal pages */
#ifdef CONFIG_PPC_4K_PAGES
	[MMU_PAGE_4K] = {
		.shift	= 12,
	},
#else
	[MMU_PAGE_16K] = {
		.shift	= 14,
	},
#endif
	[MMU_PAGE_512K] = {
		.shift	= 19,
	},
	[MMU_PAGE_8M] = {
		.shift	= 23,
	},
};
#else
struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT] = {
	[MMU_PAGE_4K] = {
		.shift	= 12,
		.ind	= 20,
		.enc	= BOOK3E_PAGESZ_4K,
	},
	[MMU_PAGE_16K] = {
		.shift	= 14,
		.enc	= BOOK3E_PAGESZ_16K,
	},
	[MMU_PAGE_64K] = {
		.shift	= 16,
		.ind	= 28,
		.enc	= BOOK3E_PAGESZ_64K,
	},
	[MMU_PAGE_1M] = {
		.shift	= 20,
		.enc	= BOOK3E_PAGESZ_1M,
	},
	[MMU_PAGE_16M] = {
		.shift	= 24,
		.ind	= 36,
		.enc	= BOOK3E_PAGESZ_16M,
	},
	[MMU_PAGE_256M] = {
		.shift	= 28,
		.enc	= BOOK3E_PAGESZ_256M,
	},
	[MMU_PAGE_1G] = {
		.shift	= 30,
		.enc	= BOOK3E_PAGESZ_1GB,
	},
};
#endif /* CONFIG_FSL_BOOKE */

static inline int mmu_get_tsize(int psize)
{
	return mmu_psize_defs[psize].enc;
}
#else
static inline int mmu_get_tsize(int psize)
{
	/* This isn't used on !Book3E for now */
	return 0;
}
#endif /* CONFIG_PPC_BOOK3E_MMU */

/* The variables below are currently only used on 64-bit Book3E
 * though this will probably be made common with other nohash
 * implementations at some point
 */
#ifdef CONFIG_PPC64

int mmu_linear_psize;		/* Page size used for the linear mapping */
int mmu_pte_psize;		/* Page size used for PTE pages */
int mmu_vmemmap_psize;		/* Page size used for the virtual mem map */
int book3e_htw_mode;		/* HW tablewalk?  Value is PPC_HTW_* */
unsigned long linear_map_top;	/* Top of linear mapping */


/*
 * Number of bytes to add to SPRN_SPRG_TLB_EXFRAME on crit/mcheck/debug
 * exceptions.  This is used for bolted and e6500 TLB miss handlers which
 * do not modify this SPRG in the TLB miss code; for other TLB miss handlers,
 * this is set to zero.
 */
int extlb_level_exc;

#endif /* CONFIG_PPC64 */

#ifdef CONFIG_PPC_FSL_BOOK3E
/* next_tlbcam_idx is used to round-robin tlbcam entry assignment */
DEFINE_PER_CPU(int, next_tlbcam_idx);
EXPORT_PER_CPU_SYMBOL(next_tlbcam_idx);
#endif

/*
 * Base TLB flushing operations:
 *
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes kernel pages
 *
 *  - local_* variants of page and mm only apply to the current
 *    processor
 */

/*
 * These are the base non-SMP variants of page and mm flushing
 */
void local_flush_tlb_mm(struct mm_struct *mm)
{
	unsigned int pid;

	preempt_disable();
	pid = mm->context.id;
	if (pid != MMU_NO_CONTEXT)
		_tlbil_pid(pid);
	preempt_enable();
}
EXPORT_SYMBOL(local_flush_tlb_mm);

void __local_flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
			    int tsize, int ind)
{
	unsigned int pid;

	preempt_disable();
	pid = mm ? mm->context.id : 0;
	if (pid != MMU_NO_CONTEXT)
		_tlbil_va(vmaddr, pid, tsize, ind);
	preempt_enable();
}

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	__local_flush_tlb_page(vma ? vma->vm_mm : NULL, vmaddr,
			       mmu_get_tsize(mmu_virtual_psize), 0);
}
EXPORT_SYMBOL(local_flush_tlb_page);

/*
 * And here are the SMP non-local implementations
 */
#ifdef CONFIG_SMP

static DEFINE_RAW_SPINLOCK(tlbivax_lock);

struct tlb_flush_param {
	unsigned long addr;
	unsigned int pid;
	unsigned int tsize;
	unsigned int ind;
};

static void do_flush_tlb_mm_ipi(void *param)
{
	struct tlb_flush_param *p = param;

	_tlbil_pid(p ? p->pid : 0);
}

static void do_flush_tlb_page_ipi(void *param)
{
	struct tlb_flush_param *p = param;

	_tlbil_va(p->addr, p->pid, p->tsize, p->ind);
}


/* Note on invalidations and PID:
 *
 * We snapshot the PID with preempt disabled. At this point, it can still
 * change either because:
 * - our context is being stolen (PID -> NO_CONTEXT) on another CPU
 * - we are invaliating some target that isn't currently running here
 *   and is concurrently acquiring a new PID on another CPU
 * - some other CPU is re-acquiring a lost PID for this mm
 * etc...
 *
 * However, this shouldn't be a problem as we only guarantee
 * invalidation of TLB entries present prior to this call, so we
 * don't care about the PID changing, and invalidating a stale PID
 * is generally harmless.
 */

void flush_tlb_mm(struct mm_struct *mm)
{
	unsigned int pid;

	preempt_disable();
	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		goto no_context;
	if (!mm_is_core_local(mm)) {
		struct tlb_flush_param p = { .pid = pid };
		/* Ignores smp_processor_id() even if set. */
		smp_call_function_many(mm_cpumask(mm),
				       do_flush_tlb_mm_ipi, &p, 1);
	}
	_tlbil_pid(pid);
 no_context:
	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_mm);

void __flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
		      int tsize, int ind)
{
	struct cpumask *cpu_mask;
	unsigned int pid;

	/*
	 * This function as well as __local_flush_tlb_page() must only be called
	 * for user contexts.
	 */
	if (WARN_ON(!mm))
		return;

	preempt_disable();
	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		goto bail;
	cpu_mask = mm_cpumask(mm);
	if (!mm_is_core_local(mm)) {
		/* If broadcast tlbivax is supported, use it */
		if (mmu_has_feature(MMU_FTR_USE_TLBIVAX_BCAST)) {
			int lock = mmu_has_feature(MMU_FTR_LOCK_BCAST_INVAL);
			if (lock)
				raw_spin_lock(&tlbivax_lock);
			_tlbivax_bcast(vmaddr, pid, tsize, ind);
			if (lock)
				raw_spin_unlock(&tlbivax_lock);
			goto bail;
		} else {
			struct tlb_flush_param p = {
				.pid = pid,
				.addr = vmaddr,
				.tsize = tsize,
				.ind = ind,
			};
			/* Ignores smp_processor_id() even if set in cpu_mask */
			smp_call_function_many(cpu_mask,
					       do_flush_tlb_page_ipi, &p, 1);
		}
	}
	_tlbil_va(vmaddr, pid, tsize, ind);
 bail:
	preempt_enable();
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
#ifdef CONFIG_HUGETLB_PAGE
	if (vma && is_vm_hugetlb_page(vma))
		flush_hugetlb_page(vma, vmaddr);
#endif

	__flush_tlb_page(vma ? vma->vm_mm : NULL, vmaddr,
			 mmu_get_tsize(mmu_virtual_psize), 0);
}
EXPORT_SYMBOL(flush_tlb_page);

#endif /* CONFIG_SMP */

#ifdef CONFIG_PPC_47x
void __init early_init_mmu_47x(void)
{
#ifdef CONFIG_SMP
	unsigned long root = of_get_flat_dt_root();
	if (of_get_flat_dt_prop(root, "cooperative-partition", NULL))
		mmu_clear_feature(MMU_FTR_USE_TLBIVAX_BCAST);
#endif /* CONFIG_SMP */
}
#endif /* CONFIG_PPC_47x */

/*
 * Flush kernel TLB entries in the given range
 */
void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
#ifdef CONFIG_SMP
	preempt_disable();
	smp_call_function(do_flush_tlb_mm_ipi, NULL, 1);
	_tlbil_pid(0);
	preempt_enable();
#else
	_tlbil_pid(0);
#endif
}
EXPORT_SYMBOL(flush_tlb_kernel_range);

/*
 * Currently, for range flushing, we just do a full mm flush. This should
 * be optimized based on a threshold on the size of the range, since
 * some implementation can stack multiple tlbivax before a tlbsync but
 * for now, we keep it that way
 */
void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)

{
	if (end - start == PAGE_SIZE && !(start & ~PAGE_MASK))
		flush_tlb_page(vma, start);
	else
		flush_tlb_mm(vma->vm_mm);
}
EXPORT_SYMBOL(flush_tlb_range);

void tlb_flush(struct mmu_gather *tlb)
{
	flush_tlb_mm(tlb->mm);
}

/*
 * Below are functions specific to the 64-bit variant of Book3E though that
 * may change in the future
 */

#ifdef CONFIG_PPC64

/*
 * Handling of virtual linear page tables or indirect TLB entries
 * flushing when PTE pages are freed
 */
void tlb_flush_pgtable(struct mmu_gather *tlb, unsigned long address)
{
	int tsize = mmu_psize_defs[mmu_pte_psize].enc;

	if (book3e_htw_mode != PPC_HTW_NONE) {
		unsigned long start = address & PMD_MASK;
		unsigned long end = address + PMD_SIZE;
		unsigned long size = 1UL << mmu_psize_defs[mmu_pte_psize].shift;

		/* This isn't the most optimal, ideally we would factor out the
		 * while preempt & CPU mask mucking around, or even the IPI but
		 * it will do for now
		 */
		while (start < end) {
			__flush_tlb_page(tlb->mm, start, tsize, 1);
			start += size;
		}
	} else {
		unsigned long rmask = 0xf000000000000000ul;
		unsigned long rid = (address & rmask) | 0x1000000000000000ul;
		unsigned long vpte = address & ~rmask;

		vpte = (vpte >> (PAGE_SHIFT - 3)) & ~0xffful;
		vpte |= rid;
		__flush_tlb_page(tlb->mm, vpte, tsize, 0);
	}
}

static void setup_page_sizes(void)
{
	unsigned int tlb0cfg;
	unsigned int tlb0ps;
	unsigned int eptcfg;
	int i, psize;

#ifdef CONFIG_PPC_FSL_BOOK3E
	unsigned int mmucfg = mfspr(SPRN_MMUCFG);
	int fsl_mmu = mmu_has_feature(MMU_FTR_TYPE_FSL_E);

	if (fsl_mmu && (mmucfg & MMUCFG_MAVN) == MMUCFG_MAVN_V1) {
		unsigned int tlb1cfg = mfspr(SPRN_TLB1CFG);
		unsigned int min_pg, max_pg;

		min_pg = (tlb1cfg & TLBnCFG_MINSIZE) >> TLBnCFG_MINSIZE_SHIFT;
		max_pg = (tlb1cfg & TLBnCFG_MAXSIZE) >> TLBnCFG_MAXSIZE_SHIFT;

		for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
			struct mmu_psize_def *def;
			unsigned int shift;

			def = &mmu_psize_defs[psize];
			shift = def->shift;

			if (shift == 0 || shift & 1)
				continue;

			/* adjust to be in terms of 4^shift Kb */
			shift = (shift - 10) >> 1;

			if ((shift >= min_pg) && (shift <= max_pg))
				def->flags |= MMU_PAGE_SIZE_DIRECT;
		}

		goto out;
	}

	if (fsl_mmu && (mmucfg & MMUCFG_MAVN) == MMUCFG_MAVN_V2) {
		u32 tlb1cfg, tlb1ps;

		tlb0cfg = mfspr(SPRN_TLB0CFG);
		tlb1cfg = mfspr(SPRN_TLB1CFG);
		tlb1ps = mfspr(SPRN_TLB1PS);
		eptcfg = mfspr(SPRN_EPTCFG);

		if ((tlb1cfg & TLBnCFG_IND) && (tlb0cfg & TLBnCFG_PT))
			book3e_htw_mode = PPC_HTW_E6500;

		/*
		 * We expect 4K subpage size and unrestricted indirect size.
		 * The lack of a restriction on indirect size is a Freescale
		 * extension, indicated by PSn = 0 but SPSn != 0.
		 */
		if (eptcfg != 2)
			book3e_htw_mode = PPC_HTW_NONE;

		for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
			struct mmu_psize_def *def = &mmu_psize_defs[psize];

			if (!def->shift)
				continue;

			if (tlb1ps & (1U << (def->shift - 10))) {
				def->flags |= MMU_PAGE_SIZE_DIRECT;

				if (book3e_htw_mode && psize == MMU_PAGE_2M)
					def->flags |= MMU_PAGE_SIZE_INDIRECT;
			}
		}

		goto out;
	}
#endif

	tlb0cfg = mfspr(SPRN_TLB0CFG);
	tlb0ps = mfspr(SPRN_TLB0PS);
	eptcfg = mfspr(SPRN_EPTCFG);

	/* Look for supported direct sizes */
	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		struct mmu_psize_def *def = &mmu_psize_defs[psize];

		if (tlb0ps & (1U << (def->shift - 10)))
			def->flags |= MMU_PAGE_SIZE_DIRECT;
	}

	/* Indirect page sizes supported ? */
	if ((tlb0cfg & TLBnCFG_IND) == 0 ||
	    (tlb0cfg & TLBnCFG_PT) == 0)
		goto out;

	book3e_htw_mode = PPC_HTW_IBM;

	/* Now, we only deal with one IND page size for each
	 * direct size. Hopefully all implementations today are
	 * unambiguous, but we might want to be careful in the
	 * future.
	 */
	for (i = 0; i < 3; i++) {
		unsigned int ps, sps;

		sps = eptcfg & 0x1f;
		eptcfg >>= 5;
		ps = eptcfg & 0x1f;
		eptcfg >>= 5;
		if (!ps || !sps)
			continue;
		for (psize = 0; psize < MMU_PAGE_COUNT; psize++) {
			struct mmu_psize_def *def = &mmu_psize_defs[psize];

			if (ps == (def->shift - 10))
				def->flags |= MMU_PAGE_SIZE_INDIRECT;
			if (sps == (def->shift - 10))
				def->ind = ps + 10;
		}
	}

out:
	/* Cleanup array and print summary */
	pr_info("MMU: Supported page sizes\n");
	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		struct mmu_psize_def *def = &mmu_psize_defs[psize];
		const char *__page_type_names[] = {
			"unsupported",
			"direct",
			"indirect",
			"direct & indirect"
		};
		if (def->flags == 0) {
			def->shift = 0;	
			continue;
		}
		pr_info("  %8ld KB as %s\n", 1ul << (def->shift - 10),
			__page_type_names[def->flags & 0x3]);
	}
}

static void setup_mmu_htw(void)
{
	/*
	 * If we want to use HW tablewalk, enable it by patching the TLB miss
	 * handlers to branch to the one dedicated to it.
	 */

	switch (book3e_htw_mode) {
	case PPC_HTW_IBM:
		patch_exception(0x1c0, exc_data_tlb_miss_htw_book3e);
		patch_exception(0x1e0, exc_instruction_tlb_miss_htw_book3e);
		break;
#ifdef CONFIG_PPC_FSL_BOOK3E
	case PPC_HTW_E6500:
		extlb_level_exc = EX_TLB_SIZE;
		patch_exception(0x1c0, exc_data_tlb_miss_e6500_book3e);
		patch_exception(0x1e0, exc_instruction_tlb_miss_e6500_book3e);
		break;
#endif
	}
	pr_info("MMU: Book3E HW tablewalk %s\n",
		book3e_htw_mode != PPC_HTW_NONE ? "enabled" : "not supported");
}

/*
 * Early initialization of the MMU TLB code
 */
static void early_init_this_mmu(void)
{
	unsigned int mas4;

	/* Set MAS4 based on page table setting */

	mas4 = 0x4 << MAS4_WIMGED_SHIFT;
	switch (book3e_htw_mode) {
	case PPC_HTW_E6500:
		mas4 |= MAS4_INDD;
		mas4 |= BOOK3E_PAGESZ_2M << MAS4_TSIZED_SHIFT;
		mas4 |= MAS4_TLBSELD(1);
		mmu_pte_psize = MMU_PAGE_2M;
		break;

	case PPC_HTW_IBM:
		mas4 |= MAS4_INDD;
		mas4 |=	BOOK3E_PAGESZ_1M << MAS4_TSIZED_SHIFT;
		mmu_pte_psize = MMU_PAGE_1M;
		break;

	case PPC_HTW_NONE:
		mas4 |=	BOOK3E_PAGESZ_4K << MAS4_TSIZED_SHIFT;
		mmu_pte_psize = mmu_virtual_psize;
		break;
	}
	mtspr(SPRN_MAS4, mas4);

#ifdef CONFIG_PPC_FSL_BOOK3E
	if (mmu_has_feature(MMU_FTR_TYPE_FSL_E)) {
		unsigned int num_cams;
		bool map = true;

		/* use a quarter of the TLBCAM for bolted linear map */
		num_cams = (mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY) / 4;

		/*
		 * Only do the mapping once per core, or else the
		 * transient mapping would cause problems.
		 */
#ifdef CONFIG_SMP
		if (hweight32(get_tensr()) > 1)
			map = false;
#endif

		if (map)
			linear_map_top = map_mem_in_cams(linear_map_top,
							 num_cams, false);
	}
#endif

	/* A sync won't hurt us after mucking around with
	 * the MMU configuration
	 */
	mb();
}

static void __init early_init_mmu_global(void)
{
	/* XXX This will have to be decided at runtime, but right
	 * now our boot and TLB miss code hard wires it. Ideally
	 * we should find out a suitable page size and patch the
	 * TLB miss code (either that or use the PACA to store
	 * the value we want)
	 */
	mmu_linear_psize = MMU_PAGE_1G;

	/* XXX This should be decided at runtime based on supported
	 * page sizes in the TLB, but for now let's assume 16M is
	 * always there and a good fit (which it probably is)
	 *
	 * Freescale booke only supports 4K pages in TLB0, so use that.
	 */
	if (mmu_has_feature(MMU_FTR_TYPE_FSL_E))
		mmu_vmemmap_psize = MMU_PAGE_4K;
	else
		mmu_vmemmap_psize = MMU_PAGE_16M;

	/* XXX This code only checks for TLB 0 capabilities and doesn't
	 *     check what page size combos are supported by the HW. It
	 *     also doesn't handle the case where a separate array holds
	 *     the IND entries from the array loaded by the PT.
	 */
	/* Look for supported page sizes */
	setup_page_sizes();

	/* Look for HW tablewalk support */
	setup_mmu_htw();

#ifdef CONFIG_PPC_FSL_BOOK3E
	if (mmu_has_feature(MMU_FTR_TYPE_FSL_E)) {
		if (book3e_htw_mode == PPC_HTW_NONE) {
			extlb_level_exc = EX_TLB_SIZE;
			patch_exception(0x1c0, exc_data_tlb_miss_bolted_book3e);
			patch_exception(0x1e0,
				exc_instruction_tlb_miss_bolted_book3e);
		}
	}
#endif

	/* Set the global containing the top of the linear mapping
	 * for use by the TLB miss code
	 */
	linear_map_top = memblock_end_of_DRAM();

	ioremap_bot = IOREMAP_BASE;
}

static void __init early_mmu_set_memory_limit(void)
{
#ifdef CONFIG_PPC_FSL_BOOK3E
	if (mmu_has_feature(MMU_FTR_TYPE_FSL_E)) {
		/*
		 * Limit memory so we dont have linear faults.
		 * Unlike memblock_set_current_limit, which limits
		 * memory available during early boot, this permanently
		 * reduces the memory available to Linux.  We need to
		 * do this because highmem is not supported on 64-bit.
		 */
		memblock_enforce_memory_limit(linear_map_top);
	}
#endif

	memblock_set_current_limit(linear_map_top);
}

/* boot cpu only */
void __init early_init_mmu(void)
{
	early_init_mmu_global();
	early_init_this_mmu();
	early_mmu_set_memory_limit();
}

void early_init_mmu_secondary(void)
{
	early_init_this_mmu();
}

void setup_initial_memory_limit(phys_addr_t first_memblock_base,
				phys_addr_t first_memblock_size)
{
	/* On non-FSL Embedded 64-bit, we adjust the RMA size to match
	 * the bolted TLB entry. We know for now that only 1G
	 * entries are supported though that may eventually
	 * change.
	 *
	 * on FSL Embedded 64-bit, usually all RAM is bolted, but with
	 * unusual memory sizes it's possible for some RAM to not be mapped
	 * (such RAM is not used at all by Linux, since we don't support
	 * highmem on 64-bit).  We limit ppc64_rma_size to what would be
	 * mappable if this memblock is the only one.  Additional memblocks
	 * can only increase, not decrease, the amount that ends up getting
	 * mapped.  We still limit max to 1G even if we'll eventually map
	 * more.  This is due to what the early init code is set up to do.
	 *
	 * We crop it to the size of the first MEMBLOCK to
	 * avoid going over total available memory just in case...
	 */
#ifdef CONFIG_PPC_FSL_BOOK3E
	if (early_mmu_has_feature(MMU_FTR_TYPE_FSL_E)) {
		unsigned long linear_sz;
		unsigned int num_cams;

		/* use a quarter of the TLBCAM for bolted linear map */
		num_cams = (mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY) / 4;

		linear_sz = map_mem_in_cams(first_memblock_size, num_cams,
					    true);

		ppc64_rma_size = min_t(u64, linear_sz, 0x40000000);
	} else
#endif
		ppc64_rma_size = min_t(u64, first_memblock_size, 0x40000000);

	/* Finally limit subsequent allocations */
	memblock_set_current_limit(first_memblock_base + ppc64_rma_size);
}
#else /* ! CONFIG_PPC64 */
void __init early_init_mmu(void)
{
#ifdef CONFIG_PPC_47x
	early_init_mmu_47x();
#endif

#ifdef CONFIG_PPC_MM_SLICES
	mm_ctx_set_slb_addr_limit(&init_mm.context, SLB_ADDR_LIMIT_DEFAULT);
#endif
}
#endif /* CONFIG_PPC64 */

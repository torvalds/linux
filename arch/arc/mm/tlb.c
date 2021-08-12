// SPDX-License-Identifier: GPL-2.0-only
/*
 * TLB Management (flush/create/diagnostics) for MMUv3 and MMUv4
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 */

#include <linux/module.h>
#include <linux/bug.h>
#include <linux/mm_types.h>

#include <asm/arcregs.h>
#include <asm/setup.h>
#include <asm/mmu_context.h>
#include <asm/mmu.h>

/* A copy of the ASID from the PID reg is kept in asid_cache */
DEFINE_PER_CPU(unsigned int, asid_cache) = MM_CTXT_FIRST_CYCLE;

static int __read_mostly pae_exists;

/*
 * Utility Routine to erase a J-TLB entry
 * Caller needs to setup Index Reg (manually or via getIndex)
 */
static inline void __tlb_entry_erase(void)
{
	write_aux_reg(ARC_REG_TLBPD1, 0);

	if (is_pae40_enabled())
		write_aux_reg(ARC_REG_TLBPD1HI, 0);

	write_aux_reg(ARC_REG_TLBPD0, 0);
	write_aux_reg(ARC_REG_TLBCOMMAND, TLBWrite);
}

static void utlb_invalidate(void)
{
	write_aux_reg(ARC_REG_TLBCOMMAND, TLBIVUTLB);
}

#ifdef CONFIG_ARC_MMU_V3

static inline unsigned int tlb_entry_lkup(unsigned long vaddr_n_asid)
{
	unsigned int idx;

	write_aux_reg(ARC_REG_TLBPD0, vaddr_n_asid);

	write_aux_reg(ARC_REG_TLBCOMMAND, TLBProbe);
	idx = read_aux_reg(ARC_REG_TLBINDEX);

	return idx;
}

static void tlb_entry_erase(unsigned int vaddr_n_asid)
{
	unsigned int idx;

	/* Locate the TLB entry for this vaddr + ASID */
	idx = tlb_entry_lkup(vaddr_n_asid);

	/* No error means entry found, zero it out */
	if (likely(!(idx & TLB_LKUP_ERR))) {
		__tlb_entry_erase();
	} else {
		/* Duplicate entry error */
		WARN(idx == TLB_DUP_ERR, "Probe returned Dup PD for %x\n",
					   vaddr_n_asid);
	}
}

static void tlb_entry_insert(unsigned int pd0, phys_addr_t pd1)
{
	unsigned int idx;

	/*
	 * First verify if entry for this vaddr+ASID already exists
	 * This also sets up PD0 (vaddr, ASID..) for final commit
	 */
	idx = tlb_entry_lkup(pd0);

	/*
	 * If Not already present get a free slot from MMU.
	 * Otherwise, Probe would have located the entry and set INDEX Reg
	 * with existing location. This will cause Write CMD to over-write
	 * existing entry with new PD0 and PD1
	 */
	if (likely(idx & TLB_LKUP_ERR))
		write_aux_reg(ARC_REG_TLBCOMMAND, TLBGetIndex);

	/* setup the other half of TLB entry (pfn, rwx..) */
	write_aux_reg(ARC_REG_TLBPD1, pd1);

	/*
	 * Commit the Entry to MMU
	 * It doesn't sound safe to use the TLBWriteNI cmd here
	 * which doesn't flush uTLBs. I'd rather be safe than sorry.
	 */
	write_aux_reg(ARC_REG_TLBCOMMAND, TLBWrite);
}

#else	/* MMUv4 */

static void tlb_entry_erase(unsigned int vaddr_n_asid)
{
	write_aux_reg(ARC_REG_TLBPD0, vaddr_n_asid | _PAGE_PRESENT);
	write_aux_reg(ARC_REG_TLBCOMMAND, TLBDeleteEntry);
}

static void tlb_entry_insert(unsigned int pd0, phys_addr_t pd1)
{
	write_aux_reg(ARC_REG_TLBPD0, pd0);

	if (!is_pae40_enabled()) {
		write_aux_reg(ARC_REG_TLBPD1, pd1);
	} else {
		write_aux_reg(ARC_REG_TLBPD1, pd1 & 0xFFFFFFFF);
		write_aux_reg(ARC_REG_TLBPD1HI, (u64)pd1 >> 32);
	}

	write_aux_reg(ARC_REG_TLBCOMMAND, TLBInsertEntry);
}

#endif

/*
 * Un-conditionally (without lookup) erase the entire MMU contents
 */

noinline void local_flush_tlb_all(void)
{
	struct cpuinfo_arc_mmu *mmu = &cpuinfo_arc700[smp_processor_id()].mmu;
	unsigned long flags;
	unsigned int entry;
	int num_tlb = mmu->sets * mmu->ways;

	local_irq_save(flags);

	/* Load PD0 and PD1 with template for a Blank Entry */
	write_aux_reg(ARC_REG_TLBPD1, 0);

	if (is_pae40_enabled())
		write_aux_reg(ARC_REG_TLBPD1HI, 0);

	write_aux_reg(ARC_REG_TLBPD0, 0);

	for (entry = 0; entry < num_tlb; entry++) {
		/* write this entry to the TLB */
		write_aux_reg(ARC_REG_TLBINDEX, entry);
		write_aux_reg(ARC_REG_TLBCOMMAND, TLBWriteNI);
	}

	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
		const int stlb_idx = 0x800;

		/* Blank sTLB entry */
		write_aux_reg(ARC_REG_TLBPD0, _PAGE_HW_SZ);

		for (entry = stlb_idx; entry < stlb_idx + 16; entry++) {
			write_aux_reg(ARC_REG_TLBINDEX, entry);
			write_aux_reg(ARC_REG_TLBCOMMAND, TLBWriteNI);
		}
	}

	utlb_invalidate();

	local_irq_restore(flags);
}

/*
 * Flush the entire MM for userland. The fastest way is to move to Next ASID
 */
noinline void local_flush_tlb_mm(struct mm_struct *mm)
{
	/*
	 * Small optimisation courtesy IA64
	 * flush_mm called during fork,exit,munmap etc, multiple times as well.
	 * Only for fork( ) do we need to move parent to a new MMU ctxt,
	 * all other cases are NOPs, hence this check.
	 */
	if (atomic_read(&mm->mm_users) == 0)
		return;

	/*
	 * - Move to a new ASID, but only if the mm is still wired in
	 *   (Android Binder ended up calling this for vma->mm != tsk->mm,
	 *    causing h/w - s/w ASID to get out of sync)
	 * - Also get_new_mmu_context() new implementation allocates a new
	 *   ASID only if it is not allocated already - so unallocate first
	 */
	destroy_context(mm);
	if (current->mm == mm)
		get_new_mmu_context(mm);
}

/*
 * Flush a Range of TLB entries for userland.
 * @start is inclusive, while @end is exclusive
 * Difference between this and Kernel Range Flush is
 *  -Here the fastest way (if range is too large) is to move to next ASID
 *      without doing any explicit Shootdown
 *  -In case of kernel Flush, entry has to be shot down explicitly
 */
void local_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			   unsigned long end)
{
	const unsigned int cpu = smp_processor_id();
	unsigned long flags;

	/* If range @start to @end is more than 32 TLB entries deep,
	 * its better to move to a new ASID rather than searching for
	 * individual entries and then shooting them down
	 *
	 * The calc above is rough, doesn't account for unaligned parts,
	 * since this is heuristics based anyways
	 */
	if (unlikely((end - start) >= PAGE_SIZE * 32)) {
		local_flush_tlb_mm(vma->vm_mm);
		return;
	}

	/*
	 * @start moved to page start: this alone suffices for checking
	 * loop end condition below, w/o need for aligning @end to end
	 * e.g. 2000 to 4001 will anyhow loop twice
	 */
	start &= PAGE_MASK;

	local_irq_save(flags);

	if (asid_mm(vma->vm_mm, cpu) != MM_CTXT_NO_ASID) {
		while (start < end) {
			tlb_entry_erase(start | hw_pid(vma->vm_mm, cpu));
			start += PAGE_SIZE;
		}
	}

	local_irq_restore(flags);
}

/* Flush the kernel TLB entries - vmalloc/modules (Global from MMU perspective)
 *  @start, @end interpreted as kvaddr
 * Interestingly, shared TLB entries can also be flushed using just
 * @start,@end alone (interpreted as user vaddr), although technically SASID
 * is also needed. However our smart TLbProbe lookup takes care of that.
 */
void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	/* exactly same as above, except for TLB entry not taking ASID */

	if (unlikely((end - start) >= PAGE_SIZE * 32)) {
		local_flush_tlb_all();
		return;
	}

	start &= PAGE_MASK;

	local_irq_save(flags);
	while (start < end) {
		tlb_entry_erase(start);
		start += PAGE_SIZE;
	}

	local_irq_restore(flags);
}

/*
 * Delete TLB entry in MMU for a given page (??? address)
 * NOTE One TLB entry contains translation for single PAGE
 */

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	const unsigned int cpu = smp_processor_id();
	unsigned long flags;

	/* Note that it is critical that interrupts are DISABLED between
	 * checking the ASID and using it flush the TLB entry
	 */
	local_irq_save(flags);

	if (asid_mm(vma->vm_mm, cpu) != MM_CTXT_NO_ASID) {
		tlb_entry_erase((page & PAGE_MASK) | hw_pid(vma->vm_mm, cpu));
	}

	local_irq_restore(flags);
}

#ifdef CONFIG_SMP

struct tlb_args {
	struct vm_area_struct *ta_vma;
	unsigned long ta_start;
	unsigned long ta_end;
};

static inline void ipi_flush_tlb_page(void *arg)
{
	struct tlb_args *ta = arg;

	local_flush_tlb_page(ta->ta_vma, ta->ta_start);
}

static inline void ipi_flush_tlb_range(void *arg)
{
	struct tlb_args *ta = arg;

	local_flush_tlb_range(ta->ta_vma, ta->ta_start, ta->ta_end);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline void ipi_flush_pmd_tlb_range(void *arg)
{
	struct tlb_args *ta = arg;

	local_flush_pmd_tlb_range(ta->ta_vma, ta->ta_start, ta->ta_end);
}
#endif

static inline void ipi_flush_tlb_kernel_range(void *arg)
{
	struct tlb_args *ta = (struct tlb_args *)arg;

	local_flush_tlb_kernel_range(ta->ta_start, ta->ta_end);
}

void flush_tlb_all(void)
{
	on_each_cpu((smp_call_func_t)local_flush_tlb_all, NULL, 1);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	on_each_cpu_mask(mm_cpumask(mm), (smp_call_func_t)local_flush_tlb_mm,
			 mm, 1);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long uaddr)
{
	struct tlb_args ta = {
		.ta_vma = vma,
		.ta_start = uaddr
	};

	on_each_cpu_mask(mm_cpumask(vma->vm_mm), ipi_flush_tlb_page, &ta, 1);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	struct tlb_args ta = {
		.ta_vma = vma,
		.ta_start = start,
		.ta_end = end
	};

	on_each_cpu_mask(mm_cpumask(vma->vm_mm), ipi_flush_tlb_range, &ta, 1);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void flush_pmd_tlb_range(struct vm_area_struct *vma, unsigned long start,
			 unsigned long end)
{
	struct tlb_args ta = {
		.ta_vma = vma,
		.ta_start = start,
		.ta_end = end
	};

	on_each_cpu_mask(mm_cpumask(vma->vm_mm), ipi_flush_pmd_tlb_range, &ta, 1);
}
#endif

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	struct tlb_args ta = {
		.ta_start = start,
		.ta_end = end
	};

	on_each_cpu(ipi_flush_tlb_kernel_range, &ta, 1);
}
#endif

/*
 * Routine to create a TLB entry
 */
void create_tlb(struct vm_area_struct *vma, unsigned long vaddr, pte_t *ptep)
{
	unsigned long flags;
	unsigned int asid_or_sasid, rwx;
	unsigned long pd0;
	phys_addr_t pd1;

	/*
	 * create_tlb() assumes that current->mm == vma->mm, since
	 * -it ASID for TLB entry is fetched from MMU ASID reg (valid for curr)
	 * -completes the lazy write to SASID reg (again valid for curr tsk)
	 *
	 * Removing the assumption involves
	 * -Using vma->mm->context{ASID,SASID}, as opposed to MMU reg.
	 * -More importantly it makes this handler inconsistent with fast-path
	 *  TLB Refill handler which always deals with "current"
	 *
	 * Lets see the use cases when current->mm != vma->mm and we land here
	 *  1. execve->copy_strings()->__get_user_pages->handle_mm_fault
	 *     Here VM wants to pre-install a TLB entry for user stack while
	 *     current->mm still points to pre-execve mm (hence the condition).
	 *     However the stack vaddr is soon relocated (randomization) and
	 *     move_page_tables() tries to undo that TLB entry.
	 *     Thus not creating TLB entry is not any worse.
	 *
	 *  2. ptrace(POKETEXT) causes a CoW - debugger(current) inserting a
	 *     breakpoint in debugged task. Not creating a TLB now is not
	 *     performance critical.
	 *
	 * Both the cases above are not good enough for code churn.
	 */
	if (current->active_mm != vma->vm_mm)
		return;

	local_irq_save(flags);

	vaddr &= PAGE_MASK;

	/* update this PTE credentials */
	pte_val(*ptep) |= (_PAGE_PRESENT | _PAGE_ACCESSED);

	/* Create HW TLB(PD0,PD1) from PTE  */

	/* ASID for this task */
	asid_or_sasid = read_aux_reg(ARC_REG_PID) & 0xff;

	pd0 = vaddr | asid_or_sasid | (pte_val(*ptep) & PTE_BITS_IN_PD0);

	/*
	 * ARC MMU provides fully orthogonal access bits for K/U mode,
	 * however Linux only saves 1 set to save PTE real-estate
	 * Here we convert 3 PTE bits into 6 MMU bits:
	 * -Kernel only entries have Kr Kw Kx 0 0 0
	 * -User entries have mirrored K and U bits
	 */
	rwx = pte_val(*ptep) & PTE_BITS_RWX;

	if (pte_val(*ptep) & _PAGE_GLOBAL)
		rwx <<= 3;		/* r w x => Kr Kw Kx 0 0 0 */
	else
		rwx |= (rwx << 3);	/* r w x => Kr Kw Kx Ur Uw Ux */

	pd1 = rwx | (pte_val(*ptep) & PTE_BITS_NON_RWX_IN_PD1);

	tlb_entry_insert(pd0, pd1);

	local_irq_restore(flags);
}

/*
 * Called at the end of pagefault, for a userspace mapped page
 *  -pre-install the corresponding TLB entry into MMU
 *  -Finalize the delayed D-cache flush of kernel mapping of page due to
 *  	flush_dcache_page(), copy_user_page()
 *
 * Note that flush (when done) involves both WBACK - so physical page is
 * in sync as well as INV - so any non-congruent aliases don't remain
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long vaddr_unaligned,
		      pte_t *ptep)
{
	unsigned long vaddr = vaddr_unaligned & PAGE_MASK;
	phys_addr_t paddr = pte_val(*ptep) & PAGE_MASK_PHYS;
	struct page *page = pfn_to_page(pte_pfn(*ptep));

	create_tlb(vma, vaddr, ptep);

	if (page == ZERO_PAGE(0)) {
		return;
	}

	/*
	 * Exec page : Independent of aliasing/page-color considerations,
	 *	       since icache doesn't snoop dcache on ARC, any dirty
	 *	       K-mapping of a code page needs to be wback+inv so that
	 *	       icache fetch by userspace sees code correctly.
	 * !EXEC page: If K-mapping is NOT congruent to U-mapping, flush it
	 *	       so userspace sees the right data.
	 *  (Avoids the flush for Non-exec + congruent mapping case)
	 */
	if ((vma->vm_flags & VM_EXEC) ||
	     addr_not_cache_congruent(paddr, vaddr)) {

		int dirty = !test_and_set_bit(PG_dc_clean, &page->flags);
		if (dirty) {
			/* wback + inv dcache lines (K-mapping) */
			__flush_dcache_page(paddr, paddr);

			/* invalidate any existing icache lines (U-mapping) */
			if (vma->vm_flags & VM_EXEC)
				__inv_icache_page(paddr, vaddr);
		}
	}
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

/*
 * MMUv4 in HS38x cores supports Super Pages which are basis for Linux THP
 * support.
 *
 * Normal and Super pages can co-exist (ofcourse not overlap) in TLB with a
 * new bit "SZ" in TLB page descriptor to distinguish between them.
 * Super Page size is configurable in hardware (4K to 16M), but fixed once
 * RTL builds.
 *
 * The exact THP size a Linux configuration will support is a function of:
 *  - MMU page size (typical 8K, RTL fixed)
 *  - software page walker address split between PGD:PTE:PFN (typical
 *    11:8:13, but can be changed with 1 line)
 * So for above default, THP size supported is 8K * (2^8) = 2M
 *
 * Default Page Walker is 2 levels, PGD:PTE:PFN, which in THP regime
 * reduces to 1 level (as PTE is folded into PGD and canonically referred
 * to as PMD).
 * Thus THP PMD accessors are implemented in terms of PTE (just like sparc)
 */

void update_mmu_cache_pmd(struct vm_area_struct *vma, unsigned long addr,
				 pmd_t *pmd)
{
	pte_t pte = __pte(pmd_val(*pmd));
	update_mmu_cache(vma, addr, &pte);
}

void local_flush_pmd_tlb_range(struct vm_area_struct *vma, unsigned long start,
			       unsigned long end)
{
	unsigned int cpu;
	unsigned long flags;

	local_irq_save(flags);

	cpu = smp_processor_id();

	if (likely(asid_mm(vma->vm_mm, cpu) != MM_CTXT_NO_ASID)) {
		unsigned int asid = hw_pid(vma->vm_mm, cpu);

		/* No need to loop here: this will always be for 1 Huge Page */
		tlb_entry_erase(start | _PAGE_HW_SZ | asid);
	}

	local_irq_restore(flags);
}

#endif

/* Read the Cache Build Configuration Registers, Decode them and save into
 * the cpuinfo structure for later use.
 * No Validation is done here, simply read/convert the BCRs
 */
void read_decode_mmu_bcr(void)
{
	struct cpuinfo_arc_mmu *mmu = &cpuinfo_arc700[smp_processor_id()].mmu;
	unsigned int tmp;
	struct bcr_mmu_3 {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int ver:8, ways:4, sets:4, res:3, sasid:1, pg_sz:4,
		     u_itlb:4, u_dtlb:4;
#else
	unsigned int u_dtlb:4, u_itlb:4, pg_sz:4, sasid:1, res:3, sets:4,
		     ways:4, ver:8;
#endif
	} *mmu3;

	struct bcr_mmu_4 {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int ver:8, sasid:1, sz1:4, sz0:4, res:2, pae:1,
		     n_ways:2, n_entry:2, n_super:2, u_itlb:3, u_dtlb:3;
#else
	/*           DTLB      ITLB      JES        JE         JA      */
	unsigned int u_dtlb:3, u_itlb:3, n_super:2, n_entry:2, n_ways:2,
		     pae:1, res:2, sz0:4, sz1:4, sasid:1, ver:8;
#endif
	} *mmu4;

	tmp = read_aux_reg(ARC_REG_MMU_BCR);
	mmu->ver = (tmp >> 24);

	if (is_isa_arcompact() && mmu->ver == 3) {
		mmu3 = (struct bcr_mmu_3 *)&tmp;
		mmu->pg_sz_k = 1 << (mmu3->pg_sz - 1);
		mmu->sets = 1 << mmu3->sets;
		mmu->ways = 1 << mmu3->ways;
		mmu->u_dtlb = mmu3->u_dtlb;
		mmu->u_itlb = mmu3->u_itlb;
		mmu->sasid = mmu3->sasid;
	} else {
		mmu4 = (struct bcr_mmu_4 *)&tmp;
		mmu->pg_sz_k = 1 << (mmu4->sz0 - 1);
		mmu->s_pg_sz_m = 1 << (mmu4->sz1 - 11);
		mmu->sets = 64 << mmu4->n_entry;
		mmu->ways = mmu4->n_ways * 2;
		mmu->u_dtlb = mmu4->u_dtlb * 4;
		mmu->u_itlb = mmu4->u_itlb * 4;
		mmu->sasid = mmu4->sasid;
		pae_exists = mmu->pae = mmu4->pae;
	}
}

char *arc_mmu_mumbojumbo(int cpu_id, char *buf, int len)
{
	int n = 0;
	struct cpuinfo_arc_mmu *p_mmu = &cpuinfo_arc700[cpu_id].mmu;
	char super_pg[64] = "";

	if (p_mmu->s_pg_sz_m)
		scnprintf(super_pg, 64, "%dM Super Page %s",
			  p_mmu->s_pg_sz_m,
			  IS_USED_CFG(CONFIG_TRANSPARENT_HUGEPAGE));

	n += scnprintf(buf + n, len - n,
		      "MMU [v%x]\t: %dk PAGE, %sJTLB %d (%dx%d), uDTLB %d, uITLB %d%s%s\n",
		       p_mmu->ver, p_mmu->pg_sz_k, super_pg,
		       p_mmu->sets * p_mmu->ways, p_mmu->sets, p_mmu->ways,
		       p_mmu->u_dtlb, p_mmu->u_itlb,
		       IS_AVAIL2(p_mmu->pae, ", PAE40 ", CONFIG_ARC_HAS_PAE40));

	return buf;
}

int pae40_exist_but_not_enab(void)
{
	return pae_exists && !is_pae40_enabled();
}

void arc_mmu_init(void)
{
	struct cpuinfo_arc_mmu *mmu = &cpuinfo_arc700[smp_processor_id()].mmu;
	char str[256];
	int compat = 0;

	pr_info("%s", arc_mmu_mumbojumbo(0, str, sizeof(str)));

	/*
	 * Can't be done in processor.h due to header include dependencies
	 */
	BUILD_BUG_ON(!IS_ALIGNED((CONFIG_ARC_KVADDR_SIZE << 20), PMD_SIZE));

	/*
	 * stack top size sanity check,
	 * Can't be done in processor.h due to header include dependencies
	 */
	BUILD_BUG_ON(!IS_ALIGNED(STACK_TOP, PMD_SIZE));

	/*
	 * Ensure that MMU features assumed by kernel exist in hardware.
	 *  - For older ARC700 cpus, only v3 supported
	 *  - For HS cpus, v4 was baseline and v5 is backwards compatible
	 *    (will run older software).
	 */
	if (is_isa_arcompact() && mmu->ver == 3)
		compat = 1;
	else if (is_isa_arcv2() && mmu->ver >= 4)
		compat = 1;

	if (!compat)
		panic("MMU ver %d doesn't match kernel built for\n", mmu->ver);

	if (mmu->pg_sz_k != TO_KB(PAGE_SIZE))
		panic("MMU pg size != PAGE_SIZE (%luk)\n", TO_KB(PAGE_SIZE));

	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
	    mmu->s_pg_sz_m != TO_MB(HPAGE_PMD_SIZE))
		panic("MMU Super pg size != Linux HPAGE_PMD_SIZE (%luM)\n",
		      (unsigned long)TO_MB(HPAGE_PMD_SIZE));

	if (IS_ENABLED(CONFIG_ARC_HAS_PAE40) && !mmu->pae)
		panic("Hardware doesn't support PAE40\n");

	/* Enable the MMU with ASID 0 */
	mmu_setup_asid(NULL, 0);

	/* cache the pgd pointer in MMU SCRATCH reg (ARCv2 only) */
	mmu_setup_pgd(NULL, swapper_pg_dir);

	if (pae40_exist_but_not_enab())
		write_aux_reg(ARC_REG_TLBPD1HI, 0);
}

/*
 * TLB Programmer's Model uses Linear Indexes: 0 to {255, 511} for 128 x {2,4}
 * The mapping is Column-first.
 *		---------------------	-----------
 *		|way0|way1|way2|way3|	|way0|way1|
 *		---------------------	-----------
 * [set0]	|  0 |  1 |  2 |  3 |	|  0 |  1 |
 * [set1]	|  4 |  5 |  6 |  7 |	|  2 |  3 |
 *		~		    ~	~	  ~
 * [set127]	| 508| 509| 510| 511|	| 254| 255|
 *		---------------------	-----------
 * For normal operations we don't(must not) care how above works since
 * MMU cmd getIndex(vaddr) abstracts that out.
 * However for walking WAYS of a SET, we need to know this
 */
#define SET_WAY_TO_IDX(mmu, set, way)  ((set) * mmu->ways + (way))

/* Handling of Duplicate PD (TLB entry) in MMU.
 * -Could be due to buggy customer tapeouts or obscure kernel bugs
 * -MMU complaints not at the time of duplicate PD installation, but at the
 *      time of lookup matching multiple ways.
 * -Ideally these should never happen - but if they do - workaround by deleting
 *      the duplicate one.
 * -Knob to be verbose abt it.(TODO: hook them up to debugfs)
 */
volatile int dup_pd_silent; /* Be silent abt it or complain (default) */

void do_tlb_overlap_fault(unsigned long cause, unsigned long address,
			  struct pt_regs *regs)
{
	struct cpuinfo_arc_mmu *mmu = &cpuinfo_arc700[smp_processor_id()].mmu;
	unsigned long flags;
	int set, n_ways = mmu->ways;

	n_ways = min(n_ways, 4);
	BUG_ON(mmu->ways > 4);

	local_irq_save(flags);

	/* loop thru all sets of TLB */
	for (set = 0; set < mmu->sets; set++) {

		int is_valid, way;
		unsigned int pd0[4];

		/* read out all the ways of current set */
		for (way = 0, is_valid = 0; way < n_ways; way++) {
			write_aux_reg(ARC_REG_TLBINDEX,
					  SET_WAY_TO_IDX(mmu, set, way));
			write_aux_reg(ARC_REG_TLBCOMMAND, TLBRead);
			pd0[way] = read_aux_reg(ARC_REG_TLBPD0);
			is_valid |= pd0[way] & _PAGE_PRESENT;
			pd0[way] &= PAGE_MASK;
		}

		/* If all the WAYS in SET are empty, skip to next SET */
		if (!is_valid)
			continue;

		/* Scan the set for duplicate ways: needs a nested loop */
		for (way = 0; way < n_ways - 1; way++) {

			int n;

			if (!pd0[way])
				continue;

			for (n = way + 1; n < n_ways; n++) {
				if (pd0[way] != pd0[n])
					continue;

				if (!dup_pd_silent)
					pr_info("Dup TLB PD0 %08x @ set %d ways %d,%d\n",
						pd0[way], set, way, n);

				/*
				 * clear entry @way and not @n.
				 * This is critical to our optimised loop
				 */
				pd0[way] = 0;
				write_aux_reg(ARC_REG_TLBINDEX,
						SET_WAY_TO_IDX(mmu, set, way));
				__tlb_entry_erase();
			}
		}
	}

	local_irq_restore(flags);
}

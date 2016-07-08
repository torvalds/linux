/*
 * Page table handling routines for radix page table.
 *
 * Copyright 2015-2016, Aneesh Kumar K.V, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/sched.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/mmu.h>
#include <asm/firmware.h>

#include <trace/events/thp.h>

static int native_update_partition_table(u64 patb1)
{
	partition_tb->patb1 = cpu_to_be64(patb1);
	return 0;
}

static __ref void *early_alloc_pgtable(unsigned long size)
{
	void *pt;

	pt = __va(memblock_alloc_base(size, size, MEMBLOCK_ALLOC_ANYWHERE));
	memset(pt, 0, size);

	return pt;
}

int radix__map_kernel_page(unsigned long ea, unsigned long pa,
			  pgprot_t flags,
			  unsigned int map_page_size)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	/*
	 * Make sure task size is correct as per the max adddr
	 */
	BUILD_BUG_ON(TASK_SIZE_USER64 > RADIX_PGTABLE_RANGE);
	if (slab_is_available()) {
		pgdp = pgd_offset_k(ea);
		pudp = pud_alloc(&init_mm, pgdp, ea);
		if (!pudp)
			return -ENOMEM;
		if (map_page_size == PUD_SIZE) {
			ptep = (pte_t *)pudp;
			goto set_the_pte;
		}
		pmdp = pmd_alloc(&init_mm, pudp, ea);
		if (!pmdp)
			return -ENOMEM;
		if (map_page_size == PMD_SIZE) {
			ptep = (pte_t *)pudp;
			goto set_the_pte;
		}
		ptep = pte_alloc_kernel(pmdp, ea);
		if (!ptep)
			return -ENOMEM;
	} else {
		pgdp = pgd_offset_k(ea);
		if (pgd_none(*pgdp)) {
			pudp = early_alloc_pgtable(PUD_TABLE_SIZE);
			BUG_ON(pudp == NULL);
			pgd_populate(&init_mm, pgdp, pudp);
		}
		pudp = pud_offset(pgdp, ea);
		if (map_page_size == PUD_SIZE) {
			ptep = (pte_t *)pudp;
			goto set_the_pte;
		}
		if (pud_none(*pudp)) {
			pmdp = early_alloc_pgtable(PMD_TABLE_SIZE);
			BUG_ON(pmdp == NULL);
			pud_populate(&init_mm, pudp, pmdp);
		}
		pmdp = pmd_offset(pudp, ea);
		if (map_page_size == PMD_SIZE) {
			ptep = (pte_t *)pudp;
			goto set_the_pte;
		}
		if (!pmd_present(*pmdp)) {
			ptep = early_alloc_pgtable(PAGE_SIZE);
			BUG_ON(ptep == NULL);
			pmd_populate_kernel(&init_mm, pmdp, ptep);
		}
		ptep = pte_offset_kernel(pmdp, ea);
	}

set_the_pte:
	set_pte_at(&init_mm, ea, ptep, pfn_pte(pa >> PAGE_SHIFT, flags));
	smp_wmb();
	return 0;
}

static void __init radix_init_pgtable(void)
{
	int loop_count;
	u64 base, end, start_addr;
	unsigned long rts_field;
	struct memblock_region *reg;
	unsigned long linear_page_size;

	/* We don't support slb for radix */
	mmu_slb_size = 0;
	/*
	 * Create the linear mapping, using standard page size for now
	 */
	loop_count = 0;
	for_each_memblock(memory, reg) {

		start_addr = reg->base;

redo:
		if (loop_count < 1 && mmu_psize_defs[MMU_PAGE_1G].shift)
			linear_page_size = PUD_SIZE;
		else if (loop_count < 2 && mmu_psize_defs[MMU_PAGE_2M].shift)
			linear_page_size = PMD_SIZE;
		else
			linear_page_size = PAGE_SIZE;

		base = _ALIGN_UP(start_addr, linear_page_size);
		end = _ALIGN_DOWN(reg->base + reg->size, linear_page_size);

		pr_info("Mapping range 0x%lx - 0x%lx with 0x%lx\n",
			(unsigned long)base, (unsigned long)end,
			linear_page_size);

		while (base < end) {
			radix__map_kernel_page((unsigned long)__va(base),
					      base, PAGE_KERNEL_X,
					      linear_page_size);
			base += linear_page_size;
		}
		/*
		 * map the rest using lower page size
		 */
		if (end < reg->base + reg->size) {
			start_addr = end;
			loop_count++;
			goto redo;
		}
	}
	/*
	 * Allocate Partition table and process table for the
	 * host.
	 */
	BUILD_BUG_ON_MSG((PRTB_SIZE_SHIFT > 23), "Process table size too large.");
	process_tb = early_alloc_pgtable(1UL << PRTB_SIZE_SHIFT);
	/*
	 * Fill in the process table.
	 */
	rts_field = radix__get_tree_size();
	process_tb->prtb0 = cpu_to_be64(rts_field | __pa(init_mm.pgd) | RADIX_PGD_INDEX_SIZE);
	/*
	 * Fill in the partition table. We are suppose to use effective address
	 * of process table here. But our linear mapping also enable us to use
	 * physical address here.
	 */
	ppc_md.update_partition_table(__pa(process_tb) | (PRTB_SIZE_SHIFT - 12) | PATB_GR);
	pr_info("Process table %p and radix root for kernel: %p\n", process_tb, init_mm.pgd);
}

static void __init radix_init_partition_table(void)
{
	unsigned long rts_field;

	rts_field = radix__get_tree_size();

	BUILD_BUG_ON_MSG((PATB_SIZE_SHIFT > 24), "Partition table size too large.");
	partition_tb = early_alloc_pgtable(1UL << PATB_SIZE_SHIFT);
	partition_tb->patb0 = cpu_to_be64(rts_field | __pa(init_mm.pgd) |
					  RADIX_PGD_INDEX_SIZE | PATB_HR);
	printk("Partition table %p\n", partition_tb);

	memblock_set_current_limit(MEMBLOCK_ALLOC_ANYWHERE);
	/*
	 * update partition table control register,
	 * 64 K size.
	 */
	mtspr(SPRN_PTCR, __pa(partition_tb) | (PATB_SIZE_SHIFT - 12));
}

void __init radix_init_native(void)
{
	ppc_md.update_partition_table = native_update_partition_table;
}

static int __init get_idx_from_shift(unsigned int shift)
{
	int idx = -1;

	switch (shift) {
	case 0xc:
		idx = MMU_PAGE_4K;
		break;
	case 0x10:
		idx = MMU_PAGE_64K;
		break;
	case 0x15:
		idx = MMU_PAGE_2M;
		break;
	case 0x1e:
		idx = MMU_PAGE_1G;
		break;
	}
	return idx;
}

static int __init radix_dt_scan_page_sizes(unsigned long node,
					   const char *uname, int depth,
					   void *data)
{
	int size = 0;
	int shift, idx;
	unsigned int ap;
	const __be32 *prop;
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	prop = of_get_flat_dt_prop(node, "ibm,processor-radix-AP-encodings", &size);
	if (!prop)
		return 0;

	pr_info("Page sizes from device-tree:\n");
	for (; size >= 4; size -= 4, ++prop) {

		struct mmu_psize_def *def;

		/* top 3 bit is AP encoding */
		shift = be32_to_cpu(prop[0]) & ~(0xe << 28);
		ap = be32_to_cpu(prop[0]) >> 29;
		pr_info("Page size sift = %d AP=0x%x\n", shift, ap);

		idx = get_idx_from_shift(shift);
		if (idx < 0)
			continue;

		def = &mmu_psize_defs[idx];
		def->shift = shift;
		def->ap  = ap;
	}

	/* needed ? */
	cur_cpu_spec->mmu_features &= ~MMU_FTR_NO_SLBIE_B;
	return 1;
}

static void __init radix_init_page_sizes(void)
{
	int rc;

	/*
	 * Try to find the available page sizes in the device-tree
	 */
	rc = of_scan_flat_dt(radix_dt_scan_page_sizes, NULL);
	if (rc != 0)  /* Found */
		goto found;
	/*
	 * let's assume we have page 4k and 64k support
	 */
	mmu_psize_defs[MMU_PAGE_4K].shift = 12;
	mmu_psize_defs[MMU_PAGE_4K].ap = 0x0;

	mmu_psize_defs[MMU_PAGE_64K].shift = 16;
	mmu_psize_defs[MMU_PAGE_64K].ap = 0x5;
found:
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	if (mmu_psize_defs[MMU_PAGE_2M].shift) {
		/*
		 * map vmemmap using 2M if available
		 */
		mmu_vmemmap_psize = MMU_PAGE_2M;
	}
#endif /* CONFIG_SPARSEMEM_VMEMMAP */
	return;
}

void __init radix__early_init_mmu(void)
{
	unsigned long lpcr;

#ifdef CONFIG_PPC_64K_PAGES
	/* PAGE_SIZE mappings */
	mmu_virtual_psize = MMU_PAGE_64K;
#else
	mmu_virtual_psize = MMU_PAGE_4K;
#endif

#ifdef CONFIG_SPARSEMEM_VMEMMAP
	/* vmemmap mapping */
	mmu_vmemmap_psize = mmu_virtual_psize;
#endif
	/*
	 * initialize page table size
	 */
	__pte_index_size = RADIX_PTE_INDEX_SIZE;
	__pmd_index_size = RADIX_PMD_INDEX_SIZE;
	__pud_index_size = RADIX_PUD_INDEX_SIZE;
	__pgd_index_size = RADIX_PGD_INDEX_SIZE;
	__pmd_cache_index = RADIX_PMD_INDEX_SIZE;
	__pte_table_size = RADIX_PTE_TABLE_SIZE;
	__pmd_table_size = RADIX_PMD_TABLE_SIZE;
	__pud_table_size = RADIX_PUD_TABLE_SIZE;
	__pgd_table_size = RADIX_PGD_TABLE_SIZE;

	__pmd_val_bits = RADIX_PMD_VAL_BITS;
	__pud_val_bits = RADIX_PUD_VAL_BITS;
	__pgd_val_bits = RADIX_PGD_VAL_BITS;

	__kernel_virt_start = RADIX_KERN_VIRT_START;
	__kernel_virt_size = RADIX_KERN_VIRT_SIZE;
	__vmalloc_start = RADIX_VMALLOC_START;
	__vmalloc_end = RADIX_VMALLOC_END;
	vmemmap = (struct page *)RADIX_VMEMMAP_BASE;
	ioremap_bot = IOREMAP_BASE;

#ifdef CONFIG_PCI
	pci_io_base = ISA_IO_BASE;
#endif

	/*
	 * For now radix also use the same frag size
	 */
	__pte_frag_nr = H_PTE_FRAG_NR;
	__pte_frag_size_shift = H_PTE_FRAG_SIZE_SHIFT;

	radix_init_page_sizes();
	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		lpcr = mfspr(SPRN_LPCR);
		mtspr(SPRN_LPCR, lpcr | LPCR_UPRT);
		radix_init_partition_table();
	}

	radix_init_pgtable();
}

void radix__early_init_mmu_secondary(void)
{
	unsigned long lpcr;
	/*
	 * update partition table control register and UPRT
	 */
	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		lpcr = mfspr(SPRN_LPCR);
		mtspr(SPRN_LPCR, lpcr | LPCR_UPRT);

		mtspr(SPRN_PTCR,
		      __pa(partition_tb) | (PATB_SIZE_SHIFT - 12));
	}
}

void radix__setup_initial_memory_limit(phys_addr_t first_memblock_base,
				phys_addr_t first_memblock_size)
{
	/* We don't currently support the first MEMBLOCK not mapping 0
	 * physical on those processors
	 */
	BUG_ON(first_memblock_base != 0);
	/*
	 * We limit the allocation that depend on ppc64_rma_size
	 * to first_memblock_size. We also clamp it to 1GB to
	 * avoid some funky things such as RTAS bugs.
	 *
	 * On radix config we really don't have a limitation
	 * on real mode access. But keeping it as above works
	 * well enough.
	 */
	ppc64_rma_size = min_t(u64, first_memblock_size, 0x40000000);
	/*
	 * Finally limit subsequent allocations. We really don't want
	 * to limit the memblock allocations to rma_size. FIXME!! should
	 * we even limit at all ?
	 */
	memblock_set_current_limit(first_memblock_base + first_memblock_size);
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
int __meminit radix__vmemmap_create_mapping(unsigned long start,
				      unsigned long page_size,
				      unsigned long phys)
{
	/* Create a PTE encoding */
	unsigned long flags = _PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_KERNEL_RW;

	BUG_ON(radix__map_kernel_page(start, phys, __pgprot(flags), page_size));
	return 0;
}

#ifdef CONFIG_MEMORY_HOTPLUG
void radix__vmemmap_remove_mapping(unsigned long start, unsigned long page_size)
{
	/* FIXME!! intel does more. We should free page tables mapping vmemmap ? */
}
#endif
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

unsigned long radix__pmd_hugepage_update(struct mm_struct *mm, unsigned long addr,
				  pmd_t *pmdp, unsigned long clr,
				  unsigned long set)
{
	unsigned long old;

#ifdef CONFIG_DEBUG_VM
	WARN_ON(!radix__pmd_trans_huge(*pmdp));
	assert_spin_locked(&mm->page_table_lock);
#endif

	old = radix__pte_update(mm, addr, (pte_t *)pmdp, clr, set, 1);
	trace_hugepage_update(addr, old, clr, set);

	return old;
}

pmd_t radix__pmdp_collapse_flush(struct vm_area_struct *vma, unsigned long address,
			pmd_t *pmdp)

{
	pmd_t pmd;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	VM_BUG_ON(radix__pmd_trans_huge(*pmdp));
	/*
	 * khugepaged calls this for normal pmd
	 */
	pmd = *pmdp;
	pmd_clear(pmdp);
	/*FIXME!!  Verify whether we need this kick below */
	kick_all_cpus_sync();
	flush_tlb_range(vma, address, address + HPAGE_PMD_SIZE);
	return pmd;
}

/*
 * For us pgtable_t is pte_t *. Inorder to save the deposisted
 * page table, we consider the allocated page table as a list
 * head. On withdraw we need to make sure we zero out the used
 * list_head memory area.
 */
void radix__pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				 pgtable_t pgtable)
{
        struct list_head *lh = (struct list_head *) pgtable;

        assert_spin_locked(pmd_lockptr(mm, pmdp));

        /* FIFO */
        if (!pmd_huge_pte(mm, pmdp))
                INIT_LIST_HEAD(lh);
        else
                list_add(lh, (struct list_head *) pmd_huge_pte(mm, pmdp));
        pmd_huge_pte(mm, pmdp) = pgtable;
}

pgtable_t radix__pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp)
{
        pte_t *ptep;
        pgtable_t pgtable;
        struct list_head *lh;

        assert_spin_locked(pmd_lockptr(mm, pmdp));

        /* FIFO */
        pgtable = pmd_huge_pte(mm, pmdp);
        lh = (struct list_head *) pgtable;
        if (list_empty(lh))
                pmd_huge_pte(mm, pmdp) = NULL;
        else {
                pmd_huge_pte(mm, pmdp) = (pgtable_t) lh->next;
                list_del(lh);
        }
        ptep = (pte_t *) pgtable;
        *ptep = __pte(0);
        ptep++;
        *ptep = __pte(0);
        return pgtable;
}


pmd_t radix__pmdp_huge_get_and_clear(struct mm_struct *mm,
			       unsigned long addr, pmd_t *pmdp)
{
	pmd_t old_pmd;
	unsigned long old;

	old = radix__pmd_hugepage_update(mm, addr, pmdp, ~0UL, 0);
	old_pmd = __pmd(old);
	/*
	 * Serialize against find_linux_pte_or_hugepte which does lock-less
	 * lookup in page tables with local interrupts disabled. For huge pages
	 * it casts pmd_t to pte_t. Since format of pte_t is different from
	 * pmd_t we want to prevent transit from pmd pointing to page table
	 * to pmd pointing to huge page (and back) while interrupts are disabled.
	 * We clear pmd to possibly replace it with page table pointer in
	 * different code paths. So make sure we wait for the parallel
	 * find_linux_pte_or_hugepage to finish.
	 */
	kick_all_cpus_sync();
	return old_pmd;
}

int radix__has_transparent_hugepage(void)
{
	/* For radix 2M at PMD level means thp */
	if (mmu_psize_defs[MMU_PAGE_2M].shift == PMD_SHIFT)
		return 1;
	return 0;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

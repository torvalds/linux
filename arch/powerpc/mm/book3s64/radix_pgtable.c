// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Page table handling routines for radix page table.
 *
 * Copyright 2015-2016, Aneesh Kumar K.V, IBM Corporation.
 */

#define pr_fmt(fmt) "radix-mmu: " fmt

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/sched/mm.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/string_helpers.h>
#include <linux/memory.h>
#include <linux/kfence.h>

#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/mmu.h>
#include <asm/firmware.h>
#include <asm/powernv.h>
#include <asm/sections.h>
#include <asm/smp.h>
#include <asm/trace.h>
#include <asm/uaccess.h>
#include <asm/ultravisor.h>
#include <asm/set_memory.h>
#include <asm/kfence.h>

#include <trace/events/thp.h>

#include <mm/mmu_decl.h>

unsigned int mmu_base_pid;

static __ref void *early_alloc_pgtable(unsigned long size, int nid,
			unsigned long region_start, unsigned long region_end)
{
	phys_addr_t min_addr = MEMBLOCK_LOW_LIMIT;
	phys_addr_t max_addr = MEMBLOCK_ALLOC_ANYWHERE;
	void *ptr;

	if (region_start)
		min_addr = region_start;
	if (region_end)
		max_addr = region_end;

	ptr = memblock_alloc_try_nid(size, size, min_addr, max_addr, nid);

	if (!ptr)
		panic("%s: Failed to allocate %lu bytes align=0x%lx nid=%d from=%pa max_addr=%pa\n",
		      __func__, size, size, nid, &min_addr, &max_addr);

	return ptr;
}

/*
 * When allocating pud or pmd pointers, we allocate a complete page
 * of PAGE_SIZE rather than PUD_TABLE_SIZE or PMD_TABLE_SIZE. This
 * is to ensure that the page obtained from the memblock allocator
 * can be completely used as page table page and can be freed
 * correctly when the page table entries are removed.
 */
static int early_map_kernel_page(unsigned long ea, unsigned long pa,
			  pgprot_t flags,
			  unsigned int map_page_size,
			  int nid,
			  unsigned long region_start, unsigned long region_end)
{
	unsigned long pfn = pa >> PAGE_SHIFT;
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	pgdp = pgd_offset_k(ea);
	p4dp = p4d_offset(pgdp, ea);
	if (p4d_none(*p4dp)) {
		pudp = early_alloc_pgtable(PAGE_SIZE, nid,
					   region_start, region_end);
		p4d_populate(&init_mm, p4dp, pudp);
	}
	pudp = pud_offset(p4dp, ea);
	if (map_page_size == PUD_SIZE) {
		ptep = (pte_t *)pudp;
		goto set_the_pte;
	}
	if (pud_none(*pudp)) {
		pmdp = early_alloc_pgtable(PAGE_SIZE, nid, region_start,
					   region_end);
		pud_populate(&init_mm, pudp, pmdp);
	}
	pmdp = pmd_offset(pudp, ea);
	if (map_page_size == PMD_SIZE) {
		ptep = pmdp_ptep(pmdp);
		goto set_the_pte;
	}
	if (!pmd_present(*pmdp)) {
		ptep = early_alloc_pgtable(PAGE_SIZE, nid,
						region_start, region_end);
		pmd_populate_kernel(&init_mm, pmdp, ptep);
	}
	ptep = pte_offset_kernel(pmdp, ea);

set_the_pte:
	set_pte_at(&init_mm, ea, ptep, pfn_pte(pfn, flags));
	asm volatile("ptesync": : :"memory");
	return 0;
}

/*
 * nid, region_start, and region_end are hints to try to place the page
 * table memory in the same node or region.
 */
static int __map_kernel_page(unsigned long ea, unsigned long pa,
			  pgprot_t flags,
			  unsigned int map_page_size,
			  int nid,
			  unsigned long region_start, unsigned long region_end)
{
	unsigned long pfn = pa >> PAGE_SHIFT;
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	/*
	 * Make sure task size is correct as per the max adddr
	 */
	BUILD_BUG_ON(TASK_SIZE_USER64 > RADIX_PGTABLE_RANGE);

#ifdef CONFIG_PPC_64K_PAGES
	BUILD_BUG_ON(RADIX_KERN_MAP_SIZE != (1UL << MAX_EA_BITS_PER_CONTEXT));
#endif

	if (unlikely(!slab_is_available()))
		return early_map_kernel_page(ea, pa, flags, map_page_size,
						nid, region_start, region_end);

	/*
	 * Should make page table allocation functions be able to take a
	 * node, so we can place kernel page tables on the right nodes after
	 * boot.
	 */
	pgdp = pgd_offset_k(ea);
	p4dp = p4d_offset(pgdp, ea);
	pudp = pud_alloc(&init_mm, p4dp, ea);
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
		ptep = pmdp_ptep(pmdp);
		goto set_the_pte;
	}
	ptep = pte_alloc_kernel(pmdp, ea);
	if (!ptep)
		return -ENOMEM;

set_the_pte:
	set_pte_at(&init_mm, ea, ptep, pfn_pte(pfn, flags));
	asm volatile("ptesync": : :"memory");
	return 0;
}

int radix__map_kernel_page(unsigned long ea, unsigned long pa,
			  pgprot_t flags,
			  unsigned int map_page_size)
{
	return __map_kernel_page(ea, pa, flags, map_page_size, -1, 0, 0);
}

#ifdef CONFIG_STRICT_KERNEL_RWX
static void radix__change_memory_range(unsigned long start, unsigned long end,
				       unsigned long clear)
{
	unsigned long idx;
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	start = ALIGN_DOWN(start, PAGE_SIZE);
	end = PAGE_ALIGN(end); // aligns up

	pr_debug("Changing flags on range %lx-%lx removing 0x%lx\n",
		 start, end, clear);

	for (idx = start; idx < end; idx += PAGE_SIZE) {
		pgdp = pgd_offset_k(idx);
		p4dp = p4d_offset(pgdp, idx);
		pudp = pud_alloc(&init_mm, p4dp, idx);
		if (!pudp)
			continue;
		if (pud_leaf(*pudp)) {
			ptep = (pte_t *)pudp;
			goto update_the_pte;
		}
		pmdp = pmd_alloc(&init_mm, pudp, idx);
		if (!pmdp)
			continue;
		if (pmd_leaf(*pmdp)) {
			ptep = pmdp_ptep(pmdp);
			goto update_the_pte;
		}
		ptep = pte_alloc_kernel(pmdp, idx);
		if (!ptep)
			continue;
update_the_pte:
		radix__pte_update(&init_mm, idx, ptep, clear, 0, 0);
	}

	radix__flush_tlb_kernel_range(start, end);
}

void radix__mark_rodata_ro(void)
{
	unsigned long start, end;

	start = (unsigned long)_stext;
	end = (unsigned long)__end_rodata;

	radix__change_memory_range(start, end, _PAGE_WRITE);

	for (start = PAGE_OFFSET; start < (unsigned long)_stext; start += PAGE_SIZE) {
		end = start + PAGE_SIZE;
		if (overlaps_interrupt_vector_text(start, end))
			radix__change_memory_range(start, end, _PAGE_WRITE);
		else
			break;
	}
}

void radix__mark_initmem_nx(void)
{
	unsigned long start = (unsigned long)__init_begin;
	unsigned long end = (unsigned long)__init_end;

	radix__change_memory_range(start, end, _PAGE_EXEC);
}
#endif /* CONFIG_STRICT_KERNEL_RWX */

static inline void __meminit
print_mapping(unsigned long start, unsigned long end, unsigned long size, bool exec)
{
	char buf[10];

	if (end <= start)
		return;

	string_get_size(size, 1, STRING_UNITS_2, buf, sizeof(buf));

	pr_info("Mapped 0x%016lx-0x%016lx with %s pages%s\n", start, end, buf,
		exec ? " (exec)" : "");
}

static unsigned long next_boundary(unsigned long addr, unsigned long end)
{
#ifdef CONFIG_STRICT_KERNEL_RWX
	unsigned long stext_phys;

	stext_phys = __pa_symbol(_stext);

	// Relocatable kernel running at non-zero real address
	if (stext_phys != 0) {
		// The end of interrupts code at zero is a rodata boundary
		unsigned long end_intr = __pa_symbol(__end_interrupts) - stext_phys;
		if (addr < end_intr)
			return end_intr;

		// Start of relocated kernel text is a rodata boundary
		if (addr < stext_phys)
			return stext_phys;
	}

	if (addr < __pa_symbol(__srwx_boundary))
		return __pa_symbol(__srwx_boundary);
#endif
	return end;
}

static int __meminit create_physical_mapping(unsigned long start,
					     unsigned long end,
					     int nid, pgprot_t _prot,
					     unsigned long mapping_sz_limit)
{
	unsigned long vaddr, addr, mapping_size = 0;
	bool prev_exec, exec = false;
	pgprot_t prot;
	int psize;
	unsigned long max_mapping_size = memory_block_size;

	if (mapping_sz_limit < max_mapping_size)
		max_mapping_size = mapping_sz_limit;

	if (debug_pagealloc_enabled())
		max_mapping_size = PAGE_SIZE;

	start = ALIGN(start, PAGE_SIZE);
	end   = ALIGN_DOWN(end, PAGE_SIZE);
	for (addr = start; addr < end; addr += mapping_size) {
		unsigned long gap, previous_size;
		int rc;

		gap = next_boundary(addr, end) - addr;
		if (gap > max_mapping_size)
			gap = max_mapping_size;
		previous_size = mapping_size;
		prev_exec = exec;

		if (IS_ALIGNED(addr, PUD_SIZE) && gap >= PUD_SIZE &&
		    mmu_psize_defs[MMU_PAGE_1G].shift) {
			mapping_size = PUD_SIZE;
			psize = MMU_PAGE_1G;
		} else if (IS_ALIGNED(addr, PMD_SIZE) && gap >= PMD_SIZE &&
			   mmu_psize_defs[MMU_PAGE_2M].shift) {
			mapping_size = PMD_SIZE;
			psize = MMU_PAGE_2M;
		} else {
			mapping_size = PAGE_SIZE;
			psize = mmu_virtual_psize;
		}

		vaddr = (unsigned long)__va(addr);

		if (overlaps_kernel_text(vaddr, vaddr + mapping_size) ||
		    overlaps_interrupt_vector_text(vaddr, vaddr + mapping_size)) {
			prot = PAGE_KERNEL_X;
			exec = true;
		} else {
			prot = _prot;
			exec = false;
		}

		if (mapping_size != previous_size || exec != prev_exec) {
			print_mapping(start, addr, previous_size, prev_exec);
			start = addr;
		}

		rc = __map_kernel_page(vaddr, addr, prot, mapping_size, nid, start, end);
		if (rc)
			return rc;

		update_page_count(psize, 1);
	}

	print_mapping(start, addr, mapping_size, exec);
	return 0;
}

#ifdef CONFIG_KFENCE
static inline phys_addr_t alloc_kfence_pool(void)
{
	phys_addr_t kfence_pool;

	/*
	 * TODO: Support to enable KFENCE after bootup depends on the ability to
	 *       split page table mappings. As such support is not currently
	 *       implemented for radix pagetables, support enabling KFENCE
	 *       only at system startup for now.
	 *
	 *       After support for splitting mappings is available on radix,
	 *       alloc_kfence_pool() & map_kfence_pool() can be dropped and
	 *       mapping for __kfence_pool memory can be
	 *       split during arch_kfence_init_pool().
	 */
	if (!kfence_early_init)
		goto no_kfence;

	kfence_pool = memblock_phys_alloc(KFENCE_POOL_SIZE, PAGE_SIZE);
	if (!kfence_pool)
		goto no_kfence;

	memblock_mark_nomap(kfence_pool, KFENCE_POOL_SIZE);
	return kfence_pool;

no_kfence:
	disable_kfence();
	return 0;
}

static inline void map_kfence_pool(phys_addr_t kfence_pool)
{
	if (!kfence_pool)
		return;

	if (create_physical_mapping(kfence_pool, kfence_pool + KFENCE_POOL_SIZE,
				    -1, PAGE_KERNEL, PAGE_SIZE))
		goto err;

	memblock_clear_nomap(kfence_pool, KFENCE_POOL_SIZE);
	__kfence_pool = __va(kfence_pool);
	return;

err:
	memblock_phys_free(kfence_pool, KFENCE_POOL_SIZE);
	disable_kfence();
}
#else
static inline phys_addr_t alloc_kfence_pool(void) { return 0; }
static inline void map_kfence_pool(phys_addr_t kfence_pool) { }
#endif

static void __init radix_init_pgtable(void)
{
	phys_addr_t kfence_pool;
	unsigned long rts_field;
	phys_addr_t start, end;
	u64 i;

	/* We don't support slb for radix */
	slb_set_size(0);

	kfence_pool = alloc_kfence_pool();

	/*
	 * Create the linear mapping
	 */
	for_each_mem_range(i, &start, &end) {
		/*
		 * The memblock allocator  is up at this point, so the
		 * page tables will be allocated within the range. No
		 * need or a node (which we don't have yet).
		 */

		if (end >= RADIX_VMALLOC_START) {
			pr_warn("Outside the supported range\n");
			continue;
		}

		WARN_ON(create_physical_mapping(start, end,
						-1, PAGE_KERNEL, ~0UL));
	}

	map_kfence_pool(kfence_pool);

	if (!cpu_has_feature(CPU_FTR_HVMODE) &&
			cpu_has_feature(CPU_FTR_P9_RADIX_PREFETCH_BUG)) {
		/*
		 * Older versions of KVM on these machines prefer if the
		 * guest only uses the low 19 PID bits.
		 */
		mmu_pid_bits = 19;
	}
	mmu_base_pid = 1;

	/*
	 * Allocate Partition table and process table for the
	 * host.
	 */
	BUG_ON(PRTB_SIZE_SHIFT > 36);
	process_tb = early_alloc_pgtable(1UL << PRTB_SIZE_SHIFT, -1, 0, 0);
	/*
	 * Fill in the process table.
	 */
	rts_field = radix__get_tree_size();
	process_tb->prtb0 = cpu_to_be64(rts_field | __pa(init_mm.pgd) | RADIX_PGD_INDEX_SIZE);

	/*
	 * The init_mm context is given the first available (non-zero) PID,
	 * which is the "guard PID" and contains no page table. PIDR should
	 * never be set to zero because that duplicates the kernel address
	 * space at the 0x0... offset (quadrant 0)!
	 *
	 * An arbitrary PID that may later be allocated by the PID allocator
	 * for userspace processes must not be used either, because that
	 * would cause stale user mappings for that PID on CPUs outside of
	 * the TLB invalidation scheme (because it won't be in mm_cpumask).
	 *
	 * So permanently carve out one PID for the purpose of a guard PID.
	 */
	init_mm.context.id = mmu_base_pid;
	mmu_base_pid++;
}

static void __init radix_init_partition_table(void)
{
	unsigned long rts_field, dw0, dw1;

	mmu_partition_table_init();
	rts_field = radix__get_tree_size();
	dw0 = rts_field | __pa(init_mm.pgd) | RADIX_PGD_INDEX_SIZE | PATB_HR;
	dw1 = __pa(process_tb) | (PRTB_SIZE_SHIFT - 12) | PATB_GR;
	mmu_partition_table_set_entry(0, dw0, dw1, false);

	pr_info("Initializing Radix MMU\n");
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

	/* Grab page size encodings */
	prop = of_get_flat_dt_prop(node, "ibm,processor-radix-AP-encodings", &size);
	if (!prop)
		return 0;

	pr_info("Page sizes from device-tree:\n");
	for (; size >= 4; size -= 4, ++prop) {

		struct mmu_psize_def *def;

		/* top 3 bit is AP encoding */
		shift = be32_to_cpu(prop[0]) & ~(0xe << 28);
		ap = be32_to_cpu(prop[0]) >> 29;
		pr_info("Page size shift = %d AP=0x%x\n", shift, ap);

		idx = get_idx_from_shift(shift);
		if (idx < 0)
			continue;

		def = &mmu_psize_defs[idx];
		def->shift = shift;
		def->ap  = ap;
		def->h_rpt_pgsize = psize_to_rpti_pgsize(idx);
	}

	/* needed ? */
	cur_cpu_spec->mmu_features &= ~MMU_FTR_NO_SLBIE_B;
	return 1;
}

void __init radix__early_init_devtree(void)
{
	int rc;

	/*
	 * Try to find the available page sizes in the device-tree
	 */
	rc = of_scan_flat_dt(radix_dt_scan_page_sizes, NULL);
	if (!rc) {
		/*
		 * No page size details found in device tree.
		 * Let's assume we have page 4k and 64k support
		 */
		mmu_psize_defs[MMU_PAGE_4K].shift = 12;
		mmu_psize_defs[MMU_PAGE_4K].ap = 0x0;
		mmu_psize_defs[MMU_PAGE_4K].h_rpt_pgsize =
			psize_to_rpti_pgsize(MMU_PAGE_4K);

		mmu_psize_defs[MMU_PAGE_64K].shift = 16;
		mmu_psize_defs[MMU_PAGE_64K].ap = 0x5;
		mmu_psize_defs[MMU_PAGE_64K].h_rpt_pgsize =
			psize_to_rpti_pgsize(MMU_PAGE_64K);
	}
	return;
}

void __init radix__early_init_mmu(void)
{
	unsigned long lpcr;

#ifdef CONFIG_PPC_64S_HASH_MMU
#ifdef CONFIG_PPC_64K_PAGES
	/* PAGE_SIZE mappings */
	mmu_virtual_psize = MMU_PAGE_64K;
#else
	mmu_virtual_psize = MMU_PAGE_4K;
#endif
#endif
	/*
	 * initialize page table size
	 */
	__pte_index_size = RADIX_PTE_INDEX_SIZE;
	__pmd_index_size = RADIX_PMD_INDEX_SIZE;
	__pud_index_size = RADIX_PUD_INDEX_SIZE;
	__pgd_index_size = RADIX_PGD_INDEX_SIZE;
	__pud_cache_index = RADIX_PUD_INDEX_SIZE;
	__pte_table_size = RADIX_PTE_TABLE_SIZE;
	__pmd_table_size = RADIX_PMD_TABLE_SIZE;
	__pud_table_size = RADIX_PUD_TABLE_SIZE;
	__pgd_table_size = RADIX_PGD_TABLE_SIZE;

	__pmd_val_bits = RADIX_PMD_VAL_BITS;
	__pud_val_bits = RADIX_PUD_VAL_BITS;
	__pgd_val_bits = RADIX_PGD_VAL_BITS;

	__kernel_virt_start = RADIX_KERN_VIRT_START;
	__vmalloc_start = RADIX_VMALLOC_START;
	__vmalloc_end = RADIX_VMALLOC_END;
	__kernel_io_start = RADIX_KERN_IO_START;
	__kernel_io_end = RADIX_KERN_IO_END;
	vmemmap = (struct page *)RADIX_VMEMMAP_START;
	ioremap_bot = IOREMAP_BASE;

#ifdef CONFIG_PCI
	pci_io_base = ISA_IO_BASE;
#endif
	__pte_frag_nr = RADIX_PTE_FRAG_NR;
	__pte_frag_size_shift = RADIX_PTE_FRAG_SIZE_SHIFT;
	__pmd_frag_nr = RADIX_PMD_FRAG_NR;
	__pmd_frag_size_shift = RADIX_PMD_FRAG_SIZE_SHIFT;

	radix_init_pgtable();

	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		lpcr = mfspr(SPRN_LPCR);
		mtspr(SPRN_LPCR, lpcr | LPCR_UPRT | LPCR_HR);
		radix_init_partition_table();
	} else {
		radix_init_pseries();
	}

	memblock_set_current_limit(MEMBLOCK_ALLOC_ANYWHERE);

	/* Switch to the guard PID before turning on MMU */
	radix__switch_mmu_context(NULL, &init_mm);
	tlbiel_all();
}

void radix__early_init_mmu_secondary(void)
{
	unsigned long lpcr;
	/*
	 * update partition table control register and UPRT
	 */
	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		lpcr = mfspr(SPRN_LPCR);
		mtspr(SPRN_LPCR, lpcr | LPCR_UPRT | LPCR_HR);

		set_ptcr_when_no_uv(__pa(partition_tb) |
				    (PATB_SIZE_SHIFT - 12));
	}

	radix__switch_mmu_context(NULL, &init_mm);
	tlbiel_all();

	/* Make sure userspace can't change the AMR */
	mtspr(SPRN_UAMOR, 0);
}

/* Called during kexec sequence with MMU off */
notrace void radix__mmu_cleanup_all(void)
{
	unsigned long lpcr;

	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		lpcr = mfspr(SPRN_LPCR);
		mtspr(SPRN_LPCR, lpcr & ~LPCR_UPRT);
		set_ptcr_when_no_uv(0);
		powernv_set_nmmu_ptcr(0);
		radix__flush_tlb_all();
	}
}

#ifdef CONFIG_MEMORY_HOTPLUG
static void free_pte_table(pte_t *pte_start, pmd_t *pmd)
{
	pte_t *pte;
	int i;

	for (i = 0; i < PTRS_PER_PTE; i++) {
		pte = pte_start + i;
		if (!pte_none(*pte))
			return;
	}

	pte_free_kernel(&init_mm, pte_start);
	pmd_clear(pmd);
}

static void free_pmd_table(pmd_t *pmd_start, pud_t *pud)
{
	pmd_t *pmd;
	int i;

	for (i = 0; i < PTRS_PER_PMD; i++) {
		pmd = pmd_start + i;
		if (!pmd_none(*pmd))
			return;
	}

	pmd_free(&init_mm, pmd_start);
	pud_clear(pud);
}

static void free_pud_table(pud_t *pud_start, p4d_t *p4d)
{
	pud_t *pud;
	int i;

	for (i = 0; i < PTRS_PER_PUD; i++) {
		pud = pud_start + i;
		if (!pud_none(*pud))
			return;
	}

	pud_free(&init_mm, pud_start);
	p4d_clear(p4d);
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
static bool __meminit vmemmap_pmd_is_unused(unsigned long addr, unsigned long end)
{
	unsigned long start = ALIGN_DOWN(addr, PMD_SIZE);

	return !vmemmap_populated(start, PMD_SIZE);
}

static bool __meminit vmemmap_page_is_unused(unsigned long addr, unsigned long end)
{
	unsigned long start = ALIGN_DOWN(addr, PAGE_SIZE);

	return !vmemmap_populated(start, PAGE_SIZE);

}
#endif

static void __meminit free_vmemmap_pages(struct page *page,
					 struct vmem_altmap *altmap,
					 int order)
{
	unsigned int nr_pages = 1 << order;

	if (altmap) {
		unsigned long alt_start, alt_end;
		unsigned long base_pfn = page_to_pfn(page);

		/*
		 * with 2M vmemmap mmaping we can have things setup
		 * such that even though atlmap is specified we never
		 * used altmap.
		 */
		alt_start = altmap->base_pfn;
		alt_end = altmap->base_pfn + altmap->reserve + altmap->free;

		if (base_pfn >= alt_start && base_pfn < alt_end) {
			vmem_altmap_free(altmap, nr_pages);
			return;
		}
	}

	if (PageReserved(page)) {
		/* allocated from memblock */
		while (nr_pages--)
			free_reserved_page(page++);
	} else
		free_pages((unsigned long)page_address(page), order);
}

static void __meminit remove_pte_table(pte_t *pte_start, unsigned long addr,
				       unsigned long end, bool direct,
				       struct vmem_altmap *altmap)
{
	unsigned long next, pages = 0;
	pte_t *pte;

	pte = pte_start + pte_index(addr);
	for (; addr < end; addr = next, pte++) {
		next = (addr + PAGE_SIZE) & PAGE_MASK;
		if (next > end)
			next = end;

		if (!pte_present(*pte))
			continue;

		if (PAGE_ALIGNED(addr) && PAGE_ALIGNED(next)) {
			if (!direct)
				free_vmemmap_pages(pte_page(*pte), altmap, 0);
			pte_clear(&init_mm, addr, pte);
			pages++;
		}
#ifdef CONFIG_SPARSEMEM_VMEMMAP
		else if (!direct && vmemmap_page_is_unused(addr, next)) {
			free_vmemmap_pages(pte_page(*pte), altmap, 0);
			pte_clear(&init_mm, addr, pte);
		}
#endif
	}
	if (direct)
		update_page_count(mmu_virtual_psize, -pages);
}

static void __meminit remove_pmd_table(pmd_t *pmd_start, unsigned long addr,
				       unsigned long end, bool direct,
				       struct vmem_altmap *altmap)
{
	unsigned long next, pages = 0;
	pte_t *pte_base;
	pmd_t *pmd;

	pmd = pmd_start + pmd_index(addr);
	for (; addr < end; addr = next, pmd++) {
		next = pmd_addr_end(addr, end);

		if (!pmd_present(*pmd))
			continue;

		if (pmd_leaf(*pmd)) {
			if (IS_ALIGNED(addr, PMD_SIZE) &&
			    IS_ALIGNED(next, PMD_SIZE)) {
				if (!direct)
					free_vmemmap_pages(pmd_page(*pmd), altmap, get_order(PMD_SIZE));
				pte_clear(&init_mm, addr, (pte_t *)pmd);
				pages++;
			}
#ifdef CONFIG_SPARSEMEM_VMEMMAP
			else if (!direct && vmemmap_pmd_is_unused(addr, next)) {
				free_vmemmap_pages(pmd_page(*pmd), altmap, get_order(PMD_SIZE));
				pte_clear(&init_mm, addr, (pte_t *)pmd);
			}
#endif
			continue;
		}

		pte_base = (pte_t *)pmd_page_vaddr(*pmd);
		remove_pte_table(pte_base, addr, next, direct, altmap);
		free_pte_table(pte_base, pmd);
	}
	if (direct)
		update_page_count(MMU_PAGE_2M, -pages);
}

static void __meminit remove_pud_table(pud_t *pud_start, unsigned long addr,
				       unsigned long end, bool direct,
				       struct vmem_altmap *altmap)
{
	unsigned long next, pages = 0;
	pmd_t *pmd_base;
	pud_t *pud;

	pud = pud_start + pud_index(addr);
	for (; addr < end; addr = next, pud++) {
		next = pud_addr_end(addr, end);

		if (!pud_present(*pud))
			continue;

		if (pud_leaf(*pud)) {
			if (!IS_ALIGNED(addr, PUD_SIZE) ||
			    !IS_ALIGNED(next, PUD_SIZE)) {
				WARN_ONCE(1, "%s: unaligned range\n", __func__);
				continue;
			}
			pte_clear(&init_mm, addr, (pte_t *)pud);
			pages++;
			continue;
		}

		pmd_base = pud_pgtable(*pud);
		remove_pmd_table(pmd_base, addr, next, direct, altmap);
		free_pmd_table(pmd_base, pud);
	}
	if (direct)
		update_page_count(MMU_PAGE_1G, -pages);
}

static void __meminit
remove_pagetable(unsigned long start, unsigned long end, bool direct,
		 struct vmem_altmap *altmap)
{
	unsigned long addr, next;
	pud_t *pud_base;
	pgd_t *pgd;
	p4d_t *p4d;

	spin_lock(&init_mm.page_table_lock);

	for (addr = start; addr < end; addr = next) {
		next = pgd_addr_end(addr, end);

		pgd = pgd_offset_k(addr);
		p4d = p4d_offset(pgd, addr);
		if (!p4d_present(*p4d))
			continue;

		if (p4d_leaf(*p4d)) {
			if (!IS_ALIGNED(addr, P4D_SIZE) ||
			    !IS_ALIGNED(next, P4D_SIZE)) {
				WARN_ONCE(1, "%s: unaligned range\n", __func__);
				continue;
			}

			pte_clear(&init_mm, addr, (pte_t *)pgd);
			continue;
		}

		pud_base = p4d_pgtable(*p4d);
		remove_pud_table(pud_base, addr, next, direct, altmap);
		free_pud_table(pud_base, p4d);
	}

	spin_unlock(&init_mm.page_table_lock);
	radix__flush_tlb_kernel_range(start, end);
}

int __meminit radix__create_section_mapping(unsigned long start,
					    unsigned long end, int nid,
					    pgprot_t prot)
{
	if (end >= RADIX_VMALLOC_START) {
		pr_warn("Outside the supported range\n");
		return -1;
	}

	return create_physical_mapping(__pa(start), __pa(end),
				       nid, prot, ~0UL);
}

int __meminit radix__remove_section_mapping(unsigned long start, unsigned long end)
{
	remove_pagetable(start, end, true, NULL);
	return 0;
}
#endif /* CONFIG_MEMORY_HOTPLUG */

#ifdef CONFIG_SPARSEMEM_VMEMMAP
static int __map_kernel_page_nid(unsigned long ea, unsigned long pa,
				 pgprot_t flags, unsigned int map_page_size,
				 int nid)
{
	return __map_kernel_page(ea, pa, flags, map_page_size, nid, 0, 0);
}

int __meminit radix__vmemmap_create_mapping(unsigned long start,
				      unsigned long page_size,
				      unsigned long phys)
{
	/* Create a PTE encoding */
	int nid = early_pfn_to_nid(phys >> PAGE_SHIFT);
	int ret;

	if ((start + page_size) >= RADIX_VMEMMAP_END) {
		pr_warn("Outside the supported range\n");
		return -1;
	}

	ret = __map_kernel_page_nid(start, phys, PAGE_KERNEL, page_size, nid);
	BUG_ON(ret);

	return 0;
}


bool vmemmap_can_optimize(struct vmem_altmap *altmap, struct dev_pagemap *pgmap)
{
	if (radix_enabled())
		return __vmemmap_can_optimize(altmap, pgmap);

	return false;
}

int __meminit vmemmap_check_pmd(pmd_t *pmdp, int node,
				unsigned long addr, unsigned long next)
{
	int large = pmd_leaf(*pmdp);

	if (large)
		vmemmap_verify(pmdp_ptep(pmdp), node, addr, next);

	return large;
}

void __meminit vmemmap_set_pmd(pmd_t *pmdp, void *p, int node,
			       unsigned long addr, unsigned long next)
{
	pte_t entry;
	pte_t *ptep = pmdp_ptep(pmdp);

	VM_BUG_ON(!IS_ALIGNED(addr, PMD_SIZE));
	entry = pfn_pte(__pa(p) >> PAGE_SHIFT, PAGE_KERNEL);
	set_pte_at(&init_mm, addr, ptep, entry);
	asm volatile("ptesync": : :"memory");

	vmemmap_verify(ptep, node, addr, next);
}

static pte_t * __meminit radix__vmemmap_pte_populate(pmd_t *pmdp, unsigned long addr,
						     int node,
						     struct vmem_altmap *altmap,
						     struct page *reuse)
{
	pte_t *pte = pte_offset_kernel(pmdp, addr);

	if (pte_none(*pte)) {
		pte_t entry;
		void *p;

		if (!reuse) {
			/*
			 * make sure we don't create altmap mappings
			 * covering things outside the device.
			 */
			if (altmap && altmap_cross_boundary(altmap, addr, PAGE_SIZE))
				altmap = NULL;

			p = vmemmap_alloc_block_buf(PAGE_SIZE, node, altmap);
			if (!p && altmap)
				p = vmemmap_alloc_block_buf(PAGE_SIZE, node, NULL);
			if (!p)
				return NULL;
			pr_debug("PAGE_SIZE vmemmap mapping\n");
		} else {
			/*
			 * When a PTE/PMD entry is freed from the init_mm
			 * there's a free_pages() call to this page allocated
			 * above. Thus this get_page() is paired with the
			 * put_page_testzero() on the freeing path.
			 * This can only called by certain ZONE_DEVICE path,
			 * and through vmemmap_populate_compound_pages() when
			 * slab is available.
			 */
			get_page(reuse);
			p = page_to_virt(reuse);
			pr_debug("Tail page reuse vmemmap mapping\n");
		}

		VM_BUG_ON(!PAGE_ALIGNED(addr));
		entry = pfn_pte(__pa(p) >> PAGE_SHIFT, PAGE_KERNEL);
		set_pte_at(&init_mm, addr, pte, entry);
		asm volatile("ptesync": : :"memory");
	}
	return pte;
}

static inline pud_t *vmemmap_pud_alloc(p4d_t *p4dp, int node,
				       unsigned long address)
{
	pud_t *pud;

	/* All early vmemmap mapping to keep simple do it at PAGE_SIZE */
	if (unlikely(p4d_none(*p4dp))) {
		if (unlikely(!slab_is_available())) {
			pud = early_alloc_pgtable(PAGE_SIZE, node, 0, 0);
			p4d_populate(&init_mm, p4dp, pud);
			/* go to the pud_offset */
		} else
			return pud_alloc(&init_mm, p4dp, address);
	}
	return pud_offset(p4dp, address);
}

static inline pmd_t *vmemmap_pmd_alloc(pud_t *pudp, int node,
				       unsigned long address)
{
	pmd_t *pmd;

	/* All early vmemmap mapping to keep simple do it at PAGE_SIZE */
	if (unlikely(pud_none(*pudp))) {
		if (unlikely(!slab_is_available())) {
			pmd = early_alloc_pgtable(PAGE_SIZE, node, 0, 0);
			pud_populate(&init_mm, pudp, pmd);
		} else
			return pmd_alloc(&init_mm, pudp, address);
	}
	return pmd_offset(pudp, address);
}

static inline pte_t *vmemmap_pte_alloc(pmd_t *pmdp, int node,
				       unsigned long address)
{
	pte_t *pte;

	/* All early vmemmap mapping to keep simple do it at PAGE_SIZE */
	if (unlikely(pmd_none(*pmdp))) {
		if (unlikely(!slab_is_available())) {
			pte = early_alloc_pgtable(PAGE_SIZE, node, 0, 0);
			pmd_populate(&init_mm, pmdp, pte);
		} else
			return pte_alloc_kernel(pmdp, address);
	}
	return pte_offset_kernel(pmdp, address);
}



int __meminit radix__vmemmap_populate(unsigned long start, unsigned long end, int node,
				      struct vmem_altmap *altmap)
{
	unsigned long addr;
	unsigned long next;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	for (addr = start; addr < end; addr = next) {
		next = pmd_addr_end(addr, end);

		pgd = pgd_offset_k(addr);
		p4d = p4d_offset(pgd, addr);
		pud = vmemmap_pud_alloc(p4d, node, addr);
		if (!pud)
			return -ENOMEM;
		pmd = vmemmap_pmd_alloc(pud, node, addr);
		if (!pmd)
			return -ENOMEM;

		if (pmd_none(READ_ONCE(*pmd))) {
			void *p;

			/*
			 * keep it simple by checking addr PMD_SIZE alignment
			 * and verifying the device boundary condition.
			 * For us to use a pmd mapping, both addr and pfn should
			 * be aligned. We skip if addr is not aligned and for
			 * pfn we hope we have extra area in the altmap that
			 * can help to find an aligned block. This can result
			 * in altmap block allocation failures, in which case
			 * we fallback to RAM for vmemmap allocation.
			 */
			if (altmap && (!IS_ALIGNED(addr, PMD_SIZE) ||
				       altmap_cross_boundary(altmap, addr, PMD_SIZE))) {
				/*
				 * make sure we don't create altmap mappings
				 * covering things outside the device.
				 */
				goto base_mapping;
			}

			p = vmemmap_alloc_block_buf(PMD_SIZE, node, altmap);
			if (p) {
				vmemmap_set_pmd(pmd, p, node, addr, next);
				pr_debug("PMD_SIZE vmemmap mapping\n");
				continue;
			} else if (altmap) {
				/*
				 * A vmemmap block allocation can fail due to
				 * alignment requirements and we trying to align
				 * things aggressively there by running out of
				 * space. Try base mapping on failure.
				 */
				goto base_mapping;
			}
		} else if (vmemmap_check_pmd(pmd, node, addr, next)) {
			/*
			 * If a huge mapping exist due to early call to
			 * vmemmap_populate, let's try to use that.
			 */
			continue;
		}
base_mapping:
		/*
		 * Not able allocate higher order memory to back memmap
		 * or we found a pointer to pte page. Allocate base page
		 * size vmemmap
		 */
		pte = vmemmap_pte_alloc(pmd, node, addr);
		if (!pte)
			return -ENOMEM;

		pte = radix__vmemmap_pte_populate(pmd, addr, node, altmap, NULL);
		if (!pte)
			return -ENOMEM;

		vmemmap_verify(pte, node, addr, addr + PAGE_SIZE);
		next = addr + PAGE_SIZE;
	}
	return 0;
}

static pte_t * __meminit radix__vmemmap_populate_address(unsigned long addr, int node,
							 struct vmem_altmap *altmap,
							 struct page *reuse)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset_k(addr);
	p4d = p4d_offset(pgd, addr);
	pud = vmemmap_pud_alloc(p4d, node, addr);
	if (!pud)
		return NULL;
	pmd = vmemmap_pmd_alloc(pud, node, addr);
	if (!pmd)
		return NULL;
	if (pmd_leaf(*pmd))
		/*
		 * The second page is mapped as a hugepage due to a nearby request.
		 * Force our mapping to page size without deduplication
		 */
		return NULL;
	pte = vmemmap_pte_alloc(pmd, node, addr);
	if (!pte)
		return NULL;
	radix__vmemmap_pte_populate(pmd, addr, node, NULL, NULL);
	vmemmap_verify(pte, node, addr, addr + PAGE_SIZE);

	return pte;
}

static pte_t * __meminit vmemmap_compound_tail_page(unsigned long addr,
						    unsigned long pfn_offset, int node)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long map_addr;

	/* the second vmemmap page which we use for duplication */
	map_addr = addr - pfn_offset * sizeof(struct page) + PAGE_SIZE;
	pgd = pgd_offset_k(map_addr);
	p4d = p4d_offset(pgd, map_addr);
	pud = vmemmap_pud_alloc(p4d, node, map_addr);
	if (!pud)
		return NULL;
	pmd = vmemmap_pmd_alloc(pud, node, map_addr);
	if (!pmd)
		return NULL;
	if (pmd_leaf(*pmd))
		/*
		 * The second page is mapped as a hugepage due to a nearby request.
		 * Force our mapping to page size without deduplication
		 */
		return NULL;
	pte = vmemmap_pte_alloc(pmd, node, map_addr);
	if (!pte)
		return NULL;
	/*
	 * Check if there exist a mapping to the left
	 */
	if (pte_none(*pte)) {
		/*
		 * Populate the head page vmemmap page.
		 * It can fall in different pmd, hence
		 * vmemmap_populate_address()
		 */
		pte = radix__vmemmap_populate_address(map_addr - PAGE_SIZE, node, NULL, NULL);
		if (!pte)
			return NULL;
		/*
		 * Populate the tail pages vmemmap page
		 */
		pte = radix__vmemmap_pte_populate(pmd, map_addr, node, NULL, NULL);
		if (!pte)
			return NULL;
		vmemmap_verify(pte, node, map_addr, map_addr + PAGE_SIZE);
		return pte;
	}
	return pte;
}

int __meminit vmemmap_populate_compound_pages(unsigned long start_pfn,
					      unsigned long start,
					      unsigned long end, int node,
					      struct dev_pagemap *pgmap)
{
	/*
	 * we want to map things as base page size mapping so that
	 * we can save space in vmemmap. We could have huge mapping
	 * covering out both edges.
	 */
	unsigned long addr;
	unsigned long addr_pfn = start_pfn;
	unsigned long next;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	for (addr = start; addr < end; addr = next) {

		pgd = pgd_offset_k(addr);
		p4d = p4d_offset(pgd, addr);
		pud = vmemmap_pud_alloc(p4d, node, addr);
		if (!pud)
			return -ENOMEM;
		pmd = vmemmap_pmd_alloc(pud, node, addr);
		if (!pmd)
			return -ENOMEM;

		if (pmd_leaf(READ_ONCE(*pmd))) {
			/* existing huge mapping. Skip the range */
			addr_pfn += (PMD_SIZE >> PAGE_SHIFT);
			next = pmd_addr_end(addr, end);
			continue;
		}
		pte = vmemmap_pte_alloc(pmd, node, addr);
		if (!pte)
			return -ENOMEM;
		if (!pte_none(*pte)) {
			/*
			 * This could be because we already have a compound
			 * page whose VMEMMAP_RESERVE_NR pages were mapped and
			 * this request fall in those pages.
			 */
			addr_pfn += 1;
			next = addr + PAGE_SIZE;
			continue;
		} else {
			unsigned long nr_pages = pgmap_vmemmap_nr(pgmap);
			unsigned long pfn_offset = addr_pfn - ALIGN_DOWN(addr_pfn, nr_pages);
			pte_t *tail_page_pte;

			/*
			 * if the address is aligned to huge page size it is the
			 * head mapping.
			 */
			if (pfn_offset == 0) {
				/* Populate the head page vmemmap page */
				pte = radix__vmemmap_pte_populate(pmd, addr, node, NULL, NULL);
				if (!pte)
					return -ENOMEM;
				vmemmap_verify(pte, node, addr, addr + PAGE_SIZE);

				/*
				 * Populate the tail pages vmemmap page
				 * It can fall in different pmd, hence
				 * vmemmap_populate_address()
				 */
				pte = radix__vmemmap_populate_address(addr + PAGE_SIZE, node, NULL, NULL);
				if (!pte)
					return -ENOMEM;

				addr_pfn += 2;
				next = addr + 2 * PAGE_SIZE;
				continue;
			}
			/*
			 * get the 2nd mapping details
			 * Also create it if that doesn't exist
			 */
			tail_page_pte = vmemmap_compound_tail_page(addr, pfn_offset, node);
			if (!tail_page_pte) {

				pte = radix__vmemmap_pte_populate(pmd, addr, node, NULL, NULL);
				if (!pte)
					return -ENOMEM;
				vmemmap_verify(pte, node, addr, addr + PAGE_SIZE);

				addr_pfn += 1;
				next = addr + PAGE_SIZE;
				continue;
			}

			pte = radix__vmemmap_pte_populate(pmd, addr, node, NULL, pte_page(*tail_page_pte));
			if (!pte)
				return -ENOMEM;
			vmemmap_verify(pte, node, addr, addr + PAGE_SIZE);

			addr_pfn += 1;
			next = addr + PAGE_SIZE;
			continue;
		}
	}
	return 0;
}


#ifdef CONFIG_MEMORY_HOTPLUG
void __meminit radix__vmemmap_remove_mapping(unsigned long start, unsigned long page_size)
{
	remove_pagetable(start, start + page_size, true, NULL);
}

void __ref radix__vmemmap_free(unsigned long start, unsigned long end,
			       struct vmem_altmap *altmap)
{
	remove_pagetable(start, end, false, altmap);
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
	WARN_ON(!radix__pmd_trans_huge(*pmdp) && !pmd_devmap(*pmdp));
	assert_spin_locked(pmd_lockptr(mm, pmdp));
#endif

	old = radix__pte_update(mm, addr, pmdp_ptep(pmdp), clr, set, 1);
	trace_hugepage_update_pmd(addr, old, clr, set);

	return old;
}

unsigned long radix__pud_hugepage_update(struct mm_struct *mm, unsigned long addr,
					 pud_t *pudp, unsigned long clr,
					 unsigned long set)
{
	unsigned long old;

#ifdef CONFIG_DEBUG_VM
	WARN_ON(!pud_devmap(*pudp));
	assert_spin_locked(pud_lockptr(mm, pudp));
#endif

	old = radix__pte_update(mm, addr, pudp_ptep(pudp), clr, set, 1);
	trace_hugepage_update_pud(addr, old, clr, set);

	return old;
}

pmd_t radix__pmdp_collapse_flush(struct vm_area_struct *vma, unsigned long address,
			pmd_t *pmdp)

{
	pmd_t pmd;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	VM_BUG_ON(radix__pmd_trans_huge(*pmdp));
	VM_BUG_ON(pmd_devmap(*pmdp));
	/*
	 * khugepaged calls this for normal pmd
	 */
	pmd = *pmdp;
	pmd_clear(pmdp);

	radix__flush_tlb_collapsed_pmd(vma->vm_mm, address);

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
	return old_pmd;
}

pud_t radix__pudp_huge_get_and_clear(struct mm_struct *mm,
				     unsigned long addr, pud_t *pudp)
{
	pud_t old_pud;
	unsigned long old;

	old = radix__pud_hugepage_update(mm, addr, pudp, ~0UL, 0);
	old_pud = __pud(old);
	return old_pud;
}

#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

void radix__ptep_set_access_flags(struct vm_area_struct *vma, pte_t *ptep,
				  pte_t entry, unsigned long address, int psize)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long set = pte_val(entry) & (_PAGE_DIRTY | _PAGE_SOFT_DIRTY |
					      _PAGE_ACCESSED | _PAGE_RW | _PAGE_EXEC);

	unsigned long change = pte_val(entry) ^ pte_val(*ptep);
	/*
	 * On POWER9, the NMMU is not able to relax PTE access permissions
	 * for a translation with a TLB. The PTE must be invalidated, TLB
	 * flushed before the new PTE is installed.
	 *
	 * This only needs to be done for radix, because hash translation does
	 * flush when updating the linux pte (and we don't support NMMU
	 * accelerators on HPT on POWER9 anyway XXX: do we?).
	 *
	 * POWER10 (and P9P) NMMU does behave as per ISA.
	 */
	if (!cpu_has_feature(CPU_FTR_ARCH_31) && (change & _PAGE_RW) &&
	    atomic_read(&mm->context.copros) > 0) {
		unsigned long old_pte, new_pte;

		old_pte = __radix_pte_update(ptep, _PAGE_PRESENT, _PAGE_INVALID);
		new_pte = old_pte | set;
		radix__flush_tlb_page_psize(mm, address, psize);
		__radix_pte_update(ptep, _PAGE_INVALID, new_pte);
	} else {
		__radix_pte_update(ptep, 0, set);
		/*
		 * Book3S does not require a TLB flush when relaxing access
		 * restrictions when the address space (modulo the POWER9 nest
		 * MMU issue above) because the MMU will reload the PTE after
		 * taking an access fault, as defined by the architecture. See
		 * "Setting a Reference or Change Bit or Upgrading Access
		 *  Authority (PTE Subject to Atomic Hardware Updates)" in
		 *  Power ISA Version 3.1B.
		 */
	}
	/* See ptesync comment in radix__set_pte_at */
}

void radix__ptep_modify_prot_commit(struct vm_area_struct *vma,
				    unsigned long addr, pte_t *ptep,
				    pte_t old_pte, pte_t pte)
{
	struct mm_struct *mm = vma->vm_mm;

	/*
	 * POWER9 NMMU must flush the TLB after clearing the PTE before
	 * installing a PTE with more relaxed access permissions, see
	 * radix__ptep_set_access_flags.
	 */
	if (!cpu_has_feature(CPU_FTR_ARCH_31) &&
	    is_pte_rw_upgrade(pte_val(old_pte), pte_val(pte)) &&
	    (atomic_read(&mm->context.copros) > 0))
		radix__flush_tlb_page(vma, addr);

	set_pte_at(mm, addr, ptep, pte);
}

int pud_set_huge(pud_t *pud, phys_addr_t addr, pgprot_t prot)
{
	pte_t *ptep = (pte_t *)pud;
	pte_t new_pud = pfn_pte(__phys_to_pfn(addr), prot);

	if (!radix_enabled())
		return 0;

	set_pte_at(&init_mm, 0 /* radix unused */, ptep, new_pud);

	return 1;
}

int pud_clear_huge(pud_t *pud)
{
	if (pud_leaf(*pud)) {
		pud_clear(pud);
		return 1;
	}

	return 0;
}

int pud_free_pmd_page(pud_t *pud, unsigned long addr)
{
	pmd_t *pmd;
	int i;

	pmd = pud_pgtable(*pud);
	pud_clear(pud);

	flush_tlb_kernel_range(addr, addr + PUD_SIZE);

	for (i = 0; i < PTRS_PER_PMD; i++) {
		if (!pmd_none(pmd[i])) {
			pte_t *pte;
			pte = (pte_t *)pmd_page_vaddr(pmd[i]);

			pte_free_kernel(&init_mm, pte);
		}
	}

	pmd_free(&init_mm, pmd);

	return 1;
}

int pmd_set_huge(pmd_t *pmd, phys_addr_t addr, pgprot_t prot)
{
	pte_t *ptep = (pte_t *)pmd;
	pte_t new_pmd = pfn_pte(__phys_to_pfn(addr), prot);

	if (!radix_enabled())
		return 0;

	set_pte_at(&init_mm, 0 /* radix unused */, ptep, new_pmd);

	return 1;
}

int pmd_clear_huge(pmd_t *pmd)
{
	if (pmd_leaf(*pmd)) {
		pmd_clear(pmd);
		return 1;
	}

	return 0;
}

int pmd_free_pte_page(pmd_t *pmd, unsigned long addr)
{
	pte_t *pte;

	pte = (pte_t *)pmd_page_vaddr(*pmd);
	pmd_clear(pmd);

	flush_tlb_kernel_range(addr, addr + PMD_SIZE);

	pte_free_kernel(&init_mm, pte);

	return 1;
}

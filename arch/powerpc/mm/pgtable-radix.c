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

#define pr_fmt(fmt) "radix-mmu: " fmt

#include <linux/kernel.h>
#include <linux/sched/mm.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/mm.h>
#include <linux/string_helpers.h>
#include <linux/stop_machine.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/mmu.h>
#include <asm/firmware.h>
#include <asm/powernv.h>
#include <asm/sections.h>
#include <asm/trace.h>

#include <trace/events/thp.h>

unsigned int mmu_pid_bits;
unsigned int mmu_base_pid;

static int native_register_process_table(unsigned long base, unsigned long pg_sz,
					 unsigned long table_size)
{
	unsigned long patb0, patb1;

	patb0 = be64_to_cpu(partition_tb[0].patb0);
	patb1 = base | table_size | PATB_GR;

	mmu_partition_table_set_entry(0, patb0, patb1);

	return 0;
}

static __ref void *early_alloc_pgtable(unsigned long size, int nid,
			unsigned long region_start, unsigned long region_end)
{
	unsigned long pa = 0;
	void *pt;

	if (region_start || region_end) /* has region hint */
		pa = memblock_alloc_range(size, size, region_start, region_end,
						MEMBLOCK_NONE);
	else if (nid != -1) /* has node hint */
		pa = memblock_alloc_base_nid(size, size,
						MEMBLOCK_ALLOC_ANYWHERE,
						nid, MEMBLOCK_NONE);

	if (!pa)
		pa = memblock_alloc_base(size, size, MEMBLOCK_ALLOC_ANYWHERE);

	BUG_ON(!pa);

	pt = __va(pa);
	memset(pt, 0, size);

	return pt;
}

static int early_map_kernel_page(unsigned long ea, unsigned long pa,
			  pgprot_t flags,
			  unsigned int map_page_size,
			  int nid,
			  unsigned long region_start, unsigned long region_end)
{
	unsigned long pfn = pa >> PAGE_SHIFT;
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	pgdp = pgd_offset_k(ea);
	if (pgd_none(*pgdp)) {
		pudp = early_alloc_pgtable(PUD_TABLE_SIZE, nid,
						region_start, region_end);
		pgd_populate(&init_mm, pgdp, pudp);
	}
	pudp = pud_offset(pgdp, ea);
	if (map_page_size == PUD_SIZE) {
		ptep = (pte_t *)pudp;
		goto set_the_pte;
	}
	if (pud_none(*pudp)) {
		pmdp = early_alloc_pgtable(PMD_TABLE_SIZE, nid,
						region_start, region_end);
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
	smp_wmb();
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
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	/*
	 * Make sure task size is correct as per the max adddr
	 */
	BUILD_BUG_ON(TASK_SIZE_USER64 > RADIX_PGTABLE_RANGE);

	if (unlikely(!slab_is_available()))
		return early_map_kernel_page(ea, pa, flags, map_page_size,
						nid, region_start, region_end);

	/*
	 * Should make page table allocation functions be able to take a
	 * node, so we can place kernel page tables on the right nodes after
	 * boot.
	 */
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
		ptep = pmdp_ptep(pmdp);
		goto set_the_pte;
	}
	ptep = pte_alloc_kernel(pmdp, ea);
	if (!ptep)
		return -ENOMEM;

set_the_pte:
	set_pte_at(&init_mm, ea, ptep, pfn_pte(pfn, flags));
	smp_wmb();
	return 0;
}

int radix__map_kernel_page(unsigned long ea, unsigned long pa,
			  pgprot_t flags,
			  unsigned int map_page_size)
{
	return __map_kernel_page(ea, pa, flags, map_page_size, -1, 0, 0);
}

#ifdef CONFIG_STRICT_KERNEL_RWX
void radix__change_memory_range(unsigned long start, unsigned long end,
				unsigned long clear)
{
	unsigned long idx;
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	start = ALIGN_DOWN(start, PAGE_SIZE);
	end = PAGE_ALIGN(end); // aligns up

	pr_debug("Changing flags on range %lx-%lx removing 0x%lx\n",
		 start, end, clear);

	for (idx = start; idx < end; idx += PAGE_SIZE) {
		pgdp = pgd_offset_k(idx);
		pudp = pud_alloc(&init_mm, pgdp, idx);
		if (!pudp)
			continue;
		if (pud_huge(*pudp)) {
			ptep = (pte_t *)pudp;
			goto update_the_pte;
		}
		pmdp = pmd_alloc(&init_mm, pudp, idx);
		if (!pmdp)
			continue;
		if (pmd_huge(*pmdp)) {
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
	end = (unsigned long)__init_begin;

	radix__change_memory_range(start, end, _PAGE_WRITE);
}

void radix__mark_initmem_nx(void)
{
	unsigned long start = (unsigned long)__init_begin;
	unsigned long end = (unsigned long)__init_end;

	radix__change_memory_range(start, end, _PAGE_EXEC);
}
#endif /* CONFIG_STRICT_KERNEL_RWX */

static inline void __meminit print_mapping(unsigned long start,
					   unsigned long end,
					   unsigned long size)
{
	char buf[10];

	if (end <= start)
		return;

	string_get_size(size, 1, STRING_UNITS_2, buf, sizeof(buf));

	pr_info("Mapped 0x%016lx-0x%016lx with %s pages\n", start, end, buf);
}

static int __meminit create_physical_mapping(unsigned long start,
					     unsigned long end,
					     int nid)
{
	unsigned long vaddr, addr, mapping_size = 0;
	pgprot_t prot;
	unsigned long max_mapping_size;
#ifdef CONFIG_STRICT_KERNEL_RWX
	int split_text_mapping = 1;
#else
	int split_text_mapping = 0;
#endif
	int psize;

	start = _ALIGN_UP(start, PAGE_SIZE);
	for (addr = start; addr < end; addr += mapping_size) {
		unsigned long gap, previous_size;
		int rc;

		gap = end - addr;
		previous_size = mapping_size;
		max_mapping_size = PUD_SIZE;

retry:
		if (IS_ALIGNED(addr, PUD_SIZE) && gap >= PUD_SIZE &&
		    mmu_psize_defs[MMU_PAGE_1G].shift &&
		    PUD_SIZE <= max_mapping_size) {
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

		if (split_text_mapping && (mapping_size == PUD_SIZE) &&
			(addr <= __pa_symbol(__init_begin)) &&
			(addr + mapping_size) >= __pa_symbol(_stext)) {
			max_mapping_size = PMD_SIZE;
			goto retry;
		}

		if (split_text_mapping && (mapping_size == PMD_SIZE) &&
		    (addr <= __pa_symbol(__init_begin)) &&
		    (addr + mapping_size) >= __pa_symbol(_stext)) {
			mapping_size = PAGE_SIZE;
			psize = mmu_virtual_psize;
		}

		if (mapping_size != previous_size) {
			print_mapping(start, addr, previous_size);
			start = addr;
		}

		vaddr = (unsigned long)__va(addr);

		if (overlaps_kernel_text(vaddr, vaddr + mapping_size) ||
		    overlaps_interrupt_vector_text(vaddr, vaddr + mapping_size))
			prot = PAGE_KERNEL_X;
		else
			prot = PAGE_KERNEL;

		rc = __map_kernel_page(vaddr, addr, prot, mapping_size, nid, start, end);
		if (rc)
			return rc;

		update_page_count(psize, 1);
	}

	print_mapping(start, addr, mapping_size);
	return 0;
}

void __init radix_init_pgtable(void)
{
	unsigned long rts_field;
	struct memblock_region *reg;

	/* We don't support slb for radix */
	mmu_slb_size = 0;
	/*
	 * Create the linear mapping, using standard page size for now
	 */
	for_each_memblock(memory, reg) {
		/*
		 * The memblock allocator  is up at this point, so the
		 * page tables will be allocated within the range. No
		 * need or a node (which we don't have yet).
		 */
		WARN_ON(create_physical_mapping(reg->base,
						reg->base + reg->size,
						-1));
	}

	/* Find out how many PID bits are supported */
	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		if (!mmu_pid_bits)
			mmu_pid_bits = 20;
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
		/*
		 * When KVM is possible, we only use the top half of the
		 * PID space to avoid collisions between host and guest PIDs
		 * which can cause problems due to prefetch when exiting the
		 * guest with AIL=3
		 */
		mmu_base_pid = 1 << (mmu_pid_bits - 1);
#else
		mmu_base_pid = 1;
#endif
	} else {
		/* The guest uses the bottom half of the PID space */
		if (!mmu_pid_bits)
			mmu_pid_bits = 19;
		mmu_base_pid = 1;
	}

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
	 * Fill in the partition table. We are suppose to use effective address
	 * of process table here. But our linear mapping also enable us to use
	 * physical address here.
	 */
	register_process_table(__pa(process_tb), 0, PRTB_SIZE_SHIFT - 12);
	pr_info("Process table %p and radix root for kernel: %p\n", process_tb, init_mm.pgd);
	asm volatile("ptesync" : : : "memory");
	asm volatile(PPC_TLBIE_5(%0,%1,2,1,1) : :
		     "r" (TLBIEL_INVAL_SET_LPID), "r" (0));
	asm volatile("eieio; tlbsync; ptesync" : : : "memory");
	trace_tlbie(0, 0, TLBIEL_INVAL_SET_LPID, 0, 2, 1, 1);

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
	unsigned long rts_field, dw0;

	mmu_partition_table_init();
	rts_field = radix__get_tree_size();
	dw0 = rts_field | __pa(init_mm.pgd) | RADIX_PGD_INDEX_SIZE | PATB_HR;
	mmu_partition_table_set_entry(0, dw0, 0);

	pr_info("Initializing Radix MMU\n");
	pr_info("Partition table %p\n", partition_tb);
}

void __init radix_init_native(void)
{
	register_process_table = native_register_process_table;
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

	/* Find MMU PID size */
	prop = of_get_flat_dt_prop(node, "ibm,mmu-pid-bits", &size);
	if (prop && size == 4)
		mmu_pid_bits = be32_to_cpup(prop);

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

static void radix_init_amor(void)
{
	/*
	* In HV mode, we init AMOR (Authority Mask Override Register) so that
	* the hypervisor and guest can setup IAMR (Instruction Authority Mask
	* Register), enable key 0 and set it to 1.
	*
	* AMOR = 0b1100 .... 0000 (Mask for key 0 is 11)
	*/
	mtspr(SPRN_AMOR, (3ul << 62));
}

static void radix_init_iamr(void)
{
	/*
	 * Radix always uses key0 of the IAMR to determine if an access is
	 * allowed. We set bit 0 (IBM bit 1) of key0, to prevent instruction
	 * fetch.
	 */
	mtspr(SPRN_IAMR, (1ul << 62));
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
	__pud_cache_index = RADIX_PUD_INDEX_SIZE;
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
	__kernel_io_start = RADIX_KERN_IO_START;
	vmemmap = (struct page *)RADIX_VMEMMAP_BASE;
	ioremap_bot = IOREMAP_BASE;

#ifdef CONFIG_PCI
	pci_io_base = ISA_IO_BASE;
#endif
	__pte_frag_nr = RADIX_PTE_FRAG_NR;
	__pte_frag_size_shift = RADIX_PTE_FRAG_SIZE_SHIFT;
	__pmd_frag_nr = RADIX_PMD_FRAG_NR;
	__pmd_frag_size_shift = RADIX_PMD_FRAG_SIZE_SHIFT;

	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		radix_init_native();
		lpcr = mfspr(SPRN_LPCR);
		mtspr(SPRN_LPCR, lpcr | LPCR_UPRT | LPCR_HR);
		radix_init_partition_table();
		radix_init_amor();
	} else {
		radix_init_pseries();
	}

	memblock_set_current_limit(MEMBLOCK_ALLOC_ANYWHERE);

	radix_init_iamr();
	radix_init_pgtable();
	/* Switch to the guard PID before turning on MMU */
	radix__switch_mmu_context(NULL, &init_mm);
	if (cpu_has_feature(CPU_FTR_HVMODE))
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

		mtspr(SPRN_PTCR,
		      __pa(partition_tb) | (PATB_SIZE_SHIFT - 12));
		radix_init_amor();
	}
	radix_init_iamr();

	radix__switch_mmu_context(NULL, &init_mm);
	if (cpu_has_feature(CPU_FTR_HVMODE))
		tlbiel_all();
}

void radix__mmu_cleanup_all(void)
{
	unsigned long lpcr;

	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		lpcr = mfspr(SPRN_LPCR);
		mtspr(SPRN_LPCR, lpcr & ~LPCR_UPRT);
		mtspr(SPRN_PTCR, 0);
		powernv_set_nmmu_ptcr(0);
		radix__flush_tlb_all();
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
	 * Radix mode is not limited by RMA / VRMA addressing.
	 */
	ppc64_rma_size = ULONG_MAX;
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

struct change_mapping_params {
	pte_t *pte;
	unsigned long start;
	unsigned long end;
	unsigned long aligned_start;
	unsigned long aligned_end;
};

static int __meminit stop_machine_change_mapping(void *data)
{
	struct change_mapping_params *params =
			(struct change_mapping_params *)data;

	if (!data)
		return -1;

	spin_unlock(&init_mm.page_table_lock);
	pte_clear(&init_mm, params->aligned_start, params->pte);
	create_physical_mapping(params->aligned_start, params->start, -1);
	create_physical_mapping(params->end, params->aligned_end, -1);
	spin_lock(&init_mm.page_table_lock);
	return 0;
}

static void remove_pte_table(pte_t *pte_start, unsigned long addr,
			     unsigned long end)
{
	unsigned long next;
	pte_t *pte;

	pte = pte_start + pte_index(addr);
	for (; addr < end; addr = next, pte++) {
		next = (addr + PAGE_SIZE) & PAGE_MASK;
		if (next > end)
			next = end;

		if (!pte_present(*pte))
			continue;

		if (!PAGE_ALIGNED(addr) || !PAGE_ALIGNED(next)) {
			/*
			 * The vmemmap_free() and remove_section_mapping()
			 * codepaths call us with aligned addresses.
			 */
			WARN_ONCE(1, "%s: unaligned range\n", __func__);
			continue;
		}

		pte_clear(&init_mm, addr, pte);
	}
}

/*
 * clear the pte and potentially split the mapping helper
 */
static void __meminit split_kernel_mapping(unsigned long addr, unsigned long end,
				unsigned long size, pte_t *pte)
{
	unsigned long mask = ~(size - 1);
	unsigned long aligned_start = addr & mask;
	unsigned long aligned_end = addr + size;
	struct change_mapping_params params;
	bool split_region = false;

	if ((end - addr) < size) {
		/*
		 * We're going to clear the PTE, but not flushed
		 * the mapping, time to remap and flush. The
		 * effects if visible outside the processor or
		 * if we are running in code close to the
		 * mapping we cleared, we are in trouble.
		 */
		if (overlaps_kernel_text(aligned_start, addr) ||
			overlaps_kernel_text(end, aligned_end)) {
			/*
			 * Hack, just return, don't pte_clear
			 */
			WARN_ONCE(1, "Linear mapping %lx->%lx overlaps kernel "
				  "text, not splitting\n", addr, end);
			return;
		}
		split_region = true;
	}

	if (split_region) {
		params.pte = pte;
		params.start = addr;
		params.end = end;
		params.aligned_start = addr & ~(size - 1);
		params.aligned_end = min_t(unsigned long, aligned_end,
				(unsigned long)__va(memblock_end_of_DRAM()));
		stop_machine(stop_machine_change_mapping, &params, NULL);
		return;
	}

	pte_clear(&init_mm, addr, pte);
}

static void remove_pmd_table(pmd_t *pmd_start, unsigned long addr,
			     unsigned long end)
{
	unsigned long next;
	pte_t *pte_base;
	pmd_t *pmd;

	pmd = pmd_start + pmd_index(addr);
	for (; addr < end; addr = next, pmd++) {
		next = pmd_addr_end(addr, end);

		if (!pmd_present(*pmd))
			continue;

		if (pmd_huge(*pmd)) {
			split_kernel_mapping(addr, end, PMD_SIZE, (pte_t *)pmd);
			continue;
		}

		pte_base = (pte_t *)pmd_page_vaddr(*pmd);
		remove_pte_table(pte_base, addr, next);
		free_pte_table(pte_base, pmd);
	}
}

static void remove_pud_table(pud_t *pud_start, unsigned long addr,
			     unsigned long end)
{
	unsigned long next;
	pmd_t *pmd_base;
	pud_t *pud;

	pud = pud_start + pud_index(addr);
	for (; addr < end; addr = next, pud++) {
		next = pud_addr_end(addr, end);

		if (!pud_present(*pud))
			continue;

		if (pud_huge(*pud)) {
			split_kernel_mapping(addr, end, PUD_SIZE, (pte_t *)pud);
			continue;
		}

		pmd_base = (pmd_t *)pud_page_vaddr(*pud);
		remove_pmd_table(pmd_base, addr, next);
		free_pmd_table(pmd_base, pud);
	}
}

static void __meminit remove_pagetable(unsigned long start, unsigned long end)
{
	unsigned long addr, next;
	pud_t *pud_base;
	pgd_t *pgd;

	spin_lock(&init_mm.page_table_lock);

	for (addr = start; addr < end; addr = next) {
		next = pgd_addr_end(addr, end);

		pgd = pgd_offset_k(addr);
		if (!pgd_present(*pgd))
			continue;

		if (pgd_huge(*pgd)) {
			split_kernel_mapping(addr, end, PGDIR_SIZE, (pte_t *)pgd);
			continue;
		}

		pud_base = (pud_t *)pgd_page_vaddr(*pgd);
		remove_pud_table(pud_base, addr, next);
	}

	spin_unlock(&init_mm.page_table_lock);
	radix__flush_tlb_kernel_range(start, end);
}

int __meminit radix__create_section_mapping(unsigned long start, unsigned long end, int nid)
{
	return create_physical_mapping(start, end, nid);
}

int __meminit radix__remove_section_mapping(unsigned long start, unsigned long end)
{
	remove_pagetable(start, end);
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
	unsigned long flags = _PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_KERNEL_RW;
	int nid = early_pfn_to_nid(phys >> PAGE_SHIFT);
	int ret;

	ret = __map_kernel_page_nid(start, phys, __pgprot(flags), page_size, nid);
	BUG_ON(ret);

	return 0;
}

#ifdef CONFIG_MEMORY_HOTPLUG
void __meminit radix__vmemmap_remove_mapping(unsigned long start, unsigned long page_size)
{
	remove_pagetable(start, start + page_size);
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
	VM_BUG_ON(pmd_devmap(*pmdp));
	/*
	 * khugepaged calls this for normal pmd
	 */
	pmd = *pmdp;
	pmd_clear(pmdp);

	/*FIXME!!  Verify whether we need this kick below */
	serialize_against_pte_lookup(vma->vm_mm);

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
	/*
	 * Serialize against find_current_mm_pte which does lock-less
	 * lookup in page tables with local interrupts disabled. For huge pages
	 * it casts pmd_t to pte_t. Since format of pte_t is different from
	 * pmd_t we want to prevent transit from pmd pointing to page table
	 * to pmd pointing to huge page (and back) while interrupts are disabled.
	 * We clear pmd to possibly replace it with page table pointer in
	 * different code paths. So make sure we wait for the parallel
	 * find_current_mm_pte to finish.
	 */
	serialize_against_pte_lookup(mm);
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

void radix__ptep_set_access_flags(struct vm_area_struct *vma, pte_t *ptep,
				  pte_t entry, unsigned long address, int psize)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long set = pte_val(entry) & (_PAGE_DIRTY | _PAGE_ACCESSED |
					      _PAGE_RW | _PAGE_EXEC);

	unsigned long change = pte_val(entry) ^ pte_val(*ptep);
	/*
	 * To avoid NMMU hang while relaxing access, we need mark
	 * the pte invalid in between.
	 */
	if ((change & _PAGE_RW) && atomic_read(&mm->context.copros) > 0) {
		unsigned long old_pte, new_pte;

		old_pte = __radix_pte_update(ptep, _PAGE_PRESENT, _PAGE_INVALID);
		/*
		 * new value of pte
		 */
		new_pte = old_pte | set;
		radix__flush_tlb_page_psize(mm, address, psize);
		__radix_pte_update(ptep, _PAGE_INVALID, new_pte);
	} else {
		__radix_pte_update(ptep, 0, set);
		/*
		 * Book3S does not require a TLB flush when relaxing access
		 * restrictions when the address space is not attached to a
		 * NMMU, because the core MMU will reload the pte after taking
		 * an access fault, which is defined by the architectue.
		 */
	}
	/* See ptesync comment in radix__set_pte_at */
}

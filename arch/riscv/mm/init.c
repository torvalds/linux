// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/initrd.h>
#include <linux/swap.h>
#include <linux/sizes.h>
#include <linux/of_fdt.h>

#include <asm/fixmap.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/io.h>

unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)]
							__page_aligned_bss;
EXPORT_SYMBOL(empty_zero_page);

extern char _start[];

static void __init zone_sizes_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES] = { 0, };

#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32] = PFN_DOWN(min(4UL * SZ_1G,
			(unsigned long) PFN_PHYS(max_low_pfn)));
#endif
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;

	free_area_init_nodes(max_zone_pfns);
}

void setup_zero_page(void)
{
	memset((void *)empty_zero_page, 0, PAGE_SIZE);
}

void __init paging_init(void)
{
	setup_zero_page();
	local_flush_tlb_all();
	zone_sizes_init();
}

void __init mem_init(void)
{
#ifdef CONFIG_FLATMEM
	BUG_ON(!mem_map);
#endif /* CONFIG_FLATMEM */

	high_memory = (void *)(__va(PFN_PHYS(max_low_pfn)));
	memblock_free_all();

	mem_init_print_info(NULL);
}

#ifdef CONFIG_BLK_DEV_INITRD
static void __init setup_initrd(void)
{
	unsigned long size;

	if (initrd_start >= initrd_end) {
		pr_info("initrd not found or empty");
		goto disable;
	}
	if (__pa(initrd_end) > PFN_PHYS(max_low_pfn)) {
		pr_err("initrd extends beyond end of memory");
		goto disable;
	}

	size = initrd_end - initrd_start;
	memblock_reserve(__pa(initrd_start), size);
	initrd_below_start_ok = 1;

	pr_info("Initial ramdisk at: 0x%p (%lu bytes)\n",
		(void *)(initrd_start), size);
	return;
disable:
	pr_cont(" - disabling initrd\n");
	initrd_start = 0;
	initrd_end = 0;
}

void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, -1, "initrd");
}
#endif /* CONFIG_BLK_DEV_INITRD */

void __init setup_bootmem(void)
{
	struct memblock_region *reg;
	phys_addr_t mem_size = 0;
	phys_addr_t vmlinux_end = __pa(&_end);
	phys_addr_t vmlinux_start = __pa(&_start);

	/* Find the memory region containing the kernel */
	for_each_memblock(memory, reg) {
		phys_addr_t end = reg->base + reg->size;

		if (reg->base <= vmlinux_end && vmlinux_end <= end) {
			mem_size = min(reg->size, (phys_addr_t)-PAGE_OFFSET);

			/*
			 * Remove memblock from the end of usable area to the
			 * end of region
			 */
			if (reg->base + mem_size < end)
				memblock_remove(reg->base + mem_size,
						end - reg->base - mem_size);
		}
	}
	BUG_ON(mem_size == 0);

	/* Reserve from the start of the kernel to the end of the kernel */
	memblock_reserve(vmlinux_start, vmlinux_end - vmlinux_start);

	set_max_mapnr(PFN_DOWN(mem_size));
	max_low_pfn = PFN_DOWN(memblock_end_of_DRAM());

#ifdef CONFIG_BLK_DEV_INITRD
	setup_initrd();
#endif /* CONFIG_BLK_DEV_INITRD */

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();
	memblock_allow_resize();
	memblock_dump_all();

	for_each_memblock(memory, reg) {
		unsigned long start_pfn = memblock_region_memory_base_pfn(reg);
		unsigned long end_pfn = memblock_region_memory_end_pfn(reg);

		memblock_set_node(PFN_PHYS(start_pfn),
				  PFN_PHYS(end_pfn - start_pfn),
				  &memblock.memory, 0);
	}
}

unsigned long va_pa_offset;
EXPORT_SYMBOL(va_pa_offset);
unsigned long pfn_base;
EXPORT_SYMBOL(pfn_base);

pgd_t swapper_pg_dir[PTRS_PER_PGD] __page_aligned_bss;
pgd_t trampoline_pg_dir[PTRS_PER_PGD] __initdata __aligned(PAGE_SIZE);

#ifndef __PAGETABLE_PMD_FOLDED
#define NUM_SWAPPER_PMDS ((uintptr_t)-PAGE_OFFSET >> PGDIR_SHIFT)
pmd_t swapper_pmd[PTRS_PER_PMD*((-PAGE_OFFSET)/PGDIR_SIZE)] __page_aligned_bss;
pmd_t trampoline_pmd[PTRS_PER_PGD] __initdata __aligned(PAGE_SIZE);
pmd_t fixmap_pmd[PTRS_PER_PMD] __page_aligned_bss;
#endif

pte_t fixmap_pte[PTRS_PER_PTE] __page_aligned_bss;

void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t prot)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *ptep;

	BUG_ON(idx <= FIX_HOLE || idx >= __end_of_fixed_addresses);

	ptep = &fixmap_pte[pte_index(addr)];

	if (pgprot_val(prot)) {
		set_pte(ptep, pfn_pte(phys >> PAGE_SHIFT, prot));
	} else {
		pte_clear(&init_mm, addr, ptep);
		local_flush_tlb_page(addr);
	}
}

/*
 * setup_vm() is called from head.S with MMU-off.
 *
 * Following requirements should be honoured for setup_vm() to work
 * correctly:
 * 1) It should use PC-relative addressing for accessing kernel symbols.
 *    To achieve this we always use GCC cmodel=medany.
 * 2) The compiler instrumentation for FTRACE will not work for setup_vm()
 *    so disable compiler instrumentation when FTRACE is enabled.
 *
 * Currently, the above requirements are honoured by using custom CFLAGS
 * for init.o in mm/Makefile.
 */

#ifndef __riscv_cmodel_medany
#error "setup_vm() is called from head.S before relocate so it should "
	"not use absolute addressing."
#endif

asmlinkage void __init setup_vm(void)
{
	uintptr_t i;
	uintptr_t pa = (uintptr_t) &_start;
	pgprot_t prot = __pgprot(pgprot_val(PAGE_KERNEL) | _PAGE_EXEC);

	va_pa_offset = PAGE_OFFSET - pa;
	pfn_base = PFN_DOWN(pa);

	/* Sanity check alignment and size */
	BUG_ON((PAGE_OFFSET % PGDIR_SIZE) != 0);
	BUG_ON((pa % (PAGE_SIZE * PTRS_PER_PTE)) != 0);

#ifndef __PAGETABLE_PMD_FOLDED
	trampoline_pg_dir[(PAGE_OFFSET >> PGDIR_SHIFT) % PTRS_PER_PGD] =
		pfn_pgd(PFN_DOWN((uintptr_t)trampoline_pmd),
			__pgprot(_PAGE_TABLE));
	trampoline_pmd[0] = pfn_pmd(PFN_DOWN(pa), prot);

	for (i = 0; i < (-PAGE_OFFSET)/PGDIR_SIZE; ++i) {
		size_t o = (PAGE_OFFSET >> PGDIR_SHIFT) % PTRS_PER_PGD + i;

		swapper_pg_dir[o] =
			pfn_pgd(PFN_DOWN((uintptr_t)swapper_pmd) + i,
				__pgprot(_PAGE_TABLE));
	}
	for (i = 0; i < ARRAY_SIZE(swapper_pmd); i++)
		swapper_pmd[i] = pfn_pmd(PFN_DOWN(pa + i * PMD_SIZE), prot);

	swapper_pg_dir[(FIXADDR_START >> PGDIR_SHIFT) % PTRS_PER_PGD] =
		pfn_pgd(PFN_DOWN((uintptr_t)fixmap_pmd),
				__pgprot(_PAGE_TABLE));
	fixmap_pmd[(FIXADDR_START >> PMD_SHIFT) % PTRS_PER_PMD] =
		pfn_pmd(PFN_DOWN((uintptr_t)fixmap_pte),
				__pgprot(_PAGE_TABLE));
#else
	trampoline_pg_dir[(PAGE_OFFSET >> PGDIR_SHIFT) % PTRS_PER_PGD] =
		pfn_pgd(PFN_DOWN(pa), prot);

	for (i = 0; i < (-PAGE_OFFSET)/PGDIR_SIZE; ++i) {
		size_t o = (PAGE_OFFSET >> PGDIR_SHIFT) % PTRS_PER_PGD + i;

		swapper_pg_dir[o] =
			pfn_pgd(PFN_DOWN(pa + i * PGDIR_SIZE), prot);
	}

	swapper_pg_dir[(FIXADDR_START >> PGDIR_SHIFT) % PTRS_PER_PGD] =
		pfn_pgd(PFN_DOWN((uintptr_t)fixmap_pte),
				__pgprot(_PAGE_TABLE));
#endif
}

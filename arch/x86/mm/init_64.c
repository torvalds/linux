/*
 *  linux/arch/x86_64/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 *  Copyright (C) 2002,2003 Andi Kleen <ak@suse.de>
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/pfn.h>
#include <linux/poison.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/memory_hotplug.h>
#include <linux/nmi.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/fixmap.h>
#include <asm/e820.h>
#include <asm/apic.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <asm/smp.h>
#include <asm/sections.h>
#include <asm/kdebug.h>
#include <asm/numa.h>
#include <asm/cacheflush.h>

static unsigned long dma_reserve __initdata;

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

int direct_gbpages __meminitdata
#ifdef CONFIG_DIRECT_GBPAGES
				= 1
#endif
;

static int __init parse_direct_gbpages_off(char *arg)
{
	direct_gbpages = 0;
	return 0;
}
early_param("nogbpages", parse_direct_gbpages_off);

static int __init parse_direct_gbpages_on(char *arg)
{
	direct_gbpages = 1;
	return 0;
}
early_param("gbpages", parse_direct_gbpages_on);

/*
 * NOTE: pagetable_init alloc all the fixmap pagetables contiguous on the
 * physical space so we can cache the place of the first one and move
 * around without checking the pgd every time.
 */

void show_mem(void)
{
	long i, total = 0, reserved = 0;
	long shared = 0, cached = 0;
	struct page *page;
	pg_data_t *pgdat;

	printk(KERN_INFO "Mem-info:\n");
	show_free_areas();
	for_each_online_pgdat(pgdat) {
		for (i = 0; i < pgdat->node_spanned_pages; ++i) {
			/*
			 * This loop can take a while with 256 GB and
			 * 4k pages so defer the NMI watchdog:
			 */
			if (unlikely(i % MAX_ORDER_NR_PAGES == 0))
				touch_nmi_watchdog();

			if (!pfn_valid(pgdat->node_start_pfn + i))
				continue;

			page = pfn_to_page(pgdat->node_start_pfn + i);
			total++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (page_count(page))
				shared += page_count(page) - 1;
		}
	}
	printk(KERN_INFO "%lu pages of RAM\n",		total);
	printk(KERN_INFO "%lu reserved pages\n",	reserved);
	printk(KERN_INFO "%lu pages shared\n",		shared);
	printk(KERN_INFO "%lu pages swap cached\n",	cached);
}

int after_bootmem;

static __init void *spp_getpage(void)
{
	void *ptr;

	if (after_bootmem)
		ptr = (void *) get_zeroed_page(GFP_ATOMIC);
	else
		ptr = alloc_bootmem_pages(PAGE_SIZE);

	if (!ptr || ((unsigned long)ptr & ~PAGE_MASK)) {
		panic("set_pte_phys: cannot allocate page data %s\n",
			after_bootmem ? "after bootmem" : "");
	}

	pr_debug("spp_getpage %p\n", ptr);

	return ptr;
}

static void
set_pte_phys(unsigned long vaddr, unsigned long phys, pgprot_t prot)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte, new_pte;

	pr_debug("set_pte_phys %lx to %lx\n", vaddr, phys);

	pgd = pgd_offset_k(vaddr);
	if (pgd_none(*pgd)) {
		printk(KERN_ERR
			"PGD FIXMAP MISSING, it should be setup in head.S!\n");
		return;
	}
	pud = pud_offset(pgd, vaddr);
	if (pud_none(*pud)) {
		pmd = (pmd_t *) spp_getpage();
		set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE | _PAGE_USER));
		if (pmd != pmd_offset(pud, 0)) {
			printk(KERN_ERR "PAGETABLE BUG #01! %p <-> %p\n",
				pmd, pmd_offset(pud, 0));
			return;
		}
	}
	pmd = pmd_offset(pud, vaddr);
	if (pmd_none(*pmd)) {
		pte = (pte_t *) spp_getpage();
		set_pmd(pmd, __pmd(__pa(pte) | _KERNPG_TABLE | _PAGE_USER));
		if (pte != pte_offset_kernel(pmd, 0)) {
			printk(KERN_ERR "PAGETABLE BUG #02!\n");
			return;
		}
	}
	new_pte = pfn_pte(phys >> PAGE_SHIFT, prot);

	pte = pte_offset_kernel(pmd, vaddr);
	if (!pte_none(*pte) && pte_val(new_pte) &&
	    pte_val(*pte) != (pte_val(new_pte) & __supported_pte_mask))
		pte_ERROR(*pte);
	set_pte(pte, new_pte);

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

/*
 * The head.S code sets up the kernel high mapping:
 *
 *   from __START_KERNEL_map to __START_KERNEL_map + size (== _end-_text)
 *
 * phys_addr holds the negative offset to the kernel, which is added
 * to the compile time generated pmds. This results in invalid pmds up
 * to the point where we hit the physaddr 0 mapping.
 *
 * We limit the mappings to the region from _text to _end.  _end is
 * rounded up to the 2MB boundary. This catches the invalid pmds as
 * well, as they are located before _text:
 */
void __init cleanup_highmap(void)
{
	unsigned long vaddr = __START_KERNEL_map;
	unsigned long end = round_up((unsigned long)_end, PMD_SIZE) - 1;
	pmd_t *pmd = level2_kernel_pgt;
	pmd_t *last_pmd = pmd + PTRS_PER_PMD;

	for (; pmd < last_pmd; pmd++, vaddr += PMD_SIZE) {
		if (!pmd_present(*pmd))
			continue;
		if (vaddr < (unsigned long) _text || vaddr > end)
			set_pmd(pmd, __pmd(0));
	}
}

/* NOTE: this is meant to be run only at boot */
void __set_fixmap(enum fixed_addresses idx, unsigned long phys, pgprot_t prot)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		printk(KERN_ERR "Invalid __set_fixmap\n");
		return;
	}
	set_pte_phys(address, phys, prot);
}

static unsigned long __initdata table_start;
static unsigned long __meminitdata table_end;

static __meminit void *alloc_low_page(unsigned long *phys)
{
	unsigned long pfn = table_end++;
	void *adr;

	if (after_bootmem) {
		adr = (void *)get_zeroed_page(GFP_ATOMIC);
		*phys = __pa(adr);

		return adr;
	}

	if (pfn >= end_pfn)
		panic("alloc_low_page: ran out of memory");

	adr = early_ioremap(pfn * PAGE_SIZE, PAGE_SIZE);
	memset(adr, 0, PAGE_SIZE);
	*phys  = pfn * PAGE_SIZE;
	return adr;
}

static __meminit void unmap_low_page(void *adr)
{
	if (after_bootmem)
		return;

	early_iounmap(adr, PAGE_SIZE);
}

/* Must run before zap_low_mappings */
__meminit void *early_ioremap(unsigned long addr, unsigned long size)
{
	pmd_t *pmd, *last_pmd;
	unsigned long vaddr;
	int i, pmds;

	pmds = ((addr & ~PMD_MASK) + size + ~PMD_MASK) / PMD_SIZE;
	vaddr = __START_KERNEL_map;
	pmd = level2_kernel_pgt;
	last_pmd = level2_kernel_pgt + PTRS_PER_PMD - 1;

	for (; pmd <= last_pmd; pmd++, vaddr += PMD_SIZE) {
		for (i = 0; i < pmds; i++) {
			if (pmd_present(pmd[i]))
				goto continue_outer_loop;
		}
		vaddr += addr & ~PMD_MASK;
		addr &= PMD_MASK;

		for (i = 0; i < pmds; i++, addr += PMD_SIZE)
			set_pmd(pmd+i, __pmd(addr | __PAGE_KERNEL_LARGE_EXEC));
		__flush_tlb_all();

		return (void *)vaddr;
continue_outer_loop:
		;
	}
	printk(KERN_ERR "early_ioremap(0x%lx, %lu) failed\n", addr, size);

	return NULL;
}

/*
 * To avoid virtual aliases later:
 */
__meminit void early_iounmap(void *addr, unsigned long size)
{
	unsigned long vaddr;
	pmd_t *pmd;
	int i, pmds;

	vaddr = (unsigned long)addr;
	pmds = ((vaddr & ~PMD_MASK) + size + ~PMD_MASK) / PMD_SIZE;
	pmd = level2_kernel_pgt + pmd_index(vaddr);

	for (i = 0; i < pmds; i++)
		pmd_clear(pmd + i);

	__flush_tlb_all();
}

static unsigned long __meminit
phys_pmd_init(pmd_t *pmd_page, unsigned long address, unsigned long end)
{
	int i = pmd_index(address);

	for (; i < PTRS_PER_PMD; i++, address += PMD_SIZE) {
		pmd_t *pmd = pmd_page + pmd_index(address);

		if (address >= end) {
			if (!after_bootmem) {
				for (; i < PTRS_PER_PMD; i++, pmd++)
					set_pmd(pmd, __pmd(0));
			}
			break;
		}

		if (pmd_val(*pmd))
			continue;

		set_pte((pte_t *)pmd,
			pfn_pte(address >> PAGE_SHIFT, PAGE_KERNEL_LARGE));
	}
	return address;
}

static unsigned long __meminit
phys_pmd_update(pud_t *pud, unsigned long address, unsigned long end)
{
	pmd_t *pmd = pmd_offset(pud, 0);
	unsigned long last_map_addr;

	spin_lock(&init_mm.page_table_lock);
	last_map_addr = phys_pmd_init(pmd, address, end);
	spin_unlock(&init_mm.page_table_lock);
	__flush_tlb_all();
	return last_map_addr;
}

static unsigned long __meminit
phys_pud_init(pud_t *pud_page, unsigned long addr, unsigned long end)
{
	unsigned long last_map_addr = end;
	int i = pud_index(addr);

	for (; i < PTRS_PER_PUD; i++, addr = (addr & PUD_MASK) + PUD_SIZE) {
		unsigned long pmd_phys;
		pud_t *pud = pud_page + pud_index(addr);
		pmd_t *pmd;

		if (addr >= end)
			break;

		if (!after_bootmem &&
				!e820_any_mapped(addr, addr+PUD_SIZE, 0)) {
			set_pud(pud, __pud(0));
			continue;
		}

		if (pud_val(*pud)) {
			if (!pud_large(*pud))
				last_map_addr = phys_pmd_update(pud, addr, end);
			continue;
		}

		if (direct_gbpages) {
			set_pte((pte_t *)pud,
				pfn_pte(addr >> PAGE_SHIFT, PAGE_KERNEL_LARGE));
			last_map_addr = (addr & PUD_MASK) + PUD_SIZE;
			continue;
		}

		pmd = alloc_low_page(&pmd_phys);

		spin_lock(&init_mm.page_table_lock);
		set_pud(pud, __pud(pmd_phys | _KERNPG_TABLE));
		last_map_addr = phys_pmd_init(pmd, addr, end);
		spin_unlock(&init_mm.page_table_lock);

		unmap_low_page(pmd);
	}
	__flush_tlb_all();

	return last_map_addr >> PAGE_SHIFT;
}

static void __init find_early_table_space(unsigned long end)
{
	unsigned long puds, pmds, tables, start;

	puds = (end + PUD_SIZE - 1) >> PUD_SHIFT;
	tables = round_up(puds * sizeof(pud_t), PAGE_SIZE);
	if (!direct_gbpages) {
		pmds = (end + PMD_SIZE - 1) >> PMD_SHIFT;
		tables += round_up(pmds * sizeof(pmd_t), PAGE_SIZE);
	}

	/*
	 * RED-PEN putting page tables only on node 0 could
	 * cause a hotspot and fill up ZONE_DMA. The page tables
	 * need roughly 0.5KB per GB.
	 */
	start = 0x8000;
	table_start = find_e820_area(start, end, tables, PAGE_SIZE);
	if (table_start == -1UL)
		panic("Cannot find space for the kernel page tables");

	table_start >>= PAGE_SHIFT;
	table_end = table_start;

	early_printk("kernel direct mapping tables up to %lx @ %lx-%lx\n",
		end, table_start << PAGE_SHIFT,
		(table_start << PAGE_SHIFT) + tables);
}

static void __init init_gbpages(void)
{
	if (direct_gbpages && cpu_has_gbpages)
		printk(KERN_INFO "Using GB pages for direct mapping\n");
	else
		direct_gbpages = 0;
}

#ifdef CONFIG_MEMTEST_BOOTPARAM

static void __init memtest(unsigned long start_phys, unsigned long size,
				 unsigned pattern)
{
	unsigned long i;
	unsigned long *start;
	unsigned long start_bad;
	unsigned long last_bad;
	unsigned long val;
	unsigned long start_phys_aligned;
	unsigned long count;
	unsigned long incr;

	switch (pattern) {
	case 0:
		val = 0UL;
		break;
	case 1:
		val = -1UL;
		break;
	case 2:
		val = 0x5555555555555555UL;
		break;
	case 3:
		val = 0xaaaaaaaaaaaaaaaaUL;
		break;
	default:
		return;
	}

	incr = sizeof(unsigned long);
	start_phys_aligned = ALIGN(start_phys, incr);
	count = (size - (start_phys_aligned - start_phys))/incr;
	start = __va(start_phys_aligned);
	start_bad = 0;
	last_bad = 0;

	for (i = 0; i < count; i++)
		start[i] = val;
	for (i = 0; i < count; i++, start++, start_phys_aligned += incr) {
		if (*start != val) {
			if (start_phys_aligned == last_bad + incr) {
				last_bad += incr;
			} else {
				if (start_bad) {
					printk(KERN_CONT "\n  %016lx bad mem addr %016lx - %016lx reserved",
						val, start_bad, last_bad + incr);
					reserve_early(start_bad, last_bad - start_bad, "BAD RAM");
				}
				start_bad = last_bad = start_phys_aligned;
			}
		}
	}
	if (start_bad) {
		printk(KERN_CONT "\n  %016lx bad mem addr %016lx - %016lx reserved",
			val, start_bad, last_bad + incr);
		reserve_early(start_bad, last_bad - start_bad, "BAD RAM");
	}

}

static int memtest_pattern __initdata = CONFIG_MEMTEST_BOOTPARAM_VALUE;

static int __init parse_memtest(char *arg)
{
	if (arg)
		memtest_pattern = simple_strtoul(arg, NULL, 0);
	return 0;
}

early_param("memtest", parse_memtest);

static void __init early_memtest(unsigned long start, unsigned long end)
{
	unsigned long t_start, t_size;
	unsigned pattern;

	if (!memtest_pattern)
		return;

	printk(KERN_INFO "early_memtest: pattern num %d", memtest_pattern);
	for (pattern = 0; pattern < memtest_pattern; pattern++) {
		t_start = start;
		t_size = 0;
		while (t_start < end) {
			t_start = find_e820_area_size(t_start, &t_size, 1);

			/* done ? */
			if (t_start >= end)
				break;
			if (t_start + t_size > end)
				t_size = end - t_start;

			printk(KERN_CONT "\n  %016lx - %016lx pattern %d",
				t_start, t_start + t_size, pattern);

			memtest(t_start, t_size, pattern);

			t_start += t_size;
		}
	}
	printk(KERN_CONT "\n");
}
#else
static void __init early_memtest(unsigned long start, unsigned long end)
{
}
#endif

/*
 * Setup the direct mapping of the physical memory at PAGE_OFFSET.
 * This runs before bootmem is initialized and gets pages directly from
 * the physical memory. To access them they are temporarily mapped.
 */
unsigned long __init_refok init_memory_mapping(unsigned long start, unsigned long end)
{
	unsigned long next, last_map_addr = end;
	unsigned long start_phys = start, end_phys = end;

	printk(KERN_INFO "init_memory_mapping\n");

	/*
	 * Find space for the kernel direct mapping tables.
	 *
	 * Later we should allocate these tables in the local node of the
	 * memory mapped. Unfortunately this is done currently before the
	 * nodes are discovered.
	 */
	if (!after_bootmem) {
		init_gbpages();
		find_early_table_space(end);
	}

	start = (unsigned long)__va(start);
	end = (unsigned long)__va(end);

	for (; start < end; start = next) {
		pgd_t *pgd = pgd_offset_k(start);
		unsigned long pud_phys;
		pud_t *pud;

		if (after_bootmem)
			pud = pud_offset(pgd, start & PGDIR_MASK);
		else
			pud = alloc_low_page(&pud_phys);

		next = start + PGDIR_SIZE;
		if (next > end)
			next = end;
		last_map_addr = phys_pud_init(pud, __pa(start), __pa(next));
		if (!after_bootmem)
			set_pgd(pgd_offset_k(start), mk_kernel_pgd(pud_phys));
		unmap_low_page(pud);
	}

	if (!after_bootmem)
		mmu_cr4_features = read_cr4();
	__flush_tlb_all();

	if (!after_bootmem)
		reserve_early(table_start << PAGE_SHIFT,
				 table_end << PAGE_SHIFT, "PGTABLE");

	if (!after_bootmem)
		early_memtest(start_phys, end_phys);

	return last_map_addr;
}

#ifndef CONFIG_NUMA
void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] = MAX_DMA_PFN;
	max_zone_pfns[ZONE_DMA32] = MAX_DMA32_PFN;
	max_zone_pfns[ZONE_NORMAL] = end_pfn;

	memory_present(0, 0, end_pfn);
	sparse_init();
	free_area_init_nodes(max_zone_pfns);
}
#endif

/*
 * Memory hotplug specific functions
 */
void online_page(struct page *page)
{
	ClearPageReserved(page);
	init_page_count(page);
	__free_page(page);
	totalram_pages++;
	num_physpages++;
}

#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * Memory is added always to NORMAL zone. This means you will never get
 * additional DMA/DMA32 memory.
 */
int arch_add_memory(int nid, u64 start, u64 size)
{
	struct pglist_data *pgdat = NODE_DATA(nid);
	struct zone *zone = pgdat->node_zones + ZONE_NORMAL;
	unsigned long last_mapped_pfn, start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int ret;

	last_mapped_pfn = init_memory_mapping(start, start + size-1);
	if (last_mapped_pfn > max_pfn_mapped)
		max_pfn_mapped = last_mapped_pfn;

	ret = __add_pages(zone, start_pfn, nr_pages);
	WARN_ON(1);

	return ret;
}
EXPORT_SYMBOL_GPL(arch_add_memory);

#if !defined(CONFIG_ACPI_NUMA) && defined(CONFIG_NUMA)
int memory_add_physaddr_to_nid(u64 start)
{
	return 0;
}
EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif

#endif /* CONFIG_MEMORY_HOTPLUG */

/*
 * devmem_is_allowed() checks to see if /dev/mem access to a certain address
 * is valid. The argument is a physical page number.
 *
 *
 * On x86, access has to be given to the first megabyte of ram because that area
 * contains bios code and data regions used by X and dosemu and similar apps.
 * Access has to be given to non-kernel-ram areas as well, these contain the PCI
 * mmio resources as well as potential bios/acpi data regions.
 */
int devmem_is_allowed(unsigned long pagenr)
{
	if (pagenr <= 256)
		return 1;
	if (!page_is_ram(pagenr))
		return 1;
	return 0;
}


static struct kcore_list kcore_mem, kcore_vmalloc, kcore_kernel,
			 kcore_modules, kcore_vsyscall;

void __init mem_init(void)
{
	long codesize, reservedpages, datasize, initsize;

	pci_iommu_alloc();

	/* clear_bss() already clear the empty_zero_page */

	reservedpages = 0;

	/* this will put all low memory onto the freelists */
#ifdef CONFIG_NUMA
	totalram_pages = numa_free_all_bootmem();
#else
	totalram_pages = free_all_bootmem();
#endif
	reservedpages = end_pfn - totalram_pages -
					absent_pages_in_range(0, end_pfn);
	after_bootmem = 1;

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	/* Register memory areas for /proc/kcore */
	kclist_add(&kcore_mem, __va(0), max_low_pfn << PAGE_SHIFT);
	kclist_add(&kcore_vmalloc, (void *)VMALLOC_START,
		   VMALLOC_END-VMALLOC_START);
	kclist_add(&kcore_kernel, &_stext, _end - _stext);
	kclist_add(&kcore_modules, (void *)MODULES_VADDR, MODULES_LEN);
	kclist_add(&kcore_vsyscall, (void *)VSYSCALL_START,
				 VSYSCALL_END - VSYSCALL_START);

	printk(KERN_INFO "Memory: %luk/%luk available (%ldk kernel code, "
				"%ldk reserved, %ldk data, %ldk init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		end_pfn << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		initsize >> 10);

	cpa_init();
}

void free_init_pages(char *what, unsigned long begin, unsigned long end)
{
	unsigned long addr = begin;

	if (addr >= end)
		return;

	/*
	 * If debugging page accesses then do not free this memory but
	 * mark them not present - any buggy init-section access will
	 * create a kernel page fault:
	 */
#ifdef CONFIG_DEBUG_PAGEALLOC
	printk(KERN_INFO "debug: unmapping init memory %08lx..%08lx\n",
		begin, PAGE_ALIGN(end));
	set_memory_np(begin, (end - begin) >> PAGE_SHIFT);
#else
	printk(KERN_INFO "Freeing %s: %luk freed\n", what, (end - begin) >> 10);

	for (; addr < end; addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		memset((void *)(addr & ~(PAGE_SIZE-1)),
			POISON_FREE_INITMEM, PAGE_SIZE);
		free_page(addr);
		totalram_pages++;
	}
#endif
}

void free_initmem(void)
{
	free_init_pages("unused kernel memory",
			(unsigned long)(&__init_begin),
			(unsigned long)(&__init_end));
}

#ifdef CONFIG_DEBUG_RODATA
const int rodata_test_data = 0xC3;
EXPORT_SYMBOL_GPL(rodata_test_data);

void mark_rodata_ro(void)
{
	unsigned long start = PFN_ALIGN(_stext), end = PFN_ALIGN(__end_rodata);

	printk(KERN_INFO "Write protecting the kernel read-only data: %luk\n",
	       (end - start) >> 10);
	set_memory_ro(start, (end - start) >> PAGE_SHIFT);

	/*
	 * The rodata section (but not the kernel text!) should also be
	 * not-executable.
	 */
	start = ((unsigned long)__start_rodata + PAGE_SIZE - 1) & PAGE_MASK;
	set_memory_nx(start, (end - start) >> PAGE_SHIFT);

	rodata_test();

#ifdef CONFIG_CPA_DEBUG
	printk(KERN_INFO "Testing CPA: undo %lx-%lx\n", start, end);
	set_memory_rw(start, (end-start) >> PAGE_SHIFT);

	printk(KERN_INFO "Testing CPA: again\n");
	set_memory_ro(start, (end-start) >> PAGE_SHIFT);
#endif
}

#endif

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_init_pages("initrd memory", start, end);
}
#endif

void __init reserve_bootmem_generic(unsigned long phys, unsigned len)
{
#ifdef CONFIG_NUMA
	int nid, next_nid;
#endif
	unsigned long pfn = phys >> PAGE_SHIFT;

	if (pfn >= end_pfn) {
		/*
		 * This can happen with kdump kernels when accessing
		 * firmware tables:
		 */
		if (pfn < max_pfn_mapped)
			return;

		printk(KERN_ERR "reserve_bootmem: illegal reserve %lx %u\n",
				phys, len);
		return;
	}

	/* Should check here against the e820 map to avoid double free */
#ifdef CONFIG_NUMA
	nid = phys_to_nid(phys);
	next_nid = phys_to_nid(phys + len - 1);
	if (nid == next_nid)
		reserve_bootmem_node(NODE_DATA(nid), phys, len, BOOTMEM_DEFAULT);
	else
		reserve_bootmem(phys, len, BOOTMEM_DEFAULT);
#else
	reserve_bootmem(phys, len, BOOTMEM_DEFAULT);
#endif

	if (phys+len <= MAX_DMA_PFN*PAGE_SIZE) {
		dma_reserve += len / PAGE_SIZE;
		set_dma_reserve(dma_reserve);
	}
}

int kern_addr_valid(unsigned long addr)
{
	unsigned long above = ((long)addr) >> __VIRTUAL_MASK_SHIFT;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	if (above != 0 && above != -1UL)
		return 0;

	pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd))
		return 0;

	pud = pud_offset(pgd, addr);
	if (pud_none(*pud))
		return 0;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return 0;

	if (pmd_large(*pmd))
		return pfn_valid(pmd_pfn(*pmd));

	pte = pte_offset_kernel(pmd, addr);
	if (pte_none(*pte))
		return 0;

	return pfn_valid(pte_pfn(*pte));
}

/*
 * A pseudo VMA to allow ptrace access for the vsyscall page.  This only
 * covers the 64bit vsyscall page now. 32bit has a real VMA now and does
 * not need special handling anymore:
 */
static struct vm_area_struct gate_vma = {
	.vm_start	= VSYSCALL_START,
	.vm_end		= VSYSCALL_START + (VSYSCALL_MAPPED_PAGES * PAGE_SIZE),
	.vm_page_prot	= PAGE_READONLY_EXEC,
	.vm_flags	= VM_READ | VM_EXEC
};

struct vm_area_struct *get_gate_vma(struct task_struct *tsk)
{
#ifdef CONFIG_IA32_EMULATION
	if (test_tsk_thread_flag(tsk, TIF_IA32))
		return NULL;
#endif
	return &gate_vma;
}

int in_gate_area(struct task_struct *task, unsigned long addr)
{
	struct vm_area_struct *vma = get_gate_vma(task);

	if (!vma)
		return 0;

	return (addr >= vma->vm_start) && (addr < vma->vm_end);
}

/*
 * Use this when you have no reliable task/vma, typically from interrupt
 * context. It is less reliable than using the task's vma and may give
 * false positives:
 */
int in_gate_area_no_task(unsigned long addr)
{
	return (addr >= VSYSCALL_START) && (addr < VSYSCALL_END);
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso)
		return "[vdso]";
	if (vma == &gate_vma)
		return "[vsyscall]";
	return NULL;
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/*
 * Initialise the sparsemem vmemmap using huge-pages at the PMD level.
 */
static long __meminitdata addr_start, addr_end;
static void __meminitdata *p_start, *p_end;
static int __meminitdata node_start;

int __meminit
vmemmap_populate(struct page *start_page, unsigned long size, int node)
{
	unsigned long addr = (unsigned long)start_page;
	unsigned long end = (unsigned long)(start_page + size);
	unsigned long next;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	for (; addr < end; addr = next) {
		next = pmd_addr_end(addr, end);

		pgd = vmemmap_pgd_populate(addr, node);
		if (!pgd)
			return -ENOMEM;

		pud = vmemmap_pud_populate(pgd, addr, node);
		if (!pud)
			return -ENOMEM;

		pmd = pmd_offset(pud, addr);
		if (pmd_none(*pmd)) {
			pte_t entry;
			void *p;

			p = vmemmap_alloc_block(PMD_SIZE, node);
			if (!p)
				return -ENOMEM;

			entry = pfn_pte(__pa(p) >> PAGE_SHIFT,
							PAGE_KERNEL_LARGE);
			set_pmd(pmd, __pmd(pte_val(entry)));

			/* check to see if we have contiguous blocks */
			if (p_end != p || node_start != node) {
				if (p_start)
					printk(KERN_DEBUG " [%lx-%lx] PMD -> [%p-%p] on node %d\n",
						addr_start, addr_end-1, p_start, p_end-1, node_start);
				addr_start = addr;
				node_start = node;
				p_start = p;
			}
			addr_end = addr + PMD_SIZE;
			p_end = p + PMD_SIZE;
		} else {
			vmemmap_verify((pte_t *)pmd, node, addr, next);
		}
	}
	return 0;
}

void __meminit vmemmap_populate_print_last(void)
{
	if (p_start) {
		printk(KERN_DEBUG " [%lx-%lx] PMD -> [%p-%p] on node %d\n",
			addr_start, addr_end-1, p_start, p_end-1, node_start);
		p_start = NULL;
		p_end = NULL;
		node_start = 0;
	}
}
#endif

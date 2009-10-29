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
#include <linux/initrd.h>
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
#include <asm/bios_ebda.h>
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
#include <asm/init.h>

static unsigned long dma_reserve __initdata;

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

pteval_t __supported_pte_mask __read_mostly = ~_PAGE_IOMAP;
EXPORT_SYMBOL_GPL(__supported_pte_mask);

int force_personality32;

/*
 * noexec32=on|off
 * Control non executable heap for 32bit processes.
 * To control the stack too use noexec=off
 *
 * on	PROT_READ does not imply PROT_EXEC for 32-bit processes (default)
 * off	PROT_READ implies PROT_EXEC
 */
static int __init nonx32_setup(char *str)
{
	if (!strcmp(str, "on"))
		force_personality32 &= ~READ_IMPLIES_EXEC;
	else if (!strcmp(str, "off"))
		force_personality32 |= READ_IMPLIES_EXEC;
	return 1;
}
__setup("noexec32=", nonx32_setup);

/*
 * NOTE: This function is marked __ref because it calls __init function
 * (alloc_bootmem_pages). It's safe to do it ONLY when after_bootmem == 0.
 */
static __ref void *spp_getpage(void)
{
	void *ptr;

	if (after_bootmem)
		ptr = (void *) get_zeroed_page(GFP_ATOMIC | __GFP_NOTRACK);
	else
		ptr = alloc_bootmem_pages(PAGE_SIZE);

	if (!ptr || ((unsigned long)ptr & ~PAGE_MASK)) {
		panic("set_pte_phys: cannot allocate page data %s\n",
			after_bootmem ? "after bootmem" : "");
	}

	pr_debug("spp_getpage %p\n", ptr);

	return ptr;
}

static pud_t *fill_pud(pgd_t *pgd, unsigned long vaddr)
{
	if (pgd_none(*pgd)) {
		pud_t *pud = (pud_t *)spp_getpage();
		pgd_populate(&init_mm, pgd, pud);
		if (pud != pud_offset(pgd, 0))
			printk(KERN_ERR "PAGETABLE BUG #00! %p <-> %p\n",
			       pud, pud_offset(pgd, 0));
	}
	return pud_offset(pgd, vaddr);
}

static pmd_t *fill_pmd(pud_t *pud, unsigned long vaddr)
{
	if (pud_none(*pud)) {
		pmd_t *pmd = (pmd_t *) spp_getpage();
		pud_populate(&init_mm, pud, pmd);
		if (pmd != pmd_offset(pud, 0))
			printk(KERN_ERR "PAGETABLE BUG #01! %p <-> %p\n",
			       pmd, pmd_offset(pud, 0));
	}
	return pmd_offset(pud, vaddr);
}

static pte_t *fill_pte(pmd_t *pmd, unsigned long vaddr)
{
	if (pmd_none(*pmd)) {
		pte_t *pte = (pte_t *) spp_getpage();
		pmd_populate_kernel(&init_mm, pmd, pte);
		if (pte != pte_offset_kernel(pmd, 0))
			printk(KERN_ERR "PAGETABLE BUG #02!\n");
	}
	return pte_offset_kernel(pmd, vaddr);
}

void set_pte_vaddr_pud(pud_t *pud_page, unsigned long vaddr, pte_t new_pte)
{
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pud = pud_page + pud_index(vaddr);
	pmd = fill_pmd(pud, vaddr);
	pte = fill_pte(pmd, vaddr);

	set_pte(pte, new_pte);

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

void set_pte_vaddr(unsigned long vaddr, pte_t pteval)
{
	pgd_t *pgd;
	pud_t *pud_page;

	pr_debug("set_pte_vaddr %lx to %lx\n", vaddr, native_pte_val(pteval));

	pgd = pgd_offset_k(vaddr);
	if (pgd_none(*pgd)) {
		printk(KERN_ERR
			"PGD FIXMAP MISSING, it should be setup in head.S!\n");
		return;
	}
	pud_page = (pud_t*)pgd_page_vaddr(*pgd);
	set_pte_vaddr_pud(pud_page, vaddr, pteval);
}

pmd_t * __init populate_extra_pmd(unsigned long vaddr)
{
	pgd_t *pgd;
	pud_t *pud;

	pgd = pgd_offset_k(vaddr);
	pud = fill_pud(pgd, vaddr);
	return fill_pmd(pud, vaddr);
}

pte_t * __init populate_extra_pte(unsigned long vaddr)
{
	pmd_t *pmd;

	pmd = populate_extra_pmd(vaddr);
	return fill_pte(pmd, vaddr);
}

/*
 * Create large page table mappings for a range of physical addresses.
 */
static void __init __init_extra_mapping(unsigned long phys, unsigned long size,
						pgprot_t prot)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	BUG_ON((phys & ~PMD_MASK) || (size & ~PMD_MASK));
	for (; size; phys += PMD_SIZE, size -= PMD_SIZE) {
		pgd = pgd_offset_k((unsigned long)__va(phys));
		if (pgd_none(*pgd)) {
			pud = (pud_t *) spp_getpage();
			set_pgd(pgd, __pgd(__pa(pud) | _KERNPG_TABLE |
						_PAGE_USER));
		}
		pud = pud_offset(pgd, (unsigned long)__va(phys));
		if (pud_none(*pud)) {
			pmd = (pmd_t *) spp_getpage();
			set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE |
						_PAGE_USER));
		}
		pmd = pmd_offset(pud, phys);
		BUG_ON(!pmd_none(*pmd));
		set_pmd(pmd, __pmd(phys | pgprot_val(prot)));
	}
}

void __init init_extra_mapping_wb(unsigned long phys, unsigned long size)
{
	__init_extra_mapping(phys, size, PAGE_KERNEL_LARGE);
}

void __init init_extra_mapping_uc(unsigned long phys, unsigned long size)
{
	__init_extra_mapping(phys, size, PAGE_KERNEL_LARGE_NOCACHE);
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
	unsigned long end = roundup((unsigned long)_end, PMD_SIZE) - 1;
	pmd_t *pmd = level2_kernel_pgt;
	pmd_t *last_pmd = pmd + PTRS_PER_PMD;

	for (; pmd < last_pmd; pmd++, vaddr += PMD_SIZE) {
		if (pmd_none(*pmd))
			continue;
		if (vaddr < (unsigned long) _text || vaddr > end)
			set_pmd(pmd, __pmd(0));
	}
}

static __ref void *alloc_low_page(unsigned long *phys)
{
	unsigned long pfn = e820_table_end++;
	void *adr;

	if (after_bootmem) {
		adr = (void *)get_zeroed_page(GFP_ATOMIC | __GFP_NOTRACK);
		*phys = __pa(adr);

		return adr;
	}

	if (pfn >= e820_table_top)
		panic("alloc_low_page: ran out of memory");

	adr = early_memremap(pfn * PAGE_SIZE, PAGE_SIZE);
	memset(adr, 0, PAGE_SIZE);
	*phys  = pfn * PAGE_SIZE;
	return adr;
}

static __ref void unmap_low_page(void *adr)
{
	if (after_bootmem)
		return;

	early_iounmap(adr, PAGE_SIZE);
}

static unsigned long __meminit
phys_pte_init(pte_t *pte_page, unsigned long addr, unsigned long end,
	      pgprot_t prot)
{
	unsigned pages = 0;
	unsigned long last_map_addr = end;
	int i;

	pte_t *pte = pte_page + pte_index(addr);

	for(i = pte_index(addr); i < PTRS_PER_PTE; i++, addr += PAGE_SIZE, pte++) {

		if (addr >= end) {
			if (!after_bootmem) {
				for(; i < PTRS_PER_PTE; i++, pte++)
					set_pte(pte, __pte(0));
			}
			break;
		}

		/*
		 * We will re-use the existing mapping.
		 * Xen for example has some special requirements, like mapping
		 * pagetable pages as RO. So assume someone who pre-setup
		 * these mappings are more intelligent.
		 */
		if (pte_val(*pte)) {
			pages++;
			continue;
		}

		if (0)
			printk("   pte=%p addr=%lx pte=%016lx\n",
			       pte, addr, pfn_pte(addr >> PAGE_SHIFT, PAGE_KERNEL).pte);
		pages++;
		set_pte(pte, pfn_pte(addr >> PAGE_SHIFT, prot));
		last_map_addr = (addr & PAGE_MASK) + PAGE_SIZE;
	}

	update_page_count(PG_LEVEL_4K, pages);

	return last_map_addr;
}

static unsigned long __meminit
phys_pte_update(pmd_t *pmd, unsigned long address, unsigned long end,
		pgprot_t prot)
{
	pte_t *pte = (pte_t *)pmd_page_vaddr(*pmd);

	return phys_pte_init(pte, address, end, prot);
}

static unsigned long __meminit
phys_pmd_init(pmd_t *pmd_page, unsigned long address, unsigned long end,
	      unsigned long page_size_mask, pgprot_t prot)
{
	unsigned long pages = 0;
	unsigned long last_map_addr = end;

	int i = pmd_index(address);

	for (; i < PTRS_PER_PMD; i++, address += PMD_SIZE) {
		unsigned long pte_phys;
		pmd_t *pmd = pmd_page + pmd_index(address);
		pte_t *pte;
		pgprot_t new_prot = prot;

		if (address >= end) {
			if (!after_bootmem) {
				for (; i < PTRS_PER_PMD; i++, pmd++)
					set_pmd(pmd, __pmd(0));
			}
			break;
		}

		if (pmd_val(*pmd)) {
			if (!pmd_large(*pmd)) {
				spin_lock(&init_mm.page_table_lock);
				last_map_addr = phys_pte_update(pmd, address,
								end, prot);
				spin_unlock(&init_mm.page_table_lock);
				continue;
			}
			/*
			 * If we are ok with PG_LEVEL_2M mapping, then we will
			 * use the existing mapping,
			 *
			 * Otherwise, we will split the large page mapping but
			 * use the same existing protection bits except for
			 * large page, so that we don't violate Intel's TLB
			 * Application note (317080) which says, while changing
			 * the page sizes, new and old translations should
			 * not differ with respect to page frame and
			 * attributes.
			 */
			if (page_size_mask & (1 << PG_LEVEL_2M)) {
				pages++;
				continue;
			}
			new_prot = pte_pgprot(pte_clrhuge(*(pte_t *)pmd));
		}

		if (page_size_mask & (1<<PG_LEVEL_2M)) {
			pages++;
			spin_lock(&init_mm.page_table_lock);
			set_pte((pte_t *)pmd,
				pfn_pte(address >> PAGE_SHIFT,
					__pgprot(pgprot_val(prot) | _PAGE_PSE)));
			spin_unlock(&init_mm.page_table_lock);
			last_map_addr = (address & PMD_MASK) + PMD_SIZE;
			continue;
		}

		pte = alloc_low_page(&pte_phys);
		last_map_addr = phys_pte_init(pte, address, end, new_prot);
		unmap_low_page(pte);

		spin_lock(&init_mm.page_table_lock);
		pmd_populate_kernel(&init_mm, pmd, __va(pte_phys));
		spin_unlock(&init_mm.page_table_lock);
	}
	update_page_count(PG_LEVEL_2M, pages);
	return last_map_addr;
}

static unsigned long __meminit
phys_pmd_update(pud_t *pud, unsigned long address, unsigned long end,
		unsigned long page_size_mask, pgprot_t prot)
{
	pmd_t *pmd = pmd_offset(pud, 0);
	unsigned long last_map_addr;

	last_map_addr = phys_pmd_init(pmd, address, end, page_size_mask, prot);
	__flush_tlb_all();
	return last_map_addr;
}

static unsigned long __meminit
phys_pud_init(pud_t *pud_page, unsigned long addr, unsigned long end,
			 unsigned long page_size_mask)
{
	unsigned long pages = 0;
	unsigned long last_map_addr = end;
	int i = pud_index(addr);

	for (; i < PTRS_PER_PUD; i++, addr = (addr & PUD_MASK) + PUD_SIZE) {
		unsigned long pmd_phys;
		pud_t *pud = pud_page + pud_index(addr);
		pmd_t *pmd;
		pgprot_t prot = PAGE_KERNEL;

		if (addr >= end)
			break;

		if (!after_bootmem &&
				!e820_any_mapped(addr, addr+PUD_SIZE, 0)) {
			set_pud(pud, __pud(0));
			continue;
		}

		if (pud_val(*pud)) {
			if (!pud_large(*pud)) {
				last_map_addr = phys_pmd_update(pud, addr, end,
							 page_size_mask, prot);
				continue;
			}
			/*
			 * If we are ok with PG_LEVEL_1G mapping, then we will
			 * use the existing mapping.
			 *
			 * Otherwise, we will split the gbpage mapping but use
			 * the same existing protection  bits except for large
			 * page, so that we don't violate Intel's TLB
			 * Application note (317080) which says, while changing
			 * the page sizes, new and old translations should
			 * not differ with respect to page frame and
			 * attributes.
			 */
			if (page_size_mask & (1 << PG_LEVEL_1G)) {
				pages++;
				continue;
			}
			prot = pte_pgprot(pte_clrhuge(*(pte_t *)pud));
		}

		if (page_size_mask & (1<<PG_LEVEL_1G)) {
			pages++;
			spin_lock(&init_mm.page_table_lock);
			set_pte((pte_t *)pud,
				pfn_pte(addr >> PAGE_SHIFT, PAGE_KERNEL_LARGE));
			spin_unlock(&init_mm.page_table_lock);
			last_map_addr = (addr & PUD_MASK) + PUD_SIZE;
			continue;
		}

		pmd = alloc_low_page(&pmd_phys);
		last_map_addr = phys_pmd_init(pmd, addr, end, page_size_mask,
					      prot);
		unmap_low_page(pmd);

		spin_lock(&init_mm.page_table_lock);
		pud_populate(&init_mm, pud, __va(pmd_phys));
		spin_unlock(&init_mm.page_table_lock);
	}
	__flush_tlb_all();

	update_page_count(PG_LEVEL_1G, pages);

	return last_map_addr;
}

static unsigned long __meminit
phys_pud_update(pgd_t *pgd, unsigned long addr, unsigned long end,
		 unsigned long page_size_mask)
{
	pud_t *pud;

	pud = (pud_t *)pgd_page_vaddr(*pgd);

	return phys_pud_init(pud, addr, end, page_size_mask);
}

unsigned long __meminit
kernel_physical_mapping_init(unsigned long start,
			     unsigned long end,
			     unsigned long page_size_mask)
{

	unsigned long next, last_map_addr = end;

	start = (unsigned long)__va(start);
	end = (unsigned long)__va(end);

	for (; start < end; start = next) {
		pgd_t *pgd = pgd_offset_k(start);
		unsigned long pud_phys;
		pud_t *pud;

		next = (start + PGDIR_SIZE) & PGDIR_MASK;
		if (next > end)
			next = end;

		if (pgd_val(*pgd)) {
			last_map_addr = phys_pud_update(pgd, __pa(start),
						 __pa(end), page_size_mask);
			continue;
		}

		pud = alloc_low_page(&pud_phys);
		last_map_addr = phys_pud_init(pud, __pa(start), __pa(next),
						 page_size_mask);
		unmap_low_page(pud);

		spin_lock(&init_mm.page_table_lock);
		pgd_populate(&init_mm, pgd, __va(pud_phys));
		spin_unlock(&init_mm.page_table_lock);
	}
	__flush_tlb_all();

	return last_map_addr;
}

#ifndef CONFIG_NUMA
void __init initmem_init(unsigned long start_pfn, unsigned long end_pfn,
				int acpi, int k8)
{
	unsigned long bootmap_size, bootmap;

	bootmap_size = bootmem_bootmap_pages(end_pfn)<<PAGE_SHIFT;
	bootmap = find_e820_area(0, end_pfn<<PAGE_SHIFT, bootmap_size,
				 PAGE_SIZE);
	if (bootmap == -1L)
		panic("Cannot find bootmem map of size %ld\n", bootmap_size);
	/* don't touch min_low_pfn */
	bootmap_size = init_bootmem_node(NODE_DATA(0), bootmap >> PAGE_SHIFT,
					 0, end_pfn);
	e820_register_active_regions(0, start_pfn, end_pfn);
	free_bootmem_with_active_regions(0, end_pfn);
	early_res_to_bootmem(0, end_pfn<<PAGE_SHIFT);
	reserve_bootmem(bootmap, bootmap_size, BOOTMEM_DEFAULT);
}
#endif

void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] = MAX_DMA_PFN;
	max_zone_pfns[ZONE_DMA32] = MAX_DMA32_PFN;
	max_zone_pfns[ZONE_NORMAL] = max_pfn;

	sparse_memory_present_with_active_regions(MAX_NUMNODES);
	sparse_init();

	/*
	 * clear the default setting with node 0
	 * note: don't use nodes_clear here, that is really clearing when
	 *	 numa support is not compiled in, and later node_set_state
	 *	 will not set it back.
	 */
	node_clear_state(0, N_NORMAL_MEMORY);

	free_area_init_nodes(max_zone_pfns);
}

/*
 * Memory hotplug specific functions
 */
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

	last_mapped_pfn = init_memory_mapping(start, start + size);
	if (last_mapped_pfn > max_pfn_mapped)
		max_pfn_mapped = last_mapped_pfn;

	ret = __add_pages(nid, zone, start_pfn, nr_pages);
	WARN_ON_ONCE(ret);

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

static struct kcore_list kcore_vsyscall;

void __init mem_init(void)
{
	long codesize, reservedpages, datasize, initsize;
	unsigned long absent_pages;

	pci_iommu_alloc();

	/* clear_bss() already clear the empty_zero_page */

	reservedpages = 0;

	/* this will put all low memory onto the freelists */
#ifdef CONFIG_NUMA
	totalram_pages = numa_free_all_bootmem();
#else
	totalram_pages = free_all_bootmem();
#endif

	absent_pages = absent_pages_in_range(0, max_pfn);
	reservedpages = max_pfn - totalram_pages - absent_pages;
	after_bootmem = 1;

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	/* Register memory areas for /proc/kcore */
	kclist_add(&kcore_vsyscall, (void *)VSYSCALL_START,
			 VSYSCALL_END - VSYSCALL_START, KCORE_OTHER);

	printk(KERN_INFO "Memory: %luk/%luk available (%ldk kernel code, "
			 "%ldk absent, %ldk reserved, %ldk data, %ldk init)\n",
		nr_free_pages() << (PAGE_SHIFT-10),
		max_pfn << (PAGE_SHIFT-10),
		codesize >> 10,
		absent_pages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		initsize >> 10);
}

#ifdef CONFIG_DEBUG_RODATA
const int rodata_test_data = 0xC3;
EXPORT_SYMBOL_GPL(rodata_test_data);

int kernel_set_to_readonly;

void set_kernel_text_rw(void)
{
	unsigned long start = PFN_ALIGN(_text);
	unsigned long end = PFN_ALIGN(__stop___ex_table);

	if (!kernel_set_to_readonly)
		return;

	pr_debug("Set kernel text: %lx - %lx for read write\n",
		 start, end);

	/*
	 * Make the kernel identity mapping for text RW. Kernel text
	 * mapping will always be RO. Refer to the comment in
	 * static_protections() in pageattr.c
	 */
	set_memory_rw(start, (end - start) >> PAGE_SHIFT);
}

void set_kernel_text_ro(void)
{
	unsigned long start = PFN_ALIGN(_text);
	unsigned long end = PFN_ALIGN(__stop___ex_table);

	if (!kernel_set_to_readonly)
		return;

	pr_debug("Set kernel text: %lx - %lx for read only\n",
		 start, end);

	/*
	 * Set the kernel identity mapping for text RO.
	 */
	set_memory_ro(start, (end - start) >> PAGE_SHIFT);
}

void mark_rodata_ro(void)
{
	unsigned long start = PFN_ALIGN(_text);
	unsigned long rodata_start =
		((unsigned long)__start_rodata + PAGE_SIZE - 1) & PAGE_MASK;
	unsigned long end = (unsigned long) &__end_rodata_hpage_align;
	unsigned long text_end = PAGE_ALIGN((unsigned long) &__stop___ex_table);
	unsigned long rodata_end = PAGE_ALIGN((unsigned long) &__end_rodata);
	unsigned long data_start = (unsigned long) &_sdata;

	printk(KERN_INFO "Write protecting the kernel read-only data: %luk\n",
	       (end - start) >> 10);
	set_memory_ro(start, (end - start) >> PAGE_SHIFT);

	kernel_set_to_readonly = 1;

	/*
	 * The rodata section (but not the kernel text!) should also be
	 * not-executable.
	 */
	set_memory_nx(rodata_start, (end - rodata_start) >> PAGE_SHIFT);

	rodata_test();

#ifdef CONFIG_CPA_DEBUG
	printk(KERN_INFO "Testing CPA: undo %lx-%lx\n", start, end);
	set_memory_rw(start, (end-start) >> PAGE_SHIFT);

	printk(KERN_INFO "Testing CPA: again\n");
	set_memory_ro(start, (end-start) >> PAGE_SHIFT);
#endif

	free_init_pages("unused kernel memory",
			(unsigned long) page_address(virt_to_page(text_end)),
			(unsigned long)
				 page_address(virt_to_page(rodata_start)));
	free_init_pages("unused kernel memory",
			(unsigned long) page_address(virt_to_page(rodata_end)),
			(unsigned long) page_address(virt_to_page(data_start)));
}

#endif

int __init reserve_bootmem_generic(unsigned long phys, unsigned long len,
				   int flags)
{
#ifdef CONFIG_NUMA
	int nid, next_nid;
	int ret;
#endif
	unsigned long pfn = phys >> PAGE_SHIFT;

	if (pfn >= max_pfn) {
		/*
		 * This can happen with kdump kernels when accessing
		 * firmware tables:
		 */
		if (pfn < max_pfn_mapped)
			return -EFAULT;

		printk(KERN_ERR "reserve_bootmem: illegal reserve %lx %lu\n",
				phys, len);
		return -EFAULT;
	}

	/* Should check here against the e820 map to avoid double free */
#ifdef CONFIG_NUMA
	nid = phys_to_nid(phys);
	next_nid = phys_to_nid(phys + len - 1);
	if (nid == next_nid)
		ret = reserve_bootmem_node(NODE_DATA(nid), phys, len, flags);
	else
		ret = reserve_bootmem(phys, len, flags);

	if (ret != 0)
		return ret;

#else
	reserve_bootmem(phys, len, flags);
#endif

	if (phys+len <= MAX_DMA_PFN*PAGE_SIZE) {
		dma_reserve += len / PAGE_SIZE;
		set_dma_reserve(dma_reserve);
	}

	return 0;
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
		void *p = NULL;

		pgd = vmemmap_pgd_populate(addr, node);
		if (!pgd)
			return -ENOMEM;

		pud = vmemmap_pud_populate(pgd, addr, node);
		if (!pud)
			return -ENOMEM;

		if (!cpu_has_pse) {
			next = (addr + PAGE_SIZE) & PAGE_MASK;
			pmd = vmemmap_pmd_populate(pud, addr, node);

			if (!pmd)
				return -ENOMEM;

			p = vmemmap_pte_populate(pmd, addr, node);

			if (!p)
				return -ENOMEM;

			addr_end = addr + PAGE_SIZE;
			p_end = p + PAGE_SIZE;
		} else {
			next = pmd_addr_end(addr, end);

			pmd = pmd_offset(pud, addr);
			if (pmd_none(*pmd)) {
				pte_t entry;

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
			} else
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

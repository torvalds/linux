/*
 *  linux/arch/x86_64/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 *  Copyright (C) 2002,2003 Andi Kleen <ak@suse.de>
 */

#include <linux/config.h>
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
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/memory_hotplug.h>

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
#include <asm/dma-mapping.h>
#include <asm/swiotlb.h>

#ifndef Dprintk
#define Dprintk(x...)
#endif

struct dma_mapping_ops* dma_ops;
EXPORT_SYMBOL(dma_ops);

static unsigned long dma_reserve __initdata;

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

/*
 * NOTE: pagetable_init alloc all the fixmap pagetables contiguous on the
 * physical space so we can cache the place of the first one and move
 * around without checking the pgd every time.
 */

void show_mem(void)
{
	long i, total = 0, reserved = 0;
	long shared = 0, cached = 0;
	pg_data_t *pgdat;
	struct page *page;

	printk(KERN_INFO "Mem-info:\n");
	show_free_areas();
	printk(KERN_INFO "Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));

	for_each_online_pgdat(pgdat) {
               for (i = 0; i < pgdat->node_spanned_pages; ++i) {
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
	printk(KERN_INFO "%lu pages of RAM\n", total);
	printk(KERN_INFO "%lu reserved pages\n",reserved);
	printk(KERN_INFO "%lu pages shared\n",shared);
	printk(KERN_INFO "%lu pages swap cached\n",cached);
}

/* References to section boundaries */

int after_bootmem;

static __init void *spp_getpage(void)
{ 
	void *ptr;
	if (after_bootmem)
		ptr = (void *) get_zeroed_page(GFP_ATOMIC); 
	else
		ptr = alloc_bootmem_pages(PAGE_SIZE);
	if (!ptr || ((unsigned long)ptr & ~PAGE_MASK))
		panic("set_pte_phys: cannot allocate page data %s\n", after_bootmem?"after bootmem":"");

	Dprintk("spp_getpage %p\n", ptr);
	return ptr;
} 

static __init void set_pte_phys(unsigned long vaddr,
			 unsigned long phys, pgprot_t prot)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte, new_pte;

	Dprintk("set_pte_phys %lx to %lx\n", vaddr, phys);

	pgd = pgd_offset_k(vaddr);
	if (pgd_none(*pgd)) {
		printk("PGD FIXMAP MISSING, it should be setup in head.S!\n");
		return;
	}
	pud = pud_offset(pgd, vaddr);
	if (pud_none(*pud)) {
		pmd = (pmd_t *) spp_getpage(); 
		set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE | _PAGE_USER));
		if (pmd != pmd_offset(pud, 0)) {
			printk("PAGETABLE BUG #01! %p <-> %p\n", pmd, pmd_offset(pud,0));
			return;
		}
	}
	pmd = pmd_offset(pud, vaddr);
	if (pmd_none(*pmd)) {
		pte = (pte_t *) spp_getpage();
		set_pmd(pmd, __pmd(__pa(pte) | _KERNPG_TABLE | _PAGE_USER));
		if (pte != pte_offset_kernel(pmd, 0)) {
			printk("PAGETABLE BUG #02!\n");
			return;
		}
	}
	new_pte = pfn_pte(phys >> PAGE_SHIFT, prot);

	pte = pte_offset_kernel(pmd, vaddr);
	if (!pte_none(*pte) &&
	    pte_val(*pte) != (pte_val(new_pte) & __supported_pte_mask))
		pte_ERROR(*pte);
	set_pte(pte, new_pte);

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

/* NOTE: this is meant to be run only at boot */
void __init 
__set_fixmap (enum fixed_addresses idx, unsigned long phys, pgprot_t prot)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		printk("Invalid __set_fixmap\n");
		return;
	}
	set_pte_phys(address, phys, prot);
}

unsigned long __initdata table_start, table_end; 

extern pmd_t temp_boot_pmds[]; 

static  struct temp_map { 
	pmd_t *pmd;
	void  *address; 
	int    allocated; 
} temp_mappings[] __initdata = { 
	{ &temp_boot_pmds[0], (void *)(40UL * 1024 * 1024) },
	{ &temp_boot_pmds[1], (void *)(42UL * 1024 * 1024) }, 
	{}
}; 

static __meminit void *alloc_low_page(int *index, unsigned long *phys)
{ 
	struct temp_map *ti;
	int i; 
	unsigned long pfn = table_end++, paddr; 
	void *adr;

	if (after_bootmem) {
		adr = (void *)get_zeroed_page(GFP_ATOMIC);
		*phys = __pa(adr);
		return adr;
	}

	if (pfn >= end_pfn) 
		panic("alloc_low_page: ran out of memory"); 
	for (i = 0; temp_mappings[i].allocated; i++) {
		if (!temp_mappings[i].pmd) 
			panic("alloc_low_page: ran out of temp mappings"); 
	} 
	ti = &temp_mappings[i];
	paddr = (pfn << PAGE_SHIFT) & PMD_MASK; 
	set_pmd(ti->pmd, __pmd(paddr | _KERNPG_TABLE | _PAGE_PSE)); 
	ti->allocated = 1; 
	__flush_tlb(); 	       
	adr = ti->address + ((pfn << PAGE_SHIFT) & ~PMD_MASK); 
	memset(adr, 0, PAGE_SIZE);
	*index = i; 
	*phys  = pfn * PAGE_SIZE;  
	return adr; 
} 

static __meminit void unmap_low_page(int i)
{ 
	struct temp_map *ti;

	if (after_bootmem)
		return;

	ti = &temp_mappings[i];
	set_pmd(ti->pmd, __pmd(0));
	ti->allocated = 0; 
} 

/* Must run before zap_low_mappings */
__init void *early_ioremap(unsigned long addr, unsigned long size)
{
	unsigned long map = round_down(addr, LARGE_PAGE_SIZE); 

	/* actually usually some more */
	if (size >= LARGE_PAGE_SIZE) { 
		printk("SMBIOS area too long %lu\n", size);
		return NULL;
	}
	set_pmd(temp_mappings[0].pmd,  __pmd(map | _KERNPG_TABLE | _PAGE_PSE));
	map += LARGE_PAGE_SIZE;
	set_pmd(temp_mappings[1].pmd,  __pmd(map | _KERNPG_TABLE | _PAGE_PSE));
	__flush_tlb();
	return temp_mappings[0].address + (addr & (LARGE_PAGE_SIZE-1));
}

/* To avoid virtual aliases later */
__init void early_iounmap(void *addr, unsigned long size)
{
	if ((void *)round_down((unsigned long)addr, LARGE_PAGE_SIZE) != temp_mappings[0].address)
		printk("early_iounmap: bad address %p\n", addr);
	set_pmd(temp_mappings[0].pmd, __pmd(0));
	set_pmd(temp_mappings[1].pmd, __pmd(0));
	__flush_tlb();
}

static void __meminit
phys_pmd_init(pmd_t *pmd, unsigned long address, unsigned long end)
{
	int i;

	for (i = 0; i < PTRS_PER_PMD; pmd++, i++, address += PMD_SIZE) {
		unsigned long entry;

		if (address > end) {
			for (; i < PTRS_PER_PMD; i++, pmd++)
				set_pmd(pmd, __pmd(0));
			break;
		}
		entry = _PAGE_NX|_PAGE_PSE|_KERNPG_TABLE|_PAGE_GLOBAL|address;
		entry &= __supported_pte_mask;
		set_pmd(pmd, __pmd(entry));
	}
}

static void __meminit
phys_pmd_update(pud_t *pud, unsigned long address, unsigned long end)
{
	pmd_t *pmd = pmd_offset(pud, (unsigned long)__va(address));

	if (pmd_none(*pmd)) {
		spin_lock(&init_mm.page_table_lock);
		phys_pmd_init(pmd, address, end);
		spin_unlock(&init_mm.page_table_lock);
		__flush_tlb_all();
	}
}

static void __meminit phys_pud_init(pud_t *pud, unsigned long address, unsigned long end)
{ 
	long i = pud_index(address);

	pud = pud + i;

	if (after_bootmem && pud_val(*pud)) {
		phys_pmd_update(pud, address, end);
		return;
	}

	for (; i < PTRS_PER_PUD; pud++, i++) {
		int map; 
		unsigned long paddr, pmd_phys;
		pmd_t *pmd;

		paddr = (address & PGDIR_MASK) + i*PUD_SIZE;
		if (paddr >= end)
			break;

		if (!after_bootmem && !e820_mapped(paddr, paddr+PUD_SIZE, 0)) {
			set_pud(pud, __pud(0)); 
			continue;
		} 

		pmd = alloc_low_page(&map, &pmd_phys);
		spin_lock(&init_mm.page_table_lock);
		set_pud(pud, __pud(pmd_phys | _KERNPG_TABLE));
		phys_pmd_init(pmd, paddr, end);
		spin_unlock(&init_mm.page_table_lock);
		unmap_low_page(map);
	}
	__flush_tlb();
} 

static void __init find_early_table_space(unsigned long end)
{
	unsigned long puds, pmds, tables, start;

	puds = (end + PUD_SIZE - 1) >> PUD_SHIFT;
	pmds = (end + PMD_SIZE - 1) >> PMD_SHIFT;
	tables = round_up(puds * sizeof(pud_t), PAGE_SIZE) +
		 round_up(pmds * sizeof(pmd_t), PAGE_SIZE);

 	/* RED-PEN putting page tables only on node 0 could
 	   cause a hotspot and fill up ZONE_DMA. The page tables
 	   need roughly 0.5KB per GB. */
 	start = 0x8000;
 	table_start = find_e820_area(start, end, tables);
	if (table_start == -1UL)
		panic("Cannot find space for the kernel page tables");

	table_start >>= PAGE_SHIFT;
	table_end = table_start;

	early_printk("kernel direct mapping tables up to %lx @ %lx-%lx\n",
		end, table_start << PAGE_SHIFT, table_end << PAGE_SHIFT);
}

/* Setup the direct mapping of the physical memory at PAGE_OFFSET.
   This runs before bootmem is initialized and gets pages directly from the 
   physical memory. To access them they are temporarily mapped. */
void __meminit init_memory_mapping(unsigned long start, unsigned long end)
{ 
	unsigned long next; 

	Dprintk("init_memory_mapping\n");

	/* 
	 * Find space for the kernel direct mapping tables.
	 * Later we should allocate these tables in the local node of the memory
	 * mapped.  Unfortunately this is done currently before the nodes are 
	 * discovered.
	 */
	if (!after_bootmem)
		find_early_table_space(end);

	start = (unsigned long)__va(start);
	end = (unsigned long)__va(end);

	for (; start < end; start = next) {
		int map;
		unsigned long pud_phys; 
		pgd_t *pgd = pgd_offset_k(start);
		pud_t *pud;

		if (after_bootmem)
			pud = pud_offset_k(pgd, start & PGDIR_MASK);
		else
			pud = alloc_low_page(&map, &pud_phys);

		next = start + PGDIR_SIZE;
		if (next > end) 
			next = end; 
		phys_pud_init(pud, __pa(start), __pa(next));
		if (!after_bootmem)
			set_pgd(pgd_offset_k(start), mk_kernel_pgd(pud_phys));
		unmap_low_page(map);   
	} 

	if (!after_bootmem)
		asm volatile("movq %%cr4,%0" : "=r" (mmu_cr4_features));
	__flush_tlb_all();
}

void __cpuinit zap_low_mappings(int cpu)
{
	if (cpu == 0) {
		pgd_t *pgd = pgd_offset_k(0UL);
		pgd_clear(pgd);
	} else {
		/*
		 * For AP's, zap the low identity mappings by changing the cr3
		 * to init_level4_pgt and doing local flush tlb all
		 */
		asm volatile("movq %0,%%cr3" :: "r" (__pa_symbol(&init_level4_pgt)));
	}
	__flush_tlb_all();
}

/* Compute zone sizes for the DMA and DMA32 zones in a node. */
__init void
size_zones(unsigned long *z, unsigned long *h,
	   unsigned long start_pfn, unsigned long end_pfn)
{
 	int i;
 	unsigned long w;

 	for (i = 0; i < MAX_NR_ZONES; i++)
 		z[i] = 0;

 	if (start_pfn < MAX_DMA_PFN)
 		z[ZONE_DMA] = MAX_DMA_PFN - start_pfn;
 	if (start_pfn < MAX_DMA32_PFN) {
 		unsigned long dma32_pfn = MAX_DMA32_PFN;
 		if (dma32_pfn > end_pfn)
 			dma32_pfn = end_pfn;
 		z[ZONE_DMA32] = dma32_pfn - start_pfn;
 	}
 	z[ZONE_NORMAL] = end_pfn - start_pfn;

 	/* Remove lower zones from higher ones. */
 	w = 0;
 	for (i = 0; i < MAX_NR_ZONES; i++) {
 		if (z[i])
 			z[i] -= w;
 	        w += z[i];
	}

	/* Compute holes */
	w = start_pfn;
	for (i = 0; i < MAX_NR_ZONES; i++) {
		unsigned long s = w;
		w += z[i];
		h[i] = e820_hole_size(s, w);
	}

	/* Add the space pace needed for mem_map to the holes too. */
	for (i = 0; i < MAX_NR_ZONES; i++)
		h[i] += (z[i] * sizeof(struct page)) / PAGE_SIZE;

	/* The 16MB DMA zone has the kernel and other misc mappings.
 	   Account them too */
	if (h[ZONE_DMA]) {
		h[ZONE_DMA] += dma_reserve;
		if (h[ZONE_DMA] >= z[ZONE_DMA]) {
			printk(KERN_WARNING
				"Kernel too large and filling up ZONE_DMA?\n");
			h[ZONE_DMA] = z[ZONE_DMA];
		}
	}
}

#ifndef CONFIG_NUMA
void __init paging_init(void)
{
	unsigned long zones[MAX_NR_ZONES], holes[MAX_NR_ZONES];

	memory_present(0, 0, end_pfn);
	sparse_init();
	size_zones(zones, holes, 0, end_pfn);
	free_area_init_node(0, NODE_DATA(0), zones,
			    __pa(PAGE_OFFSET) >> PAGE_SHIFT, holes);
}
#endif

/* Unmap a kernel mapping if it exists. This is useful to avoid prefetches
   from the CPU leading to inconsistent cache lines. address and size
   must be aligned to 2MB boundaries. 
   Does nothing when the mapping doesn't exist. */
void __init clear_kernel_mapping(unsigned long address, unsigned long size) 
{
	unsigned long end = address + size;

	BUG_ON(address & ~LARGE_PAGE_MASK);
	BUG_ON(size & ~LARGE_PAGE_MASK); 
	
	for (; address < end; address += LARGE_PAGE_SIZE) { 
		pgd_t *pgd = pgd_offset_k(address);
		pud_t *pud;
		pmd_t *pmd;
		if (pgd_none(*pgd))
			continue;
		pud = pud_offset(pgd, address);
		if (pud_none(*pud))
			continue; 
		pmd = pmd_offset(pud, address);
		if (!pmd || pmd_none(*pmd))
			continue; 
		if (0 == (pmd_val(*pmd) & _PAGE_PSE)) { 
			/* Could handle this, but it should not happen currently. */
			printk(KERN_ERR 
	       "clear_kernel_mapping: mapping has been split. will leak memory\n"); 
			pmd_ERROR(*pmd); 
		}
		set_pmd(pmd, __pmd(0)); 		
	}
	__flush_tlb_all();
} 

/*
 * Memory hotplug specific functions
 */
#if defined(CONFIG_ACPI_HOTPLUG_MEMORY) || defined(CONFIG_ACPI_HOTPLUG_MEMORY_MODULE)

void online_page(struct page *page)
{
	ClearPageReserved(page);
	init_page_count(page);
	__free_page(page);
	totalram_pages++;
	num_physpages++;
}

#ifndef CONFIG_MEMORY_HOTPLUG
/*
 * Memory Hotadd without sparsemem. The mem_maps have been allocated in advance,
 * just online the pages.
 */
int __add_pages(struct zone *z, unsigned long start_pfn, unsigned long nr_pages)
{
	int err = -EIO;
	unsigned long pfn;
	unsigned long total = 0, mem = 0;
	for (pfn = start_pfn; pfn < start_pfn + nr_pages; pfn++) {
		unsigned long addr = pfn << PAGE_SHIFT;
		if (pfn_valid(pfn) && e820_mapped(addr, addr+1, E820_RAM)) {
			online_page(pfn_to_page(pfn));
			err = 0;
			mem++;
		}
		total++;
	}
	if (!err) {
		z->spanned_pages += total;
		z->present_pages += mem;
		z->zone_pgdat->node_spanned_pages += total;
		z->zone_pgdat->node_present_pages += mem;
	}
	return err;
}
#endif

/*
 * Memory is added always to NORMAL zone. This means you will never get
 * additional DMA/DMA32 memory.
 */
int add_memory(u64 start, u64 size)
{
	struct pglist_data *pgdat = NODE_DATA(0);
	struct zone *zone = pgdat->node_zones + MAX_NR_ZONES-2;
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int ret;

	ret = __add_pages(zone, start_pfn, nr_pages);
	if (ret)
		goto error;

	init_memory_mapping(start, (start + size -1));

	return ret;
error:
	printk("%s: Problem encountered in __add_pages!\n", __func__);
	return ret;
}
EXPORT_SYMBOL_GPL(add_memory);

int remove_memory(u64 start, u64 size)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(remove_memory);

#endif

static struct kcore_list kcore_mem, kcore_vmalloc, kcore_kernel, kcore_modules,
			 kcore_vsyscall;

void __init mem_init(void)
{
	long codesize, reservedpages, datasize, initsize;

#ifdef CONFIG_SWIOTLB
	pci_swiotlb_init();
#endif
	no_iommu_init();

	/* How many end-of-memory variables you have, grandma! */
	max_low_pfn = end_pfn;
	max_pfn = end_pfn;
	num_physpages = end_pfn;
	high_memory = (void *) __va(end_pfn * PAGE_SIZE);

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	reservedpages = 0;

	/* this will put all low memory onto the freelists */
#ifdef CONFIG_NUMA
	totalram_pages = numa_free_all_bootmem();
#else
	totalram_pages = free_all_bootmem();
#endif
	reservedpages = end_pfn - totalram_pages - e820_hole_size(0, end_pfn);

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

	printk("Memory: %luk/%luk available (%ldk kernel code, %ldk reserved, %ldk data, %ldk init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		end_pfn << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		initsize >> 10);

#ifdef CONFIG_SMP
	/*
	 * Sync boot_level4_pgt mappings with the init_level4_pgt
	 * except for the low identity mappings which are already zapped
	 * in init_level4_pgt. This sync-up is essential for AP's bringup
	 */
	memcpy(boot_level4_pgt+1, init_level4_pgt+1, (PTRS_PER_PGD-1)*sizeof(pgd_t));
#endif
}

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		memset((void *)(addr & ~(PAGE_SIZE-1)), 0xcc, PAGE_SIZE); 
		free_page(addr);
		totalram_pages++;
	}
	memset(__initdata_begin, 0xba, __initdata_end - __initdata_begin);
	printk ("Freeing unused kernel memory: %luk freed\n", (__init_end - __init_begin) >> 10);
}

#ifdef CONFIG_DEBUG_RODATA

extern char __start_rodata, __end_rodata;
void mark_rodata_ro(void)
{
	unsigned long addr = (unsigned long)&__start_rodata;

	for (; addr < (unsigned long)&__end_rodata; addr += PAGE_SIZE)
		change_page_attr_addr(addr, 1, PAGE_KERNEL_RO);

	printk ("Write protecting the kernel read-only data: %luk\n",
			(&__end_rodata - &__start_rodata) >> 10);

	/*
	 * change_page_attr_addr() requires a global_flush_tlb() call after it.
	 * We do this after the printk so that if something went wrong in the
	 * change, the printk gets out at least to give a better debug hint
	 * of who is the culprit.
	 */
	global_flush_tlb();
}
#endif

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start >= end)
		return;
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		init_page_count(virt_to_page(start));
		free_page(start);
		totalram_pages++;
	}
}
#endif

void __init reserve_bootmem_generic(unsigned long phys, unsigned len) 
{ 
	/* Should check here against the e820 map to avoid double free */ 
#ifdef CONFIG_NUMA
	int nid = phys_to_nid(phys);
  	reserve_bootmem_node(NODE_DATA(nid), phys, len);
#else       		
	reserve_bootmem(phys, len);    
#endif
	if (phys+len <= MAX_DMA_PFN*PAGE_SIZE)
		dma_reserve += len / PAGE_SIZE;
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

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>

extern int exception_trace, page_fault_trace;

static ctl_table debug_table2[] = {
	{ 99, "exception-trace", &exception_trace, sizeof(int), 0644, NULL,
	  proc_dointvec },
	{ 0, }
}; 

static ctl_table debug_root_table2[] = { 
	{ .ctl_name = CTL_DEBUG, .procname = "debug", .mode = 0555, 
	   .child = debug_table2 }, 
	{ 0 }, 
}; 

static __init int x8664_sysctl_init(void)
{ 
	register_sysctl_table(debug_root_table2, 1);
	return 0;
}
__initcall(x8664_sysctl_init);
#endif

/* A pseudo VMAs to allow ptrace access for the vsyscall page.   This only
   covers the 64bit vsyscall page now. 32bit has a real VMA now and does
   not need special handling anymore. */

static struct vm_area_struct gate_vma = {
	.vm_start = VSYSCALL_START,
	.vm_end = VSYSCALL_END,
	.vm_page_prot = PAGE_READONLY
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

/* Use this when you have no reliable task/vma, typically from interrupt
 * context.  It is less reliable than using the task's vma and may give
 * false positives.
 */
int in_gate_area_no_task(unsigned long addr)
{
	return (addr >= VSYSCALL_START) && (addr < VSYSCALL_END);
}

/*
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Dave Engebretsen <engebret@us.ibm.com>
 *      Rework for PPC64 port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/idr.h>
#include <linux/nodemask.h>
#include <linux/module.h>

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/abs_addr.h>
#include <asm/prom.h>
#include <asm/lmb.h>
#include <asm/rtas.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/uaccess.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/tlb.h>
#include <asm/eeh.h>
#include <asm/processor.h>
#include <asm/mmzone.h>
#include <asm/cputable.h>
#include <asm/ppcdebug.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/iommu.h>
#include <asm/abs_addr.h>
#include <asm/vdso.h>
#include <asm/imalloc.h>

int mem_init_done;
unsigned long ioremap_bot = IMALLOC_BASE;
static unsigned long phbs_io_bot = PHBS_IO_BASE;

extern pgd_t swapper_pg_dir[];
extern struct task_struct *current_set[NR_CPUS];

extern pgd_t ioremap_dir[];
pgd_t * ioremap_pgd = (pgd_t *)&ioremap_dir;

unsigned long klimit = (unsigned long)_end;

unsigned long _SDR1=0;
unsigned long _ASR=0;

/* max amount of RAM to use */
unsigned long __max_memory;

/* info on what we think the IO hole is */
unsigned long 	io_hole_start;
unsigned long	io_hole_size;

void show_mem(void)
{
	unsigned long total = 0, reserved = 0;
	unsigned long shared = 0, cached = 0;
	struct page *page;
	pg_data_t *pgdat;
	unsigned long i;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	for_each_pgdat(pgdat) {
		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			page = pgdat->node_mem_map + i;
			total++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (page_count(page))
				shared += page_count(page) - 1;
		}
	}
	printk("%ld pages of RAM\n", total);
	printk("%ld reserved pages\n", reserved);
	printk("%ld pages shared\n", shared);
	printk("%ld pages swap cached\n", cached);
}

#ifdef CONFIG_PPC_ISERIES

void __iomem *ioremap(unsigned long addr, unsigned long size)
{
	return (void __iomem *)addr;
}

extern void __iomem *__ioremap(unsigned long addr, unsigned long size,
		       unsigned long flags)
{
	return (void __iomem *)addr;
}

void iounmap(volatile void __iomem *addr)
{
	return;
}

#else

static void unmap_im_area_pte(pmd_t *pmd, unsigned long addr,
				  unsigned long end)
{
	pte_t *pte;

	pte = pte_offset_kernel(pmd, addr);
	do {
		pte_t ptent = ptep_get_and_clear(&ioremap_mm, addr, pte);
		WARN_ON(!pte_none(ptent) && !pte_present(ptent));
	} while (pte++, addr += PAGE_SIZE, addr != end);
}

static inline void unmap_im_area_pmd(pud_t *pud, unsigned long addr,
				     unsigned long end)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		unmap_im_area_pte(pmd, addr, next);
	} while (pmd++, addr = next, addr != end);
}

static inline void unmap_im_area_pud(pgd_t *pgd, unsigned long addr,
				     unsigned long end)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		unmap_im_area_pmd(pud, addr, next);
	} while (pud++, addr = next, addr != end);
}

static void unmap_im_area(unsigned long addr, unsigned long end)
{
	struct mm_struct *mm = &ioremap_mm;
	unsigned long next;
	pgd_t *pgd;

	spin_lock(&mm->page_table_lock);

	pgd = pgd_offset_i(addr);
	flush_cache_vunmap(addr, end);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		unmap_im_area_pud(pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
	flush_tlb_kernel_range(start, end);

	spin_unlock(&mm->page_table_lock);
}

/*
 * map_io_page currently only called by __ioremap
 * map_io_page adds an entry to the ioremap page table
 * and adds an entry to the HPT, possibly bolting it
 */
static int map_io_page(unsigned long ea, unsigned long pa, int flags)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long vsid;

	if (mem_init_done) {
		spin_lock(&ioremap_mm.page_table_lock);
		pgdp = pgd_offset_i(ea);
		pudp = pud_alloc(&ioremap_mm, pgdp, ea);
		if (!pudp)
			return -ENOMEM;
		pmdp = pmd_alloc(&ioremap_mm, pudp, ea);
		if (!pmdp)
			return -ENOMEM;
		ptep = pte_alloc_kernel(&ioremap_mm, pmdp, ea);
		if (!ptep)
			return -ENOMEM;
		pa = abs_to_phys(pa);
		set_pte_at(&ioremap_mm, ea, ptep, pfn_pte(pa >> PAGE_SHIFT,
							  __pgprot(flags)));
		spin_unlock(&ioremap_mm.page_table_lock);
	} else {
		unsigned long va, vpn, hash, hpteg;

		/*
		 * If the mm subsystem is not fully up, we cannot create a
		 * linux page table entry for this mapping.  Simply bolt an
		 * entry in the hardware page table.
		 */
		vsid = get_kernel_vsid(ea);
		va = (vsid << 28) | (ea & 0xFFFFFFF);
		vpn = va >> PAGE_SHIFT;

		hash = hpt_hash(vpn, 0);

		hpteg = ((hash & htab_hash_mask) * HPTES_PER_GROUP);

		/* Panic if a pte grpup is full */
		if (ppc_md.hpte_insert(hpteg, va, pa >> PAGE_SHIFT, 0,
				       _PAGE_NO_CACHE|_PAGE_GUARDED|PP_RWXX,
				       1, 0) == -1) {
			panic("map_io_page: could not insert mapping");
		}
	}
	return 0;
}


static void __iomem * __ioremap_com(unsigned long addr, unsigned long pa,
			    unsigned long ea, unsigned long size,
			    unsigned long flags)
{
	unsigned long i;

	if ((flags & _PAGE_PRESENT) == 0)
		flags |= pgprot_val(PAGE_KERNEL);

	for (i = 0; i < size; i += PAGE_SIZE)
		if (map_io_page(ea+i, pa+i, flags))
			goto failure;

	return (void __iomem *) (ea + (addr & ~PAGE_MASK));
 failure:
	if (mem_init_done)
		unmap_im_area(ea, ea + size);
	return NULL;
}


void __iomem *
ioremap(unsigned long addr, unsigned long size)
{
	return __ioremap(addr, size, _PAGE_NO_CACHE | _PAGE_GUARDED);
}

void __iomem * __ioremap(unsigned long addr, unsigned long size,
			 unsigned long flags)
{
	unsigned long pa, ea;
	void __iomem *ret;

	/*
	 * Choose an address to map it to.
	 * Once the imalloc system is running, we use it.
	 * Before that, we map using addresses going
	 * up from ioremap_bot.  imalloc will use
	 * the addresses from ioremap_bot through
	 * IMALLOC_END (0xE000001fffffffff)
	 * 
	 */
	pa = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - pa;

	if (size == 0)
		return NULL;

	if (mem_init_done) {
		struct vm_struct *area;
		area = im_get_free_area(size);
		if (area == NULL)
			return NULL;
		ea = (unsigned long)(area->addr);
		ret = __ioremap_com(addr, pa, ea, size, flags);
		if (!ret)
			im_free(area->addr);
	} else {
		ea = ioremap_bot;
		ret = __ioremap_com(addr, pa, ea, size, flags);
		if (ret)
			ioremap_bot += size;
	}
	return ret;
}

#define IS_PAGE_ALIGNED(_val) ((_val) == ((_val) & PAGE_MASK))

int __ioremap_explicit(unsigned long pa, unsigned long ea,
		       unsigned long size, unsigned long flags)
{
	struct vm_struct *area;
	void __iomem *ret;
	
	/* For now, require page-aligned values for pa, ea, and size */
	if (!IS_PAGE_ALIGNED(pa) || !IS_PAGE_ALIGNED(ea) ||
	    !IS_PAGE_ALIGNED(size)) {
		printk(KERN_ERR	"unaligned value in %s\n", __FUNCTION__);
		return 1;
	}
	
	if (!mem_init_done) {
		/* Two things to consider in this case:
		 * 1) No records will be kept (imalloc, etc) that the region
		 *    has been remapped
		 * 2) It won't be easy to iounmap() the region later (because
		 *    of 1)
		 */
		;
	} else {
		area = im_get_area(ea, size,
			IM_REGION_UNUSED|IM_REGION_SUBSET|IM_REGION_EXISTS);
		if (area == NULL) {
			/* Expected when PHB-dlpar is in play */
			return 1;
		}
		if (ea != (unsigned long) area->addr) {
			printk(KERN_ERR "unexpected addr return from "
			       "im_get_area\n");
			return 1;
		}
	}
	
	ret = __ioremap_com(pa, pa, ea, size, flags);
	if (ret == NULL) {
		printk(KERN_ERR "ioremap_explicit() allocation failure !\n");
		return 1;
	}
	if (ret != (void *) ea) {
		printk(KERN_ERR "__ioremap_com() returned unexpected addr\n");
		return 1;
	}

	return 0;
}

/*  
 * Unmap an IO region and remove it from imalloc'd list.
 * Access to IO memory should be serialized by driver.
 * This code is modeled after vmalloc code - unmap_vm_area()
 *
 * XXX	what about calls before mem_init_done (ie python_countermeasures())
 */
void iounmap(volatile void __iomem *token)
{
	unsigned long address, size;
	void *addr;

	if (!mem_init_done)
		return;
	
	addr = (void *) ((unsigned long __force) token & PAGE_MASK);
	
	if ((size = im_free(addr)) == 0)
		return;

	address = (unsigned long)addr; 
	unmap_im_area(address, address + size);
}

static int iounmap_subset_regions(unsigned long addr, unsigned long size)
{
	struct vm_struct *area;

	/* Check whether subsets of this region exist */
	area = im_get_area(addr, size, IM_REGION_SUPERSET);
	if (area == NULL)
		return 1;

	while (area) {
		iounmap((void __iomem *) area->addr);
		area = im_get_area(addr, size,
				IM_REGION_SUPERSET);
	}

	return 0;
}

int iounmap_explicit(volatile void __iomem *start, unsigned long size)
{
	struct vm_struct *area;
	unsigned long addr;
	int rc;
	
	addr = (unsigned long __force) start & PAGE_MASK;

	/* Verify that the region either exists or is a subset of an existing
	 * region.  In the latter case, split the parent region to create 
	 * the exact region 
	 */
	area = im_get_area(addr, size, 
			    IM_REGION_EXISTS | IM_REGION_SUBSET);
	if (area == NULL) {
		/* Determine whether subset regions exist.  If so, unmap */
		rc = iounmap_subset_regions(addr, size);
		if (rc) {
			printk(KERN_ERR
			       "%s() cannot unmap nonexistent range 0x%lx\n",
 				__FUNCTION__, addr);
			return 1;
		}
	} else {
		iounmap((void __iomem *) area->addr);
	}
	/*
	 * FIXME! This can't be right:
	iounmap(area->addr);
	 * Maybe it should be "iounmap(area);"
	 */
	return 0;
}

#endif

EXPORT_SYMBOL(ioremap);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)__init_begin;
	for (; addr < (unsigned long)__init_end; addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		set_page_count(virt_to_page(addr), 1);
		free_page(addr);
		totalram_pages++;
	}
	printk ("Freeing unused kernel memory: %luk freed\n",
		((unsigned long)__init_end - (unsigned long)__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
#endif

static DEFINE_SPINLOCK(mmu_context_lock);
static DEFINE_IDR(mmu_context_idr);

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	int index;
	int err;

#ifdef CONFIG_HUGETLB_PAGE
	/* We leave htlb_segs as it was, but for a fork, we need to
	 * clear the huge_pgdir. */
	mm->context.huge_pgdir = NULL;
#endif

again:
	if (!idr_pre_get(&mmu_context_idr, GFP_KERNEL))
		return -ENOMEM;

	spin_lock(&mmu_context_lock);
	err = idr_get_new_above(&mmu_context_idr, NULL, 1, &index);
	spin_unlock(&mmu_context_lock);

	if (err == -EAGAIN)
		goto again;
	else if (err)
		return err;

	if (index > MAX_CONTEXT) {
		idr_remove(&mmu_context_idr, index);
		return -ENOMEM;
	}

	mm->context.id = index;

	return 0;
}

void destroy_context(struct mm_struct *mm)
{
	spin_lock(&mmu_context_lock);
	idr_remove(&mmu_context_idr, mm->context.id);
	spin_unlock(&mmu_context_lock);

	mm->context.id = NO_CONTEXT;

	hugetlb_mm_free_pgd(mm);
}

/*
 * Do very early mm setup.
 */
void __init mm_init_ppc64(void)
{
#ifndef CONFIG_PPC_ISERIES
	unsigned long i;
#endif

	ppc64_boot_msg(0x100, "MM Init");

	/* This is the story of the IO hole... please, keep seated,
	 * unfortunately, we are out of oxygen masks at the moment.
	 * So we need some rough way to tell where your big IO hole
	 * is. On pmac, it's between 2G and 4G, on POWER3, it's around
	 * that area as well, on POWER4 we don't have one, etc...
	 * We need that as a "hint" when sizing the TCE table on POWER3
	 * So far, the simplest way that seem work well enough for us it
	 * to just assume that the first discontinuity in our physical
	 * RAM layout is the IO hole. That may not be correct in the future
	 * (and isn't on iSeries but then we don't care ;)
	 */

#ifndef CONFIG_PPC_ISERIES
	for (i = 1; i < lmb.memory.cnt; i++) {
		unsigned long base, prevbase, prevsize;

		prevbase = lmb.memory.region[i-1].physbase;
		prevsize = lmb.memory.region[i-1].size;
		base = lmb.memory.region[i].physbase;
		if (base > (prevbase + prevsize)) {
			io_hole_start = prevbase + prevsize;
			io_hole_size = base  - (prevbase + prevsize);
			break;
		}
	}
#endif /* CONFIG_PPC_ISERIES */
	if (io_hole_start)
		printk("IO Hole assumed to be %lx -> %lx\n",
		       io_hole_start, io_hole_start + io_hole_size - 1);

	ppc64_boot_msg(0x100, "MM Init Done");
}

/*
 * This is called by /dev/mem to know if a given address has to
 * be mapped non-cacheable or not
 */
int page_is_ram(unsigned long pfn)
{
	int i;
	unsigned long paddr = (pfn << PAGE_SHIFT);

	for (i=0; i < lmb.memory.cnt; i++) {
		unsigned long base;

#ifdef CONFIG_MSCHUNKS
		base = lmb.memory.region[i].physbase;
#else
		base = lmb.memory.region[i].base;
#endif
		if ((paddr >= base) &&
			(paddr < (base + lmb.memory.region[i].size))) {
			return 1;
		}
	}

	return 0;
}
EXPORT_SYMBOL(page_is_ram);

/*
 * Initialize the bootmem system and give it all the memory we
 * have available.
 */
#ifndef CONFIG_DISCONTIGMEM
void __init do_init_bootmem(void)
{
	unsigned long i;
	unsigned long start, bootmap_pages;
	unsigned long total_pages = lmb_end_of_DRAM() >> PAGE_SHIFT;
	int boot_mapsize;

	/*
	 * Find an area to use for the bootmem bitmap.  Calculate the size of
	 * bitmap required as (Total Memory) / PAGE_SIZE / BITS_PER_BYTE.
	 * Add 1 additional page in case the address isn't page-aligned.
	 */
	bootmap_pages = bootmem_bootmap_pages(total_pages);

	start = abs_to_phys(lmb_alloc(bootmap_pages<<PAGE_SHIFT, PAGE_SIZE));
	BUG_ON(!start);

	boot_mapsize = init_bootmem(start >> PAGE_SHIFT, total_pages);

	max_pfn = max_low_pfn;

	/* add all physical memory to the bootmem map. Also find the first */
	for (i=0; i < lmb.memory.cnt; i++) {
		unsigned long physbase, size;

		physbase = lmb.memory.region[i].physbase;
		size = lmb.memory.region[i].size;
		free_bootmem(physbase, size);
	}

	/* reserve the sections we're already using */
	for (i=0; i < lmb.reserved.cnt; i++) {
		unsigned long physbase = lmb.reserved.region[i].physbase;
		unsigned long size = lmb.reserved.region[i].size;

		reserve_bootmem(physbase, size);
	}
}

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];
	unsigned long zholes_size[MAX_NR_ZONES];
	unsigned long total_ram = lmb_phys_mem_size();
	unsigned long top_of_ram = lmb_end_of_DRAM();

	printk(KERN_INFO "Top of RAM: 0x%lx, Total RAM: 0x%lx\n",
	       top_of_ram, total_ram);
	printk(KERN_INFO "Memory hole size: %ldMB\n",
	       (top_of_ram - total_ram) >> 20);
	/*
	 * All pages are DMA-able so we put them all in the DMA zone.
	 */
	memset(zones_size, 0, sizeof(zones_size));
	memset(zholes_size, 0, sizeof(zholes_size));

	zones_size[ZONE_DMA] = top_of_ram >> PAGE_SHIFT;
	zholes_size[ZONE_DMA] = (top_of_ram - total_ram) >> PAGE_SHIFT;

	free_area_init_node(0, NODE_DATA(0), zones_size,
			    __pa(PAGE_OFFSET) >> PAGE_SHIFT, zholes_size);
}
#endif /* CONFIG_DISCONTIGMEM */

static struct kcore_list kcore_vmem;

static int __init setup_kcore(void)
{
	int i;

	for (i=0; i < lmb.memory.cnt; i++) {
		unsigned long physbase, size;
		struct kcore_list *kcore_mem;

		physbase = lmb.memory.region[i].physbase;
		size = lmb.memory.region[i].size;

		/* GFP_ATOMIC to avoid might_sleep warnings during boot */
		kcore_mem = kmalloc(sizeof(struct kcore_list), GFP_ATOMIC);
		if (!kcore_mem)
			panic("mem_init: kmalloc failed\n");

		kclist_add(kcore_mem, __va(physbase), size);
	}

	kclist_add(&kcore_vmem, (void *)VMALLOC_START, VMALLOC_END-VMALLOC_START);

	return 0;
}
module_init(setup_kcore);

void __init mem_init(void)
{
#ifdef CONFIG_DISCONTIGMEM
	int nid;
#endif
	pg_data_t *pgdat;
	unsigned long i;
	struct page *page;
	unsigned long reservedpages = 0, codesize, initsize, datasize, bsssize;

	num_physpages = max_low_pfn;	/* RAM is assumed contiguous */
	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

#ifdef CONFIG_DISCONTIGMEM
        for_each_online_node(nid) {
		if (NODE_DATA(nid)->node_spanned_pages != 0) {
			printk("freeing bootmem node %x\n", nid);
			totalram_pages +=
				free_all_bootmem_node(NODE_DATA(nid));
		}
	}
#else
	max_mapnr = num_physpages;
	totalram_pages += free_all_bootmem();
#endif

	for_each_pgdat(pgdat) {
		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			page = pgdat->node_mem_map + i;
			if (PageReserved(page))
				reservedpages++;
		}
	}

	codesize = (unsigned long)&_etext - (unsigned long)&_stext;
	initsize = (unsigned long)&__init_end - (unsigned long)&__init_begin;
	datasize = (unsigned long)&_edata - (unsigned long)&__init_end;
	bsssize = (unsigned long)&__bss_stop - (unsigned long)&__bss_start;

	printk(KERN_INFO "Memory: %luk/%luk available (%luk kernel code, "
	       "%luk reserved, %luk data, %luk bss, %luk init)\n",
		(unsigned long)nr_free_pages() << (PAGE_SHIFT-10),
		num_physpages << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		bsssize >> 10,
		initsize >> 10);

	mem_init_done = 1;

#ifdef CONFIG_PPC_ISERIES
	iommu_vio_init();
#endif
	/* Initialize the vDSO */
	vdso_init();
}

/*
 * This is called when a page has been modified by the kernel.
 * It just marks the page as not i-cache clean.  We do the i-cache
 * flush later when the page is given to a user process, if necessary.
 */
void flush_dcache_page(struct page *page)
{
	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		return;
	/* avoid an atomic op if possible */
	if (test_bit(PG_arch_1, &page->flags))
		clear_bit(PG_arch_1, &page->flags);
}
EXPORT_SYMBOL(flush_dcache_page);

void clear_user_page(void *page, unsigned long vaddr, struct page *pg)
{
	clear_page(page);

	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		return;
	/*
	 * We shouldnt have to do this, but some versions of glibc
	 * require it (ld.so assumes zero filled pages are icache clean)
	 * - Anton
	 */

	/* avoid an atomic op if possible */
	if (test_bit(PG_arch_1, &pg->flags))
		clear_bit(PG_arch_1, &pg->flags);
}
EXPORT_SYMBOL(clear_user_page);

void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
		    struct page *pg)
{
	copy_page(vto, vfrom);

	/*
	 * We should be able to use the following optimisation, however
	 * there are two problems.
	 * Firstly a bug in some versions of binutils meant PLT sections
	 * were not marked executable.
	 * Secondly the first word in the GOT section is blrl, used
	 * to establish the GOT address. Until recently the GOT was
	 * not marked executable.
	 * - Anton
	 */
#if 0
	if (!vma->vm_file && ((vma->vm_flags & VM_EXEC) == 0))
		return;
#endif

	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		return;

	/* avoid an atomic op if possible */
	if (test_bit(PG_arch_1, &pg->flags))
		clear_bit(PG_arch_1, &pg->flags);
}

void flush_icache_user_range(struct vm_area_struct *vma, struct page *page,
			     unsigned long addr, int len)
{
	unsigned long maddr;

	maddr = (unsigned long)page_address(page) + (addr & ~PAGE_MASK);
	flush_icache_range(maddr, maddr + len);
}
EXPORT_SYMBOL(flush_icache_user_range);

/*
 * This is called at the end of handling a user page fault, when the
 * fault has been handled by updating a PTE in the linux page tables.
 * We use it to preload an HPTE into the hash table corresponding to
 * the updated linux PTE.
 * 
 * This must always be called with the mm->page_table_lock held
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long ea,
		      pte_t pte)
{
	unsigned long vsid;
	void *pgdir;
	pte_t *ptep;
	int local = 0;
	cpumask_t tmp;
	unsigned long flags;

	/* handle i-cache coherency */
	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE) &&
	    !cpu_has_feature(CPU_FTR_NOEXECUTE)) {
		unsigned long pfn = pte_pfn(pte);
		if (pfn_valid(pfn)) {
			struct page *page = pfn_to_page(pfn);
			if (!PageReserved(page)
			    && !test_bit(PG_arch_1, &page->flags)) {
				__flush_dcache_icache(page_address(page));
				set_bit(PG_arch_1, &page->flags);
			}
		}
	}

	/* We only want HPTEs for linux PTEs that have _PAGE_ACCESSED set */
	if (!pte_young(pte))
		return;

	pgdir = vma->vm_mm->pgd;
	if (pgdir == NULL)
		return;

	ptep = find_linux_pte(pgdir, ea);
	if (!ptep)
		return;

	vsid = get_vsid(vma->vm_mm->context.id, ea);

	local_irq_save(flags);
	tmp = cpumask_of_cpu(smp_processor_id());
	if (cpus_equal(vma->vm_mm->cpu_vm_mask, tmp))
		local = 1;

	__hash_page(ea, pte_val(pte) & (_PAGE_USER|_PAGE_RW), vsid, ptep,
		    0x300, local);
	local_irq_restore(flags);
}

void __iomem * reserve_phb_iospace(unsigned long size)
{
	void __iomem *virt_addr;
		
	if (phbs_io_bot >= IMALLOC_BASE) 
		panic("reserve_phb_iospace(): phb io space overflow\n");
			
	virt_addr = (void __iomem *) phbs_io_bot;
	phbs_io_bot += size;

	return virt_addr;
}

kmem_cache_t *zero_cache;

static void zero_ctor(void *pte, kmem_cache_t *cache, unsigned long flags)
{
	memset(pte, 0, PAGE_SIZE);
}

void pgtable_cache_init(void)
{
	zero_cache = kmem_cache_create("zero",
				PAGE_SIZE,
				0,
				SLAB_HWCACHE_ALIGN | SLAB_MUST_HWCACHE_ALIGN,
				zero_ctor,
				NULL);
	if (!zero_cache)
		panic("pgtable_cache_init(): could not create zero_cache!\n");
}

pgprot_t phys_mem_access_prot(struct file *file, unsigned long addr,
			      unsigned long size, pgprot_t vma_prot)
{
	if (ppc_md.phys_mem_access_prot)
		return ppc_md.phys_mem_access_prot(file, addr, size, vma_prot);

	if (!page_is_ram(addr >> PAGE_SHIFT))
		vma_prot = __pgprot(pgprot_val(vma_prot)
				    | _PAGE_GUARDED | _PAGE_NO_CACHE);
	return vma_prot;
}
EXPORT_SYMBOL(phys_mem_access_prot);

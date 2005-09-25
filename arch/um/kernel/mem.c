/*
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/kernel.h"
#include "linux/mm.h"
#include "linux/bootmem.h"
#include "linux/swap.h"
#include "linux/highmem.h"
#include "linux/gfp.h"
#include "asm/page.h"
#include "asm/fixmap.h"
#include "asm/pgalloc.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "mem_user.h"
#include "uml_uaccess.h"
#include "os.h"
#include "linux/types.h"
#include "linux/string.h"
#include "init.h"
#include "kern_constants.h"

extern char __binary_start;

/* Changed during early boot */
unsigned long *empty_zero_page = NULL;
unsigned long *empty_bad_page = NULL;
pgd_t swapper_pg_dir[PTRS_PER_PGD];
unsigned long highmem;
int kmalloc_ok = 0;

static unsigned long brk_end;

void unmap_physmem(void)
{
	os_unmap_memory((void *) brk_end, uml_reserved - brk_end);
}

static void map_cb(void *unused)
{
	map_memory(brk_end, __pa(brk_end), uml_reserved - brk_end, 1, 1, 0);
}

#ifdef CONFIG_HIGHMEM
static void setup_highmem(unsigned long highmem_start,
			  unsigned long highmem_len)
{
	struct page *page;
	unsigned long highmem_pfn;
	int i;

	highmem_pfn = __pa(highmem_start) >> PAGE_SHIFT;
	for(i = 0; i < highmem_len >> PAGE_SHIFT; i++){
		page = &mem_map[highmem_pfn + i];
		ClearPageReserved(page);
		set_page_count(page, 1);
		__free_page(page);
	}
}
#endif

void mem_init(void)
{
	unsigned long start;

	max_low_pfn = (high_physmem - uml_physmem) >> PAGE_SHIFT;

        /* clear the zero-page */
        memset((void *) empty_zero_page, 0, PAGE_SIZE);

	/* Map in the area just after the brk now that kmalloc is about
	 * to be turned on.
	 */
	brk_end = (unsigned long) UML_ROUND_UP(sbrk(0));
	map_cb(NULL);
	initial_thread_cb(map_cb, NULL);
	free_bootmem(__pa(brk_end), uml_reserved - brk_end);
	uml_reserved = brk_end;

	/* Fill in any hole at the start of the binary */
	start = (unsigned long) &__binary_start & PAGE_MASK;
	if(uml_physmem != start){
		map_memory(uml_physmem, __pa(uml_physmem), start - uml_physmem,
			   1, 1, 0);
	}

	/* this will put all low memory onto the freelists */
	totalram_pages = free_all_bootmem();
	totalhigh_pages = highmem >> PAGE_SHIFT;
	totalram_pages += totalhigh_pages;
	num_physpages = totalram_pages;
	max_pfn = totalram_pages;
	printk(KERN_INFO "Memory: %luk available\n", 
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT-10));
	kmalloc_ok = 1;

#ifdef CONFIG_HIGHMEM
	setup_highmem(end_iomem, highmem);
#endif
}

/*
 * Create a page table and place a pointer to it in a middle page
 * directory entry.
 */
static void __init one_page_table_init(pmd_t *pmd)
{
	if (pmd_none(*pmd)) {
		pte_t *pte = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);
		set_pmd(pmd, __pmd(_KERNPG_TABLE +
					   (unsigned long) __pa(pte)));
		if (pte != pte_offset_kernel(pmd, 0))
			BUG();
	}
}

static void __init one_md_table_init(pud_t *pud)
{
#ifdef CONFIG_3_LEVEL_PGTABLES
	pmd_t *pmd_table = (pmd_t *) alloc_bootmem_low_pages(PAGE_SIZE);
	set_pud(pud, __pud(_KERNPG_TABLE + (unsigned long) __pa(pmd_table)));
	if (pmd_table != pmd_offset(pud, 0))
		BUG();
#endif
}

static void __init fixrange_init(unsigned long start, unsigned long end, 
				 pgd_t *pgd_base)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	int i, j;
	unsigned long vaddr;

	vaddr = start;
	i = pgd_index(vaddr);
	j = pmd_index(vaddr);
	pgd = pgd_base + i;

	for ( ; (i < PTRS_PER_PGD) && (vaddr < end); pgd++, i++) {
		pud = pud_offset(pgd, vaddr);
		if (pud_none(*pud))
			one_md_table_init(pud);
		pmd = pmd_offset(pud, vaddr);
		for (; (j < PTRS_PER_PMD) && (vaddr != end); pmd++, j++) {
			one_page_table_init(pmd);
			vaddr += PMD_SIZE;
		}
		j = 0;
	}
}

#ifdef CONFIG_HIGHMEM
pte_t *kmap_pte;
pgprot_t kmap_prot;

#define kmap_get_fixmap_pte(vaddr)					\
	pte_offset_kernel(pmd_offset(pud_offset(pgd_offset_k(vaddr), (vaddr)),\
 			  (vaddr)), (vaddr))

static void __init kmap_init(void)
{
	unsigned long kmap_vstart;

	/* cache the first kmap pte */
	kmap_vstart = __fix_to_virt(FIX_KMAP_BEGIN);
	kmap_pte = kmap_get_fixmap_pte(kmap_vstart);

	kmap_prot = PAGE_KERNEL;
}

static void init_highmem(void)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long vaddr;

	/*
	 * Permanent kmaps:
	 */
	vaddr = PKMAP_BASE;
	fixrange_init(vaddr, vaddr + PAGE_SIZE*LAST_PKMAP, swapper_pg_dir);

	pgd = swapper_pg_dir + pgd_index(vaddr);
	pud = pud_offset(pgd, vaddr);
	pmd = pmd_offset(pud, vaddr);
	pte = pte_offset_kernel(pmd, vaddr);
	pkmap_page_table = pte;

	kmap_init();
}
#endif /* CONFIG_HIGHMEM */

static void __init fixaddr_user_init( void)
{
#ifdef CONFIG_ARCH_REUSE_HOST_VSYSCALL_AREA
	long size = FIXADDR_USER_END - FIXADDR_USER_START;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long paddr, vaddr = FIXADDR_USER_START;

	if (  ! size )
		return;

	fixrange_init( FIXADDR_USER_START, FIXADDR_USER_END, swapper_pg_dir);
	paddr = (unsigned long)alloc_bootmem_low_pages( size);
	memcpy( (void *)paddr, (void *)FIXADDR_USER_START, size);
	paddr = __pa(paddr);
	for ( ; size > 0; size-=PAGE_SIZE, vaddr+=PAGE_SIZE, paddr+=PAGE_SIZE){
		pgd = swapper_pg_dir + pgd_index(vaddr);
		pud = pud_offset(pgd, vaddr);
		pmd = pmd_offset(pud, vaddr);
		pte = pte_offset_kernel(pmd, vaddr);
		pte_set_val( (*pte), paddr, PAGE_READONLY);
	}
#endif
}

void paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES], vaddr;
	int i;

	empty_zero_page = (unsigned long *) alloc_bootmem_low_pages(PAGE_SIZE);
	empty_bad_page = (unsigned long *) alloc_bootmem_low_pages(PAGE_SIZE);
	for(i=0;i<sizeof(zones_size)/sizeof(zones_size[0]);i++) 
		zones_size[i] = 0;
	zones_size[0] = (end_iomem >> PAGE_SHIFT) - (uml_physmem >> PAGE_SHIFT);
	zones_size[2] = highmem >> PAGE_SHIFT;
	free_area_init(zones_size);

	/*
	 * Fixed mappings, only the page table structure has to be
	 * created - mappings will be set by set_fixmap():
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	fixrange_init(vaddr, FIXADDR_TOP, swapper_pg_dir);

	fixaddr_user_init();

#ifdef CONFIG_HIGHMEM
	init_highmem();
#endif
}

struct page *arch_validate(struct page *page, int mask, int order)
{
	unsigned long addr, zero = 0;
	int i;

 again:
	if(page == NULL) return(page);
	if(PageHighMem(page)) return(page);

	addr = (unsigned long) page_address(page);
	for(i = 0; i < (1 << order); i++){
		current->thread.fault_addr = (void *) addr;
		if(__do_copy_to_user((void __user *) addr, &zero,
				     sizeof(zero),
				     &current->thread.fault_addr,
				     &current->thread.fault_catcher)){
			if(!(mask & __GFP_WAIT)) return(NULL);
			else break;
		}
		addr += PAGE_SIZE;
	}

	if(i == (1 << order)) return(page);
	page = alloc_pages(mask, order);
	goto again;
}

/* This can't do anything because nothing in the kernel image can be freed
 * since it's not in kernel physical memory.
 */

void free_initmem(void)
{
}

#ifdef CONFIG_BLK_DEV_INITRD

void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		printk ("Freeing initrd memory: %ldk freed\n", 
			(end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
	
#endif

void show_mem(void)
{
        int pfn, total = 0, reserved = 0;
        int shared = 0, cached = 0;
        int highmem = 0;
	struct page *page;

        printk("Mem-info:\n");
        show_free_areas();
        printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
        pfn = max_mapnr;
        while(pfn-- > 0) {
		page = pfn_to_page(pfn);
                total++;
                if(PageHighMem(page))
                        highmem++;
                if(PageReserved(page))
                        reserved++;
                else if(PageSwapCache(page))
                        cached++;
                else if(page_count(page))
                        shared += page_count(page) - 1;
        }
        printk("%d pages of RAM\n", total);
        printk("%d pages of HIGHMEM\n", highmem);
        printk("%d reserved pages\n", reserved);
        printk("%d pages shared\n", shared);
        printk("%d pages swap cached\n", cached);
}

/*
 * Allocate and free page tables.
 */

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL);

	if (pgd) {
		memset(pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(pgd + USER_PTRS_PER_PGD, 
		       swapper_pg_dir + USER_PTRS_PER_PGD, 
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}
	return pgd;
}

void pgd_free(pgd_t *pgd)
{
	free_page((unsigned long) pgd);
}

pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte;

	pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);
	return pte;
}

struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pte;
   
	pte = alloc_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);
	return pte;
}

struct iomem_region *iomem_regions = NULL;
int iomem_size = 0;

extern int parse_iomem(char *str, int *add) __init;

__uml_setup("iomem=", parse_iomem,
"iomem=<name>,<file>\n"
"    Configure <file> as an IO memory region named <name>.\n\n"
);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

// SPDX-License-Identifier: GPL-2.0
/*
 * linux/arch/m68k/mm/motorola.c
 *
 * Routines specific to the Motorola MMU, originally from:
 * linux/arch/m68k/init.c
 * which are Copyright (C) 1995 Hamish Macdonald
 *
 * Moved 8/20/1999 Sam Creasey
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/gfp.h>

#include <asm/setup.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/dma.h>
#ifdef CONFIG_ATARI
#include <asm/atari_stram.h>
#endif
#include <asm/sections.h>

#undef DEBUG

#ifndef mm_cachebits
/*
 * Bits to add to page descriptors for "normal" caching mode.
 * For 68020/030 this is 0.
 * For 68040, this is _PAGE_CACHE040 (cachable, copyback)
 */
unsigned long mm_cachebits;
EXPORT_SYMBOL(mm_cachebits);
#endif


/*
 * Motorola 680x0 user's manual recommends using uncached memory for address
 * translation tables.
 *
 * Seeing how the MMU can be external on (some of) these chips, that seems like
 * a very important recommendation to follow. Provide some helpers to combat
 * 'variation' amongst the users of this.
 */

void mmu_page_ctor(void *page)
{
	__flush_page_to_ram(page);
	flush_tlb_kernel_page(page);
	nocache_page(page);
}

void mmu_page_dtor(void *page)
{
	cache_page(page);
}

/* ++andreas: {get,free}_pointer_table rewritten to use unused fields from
   struct page instead of separately kmalloced struct.  Stolen from
   arch/sparc/mm/srmmu.c ... */

typedef struct list_head ptable_desc;
static LIST_HEAD(ptable_list);

#define PD_PTABLE(page) ((ptable_desc *)&(virt_to_page(page)->lru))
#define PD_PAGE(ptable) (list_entry(ptable, struct page, lru))
#define PD_MARKBITS(dp) (*(unsigned char *)&PD_PAGE(dp)->index)

#define PTABLE_SIZE (PTRS_PER_PMD * sizeof(pmd_t))

void __init init_pointer_table(unsigned long ptable)
{
	ptable_desc *dp;
	unsigned long page = ptable & PAGE_MASK;
	unsigned char mask = 1 << ((ptable - page)/PTABLE_SIZE);

	dp = PD_PTABLE(page);
	if (!(PD_MARKBITS(dp) & mask)) {
		PD_MARKBITS(dp) = 0xff;
		list_add(dp, &ptable_list);
	}

	PD_MARKBITS(dp) &= ~mask;
	pr_debug("init_pointer_table: %lx, %x\n", ptable, PD_MARKBITS(dp));

	/* unreserve the page so it's possible to free that page */
	__ClearPageReserved(PD_PAGE(dp));
	init_page_count(PD_PAGE(dp));

	return;
}

pmd_t *get_pointer_table (void)
{
	ptable_desc *dp = ptable_list.next;
	unsigned char mask = PD_MARKBITS (dp);
	unsigned char tmp;
	unsigned int off;

	/*
	 * For a pointer table for a user process address space, a
	 * table is taken from a page allocated for the purpose.  Each
	 * page can hold 8 pointer tables.  The page is remapped in
	 * virtual address space to be noncacheable.
	 */
	if (mask == 0) {
		void *page;
		ptable_desc *new;

		if (!(page = (void *)get_zeroed_page(GFP_KERNEL)))
			return NULL;

		mmu_page_ctor(page);

		new = PD_PTABLE(page);
		PD_MARKBITS(new) = 0xfe;
		list_add_tail(new, dp);

		return (pmd_t *)page;
	}

	for (tmp = 1, off = 0; (mask & tmp) == 0; tmp <<= 1, off += PTABLE_SIZE)
		;
	PD_MARKBITS(dp) = mask & ~tmp;
	if (!PD_MARKBITS(dp)) {
		/* move to end of list */
		list_move_tail(dp, &ptable_list);
	}
	return (pmd_t *) (page_address(PD_PAGE(dp)) + off);
}

int free_pointer_table (pmd_t *ptable)
{
	ptable_desc *dp;
	unsigned long page = (unsigned long)ptable & PAGE_MASK;
	unsigned char mask = 1 << (((unsigned long)ptable - page)/PTABLE_SIZE);

	dp = PD_PTABLE(page);
	if (PD_MARKBITS (dp) & mask)
		panic ("table already free!");

	PD_MARKBITS (dp) |= mask;

	if (PD_MARKBITS(dp) == 0xff) {
		/* all tables in page are free, free page */
		list_del(dp);
		mmu_page_dtor((void *)page);
		free_page (page);
		return 1;
	} else if (ptable_list.next != dp) {
		/*
		 * move this descriptor to the front of the list, since
		 * it has one or more free tables.
		 */
		list_move(dp, &ptable_list);
	}
	return 0;
}

/* size of memory already mapped in head.S */
extern __initdata unsigned long m68k_init_mapped_size;

extern unsigned long availmem;

static pte_t * __init kernel_page_table(void)
{
	pte_t *ptablep;

	ptablep = (pte_t *)memblock_alloc_low(PAGE_SIZE, PAGE_SIZE);
	if (!ptablep)
		panic("%s: Failed to allocate %lu bytes align=%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);

	clear_page(ptablep);
	mmu_page_ctor(ptablep);

	return ptablep;
}

static pmd_t *last_pgtable __initdata = NULL;
pmd_t *zero_pgtable __initdata = NULL;

static pmd_t * __init kernel_ptr_table(void)
{
	if (!last_pgtable) {
		unsigned long pmd, last;
		int i;

		/* Find the last ptr table that was used in head.S and
		 * reuse the remaining space in that page for further
		 * ptr tables.
		 */
		last = (unsigned long)kernel_pg_dir;
		for (i = 0; i < PTRS_PER_PGD; i++) {
			pud_t *pud = (pud_t *)(&kernel_pg_dir[i]);

			if (!pud_present(*pud))
				continue;
			pmd = pgd_page_vaddr(kernel_pg_dir[i]);
			if (pmd > last)
				last = pmd;
		}

		last_pgtable = (pmd_t *)last;
#ifdef DEBUG
		printk("kernel_ptr_init: %p\n", last_pgtable);
#endif
	}

	last_pgtable += PTRS_PER_PMD;
	if (((unsigned long)last_pgtable & ~PAGE_MASK) == 0) {
		last_pgtable = (pmd_t *)memblock_alloc_low(PAGE_SIZE,
							   PAGE_SIZE);
		if (!last_pgtable)
			panic("%s: Failed to allocate %lu bytes align=%lx\n",
			      __func__, PAGE_SIZE, PAGE_SIZE);

		clear_page(last_pgtable);
		mmu_page_ctor(last_pgtable);
	}

	return last_pgtable;
}

static void __init map_node(int node)
{
#define PTRTREESIZE (256*1024)
#define ROOTTREESIZE (32*1024*1024)
	unsigned long physaddr, virtaddr, size;
	pgd_t *pgd_dir;
	p4d_t *p4d_dir;
	pud_t *pud_dir;
	pmd_t *pmd_dir;
	pte_t *pte_dir;

	size = m68k_memory[node].size;
	physaddr = m68k_memory[node].addr;
	virtaddr = (unsigned long)phys_to_virt(physaddr);
	physaddr |= m68k_supervisor_cachemode |
		    _PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_DIRTY;
	if (CPU_IS_040_OR_060)
		physaddr |= _PAGE_GLOBAL040;

	while (size > 0) {
#ifdef DEBUG
		if (!(virtaddr & (PTRTREESIZE-1)))
			printk ("\npa=%#lx va=%#lx ", physaddr & PAGE_MASK,
				virtaddr);
#endif
		pgd_dir = pgd_offset_k(virtaddr);
		if (virtaddr && CPU_IS_020_OR_030) {
			if (!(virtaddr & (ROOTTREESIZE-1)) &&
			    size >= ROOTTREESIZE) {
#ifdef DEBUG
				printk ("[very early term]");
#endif
				pgd_val(*pgd_dir) = physaddr;
				size -= ROOTTREESIZE;
				virtaddr += ROOTTREESIZE;
				physaddr += ROOTTREESIZE;
				continue;
			}
		}
		p4d_dir = p4d_offset(pgd_dir, virtaddr);
		pud_dir = pud_offset(p4d_dir, virtaddr);
		if (!pud_present(*pud_dir)) {
			pmd_dir = kernel_ptr_table();
#ifdef DEBUG
			printk ("[new pointer %p]", pmd_dir);
#endif
			pud_set(pud_dir, pmd_dir);
		} else
			pmd_dir = pmd_offset(pud_dir, virtaddr);

		if (CPU_IS_020_OR_030) {
			if (virtaddr) {
#ifdef DEBUG
				printk ("[early term]");
#endif
				pmd_dir->pmd[(virtaddr/PTRTREESIZE) & 15] = physaddr;
				physaddr += PTRTREESIZE;
			} else {
				int i;
#ifdef DEBUG
				printk ("[zero map]");
#endif
				zero_pgtable = kernel_ptr_table();
				pte_dir = (pte_t *)zero_pgtable;
				pmd_dir->pmd[0] = virt_to_phys(pte_dir) |
					_PAGE_TABLE | _PAGE_ACCESSED;
				pte_val(*pte_dir++) = 0;
				physaddr += PAGE_SIZE;
				for (i = 1; i < 64; physaddr += PAGE_SIZE, i++)
					pte_val(*pte_dir++) = physaddr;
			}
			size -= PTRTREESIZE;
			virtaddr += PTRTREESIZE;
		} else {
			if (!pmd_present(*pmd_dir)) {
#ifdef DEBUG
				printk ("[new table]");
#endif
				pte_dir = kernel_page_table();
				pmd_set(pmd_dir, pte_dir);
			}
			pte_dir = pte_offset_kernel(pmd_dir, virtaddr);

			if (virtaddr) {
				if (!pte_present(*pte_dir))
					pte_val(*pte_dir) = physaddr;
			} else
				pte_val(*pte_dir) = 0;
			size -= PAGE_SIZE;
			virtaddr += PAGE_SIZE;
			physaddr += PAGE_SIZE;
		}

	}
#ifdef DEBUG
	printk("\n");
#endif
}

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = { 0, };
	unsigned long min_addr, max_addr;
	unsigned long addr;
	int i;

#ifdef DEBUG
	printk ("start of paging_init (%p, %lx)\n", kernel_pg_dir, availmem);
#endif

	/* Fix the cache mode in the page descriptors for the 680[46]0.  */
	if (CPU_IS_040_OR_060) {
		int i;
#ifndef mm_cachebits
		mm_cachebits = _PAGE_CACHE040;
#endif
		for (i = 0; i < 16; i++)
			pgprot_val(protection_map[i]) |= _PAGE_CACHE040;
	}

	min_addr = m68k_memory[0].addr;
	max_addr = min_addr + m68k_memory[0].size;
	memblock_add(m68k_memory[0].addr, m68k_memory[0].size);
	for (i = 1; i < m68k_num_memory;) {
		if (m68k_memory[i].addr < min_addr) {
			printk("Ignoring memory chunk at 0x%lx:0x%lx before the first chunk\n",
				m68k_memory[i].addr, m68k_memory[i].size);
			printk("Fix your bootloader or use a memfile to make use of this area!\n");
			m68k_num_memory--;
			memmove(m68k_memory + i, m68k_memory + i + 1,
				(m68k_num_memory - i) * sizeof(struct m68k_mem_info));
			continue;
		}
		memblock_add(m68k_memory[i].addr, m68k_memory[i].size);
		addr = m68k_memory[i].addr + m68k_memory[i].size;
		if (addr > max_addr)
			max_addr = addr;
		i++;
	}
	m68k_memoffset = min_addr - PAGE_OFFSET;
	m68k_virt_to_node_shift = fls(max_addr - min_addr - 1) - 6;

	module_fixup(NULL, __start_fixup, __stop_fixup);
	flush_icache();

	high_memory = phys_to_virt(max_addr);

	min_low_pfn = availmem >> PAGE_SHIFT;
	max_pfn = max_low_pfn = max_addr >> PAGE_SHIFT;

	/* Reserve kernel text/data/bss and the memory allocated in head.S */
	memblock_reserve(m68k_memory[0].addr, availmem - m68k_memory[0].addr);

	/*
	 * Map the physical memory available into the kernel virtual
	 * address space. Make sure memblock will not try to allocate
	 * pages beyond the memory we already mapped in head.S
	 */
	memblock_set_bottom_up(true);

	for (i = 0; i < m68k_num_memory; i++) {
		m68k_setup_node(i);
		map_node(i);
	}

	flush_tlb_all();

	/*
	 * initialize the bad page table and bad page to point
	 * to a couple of allocated pages
	 */
	empty_zero_page = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!empty_zero_page)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);

	/*
	 * Set up SFC/DFC registers
	 */
	set_fs(KERNEL_DS);

#ifdef DEBUG
	printk ("before free_area_init\n");
#endif
	for (i = 0; i < m68k_num_memory; i++) {
		zones_size[ZONE_DMA] = m68k_memory[i].size >> PAGE_SHIFT;
		free_area_init_node(i, zones_size,
				    m68k_memory[i].addr >> PAGE_SHIFT, NULL);
		if (node_present_pages(i))
			node_set_state(i, N_NORMAL_MEMORY);
	}
}

/*
 *  linux/arch/m68k/mm/memory.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pagemap.h>

#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/machdep.h>


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
#ifdef DEBUG
	printk("init_pointer_table: %lx, %x\n", ptable, PD_MARKBITS(dp));
#endif

	/* unreserve the page so it's possible to free that page */
	PD_PAGE(dp)->flags &= ~(1 << PG_reserved);
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

		flush_tlb_kernel_page(page);
		nocache_page(page);

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
		list_del(dp);
		list_add_tail(dp, &ptable_list);
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
		cache_page((void *)page);
		free_page (page);
		return 1;
	} else if (ptable_list.next != dp) {
		/*
		 * move this descriptor to the front of the list, since
		 * it has one or more free tables.
		 */
		list_del(dp);
		list_add(dp, &ptable_list);
	}
	return 0;
}

#ifdef DEBUG_INVALID_PTOV
int mm_inv_cnt = 5;
#endif

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
/*
 * The following two routines map from a physical address to a kernel
 * virtual address and vice versa.
 */
unsigned long mm_vtop(unsigned long vaddr)
{
	int i=0;
	unsigned long voff = (unsigned long)vaddr - PAGE_OFFSET;

	do {
		if (voff < m68k_memory[i].size) {
#ifdef DEBUGPV
			printk ("VTOP(%p)=%lx\n", vaddr,
				m68k_memory[i].addr + voff);
#endif
			return m68k_memory[i].addr + voff;
		}
		voff -= m68k_memory[i].size;
	} while (++i < m68k_num_memory);

	/* As a special case allow `__pa(high_memory)'.  */
	if (voff == 0)
		return m68k_memory[i-1].addr + m68k_memory[i-1].size;

	return -1;
}
#endif

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
unsigned long mm_ptov (unsigned long paddr)
{
	int i = 0;
	unsigned long poff, voff = PAGE_OFFSET;

	do {
		poff = paddr - m68k_memory[i].addr;
		if (poff < m68k_memory[i].size) {
#ifdef DEBUGPV
			printk ("PTOV(%lx)=%lx\n", paddr, poff + voff);
#endif
			return poff + voff;
		}
		voff += m68k_memory[i].size;
	} while (++i < m68k_num_memory);

#ifdef DEBUG_INVALID_PTOV
	if (mm_inv_cnt > 0) {
		mm_inv_cnt--;
		printk("Invalid use of phys_to_virt(0x%lx) at 0x%p!\n",
			paddr, __builtin_return_address(0));
	}
#endif
	return -1;
}
#endif

/* invalidate page in both caches */
static inline void clear040(unsigned long paddr)
{
	asm volatile (
		"nop\n\t"
		".chip 68040\n\t"
		"cinvp %%bc,(%0)\n\t"
		".chip 68k"
		: : "a" (paddr));
}

/* invalidate page in i-cache */
static inline void cleari040(unsigned long paddr)
{
	asm volatile (
		"nop\n\t"
		".chip 68040\n\t"
		"cinvp %%ic,(%0)\n\t"
		".chip 68k"
		: : "a" (paddr));
}

/* push page in both caches */
/* RZ: cpush %bc DOES invalidate %ic, regardless of DPI */
static inline void push040(unsigned long paddr)
{
	asm volatile (
		"nop\n\t"
		".chip 68040\n\t"
		"cpushp %%bc,(%0)\n\t"
		".chip 68k"
		: : "a" (paddr));
}

/* push and invalidate page in both caches, must disable ints
 * to avoid invalidating valid data */
static inline void pushcl040(unsigned long paddr)
{
	unsigned long flags;

	local_irq_save(flags);
	push040(paddr);
	if (CPU_IS_060)
		clear040(paddr);
	local_irq_restore(flags);
}

/*
 * 040: Hit every page containing an address in the range paddr..paddr+len-1.
 * (Low order bits of the ea of a CINVP/CPUSHP are "don't care"s).
 * Hit every page until there is a page or less to go. Hit the next page,
 * and the one after that if the range hits it.
 */
/* ++roman: A little bit more care is required here: The CINVP instruction
 * invalidates cache entries WITHOUT WRITING DIRTY DATA BACK! So the beginning
 * and the end of the region must be treated differently if they are not
 * exactly at the beginning or end of a page boundary. Else, maybe too much
 * data becomes invalidated and thus lost forever. CPUSHP does what we need:
 * it invalidates the page after pushing dirty data to memory. (Thanks to Jes
 * for discovering the problem!)
 */
/* ... but on the '060, CPUSH doesn't invalidate (for us, since we have set
 * the DPI bit in the CACR; would it cause problems with temporarily changing
 * this?). So we have to push first and then additionally to invalidate.
 */


/*
 * cache_clear() semantics: Clear any cache entries for the area in question,
 * without writing back dirty entries first. This is useful if the data will
 * be overwritten anyway, e.g. by DMA to memory. The range is defined by a
 * _physical_ address.
 */

void cache_clear (unsigned long paddr, int len)
{
    if (CPU_IS_040_OR_060) {
	int tmp;

	/*
	 * We need special treatment for the first page, in case it
	 * is not page-aligned. Page align the addresses to work
	 * around bug I17 in the 68060.
	 */
	if ((tmp = -paddr & (PAGE_SIZE - 1))) {
	    pushcl040(paddr & PAGE_MASK);
	    if ((len -= tmp) <= 0)
		return;
	    paddr += tmp;
	}
	tmp = PAGE_SIZE;
	paddr &= PAGE_MASK;
	while ((len -= tmp) >= 0) {
	    clear040(paddr);
	    paddr += tmp;
	}
	if ((len += tmp))
	    /* a page boundary gets crossed at the end */
	    pushcl040(paddr);
    }
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I_AND_D)
		      : "d0");
#ifdef CONFIG_M68K_L2_CACHE
    if(mach_l2_flush)
	mach_l2_flush(0);
#endif
}


/*
 * cache_push() semantics: Write back any dirty cache data in the given area,
 * and invalidate the range in the instruction cache. It needs not (but may)
 * invalidate those entries also in the data cache. The range is defined by a
 * _physical_ address.
 */

void cache_push (unsigned long paddr, int len)
{
    if (CPU_IS_040_OR_060) {
	int tmp = PAGE_SIZE;

	/*
         * on 68040 or 68060, push cache lines for pages in the range;
	 * on the '040 this also invalidates the pushed lines, but not on
	 * the '060!
	 */
	len += paddr & (PAGE_SIZE - 1);

	/*
	 * Work around bug I17 in the 68060 affecting some instruction
	 * lines not being invalidated properly.
	 */
	paddr &= PAGE_MASK;

	do {
	    push040(paddr);
	    paddr += tmp;
	} while ((len -= tmp) > 0);
    }
    /*
     * 68030/68020 have no writeback cache. On the other hand,
     * cache_push is actually a superset of cache_clear (the lines
     * get written back and invalidated), so we should make sure
     * to perform the corresponding actions. After all, this is getting
     * called in places where we've just loaded code, or whatever, so
     * flushing the icache is appropriate; flushing the dcache shouldn't
     * be required.
     */
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I)
		      : "d0");
#ifdef CONFIG_M68K_L2_CACHE
    if(mach_l2_flush)
	mach_l2_flush(1);
#endif
}

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
int mm_end_of_chunk (unsigned long addr, int len)
{
	int i;

	for (i = 0; i < m68k_num_memory; i++)
		if (m68k_memory[i].addr + m68k_memory[i].size == addr + len)
			return 1;
	return 0;
}
#endif

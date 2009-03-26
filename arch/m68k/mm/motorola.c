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
#include <linux/bootmem.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/system.h>
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

/* size of memory already mapped in head.S */
#define INIT_MAPPED_SIZE	(4UL<<20)

extern unsigned long availmem;

static pte_t * __init kernel_page_table(void)
{
	pte_t *ptablep;

	ptablep = (pte_t *)alloc_bootmem_low_pages(PAGE_SIZE);

	clear_page(ptablep);
	__flush_page_to_ram(ptablep);
	flush_tlb_kernel_page(ptablep);
	nocache_page(ptablep);

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
			if (!pgd_present(kernel_pg_dir[i]))
				continue;
			pmd = __pgd_page(kernel_pg_dir[i]);
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
		last_pgtable = (pmd_t *)alloc_bootmem_low_pages(PAGE_SIZE);

		clear_page(last_pgtable);
		__flush_page_to_ram(last_pgtable);
		flush_tlb_kernel_page(last_pgtable);
		nocache_page(last_pgtable);
	}

	return last_pgtable;
}

static void __init map_node(int node)
{
#define PTRTREESIZE (256*1024)
#define ROOTTREESIZE (32*1024*1024)
	unsigned long physaddr, virtaddr, size;
	pgd_t *pgd_dir;
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
		if (!pgd_present(*pgd_dir)) {
			pmd_dir = kernel_ptr_table();
#ifdef DEBUG
			printk ("[new pointer %p]", pmd_dir);
#endif
			pgd_set(pgd_dir, pmd_dir);
		} else
			pmd_dir = pmd_offset(pgd_dir, virtaddr);

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
	unsigned long addr, size, end;
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
	for (i = 1; i < m68k_num_memory;) {
		if (m68k_memory[i].addr < min_addr) {
			printk("Ignoring memory chunk at 0x%lx:0x%lx before the first chunk\n",
				m68k_memory[i].addr, m68k_memory[i].size);
			printk("Fix your bootloader or use a memfile to make use of this area!\n");
			m68k_num_memory--;
			memmove(m68k_memory + i, m68k_memory + i + 1,
				(m68k_num_memory - i) * sizeof(struct mem_info));
			continue;
		}
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
	max_low_pfn = max_addr >> PAGE_SHIFT;

	for (i = 0; i < m68k_num_memory; i++) {
		addr = m68k_memory[i].addr;
		end = addr + m68k_memory[i].size;
		m68k_setup_node(i);
		availmem = PAGE_ALIGN(availmem);
		availmem += init_bootmem_node(NODE_DATA(i),
					      availmem >> PAGE_SHIFT,
					      addr >> PAGE_SHIFT,
					      end >> PAGE_SHIFT);
	}

	/*
	 * Map the physical memory available into the kernel virtual
	 * address space. First initialize the bootmem allocator with
	 * the memory we already mapped, so map_node() has something
	 * to allocate.
	 */
	addr = m68k_memory[0].addr;
	size = m68k_memory[0].size;
	free_bootmem_node(NODE_DATA(0), availmem, min(INIT_MAPPED_SIZE, size) - (availmem - addr));
	map_node(0);
	if (size > INIT_MAPPED_SIZE)
		free_bootmem_node(NODE_DATA(0), addr + INIT_MAPPED_SIZE, size - INIT_MAPPED_SIZE);

	for (i = 1; i < m68k_num_memory; i++)
		map_node(i);

	flush_tlb_all();

	/*
	 * initialize the bad page table and bad page to point
	 * to a couple of allocated pages
	 */
	empty_zero_page = alloc_bootmem_pages(PAGE_SIZE);

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
	}
}

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)__init_begin;
	for (; addr < (unsigned long)__init_end; addr += PAGE_SIZE) {
		virt_to_page(addr)->flags &= ~(1 << PG_reserved);
		init_page_count(virt_to_page(addr));
		free_page(addr);
		totalram_pages++;
	}
}



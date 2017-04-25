/*
 * linux/arch/m68k/mm/sun3mmu.c
 *
 * Implementations of mm routines specific to the sun3 MMU.
 *
 * Moved here 8/20/1999 Sam Creasey
 *
 */

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
#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/io.h>

extern void mmu_emu_init (unsigned long bootmem_end);

const char bad_pmd_string[] = "Bad pmd in pte_alloc: %08lx\n";

extern unsigned long num_pages;

/* For the sun3 we try to follow the i386 paging_init() more closely */
/* start_mem and end_mem have PAGE_OFFSET added already */
/* now sets up tables using sun3 PTEs rather than i386 as before. --m */
void __init paging_init(void)
{
	pgd_t * pg_dir;
	pte_t * pg_table;
	int i;
	unsigned long address;
	unsigned long next_pgtable;
	unsigned long bootmem_end;
	unsigned long zones_size[MAX_NR_ZONES] = { 0, };
	unsigned long size;

	empty_zero_page = alloc_bootmem_pages(PAGE_SIZE);

	address = PAGE_OFFSET;
	pg_dir = swapper_pg_dir;
	memset (swapper_pg_dir, 0, sizeof (swapper_pg_dir));
	memset (kernel_pg_dir,  0, sizeof (kernel_pg_dir));

	size = num_pages * sizeof(pte_t);
	size = (size + PAGE_SIZE) & ~(PAGE_SIZE-1);

	next_pgtable = (unsigned long)alloc_bootmem_pages(size);
	bootmem_end = (next_pgtable + size + PAGE_SIZE) & PAGE_MASK;

	/* Map whole memory from PAGE_OFFSET (0x0E000000) */
	pg_dir += PAGE_OFFSET >> PGDIR_SHIFT;

	while (address < (unsigned long)high_memory) {
		pg_table = (pte_t *) __pa (next_pgtable);
		next_pgtable += PTRS_PER_PTE * sizeof (pte_t);
		pgd_val(*pg_dir) = (unsigned long) pg_table;
		pg_dir++;

		/* now change pg_table to kernel virtual addresses */
		pg_table = (pte_t *) __va ((unsigned long) pg_table);
		for (i=0; i<PTRS_PER_PTE; ++i, ++pg_table) {
			pte_t pte = pfn_pte(virt_to_pfn(address), PAGE_INIT);
			if (address >= (unsigned long)high_memory)
				pte_val (pte) = 0;
			set_pte (pg_table, pte);
			address += PAGE_SIZE;
		}
	}

	mmu_emu_init(bootmem_end);

	current->mm = NULL;

	/* memory sizing is a hack stolen from motorola.c..  hope it works for us */
	zones_size[ZONE_DMA] = ((unsigned long)high_memory - PAGE_OFFSET) >> PAGE_SHIFT;

	/* I really wish I knew why the following change made things better...  -- Sam */
/*	free_area_init(zones_size); */
	free_area_init_node(0, zones_size,
			    (__pa(PAGE_OFFSET) >> PAGE_SHIFT) + 1, NULL);


}



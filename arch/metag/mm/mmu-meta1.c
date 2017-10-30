/*
 *  Copyright (C) 2005,2006,2007,2008,2009 Imagination Technologies
 *
 * Meta 1 MMU handling code.
 *
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/io.h>

#include <asm/mmu.h>

#define DM3_BASE (LINSYSDIRECT_BASE + (MMCU_DIRECTMAPn_ADDR_SCALE * 3))

/*
 * This contains the physical address of the top level 2k pgd table.
 */
static unsigned long mmu_base_phys;

/*
 * Given a physical address, return a mapped virtual address that can be used
 * to access that location.
 * In practice, we use the DirectMap region to make this happen.
 */
static unsigned long map_addr(unsigned long phys)
{
	static unsigned long dm_base = 0xFFFFFFFF;
	int offset;

	offset = phys - dm_base;

	/* Are we in the current map range ? */
	if ((offset < 0) || (offset >= MMCU_DIRECTMAPn_ADDR_SCALE)) {
		/* Calculate new DM area */
		dm_base = phys & ~(MMCU_DIRECTMAPn_ADDR_SCALE - 1);

		/* Actually map it in! */
		metag_out32(dm_base, MMCU_DIRECTMAP3_ADDR);

		/* And calculate how far into that area our reference is */
		offset = phys - dm_base;
	}

	return DM3_BASE + offset;
}

/*
 * Return the physical address of the base of our pgd table.
 */
static inline unsigned long __get_mmu_base(void)
{
	unsigned long base_phys;
	unsigned int stride;

	if (is_global_space(PAGE_OFFSET))
		stride = 4;
	else
		stride = hard_processor_id();	/* [0..3] */

	base_phys = metag_in32(MMCU_TABLE_PHYS_ADDR);
	base_phys += (0x800 * stride);

	return base_phys;
}

/* Given a virtual address, return the virtual address of the relevant pgd */
static unsigned long pgd_entry_addr(unsigned long virt)
{
	unsigned long pgd_phys;
	unsigned long pgd_virt;

	if (!mmu_base_phys)
		mmu_base_phys = __get_mmu_base();

	/*
	 * Are we trying to map a global address.  If so, then index
	 * the global pgd table instead of our local one.
	 */
	if (is_global_space(virt)) {
		/* Scale into 2gig map */
		virt &= ~0x80000000;
	}

	/* Base of the pgd table plus our 4Meg entry, 4bytes each */
	pgd_phys = mmu_base_phys + ((virt >> PGDIR_SHIFT) * 4);

	pgd_virt = map_addr(pgd_phys);

	return pgd_virt;
}

/* Given a virtual address, return the virtual address of the relevant pte */
static unsigned long pgtable_entry_addr(unsigned long virt)
{
	unsigned long pgtable_phys;
	unsigned long pgtable_virt, pte_virt;

	/* Find the physical address of the 4MB page table*/
	pgtable_phys = metag_in32(pgd_entry_addr(virt)) & MMCU_ENTRY_ADDR_BITS;

	/* Map it to a virtual address */
	pgtable_virt = map_addr(pgtable_phys);

	/* And index into it for our pte */
	pte_virt = pgtable_virt + ((virt >> PAGE_SHIFT) & 0x3FF) * 4;

	return pte_virt;
}

unsigned long mmu_read_first_level_page(unsigned long vaddr)
{
	return metag_in32(pgd_entry_addr(vaddr));
}

unsigned long mmu_read_second_level_page(unsigned long vaddr)
{
	return metag_in32(pgtable_entry_addr(vaddr));
}

unsigned long mmu_get_base(void)
{
	static unsigned long __base;

	/* Find the base of our MMU pgd table */
	if (!__base)
		__base = pgd_entry_addr(0);

	return __base;
}

void __init mmu_init(unsigned long mem_end)
{
	unsigned long entry, addr;
	pgd_t *p_swapper_pg_dir;

	/*
	 * Now copy over any MMU pgd entries already in the mmu page tables
	 * over to our root init process (swapper_pg_dir) map.  This map is
	 * then inherited by all other processes, which means all processes
	 * inherit a map of the kernel space.
	 */
	addr = PAGE_OFFSET;
	entry = pgd_index(PAGE_OFFSET);
	p_swapper_pg_dir = pgd_offset_k(0) + entry;

	while (addr <= META_MEMORY_LIMIT) {
		unsigned long pgd_entry;
		/* copy over the current MMU value */
		pgd_entry = mmu_read_first_level_page(addr);
		pgd_val(*p_swapper_pg_dir) = pgd_entry;

		p_swapper_pg_dir++;
		addr += PGDIR_SIZE;
	}
}

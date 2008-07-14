/*
 * nosun4c.c: This file is a bunch of dummies for SMP compiles, 
 *         so that it does not need sun4c and avoid ifdefs.
 *
 * Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <asm/pgtable.h>

static char shouldnothappen[] __initdata = "32bit SMP kernel only supports sun4m and sun4d\n";

/* Dummies */
struct sun4c_mmu_ring {
	unsigned long xxx1[3];
	unsigned char xxx2[2];
	int xxx3;
};
struct sun4c_mmu_ring sun4c_kernel_ring;
struct sun4c_mmu_ring sun4c_kfree_ring;
unsigned long sun4c_kernel_faults;
unsigned long *sun4c_memerr_reg;

static void __init should_not_happen(void)
{
	prom_printf(shouldnothappen);
	prom_halt();
}

unsigned long __init sun4c_paging_init(unsigned long start_mem, unsigned long end_mem)
{
	should_not_happen();
	return 0;
}

void __init ld_mmu_sun4c(void)
{
	should_not_happen();
}

void sun4c_mapioaddr(unsigned long physaddr, unsigned long virt_addr, int bus_type, int rdonly)
{
}

void sun4c_unmapioaddr(unsigned long virt_addr)
{
}

void sun4c_complete_all_stores(void)
{
}

pte_t *sun4c_pte_offset(pmd_t * dir, unsigned long address)
{
	return NULL;
}

pte_t *sun4c_pte_offset_kernel(pmd_t *dir, unsigned long address)
{
	return NULL;
}

void sun4c_update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
}

void __init sun4c_probe_vac(void)
{
	should_not_happen();
}

void __init sun4c_probe_memerr_reg(void)
{
	should_not_happen();
}

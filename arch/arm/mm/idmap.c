#include <linux/kernel.h>

#include <asm/cputype.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

void identity_mapping_add(pgd_t *pgd, unsigned long addr, unsigned long end)
{
	unsigned long prot;

	prot = PMD_TYPE_SECT | PMD_SECT_AP_WRITE;
	if (cpu_architecture() <= CPU_ARCH_ARMv5TEJ && !cpu_is_xscale())
		prot |= PMD_BIT4;

	for (addr &= PGDIR_MASK; addr < end;) {
		pmd_t *pmd = pmd_offset(pgd + pgd_index(addr), addr);
		pmd[0] = __pmd(addr | prot);
		addr += SECTION_SIZE;
		pmd[1] = __pmd(addr | prot);
		addr += SECTION_SIZE;
		flush_pmd_entry(pmd);
	}
}

#ifdef CONFIG_SMP
void identity_mapping_del(pgd_t *pgd, unsigned long addr, unsigned long end)
{
	for (addr &= PGDIR_MASK; addr < end; addr += PGDIR_SIZE) {
		pmd_t *pmd = pmd_offset(pgd + pgd_index(addr), addr);
		pmd[0] = __pmd(0);
		pmd[1] = __pmd(0);
		clean_pmd_entry(pmd);
	}
}
#endif

/*
 * In order to soft-boot, we need to insert a 1:1 mapping in place of
 * the user-mode pages.  This will then ensure that we have predictable
 * results when turning the mmu off
 */
void setup_mm_for_reboot(char mode)
{
	/*
	 * We need to access to user-mode page tables here. For kernel threads
	 * we don't have any user-mode mappings so we use the context that we
	 * "borrowed".
	 */
	identity_mapping_add(current->active_mm->pgd, 0, TASK_SIZE);
	local_flush_tlb_all();
}

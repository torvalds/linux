#include <linux/kernel.h>

#include <asm/cputype.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

static void idmap_add_pmd(pgd_t *pgd, unsigned long addr, unsigned long end,
	unsigned long prot)
{
	pmd_t *pmd = pmd_offset(pgd, addr);

	addr = (addr & PMD_MASK) | prot;
	pmd[0] = __pmd(addr);
	addr += SECTION_SIZE;
	pmd[1] = __pmd(addr);
	flush_pmd_entry(pmd);
}

void identity_mapping_add(pgd_t *pgd, unsigned long addr, unsigned long end)
{
	unsigned long prot, next;

	prot = PMD_TYPE_SECT | PMD_SECT_AP_WRITE;
	if (cpu_architecture() <= CPU_ARCH_ARMv5TEJ && !cpu_is_xscale())
		prot |= PMD_BIT4;

	pgd += pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		idmap_add_pmd(pgd, addr, next, prot);
	} while (pgd++, addr = next, addr != end);
}

#ifdef CONFIG_SMP
static void idmap_del_pmd(pgd_t *pgd, unsigned long addr, unsigned long end)
{
	pmd_t *pmd = pmd_offset(pgd, addr);
	pmd_clear(pmd);
}

void identity_mapping_del(pgd_t *pgd, unsigned long addr, unsigned long end)
{
	unsigned long next;

	pgd += pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		idmap_del_pmd(pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
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

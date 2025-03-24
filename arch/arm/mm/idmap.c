// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm_types.h>
#include <linux/pgtable.h>

#include <asm/cputype.h>
#include <asm/idmap.h>
#include <asm/hwcap.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/system_info.h>

/*
 * Note: accesses outside of the kernel image and the identity map area
 * are not supported on any CPU using the idmap tables as its current
 * page tables.
 */
pgd_t *idmap_pgd __ro_after_init;
long long arch_phys_to_idmap_offset __ro_after_init;

#ifdef CONFIG_ARM_LPAE
static void idmap_add_pmd(pud_t *pud, unsigned long addr, unsigned long end,
	unsigned long prot)
{
	pmd_t *pmd;
	unsigned long next;

	if (pud_none_or_clear_bad(pud) || (pud_val(*pud) & L_PGD_SWAPPER)) {
		pmd = pmd_alloc_one(&init_mm, addr);
		if (!pmd) {
			pr_warn("Failed to allocate identity pmd.\n");
			return;
		}
		/*
		 * Copy the original PMD to ensure that the PMD entries for
		 * the kernel image are preserved.
		 */
		if (!pud_none(*pud))
			memcpy(pmd, pmd_offset(pud, 0),
			       PTRS_PER_PMD * sizeof(pmd_t));
		pud_populate(&init_mm, pud, pmd);
		pmd += pmd_index(addr);
	} else
		pmd = pmd_offset(pud, addr);

	do {
		next = pmd_addr_end(addr, end);
		*pmd = __pmd((addr & PMD_MASK) | prot);
		flush_pmd_entry(pmd);
	} while (pmd++, addr = next, addr != end);
}
#else	/* !CONFIG_ARM_LPAE */
static void idmap_add_pmd(pud_t *pud, unsigned long addr, unsigned long end,
	unsigned long prot)
{
	pmd_t *pmd = pmd_offset(pud, addr);

	addr = (addr & PMD_MASK) | prot;
	pmd[0] = __pmd(addr);
	addr += SECTION_SIZE;
	pmd[1] = __pmd(addr);
	flush_pmd_entry(pmd);
}
#endif	/* CONFIG_ARM_LPAE */

static void idmap_add_pud(pgd_t *pgd, unsigned long addr, unsigned long end,
	unsigned long prot)
{
	p4d_t *p4d = p4d_offset(pgd, addr);
	pud_t *pud = pud_offset(p4d, addr);
	unsigned long next;

	do {
		next = pud_addr_end(addr, end);
		idmap_add_pmd(pud, addr, next, prot);
	} while (pud++, addr = next, addr != end);
}

static void identity_mapping_add(pgd_t *pgd, const char *text_start,
				 const char *text_end, unsigned long prot)
{
	unsigned long addr, end;
	unsigned long next;

#ifdef CONFIG_XIP_KERNEL
	addr = (phys_addr_t)(text_start) - XIP_VIRT_ADDR(CONFIG_XIP_PHYS_ADDR)
		+ CONFIG_XIP_PHYS_ADDR;
	end = (phys_addr_t)(text_end) - XIP_VIRT_ADDR(CONFIG_XIP_PHYS_ADDR)
		+ CONFIG_XIP_PHYS_ADDR;
#else
	addr = virt_to_idmap(text_start);
	end = virt_to_idmap(text_end);
#endif
	pr_info("Setting up static identity map for 0x%lx - 0x%lx\n", addr, end);

	prot |= PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_SECT_AF;

	if (cpu_architecture() <= CPU_ARCH_ARMv5TEJ && !cpu_is_xscale_family())
		prot |= PMD_BIT4;

	pgd += pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		idmap_add_pud(pgd, addr, next, prot);
	} while (pgd++, addr = next, addr != end);
}

extern char  __idmap_text_start[], __idmap_text_end[];

static int __init init_static_idmap(void)
{
	idmap_pgd = pgd_alloc(&init_mm);
	if (!idmap_pgd)
		return -ENOMEM;

	identity_mapping_add(idmap_pgd, __idmap_text_start,
			     __idmap_text_end, 0);

	/* Flush L1 for the hardware to see this page table content */
	if (!(elf_hwcap & HWCAP_LPAE))
		flush_cache_louis();

	return 0;
}
early_initcall(init_static_idmap);

/*
 * In order to soft-boot, we need to switch to a 1:1 mapping for the
 * cpu_reset functions. This will then ensure that we have predictable
 * results when turning off the mmu.
 */
void setup_mm_for_reboot(void)
{
	/* Switch to the identity mapping. */
	cpu_switch_mm(idmap_pgd, &init_mm);
	local_flush_bp_all();

#ifdef CONFIG_CPU_HAS_ASID
	/*
	 * We don't have a clean ASID for the identity mapping, which
	 * may clash with virtual addresses of the previous page tables
	 * and therefore potentially in the TLB.
	 */
	local_flush_tlb_all();
#endif
}

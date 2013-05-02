#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <asm/cputype.h>
#include <asm/idmap.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/system_info.h>
#include <asm/virt.h>

pgd_t *idmap_pgd;

#ifdef CONFIG_ARM_LPAE
static void idmap_add_pmd(pud_t *pud, unsigned long addr, unsigned long end,
	unsigned long prot)
{
	pmd_t *pmd;
	unsigned long next;

	if (pud_none_or_clear_bad(pud) || (pud_val(*pud) & L_PGD_SWAPPER)) {
		pmd = pmd_alloc_one(&init_mm, addr);
		if (!pmd) {
			pr_warning("Failed to allocate identity pmd.\n");
			return;
		}
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
	pud_t *pud = pud_offset(pgd, addr);
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

	addr = virt_to_phys(text_start);
	end = virt_to_phys(text_end);

	prot |= PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_SECT_AF;

	if (cpu_architecture() <= CPU_ARCH_ARMv5TEJ && !cpu_is_xscale())
		prot |= PMD_BIT4;

	pgd += pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		idmap_add_pud(pgd, addr, next, prot);
	} while (pgd++, addr = next, addr != end);
}

#if defined(CONFIG_ARM_VIRT_EXT) && defined(CONFIG_ARM_LPAE)
pgd_t *hyp_pgd;

extern char  __hyp_idmap_text_start[], __hyp_idmap_text_end[];

static int __init init_static_idmap_hyp(void)
{
	hyp_pgd = kzalloc(PTRS_PER_PGD * sizeof(pgd_t), GFP_KERNEL);
	if (!hyp_pgd)
		return -ENOMEM;

	pr_info("Setting up static HYP identity map for 0x%p - 0x%p\n",
		__hyp_idmap_text_start, __hyp_idmap_text_end);
	identity_mapping_add(hyp_pgd, __hyp_idmap_text_start,
			     __hyp_idmap_text_end, PMD_SECT_AP1);

	return 0;
}
#else
static int __init init_static_idmap_hyp(void)
{
	return 0;
}
#endif

extern char  __idmap_text_start[], __idmap_text_end[];

static int __init init_static_idmap(void)
{
	int ret;

	idmap_pgd = pgd_alloc(&init_mm);
	if (!idmap_pgd)
		return -ENOMEM;

	pr_info("Setting up static identity map for 0x%p - 0x%p\n",
		__idmap_text_start, __idmap_text_end);
	identity_mapping_add(idmap_pgd, __idmap_text_start,
			     __idmap_text_end, 0);

	ret = init_static_idmap_hyp();

	/* Flush L1 for the hardware to see this page table content */
	flush_cache_louis();

	return ret;
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

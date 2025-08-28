// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pgtable.h>

#include <asm/init.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/sev.h>

extern pmd_t early_dynamic_pgts[EARLY_DYNAMIC_PAGE_TABLES][PTRS_PER_PMD];
extern unsigned int next_early_pgt;

static inline bool check_la57_support(void)
{
	/*
	 * 5-level paging is detected and enabled at kernel decompression
	 * stage. Only check if it has been enabled there.
	 */
	if (!(native_read_cr4() & X86_CR4_LA57))
		return false;

	__pgtable_l5_enabled	= 1;
	pgdir_shift		= 48;
	ptrs_per_p4d		= 512;

	return true;
}

static unsigned long __init sme_postprocess_startup(struct boot_params *bp,
						    pmdval_t *pmd,
						    unsigned long p2v_offset)
{
	unsigned long paddr, paddr_end;
	int i;

	/* Encrypt the kernel and related (if SME is active) */
	sme_encrypt_kernel(bp);

	/*
	 * Clear the memory encryption mask from the .bss..decrypted section.
	 * The bss section will be memset to zero later in the initialization so
	 * there is no need to zero it after changing the memory encryption
	 * attribute.
	 */
	if (sme_get_me_mask()) {
		paddr = (unsigned long)rip_rel_ptr(__start_bss_decrypted);
		paddr_end = (unsigned long)rip_rel_ptr(__end_bss_decrypted);

		for (; paddr < paddr_end; paddr += PMD_SIZE) {
			/*
			 * On SNP, transition the page to shared in the RMP table so that
			 * it is consistent with the page table attribute change.
			 *
			 * __start_bss_decrypted has a virtual address in the high range
			 * mapping (kernel .text). PVALIDATE, by way of
			 * early_snp_set_memory_shared(), requires a valid virtual
			 * address but the kernel is currently running off of the identity
			 * mapping so use the PA to get a *currently* valid virtual address.
			 */
			early_snp_set_memory_shared(paddr, paddr, PTRS_PER_PMD);

			i = pmd_index(paddr - p2v_offset);
			pmd[i] -= sme_get_me_mask();
		}
	}

	/*
	 * Return the SME encryption mask (if SME is active) to be used as a
	 * modifier for the initial pgdir entry programmed into CR3.
	 */
	return sme_get_me_mask();
}

/*
 * This code is compiled using PIC codegen because it will execute from the
 * early 1:1 mapping of memory, which deviates from the mapping expected by the
 * linker. Due to this deviation, taking the address of a global variable will
 * produce an ambiguous result when using the plain & operator.  Instead,
 * rip_rel_ptr() must be used, which will return the RIP-relative address in
 * the 1:1 mapping of memory. Kernel virtual addresses can be determined by
 * subtracting p2v_offset from the RIP-relative address.
 */
unsigned long __init __startup_64(unsigned long p2v_offset,
				  struct boot_params *bp)
{
	pmd_t (*early_pgts)[PTRS_PER_PMD] = rip_rel_ptr(early_dynamic_pgts);
	unsigned long physaddr = (unsigned long)rip_rel_ptr(_text);
	unsigned long va_text, va_end;
	unsigned long pgtable_flags;
	unsigned long load_delta;
	pgdval_t *pgd;
	p4dval_t *p4d;
	pudval_t *pud;
	pmdval_t *pmd, pmd_entry;
	bool la57;
	int i;

	la57 = check_la57_support();

	/* Is the address too large? */
	if (physaddr >> MAX_PHYSMEM_BITS)
		for (;;);

	/*
	 * Compute the delta between the address I am compiled to run at
	 * and the address I am actually running at.
	 */
	phys_base = load_delta = __START_KERNEL_map + p2v_offset;

	/* Is the address not 2M aligned? */
	if (load_delta & ~PMD_MASK)
		for (;;);

	va_text = physaddr - p2v_offset;
	va_end  = (unsigned long)rip_rel_ptr(_end) - p2v_offset;

	/* Include the SME encryption mask in the fixup value */
	load_delta += sme_get_me_mask();

	/* Fixup the physical addresses in the page table */

	pgd = rip_rel_ptr(early_top_pgt);
	pgd[pgd_index(__START_KERNEL_map)] += load_delta;

	if (la57) {
		p4d = (p4dval_t *)rip_rel_ptr(level4_kernel_pgt);
		p4d[MAX_PTRS_PER_P4D - 1] += load_delta;

		pgd[pgd_index(__START_KERNEL_map)] = (pgdval_t)p4d | _PAGE_TABLE;
	}

	level3_kernel_pgt[PTRS_PER_PUD - 2].pud += load_delta;
	level3_kernel_pgt[PTRS_PER_PUD - 1].pud += load_delta;

	for (i = FIXMAP_PMD_TOP; i > FIXMAP_PMD_TOP - FIXMAP_PMD_NUM; i--)
		level2_fixmap_pgt[i].pmd += load_delta;

	/*
	 * Set up the identity mapping for the switchover.  These
	 * entries should *NOT* have the global bit set!  This also
	 * creates a bunch of nonsense entries but that is fine --
	 * it avoids problems around wraparound.
	 */

	pud = &early_pgts[0]->pmd;
	pmd = &early_pgts[1]->pmd;
	next_early_pgt = 2;

	pgtable_flags = _KERNPG_TABLE_NOENC + sme_get_me_mask();

	if (la57) {
		p4d = &early_pgts[next_early_pgt++]->pmd;

		i = (physaddr >> PGDIR_SHIFT) % PTRS_PER_PGD;
		pgd[i + 0] = (pgdval_t)p4d + pgtable_flags;
		pgd[i + 1] = (pgdval_t)p4d + pgtable_flags;

		i = physaddr >> P4D_SHIFT;
		p4d[(i + 0) % PTRS_PER_P4D] = (pgdval_t)pud + pgtable_flags;
		p4d[(i + 1) % PTRS_PER_P4D] = (pgdval_t)pud + pgtable_flags;
	} else {
		i = (physaddr >> PGDIR_SHIFT) % PTRS_PER_PGD;
		pgd[i + 0] = (pgdval_t)pud + pgtable_flags;
		pgd[i + 1] = (pgdval_t)pud + pgtable_flags;
	}

	i = physaddr >> PUD_SHIFT;
	pud[(i + 0) % PTRS_PER_PUD] = (pudval_t)pmd + pgtable_flags;
	pud[(i + 1) % PTRS_PER_PUD] = (pudval_t)pmd + pgtable_flags;

	pmd_entry = __PAGE_KERNEL_LARGE_EXEC & ~_PAGE_GLOBAL;
	pmd_entry += sme_get_me_mask();
	pmd_entry +=  physaddr;

	for (i = 0; i < DIV_ROUND_UP(va_end - va_text, PMD_SIZE); i++) {
		int idx = i + (physaddr >> PMD_SHIFT);

		pmd[idx % PTRS_PER_PMD] = pmd_entry + i * PMD_SIZE;
	}

	/*
	 * Fixup the kernel text+data virtual addresses. Note that
	 * we might write invalid pmds, when the kernel is relocated
	 * cleanup_highmap() fixes this up along with the mappings
	 * beyond _end.
	 *
	 * Only the region occupied by the kernel image has so far
	 * been checked against the table of usable memory regions
	 * provided by the firmware, so invalidate pages outside that
	 * region. A page table entry that maps to a reserved area of
	 * memory would allow processor speculation into that area,
	 * and on some hardware (particularly the UV platform) even
	 * speculative access to some reserved areas is caught as an
	 * error, causing the BIOS to halt the system.
	 */

	pmd = rip_rel_ptr(level2_kernel_pgt);

	/* invalidate pages before the kernel image */
	for (i = 0; i < pmd_index(va_text); i++)
		pmd[i] &= ~_PAGE_PRESENT;

	/* fixup pages that are part of the kernel image */
	for (; i <= pmd_index(va_end); i++)
		if (pmd[i] & _PAGE_PRESENT)
			pmd[i] += load_delta;

	/* invalidate pages after the kernel image */
	for (; i < PTRS_PER_PMD; i++)
		pmd[i] &= ~_PAGE_PRESENT;

	return sme_postprocess_startup(bp, pmd, p2v_offset);
}

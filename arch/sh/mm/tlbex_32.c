/*
 * TLB miss handler for SH with an MMU.
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2003 - 2012  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <asm/mmu_context.h>
#include <asm/thread_info.h>
#include <asm/tlb.h>

/*
 * Called with interrupts disabled.
 */
asmlinkage int __kprobes
handle_tlbmiss(struct pt_regs *regs, unsigned long error_code,
	       unsigned long address)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;

	/*
	 * We don't take page faults for P1, P2, and parts of P4, these
	 * are always mapped, whether it be due to legacy behaviour in
	 * 29-bit mode, or due to PMB configuration in 32-bit mode.
	 */
	if (address >= P3SEG && address < P3_ADDR_MAX) {
		pgd = pgd_offset_k(address);
	} else {
		if (unlikely(address >= TASK_SIZE || !current->mm))
			return 1;

		pgd = pgd_offset(current->mm, address);
	}

	p4d = p4d_offset(pgd, address);
	if (p4d_none_or_clear_bad(p4d))
		return 1;
	pud = pud_offset(p4d, address);
	if (pud_none_or_clear_bad(pud))
		return 1;
	pmd = pmd_offset(pud, address);
	if (pmd_none_or_clear_bad(pmd))
		return 1;
	pte = pte_offset_kernel(pmd, address);
	entry = *pte;
	if (unlikely(pte_none(entry) || pte_not_present(entry)))
		return 1;
	if (unlikely(error_code && !pte_write(entry)))
		return 1;

	if (error_code)
		entry = pte_mkdirty(entry);
	entry = pte_mkyoung(entry);

	set_pte(pte, entry);

#if defined(CONFIG_CPU_SH4) && !defined(CONFIG_SMP)
	/*
	 * SH-4 does not set MMUCR.RC to the corresponding TLB entry in
	 * the case of an initial page write exception, so we need to
	 * flush it in order to avoid potential TLB entry duplication.
	 */
	if (error_code == FAULT_CODE_INITIAL)
		local_flush_tlb_one(get_asid(), address & PAGE_MASK);
#endif

	set_thread_fault_code(error_code);
	update_mmu_cache(NULL, address, pte);

	return 0;
}

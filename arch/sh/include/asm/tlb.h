/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_TLB_H
#define __ASM_SH_TLB_H

#ifndef __ASSEMBLER__
#include <linux/pagemap.h>
#include <asm-generic/tlb.h>

#ifdef CONFIG_MMU
#include <linux/swap.h>

#if defined(CONFIG_CPU_SH4)
extern void tlb_wire_entry(struct vm_area_struct *, unsigned long, pte_t);
extern void tlb_unwire_entry(void);
#else
static inline void tlb_wire_entry(struct vm_area_struct *vma ,
				  unsigned long addr, pte_t pte)
{
	BUG();
}

static inline void tlb_unwire_entry(void)
{
	BUG();
}
#endif /* CONFIG_CPU_SH4 */

asmlinkage int handle_tlbmiss(struct pt_regs *regs, unsigned long error_code,
			      unsigned long address);

#endif /* CONFIG_MMU */
#endif /* __ASSEMBLER__ */
#endif /* __ASM_SH_TLB_H */

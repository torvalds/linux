/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_TLB_H
#define __ASM_SH_TLB_H

#ifdef CONFIG_SUPERH64
# include <asm/tlb_64.h>
#endif

#ifndef __ASSEMBLY__
#include <linux/pagemap.h>

#ifdef CONFIG_MMU
#include <linux/swap.h>

#include <asm-generic/tlb.h>

#if defined(CONFIG_CPU_SH4) || defined(CONFIG_SUPERH64)
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
#endif

#else /* CONFIG_MMU */

#include <asm-generic/tlb.h>

#endif /* CONFIG_MMU */
#endif /* __ASSEMBLY__ */
#endif /* __ASM_SH_TLB_H */

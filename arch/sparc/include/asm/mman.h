/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPARC_MMAN_H__
#define __SPARC_MMAN_H__

#include <uapi/asm/mman.h>

#ifndef __ASSEMBLY__
#define arch_mmap_check(addr,len,flags)	sparc_mmap_check(addr,len)
int sparc_mmap_check(unsigned long addr, unsigned long len);

#ifdef CONFIG_SPARC64
#include <asm/adi_64.h>

static inline void ipi_set_tstate_mcde(void *arg)
{
	struct mm_struct *mm = arg;

	/* Set TSTATE_MCDE for the task using address map that ADI has been
	 * enabled on if the task is running. If not, it will be set
	 * automatically at the next context switch
	 */
	if (current->mm == mm) {
		struct pt_regs *regs;

		regs = task_pt_regs(current);
		regs->tstate |= TSTATE_MCDE;
	}
}

#define arch_calc_vm_prot_bits(prot, pkey) sparc_calc_vm_prot_bits(prot)
static inline unsigned long sparc_calc_vm_prot_bits(unsigned long prot)
{
	if (adi_capable() && (prot & PROT_ADI)) {
		struct pt_regs *regs;

		if (!current->mm->context.adi) {
			regs = task_pt_regs(current);
			regs->tstate |= TSTATE_MCDE;
			current->mm->context.adi = true;
			on_each_cpu_mask(mm_cpumask(current->mm),
					 ipi_set_tstate_mcde, current->mm, 0);
		}
		return VM_SPARC_ADI;
	} else {
		return 0;
	}
}

#define arch_vm_get_page_prot(vm_flags) sparc_vm_get_page_prot(vm_flags)
static inline pgprot_t sparc_vm_get_page_prot(unsigned long vm_flags)
{
	return (vm_flags & VM_SPARC_ADI) ? __pgprot(_PAGE_MCD_4V) : __pgprot(0);
}

#define arch_validate_prot(prot, addr) sparc_validate_prot(prot, addr)
static inline int sparc_validate_prot(unsigned long prot, unsigned long addr)
{
	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_SEM | PROT_ADI))
		return 0;
	if (prot & PROT_ADI) {
		if (!adi_capable())
			return 0;

		if (addr) {
			struct vm_area_struct *vma;

			vma = find_vma(current->mm, addr);
			if (vma) {
				/* ADI can not be enabled on PFN
				 * mapped pages
				 */
				if (vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP))
					return 0;

				/* Mergeable pages can become unmergeable
				 * if ADI is enabled on them even if they
				 * have identical data on them. This can be
				 * because ADI enabled pages with identical
				 * data may still not have identical ADI
				 * tags on them. Disallow ADI on mergeable
				 * pages.
				 */
				if (vma->vm_flags & VM_MERGEABLE)
					return 0;
			}
		}
	}
	return 1;
}
#endif /* CONFIG_SPARC64 */

#endif /* __ASSEMBLY__ */
#endif /* __SPARC_MMAN_H__ */

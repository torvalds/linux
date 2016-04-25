#ifndef _ASM_POWERPC_BOOK3S_PGTABLE_H
#define _ASM_POWERPC_BOOK3S_PGTABLE_H

#ifdef CONFIG_PPC64
#include <asm/book3s/64/pgtable.h>
#else
#include <asm/book3s/32/pgtable.h>
#endif

#define FIRST_USER_ADDRESS	0UL
#ifndef __ASSEMBLY__
/* Insert a PTE, top-level function is out of line. It uses an inline
 * low level function in the respective pgtable-* files
 */
extern void set_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
		       pte_t pte);


#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
extern int ptep_set_access_flags(struct vm_area_struct *vma, unsigned long address,
				 pte_t *ptep, pte_t entry, int dirty);

struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);
#define __HAVE_PHYS_MEM_ACCESS_PROT

#endif /* __ASSEMBLY__ */
#endif

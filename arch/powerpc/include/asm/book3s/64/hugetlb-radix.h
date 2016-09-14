#ifndef _ASM_POWERPC_BOOK3S_64_HUGETLB_RADIX_H
#define _ASM_POWERPC_BOOK3S_64_HUGETLB_RADIX_H
/*
 * For radix we want generic code to handle hugetlb. But then if we want
 * both hash and radix to be enabled together we need to workaround the
 * limitations.
 */
void radix__flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
void radix__local_flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern unsigned long
radix__hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
				unsigned long len, unsigned long pgoff,
				unsigned long flags);
#endif

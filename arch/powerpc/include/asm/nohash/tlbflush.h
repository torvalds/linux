/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_TLBFLUSH_H
#define _ASM_POWERPC_NOHASH_TLBFLUSH_H

/*
 * TLB flushing:
 *
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - local_flush_tlb_mm(mm, full) flushes the specified mm context on
 *                           the local processor
 *  - local_flush_tlb_page(vma, vmaddr) flushes one page on the local processor
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *
 */

/*
 * TLB flushing for software loaded TLB chips
 *
 * TODO: (CONFIG_PPC_85xx) determine if flush_tlb_range &
 * flush_tlb_kernel_range are best implemented as tlbia vs
 * specific tlbie's
 */

struct vm_area_struct;
struct mm_struct;

#define MMU_NO_CONTEXT      	((unsigned int)-1)

extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);

#ifdef CONFIG_PPC_8xx
static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
	unsigned int pid = READ_ONCE(mm->context.id);

	if (pid != MMU_NO_CONTEXT)
		asm volatile ("sync; tlbia; isync" : : : "memory");
}

static inline void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	asm volatile ("tlbie %0; sync" : : "r" (vmaddr) : "memory");
}

static inline void local_flush_tlb_page_psize(struct mm_struct *mm,
					      unsigned long vmaddr, int psize)
{
	asm volatile ("tlbie %0; sync" : : "r" (vmaddr) : "memory");
}

static inline void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	start &= PAGE_MASK;

	if (end - start <= PAGE_SIZE)
		asm volatile ("tlbie %0; sync" : : "r" (start) : "memory");
	else
		asm volatile ("sync; tlbia; isync" : : : "memory");
}
#else
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);
extern void local_flush_tlb_mm(struct mm_struct *mm);
extern void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
void local_flush_tlb_page_psize(struct mm_struct *mm, unsigned long vmaddr, int psize);

extern void __local_flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
				   int tsize, int ind);
#endif

#ifdef CONFIG_SMP
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void __flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
			     int tsize, int ind);
#else
#define flush_tlb_mm(mm)		local_flush_tlb_mm(mm)
#define flush_tlb_page(vma,addr)	local_flush_tlb_page(vma,addr)
#define __flush_tlb_page(mm,addr,p,i)	__local_flush_tlb_page(mm,addr,p,i)
#endif

#endif /* _ASM_POWERPC_NOHASH_TLBFLUSH_H */

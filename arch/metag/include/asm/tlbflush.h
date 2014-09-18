#ifndef __ASM_METAG_TLBFLUSH_H
#define __ASM_METAG_TLBFLUSH_H

#include <linux/io.h>
#include <linux/sched.h>
#include <asm/metag_mem.h>
#include <asm/pgalloc.h>

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 *
 * FIXME: Meta 2 can flush single TLB entries.
 *
 */

#if defined(CONFIG_METAG_META21) && !defined(CONFIG_SMP)
static inline void __flush_tlb(void)
{
	/* flush TLB entries for just the current hardware thread */
	int thread = hard_processor_id();
	metag_out32(0, (LINSYSCFLUSH_TxMMCU_BASE +
			LINSYSCFLUSH_TxMMCU_STRIDE * thread));
}
#else
static inline void __flush_tlb(void)
{
	/* flush TLB entries for all hardware threads */
	metag_out32(0, LINSYSCFLUSH_MMCU);
}
#endif /* defined(CONFIG_METAG_META21) && !defined(CONFIG_SMP) */

#define flush_tlb() __flush_tlb()

#define flush_tlb_all() __flush_tlb()

#define local_flush_tlb_all() __flush_tlb()

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->active_mm)
		__flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long addr)
{
	flush_tlb_mm(vma->vm_mm);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	flush_tlb_mm(vma->vm_mm);
}

static inline void flush_tlb_pgtables(struct mm_struct *mm,
				      unsigned long start, unsigned long end)
{
	flush_tlb_mm(mm);
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	flush_tlb_all();
}

#endif /* __ASM_METAG_TLBFLUSH_H */


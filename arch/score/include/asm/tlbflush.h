#ifndef _ASM_SCORE_TLBFLUSH_H
#define _ASM_SCORE_TLBFLUSH_H

#include <linux/mm.h>

/*
 * TLB flushing:
 *
 * - flush_tlb_all() flushes all processes TLB entries
 * - flush_tlb_mm(mm) flushes the specified mm context TLB entries
 * - flush_tlb_page(vma, vmaddr) flushes one page
 * - flush_tlb_range(vma, start, end) flushes a range of pages
 * - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 */
extern void local_flush_tlb_all(void);
extern void local_flush_tlb_mm(struct mm_struct *mm);
extern void local_flush_tlb_range(struct vm_area_struct *vma,
	unsigned long start, unsigned long end);
extern void local_flush_tlb_kernel_range(unsigned long start,
	unsigned long end);
extern void local_flush_tlb_page(struct vm_area_struct *vma,
	unsigned long page);
extern void local_flush_tlb_one(unsigned long vaddr);

#define flush_tlb_all()			local_flush_tlb_all()
#define flush_tlb_mm(mm)		local_flush_tlb_mm(mm)
#define flush_tlb_range(vma, vmaddr, end) \
	local_flush_tlb_range(vma, vmaddr, end)
#define flush_tlb_kernel_range(vmaddr, end) \
	local_flush_tlb_kernel_range(vmaddr, end)
#define flush_tlb_page(vma, page)	local_flush_tlb_page(vma, page)
#define flush_tlb_one(vaddr)		local_flush_tlb_one(vaddr)

#ifndef __ASSEMBLY__

static inline unsigned long pevn_get(void)
{
	unsigned long val;

	__asm__ __volatile__(
		"mfcr %0, cr11\n"
		"nop\nnop\n"
		: "=r" (val));

	return val;
}

static inline void pevn_set(unsigned long val)
{
	__asm__ __volatile__(
		"mtcr %0, cr11\n"
		"nop\nnop\nnop\nnop\nnop\n"
	: : "r" (val));
}

static inline void pectx_set(unsigned long val)
{
	__asm__ __volatile__(
		"mtcr %0, cr12\n"
		"nop\nnop\nnop\nnop\nnop\n"
	: : "r" (val));
}

static inline unsigned long pectx_get(void)
{
	unsigned long val;
	__asm__ __volatile__(
		"mfcr %0, cr12\n"
		"nop\nnop\n"
	: "=r" (val));
	return val;
}
static inline unsigned long tlblock_get(void)
{
	unsigned long val;

	__asm__ __volatile__(
		"mfcr %0, cr7\n"
		"nop\nnop\n"
	: "=r" (val));
	return val;
}
static inline void tlblock_set(unsigned long val)
{
	__asm__ __volatile__(
		"mtcr %0, cr7\n"
		"nop\nnop\nnop\nnop\nnop\n"
	: : "r" (val));
}

static inline void tlbpt_set(unsigned long val)
{
	__asm__ __volatile__(
		"mtcr %0, cr8\n"
		"nop\nnop\nnop\nnop\nnop\n"
		: : "r" (val));
}

static inline long tlbpt_get(void)
{
	long val;

	__asm__ __volatile__(
		"mfcr %0, cr8\n"
		"nop\nnop\n"
		: "=r" (val));

	return val;
}

static inline void peaddr_set(unsigned long val)
{
	__asm__ __volatile__(
		"mtcr %0, cr9\n"
		"nop\nnop\nnop\nnop\nnop\n"
		: : "r" (val));
}

/* TLB operations. */
static inline void tlb_probe(void)
{
	__asm__ __volatile__("stlb;nop;nop;nop;nop;nop");
}

static inline void tlb_read(void)
{
	__asm__ __volatile__("mftlb;nop;nop;nop;nop;nop");
}

static inline void tlb_write_indexed(void)
{
	__asm__ __volatile__("mtptlb;nop;nop;nop;nop;nop");
}

static inline void tlb_write_random(void)
{
	__asm__ __volatile__("mtrtlb;nop;nop;nop;nop;nop");
}

#endif /* Not __ASSEMBLY__ */

#endif /* _ASM_SCORE_TLBFLUSH_H */

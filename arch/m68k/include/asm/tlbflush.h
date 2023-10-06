/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_TLBFLUSH_H
#define _M68K_TLBFLUSH_H

#ifdef CONFIG_MMU
#ifndef CONFIG_SUN3

#include <asm/current.h>
#include <asm/mcfmmu.h>

static inline void flush_tlb_kernel_page(void *addr)
{
	if (CPU_IS_COLDFIRE) {
		mmu_write(MMUOR, MMUOR_CNL);
	} else if (CPU_IS_040_OR_060) {
		set_fc(SUPER_DATA);
		__asm__ __volatile__(".chip 68040\n\t"
				     "pflush (%0)\n\t"
				     ".chip 68k"
				     : : "a" (addr));
		set_fc(USER_DATA);
	} else if (CPU_IS_020_OR_030)
		__asm__ __volatile__("pflush #4,#4,(%0)" : : "a" (addr));
}

/*
 * flush all user-space atc entries.
 */
static inline void __flush_tlb(void)
{
	if (CPU_IS_COLDFIRE) {
		mmu_write(MMUOR, MMUOR_CNL);
	} else if (CPU_IS_040_OR_060) {
		__asm__ __volatile__(".chip 68040\n\t"
				     "pflushan\n\t"
				     ".chip 68k");
	} else if (CPU_IS_020_OR_030) {
		__asm__ __volatile__("pflush #0,#4");
	}
}

static inline void __flush_tlb040_one(unsigned long addr)
{
	__asm__ __volatile__(".chip 68040\n\t"
			     "pflush (%0)\n\t"
			     ".chip 68k"
			     : : "a" (addr));
}

static inline void __flush_tlb_one(unsigned long addr)
{
	if (CPU_IS_COLDFIRE)
		mmu_write(MMUOR, MMUOR_CNL);
	else if (CPU_IS_040_OR_060)
		__flush_tlb040_one(addr);
	else if (CPU_IS_020_OR_030)
		__asm__ __volatile__("pflush #0,#4,(%0)" : : "a" (addr));
}

#define flush_tlb() __flush_tlb()

/*
 * flush all atc entries (both kernel and user-space entries).
 */
static inline void flush_tlb_all(void)
{
	if (CPU_IS_COLDFIRE) {
		mmu_write(MMUOR, MMUOR_CNL);
	} else if (CPU_IS_040_OR_060) {
		__asm__ __volatile__(".chip 68040\n\t"
				     "pflusha\n\t"
				     ".chip 68k");
	} else if (CPU_IS_020_OR_030) {
		__asm__ __volatile__("pflusha");
	}
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->active_mm)
		__flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb_one(addr);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb();
}

static inline void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	flush_tlb_all();
}

#else


/* Reserved PMEGs. */
extern char sun3_reserved_pmeg[SUN3_PMEGS_NUM];
extern unsigned long pmeg_vaddr[SUN3_PMEGS_NUM];
extern unsigned char pmeg_alloc[SUN3_PMEGS_NUM];
extern unsigned char pmeg_ctx[SUN3_PMEGS_NUM];

/* Flush all userspace mappings one by one...  (why no flush command,
   sun?) */
static inline void flush_tlb_all(void)
{
	unsigned long addr;
	unsigned char ctx, oldctx;

	oldctx = sun3_get_context();
	for (addr = 0x00000000; addr < TASK_SIZE; addr += SUN3_PMEG_SIZE) {
		for (ctx = 0; ctx < 8; ctx++) {
			sun3_put_context(ctx);
			sun3_put_segmap(addr, SUN3_INVALID_PMEG);
		}
	}

	sun3_put_context(oldctx);
	/* erase all of the userspace pmeg maps, we've clobbered them
	   all anyway */
	for (addr = 0; addr < SUN3_INVALID_PMEG; addr++) {
		if (pmeg_alloc[addr] == 1) {
			pmeg_alloc[addr] = 0;
			pmeg_ctx[addr] = 0;
			pmeg_vaddr[addr] = 0;
		}
	}
}

/* Clear user TLB entries within the context named in mm */
static inline void flush_tlb_mm (struct mm_struct *mm)
{
	unsigned char oldctx;
	unsigned char seg;
	unsigned long i;

	oldctx = sun3_get_context();
	sun3_put_context(mm->context);

	for (i = 0; i < TASK_SIZE; i += SUN3_PMEG_SIZE) {
		seg = sun3_get_segmap(i);
		if (seg == SUN3_INVALID_PMEG)
			continue;

		sun3_put_segmap(i, SUN3_INVALID_PMEG);
		pmeg_alloc[seg] = 0;
		pmeg_ctx[seg] = 0;
		pmeg_vaddr[seg] = 0;
	}

	sun3_put_context(oldctx);
}

/* Flush a single TLB page. In this case, we're limited to flushing a
   single PMEG */
static inline void flush_tlb_page (struct vm_area_struct *vma,
				   unsigned long addr)
{
	unsigned char oldctx;
	unsigned char i;

	oldctx = sun3_get_context();
	sun3_put_context(vma->vm_mm->context);
	addr &= ~SUN3_PMEG_MASK;
	if((i = sun3_get_segmap(addr)) != SUN3_INVALID_PMEG)
	{
		pmeg_alloc[i] = 0;
		pmeg_ctx[i] = 0;
		pmeg_vaddr[i] = 0;
		sun3_put_segmap (addr,  SUN3_INVALID_PMEG);
	}
	sun3_put_context(oldctx);

}
/* Flush a range of pages from TLB. */

static inline void flush_tlb_range (struct vm_area_struct *vma,
		      unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned char seg, oldctx;

	start &= ~SUN3_PMEG_MASK;

	oldctx = sun3_get_context();
	sun3_put_context(mm->context);

	while(start < end)
	{
		if((seg = sun3_get_segmap(start)) == SUN3_INVALID_PMEG)
		     goto next;
		if(pmeg_ctx[seg] == mm->context) {
			pmeg_alloc[seg] = 0;
			pmeg_ctx[seg] = 0;
			pmeg_vaddr[seg] = 0;
		}
		sun3_put_segmap(start, SUN3_INVALID_PMEG);
	next:
		start += SUN3_PMEG_SIZE;
	}
	sun3_put_context(oldctx);
}

static inline void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	flush_tlb_all();
}

/* Flush kernel page from TLB. */
static inline void flush_tlb_kernel_page (unsigned long addr)
{
	sun3_put_segmap (addr & ~(SUN3_PMEG_SIZE - 1), SUN3_INVALID_PMEG);
}

#endif

#else /* !CONFIG_MMU */

/*
 * flush all user-space atc entries.
 */
static inline void __flush_tlb(void)
{
	BUG();
}

static inline void __flush_tlb_one(unsigned long addr)
{
	BUG();
}

#define flush_tlb() __flush_tlb()

/*
 * flush all atc entries (both kernel and user-space entries).
 */
static inline void flush_tlb_all(void)
{
	BUG();
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	BUG();
}

static inline void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	BUG();
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	BUG();
}

static inline void flush_tlb_kernel_page(unsigned long addr)
{
	BUG();
}

#endif /* CONFIG_MMU */

#endif /* _M68K_TLBFLUSH_H */

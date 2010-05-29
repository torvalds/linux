/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_TLBFLUSH_H
#define _ASM_TILE_TLBFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <asm/cacheflush.h>
#include <asm/page.h>
#include <hv/hypervisor.h>

/*
 * Rather than associating each mm with its own ASID, we just use
 * ASIDs to allow us to lazily flush the TLB when we switch mms.
 * This way we only have to do an actual TLB flush on mm switch
 * every time we wrap ASIDs, not every single time we switch.
 *
 * FIXME: We might improve performance by keeping ASIDs around
 * properly, though since the hypervisor direct-maps VAs to TSB
 * entries, we're likely to have lost at least the executable page
 * mappings by the time we switch back to the original mm.
 */
DECLARE_PER_CPU(int, current_asid);

/* The hypervisor tells us what ASIDs are available to us. */
extern int min_asid, max_asid;

static inline unsigned long hv_page_size(const struct vm_area_struct *vma)
{
	return (vma->vm_flags & VM_HUGETLB) ? HPAGE_SIZE : PAGE_SIZE;
}

/* Pass as vma pointer for non-executable mapping, if no vma available. */
#define FLUSH_NONEXEC ((const struct vm_area_struct *)-1UL)

/* Flush a single user page on this cpu. */
static inline void local_flush_tlb_page(const struct vm_area_struct *vma,
					unsigned long addr,
					unsigned long page_size)
{
	int rc = hv_flush_page(addr, page_size);
	if (rc < 0)
		panic("hv_flush_page(%#lx,%#lx) failed: %d",
		      addr, page_size, rc);
	if (!vma || (vma != FLUSH_NONEXEC && (vma->vm_flags & VM_EXEC)))
		__flush_icache();
}

/* Flush range of user pages on this cpu. */
static inline void local_flush_tlb_pages(const struct vm_area_struct *vma,
					 unsigned long addr,
					 unsigned long page_size,
					 unsigned long len)
{
	int rc = hv_flush_pages(addr, page_size, len);
	if (rc < 0)
		panic("hv_flush_pages(%#lx,%#lx,%#lx) failed: %d",
		      addr, page_size, len, rc);
	if (!vma || (vma != FLUSH_NONEXEC && (vma->vm_flags & VM_EXEC)))
		__flush_icache();
}

/* Flush all user pages on this cpu. */
static inline void local_flush_tlb(void)
{
	int rc = hv_flush_all(1);   /* preserve global mappings */
	if (rc < 0)
		panic("hv_flush_all(1) failed: %d", rc);
	__flush_icache();
}

/*
 * Global pages have to be flushed a bit differently. Not a real
 * performance problem because this does not happen often.
 */
static inline void local_flush_tlb_all(void)
{
	int i;
	for (i = 0; ; ++i) {
		HV_VirtAddrRange r = hv_inquire_virtual(i);
		if (r.size == 0)
			break;
		local_flush_tlb_pages(NULL, r.start, PAGE_SIZE, r.size);
		local_flush_tlb_pages(NULL, r.start, HPAGE_SIZE, r.size);
	}
}

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_others(cpumask, mm, va) flushes TLBs on other cpus
 *
 * Here (as in vm_area_struct), "end" means the first byte after
 * our end address.
 */

extern void flush_tlb_all(void);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);
extern void flush_tlb_current_task(void);
extern void flush_tlb_mm(struct mm_struct *);
extern void flush_tlb_page(const struct vm_area_struct *, unsigned long);
extern void flush_tlb_page_mm(const struct vm_area_struct *,
			      struct mm_struct *, unsigned long);
extern void flush_tlb_range(const struct vm_area_struct *,
			    unsigned long start, unsigned long end);

#define flush_tlb()     flush_tlb_current_task()

#endif /* _ASM_TILE_TLBFLUSH_H */

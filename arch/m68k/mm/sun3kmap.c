/*
 * linux/arch/m68k/mm/sun3kmap.c
 *
 * Copyright (C) 2002 Sam Creasey <sammy@sammy.net>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#include <asm/page.h>
#include <asm/io.h>
#include <asm/sun3mmu.h>

#undef SUN3_KMAP_DEBUG

#ifdef SUN3_KMAP_DEBUG
extern void print_pte_vaddr(unsigned long vaddr);
#endif

extern void mmu_emu_map_pmeg (int context, int vaddr);

static inline void do_page_mapin(unsigned long phys, unsigned long virt,
				 unsigned long type)
{
	unsigned long pte;
	pte_t ptep;

	ptep = pfn_pte(phys >> PAGE_SHIFT, PAGE_KERNEL);
	pte = pte_val(ptep);
	pte |= type;

	sun3_put_pte(virt, pte);

#ifdef SUN3_KMAP_DEBUG
	pr_info("mapin:");
	print_pte_vaddr(virt);
#endif

}

static inline void do_pmeg_mapin(unsigned long phys, unsigned long virt,
				 unsigned long type, int pages)
{

	if(sun3_get_segmap(virt & ~SUN3_PMEG_MASK) == SUN3_INVALID_PMEG)
		mmu_emu_map_pmeg(sun3_get_context(), virt);

	while(pages) {
		do_page_mapin(phys, virt, type);
		phys += PAGE_SIZE;
		virt += PAGE_SIZE;
		pages--;
	}
}

void __iomem *sun3_ioremap(unsigned long phys, unsigned long size,
		   unsigned long type)
{
	struct vm_struct *area;
	unsigned long offset, virt, ret;
	int pages;

	if(!size)
		return NULL;

	/* page align */
	offset = phys & (PAGE_SIZE-1);
	phys &= ~(PAGE_SIZE-1);

	size += offset;
	size = PAGE_ALIGN(size);
	if((area = get_vm_area(size, VM_IOREMAP)) == NULL)
		return NULL;

#ifdef SUN3_KMAP_DEBUG
	pr_info("ioremap: got virt %p size %lx(%lx)\n", area->addr, size,
		area->size);
#endif

	pages = size / PAGE_SIZE;
	virt = (unsigned long)area->addr;
	ret = virt + offset;

	while(pages) {
		int seg_pages;

		seg_pages = (SUN3_PMEG_SIZE - (virt & SUN3_PMEG_MASK)) / PAGE_SIZE;
		if(seg_pages > pages)
			seg_pages = pages;

		do_pmeg_mapin(phys, virt, type, seg_pages);

		pages -= seg_pages;
		phys += seg_pages * PAGE_SIZE;
		virt += seg_pages * PAGE_SIZE;
	}

	return (void __iomem *)ret;

}
EXPORT_SYMBOL(sun3_ioremap);


void __iomem *__ioremap(unsigned long phys, unsigned long size, int cache)
{

	return sun3_ioremap(phys, size, SUN3_PAGE_TYPE_IO);

}
EXPORT_SYMBOL(__ioremap);

void iounmap(void __iomem *addr)
{
	vfree((void *)(PAGE_MASK & (unsigned long)addr));
}
EXPORT_SYMBOL(iounmap);

/* sun3_map_test(addr, val) -- Reads a byte from addr, storing to val,
 * trapping the potential read fault.  Returns 0 if the access faulted,
 * 1 on success.
 *
 * This function is primarily used to check addresses on the VME bus.
 *
 * Mucking with the page fault handler seems a little hackish to me, but
 * SunOS, NetBSD, and Mach all implemented this check in such a manner,
 * so I figure we're allowed.
 */
int sun3_map_test(unsigned long addr, char *val)
{
	int ret = 0;

	__asm__ __volatile__
		(".globl _sun3_map_test_start\n"
		 "_sun3_map_test_start:\n"
		 "1: moveb (%2), (%0)\n"
		 "   moveq #1, %1\n"
		 "2:\n"
		 ".section .fixup,\"ax\"\n"
		 ".even\n"
		 "3: moveq #0, %1\n"
		 "   jmp 2b\n"
		 ".previous\n"
		 ".section __ex_table,\"a\"\n"
		 ".align 4\n"
		 ".long 1b,3b\n"
		 ".previous\n"
		 ".globl _sun3_map_test_end\n"
		 "_sun3_map_test_end:\n"
		 : "=a"(val), "=r"(ret)
		 : "a"(addr));

	return ret;
}
EXPORT_SYMBOL(sun3_map_test);

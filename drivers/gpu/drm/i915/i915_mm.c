/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/mm.h>
#include <linux/io-mapping.h>

#include <asm/pgtable.h>

#include "i915_drv.h"

struct remap_pfn {
	struct mm_struct *mm;
	unsigned long pfn;
	pgprot_t prot;
};

static int remap_pfn(pte_t *pte, pgtable_t token,
		     unsigned long addr, void *data)
{
	struct remap_pfn *r = data;

	/* Special PTE are not associated with any struct page */
	set_pte_at(r->mm, addr, pte, pte_mkspecial(pfn_pte(r->pfn, r->prot)));
	r->pfn++;

	return 0;
}

/**
 * remap_io_mapping - remap an IO mapping to userspace
 * @vma: user vma to map to
 * @addr: target user address to start at
 * @pfn: physical address of kernel memory
 * @size: size of map area
 * @iomap: the source io_mapping
 *
 *  Note: this is only safe if the mm semaphore is held when called.
 */
int remap_io_mapping(struct vm_area_struct *vma,
		     unsigned long addr, unsigned long pfn, unsigned long size,
		     struct io_mapping *iomap)
{
	struct remap_pfn r;
	int err;

	GEM_BUG_ON((vma->vm_flags &
		    (VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP)) !=
		   (VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP));

	/* We rely on prevalidation of the io-mapping to skip track_pfn(). */
	r.mm = vma->vm_mm;
	r.pfn = pfn;
	r.prot = __pgprot((pgprot_val(iomap->prot) & _PAGE_CACHE_MASK) |
			  (pgprot_val(vma->vm_page_prot) & ~_PAGE_CACHE_MASK));

	err = apply_to_page_range(r.mm, addr, size, remap_pfn, &r);
	if (unlikely(err)) {
		zap_vma_ptes(vma, addr, (r.pfn - pfn) << PAGE_SHIFT);
		return err;
	}

	return 0;
}

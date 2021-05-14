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


#include "i915_drv.h"

#define EXPECTED_FLAGS (VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP)

#define use_dma(io) ((io) != -1)

/**
 * remap_io_sg - remap an IO mapping to userspace
 * @vma: user vma to map to
 * @addr: target user address to start at
 * @size: size of map area
 * @sgl: Start sg entry
 * @iobase: Use stored dma address offset by this address or pfn if -1
 *
 *  Note: this is only safe if the mm semaphore is held when called.
 */
int remap_io_sg(struct vm_area_struct *vma,
		unsigned long addr, unsigned long size,
		struct scatterlist *sgl, resource_size_t iobase)
{
	unsigned long pfn, len, remapped = 0;
	int err;

	/* We rely on prevalidation of the io-mapping to skip track_pfn(). */
	GEM_BUG_ON((vma->vm_flags & EXPECTED_FLAGS) != EXPECTED_FLAGS);

	if (!use_dma(iobase))
		flush_cache_range(vma, addr, size);

	do {
		if (use_dma(iobase)) {
			if (!sg_dma_len(sgl))
				break;
			pfn = (sg_dma_address(sgl) + iobase) >> PAGE_SHIFT;
			len = sg_dma_len(sgl);
		} else {
			pfn = page_to_pfn(sg_page(sgl));
			len = sgl->length;
		}

		err = remap_pfn_range(vma, addr + remapped, pfn, len,
				      vma->vm_page_prot);
		if (err)
			break;
		remapped += len;
	} while ((sgl = __sg_next(sgl)));

	if (err)
		zap_vma_ptes(vma, addr, remapped);
	return err;
}

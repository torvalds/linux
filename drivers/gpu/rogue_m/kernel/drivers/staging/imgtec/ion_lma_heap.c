/*************************************************************************/ /*!
@File           ion_lma_heap.c
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
/* vi: set ts=8: */

#include "ion_lma_heap.h"

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/genalloc.h>
#include <linux/scatterlist.h>

/* Ion heap for LMA allocations. This heap is identical to CARVEOUT except
 * that it does not do any CPU cache maintenance nor does it zero the memory
 * using the CPU (this is handled with PVR_ANDROID_DEFER_CLEAR in userspace).
 */

struct ion_lma_heap {
	struct ion_heap heap;
	struct gen_pool *pool;
	ion_phys_addr_t base;
};

static ion_phys_addr_t ion_lma_allocate(struct ion_heap *heap,
					unsigned long size,
					unsigned long align)
{
	struct ion_lma_heap *lma_heap =
		container_of(heap, struct ion_lma_heap, heap);
	unsigned long offset = gen_pool_alloc(lma_heap->pool, size);

	if (!offset)
		return ION_CARVEOUT_ALLOCATE_FAIL;

	return offset;
}

static void ion_lma_free(struct ion_heap *heap, ion_phys_addr_t addr,
			 unsigned long size)
{
	struct ion_lma_heap *lma_heap =
		container_of(heap, struct ion_lma_heap, heap);

	if (addr == ION_CARVEOUT_ALLOCATE_FAIL)
		return;

	gen_pool_free(lma_heap->pool, addr, size);
}

static int ion_lma_heap_phys(struct ion_heap *heap,
			     struct ion_buffer *buffer,
			     ion_phys_addr_t *addr, size_t *len)
{
	struct sg_table *table = buffer->priv_virt;
	struct page *page = sg_page(table->sgl);
	ion_phys_addr_t paddr = PFN_PHYS(page_to_pfn(page));

	*addr = paddr;
	*len = buffer->size;
	return 0;
}

static int ion_lma_heap_allocate(struct ion_heap *heap,
				 struct ion_buffer *buffer,
				 unsigned long size, unsigned long align,
				 unsigned long flags)
{
	struct sg_table *table;
	ion_phys_addr_t paddr;
	int ret;

	if (align > PAGE_SIZE)
		return -EINVAL;

	table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret)
		goto err_free;

	paddr = ion_lma_allocate(heap, size, align);
	if (paddr == ION_CARVEOUT_ALLOCATE_FAIL) {
		ret = -ENOMEM;
		goto err_free_table;
	}

	sg_set_page(table->sgl, pfn_to_page(PFN_DOWN(paddr)), size, 0);
	buffer->priv_virt = table;
	return 0;

err_free_table:
	sg_free_table(table);
err_free:
	kfree(table);
	return ret;
}

static void ion_lma_heap_free(struct ion_buffer *buffer)
{
	struct ion_heap *heap = buffer->heap;
	struct sg_table *table = buffer->priv_virt;
	struct page *page = sg_page(table->sgl);
	ion_phys_addr_t paddr = PFN_PHYS(page_to_pfn(page));

	/* Do not zero the LMA heap from the CPU. This is very slow with
	 * the current TCF (w/ no DMA engine). We will use the TLA to clear
	 * the memory with Rogue in another place.
	 *
	 * We also skip the CPU cache maintenance for the heap space, as we
	 * statically know that the TCF PCI memory bar has UC/WC set by the
	 * MTRR/PAT subsystem.
	 */

	ion_lma_free(heap, paddr, buffer->size);
	sg_free_table(table);
	kfree(table);
}

static struct sg_table *ion_lma_heap_map_dma(struct ion_heap *heap,
					     struct ion_buffer *buffer)
{
	return buffer->priv_virt;
}

static void ion_lma_heap_unmap_dma(struct ion_heap *heap,
				   struct ion_buffer *buffer)
{
	return;
}

static int ion_lma_heap_map_user(struct ion_heap *mapper,
				 struct ion_buffer *buffer,
				 struct vm_area_struct *vma)
{
	struct sg_table *table = buffer->priv_virt;
	struct page *page = sg_page(table->sgl);
	ion_phys_addr_t paddr = PFN_PHYS(page_to_pfn(page));

	return remap_pfn_range(vma, vma->vm_start,
			       PFN_DOWN(paddr) + vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       pgprot_writecombine(vma->vm_page_prot));
}

static void *ion_lma_heap_map_kernel(struct ion_heap *heap,
				     struct ion_buffer *buffer)
{
	struct sg_table *table = buffer->priv_virt;
	struct page *page = sg_page(table->sgl);
	ion_phys_addr_t paddr = PFN_PHYS(page_to_pfn(page));

	return ioremap_wc(paddr, buffer->size);
}

static void ion_lma_heap_unmap_kernel(struct ion_heap *heap,
				      struct ion_buffer *buffer)
{
	iounmap(buffer->vaddr);
}

static struct ion_heap_ops lma_heap_ops = {
	.allocate = ion_lma_heap_allocate,
	.free = ion_lma_heap_free,
	.phys = ion_lma_heap_phys,
	.map_dma = ion_lma_heap_map_dma,
	.unmap_dma = ion_lma_heap_unmap_dma,
	.map_user = ion_lma_heap_map_user,
	.map_kernel = ion_lma_heap_map_kernel,
	.unmap_kernel = ion_lma_heap_unmap_kernel,
};

struct ion_heap *ion_lma_heap_create(struct ion_platform_heap *heap_data)
{
	struct ion_lma_heap *lma_heap;
	size_t size = heap_data->size;
	struct page *page;

	page = pfn_to_page(PFN_DOWN(heap_data->base));

	/* Do not zero the LMA heap from the CPU. This is very slow with
	 * the current TCF (w/ no DMA engine). We will use the TLA to clear
	 * the memory with Rogue in another place.
	 *
	 * We also skip the CPU cache maintenance for the heap space, as we
	 * statically know that the TCF PCI memory bar has UC/WC set by the
	 * MTRR/PAT subsystem.
	 */

	lma_heap = kzalloc(sizeof(struct ion_lma_heap), GFP_KERNEL);
	if (!lma_heap)
		return ERR_PTR(-ENOMEM);

	lma_heap->pool = gen_pool_create(12, -1);
	if (!lma_heap->pool) {
		kfree(lma_heap);
		return ERR_PTR(-ENOMEM);
	}

	lma_heap->base = heap_data->base;
	gen_pool_add(lma_heap->pool, lma_heap->base, size, -1);

	lma_heap->heap.id = heap_data->id;
	lma_heap->heap.ops = &lma_heap_ops;
	lma_heap->heap.name = heap_data->name;
	lma_heap->heap.type = ION_HEAP_TYPE_CUSTOM;
	lma_heap->heap.flags = ION_HEAP_FLAG_DEFER_FREE;

	return &lma_heap->heap;
}

void ion_lma_heap_destroy(struct ion_heap *heap)
{
	struct ion_lma_heap *lma_heap =
		container_of(heap, struct ion_lma_heap, heap);
	gen_pool_destroy(lma_heap->pool);
	kfree(lma_heap);
	lma_heap = NULL;
}

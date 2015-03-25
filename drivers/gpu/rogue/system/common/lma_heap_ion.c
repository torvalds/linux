/*************************************************************************/ /*!
@File           lma_heap_ion.c
@Title          Ion heap for local memory
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

#include PVR_ANDROID_ION_HEADER
#include <linux/slab.h>
#include <linux/genalloc.h>
#include <linux/scatterlist.h>
#include PVR_ANDROID_ION_PRIV_HEADER

#include "lma_heap_ion.h"
#include "pvr_debug.h"

struct lma_heap
{
	struct ion_heap heap;
	/* Base address in local physical memory to start allocating from. */
	unsigned long base;
	/* Size of the memory region we're responsible for */
	size_t size;
	/* Pool to manage allocations from this memory region. */
	struct gen_pool *pool;
};

static int lma_heap_allocate(struct ion_heap *heap,
							 struct ion_buffer *buffer,
							 unsigned long len, unsigned long align,
							 unsigned long flags)
{
	struct lma_heap *psHeap = container_of(heap, struct lma_heap, heap);
	int err = 0;

	buffer->priv_phys = gen_pool_alloc(psHeap->pool, len);
	if (!buffer->priv_phys)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Out of space in local memory pool",
				 __func__));
		err = -ENOMEM;
	}

	return err;
}

static void lma_heap_free(struct ion_buffer *buffer)
{
	struct lma_heap *psHeap =
		container_of(buffer->heap, struct lma_heap, heap);
	gen_pool_free(psHeap->pool, buffer->priv_phys, buffer->size);
}

static int lma_heap_phys(struct ion_heap *heap, struct ion_buffer *buffer,
						 ion_phys_addr_t *addr, size_t *len)
{
	*addr = buffer->priv_phys;
	*len = buffer->size;
	return 0;
}

static struct sg_table *lma_heap_map_dma(struct ion_heap *heap,
										 struct ion_buffer *buffer)
{
	struct sg_table *psTable;

	psTable = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!psTable)
		return NULL;

	if (sg_alloc_table(psTable, 1, GFP_KERNEL))
	{
		kfree(psTable);
		return NULL;
	}

	
#if defined(CONFIG_FLATMEM)
	sg_set_page(psTable->sgl, pfn_to_page(PFN_DOWN(buffer->priv_phys)),
				buffer->size, 0);
#else
#error ion_lma_heap requires CONFIG_FLATMEM
#endif

	return psTable;
}

static void lma_heap_unmap_dma(struct ion_heap *heap,
							   struct ion_buffer *buffer)
{
	sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
}

static void *lma_heap_map_kernel(struct ion_heap *heap,
								 struct ion_buffer *buffer)
{
	return ioremap_wc(buffer->priv_phys, buffer->size);
}

static void lma_heap_unmap_kernel(struct ion_heap *heap,
								  struct ion_buffer *buffer)
{
	iounmap(buffer->vaddr);
}

static int lma_heap_map_user(struct ion_heap *mapper, struct ion_buffer *buffer,
							 struct vm_area_struct *vma)
{
	return remap_pfn_range(vma, vma->vm_start,
						   PFN_DOWN(buffer->priv_phys) + vma->vm_pgoff,
						   vma->vm_end - vma->vm_start,
						   pgprot_writecombine(vma->vm_page_prot));
}

static struct ion_heap_ops lma_heap_ops =
{
	.allocate = lma_heap_allocate,
	.free = lma_heap_free,
	.phys = lma_heap_phys,
	.map_dma = lma_heap_map_dma,
	.unmap_dma = lma_heap_unmap_dma,
	.map_kernel = lma_heap_map_kernel,
	.unmap_kernel = lma_heap_unmap_kernel,
	.map_user = lma_heap_map_user
};

struct ion_heap *lma_heap_create(struct ion_platform_heap *data)
{
	struct lma_heap *psHeap;
	struct gen_pool *psPool;

	/* Check that sysconfig.c filled out the information we need. */
	if (!data->size)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: System code did not set up a sensible "
				 "heap size", __func__));
		goto err_out;
	}

	psHeap = kzalloc(sizeof(struct lma_heap), GFP_KERNEL);
	if (!psHeap)
		goto err_out;

	psPool = gen_pool_create(PAGE_SHIFT, -1);
	if (!psPool)
		goto err_free;

	psHeap->heap.type = ION_HEAP_TYPE_CUSTOM;
	psHeap->heap.ops = &lma_heap_ops;
	psHeap->heap.name = data->name;
	psHeap->heap.id = data->id;
	psHeap->base = data->base;
	psHeap->size = data->size;

	/* Tell the pool allocator about the region of physical address space we
	   want it to allocate from. */
	if (gen_pool_add(psPool, psHeap->base, psHeap->size, -1))
		goto err_pool_destroy;

	psHeap->pool = psPool;
	return &psHeap->heap;

err_pool_destroy:
	gen_pool_destroy(psPool);
err_free:
	kfree(psHeap);
err_out:
	return NULL;
}

/*
 * drivers/gpu/ion/ion_carveout_heap.c
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

 #include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/ion.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/iommu.h>
#include <linux/seq_file.h>
#include <asm/mach/map.h>
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>
#include "ion_priv.h"

struct ion_carveout_heap {
	struct ion_heap heap;
	struct gen_pool *pool;
	ion_phys_addr_t base;
	unsigned long bit_nr;
	unsigned long *bits;
};
ion_phys_addr_t ion_carveout_allocate(struct ion_heap *heap,
				      unsigned long size,
				      unsigned long align)
{
	struct ion_carveout_heap *carveout_heap =
		container_of(heap, struct ion_carveout_heap, heap);
	unsigned long offset = gen_pool_alloc(carveout_heap->pool, size);

	if (!offset) {
		if ((heap->total_size - heap->allocated_size) > size)
			pr_debug("%s: heap %s has enough memory (%luK) but"
				" the allocation of size(%luK) still failed."
				" Memory is probably fragmented.\n",
				__func__, heap->name,
				(heap->total_size - heap->allocated_size)/SZ_1K, 
				size/SZ_1K);
		else
			pr_debug("%s: heap %s has not enough memory(%luK)"
				"the alloction of size is %luK.\n",
				__func__, heap->name,
				(heap->total_size - heap->allocated_size)/SZ_1K, 
				size/SZ_1K);
		return ION_CARVEOUT_ALLOCATE_FAIL;
	}

	heap->allocated_size += size;

	if((offset + size - carveout_heap->base) > heap->max_allocated)
		heap->max_allocated = offset + size - carveout_heap->base;

	bitmap_set(carveout_heap->bits, 
		(offset - carveout_heap->base)/PAGE_SIZE , size/PAGE_SIZE);
	return offset;
}

void ion_carveout_free(struct ion_heap *heap, ion_phys_addr_t addr,
		       unsigned long size)
{
	struct ion_carveout_heap *carveout_heap =
		container_of(heap, struct ion_carveout_heap, heap);

	if (addr == ION_CARVEOUT_ALLOCATE_FAIL)
		return;
	gen_pool_free(carveout_heap->pool, addr, size);

	heap->allocated_size -= size;
	bitmap_clear(carveout_heap->bits, 
		(addr - carveout_heap->base)/PAGE_SIZE, size/PAGE_SIZE);
}

static int ion_carveout_heap_phys(struct ion_heap *heap,
				  struct ion_buffer *buffer,
				  ion_phys_addr_t *addr, size_t *len)
{
	*addr = buffer->priv_phys;
	*len = buffer->size;
	return 0;
}

static int ion_carveout_heap_allocate(struct ion_heap *heap,
				      struct ion_buffer *buffer,
				      unsigned long size, unsigned long align,
				      unsigned long flags)
{
	buffer->priv_phys = ion_carveout_allocate(heap, size, align);
	return buffer->priv_phys == ION_CARVEOUT_ALLOCATE_FAIL ? -ENOMEM : 0;
}

static void ion_carveout_heap_free(struct ion_buffer *buffer)
{
	struct ion_heap *heap = buffer->heap;

	ion_carveout_free(heap, buffer->priv_phys, buffer->size);
	buffer->priv_phys = ION_CARVEOUT_ALLOCATE_FAIL;
}

struct scatterlist *ion_carveout_heap_map_dma(struct ion_heap *heap,
					      struct ion_buffer *buffer)
{
	return ERR_PTR(-EINVAL);
}

void ion_carveout_heap_unmap_dma(struct ion_heap *heap,
				 struct ion_buffer *buffer)
{
	return;
}

void *ion_carveout_heap_map_kernel(struct ion_heap *heap,
				   struct ion_buffer *buffer)
{
	return __arch_ioremap(buffer->priv_phys, buffer->size,
			      MT_MEMORY_NONCACHED);
}

void ion_carveout_heap_unmap_kernel(struct ion_heap *heap,
				    struct ion_buffer *buffer)
{
	__arch_iounmap(buffer->vaddr);
	buffer->vaddr = NULL;
	return;
}

int ion_carveout_heap_map_user(struct ion_heap *heap, struct ion_buffer *buffer,
			       struct vm_area_struct *vma)
{
	return remap_pfn_range(vma, vma->vm_start,
			       __phys_to_pfn(buffer->priv_phys) + vma->vm_pgoff,
			       buffer->size,
					vma->vm_page_prot);
}
int ion_carveout_cache_op(struct ion_heap *heap, struct ion_buffer *buffer,
			void *virt, unsigned int type)
{
	unsigned long phys_start = 0, phys_end = 0;
	void *virt_start = NULL, *virt_end = NULL;
	struct ion_user_map_addr *map = NULL;

        if(!buffer)
                return -EINVAL;
	phys_start = buffer->priv_phys;
	phys_end = buffer->priv_phys + buffer->size;
	
	list_for_each_entry(map, &buffer->map_addr, list) {
		if(map->vaddr == (unsigned long)virt){
			virt_start = virt;
			virt_end = (void *)((unsigned long)virt + map->size);
			break;
		}
	}
	if(!virt_start){
		pr_err("%s: virt(%p) has not been maped or has been unmaped\n",
			__func__, virt);
		return -EINVAL;
	}
	switch(type) {
	case ION_CACHE_FLUSH:
		dmac_flush_range(virt_start, virt_end);
		outer_flush_range(phys_start,phys_end); 
		break;
	case ION_CACHE_CLEAN:
		/* When cleaning, always clean the innermost (L1) cache first 
		* and then clean the outer cache(s).
		*/
		dmac_clean_range(virt_start, virt_end);
		outer_clean_range(phys_start,phys_end); 
		break;
	case ION_CACHE_INV:
		/* When invalidating, always invalidate the outermost cache first 
		* and the L1 cache last.
		*/
		outer_inv_range(phys_start,phys_end); 
		dmac_inv_range(virt_start, virt_end);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ion_carveout_print_debug(struct ion_heap *heap, struct seq_file *s)
{
	int i;
	struct ion_carveout_heap *carveout_heap =
		container_of(heap, struct ion_carveout_heap, heap);

	for(i = carveout_heap->bit_nr/8 - 1; i>= 0; i--){
		seq_printf(s, "%.3uM> Bits[%.3d - %.3d]: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", 
				i+1, i*8 + 7, i*8,
				carveout_heap->bits[i*8 + 7],
				carveout_heap->bits[i*8 + 6],
				carveout_heap->bits[i*8 + 5],
				carveout_heap->bits[i*8 + 4],
				carveout_heap->bits[i*8 + 3],
				carveout_heap->bits[i*8 + 2],
				carveout_heap->bits[i*8 + 1],
				carveout_heap->bits[i*8]);
	}
	seq_printf(s, "Total allocated: %luM\n",
		heap->allocated_size/SZ_1M);
	seq_printf(s, "max_allocated: %luM\n",
		heap->max_allocated/SZ_1M);
	seq_printf(s, "Heap size: %luM, heap base: 0x%lx\n", 
		heap->total_size/SZ_1M, carveout_heap->base);
	return 0;
}
static struct ion_heap_ops carveout_heap_ops = {
	.allocate = ion_carveout_heap_allocate,
	.free = ion_carveout_heap_free,
	.phys = ion_carveout_heap_phys,
	.map_user = ion_carveout_heap_map_user,
	.map_kernel = ion_carveout_heap_map_kernel,
	.unmap_kernel = ion_carveout_heap_unmap_kernel,
	.cache_op = ion_carveout_cache_op,
	.print_debug = ion_carveout_print_debug,
};

struct ion_heap *ion_carveout_heap_create(struct ion_platform_heap *heap_data)
{
	struct ion_carveout_heap *carveout_heap;

	carveout_heap = kzalloc(sizeof(struct ion_carveout_heap), GFP_KERNEL);
	if (!carveout_heap)
		return ERR_PTR(-ENOMEM);

	carveout_heap->pool = gen_pool_create(12, -1);
	if (!carveout_heap->pool) {
		kfree(carveout_heap);
		return ERR_PTR(-ENOMEM);
	}
	carveout_heap->base = heap_data->base;
	gen_pool_add(carveout_heap->pool, carveout_heap->base, heap_data->size,
		     -1);
	carveout_heap->heap.ops = &carveout_heap_ops;
	carveout_heap->heap.type = ION_HEAP_TYPE_CARVEOUT;
	carveout_heap->heap.allocated_size = 0;
	carveout_heap->heap.max_allocated = 0;
	carveout_heap->heap.total_size = heap_data->size;
	carveout_heap->bit_nr = heap_data->size/(PAGE_SIZE * sizeof(unsigned long) * 8);
	carveout_heap->bits = 
		(unsigned long *)kzalloc(carveout_heap->bit_nr * sizeof(unsigned long), GFP_KERNEL);

	return &carveout_heap->heap;
}

void ion_carveout_heap_destroy(struct ion_heap *heap)
{
	struct ion_carveout_heap *carveout_heap =
	     container_of(heap, struct  ion_carveout_heap, heap);

	gen_pool_destroy(carveout_heap->pool);
	kfree(carveout_heap->bits);
	kfree(carveout_heap);
	carveout_heap = NULL;
}

/*
 * drivers/gpu/exynos/exynos_ion.c
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
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

#include <linux/err.h>
#include <linux/ion.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/cma.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/bitops.h>
#include <linux/pagemap.h>
#include <linux/dma-mapping.h>

#include <asm/pgtable.h>

#include "../ion_priv.h"

struct ion_device *ion_exynos;

static int num_heaps;
static struct ion_heap **heaps;
static struct device *exynos_ion_dev;

/* IMBUFS stands for "InterMediate BUFfer Storage" */
#define IMBUFS_SHIFT	4
#define IMBUFS_ENTRIES	(1 << IMBUFS_SHIFT)
#define IMBUFS_MASK	(IMBUFS_ENTRIES - 1)	/* masking lower bits */
#define MAX_LV0IMBUFS	IMBUFS_ENTRIES
#define MAX_LV1IMBUFS	(IMBUFS_ENTRIES + IMBUFS_ENTRIES * IMBUFS_ENTRIES)
#define MAX_IMBUFS	(MAX_LV1IMBUFS + (IMBUFS_ENTRIES << (IMBUFS_SHIFT * 2)))

#define LV1IDX(lv1base)		((lv1base) >> IMBUFS_SHIFT)
#define LV2IDX1(lv2base)	((lv2base) >> (IMBUFS_SHIFT * 2))
#define LV2IDX2(lv2base)	(((lv2base) >> (IMBUFS_SHIFT)) & IMBUFS_MASK)

static int orders[] = {PAGE_SHIFT + 8, PAGE_SHIFT + 4, PAGE_SHIFT, 0};

static inline phys_addr_t *get_imbufs(int idx,
		phys_addr_t *lv0imbufs, phys_addr_t **lv1pimbufs,
		phys_addr_t ***lv2ppimbufs)
{
	if (idx < MAX_LV0IMBUFS) {
		return lv0imbufs;
	} else if (idx < MAX_LV1IMBUFS) {
		idx -= MAX_LV0IMBUFS;
		return lv1pimbufs[LV1IDX(idx)];
	} else if (idx < MAX_IMBUFS) {
		idx -= MAX_LV1IMBUFS;
		return lv2ppimbufs[LV2IDX1(idx)][LV2IDX2(idx)];
	}
	return NULL;
}

static inline phys_addr_t *get_imbufs_4free(int idx,
		phys_addr_t *lv0imbufs, phys_addr_t **lv1pimbufs,
		phys_addr_t ***lv2ppimbufs)
{
	if (idx < MAX_LV0IMBUFS) {
		return lv0imbufs;
	} else if (idx < MAX_LV1IMBUFS) {
		phys_addr_t *imbufs;
		idx -= MAX_LV0IMBUFS;
		imbufs = lv1pimbufs[LV1IDX(idx)];
		if (LV1IDX(idx) == 0)
			kfree(lv1pimbufs);
		return imbufs;
	} else if (idx < MAX_IMBUFS) {
		int baseidx;
		phys_addr_t *imbufs;
		baseidx = idx - MAX_LV1IMBUFS;
		imbufs = lv2ppimbufs[LV2IDX1(baseidx)][LV2IDX2(baseidx)];
		if (LV2IDX2(baseidx) == 0) {
			kfree(lv2ppimbufs[LV2IDX1(baseidx)]);
			if (LV2IDX1(baseidx) == 0)
				kfree(lv2ppimbufs);
		}
		return imbufs;

	}
	return NULL;
}

static int ion_exynos_heap_allocate(struct ion_heap *heap,
		struct ion_buffer *buffer,
				     unsigned long size, unsigned long align,
				     unsigned long flags)
{
	int *cur_order = orders;
	int alloc_chunks;
	int ret = 0;
	phys_addr_t *im_phys_bufs;
	phys_addr_t **pim_phys_bufs = NULL;
	phys_addr_t ***ppim_phys_bufs = NULL;
	phys_addr_t *cur_bufs = NULL;
	int copied = 0;
	struct scatterlist *sgl;
	struct sg_table *sgtable;

	im_phys_bufs = kzalloc(sizeof(*im_phys_bufs) * IMBUFS_ENTRIES,
				GFP_KERNEL);
	if (!im_phys_bufs)
		return -ENOMEM;

	alloc_chunks = 0;

	while (size && *cur_order) {
		struct page *page;

		if (size < (1 << *cur_order)) {
			cur_order++;
			continue;
		}

		page = alloc_pages(GFP_HIGHUSER | __GFP_COMP |
						__GFP_NOWARN | __GFP_NORETRY,
						*cur_order - PAGE_SHIFT);
		if (!page) {
			cur_order++;
			continue;
		}

		if (alloc_chunks & IMBUFS_MASK) {
			cur_bufs++;
		} else if (alloc_chunks < MAX_LV0IMBUFS) {
			cur_bufs = im_phys_bufs;
		} else if (alloc_chunks < MAX_LV1IMBUFS) {
			int lv1idx = LV1IDX(alloc_chunks - MAX_LV0IMBUFS);

			if (!pim_phys_bufs) {
				pim_phys_bufs = kzalloc(
					sizeof(*pim_phys_bufs) * IMBUFS_ENTRIES,
					GFP_KERNEL);
				if (!pim_phys_bufs)
					break;
			}

			if (!pim_phys_bufs[lv1idx]) {
				pim_phys_bufs[lv1idx] = kzalloc(
					sizeof(*cur_bufs) * IMBUFS_ENTRIES,
					GFP_KERNEL);
				if (!pim_phys_bufs[lv1idx])
					break;
			}

			cur_bufs = pim_phys_bufs[lv1idx];
		} else if (alloc_chunks < MAX_IMBUFS) {
			phys_addr_t **pcur_bufs;
			int lv2base = alloc_chunks - MAX_LV1IMBUFS;

			if (!ppim_phys_bufs) {
				ppim_phys_bufs = kzalloc(
					sizeof(*ppim_phys_bufs) * IMBUFS_ENTRIES
					, GFP_KERNEL);
				if (!ppim_phys_bufs)
					break;
			}

			if (!ppim_phys_bufs[LV2IDX1(lv2base)]) {
				ppim_phys_bufs[LV2IDX1(lv2base)] = kzalloc(
					sizeof(*pcur_bufs) * IMBUFS_ENTRIES,
					GFP_KERNEL);
				if (!ppim_phys_bufs[LV2IDX1(lv2base)])
					break;
			}
			pcur_bufs = ppim_phys_bufs[LV2IDX1(lv2base)];

			if (!pcur_bufs[LV2IDX2(lv2base)]) {
				pcur_bufs[LV2IDX2(lv2base)] = kzalloc(
					sizeof(*cur_bufs) * IMBUFS_ENTRIES,
					GFP_KERNEL);
				if (!pcur_bufs[LV2IDX2(lv2base)])
					break;
			}
			cur_bufs = pcur_bufs[LV2IDX2(lv2base)];
		} else {
			break;
		}

		*cur_bufs = page_to_phys(page) | *cur_order;

		size = size - (1 << *cur_order);
		alloc_chunks++;
	}

	if (size) {
		ret = -ENOMEM;
		goto alloc_error;
	}

	sgtable = kmalloc(sizeof(*sgtable), GFP_KERNEL);
	if (!sgtable) {
		ret = -ENOMEM;
		goto alloc_error;
	}

	if (sg_alloc_table(sgtable, alloc_chunks, GFP_KERNEL)) {
		ret = -ENOMEM;
		kfree(sgtable);
		goto alloc_error;
	}

	sgl = sgtable->sgl;
	while (copied < alloc_chunks) {
		int i;
		cur_bufs = get_imbufs(copied, im_phys_bufs, pim_phys_bufs,
								ppim_phys_bufs);
		BUG_ON(!cur_bufs);
		for (i = 0; (i < IMBUFS_ENTRIES) && cur_bufs[i]; i++) {
			phys_addr_t phys;
			int order;

			phys = cur_bufs[i];
			order = phys & ~PAGE_MASK;
			sg_set_page(sgl, phys_to_page(phys), 1 << order, 0);
			sgl = sg_next(sgl);
			copied++;
		}
	}

	buffer->priv_virt = sgtable;
	buffer->flags = flags;

alloc_error:
	alloc_chunks = (alloc_chunks + (IMBUFS_ENTRIES - 1)) & ~IMBUFS_MASK;
	while (alloc_chunks) {
		int i;
		cur_bufs = get_imbufs_4free(alloc_chunks - 1, im_phys_bufs,
				pim_phys_bufs, ppim_phys_bufs);
		if (unlikely(ret)) {
			for (i = 0; (i < IMBUFS_ENTRIES) && cur_bufs[i]; i++) {
				phys_addr_t phys;
				int gfp_order;

				phys = cur_bufs[i];
				gfp_order = (phys & ~PAGE_MASK) - PAGE_SHIFT;
				phys = phys & PAGE_MASK;
				__free_pages(phys_to_page(phys), gfp_order);
			}
		}

		kfree(cur_bufs);
		alloc_chunks -= IMBUFS_ENTRIES;
	}

	return ret;
}

static void ion_exynos_heap_free(struct ion_buffer *buffer)
{
	struct scatterlist *sg;
	int i;
	struct sg_table *sgtable = buffer->priv_virt;

	for_each_sg(sgtable->sgl, sg, sgtable->orig_nents, i)
		__free_pages(sg_page(sg), __ffs(sg_dma_len(sg)) - PAGE_SHIFT);

	sg_free_table(sgtable);
	kfree(sgtable);
}

static struct scatterlist *ion_exynos_heap_map_dma(struct ion_heap *heap,
					    struct ion_buffer *buffer)
{
	return ((struct sg_table *)buffer->priv_virt)->sgl;
}

static void ion_exynos_heap_unmap_dma(struct ion_heap *heap,
			       struct ion_buffer *buffer)
{
}

static void *ion_exynos_heap_map_kernel(struct ion_heap *heap,
				 struct ion_buffer *buffer)
{
	struct page **pages, **tmp_pages;
	struct sg_table *sgt;
	struct scatterlist *sgl;
	int num_pages = buffer->size >> PAGE_SHIFT;
	int i;
	void *vaddr;

	sgt = buffer->priv_virt;

	pages = vmalloc(sizeof(*pages) * (buffer->size >> PAGE_SHIFT));
	if (!pages)
		return NULL;

	tmp_pages = pages;
	for_each_sg(sgt->sgl, sgl, sgt->orig_nents, i) {
		struct page *page = sg_page(sgl);
		int n;

		for (n = sg_dma_len(sgl) >> PAGE_SHIFT; n > 0; n--)
			*(tmp_pages++) = page++;

	}

	vaddr = vmap(pages, num_pages, VM_USERMAP | VM_MAP, PAGE_KERNEL);

	vfree(pages);

	return vaddr + offset_in_page(sg_phys(sgt->sgl));
}

static void ion_exynos_heap_unmap_kernel(struct ion_heap *heap,
				  struct ion_buffer *buffer)
{
	vunmap(buffer->vaddr);
}

static int ion_exynos_heap_map_user(struct ion_heap *heap,
			struct ion_buffer *buffer, struct vm_area_struct *vma)
{
	struct sg_table *sgt = buffer->priv_virt;
	struct scatterlist *sgl;
	unsigned long pgoff;
	int i;
	unsigned long start;
	int map_pages;

	if (buffer->kmap_cnt)
		return remap_vmalloc_range(vma, buffer->vaddr, vma->vm_pgoff);

	pgoff = vma->vm_pgoff;
	start = vma->vm_start;
	map_pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	vma->vm_flags |= VM_RESERVED;

	for_each_sg(sgt->sgl, sgl, sgt->orig_nents, i) {
		unsigned long sg_pgnum = sg_dma_len(sgl) >> PAGE_SHIFT;

		if (sg_pgnum <= pgoff) {
			pgoff -= sg_pgnum;
		} else {
			struct page *page = sg_page(sgl) + pgoff;
			int i;

			sg_pgnum -= pgoff;

			for (i = 0; (map_pages > 0) && (i < sg_pgnum); i++) {
				int ret;
				ret = vm_insert_page(vma, start, page);
				if (ret)
					return ret;
				start += PAGE_SIZE;
				page++;
				map_pages--;
			}

			pgoff = 0;

			if (map_pages == 0)
				break;
		}
	}

	return 0;
}

static struct ion_heap_ops vmheap_ops = {
	.allocate = ion_exynos_heap_allocate,
	.free = ion_exynos_heap_free,
	.map_dma = ion_exynos_heap_map_dma,
	.unmap_dma = ion_exynos_heap_unmap_dma,
	.map_kernel = ion_exynos_heap_map_kernel,
	.unmap_kernel = ion_exynos_heap_unmap_kernel,
	.map_user = ion_exynos_heap_map_user,
};

static struct ion_heap *ion_exynos_heap_create(struct ion_platform_heap *unused)
{
	struct ion_heap *heap;

	heap = kzalloc(sizeof(struct ion_heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->ops = &vmheap_ops;
	heap->type = ION_HEAP_TYPE_EXYNOS;
	return heap;
}

static void ion_exynos_heap_destroy(struct ion_heap *heap)
{
	kfree(heap);
}

static int ion_exynos_contig_heap_allocate(struct ion_heap *heap,
					   struct ion_buffer *buffer,
					   unsigned long len,
					   unsigned long align,
					   unsigned long flags)
{
	buffer->priv_phys = cma_alloc(exynos_ion_dev, NULL, len, align);
	if (IS_ERR_VALUE(buffer->priv_phys))
		return (int)buffer->priv_phys;

	buffer->flags = flags;

	return 0;
}

static void ion_exynos_contig_heap_free(struct ion_buffer *buffer)
{
	cma_free(buffer->priv_phys);
}

static int ion_exynos_contig_heap_phys(struct ion_heap *heap,
				       struct ion_buffer *buffer,
				       ion_phys_addr_t *addr, size_t *len)
{
	*addr = buffer->priv_phys;
	*len = buffer->size;
	return 0;
}

static struct scatterlist *ion_exynos_contig_heap_map_dma(struct ion_heap *heap,
						   struct ion_buffer *buffer)
{
	struct scatterlist *sglist;

	sglist = vmalloc(sizeof(struct scatterlist));
	if (!sglist)
		return ERR_PTR(-ENOMEM);
	sg_init_table(sglist, 1);
	sg_set_page(sglist, phys_to_page(buffer->priv_phys), buffer->size, 0);
	return sglist;
}

static void ion_exynos_contig_heap_unmap_dma(struct ion_heap *heap,
			       struct ion_buffer *buffer)
{
	vfree(buffer->sglist);
}

static int ion_exynos_contig_heap_map_user(struct ion_heap *heap,
				    struct ion_buffer *buffer,
				    struct vm_area_struct *vma)
{
	unsigned long pfn = __phys_to_pfn(buffer->priv_phys);

	return remap_pfn_range(vma, vma->vm_start, pfn + vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);

}

static void *ion_exynos_contig_heap_map_kernel(struct ion_heap *heap,
				 struct ion_buffer *buffer)
{
	return phys_to_virt(buffer->priv_phys);
}

static void ion_exynos_contig_heap_unmap_kernel(struct ion_heap *heap,
				  struct ion_buffer *buffer)
{
}

static struct ion_heap_ops contig_heap_ops = {
	.allocate = ion_exynos_contig_heap_allocate,
	.free = ion_exynos_contig_heap_free,
	.phys = ion_exynos_contig_heap_phys,
	.map_dma = ion_exynos_contig_heap_map_dma,
	.unmap_dma = ion_exynos_contig_heap_unmap_dma,
	.map_kernel = ion_exynos_contig_heap_map_kernel,
	.unmap_kernel = ion_exynos_contig_heap_unmap_kernel,
	.map_user = ion_exynos_contig_heap_map_user,
};

static struct ion_heap *ion_exynos_contig_heap_create(
					struct ion_platform_heap *unused)
{
	struct ion_heap *heap;

	heap = kzalloc(sizeof(struct ion_heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->ops = &contig_heap_ops;
	heap->type = ION_HEAP_TYPE_EXYNOS_CONTIG;
	return heap;
}

static void ion_exynos_contig_heap_destroy(struct ion_heap *heap)
{
	kfree(heap);
}

struct exynos_user_heap_data {
	struct sg_table sgt;
	bool is_pfnmap; /* The region has VM_PFNMAP property */
};

static int pfnmap_digger(struct sg_table *sgt, unsigned long addr, int nr_pages)
{
	/* If the given user address is not normal mapping,
	   It must be contiguous physical mapping */
	struct vm_area_struct *vma;
	unsigned long *pfns;
	int i, ipfn, pi, ret;
	struct scatterlist *sg;
	unsigned int contigs;
	unsigned long pfn;


	down_read(&current->mm->mmap_sem);
	vma = find_vma(current->mm, addr);
	up_read(&current->mm->mmap_sem);

	if ((vma == NULL) || (vma->vm_end < (addr + (nr_pages << PAGE_SHIFT))))
		return -EINVAL;

	pfns = kmalloc(sizeof(*pfns) * nr_pages, GFP_KERNEL);
	if (!pfns)
		return -ENOMEM;

	ret = follow_pfn(vma, addr, &pfns[0]); /* no side effect */
	if (ret)
		goto err_follow_pfn;

	if (!pfn_valid(pfns[0])) {
		ret = -EINVAL;
		goto err_follow_pfn;
	}

	addr += PAGE_SIZE;

	/* An element of pfns consists of
	 * - higher 20 bits: page frame number (pfn)
	 * - lower  12 bits: number of contiguous pages from the pfn
	 * Maximum size of a contiguous chunk: 16MB (4096 pages)
	 * contigs = 0 indicates no adjacent page is found yet.
	 * Thus, contigs = x means (x + 1) pages are contiguous.
	 */
	for (i = 1, pi = 0, ipfn = 0, contigs = 0; i < nr_pages; i++) {
		ret = follow_pfn(vma, addr, &pfn);
		if (ret)
			break;

		if (pfns[ipfn] == (pfn - (i - pi))) {
			contigs++;
		} else {
			if (contigs & PAGE_MASK) {
				ret = -EOVERFLOW;
				break;
			}

			pfns[ipfn] <<= PAGE_SHIFT;
			pfns[ipfn] |= contigs;
			ipfn++;
			pi = i;
			contigs = 0;
			pfns[ipfn] = pfn;
		}

		addr += PAGE_SIZE;
	}

	if (i == nr_pages) {
		if (contigs & PAGE_MASK) {
			ret = -EOVERFLOW;
			goto err_follow_pfn;
		}

		pfns[ipfn] <<= PAGE_SHIFT;
		pfns[ipfn] |= contigs;

		nr_pages = ipfn + 1;
	} else {
		goto err_follow_pfn;
	}

	ret = sg_alloc_table(sgt, nr_pages, GFP_KERNEL);
	if (ret)
		goto err_follow_pfn;

	for_each_sg(sgt->sgl, sg, nr_pages, i)
		sg_set_page(sg, phys_to_page(pfns[i]),
			((pfns[i] & ~PAGE_MASK) + 1) << PAGE_SHIFT, 0);
err_follow_pfn:
	kfree(pfns);
	return ret;
}

static int ion_exynos_user_heap_allocate(struct ion_heap *heap,
					   struct ion_buffer *buffer,
					   unsigned long len,
					   unsigned long align,
					   unsigned long flags)
{
	unsigned long start = align;
	size_t last_size = 0;
	struct page **pages;
	int nr_pages;
	int ret = 0, i;
	off_t start_off;
	struct exynos_user_heap_data *privdata = NULL;
	struct scatterlist *sgl;

	last_size = (start + len) & ~PAGE_MASK;
	if (last_size == 0)
		last_size = PAGE_SIZE;

	start_off = offset_in_page(start);

	start = round_down(start, PAGE_SIZE);

	nr_pages = PFN_DOWN(PAGE_ALIGN(len + start_off));

	pages = kzalloc(nr_pages * sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	privdata = kmalloc(sizeof(*privdata), GFP_KERNEL);
	if (!privdata) {
		ret = -ENOMEM;
		goto err_privdata;
	}

	buffer->priv_virt = privdata;
	buffer->flags = flags;

	ret = get_user_pages_fast(start, nr_pages,
				flags & ION_EXYNOS_WRITE_MASK, pages);

	if (ret < 0) {
		kfree(pages);

		ret = pfnmap_digger(&privdata->sgt, start, nr_pages);
		if (ret)
			goto err_pfnmap;

		privdata->is_pfnmap = true;

		return 0;
	}

	if (ret != nr_pages) {
		nr_pages = ret;
		ret = -EFAULT;
		goto err_alloc_sg;
	}

	ret = sg_alloc_table(&privdata->sgt, nr_pages, GFP_KERNEL);
	if (ret)
		goto err_alloc_sg;

	sgl = privdata->sgt.sgl;

	sg_set_page(sgl, pages[0],
			(nr_pages == 1) ? len : PAGE_SIZE - start_off,
			start_off);

	sgl = sg_next(sgl);

	/* nr_pages == 1 if sgl == NULL here */
	for (i = 1; i < (nr_pages - 1); i++) {
		sg_set_page(sgl, pages[i], PAGE_SIZE, 0);
		sgl = sg_next(sgl);
	}

	if (sgl)
		sg_set_page(sgl, pages[i], last_size, 0);

	privdata->is_pfnmap = false;

	kfree(pages);

	return 0;
err_alloc_sg:
	for (i = 0; i < nr_pages; i++)
		put_page(pages[i]);
err_pfnmap:
	kfree(privdata);
err_privdata:
	kfree(pages);
	return ret;
}

static void ion_exynos_user_heap_free(struct ion_buffer *buffer)
{
	struct scatterlist *sg;
	int i;
	struct exynos_user_heap_data *privdata = buffer->priv_virt;

	if (!privdata->is_pfnmap) {
		if (buffer->flags & ION_EXYNOS_WRITE_MASK) {
			for_each_sg(privdata->sgt.sgl, sg,
						privdata->sgt.orig_nents, i) {
				set_page_dirty_lock(sg_page(sg));
				put_page(sg_page(sg));
			}
		} else {
			for_each_sg(privdata->sgt.sgl, sg,
						privdata->sgt.orig_nents, i)
				put_page(sg_page(sg));
		}
	}

	sg_free_table(&privdata->sgt);
	kfree(privdata);
}

static struct ion_heap_ops user_heap_ops = {
	.allocate = ion_exynos_user_heap_allocate,
	.free = ion_exynos_user_heap_free,
	.map_dma = ion_exynos_heap_map_dma,
	.unmap_dma = ion_exynos_heap_unmap_dma,
	.map_kernel = ion_exynos_heap_map_kernel,
	.unmap_kernel = ion_exynos_heap_unmap_kernel,
};

static struct ion_heap *ion_exynos_user_heap_create(
					struct ion_platform_heap *unused)
{
	struct ion_heap *heap;

	heap = kzalloc(sizeof(struct ion_heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->ops = &user_heap_ops;
	heap->type = ION_HEAP_TYPE_EXYNOS_USER;
	return heap;
}

static void ion_exynos_user_heap_destroy(struct ion_heap *heap)
{
	kfree(heap);
}

enum ION_MSYNC_TYPE {
	IMSYNC_DEV_TO_READ = 0,
	IMSYNC_DEV_TO_WRITE = 1,
	IMSYNC_DEV_TO_RW = 2,
	IMSYNC_BUF_TYPES_MASK = 3,
	IMSYNC_BUF_TYPES_NUM = 4,
	IMSYNC_SYNC_FOR_DEV = 0x10000,
	IMSYNC_SYNC_FOR_CPU = 0x20000,
};

static enum dma_data_direction ion_msync_dir_table[IMSYNC_BUF_TYPES_NUM] = {
	DMA_TO_DEVICE,
	DMA_FROM_DEVICE,
	DMA_BIDIRECTIONAL,
};

static long ion_exynos_heap_msync(struct ion_client *client,
		struct ion_handle *handle, off_t offset, size_t size, long dir)
{
	struct ion_buffer *buffer;
	struct scatterlist *sg, *tsg;
	int nents = 0;
	int ret = 0;

	buffer = ion_share(client, handle);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	if ((offset + size) > buffer->size)
		return -EINVAL;

	sg = ion_map_dma(client, handle);
	if (IS_ERR(sg))
		return PTR_ERR(sg);

	while (sg && (offset >= sg_dma_len(sg))) {
		offset -= sg_dma_len(sg);
		sg = sg_next(sg);
	}

	size += offset;

	if (!sg)
		goto err_buf_sync;

	tsg = sg;
	while (tsg && (size > sg_dma_len(tsg))) {
		size -= sg_dma_len(tsg);
		nents++;
		tsg = sg_next(tsg);
	}

	if (tsg && size)
		nents++;

	/* TODO: exclude offset in the first entry and remainder of the
	   last entry. */
	if (dir & IMSYNC_SYNC_FOR_CPU)
		dma_sync_sg_for_cpu(NULL, sg, nents,
			ion_msync_dir_table[dir & IMSYNC_BUF_TYPES_MASK]);
	else if (dir & IMSYNC_SYNC_FOR_DEV)
		dma_sync_sg_for_device(NULL, sg, nents,
			ion_msync_dir_table[dir & IMSYNC_BUF_TYPES_MASK]);

err_buf_sync:
	ion_unmap_dma(client, handle);
	return ret;
}

struct ion_msync_data {
	enum ION_MSYNC_TYPE dir;
	int fd_buffer;
	size_t size;
	off_t offset;
};

enum ION_EXYNOS_CUSTOM_CMD {
	ION_EXYNOS_CUSTOM_MSYNC
};

static long exynos_heap_ioctl(struct ion_client *client, unsigned int cmd,
				unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case ION_EXYNOS_CUSTOM_MSYNC:
	{
		struct ion_msync_data data;
		struct ion_handle *handle;

		if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
			return -EFAULT;

		if ((data.offset + data.size) < data.offset)
			return -EINVAL;

		handle = ion_import_fd(client, data.fd_buffer);
		if (IS_ERR(handle))
			return PTR_ERR(handle);

		ret = ion_exynos_heap_msync(client, handle, data.offset,
						data.size, data.dir);
		ion_free(client, handle);
		break;
	}
	default:
		return -ENOTTY;
	}

	return ret;
}

static struct ion_heap *__ion_heap_create(struct ion_platform_heap *heap_data)
{
	struct ion_heap *heap = NULL;

	switch (heap_data->type) {
	case ION_HEAP_TYPE_EXYNOS:
		heap = ion_exynos_heap_create(heap_data);
		break;
	case ION_HEAP_TYPE_EXYNOS_CONTIG:
		heap = ion_exynos_contig_heap_create(heap_data);
		break;
	case ION_HEAP_TYPE_EXYNOS_USER:
		heap = ion_exynos_user_heap_create(heap_data);
		break;
	default:
		return ion_heap_create(heap_data);
	}

	if (IS_ERR_OR_NULL(heap)) {
		pr_err("%s: error creating heap %s type %d base %lu size %u\n",
		       __func__, heap_data->name, heap_data->type,
		       heap_data->base, heap_data->size);
		return ERR_PTR(-EINVAL);
	}

	heap->name = heap_data->name;
	heap->id = heap_data->id;

	return heap;
}

void __ion_heap_destroy(struct ion_heap *heap)
{
	if (!heap)
		return;

	switch (heap->type) {
	case ION_HEAP_TYPE_EXYNOS:
		ion_exynos_heap_destroy(heap);
		break;
	case ION_HEAP_TYPE_EXYNOS_CONTIG:
		ion_exynos_contig_heap_destroy(heap);
		break;
	case ION_HEAP_TYPE_EXYNOS_USER:
		ion_exynos_user_heap_destroy(heap);
		break;
	default:
		ion_heap_destroy(heap);
	}
}

static int exynos_ion_probe(struct platform_device *pdev)
{
	struct ion_platform_data *pdata = pdev->dev.platform_data;
	int err;
	int i;

	num_heaps = pdata->nr;

	heaps = kzalloc(sizeof(struct ion_heap *) * pdata->nr, GFP_KERNEL);
	if (!heaps)
		return -ENOMEM;

	ion_exynos = ion_device_create(&exynos_heap_ioctl);
	if (IS_ERR_OR_NULL(ion_exynos)) {
		kfree(heaps);
		return PTR_ERR(ion_exynos);
	}

	/* create the heaps as specified in the board file */
	for (i = 0; i < num_heaps; i++) {
		struct ion_platform_heap *heap_data = &pdata->heaps[i];

		heaps[i] = __ion_heap_create(heap_data);
		if (IS_ERR_OR_NULL(heaps[i])) {
			err = PTR_ERR(heaps[i]);
			goto err;
		}
		ion_device_add_heap(ion_exynos, heaps[i]);
	}
	platform_set_drvdata(pdev, ion_exynos);

	exynos_ion_dev = &pdev->dev;

	return 0;
err:
	for (i = 0; i < num_heaps; i++) {
		if (heaps[i])
			ion_heap_destroy(heaps[i]);
	}
	kfree(heaps);
	return err;
}

static int exynos_ion_remove(struct platform_device *pdev)
{
	struct ion_device *idev = platform_get_drvdata(pdev);
	int i;

	ion_device_destroy(idev);
	for (i = 0; i < num_heaps; i++)
		__ion_heap_destroy(heaps[i]);
	kfree(heaps);
	return 0;
}

static struct platform_driver ion_driver = {
	.probe = exynos_ion_probe,
	.remove = exynos_ion_remove,
	.driver = { .name = "ion-exynos" }
};

static int __init ion_init(void)
{
	return platform_driver_register(&ion_driver);
}

subsys_initcall(ion_init);

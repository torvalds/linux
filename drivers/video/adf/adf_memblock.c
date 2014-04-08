/*
 * Copyright (C) 2013 Google, Inc.
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

#include <linux/dma-buf.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/slab.h>

struct adf_memblock_pdata {
	phys_addr_t base;
};

static struct sg_table *adf_memblock_map(struct dma_buf_attachment *attach,
		enum dma_data_direction direction)
{
	struct adf_memblock_pdata *pdata = attach->dmabuf->priv;
	unsigned long pfn = PFN_DOWN(pdata->base);
	struct page *page = pfn_to_page(pfn);
	struct sg_table *table;
	int ret;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret < 0)
		goto err;

	sg_set_page(table->sgl, page, attach->dmabuf->size, 0);
	return table;

err:
	kfree(table);
	return ERR_PTR(ret);
}

static void adf_memblock_unmap(struct dma_buf_attachment *attach,
		struct sg_table *table, enum dma_data_direction direction)
{
	sg_free_table(table);
}

static void __init_memblock adf_memblock_release(struct dma_buf *buf)
{
	struct adf_memblock_pdata *pdata = buf->priv;
	int err = memblock_free(pdata->base, buf->size);

	if (err < 0)
		pr_warn("%s: freeing memblock failed: %d\n", __func__, err);
	kfree(pdata);
}

static void *adf_memblock_do_kmap(struct dma_buf *buf, unsigned long pgoffset,
		bool atomic)
{
	struct adf_memblock_pdata *pdata = buf->priv;
	unsigned long pfn = PFN_DOWN(pdata->base) + pgoffset;
	struct page *page = pfn_to_page(pfn);

	if (atomic)
		return kmap_atomic(page);
	else
		return kmap(page);
}

static void *adf_memblock_kmap_atomic(struct dma_buf *buf,
		unsigned long pgoffset)
{
	return adf_memblock_do_kmap(buf, pgoffset, true);
}

static void adf_memblock_kunmap_atomic(struct dma_buf *buf,
		unsigned long pgoffset, void *vaddr)
{
	kunmap_atomic(vaddr);
}

static void *adf_memblock_kmap(struct dma_buf *buf, unsigned long pgoffset)
{
	return adf_memblock_do_kmap(buf, pgoffset, false);
}

static void adf_memblock_kunmap(struct dma_buf *buf, unsigned long pgoffset,
		void *vaddr)
{
	kunmap(vaddr);
}

static int adf_memblock_mmap(struct dma_buf *buf, struct vm_area_struct *vma)
{
	struct adf_memblock_pdata *pdata = buf->priv;

	return remap_pfn_range(vma, vma->vm_start, PFN_DOWN(pdata->base),
			vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

struct dma_buf_ops adf_memblock_ops = {
	.map_dma_buf = adf_memblock_map,
	.unmap_dma_buf = adf_memblock_unmap,
	.release = adf_memblock_release,
	.kmap_atomic = adf_memblock_kmap_atomic,
	.kunmap_atomic = adf_memblock_kunmap_atomic,
	.kmap = adf_memblock_kmap,
	.kunmap = adf_memblock_kunmap,
	.mmap = adf_memblock_mmap,
};

/**
 * adf_memblock_export - export a memblock reserved area as a dma-buf
 *
 * @base: base physical address
 * @size: memblock size
 * @flags: mode flags for the dma-buf's file
 *
 * @base and @size must be page-aligned.
 *
 * Returns a dma-buf on success or ERR_PTR(-errno) on failure.
 */
struct dma_buf *adf_memblock_export(phys_addr_t base, size_t size, int flags)
{
	struct adf_memblock_pdata *pdata;
	struct dma_buf *buf;

	if (PAGE_ALIGN(base) != base || PAGE_ALIGN(size) != size)
		return ERR_PTR(-EINVAL);

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->base = base;
	buf = dma_buf_export(pdata, &adf_memblock_ops, size, flags);
	if (IS_ERR(buf))
		kfree(pdata);

	return buf;
}
EXPORT_SYMBOL(adf_memblock_export);

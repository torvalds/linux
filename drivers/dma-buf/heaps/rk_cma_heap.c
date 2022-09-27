// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF CMA heap exporter
 *
 * Copyright (C) 2012, 2019, 2020 Linaro Ltd.
 * Author: <benjamin.gaignard@linaro.org> for ST-Ericsson.
 *
 * Also utilizing parts of Andrew Davis' SRAM heap:
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * Copyright (C) 2021, 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/cma.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-map-ops.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <uapi/linux/dma-heap.h>

struct cma_heap {
	struct dma_heap *heap;
	struct cma *cma;
};

struct cma_heap_buffer {
	struct cma_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct page *cma_pages;
	struct page **pages;
	pgoff_t pagecount;
	int vmap_cnt;
	void *vaddr;

	bool uncached;
};

struct dma_heap_attachment {
	struct device *dev;
	struct sg_table table;
	struct list_head list;
	bool mapped;

	bool uncached;
};

static int cma_heap_attach(struct dma_buf *dmabuf,
			   struct dma_buf_attachment *attachment)
{
	struct cma_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	ret = sg_alloc_table_from_pages(&a->table, buffer->pages,
					buffer->pagecount, 0,
					buffer->pagecount << PAGE_SHIFT,
					GFP_KERNEL);
	if (ret) {
		kfree(a);
		return ret;
	}

	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);
	a->mapped = false;

	a->uncached = buffer->uncached;
	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void cma_heap_detach(struct dma_buf *dmabuf,
			    struct dma_buf_attachment *attachment)
{
	struct cma_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(&a->table);
	kfree(a);
}

static struct sg_table *cma_heap_map_dma_buf(struct dma_buf_attachment *attachment,
					     enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	struct sg_table *table = &a->table;
	int attrs = attachment->dma_map_attrs;
	int ret;

	if (a->uncached)
		attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	ret = dma_map_sgtable(attachment->dev, table, direction, attrs);
	if (ret)
		return ERR_PTR(-ENOMEM);
	a->mapped = true;
	return table;
}

static void cma_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
				   struct sg_table *table,
				   enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	int attrs = attachment->dma_map_attrs;

	a->mapped = false;

	if (a->uncached)
		attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	dma_unmap_sgtable(attachment->dev, table, direction, attrs);
}

static int
cma_heap_dma_buf_begin_cpu_access_partial(struct dma_buf *dmabuf,
					  enum dma_data_direction direction,
					  unsigned int offset,
					  unsigned int len)
{
	struct cma_heap_buffer *buffer = dmabuf->priv;
	phys_addr_t phys = page_to_phys(buffer->cma_pages);

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);

	if (buffer->uncached)
		return 0;

	mutex_lock(&buffer->lock);
	dma_sync_single_for_cpu(dma_heap_get_dev(buffer->heap->heap),
				phys + offset,
				len,
				direction);
	mutex_unlock(&buffer->lock);

	return 0;
}

static int
cma_heap_dma_buf_end_cpu_access_partial(struct dma_buf *dmabuf,
					enum dma_data_direction direction,
					unsigned int offset,
					unsigned int len)
{
	struct cma_heap_buffer *buffer = dmabuf->priv;
	phys_addr_t phys = page_to_phys(buffer->cma_pages);

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);

	if (buffer->uncached)
		return 0;

	mutex_lock(&buffer->lock);
	dma_sync_single_for_device(dma_heap_get_dev(buffer->heap->heap),
				   phys + offset,
				   len,
				   direction);
	mutex_unlock(&buffer->lock);

	return 0;
}

static int cma_heap_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					     enum dma_data_direction direction)
{
	struct cma_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);

	mutex_lock(&buffer->lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		if (!a->mapped)
			continue;
		dma_sync_sgtable_for_cpu(a->dev, &a->table, direction);
	}
	mutex_unlock(&buffer->lock);

	return 0;
}

static int cma_heap_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					   enum dma_data_direction direction)
{
	struct cma_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);

	mutex_lock(&buffer->lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		if (!a->mapped)
			continue;
		dma_sync_sgtable_for_device(a->dev, &a->table, direction);
	}
	mutex_unlock(&buffer->lock);

	return 0;
}

static vm_fault_t cma_heap_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct cma_heap_buffer *buffer = vma->vm_private_data;

	if (vmf->pgoff > buffer->pagecount)
		return VM_FAULT_SIGBUS;

	vmf->page = buffer->pages[vmf->pgoff];
	get_page(vmf->page);

	return 0;
}

static const struct vm_operations_struct dma_heap_vm_ops = {
	.fault = cma_heap_vm_fault,
};

static int cma_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct cma_heap_buffer *buffer = dmabuf->priv;

	if ((vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) == 0)
		return -EINVAL;

	if (buffer->uncached)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	vma->vm_ops = &dma_heap_vm_ops;
	vma->vm_private_data = buffer;

	return 0;
}

static void *cma_heap_do_vmap(struct cma_heap_buffer *buffer)
{
	void *vaddr;
	pgprot_t pgprot = PAGE_KERNEL;

	if (buffer->uncached)
		pgprot = pgprot_writecombine(PAGE_KERNEL);

	vaddr = vmap(buffer->pages, buffer->pagecount, VM_MAP, pgprot);
	if (!vaddr)
		return ERR_PTR(-ENOMEM);

	return vaddr;
}

static void *cma_heap_vmap(struct dma_buf *dmabuf)
{
	struct cma_heap_buffer *buffer = dmabuf->priv;
	void *vaddr;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		vaddr = buffer->vaddr;
		goto out;
	}

	vaddr = cma_heap_do_vmap(buffer);
	if (IS_ERR(vaddr))
		goto out;

	buffer->vaddr = vaddr;
	buffer->vmap_cnt++;
out:
	mutex_unlock(&buffer->lock);

	return vaddr;
}

static void cma_heap_vunmap(struct dma_buf *dmabuf, void *vaddr)
{
	struct cma_heap_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mutex_unlock(&buffer->lock);
}

static void cma_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct cma_heap_buffer *buffer = dmabuf->priv;
	struct cma_heap *cma_heap = buffer->heap;

	if (buffer->vmap_cnt > 0) {
		WARN(1, "%s: buffer still mapped in the kernel\n", __func__);
		vunmap(buffer->vaddr);
	}

	/* free page list */
	kfree(buffer->pages);
	/* release memory */
	cma_release(cma_heap->cma, buffer->cma_pages, buffer->pagecount);
	kfree(buffer);
}

static const struct dma_buf_ops cma_heap_buf_ops = {
	.attach = cma_heap_attach,
	.detach = cma_heap_detach,
	.map_dma_buf = cma_heap_map_dma_buf,
	.unmap_dma_buf = cma_heap_unmap_dma_buf,
	.begin_cpu_access = cma_heap_dma_buf_begin_cpu_access,
	.end_cpu_access = cma_heap_dma_buf_end_cpu_access,
	.begin_cpu_access_partial = cma_heap_dma_buf_begin_cpu_access_partial,
	.end_cpu_access_partial = cma_heap_dma_buf_end_cpu_access_partial,
	.mmap = cma_heap_mmap,
	.vmap = cma_heap_vmap,
	.vunmap = cma_heap_vunmap,
	.release = cma_heap_dma_buf_release,
};

static struct dma_buf *cma_heap_do_allocate(struct dma_heap *heap,
					 unsigned long len,
					 unsigned long fd_flags,
					 unsigned long heap_flags, bool uncached)
{
	struct cma_heap *cma_heap = dma_heap_get_drvdata(heap);
	struct cma_heap_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	size_t size = PAGE_ALIGN(len);
	pgoff_t pagecount = size >> PAGE_SHIFT;
	unsigned long align = get_order(size);
	struct page *cma_pages;
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;
	pgoff_t pg;
	dma_addr_t dma;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	buffer->uncached = uncached;

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->len = size;

	if (align > CONFIG_CMA_ALIGNMENT)
		align = CONFIG_CMA_ALIGNMENT;

	cma_pages = cma_alloc(cma_heap->cma, pagecount, align, GFP_KERNEL);
	if (!cma_pages)
		goto free_buffer;

	/* Clear the cma pages */
	if (PageHighMem(cma_pages)) {
		unsigned long nr_clear_pages = pagecount;
		struct page *page = cma_pages;

		while (nr_clear_pages > 0) {
			void *vaddr = kmap_atomic(page);

			memset(vaddr, 0, PAGE_SIZE);
			kunmap_atomic(vaddr);
			/*
			 * Avoid wasting time zeroing memory if the process
			 * has been killed by by SIGKILL
			 */
			if (fatal_signal_pending(current))
				goto free_cma;
			page++;
			nr_clear_pages--;
		}
	} else {
		memset(page_address(cma_pages), 0, size);
	}

	buffer->pages = kmalloc_array(pagecount, sizeof(*buffer->pages), GFP_KERNEL);
	if (!buffer->pages) {
		ret = -ENOMEM;
		goto free_cma;
	}

	for (pg = 0; pg < pagecount; pg++)
		buffer->pages[pg] = &cma_pages[pg];

	buffer->cma_pages = cma_pages;
	buffer->heap = cma_heap;
	buffer->pagecount = pagecount;

	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.ops = &cma_heap_buf_ops;
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pages;
	}

	if (buffer->uncached) {
		dma = dma_map_page(dma_heap_get_dev(heap), buffer->cma_pages, 0,
			     buffer->pagecount * PAGE_SIZE, DMA_FROM_DEVICE);
		dma_unmap_page(dma_heap_get_dev(heap), dma,
			       buffer->pagecount * PAGE_SIZE, DMA_FROM_DEVICE);
	}

	return dmabuf;

free_pages:
	kfree(buffer->pages);
free_cma:
	cma_release(cma_heap->cma, cma_pages, pagecount);
free_buffer:
	kfree(buffer);

	return ERR_PTR(ret);
}

static struct dma_buf *cma_heap_allocate(struct dma_heap *heap,
					 unsigned long len,
					 unsigned long fd_flags,
					 unsigned long heap_flags)
{
	return cma_heap_do_allocate(heap, len, fd_flags, heap_flags, false);
}

#if IS_ENABLED(CONFIG_NO_GKI)
static int cma_heap_get_phys(struct dma_heap *heap,
			     struct dma_heap_phys_data *phys)
{
	struct cma_heap *cma_heap = dma_heap_get_drvdata(heap);
	struct cma_heap_buffer *buffer;
	struct dma_buf *dmabuf;

	phys->paddr = (__u64)-1;

	if (IS_ERR_OR_NULL(phys))
		return -EINVAL;

	dmabuf = dma_buf_get(phys->fd);
	if (IS_ERR_OR_NULL(dmabuf))
		return -EBADFD;

	buffer = dmabuf->priv;
	if (IS_ERR_OR_NULL(buffer))
		goto err;

	if (buffer->heap != cma_heap)
		goto err;

	phys->paddr = page_to_phys(buffer->cma_pages);

err:
	dma_buf_put(dmabuf);

	return (phys->paddr == (__u64)-1) ? -EINVAL : 0;
}
#endif

static const struct dma_heap_ops cma_heap_ops = {
	.allocate = cma_heap_allocate,
#if IS_ENABLED(CONFIG_NO_GKI)
	.get_phys = cma_heap_get_phys,
#endif
};

static struct dma_buf *cma_uncached_heap_allocate(struct dma_heap *heap,
					 unsigned long len,
					 unsigned long fd_flags,
					 unsigned long heap_flags)
{
	return cma_heap_do_allocate(heap, len, fd_flags, heap_flags, true);
}

static struct dma_buf *cma_uncached_heap_not_initialized(struct dma_heap *heap,
					 unsigned long len,
					 unsigned long fd_flags,
					 unsigned long heap_flags)
{
	pr_info("heap %s not initialized\n", dma_heap_get_name(heap));
	return ERR_PTR(-EBUSY);
}

static struct dma_heap_ops cma_uncached_heap_ops = {
	.allocate = cma_uncached_heap_not_initialized,
};

static int set_heap_dev_dma(struct device *heap_dev)
{
	int err = 0;

	if (!heap_dev)
		return -EINVAL;

	dma_coerce_mask_and_coherent(heap_dev, DMA_BIT_MASK(64));

	if (!heap_dev->dma_parms) {
		heap_dev->dma_parms = devm_kzalloc(heap_dev,
						   sizeof(*heap_dev->dma_parms),
						   GFP_KERNEL);
		if (!heap_dev->dma_parms)
			return -ENOMEM;

		err = dma_set_max_seg_size(heap_dev, (unsigned int)DMA_BIT_MASK(64));
		if (err) {
			devm_kfree(heap_dev, heap_dev->dma_parms);
			dev_err(heap_dev, "Failed to set DMA segment size, err:%d\n", err);
			return err;
		}
	}

	return 0;
}

static int __add_cma_heap(struct cma *cma, void *data)
{
	struct cma_heap *cma_heap, *cma_uncached_heap;
	struct dma_heap_export_info exp_info;
	int ret;

	cma_heap = kzalloc(sizeof(*cma_heap), GFP_KERNEL);
	if (!cma_heap)
		return -ENOMEM;
	cma_heap->cma = cma;

	exp_info.name = "cma";
	exp_info.ops = &cma_heap_ops;
	exp_info.priv = cma_heap;

	cma_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR(cma_heap->heap)) {
		ret = PTR_ERR(cma_heap->heap);
		goto free_cma_heap;
	}

	cma_uncached_heap = kzalloc(sizeof(*cma_heap), GFP_KERNEL);
	if (!cma_uncached_heap) {
		ret = -ENOMEM;
		goto put_cma_heap;
	}

	cma_uncached_heap->cma = cma;

	exp_info.name = "cma-uncached";
	exp_info.ops = &cma_uncached_heap_ops;
	exp_info.priv = cma_uncached_heap;

	cma_uncached_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR(cma_uncached_heap->heap)) {
		ret = PTR_ERR(cma_uncached_heap->heap);
		goto free_uncached_cma_heap;
	}

	ret = set_heap_dev_dma(dma_heap_get_dev(cma_uncached_heap->heap));
	if (ret)
		goto put_uncached_cma_heap;

	mb(); /* make sure we only set allocate after dma_mask is set */
	cma_uncached_heap_ops.allocate = cma_uncached_heap_allocate;

	return 0;

put_uncached_cma_heap:
	dma_heap_put(cma_uncached_heap->heap);
free_uncached_cma_heap:
	kfree(cma_uncached_heap);
put_cma_heap:
	dma_heap_put(cma_heap->heap);
free_cma_heap:
	kfree(cma_heap);

	return ret;
}

static int add_default_cma_heap(void)
{
	struct cma *default_cma = dev_get_cma_area(NULL);
	int ret = 0;

	if (default_cma)
		ret = __add_cma_heap(default_cma, NULL);

	return ret;
}
module_init(add_default_cma_heap);
MODULE_DESCRIPTION("DMA-BUF CMA Heap");
MODULE_LICENSE("GPL");
